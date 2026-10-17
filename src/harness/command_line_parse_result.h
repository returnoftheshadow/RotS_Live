/* command_line_parse_result.h */

#ifndef COMMAND_LINE_PARSE_RESULT_H
#define COMMAND_LINE_PARSE_RESULT_H

#include <functional>
#include <map>
#include <string>
#include <vector>

namespace harness {

// What a CommandLineParser takes from one command line.
struct CommandLineParseResult {
    // Each claimed option's value, by the option's name.
    std::map<std::string, std::string, std::less<>> option_values;
    // The program name, then every argument no option claimed, in their original order. The
    // pointers borrow the parsed argv, so they are valid only while it lives.
    std::vector<char*> unclaimed_arguments;
};

} // namespace harness

#endif /* COMMAND_LINE_PARSE_RESULT_H */
