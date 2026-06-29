#include "../account_management.h"
#include "../db.h"
#include "../handler.h"
#include "../interpre.h"
#include "../limits.h"
#include "../objects_json.h"
#include "../profs.h"
#include "../spells.h"
#include "../structs.h"
#include "../utils.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <limits.h>
#include <string>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

extern struct player_index_element* player_table;
extern struct char_data* character_list;
extern struct descriptor_data* descriptor_list;
extern struct room_data world;
extern FILE* fpCommand;
extern int iCommands;
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
int process_input(struct descriptor_data* t);
int get_from_q(struct txt_q* queue, char* dest);

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

class ScopedDescriptorListReset {
public:
    ScopedDescriptorListReset()
        : m_previous_descriptor_list(descriptor_list)
    {
        descriptor_list = nullptr;
    }

    ~ScopedDescriptorListReset()
    {
        descriptor_list = m_previous_descriptor_list;
    }

private:
    descriptor_data* m_previous_descriptor_list;
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

class ScopedCommandLog {
public:
    explicit ScopedCommandLog(const std::string& path)
        : m_previous_command_file(fpCommand)
        , m_previous_command_count(iCommands)
    {
        fpCommand = std::fopen(path.c_str(), "w");
        EXPECT_NE(fpCommand, nullptr);
        iCommands = 0;
    }

    ~ScopedCommandLog()
    {
        if (fpCommand != nullptr && fpCommand != m_previous_command_file)
            std::fclose(fpCommand);
        fpCommand = m_previous_command_file;
        iCommands = m_previous_command_count;
    }

private:
    FILE* m_previous_command_file;
    int m_previous_command_count;
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

        if (setenv(name, value.c_str(), 1) != 0)
            ADD_FAILURE() << "Expected test helper to set environment variable " << name << ".";
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

void write_text_file(const std::string& path, const std::string& contents)
{
    FILE* file = std::fopen(path.c_str(), "wb");
    ASSERT_NE(file, nullptr) << "Expected to open " << path << " for writing.";
    ASSERT_EQ(std::fwrite(contents.data(), sizeof(char), contents.size(), file), contents.size());
    std::fclose(file);
}

size_t count_occurrences(const std::string& haystack, const std::string& needle)
{
    if (needle.empty())
        return 0;

    size_t count = 0;
    size_t position = 0;
    while ((position = haystack.find(needle, position)) != std::string::npos) {
        ++count;
        position += needle.size();
    }
    return count;
}

size_t count_affects(const char_data* character)
{
    size_t count = 0;
    for (const affected_type* affect = character != nullptr ? character->affected : nullptr;
        affect != nullptr && count < MAX_AFFECT + 1;
        affect = affect->next) {
        ++count;
    }

    return count;
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

void initialize_descriptor(descriptor_data* descriptor)
{
    *descriptor = {};
    descriptor->output = descriptor->small_outbuf;
    descriptor->small_outbuf[0] = '\0';
    descriptor->bufptr = 0;
    descriptor->bufspace = SMALL_BUFSIZE - 1;
    descriptor->pos = -1;
    descriptor->connected = CON_ACCTMENU;
    std::snprintf(descriptor->account_name, sizeof(descriptor->account_name), "%s", "acct");
}

descriptor_data make_descriptor()
{
    descriptor_data descriptor {};
    initialize_descriptor(&descriptor);
    return descriptor;
}

descriptor_data* allocate_descriptor()
{
    descriptor_data* descriptor = nullptr;
    CREATE(descriptor, descriptor_data, 1);
    initialize_descriptor(descriptor);
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

struct SnoopProbeResult {
    std::string queued_input;
    std::string snoop_output;
    std::string last_input;
};

void reset_descriptor_output(descriptor_data* descriptor)
{
    descriptor->output[0] = '\0';
    descriptor->bufptr = 0;
    descriptor->bufspace = SMALL_BUFSIZE - 1;
}

void write_and_process_snooped_input(
    descriptor_data* descriptor, int socket_fd, const char* input, std::string* queued_input)
{
    const std::string input_line = std::string(input) + "\n";
    EXPECT_EQ(write(socket_fd, input_line.c_str(), input_line.size()),
        static_cast<ssize_t>(input_line.size()));
    EXPECT_EQ(process_input(descriptor), 1);

    char queued[MAX_INPUT_LENGTH] = {};
    EXPECT_TRUE(get_from_q(&descriptor->input, queued));
    if (queued_input != nullptr)
        *queued_input = queued;
}

SnoopProbeResult snoop_output_for_input_state(int connection_state, const char* input)
{
    int sockets[2] = { -1, -1 };
    EXPECT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets), 0);
    if (sockets[0] < 0 || sockets[1] < 0)
        return {};

    descriptor_data victim_descriptor = make_descriptor();
    victim_descriptor.connected = connection_state;
    victim_descriptor.descriptor = sockets[0];

    descriptor_data snooper_descriptor = make_descriptor();
    char_data snooper {};
    snooper.desc = &snooper_descriptor;
    victim_descriptor.snoop.snoop_by = &snooper;

    std::string queued_input;
    write_and_process_snooped_input(&victim_descriptor, sockets[1], input, &queued_input);

    close(sockets[0]);
    close(sockets[1]);
    return { queued_input, snooper_descriptor.output, victim_descriptor.last_input };
}

struct SnoopReplayResult {
    std::string secret_input;
    std::string replayed_input;
    std::string snoop_output;
    std::string last_input;
};

SnoopReplayResult snoop_replay_after_secret_input_state(int secret_connection_state, const char* secret_input)
{
    int sockets[2] = { -1, -1 };
    EXPECT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets), 0);
    if (sockets[0] < 0 || sockets[1] < 0)
        return {};

    descriptor_data victim_descriptor = make_descriptor();
    victim_descriptor.connected = secret_connection_state;
    victim_descriptor.descriptor = sockets[0];
    std::snprintf(victim_descriptor.last_input, sizeof(victim_descriptor.last_input), "%s", "look");

    descriptor_data snooper_descriptor = make_descriptor();
    char_data snooper {};
    snooper.desc = &snooper_descriptor;
    victim_descriptor.snoop.snoop_by = &snooper;

    std::string queued_secret_input;
    write_and_process_snooped_input(
        &victim_descriptor, sockets[1], secret_input, &queued_secret_input);

    victim_descriptor.connected = CON_DELCNF2;
    reset_descriptor_output(&snooper_descriptor);

    std::string replayed_input;
    write_and_process_snooped_input(&victim_descriptor, sockets[1], "!", &replayed_input);

    close(sockets[0]);
    close(sockets[1]);
    return {
        queued_secret_input,
        replayed_input,
        snooper_descriptor.output,
        victim_descriptor.last_input,
    };
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

TEST(InterpreAccountMenu, ActiveAccountSessionShowsPlayingLinkedCharacterInAccountMenu)
{
    TemporaryDirectory temp_directory;
    ScopedWorkingDirectory working_directory(temp_directory.path());
    ScopedDescriptorListReset descriptor_list_reset;
    ASSERT_EQ(mkdir("accounts", 0700), 0);
    ASSERT_EQ(mkdir("accounts/A-E", 0700), 0);

    account::AccountData stored_account;
    std::string error_message;
    ASSERT_TRUE(account::create_account(".", "acct", "player@example.com", "ValidPass1", 1700010200, &stored_account, &error_message)) << error_message;
    ASSERT_TRUE(account::admin_link_character(".", "acct", "aragorn", 1700010201, &stored_account, &error_message)) << error_message;

    descriptor_data active_descriptor = make_descriptor();
    active_descriptor.connected = CON_PLYNG;
    active_descriptor.descriptor = 7;
    attach_active_character(&active_descriptor, "aragorn", 50, 4242);
    descriptor_list = &active_descriptor;

    descriptor_data descriptor = make_descriptor();
    char invalid_choice[] = "x";
    nanny(&descriptor, invalid_choice);

    const std::string output = descriptor.output;
    EXPECT_NE(output.find("Active character: Aragorn (level 50, playing)\n\r"), std::string::npos) << output;
    EXPECT_EQ(output.find("Different character selection is locked"), std::string::npos) << output;

    free_char(active_descriptor.character);
    active_descriptor.character = nullptr;
}

TEST(InterpreAccountMenu, ActiveAccountSessionShowsLinklessLinkedCharacterInAccountMenu)
{
    TemporaryDirectory temp_directory;
    ScopedWorkingDirectory working_directory(temp_directory.path());
    ScopedDescriptorListReset descriptor_list_reset;
    ASSERT_EQ(mkdir("accounts", 0700), 0);
    ASSERT_EQ(mkdir("accounts/A-E", 0700), 0);

    account::AccountData stored_account;
    std::string error_message;
    ASSERT_TRUE(account::create_account(".", "acct", "player@example.com", "ValidPass1", 1700010200, &stored_account, &error_message)) << error_message;
    ASSERT_TRUE(account::admin_link_character(".", "acct", "aragorn", 1700010201, &stored_account, &error_message)) << error_message;

    descriptor_data active_descriptor = make_descriptor();
    active_descriptor.connected = CON_LINKLS;
    active_descriptor.descriptor = 0;
    attach_active_character(&active_descriptor, "aragorn", 50, 4242);
    descriptor_list = &active_descriptor;

    descriptor_data descriptor = make_descriptor();
    char invalid_choice[] = "x";
    nanny(&descriptor, invalid_choice);

    const std::string output = descriptor.output;
    EXPECT_NE(output.find("Active character: Aragorn (level 50, linkless)\n\r"), std::string::npos) << output;
    EXPECT_EQ(output.find("Different character selection is locked"), std::string::npos) << output;

    free_char(active_descriptor.character);
    active_descriptor.character = nullptr;
}

TEST(InterpreAccountMenu, ActiveAccountSessionIgnoresOtherAccountUnauthenticatedNpcAndUnlinkedDescriptors)
{
    TemporaryDirectory temp_directory;
    ScopedWorkingDirectory working_directory(temp_directory.path());
    ScopedDescriptorListReset descriptor_list_reset;
    ASSERT_EQ(mkdir("accounts", 0700), 0);
    ASSERT_EQ(mkdir("accounts/A-E", 0700), 0);

    account::AccountData stored_account;
    std::string error_message;
    ASSERT_TRUE(account::create_account(".", "acct", "player@example.com", "ValidPass1", 1700010200, &stored_account, &error_message)) << error_message;
    ASSERT_TRUE(account::admin_link_character(".", "acct", "aragorn", 1700010201, &stored_account, &error_message)) << error_message;

    descriptor_data other_account_descriptor = make_descriptor();
    std::snprintf(other_account_descriptor.account_name, sizeof(other_account_descriptor.account_name), "%s", "other");
    other_account_descriptor.connected = CON_PLYNG;
    attach_active_character(&other_account_descriptor, "aragorn", 50, 4242);

    descriptor_data unauthenticated_descriptor = make_descriptor();
    unauthenticated_descriptor.connected = CON_ACCTPWD;
    attach_active_character(&unauthenticated_descriptor, "aragorn", 50, 4242);

    descriptor_data unlinked_descriptor = make_descriptor();
    unlinked_descriptor.connected = CON_PLYNG;
    attach_active_character(&unlinked_descriptor, "boromir", 50, 5252);

    descriptor_data mismatched_owner_descriptor = make_descriptor();
    mismatched_owner_descriptor.connected = CON_PLYNG;
    attach_active_character(&mismatched_owner_descriptor, "aragorn", 50, 4242);
    descriptor_data actual_owner_descriptor = make_descriptor();
    mismatched_owner_descriptor.character->desc = &actual_owner_descriptor;

    descriptor_data npc_descriptor = make_descriptor();
    npc_descriptor.connected = CON_PLYNG;
    attach_active_character(&npc_descriptor, "aragorn", 50, 4242);
    npc_descriptor.character->specials2.act = MOB_ISNPC;

    descriptor_data null_character_descriptor = make_descriptor();
    null_character_descriptor.connected = CON_PLYNG;

    other_account_descriptor.next = &unauthenticated_descriptor;
    unauthenticated_descriptor.next = &unlinked_descriptor;
    unlinked_descriptor.next = &mismatched_owner_descriptor;
    mismatched_owner_descriptor.next = &npc_descriptor;
    npc_descriptor.next = &null_character_descriptor;
    descriptor_list = &other_account_descriptor;

    descriptor_data descriptor = make_descriptor();
    char invalid_choice[] = "x";
    nanny(&descriptor, invalid_choice);

    const std::string output = descriptor.output;
    EXPECT_EQ(output.find("Active character:"), std::string::npos) << output;
    EXPECT_EQ(output.find("Different character selection is locked"), std::string::npos) << output;

    free_char(other_account_descriptor.character);
    free_char(unauthenticated_descriptor.character);
    free_char(unlinked_descriptor.character);
    free_char(mismatched_owner_descriptor.character);
    npc_descriptor.character->specials2.act = 0;
    free_char(npc_descriptor.character);
    other_account_descriptor.character = nullptr;
    unauthenticated_descriptor.character = nullptr;
    unlinked_descriptor.character = nullptr;
    mismatched_owner_descriptor.character = nullptr;
    npc_descriptor.character = nullptr;
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

TEST(InterpreAccountMenu, ActiveLevelNinetyFiveBlocksDifferentLinkedCharacterBeforeSelectionSideEffects)
{
    TemporaryDirectory temp_directory;
    ScopedWorkingDirectory working_directory(temp_directory.path());
    ScopedDescriptorListReset descriptor_list_reset;
    ScopedPlayerTableReset player_table_reset;
    ASSERT_EQ(mkdir("accounts", 0700), 0);
    ASSERT_EQ(mkdir("accounts/A-E", 0700), 0);

    account::AccountData stored_account;
    std::string error_message;
    ASSERT_TRUE(account::create_account(".", "acct", "player@example.com", "ValidPass1", 1700010200, &stored_account, &error_message)) << error_message;
    ASSERT_TRUE(account::admin_link_character(".", "acct", "aragorn", 1700010201, &stored_account, &error_message)) << error_message;
    char_file_u legolas = make_stored_character("legolas", 45, RACE_HUMAN);
    legolas.specials2.idnum = 5252;
    legolas.specials2.load_room = 3001;
    ASSERT_TRUE(account::write_account_character_file(".", "acct", legolas, &error_message)) << error_message;
    ASSERT_TRUE(account::write_default_account_object_file(".", "acct", "legolas", &error_message)) << error_message;
    ASSERT_TRUE(account::write_default_account_exploit_file(".", "acct", "legolas", &error_message)) << error_message;
    ASSERT_TRUE(account::admin_link_character(".", "acct", "legolas", 1700010202, &stored_account, &error_message)) << error_message;

    descriptor_data active_descriptor = make_descriptor();
    active_descriptor.connected = CON_PLYNG;
    active_descriptor.descriptor = 7;
    attach_active_character(&active_descriptor, "aragorn", 95, 4242);
    descriptor_list = &active_descriptor;

    descriptor_data descriptor = make_descriptor();
    descriptor.connected = CON_ACCTSLCT;
    char selection[] = "2";
    nanny(&descriptor, selection);

    EXPECT_EQ(descriptor.connected, CON_ACCTSLCT);
    EXPECT_EQ(descriptor.character, nullptr);
    EXPECT_EQ(descriptor.pos, -1);
    EXPECT_EQ(top_of_p_table, -1)
        << "Blocked selection should happen before creating or updating player-table entries.";
    const std::string output = descriptor.output;
    EXPECT_NE(output.find("You are already connected as Aragorn."), std::string::npos) << output;
    EXPECT_NE(output.find("\n\rCharacter number: "), std::string::npos) << output;

    struct stat file_info {};
    EXPECT_NE(stat("players", &file_info), 0);
    EXPECT_NE(stat("plrobjs", &file_info), 0);
    EXPECT_NE(stat("exploits", &file_info), 0);

    free_char(active_descriptor.character);
    active_descriptor.character = nullptr;
}

TEST(InterpreAccountMenu, ActiveOverLevelNinetyFiveStillShowsActiveCharacterWithoutLockMessage)
{
    TemporaryDirectory temp_directory;
    ScopedWorkingDirectory working_directory(temp_directory.path());
    ScopedDescriptorListReset descriptor_list_reset;
    ASSERT_EQ(mkdir("accounts", 0700), 0);
    ASSERT_EQ(mkdir("accounts/A-E", 0700), 0);

    account::AccountData stored_account;
    std::string error_message;
    ASSERT_TRUE(account::create_account(".", "acct", "player@example.com", "ValidPass1", 1700010200, &stored_account, &error_message)) << error_message;
    ASSERT_TRUE(account::admin_link_character(".", "acct", "aragorn", 1700010201, &stored_account, &error_message)) << error_message;

    descriptor_data active_descriptor = make_descriptor();
    active_descriptor.connected = CON_PLYNG;
    active_descriptor.descriptor = 7;
    attach_active_character(&active_descriptor, "aragorn", 96, 4242);
    descriptor_list = &active_descriptor;

    descriptor_data descriptor = make_descriptor();
    char invalid_choice[] = "x";
    nanny(&descriptor, invalid_choice);

    const std::string output = descriptor.output;
    EXPECT_NE(output.find("Active character: Aragorn (level 96, playing)\n\r"), std::string::npos) << output;
    EXPECT_EQ(output.find("Different character selection is locked"), std::string::npos) << output;

    free_char(active_descriptor.character);
    active_descriptor.character = nullptr;
}

TEST(InterpreAccountMenu, ActiveOverLevelNinetyFiveAllowsDifferentLinkedCharacterSelection)
{
    TemporaryDirectory temp_directory;
    ScopedWorkingDirectory working_directory(temp_directory.path());
    ScopedDescriptorListReset descriptor_list_reset;
    ScopedPlayerTableReset player_table_reset;
    ASSERT_EQ(mkdir("accounts", 0700), 0);
    ASSERT_EQ(mkdir("accounts/A-E", 0700), 0);
    static char test_motd[] = "Test MOTD\r\n";
    ScopedMotdOverride motd_override(test_motd);

    account::AccountData stored_account;
    std::string error_message;
    ASSERT_TRUE(account::create_account(".", "acct", "player@example.com", "ValidPass1", 1700010200, &stored_account, &error_message)) << error_message;
    char_file_u aragorn = make_stored_character("aragorn", 96, RACE_HUMAN);
    aragorn.specials2.idnum = 4242;
    ASSERT_TRUE(account::write_account_character_file(".", "acct", aragorn, &error_message)) << error_message;
    ASSERT_TRUE(account::write_default_account_object_file(".", "acct", "aragorn", &error_message)) << error_message;
    ASSERT_TRUE(account::write_default_account_exploit_file(".", "acct", "aragorn", &error_message)) << error_message;
    ASSERT_TRUE(account::admin_link_character(".", "acct", "aragorn", 1700010201, &stored_account, &error_message)) << error_message;
    char_file_u legolas = make_stored_character("legolas", 45, RACE_HUMAN);
    legolas.specials2.idnum = 5252;
    legolas.specials2.load_room = 3001;
    ASSERT_TRUE(account::write_account_character_file(".", "acct", legolas, &error_message)) << error_message;
    ASSERT_TRUE(account::write_default_account_object_file(".", "acct", "legolas", &error_message)) << error_message;
    ASSERT_TRUE(account::write_default_account_exploit_file(".", "acct", "legolas", &error_message)) << error_message;
    ASSERT_TRUE(account::admin_link_character(".", "acct", "legolas", 1700010202, &stored_account, &error_message)) << error_message;

    descriptor_data active_descriptor = make_descriptor();
    active_descriptor.connected = CON_PLYNG;
    active_descriptor.descriptor = 7;
    attach_active_character(&active_descriptor, "aragorn", 96, 4242);
    descriptor_list = &active_descriptor;

    descriptor_data descriptor = make_descriptor();
    descriptor.connected = CON_ACCTSLCT;
    std::snprintf(descriptor.host, sizeof(descriptor.host), "%s", "127.0.0.1");
    char selection[] = "2";
    nanny(&descriptor, selection);

    EXPECT_EQ(descriptor.connected, CON_SLCT);
    ASSERT_NE(descriptor.character, nullptr);
    ASSERT_NE(descriptor.character->player.name, nullptr);
    EXPECT_STREQ(descriptor.character->player.name, "legolas");
    EXPECT_NE(std::string(descriptor.output).find("0) Back to Account Menu.\n\r"), std::string::npos);
    EXPECT_EQ(std::string(descriptor.output).find("You are already connected as Aragorn."), std::string::npos);

    free_char(descriptor.character);
    descriptor.character = nullptr;
    free_char(active_descriptor.character);
    active_descriptor.character = nullptr;
}

TEST(InterpreAccountMenu, MultipleActiveSessionsShowAllAndLowLevelOneStillRestrictsSelection)
{
    TemporaryDirectory temp_directory;
    ScopedWorkingDirectory working_directory(temp_directory.path());
    ScopedDescriptorListReset descriptor_list_reset;
    ScopedPlayerTableReset player_table_reset;
    ASSERT_EQ(mkdir("accounts", 0700), 0);
    ASSERT_EQ(mkdir("accounts/A-E", 0700), 0);

    account::AccountData stored_account;
    std::string error_message;
    ASSERT_TRUE(account::create_account(".", "acct", "player@example.com", "ValidPass1", 1700010200, &stored_account, &error_message)) << error_message;
    ASSERT_TRUE(account::admin_link_character(".", "acct", "aragorn", 1700010201, &stored_account, &error_message)) << error_message;
    ASSERT_TRUE(account::admin_link_character(".", "acct", "boromir", 1700010202, &stored_account, &error_message)) << error_message;
    ASSERT_TRUE(account::admin_link_character(".", "acct", "legolas", 1700010203, &stored_account, &error_message)) << error_message;

    descriptor_data high_level_descriptor = make_descriptor();
    high_level_descriptor.connected = CON_PLYNG;
    high_level_descriptor.descriptor = 7;
    attach_active_character(&high_level_descriptor, "aragorn", 96, 4242);

    descriptor_data low_level_descriptor = make_descriptor();
    low_level_descriptor.connected = CON_PLYNG;
    low_level_descriptor.descriptor = 8;
    attach_active_character(&low_level_descriptor, "boromir", 50, 5252);
    high_level_descriptor.next = &low_level_descriptor;
    descriptor_list = &high_level_descriptor;

    descriptor_data menu_descriptor = make_descriptor();
    char invalid_choice[] = "x";
    nanny(&menu_descriptor, invalid_choice);

    const std::string menu_output = menu_descriptor.output;
    EXPECT_NE(menu_output.find("Active characters:\n\r"), std::string::npos) << menu_output;
    EXPECT_NE(menu_output.find("- Aragorn (level 96, playing)\n\r"), std::string::npos) << menu_output;
    EXPECT_NE(menu_output.find("- Boromir (level 50, playing)\n\r"), std::string::npos) << menu_output;
    EXPECT_EQ(menu_output.find("Different character selection is locked"), std::string::npos) << menu_output;

    descriptor_data select_descriptor = make_descriptor();
    select_descriptor.connected = CON_ACCTSLCT;
    char selection[] = "3";
    nanny(&select_descriptor, selection);

    EXPECT_EQ(select_descriptor.connected, CON_ACCTSLCT);
    EXPECT_EQ(select_descriptor.character, nullptr);
    EXPECT_EQ(select_descriptor.pos, -1);
    const std::string selection_output = select_descriptor.output;
    EXPECT_NE(selection_output.find("You are already connected as Boromir."), std::string::npos) << selection_output;
    EXPECT_NE(selection_output.find("\n\rCharacter number: "), std::string::npos) << selection_output;

    free_char(high_level_descriptor.character);
    free_char(low_level_descriptor.character);
    high_level_descriptor.character = nullptr;
    low_level_descriptor.character = nullptr;
}

TEST(InterpreAccountMenu, SelectingSameLinklessActiveCharacterReconnectsExistingBody)
{
    TemporaryDirectory temp_directory;
    ScopedWorkingDirectory working_directory(temp_directory.path());
    ScopedDescriptorListReset descriptor_list_reset;
    ScopedPlayerTableReset player_table_reset;
    ASSERT_EQ(mkdir("accounts", 0700), 0);
    ASSERT_EQ(mkdir("accounts/A-E", 0700), 0);
    ensure_test_world_room(1200);

    account::AccountData stored_account;
    std::string error_message;
    ASSERT_TRUE(account::create_account(".", "acct", "player@example.com", "ValidPass1", 1700010200, &stored_account, &error_message)) << error_message;
    char_file_u aragorn = make_stored_character("aragorn", 95, RACE_HUMAN);
    aragorn.specials2.idnum = 4242;
    aragorn.specials2.load_room = 1200;
    ASSERT_TRUE(account::write_account_character_file(".", "acct", aragorn, &error_message)) << error_message;
    ASSERT_TRUE(account::write_default_account_object_file(".", "acct", "aragorn", &error_message)) << error_message;
    ASSERT_TRUE(account::write_default_account_exploit_file(".", "acct", "aragorn", &error_message)) << error_message;
    ASSERT_TRUE(account::admin_link_character(".", "acct", "aragorn", 1700010201, &stored_account, &error_message)) << error_message;

    descriptor_data* active_descriptor = allocate_descriptor();
    active_descriptor->connected = CON_LINKLS;
    active_descriptor->descriptor = 0;
    char_data* active_character = attach_active_character(active_descriptor, "aragorn", 95, 4242);
    active_character->in_room = 0;
    register_pc_char(active_character);
    active_character->next = nullptr;
    character_list = active_character;
    descriptor_list = active_descriptor;

    descriptor_data descriptor = make_descriptor();
    descriptor.connected = CON_ACCTSLCT;
    std::snprintf(descriptor.host, sizeof(descriptor.host), "%s", "127.0.0.1");
    char selection[] = "1";
    nanny(&descriptor, selection);

    EXPECT_EQ(descriptor.connected, CON_PLYNG);
    EXPECT_EQ(descriptor.character, active_character);
    EXPECT_EQ(active_character->desc, &descriptor);
    EXPECT_EQ(descriptor_list, nullptr);
    EXPECT_NE(std::string(descriptor.output).find("Reconnecting."), std::string::npos);

    character_list = nullptr;
    free_char(descriptor.character);
    descriptor.character = nullptr;
}

TEST(InterpreAccountMenu, SelectingSameActivePlayingCharacterUsurpsExistingDescriptor)
{
    TemporaryDirectory temp_directory;
    ScopedWorkingDirectory working_directory(temp_directory.path());
    ScopedDescriptorListReset descriptor_list_reset;
    ScopedPlayerTableReset player_table_reset;
    ASSERT_EQ(mkdir("accounts", 0700), 0);
    ASSERT_EQ(mkdir("accounts/A-E", 0700), 0);
    ensure_test_world_room(1200);

    account::AccountData stored_account;
    std::string error_message;
    ASSERT_TRUE(account::create_account(".", "acct", "player@example.com", "ValidPass1", 1700010200, &stored_account, &error_message)) << error_message;
    char_file_u aragorn = make_stored_character("aragorn", 95, RACE_HUMAN);
    aragorn.specials2.idnum = 4242;
    aragorn.specials2.load_room = 1200;
    ASSERT_TRUE(account::write_account_character_file(".", "acct", aragorn, &error_message)) << error_message;
    ASSERT_TRUE(account::write_default_account_object_file(".", "acct", "aragorn", &error_message)) << error_message;
    ASSERT_TRUE(account::write_default_account_exploit_file(".", "acct", "aragorn", &error_message)) << error_message;
    ASSERT_TRUE(account::admin_link_character(".", "acct", "aragorn", 1700010201, &stored_account, &error_message)) << error_message;

    descriptor_data active_descriptor = make_descriptor();
    active_descriptor.connected = CON_PLYNG;
    active_descriptor.descriptor = 7;
    char_data* active_character = attach_active_character(&active_descriptor, "aragorn", 95, 4242);
    active_character->in_room = 0;
    register_pc_char(active_character);
    active_character->next = nullptr;
    character_list = active_character;
    descriptor_list = &active_descriptor;

    descriptor_data descriptor = make_descriptor();
    descriptor.connected = CON_ACCTSLCT;
    std::snprintf(descriptor.host, sizeof(descriptor.host), "%s", "127.0.0.1");
    char selection[] = "1";
    nanny(&descriptor, selection);

    EXPECT_EQ(descriptor.connected, CON_PLYNG);
    EXPECT_EQ(descriptor.character, active_character);
    EXPECT_EQ(active_character->desc, &descriptor);
    EXPECT_EQ(active_descriptor.connected, CON_CLOSE);
    EXPECT_EQ(active_descriptor.character, nullptr);
    EXPECT_NE(std::string(descriptor.output).find("You take over your own body, already in use!"), std::string::npos);

    character_list = nullptr;
    free_char(descriptor.character);
    descriptor.character = nullptr;
}

TEST(InterpreAccountMenu, StaleAccountBackedCharacterMenuBlocksDifferentActiveLowLevelCharacter)
{
    TemporaryDirectory temp_directory;
    ScopedWorkingDirectory working_directory(temp_directory.path());
    ScopedDescriptorListReset descriptor_list_reset;
    ScopedPlayerTableReset player_table_reset;
    ASSERT_EQ(mkdir("accounts", 0700), 0);
    ASSERT_EQ(mkdir("accounts/A-E", 0700), 0);

    account::AccountData stored_account;
    std::string error_message;
    ASSERT_TRUE(account::create_account(".", "acct", "player@example.com", "ValidPass1", 1700010200, &stored_account, &error_message)) << error_message;
    ASSERT_TRUE(account::admin_link_character(".", "acct", "aragorn", 1700010201, &stored_account, &error_message)) << error_message;
    ASSERT_TRUE(account::admin_link_character(".", "acct", "legolas", 1700010202, &stored_account, &error_message)) << error_message;

    descriptor_data active_descriptor = make_descriptor();
    active_descriptor.connected = CON_PLYNG;
    active_descriptor.descriptor = 7;
    attach_active_character(&active_descriptor, "aragorn", 50, 4242);
    descriptor_list = &active_descriptor;

    descriptor_data descriptor = make_descriptor();
    descriptor.connected = CON_SLCT;
    descriptor.character = new char_data {};
    clear_char(descriptor.character, MOB_VOID);
    descriptor.character->player.name = strdup("legolas");
    descriptor.character->player.level = 45;
    descriptor.character->specials2.idnum = 5252;
    descriptor.character->desc = &descriptor;
    descriptor.pos = 2;

    char enter_choice[] = "1";
    nanny(&descriptor, enter_choice);

    EXPECT_EQ(descriptor.connected, CON_ACCTMENU);
    EXPECT_EQ(descriptor.character, nullptr);
    EXPECT_EQ(descriptor.pos, -1);
    EXPECT_EQ(top_of_p_table, -1);
    const std::string output = descriptor.output;
    EXPECT_NE(output.find("You are already connected as Aragorn."), std::string::npos) << output;
    EXPECT_NE(output.find("Choice: "), std::string::npos) << output;

    free_char(active_descriptor.character);
    active_descriptor.character = nullptr;
}

TEST(InterpreAccountMenu, RestrictedActiveCharacterBlocksNewCharacterCreation)
{
    TemporaryDirectory temp_directory;
    ScopedWorkingDirectory working_directory(temp_directory.path());
    ScopedDescriptorListReset descriptor_list_reset;
    ASSERT_EQ(mkdir("accounts", 0700), 0);
    ASSERT_EQ(mkdir("accounts/A-E", 0700), 0);

    account::AccountData stored_account;
    std::string error_message;
    ASSERT_TRUE(account::create_account(".", "acct", "player@example.com", "ValidPass1", 1700010200, &stored_account, &error_message)) << error_message;
    ASSERT_TRUE(account::admin_link_character(".", "acct", "aragorn", 1700010201, &stored_account, &error_message)) << error_message;

    descriptor_data active_descriptor = make_descriptor();
    active_descriptor.connected = CON_PLYNG;
    active_descriptor.descriptor = 7;
    attach_active_character(&active_descriptor, "aragorn", 50, 4242);
    descriptor_list = &active_descriptor;

    descriptor_data descriptor = make_descriptor();
    char new_character_choice[] = "4";
    nanny(&descriptor, new_character_choice);

    EXPECT_EQ(descriptor.connected, CON_ACCTMENU);
    const std::string output = descriptor.output;
    EXPECT_NE(output.find("You are already connected as Aragorn."), std::string::npos) << output;
    EXPECT_EQ(output.find("New character name:"), std::string::npos) << output;
    EXPECT_NE(output.find("Choice: "), std::string::npos) << output;

    free_char(active_descriptor.character);
    active_descriptor.character = nullptr;
}

TEST(InterpreAccountMenu, StaleAccountCreationWizardBlocksBirthWhenLowLevelCharacterBecomesActive)
{
    TemporaryDirectory temp_directory;
    ScopedWorkingDirectory working_directory(temp_directory.path());
    ScopedDescriptorListReset descriptor_list_reset;
    ScopedPlayerTableReset player_table_reset;
    ASSERT_EQ(mkdir("accounts", 0700), 0);
    ASSERT_EQ(mkdir("accounts/A-E", 0700), 0);

    account::AccountData stored_account;
    std::string error_message;
    ASSERT_TRUE(account::create_account(".", "acct", "player@example.com", "ValidPass1", 1700010200, &stored_account, &error_message)) << error_message;
    ASSERT_TRUE(account::admin_link_character(".", "acct", "aragorn", 1700010201, &stored_account, &error_message)) << error_message;

    descriptor_data active_descriptor = make_descriptor();
    active_descriptor.connected = CON_PLYNG;
    active_descriptor.descriptor = 7;
    attach_active_character(&active_descriptor, "aragorn", 50, 4242);
    descriptor_list = &active_descriptor;

    descriptor_data descriptor = make_descriptor();
    descriptor.connected = CON_CREATE;
    descriptor.character = new char_data {};
    clear_char(descriptor.character, MOB_VOID);
    descriptor.character->player.name = strdup("legolas");
    descriptor.character->player.race = RACE_HUMAN;
    descriptor.character->player.sex = SEX_MALE;
    descriptor.character->desc = &descriptor;
    descriptor.pos = -1;

    introduce_char(&descriptor);

    EXPECT_EQ(descriptor.connected, CON_ACCTMENU);
    EXPECT_EQ(descriptor.character, nullptr);
    EXPECT_EQ(descriptor.pos, -1);
    EXPECT_EQ(top_of_p_table, -1)
        << "Stale creation guard should run before create_entry().";
    const std::string output = descriptor.output;
    EXPECT_NE(output.find("You are already connected as Aragorn."), std::string::npos) << output;
    EXPECT_NE(output.find("New character creation cancelled."), std::string::npos) << output;

    struct stat file_info {};
    EXPECT_NE(stat(account::account_character_player_path(".", "acct", "legolas").c_str(), &file_info), 0);
    EXPECT_NE(stat(account::account_character_object_path(".", "acct", "legolas").c_str(), &file_info), 0);
    EXPECT_NE(stat(account::account_character_exploits_path(".", "acct", "legolas").c_str(), &file_info), 0);

    free_char(active_descriptor.character);
    active_descriptor.character = nullptr;
}

TEST(InterpreAccountMenu, StaleAccountCreationWizardCannotOverwriteSameNameActiveCharacterAssets)
{
    TemporaryDirectory temp_directory;
    ScopedWorkingDirectory working_directory(temp_directory.path());
    ScopedDescriptorListReset descriptor_list_reset;
    ScopedPlayerTableReset player_table_reset;
    ASSERT_EQ(mkdir("accounts", 0700), 0);
    ASSERT_EQ(mkdir("accounts/A-E", 0700), 0);

    account::AccountData stored_account;
    std::string error_message;
    ASSERT_TRUE(account::create_account(".", "acct", "player@example.com", "ValidPass1", 1700010200, &stored_account, &error_message)) << error_message;
    char_file_u stored_aragorn = make_stored_character("aragorn", 50, RACE_HUMAN);
    stored_aragorn.specials2.idnum = 4242;
    ASSERT_TRUE(account::write_account_character_file(".", "acct", stored_aragorn, &error_message)) << error_message;
    ASSERT_TRUE(account::write_default_account_object_file(".", "acct", "aragorn", &error_message)) << error_message;
    ASSERT_TRUE(account::write_default_account_exploit_file(".", "acct", "aragorn", &error_message)) << error_message;
    ASSERT_TRUE(account::admin_link_character(".", "acct", "aragorn", 1700010201, &stored_account, &error_message)) << error_message;

    descriptor_data active_descriptor = make_descriptor();
    active_descriptor.connected = CON_PLYNG;
    active_descriptor.descriptor = 7;
    attach_active_character(&active_descriptor, "aragorn", 50, 4242);
    descriptor_list = &active_descriptor;

    descriptor_data descriptor = make_descriptor();
    descriptor.connected = CON_CREATE;
    descriptor.character = new char_data {};
    clear_char(descriptor.character, MOB_VOID);
    descriptor.character->player.name = strdup("aragorn");
    descriptor.character->player.race = RACE_HUMAN;
    descriptor.character->player.sex = SEX_MALE;
    descriptor.character->desc = &descriptor;
    descriptor.pos = -1;

    introduce_char(&descriptor);

    EXPECT_EQ(descriptor.connected, CON_ACCTMENU);
    EXPECT_EQ(descriptor.character, nullptr);
    EXPECT_EQ(descriptor.pos, -1);
    EXPECT_EQ(top_of_p_table, -1)
        << "Stale same-name creation guard should run before create_entry().";
    const std::string output = descriptor.output;
    EXPECT_NE(output.find("That character name is already linked to an account."), std::string::npos) << output;
    EXPECT_NE(output.find("New character creation cancelled."), std::string::npos) << output;

    char_file_u reloaded_aragorn {};
    ASSERT_TRUE(account::read_account_character_file(".", "acct", "aragorn", &reloaded_aragorn, &error_message)) << error_message;
    EXPECT_EQ(reloaded_aragorn.specials2.idnum, stored_aragorn.specials2.idnum);
    struct stat file_info {};
    EXPECT_EQ(stat(account::account_character_object_path(".", "acct", "aragorn").c_str(), &file_info), 0);
    EXPECT_EQ(stat(account::account_character_exploits_path(".", "acct", "aragorn").c_str(), &file_info), 0);

    free_char(active_descriptor.character);
    active_descriptor.character = nullptr;
}

TEST(InterpreAccountMenu, RestrictedActiveCharacterStillAllowsListResetAndLogout)
{
    TemporaryDirectory temp_directory;
    ScopedWorkingDirectory working_directory(temp_directory.path());
    ScopedDescriptorListReset descriptor_list_reset;
    ASSERT_EQ(mkdir("accounts", 0700), 0);
    ASSERT_EQ(mkdir("accounts/A-E", 0700), 0);

    account::AccountData stored_account;
    std::string error_message;
    ASSERT_TRUE(account::create_account(".", "acct", "player@example.com", "ValidPass1", 1700010200, &stored_account, &error_message)) << error_message;
    ASSERT_TRUE(account::admin_link_character(".", "acct", "aragorn", 1700010201, &stored_account, &error_message)) << error_message;
    ASSERT_TRUE(account::write_account_character_file(".", "acct", make_stored_character("aragorn", 50, RACE_HUMAN), &error_message)) << error_message;

    descriptor_data active_descriptor = make_descriptor();
    active_descriptor.connected = CON_PLYNG;
    active_descriptor.descriptor = 7;
    attach_active_character(&active_descriptor, "aragorn", 50, 4242);
    descriptor_list = &active_descriptor;

    descriptor_data list_descriptor = make_descriptor();
    char list_choice[] = "1";
    nanny(&list_descriptor, list_choice);
    EXPECT_EQ(list_descriptor.connected, CON_ACCTMENU);
    std::string output = list_descriptor.output;
    EXPECT_NE(output.find("\n\rLinked characters:\n\r"), std::string::npos) << output;
    EXPECT_NE(output.find("1) [ 50 Hum] Aragorn"), std::string::npos) << output;
    EXPECT_NE(output.find("Active character: Aragorn (level 50, playing)\n\r"), std::string::npos) << output;

    descriptor_data reset_descriptor = make_descriptor();
    char reset_choice[] = "5";
    nanny(&reset_descriptor, reset_choice);
    EXPECT_EQ(reset_descriptor.connected, CON_ACCTRESETOLD);
    EXPECT_NE(std::string(reset_descriptor.output).find("Current account password: "), std::string::npos);

    descriptor_data link_descriptor = make_descriptor();
    char link_choice[] = "3";
    nanny(&link_descriptor, link_choice);
    EXPECT_EQ(link_descriptor.connected, CON_ACCTLINKNAME);
    EXPECT_NE(std::string(link_descriptor.output).find("Legacy character name: "), std::string::npos);

    descriptor_data logout_descriptor = make_descriptor();
    char logout_choice[] = "0";
    nanny(&logout_descriptor, logout_choice);
    EXPECT_EQ(logout_descriptor.connected, CON_NME);
    EXPECT_STREQ(logout_descriptor.account_name, "");
    EXPECT_STREQ(logout_descriptor.account_email, "");
    EXPECT_NE(std::string(logout_descriptor.output).find("Account email: "), std::string::npos);

    free_char(active_descriptor.character);
    active_descriptor.character = nullptr;
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

TEST(InterpreAccountMenu, PendingVerificationLoginResetsBadPasswordCounterAndPromptsForCode)
{
    TemporaryDirectory temp_directory;
    ScopedWorkingDirectory working_directory(temp_directory.path());
    ScopedEnvironmentVariable sendmail_override("ROTS_SENDMAIL_COMMAND", "/bin/true");
    ASSERT_EQ(mkdir("accounts", 0700), 0);
    ASSERT_EQ(mkdir("accounts/A-E", 0700), 0);

    account::AccountData stored_account;
    std::string error_message;
    ASSERT_TRUE(account::create_account(".", "acct", "Player@Example.COM", "ValidPass1",
        1700010200, &stored_account, &error_message))
        << error_message;

    descriptor_data descriptor = make_descriptor();
    descriptor.connected = CON_ACCTPWD;
    descriptor.bad_pws = 4;
    std::snprintf(
        descriptor.account_email, sizeof(descriptor.account_email), "%s", "player@example.com");
    std::snprintf(
        descriptor.account_password, sizeof(descriptor.account_password), "%s", "stale-secret");

    char password[] = "ValidPass1";
    nanny(&descriptor, password);

    EXPECT_EQ(descriptor.connected, CON_ACCTVERIFY);
    EXPECT_EQ(descriptor.bad_pws, 0);
    EXPECT_STREQ(descriptor.account_name, "acct");
    EXPECT_STREQ(descriptor.account_email, "player@example.com");
    EXPECT_STREQ(descriptor.account_password, "");
    const std::string output = descriptor.output;
    EXPECT_NE(output.find("pending verification"), std::string::npos) << output;
    EXPECT_NE(output.find("Verification code"), std::string::npos) << output;
    EXPECT_EQ(output.find("Account:"), std::string::npos) << output;
}

TEST(InterpreAccountMenu, VerificationCancelClearsTransientLoginStateAndReturnsToEmailPrompt)
{
    descriptor_data descriptor = make_descriptor();
    descriptor.connected = CON_ACCTVERIFY;
    descriptor.bad_pws = 3;
    std::snprintf(descriptor.account_name, sizeof(descriptor.account_name), "%s", "acct");
    std::snprintf(
        descriptor.account_email, sizeof(descriptor.account_email), "%s", "player@example.com");
    std::snprintf(
        descriptor.account_password, sizeof(descriptor.account_password), "%s", "stale-secret");
    std::snprintf(descriptor.account_character_name,
        sizeof(descriptor.account_character_name), "%s", "aragorn");

    char cancel[] = "cancel";
    nanny(&descriptor, cancel);

    EXPECT_EQ(descriptor.connected, CON_NME);
    EXPECT_STREQ(descriptor.account_name, "");
    EXPECT_STREQ(descriptor.account_email, "");
    EXPECT_STREQ(descriptor.account_password, "");
    EXPECT_STREQ(descriptor.account_character_name, "");
    EXPECT_EQ(descriptor.bad_pws, 0);
    EXPECT_EQ(std::string(descriptor.output), "Account email: ");
}

TEST(InterpreAccountMenu, VerificationBadCodeThresholdKeepsPromptUntilFifthDescriptorFailure)
{
    TemporaryDirectory temp_directory;
    ScopedWorkingDirectory working_directory(temp_directory.path());
    ASSERT_EQ(mkdir("accounts", 0700), 0);
    ASSERT_EQ(mkdir("accounts/A-E", 0700), 0);

    account::AccountData stored_account;
    std::string error_message;
    ASSERT_TRUE(account::create_account(".", "acct", "player@example.com", "ValidPass1",
        1700010200, &stored_account, &error_message))
        << error_message;
    std::string verification_code;
    ASSERT_TRUE(account::prepare_email_verification_code(
        &stored_account, 1700010201, &verification_code, &error_message))
        << error_message;
    ASSERT_TRUE(account::write_account_file(".", stored_account, &error_message)) << error_message;

    descriptor_data retry_descriptor = make_descriptor();
    retry_descriptor.connected = CON_ACCTVERIFY;
    retry_descriptor.bad_pws = 3;
    std::snprintf(
        retry_descriptor.account_name, sizeof(retry_descriptor.account_name), "%s", "acct");
    std::snprintf(retry_descriptor.account_email,
        sizeof(retry_descriptor.account_email), "%s", "player@example.com");

    char bad_code[] = "000000";
    nanny(&retry_descriptor, bad_code);

    EXPECT_EQ(retry_descriptor.connected, CON_ACCTVERIFY);
    EXPECT_EQ(retry_descriptor.bad_pws, 4);
    EXPECT_STREQ(retry_descriptor.account_name, "acct");
    EXPECT_STREQ(retry_descriptor.account_email, "player@example.com");
    EXPECT_NE(std::string(retry_descriptor.output).find("Verification code (or type RESEND/CANCEL): "),
        std::string::npos);
    EXPECT_EQ(std::string(retry_descriptor.output).find("Account:"), std::string::npos);

    descriptor_data closing_descriptor = make_descriptor();
    closing_descriptor.connected = CON_ACCTVERIFY;
    closing_descriptor.bad_pws = 4;
    std::snprintf(
        closing_descriptor.account_name, sizeof(closing_descriptor.account_name), "%s", "acct");
    std::snprintf(closing_descriptor.account_email,
        sizeof(closing_descriptor.account_email), "%s", "player@example.com");

    char final_bad_code[] = "000000";
    nanny(&closing_descriptor, final_bad_code);

    EXPECT_EQ(closing_descriptor.connected, CON_CLOSE);
    EXPECT_EQ(closing_descriptor.bad_pws, 5);
    EXPECT_STREQ(closing_descriptor.account_name, "acct");
    EXPECT_STREQ(closing_descriptor.account_email, "player@example.com");
    EXPECT_NE(std::string(closing_descriptor.output).find("Too many invalid verification attempts"),
        std::string::npos);
    EXPECT_EQ(std::string(closing_descriptor.output).find("Account:"), std::string::npos);
}

TEST(InterpreAccountMenu, AccountCreationPasswordMismatchClearsStagedPasswordAndCreatesNoAccount)
{
    TemporaryDirectory temp_directory;
    ScopedWorkingDirectory working_directory(temp_directory.path());
    ASSERT_EQ(mkdir("accounts", 0700), 0);
    ASSERT_EQ(mkdir("accounts/A-E", 0700), 0);

    descriptor_data descriptor = make_descriptor();
    descriptor.connected = CON_ACCTNEWPWDCNF;
    std::snprintf(
        descriptor.account_email, sizeof(descriptor.account_email), "%s", "player@example.com");
    std::snprintf(
        descriptor.account_password, sizeof(descriptor.account_password), "%s", "ValidPass1");

    char confirmation[] = "DifferentPass1";
    nanny(&descriptor, confirmation);

    EXPECT_EQ(descriptor.connected, CON_ACCTNEWPWD);
    EXPECT_STREQ(descriptor.account_email, "player@example.com");
    EXPECT_STREQ(descriptor.account_password, "");
    EXPECT_NE(std::string(descriptor.output).find("Passwords don't match"), std::string::npos);

    account::AccountData account_data;
    std::string error_message;
    EXPECT_FALSE(account::read_account_file_by_email(
        ".", "player@example.com", &account_data, &error_message));
}

TEST(InterpreAccountMenu, AccountCreationBlankPasswordClearsStaleStagedPassword)
{
    TemporaryDirectory temp_directory;
    ScopedWorkingDirectory working_directory(temp_directory.path());
    ASSERT_EQ(mkdir("accounts", 0700), 0);
    ASSERT_EQ(mkdir("accounts/A-E", 0700), 0);

    descriptor_data descriptor = make_descriptor();
    descriptor.connected = CON_ACCTNEWPWD;
    std::snprintf(
        descriptor.account_email, sizeof(descriptor.account_email), "%s", "player@example.com");
    std::snprintf(
        descriptor.account_password, sizeof(descriptor.account_password), "%s", "stale-secret");

    char password[] = "";
    nanny(&descriptor, password);

    EXPECT_EQ(descriptor.connected, CON_ACCTNEWPWD);
    EXPECT_STREQ(descriptor.account_email, "player@example.com");
    EXPECT_STREQ(descriptor.account_password, "");
    EXPECT_NE(std::string(descriptor.output).find("Illegal password."), std::string::npos);

    account::AccountData account_data;
    std::string error_message;
    EXPECT_FALSE(account::read_account_file_by_email(
        ".", "player@example.com", &account_data, &error_message));
}

TEST(InterpreAccountMenu, AccountCreationWeakPasswordClearsStaleStagedPassword)
{
    TemporaryDirectory temp_directory;
    ScopedWorkingDirectory working_directory(temp_directory.path());
    ASSERT_EQ(mkdir("accounts", 0700), 0);
    ASSERT_EQ(mkdir("accounts/A-E", 0700), 0);

    descriptor_data descriptor = make_descriptor();
    descriptor.connected = CON_ACCTNEWPWD;
    std::snprintf(
        descriptor.account_email, sizeof(descriptor.account_email), "%s", "player@example.com");
    std::snprintf(
        descriptor.account_password, sizeof(descriptor.account_password), "%s", "stale-secret");

    char password[] = "lowercase1";
    nanny(&descriptor, password);

    EXPECT_EQ(descriptor.connected, CON_ACCTNEWPWD);
    EXPECT_STREQ(descriptor.account_email, "player@example.com");
    EXPECT_STREQ(descriptor.account_password, "");
    EXPECT_NE(std::string(descriptor.output).find("Please enter a password: "), std::string::npos);

    account::AccountData account_data;
    std::string error_message;
    EXPECT_FALSE(account::read_account_file_by_email(
        ".", "player@example.com", &account_data, &error_message));
}

TEST(InterpreAccountMenu, AccountCreationSuccessClearsStagedPasswordAndPromptsForVerification)
{
    TemporaryDirectory temp_directory;
    ScopedWorkingDirectory working_directory(temp_directory.path());
    ScopedEnvironmentVariable sendmail_override("ROTS_SENDMAIL_COMMAND", "/bin/true");

    descriptor_data descriptor = make_descriptor();
    descriptor.connected = CON_ACCTNEWPWDCNF;
    std::snprintf(
        descriptor.account_email, sizeof(descriptor.account_email), "%s", "Player@Example.COM");
    std::snprintf(
        descriptor.account_password, sizeof(descriptor.account_password), "%s", "ValidPass1");

    char confirmation[] = "ValidPass1";
    nanny(&descriptor, confirmation);

    EXPECT_EQ(descriptor.connected, CON_ACCTVERIFY);
    EXPECT_STREQ(descriptor.account_email, "player@example.com");
    EXPECT_STREQ(descriptor.account_password, "");
    EXPECT_NE(std::string(descriptor.output).find("Account created."), std::string::npos);
    EXPECT_NE(std::string(descriptor.output).find("Verification code"), std::string::npos);

    account::AccountData account_data;
    std::string error_message;
    ASSERT_TRUE(account::read_account_file_by_email(
        ".", "player@example.com", &account_data, &error_message))
        << error_message;
    EXPECT_EQ(account_data.account_name, descriptor.account_name);
    EXPECT_TRUE(account::verify_password("ValidPass1", account_data.password_hash));
}

TEST(InterpreAccountMenu, AccountResetBlankCurrentPasswordCancelsWithoutChangingPassword)
{
    TemporaryDirectory temp_directory;
    ScopedWorkingDirectory working_directory(temp_directory.path());
    ASSERT_EQ(mkdir("accounts", 0700), 0);
    ASSERT_EQ(mkdir("accounts/A-E", 0700), 0);

    account::AccountData stored_account;
    std::string error_message;
    ASSERT_TRUE(account::create_account(".", "acct", "player@example.com", "ValidPass1",
        1700010200, &stored_account, &error_message))
        << error_message;
    const std::string original_hash = stored_account.password_hash;

    descriptor_data descriptor = make_descriptor();
    descriptor.connected = CON_ACCTRESETOLD;
    std::snprintf(descriptor.account_name, sizeof(descriptor.account_name), "%s", "acct");
    std::snprintf(
        descriptor.account_email, sizeof(descriptor.account_email), "%s", "player@example.com");
    std::snprintf(descriptor.account_password,
        sizeof(descriptor.account_password), "%s", "stale-reset-secret");

    char blank[] = "";
    nanny(&descriptor, blank);

    account::AccountData reloaded_account;
    ASSERT_TRUE(account::read_account_file(".", "acct", &reloaded_account, &error_message))
        << error_message;
    EXPECT_EQ(descriptor.connected, CON_ACCTMENU);
    EXPECT_STREQ(descriptor.account_name, "acct");
    EXPECT_STREQ(descriptor.account_email, "player@example.com");
    EXPECT_STREQ(descriptor.account_password, "");
    EXPECT_EQ(reloaded_account.password_hash, original_hash);
    EXPECT_TRUE(account::verify_password("ValidPass1", reloaded_account.password_hash));
    EXPECT_NE(std::string(descriptor.output).find("Password reset cancelled."), std::string::npos);
}

TEST(InterpreAccountMenu, AccountResetBlankNewPasswordClearsStaleStagedPassword)
{
    TemporaryDirectory temp_directory;
    ScopedWorkingDirectory working_directory(temp_directory.path());
    ASSERT_EQ(mkdir("accounts", 0700), 0);
    ASSERT_EQ(mkdir("accounts/A-E", 0700), 0);

    account::AccountData stored_account;
    std::string error_message;
    ASSERT_TRUE(account::create_account(".", "acct", "player@example.com", "ValidPass1",
        1700010200, &stored_account, &error_message))
        << error_message;
    const std::string original_hash = stored_account.password_hash;

    descriptor_data descriptor = make_descriptor();
    descriptor.connected = CON_ACCTRESETNEW;
    std::snprintf(descriptor.account_name, sizeof(descriptor.account_name), "%s", "acct");
    std::snprintf(
        descriptor.account_email, sizeof(descriptor.account_email), "%s", "player@example.com");
    std::snprintf(descriptor.account_password,
        sizeof(descriptor.account_password), "%s", "stale-reset-secret");

    char password[] = "";
    nanny(&descriptor, password);

    account::AccountData reloaded_account;
    ASSERT_TRUE(account::read_account_file(".", "acct", &reloaded_account, &error_message))
        << error_message;
    EXPECT_EQ(descriptor.connected, CON_ACCTRESETNEW);
    EXPECT_STREQ(descriptor.account_name, "acct");
    EXPECT_STREQ(descriptor.account_email, "player@example.com");
    EXPECT_STREQ(descriptor.account_password, "");
    EXPECT_EQ(reloaded_account.password_hash, original_hash);
    EXPECT_TRUE(account::verify_password("ValidPass1", reloaded_account.password_hash));
    EXPECT_NE(std::string(descriptor.output).find("Illegal password."), std::string::npos);
}

TEST(InterpreAccountMenu, AccountResetWeakNewPasswordClearsStaleStagedPassword)
{
    TemporaryDirectory temp_directory;
    ScopedWorkingDirectory working_directory(temp_directory.path());
    ASSERT_EQ(mkdir("accounts", 0700), 0);
    ASSERT_EQ(mkdir("accounts/A-E", 0700), 0);

    account::AccountData stored_account;
    std::string error_message;
    ASSERT_TRUE(account::create_account(".", "acct", "player@example.com", "ValidPass1",
        1700010200, &stored_account, &error_message))
        << error_message;
    const std::string original_hash = stored_account.password_hash;

    descriptor_data descriptor = make_descriptor();
    descriptor.connected = CON_ACCTRESETNEW;
    std::snprintf(descriptor.account_name, sizeof(descriptor.account_name), "%s", "acct");
    std::snprintf(
        descriptor.account_email, sizeof(descriptor.account_email), "%s", "player@example.com");
    std::snprintf(descriptor.account_password,
        sizeof(descriptor.account_password), "%s", "stale-reset-secret");

    char password[] = "lowercase1";
    nanny(&descriptor, password);

    account::AccountData reloaded_account;
    ASSERT_TRUE(account::read_account_file(".", "acct", &reloaded_account, &error_message))
        << error_message;
    EXPECT_EQ(descriptor.connected, CON_ACCTRESETNEW);
    EXPECT_STREQ(descriptor.account_name, "acct");
    EXPECT_STREQ(descriptor.account_email, "player@example.com");
    EXPECT_STREQ(descriptor.account_password, "");
    EXPECT_EQ(reloaded_account.password_hash, original_hash);
    EXPECT_TRUE(account::verify_password("ValidPass1", reloaded_account.password_hash));
    EXPECT_NE(std::string(descriptor.output).find("New account password: "), std::string::npos);
}

TEST(InterpreAccountMenu, LegacyLinkBlankPasswordCancelsWithoutCreatingLink)
{
    TemporaryDirectory temp_directory;
    ScopedWorkingDirectory working_directory(temp_directory.path());
    ASSERT_EQ(mkdir("accounts", 0700), 0);
    ASSERT_EQ(mkdir("accounts/A-E", 0700), 0);

    account::AccountData stored_account;
    std::string error_message;
    ASSERT_TRUE(account::create_account(".", "acct", "player@example.com", "ValidPass1",
        1700010200, &stored_account, &error_message))
        << error_message;

    descriptor_data descriptor = make_descriptor();
    descriptor.connected = CON_ACCTLEGPWD;
    std::snprintf(descriptor.account_name, sizeof(descriptor.account_name), "%s", "acct");
    std::snprintf(
        descriptor.account_email, sizeof(descriptor.account_email), "%s", "player@example.com");
    std::snprintf(descriptor.account_character_name,
        sizeof(descriptor.account_character_name), "%s", "aragorn");

    char blank[] = "";
    nanny(&descriptor, blank);

    account::AccountData reloaded_account;
    ASSERT_TRUE(account::read_account_file(".", "acct", &reloaded_account, &error_message))
        << error_message;
    EXPECT_EQ(descriptor.connected, CON_ACCTMENU);
    EXPECT_STREQ(descriptor.account_name, "acct");
    EXPECT_STREQ(descriptor.account_email, "player@example.com");
    EXPECT_STREQ(descriptor.account_character_name, "");
    EXPECT_FALSE(account::account_has_character(reloaded_account, "aragorn"));
    EXPECT_TRUE(reloaded_account.character_links.empty());
    EXPECT_NE(std::string(descriptor.output).find("Character linking cancelled."),
        std::string::npos);
}

TEST(InterpreAccountMenu, LegacyLinkWrongPasswordClearsPendingCharacterWithoutCreatingLink)
{
    TemporaryDirectory temp_directory;
    ScopedWorkingDirectory working_directory(temp_directory.path());
    ScopedPlayerTableReset player_table_reset;
    ASSERT_EQ(mkdir("players", 0700), 0);
    ASSERT_EQ(mkdir("players/A-E", 0700), 0);

    account::AccountData stored_account;
    std::string error_message;
    ASSERT_TRUE(account::create_account(".", "acct", "player@example.com", "ValidPass1",
        1700010200, &stored_account, &error_message))
        << error_message;

    char_file_u legacy_character = make_stored_character("aragorn", 50, RACE_WOOD);
    legacy_character.specials2.idnum = 4242;
    legacy_character.last_logon = 1700010202;
    const std::string legacy_player_path
        = write_valid_legacy_player_file(temp_directory.path(), legacy_character);
    const int legacy_player_index = create_entry(const_cast<char*>("aragorn"));
    ASSERT_GE(legacy_player_index, 0);
    std::snprintf(player_table[legacy_player_index].ch_file,
        sizeof(player_table[legacy_player_index].ch_file), "%s", legacy_player_path.c_str());
    player_table[legacy_player_index].level = legacy_character.level;
    player_table[legacy_player_index].race = legacy_character.race;
    player_table[legacy_player_index].idnum = legacy_character.specials2.idnum;
    player_table[legacy_player_index].log_time = legacy_character.last_logon;
    player_table[legacy_player_index].flags = legacy_character.specials2.act;

    descriptor_data descriptor = make_descriptor();
    descriptor.connected = CON_ACCTLEGPWD;
    std::snprintf(descriptor.account_name, sizeof(descriptor.account_name), "%s", "acct");
    std::snprintf(
        descriptor.account_email, sizeof(descriptor.account_email), "%s", "player@example.com");
    std::snprintf(descriptor.account_character_name,
        sizeof(descriptor.account_character_name), "%s", "aragorn");

    char wrong_password[] = "WrongLegacy1";
    nanny(&descriptor, wrong_password);

    account::AccountData reloaded_account;
    ASSERT_TRUE(account::read_account_file(".", "acct", &reloaded_account, &error_message))
        << error_message;
    EXPECT_EQ(descriptor.connected, CON_ACCTMENU);
    EXPECT_STREQ(descriptor.account_name, "acct");
    EXPECT_STREQ(descriptor.account_email, "player@example.com");
    EXPECT_STREQ(descriptor.account_character_name, "");
    EXPECT_FALSE(account::account_has_character(reloaded_account, "aragorn"));
    EXPECT_TRUE(reloaded_account.character_links.empty());
    EXPECT_NE(std::string(descriptor.output).find("Incorrect legacy character password."),
        std::string::npos);
}

TEST(InterpreAccountMenu, LegacyLinkMalformedObjectMigrationFailureClearsPendingCharacterWithoutCreatingLink)
{
    TemporaryDirectory temp_directory;
    ScopedWorkingDirectory working_directory(temp_directory.path());
    ScopedPlayerTableReset player_table_reset;
    ASSERT_EQ(mkdir("players", 0700), 0);
    ASSERT_EQ(mkdir("players/A-E", 0700), 0);
    ASSERT_EQ(mkdir("plrobjs", 0700), 0);
    ASSERT_EQ(mkdir("plrobjs/A-E", 0700), 0);

    account::AccountData stored_account;
    std::string error_message;
    ASSERT_TRUE(account::create_account(".", "acct", "player@example.com", "ValidPass1",
        1700010200, &stored_account, &error_message))
        << error_message;

    char_file_u legacy_character = make_stored_character("aragorn", 50, RACE_WOOD);
    legacy_character.specials2.idnum = 4242;
    legacy_character.last_logon = 1700010202;
    const std::string legacy_player_path
        = write_valid_legacy_player_file(temp_directory.path(), legacy_character);
    write_text_file(account::legacy_object_file_path(".", "aragorn"), "bad");

    const int legacy_player_index = create_entry(const_cast<char*>("aragorn"));
    ASSERT_GE(legacy_player_index, 0);
    std::snprintf(player_table[legacy_player_index].ch_file,
        sizeof(player_table[legacy_player_index].ch_file), "%s", legacy_player_path.c_str());
    player_table[legacy_player_index].level = legacy_character.level;
    player_table[legacy_player_index].race = legacy_character.race;
    player_table[legacy_player_index].idnum = legacy_character.specials2.idnum;
    player_table[legacy_player_index].log_time = legacy_character.last_logon;
    player_table[legacy_player_index].flags = legacy_character.specials2.act;

    descriptor_data descriptor = make_descriptor();
    descriptor.connected = CON_ACCTLEGPWD;
    std::snprintf(descriptor.account_name, sizeof(descriptor.account_name), "%s", "acct");
    std::snprintf(
        descriptor.account_email, sizeof(descriptor.account_email), "%s", "player@example.com");
    std::snprintf(descriptor.account_character_name,
        sizeof(descriptor.account_character_name), "%s", "aragorn");

    char password[] = "LegacyPw1";
    nanny(&descriptor, password);

    account::AccountData reloaded_account;
    ASSERT_TRUE(account::read_account_file(".", "acct", &reloaded_account, &error_message))
        << error_message;
    EXPECT_EQ(descriptor.connected, CON_ACCTMENU);
    EXPECT_STREQ(descriptor.account_name, "acct");
    EXPECT_STREQ(descriptor.account_email, "player@example.com");
    EXPECT_STREQ(descriptor.account_character_name, "");
    EXPECT_FALSE(account::account_has_character(reloaded_account, "aragorn"));
    EXPECT_TRUE(reloaded_account.characters.empty());
    EXPECT_TRUE(reloaded_account.character_links.empty());
    EXPECT_NE(std::string(descriptor.output).find("Truncated objects data"), std::string::npos);

    struct stat file_info {};
    EXPECT_EQ(stat(legacy_player_path.c_str(), &file_info), 0);
    EXPECT_EQ(stat(account::legacy_object_file_path(".", "aragorn").c_str(), &file_info), 0);
    EXPECT_NE(stat(account::account_character_player_path(".", "acct", "aragorn").c_str(), &file_info), 0);
    EXPECT_NE(stat(account::account_character_object_path(".", "acct", "aragorn").c_str(), &file_info), 0);
    EXPECT_NE(stat(account::account_character_exploits_path(".", "acct", "aragorn").c_str(), &file_info), 0);
}

TEST(InterpreAccountMenu, DeletePasswordInputIsHiddenFromSnoopers)
{
    TemporaryDirectory temp_directory;
    ScopedCommandLog command_log(temp_directory.path() + "/last_cmds");

    const SnoopProbeResult visible_result
        = snoop_output_for_input_state(CON_ACCTMENU, "visible-input");
    EXPECT_EQ(visible_result.queued_input, "visible-input");
    EXPECT_NE(visible_result.snoop_output.find("% visible-input\n\r"), std::string::npos)
        << visible_result.snoop_output;

    const SnoopProbeResult secret_result
        = snoop_output_for_input_state(CON_ACCTDELCNF1, "DeletePass1");
    EXPECT_EQ(secret_result.queued_input, "DeletePass1");
    EXPECT_EQ(secret_result.last_input, "");
    EXPECT_EQ(secret_result.snoop_output.find("DeletePass1"), std::string::npos)
        << secret_result.snoop_output;
    EXPECT_EQ(secret_result.snoop_output.find("% "), std::string::npos)
        << secret_result.snoop_output;

    const SnoopProbeResult legacy_secret_result
        = snoop_output_for_input_state(CON_DELCNF1, "LegacyPass1");
    EXPECT_EQ(legacy_secret_result.queued_input, "LegacyPass1");
    EXPECT_EQ(legacy_secret_result.last_input, "");
    EXPECT_EQ(legacy_secret_result.snoop_output.find("LegacyPass1"), std::string::npos)
        << legacy_secret_result.snoop_output;
    EXPECT_EQ(legacy_secret_result.snoop_output.find("% "), std::string::npos)
        << legacy_secret_result.snoop_output;

    const SnoopReplayResult replay_result
        = snoop_replay_after_secret_input_state(CON_ACCTDELCNF1, "DeletePass1");
    EXPECT_EQ(replay_result.secret_input, "DeletePass1");
    EXPECT_EQ(replay_result.replayed_input, "look");
    EXPECT_EQ(replay_result.last_input, "look");
    EXPECT_EQ(replay_result.snoop_output.find("DeletePass1"), std::string::npos)
        << replay_result.snoop_output;
    EXPECT_NE(replay_result.snoop_output.find("% look\n\r"), std::string::npos)
        << replay_result.snoop_output;
}

TEST(InterpreAccountMenu, SuccessfulAccountLoginLogsEmailAndHost)
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
    descriptor.connected = CON_ACCTPWD;
    std::snprintf(descriptor.account_email, sizeof(descriptor.account_email), "%s", "player@example.com");
    std::snprintf(descriptor.host, sizeof(descriptor.host), "%s", "127.0.0.1");

    const std::string stderr_path = temp_directory.path() + "/account-login.stderr";
    char password[] = "ValidPass1";
    std::string stderr_output;
    {
        ScopedStderrRedirect stderr_redirect(stderr_path);
        nanny(&descriptor, password);
        stderr_output = stderr_redirect.read_contents();
    }

    EXPECT_EQ(descriptor.connected, CON_ACCTMENU);
    EXPECT_NE(stderr_output.find("Account login for player@example.com [127.0.0.1]"), std::string::npos) << stderr_output;
    EXPECT_EQ(count_occurrences(stderr_output, "Account login for player@example.com [127.0.0.1]"), 1u) << stderr_output;
}

TEST(InterpreAccountMenu, AccountLogoutLogsEmailAndHostExactlyOnce)
{
    descriptor_data descriptor = make_descriptor();
    descriptor.connected = CON_ACCTMENU;
    std::snprintf(descriptor.account_name, sizeof(descriptor.account_name), "%s", "acct");
    std::snprintf(descriptor.account_email, sizeof(descriptor.account_email), "%s", "player@example.com");
    std::snprintf(descriptor.host, sizeof(descriptor.host), "%s", "127.0.0.1");

    TemporaryDirectory temp_directory;
    ScopedWorkingDirectory working_directory(temp_directory.path());
    ASSERT_EQ(mkdir("accounts", 0700), 0);
    ASSERT_EQ(mkdir("accounts/A-E", 0700), 0);

    account::AccountData stored_account;
    std::string error_message;
    ASSERT_TRUE(account::create_account(".", "acct", "player@example.com", "ValidPass1", 1700010200, &stored_account, &error_message)) << error_message;

    const std::string stderr_path = temp_directory.path() + "/account-logout.stderr";
    char choice[] = "0";
    std::string stderr_output;
    {
        ScopedStderrRedirect stderr_redirect(stderr_path);
        nanny(&descriptor, choice);
        stderr_output = stderr_redirect.read_contents();
    }

    EXPECT_EQ(descriptor.connected, CON_NME);
    EXPECT_EQ(std::string(descriptor.output), "Account email: ");
    EXPECT_NE(stderr_output.find("Account logout for player@example.com [127.0.0.1]"), std::string::npos) << stderr_output;
    EXPECT_EQ(count_occurrences(stderr_output, "Account logout for player@example.com [127.0.0.1]"), 1u) << stderr_output;
}

TEST(InterpreAccountMenu, InvalidAccountPasswordAttemptLogsEmailAndHost)
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
    descriptor.connected = CON_ACCTPWD;
    std::snprintf(descriptor.account_email, sizeof(descriptor.account_email), "%s", "player@example.com");
    std::snprintf(descriptor.host, sizeof(descriptor.host), "%s", "127.0.0.1");

    const std::string stderr_path = temp_directory.path() + "/bad-account-password.stderr";
    char password[] = "WrongPass1";
    std::string stderr_output;
    {
        ScopedStderrRedirect stderr_redirect(stderr_path);
        nanny(&descriptor, password);
        stderr_output = stderr_redirect.read_contents();
    }

    EXPECT_EQ(descriptor.connected, CON_ACCTPWD);
    EXPECT_NE(std::string(descriptor.output).find("Invalid account credentials.\n\r"), std::string::npos);
    EXPECT_NE(stderr_output.find("Bad account password for player@example.com [127.0.0.1]"), std::string::npos) << stderr_output;
    EXPECT_EQ(stderr_output.find("WrongPass1"), std::string::npos) << stderr_output;
}

TEST(InterpreAccountMenu, PendingVerificationAccountDoesNotLogBadPasswordAttempt)
{
    TemporaryDirectory temp_directory;
    ScopedWorkingDirectory working_directory(temp_directory.path());
    ASSERT_EQ(mkdir("accounts", 0700), 0);
    ASSERT_EQ(mkdir("accounts/A-E", 0700), 0);

    account::AccountData stored_account;
    std::string error_message;
    ASSERT_TRUE(account::create_account(".", "acct", "player@example.com", "ValidPass1", 1700010200, &stored_account, &error_message)) << error_message;

    descriptor_data descriptor = make_descriptor();
    descriptor.connected = CON_ACCTPWD;
    std::snprintf(descriptor.account_email, sizeof(descriptor.account_email), "%s", "player@example.com");
    std::snprintf(descriptor.host, sizeof(descriptor.host), "%s", "127.0.0.1");

    const std::string stderr_path = temp_directory.path() + "/pending-verification.stderr";
    char password[] = "ValidPass1";
    std::string stderr_output;
    {
        ScopedStderrRedirect stderr_redirect(stderr_path);
        nanny(&descriptor, password);
        stderr_output = stderr_redirect.read_contents();
    }

    EXPECT_NE(descriptor.connected, CON_ACCTPWD);
    EXPECT_EQ(std::string(descriptor.output).find("Invalid account credentials."), std::string::npos);
    EXPECT_EQ(stderr_output.find("Bad account password"), std::string::npos) << stderr_output;
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

TEST(InterpreAccountMenu, AccountPasswordResetRejectsIncorrectCurrentPasswordAndLogsAttemptWithoutLeakingSecret)
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
    descriptor.connected = CON_ACCTRESETOLD;
    std::snprintf(descriptor.account_name, sizeof(descriptor.account_name), "%s", "acct");
    std::snprintf(descriptor.account_email, sizeof(descriptor.account_email), "%s", "player@example.com");
    std::snprintf(descriptor.host, sizeof(descriptor.host), "%s", "127.0.0.1");

    const std::string stderr_path = temp_directory.path() + "/account-reset-old-failure.stderr";
    char password[] = "WrongPass1";
    std::string stderr_output;
    {
        ScopedStderrRedirect stderr_redirect(stderr_path);
        nanny(&descriptor, password);
        stderr_output = stderr_redirect.read_contents();
    }

    EXPECT_EQ(descriptor.connected, CON_ACCTMENU);
    EXPECT_NE(std::string(descriptor.output).find("Incorrect account password.\n\r"), std::string::npos);
    EXPECT_NE(stderr_output.find("Bad account password for player@example.com [127.0.0.1]"), std::string::npos) << stderr_output;
    EXPECT_EQ(stderr_output.find("WrongPass1"), std::string::npos) << stderr_output;
    EXPECT_EQ(stderr_output.find("Account password reset for player@example.com [127.0.0.1]"), std::string::npos) << stderr_output;
}

TEST(InterpreAccountMenu, AccountPasswordResetLogsEmailAndHost)
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
    descriptor.connected = CON_ACCTRESETCNF;
    std::snprintf(descriptor.account_name, sizeof(descriptor.account_name), "%s", "acct");
    std::snprintf(descriptor.account_email, sizeof(descriptor.account_email), "%s", "player@example.com");
    std::snprintf(descriptor.account_password, sizeof(descriptor.account_password), "%s", "ChangedPass2");
    std::snprintf(descriptor.host, sizeof(descriptor.host), "%s", "127.0.0.1");

    const std::string stderr_path = temp_directory.path() + "/account-reset.stderr";
    char confirmation[] = "ChangedPass2";
    std::string stderr_output;
    {
        ScopedStderrRedirect stderr_redirect(stderr_path);
        nanny(&descriptor, confirmation);
        stderr_output = stderr_redirect.read_contents();
    }

    account::AccountData reloaded_account;
    ASSERT_TRUE(account::read_account_file(".", "acct", &reloaded_account, &error_message)) << error_message;

    EXPECT_EQ(descriptor.connected, CON_ACCTMENU);
    EXPECT_STREQ(descriptor.account_password, "");
    EXPECT_TRUE(account::verify_password("ChangedPass2", reloaded_account.password_hash));
    EXPECT_NE(stderr_output.find("Account password reset for player@example.com [127.0.0.1]"), std::string::npos) << stderr_output;
    EXPECT_EQ(stderr_output.find("ChangedPass2"), std::string::npos) << stderr_output;
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

TEST(InterpreAccountMenu, AccountSelectionReplacesRentedCharacterShellSoStoredAffectsDoNotDouble)
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
    stored_character.affected[0].type = SPELL_SANCTUARY;
    stored_character.affected[0].duration = 8;
    stored_character.affected[0].modifier = 11;
    stored_character.affected[0].location = APPLY_NONE;
    stored_character.affected[0].bitvector = AFF_SANCTUARY;
    ASSERT_TRUE(account::write_account_character_file(".", "acct", stored_character, &error_message)) << error_message;
    ASSERT_TRUE(account::write_default_account_object_file(".", "acct", "aragorn", &error_message)) << error_message;
    ASSERT_TRUE(account::admin_link_character(".", "acct", "aragorn", 1700010201, &stored_account, &error_message)) << error_message;

    descriptor_data descriptor = make_descriptor();
    descriptor.connected = CON_ACCTSLCT;
    descriptor.character = new char_data {};
    clear_char(descriptor.character, MOB_VOID);
    register_pc_char(descriptor.character);
    descriptor.character->desc = &descriptor;
    descriptor.character->player.name = strdup("aragorn");
    std::snprintf(descriptor.host, sizeof(descriptor.host), "%s", "127.0.0.1");

    affected_type stale_affect {};
    stale_affect.type = SPELL_SANCTUARY;
    stale_affect.duration = 99;
    stale_affect.modifier = 22;
    stale_affect.location = APPLY_NONE;
    stale_affect.bitvector = AFF_SANCTUARY;
    affect_to_char(descriptor.character, &stale_affect);
    ASSERT_EQ(count_affects(descriptor.character), 1u);

    char choice[] = "1";
    nanny(&descriptor, choice);

    ASSERT_EQ(descriptor.connected, CON_SLCT);
    ASSERT_NE(descriptor.character, nullptr);
    ASSERT_NE(descriptor.character->affected, nullptr);
    EXPECT_EQ(count_affects(descriptor.character), 1u);
    EXPECT_EQ(descriptor.character->affected->type, SPELL_SANCTUARY);
    EXPECT_EQ(descriptor.character->affected->modifier, 11);
    EXPECT_EQ(descriptor.character->affected->duration, 8);
    EXPECT_EQ(descriptor.character->affected->bitvector, AFF_SANCTUARY);

    free_char(descriptor.character);
    descriptor.character = nullptr;
}

TEST(InterpreAccountMenu, ReturningToAccountAndReselectingSameCharacterDoesNotDuplicateStoredAffects)
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
    stored_character.affected[0].type = SPELL_SANCTUARY;
    stored_character.affected[0].duration = 8;
    stored_character.affected[0].modifier = 11;
    stored_character.affected[0].location = APPLY_NONE;
    stored_character.affected[0].bitvector = AFF_SANCTUARY;
    ASSERT_TRUE(account::write_account_character_file(".", "acct", stored_character, &error_message)) << error_message;
    ASSERT_TRUE(account::write_default_account_object_file(".", "acct", "aragorn", &error_message)) << error_message;
    ASSERT_TRUE(account::admin_link_character(".", "acct", "aragorn", 1700010201, &stored_account, &error_message)) << error_message;

    descriptor_data descriptor = make_descriptor();
    descriptor.connected = CON_ACCTSLCT;
    descriptor.character = new char_data {};
    clear_char(descriptor.character, MOB_VOID);
    register_pc_char(descriptor.character);
    descriptor.character->desc = &descriptor;
    descriptor.character->player.name = strdup("aragorn");
    std::snprintf(descriptor.host, sizeof(descriptor.host), "%s", "127.0.0.1");

    char select_character[] = "1";
    nanny(&descriptor, select_character);
    ASSERT_EQ(descriptor.connected, CON_SLCT);
    ASSERT_NE(descriptor.character, nullptr);
    ASSERT_NE(descriptor.character->affected, nullptr);
    EXPECT_EQ(count_affects(descriptor.character), 1u);
    EXPECT_EQ(descriptor.character->affected->modifier, 11);

    descriptor.output[0] = '\0';
    descriptor.bufptr = 0;
    descriptor.bufspace = SMALL_BUFSIZE - 1;
    char back_to_account[] = "0";
    nanny(&descriptor, back_to_account);
    ASSERT_EQ(descriptor.connected, CON_ACCTMENU);

    descriptor.output[0] = '\0';
    descriptor.bufptr = 0;
    descriptor.bufspace = SMALL_BUFSIZE - 1;
    char play_choice[] = "2";
    nanny(&descriptor, play_choice);
    ASSERT_EQ(descriptor.connected, CON_ACCTSLCT);

    descriptor.output[0] = '\0';
    descriptor.bufptr = 0;
    descriptor.bufspace = SMALL_BUFSIZE - 1;
    nanny(&descriptor, select_character);
    ASSERT_EQ(descriptor.connected, CON_SLCT);
    ASSERT_NE(descriptor.character, nullptr);
    ASSERT_NE(descriptor.character->affected, nullptr);
    EXPECT_EQ(count_affects(descriptor.character), 1u);
    EXPECT_EQ(descriptor.character->affected->modifier, 11);
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
    descriptor_data descriptor = make_descriptor();
    descriptor.connected = CON_QSEX;
    character->desc = &descriptor;

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
    ensure_test_world_room(1200);
    create_entry(const_cast<char*>("existingplayer"));
    create_entry(const_cast<char*>("secondplayer"));
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
    EXPECT_NE(stat("players", &file_info), 0)
        << "Account-born characters should not require a legacy players directory.";
    EXPECT_NE(stat("plrobjs", &file_info), 0)
        << "Account-born characters should not require a legacy object-save directory.";
    EXPECT_NE(stat("exploits", &file_info), 0)
        << "Account-born characters should not require a legacy exploit-history directory.";
    EXPECT_NE(stat(account::legacy_player_file_path(".", "aragorn").c_str(), &file_info), 0);
    EXPECT_NE(stat(account::legacy_object_file_path(".", "aragorn").c_str(), &file_info), 0);
    EXPECT_NE(stat(account::legacy_exploits_file_path(".", "aragorn").c_str(), &file_info), 0);

    account::AccountData reloaded_account;
    ASSERT_TRUE(account::read_account_file(".", "acct", &reloaded_account, &error_message)) << error_message;
    ASSERT_EQ(reloaded_account.characters.size(), 1u);
    EXPECT_EQ(reloaded_account.characters[0], "aragorn");
    ASSERT_EQ(reloaded_account.character_links.size(), 1u);
    EXPECT_EQ(reloaded_account.character_links[0].character_name, "aragorn");
    EXPECT_EQ(reloaded_account.character_links[0].character_path, "aragorn.character.json");
    EXPECT_EQ(reloaded_account.character_links[0].object_path, "aragorn.objects.json");
    EXPECT_EQ(reloaded_account.character_links[0].exploits_path, "aragorn.exploits.json");

    const std::string account_character_path = account::account_character_player_path(".", "acct", "aragorn");
    ASSERT_GE(descriptor.pos, 0);
    EXPECT_STREQ(player_table[descriptor.pos].name, "aragorn");
    EXPECT_STREQ(player_table[descriptor.pos].ch_file, account_character_path.c_str());
    EXPECT_EQ(stat(player_table[descriptor.pos].ch_file, &file_info), 0)
        << "Account-born characters should enter the live player index through their account-native character.json.";

    char_file_u stored_character {};
    ASSERT_TRUE(account::read_account_character_file(".", "acct", "aragorn", &stored_character, &error_message)) << error_message;
    EXPECT_EQ(stored_character.specials2.load_room, 1200);
    EXPECT_EQ(stored_character.specials2.rawPerception, descriptor.character->specials2.rawPerception);
    EXPECT_EQ(stored_character.specials2.perception, descriptor.character->specials2.perception);

    char load_name[] = "aragorn";
    char_file_u loaded_by_name {};
    ASSERT_GE(load_char(load_name, &loaded_by_name), 0)
        << "Same-process name loads for account-born characters should use account-native character.json, not a legacy birth file.";
    EXPECT_STREQ(loaded_by_name.name, "aragorn");
    EXPECT_EQ(loaded_by_name.specials2.idnum, stored_character.specials2.idnum);

    std::string object_bytes;
    ASSERT_TRUE(load_object_save_bytes_for_character(".", "aragorn", &object_bytes, &error_message)) << error_message;
    EXPECT_FALSE(object_bytes.empty());
    std::string account_object_bytes;
    ASSERT_TRUE(account::read_account_object_file(".", "acct", "aragorn", &account_object_bytes, &error_message)) << error_message;
    objects_json::ObjectSaveData account_object_data;
    ASSERT_TRUE(objects_json::object_save_data_from_binary(account_object_bytes, &account_object_data, &error_message)) << error_message;
    EXPECT_EQ(account_object_data.rent.rentcode, RENT_CRASH);
    EXPECT_TRUE(account_object_data.objects.empty());
    EXPECT_TRUE(account_object_data.aliases.empty());
    EXPECT_TRUE(account_object_data.followers.empty());

    std::vector<exploit_record> exploit_records;
    ASSERT_TRUE(account::read_account_exploit_file(".", "acct", "aragorn", &exploit_records, &error_message)) << error_message;
    ASSERT_EQ(exploit_records.size(), 1u);
    EXPECT_EQ(exploit_records[0].type, EXPLOIT_BIRTH);

    char_data* loaded_character = new char_data {};
    clear_char(loaded_character, MOB_VOID);
    store_to_char(&stored_character, loaded_character);
    descriptor_data loaded_descriptor = make_descriptor();
    std::snprintf(loaded_descriptor.account_name, sizeof(loaded_descriptor.account_name), "%s", "acct");
    loaded_character->desc = &loaded_descriptor;

    FILE* fp = nullptr;
    stage_account_backed_object_bytes_for_character(loaded_character, object_bytes.data(), object_bytes.size());
    fp = Crash_load(loaded_character);
    ASSERT_NE(fp, nullptr);
    EXPECT_EQ(std::fclose(fp), 0);
    EXPECT_EQ(loaded_character->specials2.load_room, 0);
    EXPECT_EQ(world[loaded_character->specials2.load_room].number, 1200);
    EXPECT_EQ(loaded_character->specials2.rawPerception, get_naked_perception(loaded_character));
    EXPECT_EQ(loaded_character->specials2.perception, get_naked_perception(loaded_character));
    EXPECT_NE(stat("players", &file_info), 0);
    EXPECT_NE(stat("plrobjs", &file_info), 0);
    EXPECT_NE(stat("exploits", &file_info), 0);

    free_char(descriptor.character);
    descriptor.character = nullptr;
    free_char(loaded_character);
}

TEST(InterpreAccountMenu, IntroduceCharRejectsTooLongAccountNativeIndexPathWithoutTruncation)
{
    TemporaryDirectory temp_directory;
    ScopedWorkingDirectory working_directory(temp_directory.path());
    ScopedPlayerTableReset player_table_reset;
    ScopedStartRoomOverride start_room_override(RACE_HUMAN, 0);

    ASSERT_EQ(mkdir("accounts", 0700), 0);
    ASSERT_EQ(mkdir("accounts/A-E", 0700), 0);
    ensure_test_world_room(1200);
    create_entry(const_cast<char*>("existingplayer"));
    create_entry(const_cast<char*>("secondplayer"));
    static char test_motd[] = "Test MOTD\r\n";
    ScopedMotdOverride motd_override(test_motd);

    const char* account_name = "abcdefghijklmnopqrst";
    const char* long_email = "abcdefghijklmnopqrst123456789012345678901234567890@example.com";
    std::string error_message;
    ASSERT_TRUE(account::create_account(".", account_name, long_email, "ValidPass1", 1700010200, nullptr, &error_message)) << error_message;
    const std::string account_character_path = account::account_character_player_path(".", account_name, "aragorn");
    ASSERT_GE(account_character_path.size(), sizeof(player_table[0].ch_file))
        << "Test setup must exceed the legacy player index path buffer.";

    descriptor_data descriptor = make_descriptor();
    descriptor.character = new char_data {};
    clear_char(descriptor.character, MOB_VOID);
    register_pc_char(descriptor.character);
    descriptor.character->desc = &descriptor;
    descriptor.connected = CON_QSEX;
    std::snprintf(descriptor.account_name, sizeof(descriptor.account_name), "%s", account_name);
    std::snprintf(descriptor.host, sizeof(descriptor.host), "%s", "127.0.0.1");
    std::snprintf(descriptor.pwd, sizeof(descriptor.pwd), "%s", "*ACCOUNT*");

    descriptor.character->player.sex = SEX_MALE;
    descriptor.character->player.race = RACE_HUMAN;
    descriptor.character->player.name = strdup("aragorn");

    introduce_char(&descriptor);

    EXPECT_EQ(descriptor.connected, CON_CLOSE);
    const std::string output = descriptor.output;
    EXPECT_NE(output.find("rolled back"), std::string::npos) << output;

    struct stat file_info {};
    EXPECT_NE(stat("players", &file_info), 0);
    EXPECT_NE(stat("plrobjs", &file_info), 0);
    EXPECT_NE(stat("exploits", &file_info), 0);
    EXPECT_NE(stat(account_character_path.c_str(), &file_info), 0);
    EXPECT_NE(stat(account::account_character_object_path(".", account_name, "aragorn").c_str(), &file_info), 0);
    EXPECT_NE(stat(account::account_character_exploits_path(".", account_name, "aragorn").c_str(), &file_info), 0);

    account::AccountData reloaded_account;
    ASSERT_TRUE(account::read_account_file(".", account_name, &reloaded_account, &error_message)) << error_message;
    EXPECT_TRUE(reloaded_account.characters.empty());
    EXPECT_TRUE(reloaded_account.character_links.empty());

    ASSERT_GE(descriptor.pos, 0);
    EXPECT_TRUE(IS_SET(player_table[descriptor.pos].flags, PLR_DELETED));
    EXPECT_EQ(player_table[descriptor.pos].ch_file[0], '\0')
        << "The live player index must fail closed instead of keeping a truncated account-native path.";

    free_char(descriptor.character);
    descriptor.character = nullptr;
}

TEST(InterpreAccountMenu, AccountSelectionRejectsTooLongAccountNativeIndexPathWithoutLegacyFallback)
{
    TemporaryDirectory temp_directory;
    ScopedWorkingDirectory working_directory(temp_directory.path());
    ScopedPlayerTableReset player_table_reset;

    ASSERT_EQ(mkdir("accounts", 0700), 0);
    ASSERT_EQ(mkdir("accounts/A-E", 0700), 0);

    const char* account_name = "abcdefghijklmnopqrst";
    const char* long_email = "abcdefghijklmnopqrst123456789012345678901234567890@example.com";
    std::string error_message;
    account::AccountData account_data;
    ASSERT_TRUE(account::create_account(".", account_name, long_email, "ValidPass1", 1700010200, &account_data, &error_message)) << error_message;

    char_file_u stored_character = make_stored_character("aragorn", 1, RACE_HUMAN);
    stored_character.specials2.idnum = 4242;
    ASSERT_TRUE(account::write_account_character_file(".", account_name, stored_character, &error_message)) << error_message;
    ASSERT_TRUE(account::write_default_account_object_file(".", account_name, "aragorn", &error_message)) << error_message;
    ASSERT_TRUE(account::write_default_account_exploit_file(".", account_name, "aragorn", &error_message)) << error_message;
    ASSERT_TRUE(account::admin_link_character(".", account_name, "aragorn", 1700010201, &account_data, &error_message)) << error_message;
    const std::string account_character_path = account::account_character_player_path(".", account_name, "aragorn");
    ASSERT_GE(account_character_path.size(), sizeof(player_table[0].ch_file))
        << "Test setup must exceed the legacy player index path buffer.";

    descriptor_data descriptor = make_descriptor();
    descriptor.connected = CON_ACCTSLCT;
    std::snprintf(descriptor.account_name, sizeof(descriptor.account_name), "%s", account_name);
    std::snprintf(descriptor.account_email, sizeof(descriptor.account_email), "%s", long_email);

    char selection[] = "1";
    nanny(&descriptor, selection);

    EXPECT_EQ(descriptor.connected, CON_ACCTSLCT);
    EXPECT_EQ(descriptor.character, nullptr);
    EXPECT_EQ(top_of_p_table, -1)
        << "Rejected account-native path should not create a live player index entry.";
    const std::string output = descriptor.output;
    EXPECT_NE(output.find("cannot be loaded from account storage"), std::string::npos) << output;
    EXPECT_NE(output.find("too long for the live player index"), std::string::npos) << output;
    struct stat file_info {};
    EXPECT_NE(stat("players", &file_info), 0);
    EXPECT_NE(stat("plrobjs", &file_info), 0);
    EXPECT_NE(stat("exploits", &file_info), 0);

    account::AccountData reloaded_account;
    ASSERT_TRUE(account::read_account_file(".", account_name, &reloaded_account, &error_message)) << error_message;
    ASSERT_EQ(reloaded_account.characters.size(), 1u);
    EXPECT_EQ(reloaded_account.characters[0], "aragorn");
    ASSERT_EQ(reloaded_account.character_links.size(), 1u);
    EXPECT_EQ(reloaded_account.character_links[0].character_path, "aragorn.character.json");

    char_file_u reloaded_character {};
    ASSERT_TRUE(account::read_account_character_file(".", account_name, "aragorn", &reloaded_character, &error_message)) << error_message;
    EXPECT_EQ(reloaded_character.specials2.idnum, stored_character.specials2.idnum);

    std::string account_object_bytes;
    ASSERT_TRUE(account::read_account_object_file(".", account_name, "aragorn", &account_object_bytes, &error_message)) << error_message;
    objects_json::ObjectSaveData account_object_data;
    ASSERT_TRUE(objects_json::object_save_data_from_binary(account_object_bytes, &account_object_data, &error_message)) << error_message;
    EXPECT_TRUE(account_object_data.objects.empty());

    std::vector<exploit_record> exploit_records;
    ASSERT_TRUE(account::read_account_exploit_file(".", account_name, "aragorn", &exploit_records, &error_message)) << error_message;
    EXPECT_TRUE(exploit_records.empty());
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

TEST(InterpreAccountMenu, IntroduceCharRejectsNameLinkedToAnotherAccountBeforeWritingAssets)
{
    TemporaryDirectory temp_directory;
    ScopedWorkingDirectory working_directory(temp_directory.path());
    ScopedPlayerTableReset player_table_reset;
    ScopedStartRoomOverride start_room_override(RACE_HUMAN, 0);

    ASSERT_EQ(mkdir("accounts", 0700), 0);
    ASSERT_EQ(mkdir("accounts/A-E", 0700), 0);
    ensure_test_world_room(1200);
    create_entry(const_cast<char*>("existingplayer"));
    create_entry(const_cast<char*>("secondplayer"));
    const int top_of_p_table_before_birth = top_of_p_table;
    static char test_motd[] = "Test MOTD\r\n";
    ScopedMotdOverride motd_override(test_motd);

    std::string error_message;
    account::AccountData target_account;
    ASSERT_TRUE(account::create_account(".", "acct", "player@example.com", "ValidPass1", 1700010200, &target_account, &error_message)) << error_message;
    account::AccountData owner_account;
    ASSERT_TRUE(account::create_account(".", "other", "owner@example.com", "ValidPass1", 1700010201, &owner_account, &error_message)) << error_message;
    char_file_u owner_character = make_stored_character("aragorn", 20, RACE_HUMAN);
    owner_character.specials2.idnum = 9999;
    ASSERT_TRUE(account::write_account_character_file(".", "other", owner_character, &error_message)) << error_message;
    ASSERT_TRUE(account::write_default_account_object_file(".", "other", "aragorn", &error_message)) << error_message;
    exploit_record owner_exploit {};
    owner_exploit.type = EXPLOIT_LEVEL;
    owner_exploit.iIntParam = 20;
    std::snprintf(owner_exploit.chtime, sizeof(owner_exploit.chtime), "%s", "Wed Jan  3 00:00:00 2024");
    std::vector<exploit_record> owner_exploits;
    owner_exploits.push_back(owner_exploit);
    ASSERT_TRUE(account::write_account_exploit_file(".", "other", "aragorn", owner_exploits, &error_message)) << error_message;
    ASSERT_TRUE(account::admin_link_character(".", "other", "aragorn", 1700010202, &owner_account, &error_message)) << error_message;

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

    EXPECT_EQ(descriptor.connected, CON_ACCTMENU);
    EXPECT_EQ(descriptor.character, nullptr);
    EXPECT_EQ(descriptor.pos, -1);
    EXPECT_EQ(top_of_p_table, top_of_p_table_before_birth)
        << "Linked-name rejection should run before create_entry().";
    const std::string output = descriptor.output;
    EXPECT_NE(output.find("That character name is already linked to an account."), std::string::npos) << output;
    EXPECT_NE(output.find("New character creation cancelled."), std::string::npos) << output;

    struct stat file_info {};
    EXPECT_NE(stat("players", &file_info), 0);
    EXPECT_NE(stat("plrobjs", &file_info), 0);
    EXPECT_NE(stat("exploits", &file_info), 0);
    EXPECT_NE(stat(account::account_character_player_path(".", "acct", "aragorn").c_str(), &file_info), 0);
    EXPECT_NE(stat(account::account_character_object_path(".", "acct", "aragorn").c_str(), &file_info), 0);
    EXPECT_NE(stat(account::account_character_exploits_path(".", "acct", "aragorn").c_str(), &file_info), 0);

    ASSERT_TRUE(account::read_account_file(".", "acct", &target_account, &error_message)) << error_message;
    EXPECT_TRUE(target_account.characters.empty());
    EXPECT_TRUE(target_account.character_links.empty());

    ASSERT_TRUE(account::read_account_file(".", "other", &owner_account, &error_message)) << error_message;
    ASSERT_EQ(owner_account.characters.size(), 1u);
    EXPECT_EQ(owner_account.characters[0], "aragorn");
    ASSERT_EQ(owner_account.character_links.size(), 1u);
    EXPECT_EQ(owner_account.character_links[0].character_path, "aragorn.character.json");
    EXPECT_EQ(owner_account.character_links[0].object_path, "aragorn.objects.json");
    EXPECT_EQ(owner_account.character_links[0].exploits_path, "aragorn.exploits.json");

    char_file_u preserved_owner_character {};
    ASSERT_TRUE(account::read_account_character_file(".", "other", "aragorn", &preserved_owner_character, &error_message)) << error_message;
    EXPECT_EQ(preserved_owner_character.specials2.idnum, owner_character.specials2.idnum);
    std::string preserved_owner_object_bytes;
    ASSERT_TRUE(account::read_account_object_file(".", "other", "aragorn", &preserved_owner_object_bytes, &error_message)) << error_message;
    objects_json::ObjectSaveData preserved_owner_object_data;
    ASSERT_TRUE(objects_json::object_save_data_from_binary(preserved_owner_object_bytes, &preserved_owner_object_data, &error_message)) << error_message;
    EXPECT_TRUE(preserved_owner_object_data.objects.empty());
    std::vector<exploit_record> preserved_owner_exploits;
    ASSERT_TRUE(account::read_account_exploit_file(".", "other", "aragorn", &preserved_owner_exploits, &error_message)) << error_message;
    ASSERT_EQ(preserved_owner_exploits.size(), 1u);
    EXPECT_EQ(preserved_owner_exploits[0].type, EXPLOIT_LEVEL);
    EXPECT_EQ(preserved_owner_exploits[0].iIntParam, 20);
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
