#ifndef ACCOUNT_MANAGEMENT_TYPES_H
#define ACCOUNT_MANAGEMENT_TYPES_H

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

} // namespace account

#endif
