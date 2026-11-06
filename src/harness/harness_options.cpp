/* harness_options.cpp */

#include "harness_options.h"

#include "command_line_option.h"
#include "command_line_parse_result.h"
#include "schedule_definition.h"

#include <optional>
#include <utility>
#include <vector>

namespace harness {

namespace {
// What the usage text calls the schedule option's value.
constexpr std::string_view SCHEDULE_VALUE_NAME = "NAME";
// The schedule option's line in the usage text.
constexpr std::string_view SCHEDULE_HELP = "the registered schedule the run uses";
// ageland's options, as its own usage text gives them.
constexpr std::string_view AGELAND_OPTIONS_SUMMARY
    = " [-m] [-q] [-r] [-s] [-x] [-d pathname] [-p port #] [--random-seed n] [ port # ]";

// The registered names separated by ", ", or "none" when there are none.
[[nodiscard]] std::string join_schedule_names(const std::vector<std::string>& schedule_names)
{
    if (schedule_names.empty()) {
        return "none";
    }

    std::string joined;
    for (const std::string& schedule_name : schedule_names) {
        if (!joined.empty()) {
            joined += ", ";
        }
        joined += schedule_name;
    }
    return joined;
}
} // namespace

//============================================================================
CommandLineParser harness_option_parser(const ScheduleRegistry& schedules)
{
    CommandLineOption schedule_option;
    schedule_option.name = std::string(SCHEDULE_OPTION);
    schedule_option.value_name = std::string(SCHEDULE_VALUE_NAME);
    schedule_option.help = std::string(SCHEDULE_HELP);
    schedule_option.required = true;
    schedule_option.validate = [schedule_names = schedules.names()](
                                   std::string_view value) -> std::optional<std::string> {
        for (const std::string& schedule_name : schedule_names) {
            if (schedule_name == value) {
                return std::nullopt;
            }
        }
        return "unknown schedule '" + std::string(value)
            + "'; registered schedules: " + join_schedule_names(schedule_names);
    };

    CommandLineParser parser;
    // A refusal is logged by the parser; parse_harness_options then reports the option missing.
    const bool schedule_registered = parser.register_option(std::move(schedule_option));
    static_cast<void>(schedule_registered);
    return parser;
}

//============================================================================
bool parse_harness_options(int argc, char** argv, const ScheduleRegistry& schedules,
    HarnessOptions& out_options, std::string& out_error)
{
    if (argc < 1 || argv == nullptr) {
        out_error = "no command line";
        return false;
    }

    const CommandLineParser parser = harness_option_parser(schedules);
    CommandLineParseResult parsed;
    if (!parser.parse(argc, argv, parsed, out_error)) {
        return false;
    }

    const auto schedule_value = parsed.option_values.find(SCHEDULE_OPTION);
    if (schedule_value == parsed.option_values.end()) {
        out_error = std::string(SCHEDULE_OPTION) + " is required";
        return false;
    }

    StartupOptions startup {};
    std::string startup_error;
    // unclaimed_arguments holds at most argc entries, so its size fits an int.
    const int startup_argument_count = static_cast<int>(parsed.unclaimed_arguments.size());
    if (!parse_startup_options(startup_argument_count, parsed.unclaimed_arguments.data(),
            &startup, &startup_error)) {
        out_error = startup_error;
        return false;
    }

    out_options.schedule_name = std::move(schedule_value->second);
    out_options.startup = std::move(startup);
    return true;
}

//============================================================================
std::string harness_usage(const ScheduleRegistry& schedules, std::string_view program_name)
{
    std::string text = "Usage: " + std::string(program_name) + " <harness options>"
        + std::string(AGELAND_OPTIONS_SUMMARY) + "\n";
    text += "Harness options:\n";
    text += harness_option_parser(schedules).usage();
    text += "Schedules:\n";
    for (const std::string& schedule_name : schedules.names()) {
        const ScheduleDefinition* const definition = schedules.find(schedule_name);
        text += "  " + schedule_name;
        if (definition != nullptr) {
            text += "  " + definition->summary;
        }
        text += "\n";
    }
    return text;
}

} // namespace harness
