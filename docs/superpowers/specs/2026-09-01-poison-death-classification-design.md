# Poison-Death Classification — Design

**Date:** 2026-09-01 · **Owner:** drelidan · **Branch:** `fix/spell-room-affect-uaf-port`
**Status:** approved design, pending implementation plan

## Problem

The spell-port branch made poison ticks credit the resolved poisoner
(`resolve_poisoner()`, TASK-021). A side effect: a mob-sourced poison death now punishes as a
full mob death — full `base_xp_gain` XP loss plus the harsh 2/3-stat penalty — even when the
victim died alone, unengaged, rooms away from the mob. The legacy system never did this.

Legacy behavior (verified against `release-frodo`, not from memory):

- The legacy poison tick called `damage(i, i, 5, SPELL_POISON, 0)` — the victim was its own
  killer — so `IS_NPC(killer)` was false for every poison death and the full mob-death XP loss
  **never** applied, engaged or not. Only the unconditional `min(0, base_xp_gain / 10)` loss ran.
- Legacy `raw_kill()` computed `died_to_player = attack_type == SPELL_POISON || ...` — the
  short-circuit made **every** poison death gentle (revive at hp/4, mana 0, no stat loss).
- The only thing legacy gated on engagement was record-keeping: the
  `if (attack_type == SPELL_POISON) { ... if (fighting == NULL) { raw_kill(); return; } }`
  early-out skipped pkill/EXPLOIT_PK/EXPLOIT_DEATH and the hunger/thirst resets for
  out-of-combat poison deaths.
- `RotS_Live_Modern` (the port source) contains no carveout to reuse — it removed the poison
  special-casing deliberately (its comments argue "being in combat was never the right
  question" for *attribution*; this design re-answers the question for *punishment*).

## Ruling (owner, 2026-09-01)

Punishment for a PC dying to a `SPELL_POISON` tick is decided by **engagement with a real mob
at the instant of death**, source-agnostic. Attribution is a separate concern and stays as the
port built it.

1. **Engagement** = the victim is fighting a real mob OR a real mob is fighting the victim
   (either direction; visibility irrelevant). **Real mob** = NPC that is neither `MOB_PET` nor
   `MOB_ORC_FRIEND` — player-controlled NPCs are player-combat context.
2. **Engaged → mob death**: full mob-death XP loss + harsh penalty, regardless of who poisoned
   (mob, player, or unresolvable). The poisoning player, if any, keeps PK/kill records.
3. **Unengaged → full legacy treatment**: `/10` XP loss only + gentle penalty, regardless of
   source — including a mob poisoner and a null (unresolvable) killer.
4. **Scope: `SPELL_POISON` deaths only.** Blaze/haze damage ticks keep the port's behavior.
   Mist-applied poison inherits the carveout: mist applies ordinary `SPELL_POISON`, and `die()`
   neither can nor should distinguish its origin.
5. Non-poison deaths are byte-for-byte unchanged.

## Design

### Classification, extracted as pure functions

All decision logic lives in six pure free functions plus one enum, declared in
`src/handler.h`'s existing "prototypes from fight.c" block (the `resolve_poisoner()` /
`damage_credited()` precedent) and defined in `src/fight.cpp` beside `kill_contributors()`:

- `enum class death_punishment { legacy, mob_death, player_death }` — `legacy` means "decide
  from the credited killer exactly as die()/raw_kill() always have"; the two poison values
  override the killer-based rules.
- `bool is_real_mob(const char_data*)` — NPC acting for itself (not pet, not orc-friend);
  null and players answer false. (`IS_NPC` null-checks — `src/utils.h:209`.)
- `char_data* find_engaged_real_mob(char_data* victim, char_data* engaged_opponent)` — the
  real mob the victim counts as engaged with, or null. Checks `engaged_opponent` (the victim's
  own target, captured before `stop_fighting()`) first, then walks `combat_list` for a real
  mob fighting the victim. No `CAN_SEE` check: an unseen attacker still engages.
- `death_punishment classify_pc_death(int attack_type, bool engaged_with_real_mob)` — only
  `SPELL_POISON` classifies away from `legacy`.
- `bool death_takes_full_mob_xp_loss(const char_data* killer, death_punishment)` — the
  `legacy` arm reproduces die()'s old `IS_NPC(killer) && !pet && !orc-friend` branch verbatim.
- `bool death_counts_as_player_kill(const char_data* killer, death_punishment)` — the
  `legacy` arm reproduces raw_kill()'s old `killer != NULL && !IS_NPC(killer)` verbatim.
- `char_data* mobdeath_record_mob(char_data* killer, char_data* engaged_mob, death_punishment)`
  — the NPC an `EXPLOIT_MOBDEATH` record names, or null for "no record".

Why extraction: the gtest harness cannot drive a PC-victim death end-to-end (documented in
`src/tests/fight_credit_tests.cpp`'s banner — `free_char()`'s string RELEASE and
`write_exploits()`'s real file write are unexercised for PC victims), so the decision tables
must be pinnable directly.

### Plumbing: 4-arg forms with 3-arg forwarders

`die()` gains `char_data* engaged_opponent`; `raw_kill()` gains `death_punishment punishment`.
Both keep their 3-arg signatures as thin forwarders (`nullptr` / `legacy`), the repo's own
`damage()` → `damage_credited()` pattern. Forwarders rather than default arguments because the
3-arg externs re-declared locally in seven other TUs (act_othe, clerics, limits, act_offe,
act_move, handler, script, spec_pro) would fail to link against a re-mangled defaulted
signature; with forwarders, zero files outside `fight.cpp` change.

`damage_credited()` passes its already-captured `engaged_opponent` into `die()` (a one-token
change at the call site). The capture happens before `stop_fighting(victim)`, and between
capture and `die()` nothing mutates any *other* character's `specials.fighting`, so `die()`'s
deferred `combat_list` walk sees the true engagement state. The stale comment claiming
stop_fighting "clears" the pointer (it actually retargets) gets corrected in passing.

### die() PC-region restructure

Order preserved from today (MOBDEATH record → `/10` loss → POISON record → contributor/PK
block → full mob loss → cond resets → raw_kill), with three semantic changes:

- `EXPLOIT_MOBDEATH` via `mobdeath_record_mob()`: recorded iff the death classifies as a mob
  death, naming the real-mob killer when there is one, else the engaged mob (deterministic:
  `find_engaged_real_mob` prefers the victim's own target). Suppressed for unengaged poison
  deaths even when the poisoner is a mob — the player-visible record must agree with the
  penalty applied.
- `EXPLOIT_POISON` recorded for **every** PC poison death — the current killer-null arm
  records nothing; legacy always recorded it.
- The full-loss branch and raw_kill's gentle/harsh arm consult the classification selectors.

The contributor/PK/EXPLOIT_DEATH block is untouched and still gated on `if (killer)`.

### Behavior matrix (PC victim, SPELL_POISON; Δ = change vs current branch)

| Poison source (credited killer) | Engaged | Full mob XP loss | Penalty | EXPLOIT_MOBDEATH |
|---|---|---|---|---|
| Real mob | yes | yes | harsh | yes (names killer) |
| Real mob | no | **no Δ** | **gentle Δ** | **suppressed Δ** |
| Player | yes | **yes Δ** | **harsh Δ** | **yes Δ (names engaged mob)**; poisoner keeps PK records |
| Player | no | no | gentle | no |
| null (unresolvable) | yes | **yes Δ** | harsh | **yes Δ (names engaged mob)** |
| null (unresolvable) | no | no | **gentle Δ** | no |

All rows: `/10` loss applies and `EXPLOIT_POISON` is recorded (Δ new for the null-killer arm).
Non-poison deaths and NPC victims: identical to today in every cell.

## Testing

New TU `src/tests/death_classification_tests.cpp` (registered in `src/CMakeLists.txt` and
`src/tests/Makefile`): ~15 pins across five suites driving the pure functions with stack
`char_data{}` fixtures and a local `CombatListGuard` copy — no rooms, no death pipeline.
Coverage: `is_real_mob` truth table; both engagement directions plus pet/player/other-fighter
skips and opponent-over-list preference; classification table; both selectors' legacy arms
byte-compatible with the removed expressions (including null-killer harsh and
poison-term-stays-gone); MOBDEATH naming/suppression table.

Existing suites that must stay green: `FightCredit*`, `GroupGain*`, `Damage*`, `PoisonOrigin*`,
`RoomAffectTick*`, `KillContributor*`. Full gate: the Docker i386 container suite
(`scripts/rots-docker.sh test`); the server x86_64-host full run has a known pre-existing
order-dependent segfault (`AffectUpdateWalk`) and is not a gate.

Manual verification on port 4810 (added to `manual-test-plan.md`): flee-and-die-alone →
gentle + small loss + POISON record only; die swinging at the poisoning mob → harsh + full
loss + MOBDEATH; player-poisoned while fighting a mob → harsh + full loss, poisoner keeps PK
record.

## Out of scope

- Blaze/haze/mist *damage* tick classification (they stay `legacy`).
- Any change to `kill_contributors()`, `pkill_create()`, `group_gain()`, `resolve_poisoner()`,
  the limits.cpp tick call sites, or `room_affect_tick.cpp`.
- The legacy early-out's skipped hunger/thirst resets (the port already restored them
  unconditionally; nobody asked for that quirk back).
