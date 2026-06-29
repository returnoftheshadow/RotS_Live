#include "account_management.h"
#include "character_json.h"
#include "exploits_json.h"
#include "json_utils.h"
#include "objects_json.h"
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
    constexpr size_t kMaxDisplayedAccountCharacters = 100;

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

    std::string format_account_character_short_entry(const std::string& root_directory, const AccountData& account, size_t index, const std::string& character_name)
    {
        const std::string display_name = format_character_name_for_display(character_name);

        char_file_u stored_character {};
        std::string error_message;
        if (!read_account_character_file(root_directory, account.account_name, character_name, &stored_character, &error_message))
        {
            char line[256];
            std::snprintf(line, sizeof(line), "%zu) [ ?? ???] %-12.12s", index + 1, display_name.c_str());
            return line;
        }

        char line[256];
        std::snprintf(line, sizeof(line), "%zu) [%3d %s] %-12.12s", index + 1, stored_character.level, safe_race_abbrev(stored_character.race), display_name.c_str());
        return line;
    }

    std::string format_account_character_short_roster(const std::string& root_directory, const AccountData& account)
    {
        if (account.characters.empty())
            return "\n\rNo linked characters yet.\n\r";

        std::ostringstream output;
        const size_t displayed_count = std::min(account.characters.size(), kMaxDisplayedAccountCharacters);
        for (size_t index = 0; index < displayed_count; ++index) {
            output << format_account_character_short_entry(root_directory, account, index, account.characters[index]);
            if ((index + 1) % 2 == 0)
                output << "\n\r";
        }

        if (displayed_count % 2 != 0)
            output << "\n\r";

        if (account.characters.size() > displayed_count)
            output << "\n\r... and " << (account.characters.size() - displayed_count) << " more\n\r";

        output << "\n\r" << displayed_count << " character" << (displayed_count == 1 ? "" : "s") << " displayed.\n\r";
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

        AccountData stored_account;
        if (read_account_file(root_directory, account_identifier, &stored_account, nullptr))
            return normalize_email(stored_account.normalized_email);

        return "";
    }

    bool read_account_file_from_bucket_entry(const std::string& bucket_path, const dirent& account_entry, AccountData* account, std::string* error_message)
    {
        const std::string entry_path = bucket_path + "/" + account_entry.d_name;
        struct stat entry_info { };
        if (stat(entry_path.c_str(), &entry_info) != 0)
            return false;

        if (S_ISDIR(entry_info.st_mode)) {
            const std::string account_json_path = entry_path + "/account.json";
            struct stat account_json_info { };
            if (stat(account_json_path.c_str(), &account_json_info) != 0) {
                if (errno == ENOENT) {
                    set_error(error_message, "Entry is not an account record.");
                    return false;
                }

                set_error(error_message, "Failed to stat account file '" + account_json_path + "': " + std::strerror(errno));
                return false;
            }

            return read_account_file_from_path(account_json_path, account, error_message);
        }

        const std::string file_name = account_entry.d_name;
        if (S_ISREG(entry_info.st_mode) && file_name.length() >= 6 && file_name.substr(file_name.length() - 5) == ".json")
            return read_account_file_from_path(entry_path, account, error_message);

        set_error(error_message, "Entry is not an account record.");
        return false;
    }

    bool is_directory_bucket_entry(const std::string& bucket_path, const dirent& account_entry)
    {
        const std::string entry_path = bucket_path + "/" + account_entry.d_name;
        struct stat entry_info { };
        return stat(entry_path.c_str(), &entry_info) == 0 && S_ISDIR(entry_info.st_mode);
    }

    bool find_account_file_path_by_account_name(const std::string& root_directory, const std::string& account_name, std::string* account_path, std::string* error_message)
    {
        if (account_path == nullptr) {
            set_error(error_message, "Account-path output parameter must not be null.");
            return false;
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

    bool find_account_by_email_internal(const std::string& root_directory, const std::string& email, AccountData* account, std::string* error_message)
    {
        if (account == nullptr) {
            set_error(error_message, "Account output parameter must not be null.");
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

    bool account_storage_contains_unreadable_records(const std::string& root_directory, std::string* error_message)
    {
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
                return true;
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
                    set_error(error_message, "Existing account records could not be read safely.");
                    return true;
                }
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
#include "account_management_internal.cpp"
#include "account_management_identity.cpp"
#include "account_management_storage.cpp"
#include "account_management_assets.cpp"

namespace {

    bool validate_migration_identity(const CharacterMigrationData& migration, const std::string& expected_account_name, const std::string& expected_character_name, std::string* error_message)
    {
        if (!is_valid_account_name(migration.account_name, error_message))
            return false;
        if (!validate_identifier_for_path(migration.character_name, "Character name", error_message))
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

#include "account_management_migration.cpp"
#include "account_management_presentation.cpp"

} // namespace account
