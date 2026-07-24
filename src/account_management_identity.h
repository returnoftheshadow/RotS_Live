#ifndef ACCOUNT_MANAGEMENT_IDENTITY_H
#define ACCOUNT_MANAGEMENT_IDENTITY_H

#include "account_management_types.h"

namespace account {

std::string normalize_account_name(const std::string& account_name);
std::string normalize_email(const std::string& email);

bool is_valid_account_name(const std::string& account_name, std::string* error_message = nullptr);
bool is_valid_email(const std::string& email, std::string* error_message = nullptr);
bool is_valid_password(const std::string& password, std::string* error_message = nullptr);

bool generate_password_credentials(const std::string& password, std::string* password_hash, std::string* password_salt, std::string* error_message = nullptr);
bool verify_password(const std::string& password, const std::string& password_hash);
bool initialize_new_account(const std::string& account_name, const std::string& email, const std::string& password, long created_at, AccountData* account, std::string* error_message = nullptr);
bool add_character_to_account(AccountData* account, const std::string& character_name, std::string* error_message = nullptr);
bool account_has_character(const AccountData& account, const std::string& character_name);
bool select_linked_character(const AccountData& account, const std::string& character_name, std::string* normalized_character_name, std::string* error_message = nullptr);
bool prepare_email_verification_code(AccountData* account, long sent_at, std::string* verification_code, std::string* error_message = nullptr);
bool confirm_email_verification_code(AccountData* account, const std::string& verification_code, const std::string& verified_by, long verified_at, std::string* error_message = nullptr);
void verify_email(AccountData* account, const std::string& verified_by, long verified_at);
void unverify_email(AccountData* account);
void block_account(AccountData* account, const std::string& blocked_by, const std::string& block_reason, long blocked_at);
void unblock_account(AccountData* account);
bool reset_account_password(AccountData* account, const std::string& new_password, const std::string& reset_by, long reset_at, std::string* error_message = nullptr);

bool create_account(const std::string& root_directory, const std::string& account_name, const std::string& email, const std::string& password, long created_at, AccountData* account, std::string* error_message = nullptr);
bool create_account_for_email(const std::string& root_directory, const std::string& email, const std::string& password, long created_at, AccountData* account, std::string* error_message = nullptr);
bool authenticate_account(const std::string& root_directory, const std::string& account_name, const std::string& password, AccountData* account, std::string* error_message = nullptr);
bool authenticate_account_by_email(const std::string& root_directory, const std::string& email, const std::string& password, AccountData* account, std::string* error_message = nullptr);
bool start_email_verification(const std::string& root_directory, const std::string& account_name, long sent_at, AccountData* account, std::string* error_message = nullptr);
bool complete_email_verification(const std::string& root_directory, const std::string& account_name, const std::string& verification_code, const std::string& verified_by, long verified_at, AccountData* account, std::string* error_message = nullptr);
bool find_linked_character_owner_account(const std::string& root_directory, const std::string& character_name, std::string* owner_account_name, std::string* error_message = nullptr);
// Uncached owner resolution (the real scan). find_linked_character_owner_account delegates here when
// the cache is disabled, and it is the owner cache's backing resolver on a miss.
bool find_linked_character_owner_account_uncached(const std::string& root_directory, const std::string& character_name, std::string* owner_account_name, std::string* error_message = nullptr);
bool admin_link_character(const std::string& root_directory, const std::string& account_name, const std::string& character_name, long updated_at, AccountData* account, std::string* error_message = nullptr);
bool admin_link_and_migrate_character(const std::string& root_directory, const std::string& account_name, const std::string& character_name, long updated_at, AccountData* account, CharacterMigrationData* migration, std::string* error_message = nullptr);
bool admin_verify_email(const std::string& root_directory, const std::string& account_name, const std::string& verified_by, long verified_at, AccountData* account, std::string* error_message = nullptr);
bool admin_unverify_email(const std::string& root_directory, const std::string& account_name, long updated_at, AccountData* account, std::string* error_message = nullptr);
bool admin_block_account(const std::string& root_directory, const std::string& account_name, const std::string& blocked_by, const std::string& block_reason, long blocked_at, AccountData* account, std::string* error_message = nullptr);
bool admin_unblock_account(const std::string& root_directory, const std::string& account_name, long updated_at, AccountData* account, std::string* error_message = nullptr);
bool admin_reset_password(const std::string& root_directory, const std::string& account_name, const std::string& new_password, const std::string& reset_by, long reset_at, AccountData* account, std::string* error_message = nullptr);
bool admin_delete_linked_character(const std::string& root_directory, const std::string& account_name, const std::string& character_name, long updated_at, AccountData* account, std::string* error_message = nullptr);
bool link_and_migrate_character(const std::string& root_directory, const std::string& account_name, const std::string& password, const std::string& character_name, long updated_at, AccountData* account, CharacterMigrationData* migration, std::string* error_message = nullptr);

} // namespace account

#endif
