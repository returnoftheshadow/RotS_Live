#include "../account_management.h"
#include "../db.h"
#include "../handler.h"
#include "../interpre.h"
#include "../limits.h"
#include "../profs.h"
#include "../structs.h"
#include "../utils.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <fcntl.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

extern struct player_index_element* player_table;
extern struct char_data* character_list;
extern struct room_data world;
extern int r_mortal_start_room[];
extern int top_of_p_table;
extern int top_of_world;
extern char* motd;
void clear_char(struct char_data* ch, int mode);
sh_int get_naked_perception(struct char_data* ch);
int register_pc_char(struct char_data* ch);
void introduce_char(struct descriptor_data* d);
int create_entry(char* name);
void save_player(struct char_data* ch, int load_room, int index_pos);

namespace {

char_file_u make_stored_character(const char* name, int level, int race)
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
        char directory_template[] = "/tmp/rots-interpre-account-menu-XXXXXX";
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
        if (!m_original_path.empty()) {
            EXPECT_EQ(chdir(m_original_path.c_str()), 0);
        }
    }

private:
    std::string m_original_path;
};

class ScopedStderrRedirect {
public:
    explicit ScopedStderrRedirect(const std::string& path)
        : m_path(path)
    {
        m_original_stderr_fd = dup(STDERR_FILENO);
        EXPECT_GE(m_original_stderr_fd, 0);

        m_redirect_fd = open(path.c_str(), O_CREAT | O_TRUNC | O_WRONLY, 0600);
        EXPECT_GE(m_redirect_fd, 0);
        if (m_redirect_fd >= 0)
            EXPECT_GE(dup2(m_redirect_fd, STDERR_FILENO), 0);
    }

    ~ScopedStderrRedirect()
    {
        if (m_original_stderr_fd >= 0) {
            fflush(stderr);
            EXPECT_GE(dup2(m_original_stderr_fd, STDERR_FILENO), 0);
            close(m_original_stderr_fd);
        }
        if (m_redirect_fd >= 0)
            close(m_redirect_fd);
    }

    std::string read_contents() const
    {
        fflush(stderr);

        FILE* file = std::fopen(m_path.c_str(), "rb");
        EXPECT_NE(file, nullptr);
        if (file == nullptr)
            return "";

        std::string contents;
        char buffer[1024];
        while (true) {
            const size_t bytes_read = std::fread(buffer, sizeof(char), sizeof(buffer), file);
            if (bytes_read > 0)
                contents.append(buffer, bytes_read);
            if (bytes_read < sizeof(buffer)) {
                EXPECT_EQ(std::ferror(file), 0);
                break;
            }
        }

        std::fclose(file);
        return contents;
    }

private:
    std::string m_path;
    int m_original_stderr_fd = -1;
    int m_redirect_fd = -1;
};

class ScopedPlayerTableReset {
public:
    ScopedPlayerTableReset()
        : m_previous_player_table(player_table)
        , m_previous_top_of_p_table(top_of_p_table)
    {
        player_table = nullptr;
        top_of_p_table = -1;
    }

    ~ScopedPlayerTableReset()
    {
        player_table = m_previous_player_table;
        top_of_p_table = m_previous_top_of_p_table;
    }

private:
    player_index_element* m_previous_player_table;
    int m_previous_top_of_p_table;
};

class ScopedStartRoomOverride {
public:
    ScopedStartRoomOverride(int race, int room_rnum)
        : m_race(race)
        , m_previous_room(r_mortal_start_room[race])
    {
        r_mortal_start_room[m_race] = room_rnum;
    }

    ~ScopedStartRoomOverride()
    {
        r_mortal_start_room[m_race] = m_previous_room;
    }

private:
    int m_race;
    int m_previous_room;
};

class ScopedMotdOverride {
public:
    explicit ScopedMotdOverride(char* temporary_motd)
        : m_previous_motd(motd)
    {
        motd = temporary_motd;
    }

    ~ScopedMotdOverride()
    {
        motd = m_previous_motd;
    }

private:
    char* m_previous_motd;
};

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

    free(player_table[0].name);
    delete[] player_table;
    player_table = previous_player_table;
    top_of_p_table = previous_top_of_p_table;
    free_char(character);

    return generated_path;
}

void ensure_test_world_room(int room_number)
{
    if (room_data::BASE_WORLD == nullptr)
        world.create_bulk(1);

    top_of_world = 0;
    world[0].number = room_number;
    world[0].name = strdup("The Testing Meadow");
    world[0].description = strdup("A quiet room used for account-menu tests.\n\r");
}

descriptor_data make_descriptor()
{
    descriptor_data descriptor {};
    descriptor.output = descriptor.small_outbuf;
    descriptor.small_outbuf[0] = '\0';
    descriptor.bufptr = 0;
    descriptor.bufspace = SMALL_BUFSIZE - 1;
    descriptor.pos = -1;
    descriptor.connected = CON_ACCTMENU;
    std::snprintf(descriptor.account_name, sizeof(descriptor.account_name), "%s", "acct");
    return descriptor;
}

TEST(InterpreAccountMenu, ShowAccountCharacterListCapitalizesFirstLetterOfStoredNames)
{
    TemporaryDirectory temp_directory;
    ScopedWorkingDirectory working_directory(temp_directory.path());
    ASSERT_EQ(mkdir("accounts", 0700), 0);
    ASSERT_EQ(mkdir("accounts/A-E", 0700), 0);

    account::AccountData account_data;
    account_data.account_name = "acct";
    account_data.characters = { "aragorn", "legolas" };
    std::string error_message;
    ASSERT_TRUE(account::create_account(".", "acct", "player@example.com", "ValidPass1", 1700010200, nullptr, &error_message)) << error_message;
    ASSERT_TRUE(account::write_account_character_file(".", "acct", make_stored_character("aragorn", 50, RACE_WOOD), &error_message)) << error_message;
    ASSERT_TRUE(account::write_account_character_file(".", "acct", make_stored_character("legolas", 45, RACE_HUMAN), &error_message)) << error_message;

    const std::string output = account::format_account_character_list(".", account_data);

    EXPECT_EQ(output,
        "\n\rLinked characters:\n\r"
        "1) [ 50 WdE] Aragorn     2) [ 45 Hum] Legolas     \n\r"
        "\n\r2 characters displayed.\n\r");
    EXPECT_EQ(output.find("aragorn"), std::string::npos);
    EXPECT_EQ(output.find("legolas"), std::string::npos);
}

TEST(InterpreAccountMenu, ShowAccountCharacterListOnlyChangesTheFirstByteOfStoredNames)
{
    TemporaryDirectory temp_directory;
    ScopedWorkingDirectory working_directory(temp_directory.path());
    ASSERT_EQ(mkdir("accounts", 0700), 0);
    ASSERT_EQ(mkdir("accounts/A-E", 0700), 0);

    account::AccountData account_data;
    account_data.account_name = "acct";
    account_data.characters = { "mCduck", "oBrian" };
    std::string error_message;
    ASSERT_TRUE(account::create_account(".", "acct", "player@example.com", "ValidPass1", 1700010200, nullptr, &error_message)) << error_message;
    ASSERT_TRUE(account::write_account_character_file(".", "acct", make_stored_character("mCduck", 12, RACE_WOOD), &error_message)) << error_message;
    ASSERT_TRUE(account::write_account_character_file(".", "acct", make_stored_character("oBrian", 9, RACE_HUMAN), &error_message)) << error_message;

    const std::string output = account::format_account_character_list(".", account_data);

    EXPECT_EQ(output,
        "\n\rLinked characters:\n\r"
        "1) [ 12 WdE] MCduck      2) [  9 Hum] OBrian      \n\r"
        "\n\r2 characters displayed.\n\r");
    EXPECT_EQ(output.find("Mcduck"), std::string::npos);
    EXPECT_EQ(output.find("Obrian"), std::string::npos);
}

TEST(InterpreAccountMenu, ShowAccountCharacterListKeepsEmptyMessageForAccountsWithoutCharacters)
{
    account::AccountData account_data;

    const std::string output = account::format_account_character_list(".", account_data);

    EXPECT_EQ(output, "\n\rNo linked characters yet.\n\r");
}

TEST(InterpreAccountMenu, ShowAccountCharacterListFallsBackToUnknownWhoStyleEntryWhenCharacterFileIsUnavailable)
{
    TemporaryDirectory temp_directory;
    ScopedWorkingDirectory working_directory(temp_directory.path());
    ASSERT_EQ(mkdir("accounts", 0700), 0);
    ASSERT_EQ(mkdir("accounts/A-E", 0700), 0);

    account::AccountData account_data;
    account_data.account_name = "acct";
    account_data.characters = { "aragorn" };
    std::string error_message;
    ASSERT_TRUE(account::create_account(".", "acct", "player@example.com", "ValidPass1", 1700010200, nullptr, &error_message)) << error_message;

    const std::string output = account::format_account_character_list(".", account_data);

    EXPECT_EQ(output,
        "\n\rLinked characters:\n\r"
        "1) [ ?? ???] Aragorn     \n\r"
        "\n\r1 character displayed.\n\r");
}

TEST(InterpreAccountMenu, ShowAccountCharacterListTruncatesVeryLargeRenderedLists)
{
    TemporaryDirectory temp_directory;
    ScopedWorkingDirectory working_directory(temp_directory.path());
    ASSERT_EQ(mkdir("accounts", 0700), 0);
    ASSERT_EQ(mkdir("accounts/A-E", 0700), 0);

    account::AccountData account_data;
    account_data.account_name = "acct";
    for (int index = 0; index < 105; ++index)
        account_data.characters.push_back("char" + std::to_string(index));
    std::string error_message;
    ASSERT_TRUE(account::create_account(".", "acct", "player@example.com", "ValidPass1", 1700010200, nullptr, &error_message)) << error_message;

    const std::string output = account::format_account_character_list(".", account_data);

    EXPECT_NE(output.find("[ ?? ???] Char0"), std::string::npos);
    EXPECT_NE(output.find("[ ?? ???] Char99"), std::string::npos);
    EXPECT_EQ(output.find("[ ?? ???] Char100"), std::string::npos);
    EXPECT_NE(output.find("\n\r... and 5 more\n\r"), std::string::npos);
    EXPECT_NE(output.find("\n\r100 characters displayed.\n\r"), std::string::npos);
}

TEST(InterpreAccountMenu, AccountMenuChoiceOneWritesCapitalizedCharacterListToDescriptorOutput)
{
    TemporaryDirectory temp_directory;
    ScopedWorkingDirectory working_directory(temp_directory.path());
    ASSERT_EQ(mkdir("accounts", 0700), 0);
    ASSERT_EQ(mkdir("accounts/A-E", 0700), 0);

    account::AccountData stored_account;
    std::string error_message;
    ASSERT_TRUE(account::create_account(".", "acct", "player@example.com", "ValidPass1", 1700010200, &stored_account, &error_message)) << error_message;
    ASSERT_TRUE(account::admin_verify_email(".", "acct", "test", 1700010201, &stored_account, &error_message)) << error_message;
    stored_account.characters = { "aragorn", "legolas" };
    stored_account.updated_at = 1700010201;
    ASSERT_TRUE(account::write_account_file(".", stored_account, &error_message)) << error_message;
    ASSERT_TRUE(account::write_account_character_file(".", "acct", make_stored_character("aragorn", 50, RACE_WOOD), &error_message)) << error_message;
    ASSERT_TRUE(account::write_account_character_file(".", "acct", make_stored_character("legolas", 45, RACE_HUMAN), &error_message)) << error_message;

    descriptor_data descriptor = make_descriptor();
    char choice[] = "1";

    nanny(&descriptor, choice);

    const std::string output = descriptor.output;
    EXPECT_EQ(output,
        "\n\rLinked characters:\n\r"
        "1) [ 50 WdE] Aragorn     2) [ 45 Hum] Legolas     \n\r"
        "\n\r2 characters displayed.\n\r"
        "\n\rAccount: player@example.com\n\r"
        "Linked characters: 2\n\r"
        "\n\r"
        "1) List linked characters\n\r"
        "2) Play a linked character\n\r"
        "3) Add an existing character\n\r"
        "4) Create a new character\n\r"
        "5) Reset account password\n\r"
        "0) Log out\n\r"
        "\n\r"
        "Choice: ");
}

TEST(InterpreAccountMenu, AccountMenuPlayChoiceWritesWhoStyleCharacterPromptToDescriptorOutput)
{
    TemporaryDirectory temp_directory;
    ScopedWorkingDirectory working_directory(temp_directory.path());
    ASSERT_EQ(mkdir("accounts", 0700), 0);
    ASSERT_EQ(mkdir("accounts/A-E", 0700), 0);

    account::AccountData stored_account;
    std::string error_message;
    ASSERT_TRUE(account::create_account(".", "acct", "player@example.com", "ValidPass1", 1700010200, &stored_account, &error_message)) << error_message;
    stored_account.characters = { "aragorn", "legolas" };
    stored_account.updated_at = 1700010201;
    ASSERT_TRUE(account::write_account_file(".", stored_account, &error_message)) << error_message;
    ASSERT_TRUE(account::write_account_character_file(".", "acct", make_stored_character("aragorn", 50, RACE_WOOD), &error_message)) << error_message;
    ASSERT_TRUE(account::write_account_character_file(".", "acct", make_stored_character("legolas", 45, RACE_HUMAN), &error_message)) << error_message;

    descriptor_data descriptor = make_descriptor();
    char choice[] = "2";

    nanny(&descriptor, choice);

    EXPECT_EQ(descriptor.connected, CON_ACCTSLCT);
    EXPECT_EQ(std::string(descriptor.output),
        "\n\rLinked characters for your account:\n\r"
        "1) [ 50 WdE] Aragorn     2) [ 45 Hum] Legolas     \n\r"
        "\n\r2 characters displayed.\n\r"
        "\n\r0) Back to Account Menu.\n\r"
        "\n\rCharacter number: ");
}

TEST(InterpreAccountMenu, AccountMenuPlayChoiceZeroReturnsToAccountMenu)
{
    TemporaryDirectory temp_directory;
    ScopedWorkingDirectory working_directory(temp_directory.path());
    ASSERT_EQ(mkdir("accounts", 0700), 0);
    ASSERT_EQ(mkdir("accounts/A-E", 0700), 0);

    account::AccountData stored_account;
    std::string error_message;
    ASSERT_TRUE(account::create_account(".", "acct", "player@example.com", "ValidPass1", 1700010200, &stored_account, &error_message)) << error_message;
    stored_account.characters = { "aragorn", "legolas" };
    stored_account.updated_at = 1700010201;
    ASSERT_TRUE(account::write_account_file(".", stored_account, &error_message)) << error_message;

    descriptor_data descriptor = make_descriptor();
    descriptor.connected = CON_ACCTSLCT;

    char choice[] = "0";
    nanny(&descriptor, choice);

    EXPECT_EQ(descriptor.connected, CON_ACCTMENU);
    EXPECT_EQ(std::string(descriptor.output),
        "\n\rAccount: player@example.com\n\r"
        "Linked characters: 2\n\r"
        "\n\r"
        "1) List linked characters\n\r"
        "2) Play a linked character\n\r"
        "3) Add an existing character\n\r"
        "4) Create a new character\n\r"
        "5) Reset account password\n\r"
        "0) Log out\n\r"
        "\n\r"
        "Choice: ");
}

TEST(InterpreAccountMenu, AccountMenuLinkChoiceUsesPlayerFacingSuccessMessage)
{
    TemporaryDirectory temp_directory;
    ScopedWorkingDirectory working_directory(temp_directory.path());
    ScopedPlayerTableReset player_table_reset;
    ASSERT_EQ(mkdir("accounts", 0700), 0);
    ASSERT_EQ(mkdir("accounts/A-E", 0700), 0);
    ASSERT_EQ(mkdir("players", 0700), 0);
    ASSERT_EQ(mkdir("players/A-E", 0700), 0);
    ASSERT_EQ(mkdir("players/F-J", 0700), 0);
    ASSERT_EQ(mkdir("players/K-O", 0700), 0);
    ASSERT_EQ(mkdir("players/P-T", 0700), 0);
    ASSERT_EQ(mkdir("players/U-Z", 0700), 0);
    ASSERT_EQ(mkdir("players/ZZZ", 0700), 0);
    ASSERT_EQ(mkdir("plrobjs", 0700), 0);
    ASSERT_EQ(mkdir("plrobjs/A-E", 0700), 0);
    ASSERT_EQ(mkdir("exploits", 0700), 0);
    ASSERT_EQ(mkdir("exploits/A-E", 0700), 0);

    account::AccountData stored_account;
    std::string error_message;
    ASSERT_TRUE(account::create_account(".", "acct", "player@example.com", "ValidPass1", 1700010200, &stored_account, &error_message)) << error_message;
    ASSERT_TRUE(account::admin_verify_email(".", "acct", "test", 1700010201, &stored_account, &error_message)) << error_message;

    char_file_u legacy_character = make_stored_character("aragorn", 50, RACE_WOOD);
    legacy_character.specials2.idnum = 4242;
    legacy_character.last_logon = 1700010202;
    const std::string legacy_player_path = write_valid_legacy_player_file(temp_directory.path(), legacy_character);
    const int legacy_player_index = create_entry(const_cast<char*>("aragorn"));
    ASSERT_GE(legacy_player_index, 0);
    std::snprintf(player_table[legacy_player_index].ch_file, sizeof(player_table[legacy_player_index].ch_file), "%s", legacy_player_path.c_str());
    player_table[legacy_player_index].level = legacy_character.level;
    player_table[legacy_player_index].race = legacy_character.race;
    player_table[legacy_player_index].idnum = legacy_character.specials2.idnum;
    player_table[legacy_player_index].log_time = legacy_character.last_logon;
    player_table[legacy_player_index].flags = legacy_character.specials2.act;

    descriptor_data descriptor = make_descriptor();

    char menu_choice[] = "3";
    nanny(&descriptor, menu_choice);
    ASSERT_EQ(descriptor.connected, CON_ACCTLINKNAME);

    descriptor.output[0] = '\0';
    descriptor.bufptr = 0;
    descriptor.bufspace = SMALL_BUFSIZE - 1;
    char name_choice[] = "aragorn";
    nanny(&descriptor, name_choice);
    ASSERT_EQ(descriptor.connected, CON_ACCTLEGPWD);

    descriptor.output[0] = '\0';
    descriptor.bufptr = 0;
    descriptor.bufspace = SMALL_BUFSIZE - 1;
    char password_choice[] = "LegacyPw1";
    nanny(&descriptor, password_choice);

    account::AccountData reloaded_account;
    ASSERT_TRUE(account::read_account_file(".", "acct", &reloaded_account, &error_message)) << error_message;

    EXPECT_EQ(descriptor.connected, CON_ACCTMENU);
    EXPECT_EQ(std::string(descriptor.output).find("Successfully added Aragorn to your account.\n\r"), 0u);
    EXPECT_EQ(std::string(descriptor.output).find("account storage"), std::string::npos);
    EXPECT_NE(std::string(descriptor.output).find("Choice: "), std::string::npos);
    EXPECT_TRUE(account::account_has_character(reloaded_account, "aragorn"));
    EXPECT_EQ(std::count(reloaded_account.characters.begin(), reloaded_account.characters.end(), "aragorn"), 1);
}

TEST(InterpreAccountMenu, InGameLinkChoiceUsesPlayerFacingSuccessMessage)
{
    TemporaryDirectory temp_directory;
    ScopedWorkingDirectory working_directory(temp_directory.path());
    ScopedPlayerTableReset player_table_reset;
    ASSERT_EQ(mkdir("accounts", 0700), 0);
    ASSERT_EQ(mkdir("accounts/A-E", 0700), 0);
    ASSERT_EQ(mkdir("players", 0700), 0);
    ASSERT_EQ(mkdir("players/A-E", 0700), 0);
    ASSERT_EQ(mkdir("players/F-J", 0700), 0);
    ASSERT_EQ(mkdir("players/K-O", 0700), 0);
    ASSERT_EQ(mkdir("players/P-T", 0700), 0);
    ASSERT_EQ(mkdir("players/U-Z", 0700), 0);
    ASSERT_EQ(mkdir("players/ZZZ", 0700), 0);
    ASSERT_EQ(mkdir("plrobjs", 0700), 0);
    ASSERT_EQ(mkdir("plrobjs/A-E", 0700), 0);
    ASSERT_EQ(mkdir("exploits", 0700), 0);
    ASSERT_EQ(mkdir("exploits/A-E", 0700), 0);

    account::AccountData stored_account;
    std::string error_message;
    ASSERT_TRUE(account::create_account(".", "acct", "player@example.com", "ValidPass1", 1700010200, &stored_account, &error_message)) << error_message;
    ASSERT_TRUE(account::admin_verify_email(".", "acct", "test", 1700010201, &stored_account, &error_message)) << error_message;

    char_file_u legacy_character = make_stored_character("aragorn", 50, RACE_WOOD);
    legacy_character.specials2.idnum = 4242;
    legacy_character.last_logon = 1700010202;
    const std::string legacy_player_path = write_valid_legacy_player_file(temp_directory.path(), legacy_character);
    const int legacy_player_index = create_entry(const_cast<char*>("aragorn"));
    ASSERT_GE(legacy_player_index, 0);
    std::snprintf(player_table[legacy_player_index].ch_file, sizeof(player_table[legacy_player_index].ch_file), "%s", legacy_player_path.c_str());
    player_table[legacy_player_index].level = legacy_character.level;
    player_table[legacy_player_index].race = legacy_character.race;
    player_table[legacy_player_index].idnum = legacy_character.specials2.idnum;
    player_table[legacy_player_index].log_time = legacy_character.last_logon;
    player_table[legacy_player_index].flags = legacy_character.specials2.act;

    descriptor_data descriptor = make_descriptor();
    descriptor.connected = CON_ACCTLINKPWD;
    descriptor.character = new char_data {};
    clear_char(descriptor.character, MOB_VOID);
    register_pc_char(descriptor.character);
    descriptor.character->desc = &descriptor;
    descriptor.character->player.name = strdup("aragorn");

    char password_choice[] = "ValidPass1";
    nanny(&descriptor, password_choice);

    account::AccountData reloaded_account;
    ASSERT_TRUE(account::read_account_file(".", "acct", &reloaded_account, &error_message)) << error_message;

    EXPECT_EQ(descriptor.connected, CON_PLYNG);
    EXPECT_EQ(std::string(descriptor.output), "Successfully added Aragorn to your account.\n\r");
    EXPECT_EQ(std::string(descriptor.output).find("account storage"), std::string::npos);
    EXPECT_TRUE(account::account_has_character(reloaded_account, "aragorn"));

    free_char(descriptor.character);
    descriptor.character = nullptr;
}

TEST(InterpreAccountMenu, AccountMenuNewCharacterConfirmationSkipsLegacyPasswordPrompt)
{
    TemporaryDirectory temp_directory;
    ScopedWorkingDirectory working_directory(temp_directory.path());
    ScopedPlayerTableReset player_table_reset;
    ASSERT_EQ(mkdir("accounts", 0700), 0);
    ASSERT_EQ(mkdir("accounts/A-E", 0700), 0);
    ASSERT_EQ(mkdir("players", 0700), 0);
    ASSERT_EQ(mkdir("players/A-E", 0700), 0);
    ASSERT_EQ(mkdir("players/F-J", 0700), 0);
    ASSERT_EQ(mkdir("players/K-O", 0700), 0);
    ASSERT_EQ(mkdir("players/P-T", 0700), 0);
    ASSERT_EQ(mkdir("players/U-Z", 0700), 0);
    ASSERT_EQ(mkdir("players/ZZZ", 0700), 0);

    account::AccountData stored_account;
    std::string error_message;
    ASSERT_TRUE(account::create_account(".", "acct", "player@example.com", "ValidPass1", 1700010200, &stored_account, &error_message)) << error_message;

    descriptor_data descriptor = make_descriptor();
    descriptor.character = new char_data {};
    clear_char(descriptor.character, MOB_VOID);
    register_pc_char(descriptor.character);
    descriptor.character->desc = &descriptor;
    char create_choice[] = "4";
    nanny(&descriptor, create_choice);
    ASSERT_EQ(descriptor.connected, CON_ACCTNEWCHAR);

    descriptor.output[0] = '\0';
    descriptor.bufptr = 0;
    descriptor.bufspace = SMALL_BUFSIZE - 1;
    char new_name[] = "aragorn";
    nanny(&descriptor, new_name);
    ASSERT_EQ(descriptor.connected, CON_NMECNF);

    descriptor.output[0] = '\0';
    descriptor.bufptr = 0;
    descriptor.bufspace = SMALL_BUFSIZE - 1;
    char confirm[] = "Y";
    nanny(&descriptor, confirm);

    EXPECT_EQ(descriptor.connected, CON_QSEX);
    EXPECT_STREQ(descriptor.pwd, "*ACCOUNT*");
    EXPECT_NE(std::string(descriptor.output).find("What is your sex (M/F)? "), std::string::npos);
    EXPECT_EQ(std::string(descriptor.output).find("Please enter a password"), std::string::npos);

    free_char(descriptor.character);
    descriptor.character = nullptr;
}

TEST(InterpreAccountMenu, CharacterMenuPasswordOptionRoutesAccountBackedCharactersToAccountResetFlow)
{
    descriptor_data descriptor = make_descriptor();
    descriptor.connected = CON_SLCT;
    std::snprintf(descriptor.account_name, sizeof(descriptor.account_name), "%s", "acct");
    std::snprintf(descriptor.pwd, sizeof(descriptor.pwd), "%s", "*ACCOUNT*");

    char choice[] = "4";
    nanny(&descriptor, choice);

    EXPECT_EQ(descriptor.connected, CON_ACCTRESETOLD);
    EXPECT_EQ(std::string(descriptor.output), "Current account password: ");
}

TEST(InterpreAccountMenu, CharacterMenuDeleteOptionRoutesAccountBackedCharactersToAccountPasswordVerification)
{
    descriptor_data descriptor = make_descriptor();
    descriptor.connected = CON_SLCT;
    std::snprintf(descriptor.account_name, sizeof(descriptor.account_name), "%s", "acct");
    std::snprintf(descriptor.pwd, sizeof(descriptor.pwd), "%s", "*ACCOUNT*");
    descriptor.character = new char_data {};
    clear_char(descriptor.character, MOB_VOID);
    register_pc_char(descriptor.character);
    descriptor.character->player.level = 10;

    char choice[] = "5";
    nanny(&descriptor, choice);

    EXPECT_EQ(descriptor.connected, CON_ACCTDELCNF1);
    EXPECT_EQ(std::string(descriptor.output), "\n\rEnter your account password for verification: ");

    free_char(descriptor.character);
    descriptor.character = nullptr;
}

TEST(InterpreAccountMenu, AccountBackedDeleteVerificationRejectsIncorrectAccountPasswordAndReturnsToCharacterMenu)
{
    TemporaryDirectory temp_directory;
    ScopedWorkingDirectory working_directory(temp_directory.path());
    ASSERT_EQ(mkdir("accounts", 0700), 0);
    ASSERT_EQ(mkdir("accounts/A-E", 0700), 0);

    account::AccountData stored_account;
    std::string error_message;
    ASSERT_TRUE(account::create_account(".", "acct", "player@example.com", "ValidPass1", 1700010200, &stored_account, &error_message)) << error_message;
    ASSERT_TRUE(account::admin_verify_email(".", "acct", "test", 1700010201, &stored_account, &error_message)) << error_message;

    descriptor_data descriptor = make_descriptor();
    descriptor.connected = CON_ACCTDELCNF1;
    std::snprintf(descriptor.account_name, sizeof(descriptor.account_name), "%s", "acct");
    descriptor.character = new char_data {};
    clear_char(descriptor.character, MOB_VOID);
    register_pc_char(descriptor.character);
    descriptor.character->player.level = 10;

    char password[] = "WrongPass1";
    nanny(&descriptor, password);

    EXPECT_EQ(descriptor.connected, CON_SLCT);
    EXPECT_NE(std::string(descriptor.output).find("Incorrect account password.\n\r"), std::string::npos);
    EXPECT_NE(std::string(descriptor.output).find("0) Back to Account Menu.\n\r"), std::string::npos);

    free_char(descriptor.character);
    descriptor.character = nullptr;
}

TEST(InterpreAccountMenu, AccountBackedDeleteVerificationAcceptsCorrectAccountPassword)
{
    TemporaryDirectory temp_directory;
    ScopedWorkingDirectory working_directory(temp_directory.path());
    ASSERT_EQ(mkdir("accounts", 0700), 0);
    ASSERT_EQ(mkdir("accounts/A-E", 0700), 0);

    account::AccountData stored_account;
    std::string error_message;
    ASSERT_TRUE(account::create_account(".", "acct", "player@example.com", "ValidPass1", 1700010200, &stored_account, &error_message)) << error_message;
    ASSERT_TRUE(account::admin_verify_email(".", "acct", "test", 1700010201, &stored_account, &error_message)) << error_message;

    descriptor_data descriptor = make_descriptor();
    descriptor.connected = CON_ACCTDELCNF1;
    std::snprintf(descriptor.account_name, sizeof(descriptor.account_name), "%s", "acct");
    std::snprintf(descriptor.pwd, sizeof(descriptor.pwd), "%s", "WrongPwd");
    descriptor.character = new char_data {};
    clear_char(descriptor.character, MOB_VOID);
    register_pc_char(descriptor.character);
    descriptor.character->player.level = 10;
    descriptor.character->player.name = strdup("aragorn");

    char password[] = "ValidPass1";
    nanny(&descriptor, password);

    EXPECT_EQ(descriptor.connected, CON_DELCNF2);
    EXPECT_NE(std::string(descriptor.output).find("YOU ARE ABOUT TO DELETE THIS CHARACTER PERMANENTLY."), std::string::npos);

    free_char(descriptor.character);
    descriptor.character = nullptr;
}

TEST(InterpreAccountMenu, ConfirmedAccountBackedDeleteReturnsToUsableAccountMenuAndRemovesAccountNativeCharacterData)
{
    TemporaryDirectory temp_directory;
    ScopedWorkingDirectory working_directory(temp_directory.path());
    ScopedPlayerTableReset player_table_reset;
    ASSERT_EQ(mkdir("accounts", 0700), 0);
    ASSERT_EQ(mkdir("accounts/A-E", 0700), 0);

    account::AccountData stored_account;
    std::string error_message;
    ASSERT_TRUE(account::create_account(".", "acct", "player@example.com", "ValidPass1", 1700010200, &stored_account, &error_message)) << error_message;
    ASSERT_TRUE(account::admin_verify_email(".", "acct", "test", 1700010201, &stored_account, &error_message)) << error_message;

    char_file_u stored_character = make_stored_character("aragorn", 10, RACE_HUMAN);
    stored_character.specials2.idnum = 4242;
    stored_character.player_index = -1;
    ASSERT_TRUE(account::write_account_character_file(".", "acct", stored_character, &error_message)) << error_message;
    ASSERT_TRUE(account::write_default_account_object_file(".", "acct", "aragorn", &error_message)) << error_message;
    ASSERT_TRUE(account::write_default_account_exploit_file(".", "acct", "aragorn", &error_message)) << error_message;
    ASSERT_TRUE(account::admin_link_character(".", "acct", "aragorn", 1700010202, &stored_account, &error_message)) << error_message;

    descriptor_data descriptor = make_descriptor();
    descriptor.connected = CON_SLCT;
    std::snprintf(descriptor.account_name, sizeof(descriptor.account_name), "%s", "acct");
    std::snprintf(descriptor.pwd, sizeof(descriptor.pwd), "%s", "WrongPwd");
    descriptor.character = new char_data {};
    clear_char(descriptor.character, MOB_VOID);
    register_pc_char(descriptor.character);
    descriptor.character->player.level = 10;
    descriptor.character->player.name = strdup("aragorn");
    descriptor.pos = create_entry(descriptor.character->player.name);
    ASSERT_GE(descriptor.pos, 0);
    const int deleted_player_index = descriptor.pos;
    player_table[descriptor.pos].flags = 0;
    std::snprintf(player_table[descriptor.pos].ch_file, sizeof(player_table[descriptor.pos].ch_file), "%s", "accounts/A-E/player@example.com/aragorn.character.json");

    char delete_choice[] = "5";
    nanny(&descriptor, delete_choice);
    ASSERT_EQ(descriptor.connected, CON_ACCTDELCNF1);

    descriptor.output[0] = '\0';
    descriptor.bufptr = 0;
    descriptor.bufspace = SMALL_BUFSIZE - 1;
    char password[] = "ValidPass1";
    nanny(&descriptor, password);
    ASSERT_EQ(descriptor.connected, CON_DELCNF2);

    descriptor.output[0] = '\0';
    descriptor.bufptr = 0;
    descriptor.bufspace = SMALL_BUFSIZE - 1;
    char confirm[] = "yes";
    nanny(&descriptor, confirm);

    struct stat file_info {};
    account::AccountData reloaded_account;
    ASSERT_TRUE(account::read_account_file(".", "acct", &reloaded_account, &error_message)) << error_message;

    EXPECT_EQ(descriptor.connected, CON_ACCTMENU);
    EXPECT_NE(std::string(descriptor.output).find("Character 'aragorn' deleted!"), std::string::npos);
    EXPECT_NE(std::string(descriptor.output).find("Account: player@example.com\n\r"), std::string::npos);
    EXPECT_NE(std::string(descriptor.output).find("Linked characters: 0\n\r"), std::string::npos);
    EXPECT_NE(std::string(descriptor.output).find("Choice: "), std::string::npos);
    EXPECT_EQ(descriptor.character, nullptr);
    EXPECT_EQ(descriptor.pos, -1);
    EXPECT_FALSE(account::account_has_character(reloaded_account, "aragorn"));
    EXPECT_TRUE(reloaded_account.character_links.empty());
    EXPECT_TRUE(reloaded_account.characters.empty());
    EXPECT_NE(stat(account::account_character_player_path(".", "acct", "aragorn").c_str(), &file_info), 0);
    EXPECT_NE(stat(account::account_character_object_path(".", "acct", "aragorn").c_str(), &file_info), 0);
    EXPECT_NE(stat(account::account_character_exploits_path(".", "acct", "aragorn").c_str(), &file_info), 0);
    EXPECT_TRUE(IS_SET(player_table[deleted_player_index].flags, PLR_DELETED));
    EXPECT_EQ(player_table[deleted_player_index].ch_file[0], '\0');

    descriptor.output[0] = '\0';
    descriptor.bufptr = 0;
    descriptor.bufspace = SMALL_BUFSIZE - 1;
    char list_choice[] = "1";
    nanny(&descriptor, list_choice);

    EXPECT_EQ(descriptor.connected, CON_ACCTMENU);
    EXPECT_EQ(std::string(descriptor.output),
        "\n\rNo linked characters yet.\n\r"
        "\n\rAccount: player@example.com\n\r"
        "Linked characters: 0\n\r"
        "\n\r"
        "1) List linked characters\n\r"
        "2) Play a linked character\n\r"
        "3) Add an existing character\n\r"
        "4) Create a new character\n\r"
        "5) Reset account password\n\r"
        "0) Log out\n\r"
        "\n\r"
        "Choice: ");
}

TEST(InterpreAccountMenu, SelectingAnotherLinkedCharacterAfterDeleteRecreatesDescriptorCharacter)
{
    TemporaryDirectory temp_directory;
    ScopedWorkingDirectory working_directory(temp_directory.path());
    ScopedPlayerTableReset player_table_reset;
    ASSERT_EQ(mkdir("accounts", 0700), 0);
    ASSERT_EQ(mkdir("accounts/A-E", 0700), 0);
    static char test_motd[] = "Test MOTD\r\n";
    ScopedMotdOverride motd_override(test_motd);

    account::AccountData stored_account;
    std::string error_message;
    ASSERT_TRUE(account::create_account(".", "acct", "player@example.com", "ValidPass1", 1700010200, &stored_account, &error_message)) << error_message;
    ASSERT_TRUE(account::admin_verify_email(".", "acct", "test", 1700010201, &stored_account, &error_message)) << error_message;

    char_file_u aragorn = make_stored_character("aragorn", 10, RACE_WOOD);
    aragorn.specials2.idnum = 4242;
    aragorn.player_index = -1;
    aragorn.specials2.load_room = 3001;
    ASSERT_TRUE(account::write_account_character_file(".", "acct", aragorn, &error_message)) << error_message;
    ASSERT_TRUE(account::write_default_account_object_file(".", "acct", "aragorn", &error_message)) << error_message;
    ASSERT_TRUE(account::write_default_account_exploit_file(".", "acct", "aragorn", &error_message)) << error_message;
    ASSERT_TRUE(account::admin_link_character(".", "acct", "aragorn", 1700010202, &stored_account, &error_message)) << error_message;

    char_file_u legolas = make_stored_character("legolas", 45, RACE_HUMAN);
    legolas.specials2.idnum = 4343;
    legolas.player_index = -1;
    legolas.specials2.load_room = 3002;
    ASSERT_TRUE(account::write_account_character_file(".", "acct", legolas, &error_message)) << error_message;
    ASSERT_TRUE(account::write_default_account_object_file(".", "acct", "legolas", &error_message)) << error_message;
    ASSERT_TRUE(account::write_default_account_exploit_file(".", "acct", "legolas", &error_message)) << error_message;
    ASSERT_TRUE(account::admin_link_character(".", "acct", "legolas", 1700010203, &stored_account, &error_message)) << error_message;

    descriptor_data descriptor = make_descriptor();
    descriptor.connected = CON_SLCT;
    std::snprintf(descriptor.account_name, sizeof(descriptor.account_name), "%s", "acct");
    std::snprintf(descriptor.pwd, sizeof(descriptor.pwd), "%s", "WrongPwd");
    std::snprintf(descriptor.host, sizeof(descriptor.host), "%s", "127.0.0.1");
    descriptor.character = new char_data {};
    clear_char(descriptor.character, MOB_VOID);
    register_pc_char(descriptor.character);
    descriptor.character->desc = &descriptor;
    descriptor.character->player.level = 10;
    descriptor.character->player.name = strdup("aragorn");
    descriptor.pos = create_entry(descriptor.character->player.name);
    ASSERT_GE(descriptor.pos, 0);
    player_table[descriptor.pos].flags = 0;
    std::snprintf(player_table[descriptor.pos].ch_file, sizeof(player_table[descriptor.pos].ch_file), "%s", "accounts/A-E/player@example.com/aragorn.character.json");

    char delete_choice[] = "5";
    nanny(&descriptor, delete_choice);
    ASSERT_EQ(descriptor.connected, CON_ACCTDELCNF1);

    descriptor.output[0] = '\0';
    descriptor.bufptr = 0;
    descriptor.bufspace = SMALL_BUFSIZE - 1;
    char password[] = "ValidPass1";
    nanny(&descriptor, password);
    ASSERT_EQ(descriptor.connected, CON_DELCNF2);

    descriptor.output[0] = '\0';
    descriptor.bufptr = 0;
    descriptor.bufspace = SMALL_BUFSIZE - 1;
    char confirm[] = "yes";
    nanny(&descriptor, confirm);
    ASSERT_EQ(descriptor.connected, CON_ACCTMENU);
    ASSERT_EQ(descriptor.character, nullptr);
    ASSERT_EQ(descriptor.pos, -1);

    descriptor.output[0] = '\0';
    descriptor.bufptr = 0;
    descriptor.bufspace = SMALL_BUFSIZE - 1;
    char play_choice[] = "2";
    nanny(&descriptor, play_choice);
    ASSERT_EQ(descriptor.connected, CON_ACCTSLCT);
    EXPECT_NE(std::string(descriptor.output).find("1) [ 45 Hum] Legolas"), std::string::npos);
    EXPECT_EQ(std::string(descriptor.output).find("Aragorn"), std::string::npos);

    descriptor.output[0] = '\0';
    descriptor.bufptr = 0;
    descriptor.bufspace = SMALL_BUFSIZE - 1;
    char select_choice[] = "1";
    nanny(&descriptor, select_choice);

    EXPECT_EQ(descriptor.connected, CON_SLCT);
    ASSERT_NE(descriptor.character, nullptr);
    ASSERT_NE(descriptor.character->player.name, nullptr);
    EXPECT_STREQ(descriptor.character->player.name, "legolas");
    EXPECT_EQ(descriptor.character->desc, &descriptor);
    EXPECT_EQ(descriptor.pos, descriptor.character->player_index);
    EXPECT_STREQ(descriptor.account_name, "acct");
    EXPECT_NE(std::string(descriptor.output).find("0) Back to Account Menu.\n\r"), std::string::npos);

    free_char(descriptor.character);
    descriptor.character = nullptr;
}

TEST(InterpreAccountMenu, FailedSelectionAfterDeleteDoesNotLeaveReplacementDescriptorCharacterBehind)
{
    TemporaryDirectory temp_directory;
    ScopedWorkingDirectory working_directory(temp_directory.path());
    ScopedPlayerTableReset player_table_reset;
    ASSERT_EQ(mkdir("accounts", 0700), 0);
    ASSERT_EQ(mkdir("accounts/A-E", 0700), 0);
    static char test_motd[] = "Test MOTD\r\n";
    ScopedMotdOverride motd_override(test_motd);

    account::AccountData stored_account;
    std::string error_message;
    ASSERT_TRUE(account::create_account(".", "acct", "player@example.com", "ValidPass1", 1700010200, &stored_account, &error_message)) << error_message;
    ASSERT_TRUE(account::admin_verify_email(".", "acct", "test", 1700010201, &stored_account, &error_message)) << error_message;

    char_file_u aragorn = make_stored_character("aragorn", 10, RACE_WOOD);
    aragorn.specials2.idnum = 4242;
    aragorn.player_index = -1;
    aragorn.specials2.load_room = 3001;
    ASSERT_TRUE(account::write_account_character_file(".", "acct", aragorn, &error_message)) << error_message;
    ASSERT_TRUE(account::write_default_account_object_file(".", "acct", "aragorn", &error_message)) << error_message;
    ASSERT_TRUE(account::write_default_account_exploit_file(".", "acct", "aragorn", &error_message)) << error_message;
    ASSERT_TRUE(account::admin_link_character(".", "acct", "aragorn", 1700010202, &stored_account, &error_message)) << error_message;

    char_file_u legolas = make_stored_character("legolas", 45, RACE_HUMAN);
    legolas.specials2.idnum = 4343;
    legolas.player_index = -1;
    legolas.specials2.load_room = 3002;
    SET_BIT(legolas.specials2.act, PLR_DELETED);
    ASSERT_TRUE(account::write_account_character_file(".", "acct", legolas, &error_message)) << error_message;
    ASSERT_TRUE(account::admin_link_character(".", "acct", "legolas", 1700010203, &stored_account, &error_message)) << error_message;

    descriptor_data descriptor = make_descriptor();
    descriptor.connected = CON_SLCT;
    std::snprintf(descriptor.account_name, sizeof(descriptor.account_name), "%s", "acct");
    std::snprintf(descriptor.pwd, sizeof(descriptor.pwd), "%s", "WrongPwd");
    std::snprintf(descriptor.host, sizeof(descriptor.host), "%s", "127.0.0.1");
    descriptor.character = new char_data {};
    clear_char(descriptor.character, MOB_VOID);
    register_pc_char(descriptor.character);
    descriptor.character->desc = &descriptor;
    descriptor.character->player.level = 10;
    descriptor.character->player.name = strdup("aragorn");
    descriptor.pos = create_entry(descriptor.character->player.name);
    ASSERT_GE(descriptor.pos, 0);
    player_table[descriptor.pos].flags = 0;
    std::snprintf(player_table[descriptor.pos].ch_file, sizeof(player_table[descriptor.pos].ch_file), "%s", "accounts/A-E/player@example.com/aragorn.character.json");

    char delete_choice[] = "5";
    nanny(&descriptor, delete_choice);
    ASSERT_EQ(descriptor.connected, CON_ACCTDELCNF1);

    descriptor.output[0] = '\0';
    descriptor.bufptr = 0;
    descriptor.bufspace = SMALL_BUFSIZE - 1;
    char password[] = "ValidPass1";
    nanny(&descriptor, password);
    ASSERT_EQ(descriptor.connected, CON_DELCNF2);

    descriptor.output[0] = '\0';
    descriptor.bufptr = 0;
    descriptor.bufspace = SMALL_BUFSIZE - 1;
    char confirm[] = "yes";
    nanny(&descriptor, confirm);
    ASSERT_EQ(descriptor.connected, CON_ACCTMENU);
    ASSERT_EQ(descriptor.character, nullptr);
    ASSERT_EQ(descriptor.pos, -1);

    descriptor.output[0] = '\0';
    descriptor.bufptr = 0;
    descriptor.bufspace = SMALL_BUFSIZE - 1;
    char play_choice[] = "2";
    nanny(&descriptor, play_choice);
    ASSERT_EQ(descriptor.connected, CON_ACCTSLCT);

    descriptor.output[0] = '\0';
    descriptor.bufptr = 0;
    descriptor.bufspace = SMALL_BUFSIZE - 1;
    char select_choice[] = "1";
    nanny(&descriptor, select_choice);

    EXPECT_EQ(descriptor.connected, CON_ACCTSLCT);
    EXPECT_EQ(descriptor.character, nullptr);
    EXPECT_EQ(descriptor.pos, -1);
    EXPECT_NE(std::string(descriptor.output).find("deleted and cannot be selected"), std::string::npos);
    EXPECT_NE(std::string(descriptor.output).find("Character number: "), std::string::npos);
}

TEST(InterpreAccountMenu, CreatingNewCharacterAfterDeleteRecreatesDescriptorCharacterShell)
{
    TemporaryDirectory temp_directory;
    ScopedWorkingDirectory working_directory(temp_directory.path());
    ScopedPlayerTableReset player_table_reset;
    ASSERT_EQ(mkdir("accounts", 0700), 0);
    ASSERT_EQ(mkdir("accounts/A-E", 0700), 0);

    account::AccountData stored_account;
    std::string error_message;
    ASSERT_TRUE(account::create_account(".", "acct", "player@example.com", "ValidPass1", 1700010200, &stored_account, &error_message)) << error_message;
    ASSERT_TRUE(account::admin_verify_email(".", "acct", "test", 1700010201, &stored_account, &error_message)) << error_message;

    char_file_u aragorn = make_stored_character("aragorn", 10, RACE_WOOD);
    aragorn.specials2.idnum = 4242;
    aragorn.player_index = -1;
    ASSERT_TRUE(account::write_account_character_file(".", "acct", aragorn, &error_message)) << error_message;
    ASSERT_TRUE(account::write_default_account_object_file(".", "acct", "aragorn", &error_message)) << error_message;
    ASSERT_TRUE(account::write_default_account_exploit_file(".", "acct", "aragorn", &error_message)) << error_message;
    ASSERT_TRUE(account::admin_link_character(".", "acct", "aragorn", 1700010202, &stored_account, &error_message)) << error_message;

    descriptor_data descriptor = make_descriptor();
    descriptor.connected = CON_SLCT;
    std::snprintf(descriptor.account_name, sizeof(descriptor.account_name), "%s", "acct");
    std::snprintf(descriptor.pwd, sizeof(descriptor.pwd), "%s", "WrongPwd");
    descriptor.character = new char_data {};
    clear_char(descriptor.character, MOB_VOID);
    register_pc_char(descriptor.character);
    descriptor.character->desc = &descriptor;
    descriptor.character->player.level = 10;
    descriptor.character->player.name = strdup("aragorn");
    descriptor.pos = create_entry(descriptor.character->player.name);
    ASSERT_GE(descriptor.pos, 0);
    player_table[descriptor.pos].flags = 0;
    std::snprintf(player_table[descriptor.pos].ch_file, sizeof(player_table[descriptor.pos].ch_file), "%s", "accounts/A-E/player@example.com/aragorn.character.json");

    char delete_choice[] = "5";
    nanny(&descriptor, delete_choice);
    ASSERT_EQ(descriptor.connected, CON_ACCTDELCNF1);

    descriptor.output[0] = '\0';
    descriptor.bufptr = 0;
    descriptor.bufspace = SMALL_BUFSIZE - 1;
    char password[] = "ValidPass1";
    nanny(&descriptor, password);
    ASSERT_EQ(descriptor.connected, CON_DELCNF2);

    descriptor.output[0] = '\0';
    descriptor.bufptr = 0;
    descriptor.bufspace = SMALL_BUFSIZE - 1;
    char confirm[] = "yes";
    nanny(&descriptor, confirm);
    ASSERT_EQ(descriptor.connected, CON_ACCTMENU);
    ASSERT_EQ(descriptor.character, nullptr);
    ASSERT_EQ(descriptor.pos, -1);

    descriptor.output[0] = '\0';
    descriptor.bufptr = 0;
    descriptor.bufspace = SMALL_BUFSIZE - 1;
    char create_choice[] = "4";
    nanny(&descriptor, create_choice);
    ASSERT_EQ(descriptor.connected, CON_ACCTNEWCHAR);

    descriptor.output[0] = '\0';
    descriptor.bufptr = 0;
    descriptor.bufspace = SMALL_BUFSIZE - 1;
    char name_choice[] = "legolas";
    nanny(&descriptor, name_choice);

    EXPECT_EQ(descriptor.connected, CON_NMECNF);
    ASSERT_NE(descriptor.character, nullptr);
    ASSERT_NE(descriptor.character->player.name, nullptr);
    EXPECT_STREQ(descriptor.character->player.name, "Legolas");
    EXPECT_EQ(descriptor.character->desc, &descriptor);
    EXPECT_EQ(descriptor.pos, -1);
    EXPECT_STREQ(descriptor.account_name, "acct");

    free_char(descriptor.character);
    descriptor.character = nullptr;
}

TEST(InterpreAccountMenu, AccountBackedCharacterMenuChoiceZeroReturnsToAccountMenu)
{
    TemporaryDirectory temp_directory;
    ScopedWorkingDirectory working_directory(temp_directory.path());
    ASSERT_EQ(mkdir("accounts", 0700), 0);
    ASSERT_EQ(mkdir("accounts/A-E", 0700), 0);

    account::AccountData stored_account;
    std::string error_message;
    ASSERT_TRUE(account::create_account(".", "acct", "player@example.com", "ValidPass1", 1700010200, &stored_account, &error_message)) << error_message;

    descriptor_data descriptor = make_descriptor();
    descriptor.connected = CON_SLCT;
    char choice[] = "0";

    nanny(&descriptor, choice);

    EXPECT_EQ(descriptor.connected, CON_ACCTMENU);
    EXPECT_EQ(std::string(descriptor.output),
        "\n\rAccount: player@example.com\n\r"
        "Linked characters: 0\n\r"
        "\n\r"
        "1) List linked characters\n\r"
        "2) Play a linked character\n\r"
        "3) Add an existing character\n\r"
        "4) Create a new character\n\r"
        "5) Reset account password\n\r"
        "0) Log out\n\r"
        "\n\r"
        "Choice: ");
}

TEST(InterpreAccountMenu, AccountBackedCharacterMenuUsesBackToAccountMenuLabel)
{
    descriptor_data descriptor = make_descriptor();
    descriptor.connected = CON_SLCT;
    std::snprintf(descriptor.account_name, sizeof(descriptor.account_name), "%s", "acct");

    char invalid_choice[] = "x";
    nanny(&descriptor, invalid_choice);

    const std::string output = descriptor.output;
    EXPECT_NE(output.find("0) Back to Account Menu.\n\r"), std::string::npos);
    EXPECT_EQ(output.find("0) Exit from the MUD.\n\r"), std::string::npos);
}

TEST(InterpreAccountMenu, ExtractCharReturnsAccountBackedCharactersToAccountAwareMenu)
{
    TemporaryDirectory temp_directory;
    ScopedWorkingDirectory working_directory(temp_directory.path());
    ScopedPlayerTableReset player_table_reset;
    ASSERT_EQ(mkdir("players", 0700), 0);
    ASSERT_EQ(mkdir("players/A-E", 0700), 0);

    descriptor_data descriptor = make_descriptor();
    descriptor.connected = CON_PLYNG;
    descriptor.descriptor = 1;

    char_data* character = new char_data {};
    clear_char(character, MOB_VOID);
    register_pc_char(character);
    character->desc = &descriptor;
    descriptor.character = character;
    character->next = nullptr;
    character_list = character;

    character->player.name = strdup("aragorn");
    character->specials2.idnum = 1234;
    character->player.level = 10;
    character->player.race = RACE_HUMAN;
    descriptor.pos = create_entry(character->player.name);

    extract_char(character);

    EXPECT_EQ(descriptor.connected, CON_SLCT);
    EXPECT_NE(std::string(descriptor.output).find("0) Back to Account Menu.\n\r"), std::string::npos);
    EXPECT_EQ(std::string(descriptor.output).find("0) Exit from the MUD.\n\r"), std::string::npos);

    character_list = nullptr;
    free_char(character);
    descriptor.character = nullptr;
}

TEST(InterpreAccountMenu, AccountSelectionKeepsAccountSessionForCharacterMenuOptions)
{
    TemporaryDirectory temp_directory;
    ScopedWorkingDirectory working_directory(temp_directory.path());
    ScopedPlayerTableReset player_table_reset;
    ASSERT_EQ(mkdir("accounts", 0700), 0);
    ASSERT_EQ(mkdir("accounts/A-E", 0700), 0);
    static char test_motd[] = "Test MOTD\r\n";
    ScopedMotdOverride motd_override(test_motd);

    std::string error_message;
    account::AccountData stored_account;
    ASSERT_TRUE(account::create_account(".", "acct", "player@example.com", "ValidPass1", 1700010200, &stored_account, &error_message)) << error_message;

    char_file_u stored_character = make_stored_character("aragorn", 50, RACE_WOOD);
    stored_character.player_index = -1;
    stored_character.specials2.load_room = 3001;
    ASSERT_TRUE(account::write_account_character_file(".", "acct", stored_character, &error_message)) << error_message;
    ASSERT_TRUE(account::write_default_account_object_file(".", "acct", "aragorn", &error_message)) << error_message;
    ASSERT_TRUE(account::admin_link_character(".", "acct", "aragorn", 1700010201, &stored_account, &error_message)) << error_message;

    descriptor_data descriptor = make_descriptor();
    descriptor.connected = CON_ACCTSLCT;
    descriptor.character = new char_data {};
    clear_char(descriptor.character, MOB_VOID);
    register_pc_char(descriptor.character);
    descriptor.character->desc = &descriptor;
    std::snprintf(descriptor.host, sizeof(descriptor.host), "%s", "127.0.0.1");

    char choice[] = "1";
    nanny(&descriptor, choice);

    EXPECT_EQ(descriptor.connected, CON_SLCT);
    EXPECT_STREQ(descriptor.account_name, "acct");
    EXPECT_NE(std::string(descriptor.output).find("0) Back to Account Menu.\n\r"), std::string::npos);

    descriptor.output[0] = '\0';
    descriptor.bufptr = 0;
    descriptor.bufspace = SMALL_BUFSIZE - 1;
    char password_choice[] = "4";
    nanny(&descriptor, password_choice);

    EXPECT_EQ(descriptor.connected, CON_ACCTRESETOLD);
    EXPECT_EQ(std::string(descriptor.output), "Current account password: ");

    free_char(descriptor.character);
    descriptor.character = nullptr;
}

TEST(InterpreAccountMenu, AccountSelectionLoadsTheSecondNumberedLinkedCharacter)
{
    TemporaryDirectory temp_directory;
    ScopedWorkingDirectory working_directory(temp_directory.path());
    ScopedPlayerTableReset player_table_reset;
    ASSERT_EQ(mkdir("accounts", 0700), 0);
    ASSERT_EQ(mkdir("accounts/A-E", 0700), 0);
    static char test_motd[] = "Test MOTD\r\n";
    ScopedMotdOverride motd_override(test_motd);

    std::string error_message;
    account::AccountData stored_account;
    ASSERT_TRUE(account::create_account(".", "acct", "player@example.com", "ValidPass1", 1700010200, &stored_account, &error_message)) << error_message;

    char_file_u aragorn = make_stored_character("aragorn", 50, RACE_WOOD);
    aragorn.player_index = -1;
    aragorn.specials2.load_room = 3001;
    ASSERT_TRUE(account::write_account_character_file(".", "acct", aragorn, &error_message)) << error_message;
    ASSERT_TRUE(account::write_default_account_object_file(".", "acct", "aragorn", &error_message)) << error_message;
    ASSERT_TRUE(account::admin_link_character(".", "acct", "aragorn", 1700010201, &stored_account, &error_message)) << error_message;

    char_file_u legolas = make_stored_character("legolas", 45, RACE_HUMAN);
    legolas.player_index = -1;
    legolas.specials2.load_room = 3002;
    ASSERT_TRUE(account::write_account_character_file(".", "acct", legolas, &error_message)) << error_message;
    ASSERT_TRUE(account::write_default_account_object_file(".", "acct", "legolas", &error_message)) << error_message;
    ASSERT_TRUE(account::admin_link_character(".", "acct", "legolas", 1700010202, &stored_account, &error_message)) << error_message;

    descriptor_data descriptor = make_descriptor();
    descriptor.connected = CON_ACCTSLCT;
    descriptor.character = new char_data {};
    clear_char(descriptor.character, MOB_VOID);
    register_pc_char(descriptor.character);
    descriptor.character->desc = &descriptor;
    std::snprintf(descriptor.host, sizeof(descriptor.host), "%s", "127.0.0.1");

    char choice[] = "2";
    nanny(&descriptor, choice);

    EXPECT_EQ(descriptor.connected, CON_SLCT);
    ASSERT_NE(descriptor.character, nullptr);
    ASSERT_NE(descriptor.character->player.name, nullptr);
    EXPECT_STREQ(descriptor.character->player.name, "legolas");
    EXPECT_NE(std::string(descriptor.output).find("0) Back to Account Menu.\n\r"), std::string::npos);

    free_char(descriptor.character);
    descriptor.character = nullptr;
}

TEST(InterpreAccountMenu, AccountSelectionKeepsBackToAccountMenuLabelWhenMenuRerenders)
{
    TemporaryDirectory temp_directory;
    ScopedWorkingDirectory working_directory(temp_directory.path());
    ScopedPlayerTableReset player_table_reset;
    ASSERT_EQ(mkdir("accounts", 0700), 0);
    ASSERT_EQ(mkdir("accounts/A-E", 0700), 0);
    static char test_motd[] = "Test MOTD\r\n";
    ScopedMotdOverride motd_override(test_motd);

    std::string error_message;
    account::AccountData stored_account;
    ASSERT_TRUE(account::create_account(".", "acct", "player@example.com", "ValidPass1", 1700010200, &stored_account, &error_message)) << error_message;

    char_file_u stored_character = make_stored_character("aragorn", 50, RACE_WOOD);
    stored_character.player_index = -1;
    stored_character.specials2.load_room = 3001;
    ASSERT_TRUE(account::write_account_character_file(".", "acct", stored_character, &error_message)) << error_message;
    ASSERT_TRUE(account::write_default_account_object_file(".", "acct", "aragorn", &error_message)) << error_message;
    ASSERT_TRUE(account::admin_link_character(".", "acct", "aragorn", 1700010201, &stored_account, &error_message)) << error_message;

    descriptor_data descriptor = make_descriptor();
    descriptor.connected = CON_ACCTSLCT;
    descriptor.character = new char_data {};
    clear_char(descriptor.character, MOB_VOID);
    register_pc_char(descriptor.character);
    descriptor.character->desc = &descriptor;
    std::snprintf(descriptor.host, sizeof(descriptor.host), "%s", "127.0.0.1");

    char character_choice[] = "1";
    nanny(&descriptor, character_choice);

    ASSERT_EQ(descriptor.connected, CON_SLCT);
    descriptor.output[0] = '\0';
    descriptor.bufptr = 0;
    descriptor.bufspace = SMALL_BUFSIZE - 1;

    char invalid_choice[] = "x";
    nanny(&descriptor, invalid_choice);

    const std::string output = descriptor.output;
    EXPECT_NE(output.find("0) Back to Account Menu.\n\r"), std::string::npos);
    EXPECT_EQ(output.find("0) Exit from the MUD.\n\r"), std::string::npos);

    free_char(descriptor.character);
    descriptor.character = nullptr;
}

TEST(InterpreAccountMenu, LegacyCharacterMenuStillUsesExitFromMudLabel)
{
    descriptor_data descriptor = make_descriptor();
    descriptor.connected = CON_SLCT;
    descriptor.account_name[0] = '\0';

    char invalid_choice[] = "x";
    nanny(&descriptor, invalid_choice);

    const std::string output = descriptor.output;
    EXPECT_NE(output.find("0) Exit from the MUD.\n\r"), std::string::npos);
    EXPECT_EQ(output.find("0) Back to Account Menu.\n\r"), std::string::npos);
}

TEST(InterpreAccountMenu, AccountBackedNewCharactersAreBornWithStartRoomAndNakedPerception)
{
    ScopedPlayerTableReset player_table_reset;
    ScopedStartRoomOverride start_room_override(RACE_HUMAN, 0);
    ensure_test_world_room(1200);

    char_data* character = new char_data {};
    clear_char(character, MOB_VOID);
    character->player.race = RACE_HUMAN;
    character->player.sex = SEX_MALE;
    character->player.name = strdup("aragorn");
    character->specials2.load_room = NOWHERE;
    character->specials2.rawPerception = 0;
    character->specials2.perception = 0;

    finalize_new_character_start_state(character);

    EXPECT_GT(GET_LEVEL(character), 0);
    EXPECT_EQ(character->specials2.load_room, 1200);
    EXPECT_EQ(character->specials2.rawPerception, get_naked_perception(character));
    EXPECT_EQ(character->specials2.perception, get_naked_perception(character));

    free_char(character);
}

TEST(InterpreAccountMenu, IntroduceCharForAccountBackedCharactersAvoidsLegacyFilesAndKeepsFirstLoginState)
{
    TemporaryDirectory temp_directory;
    ScopedWorkingDirectory working_directory(temp_directory.path());
    ScopedPlayerTableReset player_table_reset;
    ScopedStartRoomOverride start_room_override(RACE_HUMAN, 0);

    ASSERT_EQ(mkdir("accounts", 0700), 0);
    ASSERT_EQ(mkdir("accounts/A-E", 0700), 0);
    ASSERT_EQ(mkdir("players", 0700), 0);
    ASSERT_EQ(mkdir("players/A-E", 0700), 0);
    ASSERT_EQ(mkdir("plrobjs", 0700), 0);
    ASSERT_EQ(mkdir("plrobjs/A-E", 0700), 0);
    ensure_test_world_room(1200);
    static char test_motd[] = "Test MOTD\r\n";
    ScopedMotdOverride motd_override(test_motd);

    std::string error_message;
    ASSERT_TRUE(account::create_account(".", "acct", "player@example.com", "ValidPass1", 1700010200, nullptr, &error_message)) << error_message;

    descriptor_data descriptor = make_descriptor();
    descriptor.character = new char_data {};
    clear_char(descriptor.character, MOB_VOID);
    register_pc_char(descriptor.character);
    descriptor.character->desc = &descriptor;
    descriptor.connected = CON_QSEX;
    std::snprintf(descriptor.host, sizeof(descriptor.host), "%s", "127.0.0.1");
    std::snprintf(descriptor.pwd, sizeof(descriptor.pwd), "%s", "*ACCOUNT*");

    descriptor.character->player.sex = SEX_MALE;
    descriptor.character->player.race = RACE_HUMAN;
    descriptor.character->player.name = strdup("aragorn");

    const std::string stderr_path = temp_directory.path() + "/introduce.stderr";
    {
        ScopedStderrRedirect stderr_redirect(stderr_path);
        introduce_char(&descriptor);
        const std::string stderr_output = stderr_redirect.read_contents();
        EXPECT_EQ(stderr_output.find("cannot remove"), std::string::npos) << stderr_output;
    }

    struct stat file_info {};
    EXPECT_NE(stat(account::legacy_player_file_path(".", "aragorn").c_str(), &file_info), 0);
    EXPECT_NE(stat(account::legacy_object_file_path(".", "aragorn").c_str(), &file_info), 0);
    EXPECT_NE(stat(account::legacy_exploits_file_path(".", "aragorn").c_str(), &file_info), 0);

    char_file_u stored_character {};
    ASSERT_TRUE(account::read_account_character_file(".", "acct", "aragorn", &stored_character, &error_message)) << error_message;
    EXPECT_EQ(stored_character.specials2.load_room, 1200);
    EXPECT_EQ(stored_character.specials2.rawPerception, descriptor.character->specials2.rawPerception);
    EXPECT_EQ(stored_character.specials2.perception, descriptor.character->specials2.perception);

    std::string object_bytes;
    ASSERT_TRUE(load_object_save_bytes_for_character(".", "aragorn", &object_bytes, &error_message)) << error_message;
    EXPECT_FALSE(object_bytes.empty());

    char_data* loaded_character = new char_data {};
    clear_char(loaded_character, MOB_VOID);
    store_to_char(&stored_character, loaded_character);

    FILE* fp = nullptr;
    stage_account_backed_object_bytes_for_character(loaded_character, object_bytes.data(), object_bytes.size());
    fp = Crash_load(loaded_character);
    ASSERT_NE(fp, nullptr);
    EXPECT_EQ(std::fclose(fp), 0);
    EXPECT_EQ(loaded_character->specials2.load_room, 0);
    EXPECT_EQ(world[loaded_character->specials2.load_room].number, 1200);
    EXPECT_EQ(loaded_character->specials2.rawPerception, get_naked_perception(loaded_character));
    EXPECT_EQ(loaded_character->specials2.perception, get_naked_perception(loaded_character));

    free_char(descriptor.character);
    descriptor.character = nullptr;
    free_char(loaded_character);
}

TEST(InterpreAccountMenu, IntroduceCharRollbackDoesNotLeaveLegacyOrAccountNativeFiles)
{
    TemporaryDirectory temp_directory;
    ScopedWorkingDirectory working_directory(temp_directory.path());
    ScopedPlayerTableReset player_table_reset;
    ScopedStartRoomOverride start_room_override(RACE_HUMAN, 0);

    ASSERT_EQ(mkdir("accounts", 0700), 0);
    ASSERT_EQ(mkdir("accounts/A-E", 0700), 0);
    ASSERT_EQ(mkdir("players", 0700), 0);
    ASSERT_EQ(mkdir("players/A-E", 0700), 0);
    ASSERT_EQ(mkdir("plrobjs", 0700), 0);
    ASSERT_EQ(mkdir("plrobjs/A-E", 0700), 0);
    ASSERT_EQ(mkdir("exploits", 0700), 0);
    ASSERT_EQ(mkdir("exploits/A-E", 0700), 0);
    ensure_test_world_room(1200);
    create_entry(const_cast<char*>("existingplayer"));
    create_entry(const_cast<char*>("secondplayer"));
    static char test_motd[] = "Test MOTD\r\n";
    ScopedMotdOverride motd_override(test_motd);

    std::string error_message;
    ASSERT_TRUE(account::create_account(".", "acct", "player@example.com", "ValidPass1", 1700010200, nullptr, &error_message)) << error_message;

    const std::string blocking_exploit_path = account::account_character_exploits_path(".", "acct", "aragorn");
    ASSERT_EQ(mkdir(blocking_exploit_path.c_str(), 0700), 0);

    descriptor_data descriptor = make_descriptor();
    descriptor.character = new char_data {};
    clear_char(descriptor.character, MOB_VOID);
    register_pc_char(descriptor.character);
    descriptor.character->desc = &descriptor;
    descriptor.connected = CON_QSEX;
    std::snprintf(descriptor.host, sizeof(descriptor.host), "%s", "127.0.0.1");
    std::snprintf(descriptor.pwd, sizeof(descriptor.pwd), "%s", "*ACCOUNT*");

    descriptor.character->player.sex = SEX_MALE;
    descriptor.character->player.race = RACE_HUMAN;
    descriptor.character->player.name = strdup("aragorn");

    introduce_char(&descriptor);

    EXPECT_EQ(descriptor.connected, CON_CLOSE);
    EXPECT_NE(std::string(descriptor.output).find("rolled back"), std::string::npos);

    struct stat file_info {};
    EXPECT_NE(stat(account::legacy_player_file_path(".", "aragorn").c_str(), &file_info), 0);
    EXPECT_NE(stat(account::legacy_object_file_path(".", "aragorn").c_str(), &file_info), 0);
    EXPECT_NE(stat(account::legacy_exploits_file_path(".", "aragorn").c_str(), &file_info), 0);
    EXPECT_NE(stat(account::account_character_player_path(".", "acct", "aragorn").c_str(), &file_info), 0);
    EXPECT_NE(stat(account::account_character_object_path(".", "acct", "aragorn").c_str(), &file_info), 0);
    EXPECT_NE(stat(account::account_character_exploits_path(".", "acct", "aragorn").c_str(), &file_info), 0);

    account::AccountData account_data;
    ASSERT_TRUE(account::read_account_file(".", "acct", &account_data, &error_message)) << error_message;
    EXPECT_TRUE(account_data.characters.empty());
    EXPECT_TRUE(account_data.character_links.empty());
    ASSERT_GE(descriptor.pos, 0);
    EXPECT_TRUE(IS_SET(player_table[descriptor.pos].flags, PLR_DELETED));
    EXPECT_EQ(player_table[descriptor.pos].ch_file[0], '\0');

    free_char(descriptor.character);
    descriptor.character = nullptr;
}

TEST(InterpreAccountMenu, AdvanceLevelStillPersistsWhenAccountOwnershipLookupFails)
{
    TemporaryDirectory temp_directory;
    ScopedWorkingDirectory working_directory(temp_directory.path());
    ScopedPlayerTableReset player_table_reset;
    ASSERT_EQ(mkdir("players", 0700), 0);
    ASSERT_EQ(mkdir("players/A-E", 0700), 0);
    ASSERT_EQ(mkdir("players/F-J", 0700), 0);
    ASSERT_EQ(mkdir("players/K-O", 0700), 0);
    ASSERT_EQ(mkdir("players/P-T", 0700), 0);
    ASSERT_EQ(mkdir("players/U-Z", 0700), 0);
    ASSERT_EQ(mkdir("players/ZZZ", 0700), 0);

    descriptor_data descriptor = make_descriptor();
    descriptor.output = descriptor.small_outbuf;
    descriptor.bufspace = SMALL_BUFSIZE - 1;
    descriptor.character = new char_data {};
    clear_char(descriptor.character, MOB_VOID);
    register_pc_char(descriptor.character);
    descriptor.character->desc = &descriptor;
    descriptor.character->player.name = strdup("aragorn");
    descriptor.character->player.level = 2;
    descriptor.character->specials2.idnum = 4242;
    descriptor.character->player.race = RACE_HUMAN;
    descriptor.character->in_room = NOWHERE;
    descriptor.character->specials2.load_room = NOWHERE;
    descriptor.pos = create_entry(descriptor.character->player.name);

    advance_level(descriptor.character);

    struct stat file_info {};
    ASSERT_GE(descriptor.pos, 0);
    EXPECT_NE(player_table[descriptor.pos].ch_file[0], '\0');
    EXPECT_EQ(stat(player_table[descriptor.pos].ch_file, &file_info), 0);

    free_char(descriptor.character);
    descriptor.character = nullptr;
}

} // namespace
