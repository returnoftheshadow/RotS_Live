#include "fixtures_write_command.h"

#include "comm.h"
#include "fixture_lib_writer.h"
#include "fixture_spec.h"
#include "fixture_spec_parser.h"
#include "rotstool_command.h"
#include "spells.h"
#include "structs.h"
#include "utils.h"
#include "written_fixture_character.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <ostream>
#include <utility>

namespace rotstool {
namespace {

// The prefix of every message the command writes to err.
constexpr std::string_view message_prefix = "rotstool fixtures write: ";

// The options and spec path of one fixtures write run.
struct FixturesWriteOptions {
    // The lib to write into, from --lib.
    std::filesystem::path lib;
    // The seed from --random-seed; empty when the command draws its own.
    std::optional<unsigned int> seed;
    // Whether --verbose asked for a line per skill.
    bool verbose = false;
    // The spec file, the one argument that is not an option.
    std::filesystem::path spec_path;
};

// Splits an option argument such as "--lib=dir" into its name ("--lib") and, when it has an
// "=", the value after it.
std::pair<std::string_view, std::optional<std::string_view>> split_option(std::string_view argument)
{
    const std::size_t equals_position = argument.find('=');
    if (equals_position == std::string_view::npos) {
        return { argument, std::nullopt };
    }
    return { argument.substr(0, equals_position), argument.substr(equals_position + 1) };
}

// Reads the command's arguments into out_options. On bad usage returns false with the reason in
// out_error_message.
bool parse_fixtures_write_options(
    const std::vector<std::string>& arguments, FixturesWriteOptions& out_options, std::string& out_error_message)
{
    bool has_lib = false;
    bool has_spec = false;
    for (std::size_t argument_index = 0; argument_index < arguments.size(); ++argument_index) {
        const std::string_view argument = arguments[argument_index];
        if (argument.size() < 2 || argument.front() != '-') {
            if (has_spec) {
                out_error_message = "only one spec file may be given.";
                return false;
            }
            out_options.spec_path = std::filesystem::path(argument);
            has_spec = true;
            continue;
        }

        const std::pair<std::string_view, std::optional<std::string_view>> option = split_option(argument);
        const std::string_view option_name = option.first;
        if (option_name == "--verbose") {
            if (option.second.has_value()) {
                out_error_message = "--verbose takes no value.";
                return false;
            }
            out_options.verbose = true;
            continue;
        }
        if (option_name != "--lib" && option_name != "--random-seed") {
            out_error_message = "unknown option " + std::string(option_name) + ".";
            return false;
        }

        std::string_view value;
        if (option.second.has_value()) {
            value = *option.second;
        } else if (argument_index + 1 < arguments.size()) {
            ++argument_index;
            value = arguments[argument_index];
        } else {
            out_error_message = std::string(option_name) + " needs a value.";
            return false;
        }

        if (option_name == "--lib") {
            if (has_lib) {
                out_error_message = "--lib may be given only once.";
                return false;
            }
            if (value.empty()) {
                out_error_message = "--lib needs a directory.";
                return false;
            }
            out_options.lib = std::filesystem::path(value);
            has_lib = true;
            continue;
        }

        if (out_options.seed.has_value()) {
            out_error_message = "--random-seed may be given only once.";
            return false;
        }
        unsigned int seed = 0;
        std::string seed_error;
        if (!parse_random_seed_value(value, seed, &seed_error)) {
            out_error_message = seed_error;
            return false;
        }
        out_options.seed = seed;
    }

    if (!has_lib) {
        out_error_message = "--lib is required.";
        return false;
    }
    if (!has_spec) {
        out_error_message = "a spec file is required.";
        return false;
    }
    return true;
}

// Returns the current time as whole seconds since the epoch.
long current_time_in_seconds()
{
    const std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
    const std::chrono::seconds since_epoch
        = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch());
    return static_cast<long>(since_epoch.count());
}

// Prints one line per written character and, when verbose, a line per skill after it with the
// practices the spec gave and the knowledge they produced.
void print_written_characters(const FixtureSpec& spec, const std::vector<WrittenFixtureCharacter>& written,
    bool verbose, std::ostream& out)
{
    const skill_data* const skills = get_skill_array();
    for (std::size_t character_index = 0; character_index < written.size(); ++character_index) {
        const WrittenFixtureCharacter& character = written[character_index];
        out << character.name << '\t' << character.idnum << '\t' << character.character_file.string() << '\n';
        if (!verbose) {
            continue;
        }

        // The writer reports skills in spec order, so each entry pairs with the spec's practices.
        const std::vector<std::pair<int, int>>& practices = spec.characters[character_index].skill_practices;
        const std::size_t skill_count = std::min(practices.size(), character.skill_knowledge.size());
        for (std::size_t skill_index = 0; skill_index < skill_count; ++skill_index) {
            const std::pair<int, int>& knowledge = character.skill_knowledge[skill_index];
            out << character.name << '\t' << skills[knowledge.first].name << '\t'
                << practices[skill_index].second << '\t' << knowledge.second << '\n';
        }
    }
}

} // namespace

int run_fixtures_write(const std::vector<std::string>& arguments, std::ostream& out, std::ostream& err)
{
    FixturesWriteOptions options;
    std::string error_message;
    if (!parse_fixtures_write_options(arguments, options, error_message)) {
        err << message_prefix << error_message << "\n" << fixtures_write_usage << "\n";
        return exit_usage;
    }

    std::string spec_text;
    if (!read_fixture_spec_file(options.spec_path, spec_text, error_message)) {
        err << message_prefix << error_message << "\n";
        return exit_failure;
    }
    FixtureSpec spec;
    if (!parse_fixture_spec(spec_text, spec, error_message)) {
        err << message_prefix << error_message << "\n";
        return exit_failure;
    }

    seed_random_numbers(options.seed, draw_clock_seed);
    const long now = current_time_in_seconds();
    std::vector<WrittenFixtureCharacter> written;
    if (!write_fixtures_to_lib(spec, options.lib, now, written, error_message)) {
        err << message_prefix << error_message << "\n";
        return exit_failure;
    }

    print_written_characters(spec, written, options.verbose, out);
    return exit_success;
}

} // namespace rotstool
