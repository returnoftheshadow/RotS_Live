#ifndef ACCOUNT_MANAGEMENT_TYPES_H
#define ACCOUNT_MANAGEMENT_TYPES_H

#include "db.h"
#include "structs.h"

#include <string>
#include <vector>

namespace account {

// Declared here (rather than in account_management.h, which pulls in identity/presentation
// headers before defining its own types) because select_linked_character (identity.h) and
// format_account_character_prompt (presentation.h) both need these types in their signatures.
enum class RosterSort {
    Account, // insertion order: the pre-feature behaviour, and the default for accounts that never chose
    Name,
    Level,
    Race,
    Side,
};

enum class RosterFilter {
    None,
    Warrior,
    Ranger,
    Mystic,
    Mage,
};

static constexpr int ACCOUNT_SCHEMA_VERSION = 1;
static constexpr int MIN_PASSWORD_LENGTH = 8;
static constexpr int MIN_ACCOUNT_NAME_LENGTH = 3;
static constexpr int MAX_ACCOUNT_NAME_LENGTH = 20;
static constexpr long EMAIL_VERIFICATION_WINDOW_SECONDS = 15 * 60;
static constexpr long EMAIL_VERIFICATION_RESEND_COOLDOWN_SECONDS = 60;
static constexpr int MAX_EMAIL_VERIFICATION_ATTEMPTS = 5;
// Matches the descriptor host field width in structs.h, so a recorded host can never be
// longer than the value the connection itself carried.
static constexpr int MAX_FAILED_LOGIN_HOST_LENGTH = 49;
// Deliberately defined in terms of the verification constants rather than repeating the literals:
// five password attempts, five verification attempts, five reset-code attempts.
static constexpr long PASSWORD_RESET_WINDOW_SECONDS = EMAIL_VERIFICATION_WINDOW_SECONDS;
static constexpr long PASSWORD_RESET_RESEND_COOLDOWN_SECONDS = EMAIL_VERIFICATION_RESEND_COOLDOWN_SECONDS;
static constexpr int MAX_PASSWORD_RESET_ATTEMPTS = MAX_EMAIL_VERIFICATION_ATTEMPTS;

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
    // Empty means "never chosen": the roster keeps insertion order, matching pre-feature behaviour.
    // One of "", "name", "level", "race", "side".
    std::string roster_sort;
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
    int failed_login_count = 0;
    long failed_login_last_at = 0;
    std::string failed_login_last_host;
    std::string password_reset_code_hash;
    long password_reset_code_sent_at = 0;
    long password_reset_code_expires_at = 0;
    int password_reset_attempt_count = 0;
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
