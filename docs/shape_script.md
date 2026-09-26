# Shape Script Command

Scripts are short command sequences that attach to mobiles (and, partially, to
objects) and execute when a trigger fires: someone enters a room, speaks, wears
an item, etc. They are stored in `world/scr/<zone>.scr`. The `.scr` files are
created outside the game; the scripts inside them are edited in-game. This
guide covers the same ground as the in-game `man script` pages (`scr_tbl`).

## Prerequisites

- Immortal access plus zone permissions (`get_permission(zone, ch)`).
- A reserved script vnum (matching the zone number, e.g., script 4205 lives in
  `world/scr/42.scr`). The zone's `.scr` file must already exist; the editor
  never creates files. Coordinate with an implementor if unsure.
- Target mobile: scripts currently run on mobiles; object hooks exist but only
  a subset of triggers honor them. Assign the script vnum to a mobile via
  `shape mob /38` and clear the `SPECIAL` flag unless combining it with a
  hard-coded proc.

## Working with `shape script`

1. `shape script <vnum>` loads a script from its zone's `.scr` file. If the
   vnum is not in the file yet, the editor starts it with one empty row
   (`could not find script #N, created it.`); `/save` adds it to the file.
   `shape script new <zone>` is disabled (see below).
2. Use the numeric menu (`/0`):
   - `/1` show previous/current/next row.
   - `/3` change the current row's command type. It lists every command by
     group; type the full name (`DO_SAY`). A blank answer keeps the type; an
     unknown name gives `Unknown command type. dropped.` and keeps it too.
     `/3` goes on to `/4`, and `/4` to `/5` for most commands.
   - `/4` edit the parameters of the current row (see
     [Entering parameters](#entering-parameters)).
   - `/5` edit the row's text. The prompt says what the text is for:
     `MESSAGE` for `SEND_TO_CHAR`/`SEND_TO_ROOM`/`SEND_TO_ROOM_X` (the text the
     players see; `%s` inserts the text value, `%%` prints a `%`; `%q` is refused
     with `The message can't be empty.`), `TEXT` for `DO_SAY`, `DO_YELL`,
     `DO_EMOTE`, `DO_SOCIAL`, `ASSIGN_STR` and the `IF_STR_*` tests (the same
     text `/4` asked for), and `COMMENT, shown in the list only` for the rest.
   - `/6` / `/7` move to the next/previous row.
   - `/8` jump to a row number (blank keeps the current row).
   - `/9` delete the current row. There is no confirmation.
   - `/10` insert a new row after the current one. `/11` inserts before.
   - `/13` swap the current row with the next.
   - `/20` change the script name, a one-line title (`%q` is refused with
     `The script name can't be empty.`); `/21` change the script description
     (the `%e`/`%q`/`%f` text editor).
   - `/50` list the entire script. `/51` show the name and description.
   - `/2` and `/14` do nothing. `/12` is disabled (it was a room filter left
     over from the zone editor).
3. Text rows (`/5`, `/20`, the say/emote/social/store/compare text) are single
   lines: a blank line keeps the old text, `%q` empties it, `#` becomes `+` and
   `~` becomes `-`. Only `/21` opens the multi-line editor.
4. `/save` writes the script back to disk (after backing it up to
   `world/scr/oldscrs/`). `/implement` copies the edited version into the live
   `script_table` if the script existed when the MUD booted; a script added
   since the last boot needs a reboot. `/done` runs `/save`, `/implement`,
   then `/free`. If the save fails, `/done` stops and keeps you shaping
   (`Not saved - still shaping. Fix the problem and /done again, or /free to
   discard.`). `/implement` lists every line that names a room, mobile or
   object that does not exist (see [Error messages](#error-messages)).
5. `/free` abandons changes and exits shaping. Always `/save` first if you care
   about the edits.
6. Disabled commands. These print exactly what was typed, e.g.
   `"/delete" has been disabled due to a bug.`, and do nothing:
   - `/delete` (it rewrote the zone file to drop the script),
   - `/new <zone>` and `shape script new <zone>` (they picked the file's last
     vnum + 1; script vnums are set up outside the game),
   - `/add <anything>` (plain `/add` still saves the loaded script),
   - `/12`.

### Entering parameters

`/4` asks for the values the command needs, one prompt per step. Each prompt
names the positions in order and gives an example, then shows the row's
current values:

```
Enter DO_GIVE: giver receiver object  e.g. ch1 ch2 ob1
  (giver must carry it; the object variable is cleared after)
Current: ch1 ch2 ob1   (blank = keep)
```

- Type the values separated by spaces, no commas. Case does not matter.
- A blank answer keeps the current values. Fewer values than asked set the
  missing ones to 0; extra words are ignored.
- A word that is not a variable and not a number is refused, and nothing
  changes: `Unknown value: ch1, - dropped.`
- Positions that take a typed number (a vnum, an equipment slot, a direction,
  a pulse count, a zone number) refuse variable words: `Not a number: rm1 -
  dropped.` (Typing `rm1` there used to store its code, 700, as the number.)
- `ASSIGN_STR` only accepts `str1`-`str3` or `obN.name`
  (`Must be str1-3 or obN.name. dropped.`).
- Commands with a second step (`SET_INT_VALUE`, `SET_EXIT_STATE`,
  `PAGE_ZONE_MAP`, `EQUIP_CHAR`, `DO_SAY`, `DO_YELL`, `DO_EMOTE`, `DO_SOCIAL`,
  the `IF_STR_*` tests, `ASSIGN_STR`) ask the next part straight after. A
  refused answer ends the chain; type `/4` again.
- `EQUIP_CHAR` stores the vnums in the order typed. Rows saved before this
  change keep their old order (the first vnum last).

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
| Rooms (`rm1`, `rm2`, `rm3`) | Codes `700/800/900` | `.name`. Only `rm1` is ever set (by the mob `ON_ENTER`/`ON_BEFORE_ENTER` triggers); typing `rm2` stores `rm3`. Use `chN.room` for rooms, especially in object scripts. |
| Integers (`int1`, `int2`, `int3`) | Codes `953/954/955` | Arbitrary numeric storage. |
| Strings (`str1`, `str2`, `str3`) | Codes `950/951/952` | Arbitrary text storage. |

“Read-only” fields (e.g., `chx.level`, `obx.vnum`) cannot be set via
`SET_INT_VALUE`; only `int1`-`int3` and `chN.hit` can be written. `ASSIGN_STR`
stores literal text you type; no command copies one text variable into
another. The `IF_STR_*` tests compare a text variable with text you type.

Use the `get_param_text()` output shown in `/50` to verify that parameters map
to the intended variables.

## Triggers

Each trigger sits at the top of a script and provides entry points. Multiple
triggers can live in one script (e.g., ON_ENTER plus ON_HEAR_SAY).

| Trigger | When it fires | Variables | Return value |
|---------|---------------|-----------|--------------|
| `ON_ENTER` | After a character enters the room. | `ch1`=owner mob, `ch2`=entrant, `rm1`=room (for objects: `ob1` owner, `ch1` entrant). | Ignored. |
| `ON_BEFORE_ENTER` | Before the entrant is allowed in. | Same as `ON_ENTER`. | `FALSE` blocks entry; script must message the player. `TRUE` (default) lets them in. |
| `ON_DIE` | Just before the owner would die. | `ch1`=owner (nothing else is set). | `FALSE` prevents death (script must restore HP, send messages). |
| `ON_RECEIVE` | When somebody gives the owner an object. | `ch1`=recipient, `ch2`=giver (optional), `ob1`=item. | Ignored. |
| `ON_EXAMINE_OBJECT` | When someone examines the scripted object. | `ob1` owner, `ch1` examiner. | Ignored. |
| `ON_DAMAGE` | Before damage is applied. Works on mobiles and wielded objects. | `ch1`=victim, `ch2`=attacker, `ob1`=weapon (objects only). | `FALSE` cancels damage (script must handle messaging and HP updates); `TRUE` lets combat proceed. |
| `ON_DRINK` | When a character drinks from the scripted object. | `ch1` drinker, `ob1` container. | Ignored. |
| `ON_EAT` | When a character eats the scripted object. | `ch1` eater, `ob1` food. | Ignored. |
| `ON_HEAR_SAY` | Whenever someone in the room says something. | `ch1` owner, `ch2` speaker, `str1` spoken text. | Ignored. |
| `ON_HEAR_YELL` | Despite the name, when someone in the room *says* something: a `say` runs both the `ON_HEAR_SAY` and `ON_HEAR_YELL` parts of a script. Nothing calls it for yells. | Same as `ON_HEAR_SAY`. | Ignored. |
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
| `IF_STR_EQUAL`, `IF_STR_CONTAINS` | Compare a text variable with text typed in the row. `IF_STR_EQUAL` ignores case. `IF_STR_CONTAINS` uppercases the variable first, so type the text in CAPITALS. |
| `IF_IS_NPC` | Check whether a character variable refers to an NPC. |
| `IF_ROOM_SUNLIT` | Test whether a room (e.g. `ch1.room`) is currently lit by the sun. |
| `ABORT` | Stop execution and return `TRUE`. |
| `RETURN_FALSE` | Stop execution and return `FALSE`. Used to suppress default behaviour (e.g., block entry, cancel damage). |
| `DO_WAIT` | `ch1` waits N pulses (4 pulses = 1 second), then the script goes on from the next row. When it resumes only `ch1` is set again; every other variable is empty, so refetch anything you need. In object scripts `ch1` (the player) waits and the rest of the script does not run. |

### Player command emulation

| Command | Description |
|---------|-------------|
| `DO_SAY`, `DO_YELL` | Force a character to speak. `/4` asks for the text first (`%s` inserts the text value), then the speaker and the optional text value. |
| `DO_EMOTE` | Force an emote, text taken from the command’s string. |
| `DO_SOCIAL` | Run a social by name (`nod`, `wave`). The optional target is used only if it is in the same room. An unset performer skips the row. |
| `DO_GIVE` | Make a character give an object they carry to someone else. The object variable is cleared afterwards either way. |
| `DO_DROP`, `DO_REMOVE`, `DO_WEAR` | Manage equipment/inventory. Combine with `ASSIGN_INV`/`ASSIGN_EQ`. |
| `DO_WEAR` | Automatically finds the correct slot. Only if the character carries the object; an unset character or object skips the row. |
| `DO_HIT`, `DO_FLEE`, `DO_FOLLOW` | Force combat-related actions (use sparingly). |
| `DO_WAIT` | Already covered above—used for casting delays, etc. |

### Creating & removing

| Command | Description |
|---------|-------------|
| `LOAD_MOB` / `LOAD_OBJ` | Create a new mob/object by vnum and assign it to a `chx` or `obx`. Scripts are responsible for placing the object (use `OBJ_TO_CHAR`/`OBJ_TO_ROOM`). |
| `LOAD_OBJ_X` | Load another copy of the object in `ob1` (always `ob1`; the first value is unused) into the given object variable. If `ob1` is not set, the row is skipped. |
| `EQUIP_CHAR` | Load up to five vnums on a character and auto-wear them. Leave unused slots as `0`. |
| `EXTRACT_CHAR` | Remove a mobile from the game (inventory drops in room). Never use on PCs. |
| `EXTRACT_OBJ` | Remove an object from the game. |

### Setting and reading values

| Command | Description |
|---------|-------------|
| `ASSIGN_STR` | Store typed text in `str1`-`str3`, or rename an object with `obN.name`. Anything else is refused by the editor, and skipped when the row runs. |
| `ASSIGN_INV` / `ASSIGN_ROOM` | Locate an object by vnum in a character’s inventory or a room. `ASSIGN_INV` also looks inside the first carried container. The result integer (`int1`-`int3`) gets the **count** of top-level copies, not 1/0, so an item found only inside a container is assigned but counts 0. |
| `ASSIGN_EQ` | Fetch an object from a specific equipment slot (see Equipment table below). |
| `SET_INT_VALUE` | Set a variable (`int1`-`int3` or `chN.hit`) to a typed number. |
| `SET_INT_SUM`, `SET_INT_SUB`, `SET_INT_MULT`, `SET_INT_DIV`, `SET_INT_RANDOM` | `result = first op second`. The two operands must be integer variables (`int1`-`int3`, `chN.level/.hit/.race/.exp/.rank`, `obN.vnum`); a typed number does not work. The result must be `int1`-`int3` or `chN.hit`. Dividing by 0 gives 0. Use with caution when targeting live character stats. |
| `SET_INT_WAR_STATUS` | Store fame-war state in an integer (1 if whities lead, -1 darkies lead, 0 tie). |
| `SET_EXIT_STATE` | Open/close/lock a door (state 0=open, 1=closed, 2=closed+locked). Automatically mirrors to the reverse exit and sends default messages. Any direction 0-5 works, including north (0). |
| `CHANGE_EXIT_TO` | Change an exit’s destination room. The exit must already exist; a room with no exit in that direction is reported and the line is skipped. |

### Modifying the world

| Command | Description |
|---------|-------------|
| `OBJ_FROM_CHAR` / `OBJ_FROM_ROOM` | Remove an object from a character or room (no destination), only if it is carried by that character / lying in that room. Combine with `OBJ_TO_*` to teleport items. |
| `OBJ_TO_CHAR` / `OBJ_TO_ROOM` | Place an object in inventory or the room contents. The object should be nowhere (just loaded, or removed with `OBJ_FROM_*`). |
| `TELEPORT_CHAR`, `TELEPORT_CHAR_X`, `TELEPORT_CHAR_XL` | Move characters between rooms. `TELEPORT_CHAR` and `_X` take a typed room vnum; `TELEPORT_CHAR_XL` takes a room variable (`chN.room`). `TELEPORT_CHAR` also moves NPC followers in the same room. These commands do **not** send messages; scripts must narrate arrivals/departures. If the `TELEPORT_CHAR_XL` room is not set (an unset room variable, or the room of an unset character) it is reported and the line is skipped. |
| `RAW_KILL` | Kill a character immediately (silent corpse). Ensure your script handles messaging and loot placement if needed. |
| `GAIN_EXP` | Adjust experience (positive or negative). Handles level-up/level-loss automatically. |
| `PAGE_ZONE_MAP` | Show the shaped zone map to a character (handy for maze hints). The zone is a number (room vnum / 100). |

### Messaging

| Command | Description |
|---------|-------------|
| `SEND_TO_CHAR` | Send text to a single character. The message is the row's text (the `/5` `MESSAGE` prompt, asked right after `/4`); `%s` inserts the optional text value (`strx` or a `.name`), `%%` prints a `%`. |
| `SEND_TO_ROOM` | Broadcast to the room (including the source). |
| `SEND_TO_ROOM_X` | Broadcast to the room except a specific character (required). |

## Rows that are skipped instead of crashing

A row that needs a variable the trigger did not set, or cannot use, is
skipped and the script goes on. This used to crash the server for:
`ASSIGN_STR` into something that is not `str1-3`/`obN.name`, `DO_SOCIAL` with
an unset performer, `DO_WEAR` with an unset character or object, and
`LOAD_OBJ_X` with `ob1` unset.

## Error messages

A line that names something that does not exist is reported with the script
number and the line number shown in brackets by `/1` and `/50`. It goes to the
log, and to anyone at area god or above with `syslog` set to `normal` or
higher.

```
SCRIPT ERROR: script #2212, line 14 (load mob): mobile vnum 31099 not found
```

- **Vnum not found** (`load mob`, `load obj`, `equip char`, `assign inv`,
  `assign room`, `teleport`, `teleport x`, `change exit to`): reported at boot,
  on `/implement` (shown to the builder directly as well), and every time the
  line runs. The line still runs as it always did -- the message only tells
  you it is wrong.
- **`slot S is not 0-21`** (`assign eq`, `do remove`) and **`direction D is not
  0-5`** (`set exit state`, `change exit to`): the number typed into the line is
  out of range. Reported at boot, on `/implement`, and every time the line runs;
  the line is skipped.
- **`(change exit to): room R has no exit D`**: the room has no exit in that
  direction (or the direction is not 0-5). The line is skipped.
- **`(teleport xl): room not found`**: the room parameter is not set. The line
  is skipped.
- **`line N: negative room lookup`**: something the line set off looked up a
  room that does not exist.

Run-time messages repeat every time the line runs, so a busy script with a bad
line shows up often -- that is deliberate.

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
exists in-world, then gifts it to the visitor.

### Blocking entry by race

```
TRIGGER: ON_BEFORE_ENTER ()
SET_INT_VALUE int1 11          ; RACE_URUK
IF_INT_EQUAL ch2.race int1
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
SET_INT_VALUE int2 50
IF_INT_LESS ch2.level int2
BEGIN
    SEND_TO_CHAR `The blade fizzles before it harms you.` ch1
    SEND_TO_ROOM_X `The cursed sword fizzles.` ch1.room ch1
    RETURN_FALSE                 ; cancel damage
END
```

When the wielder (`ch2`) is below level 50 the victim (`ch1`) takes no damage;
the script handles messaging and returns `FALSE` so the engine skips damage
processing.

## Safety tips

- Scripts lose all context when they finish or delay. After `DO_WAIT` only
  `ch1` is set again; reassign any other `chx`/`obx` you need.
- Returning `FALSE` shifts responsibility to your script: cancel door pulls,
  deliver alternate damage, or provide failure text as appropriate.
- Be mindful of destructive commands (`RAW_KILL`, `EXTRACT_CHAR`, `SET_INT_VALUE`
  on `ch.hit`). Test on a copy of the zone before deploying to live.
- Keep scripts small and readable. Use `/5` comments on each command to explain
  intent for future maintainers.

### Quest hand-off (ON_RECEIVE)

```
TRIGGER: ON_RECEIVE ()
IF_STR_EQUAL ob1.name `an ancient chalice`
BEGIN
    DO_SAY `Thank you! Here is your reward.` (ch1)(null)
    LOAD_OBJ 8001 -> ob2           ; reward token
    OBJ_TO_CHAR ob2 ch1
    DO_GIVE ch1 ch2 ob2
    ABORT                          ; done: keep the chalice
END
DO_SAY `I have no need for that.` (ch1)(null)
DO_GIVE ch1 ch2 ob1                ; hand back other items
```

Players who hand the NPC the “ancient chalice” receive a reward and the NPC
keeps the chalice; any other item is handed back. (`ON_RECEIVE` ignores the
return value: the item has already changed hands when the script runs.)

### Listener with branching responses (ON_HEAR_SAY)

```
TRIGGER: ON_HEAR_SAY ()
IF_STR_CONTAINS str1 `HELP`
BEGIN
    SEND_TO_CHAR `Gather three sigils and return to me.` ch2
    ABORT
END
IF_STR_CONTAINS str1 `SECRET`
BEGIN
    SEND_TO_ROOM `The hermit whispers a secret word.` ch1.room
    ABORT
END
SEND_TO_CHAR `The hermit ignores you.` ch2
```

This script reacts differently based on what the player says. “Help” gets
instructions, “secret” triggers a special message, and anything else is
ignored. The typed text is in capitals because `IF_STR_CONTAINS` uppercases
`str1` before searching. (Separate `IF` blocks with `ABORT` avoid nesting
`END_ELSE_BEGIN`, which does not work inside another `IF`.)

### Cursed weapon (ON_WEAR)

```
TRIGGER: ON_WEAR ()
SET_INT_VALUE int2 60
IF_INT_LESS ch1.level int2
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
IF_STR_CONTAINS str1 `HELP`
BEGIN
    DO_SAY `Give me a moment to prepare.` (ch1)(null)
    LOAD_OBJ 7001 -> ob1
    OBJ_TO_CHAR ob1 ch1
    DO_GIVE ch1 ch2 ob1
    DO_WAIT 12             ; 3 seconds (4 pulses = 1 second)
    DO_SAY `Use it well.` (ch1)(null)
END
```

When someone says “help”, the mob acknowledges, loads and gives a quest item,
then pauses via `DO_WAIT`. Everything that needs `ch2` or `ob1` happens before
the wait: when the script resumes only `ch1` (the mob) is set again.

### Advanced: Checkpoint gatekeeper (multiple commands)

```
TRIGGER: ON_BEFORE_ENTER ()
ASSIGN_INV 9101 -> ob1 ch2 int1     ; look for the pass in entrant inventory
IF_INT_TRUE int1
BEGIN
    DO_SAY `Pass verified, you may travel on.` (ch1)(null)
    DO_GIVE ch2 ch1 ob1              ; optional: collect the pass
    LOAD_OBJ 9300 -> ob2             ; issue a stamped pass
    OBJ_TO_CHAR ob2 ch1
    DO_GIVE ch1 ch2 ob2
    TELEPORT_CHAR_XL ch1.room ch2     ; move them into the guarded checkpoint
    SEND_TO_ROOM `The guard waves someone through the gate.` ch1.room
    RETURN_FALSE                      ; the engine does not move them itself
END_ELSE_BEGIN
SEND_TO_CHAR `No pass, no entry. Return when you have authorization.` ch2
RETURN_FALSE                          ; block the move
END
```

Breakdown:
1. `ASSIGN_INV` searches the entrant’s inventory and sets `int1` to the number
   of passes (vnum 9101) carried; `IF_INT_TRUE` is true for 1 or more.
2. `IF_INT_TRUE` gates the rest of the logic; successful players get dialogue,
   optionally surrender their old pass, receive a stamped version, and are
   teleported using `TELEPORT_CHAR_XL`.
3. Both branches return `FALSE` to control movement manually; the success branch
   handles the teleport and messaging first (rows after `RETURN_FALSE` never
   run), while the failure branch simply blocks.

### Advanced: Boss enrages and spawns adds

```
TRIGGER: ON_DAMAGE ()        ; on the boss: ch1 = the boss (being hit), ch2 = attacker
SET_INT_VALUE int1 200
IF_INT_LESS ch1.hit int1                   ; boss below 200 hit points
BEGIN
    SEND_TO_ROOM `The shaman screams for aid!` ch1.room
    LOAD_MOB 14012 -> ch3                  ; summon an add
    TELEPORT_CHAR_XL ch1.room ch3
    DO_SAY `Protect the master!` (ch3)(null)
END
SET_INT_VALUE int1 50
IF_INT_LESS ch1.hit int1
BEGIN
    SEND_TO_ROOM `Dark flames erupt from the shaman.` ch1.room
    SET_INT_VALUE ch2.hit 1                ; drop the attacker to 1 HP
END
```

Breakdown:
1. Comparisons need two integer variables, so the threshold goes into `int1`
   first (`IF_INT_LESS ch1.hit 200` would stop the script).
2. Below 200 hit points the boss summons a helper mob into its room. Nothing
   remembers that it already did so — scripts keep no state between runs, and
   there is no `maxhit` field or `int4` — so it summons on every hit while
   below the threshold; guard real scripts with a one-time mechanism such as a
   `LOAD_OBJ` marker checked with `ASSIGN_ROOM`.
3. Below 50 hit points the boss blasts its attacker down to 1 hit point.

Armed with this guide plus the `shape room`, `shape object`, and `shape zone`
references, builders can safely craft complex behaviours without touching the
legacy documentation.
