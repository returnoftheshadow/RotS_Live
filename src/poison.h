#pragma once

struct affected_type;
struct caster_snapshot;
struct char_data;

// The identity of the character recorded as the source of a victim's poison. No field is
// meaningful alone; only resolve_poisoner() turns it back into a character. Not persisted.
struct poison_origin {
    int abs_number; // the poisoner's abs_number, -1 when nothing is recorded
    char_data* identity; // the poisoner's address when recorded; compared, never dereferenced
    long registration_serial; // the poisoner's registration_serial when recorded
};

// The live character recorded as the source of `victim`'s poison, or null when the record no
// longer names a live character in the game.
char_data* resolve_poisoner(const char_data& victim);
// Records `poisoner` as the source of `victim`'s poison. A null `poisoner` clears the record.
// `poisoner` is not retained beyond its identity and is never dereferenced later.
void record_poison_origin(char_data* victim, char_data* poisoner);
// Clears `victim`'s poison record.
void clear_poison_origin(char_data* victim);
// Clears `victim`'s poison record once no SPELL_POISON affect remains, at any depth.
void forget_poison_origin_if_cured(char_data* victim);

// The poison mystic poison, black arrow, the snake bite and the room poison cloud apply at
// caster level `level`: -2 STR for `level` + 1 ticks.
affected_type poison_victim_affect_at_level(int level);
// poison_victim_affect_at_level() at `who`'s mystic caster level.
affected_type poison_victim_affect(const caster_snapshot& who);
// The Pale Lady's bite: -4 STR for 24 ticks, the strongest poison.
affected_type pale_lady_poison_affect();
// Poisoned food or drink: no strength malus, for `duration` ticks.
affected_type consumed_poison_affect(int duration);

// True when `victim` resists a poison from `caster`.
char saves_poison(char_data* victim, const caster_snapshot& caster);

// One tick of a running SPELL_POISON affect, after its normal duration decrement: shortens it
// further by any resist-poison affect, syncs that affect's duration to it, then deals the poison
// damage. Returns nonzero when the victim died.
int tick_poison_affect(char_data* victim, affected_type* poison);
// The poison damage one tick deals to `victim`, credited to its resolved poisoner or nobody.
// Returns nonzero when the victim died.
int deal_poison_tick_damage(char_data* victim);

// Removes every SPELL_POISON affect from `victim`, at any depth. Returns true when there was one.
// A poison flag set by worn gear stays.
bool cure_poison(char_data* victim);

// How a resist-poison attempt resolved.
enum class poison_resistance_outcome {
    started, // a resist-poison affect now matches the running poison
    already_resisting, // one was running already; nothing changed
    not_poisoned, // the victim has no poison affect; nothing changed
};
// Starts resisting `victim`'s running poison at `cleric_level`, the resist affect's modifier.
poison_resistance_outcome start_poison_resistance(char_data* victim, int cleric_level);

// Applies poisoned food or drink under the old "longer duration wins" rule, until the poison
// rules replace it.
void apply_consumed_poison(char_data* victim, const affected_type& poison);
