#ifndef FIXTURE_SPEC_H
#define FIXTURE_SPEC_H

#include "creation_points.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

// The content of a fixture spec after parse_fixture_spec() has checked it, with every name
// resolved to the game's numbers.
struct FixtureSpec {
    // One character the spec asks for.
    struct Character {
        // The name as the player would type it; the maker capitalises the first letter.
        std::string name;
        // RACE_* number of a race the creation menu offers.
        int race = 0;
        // SEX_MALE or SEX_FEMALE.
        int sex = 0;
        // The creation points; always set on a parsed spec.
        std::optional<CreationPoints> creation_points;
        // The level to reach, 1 to LEVEL_IMPL.
        int level = 0;
        // Virtual number of the room the character enters the game in.
        int load_room = 0;
        // Practice sessions per skill, as (skill index, practices) pairs in spec order.
        std::vector<std::pair<int, int>> skill_practices;
        // Current hit points after levelling, when the spec sets them; at most the maximum.
        std::optional<int> hit;
        // Current mana after levelling, when the spec sets them; at most the maximum.
        std::optional<int> mana;
        // Current movement after levelling, when the spec sets them; at most the maximum.
        std::optional<int> move;
    };

    // The account's email address, which also names the account's directory.
    std::string email;
    // The account's password; meets the server's password policy.
    std::string password;
    // The characters to make, in spec order, with names unique regardless of case.
    std::vector<Character> characters;
};

#endif /* FIXTURE_SPEC_H */
