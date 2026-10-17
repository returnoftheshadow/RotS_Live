# Idle, the void, and what happens to followers and mounts

**Status:** ✅ current behaviour, observed on a running server 2026-09-14 and cross-checked against
the code. **This is historic functionality and is not to be changed** without an explicit decision —
it is recorded here so a fix elsewhere (object saves, account storage) does not alter it by accident.

**Locked in by** `src/tests/idle_follower_tests.cpp` (`IdleFollowersTest`), which calls `check_idling`
directly for both thresholds and asserts every row of the tables below. A failure there means this
behaviour changed.

**Source files:** `src/limits.cpp` (`check_idling:523`), `src/handler.cpp` (`stop_follower:983`,
`die_follower:1088`, `stop_riding:2526`), `src/objsave.cpp` (`Crash_follower_save:873`,
`Crash_follower_load:949`, `Crash_crashsave:1286`, `Crash_idlesave:1329`, `Crash_save_all:1869`),
`src/ranger.cpp` (`do_ride:80`, `do_dismount:204`, `do_tame:1460`), `src/act_othe.cpp`
(`do_recruit:177`).

## The idle timer

`check_idling` runs once per mud hour (60 real seconds) from `point_update`, and
`specials.timer` counts those ticks since the player's last command. Immortals of level `LEVEL_GOD`
and above only get the AFK flag and are never moved or disconnected (`limits.cpp:527-534`).

| Timer | What happens (`limits.cpp`) |
|---|---|
| ≥ 3 | Flagged AFK: *"You have been idle, and are now flagged as AFK."* The room shows `(AFK) (holy protection)`. |
| > 8 | **Pulled into the void** (`:550-567`): fighting stops; **`stop_riding`**; *"You have been idle, and are pulled into a void."*; `save_char` + `Crash_crashsave` while still in the room; moved to the race's idle room (`mortal_idle_room`, `consts.cpp:2508`: 1102 for the free peoples, 1191 common orc, 1190 uruk-hai, 1192 uruk-lhuth/haradrim, 1129 olog-hai). The previous room is kept in `was_in_room`. |
| > 28 | **Idle disconnect** (`:569-597`): moved back to `was_in_room`, `save_char` there, **`Crash_idlesave`** (rent code 5, `RENT_TIMEDOUT`), carried and worn objects extracted, socket closed, **`extract_char`**. Mudlog: *"X force-rented and extracted (idle)."* |

## Followers and mounts, stage by stage

Observed with a human ranger (a tamed wolf cub, one ridden horse, one following horse) and a common
orc (a recruited snaga, one ridden warg, one following warg). A "following" mount is one that was
ridden and then dismounted: `do_dismount` re-adds it as a follower (`ranger.cpp:227-228`).

| | Before idling | In the void (> 8) | Idle disconnect (> 28) | Next login (after a reboot) |
|---|---|---|---|---|
| **Ridden mount** | ridden; not a follower | *"You stop riding X."* — released: no master, stays in the old room as an ordinary mob | (already gone) | not restored |
| **Following mount** | follows | **still follows**, stays in the old room | released: no master, stays in the room | not restored |
| **Tamed animal** | follows; `CHARM`, `tame` affect, `IS_PET` | **still follows**, keeps charm | released: `CHARM`, `tame` and `IS_PET` removed, stays in the room | not restored |
| **Recruited orc** | follows; `CHARM`, `recruit` affect, `IS_PET` | **still follows**, keeps charm | released: `CHARM`, `recruit` and `IS_PET` removed, stays in the room | not restored |
| **The player** | in the room | in the idle room | back in the old room, then extracted | enters in the room they idled out of, with no followers, no mount, no group: *"You return to your keyboard."* |

Why the disconnect releases everything: `extract_char` calls `die_follower`, which runs
`stop_follower(…, FOLLOW_MOVE)` on each follower (`handler.cpp:983-1040`) — charmed ones get
*"You realize that X is a jerk!"* and lose `CHARM` and their tame/recruit affect. The mobs are not
extracted, unlike rent (`Crash_rentsave` saves them and then `extract_followers`, so they return).

Why nothing comes back on the next login: every save made while the player is in the void holds no
followers, and the final idle save writes no follower section at all.

## What is written to disk

| Moment | Object save |
|---|---|
| Before idling (a `save`) | Followers written, each with a `flag_config` (`objsave.cpp:43-46`: 0 mount, 1 recruited orc, 2 tamed, 3 guardian). The ridden mount is written by the `IS_RIDING` block after the follower loop. |
| Voiding | The save `check_idling` makes while the player is still in the room holds the followers — the following mount, tamed animal and recruited orc — but not the ridden mount, which `stop_riding` has already released. |
| In the void | `Crash_save_all` saves every connected player every 30 s with no dirty check (`:1883-1888`), and `Crash_follower_save` only writes followers **in the player's room** (`:887`). The player is in the idle room, so each save has an empty follower list — within 30 s of voiding, the save holds no followers. |
| Idle disconnect | `Crash_idlesave` writes rent code 5, objects and aliases, and **no follower section** (no call to `Crash_follower_save`; unchanged since 2017). |
| Next login | The character's own new save; empty follower list. |

## Side effects and quirks found while observing (not changed)

- **The idle save does not reach the account `objects.json`.** `refresh_account_backed_object_file`
  copies the save through the strict reader, which requires a follower section, so the copy fails
  (SYSERR) and `objects.json` keeps the last 30-second save. Because that save already has no
  followers, followers come back the same way either way. **Unrentable items do not:**
  `Crash_idlesave` destroys keys and `ITEM_NORENT` items (`Crash_extract_norents`) before saving,
  but the 30-second save keeps them (`Crash_save` does not filter), and selection loads
  `objects.json` and deletes the idle save. Confirmed live 2026-09-14: an account character idled
  out carrying a key, a no-rent item and a plain ring, and logged back in with all three — the
  legacy path returns only the ring. **This was a bug, not historic behaviour, and is fixed:**
  `write_linked_character_object_file` (the save-copy path) now reads the save with the tolerant
  reader, which forgives only a follower section missing entirely, so the idle save reaches
  `objects.json`. `write_account_object_file` stays strict for direct writes. Covered by
  `AccountManagement.LinkedCharacterObjectRefreshAcceptsAnIdleSaveWithoutFollowerSection`. 231 live
  `.obj` files are in the rent-code-5 shape.
- **Loading a save with no follower section closes the file twice** — `Crash_follower_load` fcloses
  on the failed read (`objsave.cpp:960`) and `load_character` fcloses again (`:762`). Unverified at
  runtime.
- **One manual save omitted a ridden mount.** Before idling, the ranger's save held its ridden horse
  but the orc's save did not hold its ridden warg, although `stat` showed it riding. Not explained;
  it does not affect the idle outcome, since the void releases a ridden mount anyway.
- **`do_purge` saves the wrong character** — `Crash_idlesave(ch)` saves the immortal, not the
  purged player (`act_wiz.cpp:1429`, since 2018).

## How this was observed

A throwaway build with the thresholds lowered (AFK ≥ 1, void > 2, disconnect > 4, still once per
60 s) on a copy of `lib/`; a scripted client driving the level-100 god-race `Debugbot` as observer,
a new human ranger (advanced to 40, `tame`/`animals`/`ride` practised at guildmaster 1109) in the
arena (1120), and the `Orcrepro` fixture in the Great Crossroads (1160, peaceful, so its snaga and
wargs could not start race-war fights with the human). After both idled out, the normal build was
booted on the same data and both characters logged back in.
