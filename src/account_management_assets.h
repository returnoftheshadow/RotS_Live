#ifndef ACCOUNT_MANAGEMENT_ASSETS_H
#define ACCOUNT_MANAGEMENT_ASSETS_H

#include "account_management_types.h"

namespace account {

bool write_account_character_file(const std::string& root_directory, const std::string& account_name, const char_file_u& stored_character, std::string* error_message = nullptr);
bool write_linked_character_file(const std::string& root_directory, const std::string& character_name, const char_file_u& stored_character, std::string* error_message = nullptr);
bool read_account_character_file(const std::string& root_directory, const std::string& account_name, const std::string& character_name, char_file_u* stored_character, std::string* error_message = nullptr);
bool inspect_account_character_file(const std::string& root_directory, const std::string& account_name, const std::string& character_name, bool* exists, std::string* error_message = nullptr);
bool account_character_file_exists(const std::string& root_directory, const std::string& account_name, const std::string& character_name, std::string* error_message = nullptr);
bool remove_account_character_file(const std::string& root_directory, const std::string& account_name, const std::string& character_name, std::string* error_message = nullptr);

bool write_account_object_file(const std::string& root_directory, const std::string& account_name, const std::string& character_name, const std::string& object_bytes, std::string* error_message = nullptr);
bool write_default_account_object_file(const std::string& root_directory, const std::string& account_name, const std::string& character_name, std::string* error_message = nullptr);
bool write_linked_character_object_file(const std::string& root_directory, const std::string& character_name, const std::string& object_bytes, std::string* error_message = nullptr);
bool read_account_object_file(const std::string& root_directory, const std::string& account_name, const std::string& character_name, std::string* object_bytes, std::string* error_message = nullptr);
bool inspect_account_object_file(const std::string& root_directory, const std::string& account_name, const std::string& character_name, bool* exists, std::string* error_message = nullptr);
bool account_object_file_exists(const std::string& root_directory, const std::string& account_name, const std::string& character_name, std::string* error_message = nullptr);
bool remove_account_object_file(const std::string& root_directory, const std::string& account_name, const std::string& character_name, std::string* error_message = nullptr);

bool write_account_exploit_file(const std::string& root_directory, const std::string& account_name, const std::string& character_name, const std::vector<exploit_record>& records, std::string* error_message = nullptr);
bool write_default_account_exploit_file(const std::string& root_directory, const std::string& account_name, const std::string& character_name, std::string* error_message = nullptr);
bool write_linked_character_exploit_file(const std::string& root_directory, const std::string& character_name, const std::vector<exploit_record>& records, std::string* error_message = nullptr);
bool read_account_exploit_file(const std::string& root_directory, const std::string& account_name, const std::string& character_name, std::vector<exploit_record>* records, std::string* error_message = nullptr);
bool inspect_account_exploit_file(const std::string& root_directory, const std::string& account_name, const std::string& character_name, bool* exists, std::string* error_message = nullptr);
bool account_exploit_file_exists(const std::string& root_directory, const std::string& account_name, const std::string& character_name, std::string* error_message = nullptr);
bool remove_account_exploit_file(const std::string& root_directory, const std::string& account_name, const std::string& character_name, std::string* error_message = nullptr);

bool clear_account_character_runtime_support_files(const std::string& root_directory, const std::string& character_name, std::string* error_message = nullptr);
bool decode_snapshot_content(const LegacyAssetSnapshot& snapshot, std::string* contents, std::string* error_message = nullptr);

} // namespace account

#endif
