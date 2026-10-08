#include "fixture_character_maker.h"

#include "db.h"
#include "handler.h"
#include "limits.h"
#include "spells.h"
#include "utils.h"

#include <cstdio>
#include <cstring>
#include <memory>
#include <optional>

extern int top_of_p_table;
extern long top_idnum;

namespace {

// Sets a global for one scope and puts its previous value back when the scope ends.
template <typename Value>
class ScopedGlobalValue {
public:
    ScopedGlobalValue(Value& global, Value scoped_value)
        : global(global)
        , saved_value(global)
    {
        global = scoped_value;
    }

    ~ScopedGlobalValue()
    {
        global = saved_value;
    }

    ScopedGlobalValue(const ScopedGlobalValue&) = delete;
    ScopedGlobalValue& operator=(const ScopedGlobalValue&) = delete;

private:
    // The global this object changed.
    Value& global;
    // The global's value before this object changed it.
    Value saved_value;
};

// Frees a character allocated with CREATE and cleared with clear_char.
struct CharacterReleaser {
    void operator()(char_data* character) const
    {
        character->desc = nullptr;
        free_char(character);
    }
};

using OwnedCharacter = std::unique_ptr<char_data, CharacterReleaser>;

// Readies a connection that the game treats as one in the creation dialogue for account_name.
void prepare_stand_in_descriptor(descriptor_data& descriptor, const std::string& account_name)
{
    descriptor.output = descriptor.small_outbuf;
    descriptor.small_outbuf[0] = '\0';
    descriptor.bufptr = 0;
    descriptor.bufspace = SMALL_BUFSIZE - 1;
    descriptor.pos = -1;
    descriptor.connected = CON_CREATE;
    std::snprintf(descriptor.account_name, sizeof(descriptor.account_name), "%s", account_name.c_str());
}

// Returns the refusal for a value the spec sets that the character cannot hold.
std::string set_value_refusal(const char* character_name, const char* field_name, int requested, int maximum)
{
    if (requested < 0) {
        return std::string(character_name) + "'s " + field_name + " " + std::to_string(requested) + " is below zero.";
    }
    return std::string(character_name) + "'s " + field_name + " " + std::to_string(requested)
        + " is above its maximum of " + std::to_string(maximum) + ".";
}

// Sets current to the value the spec sets, if any. Returns false, with the reason in
// out_error_message, when that value is below zero or above maximum.
template <typename Points>
bool set_current_value(const char* character_name, const char* field_name, const std::optional<int>& requested,
    int maximum, Points& current, std::string& out_error_message)
{
    if (!requested) {
        return true;
    }
    // Bounding the value by 0 and a maximum of the field's own type keeps the cast exact.
    if (*requested < 0 || *requested > maximum) {
        out_error_message = set_value_refusal(character_name, field_name, *requested, maximum);
        return false;
    }
    current = static_cast<Points>(*requested);
    return true;
}

// Returns false, with the reason in out_error_message, when the spec sets a value and the stored
// character holds a different one.
bool stored_value_matches(const char* character_name, const char* field_name, const std::optional<int>& requested,
    int maximum, int stored_value, std::string& out_error_message)
{
    if (!requested || *requested == stored_value) {
        return true;
    }
    out_error_message = set_value_refusal(character_name, field_name, *requested, maximum);
    return false;
}

} // namespace

bool make_fixture_character(const FixtureSpec::Character& spec, const std::string& account_name, long idnum,
    long now, char_file_u& out_stored_character, std::vector<int>& out_knowledge, std::string& out_error_message)
{
    if (!spec.creation_points) {
        out_error_message = "The character has no creation points.";
        return false;
    }

    // An empty player table (-1) keeps do_start from treating this as the first player ever and
    // making it an implementor, which it does when the table holds exactly one entry.
    const ScopedGlobalValue<int> player_table_top(top_of_p_table, -1);
    // init_char takes the next idnum from top_idnum; the character gets idnum instead.
    const ScopedGlobalValue<long> highest_idnum(top_idnum, top_idnum);

    char_data* allocated_character = nullptr;
    CREATE(allocated_character, struct char_data, 1);
    OwnedCharacter character(allocated_character);
    clear_char(character.get(), MOB_VOID);

    CREATE(character->player.name, char, spec.name.size() + 1);
    std::strcpy(character->player.name, spec.name.c_str());
    CAP(character->player.name);
    GET_RACE(character.get()) = spec.race;
    GET_SEX(character.get()) = spec.sex;

    // While the character is attached to a connection in the creation dialogue, advance_level
    // neither records exploits nor saves (the game defers both until a new character is
    // introduced), and write_exploits drops the stat-gain records it is still sent, because the
    // account in the working directory does not list this character yet. The stand-in stays out
    // of descriptor_list, and the game sends output only to playing connections.
    descriptor_data stand_in {};
    prepare_stand_in_descriptor(stand_in, account_name);
    stand_in.character = character.get();
    character->desc = &stand_in;

    spec.creation_points->apply_to(*character);
    init_char(character.get());
    character->specials2.idnum = idnum;
    // The two start-of-life steps of finalize_new_character_start_state(), without its load-room
    // lookup, which needs a booted world.
    do_start(character.get());
    affect_total(character.get());

    const int experience_needed = xp_to_level(spec.level) - GET_EXP(character.get());
    if (experience_needed > 0) {
        gain_exp_regardless(character.get(), experience_needed);
    }
    if (GET_LEVEL(character.get()) != spec.level) {
        out_error_message = spec.name + " reached level " + std::to_string(GET_LEVEL(character.get()))
            + ", not " + std::to_string(spec.level) + ".";
        return false;
    }

    for (const std::pair<int, int> skill_practice : spec.skill_practices) {
        character->skills[skill_practice.first] = static_cast<byte>(skill_practice.second);
    }
    recalc_skills(character.get());
    out_knowledge.clear();
    out_knowledge.reserve(spec.skill_practices.size());
    for (const std::pair<int, int> skill_practice : spec.skill_practices) {
        out_knowledge.push_back(character->knowledge[skill_practice.first]);
    }
    // The maximums depend on knowledge (stealth lowers hit points, travelling raises movement).
    affect_total(character.get());

    const char* const character_name = GET_NAME(character.get());
    if (!set_current_value(character_name, "hit", spec.hit, GET_MAX_HIT(character.get()),
            character->tmpabilities.hit, out_error_message)
        || !set_current_value(character_name, "mana", spec.mana, GET_MAX_MANA(character.get()),
            character->tmpabilities.mana, out_error_message)
        || !set_current_value(character_name, "move", spec.move, GET_MAX_MOVE(character.get()),
            character->tmpabilities.move, out_error_message)) {
        return false;
    }

    character->specials2.load_room = spec.load_room;
    character->desc = nullptr;
    stand_in.character = nullptr;

    out_stored_character = char_file_u {};
    char_to_store(character.get(), &out_stored_character);
    out_stored_character.specials2.load_room = spec.load_room;
    out_stored_character.last_logon = now;

    // char_to_store recomputes the maximums and clamps the current values to them.
    return stored_value_matches(character_name, "hit", spec.hit, GET_MAX_HIT(character.get()),
               out_stored_character.tmpabilities.hit, out_error_message)
        && stored_value_matches(character_name, "mana", spec.mana, GET_MAX_MANA(character.get()),
            out_stored_character.tmpabilities.mana, out_error_message)
        && stored_value_matches(character_name, "move", spec.move, GET_MAX_MOVE(character.get()),
            out_stored_character.tmpabilities.move, out_error_message);
}
