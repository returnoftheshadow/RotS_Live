# Shape Object Command

Object shaping is the in-game workflow for editing prototypes stored under
`world/obj/*.obj`. The entry point is `shape object …` inside
`src/shapemob.cpp`, while the interactive editor lives in `src/shapeobj.cpp`.
This guide explains how to start a session, what the slash commands do, and
what each numeric menu option edits.

## Prerequisites

- **Builder access** – `get_permission(zone, ch)` must grant write privileges
  for the target zone. The object master (`object_master_idnum`, set with
  `shape master_object <idnum>`) bypasses this; regular builders need explicit
  access.
- **The zone file must already exist** – object files are created outside the
  game. The editor never creates a file and never picks a vnum for you: you
  shape an object by its vnum, and a vnum that is not in the file yet becomes a
  new record in it when you `/save`.
- **Prompt awareness** – while an object is loaded your prompt reads
  `Object: <vnum>`. It changes to `Lin]` at a single-line text prompt and `Dgt]`
  at a number prompt.
- **Zone fit** – skim the OBJLIST guidelines to ensure the item type suits
  the zone’s weapon/armor categories before you start shaping.

## Starting a session

| Command | When to use | Notes |
|---------|-------------|-------|
| `shape object <vnum>` | Edit an existing object, or start a new one | Loads the prototype from `world/obj/<vnum/100>.obj`. If that vnum is not in the file, the editor says `Could not find obj #N, created it.` and gives you a blank template under that number; `/save` writes it into the file in vnum order. |
| `shape object new <zone#>` | – | Disabled: prints `"shape object new 42" has been disabled due to a bug.` It used to pick the file's last vnum + 1, which could run past the zone's range. |

Once loaded you’ll see “You start shaping an object.” and the editor prompt
appears. Editor commands and field numbers start with `/` (per the builder
manual’s GENERAL section). `help shape obj <number>` shows the manual entry for
a field.

A brand-new vnum cannot be tested with `/implement` until the next reboot
(`This object does not exist (yet). Maybe reboot will help.`); `/save` it and
it loads at boot.

## Session control commands (`extra_coms_obj`)

| Command | Purpose & behaviour |
|---------|---------------------|
| `/save` | Writes the edited object back into its zone file (`replace_object()`), first copying the file to `world/obj/oldobjs/<zone>.obj`. A vnum not yet in the file is inserted in vnum order. |
| `/add` | Same as `/save` for a loaded object. `/add <anything>` is disabled (`"/add foo" has been disabled due to a bug.`): it used to write the object into `world/mob/<file>.mob`. |
| `/delete` | Disabled: `"/delete" has been disabled due to a bug.` It rewrote the zone file to drop the object, saved never-saved objects instead, and left the editor stuck. |
| `/new` | Disabled: `"/new 42" has been disabled due to a bug.` |
| `/implement` | Calls `implement_object()` to push the edited object into the live prototype without touching disk. Objects loaded from now on use the new values. Copies already lying in the game keep what they had until they are loaded again. |
| `/done` | `/save`, then `/implement`, then `/free`. If the save fails (no permission, a file that can't be opened) it stops with `Not saved - still shaping. Fix the problem and /done again, or /free to discard.` and your edits are kept. |
| `/free` | Releases the editor buffer (`free_object()`), clears shaping flags, and restores your normal prompt/position. Unsaved changes are discarded. |

Entering any other word after `/` prints the supported verbs and leaves you in
edit mode. A word is matched by prefix, so `/d` means `/done`, not `/delete`.

## Editing workflow

Type `/0` (or any number that is not a field) to display the numeric menu
(`list_help_obj()`), then run `/1`, `/2`, etc. The value always goes on the
**next** line: anything typed after `/N` on the same line is ignored. While a
prompt is waiting, every line you type is the answer – including `/free` or
`/50` – so answer the prompt first. Inputs fall into four patterns:

1. **Single-line text** (`/1`, `/2`, `/3`, `/6`). The prompt shows the current
   text in brackets. A blank line keeps it. `%q` empties the field (refused for
   `/1` and `/2`, which must not be empty). `#` becomes `+` and `~` becomes `-`.
2. **Multi-line text** (`/4`, `/7`) opens the string editor: `%e` saves and
   exits, `%q` aborts and keeps the old text, `%f` formats, `%h` shows help.
3. **Single-value prompts** (`DIGITCHANGE`) show `[current]` and use
   `string_to_new_value()`: a number sets it (`123`), `+5` adds, `-2`
   **subtracts** (only `/15` treats `-N` as the negative number), `p7` sets bit
   7, `m3` clears bit 3 (one bit per answer). A blank line, or a word, keeps
   the old value.
4. **Multi-value prompts** (`/12`, `/19`) take several numbers on one line and
   show the current values. A blank line keeps them all.

`/49` walks the creation sequence: `/1` → `/2` → `/3` → `/4` → `/9` → `/11` →
`/12` → `/13` → `/14` → `/15` (`obj_chain[]`). It skips extra flags, level,
rarity, material, affects and script.

## Field reference

### Text fields

| `/n` | Field | Notes |
|------|-------|-------|
| `/1` | Aliases | Keywords separated by spaces (`sword longsword`), not commas. For drink containers and fountains the **first word is the liquid name**: it is removed when the container is emptied and put back when it is filled. Can't be emptied with `%q`. |
| `/2` | Reference description | The name in action messages and inventory lists (“a steel longsword”). No capital, no final period (boot lowercases a leading A/An/The; `/implement` does not). Can't be emptied with `%q`. |
| `/3` | Room line | One line shown when the object lies in a room (“A steel longsword is lying here.”). Capitalize and end with a period. The menu and `/50` call it “full description”. |
| `/4` | Look text | Multi-line text shown by `look at`/`examine` and by `identify`. For ITEM_NOTE objects it is the written text of the note. The file calls it the action description. |

### Extra descriptions

| `/n` | Behaviour |
|------|-----------|
| `/5` | Adds a new extra description at the end of the list and chains to `/6` and `/7` for it. |
| `/6` | Keywords of the **last** extra description (the words `look <word>` matches), separated by spaces. |
| `/7` | Text of the **last** extra description (multi-line editor). |
| `/8` | Removes the last extra description. |

Only the last extra description can be edited; `/50` lists them all, each
labelled `(6)`/`(7)`. The editor keeps them in file order, so saving no longer
reverses them. After `/implement`, removed or renamed extra descriptions are
gone for newly loaded copies; copies already in the game keep the old ones.

### Flags and wear slots

| `/n` | Field | Notes |
|------|-------|-------|
| `/9` | Type | Prompt lists the types. Must be 1-25 (`Item type must be 1-25. dropped.` otherwise). Changing the type changes what `/12` means. |
| `/10` | Extra flags | Prompt lists the bits. `pN`/`mN` toggle one bit per answer, or type the full number. Not in the `/49` sequence. |
| `/11` | Wear flags | Prompt lists the bits. At minimum set `TAKE` for portable items and `WIELD` for weapons. |

Type ids: `1` LIGHT, `2` SCROLL, `3` WAND, `4` STAFF, `5` WEAPON, `6` FIRE
WEAPON, `7` MISSILE, `8` TREASURE, `9` ARMOR, `10` POTION, `11` WORN, `12`
OTHER, `13` TRASH (new objects start here), `14` TRAP, `15` CONTAINER, `16`
NOTE, `17` LIQUID CONTAINER, `18` KEY, `19` FOOD, `20` MONEY, `21` PEN, `22`
BOAT, `23` FOUNTAIN, `24` SHIELD, `25` LEVER.

Extra-flag bits:

- `0` (`1`) GLOW
- `1` (`2`) HUM
- `2` (`4`) DARK
- `3` (`8`) BREAKABLE (keys can break)
- `4` (`16`) EVIL
- `5` (`32`) INVISIBLE
- `6` (`64`) MAGIC
- `7` (`128`) NODROP
- `8` (`256`) BROKEN (a broken key does not work)
- `9` (`512`) ANTI_GOOD (avoid unless directed)
- `10` (`1024`) ANTI_EVIL (avoid)
- `11` (`2048`) ANTI_NEUTRAL (avoid)
- `12` (`4096`) NORENT
- `13` unused
- `14` NOINVIS
- `15` WILLPOWER (can hit wraiths)
- `16` IMM
- `17`-`27` race locks: HUMAN, DWARF, WOODELF, HOBBIT, BEORNING, URUK, ORC,
  MOBORC, MAGUS, OLOGHAI, HARADRIM (the wrong race is zapped on equip)
- `28` STAY_ZONE

Wear-flag bits:

`0` TAKE, `1` FINGER, `2` NECK, `3` BODY, `4` HEAD, `5` LEGS, `6` FEET, `7`
HANDS, `8` ARMS, `9` SHIELD, `10` ABOUT BODY, `11` WAIST, `12` WRIST, `13`
WIELD, `14` HOLD, `15` THROW, `16` BACK, `17` BELT.

### Core stats & metadata

| `/n` | Field | Notes |
|------|-------|-------|
| `/12` | Values[0..4] | The prompt names the five slots for the current type (`Enter VALUES for WEAPON: OB PARRY BULK WEAPON_TYPE -`, `-` = not used) and shows `Current:`. Typing fewer than five numbers changes only the first ones and keeps the rest; a blank line keeps all. Negative numbers can be typed directly. See “Object values” below. |
| `/13` | Weight | In 1/100 lb (`identify` shows weight/100 as lbs). `0` counts as 1. Wielding: above `150*STR` it can't be wielded, above `100*STR` it is too heavy, above `50*STR` too heavy for one hand. Also feeds weapon damage/speed and armour absorb. |
| `/14` | Cost | Shop price in **copper** (times the shop's profit). `0` means shops won't trade it. Also the base for rent when `/15` is `-1`. |
| `/15` | Rent per day | `-1` = use cost/100; **below -1 = the item can't be rented**. `-N` sets the negative value directly. Stored as a 16-bit number. |
| `/16` | Level | 0-255 (`Level must be 0-255. dropped.` otherwise). Item power, not a level needed to use it: feeds weapon damage (reduced for low-level wielders), armour absorb, bow damage, rent, butchering and shop restocking. |
| `/17` | Rarity | Unused by the game. |
| `/18` | Material | Prompt lists the ids. Must be 0-13 (`Material must be 0-13. dropped.` otherwise). Smiting weapons do extra damage to `4` metal; arrows can break on `3` chain and `4` metal; shops filter on it. |

Material ids:
`0` usual, `1` cloth, `2` leather, `3` chain, `4` metal, `5` wood, `6` stone,
`7` crystal, `8` gold, `9` silver, `10` mithril, `11` fur, `12` glass,
`13` plant.

### Affects and scripts

| `/n` | Field | Notes |
|------|-------|-------|
| `/19` | Object affects | Prompt lists the location codes and shows `Current: (a b) (c d)`. Type up to **two** pairs like `(18 10) (17 5)` for +10 OB and +5 dodge; extra pairs are ignored. A slot you don't type keeps its old value; a blank line keeps both. `(0 0)` is an empty slot and is not saved (an empty first slot no longer drops the second one). |
| `/20` | Program | Not saved and has no effect on the prototype. Leave it alone. |
| `/21` | Script vnum | A `world/scr` script number (not mudlle); `0` = none. Object triggers: ON_DAMAGE, ON_EAT, ON_ENTER, ON_EXAMINE_OBJECT, ON_DRINK, ON_PULL, ON_WEAR. The number is not checked to exist. |
| `/49` | Creation sequence | See “Editing workflow” above. |
| `/50` | List | Calls `list_object()` and prints every field for auditing. |

## Object value reference (command `/12`)

Because `/12` edits five raw integers, consult the per-type meanings below (the
`/12` prompt names them too). New objects start with zeros. “unused” means no
game code reads that slot for that type.

Every worn item's `value[2]` also counts towards the wearer's encumbrance for
its slot, whatever the type.

| Type | Value meanings |
|------|----------------|
| LIGHT (1) | `value[2]` = hours of burn: counts down while lit, the light is destroyed at 0; `0` = can't be lit; `<0` = never burns out. `value[3]` = lit state (set by the game; `<0` can't be blown out). `value[0]`, `[1]`, `[4]` unused. |
| SCROLL (2), POTION (10) | No game code reads the values. |
| WAND (3), STAFF (4) | `value[2]` charges (each use takes one), `value[3]` spell number. `value[0]`, `[1]`, `[4]` unused. |
| WEAPON (5) | `value[0]` OB (also a damage input, capped at 40 there), `value[1]` parry (also a damage input), `value[2]` bulk (OB/damage/speed; above 4 forces a two-handed grip without a shield), `value[3]` weapon type: 2 whipping, 3 slashing, 4 two-handed slashing, 5 flailing, 6 bludgeoning, 7 two-handed bludgeoning, 8 cleaving, 9 two-handed cleaving, 10 stabbing (spear), 11 piercing, 12 smiting, 13 bow, 14 crossbow. `value[4]` unused (damage is computed from the other values). |
| FIRE WEAPON (6) | Combat only accepts WEAPON; the values are not read. |
| MISSILE (7) | `value[1]` to-damage, `value[3]` break chance % against chain/metal armour. `value[2]` is overwritten with the shooter at run time. `value[0]`, `[4]` unused. |
| ARMOR (9) | `value[0]` `-1` = absorbs nothing, any other value = normal; `value[1]` minimum absorb (a flat reduction per hit); `value[2]` encumbrance (raises % absorb); `value[3]` dodge bonus; `value[4]` unused. |
| WORN (11), OTHER (12), TRASH (13), TREASURE (8), PEN (21) | Values not read. |
| TRAP (14) | Values not read. |
| CONTAINER (15) | `value[0]` capacity in weight units, **including the container's own weight**; `value[1]` flags (1 closeable / 2 pickproof / 4 closed / 8 locked); `value[2]` key object vnum (`<0` = can't be locked); `value[3]` `1` marks a corpse – leave it `0`; `value[4]` is set by the game from `value[1]` (the lock state it resets to). A container named “quiver” holds arrows. |
| NOTE (16) | Values not read (`value[0]` shows as the language in `stat`, but nothing uses it). |
| LIQUID CONTAINER (17) | `value[0]` max units, `value[1]` current units (drinking also lowers the weight), `value[2]` liquid id (table below), `value[3]` non-zero = poisoned, `value[4]` unused. |
| KEY (18) | Values not read. A lock matches the key object's **vnum** (stored on the door or container). |
| FOOD (19) | `value[0]` fullness it gives; `value[3]` non-zero = poisoned; rest unused. |
| MONEY (20) | `value[0]` amount in **copper** (100 = 1 silver, 1000 = 1 gold). Others unused. |
| BOAT (22) | Having one lets you travel over water. Values not read. |
| FOUNTAIN (23) | Same layout as LIQUID CONTAINER; `value[1]` refills to `value[0]` every object tick. |
| SHIELD (24) | `value[0]` dodge bonus, `value[1]` parry bonus, `value[2]` encumbrance, `value[3]`/`value[4]` unused. |
| LEVER (25) | `value[0]` vnum of the room that has the door, `value[1]` direction 0-5 (N/E/S/W/U/D; anything else “seems to be broken”). Mark levers not TAKE and set the matching door flag. |

Copy values from similar objects with `/50` when in doubt.

### Liquid type ids

| Name | Id | Drunkness | Fullness | Thirst |
|------|----|-----------|----------|--------|
| LIQ_WATER | 0 | 0 | 1 | 10 |
| LIQ_BEER | 1 | 3 | 2 | 5 |
| LIQ_WINE | 2 | 5 | 2 | 5 |
| LIQ_ALE | 3 | 2 | 2 | 5 |
| LIQ_DARKALE | 4 | 1 | 2 | 5 |
| LIQ_WHISKY | 5 | 6 | 1 | 4 |
| LIQ_LEMONADE | 6 | 0 | 1 | 8 |
| LIQ_FIREBRT | 7 | 10 | 0 | 0 |
| LIQ_LOCALSPC | 8 | 3 | 3 | 3 |
| LIQ_SLIME | 9 | 0 | 4 | -8 |
| LIQ_MILK | 10 | 0 | 3 | 6 |
| LIQ_TEA | 11 | 0 | 1 | 6 |
| LIQ_COFFE | 12 | 0 | 1 | 6 |
| LIQ_BLOOD | 13 | 0 | 2 | -1 |
| LIQ_SALTWATER | 14 | 0 | 1 | -2 |
| LIQ_CLEARWATER | 15 | 0 | 0 | 13 |

### Affect reference (`/19`, `OBJ 19` / `OBJ BITVECTOR`)

- `/19` takes at most **two** `(location modifier)` pairs. `location` is an
  `APPLY_*` code from the table below.
- For `APPLY_BITVECTOR` (location `28`), `modifier` is the bit number from the
  affect-bit table (e.g. `0` detect hidden, `7` sanctuary).
- For `APPLY_SPELL` (location `27`), `modifier & 255` is the spell number
  (numbers of 128 and above are ignored). The old `256 * level + spell` form
  still parses, but the level part is never used.
- Locations `30` (resistance) and `31` (vulnerability) take a bit number
  documented in `MAN SHAPE MOB 36`.
- Test unusual combinations before shipping them, and ask an implementor
  before granting powerful affects like sanctuary.

The affect-bit reference:

| Bit # | Affect |
|-------|--------|
| 0 | AFF_DETECT_HIDDEN |
| 1 | AFF_INFRARED |
| 2 | AFF_SNEAK |
| 3 | AFF_HIDE |
| 4 | AFF_DETECT_MAGIC |
| 5 | AFF_CHARM |
| 6 | AFF_CURSE |
| 7 | AFF_SANCTUARY |
| 8 | AFF_TWOHANDED |
| 9 | AFF_INVISIBLE |
| 10 | AFF_MOONVISION |
| 11 | AFF_POISON |
| 12 | AFF_SHIELD |
| 13 | BREATHE |
| 14 | GROUP (unused) |
| 15 | AFF_CONFUSE |
| 16 | AFF_SLEEP |
| 17 | AFF_BASH |
| 18 | AFF_FLYING |
| 19 | AFF_DETECT_INVISIBLE |
| 20 | AFF_FEAR |
| 21 | AFF_BLIND |
| 22 | AFF_FOLLOW |
| 23 | AFF_SWIM |
| 24 | AFF_HUNT |
| 25 | AFF_EVASION |
| 26 | AFF_WAITING |
| 27 | AFF_WAITWHEEL |
| 28 | (unused) |
| 29 | AFF_CONCENTRATION |
| 30 | AFF_HAZE |
| 31 | AFF_HALLUCINATE |

Use the following `APPLY_*` codes when filling the `(location modifier)` pairs:

| Code | Applies to | Notes |
|------|------------|-------|
| 0 | APPLY_NONE | Empty slot, not saved. |
| 1 | APPLY_STR | Strength |
| 2 | APPLY_DEX | Dexterity |
| 3 | APPLY_INT | Intelligence |
| 4 | APPLY_WILL | Will (`identify` wrongly shows it as WIS) |
| 5 | APPLY_CON | Constitution |
| 6 | APPLY_LEA | Leadership |
| 7 | APPLY_PROF | No effect |
| 8 | APPLY_LEVEL | No effect |
| 9 | APPLY_AGE | Age in years |
| 10 | APPLY_CHAR_WEIGHT | Weight |
| 11 | APPLY_CHAR_HEIGHT | Height |
| 12 | APPLY_MANA | Max stamina/mana |
| 13 | APPLY_HIT | Max hit points |
| 14 | APPLY_MOVE | Max movement points |
| 15 | APPLY_GOLD | No effect |
| 16 | APPLY_EXP | No effect |
| 17 | APPLY_DODGE | Dodge bonus |
| 18 | APPLY_OB | Offensive bonus |
| 19 | APPLY_DAMROLL | Damage bonus |
| 20 | APPLY_SAVING_SPELL | Saving throw |
| 21 | APPLY_WILLPOWER | Willpower |
| 22 | APPLY_REGEN | **No effect** (use 24 for energy regen) |
| 23 | APPLY_VISION | Positive gives infravision, negative blinds |
| 24 | APPLY_SPEED | Energy regen (attack speed) |
| 25 | APPLY_PERCEPTION | Perception |
| 26 | APPLY_ARMOR | **No effect** |
| 27 | APPLY_SPELL | Spell number in `modifier & 255`; the level part is ignored |
| 28 | APPLY_BITVECTOR | Affect bit number (see table above) |
| 29 | APPLY_MANA_REGEN | Mana regen per tick |
| 30 | APPLY_RESISTANCE | Resistance bit number (`MAN SHAPE MOB 36`) |
| 31 | APPLY_VULNERABILITY | Vulnerability bit number (`MAN SHAPE MOB 36`) |
| 32 | APPLY_MAUL | Does nothing on items |
| 33 | APPLY_BEND | Bend |
| 34-37 | APPLY_PK_* | Not for items: logs a SYSERR |
| 38 | APPLY_SPELL_PEN | Spell penetration |
| 39 | APPLY_SPELL_POW | Spell power |

## Example workflows

### Modify an existing weapon

```
shape object 2503            # load an existing longsword
/50                          # inspect current stats
/1
mithril greatsword
/2
a gleaming mithril greatsword
/3
A gleaming mithril greatsword has been left here.
/4
   Etched runes crawl along the blade, humming with latent fire.
%f
%e
/9
5                           # ensure it's still a weapon
/12
40 30 6 9                   # OB 40, parry 30, bulk 6, two-handed cleaving; value[4] kept
/13
650                         # 6.5 lb
/16
45
/18
10                          # mithril material
/19
(18 12) (17 5)              # +12 OB, +5 dodge
/14
20250
/15
3645
/save
/implement
/done
```

### Create a new quest note

The zone file (here `world/obj/42.obj`) must already exist. Pick a free vnum
in the zone's range and shape it by number:

```
shape object 4205            # "Could not find obj #4205, created it."
/1
note parchment
/2
a sealed parchment note
/3
A sealed parchment note flutters here.
/4
   Wax stamped with a silver falcon holds the parchment closed.
%f
%e
/9
16                          # ITEM_NOTE (values are not read for notes)
/11
p0                          # TAKE
/13
5                           # light as paper
/14
500
/15
125
/16
10
/18
1                           # cloth
/save                       # inserted into 42.obj as #4205
/done                       # implement refuses a new vnum until reboot;
                            # /done still saves first
```

### Retune a drink container instead of duplicating one

```
shape object 13302           # waterskin full of water
/5                          # add an extra desc for the scent (chains to /6 /7)
brew smell
   A sweet scent of mulled cider rises from the mouth of the flask.
%f
%e
/12
40 40 2                     # 40 units of wine (LIQ_WINE=2); value[3..4] kept
/18
2                           # leather
/19
(19 2)                      # +2 damage (maybe the brew inspires courage)
/14
800
/15
160
/save
/implement
/done
```

These sequences show both modification and net-new records: inspect with `/50`,
update text fields, tune stats/values, adjust special fields, then `/save`,
`/implement`, `/done`.

## Troubleshooting & tips

- “No object loaded for shaping” – you issued a numeric command without an
  object loaded. Run `shape object <vnum>` first.
- “You released an object and stopped shaping” – you may have typed `/free`
  accidentally. Reload and resume editing.
- `/12` changes only the values you type: `40 30` changes `value[0]` and
  `value[1]` and keeps the other three.
- To copy an item, load the source, note its values with `/50`, then shape the
  new vnum and enter them; `/add` no longer creates a copy.
- Keep an eye on weight vs. wear slots: weapons need `WIELD`, shields need
  `SHIELD`, armour must include the appropriate body slot plus `TAKE`.
- Only assign scripts (`/21`) if the script exists. `/20` does nothing.
- `Not saved - still shaping` after `/done` means the save failed; fix the
  problem it reported (for example a missing `world/obj/oldobjs/` folder) and
  `/done` again, or `/free` to discard.
