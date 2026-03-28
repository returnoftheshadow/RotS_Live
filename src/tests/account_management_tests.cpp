#include "../account_management.h"
#include "../exploits_json.h"
#include "../objects_json.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

extern struct player_index_element* player_table;
extern int top_of_p_table;
void save_player(struct char_data* ch, int load_room, int index_pos);

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

class ScopedWorkingDirectory {
public:
    explicit ScopedWorkingDirectory(const std::string& path)
    {
        char buffer[PATH_MAX];
        char* current_working_directory = getcwd(buffer, sizeof(buffer));
        EXPECT_NE(current_working_directory, nullptr);
        if (current_working_directory != nullptr)
            m_original_path = buffer;

        EXPECT_EQ(chdir(path.c_str()), 0);
    }

    ~ScopedWorkingDirectory()
    {
        if (!m_original_path.empty())
            EXPECT_EQ(chdir(m_original_path.c_str()), 0);
    }

private:
    std::string m_original_path;
};

class ScopedPlayerTableEntry {
public:
    explicit ScopedPlayerTableEntry(const char* name)
        : m_previous_player_table(player_table)
        , m_previous_top_of_p_table(top_of_p_table)
    {
        player_table = new player_index_element[1] {};
        top_of_p_table = 0;
        player_table[0].name = strdup(name);
    }

    ~ScopedPlayerTableEntry()
    {
        free(player_table[0].name);
        delete[] player_table;
        player_table = m_previous_player_table;
        top_of_p_table = m_previous_top_of_p_table;
    }

private:
    player_index_element* m_previous_player_table;
    int m_previous_top_of_p_table;
};

class ScopedEnvironmentVariable {
public:
    ScopedEnvironmentVariable(const char* name, const std::string& value)
        : m_name(name)
    {
        const char* original_value = std::getenv(name);
        if (original_value != nullptr) {
            m_had_original_value = true;
            m_original_value = original_value;
        }

        if (setenv(name, value.c_str(), 1) != 0) {
            ADD_FAILURE() << "Expected test helper to set environment variable " << name << ".";
        }
    }

    ~ScopedEnvironmentVariable()
    {
        if (m_had_original_value)
            setenv(m_name.c_str(), m_original_value.c_str(), 1);
        else
            unsetenv(m_name.c_str());
    }

private:
    std::string m_name;
    std::string m_original_value;
    bool m_had_original_value = false;
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

char_file_u make_stored_character(const char* name = "aragorn")
{
    char_file_u stored_character {};
    std::snprintf(stored_character.name, sizeof(stored_character.name), "%s", name);
    std::snprintf(stored_character.title, sizeof(stored_character.title), "%s", "the Ranger");
    std::snprintf(stored_character.description, sizeof(stored_character.description), "%s", "A ranger from the north.");
    stored_character.sex = 1;
    stored_character.race = 2;
    stored_character.bodytype = 3;
    stored_character.level = 12;
    stored_character.language = 4;
    stored_character.birth = 1700000000;
    stored_character.played = 456;
    stored_character.weight = 190;
    stored_character.height = 72;
    stored_character.hometown = 7;
    stored_character.last_logon = 1700000100;
    stored_character.skills[5] = 88;
    stored_character.talks[2] = 1;
    stored_character.points.gold = 1234;
    stored_character.points.exp = 5678;
    stored_character.points.bodypart_hit[0] = 99;
    stored_character.specials2.idnum = 4242;
    stored_character.specials2.load_room = 3001;
    stored_character.specials2.alignment = 500;
    stored_character.specials2.act = 0;
    stored_character.specials2.pref = 1L << 5;
    stored_character.profs.prof_level[PROF_WARRIOR] = 12;
    stored_character.profs.prof_coof[PROF_WARRIOR] = 34;
    return stored_character;
}

std::string make_valid_object_bytes()
{
    objects_json::ObjectSaveData object_data;
    object_data.rent.rentcode = RENT_CRASH;
    object_data.objects.push_back(objects_json::ObjectRecord {});
    object_data.objects[0].item_number = 1234;
    object_data.objects[0].wear_pos = WEAR_HEAD;

    std::string error_message;
    std::string object_bytes;
    EXPECT_TRUE(objects_json::object_save_data_to_binary(object_data, &object_bytes, &error_message)) << error_message;
    return object_bytes;
}

exploit_record make_exploit_record(int type, const char* timestamp, const char* victim_name, int victim_level, int killer_level, int int_param)
{
    exploit_record record {};
    record.type = type;
    std::snprintf(record.chtime, sizeof(record.chtime), "%s", timestamp);
    std::snprintf(record.chVictimName, sizeof(record.chVictimName), "%s", victim_name);
    record.iVictimLevel = victim_level;
    record.iKillerLevel = killer_level;
    record.iIntParam = int_param;
    return record;
}

std::string make_valid_exploit_bytes()
{
    std::vector<exploit_record> records;
    records.push_back(make_exploit_record(EXPLOIT_LEVEL, "Mon Jan  1 00:00:00 2024", "level", 10, 0, 20));

    std::string error_message;
    std::string exploit_bytes;
    EXPECT_TRUE(exploits_json::exploit_records_to_binary(records, &exploit_bytes, &error_message)) << error_message;
    return exploit_bytes;
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

std::string write_valid_legacy_player_file(const std::string& root_directory, const char_file_u& stored_character, const std::string& destination_path = "")
{
    ScopedWorkingDirectory working_directory(root_directory);
    player_index_element* previous_player_table = player_table;
    const int previous_top_of_p_table = top_of_p_table;

    player_table = new player_index_element[1] {};
    top_of_p_table = 0;
    player_table[0].name = strdup(stored_character.name);

    player_table[0].level = stored_character.level;
    player_table[0].race = stored_character.race;
    player_table[0].idnum = stored_character.specials2.idnum;
    player_table[0].log_time = stored_character.last_logon;
    player_table[0].flags = stored_character.specials2.act;

    char_data* character = new char_data {};
    clear_char(character, MOB_VOID);

    char_file_u mutable_store = stored_character;
    store_to_char(&mutable_store, character);

    descriptor_data descriptor {};
    std::snprintf(descriptor.pwd, sizeof(descriptor.pwd), "%s", "LegacyPw1");
    std::snprintf(descriptor.host, sizeof(descriptor.host), "%s", "test-host");
    character->desc = &descriptor;

    save_player(character, stored_character.specials2.load_room, 0);
    const std::string generated_path = player_table[0].ch_file;
    const std::string player_text = read_file_contents(generated_path);
    const std::string final_path = destination_path.empty() ? account::legacy_player_file_path(root_directory, stored_character.name) : destination_path;
    write_text_file(final_path, player_text);
    if (generated_path != final_path)
        std::remove(generated_path.c_str());

    free(player_table[0].name);
    delete[] player_table;
    if (previous_player_table != nullptr && previous_top_of_p_table >= 0) {
        player_table = previous_player_table;
        top_of_p_table = previous_top_of_p_table;
    } else {
        player_table = nullptr;
        top_of_p_table = -1;
    }

    return player_text;
}

void make_file_executable(const std::string& path)
{
    ASSERT_EQ(chmod(path.c_str(), 0700), 0)
        << "Expected test helper to mark fixture file executable: " << path;
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

TEST(AccountManagement, RejectsStaleEmailVerificationCodeAfterResend) {
    account::AccountData account_data;
    std::string error_message;
    ASSERT_TRUE(account::initialize_new_account("alpha-admin", "player@example.com", "ValidPass1", 1700001000, &account_data, &error_message)) << error_message;

    std::string original_code;
    ASSERT_TRUE(account::prepare_email_verification_code(&account_data, 1700002000, &original_code, &error_message)) << error_message;

    std::string resent_code;
    ASSERT_TRUE(account::prepare_email_verification_code(&account_data, 1700002010, &resent_code, &error_message)) << error_message;
    EXPECT_NE(original_code, resent_code);

    EXPECT_FALSE(account::confirm_email_verification_code(&account_data, original_code, "email-code", 1700002020, &error_message));
    EXPECT_NE(error_message.find("invalid"), std::string::npos);
    EXPECT_FALSE(account_data.email_verified);

    ASSERT_TRUE(account::confirm_email_verification_code(&account_data, resent_code, "email-code", 1700002021, &error_message)) << error_message;
    EXPECT_TRUE(account_data.email_verified);
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

TEST(AccountManagement, UsesConfiguredSendmailCommandForVerificationEmailDelivery) {
    TemporaryDirectory temp_directory;
    const std::string root = temp_directory.path();
    const std::string capture_path = root + "/captured-mail.txt";
    const std::string command_script_path = root + "/capture-sendmail.sh";
    write_text_file(command_script_path,
        "#!/bin/sh\n"
        "cat > \"" + capture_path + "\"\n");
    make_file_executable(command_script_path);

    ScopedEnvironmentVariable sendmail_override("ROTS_SENDMAIL_COMMAND", command_script_path);

    account::AccountData created_account;
    std::string error_message;
    ASSERT_TRUE(account::create_account(root, "alpha-admin", "player@example.com", "ValidPass1", 1700001000, &created_account, &error_message))
        << error_message;

    account::AccountData pending_account;
    ASSERT_TRUE(account::start_email_verification(root, created_account.account_name, 1700002000, &pending_account, &error_message))
        << error_message;

    const std::string captured_mail = read_file_contents(capture_path);
    EXPECT_NE(captured_mail.find("To: player@example.com"), std::string::npos)
        << "Expected the configured sendmail command to receive the verification message.";
    EXPECT_NE(captured_mail.find("Subject: RotS account verification code"), std::string::npos)
        << "Expected the verification email subject to be written to the configured sendmail command.";
    EXPECT_NE(captured_mail.find("Verification code: "), std::string::npos)
        << "Expected the verification email body to include the generated code.";
}

TEST(AccountManagement, StartingEmailVerificationLeavesVerifiedAccountsVerified) {
    TemporaryDirectory temp_directory;
    std::string error_message;

    account::AccountData account_data;
    ASSERT_TRUE(account::create_account_for_email(temp_directory.path(), "player@example.com", "ValidPass1", 1700002000, &account_data, &error_message)) << error_message;
    ASSERT_TRUE(account::admin_verify_email(temp_directory.path(), account_data.account_name, "VerifierAdmin", 1700002001, &account_data, &error_message)) << error_message;

    const long original_verified_at = account_data.email_verified_at;
    const long original_updated_at = account_data.updated_at;

    account::AccountData verified_account;
    ASSERT_TRUE(account::start_email_verification(temp_directory.path(), account_data.account_name, 1700002010, &verified_account, &error_message)) << error_message;
    EXPECT_TRUE(verified_account.email_verified);
    EXPECT_EQ(verified_account.email_verified_at, original_verified_at);
    EXPECT_EQ(verified_account.updated_at, original_updated_at);
    EXPECT_TRUE(verified_account.verification_code_hash.empty());
    EXPECT_EQ(verified_account.verification_code_sent_at, 0);
    EXPECT_EQ(verified_account.verification_code_expires_at, 0);
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
    EXPECT_EQ(account::account_file_path("/game/lib", " Player@Example.com "),
        "/game/lib/accounts/P-T/player@example.com/account.json");
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
    EXPECT_EQ(parsed_account.characters, std::vector<std::string>({ "aragorn", "legolas" }));
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

TEST(AccountManagement, PopulatesDefaultCharacterLinkFileNamesDuringAccountJsonRoundTrip) {
    account::AccountData original_account = make_account();
    original_account.characters = {"aragorn"};
    original_account.character_links.clear();

    const std::string json = account::serialize_account_to_json(original_account);

    account::AccountData parsed_account;
    std::string error_message;
    ASSERT_TRUE(account::deserialize_account_from_json(json, &parsed_account, &error_message)) << error_message;
    ASSERT_EQ(parsed_account.character_links.size(), 1u);
    EXPECT_EQ(parsed_account.character_links[0].character_name, "aragorn");
    EXPECT_EQ(parsed_account.character_links[0].character_path, "aragorn.character.json");
    EXPECT_EQ(parsed_account.character_links[0].object_path, "aragorn.objects.json");
    EXPECT_EQ(parsed_account.character_links[0].exploits_path, "aragorn.exploits.json");
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

TEST(AccountManagement, RejectsUnsafeCharacterNamesInStoredAccountJson) {
    const std::string json = "{\n"
                             "  \"version\": 1,\n"
                             "  \"account_name\": \"alpha-admin\",\n"
                             "  \"normalized_email\": \"player@example.com\",\n"
                             "  \"password_hash\": \"hash\",\n"
                             "  \"password_salt\": \"salt\",\n"
                             "  \"characters\": [\"../escape\"],\n"
                             "  \"character_links\": [],\n"
                             "  \"email_verified\": true,\n"
                             "  \"email_verified_by\": \"Verifier\",\n"
                             "  \"email_verified_at\": 1,\n"
                             "  \"verification_code_hash\": \"\",\n"
                             "  \"verification_code_sent_at\": 0,\n"
                             "  \"verification_code_expires_at\": 0,\n"
                             "  \"verification_attempt_count\": 0,\n"
                             "  \"verification_last_attempt_at\": 0,\n"
                             "  \"blocked\": false,\n"
                             "  \"block_reason\": \"\",\n"
                             "  \"blocked_by\": \"\",\n"
                             "  \"blocked_at\": 0,\n"
                             "  \"created_at\": 1,\n"
                             "  \"updated_at\": 1,\n"
                             "  \"password_reset_at\": 0,\n"
                             "  \"password_reset_by\": \"\"\n"
                             "}\n";

    account::AccountData parsed_account;
    std::string error_message;
    EXPECT_FALSE(account::deserialize_account_from_json(json, &parsed_account, &error_message));
    EXPECT_NE(error_message.find("Character name"), std::string::npos);
}

TEST(AccountManagement, PersistsAccountsToBucketedJsonFilesAndReadsThemBack) {
    TemporaryDirectory temp_directory;
    const account::AccountData original_account = make_account();
    std::string error_message;

    ASSERT_TRUE(account::write_account_file(temp_directory.path(), original_account, &error_message)) << error_message;

    account::AccountData loaded_account;
    ASSERT_TRUE(account::read_account_file(temp_directory.path(), "ALPHA-ADMIN", &loaded_account, &error_message)) << error_message;

    EXPECT_EQ(loaded_account.account_name, "alpha-admin");
    EXPECT_EQ(loaded_account.characters, std::vector<std::string>({ "aragorn", "legolas" }));
    EXPECT_EQ(loaded_account.block_reason, original_account.block_reason);

    struct stat file_info {};
    ASSERT_EQ(stat(account::account_file_path(temp_directory.path(), original_account.normalized_email).c_str(), &file_info), 0)
        << "Expected write_account_file() to materialize the final account JSON file.";

    EXPECT_NE(stat((account::account_file_path(temp_directory.path(), original_account.normalized_email) + ".tmp").c_str(), &file_info), 0)
        << "Expected temporary files to be cleaned up after a successful atomic write.";
}

TEST(AccountManagement, RewritesLegacyFlatAccountFilesIntoEmailRootedLayout) {
    TemporaryDirectory temp_directory;
    ASSERT_EQ(mkdir((temp_directory.path() + "/accounts").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/accounts/A-E").c_str(), 0700), 0);

    account::AccountData original_account = make_account();
    const std::string legacy_path = temp_directory.path() + "/accounts/A-E/alpha-admin.json";
    write_text_file(legacy_path, account::serialize_account_to_json(original_account));

    account::AccountData loaded_account;
    std::string error_message;
    ASSERT_TRUE(account::read_account_file(temp_directory.path(), "alpha-admin", &loaded_account, &error_message)) << error_message;
    loaded_account.block_reason = "updated reason";
    ASSERT_TRUE(account::write_account_file(temp_directory.path(), loaded_account, &error_message)) << error_message;

    struct stat file_info {};
    EXPECT_EQ(stat(account::account_file_path(temp_directory.path(), original_account.normalized_email).c_str(), &file_info), 0);
    EXPECT_NE(stat(legacy_path.c_str(), &file_info), 0);

    account::AccountData looked_up_account;
    ASSERT_TRUE(account::read_account_file_by_email(temp_directory.path(), "player@example.com", &looked_up_account, &error_message)) << error_message;
    EXPECT_EQ(looked_up_account.block_reason, "updated reason");
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
    const std::string duplicate_directory = temp_directory.path() + "/accounts/ZZZ/manual-duplicate";
    ASSERT_EQ(mkdir((temp_directory.path() + "/accounts/ZZZ").c_str(), 0700), 0);
    ASSERT_EQ(mkdir(duplicate_directory.c_str(), 0700), 0);
    write_text_file(duplicate_directory + "/account.json", account::serialize_account_to_json(duplicate_account));

    account::AccountData looked_up_account;
    EXPECT_FALSE(account::read_account_file_by_email(temp_directory.path(), "player@example.com", &looked_up_account, &error_message));
    EXPECT_NE(error_message.find("Multiple account records"), std::string::npos);
}

TEST(AccountManagement, ReadAccountFileByEmailRejectsConflictingLegacyAndRootedDuplicateRecords) {
    TemporaryDirectory temp_directory;
    std::string error_message;

    account::AccountData rooted_account;
    ASSERT_TRUE(account::create_account_for_email(temp_directory.path(), "player@example.com", "ValidPass1", 1700005702, &rooted_account, &error_message)) << error_message;

    account::AccountData legacy_duplicate = rooted_account;
    legacy_duplicate.account_name = "manual-legacy";
    write_text_file(temp_directory.path() + "/accounts/P-T/manual-legacy.json", account::serialize_account_to_json(legacy_duplicate));

    account::AccountData looked_up_account;
    EXPECT_FALSE(account::read_account_file_by_email(temp_directory.path(), "player@example.com", &looked_up_account, &error_message));
    EXPECT_NE(error_message.find("Multiple account records"), std::string::npos);
}

TEST(AccountManagement, CreatesAccountForEmailFailsWhenLegacyFlatRecordAlreadyExistsForSameNormalizedEmail) {
    TemporaryDirectory temp_directory;
    std::string error_message;

    ASSERT_EQ(mkdir((temp_directory.path() + "/accounts").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/accounts/A-E").c_str(), 0700), 0);
    account::AccountData legacy_account = make_account();
    write_text_file(temp_directory.path() + "/accounts/A-E/alpha-admin.json", account::serialize_account_to_json(legacy_account));

    account::AccountData created_account;
    EXPECT_FALSE(account::create_account_for_email(temp_directory.path(), "Player@example.com", "ValidPass1", 1700005703, &created_account, &error_message));
    EXPECT_NE(error_message.find("already exists"), std::string::npos);
}

TEST(AccountManagement, RefusesToOverwriteDifferentAccountAtTargetEmailPath) {
    TemporaryDirectory temp_directory;
    std::string error_message;
    ASSERT_TRUE(account::create_account(temp_directory.path(), "alpha-admin", "player@example.com", "ValidPass1", 1700005700, nullptr, &error_message)) << error_message;
    ASSERT_TRUE(account::create_account(temp_directory.path(), "beta-admin", "other@example.com", "ValidPass1", 1700005701, nullptr, &error_message)) << error_message;

    account::AccountData duplicate_account;
    ASSERT_TRUE(account::read_account_file(temp_directory.path(), "beta-admin", &duplicate_account, &error_message)) << error_message;
    duplicate_account.normalized_email = "player@example.com";

    EXPECT_FALSE(account::write_account_file(temp_directory.path(), duplicate_account, &error_message));
    EXPECT_NE(error_message.find("occupied by a different account"), std::string::npos);
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
    write_valid_legacy_player_file(temp_directory.path(), make_stored_character("aragorn"));

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
    EXPECT_FALSE(error_message.empty());

    account::AccountData account_data;
    ASSERT_TRUE(account::read_account_file(temp_directory.path(), "alpha-admin", &account_data, &error_message)) << error_message;
    EXPECT_FALSE(account::account_has_character(account_data, "aragorn"));
}

TEST(AccountManagement, BuildsAccountLinkedCharacterSnapshotPaths) {
    EXPECT_EQ(account::account_character_directory("/game/lib", "player@example.com", "aragorn"),
        "/game/lib/accounts/P-T/player@example.com");
    EXPECT_EQ(account::account_character_snapshot_path("/game/lib", "player@example.com", "aragorn"),
        "/game/lib/accounts/P-T/player@example.com/aragorn.migration.json");
    EXPECT_EQ(account::account_character_player_path("/game/lib", "player@example.com", "aragorn"),
        "/game/lib/accounts/P-T/player@example.com/aragorn.character.json");
    EXPECT_EQ(account::account_character_object_path("/game/lib", "player@example.com", "aragorn"),
        "/game/lib/accounts/P-T/player@example.com/aragorn.objects.json");
    EXPECT_EQ(account::account_character_exploits_path("/game/lib", "player@example.com", "aragorn"),
        "/game/lib/accounts/P-T/player@example.com/aragorn.exploits.json");
}

TEST(AccountManagement, WritesAndReadsAccountNativeCharacterFile) {
    TemporaryDirectory temp_directory;
    std::string error_message;

    ASSERT_TRUE(account::create_account(temp_directory.path(), "alpha-admin", "player@example.com", "ValidPass1", 1700007776, nullptr, &error_message)) << error_message;
    ASSERT_TRUE(account::admin_link_character(temp_directory.path(), "alpha-admin", "aragorn", 1700007777, nullptr, &error_message)) << error_message;

    const char_file_u original = make_stored_character("aragorn");
    ASSERT_TRUE(account::write_account_character_file(temp_directory.path(), "alpha-admin", original, &error_message)) << error_message;

    char_file_u loaded {};
    ASSERT_TRUE(account::read_account_character_file(temp_directory.path(), "alpha-admin", "aragorn", &loaded, &error_message)) << error_message;
    EXPECT_STREQ(loaded.name, "aragorn");
    EXPECT_STREQ(loaded.title, "the Ranger");
    EXPECT_EQ(loaded.level, original.level);
    EXPECT_EQ(loaded.specials2.idnum, original.specials2.idnum);
    EXPECT_EQ(loaded.specials2.load_room, original.specials2.load_room);
    EXPECT_EQ(loaded.points.gold, original.points.gold);
    EXPECT_EQ(loaded.skills[5], original.skills[5]);
    EXPECT_EQ(loaded.talks[2], original.talks[2]);
    EXPECT_EQ(loaded.profs.prof_level[PROF_WARRIOR], original.profs.prof_level[PROF_WARRIOR]);
    EXPECT_EQ(loaded.profs.prof_coof[PROF_WARRIOR], original.profs.prof_coof[PROF_WARRIOR]);
}

TEST(AccountManagement, WritesAndReadsAccountNativeObjectFile) {
    TemporaryDirectory temp_directory;
    std::string error_message;

    ASSERT_TRUE(account::create_account(temp_directory.path(), "alpha-admin", "player@example.com", "ValidPass1", 1700007776, nullptr, &error_message)) << error_message;
    ASSERT_TRUE(account::admin_link_character(temp_directory.path(), "alpha-admin", "aragorn", 1700007777, nullptr, &error_message)) << error_message;

    objects_json::ObjectSaveData object_data;
    object_data.rent.rentcode = RENT_CRASH;
    object_data.objects.push_back(objects_json::ObjectRecord {});
    object_data.objects[0].item_number = 1234;
    object_data.objects[0].wear_pos = WEAR_HEAD;
    object_data.aliases.push_back({ "assist", "kill orc" });

    std::string object_bytes;
    ASSERT_TRUE(objects_json::object_save_data_to_binary(object_data, &object_bytes, &error_message)) << error_message;
    ASSERT_TRUE(account::write_account_object_file(temp_directory.path(), "alpha-admin", "aragorn", object_bytes, &error_message)) << error_message;
    ASSERT_TRUE(account::account_object_file_exists(temp_directory.path(), "alpha-admin", "aragorn", &error_message));

    std::string loaded_bytes;
    ASSERT_TRUE(account::read_account_object_file(temp_directory.path(), "alpha-admin", "aragorn", &loaded_bytes, &error_message)) << error_message;

    objects_json::ObjectSaveData loaded;
    ASSERT_TRUE(objects_json::object_save_data_from_binary(loaded_bytes, &loaded, &error_message)) << error_message;
    ASSERT_EQ(loaded.objects.size(), 1u);
    EXPECT_EQ(loaded.objects[0].item_number, 1234);
    ASSERT_EQ(loaded.aliases.size(), 1u);
    EXPECT_EQ(loaded.aliases[0].keyword, "assist");
}

TEST(AccountManagement, WritesDefaultAccountNativeObjectFile) {
    TemporaryDirectory temp_directory;
    std::string error_message;

    ASSERT_TRUE(account::create_account(temp_directory.path(), "alpha-admin", "player@example.com", "ValidPass1", 1700007776, nullptr, &error_message)) << error_message;
    ASSERT_TRUE(account::admin_link_character(temp_directory.path(), "alpha-admin", "aragorn", 1700007777, nullptr, &error_message)) << error_message;
    ASSERT_TRUE(account::write_default_account_object_file(temp_directory.path(), "alpha-admin", "aragorn", &error_message)) << error_message;
    ASSERT_TRUE(account::account_object_file_exists(temp_directory.path(), "alpha-admin", "aragorn", &error_message));

    std::string loaded_bytes;
    ASSERT_TRUE(account::read_account_object_file(temp_directory.path(), "alpha-admin", "aragorn", &loaded_bytes, &error_message)) << error_message;

    objects_json::ObjectSaveData loaded;
    ASSERT_TRUE(objects_json::object_save_data_from_binary(loaded_bytes, &loaded, &error_message)) << error_message;
    EXPECT_TRUE(loaded.objects.empty());
    EXPECT_TRUE(loaded.aliases.empty());
    EXPECT_TRUE(loaded.followers.empty());
    EXPECT_EQ(loaded.rent.rentcode, 0);
}

TEST(AccountManagement, WritesAndReadsAccountNativeExploitFile) {
    TemporaryDirectory temp_directory;
    std::string error_message;

    ASSERT_TRUE(account::create_account(temp_directory.path(), "alpha-admin", "player@example.com", "ValidPass1", 1700007776, nullptr, &error_message)) << error_message;
    ASSERT_TRUE(account::admin_link_character(temp_directory.path(), "alpha-admin", "aragorn", 1700007777, nullptr, &error_message)) << error_message;

    std::vector<exploit_record> records;
    records.push_back(make_exploit_record(EXPLOIT_LEVEL, "Mon Jan  1 00:00:00 2024", "level", 10, 0, 20));
    records.push_back(make_exploit_record(EXPLOIT_ACHIEVEMENT, "Tue Jan  2 00:00:00 2024", "Won a battle", 11, 0, 0));

    ASSERT_TRUE(account::write_account_exploit_file(temp_directory.path(), "alpha-admin", "aragorn", records, &error_message)) << error_message;
    ASSERT_TRUE(account::account_exploit_file_exists(temp_directory.path(), "alpha-admin", "aragorn", &error_message));

    std::vector<exploit_record> loaded_records;
    ASSERT_TRUE(account::read_account_exploit_file(temp_directory.path(), "alpha-admin", "aragorn", &loaded_records, &error_message)) << error_message;
    ASSERT_EQ(loaded_records.size(), 2u);
    EXPECT_EQ(loaded_records[0].type, EXPLOIT_LEVEL);
    EXPECT_STREQ(loaded_records[1].chVictimName, "Won a battle");
}

TEST(AccountManagement, WritesDefaultAccountNativeExploitFile) {
    TemporaryDirectory temp_directory;
    std::string error_message;

    ASSERT_TRUE(account::create_account(temp_directory.path(), "alpha-admin", "player@example.com", "ValidPass1", 1700007776, nullptr, &error_message)) << error_message;
    ASSERT_TRUE(account::admin_link_character(temp_directory.path(), "alpha-admin", "aragorn", 1700007777, nullptr, &error_message)) << error_message;
    ASSERT_TRUE(account::write_default_account_exploit_file(temp_directory.path(), "alpha-admin", "aragorn", &error_message)) << error_message;
    ASSERT_TRUE(account::account_exploit_file_exists(temp_directory.path(), "alpha-admin", "aragorn", &error_message));

    std::vector<exploit_record> loaded_records;
    ASSERT_TRUE(account::read_account_exploit_file(temp_directory.path(), "alpha-admin", "aragorn", &loaded_records, &error_message)) << error_message;
    EXPECT_TRUE(loaded_records.empty());
}

TEST(AccountManagement, RejectsStoredObjectPathThatDoesNotMatchExpectedCharacterFileName) {
    TemporaryDirectory temp_directory;
    std::string error_message;

    ASSERT_TRUE(account::create_account(temp_directory.path(), "alpha-admin", "player@example.com", "ValidPass1", 1700007776, nullptr, &error_message)) << error_message;
    ASSERT_TRUE(account::admin_link_character(temp_directory.path(), "alpha-admin", "aragorn", 1700007777, nullptr, &error_message)) << error_message;

    const std::string account_path = temp_directory.path() + "/accounts/P-T/player@example.com/account.json";
    std::string account_json = read_file_contents(account_path);
    const std::string original_fragment = "\"object_path\": \"aragorn.objects.json\"";
    const std::string malicious_fragment = "\"object_path\": \"../outside.objects.json\"";
    ASSERT_NE(account_json.find(original_fragment), std::string::npos);
    account_json.replace(account_json.find(original_fragment), original_fragment.size(), malicious_fragment);
    write_text_file(account_path, account_json);

    std::string object_bytes;
    EXPECT_FALSE(account::read_account_object_file(temp_directory.path(), "alpha-admin", "aragorn", &object_bytes, &error_message));
    EXPECT_NE(error_message.find("expected account-owned object filename"), std::string::npos);
}

TEST(AccountManagement, RejectsStoredObjectPathWithAbsolutePath) {
    TemporaryDirectory temp_directory;
    std::string error_message;

    ASSERT_TRUE(account::create_account(temp_directory.path(), "alpha-admin", "player@example.com", "ValidPass1", 1700007776, nullptr, &error_message)) << error_message;
    ASSERT_TRUE(account::admin_link_character(temp_directory.path(), "alpha-admin", "aragorn", 1700007777, nullptr, &error_message)) << error_message;

    const std::string account_path = temp_directory.path() + "/accounts/P-T/player@example.com/account.json";
    std::string account_json = read_file_contents(account_path);
    const std::string original_fragment = "\"object_path\": \"aragorn.objects.json\"";
    const std::string malicious_fragment = "\"object_path\": \"/tmp/aragorn.objects.json\"";
    ASSERT_NE(account_json.find(original_fragment), std::string::npos);
    account_json.replace(account_json.find(original_fragment), original_fragment.size(), malicious_fragment);
    write_text_file(account_path, account_json);

    std::string object_bytes;
    EXPECT_FALSE(account::read_account_object_file(temp_directory.path(), "alpha-admin", "aragorn", &object_bytes, &error_message));
    EXPECT_NE(error_message.find("expected account-owned object filename"), std::string::npos);
}

TEST(AccountManagement, WritesCanonicalObjectPathWhenSafeLegacyRelativePathIsStored) {
    TemporaryDirectory temp_directory;
    std::string error_message;

    ASSERT_TRUE(account::create_account(temp_directory.path(), "alpha-admin", "player@example.com", "ValidPass1", 1700007776, nullptr, &error_message)) << error_message;
    ASSERT_TRUE(account::admin_link_character(temp_directory.path(), "alpha-admin", "aragorn", 1700007777, nullptr, &error_message)) << error_message;

    const std::string account_path = temp_directory.path() + "/accounts/P-T/player@example.com/account.json";
    std::string account_json = read_file_contents(account_path);
    const std::string original_fragment = "\"object_path\": \"aragorn.objects.json\"";
    const std::string legacy_fragment = "\"object_path\": \"legacy/aragorn.objects.json\"";
    ASSERT_NE(account_json.find(original_fragment), std::string::npos);
    account_json.replace(account_json.find(original_fragment), original_fragment.size(), legacy_fragment);
    write_text_file(account_path, account_json);

    ASSERT_EQ(mkdir((temp_directory.path() + "/accounts/P-T/player@example.com/legacy").c_str(), 0700), 0);

    objects_json::ObjectSaveData object_data;
    object_data.rent.rentcode = RENT_CRASH;
    object_data.objects.push_back(objects_json::ObjectRecord {});
    object_data.objects[0].item_number = 1234;

    std::string object_bytes;
    ASSERT_TRUE(objects_json::object_save_data_to_binary(object_data, &object_bytes, &error_message)) << error_message;
    ASSERT_TRUE(account::write_account_object_file(temp_directory.path(), "alpha-admin", "aragorn", object_bytes, &error_message)) << error_message;

    const std::string updated_account_json = read_file_contents(account_path);
    EXPECT_NE(updated_account_json.find(original_fragment), std::string::npos);
    EXPECT_EQ(updated_account_json.find(legacy_fragment), std::string::npos);
}

TEST(AccountManagement, LeavesStoredObjectPathUnchangedWhenCanonicalObjectWriteFails) {
    TemporaryDirectory temp_directory;
    std::string error_message;

    ASSERT_TRUE(account::create_account(temp_directory.path(), "alpha-admin", "player@example.com", "ValidPass1", 1700007776, nullptr, &error_message)) << error_message;
    ASSERT_TRUE(account::admin_link_character(temp_directory.path(), "alpha-admin", "aragorn", 1700007777, nullptr, &error_message)) << error_message;

    const std::string account_path = temp_directory.path() + "/accounts/P-T/player@example.com/account.json";
    std::string account_json = read_file_contents(account_path);
    const std::string original_fragment = "\"object_path\": \"aragorn.objects.json\"";
    const std::string legacy_fragment = "\"object_path\": \"legacy/aragorn.objects.json\"";
    ASSERT_NE(account_json.find(original_fragment), std::string::npos);
    account_json.replace(account_json.find(original_fragment), original_fragment.size(), legacy_fragment);
    write_text_file(account_path, account_json);

    const std::string account_directory = temp_directory.path() + "/accounts/P-T/player@example.com";
    ASSERT_EQ(mkdir((account_directory + "/legacy").c_str(), 0700), 0);
    ASSERT_EQ(chmod(account_directory.c_str(), 0500), 0);

    objects_json::ObjectSaveData object_data;
    object_data.rent.rentcode = RENT_CRASH;
    object_data.objects.push_back(objects_json::ObjectRecord {});
    object_data.objects[0].item_number = 1234;

    std::string object_bytes;
    ASSERT_TRUE(objects_json::object_save_data_to_binary(object_data, &object_bytes, &error_message)) << error_message;
    EXPECT_FALSE(account::write_account_object_file(temp_directory.path(), "alpha-admin", "aragorn", object_bytes, &error_message));

    ASSERT_EQ(chmod(account_directory.c_str(), 0700), 0);

    const std::string updated_account_json = read_file_contents(account_path);
    EXPECT_NE(updated_account_json.find(legacy_fragment), std::string::npos);
    EXPECT_EQ(updated_account_json.find(original_fragment), std::string::npos);
}

TEST(AccountManagement, RejectsStoredCharacterPathThatDoesNotMatchExpectedCharacterFileName) {
    TemporaryDirectory temp_directory;
    std::string error_message;

    ASSERT_TRUE(account::create_account(temp_directory.path(), "alpha-admin", "player@example.com", "ValidPass1", 1700007776, nullptr, &error_message)) << error_message;
    ASSERT_TRUE(account::admin_link_character(temp_directory.path(), "alpha-admin", "aragorn", 1700007777, nullptr, &error_message)) << error_message;

    const std::string account_path = temp_directory.path() + "/accounts/P-T/player@example.com/account.json";
    std::string account_json = read_file_contents(account_path);
    const std::string original_fragment = "\"character_path\": \"aragorn.character.json\"";
    const std::string malicious_fragment = "\"character_path\": \"../outside.character.json\"";
    ASSERT_NE(account_json.find(original_fragment), std::string::npos);
    account_json.replace(account_json.find(original_fragment), original_fragment.size(), malicious_fragment);
    write_text_file(account_path, account_json);

    char_file_u stored_character = make_stored_character("aragorn");
    EXPECT_FALSE(account::write_account_character_file(temp_directory.path(), "alpha-admin", stored_character, &error_message));
    EXPECT_NE(error_message.find("expected account-owned character filename"), std::string::npos);
}

TEST(AccountManagement, RejectsStoredExploitsPathThatDoesNotMatchExpectedCharacterFileName) {
    TemporaryDirectory temp_directory;
    std::string error_message;

    ASSERT_TRUE(account::create_account(temp_directory.path(), "alpha-admin", "player@example.com", "ValidPass1", 1700007776, nullptr, &error_message)) << error_message;
    ASSERT_TRUE(account::admin_link_character(temp_directory.path(), "alpha-admin", "aragorn", 1700007777, nullptr, &error_message)) << error_message;

    const std::string account_path = temp_directory.path() + "/accounts/P-T/player@example.com/account.json";
    std::string account_json = read_file_contents(account_path);
    const std::string original_fragment = "\"exploits_path\": \"aragorn.exploits.json\"";
    const std::string malicious_fragment = "\"exploits_path\": \"../outside.exploits.json\"";
    ASSERT_NE(account_json.find(original_fragment), std::string::npos);
    account_json.replace(account_json.find(original_fragment), original_fragment.size(), malicious_fragment);
    write_text_file(account_path, account_json);

    std::vector<exploit_record> records;
    records.push_back(make_exploit_record(EXPLOIT_LEVEL, "Mon Jan  1 00:00:00 2024", "level", 10, 0, 20));
    EXPECT_FALSE(account::write_account_exploit_file(temp_directory.path(), "alpha-admin", "aragorn", records, &error_message));
    EXPECT_NE(error_message.find("expected account-owned exploits filename"), std::string::npos);
}

TEST(AccountManagement, RemovesAccountNativeCharacterFile) {
    TemporaryDirectory temp_directory;
    std::string error_message;

    ASSERT_TRUE(account::create_account(temp_directory.path(), "alpha-admin", "player@example.com", "ValidPass1", 1700007776, nullptr, &error_message)) << error_message;
    ASSERT_TRUE(account::admin_link_character(temp_directory.path(), "alpha-admin", "aragorn", 1700007777, nullptr, &error_message)) << error_message;
    ASSERT_TRUE(account::write_account_character_file(temp_directory.path(), "alpha-admin", make_stored_character("aragorn"), &error_message)) << error_message;
    ASSERT_TRUE(account::account_character_file_exists(temp_directory.path(), "alpha-admin", "aragorn", &error_message));

    ASSERT_TRUE(account::remove_account_character_file(temp_directory.path(), "alpha-admin", "aragorn", &error_message)) << error_message;
    EXPECT_FALSE(account::account_character_file_exists(temp_directory.path(), "alpha-admin", "aragorn", &error_message));
}

TEST(AccountManagement, MigratesLegacyCharacterFilesIntoAccountLinkedSnapshotJson) {
    TemporaryDirectory temp_directory;
    std::string error_message;
    ASSERT_TRUE(account::create_account(temp_directory.path(), "alpha-admin", "player@example.com", "ValidPass1", 1700007776, nullptr, &error_message)) << error_message;

    account::CharacterMigrationData migration;
    ASSERT_EQ(mkdir((temp_directory.path() + "/players").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/players/A-E").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/plrobjs").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/plrobjs/A-E").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/exploits").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/exploits/A-E").c_str(), 0700), 0);

    write_valid_legacy_player_file(temp_directory.path(), make_stored_character("aragorn"));
    write_text_file(account::legacy_object_file_path(temp_directory.path(), "aragorn"), make_valid_object_bytes());
    write_text_file(account::legacy_exploits_file_path(temp_directory.path(), "aragorn"), make_valid_exploit_bytes());

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
    ASSERT_TRUE(account::create_account(temp_directory.path(), "alpha-admin", "player@example.com", "ValidPass1", 1700008887, nullptr, &error_message)) << error_message;

    EXPECT_FALSE(account::migrate_legacy_character_by_name(temp_directory.path(), "alpha-admin", "aragorn", 1700008888, &migration, &error_message));
    EXPECT_FALSE(error_message.empty());
}

TEST(AccountManagement, MigratesLegacyCharacterByDefaultFileLayout) {
    TemporaryDirectory temp_directory;
    std::string error_message;
    ASSERT_TRUE(account::create_account(temp_directory.path(), "alpha-admin", "player@example.com", "ValidPass1", 1700009998, nullptr, &error_message)) << error_message;
    ASSERT_EQ(mkdir((temp_directory.path() + "/players").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/players/A-E").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/plrobjs").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/plrobjs/A-E").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/exploits").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/exploits/A-E").c_str(), 0700), 0);

    write_valid_legacy_player_file(temp_directory.path(), make_stored_character("aragorn"));
    write_text_file(account::legacy_object_file_path(temp_directory.path(), "aragorn"), make_valid_object_bytes());
    write_text_file(account::legacy_exploits_file_path(temp_directory.path(), "aragorn"), make_valid_exploit_bytes());

    account::CharacterMigrationData migration;
    ASSERT_TRUE(account::migrate_legacy_character_by_name(temp_directory.path(), "alpha-admin", "aragorn", 1700009999, &migration, &error_message)) << error_message;
    EXPECT_EQ(migration.account_name, "alpha-admin");
    EXPECT_EQ(migration.character_name, "aragorn");
}

TEST(AccountManagement, MigratesVersionedLegacyPlayerFilesForFreshCharacters) {
    TemporaryDirectory temp_directory;
    std::string error_message;
    ASSERT_TRUE(account::create_account(temp_directory.path(), "alpha-admin", "player@example.com", "ValidPass1", 1700010000, nullptr, &error_message)) << error_message;
    ASSERT_EQ(mkdir((temp_directory.path() + "/players").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/players/A-E").c_str(), 0700), 0);

    const std::string versioned_player_path = account::legacy_player_file_path(temp_directory.path(), "aragorn") + ".1.1.1234.1700010000.0";
    char_file_u stored_character = make_stored_character("aragorn");
    stored_character.specials2.idnum = 1234;
    const std::string expected_player_text = write_valid_legacy_player_file(temp_directory.path(), stored_character, versioned_player_path);

    account::CharacterMigrationData migration;
    ASSERT_TRUE(account::migrate_legacy_character_by_name(temp_directory.path(), "alpha-admin", "aragorn", 1700010001, &migration, &error_message)) << error_message;
    EXPECT_TRUE(migration.player_file.present);
    EXPECT_EQ(migration.player_file.source_path, versioned_player_path);
    std::string decoded_player_text;
    ASSERT_TRUE(account::decode_snapshot_content(migration.player_file, &decoded_player_text, &error_message)) << error_message;
    EXPECT_EQ(decoded_player_text, expected_player_text);
}

TEST(AccountManagement, PrefersVersionedLegacyPlayerFilesOverStaleFlatFilesDuringMigration) {
    TemporaryDirectory temp_directory;
    std::string error_message;
    ASSERT_TRUE(account::create_account(temp_directory.path(), "alpha-admin", "player@example.com", "ValidPass1", 1700010000, nullptr, &error_message)) << error_message;
    ASSERT_EQ(mkdir((temp_directory.path() + "/players").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/players/A-E").c_str(), 0700), 0);

    char_file_u stale_flat_character = make_stored_character("aragorn");
    stale_flat_character.points.gold = 111;
    stale_flat_character.specials2.idnum = 1111;
    write_valid_legacy_player_file(temp_directory.path(), stale_flat_character);

    char_file_u versioned_character = make_stored_character("aragorn");
    versioned_character.points.gold = 9999;
    versioned_character.specials2.idnum = 2222;
    const std::string versioned_player_path = account::legacy_player_file_path(temp_directory.path(), "aragorn") + ".1.1.2222.1700010000.0";
    const std::string expected_player_text = write_valid_legacy_player_file(temp_directory.path(), versioned_character, versioned_player_path);

    account::CharacterMigrationData migration;
    ASSERT_TRUE(account::migrate_legacy_character_by_name(temp_directory.path(), "alpha-admin", "aragorn", 1700010002, &migration, &error_message)) << error_message;
    EXPECT_EQ(migration.player_file.source_path, versioned_player_path);

    std::string decoded_player_text;
    ASSERT_TRUE(account::decode_snapshot_content(migration.player_file, &decoded_player_text, &error_message)) << error_message;
    EXPECT_EQ(decoded_player_text, expected_player_text);

    char_file_u restored_character {};
    ASSERT_TRUE(account::read_account_character_file(temp_directory.path(), "alpha-admin", "aragorn", &restored_character, &error_message)) << error_message;
    EXPECT_STREQ(restored_character.name, "aragorn");
    EXPECT_EQ(restored_character.points.gold, versioned_character.points.gold);
    EXPECT_EQ(restored_character.specials2.idnum, versioned_character.specials2.idnum);

    struct stat file_info {};
    EXPECT_NE(stat(account::legacy_player_file_path(temp_directory.path(), "aragorn").c_str(), &file_info), 0)
        << "Expected migration to retire the stale flat legacy player file once the versioned save won precedence.";
}

TEST(AccountManagement, MigratesVersionedLegacyPlayerFileEvenWhenStaleFlatFileIsUnreadable) {
    TemporaryDirectory temp_directory;
    std::string error_message;
    ASSERT_TRUE(account::create_account(temp_directory.path(), "alpha-admin", "player@example.com", "ValidPass1", 1700010000, nullptr, &error_message)) << error_message;
    ASSERT_EQ(mkdir((temp_directory.path() + "/players").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/players/A-E").c_str(), 0700), 0);

    char_file_u stale_flat_character = make_stored_character("aragorn");
    stale_flat_character.points.gold = 111;
    write_valid_legacy_player_file(temp_directory.path(), stale_flat_character);
    ASSERT_EQ(chmod(account::legacy_player_file_path(temp_directory.path(), "aragorn").c_str(), 0000), 0);

    char_file_u versioned_character = make_stored_character("aragorn");
    versioned_character.points.gold = 9999;
    versioned_character.specials2.idnum = 2222;
    const std::string versioned_player_path = account::legacy_player_file_path(temp_directory.path(), "aragorn") + ".1.1.2222.1700010000.0";
    ASSERT_FALSE(write_valid_legacy_player_file(temp_directory.path(), versioned_character, versioned_player_path).empty());

    account::CharacterMigrationData migration;
    ASSERT_TRUE(account::migrate_legacy_character_by_name(temp_directory.path(), "alpha-admin", "aragorn", 1700010002, &migration, &error_message)) << error_message;
    EXPECT_EQ(migration.player_file.source_path, versioned_player_path);

    char_file_u restored_character {};
    ASSERT_TRUE(account::read_account_character_file(temp_directory.path(), "alpha-admin", "aragorn", &restored_character, &error_message)) << error_message;
    EXPECT_EQ(restored_character.points.gold, versioned_character.points.gold);

    struct stat file_info {};
    EXPECT_NE(stat(account::legacy_player_file_path(temp_directory.path(), "aragorn").c_str(), &file_info), 0);
}

TEST(AccountManagement, RejectsAmbiguousVersionedLegacyPlayerFilesDuringMigration) {
    TemporaryDirectory temp_directory;
    std::string error_message;
    ASSERT_TRUE(account::create_account(temp_directory.path(), "alpha-admin", "player@example.com", "ValidPass1", 1700010001, nullptr, &error_message)) << error_message;
    ASSERT_EQ(mkdir((temp_directory.path() + "/players").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/players/A-E").c_str(), 0700), 0);

    char_file_u stored_character = make_stored_character("aragorn");
    stored_character.specials2.idnum = 1234;
    const std::string valid_player_text = write_valid_legacy_player_file(
        temp_directory.path(),
        stored_character,
        account::legacy_player_file_path(temp_directory.path(), "aragorn") + ".1.1.1234.1700010000.0");
    write_text_file(account::legacy_player_file_path(temp_directory.path(), "aragorn") + ".2.1.1234.1700010001.0", valid_player_text);

    account::CharacterMigrationData migration;
    EXPECT_FALSE(account::migrate_legacy_character_by_name(temp_directory.path(), "alpha-admin", "aragorn", 1700010002, &migration, &error_message));
    EXPECT_NE(error_message.find("Multiple versioned legacy player files matched"), std::string::npos);
}

TEST(AccountManagement, IgnoresStrayNonVersionedPlayerArtifactsWhenResolvingFreshCharacterSaves) {
    TemporaryDirectory temp_directory;
    std::string error_message;
    ASSERT_TRUE(account::create_account(temp_directory.path(), "alpha-admin", "player@example.com", "ValidPass1", 1700010002, nullptr, &error_message)) << error_message;
    ASSERT_EQ(mkdir((temp_directory.path() + "/players").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/players/A-E").c_str(), 0700), 0);

    write_text_file(account::legacy_player_file_path(temp_directory.path(), "aragorn") + ".tmp", "stray-temp-data");
    const std::string versioned_player_path = account::legacy_player_file_path(temp_directory.path(), "aragorn") + ".1.1.1234.1700010000.0";
    char_file_u stored_character = make_stored_character("aragorn");
    stored_character.specials2.idnum = 1234;
    write_valid_legacy_player_file(temp_directory.path(), stored_character, versioned_player_path);

    account::CharacterMigrationData migration;
    ASSERT_TRUE(account::migrate_legacy_character_by_name(temp_directory.path(), "alpha-admin", "aragorn", 1700010003, &migration, &error_message)) << error_message;
    EXPECT_EQ(migration.player_file.source_path, versioned_player_path);
}

TEST(AccountManagement, TreatsMissingOptionalLegacyFilesAsAbsentSnapshots) {
    TemporaryDirectory temp_directory;
    std::string error_message;
    ASSERT_TRUE(account::create_account(temp_directory.path(), "alpha-admin", "player@example.com", "ValidPass1", 1700010000, nullptr, &error_message)) << error_message;
    ASSERT_EQ(mkdir((temp_directory.path() + "/players").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/players/A-E").c_str(), 0700), 0);

    write_valid_legacy_player_file(temp_directory.path(), make_stored_character("aragorn"));

    account::CharacterMigrationData migration;
    ASSERT_TRUE(account::migrate_legacy_character_by_name(temp_directory.path(), "alpha-admin", "aragorn", 1700010000, &migration, &error_message)) << error_message;

    EXPECT_TRUE(migration.player_file.present);
    EXPECT_FALSE(migration.object_file.present);
    EXPECT_FALSE(migration.exploits_file.present);
}

TEST(AccountManagement, EnsuresCharacterMigrationByCreatingMissingSnapshotFromLegacyFiles) {
    TemporaryDirectory temp_directory;
    std::string error_message;
    ASSERT_TRUE(account::create_account(temp_directory.path(), "alpha-admin", "player@example.com", "ValidPass1", 1700010099, nullptr, &error_message)) << error_message;
    ASSERT_EQ(mkdir((temp_directory.path() + "/players").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/players/A-E").c_str(), 0700), 0);

    const std::string expected_player_text = write_valid_legacy_player_file(temp_directory.path(), make_stored_character("aragorn"));

    account::CharacterMigrationData migration;
    ASSERT_TRUE(account::ensure_character_migration(temp_directory.path(), "alpha-admin", "aragorn", 1700010100, &migration, &error_message)) << error_message;
    EXPECT_EQ(migration.account_name, "alpha-admin");
    EXPECT_EQ(migration.character_name, "aragorn");
    EXPECT_TRUE(migration.player_file.present);

    account::CharacterMigrationData loaded_migration;
    ASSERT_TRUE(account::read_character_migration(temp_directory.path(), "alpha-admin", "aragorn", &loaded_migration, &error_message)) << error_message;
    EXPECT_EQ(loaded_migration.player_file.content, migration.player_file.content);

    char_file_u restored_character {};
    ASSERT_TRUE(account::read_account_character_file(temp_directory.path(), "alpha-admin", "aragorn", &restored_character, &error_message)) << error_message;
    EXPECT_STREQ(restored_character.name, "aragorn");
    EXPECT_EQ(restored_character.points.gold, make_stored_character("aragorn").points.gold);
    struct stat file_info {};
    EXPECT_NE(stat(account::legacy_player_file_path(temp_directory.path(), "aragorn").c_str(), &file_info), 0);
}

TEST(AccountManagement, RebuildsCorruptCharacterMigrationSnapshotFromLegacyFiles) {
    TemporaryDirectory temp_directory;
    std::string error_message;
    ASSERT_TRUE(account::create_account(temp_directory.path(), "alpha-admin", "player@example.com", "ValidPass1", 1700010099, nullptr, &error_message)) << error_message;
    ASSERT_EQ(mkdir((temp_directory.path() + "/players").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/players/A-E").c_str(), 0700), 0);

    const std::string expected_player_text = write_valid_legacy_player_file(temp_directory.path(), make_stored_character("aragorn"));
    write_text_file(account::account_character_snapshot_path(temp_directory.path(), "alpha-admin", "aragorn"), "{bad-json");

    account::CharacterMigrationData migration;
    ASSERT_TRUE(account::ensure_character_migration(temp_directory.path(), "alpha-admin", "aragorn", 1700010100, &migration, &error_message)) << error_message;
    EXPECT_TRUE(migration.player_file.present);

    account::CharacterMigrationData loaded_migration;
    ASSERT_TRUE(account::read_character_migration(temp_directory.path(), "alpha-admin", "aragorn", &loaded_migration, &error_message)) << error_message;
    EXPECT_EQ(loaded_migration.player_file.content, migration.player_file.content);

    char_file_u restored_character {};
    ASSERT_TRUE(account::read_account_character_file(temp_directory.path(), "alpha-admin", "aragorn", &restored_character, &error_message)) << error_message;
    EXPECT_STREQ(restored_character.name, "aragorn");
    EXPECT_EQ(restored_character.points.gold, make_stored_character("aragorn").points.gold);

    struct stat file_info {};
    EXPECT_NE(stat(account::legacy_player_file_path(temp_directory.path(), "aragorn").c_str(), &file_info), 0);
}

TEST(AccountManagement, EnsureCharacterMigrationFailsClosedWhenOnlySnapshotRemains) {
    TemporaryDirectory temp_directory;
    std::string error_message;
    ASSERT_TRUE(account::create_account(temp_directory.path(), "alpha-admin", "player@example.com", "ValidPass1", 1700010099, nullptr, &error_message)) << error_message;
    ASSERT_EQ(mkdir((temp_directory.path() + "/players").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/players/A-E").c_str(), 0700), 0);

    write_valid_legacy_player_file(temp_directory.path(), make_stored_character("aragorn"));

    account::CharacterMigrationData migration;
    ASSERT_TRUE(account::migrate_legacy_character_by_name(temp_directory.path(), "alpha-admin", "aragorn", 1700010100, &migration, &error_message)) << error_message;

    ASSERT_TRUE(account::remove_account_character_file(temp_directory.path(), "alpha-admin", "aragorn", &error_message)) << error_message;
    EXPECT_FALSE(account::account_character_file_exists(temp_directory.path(), "alpha-admin", "aragorn", &error_message));

    account::CharacterMigrationData ensured_migration;
    EXPECT_FALSE(account::ensure_character_migration(temp_directory.path(), "alpha-admin", "aragorn", 1700010101, &ensured_migration, &error_message));
    EXPECT_NE(error_message.find("transitional migration snapshot alone is insufficient"), std::string::npos);
    EXPECT_FALSE(account::account_character_file_exists(temp_directory.path(), "alpha-admin", "aragorn", nullptr));
}

TEST(AccountManagement, EnsureCharacterMigrationSucceedsWhenAuthoritativeCharacterFileExistsAndSnapshotIsCorrupt) {
    TemporaryDirectory temp_directory;
    std::string error_message;
    ASSERT_TRUE(account::create_account(temp_directory.path(), "alpha-admin", "player@example.com", "ValidPass1", 1700010102, nullptr, &error_message)) << error_message;
    ASSERT_EQ(mkdir((temp_directory.path() + "/players").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/players/A-E").c_str(), 0700), 0);

    write_valid_legacy_player_file(temp_directory.path(), make_stored_character("aragorn"));

    account::CharacterMigrationData migration;
    ASSERT_TRUE(account::migrate_legacy_character_by_name(temp_directory.path(), "alpha-admin", "aragorn", 1700010103, &migration, &error_message)) << error_message;

    write_text_file(account::account_character_snapshot_path(temp_directory.path(), "alpha-admin", "aragorn"), "{bad-json");

    account::CharacterMigrationData ensured_migration;
    EXPECT_TRUE(account::ensure_character_migration(temp_directory.path(), "alpha-admin", "aragorn", 1700010104, &ensured_migration, &error_message)) << error_message;
    EXPECT_TRUE(account::account_character_file_exists(temp_directory.path(), "alpha-admin", "aragorn", &error_message)) << error_message;
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

    std::string error_message;
    ASSERT_TRUE(account::create_account(temp_directory.path(), "alpha-admin", "player@example.com", "ValidPass1", 1700010100, nullptr, &error_message)) << error_message;

    const std::string expected_player_text = write_valid_legacy_player_file(temp_directory.path(), make_stored_character("aragorn"));
    const std::string expected_object_bytes = make_valid_object_bytes();
    write_text_file(account::legacy_object_file_path(temp_directory.path(), "aragorn"), expected_object_bytes);

    account::CharacterMigrationData migration;
    ASSERT_TRUE(account::migrate_legacy_character_by_name(temp_directory.path(), "alpha-admin", "aragorn", 1700010101, &migration, &error_message)) << error_message;

    write_text_file(account::legacy_player_file_path(temp_directory.path(), "aragorn"), "stale-player-data");
    write_text_file(account::legacy_object_file_path(temp_directory.path(), "aragorn"), "stale-object-data");
    write_text_file(account::legacy_exploits_file_path(temp_directory.path(), "aragorn"), "stale-exploit-data");

    ASSERT_TRUE(account::restore_character_migration(temp_directory.path(), "alpha-admin", "aragorn", migration, &error_message)) << error_message;

    EXPECT_EQ(read_file_contents(account::legacy_player_file_path(temp_directory.path(), "aragorn")), expected_player_text);
    EXPECT_EQ(read_file_contents(account::legacy_object_file_path(temp_directory.path(), "aragorn")), expected_object_bytes);

    struct stat file_info {};
    EXPECT_NE(stat(account::legacy_exploits_file_path(temp_directory.path(), "aragorn").c_str(), &file_info), 0);
}

TEST(AccountManagement, RejectsMismatchedRestoreRequestWithoutTouchingLegacyFiles) {
    TemporaryDirectory temp_directory;
    ASSERT_EQ(mkdir((temp_directory.path() + "/players").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/players/A-E").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/plrobjs").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/plrobjs/A-E").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/exploits").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/exploits/A-E").c_str(), 0700), 0);

    std::string error_message;
    ASSERT_TRUE(account::create_account(temp_directory.path(), "alpha-admin", "player@example.com", "ValidPass1", 1700010100, nullptr, &error_message)) << error_message;

    write_valid_legacy_player_file(temp_directory.path(), make_stored_character("aragorn"));
    write_text_file(account::legacy_object_file_path(temp_directory.path(), "aragorn"), make_valid_object_bytes());

    account::CharacterMigrationData migration;
    ASSERT_TRUE(account::migrate_legacy_character_by_name(temp_directory.path(), "alpha-admin", "aragorn", 1700010101, &migration, &error_message)) << error_message;

    write_text_file(account::legacy_player_file_path(temp_directory.path(), "aragorn"), "stale-player-data");
    write_text_file(account::legacy_object_file_path(temp_directory.path(), "aragorn"), "stale-object-data");
    write_text_file(account::legacy_exploits_file_path(temp_directory.path(), "aragorn"), "stale-exploit-data");

    EXPECT_FALSE(account::restore_character_migration(temp_directory.path(), "beta-admin", "aragorn", migration, &error_message));
    EXPECT_NE(error_message.find("Migration account identity did not match"), std::string::npos);
    EXPECT_EQ(read_file_contents(account::legacy_player_file_path(temp_directory.path(), "aragorn")), "stale-player-data");
    EXPECT_EQ(read_file_contents(account::legacy_object_file_path(temp_directory.path(), "aragorn")), "stale-object-data");
    EXPECT_EQ(read_file_contents(account::legacy_exploits_file_path(temp_directory.path(), "aragorn")), "stale-exploit-data");
}

TEST(AccountManagement, ClearsRuntimeSupportFilesForAccountBackedPlayWithoutRewritingPlayerFile) {
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

    write_text_file(account::legacy_player_file_path(temp_directory.path(), "aragorn"), "player-stays-put");
    write_text_file(account::legacy_object_file_path(temp_directory.path(), "aragorn"), "stale-object-data");
    write_text_file(account::legacy_exploits_file_path(temp_directory.path(), "aragorn"), "stale-exploit-data");

    std::string error_message;
    ASSERT_TRUE(account::clear_character_runtime_support_files_for_account_play(temp_directory.path(), "alpha-admin", "aragorn", migration, &error_message)) << error_message;
    EXPECT_EQ(read_file_contents(account::legacy_player_file_path(temp_directory.path(), "aragorn")), "player-stays-put");

    struct stat file_info {};
    EXPECT_NE(stat(account::legacy_object_file_path(temp_directory.path(), "aragorn").c_str(), &file_info), 0);
    EXPECT_NE(stat(account::legacy_exploits_file_path(temp_directory.path(), "aragorn").c_str(), &file_info), 0);
}

TEST(AccountManagement, RejectsClearingSupportFilesWhenSnapshotIdentityDoesNotMatchSelection) {
    TemporaryDirectory temp_directory;
    account::CharacterMigrationData migration;
    migration.account_name = "beta-admin";
    migration.character_name = "legolas";

    std::string error_message;
    EXPECT_FALSE(account::clear_character_runtime_support_files_for_account_play(temp_directory.path(), "alpha-admin", "aragorn", migration, &error_message));
    EXPECT_NE(error_message.find("did not match"), std::string::npos);
}

TEST(AccountManagement, RefreshesSnapshotForLinkedCharactersUsingCurrentLegacyFiles) {
    TemporaryDirectory temp_directory;
    ASSERT_EQ(mkdir((temp_directory.path() + "/players").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/players/A-E").c_str(), 0700), 0);

    std::string error_message;
    ASSERT_TRUE(account::create_account(temp_directory.path(), "alpha-admin", "player@example.com", "ValidPass1", 1700010101, nullptr, &error_message)) << error_message;
    ASSERT_TRUE(account::admin_link_character(temp_directory.path(), "alpha-admin", "aragorn", 1700010102, nullptr, &error_message)) << error_message;
    char_file_u refreshed_store = make_stored_character("aragorn");
    refreshed_store.points.gold = 9001;
    write_valid_legacy_player_file(temp_directory.path(), refreshed_store);

    account::CharacterMigrationData migration;
    ASSERT_TRUE(account::refresh_linked_character_snapshot(temp_directory.path(), "aragorn", 1700010103, &migration, &error_message)) << error_message;
    EXPECT_EQ(migration.account_name, "alpha-admin");
    EXPECT_EQ(migration.character_name, "aragorn");
    EXPECT_TRUE(migration.player_file.present);

    account::CharacterMigrationData loaded_migration;
    ASSERT_TRUE(account::read_character_migration(temp_directory.path(), "alpha-admin", "aragorn", &loaded_migration, &error_message)) << error_message;
    EXPECT_EQ(loaded_migration.player_file.content, migration.player_file.content);
}

TEST(AccountManagement, MigrationWritesAccountNativeObjectFileWhenLegacyObjectDataIsValid) {
    TemporaryDirectory temp_directory;
    ASSERT_EQ(mkdir((temp_directory.path() + "/players").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/players/A-E").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/plrobjs").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/plrobjs/A-E").c_str(), 0700), 0);

    std::string error_message;
    ASSERT_TRUE(account::create_account(temp_directory.path(), "alpha-admin", "player@example.com", "ValidPass1", 1700010101, nullptr, &error_message)) << error_message;

    write_valid_legacy_player_file(temp_directory.path(), make_stored_character("aragorn"));

    objects_json::ObjectSaveData object_data;
    object_data.rent.rentcode = RENT_CRASH;
    object_data.objects.push_back(objects_json::ObjectRecord {});
    object_data.objects[0].item_number = 3210;
    object_data.objects[0].wear_pos = WEAR_BODY;

    std::string object_bytes;
    ASSERT_TRUE(objects_json::object_save_data_to_binary(object_data, &object_bytes, &error_message)) << error_message;
    write_text_file(account::legacy_object_file_path(temp_directory.path(), "aragorn"), object_bytes);

    account::CharacterMigrationData migration;
    ASSERT_TRUE(account::migrate_legacy_character_by_name(temp_directory.path(), "alpha-admin", "aragorn", 1700010102, &migration, &error_message)) << error_message;
    ASSERT_TRUE(account::account_object_file_exists(temp_directory.path(), "alpha-admin", "aragorn", &error_message));

    std::string loaded_bytes;
    ASSERT_TRUE(account::read_account_object_file(temp_directory.path(), "alpha-admin", "aragorn", &loaded_bytes, &error_message)) << error_message;

    objects_json::ObjectSaveData loaded;
    ASSERT_TRUE(objects_json::object_save_data_from_binary(loaded_bytes, &loaded, &error_message)) << error_message;
    ASSERT_EQ(loaded.objects.size(), 1u);
    EXPECT_EQ(loaded.objects[0].item_number, 3210);
    EXPECT_EQ(loaded.objects[0].wear_pos, WEAR_BODY);
}

TEST(AccountManagement, MigrationWritesDefaultAccountNativeObjectFileWhenLegacyObjectDataIsMissing) {
    TemporaryDirectory temp_directory;
    ASSERT_EQ(mkdir((temp_directory.path() + "/players").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/players/A-E").c_str(), 0700), 0);

    std::string error_message;
    ASSERT_TRUE(account::create_account(temp_directory.path(), "alpha-admin", "player@example.com", "ValidPass1", 1700010101, nullptr, &error_message)) << error_message;

    write_valid_legacy_player_file(temp_directory.path(), make_stored_character("aragorn"));

    account::CharacterMigrationData migration;
    ASSERT_TRUE(account::migrate_legacy_character_by_name(temp_directory.path(), "alpha-admin", "aragorn", 1700010102, &migration, &error_message)) << error_message;
    ASSERT_TRUE(account::account_object_file_exists(temp_directory.path(), "alpha-admin", "aragorn", &error_message));

    std::string loaded_bytes;
    ASSERT_TRUE(account::read_account_object_file(temp_directory.path(), "alpha-admin", "aragorn", &loaded_bytes, &error_message)) << error_message;

    objects_json::ObjectSaveData loaded;
    ASSERT_TRUE(objects_json::object_save_data_from_binary(loaded_bytes, &loaded, &error_message)) << error_message;
    EXPECT_TRUE(loaded.objects.empty());
    EXPECT_TRUE(loaded.aliases.empty());
    EXPECT_TRUE(loaded.followers.empty());
}

TEST(AccountManagement, MigrationWritesAccountNativeExploitFileWhenLegacyExploitDataIsValid) {
    TemporaryDirectory temp_directory;
    ASSERT_EQ(mkdir((temp_directory.path() + "/players").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/players/A-E").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/exploits").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/exploits/A-E").c_str(), 0700), 0);

    std::string error_message;
    ASSERT_TRUE(account::create_account(temp_directory.path(), "alpha-admin", "player@example.com", "ValidPass1", 1700010101, nullptr, &error_message)) << error_message;

    write_valid_legacy_player_file(temp_directory.path(), make_stored_character("aragorn"));
    std::vector<exploit_record> records;
    records.push_back(make_exploit_record(EXPLOIT_LEVEL, "Mon Jan  1 00:00:00 2024", "level", 10, 0, 20));
    std::string exploit_bytes;
    ASSERT_TRUE(exploits_json::exploit_records_to_binary(records, &exploit_bytes, &error_message)) << error_message;
    write_text_file(account::legacy_exploits_file_path(temp_directory.path(), "aragorn"), exploit_bytes);

    account::CharacterMigrationData migration;
    ASSERT_TRUE(account::migrate_legacy_character_by_name(temp_directory.path(), "alpha-admin", "aragorn", 1700010102, &migration, &error_message)) << error_message;
    ASSERT_TRUE(account::account_exploit_file_exists(temp_directory.path(), "alpha-admin", "aragorn", &error_message));

    std::vector<exploit_record> loaded_records;
    ASSERT_TRUE(account::read_account_exploit_file(temp_directory.path(), "alpha-admin", "aragorn", &loaded_records, &error_message)) << error_message;
    ASSERT_EQ(loaded_records.size(), 1u);
    EXPECT_EQ(loaded_records[0].type, EXPLOIT_LEVEL);
    EXPECT_EQ(loaded_records[0].iIntParam, 20);
}

TEST(AccountManagement, MigrationRetiresLegacyFilesAfterSuccessfulAccountNativeWrite) {
    TemporaryDirectory temp_directory;
    ASSERT_EQ(mkdir((temp_directory.path() + "/players").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/players/A-E").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/plrobjs").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/plrobjs/A-E").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/exploits").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/exploits/A-E").c_str(), 0700), 0);

    std::string error_message;
    ASSERT_TRUE(account::create_account(temp_directory.path(), "alpha-admin", "player@example.com", "ValidPass1", 1700010101, nullptr, &error_message)) << error_message;

    const std::string player_path = account::legacy_player_file_path(temp_directory.path(), "aragorn");
    const std::string object_path = account::legacy_object_file_path(temp_directory.path(), "aragorn");
    const std::string exploits_path = account::legacy_exploits_file_path(temp_directory.path(), "aragorn");
    const std::string expected_player_text = write_valid_legacy_player_file(temp_directory.path(), make_stored_character("aragorn"));
    write_text_file(object_path, make_valid_object_bytes());
    write_text_file(exploits_path, make_valid_exploit_bytes());

    account::CharacterMigrationData migration;
    ASSERT_TRUE(account::migrate_legacy_character_by_name(temp_directory.path(), "alpha-admin", "aragorn", 1700010102, &migration, &error_message)) << error_message;

    struct stat file_info {};
    EXPECT_EQ(stat(account::account_character_snapshot_path(temp_directory.path(), "alpha-admin", "aragorn").c_str(), &file_info), 0);
    EXPECT_NE(stat(player_path.c_str(), &file_info), 0);
    EXPECT_NE(stat(object_path.c_str(), &file_info), 0);
    EXPECT_NE(stat(exploits_path.c_str(), &file_info), 0);
    EXPECT_TRUE(account::account_object_file_exists(temp_directory.path(), "alpha-admin", "aragorn", &error_message));
    EXPECT_TRUE(account::account_exploit_file_exists(temp_directory.path(), "alpha-admin", "aragorn", &error_message));
    EXPECT_FALSE(expected_player_text.empty());
}

TEST(AccountManagement, MigrationFailsClosedWhenLegacyFileRetirementFails) {
    TemporaryDirectory temp_directory;
    ASSERT_EQ(mkdir((temp_directory.path() + "/players").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/players/A-E").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/plrobjs").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/plrobjs/A-E").c_str(), 0700), 0);

    std::string error_message;
    ASSERT_TRUE(account::create_account(temp_directory.path(), "alpha-admin", "player@example.com", "ValidPass1", 1700010101, nullptr, &error_message)) << error_message;

    const std::string player_path = account::legacy_player_file_path(temp_directory.path(), "aragorn");
    const std::string object_bucket = temp_directory.path() + "/plrobjs/A-E";
    const std::string object_path = account::legacy_object_file_path(temp_directory.path(), "aragorn");
    const std::string expected_player_text = write_valid_legacy_player_file(temp_directory.path(), make_stored_character("aragorn"));
    write_text_file(object_path, make_valid_object_bytes());

    ASSERT_EQ(chmod(object_bucket.c_str(), 0500), 0);

    account::CharacterMigrationData migration;
    EXPECT_FALSE(account::migrate_legacy_character_by_name(temp_directory.path(), "alpha-admin", "aragorn", 1700010102, &migration, &error_message));
    EXPECT_NE(error_message.find("Failed to retire legacy object file"), std::string::npos);

    ASSERT_EQ(chmod(object_bucket.c_str(), 0700), 0);

    struct stat file_info {};
    EXPECT_EQ(stat(player_path.c_str(), &file_info), 0);
    EXPECT_EQ(stat(object_path.c_str(), &file_info), 0);
    EXPECT_EQ(read_file_contents(player_path), expected_player_text);
    EXPECT_NE(stat(account::account_character_snapshot_path(temp_directory.path(), "alpha-admin", "aragorn").c_str(), &file_info), 0);
    EXPECT_FALSE(account::account_character_file_exists(temp_directory.path(), "alpha-admin", "aragorn", &error_message));
    EXPECT_FALSE(account::account_object_file_exists(temp_directory.path(), "alpha-admin", "aragorn", &error_message));
    EXPECT_FALSE(account::account_exploit_file_exists(temp_directory.path(), "alpha-admin", "aragorn", &error_message));
}

TEST(AccountManagement, MigrationCleansUpAccountNativeOutputsWhenStaleFlatRetirementFails) {
    TemporaryDirectory temp_directory;
    std::string error_message;
    ASSERT_TRUE(account::create_account(temp_directory.path(), "alpha-admin", "player@example.com", "ValidPass1", 1700010100, nullptr, &error_message)) << error_message;
    ASSERT_EQ(mkdir((temp_directory.path() + "/players").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/players/A-E").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/plrobjs").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/plrobjs/A-E").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/exploits").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/exploits/A-E").c_str(), 0700), 0);

    char_file_u versioned_character = make_stored_character("aragorn");
    versioned_character.specials2.idnum = 2222;
    const std::string versioned_player_path = account::legacy_player_file_path(temp_directory.path(), "aragorn") + ".1.1.2222.1700010000.0";
    const std::string expected_player_text = write_valid_legacy_player_file(temp_directory.path(), versioned_character, versioned_player_path);
    const std::string expected_object_bytes = make_valid_object_bytes();
    const std::string expected_exploit_bytes = make_valid_exploit_bytes();
    write_text_file(account::legacy_object_file_path(temp_directory.path(), "aragorn"), expected_object_bytes);
    write_text_file(account::legacy_exploits_file_path(temp_directory.path(), "aragorn"), expected_exploit_bytes);

    const std::string stale_flat_path = account::legacy_player_file_path(temp_directory.path(), "aragorn");
    ASSERT_EQ(mkdir(stale_flat_path.c_str(), 0700), 0);
    write_text_file(stale_flat_path + "/blocker", "x");

    account::CharacterMigrationData migration;
    EXPECT_FALSE(account::migrate_legacy_character_by_name(temp_directory.path(), "alpha-admin", "aragorn", 1700010102, &migration, &error_message));
    EXPECT_NE(error_message.find("Failed to retire stale legacy player file"), std::string::npos);

    EXPECT_FALSE(account::account_character_file_exists(temp_directory.path(), "alpha-admin", "aragorn", nullptr));
    EXPECT_FALSE(account::account_object_file_exists(temp_directory.path(), "alpha-admin", "aragorn", nullptr));
    EXPECT_FALSE(account::account_exploit_file_exists(temp_directory.path(), "alpha-admin", "aragorn", nullptr));

    struct stat file_info {};
    EXPECT_NE(stat(account::account_character_snapshot_path(temp_directory.path(), "alpha-admin", "aragorn").c_str(), &file_info), 0);
    EXPECT_EQ(read_file_contents(versioned_player_path), expected_player_text);
    EXPECT_EQ(read_file_contents(account::legacy_object_file_path(temp_directory.path(), "aragorn")), expected_object_bytes);
    EXPECT_EQ(read_file_contents(account::legacy_exploits_file_path(temp_directory.path(), "aragorn")), expected_exploit_bytes);
    EXPECT_EQ(stat(stale_flat_path.c_str(), &file_info), 0);
}

TEST(AccountManagement, MigrationRestoresRetiredFilesWhenExploitRetirementFails) {
    TemporaryDirectory temp_directory;
    ASSERT_EQ(mkdir((temp_directory.path() + "/players").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/players/A-E").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/plrobjs").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/plrobjs/A-E").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/exploits").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/exploits/A-E").c_str(), 0700), 0);

    std::string error_message;
    ASSERT_TRUE(account::create_account(temp_directory.path(), "alpha-admin", "player@example.com", "ValidPass1", 1700010101, nullptr, &error_message)) << error_message;

    const std::string player_path = account::legacy_player_file_path(temp_directory.path(), "aragorn");
    const std::string object_path = account::legacy_object_file_path(temp_directory.path(), "aragorn");
    const std::string exploits_bucket = temp_directory.path() + "/exploits/A-E";
    const std::string exploits_path = account::legacy_exploits_file_path(temp_directory.path(), "aragorn");
    const std::string expected_player_text = write_valid_legacy_player_file(temp_directory.path(), make_stored_character("aragorn"));
    write_text_file(object_path, make_valid_object_bytes());
    write_text_file(exploits_path, make_valid_exploit_bytes());

    ASSERT_EQ(chmod(exploits_bucket.c_str(), 0500), 0);

    account::CharacterMigrationData migration;
    EXPECT_FALSE(account::migrate_legacy_character_by_name(temp_directory.path(), "alpha-admin", "aragorn", 1700010102, &migration, &error_message));
    EXPECT_NE(error_message.find("Failed to retire legacy exploit file"), std::string::npos);

    ASSERT_EQ(chmod(exploits_bucket.c_str(), 0700), 0);

    struct stat file_info {};
    EXPECT_EQ(stat(player_path.c_str(), &file_info), 0);
    EXPECT_EQ(stat(object_path.c_str(), &file_info), 0);
    EXPECT_EQ(stat(exploits_path.c_str(), &file_info), 0);
    EXPECT_EQ(read_file_contents(player_path), expected_player_text);
    EXPECT_NE(stat(account::account_character_snapshot_path(temp_directory.path(), "alpha-admin", "aragorn").c_str(), &file_info), 0);
    EXPECT_FALSE(account::account_character_file_exists(temp_directory.path(), "alpha-admin", "aragorn", &error_message));
    EXPECT_FALSE(account::account_object_file_exists(temp_directory.path(), "alpha-admin", "aragorn", &error_message));
    EXPECT_FALSE(account::account_exploit_file_exists(temp_directory.path(), "alpha-admin", "aragorn", &error_message));
}

TEST(AccountManagement, MigrationWritesDefaultAccountNativeExploitFileWhenLegacyExploitDataIsMissing) {
    TemporaryDirectory temp_directory;
    ASSERT_EQ(mkdir((temp_directory.path() + "/players").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/players/A-E").c_str(), 0700), 0);

    std::string error_message;
    ASSERT_TRUE(account::create_account(temp_directory.path(), "alpha-admin", "player@example.com", "ValidPass1", 1700010101, nullptr, &error_message)) << error_message;

    write_valid_legacy_player_file(temp_directory.path(), make_stored_character("aragorn"));

    account::CharacterMigrationData migration;
    ASSERT_TRUE(account::migrate_legacy_character_by_name(temp_directory.path(), "alpha-admin", "aragorn", 1700010102, &migration, &error_message)) << error_message;
    ASSERT_TRUE(account::account_exploit_file_exists(temp_directory.path(), "alpha-admin", "aragorn", &error_message));

    std::vector<exploit_record> loaded_records;
    ASSERT_TRUE(account::read_account_exploit_file(temp_directory.path(), "alpha-admin", "aragorn", &loaded_records, &error_message)) << error_message;
    EXPECT_TRUE(loaded_records.empty());
}

TEST(AccountManagement, MigrationFailsWhenLegacyExploitDataIsMalformed) {
    TemporaryDirectory temp_directory;
    ASSERT_EQ(mkdir((temp_directory.path() + "/players").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/players/A-E").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/exploits").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/exploits/A-E").c_str(), 0700), 0);

    std::string error_message;
    ASSERT_TRUE(account::create_account(temp_directory.path(), "alpha-admin", "player@example.com", "ValidPass1", 1700010101, nullptr, &error_message)) << error_message;

    write_valid_legacy_player_file(temp_directory.path(), make_stored_character("aragorn"));
    write_text_file(account::legacy_exploits_file_path(temp_directory.path(), "aragorn"), "bad");

    account::CharacterMigrationData migration;
    EXPECT_FALSE(account::migrate_legacy_character_by_name(temp_directory.path(), "alpha-admin", "aragorn", 1700010102, &migration, &error_message));
    EXPECT_NE(error_message.find("Exploit history bytes are malformed"), std::string::npos);
}

TEST(AccountManagement, MigrationFailsWhenLegacyObjectDataIsMalformed) {
    TemporaryDirectory temp_directory;
    ASSERT_EQ(mkdir((temp_directory.path() + "/players").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/players/A-E").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/plrobjs").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/plrobjs/A-E").c_str(), 0700), 0);

    std::string error_message;
    ASSERT_TRUE(account::create_account(temp_directory.path(), "alpha-admin", "player@example.com", "ValidPass1", 1700010101, nullptr, &error_message)) << error_message;

    write_valid_legacy_player_file(temp_directory.path(), make_stored_character("aragorn"));
    write_text_file(account::legacy_object_file_path(temp_directory.path(), "aragorn"), "bad");

    account::CharacterMigrationData migration;
    EXPECT_FALSE(account::migrate_legacy_character_by_name(temp_directory.path(), "alpha-admin", "aragorn", 1700010102, &migration, &error_message));
    EXPECT_NE(error_message.find("Truncated objects data"), std::string::npos);
}

TEST(AccountManagement, RestoredSnapshotReflectsRefreshedLinkedCharacterState) {
    TemporaryDirectory temp_directory;
    ASSERT_EQ(mkdir((temp_directory.path() + "/players").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/players/A-E").c_str(), 0700), 0);

    std::string error_message;
    ASSERT_TRUE(account::create_account(temp_directory.path(), "alpha-admin", "player@example.com", "ValidPass1", 1700010102, nullptr, &error_message)) << error_message;
    ASSERT_TRUE(account::admin_link_character(temp_directory.path(), "alpha-admin", "aragorn", 1700010103, nullptr, &error_message)) << error_message;
    char_file_u latest_store = make_stored_character("aragorn");
    latest_store.points.gold = 7777;
    const std::string expected_player_text = write_valid_legacy_player_file(temp_directory.path(), latest_store);

    account::CharacterMigrationData migration;
    ASSERT_TRUE(account::refresh_linked_character_snapshot(temp_directory.path(), "aragorn", 1700010104, &migration, &error_message)) << error_message;

    std::remove(account::legacy_player_file_path(temp_directory.path(), "aragorn").c_str());
    ASSERT_TRUE(account::restore_character_migration(temp_directory.path(), "alpha-admin", "aragorn", migration, &error_message)) << error_message;
    EXPECT_EQ(read_file_contents(account::legacy_player_file_path(temp_directory.path(), "aragorn")), expected_player_text);
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

    char_file_u first_store = make_stored_character("aragorn");
    first_store.points.gold = 101;
    write_valid_legacy_player_file(temp_directory.path(), first_store);
    write_text_file(account::legacy_exploits_file_path(temp_directory.path(), "aragorn"), make_valid_exploit_bytes());

    account::CharacterMigrationData initial_migration;
    ASSERT_TRUE(account::refresh_linked_character_snapshot(temp_directory.path(), "aragorn", 1700010104, &initial_migration, &error_message)) << error_message;
    ASSERT_TRUE(initial_migration.exploits_file.present);

    std::remove(account::legacy_exploits_file_path(temp_directory.path(), "aragorn").c_str());
    char_file_u second_store = make_stored_character("aragorn");
    second_store.points.gold = 202;
    write_valid_legacy_player_file(temp_directory.path(), second_store);

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
    write_valid_legacy_player_file(temp_directory.path(), make_stored_character("aragorn"));

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
    ASSERT_EQ(mkdir((temp_directory.path() + "/accounts/P-T").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/accounts/P-T/player@example.com").c_str(), 0700), 0);
    write_text_file(account::account_file_path(temp_directory.path(), "player@example.com"), "{not-valid-json");

    std::string error_message;
    EXPECT_FALSE(account::create_account(temp_directory.path(), "alpha-admin", "player@example.com", "ValidPass1", 1700011111, nullptr, &error_message));
    EXPECT_NE(error_message.find("could not be read safely"), std::string::npos);
}
