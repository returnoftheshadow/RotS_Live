# Shape Object Command

Object shaping is the in-game workflow for creating or editing prototypes stored
under `world/obj/*.obj`. The entry point is `shape object …` inside
`src/shapemob.cpp`, while the interactive editor lives in `src/shapeobj.cpp`.
This guide explains how to start a session, what the slash commands do, and
what each numeric menu option edits.

## Prerequisites

- **Builder access** – `get_permission(zone, ch)` must grant write privileges
  for the target zone. Implementors (`object_master_idnum`, etc.) bypass this,
  but regular builders need explicit access.
- **Vnum assignment** – use the in-game `register` command (see `MAN WIZ REGISTER`)
  to reserve a new
  object vnum before running `shape object new <zone>`. Editing existing objects
  requires knowing their vnums (`stat obj` or `show zone` helps).
- **Prompt awareness** – when the object editor is active your prompt number
  changes to `6`, reminding you that every command must be prefixed with `/`.
- **Zone fit** – skim the OBJLIST guidelines to ensure the item type suits
  the zone’s weapon/armor categories before you start shaping.

## Starting a session

| Command | When to use | Notes |
|---------|-------------|-------|
| `shape object <vnum>` | Edit an existing object | Loads the prototype from `world/obj/<zone>.obj` and places it in the editor buffer. |
| `shape object new <zone#>` | Begin a fresh object | Creates a blank template, points it at `world/obj/<zone>.obj`, and jumps into the creation sequence (`/49`). Follow up with `/add <zone#>` to assign the final vnum. |

Once loaded you’ll see “You start shaping an object.” and the editor prompt
appears. All further commands must start with `/` (per the builder manual’s
GENERAL section). Inside the editor, `/help` or `/0` mirrors the `MAN SHAPE OBJ
<number>` entries, so keep that manual handy for deep dives.

## Session control commands (`extra_coms_obj`)

| Command | Purpose & behaviour |
|---------|---------------------|
| `/create <zone#>` | Sets target files (`world/obj/<zone>.obj` and `world/obj/oldobjs/<zone>.obj`), allocates a blank template via `new_obj()`, and starts the creation chain. |
| `/load <vnum>` | Reads the specified object into the editor buffer (`load_object()`). Refuses if another object is already loaded. |
| `/save` | Writes the edited object back over its existing record (`replace_object()`), first backing up to `world/obj/oldobjs/<zone>.obj`. |
| `/add <zone#>` | Appends the buffer as a brand-new vnum at the end of the zone file (`append_object()`), also producing a backup copy. Use this after `shape object new`. |
| `/delete` | Two-step safety. First call arms deletion and asks for “yes”. Typing `yes` immediately afterward flags the next `/save` to remove the object from disk. |
| `/implement` | Calls `implement_object()` to push the edited object into the live `obj_proto[]` array without touching disk. Useful for testing changes right away. |
| `/done` | Convenience action: `/save`, `/implement`, then `/free`. Ends the session cleanly. |
| `/free` | Releases the editor buffer (`free_object()`), clears shaping flags, and restores your normal prompt/position. Always free before switching targets. |

Entering any other word after `/` prints the supported verbs and leaves you in
edit mode.

## Editing workflow

Type `/0` (or any non-digit) to display the numeric menu (`list_help_obj()`),
then run `/1`, `/2`, etc. to change fields. Inputs fall into four patterns:

1. **Text entry** (`LINECHANGE` / `DESCRCHANGE`) uses the standard `%f`/`%e`
   editor. `%q` cancels and keeps the previous string.
2. **Single-value prompts** (`DIGITCHANGE`) leverage `string_to_new_value()`, so
   you can enter absolute numbers (`123`), add/subtract (`+5`, `-2`), or toggle
   bit positions (`p7`, `m3`). Blank lines keep the old value.
3. **Multi-value prompts** (e.g., `/12`, `/19`) expect space-separated numbers
   on one line. Press Enter with no input to abort.
4. **Creation chain** (`/49`) toggles `SHAPE_CHAIN` and automatically steps you
   through the recommended sequence (`obj_chain[]`). It’s a nice guided tour
   when drafting new gear.

## Field reference

### Text fields

| `/n` | Field | Notes |
|------|-------|-------|
| `/1` | Aliases | Space-separated keywords players type (`get axe`). For drink containers, make the liquid name the first alias (`beer mug`). |
| `/2` | Reference description | Short description shown in inventory lists (“a steel longsword”). No trailing period. |
| `/3` | Full (in-room) description | How the object appears in a room (“A steel longsword lies here.”). Capitalize and end with a period. |
| `/4` | Action description | Multi-line `look` text (paragraph). Treat like a room description with `%f` formatting. For ITEM_NOTE objects this is the readable body of the note. |

### Extra descriptions

| `/n` | Behaviour |
|------|-----------|
| `/5` | Push a new extra description onto the list (no input). Automatically chains to `/6` and `/7`. |
| `/6` | Edit the keyword list for the current extra description (lowercase words, space-separated, avoid punctuation). |
| `/7` | Edit the text for the current extra description. Use `%f` if needed. |
| `/8` | Remove the current/last extra description. |

Extra descriptions work as a stack. Run `/5`, then `/6`/`/7` to populate the
new record. `/8` removes the most recent entry.

### Flags and wear slots

| `/n` | Field | Notes |
|------|-------|-------|
| `/9` | Type flag | See the `ITEM_*` constants in `structs.h`. Determines how `/12` values are interpreted. |
| `/10` | Extra flags | Bitvector (glow, humming, magic, nodrop, etc.). Use `p<bit>`/`m<bit>` to toggle. |
| `/11` | Wear flags | Bitvector (TAKE, FINGER, NECK, …). At minimum set `TAKE` for portable items and `WIELD` for weapons. |

Extra-flag bits:

- `0` (`1`) GLOW
- `1` (`2`) HUMMING
- `2` (`4`) DARK
- `3` (`8`) BREAKABLE (keys, brittle items)
- `4` (`16`) EVIL
- `5` (`32`) INVISIBLE
- `6` (`64`) MAGIC
- `7` (`128`) NODROP
- `8` (`256`) BROKEN
- `9` (`512`) ANTI_GOOD (avoid unless directed)
- `10` (`1024`) ANTI_EVIL (avoid)
- `11` (`2048`) ANTI_NEUTRAL (avoid)
- `12` (`4096`) NORENT

Wear-flag bits:

`0` TAKE, `1` FINGER, `2` NECK, `3` BODY, `4` HEAD, `5` LEGS, `6` FEET, `7`
HANDS, `8` ARMS, `9` SHIELD, `10` ABOUT BODY, `11` WAIST, `12` WRIST, `13`
WIELD, `14` HOLD, `15` THROW, `16` LIGHT-SOURCE, `17` BELT.

### Core stats & metadata

| `/n` | Field | Notes |
|------|-------|-------|
| `/12` | Values[0..4] | Five integers whose meaning depends on the type flag. Enter five numbers at once. See the “Object values” section below. |
| `/13` | Weight | Stored in hundredths of a kilogram. A one-kilogram item is `100`. Pickable objects **must** have a non-zero weight; non-takeable props may stay `0`. Use the shapetable guideline: wielding two-handed roughly requires Strength equal to the weight in kg (so 8 kg takes STR 8), while one-handed wielding needs double that. |
| `/14` | Cost | Shop price guideline (typically `10 * level^2` for levels ≤10, doubling thereafter). |
| `/15` | Rent / cost per day | Suggested rent per in-game hour (`level^2` for ≤5, otherwise `(level^3)/5`). Matches the original “cost per day” design. |
| `/16` | Level | Represents the quality/tier of the item. Keep it near the mobs that drop it. |
| `/17` | Rarity | Reserved for future random generators; leave at `0` unless directed otherwise. |
| `/18` | Material | Integer index into `object_materials[]` (`cloth`, `leather`, `metal`, etc.). |

Common material ids:
`0` usual, `1` cloth, `2` leather, `3` chain, `4` metal, `5` wood, `6` stone,
`7` crystal, `8` gold, `9` silver, `10` mithril, `11` fur, `12` glass,
`13` plant.

### Affects and scripts

| `/n` | Field | Notes |
|------|-------|-------|
| `/19` | Object affects | Enter pairs like `( 18 10 ) ( 17 5 )` to apply +10 OB and +5 dodge. Slots beyond `MAX_OBJ_AFFECT` are ignored. Use `(0 0)` fillers if you’re not sure. |
| `/20` | Program number | Legacy prog hook; almost never used. Only touch if a senior implementor asks you to. |
| `/21` | Script number | Slots the object into the mudlle/script subsystem. Also restricted to special cases. |
| `/49` | Creation sequence | Walks you through aliases → descriptions → flags → stats using `obj_chain[]`. Great for new items. |
| `/50` | List | Calls `list_object()` and prints every field for auditing. |

## Object value reference (command `/12`)

Because `/12` edits five raw integers, you **must** consult the per-type
definitions below. New objects start with zeros (the TRASH
defaults), so adjust every slot unless you truly want a junk item. The table
below summarizes every entry documented there:

| Entry | Value meanings |
|-------|----------------|
| LIGHT (type 1) | `value[2]` = burn hours (`0` = burnt out, `<0` = eternal). All other slots unused. |
| WEAPON (5) | `value[0]` OB, `value[1]` parry bonus, `value[2]` bulk (≈2/3 feet), `value[3]` attack category (2=whip, 8=axe, 11=pierce…), `value[4]` damage. |
| ARMOR (9) | `value[0]` `0` for auto absorb, `-1` to disable; `value[1]` min absorb; `value[2]` encumbrance; `value[3]` dodge bonus; `value[4]` reserved. |
| WORN (11) | Deprecated catch-all. Leave unused—create light armor with zeroed armor values instead. |
| OTHER (12) | All zeros. Use when no other category fits. |
| TRASH (13) | All zeros. Pure flavour items. |
| CONTAINER (15) | `value[0]` capacity (hundredths of kg), `value[1]` flags (1 closeable / 2 pickproof / 4 closed / 8 locked), `value[2]` key vnum (`-1` none), `value[3]` corpse rot timer, `value[4]` unused. |
| NOTE (16) | `value[0]` language id (tongue). Others unused. |
| DRINKCON (17) | `value[0]` max units, `value[1]` current units, `value[2]` liquid type (`LIQ_WATER` … `LIQ_CLEARWATER`), `value[3]` poison flag, `value[4]` unused. |
| KEY (18) | `value[0]` key/lock id (match door’s lock vnum). Others unused. |
| FOOD (19) | `value[0]` hours of fullness; `value[3]` poison flag; rest unused. |
| MONEY (20) | `value[0]` number of coins. Others unused. |
| BOAT (22) | No special values; leave zeros. |
| FOUNTAIN (23) | Same layout as DRINKCON. |
| SHIELD (24) | `value[0]` dodge bonus, `value[1]` parry bonus, `value[2]` encumbrance, `value[3]` shield block coefficient, `value[4]` reserved. |
| LEVER (25) | `value[0]` room vnum containing the door, `value[1]` direction (0–5 for N/E/S/W/U/D). Always mark levers as NOTAKE and set the matching door flag. |

Copy values from similar objects with `/50` when in doubt, and stick to the
attack-category guidelines listed under the weapon summary above when selecting
message types.

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

### Bitvector reference (`OBJ 19` / `OBJ BITVECTOR`)

- `/19` expects `(location modifier)` pairs. `location` corresponds to the
  APPLY table below (OB, dodge, regen, spell bonuses, etc.).
- For `APPLY_BITVECTOR` (location `28`), `modifier` represents the bit number
  shown in the affect-bit table (e.g., `AFF_DETECT_HIDDEN = 0`,
  `AFF_SANCTUARY = 7`). Use `p`/`m` syntax when editing extra/wear flags, but
  stick to `(location modifier)` tuples for `/19`.
- For `APPLY_SPELL` (location `27`), encode `modifier` as `256 * spell_level +
  spell_number`. Location `30` (RESISTANCE) and `31` (VULNERABILITY) treat the
  modifier as the bit position documented in `MAN SHAPE MOB 36`.
- Keep a reference of the effect list handy; when unsure, default to `(0 0)` and
  ask an implementor before granting powerful affects like sanctuary or haste.
  The original table cautions that some flags do not behave as expected, so test
  thoroughly before shipping unusual combinations.

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
| 12 | AFF_PROTECT_EVIL |
| 13 | AFF_PARALYSIS |
| 14 | AFF_GROUP |
| 15 | AFF_CONFUSE |
| 16 | AFF_SLEEP |
| 17 | AFF_BASH |
| 18 | AFF_DETECT_EVIL |
| 19 | AFF_DETECT_INVISIBLE |
| 20 | AFF_FEAR |
| 21 | AFF_BLIND |
| 22 | AFF_FOLLOW |
| 23 | AFF_SWIM |
| 24 | AFF_HUNT |
| 25 | AFF_EVASION |
| 26 | AFF_WAITING |
| 27 | AFF_WAITWHEEL |
| 28 | AFF_ORC_DELAY |
| 29 | AFF_CONCENTRATION |
| 30 | AFF_HAZE |

Use the following `APPLY_*` codes when filling the `(location modifier)` pairs:

| Code | Applies to | Notes |
|------|------------|-------|
| 0 | APPLY_NONE | Placeholder, no effect. |
| 1 | APPLY_STR | Strength |
| 2 | APPLY_DEX | Dexterity |
| 3 | APPLY_INT | Intelligence |
| 4 | APPLY_WIS | Wisdom |
| 5 | APPLY_CON | Constitution |
| 6 | APPLY_LEA | Leadership |
| 7 | APPLY_PROF | Proficiency points |
| 8 | APPLY_LEVEL | Character level |
| 9 | APPLY_AGE | Age |
| 10 | APPLY_CHAR_WEIGHT | Weight |
| 11 | APPLY_CHAR_HEIGHT | Height |
| 12 | APPLY_MANA | Stamina/mana |
| 13 | APPLY_HIT | Hit points |
| 14 | APPLY_MOVE | Movement points |
| 15 | APPLY_GOLD | Money |
| 16 | APPLY_EXP | Experience |
| 17 | APPLY_DODGE | Dodge bonus |
| 18 | APPLY_OB | Offensive bonus |
| 19 | APPLY_DAMROLL | Damage bonus |
| 20 | APPLY_SAVING_SPELL | Saving throws |
| 21 | APPLY_WILLPOWER | Will |
| 22 | APPLY_REGEN | Energy regen |
| 23 | APPLY_VISION | Positive values give infravision, negatives blind |
| 24 | APPLY_SPEED | Initiative/speed |
| 25 | APPLY_PERCEPTION | Search/listen |
| 26 | APPLY_ARMOR | Generic armor modifier |
| 27 | APPLY_SPELL | Encodes spell/level via `256*level + spell_number` |
| 28 | APPLY_BITVECTOR | Adds/removes affect bits (see table above) |
| 29 | APPLY_MANA_REGEN | Mana regen per tick |
| 30 | APPLY_RESISTANCE | Bitvector from `MAN SHAPE MOB 36` |
| 31 | APPLY_VULNERABILITY | Bitvector from `MAN SHAPE MOB 36` |

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
110 30 6 8 32               # OB 110, parry 30, bulk 6, axe slash, damage 32
/13
650                         # 6.5 kg two-hander
/16
45
/18
10                          # mithril material
/19
( 18 12 ) ( 17 5 )          # +12 OB, +5 dodge
/14
20250
/15
3645
/save
/implement
/done
```

### Create a brand-new quest note

```
register                     # get the next open vnum, say 4205
shape object new 42          # start a template in zone 42
/49                          # run through the guided sequence
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
16                          # ITEM_NOTE
/12
5 0 0 0 0                   # language 5 (Sindarin)
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
/19
( 0 0 ) ( 0 0 )             # no magical affects
/add 42                     # assign the next object vnum in zone 42
/save
/implement
/done
```

### Retune a drink container instead of duplicating one

```
shape object 13302           # waterskin full of water
/5                          # add an extra desc for the scent
/6
brew smell
/7
   A sweet scent of mulled cider rises from the mouth of the flask.
%f
%e
/12
40 40 2 0 0                # 5 drinks of cider (LIQ_WINE=2)
/18
5                          # leather
/19
( 19 2 ) ( 0 0 )           # +2 damage (maybe the brew inspires courage)
/14
800
/15
160
/save
/implement
/done
```

These sequences show both modification and net-new creation: inspect with `/50`,
update text fields, tune stats/values, adjust special fields, then `/save`,
`/implement`, `/done`.

## Troubleshooting & tips

- “No object loaded for shaping” – you issued a numeric command before
  `shape object …` or `/load`. Run `/load <vnum>` or restart.
- “You released an object and stopped shaping” – you may have typed `/free`
  accidentally. Reload and resume editing.
- Remember that `/12` overwrites all five values. If you only want to change
  one, re-enter all five numbers, or use `/49` to walk the defaults again.
- When duplicating an item, load the source, `/save` it under a new vnum using
  `/add`, then immediately change aliases/descriptions to avoid identical
  objects.
- Keep an eye on weight vs. wear slots: weapons need `WIELD`, shields need
  `SHIELD`, armour must include the appropriate body slot plus `TAKE`.
- Only assign programs/scripts if you have mudlle support in place. Ordinary
  builder items should leave `/20` and `/21` at `0`.

Documenting `shape object` alongside `shape room` creates a consistent reference
for builders. Future sections (mob, zone, script) can point back here for slash
command etiquette and numeric input conventions.
