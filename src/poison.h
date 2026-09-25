#pragma once

struct affected_type;
struct caster_snapshot;
struct char_data;

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

// How one poison application resolved against the poison already running on the victim.
enum class poison_outcome {
    applied, // the victim had no poison; this one now runs
    replaced, // it displaced a weaker poison, or an equal one it outlasts
    extended, // an equal poison was running; its duration grew, up to its initial duration
    blocked_by_stronger, // a stronger poison is running; nothing changed
};

// The size of `poison`'s strength malus: 4 for a -4 STR poison, 0 for one with no strength malus.
int poison_strength(const affected_type& poison);

// Applies `poison` (a SPELL_POISON affect) to `victim` and records `source` as the poisoner
// when the poison starts or is replaced. A null `source` means nobody. A weaker poison than the
// running one changes nothing. A stronger one, or an equal one lasting longer than the running
// poison's initial duration, replaces it. Otherwise an equal one extends the running poison by
// half its duration, capped at that initial duration; an equal one leaves a permanent poison's
// duration alone. Either extension takes over the record only when the recorded poisoner no longer
// resolves. Sends no messages.
poison_outcome apply_poison(char_data* victim, const affected_type& poison, char_data* source);

// Tells `victim` how a poison application resolved. Applied or replaced sends
// `fresh_victim_line`, the source's own line, when it is not null. Extended or refused sends the
// shared merge line, and a refusal also tells `caster` when it is not null.
void send_poison_outcome_messages(poison_outcome outcome, char_data* victim, char_data* caster,
                                  const char* fresh_victim_line);

// How a resist-poison attempt resolved.
enum class poison_resistance_outcome {
    started, // a resist-poison affect now matches the running poison
    already_resisting, // one was running already; nothing changed
    not_poisoned, // the victim has no poison affect; nothing changed
};
// Starts resisting `victim`'s running poison at `cleric_level`, the resist affect's modifier.
poison_resistance_outcome start_poison_resistance(char_data* victim, int cleric_level);
