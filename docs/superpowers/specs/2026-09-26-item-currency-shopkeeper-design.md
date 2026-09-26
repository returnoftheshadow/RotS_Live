# Barter vendors (item-currency shopkeeper) — design

**Date:** 2026-09-26 (revised same day: price lines replace room-count pricing)
**Status:** design agreed in discussion; spec awaiting review. Nothing built.

## Goal

A new kind of shopkeeper that sells goods for **items** used as currency (hides, belts,
arrowheads…), each item with its own combination of costs. Builders set it up entirely
through data: a mob program number plus a new per-mob **options** field. There are no vnums in
code and no `.shp` file.

Long term, the same keeper should grow a coin mode and become a data-driven replacement for
the old `.shp` shops (`src/shop.cpp`). The first version trades items only.

**Core principle:** the store room decides *what* is for sale; the vendor's price lines decide
*what it costs*. The vendor applies his rules to whatever is in stock.

**Performance is a hard requirement:** nothing in this feature may add noticeable lag. Options
are parsed ahead of time, and every command does a small, bounded amount of work (see
Performance).

## How builders set one up

1. On the mob: set `MOB_SPEC` and program number **33** (shaping field 29).
2. On the mob: write the **options** (shaping field 42), e.g.
   ```
   store=12345
   hours=6-12,14-20
   price 5001 2222x1 3333x2 deduct
   price 5002 3333x4
   ```
3. In the zone: use ordinary zone commands to load the goods into the store room. An item is
   for sale while at least one copy of it is in the store room.

## The options field

A new persisted text field on mob prototypes, meant for any mob program's settings, not just
vendors.

- **Format:** multi-line text, one setting per line: `key=value`, a bare word, or a keyword
  line such as `price …`. A line starting with `//` is a comment: skipped entirely, never
  warned about. Lines that are blank or that no program recognises are ignored at use.
  The text can't contain `~` or start with `#`/`$`, since those would corrupt the mob file.
  The editor refuses such text and keeps the old value.
- **Size:** up to 4,000 characters. Edited with the same multi-line editor as descriptions.
  `stat` and the editor always show the whole value.
- **File format:** one extra `~`-ended string after the mob record's last number line. The
  loader reads it only if the next token isn't the start of the next record (`#` or `$`), so
  existing mob files load unchanged with empty options. The editor (`write_proto`) writes it
  **only for mobs that have options**, so every other mob record stays in the old format.
- **Parsed ahead of time:** a program-33 prototype's options are parsed into an in-memory
  table at boot and again when a builder saves the mob. All copies of the mob share the
  table. `list`/`buy` never re-read the text.
- **General purpose:** other mob programs can read their own settings from it (e.g. a
  `mob_option(mob, "key")` helper). This replaces the practice of putting program switches in
  the mob's aliases (`has_alias(host, "conj")` in `mob_magic_user_spec`).
- **Rollback note:** an older server binary can't read a mob file containing an options
  block. Only files with vendor (or other options-using) mobs are affected.

## Vendor settings

| Setting | Meaning | Missing | Bad |
|---|---|---|---|
| `store=<room vnum>` | The store room; its contents are the stock | Warning; no trading | Warning; no trading |
| `hours=a-b[,c-d…]` | Game hours (0–23) he trades. Overnight (`20-4`) and several windows allowed | Always open | **Strict:** warning; no trading |
| `price <item vnum> <cur vnum>x<qty> [<cur vnum>x<qty>…] [deduct]` | The cost of one item | Item not sold by this vendor | **Lenient:** that line is skipped with a warning; the rest still work |

**`price` line details**
- `<item vnum>`: the object sold. `<cur vnum>x<qty>`: `qty` copies of object `cur vnum` are
  taken as payment.
- One to four currencies per line; quantities are whole numbers 1–100.
- `deduct`: each purchase removes **one** copy of the item from the store room, so stock is
  limited to the copies the zone loaded. Without `deduct` the copy stays and there is no
  purchase limit.
- A line is bad (skipped, warned) if an item or currency vnum isn't a real object, a quantity
  is outside 1–100, there are more than four currencies, or it doesn't parse.
- Up to 30 price lines per vendor; lines past 30 are skipped with a warning.
- A second line for the same item vnum is skipped with a warning; the first one wins.

**`hours=`** is bad if it's not in the `a-b` form, an hour is outside 0–23, or a window's start
equals its end. Hours are strict because they tie into the game clock; a vendor with bad hours
doesn't trade at all rather than silently trading around the clock.

Validation only checks that vnums are real objects/rooms. It does **not** check that a zone
ever loads a priced item into the store room: a priced item that isn't in stock simply isn't
listed (not loaded yet, seasonal, sold out).

## Two vendors in one room

`list`/`buy` is answered by the first vendor the room's character list reaches, the same as
the old shops. The player gets one answer.

## Shared store rooms

Several vendors can point at the same store room and price the same item differently (one for
wolf hides, another for leather belts). Each lists only the in-stock items he has a price line
for. With `deduct`, a purchase from any of them removes the copy from the shared room, so the
last copy can be bought from whichever vendor gets there first.

## Player-facing behaviour

### `list`
Answers "what do you sell?" Lists every item that is **in the store room** and has a **valid
price line** for this vendor, once each, in the order of his price lines:

```
 1. a hunter's belt (2 left)  1 x a leather belt
                              2 x a grey wolf hide
 2. a fur-lined cloak         4 x a grey wolf hide
 3. a bone-handled knife      1 x a leather belt
                              1 x a grey wolf hide
                              1 x a raven feather
```

- Names are the items' short descriptions. List numbers are right-aligned (` 1.` … `30.`).
- The cost column starts where the longest name on this list ends. Each extra currency goes on
  its own line under the first, with the name column blank.
- The name column is capped at 38 columns; a longer name wraps onto a second line so the costs
  always fit within 78 columns.
- `(N left)` shows only on `deduct` items, where N is the number of copies in the store room.
- Only items the player can see are listed. If nothing qualifies, the vendor says he has
  nothing to sell right now.

### `buy <name>` / `buy <number>`
1. Find the item by keyword or list number among the items `list` would show.
2. Check the player can carry it (item count and weight).
3. For **each** currency, count the copies in the player's **loose inventory** (not inside
   containers, not worn). A currency copy that is itself a container with something inside it
   doesn't count, so paying can never destroy its contents. If any is short, refuse, and say what's needed and what the player
   has. Nothing is taken.
4. Only when every currency is covered: remove the required copies one at a time, confirm the
   full amount was collected, and destroy them.
5. Create a **new** copy of the item and give it to the player.
6. If the line has `deduct`: remove one copy of the item from the store room.

All of this happens within one command; nothing can change between the steps.

### Who the vendor refuses
The same checks as the old shops (`is_ok` in `shop.cpp`), reused as they are:
- A vendor with a race refuses the other side, and anyone he's set aggressive to
  (`IS_AGGR_TO`: race side and `pref`).
- If `rp_flag` is set, only those races may trade (`RP_RACE_CHECK`). 0 means everyone.
- Shadows (`IS_SHADOW`).
- Anyone he can't see.
- Anyone who comes outside `hours=`. When closed he only refuses to trade: no pushing people
  out and no locking doors.

Side-specific vendors therefore need no new setting.

### Messages
Fixed built-in wording; no per-vendor message text. Every message we write is 78 columns or
less.
- **The vendor speaks with the normal `say`**, heard by the room: refusals, closed, nothing to
  sell, "Don't even think about it.", refusing gifts.
- **Lines about the buyer's own inventory go only to the buyer**: shortfalls ("You need 2 x a
  grey wolf hide and have 1."), carry limits, and the purchase summary ("You hand over 1 x a
  leather belt, 2 x a grey wolf hide. You now have a hunter's belt.").
- `say` refuses mobs with INT below 6, so a vendor needs INT 6 or more. This is warned at
  boot, `/save` and `/implement` (see Builder warnings) rather than special-cased in `say`.

### Protection
- **Attacks:** handled through the same hook as the old keeper (`SPECIAL_DAMAGE`,
  `fight.cpp:1648`). Damage is cancelled, he says "Don't even think about it.", the attacker
  stops fighting, and he never fights back.
- **Given items:** `give` to the vendor is refused with a message and nothing changes hands.

### Location
He trades wherever he stands, so traveling vendors work. A room restriction can be added later
as a setting.

## Performance

- Options are parsed at boot and on editor save only; `list`/`buy` read the parsed table.
- `list`: one pass over the store room's contents, matched against at most 30 price lines.
- `buy`: one pass over the player's inventory per currency (at most 4), plus removing at most
  4 × 100 currency objects and one store copy.
- No world-wide searches and no unbounded loops. This is the same order of work as the current
  shops' `list`/`buy`.

## Builder warnings

Reported like the other boot misconfiguration warnings (type + vnum + line, no prose):
- **At boot, `/save` and `/implement`:** for each program-33 prototype, a missing or bad
  `store=`, a bad `hours=`, each skipped price line, unrecognised lines (to catch typos), and
  INT below 6 ("vendor can't speak"). Comment lines are never warned about.
- **At boot:** a program-33 mob that also has a hard-coded special procedure (`.shp` file or
  `ASSIGNMOB`) is warned, because that procedure overrides program 33
  (`interpre.cpp:1581`).
- **At use:** a vendor with a missing/bad `store=` or bad `hours=` refuses to trade, and logs
  a warning **every time** someone tries. A broken vendor in play should be noisy. There is
  deliberately no setting to silence it; price-line warnings never fire during play anyway.

## Builder notes (documentation, not code)

- The store room must be unreachable by players. Anything dropped in or taken out changes
  what's for sale.
- Every copy sold adds to that item's count in the world. Don't sell items that other zones
  load with a world limit. Store-room load lines are best without a world limit, since copies
  players have bought count toward it.
- With `deduct`, the zone's load lines decide how many can be sold and how often stock
  returns.
- To convert an old keeper, remove his `.shp` entry.

## Code changes (outline)

| Area | Change |
|---|---|
| `structs.h` | New `char*` options field on the mob, owned by the prototype like the other mob text fields |
| `db.cpp` mob loader | Read the optional trailing options string; copy/free it correctly between the prototype and loaded mobs |
| `shapemob.cpp` | Field 42 "options" (multi-line text editor), shown in both mob display views; `write_proto` writes it; reparse and warn on save |
| New `mob_options.{h,cpp}` | Generic line/key lookup, plus the vendor parser (store, hours, price lines → table with limits) and the "open now?" check. Pure functions, unit-tested |
| New folder `src/mob_progs/`, file `passive.cpp` | Home for passive/service mob programs. First resident: `SPECIAL(barter_vendor)` (list, buy, refusals, protection, give refusal). A later, separate move-only refactor migrates the existing passive programs (trainers, gatekeepers, ferries, herald…) out of `spec_pro.cpp` into it |
| `spec_ass.cpp` | Program 33 in both `virt_program_number` and `get_special_function`; extend `spec_pro_message[]` to index 33 |
| Boot | Parse and validate every program-33 prototype after rooms, objects and shops are loaded |
| `act_wiz.cpp` stat | Show options in mob `stat` |
| Docs | `docs/data-formats/world-files.md` (mob record), a builder guide for vendors, shaping help |

The external script that creates world files, and any other mob-file readers, need to know
about the new line.

### Build and environment changes (each reviewed by the user individually)

`src/mob_progs/` is the first source subfolder besides `tests/`. Each change below is
proposed separately for the user's review before it's made, because the server side may
depend on things not visible from this repo:

1. `src/Makefile` `OBJFILES`: add `mob_progs/passive.o` (plus `mob_options.o`), with dependency
   lines.
2. `src/Makefile` `clean`: also remove `mob_progs/*.o`. Today it is `rm -f *.o`, which would
   leave stale objects in the folder.
3. Includes: files in the folder use `#include "../structs.h"` (as `tests/` does), or add `-I.`
   to the compile flags. Pick one.
4. `src/CMakeLists.txt`: add `mob_progs/passive.cpp` and `mob_options.cpp` to the server and
   test source lists.
5. `make format` globs top-level `*.cpp` only. Leave as is (formatting is done per file) or
   extend it.
6. `scripts/deploy.py`: uploads with `put -r *` and backs up with a recursive copy, so it
   already handles subfolders. To confirm against the server, not change.
7. Docker / `scripts/rots-docker.sh`: check nothing assumes a flat `src/`.

## Testing

- **Unit (gtest):**
  - Generic options lookup.
  - Vendor parser: each setting, every bad form of a price line, the limits (30 lines, 4
    currencies, qty 1–100), duplicate items, a first-wins check.
  - `hours=`: plain, overnight and multi-window, each bad form, the edge hours.
  - `list` building: in stock + priced, priced but not in stock, in stock but not priced, `(N
    left)`, column layout and name wrapping within 78 columns.
  - Payment: exact amounts, one currency short (nothing taken), currency in a bag not counted.
  - `deduct`: one store copy removed per purchase; the item disappears when the last copy
    goes.
  - Loader: old files without options, new files with them.
- **In game (local test server):**
  - A vendor with a good setup, and one with each kind of bad setup (boot warnings shown).
  - `list`; `buy` by name and number; paying, and failing to pay.
  - `deduct` running out, then restocking on zone reset.
  - Two vendors sharing a store room.
  - Carry limits; closed hours.
  - Refusals by side, `rp_flag`, shadow, invisibility.
  - Attacking: melee, spells, and **non-damage skills such as bash**, which are untested
    against the old hook too.
  - `give`.
  - Editor save and reload round trip.
- **New mob field: extra testing (high risk).** Adding a field to every mob touches loading,
  saving, editing and freeing, and mistakes here corrupt world files or crash. Test each point:
  - **Boot loader** (`db.cpp`): real world files, which use `\n\r` line endings (the peek for
    `#`/`$` must skip `\r`); a mob with and without options; the last mob before `$`; an empty
    `~` string; multi-line text; a file mixing old and new records.
  - **Editor's own loader** (`shapemob.cpp` ~1318 reads mob files with `get_text`, separately
    from `db.cpp`): must learn the new line too. Test loading old and new files into the
    editor.
  - **Save round trip:** edit → save → reboot → identical mob, repeated twice. Saving writes
    every mob in the file, so a file of old-format mobs gets the new line on every mob; diff
    the file before and after.
  - **Memory ownership:** loaded mobs share the prototype's strings, and `free_char` only frees
    a string that differs from the prototype's (`db.cpp:3630`). The options string needs the
    same guard. Test purging, killing and extracting mobs with options, and editor
    create/abort/save, looking for double frees and leaks.
  - **Live edit:** saving a mob while copies of it are loaded must not leave those copies
    pointing at a freed options table. They should look it up through the prototype each
    time, not keep their own pointer.
  - **Other readers:** `mob_csv_extract`, the external world-file script, and any
    `reports/`/`testing/` tools that parse `.mob` files.
  - **Rollback:** confirm an older binary fails on new files as expected, and document the
    revert procedure.
- **Timing:** measure `list` and `buy` on a vendor at the limits (30 lines, a full store
  room) to confirm no noticeable cost.

## Out of scope / tabled

Coins, deferred as one unit: coin amounts in price lines **and** buying from players
(`sell`/`buyback=`, where sold items would go, vendor cash, accepted item types) are to be
designed and built together, or not at all yet. Also tabled: `markup=`,
material filters, room restriction, closing-time behaviour (push out / lock), and the script
ASSIGN_INV container-search bug (recorded separately, to be fixed later).

## Decisions made while writing (please confirm)

1. Items in `list` appear in the order of the vendor's price lines.
2. With duplicate price lines for one item, the first wins and later ones are warned and
   skipped.
3. Unrecognised option lines are ignored at use and warned at boot/save.
4. The editor field number is 42 (next free after 41 "will teach"). New code files:
   `mob_options.{h,cpp}` (general, in `src/`) and `src/mob_progs/passive.cpp` (the vendor).
5. The boot check also warns about a program-33 mob that has a hard-coded procedure.
