#ifndef ROTSTOOL_COMMAND_H
#define ROTSTOOL_COMMAND_H

#include <iosfwd>
#include <string>
#include <string_view>
#include <vector>

namespace rotstool {

// Exit code of a command that did what it was asked.
constexpr int EXIT_CODE_SUCCESS = 0;

// Exit code of a command that ran and failed; nothing usable was left behind.
constexpr int EXIT_CODE_FAILURE = 1;

// Exit code for arguments a command does not accept.
constexpr int EXIT_CODE_USAGE = 2;

// Runs a command with the arguments that follow its name, writing results to out and errors to
// err, and returns its exit code.
using CommandRunner = int (*)(const std::vector<std::string>& arguments, std::ostream& out, std::ostream& err);

// One rotstool command: the words that select it, what it does and how to run it.
struct RotstoolCommand {
    // The words that select the command, separated by single spaces, such as "fixtures write".
    std::string_view name;
    // One line saying what the command does, for the command list.
    std::string_view summary;
    // The command's usage text, printed by help and after bad usage.
    std::string_view usage;
    // Runs the command.
    CommandRunner run;
};

// Returns every command, in the order help lists them.
const std::vector<RotstoolCommand>& rotstool_commands();

// Runs rotstool with the arguments that follow the program name and returns the exit code. With
// no arguments or an unknown command it lists the commands on err and returns EXIT_CODE_USAGE.
int run_rotstool(const std::vector<std::string>& arguments, std::ostream& out, std::ostream& err);

} // namespace rotstool

#endif /* ROTSTOOL_COMMAND_H */
