#include "advance_hours_request.h"
#include "fake_control_channel.h"
#include "harness_controlled_schedule.h"
#include "real_time_registration.h"
#include "run_token.h"
#include "schedule_definition.h"
#include "schedule_registry.h"
#include "schedule_setup.h"

#include "../game_tick_schedule.h"
#include "../real_time_tick_schedule.h"
#include "../structs.h"

#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
// The words the registry logs when it refuses a duplicate name.
constexpr std::string_view DUPLICATE_PHRASE = "already registered";
// The words the registry logs when it refuses a definition with no factory.
constexpr std::string_view NO_FACTORY_PHRASE = "has no factory";

// A definition named schedule_name, with summary and a factory that builds an empty setup.
harness::ScheduleDefinition make_definition(
    std::string_view schedule_name, std::string_view summary)
{
    harness::ScheduleDefinition definition;
    definition.name = std::string(schedule_name);
    definition.summary = std::string(summary);
    definition.create_setup = []() -> harness::ScheduleSetup { return harness::ScheduleSetup {}; };
    return definition;
}

// Passes enough to cover two game hours of the real-time cadence, counted as
// real_time_tick_schedule.cpp counts a game hour.
constexpr int TWO_REAL_TIME_HOURS_OF_PASSES = 2 * SECS_PER_MUD_HOUR * TICS_PER_SECOND;

// Builds the real-time setup through its registration and the registry's factory.
harness::ScheduleSetup create_registered_real_time_setup()
{
    harness::ScheduleRegistry registry;
    const bool registered = harness::register_real_time_schedule(registry);
    EXPECT_TRUE(registered) << "Expected real-time to register.";
    const harness::ScheduleDefinition* const definition
        = registry.find(harness::REAL_TIME_SCHEDULE_NAME);
    if (definition == nullptr) {
        ADD_FAILURE() << "Expected real-time to be found.";
        return harness::ScheduleSetup { };
    }
    return definition->create_setup();
}

// Returns whether the two passes run the same updates at the same phase.
bool same_pass(const GameTickSchedule::WorldPass& left, const GameTickSchedule::WorldPass& right)
{
    return left.time_phase == right.time_phase && left.run_zone_update == right.run_zone_update
        && left.run_mobile_activity == right.run_mobile_activity
        && left.run_violence == right.run_violence
        && left.run_hourly_update == right.run_hourly_update
        && left.run_fast_update == right.run_fast_update
        && left.run_skill_timer == right.run_skill_timer;
}
} // namespace

TEST(ScheduleRegistry, FindsARegisteredDefinitionByName)
{
    harness::ScheduleRegistry registry;
    ASSERT_TRUE(registry.register_schedule(make_definition("first", "the first")))
        << "Expected first to register.";
    ASSERT_TRUE(registry.register_schedule(make_definition("second", "the second")))
        << "Expected second to register.";

    const harness::ScheduleDefinition* const found = registry.find("second");

    ASSERT_NE(found, nullptr) << "Expected the second definition.";
    EXPECT_EQ(found->name, "second") << "Unexpected definition name.";
    EXPECT_EQ(found->summary, "the second") << "Unexpected definition summary.";
}

TEST(ScheduleRegistry, FindsNothingForAnUnregisteredName)
{
    harness::ScheduleRegistry registry;
    ASSERT_TRUE(registry.register_schedule(make_definition("known", "known")))
        << "Expected the definition to register.";

    EXPECT_EQ(registry.find("unknown"), nullptr) << "Expected nothing for an unknown name.";
    EXPECT_EQ(registry.find("know"), nullptr) << "Expected a prefix not to match.";
    EXPECT_EQ(registry.find(""), nullptr) << "Expected nothing for an empty name.";
}

TEST(ScheduleRegistry, ListsTheNamesInRegistrationOrder)
{
    harness::ScheduleRegistry registry;
    const std::vector<std::string> registration_order = { "zulu", "alpha", "mike" };
    for (const std::string_view schedule_name : registration_order) {
        ASSERT_TRUE(registry.register_schedule(make_definition(schedule_name, "summary")))
            << "Expected " << schedule_name << " to register.";
    }

    EXPECT_EQ(registry.names(), registration_order) << "Expected the registration order.";
}

TEST(ScheduleRegistry, ListsNoNamesWhenEmpty)
{
    const harness::ScheduleRegistry registry;

    EXPECT_TRUE(registry.names().empty()) << "Expected no names.";
}

TEST(ScheduleRegistry, RefusesAndLogsADuplicateNameKeepingTheFirst)
{
    harness::ScheduleRegistry registry;
    ASSERT_TRUE(registry.register_schedule(make_definition("twice", "the first")))
        << "Expected first to register.";

    testing::internal::CaptureStderr();
    const bool duplicate_registered
        = registry.register_schedule(make_definition("twice", "the second"));
    const std::string logged = testing::internal::GetCapturedStderr();

    EXPECT_FALSE(duplicate_registered) << "Expected the duplicate refused.";
    EXPECT_NE(logged.find(DUPLICATE_PHRASE), std::string::npos)
        << "Expected the refusal logged; log was: " << logged;
    const harness::ScheduleDefinition* const found = registry.find("twice");
    ASSERT_NE(found, nullptr) << "Expected the first definition kept.";
    EXPECT_EQ(found->summary, "the first") << "Expected the first definition kept.";
    const std::vector<std::string> expected_names = { "twice" };
    EXPECT_EQ(registry.names(), expected_names) << "Expected the name listed once.";
}

TEST(ScheduleRegistry, RefusesAndLogsADefinitionWithNoFactory)
{
    harness::ScheduleRegistry registry;
    harness::ScheduleDefinition definition = make_definition("unbuildable", "summary");
    definition.create_setup = nullptr;

    testing::internal::CaptureStderr();
    const bool registered = registry.register_schedule(std::move(definition));
    const std::string logged = testing::internal::GetCapturedStderr();

    EXPECT_FALSE(registered) << "Expected a definition with no factory refused.";
    EXPECT_NE(logged.find(NO_FACTORY_PHRASE), std::string::npos)
        << "Expected the refusal logged; log was: " << logged;
    EXPECT_EQ(registry.find("unbuildable"), nullptr) << "Expected nothing registered.";
}

TEST(ScheduleRegistry, RegistersRealTimeWithNoRequestsAndAnEmptyReadyState)
{
    harness::ScheduleSetup setup = create_registered_real_time_setup();

    ASSERT_NE(setup.schedule, nullptr) << "Expected the factory to build a schedule.";
    EXPECT_EQ(setup.ready_state, "") << "Expected an empty ready state.";
    EXPECT_EQ(setup.requests.find(harness::AdvanceHoursRequest::NAME), nullptr)
        << "Expected real-time to accept no advance-hours request.";
}

TEST(ScheduleRegistry, RealTimeRunsTheLiveServersCadence)
{
    harness::ScheduleSetup setup = create_registered_real_time_setup();
    ASSERT_NE(setup.schedule, nullptr) << "Expected the factory to build a schedule.";
    // The schedule as ageland_harness runs it: inside the wrapper that answers the control channel.
    harness::HarnessControlledSchedule harness_schedule(harness::REAL_TIME_SCHEDULE_NAME, std::move(setup),
        std::make_unique<harness_tests::FakeControlChannel>(
            std::make_shared<harness_tests::FakeControlChannel::State>()));
    RealTimeTickSchedule live_schedule;

    for (int pass_number = 1; pass_number <= TWO_REAL_TIME_HOURS_OF_PASSES; ++pass_number) {
        const GameTickSchedule::WorldPass harness_pass = harness_schedule.next_pass();
        const GameTickSchedule::WorldPass live_pass = live_schedule.next_pass();
        ASSERT_TRUE(same_pass(harness_pass, live_pass)) << "Passes differ at pass " << pass_number;
    }
}

TEST(ScheduleRegistry, RealTimesReadyReplyCarriesOnlyTheEntryToken)
{
    harness::ScheduleSetup setup = create_registered_real_time_setup();
    setup.ready_state = harness::with_entry_token(setup.ready_state, "00ff");
    std::shared_ptr<harness_tests::FakeControlChannel::State> channel_state
        = std::make_shared<harness_tests::FakeControlChannel::State>();
    harness::HarnessControlledSchedule wrapper(harness::REAL_TIME_SCHEDULE_NAME, std::move(setup),
        std::make_unique<harness_tests::FakeControlChannel>(channel_state));

    static_cast<void>(wrapper.next_pass());

    const std::vector<std::string> expected = { "ready schedule=real-time entry=00ff" };
    EXPECT_EQ(channel_state->replies, expected) << "Unexpected ready reply.";
}
