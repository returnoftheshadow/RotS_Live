#include "../account_management.h"
#include "../db.h"
#include "../handler.h"
#include "../mob_progs/banker.h"
#include "../objects_json.h"
#include "../signal_requests.h"
#include "../signals.h"
#include "../structs.h"
#include "../utils.h"
#include "scripted_room_fixture.h"
#include "test_cleanup.h"

#include <chrono>
#include <csignal>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <iterator>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

extern struct descriptor_data* descriptor_list;

namespace {

// The step names RecordingSignalSteps writes, one per call.
constexpr const char* STOP_STEP = "stop";
constexpr const char* REREAD_STEP = "reread_wizlists";
constexpr const char* UNRESTRICT_STEP = "unrestrict_game";
constexpr const char* IGNORED_STEP = "log_ignored_signal";

// Longer than any test waits for a stop, so a sleep that ignored the stop would fail the test.
constexpr std::chrono::seconds LONG_SLEEP(30);
// Room for a loaded or emulated machine to schedule the sleeping thread late.
constexpr std::chrono::seconds PROMPT_MARGIN(5);
// How long the other thread waits before recording its stop, so the sleep has begun.
constexpr std::chrono::milliseconds STOP_DELAY(50);
// A short sleep the tests expect to be served in full.
constexpr std::chrono::milliseconds SHORT_SLEEP(50);

// Steps that record which ones were asked for, in order, instead of doing the server's work.
class RecordingSignalSteps : public SignalRequestSteps {
public:
    void stop(int signal_number) override
    {
        steps_run.push_back(STOP_STEP);
        stop_signals.push_back(signal_number);
    }

    void reread_wizlists() override { steps_run.push_back(REREAD_STEP); }

    void unrestrict_game() override { steps_run.push_back(UNRESTRICT_STEP); }

    void log_ignored_signal() override { steps_run.push_back(IGNORED_STEP); }

    // The name of every step run, in the order it ran.
    std::vector<std::string> steps_run;
    // The signal number each stop step was given, in order.
    std::vector<int> stop_signals;
};

// Starts and ends each test with nothing recorded, since the requests are process-wide.
class SignalRequestsTest : public testing::Test {
protected:
    void SetUp() override { forget_recorded_requests(); }

    void TearDown() override { forget_recorded_requests(); }

    // Performs every recorded request with steps that do nothing lasting.
    static void forget_recorded_requests()
    {
        RecordingSignalSteps discarded_steps;
        perform_signal_requests(discarded_steps);
    }
};

// The account whose vault the stop test leaves behind.
constexpr const char* STOP_ACCOUNT = "stopper";
// The coins that vault holds, which its file holds only once written.
constexpr int STOP_VAULT_COINS = 1234;
// The playing players' level: a mortal's, below LEVEL_IMMORT.
constexpr int STOP_PLAYER_LEVEL = 10;
// The root the account and object code is given: the temporary folder the test runs in.
constexpr const char* SAVE_ROOT = ".";
// The script number make_object gives an object that runs no script.
constexpr int NO_SCRIPT = 0;
// The wear position saved_wear_position gives an object saved nowhere.
constexpr int NOT_SAVED = -1;
// The account the players are linked to, as every player on the game is.
constexpr const char* PLAYERS_ACCOUNT = "fellowship";
// That account's email address and password.
constexpr const char* PLAYERS_EMAIL = "fellowship@example.com";
constexpr const char* PLAYERS_PASSWORD = "ValidPass1";
// When the account was made and its characters linked, as the account records it.
constexpr long ACCOUNT_TIME = 1700010101;
// The largest saved file the stop test reads back.
constexpr std::uintmax_t SAVED_FILE_SIZE_LIMIT = 1 << 20;
// Every bucket the game files accounts, characters and objects under.
constexpr const char* SAVE_FOLDERS[] = { "accounts", "accounts/A-E", "accounts/F-J",
    "accounts/K-O", "accounts/P-T", "accounts/U-Z", "accounts/ZZZ", "players", "players/A-E",
    "players/F-J", "players/K-O", "players/P-T", "players/U-Z", "players/ZZZ", "plrobjs",
    "plrobjs/A-E", "plrobjs/F-J", "plrobjs/K-O", "plrobjs/P-T", "plrobjs/U-Z", "plrobjs/ZZZ" };
// The player index: the fixture's player, a second player in another room, and a player still
// choosing a character at the menus.
constexpr int PLAYER_ENTRY = 0;
constexpr int SECOND_PLAYER_ENTRY = 1;
constexpr int MENU_PLAYER_ENTRY = 2;
constexpr int PLAYER_ENTRY_COUNT = 3;

// Two playing players and one at the menus, all linked to one account, and another account with a
// vault, in a temporary folder the test runs in, so the stop's saves land there and go with it.
// The fixture's player stands in room 0; the second player stands in the second room.
class StopSavingTest : public scripted_room::ScriptedRoomFixture {
protected:
    void SetUp() override
    {
        // Saved first, so TearDown puts back the real index even when a step below fails.
        m_saved_player_table = player_table;
        m_saved_top_of_p_table = top_of_p_table;
        ScriptedRoomFixture::SetUp();
        link_second_room();

        std::error_code error;
        m_original_folder = std::filesystem::current_path(error);
        ASSERT_FALSE(error) << "no current folder: " << error.message();
        const std::filesystem::path temporary_root = std::filesystem::temp_directory_path(error);
        ASSERT_FALSE(error) << "no temporary folder: " << error.message();
        m_folder = temporary_root / "rots_stop_saving";
        std::filesystem::remove_all(m_folder, error);
        for (const char* subfolder : SAVE_FOLDERS) {
            std::filesystem::create_directories(m_folder / subfolder, error);
            ASSERT_FALSE(error) << "cannot make " << (m_folder / subfolder) << ": "
                                << error.message();
        }
        m_vault_folder = m_folder / "vaults" / STOP_ACCOUNT;
        std::filesystem::create_directories(m_vault_folder, error);
        ASSERT_FALSE(error) << "cannot make " << m_vault_folder << ": " << error.message();
        std::filesystem::current_path(m_folder, error);
        ASSERT_FALSE(error) << "cannot enter " << m_folder << ": " << error.message();

        bank_vault_forget_all_for_tests();
        const std::filesystem::path vault_folder = m_vault_folder;
        auto resolve_account = [vault_folder](const std::string& account_name) -> std::string {
            if (account_name != STOP_ACCOUNT) {
                return std::string();
            }
            return vault_folder.string();
        };
        bank_set_directory_resolver(resolve_account);

        m_player_entries[PLAYER_ENTRY].name = m_player.player.name;
        m_player_entries[SECOND_PLAYER_ENTRY].name = m_second_player_name;
        m_player_entries[MENU_PLAYER_ENTRY].name = m_menu_player_name;
        player_table = m_player_entries;
        top_of_p_table = PLAYER_ENTRY_COUNT - 1;

        std::string error_message;
        ASSERT_TRUE(account::create_account(SAVE_ROOT, PLAYERS_ACCOUNT, PLAYERS_EMAIL,
            PLAYERS_PASSWORD, ACCOUNT_TIME, nullptr, &error_message))
            << error_message;
        const char* const linked_names[] = { m_player.player.name, m_second_player_name,
            m_menu_player_name };
        for (const char* name : linked_names) {
            ASSERT_TRUE(account::admin_link_character(SAVE_ROOT, PLAYERS_ACCOUNT, name,
                ACCOUNT_TIME, nullptr, &error_message))
                << error_message;
        }

        join_game(m_second_player, m_second_descriptor, m_second_player_name, CON_PLYNG);
        m_second_player.in_room = scripted_room::SECOND_ROOM;
        world[scripted_room::SECOND_ROOM].people = &m_second_player;
        join_game(m_menu_player, m_menu_descriptor, m_menu_player_name, CON_SLCT);
        m_descriptor.next = &m_second_descriptor;
        m_second_descriptor.next = &m_menu_descriptor;
        descriptor_list = &m_descriptor;
    }

    void TearDown() override
    {
        player_table = m_saved_player_table;
        top_of_p_table = m_saved_top_of_p_table;
        for (char_data* player : { &m_second_player, &m_menu_player }) {
            while (player->carrying != nullptr) {
                extract_obj(player->carrying);
            }
            release_cleared_character(player);
        }
        release_test_descriptor(&m_second_descriptor);
        release_test_descriptor(&m_menu_descriptor);
        bank_vault_forget_all_for_tests();
        bank_set_directory_resolver(nullptr);
        std::error_code error;
        if (!m_original_folder.empty()) {
            std::filesystem::current_path(m_original_folder, error);
        }
        if (!m_folder.empty()) {
            std::filesystem::remove_all(m_folder, error);
        }
        ScriptedRoomFixture::TearDown();
    }

    // Makes <player> a mortal human named <name>, connected through <descriptor> in state
    // <connection_state>, standing in no room.
    static void join_game(char_data& player, descriptor_data& descriptor, char* name,
        int connection_state)
    {
        clear_char(&player, MOB_VOID);
        player.player.name = name;
        player.player.race = RACE_HUMAN;
        player.player.level = STOP_PLAYER_LEVEL;
        player.in_room = NOWHERE;
        descriptor.output = descriptor.small_outbuf;
        descriptor.small_outbuf[0] = '\0';
        descriptor.bufspace = SMALL_BUFSIZE - 1;
        descriptor.connected = connection_state;
        descriptor.character = &player;
        player.desc = &descriptor;
    }

    // Opens the account's vault, gives it STOP_VAULT_COINS and fails to write it, so that only the
    // vault in play holds them: the file is left behind.
    void leave_vault_file_behind()
    {
        std::string error_message;
        bank_vault* vault = bank_vault_open(STOP_ACCOUNT, BANK_SIDE_LIGHT, &error_message);
        ASSERT_NE(vault, nullptr) << error_message;
        vault->coins = STOP_VAULT_COINS;
        // The write goes through a temporary file: a folder in its place makes it fail.
        const std::filesystem::path blocker = vault_file().string() + ".tmp";
        std::error_code error;
        ASSERT_TRUE(std::filesystem::create_directory(blocker, error)) << error.message();
        ASSERT_FALSE(bank_vault_write(STOP_ACCOUNT, BANK_SIDE_LIGHT, &error_message));
        std::filesystem::remove(blocker, error);
        ASSERT_FALSE(error) << error.message();
        ASSERT_FALSE(std::filesystem::exists(vault_file(), error));
    }

    // The file the account's vault is written to.
    std::filesystem::path vault_file() const
    {
        return m_vault_folder / bank_side_file_name(BANK_SIDE_LIGHT);
    }

    // Whether <name> has a character file of its own on the account.
    static bool has_character_file(const char* name)
    {
        std::string error_message;
        const bool exists = account::account_character_file_exists(SAVE_ROOT, PLAYERS_ACCOUNT, name,
            &error_message);
        EXPECT_TRUE(error_message.empty()) << error_message;
        return exists;
    }

    // The room <name>'s character file brings them back to; NOWHERE, after reporting why, when the
    // file cannot be read.
    static int saved_load_room(const char* name)
    {
        char_file_u stored {};
        std::string error_message;
        if (!account::read_account_character_file(SAVE_ROOT, PLAYERS_ACCOUNT, name, &stored,
                &error_message)) {
            ADD_FAILURE() << "no character file for " << name << ": " << error_message;
            return NOWHERE;
        }
        return stored.specials2.load_room;
    }

    // What <name>'s object file holds; empty, after reporting why, when it cannot be read.
    static objects_json::ObjectSaveData saved_objects(const char* name)
    {
        objects_json::ObjectSaveData saved;
        std::string bytes;
        std::string error_message;
        if (!load_object_save_bytes_for_character(SAVE_ROOT, name, &bytes, &error_message)) {
            ADD_FAILURE() << "no object file for " << name << ": " << error_message;
            return saved;
        }

        if (!objects_json::object_save_data_from_binary(bytes, &saved, &error_message)) {
            ADD_FAILURE() << "unreadable object file for " << name << ": " << error_message;
        }
        return saved;
    }

    // Where the object with vnum <virtual_number> was saved in <saved>: its wear position, or
    // NOT_SAVED when it was saved nowhere.
    static int saved_wear_position(const objects_json::ObjectSaveData& saved, int virtual_number)
    {
        for (const objects_json::ObjectRecord& object : saved.objects) {
            if (object.item_number == virtual_number) {
                return object.wear_pos;
            }
        }
        return NOT_SAVED;
    }

    // The whole of the file at <path>; empty, after reporting why, when there is none.
    static std::string read_saved_file(const std::filesystem::path& path)
    {
        std::error_code error;
        const std::uintmax_t size = std::filesystem::file_size(path, error);
        if (error) {
            ADD_FAILURE() << "no file at " << path << ": " << error.message();
            return std::string();
        }
        if (size > SAVED_FILE_SIZE_LIMIT) {
            ADD_FAILURE() << path << " is " << size << " bytes, more than a saved file holds";
            return std::string();
        }
        std::ifstream file(path, std::ios::binary);
        const std::istreambuf_iterator<char> end_of_file;
        return std::string(std::istreambuf_iterator<char>(file), end_of_file);
    }

    // The folder the test started in, entered again by TearDown.
    std::filesystem::path m_original_folder;
    // The temporary folder the test runs in, removed by TearDown.
    std::filesystem::path m_folder;
    // The folder the bank resolves STOP_ACCOUNT to, holding its vault files.
    std::filesystem::path m_vault_folder;
    // The player index before the test, put back by TearDown.
    player_index_element* m_saved_player_table = nullptr;
    int m_saved_top_of_p_table = 0;
    // The index the test's players are found in.
    player_index_element m_player_entries[PLAYER_ENTRY_COUNT] {};
    // The second playing player, its name and its connection.
    char m_second_player_name[MAX_NAME_LENGTH + 1] = "Samwise";
    char_data m_second_player {};
    descriptor_data m_second_descriptor {};
    // The player still choosing a character, its name and its connection.
    char m_menu_player_name[MAX_NAME_LENGTH + 1] = "Peregrin";
    char_data m_menu_player {};
    descriptor_data m_menu_descriptor {};
};

} // namespace

TEST_F(SignalRequestsTest, StopHandlerOnlyRecordsTheSignal)
{
    RecordingSignalSteps steps;
    const auto previous_handler = std::signal(SIGTERM, hupsig);
    ASSERT_NE(previous_handler, SIG_ERR) << "Expected the test to install the stop handler.";

    testing::internal::CaptureStderr();
    std::raise(SIGTERM);
    const std::string handler_output = testing::internal::GetCapturedStderr();
    std::signal(SIGTERM, previous_handler);

    EXPECT_TRUE(handler_output.empty())
        << "Expected the handler to log nothing, but it wrote: " << handler_output;
    EXPECT_TRUE(stop_requested()) << "Expected the handler to record the stop.";
    EXPECT_TRUE(steps.steps_run.empty()) << "Expected no step to run before the game loop asks.";

    perform_signal_requests(steps);

    ASSERT_EQ(steps.steps_run, std::vector<std::string>({ STOP_STEP }))
        << "Expected the game loop's call to run the stop step once.";
    EXPECT_EQ(steps.stop_signals, std::vector<int>({ SIGTERM }))
        << "Expected the stop step to be told which signal asked for it.";
    EXPECT_FALSE(stop_requested()) << "Expected the performed stop to be forgotten.";
}

TEST_F(SignalRequestsTest, StopRunsOnceForEachRecordedRequest)
{
    RecordingSignalSteps steps;
    hupsig(SIGINT);

    perform_signal_requests(steps);
    perform_signal_requests(steps);

    EXPECT_EQ(steps.steps_run, std::vector<std::string>({ STOP_STEP }))
        << "Expected a second call with nothing new recorded to run no step.";
}

TEST_F(SignalRequestsTest, StopKeepsTheFirstSignalThatArrived)
{
    RecordingSignalSteps steps;
    hupsig(SIGINT);
    hupsig(SIGTERM);

    perform_signal_requests(steps);

    EXPECT_EQ(steps.stop_signals, std::vector<int>({ SIGINT }))
        << "Expected one stop, for the signal that arrived first.";
}

TEST_F(SignalRequestsTest, OtherHandlersOnlyRecordTheirRequests)
{
    RecordingSignalSteps steps;
    // These handlers ignore their argument; the signals they serve are not standard C++.
    const int unused_signal_number = 0;

    testing::internal::CaptureStderr();
    reread_wizlists(unused_signal_number);
    unrestrict_game(unused_signal_number);
    logsig(unused_signal_number);
    const std::string handler_output = testing::internal::GetCapturedStderr();

    EXPECT_TRUE(handler_output.empty())
        << "Expected the handlers to log nothing, but they wrote: " << handler_output;
    EXPECT_FALSE(stop_requested()) << "Expected none of these handlers to ask for a stop.";
    EXPECT_TRUE(steps.steps_run.empty()) << "Expected no step to run before the game loop asks.";

    perform_signal_requests(steps);

    EXPECT_EQ(steps.steps_run,
        std::vector<std::string>({ REREAD_STEP, UNRESTRICT_STEP, IGNORED_STEP }))
        << "Expected the game loop's call to run each recorded step once.";
}

TEST_F(SignalRequestsTest, StopRunsAfterTheOtherRequests)
{
    RecordingSignalSteps steps;
    // reread_wizlists ignores its argument.
    const int unused_signal_number = 0;
    hupsig(SIGTERM);
    reread_wizlists(unused_signal_number);

    perform_signal_requests(steps);

    EXPECT_EQ(steps.steps_run, std::vector<std::string>({ REREAD_STEP, STOP_STEP }))
        << "Expected the stop, which does not return in the server, to run last.";
}

TEST_F(SignalRequestsTest, SleepReturnsAtOnceWhenAStopIsAlreadyRecorded)
{
    record_stop_request(SIGTERM);
    const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();

    sleep_unless_stop_requested(LONG_SLEEP);

    const std::chrono::steady_clock::duration elapsed = std::chrono::steady_clock::now() - start;
    EXPECT_LT(elapsed, PROMPT_MARGIN) << "Expected a recorded stop to cut the sleep short.";
    EXPECT_TRUE(stop_requested()) << "Expected the sleep to leave the stop for the game loop.";
}

TEST_F(SignalRequestsTest, SleepEndsSoonAfterAStopArrivesOnAnotherThread)
{
    const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    std::thread signal_thread([]() -> void {
        std::this_thread::sleep_for(STOP_DELAY);
        record_stop_request(SIGTERM);
    });

    sleep_unless_stop_requested(LONG_SLEEP);

    const std::chrono::steady_clock::duration elapsed = std::chrono::steady_clock::now() - start;
    signal_thread.join();
    EXPECT_GE(elapsed, STOP_DELAY) << "Expected the sleep to last until the stop arrived.";
    EXPECT_LT(elapsed, PROMPT_MARGIN) << "Expected the stop to end the sleep soon after.";
}

TEST_F(SignalRequestsTest, SleepLastsTheWholeDurationWithoutAStop)
{
    const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();

    sleep_unless_stop_requested(SHORT_SLEEP);

    const std::chrono::steady_clock::duration elapsed = std::chrono::steady_clock::now() - start;
    EXPECT_GE(elapsed, SHORT_SLEEP) << "Expected the sleep to last at least its duration.";
}

// A stop saves each playing player to the room they stand in.
TEST_F(StopSavingTest, AStopSavesEachPlayingPlayerToTheRoomTheyAreIn)
{
    save_everything_for_stop();

    EXPECT_EQ(scripted_room::ROOM_VNUM, saved_load_room(m_player.player.name));
    EXPECT_EQ(scripted_room::SECOND_ROOM_VNUM, saved_load_room(m_second_player_name));
}

// A stop saves each playing player's gear, what they carry and what they wear, as a crash save.
TEST_F(StopSavingTest, AStopSavesEachPlayingPlayersGear)
{
    obj_data* stone = make_object(scripted_room::STONE_VNUM, NO_SCRIPT);
    ASSERT_NE(stone, nullptr);
    obj_to_char(stone, &m_player);
    obj_data* ring = make_object(scripted_room::RING_VNUM, NO_SCRIPT);
    ASSERT_NE(ring, nullptr);
    equip_char(&m_player, ring, WEAR_FINGER_R);
    obj_data* bread = make_object(scripted_room::BREAD_VNUM, NO_SCRIPT);
    ASSERT_NE(bread, nullptr);
    obj_to_char(bread, &m_second_player);

    save_everything_for_stop();

    const objects_json::ObjectSaveData player_objects = saved_objects(m_player.player.name);
    EXPECT_EQ(RENT_CRASH, player_objects.rent.rentcode);
    EXPECT_EQ(MAX_WEAR, saved_wear_position(player_objects, scripted_room::STONE_VNUM))
        << "Expected the stone to be saved as carried.";
    EXPECT_EQ(WEAR_FINGER_R, saved_wear_position(player_objects, scripted_room::RING_VNUM))
        << "Expected the ring to be saved on the finger it was worn on.";
    const objects_json::ObjectSaveData second_objects = saved_objects(m_second_player_name);
    EXPECT_EQ(RENT_CRASH, second_objects.rent.rentcode);
    EXPECT_EQ(MAX_WEAR, saved_wear_position(second_objects, scripted_room::BREAD_VNUM))
        << "Expected the second player's bread to be saved as carried.";
}

// A connection still at the menus has no character in the game, so a stop saves nothing for it.
TEST_F(StopSavingTest, AStopSavesNothingForAPlayerStillAtTheMenus)
{
    obj_data* stone = make_object(scripted_room::STONE_VNUM, NO_SCRIPT);
    ASSERT_NE(stone, nullptr);
    obj_to_char(stone, &m_menu_player);
    // Linking a character gives it an object file of its own already.
    std::string objects_before;
    std::string error_message;
    ASSERT_TRUE(load_object_save_bytes_for_character(SAVE_ROOT, m_menu_player_name,
        &objects_before, &error_message))
        << error_message;

    save_everything_for_stop();

    EXPECT_TRUE(has_character_file(m_player.player.name))
        << "Expected the playing player to be saved.";
    EXPECT_FALSE(has_character_file(m_menu_player_name))
        << "Expected no character file for the player at the menus.";
    std::string objects_after;
    ASSERT_TRUE(load_object_save_bytes_for_character(SAVE_ROOT, m_menu_player_name,
        &objects_after, &error_message))
        << error_message;
    EXPECT_EQ(objects_before, objects_after)
        << "Expected the stop to leave the object file of the player at the menus as it was.";
}

// A stop writes the vault file a failed write left behind.
TEST_F(StopSavingTest, AStopWritesEveryVaultFileLeftBehind)
{
    leave_vault_file_behind();

    save_everything_for_stop();

    const std::string vault_text = read_saved_file(vault_file());
    const std::string coins_line = "\"coins\": " + std::to_string(STOP_VAULT_COINS);
    EXPECT_NE(vault_text.find(coins_line), std::string::npos) << vault_text;
}
