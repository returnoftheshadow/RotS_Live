#include "../game_tick_schedule.h"
#include "../real_time_tick_schedule.h"
#include "../structs.h"
#include "game_tick_schedule_contract.h"

#include <gtest/gtest.h>

namespace {
// The pulse count at which the reference counter restarts.
constexpr int REFERENCE_PULSE_RESTART = 2400;
// Game-loop passes in one game hour.
constexpr int PASSES_PER_GAME_HOUR = SECS_PER_MUD_HOUR * TICS_PER_SECOND;
// Enough passes to cross the reference counter's restart twice and run on into a third cycle.
constexpr int PASSES_TO_COMPARE = 2 * REFERENCE_PULSE_RESTART + PASSES_PER_GAME_HOUR;

// The reference cadence, worked out from a pulse counter that counts 1 to 2400 and then
// restarts at 1. It is written without RealTimeTickSchedule's constants so that a mistake in
// the schedule cannot hide in both.
GameTickSchedule::WorldPass reference_world_pass(int pulse)
{
    GameTickSchedule::WorldPass world_pass;
    world_pass.time_phase = (pulse % (SECS_PER_MUD_HOUR * 4)) / PULSE_FAST_UPDATE;
    world_pass.run_zone_update = !((pulse + 3) % PULSE_ZONE);
    world_pass.run_mobile_activity = !((pulse + 9) % PULSE_MOBILE);
    world_pass.run_violence = true;
    world_pass.run_hourly_update = !(pulse % (SECS_PER_MUD_HOUR * 4));
    world_pass.run_fast_update = !(pulse % PULSE_FAST_UPDATE);
    world_pass.run_skill_timer = !(pulse % 4);
    return world_pass;
}
} // namespace

TEST(GameTickSchedule, ADefaultWorldPassRunsNoWorldUpdate)
{
    const GameTickSchedule::WorldPass world_pass;

    EXPECT_EQ(world_pass.time_phase, 0) << "Expected a default pass to carry phase 0.";
    EXPECT_FALSE(world_pass.run_zone_update) << "Expected no zone update on a default pass.";
    EXPECT_FALSE(world_pass.run_mobile_activity)
        << "Expected no mobile activity on a default pass.";
    EXPECT_FALSE(world_pass.run_violence) << "Expected no combat on a default pass.";
    EXPECT_FALSE(world_pass.run_hourly_update) << "Expected no hourly update on a default pass.";
    EXPECT_FALSE(world_pass.run_fast_update) << "Expected no fast update on a default pass.";
    EXPECT_FALSE(world_pass.run_skill_timer) << "Expected no skill timer on a default pass.";
}

TEST(RealTimeTickSchedule, MatchesTheReferenceCadenceOnEveryPass)
{
    RealTimeTickSchedule schedule;
    int reference_pulse = 0;

    for (int pass_number = 1; pass_number <= PASSES_TO_COMPARE; ++pass_number) {
        reference_pulse++;
        const GameTickSchedule::WorldPass expected = reference_world_pass(reference_pulse);
        const GameTickSchedule::WorldPass actual = schedule.next_pass();

        ASSERT_EQ(actual.time_phase, expected.time_phase)
            << "time_phase differs on pass " << pass_number;
        ASSERT_EQ(actual.run_zone_update, expected.run_zone_update)
            << "run_zone_update differs on pass " << pass_number;
        ASSERT_EQ(actual.run_mobile_activity, expected.run_mobile_activity)
            << "run_mobile_activity differs on pass " << pass_number;
        ASSERT_EQ(actual.run_violence, expected.run_violence)
            << "run_violence differs on pass " << pass_number;
        ASSERT_EQ(actual.run_hourly_update, expected.run_hourly_update)
            << "run_hourly_update differs on pass " << pass_number;
        ASSERT_EQ(actual.run_fast_update, expected.run_fast_update)
            << "run_fast_update differs on pass " << pass_number;
        ASSERT_EQ(actual.run_skill_timer, expected.run_skill_timer)
            << "run_skill_timer differs on pass " << pass_number;

        if (reference_pulse >= REFERENCE_PULSE_RESTART) {
            reference_pulse = 0;
        }
    }
}

TEST(RealTimeTickSchedule, KeepsThePhaseContractOnEveryPass)
{
    RealTimeTickSchedule schedule;
    int previous_phase = 0;

    for (int pass_number = 1; pass_number <= 2 * PASSES_PER_GAME_HOUR; ++pass_number) {
        const GameTickSchedule::WorldPass world_pass = schedule.next_pass();

        ASSERT_TRUE(keeps_the_phase_contract(previous_phase, world_pass))
            << "on pass " << pass_number;
        previous_phase = world_pass.time_phase;
    }
}

TEST(RealTimeTickSchedule, RunsOneHourlyUpdateAndOneFastUpdatePerPhaseEachGameHour)
{
    RealTimeTickSchedule schedule;
    int hourly_updates = 0;
    int fast_updates = 0;

    for (int pass_number = 1; pass_number <= PASSES_PER_GAME_HOUR; ++pass_number) {
        const GameTickSchedule::WorldPass world_pass = schedule.next_pass();
        if (world_pass.run_hourly_update) {
            hourly_updates++;
        }
        if (world_pass.run_fast_update) {
            fast_updates++;
        }
    }

    EXPECT_EQ(hourly_updates, 1) << "Expected exactly one hourly update in one game hour.";
    EXPECT_EQ(fast_updates, FAST_UPDATE_RATE)
        << "Expected one fast update per phase in one game hour.";
}
