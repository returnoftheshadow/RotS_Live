#include "fake_control_channel.h"
#include "fake_harness_request.h"
#include "harness_control_channel.h"
#include "harness_controlled_schedule.h"
#include "harness_request.h"
#include "request_outcome.h"
#include "schedule_setup.h"

#include "../game_tick_schedule.h"

#include <cstddef>
#include <cstdint>
#include <gtest/gtest.h>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
// The words the wrapper logs when the channel's input closes.
constexpr std::string_view CLOSED_INPUT_PHRASE = "no more time requests";
// The words the wrapper logs when it has no channel.
constexpr std::string_view NO_CHANNEL_PHRASE = "no control channel";
// The words the wrapper logs when its setup has no schedule.
constexpr std::string_view NO_SCHEDULE_PHRASE = "has no schedule";
// The words the wrapper logs when a deferred reply cannot build its body.
constexpr std::string_view NO_BODY_PHRASE = "cannot build its body";
// The words the wrapper logs when a deferred reply waits for no pass.
constexpr std::string_view NO_PASS_PHRASE = "waits for no pass";
// The words the wrapper logs when a deferred reply's due pass cannot be counted.
constexpr std::string_view UNCOUNTABLE_PHRASE = "more passes than can be counted";
// The name the wrapper under test reports for its schedule.
constexpr std::string_view FAKE_SCHEDULE_NAME = "fake-schedule";
// The ready state the fake setup reports.
constexpr std::string_view FAKE_READY_STATE = "state=fresh";
// The name the fake request is registered under.
constexpr std::string_view FAKE_REQUEST_NAME = "fake";

// A schedule that counts the passes it decides and returns the world pass its test chooses.
class FakeSchedule : public GameTickSchedule {
public:
    // What the schedule shares with its test.
    struct State {
        // Passes decided so far.
        int passes_decided = 0;
        // Whether the passes it decides run the fast update.
        bool run_fast_update = false;
    };

    // Reads and writes state, which must not be null.
    explicit FakeSchedule(std::shared_ptr<State> state)
        : shared_state(std::move(state))
    {
    }

    [[nodiscard]] WorldPass next_pass() override
    {
        shared_state->passes_decided++;
        WorldPass world_pass;
        world_pass.run_fast_update = shared_state->run_fast_update;
        return world_pass;
    }

private:
    // The state the test sets and inspects.
    std::shared_ptr<State> shared_state;
};

// Builds a setup holding a FakeSchedule on schedule_state and one FakeHarnessRequest, named
// FAKE_REQUEST_NAME, on request_state.
harness::ScheduleSetup create_fake_setup(std::shared_ptr<FakeSchedule::State> schedule_state,
    std::shared_ptr<harness_tests::FakeHarnessRequest::State> request_state,
    std::string_view ready_state)
{
    harness::ScheduleSetup setup;
    setup.schedule = std::make_shared<FakeSchedule>(std::move(schedule_state));
    std::unique_ptr<harness::HarnessRequest> request
        = std::make_unique<harness_tests::FakeHarnessRequest>(
            std::string(FAKE_REQUEST_NAME), std::move(request_state));
    const bool registered = setup.requests.register_request(std::move(request));
    EXPECT_TRUE(registered) << "Expected the fake request to register.";
    setup.ready_state = std::string(ready_state);
    return setup;
}

// A wrapper over a fake channel, schedule and request, and the state that drives them.
struct WrapperUnderTest {
    // The lines the test feeds and the replies it reads.
    std::shared_ptr<harness_tests::FakeControlChannel::State> channel_state
        = std::make_shared<harness_tests::FakeControlChannel::State>();
    // The fake schedule's pass count and the passes it returns.
    std::shared_ptr<FakeSchedule::State> schedule_state = std::make_shared<FakeSchedule::State>();
    // The fake request's scripted outcomes and the lines it was handed.
    std::shared_ptr<harness_tests::FakeHarnessRequest::State> request_state
        = std::make_shared<harness_tests::FakeHarnessRequest::State>();
    // The wrapper, owning its fake channel and setup.
    harness::HarnessControlledSchedule wrapper { FAKE_SCHEDULE_NAME,
        create_fake_setup(schedule_state, request_state, FAKE_READY_STATE),
        std::make_unique<harness_tests::FakeControlChannel>(channel_state) };
};

// The ready reply the wrapper under test sends on its first pass.
std::string ready_reply()
{
    return "ready schedule=" + std::string(FAKE_SCHEDULE_NAME) + " "
        + std::string(FAKE_READY_STATE);
}

// The line naming the fake request, with arguments after it.
std::string fake_request_line(std::string_view arguments)
{
    return std::string(FAKE_REQUEST_NAME) + " " + std::string(arguments);
}

// A reply that falls due after pass_count passes, with body as its text.
harness::DeferredReply deferred_reply(std::uint64_t pass_count, std::string_view body)
{
    harness::DeferredReply deferred;
    deferred.pass_count = pass_count;
    std::string reply_body(body);
    deferred.build_body = [reply_body]() -> std::string { return reply_body; };
    return deferred;
}

// Calls next_pass() on wrapper pass_count times, discarding the passes.
void run_passes(harness::HarnessControlledSchedule& wrapper, int pass_count)
{
    for (int pass_number = 1; pass_number <= pass_count; ++pass_number) {
        static_cast<void>(wrapper.next_pass());
    }
}

// Returns how many times phrase occurs in text.
int count_occurrences(std::string_view text, std::string_view phrase)
{
    int occurrences = 0;
    std::size_t position = text.find(phrase);
    while (position != std::string_view::npos) {
        occurrences++;
        position = text.find(phrase, position + phrase.size());
    }
    return occurrences;
}
} // namespace

TEST(HarnessControlledSchedule, SendsReadyOnceOnTheFirstPass)
{
    WrapperUnderTest under_test;

    run_passes(under_test.wrapper, 3);

    const std::vector<std::string> expected = { ready_reply() };
    EXPECT_EQ(under_test.channel_state->replies, expected) << "Expected exactly one ready reply.";
}

TEST(HarnessControlledSchedule, SendsReadyWithNoStateWhenTheSetupHasNone)
{
    std::shared_ptr<harness_tests::FakeControlChannel::State> channel_state
        = std::make_shared<harness_tests::FakeControlChannel::State>();
    harness::HarnessControlledSchedule wrapper(FAKE_SCHEDULE_NAME,
        create_fake_setup(std::make_shared<FakeSchedule::State>(),
            std::make_shared<harness_tests::FakeHarnessRequest::State>(), ""),
        std::make_unique<harness_tests::FakeControlChannel>(channel_state));

    static_cast<void>(wrapper.next_pass());

    const std::vector<std::string> expected
        = { "ready schedule=" + std::string(FAKE_SCHEDULE_NAME) };
    EXPECT_EQ(channel_state->replies, expected) << "Expected no space after the name.";
}

TEST(HarnessControlledSchedule, ReturnsTheSchedulesPassOncePerCall)
{
    WrapperUnderTest under_test;
    under_test.schedule_state->run_fast_update = true;

    for (int pass_number = 1; pass_number <= 3; ++pass_number) {
        const GameTickSchedule::WorldPass world_pass = under_test.wrapper.next_pass();
        EXPECT_TRUE(world_pass.run_fast_update) << "Expected the schedule's pass " << pass_number;
        EXPECT_EQ(under_test.schedule_state->passes_decided, pass_number)
            << "Expected the schedule asked once per pass.";
    }
}

TEST(HarnessControlledSchedule, HandsALineToItsRequestBeforeTheScheduleDecidesThatPass)
{
    WrapperUnderTest under_test;
    run_passes(under_test.wrapper, 4);
    std::vector<int> passes_decided_when_handled;
    std::shared_ptr<FakeSchedule::State> schedule_state = under_test.schedule_state;
    under_test.request_state->on_handle = [schedule_state, &passes_decided_when_handled]() -> void {
        passes_decided_when_handled.push_back(schedule_state->passes_decided);
        schedule_state->run_fast_update = true;
    };

    under_test.channel_state->lines_to_deliver.push_back(fake_request_line("go"));
    const GameTickSchedule::WorldPass world_pass = under_test.wrapper.next_pass();

    const std::vector<int> expected_passes = { 4 };
    EXPECT_EQ(passes_decided_when_handled, expected_passes)
        << "Expected the line handled before the fifth pass was decided.";
    EXPECT_TRUE(world_pass.run_fast_update) << "Expected the request to shape the same pass.";
    const std::vector<std::string> expected_arguments = { "go" };
    EXPECT_EQ(under_test.request_state->handled_arguments, expected_arguments)
        << "Unexpected arguments handed to the request.";
}

TEST(HarnessControlledSchedule, HandlesALineReceivedBeforeTheFirstPassAfterSendingReady)
{
    WrapperUnderTest under_test;
    under_test.channel_state->lines_to_deliver.push_back(fake_request_line("early"));

    static_cast<void>(under_test.wrapper.next_pass());

    const std::vector<std::string> expected
        = { ready_reply(), std::string(harness_tests::FakeHarnessRequest::UNSCRIPTED_REPLY) };
    EXPECT_EQ(under_test.channel_state->replies, expected)
        << "Expected ready, then the early request's immediate reply.";
}

TEST(HarnessControlledSchedule, SendsADeferredReplyOnThePassAfterItsLastPassIsReturned)
{
    WrapperUnderTest under_test;
    constexpr int WAIT_PASSES = 20;
    under_test.request_state->outcomes.push_back(deferred_reply(WAIT_PASSES, "done waiting"));
    under_test.channel_state->lines_to_deliver.push_back(fake_request_line("wait"));

    run_passes(under_test.wrapper, WAIT_PASSES);
    EXPECT_EQ(under_test.channel_state->replies.size(), 1u)
        << "Expected no reply while its last pass is still to be returned.";

    static_cast<void>(under_test.wrapper.next_pass());

    const std::vector<std::string> expected = { ready_reply(), "done waiting" };
    EXPECT_EQ(under_test.channel_state->replies, expected) << "Expected the deferred reply.";
}

TEST(HarnessControlledSchedule, SendsQueuedDeferredRepliesInOrderOnceEachIsDue)
{
    WrapperUnderTest under_test;
    under_test.request_state->outcomes.push_back(deferred_reply(20, "done first"));
    under_test.request_state->outcomes.push_back(deferred_reply(60, "done second"));
    under_test.channel_state->lines_to_deliver
        = { fake_request_line("first"), fake_request_line("second") };
    std::vector<int> passes_at_reply;
    std::size_t replies_seen = 1;

    for (int pass_number = 1; pass_number <= 61; ++pass_number) {
        static_cast<void>(under_test.wrapper.next_pass());
        if (under_test.channel_state->replies.size() > replies_seen) {
            passes_at_reply.push_back(pass_number);
            replies_seen = under_test.channel_state->replies.size();
        }
    }

    const std::vector<std::string> expected = { ready_reply(), "done first", "done second" };
    EXPECT_EQ(under_test.channel_state->replies, expected) << "Expected both replies, in order.";
    const std::vector<int> expected_passes = { 21, 61 };
    EXPECT_EQ(passes_at_reply, expected_passes)
        << "Expected each reply on the pass after its last pass was returned.";
}

TEST(HarnessControlledSchedule, SendsNoDeferredReplyBeforeOneQueuedAheadOfIt)
{
    WrapperUnderTest under_test;
    under_test.request_state->outcomes.push_back(deferred_reply(30, "done slow"));
    under_test.request_state->outcomes.push_back(deferred_reply(5, "done quick"));
    under_test.channel_state->lines_to_deliver
        = { fake_request_line("slow"), fake_request_line("quick") };

    run_passes(under_test.wrapper, 30);
    EXPECT_EQ(under_test.channel_state->replies.size(), 1u)
        << "Expected the quick reply to wait behind the slow one.";

    static_cast<void>(under_test.wrapper.next_pass());

    const std::vector<std::string> expected = { ready_reply(), "done slow", "done quick" };
    EXPECT_EQ(under_test.channel_state->replies, expected)
        << "Expected both replies once the first is due, in order.";
}

TEST(HarnessControlledSchedule, BuildsADeferredReplyWhenItIsSent)
{
    WrapperUnderTest under_test;
    std::shared_ptr<FakeSchedule::State> schedule_state = under_test.schedule_state;
    harness::DeferredReply deferred;
    deferred.pass_count = 3;
    deferred.build_body = [schedule_state]() -> std::string {
        return "done passes=" + std::to_string(schedule_state->passes_decided);
    };
    under_test.request_state->outcomes.push_back(std::move(deferred));
    under_test.channel_state->lines_to_deliver.push_back(fake_request_line("count"));

    run_passes(under_test.wrapper, 4);

    const std::vector<std::string> expected = { ready_reply(), "done passes=3" };
    EXPECT_EQ(under_test.channel_state->replies, expected)
        << "Expected the body built after the reply's three passes.";
}

TEST(HarnessControlledSchedule, AnswersAnUnknownRequestWithAnErrorAndKeepsRunning)
{
    WrapperUnderTest under_test;
    under_test.channel_state->lines_to_deliver.push_back("status");

    static_cast<void>(under_test.wrapper.next_pass());

    ASSERT_EQ(under_test.channel_state->replies.size(), 2u) << "Expected ready and an error.";
    EXPECT_EQ(under_test.channel_state->replies.back(), "error status: unknown request")
        << "Expected the dispatcher's refusal as the reply.";
    EXPECT_TRUE(under_test.request_state->handled_lines.empty())
        << "Expected the unknown request handed to no request.";

    under_test.channel_state->lines_to_deliver.push_back(fake_request_line("later"));
    static_cast<void>(under_test.wrapper.next_pass());
    EXPECT_EQ(under_test.request_state->handled_lines.size(), 1u)
        << "Expected a later valid request to be handled.";
}

TEST(HarnessControlledSchedule, SendsADeferredReplyAfterTheInputCloses)
{
    WrapperUnderTest under_test;
    under_test.request_state->outcomes.push_back(deferred_reply(2, "done after close"));
    under_test.channel_state->lines_to_deliver.push_back(fake_request_line("close"));
    static_cast<void>(under_test.wrapper.next_pass());
    under_test.channel_state->closed = true;

    testing::internal::CaptureStderr();
    run_passes(under_test.wrapper, 2);
    static_cast<void>(testing::internal::GetCapturedStderr());

    EXPECT_EQ(under_test.channel_state->replies.back(), "done after close")
        << "Expected the deferred reply sent after input closed.";
}

TEST(HarnessControlledSchedule, LogsOnceWhenTheChannelInputCloses)
{
    WrapperUnderTest under_test;
    under_test.channel_state->closed = true;

    testing::internal::CaptureStderr();
    run_passes(under_test.wrapper, 3);
    const std::string logged = testing::internal::GetCapturedStderr();

    EXPECT_EQ(count_occurrences(logged, CLOSED_INPUT_PHRASE), 1)
        << "Expected the closed input logged once; log was: " << logged;
}

TEST(HarnessControlledSchedule, WithANullChannelLogsOnceAndReturnsTheSchedulesPasses)
{
    std::shared_ptr<FakeSchedule::State> schedule_state = std::make_shared<FakeSchedule::State>();
    schedule_state->run_fast_update = true;

    testing::internal::CaptureStderr();
    harness::HarnessControlledSchedule wrapper(FAKE_SCHEDULE_NAME,
        create_fake_setup(schedule_state,
            std::make_shared<harness_tests::FakeHarnessRequest::State>(), FAKE_READY_STATE),
        nullptr);
    const GameTickSchedule::WorldPass world_pass = wrapper.next_pass();
    static_cast<void>(wrapper.next_pass());
    const std::string logged = testing::internal::GetCapturedStderr();

    EXPECT_TRUE(world_pass.run_fast_update) << "Expected the schedule's pass without a channel.";
    EXPECT_EQ(schedule_state->passes_decided, 2) << "Expected the schedule asked every pass.";
    EXPECT_EQ(count_occurrences(logged, NO_CHANNEL_PHRASE), 1)
        << "Expected the missing channel logged once; log was: " << logged;
}

TEST(HarnessControlledSchedule, WithNoScheduleLogsOnceAndRunsNoWorldUpdate)
{
    std::shared_ptr<harness_tests::FakeControlChannel::State> channel_state
        = std::make_shared<harness_tests::FakeControlChannel::State>();

    testing::internal::CaptureStderr();
    harness::HarnessControlledSchedule wrapper(FAKE_SCHEDULE_NAME, harness::ScheduleSetup {},
        std::make_unique<harness_tests::FakeControlChannel>(channel_state));
    const GameTickSchedule::WorldPass world_pass = wrapper.next_pass();
    static_cast<void>(wrapper.next_pass());
    const std::string logged = testing::internal::GetCapturedStderr();

    EXPECT_FALSE(world_pass.run_fast_update) << "Expected no world update without a schedule.";
    EXPECT_EQ(world_pass.time_phase, 0) << "Expected phase 0 without a schedule.";
    EXPECT_EQ(count_occurrences(logged, NO_SCHEDULE_PHRASE), 1)
        << "Expected the missing schedule logged once; log was: " << logged;
}

TEST(HarnessControlledSchedule, DropsAndLogsADeferredReplyThatCannotBuildItsBody)
{
    WrapperUnderTest under_test;
    harness::DeferredReply bodiless;
    bodiless.pass_count = 1;
    under_test.request_state->outcomes.push_back(std::move(bodiless));
    under_test.channel_state->lines_to_deliver.push_back(fake_request_line("bodiless"));

    testing::internal::CaptureStderr();
    run_passes(under_test.wrapper, 3);
    const std::string logged = testing::internal::GetCapturedStderr();

    const std::vector<std::string> expected = { ready_reply() };
    EXPECT_EQ(under_test.channel_state->replies, expected) << "Expected no reply sent.";
    EXPECT_EQ(count_occurrences(logged, NO_BODY_PHRASE), 1)
        << "Expected the bodiless reply logged once; log was: " << logged;
}

TEST(HarnessControlledSchedule, SendsADeferredReplyWaitingForNoPassAfterOnePassAndLogsIt)
{
    WrapperUnderTest under_test;
    under_test.request_state->outcomes.push_back(deferred_reply(0, "done at once"));
    under_test.channel_state->lines_to_deliver.push_back(fake_request_line("zero"));

    testing::internal::CaptureStderr();
    static_cast<void>(under_test.wrapper.next_pass());
    const std::size_t replies_after_first_pass = under_test.channel_state->replies.size();
    static_cast<void>(under_test.wrapper.next_pass());
    const std::string logged = testing::internal::GetCapturedStderr();

    EXPECT_EQ(replies_after_first_pass, 1u) << "Expected only ready on the first pass.";
    const std::vector<std::string> expected = { ready_reply(), "done at once" };
    EXPECT_EQ(under_test.channel_state->replies, expected)
        << "Expected the reply on the second pass.";
    EXPECT_EQ(count_occurrences(logged, NO_PASS_PHRASE), 1)
        << "Expected the zero wait logged once; log was: " << logged;
}

TEST(HarnessControlledSchedule, DropsAndLogsAReplyWhoseDuePassCannotBeCountedAndSendsTheNext)
{
    WrapperUnderTest under_test;
    static_cast<void>(under_test.wrapper.next_pass());
    under_test.request_state->outcomes.push_back(
        deferred_reply(std::numeric_limits<std::uint64_t>::max(), "done never"));
    under_test.request_state->outcomes.push_back(deferred_reply(1, "done next"));
    under_test.channel_state->lines_to_deliver
        = { fake_request_line("never"), fake_request_line("next") };

    testing::internal::CaptureStderr();
    run_passes(under_test.wrapper, 2);
    const std::string logged = testing::internal::GetCapturedStderr();

    const std::vector<std::string> expected = { ready_reply(), "done next" };
    EXPECT_EQ(under_test.channel_state->replies, expected)
        << "Expected the uncountable reply dropped and the next one sent on the following pass.";
    EXPECT_EQ(count_occurrences(logged, UNCOUNTABLE_PHRASE), 1)
        << "Expected the dropped reply logged once; log was: " << logged;
}
