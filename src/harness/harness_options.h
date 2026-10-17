/* harness_options.h */

#ifndef HARNESS_OPTIONS_H
#define HARNESS_OPTIONS_H

#include "comm.h"
#include "command_line_parser.h"
#include "schedule_registry.h"

#include <string>
#include <string_view>

namespace harness {

// The option that names the schedule a harness run uses.
inline constexpr std::string_view SCHEDULE_OPTION = "--schedule";

// The harness binary's command line.
struct HarnessOptions {
    // The name of the registered schedule the run uses.
    std::string schedule_name;
    // The options the harness binary takes as ageland does.
    StartupOptions startup {};
};

// Returns a parser with the harness binary's own options registered: SCHEDULE_OPTION, required,
// whose validator refuses a name schedules does not hold and lists the names it does. The
// validator keeps a copy of the names registered when it is built, so the parser does not borrow
// schedules. A registration the parser refuses is logged and the option is left out.
[[nodiscard]] CommandLineParser harness_option_parser(const ScheduleRegistry& schedules);

// Reads the harness binary's command line: the options harness_option_parser registers, anywhere
// after the program name, and ageland's own options, which pass to parse_startup_options
// unchanged and in order. Each of argv[0] to argv[argc - 1] must be a non-null string. Returns
// true and fills out_options once both parses succeed. Otherwise leaves out_options unchanged,
// sets out_error and returns false: when argc < 1 or argv is null (out_error is then "no command
// line"), when the harness options are refused (out_error then says why), or when ageland's
// options do not parse (out_error then holds ageland's message, which may be empty).
bool parse_harness_options(int argc, char** argv, const ScheduleRegistry& schedules,
    HarnessOptions& out_options, std::string& out_error);

// The harness binary's usage text for program_name: a synopsis with ageland's options, then the
// harness options with their help, then each registered schedule's name and summary in
// registration order. It ends with a newline.
[[nodiscard]] std::string harness_usage(
    const ScheduleRegistry& schedules, std::string_view program_name);

} // namespace harness

#endif /* HARNESS_OPTIONS_H */
