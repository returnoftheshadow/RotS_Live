// The rules of a character's poison: which poison each source applies, whether it lands, who owns
// it, how it hurts each tick, and how it ends early.
#include "poison.h"

#include "caster_snapshot.h"
#include "handler.h"
#include "spells.h"
#include "structs.h"
#include "utils.h"

#include <algorithm>

namespace {

// The damage one poison tick deals, whether a SPELL_POISON affect or worn gear's flag drives it.
constexpr int kPoisonTickDamage = 5;

// The defense a wood elf adds to its poison save.
constexpr int kWoodElfPoisonBonus = 30;

} // namespace

// The recorded address is only compared, never dereferenced: the character it named may have been
// extracted and freed. char_by_abs_number() names whoever holds the slot now, and only the same
// address under the same registration serial counts. An extracted poisoner, a slot recycled by a
// different character and a re-registration at the old address therefore all resolve to null, as
// does a poisoner parked at the character menu, which is out of the game.
char_data* resolve_poisoner(const char_data& victim) {
    const poison_origin& origin = victim.specials.poisoned_by;
    if (origin.abs_number < 0 || origin.identity == nullptr) {
        return nullptr;
    }
    char_data* const slot_holder = char_by_abs_number(origin.abs_number);
    if (slot_holder != nullptr && slot_holder == origin.identity &&
        slot_holder->registration_serial == origin.registration_serial &&
        character_in_game(slot_holder)) {
        return slot_holder;
    }
    return nullptr;
}

// Every field is written at once: a slot left standing without its address, or the reverse, could
// answer for whoever holds that slot today.
void record_poison_origin(char_data* victim, char_data* poisoner) {
    if (poisoner == nullptr) {
        clear_poison_origin(victim);
        return;
    }
    victim->specials.poisoned_by.abs_number = poisoner->abs_number;
    victim->specials.poisoned_by.identity = poisoner;
    victim->specials.poisoned_by.registration_serial = poisoner->registration_serial;
}

void clear_poison_origin(char_data* victim) {
    victim->specials.poisoned_by.abs_number = -1;
    victim->specials.poisoned_by.identity = nullptr;
    victim->specials.poisoned_by.registration_serial = 0;
}

// Only a SPELL_POISON affect owns the record, so a poison flag from worn gear does not keep it.
void forget_poison_origin_if_cured(char_data* victim) {
    if (get_affect_unbounded(victim, SPELL_POISON) == nullptr) {
        clear_poison_origin(victim);
    }
}

affected_type poison_victim_affect_at_level(int level) {
    affected_type poison{};
    poison.type = SPELL_POISON;
    poison.duration = level + 1;
    poison.modifier = -2;
    poison.location = APPLY_STR;
    poison.bitvector = AFF_POISON;
    return poison;
}

affected_type poison_victim_affect(const caster_snapshot& who) {
    return poison_victim_affect_at_level(get_mystic_caster_level(who));
}

affected_type pale_lady_poison_affect() {
    affected_type poison{};
    poison.type = SPELL_POISON;
    poison.duration = 24;
    poison.modifier = -4;
    poison.location = APPLY_STR;
    poison.bitvector = AFF_POISON;
    return poison;
}

affected_type consumed_poison_affect(int duration) {
    affected_type poison{};
    poison.type = SPELL_POISON;
    poison.duration = duration;
    poison.modifier = 0;
    poison.location = APPLY_NONE;
    poison.bitvector = AFF_POISON;
    return poison;
}

// The caster's willpower and perception attack; the victim's constitution and willpower defend.
char saves_poison(char_data* victim, const caster_snapshot& caster) {
    // Wood elves resist disease well, which their low constitution would otherwise hide.
    int wood_elf_bonus = 0;
    if (GET_RACE(victim) == RACE_WOOD) {
        wood_elf_bonus = kWoodElfPoisonBonus;
    }
    const int offence = ((caster.willpower * 8) * caster.perception) / 100;
    const int defense = (GET_CON(victim) * 5) + (GET_WILLPOWER(victim) * 3) + wood_elf_bonus;

    return (number(offence / 3, offence) < number(defense / 2, defense));
}

// The first resist-poison affect within affected_by_spell()'s reach is the one that shortens it.
int tick_poison_affect(char_data* victim, affected_type* poison) {
    affected_type* const resistance = affected_by_spell(victim, SPELL_RESIST_POISON);
    if (resistance != nullptr) {
        poison->duration = std::max(poison->duration - resistance->modifier, 0);
        resistance->duration = poison->duration;
    }
    return deal_poison_tick_damage(victim);
}

// The victim engages only itself, so nobody else is drawn into a fight; the kill goes to the
// resolved poisoner, or to nobody once that character is gone.
int deal_poison_tick_damage(char_data* victim) {
    char_data* const poisoner = resolve_poisoner(*victim);
    return damage_credited(victim, victim, poisoner, kPoisonTickDamage, SPELL_POISON, 0);
}

bool cure_poison(char_data* victim) {
    bool cured = false;
    affected_type* affect = victim->affected;
    while (affect != nullptr) {
        // Read before the removal returns this affect to the pool; affect_remove() touches no
        // other entry.
        affected_type* const next_affect = affect->next;
        if (affect->type == SPELL_POISON) {
            affect_remove(victim, affect);
            cured = true;
        }
        affect = next_affect;
    }
    return cured;
}

// Both lookups stop after MAX_AFFECT entries, as the resist-poison tick's does.
poison_resistance_outcome start_poison_resistance(char_data* victim, int cleric_level) {
    const affected_type* const poison = affected_by_spell(victim, SPELL_POISON);
    if (poison == nullptr) {
        return poison_resistance_outcome::not_poisoned;
    }
    if (affected_by_spell(victim, SPELL_RESIST_POISON) != nullptr) {
        return poison_resistance_outcome::already_resisting;
    }

    affected_type resistance{};
    resistance.type = SPELL_RESIST_POISON;
    resistance.duration = poison->duration;
    resistance.modifier = cleric_level;
    resistance.location = APPLY_NONE;
    resistance.bitvector = 0;
    affect_to_char(victim, &resistance);
    return poison_resistance_outcome::started;
}

// A consumed poison no longer than the running one changes nothing, and the running poison keeps
// its poisoner. A longer one replaces it and clears the record, since nobody owns it.
void apply_consumed_poison(char_data* victim, const affected_type& poison) {
    const affected_type* running = affected_by_spell(victim, SPELL_POISON);
    if (running != nullptr && running->duration >= poison.duration) {
        return;
    }
    if (running != nullptr) {
        affect_from_char(victim, SPELL_POISON);
    }
    affected_type consumed = poison;
    affect_to_char(victim, &consumed);
    record_poison_origin(victim, nullptr);
}
