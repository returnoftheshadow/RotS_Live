#include "../account_management.h"
#include "../db.h"
#include "../exploits_json.h"
#include "../handler.h"
#include "../interpre.h"
#include "../objects_json.h"
#include "../structs.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

ACMD(do_account);
ACMD(do_whoacct);
extern struct player_index_element* player_table;
extern struct descriptor_data* descriptor_list;
extern int top_of_p_table;
void clear_char(struct char_data* ch, int mode);
void save_player(struct char_data* ch, int load_room, int index_pos);
void store_to_char(struct char_file_u* st, struct char_data* ch);

namespace {

char_file_u make_stored_character(const char* name, int level = 12, int race = 2)
{
    char_file_u stored_character {};
    std::snprintf(stored_character.name, sizeof(stored_character.name), "%s", name);
    stored_character.level = level;
    stored_character.race = race;
    stored_character.specials2.idnum = 1234;
    return stored_character;
}

class TemporaryDirectory {
public:
    TemporaryDirectory()
    {
        char directory_template[] = "/tmp/rots-act-wiz-tests-XXXXXX";
        char* created_path = mkdtemp(directory_template);
        EXPECT_NE(created_path, nullptr);
        if (created_path != nullptr)
            m_path = created_path;
    }

    ~TemporaryDirectory()
    {
        if (!m_path.empty()) {
            const std::string command = "rm -rf '" + m_path + "'";
            std::system(command.c_str());
        }
    }

    const std::string& path() const { return m_path; }

private:
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

class ScopedDescriptorList {
public:
    ScopedDescriptorList()
        : m_previous_descriptor_list(descriptor_list)
    {
        descriptor_list = nullptr;
    }

    ~ScopedDescriptorList()
    {
        descriptor_list = m_previous_descriptor_list;
    }

private:
    descriptor_data* m_previous_descriptor_list;
};

descriptor_data make_descriptor()
{
    descriptor_data descriptor {};
    descriptor.output = descriptor.small_outbuf;
    descriptor.small_outbuf[0] = '\0';
    descriptor.bufptr = 0;
    descriptor.bufspace = SMALL_BUFSIZE - 1;
    descriptor.connected = CON_PLYNG;
    return descriptor;
}

char_data* attach_active_character(
    descriptor_data* descriptor, const char* name, int level, long idnum, int race = RACE_HUMAN)
{
    char_data* character = new char_data {};
    clear_char(character, MOB_VOID);
    character->player.name = strdup(name);
    character->player.level = level;
    character->player.race = race;
    character->specials2.idnum = idnum;
    character->desc = descriptor;
    descriptor->character = character;
    return character;
}

std::string read_file_contents(const std::string& path)
{
    FILE* file = std::fopen(path.c_str(), "rb");
    EXPECT_NE(file, nullptr);
    if (file == nullptr)
        return "";

    std::string contents;
    char buffer[256];
    while (true) {
        const size_t bytes_read = std::fread(buffer, sizeof(char), sizeof(buffer), file);
        if (bytes_read > 0)
            contents.append(buffer, bytes_read);
        if (bytes_read < sizeof(buffer)) {
            EXPECT_EQ(std::ferror(file), 0);
            break;
        }
    }

    EXPECT_EQ(std::fclose(file), 0);
    return contents;
}

void write_text_file(const std::string& path, const std::string& contents)
{
    FILE* file = std::fopen(path.c_str(), "wb");
    ASSERT_NE(file, nullptr);
    ASSERT_EQ(std::fwrite(contents.data(), sizeof(char), contents.size(), file), contents.size());
    ASSERT_EQ(std::fclose(file), 0);
}

std::string write_valid_legacy_player_file(const std::string& root_directory, const char_file_u& stored_character)
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
    const std::string final_path = account::legacy_player_file_path(root_directory, stored_character.name);
    write_text_file(final_path, player_text);
    if (generated_path != final_path)
        std::remove(generated_path.c_str());

    delete character;
    free(player_table[0].name);
    delete[] player_table;
    player_table = previous_player_table;
    top_of_p_table = previous_top_of_p_table;
    return player_text;
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

TEST(ActWiz, AccountCommandAcceptsEmailForShowAndMutatingSubcommands)
{
    TemporaryDirectory temp_directory;
    ScopedWorkingDirectory working_directory(temp_directory.path());
    ASSERT_EQ(mkdir("accounts", 0700), 0);
    ASSERT_EQ(mkdir("accounts/A-E", 0700), 0);

    account::AccountData created_account;
    std::string error_message;
    ASSERT_TRUE(account::create_account(".", "alpha-admin", "player@example.com", "ValidPass1", 1700010200, &created_account, &error_message)) << error_message;

    descriptor_data descriptor = make_descriptor();
    char_data admin {};
    admin.desc = &descriptor;
    admin.player.name = strdup("tester");

    char show_command[] = "show player@example.com";
    do_account(&admin, show_command, nullptr, 0, 0);
    EXPECT_NE(std::string(descriptor.output).find("Account email: player@example.com\n\r"), std::string::npos);
    EXPECT_NE(std::string(descriptor.output).find("Internal name: alpha-admin\n\r"), std::string::npos);

    descriptor.output[0] = '\0';
    descriptor.bufptr = 0;
    descriptor.bufspace = SMALL_BUFSIZE - 1;

    char show_internal_name_command[] = "show alpha-admin";
    do_account(&admin, show_internal_name_command, nullptr, 0, 0);
    EXPECT_NE(std::string(descriptor.output).find("Account email: player@example.com\n\r"), std::string::npos);
    EXPECT_NE(std::string(descriptor.output).find("Internal name: alpha-admin\n\r"), std::string::npos);

    descriptor.output[0] = '\0';
    descriptor.bufptr = 0;
    descriptor.bufspace = SMALL_BUFSIZE - 1;

    char verify_command[] = "verify player@example.com";
    do_account(&admin, verify_command, nullptr, 0, 0);
    EXPECT_EQ(std::string(descriptor.output), "Account email verified.\n\r");

    account::AccountData verified_account;
    ASSERT_TRUE(account::read_account_file(".", "alpha-admin", &verified_account, &error_message)) << error_message;
    EXPECT_TRUE(verified_account.email_verified);

    descriptor.output[0] = '\0';
    descriptor.bufptr = 0;
    descriptor.bufspace = SMALL_BUFSIZE - 1;

    char unverify_command[] = "unverify alpha-admin";
    do_account(&admin, unverify_command, nullptr, 0, 0);
    EXPECT_EQ(std::string(descriptor.output), "Account email marked unverified.\n\r");

    account::AccountData unverified_account;
    ASSERT_TRUE(account::read_account_file(".", "alpha-admin", &unverified_account, &error_message)) << error_message;
    EXPECT_FALSE(unverified_account.email_verified);

    free(admin.player.name);
}

TEST(ActWiz, AccountCommandUsesIdentifierLookupForAdditionalMutatingSubcommands)
{
    TemporaryDirectory temp_directory;
    ScopedWorkingDirectory working_directory(temp_directory.path());
    ASSERT_EQ(mkdir("accounts", 0700), 0);
    ASSERT_EQ(mkdir("accounts/A-E", 0700), 0);

    account::AccountData created_account;
    std::string error_message;
    ASSERT_TRUE(account::create_account(".", "alpha-admin", "player@example.com", "ValidPass1", 1700010200, &created_account, &error_message)) << error_message;

    descriptor_data descriptor = make_descriptor();
    char_data admin {};
    admin.desc = &descriptor;
    admin.player.name = strdup("tester");

    char block_command[] = "block player@example.com Testing block reason";
    do_account(&admin, block_command, nullptr, 0, 0);
    EXPECT_EQ(std::string(descriptor.output), "Account blocked.\n\r");

    account::AccountData blocked_account;
    ASSERT_TRUE(account::read_account_file(".", "alpha-admin", &blocked_account, &error_message)) << error_message;
    EXPECT_TRUE(blocked_account.blocked);
    EXPECT_EQ(blocked_account.block_reason, "Testing block reason");

    descriptor.output[0] = '\0';
    descriptor.bufptr = 0;
    descriptor.bufspace = SMALL_BUFSIZE - 1;

    char unblock_command[] = "unblock alpha-admin";
    do_account(&admin, unblock_command, nullptr, 0, 0);
    EXPECT_EQ(std::string(descriptor.output), "Account unblocked.\n\r");

    account::AccountData unblocked_account;
    ASSERT_TRUE(account::read_account_file(".", "alpha-admin", &unblocked_account, &error_message)) << error_message;
    EXPECT_FALSE(unblocked_account.blocked);

    descriptor.output[0] = '\0';
    descriptor.bufptr = 0;
    descriptor.bufspace = SMALL_BUFSIZE - 1;

    char passwd_command[] = "passwd player@example.com NewValid9";
    do_account(&admin, passwd_command, nullptr, 0, 0);
    EXPECT_EQ(std::string(descriptor.output), "Account password reset.\n\r");

    account::AccountData reset_account;
    ASSERT_TRUE(account::read_account_file(".", "alpha-admin", &reset_account, &error_message)) << error_message;
    EXPECT_TRUE(account::verify_password("NewValid9", reset_account.password_hash));

    descriptor.output[0] = '\0';
    descriptor.bufptr = 0;
    descriptor.bufspace = SMALL_BUFSIZE - 1;

    char addchar_command[] = "addchar player@example.com aragorn";
    do_account(&admin, addchar_command, nullptr, 0, 0);
    EXPECT_EQ(std::string(descriptor.output), "Character linked to account.\n\r");

    account::AccountData linked_account;
    ASSERT_TRUE(account::read_account_file(".", "alpha-admin", &linked_account, &error_message)) << error_message;
    EXPECT_TRUE(account::account_has_character(linked_account, "aragorn"));

    free(admin.player.name);
}

TEST(ActWiz, AccountUnlockSelectGrantsForRestrictingActiveLinkedSessionByEmail)
{
    TemporaryDirectory temp_directory;
    ScopedWorkingDirectory working_directory(temp_directory.path());
    ScopedDescriptorList descriptor_list_scope;
    ASSERT_EQ(mkdir("accounts", 0700), 0);
    ASSERT_EQ(mkdir("accounts/A-E", 0700), 0);

    account::AccountData created_account;
    std::string error_message;
    ASSERT_TRUE(account::create_account(".", "alpha-unlock-email", "unlock-email@example.com", "ValidPass1", 1700010200, &created_account, &error_message)) << error_message;
    ASSERT_TRUE(account::admin_link_character(".", "alpha-unlock-email", "aragorn", 1700010201, &created_account, &error_message)) << error_message;
    const std::string account_json_before = read_file_contents(account::account_file_path(".", "unlock-email@example.com"));

    descriptor_data active_descriptor = make_descriptor();
    active_descriptor.connected = CON_PLYNG;
    std::snprintf(active_descriptor.account_name, sizeof(active_descriptor.account_name), "%s", "alpha-unlock-email");
    std::snprintf(active_descriptor.account_email, sizeof(active_descriptor.account_email), "%s", "unlock-email@example.com");
    attach_active_character(&active_descriptor, "aragorn", 50, 4242);
    descriptor_list = &active_descriptor;

    descriptor_data admin_descriptor = make_descriptor();
    char_data admin {};
    admin.desc = &admin_descriptor;
    admin.player.name = strdup("tester");

    char unlock_command[] = "unlockselect unlock-email@example.com";
    do_account(&admin, unlock_command, nullptr, 0, 0);

    EXPECT_EQ(std::string(admin_descriptor.output), "Account linked-character selection unlocked once.\n\r");
    EXPECT_EQ(read_file_contents(account::account_file_path(".", "unlock-email@example.com")), account_json_before)
        << "Unlock state must stay runtime-only and out of account.json.";

    free(admin.player.name);
    free_char(active_descriptor.character);
    active_descriptor.character = nullptr;
}

TEST(ActWiz, AccountUnlockSelectGrantsForRestrictingLinklessSessionByAccountName)
{
    TemporaryDirectory temp_directory;
    ScopedWorkingDirectory working_directory(temp_directory.path());
    ScopedDescriptorList descriptor_list_scope;
    ASSERT_EQ(mkdir("accounts", 0700), 0);
    ASSERT_EQ(mkdir("accounts/A-E", 0700), 0);

    account::AccountData created_account;
    std::string error_message;
    ASSERT_TRUE(account::create_account(".", "alpha-linkless", "unlock-linkless@example.com", "ValidPass1", 1700010200, &created_account, &error_message)) << error_message;
    ASSERT_TRUE(account::admin_link_character(".", "alpha-linkless", "aragorn", 1700010201, &created_account, &error_message)) << error_message;

    descriptor_data active_descriptor = make_descriptor();
    active_descriptor.connected = CON_LINKLS;
    std::snprintf(active_descriptor.account_name, sizeof(active_descriptor.account_name), "%s", "alpha-linkless");
    std::snprintf(active_descriptor.account_email, sizeof(active_descriptor.account_email), "%s", "unlock-linkless@example.com");
    attach_active_character(&active_descriptor, "aragorn", 50, 4242);
    descriptor_list = &active_descriptor;

    descriptor_data admin_descriptor = make_descriptor();
    char_data admin {};
    admin.desc = &admin_descriptor;
    admin.player.name = strdup("tester");

    char unlock_command[] = "unlockselect alpha-linkless";
    do_account(&admin, unlock_command, nullptr, 0, 0);

    EXPECT_EQ(std::string(admin_descriptor.output), "Account linked-character selection unlocked once.\n\r");

    free(admin.player.name);
    free_char(active_descriptor.character);
    active_descriptor.character = nullptr;
}

TEST(ActWiz, AccountUnlockSelectRejectsWhenNoRestrictingActiveSessionExists)
{
    TemporaryDirectory temp_directory;
    ScopedWorkingDirectory working_directory(temp_directory.path());
    ScopedDescriptorList descriptor_list_scope;
    ASSERT_EQ(mkdir("accounts", 0700), 0);
    ASSERT_EQ(mkdir("accounts/A-E", 0700), 0);

    account::AccountData created_account;
    std::string error_message;
    ASSERT_TRUE(account::create_account(".", "alpha-unlock-reject", "unlock-reject@example.com", "ValidPass1", 1700010200, &created_account, &error_message)) << error_message;
    ASSERT_TRUE(account::admin_link_character(".", "alpha-unlock-reject", "aragorn", 1700010201, &created_account, &error_message)) << error_message;
    ASSERT_TRUE(account::admin_link_character(".", "alpha-unlock-reject", "boromir", 1700010202, &created_account, &error_message)) << error_message;

    descriptor_data admin_descriptor = make_descriptor();
    char_data admin {};
    admin.desc = &admin_descriptor;
    admin.player.name = strdup("tester");

    char no_session_command[] = "unlockselect unlock-reject@example.com";
    do_account(&admin, no_session_command, nullptr, 0, 0);
    EXPECT_EQ(std::string(admin_descriptor.output),
        "That account does not currently have a restricting active linked character session.\n\r");

    admin_descriptor.output[0] = '\0';
    admin_descriptor.bufptr = 0;
    admin_descriptor.bufspace = SMALL_BUFSIZE - 1;

    descriptor_data high_level_descriptor = make_descriptor();
    high_level_descriptor.connected = CON_PLYNG;
    std::snprintf(high_level_descriptor.account_name, sizeof(high_level_descriptor.account_name), "%s", "alpha-unlock-reject");
    attach_active_character(&high_level_descriptor, "aragorn", 92, 4242);
    descriptor_list = &high_level_descriptor;

    char high_session_command[] = "unlockselect alpha-unlock-reject";
    do_account(&admin, high_session_command, nullptr, 0, 0);
    EXPECT_EQ(std::string(admin_descriptor.output),
        "That account does not currently have a restricting active linked character session.\n\r");

    admin_descriptor.output[0] = '\0';
    admin_descriptor.bufptr = 0;
    admin_descriptor.bufspace = SMALL_BUFSIZE - 1;

    descriptor_data unlinked_descriptor = make_descriptor();
    unlinked_descriptor.connected = CON_PLYNG;
    std::snprintf(unlinked_descriptor.account_name, sizeof(unlinked_descriptor.account_name), "%s", "alpha-unlock-reject");
    attach_active_character(&unlinked_descriptor, "legolas", 50, 5252);
    descriptor_list = &unlinked_descriptor;

    char unlinked_command[] = "unlockselect unlock-reject@example.com";
    do_account(&admin, unlinked_command, nullptr, 0, 0);
    EXPECT_EQ(std::string(admin_descriptor.output),
        "That account does not currently have a restricting active linked character session.\n\r");

    admin_descriptor.output[0] = '\0';
    admin_descriptor.bufptr = 0;
    admin_descriptor.bufspace = SMALL_BUFSIZE - 1;

    descriptor_data other_account_descriptor = make_descriptor();
    other_account_descriptor.connected = CON_PLYNG;
    std::snprintf(other_account_descriptor.account_name, sizeof(other_account_descriptor.account_name), "%s", "other");
    attach_active_character(&other_account_descriptor, "aragorn", 50, 6262);
    descriptor_list = &other_account_descriptor;

    char other_account_command[] = "unlockselect alpha-unlock-reject";
    do_account(&admin, other_account_command, nullptr, 0, 0);
    EXPECT_EQ(std::string(admin_descriptor.output),
        "That account does not currently have a restricting active linked character session.\n\r");

    free(admin.player.name);
    free_char(high_level_descriptor.character);
    free_char(unlinked_descriptor.character);
    free_char(other_account_descriptor.character);
    high_level_descriptor.character = nullptr;
    unlinked_descriptor.character = nullptr;
    other_account_descriptor.character = nullptr;
}

TEST(ActWiz, AccountUnlockSelectReplacesStalePendingUnlockForLaterRestriction)
{
    TemporaryDirectory temp_directory;
    ScopedWorkingDirectory working_directory(temp_directory.path());
    ScopedDescriptorList descriptor_list_scope;
    ASSERT_EQ(mkdir("accounts", 0700), 0);
    ASSERT_EQ(mkdir("accounts/A-E", 0700), 0);

    account::AccountData created_account;
    std::string error_message;
    ASSERT_TRUE(account::create_account(".", "alpha-stalegrant", "stale-grant@example.com", "ValidPass1", 1700010200, &created_account, &error_message)) << error_message;
    ASSERT_TRUE(account::admin_link_character(".", "alpha-stalegrant", "aragorn", 1700010201, &created_account, &error_message)) << error_message;
    ASSERT_TRUE(account::admin_link_character(".", "alpha-stalegrant", "boromir", 1700010202, &created_account, &error_message)) << error_message;

    descriptor_data admin_descriptor = make_descriptor();
    char_data admin {};
    admin.desc = &admin_descriptor;
    admin.player.name = strdup("tester");

    descriptor_data original_active_descriptor = make_descriptor();
    original_active_descriptor.connected = CON_PLYNG;
    std::snprintf(original_active_descriptor.account_name, sizeof(original_active_descriptor.account_name), "%s", "alpha-stalegrant");
    attach_active_character(&original_active_descriptor, "aragorn", 50, 4242);
    descriptor_list = &original_active_descriptor;

    char first_unlock_command[] = "unlockselect stale-grant@example.com";
    do_account(&admin, first_unlock_command, nullptr, 0, 0);
    EXPECT_EQ(std::string(admin_descriptor.output), "Account linked-character selection unlocked once.\n\r");

    descriptor_list = nullptr;
    free_char(original_active_descriptor.character);
    original_active_descriptor.character = nullptr;

    admin_descriptor.output[0] = '\0';
    admin_descriptor.bufptr = 0;
    admin_descriptor.bufspace = SMALL_BUFSIZE - 1;

    descriptor_data later_active_descriptor = make_descriptor();
    later_active_descriptor.connected = CON_PLYNG;
    std::snprintf(later_active_descriptor.account_name, sizeof(later_active_descriptor.account_name), "%s", "alpha-stalegrant");
    attach_active_character(&later_active_descriptor, "boromir", 50, 5252);
    descriptor_list = &later_active_descriptor;

    char second_unlock_command[] = "unlockselect alpha-stalegrant";
    do_account(&admin, second_unlock_command, nullptr, 0, 0);
    EXPECT_EQ(std::string(admin_descriptor.output), "Account linked-character selection unlocked once.\n\r");

    free(admin.player.name);
    free_char(later_active_descriptor.character);
    later_active_descriptor.character = nullptr;
}

TEST(ActWiz, AccountCommandAcceptsEmailForMigrateChar)
{
    TemporaryDirectory temp_directory;
    ScopedWorkingDirectory working_directory(temp_directory.path());
    ASSERT_EQ(mkdir("accounts", 0700), 0);
    ASSERT_EQ(mkdir("accounts/A-E", 0700), 0);
    ASSERT_EQ(mkdir("players", 0700), 0);
    ASSERT_EQ(mkdir("players/A-E", 0700), 0);
    ASSERT_EQ(mkdir("plrobjs", 0700), 0);
    ASSERT_EQ(mkdir("plrobjs/A-E", 0700), 0);
    ASSERT_EQ(mkdir("exploits", 0700), 0);
    ASSERT_EQ(mkdir("exploits/A-E", 0700), 0);

    account::AccountData created_account;
    std::string error_message;
    ASSERT_TRUE(account::create_account(".", "alpha-admin", "player@example.com", "ValidPass1", 1700010200, &created_account, &error_message)) << error_message;

    char_file_u legacy_character = make_stored_character("aragorn", 12, RACE_HUMAN);
    legacy_character.specials2.idnum = 4242;
    write_valid_legacy_player_file(temp_directory.path(), legacy_character);
    write_text_file(account::legacy_object_file_path(temp_directory.path(), "aragorn"), make_valid_object_bytes());
    write_text_file(account::legacy_exploits_file_path(temp_directory.path(), "aragorn"), make_valid_exploit_bytes());
    ScopedPlayerTableEntry player_index_entry("aragorn");
    player_table[0].level = legacy_character.level;
    player_table[0].race = legacy_character.race;
    player_table[0].idnum = legacy_character.specials2.idnum;
    player_table[0].log_time = legacy_character.last_logon;
    player_table[0].flags = legacy_character.specials2.act;

    descriptor_data descriptor = make_descriptor();
    char_data admin {};
    admin.desc = &descriptor;
    admin.player.name = strdup("tester");

    char migrate_command[] = "migratechar player@example.com aragorn";
    do_account(&admin, migrate_command, nullptr, 0, 0);
    EXPECT_EQ(std::string(descriptor.output), "Character migrated into account storage.\n\r");

    account::AccountData migrated_account;
    ASSERT_TRUE(account::read_account_file(".", "alpha-admin", &migrated_account, &error_message)) << error_message;
    EXPECT_TRUE(account::account_has_character(migrated_account, "aragorn"));

    struct stat file_info {};
    EXPECT_EQ(stat(account::account_character_player_path(".", "alpha-admin", "aragorn").c_str(), &file_info), 0);
    EXPECT_NE(stat(account::account_character_object_path(".", "alpha-admin", "aragorn").c_str(), &file_info), -1);
    EXPECT_NE(stat(account::account_character_exploits_path(".", "alpha-admin", "aragorn").c_str(), &file_info), -1);
    EXPECT_EQ(stat(account::legacy_player_file_path(".", "aragorn").c_str(), &file_info), -1);
    EXPECT_EQ(stat(account::legacy_object_file_path(".", "aragorn").c_str(), &file_info), -1);
    EXPECT_EQ(stat(account::legacy_exploits_file_path(".", "aragorn").c_str(), &file_info), -1);

    char_file_u second_legacy_character = make_stored_character("boromir", 10, RACE_HUMAN);
    second_legacy_character.specials2.idnum = 4343;
    write_valid_legacy_player_file(temp_directory.path(), second_legacy_character);
    write_text_file(account::legacy_object_file_path(temp_directory.path(), "boromir"), make_valid_object_bytes());
    write_text_file(account::legacy_exploits_file_path(temp_directory.path(), "boromir"), make_valid_exploit_bytes());

    descriptor.output[0] = '\0';
    descriptor.bufptr = 0;
    descriptor.bufspace = SMALL_BUFSIZE - 1;

    char migrate_internal_name_command[] = "migratechar alpha-admin boromir";
    do_account(&admin, migrate_internal_name_command, nullptr, 0, 0);
    EXPECT_EQ(std::string(descriptor.output), "Character migrated into account storage.\n\r");

    ASSERT_TRUE(account::read_account_file(".", "alpha-admin", &migrated_account, &error_message)) << error_message;
    EXPECT_TRUE(account::account_has_character(migrated_account, "boromir"));
    EXPECT_EQ(stat(account::legacy_player_file_path(".", "boromir").c_str(), &file_info), -1);
    EXPECT_EQ(stat(account::legacy_object_file_path(".", "boromir").c_str(), &file_info), -1);
    EXPECT_EQ(stat(account::legacy_exploits_file_path(".", "boromir").c_str(), &file_info), -1);

    free(admin.player.name);
}

TEST(ActWiz, WhoAcctShowsAuthenticatedAccountsAndCurrentCharacterOrMenuState)
{
    ScopedDescriptorList descriptor_list_scope;

    descriptor_data admin_descriptor = make_descriptor();
    char_data admin {};
    admin.desc = &admin_descriptor;
    admin.player.name = strdup("tester");

    descriptor_data playing_descriptor {};
    playing_descriptor.desc_num = 12;
    playing_descriptor.connected = CON_PLYNG;
    std::snprintf(playing_descriptor.account_name, sizeof(playing_descriptor.account_name), "%s", "acct-one");
    std::snprintf(playing_descriptor.account_email, sizeof(playing_descriptor.account_email), "%s", "player1@example.com");
    std::snprintf(playing_descriptor.host, sizeof(playing_descriptor.host), "%s", "127.0.0.1");
    playing_descriptor.next = descriptor_list;
    descriptor_list = &playing_descriptor;

    char_data playing_character {};
    clear_char(&playing_character, MOB_VOID);
    playing_character.player.name = strdup("aragorn");
    playing_descriptor.character = &playing_character;

    descriptor_data account_menu_descriptor {};
    account_menu_descriptor.desc_num = 13;
    account_menu_descriptor.connected = CON_ACCTMENU;
    std::snprintf(account_menu_descriptor.account_name, sizeof(account_menu_descriptor.account_name), "%s", "acct-two");
    std::snprintf(account_menu_descriptor.account_email, sizeof(account_menu_descriptor.account_email), "%s", "player2@example.com");
    std::snprintf(account_menu_descriptor.host, sizeof(account_menu_descriptor.host), "%s", "127.0.0.2");
    account_menu_descriptor.next = descriptor_list;
    descriptor_list = &account_menu_descriptor;

    descriptor_data character_menu_descriptor {};
    character_menu_descriptor.desc_num = 14;
    character_menu_descriptor.connected = CON_SLCT;
    std::snprintf(character_menu_descriptor.account_name, sizeof(character_menu_descriptor.account_name), "%s", "acct-three");
    std::snprintf(character_menu_descriptor.account_email, sizeof(character_menu_descriptor.account_email), "%s", "player3@example.com");
    std::snprintf(character_menu_descriptor.host, sizeof(character_menu_descriptor.host), "%s", "127.0.0.3");
    character_menu_descriptor.next = descriptor_list;
    descriptor_list = &character_menu_descriptor;

    char_data character_menu_character {};
    clear_char(&character_menu_character, MOB_VOID);
    character_menu_character.player.name = strdup("legolas");
    character_menu_descriptor.character = &character_menu_character;

    descriptor_data legacy_descriptor {};
    legacy_descriptor.desc_num = 15;
    legacy_descriptor.connected = CON_PLYNG;
    std::snprintf(legacy_descriptor.host, sizeof(legacy_descriptor.host), "%s", "127.0.0.4");
    legacy_descriptor.next = descriptor_list;
    descriptor_list = &legacy_descriptor;

    char_data legacy_character {};
    clear_char(&legacy_character, MOB_VOID);
    legacy_character.player.name = strdup("boromir");
    legacy_descriptor.character = &legacy_character;

    char empty_argument[] = "";
    do_whoacct(&admin, empty_argument, nullptr, 0, 0);

    const std::string output = admin_descriptor.output;
    char expected_playing_row[128];
    char expected_account_menu_row[128];
    char expected_character_menu_row[128];
    std::string expected_output;
    std::snprintf(expected_playing_row, sizeof(expected_playing_row), "%3d %-26.26s %-12.12s %-16.16s %s\n\r",
        12, "player1@example.com", "Aragorn", "Playing", "127.0.0.1");
    std::snprintf(expected_account_menu_row, sizeof(expected_account_menu_row), "%3d %-26.26s %-12.12s %-16.16s %s\n\r",
        13, "player2@example.com", "-", "Account Menu", "127.0.0.2");
    std::snprintf(expected_character_menu_row, sizeof(expected_character_menu_row),
        "%3d %-26.26s %-12.12s %-16.16s %s\n\r", 14, "player3@example.com", "Legolas",
        "Character Menu", "127.0.0.3");
    EXPECT_NE(output.find("Num "), std::string::npos);
    EXPECT_NE(output.find("Account"), std::string::npos);
    EXPECT_NE(output.find("Character"), std::string::npos);
    EXPECT_NE(output.find("State"), std::string::npos);
    EXPECT_NE(output.find("Site"), std::string::npos);
    EXPECT_NE(output.find(expected_playing_row), std::string::npos) << output;
    EXPECT_NE(output.find(expected_account_menu_row), std::string::npos) << output;
    EXPECT_NE(output.find(expected_character_menu_row), std::string::npos) << output;
    EXPECT_EQ(output.find("->"), std::string::npos);
    EXPECT_EQ(output.find("("), std::string::npos);
    EXPECT_EQ(output.find(")"), std::string::npos);
    EXPECT_EQ(output.find("boromir"), std::string::npos);
    EXPECT_NE(output.find("3 visible account sessions connected."), std::string::npos);
    expected_output += "Num   Account                    Character    State            Site\n\r";
    expected_output += "--- -------------------------- ------------ ---------------- ------------------------\n\r";
    expected_output += expected_character_menu_row;
    expected_output += expected_account_menu_row;
    expected_output += expected_playing_row;
    expected_output += "\n\r3 visible account sessions connected.\n\r";
    EXPECT_EQ(output, expected_output);

    free(admin.player.name);
    free(playing_character.player.name);
    free(character_menu_character.player.name);
    free(legacy_character.player.name);
}

TEST(ActWiz, WhoAcctReportsWhenNoAuthenticatedAccountsAreConnected)
{
    ScopedDescriptorList descriptor_list_scope;

    descriptor_data admin_descriptor = make_descriptor();
    char_data admin {};
    admin.desc = &admin_descriptor;
    admin.player.name = strdup("tester");

    descriptor_data legacy_descriptor {};
    legacy_descriptor.connected = CON_PLYNG;
    descriptor_list = &legacy_descriptor;

    char_data legacy_character {};
    clear_char(&legacy_character, MOB_VOID);
    legacy_character.player.name = strdup("boromir");
    legacy_descriptor.character = &legacy_character;

    char empty_argument[] = "";
    do_whoacct(&admin, empty_argument, nullptr, 0, 0);

    EXPECT_EQ(std::string(admin_descriptor.output), "No visible account sessions connected.\n\r");

    free(admin.player.name);
    free(legacy_character.player.name);
}

TEST(ActWiz, WhoAcctListsDuplicateAuthenticatedSessionsSeparatelyAndSkipsClosingDescriptors)
{
    ScopedDescriptorList descriptor_list_scope;

    descriptor_data admin_descriptor = make_descriptor();
    char_data admin {};
    admin.desc = &admin_descriptor;
    admin.player.name = strdup("tester");

    descriptor_data closing_descriptor {};
    closing_descriptor.desc_num = 21;
    closing_descriptor.connected = CON_CLOSE;
    std::snprintf(closing_descriptor.account_name, sizeof(closing_descriptor.account_name), "%s", "acct-one");
    std::snprintf(closing_descriptor.account_email, sizeof(closing_descriptor.account_email), "%s", "player1@example.com");
    descriptor_list = &closing_descriptor;

    descriptor_data menu_descriptor {};
    menu_descriptor.desc_num = 22;
    menu_descriptor.connected = CON_ACCTMENU;
    std::snprintf(menu_descriptor.account_name, sizeof(menu_descriptor.account_name), "%s", "acct-one");
    std::snprintf(menu_descriptor.account_email, sizeof(menu_descriptor.account_email), "%s", "player1@example.com");
    menu_descriptor.next = descriptor_list;
    descriptor_list = &menu_descriptor;

    descriptor_data playing_descriptor {};
    playing_descriptor.desc_num = 23;
    playing_descriptor.connected = CON_PLYNG;
    std::snprintf(playing_descriptor.account_name, sizeof(playing_descriptor.account_name), "%s", "acct-one");
    std::snprintf(playing_descriptor.account_email, sizeof(playing_descriptor.account_email), "%s", "player1@example.com");
    playing_descriptor.next = descriptor_list;
    descriptor_list = &playing_descriptor;

    char_data playing_character {};
    clear_char(&playing_character, MOB_VOID);
    playing_character.player.name = strdup("aragorn");
    playing_descriptor.character = &playing_character;

    char empty_argument[] = "";
    do_whoacct(&admin, empty_argument, nullptr, 0, 0);

    const std::string output = admin_descriptor.output;
    EXPECT_NE(output.find(" 23 player1@example.com"), std::string::npos);
    EXPECT_NE(output.find(" 22 player1@example.com"), std::string::npos);
    EXPECT_NE(output.find("Aragorn"), std::string::npos);
    EXPECT_NE(output.find("Account Menu"), std::string::npos);
    EXPECT_EQ(output.find(" 21 player1@example.com"), std::string::npos);
    EXPECT_NE(output.find("2 visible account sessions connected."), std::string::npos);

    free(admin.player.name);
    free(playing_character.player.name);
}

TEST(ActWiz, WhoAcctShowsCharacterSelectStateAndSkipsPendingVerificationSessions)
{
    ScopedDescriptorList descriptor_list_scope;

    descriptor_data admin_descriptor = make_descriptor();
    char_data admin {};
    admin.desc = &admin_descriptor;
    admin.player.name = strdup("tester");

    descriptor_data pending_verification_descriptor {};
    pending_verification_descriptor.desc_num = 31;
    pending_verification_descriptor.connected = CON_ACCTVERIFY;
    std::snprintf(pending_verification_descriptor.account_name, sizeof(pending_verification_descriptor.account_name), "%s", "acct-one");
    std::snprintf(pending_verification_descriptor.account_email, sizeof(pending_verification_descriptor.account_email), "%s", "player1@example.com");
    descriptor_list = &pending_verification_descriptor;

    descriptor_data select_descriptor {};
    select_descriptor.desc_num = 32;
    select_descriptor.connected = CON_ACCTSLCT;
    std::snprintf(select_descriptor.account_name, sizeof(select_descriptor.account_name), "%s", "acct-two");
    std::snprintf(select_descriptor.account_email, sizeof(select_descriptor.account_email), "%s", "player2@example.com");
    select_descriptor.next = descriptor_list;
    descriptor_list = &select_descriptor;

    char empty_argument[] = "";
    do_whoacct(&admin, empty_argument, nullptr, 0, 0);

    const std::string output = admin_descriptor.output;
    EXPECT_NE(output.find(" 32 player2@example.com"), std::string::npos);
    EXPECT_NE(output.find("Character Select"), std::string::npos);
    EXPECT_EQ(output.find("player1@example.com"), std::string::npos);
    EXPECT_NE(output.find("1 visible account session connected.\n\r"), std::string::npos);

    free(admin.player.name);
}

TEST(ActWiz, WhoAcctSanitizesDisplayedAccountAndHostFields)
{
    ScopedDescriptorList descriptor_list_scope;

    descriptor_data admin_descriptor = make_descriptor();
    char_data admin {};
    admin.desc = &admin_descriptor;
    admin.player.name = strdup("tester");

    descriptor_data hostile_descriptor {};
    hostile_descriptor.desc_num = 40;
    hostile_descriptor.connected = CON_ACCTMENU;
    std::snprintf(hostile_descriptor.account_name, sizeof(hostile_descriptor.account_name), "%s", "acct-one");
    std::snprintf(hostile_descriptor.account_email, sizeof(hostile_descriptor.account_email), "%s",
        "player@example.com\r\nFAKE");
    std::snprintf(hostile_descriptor.host, sizeof(hostile_descriptor.host), "%s", "127.0.0.1\t\x1b[31m");
    descriptor_list = &hostile_descriptor;

    char empty_argument[] = "";
    do_whoacct(&admin, empty_argument, nullptr, 0, 0);

    const std::string output = admin_descriptor.output;
    EXPECT_EQ(output.find("\x1b"), std::string::npos);
    EXPECT_EQ(output.find("player@example.com\r\nFAKE"), std::string::npos);
    EXPECT_EQ(output.find("\n\rFAKE"), std::string::npos);
    EXPECT_NE(output.find("player@example.com  FAKE"), std::string::npos);
    EXPECT_NE(output.find("127.0.0.1 ?[31m"), std::string::npos);

    free(admin.player.name);
}

TEST(ActWiz, WhoAcctFormatsLongFieldsIntoStableColumns)
{
    ScopedDescriptorList descriptor_list_scope;

    descriptor_data admin_descriptor = make_descriptor();
    char_data admin {};
    admin.desc = &admin_descriptor;
    admin.player.name = strdup("tester");

    descriptor_data linking_descriptor {};
    linking_descriptor.desc_num = 41;
    linking_descriptor.connected = CON_ACCTLINKPWD;
    std::snprintf(linking_descriptor.account_name, sizeof(linking_descriptor.account_name), "%s", "acct-one");
    std::snprintf(linking_descriptor.account_email, sizeof(linking_descriptor.account_email), "%s",
        "verylongplayeremailaddress@example.com");
    std::snprintf(linking_descriptor.host, sizeof(linking_descriptor.host), "%s",
        "proxy-host-name-with-many-segments.example.net");
    descriptor_list = &linking_descriptor;

    char empty_argument[] = "";
    do_whoacct(&admin, empty_argument, nullptr, 0, 0);

    char expected_row[160];
    std::snprintf(expected_row, sizeof(expected_row), "%3d %-26.26s %-12.12s %-16.16s %s\n\r", 41,
        "verylongplayeremailaddress@example.com", "-", "Linking Character",
        "proxy-host-name-with-many-segments.example.net");

    const std::string output = admin_descriptor.output;
    EXPECT_NE(output.find(expected_row), std::string::npos) << output;

    free(admin.player.name);
}

} // namespace
