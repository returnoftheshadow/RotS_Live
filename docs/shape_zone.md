# Shape Zone Command

Zone shaping controls how rooms, mobiles, and objects reset in your area. Unlike
rooms/objects/mobs (which are read once), the zone file is reinterpreted every
time the zone regens, so mistakes can break live spawns. This guide consolidates
all legacy shaping instructions into one reference you can rely on when editing
or building regen scripts.

## Prerequisites

- **Zone access** – you need explicit permission for the zone you are editing
  (granted via `get_permission`). Use `shape zone <zone#>` or `shape zone current`
  while standing in the area.
- **Reset testing** – after saving, use `zreset <zone#>` (or `zreset current`) to
  validate the file. Watch for errors in the log (see
  [Error messages](#error-messages)).
- **General builder rules** – keep the zone description/map/name up to date,
  follow the room writing guidelines documented in `shape_room.md`, and avoid
  ad-hoc hacks when a real zone command (`A`, `L`, etc.) suffices.

## Zone metadata commands

Use the following slash commands to maintain the header info:

| Command | Field | Notes |
|---------|-------|-------|
| `/14` | Add owner | Area god and up. `Enter owner idnum to add:` takes the player's idnum, not their name. `0` is refused. |
| `/15` | Remove owner | Area god and up. `Enter owner idnum to remove:`; `0` is refused. |
| `/16` | Map coordinates | Greater god and up. `x y` on the world map; the prompt shows the current pair and blank keeps it. |
| `/17` | Map symbol | Greater god and up. `Enter zone map symbol, one character [x]:`. |
| `/20` | Zone name | Short label shown in admin tools. |
| `/21` | Zone description | High-level summary of the area (include designer, theme, connections). |
| `/22` | Zone map | ASCII map block or overview text. |
| `/23` | Reset time (minutes) | `Enter reset time in minutes [N]:`. The zone ages about once per real minute; blank keeps the current value. |
| `/24` | Reset mode | See table below; defaults to `2`. The prompt lists the modes; anything outside 0-3 is refused (`Reset mode must be 0-3. dropped.`). |
| `/25` | Zone level | `Enter zone level [N]:`. Used by early-warning systems (e.g., “high-level darkie in zone” alerts). Coordinate with the implementor who assigned the zone. |
| `/51` | Show metadata | Prints the current name/description/map for review. |

### Reset modes (`/24`)

| Value | Behaviour |
|-------|-----------|
| `0` | Never reset automatically. |
| `1` | Reset when empty (no PCs) and timer expired. |
| `2` | Reset when timer expires, even if players are present (default). |
| `3` | Reset when (empty AND timer expired) OR (occupied AND timer has been overdue 3×). |

### Zone level (`/25`)

This is not a difficulty slider; it feeds the early-warning system that notifies
factions when enemy raids enter a zone. Use the level provided by the zone
owners/implementors.

## Editing workflow

1. **Enter the interface**: `shape zone current` (or `shape zone <zone#>`).
2. **Preview commands for your current room**: `/current`. Move to another room
   and repeat `/current` to change the focus or clear it to browse the full list.
3. **List all commands**: `/50`.
4. **Show the numeric menu**: `/0`.
5. **Navigate**:
   - `/1` – display the previous/current/next commands for quick context.
   - `/6` – move to the next command (filtered by current room if set).
   - `/7` – move to the previous command.
   - `/8` – jump to a specific command number (blank stays on the current one).
   - `/9` – delete the current command (`REMOVE current command?<yn>:`).
   - `/12` – set the "current room" filter to a room vnum (`0` turns it off).
   - `/13` – swap the current command with the next one.
6. **Create commands**:
   - `/10` – insert a blank command after the current entry.
   - `/11` – insert a blank command before the current entry.
   - `/3` – choose the command letter. The prompt lists the nine letters with
     what each does (`M O G E P D L K A`). A blank answer prints `Nothing
     entered. dropped.` Changing the letter of an existing row resets its
     numbers to 0, because the old numbers mean something else under a new
     letter.
   - `/4` – fill in the numbers. The prompt names every field for that letter
     (for example `if_flag mob_vnum room_vnum max_world chance% xp%
     max_alive_line trophy` for `M`), lists what the codes mean, and shows the
     row's `Current:` values. A blank answer keeps them (`Values kept.`) and
     goes on to the comment. Typing fewer numbers than asked keeps the rest.
   - `/5` – add/edit the one-line comment (`blank = keep, %q = empty`).
7. **Use masks**: `/2` asks `Enter list mask: letter if_flag arg1 arg2 arg3
   arg4 arg5 arg6 arg7 ('*' = any)` and then lists only matching commands.
   Handy for locating all `O` commands in a room.
8. **Save / exit**: `/save` writes the `.zon` file (after copying the old one
   to `world/zon/oldzons/`), `/implement` syncs the live world, `/done` does both
   and exits, `/free` abandons the session without saving. If the save fails,
   `/done` stops and keeps your edits (`Not saved - still shaping. Fix the
   problem and /done again, or /free to discard.`). `/implement` lists every
   command that names a room, mobile or object that does not exist, and says
   how many commands were disabled (see [Error messages](#error-messages)).
   Mobiles the zone already loaded are counted against the new command list,
   so `max_alive_line` limits stay right after an implement; a tamed mobile no
   longer counts for the line that loaded it.
9. **Not available here**: `/add` (`A zone file holds one zone, so there is
   nothing to add it to.`), `/delete` (`Zones cannot be deleted here. If you
   want a zone removed, talk to the Implementors.`) and `/create` (`Zone
   cannot be created that simple.`). Zone files are made outside the game.
10. **Disabled or note rows**: a line starting with `*` in the file is kept as
   its own row and written back exactly as it was on `/save`. It does nothing
   at reset, but its if-flag still counts the way boot reads it.

> Tip: zone commands run as a script every reset, top to bottom. Keep related
> commands grouped (load mob → kit/equip/give → tweak with `A` commands) and use
> `/5` comments liberally.

## If-flag reference

Every command except `A` uses an if-flag to decide whether it runs. The base
values are:

| Flag | Meaning |
|------|---------|
| `0` | Perform unconditionally. |
| `1` | Perform if the previous command ran. |
| `2` | Perform if the last mobile was loaded. |
| `4` | Perform if the last object was loaded. |
| `8` | Invert the conditions (turns “if previous ran” into “if previous did *not* run”). |
| `9` | Shorthand for “previous command did NOT run” (`1 + 8`). |
| `10` | Last mobile was NOT loaded (`2 + 8`). |
| `12` | Last object was NOT loaded (`4 + 8`). |
| `16` | Total whitie fame > darkie fame. |
| `24` | Total whitie fame ≤ darkie fame (`16 + 8`). |
| `32` | Total darkie fame > whitie fame. |
| `40` | Total darkie fame ≤ whitie fame (`32 + 8`). |
| `64` | Sun is up. |
| `72` | Sun is not up (`64 + 8`). |

Because the flag is a bitvector, add the values you need (except the “invert”
options, which already include 8). Example: `if_flag 3` means “run if previous
command succeeded AND the last mobile loaded”.

## Zone command reference

Each command follows a strict argument order. Comments are optional but
strongly recommended.

### `D` — Door state

```
D <if_flag> <room> <direction> <state>
```

- Directions: `0` N, `1` E, `2` S, `3` W, `4` Up, `5` Down.
- States: `0` open, `1` closed, `2` closed & locked.

Set each doorway once; the paired exit inherits the state automatically.

### `M` — Load mobile

```
M <if_flag> <mob_vnum> <room_vnum> <max_in_world> <prob%> <difficulty%> <max_line> <trophy>
```

- `max_in_world`: `0` means no limit.
- Keep `prob%` at `100` unless you want a rare spawn.
- `difficulty%` (xp%) modifies XP reward (leave at `100` unless asked; `0` is
  treated as normal).
- `max_line` (max_alive_line) is how many mobs loaded by this line may be alive
  at once; `0` means no limit.
- `trophy` toggles trophy protection (`1` typical, `0` for no trophy).

After loading, use `K`, `E`, or `G` to equip the mobile.

### `O` — Load object in a room

```
O <if_flag> <obj_vnum> <room_vnum> <max_in_world> <prob%> <max_in_room>
```

`max_in_room` limits how many of this object may exist in that room at once.

### `P` — Put object into object

```
P <if_flag> <room_vnum> <obj_vnum> <container_vnum> <max_in_world> <prob%> <max_in_container>
```

- If `<room_vnum>` or `<container_vnum>` is `-1`, the command uses the last
  loaded object (great for stuffing loot into a chest you just spawned with
  `O`). `0` is a real vnum, not "last object".
- `max_in_container` limits how many copies land inside that container.
- Remember to load the container first with an `O` or `G/E/K` command.

### `K` — Kit (auto-wear)

```
K <if_flag> <obj1> <obj2> ... <obj7>
```

Loads up to seven objects onto the last mobile and performs “wear all”. Use
`if_flag 2` so it runs only if the mobile loaded successfully. Leave unused
slots as `-1` (`0` is a real object vnum).

### `E` — Equip (slot-specific)

```
E 2 <obj_vnum> <wear_slot> <max_in_world> <prob%>
```

Always use `if_flag 2`. `wear_slot` values:

| Slot | Position | Slot | Position |
|------|----------|------|----------|
| 0 | Light source | 9 | Hands |
| 1 | Right finger | 10 | Arms |
| 2 | Left finger | 11 | Shield |
| 3 | Neck (slot 1) | 12 | About body (cloak) |
| 4 | Neck (slot 2) | 13 | Waist |
| 5 | Body | 14 | Right wrist |
| 6 | Head | 15 | Left wrist |
| 7 | Legs | 16 | Wield |
| 8 | Feet | 17 | Hold |
|  |  | 18 | Back |
|  |  | 19-21 | Belt 1-3 |

Use `E` when you need per-slot probabilities; otherwise `K` is simpler.

### `G` — Give without wearing

```
G <if_flag> <obj_vnum> 0 <max_in_world> <prob%>
```

Adds the object to the last mobile’s inventory (rings, potions, quest items). A
common pattern is `M` → `G` (key) → `D` (lock door).

### `A` — Auxiliary commands

`A` ignores if-flags and uses a subcommand number:

| Subcmd | Effect |
|--------|--------|
| `1` | Set gold on the last mobile to value `(b)`. |
| `2` | Set difficulty coefficient (XP modifier) on the last mobile. |
| `3` | Set trophy coefficient on the last mobile. |
| `4` | Make the last mobile follow the Nth mobile in the room (`b`). |
| `5` | Set the last object's value number `b` (0-4) to `c`. |
| `6` | Change what later if-flags see: bit 1 of `b` marks the last command as not run (without it, as run), bit 2 forgets the last mobile, bit 4 forgets the last object. |
| `7` | Assign a special procedure ID to the last mobile. |
| `8` | Toggle mobile flags on the last mobile (`A 8 1 <flag#>` sets, `A 8 0 <flag#>` clears; flag numbers match the `shape mob` table). |
| `9` | Set the butcher item vnum for the last mobile. |
| `10` | Remove the last loaded mobile (`b=1`) or object (`b=2`). |
| `11` | Set race aggression for the last mobile; pass the bitmask that matches the target `RACE_*` value from `src/structs.h` (e.g., `1 << RACE_HUMAN`). |
| `12` | Set the script number for the last mobile. |

Use these sparingly; most behaviour should come from `shape mob` definitions.

### `L` — Select an existing entity

```
L <if_flag> <mode> <room_vnum> <mob_or_obj_vnum> <ordinal>
```

Modes:

| Mode | Meaning |
|------|---------|
| `0` | Select the Nth instance (`ordinal`) of the given mob vnum in the room. |
| `1` | Select the Nth instance of the object vnum in the room. |
| `2` | Select the Nth object inside the last loaded object’s contents. |
| `3` | Select the Nth object in the last loaded mobile’s inventory. |
| `4` | Select the object worn in wear slot `ordinal` on the last mobile. The vnum must match, or `-1` for any object in that slot. |
| `5` | Select the Nth instance of the mob vnum anywhere in the world. The room is ignored. |
| `6` | Select the Nth instance of the mob vnum anywhere in the zone that contains `room_vnum`. |

`L` updates the “last mobile/object” pointers so subsequent commands (like `A`,
`E`, or `G`) operate on existing instances. Example: use `L` to find mounts in a
stable, then `M` with if-flag `10` to spawn replacements only when needed.

### Other command letters

- `N`, `H`, `Q` are reserved variants of `M`/`E` for special systems; consult an
  implementor before using them.

## Examples

### Guard with a partial kit and rare drop

```
M 0 1205 1201 0 100 100 2 1      ; load two town guards in room 1201
K 2 4001 4002 -1 -1 -1 -1 -1    ; wear tunic + helm
E 2 4003 16 0 100                ; wield spear (100%)
E 2 5200 11 0 15                 ; 15% chance to wear a tower shield
G 2 6001 0 0 10                  ; give jail key (10%)
```

`K` outfits the basics, `E` handles per-slot probabilities, and `G` provides an
optional key. All commands use `if_flag 2` so they only run when the guard
actually loaded.

### Locked chest with guaranteed loot

```
O 0 7001 1610 0 100 1            ; place an oak chest in room 1610
D 0 1610 1 2                     ; keep the eastern door locked
P 0 1610 7105 7001 0 100 1       ; put the ritual scroll inside the chest
P 0 -1 7302 -1 0 50 3            ; 50% chance to add up to 3 gems (uses last object)
```

The first `P` references the chest explicitly; the second sets both room and
container to `-1`, so it reuses the “last loaded object” (the chest). Adjust the
probabilities and counts to taste.

### Mount maintenance via `L`

```
L 0 0 1820 5001 2                ; find the 2nd warhorse in room 1820
M 10 5001 1820 0 100 50 0 1      ; if that horse is missing, spawn a new one
```

The `L` command updates the “last mobile” pointer. The `M` command uses
`if_flag 10` (“last mobile NOT loaded”) so it only fires when the mount count
dips below the desired number.

## Error messages

A command that names a room, mobile or object that does not exist is reported
with its zone number and command number (its position in the zone's command
list, counting from 1). The line also goes to the log, and to anyone at area god or above
with `syslog` set to `normal` or higher. On `/implement` the builder sees the
lines directly as well (once, even if they already see syslog).

**When the zone is implemented, and at boot:**

```
ZONE ERROR: zone #23, command 39 (M): mobile vnum 2156 not found - command disabled
ZONE ERROR: zone #73, command 6 (K): object vnum 7312 not found
```

- `- command disabled`: the vnum the command needs (the mob of `M`, the object
  of `O`/`G`/`E`/`P`, the room of `M`/`O`/`D`, the target of `L`/`A`) does not
  exist, so the command is turned off and does nothing until it is fixed and
  implemented again. `/implement` ends with "N command(s) were DISABLED".
- No suffix: an optional vnum (a `K` slot, the room or container of `P`, the
  room of `L`) does not exist. The command still runs.
- `unknown command - does nothing`: the command letter is not one the zone reset
  runs (`N`, `X`, `H` and `Q` are read from the file but never run). The row is
  skipped at every reset. A row with no letter (`.`) is empty and not reported.
- Unused slots (`0`) are never reported.

**At every zone reset, each time it happens:**

| Message | Meaning |
|---------|---------|
| `(K): object not loaded` | The mob loaded, but a `K` slot names an object that does not exist, so it goes without it. |
| `(P): room not found - put in last object loaded` | The object loaded, but into the last object loaded instead of the named room. |
| `(P): container not found - put in last object loaded` | Same, for a container that does not exist. |
| `(P): room not found - nothing loaded`, `(P): container not found - nothing loaded` | As above, and there was no last object to fall back on. |
| `(L): room not found - searched room 0 instead` | The `L` room does not exist; the search looked at room 0. |
| `(<letter>): negative room lookup while running this command` | Something the command set off (loading or equipping a mob, and so on) looked up a room that does not exist. |

A command whose if-flag, max count or probability stopped it is not an error
and is not reported.

**Vnums in the file are 16-bit.** A negative number reads back as 65536 minus
it (`-4425` becomes `61111`), and a vnum of `65535` is treated as empty and never
reported. The messages show the number as it was read.

## Best practices

- Keep command comments (`/5`) descriptive so future builders understand intent.
- Group related commands: load mobile → equip → tweak with `A` → door state.
- Use `max_in_world` for unique boss mobs/objects.
- Keep probabilities explicit (even when 100%) to avoid confusion.
- After edits, run `zreset <zone#>` and walk the area to verify spawns, doors,
  and containers behave as expected.

With rooms, objects, zones, and programs documented here, these Markdown guides
now act as the source of truth for builders.
