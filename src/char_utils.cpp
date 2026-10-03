
#include "char_utils.h"
#include "environment_utils.h"
#include "object_utils.h"
#include "spells.h"

#include "db.h" // for get_encumb_table
#include "handler.h" // for fname and other_side

#include "structs.h"
#include "utils.h"
#include "warrior_spec_handlers.h"
#include <algorithm>
#include <assert.h>
#include <cmath>

#include "comm.h" // for send_to_char
#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

struct race_bodypart_data;

// TODO(dgurley):  Move these tables elsewhere or provide accessors or something.
extern sh_int square_root[];
// extern race_bodypart_data bodyparts[MAX_BODYTYPES]; // Due to where this is located, this currently isn't possible to support here.

namespace utils {
//============================================================================
bool is_pc(const char_data& character)
{
    return !is_npc(character);
}

//============================================================================
bool is_npc(const char_data& character)
{
    return utils::is_set(character.specials2.act, (long)MOB_ISNPC);
}

//============================================================================
bool is_mob(const char_data& character)
{
    return is_npc(character) && character.nr >= 0;
}

//============================================================================
bool is_retired(const char_data& character)
{
    return utils::is_set(character.specials2.act, (long)PLR_RETIRED);
}

//============================================================================
bool is_mob_flagged(const char_data& mob, long flag)
{
    return is_npc(mob) && utils::is_set(mob.specials2.act, flag);
}

//============================================================================
bool is_player_flagged(const char_data& character, long flag)
{
    return !is_npc(character) && utils::is_set(character.specials2.act, flag);
}

//============================================================================
bool is_preference_flagged(const char_data& character, long flag)
{
    return utils::is_set(character.specials2.pref, flag);
}

//============================================================================
// Apparently this isn't used.
//============================================================================
bool is_player_mode_on(const char_data& character, long flag)
{
    return false;
    // there is no character.specials2.mode
    // return base_utils::is_set(character.specials2.mode, flag);
}

//============================================================================
bool is_affected_by(const char_data& character, long skill_id)
{
    return utils::is_set(character.specials.affected_by, skill_id);
}

const bool is_fighting(const char_data& character)
{
    return character.specials.fighting;
}

void add_spirits(char_data* character, int spirits)
{
    if ((character->points.spirit + spirits) <= MAX_SPIRITS) {
        character->points.spirit += spirits;
    }
}

void set_spirits(char_data* character, int spirits)
{
    if (spirits <= MAX_SPIRITS) {
        character->points.spirit = spirits;
    }
}

int get_spirits(char_data* character)
{
    return character->points.spirit;
}

int get_current_moves(char_data* character)
{
    return character->tmpabilities.move;
}

int get_max_moves(char_data* character)
{
    return character->abilities.move;
}

int get_current_hit(const char_data& character)
{
    return character.tmpabilities.hit;
}

int get_max_hit(const char_data& character)
{
    return character.abilities.hit;
}

//============================================================================
affected_type* is_affected_by_spell(char_data& character, int skill_id)
{
    int count = 0;
    for (affected_type* affect = character.affected; affect && count < MAX_AFFECT; affect = affect->next) {
        if (affect->type == skill_id) {
            return affect;
        }
        ++count;
    }

    return nullptr;
}

//============================================================================
const char* his_or_her(const char_data& character)
{
    switch (character.player.sex) {
    case 0:
        return "its";
        break;
    case 1:
        return "his";
        break;
    case 2:
        return "her";
        break;
    default:
        return "its";
        break;
    }
}

//============================================================================
const char* he_or_she(const char_data& character)
{
    switch (character.player.sex) {
    case 0:
        return "it";
        break;
    case 1:
        return "he";
        break;
    case 2:
        return "she";
        break;
    default:
        return "it";
        break;
    }
}

//============================================================================
const char* him_or_her(const char_data& character)
{
    switch (character.player.sex) {
    case 0:
        return "it";
        break;
    case 1:
        return "him";
        break;
    case 2:
        return "her";
        break;
    default:
        return "it";
        break;
    }
}

//============================================================================
int get_tactics(const char_data& character)
{
    if (is_npc(character))
        return 0;

    return character.specials.tactics;
}

//============================================================================
void set_tactics(char_data& character, int value)
{
    if (value <= 0)
        value = TACTICS_NORMAL;

    if (!is_npc(character)) {
        character.specials.tactics = static_cast<ubyte>(value);
    }
}

//============================================================================
int get_shooting(const char_data& character)
{
    if (is_npc(character))
        return 0;

    return character.specials.shooting;
}

//============================================================================
void set_shooting(char_data& character, int value)
{
    if (value <= 0)
        value = SHOOTING_NORMAL;

    if (!is_npc(character))
        character.specials.shooting = static_cast<ubyte>(value);
}

//============================================================================
int get_casting(const char_data& character)
{
    if (is_npc(character))
        return 0;

    return character.specials.casting;
}

//============================================================================
void set_casting(char_data& character, int value)
{
    if (value <= 0)
        value = CASTING_NORMAL;

    if (is_pc(character)) {
        character.specials.casting = static_cast<ubyte>(value);
    }
}

//============================================================================
int get_condition(const char_data& character, int index)
{
    if (index > 2 || index < 0)
        return 0;

    return character.specials2.conditions[index];
}

//============================================================================
void set_condition(char_data& character, int index, sh_int value)
{
    if (index >= 0 && index <= 2) {
        character.specials2.conditions[index] = value;
    }
}

//============================================================================
int get_index(const char_data& character)
{
    if (is_npc(character))
        return -1;

    return character.player_index;
}

//============================================================================
const char* get_name(const char_data& character)
{
    if (is_npc(character))
        return character.player.short_descr;

    return character.player.name;
}

//============================================================================
const char* get_skill_name(const int skill_id)
{
    const skill_data* skills = get_skill_array();
    const char* skill_name = skills[skill_id].name;
    return skill_name;
}

//============================================================================
int get_level_a(const char_data& character)
{
    if (is_npc(character))
        return character.player.level;

    return std::min(character.player.level, LEVEL_MAX);
}

//============================================================================
int get_level_legend_cap(const char_data& character)
{
    return get_level_a(character);
}

//============================================================================
int get_level_b(const char_data& character)
{
    if (is_npc(character))
        return character.player.level;

    return std::min(character.player.level, character.player.level / 3 + (LEVEL_MAX * 2 / 3));
}

//============================================================================
int get_prof_level(int prof, const char_data& character)
{
    if (prof == PROF_GENERAL || is_npc(character) || prof > MAX_PROFS)
        return character.player.level;

    return character.profs->prof_level[prof];
}

//============================================================================
int get_max_race_prof_level(int prof, const char_data& character)
{
    int max_prof_level = 30;

    if (character.player.race == RACE_ORC) {
        max_prof_level = 20;
    } else if (character.player.race == RACE_URUK && prof == PROF_MAGE) {
        max_prof_level = 27;
    }

    return max_prof_level;
}

//============================================================================
void set_prof_level(int prof, char_data& character, sh_int value)
{
    if (prof == PROF_GENERAL || prof > MAX_PROFS || is_npc(character))
        return;

    character.profs->prof_level[prof] = value;
}

//============================================================================
int get_prof_coof(int prof, const char_data& character)
{
    if (prof > MAX_PROFS)
        return 0; // should add assert functionality for this

    if (prof == PROF_GENERAL)
        return 1000;

    int prof_coof = character.profs->prof_coof[prof];
    // TODO(dgurley):  square_root is an externed array; fix this crap.
    int return_prof_coof = square_root[prof_coof];

    if (character.player.race == RACE_ORC) {
        return_prof_coof = (return_prof_coof * 2 + 2) / 3;
    } else if (character.player.race == RACE_URUK && prof == PROF_MAGE) {
        return_prof_coof -= 100;
    }

    return return_prof_coof;
}

//============================================================================
int get_prof_points(int prof, const char_data& character)
{
    // Added a safety check.
    if (prof > MAX_PROFS)
        return 0;

    // Add is_npc check like above?  Not in the current macro, so I won't be adding new functionality.
    return character.profs->prof_coof[prof];
}

//============================================================================
int get_highest_coeffs(const char_data& character)
{
    int coeffs[4];
    int prof = 0;
    coeffs[0] = get_prof_level(PROF_MAGE, character);
    coeffs[1] = get_prof_level(PROF_CLERIC, character);
    coeffs[2] = get_prof_level(PROF_RANGER, character);
    coeffs[3] = get_prof_level(PROF_WARRIOR, character);

    for (int i = 1; i < 4; ++i) {
        if (coeffs[0] < coeffs[i]) {
            coeffs[0] = coeffs[i];
            prof = i;
        }
    }

    return prof + 1;
}

//============================================================================
int get_bal_strength(const char_data& character)
{
    // dgurley:  I agree with the intent behind this function, but not it's implementation.
    // I don't think that there should be a racial normalization factor.  However, we could
    // treat all high strength scores with diminishing returns.  This function wouldn't be
    // the correct place to do that though - it would be in whatever function is using strength.

    static const int max_race_str[] = {
        22, // IMM
        22, // HUMAN
        22, // DWARF
        22, // WOOD
        20, // HOBBIT
        22, // HIGH ELF
        22,
        22,
        22,
        22,
        22,
        22, // URUK
        22, // HARAD
        22, // COMMON ORC
        22, // EASTERLING
        22, // LHUTH
        22,
        22,
        22,
        22,
        22, // TROLL
        22
    };

    // If the character's strength is within normal bounds for their race, just return it.
    int race = character.player.race;
    if (race > 21)
        return character.tmpabilities.str;

    int race_strength_cap = max_race_str[race];
    if (character.tmpabilities.str <= race_strength_cap)
        return character.tmpabilities.str;

    // Otherwise, only even points of strength count.
    int bounded_strength = race_strength_cap + ((character.tmpabilities.str - race_strength_cap) / 2);
    return bounded_strength;
}

//============================================================================
double get_bal_strength_d(const char_data& character)
{
    // dgurley:  I agree with the intent behind this function, but not it's implementation.
    // I don't think that there should be a racial normalization factor.  However, we could
    // treat all high strength scores with diminishing returns.  This function wouldn't be
    // the correct place to do that though - it would be in whatever function is using strength.

    static const double max_race_str[] = {
        22, // IMM
        22, // HUMAN
        22, // DWARF
        22, // WOOD
        20, // HOBBIT
        22, // HIGH ELF
        22,
        22,
        22,
        22,
        22,
        22, // URUK
        22, // HARAD
        22, // COMMON ORC
        22, // EASTERLING
        22, // LHUTH
        22,
        22,
        22,
        22,
        22, // TROLL
        22
    };

    // If the character's strength is within normal bounds for their race, just return it.
    int race = character.player.race;
    if (race > 21)
        return character.tmpabilities.str;

    double race_strength_cap = max_race_str[race];
    if (character.tmpabilities.str <= race_strength_cap)
        return character.tmpabilities.str;

    // Otherwise, only even points of strength count.
    double bounded_strength = race_strength_cap + ((character.tmpabilities.str - race_strength_cap) * 0.5);
    return bounded_strength;
}

//============================================================================
bool is_evil_race(const char_data& character)
{
    int race = character.player.race;
    return race == RACE_URUK || race == RACE_ORC || race == RACE_MAGUS || race == RACE_OLOGHAI || race == RACE_HARADRIM;
}

//============================================================================
// Internal helper namespace for constants, etc.
//============================================================================
namespace {
    /* Encumbrance values used for light fighting.  These values represent the
     * expected encumbrance for a piece of gear in that slot. */
    const int light_fighting_encumb_table[MAX_WEAR] = {
        0,
        0,
        0,
        0,
        0,
        1, /*body*/
        1, /*head*/
        1, /*legs*/
        1, /*feet*/
        1, /*hands*/
        1, /*arms*/
        2, /*shield*/
        0, /*about body*/
        0, /*about waist*/
        0,
        0,
        2, /*weapon*/
        0, /*held*/
        0, /*back*/
        0, /*belt*/
        0, /*belt*/
        0 /*belt*/
    };

    /* Light fighting weight values.  These values represent the expected
     * weight of an item in this slot. */
    const int light_fighting_weight_table[MAX_WEAR] = {
        0,
        0,
        0,
        0,
        0,
        225, /*body*/
        225, /*head*/
        225, /*legs*/
        225, /*feet*/
        225, /*hands*/
        225, /*arms*/
        500, /*shield*/
        50, /*about body*/
        0, /*about waist*/
        0,
        0,
        165, /*weapon*/
        0, /*held*/
        0, /*back*/
        0, /*belt*/
        0, /*belt*/
        0 /*belt*/
    };

    /* Encumbrance values used for heavy fighting.  These values represent the
     * expected encumbrance for a piece of gear in that slot. */
    const int heavy_fighting_encumb_table[MAX_WEAR] = {
        0,
        0,
        0,
        0,
        0,
        2, /*body*/
        2, /*head*/
        2, /*legs*/
        2, /*feet*/
        2, /*hands*/
        2, /*arms*/
        3, /*shield*/
        0, /*about body*/
        0, /*about waist*/
        0,
        0,
        3, /*weapon*/
        0, /*held*/
        0, /*back*/
        0, /*belt*/
        0, /*belt*/
        0 /*belt*/
    };

    /* Weight values used for heavy fighting.  These values represent the
     * expected weight for a piece of gear in that slot. */
    const int heavy_fighting_weight_table[MAX_WEAR] = {
        0,
        0,
        0,
        0,
        0,
        975, /*body*/
        325, /*head*/
        650, /*legs*/
        350, /*feet*/
        400, /*hands*/
        650, /*arms*/
        500, /*shield*/
        100, /*about body*/
        0, /*about waist*/
        0,
        0,
        250, /*weapon*/
        0, /*held*/
        0, /*back*/
        0, /*belt*/
        0, /*belt*/
        0 /*belt*/
    };

    //============================================================================
    // Returns the weight of items worn by the character, potentially multiplying
    // them by the encumbrance table.
    //============================================================================
    int get_character_item_weight(const char_data& character, const sh_int* encumb_table, int default_value)
    {
        game_types::player_specs spec = get_specialization(character);
        if (spec == game_types::PS_HeavyFighting) {
            int total_worn_weight = 0;
            for (int wear_index = 0; wear_index < MAX_WEAR; ++wear_index) {
                const obj_data* worn_item = character.equipment[wear_index];
                if (worn_item) {
                    int item_weight = worn_item->get_weight();
                    const int heavy_item_weight = heavy_fighting_weight_table[wear_index];
                    if (heavy_item_weight > 0 && item_weight > heavy_item_weight) {
                        // Heavy fighting uses the base item weight as the soft cap for worn weight.
                        int heavy_adjustment = item_weight - heavy_item_weight;
                        item_weight = heavy_item_weight + heavy_adjustment / 3;
                    }

                    if (encumb_table) {
                        int encumb_value = encumb_table[wear_index];
                        if (encumb_value > 0) {
                            total_worn_weight += item_weight * encumb_value;
                        } else {
                            total_worn_weight += item_weight / 2;
                        }
                    } else {
                        total_worn_weight += item_weight;
                    }
                }
            }

            return total_worn_weight;
        } else if (spec == game_types::PS_LightFighting) {
            int total_worn_weight = 0;
            for (int wear_index = 0; wear_index < MAX_WEAR; ++wear_index) {
                const obj_data* worn_item = character.equipment[wear_index];
                if (worn_item) {
                    int item_weight = worn_item->get_weight();
                    const int light_item_weight = light_fighting_weight_table[wear_index];

                    // Light fighting performs a soft weight reduction based on the item weight.
                    item_weight = std::max(item_weight - light_item_weight, 0);

                    if (encumb_table) {
                        int encumb_value = encumb_table[wear_index];
                        if (encumb_value > 0) {
                            total_worn_weight += item_weight * encumb_value;
                        } else {
                            total_worn_weight += item_weight / 2;
                        }
                    } else {
                        total_worn_weight += item_weight;
                    }
                }
            }

            return total_worn_weight;
        }

        return default_value;
    }

    //============================================================================
    // Returns the character's encumbrance value, calculated based on spec.
    //============================================================================
    int get_encumbrance(const char_data& character, const sh_int* encumb_table, int default_value)
    {
        if (get_specialization(character) == game_types::PS_HeavyFighting) {
            // Recalculate encumbrance.
            int new_encumb = 0;

            // Used to track how much the character is "over" the encumbrance difference.
            int heavy_fighting_encumbrance_difference = 0;

            for (int item_index = 0; item_index < MAX_WEAR; ++item_index) {
                if (encumb_table[item_index] > 0) {
                    const obj_data* worn_item = character.equipment[item_index];
                    if (worn_item) {
                        sh_int multiplier = encumb_table[item_index];
                        int item_encumbrance = worn_item->obj_flags.value[2];

                        const int heavy_item_encumbrance = heavy_fighting_encumb_table[item_index];
                        if (heavy_item_encumbrance > 0 && item_encumbrance > heavy_item_encumbrance) {
                            heavy_fighting_encumbrance_difference += (item_encumbrance - heavy_item_encumbrance) * multiplier;
                            item_encumbrance = heavy_item_encumbrance;
                        }

                        new_encumb += item_encumbrance * multiplier;
                    }
                }
            }

            // Drelidan:  Removing this for now, but leaving it in for future changes.
            // new_encumb += int(std::sqrt(heavy_fighting_encumbrance_difference));

            return new_encumb;
        } else if (get_specialization(character) == game_types::PS_LightFighting) {
            // Recalculate encumbrance.
            int new_encumb = 0;

            for (int item_index = 0; item_index < MAX_WEAR; ++item_index) {
                if (encumb_table[item_index] > 0) {
                    const obj_data* worn_item = character.equipment[item_index];
                    if (worn_item) {
                        int item_encumbrance = worn_item->obj_flags.value[2];
                        item_encumbrance = std::max(item_encumbrance - light_fighting_encumb_table[item_index], 0);
                        new_encumb += item_encumbrance * encumb_table[item_index];
                    }
                }
            }

            return new_encumb;
        }

        return default_value;
    }
}

//============================================================================
// Calculates the new encumbrance weight for a character based on specialization.
//============================================================================
int get_encumbrance_weight(const char_data& character)
{
    sh_int* encumb_table = get_encumb_table();
    int encumbrance_weight = character.specials.encumb_weight;

    return get_character_item_weight(character, encumb_table, encumbrance_weight);
}

//============================================================================
// Calculates the new worn weight for a character based on specialization.
//============================================================================
int get_worn_weight(const char_data& character)
{
    int worn_weight = character.specials.worn_weight;

    return get_character_item_weight(character, NULL, worn_weight);
}

//============================================================================
int get_encumbrance(const char_data& character)
{
    int base_encumb = character.points.encumb;
    sh_int* encumb_table = get_encumb_table();
    return get_encumbrance(character, encumb_table, base_encumb);
}

//============================================================================
int get_leg_encumbrance(const char_data& character)
{
    int base_encumb = character.specials2.leg_encumb;
    sh_int* encumb_table = get_leg_encumb_table();
    return get_encumbrance(character, encumb_table, base_encumb);
}

//============================================================================
int get_skill_penalty(const char_data& character)
{
    const int encumb_multiplier = 25;
    const int encumb_divisor = 50;

    int character_strength = get_bal_strength(character);
    int encumb_weight = get_encumbrance_weight(character);
    int encumbrance = get_encumbrance(character);

    int raw_encumb_factor = encumbrance * encumb_multiplier;
    int encumb_weight_factor = encumb_weight / character_strength;
    int skill_penalty = raw_encumb_factor + encumb_weight_factor;
    skill_penalty /= encumb_divisor;
    return skill_penalty;
}

//============================================================================
int get_dodge_penalty(const char_data& character)
{
    const int dodge_multiplier = 20;

    int character_strength = get_bal_strength(character);
    int worn_weight = get_worn_weight(character);

    int raw_encumb_factor = get_leg_encumbrance(character) * dodge_multiplier;
    int worn_weight_factor = worn_weight / character_strength;
    int dodge_penalty = raw_encumb_factor + worn_weight_factor;
    dodge_penalty /= dodge_multiplier;
    return dodge_penalty;
}

//============================================================================
long get_idnum(const char_data& character)
{
    if (is_npc(character))
        return -1;

    return character.specials2.idnum;
}

//============================================================================
bool is_awake(const char_data& character)
{
    return character.specials.position > POSITION_SLEEPING;
}

//============================================================================
int get_ranking_tier(const char_data& character)
{
    return get_ranking_tier(character.player.ranking);
}

//============================================================================
int get_ranking_tier(int ranking)
{
    if (ranking <= 3)
        return ranking;

    return 4;
}

//============================================================================
// Functions in this namespace do not belong in this file and need to be moved
// elsewhere.
//============================================================================
namespace TEMPORARY {
    int ch_get_confuse_modifier(const char_data& character)
    {
        int modifier = 0;
        const affected_type* status_effect = character.affected;

        // Iterate through the character affects, look for confuse, and
        // use its duration to determine its strength.
        while (modifier == 0 && status_effect) {
            if (status_effect->type == SPELL_CONFUSE) {
                modifier = (status_effect->duration * 2) - 10;
            }

            status_effect = status_effect->next;
        }

        return modifier;
    }
}

//============================================================================
int get_raw_skill(const char_data& character, int skill_index)
{
    if (character.player.bodytype == 2)
        return 0;

    // TODO(dgurley):  This is the GET_RAW_SKILL macro as written.
    // Ensure it is correct.
    return get_knowledge(character, skill_index);
}

//============================================================================
int get_skill(const char_data& character, int skill_index)
{
    int raw_skill = get_raw_skill(character, skill_index);

    if (is_affected_by(character, AFF_CONFUSE)) {
        raw_skill -= TEMPORARY::ch_get_confuse_modifier(character);
    }

    return raw_skill;
}

//============================================================================
void set_skill(char_data& character, int skill_index, byte value)
{
    if (character.skills) {
        character.skills[skill_index] = value;
    }
}

//============================================================================
int get_raw_knowledge(const char_data& character, int skill_index)
{
    if (!character.knowledge)
        return 80;

    return character.knowledge[skill_index];
}

//============================================================================
int get_knowledge(const char_data& character, int skill_index)
{
    int raw_knowledge = get_raw_knowledge(character, skill_index);

    if (is_affected_by(character, AFF_CONFUSE)) {
        raw_knowledge -= TEMPORARY::ch_get_confuse_modifier(character);
    }

    return raw_knowledge;
}

//============================================================================
void set_knowledge(char_data& character, int skill_index, byte value)
{
    if (character.knowledge) {
        character.knowledge[skill_index] = value;
    }
}

//============================================================================
int get_carry_weight_limit(const char_data& character)
{
    const int base_carry_weight = 2000;
    const int strength_multiplier = 1000;

    return base_carry_weight + strength_multiplier * character.tmpabilities.str;
}

//============================================================================
int get_carry_item_limit(const char_data& character)
{
    const int base_items = 5;

    return base_items + (character.tmpabilities.dex / 2) + (character.player.level / 2);
}

//============================================================================
bool is_twohanded(const char_data& character)
{
    return utils::is_set(character.specials.affected_by, (long)AFF_TWOHANDED);
}

//============================================================================
bool can_carry_object(const char_data& character, const obj_data& object)
{
    bool can_lift_object = character.specials.carry_weight + object.obj_flags.weight < get_carry_weight_limit(character);
    bool has_hands_free = character.specials.carry_items + 1 < get_carry_item_limit(character);

    return can_lift_object && has_hands_free;
}

//============================================================================
bool can_see(const char_data& character, const weather_data& weather, const room_data& room)
{
    if (character.in_room == NOWHERE)
        return false;

    if (is_affected_by(character, AFF_BLIND))
        return false;

    if (is_player_flagged(character, PLR_WRITING))
        return false;

    if (is_shadow(character))
        return true;

    // need the current room for the character as well.
    if (utils::is_light(room, weather))
        return true;

    if (is_affected_by(character, AFF_INFRARED) || is_preference_flagged(character, PRF_HOLYLIGHT))
        return true;

    if (weather.moonlight > 0 && is_affected_by(character, AFF_MOONVISION) && utils::is_room_outside(room))
        return true;

    return true;
}

//============================================================================
bool can_see_object(const char_data& character, const obj_data& object, const weather_data& weather,
    const room_data& room)
{
    int item_flags = object.obj_flags.extra_flags;

    if (is_shadow(character)) {
        return utils::is_set(item_flags, ITEM_MAGIC)
            || utils::is_set(item_flags, ITEM_WILLPOWER);
    } else {
        return can_see(character, weather, room)
            && (!utils::is_set(item_flags, ITEM_INVISIBLE)
                || is_affected_by(character, AFF_DETECT_INVISIBLE));
    }
}

//============================================================================
bool can_get_object(const char_data& character, const obj_data& object, const weather_data& weather,
    const room_data& room)
{
    return utils::can_wear(object, ITEM_TAKE)
        && can_carry_object(character, object)
        && can_see_object(character, object, weather, room);
}

//============================================================================
bool is_shadow(const char_data& character)
{
    if (is_npc(character)) {
        return utils::is_set(character.specials2.act, (long)MOB_SHADOW);
    } else {
        return utils::is_set(character.specials2.act, (long)PLR_ISSHADOW);
    }
}

//============================================================================
bool is_race_good(const char_data& character)
{
    return is_race_good(character.player.race);
}

//============================================================================
bool is_race_evil(const char_data& character)
{
    return is_race_evil(character.player.race);
}

//============================================================================
bool is_race_easterling(const char_data& character)
{
    return is_race_easterling(character.player.race);
}

//============================================================================
bool is_race_magi(const char_data& character)
{
    return is_race_magi(character.player.race);
}

//============================================================================
bool is_race_haradrim(const char_data& character)
{
    return is_race_haradrim(character.player.race);
}

//============================================================================
bool is_race_good(int race)
{
    return race > 0 && race < 10;
}

//============================================================================
bool is_race_evil(int race)
{
    return race > 10;
}

//============================================================================
bool is_race_easterling(int race)
{
    return race == 14;
}

//============================================================================
bool is_race_magi(int race)
{
    return race == 15;
}

//============================================================================
bool is_race_haradrim(int race)
{
    return race == 18;
}

//============================================================================
const char* get_object_string(const char_data& character, const obj_data& object,
    const weather_data& weather, const room_data& room)
{
    if (can_see_object(character, object, weather, room)) {
        return object.short_description;
    } else {
        return "something";
    }
}

//============================================================================
const char* get_object_name(const char_data& character, const obj_data& object,
    const weather_data& weather, const room_data& room)
{
    if (can_see_object(character, object, weather, room)) {
        // dgurley: fname isn't thread safe
        return fname(object.name);
    } else {
        return "something";
    }
}

//============================================================================
bool is_good(const char_data& character)
{
    return character.specials2.alignment >= 100;
}

//============================================================================
bool is_evil(const char_data& character)
{
    return character.specials2.alignment <= -100;
}

//============================================================================
bool is_neutral(const char_data& character)
{
    return !is_good(character) && !is_evil(character);
}

//============================================================================
bool is_hostile_to(const char_data& character, const char_data& victim)
{
    if (!is_npc(character))
        return false;

    // An NPC from the other side of the race war than the victim will be hostile.
    if (character.player.race != 0 && !is_npc(victim) && other_side(&character, &victim))
        return true;

    // An NPC is hostile if it has a preference for the victim's race set.
    if (is_preference_flagged(character, 1 << victim.player.race))
        return true;

    return false;
}

//============================================================================
bool is_rp_race_check(const char_data& character, const char_data& victim)
{
    return (is_npc(character) && character.specials2.rp_flag != 0) || utils::is_set(character.specials2.rp_flag, 1 << victim.player.race);
}

//============================================================================
bool is_riding(const char_data& character)
{
    return character.mount_data.mount && char_exists(character.mount_data.mount_number);
}

//============================================================================
bool is_ridden(const char_data& character)
{
    return character.mount_data.rider && char_exists(character.mount_data.rider_number);
}

//============================================================================
const char* get_prof_abbrev(const char_data& character)
{
    if (is_npc(character))
        return "--";

    static const char* prof_abbrevs[] = {
        "--", "Mu", "Cl", "Ra", "Wa"
    };

    int prof = character.player.prof;
    return prof_abbrevs[prof];
}

//============================================================================
const char* get_race_abbrev(const char_data& character)
{
    if (is_npc(character))
        return "--";

    // dgurley:  Unsure the extra padding is necessary.
    // Just copying the table from consts.
    const char* race_abbrevs[] = {
        "Imm",
        "Hum",
        "Dwf",
        "WdE",
        "Hob",
        "HiE",
        "Beo",
        "??",
        "??",
        "??",
        "??",
        "Urk",
        "Har",
        "Orc",
        "Eas",
        "Lhu",
        "??",
        "??",
        "??",
        "??",
        "Trl",
        "??"
    };

    int race = character.player.race;
    return race_abbrevs[race];
}

int get_race(const char_data& character)
{
    return character.player.race;
}

//============================================================================
int get_minimum_insight_perception(const char_data& character)
{
    int race = character.player.race;
    switch (race) {
    case RACE_GOD:
        return 0;
    case RACE_HUMAN:
        return 20;
    case RACE_DWARF:
        return 15;
    case RACE_WOOD:
        return 30;
    case RACE_HOBBIT:
        return 20;
    case RACE_BEORNING:
        return 15;
    case RACE_URUK:
        return 20;
    case RACE_ORC:
        return 15;
    case RACE_MAGUS:
        return 20;
    case RACE_HARADRIM:
        return 20;
    case RACE_OLOGHAI:
        return 15;
    default:
        return 0;
    }

    return 0;
}

//============================================================================
int get_race_perception(const char_data& character)
{
    int race = character.player.race;

    switch (race) {
    case RACE_GOD:
        return 0;
    case RACE_HUMAN:
        return 30;
    case RACE_DWARF:
        return 0;
    case RACE_WOOD:
        return 50;
    case RACE_HOBBIT:
        return 30;
    case RACE_HIGH:
        return 100;
    case RACE_BEORNING:
        return 15;
    case RACE_URUK:
        return 30;
    case RACE_HARAD:
        return 30;
    case RACE_ORC:
        return 10;
    case RACE_EASTERLING:
        return 30;
    case RACE_MAGUS:
        return 30;
    case RACE_UNDEAD:
        return 60;
    case RACE_TROLL:
        return 30;
    default:
        return 0;
    }

    return 0;
}

//============================================================================
int get_perception(const char_data& character)
{
    if (is_shadow(character))
        return 100;

    int perception = character.specials2.perception;
    if (character.specials2.perception == -1)
        return get_race_perception(character);

    // perception is clamped between 0 and 100.
    return std::min(std::max(perception, 0), 100);
}

//============================================================================
bool is_mental(const char_data& character)
{
    return is_shadow(character) || (!is_npc(character) && is_preference_flagged(character, PRF_MENTAL));
}

//============================================================================
game_types::player_specs get_specialization(const char_data& character)
{
    if (is_npc(character) || character.profs == NULL)
        return game_types::PS_None;

    return game_types::player_specs(character.profs->specialization);
}

//============================================================================
void set_specialization(char_data& character, game_types::player_specs value)
{
    if (is_npc(character) || character.profs == NULL)
        return;

    if (character.extra_specialization_data.is_mage_spec()) {
        untrack_specialized_mage(&character);
    }

    character.profs->specialization = (int)value;
    character.extra_specialization_data.set(character);

    if (character.extra_specialization_data.is_mage_spec()) {
        track_specialized_mage(&character);
    }
}

//============================================================================
bool is_resistant(const char_data& character, int attack_group)
{
    return (character.specials.resistance & (1 << attack_group)) != 0;
}

//============================================================================
bool is_vulnerable(const char_data& character, int attack_group)
{
    return (character.specials.vulnerability & (1 << attack_group)) != 0;
}

//============================================================================
bool is_guardian(const char_data& character)
{
    return is_npc(character) && is_affected_by(character, AFF_CHARM)
        && character.master != NULL && is_mob_flagged(character, MOB_GUARDIAN);
}

//============================================================================
int get_energy_regen(const char_data& character)
{
    int regen = character.points.ENE_regen;

    // woah now dre, const_casts are not legit.  leaky abstraction for sure
    player_spec::wild_fighting_handler handler(const_cast<char_data*>(&character));
    regen *= handler.get_attack_speed_multiplier();

    return regen;
}

}

//============================================================================
// Below here you will find various implementations from structs.h
// TODO(drelidan): Create a structs.cpp file and put all of this code there.
//============================================================================

//============================================================================
int char_data::get_spent_practice_count() const
{
    if (skills == NULL)
        return 0;

    int count = 0;
    for (int index = 0; index < MAX_SKILLS; ++index) {
        count += skills[index];
    }

    return count;
}

//============================================================================
int char_data::get_max_practice_count() const
{
    const int free_pracs = 10;

    int base_pracs = player.level * PRACS_PER_LEVEL;
    int bonus_lea_pracs = player.level * get_max_lea() / LEA_PRAC_FACTOR;

    return base_pracs + bonus_lea_pracs + free_pracs;
}

//============================================================================
void char_data::update_available_practice_sessions()
{
    specials2.spells_to_learn = get_max_practice_count() - get_spent_practice_count();
}

//============================================================================
void char_data::reset_skills()
{
    if (skills == NULL || knowledge == NULL)
        return;

    for (int index = 0; index < MAX_SKILLS; ++index) {
        skills[index] = 0;
        knowledge[index] = 0;
    }

    specials2.spells_to_learn = get_max_practice_count();
}

//============================================================================
bool char_data::is_affected() const
{
    return affected != NULL;
}

//============================================================================
// Specialization stuff!
//============================================================================
void cold_spec_data::on_chill_applied(int chill_amount)
{
    total_energy_sapped += chill_amount;
}

//============================================================================
void cold_spec_data::on_chill_ray_success(int damage)
{
    ++total_chill_ray_count;
    ++successful_chill_ray_count;
    total_chill_ray_damage += damage;
}

//============================================================================
void cold_spec_data::on_chill_ray_fail(int damage)
{
    ++total_chill_ray_count;
    ++failed_chill_ray_count;
    total_chill_ray_damage += damage;
}

//============================================================================
void cold_spec_data::on_cone_of_cold_success(int damage)
{
    ++total_cone_of_cold_count;
    ++successful_cone_of_cold_count;
    total_cone_of_cold_damage += damage;
}

//============================================================================
void cold_spec_data::on_cone_of_cold_failed(int damage)
{
    ++total_cone_of_cold_count;
    ++failed_cone_of_cold_count;
    total_cone_of_cold_damage += damage;
}

//============================================================================
void specialization_data::reset()
{
    if (current_spec_info) {
        delete current_spec_info;
        current_spec_info = NULL;
    }

    current_spec = game_types::PS_None;
}

//============================================================================
void specialization_data::set(char_data& character)
{
    reset();

    game_types::player_specs spec = utils::get_specialization(character);
    if (spec == game_types::PS_Darkness) {
        current_spec_info = new darkness_spec_data();
    } else if (spec == game_types::PS_Fire) {
        current_spec_info = new fire_spec_data();
    } else if (spec == game_types::PS_Lightning) {
        current_spec_info = new lightning_spec_data();
    } else if (spec == game_types::PS_Arcane) {
        current_spec_info = new arcane_spec_data();
    } else if (spec == game_types::PS_Cold) {
        current_spec_info = new cold_spec_data();
    } else if (spec == game_types::PS_Defender) {
        current_spec_info = new defender_data();
    } else if (spec == game_types::PS_LightFighting) {
        current_spec_info = new light_fighting_data();
    } else if (spec == game_types::PS_HeavyFighting) {
        current_spec_info = new heavy_fighting_data();
    } else if (spec == game_types::PS_WildFighting) {
        current_spec_info = new wild_fighting_data();
    } else if (spec == game_types::PS_BattleMage) {
        current_spec_info = new battle_mage_spec_data();
    }

    current_spec = spec;
}

//============================================================================
std::string specialization_data::to_string(char_data& character) const
{
    if (current_spec_info) {
        return current_spec_info->to_string(character);
    }

    return std::string("You are not specialized in anything.\r\n");
}

//============================================================================
std::string elemental_spec_data::to_string(char_data& character) const
{
    std::ostringstream message_writer;
    message_writer << "You are specialized in a mage specialization." << std::endl;
    message_writer << "------------------------------------------------------------" << std::endl;
    message_writer << "You have access to the 'expose elements' spell, which makes a particular" << std::endl;
    message_writer << "elemental spell cost much less mana on the target.  cast 'expose elements'." << std::endl;
    message_writer << "------------------------------------------------------------" << std::endl;
    report_exposed_data(message_writer);
    return message_writer.str();
}

//============================================================================
void elemental_spec_data::report_exposed_data(std::ostringstream& message_writer) const
{
    if (exposed_target) {
        const skill_data* skills = get_skill_array();
        const char* skill_name = skills[spell_id].name;

        message_writer << utils::get_name(*exposed_target) << " is exposed to the spell ";
        message_writer << "[" << skill_name << "]." << std::endl;
        message_writer << "------------------------------------------------------------" << std::endl;
    }
}

//============================================================================
std::string cold_spec_data::to_string(char_data& character) const
{
    std::ostringstream message_writer;
    message_writer << "You are specialized in cold." << std::endl;
    message_writer << "------------------------------------------------------------" << std::endl;
    message_writer << "Your cold spells are more difficult to resist." << std::endl;
    message_writer << "Your fire spells are easier to resist." << std::endl;
    message_writer << "You have access to the 'expose elements' spell, which makes a particular" << std::endl;
    message_writer << "elemental spell cost much less mana on the target.  cast 'expose elements'." << std::endl;
    message_writer << "Your cone of cold spell can now chill targets." << std::endl;
    message_writer << "Your chill ray spell is much harder to resist." << std::endl;
    message_writer << "------------------------------------------------------------" << std::endl;
    /*
        message_writer << "Chill Ray:" << std::endl;
        message_writer << "\tTotal Casts: " << get_chill_ray_count() << std::endl;
        message_writer << "\tSuccessful Casts: " << get_successful_chills() << std::endl;
        message_writer << "\tFailed Casts: " << get_saved_chills() << std::endl;
        message_writer << "\tTotal Damage: " << total_chill_ray_damage << std::endl << std::endl;
        message_writer << "Cone of Cold:" << std::endl;
        message_writer << "\tTotal Casts: " << get_cone_count() << std::endl;
        message_writer << "\tSuccessful Casts: " << get_successful_cones() << std::endl;
        message_writer << "\tFailed Casts: " << get_saved_cones() << std::endl;
        message_writer << "\tTotal Damage: " << total_cone_of_cold_damage << std::endl << std::endl;
        message_writer << "\tTotal Attacks Stopped: " << get_total_energy_sapped() / ENE_TO_HIT << std::endl;
        */
    report_exposed_data(message_writer);
    return message_writer.str();
}

//============================================================================
std::string fire_spec_data::to_string(char_data& character) const
{
    std::ostringstream message_writer;
    message_writer << "You are specialized in fire." << std::endl;
    message_writer << "------------------------------------------------------------" << std::endl;
    message_writer << "Your fire spells are more difficult to resist." << std::endl;
    message_writer << "Your cold spells are easier to resist." << std::endl;
    message_writer << "You have access to the 'expose elements' spell, which makes a particular" << std::endl;
    message_writer << "elemental spell cost much less mana on the target.  cast 'expose elements'." << std::endl;
    if (utils::is_race_good(character)) {
        message_writer << "The minimum damage of firebolt is increased significantly." << std::endl;
        message_writer << "Your fireballs will no longer spread to friendly targets." << std::endl;
    } else {
        message_writer << "Your searing darkness spell deals significantly more fire damage." << std::endl;
    }
    message_writer << "------------------------------------------------------------" << std::endl;
    report_exposed_data(message_writer);
    return message_writer.str();
}

//============================================================================
std::string lightning_spec_data::to_string(char_data& character) const
{
    std::ostringstream message_writer;
    message_writer << "You are specialized in lightning." << std::endl;
    message_writer << "------------------------------------------------------------" << std::endl;
    message_writer << "Your lightning spells are more difficult to resist." << std::endl;
    message_writer << "You have access to the 'expose elements' spell, which makes a particular" << std::endl;
    message_writer << "elemental spell cost much less mana on the target.  cast 'expose elements'." << std::endl;
    message_writer << "Lightning bolt does not lose effectiveness indoors, and deals increased damage." << std::endl;
    message_writer << "You can cast lightning strike without a storm at slightly reduced effectiveness." << std::endl;
    message_writer << "------------------------------------------------------------" << std::endl;
    report_exposed_data(message_writer);
    return message_writer.str();
}

//============================================================================
std::string darkness_spec_data::to_string(char_data& character) const
{
    std::ostringstream message_writer;
    message_writer << "You are specialized in darkness." << std::endl;
    message_writer << "------------------------------------------------------------" << std::endl;
    message_writer << "Your dark spells are more difficult to resist." << std::endl;
    message_writer << "You have access to the 'expose elements' spell, which makes a particular" << std::endl;
    message_writer << "elemental spell cost much less mana on the target.  cast 'expose elements'." << std::endl;
    message_writer << "Your dark bolt spell deals increased damage." << std::endl;
    if (utils::is_race_magi(character)) {
        message_writer << "Your black arrow is harder to resist." << std::endl;
        message_writer << "Your spear of darkness spell deals additional damage." << std::endl;
    } else {
        message_writer << "Your searing darkness spell deals additional dark damage." << std::endl;
    }
    message_writer << "------------------------------------------------------------" << std::endl;
    report_exposed_data(message_writer);
    return message_writer.str();
}

//============================================================================
std::string arcane_spec_data::to_string(char_data& character) const
{
    std::ostringstream message_writer;
    message_writer << "You are specialized in the arcane." << std::endl;
    message_writer << "------------------------------------------------------------" << std::endl;
    message_writer << "You have access to the 'expose elements' spell, which makes a particular" << std::endl;
    message_writer << "elemental spell cost much less mana on the target.  cast 'expose elements'." << std::endl;
    message_writer << "You can cast spells at a normal, fast, or slow pace." << std::endl;
    message_writer << "Slow cast spells against exposed targets will restore mana." << std::endl;
    message_writer << "------------------------------------------------------------" << std::endl;
    report_exposed_data(message_writer);

    return message_writer.str();
}

//============================================================================
std::string heavy_fighting_data::to_string(char_data& character) const
{
    return std::string("You are specialized in heavy fighting.");
}

//============================================================================
std::string light_fighting_data::to_string(char_data& character) const
{
    return std::string("You are specialized in light fighting.\n\r");
}

//============================================================================
std::string defender_data::to_string(char_data& character) const
{
    return std::string("You are specialized in defending.\n\r");
}

std::string battle_mage_spec_data::to_string(char_data& character) const
{
    return std::string("You are specialized in battle mage.\n\r");
}

//============================================================================
std::string wild_fighting_data::to_string(char_data& character) const
{
    return std::string("You are specialized in wild fighting.\n\r");
}

//============================================================================
// Damage reporting stuff!
//============================================================================
std::string player_damage_details::get_damage_report(const char_data* character) const
{
    typedef std::map<int, damage_details>::const_iterator map_iter;

    const skill_data* skills = get_skill_array();

    const char* character_name = utils::get_name(*character);
    if (damage_map.empty()) {
        std::ostringstream message_writer;
        message_writer << character_name << " has not recorded any damage dealt." << std::endl;
        return message_writer.str();
    }

    /* First pass: Calculate total damage dealt.*/
    long total_damage_dealt = 0;
    for (map_iter iter = damage_map.begin(); iter != damage_map.end(); ++iter) {
        total_damage_dealt += iter->second.get_total_damage();
    }

    std::ostringstream message_writer;
    message_writer << "Damage report details for " << character_name << ":" << std::endl;
    message_writer << "-------------------------------------------------------------------------------" << std::endl;

    for (map_iter iter = damage_map.begin(); iter != damage_map.end(); ++iter) {
        char ability_name[25];

        int ability_index = iter->first;
        if (ability_index > 128 && ability_index >= TYPE_HIT && ability_index < 152) {
            const attack_hit_type& hit_text = get_hit_text(ability_index);
            sprintf(ability_name, "%-24s", hit_text.singular);
        } else {
            const skill_data& skill = skills[iter->first];
            sprintf(ability_name, "%-24s", skill.name);
        }

        const damage_details& details = iter->second;

        message_writer << ability_name;
        message_writer << "<Count: " << details.get_instance_count();
        message_writer << ", Total: " << details.get_total_damage();
        message_writer << ", Max: " << details.get_largest_damage();
        message_writer << std::fixed;
        message_writer.precision(2);
        message_writer << ", Average: " << details.get_average_damage() << "> ";
        message_writer << (details.get_total_damage() / double(total_damage_dealt)) * 100;
        message_writer << "% of damage";
        message_writer << std::endl;
    }

    float combat_seconds = std::max(elapsed_combat_seconds, 0.5f);
    float dps = static_cast<float>(total_damage_dealt) / combat_seconds;
    message_writer << "-------------------------------------------------------------------------------" << std::endl;
    message_writer << "Total Damage: " << total_damage_dealt;
    message_writer << "; Combat Time: " << combat_seconds;
    message_writer << "s; Damage per Second: " << dps << std::endl;
    message_writer << "-------------------------------------------------------------------------------" << std::endl;
    return message_writer.str();
}

//============================================================================
std::string group_damaga_data::get_damage_report() const
{
    typedef std::map<char_data*, timed_damage_details>::const_iterator map_iter;

    if (damage_map.empty()) {
        return std::string("You have not recorded any damage dealt.\r\n");
    }

    /* First pass: Calculate total damage dealt.*/
    long total_damage_dealt = 0;
    for (map_iter iter = damage_map.begin(); iter != damage_map.end(); ++iter) {
        total_damage_dealt += iter->second.get_total_damage();
    }

    std::ostringstream message_writer;
    message_writer << "Group damage report details:" << std::endl;
    message_writer << "-------------------------------------------------------------------------------" << std::endl;

    for (map_iter iter = damage_map.begin(); iter != damage_map.end(); ++iter) {
        char character_name[100];

        char_data* character = iter->first;
        sprintf(character_name, "%-24s", utils::get_name(*character));

        const timed_damage_details& details = iter->second;

        message_writer << character_name;
        message_writer << "<Count: " << details.get_instance_count();
        message_writer << ", Total: " << details.get_total_damage();
        message_writer << ", Max: " << details.get_largest_damage();
        message_writer << std::fixed;
        message_writer.precision(2);
        message_writer << ", Average: " << details.get_average_damage();
        message_writer << ", DPS: " << details.get_dps() << "> ";
        message_writer << (details.get_total_damage() / double(total_damage_dealt)) * 100;
        message_writer << "% of group damage";
        message_writer << std::endl;
    }

    message_writer << "-------------------------------------------------------------------------------" << std::endl;
    message_writer << "Total Damage: " << total_damage_dealt << std::endl;
    message_writer << "-------------------------------------------------------------------------------" << std::endl;
    return message_writer.str();
}

//============================================================================
// Group-based code!
//============================================================================
void group_data::add_member(char_data* member)
{
    if (std::find(members.begin(), members.end(), member) != members.end())
        return;

    members.push_back(member);
    member->group = this;

    if (utils::is_pc(*member)) {
        ++pc_count;
    }
}

bool group_data::remove_member(char_data* member)
{
    /* The leader cannot be removed from a group. */
    if (member == leader)
        return false;

    char_iter member_iter = std::remove(members.begin(), members.end(), member);
    if (member_iter == members.end())
        return false;

    members.erase(member_iter);
    damage_report.remove(member);
    member->group = NULL;

    if (utils::is_pc(*member)) {
        --pc_count;
    }

    return true;
}

//============================================================================
bool group_data::is_member(struct char_data* character) const
{
    return std::find(members.begin(), members.end(), character) != members.end();
}

//============================================================================
void group_data::get_pcs_in_room(char_vector& pc_vec, int room_number) const
{
    for (const_char_iter iter = members.begin(); iter != members.end(); ++iter) {
        char_data* character = *iter;
        if (utils::is_pc(*character) && character->in_room == room_number) {
            pc_vec.push_back(character);
        }
    }
}

//============================================================================
void group_data::reset_damage()
{
    damage_report.reset();

    for (char_iter character = members.begin(); character != members.end(); ++character) {
        send_to_char("The group damage meter has been reset.\r\n", *character);
    }
}

//============================================================================
void group_data::track_combat_time(char_data* character, float elapsed_seconds)
{
    damage_report.track_time(character, elapsed_seconds);
}

//============================================================================
namespace string_func {
bool equals(const char* a, const char* b) { return strcmp(a, b) == 0; }

bool is_null_or_empty(const char* a) { return !a || a[0] == '\0'; }

bool contains(const char* a, const char* b) { return strstr(a, b) != NULL; }
}
