# Experience & progression — gain, loss, and the high-level incentive

**Source files:** `src/limits.cpp` (`xp_to_level:90`, `advance_mini_level:92`, `gain_exp:410`,
`gain_exp_regardless:434`), `src/fight.cpp` (`exp_with_modifiers:1334`, `group_gain:1407`,
`die:1285-1325`, `damage_credited:2097`), `src/act_offe.cpp` (`do_flee:388`), `src/clerics.cpp`
(`do_mental:226`), `src/script.cpp` (`SCRIPT_GAIN_EXP:1092`), `src/zone.cpp` (`M` command `:729`).
**Status:** ✅ every gain and loss path traced and pinned; corpus evaluated at seven tiers.
**As of:** 2026-09-19, branch `fix/spell-room-affect-uaf-port`. Working trace with per-row citations:
`tools/xp_research/FORMULAS.md`; Python mirror and evaluator: `tools/xp_research/`; C++ pins:
`src/tests/xp_formula_tests.cpp` (`XpFormula.*`).

## Purpose

Experience drives the only level curve in the game, and level feeds nearly every other formula. This
document records exactly how a character gains and loses experience today, what the live world offers
a character at levels 30 to 90, and what that combination rewards.

It is the baseline for the progression redesign (removing hitting XP and flee/death loss, adding a new
reward vector). The redesign itself is not described here.

## Data structures

- `points.exp` and `player.level` on `char_data`; `GET_MINI_LEVEL` is a finer counter advanced by
  `gain_exp_regardless` and is what actually raises `level` (`src/limits.cpp:103-107`).
- `GET_LEVELB(pc) = min(level, 20 + level / 3)` (`src/utils.h:315`): the "effective level" used for kill
  shares and for the mob's `attacked_level` malus. It is 30 at level 30, 40 at 60, 49 at 89.
- `specials.attacked_level` on a mob: the highest `GET_LEVELB` that has hit it; decays by 2 per tick
  while it is not fighting.
- `specials.prompt_number` doubles as `GET_DIFFICULTY` on mobs, set per spawn by the zone `M` command's
  fifth argument (100 or 0 = unchanged).
- `gain_exp` clamps: +7000 per positive event, -10000 per negative event; positive gains stop entirely
  at level 90, negative ones at 91. `gain_exp_regardless` bypasses both clamps; death uses it.
- Delevel rule: while `xp_to_level(level) - 20000 > exp`, lose a level and its practices.

## Format / Algorithm

### Every path that changes a player's experience

| id | trigger | formula | clamp | source |
| --- | --- | --- | --- | --- |
| `xp_to_level` | level threshold | `1500 * L * L` | — | `limits.cpp:90` |
| `hit_xp_melee` | every damage event through `damage()` (melee, skills, spells), attacker ≠ victim, PC attacker | `(1 + L_v) * min(20 + 2 * L_a, dam) / (1 + L_a)` | +7000 per hit | `fight.cpp:2098` |
| `hit_xp_mental` | successful mental attack | same with `dam` → `damg * 5` | +7000 | `clerics.cpp:226` |
| `kill_share` | `group_gain` after a death | see below | via `exp_with_modifiers`, then +7000 | `fight.cpp:1490-1541` |
| `flee_loss` | successful flee while fighting, PC only | `-(L_fleeing + L_opponent)` | -10000 (never reached) | `act_offe.cpp:388-390` |
| `death_loss` | `die()` | `base = -(exp - 3000) / (L + 2)`; always `base / 10`; plus `base` when the killer is a real mob (or a poison death while engaged with one) | none; delevel applies | `fight.cpp:1293-1325`, `:1037` |
| `script_grant` | Mudlle `GAIN_EXP` (opcode 64) | script value; above level 30: `v * 25 / L`, then `6 * v / (L - 25)` | +7000 / -10000 | `script.cpp:1092-1106` |
| `wiz_advance` | immortal `advance` | sets exp to the level threshold | — | `act_wiz.cpp:1560-1578` |

Sites that read `points.exp` without changing it, and the pet shop's use of a pet's own exp as a price,
are listed in `tools/xp_research/FORMULAS.md`.

**Kill share** (`group_gain`): the victim's exp is divided by 10, multiplied by `(n + 1) / n` for an NPC
victim with `n` player killers, divided by `level_total` (the sum of the killers' `GET_LEVELB` plus the
mob's `attacked_level`), and each killer's base is `share * levelb` plus a group bonus that is zero for a
solo killer. Killers are every PC in the death room who is fighting the victim, is its target, is the
credited killer, is grouped with one of those, or masters a fighting pet or orc-friend. Immortals skip.

**`exp_with_modifiers`**, applied to each killer's base in this order:

1. Orc killer, orc-friend victim: 0.
2. `base /= max(L_killer + 1, L_victim - 2)`.
3. Player victim: return here. Nothing below applies to player kills.
4. Victim more than 6 levels below the killer: `base = 6 * base / (L_killer - L_victim)`.
5. Age curve (victim above level 5): 60 to 140 percent depending on how long the mob has lived
   relative to `average_mob_life` (40 mud hours).
6. Flag bonuses on `base`: aggressive +1/5, fast +1/10, switching +1/10, memory +1/20, default position below standing
   (resting, sitting or sleeping) -1/20, live special procedure +1/10; good killer and good victim: ×2/3.
7. Difficulty: `× difficulty / 100` when non-zero.
8. East bonus: a good-race killer in a zone with map `x > 8` gets `+ min(x - 8, 5) * 3` percent (up to 15).
   223 of 337 zones are east of the river; 150 of them carry the full 15 percent.
9. Marked `TEMPORARY` in the source: `+ 2 * exp / max(1, L_killer - 1)` (7 percent at 30, 2 percent at 89).

**Are there other loss mechanisms?** No. Only flee, death, and a script or immortal command passing a
negative value remove experience. Nothing else writes `points.exp` downward.

## RotS-specific notes

- **Hitting XP is per damage event and has no level-gap penalty.** Its only level term is
  `(1 + L_victim) / (1 + L_attacker)`, and damage above `20 + 2 * L_attacker` earns nothing extra.
  "Hitting very hard" is therefore not the lever; hitting *often* is.
- **Kill XP is crushed by the level gap twice**: step 2 divides by the killer's level, and step 4
  multiplies by `6 / gap` for anything 7 or more levels below.
- **Level 90 earns nothing.** `gain_exp` refuses positive gains at level 90 or above. A level-90 character
  can only lose experience (death, flee) and be deleveled to 89, then earn again.
- **Death to a mob costs about half a level at every tier; death to a player costs 5 percent.** Flee costs
  under 200 points, which is noise.
- **The east-of-the-river faction bonus** exists only for good races, tops out at 15 percent, and applies
  after the level-gap divisors, so at high tiers it usually truncates to a point or two.
- **Difficulty is live tuning**: 459 of 11159 mob spawns carry a value other than 100, from 1 percent (71 spawns)
  and 10 percent (95) up to 150 percent (87), 200 percent (23) and 300 percent (15).
- **Scripts do grant XP**: 54 `GAIN_EXP` operations across seven zone script files.
- **A bonus the source still marks `TEMPORARY`** adds `2 * exp / (level - 1)`: it triples a level-2
  character's kill XP, adds 7 percent at 30, and is under 3 percent from 60 up. Stock CircleMUD has no
  such term.
- **Good killing good pays two thirds**, applied before difficulty and the east bonus; orcs killing
  orc-friends get nothing at all. Both are RotS alignment rules with no stock counterpart.

## Worked example: the corpus at seven tiers

Assumptions for every table: solo good-race, good-aligned killer; mob at average age; damage per hit at
the cap `20 + 2 * tier`; difficulty and zone from each mob's actual spawn command; pets, orc-friends and
never-spawned prototypes excluded (10909 spawn rows evaluated). Full tables with 90th percentiles, the
east-bonus columns and the outlier lists come from `python3 tools/xp_research/evaluate_corpus.py`.

### What the world offers

Distinct spawned mobs by level band, all alignments (a join of the Task 2 mob and spawn CSVs; the zone
counts above come from the same parser's summary):

| mob level | 0-9 | 10-19 | 20-29 | 30-39 | 40-49 | 50-59 | 60+ |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| distinct mobs | 642 | 794 | 614 | 110 | 33 | 17 | 8 |

Of the eight at 60 and above, five are shopkeepers, innkeepers and doorkeepers whose level is armour,
not content. Combat content effectively ends in the 50s. For a level 75 or 89 character, every mob in the
world is at least 15 to 30 levels below them.

### Table 1: median kill XP by mob level band (no east bonus)

| tier | 10-19 | 20-29 | 30-39 | 40-49 | 50-59 | good-on-good, 30-39 band |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 30 | 204 | 1413.5 | 2796 | 3537 | 2739 | 1514 |
| 40 | 89 | 428 | 1264 | 3224 | 2694 | 674 |
| 50 | 51 | 215 | 501 | 1539 | 2510 | 266 |
| 60 | 33 | 128 | 276 | 636 | 1251 | 145 |
| 75 | 18 | 72 | 144 | 287 | 397 | 77 |
| 89 | 12 | 47 | 94 | 172 | 213 | 50 |
| 90 | 0 | 0 | 0 | 0 | 0 | 0 |

Kill XP falls by roughly half for every 15 levels the killer gains over the mob, and a good character
killing a good mob gets about half again. The 90th percentile reaches the 7000 clamp only for level
30-59 mobs killed at tiers 30 to 50. Even-sized bands report the mean of the two middle values.

### Table 2: XP per hit at the damage cap, and hits in one median level-matched kill

| tier | vs level 15 | vs level 20 | vs own level | hits equal to one level-matched kill |
| ---: | ---: | ---: | ---: | ---: |
| 30 | 41 | 54 | 80 | 35 |
| 60 | 36 | 48 | 140 | 26 |
| 75 | 35 | 46 | 170 | 20 |
| 89 | 35 | 46 | 198 | 5 |
| 90 | 0 | 0 | 0 | — |

Per hit, a low-level victim pays a third to a fifth of a level-matched one; the last column shows how
few level-matched hits equal a whole kill once the level-gap divisors have shrunk the kill.

### Table 3: XP per mob, split into hitting and killing (median mob of each band)

| tier | band | hits to kill | hitting XP | kill XP | hitting share | XP per swing |
| ---: | --- | ---: | ---: | ---: | ---: | ---: |
| 30 | 10-19 | 4 | 164 | 204 | 45% | 92 |
| 30 | 30-39 | 13 | 1040 | 2796 | 27% | 295 |
| 60 | 10-19 | 2 | 72 | 33 | 69% | 53 |
| 60 | 50-59 | 22 | 2574 | 1251 | 67% | 174 |
| 75 | 10-19 | 2 | 70 | 18 | 80% | 44 |
| 75 | 50-59 | 18 | 2052 | 397 | 84% | 136 |
| 89 | 10-19 | 2 | 70 | 12 | 85% | 41 |
| 89 | 50-59 | 16 | 1792 | 213 | 89% | 125 |

From tier 60 upward, hitting is the main experience vector even against the hardest content that exists.

### Table 4: cost of failure at the level threshold

| tier | flee vs level 15 | death to a player (tenth) | as % of next level | death to a mob (full) | as % of next level | full loss in level-50 mob kills |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 30 | 45 | 4209 | 4.6 | 46302 | 50.6 | 6 |
| 60 | 75 | 8704 | 4.8 | 95752 | 52.8 | 25 |
| 75 | 90 | 10953 | 4.8 | 120491 | 53.2 | 49 |
| 89 | 104 | 13053 | 4.9 | 143585 | 53.5 | 72 |
| 90 | 105 | 13203 | 4.9 | 145235 | 53.5 | — |

One death to a mob undoes half a level at every tier; the last column converts that into the hardest
kills the world offers (hitting XP included), which is what a player weighs against the risk.

### Table 5: clamp pressure and mobs per level

| tier | next level costs | minimum events (7000 clamp) | level-15 mobs per level | level-50 mobs per level |
| ---: | ---: | ---: | ---: | ---: |
| 30 | 91500 | 14 | 249 | — |
| 60 | 181500 | 26 | 1729 | 47 |
| 75 | 226500 | 33 | 2574 | 92 |
| 89 | 268500 | 39 | 3274 | 134 |

The clamp's theoretical minimum is never the binding constraint; median kills are a fraction of it, so
the real count of kills per level is 4 to 80 times the clamp floor.

### Table 6: solo player-kill XP (victim at their level threshold)

| killer \ victim | 30 | 40 | 50 | 60 | 75 | 90 |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 30 | 4354 | 6315 | 7000 | 7000 | 7000 | 7000 |
| 60 | 2213 | 3934 | 6147 | 7000 | 7000 | 7000 |
| 89 | 1499 | 2666 | 4166 | 5999 | 7000 | 7000 |
| 90 | 0 | 0 | 0 | 0 | 0 | 0 |

A player kill at or above one's own tier is the single largest experience event in the game at every
tier from 50 up: the hitting-plus-kill XP of about two level-50 mobs at tier 60 and three and a half at
tier 89, and the victim loses only the tenth.

### Table 7: groups (equal-level members, level-matched median mob)

| tier | solo | duo per member | trio per member | trio total vs solo |
| ---: | ---: | ---: | ---: | ---: |
| 30 | 1980 | 1237 | 989 | 150% |
| 60 | 4576 | 2859 | 2286 | 150% |
| 89 | 2838 | 1772 | 1418 | 150% |

Grouping raises the total paid out by half but halves each member's share; per member, solo always wins.

### The player population

All 4143 characters on file, and those active within a year of the newest save:

| level band | 1-9 | 10-19 | 20-29 | 30-39 | 40-49 | 50-59 | 60-74 | 75-89 | 90 | 91+ (staff) |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| all | 355 | 163 | 1275 | 1785 | 379 | 74 | 16 | 3 | 5 | 88 |
| active ≤ 365 days | 15 | 36 | 247 | 589 | 249 | 58 | 15 | 2 | 4 | 13 |

Legends (30+) are 57 percent of all characters; mortals at 60 to 89 are 19 in total, 17 of them active
within a year. Of a 23-character sample from the top of the curve, 6 sat below their level's threshold
(inside the 20000-point delevel tolerance) and none in the top quarter of their level.

## Incentive synthesis

1. **Does the reward favour low-level content at high tiers?** (Tables 3 and 4.) Per swing, no: a level-89 character earns
   about 3 times more per swing on a level-50 mob (125) than on a level-15 mob (41), and the same ratio
   holds at 60 and 75. Per unit of risk, yes, decisively. A death to a mob at tier 89 costs 143585 points,
   the value of 72 level-50 kills or 1751 level-15 kills. Farming level-15 mobs carries no death risk. The
   break-even is one death per 106 level-50 kills at tier 89, per 73 at tier 75, per 36 at tier 60, and per
   18 level-30 kills at tier 30. Any content dangerous enough to kill a high-level character more often
   than that pays worse than farming trash.
2. **Which vector carries it?** (Tables 1 to 3.) Hitting XP, not kill XP. Kill XP for a level-15 mob at tier 89 is 12 points;
   hitting it twice pays 70. The level-gap divisors apply only to kills, so hitting is what a high-level
   character actually lives on, and it is why low-level mobs remain worth anything at all.
3. **Does "hitting very hard" matter?** (Table 2.) No. The per-hit formula caps damage at `20 + 2 * level`, which
   every high-level character exceeds routinely; what counts is the number of damage events. This also
   means fast weapons, multi-hit skills and damage spells are the efficient tools, not big single blows.
4. **Flee and death.** (Tables 4 and 6.) Flee loss is irrelevant at every tier (under 0.1 percent of a level). Death loss is
   the entire risk economy: half a level to a mob, one twentieth to a player. That asymmetry pushes
   high-level characters away from mob content and toward player kills, which are also the largest single
   reward available.
5. **Faction and alignment modifiers.** (Table 1.) The east bonus is real, small, and applied last: 15
   percent of a number the level-gap divisors have already reduced to a handful of points. It matters at
   tiers 30 to 50 in the east, not above. Good-on-good is the larger effect at every tier, taking a good
   character's kill XP on good mobs to about half of the neutral-or-evil median.
6. **The 7000 clamp.** (Table 5.) It caps a level-89 character at 39 events per level in theory, but median kills are
   so small that the practical count is 134 level-50 kills or 3274 level-15 kills. The clamp only bites
   the top decile of kills at tiers 30 to 50 and every equal-tier player kill.
7. **Level 90 is a dead end by code** (Tables 1, 2 and 6, and the population table), not by content: the gain gate stops at 89. Five mortals are parked
   there, and they can only go down.

The data supports the hypothesis behind the redesign, with one correction: the incentive is created by
the death penalty and the level-gap kill divisors together, and it is expressed through hitting XP. Removing
hitting XP alone, without addressing the level-gap divisors, would leave a level-89 character with 12
points per level-15 kill and 213 per level-50 kill and nothing else; removing death loss alone would
remove the risk asymmetry but leave hitting as the dominant vector.

## Open questions

- Actual death rates per kill at each tier, which set the real break-even; only live telemetry answers it.
- Time per kill (travel, regeneration, repop) is not modelled; every "per swing" figure is a lower bound
  on the true gap between farming and hunting.
- 256 of the 782 mobs flagged `MOB_SPEC` have no stored program, so their +10 percent depends on a
  boot-time procedure assignment the offline model cannot see.
- The amounts granted by the 54 script `GAIN_EXP` operations are script variables and were not read.
- Mental attacks apply the cap to five times their damage; whether that makes mystics hit the cap at lower
  levels than fighters was not evaluated.
