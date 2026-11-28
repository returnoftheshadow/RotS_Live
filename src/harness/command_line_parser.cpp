/* command_line_parser.cpp */

#include "command_line_parser.h"

#include "utils.h"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <utility>

namespace harness {

namespace {
// The prefix every registered option's name starts with.
constexpr std::string_view OPTION_PREFIX = "--";
} // namespace

//============================================================================
bool CommandLineParser::register_option(CommandLineOption option)
{
    const bool has_prefix = option.name.size() > OPTION_PREFIX.size()
        && std::string_view(option.name).substr(0, OPTION_PREFIX.size()) == OPTION_PREFIX;
    if (!has_prefix) {
        const std::string message = std::string("SYSERR: ") + __func__ + ": the option '"
            + option.name + "' does not start with " + std::string(OPTION_PREFIX)
            + " and a name, and cannot be registered.";
        log(message.c_str());
        return false;
    }

    const bool is_reserved = std::find(RESERVED_OPTION_NAMES.begin(),
                                 RESERVED_OPTION_NAMES.end(), std::string_view(option.name))
        != RESERVED_OPTION_NAMES.end();
    if (is_reserved) {
        const std::string message = std::string("SYSERR: ") + __func__ + ": the option '"
            + option.name + "' is read by ageland's own parser and cannot be registered.";
        log(message.c_str());
        return false;
    }

    const CommandLineOption* const registered = find(option.name);
    if (registered != nullptr) {
        const std::string message = std::string("SYSERR: ") + __func__ + ": the option '"
            + option.name + "' is already registered.";
        log(message.c_str());
        return false;
    }

    options.push_back(std::move(option));
    return true;
}

//============================================================================
bool CommandLineParser::parse(
    int argc, char** argv, CommandLineParseResult& out_result, std::string& out_error) const
{
    if (argc < 1 || argv == nullptr) {
        out_error = "no command line";
        return false;
    }

    // Built apart from out_result, so a refusal leaves it unchanged.
    CommandLineParseResult parsed;
    parsed.unclaimed_arguments.reserve(static_cast<std::size_t>(argc));
    parsed.unclaimed_arguments.push_back(argv[0]);
    for (int argument_index = 1; argument_index < argc; ++argument_index) {
        const std::string_view argument = argv[argument_index];
        const std::size_t equals_position = argument.find('=');
        if (equals_position != std::string_view::npos) {
            const CommandLineOption* const attached = find(argument.substr(0, equals_position));
            if (attached != nullptr) {
                out_error = attached->name + " takes its value as the next argument";
                return false;
            }
        }

        const CommandLineOption* const option = find(argument);
        if (option == nullptr) {
            parsed.unclaimed_arguments.push_back(argv[argument_index]);
            continue;
        }

        if (parsed.option_values.count(option->name) != 0) {
            out_error = option->name + " is given more than once";
            return false;
        }
        const bool has_value
            = argument_index + 1 < argc && find(argv[argument_index + 1]) == nullptr;
        if (!has_value) {
            out_error = option->name + " needs a value (" + option->value_name + ")";
            return false;
        }
        ++argument_index;
        const std::string_view value = argv[argument_index];
        if (option->validate) {
            std::optional<std::string> refusal = option->validate(value);
            if (refusal.has_value()) {
                out_error = std::move(*refusal);
                return false;
            }
        }
        parsed.option_values.emplace(option->name, std::string(value));
    }

    for (const CommandLineOption& option : options) {
        if (option.required && parsed.option_values.count(option.name) == 0) {
            out_error = option.name + " is required";
            return false;
        }
    }

    out_result = std::move(parsed);
    return true;
}

//============================================================================
std::string CommandLineParser::usage() const
{
    std::string text;
    for (const CommandLineOption& option : options) {
        text += "  " + option.name + " " + option.value_name + "  " + option.help;
        if (option.required) {
            text += " (required)";
        }
        text += "\n";
    }
    return text;
}

//============================================================================
const CommandLineOption* CommandLineParser::find(std::string_view name) const
{
    for (const CommandLineOption& option : options) {
        if (option.name == name) {
            return &option;
        }
    }
    return nullptr;
}

} // namespace harness
