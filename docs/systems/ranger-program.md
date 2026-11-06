# Ranger program (program 32)

Program 32, `mob_ranger_new`, gives a mob the ranger's habits: it hides, ambushes, follows tracks
and hunts the ones it remembers. Code: `src/spec_pro.cpp:2061-2430`; its settings live in the
mob options and are read by `src/mob_progs/ranger_prog.cpp`. Builder help: `MAN SHAPE RANGER`
(`lib/text/shap_tbl`). Design notes: issue #253 and PRs #224, #242, #254, #264.

The old ranger program is program 15 (`mob_ranger`, `src/spec_pro.cpp:1979`) and is unchanged.

## When it runs

Only on the mob's own turn (`SPECIAL_SELF`, from `one_mobile_activity`, `src/mobact.cpp`). It
ignores the room-entry call (`SPECIAL_ENTER`) completely, so it never acts the moment someone
walks in, and never answers "handled" for it. It always returns 1 on its own turn, so the
standard mob turn never runs after it: that is what keeps it from moving or attacking twice in
one turn.

## Settings (`/42`)

One per line, `key=yes|no`, all optional, default no. Parsed once per prototype at boot and on
`/implement`, and kept by mob rnum (`ranger_config_for`); running mobs pick up a change at
`/implement` or the next boot (the settings follow the program a mob runs, its type's function
else its own number, so loaded copies follow a switch to program 32; when `/implement` leaves the type with no program,
`/29 0` or the spec flag cleared, copies already loaded keep running program 32 until they reset,
and read the type's settings still; a mob without the spec flag gets none). `/save` only checks the
lines and reports problems, also for a mob just switched to program 32 by `/29` before
`/implement` (`mob_program_candidate`, `src/mob_options.cpp`). General settings
(walkers) are passed over.

| Setting | Effect |
|---|---|
| `hide=yes` | Hides when standing without cover and not fighting; again after a move if something took its cover. A hide that starts ends the turn, so targets are looked for from cover on the next one. |
| `ambush=yes` | Opens with an ambush instead of a hit when neither the mob nor the target is fighting. If the ambush does not start (no weapon, a weapon too big, no skill), it hits in the same turn: only a wind-up in progress counts as started, since the delay keeps the name of an ambush that has ended. |
| `track=yes` | Follows the freshest track it can step along, left by a player of a race it is aggressive to; with AGGRESSIVE, a player of any race. A track records race only, so an AGGRESSIVE_EVIL, _GOOD or _NEUTRAL mob follows any player's track and may not attack the one it catches up with. |
| `riposte=yes` | Ripostes parried blows at skill 75 (`check_riposte`, `src/fight.cpp:2182`). |

Hiding and sneaking. A program move calls `do_move` directly, which never unhides a walking
character, so without SNEAK a `hide=yes` mob keeps its cover as it moves: anyone whose
`see_hiding` is below its cover does not see it leave or arrive (`CAN_SEE`), anyone else does.
With the SNEAK affect (`/6`, bit 2) each step also rolls its sneak (`src/act_move.cpp:855`,
`910`): on a success even those who see through its cover miss the leave and arrive lines, and
`snuck_in` gives it a fresh cover from `hide_prof` on arrival, higher or lower than before.
The program clears the mob's sneaked-in mark before each step, as a player's typed move does, so
the sneak roll and that cover leave out sneak's own bonus, as a player's do:
`get_real_stealth`'s NPC line reads `max(100, ...)`, always +100, and a mob's mark is never
cleared by its own direct moves (a wanderer's or the program's), so the +100 would stick (stock
sneaking mobs keep it). A follower's move goes through the command handler, which unhides it first
(cover and mark, `interpre.cpp` 1400), so its sneak roll is a player's. With
`hide=yes`, the mob then hides once more on its next free turn, as a player may type `hide`
after sneaking in: the program rolls that hide itself (`hide_roll`, the plain-hide range) from
`hide_prof` plus sneak's bonus at its meant size, `min(100, 10 * level / 30)`
(`ranger_sneak_hide_prof`; no bonus once the SNEAK affect is gone) and clears the mark. As a
player's hide, the cover comes when the hide-after-sneak wind-up ends (the wind-up calls the
program back, `ranger_finish_hide`); until then the mob keeps the cover it snuck in with, and a
broken wind-up gives no new cover. It skips that hide while chasing, or following a leader
in its room (a delayed follower cannot follow, `act_move.cpp` 1059; one whose leader went on
without it hides, as a wanderer), with no one in the room to go for
(`ranger_chasing`: HUNTER with a remembered enemy in another room, or `track=yes` with a
track here; `ranger_target_here`: a remembered enemy or someone it is aggressive to, by
presence, no see roll), so a chaser or follower sneaks room to room and hides where it finds
a target; a wanderer hides after each sneak-in. With `hide=no` the mark is cleared on arrival, and at
the start of each turn for a move the program did not make (a follower moving with its
leader), so a later ambush does not read the +100 either; the sneak-in cover stays. A
`hide_prof` below 0 gives no cover: a sneak-in from it gets none (checked after the program's
steps, and at the start of a turn for a follower's move), and `hide=yes` starts no
hide while it lasts (the byte would wrap to a huge cover). For a follower's move both checks
wait for its next turn (its next follow move clears the mark first anyway): turns come every `PULSE_MOBILE` (about 6 s), so the cover and mark
live until then, and a tamed pet in its master's room never runs the program
(`mobact.cpp` 187). Only the core `snuck_in` could close that. Apart from that one hide, a
sneaking mob's cover means it does not hide again after each step. SNEAK alone, without
`hide=yes`, does the same on each step; the mob just does not hide when it stands.

Problems are reported in the house style on `/save` and `/implement` (and logged at boot):
`unknown setting - line ignored`, `duplicate <key> - line ignored` (the first good line stands),
`bad <key> - line ignored` (a later line with a good value still counts).
A mob whose /29 is 31, 32, 33 or 34 and whose program will not run gets one line saying why, then
the program's own check as if it ran (`report_blocked_program`, `src/shapemob.cpp`). With SPEC
off it is quiet when a mudlle program has that number, since /29 then means that one:
`MOB ERROR: mobile #<vnum>: program 32 set but SPEC flag off`, or `... but vnum has its own
special` when a special assigned to the vnum at boot takes the program's place.
`/implement` also warns, to the builder only, about flags the program does not act on:
`MOB WARNING: mobile #<vnum>: scavenger has no effect with program 32` (and bodyguard,
assistant).

## The turn

While anything is pending (a script's wait, a wind-up, the delay after a bash or flee), the mob
does nothing. The standard turn also acts on the last beat of a delay with no command waiting;
this program waits it out, since a hide or ambush started then is refused ("double delay": its
priority 30 cannot replace the delay's 50).

The WIMPY rule: a mob with the WIMPY flag at a fifth of its health or less skips steps 2 to 8. It
still hides and wanders. A mob without WIMPY never skips anything for health.

1. **Hide** (`hide=yes`). If a hide starts, the turn ends there.
2. **Remembered enemy here** (MEMORY or HUNTER, standing, not a pet): an enemy in the room it can
   see is attacked even if the mob is not otherwise aggressive to them, the hidden roll and not a
   nohassle immortal (its master is forgotten instead). An ambush if `ambush=yes` and they are
   not fighting, otherwise a hit.
3. **Ambush someone new** (`ambush=yes`): the first character in the room who is not fighting and
   whom `should_attack` accepts (below). Comes before help, since joining a fight would use up
   the opener. If the ambush does not start, it hits instead.
4. **Help** (HELPER): `mob_helper_assist`, the same rules as the standard turn
   (`src/mobact.cpp:77`). An ally whose enemy the mob cannot see, or whom the gods protect
   (Big Brother), is passed over before anything is said. Once it has said its line and
   assisted, that is its action for the turn, whatever became of the blow (sanctuary, a peace
   room). The same function serves the standard turn.
5. **Hit someone new**: the first character in the room that `should_attack` accepts: seen, the
   hidden roll (`see_hidden`), not a nohassle immortal, and aggressive to by race
   (`mob_race_aggressive_to`) or by the AGGRESSIVE flag with its alignment limits
   (`mob_flag_aggressive_to`); a MOUNT is never aggressive.

   Steps 2 to 5 run only while the mob is not fighting, and only attack if the mob is standing,
   the room is not a peace room, it can see the target, it is not a shadow (whose blows are
   mental) and neither is the charmed follower of the other; otherwise the turn goes on. The
   attack line ("grins evilly and attacks", or "snarls and lunges" below intelligence 7) is
   shown only for an attack that starts. Against sanctuary the mob does try: each try wears the
   sanctuary down ("aura flickers") until it breaks.
6. **Switch** (SWITCHING, fighting): one in four, to another character attacking it.
7. **Walk toward a remembered enemy** (HUNTER, none of them here, not ridden; only MEMORY makes a mob
   remember its attackers, so HUNTER needs MEMORY, short of a player ranger's hunt mark): one step by the shortest way,
   if that step would go through (`ranger_can_step`, below); otherwise the next enemy, and if
   none, the turn goes on.
8. **Tracks** (`track=yes`, standing and not fighting, no master, not ridden, nothing pending): a step along the chosen track.
9. **Wander** (not SENTINEL, standing, no master, not ridden, nothing pending): a random direction every
   turn, taken only through a usable exit (`exit_is_usable`, as the standard turn). A walker's rules (`walker_wander`, `src/mob_walker.cpp`) take the roll first; then
   `walker_move_chance` sets the pace, since this program tries a step on every turn where the
   standard turn rolls on about one in eight.

Every move ends the turn; the mob looks for targets in its new room on its next turn.
After any move the program checks the mob still exists, and stops if it was removed (an arrival
script, for example).

Tracks and hunt steps are only taken if the step would go through (`ranger_can_step`), by the
shared mob step checks in `src/mob_walker.h`: `exit_is_usable` (open, not NO_MOB, not a death
room) and `stay_flags_allow` (STAY_ZONE, STAY_TYPE). A closed door that is neither locked, hidden
nor a lever door does not stop a track: the mob opens it on its way, by the move code's own
rule (`mob_opens_door_on_move`, `src/act_move.cpp`); a shadow opens no door, but passes through
any that is not heavy. Wander, like the standard turn, takes open exits
only.

Those checks only rule out the steps that can be foreseen. Whether a hunt or track step went
through is read off the mob's room afterwards (`ranger_take_step`): a step the move refused
(water it cannot swim, a room script, a no-walk exit, too few move points) lets the turn go on
to the next enemy, then tracks, then wander. Each exit is tried at most once per turn
(`refused_exits`), so a refusal's scripts and costs (a before-enter script, Power of Arda)
happen once. A refused step that still opened a door on the way ends the turn: opening it was
the mob's action. SENTINEL stops wandering only; a SENTINEL mob still follows tracks and
hunts. A mob with a master (a zone-file follower, say) neither wanders nor tracks, and does both
again from the turn it loses its master. Tame and recruit take the program off the mob for good.
A ridden mount has no master while it is ridden (`do_ride` takes it off its follow, and
`do_dismount` puts it back), so being ridden is checked on its own: a ridden mount never
wanders, tracks or hunts, since its rider steers it. The standard turn applies the same rule to
its wander and HUNTER steps (`src/mobact.cpp`).

## Hidden roll

One roll per character per turn: a later step of the same turn reuses the result
(`hidden_rolls`), so being checked by several steps gives no extra chances to be found.

`see_hidden` uses the standard `see_hiding()` value below level 40. From level 40 the mob rolls
`level * 2 + 3 x number(-7, 20)` instead, on purpose lower than the standard value for high
levels, so a high ranger keeps a small chance to stay hidden. A mob vulnerable to stealth halves
the value.

## Cost

The settings are read once per mob prototype (boot, `/implement`) and kept in one table
by rnum: one small entry per program-32 prototype, shared by all its loaded mobs. The HUNTER step
runs a whole-world path search per remembered enemy, as the standard turn does: issue #394.

## Not supported

SCAVENGER, BODYGUARD and ASSISTANT do nothing on a program-32 mob: the standard turn that runs
them never runs after this program. Scavenger problems in general: issue #393.

## Tests

`src/tests/ranger_prog_tests.cpp`: the settings, the shared aggression test, and one test per
step and variant of the turn, against a three-room fixture world (rooms 989-991).
