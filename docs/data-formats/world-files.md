# World text files (rooms, mobiles, objects, zones)

**Source files:** `src/db.cpp` (`index_boot:600`, `load_rooms:740`, `setup_dir:867`,
`load_mobiles:1253`, `load_objects:1469`, `fread_string:2525`), `src/zone.cpp`
(`load_zones:45`), `src/db.h` (prefixes/struct defs)
**Status:** 🟡 rooms, mobiles, objects, zones complete. Shop files (`.shp`), Mudlle
(`.mdl`)/scripts (`.scr`), and full zone-reset-command **semantics** are pending.

## Purpose
The persistent game world is a set of plain-text files under `lib/world/`, grouped by type.
They define rooms, NPC prototypes (mobiles), item prototypes (objects), and zones (vnum
ranges + reset scripts that populate the world). The game `chdir`s into `lib/` at startup
(`config.cpp:58 DFLT_DIR="lib"`) and loads each category via `index_boot()`.

This is a CircleMUD/DikuMUD lineage format (`#vnum` records, tilde-terminated strings,
tilde/`$` terminators) with substantial RotS-specific field additions — noted per section.

---

## Common conventions

### Directory layout & prefixes (`db.h:33-39`)
```
lib/world/wld/   rooms      (WLD_PREFIX "world/wld")
lib/world/mob/   mobiles    (MOB_PREFIX "world/mob")
lib/world/obj/   objects    (OBJ_PREFIX "world/obj")
lib/world/zon/   zones      (ZON_PREFIX "world/zon")
lib/world/shp/   shops      (SHP_PREFIX "world/shp")
lib/world/mdl/   mudlle     (MDL_PREFIX "world/mdl")
lib/world/scr/   scripts    (SCR_PREFIX "world/scr")
```

### Index file (`index_boot:642-665, 699-736`)
Each prefix directory contains an index file naming the data files to load, one per line,
terminated by a line containing `$`:
```
30.wld
31.wld
...
$
```
Filename is `index` normally, `index.min` in mini-mud mode (`-m`), `index.new` in new-mud
mode (`-n`) (`db.h:30-32` — `INDEX_FILE`/`MINDEX_FILE`/`NEWINDEX_FILE`).
**Boot order matters** (`boot_db:330-382`): scripts → zones → mudlle → **rooms → mobiles →
objects** → shops. Zones load before rooms because room→zone assignment uses zone vnum tops.

### Records
- A record starts with `#<vnum>` (an integer "virtual number").
- Mobile/object files end at vnum `>= 99999` or a token `$` (`db.cpp:1268, 1414, 1483`).
- Room files end with a record whose **name string begins with `$`** (`load_rooms:758`).
- `count_hash_records` (`db.cpp:583`) pre-counts `#` lines to size arrays.

### Strings — `fread_string` (`db.cpp:2525`)
A string spans one or more lines and is terminated by a `~`. Rules:
- Reading continues until a line whose last non-space char is `~`; the `~` is stripped.
- A leading all-blank line is discarded.
- Embedded line breaks are stored internally as `\r` (carriage return).
- Empty string = a lone `~`.

Example (4-line description):
```
You stand in a dim stone hall.  Cobwebs hang from
the ceiling and a cold draft comes from the north.
~
```

### Virtual vs real numbers
Files reference **virtual numbers** (vnums). After load, `renum_world`/`renum_zone_table`
convert them to **real** array indices (`db.cpp:926`, `zone.cpp:173`). On-disk = always vnums.

---

## Room file (`.wld`) — `load_rooms:740`, `setup_dir:867`

Per record, in order:
```
#<vnum>
<name>~
<description>~
<zone> <room_flags> <sector_type> <level>
<field>...
S
```
- **Line 4** is 4 ints. The first (`<zone>`) is a placeholder — the room's zone is derived
  from which zone's `top` vnum range contains this vnum (`load_rooms:770-780`), not from this
  field. The meaningful values are `room_flags` (bitvector), `sector_type`, and **`level`**
  (a RotS addition — room level). *(Before the zone table exists, only 3 ints are read.)*

Then zero or more fields, each introduced by a letter token (`load_rooms:814`):

| Tok | Meaning | Payload |
|-----|---------|---------|
| `D<n>` | Exit in direction `n` (0–5) | `setup_dir`: `<general_desc>~` `<keyword>~` then 4 ints: `exit_info key to_room exit_width` |
| `E` | Extra (look-at) description | `<keyword>~` `<description>~` |
| `F` | Persistent room affect (**RotS**) | 4 ints: `type location modifier bitvector` |
| `S` | End of this room | — |

- Direction index `n`: 0–5 = **N, E, S, W, U, D** (`structs.h:529-534`).
- `exit_info` = door-flag bitvector, `key` = key obj vnum, `to_room` = destination vnum
  (`-1`/`NOWHERE` = none), `exit_width` = **RotS** passage width (mounts/large mobs).
- `F` adds a permanent `affected_type` to the room (`load_rooms:826-851`): `type`,
  `location` (apply), `modifier` (used as spell level), `bitvector` OR'd with `PERMAFFECT`.

---

## Mobile file (`.mob`) — `load_mobiles:1253`

Per record:
```
#<vnum>
<name (keywords)>~
<short_descr>~
<long_descr>~
<description>~
<mob_action_flags>
<affected_by> <alignment> <type_letter>
```
`type_letter` selects the body that follows (`load_mobiles:1303-1397`):
- `N` ("new monster"): two extra strings first — `<death_cry>~` `<death_cry2>~` — then the
  full stat block below.
- `M`: the full stat block, no death cries.
- other (legacy): no stat block.

**Full stat block** (M and N), each line a fixed group of ints (exact order from the
`fscanf` calls — *file order, not struct order*):
```
<level> <OB> <parry> <dodge>
<hp_current> <hp_max>
<damage> <energy_regen>
<gold> <exp> <ignored_owner>
<position> <default_position> <sex> <race> <pref>
<weight> <height> <store_prog_number> <butcher_item> <corpse_num> <rp_flag>
<prof> <mana> <move> <bodytype>
<saving_throw>
<str> <int> <wil> <dex> <con> <lea>
<language> <perception> <resistance> <vulnerability> <script_number> <spirit> <will_teach>
```
Notes:
- `MOB_ISNPC` is force-set (`:1295`); files needn't include it.
- `corpse_num` is only consumed for `N` mobs (`:1349`).
- `language` indexes `language_skills[language-1]`; out-of-range → 0 (`:1378`).
- Most of these (energy/regen, OB/parry/dodge, perception, resistance/vulnerability,
  spirit, prof, languages, script_number, butcher_item, rp_flag) are **RotS additions**;
  stock Diku mobiles are far simpler.

---

## Object file (`.obj`) — `load_objects:1469`

Per record:
```
#<vnum>
<name (keywords)>~
<short_description>~
<description>~
<action_description>~
<type_flag> <extra_flags> <wear_flags>
<value0> <value1> <value2> <value3> <value4>
<weight> <cost> <cost_per_day>
<level> <rarity> <material> <script_number> <reserved>
```
Then, optionally:
- Zero or more `E` extra-description blocks: `E` then `<keyword>~` `<description>~`
  (`:1548`).
- Up to `MAX_OBJ_AFFECT` apply lines: `A` then 2 ints `<location> <modifier>` (`:1556`).

The record ends when the next token is `#` (next object) or `$` (EOF) — there is no `S`.
Notes:
- `value[0..4]` meaning depends on `type_flag` (weapon dice, container capacity, light
  duration, etc.) — to be enumerated in the Objects system doc.
- `level`, `rarity`, `material`, `script_number` are **RotS additions** (`:1531-1535`);
  a 5th int on that line is read but currently unused. The old `poisoned`/`poisondata`
  fields are commented out (`:1536-1540`).

---

## Zone file (`.zon`) — `load_zones:45`

Per record:
```
#<zone_number>
<name>~
<description>~
<map>~
<owner_id> <owner_id> ... 0          (owner list, terminated by 0; rest of line ignored)
<symbol> <x> <y> <level>             (%c %d %d %d)
<top>                                (highest room vnum belonging to this zone)
<lifespan>                           (minutes between resets)
<reset_mode>                         (0=never, 1=empty-only, 2=always — verify)
<command lines...>
S
```
The `description`, `map`, owner list, `symbol/x/y/level` are **RotS additions** (stock Diku
zones have only name, top, lifespan, reset_mode).

### Reset command lines (`load_zones:86-163`)
Each command is:
```
<cmd> <if_flag> <arg1> <arg2> <arg3> <arg4> <arg5> [<arg6> [<arg7>]]   <comment to EOL>
```
- `if_flag`: if nonzero, execute only if the previous command executed
  (DikuMUD-style chaining).
- Commands `M N X H E K Q` read **two** extra args (`arg6 arg7`); `P` reads **one** (`arg6`);
  others read five args (`load_zones:127-143`).
- `S` terminates the command list (`:98`).
- The trailing text on each line is a human comment (only preserved by the OLC `shapezon`).

The RotS command letters seen in `renum_zone_one` (`zone.cpp:196-`) include at least
`A`, `L`, `M`, `N`, `X`, `H`, `E`, `K`, `Q`, `P` — a richer set than Diku's
`M/O/G/E/P/D`. **The db.h:166-174 comment listing M/O/G/P/E/D is stale** and does not match
this loader.

---

## Open questions
- **Full zone reset-command semantics**: what each letter (`A L M N X H E K Q P`…) and its
  args actually do at runtime. The executor is `reset_zone` (`zone.cpp:478`) — not yet read.
  arg1 of `A`/`L` appears to be a sub-type selector (`renum_zone_one:198-214`).
- **`reset_mode` values** (switch at `zone.cpp:308`) and **`if_flag`** exact semantics —
  confirm against `reset_zone`.
- **Bitvector/enum tables**: `room_flags`, `sector_type`, `exit_info`, mob `action`/
  `affected_by`, object `type_flag`/`extra_flags`/`wear_flags`, apply `location` codes —
  to be enumerated (belongs in catalogs + the per-system docs).
- Object record tail after affects — confirm no other trailing tokens exist in real files
  (none observed in the loader, but no sample data is available to validate).
