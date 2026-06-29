#ifndef ACCOUNT_MANAGEMENT_STORAGE_H
#define ACCOUNT_MANAGEMENT_STORAGE_H

#include "account_management_types.h"

namespace account {

std::string account_bucket_for_name(const std::string& name);
std::string legacy_player_file_path(const std::string& root_directory, const std::string& character_name);
std::string legacy_object_file_path(const std::string& root_directory, const std::string& character_name);
std::string legacy_exploits_file_path(const std::string& root_directory, const std::string& character_name);
std::string account_file_path(const std::string& root_directory, const std::string& account_name);
std::string account_character_directory(const std::string& root_directory, const std::string& account_name, const std::string& character_name);
std::string account_character_snapshot_path(const std::string& root_directory, const std::string& account_name, const std::string& character_name);
std::string account_character_player_path(const std::string& root_directory, const std::string& account_name, const std::string& character_name);
std::string account_character_object_path(const std::string& root_directory, const std::string& account_name, const std::string& character_name);
std::string account_character_exploits_path(const std::string& root_directory, const std::string& account_name, const std::string& character_name);

std::string serialize_account_to_json(const AccountData& account);
bool deserialize_account_from_json(const std::string& json, AccountData* account, std::string* error_message = nullptr);

bool write_account_file(const std::string& root_directory, const AccountData& account, std::string* error_message = nullptr);
bool read_account_file(const std::string& root_directory, const std::string& account_name, AccountData* account, std::string* error_message = nullptr);
bool read_account_file_by_email(const std::string& root_directory, const std::string& email, AccountData* account, std::string* error_message = nullptr);
bool read_account_file_by_identifier(const std::string& root_directory, const std::string& identifier, AccountData* account, std::string* error_message = nullptr);

std::string serialize_character_migration_to_json(const CharacterMigrationData& migration);
bool deserialize_character_migration_from_json(const std::string& json, CharacterMigrationData* migration, std::string* error_message = nullptr);

// Read an entire text file into *contents (POSIX-backed). Exposed for stage-timing the
// LOAD pipeline's file-read step.
bool read_text_file(const std::string& path, std::string* contents, std::string* error_message);

// Atomic write: temp(path+".tmp") -> fwrite -> rename. Exposed for stage-timing the SAVE
// pipeline's disk-write step against a throwaway path.
bool write_text_file_atomically(const std::string& path, const std::string& text,
                                std::string* error_message);

} // namespace account

#endif
