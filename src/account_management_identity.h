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
// Note a rejected account-login password on the stored account so the owner can be told about it
// the next time they get in. Unknown or malformed addresses are a silent no-op (returning true and
// writing nothing), so a failed attempt never reveals whether an address has an account behind it.
bool record_account_login_failure(const std::string& root_directory, const std::string& email, const std::string& host, long attempted_at, std::string* error_message = nullptr);
// Clear the failure tally after a successful login. Writes nothing when there is nothing to clear.
bool clear_account_login_failures(const std::string& root_directory, const std::string& account_name, std::string* error_message = nullptr);
// Begin a forgot-password reset for the account at this address. Malformed addresses, addresses
// with no account, and repeat requests inside the resend cooldown all return true having written
// and mailed nothing -- a failed request must never reveal whether an account exists.
// *code_expires_at is always sent_at + PASSWORD_RESET_WINDOW_SECONDS: a synthetic value derived
// only from the caller's clock, never from stored state. The caller stamps a visible connection
// deadline from it, so anything account-dependent here would leak through how long the connection
// lived. Inside the cooldown the pending code may therefore expire slightly before the deadline.
// The return value says whether the mail was actually sent; it distinguishes a send failure from
// success and nothing else, and must never change anything the player can observe.
bool start_password_reset(const std::string& root_directory, const std::string& email, long sent_at, long* code_expires_at, std::string* error_message = nullptr);
// Check a reset code WITHOUT consuming it, so the player learns a code is wrong at the code prompt
// rather than after typing a new password twice. A wrong code costs an attempt exactly as the
// completing call would; a correct one changes nothing at all.
bool verify_password_reset_code(const std::string& root_directory, const std::string& email, const std::string& reset_code, long attempted_at, std::string* error_message = nullptr);

// Finish a forgot-password reset. Fails for an unknown address, a missing or expired code, a wrong
// code, or a password the policy rejects. The fifth wrong code clears the stored code so a
// reconnect cannot resume against it. On success the reset state is cleared, the address is marked
// verified (mailbox control was just proven) and the failed-login tally is reset.
bool complete_password_reset(const std::string& root_directory, const std::string& email, const std::string& reset_code, const std::string& new_password, long reset_at, AccountData* account, std::string* error_message = nullptr);
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
