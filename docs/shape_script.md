# Shape Script Command

Scripts are short command sequences that attach to mobiles (and, partially, to
objects) and execute when a trigger fires: someone enters a room, speaks, wears
an item, etc. They are stored in `world/scr/<zone>.scr` and edited entirely
in-game. This guide replaces the legacy `scr_tbl` so builders can shape scripts
without hunting through old text files.

## Prerequisites

- Immortal access plus zone permissions (`get_permission(zone, ch)`).
- A reserved script vnum (matching the zone number, e.g., script 4205 lives in
  `world/scr/42.scr`). Coordinate with an implementor if unsure.
- Target mobile: scripts currently run on mobiles; object hooks exist but only
  a subset of triggers honor them. Assign the script vnum to a mobile via
  `shape mob /38` and clear the `SPECIAL` flag unless combining it with a
  hard-coded proc.

## Working with `shape script`

1. `shape script <vnum>` to load or create a program. New scripts get a blank
   header but no commands.
2. Use the numeric menu (`/0`):
   - `/1` show previous/current/next command.
   - `/2` set a mask (filter) by command letter, room, etc.
   - `/3` change the current command type.
   - `/4` edit parameters for the current command.
   - `/5` edit the one-line comment/description attached to the current command.
   - `/6` / `/7` move to the next/previous command (respecting the “current
     room” filter set via `/12`).
   - `/8` jump to a specific command number.
   - `/9` delete the current command (prompts for `y/n`).
   - `/10` insert a new command after the current one. `/11` inserts before.
   - `/12` set the “current room” for filtering (`0` = show entire script).
   - `/13` swap the current command with the next.
   - `/14` run a syntax check (flags unterminated `BEGIN/END`, etc.).
   - `/20` change the script name; `/21` change the script description.
   - `/50` list the entire script.
3. Editing fields uses the same conventions as other shapings:
   - Text entry (`/5`, `/20`, `/21`) opens the `%f/%e` editor.
   - Numeric prompts accept direct values (e.g., `42`), offsets (`+5`), or
     bit toggles (`p3` sets bit 3, `m3` clears). Blank lines keep the old value.
4. `/save` writes the script back to disk (after backing up to
   `world/scr/old/`). `/implement` copies the temporary version into the live
   `script_table` if the script existed when the MUD booted. `/done` performs
   `/implement`, `/save`, then `/free`. New scripts require a reboot before
   they can be implemented.
5. `/free` abandons changes and exits shaping. Always `/save` first if you care
   about the edits.

### Script file structure

Each entry in a `.scr` file looks like:

```
#<script number> <title>~
<multiline description ending with ~>
<command_type> <cmd_no> <param0> … <param5>
<optional text ending with ~>
...
999 0 0 0 0 0 0
```

`999` marks the end of one script; `#99999` marks the end of the file.

## Attaching scripts to mobiles

- In `shape mob`, set field `/38` (script number) to the script’s vnum. Do not
  set both a SPECIAL proc and a script unless you intend to chain them.
- In the zone file, use standard `M`/`K` commands; scripts load automatically
  when the mob is created. A reboot is required after adding a brand-new script
  vnum so `script_table` knows about it.

## Variables and parameter codes

Scripts use generic variables:

| Type | Storage | Fields |
|------|---------|--------|
| Characters (`ch1`, `ch2`, `ch3`) | Access via codes `100/200/300` (`SCRIPT_PARAM_CH1`, etc.) | `.name` (string), `.room`, `.level` (read-only), `.hit`, `.race` (RO), `.exp` (RO), `.rank` (RO). |
| Objects (`ob1`, `ob2`, `ob3`) | Codes `400/500/600` | `.name`, `.vnum` (RO). |
| Rooms (`rm1`, `rm2`, `rm3`) | Codes `700/800/900` | `.name`. |
| Integers (`int1`, `int2`, `int3`) | Codes `953/954/955` | Arbitrary numeric storage. |
| Strings (`str1`, `str2`, `str3`) | Codes `950/951/952` | Arbitrary text storage. |

“Read-only” fields (e.g., `chx.level`, `obx.vnum`) cannot be set via
`SET_INT_VALUE`. Use `ASSIGN_STR` to assign literal strings, or flow commands to
copy pointers (e.g., assign `ch2.name` to `str1` then compare).

Use the `get_param_text()` output shown in `/50` to verify that parameters map
to the intended variables.

## Triggers

Each trigger sits at the top of a script and provides entry points. Multiple
triggers can live in one script (e.g., ON_ENTER plus ON_HEAR_SAY).

| Trigger | When it fires | Variables | Return value |
|---------|---------------|-----------|--------------|
| `ON_ENTER` | After a character enters the room. | `ch1`=owner mob, `ch2`=entrant, `rm1`=room (for objects: `ob1` owner, `ch1` entrant). | Ignored. |
| `ON_BEFORE_ENTER` | Before the entrant is allowed in. | Same as `ON_ENTER`. | `FALSE` blocks entry; script must message the player. `TRUE` (default) lets them in. |
| `ON_DIE` | Just before the owner would die. | `ch1`=owner, others depend on cause. | `FALSE` prevents death (script must restore HP, send messages). |
| `ON_RECEIVE` | When somebody gives the owner an object. | `ch1`=recipient, `ch2`=giver (optional), `ob1`=item. | Ignored. |
| `ON_EXAMINE_OBJECT` | When someone examines the scripted object. | `ob1` owner, `ch1` examiner. | Ignored. |
| `ON_DAMAGE` | Before damage is applied. Works on mobiles and wielded objects. | `ch1`=victim, `ch2`=attacker, `ob1`=weapon (objects only). | `FALSE` cancels damage (script must handle messaging and HP updates); `TRUE` lets combat proceed. |
| `ON_DRINK` | When a character drinks from the scripted object. | `ch1` drinker, `ob1` container. | Ignored. |
| `ON_EAT` | When a character eats the scripted object. | `ch1` eater, `ob1` food. | Ignored. |
| `ON_HEAR_SAY` | Whenever someone in the room says something. | `ch1` owner, `ch2` speaker, `str1` spoken text. | Ignored. |
| `ON_HEAR_YELL` | When someone yells in the area. | Same as `ON_HEAR_SAY`. | Ignored. |
| `ON_PULL` | Before a lever is pulled. (Objects only.) | `ch1` puller, `ob1` lever. | `FALSE` cancels the pull; script must explain why. |
| `ON_WEAR` | Before an item is worn/wielded/lit. (Objects only.) | `ch1` wearer, `ob1` item. | `FALSE` prevents the action (script must message); `TRUE` lets it succeed. |

Remember that scripts lose all local data once they return, delay, or pause.
If a trigger stores `ch2` in `int1`, that value disappears after the script
exits. Use integers/strings only for within-script comparisons.

## Command reference

Commands fall into six categories. Arguments below use the variable abbreviations
noted earlier.

### Flow control

| Command | Description |
|---------|-------------|
| `BEGIN` / `END` | Mark blocks controlled by IF commands. |
| `END_ELSE_BEGIN` | Chain an `else` block after a `BEGIN` block. (Do not nest due to known bug.) |
| `IF_INT_EQUAL`, `IF_INT_LESS`, `IF_INT_GREATER`, `IF_INT_TRUE`, `IF_INT_FALSE` | Compare integer variables and execute the next block only if conditions match. |
| `IF_STR_EQUAL`, `IF_STR_CONTAINS` | Case-insensitive string comparisons. |
| `IF_IS_NPC` | Check whether a character variable refers to an NPC. |
| `IF_ROOM_SUNLIT` | Test whether a room is currently lit by the sun. |
| `ABORT` | Stop execution and return `TRUE`. |
| `RETURN_FALSE` | Stop execution and return `FALSE`. Used to suppress default behaviour (e.g., block entry, cancel damage). |
| `DO_WAIT` | Pause the script for N pulses (N from `param[0]`). When it resumes, all variables are reset to `0`, so refetch anything you need. |

### Player command emulation

| Command | Description |
|---------|-------------|
| `DO_SAY`, `DO_YELL` | Force a character to speak. Supports `%s` substitution for inserting another string. |
| `DO_EMOTE` | Force an emote, text taken from the command’s string. |
| `DO_SOCIAL` | Run a social by name (`nod`, `wave`). Optional target. |
| `DO_GIVE` | Make a character give an object to someone else. |
| `DO_DROP`, `DO_REMOVE`, `DO_WEAR` | Manage equipment/inventory. Combine with `ASSIGN_INV`/`ASSIGN_EQ`. |
| `DO_WEAR` | Automatically finds the correct slot. |
| `DO_HIT`, `DO_FLEE`, `DO_FOLLOW` | Force combat-related actions (use sparingly). |
| `DO_WAIT` | Already covered above—used for casting delays, etc. |

### Creating & removing

| Command | Description |
|---------|-------------|
| `LOAD_MOB` / `LOAD_OBJ` | Create a new mob/object by vnum and assign it to a `chx` or `obx`. Scripts are responsible for placing the object (use `OBJ_TO_CHAR`/`OBJ_TO_ROOM`). |
| `LOAD_OBJ_X` | Clone an existing object referenced in `obx`. |
| `EQUIP_CHAR` | Load up to five vnums on a character and auto-wear them. |
| `EXTRACT_CHAR` | Remove a mobile from the game (inventory drops in room). Never use on PCs. |
| `EXTRACT_OBJ` | Remove an object from the game. |

### Setting and reading values

| Command | Description |
|---------|-------------|
| `ASSIGN_STR` | Store a literal string in `strx`. |
| `ASSIGN_INV` / `ASSIGN_ROOM` | Locate objects by vnum in a character’s inventory (deep search) or room. Returns success in an `intx`. |
| `ASSIGN_EQ` | Fetch an object from a specific equipment slot (see Equipment table below). |
| `SET_INT_VALUE`, `SET_INT_SUM`, `SET_INT_SUB`, `SET_INT_MULT`, `SET_INT_DIV`, `SET_INT_RANDOM` | Perform integer math and assign the result to `intx` or fields like `ch1.hit`. Use with caution when targeting live character stats. |
| `SET_INT_WAR_STATUS` | Store fame-war state in an integer (1 if whities lead, -1 darkies lead, 0 tie). |
| `SET_EXIT_STATE` | Open/close/lock a door (state 0=open, 1=closed, 2=closed+locked). Automatically mirrors to the reverse exit and sends default messages. |
| `CHANGE_EXIT_TO` | Change an exit’s destination room. |
| `ASSIGN_ROOM` | Retrieve an object in a room by vnum. |

### Modifying the world

| Command | Description |
|---------|-------------|
| `OBJ_FROM_CHAR` / `OBJ_FROM_ROOM` | Remove an object from a character or room (no destination). Combine with `OBJ_TO_*` to teleport items. |
| `OBJ_TO_CHAR` / `OBJ_TO_ROOM` | Place an object in inventory or the room contents. |
| `TELEPORT_CHAR`, `TELEPORT_CHAR_X`, `TELEPORT_CHAR_XL` | Move characters between rooms (with or without followers). These commands do **not** send messages; scripts must narrate arrivals/departures. |
| `RAW_KILL` | Kill a character immediately (silent corpse). Ensure your script handles messaging and loot placement if needed. |
| `GAIN_EXP` | Adjust experience (positive or negative). Handles level-up/level-loss automatically. |
| `PAGE_ZONE_MAP` | Show the shaped zone map to a character (handy for maze hints). |

### Messaging

| Command | Description |
|---------|-------------|
| `SEND_TO_CHAR` | Send formatted text to a single character. Use `%s` to insert `strx` or `.name`. |
| `SEND_TO_ROOM` | Broadcast to the room (including the source). |
| `SEND_TO_ROOM_X` | Broadcast to the room except a specific character (e.g., to hide secret messages). |

## Equipment, race, and exit tables

Use these IDs with `ASSIGN_EQ`, `DO_REMOVE`, `CHANGE_EXIT_TO`, etc.

### Equipment slot IDs

| ID | Slot | ID | Slot |
|----|------|----|------|
| 0 | Light | 11 | Shield |
| 1 | Right finger | 12 | Cloak |
| 2 | Left finger | 13 | Belt |
| 3 | Neck slot 1 | 14 | Right wrist |
| 4 | Neck slot 2 | 15 | Left wrist |
| 5 | Body | 18 | Back |
| 6 | Head | 19/20/21 | Belt slots 1-3 |
| 7 | Legs |  |  |
| 8 | Feet |  |  |
| 9 | Hands |  |  |
| 10 | Arms |  |  |

### Race IDs

Use the `RACE_*` constants from `src/structs.h`. Key values as of this codebase:

| Constant | Id | Notes |
|----------|----|-------|
| `RACE_GOD` | 0 | Immortal slot. |
| `RACE_HUMAN` | 1 | Standard PC race. |
| `RACE_DWARF` | 2 |  |
| `RACE_WOOD` | 3 | Wood elf. |
| `RACE_HOBBIT` | 4 |  |
| `RACE_HIGH` | 5 | High elf. |
| `RACE_BEORNING` | 6 |  |
| `RACE_URUK` | 11 | Uruk-hai. |
| `RACE_HARAD` | 12 | NPC Harad (legacy macro). |
| `RACE_ORC` | 13 |  |
| `RACE_EASTERLING` | 14 | NPC Easterling. |
| `RACE_MAGUS` | 15 | Magi / Uruk-lhuth slot. |
| `RACE_UNDEAD` | 16 | NPC undead. |
| `RACE_OLOGHAI` | 17 | Olog-hai/troll elite. |
| `RACE_HARADRIM` | 18 | PC Haradrim. |
| `RACE_TROLL` | 20 | Cave troll. |

If code adds or renames constants, update this table to match.

### Exit IDs

`0`=North, `1`=East, `2`=South, `3`=West, `4`=Up, `5`=Down.

## Examples

BuilderClient TypeScript examples are kept as executable offline fixtures so
documentation cannot drift ahead of the editor/runtime API. The current examples
compile in the BuilderClient TypeScript pipeline and run through the offline
fixture runner while using command helpers accepted by the live JavaScript
runtime:

- [`gate-greeter.ts`](../BuilderClient/examples/shape-script/gate-greeter.ts)
  covers `doSay`, `sendToChar`, `sendToRoom`, and `doWait`.
- [`quest-reward.ts`](../BuilderClient/examples/shape-script/quest-reward.ts)
  covers `loadObj`, `doGive`, and `sendToChar`.
- [Legacy script corpus plan](../BuilderClient/docs/legacy-script-corpus.md)
  tracks the active `lib/world/scr` scan and the JavaScript example slices
  derived from current game scripts.
- [`1100-herald-enter.ts`](../BuilderClient/examples/legacy-script/1100-herald-enter.ts)
  is derived from `lib/world/scr/11.scr` script `#1100` and covers a simple
  `ON_ENTER` player guard with `doSay`.
- [`1101-training-reward.ts`](../BuilderClient/examples/legacy-script/1101-training-reward.ts)
  is a supported-subset translation of `lib/world/scr/11.scr` script `#1101`
  and covers `ON_ENTER`, race branching, `loadObj`, and `MutationResult`
  fallback messaging. Legacy local object variables plus NPC custody before
  giving are captured by the modern reward/custody helper design rather than
  exposed as pointer-like script slots.
- [`1130-yell-social-output.ts`](../BuilderClient/examples/legacy-script/1130-yell-social-output.ts)
  is derived from `lib/world/scr/11.scr` script `#1130` and covers `ON_ENTER`,
  `sendToRoomExcept`, `yell`, `emote`, `social`, and `pageZoneMap`.
- [`27500-gate-watch.ts`](../BuilderClient/examples/legacy-script/27500-gate-watch.ts)
  is derived from `lib/world/scr/275.scr` script `#27500` and covers race
  branching, output, and `doWait`; repeated door-state commands remain a future
  room/exit helper slice.
- [`6300-climb-before-enter.ts`](../BuilderClient/examples/legacy-script/6300-climb-before-enter.ts)
  is derived from `lib/world/scr/63.scr` script `#6300` and covers
  `ON_BEFORE_ENTER`, allow/block returns, and fall messaging.
- [`1800-honeycake-eat.ts`](../BuilderClient/examples/legacy-script/1800-honeycake-eat.ts)
  covers `ON_EAT` object-host fixtures, actor/NPC guards, object-vnum
  branching, and actor/room output.
- [`1810-moonwell-drink.ts`](../BuilderClient/examples/legacy-script/1810-moonwell-drink.ts)
  covers `ON_DRINK` object-host fixtures, object-name branching, and
  no-mutation warning output.
- [`1820-rune-examine.ts`](../BuilderClient/examples/legacy-script/1820-rune-examine.ts)
  covers `ON_EXAMINE_OBJECT` examiner-only output and ordinary-object fallback
  branches.
- [`1830-locked-lever-pull.ts`](../BuilderClient/examples/legacy-script/1830-locked-lever-pull.ts)
  covers `ON_PULL` allow/block returns, level gating, and pull-cancel
  explanation output.

Builder-facing client docs are maintained with the client:

- [BuilderClient tutorial](../BuilderClient/docs/builder-tutorial.md) covers the
  end-to-end authoring workflow from Git workspace setup through offline
  fixtures, packaging, and test-server publishing.
- [BuilderClient help](../BuilderClient/docs/builder-help.md) is the quick
  reference for panels, fixture fields, expectations, helper result codes, and
  troubleshooting.

These helpers currently queue or reserve validated JavaScript command intents.
For `doGive`, `loadObj`, `doWait`, `doSay`, `sendToChar`, `sendToRoom`,
`sendToRoomExcept`, `yell`, `emote`, `social`, and `pageZoneMap`, live dispatch
now returns inline `MutationResult` codes for the failures it can classify
before queuing the command, so builders do not need a separate preflight helper
for normal capacity, ownership, wait-state, audit, map lookup, or
output-recipient branches. Snake-case helper names remain compatibility aliases
for migrated scripts, and the internal command log operation labels still use
the existing snake-case transaction names. A successful `ok` result means the
command was accepted into the current mutation transaction; it does not mean the
live game mutation or descriptor write has already completed inside the running
script.
After the handler returns successfully, live dispatch prepares all
setter, room-flag helper, object-helper, wait, and output intents, including
setter target validation; rejects malformed targets, failed command-helper
audit, and failed room-flag-helper audit before writing; rechecks room-flag,
object-helper, wait, and character movement liveness plus zone authority; applies
object commands; applies room-flag helpers; applies scalar setters; applies wait
state; applies character movement; then applies descriptor output last. That
category order means output cannot leak
from a script whose earlier mutation validation fails, but it also means live
side effects are not replayed strictly in JavaScript source order.

Command helpers only accept real handles from the current `ctx`; fake objects,
spread copies, prototype copies, copied ids, or borrowed `isValid` functions are
rejected before any command event is queued. Server-side command audit can also
reject a whole output/object/wait helper batch before room flags, setters,
object movement, wait state, or descriptor output changes. Audit records the
operation summary plus trigger/package/handler context. Numeric target ids must
use bounded, unsigned live id forms such as `player:7`, `room:1204`, or
`object:301`; malformed ids reject before audit. Polymorphic trigger roles
(`ctx.target`, `ctx.targ1`, and `ctx.targ2`) can be passed to command helpers
only when the role's concrete handle kind matches the helper. Character targets
work with character helpers, room targets work with room helpers, and object
targets work as object helper arguments. Wrong-kind polymorphic handles fail
without queuing a command event, and raw numeric character/object id lookup is
not promoted as a live command-target API. During live dispatch, `target` is
resolved only from the explicit target payload; a stale explicit target never
falls back to `targ1` or `targ2`. Room-valued polymorphic command targets must
still point at a loaded world room; detached rooms are rejected before output,
object loading, audit, or mixed-batch side effects. Mutating command helpers
require server command audit when persistent authority is present. If an object
helper batch or a later room-flag apply step fails after object apply begins,
loaded objects are extracted and `doGive` transfers are reversed when the
expected recipient still carries the object. JavaScript `loadObj(vnum,
character)` prechecks the recipient's count and carried-weight capacity before
creating the object. JavaScript `doGive` uses a silent transaction transfer,
not the legacy player-command `perform_give()` path, so it does not emit
give-command messages, write legacy give logs, or fire `ON_RECEIVE` while the
outer transaction is only partially applied.
JavaScript `doWait(pulses)` returns inline `not-authorized`, `invalid-target`,
`already-waiting`, or `audit-rejected` failures before queuing when the live
server can classify them. `already-waiting` means no new wait was scheduled; a
successful `ok` wait still applies only after the transaction rechecks the live
host and reaches the wait category.

The intended final helper shape is:

```ts
const give = RotS.Script.doGive(giver, recipient, object);
if (!give.ok) {
  RotS.Script.sendToChar(recipient, give.code === "inventory-full"
    ? "It appears your inventory is full."
    : "I cannot give that to you right now.");
  return;
}
```

For live server dispatch, `RotS.Script.doGive()` returns inline command outcome
codes for the current transaction preflight. `inventory-full`, `too-heavy`,
`no-drop`, `not-carried`, `invalid-target`, `not-authorized`, and
`audit-rejected` failures do not queue a transfer, so builders can branch and
send their own fallback text. A successful `ok` result means the give was
accepted into the current mutation transaction, not that the object has already
moved inside the running script. The engine still performs the actual transfer
once during transaction apply and rechecks live state before committing.
`RotS.Script.loadObj(vnum, target)` follows the same accepted-transaction model:
`not-found`, `invalid-target`, `not-authorized`, `inventory-full`, `too-heavy`,
and `audit-rejected` failures do not queue object creation, while `ok` creates
the object later during transaction apply after a final recheck. The
one-argument form also returns an inline result, but currently represents a
validated no-placement intent and does not create an object until local object
variables are designed. BuilderClient offline fixtures can include a
non-script-visible `objectPrototypes` catalog. When that catalog is present,
`loadObj`/`load_obj` returns `not-found` for absent vnums and uses the matched
prototype's `flags.weight` for carried-weight capacity checks. When the catalog
is omitted, offline fixtures keep the lightweight optimistic behavior so concise
fixtures do not need to model every object prototype. An empty catalog is still
an explicit catalog and means no object prototypes are available.
`RotS.Script.doWait(pulses)` also follows the accepted-transaction model on the
live server. Offline fixtures validate the pulse range and can model wait-list
state with `ctx.hostAlreadyWaiting`. When `hostAlreadyWaiting` is `true`, the
first offline `doWait()` returns `already-waiting` without logging a command
event. When it is omitted or `false`, the first valid wait logs a command event
and later waits in the same fixture run return `already-waiting`.
`RotS.Script.doSay(speaker, text)`, `RotS.Script.sendToChar(target, text)`,
`RotS.Script.sendToRoom(room, text)`, `RotS.Script.sendToRoomExcept(room,
except, text)`, `RotS.Script.yell(speaker, text)`, `RotS.Script.emote(actor,
text)`, `RotS.Script.social(actor, command, target?)`, and
`RotS.Script.pageZoneMap(target, zone)` return inline `invalid-target`,
`audit-rejected`, `not-found`, or `no-recipient` failures before queuing when
the live server can classify them. The live server audits output helpers before
exposing recipient reachability, so an audit denial returns `audit-rejected`
before the script can observe `no-recipient`. `no-recipient` means the character
or room target exists but no connected playing descriptor would receive the
text. `pageZoneMap` accepts a live `Zone` handle from the current context, such
as `ctx.room.zone`, and returns `not-found` when that zone's map text is not
loaded. Successful `ok` output still writes only after the transaction reaches
the descriptor-output category, where script output again filters recipients to
connected playing descriptors.
BuilderClient offline fixtures currently record command-helper events in source
call order for diagnostics. They compile and validate the same helper API, but
they do not emulate live server category ordering as the final commit order.
Offline object-helper state is private, per-run diagnostic state: accepted
`loadObj(vnum, character)`, `doGive(giver, recipient, object)`,
`moveObject(object, target)`, `dropObject(character, object)`, and
`extractObject(object)` calls update hidden count, weight, room placement,
ownership, and extracted-object facts so later helper calls in the same run can
branch on realistic `inventory-full`, `too-heavy`, `not-carried`,
`no-drop`, or `stale-handle` results. Failed object helpers do not update that
hidden state, repeated fixture runs start from the original fixture, and
script-visible frozen snapshots such as `ctx.actor.inventory` and
`ctx.object.carriedBy` do not change. Room object placement is also covered by a
non-script-visible BuilderClient probe and private per-run execution state that
clones fixture room contents and applies accepted `loadObj(vnum, room)`,
`moveObject(object, room)`, and `dropObject(character, object)` commands with
deterministic offline ids while leaving `ctx.room.contents` frozen and
unchanged for the running script. Missing-prototype, wrong-room, and failed
movement/drop/extract branches leave placement state unchanged, and each fixture
run starts from the original room contents. Offline `doGive` treats
room-contained objects as `not-carried` unless an accepted movement or transfer
has explicitly moved them into character ownership. Remaining fixture parity
gaps include source-order diagnostic behavior versus server category commit
order. A
non-script-visible BuilderClient probe can inspect queued output intents
separately from committed descriptor
output events. Normal offline fixture runs expose only committed descriptor
output after helper validation succeeds, while preserving existing source-order
command logs; failed output helpers do not add committed descriptor output, and
committed output is bounded for result rendering. Builders can inspect committed
descriptor output in fixture results, the Output panel, CLI fixture JSON, and
fixture expectations through `descriptorOutputContain`. For output helpers,
offline fixtures can emulate `no-recipient` by setting a character fixture
handle's `canReceiveOutput` to `false`, or by giving a room fixture a
`characters` array with no reachable character entries. `sendToRoomExcept` also
uses the room `characters` list when present so tests can cover the branch where
excluding the actor leaves no recipient. `pageZoneMap` uses the passed fixture
zone handle's `map` text; missing or empty map text returns `not-found`. Fixture
handles remain reachable by default when descriptor state is omitted, so
existing examples stay concise while branch tests can still model disconnected
recipients.

`RotS.Script.teleportChar(character, room)` is the callable `TELEPORT_CHAR`
migration path for moving a live character handle to a live room handle while
also carrying NPC followers that are currently in the same source room. Player
followers stay behind. Successful `ok` means the movement command is accepted
into the current transaction; the live server rechecks liveness,
target-zone authority, blocked-room policy, `NO_TELEPORT`, and audit before
applying. Apply uses legacy `TELEPORT_CHAR` semantics: stop riding, move eligible
NPC followers first, move the selected character, emit no movement text, and do
not run movement-trigger propagation. The migration alias is
`RotS.Script.teleport_char(character, room)`; prefer camelCase in new scripts.

`RotS.Script.teleportCharOnly(character, room)` remains the callable
`TELEPORT_CHAR_X` migration path for moving exactly one live character handle to
a live room handle. It uses the same validation and result codes as
`teleportChar`, but deliberately leaves all followers behind. Offline fixtures
mirror accepted movement in hidden per-run state while keeping object/drop helper
effects aligned to live category order: object helpers still commit before
character movement, and `ctx.actor.room` stays frozen. Failure codes include
`invalid-target`, `not-authorized`, `blocked-room`, `no-teleport`, and
`audit-rejected`. The migration alias is `RotS.Script.teleport_char_x(character,
room)`; prefer camelCase in new scripts.

`RotS.Script.teleportCharToTargetRoom(character, target)` is the callable
`TELEPORT_CHAR_XL` migration path for moving exactly one live character handle to
the current room of another live character handle. It deliberately leaves all
followers behind. Live dispatch resolves the destination from the target
character's current room during inline preflight and rechecks it during
transaction apply, so a stale target or target without a current room returns
`invalid-target`. The destination room uses the same blocked-room and
`NO_TELEPORT` policy as the other teleport helpers. Offline fixtures resolve the
target character's hidden per-run current room and keep visible `ctx` snapshots
frozen. The migration alias is
`RotS.Script.teleport_char_xl(character, target)`; prefer camelCase in new
scripts.

Other character movement helpers remain planned but not callable yet. The
documented modern mappings are `LOAD_MOB` to `RotS.Script.loadMob(vnum, room)`,
`EXTRACT_CHAR` to `RotS.Script.extractChar(character)`, `DO_FOLLOW` to
`RotS.Script.doFollow(follower, leader)`, and `DO_FLEE` to
`RotS.Script.doFlee(character)`, but they stay absent from generated typings,
fallback typings, live QuickJS handles, and offline fixtures until the live and
offline implementations both have audited room, follower, combat, liveness,
rollback, and result-code parity. Do not use raw `Character.setRoom`,
`Character.setFollowers`, `Character.setMaster`, `Character.setMount`,
`Character.setGroup`, or `Room.setCharacters`; those raw relationship writes are
intentionally unavailable because they would bypass movement triggers, room
people lists, follow/master links, mount propagation, combat cleanup, and
account/admin audit.

`LOAD_MOB` remains a preflight-only JavaScript design. The legacy command loads a
mobile prototype into a temporary character variable without placing it; scripts
then combine that pointer with teleport or room/object commands. A future
`RotS.Script.loadMob(vnum, room)` helper should use an explicit room argument
instead, reject unknown or unauthorized mobile prototypes through a server-owned
prototype catalog, audit the target room and package context, initialize mobile
procedure and Mudlle state through the normal server loader, update room
membership and mobile live counts atomically, and avoid returning a mutable live
character handle until handle lifetime tokens exist. BuilderClient offline
fixtures must model hidden `mobilePrototypes`, deterministic generated mobile
ids, room membership, load limits, and `not-found` branches without mutating the
visible `ctx` snapshot. Until that parity exists, builders should not spawn
characters from JavaScript.

`DO_FOLLOW` remains a preflight-only JavaScript design. The legacy command makes
one character follow another, but if the requested relationship would create a
follow loop it first stops the would-be leader from following their own master
and then adds the new follower. A future `RotS.Script.doFollow(follower, leader)`
helper should reject self-follow and looped graphs instead of silently rewriting
the leader's relationship, audit both live character handles, model follower
caps and protected charm/tame/recruit/pet state, preserve reciprocal
`master`/`followers` links, and define whether follow/stop-follow messages are
emitted or suppressed. BuilderClient offline fixtures must model hidden
relationship state for previous-master replacement, loop rejection, stale
handles, follower caps, and rollback while leaving visible relationship snapshots
frozen. Until that parity exists, builders should use explicit teleport helpers
or script-visible branch outcomes instead of rewriting follow links directly.

`DO_FLEE` remains a preflight-only JavaScript design. The legacy `do_flee`
command rejects berserk characters, stands lower-position characters when
possible, tries up to six random exits, filters `EX_NOFLEE`, `EX_NOWALK`, death
rooms, and NPC stay-zone/stay-type constraints, calls `check_simple_move`, then
uses `do_move(..., SCMD_FLEE)` for successful movement. Success can remove
`AFF_HUNT`, stop combat for the fleeing character and room opponents, charge
player flee XP, move same-room followers and mounts through the normal movement
path, fire movement/entry triggers, drop stay-zone objects, and avoid death-room
exits before movement. The legacy path also inherits `AFF_HAZE` direction
redirection inside `do_move`, and it can run command/before-enter checks once in
`do_flee` and again in `do_move` before the final entry triggers. A future
`RotS.Script.doFlee(character)` helper should replace legacy random attempts
with deterministic eligible-exit selection, explicitly suppress or model haze
redirection, define whether duplicate command/before-enter checks are collapsed
or preserved, audit the selected source/destination rooms plus combat and
follower/mount policy, return branchable results such as `not-eligible`,
`no-exit`, `no-flee`, `blocked-exit`, and `too-exhausted`, and roll back combat,
room membership, movement points, follower/mount propagation, XP loss,
`AFF_HUNT`, and descriptor output if the batch fails. BuilderClient offline
fixtures must model hidden movement/combat state while keeping visible snapshots
frozen. Until that parity exists, builders should use explicit teleport helpers
or return branch outcomes instead of forcing characters to flee from JavaScript.

Equipment helpers remain preflight-only JavaScript designs. The documented
modern mappings are `DO_WEAR` to `RotS.Script.doWear(character, object, slot?)`,
`DO_REMOVE` to `RotS.Script.doRemove(character, slotOrObject)`, and
`EQUIP_CHAR` to `RotS.Script.equipChar(character, prototypes)`, but they stay
absent from generated typings, fallback typings, live QuickJS handles, and
offline fixtures until live/offline implementations can preserve direct
carried-object ownership, equipped-slot membership, canonical wear-slot
validation, race/body restrictions, anti-alignment and item race-flag zap
restrictions, pet restrictions, belt prerequisites, shield/two-handed conflicts,
`ON_WEAR` blocking before alternate finger/neck/wrist/belt slot fallback, forced
remove of occupied slots, waist-belt cascade removal, inventory-full room drops,
light counters, object affects, poison damage/death side effects, crash-save
state, deterministic prototype loads, audit, branchable result codes, and
rollback or no-partial behavior. The future `equipChar` helper should
deliberately tighten legacy `EQUIP_CHAR` partial wear-all behavior into
JavaScript V1 no-partial preflight/rollback semantics. Do not use
`Character.setEquipmentSlot` or raw inventory setters; equipment mutation must
go through audited helpers once promoted.

Room and exit mutation helpers remain preflight-only JavaScript designs. The
documented modern mappings are `SET_EXIT_STATE` to
`RotS.Script.setExitState(room, direction, state)` and `CHANGE_EXIT_TO` to
`RotS.Script.changeExitTo(room, direction, destination)`, but they stay absent
from generated typings, fallback typings, live QuickJS handles, and offline
fixtures until live/offline implementations can preserve canonical direction
validation, open/closed/locked state validation, existing-exit checks,
`EX_ISDOOR`, `EX_ISBROKEN`, `NOWHERE` destination handling, conditional
reverse-exit mirroring, source-only versus reciprocal destination policy,
destination-zone authority, reset-command impact checks, room-message
reachability, audit, branchable result codes, and rollback or no-partial
behavior. JavaScript V1 should deliberately tighten legacy exit behavior:
`SET_EXIT_STATE` should accept north/direction 0 even though the legacy script
branch skips falsey direction 0, reject invalid states instead of defaulting them
to open, and still preserve the legacy door-state side effects where valid.
Legacy `SET_EXIT_STATE` changes only `EX_CLOSED` and `EX_LOCKED` while
preserving other exit flags, clears broken-door state with output, and mirrors
only a reciprocal reverse exit. Future `changeExitTo` should reject invalid
directions and missing source exits instead of preserving the legacy
chained-comparison and unchecked `dir_option` dereference hazards. Legacy
`CHANGE_EXIT_TO` changes only the source exit destination and does not create or
repair reverse links. Do not use `Room.setExit` or raw exit arrays; exit
mutation must go through audited helpers once promoted.

`EXTRACT_CHAR` remains a preflight-only JavaScript design. The legacy command
extracts only NPC targets and then clears the legacy character variable; player
characters are ignored by that script branch, while the underlying
`extract_char(...)` server function also owns descriptor, account menu, save,
combat, riding, follower, equipment, carried-object, room-list, and mobile-index
side effects. A future `RotS.Script.extractChar(character)` helper must reject
player/account-backed targets, audit the NPC target and source room, mark
extracted handles stale, model carried and worn object placement, and either
reject mixed batches containing extraction or prove rollback before descriptor
output commits. Until those branches are implemented in both live dispatch and
BuilderClient offline fixtures, use builder-authored control flow and output
helpers to block or narrate a path instead of removing characters from
JavaScript.

Combat and proc effect helpers remain preflight-only JavaScript designs. The
documented modern mappings are `DO_HIT` to
`RotS.Script.doHit(attacker, victim)`, direct damage/healing to
`RotS.Script.applyDamage(victim, amount, options)`, `RAW_KILL` to
`RotS.Script.rawKill(character)`, and `GAIN_EXP` to
`RotS.Script.gainExperience(character, amount)`, but all stay absent from
generated typings, fallback typings, live QuickJS handles, and offline fixtures
until live/offline parity exists. `applyDamage` is intentionally not a direct
port of unsafe legacy `SET_INT_VALUE ch.hit`; it should replace raw hit-point
writes with an audited trigger-aware damage/healing path. `doHit` must preserve peace-room,
same-room/visibility, self-target, charm/master protection, big-brother target,
sanctuary, existing-fight, wait-state, and recursion policies before it can
start or redirect combat. `applyDamage` must preserve victim and weapon
`ON_DAMAGE` blocking order, recursive-damage guards, bounded hit-point deltas,
death-threshold policy, damage-details accounting, source attribution, and
healing caps. `rawKill` is destructive: it owns delay cleanup, fighting/riding
cleanup, the terminal `SPECIAL_DEATH` hook, affect removal, death cries, corpse creation,
big-brother records, player save/crash-save, ability restore, and stale-handle
state, so any first promotion should be admin-grade or batch-isolated.
`gainExperience` must remain player-only like legacy `SCRIPT_GAIN_EXP`, apply
the legacy high-level scaling policy deliberately, audit reward/source context,
and define level-up/down, practice/progression, output, persistence, and
rollback behavior. Until those contracts exist, builders should use blocking
trigger returns plus output helpers to narrate combat outcomes instead of
mutating combat, death, or progression state directly.

### Greeter with gift

```
TRIGGER: ON_ENTER ()
DO_SAY `Good evening %s.` (ch1)(ch2.name)
LOAD_OBJ 5104 -> ob1
OBJ_TO_CHAR ob1 ch1
DO_GIVE ch1 ch2 ob1
DO_SAY `Please accept this gift.` (ch1)(null)
```

This script greets entrants, creates a scimitar, gives it to the owner so it
exists in-world, then gifts it to the visitor. The current JavaScript surface
can model the simple direct-target path with `loadObj(vnum, character)`, but the
legacy custody sequence should usually use higher-level helpers once they are
promoted:
`giveReward(...)` for atomic load/give reward handoffs, `exchangeReceivedObject(...)`
for ON_RECEIVE exchange tables, typed `findInventoryObject`/`findEquippedObject`/
`findRoomObject` lookups instead of object slots, `stashObject`/`moveObjectToRoom`
for workshop custody, and audited destructive cleanup built on
`extractObject`. Those higher-level helpers must preflight multi-reward capacity and weight as a batch, keep
default item-return paths branchable, and roll back without consuming accepted
input objects when later reward creation or transfer fails.

### Blocking entry by race

```
TRIGGER: ON_BEFORE_ENTER ()
SET_INT_VALUE int1 11          ; RACE_URUK
SET_INT_VALUE int2 ch2.race
IF_INT_EQUAL int1 int2
BEGIN
    SEND_TO_CHAR `Orcs are not welcome in here.` ch2
    RETURN_FALSE                 ; block entry
END
```

If the entrant’s race matches `RACE_URUK` (11), the script sends a rejection
message and returns `FALSE`, preventing the move.

### Hurtful weapon (ON_DAMAGE)

Attach this script to a cursed sword so the victim doesn’t take actual damage
unless a condition passes.

```
TRIGGER: ON_DAMAGE ()
SET_INT_VALUE int1 ch2.level
SET_INT_VALUE int2 50
IF_INT_LESS int1 int2
BEGIN
    SEND_TO_CHAR `The blade fizzles before it harms you.` ch1
    SEND_TO_ROOM_X `The cursed sword fizzles.` ch1.room ch2
    RETURN_FALSE                 ; cancel damage
END
```

Characters below level 50 take no damage; the script handles messaging and
returns `FALSE` so the engine skips damage processing.

## Safety tips

- Scripts lose all context when they finish or delay. Always reassign any
  `chx`/`obx` pointers after `DO_WAIT`.
- Returning `FALSE` shifts responsibility to your script: cancel door pulls,
  deliver alternate damage, or provide failure text as appropriate.
- Be mindful of destructive commands (`RAW_KILL`, `EXTRACT_CHAR`, `SET_INT_VALUE`
  on `ch.hit`). Test on a copy of the zone before deploying to live.
- Keep scripts small and readable. Use `/5` comments on each command to explain
  intent for future maintainers.

### Quest hand-off (ON_RECEIVE)

```
TRIGGER: ON_RECEIVE ()
ASSIGN_STR `ancient chalice` -> str1
IF_STR_EQUAL ob1.name str1
BEGIN
    SEND_TO_CHAR `Thank you! Here is your reward.` ch1
    LOAD_OBJ 8001 -> ob2           ; reward token
    OBJ_TO_CHAR ob2 ch1
    DO_GIVE ch1 ch2 ob2
    RETURN_FALSE                   ; keep the original item
END
SEND_TO_CHAR `I have no need for that.` ch1
DO_GIVE ch1 ch2 ob1                ; hand back other items
```

Players who hand the NPC the “ancient chalice” receive a reward while the script
returns `FALSE` to stop the MUD from automatically transferring the item. Any
other item is immediately returned.

### Listener with branching responses (ON_HEAR_SAY)

```
TRIGGER: ON_HEAR_SAY ()
ASSIGN_STR `HELP` -> str2
IF_STR_CONTAINS str1 str2
BEGIN
    SEND_TO_CHAR `Gather three sigils and return to me.` ch2
    END_ELSE_BEGIN
    ASSIGN_STR `SECRET` -> str2
    IF_STR_CONTAINS str1 str2
    BEGIN
        SEND_TO_ROOM `The hermit whispers a secret word.` ch1.room
        END_ELSE_BEGIN
        SEND_TO_CHAR `The hermit ignores you.` ch2
    END
END
```

This script reacts differently based on what the player says. “Help” gets
instructions, “secret” triggers a special message, and any other statement is
ignored.

### Cursed weapon (ON_WEAR)

```
TRIGGER: ON_WEAR ()
SET_INT_VALUE int1 ch1.level
SET_INT_VALUE int2 60
IF_INT_LESS int1 int2
BEGIN
    SEND_TO_CHAR `The blade rejects such a feeble wielder!` ch1
    RETURN_FALSE                   ; block the wear
END
SEND_TO_CHAR `Dark power surges through you.` ch1
SET_INT_VALUE ch1.hit 1            ; drop HP to 1 as a drawback
```

If a player under level 60 tries to wear the sword, the script denies them and
returns `FALSE`. Otherwise the weapon can be worn, but it punishes the wearer by
setting their HP to 1 (demonstrating careful use of `SET_INT_VALUE`).

### Delayed response using `DO_WAIT`

```
TRIGGER: ON_HEAR_SAY ()   ; str1 contains spoken text
ASSIGN_STR `HELP` -> str2
IF_STR_CONTAINS str1 str2
BEGIN
    SEND_TO_CHAR `Give me a moment to prepare.` ch1
    DO_WAIT 10             ; pause ~1 second per pulse
    LOAD_OBJ 7001 -> ob1
    OBJ_TO_CHAR ob1 ch1
    DO_GIVE ch1 ch2 ob1
END
```

When someone says “help”, the mob acknowledges, pauses via `DO_WAIT`, then loads
and gives a quest item. After `DO_WAIT`, all variables reset to zero, so the
script must use assignments again (here we rely on `ch1`/`ch2` still being set
when the pause ends; if your script depends on other pointers re-fetch them at
the top of the block).

### Advanced: Checkpoint gatekeeper (multiple commands)

```
TRIGGER: ON_BEFORE_ENTER ()
ASSIGN_STR `checkpoint pass` -> str1
ASSIGN_INV 9101 -> ob1 ch2 int1     ; look for the pass in entrant inventory
IF_INT_TRUE int1
BEGIN
    DO_SAY `Pass verified, you may travel on.` (ch1)(null)
    DO_GIVE ch2 ch1 ob1              ; optional: collect the pass
    LOAD_OBJ 9300 -> ob2             ; issue a stamped pass
    OBJ_TO_CHAR ob2 ch1
    DO_GIVE ch1 ch2 ob2
    RETURN_FALSE                      ; stop the engine from moving them yet
    TELEPORT_CHAR_XL ch1.room ch2     ; move them into the guarded checkpoint
    SEND_TO_ROOM `The guard waves someone through the gate.` ch1.room
END_ELSE_BEGIN
SEND_TO_CHAR `No pass, no entry. Return when you have authorization.` ch2
RETURN_FALSE                          ; block the move
END
```

Breakdown:
1. `ASSIGN_INV` searches the entrant’s inventory and sets `int1` to `1` if the
   pass (vnum 9101) exists.
2. `IF_INT_TRUE` gates the rest of the logic; successful players get dialogue,
   optionally surrender their old pass, receive a stamped version, and are
   teleported using `TELEPORT_CHAR_XL`.
3. Both branches return `FALSE` to control movement manually; the success branch
   handles the teleport and messaging, while the failure branch simply blocks.

### Advanced: Boss enrages and spawns adds

```
TRIGGER: ON_DAMAGE ()
SET_INT_VALUE int1 ch1.hit                 ; boss HP
SET_INT_VALUE int2 ch1.maxhit
SET_INT_DIV int3 int1 int2                 ; percentage in int3
SET_INT_VALUE int4 0
IF_INT_LESS int3 25
BEGIN
    IF_INT_FALSE int4
    BEGIN
        ASSIGN_STR `The shaman screams for aid!` -> str1
        SEND_TO_ROOM str1 ch1.room
        LOAD_MOB 14012 -> ch3               ; summon an add
        TELEPORT_CHAR_XL ch1.room ch3
        DO_SAY `Protect the master!` (ch3)(null)
        SET_INT_VALUE int4 1                ; flag so we don’t spawn endlessly
    END
END
IF_INT_LESS int3 10
BEGIN
    SEND_TO_ROOM `Dark flames erupt from the shaman.` ch1.room
    SET_INT_VALUE ch2.hit 1                 ; drop attacker to 1 HP
END
```

Breakdown:
1. The script computes remaining HP percentage and stores it in `int3`.
2. When the boss drops below 25%, it checks a local flag `int4` to ensure the
   add spawns only once, then loads a helper mob and teleports it into the room.
3. At 10% HP, the boss unleashes a final blast by forcing the attacker’s hit
   points to `1`.
4. Because `int4` resets to zero after the script exits, this trigger works each
   time the fight restarts—no persistent state is carried between battles.

Armed with this guide plus the `shape room`, `shape object`, and `shape zone`
references, builders can safely craft complex behaviours without touching the
legacy documentation.
