#include "char_utils.h"
#include "utils.h"
#include "warrior_spec_handlers.h"

namespace player_spec {
//============================================================================
// Battle Mage Implementation
//============================================================================
battle_mage_handler::battle_mage_handler(const char_data* in_character)
{
    is_battle_spec = utils::get_specialization(*in_character) == game_types::PS_BattleMage;
    tactics = in_character->specials.tactics;
    mage_level = utils::get_prof_level(PROF_MAGE, *in_character);
    warrior_level = utils::get_prof_level(PROF_WARRIOR, *in_character);
}

int battle_mage_handler::get_bonus_spell_pen(game_types::player_specs spec, int tactics, int mage_level, int spell_pen)
{
    if (spec != game_types::PS_BattleMage) {
        return spell_pen;
    }

    return spell_pen + (tactics / 2) + (mage_level / 12);
}

int battle_mage_handler::get_bonus_spell_power(game_types::player_specs spec, int tactics, int mage_level, int spell_power)
{
    if (spec != game_types::PS_BattleMage) {
        return spell_power;
    }

    return spell_power + (tactics / 2) + (mage_level / 12);
}

// The two member forms below are thin adapters onto the static forms above:
// is_battle_spec is exactly `specialization == PS_BattleMage`, so replaying it
// as a spec keeps the live path byte-identical (TASK-021).
int battle_mage_handler::get_bonus_spell_pen(int spell_pen) const
{
    return get_bonus_spell_pen(is_battle_spec ? game_types::PS_BattleMage : game_types::PS_None,
        tactics, mage_level, spell_pen);
}

int battle_mage_handler::get_bonus_spell_power(int spell_power) const
{
    return get_bonus_spell_power(is_battle_spec ? game_types::PS_BattleMage : game_types::PS_None,
        tactics, mage_level, spell_power);
}

bool battle_mage_handler::can_prepare_spell() const
{
    return !is_battle_spec;
}

bool battle_mage_handler::does_spell_get_interrupted() const
{
    if (!is_battle_spec) {
        return true;
    }

    if (tactics < TACTICS_AGGRESSIVE) {
        return number() > base_chance;
    }

    float warrior_bonus = (float)warrior_level / 100.0f;
    float mage_bonus = (float)mage_level / 100.0f;
    float tactic_bonus = (float)(tactics * 2) / 100.0f;
    float total_bonus = base_chance + warrior_bonus + mage_bonus + tactic_bonus;
    return number() > total_bonus;
}

bool battle_mage_handler::does_mental_attack_interrupt_spell() const
{
    if (!is_battle_spec) {
        return true;
    }

    if (tactics < TACTICS_AGGRESSIVE) {
        return number() > base_chance;
    }

    float mage_bonus = (float)mage_level / 100.0f;
    float tactic_bonus = (float)(tactics * 2) / 100.0f;
    float total_bonus = base_chance + mage_bonus + tactic_bonus;
    return number() > total_bonus;
}

bool battle_mage_handler::does_armor_fail_spell() const
{
    if (!is_battle_spec) {
        return true;
    }

    if (tactics < TACTICS_AGGRESSIVE) {
        return number() > base_chance;
    }

    float tactic_bonus = (float)(tactics * 2) / 100.0f;
    float warrior_bonus = (float)warrior_level / 100.0f;
    float total_bonus = base_chance + tactic_bonus + warrior_bonus;
    return number() > total_bonus;
}
} // namespace player_spec
