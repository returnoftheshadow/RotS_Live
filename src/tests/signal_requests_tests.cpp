#include "../db.h"
#include "../handler.h"
#include "../mob_progs/banker.h"
#include "../objects_json.h"
#include "../signal_requests.h"
#include "../signals.h"
#include "../structs.h"
#include "../utils.h"
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

void clear_char(struct char_data* ch, int mode);
extern struct descriptor_data* descriptor_list;
extern int top_of_p_table;

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
// The largest saved file the stop test reads back.
constexpr std::uintmax_t SAVED_FILE_SIZE_LIMIT = 1 << 20;
// The subfolders of plrobjs that Crash_get_filename files a character's objects under.
constexpr const char* OBJECT_FILE_FOLDERS[] = { "plrobjs/A-E", "plrobjs/F-J", "plrobjs/K-O",
    "plrobjs/P-T", "plrobjs/U-Z", "plrobjs/ZZZ" };

// A playing player and an account folder holding a vault, all in a temporary folder the test runs
// in, so the stop's saves land there and go with it.
class StopSavingTest : public testing::Test {
protected:
    void SetUp() override
    {
        std::error_code error;
        m_original_folder = std::filesystem::current_path(error);
        ASSERT_FALSE(error) << "no current folder: " << error.message();
        const std::filesystem::path temporary_root = std::filesystem::temp_directory_path(error);
        ASSERT_FALSE(error) << "no temporary folder: " << error.message();
        m_folder = temporary_root / "rots_stop_saving";
        std::filesystem::remove_all(m_folder, error);
        for (const char* subfolder : OBJECT_FILE_FOLDERS) {
            std::filesystem::create_directories(m_folder / subfolder, error);
            ASSERT_FALSE(error) << "cannot make " << (m_folder / subfolder) << ": "
                                << error.message();
        }
        m_account_folder = m_folder / "vaults" / STOP_ACCOUNT;
        std::filesystem::create_directories(m_account_folder, error);
        ASSERT_FALSE(error) << "cannot make " << m_account_folder << ": " << error.message();
        std::filesystem::current_path(m_folder, error);
        ASSERT_FALSE(error) << "cannot enter " << m_folder << ": " << error.message();

        bank_vault_forget_all_for_tests();
        const std::filesystem::path account_folder = m_account_folder;
        auto resolve_account = [account_folder](const std::string& account_name) -> std::string {
            if (account_name != STOP_ACCOUNT) {
                return std::string();
            }
            return account_folder.string();
        };
        bank_set_directory_resolver(resolve_account);

        /* No player table: the character's own save finds nobody, and only its objects are
         * written. */
        m_saved_top_of_p_table = top_of_p_table;
        top_of_p_table = -1;
        m_saved_descriptor_list = descriptor_list;

        clear_char(&m_player, 0);
        m_player.player.name = m_player_name;
        m_player.player.race = RACE_HUMAN;
        m_player.player.level = 10;
        m_player.in_room = NOWHERE;
        m_descriptor.output = m_descriptor.small_outbuf;
        m_descriptor.small_outbuf[0] = '\0';
        m_descriptor.bufspace = SMALL_BUFSIZE - 1;
        m_descriptor.connected = CON_PLYNG;
        m_descriptor.character = &m_player;
        m_player.desc = &m_descriptor;
        descriptor_list = &m_descriptor;
    }

    void TearDown() override
    {
        descriptor_list = m_saved_descriptor_list;
        top_of_p_table = m_saved_top_of_p_table;
        release_cleared_character(&m_player);
        release_test_descriptor(&m_descriptor);
        bank_vault_forget_all_for_tests();
        bank_set_directory_resolver(nullptr);
        std::error_code error;
        if (!m_original_folder.empty()) {
            std::filesystem::current_path(m_original_folder, error);
        }
        if (!m_folder.empty()) {
            std::filesystem::remove_all(m_folder, error);
        }
    }

    /* Opens the account's vault, gives it STOP_VAULT_COINS and fails to write it, so that only the
     * vault in play holds them: the file is left behind. */
    void leave_vault_file_behind()
    {
        std::string error_message;
        bank_vault* vault = bank_vault_open(STOP_ACCOUNT, BANK_SIDE_LIGHT, &error_message);
        ASSERT_NE(vault, nullptr) << error_message;
        vault->coins = STOP_VAULT_COINS;
        /* The write goes through a temporary file: a folder in its place makes it fail. */
        const std::filesystem::path blocker = vault_file().string() + ".tmp";
        std::error_code error;
        ASSERT_TRUE(std::filesystem::create_directory(blocker, error)) << error.message();
        ASSERT_FALSE(bank_vault_write(STOP_ACCOUNT, BANK_SIDE_LIGHT, &error_message));
        std::filesystem::remove(blocker, error);
        ASSERT_FALSE(error) << error.message();
        ASSERT_FALSE(std::filesystem::exists(vault_file(), error));
    }

    /* The file the account's vault is written to. */
    std::filesystem::path vault_file() const
    {
        return m_account_folder / bank_side_file_name(BANK_SIDE_LIGHT);
    }

    /* The player's object file, as the game names it. */
    std::filesystem::path player_object_file()
    {
        char path[MAX_INPUT_LENGTH];
        if (!Crash_get_filename(GET_NAME(&m_player), path)) {
            ADD_FAILURE() << "the player's object file has no name";
            return {};
        }
        return m_folder / path;
    }

    /* The whole of the file at <path>; empty, after reporting why, when there is none. */
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
    // The account folder the bank resolves STOP_ACCOUNT to, holding its vault files.
    std::filesystem::path m_account_folder;
    // The player table's last index before the test, put back by TearDown.
    int m_saved_top_of_p_table = 0;
    // The connection list before the test, put back by TearDown.
    descriptor_data* m_saved_descriptor_list = nullptr;
    // The playing player's name.
    char m_player_name[16] = "Stopper";
    // The playing player, never freed.
    char_data m_player {};
    // The player's connection, the only one on the list.
    descriptor_data m_descriptor {};
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

// A stop saves every playing player's gear, and writes the vault file a failed write left behind.
TEST_F(StopSavingTest, AStopSavesEveryPlayerAndEveryVaultLeftBehind)
{
    leave_vault_file_behind();
    std::error_code error;
    ASSERT_FALSE(std::filesystem::exists(player_object_file(), error));

    save_everything_for_stop();

    const std::string object_bytes = read_saved_file(player_object_file());
    objects_json::ObjectSaveData saved;
    std::string error_message;
    ASSERT_TRUE(objects_json::object_save_data_from_binary(object_bytes, &saved, &error_message))
        << error_message;
    EXPECT_EQ(RENT_CRASH, saved.rent.rentcode);
    const std::string vault_text = read_saved_file(vault_file());
    const std::string coins_line = "\"coins\": " + std::to_string(STOP_VAULT_COINS);
    EXPECT_NE(vault_text.find(coins_line), std::string::npos) << vault_text;
}
