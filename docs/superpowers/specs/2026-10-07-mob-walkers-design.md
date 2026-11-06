# Mob walkers: design

Decided with the project owner 2026-10-03 to 2026-10-07. Shared page:
https://claude.ai/artifact/UZsp6iSzRYKAQ8hWPeWL7c

## Goal

Movement automation for mobs that does not hang complicated processes on every mob that opts
in. A builder gives a mob a list of rooms and says how the list is used. No new mob program:
it works on any mob, including one that already has a program (vendor, banker, caster).

## Settings

In the mob options field (`/42`, `specials.mob_options`), one `name=value` per line.

| Setting | Meaning | Modes |
|---|---|---|
| `walker_type=path\|loop\|bounded` | how the room list is used | all |
| `walker_rooms=1101-1150,1162` | room vnums: commas and ranges; several lines are joined in order | all |
| `walker_move_chance=<1-100>` | percent chance a valid route step succeeds (speed control) | path, loop |
| `path_complete_extract=yes` | remove the mob at the end of the route | path |
| `path_complete_message=<text>` | line sent to the room at the end; no default | path, loop |
| `continue_wandering=yes` | at the end, stop following the route and wander | path |

A room appears at most once in a list. The options field keeps its 4000-character cap.

## Modes

- **path**: walks the rooms in order, once.
- **loop**: like path, but from the last room the only step allowed is into the first.
- **bounded**: wanders as usual, but a move is refused when the target room is not listed.

## Movement rules

1. The standard wander roll (`number(0, 45)`, `src/mobact.cpp`) is not changed. A path or loop
   mob only steps when the roll lands on the direction of its next room.
2. `walker_move_chance` is an extra roll on top, for path and loop only.
3. Nothing tracks progress. The mob works from the room it is standing in.
4. A walker standing in a room that is not on its list (it fled, was summoned) wanders like a
   normal mob until it steps onto a listed room, then follows the rules again from there.
5. The "no two steps in a row in the same direction" rule (`last_direction`) is skipped for
   path and loop mobs while they follow the route. It still applies to bounded mobs, to
   off-route wandering, and after `continue_wandering`.
6. Normal flags still apply: SENTINEL, STAY-ZONE, STAY-TYPE, NO_MOB and DEATH rooms, a master.
   ACTIVE makes a walker much faster. HUNTER can be used by a player to lead it off its route.

## End of route

Entering the last room only means the mob has arrived. The end fires at its **first successful
leave attempt** from the last room (wander roll and `walker_move_chance` both passed).

- **path**: the mob stays, and in place of the move, in this order:
  1. the `ON_PATH_END` block of the mob's script runs, if it has one (new script trigger);
  2. if `path_complete_extract=yes`, the mob is extracted;
  3. if `path_complete_message` is set, the text is sent to the room the mob was in.
  If the mob is not extracted: by default it is set SENTINEL with 0 max moves; with
  `continue_wandering=yes` it wanders freely instead. Either way a non-persisted "path
  finished" attribute on that mob stops the route from starting again.
  The extract is not done in the middle of the mob's own turn (the loops running that turn
  still hold the mob): the mob is queued, parked, and removed with its message as soon as the
  pass is over (`walker_remove_finished`). Found while building.
- **loop**: `ON_PATH_END` and the message run once the mob has stepped from the last room into
  the first (script in the first room, message to the last room; a refused step runs neither).
  Changed after review 2026-10-08: they used to run before the step. `path_complete_extract` and `continue_wandering` do nothing.

## Shared general settings

Walker settings are the first entries on one shared list of general settings, valid on any
mob (decided in the day/night mob design; built here because walkers ship first). Vendor and
banker parsers skip lines on the list instead of reporting them unknown. A mob with no program
has its lines checked against the list on `/save` and `/implement`. The help for the
options field (`lib/text/shap_tbl`) is written by hand and lists the same keys.

## Changes made after this spec, during review (2026-10-08/09)

`docs/systems/mob-walkers.md` is the record of the behaviour as built. Points that differ
from, or add to, the text above:

- A mob removed by `path_complete_extract` is queued and removed at the end of the pass,
  and what it wears and carries is destroyed with it.
- `path_complete_message` always goes to the route's last room.
- A loop mob steps into the first room first; `ON_PATH_END` and the message run only if it
  arrived.
- A mob in a waiting state does not end its path or lap on that turn; it tries again on a
  later turn.
- If the last room has no usable exit at all, any rolled direction counts as the leave
  attempt, so the path still ends.
- The message is still sent when the mob's own script, or anything else, removes the mob
  first.
- A room list holds at most 2000 rooms, and a range may count down (`1105-1101`).

## Builder checks (on /save and /implement, to the builder only)

Unknown `walker_type`; a room that does not exist; a room listed twice; `walker_move_chance`
outside 1-100; a path-only setting on a loop or bounded mob; two neighbouring route rooms not
joined by an exit. No check at boot and no mudlog: a mob on a route that later breaks simply
gets stuck.

## Not supported at launch

A vendor or banker set `attacks=no` ends its whole turn, so it never wanders and never walks.
