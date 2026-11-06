/* test_command_line.h */

#ifndef TEST_COMMAND_LINE_H
#define TEST_COMMAND_LINE_H

#include <string>
#include <utility>
#include <vector>

namespace harness_tests {

// Owns a command line's words and gives them out as the argv a main() receives.
class CommandLine {
public:
    // Takes the words of a command line, the program name first.
    explicit CommandLine(std::vector<std::string> arguments)
        : words(std::move(arguments))
    {
        pointers.reserve(words.size());
        for (std::string& word : words) {
            pointers.push_back(word.data());
        }
    }

    // Holds pointers into its own words, so it cannot be copied or moved.
    CommandLine(const CommandLine&) = delete;
    CommandLine& operator=(const CommandLine&) = delete;

    // The argument count.
    [[nodiscard]] int count() const
    {
        // pointers holds one entry per word of a test's short command line, so it fits an int.
        return static_cast<int>(pointers.size());
    }

    // The argument vector; valid while the command line lives.
    [[nodiscard]] char** vector()
    {
        return pointers.data();
    }

private:
    // The words, owning their characters.
    std::vector<std::string> words;
    // Pointers into words, as argv.
    std::vector<char*> pointers;
};

} // namespace harness_tests

#endif /* TEST_COMMAND_LINE_H */
