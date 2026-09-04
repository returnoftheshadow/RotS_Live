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
