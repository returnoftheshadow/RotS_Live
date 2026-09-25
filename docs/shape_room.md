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

- Builder permissions: players below god level may shape rooms only. Higher
  levels can shape any prototype, but you still need zone permission to save.
- Location: `shape room current` uses your current room number, so ensure you
  are standing in the room you want to copy before starting.
- Prompt: once shaping, your prompt shows `Shaping: <vnum>`.

## The `shape` command (all editors)

`shape <mobile|object|room|zone|script|program> <vnum>` starts an editor
(`shape room current` / `shape zone current` use where you stand). Words can be
shortened (`shape mob 1300`). One editor at a time: `/free` or `/done` first.

| Form | Result |
|------|--------|
| `shape <mobile\|object\|room\|script> new <zone>` | Disabled: `"shape mobile new 13" has been disabled due to a bug.` World files are created outside the game; shape the vnum you want instead. |
| `shape recalc_mobile` (implementor) | Disabled: `"shape recalc_mobile" has been disabled due to a bug.` It rewrote every mob in every file from its level and shut the game down. |
| `shape master_mobile <idnum>` / `shape master_object <idnum>` (greater god) | Gives that player permission to shape any mob / object. With no number nothing changes and the current master is shown: `Mobile master is player #51566.` / `Usage: shape master_mobile <idnum>`. |

Disabled commands always repeat exactly what you typed, so it is clear which one
was turned off.

## Room Writing Guidelines

### Level 91 (Lower Maias)

- Room titles start at column 0, use title case (“Woods in the Valley”), and
  never end with a period.
- Describe the location, not the visitor. Avoid implying actions (“You shiver”),
  emotions, racial biases, or times of day unless the room enforces them.
- Keep a neutral voice and avoid second-person pronouns, exclamation points, or
  sentence fragments.
- Ensure descriptions are at least four lines long, each indented with three
  spaces. Run `%f` to wrap them neatly (lines stay under 79 columns).
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
| `shape room current` | Edit the room you are standing in | `do_shape` turns `current` into your room's vnum and loads it. |
| `shape room 1234` | Edit any room by vnum | The loader reads `world/wld/<vnum/100>.wld`. If that vnum is not in the file yet, the editor makes a blank room with that number ("could not find room #1234, created it.") and `/save` puts it into the file in number order. This is how new rooms are made. |
| `shape room new <zone#>` | Disabled | Prints `"shape room new 16" has been disabled due to a bug.` Zone files are created outside the game, and `new` picked "last room + 1" itself. Shape the vnum you want instead. |

Non-gods may shape **only** rooms (`You are permitted to shape rooms only.`).
Once a room is loaded you see "You start shaping a room." and your prompt shows
`Shaping: <vnum>`. Every command while shaping starts with `/`.

## Session control commands

While shaping, a non-numeric `/word` is matched by prefix against: create, new,
load, save, add, free, done, delete, implement (in that order, so `/d` is
`/done`, not `/delete`).

| Command | Purpose & behaviour |
|---------|---------------------|
| `/save` | Copies `world/wld/<zone>.wld` to `world/wld/oldroms/<zone>.wld` (a whole-file backup, overwritten on every save), then rewrites the file with your room. Prints `Saved as room #N`. |
| `/implement` | Pushes the room into the live world without touching disk. Flags the room booted with (DARK, and so on) are kept. |
| `/done` | `/save`, then `/implement`, then `/free`. **If the save fails** (no permission, missing backup folder, file error) it stops, keeps your edits and says `Not saved - still shaping. Fix the problem and /done again, or /free to discard.` |
| `/free` | Ends shaping. Unsaved changes are thrown away. |
| `/delete`, `/add`, `/new`, `/create` | **Disabled.** Each prints exactly what you typed, e.g. `"/delete" has been disabled due to a bug.`, and changes nothing. Rooms are removed from (or added to) zone files outside the game. |
| `/load <vnum>` | Only works when no room is loaded, which never happens inside a session. Use `/free` and `shape room <vnum>` instead. |

Any other word prints the command list.

## Editing workflow

`/0` (or any unused number) prints the field list; `/50` prints the current
values. Prompts fall into three kinds:

1. **Single line** (`/1`, `/8`, `/13`): `Enter line <FIELD> (blank = keep, %q = empty):`
   followed by the current text in brackets. A blank line keeps the value, `%q`
   empties it. `#` becomes `+` and `~` becomes `-`.
2. **Multi-line** (`/2`, `/9`, `/14`): the text editor. Your old text is kept
   and new lines are added after it. `%e` saves, `%q` aborts and keeps the old
   text, `%f` formats, `%h` shows help.
3. **Number** (`Enter <field> [current]:`): `N` sets, `+N` adds, `-N`
   subtracts, `pN` sets bit N, `mN` clears bit N, blank keeps the value. Only
   the first word counts (`p0 p4` applies only `p0`), and a word the editor
   does not understand leaves the value unchanged. Exception: the key (`/10`)
   and destination (`/11`) prompts take `-N` as a negative value (see below).

### Room field commands

| `/n` | Field | Description |
|------|-------|-------------|
| `/1` | Name | One-line room title. Follow `GUIDELINES` (title case, no trailing period). |
| `/2` | Description | Multi-line. Indent with three spaces, `%f`, then `%e`. |
| `/3` | Room flags | Shows the list: 0 dark, 1 death, 2 no_mob, 3 indoors, 4 noride, 5 (internal), 6 shadowy, 7 no_magic, 8 tunnel, 9 private, 10 godroom, 11 (internal), 12 water, 13 poison, 14 security, 15 peace, 16 no_teleport, 17 hide_vnum. Use `pN`/`mN` one bit per answer. Do not set 5 or 11. |
| `/4` | Sector type | Shows the list (0 floor, 1 city, 2 field, 3 forest, 4 hills, 5 mountain, 6 water, 7 water_noswim, 8 underwater, 9 road, 10 crack, 11 dense_forest, 12 swamp). Anything outside 0-12 is refused: `Sector type must be 0-12. dropped.` |
| `/17` | Room level | 0-255 (anything else is refused). Its only game use is mortal `where`, which finds players in the same zone and the same room level. |
| `/18` | Top room affect | `Enter room affect: type spell_number level room_flag_bits` with the current four values shown. Type 1 = spell. Blank keeps the values; 1-3 numbers print `four numbers required. dropped`. Duration is always permanent. |
| `/19` | Add affect | Adds a new affect on top: type 1 (spell), spell 127 (none), permanent. Then use `/18` to fill it in. |
| `/20` | Remove affect | Removes the top affect. |
| `/50` | List | Prints every field, the selected exit, the top extra description, the room level and the top affect. |

### Exit commands

1. `/5` — `Enter exit to edit (n e s w u d):`. Only the first letter counts. If
   there is no exit in that direction one is created and you are told:
   `New exit: it leads nowhere until you set its destination with /11. Use /7 to remove it.`
   Selecting a direction you do not want still creates it, so `/7` it before
   saving.
2. `/6` — Exit flags, with the list shown: 0 door, 1 closed, 2 locked, 3 noflee,
   4 (unused), 5 nopick, 6 isheavy, 7 nobreak, 8 nolook, 9 hidden, 10 broken,
   11 noride, 12 noblink, 13 lever, 14 nowalk. `pN`/`mN` one bit per answer.
   Closed/locked are only the boot state; a zone `D` command sets them at reset.
3. `/7` — `Enter exit to remove (n e s w u d):`. It asks for a direction; it does
   not use the selected exit.
4. `/8` — `EXIT KEYWORDS, first one is shown`: space-separated words used by
   open/close/lock. The first word appears in messages ("The door is closed.").
5. `/9` — Exit description (multi-line), shown on `look <direction>`. Empty
   means the game shows the room it leads to.
6. `/10` — `key object vnum, -1 = no keyhole`. Typing `-1` sets -1 (no lock can
   be used); `0` means the key with vnum 0.
7. `/11` — `destination room vnum, -1 = nowhere`. Typing `-1` sets -1. The
   reverse exit is not created for you.
8. `/12` — `exit width 1-6, 0 = sector default`. Only blocking mobs use it; a
   bigger number is harder to block. 0-255 is accepted, anything else refused.

### Extra description commands

| `/13` | Keywords (space-separated) of the **top** extra description, used by `look <word>`. |
| `/14` | Text of the top extra description. |
| `/15` | Adds a new extra description on top, then asks for its keywords (`/13`). Use `/14` for the text. |
| `/16` | Removes the top extra description. |

`/13` and `/14` only ever edit the top (most recently added) entry.

## Example workflows

### Modify an existing room

```text
shape room current        # load the room you are standing in
/5                        # choose which exit to edit
n                         # the north exit
/11                       # set the destination vnum
1605
/8                        # the door keywords (one line, no %e)
oak door
/2                        # add to the room description
   You stand before a weathered oak door...
%f
%e
/15                       # add an extra description; asks for keywords
door oak
/14                       # its text
   The door is banded with iron.
%f
%e
/50                       # check everything
/done                     # save, implement, stop shaping
```

### Make a new room

Pick a free vnum inside your zone's range and shape it directly:

```text
shape room 1650           # "could not find room #1650, created it."
/1
Mist-Draped Bridge
/2
   Wisps of mist cling to the old stone bridge, hiding the drop below.
%f
%e
/3
p0                        # DARK (one bit per answer)
/4
2                         # field
/5
n                         # new north exit
/11
1651                      # where it leads
/12
0                         # sector default width
/done
```

## Troubleshooting tips

- "You have nothing to shape." — you ran a numeric command with no room loaded.
  Start with `shape room current` or `shape room <vnum>`.
- "You are already shaping something. Free it first." — `/free` or `/done` the
  previous session.
- "You may not do that in this zone." — the zone does not grant you permission.
  Ask the zone owner or an implementor.
- "could not open backup file" — the `world/wld/oldroms/` folder is missing;
  `/save` and `/done` cannot work until it exists. `/done` keeps your edits.
- To undo a bad save, copy `world/wld/oldroms/<zone>.wld` back. It holds the
  file as it was just before the **last** save only.
