# Gotchas

Server behaviours that have already cost a scenario a fix round. Each entry names where the
behaviour lives so you can confirm it still holds.

## Timing

- **The fast block never stops.** `comm.cpp`'s three-second block runs in harness mode and
  regenerates every character's hit points toward max, PC and NPC alike. A value floored
  before a loop drifts up between iterations; re-floor it with `wizset <name> hit N` on
  every iteration (`blaze_support.tick_until_marker` does this).
- **Slow affects age only on their phase.** A person affect's duration drops once per
  matching `time_phase` (about a minute) or on `harness affects`; room affects spend their
  duration on every sweep, including the one `harness affects` triggers.
- **Commands from different connections are not ordered by send time.** `game_loop`
  executes one command per descriptor per pulse in `descriptor_list` order
  (`comm.cpp` ~930-972). The imp's `stat` sent right after a victim's `kill` can be
  processed first. Poll with `combat_support.wait_for_engagement` rather than trusting one
  reply.
- **A `look` sent before a `transfer` lands reads the old room.** Use `expect_room`.
- **A death sets mana to zero on both punishment branches** (`fight.cpp` die paths), so
  mana cannot tell a gentle death from a harsh one; hit points and stats can.
- **While a fight runs in the imp's own room, `command()` can return before the command it
  just sent has executed.** Every violence pulse ends a combat broadcast with a prompt
  (`session.py`'s `ends_with_prompt`), and `command()`'s wait is satisfied by *any* prompt, not
  specifically the reply to what it sent; a stray combat-broadcast prompt can land in that
  window and let `command()` return while the actual command is still queued behind it on the
  server. Seen on CI (run 35655894942, kept run `build/integration/4ba60d7e87b2`): with the
  caster meleeing a splash-engaged bystander, `imp.command("load mob 1130")` returned on a
  stray prompt before the load had executed, so a `kill target` issued right after it reached
  the server first and got "They aren't here." Send the command with `send_line` and `expect`
  on its own reply marker instead once a fight may be running in that room.

## Wizard commands

- **`wizset <name> level` sets only `player.level`.** Profession levels
  (`profs->prof_level[]`), which drive spell duration through `get_mage_caster_level()`,
  are untouched; `wizset`'s `prof` field is a no-op (`act_wiz.cpp`, `do_wizset`). To
  change a profession level, change the roster spec.
- **`wizset <name> maxhit N` raises a PC's maximum, not the current value.** It writes
  `constabilities.hit` and calls `affect_total()`, whose `recalc_abilities()` adds
  `constabilities.hit * CON / 20` (about 1100 at CON 11 for `maxhit 2000`). Follow it with
  `restore` to heal to the new max. For an NPC it does nothing: `recalc_abilities` is
  skipped.
- **`restore` sets position standing** for a character with positive hit and no fight;
  `transfer` does not. Transfer first, restore second.
- **`purge <mob>` and `load mob <vnum>`** are the way to reset a mob between attempts; there
  is no command that strips a room affect, which is why each test boots its own server.
- **`wizset <name> room` resolves to `roomflag` by prefix match**, not the `room` field: the
  `fields[]` table lists `roomflag` before `room` and `do_wizset`'s lookup takes the first
  prefix match (`act_wiz.cpp:2766-2767`, field order). Use `teleport <name> <vnum>` instead.

## Characters and rendering

- **A magus renders as `*an Uruk*` to non-magus observers** (`pc_star_types[RACE_MAGUS]`).
  Harnmage's name never appears in Harnfighter's or Harnvictim's transcripts for `act()`
  messages; match the star form or use a human caster (Harncaller).
- **`quit` is refused while `SPELL_ANGER` lingers** (`do_quit`, `act_othe.cpp`). A character
  that was attacked cannot quit until the anger expires; `drop_link()` (link-dead) is not
  refused.
- **Summon fails across sides.** `other_side()` (`handler.cpp`) places a magus opposite a
  wood elf; Harncaller (human) exists so summon has a same-side caster.
- **Big Brother makes a three-times-lower-level player an impossible damage target**, and a
  god an impossible one at any level. `damage_credited` asks `big_brother::is_target_valid`
  before anything else (`fight.cpp:1847-1849`), sends the attacker "...protecting your target.
  Your hand is stayed." and returns 0 without reaching either `set_fighting` call, so no
  engagement happens either. The band is `attacker_level >= defender_level * 3` (or the
  reverse) on `is_level_range_appropriate` (`big_brother.cpp:381-394`), reached from
  `is_target_valid` at `:315-316`, and `victim->player.level >= LEVEL_MINIMM` at `:303` for a
  god. On the roster that is Harncaller (30) against Harnvictim (10) and against Harnimp
  (100), but not against Harnfighter (20). Pick the partner accordingly: it decides whether an
  area spell can splash your own bystander player into PvP with the caster.
- **A mob special needs `MOB_SPEC` as well as the program number.** The harness snake
  (`mob/11.mob` #1131) carries act flags `3`; without the flag the program is never bound and
  the mob never bites. `tests/integration/unit/test_world.py` pins this.
- **Exit directions:** `D0` north, `D1` east, `D2` south, `D3` west, `D4` up, `D5` down
  (`src/structs.h`).

## Credit and records

- **No exploit record is written for a mob's death.** `die()` returns through `raw_kill()`
  for an NPC before any `add_exploit_record` call. Kill credit for a mob kill is observable
  only through `group_gain()`'s XP share line, which goes to the present credited killer
  and to anyone still fighting the mob, so an engaged fighter and an unengaged present
  caster both see it.
- **Records are read from disk.** `records.read_exploits` sees a record only after the
  server saved the character (death, quit, `save`).
- **The crash monitor attributes a report to a test only if it lands in `game.log` before
  that test's teardown check.** A report raised during the SIGTERM save path is not seen.

## Process

- **Bound every retry loop by the mechanism's duration.** A blaze lasts the caster's mage
  level plus an intelligence factor in ticks (about 36 for Harnmage); a loop that spends two
  ticks per attempt reaches only a fraction of ten attempts. State the bound in the
  docstring and fail explicitly when the affect is gone.
- **Find markers in a kept transcript, then cite the source.** `ROTS_IT_KEEP=1`, run once,
  read `build/integration/<run-id>/<name>.txt`, then grep `src/` for the string.
- **Delete only your own kept run directories.** Others under `build/integration/` are
  someone else's evidence.
