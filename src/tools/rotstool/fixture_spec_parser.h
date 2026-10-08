#ifndef FIXTURE_SPEC_PARSER_H
#define FIXTURE_SPEC_PARSER_H

#include "fixture_spec.h"

#include <cstdint>
#include <filesystem>
#include <string>

// Largest fixture spec file read_fixture_spec_file() accepts, in bytes.
constexpr std::uintmax_t fixture_spec_byte_limit = 1024 * 1024;

// Reads a fixture spec file into out_contents. Refuses a missing or unreadable file and one over
// fixture_spec_byte_limit, with the reason in out_error_message.
[[nodiscard]] bool read_fixture_spec_file(
    const std::filesystem::path& path, std::string& out_contents, std::string& out_error_message);

// Checks fixture spec JSON and resolves its names into out_spec. Refuses, with the reason in
// out_error_message: an unknown or repeated key, a missing field,
// a race the creation menu does not offer, an unknown class or skill key, a split CreationPoints
// refuses, a level outside 1 to LEVEL_IMPL, practices outside 1 to 255, set values outside 1 to
// 32767, a name the account rules refuse or longer than MAX_NAME_LENGTH, two names that differ
// only in case, a password the policy refuses, or no characters.
[[nodiscard]] bool parse_fixture_spec(const std::string& json, FixtureSpec& out_spec, std::string& out_error_message);

#endif /* FIXTURE_SPEC_PARSER_H */
