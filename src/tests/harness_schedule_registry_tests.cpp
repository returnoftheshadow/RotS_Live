#include "schedule_definition.h"
#include "schedule_registry.h"
#include "schedule_setup.h"

#include <gtest/gtest.h>
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
