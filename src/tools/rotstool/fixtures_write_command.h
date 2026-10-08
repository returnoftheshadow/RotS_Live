#ifndef FIXTURES_WRITE_COMMAND_H
#define FIXTURES_WRITE_COMMAND_H

#include <iosfwd>
#include <string>
#include <string_view>
#include <vector>

namespace rotstool {

// The usage text of rotstool fixtures write, printed by help and after bad usage.
constexpr std::string_view fixtures_write_usage
    = "usage: rotstool fixtures write --lib <dir> [--random-seed <n>] [--verbose] <spec.json>\n"
      "\n"
      "Writes a verified account and the characters a fixture spec describes into <dir>, made and\n"
      "levelled by the game's own code. With --random-seed the same spec gives the same characters.\n"
      "Prints one line per character: name, idnum and character file. --verbose adds a line per skill\n"
      "with its practices and knowledge.";

// Runs rotstool fixtures write: reads the spec, seeds the game's random numbers, writes the lib and
// prints one line per character; exit codes as in rotstool_command.h.
int run_fixtures_write(const std::vector<std::string>& arguments, std::ostream& out, std::ostream& err);

} // namespace rotstool

#endif /* FIXTURES_WRITE_COMMAND_H */
