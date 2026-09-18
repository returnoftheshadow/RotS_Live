# Shape Mob Command

`shape mob` is the in-game tool for creating or modifying mobile prototypes.
Mob definitions live in `world/mob/<zone>.mob` (or the alternate `world/prx`
paths for special zones). This guide describes the shaping interface, every
editable field, and the conventions expected by Return of the Shadow so you can
retire the old `shap_tbl` excerpts.

## Prerequisites

- **Permissions**: you must have builder rights for the zone (`get_permission`).
- **Vnum assignment**: reserve a mob number via `register` before creating a new
  entry (`shape mob new <zone>` automatically targets the right file).
- **Reference mob**: for quick sanity checks, keep another mob with similar
  behaviour handy and compare stats with `/50`.

## Workflow overview

| Action | Command |
|--------|---------|
| Load/create | `shape mob <vnum>` to edit an existing mob, or `shape mob new <zone>` to start from a blank template. |
| Mode toggle | `/simple` switches between simple (fields 1–12 + spirit) and extended editing; `/extended` switches back. |
| Show menu | `/0` (or any non-numeric input) prints the numeric command list for the current mode. |
| Edit field | `/<number>` runs the field editor described below. |
| List current values | `/50` dumps the whole mob definition (or `/49` to run the guided creation sequence). |
| Save & implement | `/save` writes to disk (after a backup); `/implement` pushes the temp mob into memory for live testing; `/done` performs save → implement → free. |
| Exit without saving | `/free`. |

Editing uses the standard editor syntax:

- Multiline text: enter text, `%f` to format, `%e` to finish (`%q` aborts).
- Numeric fields: enter the full value (e.g., `42`), apply offsets (`+5`, `-2`),
  or toggle bit numbers (`p7` sets bit 7, `m7` clears). Blank input keeps the
  previous value.

## Simple-mode fields

Simple mode exposes the minimum set needed for quick tuning:

| `/n` | Field | Notes |
|------|-------|-------|
| `/1` | Aliases | Lowercase keywords players type (`orc guard orc guard`). |
| `/2` | Reference description | Short desc (`a surly orc guard`). Used in lists. |
| `/3` | Full room description | The long description players see when entering the room. Must end in a newline and period. |
| `/4` | Detailed description | The text shown when someone `look <mob>`. |
| `/5` | Mob flags | Bitvector of behaviour flags (see “Mob flags” below). Input accepts sums or `p<n>` toggles. |
| `/6` | Affects | Bitvector of permanent affects applied to the mob (see “Affect flags”). |
| `/7` | Level | Combat level. Keep within expected zone ranges (check `/50` on similar mobs). |
| `/8` | Sex | `0` neutral, `1` male, `2` female. |
| `/9` | Race | Use the `RACE_*` constants from `src/structs.h` (see table in `docs/shape_script.md`). |
| `/10` | Body type | Determines hit locations (0 tiny, 1 humanoid, 2 quadruped, 3 tentacled, 4 bird). |
| `/11` | Race aggression | Bitvector built from race IDs (`1 << RACE_HUMAN`, etc.). Aggressive mobs attack those races on sight even without the `AGGRESSIVE` flag. |
| `/12` | Butcher item | Vnum of the object dropped when butchering the corpse (`0` for none). |
| `/40` | Mob spirit | Role-play marker shown in prompts (`docs/shape_script.md` lists the titles). Use the constants from `prompt_spirit` in `src/consts.cpp`. |

## Extended-mode field reference

| `/n` | Field | Description / Notes |
|------|-------|---------------------|
| `/1` – `/6` | Same as simple mode. |
| `/7` | Alignment | Stored in `specials2.alignment`, range roughly `-1000..1000`. Positive = good. |
| `/8` | Level | Combat level. |
| `/9` | Combat stats | Prompts for OB, parry, and dodge (three integers). Check balance against existing mobs. |
| `/10` | Hit points | Prompts for min and max hit (`min_hit`, `max_hit`). |
| `/11` | Damage | Base damage per swing (affects `points.damage`). |
| `/12` | Energy regen | Controls `points.ENE_regen`, which is used by `char_utils::get_energy_regen` and combat systems to determine both how quickly the mob regains energy and how fast it cycles attacks. Higher values shorten weapon recovery times (see ranger special shots, wild fighting handler, and `profs.cpp` multipliers), while lower values slow its swing rate. Defaults to roughly `70 + level * 2` in `new_mob`. |
| `/13` | Gold | Coins carried. Keep small unless the mob is meant to be a bank. |
| `/14` | Experience | Raw XP value. Use `/recalc` as a starting point and adjust sparingly. |
| `/16` | Current position | See `structs.h` `POSITION_*` defines (`POSITION_STANDING`, etc.). Determines the mob’s immediate pose when spawned. |
| `/17` | Default position | Fallback pose when not engaged (often `POSITION_STANDING` or `POSITION_RESTING`). |
| `/18` | Sex | 0 neutral, 1 male, 2 female. |
| `/19` | Race | `RACE_*` constant. Drives stat caps, languages, sunlight penalties. |
| `/20` | Race aggression | Bitvector of target races (see simple mode). |
| `/21` | Weight | Stored in hundredths of kg (`5000` = 50 kg). |
| `/22` | Height | Centimetres. |
| `/23` | Profession points | `GET_PROF_POINTS` per class (wizard/cleric/ranger/warrior). Enter four integers separated by spaces. |
| `/24` | Stamina (mana) | `constabilities.mana`. Use `/recalc` as reference when raising above default. |
| `/25` | Move points | `constabilities.move`. |
| `/26` | Body type | Same as `/10` in simple mode (0–4). |
| `/27` | Saving throw | Base save modifier (`GET_SAVE`). Negative is better. |
| `/28` | Stats | Enter `STR INT WILL DEX CON LEA` in that order. Range 0–40. |
| `/29` | Program number | ASIMA program ID (see `docs/shape_mudlle.md`). Clears `MOB_SPEC` when used. |
| `/30` | Language | The skill ID from `language_skills[]`/`LANG_*`. `LANG_BASIC` = common. |
| `/31` | Butcher item | Same as simple `/12`. |
| `/32` | Perception | `specials2.perception`; affects search/hide detection. |
| `/33` | Room death cry | Text shown in the room when the mob dies. |
| `/34` | Other-room death cry | Text broadcast to adjacent rooms. |
| `/35` | Corpse number | Vnum of the corpse object to spawn (`0` uses default). |
| `/36` | Resistances | Bitvector; see `MAN SHAPE MOB 36` (values align with spell schools such as fire, cold). |
| `/37` | Vulnerabilities | Bitvector, same mapping as resistances. |
| `/38` | Script number | Script vnum (see `docs/shape_script.md`). Works alongside ASIMA programs. |
| `/39` | RP flag | Bitmask of races allowed to roleplay with this mob (`specials2.rp_flag`). |
| `/40` | Mob spirit | Same as simple `/40`. |
| `/41` | Will teach | Toggles teaching capabilities (which skills/specs this trainer handles). Use bitmask defined in the training subsystem (`TRAIN_*`). |
| `/49` | Guided creation | Runs the field sequence recommended for new mobs. |
| `/50` | List | Prints every field (`/imp` plus `/50` is a good sanity check). |

## Mob flags (`/5`)

Bit numbers live in `src/structs.h` (`MOB_*`). Key ones:

| Bit # | Flag | Description |
|-------|------|-------------|
| 0 | `MOB_SPEC` | Hard-coded special procedure. Clear when using ASIMA. |
| 1 | `MOB_SENTINEL` | Never roams. |
| 2 | `MOB_SCAVENGER` | Picks up/wears gear. |
| 3 | `MOB_ISNPC` | Always set. |
| 4 | `MOB_NOBASH` | Immune to bash. |
| 5 | `MOB_AGGRESSIVE` | Attacks any PC entering the room. |
| 6 | `MOB_STAY_ZONE` | Won’t leave its zone. |
| 7 | `MOB_WIMPY` | Flees when low on HP. |
| 8 | `MOB_STAY_SECT` (`STAY_TYPE`) | Only wanders within its sector type (e.g., water). |
| 9 | `MOB_IS_MOUNT` | Can be ridden. |
| 10 | `MOB_CAN_SWIM` | Doesn’t need a boat. |
| 11 | `MOB_MEMORY` | Remembers attackers and re-aggros them. |
| 12 | `MOB_HELPER` | Assists friends during fights. |
| 16 | `MOB_BODYGUARD` | Rescues its master. |
| 17 | `MOB_WRAITH` | Ghost-like (no corpse). |
| 18 | `MOB_SWITCHING` | Switches targets mid-fight. |
| 19 | `MOB_NORECALC` | Prevents `/recalc` from overwriting handcrafted stats. |
| 20 | `MOB_ACTIVE` | Acts immediately on room entry (50% chance). |
| 21 | `MOB_PET` | Tamed pet (usually set automatically). |
| 22 | `MOB_HUNTER` | Hunts down remembered attackers. |
| 23 | `MOB_ORC_FRIEND` | Recruitable by common orcs. |

Use `p<number>` / `m<number>` to toggle bits or enter the summed integer.

## Affect flags (`/6`)

These map to the `AFF_*` bitvector (`src/structs.h`). Frequently used bits from
the old MAN SHAPE MOB 6 reference:

| Bit # | Flag | Effect |
|-------|------|--------|
| 0 | `AFF_SENSE_LIFE` | Detect hidden/invisible players. |
| 1 | `AFF_INFRARED` | See in the dark. |
| 2 | `AFF_SNEAK` | Suppresses “leaves” messages when moving. |
| 3 | `AFF_HIDE` | Mob starts hidden (requires sense life to spot). |
| 4 | `AFF_DETECT_MAGIC` | Reserved for players; avoid setting. |
| 5 | `AFF_CHARM` | Acts as charmed (follows whoever issued `follow`). |
| 6 | `AFF_CURSE` | Broken—do not use. |
| 7 | `AFF_SANCTUARY` | Permanent sanctuary—heavily reduces damage. Use sparingly. |
| 8 | `AFF_TWOHANDED` | Forces two-handed wielding. |
| 13 | `AFF_BREATHE` | Breathe underwater. |
| 18 | `AFF_FLYING` | Leaves no tracks, immune to ground effects. |

Setting an affect grants the mob the corresponding spell permanently—use with
caution, especially for sanctuary, flying, or invisibility.

## Alignment guidelines (MAN SHAPE MOB 7)

Alignment (`/7` in extended mode) is a flavour stat used by scripts and some
combat checks. Use these rough ranges when tuning mobs:

| Race type | Recommended range |
|-----------|-------------------|
| Elves | `+200` to `+350` |
| Dwarves / Hobbits | `+150` to `+300` |
| Humans | `+100` to `+200` |
| Neutral creatures | `-100` to `+100` |
| Wargs | `-100` to `-200` |
| Orcs / Uruks | `-200` to `-350` |

Avoid values beyond ±500 unless the mob is a unique lore figure.

## Race aggression table (MAN SHAPE MOB 11)

Race-aggression bits (field `/20`) let you target specific races even if the
mob lacks the global `AGGRESSIVE` flag. Add the bit value to the bitvector (or
`p<bit#>`):

| Target | Bit # | Value |
|--------|-------|-------|
| God | 0 | 1 |
| Human | 1 | 2 |
| Dwarf | 2 | 4 |
| Wood elf | 3 | 8 |
| Hobbit | 4 | 16 |
| High elf | 5 | 32 |
| Uruk | 11 | 2048 |
| Haradrim | 12 | 4096 |
| Orc | 13 | 8192 |
| Easterling | 14 | 16384 |
| Magus | 15 | 32768 |

`62` makes a mob hostile to the “whitie” races, and `63488` to the “darkie”
factions. These bits stack with the global `AGGRESSIVE` flag.

## Body types (MAN SHAPE MOB 10)

Field `/26` chooses the hit-location template:

| Value | Description |
|-------|-------------|
| 0 | Tiny/limbless creatures (snakes, slimes). |
| 1 | Humanoids (head, two arms, two legs). |
| 2 | Quadrupeds (four legs + head). Only these can be tamed as mounts. |
| 3 | Tentacled (octopi, aberrations). |
| 4 | Birds (wings + talons). |

Pick the number that best matches the mob’s anatomy; scripts and combat tables
use it for butcher parts and hit messages.

## Butcher items (MAN SHAPE MOB 12)

Field `/31` sets the vnum dropped from butchering the corpse. Humanoids with no
special drop should use vnum `17` (the “body parts” placeholder). Non-humanoids
can point to custom meat/pelt objects. Use `0` to disable butchering entirely.

## Languages (MAN SHAPE MOB 30)

Field `/30` controls the language the mob speaks/listens for. The code currently
supports the three entries in `language_skills[]`:

| Value | Constant | Notes |
|-------|----------|-------|
| 0 | `LANG_BASIC` | Westron/common. |
| 121 | `LANG_ANIMAL` | Used by beasts. |
| 122 | `LANG_HUMAN` | Human dialect (legacy). |
| 123 | `LANG_ORC` | Black-speech. |

Mobs default to `LANG_BASIC`. Only change this if you have scripts that check
for specific dialects.

## Perception defaults (MAN SHAPE MOB 32)

Leaving `/32` at `-1` lets the engine assign a racial default:

| Race type | Default perception |
|-----------|--------------------|
| Elves | 50 |
| Other humanoids | 30 |
| Undead (non-wraith) | 60 |
| Wraiths | 100 |

Set an explicit number if you need sharper senses (higher) or dulled senses
(lower). Values feed the hide/search routines.

## Death cries & corpses (MAN SHAPE MOB 33–35)

- `/33` – in-room death cry. Defaults to “Your blood freezes as you hear its
  death cry.” Enter a custom string to override.
- `/34` – other-room death cry echoed to adjacent rooms.
- `/35` – corpse vnum. `0` uses the generic corpse, which is a zero-capacity
  container that inherits the mob’s weight. Custom corpses must still be
  containers if you want loot to remain accessible.

## Resistances and vulnerabilities (MAN SHAPE MOB 36/37)

Fields `/36` (resistance) and `/37` (vulnerability) are bitvectors that map to
the specialization attack groups. Bits are shared with object resist/vuln flags:

| Bit # | Group | Value |
|-------|-------|-------|
| 0 | None / general | 1 |
| 1 | Fire | 2 |
| 2 | Cold | 4 |
| 3 | Regeneration | 8 |
| 4 | Protection | 16 |
| 5 | Animals | 32 |
| 6 | Stealth | 64 |
| 7 | Wild fighting | 128 |
| 8 | Teleport | 256 |
| 9 | Illusion | 512 |
| 10 | Lightning | 1024 |
| 11 | Mind | 2048 |

Most of these hooks are only consulted by a handful of spells/skills; when in
doubt, leave both vectors at 0.

## Special procedures (MAN SHAPE MOB2 29)

Field `/29` can reference built-in hard-coded behaviours instead of ASIMA
programs. Common IDs:

| ID | Behaviour |
|----|-----------|
| 1 | Snake (poisons on hit). |
| 2 | Friendly gatekeeper (opens doors during day / on knock). |
| 3 | Caster-mystic (buffs/heals). |
| 4 | Caster/mage (offensive spells). |
| 5 | Warrior (bashes frequently). |
| 6 | Paranoid gatekeeper (keeps doors shut). |
| 7 | Jig (performs the jig command). |
| 8–13 | Exit blockers (north/east/south/west/up/down). |
| 14 | Resetter (practice resetter). |
| 15 | Ranger ambusher. |
| 26 | Summoner (calls adds during fights). |
| 27 | Reciter (reads textscrolls). |
| 28 | Herald (announces arrivals). |

If you use a spec proc, remember to set the `SPECIAL` flag (`/5` bit 0). For
anything beyond these stock options, use ASIMA (`/29` with a program number) or
scripts (`/38`).

## Best practices

- **Use `/recalc` cautiously**: the command recalculates combat stats from the
  level. It’s useful when starting but will wipe custom OB/hp/damage unless the
  mob carries `MOB_NORECALC`.
- **Match zone expectations**: compare your mob’s `/50` output with similar
  creatures already in the zone. Keep OB/HP/damage roughly aligned.
- **Body type matters**: choose the right `/26` for hit locations (humanoid vs
  quadruped). Only body type 2 (quadruped) mobs can be tamed as mounts.
- **Race + sunlight**: orcs, uruks, and olog-hai suffer daylight penalties. If
  your mob roams outside, consider equipping them with cloaks or scheduling
  behaviour via scripts.
- **Programs vs scripts**: `/29` ASIMA programs run before `/38` script
  triggers. Avoid using both unless you know which behaviour fires first.
- **Training mobs**: when using `/41`, ensure the trainer actually offers
  corresponding lessons via the spec code—setting random bits does nothing if
  the spec isn’t implemented.

## Example: Updating a city guard

Goal: create a level-40 human guard who challenges orcs at the gate, carries a
halberd, and speaks Westron.

```text
shape mob 4005
/1 gate guard guard human guard
/2 a vigilant gate guard
/3 A vigilant gate guard watches the traffic.
/4    The guard scans every traveler before waving them through.
/5 p1 p6 p12                # SENTINEL + STAY_ZONE + HELPER
/6 p1                       # AFF_SENSE_LIFE
/7 200                      # alignment
/8 40                       # level
/9 95 60 20                 # OB, parry, dodge
/10 1200 1500               # min/max hit
/11 28
/12 30
/13 15                      # gold
/14 250000                  # experience
/16 8                       # current position (standing)
/17 8                       # default position
/18 1                       # male
/19 1                       # RACE_HUMAN
/20 p13                     # aggressive to orcs
/21 8500
/22 185
/23 10 10 10 10             # prof pools
/24 200
/25 300
/26 1                       # humanoid
/27 -10
/28 35 20 25 30 32 25
/29 0                       # no ASIMA program
/30 0                       # LANG_BASIC
/31 0                       # no butcher drop
/32 15
/33 The guard crumples with a surprised gasp.
/34 You hear a guard fall nearby!
/35 0
/36 0
/37 0
/38 4206                    # gatekeeper script
/40 Master
/41 0
/50
/save
/implement
```

After shaping, add the mob to the zone file with an `M` command and kit it with
a halberd via `K`/`E`.

## Example: Mountable warg for an orc patrol

```text
shape mob 4802
/1 warg mount patrol mount
/2 a hulking warg mount
/3 A hulking warg patiently waits for an orc rider.
/4    Slaver dripping from its jaws, the beast paws at the ground.
/5 p1 p6 p9 p10 p22          # ISNPC + STAY_ZONE + IS_MOUNT + CAN_SWIM + HUNTER
/8 32                        # level
/9 85 20 10
/10 900 1100
/11 22
/12 40
/18 0                        # neutral sex
/19 13                       # RACE_ORC (shares penalties)
/21 18000
/22 150
/24 150
/25 250
/26 2                        # quadruped
/32 5                        # low perception
/29 0
/38 0                        # optional script for bucking non-orcs
/50
/save
/implement
```

Use the `/49` guided sequence whenever you start a new mob; it steps through
aliases → descriptions → combat stats → loot → extras, mirroring the logical
order outlined above.

Refer back to this document whenever you need the exact field semantics—the goal
is to keep shaping knowledge in one place so we can finally retire `shap_tbl`.
