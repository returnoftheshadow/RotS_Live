#pragma once
// A cast-time snapshot of everything the combat formulas read from a caster
// (TASK-021). Room affects and poison credit keep this instead of a live
// char_data*: a caster who dies, levels, re-specs or is extracted after the
// cast never changes an active spell, and nothing can dangle. POD on purpose
// (copied by value, may live in pooled storage).
#include "structs.h" // For game_types::player_specs

struct char_data;

struct caster_snapshot {
    // GET_NAME() returns player.short_descr for an NPC ("a battle-scarred
    // orc chieftain") -- routinely well past MAX_NAME_LENGTH (12), the PC
    // player-name limit -- so NPC casters, the common case, need real
    // headroom. 64 is deliberate headroom for NPC short_descr names;
    // snprintf in capture()/none() still bounds every write.
    static constexpr int kNameCapacity = 64;

    int abs_number; // identity for kill credit only; never used to read stats
    char_data* identity_ptr; // the pointer at capture; meaningful only through resolve()
    int level_a; // GET_LEVELA at cast time
    int mage_prof_level; // utils::get_prof_level(PROF_MAGE, caster)
    int cleric_prof_level; // utils::get_prof_level(PROF_CLERIC, caster)
    int intel; // tmpabilities.intel (mage caster level / save DC input)
    int wil; // tmpabilities.wil (mystic caster level input)
    int perception; // GET_PERCEPTION value (saves_poison offence input)
    int willpower; // GET_WILLPOWER value (saves_poison offence input)
    int spell_power; // points.spell_power (battle_mage_handler bonus input)
    int spell_pen; // points.spell_pen (save DC / spell penetration input)
    int tactics; // specials.tactics (battle_mage_handler spell-power/pen bonus input)
    game_types::player_specs specialization; // utils::get_specialization
    int race; // GET_RACE (other_side / friendly-fire / max-race-prof inputs)
    bool is_npc; // IS_NPC (other_side, spell penetration, get_prof_level inputs)
    bool is_charmed; // IS_AFFECTED(AFF_CHARM) (other_side / spell pen inputs)
    bool is_pc_for_spell_pen; // should_apply_spell_penetration() at capture
    int master_mage_prof_level; // charmed NPC's master PROF_MAGE level (get_spell_pen_value), else 0
    char name[kNameCapacity]; // display name for messages when the caster is gone

    static caster_snapshot capture(const char_data& caster);
    static caster_snapshot none();
    bool is_none() const { return abs_number < 0; }
    bool same_character_as(const char_data& ch) const;
    char_data* resolve() const;
};
