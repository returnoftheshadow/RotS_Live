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
extern struct player_index_element* player_table;
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

} // namespace
