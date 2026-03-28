#include "account_management.h"
#include "character_json.h"
#include "exploits_json.h"
#include "json_utils.h"
#include "objects_json.h"

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
#include <sstream>
#include <utility>

namespace account {
namespace {

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

namespace {

    CharacterLinkReference* find_character_link_reference(AccountData* account, const std::string& character_name)
    {
        if (account == nullptr)
            return nullptr;

        const std::string normalized_character_name = normalize_account_name(character_name);
        for (CharacterLinkReference& link : account->character_links) {
            if (normalize_account_name(link.character_name) == normalized_character_name)
                return &link;
        }

        return nullptr;
    }

    const CharacterLinkReference* find_character_link_reference(const AccountData& account, const std::string& character_name)
    {
        const std::string normalized_character_name = normalize_account_name(character_name);
        for (const CharacterLinkReference& link : account.character_links) {
            if (normalize_account_name(link.character_name) == normalized_character_name)
                return &link;
        }

        return nullptr;
    }

    std::string resolved_character_path(const AccountData& account, const std::string& root_directory, const std::string& character_name)
    {
        return account_character_player_path(root_directory, account.account_name, character_name);
    }

    std::string resolved_object_path(const AccountData& account, const std::string& root_directory, const std::string& character_name)
    {
        const CharacterLinkReference* link = find_character_link_reference(account, character_name);
        if (link != nullptr && !link->object_path.empty())
            return account_character_directory(root_directory, account.account_name, character_name) + "/" + link->object_path;
        return account_character_object_path(root_directory, account.account_name, character_name);
    }

    std::string resolved_exploits_path(const AccountData& account, const std::string& root_directory, const std::string& character_name)
    {
        const CharacterLinkReference* link = find_character_link_reference(account, character_name);
        if (link != nullptr && !link->exploits_path.empty())
            return account_character_directory(root_directory, account.account_name, character_name) + "/" + link->exploits_path;
        return account_character_exploits_path(root_directory, account.account_name, character_name);
    }

    std::string safe_relative_object_path_or_empty(const std::string& object_path, const std::string& expected_basename)
    {
        if (object_path.empty())
            return "";
        if (object_path[0] == '/' || object_path.find('\\') != std::string::npos)
            return "";

        size_t segment_start = 0;
        while (segment_start <= object_path.size()) {
            const size_t slash = object_path.find('/', segment_start);
            const size_t segment_end = (slash == std::string::npos) ? object_path.size() : slash;
            const std::string segment = object_path.substr(segment_start, segment_end - segment_start);
            if (segment.empty() || segment == "." || segment == "..")
                return "";
            if (slash == std::string::npos)
                break;
            segment_start = slash + 1;
        }

        const size_t last_slash = object_path.find_last_of('/');
        const std::string basename = (last_slash == std::string::npos) ? object_path : object_path.substr(last_slash + 1);
        if (basename != expected_basename)
            return "";

        return object_path;
    }

    bool validate_account_owned_object_path(const AccountData& account, const std::string& character_name, std::string* error_message)
    {
        const CharacterLinkReference* link = find_character_link_reference(account, character_name);
        if (link == nullptr || link->object_path.empty()) {
            set_error(error_message, "");
            return true;
        }

        const std::string expected_path = objects_json_file_name(character_name);
        if (safe_relative_object_path_or_empty(link->object_path, expected_path).empty()) {
            set_error(error_message, "Stored object path did not match the expected account-owned object filename.");
            return false;
        }

        set_error(error_message, "");
        return true;
    }

    bool validate_account_owned_character_path(const AccountData& account, const std::string& character_name, std::string* error_message)
    {
        const CharacterLinkReference* link = find_character_link_reference(account, character_name);
        if (link == nullptr || link->character_path.empty()) {
            set_error(error_message, "");
            return true;
        }

        const std::string expected_path = character_json_file_name(character_name);
        if (link->character_path != expected_path) {
            set_error(error_message, "Stored character path did not match the expected account-owned character filename.");
            return false;
        }

        set_error(error_message, "");
        return true;
    }

    bool validate_account_owned_exploits_path(const AccountData& account, const std::string& character_name, std::string* error_message)
    {
        const CharacterLinkReference* link = find_character_link_reference(account, character_name);
        if (link == nullptr || link->exploits_path.empty()) {
            set_error(error_message, "");
            return true;
        }

        const std::string expected_path = exploits_json_file_name(character_name);
        if (link->exploits_path != expected_path) {
            set_error(error_message, "Stored exploits path did not match the expected account-owned exploits filename.");
            return false;
        }

        set_error(error_message, "");
        return true;
    }

    void populate_default_character_link_paths(CharacterLinkReference* link)
    {
        if (link == nullptr)
            return;

        const std::string normalized_character_name = normalize_account_name(link->character_name);
        if (normalized_character_name.empty())
            return;

        if (link->character_path.empty())
            link->character_path = character_json_file_name(normalized_character_name);
        if (link->object_path.empty())
            link->object_path = objects_json_file_name(normalized_character_name);
        if (link->exploits_path.empty())
            link->exploits_path = exploits_json_file_name(normalized_character_name);
    }

    void sync_character_links_from_characters(AccountData* account)
    {
        if (account == nullptr)
            return;

        for (CharacterLinkReference& link : account->character_links) {
            link.character_name = normalize_account_name(link.character_name);
            populate_default_character_link_paths(&link);
        }

        for (const std::string& character_name : account->characters) {
            if (find_character_link_reference(account, character_name) != nullptr)
                continue;

            CharacterLinkReference link;
            link.character_name = normalize_account_name(character_name);
            populate_default_character_link_paths(&link);
            account->character_links.push_back(link);
        }
    }

} // namespace

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
    sync_character_links_from_characters(account);
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

    std::string existing_account_path;
    if (find_account_file_path_by_account_name(root_directory, account_name, &existing_account_path, nullptr)) {
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
    return account_file_path_from_email(root_directory, account_name);
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
    output << "  \"account_name\": \"" << json_utils::escape_json_string(account.account_name) << "\",\n";
    output << "  \"normalized_email\": \"" << json_utils::escape_json_string(account.normalized_email) << "\",\n";
    output << "  \"password_hash\": \"" << json_utils::escape_json_string(account.password_hash) << "\",\n";
    output << "  \"password_salt\": \"" << json_utils::escape_json_string(account.password_salt) << "\",\n";
    output << "  \"characters\": [";
    for (size_t index = 0; index < account.characters.size(); ++index) {
        if (index > 0)
            output << ", ";
        output << "\"" << json_utils::escape_json_string(account.characters[index]) << "\"";
    }
    output << "],\n";
    output << "  \"character_links\": [";
    for (size_t index = 0; index < account.character_links.size(); ++index) {
        const CharacterLinkReference& link = account.character_links[index];
        if (index > 0)
            output << ", ";
        output << "{";
        output << "\"character_name\": \"" << json_utils::escape_json_string(link.character_name) << "\", ";
        output << "\"character_path\": \"" << json_utils::escape_json_string(json_path_or_empty(link.character_path)) << "\", ";
        output << "\"object_path\": \"" << json_utils::escape_json_string(json_path_or_empty(link.object_path)) << "\", ";
        output << "\"exploits_path\": \"" << json_utils::escape_json_string(json_path_or_empty(link.exploits_path)) << "\"";
        output << "}";
    }
    output << "],\n";
    output << "  \"email_verified\": " << (account.email_verified ? "true" : "false") << ",\n";
    output << "  \"email_verified_by\": \"" << json_utils::escape_json_string(account.email_verified_by) << "\",\n";
    output << "  \"email_verified_at\": " << account.email_verified_at << ",\n";
    output << "  \"verification_code_hash\": \"" << json_utils::escape_json_string(account.verification_code_hash) << "\",\n";
    output << "  \"verification_code_sent_at\": " << account.verification_code_sent_at << ",\n";
    output << "  \"verification_code_expires_at\": " << account.verification_code_expires_at << ",\n";
    output << "  \"verification_attempt_count\": " << account.verification_attempt_count << ",\n";
    output << "  \"verification_last_attempt_at\": " << account.verification_last_attempt_at << ",\n";
    output << "  \"blocked\": " << (account.blocked ? "true" : "false") << ",\n";
    output << "  \"block_reason\": \"" << json_utils::escape_json_string(account.block_reason) << "\",\n";
    output << "  \"blocked_by\": \"" << json_utils::escape_json_string(account.blocked_by) << "\",\n";
    output << "  \"blocked_at\": " << account.blocked_at << ",\n";
    output << "  \"created_at\": " << account.created_at << ",\n";
    output << "  \"updated_at\": " << account.updated_at << ",\n";
    output << "  \"password_reset_at\": " << account.password_reset_at << ",\n";
    output << "  \"password_reset_by\": \"" << json_utils::escape_json_string(account.password_reset_by) << "\"\n";
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
    json_utils::JsonReader reader(json);
    if (!reader.parse_root_object([&parsed_account](const std::string& key, json_utils::JsonReader* nested_reader, std::string* nested_error_message) {
            return parse_account_property(key, nested_reader, &parsed_account, nested_error_message);
        },
            error_message))
        return false;

    if (parsed_account.version != ACCOUNT_SCHEMA_VERSION) {
        set_error(error_message, "Unsupported account schema version.");
        return false;
    }

    if (!is_valid_account_name(parsed_account.account_name, error_message))
        return false;

    parsed_account.account_name = normalize_account_name(parsed_account.account_name);
    parsed_account.normalized_email = normalize_email(parsed_account.normalized_email);
    for (std::string& character_name : parsed_account.characters) {
        if (!validate_identifier_for_path(character_name, "Character name", error_message))
            return false;
        character_name = normalize_account_name(character_name);
    }
    for (CharacterLinkReference& link : parsed_account.character_links) {
        if (!validate_identifier_for_path(link.character_name, "Character name", error_message))
            return false;
        link.character_name = normalize_account_name(link.character_name);
    }
    sync_character_links_from_characters(&parsed_account);

    *account = std::move(parsed_account);
    set_error(error_message, "");
    return true;
}

bool write_account_file(const std::string& root_directory, const AccountData& account, std::string* error_message)
{
    if (!validate_identifier_for_path(account.account_name, "Account name", error_message))
        return false;
    if (!is_valid_email(account.normalized_email, error_message))
        return false;

    const std::string accounts_directory = root_directory + "/accounts";
    const std::string normalized_email = normalize_email(account.normalized_email);
    const std::string bucket_directory = accounts_directory + "/" + account_bucket_for_name(normalized_email);
    const std::string account_directory = account_directory_path_from_email(root_directory, normalized_email);
    const std::string final_path = account_file_path_from_email(root_directory, normalized_email);
    const std::string temp_path = final_path + ".tmp";
    const std::string legacy_flat_path = legacy_account_file_path_from_account_name(root_directory, account.account_name);

    std::string existing_account_path;
    const bool found_existing_account_path = find_account_file_path_by_account_name(root_directory, account.account_name, &existing_account_path, nullptr);

    if (!create_directory_if_missing(accounts_directory, error_message))
        return false;
    if (!create_directory_if_missing(bucket_directory, error_message))
        return false;
    if (!create_directory_if_missing(account_directory, error_message))
        return false;

    FILE* file = open_secure_output_file(temp_path, error_message);
    if (file == nullptr) {
        return false;
    }

    AccountData normalized_account = account;
    normalized_account.account_name = normalize_account_name(account.account_name);
    normalized_account.normalized_email = normalized_email;
    sync_character_links_from_characters(&normalized_account);
    for (CharacterLinkReference& link : normalized_account.character_links) {
        link.character_name = normalize_account_name(link.character_name);
        link.character_path = character_json_file_name(link.character_name);
        const std::string expected_object_path = objects_json_file_name(link.character_name);
        if (!safe_relative_object_path_or_empty(link.object_path, expected_object_path).empty())
            link.object_path = expected_object_path;
    }

    if (path_exists(final_path)) {
        AccountData existing_account_at_target;
        if (!read_account_file_from_path(final_path, &existing_account_at_target, nullptr)) {
            set_error(error_message, "Existing account file could not be read safely.");
            return false;
        }

        if (normalize_account_name(existing_account_at_target.account_name) != normalized_account.account_name) {
            set_error(error_message, "Account storage path is already occupied by a different account.");
            return false;
        }
    }

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

    if (found_existing_account_path && existing_account_path != final_path) {
        if (std::remove(existing_account_path.c_str()) != 0 && errno != ENOENT) {
            set_error(error_message, "Failed to retire stale account file '" + existing_account_path + "': " + std::strerror(errno));
            return false;
        }
    }

    if (legacy_flat_path != final_path && std::remove(legacy_flat_path.c_str()) != 0 && errno != ENOENT) {
        set_error(error_message, "Failed to retire legacy account file '" + legacy_flat_path + "': " + std::strerror(errno));
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

    std::string account_path;
    if (!find_account_file_path_by_account_name(root_directory, account_name, &account_path, error_message))
        return false;

    return read_account_file_from_path(account_path, account, error_message);
}

bool read_account_file_by_email(const std::string& root_directory, const std::string& email, AccountData* account, std::string* error_message)
{
    if (!is_valid_email(email, error_message))
        return false;

    return find_account_by_email_internal(root_directory, email, account, error_message);
}

std::string account_character_directory(const std::string& root_directory, const std::string& account_name, const std::string&)
{
    const std::string account_storage_key = resolve_account_storage_key(root_directory, account_name);
    if (account_storage_key.empty())
        return root_directory + "/accounts/__invalid_account__";
    return root_directory + "/accounts/" + account_bucket_for_name(account_storage_key) + "/" + account_storage_key;
}

std::string account_character_snapshot_path(const std::string& root_directory, const std::string& account_name, const std::string& character_name)
{
    return account_character_directory(root_directory, account_name, character_name) + "/" + character_asset_slug(character_name) + ".migration.json";
}

std::string account_character_player_path(const std::string& root_directory, const std::string& account_name, const std::string& character_name)
{
    return account_character_directory(root_directory, account_name, character_name) + "/" + character_json_file_name(character_name);
}

std::string account_character_object_path(const std::string& root_directory, const std::string& account_name, const std::string& character_name)
{
    return account_character_directory(root_directory, account_name, character_name) + "/" + objects_json_file_name(character_name);
}

std::string account_character_exploits_path(const std::string& root_directory, const std::string& account_name, const std::string& character_name)
{
    return account_character_directory(root_directory, account_name, character_name) + "/" + exploits_json_file_name(character_name);
}

std::string serialize_character_migration_to_json(const CharacterMigrationData& migration)
{
    auto write_snapshot = [](std::ostringstream& output, const char* name, const LegacyAssetSnapshot& snapshot) {
        output << "  \"" << name << "\": {\n";
        output << "    \"source_path\": \"" << json_utils::escape_json_string(snapshot.source_path) << "\",\n";
        output << "    \"encoding\": \"" << json_utils::escape_json_string(snapshot.encoding) << "\",\n";
        output << "    \"content\": \"" << json_utils::escape_json_string(snapshot.content) << "\",\n";
        output << "    \"present\": " << (snapshot.present ? "true" : "false") << "\n";
        output << "  }";
    };

    std::ostringstream output;
    output << "{\n";
    output << "  \"version\": " << migration.version << ",\n";
    output << "  \"account_name\": \"" << json_utils::escape_json_string(migration.account_name) << "\",\n";
    output << "  \"character_name\": \"" << json_utils::escape_json_string(migration.character_name) << "\",\n";
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
    json_utils::JsonReader reader(json);
    if (!reader.parse_root_object([&parsed_migration](const std::string& key, json_utils::JsonReader* nested_reader, std::string* nested_error_message) {
            return parse_migration_property(key, nested_reader, &parsed_migration, nested_error_message);
        },
            error_message))
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
        const std::string account_storage_key = resolve_account_storage_key(root_directory, snapshot_data.account_name);
        const std::string account_root = root_directory + "/accounts";
        const std::string bucket_directory = account_root + "/" + account_bucket_for_name(account_storage_key);
        const std::string account_directory = account_character_directory(root_directory, snapshot_data.account_name, snapshot_data.character_name);
        const std::string final_path = account_character_snapshot_path(root_directory, snapshot_data.account_name, snapshot_data.character_name);
        const std::string temp_path = final_path + ".tmp";

        if (!create_directory_if_missing(account_root, error_message))
            return false;
        if (!create_directory_if_missing(bucket_directory, error_message))
            return false;
        if (!create_directory_if_missing(account_directory, error_message))
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

    bool retire_previous_account_object_path(const std::string& root_directory, const std::string& account_name, const std::string& character_name, const std::string& previous_object_path, std::string* error_message)
    {
        const std::string expected_object_path = objects_json_file_name(character_name);
        if (previous_object_path.empty() || previous_object_path == expected_object_path) {
            set_error(error_message, "");
            return true;
        }

        const std::string legacy_safe_path = safe_relative_object_path_or_empty(previous_object_path, expected_object_path);
        if (legacy_safe_path.empty()) {
            set_error(error_message, "");
            return true;
        }

        const std::string final_path = account_character_object_path(root_directory, account_name, character_name);
        const std::string prior_path = account_character_directory(root_directory, account_name, character_name) + "/" + legacy_safe_path;
        if (prior_path != final_path && std::remove(prior_path.c_str()) != 0 && errno != ENOENT) {
            set_error(error_message, "Failed to retire legacy account-owned object file '" + prior_path + "': " + std::strerror(errno));
            return false;
        }

        set_error(error_message, "");
        return true;
    }

    bool prepare_account_object_file_destination(const std::string& root_directory, const std::string& account_name, const std::string& character_name, std::string* final_path, std::string* previous_object_path, std::string* error_message)
    {
        if (final_path == nullptr || previous_object_path == nullptr) {
            set_error(error_message, "Object-file destination outputs must not be null.");
            return false;
        }

        if (!validate_identifier_for_path(account_name, "Account name", error_message))
            return false;
        if (!validate_identifier_for_path(character_name, "Character name", error_message))
            return false;

        AccountData account;
        if (!read_account_file(root_directory, account_name, &account, error_message))
            return false;
        if (!validate_account_owned_object_path(account, character_name, error_message))
            return false;

        CharacterLinkReference* link = find_character_link_reference(&account, character_name);
        *previous_object_path = "";
        const std::string expected_object_path = objects_json_file_name(character_name);
        if (link != nullptr) {
            *previous_object_path = link->object_path;
        }

        const std::string account_directory = account_character_directory(root_directory, account_name, character_name);
        if (!create_directory_if_missing(root_directory + "/accounts", error_message))
            return false;
        if (!create_directory_if_missing(root_directory + "/accounts/" + account_bucket_for_name(resolve_account_storage_key(root_directory, account_name)), error_message))
            return false;
        if (!create_directory_if_missing(account_directory, error_message))
            return false;

        *final_path = account_character_object_path(root_directory, account_name, character_name);
        set_error(error_message, "");
        return true;
    }

    bool normalize_account_object_path_after_successful_write(const std::string& root_directory, const std::string& account_name, const std::string& character_name, const std::string& previous_object_path, std::string* error_message)
    {
        const std::string expected_object_path = objects_json_file_name(character_name);
        if (previous_object_path.empty() || previous_object_path == expected_object_path) {
            set_error(error_message, "");
            return true;
        }

        AccountData account;
        if (!read_account_file(root_directory, account_name, &account, error_message))
            return false;
        if (!validate_account_owned_object_path(account, character_name, error_message))
            return false;

        CharacterLinkReference* link = find_character_link_reference(&account, character_name);
        if (link == nullptr) {
            set_error(error_message, "Character '" + character_name + "' is not linked to account '" + account_name + "'.");
            return false;
        }

        link->object_path = expected_object_path;
        if (!write_account_file(root_directory, account, error_message))
            return false;

        set_error(error_message, "");
        return true;
    }

    bool write_account_object_json_file(const std::string& root_directory, const std::string& account_name, const std::string& character_name, const objects_json::ObjectSaveData& object_data, std::string* error_message)
    {
        std::string final_path;
        std::string previous_object_path;
        if (!prepare_account_object_file_destination(root_directory, account_name, character_name, &final_path, &previous_object_path, error_message))
            return false;

        if (!write_text_file_atomically(final_path, objects_json::serialize_objects_to_json(object_data), error_message))
            return false;

        if (!normalize_account_object_path_after_successful_write(root_directory, account_name, character_name, previous_object_path, error_message))
            return false;

        if (!retire_previous_account_object_path(root_directory, account_name, character_name, previous_object_path, error_message))
            return false;

        set_error(error_message, "");
        return true;
    }

    bool hydrate_account_native_object_file_from_migration(const std::string& root_directory, const std::string& account_name, const std::string& character_name, const CharacterMigrationData& snapshot_data, std::string* error_message)
    {
        if (!snapshot_data.object_file.present)
            return write_default_account_object_file(root_directory, account_name, character_name, error_message);

        std::string object_bytes;
        if (!decode_snapshot_content(snapshot_data.object_file, &object_bytes, error_message))
            return false;

        objects_json::ObjectSaveData object_data;
        if (!objects_json::object_save_data_from_binary(object_bytes, &object_data, error_message))
            return false;

        return write_account_object_json_file(root_directory, account_name, character_name, object_data, error_message);
    }

    bool hydrate_account_native_character_file_from_migration(const std::string& root_directory, const std::string& account_name, const std::string& character_name, const CharacterMigrationData& snapshot_data, std::string* error_message)
    {
        std::string player_text;
        if (!decode_snapshot_content(snapshot_data.player_file, &player_text, error_message))
            return false;

        ensure_player_index_entry(character_name.c_str());

        char normalized_name[MAX_INPUT_LENGTH];
        std::snprintf(normalized_name, sizeof(normalized_name), "%s", character_name.c_str());

        char_file_u stored_character {};
        if (load_char_from_text(normalized_name, player_text.c_str(), &stored_character) < 0) {
            set_error(error_message, "Legacy player data for '" + character_name + "' could not be converted into account-native character storage.");
            return false;
        }

        return write_account_character_file(root_directory, account_name, stored_character, error_message);
    }

    bool write_account_exploits_json_file(const std::string& root_directory, const std::string& account_name, const std::string& character_name, const exploits_json::ExploitHistoryData& exploit_history, std::string* error_message)
    {
        if (!validate_identifier_for_path(account_name, "Account name", error_message))
            return false;
        if (!validate_identifier_for_path(character_name, "Character name", error_message))
            return false;

        AccountData account;
        if (!read_account_file(root_directory, account_name, &account, error_message))
            return false;
        if (!validate_account_owned_exploits_path(account, character_name, error_message))
            return false;

        const std::string account_directory = account_character_directory(root_directory, account_name, character_name);
        if (!create_directory_if_missing(root_directory + "/accounts", error_message))
            return false;
        if (!create_directory_if_missing(root_directory + "/accounts/" + account_bucket_for_name(resolve_account_storage_key(root_directory, account_name)), error_message))
            return false;
        if (!create_directory_if_missing(account_directory, error_message))
            return false;

        return write_text_file_atomically(resolved_exploits_path(account, root_directory, character_name), exploits_json::serialize_exploits_to_json(exploit_history), error_message);
    }

    bool hydrate_account_native_exploit_file_from_migration(const std::string& root_directory, const std::string& account_name, const std::string& character_name, const CharacterMigrationData& snapshot_data, std::string* error_message)
    {
        if (!snapshot_data.exploits_file.present)
            return write_default_account_exploit_file(root_directory, account_name, character_name, error_message);

        std::string exploit_bytes;
        if (!decode_snapshot_content(snapshot_data.exploits_file, &exploit_bytes, error_message))
            return false;

        std::vector<exploit_record> records;
        if (!exploits_json::exploit_records_from_binary(exploit_bytes, &records, error_message))
            return false;

        exploits_json::ExploitHistoryData exploit_history;
        exploit_history.records = std::move(records);
        return write_account_exploits_json_file(root_directory, account_name, character_name, exploit_history, error_message);
    }

    void append_restore_failure(std::string* error_message, const std::string& restore_error)
    {
        if (restore_error.empty())
            return;

        if (error_message == nullptr)
            return;

        if (error_message->empty())
            *error_message = restore_error;
        else
            *error_message += " Restore also failed: " + restore_error;
    }

    void restore_retired_legacy_files(const std::string& player_file_path, const std::string& object_file_path, const std::string& exploits_file_path, const CharacterMigrationData& snapshot_data, bool restore_player, bool restore_object, bool restore_exploits, std::string* error_message)
    {
        if (restore_player) {
            std::string restore_error;
            if (!write_snapshot_bytes(player_file_path, snapshot_data.player_file, true, &restore_error))
                append_restore_failure(error_message, restore_error);
        }

        if (restore_object) {
            std::string restore_error;
            if (!write_snapshot_bytes(object_file_path, snapshot_data.object_file, false, &restore_error))
                append_restore_failure(error_message, restore_error);
        }

        if (restore_exploits) {
            std::string restore_error;
            if (!write_snapshot_bytes(exploits_file_path, snapshot_data.exploits_file, false, &restore_error))
                append_restore_failure(error_message, restore_error);
        }
    }

    bool retire_legacy_character_files_after_migration(const std::string& player_file_path, const std::string& stale_flat_player_file_path, const std::string& object_file_path, const std::string& exploits_file_path, const CharacterMigrationData& snapshot_data, std::string* error_message)
    {
        const bool had_player_file = path_exists(player_file_path);
        const bool had_object_file = path_exists(object_file_path);
        bool retired_player = false;
        bool retired_object = false;

        if (std::remove(player_file_path.c_str()) != 0 && errno != ENOENT) {
            set_error(error_message, "Failed to retire legacy player file '" + player_file_path + "': " + std::strerror(errno));
            return false;
        }
        retired_player = had_player_file;

        if (std::remove(object_file_path.c_str()) != 0 && errno != ENOENT) {
            set_error(error_message, "Failed to retire legacy object file '" + object_file_path + "': " + std::strerror(errno));
            restore_retired_legacy_files(player_file_path, object_file_path, exploits_file_path, snapshot_data, retired_player, false, false, error_message);
            return false;
        }
        retired_object = had_object_file;

        if (std::remove(exploits_file_path.c_str()) != 0 && errno != ENOENT) {
            set_error(error_message, "Failed to retire legacy exploit file '" + exploits_file_path + "': " + std::strerror(errno));
            restore_retired_legacy_files(player_file_path, object_file_path, exploits_file_path, snapshot_data, retired_player, retired_object, false, error_message);
            return false;
        }

        if (!stale_flat_player_file_path.empty() && std::remove(stale_flat_player_file_path.c_str()) != 0 && errno != ENOENT) {
            set_error(error_message, "Failed to retire stale legacy player file '" + stale_flat_player_file_path + "': " + std::strerror(errno));
            restore_retired_legacy_files(player_file_path, object_file_path, exploits_file_path, snapshot_data, retired_player, retired_object, true, error_message);
            return false;
        }

        set_error(error_message, "");
        return true;
    }

    void cleanup_account_native_migration_outputs(const std::string& root_directory, const std::string& account_name, const std::string& character_name)
    {
        std::remove(account_character_player_path(root_directory, account_name, character_name).c_str());
        std::remove(account_character_object_path(root_directory, account_name, character_name).c_str());
        std::remove(account_character_exploits_path(root_directory, account_name, character_name).c_str());
        std::remove(account_character_snapshot_path(root_directory, account_name, character_name).c_str());
    }

    bool migrate_legacy_character_files_internal(const std::string& root_directory, const std::string& account_name, const std::string& character_name, const std::string& player_file_path, const std::string& stale_flat_player_file_path, const std::string& object_file_path, const std::string& exploits_file_path, long migrated_at, CharacterMigrationData* migration, std::string* error_message)
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
        if (!hydrate_account_native_character_file_from_migration(root_directory, account_name, character_name, snapshot_data, error_message))
            return false;
        if (!hydrate_account_native_object_file_from_migration(root_directory, account_name, character_name, snapshot_data, error_message)) {
            cleanup_account_native_migration_outputs(root_directory, account_name, character_name);
            return false;
        }
        if (!hydrate_account_native_exploit_file_from_migration(root_directory, account_name, character_name, snapshot_data, error_message)) {
            cleanup_account_native_migration_outputs(root_directory, account_name, character_name);
            return false;
        }
        if (!write_character_migration_snapshot(root_directory, snapshot_data, migration, error_message)) {
            cleanup_account_native_migration_outputs(root_directory, account_name, character_name);
            return false;
        }

        if (!retire_legacy_character_files_after_migration(player_file_path, stale_flat_player_file_path, object_file_path, exploits_file_path, snapshot_data, error_message)) {
            cleanup_account_native_migration_outputs(root_directory, account_name, character_name);
            return false;
        }

        return true;
    }

} // namespace

bool migrate_legacy_character_by_name(const std::string& root_directory, const std::string& account_name, const std::string& character_name, long migrated_at, CharacterMigrationData* migration, std::string* error_message)
{
    std::string player_file_path;
    if (!resolve_legacy_player_file_path(root_directory, character_name, &player_file_path, error_message))
        return false;

    std::string stale_flat_player_file_path;
    const std::string canonical_player_file_path = legacy_player_file_path(root_directory, character_name);
    if (player_file_path != canonical_player_file_path && path_exists(canonical_player_file_path))
        stale_flat_player_file_path = canonical_player_file_path;

    return migrate_legacy_character_files_internal(root_directory, account_name, character_name,
        player_file_path,
        stale_flat_player_file_path,
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
    bool account_character_exists = false;
    if (!inspect_account_character_file(root_directory, account_name, character_name, &account_character_exists, error_message))
        return false;
    if (account_character_exists)
        return true;

    const std::string path = account_character_snapshot_path(root_directory, account_name, character_name);
    struct stat file_info { };
    if (stat(path.c_str(), &file_info) == 0) {
        CharacterMigrationData loaded_migration;
        CharacterMigrationData* migration_target = migration != nullptr ? migration : &loaded_migration;
        if (read_character_migration(root_directory, account_name, character_name, migration_target, error_message)) {
            set_error(error_message, "Authoritative character.json is missing for '" + character_name + "'; the transitional migration snapshot alone is insufficient.");
            return false;
        }
    } else if (errno != ENOENT) {
        set_error(error_message, "Failed to inspect migration file '" + path + "': " + std::strerror(errno));
        return false;
    }

    return migrate_legacy_character_by_name(root_directory, account_name, character_name, migrated_at, migration, error_message);
}

bool write_account_character_file(const std::string& root_directory, const std::string& account_name, const char_file_u& stored_character, std::string* error_message)
{
    if (!validate_identifier_for_path(account_name, "Account name", error_message))
        return false;
    if (!validate_identifier_for_path(stored_character.name, "Character name", error_message))
        return false;

    AccountData account;
    if (!read_account_file(root_directory, account_name, &account, error_message))
        return false;
    if (!validate_account_owned_character_path(account, stored_character.name, error_message))
        return false;

    const std::string account_directory = account_character_directory(root_directory, account_name, stored_character.name);
    if (!create_directory_if_missing(root_directory + "/accounts", error_message))
        return false;
    if (!create_directory_if_missing(root_directory + "/accounts/" + account_bucket_for_name(resolve_account_storage_key(root_directory, account_name)), error_message))
        return false;
    if (!create_directory_if_missing(account_directory, error_message))
        return false;

    const std::string final_path = resolved_character_path(account, root_directory, stored_character.name);
    const std::string json = character_json::serialize_character_to_json(character_json::character_data_from_store(stored_character));
    return write_text_file_atomically(final_path, json, error_message);
}

bool write_linked_character_file(const std::string& root_directory, const std::string& character_name, const char_file_u& stored_character, std::string* error_message)
{
    std::string owner_account_name;
    if (!find_linked_character_owner_account(root_directory, character_name, &owner_account_name, error_message))
        return false;
    if (owner_account_name.empty()) {
        set_error(error_message, "");
        return true;
    }

    return write_account_character_file(root_directory, owner_account_name, stored_character, error_message);
}

bool read_account_character_file(const std::string& root_directory, const std::string& account_name, const std::string& character_name, char_file_u* stored_character, std::string* error_message)
{
    if (stored_character == nullptr) {
        set_error(error_message, "Stored character output parameter must not be null.");
        return false;
    }

    AccountData account;
    if (!read_account_file(root_directory, account_name, &account, error_message))
        return false;
    if (!validate_account_owned_character_path(account, character_name, error_message))
        return false;

    const std::string path = resolved_character_path(account, root_directory, character_name);
    std::string json;
    if (!read_text_file(path, &json, error_message))
        return false;

    character_json::CharacterData character;
    if (!character_json::deserialize_character_from_json(json, &character, error_message))
        return false;
    if (!character_json::apply_character_data_to_store(character, stored_character, error_message))
        return false;

    set_error(error_message, "");
    return true;
}

bool account_character_file_exists(const std::string& root_directory, const std::string& account_name, const std::string& character_name, std::string* error_message)
{
    bool exists = false;
    if (!inspect_account_character_file(root_directory, account_name, character_name, &exists, error_message))
        return false;
    return exists;
}

bool inspect_account_character_file(const std::string& root_directory, const std::string& account_name, const std::string& character_name, bool* exists, std::string* error_message)
{
    AccountData account;
    if (!read_account_file(root_directory, account_name, &account, error_message))
        return false;
    if (!validate_account_owned_character_path(account, character_name, error_message))
        return false;

    return inspect_path_existence(resolved_character_path(account, root_directory, character_name), "account character file", exists, error_message);
}

bool remove_account_character_file(const std::string& root_directory, const std::string& account_name, const std::string& character_name, std::string* error_message)
{
    AccountData account;
    if (!read_account_file(root_directory, account_name, &account, error_message))
        return false;
    if (!validate_account_owned_character_path(account, character_name, error_message))
        return false;

    const std::string path = resolved_character_path(account, root_directory, character_name);
    if (std::remove(path.c_str()) != 0 && errno != ENOENT) {
        set_error(error_message, "Failed to remove account character file '" + path + "': " + std::strerror(errno));
        return false;
    }

    set_error(error_message, "");
    return true;
}

bool write_account_object_file(const std::string& root_directory, const std::string& account_name, const std::string& character_name, const std::string& object_bytes, std::string* error_message)
{
    objects_json::ObjectSaveData object_data;
    if (!objects_json::object_save_data_from_binary(object_bytes, &object_data, error_message))
        return false;

    return write_account_object_json_file(root_directory, account_name, character_name, object_data, error_message);
}

bool write_default_account_object_file(const std::string& root_directory, const std::string& account_name, const std::string& character_name, std::string* error_message)
{
    const objects_json::ObjectSaveData empty_object_data;
    return write_account_object_json_file(root_directory, account_name, character_name, empty_object_data, error_message);
}

bool write_linked_character_object_file(const std::string& root_directory, const std::string& character_name, const std::string& object_bytes, std::string* error_message)
{
    std::string owner_account_name;
    if (!find_linked_character_owner_account(root_directory, character_name, &owner_account_name, error_message))
        return false;
    if (owner_account_name.empty()) {
        set_error(error_message, "");
        return true;
    }

    return write_account_object_file(root_directory, owner_account_name, character_name, object_bytes, error_message);
}

bool read_account_object_file(const std::string& root_directory, const std::string& account_name, const std::string& character_name, std::string* object_bytes, std::string* error_message)
{
    if (object_bytes == nullptr) {
        set_error(error_message, "Object-bytes output parameter must not be null.");
        return false;
    }

    AccountData account;
    if (!read_account_file(root_directory, account_name, &account, error_message))
        return false;
    if (!validate_account_owned_object_path(account, character_name, error_message))
        return false;

    const std::string path = resolved_object_path(account, root_directory, character_name);
    std::string json;
    if (!read_text_file(path, &json, error_message))
        return false;

    objects_json::ObjectSaveData object_data;
    if (!objects_json::deserialize_objects_from_json(json, &object_data, error_message))
        return false;
    if (!objects_json::object_save_data_to_binary(object_data, object_bytes, error_message))
        return false;

    set_error(error_message, "");
    return true;
}

bool account_object_file_exists(const std::string& root_directory, const std::string& account_name, const std::string& character_name, std::string* error_message)
{
    bool exists = false;
    if (!inspect_account_object_file(root_directory, account_name, character_name, &exists, error_message))
        return false;
    return exists;
}

bool inspect_account_object_file(const std::string& root_directory, const std::string& account_name, const std::string& character_name, bool* exists, std::string* error_message)
{
    AccountData account;
    if (!read_account_file(root_directory, account_name, &account, error_message))
        return false;
    if (!validate_account_owned_object_path(account, character_name, error_message))
        return false;

    return inspect_path_existence(resolved_object_path(account, root_directory, character_name), "account object file", exists, error_message);
}

bool remove_account_object_file(const std::string& root_directory, const std::string& account_name, const std::string& character_name, std::string* error_message)
{
    AccountData account;
    if (!read_account_file(root_directory, account_name, &account, error_message))
        return false;
    if (!validate_account_owned_object_path(account, character_name, error_message))
        return false;

    const std::string path = resolved_object_path(account, root_directory, character_name);
    if (std::remove(path.c_str()) != 0 && errno != ENOENT) {
        set_error(error_message, "Failed to remove account object file '" + path + "': " + std::strerror(errno));
        return false;
    }

    set_error(error_message, "");
    return true;
}

bool write_account_exploit_file(const std::string& root_directory, const std::string& account_name, const std::string& character_name, const std::vector<exploit_record>& records, std::string* error_message)
{
    exploits_json::ExploitHistoryData exploit_history;
    exploit_history.records = records;
    return write_account_exploits_json_file(root_directory, account_name, character_name, exploit_history, error_message);
}

bool write_default_account_exploit_file(const std::string& root_directory, const std::string& account_name, const std::string& character_name, std::string* error_message)
{
    const exploits_json::ExploitHistoryData empty_history;
    return write_account_exploits_json_file(root_directory, account_name, character_name, empty_history, error_message);
}

bool write_linked_character_exploit_file(const std::string& root_directory, const std::string& character_name, const std::vector<exploit_record>& records, std::string* error_message)
{
    std::string owner_account_name;
    if (!find_linked_character_owner_account(root_directory, character_name, &owner_account_name, error_message))
        return false;
    if (owner_account_name.empty()) {
        set_error(error_message, "");
        return true;
    }

    return write_account_exploit_file(root_directory, owner_account_name, character_name, records, error_message);
}

bool read_account_exploit_file(const std::string& root_directory, const std::string& account_name, const std::string& character_name, std::vector<exploit_record>* records, std::string* error_message)
{
    if (records == nullptr) {
        set_error(error_message, "Exploit-record output parameter must not be null.");
        return false;
    }

    AccountData account;
    if (!read_account_file(root_directory, account_name, &account, error_message))
        return false;
    if (!validate_account_owned_exploits_path(account, character_name, error_message))
        return false;

    std::string json;
    if (!read_text_file(resolved_exploits_path(account, root_directory, character_name), &json, error_message))
        return false;

    exploits_json::ExploitHistoryData exploit_history;
    if (!exploits_json::deserialize_exploits_from_json(json, &exploit_history, error_message))
        return false;

    *records = std::move(exploit_history.records);
    set_error(error_message, "");
    return true;
}

bool account_exploit_file_exists(const std::string& root_directory, const std::string& account_name, const std::string& character_name, std::string* error_message)
{
    bool exists = false;
    if (!inspect_account_exploit_file(root_directory, account_name, character_name, &exists, error_message))
        return false;
    return exists;
}

bool inspect_account_exploit_file(const std::string& root_directory, const std::string& account_name, const std::string& character_name, bool* exists, std::string* error_message)
{
    AccountData account;
    if (!read_account_file(root_directory, account_name, &account, error_message))
        return false;
    if (!validate_account_owned_exploits_path(account, character_name, error_message))
        return false;

    return inspect_path_existence(resolved_exploits_path(account, root_directory, character_name), "account exploits file", exists, error_message);
}

bool remove_account_exploit_file(const std::string& root_directory, const std::string& account_name, const std::string& character_name, std::string* error_message)
{
    AccountData account;
    if (!read_account_file(root_directory, account_name, &account, error_message))
        return false;
    if (!validate_account_owned_exploits_path(account, character_name, error_message))
        return false;

    const std::string path = resolved_exploits_path(account, root_directory, character_name);
    if (std::remove(path.c_str()) != 0 && errno != ENOENT) {
        set_error(error_message, "Failed to remove account exploits file '" + path + "': " + std::strerror(errno));
        return false;
    }

    set_error(error_message, "");
    return true;
}

bool clear_account_character_runtime_support_files(const std::string& root_directory, const std::string& character_name, std::string* error_message)
{
    if (!validate_identifier_for_path(character_name, "Character name", error_message))
        return false;

    const std::string object_path = legacy_object_file_path(root_directory, character_name);
    if (std::remove(object_path.c_str()) != 0 && errno != ENOENT) {
        set_error(error_message, "Failed to remove stale legacy file '" + object_path + "': " + std::strerror(errno));
        return false;
    }

    const std::string exploits_path = legacy_exploits_file_path(root_directory, character_name);
    if (std::remove(exploits_path.c_str()) != 0 && errno != ENOENT) {
        set_error(error_message, "Failed to remove stale legacy file '" + exploits_path + "': " + std::strerror(errno));
        return false;
    }

    set_error(error_message, "");
    return true;
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

bool clear_character_runtime_support_files_for_account_play(const std::string& root_directory, const std::string& expected_account_name, const std::string& expected_character_name, const CharacterMigrationData& migration, std::string* error_message)
{
    if (!validate_migration_identity(migration, expected_account_name, expected_character_name, error_message))
        return false;

    return clear_account_character_runtime_support_files(root_directory, migration.character_name, error_message);
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
