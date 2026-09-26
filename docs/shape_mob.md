# Shape Mob Command

`shape mob` is the in-game tool for creating or modifying mobile prototypes.
Mob definitions live in `world/mob/<zone>.mob`. This guide describes the
shaping interface, every editable field, and the conventions expected by
Return of the Shadow. The in-game `help shape mob <n>` entries (`shap_tbl`)
cover the same fields in short form.

## Prerequisites

- **Permissions**: you must have builder rights for the zone (`get_permission`).
- **Files and vnums**: zone files are created outside the game. You shape a mob
  by its vnum: `shape mob 1350` loads #1350, or, if the file `13.mob` has no
  #1350 yet, starts a blank mob with that number ("could not find mob #1350,
  created it"). `/save` then writes it into its place in the file. A mistyped
  vnum therefore starts a new mob instead of reporting an error.
- **Reference mob**: for quick sanity checks, keep another mob with similar
  behaviour handy and compare stats with `/50`.

## Workflow overview

| Action | Command |
|--------|---------|
| Load/create | `shape mob <vnum>` (see Prerequisites). |
| Mode toggle | The editor opens in simple mode. `/simple` toggles between simple (fields 1–13) and extended editing. |
| Show menu | `/0` (or any unused field number) prints the field list for the current mode. A word that is not a command (e.g. `/help`) prints the command list. |
| Edit field | `/<number>` asks for the field on the **next** line. Anything typed after `/N` on the same line is ignored. |
| List current values | `/50` lists the whole mob; `/49` runs the guided creation sequence. |
| Save & implement | `/save` writes to disk (after a backup); `/implement` pushes the mob into the running game; `/done` does save → implement → free. |
| Exit without saving | `/free`. |

`/done` stops if the save fails (no permission, missing backup folder, file
error): it prints `Not saved - still shaping. Fix the problem and /done again,
or /free to discard.` and keeps your edits.

### Disabled commands

These commands did harm and are turned off. Each one answers with exactly what
was typed, e.g. `"/delete" has been disabled due to a bug.`:

- `/delete` (any arguments)
- `/new <zone>` and `shape mobile new <zone>` (picked last vnum + 1, which could
  run past the zone's range)
- `/add <anything>` (could write the mob into any file). Plain `/add` still
  works and simply saves.
- `shape recalc_mobile` (rewrote every mob file from level and shut down)

`shape master_mobile <idnum>` / `shape master_object <idnum>` (Greater God and
up) set the one player who may shape any mob / object. With no number they now
change nothing and show the current master and the usage line.

`/recalculate` needs at least `/recalc`; `/r` alone shows the command list.

### Answering prompts

- Single-line text: the whole line is stored. A blank line keeps the old value.
  `%q` empties the field (refused for `/1` and `/2`, see below). `#` becomes
  `+` and `~` becomes `-`.
- Multi-line text (`/4`): the shared editor. `%e` saves, `%q` aborts and keeps
  the old text, `%f` formats, `%h` shows help.
- Numbers: a plain number sets the value; `+5` adds; `-5` **subtracts** (except
  the fields that accept negatives, below); `p7` sets bit 7 and `m7` clears it
  (one toggle per answer). A blank line keeps the value. A word keeps the value
  silently.
- Negative values: `/7` alignment, `/27` saving throw and `/32` perception take
  `-N` as the value `-N` (e.g. `-300`), not as "subtract N".
- Every prompt shows the current value; the fields with several numbers
  (`/9`, `/10`, `/28`) show a `Current:` line.

## Simple-mode fields

| `/n` | Field | Notes |
|------|-------|-------|
| `/1` | Aliases | Keywords separated by spaces (`gate guard human`), not commas. Can't be emptied with `%q` ("Aliases can't be empty."). |
| `/2` | Reference description | The name used in messages (`a surly orc guard`). Can't be emptied with `%q`. Boot lowercases a leading A/An/The; `/implement` does not. |
| `/3` | Room line | Shown in the room only while the mob is in its **default position** (`/17`); otherwise the game shows the reference description plus the position. Capitalise and end with a period. |
| `/4` | Detailed description | The text shown by `look <mob>`. |
| `/5` | Mob flags | Bitvector; the prompt lists all bits (see "Mob flags"). Bit 3 ISNPC is always forced on. |
| `/6` | Affects | Bitvector; the prompt lists all bits (see "Affect flags"). |
| `/7` | Level | Does **not** recalculate stats; use `/recalculate` for that. |
| `/8` | Sex | Letter: `n`, `m` or `f`. The prompt shows the current letter; blank keeps it; digits are rejected ("Unrecognized sex."). |
| `/9` | Race | Must be 0–20 ("Race must be 0-20. dropped."). The prompt lists the races. |
| `/10` | Body type | Must be 0–15 (see "Body types"). |
| `/11` | Race aggression | Race bitvector (`p<race#>`). |
| `/12` | Butcher item | Object vnum given by butchering; `0` = none. |
| `/13` | Spirit | Spirit points (see extended `/40`). |
| `/49` | Guided creation sequence | See below. |
| `/50` | List | Prints the simple-mode fields. |

## Extended-mode field reference

| `/n` | Field | Description / Notes |
|------|-------|---------------------|
| `/1` – `/6` | Same as simple mode. |
| `/7` | Alignment | `specials2.alignment`, `-1000..1000`; good is `>= 100`, evil `<= -100`. `-N` sets a negative value. |
| `/8` | Level | Combat level. Does not recalculate stats. |
| `/9` | OB, parry, dodge | Three numbers (`OB PARRY DODGE`). Shows `Current:`; blank keeps. Negatives work here. Fewer than three numbers: "three numbers required. dropped". |
| `/10` | Hit points | `MIN_HIT MAX_HIT`; each copy rolls between the two when it loads. Shows `Current:`; blank keeps. |
| `/11` | Damage | Added ×10 to each melee hit (`points.damage`). |
| `/12` | Energy regen | `points.ENE_regen`: energy gained per pulse; higher = faster attacks. `/recalculate` sets `70 + 2 × level`; a new mob starts at 0. |
| `/13` | Gold | Coins moved to the corpse, in **copper** (1000 = 1 gold). |
| `/14` | Experience | Base kill experience (`points.exp`). Use `/recalculate` as a starting point. |
| `/16` | Position | The pose it loads in. Only `4` sleeping, `5` resting, `6` sitting, `8` standing are accepted ("Position must be 4, 5, 6 or 8. dropped."). |
| `/17` | Default position | The pose it returns to, and the pose in which `/3` is shown. Same four values. Below standing cuts exp by 1/20. |
| `/18` | Sex | Letter `n`, `m`, `f` (as simple `/8`). |
| `/19` | Race | 0–20 (as simple `/9`). Drives side-based aggression, racial perception and race-bit checks. |
| `/20` | Race aggression | Race bitvector (see "Race aggression"). |
| `/21` | Weight | In 1/100 lb (`score` shows weight/100 as lb). Corpse weight, mount load, bash, block. |
| `/22` | Height | Centimetres. Display only for mobs. |
| `/23` | Prof | One number (`player.prof`, 0–255). For NPCs it only shows in `stat` (Normal/Undead). |
| `/24` | Stamina (mana) | `abilities.mana`, the pool for mage-type spells. |
| `/25` | Move points | `abilities.move`. |
| `/26` | Body type | 0–15 (as simple `/10`). |
| `/27` | Saving throw | Positive = less spell damage (damage × 20/(20+s)); negative = more. `-N` sets a negative value. |
| `/28` | Stats | `STR INT WILL DEX CON LEA`, six numbers. Shows `Current:`; blank keeps. Stored as signed bytes (above 127 wraps); `0` or less becomes 17 when the mob loads. |
| `/29` | Program number | If flag bit 0 (SPEC) is set: a spec-proc number (see "Special procedures"). Otherwise a mudlle program vnum; `0` = none. For hard-coded guildmasters it is the guild list number instead. |
| `/30` | Language | Index: `0` common, `1` animal, `2` human, `3` orc. Anything above 3 is stored as 3. |
| `/31` | Butcher item | Object vnum given by butchering; `0` = nothing of value. |
| `/32` | Perception | `0`–`100`, or `-1` for the racial default (see "Perception defaults"). `-1` can be typed directly. |
| `/33` | Room death cry | `act()` text sent to the room on death (`$n` = the mob). `%q` = no message. |
| `/34` | Adjacent-room death cry | Sent to rooms reachable through open exits. `%q` = no message. Spec procs herald (28) and wolf summoner (26) reuse this text. |
| `/35` | Corpse | Object vnum used as the corpse; `0` (or a vnum that doesn't exist) = the generic corpse. |
| `/36` | Resistances | Bitvector of groups; the prompt lists them (see "Resistances"). |
| `/37` | Vulnerabilities | Same groups as `/36`. |
| `/38` | Script | Script vnum (`world/scr`, see `docs/shape_script.md`); `0` = none. Separate from `/29`. |
| `/39` | Roleplay flag | Race bitmask used by guildmasters (`RP_RACE_CHECK`): when non-zero, only races whose bit is set may practise. `0` = no restriction. |
| `/40` | Spirit | Spirit points spent when the mob casts cleric/mystic spells and curse. |
| `/41` | Will teach | Race bitmask for guildmasters (`WILL_TEACH`): the races this guildmaster teaches. Only matters for guildmaster mobs. |
| `/49` | Guided creation | See below. |
| `/50` | List | Prints every field. |

## The `/49` sequence

`/49` walks: `/1` aliases → `/2` → `/3` → `/4` → `/8` level → `/50` list →
recalculate. **The recalculation is applied before it asks** "Stats were reset
from level. Edit them now?" — answering N does not undo it, so running `/49` on
an existing mob wipes its hand-set stats. On Y it continues in extended mode
with `/23`–`/28`, `/9`, `/10`, `/29`. A blank answer to `/9`, `/10` or `/28`
keeps the values and moves on. It never visits flags, affects, alignment,
damage/regen/gold/xp, positions, sex, race, aggression, weight/height or
`/30`–`/41`.

## `/recalculate`

Resets from level: stamina, moves, hit points, OB/parry/dodge, damage, energy
regen, experience, saving throw, all six stats (`7 + level/2`), both positions
(standing) and language. Language by race: human/dwarf/elves/hobbit → 2 human,
beorning → 1 animal, uruk/harad/orc/magus → 3 orc, easterling → 0 common,
anything else → 1 animal. The per-mob `/recalculate` ignores `MOB_NORECALC`.

## Mob flags (`/5`)

Bit numbers are the `MOB_*` values in `src/structs.h`; the names are what
`stat` shows. The `/5` prompt lists all of them.

| Bit # | Flag | Description |
|-------|------|-------------|
| 0 | SPEC | Run the spec proc in `/29`. |
| 1 | SENTINEL | Never roams. |
| 2 | SCAVENGER | Picks up items lying around. |
| 3 | ISNPC | Always set (forced on by the editor). |
| 4 | NOBASH | Immune to bash. |
| 5 | AGGR | Attacks on sight. |
| 6 | STAY-ZONE | Won't leave its zone. |
| 7 | WIMPY | Flees when hurt; if aggressive, only attacks sleepers. |
| 8 | STAY-TYPE | Only wanders rooms of the same sector type. |
| 9 | MOUNT | Can be ridden. |
| 10 | CAN_SWIM | Doesn't need a boat. |
| 11 | MEMORY | Remembers attackers. |
| 12 | HELPER | Attacks characters fighting a PC in the room. |
| 13 | AGGR_EVIL | With AGGR (bit 5): attack only evil players. No effect without AGGR. |
| 14 | AGGR_NEUT | With AGGR (bit 5): attack only neutral players. No effect without AGGR. |
| 15 | AGGR_GOOD | With AGGR (bit 5): attack only good players. No effect without AGGR. |
| 16 | BODYGUARD | Rescues its master. |
| 17 | WRAITH (`MOB_SHADOW`) | A spirit; perception forced to 100. |
| 18 | SWITCH | Won't switch opponents. |
| 19 | NORECALC | The global recalc skips it (per-mob `/recalculate` does not). |
| 20 | FAST (`stat`: ACTIVE) | Acts when someone enters. |
| 21 | IS_PET | Pet of a player (set automatically). |
| 22 | HUNTER | Memory + hunts its enemies. |
| 23 | ORC_FRIEND | Recruitable by common orcs. |
| 24 | RACE_GUARD | Blocks players of a different race. |
| 25 | ASSISTANT | Assists its master in combat. |
| 26 | GUARDIAN | Guardian mob. |

Use `p<number>` / `m<number>` to toggle bits, or enter the summed integer.

## Affect flags (`/6`)

These map to the `AFF_*` bitvector (`src/structs.h`); names as `stat` shows
them. The `/6` prompt lists bits 0–31. Frequently used:

| Bit # | Flag | Effect |
|-------|------|--------|
| 0 | SENSE (`AFF_DETECT_HIDDEN`) | Detects hidden characters. |
| 1 | INFRA | See in the dark. |
| 2 | SNEAK | No "leaves" messages when moving. |
| 3 | HIDE | Starts hidden. |
| 7 | SANCT | Permanent sanctuary — heavily reduces damage. Use sparingly. |
| 8 | TWO-HANDED | Two-handed wielding. |
| 9 | INVIS | Invisible. |
| 13 | BREATHE | Breathes underwater. |
| 18 | FLYING | Flying. |

Setting an affect grants it permanently — use with caution.

## Alignment guidelines

Alignment (`/7`) is used by the AGGR_EVIL/NEUT/GOOD flags and by exp scaling.
Rough ranges:

| Race type | Recommended range |
|-----------|-------------------|
| Elves | `+200` to `+350` |
| Dwarves / Hobbits | `+150` to `+300` |
| Humans | `+100` to `+200` |
| Neutral creatures | `-100` to `+100` |
| Wargs | `-100` to `-200` |
| Orcs / Uruks | `-200` to `-350` |

Type `-300` to set -300 directly. Avoid values beyond ±500 unless the mob is a
unique lore figure.

## Race aggression (`/20`, simple `/11`)

A bitvector over race numbers: the mob attacks anyone, PC or NPC, whose race bit
is set (`p<race#>`). This check runs before the AGGR flags, so it also targets
NPCs.

| Target | Bit # | Value |
|--------|-------|-------|
| God | 0 | 1 |
| Human | 1 | 2 |
| Dwarf | 2 | 4 |
| Wood elf | 3 | 8 |
| Hobbit | 4 | 16 |
| High elf | 5 | 32 |
| Beorning | 6 | 64 |
| Uruk | 11 | 2048 |
| Harad | 12 | 4096 |
| Orc | 13 | 8192 |
| Easterling | 14 | 16384 |
| Uruk-Lhuth (Magus) | 15 | 32768 |
| Undead | 16 | 65536 |
| Olog-hai | 17 | 131072 |
| Haradrim | 18 | 262144 |
| Troll | 20 | 1048576 |

`126` (bits 1–6) targets every "whitie" race including Beornings; `456704`
(bits 11–15, 17, 18) targets the "darkie" races including Olog-hai and
Haradrim. The same bits are used by `/41` and `/39`.

## Races (`/19`, simple `/9`)

`0` god/animal, `1` human, `2` dwarf, `3` wood elf, `4` hobbit, `5` high elf,
`6` beorning, `11` uruk-hai, `12` harad, `13` orc, `14` easterling,
`15` uruk-lhuth, `16` undead, `17` olog-hai, `18` haradrim, `20` troll.
7–10 and 19 are unused. Values outside 0–20 are refused by the editor; a mob
file with a race outside 0–20 logs `MOB ERROR: mobile #N: race R out of range
0-20` at boot (the file is left as it is).

## Body types (`/26`, simple `/10`)

| Value | Description |
|-------|-------------|
| 0 | No hit locations. |
| 1 | Humanoid (head, body, arms, hands, legs, feet). Required by some mental spells. |
| 2 | Quadruped (the animal type): no skills, bonus move regeneration. |
| 3 | Head, body and eight legs. |
| 4 | Bird (wings and claws). |
| 5–14 | Unused; behave like 0. |
| 15 | Bear form. |

Values above 15 are refused ("Body type must be 0-15. dropped.").

## Butcher items (`/31`, simple `/12`)

The object vnum copied to the corpse for butchering. `0` gives "nothing of
value". The corpse stores it as a short, so vnums above 32767 do not work.

## Languages (`/30`)

The editor stores an index into `language_skills[]`:

| Value | Language |
|-------|----------|
| 0 | Common (everyone understands) |
| 1 | Animal |
| 2 | Human |
| 3 | Orc |

Boot reads anything outside 1–3 as 0 (common). Many older mobs have 121–123
saved (an old `/recalculate` bug); they play as common, and the editor now
loads them as 0 so a re-save keeps them common.

## Perception defaults (`/32`)

`-1` uses the racial default from `get_race_perception`:

| Race | Default |
|------|---------|
| High elf | 100 |
| Wood elf | 50 |
| Undead | 60 |
| Orc | 10 |
| Dwarf, god/animal (0) | 0 |
| Other races | 30 |

`MOB_SHADOW` (flag 17) forces 100. Any other value is clamped to 0–100.

## Death cries & corpses (`/33`–`/35`)

- `/33` – in-room death cry, an `act()` string (`$n` = the mob). With no text
  set, the game sends "Your blood freezes as you hear <mob>'s death cry."
  `%q` stores an empty cry, which sends **no** message.
- `/34` – sent to each room reachable through an open exit. Default "Your blood
  freezes as you hear someone's death cry." `%q` = no message.
- `/35` – corpse vnum. `0` uses the generic corpse.

## Resistances and vulnerabilities (`/36`/`/37`)

Bitvectors over specialization groups (`resistance_name[]`); the prompts list
them:

| Bit # | Group | Value |
|-------|-------|-------|
| 0 | Ungrouped | 1 |
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

The fields are shorts: bits 16 and up are lost. When in doubt, leave both at 0.

## Special procedures (`/29` with flag bit 0)

With SPEC set, `/29` picks a built-in behaviour:

| ID | Behaviour |
|----|-----------|
| 1 | Snake (poisons on hit). |
| 2 | Gatekeeper. |
| 3 | Cleric caster. |
| 4 | Mage caster. |
| 5 | Warrior. |
| 6 | Gatekeeper 2. |
| 7 | Jig. |
| 8–13 | Exit blockers (north/east/south/west/up/down). |
| 14 | Resetter. |
| 15 | Ranger (old). |
| 16 | Trap reactor. |
| 17 | Gatekeeper (no knock). |
| 18 | Ar-Tarthalon. |
| 19 | Ghoul. |
| 20–23 | Vampire huntress / Thuringwethil / vampire doorkeeper / vampire killer. |
| 24 | Healing plant. |
| 25 | Vortex elevator. |
| 26 | Wolf summoner. |
| 27 | Reciter. |
| 28 | Herald. |
| 29 | Postmaster. |
| 30 | Dragon. |
| 31 | Mage caster (spec). |
| 32 | Ranger (new). |

`/implement` puts a changed `/29` into play for mobs loaded from then on, every
time you implement (it used to take effect only on the first implement after a
reboot). Copies already in the game keep what they have. Spec procs attached by
vnum in the code (guildmasters, receptionists, postmasters, shopkeepers) are
not changed by shaping; for a hard-coded guildmaster `/29` is the guild list
number.

## Best practices

- **Use `/recalculate` cautiously**: it resets combat stats, stats, positions
  and language from the level, and ignores `MOB_NORECALC`.
- **Match zone expectations**: compare your mob's `/50` with similar creatures
  already in the zone.
- **Body type matters**: choose the right `/26` for hit locations.
- **Programs vs scripts**: `/29` and `/38` are separate systems; check which one
  a mob uses before changing behaviour (`docs/data-formats/mudlle-and-scripts.md`).
- **Training mobs**: `/41` and `/39` only matter for hard-coded guildmasters.

## Example: Updating a city guard

Goal: a level-40 human guard who attacks orcs, standing at the gate. Each field
number is typed on its own line; the value goes on the **next** line.

```text
shape mob 4005
/simple                     (switch to extended mode)
/1
gate guard human
/2
a vigilant gate guard
/3
A vigilant gate guard watches the traffic.
/5
p1                          (SENTINEL; one toggle per answer)
/5
p6                          (STAY-ZONE)
/5
p12                         (HELPER)
/7
200
/8
40
/9
95 60 20                    (OB parry dodge)
/10
1200 1500                   (min/max hit)
/11
28
/12
150
/16
8                           (standing)
/17
8
/18
m
/19
1                           (human)
/20
p13                         (aggressive to orcs)
/26
1                           (humanoid)
/27
-10                         (sets -10)
/28
35 20 25 30 32 25
/38
4206                        (gatekeeper script)
/50
/save
/implement
```

After shaping, add the mob to the zone file with an `M` command and kit it
with `K`/`E`.

## Example: Mountable warg for an orc patrol

```text
shape mob 4802
/simple
/5
p9                          (MOUNT)
/5
p22                         (HUNTER)
/8
32
/19
13                          (orc)
/26
2                           (quadruped)
/32
5
/50
/save
/implement
```
