// The rules of a character's poison: which poison each source applies, whether it lands, how it
// merges with a running poison, who owns it, how it hurts each tick, and how it ends early.
#include "poison.h"

#include "caster_snapshot.h"
#include "comm.h"
#include "handler.h"
#include "poison_origin.h"
#include "spells.h"
#include "structs.h"
#include "utils.h"

#include <algorithm>

namespace {

// The damage one poison tick deals, whether a SPELL_POISON affect or worn gear's flag drives it.
constexpr int kPoisonTickDamage = 5;

// The defense a wood elf adds to its poison save.
constexpr int kWoodElfPoisonBonus = 30;

// The running poison's initial duration, which apply_poison() keeps in its counter. A counter
// below the remaining duration (zero, or a poison saved before counters meant this) falls back to
// the remaining duration, so an extension can never shorten a poison.
int initial_duration_of(const affected_type& running) {
    return std::max<int>(running.counter, running.duration);
}

// Starts `poison` in place of any running poison and records `source` as its poisoner.
poison_outcome start_poison(char_data* victim, const affected_type& poison, const char_data* source,
                            poison_outcome outcome) {
    cure_poison(victim);
    affected_type fresh = poison;
    fresh.counter = static_cast<sh_int>(poison.duration);
    affect_to_char(victim, &fresh);
    record_poison_origin(victim, source);
    return outcome;
}

// An extension keeps a poisoner who still resolves; otherwise `source` takes the record.
void hand_over_record_if_poisoner_gone(char_data* victim, const char_data* source) {
    if (resolve_poisoner(*victim) == nullptr) {
        record_poison_origin(victim, source);
    }
}

} // namespace

// Only a SPELL_POISON affect owns the record, so a poison flag from worn gear does not keep it.
void forget_poison_origin_if_cured(char_data* victim) {
    if (victim == nullptr) {
        log("SYSERR: forget_poison_origin_if_cured: null victim");
        return;
    }
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

affected_type poison_victim_affect(const caster_snapshot& caster) {
    const int caster_level = get_mystic_caster_level(caster);
    return poison_victim_affect_at_level(caster_level);
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
bool saves_poison(const char_data* victim, const caster_snapshot& caster) {
    if (victim == nullptr) {
        log("SYSERR: saves_poison: null victim");
        return true;
    }
    // Wood elves resist disease well, which their low constitution would otherwise hide.
    int wood_elf_bonus = 0;
    if (GET_RACE(victim) == RACE_WOOD) {
        wood_elf_bonus = kWoodElfPoisonBonus;
    }
    const int offence = ((caster.willpower * 8) * caster.perception) / 100;
    const int defense = (GET_CON(victim) * 5) + (GET_WILLPOWER(victim) * 3) + wood_elf_bonus;

    // Rolled as separate statements so the offence roll always draws first.
    const int offence_roll = number(offence / 3, offence);
    const int defense_roll = number(defense / 2, defense);
    return offence_roll < defense_roll;
}

// The first resist-poison affect within affected_by_spell()'s reach is the one that shortens it.
bool tick_poison_affect(char_data* victim, affected_type* poison) {
    if (victim == nullptr) {
        log("SYSERR: tick_poison_affect: null victim");
        return false;
    }
    if (poison == nullptr) {
        log("SYSERR: tick_poison_affect: null poison");
        return false;
    }
    affected_type* const resistance = affected_by_spell(victim, SPELL_RESIST_POISON);
    if (resistance != nullptr) {
        poison->duration = std::max(poison->duration - resistance->modifier, 0);
        resistance->duration = poison->duration;
    }
    const int victim_died = deal_poison_tick_damage(victim);
    return victim_died != 0;
}

// The victim engages only itself, so nobody else is drawn into a fight; the kill goes to the
// resolved poisoner, or to nobody once that character is gone.
int deal_poison_tick_damage(char_data* victim) {
    if (victim == nullptr) {
        log("SYSERR: deal_poison_tick_damage: null victim");
        return 0;
    }
    char_data* const poisoner = resolve_poisoner(*victim);
    return damage_credited(victim, victim, poisoner, kPoisonTickDamage, SPELL_POISON, 0);
}

bool cure_poison(char_data* victim) {
    if (victim == nullptr) {
        log("SYSERR: cure_poison: null victim");
        return false;
    }
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

int poison_strength(const affected_type& poison) {
    if (poison.location != APPLY_STR || poison.modifier >= 0) {
        return 0;
    }
    return -poison.modifier;
}

poison_outcome apply_poison(char_data* victim, const affected_type& poison,
                            const char_data* source) {
    if (victim == nullptr) {
        log("SYSERR: apply_poison: null victim");
        return poison_outcome::not_applied;
    }
    affected_type* const running = get_affect_unbounded(victim, SPELL_POISON);
    if (running == nullptr) {
        return start_poison(victim, poison, source, poison_outcome::applied);
    }

    const int running_strength = poison_strength(*running);
    const int new_strength = poison_strength(poison);
    if (new_strength < running_strength) {
        return poison_outcome::blocked_by_stronger;
    }
    if (new_strength > running_strength) {
        return start_poison(victim, poison, source, poison_outcome::replaced);
    }

    // A permanent poison has no initial duration to cap at, and nothing an equal poison could add
    // to its duration.
    if (running->duration < 0) {
        hand_over_record_if_poisoner_gone(victim, source);
        return poison_outcome::extended;
    }

    const int initial_duration = initial_duration_of(*running);
    if (poison.duration > initial_duration) {
        return start_poison(victim, poison, source, poison_outcome::replaced);
    }

    running->duration = std::min(running->duration + poison.duration / 2, initial_duration);
    running->counter = static_cast<sh_int>(initial_duration);
    hand_over_record_if_poisoner_gone(victim, source);
    return poison_outcome::extended;
}

void send_poison_outcome_messages(poison_outcome outcome, char_data* victim, char_data* caster,
                                  const char* fresh_victim_line) {
    if (victim == nullptr) {
        log("SYSERR: send_poison_outcome_messages: null victim");
        return;
    }
    switch (outcome) {
    case poison_outcome::applied:
    case poison_outcome::replaced:
        if (fresh_victim_line != nullptr) {
            send_to_char(fresh_victim_line, victim);
        }
        break;
    case poison_outcome::extended:
        send_to_char("You feel sicker as the poison lingers in your blood.\n\r", victim);
        break;
    case poison_outcome::blocked_by_stronger:
        send_to_char("Your body is already fighting a stronger poison.\n\r", victim);
        if (caster != nullptr) {
            act("$N is already suffering from a stronger poison.", FALSE, caster, nullptr, victim,
                TO_CHAR);
        }
        break;
    case poison_outcome::not_applied:
        break;
    }
}

// Both lookups stop after MAX_AFFECT entries, as the resist-poison tick's does.
poison_resistance_outcome start_poison_resistance(char_data* victim, int cleric_level) {
    if (victim == nullptr) {
        log("SYSERR: start_poison_resistance: null victim");
        return poison_resistance_outcome::not_poisoned;
    }
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
    resistance.modifier = static_cast<sh_int>(cleric_level);
    resistance.location = APPLY_NONE;
    resistance.bitvector = 0;
    affect_to_char(victim, &resistance);
    return poison_resistance_outcome::started;
}
