#include "char_utils.h"
#include "comm.h"
#include "handler.h"
#include "spells.h"
#include "utils.h"
#include "warrior_spec_handlers.h"

namespace player_spec {
//============================================================================
// Weapon-master implementation
//============================================================================

namespace {
    constexpr float smiting_proc_chance(int damage)
    {
        return damage * 0.005f;
    }
    constexpr const float bludgeon_proc_chance = 0.25f;
    constexpr const float cleave_proc_chance = 0.50f;

    constexpr const float flail_proc_chance = 0.40f;
    constexpr const float piercing_proc_chance = 0.25f;
    constexpr const float slashing_proc_chance = 0.40f;
    constexpr const float stabbing_proc_chance = 0.50f;
    constexpr const float whipping_proc_chance = 0.40f;

} // namespace

weapon_master_handler::weapon_master_handler(char_data* in_character)
    : weapon_master_handler(in_character, in_character->equipment[WIELD])
{
}

weapon_master_handler::weapon_master_handler(char_data* in_character, const obj_data* in_weapon)
    : character(in_character)
    , weapon(in_weapon)
    , spec(utils::get_specialization(*in_character))
{
    if (weapon != nullptr && weapon->obj_flags.type_flag == ITEM_WEAPON) {
        weapon_type = weapon->get_weapon_type();
    }
}

float weapon_master_handler::get_attack_speed_multiplier() const
{
    if (spec != game_types::PS_WeaponMaster)
        return 1.0f;

    if (weapon_type == game_types::WT_PIERCING || weapon_type == game_types::WT_WHIPPING)
        return 1.15f;

    return 1.0f;
}

int weapon_master_handler::get_total_damage(int starting_damage) const
{
    if (spec != game_types::PS_WeaponMaster)
        return starting_damage;

    if (weapon_type == game_types::WT_CLEAVING || weapon_type == game_types::WT_CLEAVING_TWO || weapon_type == game_types::WT_FLAILING) {
        // 15% bonus damage with these weapon types
        int bonus_damage = starting_damage * 15 / 100;
        return starting_damage + bonus_damage;
    }

    return starting_damage;
}

int weapon_master_handler::do_on_damage_rolled(int damage_roll, char_data* victim)
{
    if (spec != game_types::PS_WeaponMaster)
        return damage_roll;

    if (weapon_type == game_types::WT_CLEAVING || weapon_type == game_types::WT_CLEAVING_TWO) {
        if (number() < cleave_proc_chance) {
            int new_roll = number(1, 100);
            if (new_roll > damage_roll) {

                act("Your axe strikes hard and true!", FALSE, character, NULL, victim, TO_CHAR);
                return new_roll;
            }
        }
    }

    return damage_roll;
}

bool weapon_master_handler::ignores_shields(char_data* victim)
{
    if (spec != game_types::PS_WeaponMaster)
        return false;

    if (victim == nullptr || victim->equipment[WEAR_SHIELD] == nullptr)
        return false;

    // whips and flails can ignore armor
    if (weapon_type != game_types::WT_FLAILING && weapon_type != game_types::WT_WHIPPING)
        return false;

    if (weapon_type == game_types::WT_FLAILING) {
        if (number() <= flail_proc_chance) {
            act("Your flail wraps around $N's shield, rendering it immaterial!", FALSE, character, NULL, victim, TO_CHAR);
            act("$n's flail wraps around your shield, rendering it immaterial!.", FALSE, character, NULL, victim, TO_VICT);
            return true;
        }
    } else if (weapon_type == game_types::WT_WHIPPING) {
        if (number() <= whipping_proc_chance) {
            act("Your whip snakes past $N's shield, rendering it immaterial!", FALSE, character, NULL, victim, TO_CHAR);
            act("$n's whip snakes past your shield, rendering it immaterial!", FALSE, character, NULL, victim, TO_VICT);
            return true;
        }
    }

    return false;
}

bool weapon_master_handler::ignores_armor(char_data* victim)
{
    if (spec != game_types::PS_WeaponMaster)
        return false;

    if (weapon_type != game_types::WT_PIERCING)
        return false;

    if (number() > piercing_proc_chance)
        return false;

    // act so people know armor is ignored
    act("You find a weakness in $N's armor and slip your weapon right through!", FALSE, character, NULL, victim, TO_CHAR);
    act("$n finds a weakness in your armor and slips $s weapon right through!", FALSE, character, NULL, victim, TO_VICT);
    return true;
}

bool weapon_master_handler::does_spear_proc(char_data* victim)
{
    if (spec != game_types::PS_WeaponMaster)
        return false;

    if (weapon_type != game_types::WT_STABBING)
        return false;

    if (number() > stabbing_proc_chance)
        return false;

    act("Your spear punches through $N's armor!", FALSE, character, nullptr, victim, TO_CHAR);
    act("$n thrusts $s spear forcefully, punching a hole in your armor!", FALSE, character, nullptr, victim, TO_VICT);
    return true;
}

void weapon_master_handler::regain_energy(char_data* victim)
{
    if (!does_sword_proc())
        return;

    // It's possible that victim has died in response to the strike
    // that caused this.  Only send a message if the character is currently
    // in combat.
    if (character->specials.fighting != nullptr) {
        act("You gain a rush of momentum!", FALSE, character, NULL, victim, TO_CHAR);
        act("$n gains a rush of momentum!", FALSE, character, 0, victim, TO_ROOM, FALSE);
    }

    character->specials.ENERGY += ENE_TO_HIT / 2;
}

bool weapon_master_handler::does_sword_proc() const
{
    if (spec != game_types::PS_WeaponMaster)
        return false;

    if (weapon_type != game_types::WT_SLASHING && weapon_type != game_types::WT_SLASHING_TWO)
        return false;

    return number() <= slashing_proc_chance;
}

void weapon_master_handler::do_on_damage_dealt(int damage, char_data* victim)
{
    if (spec != game_types::PS_WeaponMaster)
        return;

    if (weapon_type == game_types::WT_BLUDGEONING || weapon_type == game_types::WT_BLUDGEONING_TWO) {
        if (number() <= bludgeon_proc_chance) {
            int lost_energy = damage * 10; // victim loses 10x damage taken as energy
            victim->specials.ENERGY -= lost_energy;
            victim->specials.ENERGY = std::max(victim->specials.ENERGY, 0);

            act("Your crushing blow knocks the wind from $N!", FALSE, character, NULL, victim, TO_CHAR);
            act("$n's blow staggers you and knocks the wind from your chest!", FALSE, character, NULL, victim, TO_VICT);
        }
    } else if (weapon_type == game_types::WT_SMITING) {
        // damage influences chance to inflict haze
        if (number() <= smiting_proc_chance(damage)) {
            // victim is afflicted with haze

            auto haze = affected_by_spell(victim, SPELL_HAZE);
            if (haze == nullptr) {
                affected_type af{};
                af.type = SPELL_HAZE;
                af.duration = 1; // the spell's default duration
                af.modifier = 1;
                af.location = APPLY_NONE;
                af.bitvector = AFF_HAZE;

                affect_to_char(victim, &af);

                act("You stagger $N with your crushing blow!", FALSE, character, NULL, victim, TO_CHAR);
                act("$n blow crushes into you, causing your world to spin!", FALSE, character, NULL, victim, TO_VICT);
                act("$N staggers from $n's crushing blow, overcome by dizziness!", FALSE, character, 0, victim, TO_NOTVICT, FALSE);
            } else {
                haze->duration = 1; // refresh duration
            }
        }
    }
}

int weapon_master_handler::get_bonus_OB() const
{
    if (spec != game_types::PS_WeaponMaster)
        return 0;

    if (weapon_type == game_types::WT_BLUDGEONING || weapon_type == game_types::WT_BLUDGEONING_TWO || weapon_type == game_types::WT_SMITING) {
        return 10;
    } else if (weapon_type == game_types::WT_SLASHING || weapon_type == game_types::WT_SLASHING_TWO) {
        return 5;
    }

    return 0;
}

int weapon_master_handler::get_bonus_PB() const
{
    if (spec != game_types::PS_WeaponMaster)
        return 0;

    if (weapon_type == game_types::WT_STABBING) {
        return 10;
    } else if (weapon_type == game_types::WT_SLASHING || weapon_type == game_types::WT_SLASHING_TWO) {
        return 5;
    }

    return 0;
}

int weapon_master_handler::append_score_message(char* message_buffer) const
{
    if (spec != game_types::PS_WeaponMaster)
        return 0;

    switch (weapon_type) {
    case game_types::WT_BLUDGEONING:
    case game_types::WT_BLUDGEONING_TWO:
        return sprintf(message_buffer, "Your mastery grants offensive prowess and staggering blows.\r\n");
    case game_types::WT_CLEAVING:
    case game_types::WT_CLEAVING_TWO:
        return sprintf(message_buffer, "Your mastery grants unrivaled power to your blows.\r\n");
    case game_types::WT_FLAILING:
        return sprintf(message_buffer, "Your mastery grants power to your blows and renders shields ineffective.\r\n");
    case game_types::WT_PIERCING:
        return sprintf(message_buffer, "Your mastery grants swiftness to your blows and renders armor useless.\r\n");
    case game_types::WT_SLASHING:
    case game_types::WT_SLASHING_TWO:
        return sprintf(message_buffer, "Your mastery grants balanced prowess and occasional swift strikes.\r\n");
    case game_types::WT_SMITING:
        return sprintf(message_buffer, "Your mastery grants offensive prowess and dazing blows.\r\n");
    case game_types::WT_STABBING:
        return sprintf(message_buffer, "Your mastery grants defensive prowess and armor piercing blows.\r\n");
    case game_types::WT_WHIPPING:
        return sprintf(message_buffer, "Your mastery grants swiftness to your blows and renders armor useless.\r\n");
    default:
        break;
    }

    return 0;
}

} // namespace player_spec