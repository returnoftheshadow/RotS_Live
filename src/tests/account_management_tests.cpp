#include "../account_management.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace {

class TemporaryDirectory {
public:
    TemporaryDirectory()
    {
        char directory_template[] = "/tmp/rots-account-tests-XXXXXX";
        char* created_path = mkdtemp(directory_template);
        EXPECT_NE(created_path, nullptr) << "Expected mkdtemp to create a temporary directory for account-management tests.";
        if (created_path)
            m_path = created_path;
    }

    ~TemporaryDirectory()
    {
        if (!m_path.empty())
            remove_tree(m_path);
    }

    const std::string& path() const
    {
        return m_path;
    }

private:
    static void remove_tree(const std::string& path)
    {
        DIR* directory = opendir(path.c_str());
        if (directory == nullptr) {
            std::remove(path.c_str());
            return;
        }

        while (dirent* entry = readdir(directory)) {
            if (std::strcmp(entry->d_name, ".") == 0 || std::strcmp(entry->d_name, "..") == 0)
                continue;

            const std::string child_path = path + "/" + entry->d_name;
            struct stat file_info {};
            if (stat(child_path.c_str(), &file_info) != 0)
                continue;

            if (S_ISDIR(file_info.st_mode))
                remove_tree(child_path);
            else
                std::remove(child_path.c_str());
        }

        closedir(directory);
        rmdir(path.c_str());
    }

    std::string m_path;
};

account::AccountData make_account()
{
    account::AccountData data;
    data.account_name = "alpha-admin";
    data.normalized_email = "player@example.com";
    data.password_hash = "hash-value";
    data.password_salt = "salt-value";
    data.characters = { "Aragorn", "Legolas" };
    data.email_verified = true;
    data.email_verified_by = "VerifierAdmin";
    data.email_verified_at = 1695000000;
    data.verification_code_hash = "pending-code-hash";
    data.verification_code_sent_at = 1694999500;
    data.verification_code_expires_at = 1695000400;
    data.verification_attempt_count = 2;
    data.verification_last_attempt_at = 1694999600;
    data.blocked = true;
    data.block_reason = "Testing block reason";
    data.blocked_by = "AdminUser";
    data.blocked_at = 1700000000;
    data.created_at = 1690000000;
    data.updated_at = 1700000001;
    data.password_reset_at = 1700000002;
    data.password_reset_by = "SecurityAdmin";
    return data;
}

void write_text_file(const std::string& path, const std::string& contents)
{
    FILE* file = std::fopen(path.c_str(), "wb");
    ASSERT_NE(file, nullptr) << "Expected test helper to create fixture file: " << path;
    ASSERT_EQ(std::fwrite(contents.data(), sizeof(char), contents.size(), file), contents.size());
    ASSERT_EQ(std::fclose(file), 0);
}

std::string read_file_contents(const std::string& path)
{
    FILE* file = std::fopen(path.c_str(), "rb");
    EXPECT_NE(file, nullptr) << "Expected test helper to open fixture file: " << path;
    if (file == nullptr)
        return "";

    std::string contents;
    char buffer[256];
    while (true) {
        const size_t bytes_read = std::fread(buffer, sizeof(char), sizeof(buffer), file);
        if (bytes_read > 0)
            contents.append(buffer, bytes_read);

        if (bytes_read < sizeof(buffer)) {
            EXPECT_EQ(std::ferror(file), 0) << "Expected test helper to read fixture file cleanly: " << path;
            break;
        }
    }

    EXPECT_EQ(std::fclose(file), 0);
    return contents;
}

} // namespace

TEST(AccountManagement, NormalizesEmailByTrimmingAndLowercasing) {
    EXPECT_EQ(account::normalize_email("  Player@Example.COM "), "player@example.com");
}

TEST(AccountManagement, AcceptsReasonableEmailAddresses) {
    std::string error_message;

    EXPECT_TRUE(account::is_valid_email("player@example.com", &error_message)) << error_message;
    EXPECT_TRUE(account::is_valid_email("player.one+alts@example-domain.com", &error_message)) << error_message;
}

TEST(AccountManagement, RejectsMalformedEmailAddresses) {
    std::string error_message;

    EXPECT_FALSE(account::is_valid_email("not-an-email", &error_message));
    EXPECT_NE(error_message.find("@"), std::string::npos);

    EXPECT_FALSE(account::is_valid_email("player@example.com\nbcc@example.com", &error_message));
    EXPECT_NE(error_message.find("whitespace"), std::string::npos);

    EXPECT_FALSE(account::is_valid_email("player@example.com,other@example.com", &error_message));
    EXPECT_NE(error_message.find("recipient"), std::string::npos);
}

TEST(AccountManagement, AcceptsConservativeAccountNames) {
    std::string error_message;

    EXPECT_TRUE(account::is_valid_account_name("Alpha-Admin_7", &error_message)) << error_message;
    EXPECT_TRUE(error_message.empty());
}

TEST(AccountManagement, RejectsAccountNamesWithUnsupportedCharacters) {
    std::string error_message;

    EXPECT_FALSE(account::is_valid_account_name("Alpha Admin!", &error_message));
    EXPECT_NE(error_message.find("letters"), std::string::npos)
        << "Expected invalid account-name failures to explain the supported character set.";
}

TEST(AccountManagement, RejectsPasswordsMissingRequiredComplexity) {
    std::string error_message;

    EXPECT_FALSE(account::is_valid_password("alllowercase1", &error_message));
    EXPECT_NE(error_message.find("uppercase"), std::string::npos);

    EXPECT_FALSE(account::is_valid_password("ALLUPPERCASE1", &error_message));
    EXPECT_NE(error_message.find("lowercase"), std::string::npos);

    EXPECT_FALSE(account::is_valid_password("NoDigitsHere", &error_message));
    EXPECT_NE(error_message.find("number"), std::string::npos);
}

TEST(AccountManagement, AcceptsPasswordsMeetingConfiguredPolicy) {
    std::string error_message;

    EXPECT_TRUE(account::is_valid_password("ValidPass1", &error_message)) << error_message;
    EXPECT_TRUE(error_message.empty());
}

TEST(AccountManagement, GeneratesOneWayPasswordCredentialsThatVerifySuccessfully) {
    std::string password_hash;
    std::string password_salt;
    std::string error_message;

    ASSERT_TRUE(account::generate_password_credentials("ValidPass1", &password_hash, &password_salt, &error_message)) << error_message;

    EXPECT_FALSE(password_hash.empty());
    EXPECT_FALSE(password_salt.empty());
    EXPECT_NE(password_hash.find(password_salt), std::string::npos)
        << "Expected the stored password hash to embed the generated salt specification.";
    EXPECT_TRUE(account::verify_password("ValidPass1", password_hash));
    EXPECT_FALSE(account::verify_password("WrongPass1", password_hash));
}

TEST(AccountManagement, InitializesNewAccountsWithNormalizedIdentityAndPasswordHash) {
    account::AccountData new_account;
    std::string error_message;

    ASSERT_TRUE(account::initialize_new_account(" Alpha-Admin ", " Player@Example.COM ", "ValidPass1", 1700001111, &new_account, &error_message)) << error_message;

    EXPECT_EQ(new_account.account_name, "alpha-admin");
    EXPECT_EQ(new_account.normalized_email, "player@example.com");
    EXPECT_FALSE(new_account.password_hash.empty());
    EXPECT_FALSE(new_account.password_salt.empty());
    EXPECT_EQ(new_account.created_at, 1700001111);
    EXPECT_EQ(new_account.updated_at, 1700001111);
    EXPECT_FALSE(new_account.email_verified);
    EXPECT_FALSE(new_account.blocked);
    EXPECT_TRUE(account::verify_password("ValidPass1", new_account.password_hash));
}

TEST(AccountManagement, VerifiesAndUnverifiesAccountsWithAuditMetadata) {
    account::AccountData account_data;
    std::string error_message;
    ASSERT_TRUE(account::initialize_new_account("alpha-admin", "player@example.com", "ValidPass1", 1700001000, &account_data, &error_message)) << error_message;

    account::verify_email(&account_data, "VerifierAdmin", 1700002221);
    EXPECT_TRUE(account_data.email_verified);
    EXPECT_EQ(account_data.email_verified_by, "VerifierAdmin");
    EXPECT_EQ(account_data.email_verified_at, 1700002221);
    EXPECT_EQ(account_data.updated_at, 1700002221);

    account::unverify_email(&account_data);
    EXPECT_FALSE(account_data.email_verified);
    EXPECT_TRUE(account_data.email_verified_by.empty());
    EXPECT_EQ(account_data.email_verified_at, 0);
}

TEST(AccountManagement, PreparesEmailVerificationCodesWithFifteenMinuteExpiry) {
    account::AccountData account_data;
    std::string error_message;
    ASSERT_TRUE(account::initialize_new_account("alpha-admin", "player@example.com", "ValidPass1", 1700001000, &account_data, &error_message)) << error_message;

    std::string verification_code;
    ASSERT_TRUE(account::prepare_email_verification_code(&account_data, 1700002000, &verification_code, &error_message)) << error_message;

    EXPECT_EQ(verification_code.size(), 6u);
    EXPECT_FALSE(account_data.verification_code_hash.empty());
    EXPECT_EQ(account_data.verification_code_sent_at, 1700002000);
    EXPECT_EQ(account_data.verification_code_expires_at, 1700002000 + account::EMAIL_VERIFICATION_WINDOW_SECONDS);
}

TEST(AccountManagement, ConfirmsMatchingEmailVerificationCodesAndClearsPendingState) {
    account::AccountData account_data;
    std::string error_message;
    ASSERT_TRUE(account::initialize_new_account("alpha-admin", "player@example.com", "ValidPass1", 1700001000, &account_data, &error_message)) << error_message;

    std::string verification_code;
    ASSERT_TRUE(account::prepare_email_verification_code(&account_data, 1700002000, &verification_code, &error_message)) << error_message;
    ASSERT_TRUE(account::confirm_email_verification_code(&account_data, verification_code, "email-code", 1700002100, &error_message)) << error_message;

    EXPECT_TRUE(account_data.email_verified);
    EXPECT_EQ(account_data.email_verified_by, "email-code");
    EXPECT_EQ(account_data.email_verified_at, 1700002100);
    EXPECT_TRUE(account_data.verification_code_hash.empty());
    EXPECT_EQ(account_data.verification_code_sent_at, 0);
    EXPECT_EQ(account_data.verification_code_expires_at, 0);
}

TEST(AccountManagement, RejectsInvalidEmailVerificationCodes) {
    account::AccountData account_data;
    std::string error_message;
    ASSERT_TRUE(account::initialize_new_account("alpha-admin", "player@example.com", "ValidPass1", 1700001000, &account_data, &error_message)) << error_message;

    std::string verification_code;
    ASSERT_TRUE(account::prepare_email_verification_code(&account_data, 1700002000, &verification_code, &error_message)) << error_message;

    EXPECT_FALSE(account::confirm_email_verification_code(&account_data, "000000", "email-code", 1700002100, &error_message));
    EXPECT_NE(error_message.find("invalid"), std::string::npos);
    EXPECT_EQ(account_data.verification_attempt_count, 1);
    EXPECT_EQ(account_data.verification_last_attempt_at, 1700002100);
}

TEST(AccountManagement, RejectsExpiredEmailVerificationCodes) {
    account::AccountData account_data;
    std::string error_message;
    ASSERT_TRUE(account::initialize_new_account("alpha-admin", "player@example.com", "ValidPass1", 1700001000, &account_data, &error_message)) << error_message;

    std::string verification_code;
    ASSERT_TRUE(account::prepare_email_verification_code(&account_data, 1700002000, &verification_code, &error_message)) << error_message;

    EXPECT_FALSE(account::confirm_email_verification_code(&account_data, verification_code, "email-code", 1700002000 + account::EMAIL_VERIFICATION_WINDOW_SECONDS + 1, &error_message));
    EXPECT_NE(error_message.find("expired"), std::string::npos);
}

TEST(AccountManagement, InvalidatesVerificationCodeAfterTooManyInvalidAttempts) {
    account::AccountData account_data;
    std::string error_message;
    ASSERT_TRUE(account::initialize_new_account("alpha-admin", "player@example.com", "ValidPass1", 1700001000, &account_data, &error_message)) << error_message;

    std::string verification_code;
    ASSERT_TRUE(account::prepare_email_verification_code(&account_data, 1700002000, &verification_code, &error_message)) << error_message;

    for (int attempt = 1; attempt < account::MAX_EMAIL_VERIFICATION_ATTEMPTS; ++attempt) {
        EXPECT_FALSE(account::confirm_email_verification_code(&account_data, "000000", "email-code", 1700002100 + attempt, &error_message));
        EXPECT_NE(error_message.find("invalid"), std::string::npos);
    }

    EXPECT_FALSE(account::confirm_email_verification_code(&account_data, "000000", "email-code", 1700002200, &error_message));
    EXPECT_NE(error_message.find("Too many invalid"), std::string::npos);
    EXPECT_TRUE(account_data.verification_code_hash.empty());
    EXPECT_EQ(account_data.verification_attempt_count, account::MAX_EMAIL_VERIFICATION_ATTEMPTS);
}

TEST(AccountManagement, ThrottlesVerificationCodeResendsForRecentlyIssuedCodes) {
    TemporaryDirectory temp_directory;
    std::string error_message;
    account::AccountData created_account;
    ASSERT_TRUE(account::create_account_for_email(temp_directory.path(), "player@example.com", "ValidPass1", 1700005000, &created_account, &error_message)) << error_message;

    std::string verification_code;
    ASSERT_TRUE(account::prepare_email_verification_code(&created_account, 1700005100, &verification_code, &error_message)) << error_message;
    ASSERT_TRUE(account::write_account_file(temp_directory.path(), created_account, &error_message)) << error_message;

    account::AccountData pending_account;
    EXPECT_FALSE(account::start_email_verification(temp_directory.path(), created_account.account_name, 1700005100 + account::EMAIL_VERIFICATION_RESEND_COOLDOWN_SECONDS - 1, &pending_account, &error_message));
    EXPECT_NE(error_message.find("Please wait"), std::string::npos);
}

TEST(AccountManagement, AddsNormalizedCharactersAndRejectsDuplicateLinks) {
    account::AccountData account_data = make_account();
    account_data.characters.clear();
    std::string error_message;

    ASSERT_TRUE(account::add_character_to_account(&account_data, " Aragorn ", &error_message)) << error_message;
    EXPECT_TRUE(account::account_has_character(account_data, "aragorn"));
    EXPECT_TRUE(account::account_has_character(account_data, "Aragorn"));

    EXPECT_FALSE(account::add_character_to_account(&account_data, "aragorn", &error_message));
    EXPECT_NE(error_message.find("already linked"), std::string::npos);
}

TEST(AccountManagement, SelectsOnlyCharactersLinkedToTheAccount) {
    account::AccountData account_data = make_account();
    std::string selected_character;
    std::string error_message;

    ASSERT_TRUE(account::select_linked_character(account_data, " legolas ", &selected_character, &error_message)) << error_message;
    EXPECT_EQ(selected_character, "legolas");

    EXPECT_FALSE(account::select_linked_character(account_data, "gandalf", &selected_character, &error_message));
    EXPECT_NE(error_message.find("not linked"), std::string::npos);
}

TEST(AccountManagement, BlocksAndUnblocksAccountsWithAuditMetadata) {
    account::AccountData account_data = make_account();
    account_data.blocked = false;
    account_data.block_reason.clear();
    account_data.blocked_by.clear();
    account_data.blocked_at = 0;
    account_data.updated_at = 0;

    account::block_account(&account_data, "AdminUser", "Chargeback", 1700002222);

    EXPECT_TRUE(account_data.blocked);
    EXPECT_EQ(account_data.blocked_by, "AdminUser");
    EXPECT_EQ(account_data.block_reason, "Chargeback");
    EXPECT_EQ(account_data.blocked_at, 1700002222);
    EXPECT_EQ(account_data.updated_at, 1700002222);

    account::unblock_account(&account_data);

    EXPECT_FALSE(account_data.blocked);
    EXPECT_TRUE(account_data.block_reason.empty());
    EXPECT_TRUE(account_data.blocked_by.empty());
    EXPECT_EQ(account_data.blocked_at, 0);
}

TEST(AccountManagement, ResetsPasswordsAndTracksResetMetadata) {
    account::AccountData account_data;
    std::string error_message;
    ASSERT_TRUE(account::initialize_new_account("alpha-admin", "player@example.com", "ValidPass1", 1700001000, &account_data, &error_message)) << error_message;

    const std::string original_hash = account_data.password_hash;

    ASSERT_TRUE(account::reset_account_password(&account_data, "ChangedPass2", "SecurityAdmin", 1700003333, &error_message)) << error_message;

    EXPECT_NE(account_data.password_hash, original_hash)
        << "Expected password resets to produce a new one-way password hash.";
    EXPECT_EQ(account_data.password_reset_by, "SecurityAdmin");
    EXPECT_EQ(account_data.password_reset_at, 1700003333);
    EXPECT_EQ(account_data.updated_at, 1700003333);
    EXPECT_TRUE(account::verify_password("ChangedPass2", account_data.password_hash));
    EXPECT_FALSE(account::verify_password("ValidPass1", account_data.password_hash));
}

TEST(AccountManagement, ResolvesBucketedAccountPathsLikePlayerFiles) {
    EXPECT_EQ(account::account_bucket_for_name("alpha"), "A-E");
    EXPECT_EQ(account::account_bucket_for_name("jester"), "F-J");
    EXPECT_EQ(account::account_bucket_for_name("morgoth"), "K-O");
    EXPECT_EQ(account::account_bucket_for_name("samwise"), "P-T");
    EXPECT_EQ(account::account_bucket_for_name("ulmo"), "U-Z");
    EXPECT_EQ(account::account_bucket_for_name("1account"), "ZZZ");
}

TEST(AccountManagement, BuildsBucketedJsonFilePathsUsingNormalizedName) {
    EXPECT_EQ(account::account_file_path("/game/lib", " Alpha-Admin "),
        "/game/lib/accounts/A-E/alpha-admin.json");
}

TEST(AccountManagement, BuildsLegacyCharacterPathsFromCharacterName) {
    EXPECT_EQ(account::legacy_player_file_path("/game/lib", " Aragorn "),
        "/game/lib/players/A-E/aragorn");
    EXPECT_EQ(account::legacy_object_file_path("/game/lib", " Aragorn "),
        "/game/lib/plrobjs/A-E/aragorn.obj");
    EXPECT_EQ(account::legacy_exploits_file_path("/game/lib", " Aragorn "),
        "/game/lib/exploits/A-E/aragorn.exploits");
}

TEST(AccountManagement, SerializesAndDeserializesAccountJsonRoundTrip) {
    const account::AccountData original_account = make_account();
    const std::string json = account::serialize_account_to_json(original_account);

    account::AccountData parsed_account;
    std::string error_message;
    ASSERT_TRUE(account::deserialize_account_from_json(json, &parsed_account, &error_message)) << error_message;

    EXPECT_EQ(parsed_account.version, account::ACCOUNT_SCHEMA_VERSION);
    EXPECT_EQ(parsed_account.account_name, "alpha-admin");
    EXPECT_EQ(parsed_account.normalized_email, original_account.normalized_email);
    EXPECT_EQ(parsed_account.password_hash, original_account.password_hash);
    EXPECT_EQ(parsed_account.password_salt, original_account.password_salt);
    EXPECT_EQ(parsed_account.characters, original_account.characters);
    EXPECT_EQ(parsed_account.email_verified, original_account.email_verified);
    EXPECT_EQ(parsed_account.email_verified_by, original_account.email_verified_by);
    EXPECT_EQ(parsed_account.email_verified_at, original_account.email_verified_at);
    EXPECT_EQ(parsed_account.verification_code_hash, original_account.verification_code_hash);
    EXPECT_EQ(parsed_account.verification_code_sent_at, original_account.verification_code_sent_at);
    EXPECT_EQ(parsed_account.verification_code_expires_at, original_account.verification_code_expires_at);
    EXPECT_EQ(parsed_account.verification_attempt_count, original_account.verification_attempt_count);
    EXPECT_EQ(parsed_account.verification_last_attempt_at, original_account.verification_last_attempt_at);
    EXPECT_TRUE(parsed_account.blocked);
    EXPECT_EQ(parsed_account.block_reason, original_account.block_reason);
    EXPECT_EQ(parsed_account.blocked_by, original_account.blocked_by);
    EXPECT_EQ(parsed_account.blocked_at, original_account.blocked_at);
    EXPECT_EQ(parsed_account.created_at, original_account.created_at);
    EXPECT_EQ(parsed_account.updated_at, original_account.updated_at);
    EXPECT_EQ(parsed_account.password_reset_at, original_account.password_reset_at);
    EXPECT_EQ(parsed_account.password_reset_by, original_account.password_reset_by);
}

TEST(AccountManagement, FormatsAccountSummariesIncludingLinkedCharacters) {
    account::AccountData account_data = make_account();

    const std::string summary = account::format_account_summary(account_data);

    EXPECT_NE(summary.find("Account: alpha-admin"), std::string::npos);
    EXPECT_NE(summary.find("Email: player@example.com"), std::string::npos);
    EXPECT_NE(summary.find("Email verified: yes"), std::string::npos);
    EXPECT_NE(summary.find("Blocked: yes"), std::string::npos);
    EXPECT_NE(summary.find("Characters (2): Aragorn, Legolas"), std::string::npos);
}

TEST(AccountManagement, FormatsCharacterPromptWithLinkedCharacterList) {
    account::AccountData account_data = make_account();

    const std::string prompt = account::format_account_character_prompt(account_data);

    EXPECT_NE(prompt.find("alpha-admin"), std::string::npos);
    EXPECT_NE(prompt.find("Aragorn"), std::string::npos);
    EXPECT_NE(prompt.find("Legolas"), std::string::npos);
    EXPECT_NE(prompt.find("Character: "), std::string::npos);
}

TEST(AccountManagement, RejectsMalformedJsonInput) {
    account::AccountData parsed_account;
    std::string error_message;

    EXPECT_FALSE(account::deserialize_account_from_json("{\"version\":1", &parsed_account, &error_message));
    EXPECT_FALSE(error_message.empty()) << "Expected malformed JSON to report a parsing failure.";
}

TEST(AccountManagement, PersistsAccountsToBucketedJsonFilesAndReadsThemBack) {
    TemporaryDirectory temp_directory;
    const account::AccountData original_account = make_account();
    std::string error_message;

    ASSERT_TRUE(account::write_account_file(temp_directory.path(), original_account, &error_message)) << error_message;

    account::AccountData loaded_account;
    ASSERT_TRUE(account::read_account_file(temp_directory.path(), "ALPHA-ADMIN", &loaded_account, &error_message)) << error_message;

    EXPECT_EQ(loaded_account.account_name, "alpha-admin");
    EXPECT_EQ(loaded_account.characters, original_account.characters);
    EXPECT_EQ(loaded_account.block_reason, original_account.block_reason);

    struct stat file_info {};
    ASSERT_EQ(stat(account::account_file_path(temp_directory.path(), original_account.account_name).c_str(), &file_info), 0)
        << "Expected write_account_file() to materialize the final account JSON file.";

    EXPECT_NE(stat((account::account_file_path(temp_directory.path(), original_account.account_name) + ".tmp").c_str(), &file_info), 0)
        << "Expected temporary files to be cleaned up after a successful atomic write.";
}

TEST(AccountManagement, ReportsMissingAccountFilesClearly) {
    TemporaryDirectory temp_directory;
    account::AccountData loaded_account;
    std::string error_message;

    EXPECT_FALSE(account::read_account_file(temp_directory.path(), "missing-account", &loaded_account, &error_message));
    EXPECT_NE(error_message.find("Failed to open account file"), std::string::npos);
}

TEST(AccountManagement, RejectsUnsafeAccountNamesBeforeFilesystemAccess) {
    TemporaryDirectory temp_directory;
    account::AccountData loaded_account;
    std::string error_message;

    EXPECT_FALSE(account::read_account_file(temp_directory.path(), "../escape", &loaded_account, &error_message));
    EXPECT_NE(error_message.find("Account name"), std::string::npos);
}

TEST(AccountManagement, CreatesAccountsOnDiskAndRejectsDuplicates) {
    TemporaryDirectory temp_directory;
    account::AccountData created_account;
    std::string error_message;

    ASSERT_TRUE(account::create_account(temp_directory.path(), "alpha-admin", "player@example.com", "ValidPass1", 1700004444, &created_account, &error_message)) << error_message;
    EXPECT_EQ(created_account.account_name, "alpha-admin");
    EXPECT_TRUE(account::verify_password("ValidPass1", created_account.password_hash));

    EXPECT_FALSE(account::create_account(temp_directory.path(), "alpha-admin", "other@example.com", "ValidPass1", 1700004445, nullptr, &error_message));
    EXPECT_NE(error_message.find("already exists"), std::string::npos);
}

TEST(AccountManagement, CreatesAndFindsAccountsByEmailAddress) {
    TemporaryDirectory temp_directory;
    account::AccountData created_account;
    std::string error_message;

    ASSERT_TRUE(account::create_account_for_email(temp_directory.path(), "Player@Example.com", "ValidPass1", 1700005000, &created_account, &error_message)) << error_message;
    EXPECT_EQ(created_account.normalized_email, "player@example.com");
    EXPECT_FALSE(created_account.email_verified);

    account::AccountData looked_up_account;
    ASSERT_TRUE(account::read_account_file_by_email(temp_directory.path(), "player@example.com", &looked_up_account, &error_message)) << error_message;
    EXPECT_EQ(looked_up_account.account_name, created_account.account_name);
    EXPECT_EQ(looked_up_account.normalized_email, "player@example.com");
}

TEST(AccountManagement, AuthenticatesAccountsFromDiskAndRejectsBlockedAccounts) {
    TemporaryDirectory temp_directory;
    account::AccountData created_account;
    std::string error_message;
    ASSERT_TRUE(account::create_account(temp_directory.path(), "alpha-admin", "player@example.com", "ValidPass1", 1700005555, &created_account, &error_message)) << error_message;
    ASSERT_TRUE(account::admin_verify_email(temp_directory.path(), "alpha-admin", "VerifierAdmin", 1700005555, &created_account, &error_message)) << error_message;

    account::AccountData authenticated_account;
    ASSERT_TRUE(account::authenticate_account(temp_directory.path(), "alpha-admin", "ValidPass1", &authenticated_account, &error_message)) << error_message;
    EXPECT_EQ(authenticated_account.account_name, "alpha-admin");

    EXPECT_FALSE(account::authenticate_account(temp_directory.path(), "alpha-admin", "WrongPass1", nullptr, &error_message));
    EXPECT_EQ(error_message, "Account authentication failed.");

    ASSERT_TRUE(account::admin_block_account(temp_directory.path(), "alpha-admin", "AdminUser", "Chargeback", 1700005556, nullptr, &error_message)) << error_message;
    EXPECT_FALSE(account::authenticate_account(temp_directory.path(), "alpha-admin", "ValidPass1", nullptr, &error_message));
    EXPECT_EQ(error_message, "Account authentication failed.");
}

TEST(AccountManagement, AuthenticatesAccountsByEmailAddress) {
    TemporaryDirectory temp_directory;
    std::string error_message;
    account::AccountData created_account;
    ASSERT_TRUE(account::create_account_for_email(temp_directory.path(), "player@example.com", "ValidPass1", 1700005600, &created_account, &error_message)) << error_message;

    EXPECT_FALSE(account::authenticate_account_by_email(temp_directory.path(), "Player@example.com", "ValidPass1", nullptr, &error_message));
    EXPECT_EQ(error_message, "Account email verification is still pending.");

    account::AccountData pending_account;
    EXPECT_FALSE(account::authenticate_account_by_email(temp_directory.path(), "Player@example.com", "ValidPass1", &pending_account, &error_message));
    EXPECT_EQ(error_message, "Account email verification is still pending.");
    EXPECT_EQ(pending_account.account_name, created_account.account_name);

    ASSERT_TRUE(account::admin_verify_email(temp_directory.path(), created_account.account_name, "VerifierAdmin", 1700005601, nullptr, &error_message)) << error_message;

    account::AccountData authenticated_account;
    ASSERT_TRUE(account::authenticate_account_by_email(temp_directory.path(), "Player@example.com", "ValidPass1", &authenticated_account, &error_message)) << error_message;
    EXPECT_EQ(authenticated_account.normalized_email, "player@example.com");

    EXPECT_FALSE(account::authenticate_account_by_email(temp_directory.path(), "player@example.com", "WrongPass1", nullptr, &error_message));
    EXPECT_EQ(error_message, "Account authentication failed.");
}

TEST(AccountManagement, SupportsAccountPasswordsLongerThanLegacyCharacterLimit) {
    TemporaryDirectory temp_directory;
    std::string error_message;
    const std::string long_password = "LongerAccountPassword1";

    account::AccountData created_account;
    ASSERT_TRUE(account::create_account_for_email(temp_directory.path(), "player@example.com", long_password, 1700005601, &created_account, &error_message)) << error_message;
    EXPECT_TRUE(account::verify_password(long_password, created_account.password_hash));
    ASSERT_TRUE(account::admin_verify_email(temp_directory.path(), created_account.account_name, "VerifierAdmin", 1700005602, nullptr, &error_message)) << error_message;

    account::AccountData authenticated_account;
    ASSERT_TRUE(account::authenticate_account_by_email(temp_directory.path(), "Player@example.com", long_password, &authenticated_account, &error_message)) << error_message;
    EXPECT_EQ(authenticated_account.account_name, created_account.account_name);
}

TEST(AccountManagement, RejectsAmbiguousDuplicateEmailRecords) {
    TemporaryDirectory temp_directory;
    std::string error_message;
    ASSERT_TRUE(account::create_account(temp_directory.path(), "alpha-admin", "player@example.com", "ValidPass1", 1700005700, nullptr, &error_message)) << error_message;
    ASSERT_TRUE(account::create_account(temp_directory.path(), "beta-admin", "other@example.com", "ValidPass1", 1700005701, nullptr, &error_message)) << error_message;

    account::AccountData duplicate_account;
    ASSERT_TRUE(account::read_account_file(temp_directory.path(), "beta-admin", &duplicate_account, &error_message)) << error_message;
    duplicate_account.normalized_email = "player@example.com";
    ASSERT_TRUE(account::write_account_file(temp_directory.path(), duplicate_account, &error_message)) << error_message;

    account::AccountData looked_up_account;
    EXPECT_FALSE(account::read_account_file_by_email(temp_directory.path(), "player@example.com", &looked_up_account, &error_message));
    EXPECT_NE(error_message.find("Multiple account records"), std::string::npos);
}

TEST(AccountManagement, FindsEmailBackedAccountEvenIfUnrelatedAccountFileIsCorrupt) {
    TemporaryDirectory temp_directory;
    std::string error_message;
    ASSERT_TRUE(account::create_account_for_email(temp_directory.path(), "player@example.com", "ValidPass1", 1700005800, nullptr, &error_message)) << error_message;

    ASSERT_EQ(mkdir((temp_directory.path() + "/accounts/ZZZ").c_str(), 0700), 0);
    write_text_file(temp_directory.path() + "/accounts/ZZZ/corrupt.json", "{bad-json");

    account::AccountData looked_up_account;
    ASSERT_TRUE(account::read_account_file_by_email(temp_directory.path(), "player@example.com", &looked_up_account, &error_message)) << error_message;
    EXPECT_EQ(looked_up_account.normalized_email, "player@example.com");
}

TEST(AccountManagement, ReportsMissingEmailEvenIfUnrelatedAccountFileIsCorrupt) {
    TemporaryDirectory temp_directory;
    std::string error_message;
    ASSERT_TRUE(account::create_account_for_email(temp_directory.path(), "player@example.com", "ValidPass1", 1700005801, nullptr, &error_message)) << error_message;

    ASSERT_EQ(mkdir((temp_directory.path() + "/accounts/ZZZ").c_str(), 0700), 0);
    write_text_file(temp_directory.path() + "/accounts/ZZZ/corrupt.json", "{bad-json");

    account::AccountData looked_up_account;
    EXPECT_FALSE(account::read_account_file_by_email(temp_directory.path(), "new@example.com", &looked_up_account, &error_message));
    EXPECT_EQ(error_message, "No account exists for that email address.");
}

TEST(AccountManagement, FailsClosedWhenCreatingAccountIfAnyStoredRecordIsUnreadable) {
    TemporaryDirectory temp_directory;
    std::string error_message;
    ASSERT_TRUE(account::create_account_for_email(temp_directory.path(), "player@example.com", "ValidPass1", 1700005802, nullptr, &error_message)) << error_message;

    ASSERT_EQ(mkdir((temp_directory.path() + "/accounts/ZZZ").c_str(), 0700), 0);
    write_text_file(temp_directory.path() + "/accounts/ZZZ/corrupt.json", "{bad-json");

    account::AccountData created_account;
    EXPECT_FALSE(account::create_account_for_email(temp_directory.path(), "new@example.com", "ValidPass1", 1700005803, &created_account, &error_message));
    EXPECT_EQ(error_message, "Existing account records could not be read safely.");
}

TEST(AccountManagement, SupportsAdminLinkBlockUnblockAndPasswordResetOnDisk) {
    TemporaryDirectory temp_directory;
    std::string error_message;
    ASSERT_TRUE(account::create_account(temp_directory.path(), "alpha-admin", "player@example.com", "ValidPass1", 1700006660, nullptr, &error_message)) << error_message;
    ASSERT_TRUE(account::admin_verify_email(temp_directory.path(), "alpha-admin", "VerifierAdmin", 1700006660, nullptr, &error_message)) << error_message;

    account::AccountData updated_account;
    ASSERT_TRUE(account::admin_link_character(temp_directory.path(), "alpha-admin", "Aragorn", 1700006661, &updated_account, &error_message)) << error_message;
    EXPECT_TRUE(account::account_has_character(updated_account, "aragorn"));
    EXPECT_EQ(updated_account.updated_at, 1700006661);

    EXPECT_FALSE(account::admin_link_character(temp_directory.path(), "alpha-admin", "aragorn", 1700006662, nullptr, &error_message));
    EXPECT_NE(error_message.find("already linked"), std::string::npos);

    ASSERT_TRUE(account::admin_block_account(temp_directory.path(), "alpha-admin", "AdminUser", "Chargeback", 1700006663, &updated_account, &error_message)) << error_message;
    EXPECT_TRUE(updated_account.blocked);

    ASSERT_TRUE(account::admin_unblock_account(temp_directory.path(), "alpha-admin", 1700006664, &updated_account, &error_message)) << error_message;
    EXPECT_FALSE(updated_account.blocked);
    EXPECT_EQ(updated_account.updated_at, 1700006664);

    ASSERT_TRUE(account::admin_unverify_email(temp_directory.path(), "alpha-admin", 1700006665, &updated_account, &error_message)) << error_message;
    EXPECT_FALSE(updated_account.email_verified);

    ASSERT_TRUE(account::admin_verify_email(temp_directory.path(), "alpha-admin", "VerifierAdmin", 1700006666, &updated_account, &error_message)) << error_message;
    EXPECT_TRUE(updated_account.email_verified);

    const std::string prior_hash = updated_account.password_hash;
    ASSERT_TRUE(account::admin_reset_password(temp_directory.path(), "alpha-admin", "ChangedPass2", "SecurityAdmin", 1700006667, &updated_account, &error_message)) << error_message;
    EXPECT_NE(updated_account.password_hash, prior_hash);
    EXPECT_TRUE(account::verify_password("ChangedPass2", updated_account.password_hash));
}

TEST(AccountManagement, PreventsLinkingOneCharacterToMultipleAccounts) {
    TemporaryDirectory temp_directory;
    std::string error_message;

    ASSERT_TRUE(account::create_account(temp_directory.path(), "alpha-admin", "player@example.com", "ValidPass1", 1700007000, nullptr, &error_message)) << error_message;
    ASSERT_TRUE(account::create_account(temp_directory.path(), "beta-admin", "other@example.com", "ValidPass1", 1700007001, nullptr, &error_message)) << error_message;
    ASSERT_TRUE(account::admin_link_character(temp_directory.path(), "alpha-admin", "Aragorn", 1700007002, nullptr, &error_message)) << error_message;

    EXPECT_FALSE(account::admin_link_character(temp_directory.path(), "beta-admin", "aragorn", 1700007003, nullptr, &error_message));
    EXPECT_NE(error_message.find("already linked to account"), std::string::npos);
}

TEST(AccountManagement, FindsLinkedCharacterOwnerAccount) {
    TemporaryDirectory temp_directory;
    std::string error_message;

    ASSERT_TRUE(account::create_account(temp_directory.path(), "alpha-admin", "player@example.com", "ValidPass1", 1700007004, nullptr, &error_message)) << error_message;
    ASSERT_TRUE(account::admin_link_character(temp_directory.path(), "alpha-admin", "Aragorn", 1700007005, nullptr, &error_message)) << error_message;

    std::string owner_account_name;
    ASSERT_TRUE(account::find_linked_character_owner_account(temp_directory.path(), "aragorn", &owner_account_name, &error_message)) << error_message;
    EXPECT_EQ(owner_account_name, "alpha-admin");
}

TEST(AccountManagement, RejectsAmbiguousLinkedCharacterOwnership) {
    TemporaryDirectory temp_directory;
    std::string error_message;

    ASSERT_TRUE(account::create_account(temp_directory.path(), "alpha-admin", "player@example.com", "ValidPass1", 1700007006, nullptr, &error_message)) << error_message;
    ASSERT_TRUE(account::create_account(temp_directory.path(), "beta-admin", "other@example.com", "ValidPass1", 1700007007, nullptr, &error_message)) << error_message;
    ASSERT_TRUE(account::admin_link_character(temp_directory.path(), "alpha-admin", "Aragorn", 1700007008, nullptr, &error_message)) << error_message;

    account::AccountData duplicate_owner;
    ASSERT_TRUE(account::read_account_file(temp_directory.path(), "beta-admin", &duplicate_owner, &error_message)) << error_message;
    duplicate_owner.characters.push_back("aragorn");
    ASSERT_TRUE(account::write_account_file(temp_directory.path(), duplicate_owner, &error_message)) << error_message;

    std::string owner_account_name;
    EXPECT_FALSE(account::find_linked_character_owner_account(temp_directory.path(), "aragorn", &owner_account_name, &error_message));
    EXPECT_NE(error_message.find("Multiple account records claim"), std::string::npos);
}

TEST(AccountManagement, LinksAndMigratesCharacterAfterAuthenticatingAccount) {
    TemporaryDirectory temp_directory;
    ASSERT_EQ(mkdir((temp_directory.path() + "/players").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/players/A-E").c_str(), 0700), 0);
    write_text_file(account::legacy_player_file_path(temp_directory.path(), "aragorn"), "legacy-player-data");

    std::string error_message;
    ASSERT_TRUE(account::create_account(temp_directory.path(), "alpha-admin", "player@example.com", "ValidPass1", 1700012222, nullptr, &error_message)) << error_message;
    ASSERT_TRUE(account::admin_verify_email(temp_directory.path(), "alpha-admin", "VerifierAdmin", 1700012222, nullptr, &error_message)) << error_message;

    account::AccountData linked_account;
    account::CharacterMigrationData migration;
    ASSERT_TRUE(account::link_and_migrate_character(temp_directory.path(), "alpha-admin", "ValidPass1", "aragorn", 1700012223, &linked_account, &migration, &error_message)) << error_message;

    EXPECT_TRUE(account::account_has_character(linked_account, "aragorn"));
    EXPECT_EQ(migration.character_name, "aragorn");
    EXPECT_TRUE(migration.player_file.present);

    EXPECT_FALSE(account::link_and_migrate_character(temp_directory.path(), "alpha-admin", "WrongPass1", "aragorn", 1700012224, nullptr, nullptr, &error_message));
    EXPECT_EQ(error_message, "Account authentication failed.");
}

TEST(AccountManagement, DoesNotLeaveCharacterLinkedWhenMigrationFails) {
    TemporaryDirectory temp_directory;
    std::string error_message;
    ASSERT_TRUE(account::create_account(temp_directory.path(), "alpha-admin", "player@example.com", "ValidPass1", 1700012225, nullptr, &error_message)) << error_message;
    ASSERT_TRUE(account::admin_verify_email(temp_directory.path(), "alpha-admin", "VerifierAdmin", 1700012225, nullptr, &error_message)) << error_message;

    EXPECT_FALSE(account::link_and_migrate_character(temp_directory.path(), "alpha-admin", "ValidPass1", "aragorn", 1700012226, nullptr, nullptr, &error_message));
    EXPECT_NE(error_message.find("Failed to open legacy file"), std::string::npos);

    account::AccountData account_data;
    ASSERT_TRUE(account::read_account_file(temp_directory.path(), "alpha-admin", &account_data, &error_message)) << error_message;
    EXPECT_FALSE(account::account_has_character(account_data, "aragorn"));
}

TEST(AccountManagement, BuildsAccountLinkedCharacterSnapshotPaths) {
    EXPECT_EQ(account::account_character_directory("/game/lib", "alpha-admin", "aragorn"),
        "/game/lib/account_characters/A-E/alpha-admin/aragorn");
    EXPECT_EQ(account::account_character_snapshot_path("/game/lib", "alpha-admin", "aragorn"),
        "/game/lib/account_characters/A-E/alpha-admin/aragorn/snapshot.json");
}

TEST(AccountManagement, MigratesLegacyCharacterFilesIntoAccountLinkedSnapshotJson) {
    TemporaryDirectory temp_directory;

    const std::string player_path = temp_directory.path() + "/player.dat";
    const std::string object_path = temp_directory.path() + "/objects.obj";
    const std::string exploits_path = temp_directory.path() + "/history.exploits";

    write_text_file(player_path, "legacy-player-data");
    write_text_file(object_path, std::string("obj\0bin", 7));
    write_text_file(exploits_path, "legacy-exploits-data");

    account::CharacterMigrationData migration;
    std::string error_message;
    ASSERT_EQ(mkdir((temp_directory.path() + "/players").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/players/A-E").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/plrobjs").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/plrobjs/A-E").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/exploits").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/exploits/A-E").c_str(), 0700), 0);

    write_text_file(account::legacy_player_file_path(temp_directory.path(), "aragorn"), "legacy-player-data");
    write_text_file(account::legacy_object_file_path(temp_directory.path(), "aragorn"), std::string("obj\0bin", 7));
    write_text_file(account::legacy_exploits_file_path(temp_directory.path(), "aragorn"), "legacy-exploits-data");

    ASSERT_TRUE(account::migrate_legacy_character_by_name(temp_directory.path(), "alpha-admin", "aragorn", 1700007777, &migration, &error_message)) << error_message;

    EXPECT_EQ(migration.account_name, "alpha-admin");
    EXPECT_EQ(migration.character_name, "aragorn");
    EXPECT_EQ(migration.migrated_at, 1700007777);
    EXPECT_TRUE(migration.player_file.present);
    EXPECT_TRUE(migration.object_file.present);
    EXPECT_TRUE(migration.exploits_file.present);
    EXPECT_EQ(migration.player_file.encoding, "hex");
    EXPECT_EQ(migration.player_file.source_path, account::legacy_player_file_path(temp_directory.path(), "aragorn"));

    account::CharacterMigrationData loaded_migration;
    ASSERT_TRUE(account::read_character_migration(temp_directory.path(), "alpha-admin", "aragorn", &loaded_migration, &error_message)) << error_message;
    EXPECT_EQ(loaded_migration.account_name, "alpha-admin");
    EXPECT_EQ(loaded_migration.character_name, "aragorn");
    EXPECT_EQ(loaded_migration.player_file.content, migration.player_file.content);
    EXPECT_EQ(loaded_migration.object_file.content, migration.object_file.content);
    EXPECT_EQ(loaded_migration.exploits_file.content, migration.exploits_file.content);

    struct stat file_info {};
    ASSERT_EQ(stat(account::account_character_snapshot_path(temp_directory.path(), "alpha-admin", "aragorn").c_str(), &file_info), 0)
        << "Expected migration to materialize the account-linked snapshot JSON file.";
}

TEST(AccountManagement, ReportsMissingRequiredPlayerFileDuringMigration) {
    TemporaryDirectory temp_directory;
    account::CharacterMigrationData migration;
    std::string error_message;

    EXPECT_FALSE(account::migrate_legacy_character_by_name(temp_directory.path(), "alpha-admin", "aragorn", 1700008888, &migration, &error_message));
    EXPECT_NE(error_message.find("Failed to open legacy file"), std::string::npos);
}

TEST(AccountManagement, MigratesLegacyCharacterByDefaultFileLayout) {
    TemporaryDirectory temp_directory;
    ASSERT_EQ(mkdir((temp_directory.path() + "/players").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/players/A-E").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/plrobjs").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/plrobjs/A-E").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/exploits").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/exploits/A-E").c_str(), 0700), 0);

    write_text_file(account::legacy_player_file_path(temp_directory.path(), "aragorn"), "legacy-player-data");
    write_text_file(account::legacy_object_file_path(temp_directory.path(), "aragorn"), "legacy-object-data");
    write_text_file(account::legacy_exploits_file_path(temp_directory.path(), "aragorn"), "legacy-exploits-data");

    account::CharacterMigrationData migration;
    std::string error_message;
    ASSERT_TRUE(account::migrate_legacy_character_by_name(temp_directory.path(), "alpha-admin", "aragorn", 1700009999, &migration, &error_message)) << error_message;
    EXPECT_EQ(migration.account_name, "alpha-admin");
    EXPECT_EQ(migration.character_name, "aragorn");
}

TEST(AccountManagement, TreatsMissingOptionalLegacyFilesAsAbsentSnapshots) {
    TemporaryDirectory temp_directory;
    ASSERT_EQ(mkdir((temp_directory.path() + "/players").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/players/A-E").c_str(), 0700), 0);

    write_text_file(account::legacy_player_file_path(temp_directory.path(), "aragorn"), "legacy-player-data");

    account::CharacterMigrationData migration;
    std::string error_message;
    ASSERT_TRUE(account::migrate_legacy_character_by_name(temp_directory.path(), "alpha-admin", "aragorn", 1700010000, &migration, &error_message)) << error_message;

    EXPECT_TRUE(migration.player_file.present);
    EXPECT_FALSE(migration.object_file.present);
    EXPECT_FALSE(migration.exploits_file.present);
}

TEST(AccountManagement, EnsuresCharacterMigrationByCreatingMissingSnapshotFromLegacyFiles) {
    TemporaryDirectory temp_directory;
    ASSERT_EQ(mkdir((temp_directory.path() + "/players").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/players/A-E").c_str(), 0700), 0);

    write_text_file(account::legacy_player_file_path(temp_directory.path(), "aragorn"), "legacy-player-data");

    account::CharacterMigrationData migration;
    std::string error_message;
    ASSERT_TRUE(account::ensure_character_migration(temp_directory.path(), "alpha-admin", "aragorn", 1700010100, &migration, &error_message)) << error_message;
    EXPECT_EQ(migration.account_name, "alpha-admin");
    EXPECT_EQ(migration.character_name, "aragorn");
    EXPECT_TRUE(migration.player_file.present);

    account::CharacterMigrationData loaded_migration;
    ASSERT_TRUE(account::read_character_migration(temp_directory.path(), "alpha-admin", "aragorn", &loaded_migration, &error_message)) << error_message;
    EXPECT_EQ(loaded_migration.player_file.content, migration.player_file.content);
}

TEST(AccountManagement, RebuildsCorruptCharacterMigrationSnapshotFromLegacyFiles) {
    TemporaryDirectory temp_directory;
    ASSERT_EQ(mkdir((temp_directory.path() + "/players").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/players/A-E").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/account_characters").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/account_characters/A-E").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/account_characters/A-E/alpha-admin").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/account_characters/A-E/alpha-admin/aragorn").c_str(), 0700), 0);

    write_text_file(account::legacy_player_file_path(temp_directory.path(), "aragorn"), "legacy-player-data");
    write_text_file(account::account_character_snapshot_path(temp_directory.path(), "alpha-admin", "aragorn"), "{bad-json");

    account::CharacterMigrationData migration;
    std::string error_message;
    ASSERT_TRUE(account::ensure_character_migration(temp_directory.path(), "alpha-admin", "aragorn", 1700010100, &migration, &error_message)) << error_message;
    EXPECT_TRUE(migration.player_file.present);

    account::CharacterMigrationData loaded_migration;
    ASSERT_TRUE(account::read_character_migration(temp_directory.path(), "alpha-admin", "aragorn", &loaded_migration, &error_message)) << error_message;
    EXPECT_EQ(loaded_migration.player_file.content, migration.player_file.content);
}

TEST(AccountManagement, DecodesSnapshotContentBackIntoOriginalBytes) {
    account::CharacterMigrationData migration;
    std::string error_message;
    migration.player_file.present = true;
    migration.player_file.encoding = "hex";
    migration.player_file.content = "6c65676163792d706c617965722d64617461";

    std::string contents;
    ASSERT_TRUE(account::decode_snapshot_content(migration.player_file, &contents, &error_message)) << error_message;
    EXPECT_EQ(contents, "legacy-player-data");
}

TEST(AccountManagement, RestoresLegacyFilesFromCharacterMigrationSnapshot) {
    TemporaryDirectory temp_directory;
    ASSERT_EQ(mkdir((temp_directory.path() + "/players").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/players/A-E").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/plrobjs").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/plrobjs/A-E").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/exploits").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/exploits/A-E").c_str(), 0700), 0);

    write_text_file(account::legacy_player_file_path(temp_directory.path(), "aragorn"), "legacy-player-data");
    write_text_file(account::legacy_object_file_path(temp_directory.path(), "aragorn"), std::string("obj\0bin", 7));

    account::CharacterMigrationData migration;
    std::string error_message;
    ASSERT_TRUE(account::migrate_legacy_character_by_name(temp_directory.path(), "alpha-admin", "aragorn", 1700010101, &migration, &error_message)) << error_message;

    write_text_file(account::legacy_player_file_path(temp_directory.path(), "aragorn"), "stale-player-data");
    write_text_file(account::legacy_object_file_path(temp_directory.path(), "aragorn"), "stale-object-data");
    write_text_file(account::legacy_exploits_file_path(temp_directory.path(), "aragorn"), "stale-exploit-data");

    ASSERT_TRUE(account::restore_character_migration(temp_directory.path(), "alpha-admin", "aragorn", migration, &error_message)) << error_message;

    EXPECT_EQ(read_file_contents(account::legacy_player_file_path(temp_directory.path(), "aragorn")), "legacy-player-data");
    EXPECT_EQ(read_file_contents(account::legacy_object_file_path(temp_directory.path(), "aragorn")), std::string("obj\0bin", 7));

    struct stat file_info {};
    EXPECT_NE(stat(account::legacy_exploits_file_path(temp_directory.path(), "aragorn").c_str(), &file_info), 0);
}

TEST(AccountManagement, RestoresOnlyRuntimeSupportFilesWithoutRewritingPlayerFile) {
    TemporaryDirectory temp_directory;
    ASSERT_EQ(mkdir((temp_directory.path() + "/players").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/players/A-E").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/plrobjs").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/plrobjs/A-E").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/exploits").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/exploits/A-E").c_str(), 0700), 0);

    account::CharacterMigrationData migration;
    migration.account_name = "alpha-admin";
    migration.character_name = "aragorn";
    migration.object_file.present = true;
    migration.object_file.encoding = "hex";
    migration.object_file.content = "6f626a2d64617461";
    migration.exploits_file.present = false;

    write_text_file(account::legacy_player_file_path(temp_directory.path(), "aragorn"), "player-stays-put");
    write_text_file(account::legacy_exploits_file_path(temp_directory.path(), "aragorn"), "stale-exploit-data");

    std::string error_message;
    ASSERT_TRUE(account::restore_character_runtime_support_files(temp_directory.path(), "alpha-admin", "aragorn", migration, &error_message)) << error_message;
    EXPECT_EQ(read_file_contents(account::legacy_player_file_path(temp_directory.path(), "aragorn")), "player-stays-put");
    EXPECT_EQ(read_file_contents(account::legacy_object_file_path(temp_directory.path(), "aragorn")), "obj-data");

    struct stat file_info {};
    EXPECT_NE(stat(account::legacy_exploits_file_path(temp_directory.path(), "aragorn").c_str(), &file_info), 0);
}

TEST(AccountManagement, RejectsRestoringSupportFilesWhenSnapshotIdentityDoesNotMatchSelection) {
    TemporaryDirectory temp_directory;
    account::CharacterMigrationData migration;
    migration.account_name = "beta-admin";
    migration.character_name = "legolas";
    migration.object_file.present = true;
    migration.object_file.encoding = "hex";
    migration.object_file.content = "6f626a2d64617461";

    std::string error_message;
    EXPECT_FALSE(account::restore_character_runtime_support_files(temp_directory.path(), "alpha-admin", "aragorn", migration, &error_message));
    EXPECT_NE(error_message.find("did not match"), std::string::npos);
}

TEST(AccountManagement, RefreshesSnapshotForLinkedCharactersUsingCurrentLegacyFiles) {
    TemporaryDirectory temp_directory;
    ASSERT_EQ(mkdir((temp_directory.path() + "/players").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/players/A-E").c_str(), 0700), 0);

    std::string error_message;
    ASSERT_TRUE(account::create_account(temp_directory.path(), "alpha-admin", "player@example.com", "ValidPass1", 1700010101, nullptr, &error_message)) << error_message;
    ASSERT_TRUE(account::admin_link_character(temp_directory.path(), "alpha-admin", "aragorn", 1700010102, nullptr, &error_message)) << error_message;
    write_text_file(account::legacy_player_file_path(temp_directory.path(), "aragorn"), "legacy-player-data-v2");

    account::CharacterMigrationData migration;
    ASSERT_TRUE(account::refresh_linked_character_snapshot(temp_directory.path(), "aragorn", 1700010103, &migration, &error_message)) << error_message;
    EXPECT_EQ(migration.account_name, "alpha-admin");
    EXPECT_EQ(migration.character_name, "aragorn");
    EXPECT_TRUE(migration.player_file.present);

    account::CharacterMigrationData loaded_migration;
    ASSERT_TRUE(account::read_character_migration(temp_directory.path(), "alpha-admin", "aragorn", &loaded_migration, &error_message)) << error_message;
    EXPECT_EQ(loaded_migration.player_file.content, migration.player_file.content);
}

TEST(AccountManagement, RestoredSnapshotReflectsRefreshedLinkedCharacterState) {
    TemporaryDirectory temp_directory;
    ASSERT_EQ(mkdir((temp_directory.path() + "/players").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/players/A-E").c_str(), 0700), 0);

    std::string error_message;
    ASSERT_TRUE(account::create_account(temp_directory.path(), "alpha-admin", "player@example.com", "ValidPass1", 1700010102, nullptr, &error_message)) << error_message;
    ASSERT_TRUE(account::admin_link_character(temp_directory.path(), "alpha-admin", "aragorn", 1700010103, nullptr, &error_message)) << error_message;
    write_text_file(account::legacy_player_file_path(temp_directory.path(), "aragorn"), "latest-player-data");

    account::CharacterMigrationData migration;
    ASSERT_TRUE(account::refresh_linked_character_snapshot(temp_directory.path(), "aragorn", 1700010104, &migration, &error_message)) << error_message;

    std::remove(account::legacy_player_file_path(temp_directory.path(), "aragorn").c_str());
    ASSERT_TRUE(account::restore_character_migration(temp_directory.path(), "alpha-admin", "aragorn", migration, &error_message)) << error_message;
    EXPECT_EQ(read_file_contents(account::legacy_player_file_path(temp_directory.path(), "aragorn")), "latest-player-data");
}

TEST(AccountManagement, RefreshingSnapshotPreservesExistingExploitHistoryWhenRuntimeExploitFileIsAbsent) {
    TemporaryDirectory temp_directory;
    ASSERT_EQ(mkdir((temp_directory.path() + "/players").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/players/A-E").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/exploits").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/exploits/A-E").c_str(), 0700), 0);

    std::string error_message;
    ASSERT_TRUE(account::create_account(temp_directory.path(), "alpha-admin", "player@example.com", "ValidPass1", 1700010102, nullptr, &error_message)) << error_message;
    ASSERT_TRUE(account::admin_link_character(temp_directory.path(), "alpha-admin", "aragorn", 1700010103, nullptr, &error_message)) << error_message;

    write_text_file(account::legacy_player_file_path(temp_directory.path(), "aragorn"), "player-state-one");
    write_text_file(account::legacy_exploits_file_path(temp_directory.path(), "aragorn"), "historic-exploit-data");

    account::CharacterMigrationData initial_migration;
    ASSERT_TRUE(account::refresh_linked_character_snapshot(temp_directory.path(), "aragorn", 1700010104, &initial_migration, &error_message)) << error_message;
    ASSERT_TRUE(initial_migration.exploits_file.present);

    std::remove(account::legacy_exploits_file_path(temp_directory.path(), "aragorn").c_str());
    write_text_file(account::legacy_player_file_path(temp_directory.path(), "aragorn"), "player-state-two");

    account::CharacterMigrationData refreshed_migration;
    ASSERT_TRUE(account::refresh_linked_character_snapshot(temp_directory.path(), "aragorn", 1700010105, &refreshed_migration, &error_message)) << error_message;
    EXPECT_TRUE(refreshed_migration.exploits_file.present);
    EXPECT_EQ(refreshed_migration.exploits_file.content, initial_migration.exploits_file.content);
    EXPECT_NE(refreshed_migration.player_file.content, initial_migration.player_file.content);

    account::CharacterMigrationData loaded_migration;
    ASSERT_TRUE(account::read_character_migration(temp_directory.path(), "alpha-admin", "aragorn", &loaded_migration, &error_message)) << error_message;
    EXPECT_TRUE(loaded_migration.exploits_file.present);
    EXPECT_EQ(loaded_migration.exploits_file.content, initial_migration.exploits_file.content);
}

TEST(AccountManagement, RefreshingSnapshotForUnlinkedCharacterSucceedsWithoutWritingMigration) {
    TemporaryDirectory temp_directory;
    ASSERT_EQ(mkdir((temp_directory.path() + "/players").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/players/A-E").c_str(), 0700), 0);
    write_text_file(account::legacy_player_file_path(temp_directory.path(), "aragorn"), "legacy-player-data");

    account::CharacterMigrationData migration;
    std::string error_message;
    ASSERT_TRUE(account::refresh_linked_character_snapshot(temp_directory.path(), "aragorn", 1700010104, &migration, &error_message)) << error_message;
    EXPECT_TRUE(migration.account_name.empty());
    EXPECT_TRUE(migration.character_name.empty());

    struct stat file_info {};
    EXPECT_NE(stat(account::account_character_snapshot_path(temp_directory.path(), "alpha-admin", "aragorn").c_str(), &file_info), 0);
}

TEST(AccountManagement, RefusesToOverwriteCorruptExistingAccountFiles) {
    TemporaryDirectory temp_directory;
    ASSERT_EQ(mkdir((temp_directory.path() + "/accounts").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/accounts/A-E").c_str(), 0700), 0);
    write_text_file(account::account_file_path(temp_directory.path(), "alpha-admin"), "{not-valid-json");

    std::string error_message;
    EXPECT_FALSE(account::create_account(temp_directory.path(), "alpha-admin", "player@example.com", "ValidPass1", 1700011111, nullptr, &error_message));
    EXPECT_NE(error_message.find("could not be read safely"), std::string::npos);
}
