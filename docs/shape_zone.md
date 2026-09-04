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
  validate the file. Watch for errors in the log.
- **General builder rules** – keep the zone description/map/name up to date,
  follow the room writing guidelines documented in `shape_room.md`, and avoid
  ad-hoc hacks when a real zone command (`A`, `L`, etc.) suffices.

## Zone metadata commands

Use the following slash commands to maintain the header info:

| Command | Field | Notes |
|---------|-------|-------|
| `/20` | Zone name | Short label shown in admin tools. |
| `/21` | Zone description | High-level summary of the area (include designer, theme, connections). |
| `/22` | Zone map | ASCII map block or overview text. |
| `/23` | Reset time (ticks) | Defaults to `10`. Each tick is ~5 minutes. |
| `/24` | Reset mode | See table below; defaults to `2`. |
| `/25` | Zone level | Used by early-warning systems (e.g., “high-level darkie in zone” alerts). Coordinate with the implementor who assigned the zone. |
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
   - `/8` – jump to a specific command number.
   - `/9` – delete the current command (prompts for `y/n`).
6. **Create commands**:
   - `/11` – insert a blank command before the current entry.
   - `/12` – insert after the current entry.
   - `/3` – choose the command letter (`M`, `O`, `G`, `E`, `P`, `D`, `A`, `L`, `K`, etc.).
   - `/4` – fill in the numeric arguments (prompts vary by command).
   - `/5` – add/edit the one-line comment.
7. **Use masks**: `/2` lets you filter the command list by letter, room, or
   arguments. Use `*` to treat a field as “any”. Handy for locating all `O`
   commands in a room.
8. **Save / exit**: `/save` writes the `.zon` file (after backing up),
   `/implement` syncs the live world, `/done` does both and exits, `/free`
   abandons the session without saving.

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

- Set `max_in_world` to `0` for non-unique mobs.
- Keep `prob%` at `100` unless you want a rare spawn.
- `difficulty%` modifies XP reward (leave at `100` unless asked).
- `max_line` controls how many copies this line may produce during one regen.
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

- If `<room_vnum>` and `<container_vnum>` are `0`, the command uses the last
  loaded object and its location (great for stuffing loot into a chest you just
  spawned with `O`).
- `max_in_container` limits how many copies land inside that container.
- Remember to load the container first with an `O` or `G/E/K` command.

### `K` — Kit (auto-wear)

```
K <if_flag> <obj1> <obj2> ... <obj7>
```

Loads up to seven objects onto the last mobile and performs “wear all”. Use
`if_flag 2` so it runs only if the mobile loaded successfully. Leave unused
slots as `0`.

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
| `5` | Override object value `b` with `c` for the last object. |
| `6` | Manually mark the last command/mobile/object as not executed/loaded (bitmask 1/2/4). |
| `7` | Assign a special procedure ID to the last mobile. |
| `8` | Toggle mobile flags on the last mobile (`A 8 1 <flag#>` sets, `A 8 0 <flag#>` clears; flag numbers match the `shape mob` table). |
| `9` | Set the butcher item vnum for the last mobile. |
| `10` | Remove the last loaded mobile (`b=1`) or object (`b=2`). |
| `11` | Set race aggression for the last mobile; pass the bitmask that matches the target `RACE_*` value from `src/structs.h` (e.g., `1 << RACE_HUMAN`). |

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
| `4` | Select the object worn in the Nth slot on the last mobile (optionally restrict to a specific vnum). |

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
K 2 4001 4002 0 0 0 0 0          ; wear tunic + helm
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
P 0 0 7302 0 0 50 3              ; 50% chance to add up to 3 gems (uses last object)
```

The first `P` references the chest explicitly; the second sets both room and
container to `0`, so it reuses the “last loaded object” (the chest). Adjust the
probabilities and counts to taste.

### Mount maintenance via `L`

```
L 0 0 1820 5001 2                ; find the 2nd warhorse in room 1820
M 10 5001 1820 0 100 50 0 1      ; if that horse is missing, spawn a new one
```

The `L` command updates the “last mobile” pointer. The `M` command uses
`if_flag 10` (“last mobile NOT loaded”) so it only fires when the mount count
dips below the desired number.

## Best practices

- Keep command comments (`/5`) descriptive so future builders understand intent.
- Group related commands: load mobile → equip → tweak with `A` → door state.
- Use `max_in_world` for unique boss mobs/objects.
- Keep probabilities explicit (even when 100%) to avoid confusion.
- After edits, run `zreset <zone#>` and walk the area to verify spawns, doors,
  and containers behave as expected.

With rooms, objects, zones, and programs documented here, these Markdown guides
now act as the source of truth for builders.
