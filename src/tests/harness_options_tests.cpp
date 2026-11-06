#include "affect_hours_registration.h"
#include "harness_options.h"
#include "schedule_definition.h"
#include "schedule_registry.h"
#include "schedule_setup.h"
#include "test_command_line.h"

#include <gtest/gtest.h>
#include <string>
#include <utility>
#include <vector>

namespace {
// A schedule name the options mark as unchanged, to show a refused parse left them alone.
constexpr const char* UNTOUCHED_SCHEDULE = "untouched";
// A test-only schedule registered after affect-hours, to show the order of the registered names.
constexpr const char* SECOND_SCHEDULE = "second-test-schedule";

// A registry holding the affect-hours schedule.
harness::ScheduleRegistry make_registry()
{
    harness::ScheduleRegistry registry;
    const bool registered = harness::register_affect_hours_schedule(registry);
    EXPECT_TRUE(registered) << "Expected the affect-hours schedule to register.";
    return registry;
}

// Parses arguments, expects a refusal that leaves the options unchanged, and returns the error.
std::string expect_refused(std::vector<std::string> arguments)
{
    const harness::ScheduleRegistry registry = make_registry();
    harness_tests::CommandLine command_line(std::move(arguments));
    harness::HarnessOptions options;
    options.schedule_name = UNTOUCHED_SCHEDULE;
    std::string error;

    EXPECT_FALSE(harness::parse_harness_options(
        command_line.count(), command_line.vector(), registry, options, error))
        << "Expected the command line to be refused.";
    EXPECT_EQ(options.schedule_name, UNTOUCHED_SCHEDULE) << "Expected the options unchanged.";
    return error;
}
} // namespace

TEST(HarnessOptions, ReadsTheScheduleAndAgelandsOptions)
{
    const harness::ScheduleRegistry registry = make_registry();
    const std::string schedule(harness::AFFECT_HOURS_SCHEDULE_NAME);
    harness_tests::CommandLine command_line({ "ageland_harness",
        std::string(harness::SCHEDULE_OPTION), schedule, "--random-seed", "7", "-d", "testlib",
        "4321" });
    harness::HarnessOptions options;
    std::string error;

    ASSERT_TRUE(harness::parse_harness_options(
        command_line.count(), command_line.vector(), registry, options, error))
        << error;
    EXPECT_EQ(options.schedule_name, schedule) << "Expected the schedule name.";
    EXPECT_EQ(options.startup.port, 4321) << "Expected the port.";
    EXPECT_EQ(options.startup.dir, "testlib") << "Expected the data directory.";
    ASSERT_TRUE(options.startup.random_seed.has_value()) << "Expected the seed.";
    EXPECT_EQ(*options.startup.random_seed, 7u) << "Expected seed 7.";
}

TEST(HarnessOptions, AcceptsTheScheduleAfterAgelandsOptions)
{
    const harness::ScheduleRegistry registry = make_registry();
    const std::string schedule(harness::AFFECT_HOURS_SCHEDULE_NAME);
    harness_tests::CommandLine command_line({ "ageland_harness", "-d", "testlib",
        std::string(harness::SCHEDULE_OPTION), schedule, "4321" });
    harness::HarnessOptions options;
    std::string error;

    ASSERT_TRUE(harness::parse_harness_options(
        command_line.count(), command_line.vector(), registry, options, error))
        << error;
    EXPECT_EQ(options.schedule_name, schedule) << "Expected the schedule name.";
    EXPECT_EQ(options.startup.port, 4321) << "Expected the port.";
}

TEST(HarnessOptions, RefusesNoCommandLine)
{
    const harness::ScheduleRegistry registry = make_registry();
    harness::HarnessOptions options;
    options.schedule_name = UNTOUCHED_SCHEDULE;
    std::string error;

    EXPECT_FALSE(harness::parse_harness_options(0, nullptr, registry, options, error))
        << "Expected no command line to be refused.";
    EXPECT_EQ(error, "no command line") << "Unexpected refusal.";
    EXPECT_EQ(options.schedule_name, UNTOUCHED_SCHEDULE) << "Expected the options unchanged.";
}

TEST(HarnessOptions, RefusesAMissingSchedule)
{
    const std::string error = expect_refused({ "ageland_harness", "-d", "testlib", "4321" });

    EXPECT_EQ(error, "--schedule is required") << "Unexpected refusal.";
}

TEST(HarnessOptions, RefusesARepeatedSchedule)
{
    const std::string option(harness::SCHEDULE_OPTION);
    const std::string schedule(harness::AFFECT_HOURS_SCHEDULE_NAME);
    const std::string error
        = expect_refused({ "ageland_harness", option, schedule, option, schedule, "4321" });

    EXPECT_EQ(error, "--schedule is given more than once") << "Unexpected refusal.";
}

TEST(HarnessOptions, RefusesAScheduleOptionWithNoName)
{
    const std::string error = expect_refused(
        { "ageland_harness", "-d", "testlib", std::string(harness::SCHEDULE_OPTION) });

    EXPECT_EQ(error, "--schedule needs a value (NAME)") << "Unexpected refusal.";
}

TEST(HarnessOptions, RefusesAScheduleNameAttachedWithEquals)
{
    const std::string attached = std::string(harness::SCHEDULE_OPTION) + "="
        + std::string(harness::AFFECT_HOURS_SCHEDULE_NAME);
    const std::string error = expect_refused({ "ageland_harness", attached, "4321" });

    EXPECT_EQ(error, "--schedule takes its value as the next argument")
        << "Expected the refusal to say the value is the next argument.";
}

TEST(HarnessOptions, RefusesAnUnknownScheduleListingTheRegisteredOnes)
{
    const std::string error
        = expect_refused({ "ageland_harness", std::string(harness::SCHEDULE_OPTION), "fast" });

    EXPECT_EQ(error, "unknown schedule 'fast'; registered schedules: affect-hours")
        << "Expected the refusal to list the registered schedules.";
}

TEST(HarnessOptions, RefusesAnUnknownScheduleListingEveryRegisteredOneInOrder)
{
    harness::ScheduleRegistry registry = make_registry();
    harness::ScheduleDefinition second;
    second.name = SECOND_SCHEDULE;
    second.summary = "a test-only schedule";
    second.create_setup = []() -> harness::ScheduleSetup { return harness::ScheduleSetup {}; };
    ASSERT_TRUE(registry.register_schedule(std::move(second)))
        << "Expected the second schedule to register.";
    harness_tests::CommandLine command_line(
        { "ageland_harness", std::string(harness::SCHEDULE_OPTION), "fast" });
    harness::HarnessOptions options;
    options.schedule_name = UNTOUCHED_SCHEDULE;
    std::string error;

    EXPECT_FALSE(harness::parse_harness_options(
        command_line.count(), command_line.vector(), registry, options, error))
        << "Expected an unknown schedule to be refused.";
    EXPECT_EQ(error,
        "unknown schedule 'fast'; registered schedules: affect-hours, "
            + std::string(SECOND_SCHEDULE))
        << "Expected every registered schedule, in registration order.";
    EXPECT_EQ(options.schedule_name, UNTOUCHED_SCHEDULE) << "Expected the options unchanged.";
}

TEST(HarnessOptions, RefusesAnEmptyScheduleName)
{
    const std::string error
        = expect_refused({ "ageland_harness", std::string(harness::SCHEDULE_OPTION), "" });

    EXPECT_EQ(error, "unknown schedule ''; registered schedules: affect-hours")
        << "Expected an empty name to be an unknown schedule.";
}

TEST(HarnessOptions, RefusesAgelandOptionsThatDoNotParse)
{
    expect_refused({ "ageland_harness", std::string(harness::SCHEDULE_OPTION),
        std::string(harness::AFFECT_HOURS_SCHEDULE_NAME), "4321", "4322" });
}

TEST(HarnessOptions, WritesNothingWhenAgelandRefusesAfterTheScheduleIsAccepted)
{
    const harness::ScheduleRegistry registry = make_registry();
    harness_tests::CommandLine command_line({ "ageland_harness",
        std::string(harness::SCHEDULE_OPTION), std::string(harness::AFFECT_HOURS_SCHEDULE_NAME),
        "-z" });
    harness::HarnessOptions options;
    options.schedule_name = UNTOUCHED_SCHEDULE;
    options.startup.port = 1234;
    options.startup.dir = UNTOUCHED_SCHEDULE;
    std::string error;

    EXPECT_FALSE(harness::parse_harness_options(
        command_line.count(), command_line.vector(), registry, options, error))
        << "Expected ageland's parser to refuse -z.";
    EXPECT_EQ(options.schedule_name, UNTOUCHED_SCHEDULE) << "Expected the schedule unchanged.";
    EXPECT_EQ(options.startup.port, 1234) << "Expected the port unchanged.";
    EXPECT_EQ(options.startup.dir, UNTOUCHED_SCHEDULE) << "Expected the directory unchanged.";
}

TEST(HarnessOptions, UsageGivesTheOptionTheSchedulesAndAgelandsOptions)
{
    const harness::ScheduleRegistry registry = make_registry();
    const harness::ScheduleDefinition* const definition
        = registry.find(harness::AFFECT_HOURS_SCHEDULE_NAME);
    ASSERT_NE(definition, nullptr) << "Expected the affect-hours definition.";

    const std::string usage = harness::harness_usage(registry, "my_harness");

    EXPECT_EQ(usage.rfind("Usage: my_harness ", 0), 0u)
        << "Expected the usage to start with the program name; got: " << usage;
    EXPECT_NE(usage.find("--schedule NAME"), std::string::npos)
        << "Expected the schedule option; got: " << usage;
    EXPECT_NE(usage.find(harness::harness_option_parser(registry).usage()), std::string::npos)
        << "Expected the option parser's whole usage block; got: " << usage;
    EXPECT_NE(usage.find(std::string(harness::AFFECT_HOURS_SCHEDULE_NAME) + "  "
                  + definition->summary),
        std::string::npos)
        << "Expected the schedule's name and summary; got: " << usage;
    EXPECT_NE(usage.find(" [-m] [-q] [-r] [-s] [-x] [-d pathname] [-p port #] [--random-seed n]"
                         " [ port # ]"),
        std::string::npos)
        << "Expected ageland's options; got: " << usage;
    EXPECT_EQ(usage.back(), '\n') << "Expected the usage to end with a newline.";
}
