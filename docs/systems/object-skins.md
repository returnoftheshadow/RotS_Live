# Object skins

A skin is an object prototype with its own vnum and its own text (aliases, descriptions) whose
stats all come from another prototype, its base. A builder makes a new look of an item once, on
the base, and every skin follows. Code: `src/object_skins.cpp`, `src/object_skins.h`; editor in
`src/shapeobj.cpp`; `restyle` in `src/act_wiz.cpp`; script action in `src/script.cpp`.

## Fields

In `obj_flag_data` (`structs.h:443-446`):

| Field | Meaning |
|---|---|
| `skin_base` | vnum of the base; `0` = not a skin (`obj_is_skin`, `object_skins.cpp:17`) |
| `skin_own` | bit mask of stats the skin keeps for itself: `1` own cost (`SKIN_OWN_COST`), `2` own material (`SKIN_OWN_MATERIAL`) (`object_skins.h:11-12`) |

A valid base is another prototype that is not the skin itself, is not a key or a note, and is
not itself a skin (`check_skin_base`, `object_skins.cpp:43`; the log text from `skin_base_problem`, `:61`). A family is a base and its skins
(`skin_family_rnum`, `:87`; `skin_rnums_of`, `:100`).

## What is copied from the base

`copy_skin_stats_from_base` (`object_skins.cpp:19`) copies type, extra flags, wear flags, the five
values, weight, rent (`cost_per_day`), level, rarity, script number, version and all affects. Cost
and material are copied only when the skin does not own them. Text, `skin_base` and `skin_own`
are never copied.

What an own cost or material does in play (both carry over on a restyle):

- **Cost** is what shops buy and sell the skin for (`shop.cpp`), and an item whose rent is `-1`
  rents for cost/100 (`objsave.cpp:2042`), so a skin with a low own cost of an expensive item
  rents for almost nothing. If players can reach a restyle script, they can buy the cheaper look,
  restyle it and sell the dearer one.
- **Material** is not only a look. Chain or metal armor makes an archer pass an accuracy check
  and may break the arrow (`ranger.cpp:2069`, `:2210`); smite's bonus works only on metal
  (`fight.cpp:2279`); shops filter what they trade by material (`shop.cpp:190`); identify shows
  it. A metal-looking skin of leather armor gains the arrow protection, and the reverse loses it.

Load limits and vnum checks follow the copy's own vnum. A zone limit on the base counts only copies
that are the base: restyling one into a skin lowers that count, so the zone can load another, and
each skin has its own count (`zone.cpp:956`). Barter currency and a script that checks for a vnum
no longer match a copy restyled to another vnum.

Not passed on: a special program attached to an object by vnum (`assign_objects`,
`spec_ass.cpp`: the boards 1104-1135 and the ferry boat 1121). A skin has its own vnum, so a skin
of one of these looks like it but does not work, and restyling one to a skin drops its program.
Don't make skins of them.

When the copy happens:

- **Boot.** `apply_object_skins` (`object_skins.cpp:112`, called from `boot_db`, `db.cpp:430`) runs
  once after the object index is loaded. Every skin is judged against the table as loaded, so the
  result does not depend on file order. A skin with a bad base is turned into a plain object
  (`skin_base` and `skin_own` cleared, its own file stats kept) and this line is logged (NRM, for
  `LEVEL_IMMORT` and up): `OBJ ERROR: object #N: <problem> - loaded as plain object`. The problem
  is one of: `an object can't be its own base`, `base obj B not found`, `base obj B is a key`,
  `base obj B is a note`, `base obj B is a skin of obj C`. The valid skins then get the base stats.
- **Implement.** `implement_object` (`shapeobj.cpp:202`) copies base stats into a skin when it goes
  live. When the object implemented is not a skin, `refresh_skins_of_base` (`object_skins.cpp:141`,
  called at `shapeobj.cpp:311`) copies its stats into all of its valid skins. A skin whose base
  is invalid at implement time, or that has skins of its own in memory by then (`/23` refuses
  that while editing, but a skin of it can be implemented in between), goes live as a plain
  object and the builder gets `Skin not applied: <problem>.` (`obj N has skins` for the second);
  the editor's working copy keeps the base so it can be fixed. Implement is refused outright
  (`Obj N has skins (...); it can't become a key.` / `a note`, then `Not implemented.`, nothing
  changes) when an object that has skins would go live as a key or a note: `/9` refuses that
  type while editing, but it can also come from the file.
- **Editor load.** Loading a skin into the editor (`load_object`) copies the base's current
  stats into the working copy, so `/50` shows what the skin has now, not the file's fallback.
- **Save.** `replace_object` (`shapeobj.cpp:1909`, the copy at `:2070`) copies the current base stats into the skin
  before writing its file, so the stats in a skin's file are a fallback that is current when saved.
  The 7-number line is written only when `skin_base > 0` (`shapeobj.cpp:176`).

There is no automatic refresh of players' copies and no automatic version bump. A skin has the
base's version (it is copied). When the base's stats change and players' converted copies need
them, the builder bumps the base's version (`/22`) by hand; copies then refresh by the existing
version rules.

## File format

See "Object" in `docs/data-formats/world-files.md`. The level line has 5 numbers, or 7 for a skin:
`level rarity material script version skin_base skin_own`. It is read by
`read_object_level_line` (`object_skins.cpp:151`), which skips leading white space and takes the
rest of that line only; missing numbers are 0, so an old 4-number line has version 0 and no skin.
When the next line does not start with a number (a digit, `-` or `+`), for example the next `#`
record or an `E`/`A` token, the object has no level line: the reader leaves that line unread and
returns 0, so all seven numbers are 0. The editor's load (`load_object`, `shapeobj.cpp:1583`,
reader call at `:1754`) uses the same reader.

## Editor (`/23`)

Object editor field 23 is the base (`shapeobj.cpp:1319`, `shape_set_skin_base` at `:571`). Typed
vnum or `0`:

| Input | Result |
|---|---|
| `0` | `This object is no longer a skin; its stats are now its own.` Keeps the stats it has now. |
| own vnum | `An object can't be its own base.` |
| missing vnum | `There is no obj N.` |
| a key | `Keys can't have skins.` |
| a note | `Notes can't have skins.` (a player writing on a note edits its text in place; a restyle would swap it underneath) |
| a skin | `Obj N is a skin of obj M; use /23 M.` |
| the object being edited already has skins | `Obj N has skins (a, b); it can't become a skin.` (list is cut with `...` to fit 78 columns) |
| valid | `This object is now a skin of obj N (<name>).`, `Stats, values, affects and flags now come from N.`, and `Cost was X, now follows base (Y).` / `Material was X, now follows base (Y).` when they changed. `skin_own` is reset to 0. |

On a skin:

- Stat fields `/9`-`/13`, `/15`-`/17`, `/19`-`/22` are refused: `Obj N is a skin of obj M; edit stats
  on M.` (`skin_refuses_stat_edit`, `shapeobj.cpp:508`). The creation chain (`/49`) shows the
  refusal once, at its first stat step, then skips the stat steps (`next_chain_step`, `:497`):
  it goes on to cost (`/14`) and ends there.
- N in these messages is the vnum being edited (`edited_vnum`, `:470`, the editor's `number`),
  also for a vnum that is not booted yet (whose `basenum` is -1).
- `/14` (cost) and `/18` (material): typing a number sets the own bit. Typing `base` clears it and
  copies the base value again (`Cost now follows base (N).` / `Material now follows base (N).`,
  `skin_follow_base`, `:548`).
- A skin whose base is no longer valid (`skin_base_problem`: gone, a key, a note, a skin) is edited
  as the plain object it boots as (`skin_of_valid_base`): its stat fields can be edited, the list
  shows no `(from M)`, and `base` at `/14` or `/18` is refused with `Can't follow the base:
  <problem>.` and copies nothing. `/23` with a valid base, or `/23 0`, settles it.
- `/9` on a base that has skins: changing the type to a key or a note is refused with
  `Obj N has skins (...); it can't become a key.` (or `a note`) and the type is left as it was.
- The list (`list_object`, `:1416`) marks each copied field `(from M)` and cost and material
  `[own]` when owned, and shows line `(23) Skin of: obj M (<name>)`, `(23) Skins: a, b` or
  `(23) Skin of: none`.

## Restyle

`restyle_object(obj, target_rnum, &new)` (`object_skins.cpp:211`) changes a live object, in
place, into a copy of another member of its family. Refused (nothing changes) when: the target does not exist,
the object has no prototype, it is already that vnum, or the two are not in the same family
(`skin_family_rnum` differs; so two skins of one base are in the same family, and the base is in
the family of its skins; an unrelated object is not).

It stays the same `obj_data` and `*new` is set to it. Only these change: the prototype counts
(`obj_index[old].number` down, `obj_index[target].number` up), `item_number`, the five text
pointers (`name`, `short_description`, `description`, `action_description`, `ex_description`) now
point at the target prototype's text, and cost, material, `skin_base` and `skin_own` come from the
target prototype. Everything else stays as it is: place (worn slot, inventory, container or room,
and the position in the list), contents, the rest of `obj_flags` (flags, timer, values...),
affects, enchants, script, `loaded_by`, `owner`, `touched`. Nothing moves, so the character's
weight, carry and affect totals are not touched, and a caller still holding the object is safe:
object triggers (ON_WEAR, ON_EAT, ON_DRINK, ON_PULL, ON_EXAMINE_OBJECT, a mob's ON_DAMAGE) go on
using their object after a `RESTYLE_OBJ` in their own script.

Drink container names: filling and emptying change a container's name
(`name_to_drinkcon` / `name_from_drinkcon`, `act_obj2.cpp:53-81`). A prototype that starts full
has its liquid as the first name word ("water flask leather"); emptying strips the first word and
filling puts the new liquid in front. Restyle builds the new name the same way
(`drink_name_from`, `object_skins.cpp:190`): it takes the target's name, drops its first word if the
target prototype starts full, and puts the copy's own liquid in front if the copy is full. So a
full water flask restyled to "water jug clay" is "water jug clay", an empty one is "jug clay", and
one refilled with wine is "wine jug clay".

Known issue worked around: the copy's old name is never freed. After an implement of a drink
prototype, older copies can share one name string that looks like a copy's own (it differs from
the new prototype's name). Freeing it on restyle would leave the other copies pointing at freed
memory, so restyle leaks a few bytes per restyle of a drink container instead. The existing
fill/empty code (`name_to_drinkcon` / `name_from_drinkcon`) still frees in this case and has the
same hazard; it is not changed here. Revisit if drink names get a real owned/shared marker.

Any other text that differs from the prototype's is either the prototype's text from before an
implement (`implement_object` repoints the prototype and leaves the old text to the copies that
use it), shared with other copies, or a short description a script set with `ASSIGN_STR obN.name`
(see `docs/shape_script.md`). Neither is freed. A restyle replaces a script-set name with the new
look's; that name was never saved anyway, so it would have gone at the next reload.

### `restyle` command

`restyle <object> <vnum>` or `restyle <player> <object> <vnum>`. Level `LEVEL_GRGOD`
(`interpre.cpp`, command 255); `do_restyle`, `act_wiz.cpp:1224`. It looks in inventory then
equipment of the imm, or of the named visible player: only items held directly, not items inside
a container (a script's `RESTYLE_OBJ` can restyle an item anywhere). Messages: `Restyle what to which vnum?`,
`No such player.`, `You don't see that.`, `There is no obj N.`, `It is already obj N.`,
`Obj N is not a look of obj M.`, and on success `Restyled obj M (<old name>) to obj N (<new name>).`
The two names share what is left of 78 columns (a short one leaves its room to the other). When the
item is another player's, that player is told `<Old name> has been restyled into <new name>.`,
without the imm's name. The holder gets `PLR_CRASH` so the change is saved. Log (BRF, shown from
`LEVEL_GOD` or the imm's invis level, whichever is higher):
`(GC) restyle: obj M -> N, player idnum I, by <imm>`.

### Script action `RESTYLE_OBJ`

`SCRIPT_RESTYLE_OBJ` is command 79 (`script.h:99`); run by `restyle_script_object`
(`script.cpp:1123`). Parameters: `param[0]` the object variable, ob1-3 only; `param[1]` the target
vnum, typed or taken from int1-3 or obN.vnum (`is_restyle_target_param`), read with
`get_int_param` when the line runs. Nothing else is allowed. The editor refuses anything else as
the object (`Must be ob1, ob2 or ob3. dropped.`, prompt kind `o`) or as the target (`Must be a
vnum, int1-3 or obN.vnum. dropped.`, prompt kind `i`); a line in a hand-edited world file that
holds one anyway does nothing when it runs and logs `object must be ob1-3` or `target must be a
vnum, int1-3 or obN.vnum`. The variable codes (100-955) are below every object vnum in use (the
lowest is 1101), so a typed vnum is never mistaken for one. `check_script_vnums` (the
`RESTYLE_OBJ` case) skips a variable and checks a typed vnum as an object vnum. On success ob1-3 still hold the same object (restyled
in place), and a player carrying it (directly or inside containers) gets `PLR_CRASH`. It reaches
the object wherever it is. A refusal changes nothing, the script carries on, and this is logged
(NRM, `LEVEL_AREAGOD`) as `SCRIPT ERROR: script #S, line L (restyle obj): <reason>`. A missing
target uses the same wording as boot and implement, `object vnum N not found`
(`report_script_vnum`); the other reasons are `no object`, `already obj N` and `obj M -> N not same family`.

## `obj2html`

`obj2html` (`obj2html.cpp`) leaves skins out of its tables and writes each one as a row right
after its parent's row, in whatever table the parent lands. A skin row fills only the cells a
skin can set: vnum, name (shown as `  - name (#<parent vnum>)`, the vnum in italics), cost
and material. Every other cell is blank, since it holds the parent's stat. A skin demoted at
boot is a plain object and gets its own row as before.

Data `obj2html` cannot show no longer breaks it: a lever whose direction is not a direction shows
the number in red (it used to read past `dirs[]`), as is a material that is not one (any table, and
skin rows), and a weapon whose type is not one
`weapon_hit_type` knows (it used to drop out of every weapon table silently) is listed only at
the end, as is a weapon whose bulk is outside 0-10 (it fits no one- or two-handed table; listed
only when its kind, whips or slashing and so on, is in the run) and armor whose wear flags fit no
armor table. A "gizmo" placeholder stays hidden as before, a skin named "gizmo" included. Those
unplaced objects go in a "Problem Objects" table at the end of the page (vnum, name, problem in
red); one listed there has its skins right under it as `skin of N`, names shown the same way, so no skin drops off the page
with its parent. The table is only for objects no other table lists: a lever with a bad
direction stays in the lever table, in red, and is not repeated there. The command says how many
problem objects it listed, not counting their skins.

Type words (`obj2html <types> <subtypes>`, `obj2html_taken`): a type named at a level wins there,
a type with a named type below it comes next, and a level with neither takes everything. So
`weapon` is every weapon table, `slash` the slashing weapons, `slash 2h` the two-handed slashing
weapons and `2h` every two-handed weapon. Before, only top-level words matched, so no weapon
table was ever written except by `all`.

## `stat`

`stat` on an object (`do_stat_object`, `act_wiz.cpp:582`, the lines at `:604`) prints, under the vnum line,
`Skin of: obj M (<name>)` for a skin (read from the prototype, so a copy made before implement
made its prototype plain shows none; the name cut to keep the line within 78 columns), or
`Skins: a, b` for a base that has skins (every skin listed, wrapped onto more lines indented
under the first so no line passes 78 columns; `stat_skins_lines`, `:565`).

## Deploy

Deploy the code before any skin file or any script that uses `RESTYLE_OBJ`. An older binary reads
only 5 numbers of the level line and exits at boot on a 7-number line, and it ends a script at an
action it does not know, so the lines after a `RESTYLE_OBJ` would silently not run. Help text is in `lib/text/shap_tbl` (OBJ 23) and `lib/text/wizh_tbl` (RESTYLE); script help in
`lib/text/scr_tbl`.
