#pragma once
#include "structs.h" // For game_types::player_specs

struct char_data;

// A cast-time copy of everything the combat formulas read from a caster, so a
// spell that resolves later uses the caster's state AT THE CAST and never
// touches the character again: a caster who dies, levels, re-specs or is
// extracted afterwards cannot change an active spell, and nothing can dangle.
// POD: copied by value, may live in pooled storage.
struct caster_snapshot {
    // Size of `name`. GET_NAME() returns player.short_descr for an NPC ("a
    // battle-scarred orc chieftain"), routinely far past MAX_NAME_LENGTH (12),
    // so NPC casters -- the common case -- need the headroom.
    static constexpr int kNameCapacity = 64;

    int abs_number; // identity for kill credit only; never used to read stats
    char_data* identity_ptr; // the pointer at capture; meaningful only through resolve()
    long identity_serial; // registration_serial at capture; resolve() requires the slot's owner to still carry it
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

    // A snapshot of `caster` as it stands now.
    static caster_snapshot capture(const char_data& caster);

    // The snapshot that names nobody: is_none() holds and resolve() is null.
    static caster_snapshot none();

    // True when this snapshot names no caster at all.
    bool is_none() const { return abs_number < 0; }

    // True when `ch` is the very character this snapshot was captured from --
    // not merely a character that has since taken over the same abs_number.
    bool same_character_as(const char_data& ch) const;

    // The captured character if it is still in the game (registered under the
    // same serial and standing in a room), else null. Never dereferences the
    // captured pointer, so an extracted or slot-recycled caster resolves to null
    // rather than dangling.
    char_data* resolve() const;
};
