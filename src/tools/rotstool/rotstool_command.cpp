#include "rotstool_command.h"

#include "fixtures_write_command.h"

#include <cstddef>
#include <ostream>

namespace rotstool {
namespace {

int run_help(const std::vector<std::string>& arguments, std::ostream& out, std::ostream& err);

// Splits a command name into its words.
std::vector<std::string_view> split_words(std::string_view name)
{
    std::vector<std::string_view> words;
    while (!name.empty()) {
        const std::size_t space_position = name.find(' ');
        if (space_position == std::string_view::npos) {
            words.push_back(name);
            break;
        }
        words.push_back(name.substr(0, space_position));
        name.remove_prefix(space_position + 1);
    }
    return words;
}

// Returns the command whose words begin arguments, or nullptr when none does. On a match,
// out_word_count is the number of words its name used.
const RotstoolCommand* find_command(const std::vector<std::string>& arguments, std::size_t& out_word_count)
{
    const std::vector<RotstoolCommand>& commands = rotstool_commands();
    for (const RotstoolCommand& command : commands) {
        const std::vector<std::string_view> words = split_words(command.name);
        if (words.size() > arguments.size()) {
            continue;
        }
        bool matches = true;
        for (std::size_t word_index = 0; word_index < words.size(); ++word_index) {
            if (arguments[word_index] != words[word_index]) {
                matches = false;
                break;
            }
        }
        if (matches) {
            out_word_count = words.size();
            return &command;
        }
    }
    return nullptr;
}

void list_commands(std::ostream& stream)
{
    stream << "usage: rotstool <command> [options]\n\ncommands:\n";
    const std::vector<RotstoolCommand>& commands = rotstool_commands();
    for (const RotstoolCommand& command : commands) {
        stream << "  " << command.name << "  " << command.summary << "\n";
    }
}

int run_help(const std::vector<std::string>& arguments, std::ostream& out, std::ostream& err)
{
    if (arguments.empty()) {
        list_commands(out);
        return EXIT_CODE_SUCCESS;
    }

    std::size_t word_count = 0;
    const RotstoolCommand* command = find_command(arguments, word_count);
    if (command == nullptr || word_count != arguments.size()) {
        err << "rotstool help: no command named '" << arguments.front() << "'.\n";
        list_commands(err);
        return EXIT_CODE_USAGE;
    }

    out << command->usage << "\n";
    return EXIT_CODE_SUCCESS;
}

} // namespace

const std::vector<RotstoolCommand>& rotstool_commands()
{
    static const std::vector<RotstoolCommand> commands = {
        { "help", "Lists the commands, or prints one command's usage.", "usage: rotstool help [command]", run_help },
        { "fixtures write", "Writes a test account and characters into a lib from a fixture spec.",
            FIXTURES_WRITE_USAGE, run_fixtures_write },
    };
    return commands;
}

int run_rotstool(const std::vector<std::string>& arguments, std::ostream& out, std::ostream& err)
{
    if (arguments.empty()) {
        list_commands(err);
        return EXIT_CODE_USAGE;
    }

    std::size_t word_count = 0;
    const RotstoolCommand* command = find_command(arguments, word_count);
    if (command == nullptr) {
        err << "rotstool: unknown command '" << arguments.front() << "'.\n";
        list_commands(err);
        return EXIT_CODE_USAGE;
    }

    const std::vector<std::string> command_arguments(arguments.begin() + word_count, arguments.end());
    return command->run(command_arguments, out, err);
}

} // namespace rotstool
