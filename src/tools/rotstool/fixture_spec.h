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
    // Values a character is stored with, each named and limited like the wizset field that
    // sets the same value in a running game.
    struct StoredValues {
        // Coins carried, 0 to 100000000; wizset gold.
        std::optional<int> gold;
        // Spirit points, any int; wizset spirit.
        std::optional<int> spirit;
        // Alignment, -1000 to 1000; wizset alignment.
        std::optional<int> alignment;
        // PLRSPEC_* number resolved from the specialization's name; wizset specialization.
        std::optional<int> specialization;
        // Hunger, thirst and drunkenness conditions, 0 to 24 or -1 for off; wizset hunger,
        // thirst and drunk.
        std::optional<int> hunger;
        std::optional<int> thirst;
        std::optional<int> drunk;
        // Base abilities, 0 to 40, under the spec keys str, int, wil, dex, con and lea; wizset str,
        // int, will, dex, con and lea.
        std::optional<int> strength;
        std::optional<int> intelligence;
        std::optional<int> will;
        std::optional<int> dexterity;
        std::optional<int> constitution;
        std::optional<int> learning;
        // Base stamina (maximum mana), 1 to 5000; wizset maxstamina.
        std::optional<int> max_stamina;
    };

    // One character the spec asks for.
    struct Character {
        // The name as the player would type it.
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
        // Current hit points after levelling, 1 to 32767, when the spec sets it; make_fixture_character
        // refuses a value above the levelled maximum.
        std::optional<int> hit;
        // Current mana after levelling, 1 to 32767, when the spec sets it; make_fixture_character
        // refuses a value above the levelled maximum.
        std::optional<int> mana;
        // Current movement after levelling, 1 to 32767, when the spec sets it; make_fixture_character
        // refuses a value above the levelled maximum.
        std::optional<int> move;
        // The stored values the spec sets beside hit, mana and move.
        StoredValues stored;
    };

    // The account's email address, which also names the account's directory.
    std::string email;
    // The account's password; meets the server's password policy.
    std::string password;
    // The characters to make, in spec order, with names unique regardless of case.
    std::vector<Character> characters;
};

#endif /* FIXTURE_SPEC_H */
