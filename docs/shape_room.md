# Shape Room Command

Room shaping is the online builder workflow for creating or editing rooms in
place without recompiling. The `shape room` entry point lives in
`src/shapemob.cpp` (`ACMD(do_shape)`), while the editor logic is implemented in
`src/shaperom.cpp`. This guide documents how to enter the mode, which `/`
commands are available, and what each numeric editor option changes.

> The broader shaping system (objects, mobiles, zones, programs) works the same
> way. We are starting the documentation effort with rooms, so future sections
> can reuse the terminology established here.

## Prerequisites

- Builder permissions: `do_shape` only lets non-gods shape rooms. Higher level
  staff can shape any prototype, but you still need zone permissions before
  writing (`get_permission` in `create_room()` / `replace_room()`).
- Location: `shape room current` uses your current room number, so ensure you
  are standing in the room you want to copy before starting.
- Prompt: once shaping, your prompt changes to include the builder mode number
  so you know which interpreter is active.

## Room Writing Guidelines

### Level 91 (Lower Maias)

- Room titles start at column 0, use title case (“Woods in the Valley”), and
  never end with a period.
- Describe the location, not the visitor. Avoid implying actions (“You shiver”),
  emotions, racial biases, or times of day unless the room enforces them.
- Keep a neutral voice and avoid second-person pronouns, exclamation points, or
  sentence fragments.
- Ensure descriptions are at least four lines long, each indented with three
  spaces. Run `%f` to wrap them neatly.
- Stay lore-friendly: Fourth Age Middle-earth allows creative flora/fauna but
  not cars, firearms, or modern tech. Death traps are banned.
- Door keywords should be single lowercase words. Use the `exit_width` field for
  unusual widths (default `0` lets the sector decide).

### Level 93 (Maias)

- Populate the zone’s metadata in `shape zone` as soon as you claim an area.
- Unless directed otherwise, lay out zones as rectangles—8 rooms north/south by
  5 or 10 rooms east/west—so future connectors are straightforward.

## Starting a room shaping session

| Command | When to use | Notes |
|---------|-------------|-------|
| `shape room current` | Edit the room you are standing in | `do_shape` converts `current` into the real room number and runs `load <vnum>` for you (`src/shapemob.cpp:1998-2015`). |
| `shape room 1234` | Edit any existing room by vnum | Replace `1234` with the virtual room number. The loader reads from `world/wld/<zone>.wld` (see `SHAPE_ROM_DIR`). |
| `shape room new <zone#>` | Create a blank room at the end of a zone file | Calls `create_room()` which checks zone permissions, opens `world/wld/<zone>.wld`, and prepares a new `room_data`. Immediately `/add <zone#>` afterward to persist it. |

Once executed, you receive “You start shaping a room.” and your prompt number
switches to `4` to indicate the room editor is active. All subsequent commands
must be prefixed with `/` (per the builder manual’s GENERAL section).

## Session control commands

While shaping, entering any non-numeric `/command` routes through
`extra_coms_room()` (`src/shaperom.cpp:1629-1778`). These drive the lifecycle:

| Command | Purpose & behaviour |
|---------|---------------------|
| `/load <vnum>` | Calls `load_room()` to populate `SHAPE_ROOM(ch)->room` with another vnum while leaving the editor running. Useful for quickly hopping between adjacent rooms. |
| `/create <zone#>` | Allocates a blank `room_data`, remembers `world/wld/<zone>.wld` as the working file, and marks the slot as dirty so `/add` or `/save` knows where to write. |
| `/save` | Runs `replace_room()`: copies the source `.wld` file to `world/wld/oldroms/<zone>.wld`, then rewrites the original entry with your edited data. Keeps the existing vnum. |
| `/add <zone#>` | Runs `append_room()`: same backup process as `/save`, but appends your new room to the end of the zone file and assigns the next available vnum. |
| `/delete` | First invocation arms deletion and prompts for confirmation. Typing `yes` immediately afterward toggles `SHAPE_DELETE_ACTIVE`, so the next `/save` removes the room from disk. Any other response cancels the delete. |
| `/implement` | Calls `implement_room()` to push the in-memory struct into the live `world[]` array without touching disk. Use this after `/save` to see your updates instantly in game. |
| `/done` | Convenience macro: if a room is loaded it performs `/save`, then `/implement`, then `/free`. Ends the session with one command. |
| `/free` | Calls `free_room()`, releases all allocated descriptions/exits/affects, resets prompts, and moves your character back to their previous position. Always free the editor before switching to another shaper target. |

If you enter something else, the helper prints the allowed keywords (“save,
delete, implement, done, free”) and leaves you in edit mode.

## Shaping workflow tips

- Every shaping command (besides the initial `shape room …`) must start with `/`.
  `/help`, `/0`, and `/50` are always available reminders.
- `/imp` shows what you’ve built; `/50` prints the current field values.
- `/free` quits without saving. `/save` writes to disk, `/implement` syncs the
  live world, and `/done` performs save → implement → free in one shot.
- Always `/save` before `/free` unless you intend to discard edits.
- FAQ nuggets:
  - `%e` must be on its own line to finish multiline text. `%q` aborts an edit.
  - `/50` lists most commands and field states; `/help` or `/0` lists the rest.
  - `/save` followed by `/done` is redundant because `/done` already saves and
    implements, but running `/save` first gives you an explicit confirmation.
- `/free` ends shaping immediately—use `/done` if you want to save as you exit.
- Always indent descriptions manually (three spaces), run `%f`, then `%e`.
- Mob/object population limits are defined in the zone script (`L` commands).
  If you need “exactly one mob” logic, update the zone data rather than the
  room itself.

## Editing workflow

Type `/0` or any non-digit to display all numeric editor commands (handled by
`list_help_room()`), then use `/<number>` to edit a field. Inputs fall into three
categories:

1. Text entry (`LINECHANGE` / `DESCRCHANGE` macros) uses the standard `%f`/`%e`
   editor; `%q` keeps the previous value.
2. Numeric entry (`DIGITCHANGE`) accepts absolute numbers, delta modifiers
   (`+17`, `-2`), or bit toggles (`p5`, `m3`) just like the rest of the shaping
   system.
3. Selection prompts temporarily change your prompt to ask for an exit
   direction (letters `N`, `S`, `E`, `W`, `U`, `D`).

`string_to_new_value()` backs every numeric prompt, so inputs like `p1` or `m4`
edit individual bits, while plain integers overwrite the whole field. Leaving
the prompt blank keeps the previous value.

### Bitvector input cheat sheet

Use these formats to manipulate flags:

- `17` — set the full value to 17.
- `+17` / `-17` — add or subtract.
- `p17` — set bit 17 (`1 << 17`).
- `m17` — clear bit 17.

Example: to set SENTINEL (`2`) and WIMPY (`128`) on a mob flag, either enter
`138` once or run `p1` then `p7`. `/50` after each change to confirm the result.

### Room field commands

| `/n` | Field | Description |
|------|-------|-------------|
| `/1` | Name | One-line room title. Stored verbatim, so follow `GUIDELINES` (title case, no trailing punctuation). |
| `/2` | Description | Multiline description. The editor swaps `#`→`+` and `~`→`-` automatically to keep `.wld` files intact; run `%f` before `%e` for proper wrapping. |
| `/3` | Room flags | Bitvector; use `p<n>`/`m<n>` to toggle individual bits or enter summed values directly (see `ROOM_*` in `structs.h`). `p7` sets flag 7, `m2` clears flag 2, `+4` adds 4, etc. |
| `/4` | Sector type | Numeric sector id from `sector_types` (inside, city, forest, mountain...). Values live in `constants.cpp`. |
| `/17` | Room level | Integer stored in `room_data::level` for quest tooling and scaling. |
| `/18` | Top room affect | Rewrites the first `struct affected_type` entry using four integers: `type location modifier bitvector`. Duration is forced to `-1`, so the effect is permanent until removed. |
| `/19` | Add affect | Pushes a fresh affect struct onto the list and enables chaining so `/18` runs next. |
| `/20` | Remove affect | Pops the top affect entry. Repeat to remove multiple entries. |
| `/50` | List | Prints the current state of every editable field, including the selected exit, extra descriptions, and the first affect block. |

Text commands (`/1`, `/2`, `/13`, `/14`) honour the `%q` shortcut to cancel
edits. Numeric commands remember the previous value, so submitting a blank line
keeps the old value.

### Exit commands

1. `/5` — Select exit direction (must run this before editing exit-specific
   fields). Accepts `n`, `s`, `e`, `w`, `u`, or `d`. If no exit exists the
   editor allocates empty keyword/description strings so you can build it from
   scratch.
2. `/6` — Exit flags (`room_direction_data::exit_info`). Supports all
   combinations including hidden/no-look/heavy doors. Flag bits live in
   `src/structs.h` (`EX_ISDOOR`, `EX_CLOSED`, `EX_LOCKED`, `EX_NOFLEE`,
   `EX_PICKPROOF`, `EX_DOORISHEAVY`, `EX_NO_LOOK`, `EX_ISHIDDEN`, etc.). Use
   numeric additions or `p<n>`/`m<n>` to toggle bits—for example `p0 p1 p9`
   makes a closed, hidden, no-flee door.
3. `/7` — Remove the selected exit entirely and clear `exit_chosen`. Use this
   when deleting links or cleaning up auto-generated exits.
4. `/8` — Exit keyword list. Provide space-separated words (e.g., `door hatch
   trapdoor`).
5. `/9` — Exit description text (shows when players look at the door). `%f`
   works here as well.
6. `/10` — Key vnum for locked exits. Set to `0` if no key is required.
7. `/11` — Destination room vnum. Enter the virtual number of the target room.
   Remember to create the reverse exit manually.
8. `/12` — Exit width. Defaults to `0` (derived from sector type). Override
   when you need narrow crawlways or oversized gates.

Selecting an exit automatically creates placeholder `room_direction_data`
structures if one does not exist (`src/shaperom.cpp:718-734`), so you can
configure brand-new doors without leaving the editor.

### Extra description commands

| `/13` | Edit keyword (space-separated) for the current extra description record. |
| `/14` | Edit the corresponding description text. |
| `/15` | Push a new extra description onto the stack. The editor automatically sets `SHAPE_CHAIN`, so `/13` and `/14` fire next without retyping the numbers. |
| `/16` | Remove the current extra description (or the only one if it is the last). |

Extra descriptions behave like a stack: `/15` adds to the top, `/16` pops it.
Use `/50` after `/15`/`/16` to confirm you are editing the intended entry.

### Room affects

- `/18` expects four integers separated by spaces: `type location modifier
  bitvector`. For `ROOMAFF_SPELL` entries the `location` is the spell number
  (see `skills[]`). The editor forces `duration = -1`, so affects persist until
  someone removes them.
- `/19` appends a blank affect node to the head of the list, prints “A new
  affection added.”, and enables chaining so `/18` triggers immediately. Use
  this combo to add fog, damage auras, or `ROOMAFF_TRAP` behaviours.
- `/20` removes the head node. Run it repeatedly to clear the list from top to
  bottom.

If you try `/18` without any affect data present the shaper prints “No room
affections found.”, so remember to `/19` first.

### `list` snapshot

`/50` calls `list_room()` and prints:

- Room name, description, flags, sector, and selected exit details.
- Exit keyword/description/key/destination/width for the currently selected exit
  (run `/5` first to choose).
- The first extra description (keyword + text) and the first affect record if
  present.

Use it before `/save` as a final sanity check or after `/load` to understand an
existing room’s structure.

## Example workflows

### Modify an existing room

```text
shape room current        # load the room you are standing in
/5                        # choose which exit to edit
n                         # at the prompt, enter “n” to pick the north exit
/11                       # set the destination vnum
1605                      # send the new room number
/8                        # change the door keywords
oak door
%e
/2                        # edit the room description
   You stand before a weathered oak door...
%f
%e
/15                       # add an extra description for the door
/13
door oak door
/14
   The door is banded with iron.
%f
%e
/save
/implement
/done
```

This sequence highlights the typical cadence: select an exit, edit linked fields
in any order, review with `/50`, and save/implement when finished.

### Create a new room from scratch

```text
shape room new 16         # create a template (zone 16 covers rooms 1600-1699)
/49                       # optional: walk the chained command list
/1
Mist-Draped Bridge
/2
   Wisps of mist cling to the old stone bridge, hiding the drop below.
%f
%e
/3
p0 p4                     # example: DARK + NOMOB flags
/4
3                         # SECT_FIELD (adjust to taste)
/5
n                         # select the north exit
/11
1602                      # point to the destination room
/8
arch doorway bridge
/10
0                         # no key
/12
180                       # narrow exit width
/13
%q                        # no extra description yet
/17
35                        # room level
/save                     # writes to world/wld/16xx.wld and backs up
/implement                # updates the live world array
/done
```

Repeat for the south/east/west exits as needed, then `/imp` to double-check
your work in-game. Every new zone already includes 40 blank rooms, so stay
within your allocated number range.

## Troubleshooting tips

- “You have nothing to shape” — you ran a numeric command before loading a
  room. Use `/load <vnum>` or restart with `shape room current`.
- “You are already shaping something” — you forgot to `/free` your previous
  object/mob/room. Either finish and `/done`, or `/free` to start fresh.
- “You may not create room here” — the zone does not grant you permission (see
  `get_permission()`), or you mistyped the zone number. Contact the zone owner
  or the implementor.
- Accidentally deleted an exit or description? Because `/save` makes a backup in
  `lib/backups/rooms/`, you can copy the `.bak` back in place or reload the room
  without saving to revert to the last known state.

With `shape room` documented, future scripting-doc sections can reference this
file rather than repeating the basics of prompts, `/save` vs `/implement`, and
the slash command syntax.
