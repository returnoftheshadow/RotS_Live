#include "combat_manager.h"

#include "char_utils.h"
#include "char_utils_combat.h"
#include "object_utils.h"

#include "comm.h"
#include "spells.h"
#include "utils.h"

#include <cmath>

namespace game_rules {
//============================================================================
namespace {
    //============================================================================
    int weapon_hit_type(game_types::weapon_type weapon_type)
    {
        int hit_type = TYPE_HIT;
        switch (weapon_type) {
        case game_types::WT_UNUSED_1:
        case game_types::WT_UNUSED_2:
        case game_types::WT_WHIPPING:
            hit_type = TYPE_WHIP;
            break;
        case game_types::WT_SLASHING:
        case game_types::WT_SLASHING_TWO:
            hit_type = TYPE_SLASH;
            break;
        case game_types::WT_FLAILING:
            hit_type = TYPE_FLAIL;
            break;
        case game_types::WT_BLUDGEONING:
        case game_types::WT_BLUDGEONING_TWO:
            hit_type = TYPE_BLUDGEON;
            break;
        case game_types::WT_CLEAVING:
        case game_types::WT_CLEAVING_TWO:
            hit_type = TYPE_CLEAVE;
            break;
        case game_types::WT_STABBING:
            hit_type = TYPE_SPEARS;
            break;
        case game_types::WT_PIERCING:
            hit_type = TYPE_PIERCE;
            break;
        case game_types::WT_SMITING:
            hit_type = TYPE_SMITE;
            break;
        case game_types::WT_BOW:
            hit_type = TYPE_WHIP;
            break;
        case game_types::WT_CROSSBOW:
            hit_type = TYPE_BLUDGEON;
            break;
        case game_types::WT_COUNT:
            hit_type = TYPE_HIT;
            break;
        default:
            hit_type = TYPE_HIT;
            break;
        }

        return hit_type;
    }

    //============================================================================
    // Returns the part of the body on the victim that is getting hit.
    //============================================================================
    int get_hit_location(const char_data& victim)
    {
        int hit_location = 0;

        int body_type = victim.player.bodytype;

        const race_bodypart_data& body_data = bodyparts[body_type];
        if (body_data.bodyparts != 0) {
            int roll = number(1, 100);
            while (roll > 0 && hit_location < MAX_BODYPARTS) {
                roll -= body_data.percent[hit_location++];
            }
        }

        if (hit_location > 0)
            --hit_location;

        return hit_location;
    }

} // end anonymous namespace

/********************************************************************
 * Public Functions
 *********************************************************************/
//============================================================================
bool combat_manager::can_attack(const char_data* attacker, const char_data* victim)
{
    if (!attacker || !victim)
        return false;

    if (attacker == victim)
        return false;

    // TODO(drelidan):  Put additional checks in here, such as sight, peace room,
    // attacker and victim being colocated, etc.

    return true;
}

//============================================================================
combat_manager::ob_roll combat_manager::roll_ob(const char_data* attacker)
{
    double roll = number(35.0);

    const room_data& attacker_room = m_world[attacker->in_room];
    double attacker_OB = utils::get_real_ob(*attacker, m_weather, attacker_room);
    attacker_OB += number_d(1, 55 + attacker_OB * 0.25);
    attacker_OB += roll;
    attacker_OB = attacker_OB * 7.0 / 8.0;
    attacker_OB -= 40.0;

    bool is_crit = roll > 34.0;
    if (is_crit) {
        attacker_OB += 100.0;
    }

    return ob_roll(is_crit, attacker_OB);
}

//============================================================================
bool combat_manager::is_hit_accurate(const char_data& attacker)
{
    int tactics = utils::get_tactics(attacker);

    // Hits can only be accurate on careful or aggressive tactics.
    if (tactics != TACTICS_CAREFUL && tactics != TACTICS_AGGRESSIVE)
        return false;

    int target_number = utils::get_prof_level(PROF_RANGER, attacker);
    target_number -= utils::get_skill_penalty(attacker);
    target_number -= utils::get_dodge_penalty(attacker);
    target_number *= utils::get_skill(attacker, SKILL_ACCURACY) / 100;

    int roll = number(0, 100);
    return roll < target_number;
}

//============================================================================
double combat_manager::offense_if_weapon_hits(char_data* attacker, char_data* victim, bool hit_accurate)
{
    ob_roll rolled_ob = roll_ob(attacker);

    double attacker_ob = rolled_ob.get_ob_value();

    int victim_position = victim->specials.position;
    if (victim_position < POSITION_FIGHTING) {
        attacker_ob += 10 * (POSITION_FIGHTING - victim_position);
    }

    if (hit_accurate) {
        act("You manage to find an opening in $N's defense!", TRUE, attacker, NULL, victim, TO_CHAR);
        act("$n notices an opening in your defense!", TRUE, attacker, NULL, victim, TO_VICT);
    } else {
        // Test for dodge and evasion.
        double dodge_malus = utils::get_real_dodge(*victim);

        attacker_ob -= dodge_malus;
        if (attacker_ob < 0.0 && !rolled_ob.is_crit()) {
            // Victim dodged the attack.
            act("$N dodges $n's attack.", FALSE, attacker, 0, victim, TO_NOTVICT, TRUE);
            act("$N dodges your attack.", FALSE, attacker, 0, victim, TO_CHAR, TRUE);
            act("You dodge $n's attack.", FALSE, attacker, 0, victim, TO_VICT, TRUE);
            return attacker_ob;
        }

        double evasion_malus = get_evasion_malus(*attacker, *victim);

        attacker_ob -= evasion_malus;
        if (attacker_ob < 0.0 && !rolled_ob.is_crit()) {
            // Victim evaded the attack.
            act("$N distracts $n into missing $M.", FALSE, attacker, 0, victim, TO_NOTVICT, TRUE);
            act("$N is not where you attack $M.", FALSE, attacker, 0, victim, TO_CHAR, TRUE);
            act("You evade $n's attack.", FALSE, attacker, 0, victim, TO_VICT, TRUE);
            return attacker_ob;
        }
    }

    // Test for parry.
    if (victim_position > POSITION_STUNNED) {
        const room_data& room = m_world[victim->in_room];
        double victim_parry = utils::get_real_parry(*victim, m_weather, room);
        victim_parry *= victim->specials.current_parry * 0.01;

        // Apply a parry malus to the victim.
        victim->specials.current_parry = victim->specials.current_parry * 2 / 3;

        attacker_ob -= victim_parry;
        if (rolled_ob.is_crit()) {
            // Attackers can't get below 0 OB with a roll of 35.
            attacker_ob = std::max(attacker_ob, 0.0);
        }

        if (attacker_ob < 0.0) {
            act("$N deflects $n's attack.", FALSE, attacker, 0, victim, TO_NOTVICT, TRUE);
            act("$N deflects your attack.", FALSE, attacker, 0, victim, TO_CHAR, TRUE);
            act("You deflect $n's attack.", FALSE, attacker, 0, victim, TO_VICT, TRUE);

            if (does_victim_riposte(attacker, victim)) {
                do_riposte(attacker, victim);
            }

            check_grip(attacker, attacker->equipment[WIELD]);
            check_grip(victim, victim->equipment[WIELD]);
        }
    }

    return attacker_ob;
}

//============================================================================
double combat_manager::get_evasion_malus(const char_data& attacker, const char_data& victim)
{
    if (!utils::is_affected_by(victim, AFF_EVASION))
        return 0.0;

    const double BASE_VALUE = 5.0;

    double defender_bonus = utils::get_prof_level(PROF_CLERIC, victim) * 0.5;
    double attacker_offset = utils::get_prof_level(PROF_CLERIC, attacker) * (100 - utils::get_perception(attacker)) * 0.005;

    if (utils::is_npc(attacker)) {
        attacker_offset *= 0.5;
    }

    int spec_bonus = 0;
    // Protection specialization gets additional defenses against magic.
    if (utils::get_specialization(victim) == game_types::PS_Protection) {
        spec_bonus += 3;
    }

    // Always return at least the base value.
    if (attacker_offset > defender_bonus)
        return BASE_VALUE + spec_bonus;

    return BASE_VALUE + spec_bonus + defender_bonus - attacker_offset;
}

//============================================================================
bool combat_manager::does_victim_riposte(const char_data* attacker, const char_data* riposter)
{
    if (utils::is_npc(*riposter))
        return false;

    int riposte_skill = utils::get_skill(*riposter, SKILL_RIPOSTE);
    if (riposte_skill == 0)
        return false;

    if (riposter->specials.position != POSITION_FIGHTING)
        return false;

    if (utils::is_affected_by(*riposter, AFF_BASH))
        return false;

    obj_data* weapon = riposter->equipment[WIELD];
    if (!weapon)
        return false;

    if (weapon->get_bulk() > 3)
        return false;

    int prob = riposte_skill;
    prob += utils::get_skill(*riposter, SKILL_STEALTH);
    prob += riposter->tmpabilities.dex * 5;
    prob *= utils::get_prof_level(PROF_RANGER, *riposter);
    prob /= 200;

    int roll = number(0, 99);
    return roll <= prob;
}

//============================================================================
void combat_manager::do_riposte(char_data* attacker, char_data* riposter)
{
    act("$n's riposte catches $N off guard.", FALSE, attacker, 0, riposter, TO_NOTVICT, FALSE);
    act("Your riposte catches $N off guard.", FALSE, attacker, 0, riposter, TO_CHAR, FALSE);
    act("$n's riposte catches you off guard.", FALSE, attacker, 0, riposter, TO_VICT, FALSE);

    obj_data* weapon = riposter->equipment[WIELD];
    double rip_damage = utils::get_weapon_damage(*weapon);
    rip_damage *= std::min((int)riposter->tmpabilities.dex, 20);
    rip_damage /= number_d(50.0, 100.0);

    // TODO(drelidan):  Include skill type and weapon type.
    apply_damage(attacker, riposter, rip_damage, weapon_hit_type(weapon->get_weapon_type()), 21);
}

//============================================================================
// Basically, if a character is wielding a one-handed weapon
// with both hands, we cause them to lose energy, and thus slow
// them down.
//
// NOTE: A weapon is considered one handed if it has a bulk of
// 4 or less.
//============================================================================
void combat_manager::check_grip(char_data* character, const obj_data* weapon)
{
    if (!weapon || !character)
        return;

    int bulk = weapon->get_bulk();
    if (bulk <= 4 && utils::is_twohanded(*character)) {
        int roll = number(0, 99);
        int target = 30 + 5 - bulk;
        if (roll < target) {
            character->specials.ENERGY -= 300 + ((5 - bulk) * 100);
            act("You struggle to maintain your grip on $p.", FALSE, character, weapon, NULL, TO_CHAR);
        }
    }
}

//============================================================================
void combat_manager::on_weapon_hit(char_data* attacker, char_data* victim, bool hit_accurate, double remaining_ob)
{
    double weapon_damage = calculate_weapon_damage(*attacker);

    double damage = calculate_hit_damage(*attacker, hit_accurate, weapon_damage, remaining_ob);

    if (does_find_weakness(*attacker)) {
        act("You discover a weakness in $N's defense!", TRUE, attacker, NULL, victim, TO_CHAR);
        act("$n discovers a weakness in $N's defense!", TRUE, attacker, NULL, victim, TO_NOTVICT);
        act("$n discovers a weakness in your defense!", TRUE, attacker, NULL, victim, TO_VICT);

        damage *= 1.5;
    }

    if (does_rush(*attacker)) {
        send_to_char("You rush forward wildly.\n\r", attacker);
        act("$n rushes forward wildly.", TRUE, attacker, NULL, NULL, TO_ROOM);

        damage *= 1.5;
    }

    apply_weapon_damage(attacker, victim, damage);
}

//============================================================================
int combat_manager::get_weapon_skill_type(const char_data& attacker)
{
    int weapon_type = TYPE_HIT;

    obj_data* weapon = attacker.equipment[WIELD];
    if (weapon && weapon->obj_flags.type_flag == ITEM_WEAPON) {
        weapon_type = weapon_hit_type(weapon->get_weapon_type());
    } else {
        if (utils::is_npc(attacker) && attacker.specials.attack_type >= TYPE_HIT) {
            weapon_type = attacker.specials.attack_type;
        }
    }

    return weapon_type;
}

//============================================================================
double combat_manager::calculate_weapon_damage(const char_data& attacker)
{
    double weapon_damage = 0.0;
    obj_data* weapon = attacker.equipment[WIELD];
    if (weapon && weapon->obj_flags.type_flag == ITEM_WEAPON) {
        weapon_damage = utils::get_weapon_damage(*weapon);
    } else {
        if (utils::is_pc(attacker)) {
            weapon_damage = BAREHANDED_DAMAGE * 10.0;
        }
    }

    if (utils::is_npc(attacker)) {
        weapon_damage *= 0.5; // mobs have weapon damage halved
    }

    return weapon_damage;
}

//============================================================================
double combat_manager::calculate_hit_damage(const char_data& attacker, bool hit_accurate, double weapon_damage, double remaining_ob)
{
    double damage = weapon_damage + attacker.points.damage * 10;
    double damage_roll = number(100);
    double random_factor = std::pow(damage_roll, 2) + 10000;
    if (hit_accurate) {
        damage = damage * random_factor / 100000.0;
    } else {
        double strength_factor = utils::get_bal_strength_d(attacker);
        double ob_factor = remaining_ob + 100.0;
        int two_handed_factor = utils::is_twohanded(attacker) + 1;

        damage = (damage * ob_factor * random_factor + (133.0 * two_handed_factor) * strength_factor)
            / 13300000.0;
    }

    return damage;
}

//============================================================================
bool combat_manager::does_find_weakness(const char_data& attacker)
{
    if (utils::is_npc(attacker))
        return false;

    if (true) {
        // Old formula.  I don't like it.
        int warrior_level = utils::get_prof_level(PROF_WARRIOR, attacker);
        int prob = utils::get_raw_skill(attacker, SKILL_EXTRA_DAMAGE) / 3 * warrior_level / 30;
        if (warrior_level > 30) {
            // This disproportionately favors high level warriors.
            prob += warrior_level - 30;
        }

        int roll = number(0, 99);
        return prob > roll;
    } else {
        // Chance to find is skill * warrior level in 'new' formula.
        double chance = utils::get_raw_skill(attacker, SKILL_EXTRA_DAMAGE) * 0.001;
        chance *= utils::get_prof_level(PROF_WARRIOR, attacker);

        double roll = number();
        return chance > roll;
    }
}

//============================================================================
bool combat_manager::does_rush(const char_data& attacker)
{
    if (utils::get_specialization(attacker) != PLRSPEC_WILD)
        return false;

    const double RUSH_CHANCE = 0.1;

    double roll = number();
    return RUSH_CHANCE > roll;
}

//============================================================================
void combat_manager::apply_weapon_damage(char_data* attacker, const char_data* victim, double damage)
{
    int hit_location = get_hit_location(*victim);
    int weapon_type = get_weapon_skill_type(*attacker);

    int body_type = victim->player.bodytype;
    const race_bodypart_data& body_data = bodyparts[body_type];

    // Apply damage reduction.
    int armor_location = body_data.armor_location[hit_location];
    damage = apply_armor_reduction(attacker, victim, damage, weapon_type, armor_location);

    apply_damage(attacker, victim, damage, weapon_type, hit_location);

    if (damage > 0) {
        check_grip(attacker, attacker->equipment[WIELD]);
    }
}

//============================================================================
// Given a hit location and a damage, calculate how much damage
// should be done after the victim's armor is factored in.  The
// modified amount is returned.
//============================================================================
double combat_manager::apply_armor_reduction(const char_data* attacker, const char_data* victim, double damage, int weapon_type, int hit_location)
{
    // Bogus hit location
    if (hit_location < 0 || hit_location > MAX_WEAR)
        return 0.0;

    // If they've got armor, let's let it do its thing
    obj_data* armor = victim->equipment[hit_location];
    if (armor) {
        double base_damage = damage;

        // First, remove minimum absorb
        damage -= armor->obj_flags.value[1];

        // Calculate damage reduction.
        double damage_reduction = (damage * armor_absorb(armor) + 50) * 0.01;
        if (weapon_type == TYPE_SPEARS) {
            // Spears ignore half of the armor.
            damage_reduction *= 0.5;
        }

        damage -= damage_reduction;

        /*
         * Smiting weapons can sometimes crush an opponent's bones
         * under the armor; wearing armor will actually INCREASE
         * the amount of damage taken, since the bodypart cannot
         * recoil under the disfigured metal.  Additionally, one's
         * head can hit the armor from the inside and add extra
         * damage.
         *
         * Based on a real life program on the discovery channel.
         *
         * Alright, now smiting only works on rigid metal armor,
         * not chain or leather.  Unfortunately, it won't work on
         * armor made of mithril, even if the description says the
         * armor is made of mithril plats.  Perhaps mithril should
         * never appear in plates? Or perhaps we should have some
         * other flag to define rigidity? Or perhaps smiting should
         * not exist? :)
         */
        if (weapon_type == TYPE_SMITE && armor->obj_flags.material == 4) {
            const double SMITE_CHANCE = 0.2;
            double roll = number();
            if (SMITE_CHANCE > roll) {
                send_to_char("Your opponent's bones crunch loudly.\n\r", attacker);
                send_to_char("OUCH! You hear a crunching sound and feel a sharp pain.\n\r", victim);
                send_to_room_except_two("You hear a crunching sound.\n\r", attacker->in_room, attacker, victim);
                damage = base_damage + (base_damage - damage);
            }
        }
    }

    return damage;
}

//============================================================================
// Damage returns true if the victim is killed and false otherwise.
//============================================================================
bool combat_manager::apply_damage(const char_data* attacker, const char_data* victim, double damage, int attack_type, int hit_location)
{
    return false;
}

/********************************************************************
 * Singleton Implementation
 *********************************************************************/
combat_manager* combat_manager::m_pInstance(0);
bool combat_manager::m_bDestroyed(false);

//============================================================================
combat_manager::combat_manager(const weather_data& weather, const room_data* world)
    : m_weather(weather)
    , m_world(world)
{
}

//============================================================================
combat_manager::~combat_manager()
{
}
}
