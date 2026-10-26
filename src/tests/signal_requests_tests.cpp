#include "../signal_requests.h"
#include "../signals.h"

#include <chrono>
#include <csignal>
#include <gtest/gtest.h>
#include <string>
#include <thread>
#include <vector>

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
