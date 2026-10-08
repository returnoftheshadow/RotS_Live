#ifndef FIXTURE_LIB_WRITER_H
#define FIXTURE_LIB_WRITER_H

#include "fixture_spec.h"
#include "written_fixture_character.h"

#include <filesystem>
#include <string>
#include <vector>

// Idnum of the first fixture character in a lib that has none above it.
constexpr long FIRST_FIXTURE_IDNUM = 9000001;

// Writes spec's account and characters into lib_directory: the account, verified, linking every
// character, and for each character its character, objects and exploits files, all through the
// server's writers and each read back afterwards. now is used for the account's timestamps and
// each character's last logon. Refuses, before writing anything, a lib that is not a directory,
// an account that already exists and a character name already in the lib in any case. On a
// failure after writing has begun it removes the account's directory. On success out_written
// lists the characters in spec order. Changes the working directory while it runs and restores it.
[[nodiscard]] bool write_fixtures_to_lib(const FixtureSpec& spec, const std::filesystem::path& lib_directory,
    long now, std::vector<WrittenFixtureCharacter>& out_written, std::string& out_error_message);

#endif /* FIXTURE_LIB_WRITER_H */
