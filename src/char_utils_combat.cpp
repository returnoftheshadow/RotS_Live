#include "char_utils_combat.h"
#include "char_utils.h"
#include "environment_utils.h"
#include "object_utils.h"
#include "utils.h"

#include "handler.h" // for fname and other_side
#include "spells.h"

#include "big_brother.h"
#include "structs.h"
#include "world_clock.h"
#include <algorithm>
#include <assert.h>
#include <set>

namespace utils {
//============================================================================
// Functions in this namespace do not belong in this file and need to be moved
// elsewhere.
//============================================================================
namespace TEMPORARY {
    int chc_get_confuse_modifier(const char_data& character)
    {
        int modifier = 0;
        const affected_type* status_effect = character.affected;

        // Iterate through the character affects, look for confuse, and
        // use its duration to determine its strength.
        while (modifier == 0 && status_effect) {
            if (status_effect->type == SPELL_CONFUSE) {
                modifier = (status_effect->duration * 2) - 10;
                break;
            }

            status_effect = status_effect->next;
        }

        return modifier;
    }
}

namespace {
    //============================================================================
    double get_real_npc_dodge(const char_data& character)
    {
        using namespace utils;

        double npc_dodge(character.points.dodge + character.tmpabilities.dex - 5 + character.player.level / 2.0);
        if (is_affected_by(character, AFF_CONFUSE)) {
            npc_dodge -= double(TEMPORARY::chc_get_confuse_modifier(character)) * 2.0 / 3.0;
        }
        return npc_dodge;
    }

    //============================================================================
    double get_real_npc_parry(const char_data& character)
    {
        using namespace utils;

        double npc_parry(character.points.parry + character.player.level / 2.0 + 15.0);
        if (is_affected_by(character, AFF_CONFUSE)) {
            npc_parry -= double(TEMPORARY::chc_get_confuse_modifier(character)) * 2.0 / 3.0;
        }
        return npc_parry;
    }

    //============================================================================
    double get_dodge_skill_factor(const char_data& character)
    {
        double dodge_skill(utils::get_skill(character, SKILL_DODGE));
        double stealth_skill(utils::get_skill(character, SKILL_STEALTH));
        double ranger_level_factor(utils::get_prof_level(PROF_RANGER, character) * 0.005);

        double dodge = ((dodge_skill + (stealth_skill * 0.5) + 60) * ranger_level_factor) + (dodge_skill + (stealth_skill * 0.25)) * 0.05;
        return dodge;
    }

    //============================================================================
    double get_parry_skill_factor(const char_data& character, const obj_data* weapon)
    {
        const obj_flag_data& flags = weapon->obj_flags;
        int weapon_skill = weapon_skill_num(flags.value[3]);

        double skill_factor(utils::get_raw_knowledge(character, weapon_skill));
        if (utils::is_twohanded(character)) {
            skill_factor = skill_factor + utils::get_raw_knowledge(character, SKILL_TWOHANDED) * 0.5;
        }

        skill_factor = skill_factor + utils::get_raw_knowledge(character, SKILL_PARRY) * 0.75;

        int character_tactics = utils::get_tactics(character);
        if (character_tactics == TACTICS_BERSERK) {
            skill_factor *= 0.5;
        }

        return skill_factor;
    }

    //============================================================================
    double apply_defensive_sun_modifier(const char_data& character, double dodge_modifier)
    {
        int sun_mod = get_power_of_arda(const_cast<char_data*>(&character));
        if (sun_mod) {
            int race = character.player.race;
            if (race == RACE_MAGUS || race == RACE_URUK) {
                dodge_modifier = dodge_modifier * 0.9 - sun_mod;
            } else if (race == RACE_ORC) {
                dodge_modifier = dodge_modifier * (8.0 / 9.0) - sun_mod;
            }
        }

        return dodge_modifier;
    }

    //============================================================================
    double apply_dodge_tactics_modifier(const char_data& character, double dodge_modifier)
    {
        int tactics = utils::get_tactics(character);

        double base_dex(character.tmpabilities.dex);
        double total_dodge(character.points.dodge + dodge_modifier);

        switch (tactics) {
        case TACTICS_DEFENSIVE:
            total_dodge += 6.0 + base_dex;
            break;
        case TACTICS_CAREFUL:
            total_dodge += 4.0 + base_dex;
            break;
        case TACTICS_AGGRESSIVE:
            total_dodge += -4.0 + base_dex;
            break;
        case TACTICS_BERSERK:
            total_dodge += -4.0 + base_dex / 2.0;
            break;
        case TACTICS_NORMAL:
        default:
            total_dodge += base_dex;
            break;
        };

        return total_dodge;
    }

    //============================================================================
    double apply_parry_tactics_modifier(int character_tactics, double parry_bonus, int& tactics_mod)
    {
        switch (character_tactics) {
        case TACTICS_DEFENSIVE: {
            tactics_mod = 4;
            return parry_bonus * 0.6875;
        } break;
        case TACTICS_CAREFUL: {
            tactics_mod = 6;
            return parry_bonus * 0.625;
        } break;
        case TACTICS_NORMAL: {
            tactics_mod = 8;
            return parry_bonus * 0.5;
        } break;
        case TACTICS_AGGRESSIVE: {
            tactics_mod = 10;
            return parry_bonus * 0.375;
        } break;
        case TACTICS_BERSERK: {
            tactics_mod = 12;
            return parry_bonus * 0.375;
        } break;
        default: {
            tactics_mod = 10;
            return parry_bonus * 0.5;
        } break;
        };
    }

} // end anonymous namespace

//============================================================================
double get_real_dodge(const char_data& character)
{
    using namespace utils;

    if (is_npc(character)) {
        return get_real_npc_dodge(character);
    }

    double dodge = get_dodge_skill_factor(character);
    dodge -= get_dodge_penalty(character);
    dodge += 3;

    if (get_tactics(character) == TACTICS_BERSERK) {
        dodge *= 0.5;
    }

    if (is_affected_by(character, AFF_CONFUSE)) {
        dodge -= double(TEMPORARY::chc_get_confuse_modifier(character)) * 2.0 / 3.0;
    }

    dodge = apply_defensive_sun_modifier(character, dodge);
    dodge = apply_dodge_tactics_modifier(character, dodge);
    return dodge;
}

//============================================================================
double get_real_parry(const char_data& character, const weather_data& weather, const room_data& room)
{
    if (utils::is_npc(character)) {
        return get_real_npc_parry(character);
    }

    double parry(character.points.parry);
    double parry_bonus(utils::get_prof_level(PROF_WARRIOR, character) * 2 + utils::get_level_legend_cap(character) + utils::get_bal_strength(character));

    const obj_data* weapon = character.equipment[WIELD];
    if (!weapon)
        return parry + parry_bonus / 2.0;

    const obj_flag_data& flags = weapon->obj_flags;
    double weapon_bonus(flags.value[1]);

    double skill_factor = get_parry_skill_factor(character, weapon);

    int tactics_mod = 0;
    int character_tactics = utils::get_tactics(character);

    parry += apply_parry_tactics_modifier(character_tactics, parry_bonus, tactics_mod);
    parry += skill_factor * (weapon_bonus + 20) * (14 - tactics_mod) * 0.001;

    // Parry should now have bigger effect on two-handers:
    if (utils::is_twohanded(character)) {
        parry += weapon_bonus / 2;
    }

    if (utils::is_affected_by(character, AFF_CONFUSE)) {
        parry -= double(TEMPORARY::chc_get_confuse_modifier(character)) * 2.0 / 3.0;
    }

    parry = apply_defensive_sun_modifier(character, parry);

    if (utils::can_see(character, weather, room)) {
        parry -= 10;
    }

    return parry;
}

namespace {
    //============================================================================
    double get_real_npc_ob(const char_data& character)
    {
        using namespace utils;

        double base_ob = 15 + character.points.OB + get_bal_strength(character);
        base_ob -= get_skill_penalty(character) + character.player.level / 2.0;

        if (is_affected_by(character, AFF_CONFUSE)) {
            base_ob -= double(TEMPORARY::chc_get_confuse_modifier(character)) * 2.0 / 3.0;
        }

        return base_ob;
    }

    //============================================================================
    double get_ob_bonus(const char_data& character)
    {
        using namespace utils;

        // TODO(drelidan):  Perhaps replace get_bal_strength with a damage smoothing formula.
        int strength = get_bal_strength(character);
        int war_level = get_prof_level(PROF_WARRIOR, character);
        int max_war_level = get_max_race_prof_level(PROF_WARRIOR, character);
        int char_level = get_level_legend_cap(character);

        double strength_factor(strength);
        double level_factor((war_level * 3.0) + (3.0 * max_war_level * char_level / 30.0));
        level_factor *= 0.5;

        double ob_bonus = strength_factor + level_factor;
        return ob_bonus;
    }

    //============================================================================
    double get_ob_tactics_modifier(const char_data& character, double ob_bonus, int& skill_multiplier)
    {
        int character_tactics = utils::get_tactics(character);

        switch (character_tactics) {
        case TACTICS_DEFENSIVE: {
            skill_multiplier = 4;
            return ob_bonus * 0.75 - 8;
        } break;
        case TACTICS_CAREFUL: {
            skill_multiplier = 6;
            return ob_bonus * 0.875 - 4;
        } break;
        case TACTICS_NORMAL: {
            skill_multiplier = 8;
            return ob_bonus;
        } break;
        case TACTICS_AGGRESSIVE: {
            skill_multiplier = 10;
            return ob_bonus * 1.0625 + 2;
        } break;
        case TACTICS_BERSERK: {
            skill_multiplier = 10;
            return ob_bonus * 1.0625 + 5 + utils::get_raw_skill(character, SKILL_BERSERK) * 0.125;
        } break;
        default: {
            skill_multiplier = 0;
            return ob_bonus + utils::get_bal_strength(character);
        } break;
        };
    }

    //============================================================================
    double get_sun_offense_malus(const char_data& character, double base_ob)
    {
        /* to get the pre-power of arda malus, substitute 10 for sun_mod */
        int sun_mod = get_power_of_arda(const_cast<char_data*>(&character));
        if (sun_mod) {
            int race = character.player.race;
            if (race == RACE_URUK || race == RACE_MAGUS) {
                return base_ob * 0.8 - sun_mod;
            } else if (race == RACE_ORC) {
                return base_ob * 0.75 - sun_mod;
            }
        }

        return base_ob;
    }

} // end anonymous namespace

//============================================================================
double get_real_ob(const char_data& character, const weather_data& weather, const room_data& room)
{
    using namespace utils;

    if (is_npc(character)) {
        return get_real_npc_ob(character);
    }

    double ob_bonus = get_ob_bonus(character);

    double base_ob(character.points.OB);
    base_ob -= get_skill_penalty(character);

    const obj_data* weapon = character.equipment[WIELD];
    if (!weapon)
        return base_ob + ob_bonus;

    const obj_flag_data& flags = weapon->obj_flags;
    int weapon_skill = weapon_skill_num(flags.value[3]);

    double skill_factor(get_raw_knowledge(character, weapon_skill));
    int weapon_bulk = utils::get_item_bulk(*weapon);
    if (is_twohanded(character)) {
        int two_handed_skill = get_raw_knowledge(character, SKILL_TWOHANDED);
        base_ob += weapon_bulk * (200 + two_handed_skill) / 100 - 15;
        skill_factor += two_handed_skill * 0.5;
    } else {
        base_ob -= weapon_bulk * 2 - 6;
    }

    int skill_multiplier = 0;
    base_ob += get_ob_tactics_modifier(character, ob_bonus, skill_multiplier);

    if (is_affected_by(character, AFF_CONFUSE)) {
        base_ob -= double(TEMPORARY::chc_get_confuse_modifier(character)) * 2.0 / 3.0;
    }

    base_ob = get_sun_offense_malus(character, base_ob);

    if (utils::can_see(character, weather, room)) {
        base_ob -= 10;
    }

    base_ob += skill_factor * (weapon_bulk + 20) * skill_multiplier * 0.001;

    return base_ob;
}

//============================================================================
// Returns a vector of all of the characters that are currently engaged with the
// character passed in.
//============================================================================
std::vector<char_data*> get_engaged_characters(const char_data* character, const room_data& room)
{
    std::set<char_data*> seen_fighters;
    std::vector<char_data*> engaged_fighters;

    // Add the person the character is fighting to the list.
    if (character->specials.fighting != NULL) {
        seen_fighters.insert(character->specials.fighting);
        engaged_fighters.push_back(character->specials.fighting);
    }

    // Add everyone in the room that is fighting the character to the list.
    for (char_data* other = room.people; other; other = other->next_in_room) {
        if (other->specials.fighting == character) {
            if (seen_fighters.insert(other->specials.fighting).second) {
                engaged_fighters.push_back(other->specials.fighting);
            }
        }
    }

    return engaged_fighters;
}

//============================================================================
bool is_victim_player(const char_data* victim)
{
    assert(victim);

    if (utils::is_pc(*victim)) {
        return true;
    } else if (utils::is_ridden(*victim)) {
        return is_victim_player(victim->mount_data.rider);
    } else if (victim->master) {
        return is_victim_player(victim->master);
    } else {
        return false;
    }
}

//============================================================================
char_data* get_controlling_player(char_data* character)
{
    assert(character);

    if (utils::is_pc(*character)) {
        return character;
    } else if (utils::is_ridden(*character)) {
        return get_controlling_player(character->mount_data.rider);
    } else if (character->master) {
        return get_controlling_player(character->master);
    } else {
        return NULL;
    }
}

//============================================================================
void on_attacked_character(char_data* attacker, char_data* victim)
{
    /* Give anger */
    if (victim && !IS_NPC(attacker) && (victim != attacker)) {
        affected_type affect;
        affected_type* existing_affect;

        bool is_long_anger = is_victim_player(victim);
        int duration = is_long_anger ? 5 : 2;
        existing_affect = affected_by_spell(attacker, SPELL_ANGER);
        if (existing_affect) {
            existing_affect->duration = duration;
        } else {
            affect.type = SPELL_ANGER;
            affect.duration = duration;
            affect.modifier = 0;
            affect.location = APPLY_NONE;
            affect.bitvector = 0;
            affect_to_char(attacker, &affect);
        }

        // Alert Big Brother that some PK is happening.
        if (is_long_anger) {
            char_data* attacked_player = get_controlling_player(victim);

            game_rules::big_brother& bb_instance = game_rules::big_brother::instance();
            const WorldClock::time_point now = WorldClock::now();
            bb_instance.on_character_attacked_player(attacker, attacked_player, now);
        }
    }
}
}
