#include "../crashsave_schedule.h"
#include "../game_heartbeat.h"
#include "../game_tick_schedule.h"
#include "../structs.h"
#include "../utils.h"

#include <cstddef>
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <string_view>

extern struct char_data* combat_list;
extern struct descriptor_data* descriptor_list;

namespace {
// The phase IdleTickSchedule reports unless a test changes it; it differs from the phase 0
// the fixture restores.
constexpr int SCHEDULE_PHASE = 7;
// Passes a test runs. The fixture checks this is fewer than the shortest crash-save interval,
// so no pass saves players.
constexpr int PASSES_TO_RUN = 20;
// The words the heartbeat logs when it has no schedule.
constexpr std::string_view SCHEDULE_LOSS_PHRASE = "no tick schedule";

// A schedule that asks for no world update, reports the phase a test gives it and counts how
// often it is asked.
class IdleTickSchedule : public GameTickSchedule {
public:
    [[nodiscard]] WorldPass next_pass() override
    {
        passes_requested++;
        WorldPass world_pass;
        world_pass.time_phase = phase_to_report;
        return world_pass;
    }

    // The phase the next pass reports.
    int phase_to_report = SCHEDULE_PHASE;
    // How many times next_pass() has been called.
    int passes_requested = 0;
};

// Gives each test an empty descriptor list and combat list for the heartbeat's housekeeping
// to walk, and restores both lists and phase 0 afterwards.
class GameHeartbeatTest : public testing::Test {
protected:
    void SetUp() override
    {
        previous_descriptor_list = descriptor_list;
        descriptor_list = nullptr;
        previous_combat_list = combat_list;
        combat_list = nullptr;

        const int shortest_crash_save_interval = autosave_interval_pulses(0, TICS_PER_SECOND);
        ASSERT_LT(PASSES_TO_RUN, shortest_crash_save_interval)
            << "Expected the tests to stop before a crash-save could fire.";
    }

    void TearDown() override
    {
        descriptor_list = previous_descriptor_list;
        combat_list = previous_combat_list;
        set_current_time_phase(0);
    }

private:
    // The descriptor list in place before the test emptied it.
    descriptor_data* previous_descriptor_list = nullptr;
    // The combat list in place before the test emptied it.
    char_data* previous_combat_list = nullptr;
};

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

TEST_F(GameHeartbeatTest, AsksTheScheduleOncePerPass)
{
    const std::shared_ptr<IdleTickSchedule> schedule = std::make_shared<IdleTickSchedule>();
    GameHeartbeat heartbeat(schedule);

    for (int pass_number = 1; pass_number <= PASSES_TO_RUN; ++pass_number) {
        heartbeat.run_pass();
    }

    EXPECT_EQ(schedule->passes_requested, PASSES_TO_RUN)
        << "Expected one next_pass() call for each run_pass() call.";
}

TEST_F(GameHeartbeatTest, PublishesThePhaseOfEachPass)
{
    const std::shared_ptr<IdleTickSchedule> schedule = std::make_shared<IdleTickSchedule>();
    GameHeartbeat heartbeat(schedule);

    for (int phase_of_pass = 0; phase_of_pass < PASSES_TO_RUN; ++phase_of_pass) {
        // Starts at phase 1, so the first pass cannot pass on the phase 0 the fixture restores.
        schedule->phase_to_report = (phase_of_pass + 1) % FAST_UPDATE_RATE;
        heartbeat.run_pass();

        const int current_phase = get_current_time_phase();
        ASSERT_EQ(current_phase, schedule->phase_to_report)
            << "Expected pass " << phase_of_pass + 1 << " to publish the phase reported for it.";
    }
}

TEST_F(GameHeartbeatTest, DoesNotKeepTheScheduleAlive)
{
    std::shared_ptr<IdleTickSchedule> schedule = std::make_shared<IdleTickSchedule>();
    const std::weak_ptr<IdleTickSchedule> schedule_observer = schedule;
    GameHeartbeat heartbeat(schedule);

    schedule.reset();

    EXPECT_TRUE(schedule_observer.expired())
        << "Expected the schedule to be destroyed once its only owner released it.";
}

TEST_F(GameHeartbeatTest, LogsOnceAndKeepsThePublishedPhaseWhenTheScheduleIsGone)
{
    std::shared_ptr<IdleTickSchedule> schedule = std::make_shared<IdleTickSchedule>();
    GameHeartbeat heartbeat(schedule);
    heartbeat.run_pass();
    schedule.reset();

    testing::internal::CaptureStderr();
    for (int pass_number = 1; pass_number <= PASSES_TO_RUN; ++pass_number) {
        heartbeat.run_pass();
    }
    const std::string logged = testing::internal::GetCapturedStderr();

    const int loss_reports = count_occurrences(logged, SCHEDULE_LOSS_PHRASE);
    EXPECT_EQ(loss_reports, 1) << "Expected the loss to be logged once. Logged: " << logged;
    const int current_phase = get_current_time_phase();
    EXPECT_EQ(current_phase, SCHEDULE_PHASE)
        << "Expected the last published phase to stand once the schedule is gone.";
}

TEST_F(GameHeartbeatTest, ReportsAMissingScheduleWhenBuiltAndNotAgain)
{
    testing::internal::CaptureStderr();
    GameHeartbeat heartbeat(nullptr);
    const std::string logged_when_built = testing::internal::GetCapturedStderr();

    testing::internal::CaptureStderr();
    for (int pass_number = 1; pass_number <= PASSES_TO_RUN; ++pass_number) {
        heartbeat.run_pass();
    }
    const std::string logged_by_passes = testing::internal::GetCapturedStderr();

    const int reports_when_built = count_occurrences(logged_when_built, SCHEDULE_LOSS_PHRASE);
    EXPECT_EQ(reports_when_built, 1)
        << "Expected the missing schedule to be logged when the heartbeat is built. Logged: "
        << logged_when_built;
    const int reports_by_passes = count_occurrences(logged_by_passes, SCHEDULE_LOSS_PHRASE);
    EXPECT_EQ(reports_by_passes, 0)
        << "Expected no further report from the passes. Logged: " << logged_by_passes;
}
