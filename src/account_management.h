#ifndef ACCOUNT_MANAGEMENT_H
#define ACCOUNT_MANAGEMENT_H

#include "db.h"
#include "structs.h"

#include <string>
#include <vector>

namespace account {

static constexpr int ACCOUNT_SCHEMA_VERSION = 1;
static constexpr int MIN_PASSWORD_LENGTH = 8;
static constexpr int MIN_ACCOUNT_NAME_LENGTH = 3;
static constexpr int MAX_ACCOUNT_NAME_LENGTH = 20;
static constexpr long EMAIL_VERIFICATION_WINDOW_SECONDS = 15 * 60;
static constexpr long EMAIL_VERIFICATION_RESEND_COOLDOWN_SECONDS = 60;
static constexpr int MAX_EMAIL_VERIFICATION_ATTEMPTS = 5;

struct AccountData {
    struct CharacterLinkReference {
        std::string character_name;
        std::string character_path;
        std::string object_path;
        std::string exploits_path;
    };

    int version = ACCOUNT_SCHEMA_VERSION;
    std::string account_name;
    std::string normalized_email;
    std::string password_hash;
    std::string password_salt;
    std::vector<std::string> characters;
    std::vector<CharacterLinkReference> character_links;
    bool email_verified = false;
    std::string email_verified_by;
    long email_verified_at = 0;
    std::string verification_code_hash;
    long verification_code_sent_at = 0;
    long verification_code_expires_at = 0;
    int verification_attempt_count = 0;
    long verification_last_attempt_at = 0;
    bool blocked = false;
    std::string block_reason;
    std::string blocked_by;
    long blocked_at = 0;
    long created_at = 0;
    long updated_at = 0;
    long password_reset_at = 0;
    std::string password_reset_by;
};

struct LegacyAssetSnapshot {
    std::string source_path;
    std::string encoding;
    std::string content;
    bool present = false;
};

struct CharacterMigrationData {
    int version = ACCOUNT_SCHEMA_VERSION;
    std::string account_name;
    std::string character_name;
    long migrated_at = 0;
    LegacyAssetSnapshot player_file;
    LegacyAssetSnapshot object_file;
    LegacyAssetSnapshot exploits_file;
};

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
bool admin_link_character(const std::string& root_directory, const std::string& account_name, const std::string& character_name, long updated_at, AccountData* account, std::string* error_message = nullptr);
bool admin_link_and_migrate_character(const std::string& root_directory, const std::string& account_name, const std::string& character_name, long updated_at, AccountData* account, CharacterMigrationData* migration, std::string* error_message = nullptr);
bool admin_verify_email(const std::string& root_directory, const std::string& account_name, const std::string& verified_by, long verified_at, AccountData* account, std::string* error_message = nullptr);
bool admin_unverify_email(const std::string& root_directory, const std::string& account_name, long updated_at, AccountData* account, std::string* error_message = nullptr);
bool admin_block_account(const std::string& root_directory, const std::string& account_name, const std::string& blocked_by, const std::string& block_reason, long blocked_at, AccountData* account, std::string* error_message = nullptr);
bool admin_unblock_account(const std::string& root_directory, const std::string& account_name, long updated_at, AccountData* account, std::string* error_message = nullptr);
bool admin_reset_password(const std::string& root_directory, const std::string& account_name, const std::string& new_password, const std::string& reset_by, long reset_at, AccountData* account, std::string* error_message = nullptr);
bool admin_delete_linked_character(const std::string& root_directory, const std::string& account_name, const std::string& character_name, long updated_at, AccountData* account, std::string* error_message = nullptr);
bool link_and_migrate_character(const std::string& root_directory, const std::string& account_name, const std::string& password, const std::string& character_name, long updated_at, AccountData* account, CharacterMigrationData* migration, std::string* error_message = nullptr);

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

std::string serialize_character_migration_to_json(const CharacterMigrationData& migration);
bool deserialize_character_migration_from_json(const std::string& json, CharacterMigrationData* migration, std::string* error_message = nullptr);
bool migrate_legacy_character_by_name(const std::string& root_directory, const std::string& account_name, const std::string& character_name, long migrated_at, CharacterMigrationData* migration, std::string* error_message = nullptr);
bool read_character_migration(const std::string& root_directory, const std::string& account_name, const std::string& character_name, CharacterMigrationData* migration, std::string* error_message = nullptr);
bool ensure_character_migration(const std::string& root_directory, const std::string& account_name, const std::string& character_name, long migrated_at, CharacterMigrationData* migration, std::string* error_message = nullptr);
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
bool restore_character_migration(const std::string& root_directory, const std::string& expected_account_name, const std::string& expected_character_name, const CharacterMigrationData& migration, std::string* error_message = nullptr);
bool clear_character_runtime_support_files_for_account_play(const std::string& root_directory, const std::string& expected_account_name, const std::string& expected_character_name, const CharacterMigrationData& migration, std::string* error_message = nullptr);
bool refresh_linked_character_snapshot(const std::string& root_directory, const std::string& character_name, long migrated_at, CharacterMigrationData* migration, std::string* error_message = nullptr);

std::string format_character_name_for_display(const std::string& character_name);
std::string format_account_character_prompt(const std::string& root_directory, const AccountData& account);
std::string format_account_character_list(const std::string& root_directory, const AccountData& account);
std::string format_account_summary(const AccountData& account);

std::string serialize_account_to_json(const AccountData& account);
bool deserialize_account_from_json(const std::string& json, AccountData* account, std::string* error_message = nullptr);

bool write_account_file(const std::string& root_directory, const AccountData& account, std::string* error_message = nullptr);
bool read_account_file(const std::string& root_directory, const std::string& account_name, AccountData* account, std::string* error_message = nullptr);
bool read_account_file_by_email(const std::string& root_directory, const std::string& email, AccountData* account, std::string* error_message = nullptr);
bool read_account_file_by_identifier(const std::string& root_directory, const std::string& identifier, AccountData* account, std::string* error_message = nullptr);

} // namespace account

#endif
