# Object / rent files (`.obj` — player inventory & equipment)

**Source files:** `src/objsave.cpp` (`Crash_get_filename:73`, `Crash_write_rentcode:348`,
`Crash_obj2store:635`, `Crash_save:999`, `Crash_rentsave:1177`, `Crash_follower_save:680`,
`Crash_alias_save:952`, loaders `load_character:548`/`Crash_*_load`); structs
`obj_file_elem` (`structs.h:1842`), `rent_info` (`structs.h:1866`), `follower_file_elem`
(`structs.h:1826`)
**Status:** ✅ write format + layout complete. Load-side section delimiting noted under
Open questions.

## Purpose
A player's carried items, worn equipment, mounts/followers, and command aliases are stored
together in a per-player **binary** file (the DikuMUD "Crash/rent" system). Unlike the
player character file (which is now text — see `player-save.md`), these remain raw
`fwrite` of fixed structs, so the layout is the compiler's struct layout for the **32-bit**
build (4-byte `int`/`long`/`time_t`, default alignment). This is why the build must stay
32-bit (or a converter is needed): the on-disk contract is the binary struct image.

## File location & naming (`Crash_get_filename:73`)
```
plrobjs/<bucket>/<name>.obj
```
Bucketed by first letter exactly like player files: `A-E F-J K-O P-T U-Z`, else `ZZZ`.
One file per character. Opened `"w+b"` for save, `"rb"`/`"r+b"` for load/clean.

## On-disk structures

### `rent_info` — file header (`structs.h:1866`)
```c
int time;              // unix time of rent/crash
int rentcode;          // RENT_* (see below)
int net_cost_per_hour; // rent cost
int gold;              // gold on hand at save
int nitems;            // item count (see Open questions re: when set)
sh_int spare0..2;      // 3 shorts
int   spare3..7;       // 5 ints
```
`rentcode` values (`structs.h:1857-1863`): `RENT_UNDEF 0, RENT_CRASH 1, RENT_RENTED 2,
RENT_CAMP 3, RENT_FORCED 4, RENT_TIMEDOUT 5, RENT_QUIT 6`.

### `obj_file_elem` — one per object (`structs.h:1842`)
```c
sh_int item_number_deprecated; // legacy id; set to DEPRECATED_ID_VALUE(-255) by new saves
sh_int value[5];
int    extra_flags;
int    weight;
int    timer;
long   bitvector;              // 4 bytes in the 32-bit build
struct obj_affected_type affected[MAX_OBJ_AFFECT];   // MAX_OBJ_AFFECT=2; {byte location; int modifier;}
sh_int wear_pos;              // placement, see nesting below
int    loaded_by;            // idnum of immortal loader, 0 if zone-loaded
int    item_number;          // object VNUM (was spare2; widened to int)
```
Notes (`Crash_obj2store:635`):
- `item_number` holds the object **vnum** (`obj_index[...].virt`), or a negative special.
- New saves stamp `item_number_deprecated = DEPRECATED_ID_VALUE` so loaders can tell new
  from old records.
- Special case: for `generic_scalp` objects, the player-id is stashed in `extra_flags`
  (because ids exceed `sh_int`); scalp loading reverses this and zeroes `extra_flags`
  (`:663-668`).

### `follower_file_elem` — one per follower/mount (`structs.h:1826`)
```c
int fol_vnum;      // follower mob vnum; -17 sentinel terminates the follower list
int mount_vnum;
int wimpy;
int exp;
int flag_config;   // FOL_ORC_FRIEND / FOL_TAMED / FOL_GUARDIAN / FOL_MOUNT
int spare1, spare2;
```

## File layout (`Crash_rentsave:1177`)
In write order:
1. **`rent_info`** header (`Crash_write_rentcode:350`).
2. **Carried items**, then **each worn item** (`Crash_save` with `pos = MAX_WEAR` for
   carried, `pos = slot` for equipment). `Crash_save` is recursive and writes, per object:
   the object record, then **its container contents** (`obj->contains`) at nesting
   `pos+1`, then the next sibling (`obj->next_content`) — a depth-first flatten
   (`Crash_save:1007-1016`). Container membership is reconstructed from the `wear_pos`
   depth values: carried/contained items use `wear_pos >= MAX_WEAR`, with `+1` per nesting
   level; worn items use `wear_pos = slot (< MAX_WEAR)`.
3. **Aliases** (`Crash_alias_save:952`).
4. **Followers** (`Crash_follower_save:680`): for each followed NPC in the room, a
   `follower_file_elem`, then that follower's equipment as `obj_file_elem` records, then a
   `dummy_object` (`item_number = SENTINEL_ITEM_ID_VALUE(-17)`) marking end of that
   follower's eq. The mount (if ridden) is written similarly. The list ends with a
   `follower_file_elem` whose `fol_vnum = -17`.

## Sentinels
- `SENTINEL_ITEM_ID_VALUE = -17`, `DEPRECATED_ID_VALUE = -255` (`structs.h:1840-1841`).
- Follower list terminator: `follower_file_elem.fol_vnum == -17` (`:749`).
- Per-follower eq terminator: `obj_file_elem.item_number == -17` (`:688`).

## RotS-specific notes
- Followers/mounts persistence (with per-follower eq blocks) is richer than stock Diku rent.
- `loaded_by` (immortal id) and the widened `item_number` are RotS additions.

## Open questions
- **Section delimiting on load.** How the loader (`load_character:548`, `Crash_alias_load`,
  `Crash_follower_load`) distinguishes the end of the **item** section from the alias and
  follower sections — likely via `nitems` and/or sentinels. Read the loaders to pin this
  down (the writer above doesn't itself emit an item-list terminator before aliases).
- **`nitems`** — where it's populated before the header is written (not set in
  `Crash_rentsave`; possibly only meaningful in the rent-offer path). Confirm the loader's
  reliance on it.
- **Alias on-disk format** (`Crash_alias_save:952`) — not yet documented.
- A 64-bit rebuild would change `long`/`time_t`/alignment and break this format; a converter
  would be required. (Out of scope while building 32-bit.)
