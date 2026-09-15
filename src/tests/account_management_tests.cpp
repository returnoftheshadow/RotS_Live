#include "../account_index.h"
#include "../account_errors.h"
#include "../account_management.h"
#include "../exploits_json.h"
#include "../objects_json.h"
#include "../roster_cache.h"
#include "../utils.h"

#include <gtest/gtest.h>

#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <limits.h>
#include <regex>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <utility>

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
            struct stat file_info { };
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
    stored_character.specials2.tactics = TACTICS_CAREFUL;
    stored_character.specials2.shooting = SHOOTING_SLOW;
    stored_character.specials2.casting = CASTING_FAST;
    stored_character.specials2.two_handed = 1;
    stored_character.profs.specialization = PLRSPEC_WMSR;
    stored_character.profs.color_mask = 0x654321;
    stored_character.profs.colors[COLOR_CHAT] = CBLU;
    stored_character.profs.colors[COLOR_ROOM] = CYEL;
    stored_character.profs.colors[COLOR_OBJ] = CCYN;
    stored_character.profs.colors[COLOR_MAGIC] = CBBLU;
    stored_character.profs.colors[COLOR_WEATHER] = CBYEL;
    stored_character.profs.color_settings[COLOR_MAGIC].foreground.mode = COLOR_VALUE_TRUECOLOR;
    stored_character.profs.color_settings[COLOR_MAGIC].foreground.ansi = CBBLU;
    stored_character.profs.color_settings[COLOR_MAGIC].foreground.red = 80;
    stored_character.profs.color_settings[COLOR_MAGIC].foreground.green = 90;
    stored_character.profs.color_settings[COLOR_MAGIC].foreground.blue = 255;
    stored_character.profs.color_settings[COLOR_WEATHER].background.mode = COLOR_VALUE_TRUECOLOR;
    stored_character.profs.color_settings[COLOR_WEATHER].background.ansi = CBLU;
    stored_character.profs.color_settings[COLOR_WEATHER].background.red = 10;
    stored_character.profs.color_settings[COLOR_WEATHER].background.green = 20;
    stored_character.profs.color_settings[COLOR_WEATHER].background.blue = 35;
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

TEST(AccountManagement, NormalizesEmailByTrimmingAndLowercasing)
{
    EXPECT_EQ(account::normalize_email("  Player@Example.COM "), "player@example.com");
}

TEST(AccountManagement, AcceptsReasonableEmailAddresses)
{
    std::string error_message;

    EXPECT_TRUE(account::is_valid_email("player@example.com", &error_message)) << error_message;
    EXPECT_TRUE(account::is_valid_email("player.one+alts@example-domain.com", &error_message)) << error_message;
}

TEST(AccountManagement, RejectsMalformedEmailAddresses)
{
    std::string error_message;

    EXPECT_FALSE(account::is_valid_email("not-an-email", &error_message));
    EXPECT_NE(error_message.find("@"), std::string::npos);

    EXPECT_FALSE(account::is_valid_email("player@example.com\nbcc@example.com", &error_message));
    EXPECT_NE(error_message.find("whitespace"), std::string::npos);

    EXPECT_FALSE(account::is_valid_email("player@example.com,other@example.com", &error_message));
    EXPECT_NE(error_message.find("recipient"), std::string::npos);
}

TEST(AccountManagement, RejectsEmailAddressesLongerThanTheStorageLayerCanCarry)
{
    std::string error_message;

    const std::string at_the_limit = std::string(account::MAX_EMAIL_LENGTH - std::strlen("@example.com"), 'a') + "@example.com";
    ASSERT_EQ(at_the_limit.length(), static_cast<std::size_t>(account::MAX_EMAIL_LENGTH));
    EXPECT_TRUE(account::is_valid_email(at_the_limit, &error_message)) << error_message;

    const std::string one_over = std::string(1 + account::MAX_EMAIL_LENGTH - std::strlen("@example.com"), 'a') + "@example.com";
    EXPECT_FALSE(account::is_valid_email(one_over, &error_message));
    EXPECT_NE(error_message.find(std::to_string(account::MAX_EMAIL_LENGTH)), std::string::npos) << error_message;
}

TEST(AccountManagement, AcceptsConservativeAccountNames)
{
    std::string error_message;

    EXPECT_TRUE(account::is_valid_account_name("Alpha-Admin_7", &error_message)) << error_message;
    EXPECT_TRUE(error_message.empty());
}

TEST(AccountManagement, RejectsAccountNamesWithUnsupportedCharacters)
{
    std::string error_message;

    EXPECT_FALSE(account::is_valid_account_name("Alpha Admin!", &error_message));
    EXPECT_NE(error_message.find("letters"), std::string::npos)
        << "Expected invalid account-name failures to explain the supported character set.";
}

// Live data holds legacy characters older than the 3-character creation minimum (Ao, El, Fy, Ia,
// Li, Pi, Ru). New names still go through valid_name (ban.cpp); the account layer must only refuse
// what cannot be a safe path component, never an existing character for being short.
TEST(AccountManagement, AcceptsShortExistingCharacterNames)
{
    std::string error_message;

    EXPECT_TRUE(account::is_valid_character_name("pi", &error_message)) << error_message;
    EXPECT_TRUE(account::is_valid_character_name("A", &error_message)) << error_message;
    EXPECT_FALSE(account::is_valid_account_name("pi", &error_message));
}

TEST(AccountManagement, RejectsCharacterNamesThatAreNotSafePathComponents)
{
    std::string error_message;

    EXPECT_FALSE(account::is_valid_character_name("", &error_message));
    EXPECT_FALSE(error_message.empty());
    EXPECT_FALSE(account::is_valid_character_name("../pi", &error_message));
    EXPECT_NE(error_message.find("letters"), std::string::npos) << error_message;
    EXPECT_FALSE(account::is_valid_character_name(std::string(account::MAX_ACCOUNT_NAME_LENGTH + 1, 'a'), &error_message));
}

TEST(AccountManagement, RejectsPasswordsMissingRequiredComplexity)
{
    std::string error_message;

    EXPECT_FALSE(account::is_valid_password("alllowercase1", &error_message));
    EXPECT_NE(error_message.find("uppercase"), std::string::npos);

    EXPECT_FALSE(account::is_valid_password("ALLUPPERCASE1", &error_message));
    EXPECT_NE(error_message.find("lowercase"), std::string::npos);

    EXPECT_FALSE(account::is_valid_password("NoDigitsHere", &error_message));
    EXPECT_NE(error_message.find("number"), std::string::npos);
}

TEST(AccountManagement, AcceptsPasswordsMeetingConfiguredPolicy)
{
    std::string error_message;

    EXPECT_TRUE(account::is_valid_password("ValidPass1", &error_message)) << error_message;
    EXPECT_TRUE(error_message.empty());
}

TEST(AccountManagement, GeneratesOneWayPasswordCredentialsThatVerifySuccessfully)
{
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

TEST(AccountManagement, InitializesNewAccountsWithNormalizedIdentityAndPasswordHash)
{
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

TEST(AccountManagement, VerifiesAndUnverifiesAccountsWithAuditMetadata)
{
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

TEST(AccountManagement, PreparesEmailVerificationCodesWithFifteenMinuteExpiry)
{
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

TEST(AccountManagement, ConfirmsMatchingEmailVerificationCodesAndClearsPendingState)
{
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

TEST(AccountManagement, RejectsInvalidEmailVerificationCodes)
{
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

TEST(AccountManagement, RejectsExpiredEmailVerificationCodes)
{
    account::AccountData account_data;
    std::string error_message;
    ASSERT_TRUE(account::initialize_new_account("alpha-admin", "player@example.com", "ValidPass1", 1700001000, &account_data, &error_message)) << error_message;

    std::string verification_code;
    ASSERT_TRUE(account::prepare_email_verification_code(&account_data, 1700002000, &verification_code, &error_message)) << error_message;

    EXPECT_FALSE(account::confirm_email_verification_code(&account_data, verification_code, "email-code", 1700002000 + account::EMAIL_VERIFICATION_WINDOW_SECONDS + 1, &error_message));
    EXPECT_NE(error_message.find("expired"), std::string::npos);
}

TEST(AccountManagement, RejectsStaleEmailVerificationCodeAfterResend)
{
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

TEST(AccountManagement, InvalidatesVerificationCodeAfterTooManyInvalidAttempts)
{
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

TEST(AccountManagement, ThrottlesVerificationCodeResendsForRecentlyIssuedCodes)
{
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

TEST(AccountManagement, UsesConfiguredSendmailCommandForVerificationEmailDelivery)
{
    TemporaryDirectory temp_directory;
    const std::string root = temp_directory.path();
    const std::string capture_path = root + "/captured-mail.txt";
    const std::string command_script_path = root + "/capture-sendmail.sh";
    write_text_file(command_script_path,
        "#!/bin/sh\n"
        "cat > \""
            + capture_path + "\"\n");
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

TEST(AccountManagement, StartingEmailVerificationLeavesVerifiedAccountsVerified)
{
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

TEST(AccountManagement, AddsNormalizedCharactersAndRejectsDuplicateLinks)
{
    account::AccountData account_data = make_account();
    account_data.characters.clear();
    std::string error_message;

    ASSERT_TRUE(account::add_character_to_account(&account_data, " Aragorn ", &error_message)) << error_message;
    EXPECT_TRUE(account::account_has_character(account_data, "aragorn"));
    EXPECT_TRUE(account::account_has_character(account_data, "Aragorn"));

    EXPECT_FALSE(account::add_character_to_account(&account_data, "aragorn", &error_message));
    EXPECT_NE(error_message.find("already linked"), std::string::npos);
}

TEST(AccountManagement, SelectsOnlyCharactersLinkedToTheAccount)
{
    account::AccountData account_data = make_account();
    std::string selected_character;
    std::string error_message;

    ASSERT_TRUE(account::select_linked_character(".", account_data, "2", account::RosterSort::Account, account::RosterFilter::None, &selected_character, &error_message)) << error_message;
    EXPECT_EQ(selected_character, "legolas");

    EXPECT_FALSE(account::select_linked_character(".", account_data, "gandalf", account::RosterSort::Account, account::RosterFilter::None, &selected_character, &error_message));
    EXPECT_NE(error_message.find("Select a linked character by number or name"), std::string::npos);
    EXPECT_FALSE(account::select_linked_character(".", account_data, "0", account::RosterSort::Account, account::RosterFilter::None, &selected_character, &error_message));
    EXPECT_NE(error_message.find("Select a linked character by number or name"), std::string::npos);
}

TEST(AccountManagement, SelectsLinkedCharactersByFullName)
{
    account::AccountData account_data = make_account();
    std::string selected_character;
    std::string error_message;

    ASSERT_TRUE(account::select_linked_character(".", account_data, "aragorn", account::RosterSort::Account, account::RosterFilter::None, &selected_character, &error_message)) << error_message;
    EXPECT_EQ(selected_character, "aragorn");

    ASSERT_TRUE(account::select_linked_character(".", account_data, "Legolas", account::RosterSort::Account, account::RosterFilter::None, &selected_character, &error_message)) << error_message;
    EXPECT_EQ(selected_character, "legolas");
}

TEST(AccountManagement, SelectsLinkedCharactersByNameIgnoringCaseAndSurroundingSpaces)
{
    account::AccountData account_data = make_account();
    std::string selected_character;
    std::string error_message;

    ASSERT_TRUE(account::select_linked_character(".", account_data, "ArAgOrN", account::RosterSort::Account, account::RosterFilter::None, &selected_character, &error_message)) << error_message;
    EXPECT_EQ(selected_character, "aragorn");

    ASSERT_TRUE(account::select_linked_character(".", account_data, "  Legolas  ", account::RosterSort::Account, account::RosterFilter::None, &selected_character, &error_message)) << error_message;
    EXPECT_EQ(selected_character, "legolas");
}

TEST(AccountManagement, RejectsCharacterNamesThatAreNotLinkedToTheAccount)
{
    account::AccountData account_data = make_account();
    std::string selected_character;
    std::string error_message;

    EXPECT_FALSE(account::select_linked_character(".", account_data, "gandalf", account::RosterSort::Account, account::RosterFilter::None, &selected_character, &error_message));
    EXPECT_NE(error_message.find("Select a linked character by number or name"), std::string::npos);

    EXPECT_FALSE(account::select_linked_character(".", account_data, "arago", account::RosterSort::Account, account::RosterFilter::None, &selected_character, &error_message));
    EXPECT_NE(error_message.find("Select a linked character by number or name"), std::string::npos);

    EXPECT_FALSE(account::select_linked_character(".", account_data, "aragorn the second", account::RosterSort::Account, account::RosterFilter::None, &selected_character, &error_message));
    EXPECT_NE(error_message.find("Select a linked character by number or name"), std::string::npos);
}

TEST(AccountManagement, RejectsSelectionsBeyondTheDisplayedRosterRange)
{
    account::AccountData account_data = make_account();
    account_data.characters.clear();
    for (int index = 1; index <= 201; ++index)
        account_data.characters.push_back("character" + std::to_string(index));

    std::string selected_character;
    std::string error_message;

    EXPECT_TRUE(account::select_linked_character(".", account_data, "200", account::RosterSort::Account, account::RosterFilter::None, &selected_character, &error_message)) << error_message;
    EXPECT_EQ(selected_character, "character200");

    EXPECT_FALSE(account::select_linked_character(".", account_data, "201", account::RosterSort::Account, account::RosterFilter::None, &selected_character, &error_message));
    EXPECT_NE(error_message.find("Select a linked character by number or name"), std::string::npos);

    EXPECT_TRUE(account::select_linked_character(".", account_data, "character200", account::RosterSort::Account, account::RosterFilter::None, &selected_character, &error_message)) << error_message;
    EXPECT_EQ(selected_character, "character200");

    EXPECT_FALSE(account::select_linked_character(".", account_data, "character201", account::RosterSort::Account, account::RosterFilter::None, &selected_character, &error_message));
    EXPECT_NE(error_message.find("Select a linked character by number or name"), std::string::npos);
}

// The roster is written to the descriptor in a single SEND_TO_Q. write_to_output (comm.cpp) grows
// the descriptor to a LARGE_BUFSIZE buffer and, once a write no longer fits, sets bufptr = -1 and
// silently drops that write and every later one bound for the socket -- the player sees a blank
// screen and never gets a prompt back. Nothing else in the codebase bounds the roster against that
// limit, so raising kMaxDisplayedAccountCharacters past the point where a full roster stops fitting
// must fail here rather than on a player.
TEST(AccountManagement, RendersAFullRosterWithinTheOutputBuffer)
{
    account::AccountData account_data = make_account();
    account_data.characters.clear();
    // Far more characters than any sane cap, so the roster always renders exactly the cap's worth
    // of rows. This holds the test correct for every cap value: below the cliff it renders `cap`
    // rows and fits, above it renders `cap` rows and overflows, and a cap beyond this count still
    // overflows because this many rows alone already exceed the buffer.
    for (int index = 1; index <= 1000; ++index)
        account_data.characters.push_back("character" + std::to_string(index));

    // No account exists at this root, so every row takes the "[ ?? ???]" fallback -- which is
    // exactly as wide as a populated row carrying a three-character race abbreviation, and so is
    // the widest a row can render.
    const std::string prompt = account::format_account_character_prompt(".", account_data, account::RosterSort::Account, account::RosterFilter::None);

    EXPECT_LT(prompt.size(), static_cast<size_t>(LARGE_BUFSIZE))
        << "A full roster no longer fits the descriptor output buffer; output would be silently "
           "discarded. Lower kMaxDisplayedAccountCharacters or shorten the row format.";

    // The Side sort adds a section header and inter-section blank lines on top of every row, so it
    // renders slightly larger than the unsectioned Account sort above -- and that guard alone says
    // nothing about whether the sectioned path also fits. Every character here is unreadable (no
    // account exists at this root), so this also renders as a single "-- Unavailable --" section,
    // the worst case for header overhead relative to row count.
    const std::string side_sorted_prompt = account::format_account_character_prompt(
        ".", account_data, account::RosterSort::Side, account::RosterFilter::None);

    EXPECT_LT(side_sorted_prompt.size(), static_cast<size_t>(LARGE_BUFSIZE))
        << "A full Side-sorted roster no longer fits the descriptor output buffer; output would be "
           "silently discarded. Lower kMaxDisplayedAccountCharacters or shorten the row/header format.";
}

TEST(AccountManagement, BlocksAndUnblocksAccountsWithAuditMetadata)
{
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

TEST(AccountManagement, ResetsPasswordsAndTracksResetMetadata)
{
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

TEST(AccountManagement, ResolvesBucketedAccountPathsLikePlayerFiles)
{
    EXPECT_EQ(account::account_bucket_for_name("alpha"), "A-E");
    EXPECT_EQ(account::account_bucket_for_name("jester"), "F-J");
    EXPECT_EQ(account::account_bucket_for_name("morgoth"), "K-O");
    EXPECT_EQ(account::account_bucket_for_name("samwise"), "P-T");
    EXPECT_EQ(account::account_bucket_for_name("ulmo"), "U-Z");
    EXPECT_EQ(account::account_bucket_for_name("1account"), "ZZZ");
}

TEST(AccountManagement, BuildsBucketedJsonFilePathsUsingNormalizedName)
{
    EXPECT_EQ(account::account_file_path("/game/lib", " Player@Example.com "),
        "/game/lib/accounts/P-T/player@example.com/account.json");
}

TEST(AccountManagement, BuildsLegacyCharacterPathsFromCharacterName)
{
    EXPECT_EQ(account::legacy_player_file_path("/game/lib", " Aragorn "),
        "/game/lib/players/A-E/aragorn");
    EXPECT_EQ(account::legacy_object_file_path("/game/lib", " Aragorn "),
        "/game/lib/plrobjs/A-E/aragorn.obj");
    EXPECT_EQ(account::legacy_exploits_file_path("/game/lib", " Aragorn "),
        "/game/lib/exploits/A-E/aragorn.exploits");
}

TEST(AccountManagement, SerializesAndDeserializesAccountJsonRoundTrip)
{
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

TEST(AccountManagement, PopulatesDefaultCharacterLinkFileNamesDuringAccountJsonRoundTrip)
{
    account::AccountData original_account = make_account();
    original_account.characters = { "aragorn" };
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

TEST(AccountManagement, FormatsAccountSummariesIncludingLinkedCharacters)
{
    account::AccountData account_data = make_account();

    const std::string summary = account::format_account_summary(account_data);
    const std::string expected_summary = std::string("Account email: player@example.com\n\r")
        + "Internal name: alpha-admin\n\r"
        + "Email verified: yes\n\r"
        + "Verified by: VerifierAdmin\n\r"
        + "Verified at: 2023-09-18 01:20:00 UTC\n\r"
        + "Blocked: yes\n\r"
        + "Blocked by: AdminUser\n\r"
        + "Block reason: Testing block reason\n\r"
        + "Blocked at: 2023-11-14 22:13:20 UTC\n\r"
        + "Created: 2023-07-22 04:26:40 UTC\n\r"
        + "Updated: 2023-11-14 22:13:21 UTC\n\r"
        + "Password reset by: SecurityAdmin\n\r"
        + "Password reset at: 2023-11-14 22:13:22 UTC\n\r"
        + "Characters (2): Aragorn, Legolas\n\r";

    EXPECT_EQ(summary, expected_summary);
    EXPECT_NE(summary.find("Verified at: 2023-09-18 01:20:00 UTC\n\r"), std::string::npos);
    EXPECT_NE(summary.find("Created: 2023-07-22 04:26:40 UTC\n\r"), std::string::npos);
    EXPECT_NE(summary.find("Updated: 2023-11-14 22:13:21 UTC\n\r"), std::string::npos);
    EXPECT_EQ(summary.find("1695000000"), std::string::npos);
    EXPECT_EQ(summary.find("1700000000"), std::string::npos);
    EXPECT_EQ(summary.find("1690000000"), std::string::npos);
    EXPECT_EQ(summary.find("1700000001"), std::string::npos);
    EXPECT_EQ(summary.find("1700000002"), std::string::npos);
}

TEST(AccountManagement, FormatsOutOfRangeSummaryTimestampsAsInvalid)
{
    account::AccountData account_data = make_account();
    account_data.created_at = std::numeric_limits<long>::max();
    account_data.updated_at = std::numeric_limits<long>::max();

    const std::string summary = account::format_account_summary(account_data);

    EXPECT_NE(summary.find("Created: Invalid\n\r"), std::string::npos);
    EXPECT_NE(summary.find("Updated: Invalid\n\r"), std::string::npos);
}

TEST(AccountManagement, FormatsPendingVerificationWindowWithHumanReadableDates)
{
    account::AccountData account_data = make_account();
    account_data.email_verified = false;
    account_data.email_verified_by.clear();
    account_data.email_verified_at = 0;
    account_data.verification_code_hash = "pending";
    account_data.verification_code_sent_at = 1701000000;
    account_data.verification_code_expires_at = 1701000900;
    account_data.blocked = false;
    account_data.block_reason.clear();
    account_data.blocked_by.clear();
    account_data.blocked_at = 0;
    account_data.password_reset_by.clear();
    account_data.password_reset_at = 0;

    const std::string summary = account::format_account_summary(account_data);

    EXPECT_NE(summary.find("Email verified: no\n\r"), std::string::npos);
    EXPECT_EQ(summary.find("Verified at:"), std::string::npos);
    EXPECT_NE(summary.find("Verification code sent at: 2023-11-26 12:00:00 UTC\n\r"), std::string::npos);
    EXPECT_NE(summary.find("Verification code expires at: 2023-11-26 12:15:00 UTC\n\r"), std::string::npos);
    EXPECT_EQ(summary.find("1701000000"), std::string::npos);
    EXPECT_EQ(summary.find("1701000900"), std::string::npos);
    EXPECT_EQ(summary.find("Blocked at:"), std::string::npos);
    EXPECT_EQ(summary.find("Password reset by:"), std::string::npos);
    EXPECT_EQ(summary.find("Password reset at:"), std::string::npos);
}

TEST(AccountManagement, OmitsUnsetBlockAndPasswordResetMetadataFromSummary)
{
    account::AccountData account_data = make_account();
    account_data.blocked = false;
    account_data.block_reason.clear();
    account_data.blocked_by.clear();
    account_data.blocked_at = 0;
    account_data.password_reset_by.clear();
    account_data.password_reset_at = 0;

    const std::string summary = account::format_account_summary(account_data);

    EXPECT_NE(summary.find("Blocked: no\n\r"), std::string::npos);
    EXPECT_EQ(summary.find("Blocked by:"), std::string::npos);
    EXPECT_EQ(summary.find("Block reason:"), std::string::npos);
    EXPECT_EQ(summary.find("Blocked at:"), std::string::npos);
    EXPECT_EQ(summary.find("Password reset by:"), std::string::npos);
    EXPECT_EQ(summary.find("Password reset at:"), std::string::npos);
}

TEST(AccountManagement, ReadsAccountFileByIdentifierUsingEmailOrInternalName)
{
    TemporaryDirectory temp_directory;
    std::string error_message;
    account::AccountData created_account;
    ASSERT_TRUE(account::create_account(
        temp_directory.path(),
        "alpha-admin",
        "player@example.com",
        "ValidPass1",
        1700010200,
        &created_account,
        &error_message))
        << error_message;

    account::AccountData loaded_by_email;
    ASSERT_TRUE(account::read_account_file_by_identifier(
        temp_directory.path(),
        "  PLAYER@Example.com  ",
        &loaded_by_email,
        &error_message))
        << error_message;
    EXPECT_EQ(loaded_by_email.account_name, "alpha-admin");
    EXPECT_EQ(loaded_by_email.normalized_email, "player@example.com");

    account::AccountData loaded_by_name;
    ASSERT_TRUE(account::read_account_file_by_identifier(
        temp_directory.path(),
        "alpha-admin",
        &loaded_by_name,
        &error_message))
        << error_message;
    EXPECT_EQ(loaded_by_name.account_name, "alpha-admin");
    EXPECT_EQ(loaded_by_name.normalized_email, "player@example.com");
}

TEST(AccountManagement, FormatsCharacterNamesForDisplayByOnlyUppercasingTheFirstByte)
{
    EXPECT_EQ(account::format_character_name_for_display(""), "");
    EXPECT_EQ(account::format_character_name_for_display("aragorn"), "Aragorn");
    EXPECT_EQ(account::format_character_name_for_display("mCduck"), "MCduck");
}

TEST(AccountManagement, FormatsCharacterPromptWithLinkedCharacterList)
{
    TemporaryDirectory temp_directory;
    ScopedWorkingDirectory working_directory(temp_directory.path());
    ASSERT_EQ(mkdir("accounts", 0700), 0);
    ASSERT_EQ(mkdir("accounts/A-E", 0700), 0);

    account::AccountData account_data = make_account();
    std::string error_message;
    ASSERT_TRUE(account::create_account(".", account_data.account_name, account_data.normalized_email, "ValidPass1", 1700010200, nullptr, &error_message)) << error_message;
    char_file_u aragorn = make_stored_character("aragorn");
    aragorn.level = 50;
    aragorn.race = RACE_WOOD;
    ASSERT_TRUE(account::write_account_character_file(".", account_data.account_name, aragorn, &error_message)) << error_message;

    char_file_u legolas = make_stored_character("legolas");
    legolas.level = 45;
    legolas.race = RACE_HUMAN;
    ASSERT_TRUE(account::write_account_character_file(".", account_data.account_name, legolas, &error_message)) << error_message;

    const std::string prompt = account::format_account_character_prompt(".", account_data, account::RosterSort::Account, account::RosterFilter::None);

    EXPECT_EQ(prompt,
        "\n\rLinked characters for your account:\n\r"
        "1) [ 50 WdE] Aragorn     2) [ 45 Hum] Legolas     \n\r"
        "\n\r2 characters displayed.\n\r"
        "\n\rSort: (A)-Z  (L)evel  ra(C)e  (S)ide      Show only: (W)arrior (R)anger (T)mystic (M)age\n\r"
        "0) Back to Account Menu.\n\r"
        "\n\rCharacter number or name: ");
}

TEST(AccountManagement, RejectsMalformedJsonInput)
{
    account::AccountData parsed_account;
    std::string error_message;

    EXPECT_FALSE(account::deserialize_account_from_json("{\"version\":1", &parsed_account, &error_message));
    EXPECT_FALSE(error_message.empty()) << "Expected malformed JSON to report a parsing failure.";
}

TEST(AccountManagement, RejectsUnsafeCharacterNamesInStoredAccountJson)
{
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

TEST(AccountManagement, PersistsAccountsToBucketedJsonFilesAndReadsThemBack)
{
    TemporaryDirectory temp_directory;
    const account::AccountData original_account = make_account();
    std::string error_message;

    ASSERT_TRUE(account::write_account_file(temp_directory.path(), original_account, &error_message)) << error_message;

    account::AccountData loaded_account;
    ASSERT_TRUE(account::read_account_file(temp_directory.path(), "ALPHA-ADMIN", &loaded_account, &error_message)) << error_message;

    EXPECT_EQ(loaded_account.account_name, "alpha-admin");
    EXPECT_EQ(loaded_account.characters, std::vector<std::string>({ "aragorn", "legolas" }));
    EXPECT_EQ(loaded_account.block_reason, original_account.block_reason);

    struct stat file_info { };
    ASSERT_EQ(stat(account::account_file_path(temp_directory.path(), original_account.normalized_email).c_str(), &file_info), 0)
        << "Expected write_account_file() to materialize the final account JSON file.";

    EXPECT_NE(stat((account::account_file_path(temp_directory.path(), original_account.normalized_email) + ".tmp").c_str(), &file_info), 0)
        << "Expected temporary files to be cleaned up after a successful atomic write.";
}

TEST(AccountManagement, RewritesLegacyFlatAccountFilesIntoEmailRootedLayout)
{
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

    struct stat file_info { };
    EXPECT_EQ(stat(account::account_file_path(temp_directory.path(), original_account.normalized_email).c_str(), &file_info), 0);
    EXPECT_NE(stat(legacy_path.c_str(), &file_info), 0);

    account::AccountData looked_up_account;
    ASSERT_TRUE(account::read_account_file_by_email(temp_directory.path(), "player@example.com", &looked_up_account, &error_message)) << error_message;
    EXPECT_EQ(looked_up_account.block_reason, "updated reason");
}

TEST(AccountManagement, ReportsMissingAccountFilesClearly)
{
    TemporaryDirectory temp_directory;
    account::AccountData loaded_account;
    std::string error_message;

    EXPECT_FALSE(account::read_account_file(temp_directory.path(), "missing-account", &loaded_account, &error_message));
    EXPECT_NE(error_message.find("Failed to open account file"), std::string::npos);
}

TEST(AccountManagement, RejectsUnsafeAccountNamesBeforeFilesystemAccess)
{
    TemporaryDirectory temp_directory;
    account::AccountData loaded_account;
    std::string error_message;

    EXPECT_FALSE(account::read_account_file(temp_directory.path(), "../escape", &loaded_account, &error_message));
    EXPECT_NE(error_message.find("Account name"), std::string::npos);
}

TEST(AccountManagement, CreatesAccountsOnDiskAndRejectsDuplicates)
{
    TemporaryDirectory temp_directory;
    account::AccountData created_account;
    std::string error_message;

    ASSERT_TRUE(account::create_account(temp_directory.path(), "alpha-admin", "player@example.com", "ValidPass1", 1700004444, &created_account, &error_message)) << error_message;
    EXPECT_EQ(created_account.account_name, "alpha-admin");
    EXPECT_TRUE(account::verify_password("ValidPass1", created_account.password_hash));

    EXPECT_FALSE(account::create_account(temp_directory.path(), "alpha-admin", "other@example.com", "ValidPass1", 1700004445, nullptr, &error_message));
    EXPECT_NE(error_message.find("already exists"), std::string::npos);
}

TEST(AccountManagement, CreatesAndFindsAccountsByEmailAddress)
{
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

TEST(AccountManagement, AuthenticatesAccountsFromDiskAndRejectsBlockedAccounts)
{
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

TEST(AccountManagement, AuthenticatesAccountsByEmailAddress)
{
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

TEST(AccountManagement, SupportsAccountPasswordsLongerThanLegacyCharacterLimit)
{
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

TEST(AccountManagement, RejectsAmbiguousDuplicateEmailRecords)
{
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

TEST(AccountManagement, ReadAccountFileByEmailRejectsConflictingLegacyAndRootedDuplicateRecords)
{
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

TEST(AccountManagement, CreatesAccountForEmailFailsWhenLegacyFlatRecordAlreadyExistsForSameNormalizedEmail)
{
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

TEST(AccountManagement, RefusesToOverwriteDifferentAccountAtTargetEmailPath)
{
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

TEST(AccountManagement, FindsEmailBackedAccountEvenIfUnrelatedAccountFileIsCorrupt)
{
    TemporaryDirectory temp_directory;
    std::string error_message;
    ASSERT_TRUE(account::create_account_for_email(temp_directory.path(), "player@example.com", "ValidPass1", 1700005800, nullptr, &error_message)) << error_message;

    ASSERT_EQ(mkdir((temp_directory.path() + "/accounts/ZZZ").c_str(), 0700), 0);
    write_text_file(temp_directory.path() + "/accounts/ZZZ/corrupt.json", "{bad-json");

    account::AccountData looked_up_account;
    ASSERT_TRUE(account::read_account_file_by_email(temp_directory.path(), "player@example.com", &looked_up_account, &error_message)) << error_message;
    EXPECT_EQ(looked_up_account.normalized_email, "player@example.com");
}

TEST(AccountManagement, ReportsMissingEmailEvenIfUnrelatedAccountFileIsCorrupt)
{
    TemporaryDirectory temp_directory;
    std::string error_message;
    ASSERT_TRUE(account::create_account_for_email(temp_directory.path(), "player@example.com", "ValidPass1", 1700005801, nullptr, &error_message)) << error_message;

    ASSERT_EQ(mkdir((temp_directory.path() + "/accounts/ZZZ").c_str(), 0700), 0);
    write_text_file(temp_directory.path() + "/accounts/ZZZ/corrupt.json", "{bad-json");

    account::AccountData looked_up_account;
    EXPECT_FALSE(account::read_account_file_by_email(temp_directory.path(), "new@example.com", &looked_up_account, &error_message));
    EXPECT_EQ(error_message, "No account exists for that email address.");
}

TEST(AccountManagement, FailsClosedWhenCreatingAccountIfAnyStoredRecordIsUnreadable)
{
    TemporaryDirectory temp_directory;
    std::string error_message;
    ASSERT_TRUE(account::create_account_for_email(temp_directory.path(), "player@example.com", "ValidPass1", 1700005802, nullptr, &error_message)) << error_message;

    ASSERT_EQ(mkdir((temp_directory.path() + "/accounts/ZZZ").c_str(), 0700), 0);
    write_text_file(temp_directory.path() + "/accounts/ZZZ/corrupt.json", "{bad-json");

    account::AccountData created_account;
    EXPECT_FALSE(account::create_account_for_email(temp_directory.path(), "new@example.com", "ValidPass1", 1700005803, &created_account, &error_message));
    EXPECT_EQ(error_message, "Existing account records could not be read safely.");
}

TEST(AccountManagement, SupportsAdminLinkBlockUnblockAndPasswordResetOnDisk)
{
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

TEST(AccountManagement, PreventsLinkingOneCharacterToMultipleAccounts)
{
    TemporaryDirectory temp_directory;
    std::string error_message;

    ASSERT_TRUE(account::create_account(temp_directory.path(), "alpha-admin", "player@example.com", "ValidPass1", 1700007000, nullptr, &error_message)) << error_message;
    ASSERT_TRUE(account::create_account(temp_directory.path(), "beta-admin", "other@example.com", "ValidPass1", 1700007001, nullptr, &error_message)) << error_message;
    ASSERT_TRUE(account::admin_link_character(temp_directory.path(), "alpha-admin", "Aragorn", 1700007002, nullptr, &error_message)) << error_message;

    EXPECT_FALSE(account::admin_link_character(temp_directory.path(), "beta-admin", "aragorn", 1700007003, nullptr, &error_message));
    EXPECT_NE(error_message.find("already linked to account"), std::string::npos);
}

TEST(AccountManagement, FindsLinkedCharacterOwnerAccount)
{
    TemporaryDirectory temp_directory;
    std::string error_message;

    ASSERT_TRUE(account::create_account(temp_directory.path(), "alpha-admin", "player@example.com", "ValidPass1", 1700007004, nullptr, &error_message)) << error_message;
    ASSERT_TRUE(account::admin_link_character(temp_directory.path(), "alpha-admin", "Aragorn", 1700007005, nullptr, &error_message)) << error_message;

    std::string owner_account_name;
    ASSERT_TRUE(account::find_linked_character_owner_account(temp_directory.path(), "aragorn", &owner_account_name, &error_message)) << error_message;
    EXPECT_EQ(owner_account_name, "alpha-admin");
}

TEST(AccountManagement, RejectsAmbiguousLinkedCharacterOwnership)
{
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

TEST(AccountManagement, LinksAndMigratesCharacterAfterAuthenticatingAccount)
{
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

TEST(AccountManagement, LinksAndMigratesTwoLetterLegacyCharacter)
{
    TemporaryDirectory temp_directory;
    ASSERT_EQ(mkdir((temp_directory.path() + "/players").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/players/P-T").c_str(), 0700), 0);
    write_valid_legacy_player_file(temp_directory.path(), make_stored_character("Pi"));

    std::string error_message;
    ASSERT_TRUE(account::create_account(temp_directory.path(), "alpha-admin", "player@example.com", "ValidPass1", 1700012222, nullptr, &error_message)) << error_message;
    ASSERT_TRUE(account::admin_verify_email(temp_directory.path(), "alpha-admin", "VerifierAdmin", 1700012222, nullptr, &error_message)) << error_message;

    account::AccountData linked_account;
    account::CharacterMigrationData migration;
    ASSERT_TRUE(account::link_and_migrate_character(temp_directory.path(), "alpha-admin", "ValidPass1", "Pi", 1700012223, &linked_account, &migration, &error_message)) << error_message;
    EXPECT_TRUE(account::account_has_character(linked_account, "pi"));
    EXPECT_EQ(migration.character_name, "pi");
    EXPECT_TRUE(migration.player_file.present);

    // The record that now lists "pi" has to read back, or the whole account becomes unreadable.
    account::AccountData reread_account;
    ASSERT_TRUE(account::read_account_file(temp_directory.path(), "alpha-admin", &reread_account, &error_message)) << error_message;
    EXPECT_TRUE(account::account_has_character(reread_account, "pi"));

    std::string owner_account_name;
    ASSERT_TRUE(account::find_linked_character_owner_account(temp_directory.path(), "pi", &owner_account_name, &error_message)) << error_message;
    EXPECT_EQ(owner_account_name, "alpha-admin");

    // Renaming INTO a short name is a new name, so the creation minimum still applies.
    EXPECT_FALSE(account::admin_rename_linked_character(temp_directory.path(), "alpha-admin", "pi", "ab", 1700012224, nullptr, &error_message));

    ASSERT_TRUE(account::admin_delete_linked_character(temp_directory.path(), "alpha-admin", "pi", 1700012225, &reread_account, &error_message)) << error_message;
    EXPECT_FALSE(account::account_has_character(reread_account, "pi"));
}

TEST(AccountManagement, AdminLinksAndMigratesTwoLetterLegacyCharacter)
{
    TemporaryDirectory temp_directory;
    ASSERT_EQ(mkdir((temp_directory.path() + "/players").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/players/A-E").c_str(), 0700), 0);
    write_valid_legacy_player_file(temp_directory.path(), make_stored_character("El"));

    std::string error_message;
    ASSERT_TRUE(account::create_account(temp_directory.path(), "alpha-admin", "player@example.com", "ValidPass1", 1700012230, nullptr, &error_message)) << error_message;

    account::AccountData linked_account;
    account::CharacterMigrationData migration;
    ASSERT_TRUE(account::admin_link_and_migrate_character(temp_directory.path(), "alpha-admin", "El", 1700012231, &linked_account, &migration, &error_message)) << error_message;
    EXPECT_TRUE(account::account_has_character(linked_account, "el"));
    EXPECT_TRUE(migration.player_file.present);
}

TEST(AccountManagement, DoesNotLeaveCharacterLinkedWhenMigrationFails)
{
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

TEST(AccountManagement, BuildsAccountLinkedCharacterSnapshotPaths)
{
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

TEST(AccountManagement, RefusesToWriteACharacterFileItCannotReadBack)
{
    // The write validates struct -> CharacterData -> struct and never parses the TEXT it produces.
    // Skills are keyed by name and the skill table carries two entries with the same name, so a
    // character with values in both emits the same key twice and the reader refuses the whole file.
    // Written over the previous save, that is the character's only copy replaced by one nothing can
    // load -- and the player is told the character does not exist.
    TemporaryDirectory temp_directory;
    std::string error_message;

    ASSERT_TRUE(account::create_account(temp_directory.path(), "alpha-admin", "player@example.com", "ValidPass1", 1700007776, nullptr, &error_message)) << error_message;
    ASSERT_TRUE(account::admin_link_character(temp_directory.path(), "alpha-admin", "aragorn", 1700007777, nullptr, &error_message)) << error_message;

    const char_file_u original = make_stored_character("aragorn");
    ASSERT_TRUE(account::write_account_character_file(temp_directory.path(), "alpha-admin", original, &error_message)) << error_message;

    // Every skill practised, rather than naming the duplicated pair by index: the table is world
    // data and the pair moves.
    char_file_u every_skill = original;
    every_skill.level = 50;
    for (int skill = 0; skill < MAX_SKILLS; ++skill)
        every_skill.skills[skill] = 1;

    account_errors::clear();
    EXPECT_FALSE(account::write_account_character_file(temp_directory.path(), "alpha-admin", every_skill, &error_message))
        << "a file the reader will refuse must not replace the character's only copy";

    // A refused save is the most dangerous thing on the list: the player keeps playing and their
    // progress is not being written. It must be answerable in game, not only in the log.
    const std::vector<account_errors::Entry> recorded = account_errors::recent(10);
    ASSERT_EQ(recorded.size(), 1u);
    EXPECT_EQ(recorded[0].source, account_errors::Source::Save);
    EXPECT_EQ(recorded[0].character, "aragorn");
    EXPECT_EQ(recorded[0].account, "alpha-admin");
    account_errors::clear();

    char_file_u loaded {};
    std::string read_error;
    ASSERT_TRUE(account::read_account_character_file(temp_directory.path(), "alpha-admin", "aragorn", &loaded, &read_error))
        << "the character that was on disk must still load: " << read_error;
    EXPECT_EQ(loaded.level, original.level)
        << "and it must still be the character that was written, not the one that was refused";
}

TEST(AccountManagement, StillSavesACharacterHoldingOnlyOneHalfOfACollidingPair)
{
    // The guard must refuse the character that would produce a duplicate key and nobody else. One
    // side of the pair writes the key once, which reads back fine -- refusing it would stop a real
    // player's saves over nothing.
    TemporaryDirectory temp_directory;
    std::string error_message;

    ASSERT_TRUE(account::create_account(temp_directory.path(), "alpha-admin", "player@example.com", "ValidPass1", 1700007776, nullptr, &error_message)) << error_message;
    ASSERT_TRUE(account::admin_link_character(temp_directory.path(), "alpha-admin", "aragorn", 1700007777, nullptr, &error_message)) << error_message;

    char_file_u one_side = make_stored_character("aragorn");
    one_side.skills[125] = 40;
    ASSERT_TRUE(account::write_account_character_file(temp_directory.path(), "alpha-admin", one_side, &error_message))
        << error_message;

    char_file_u loaded {};
    ASSERT_TRUE(account::read_account_character_file(temp_directory.path(), "alpha-admin", "aragorn", &loaded, &error_message)) << error_message;
    EXPECT_EQ(loaded.skills[125], 40) << "and the value survives the round trip";
}

TEST(AccountManagement, WritesAndReadsAccountNativeCharacterFile)
{
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
    EXPECT_EQ(loaded.specials2.tactics, original.specials2.tactics);
    EXPECT_EQ(loaded.specials2.shooting, original.specials2.shooting);
    EXPECT_EQ(loaded.specials2.casting, original.specials2.casting);
    EXPECT_EQ(loaded.specials2.two_handed, original.specials2.two_handed);
    EXPECT_EQ(loaded.profs.specialization, original.profs.specialization);
    EXPECT_EQ(loaded.points.gold, original.points.gold);
    EXPECT_EQ(loaded.skills[5], original.skills[5]);
    EXPECT_EQ(loaded.talks[2], original.talks[2]);
    EXPECT_EQ(loaded.profs.color_mask, original.profs.color_mask);
    EXPECT_EQ(loaded.profs.colors[COLOR_CHAT], original.profs.colors[COLOR_CHAT]);
    EXPECT_EQ(loaded.profs.colors[COLOR_ROOM], original.profs.colors[COLOR_ROOM]);
    EXPECT_EQ(loaded.profs.colors[COLOR_OBJ], original.profs.colors[COLOR_OBJ]);
    EXPECT_EQ(loaded.profs.colors[COLOR_MAGIC], original.profs.colors[COLOR_MAGIC]);
    EXPECT_EQ(loaded.profs.colors[COLOR_WEATHER], original.profs.colors[COLOR_WEATHER]);
    EXPECT_EQ(loaded.profs.color_settings[COLOR_MAGIC].foreground.mode, COLOR_VALUE_TRUECOLOR);
    EXPECT_EQ(loaded.profs.color_settings[COLOR_MAGIC].foreground.ansi, original.profs.color_settings[COLOR_MAGIC].foreground.ansi);
    EXPECT_EQ(loaded.profs.color_settings[COLOR_MAGIC].foreground.red, original.profs.color_settings[COLOR_MAGIC].foreground.red);
    EXPECT_EQ(loaded.profs.color_settings[COLOR_MAGIC].foreground.green, original.profs.color_settings[COLOR_MAGIC].foreground.green);
    EXPECT_EQ(loaded.profs.color_settings[COLOR_MAGIC].foreground.blue, original.profs.color_settings[COLOR_MAGIC].foreground.blue);
    EXPECT_EQ(loaded.profs.color_settings[COLOR_WEATHER].background.mode, COLOR_VALUE_TRUECOLOR);
    EXPECT_EQ(loaded.profs.color_settings[COLOR_WEATHER].background.ansi, original.profs.color_settings[COLOR_WEATHER].background.ansi);
    EXPECT_EQ(loaded.profs.color_settings[COLOR_WEATHER].background.red, original.profs.color_settings[COLOR_WEATHER].background.red);
    EXPECT_EQ(loaded.profs.color_settings[COLOR_WEATHER].background.green, original.profs.color_settings[COLOR_WEATHER].background.green);
    EXPECT_EQ(loaded.profs.color_settings[COLOR_WEATHER].background.blue, original.profs.color_settings[COLOR_WEATHER].background.blue);
    EXPECT_EQ(loaded.profs.prof_level[PROF_WARRIOR], original.profs.prof_level[PROF_WARRIOR]);
    EXPECT_EQ(loaded.profs.prof_coof[PROF_WARRIOR], original.profs.prof_coof[PROF_WARRIOR]);
}

TEST(AccountManagement, WritesAndReadsAccountNativeCharacterFilePreservesCustomColorSettings)
{
    TemporaryDirectory temp_directory;
    std::string error_message;

    ASSERT_TRUE(account::create_account(temp_directory.path(), "alpha-admin", "player@example.com", "ValidPass1", 1700007776, nullptr, &error_message)) << error_message;
    ASSERT_TRUE(account::admin_link_character(temp_directory.path(), "alpha-admin", "aragorn", 1700007777, nullptr, &error_message)) << error_message;

    const char_file_u original = make_stored_character("aragorn");
    ASSERT_TRUE(account::write_account_character_file(temp_directory.path(), "alpha-admin", original, &error_message)) << error_message;

    const std::string stored_json = read_file_contents(account::account_character_player_path(temp_directory.path(), "alpha-admin", "aragorn"));
    EXPECT_NE(stored_json.find("\"color_mask\": 6636321"), std::string::npos);
    EXPECT_NE(stored_json.find("\"colors\": {"), std::string::npos);
    EXPECT_NE(stored_json.find("\"chat\": {\"foreground\": {\"mode\": \"ansi16\", \"value\": 4}, \"background\": {\"mode\": \"default\"}}"), std::string::npos);
    EXPECT_NE(stored_json.find("\"roomname\": {\"foreground\": {\"mode\": \"ansi16\", \"value\": 3}, \"background\": {\"mode\": \"default\"}}"), std::string::npos);
    EXPECT_NE(stored_json.find("\"object\": {\"foreground\": {\"mode\": \"ansi16\", \"value\": 6}, \"background\": {\"mode\": \"default\"}}"), std::string::npos);
    EXPECT_NE(stored_json.find("\"magic\": {\"foreground\": {\"mode\": \"truecolor\", \"value\": 11, \"r\": 80, \"g\": 90, \"b\": 255}, \"background\": {\"mode\": \"default\"}}"), std::string::npos);
    EXPECT_NE(stored_json.find("\"weather\": {\"foreground\": {\"mode\": \"ansi16\", \"value\": 10}, \"background\": {\"mode\": \"truecolor\", \"value\": 4, \"r\": 10, \"g\": 20, \"b\": 35}}"), std::string::npos);

    char_file_u loaded {};
    ASSERT_TRUE(account::read_account_character_file(temp_directory.path(), "alpha-admin", "aragorn", &loaded, &error_message)) << error_message;

    EXPECT_EQ(loaded.profs.color_mask, original.profs.color_mask);
    EXPECT_EQ(loaded.profs.colors[COLOR_CHAT], original.profs.colors[COLOR_CHAT]);
    EXPECT_EQ(loaded.profs.colors[COLOR_ROOM], original.profs.colors[COLOR_ROOM]);
    EXPECT_EQ(loaded.profs.colors[COLOR_OBJ], original.profs.colors[COLOR_OBJ]);
    EXPECT_EQ(loaded.profs.colors[COLOR_MAGIC], original.profs.colors[COLOR_MAGIC]);
    EXPECT_EQ(loaded.profs.colors[COLOR_WEATHER], original.profs.colors[COLOR_WEATHER]);
    EXPECT_EQ(loaded.profs.color_settings[COLOR_MAGIC].foreground.mode, COLOR_VALUE_TRUECOLOR);
    EXPECT_EQ(loaded.profs.color_settings[COLOR_MAGIC].foreground.red, original.profs.color_settings[COLOR_MAGIC].foreground.red);
    EXPECT_EQ(loaded.profs.color_settings[COLOR_MAGIC].foreground.green, original.profs.color_settings[COLOR_MAGIC].foreground.green);
    EXPECT_EQ(loaded.profs.color_settings[COLOR_MAGIC].foreground.blue, original.profs.color_settings[COLOR_MAGIC].foreground.blue);
    EXPECT_EQ(loaded.profs.color_settings[COLOR_WEATHER].background.mode, COLOR_VALUE_TRUECOLOR);
    EXPECT_EQ(loaded.profs.color_settings[COLOR_WEATHER].background.red, original.profs.color_settings[COLOR_WEATHER].background.red);
    EXPECT_EQ(loaded.profs.color_settings[COLOR_WEATHER].background.green, original.profs.color_settings[COLOR_WEATHER].background.green);
    EXPECT_EQ(loaded.profs.color_settings[COLOR_WEATHER].background.blue, original.profs.color_settings[COLOR_WEATHER].background.blue);
}

TEST(AccountManagement, RejectsAccountNativeCharacterFileWithOutOfRangeColorValue)
{
    TemporaryDirectory temp_directory;
    std::string error_message;

    ASSERT_TRUE(account::create_account(temp_directory.path(), "alpha-admin", "player@example.com", "ValidPass1", 1700007776, nullptr, &error_message)) << error_message;
    ASSERT_TRUE(account::admin_link_character(temp_directory.path(), "alpha-admin", "aragorn", 1700007777, nullptr, &error_message)) << error_message;

    const char_file_u original = make_stored_character("aragorn");
    ASSERT_TRUE(account::write_account_character_file(temp_directory.path(), "alpha-admin", original, &error_message)) << error_message;

    const std::string path = account::account_character_player_path(temp_directory.path(), "alpha-admin", "aragorn");
    std::string stored_json = read_file_contents(path);
    stored_json.replace(stored_json.find("\"chat\": {\"foreground\": {\"mode\": \"ansi16\", \"value\": 4}, \"background\": {\"mode\": \"default\"}}"),
        std::strlen("\"chat\": {\"foreground\": {\"mode\": \"ansi16\", \"value\": 4}, \"background\": {\"mode\": \"default\"}}"),
        "\"chat\": {\"foreground\": {\"mode\": \"ansi16\", \"value\": 200}, \"background\": {\"mode\": \"default\"}}");
    write_text_file(path, stored_json);

    char_file_u loaded {};
    EXPECT_FALSE(account::read_account_character_file(temp_directory.path(), "alpha-admin", "aragorn", &loaded, &error_message));
    EXPECT_NE(error_message.find("colors[1]"), std::string::npos);
}

TEST(AccountManagement, WritesAndReadsAccountNativeObjectFile)
{
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

TEST(AccountManagement, AccountNativeObjectWriteRejectsLegacyObjectFileWithoutFollowerSection)
{
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
    ASSERT_GT(object_bytes.size(), sizeof(follower_file_elem));
    object_bytes.erase(object_bytes.size() - sizeof(follower_file_elem));

    EXPECT_FALSE(account::write_account_object_file(temp_directory.path(), "alpha-admin", "aragorn", object_bytes, &error_message));
    EXPECT_NE(error_message.find("Truncated objects data while reading follower record"), std::string::npos);
    EXPECT_FALSE(account::account_object_file_exists(temp_directory.path(), "alpha-admin", "aragorn", nullptr));
}

TEST(AccountManagement, LinkedCharacterObjectRefreshAcceptsAnIdleSaveWithoutFollowerSection)
{
    // Every game save is copied into the account through write_linked_character_object_file. The
    // idle-out save (Crash_idlesave) writes no follower section -- historic behaviour -- and it is
    // also the save that destroys keys and NORENT items. When that copy was refused, objects.json
    // kept the previous 30-second save, and the next login handed the unrentable items back.
    TemporaryDirectory temp_directory;
    std::string error_message;

    ASSERT_TRUE(account::create_account(temp_directory.path(), "alpha-admin", "player@example.com", "ValidPass1", 1700007776, nullptr, &error_message)) << error_message;
    ASSERT_TRUE(account::admin_link_character(temp_directory.path(), "alpha-admin", "aragorn", 1700007777, nullptr, &error_message)) << error_message;

    const int kKeyVnum = 11376;
    const int kNorentVnum = 1138;
    const int kRingVnum = 6654;

    objects_json::ObjectSaveData autosave;
    autosave.rent.rentcode = RENT_CRASH;
    for (int vnum : { kKeyVnum, kNorentVnum, kRingVnum }) {
        autosave.objects.push_back(objects_json::ObjectRecord {});
        autosave.objects.back().item_number = vnum;
        autosave.objects.back().wear_pos = MAX_WEAR;
    }
    std::string autosave_bytes;
    ASSERT_TRUE(objects_json::object_save_data_to_binary(autosave, &autosave_bytes, &error_message)) << error_message;
    ASSERT_TRUE(account::write_linked_character_object_file(temp_directory.path(), "aragorn", autosave_bytes, &error_message)) << error_message;

    objects_json::ObjectSaveData idle_save;
    idle_save.rent.rentcode = RENT_TIMEDOUT;
    idle_save.objects.push_back(objects_json::ObjectRecord {});
    idle_save.objects.back().item_number = kRingVnum;
    idle_save.objects.back().wear_pos = MAX_WEAR;
    std::string idle_save_bytes;
    ASSERT_TRUE(objects_json::object_save_data_to_binary(idle_save, &idle_save_bytes, &error_message)) << error_message;
    ASSERT_GT(idle_save_bytes.size(), sizeof(follower_file_elem));
    idle_save_bytes.erase(idle_save_bytes.size() - sizeof(follower_file_elem)); // no follower section, as Crash_idlesave writes it

    EXPECT_TRUE(account::write_linked_character_object_file(temp_directory.path(), "aragorn", idle_save_bytes, &error_message)) << error_message;

    std::string loaded_bytes;
    ASSERT_TRUE(account::read_account_object_file(temp_directory.path(), "alpha-admin", "aragorn", &loaded_bytes, &error_message)) << error_message;
    objects_json::ObjectSaveData loaded;
    ASSERT_TRUE(objects_json::object_save_data_from_binary(loaded_bytes, &loaded, &error_message)) << error_message;
    EXPECT_EQ(loaded.rent.rentcode, RENT_TIMEDOUT) << "objects.json still holds the save before the idle-out";
    ASSERT_EQ(loaded.objects.size(), 1u) << "the key and the NORENT item would come back at the next login";
    EXPECT_EQ(loaded.objects[0].item_number, kRingVnum);
    EXPECT_TRUE(loaded.followers.empty());
}

TEST(AccountManagement, LinkedCharacterObjectRefreshStillRejectsAPartialFollowerRecord)
{
    // Only a follower section missing entirely is forgiven. A save cut off part-way through a
    // follower record is damaged, and must not replace the good copy.
    TemporaryDirectory temp_directory;
    std::string error_message;

    ASSERT_TRUE(account::create_account(temp_directory.path(), "alpha-admin", "player@example.com", "ValidPass1", 1700007776, nullptr, &error_message)) << error_message;
    ASSERT_TRUE(account::admin_link_character(temp_directory.path(), "alpha-admin", "aragorn", 1700007777, nullptr, &error_message)) << error_message;

    objects_json::ObjectSaveData good;
    good.rent.rentcode = RENT_CRASH;
    good.objects.push_back(objects_json::ObjectRecord {});
    good.objects.back().item_number = 1234;
    good.objects.back().wear_pos = MAX_WEAR;
    std::string good_bytes;
    ASSERT_TRUE(objects_json::object_save_data_to_binary(good, &good_bytes, &error_message)) << error_message;
    ASSERT_TRUE(account::write_linked_character_object_file(temp_directory.path(), "aragorn", good_bytes, &error_message)) << error_message;

    objects_json::ObjectSaveData with_follower = good;
    with_follower.followers.push_back(objects_json::FollowerData {});
    with_follower.followers.back().fol_vnum = 6601;
    std::string damaged_bytes;
    ASSERT_TRUE(objects_json::object_save_data_to_binary(with_follower, &damaged_bytes, &error_message)) << error_message;
    // Drop the terminator, the follower's object sentinel and half of its follower record.
    const size_t cut = sizeof(follower_file_elem) + sizeof(obj_file_elem) + sizeof(follower_file_elem) / 2;
    ASSERT_GT(damaged_bytes.size(), cut);
    damaged_bytes.erase(damaged_bytes.size() - cut);

    EXPECT_FALSE(account::write_linked_character_object_file(temp_directory.path(), "aragorn", damaged_bytes, &error_message));
    EXPECT_NE(error_message.find("Truncated objects data"), std::string::npos) << error_message;

    std::string loaded_bytes;
    ASSERT_TRUE(account::read_account_object_file(temp_directory.path(), "alpha-admin", "aragorn", &loaded_bytes, &error_message)) << error_message;
    objects_json::ObjectSaveData loaded;
    ASSERT_TRUE(objects_json::object_save_data_from_binary(loaded_bytes, &loaded, &error_message)) << error_message;
    ASSERT_EQ(loaded.objects.size(), 1u);
    EXPECT_EQ(loaded.objects[0].item_number, 1234) << "the good copy must be left in place";
}

TEST(AccountManagement, WritesDefaultAccountNativeObjectFile)
{
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
    EXPECT_EQ(loaded.rent.rentcode, RENT_CRASH);
}

TEST(AccountManagement, WritesAndReadsAccountNativeExploitFile)
{
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

TEST(AccountManagement, WritesDefaultAccountNativeExploitFile)
{
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

TEST(AccountManagement, RejectsStoredObjectPathThatDoesNotMatchExpectedCharacterFileName)
{
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

TEST(AccountManagement, RejectsStoredObjectPathWithAbsolutePath)
{
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

TEST(AccountManagement, WritesCanonicalObjectPathWhenSafeLegacyRelativePathIsStored)
{
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

TEST(AccountManagement, LeavesStoredObjectPathUnchangedWhenCanonicalObjectWriteFails)
{
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

TEST(AccountManagement, RejectsStoredCharacterPathThatDoesNotMatchExpectedCharacterFileName)
{
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

TEST(AccountManagement, RejectsOutOfRangeStoredCharacterSpecializationWithoutReplacingAccountNativeFile)
{
    TemporaryDirectory temp_directory;
    std::string error_message;

    ASSERT_TRUE(account::create_account(temp_directory.path(), "alpha-admin", "player@example.com", "ValidPass1", 1700007776, nullptr, &error_message)) << error_message;
    ASSERT_TRUE(account::admin_link_character(temp_directory.path(), "alpha-admin", "aragorn", 1700007777, nullptr, &error_message)) << error_message;

    char_file_u original_character = make_stored_character("aragorn");
    original_character.profs.specialization = PLRSPEC_DFND;
    ASSERT_TRUE(account::write_account_character_file(temp_directory.path(), "alpha-admin", original_character, &error_message)) << error_message;

    char_file_u stored_character = make_stored_character("aragorn");
    stored_character.profs.specialization = game_types::PS_Count;

    EXPECT_FALSE(account::write_account_character_file(temp_directory.path(), "alpha-admin", stored_character, &error_message));
    EXPECT_NE(error_message.find("state.specialization"), std::string::npos);

    char_file_u loaded_character {};
    ASSERT_TRUE(account::read_account_character_file(temp_directory.path(), "alpha-admin", "aragorn", &loaded_character, &error_message)) << error_message;
    EXPECT_EQ(loaded_character.profs.specialization, original_character.profs.specialization)
        << "Expected invalid stored specialization writes to leave the existing account-native character JSON unchanged.";
}

TEST(AccountManagement, RejectsStoredExploitsPathThatDoesNotMatchExpectedCharacterFileName)
{
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

TEST(AccountManagement, RemovesAccountNativeCharacterFile)
{
    TemporaryDirectory temp_directory;
    std::string error_message;

    ASSERT_TRUE(account::create_account(temp_directory.path(), "alpha-admin", "player@example.com", "ValidPass1", 1700007776, nullptr, &error_message)) << error_message;
    ASSERT_TRUE(account::admin_link_character(temp_directory.path(), "alpha-admin", "aragorn", 1700007777, nullptr, &error_message)) << error_message;
    ASSERT_TRUE(account::write_account_character_file(temp_directory.path(), "alpha-admin", make_stored_character("aragorn"), &error_message)) << error_message;
    ASSERT_TRUE(account::account_character_file_exists(temp_directory.path(), "alpha-admin", "aragorn", &error_message));

    ASSERT_TRUE(account::remove_account_character_file(temp_directory.path(), "alpha-admin", "aragorn", &error_message)) << error_message;
    EXPECT_FALSE(account::account_character_file_exists(temp_directory.path(), "alpha-admin", "aragorn", &error_message));
}

TEST(AccountManagement, MigratesLegacyCharacterFilesIntoAccountNativeAssetsWithoutPersistingSnapshotFile)
{
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

    struct stat file_info { };
    EXPECT_NE(stat(account::account_character_snapshot_path(temp_directory.path(), "alpha-admin", "aragorn").c_str(), &file_info), 0)
        << "Expected successful migration to rely on account-native assets without persisting a transitional snapshot file.";
}

TEST(AccountManagement, KeepsTheIndexCurrentWhenRetiringALegacyAccountFileFails)
{
    // The upsert sits after the two post-rename retirement steps, so a retirement that fails leaves
    // account.json in place on disk with the index still holding the pre-write keys. A character
    // linked by that write is then missing from the index, save_char resolves it as unlinked and
    // refuses the legacy fallback -- silent total save loss until the next reboot.
    TemporaryDirectory temp_directory;
    const std::string root = temp_directory.path();
    std::string error_message;

    account::AccountData account_data;
    ASSERT_TRUE(account::create_account(root, "alpha-admin", "player@example.com", "ValidPass1", 1700007776, &account_data, &error_message)) << error_message;

    // A legacy flat record that IS readable and IS this account's -- so the pre-commit guard on
    // retirement targets passes it -- but which cannot be unlinked, because its bucket has lost the
    // write bit. std::remove fails EACCES after the commit. (A record the guard can REJECT up front
    // is the separate case below; this one has to reach the post-commit bailout to be worth
    // anything.) The temp file and the rename both land inside the account's own email directory,
    // which stays writable, so only the retirement is affected.
    const std::string legacy_bucket = root + "/accounts/" + account::account_bucket_for_name("alpha-admin");
    const std::string legacy_flat_path = legacy_bucket + "/alpha-admin.json";
    mkdir(legacy_bucket.c_str(), 0700);
    write_text_file(legacy_flat_path,
        read_file_contents(root + "/accounts/" + account::account_bucket_for_name("player@example.com") + "/player@example.com/account.json"));
    ASSERT_EQ(chmod(legacy_bucket.c_str(), 0500), 0);

    account_index::clear();
    account_index::set_root_directory(root);
    account_index::set_enabled(true);

    const bool write_reported_failure = !account::write_account_file(root, account_data, &error_message);

    std::string indexed_path;
    const bool index_knows_the_record = account_index::find_path_by_email("player@example.com", &indexed_path, nullptr);

    chmod(legacy_bucket.c_str(), 0700);
    account_index::set_enabled(false);
    account_index::clear();

    EXPECT_TRUE(write_reported_failure) << "a failed retirement must still be reported";
    EXPECT_TRUE(index_knows_the_record)
        << "account.json is on disk after the rename, so the index must reflect it even though retirement failed";
}

TEST(AccountManagement, RefusesToWriteWhenALegacyRecordItWouldRetireCannotBeRead)
{
    // legacy_flat_path is composed from the ACCOUNT NAME, and a fresh registration derives that name
    // from the email -- so "bob@example.com" derives "bob" and lands exactly on an existing
    // accounts/<bucket>/bob.json. An unparseable record there is quarantined under its own PATH (it
    // has disclosed no email to be keyed by), so every email-keyed creation guard misses and the
    // write used to reach its retirement step and std::remove a real player's only copy. Nothing
    // this function deletes may go unread first.
    TemporaryDirectory temp_directory;
    const std::string root = temp_directory.path();
    std::string error_message;

    ASSERT_EQ(mkdir((root + "/accounts").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((root + "/accounts/A-E").c_str(), 0700), 0);

    const std::string legacy_flat_path = root + "/accounts/A-E/bob.json";
    const std::string original_bytes = "{ \"account_name\": \"bob\", this record no longer parses";
    write_text_file(legacy_flat_path, original_bytes);

    account::AccountData account_data;
    account_data.account_name = "bob";
    account_data.normalized_email = "bob@example.com";
    account_data.password_hash = "hash";
    account_data.password_salt = "salt";

    EXPECT_FALSE(account::write_account_file(root, account_data, &error_message))
        << "a record that cannot be read must not be retired";
    EXPECT_EQ(error_message, "Existing account file could not be read safely.");
    EXPECT_EQ(read_file_contents(legacy_flat_path), original_bytes)
        << "the unreadable record is the only copy, and must survive untouched";
}

TEST(AccountManagement, RefusesRegistrationWhoseEmailLivesInsideAnUnreadableAccountBucket)
{
    // A bucket directory that will not opendir() is filed by the boot walk as ONE quarantined record
    // keyed by the bucket's own path -- the accounts inside it are never seen, so none of their
    // emails is quarantined individually. is_quarantined(email) normalizes its argument and can
    // never match a path-shaped key, so the registration guard waves through every player in that
    // bucket and writes a fresh account over a real record.
    TemporaryDirectory temp_directory;
    const std::string root = temp_directory.path();
    std::string error_message;

    const std::string bucket_path = root + "/accounts/" + account::account_bucket_for_name("player@example.com");
    account_index::clear();
    account_index::set_root_directory(root);
    account_index::set_enabled(true);
    account_index::quarantine(bucket_path, bucket_path, "Failed to open account bucket directory");

    const bool created = account::create_account_for_email(root, "player@example.com", "ValidPass1", 1700007776, nullptr, &error_message);

    account_index::set_enabled(false);
    account_index::clear();

    EXPECT_FALSE(created)
        << "registering into a bucket the server cannot read writes a new account over a real player's record";
}

TEST(AccountManagement, MigratesLegacyCharacterWhosePersistedActCarriesTheCrashFlag)
{
    // PLR_CRASH is set on any inventory change (handler.cpp) and persisted by the legacy saver, but
    // the JSON loader masks it off by design. Conversion must still be allowed: on live data 3260 of
    // 6906 character records carry this bit, and refusing them all strands nearly half the player base.
    TemporaryDirectory temp_directory;
    std::string error_message;
    ASSERT_TRUE(account::create_account(temp_directory.path(), "alpha-admin", "player@example.com", "ValidPass1", 1700007776, nullptr, &error_message)) << error_message;

    ASSERT_EQ(mkdir((temp_directory.path() + "/players").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/players/A-E").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/plrobjs").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/plrobjs/A-E").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/exploits").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/exploits/A-E").c_str(), 0700), 0);

    char_file_u stored_character = make_stored_character("aragorn");
    stored_character.specials2.act |= PLR_CRASH;
    write_valid_legacy_player_file(temp_directory.path(), stored_character);
    write_text_file(account::legacy_object_file_path(temp_directory.path(), "aragorn"), make_valid_object_bytes());
    write_text_file(account::legacy_exploits_file_path(temp_directory.path(), "aragorn"), make_valid_exploit_bytes());

    account::CharacterMigrationData migration;
    EXPECT_TRUE(account::migrate_legacy_character_by_name(temp_directory.path(), "alpha-admin", "aragorn", 1700007777, &migration, &error_message))
        << "Expected a character carrying the transient PLR_CRASH bit to convert. Error: " << error_message;
}

TEST(AccountManagement, MigratesLegacyCharacterWhosePersistedBadPasswordCountIsNonZero)
{
    // specials2.bad_pws is persisted by the legacy saver (db.cpp) but never serialized to JSON. A
    // failed login increments it and saves, so this is the ordinary on-disk state of exactly the
    // offline character an immortal migrates by hand.
    TemporaryDirectory temp_directory;
    std::string error_message;
    ASSERT_TRUE(account::create_account(temp_directory.path(), "alpha-admin", "player@example.com", "ValidPass1", 1700007776, nullptr, &error_message)) << error_message;

    ASSERT_EQ(mkdir((temp_directory.path() + "/players").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/players/A-E").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/plrobjs").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/plrobjs/A-E").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/exploits").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/exploits/A-E").c_str(), 0700), 0);

    char_file_u stored_character = make_stored_character("aragorn");
    stored_character.specials2.bad_pws = 3;
    write_valid_legacy_player_file(temp_directory.path(), stored_character);
    write_text_file(account::legacy_object_file_path(temp_directory.path(), "aragorn"), make_valid_object_bytes());
    write_text_file(account::legacy_exploits_file_path(temp_directory.path(), "aragorn"), make_valid_exploit_bytes());

    account::CharacterMigrationData migration;
    EXPECT_TRUE(account::migrate_legacy_character_by_name(temp_directory.path(), "alpha-admin", "aragorn", 1700007777, &migration, &error_message))
        << "Expected a character with a non-zero bad-password count to convert. Error: " << error_message;
}

TEST(AccountManagement, MigratesLegacyCharacterWithJunkInGeneralProfessionSlot)
{
    // profs arrays are [MAX_PROFS + 1]; slot 0 is PROF_GENERAL, which no accessor reads. The legacy
    // saver still writes it, and 53 live characters carry junk there (Li: prof_coef 0 = 1; others
    // -27008, 32000, ...). Character JSON carries only MAGE..WARRIOR, so slot 0 cannot survive.
    TemporaryDirectory temp_directory;
    std::string error_message;
    ASSERT_TRUE(account::create_account(temp_directory.path(), "alpha-admin", "player@example.com", "ValidPass1", 1700007778, nullptr, &error_message)) << error_message;

    ASSERT_EQ(mkdir((temp_directory.path() + "/players").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/players/A-E").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/plrobjs").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/plrobjs/A-E").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/exploits").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/exploits/A-E").c_str(), 0700), 0);

    char_file_u stored_character = make_stored_character("aragorn");
    stored_character.profs.prof_coof[PROF_GENERAL] = -27008;
    stored_character.profs.prof_level[PROF_GENERAL] = 1;
    stored_character.profs.prof_exp[PROF_GENERAL] = 9728;
    write_valid_legacy_player_file(temp_directory.path(), stored_character);
    write_text_file(account::legacy_object_file_path(temp_directory.path(), "aragorn"), make_valid_object_bytes());
    write_text_file(account::legacy_exploits_file_path(temp_directory.path(), "aragorn"), make_valid_exploit_bytes());

    account::CharacterMigrationData migration;
    EXPECT_TRUE(account::migrate_legacy_character_by_name(temp_directory.path(), "alpha-admin", "aragorn", 1700007779, &migration, &error_message))
        << "Expected junk in the unused PROF_GENERAL slot not to block conversion. Error: " << error_message;
}

namespace {

// Same directory layout and fixture files as the other legacy-migration tests above.
void write_legacy_character_with_support_files(const std::string& root, const char_file_u& stored_character)
{
    ASSERT_EQ(mkdir((root + "/players").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((root + "/players/A-E").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((root + "/plrobjs").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((root + "/plrobjs/A-E").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((root + "/exploits").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((root + "/exploits/A-E").c_str(), 0700), 0);
    write_valid_legacy_player_file(root, stored_character);
    write_text_file(account::legacy_object_file_path(root, stored_character.name), make_valid_object_bytes());
    write_text_file(account::legacy_exploits_file_path(root, stored_character.name), make_valid_exploit_bytes());
}

} // namespace

TEST(AccountManagement, MigratesLegacyCharacterCarryingFlagBitsCharacterJsonCannotName)
{
    // Character JSON stores act/pref as NAMED flags. PRF_NOTHING2 (pref bit 6) has no name and no
    // reader; 58 live characters carry it (Alanis, Alkar, Turgon, ...), and one (Dandelo) carries
    // act bit 22, which has no PLR_ definition at all. Neither can survive, and neither is lost.
    TemporaryDirectory temp_directory;
    std::string error_message;
    ASSERT_TRUE(account::create_account(temp_directory.path(), "alpha-admin", "player@example.com", "ValidPass1", 1700007780, nullptr, &error_message)) << error_message;

    char_file_u stored_character = make_stored_character("aragorn");
    stored_character.specials2.pref |= PRF_NOTHING2 | PRF_BRIEF;
    stored_character.specials2.act |= (1L << 22) | PLR_NOSHOUT;
    write_legacy_character_with_support_files(temp_directory.path(), stored_character);

    account::CharacterMigrationData migration;
    EXPECT_TRUE(account::migrate_legacy_character_by_name(temp_directory.path(), "alpha-admin", "aragorn", 1700007781, &migration, &error_message))
        << "Expected flag bits with no name in character JSON not to block conversion. Error: " << error_message;
}

TEST(AccountManagement, MigratesLegacyCharacterWithGapsBetweenAffectSlots)
{
    // Character JSON keeps only non-empty affects and reloads them packed from slot 0, so a legacy
    // file with an empty slot before a used one comes back in different positions. The game loads
    // every non-empty slot regardless of position (db.cpp store_to_char). 15 live characters.
    TemporaryDirectory temp_directory;
    std::string error_message;
    ASSERT_TRUE(account::create_account(temp_directory.path(), "alpha-admin", "player@example.com", "ValidPass1", 1700007782, nullptr, &error_message)) << error_message;

    char_file_u stored_character = make_stored_character("aragorn");
    for (affected_type& affect : stored_character.affected)
        affect = affected_type {};
    stored_character.affected[0].type = 41;
    stored_character.affected[0].duration = 61;
    stored_character.affected[1].type = 69;
    stored_character.affected[1].duration = 120;
    write_legacy_character_with_support_files(temp_directory.path(), stored_character);

    // save_player repacks affects from slot 0 on the way out, so move them to slots 2 and 3 in the
    // text itself -- the shape of the live files (angurth: "affect 2 41 61 0 0 1", "affect 3 ...").
    const std::string player_path = account::legacy_player_file_path(temp_directory.path(), "aragorn");
    std::string player_text = read_file_contents(player_path);
    const std::string slot0 = "affect      0 ";
    const std::string slot1 = "affect      1 ";
    ASSERT_NE(player_text.find(slot0), std::string::npos) << player_text;
    ASSERT_NE(player_text.find(slot1), std::string::npos) << player_text;
    player_text.replace(player_text.find(slot0), slot0.size(), "affect      2 ");
    player_text.replace(player_text.find(slot1), slot1.size(), "affect      3 ");
    write_text_file(player_path, player_text);

    account::CharacterMigrationData migration;
    EXPECT_TRUE(account::migrate_legacy_character_by_name(temp_directory.path(), "alpha-admin", "aragorn", 1700007783, &migration, &error_message))
        << "Expected gaps between affect slots not to block conversion. Error: " << error_message;
}

TEST(AccountManagement, PersistedMigrationSnapshotOmitsLegacyPlayerPasswordAndHostData)
{
    TemporaryDirectory temp_directory;
    std::string error_message;
    ASSERT_TRUE(account::create_account(temp_directory.path(), "alpha-admin", "player@example.com", "ValidPass1", 1700007776, nullptr, &error_message)) << error_message;

    ASSERT_EQ(mkdir((temp_directory.path() + "/players").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/players/A-E").c_str(), 0700), 0);

    char_file_u stored_character = make_stored_character("aragorn");
    std::snprintf(stored_character.host, sizeof(stored_character.host), "%s", "legacy.example.org");
    std::snprintf(stored_character.pwd, sizeof(stored_character.pwd), "%s", "legacy-password");
    write_valid_legacy_player_file(temp_directory.path(), stored_character);

    account::CharacterMigrationData migration;
    ASSERT_TRUE(account::migrate_legacy_character_by_name(temp_directory.path(), "alpha-admin", "aragorn", 1700007777, &migration, &error_message)) << error_message;
    ASSERT_TRUE(migration.player_file.present);

    struct stat file_info { };
    EXPECT_NE(stat(account::account_character_snapshot_path(temp_directory.path(), "alpha-admin", "aragorn").c_str(), &file_info), 0)
        << "Routine migration should no longer persist a transitional snapshot file at all.";
}

TEST(AccountManagement, ReadingLegacyMigrationSnapshotScrubsPersistedPlayerPayloadFromDisk)
{
    TemporaryDirectory temp_directory;
    std::string error_message;
    ASSERT_TRUE(account::create_account(temp_directory.path(), "alpha-admin", "player@example.com", "ValidPass1", 1700007776, nullptr, &error_message)) << error_message;

    const std::string snapshot_path = account::account_character_snapshot_path(temp_directory.path(), "alpha-admin", "aragorn");
    write_text_file(snapshot_path,
        "{\n"
        "  \"version\": 1,\n"
        "  \"account_name\": \"alpha-admin\",\n"
        "  \"character_name\": \"aragorn\",\n"
        "  \"migrated_at\": 1700007777,\n"
        "  \"player_file\": {\n"
        "    \"source_path\": \"/legacy/players/A-E/aragorn\",\n"
        "    \"encoding\": \"hex\",\n"
        "    \"content\": \"686f7374202020202020206c65676163792e6578616d706c652e6f7267\",\n"
        "    \"present\": true\n"
        "  },\n"
        "  \"object_file\": {\n"
        "    \"source_path\": \"/legacy/plrobjs/A-E/aragorn.objs\",\n"
        "    \"encoding\": \"hex\",\n"
        "    \"content\": \"\",\n"
        "    \"present\": false\n"
        "  },\n"
        "  \"exploits_file\": {\n"
        "    \"source_path\": \"/legacy/exploits/A-E/aragorn.exploits\",\n"
        "    \"encoding\": \"hex\",\n"
        "    \"content\": \"\",\n"
        "    \"present\": false\n"
        "  }\n"
        "}\n");

    account::CharacterMigrationData migration;
    ASSERT_TRUE(account::read_character_migration(temp_directory.path(), "alpha-admin", "aragorn", &migration, &error_message)) << error_message;
    EXPECT_FALSE(migration.player_file.present);
    EXPECT_TRUE(migration.player_file.content.empty());
    EXPECT_TRUE(migration.player_file.source_path.empty());

    const std::string scrubbed_snapshot_json = read_file_contents(snapshot_path);
    EXPECT_EQ(scrubbed_snapshot_json.find("\"player_file\""), std::string::npos);
    EXPECT_EQ(scrubbed_snapshot_json.find("legacy.example.org"), std::string::npos);
}

TEST(AccountManagement, ReportsMissingRequiredPlayerFileDuringMigration)
{
    TemporaryDirectory temp_directory;
    account::CharacterMigrationData migration;
    std::string error_message;
    ASSERT_TRUE(account::create_account(temp_directory.path(), "alpha-admin", "player@example.com", "ValidPass1", 1700008887, nullptr, &error_message)) << error_message;

    EXPECT_FALSE(account::migrate_legacy_character_by_name(temp_directory.path(), "alpha-admin", "aragorn", 1700008888, &migration, &error_message));
    EXPECT_FALSE(error_message.empty());
}

TEST(AccountManagement, MigratesLegacyCharacterByDefaultFileLayout)
{
    TemporaryDirectory temp_directory;
    std::string error_message;
    ASSERT_TRUE(account::create_account(temp_directory.path(), "alpha-admin", "player@example.com", "ValidPass1", 1700009998, nullptr, &error_message)) << error_message;
    ASSERT_EQ(mkdir((temp_directory.path() + "/players").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/players/A-E").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/plrobjs").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/plrobjs/A-E").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/exploits").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/exploits/A-E").c_str(), 0700), 0);

    char_file_u legacy_character = make_stored_character("aragorn");
    legacy_character.profs.specialization = PLRSPEC_DFND;
    write_valid_legacy_player_file(temp_directory.path(), legacy_character);
    write_text_file(account::legacy_object_file_path(temp_directory.path(), "aragorn"), make_valid_object_bytes());
    write_text_file(account::legacy_exploits_file_path(temp_directory.path(), "aragorn"), make_valid_exploit_bytes());

    account::CharacterMigrationData migration;
    ASSERT_TRUE(account::migrate_legacy_character_by_name(temp_directory.path(), "alpha-admin", "aragorn", 1700009999, &migration, &error_message)) << error_message;
    EXPECT_EQ(migration.account_name, "alpha-admin");
    EXPECT_EQ(migration.character_name, "aragorn");

    char_file_u migrated_character {};
    ASSERT_TRUE(account::read_account_character_file(temp_directory.path(), "alpha-admin", "aragorn", &migrated_character, &error_message)) << error_message;
    EXPECT_EQ(migrated_character.profs.specialization, legacy_character.profs.specialization);
}

TEST(AccountManagement, MigratesVersionedLegacyPlayerFilesForFreshCharacters)
{
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

TEST(AccountManagement, PrefersVersionedLegacyPlayerFilesOverStaleFlatFilesDuringMigration)
{
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

    struct stat file_info { };
    EXPECT_NE(stat(account::legacy_player_file_path(temp_directory.path(), "aragorn").c_str(), &file_info), 0)
        << "Expected migration to retire the stale flat legacy player file once the versioned save won precedence.";
}

TEST(AccountManagement, MigratesVersionedLegacyPlayerFileEvenWhenStaleFlatFileIsUnreadable)
{
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

    struct stat file_info { };
    EXPECT_NE(stat(account::legacy_player_file_path(temp_directory.path(), "aragorn").c_str(), &file_info), 0);
}

TEST(AccountManagement, RejectsAmbiguousVersionedLegacyPlayerFilesDuringMigration)
{
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

TEST(AccountManagement, IgnoresStrayNonVersionedPlayerArtifactsWhenResolvingFreshCharacterSaves)
{
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

TEST(AccountManagement, TreatsMissingOptionalLegacyFilesAsAbsentSnapshots)
{
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

TEST(AccountManagement, EnsuresCharacterMigrationByCreatingMissingSnapshotFromLegacyFiles)
{
    TemporaryDirectory temp_directory;
    std::string error_message;
    ASSERT_TRUE(account::create_account(temp_directory.path(), "alpha-admin", "player@example.com", "ValidPass1", 1700010099, nullptr, &error_message)) << error_message;
    ASSERT_EQ(mkdir((temp_directory.path() + "/players").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/players/A-E").c_str(), 0700), 0);

    write_valid_legacy_player_file(temp_directory.path(), make_stored_character("aragorn"));

    account::CharacterMigrationData migration;
    ASSERT_TRUE(account::ensure_character_migration(temp_directory.path(), "alpha-admin", "aragorn", 1700010100, &migration, &error_message)) << error_message;
    EXPECT_EQ(migration.account_name, "alpha-admin");
    EXPECT_EQ(migration.character_name, "aragorn");
    EXPECT_TRUE(migration.player_file.present);

    char_file_u restored_character {};
    ASSERT_TRUE(account::read_account_character_file(temp_directory.path(), "alpha-admin", "aragorn", &restored_character, &error_message)) << error_message;
    EXPECT_STREQ(restored_character.name, "aragorn");
    EXPECT_EQ(restored_character.points.gold, make_stored_character("aragorn").points.gold);
    struct stat file_info { };
    EXPECT_NE(stat(account::legacy_player_file_path(temp_directory.path(), "aragorn").c_str(), &file_info), 0);
    EXPECT_NE(stat(account::account_character_snapshot_path(temp_directory.path(), "alpha-admin", "aragorn").c_str(), &file_info), 0);
}

TEST(AccountManagement, IgnoresCorruptSnapshotArtifactWhenRebuildingFromLegacyFiles)
{
    TemporaryDirectory temp_directory;
    std::string error_message;
    ASSERT_TRUE(account::create_account(temp_directory.path(), "alpha-admin", "player@example.com", "ValidPass1", 1700010099, nullptr, &error_message)) << error_message;
    ASSERT_EQ(mkdir((temp_directory.path() + "/players").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/players/A-E").c_str(), 0700), 0);

    write_valid_legacy_player_file(temp_directory.path(), make_stored_character("aragorn"));
    write_text_file(account::account_character_snapshot_path(temp_directory.path(), "alpha-admin", "aragorn"), "{bad-json");

    account::CharacterMigrationData migration;
    ASSERT_TRUE(account::ensure_character_migration(temp_directory.path(), "alpha-admin", "aragorn", 1700010100, &migration, &error_message)) << error_message;
    EXPECT_TRUE(migration.player_file.present);

    char_file_u restored_character {};
    ASSERT_TRUE(account::read_account_character_file(temp_directory.path(), "alpha-admin", "aragorn", &restored_character, &error_message)) << error_message;
    EXPECT_STREQ(restored_character.name, "aragorn");
    EXPECT_EQ(restored_character.points.gold, make_stored_character("aragorn").points.gold);

    struct stat file_info { };
    EXPECT_NE(stat(account::legacy_player_file_path(temp_directory.path(), "aragorn").c_str(), &file_info), 0);
    EXPECT_NE(stat(account::account_character_snapshot_path(temp_directory.path(), "alpha-admin", "aragorn").c_str(), &file_info), 0);
}

TEST(AccountManagement, EnsureCharacterMigrationFailsClosedWhenOnlyCorruptSnapshotRemains)
{
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
    EXPECT_NE(error_message.find("Failed to open legacy file"), std::string::npos);
    EXPECT_FALSE(account::account_character_file_exists(temp_directory.path(), "alpha-admin", "aragorn", nullptr));
}

TEST(AccountManagement, EnsureCharacterMigrationSucceedsWhenAuthoritativeCharacterFileExistsAndSnapshotIsCorrupt)
{
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

TEST(AccountManagement, DecodesSnapshotContentBackIntoOriginalBytes)
{
    account::CharacterMigrationData migration;
    std::string error_message;
    migration.player_file.present = true;
    migration.player_file.encoding = "hex";
    migration.player_file.content = "6c65676163792d706c617965722d64617461";

    std::string contents;
    ASSERT_TRUE(account::decode_snapshot_content(migration.player_file, &contents, &error_message)) << error_message;
    EXPECT_EQ(contents, "legacy-player-data");
}

TEST(AccountManagement, RestoresLegacyFilesFromCharacterMigrationSnapshot)
{
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

    struct stat file_info { };
    EXPECT_NE(stat(account::legacy_exploits_file_path(temp_directory.path(), "aragorn").c_str(), &file_info), 0);
}

TEST(AccountManagement, RejectsMismatchedRestoreRequestWithoutTouchingLegacyFiles)
{
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

TEST(AccountManagement, ClearsRuntimeSupportFilesForAccountBackedPlayWithoutRewritingPlayerFile)
{
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

    struct stat file_info { };
    EXPECT_NE(stat(account::legacy_object_file_path(temp_directory.path(), "aragorn").c_str(), &file_info), 0);
    EXPECT_NE(stat(account::legacy_exploits_file_path(temp_directory.path(), "aragorn").c_str(), &file_info), 0);
}

TEST(AccountManagement, RejectsClearingSupportFilesWhenSnapshotIdentityDoesNotMatchSelection)
{
    TemporaryDirectory temp_directory;
    account::CharacterMigrationData migration;
    migration.account_name = "beta-admin";
    migration.character_name = "legolas";

    std::string error_message;
    EXPECT_FALSE(account::clear_character_runtime_support_files_for_account_play(temp_directory.path(), "alpha-admin", "aragorn", migration, &error_message));
    EXPECT_NE(error_message.find("did not match"), std::string::npos);
}

TEST(AccountManagement, RefreshesSnapshotForLinkedCharactersUsingCurrentLegacyFiles)
{
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
}

TEST(AccountManagement, MigrationWritesAccountNativeObjectFileWhenLegacyObjectDataIsValid)
{
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

TEST(AccountManagement, MigrationAcceptsLegacyObjectFileWithoutFollowerSection)
{
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
    object_data.objects[0].item_number = 4321;
    object_data.objects[0].wear_pos = WEAR_BODY;
    object_data.aliases.push_back({ "assist", "kill orc" });

    std::string object_bytes;
    ASSERT_TRUE(objects_json::object_save_data_to_binary(object_data, &object_bytes, &error_message)) << error_message;
    ASSERT_GT(object_bytes.size(), sizeof(follower_file_elem));
    object_bytes.erase(object_bytes.size() - sizeof(follower_file_elem));
    write_text_file(account::legacy_object_file_path(temp_directory.path(), "aragorn"), object_bytes);

    account::CharacterMigrationData migration;
    ASSERT_TRUE(account::migrate_legacy_character_by_name(temp_directory.path(), "alpha-admin", "aragorn", 1700010102, &migration, &error_message)) << error_message;
    ASSERT_TRUE(account::account_object_file_exists(temp_directory.path(), "alpha-admin", "aragorn", &error_message));

    std::string loaded_bytes;
    ASSERT_TRUE(account::read_account_object_file(temp_directory.path(), "alpha-admin", "aragorn", &loaded_bytes, &error_message)) << error_message;

    objects_json::ObjectSaveData loaded;
    ASSERT_TRUE(objects_json::object_save_data_from_binary(loaded_bytes, &loaded, &error_message)) << error_message;
    ASSERT_EQ(loaded.objects.size(), 1u);
    EXPECT_EQ(loaded.objects[0].item_number, 4321);
    ASSERT_EQ(loaded.aliases.size(), 1u);
    EXPECT_EQ(loaded.aliases[0].keyword, "assist");
    EXPECT_EQ(loaded.aliases[0].command, "kill orc");
    EXPECT_TRUE(loaded.followers.empty());
}

TEST(AccountManagement, MigrationRejectsPartialFollowerSectionAndCleansUpAccountNativeOutputs)
{
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
    const std::string exploit_path = account::legacy_exploits_file_path(temp_directory.path(), "aragorn");
    const std::string expected_player_text = write_valid_legacy_player_file(temp_directory.path(), make_stored_character("aragorn"));
    const std::string expected_exploit_bytes = make_valid_exploit_bytes();
    write_text_file(exploit_path, expected_exploit_bytes);

    objects_json::ObjectSaveData object_data;
    object_data.rent.rentcode = RENT_CRASH;
    object_data.objects.push_back(objects_json::ObjectRecord {});
    object_data.objects[0].item_number = 4321;
    object_data.objects[0].wear_pos = WEAR_BODY;
    object_data.aliases.push_back({ "assist", "kill orc" });

    std::string object_bytes;
    ASSERT_TRUE(objects_json::object_save_data_to_binary(object_data, &object_bytes, &error_message)) << error_message;
    ASSERT_GT(object_bytes.size(), sizeof(follower_file_elem));
    object_bytes.erase(object_bytes.size() - sizeof(follower_file_elem) / 2);
    write_text_file(object_path, object_bytes);

    account::CharacterMigrationData migration;
    EXPECT_FALSE(account::migrate_legacy_character_by_name(temp_directory.path(), "alpha-admin", "aragorn", 1700010102, &migration, &error_message));
    EXPECT_NE(error_message.find("Truncated objects data while reading follower record"), std::string::npos);

    EXPECT_EQ(read_file_contents(player_path), expected_player_text);
    EXPECT_EQ(read_file_contents(object_path), object_bytes);
    EXPECT_EQ(read_file_contents(exploit_path), expected_exploit_bytes);
    EXPECT_FALSE(account::account_character_file_exists(temp_directory.path(), "alpha-admin", "aragorn", nullptr));
    EXPECT_FALSE(account::account_object_file_exists(temp_directory.path(), "alpha-admin", "aragorn", nullptr));
    EXPECT_FALSE(account::account_exploit_file_exists(temp_directory.path(), "alpha-admin", "aragorn", nullptr));
}

namespace {

// Sets up a legacy character with all three files and returns their paths, so a test can prove the
// rule that matters: whatever goes wrong, the sources are exactly as they were.
struct LegacySourceFiles {
    std::string player_path;
    std::string object_path;
    std::string exploit_path;
    std::string player_text;
    std::string object_bytes;
    std::string exploit_bytes;
};

LegacySourceFiles write_complete_legacy_character(const std::string& root, const std::string& character_name)
{
    LegacySourceFiles sources;
    sources.player_path = account::legacy_player_file_path(root, character_name);
    sources.object_path = account::legacy_object_file_path(root, character_name);
    sources.exploit_path = account::legacy_exploits_file_path(root, character_name);
    sources.player_text = write_valid_legacy_player_file(root, make_stored_character(character_name.c_str()));

    objects_json::ObjectSaveData object_data;
    object_data.rent.rentcode = RENT_CRASH;
    object_data.rent.gold = 4200;
    object_data.aliases.push_back({ "assist", "kill orc" });
    std::string error_message;
    EXPECT_TRUE(objects_json::object_save_data_to_binary(object_data, &sources.object_bytes, &error_message)) << error_message;
    write_text_file(sources.object_path, sources.object_bytes);

    sources.exploit_bytes = make_valid_exploit_bytes();
    write_text_file(sources.exploit_path, sources.exploit_bytes);
    return sources;
}

// A path the atomic write cannot rename onto. Non-empty on purpose: cleanup std::remove()s the
// output paths, and remove() rmdir's an EMPTY directory, which would take the obstruction away and
// make the test pass for the wrong reason.
void obstruct_path_with_directory(const std::string& path)
{
    ASSERT_EQ(mkdir(path.c_str(), 0700), 0);
    write_text_file(path + "/occupied", "occupied");
}

void expect_legacy_sources_untouched(const LegacySourceFiles& sources)
{
    EXPECT_EQ(read_file_contents(sources.player_path), sources.player_text);
    EXPECT_EQ(read_file_contents(sources.object_path), sources.object_bytes);
    EXPECT_EQ(read_file_contents(sources.exploit_path), sources.exploit_bytes);
}

} // namespace

TEST(AccountManagement, MigrationLeavesTheLegacySourcesIntactWhenTheObjectFileCannotBeWritten)
{
    // The rule, at the failure point nothing covered: not a malformed source this time but a write
    // that will not land. By then the character file has already been written, so "stop and leave
    // the sources alone" also means taking that back.
    TemporaryDirectory temp_directory;
    ASSERT_EQ(mkdir((temp_directory.path() + "/players").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/players/A-E").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/plrobjs").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/plrobjs/A-E").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/exploits").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/exploits/A-E").c_str(), 0700), 0);

    std::string error_message;
    ASSERT_TRUE(account::create_account(temp_directory.path(), "alpha-admin", "player@example.com", "ValidPass1", 1700010101, nullptr, &error_message)) << error_message;

    const LegacySourceFiles sources = write_complete_legacy_character(temp_directory.path(), "aragorn");
    obstruct_path_with_directory(account::account_character_object_path(temp_directory.path(), "alpha-admin", "aragorn"));

    account::CharacterMigrationData migration;
    EXPECT_FALSE(account::migrate_legacy_character_by_name(temp_directory.path(), "alpha-admin", "aragorn", 1700010102, &migration, &error_message));

    expect_legacy_sources_untouched(sources);
    EXPECT_FALSE(account::account_character_file_exists(temp_directory.path(), "alpha-admin", "aragorn", nullptr))
        << "the character file written before the failure must not be left behind";
    EXPECT_FALSE(account::account_exploit_file_exists(temp_directory.path(), "alpha-admin", "aragorn", nullptr));
}

TEST(AccountManagement, MigrationLeavesTheLegacySourcesIntactWhenTheCharacterFileCannotBeWritten)
{
    // The first output written, and the one whose failure path deliberately does no cleanup --
    // safe only while its write is the last thing that step does. Pinned here so a later edit that
    // adds work after that write has to answer for it.
    TemporaryDirectory temp_directory;
    ASSERT_EQ(mkdir((temp_directory.path() + "/players").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/players/A-E").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/plrobjs").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/plrobjs/A-E").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/exploits").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/exploits/A-E").c_str(), 0700), 0);

    std::string error_message;
    ASSERT_TRUE(account::create_account(temp_directory.path(), "alpha-admin", "player@example.com", "ValidPass1", 1700010101, nullptr, &error_message)) << error_message;

    const LegacySourceFiles sources = write_complete_legacy_character(temp_directory.path(), "aragorn");
    obstruct_path_with_directory(account::account_character_player_path(temp_directory.path(), "alpha-admin", "aragorn"));

    account::CharacterMigrationData migration;
    EXPECT_FALSE(account::migrate_legacy_character_by_name(temp_directory.path(), "alpha-admin", "aragorn", 1700010102, &migration, &error_message));

    expect_legacy_sources_untouched(sources);
    EXPECT_FALSE(account::account_object_file_exists(temp_directory.path(), "alpha-admin", "aragorn", nullptr));
    EXPECT_FALSE(account::account_exploit_file_exists(temp_directory.path(), "alpha-admin", "aragorn", nullptr));
}

TEST(AccountManagement, MigrationLeavesTheLegacySourcesIntactWhenTheExploitFileCannotBeWritten)
{
    // The last output written, so by this point both of the others are on disk and both have to go
    // back. Same rule, one step later.
    TemporaryDirectory temp_directory;
    ASSERT_EQ(mkdir((temp_directory.path() + "/players").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/players/A-E").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/plrobjs").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/plrobjs/A-E").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/exploits").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/exploits/A-E").c_str(), 0700), 0);

    std::string error_message;
    ASSERT_TRUE(account::create_account(temp_directory.path(), "alpha-admin", "player@example.com", "ValidPass1", 1700010101, nullptr, &error_message)) << error_message;

    const LegacySourceFiles sources = write_complete_legacy_character(temp_directory.path(), "aragorn");
    obstruct_path_with_directory(account::account_character_exploits_path(temp_directory.path(), "alpha-admin", "aragorn"));

    account::CharacterMigrationData migration;
    EXPECT_FALSE(account::migrate_legacy_character_by_name(temp_directory.path(), "alpha-admin", "aragorn", 1700010102, &migration, &error_message));

    expect_legacy_sources_untouched(sources);
    EXPECT_FALSE(account::account_character_file_exists(temp_directory.path(), "alpha-admin", "aragorn", nullptr))
        << "the character file written before the failure must not be left behind";
    EXPECT_FALSE(account::account_object_file_exists(temp_directory.path(), "alpha-admin", "aragorn", nullptr))
        << "nor the object file";
}

TEST(AccountManagement, MigrationWritesDefaultAccountNativeObjectFileWhenLegacyObjectDataIsMissing)
{
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

TEST(AccountManagement, MigrationWritesAccountNativeExploitFileWhenLegacyExploitDataIsValid)
{
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

TEST(AccountManagement, MigrationRetiresLegacyFilesAfterSuccessfulAccountNativeWrite)
{
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

    struct stat file_info { };
    EXPECT_NE(stat(account::account_character_snapshot_path(temp_directory.path(), "alpha-admin", "aragorn").c_str(), &file_info), 0);
    EXPECT_NE(stat(player_path.c_str(), &file_info), 0);
    EXPECT_NE(stat(object_path.c_str(), &file_info), 0);
    EXPECT_NE(stat(exploits_path.c_str(), &file_info), 0);
    EXPECT_TRUE(account::account_object_file_exists(temp_directory.path(), "alpha-admin", "aragorn", &error_message));
    EXPECT_TRUE(account::account_exploit_file_exists(temp_directory.path(), "alpha-admin", "aragorn", &error_message));
    EXPECT_FALSE(expected_player_text.empty());
}

TEST(AccountManagement, MigrationFailsClosedWhenLegacyFileRetirementFails)
{
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

    struct stat file_info { };
    EXPECT_EQ(stat(player_path.c_str(), &file_info), 0);
    EXPECT_EQ(stat(object_path.c_str(), &file_info), 0);
    EXPECT_EQ(read_file_contents(player_path), expected_player_text);
    EXPECT_NE(stat(account::account_character_snapshot_path(temp_directory.path(), "alpha-admin", "aragorn").c_str(), &file_info), 0);
    EXPECT_FALSE(account::account_character_file_exists(temp_directory.path(), "alpha-admin", "aragorn", &error_message));
    EXPECT_FALSE(account::account_object_file_exists(temp_directory.path(), "alpha-admin", "aragorn", &error_message));
    EXPECT_FALSE(account::account_exploit_file_exists(temp_directory.path(), "alpha-admin", "aragorn", &error_message));
}

TEST(AccountManagement, MigrationCleansUpAccountNativeOutputsWhenStaleFlatRetirementFails)
{
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

    struct stat file_info { };
    EXPECT_NE(stat(account::account_character_snapshot_path(temp_directory.path(), "alpha-admin", "aragorn").c_str(), &file_info), 0);
    EXPECT_EQ(read_file_contents(versioned_player_path), expected_player_text);
    EXPECT_EQ(read_file_contents(account::legacy_object_file_path(temp_directory.path(), "aragorn")), expected_object_bytes);
    EXPECT_EQ(read_file_contents(account::legacy_exploits_file_path(temp_directory.path(), "aragorn")), expected_exploit_bytes);
    EXPECT_EQ(stat(stale_flat_path.c_str(), &file_info), 0);
}

TEST(AccountManagement, MigrationRestoresRetiredFilesWhenExploitRetirementFails)
{
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

    struct stat file_info { };
    EXPECT_EQ(stat(player_path.c_str(), &file_info), 0);
    EXPECT_EQ(stat(object_path.c_str(), &file_info), 0);
    EXPECT_EQ(stat(exploits_path.c_str(), &file_info), 0);
    EXPECT_EQ(read_file_contents(player_path), expected_player_text);
    EXPECT_NE(stat(account::account_character_snapshot_path(temp_directory.path(), "alpha-admin", "aragorn").c_str(), &file_info), 0);
    EXPECT_FALSE(account::account_character_file_exists(temp_directory.path(), "alpha-admin", "aragorn", &error_message));
    EXPECT_FALSE(account::account_object_file_exists(temp_directory.path(), "alpha-admin", "aragorn", &error_message));
    EXPECT_FALSE(account::account_exploit_file_exists(temp_directory.path(), "alpha-admin", "aragorn", &error_message));
}

TEST(AccountManagement, MigrationKeepsItsOutputsWhenItCannotPutTheLegacyOriginalsBack)
{
    // The other way a conversion could end with no copy of the character. When retirement fails
    // part-way it restores the legacy files it had already deleted -- but restore_retired_legacy_files
    // only appends to the error string, so a restore that did not work reported nothing, and the
    // caller went on to delete the account-native files anyway.
    TemporaryDirectory temp_directory;
    const std::string root = temp_directory.path();
    ASSERT_EQ(mkdir((root + "/players").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((root + "/players/A-E").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((root + "/plrobjs").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((root + "/plrobjs/A-E").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((root + "/exploits").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((root + "/exploits/A-E").c_str(), 0700), 0);

    std::string error_message;
    ASSERT_TRUE(account::create_account(root, "alpha-admin", "player@example.com", "ValidPass1", 1700010101, nullptr, &error_message)) << error_message;

    const std::string player_path = account::legacy_player_file_path(root, "aragorn");
    const std::string object_path = account::legacy_object_file_path(root, "aragorn");
    const std::string exploits_bucket = root + "/exploits/A-E";
    write_valid_legacy_player_file(root, make_stored_character("aragorn"));
    write_text_file(object_path, make_valid_object_bytes());
    write_text_file(account::legacy_exploits_file_path(root, "aragorn"), make_valid_exploit_bytes());

    // Retirement gets as far as the exploit file and fails there, exactly as
    // MigrationRestoresRetiredFilesWhenExploitRetirementFails arranges it.
    ASSERT_EQ(chmod(exploits_bucket.c_str(), 0500), 0);
    // And the player file cannot be put back: write_snapshot_bytes stages through "<path>.tmp", and
    // a non-empty directory sitting there means it can neither be opened nor renamed over.
    ASSERT_EQ(mkdir((player_path + ".tmp").c_str(), 0700), 0);
    write_text_file(player_path + ".tmp/occupant", "not empty");

    account::CharacterMigrationData migration;
    EXPECT_FALSE(account::migrate_legacy_character_by_name(root, "alpha-admin", "aragorn", 1700010102, &migration, &error_message));

    ASSERT_EQ(chmod(exploits_bucket.c_str(), 0700), 0);

    const bool legacy_survives = access(player_path.c_str(), F_OK) == 0;
    const bool account_native_survives = account::account_character_file_exists(root, "alpha-admin", "aragorn", &error_message);
    EXPECT_TRUE(legacy_survives || account_native_survives)
        << "a restore that did not work must not be followed by deleting the only remaining copy: "
        << "neither " << player_path << " nor the account-native character file is left";
}

TEST(AccountManagement, MigrationWritesDefaultAccountNativeExploitFileWhenLegacyExploitDataIsMissing)
{
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

TEST(AccountManagement, MigrationFailsWhenLegacyExploitDataIsMalformed)
{
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

TEST(AccountManagement, MigrationFailsWhenLegacyObjectDataIsMalformed)
{
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

TEST(AccountManagement, RestoredSnapshotReflectsRefreshedLinkedCharacterState)
{
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

TEST(AccountManagement, RefreshingSnapshotPreservesExistingExploitHistoryWhenRuntimeExploitFileIsAbsent)
{
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
    EXPECT_TRUE(refreshed_migration.player_file.present);

    struct stat file_info { };
    EXPECT_NE(stat(account::account_character_snapshot_path(temp_directory.path(), "alpha-admin", "aragorn").c_str(), &file_info), 0);
}

TEST(AccountManagement, RefreshingSnapshotForUnlinkedCharacterSucceedsWithoutWritingMigration)
{
    TemporaryDirectory temp_directory;
    ASSERT_EQ(mkdir((temp_directory.path() + "/players").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/players/A-E").c_str(), 0700), 0);
    write_valid_legacy_player_file(temp_directory.path(), make_stored_character("aragorn"));

    account::CharacterMigrationData migration;
    std::string error_message;
    ASSERT_TRUE(account::refresh_linked_character_snapshot(temp_directory.path(), "aragorn", 1700010104, &migration, &error_message)) << error_message;
    EXPECT_TRUE(migration.account_name.empty());
    EXPECT_TRUE(migration.character_name.empty());

    struct stat file_info { };
    EXPECT_NE(stat(account::account_character_snapshot_path(temp_directory.path(), "alpha-admin", "aragorn").c_str(), &file_info), 0);
}

TEST(AccountManagement, RefusesToOverwriteCorruptExistingAccountFiles)
{
    TemporaryDirectory temp_directory;
    ASSERT_EQ(mkdir((temp_directory.path() + "/accounts").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/accounts/P-T").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/accounts/P-T/player@example.com").c_str(), 0700), 0);
    write_text_file(account::account_file_path(temp_directory.path(), "player@example.com"), "{not-valid-json");

    std::string error_message;
    EXPECT_FALSE(account::create_account(temp_directory.path(), "alpha-admin", "player@example.com", "ValidPass1", 1700011111, nullptr, &error_message));
    EXPECT_NE(error_message.find("could not be read safely"), std::string::npos);
}

TEST(AccountManagement, RoundTripsFailedLoginMetadataThroughAccountJson)
{
    account::AccountData original_account = make_account();
    original_account.failed_login_count = 3;
    original_account.failed_login_last_at = 1700020000;
    original_account.failed_login_last_host = "host.example.com";

    account::AccountData parsed_account;
    std::string error_message;
    ASSERT_TRUE(account::deserialize_account_from_json(
        account::serialize_account_to_json(original_account), &parsed_account, &error_message))
        << error_message;

    EXPECT_EQ(parsed_account.failed_login_count, 3);
    EXPECT_EQ(parsed_account.failed_login_last_at, 1700020000);
    EXPECT_EQ(parsed_account.failed_login_last_host, "host.example.com");
}

TEST(AccountManagement, DefaultsFailedLoginMetadataWhenAccountJsonOmitsIt)
{
    const std::string legacy_json = "{\n"
                                    "  \"version\": 1,\n"
                                    "  \"account_name\": \"alpha-admin\",\n"
                                    "  \"normalized_email\": \"player@example.com\"\n"
                                    "}\n";

    account::AccountData parsed_account;
    std::string error_message;
    ASSERT_TRUE(account::deserialize_account_from_json(legacy_json, &parsed_account, &error_message)) << error_message;

    EXPECT_EQ(parsed_account.failed_login_count, 0);
    EXPECT_EQ(parsed_account.failed_login_last_at, 0);
    EXPECT_TRUE(parsed_account.failed_login_last_host.empty());
}

TEST(AccountManagement, RecordsFailedLoginAttemptsAgainstTheStoredAccount)
{
    TemporaryDirectory temp_directory;
    account::AccountData created_account;
    std::string error_message;
    ASSERT_TRUE(account::create_account_for_email(temp_directory.path(), "player@example.com", "ValidPass1", 1700001000, &created_account, &error_message)) << error_message;

    EXPECT_TRUE(account::record_account_login_failure(temp_directory.path(), "player@example.com", "first.example.com", 1700002000, &error_message)) << error_message;
    EXPECT_TRUE(account::record_account_login_failure(temp_directory.path(), "Player@Example.com", "second.example.com", 1700002500, &error_message)) << error_message;

    account::AccountData stored_account;
    ASSERT_TRUE(account::read_account_file(temp_directory.path(), created_account.account_name, &stored_account, &error_message)) << error_message;
    EXPECT_EQ(stored_account.failed_login_count, 2);
    EXPECT_EQ(stored_account.failed_login_last_at, 1700002500);
    EXPECT_EQ(stored_account.failed_login_last_host, "second.example.com");
}

TEST(AccountManagement, StripsUnprintableAndOverlongHostsWhenRecordingFailedLogins)
{
    TemporaryDirectory temp_directory;
    account::AccountData created_account;
    std::string error_message;
    ASSERT_TRUE(account::create_account_for_email(temp_directory.path(), "player@example.com", "ValidPass1", 1700001000, &created_account, &error_message)) << error_message;

    const std::string hostile_host = std::string("evil\x1b[2Jhost\r\n") + std::string(80, 'a');
    EXPECT_TRUE(account::record_account_login_failure(temp_directory.path(), "player@example.com", hostile_host, 1700002000, &error_message)) << error_message;

    account::AccountData stored_account;
    ASSERT_TRUE(account::read_account_file(temp_directory.path(), created_account.account_name, &stored_account, &error_message)) << error_message;
    EXPECT_EQ(stored_account.failed_login_last_host.find('\x1b'), std::string::npos);
    EXPECT_EQ(stored_account.failed_login_last_host.find('\r'), std::string::npos);
    EXPECT_EQ(stored_account.failed_login_last_host.find('\n'), std::string::npos);
    EXPECT_LE(stored_account.failed_login_last_host.size(), static_cast<size_t>(account::MAX_FAILED_LOGIN_HOST_LENGTH));
    // Only the control bytes go; the remaining "[2J" is inert text once the escape is gone.
    EXPECT_EQ(stored_account.failed_login_last_host.substr(0, 11), "evil[2Jhost");
}

TEST(AccountManagement, IgnoresFailedLoginAttemptsForAddressesWithoutAnAccount)
{
    TemporaryDirectory temp_directory;
    std::string error_message;

    EXPECT_TRUE(account::record_account_login_failure(temp_directory.path(), "nobody@example.com", "host.example.com", 1700002000, &error_message)) << error_message;

    struct stat file_info { };
    EXPECT_NE(stat(account::account_file_path(temp_directory.path(), "nobody@example.com").c_str(), &file_info), 0);
}

TEST(AccountManagement, ClearsFailedLoginMetadataAfterASuccessfulLogin)
{
    TemporaryDirectory temp_directory;
    account::AccountData created_account;
    std::string error_message;
    ASSERT_TRUE(account::create_account_for_email(temp_directory.path(), "player@example.com", "ValidPass1", 1700001000, &created_account, &error_message)) << error_message;
    ASSERT_TRUE(account::record_account_login_failure(temp_directory.path(), "player@example.com", "host.example.com", 1700002000, &error_message)) << error_message;

    EXPECT_TRUE(account::clear_account_login_failures(temp_directory.path(), created_account.account_name, &error_message)) << error_message;

    account::AccountData stored_account;
    ASSERT_TRUE(account::read_account_file(temp_directory.path(), created_account.account_name, &stored_account, &error_message)) << error_message;
    EXPECT_EQ(stored_account.failed_login_count, 0);
    EXPECT_EQ(stored_account.failed_login_last_at, 0);
    EXPECT_TRUE(stored_account.failed_login_last_host.empty());
}

TEST(AccountManagement, FormatsFailedLoginNoticeWithCountAndMostRecentAttempt)
{
    account::AccountData account_data;
    account_data.failed_login_count = 3;
    account_data.failed_login_last_at = 1700002500;
    account_data.failed_login_last_host = "host.example.com";

    const std::string notice = account::format_account_login_failure_notice(account_data);
    EXPECT_NE(notice.find("3 FAILED LOGIN ATTEMPTS SINCE YOUR LAST SUCCESSFUL LOGIN."), std::string::npos);
    EXPECT_NE(notice.find("Most recent: 2023-11-14 22:55:00 UTC from host.example.com"), std::string::npos);
}

TEST(AccountManagement, FormatsSingularFailedLoginNotice)
{
    account::AccountData account_data;
    account_data.failed_login_count = 1;
    account_data.failed_login_last_at = 1700002500;
    account_data.failed_login_last_host = "host.example.com";

    const std::string notice = account::format_account_login_failure_notice(account_data);
    EXPECT_NE(notice.find("1 FAILED LOGIN ATTEMPT SINCE YOUR LAST SUCCESSFUL LOGIN."), std::string::npos);
    EXPECT_EQ(notice.find("ATTEMPTS"), std::string::npos);
}

TEST(AccountManagement, OmitsTheFailedLoginNoticeWhenThereAreNoFailures)
{
    account::AccountData account_data;
    account_data.failed_login_count = 0;
    account_data.failed_login_last_at = 1700002500;
    account_data.failed_login_last_host = "host.example.com";

    EXPECT_TRUE(account::format_account_login_failure_notice(account_data).empty());
}

TEST(AccountManagement, OmitsTheHostFromTheFailedLoginNoticeWhenItIsUnknown)
{
    account::AccountData account_data;
    account_data.failed_login_count = 2;
    account_data.failed_login_last_at = 1700002500;

    const std::string notice = account::format_account_login_failure_notice(account_data);
    EXPECT_NE(notice.find("Most recent: 2023-11-14 22:55:00 UTC\n\r"), std::string::npos);
    EXPECT_EQ(notice.find(" from "), std::string::npos);
}

TEST(AccountManagement, RoundTripsPasswordResetCodeMetadataThroughAccountJson)
{
    account::AccountData original_account = make_account();
    original_account.password_reset_code_hash = "reset-code-hash";
    original_account.password_reset_code_sent_at = 1700030000;
    original_account.password_reset_code_expires_at = 1700030900;
    original_account.password_reset_attempt_count = 2;

    account::AccountData parsed_account;
    std::string error_message;
    ASSERT_TRUE(account::deserialize_account_from_json(
        account::serialize_account_to_json(original_account), &parsed_account, &error_message))
        << error_message;

    EXPECT_EQ(parsed_account.password_reset_code_hash, "reset-code-hash");
    EXPECT_EQ(parsed_account.password_reset_code_sent_at, 1700030000);
    EXPECT_EQ(parsed_account.password_reset_code_expires_at, 1700030900);
    EXPECT_EQ(parsed_account.password_reset_attempt_count, 2);
}

TEST(AccountManagement, DefaultsPasswordResetCodeMetadataWhenAccountJsonOmitsIt)
{
    const std::string legacy_json = "{\n"
                                    "  \"version\": 1,\n"
                                    "  \"account_name\": \"alpha-admin\",\n"
                                    "  \"normalized_email\": \"player@example.com\"\n"
                                    "}\n";

    account::AccountData parsed_account;
    std::string error_message;
    ASSERT_TRUE(account::deserialize_account_from_json(legacy_json, &parsed_account, &error_message)) << error_message;

    EXPECT_TRUE(parsed_account.password_reset_code_hash.empty());
    EXPECT_EQ(parsed_account.password_reset_code_sent_at, 0);
    EXPECT_EQ(parsed_account.password_reset_code_expires_at, 0);
    EXPECT_EQ(parsed_account.password_reset_attempt_count, 0);
}

TEST(AccountManagement, KeepsResetAndVerificationAttemptCapsUniform)
{
    EXPECT_EQ(account::MAX_PASSWORD_RESET_ATTEMPTS, account::MAX_EMAIL_VERIFICATION_ATTEMPTS);
    EXPECT_EQ(account::PASSWORD_RESET_WINDOW_SECONDS, account::EMAIL_VERIFICATION_WINDOW_SECONDS);
    EXPECT_EQ(account::PASSWORD_RESET_RESEND_COOLDOWN_SECONDS, account::EMAIL_VERIFICATION_RESEND_COOLDOWN_SECONDS);
}

TEST(AccountManagement, StartPasswordResetIgnoresAddressesWithoutAnAccount)
{
    TemporaryDirectory temp_directory;
    std::string error_message;
    long code_expires_at = 0;

    EXPECT_TRUE(account::start_password_reset(temp_directory.path(), "nobody@example.com", 1700002000, &code_expires_at, &error_message)) << error_message;

    EXPECT_EQ(code_expires_at, 1700002000 + account::PASSWORD_RESET_WINDOW_SECONDS);
    struct stat file_info { };
    EXPECT_NE(stat(account::account_file_path(temp_directory.path(), "nobody@example.com").c_str(), &file_info), 0);
}

TEST(AccountManagement, StartPasswordResetStoresAHashedCodeAndMailsIt)
{
    TemporaryDirectory temp_directory;
    const std::string root = temp_directory.path();
    const std::string capture_path = root + "/captured-mail.txt";
    const std::string command_script_path = root + "/capture-sendmail.sh";
    write_text_file(command_script_path,
        "#!/bin/sh\n"
        "cat > \""
            + capture_path + "\"\n");
    make_file_executable(command_script_path);
    ScopedEnvironmentVariable sendmail_override("ROTS_SENDMAIL_COMMAND", command_script_path);

    account::AccountData created_account;
    std::string error_message;
    ASSERT_TRUE(account::create_account(root, "alpha-admin", "player@example.com", "ValidPass1", 1700001000, &created_account, &error_message)) << error_message;

    long code_expires_at = 0;
    ASSERT_TRUE(account::start_password_reset(root, "player@example.com", 1700002000, &code_expires_at, &error_message)) << error_message;

    EXPECT_EQ(code_expires_at, 1700002000 + account::PASSWORD_RESET_WINDOW_SECONDS);

    account::AccountData stored_account;
    ASSERT_TRUE(account::read_account_file(root, "alpha-admin", &stored_account, &error_message)) << error_message;
    EXPECT_FALSE(stored_account.password_reset_code_hash.empty());
    EXPECT_EQ(stored_account.password_reset_code_sent_at, 1700002000);
    EXPECT_EQ(stored_account.password_reset_code_expires_at, 1700002000 + account::PASSWORD_RESET_WINDOW_SECONDS);
    EXPECT_EQ(stored_account.password_reset_attempt_count, 0);

    const std::string captured_mail = read_file_contents(capture_path);
    EXPECT_NE(captured_mail.find("To: player@example.com"), std::string::npos);
    EXPECT_NE(captured_mail.find("Subject: RotS account password reset code"), std::string::npos);
    EXPECT_NE(captured_mail.find("Password reset code: "), std::string::npos);
    // The plaintext code must never be what we stored.
    EXPECT_EQ(captured_mail.find(stored_account.password_reset_code_hash), std::string::npos);
}

TEST(AccountManagement, StartPasswordResetSuppressesASecondCodeInsideTheCooldown)
{
    TemporaryDirectory temp_directory;
    const std::string root = temp_directory.path();
    const std::string command_script_path = root + "/discard-sendmail.sh";
    write_text_file(command_script_path, "#!/bin/sh\ncat > /dev/null\n");
    make_file_executable(command_script_path);
    ScopedEnvironmentVariable sendmail_override("ROTS_SENDMAIL_COMMAND", command_script_path);

    account::AccountData created_account;
    std::string error_message;
    ASSERT_TRUE(account::create_account(root, "alpha-admin", "player@example.com", "ValidPass1", 1700001000, &created_account, &error_message)) << error_message;

    long first_expiry = 0;
    ASSERT_TRUE(account::start_password_reset(root, "player@example.com", 1700002000, &first_expiry, &error_message)) << error_message;

    account::AccountData after_first;
    ASSERT_TRUE(account::read_account_file(root, "alpha-admin", &after_first, &error_message)) << error_message;

    long second_expiry = 0;
    const long inside_cooldown = 1700002000 + account::PASSWORD_RESET_RESEND_COOLDOWN_SECONDS - 1;
    ASSERT_TRUE(account::start_password_reset(root, "player@example.com", inside_cooldown, &second_expiry, &error_message)) << error_message;

    account::AccountData after_second;
    ASSERT_TRUE(account::read_account_file(root, "alpha-admin", &after_second, &error_message)) << error_message;

    // The first code is untouched and still the one that works.
    EXPECT_EQ(after_second.password_reset_code_hash, after_first.password_reset_code_hash);
    EXPECT_EQ(after_second.password_reset_code_sent_at, 1700002000);
    // The reported expiry is synthetic, not the stored one -- see the test below.
    EXPECT_EQ(second_expiry, inside_cooldown + account::PASSWORD_RESET_WINDOW_SECONDS);
}

// The caller turns *code_expires_at into a connection deadline the player can time. Reporting the
// real pending expiry inside the cooldown made that deadline depend on when an earlier code was
// issued, so an attacker could tell an address with an account from one without by watching the
// clock. Every branch must report the same clock-derived value.
TEST(AccountManagement, StartPasswordResetReportsASyntheticExpiryInsideTheCooldown)
{
    TemporaryDirectory temp_directory;
    const std::string root = temp_directory.path();
    const std::string command_script_path = root + "/discard-sendmail.sh";
    write_text_file(command_script_path, "#!/bin/sh\ncat > /dev/null\n");
    make_file_executable(command_script_path);
    ScopedEnvironmentVariable sendmail_override("ROTS_SENDMAIL_COMMAND", command_script_path);

    account::AccountData created_account;
    std::string error_message;
    ASSERT_TRUE(account::create_account(root, "alpha-admin", "player@example.com", "ValidPass1", 1700001000, &created_account, &error_message)) << error_message;

    long first_expiry = 0;
    ASSERT_TRUE(account::start_password_reset(root, "player@example.com", 1700002000, &first_expiry, &error_message)) << error_message;

    account::AccountData after_first;
    ASSERT_TRUE(account::read_account_file(root, "alpha-admin", &after_first, &error_message)) << error_message;

    long cooldown_expiry = 0;
    const long inside_cooldown = 1700002000 + account::PASSWORD_RESET_RESEND_COOLDOWN_SECONDS - 1;
    ASSERT_TRUE(account::start_password_reset(root, "player@example.com", inside_cooldown, &cooldown_expiry, &error_message)) << error_message;

    EXPECT_EQ(cooldown_expiry, inside_cooldown + account::PASSWORD_RESET_WINDOW_SECONDS);
    EXPECT_NE(cooldown_expiry, after_first.password_reset_code_expires_at);

    // An address with no account at all reports the same thing, which is the whole point.
    long unknown_expiry = 0;
    ASSERT_TRUE(account::start_password_reset(root, "nobody@example.com", inside_cooldown, &unknown_expiry, &error_message)) << error_message;
    EXPECT_EQ(unknown_expiry, cooldown_expiry);
}

TEST(AccountManagement, StartPasswordResetIssuesAFreshCodeOnceTheCooldownLapses)
{
    TemporaryDirectory temp_directory;
    const std::string root = temp_directory.path();
    const std::string command_script_path = root + "/discard-sendmail.sh";
    write_text_file(command_script_path, "#!/bin/sh\ncat > /dev/null\n");
    make_file_executable(command_script_path);
    ScopedEnvironmentVariable sendmail_override("ROTS_SENDMAIL_COMMAND", command_script_path);

    account::AccountData created_account;
    std::string error_message;
    ASSERT_TRUE(account::create_account(root, "alpha-admin", "player@example.com", "ValidPass1", 1700001000, &created_account, &error_message)) << error_message;

    long expiry = 0;
    ASSERT_TRUE(account::start_password_reset(root, "player@example.com", 1700002000, &expiry, &error_message)) << error_message;
    account::AccountData after_first;
    ASSERT_TRUE(account::read_account_file(root, "alpha-admin", &after_first, &error_message)) << error_message;

    const long past_cooldown = 1700002000 + account::PASSWORD_RESET_RESEND_COOLDOWN_SECONDS;
    ASSERT_TRUE(account::start_password_reset(root, "player@example.com", past_cooldown, &expiry, &error_message)) << error_message;
    account::AccountData after_second;
    ASSERT_TRUE(account::read_account_file(root, "alpha-admin", &after_second, &error_message)) << error_message;

    EXPECT_NE(after_second.password_reset_code_hash, after_first.password_reset_code_hash);
    EXPECT_EQ(after_second.password_reset_code_sent_at, past_cooldown);
}

TEST(AccountManagement, StartPasswordResetLeavesAPendingEmailVerificationCodeAlone)
{
    TemporaryDirectory temp_directory;
    const std::string root = temp_directory.path();
    const std::string command_script_path = root + "/discard-sendmail.sh";
    write_text_file(command_script_path, "#!/bin/sh\ncat > /dev/null\n");
    make_file_executable(command_script_path);
    ScopedEnvironmentVariable sendmail_override("ROTS_SENDMAIL_COMMAND", command_script_path);

    account::AccountData created_account;
    std::string error_message;
    ASSERT_TRUE(account::create_account(root, "alpha-admin", "player@example.com", "ValidPass1", 1700001000, &created_account, &error_message)) << error_message;

    std::string verification_code;
    ASSERT_TRUE(account::prepare_email_verification_code(&created_account, 1700001500, &verification_code, &error_message)) << error_message;
    ASSERT_TRUE(account::write_account_file(root, created_account, &error_message)) << error_message;

    long expiry = 0;
    ASSERT_TRUE(account::start_password_reset(root, "player@example.com", 1700002000, &expiry, &error_message)) << error_message;

    account::AccountData stored_account;
    ASSERT_TRUE(account::read_account_file(root, "alpha-admin", &stored_account, &error_message)) << error_message;
    EXPECT_EQ(stored_account.verification_code_hash, created_account.verification_code_hash);
    EXPECT_EQ(stored_account.verification_code_expires_at, created_account.verification_code_expires_at);
    EXPECT_NE(stored_account.password_reset_code_hash, stored_account.verification_code_hash);
}

namespace {

// Issues a reset code and returns the plaintext by capturing the outgoing mail.
std::string issue_reset_code(const std::string& root, const std::string& capture_path, long sent_at)
{
    long expiry = 0;
    std::string error_message;
    EXPECT_TRUE(account::start_password_reset(root, "player@example.com", sent_at, &expiry, &error_message)) << error_message;

    const std::string captured_mail = read_file_contents(capture_path);
    const std::string marker = "Password reset code: ";
    const size_t code_offset = captured_mail.find(marker);
    EXPECT_NE(code_offset, std::string::npos) << "Expected a reset code in the captured mail.";
    if (code_offset == std::string::npos)
        return "";
    return captured_mail.substr(code_offset + marker.size(), 6);
}

} // namespace

TEST(AccountManagement, CompletePasswordResetChangesThePasswordAndClearsResetState)
{
    TemporaryDirectory temp_directory;
    const std::string root = temp_directory.path();
    const std::string capture_path = root + "/captured-mail.txt";
    const std::string command_script_path = root + "/capture-sendmail.sh";
    write_text_file(command_script_path, "#!/bin/sh\ncat > \"" + capture_path + "\"\n");
    make_file_executable(command_script_path);
    ScopedEnvironmentVariable sendmail_override("ROTS_SENDMAIL_COMMAND", command_script_path);

    account::AccountData created_account;
    std::string error_message;
    ASSERT_TRUE(account::create_account(root, "alpha-admin", "player@example.com", "ValidPass1", 1700001000, &created_account, &error_message)) << error_message;

    // Give it failed logins and an unverified address, both of which a completed reset should settle.
    ASSERT_TRUE(account::record_account_login_failure(root, "player@example.com", "attacker.example.com", 1700001500, &error_message)) << error_message;

    const std::string reset_code = issue_reset_code(root, capture_path, 1700002000);
    ASSERT_FALSE(reset_code.empty());

    account::AccountData reset_account;
    ASSERT_TRUE(account::complete_password_reset(root, "player@example.com", reset_code, "BrandNew1", 1700002100, &reset_account, &error_message)) << error_message;

    account::AccountData stored_account;
    ASSERT_TRUE(account::read_account_file(root, "alpha-admin", &stored_account, &error_message)) << error_message;

    EXPECT_TRUE(account::verify_password("BrandNew1", stored_account.password_hash));
    EXPECT_FALSE(account::verify_password("ValidPass1", stored_account.password_hash));
    EXPECT_EQ(stored_account.password_reset_by, "forgot-password");
    EXPECT_EQ(stored_account.password_reset_at, 1700002100);
    EXPECT_TRUE(stored_account.password_reset_code_hash.empty());
    EXPECT_EQ(stored_account.password_reset_code_sent_at, 0);
    EXPECT_EQ(stored_account.password_reset_code_expires_at, 0);
    EXPECT_EQ(stored_account.password_reset_attempt_count, 0);
    EXPECT_TRUE(stored_account.email_verified);
    EXPECT_EQ(stored_account.failed_login_count, 0);
    EXPECT_TRUE(stored_account.failed_login_last_host.empty());
}

TEST(AccountManagement, CompletePasswordResetCountsWrongCodesAndInvalidatesAtTheCap)
{
    TemporaryDirectory temp_directory;
    const std::string root = temp_directory.path();
    const std::string capture_path = root + "/captured-mail.txt";
    const std::string command_script_path = root + "/capture-sendmail.sh";
    write_text_file(command_script_path, "#!/bin/sh\ncat > \"" + capture_path + "\"\n");
    make_file_executable(command_script_path);
    ScopedEnvironmentVariable sendmail_override("ROTS_SENDMAIL_COMMAND", command_script_path);

    account::AccountData created_account;
    std::string error_message;
    ASSERT_TRUE(account::create_account(root, "alpha-admin", "player@example.com", "ValidPass1", 1700001000, &created_account, &error_message)) << error_message;
    const std::string reset_code = issue_reset_code(root, capture_path, 1700002000);
    ASSERT_FALSE(reset_code.empty());

    for (int attempt = 1; attempt < account::MAX_PASSWORD_RESET_ATTEMPTS; ++attempt) {
        EXPECT_FALSE(account::complete_password_reset(root, "player@example.com", "000000", "BrandNew1", 1700002000 + attempt, nullptr, &error_message));
        account::AccountData in_progress;
        ASSERT_TRUE(account::read_account_file(root, "alpha-admin", &in_progress, &error_message)) << error_message;
        EXPECT_EQ(in_progress.password_reset_attempt_count, attempt);
        EXPECT_FALSE(in_progress.password_reset_code_hash.empty());
    }

    EXPECT_FALSE(account::complete_password_reset(root, "player@example.com", "000000", "BrandNew1", 1700002090, nullptr, &error_message));

    account::AccountData stored_account;
    ASSERT_TRUE(account::read_account_file(root, "alpha-admin", &stored_account, &error_message)) << error_message;
    EXPECT_TRUE(stored_account.password_reset_code_hash.empty());
    EXPECT_EQ(stored_account.password_reset_code_expires_at, 0);
    // The real code is dead too, so a reconnect cannot resume with it.
    EXPECT_FALSE(account::complete_password_reset(root, "player@example.com", reset_code, "BrandNew1", 1700002095, nullptr, &error_message));
    EXPECT_TRUE(account::verify_password("ValidPass1", stored_account.password_hash));
}

// Clearing password_reset_code_sent_at at the cap switched the resend cooldown off, so five wrong
// guesses bought an immediate fresh code -- unlimited mail to a known address, and a victim who
// could never finish a reset because each code they received was killed by the next five guesses.
TEST(AccountManagement, StartPasswordResetStillHonoursTheCooldownAfterTheAttemptCap)
{
    TemporaryDirectory temp_directory;
    const std::string root = temp_directory.path();
    const std::string capture_path = root + "/captured-mail.txt";
    const std::string command_script_path = root + "/capture-sendmail.sh";
    write_text_file(command_script_path, "#!/bin/sh\ncat > \"" + capture_path + "\"\n");
    make_file_executable(command_script_path);
    ScopedEnvironmentVariable sendmail_override("ROTS_SENDMAIL_COMMAND", command_script_path);

    account::AccountData created_account;
    std::string error_message;
    ASSERT_TRUE(account::create_account(root, "alpha-admin", "player@example.com", "ValidPass1", 1700001000, &created_account, &error_message)) << error_message;
    const std::string reset_code = issue_reset_code(root, capture_path, 1700002000);
    ASSERT_FALSE(reset_code.empty());

    for (int attempt = 1; attempt <= account::MAX_PASSWORD_RESET_ATTEMPTS; ++attempt)
        EXPECT_FALSE(account::verify_password_reset_code(root, "player@example.com", "000000", 1700002000 + attempt, &error_message));

    account::AccountData exhausted_account;
    ASSERT_TRUE(account::read_account_file(root, "alpha-admin", &exhausted_account, &error_message)) << error_message;
    EXPECT_TRUE(exhausted_account.password_reset_code_hash.empty());
    EXPECT_EQ(exhausted_account.password_reset_code_expires_at, 0);
    // The send timestamp survives the cap: it is the only thing the cooldown gate reads.
    EXPECT_EQ(exhausted_account.password_reset_code_sent_at, 1700002000);

    long expiry = 0;
    const long inside_cooldown = 1700002000 + account::PASSWORD_RESET_RESEND_COOLDOWN_SECONDS - 1;
    ASSERT_TRUE(account::start_password_reset(root, "player@example.com", inside_cooldown, &expiry, &error_message)) << error_message;

    account::AccountData after_request;
    ASSERT_TRUE(account::read_account_file(root, "alpha-admin", &after_request, &error_message)) << error_message;
    // Still suppressed: no new code stamped, so nothing new was mailed either.
    EXPECT_TRUE(after_request.password_reset_code_hash.empty());
    EXPECT_EQ(after_request.password_reset_code_sent_at, 1700002000);

    // Once the cooldown genuinely lapses a fresh code is issued as normal.
    const long past_cooldown = 1700002000 + account::PASSWORD_RESET_RESEND_COOLDOWN_SECONDS;
    ASSERT_TRUE(account::start_password_reset(root, "player@example.com", past_cooldown, &expiry, &error_message)) << error_message;

    account::AccountData after_cooldown;
    ASSERT_TRUE(account::read_account_file(root, "alpha-admin", &after_cooldown, &error_message)) << error_message;
    EXPECT_FALSE(after_cooldown.password_reset_code_hash.empty());
    EXPECT_EQ(after_cooldown.password_reset_code_sent_at, past_cooldown);
    EXPECT_EQ(after_cooldown.password_reset_attempt_count, 0);
}

TEST(AccountManagement, CompletePasswordResetRejectsAnExpiredCode)
{
    TemporaryDirectory temp_directory;
    const std::string root = temp_directory.path();
    const std::string capture_path = root + "/captured-mail.txt";
    const std::string command_script_path = root + "/capture-sendmail.sh";
    write_text_file(command_script_path, "#!/bin/sh\ncat > \"" + capture_path + "\"\n");
    make_file_executable(command_script_path);
    ScopedEnvironmentVariable sendmail_override("ROTS_SENDMAIL_COMMAND", command_script_path);

    account::AccountData created_account;
    std::string error_message;
    ASSERT_TRUE(account::create_account(root, "alpha-admin", "player@example.com", "ValidPass1", 1700001000, &created_account, &error_message)) << error_message;
    const std::string reset_code = issue_reset_code(root, capture_path, 1700002000);
    ASSERT_FALSE(reset_code.empty());

    const long after_expiry = 1700002000 + account::PASSWORD_RESET_WINDOW_SECONDS + 1;
    EXPECT_FALSE(account::complete_password_reset(root, "player@example.com", reset_code, "BrandNew1", after_expiry, nullptr, &error_message));

    account::AccountData stored_account;
    ASSERT_TRUE(account::read_account_file(root, "alpha-admin", &stored_account, &error_message)) << error_message;
    EXPECT_TRUE(account::verify_password("ValidPass1", stored_account.password_hash));
}

TEST(AccountManagement, CompletePasswordResetRejectsAddressesWithoutAnAccount)
{
    TemporaryDirectory temp_directory;
    std::string error_message;
    EXPECT_FALSE(account::complete_password_reset(temp_directory.path(), "nobody@example.com", "000000", "BrandNew1", 1700002000, nullptr, &error_message));
}

TEST(AccountManagement, CompletePasswordResetRejectsAnEmailVerificationCode)
{
    TemporaryDirectory temp_directory;
    const std::string root = temp_directory.path();
    account::AccountData created_account;
    std::string error_message;
    ASSERT_TRUE(account::create_account(root, "alpha-admin", "player@example.com", "ValidPass1", 1700001000, &created_account, &error_message)) << error_message;

    std::string verification_code;
    ASSERT_TRUE(account::prepare_email_verification_code(&created_account, 1700001500, &verification_code, &error_message)) << error_message;
    ASSERT_TRUE(account::write_account_file(root, created_account, &error_message)) << error_message;

    // No reset has been started, so a verification code must not stand in for one.
    EXPECT_FALSE(account::complete_password_reset(root, "player@example.com", verification_code, "BrandNew1", 1700002000, nullptr, &error_message));

    account::AccountData stored_account;
    ASSERT_TRUE(account::read_account_file(root, "alpha-admin", &stored_account, &error_message)) << error_message;
    EXPECT_TRUE(account::verify_password("ValidPass1", stored_account.password_hash));
}

TEST(AccountManagement, CompletePasswordResetRejectsAPasswordFailingPolicy)
{
    TemporaryDirectory temp_directory;
    const std::string root = temp_directory.path();
    const std::string capture_path = root + "/captured-mail.txt";
    const std::string command_script_path = root + "/capture-sendmail.sh";
    write_text_file(command_script_path, "#!/bin/sh\ncat > \"" + capture_path + "\"\n");
    make_file_executable(command_script_path);
    ScopedEnvironmentVariable sendmail_override("ROTS_SENDMAIL_COMMAND", command_script_path);

    account::AccountData created_account;
    std::string error_message;
    ASSERT_TRUE(account::create_account(root, "alpha-admin", "player@example.com", "ValidPass1", 1700001000, &created_account, &error_message)) << error_message;
    const std::string reset_code = issue_reset_code(root, capture_path, 1700002000);
    ASSERT_FALSE(reset_code.empty());

    EXPECT_FALSE(account::complete_password_reset(root, "player@example.com", reset_code, "short", 1700002100, nullptr, &error_message));

    // The code survives so the player can retry with a better password.
    account::AccountData stored_account;
    ASSERT_TRUE(account::read_account_file(root, "alpha-admin", &stored_account, &error_message)) << error_message;
    EXPECT_FALSE(stored_account.password_reset_code_hash.empty());
    EXPECT_TRUE(account::verify_password("ValidPass1", stored_account.password_hash));
}

TEST(AccountManagement, CompletePasswordResetChecksTheCodeBeforeThePasswordPolicy)
{
    TemporaryDirectory temp_directory;
    const std::string root = temp_directory.path();
    const std::string capture_path = root + "/captured-mail.txt";
    const std::string command_script_path = root + "/capture-sendmail.sh";
    write_text_file(command_script_path, "#!/bin/sh\ncat > \"" + capture_path + "\"\n");
    make_file_executable(command_script_path);
    ScopedEnvironmentVariable sendmail_override("ROTS_SENDMAIL_COMMAND", command_script_path);

    account::AccountData created_account;
    std::string error_message;
    ASSERT_TRUE(account::create_account(root, "alpha-admin", "player@example.com", "ValidPass1", 1700001000, &created_account, &error_message)) << error_message;
    const std::string reset_code = issue_reset_code(root, capture_path, 1700002000);
    ASSERT_FALSE(reset_code.empty());

    // Wrong code AND a policy-failing password. If the policy check ran first, this would surface
    // the password error and tell an attacker nothing about the guessed code; it must instead fail
    // on the code, before the password is ever looked at.
    EXPECT_FALSE(account::complete_password_reset(root, "player@example.com", "000000", "short", 1700002050, nullptr, &error_message));
    EXPECT_EQ(error_message, "That reset code is invalid.");

    account::AccountData stored_account;
    ASSERT_TRUE(account::read_account_file(root, "alpha-admin", &stored_account, &error_message)) << error_message;
    EXPECT_EQ(stored_account.password_reset_attempt_count, 1);
    EXPECT_TRUE(account::verify_password("ValidPass1", stored_account.password_hash));
}

TEST(AccountManagement, VerifyPasswordResetCodeAcceptsWithoutConsumingTheCode)
{
    TemporaryDirectory temp_directory;
    const std::string root = temp_directory.path();
    const std::string capture_path = root + "/captured-mail.txt";
    const std::string command_script_path = root + "/capture-sendmail.sh";
    write_text_file(command_script_path, "#!/bin/sh\ncat > \"" + capture_path + "\"\n");
    make_file_executable(command_script_path);
    ScopedEnvironmentVariable sendmail_override("ROTS_SENDMAIL_COMMAND", command_script_path);

    account::AccountData created_account;
    std::string error_message;
    ASSERT_TRUE(account::create_account(root, "alpha-admin", "player@example.com", "ValidPass1", 1700001000, &created_account, &error_message)) << error_message;
    const std::string reset_code = issue_reset_code(root, capture_path, 1700002000);
    ASSERT_FALSE(reset_code.empty());

    EXPECT_TRUE(account::verify_password_reset_code(root, "player@example.com", reset_code, 1700002050, &error_message)) << error_message;

    account::AccountData after_verify;
    ASSERT_TRUE(account::read_account_file(root, "alpha-admin", &after_verify, &error_message)) << error_message;
    EXPECT_EQ(after_verify.password_reset_attempt_count, 0);
    EXPECT_FALSE(after_verify.password_reset_code_hash.empty());

    // The code still completes the reset afterwards -- checking it did not spend it.
    EXPECT_TRUE(account::complete_password_reset(root, "player@example.com", reset_code, "BrandNew1", 1700002100, nullptr, &error_message)) << error_message;
}

TEST(AccountManagement, VerifyPasswordResetCodeCountsWrongCodesLikeTheCompletingCall)
{
    TemporaryDirectory temp_directory;
    const std::string root = temp_directory.path();
    const std::string capture_path = root + "/captured-mail.txt";
    const std::string command_script_path = root + "/capture-sendmail.sh";
    write_text_file(command_script_path, "#!/bin/sh\ncat > \"" + capture_path + "\"\n");
    make_file_executable(command_script_path);
    ScopedEnvironmentVariable sendmail_override("ROTS_SENDMAIL_COMMAND", command_script_path);

    account::AccountData created_account;
    std::string error_message;
    ASSERT_TRUE(account::create_account(root, "alpha-admin", "player@example.com", "ValidPass1", 1700001000, &created_account, &error_message)) << error_message;
    const std::string reset_code = issue_reset_code(root, capture_path, 1700002000);
    ASSERT_FALSE(reset_code.empty());

    EXPECT_FALSE(account::verify_password_reset_code(root, "player@example.com", "000000", 1700002050, &error_message));

    account::AccountData after_verify;
    ASSERT_TRUE(account::read_account_file(root, "alpha-admin", &after_verify, &error_message)) << error_message;
    EXPECT_EQ(after_verify.password_reset_attempt_count, 1);
}

namespace {

// SetUp/TearDown (rather than toggling roster_cache inline in the test body) so cleanup still runs
// if an ASSERT_* fails partway through: an inline toggle leaves g_enabled == true leaked into every
// later test in the binary on a failed assertion, in a suite that already aborts partway through on
// a known baseline crash. Mirrors RosterCacheTest in roster_cache_tests.cpp.
class RosterCacheDropOnWriteTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        roster_cache::clear();
        roster_cache::set_enabled(true);
    }
    void TearDown() override
    {
        roster_cache::set_enabled(false);
        roster_cache::clear();
    }
};

} // namespace

// write_account_character_file is the single chokepoint for character-file writes (5 call sites,
// including save_char's autosave path). If it does not drop the cached summary, a level-up would
// leave the roster showing the old level indefinitely.
TEST_F(RosterCacheDropOnWriteTest, WritingACharacterFileDropsItsCachedRosterSummary)
{
    TemporaryDirectory temp_directory;
    ScopedWorkingDirectory working_directory(temp_directory.path());
    ASSERT_EQ(mkdir("accounts", 0700), 0);
    ASSERT_EQ(mkdir("accounts/A-E", 0700), 0);

    account::AccountData account_data = make_account();
    std::string error_message;
    ASSERT_TRUE(account::create_account(".", account_data.account_name, account_data.normalized_email,
        "ValidPass1", 1700010200, nullptr, &error_message))
        << error_message;

    char_file_u aragorn = make_stored_character("aragorn");
    aragorn.level = 10;
    aragorn.race = RACE_WOOD;
    ASSERT_TRUE(account::write_account_character_file(".", account_data.account_name, aragorn, &error_message))
        << error_message;

    roster_cache::RosterSummary summary {};
    ASSERT_TRUE(roster_cache::get(".", account_data.account_name, "aragorn", &summary));
    ASSERT_EQ(summary.level, 10);

    aragorn.level = 11;
    ASSERT_TRUE(account::write_account_character_file(".", account_data.account_name, aragorn, &error_message))
        << error_message;

    ASSERT_TRUE(roster_cache::get(".", account_data.account_name, "aragorn", &summary));
    EXPECT_EQ(summary.level, 11) << "cached summary survived a character write";
}

namespace {

// Builds an account whose characters are deliberately NOT in name, level, or race order, so a
// passing sort test cannot be an accident of insertion order. gimli/aragorn/legolas are all light-
// side races (side rank 1), so "ugluk" (dark-side, an Orc) is included too -- without it, every
// Side-sort render in this fixture would collapse to exactly one section, and a test could pass
// while never exercising row numbering across a section boundary.
account::AccountData make_sortable_account()
{
    account::AccountData account_data = make_account();
    account_data.characters = { "gimli", "aragorn", "legolas", "ugluk" };
    return account_data;
}

// Backing reader for ordering tests: level/race/coefficients per character name. Asserts the root
// directory it was called with, so a caller that hardcodes (or drifts to) a different root than the
// renderer used -- rather than threading root_directory through -- fails loudly here instead of
// silently reading a disjoint (and therefore all-unreadable) cache entry. Every caller in this file
// uses "." by convention.
bool sortable_reader(const std::string& root_directory, const std::string&, const std::string& character_name,
    char_file_u* stored_character, std::string* error_message)
{
    EXPECT_EQ(root_directory, ".") << "reader invoked with an unexpected root directory -- selection and "
                                       "rendering must resolve roster_cache entries against the same root";
    *stored_character = char_file_u {};
    if (character_name == "gimli") {
        stored_character->level = 30;
        stored_character->race = RACE_DWARF;
        stored_character->profs.prof_coof[PROF_WARRIOR] = 160;
    } else if (character_name == "aragorn") {
        stored_character->level = 50;
        stored_character->race = RACE_HUMAN;
        stored_character->profs.prof_coof[PROF_RANGER] = 160;
    } else if (character_name == "legolas") {
        stored_character->level = 40;
        stored_character->race = RACE_WOOD;
        stored_character->profs.prof_coof[PROF_MAGE] = 160;
    } else if (character_name == "ugluk") {
        // Lowest level and highest race index of the four, so it sorts last under both Level and
        // Race without disturbing the other three's relative order. Its only nonzero coefficient is
        // Mystic (Cleric), so it is cleanly excluded from the Warrior/Ranger/Mage filters below and
        // is the sole match for Mystic -- rather than tying with every profession at the raw-0
        // baseline and muddying every filter's expected result.
        stored_character->level = 20;
        stored_character->race = RACE_ORC;
        stored_character->profs.prof_coof[PROF_CLERIC] = 160;
    } else {
        if (error_message)
            *error_message = "unknown character";
        return false;
    }
    if (error_message)
        *error_message = "";
    return true;
}

std::vector<std::string> names_in_order(const account::AccountData& account_data,
    account::RosterSort sort, account::RosterFilter filter)
{
    const std::vector<size_t> indices = account::ordered_roster_indices(".", account_data, sort, filter);
    std::vector<std::string> names;
    for (size_t index : indices)
        names.push_back(account_data.characters[index]);
    return names;
}

// Parses "N) [ lvl race] Name" row entries out of rendered roster/prompt text, in the order they
// appear (two entries can share one line, so this scans the whole text rather than per-line). Row
// numbers are returned exactly as printed and left unvalidated here -- callers assert the
// numbering themselves, so a renderer that skips, repeats, or 0-indexes rows fails the assertion
// rather than being silently reindexed away by the parser.
std::vector<std::pair<int, std::string>> parse_rendered_roster_rows(const std::string& prompt)
{
    static const std::regex kRowPattern(R"((\d+)\)\s*\[[^\]]*\]\s*(\S+))");
    std::vector<std::pair<int, std::string>> rows;
    for (std::sregex_iterator it(prompt.begin(), prompt.end(), kRowPattern), end; it != end; ++it)
        rows.emplace_back(std::stoi((*it)[1].str()), (*it)[2].str());
    return rows;
}

// Splits rendered text into lines on the renderer's own "\n\r" line terminator, so a test can
// assert actual line structure (which row starts a fresh line, which line a header sits alone on)
// rather than only which substrings appear somewhere in the blob. A trailing empty element would
// only ever appear if the text does not end on a terminator; every caller here asserts on that too.
std::vector<std::string> split_on_terminator(const std::string& text)
{
    std::vector<std::string> lines;
    size_t start = 0;
    while (start <= text.size()) {
        const size_t terminator = text.find("\n\r", start);
        if (terminator == std::string::npos) {
            lines.push_back(text.substr(start));
            break;
        }
        lines.push_back(text.substr(start, terminator - start));
        start = terminator + 2;
    }
    return lines;
}

class RosterOrderTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        roster_cache::clear();
        roster_cache::set_backing_reader_for_testing(&sortable_reader);
        roster_cache::set_enabled(true);
    }
    void TearDown() override
    {
        roster_cache::set_backing_reader_for_testing(nullptr);
        roster_cache::set_enabled(false);
        roster_cache::clear();
    }
};

} // namespace

TEST_F(RosterOrderTest, AccountSortPreservesInsertionOrder)
{
    const account::AccountData account_data = make_sortable_account();
    EXPECT_EQ(names_in_order(account_data, account::RosterSort::Account, account::RosterFilter::None),
        (std::vector<std::string> { "gimli", "aragorn", "legolas", "ugluk" }));
}

TEST_F(RosterOrderTest, NameSortIsAlphabetical)
{
    const account::AccountData account_data = make_sortable_account();
    EXPECT_EQ(names_in_order(account_data, account::RosterSort::Name, account::RosterFilter::None),
        (std::vector<std::string> { "aragorn", "gimli", "legolas", "ugluk" }));
}

TEST_F(RosterOrderTest, LevelSortIsHighestFirst)
{
    const account::AccountData account_data = make_sortable_account();
    EXPECT_EQ(names_in_order(account_data, account::RosterSort::Level, account::RosterFilter::None),
        (std::vector<std::string> { "aragorn", "legolas", "gimli", "ugluk" }));
}

TEST_F(RosterOrderTest, RaceSortIsAscendingByRaceIndex)
{
    // RACE_HUMAN 1 < RACE_DWARF 2 < RACE_WOOD 3 < RACE_ORC 13
    const account::AccountData account_data = make_sortable_account();
    EXPECT_EQ(names_in_order(account_data, account::RosterSort::Race, account::RosterFilter::None),
        (std::vector<std::string> { "aragorn", "gimli", "legolas", "ugluk" }));
}

TEST_F(RosterOrderTest, FilterKeepsOnlyCharactersWhoseHighestCoefficientMatches)
{
    const account::AccountData account_data = make_sortable_account();
    EXPECT_EQ(names_in_order(account_data, account::RosterSort::Account, account::RosterFilter::Warrior),
        (std::vector<std::string> { "gimli" }));
    EXPECT_EQ(names_in_order(account_data, account::RosterSort::Account, account::RosterFilter::Ranger),
        (std::vector<std::string> { "aragorn" }));
    EXPECT_EQ(names_in_order(account_data, account::RosterSort::Account, account::RosterFilter::Mage),
        (std::vector<std::string> { "legolas" }));
    // ugluk's only nonzero coefficient is Mystic (Cleric), so it is the sole Mystic match.
    EXPECT_EQ(names_in_order(account_data, account::RosterSort::Account, account::RosterFilter::Mystic),
        (std::vector<std::string> { "ugluk" }));
}

TEST_F(RosterOrderTest, FilterAndSortCompose)
{
    account::AccountData account_data = make_sortable_account();
    account_data.characters.push_back("gimli"); // duplicate name is fine: same summary, same filter
    EXPECT_EQ(names_in_order(account_data, account::RosterSort::Level, account::RosterFilter::Warrior),
        (std::vector<std::string> { "gimli", "gimli" }));
}

// square_root[] is monotonic non-decreasing, so a filter that compared RAW prof_coof values would
// pass every other test in this file identically. This case only distinguishes raw from derived:
// raw says Mage (40 > 30), but the Uruk-mage penalty flips it (square_root[40]-100=532 <
// square_root[30]=547), so derived says Warrior.
TEST_F(RosterOrderTest, FilterUsesDerivedCoefficientsNotRawValues)
{
    account::AccountData account_data = make_account();
    account_data.characters = { "uruk1" };

    roster_cache::set_backing_reader_for_testing(
        [](const std::string&, const std::string&, const std::string&,
            char_file_u* stored_character, std::string* error_message) -> bool {
            *stored_character = char_file_u {};
            stored_character->race = RACE_URUK;
            stored_character->profs.prof_coof[PROF_MAGE] = 40;
            stored_character->profs.prof_coof[PROF_WARRIOR] = 30;
            if (error_message)
                *error_message = "";
            return true;
        });
    roster_cache::clear();

    EXPECT_EQ(names_in_order(account_data, account::RosterSort::Account, account::RosterFilter::Warrior),
        (std::vector<std::string> { "uruk1" }));
    EXPECT_TRUE(names_in_order(account_data, account::RosterSort::Account, account::RosterFilter::Mage).empty());
}

// stable_sort is load-bearing: a redraw must never reshuffle rows with equal sort keys. Two
// DISTINCT names ("zed" before "amy" in insertion order, the opposite of alphabetical) share a
// level, so only a genuinely stable sort reproduces this exact expected order.
TEST_F(RosterOrderTest, EqualSortKeysPreserveInsertionOrder)
{
    account::AccountData account_data = make_account();
    account_data.characters = { "zed", "mid", "amy" };

    roster_cache::set_backing_reader_for_testing(
        [](const std::string&, const std::string&, const std::string& character_name,
            char_file_u* stored_character, std::string* error_message) -> bool {
            *stored_character = char_file_u {};
            stored_character->race = RACE_HUMAN;
            stored_character->level = (character_name == "mid") ? 30 : 20; // zed and amy tie
            if (error_message)
                *error_message = "";
            return true;
        });
    roster_cache::clear();

    EXPECT_EQ(names_in_order(account_data, account::RosterSort::Level, account::RosterFilter::None),
        (std::vector<std::string> { "mid", "zed", "amy" }));
}

// Side sort groups by side and orders A-Z WITHIN each side, rather than leaving equal-side rows in
// link order. Without this, an account whose link order already happens to be side-grouped sees no
// visible change at all when pressing the side key.
TEST_F(RosterOrderTest, SideSortOrdersAlphabeticallyWithinEachSide)
{
    account::AccountData account_data = make_account();
    account_data.characters = { "zulu", "alpha", "orczz", "orcaa", "mike" };

    roster_cache::set_backing_reader_for_testing(
        [](const std::string& root_directory, const std::string&, const std::string& character_name,
            char_file_u* stored_character, std::string* error_message) -> bool {
            EXPECT_EQ(root_directory, ".");
            *stored_character = char_file_u {};
            if (character_name == "orczz" || character_name == "orcaa")
                stored_character->race = RACE_ORC;   // dark
            else
                stored_character->race = RACE_HUMAN; // light
            if (error_message)
                *error_message = "";
            return true;
        });
    roster_cache::clear();

    // Lights A-Z, then darks A-Z -- not link order within either side.
    EXPECT_EQ(names_in_order(account_data, account::RosterSort::Side, account::RosterFilter::None),
        (std::vector<std::string> { "alpha", "mike", "zulu", "orcaa", "orczz" }));
}

// The side view is rendered in labelled sections. Numbering stays continuous ACROSS sections, so
// row N still selects the character printed at row N -- the invariant the whole feature rests on.
TEST_F(RosterOrderTest, SideSortRendersLabelledSectionsWithContinuousNumbering)
{
    account::AccountData account_data = make_account();
    account_data.characters = { "orcaa", "human1", "godone" };

    roster_cache::set_backing_reader_for_testing(
        [](const std::string&, const std::string&, const std::string& character_name,
            char_file_u* stored_character, std::string* error_message) -> bool {
            *stored_character = char_file_u {};
            if (character_name == "orcaa")
                stored_character->race = RACE_ORC;
            else if (character_name == "godone")
                stored_character->race = RACE_GOD;
            else
                stored_character->race = RACE_HUMAN;
            if (error_message)
                *error_message = "";
            return true;
        });
    roster_cache::clear();

    const std::string prompt = account::format_account_character_prompt(
        ".", account_data, account::RosterSort::Side, account::RosterFilter::None);

    EXPECT_NE(prompt.find("-- Gods --"), std::string::npos) << prompt;
    EXPECT_NE(prompt.find("-- Good --"), std::string::npos) << prompt;
    EXPECT_NE(prompt.find("-- Evil --"), std::string::npos) << prompt;
    // No third-side character linked, so that header must not appear at all.
    EXPECT_EQ(prompt.find("-- Third Side --"), std::string::npos) << prompt;

    // Sections appear in side order, and numbering runs 1,2,3 straight through them.
    const size_t gods = prompt.find("-- Gods --");
    const size_t lights = prompt.find("-- Good --");
    const size_t darks = prompt.find("-- Evil --");
    EXPECT_LT(gods, lights);
    EXPECT_LT(lights, darks);
    EXPECT_NE(prompt.find("1) [  0 Imm] Godone"), std::string::npos) << prompt;
    EXPECT_NE(prompt.find("2) [  0 Hum] Human1"), std::string::npos) << prompt;
    EXPECT_NE(prompt.find("3) [  0 Orc] Orcaa"), std::string::npos) << prompt;

    // Other sorts must NOT gain section headers.
    const std::string by_level = account::format_account_character_prompt(
        ".", account_data, account::RosterSort::Level, account::RosterFilter::None);
    EXPECT_EQ(by_level.find("-- Gods --"), std::string::npos) << by_level;
}

// The renderer resets its column counter at each section boundary, so a section with an ODD row
// count does not drag the following section out of pair alignment. Three lights (odd) followed by
// two darks exercises exactly that: without the "column = 0" reset, the lone trailing light would
// pair up with the first dark on one line instead of each section starting fresh; and a final-flush
// check that tested indices.size() % 2 instead of column % 2 would (5 total, odd) wrongly emit an
// extra blank line after the already-complete final pair. Asserts actual rendered LINE structure,
// not just substring presence.
TEST_F(RosterOrderTest, ColumnPairingResetsAtEachSectionBoundary)
{
    account::AccountData account_data = make_account();
    account_data.characters = { "amy", "bob", "cara", "dan", "eve" };

    roster_cache::set_backing_reader_for_testing(
        [](const std::string&, const std::string&, const std::string& character_name,
            char_file_u* stored_character, std::string* error_message) -> bool {
            *stored_character = char_file_u {};
            if (character_name == "dan" || character_name == "eve")
                stored_character->race = RACE_ORC; // dark
            else
                stored_character->race = RACE_HUMAN; // light
            if (error_message)
                *error_message = "";
            return true;
        });
    roster_cache::clear();

    const std::string prompt = account::format_account_character_prompt(
        ".", account_data, account::RosterSort::Side, account::RosterFilter::None);
    const std::vector<std::string> lines = split_on_terminator(prompt);

    const auto lights_it = std::find(lines.begin(), lines.end(), "-- Good --");
    ASSERT_NE(lights_it, lines.end()) << prompt;
    const size_t lights = static_cast<size_t>(lights_it - lines.begin());
    ASSERT_LE(lights + 7, lines.size() - 1) << "prompt is shorter than expected:\n"
                                            << prompt;

    // Lights section (3 rows, odd): row 1 starts a fresh line right after the header, paired with
    // row 2; row 3 (the odd one out) is alone on its own line, itself properly terminated (not left
    // dangling into the next header).
    EXPECT_NE(lines[lights + 1].find("1) ["), std::string::npos) << prompt;
    EXPECT_NE(lines[lights + 1].find("2) ["), std::string::npos) << prompt;
    EXPECT_EQ(lines[lights + 1].find("3) ["), std::string::npos)
        << "row 3 leaked onto the same line as rows 1-2:\n"
        << prompt;

    EXPECT_NE(lines[lights + 2].find("3) ["), std::string::npos) << prompt;
    EXPECT_EQ(lines[lights + 2].find("4) ["), std::string::npos)
        << "row 4 (Darks) leaked onto the odd Lights trailer's line -- the column counter was not "
           "reset at the section boundary:\n"
        << prompt;

    // Exactly one blank line separates the odd trailer from the next header.
    EXPECT_EQ(lines[lights + 3], "") << prompt;
    EXPECT_EQ(lines[lights + 4], "-- Evil --") << prompt;

    // Darks section (2 rows, even): row 4 starts a fresh line right after its own header, paired
    // with row 5 -- proving the reset actually put column back to 0 rather than merely happening to
    // look right after an odd trailer.
    EXPECT_NE(lines[lights + 5].find("4) ["), std::string::npos) << prompt;
    EXPECT_NE(lines[lights + 5].find("5) ["), std::string::npos) << prompt;

    // The final section ends on a complete (even) pair, so exactly ONE blank line separates it from
    // the "N characters displayed." footer -- not two. A final-flush check keyed off the total row
    // count (5, odd) rather than the actual trailing column parity (2, even) would insert a spurious
    // extra blank line here.
    EXPECT_EQ(lines[lights + 6], "") << prompt;
    EXPECT_NE(lines[lights + 7], "") << "an extra blank line was emitted after the final pair:\n"
                                     << prompt;
    EXPECT_NE(lines[lights + 7].find("characters displayed."), std::string::npos) << prompt;
}

TEST_F(RosterOrderTest, SideSortOrdersGodsLightsDarksThenThirdSide)
{
    account::AccountData account_data = make_account();
    account_data.characters = { "magus1", "orc1", "human1" };

    roster_cache::set_backing_reader_for_testing(
        [](const std::string&, const std::string&, const std::string& character_name,
            char_file_u* stored_character, std::string* error_message) -> bool {
            *stored_character = char_file_u {};
            if (character_name == "magus1")
                stored_character->race = RACE_MAGUS;
            else if (character_name == "orc1")
                stored_character->race = RACE_ORC;
            else
                stored_character->race = RACE_HUMAN;
            if (error_message)
                *error_message = "";
            return true;
        });
    roster_cache::clear();

    EXPECT_EQ(names_in_order(account_data, account::RosterSort::Side, account::RosterFilter::None),
        (std::vector<std::string> { "human1", "orc1", "magus1" }));
}

TEST_F(RosterOrderTest, TiedHighestCoefficientAppearsUnderEveryTiedFilter)
{
    account::AccountData account_data = make_account();
    account_data.characters = { "twinned" };

    roster_cache::set_backing_reader_for_testing(
        [](const std::string&, const std::string&, const std::string&,
            char_file_u* stored_character, std::string* error_message) -> bool {
            *stored_character = char_file_u {};
            stored_character->race = RACE_HUMAN;
            stored_character->profs.prof_coof[PROF_WARRIOR] = 150;
            stored_character->profs.prof_coof[PROF_MAGE] = 150;
            if (error_message)
                *error_message = "";
            return true;
        });
    roster_cache::clear();

    EXPECT_EQ(names_in_order(account_data, account::RosterSort::Account, account::RosterFilter::Warrior).size(), 1u);
    EXPECT_EQ(names_in_order(account_data, account::RosterSort::Account, account::RosterFilter::Mage).size(), 1u);
    EXPECT_TRUE(names_in_order(account_data, account::RosterSort::Account, account::RosterFilter::Ranger).empty());
}

TEST_F(RosterOrderTest, UnreadableCharactersSortLastAndAreExcludedByFilters)
{
    account::AccountData account_data = make_sortable_account();
    account_data.characters.insert(account_data.characters.begin(), "brokenchar");

    // sortable_reader returns false for "brokenchar".
    const std::vector<std::string> by_level = names_in_order(account_data, account::RosterSort::Level, account::RosterFilter::None);
    ASSERT_EQ(by_level.size(), 5u);
    EXPECT_EQ(by_level.back(), "brokenchar");

    const std::vector<std::string> warriors = names_in_order(account_data, account::RosterSort::Account, account::RosterFilter::Warrior);
    EXPECT_EQ(warriors, (std::vector<std::string> { "gimli" }));
}

// An unreadable summary defaults race to 0, which is also RACE_GOD. If side_rank_for_summary ever
// dropped its "readable" check, an unreadable character would render under a SECOND "-- Gods --"
// header instead of "-- Unavailable --", and nothing above would fail: readable_god still renders
// under the first (and only expected) "-- Gods --" header regardless. This asserts the header
// count directly, plus which section each character actually renders under.
TEST_F(RosterOrderTest, UnreadableCharacterRendersUnderUnavailableNotASecondGodsSection)
{
    account::AccountData account_data = make_account();
    account_data.characters = { "godone", "brokenchar" };

    roster_cache::set_backing_reader_for_testing(
        [](const std::string&, const std::string&, const std::string& character_name,
            char_file_u* stored_character, std::string* error_message) -> bool {
            if (character_name == "brokenchar") {
                if (error_message)
                    *error_message = "unreadable";
                return false;
            }
            *stored_character = char_file_u {};
            stored_character->race = RACE_GOD;
            if (error_message)
                *error_message = "";
            return true;
        });
    roster_cache::clear();

    const std::string prompt = account::format_account_character_prompt(
        ".", account_data, account::RosterSort::Side, account::RosterFilter::None);

    ASSERT_NE(prompt.find("-- Unavailable --"), std::string::npos) << prompt;

    const size_t first_gods = prompt.find("-- Gods --");
    ASSERT_NE(first_gods, std::string::npos) << prompt;
    EXPECT_EQ(prompt.find("-- Gods --", first_gods + 1), std::string::npos)
        << "\"-- Gods --\" rendered more than once -- the unreadable character likely fell into "
           "its own Gods section instead of Unavailable:\n"
        << prompt;

    const std::vector<std::pair<int, std::string>> rows = parse_rendered_roster_rows(prompt);
    ASSERT_EQ(rows.size(), 2u);
    EXPECT_EQ(rows[0].second, "Godone") << "readable god-race character must render first, under Gods:\n"
                                        << prompt;
    EXPECT_EQ(rows[1].second, "Brokenchar")
        << "unreadable character must render last, under Unavailable:\n"
        << prompt;

    const size_t unavailable = prompt.find("-- Unavailable --");
    const size_t godone_row = prompt.find("Godone");
    const size_t brokenchar_row = prompt.find("Brokenchar");
    ASSERT_NE(godone_row, std::string::npos);
    ASSERT_NE(brokenchar_row, std::string::npos);
    EXPECT_LT(godone_row, unavailable) << "Godone must render before the Unavailable section:\n"
                                       << prompt;
    EXPECT_GT(brokenchar_row, unavailable) << "Brokenchar must render after the Unavailable header:\n"
                                           << prompt;
}

// Names are character1..character250. Lexicographically "character250" < "character3" (the digit
// '2' loses to '3' at the first differing position), so a cap-AFTER-sort implementation keeps
// character250 in its 200-entry result while a cap-BEFORE-sort implementation (insertion prefix
// character1..character200) never even considers it. Comparing against the fully-sorted-then-
// truncated expectation catches that difference; asserting size()==200 alone would not.
TEST_F(RosterOrderTest, OrderingIsCappedAtTheDisplayedRosterLimitAfterSorting)
{
    account::AccountData account_data = make_account();
    account_data.characters.clear();
    for (int index = 1; index <= 250; ++index)
        account_data.characters.push_back("character" + std::to_string(index));

    roster_cache::set_backing_reader_for_testing(
        [](const std::string&, const std::string&, const std::string&,
            char_file_u* stored_character, std::string* error_message) -> bool {
            *stored_character = char_file_u {};
            stored_character->level = 1;
            stored_character->race = RACE_HUMAN;
            if (error_message)
                *error_message = "";
            return true;
        });
    roster_cache::clear();

    const std::vector<size_t> indices = account::ordered_roster_indices(
        ".", account_data, account::RosterSort::Name, account::RosterFilter::None);
    ASSERT_EQ(indices.size(), 200u);

    std::vector<std::string> actual;
    for (size_t index : indices)
        actual.push_back(account_data.characters[index]);

    std::vector<std::string> expected_full_order = account_data.characters;
    std::sort(expected_full_order.begin(), expected_full_order.end());
    const std::vector<std::string> expected(expected_full_order.begin(), expected_full_order.begin() + 200);

    EXPECT_EQ(actual, expected);
    EXPECT_NE(std::find(actual.begin(), actual.end(), "character250"), actual.end())
        << "a cap-before-sort implementation would never surface character250";
}

// 250 characters, of which exactly 210 match the Warrior filter (the rest are mages). The cap must
// apply to the 210 post-filter matches, not to the pre-filter 250 -- so the result is 200, not 210
// truncated from a smaller filtered set that happened to already fit, and not a filter applied
// after an incorrect pre-filter cap.
TEST_F(RosterOrderTest, OrderingIsCappedAfterFiltering)
{
    account::AccountData account_data = make_account();
    account_data.characters.clear();
    for (int index = 1; index <= 250; ++index)
        account_data.characters.push_back("character" + std::to_string(index));

    roster_cache::set_backing_reader_for_testing(
        [](const std::string&, const std::string&, const std::string& character_name,
            char_file_u* stored_character, std::string* error_message) -> bool {
            *stored_character = char_file_u {};
            stored_character->race = RACE_HUMAN;
            const int suffix = std::stoi(character_name.substr(std::string("character").size()));
            if (suffix <= 210)
                stored_character->profs.prof_coof[PROF_WARRIOR] = 100;
            else
                stored_character->profs.prof_coof[PROF_MAGE] = 100;
            if (error_message)
                *error_message = "";
            return true;
        });
    roster_cache::clear();

    EXPECT_EQ(account::ordered_roster_indices(".", account_data,
                  account::RosterSort::Account, account::RosterFilter::Warrior)
                  .size(),
        200u);
}

// The PR #289 invariant, generalised: whatever row N shows must be what typing N selects, under
// every sort and every filter. This is the single most important test in the feature. Renders the
// ACTUAL prompt (format_account_character_prompt) and parses the rows out of the rendered text,
// rather than comparing select_linked_character against ordered_roster_indices directly -- the
// latter would only prove the selector agrees with its own ordering helper, not with what the
// player is actually shown, and could not catch a renderer that diverged (a second cap, a skipped
// unreadable row, 0-based numbering, ...).
TEST_F(RosterOrderTest, SelectionByNumberMatchesTheRenderedRowUnderEverySortAndFilter)
{
    const account::AccountData account_data = make_sortable_account();

    const account::RosterSort sorts[] = { account::RosterSort::Account, account::RosterSort::Name,
        account::RosterSort::Level, account::RosterSort::Race, account::RosterSort::Side };
    const account::RosterFilter filters[] = { account::RosterFilter::None,
        account::RosterFilter::Warrior, account::RosterFilter::Ranger, account::RosterFilter::Mage };

    for (account::RosterSort sort : sorts) {
        for (account::RosterFilter filter : filters) {
            const std::string prompt = account::format_account_character_prompt(".", account_data, sort, filter);
            const std::vector<std::pair<int, std::string>> rows = parse_rendered_roster_rows(prompt);

            for (size_t index = 0; index < rows.size(); ++index) {
                ASSERT_EQ(rows[index].first, static_cast<int>(index + 1))
                    << "rendered roster rows are not numbered sequentially from 1";
            }

            for (size_t index = 0; index < rows.size(); ++index) {
                const size_t row = index + 1;
                std::string selected;
                std::string error_message;
                ASSERT_TRUE(account::select_linked_character(".", account_data,
                    std::to_string(row), sort, filter, &selected, &error_message))
                    << "row " << row << ": " << error_message;
                // Both sides normalised: the renderer prints the display form ("Aragorn"),
                // select_linked_character returns normalize_account_name's form ("aragorn").
                EXPECT_EQ(selected, account::normalize_account_name(rows[index].second))
                    << "row " << row << " renders " << rows[index].second
                    << " but selects " << selected;
            }

            std::string selected;
            std::string error_message;
            EXPECT_FALSE(account::select_linked_character(".", account_data,
                std::to_string(rows.size() + 1), sort, filter, &selected, &error_message))
                << "a row past the end of the rendered roster was selectable";
        }
    }
}

// The concrete scenario from the review that caught this: 250 linked characters (well over the
// 200-entry display cap) under a non-Account sort, with one character inserted past the cap
// boundary (insertion index 210) whose name sorts to the very front. ordered_roster_indices sorts
// the FULL list and only then truncates to 200, so this character renders at row 1 -- but a name
// scan bounded by insertion order ([0, 200) of account.characters as stored) would never reach
// insertion index 210 and would reject the player's own character as "no such character". Both the
// row-number path and the name path must find it.
TEST_F(RosterOrderTest, SelectionByNameFindsACharacterPastTheInsertionOrderCapUnderANonAccountSort)
{
    account::AccountData account_data = make_account();
    account_data.characters.clear();
    for (int index = 0; index < 210; ++index)
        account_data.characters.push_back("zzfiller" + std::to_string(index));
    account_data.characters.push_back("aaearly"); // insertion index 210: past a [0, 200) insertion-order scan
    for (int index = 210; index < 249; ++index)
        account_data.characters.push_back("zzfiller" + std::to_string(index));
    ASSERT_EQ(account_data.characters.size(), 250u);

    roster_cache::set_backing_reader_for_testing(
        [](const std::string&, const std::string&, const std::string&,
            char_file_u* stored_character, std::string* error_message) -> bool {
            *stored_character = char_file_u {};
            stored_character->level = 1;
            stored_character->race = RACE_HUMAN;
            if (error_message)
                *error_message = "";
            return true;
        });
    roster_cache::clear();

    const std::vector<std::string> displayed = names_in_order(account_data, account::RosterSort::Name, account::RosterFilter::None);
    ASSERT_EQ(displayed.size(), 200u);
    ASSERT_EQ(displayed.front(), "aaearly") << "test setup: expected the out-of-order character to sort first";

    std::string selected_by_number;
    std::string error_message;
    ASSERT_TRUE(account::select_linked_character(".", account_data, "1",
        account::RosterSort::Name, account::RosterFilter::None, &selected_by_number, &error_message))
        << error_message;
    EXPECT_EQ(selected_by_number, "aaearly");

    std::string selected_by_name;
    ASSERT_TRUE(account::select_linked_character(".", account_data, "aaearly",
        account::RosterSort::Name, account::RosterFilter::None, &selected_by_name, &error_message))
        << error_message;
    EXPECT_EQ(selected_by_name, "aaearly");
}

TEST_F(RosterOrderTest, SelectionByNameWorksForACharacterHiddenByTheActiveFilter)
{
    const account::AccountData account_data = make_sortable_account();

    std::string selected;
    std::string error_message;
    // "aragorn" is a Ranger, so the Warrior filter hides them; selecting by name must still work,
    // because a filter must never make one of a player's characters unreachable.
    ASSERT_TRUE(account::select_linked_character(".", account_data, "aragorn",
        account::RosterSort::Account, account::RosterFilter::Warrior, &selected, &error_message))
        << error_message;
    EXPECT_EQ(selected, "aragorn");
}

// roster_sort_to_string / roster_sort_from_string are named deliverables that Tasks 4 and 5 both
// build on (a persisted preference and, presumably, a "sort" subcommand). No roster_cache
// interaction here, so these do not need the RosterOrderTest fixture.

TEST(RosterSortStringConversion, RoundTripsAllEnumValues)
{
    const account::RosterSort sorts[] = {
        account::RosterSort::Account,
        account::RosterSort::Name,
        account::RosterSort::Level,
        account::RosterSort::Race,
        account::RosterSort::Side,
    };
    for (account::RosterSort sort : sorts) {
        account::RosterSort parsed = account::RosterSort::Level; // sentinel, must be overwritten
        ASSERT_TRUE(account::roster_sort_from_string(account::roster_sort_to_string(sort), &parsed));
        EXPECT_EQ(parsed, sort);
    }
}

// Pinned explicitly, not just described: "never chose" (empty string) and "chose Account" persist
// as the same string, so a stored preference round-trips through account::RosterSort::Account.
TEST(RosterSortStringConversion, AccountSortRoundTripsThroughEmptyString)
{
    EXPECT_STREQ(account::roster_sort_to_string(account::RosterSort::Account), "");

    account::RosterSort parsed = account::RosterSort::Level;
    ASSERT_TRUE(account::roster_sort_from_string("", &parsed));
    EXPECT_EQ(parsed, account::RosterSort::Account);
}

TEST(RosterSortStringConversion, GarbageStringIsRejected)
{
    account::RosterSort parsed = account::RosterSort::Level;
    EXPECT_FALSE(account::roster_sort_from_string("not-a-real-sort", &parsed));
}

TEST(RosterSortStringConversion, NullOutputPointerIsRejected)
{
    EXPECT_FALSE(account::roster_sort_from_string("name", nullptr));
}

TEST(AccountManagement, RosterSortRoundTripsThroughAccountJson)
{
    account::AccountData account_data = make_account();
    account_data.roster_sort = "level";

    const std::string json = account::serialize_account_to_json(account_data);
    EXPECT_NE(json.find("\"roster_sort\": \"level\""), std::string::npos) << json;

    account::AccountData parsed_account;
    std::string error_message;
    ASSERT_TRUE(account::deserialize_account_from_json(json, &parsed_account, &error_message)) << error_message;
    EXPECT_EQ(parsed_account.roster_sort, "level");
}

// Existing account files predate this field. They must load and fall back to insertion order,
// which is why no schema-version bump or migration is needed.
TEST(AccountManagement, AccountJsonWithoutRosterSortLoadsWithInsertionOrder)
{
    account::AccountData account_data = make_account();
    std::string json = account::serialize_account_to_json(account_data);

    const size_t field_start = json.find("  \"roster_sort\"");
    ASSERT_NE(field_start, std::string::npos);
    const size_t field_end = json.find('\n', field_start);
    json.erase(field_start, field_end - field_start + 1);

    account::AccountData parsed_account;
    std::string error_message;
    ASSERT_TRUE(account::deserialize_account_from_json(json, &parsed_account, &error_message)) << error_message;
    EXPECT_TRUE(parsed_account.roster_sort.empty());

    account::RosterSort sort = account::RosterSort::Name;
    ASSERT_TRUE(account::roster_sort_from_string(parsed_account.roster_sort, &sort));
    EXPECT_EQ(sort, account::RosterSort::Account);
}

TEST(AccountManagement, WriteAccountFileIndexesTheRecord)
{
    account_index::clear();

    TemporaryDirectory temp_directory;
    // write_account_file only indexes a record written against the index's own root -- the write
    // side of the same matches_root() guard the resolvers apply. Without declaring the root, the
    // upsert is (correctly) skipped and the assertions below would be pinning the gap instead of the
    // behaviour. account_index::clear() puts the root back to "." for whatever runs next.
    account_index::set_root_directory(temp_directory.path());
    account::AccountData account_data;
    std::string error_message;
    ASSERT_TRUE(account::initialize_new_account("indexed", "indexed@example.com", "Password123", 1000, &account_data, &error_message)) << error_message;
    ASSERT_TRUE(account::write_account_file(temp_directory.path(), account_data, &error_message)) << error_message;

    std::string path;
    EXPECT_TRUE(account_index::find_path_by_email("indexed@example.com", &path, nullptr));
    EXPECT_TRUE(account_index::find_path_by_account_name("indexed", &path, nullptr));

    account_index::clear();
}

TEST(AccountManagement, WriteAccountFileIndexesNewlyLinkedCharacters)
{
    account_index::clear();

    TemporaryDirectory temp_directory;
    account_index::set_root_directory(temp_directory.path());
    account::AccountData account_data;
    std::string error_message;
    ASSERT_TRUE(account::initialize_new_account("linker", "linker@example.com", "Password123", 1000, &account_data, &error_message)) << error_message;
    ASSERT_TRUE(account::add_character_to_account(&account_data, "Frodo", &error_message)) << error_message;
    ASSERT_TRUE(account::write_account_file(temp_directory.path(), account_data, &error_message)) << error_message;

    std::string owner_email;
    ASSERT_TRUE(account_index::find_owner_email_by_character("Frodo", &owner_email, nullptr));
    EXPECT_EQ(owner_email, "linker@example.com");

    account_index::clear();
}

TEST(AccountManagement, AWriteRejectedBeforeAnyFileWorkLeavesTheIndexAlone)
{
    account_index::clear();

    TemporaryDirectory temp_directory;
    account_index::set_root_directory(temp_directory.path());
    account::AccountData account_data;
    std::string error_message;
    ASSERT_TRUE(account::initialize_new_account("failer", "failer@example.com", "Password123", 1000, &account_data, &error_message)) << error_message;
    account_data.account_name = ""; // rejected by validate_identifier_for_path before any file work

    EXPECT_FALSE(account::write_account_file(temp_directory.path(), account_data, &error_message));

    std::string path;
    EXPECT_FALSE(account_index::find_path_by_email("failer@example.com", &path, nullptr))
        << "a write that never landed must never appear in the index";

    account_index::clear();
}

TEST(AccountManagement, AWriteThatFailsAfterReachingDiskLeavesTheIndexAlone)
{
    // The companion to the test above, and the one that exercises the ordering that matters: this
    // write creates the account/bucket directories and opens its temp file before failing on the
    // occupied-target check, so an upsert placed anywhere but after the rename would already have
    // fired. The early-validation case never reaches file work at all.
    account_index::clear();

    TemporaryDirectory temp_directory;
    account_index::set_root_directory(temp_directory.path());

    account::AccountData occupier;
    std::string error_message;
    ASSERT_TRUE(account::initialize_new_account("occupier", "shared@example.com", "Password123", 1000, &occupier, &error_message)) << error_message;
    ASSERT_TRUE(account::write_account_file(temp_directory.path(), occupier, &error_message)) << error_message;

    account::AccountData intruder;
    ASSERT_TRUE(account::initialize_new_account("intruder", "shared@example.com", "Password123", 1000, &intruder, &error_message)) << error_message;
    EXPECT_FALSE(account::write_account_file(temp_directory.path(), intruder, &error_message))
        << "the target path is already held by a different account";
    EXPECT_EQ(error_message, "Account storage path is already occupied by a different account.");

    std::string path;
    EXPECT_FALSE(account_index::find_path_by_account_name("intruder", &path, nullptr))
        << "a write that never landed must never appear in the index";
    EXPECT_TRUE(account_index::find_path_by_account_name("occupier", &path, nullptr))
        << "and it must not disturb the record that is really there";

    account_index::clear();
}

// --- Wave 3, item 2: a save must never land in the invalid-account sentinel ----------------------
//
// account_character_directory() (and account_record_directory(), its record-in-hand twin) fall back
// to accounts/__invalid_account__ when an account's storage key will not resolve. Nothing
// enumerates that directory: the index never sees it, the boot walk never sees it, and the next
// cleanup sweeps it away. A character file written there is silently lost -- the log reports
// success, the real file goes stale, and save_char repoints player_table at the doomed path so
// every later save follows it.

// Breaks the account's storage key on disk without touching anything else: the record still parses
// and is still found by name, but it discloses no email, so every account_character_* path for it
// collapses onto the sentinel. This is the reachable shape (a legacy record with no usable address);
// write_account_file will not produce it, which is why it is written by hand here.
std::string blank_out_stored_email(const std::string& account_path)
{
    std::string account_json = read_file_contents(account_path);
    const std::string original = "\"normalized_email\": \"player@example.com\"";
    EXPECT_NE(account_json.find(original), std::string::npos);
    account_json.replace(account_json.find(original), original.size(), "\"normalized_email\": \"\"");
    write_text_file(account_path, account_json);
    return account_json;
}

TEST(AccountManagement, RefusesToWriteACharacterFileIntoTheInvalidAccountSentinel)
{
    TemporaryDirectory temp_directory;
    const std::string root = temp_directory.path();
    std::string error_message;

    ASSERT_TRUE(account::create_account(root, "alpha-admin", "player@example.com", "ValidPass1", 1700007776, nullptr, &error_message)) << error_message;
    ASSERT_TRUE(account::admin_link_character(root, "alpha-admin", "aragorn", 1700007777, nullptr, &error_message)) << error_message;
    blank_out_stored_email(root + "/accounts/P-T/player@example.com/account.json");

    const std::string sentinel = root + "/accounts/__invalid_account__";
    ASSERT_TRUE(account::is_invalid_account_storage_directory(
        account::account_character_directory(root, "alpha-admin", "aragorn")))
        << "the fixture is meant to make this account's storage key unresolvable";

    char_file_u stored_character = make_stored_character("aragorn");
    EXPECT_FALSE(account::write_account_character_file(root, "alpha-admin", stored_character, &error_message))
        << "writing nothing is better than writing somewhere that will be swept away";
    EXPECT_NE(error_message.find("no resolvable storage key"), std::string::npos) << error_message;

    // The assertion that would still have passed under the bug if this only checked the return
    // value: the file must not be on disk at all. Reverted, write_account_character_file returns
    // true and this directory holds aragorn.character.json.
    struct stat sentinel_info { };
    EXPECT_NE(stat(sentinel.c_str(), &sentinel_info), 0)
        << "the invalid-account sentinel directory must never be created";
}

TEST(AccountManagement, RefusesToWriteObjectAndExploitFilesIntoTheInvalidAccountSentinel)
{
    // The other write paths that compose a destination from the same unresolvable key. Checked
    // because "check whether the sentinel can be reached from any other write path" is the half of
    // this fix that a single-site patch would have missed.
    TemporaryDirectory temp_directory;
    const std::string root = temp_directory.path();
    std::string error_message;

    ASSERT_TRUE(account::create_account(root, "alpha-admin", "player@example.com", "ValidPass1", 1700007776, nullptr, &error_message)) << error_message;
    ASSERT_TRUE(account::admin_link_character(root, "alpha-admin", "aragorn", 1700007777, nullptr, &error_message)) << error_message;
    blank_out_stored_email(root + "/accounts/P-T/player@example.com/account.json");

    EXPECT_FALSE(account::write_default_account_object_file(root, "alpha-admin", "aragorn", &error_message));
    EXPECT_NE(error_message.find("no resolvable storage key"), std::string::npos) << error_message;

    error_message.clear();
    EXPECT_FALSE(account::write_default_account_exploit_file(root, "alpha-admin", "aragorn", &error_message));
    EXPECT_NE(error_message.find("no resolvable storage key"), std::string::npos) << error_message;

    struct stat sentinel_info { };
    EXPECT_NE(stat((root + "/accounts/__invalid_account__").c_str(), &sentinel_info), 0);
}

TEST(AccountManagement, IdentifiesTheInvalidAccountSentinelWhereverItAppears)
{
    EXPECT_TRUE(account::is_invalid_account_storage_directory("./accounts/__invalid_account__"));
    EXPECT_TRUE(account::is_invalid_account_storage_directory("/srv/rots/lib/accounts/__invalid_account__"));
    EXPECT_TRUE(account::is_invalid_account_storage_directory("__invalid_account__"));
    // Not the sentinel: a real account directory, and a name that merely contains the sentinel.
    EXPECT_FALSE(account::is_invalid_account_storage_directory("./accounts/P-T/player@example.com"));
    EXPECT_FALSE(account::is_invalid_account_storage_directory("./accounts/__invalid_account__x"));
}

// --- Wave 3, item 4: a refused write must not leak a descriptor or a temp file -------------------

// Counts this process's open descriptors. write_account_file runs on every successful login
// (clear_account_login_failures), so a per-call leak walks a process that runs for weeks towards
// EMFILE -- at which point the server can neither accept connections nor write player files.
std::size_t count_open_descriptors()
{
    DIR* descriptors = opendir("/proc/self/fd");
    EXPECT_NE(descriptors, nullptr) << "Expected /proc/self/fd for the descriptor-leak check.";
    if (descriptors == nullptr)
        return 0;

    std::size_t open_count = 0;
    while (dirent* entry = readdir(descriptors)) {
        if (std::strcmp(entry->d_name, ".") == 0 || std::strcmp(entry->d_name, "..") == 0)
            continue;
        ++open_count;
    }
    closedir(descriptors);
    return open_count;
}

TEST(AccountManagement, ARefusedAccountWriteLeaksNoDescriptorAndLeavesNoTempFile)
{
    TemporaryDirectory temp_directory;
    const std::string root = temp_directory.path();
    std::string error_message;

    account::AccountData account_data;
    ASSERT_TRUE(account::create_account(root, "alpha-admin", "player@example.com", "ValidPass1", 1700007776, &account_data, &error_message)) << error_message;

    // The record at the destination path no longer parses, which is the "Existing account file
    // could not be read safely." refusal -- one of the two early returns that used to happen after
    // the temp file was already open.
    const std::string final_path = root + "/accounts/P-T/player@example.com/account.json";
    write_text_file(final_path, "{ this is not an account record");

    ASSERT_FALSE(account::write_account_file(root, account_data, &error_message));
    EXPECT_EQ(error_message, "Existing account file could not be read safely.");

    // Reverted, this file is sitting there after every refusal.
    struct stat temp_info { };
    EXPECT_NE(stat((final_path + ".tmp").c_str(), &temp_info), 0)
        << "a refused write must not leave a temporary file behind";

    // And the descriptor itself. One refusal is invisible; the login path repeats it.
    const std::size_t before = count_open_descriptors();
    for (int attempt = 0; attempt < 40; ++attempt)
        EXPECT_FALSE(account::write_account_file(root, account_data, nullptr));
    EXPECT_EQ(count_open_descriptors(), before)
        << "write_account_file leaked a descriptor per refused write";
}

TEST(AccountManagement, ARefusedWriteOverAnotherAccountsPathLeaksNoDescriptorEither)
{
    // The second early return: the destination parses fine but belongs to a different account.
    TemporaryDirectory temp_directory;
    const std::string root = temp_directory.path();
    std::string error_message;

    account::AccountData occupant;
    ASSERT_TRUE(account::create_account(root, "occupant", "player@example.com", "ValidPass1", 1700007776, &occupant, &error_message)) << error_message;

    account::AccountData intruder = occupant;
    intruder.account_name = "intruder";

    const std::string final_path = root + "/accounts/P-T/player@example.com/account.json";
    ASSERT_FALSE(account::write_account_file(root, intruder, &error_message));
    EXPECT_EQ(error_message, "Account storage path is already occupied by a different account.");

    struct stat temp_info { };
    EXPECT_NE(stat((final_path + ".tmp").c_str(), &temp_info), 0);

    const std::size_t before = count_open_descriptors();
    for (int attempt = 0; attempt < 40; ++attempt)
        EXPECT_FALSE(account::write_account_file(root, intruder, nullptr));
    EXPECT_EQ(count_open_descriptors(), before);
}

namespace {
bool path_exists_for_test(const std::string& path)
{
    struct stat info { };
    return stat(path.c_str(), &info) == 0;
}
} // namespace

// --- Conversion loss guard -----------------------------------------------------------------------
//
// A legacy character converts to JSON exactly once and the legacy file is deleted immediately after.
// If the conversion loses something, the original is already gone. These pin the guard that reads
// the written file back and refuses before anything is retired.

TEST(AccountManagement, RefusesAConversionWhoseWrittenFileCannotBeReadBack)
{
    // Not a contrived fault: skills are keyed by NAME in the character JSON and the skill table has
    // duplicate names -- indices 125 and 126 are both "trash" (consts.cpp:587-588). A character with
    // a value in both serializes to duplicate keys and will not parse back. Before the guard, the
    // conversion "succeeded", the legacy file was deleted, and the character was left unloadable.
    TemporaryDirectory temp_directory;
    ASSERT_EQ(mkdir((temp_directory.path() + "/players").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/players/A-E").c_str(), 0700), 0);

    char_file_u aragorn = make_stored_character("aragorn");
    aragorn.skills[125] = 40;
    aragorn.skills[126] = 60;
    ASSERT_FALSE(write_valid_legacy_player_file(temp_directory.path(), aragorn).empty());
    const std::string legacy_path = temp_directory.path() + "/players/A-E/aragorn";
    ASSERT_TRUE(path_exists_for_test(legacy_path)) << legacy_path;

    std::string error_message;
    ASSERT_TRUE(account::create_account(temp_directory.path(), "alpha-admin", "player@example.com",
        "ValidPass1", 1700012222, nullptr, &error_message)) << error_message;
    ASSERT_TRUE(account::admin_verify_email(temp_directory.path(), "alpha-admin", "VerifierAdmin",
        1700012222, nullptr, &error_message)) << error_message;

    account::AccountData linked_account;
    account::CharacterMigrationData migration;
    EXPECT_FALSE(account::link_and_migrate_character(temp_directory.path(), "alpha-admin",
        "ValidPass1", "aragorn", 1700012223, &linked_account, &migration, &error_message));
    EXPECT_NE(error_message.find("cannot be read back"), std::string::npos) << error_message;

    // The whole point of refusing before retirement: the character is exactly as it was.
    EXPECT_TRUE(path_exists_for_test(legacy_path)) << "the legacy file was retired despite the refusal";

    // Nothing half-converted is left in the account directory.
    EXPECT_FALSE(path_exists_for_test(temp_directory.path()
        + "/accounts/P-T/player@example.com/aragorn.character.json"));

    // And the account never claimed it, so the roster does not offer a character that cannot load.
    account::AccountData account_data;
    ASSERT_TRUE(account::read_account_file(temp_directory.path(), "alpha-admin", &account_data, &error_message))
        << error_message;
    EXPECT_FALSE(account::account_has_character(account_data, "aragorn"));
}

TEST(AccountManagement, StillConvertsACharacterThatSurvivesTheRoundTrip)
{
    // The guard must not refuse ordinary characters -- if it did, it would close the only route onto
    // the account system.
    TemporaryDirectory temp_directory;
    ASSERT_EQ(mkdir((temp_directory.path() + "/players").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/players/A-E").c_str(), 0700), 0);
    write_valid_legacy_player_file(temp_directory.path(), make_stored_character("aragorn"));

    std::string error_message;
    ASSERT_TRUE(account::create_account(temp_directory.path(), "alpha-admin", "player@example.com",
        "ValidPass1", 1700012222, nullptr, &error_message)) << error_message;
    ASSERT_TRUE(account::admin_verify_email(temp_directory.path(), "alpha-admin", "VerifierAdmin",
        1700012222, nullptr, &error_message)) << error_message;

    account::AccountData linked_account;
    account::CharacterMigrationData migration;
    ASSERT_TRUE(account::link_and_migrate_character(temp_directory.path(), "alpha-admin",
        "ValidPass1", "aragorn", 1700012223, &linked_account, &migration, &error_message))
        << error_message;
    EXPECT_TRUE(account::account_has_character(linked_account, "aragorn"));
}

TEST(AccountManagement, AFailedConversionNeverLeavesACharacterWithNoCopyAtAll)
{
    // The conversion deletes the legacy originals and then retires its own transitional
    // <name>.migration.json. If that last step fails, the failure path used to delete the
    // account-native files it had just written -- and the legacy files were already gone, so the
    // character existed nowhere. Whatever else a failed conversion does, it must never end with no
    // copy of the character on disk.
    TemporaryDirectory temp_directory;
    const std::string root = temp_directory.path();
    ASSERT_EQ(mkdir((root + "/players").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((root + "/players/A-E").c_str(), 0700), 0);
    write_valid_legacy_player_file(root, make_stored_character("aragorn"));

    std::string error_message;
    ASSERT_TRUE(account::create_account(root, "alpha-admin", "player@example.com",
        "ValidPass1", 1700012222, nullptr, &error_message))
        << error_message;
    ASSERT_TRUE(account::admin_verify_email(root, "alpha-admin", "VerifierAdmin",
        1700012222, nullptr, &error_message))
        << error_message;

    // A non-empty directory where the transitional file goes: std::remove() answers ENOTEMPTY, while
    // every sibling file in the same directory still deletes normally. An interrupted conversion or
    // a half-extracted backup leaves artifacts at exactly this path.
    const std::string snapshot_path = account::account_character_snapshot_path(root, "alpha-admin", "aragorn");
    ASSERT_EQ(mkdir(snapshot_path.c_str(), 0700), 0);
    write_text_file(snapshot_path + "/occupant", "not empty");

    const std::string legacy_player_path = account::legacy_player_file_path(root, "aragorn");
    const std::string account_character_path = account::account_character_player_path(root, "alpha-admin", "aragorn");
    ASSERT_EQ(access(legacy_player_path.c_str(), F_OK), 0) << "fixture must start with a legacy character on disk";

    account::AccountData linked_account;
    account::CharacterMigrationData migration;
    const bool converted = account::link_and_migrate_character(root, "alpha-admin",
        "ValidPass1", "aragorn", 1700012223, &linked_account, &migration, &error_message);

    const bool legacy_survives = access(legacy_player_path.c_str(), F_OK) == 0;
    const bool account_native_survives = access(account_character_path.c_str(), F_OK) == 0;
    ASSERT_TRUE(legacy_survives || account_native_survives)
        << "the conversion " << (converted ? "reported success" : "failed")
        << " and left no copy of the character: neither " << legacy_player_path
        << " nor " << account_character_path;

    // And the conversion is not merely survivable, it is done: the legacy originals were retired
    // before this step, so there is nothing left to abandon. A leftover transitional file is
    // reported and stepped over, not treated as a reason to undo work that already happened.
    EXPECT_TRUE(converted) << error_message;
    EXPECT_TRUE(account_native_survives) << account_character_path;
    EXPECT_FALSE(legacy_survives) << "the legacy original is retired by a conversion that succeeded";
    EXPECT_TRUE(account::account_has_character(linked_account, "aragorn"))
        << "a converted character must be linked to the account, not left orphaned beside it";
}

TEST(AccountManagement, RefusesRegistrationForAnAddressWhoseIndexedRecordWillNotRead)
{
    // The write-time occupancy check reads final_path, which is always the DIRECTORY layout. A legacy
    // flat record that parsed at boot and became unreadable afterwards is therefore never consulted:
    // find_account_by_email_internal fails its read, the address reads as free, and a second record
    // is written at the same email. The next boot sees two records disputing one address and refuses
    // it permanently -- the real player can no longer log in.
    TemporaryDirectory temp_directory;
    const std::string root = temp_directory.path();
    std::string error_message;

    // A legacy FLAT record is the whole story for this address: nothing sits at the directory path,
    // so the write-time guard has nothing to refuse on and only the index knows the address is taken.
    const std::string bucket = root + "/accounts/" + account::account_bucket_for_name("alpha-admin");
    ASSERT_EQ(mkdir((root + "/accounts").c_str(), 0700), 0);
    ASSERT_EQ(mkdir(bucket.c_str(), 0700), 0);
    const std::string flat_path = bucket + "/alpha-admin.json";
    write_text_file(flat_path, "{ no longer valid json");

    account::AccountData indexed_record;
    indexed_record.normalized_email = "player@example.com";
    indexed_record.account_name = "alpha-admin";

    account_index::clear();
    account_index::set_root_directory(root);
    account_index::set_enabled(true);
    account_index::upsert(indexed_record, flat_path, true);

    std::string indexed_path;
    ASSERT_TRUE(account_index::find_path_by_email("player@example.com", &indexed_path, nullptr))
        << "the address must be indexed for this to test anything";

    const bool created = account::create_account_for_email(root, "player@example.com", "ValidPass2", 1700007777, nullptr, &error_message);

    account_index::set_enabled(false);
    account_index::clear();

    EXPECT_FALSE(created)
        << "an address the index already holds must stay occupied even when its record will not read";
}

// --- admin_rename_linked_character -------------------------------------------------------------
//
// rename_char (db.cpp) predates account storage: it system("rm")s player_table[i].ch_file, which
// for an account-native character IS the account's <name>.character.json, and never rewrites
// account.json. The character's save is destroyed while the account keeps listing the old name,
// leaving a roster row that renders "[ ?? ???]" and cannot be played. These cover the account-layer
// half of the fix: move the three files and rewrite the account, atomically, or change nothing.

namespace {

// Writes the three account-owned files for a linked character, so a rename has something to move.
void write_linked_character_files(const std::string& root, const std::string& account_name,
    const std::string& character_name, const std::string& marker)
{
    const std::string character_path = account::account_character_player_path(root, account_name, character_name);
    const std::string object_path = account::account_character_object_path(root, account_name, character_name);
    const std::string exploits_path = account::account_character_exploits_path(root, account_name, character_name);
    for (const auto& entry : { std::make_pair(character_path, marker + "-character"),
             std::make_pair(object_path, marker + "-objects"),
             std::make_pair(exploits_path, marker + "-exploits") }) {
        FILE* file = std::fopen(entry.first.c_str(), "wb");
        ASSERT_NE(file, nullptr) << "could not create " << entry.first;
        std::fwrite(entry.second.data(), 1, entry.second.size(), file);
        std::fclose(file);
    }
}

bool path_is_present(const std::string& path)
{
    struct stat file_info { };
    return stat(path.c_str(), &file_info) == 0;
}

std::string read_whole(const std::string& path)
{
    FILE* file = std::fopen(path.c_str(), "rb");
    if (file == nullptr)
        return std::string();
    std::string out;
    char buffer[512];
    while (true) {
        const size_t n = std::fread(buffer, 1, sizeof(buffer), file);
        if (n > 0)
            out.append(buffer, n);
        if (n < sizeof(buffer))
            break;
    }
    std::fclose(file);
    return out;
}

} // namespace

TEST(AccountManagement, RenamingALinkedCharacterMovesItsFilesAndRewritesTheAccount)
{
    TemporaryDirectory temp_directory;
    account::AccountData account_data;
    std::string error_message;

    ASSERT_TRUE(account::create_account_for_email(temp_directory.path(), "player@example.com",
        "ValidPass1", 1700006000, &account_data, &error_message)) << error_message;
    ASSERT_TRUE(account::admin_link_character(temp_directory.path(), account_data.account_name,
        "oldname", 1700006001, &account_data, &error_message)) << error_message;
    write_linked_character_files(temp_directory.path(), account_data.account_name, "oldname", "payload");

    ASSERT_TRUE(account::admin_rename_linked_character(temp_directory.path(),
        account_data.account_name, "oldname", "newname", 1700006002, &account_data, &error_message))
        << error_message;

    const std::string old_character = account::account_character_player_path(temp_directory.path(), account_data.account_name, "oldname");
    const std::string new_character = account::account_character_player_path(temp_directory.path(), account_data.account_name, "newname");
    const std::string new_object = account::account_character_object_path(temp_directory.path(), account_data.account_name, "newname");
    const std::string new_exploits = account::account_character_exploits_path(temp_directory.path(), account_data.account_name, "newname");

    EXPECT_FALSE(path_is_present(old_character)) << "the old character file must not survive the rename";
    EXPECT_TRUE(path_is_present(new_character)) << "the character file must exist under the new name";
    EXPECT_TRUE(path_is_present(new_object));
    EXPECT_TRUE(path_is_present(new_exploits));
    // The CONTENT must move, not just the name -- this is the whole point.
    EXPECT_EQ(read_whole(new_character), "payload-character");
    EXPECT_EQ(read_whole(new_object), "payload-objects");
    EXPECT_EQ(read_whole(new_exploits), "payload-exploits");

    account::AccountData reread;
    ASSERT_TRUE(account::read_account_file(temp_directory.path(), account_data.account_name, &reread, &error_message)) << error_message;
    EXPECT_FALSE(account::account_has_character(reread, "oldname")) << "the account must stop listing the old name";
    EXPECT_TRUE(account::account_has_character(reread, "newname")) << "the account must list the new name";
}

namespace {

// A legacy flat record this account's write would retire, readable and this account's own (so the
// pre-commit guard passes it), sitting in a bucket that has lost its write bit -- so the retirement
// std::remove fails with EACCES AFTER account.json has been renamed into place. Returns the bucket
// so the caller can restore its mode.
std::string plant_unretirable_legacy_record(const std::string& root, const std::string& account_name,
    const std::string& email)
{
    const std::string legacy_bucket = root + "/accounts/" + account::account_bucket_for_name(account_name);
    mkdir(legacy_bucket.c_str(), 0700);
    const std::string account_json = root + "/accounts/" + account::account_bucket_for_name(email)
        + "/" + email + "/account.json";
    write_text_file(legacy_bucket + "/" + account_name + ".json", read_file_contents(account_json));
    EXPECT_EQ(chmod(legacy_bucket.c_str(), 0500), 0);
    return legacy_bucket;
}

} // namespace

TEST(AccountManagement, RenamingALinkedCharacterStandsWhenRetiringALegacyRecordFails)
{
    // The account write commits -- account.json is renamed into place, the cache is flushed and the
    // index re-derived -- and only then can a retirement std::remove fail. Reported as a failed
    // rename, the caller undid the file moves, so account.json and the index named the character
    // "newname" while its files sat at "oldname": the "[ ?? ???]" roster row this function exists
    // to prevent, handed to the immortal as an error. A stale legacy file left behind is litter.
    TemporaryDirectory temp_directory;
    account::AccountData account_data;
    std::string error_message;

    ASSERT_TRUE(account::create_account_for_email(temp_directory.path(), "player@example.com",
        "ValidPass1", 1700006300, &account_data, &error_message)) << error_message;
    ASSERT_TRUE(account::admin_link_character(temp_directory.path(), account_data.account_name,
        "oldname", 1700006301, &account_data, &error_message)) << error_message;
    write_linked_character_files(temp_directory.path(), account_data.account_name, "oldname", "payload");

    const std::string legacy_bucket = plant_unretirable_legacy_record(temp_directory.path(),
        account_data.account_name, "player@example.com");

    const bool renamed = account::admin_rename_linked_character(temp_directory.path(),
        account_data.account_name, "oldname", "newname", 1700006302, &account_data, &error_message);
    chmod(legacy_bucket.c_str(), 0700);

    EXPECT_TRUE(renamed) << error_message;

    const std::string new_character = account::account_character_player_path(temp_directory.path(), account_data.account_name, "newname");
    EXPECT_TRUE(path_is_present(new_character)) << "the files must stay where the committed account record says they are";
    EXPECT_EQ(read_whole(new_character), "payload-character");
    EXPECT_FALSE(path_is_present(account::account_character_player_path(temp_directory.path(), account_data.account_name, "oldname")));

    account::AccountData reread;
    ASSERT_TRUE(account::read_account_file(temp_directory.path(), account_data.account_name, &reread, &error_message)) << error_message;
    EXPECT_TRUE(account::account_has_character(reread, "newname"));
}

TEST(AccountManagement, DeletingALinkedCharacterStandsWhenRetiringALegacyRecordFails)
{
    // Same post-commit bailout on the deletion path, where the undo is worse: the character files
    // were restored while account.json no longer listed the character, and db.cpp -- reading the
    // false as "nothing happened" -- deleted the players/ZZZ archive it had just made for exactly
    // this recovery.
    TemporaryDirectory temp_directory;
    account::AccountData account_data;
    std::string error_message;

    ASSERT_TRUE(account::create_account_for_email(temp_directory.path(), "player@example.com",
        "ValidPass1", 1700006400, &account_data, &error_message)) << error_message;
    ASSERT_TRUE(account::admin_link_character(temp_directory.path(), account_data.account_name,
        "oldname", 1700006401, &account_data, &error_message)) << error_message;
    write_linked_character_files(temp_directory.path(), account_data.account_name, "oldname", "payload");

    const std::string legacy_bucket = plant_unretirable_legacy_record(temp_directory.path(),
        account_data.account_name, "player@example.com");

    const bool deleted = account::admin_delete_linked_character(temp_directory.path(),
        account_data.account_name, "oldname", 1700006402, &account_data, &error_message);
    chmod(legacy_bucket.c_str(), 0700);

    EXPECT_TRUE(deleted) << error_message;
    EXPECT_FALSE(path_is_present(account::account_character_player_path(temp_directory.path(), account_data.account_name, "oldname")))
        << "the character file must not be restored under a record that no longer lists it";

    account::AccountData reread;
    ASSERT_TRUE(account::read_account_file(temp_directory.path(), account_data.account_name, &reread, &error_message)) << error_message;
    EXPECT_FALSE(account::account_has_character(reread, "oldname"));
}

TEST(AccountManagement, DeletingALinkedCharacterStandsWhenAStagedFileCannotBeRemoved)
{
    // The staged copies are removed last, after the account write has committed. A staged path that
    // will not unlink -- here a stray directory left where the object file belongs -- was reported
    // as a failed deletion, and db.cpp then removed the players/ZZZ archive. The character is
    // deleted either way; the staged leftover is litter.
    TemporaryDirectory temp_directory;
    account::AccountData account_data;
    std::string error_message;

    ASSERT_TRUE(account::create_account_for_email(temp_directory.path(), "player@example.com",
        "ValidPass1", 1700006500, &account_data, &error_message)) << error_message;
    ASSERT_TRUE(account::admin_link_character(temp_directory.path(), account_data.account_name,
        "oldname", 1700006501, &account_data, &error_message)) << error_message;
    write_linked_character_files(temp_directory.path(), account_data.account_name, "oldname", "payload");

    const std::string object_path = account::account_character_object_path(temp_directory.path(), account_data.account_name, "oldname");
    ASSERT_EQ(std::remove(object_path.c_str()), 0);
    ASSERT_EQ(mkdir(object_path.c_str(), 0700), 0);
    write_text_file(object_path + "/stray", "stray");

    EXPECT_TRUE(account::admin_delete_linked_character(temp_directory.path(),
        account_data.account_name, "oldname", 1700006502, &account_data, &error_message))
        << error_message;
    EXPECT_FALSE(path_is_present(account::account_character_player_path(temp_directory.path(), account_data.account_name, "oldname")));

    account::AccountData reread;
    ASSERT_TRUE(account::read_account_file(temp_directory.path(), account_data.account_name, &reread, &error_message)) << error_message;
    EXPECT_FALSE(account::account_has_character(reread, "oldname"));
}

TEST(AccountManagement, RenamingALinkedCharacterOntoAnExistingNameChangesNothing)
{
    TemporaryDirectory temp_directory;
    account::AccountData account_data;
    std::string error_message;

    ASSERT_TRUE(account::create_account_for_email(temp_directory.path(), "player@example.com",
        "ValidPass1", 1700006100, &account_data, &error_message)) << error_message;
    ASSERT_TRUE(account::admin_link_character(temp_directory.path(), account_data.account_name,
        "oldname", 1700006101, &account_data, &error_message)) << error_message;
    ASSERT_TRUE(account::admin_link_character(temp_directory.path(), account_data.account_name,
        "taken", 1700006102, &account_data, &error_message)) << error_message;
    write_linked_character_files(temp_directory.path(), account_data.account_name, "oldname", "first");
    write_linked_character_files(temp_directory.path(), account_data.account_name, "taken", "second");

    EXPECT_FALSE(account::admin_rename_linked_character(temp_directory.path(),
        account_data.account_name, "oldname", "taken", 1700006103, &account_data, &error_message));

    // Neither character may be disturbed by the refusal.
    EXPECT_EQ(read_whole(account::account_character_player_path(temp_directory.path(), account_data.account_name, "oldname")), "first-character");
    EXPECT_EQ(read_whole(account::account_character_player_path(temp_directory.path(), account_data.account_name, "taken")), "second-character");
    account::AccountData reread;
    ASSERT_TRUE(account::read_account_file(temp_directory.path(), account_data.account_name, &reread, &error_message)) << error_message;
    EXPECT_TRUE(account::account_has_character(reread, "oldname"));
    EXPECT_TRUE(account::account_has_character(reread, "taken"));
}

TEST(AccountManagement, RenamingACharacterTheAccountDoesNotOwnIsRefused)
{
    TemporaryDirectory temp_directory;
    account::AccountData account_data;
    std::string error_message;

    ASSERT_TRUE(account::create_account_for_email(temp_directory.path(), "player@example.com",
        "ValidPass1", 1700006200, &account_data, &error_message)) << error_message;

    EXPECT_FALSE(account::admin_rename_linked_character(temp_directory.path(),
        account_data.account_name, "stranger", "newname", 1700006201, &account_data, &error_message));
    EXPECT_NE(error_message.find("not linked"), std::string::npos) << error_message;
}

TEST(AccountManagement, RefusesToRenameALinkedCharacterWhoseCharacterFileIsMissing)
{
    // The staged-move loop skipped any source that stat()'d ENOENT, uniformly -- including the
    // character file. That is the one condition meaning the rename cannot be carried out: all three
    // moves no-op, account.json and character_links are rewritten to the new name, the index is
    // upserted and player_table[].ch_file is repointed, and rename_char returns success. The account
    // then lists a character with no file behind it -- the "[ ?? ???]" roster row this function
    // exists to prevent. validate_account_owned_character_path does not cover it: it string-compares
    // the stored path against character_json_file_name() and stats nothing.
    TemporaryDirectory temp_directory;
    account::AccountData account_data;
    std::string error_message;

    ASSERT_TRUE(account::create_account_for_email(temp_directory.path(), "player@example.com",
        "ValidPass1", 1700006000, &account_data, &error_message)) << error_message;
    ASSERT_TRUE(account::admin_link_character(temp_directory.path(), account_data.account_name,
        "oldname", 1700006001, &account_data, &error_message)) << error_message;
    write_linked_character_files(temp_directory.path(), account_data.account_name, "oldname", "payload");

    const std::string old_character = account::account_character_player_path(temp_directory.path(), account_data.account_name, "oldname");
    ASSERT_EQ(std::remove(old_character.c_str()), 0);

    EXPECT_FALSE(account::admin_rename_linked_character(temp_directory.path(),
        account_data.account_name, "oldname", "newname", 1700006002, &account_data, &error_message))
        << "a rename with no character file to move must be refused, not reported as done";
    EXPECT_NE(error_message.find("is not there"), std::string::npos) << "reported as: " << error_message;

    account::AccountData stored_account;
    ASSERT_TRUE(account::read_account_file(temp_directory.path(), account_data.account_name, &stored_account, &error_message)) << error_message;
    EXPECT_TRUE(account::account_has_character(stored_account, "oldname"))
        << "the account must still claim the character by the name its files are under";
    EXPECT_FALSE(account::account_has_character(stored_account, "newname"))
        << "and must not have been repointed at a name with nothing behind it";

    // The object and exploits files must not have been left half-moved either.
    EXPECT_TRUE(path_is_present(account::account_character_object_path(temp_directory.path(), account_data.account_name, "oldname")));
    EXPECT_TRUE(path_is_present(account::account_character_exploits_path(temp_directory.path(), account_data.account_name, "oldname")));
}
