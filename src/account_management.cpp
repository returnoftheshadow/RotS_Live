#include "account_management.h"

#include <cerrno>
#include <crypt.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <sstream>
#include <utility>

namespace account {
namespace {

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

    std::string json_escape(const std::string& value)
    {
        std::string escaped;
        escaped.reserve(value.size() + 8);

        for (char character : value) {
            switch (character) {
            case '\\':
                escaped += "\\\\";
                break;
            case '"':
                escaped += "\\\"";
                break;
            case '\b':
                escaped += "\\b";
                break;
            case '\f':
                escaped += "\\f";
                break;
            case '\n':
                escaped += "\\n";
                break;
            case '\r':
                escaped += "\\r";
                break;
            case '\t':
                escaped += "\\t";
                break;
            default:
                escaped += character;
                break;
            }
        }

        return escaped;
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

    bool send_email_message(const std::string& recipient, const std::string& subject, const std::string& body, std::string* error_message)
    {
        FILE* pipe = popen("/usr/sbin/sendmail -t -oi", "w");
        if (pipe == nullptr) {
            set_error(error_message, "Failed to open sendmail for email delivery.");
            return false;
        }

        std::fprintf(pipe,
            "To: %s\n"
            "From: RotS Account Verification <noreply@rotsmud.org>\n"
            "Subject: %s\n"
            "\n"
            "%s\n",
            recipient.c_str(), subject.c_str(), body.c_str());

        const int close_result = pclose(pipe);
        if (close_result != 0) {
            set_error(error_message, "sendmail reported a delivery failure.");
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

                const std::string file_name = account_entry->d_name;
                if (file_name.length() < 6 || file_name.substr(file_name.length() - 5) != ".json")
                    continue;

                const std::string account_path = bucket_path + "/" + file_name;
                AccountData stored_account;
                if (!read_account_file_from_path(account_path, &stored_account, error_message)) {
                    closedir(bucket_dir);
                    closedir(accounts_dir);
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

                const std::string file_name = account_entry->d_name;
                if (file_name.length() < 6 || file_name.substr(file_name.length() - 5) != ".json")
                    continue;

                AccountData stored_account;
                if (!read_account_file_from_path(bucket_path + "/" + file_name, &stored_account, error_message)) {
                    continue;
                }

                if (stored_account.normalized_email == normalized_email) {
                    if (found_match) {
                        closedir(bucket_dir);
                        closedir(accounts_dir);
                        set_error(error_message, "Multiple account records exist for that email address.");
                        return false;
                    }

                    matched_account = stored_account;
                    found_match = true;
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

                const std::string file_name = account_entry->d_name;
                if (file_name.length() < 6 || file_name.substr(file_name.length() - 5) != ".json")
                    continue;

                AccountData stored_account;
                if (!read_account_file_from_path(bucket_path + "/" + file_name, &stored_account, nullptr)) {
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

    class JsonReader {
    public:
        explicit JsonReader(const std::string& input)
            : m_input(input)
        {
        }

        bool parse_account(AccountData* account, std::string* error_message)
        {
            skip_whitespace();
            if (!consume('{')) {
                set_error(error_message, "Expected JSON object.");
                return false;
            }

            bool first_property = true;
            while (true) {
                skip_whitespace();

                if (consume('}'))
                    break;

                if (!first_property) {
                    if (!consume(',')) {
                        set_error(error_message, "Expected ',' between object properties.");
                        return false;
                    }

                    skip_whitespace();
                }

                std::string key;
                if (!parse_string(&key, error_message))
                    return false;

                skip_whitespace();
                if (!consume(':')) {
                    set_error(error_message, "Expected ':' after object key.");
                    return false;
                }

                skip_whitespace();
                if (!parse_property(key, account, error_message))
                    return false;

                first_property = false;
            }

            skip_whitespace();
            if (!is_at_end()) {
                set_error(error_message, "Unexpected trailing characters after JSON object.");
                return false;
            }

            return true;
        }

        bool parse_character_migration(CharacterMigrationData* migration, std::string* error_message)
        {
            skip_whitespace();
            if (!consume('{')) {
                set_error(error_message, "Expected JSON object.");
                return false;
            }

            bool first_property = true;
            while (true) {
                skip_whitespace();

                if (consume('}'))
                    break;

                if (!first_property) {
                    if (!consume(',')) {
                        set_error(error_message, "Expected ',' between object properties.");
                        return false;
                    }

                    skip_whitespace();
                }

                std::string key;
                if (!parse_string(&key, error_message))
                    return false;

                skip_whitespace();
                if (!consume(':')) {
                    set_error(error_message, "Expected ':' after object key.");
                    return false;
                }

                skip_whitespace();
                if (!parse_migration_property(key, migration, error_message))
                    return false;

                first_property = false;
            }

            skip_whitespace();
            if (!is_at_end()) {
                set_error(error_message, "Unexpected trailing characters after JSON object.");
                return false;
            }

            return true;
        }

    private:
        bool parse_property(const std::string& key, AccountData* account, std::string* error_message)
        {
            if (key == "version")
                return parse_integer(&account->version, error_message);
            if (key == "account_name")
                return parse_string(&account->account_name, error_message);
            if (key == "normalized_email")
                return parse_string(&account->normalized_email, error_message);
            if (key == "password_hash")
                return parse_string(&account->password_hash, error_message);
            if (key == "password_salt")
                return parse_string(&account->password_salt, error_message);
            if (key == "characters")
                return parse_string_array(&account->characters, error_message);
            if (key == "email_verified")
                return parse_bool(&account->email_verified, error_message);
            if (key == "email_verified_by")
                return parse_string(&account->email_verified_by, error_message);
            if (key == "email_verified_at")
                return parse_long(&account->email_verified_at, error_message);
            if (key == "verification_code_hash")
                return parse_string(&account->verification_code_hash, error_message);
            if (key == "verification_code_sent_at")
                return parse_long(&account->verification_code_sent_at, error_message);
            if (key == "verification_code_expires_at")
                return parse_long(&account->verification_code_expires_at, error_message);
            if (key == "verification_attempt_count")
                return parse_integer(&account->verification_attempt_count, error_message);
            if (key == "verification_last_attempt_at")
                return parse_long(&account->verification_last_attempt_at, error_message);
            if (key == "blocked")
                return parse_bool(&account->blocked, error_message);
            if (key == "block_reason")
                return parse_string(&account->block_reason, error_message);
            if (key == "blocked_by")
                return parse_string(&account->blocked_by, error_message);
            if (key == "blocked_at")
                return parse_long(&account->blocked_at, error_message);
            if (key == "created_at")
                return parse_long(&account->created_at, error_message);
            if (key == "updated_at")
                return parse_long(&account->updated_at, error_message);
            if (key == "password_reset_at")
                return parse_long(&account->password_reset_at, error_message);
            if (key == "password_reset_by")
                return parse_string(&account->password_reset_by, error_message);

            return skip_value(error_message);
        }

        bool parse_migration_property(const std::string& key, CharacterMigrationData* migration, std::string* error_message)
        {
            if (key == "version")
                return parse_integer(&migration->version, error_message);
            if (key == "account_name")
                return parse_string(&migration->account_name, error_message);
            if (key == "character_name")
                return parse_string(&migration->character_name, error_message);
            if (key == "migrated_at")
                return parse_long(&migration->migrated_at, error_message);
            if (key == "player_file")
                return parse_snapshot(&migration->player_file, error_message);
            if (key == "object_file")
                return parse_snapshot(&migration->object_file, error_message);
            if (key == "exploits_file")
                return parse_snapshot(&migration->exploits_file, error_message);

            return skip_value(error_message);
        }

        bool parse_snapshot(LegacyAssetSnapshot* snapshot, std::string* error_message)
        {
            if (!consume('{')) {
                set_error(error_message, "Expected JSON object for snapshot.");
                return false;
            }

            bool first_property = true;
            while (true) {
                skip_whitespace();
                if (consume('}'))
                    return true;

                if (!first_property) {
                    if (!consume(',')) {
                        set_error(error_message, "Expected ',' between snapshot properties.");
                        return false;
                    }
                    skip_whitespace();
                }

                std::string key;
                if (!parse_string(&key, error_message))
                    return false;

                skip_whitespace();
                if (!consume(':')) {
                    set_error(error_message, "Expected ':' after snapshot key.");
                    return false;
                }

                skip_whitespace();
                if (key == "source_path") {
                    if (!parse_string(&snapshot->source_path, error_message))
                        return false;
                } else if (key == "encoding") {
                    if (!parse_string(&snapshot->encoding, error_message))
                        return false;
                } else if (key == "content") {
                    if (!parse_string(&snapshot->content, error_message))
                        return false;
                } else if (key == "present") {
                    if (!parse_bool(&snapshot->present, error_message))
                        return false;
                } else {
                    if (!skip_value(error_message))
                        return false;
                }

                first_property = false;
            }
        }

        bool parse_string(std::string* value, std::string* error_message)
        {
            if (!consume('"')) {
                set_error(error_message, "Expected string value.");
                return false;
            }

            std::string parsed;
            while (!is_at_end()) {
                char character = m_input[m_position++];
                if (character == '"') {
                    *value = parsed;
                    return true;
                }

                if (character == '\\') {
                    if (is_at_end()) {
                        set_error(error_message, "Unexpected end of input in string escape sequence.");
                        return false;
                    }

                    char escaped = m_input[m_position++];
                    switch (escaped) {
                    case '"':
                    case '\\':
                    case '/':
                        parsed += escaped;
                        break;
                    case 'b':
                        parsed += '\b';
                        break;
                    case 'f':
                        parsed += '\f';
                        break;
                    case 'n':
                        parsed += '\n';
                        break;
                    case 'r':
                        parsed += '\r';
                        break;
                    case 't':
                        parsed += '\t';
                        break;
                    default:
                        set_error(error_message, "Unsupported escape sequence in JSON string.");
                        return false;
                    }
                } else {
                    parsed += character;
                }
            }

            set_error(error_message, "Unterminated JSON string.");
            return false;
        }

        bool parse_bool(bool* value, std::string* error_message)
        {
            if (match_literal("true")) {
                *value = true;
                return true;
            }

            if (match_literal("false")) {
                *value = false;
                return true;
            }

            set_error(error_message, "Expected boolean value.");
            return false;
        }

        bool parse_integer(int* value, std::string* error_message)
        {
            long parsed = 0;
            if (!parse_long(&parsed, error_message))
                return false;

            *value = static_cast<int>(parsed);
            return true;
        }

        bool parse_long(long* value, std::string* error_message)
        {
            if (is_at_end()) {
                set_error(error_message, "Expected integer value.");
                return false;
            }

            size_t start = m_position;
            if (m_input[m_position] == '-')
                ++m_position;

            if (m_position >= m_input.size() || !std::isdigit(static_cast<unsigned char>(m_input[m_position]))) {
                set_error(error_message, "Expected integer value.");
                return false;
            }

            while (m_position < m_input.size() && std::isdigit(static_cast<unsigned char>(m_input[m_position])))
                ++m_position;

            std::string number_text = m_input.substr(start, m_position - start);
            char* end_ptr = nullptr;
            errno = 0;
            long parsed = std::strtol(number_text.c_str(), &end_ptr, 10);
            if (errno != 0 || end_ptr == nullptr || *end_ptr != '\0') {
                set_error(error_message, "Invalid integer value.");
                return false;
            }

            *value = parsed;
            return true;
        }

        bool parse_string_array(std::vector<std::string>* values, std::string* error_message)
        {
            if (!consume('[')) {
                set_error(error_message, "Expected array value.");
                return false;
            }

            values->clear();
            bool first_value = true;
            while (true) {
                skip_whitespace();

                if (consume(']'))
                    return true;

                if (!first_value) {
                    if (!consume(',')) {
                        set_error(error_message, "Expected ',' between array values.");
                        return false;
                    }

                    skip_whitespace();
                }

                std::string value;
                if (!parse_string(&value, error_message))
                    return false;

                values->push_back(value);
                first_value = false;
            }
        }

        bool skip_value(std::string* error_message)
        {
            skip_whitespace();
            if (is_at_end()) {
                set_error(error_message, "Expected JSON value.");
                return false;
            }

            char current = m_input[m_position];
            if (current == '"') {
                std::string ignored;
                return parse_string(&ignored, error_message);
            }

            if (current == '{') {
                ++m_position;
                bool first_property = true;
                while (true) {
                    skip_whitespace();
                    if (consume('}'))
                        return true;

                    if (!first_property) {
                        if (!consume(',')) {
                            set_error(error_message, "Expected ',' between nested object properties.");
                            return false;
                        }
                        skip_whitespace();
                    }

                    std::string ignored_key;
                    if (!parse_string(&ignored_key, error_message))
                        return false;

                    skip_whitespace();
                    if (!consume(':')) {
                        set_error(error_message, "Expected ':' after nested object key.");
                        return false;
                    }

                    skip_whitespace();
                    if (!skip_value(error_message))
                        return false;

                    first_property = false;
                }
            }

            if (current == '[') {
                ++m_position;
                bool first_value = true;
                while (true) {
                    skip_whitespace();
                    if (consume(']'))
                        return true;

                    if (!first_value) {
                        if (!consume(',')) {
                            set_error(error_message, "Expected ',' between nested array values.");
                            return false;
                        }
                        skip_whitespace();
                    }

                    if (!skip_value(error_message))
                        return false;

                    first_value = false;
                }
            }

            if (current == 't' || current == 'f') {
                bool ignored = false;
                return parse_bool(&ignored, error_message);
            }

            if (current == '-' || std::isdigit(static_cast<unsigned char>(current))) {
                long ignored = 0;
                return parse_long(&ignored, error_message);
            }

            set_error(error_message, "Unsupported JSON value.");
            return false;
        }

        bool match_literal(const char* literal)
        {
            size_t literal_length = std::strlen(literal);
            if (m_input.compare(m_position, literal_length, literal) != 0)
                return false;

            m_position += literal_length;
            return true;
        }

        void skip_whitespace()
        {
            while (m_position < m_input.size() && std::isspace(static_cast<unsigned char>(m_input[m_position])))
                ++m_position;
        }

        bool consume(char character)
        {
            if (m_position < m_input.size() && m_input[m_position] == character) {
                ++m_position;
                return true;
            }

            return false;
        }

        bool is_at_end() const
        {
            return m_position >= m_input.size();
        }

        const std::string& m_input;
        size_t m_position = 0;
    };

} // namespace

std::string normalize_account_name(const std::string& account_name)
{
    return to_lower_copy(trim_copy(account_name));
}

std::string normalize_email(const std::string& email)
{
    return to_lower_copy(trim_copy(email));
}

bool is_valid_account_name(const std::string& account_name, std::string* error_message)
{
    const std::string normalized_name = normalize_account_name(account_name);
    if (normalized_name.length() < MIN_ACCOUNT_NAME_LENGTH) {
        set_error(error_message, "Account names must be at least 3 characters long.");
        return false;
    }

    if (normalized_name.length() > MAX_ACCOUNT_NAME_LENGTH) {
        set_error(error_message, "Account names must be 20 characters or fewer.");
        return false;
    }

    for (char character : normalized_name) {
        if (!std::isalnum(static_cast<unsigned char>(character)) && character != '_' && character != '-') {
            set_error(error_message, "Account names may only contain letters, numbers, '-' and '_'.");
            return false;
        }
    }

    set_error(error_message, "");
    return true;
}

bool is_valid_email(const std::string& email, std::string* error_message)
{
    const std::string normalized_email = normalize_email(email);
    const size_t at_position = normalized_email.find('@');
    if (normalized_email.empty() || at_position == std::string::npos || at_position == 0 || at_position + 1 >= normalized_email.length()) {
        set_error(error_message, "Email addresses must contain text before and after '@'.");
        return false;
    }

    for (char character : normalized_email) {
        if (std::iscntrl(static_cast<unsigned char>(character)) || std::isspace(static_cast<unsigned char>(character))) {
            set_error(error_message, "Email addresses may not contain whitespace or control characters.");
            return false;
        }
    }

    if (normalized_email.find(',', at_position) != std::string::npos || normalized_email.find(';', at_position) != std::string::npos) {
        set_error(error_message, "Email addresses must contain exactly one recipient address.");
        return false;
    }

    const std::string local_part = normalized_email.substr(0, at_position);
    const std::string domain_part = normalized_email.substr(at_position + 1);
    if (domain_part.find('.') == std::string::npos) {
        set_error(error_message, "Email addresses must include a domain like 'example.com'.");
        return false;
    }

    auto is_valid_local_character = [](char character) {
        return std::isalnum(static_cast<unsigned char>(character))
            || character == '.'
            || character == '_'
            || character == '%'
            || character == '+'
            || character == '-';
    };

    auto is_valid_domain_character = [](char character) {
        return std::isalnum(static_cast<unsigned char>(character))
            || character == '.'
            || character == '-';
    };

    if (local_part.front() == '.' || local_part.back() == '.' || domain_part.front() == '.' || domain_part.back() == '.') {
        set_error(error_message, "Email addresses may not start or end a section with '.'.");
        return false;
    }

    if (local_part.find("..") != std::string::npos || domain_part.find("..") != std::string::npos) {
        set_error(error_message, "Email addresses may not contain repeated '.'.");
        return false;
    }

    for (char character : local_part) {
        if (!is_valid_local_character(character)) {
            set_error(error_message, "Email addresses contain unsupported characters.");
            return false;
        }
    }

    for (char character : domain_part) {
        if (!is_valid_domain_character(character)) {
            set_error(error_message, "Email addresses contain unsupported characters.");
            return false;
        }
    }

    set_error(error_message, "");
    return true;
}

bool is_valid_password(const std::string& password, std::string* error_message)
{
    if (static_cast<int>(password.length()) < MIN_PASSWORD_LENGTH) {
        set_error(error_message, "Passwords must be at least 8 characters long.");
        return false;
    }

    bool has_uppercase = false;
    bool has_lowercase = false;
    bool has_number = false;

    for (char character : password) {
        const unsigned char normalized_character = static_cast<unsigned char>(character);
        has_uppercase = has_uppercase || std::isupper(normalized_character);
        has_lowercase = has_lowercase || std::islower(normalized_character);
        has_number = has_number || std::isdigit(normalized_character);
    }

    if (!has_uppercase) {
        set_error(error_message, "Passwords must include at least one uppercase letter.");
        return false;
    }

    if (!has_lowercase) {
        set_error(error_message, "Passwords must include at least one lowercase letter.");
        return false;
    }

    if (!has_number) {
        set_error(error_message, "Passwords must include at least one number.");
        return false;
    }

    set_error(error_message, "");
    return true;
}

bool generate_password_credentials(const std::string& password, std::string* password_hash, std::string* password_salt, std::string* error_message)
{
    if (password_hash == nullptr || password_salt == nullptr) {
        set_error(error_message, "Password hash and salt outputs must not be null.");
        return false;
    }

    std::string validation_error;
    if (!is_valid_password(password, &validation_error)) {
        set_error(error_message, validation_error);
        return false;
    }

    return generate_hash_for_secret(password, password_hash, password_salt, error_message);
}

bool verify_password(const std::string& password, const std::string& password_hash)
{
    if (password_hash.empty())
        return false;

    char* hashed_password = crypt(password.c_str(), password_hash.c_str());
    if (hashed_password == nullptr)
        return false;

    return password_hash == hashed_password;
}

bool initialize_new_account(const std::string& account_name, const std::string& email, const std::string& password, long created_at, AccountData* account, std::string* error_message)
{
    if (account == nullptr) {
        set_error(error_message, "Account output parameter must not be null.");
        return false;
    }

    std::string validation_error;
    if (!is_valid_account_name(account_name, &validation_error)) {
        set_error(error_message, validation_error);
        return false;
    }

    AccountData new_account;
    new_account.account_name = normalize_account_name(account_name);
    new_account.normalized_email = normalize_email(email);
    new_account.created_at = created_at;
    new_account.updated_at = created_at;
    new_account.email_verified = false;
    new_account.blocked = false;

    if (!generate_password_credentials(password, &new_account.password_hash, &new_account.password_salt, error_message))
        return false;

    *account = std::move(new_account);
    set_error(error_message, "");
    return true;
}

bool prepare_email_verification_code(AccountData* account, long sent_at, std::string* verification_code, std::string* error_message)
{
    if (account == nullptr || verification_code == nullptr) {
        set_error(error_message, "Account and verification-code outputs must not be null.");
        return false;
    }

    const std::string generated_code = generate_numeric_verification_code();
    if (generated_code.empty()) {
        set_error(error_message, "Failed to generate an email verification code.");
        return false;
    }

    std::string verification_salt;
    if (!generate_hash_for_secret(generated_code, &account->verification_code_hash, &verification_salt, error_message))
        return false;

    account->verification_code_sent_at = sent_at;
    account->verification_code_expires_at = sent_at + EMAIL_VERIFICATION_WINDOW_SECONDS;
    account->verification_attempt_count = 0;
    account->verification_last_attempt_at = 0;
    account->updated_at = sent_at;
    *verification_code = generated_code;

    set_error(error_message, "");
    return true;
}

bool confirm_email_verification_code(AccountData* account, const std::string& verification_code, const std::string& verified_by, long verified_at, std::string* error_message)
{
    if (account == nullptr) {
        set_error(error_message, "Account output parameter must not be null.");
        return false;
    }

    const std::string trimmed_code = trim_copy(verification_code);
    if (trimmed_code.empty()) {
        set_error(error_message, "Verification code must not be empty.");
        return false;
    }

    if (account->email_verified) {
        set_error(error_message, "");
        return true;
    }

    if (account->verification_code_hash.empty() || account->verification_code_expires_at == 0) {
        set_error(error_message, "No email verification code is currently pending.");
        return false;
    }

    if (verified_at > account->verification_code_expires_at) {
        set_error(error_message, "That verification code has expired.");
        return false;
    }

    if (!verify_password(trimmed_code, account->verification_code_hash)) {
        ++account->verification_attempt_count;
        account->verification_last_attempt_at = verified_at;
        account->updated_at = verified_at;
        if (account->verification_attempt_count >= MAX_EMAIL_VERIFICATION_ATTEMPTS) {
            account->verification_code_hash.clear();
            account->verification_code_expires_at = 0;
            set_error(error_message, "Too many invalid verification attempts. Please request a new verification code.");
            return false;
        }

        set_error(error_message, "That verification code is invalid.");
        return false;
    }

    verify_email(account, verified_by, verified_at);
    set_error(error_message, "");
    return true;
}

void verify_email(AccountData* account, const std::string& verified_by, long verified_at)
{
    if (account == nullptr)
        return;

    account->email_verified = true;
    account->email_verified_by = verified_by;
    account->email_verified_at = verified_at;
    account->verification_code_hash.clear();
    account->verification_code_sent_at = 0;
    account->verification_code_expires_at = 0;
    account->verification_attempt_count = 0;
    account->verification_last_attempt_at = 0;
    account->updated_at = verified_at;
}

void unverify_email(AccountData* account)
{
    if (account == nullptr)
        return;

    account->email_verified = false;
    account->email_verified_by.clear();
    account->email_verified_at = 0;
    account->verification_code_hash.clear();
    account->verification_code_sent_at = 0;
    account->verification_code_expires_at = 0;
    account->verification_attempt_count = 0;
    account->verification_last_attempt_at = 0;
}

bool account_has_character(const AccountData& account, const std::string& character_name)
{
    const std::string normalized_character_name = normalize_account_name(character_name);
    return std::find_if(account.characters.begin(), account.characters.end(),
               [&normalized_character_name](const std::string& linked_character_name) {
                   return normalize_account_name(linked_character_name) == normalized_character_name;
               })
        != account.characters.end();
}

bool select_linked_character(const AccountData& account, const std::string& character_name, std::string* normalized_character_name, std::string* error_message)
{
    if (normalized_character_name == nullptr) {
        set_error(error_message, "Character output parameter must not be null.");
        return false;
    }

    const std::string normalized_name = normalize_account_name(character_name);
    if (normalized_name.empty()) {
        set_error(error_message, "Character name must not be empty.");
        return false;
    }

    if (!account_has_character(account, normalized_name)) {
        set_error(error_message, "Character is not linked to this account.");
        return false;
    }

    *normalized_character_name = normalized_name;
    set_error(error_message, "");
    return true;
}

bool add_character_to_account(AccountData* account, const std::string& character_name, std::string* error_message)
{
    if (account == nullptr) {
        set_error(error_message, "Account output parameter must not be null.");
        return false;
    }

    const std::string normalized_character_name = normalize_account_name(character_name);
    if (normalized_character_name.empty()) {
        set_error(error_message, "Character names must not be empty.");
        return false;
    }

    if (account_has_character(*account, normalized_character_name)) {
        set_error(error_message, "Character is already linked to this account.");
        return false;
    }

    account->characters.push_back(normalized_character_name);
    set_error(error_message, "");
    return true;
}

void block_account(AccountData* account, const std::string& blocked_by, const std::string& block_reason, long blocked_at)
{
    if (account == nullptr)
        return;

    account->blocked = true;
    account->blocked_by = blocked_by;
    account->block_reason = block_reason;
    account->blocked_at = blocked_at;
    account->updated_at = blocked_at;
}

void unblock_account(AccountData* account)
{
    if (account == nullptr)
        return;

    account->blocked = false;
    account->block_reason.clear();
    account->blocked_by.clear();
    account->blocked_at = 0;
}

bool reset_account_password(AccountData* account, const std::string& new_password, const std::string& reset_by, long reset_at, std::string* error_message)
{
    if (account == nullptr) {
        set_error(error_message, "Account output parameter must not be null.");
        return false;
    }

    std::string password_hash;
    std::string password_salt;
    if (!generate_password_credentials(new_password, &password_hash, &password_salt, error_message))
        return false;

    account->password_hash = password_hash;
    account->password_salt = password_salt;
    account->password_reset_by = reset_by;
    account->password_reset_at = reset_at;
    account->updated_at = reset_at;

    set_error(error_message, "");
    return true;
}

bool create_account(const std::string& root_directory, const std::string& account_name, const std::string& email, const std::string& password, long created_at, AccountData* account, std::string* error_message)
{
    if (!validate_identifier_for_path(account_name, "Account name", error_message))
        return false;
    if (!is_valid_email(email, error_message))
        return false;

    std::string storage_error;
    if (account_storage_contains_unreadable_records(root_directory, &storage_error)) {
        set_error(error_message, storage_error);
        return false;
    }

    AccountData existing_email_account;
    if (find_account_by_email_internal(root_directory, email, &existing_email_account, nullptr)) {
        set_error(error_message, "An account already exists for that email address.");
        return false;
    }

    AccountData existing_account;
    std::string read_error;
    if (read_account_file(root_directory, account_name, &existing_account, &read_error)) {
        set_error(error_message, "Account already exists.");
        return false;
    }

    if (path_exists(account_file_path(root_directory, account_name))) {
        set_error(error_message, "Existing account file could not be read safely.");
        return false;
    }

    AccountData new_account;
    if (!initialize_new_account(account_name, email, password, created_at, &new_account, error_message))
        return false;

    if (!write_account_file(root_directory, new_account, error_message))
        return false;

    if (account)
        *account = new_account;

    set_error(error_message, "");
    return true;
}

bool create_account_for_email(const std::string& root_directory, const std::string& email, const std::string& password, long created_at, AccountData* account, std::string* error_message)
{
    if (!is_valid_email(email, error_message))
        return false;

    std::string storage_error;
    if (account_storage_contains_unreadable_records(root_directory, &storage_error)) {
        set_error(error_message, storage_error);
        return false;
    }

    AccountData existing_account;
    if (find_account_by_email_internal(root_directory, email, &existing_account, nullptr)) {
        set_error(error_message, "An account already exists for that email address.");
        return false;
    }

    const std::string normalized_email = normalize_email(email);
    for (int sequence_number = 0; sequence_number < 1000; ++sequence_number) {
        const std::string candidate_name = make_account_name_candidate_from_email(normalized_email, sequence_number);
        AccountData candidate_account;
        std::string create_error;
        if (create_account(root_directory, candidate_name, normalized_email, password, created_at, &candidate_account, &create_error)) {
            if (account)
                *account = candidate_account;

            set_error(error_message, "");
            return true;
        }

        if (create_error != "Account already exists.") {
            set_error(error_message, create_error);
            return false;
        }
    }

    set_error(error_message, "Unable to allocate a unique account record for that email address.");
    return false;
}

bool authenticate_account(const std::string& root_directory, const std::string& account_name, const std::string& password, AccountData* account, std::string* error_message)
{
    if (!validate_identifier_for_path(account_name, "Account name", error_message))
        return false;

    AccountData stored_account;
    if (!read_account_file(root_directory, account_name, &stored_account, nullptr)) {
        set_error(error_message, "Account authentication failed.");
        return false;
    }

    if (stored_account.blocked) {
        set_error(error_message, "Account authentication failed.");
        return false;
    }

    if (!verify_password(password, stored_account.password_hash)) {
        set_error(error_message, "Account authentication failed.");
        return false;
    }

    if (!stored_account.email_verified) {
        if (account)
            *account = stored_account;
        set_error(error_message, "Account email verification is still pending.");
        return false;
    }

    if (account)
        *account = stored_account;

    set_error(error_message, "");
    return true;
}

bool authenticate_account_by_email(const std::string& root_directory, const std::string& email, const std::string& password, AccountData* account, std::string* error_message)
{
    if (!is_valid_email(email, error_message))
        return false;

    AccountData stored_account;
    if (!find_account_by_email_internal(root_directory, email, &stored_account, nullptr)) {
        set_error(error_message, "Account authentication failed.");
        return false;
    }

    return authenticate_account(root_directory, stored_account.account_name, password, account, error_message);
}

bool start_email_verification(const std::string& root_directory, const std::string& account_name, long sent_at, AccountData* account, std::string* error_message)
{
    if (!validate_identifier_for_path(account_name, "Account name", error_message))
        return false;

    AccountData stored_account;
    if (!read_account_file(root_directory, account_name, &stored_account, error_message))
        return false;

    if (stored_account.email_verified) {
        if (account)
            *account = stored_account;
        set_error(error_message, "");
        return true;
    }

    if (stored_account.verification_code_sent_at != 0
        && sent_at < stored_account.verification_code_sent_at + EMAIL_VERIFICATION_RESEND_COOLDOWN_SECONDS) {
        const long retry_after_seconds = (stored_account.verification_code_sent_at + EMAIL_VERIFICATION_RESEND_COOLDOWN_SECONDS) - sent_at;
        set_error(error_message, "Please wait " + std::to_string(retry_after_seconds) + " seconds before requesting another verification code.");
        return false;
    }

    std::string verification_code;
    if (!prepare_email_verification_code(&stored_account, sent_at, &verification_code, error_message))
        return false;

    if (!write_account_file(root_directory, stored_account, error_message))
        return false;

    if (!send_verification_email(stored_account, verification_code, error_message))
        return false;

    if (account)
        *account = stored_account;

    set_error(error_message, "");
    return true;
}

bool complete_email_verification(const std::string& root_directory, const std::string& account_name, const std::string& verification_code, const std::string& verified_by, long verified_at, AccountData* account, std::string* error_message)
{
    if (!validate_identifier_for_path(account_name, "Account name", error_message))
        return false;

    AccountData stored_account;
    if (!read_account_file(root_directory, account_name, &stored_account, error_message))
        return false;

    const std::string original_verification_code_hash = stored_account.verification_code_hash;
    const int original_attempt_count = stored_account.verification_attempt_count;
    const long original_last_attempt_at = stored_account.verification_last_attempt_at;
    const long original_updated_at = stored_account.updated_at;

    if (!confirm_email_verification_code(&stored_account, verification_code, verified_by, verified_at, error_message)) {
        if (stored_account.verification_code_hash != original_verification_code_hash
            || stored_account.verification_attempt_count != original_attempt_count
            || stored_account.verification_last_attempt_at != original_last_attempt_at
            || stored_account.updated_at != original_updated_at) {
            std::string persistence_error;
            if (!write_account_file(root_directory, stored_account, &persistence_error)) {
                set_error(error_message, persistence_error);
            }
        }
        return false;
    }

    if (!write_account_file(root_directory, stored_account, error_message))
        return false;

    if (account)
        *account = stored_account;

    set_error(error_message, "");
    return true;
}

bool find_linked_character_owner_account(const std::string& root_directory, const std::string& character_name, std::string* owner_account_name, std::string* error_message)
{
    if (!validate_identifier_for_path(character_name, "Character name", error_message))
        return false;

    return find_character_owner_account(root_directory, character_name, owner_account_name, error_message);
}

bool admin_link_character(const std::string& root_directory, const std::string& account_name, const std::string& character_name, long updated_at, AccountData* account, std::string* error_message)
{
    if (!validate_identifier_for_path(account_name, "Account name", error_message))
        return false;
    if (!validate_identifier_for_path(character_name, "Character name", error_message))
        return false;

    AccountData stored_account;
    if (!read_account_file(root_directory, account_name, &stored_account, error_message))
        return false;

    std::string owner_account_name;
    if (!find_character_owner_account(root_directory, character_name, &owner_account_name, error_message))
        return false;
    if (!owner_account_name.empty() && normalize_account_name(owner_account_name) != normalize_account_name(account_name)) {
        set_error(error_message, "Character is already linked to account '" + owner_account_name + "'.");
        return false;
    }

    if (!add_character_to_account(&stored_account, character_name, error_message))
        return false;

    stored_account.updated_at = updated_at;
    if (!write_account_file(root_directory, stored_account, error_message))
        return false;

    if (account)
        *account = stored_account;

    set_error(error_message, "");
    return true;
}

bool admin_link_and_migrate_character(const std::string& root_directory, const std::string& account_name, const std::string& character_name, long updated_at, AccountData* account, CharacterMigrationData* migration, std::string* error_message)
{
    if (!validate_identifier_for_path(account_name, "Account name", error_message))
        return false;
    if (!validate_identifier_for_path(character_name, "Character name", error_message))
        return false;

    AccountData stored_account;
    if (!read_account_file(root_directory, account_name, &stored_account, error_message))
        return false;

    const bool already_linked = account_has_character(stored_account, character_name);

    if (!already_linked) {
        std::string owner_account_name;
        if (!find_character_owner_account(root_directory, character_name, &owner_account_name, error_message))
            return false;
        if (!owner_account_name.empty() && normalize_account_name(owner_account_name) != normalize_account_name(account_name)) {
            set_error(error_message, "Character is already linked to account '" + owner_account_name + "'.");
            return false;
        }
    }

    CharacterMigrationData migrated_character;
    if (!migrate_legacy_character_by_name(root_directory, account_name, character_name, updated_at, &migrated_character, error_message))
        return false;

    if (!already_linked) {
        if (!add_character_to_account(&stored_account, character_name, error_message))
            return false;

        stored_account.updated_at = updated_at;
        if (!write_account_file(root_directory, stored_account, error_message))
            return false;
    }

    if (account)
        *account = stored_account;
    if (migration)
        *migration = migrated_character;

    set_error(error_message, "");
    return true;
}

bool admin_block_account(const std::string& root_directory, const std::string& account_name, const std::string& blocked_by, const std::string& block_reason, long blocked_at, AccountData* account, std::string* error_message)
{
    if (!validate_identifier_for_path(account_name, "Account name", error_message))
        return false;

    AccountData stored_account;
    if (!read_account_file(root_directory, account_name, &stored_account, error_message))
        return false;

    block_account(&stored_account, blocked_by, block_reason, blocked_at);
    if (!write_account_file(root_directory, stored_account, error_message))
        return false;

    if (account)
        *account = stored_account;

    set_error(error_message, "");
    return true;
}

bool admin_verify_email(const std::string& root_directory, const std::string& account_name, const std::string& verified_by, long verified_at, AccountData* account, std::string* error_message)
{
    if (!validate_identifier_for_path(account_name, "Account name", error_message))
        return false;

    AccountData stored_account;
    if (!read_account_file(root_directory, account_name, &stored_account, error_message))
        return false;

    verify_email(&stored_account, verified_by, verified_at);
    if (!write_account_file(root_directory, stored_account, error_message))
        return false;

    if (account)
        *account = stored_account;

    set_error(error_message, "");
    return true;
}

bool admin_unverify_email(const std::string& root_directory, const std::string& account_name, long updated_at, AccountData* account, std::string* error_message)
{
    if (!validate_identifier_for_path(account_name, "Account name", error_message))
        return false;

    AccountData stored_account;
    if (!read_account_file(root_directory, account_name, &stored_account, error_message))
        return false;

    unverify_email(&stored_account);
    stored_account.updated_at = updated_at;
    if (!write_account_file(root_directory, stored_account, error_message))
        return false;

    if (account)
        *account = stored_account;

    set_error(error_message, "");
    return true;
}

bool admin_unblock_account(const std::string& root_directory, const std::string& account_name, long updated_at, AccountData* account, std::string* error_message)
{
    if (!validate_identifier_for_path(account_name, "Account name", error_message))
        return false;

    AccountData stored_account;
    if (!read_account_file(root_directory, account_name, &stored_account, error_message))
        return false;

    unblock_account(&stored_account);
    stored_account.updated_at = updated_at;
    if (!write_account_file(root_directory, stored_account, error_message))
        return false;

    if (account)
        *account = stored_account;

    set_error(error_message, "");
    return true;
}

bool admin_reset_password(const std::string& root_directory, const std::string& account_name, const std::string& new_password, const std::string& reset_by, long reset_at, AccountData* account, std::string* error_message)
{
    if (!validate_identifier_for_path(account_name, "Account name", error_message))
        return false;

    AccountData stored_account;
    if (!read_account_file(root_directory, account_name, &stored_account, error_message))
        return false;

    if (!reset_account_password(&stored_account, new_password, reset_by, reset_at, error_message))
        return false;

    if (!write_account_file(root_directory, stored_account, error_message))
        return false;

    if (account)
        *account = stored_account;

    set_error(error_message, "");
    return true;
}

bool link_and_migrate_character(const std::string& root_directory, const std::string& account_name, const std::string& password, const std::string& character_name, long updated_at, AccountData* account, CharacterMigrationData* migration, std::string* error_message)
{
    if (!validate_identifier_for_path(account_name, "Account name", error_message))
        return false;
    if (!validate_identifier_for_path(character_name, "Character name", error_message))
        return false;

    AccountData authenticated_account;
    if (!authenticate_account(root_directory, account_name, password, &authenticated_account, error_message))
        return false;

    return admin_link_and_migrate_character(root_directory, account_name, character_name, updated_at, account, migration, error_message);
}

std::string account_bucket_for_name(const std::string& name)
{
    const std::string normalized_name = normalize_account_name(name);
    if (normalized_name.empty())
        return "ZZZ";

    switch (normalized_name[0]) {
    case 'a':
    case 'b':
    case 'c':
    case 'd':
    case 'e':
        return "A-E";
    case 'f':
    case 'g':
    case 'h':
    case 'i':
    case 'j':
        return "F-J";
    case 'k':
    case 'l':
    case 'm':
    case 'n':
    case 'o':
        return "K-O";
    case 'p':
    case 'q':
    case 'r':
    case 's':
    case 't':
        return "P-T";
    case 'u':
    case 'v':
    case 'w':
    case 'x':
    case 'y':
    case 'z':
        return "U-Z";
    default:
        return "ZZZ";
    }
}

std::string account_file_path(const std::string& root_directory, const std::string& account_name)
{
    const std::string normalized_name = normalize_account_name(account_name);
    return root_directory + "/accounts/" + account_bucket_for_name(normalized_name) + "/" + normalized_name + ".json";
}

std::string legacy_player_file_path(const std::string& root_directory, const std::string& character_name)
{
    const std::string normalized_name = normalize_account_name(character_name);
    return root_directory + "/players/" + account_bucket_for_name(normalized_name) + "/" + normalized_name;
}

std::string legacy_object_file_path(const std::string& root_directory, const std::string& character_name)
{
    const std::string normalized_name = normalize_account_name(character_name);
    return root_directory + "/plrobjs/" + account_bucket_for_name(normalized_name) + "/" + normalized_name + ".obj";
}

std::string legacy_exploits_file_path(const std::string& root_directory, const std::string& character_name)
{
    const std::string normalized_name = normalize_account_name(character_name);
    return root_directory + "/exploits/" + account_bucket_for_name(normalized_name) + "/" + normalized_name + ".exploits";
}

std::string serialize_account_to_json(const AccountData& account)
{
    std::ostringstream output;
    output << "{\n";
    output << "  \"version\": " << account.version << ",\n";
    output << "  \"account_name\": \"" << json_escape(account.account_name) << "\",\n";
    output << "  \"normalized_email\": \"" << json_escape(account.normalized_email) << "\",\n";
    output << "  \"password_hash\": \"" << json_escape(account.password_hash) << "\",\n";
    output << "  \"password_salt\": \"" << json_escape(account.password_salt) << "\",\n";
    output << "  \"characters\": [";
    for (size_t index = 0; index < account.characters.size(); ++index) {
        if (index > 0)
            output << ", ";
        output << "\"" << json_escape(account.characters[index]) << "\"";
    }
    output << "],\n";
    output << "  \"email_verified\": " << (account.email_verified ? "true" : "false") << ",\n";
    output << "  \"email_verified_by\": \"" << json_escape(account.email_verified_by) << "\",\n";
    output << "  \"email_verified_at\": " << account.email_verified_at << ",\n";
    output << "  \"verification_code_hash\": \"" << json_escape(account.verification_code_hash) << "\",\n";
    output << "  \"verification_code_sent_at\": " << account.verification_code_sent_at << ",\n";
    output << "  \"verification_code_expires_at\": " << account.verification_code_expires_at << ",\n";
    output << "  \"verification_attempt_count\": " << account.verification_attempt_count << ",\n";
    output << "  \"verification_last_attempt_at\": " << account.verification_last_attempt_at << ",\n";
    output << "  \"blocked\": " << (account.blocked ? "true" : "false") << ",\n";
    output << "  \"block_reason\": \"" << json_escape(account.block_reason) << "\",\n";
    output << "  \"blocked_by\": \"" << json_escape(account.blocked_by) << "\",\n";
    output << "  \"blocked_at\": " << account.blocked_at << ",\n";
    output << "  \"created_at\": " << account.created_at << ",\n";
    output << "  \"updated_at\": " << account.updated_at << ",\n";
    output << "  \"password_reset_at\": " << account.password_reset_at << ",\n";
    output << "  \"password_reset_by\": \"" << json_escape(account.password_reset_by) << "\"\n";
    output << "}\n";
    return output.str();
}

bool deserialize_account_from_json(const std::string& json, AccountData* account, std::string* error_message)
{
    if (account == nullptr) {
        set_error(error_message, "Account output parameter must not be null.");
        return false;
    }

    AccountData parsed_account;
    JsonReader reader(json);
    if (!reader.parse_account(&parsed_account, error_message))
        return false;

    if (parsed_account.version != ACCOUNT_SCHEMA_VERSION) {
        set_error(error_message, "Unsupported account schema version.");
        return false;
    }

    if (!is_valid_account_name(parsed_account.account_name, error_message))
        return false;

    parsed_account.account_name = normalize_account_name(parsed_account.account_name);
    parsed_account.normalized_email = normalize_email(parsed_account.normalized_email);

    *account = std::move(parsed_account);
    set_error(error_message, "");
    return true;
}

bool write_account_file(const std::string& root_directory, const AccountData& account, std::string* error_message)
{
    if (!validate_identifier_for_path(account.account_name, "Account name", error_message))
        return false;

    const std::string accounts_directory = root_directory + "/accounts";
    const std::string bucket_directory = accounts_directory + "/" + account_bucket_for_name(account.account_name);
    const std::string final_path = account_file_path(root_directory, account.account_name);
    const std::string temp_path = final_path + ".tmp";

    if (!create_directory_if_missing(accounts_directory, error_message))
        return false;
    if (!create_directory_if_missing(bucket_directory, error_message))
        return false;

    FILE* file = open_secure_output_file(temp_path, error_message);
    if (file == nullptr) {
        return false;
    }

    AccountData normalized_account = account;
    normalized_account.account_name = normalize_account_name(account.account_name);
    normalized_account.normalized_email = normalize_email(account.normalized_email);
    const std::string json = serialize_account_to_json(normalized_account);

    const size_t written_length = std::fwrite(json.data(), sizeof(char), json.size(), file);
    const int close_result = std::fclose(file);
    if (written_length != json.size() || close_result != 0) {
        std::remove(temp_path.c_str());
        set_error(error_message, "Failed to write temporary account file '" + temp_path + "'.");
        return false;
    }

    if (std::rename(temp_path.c_str(), final_path.c_str()) != 0) {
        std::remove(temp_path.c_str());
        set_error(error_message, "Failed to move temporary account file into place: " + std::string(std::strerror(errno)));
        return false;
    }

    set_error(error_message, "");
    return true;
}

bool read_account_file(const std::string& root_directory, const std::string& account_name, AccountData* account, std::string* error_message)
{
    if (account == nullptr) {
        set_error(error_message, "Account output parameter must not be null.");
        return false;
    }

    if (!validate_identifier_for_path(account_name, "Account name", error_message))
        return false;

    return read_account_file_from_path(account_file_path(root_directory, account_name), account, error_message);
}

bool read_account_file_by_email(const std::string& root_directory, const std::string& email, AccountData* account, std::string* error_message)
{
    if (!is_valid_email(email, error_message))
        return false;

    return find_account_by_email_internal(root_directory, email, account, error_message);
}

std::string account_character_directory(const std::string& root_directory, const std::string& account_name, const std::string& character_name)
{
    const std::string normalized_account_name = normalize_account_name(account_name);
    const std::string normalized_character_name = normalize_account_name(character_name);
    return root_directory + "/account_characters/" + account_bucket_for_name(normalized_account_name) + "/" + normalized_account_name + "/" + normalized_character_name;
}

std::string account_character_snapshot_path(const std::string& root_directory, const std::string& account_name, const std::string& character_name)
{
    return account_character_directory(root_directory, account_name, character_name) + "/snapshot.json";
}

std::string serialize_character_migration_to_json(const CharacterMigrationData& migration)
{
    auto write_snapshot = [](std::ostringstream& output, const char* name, const LegacyAssetSnapshot& snapshot) {
        output << "  \"" << name << "\": {\n";
        output << "    \"source_path\": \"" << json_escape(snapshot.source_path) << "\",\n";
        output << "    \"encoding\": \"" << json_escape(snapshot.encoding) << "\",\n";
        output << "    \"content\": \"" << json_escape(snapshot.content) << "\",\n";
        output << "    \"present\": " << (snapshot.present ? "true" : "false") << "\n";
        output << "  }";
    };

    std::ostringstream output;
    output << "{\n";
    output << "  \"version\": " << migration.version << ",\n";
    output << "  \"account_name\": \"" << json_escape(migration.account_name) << "\",\n";
    output << "  \"character_name\": \"" << json_escape(migration.character_name) << "\",\n";
    output << "  \"migrated_at\": " << migration.migrated_at << ",\n";
    write_snapshot(output, "player_file", migration.player_file);
    output << ",\n";
    write_snapshot(output, "object_file", migration.object_file);
    output << ",\n";
    write_snapshot(output, "exploits_file", migration.exploits_file);
    output << "\n}\n";
    return output.str();
}

bool deserialize_character_migration_from_json(const std::string& json, CharacterMigrationData* migration, std::string* error_message)
{
    if (migration == nullptr) {
        set_error(error_message, "Migration output parameter must not be null.");
        return false;
    }

    CharacterMigrationData parsed_migration;
    JsonReader reader(json);
    if (!reader.parse_character_migration(&parsed_migration, error_message))
        return false;

    if (parsed_migration.version != ACCOUNT_SCHEMA_VERSION) {
        set_error(error_message, "Unsupported character migration schema version.");
        return false;
    }

    if (!is_valid_account_name(parsed_migration.account_name, error_message))
        return false;

    parsed_migration.account_name = normalize_account_name(parsed_migration.account_name);
    parsed_migration.character_name = normalize_account_name(parsed_migration.character_name);

    std::string decoded_content;
    if (parsed_migration.player_file.present && parsed_migration.player_file.encoding == "hex" && !hex_decode(parsed_migration.player_file.content, &decoded_content, error_message))
        return false;
    if (parsed_migration.object_file.present && parsed_migration.object_file.encoding == "hex" && !hex_decode(parsed_migration.object_file.content, &decoded_content, error_message))
        return false;
    if (parsed_migration.exploits_file.present && parsed_migration.exploits_file.encoding == "hex" && !hex_decode(parsed_migration.exploits_file.content, &decoded_content, error_message))
        return false;

    *migration = std::move(parsed_migration);
    set_error(error_message, "");
    return true;
}

namespace {

    bool write_character_migration_snapshot(const std::string& root_directory, const CharacterMigrationData& snapshot_data, CharacterMigrationData* migration, std::string* error_message)
    {
        const std::string account_character_root = root_directory + "/account_characters";
        const std::string bucket_directory = account_character_root + "/" + account_bucket_for_name(snapshot_data.account_name);
        const std::string account_directory = bucket_directory + "/" + snapshot_data.account_name;
        const std::string character_directory = account_character_directory(root_directory, snapshot_data.account_name, snapshot_data.character_name);
        const std::string final_path = account_character_snapshot_path(root_directory, snapshot_data.account_name, snapshot_data.character_name);
        const std::string temp_path = final_path + ".tmp";

        if (!create_directory_if_missing(account_character_root, error_message))
            return false;
        if (!create_directory_if_missing(bucket_directory, error_message))
            return false;
        if (!create_directory_if_missing(account_directory, error_message))
            return false;
        if (!create_directory_if_missing(character_directory, error_message))
            return false;

        FILE* file = open_secure_output_file(temp_path, error_message);
        if (file == nullptr)
            return false;

        const std::string json = serialize_character_migration_to_json(snapshot_data);
        const size_t written_length = std::fwrite(json.data(), sizeof(char), json.size(), file);
        const int close_result = std::fclose(file);
        if (written_length != json.size() || close_result != 0) {
            std::remove(temp_path.c_str());
            set_error(error_message, "Failed to write temporary migration file '" + temp_path + "'.");
            return false;
        }

        if (std::rename(temp_path.c_str(), final_path.c_str()) != 0) {
            std::remove(temp_path.c_str());
            set_error(error_message, "Failed to move temporary migration file into place: " + std::string(std::strerror(errno)));
            return false;
        }

        if (migration)
            *migration = snapshot_data;

        set_error(error_message, "");
        return true;
    }

    bool migrate_legacy_character_files_internal(const std::string& root_directory, const std::string& account_name, const std::string& character_name, const std::string& player_file_path, const std::string& object_file_path, const std::string& exploits_file_path, long migrated_at, CharacterMigrationData* migration, std::string* error_message)
    {
        CharacterMigrationData snapshot_data;
        snapshot_data.account_name = normalize_account_name(account_name);
        snapshot_data.character_name = normalize_account_name(character_name);
        snapshot_data.migrated_at = migrated_at;

        if (!read_file_bytes(player_file_path, true, &snapshot_data.player_file, error_message))
            return false;
        if (!read_file_bytes(object_file_path, false, &snapshot_data.object_file, error_message))
            return false;
        if (!read_file_bytes(exploits_file_path, false, &snapshot_data.exploits_file, error_message))
            return false;
        return write_character_migration_snapshot(root_directory, snapshot_data, migration, error_message);
    }

} // namespace

bool migrate_legacy_character_by_name(const std::string& root_directory, const std::string& account_name, const std::string& character_name, long migrated_at, CharacterMigrationData* migration, std::string* error_message)
{
    return migrate_legacy_character_files_internal(root_directory, account_name, character_name,
        legacy_player_file_path(root_directory, character_name),
        legacy_object_file_path(root_directory, character_name),
        legacy_exploits_file_path(root_directory, character_name),
        migrated_at, migration, error_message);
}

bool read_character_migration(const std::string& root_directory, const std::string& account_name, const std::string& character_name, CharacterMigrationData* migration, std::string* error_message)
{
    if (migration == nullptr) {
        set_error(error_message, "Migration output parameter must not be null.");
        return false;
    }

    const std::string path = account_character_snapshot_path(root_directory, account_name, character_name);
    FILE* file = std::fopen(path.c_str(), "r");
    if (file == nullptr) {
        set_error(error_message, "Failed to open migration file '" + path + "': " + std::strerror(errno));
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
                set_error(error_message, "Failed to read migration file '" + path + "'.");
                return false;
            }
            break;
        }
    }

    std::fclose(file);
    return deserialize_character_migration_from_json(json, migration, error_message);
}

bool ensure_character_migration(const std::string& root_directory, const std::string& account_name, const std::string& character_name, long migrated_at, CharacterMigrationData* migration, std::string* error_message)
{
    const std::string path = account_character_snapshot_path(root_directory, account_name, character_name);
    struct stat file_info { };
    if (stat(path.c_str(), &file_info) == 0) {
        if (read_character_migration(root_directory, account_name, character_name, migration, error_message))
            return true;
    } else if (errno != ENOENT) {
        set_error(error_message, "Failed to inspect migration file '" + path + "': " + std::strerror(errno));
        return false;
    }

    return migrate_legacy_character_by_name(root_directory, account_name, character_name, migrated_at, migration, error_message);
}

bool decode_snapshot_content(const LegacyAssetSnapshot& snapshot, std::string* contents, std::string* error_message)
{
    if (contents == nullptr) {
        set_error(error_message, "Snapshot content output parameter must not be null.");
        return false;
    }

    if (!snapshot.present) {
        contents->clear();
        set_error(error_message, "Requested snapshot content is not present.");
        return false;
    }

    if (snapshot.encoding == "hex")
        return hex_decode(snapshot.content, contents, error_message);

    set_error(error_message, "Unsupported snapshot encoding '" + snapshot.encoding + "'.");
    return false;
}

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

bool restore_character_migration(const std::string& root_directory, const std::string& expected_account_name, const std::string& expected_character_name, const CharacterMigrationData& migration, std::string* error_message)
{
    if (!validate_migration_identity(migration, expected_account_name, expected_character_name, error_message))
        return false;

    if (!write_snapshot_bytes(legacy_player_file_path(root_directory, migration.character_name), migration.player_file, true, error_message))
        return false;
    if (!write_snapshot_bytes(legacy_object_file_path(root_directory, migration.character_name), migration.object_file, false, error_message))
        return false;
    if (!write_snapshot_bytes(legacy_exploits_file_path(root_directory, migration.character_name), migration.exploits_file, false, error_message))
        return false;

    set_error(error_message, "");
    return true;
}

bool restore_character_runtime_support_files(const std::string& root_directory, const std::string& expected_account_name, const std::string& expected_character_name, const CharacterMigrationData& migration, std::string* error_message)
{
    if (!validate_migration_identity(migration, expected_account_name, expected_character_name, error_message))
        return false;

    if (!write_snapshot_bytes(legacy_object_file_path(root_directory, migration.character_name), migration.object_file, false, error_message))
        return false;

    const std::string exploits_path = legacy_exploits_file_path(root_directory, migration.character_name);
    if (std::remove(exploits_path.c_str()) != 0 && errno != ENOENT) {
        set_error(error_message, "Failed to remove stale legacy file '" + exploits_path + "': " + std::strerror(errno));
        return false;
    }

    set_error(error_message, "");
    return true;
}

bool refresh_linked_character_snapshot(const std::string& root_directory, const std::string& character_name, long migrated_at, CharacterMigrationData* migration, std::string* error_message)
{
    std::string owner_account_name;
    if (!find_linked_character_owner_account(root_directory, character_name, &owner_account_name, error_message))
        return false;

    if (owner_account_name.empty()) {
        if (migration)
            *migration = CharacterMigrationData {};
        set_error(error_message, "");
        return true;
    }

    CharacterMigrationData existing_migration;
    const bool had_existing_snapshot = read_character_migration(root_directory, owner_account_name, character_name, &existing_migration, nullptr);

    CharacterMigrationData refreshed_migration;
    if (!migrate_legacy_character_by_name(root_directory, owner_account_name, character_name, migrated_at, &refreshed_migration, error_message))
        return false;

    if (!refreshed_migration.exploits_file.present && had_existing_snapshot && existing_migration.exploits_file.present) {
        refreshed_migration.exploits_file = existing_migration.exploits_file;
        if (!write_character_migration_snapshot(root_directory, refreshed_migration, &refreshed_migration, error_message))
            return false;
    }

    if (migration)
        *migration = refreshed_migration;

    set_error(error_message, "");
    return true;
}

std::string format_account_character_prompt(const AccountData& account)
{
    std::ostringstream output;
    output << "\n\rLinked characters for account '" << account.account_name << "':\n\r";
    for (const std::string& character_name : account.characters)
        output << "  " << character_name << "\n\r";
    output << "\n\rCharacter: ";
    return output.str();
}

std::string format_account_summary(const AccountData& account)
{
    std::ostringstream output;
    output << "Account: " << account.account_name << "\n\r";
    output << "Email: " << account.normalized_email << "\n\r";
    output << "Email verified: " << (account.email_verified ? "yes" : "no") << "\n\r";
    if (account.email_verified) {
        output << "Verified by: " << account.email_verified_by << "\n\r";
        output << "Verified at: " << account.email_verified_at << "\n\r";
    } else if (!account.verification_code_hash.empty()) {
        output << "Verification code sent at: " << account.verification_code_sent_at << "\n\r";
        output << "Verification code expires at: " << account.verification_code_expires_at << "\n\r";
        output << "Verification attempts: " << account.verification_attempt_count << "\n\r";
    }
    output << "Blocked: " << (account.blocked ? "yes" : "no") << "\n\r";
    if (account.blocked) {
        output << "Blocked by: " << account.blocked_by << "\n\r";
        output << "Block reason: " << account.block_reason << "\n\r";
    }
    output << "Created: " << account.created_at << "\n\r";
    output << "Updated: " << account.updated_at << "\n\r";
    output << "Characters (" << account.characters.size() << "): ";
    if (account.characters.empty()) {
        output << "(none)";
    } else {
        for (size_t index = 0; index < account.characters.size(); ++index) {
            if (index > 0)
                output << ", ";
            output << account.characters[index];
        }
    }
    output << "\n\r";
    return output.str();
}

} // namespace account
