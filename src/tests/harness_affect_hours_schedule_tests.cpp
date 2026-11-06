#include "affect_hours_schedule.h"

#include "../game_tick_schedule.h"
#include "../real_time_tick_schedule.h"
#include "../structs.h"
#include "game_tick_schedule_contract.h"

#include <gtest/gtest.h>
#include <optional>
#include <string>
#include <vector>

namespace {
// Game-loop passes in one game hour on real time.
constexpr int REAL_TIME_PASSES_PER_HOUR = SECS_PER_MUD_HOUR * TICS_PER_SECOND;

// The phase and hourly flag of one pass that runs the fast update.
struct FastPass {
    // The phase the pass runs the fast update at.
    int time_phase = 0;
    // Whether the pass also runs the hourly update.
    bool run_hourly_update = false;

    bool operator==(const FastPass& other) const
    {
        return time_phase == other.time_phase && run_hourly_update == other.run_hourly_update;
    }
};

// Asks schedule for pass_count passes and returns the ones that run the fast update, in order.
std::vector<FastPass> fast_passes_of(GameTickSchedule& schedule, int pass_count)
{
    std::vector<FastPass> fast_passes;
    for (int pass_number = 1; pass_number <= pass_count; ++pass_number) {
        const GameTickSchedule::WorldPass world_pass = schedule.next_pass();
        if (world_pass.run_fast_update) {
            FastPass fast_pass;
            fast_pass.time_phase = world_pass.time_phase;
            fast_pass.run_hourly_update = world_pass.run_hourly_update;
            fast_passes.push_back(fast_pass);
        }
    }
    return fast_passes;
}

// Whether world_pass runs any update other than the hourly and fast updates.
bool runs_other_updates(const GameTickSchedule::WorldPass& world_pass)
{
    return world_pass.run_zone_update || world_pass.run_mobile_activity
        || world_pass.run_violence || world_pass.run_skill_timer;
}

// Grants hours to schedule and fails the test when they are refused.
void grant_or_fail(harness::AffectHoursSchedule& schedule, int hours)
{
    const std::optional<std::string> refusal = schedule.grant_hours(hours);
    ASSERT_FALSE(refusal.has_value()) << "Expected " << hours << " hours to be granted, got: "
                                      << refusal.value_or("");
}
} // namespace

TEST(AffectHoursSchedule, RunsNoWorldUpdateAtPhaseZeroUntilHoursAreGranted)
{
    harness::AffectHoursSchedule schedule;

    for (int pass_number = 1; pass_number <= FAST_UPDATE_RATE; ++pass_number) {
        const GameTickSchedule::WorldPass world_pass = schedule.next_pass();
        EXPECT_FALSE(world_pass.run_fast_update)
            << "Expected no fast update on pass " << pass_number;
        EXPECT_FALSE(world_pass.run_hourly_update)
            << "Expected no hourly update on pass " << pass_number;
        EXPECT_FALSE(runs_other_updates(world_pass))
            << "Expected no other update on pass " << pass_number;
        EXPECT_EQ(world_pass.time_phase, 0) << "Expected phase 0 on idle pass " << pass_number;
    }
    EXPECT_EQ(schedule.hours_completed(), 0) << "Expected no hour to have run.";
}

TEST(AffectHoursSchedule, RunsAGrantedHourAsFastUpdatesThroughThePhasesEndingWithTheHourlyUpdate)
{
    harness::AffectHoursSchedule schedule;
    grant_or_fail(schedule, 1);

    for (int pass_number = 1; pass_number <= FAST_UPDATE_RATE; ++pass_number) {
        const GameTickSchedule::WorldPass world_pass = schedule.next_pass();
        const bool last_pass_of_hour = pass_number == FAST_UPDATE_RATE;
        EXPECT_TRUE(world_pass.run_fast_update) << "Expected a fast update on pass " << pass_number;
        EXPECT_EQ(world_pass.time_phase, pass_number % FAST_UPDATE_RATE)
            << "Unexpected phase on pass " << pass_number;
        EXPECT_EQ(world_pass.run_hourly_update, last_pass_of_hour)
            << "Expected the hourly update only on the hour's last pass; pass " << pass_number;
    }

    const GameTickSchedule::WorldPass after_hour = schedule.next_pass();
    EXPECT_FALSE(after_hour.run_fast_update) << "Expected the schedule to stop after the hour.";
    EXPECT_EQ(after_hour.time_phase, 0) << "Expected the schedule to rest at phase 0.";
    EXPECT_EQ(schedule.hours_completed(), 1) << "Expected one completed hour.";
    EXPECT_EQ(schedule.pending_passes(), 0) << "Expected nothing left pending.";
}

TEST(AffectHoursSchedule, KeepsThePhaseContractOnEveryPass)
{
    harness::AffectHoursSchedule schedule;
    grant_or_fail(schedule, 3);
    int previous_phase = 0;

    for (int pass_number = 1; pass_number <= 3 * FAST_UPDATE_RATE + 5; ++pass_number) {
        const GameTickSchedule::WorldPass world_pass = schedule.next_pass();
        EXPECT_TRUE(keeps_the_phase_contract(previous_phase, world_pass))
            << "on pass " << pass_number;
        previous_phase = world_pass.time_phase;
    }
}

TEST(AffectHoursSchedule, NeverRunsZoneUpdatesMobileActivityCombatOrSkillTimers)
{
    harness::AffectHoursSchedule schedule;
    grant_or_fail(schedule, 2);

    for (int pass_number = 1; pass_number <= 2 * FAST_UPDATE_RATE + 3; ++pass_number) {
        const GameTickSchedule::WorldPass world_pass = schedule.next_pass();
        EXPECT_FALSE(runs_other_updates(world_pass))
            << "Expected only hourly and fast updates; pass " << pass_number;
    }
}

TEST(AffectHoursSchedule, QueuesASecondGrantBehindTheOneRunning)
{
    harness::AffectHoursSchedule schedule;
    grant_or_fail(schedule, 1);
    constexpr int PASSES_BEFORE_SECOND_GRANT = 5;
    for (int pass_number = 1; pass_number <= PASSES_BEFORE_SECOND_GRANT; ++pass_number) {
        static_cast<void>(schedule.next_pass());
    }

    grant_or_fail(schedule, 2);
    EXPECT_EQ(schedule.pending_passes(), 3 * FAST_UPDATE_RATE - PASSES_BEFORE_SECOND_GRANT)
        << "Expected the second grant's passes to follow the first's.";

    int granted_passes_run = 0;
    while (schedule.pending_passes() > 0) {
        const GameTickSchedule::WorldPass world_pass = schedule.next_pass();
        EXPECT_TRUE(world_pass.run_fast_update) << "Expected every granted pass to run.";
        granted_passes_run++;
    }
    EXPECT_EQ(granted_passes_run, 3 * FAST_UPDATE_RATE - PASSES_BEFORE_SECOND_GRANT)
        << "Expected the queued passes to run without a gap.";
    EXPECT_EQ(schedule.hours_completed(), 3) << "Expected three completed hours.";
}

TEST(AffectHoursSchedule, RefusesAGrantOfFewerThanOneHourAndKeepsItsState)
{
    harness::AffectHoursSchedule schedule;

    EXPECT_TRUE(schedule.grant_hours(0).has_value()) << "Expected 0 hours to be refused.";
    EXPECT_TRUE(schedule.grant_hours(-3).has_value()) << "Expected -3 hours to be refused.";
    EXPECT_EQ(schedule.pending_passes(), 0) << "Expected a refusal to grant nothing.";
}

TEST(AffectHoursSchedule, RefusesAGrantThatWouldLeaveMoreThanTheMaximumHoursPending)
{
    harness::AffectHoursSchedule schedule;
    grant_or_fail(schedule, harness::AffectHoursSchedule::MAX_PENDING_HOURS);

    EXPECT_TRUE(schedule.grant_hours(1).has_value()) << "Expected one more hour to be refused.";
    EXPECT_EQ(schedule.pending_passes(),
        harness::AffectHoursSchedule::MAX_PENDING_HOURS * FAST_UPDATE_RATE)
        << "Expected the refusal to leave the pending passes unchanged.";

    static_cast<void>(schedule.next_pass());
    EXPECT_TRUE(schedule.grant_hours(1).has_value())
        << "Expected an hour under way to count as pending.";

    for (int pass_number = 2; pass_number <= FAST_UPDATE_RATE; ++pass_number) {
        static_cast<void>(schedule.next_pass());
    }
    EXPECT_FALSE(schedule.grant_hours(1).has_value())
        << "Expected an hour to be grantable once the first hour has run.";
}

TEST(AffectHoursSchedule, OrdersAnHoursFastAndHourlyUpdatesAsRealTimeDoes)
{
    RealTimeTickSchedule real_time;
    const std::vector<FastPass> real_time_hour =
        fast_passes_of(real_time, REAL_TIME_PASSES_PER_HOUR);
    ASSERT_EQ(real_time_hour.size(), static_cast<std::size_t>(FAST_UPDATE_RATE))
        << "Expected one real-time hour to hold FAST_UPDATE_RATE fast updates.";

    harness::AffectHoursSchedule schedule;
    grant_or_fail(schedule, 1);
    const std::vector<FastPass> granted_hour = fast_passes_of(schedule, FAST_UPDATE_RATE);

    EXPECT_TRUE(granted_hour == real_time_hour)
        << "Expected a granted hour's phases and hourly update in real time's order.";
}
