/* command_line_option.h */

#ifndef COMMAND_LINE_OPTION_H
#define COMMAND_LINE_OPTION_H

#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace harness {

// One option a CommandLineParser claims, written as its name followed by its value as the next
// argument.
struct CommandLineOption {
    // The option as written on the command line; it starts with "--".
    std::string name;
    // What the usage text calls the option's value, such as "NAME".
    std::string value_name;
    // A one-line description for the usage text.
    std::string help;
    // Whether a command line without the option is refused.
    bool required = false;
    // Returns why value is refused, or nothing when it is accepted. It must be pure and write
    // nothing, so a refused parse leaves no partial result. An empty validator accepts every
    // value.
    std::function<std::optional<std::string>(std::string_view value)> validate;
};

} // namespace harness

#endif /* COMMAND_LINE_OPTION_H */
