#include "account_management.h"
#include "account_cache.h"
#include "account_errors.h"
#include "account_index.h"
#include "character_json.h"
#include "exploits_json.h"
#include "json_utils.h"
#include "objects_json.h"
#include "roster_cache.h"
#include "utils.h"

#include <cerrno>
#include <crypt.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <limits>
#include <sstream>
#include <utility>

extern char* race_abbrevs[];

namespace account {
namespace {
    // Bounds the roster the account menu renders AND the set select_linked_character will match,
    // so a character is never displayed without being selectable or vice versa. Characters past
    // this point are unreachable by number and by name, so the limit is an access ceiling, not
    // just a display one. The hard ceiling is the descriptor output buffer: rows are 27 bytes at
    // their widest, two per line, and once a write exceeds LARGE_BUFSIZE write_to_output sets
    // bufptr = -1 and silently discards everything else bound for that socket, leaving the player
    // with a blank screen and no prompt. For the unsectioned sorts that cliff sits at 585 rows;
    // under Side sort the section headers and inter-section blank lines add overhead on top of the
    // same rows, so the cliff sits a few rows lower there. RendersAFullRosterWithinTheOutputBuffer
    // guards both the unsectioned Account-sort render and the sectioned Side-sort render at the
    // current cap (200 rows), which is comfortably under either cliff; it does not re-derive either
    // cliff's exact row count.
    constexpr size_t kMaxDisplayedAccountCharacters = 200;

    using CharacterLinkReference = AccountData::CharacterLinkReference;

    std::string trim_copy(const std::string& value)
    {
        size_t start = 0;
        while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start])))
            ++start;

        size_t end = value.size();
        while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1])))
            --end;

        return value.substr(start, end - start);
    }

    std::string to_lower_copy(const std::string& value)
    {
        std::string normalized = value;
        std::transform(normalized.begin(), normalized.end(), normalized.begin(),
            [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
        return normalized;
    }

    void set_error(std::string* error_message, const std::string& message)
    {
        if (error_message)
            *error_message = message;
    }

    std::string json_path_or_empty(const std::string& path)
    {
        return path.empty() ? "" : path;
    }

    std::string character_asset_slug(const std::string& character_name)
    {
        return normalize_account_name(character_name);
    }

    std::string character_json_file_name(const std::string& character_name)
    {
        return character_asset_slug(character_name) + ".character.json";
    }

    std::string objects_json_file_name(const std::string& character_name)
    {
        return character_asset_slug(character_name) + ".objects.json";
    }

    std::string exploits_json_file_name(const std::string& character_name)
    {
        return character_asset_slug(character_name) + ".exploits.json";
    }

    const char* safe_race_abbrev(int race)
    {
        if (race < 0 || race >= MAX_RACES + 40 || ::race_abbrevs[race] == nullptr)
            return "??";
        return ::race_abbrevs[race];
    }

    std::string format_account_character_short_entry(size_t display_row,
        const std::string& character_name, const roster_cache::RosterSummary& summary)
    {
        const std::string display_name = format_character_name_for_display(character_name);

        char line[256];
        if (!summary.readable) {
            std::snprintf(line, sizeof(line), "%zu) [ ?? ???] %-12.12s", display_row, display_name.c_str());
            return line;
        }

        std::snprintf(line, sizeof(line), "%zu) [%3d %s] %-12.12s", display_row,
            summary.level, safe_race_abbrev(summary.race), display_name.c_str());
        return line;
    }

    // Derived coefficient for one profession, mirroring get_prof_coof (char_utils.cpp) but reading
    // the cached raw value instead of a live char_data. Only the Uruk-mage -100 is profession-
    // specific, so it is the one that can change WHICH profession is highest -- filtering on the
    // raw value would put those characters under the wrong letter. The Orc (x*2+2)/3 scaling is
    // applied uniformly to every profession of the same character and is monotonic, so on its own
    // it can only collapse a near-tie into an exact tie (which, under >=, widens who matches); it
    // can never invert the ordering between two of an Orc's professions.
    int derived_prof_coof(const roster_cache::RosterSummary& summary, int profession)
    {
        const short raw = summary.prof_coof[profession];
        int derived = square_root[raw];
        if (summary.race == RACE_ORC)
            derived = (derived * 2 + 2) / 3;
        else if (summary.race == RACE_URUK && profession == PROF_MAGE)
            derived -= 100;
        return derived;
    }

    // Side ordering: gods, lights, darks, third side. Derived from race, never stored.
    int side_rank_for_race(int race)
    {
        if (race == RACE_GOD)
            return 0;
        if (race >= RACE_HUMAN && race <= RACE_BEORNING)
            return 1;
        if (race == RACE_MAGUS || race == RACE_HARADRIM)
            return 3;
        return 2;
    }

    // Side rank reserved for characters whose file could not be read. They have no race and so no
    // side; ordered_roster_indices already sorts them last, and this keeps them out of a real
    // side's section rather than silently padding one.
    constexpr int kUnreadableSideRank = 99;

    const char* side_section_label(int side_rank)
    {
        switch (side_rank) {
        case 0:
            return "Gods";
        case 1:
            return "Good";
        case 2:
            return "Evil";
        case 3:
            return "Third Side";
        default:
            return "Unavailable";
        }
    }

    int side_rank_for_summary(const roster_cache::RosterSummary& summary)
    {
        return summary.readable ? side_rank_for_race(summary.race) : kUnreadableSideRank;
    }

    bool summary_matches_filter(const roster_cache::RosterSummary& summary, RosterFilter filter)
    {
        if (filter == RosterFilter::None)
            return true;
        if (!summary.readable)
            return false; // no coefficients known; spec says unreadable rows are excluded by filters

        int wanted = PROF_WARRIOR;
        if (filter == RosterFilter::Ranger)
            wanted = PROF_RANGER;
        else if (filter == RosterFilter::Mystic)
            wanted = PROF_CLERIC;
        else if (filter == RosterFilter::Mage)
            wanted = PROF_MAGE;

        const int wanted_value = derived_prof_coof(summary, wanted);
        for (int profession = 1; profession <= MAX_PROFS; ++profession) {
            if (profession == wanted)
                continue;
            if (derived_prof_coof(summary, profession) > wanted_value)
                return false;
        }
        // >= every other profession, so ties match under every tied letter.
        return true;
    }

    const char* roster_filter_label(RosterFilter filter)
    {
        switch (filter) {
        case RosterFilter::Warrior:
            return "Warrior";
        case RosterFilter::Ranger:
            return "Ranger";
        case RosterFilter::Mystic:
            return "Mystic";
        case RosterFilter::Mage:
            return "Mage";
        default:
            return "";
        }
    }

    char roster_filter_key(RosterFilter filter)
    {
        switch (filter) {
        case RosterFilter::Warrior:
            return 'W';
        case RosterFilter::Ranger:
            return 'R';
        case RosterFilter::Mystic:
            return 'T';
        case RosterFilter::Mage:
            return 'M';
        default:
            return ' ';
        }
    }

    std::string format_account_character_short_roster(const std::string& root_directory,
        const AccountData& account, RosterSort sort, RosterFilter filter)
    {
        if (account.characters.empty())
            return "\n\rNo linked characters yet.\n\r";

        const std::vector<size_t> indices = ordered_roster_indices(root_directory, account, sort, filter);
        if (indices.empty())
            return "\n\rNo linked characters match that filter.\n\r";

        std::ostringstream output;
        // Column position WITHIN the current section, so a section holding an odd number of rows
        // does not drag the next section out of alignment. Row NUMBERING stays continuous across
        // sections regardless -- row N must still select the character printed at row N.
        size_t column = 0;
        int previous_side_rank = -1;
        for (size_t row = 0; row < indices.size(); ++row) {
            roster_cache::RosterSummary summary {};
            roster_cache::get(root_directory, account.account_name, account.characters[indices[row]], &summary);

            if (sort == RosterSort::Side) {
                const int side_rank = side_rank_for_summary(summary);
                if (side_rank != previous_side_rank) {
                    if (column % 2 != 0)
                        output << "\n\r";
                    if (previous_side_rank != -1)
                        output << "\n\r";
                    output << "-- " << side_section_label(side_rank) << " --\n\r";
                    previous_side_rank = side_rank;
                    column = 0;
                }
            }

            output << format_account_character_short_entry(row + 1, account.characters[indices[row]], summary);
            ++column;
            if (column % 2 == 0)
                output << "\n\r";
        }

        if (column % 2 != 0)
            output << "\n\r";

        output << "\n\r";
        if (filter != RosterFilter::None) {
            // Never let a filtered roster be mistaken for the whole roster.
            output << indices.size() << " of " << account.characters.size()
                   << " characters shown (" << roster_filter_label(filter) << ").  Press "
                   << roster_filter_key(filter) << " to clear.\n\r";
        } else {
            if (account.characters.size() > indices.size())
                output << "... and " << (account.characters.size() - indices.size()) << " more\n\r\n\r";
            output << indices.size() << " character" << (indices.size() == 1 ? "" : "s") << " displayed.\n\r";
        }
        return output.str();
    }

    std::string read_secure_random_bytes(size_t byte_count)
    {
        std::string bytes(byte_count, '\0');
        FILE* file = std::fopen("/dev/urandom", "r");
        if (file == nullptr)
            return "";

        const size_t bytes_read = std::fread(bytes.data(), sizeof(char), byte_count, file);
        std::fclose(file);
        if (bytes_read != byte_count)
            return "";

        return bytes;
    }

    std::string encode_salt(const std::string& random_bytes)
    {
        static constexpr char kSaltAlphabet[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789./";

        std::string salt;
        salt.reserve(random_bytes.size());
        for (unsigned char value : random_bytes)
            salt += kSaltAlphabet[value % (sizeof(kSaltAlphabet) - 1)];

        return salt;
    }

    bool generate_hash_for_secret(const std::string& secret, std::string* secret_hash, std::string* secret_salt, std::string* error_message)
    {
        if (secret_hash == nullptr || secret_salt == nullptr) {
            set_error(error_message, "Secret hash and salt outputs must not be null.");
            return false;
        }

        const std::string random_bytes = read_secure_random_bytes(16);
        if (random_bytes.empty()) {
            set_error(error_message, "Failed to read secure random bytes for credential generation.");
            return false;
        }

        *secret_salt = encode_salt(random_bytes);
        const std::string salt_spec = "$6$" + *secret_salt + "$";
        char* hashed_secret = crypt(secret.c_str(), salt_spec.c_str());
        if (hashed_secret == nullptr) {
            set_error(error_message, "Failed to hash credential.");
            return false;
        }

        *secret_hash = hashed_secret;
        set_error(error_message, "");
        return true;
    }

    std::string generate_numeric_verification_code()
    {
        const std::string random_bytes = read_secure_random_bytes(4);
        if (random_bytes.size() != 4)
            return "";

        unsigned int value = 0;
        for (unsigned char byte : random_bytes)
            value = (value << 8) | byte;

        char code[7];
        std::snprintf(code, sizeof(code), "%06u", value % 1000000);
        return code;
    }

    std::string configured_sendmail_command()
    {
        const char* configured_command = std::getenv("ROTS_SENDMAIL_COMMAND");
        if (configured_command == nullptr)
            return "/usr/sbin/sendmail -t -oi";

        const std::string trimmed_command = trim_copy(configured_command);
        if (trimmed_command.empty())
            return "/usr/sbin/sendmail -t -oi";

        return trimmed_command;
    }

    bool split_command_arguments(const std::string& command, std::vector<std::string>* arguments, std::string* error_message)
    {
        if (arguments == nullptr) {
            set_error(error_message, "Sendmail command arguments output must not be null.");
            return false;
        }

        arguments->clear();
        std::string current_argument;
        bool in_single_quotes = false;
        bool in_double_quotes = false;
        bool escaping = false;

        for (char character : command) {
            if (escaping) {
                current_argument += character;
                escaping = false;
                continue;
            }

            if (character == '\\') {
                escaping = true;
                continue;
            }

            if (in_single_quotes) {
                if (character == '\'')
                    in_single_quotes = false;
                else
                    current_argument += character;
                continue;
            }

            if (in_double_quotes) {
                if (character == '"')
                    in_double_quotes = false;
                else
                    current_argument += character;
                continue;
            }

            if (character == '\'') {
                in_single_quotes = true;
                continue;
            }

            if (character == '"') {
                in_double_quotes = true;
                continue;
            }

            if (std::isspace(static_cast<unsigned char>(character))) {
                if (!current_argument.empty()) {
                    arguments->push_back(current_argument);
                    current_argument.clear();
                }
                continue;
            }

            current_argument += character;
        }

        if (escaping || in_single_quotes || in_double_quotes) {
            set_error(error_message, "The configured sendmail command contains unmatched quoting or escaping.");
            return false;
        }

        if (!current_argument.empty())
            arguments->push_back(current_argument);

        if (arguments->empty()) {
            set_error(error_message, "The configured sendmail command must not be empty.");
            return false;
        }

        set_error(error_message, "");
        return true;
    }

    bool send_email_message(const std::string& recipient, const std::string& subject, const std::string& body, std::string* error_message)
    {
        const std::string sendmail_command = configured_sendmail_command();
        std::vector<std::string> sendmail_arguments;
        if (!split_command_arguments(sendmail_command, &sendmail_arguments, error_message))
            return false;

        int pipe_fds[2];
        if (pipe(pipe_fds) != 0) {
            set_error(error_message, "Failed to create an email-delivery pipe.");
            return false;
        }

        pid_t child_pid = fork();
        if (child_pid < 0) {
            close(pipe_fds[0]);
            close(pipe_fds[1]);
            set_error(error_message, "Failed to fork the email-delivery process.");
            return false;
        }

        if (child_pid == 0) {
            if (dup2(pipe_fds[0], STDIN_FILENO) < 0)
                _exit(127);

            close(pipe_fds[0]);
            close(pipe_fds[1]);
            std::vector<char*> sendmail_argv;
            sendmail_argv.reserve(sendmail_arguments.size() + 1);
            for (std::string& argument : sendmail_arguments)
                sendmail_argv.push_back(argument.data());
            sendmail_argv.push_back(nullptr);

            execvp(sendmail_argv[0], sendmail_argv.data());
            _exit(127);
        }

        close(pipe_fds[0]);
        std::ostringstream message;
        message << "To: " << recipient << "\n";
        message << "From: RotS Account Verification <noreply@rotsmud.org>\n";
        message << "Subject: " << subject << "\n\n";
        message << body << "\n";

        const std::string message_text = message.str();
        size_t write_offset = 0;
        while (write_offset < message_text.size()) {
            const ssize_t bytes_written = write(pipe_fds[1], message_text.data() + write_offset, message_text.size() - write_offset);
            if (bytes_written < 0) {
                if (errno == EINTR)
                    continue;

                close(pipe_fds[1]);
                int status = 0;
                while (waitpid(child_pid, &status, 0) < 0 && errno == EINTR) { }
                set_error(error_message, "Failed to write the verification email to the delivery process: " + std::string(std::strerror(errno)));
                return false;
            }

            write_offset += static_cast<size_t>(bytes_written);
        }

        if (close(pipe_fds[1]) != 0) {
            int status = 0;
            while (waitpid(child_pid, &status, 0) < 0 && errno == EINTR) { }
            set_error(error_message, "Failed to close the verification email stream cleanly: " + std::string(std::strerror(errno)));
            return false;
        }

        int child_status = 0;
        while (waitpid(child_pid, &child_status, 0) < 0) {
            if (errno == EINTR)
                continue;

            set_error(error_message, "Failed to wait for the email-delivery process: " + std::string(std::strerror(errno)));
            return false;
        }

        if (!WIFEXITED(child_status) || WEXITSTATUS(child_status) != 0) {
            if (WIFEXITED(child_status)) {
                set_error(error_message, "sendmail reported a delivery failure with exit code " + std::to_string(WEXITSTATUS(child_status)) + ".");
            } else if (WIFSIGNALED(child_status)) {
                set_error(error_message, "sendmail reported a delivery failure after signal " + std::to_string(WTERMSIG(child_status)) + ".");
            } else {
                set_error(error_message, "sendmail reported a delivery failure with unexpected process status.");
            }
            return false;
        }

        set_error(error_message, "");
        return true;
    }

    bool send_verification_email(const AccountData& account, const std::string& verification_code, std::string* error_message)
    {
        std::ostringstream body;
        body << "A verification code was requested for your RotS account.\n\n";
        body << "Email: " << account.normalized_email << "\n";
        body << "Verification code: " << verification_code << "\n";
        body << "This code is valid for 15 minutes.\n\n";
        body << "If you did not request this code, you can ignore this email.";

        return send_email_message(account.normalized_email, "RotS account verification code", body.str(), error_message);
    }

    bool send_password_reset_email(const AccountData& account, const std::string& reset_code, std::string* error_message)
    {
        std::ostringstream body;
        body << "A password reset was requested for your RotS account.\n\n";
        body << "Email: " << account.normalized_email << "\n";
        body << "Password reset code: " << reset_code << "\n";
        body << "This code is valid for 15 minutes.\n\n";
        body << "If you did not request this reset, you can ignore this email. Your password has\n";
        body << "not been changed.";

        return send_email_message(account.normalized_email, "RotS account password reset code", body.str(), error_message);
    }

    std::string hex_encode(const std::string& bytes)
    {
        static constexpr char kHexDigits[] = "0123456789abcdef";

        std::string encoded;
        encoded.reserve(bytes.size() * 2);
        for (unsigned char byte : bytes) {
            encoded += kHexDigits[(byte >> 4) & 0x0F];
            encoded += kHexDigits[byte & 0x0F];
        }

        return encoded;
    }

    bool decode_hex_digit(char character, unsigned char* value)
    {
        if (character >= '0' && character <= '9') {
            *value = static_cast<unsigned char>(character - '0');
            return true;
        }

        if (character >= 'a' && character <= 'f') {
            *value = static_cast<unsigned char>(character - 'a' + 10);
            return true;
        }

        if (character >= 'A' && character <= 'F') {
            *value = static_cast<unsigned char>(character - 'A' + 10);
            return true;
        }

        return false;
    }

    bool hex_decode(const std::string& encoded, std::string* bytes, std::string* error_message)
    {
        if ((encoded.size() % 2) != 0) {
            set_error(error_message, "Hex-encoded content must contain an even number of characters.");
            return false;
        }

        std::string decoded;
        decoded.reserve(encoded.size() / 2);
        for (size_t index = 0; index < encoded.size(); index += 2) {
            unsigned char high = 0;
            unsigned char low = 0;
            if (!decode_hex_digit(encoded[index], &high) || !decode_hex_digit(encoded[index + 1], &low)) {
                set_error(error_message, "Hex-encoded content contains invalid characters.");
                return false;
            }

            decoded += static_cast<char>((high << 4) | low);
        }

        *bytes = decoded;
        set_error(error_message, "");
        return true;
    }

    bool create_directory_if_missing(const std::string& path, std::string* error_message)
    {
        if (mkdir(path.c_str(), 0700) == 0 || errno == EEXIST)
            return true;

        set_error(error_message, "Failed to create directory '" + path + "': " + std::strerror(errno));
        return false;
    }

    FILE* open_secure_output_file(const std::string& path, std::string* error_message)
    {
        const int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
        if (fd < 0) {
            set_error(error_message, "Failed to open output file '" + path + "': " + std::strerror(errno));
            return nullptr;
        }

        FILE* file = fdopen(fd, "w");
        if (file == nullptr) {
            close(fd);
            set_error(error_message, "Failed to create stream for output file '" + path + "': " + std::strerror(errno));
            return nullptr;
        }

        return file;
    }

    bool path_exists(const std::string& path)
    {
        struct stat file_info { };
        return stat(path.c_str(), &file_info) == 0;
    }

    bool inspect_path_existence(const std::string& path, const char* description, bool* exists, std::string* error_message)
    {
        if (exists == nullptr) {
            set_error(error_message, std::string(description) + " existence output parameter must not be null.");
            return false;
        }

        struct stat file_info { };
        if (stat(path.c_str(), &file_info) == 0) {
            *exists = true;
            set_error(error_message, "");
            return true;
        }

        if (errno == ENOENT) {
            *exists = false;
            set_error(error_message, "");
            return true;
        }

        set_error(error_message, "Failed to inspect " + std::string(description) + " '" + path + "': " + std::strerror(errno));
        return false;
    }

    bool validate_identifier_for_path(const std::string& value, const char* identifier_label, std::string* error_message)
    {
        if (!is_valid_account_name(value, error_message)) {
            if (error_message && !error_message->empty())
                *error_message = std::string(identifier_label) + " " + *error_message;
            return false;
        }

        return true;
    }

    bool read_account_file_from_path(const std::string& path, AccountData* account, std::string* error_message)
    {
        FILE* file = std::fopen(path.c_str(), "r");
        if (file == nullptr) {
            set_error(error_message, "Failed to open account file '" + path + "': " + std::strerror(errno));
            return false;
        }

        std::string json;
        char buffer[1024];
        while (true) {
            const size_t bytes_read = std::fread(buffer, sizeof(char), sizeof(buffer), file);
            if (bytes_read > 0)
                json.append(buffer, bytes_read);

            if (bytes_read < sizeof(buffer)) {
                if (std::ferror(file)) {
                    std::fclose(file);
                    set_error(error_message, "Failed to read account file '" + path + "'.");
                    return false;
                }
                break;
            }
        }

        std::fclose(file);
        return deserialize_account_from_json(json, account, error_message);
    }

    std::string account_directory_path_from_email(const std::string& root_directory, const std::string& email)
    {
        const std::string normalized_email = normalize_email(email);
        return root_directory + "/accounts/" + account_bucket_for_name(normalized_email) + "/" + normalized_email;
    }

    std::string account_file_path_from_email(const std::string& root_directory, const std::string& email)
    {
        return account_directory_path_from_email(root_directory, email) + "/account.json";
    }

    std::string legacy_account_file_path_from_account_name(const std::string& root_directory, const std::string& account_name)
    {
        const std::string normalized_name = normalize_account_name(account_name);
        return root_directory + "/accounts/" + account_bucket_for_name(normalized_name) + "/" + normalized_name + ".json";
    }

    std::string resolve_account_storage_key(const std::string& root_directory, const std::string& account_identifier)
    {
        if (account_identifier.find('@') != std::string::npos) {
            if (!is_valid_email(account_identifier, nullptr))
                return "";
            return normalize_email(account_identifier);
        }

        if (!validate_identifier_for_path(account_identifier, "Account name", nullptr))
            return "";

        // Index fast path: the storage key is the email that keys the record, which the index
        // already holds. Deliberately NOT parsed back out of the record path -- for a legacy flat
        // record (accounts/<bucket>/<name>.json) the parent directory is the bucket, so that would
        // silently yield "A-E" as an account's storage key. Guarded on the root the index was built
        // against: its paths and keys are meaningful only for that tree, so any other root falls
        // through to the scan below.
        if (account_index::is_enabled() && account_index::matches_root(root_directory)) {
            std::string storage_key;
            if (!account_index::find_email_by_account_name(account_identifier, &storage_key, nullptr))
                return "";
            return storage_key;
        }

        AccountData stored_account;
        if (read_account_file(root_directory, account_identifier, &stored_account, nullptr))
            return normalize_email(stored_account.normalized_email);

        return "";
    }

    // --- What a bucket entry IS ------------------------------------------------------------------
    //
    // Decided in exactly one place, because two walkers depend on the answer and must not disagree.
    // for_each_account_record_on_disk (account_management_storage.cpp) decides what the INDEX sees;
    // account_storage_contains_unreadable_records decides whether account creation is allowed at
    // all. Every historical disagreement between them produced the same outcome: a record the guard
    // called unreadable that nothing quarantined, so neither of the guard's escape hatches fired and
    // EVERY registration on the server was refused, permanently, with nothing logged and `account
    // index` showing 0 quarantined.
    enum class BucketEntryKind {
        // Filesystem litter. Never visited by the enumerator (so it cannot count towards
        // MAX_QUARANTINED_RECORDS_AT_BOOT) and never a record the creation guard may refuse over.
        NotARecord,
        // The directory layout: <bucket>/<email>/account.json.
        DirectoryRecord,
        // The legacy flat layout: <bucket>/<name>.json, a regular file.
        FlatRecord,
        // Candidate-shaped but we could not even stat it: EACCES from a bad umask on a manual SFTP
        // deploy or a partial restore, ELOOP, EIO, ENAMETOOLONG. This is a record we cannot read,
        // NOT litter. ENOENT is the only errno that means "this name does not resolve to anything",
        // and that one lands in NotARecord above.
        Unreadable,
    };

    struct BucketEntryClassification {
        BucketEntryKind kind = BucketEntryKind::NotARecord;
        // The JSON that holds the record. For Unreadable it is whichever path the stat actually
        // failed on, so the quarantine key and the log line name the thing that could not be read.
        std::string record_path;
        // Set only for Unreadable.
        std::string failure_reason;
        // Whether the entry is KNOWN to be a directory. False for an entry we could not stat at all
        // -- we genuinely do not know -- which is what keeps the quarantine key path-shaped for that
        // case; see account_index_quarantine_key.
        bool directory_layout = false;
    };

    BucketEntryClassification classify_bucket_entry(const std::string& bucket_path, const char* entry_name)
    {
        BucketEntryClassification classification;

        // Hidden entries are litter to BOTH walkers. "." and ".." are the obvious pair, but the ones
        // that actually turn up are a macOS AppleDouble "._account.json" left by an rsync, an editor
        // swap file, a .DS_Store. The enumerator has always skipped every dotfile while the guard
        // skipped only "." and "..", so one stray AppleDouble refused every registration on the box.
        if (entry_name == nullptr || entry_name[0] == '.')
            return classification;

        const std::string entry_path = bucket_path + "/" + entry_name;
        struct stat entry_info { };
        if (stat(entry_path.c_str(), &entry_info) != 0) {
            // ENOENT means the name readdir handed us does not resolve to anything -- in practice a
            // dangling symlink, since stat() follows the link. That is litter.
            if (errno == ENOENT)
                return classification;

            classification.kind = BucketEntryKind::Unreadable;
            classification.record_path = entry_path;
            classification.failure_reason = "Failed to stat account record '" + entry_path + "': " + std::strerror(errno);
            return classification;
        }

        if (S_ISDIR(entry_info.st_mode)) {
            classification.directory_layout = true;
            const std::string account_json_path = entry_path + "/account.json";
            struct stat account_json_info { };
            if (stat(account_json_path.c_str(), &account_json_info) != 0) {
                // A directory with no account.json yet is a normal transient artifact:
                // write_account_file creates the account directory before it writes the temp file,
                // so a crash between those two steps leaves one behind. Litter.
                if (errno == ENOENT) {
                    classification.directory_layout = false;
                    return classification;
                }

                // Anything else -- most reachably an account directory whose search bit a bad umask
                // or a partial restore stripped -- is a real player's record we cannot read. Keeping
                // directory_layout true is what makes the quarantine key the email directory's own
                // name, so the owner's ADDRESS stays reserved rather than being handed to the next
                // person who registers with it.
                classification.kind = BucketEntryKind::Unreadable;
                classification.record_path = account_json_path;
                classification.failure_reason = "Failed to stat account file '" + account_json_path + "': " + std::strerror(errno);
                return classification;
            }

            classification.kind = BucketEntryKind::DirectoryRecord;
            classification.record_path = account_json_path;
            return classification;
        }

        // S_ISREG, not merely "not a directory": a fifo or a device node named "x.json" is litter,
        // not a record either walker can read.
        if (!S_ISREG(entry_info.st_mode))
            return classification;

        const std::string file_name = entry_name;
        if (!(file_name.length() >= 6 && file_name.compare(file_name.length() - 5, 5, ".json") == 0))
            return classification;

        classification.kind = BucketEntryKind::FlatRecord;
        classification.record_path = entry_path;
        return classification;
    }

    bool read_account_file_from_bucket_entry(const std::string& bucket_path, const dirent& account_entry, AccountData* account, std::string* error_message)
    {
        const BucketEntryClassification classification = classify_bucket_entry(bucket_path, account_entry.d_name);
        switch (classification.kind) {
        case BucketEntryKind::NotARecord:
            // The sentinel every caller keys off. Returning false with error_message untouched made
            // account_storage_contains_unreadable_records treat litter as a record it could not
            // read, which refuses EVERY new account.
            set_error(error_message, "Entry is not an account record.");
            return false;
        case BucketEntryKind::Unreadable:
            set_error(error_message, classification.failure_reason);
            return false;
        case BucketEntryKind::DirectoryRecord:
        case BucketEntryKind::FlatRecord:
            break;
        }

        return read_account_file_from_path(classification.record_path, account, error_message);
    }

    bool is_directory_bucket_entry(const std::string& bucket_path, const dirent& account_entry)
    {
        // Expressed through the shared classifier rather than a second stat with its own rule --
        // the two walkers disagreeing about "is this a directory record" is the same class of bug
        // as them disagreeing about "is this a record at all".
        return classify_bucket_entry(bucket_path, account_entry.d_name).directory_layout;
    }

    bool find_account_file_path_by_account_name(const std::string& root_directory, const std::string& account_name, std::string* account_path, std::string* error_message)
    {
        if (account_path == nullptr) {
            set_error(error_message, "Account-path output parameter must not be null.");
            return false;
        }

        // Index fast path. The scan below is what runs when the index is not authoritative for
        // this root: the test binary, which never calls boot_db, and any caller working against a
        // tree other than the one the index was built for.
        // account_index::find_path_by_account_name reproduces this function's not-found text
        // verbatim, and the path it returns may end in "<name>.json" for a legacy flat record --
        // exactly as the scan's own directory-over-flat precedence would return it.
        if (account_index::is_enabled() && account_index::matches_root(root_directory)) {
            std::string indexed_path;
            if (!account_index::find_path_by_account_name(account_name, &indexed_path, error_message))
                return false;

            *account_path = indexed_path;
            set_error(error_message, "");
            return true;
        }

        const std::string normalized_account_name = normalize_account_name(account_name);
        const std::string accounts_directory = root_directory + "/accounts";
        DIR* accounts_dir = opendir(accounts_directory.c_str());
        if (accounts_dir == nullptr) {
            set_error(error_message, "Failed to open account file for account '" + normalized_account_name + "': " + std::strerror(errno));
            return false;
        }

        bool found_match = false;
        bool matched_is_directory = false;
        std::string matched_path;
        while (dirent* bucket_entry = readdir(accounts_dir)) {
            if (std::strcmp(bucket_entry->d_name, ".") == 0 || std::strcmp(bucket_entry->d_name, "..") == 0)
                continue;

            const std::string bucket_path = accounts_directory + "/" + bucket_entry->d_name;
            struct stat bucket_info { };
            if (stat(bucket_path.c_str(), &bucket_info) != 0 || !S_ISDIR(bucket_info.st_mode))
                continue;

            DIR* bucket_dir = opendir(bucket_path.c_str());
            if (bucket_dir == nullptr) {
                closedir(accounts_dir);
                set_error(error_message, "Failed to open account bucket directory '" + bucket_path + "': " + std::strerror(errno));
                return false;
            }

            while (dirent* account_entry = readdir(bucket_dir)) {
                if (std::strcmp(account_entry->d_name, ".") == 0 || std::strcmp(account_entry->d_name, "..") == 0)
                    continue;

                AccountData stored_account;
                std::string read_error;
                if (!read_account_file_from_bucket_entry(bucket_path, *account_entry, &stored_account, &read_error))
                    continue;

                if (stored_account.account_name != normalized_account_name)
                    continue;

                const std::string entry_path = bucket_path + "/" + account_entry->d_name;
                const bool candidate_is_directory = is_directory_bucket_entry(bucket_path, *account_entry);
                const std::string candidate_path = candidate_is_directory ? (entry_path + "/account.json") : entry_path;
                if (found_match) {
                    AccountData matched_account;
                    std::string matched_read_error;
                    if (!read_account_file_from_path(matched_path, &matched_account, &matched_read_error)) {
                        closedir(bucket_dir);
                        closedir(accounts_dir);
                        set_error(error_message, matched_read_error);
                        return false;
                    }

                    if (matched_account.account_name == stored_account.account_name && matched_account.normalized_email == stored_account.normalized_email) {
                        if (candidate_is_directory && !matched_is_directory) {
                            matched_path = candidate_path;
                            matched_is_directory = true;
                        }
                        continue;
                    }

                    closedir(bucket_dir);
                    closedir(accounts_dir);
                    set_error(error_message, "Multiple account records exist for account '" + normalized_account_name + "'.");
                    return false;
                }

                matched_path = candidate_path;
                found_match = true;
                matched_is_directory = candidate_is_directory;
            }

            closedir(bucket_dir);
        }

        closedir(accounts_dir);
        if (!found_match) {
            set_error(error_message, "Failed to open account file for account '" + normalized_account_name + "': " + std::strerror(ENOENT));
            return false;
        }

        *account_path = matched_path;
        set_error(error_message, "");
        return true;
    }

    bool find_character_owner_account(const std::string& root_directory, const std::string& character_name, std::string* owner_account_name, std::string* error_message)
    {
        if (owner_account_name == nullptr) {
            set_error(error_message, "Owner-account output parameter must not be null.");
            return false;
        }

        owner_account_name->clear();

        const std::string accounts_directory = root_directory + "/accounts";
        DIR* accounts_dir = opendir(accounts_directory.c_str());
        if (accounts_dir == nullptr) {
            if (errno == ENOENT) {
                set_error(error_message, "");
                return true;
            }

            set_error(error_message, "Failed to open accounts directory '" + accounts_directory + "': " + std::strerror(errno));
            return false;
        }

        bool found_match = false;
        while (dirent* bucket_entry = readdir(accounts_dir)) {
            if (std::strcmp(bucket_entry->d_name, ".") == 0 || std::strcmp(bucket_entry->d_name, "..") == 0)
                continue;

            const std::string bucket_path = accounts_directory + "/" + bucket_entry->d_name;
            struct stat bucket_info { };
            if (stat(bucket_path.c_str(), &bucket_info) != 0 || !S_ISDIR(bucket_info.st_mode))
                continue;

            DIR* bucket_dir = opendir(bucket_path.c_str());
            if (bucket_dir == nullptr) {
                closedir(accounts_dir);
                set_error(error_message, "Failed to open account bucket directory '" + bucket_path + "': " + std::strerror(errno));
                return false;
            }

            while (dirent* account_entry = readdir(bucket_dir)) {
                if (std::strcmp(account_entry->d_name, ".") == 0 || std::strcmp(account_entry->d_name, "..") == 0)
                    continue;

                AccountData stored_account;
                std::string read_error;
                if (!read_account_file_from_bucket_entry(bucket_path, *account_entry, &stored_account, &read_error)) {
                    if (read_error == "Entry is not an account record.")
                        continue;
                    closedir(bucket_dir);
                    closedir(accounts_dir);
                    set_error(error_message, read_error);
                    return false;
                }

                if (account_has_character(stored_account, character_name)) {
                    if (found_match) {
                        closedir(bucket_dir);
                        closedir(accounts_dir);
                        set_error(error_message, "Multiple account records claim that linked character.");
                        return false;
                    }

                    *owner_account_name = stored_account.account_name;
                    found_match = true;
                }
            }

            closedir(bucket_dir);
        }

        closedir(accounts_dir);
        set_error(error_message, "");
        return true;
    }

    // A record the index resolved but whose file will not read. This is the ordinary shape of
    // post-boot corruption and it is NOT reached by the write chokepoint: a login reads the record
    // before it ever writes one, so a record that cannot be read fails here and the write never
    // happens. Marking only at the write site therefore missed the common case entirely -- proved
    // live: a corrupted record produced "Expected string value." at the email prompt while
    // `account index` still reported 0 unreadable.
    //
    // Report-only, and once per record. See account_index::note_unreadable_at_runtime for why this
    // must not quarantine.
    void note_unreadable_record_at_runtime(const std::string& record_key, const std::string& record_path, const std::string& reason)
    {
        if (record_key.empty())
            return;
        if (!account_index::note_unreadable_at_runtime(record_key, reason.empty() ? "Account record could not be read." : reason))
            return;

        char log_buffer[MAX_STRING_LENGTH];
        std::snprintf(log_buffer, sizeof(log_buffer),
            "Account record '%s' could not be read after boot: %s (the account is still indexed; `account index` lists it)",
            record_path.c_str(), reason.empty() ? "Account record could not be read." : reason.c_str());
        log(log_buffer);
        mudlog(log_buffer, BRF, LEVEL_IMMORT, TRUE);
    }

    bool find_account_by_email_internal(const std::string& root_directory, const std::string& email, AccountData* account, std::string* error_message)
    {
        if (account == nullptr) {
            set_error(error_message, "Account output parameter must not be null.");
            return false;
        }

        // Index fast path -- this is the change that removes the full accounts/ scan from every
        // login. The record itself is still read from disk (the index holds keys only), and
        // read_account_file_from_path handles both on-disk layouts, so a legacy flat path needs no
        // special casing. The unknown-email text matches this function's own below verbatim;
        // interpre.cpp string-compares against it. Falls through to the scan for a root the index
        // was not built against.
        if (account_index::is_enabled() && account_index::matches_root(root_directory)) {
            std::string indexed_path;
            if (!account_index::find_path_by_email(email, &indexed_path, error_message)) {
                // A record can APPEAR after boot. lib/accounts is in no backup of its own, so a
                // hand restore while the game is up is the normal way it gets repaired -- and
                // nothing walks accounts/ again after the boot sweep. Handing the index's miss
                // straight back tells interpre.cpp the address is free, which offers the player
                // registration over the restored record, and write_account_file's occupancy check
                // does not fire because the derived account name matches the record's own.
                //
                // One stat, not a walk: the path for an email is deterministic, so this cannot
                // reintroduce the quadratic the index exists to remove.
                const std::string normalized_email_key = normalize_email(email);
                if (account_index::is_quarantined(normalized_email_key)
                    || account_index::is_contested_email(normalized_email_key)) {
                    // Already answered, deliberately, and must not be re-adopted here.
                    return false;
                }

                const std::string appeared_path = account_file_path_from_email(root_directory, normalized_email_key);
                AccountData appeared_account;
                std::string appeared_error;
                if (!path_exists(appeared_path)
                    || !read_account_file_from_path(appeared_path, &appeared_account, &appeared_error)
                    || normalize_email(appeared_account.normalized_email) != normalized_email_key)
                    return false;

                // Adopt it, so the resolvers, the creation guards and save_char all agree from here
                // on rather than each rediscovering it.
                account_index::upsert(appeared_account, appeared_path);
                *account = appeared_account;
                set_error(error_message, "");
                return true;
            }
            std::string read_error;
            if (read_account_file_from_path(indexed_path, account, &read_error)) {
                set_error(error_message, "");
                return true;
            }
            note_unreadable_record_at_runtime(normalize_email(email), indexed_path, read_error);
            set_error(error_message, read_error);
            return false;
        }

        const std::string normalized_email = normalize_email(email);
        const std::string accounts_directory = root_directory + "/accounts";
        DIR* accounts_dir = opendir(accounts_directory.c_str());
        if (accounts_dir == nullptr) {
            if (errno == ENOENT) {
                set_error(error_message, "No account exists for that email address.");
                return false;
            }

            set_error(error_message, "Failed to open accounts directory '" + accounts_directory + "': " + std::strerror(errno));
            return false;
        }

        bool found_match = false;
        bool matched_is_directory = false;
        AccountData matched_account;
        while (dirent* bucket_entry = readdir(accounts_dir)) {
            if (std::strcmp(bucket_entry->d_name, ".") == 0 || std::strcmp(bucket_entry->d_name, "..") == 0)
                continue;

            const std::string bucket_path = accounts_directory + "/" + bucket_entry->d_name;
            struct stat bucket_info { };
            if (stat(bucket_path.c_str(), &bucket_info) != 0 || !S_ISDIR(bucket_info.st_mode))
                continue;

            DIR* bucket_dir = opendir(bucket_path.c_str());
            if (bucket_dir == nullptr) {
                closedir(accounts_dir);
                set_error(error_message, "Failed to open account bucket directory '" + bucket_path + "': " + std::strerror(errno));
                return false;
            }

            while (dirent* account_entry = readdir(bucket_dir)) {
                if (std::strcmp(account_entry->d_name, ".") == 0 || std::strcmp(account_entry->d_name, "..") == 0)
                    continue;

                AccountData stored_account;
                std::string read_error;
                if (!read_account_file_from_bucket_entry(bucket_path, *account_entry, &stored_account, &read_error)) {
                    if (read_error == "Entry is not an account record.")
                        continue;
                    continue;
                }

                if (stored_account.normalized_email == normalized_email) {
                    if (found_match) {
                        if (matched_account.account_name == stored_account.account_name && matched_account.normalized_email == stored_account.normalized_email) {
                            const bool candidate_is_directory = is_directory_bucket_entry(bucket_path, *account_entry);
                            if (candidate_is_directory && !matched_is_directory) {
                                matched_account = stored_account;
                                matched_is_directory = true;
                            }
                            continue;
                        }

                        closedir(bucket_dir);
                        closedir(accounts_dir);
                        set_error(error_message, "Multiple account records exist for that email address.");
                        return false;
                    }

                    matched_account = stored_account;
                    found_match = true;
                    matched_is_directory = is_directory_bucket_entry(bucket_path, *account_entry);
                }
            }

            closedir(bucket_dir);
        }

        closedir(accounts_dir);
        if (found_match) {
            *account = matched_account;
            set_error(error_message, "");
            return true;
        }

        set_error(error_message, "No account exists for that email address.");
        return false;
    }

    // The index key a bucket entry that FAILED to parse is (or would be) filed under. Expressed
    // through account_index_quarantine_key rather than restating its rule: for the directory layout
    // the key is the entry name, for a legacy flat file it is the file's own path -- and an unparsed
    // record discloses no email, so neither shape needs the record read. Takes the shared
    // classification so this and for_each_account_record_on_disk cannot key the same entry
    // differently; keying it differently is what twice put the index and the disk into disagreement
    // over a record that was really right there.
    std::string account_index_key_for_unparsed_bucket_entry(const std::string& entry_name,
        const BucketEntryClassification& classification)
    {
        AccountRecordOnDisk record;
        record.directory_entry_name = entry_name;
        record.record_path = classification.record_path;
        record.directory_layout = classification.directory_layout;
        record.parsed = false;
        return account_index_quarantine_key(record);
    }

    // The key for_each_account_record_on_disk files a bucket it could not open() under: the bucket's
    // own path, path-shaped like the other two unparsed shapes. A whole bucket that will not open is
    // every account in it leaving the index at once, so it has to be quarantined (visible, counted,
    // and armed as an escape hatch here) rather than silently skipped by one walker and treated as
    // fatal by the other.
    std::string account_index_key_for_unreadable_bucket(const std::string& bucket_path)
    {
        AccountRecordOnDisk record;
        record.record_path = bucket_path;
        record.directory_layout = false;
        record.parsed = false;
        return account_index_quarantine_key(record);
    }

    // True when the bucket this email would live in is itself quarantined. A bucket that would not
    // opendir() is filed as ONE record under its own path, so the accounts inside it were never
    // enumerated and none of their addresses is quarantined individually. is_quarantined() is keyed
    // by email and can never match a path-shaped key, which left every player in that bucket able to
    // register a fresh account straight over their own unreadable record.
    bool email_bucket_is_quarantined(const std::string& root_directory, const std::string& email)
    {
        const std::string bucket_path = root_directory + "/accounts/" + account_bucket_for_name(normalize_email(email));
        return account_index::is_quarantined_record_key(account_index_key_for_unreadable_bucket(bucket_path));
    }

    // "The index speaks for this tree": enabled AND built against this root. Every fast path in this
    // file is guarded by exactly this pair -- the index holds paths and keys for one tree only, so
    // answering a caller working against a different root would hand back this tree's answers.
    bool account_index_is_authoritative_for(const std::string& root_directory)
    {
        return account_index::is_enabled() && account_index::matches_root(root_directory);
    }

    bool account_storage_contains_unreadable_records(const std::string& root_directory, std::string* error_message)
    {
        // A record the index already quarantined is a known-bad record the game has deliberately
        // chosen to run with: boot logged it, counted it, reserved its email against exactly the
        // overwrite this function guards, and refused to continue at all past
        // MAX_QUARANTINED_RECORDS_AT_BOOT. Reporting it here as well would stop EVERY player from
        // creating an account until an operator noticed one bad file -- the whole outcome quarantine
        // exists to avoid.
        //
        // Be honest about the reachability: both live callers (create_account,
        // create_account_for_email) now SKIP this whole walk when the index is authoritative, so as
        // things stand this flag is always false and the escape hatches below never fire. They are
        // kept because they are the correct behaviour for any caller that runs this scan with the
        // index on, and because deleting them would make re-adding such a caller silently refuse
        // every registration -- which is the exact bug this wave is fixing.

        const std::string accounts_directory = root_directory + "/accounts";
        DIR* accounts_dir = opendir(accounts_directory.c_str());
        if (accounts_dir == nullptr) {
            if (errno == ENOENT) {
                set_error(error_message, "");
                return false;
            }

            set_error(error_message, "Failed to open accounts directory '" + accounts_directory + "': " + std::strerror(errno));
            return true;
        }

        while (dirent* bucket_entry = readdir(accounts_dir)) {
            // Every hidden entry, not just "." and "..": the enumerator has always skipped the whole
            // dotfile class, and the two walkers must agree about what is even a candidate.
            if (bucket_entry->d_name[0] == '.')
                continue;

            const std::string bucket_path = accounts_directory + "/" + bucket_entry->d_name;
            struct stat bucket_info { };
            if (stat(bucket_path.c_str(), &bucket_info) != 0 || !S_ISDIR(bucket_info.st_mode))
                continue;

            DIR* bucket_dir = opendir(bucket_path.c_str());
            if (bucket_dir == nullptr) {
                // Deliberately NOT gated on index_is_authority. The one condition that files this
                // quarantine key -- a bucket boot could not open -- is also the condition that makes
                // build_account_native_player_index stop trusting the index for lookups, so gating
                // the hatch on authority made it dead exactly when it was needed and refused every
                // account creation on the server. The quarantine set is a record of what boot could
                // not read; it is not a lookup path, and it stays valid whether or not the index is
                // answering queries. The address that would live in this bucket is refused
                // separately, by email_bucket_is_quarantined.
                if (account_index::is_quarantined_record_key(account_index_key_for_unreadable_bucket(bucket_path)))
                    continue;

                closedir(accounts_dir);
                set_error(error_message, "Failed to open account bucket directory '" + bucket_path + "': " + std::strerror(errno));
                return true;
            }

            while (dirent* account_entry = readdir(bucket_dir)) {
                const BucketEntryClassification classification = classify_bucket_entry(bucket_path, account_entry->d_name);
                if (classification.kind == BucketEntryKind::NotARecord)
                    continue;

                if (classification.kind != BucketEntryKind::Unreadable) {
                    AccountData stored_account;
                    if (read_account_file_from_path(classification.record_path, &stored_account, nullptr))
                        continue;
                }

                // Same reasoning as the bucket hatch above: a record boot already quarantined is one
                // the game deliberately chose to run with, and its address is reserved.
                if (account_index::is_quarantined_record_key(
                        account_index_key_for_unparsed_bucket_entry(account_entry->d_name, classification)))
                    continue;

                closedir(bucket_dir);
                closedir(accounts_dir);
                set_error(error_message, "Existing account records could not be read safely.");
                return true;
            }

            closedir(bucket_dir);
        }

        closedir(accounts_dir);
        set_error(error_message, "");
        return false;
    }

    std::string make_account_name_candidate_from_email(const std::string& normalized_email, int sequence_number)
    {
        std::string local_part = normalized_email;
        const size_t at_position = normalized_email.find('@');
        if (at_position != std::string::npos)
            local_part = normalized_email.substr(0, at_position);

        std::string sanitized_name;
        sanitized_name.reserve(local_part.size());
        for (char character : local_part) {
            if (std::isalnum(static_cast<unsigned char>(character)) || character == '-' || character == '_')
                sanitized_name += character;
        }

        if (sanitized_name.length() < MIN_ACCOUNT_NAME_LENGTH)
            sanitized_name = "acct";

        if (sanitized_name.length() > MAX_ACCOUNT_NAME_LENGTH)
            sanitized_name.resize(MAX_ACCOUNT_NAME_LENGTH);

        if (sequence_number <= 0)
            return sanitized_name;

        const std::string suffix = std::to_string(sequence_number);
        const size_t prefix_length = suffix.length() >= MAX_ACCOUNT_NAME_LENGTH ? 0 : MAX_ACCOUNT_NAME_LENGTH - suffix.length();
        return sanitized_name.substr(0, prefix_length) + suffix;
    }

    bool read_file_bytes(const std::string& path, bool required, LegacyAssetSnapshot* snapshot, std::string* error_message)
    {
        if (snapshot == nullptr) {
            set_error(error_message, "Snapshot output parameter must not be null.");
            return false;
        }

        FILE* file = std::fopen(path.c_str(), "rb");
        if (file == nullptr) {
            if (!required && errno == ENOENT) {
                snapshot->source_path = path;
                snapshot->encoding = "hex";
                snapshot->content.clear();
                snapshot->present = false;
                set_error(error_message, "");
                return true;
            }

            set_error(error_message, "Failed to open legacy file '" + path + "': " + std::strerror(errno));
            return false;
        }

        std::string bytes;
        char buffer[1024];
        while (true) {
            const size_t bytes_read = std::fread(buffer, sizeof(char), sizeof(buffer), file);
            if (bytes_read > 0)
                bytes.append(buffer, bytes_read);

            if (bytes_read < sizeof(buffer)) {
                if (std::ferror(file)) {
                    std::fclose(file);
                    set_error(error_message, "Failed to read legacy file '" + path + "'.");
                    return false;
                }
                break;
            }
        }

        std::fclose(file);
        snapshot->source_path = path;
        snapshot->encoding = "hex";
        snapshot->content = hex_encode(bytes);
        snapshot->present = true;
        set_error(error_message, "");
        return true;
    }

    bool file_exists(const std::string& path)
    {
        struct stat file_info { };
        return stat(path.c_str(), &file_info) == 0;
    }

    bool find_versioned_legacy_player_file_path(const std::string& root_directory, const std::string& character_name, std::string* resolved_path, bool* found, std::string* error_message)
    {
        if (resolved_path == nullptr) {
            set_error(error_message, "Resolved player-file path output must not be null.");
            return false;
        }
        if (found == nullptr) {
            set_error(error_message, "Versioned player-file presence output must not be null.");
            return false;
        }

        *found = false;

        const std::string normalized_name = normalize_account_name(character_name);
        const std::string directory_path = root_directory + "/players/" + account_bucket_for_name(normalized_name);
        DIR* directory = opendir(directory_path.c_str());
        if (directory == nullptr) {
            if (errno == ENOENT) {
                set_error(error_message, "");
                return true;
            }
            set_error(error_message, "Failed to open legacy player directory '" + directory_path + "': " + std::string(std::strerror(errno)));
            return false;
        }

        const std::string required_prefix = normalized_name + ".";
        std::string matched_path;
        while (dirent* entry = readdir(directory)) {
            const char* entry_name = entry->d_name;
            if (entry_name == nullptr)
                continue;
            if (entry_name[0] == '.')
                continue;
            if (std::strncmp(entry_name, required_prefix.c_str(), required_prefix.length()) != 0)
                continue;
            if (std::strchr(entry_name + required_prefix.length(), '.') == nullptr)
                continue;

            const char* suffix = entry_name + required_prefix.length();
            bool valid_versioned_name = true;
            for (int field_index = 0; field_index < 5; ++field_index) {
                if (*suffix == '\0') {
                    valid_versioned_name = false;
                    break;
                }

                while (*suffix != '\0' && *suffix != '.') {
                    if (!std::isdigit(static_cast<unsigned char>(*suffix))) {
                        valid_versioned_name = false;
                        break;
                    }
                    ++suffix;
                }

                if (!valid_versioned_name)
                    break;

                if (field_index < 4) {
                    if (*suffix != '.') {
                        valid_versioned_name = false;
                        break;
                    }
                    ++suffix;
                }
            }

            if (!valid_versioned_name || *suffix != '\0')
                continue;

            const std::string candidate_path = directory_path + "/" + entry_name;
            if (!matched_path.empty()) {
                closedir(directory);
                set_error(error_message, "Multiple versioned legacy player files matched character '" + normalized_name + "'.");
                return false;
            }

            matched_path = candidate_path;
        }

        closedir(directory);
        if (matched_path.empty()) {
            set_error(error_message, "");
            return true;
        }

        *resolved_path = matched_path;
        *found = true;
        set_error(error_message, "");
        return true;
    }

    bool resolve_legacy_player_file_path(const std::string& root_directory, const std::string& character_name, std::string* resolved_path, std::string* error_message)
    {
        if (resolved_path == nullptr) {
            set_error(error_message, "Resolved player-file path output must not be null.");
            return false;
        }

        bool found_versioned_path = false;
        if (!find_versioned_legacy_player_file_path(root_directory, character_name, resolved_path, &found_versioned_path, error_message))
            return false;
        if (found_versioned_path) {
            set_error(error_message, "");
            return true;
        }

        const std::string canonical_path = legacy_player_file_path(root_directory, character_name);
        if (file_exists(canonical_path)) {
            *resolved_path = canonical_path;
            set_error(error_message, "");
            return true;
        }

        set_error(error_message, "Failed to open legacy file '" + canonical_path + "': " + std::strerror(ENOENT));
        return false;
    }

    std::string parent_directory_for_path(const std::string& path)
    {
        const size_t separator_position = path.find_last_of('/');
        if (separator_position == std::string::npos)
            return "";

        return path.substr(0, separator_position);
    }

    bool ensure_directory_path_exists(const std::string& path, std::string* error_message)
    {
        if (path.empty()) {
            set_error(error_message, "");
            return true;
        }

        size_t segment_start = 0;
        if (!path.empty() && path[0] == '/')
            segment_start = 1;

        while (segment_start <= path.length()) {
            const size_t separator_position = path.find('/', segment_start);
            const std::string partial_path = separator_position == std::string::npos
                ? path
                : path.substr(0, separator_position);

            if (!partial_path.empty() && !create_directory_if_missing(partial_path, error_message))
                return false;

            if (separator_position == std::string::npos)
                break;

            segment_start = separator_position + 1;
        }

        set_error(error_message, "");
        return true;
    }

    bool write_snapshot_bytes(const std::string& path, const LegacyAssetSnapshot& snapshot, bool required, std::string* error_message)
    {
        if (!snapshot.present) {
            if (required) {
                set_error(error_message, "Required migration snapshot is missing for '" + path + "'.");
                return false;
            }

            if (std::remove(path.c_str()) != 0 && errno != ENOENT) {
                set_error(error_message, "Failed to remove stale legacy file '" + path + "': " + std::strerror(errno));
                return false;
            }

            set_error(error_message, "");
            return true;
        }

        std::string decoded_bytes;
        if (snapshot.encoding == "hex") {
            if (!hex_decode(snapshot.content, &decoded_bytes, error_message))
                return false;
        } else {
            set_error(error_message, "Unsupported snapshot encoding '" + snapshot.encoding + "'.");
            return false;
        }

        const std::string directory_path = parent_directory_for_path(path);
        if (!ensure_directory_path_exists(directory_path, error_message))
            return false;

        const std::string temp_path = path + ".tmp";
        FILE* file = open_secure_output_file(temp_path, error_message);
        if (file == nullptr)
            return false;

        const size_t written_length = std::fwrite(decoded_bytes.data(), sizeof(char), decoded_bytes.size(), file);
        const int close_result = std::fclose(file);
        if (written_length != decoded_bytes.size() || close_result != 0) {
            std::remove(temp_path.c_str());
            set_error(error_message, "Failed to write legacy file '" + path + "'.");
            return false;
        }

        if (std::rename(temp_path.c_str(), path.c_str()) != 0) {
            std::remove(temp_path.c_str());
            set_error(error_message, "Failed to move restored legacy file into place '" + path + "': " + std::strerror(errno));
            return false;
        }

        set_error(error_message, "");
        return true;
    }

    bool parse_snapshot(json_utils::JsonReader* reader, LegacyAssetSnapshot* snapshot, std::string* error_message)
    {
        if (reader == nullptr || snapshot == nullptr) {
            set_error(error_message, "Snapshot parser requires reader and output parameters.");
            return false;
        }

        return reader->parse_object([snapshot](const std::string& key, json_utils::JsonReader* nested_reader, std::string* nested_error_message) {
            if (key == "source_path")
                return nested_reader->parse_string(&snapshot->source_path, nested_error_message);
            if (key == "encoding")
                return nested_reader->parse_string(&snapshot->encoding, nested_error_message);
            if (key == "content")
                return nested_reader->parse_string(&snapshot->content, nested_error_message);
            if (key == "present")
                return nested_reader->parse_bool(&snapshot->present, nested_error_message);
            return nested_reader->skip_value(nested_error_message);
        },
            error_message);
    }

    bool parse_character_link(json_utils::JsonReader* reader, CharacterLinkReference* link, std::string* error_message)
    {
        if (reader == nullptr || link == nullptr) {
            set_error(error_message, "Character link parser requires reader and output parameters.");
            return false;
        }

        return reader->parse_object([link](const std::string& key, json_utils::JsonReader* nested_reader, std::string* nested_error_message) {
            if (key == "character_name")
                return nested_reader->parse_string(&link->character_name, nested_error_message);
            if (key == "character_path" || key == "player_path")
                return nested_reader->parse_string(&link->character_path, nested_error_message);
            if (key == "object_path")
                return nested_reader->parse_string(&link->object_path, nested_error_message);
            if (key == "exploits_path")
                return nested_reader->parse_string(&link->exploits_path, nested_error_message);
            return nested_reader->skip_value(nested_error_message);
        },
            error_message);
    }

    bool parse_character_link_array(json_utils::JsonReader* reader, std::vector<CharacterLinkReference>* links, std::string* error_message)
    {
        if (reader == nullptr || links == nullptr) {
            set_error(error_message, "Character link output parameter must not be null.");
            return false;
        }

        links->clear();
        return reader->parse_array([links](json_utils::JsonReader* nested_reader, std::string* nested_error_message) {
            CharacterLinkReference link;
            if (!parse_character_link(nested_reader, &link, nested_error_message))
                return false;
            links->push_back(link);
            return true;
        },
            error_message);
    }

    bool parse_account_property(const std::string& key, json_utils::JsonReader* reader, AccountData* account, std::string* error_message)
    {
        if (key == "version")
            return reader->parse_integer(&account->version, error_message);
        if (key == "account_name")
            return reader->parse_string(&account->account_name, error_message);
        if (key == "normalized_email")
            return reader->parse_string(&account->normalized_email, error_message);
        if (key == "password_hash")
            return reader->parse_string(&account->password_hash, error_message);
        if (key == "password_salt")
            return reader->parse_string(&account->password_salt, error_message);
        if (key == "characters")
            return reader->parse_string_array(&account->characters, error_message);
        if (key == "character_links")
            return parse_character_link_array(reader, &account->character_links, error_message);
        if (key == "email_verified")
            return reader->parse_bool(&account->email_verified, error_message);
        if (key == "email_verified_by")
            return reader->parse_string(&account->email_verified_by, error_message);
        if (key == "email_verified_at")
            return reader->parse_long(&account->email_verified_at, error_message);
        if (key == "verification_code_hash")
            return reader->parse_string(&account->verification_code_hash, error_message);
        if (key == "verification_code_sent_at")
            return reader->parse_long(&account->verification_code_sent_at, error_message);
        if (key == "verification_code_expires_at")
            return reader->parse_long(&account->verification_code_expires_at, error_message);
        if (key == "verification_attempt_count")
            return reader->parse_integer(&account->verification_attempt_count, error_message);
        if (key == "verification_last_attempt_at")
            return reader->parse_long(&account->verification_last_attempt_at, error_message);
        if (key == "blocked")
            return reader->parse_bool(&account->blocked, error_message);
        if (key == "block_reason")
            return reader->parse_string(&account->block_reason, error_message);
        if (key == "blocked_by")
            return reader->parse_string(&account->blocked_by, error_message);
        if (key == "blocked_at")
            return reader->parse_long(&account->blocked_at, error_message);
        if (key == "created_at")
            return reader->parse_long(&account->created_at, error_message);
        if (key == "updated_at")
            return reader->parse_long(&account->updated_at, error_message);
        if (key == "password_reset_at")
            return reader->parse_long(&account->password_reset_at, error_message);
        if (key == "password_reset_by")
            return reader->parse_string(&account->password_reset_by, error_message);
        if (key == "failed_login_count")
            return reader->parse_integer(&account->failed_login_count, error_message);
        if (key == "failed_login_last_at")
            return reader->parse_long(&account->failed_login_last_at, error_message);
        if (key == "failed_login_last_host")
            return reader->parse_string(&account->failed_login_last_host, error_message);
        if (key == "password_reset_code_hash")
            return reader->parse_string(&account->password_reset_code_hash, error_message);
        if (key == "password_reset_code_sent_at")
            return reader->parse_long(&account->password_reset_code_sent_at, error_message);
        if (key == "password_reset_code_expires_at")
            return reader->parse_long(&account->password_reset_code_expires_at, error_message);
        if (key == "password_reset_attempt_count")
            return reader->parse_integer(&account->password_reset_attempt_count, error_message);
        if (key == "roster_sort")
            return reader->parse_string(&account->roster_sort, error_message);
        if (key == "preferences") {
            account->preferences.present = true;
            return reader->parse_object([account](const std::string& nested_key,
                                            json_utils::JsonReader* nested_reader, std::string* nested_error_message) {
                if (nested_key == "flags") {
                    std::vector<std::string> flag_names;
                    if (!nested_reader->parse_string_array(&flag_names, nested_error_message))
                        return false;
                    long flags = 0;
                    if (!character_json::decode_preference_flags(flag_names, &flags, nested_error_message, /*skip_unknown_names=*/true))
                        return false;
                    account->preferences.preference_flags = flags & PPC_PRF_MASK;
                    return true;
                }
                if (nested_key == "colors") {
                    return character_json::parse_color_slots_object(nested_reader,
                        account->preferences.colors, account->preferences.color_settings, nested_error_message,
                        /*skip_unknown_keys=*/true);
                }
                return nested_reader->skip_value(nested_error_message);
            },
                error_message);
        }

        return reader->skip_value(error_message);
    }

    bool parse_migration_property(const std::string& key, json_utils::JsonReader* reader, CharacterMigrationData* migration, std::string* error_message)
    {
        if (key == "version")
            return reader->parse_integer(&migration->version, error_message);
        if (key == "account_name")
            return reader->parse_string(&migration->account_name, error_message);
        if (key == "character_name")
            return reader->parse_string(&migration->character_name, error_message);
        if (key == "migrated_at")
            return reader->parse_long(&migration->migrated_at, error_message);
        if (key == "player_file")
            return parse_snapshot(reader, &migration->player_file, error_message);
        if (key == "object_file")
            return parse_snapshot(reader, &migration->object_file, error_message);
        if (key == "exploits_file")
            return parse_snapshot(reader, &migration->exploits_file, error_message);

        return reader->skip_value(error_message);
    }

} // namespace

std::vector<size_t> ordered_roster_indices(const std::string& root_directory,
    const AccountData& account, RosterSort sort, RosterFilter filter)
{
    std::vector<size_t> indices;
    std::vector<roster_cache::RosterSummary> summaries(account.characters.size());

    for (size_t index = 0; index < account.characters.size(); ++index) {
        roster_cache::get(root_directory, account.account_name, account.characters[index], &summaries[index]);
        if (summary_matches_filter(summaries[index], filter))
            indices.push_back(index);
    }

    // stable_sort so equal keys keep insertion order and a redraw never reshuffles them.
    // Unreadable characters have no level/race/coefficients and sort last under every ordering.
    if (sort != RosterSort::Account) {
        std::stable_sort(indices.begin(), indices.end(),
            [&](size_t left, size_t right) {
                const roster_cache::RosterSummary& a = summaries[left];
                const roster_cache::RosterSummary& b = summaries[right];
                if (a.readable != b.readable)
                    return a.readable;
                if (!a.readable)
                    return false;

                if (sort == RosterSort::Name)
                    return to_lower_copy(account.characters[left]) < to_lower_copy(account.characters[right]);
                if (sort == RosterSort::Level)
                    return a.level > b.level;
                if (sort == RosterSort::Race)
                    return a.race < b.race;
                // Side groups by side, then A-Z within the side. Falling back to insertion order
                // here would make the side key look inert for any account whose link order already
                // happens to be side-grouped.
                const int left_side = side_rank_for_race(a.race);
                const int right_side = side_rank_for_race(b.race);
                if (left_side != right_side)
                    return left_side < right_side;
                return to_lower_copy(account.characters[left]) < to_lower_copy(account.characters[right]);
            });
    }

    if (indices.size() > kMaxDisplayedAccountCharacters)
        indices.resize(kMaxDisplayedAccountCharacters);
    return indices;
}

const char* roster_sort_to_string(RosterSort sort)
{
    switch (sort) {
    case RosterSort::Name:
        return "name";
    case RosterSort::Level:
        return "level";
    case RosterSort::Race:
        return "race";
    case RosterSort::Side:
        return "side";
    default:
        return "";
    }
}

bool roster_sort_from_string(const std::string& value, RosterSort* sort)
{
    if (sort == nullptr)
        return false;
    if (value.empty()) {
        *sort = RosterSort::Account;
        return true;
    }
    if (value == "name") {
        *sort = RosterSort::Name;
        return true;
    }
    if (value == "level") {
        *sort = RosterSort::Level;
        return true;
    }
    if (value == "race") {
        *sort = RosterSort::Race;
        return true;
    }
    if (value == "side") {
        *sort = RosterSort::Side;
        return true;
    }
    return false;
}

// Read an entire text file into *contents (POSIX-backed). Exposed for stage-timing the
// LOAD pipeline's file-read step.
bool read_text_file(const std::string& path, std::string* contents, std::string* error_message)
{
    if (contents == nullptr) {
        set_error(error_message, "Text-file output parameter must not be null.");
        return false;
    }

    FILE* file = std::fopen(path.c_str(), "r");
    if (file == nullptr) {
        set_error(error_message, "Failed to open file '" + path + "': " + std::strerror(errno));
        return false;
    }

    std::string text;
    char buffer[1024];
    while (true) {
        const size_t bytes_read = std::fread(buffer, sizeof(char), sizeof(buffer), file);
        if (bytes_read > 0)
            text.append(buffer, bytes_read);

        if (bytes_read < sizeof(buffer)) {
            if (std::ferror(file)) {
                std::fclose(file);
                set_error(error_message, "Failed to read file '" + path + "'.");
                return false;
            }
            break;
        }
    }

    std::fclose(file);
    *contents = std::move(text);
    set_error(error_message, "");
    return true;
}

// Atomic write: temp(path+".tmp") -> fwrite -> rename. Exposed for stage-timing the SAVE
// pipeline's disk-write step against a throwaway path.
bool write_text_file_atomically(const std::string& path, const std::string& text, std::string* error_message)
{
    const std::string temp_path = path + ".tmp";
    FILE* file = open_secure_output_file(temp_path, error_message);
    if (file == nullptr)
        return false;

    const size_t written_length = std::fwrite(text.data(), sizeof(char), text.size(), file);
    const int close_result = std::fclose(file);
    if (written_length != text.size() || close_result != 0) {
        std::remove(temp_path.c_str());
        set_error(error_message, "Failed to write temporary file '" + temp_path + "'.");
        return false;
    }

    if (std::rename(temp_path.c_str(), path.c_str()) != 0) {
        std::remove(temp_path.c_str());
        set_error(error_message, "Failed to move temporary file into place: " + std::string(std::strerror(errno)));
        return false;
    }

    set_error(error_message, "");
    return true;
}

// Keep the internal helper fragment before the public fragments. The split is
// intentionally low-risk and still shares one translation unit for now.
// clang-format off
// Order is load-bearing: these fragments are textually included into this TU and each one uses
// helpers defined by the ones above it. clang-format sorts include blocks alphabetically, which
// reorders them into a build break -- hence the guard.
#include "account_management_internal.cpp"
#include "account_management_identity.cpp"
#include "account_management_storage.cpp"
#include "account_management_assets.cpp"
// clang-format on

namespace {

    bool validate_migration_identity(const CharacterMigrationData& migration, const std::string& expected_account_name, const std::string& expected_character_name, std::string* error_message)
    {
        if (!is_valid_account_name(migration.account_name, error_message))
            return false;
        if (!is_valid_character_name(migration.character_name, error_message))
            return false;

        if (normalize_account_name(expected_account_name) != normalize_account_name(migration.account_name)) {
            set_error(error_message, "Migration account identity did not match the selected account.");
            return false;
        }

        if (normalize_account_name(expected_character_name) != normalize_account_name(migration.character_name)) {
            set_error(error_message, "Migration character identity did not match the selected character.");
            return false;
        }

        set_error(error_message, "");
        return true;
    }

} // namespace

// clang-format off
#include "account_management_migration.cpp"
#include "account_management_presentation.cpp"
// clang-format on

} // namespace account
