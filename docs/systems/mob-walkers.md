# Mob walkers

A walker is a mob that follows a listed route or stays inside a listed area. It is not a mob
program: the settings are lines in the mob's options field (`/42`), and they work on any mob,
including one that already has a program. Code: `src/mob_walker.cpp`, hooked into the wander
step of `one_mobile_activity` (`src/mobact.cpp:321-341`).

## Modes

| `walker_type` | What the mob does |
|---|---|
| `path` | Walks the listed rooms in order, once, from the first to the last. |
| `loop` | As `path`, but from the last room the only step allowed is into the first. |
| `bounded` | Wanders as usual, but a move is refused when the target room is not listed. |

## Settings

One per line in `/42`. They are general settings (see "General settings" below).

| Setting | Meaning | Modes |
|---|---|---|
| `walker_type=path\|loop\|bounded` | how the room list is used | all |
| `walker_rooms=1101-1150,1162` | room vnums in order; commas and ranges (a range may count down); several lines are joined in order | all |
| `walker_move_chance=<1-100>` | percent chance that a valid route step succeeds; default 100 | path, loop |
| `path_complete_extract=yes` | remove the mob at the end of the route | path |
| `path_complete_message=<text>` | line sent to the room at the end; no default | path, loop |
| `continue_wandering=yes` | at the end, stop following the route and wander | path |

A room may be listed once. A list holds at most 2000 rooms (`WALKER_MAX_ROOMS`). The options
field holds 4000 characters and an editor line 255, so a long route is written as ranges over
several `walker_rooms=` lines.

## How a step is decided

Nothing tracks progress. Each time, the mob works from the room it is standing in. So a path
mob that is charmed or tamed, led to its last room and released ends its path there without
having walked the route: do not treat arrival as proof of the journey.

1. The standard wander roll is untouched: a non-SENTINEL, standing mob with no master rolls
   `number(0, 45)` and only a result below `NUM_OF_DIRS` is a direction (`mobact.cpp:321`).
2. `walker_wander` (`mob_walker.cpp:383`) is given that direction and whether its exit is
   usable (open, and not into a NO_MOB or DEATH room). `walker_decide` (`mob_walker.cpp:230`)
   answers from the config, the room the mob is in and the room the exit leads to:
   - the mob is not a walker, has finished its path, or is standing in a room that is not on
     its list: ordinary wandering applies, unchanged;
   - `bounded`: ordinary wandering if the target is listed, otherwise no move;
   - `path`/`loop`: a move only when the target is the next room on the list.
3. A route step must also pass the STAY-ZONE and STAY-TYPE flags, then the
   `walker_move_chance` roll.
4. A route step skips the wanderer's rule against two steps in a row in the same direction
   (`last_direction`). That rule still applies to `bounded` mobs, to a walker wandering off
   its list, and after `continue_wandering`.

So a `path` or `loop` mob moves only when the roll picks the one right direction, about 1 roll
in 46. With the ACTIVE flag a mob acts every 3 seconds instead of roughly every 24.

A walker that leaves its list (it fled, was summoned, was ordered) wanders like any mob until
it steps onto a listed room, then carries on from that room.

## The end of a route

Walking into the last room only means the mob has arrived. The end fires at its first
successful attempt to leave: the wander roll picked a usable exit and `walker_move_chance`
passed. If the last room has no usable exit at all, any rolled direction counts.

**path** (`mob_walker.cpp:410-422`): the mob stays, and in place of the move, in this order:

1. the `ON_PATH_END` part of the mob's script runs, if it has one (`script.cpp`,
   `trigger_char_path_end`);
2. with `path_complete_extract=yes` the mob is extracted, and what it wears and carries is
   destroyed with it (nothing is left in the room). That includes anything it picked up or
   was handed on the way. A mob that its own script extracts, or that dies, leaves its gear
   as any mob does;
3. `path_complete_message`, if set, is sent to the route's last room, where the path ended,
   even if the script has moved the mob elsewhere by then.

The mob is not freed in the middle of its own turn: it is queued, parked, and removed by
`walker_remove_finished()` when the pass that was running its turn is over (the end of
`mobile_activity()` or of `affect_update()`), and its message goes out at that moment.

A mob that is not extracted is marked finished (`specials.walker_finished`, never saved, so a
reloaded mob starts fresh). By default it is also set SENTINEL with 0 moves, a visible sign
that the journey is over. With `continue_wandering=yes` it wanders freely instead, and the
finished mark stops the route from starting again when it steps back onto a route room.

**loop** (`walker_wander`, `mob_walker.cpp`): every lap, once the mob has stepped from the last
room into the first, `ON_PATH_END` runs (the mob is then in the first room) and the message is
sent to the last room. A step that `do_move` refuses (too few moves, a no-walk exit, an
`ON_BEFORE_ENTER` script) runs neither. A `DO_WAIT` in the script holds the mob until the
script is done. `path_complete_extract` and `continue_wandering` do nothing.

A route step that removes the mob (an `ON_ENTER` script where it arrives) ends its turn; so
does an ordinary wander step (`mobact.cpp`).

The script may remove the mob itself; the message is still sent. It is also sent when
something else removes a mob that was queued for removal (a death in the same moment).
`ON_PATH_END` does not run for a mob in a waiting state, so the end of a path, or of a lap,
is put off to a later turn of the mob rather than run without its script. The one case
left: a loop mob that becomes waiting during the very step that completes its lap (something
in room one acts on it) skips the script for that lap; the message is still sent. With
`path_complete_extract=yes` the mob is removed at the end of that pass, a moment after the
script returns and once its turn is over, so a `DO_WAIT` in the script is cut short.

## Other flags

| Flag | Effect on a walker |
|---|---|
| ACTIVE | Acts every 3 seconds: much faster travel. |
| AGGR | Stops to attack players it sees; walking speed is unchanged. |
| HUNTER | Once attacked, it chases the attacker. A player can lead it off its route, so avoid it on a mob that must arrive. |
| SENTINEL | Never wanders, so never walks. |
| STAY-ZONE, STAY-TYPE | Still refuse a step. A route across a zone line needs a mob without STAY-ZONE. |

A mob that is following a master does not wander and does not walk. Like any wandering mob, a
walker needs movement points: `do_move` refuses a step the mob cannot pay for
(`act_move.cpp:264`), so a mob with 0 moves never takes one. That is also why the default end
of a path, SENTINEL with 0 moves, really does keep the mob where it is. One exception: a
shadow mob pays no moves for a step, so a parked shadow HUNTER still chases an attacker, as
any SENTINEL shadow hunter does.

## Builder messages

On `/save` and `/implement`, to the builder only (`walker_config_check`,
`mob_walker.cpp:468`). Nothing is checked at boot and nothing goes to the mud log: a mob on a
route that later breaks simply gets stuck. The one exception is a route room that does not
exist: the room lookup prints `Room <vnum> does not exist in database` to the server's error
output each time that mob's settings are read (once at boot and on `/save`, twice on a
plain `/implement`), never during the mob's turns. Lines are
`MOB ERROR: mobile #<vnum>, options line <n>: <text>` or `MOB WARNING: ...`; the line number
is left out when the problem is with the settings as a whole.

| Text | Meaning |
|---|---|
| `bad walker_type - walker disabled` | not `path`, `loop` or `bounded` |
| `walker_type missing - walker disabled` | walker lines without a `walker_type` |
| `bad room list - walker disabled` | not vnums, commas and ranges; or more than 2000 rooms |
| `walker_rooms missing - walker disabled` | no room list |
| `room <vnum> listed twice - walker disabled` | a room may be listed once |
| `room <vnum> not found - walker disabled` | no such room |
| `loop needs 2 rooms - walker disabled` | a loop of one room |
| `bad value - line ignored` | `walker_move_chance` outside 1-100, or a yes/no setting that is neither |
| `duplicate setting - line ignored` | a setting given twice (`walker_rooms` may repeat) |
| `no effect with <mode>` (warning) | a setting that does nothing in this mode |
| `no effect when extracted` (warning) | `continue_wandering` with `path_complete_extract=yes` |
| `longer than 78 columns` (warning) | `path_complete_message` will wrap |
| `no exit from room <a> to room <b>` (warning) | neighbouring route rooms are not joined; the mob will stop at `<a>` |
| `unknown setting - line ignored` | on a mob with no options program: a line no feature knows |

"Walker disabled" means the mob behaves as an ordinary mob.

## General settings

`mob_general_options()` (`src/mob_options.cpp:61`) is the one list of settings that are valid
on any mob. The vendor and banker parsers skip lines on it instead of reporting them unknown
(`mob_progs/shopkeeper.cpp`, `mob_progs/banker.cpp`), and a mob with no options program has
every line checked against it. A new feature adds its keys, with a one-line help text, to that
list once; the walker keys are the first entries.

## Not supported

A vendor or banker set `attacks=no` ends its whole turn (`vendor_takes_no_turn`), which stops
wandering as well as fighting, so it never walks.

## Deploying

Deploy the code before any world file that carries walker lines or an `ON_PATH_END` script
part. A server built before the options field existed (before PR #330) stops at boot on a
mob file that has options text. A server that has the options field but not walkers boots
and ignores the walker lines; its vendors and bankers log them as unknown settings.

## Tests

Unit: `src/tests/mob_walker_tests.cpp` (settings, the step rule, builder messages),
`ScriptOnPathEnd.*` in `src/tests/script_command_tests.cpp`, `MobGeneralOptions.*` in
`src/tests/mob_options_tests.cpp`, and the `GeneralLines...` cases in the vendor and banker
tests. The code that needs a world (`walker_wander`, the end of a route, the queued removal)
is covered in game, not by unit tests.
