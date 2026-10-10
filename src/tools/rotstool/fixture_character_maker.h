#ifndef FIXTURE_CHARACTER_MAKER_H
#define FIXTURE_CHARACTER_MAKER_H

#include "fixture_spec.h"
#include "structs.h"

#include <string>
#include <vector>

// Makes one character the way the game makes and levels a new character and returns it as a
// stored character, with idnum as its id and now as its last logon. The stored name is spec.name
// with its first letter capitalised. out_knowledge receives the knowledge percentage for each of
// spec.skill_practices, in order. The spec's hit, mana and move become the stored character's
// current values.
//
// Provided the working directory is the lib and the account named account_name is on disk there
// without this character, writes nothing to disk. Leaves top_of_p_table and
// top_idnum as it found them. Returns false with the reason in out_error_message when the
// character does not reach spec.level, or a value the spec sets is below zero or above the
// character's maximum.
[[nodiscard]] bool make_fixture_character(const FixtureSpec::Character& spec, const std::string& account_name,
    long idnum, long now, char_file_u& out_stored_character, std::vector<int>& out_knowledge,
    std::string& out_error_message);

#endif /* FIXTURE_CHARACTER_MAKER_H */
