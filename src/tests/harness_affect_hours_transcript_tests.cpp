#include "affect_hours_registration.h"
#include "fake_control_channel.h"
#include "harness_controlled_schedule.h"
#include "schedule_definition.h"
#include "schedule_registry.h"
#include "schedule_setup.h"

#include "../game_tick_schedule.h"
#include "../structs.h"

#include <cstddef>
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <vector>

// These tests run the affect-hours schedule through its registration, its factory and the
// wrapper, and compare every reply with the literal text the integration harness reads, so a
// change to the wire text fails here.

namespace {
// Builds the affect-hours setup through its registration and the registry's factory.
harness::ScheduleSetup create_registered_affect_hours_setup()
{
    harness::ScheduleRegistry registry;
    const bool registered = harness::register_affect_hours_schedule(registry);
    EXPECT_TRUE(registered) << "Expected affect-hours to register.";
    const harness::ScheduleDefinition* const definition
        = registry.find(harness::AFFECT_HOURS_SCHEDULE_NAME);
    if (definition == nullptr) {
        ADD_FAILURE() << "Expected affect-hours to be found.";
        return harness::ScheduleSetup {};
    }
    return definition->create_setup();
}

// An affect-hours run over a fake channel, and the channel's state.
struct AffectHoursRun {
    // The lines the test feeds and the replies it reads.
    std::shared_ptr<harness_tests::FakeControlChannel::State> channel_state
        = std::make_shared<harness_tests::FakeControlChannel::State>();
    // The wrapper over the registered setup, owning its fake channel.
    harness::HarnessControlledSchedule wrapper { harness::AFFECT_HOURS_SCHEDULE_NAME,
        create_registered_affect_hours_setup(),
        std::make_unique<harness_tests::FakeControlChannel>(channel_state) };
};

// What the replies were, and on which passes new ones appeared.
struct ReplyTimeline {
    // The pass number on which each new reply appeared, in order.
    std::vector<int> passes_at_reply;
    // Whether each pass run, in order, ran the fast update.
    std::vector<bool> fast_updates;
};

// Runs pass_count passes of run, numbering them from first_pass_number, and fills out_timeline
// with the pass numbers on which replies were sent and each pass's fast update.
void run_and_record(
    AffectHoursRun& run, int first_pass_number, int pass_count, ReplyTimeline& out_timeline)
{
    std::size_t replies_seen = run.channel_state->replies.size();
    for (int pass_offset = 0; pass_offset < pass_count; ++pass_offset) {
        const GameTickSchedule::WorldPass world_pass = run.wrapper.next_pass();
        out_timeline.fast_updates.push_back(world_pass.run_fast_update);
        const std::size_t replies_now = run.channel_state->replies.size();
        if (replies_now > replies_seen) {
            out_timeline.passes_at_reply.push_back(first_pass_number + pass_offset);
            replies_seen = replies_now;
        }
    }
}

// Returns how many entries of fast_updates are true.
int count_fast_updates(const std::vector<bool>& fast_updates)
{
    int fast_update_count = 0;
    for (const bool ran_fast_update : fast_updates) {
        if (ran_fast_update) {
            fast_update_count++;
        }
    }
    return fast_update_count;
}
} // namespace

TEST(AffectHoursTranscript, IsTheOnlyScheduleItsRegistrationAdds)
{
    harness::ScheduleRegistry registry;

    ASSERT_TRUE(harness::register_affect_hours_schedule(registry)) << "Expected it to register.";

    const std::vector<std::string> expected = { "affect-hours" };
    EXPECT_EQ(registry.names(), expected) << "Unexpected registered names.";
    const harness::ScheduleDefinition* const definition = registry.find("affect-hours");
    ASSERT_NE(definition, nullptr) << "Expected affect-hours to be found.";
    EXPECT_FALSE(definition->summary.empty()) << "Expected a usage summary.";
}

TEST(AffectHoursTranscript, SendsTheReadyLineFirst)
{
    AffectHoursRun run;

    static_cast<void>(run.wrapper.next_pass());

    const std::vector<std::string> expected = { "ready schedule=affect-hours hours=0" };
    EXPECT_EQ(run.channel_state->replies, expected) << "Unexpected ready line.";
}

TEST(AffectHoursTranscript, RunsNoWorldUpdateUntilAGrantArrives)
{
    AffectHoursRun run;
    ReplyTimeline timeline;

    run_and_record(run, 1, FAST_UPDATE_RATE, timeline);

    EXPECT_EQ(count_fast_updates(timeline.fast_updates), 0) << "Expected only idle passes.";
}

TEST(AffectHoursTranscript, AcknowledgesTwoGrantsFromOnePassInOrderOnceBothHaveRun)
{
    AffectHoursRun run;
    run.channel_state->lines_to_deliver = { "advance-hours 1", "advance-hours 2" };
    ReplyTimeline timeline;

    run_and_record(run, 1, 3 * FAST_UPDATE_RATE + 1, timeline);

    const std::vector<std::string> expected = { "ready schedule=affect-hours hours=0",
        "done advance-hours 1 hours=1", "done advance-hours 2 hours=3" };
    EXPECT_EQ(run.channel_state->replies, expected) << "Expected both acknowledgements, in order.";
    const std::vector<int> expected_passes = { 1, FAST_UPDATE_RATE + 1, 3 * FAST_UPDATE_RATE + 1 };
    EXPECT_EQ(timeline.passes_at_reply, expected_passes)
        << "Expected ready, then each acknowledgement on the pass after its last pass, none early.";
    EXPECT_EQ(count_fast_updates(timeline.fast_updates), 3 * FAST_UPDATE_RATE)
        << "Expected three hours of passes, then an idle pass.";
    EXPECT_FALSE(timeline.fast_updates.back()) << "Expected the last pass idle.";
}

TEST(AffectHoursTranscript, AcknowledgesAGrantReadMidHourAfterTheHourUnderWay)
{
    AffectHoursRun run;
    constexpr int PASSES_BEFORE_SECOND_GRANT = 5;
    run.channel_state->lines_to_deliver = { "advance-hours 1" };
    ReplyTimeline timeline;
    run_and_record(run, 1, PASSES_BEFORE_SECOND_GRANT, timeline);

    run.channel_state->lines_to_deliver = { "advance-hours 1" };
    run_and_record(run, PASSES_BEFORE_SECOND_GRANT + 1,
        2 * FAST_UPDATE_RATE + 1 - PASSES_BEFORE_SECOND_GRANT, timeline);

    const std::vector<std::string> expected = { "ready schedule=affect-hours hours=0",
        "done advance-hours 1 hours=1", "done advance-hours 1 hours=2" };
    EXPECT_EQ(run.channel_state->replies, expected)
        << "Expected the mid-hour grant acknowledged after the hour under way and its own.";
    const std::vector<int> expected_passes = { 1, FAST_UPDATE_RATE + 1, 2 * FAST_UPDATE_RATE + 1 };
    EXPECT_EQ(timeline.passes_at_reply, expected_passes)
        << "Expected ready, then each acknowledgement on the pass after its grant's last pass.";
    EXPECT_EQ(count_fast_updates(timeline.fast_updates), 2 * FAST_UPDATE_RATE)
        << "Expected two hours of passes without a gap, then an idle pass.";
}

TEST(AffectHoursTranscript, AnswersEachMalformedLineWithItsError)
{
    AffectHoursRun run;
    const std::string overlong(65, 'a');
    run.channel_state->lines_to_deliver = { "", "\r", overlong, "status", "status\r",
        "advance-hours3", "advance-hours", "advance-hours ", "advance-hours x", "advance-hours x\r",
        "advance-hours 0", "advance-hours 1001", "advance-hours -1", "advance-hours +1",
        "advance-hours 01", "advance-hours 1x" };

    const GameTickSchedule::WorldPass world_pass = run.wrapper.next_pass();

    const std::vector<std::string> expected = { "ready schedule=affect-hours hours=0",
        "error empty request", "error empty request", "error request longer than 64 characters",
        "error status: unknown request", "error status: unknown request",
        "error advance-hours3: unknown request",
        "error advance-hours: hours must be a whole number from 1 to 1000",
        "error advance-hours : hours must be a whole number from 1 to 1000",
        "error advance-hours x: hours must be a whole number from 1 to 1000",
        "error advance-hours x: hours must be a whole number from 1 to 1000",
        "error advance-hours 0: hours must be a whole number from 1 to 1000",
        "error advance-hours 1001: hours must be a whole number from 1 to 1000",
        "error advance-hours -1: hours must be a whole number from 1 to 1000",
        "error advance-hours +1: hours must be a whole number from 1 to 1000",
        "error advance-hours 01: hours must be a whole number from 1 to 1000",
        "error advance-hours 1x: hours must be a whole number from 1 to 1000" };
    EXPECT_EQ(run.channel_state->replies, expected) << "Unexpected error replies.";
    EXPECT_FALSE(world_pass.run_fast_update) << "Expected the refused lines to grant nothing.";
}

TEST(AffectHoursTranscript, AnswersHoursBeyondThePendingLimitWithTheSchedulesReason)
{
    AffectHoursRun run;
    run.channel_state->lines_to_deliver = { "advance-hours 1000", "advance-hours 1" };

    const GameTickSchedule::WorldPass world_pass = run.wrapper.next_pass();

    const std::vector<std::string> expected = { "ready schedule=affect-hours hours=0",
        "error advance-hours 1: at most 1000 hours may be pending" };
    EXPECT_EQ(run.channel_state->replies, expected) << "Unexpected refusal.";
    EXPECT_TRUE(world_pass.run_fast_update) << "Expected the first grant to run.";
}

TEST(AffectHoursTranscript, WithANullChannelRunsNoWorldUpdate)
{
    testing::internal::CaptureStderr();
    harness::HarnessControlledSchedule wrapper(
        harness::AFFECT_HOURS_SCHEDULE_NAME, create_registered_affect_hours_setup(), nullptr);
    std::vector<bool> fast_updates;
    fast_updates.reserve(FAST_UPDATE_RATE);
    for (int pass_number = 1; pass_number <= FAST_UPDATE_RATE; ++pass_number) {
        const GameTickSchedule::WorldPass world_pass = wrapper.next_pass();
        fast_updates.push_back(world_pass.run_fast_update);
    }
    static_cast<void>(testing::internal::GetCapturedStderr());

    EXPECT_EQ(count_fast_updates(fast_updates), 0) << "Expected no world update without a channel.";
}
