/* command_line_parser.h */

#ifndef COMMAND_LINE_PARSER_H
#define COMMAND_LINE_PARSER_H

#include "command_line_option.h"
#include "command_line_parse_result.h"

#include <array>
#include <string>
#include <string_view>
#include <vector>

namespace harness {

// The long options ageland's own parser reads, which no registered option may take, so they
// always reach parse_startup_options untouched.
inline constexpr std::array<std::string_view, 1> RESERVED_OPTION_NAMES = { "--random-seed" };

// Claims the registered options from a command line, and returns their values and the
// arguments it did not claim.
class CommandLineParser {
public:
    // Adds option. Returns true when it is registered. An option whose name does not start with
    // "--" followed by at least one character, whose name is in RESERVED_OPTION_NAMES, or whose
    // name is already registered, is logged, the parser is left unchanged, and false is returned.
    [[nodiscard]] bool register_option(CommandLineOption option);

    // Reads argv[0] to argv[argc - 1], each a non-null string, with argv[0] the program name.
    // A registered option is claimed wherever it appears after the program name, as the option's
    // name followed by its value as the next argument. That includes where it stands as an
    // unregistered option's value, such as ageland's "-d --schedule"; the harness launcher writes
    // its options first, so this is not handled further. An option followed by a registered
    // option's name has no value; any other next argument, even one starting with "--", is its
    // value. Returns true and fills out_result on success. Otherwise leaves out_result unchanged,
    // sets out_error and returns false: when argc < 1 or argv is null, when a registered option
    // is written as NAME=VALUE, is repeated, has no value or has a value its validator refuses,
    // or when a required option is missing. NAME=VALUE is refused only for registered names; any
    // other argument passes through unclaimed.
    [[nodiscard]] bool parse(int argc, char** argv, CommandLineParseResult& out_result,
        std::string& out_error) const;

    // One line per registered option, in registration order: its name, its value name and its
    // help text, with required options marked. Each line ends with a newline.
    [[nodiscard]] std::string usage() const;

private:
    // Returns the option registered under name, or null when there is none.
    [[nodiscard]] const CommandLineOption* find(std::string_view name) const;

    // The registered options, oldest first. A command line has a handful, so a linear search
    // costs less than a map and keeps the registration order for the usage text.
    std::vector<CommandLineOption> options;
};

} // namespace harness

#endif /* COMMAND_LINE_PARSER_H */
