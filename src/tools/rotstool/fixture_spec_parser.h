#ifndef FIXTURE_SPEC_PARSER_H
#define FIXTURE_SPEC_PARSER_H

#include "fixture_spec.h"

#include <cstdint>
#include <filesystem>
#include <string>

// Largest fixture spec file read_fixture_spec_file() accepts, in bytes.
constexpr std::uintmax_t FIXTURE_SPEC_BYTE_LIMIT = 1024 * 1024;

// Fewest coins a stored gold value can be; wizset gold's bottom.
constexpr int MIN_GOLD = 0;

// Most coins a character can carry; wizset gold's top.
constexpr int MAX_GOLD = 100000000;

// The size of the alignment range either side of zero; wizset alignment's limit.
constexpr int MAX_ALIGNMENT = 1000;

// The condition value that switches a condition off, and the lowest a condition can be; wizset's
// "off".
constexpr int CONDITION_OFF = -1;

// Most a hunger, thirst or drunkenness condition can be; wizset's top for each.
constexpr int MAX_CONDITION = 24;

// Least a base ability can be; wizset's bottom for the spec keys str, int, wil, dex, con and lea.
constexpr int MIN_BASE_ABILITY = 0;

// Most a base ability can be; wizset's top for the spec keys str, int, wil, dex, con and lea.
constexpr int MAX_BASE_ABILITY = 40;

// Least base stamina (maximum mana) can be; wizset maxstamina's bottom.
constexpr int MIN_BASE_STAMINA = 1;

// Most base stamina (maximum mana) can be; wizset maxstamina's top.
constexpr int MAX_BASE_STAMINA = 5000;

// Reads a fixture spec file into out_contents. Refuses a missing or unreadable file, a path that
// names a directory or other non-file, and a file over FIXTURE_SPEC_BYTE_LIMIT, with the reason in
// out_error_message.
[[nodiscard]] bool read_fixture_spec_file(
    const std::filesystem::path& path, std::string& out_contents, std::string& out_error_message);

// Checks fixture spec JSON and resolves its names into out_spec. Refuses, with the reason in
// out_error_message: an unknown or repeated key, a missing field,
// a race the creation menu does not offer, an unknown class or skill key, a split CreationPoints
// refuses, a level outside 1 to LEVEL_IMPL, practices outside 1 to 255, a set hit, mana or move
// outside 1 to 32767, a stored value outside its limit above, a name that is not MIN_NAME_LENGTH to MAX_NAME_LENGTH (3 to 12) letters, two names that
// differ only in case, an email or password the account rules refuse, or no characters.
[[nodiscard]] bool parse_fixture_spec(const std::string& json, FixtureSpec& out_spec, std::string& out_error_message);

#endif /* FIXTURE_SPEC_PARSER_H */
