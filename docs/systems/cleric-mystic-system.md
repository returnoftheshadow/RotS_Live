# Cleric / Mystic system (powers & mental combat)

**Source files:** powers `mystic.cpp` (all `ASPELL(...)`), plus `spell_curse`/`spell_revive` there;
mental combat `clerics.cpp` (`do_mental` = the **will** command, `do_concentrate`,
`damage_stat`, `check_mind_block`, `weapon_willpower_damage`); saving throws `spell_pa.cpp:122-365`
(`saves_power`, `saves_mystic`, `saves_poison`, `saves_confuse`, `saves_insight`,
`saves_leadership`); caster level `mystic.cpp:68` (`get_mystic_caster_level`); spirit resource
`char_utils.cpp:93-110` (cap `MAX_SPIRITS = 90000`, gained on kills `fight.cpp:1256-1287`); power
table `consts.cpp:431-485,513-628`; ids `spells.h:85-117,177-179`; spec enum `structs.h:811`.
**Status:** ✅ powers, saves, mental combat, scaling. Mana regen (the mage resource) lives in
[magic-system.md §12](magic-system.md); this doc covers the **spirit** resource.

> **One class, two names.** "Cleric" and "Mystic" are the **same profession** (`PROF_CLERIC`);
> the code says `PROF_CLERIC` everywhere, players say "mystic". Its spells are called **powers**.
> Where the mage line (magic-system.md) is INT/spellpower/mana and rolls a **d20** save, the mystic
> line is **Willpower/Perception/spirit** and rolls **squared willpower contests** — a completely
> separate save family. A **default mystic is 30 mystic / 15 mage**; a default mage is the mirror
> (30 mage / 15 mystic), so every character has *some* of both.

## How to read this doc
Get these three quantities straight and the rest follows:

1. **Mystic caster level `L`** (`get_mystic_caster_level`, `mystic.cpp:68`) —
   `L = prof_level(CLERIC) + WIL/5` (+1 random chance for the `WIL % 5` remainder). Drives almost
   every power's magnitude/duration. **Willpower is baked into your caster level.**
2. **Willpower** — the offensive/defensive stat for **mental combat** (`saves_power` squares it).
3. **Perception** — gates whether you can *reach* a mind at all, and scales curse/concentrate.

The **spirit** pool (not mana) fuels powers (§1). "Typical" figures assume a **30-mystic** with a
**WIL ability score of ~22** (20–25 is the normal range; **25 is exceedingly high**) and
**best-in-slot willpower gear ≈ +10**. That gives caster level `L ≈ 34` and a mental-combat
**willpower of ~60** (§1 — note these are *different* numbers). Read the formulas as truth.

---

## 1. Caster level and the two resources

### Two different "will" numbers — don't conflate them
This is the mystic-side trap (mirror of the mage's double-`saving_throw`, magic-system §5):

| Quantity | Macro / field | Value | What it drives |
|---|---|---|---|
| **WIL ability score** | `GET_WILL` = `tmpabilities.wil` | **20–25** typical (25 very high); +`APPLY_WILL` gear | caster level `L` (via `/5`) |
| **Willpower** (derived combat stat) | `GET_WILLPOWER` = `points.willpower` | `≈ cleric_level + WIL` → **~50–65** for a 30-mystic | **every `saves_*` roll and `do_mental`** (§2, §3) |

`GET_WILLPOWER` is recomputed by `get_naked_willpower` (`utility.cpp:372`):
```
willpower = prof_level(CLERIC) + WIL_score − confuse/10   (+ APPLY_WILLPOWER gear)
```
So the player-facing "will" stat (20–25) is **only one input**; the number that actually fights
minds folds in your whole cleric level, landing around **50–65**. **Best-in-slot willpower gear
(≈ +10)** pushes that to ~60–75 (and, if it's `APPLY_WILL` stat gear, nudges `L` up by ~+2 too).

### Mystic caster level — `get_mystic_caster_level` (`mystic.cpp:68`)
```
L = prof_level(CLERIC) + WIL_score/5      (+1 random chance on the WIL % 5 remainder)
```
Uses the **WIL ability score** (the mirror of the mage's INT-based `get_mage_caster_level`), so a
30-mystic with WIL 22 has `L ≈ 34`. For powers cast on **others**, several spells average in the
*target's* mystic level (e.g. curing/restlessness: `(L_caster + L_victim)/2 + 5`;
detect-hidden/slow-digestion add the victim's cleric level), so buffing a fellow mystic is stronger
than buffing a warrior.

### Perception — the mystic's other core stat (`get_naked_perception`, `utility.cpp:354`)
Perception gates whether you can reach a mind, drives `saves_mystic`/`saves_insight`, and scales
curse/concentrate. For PCs:
```
perception = race_baseline + prof_level(CLERIC)·2     (+50 Insight / −50..−100 Pragmatism)
```
So perception is **strongly mystic-level-driven** (`×2` per level → +60 at 30 mystic) on top of a
fixed **per-race baseline** (`get_race_perception`, `utility.cpp:312`):

| Race | Base | Race | Base | Race | Base |
|---|---|---|---|---|---|
| High-elf (`RACE_HIGH`) | **100** | Human / Hobbit / Uruk / Harad(rim) | **30** | Dwarf | **0** |
| Undead | **60** | Easterling / Magus / Troll / Beorning / Olog-Hai | **30** | God | **0** |
| Wood-elf (`RACE_WOOD`) | **50** | Orc | **10** | | |

(NPCs: shadows = 100, otherwise their stored `GET_PERCEPTION`.) A human 30-mystic therefore sits at
`30 + 60 = 90` perception; a High-elf at `100 + 60 = 160`. Because **fear/haze/terror defense is a
pure perception roll** (`saves_mystic`: resist when `number(0,100) ≤ perception·9/10`), a
High-elf's 144+ effective threshold makes them **outright immune to those effects**, while an Orc
30-mystic (70) resists only ~63 % of the time. Willpower never enters fear defense directly — only
through Insight/Pragmatism shifting perception (§5).

### Spirit — the power resource (`points.spirit`, `char_utils.cpp:93`)
Mystic powers cost **spirit**, *not* mana:
- **Cost** = the power's `min_usesmana` column (`USE_SPIRIT`, `spells.h:331`) — a **flat** cost
  (0–10 for most powers; 25–55 for mass spells / shift), charged in `do_cast` (`spell_pa.cpp:942`).
  A **Fame-War** cleric pays **×0.80** (`spell_pa.cpp:944`).
- **Mental powers double-dip:** `curse` and `revive` *also* spend spirit **inside** the spell,
  proportional to how many stat-points they move (`number(0, stat_damage)` per stat) — so a big
  curse can drain a lot of spirit on top of the base cost.
- **Gained from kills, not time** (`fight.cpp:1256-1287`): when something dies, its
  `level × perception` worth of spirit is split among nearby characters by **perception share**.
  There is **no per-tick spirit regen — this is intentional** (see §7): spirit is a resource you
  *spend time accumulating* into a large pool (cap `MAX_SPIRITS = 90000`) and draw down when
  needed, rather than a steadily-refilling bar like mana.

> **Mana** (the *mage* resource) regenerates on a timer; that formula — and the fact that
> **mystic level feeds it via a `cleric_level/5` term** — is documented in
> [magic-system.md §12](magic-system.md). It matters here because a 30-mystic/15-mage still has a
> real mana pool for their mage spells.

---

## 2. The mystic saving-throw family (`spell_pa.cpp`)

Mystic powers do **not** use the mage `new_saves_spell` d20 (magic-system §3). They use a family of
purpose-built rolls. Positive outcome = **victim resisted**.

In the formulas below, **`Wp`** = the **derived willpower** stat (`GET_WILLPOWER` ≈ `cleric+WIL`,
~50–65; §1), **not** the 20–25 ability score. Subscripts `c`/`v` = caster/victim.

| Function | Used by | Formula (victim resists when…) |
|---|---|---|
| **`saves_power`** (`:122`) | **mental combat**, willpower weapons | `number(0, (Wp_v + bonus)²) > number(0, power²)` — squared willpower vs. the attacker's "power" |
| **`saves_mystic`** (`:291`) | haze, fear/terror, dispel | `number(0,100) ≤ Perception_v · 9/10` — pure perception roll |
| **`saves_poison`** (`:307`) | poison, black arrow | `number(off/3,off) < number(def/2,def)`, `off = Wp_c·8·Percep_c/100`, `def = 5·CON_v + 3·Wp_v (+30 wood-elf)` |
| **`saves_confuse`** (`:323`) | confuse, hallucinate | `number(0, Wp_c) < number(0, Wp_v − (NPC?5))` |
| **`saves_insight`** (`:339`) | insight vs. pragmatism | `Percep_c·Wp_c/100 < Percep_v·Wp_v/100 + number(0,10)` |
| **`saves_leadership`** (`:355`) | fear, terror | `saves_mystic` **or** `number(1,115) ≤ leader's Leadership` (mount: Ride) |

The throughline: **Willpower (`Wp`) and Perception** are to mystic saves what INT and the
`saving_throw` stat are to mage saves. There is no gear "spell save" stat in this family — your
*stats* (and willpower gear, which lifts `Wp` on both offense and defense) are the contest.
**Mind Block** (§3) adds a separate 20 % hard-stop layer on mental-stat attacks.

---

## 3. Mental combat — `will`, `concentrate`, `curse`

Mental combat is a **stat-attrition** duel: instead of HP, you grind down the opponent's six
ability scores (STR, INT, WIL, DEX, CON, LEA) and their "concentration". **Driving any stat to 0
kills the victim** (`damage_stat`, `clerics.cpp:294` → `die`). It runs on its own clock:
`GET_MENTAL_DELAY`, ticking in `PULSE_MENTAL_FIGHT = 8` pulses (**2 seconds**).

### Shared gates (every mental action)
1. **Peace room** blocks it. **Mental delay** must be ready (`> 1` = "mind is not ready").
2. **Reach check** (`clerics.cpp:181`): `Perception_ch · Perception_victim < number(1,10000)` ⇒
   "couldn't reach $S mind." It's the **product** of both perceptions, so high perception on
   *either* side makes the mind easier to reach — a very perceptive victim is *always* reachable
   (even as their perception makes them resist the effects below).
3. **Readable mind** (`clerics.cpp:169`): target must be a **Shadow**, **bodytype 1**, or a
   **Beorning** — most mobs "cannot be fathomed."
4. **Self mind-block** while you have Mind Block up: 75 % chance it prevents *your own* attack.

### `will` — `do_mental` (`clerics.cpp:89`)
The basic mental strike. Rolls **two** `saves_power` checks; each one the victim *fails* adds 1 to
`damg` (so `damg ∈ {0,1,2}`):
```
attacker power = Wp_ch   + (concentrating ? cleric_level/2 : 0)   # Wp = GET_WILLPOWER, ~50-65 (§1)
victim save_bonus =        (concentrating ? cleric_level/2 : 0)
```
On a hit (`damg > 0`): pick a random target `tmp = number(0,6)` — `0–5` = the six stats,
**`6` = concentration** (and the attacker *gains* `damg` spirit). `damage_stat` subtracts `damg`
from that stat. On total miss (`damg == 0`) there's a **25 % chance you damage yourself**. Success
grants exp and can **interrupt the victim's spellcasting** / ruin a prepared spell. Sets your
mental delay to `PULSE_MENTAL_FIGHT` (2 s).

### `concentrate` — `do_concentrate` (`clerics.cpp:468`)
A **charge-up**. Toggling it on sets `AFF_CONCENTRATION` and a deeply-negative mental delay; while
held it **passively buffs your `will`** (the `+cleric/2` power above) *and* your defense (the
`+cleric/2` save bonus). **Releasing it while fighting** unleashes a burst:
```
extra = (−mental_delay + number(0, PULSE_MENTAL_FIGHT−1)) / PULSE_MENTAL_FIGHT   # how long you held it
extra = extra · Perception_victim / 100
while (extra > 0 and victim fails saves_power(Wp_ch + extra, 0)):
    damage a random stat by 1;  extra −= 2
```
So the longer you concentrate, the bigger the multi-stat hit — but it's still gated by the
victim's willpower saves and scaled by *their* perception.

### `curse` — `spell_curse` (`mystic.cpp:119`)
A spirit-fueled AoE-on-one-target stat assault. It computes a **point budget** and scatters it
across stats:
```
count = (L + 20) · Perception_victim / 100 / 10          # number of stat-damage points
```
Then it distributes `count` among the six stats (random, biased to clump on one stat), spending
spirit per stat hit, each applied through `damage_stat`. Blocked by your own Mind Block and the
readable-mind gate. Mental delay after = `actual_count · PULSE_MENTAL_FIGHT`. **Curse scales with
mystic level *and the victim's perception*** — high-perception targets take a bigger curse (the
flip side of perception being their mental armor elsewhere).

### `damage_stat` & Mind Block (`clerics.cpp:294`, `:62`)
All the above route stat damage through `damage_stat`, which: checks **SPECIAL_DAMAGE** procs and
Big-Brother PK rules, **Mind Block**, and **sanctuary**; gives anger; sets combat; subtracts the
stat; and **kills on 0**. **Mind Block** (`check_mind_block`) only guards the **mental stats**
(INT, WIL, LEA, concentration — `is_mental_stat`): each hit has a **20 %** chance to be fully
blocked while chipping the block's duration until it breaks.

### Willpower weapons (`weapon_willpower_damage`, `clerics.cpp:537`)
A weapon **attuned** (the `attune` power sets `ITEM_WILLPOWER`) lands a bonus mental hit on melee
swings (`fight.cpp:2444`): same perception gate, a `saves_power` vs. `weapon_level +
victim_cleric_level`, then `max(1, min(WIL_attacker/10, 4))` damage to a random stat. (Note the
save scales on the **defender's** mystic level — a mystic defends their own mind better.)

---

## 4. Power catalog

Level = `prof_level(CLERIC)` to learn; Spirit = base `USE_SPIRIT` cost (mental powers spend more
on top, §1). `L` = mystic caster level (§1). Grouped by role.

### Mental / offensive
| Power (`id`) | Lvl | Spirit | Effect & scaling |
|---|---|---|---|
| **Curse** (67) | 1 | 1 | §3 — stat assault, `count = (L+20)·Percep_v/100/10` |
| **Revive** (68) | 1 | 2 | §3 inverse — **restores** the victim's most-damaged stats, `count ≈ (3·9+L)·Percep_v/100/9`, spends spirit |
| **Hallucinate** (63) | 3 | 2 | foe "misses" illusions; charges = `cleric/10 + 2 (+1 if cleric>30) (+1 Illusion-spec)`; save `saves_confuse` |
| **Haze** (52) | 5 | 1 | `AFF_HAZE` disorient; **+6 `L` Illusion-spec**; save `saves_mystic`; can haze a room |
| **Fear** (53) | 12 | 5 | flee effect, dur `L`; **+6 `L` Illusion-spec**; saves `saves_mystic` **&** `saves_leadership` |
| **Terror** (58) | 18 | 5 | room-wide Fear; **+6 `L` Illusion-spec** |
| **Poison** (43) | 14 | 5 | `AFF_POISON` (−2 STR, dur `L+1`) + 5 dmg; save `saves_poison`; can poison a room/food |
| **Confuse** (111) | 1* | 10 | `AFF_CONFUSE` (scrambles skill knowledge), dur `10+L`; save `saves_confuse`; *Illusion spec-gated* |

### Healing / regeneration *(Regeneration spec: +6 to the healing level)*
| Power (`id`) | Lvl | Spirit | Effect & scaling |
|---|---|---|---|
| **Curing Saturation** (45) | 4 | 1 | bonus HP regen `modifier = L+5` (avg w/ target), dur `~(L+5)·FAST_UPDATE_RATE/2` |
| **Restlessness** (46) | 6 | 0 | bonus **move** regen (mirror of Curing; can go negative/lethal if dispelled oddly) |
| **Regeneration** (64) | 15 | 5 | strongest HP-over-time, `regen_level = (L−10)/2·FAST_UPDATE_RATE` |
| **Vitality** (57) | 11 | 5 | move-regen buff, dur scales `L/3·(SECS_PER_MUD_HOUR·4)/PULSE_FAST_UPDATE` |
| **Mass Regen / Vitality / Insight** (158-160) | 15/11/6 | 50/50/25 | group-wide; **require 100 % skill** in the base power; free to "cast" (Expose-style) |
| **Resist Poison** (44) | 3 | 2 | suppress active poison |
| **Remove Poison** (87) | 8 | 2 | cure poison (char or food/drink) |

### Buffs / self & ally
| Power (`id`) | Lvl | Spirit | Effect & scaling |
|---|---|---|---|
| **Mind Block** (86) | 3 | 5 | mental-stat shield (§3), dur **`15 + 2·cleric`** (self only) |
| **Insight** (50) | 6 | 2 | **+50 Perception**, dur `10+L`; breaks/blocked by Pragmatism; save `saves_insight` |
| **Pragmatism** (51) | 6 | 5 | **−50 Perception** (−100 vs. wood-elf) debuff; the anti-Insight |
| **Evasion / "armor"** (42) | 2 | 0 | `AFF_EVASION` + armor mod `loc_level`; self `(L+5)/2`, ally `(cleric_v+L+5)/4` |
| **Resist Magic** (47) | 8 | 0 | **+`L/6` mage saving-throw** (`APPLY_SAVING_SPELL`, feeds magic-system §4); **+1 Protection-spec** |
| **Protection** (89) | 0* | 5 | elemental/physical **resistance** (`APPLY_RESIST` fire/cold/lightning/physical), dur `2L`; *Protection spec-gated* |
| **Death Ward** (83) | 20 | 0 | ward, dur `2L`, modifier `L/2` |
| **Sanctuary** (56) | 16 | 2 | `AFF_SANCTUARY`; blocked by Anger; dur = target cleric level |
| **Detect Hidden** (41) / **Detect Magic** (69) / **Infravision** (66) | 1-17 | 0-2 | detection buffs, dur scales with `L` (×3 / ×5 / ×1) |
| **Slow Digestion** (48) | 11 | 1 | slows hunger, dur `loc_level+12` |

### Utility / misc
| Power (`id`) | Lvl | Spirit | Effect |
|---|---|---|---|
| **Divination** (54) | 13 | 2 | reveals room flags, exits, doors/keys, contents (a builder/scout tool) |
| **Attune** (—) | — | — | flags wielded weapon `ITEM_WILLPOWER` → enables willpower-weapon hits (§3) |
| **Enchant Weapon** (60) | 20 | — | +6 OB + alignment flag on a non-magic weapon (one-time) |
| **Dispel Regeneration** (49) | 13 | 3 | strips Curing/Restless/Regen/Vitality; enemy targets get a `saves_mystic` |
| **Guardian** (65) | 10 | — | *Guardian spec-gated* — summons a pet mob; see §5 |
| **Shift** (70) | 30 | 55 | toggles shadow form — **immortal-only** in practice |

\* Levels marked `*` are spec-gated powers (`learn_type` includes `LEARN_SPEC`); the table `level`
is nominal.

---

## 5. Scaling: specialization, mystic level, willpower

**Mystic level (`prof_level(CLERIC)`)** is the master lever: it is the bulk of `L` (durations &
magnitudes), the Mind-Block duration (`15+2·cleric`), the `concentrate` will bonus (`cleric/2`),
the Hallucinate charge count, the resist-magic modifier (`L/6`), and how much spirit you draw from
kills (perception-weighted but level-driven).

**Willpower** works on two layers (§1). The **WIL ability score** (20–25; 25 very high) is a minor
input to caster level `L` (only `WIL/5`, so ~+4–5 levels). The **derived willpower**
`Wp = cleric+WIL` (~50–65) is the heavy hitter: it's the squared offense *and* defense of every
`saves_power` exchange (§3) and the offense/defense term in `saves_confuse` / `saves_poison` /
`saves_insight`. Because `Wp` already contains your whole cleric level, **+10 willpower gear is a
~15–20 % swing** on it, not a doubling — meaningful but not transformative; a high-WIL mystic both
attacks minds harder and resists them.

**Perception** is the enabler: `race_baseline + cleric·2` (§1), so it scales hard with mystic level
on top of a per-race floor (High-elf 100 → Dwarf 0). It powers the reach check (`Percep·Percep` vs
`number(1,10000)`), `saves_mystic`/`saves_insight`, and **scales curse & concentrate output** by
`Percep_victim/100`. Crucially it is the **sole defense against fear/haze/terror** (`saves_mystic`
is pure perception — no willpower), so a high-perception race like the High-elf is effectively
immune to those. **Insight** (+50) and **Pragmatism** (−50) are the only willpower-mediated levers
that move perception, which is how willpower indirectly touches fear resistance.

**Specializations** (enum `structs.h:811`; framework in [specializations.md](specializations.md)):
- **Regeneration** (`PS_Regeneration` / `PLRSPEC_REGN`): **+6** healing level on Curing,
  Restlessness, Vitality, Regeneration (and +5/+10 on the mage cure/vitalize self). Owns the
  `refresh all` spec power.
- **Protection** (`PS_Protection` / `PLRSPEC_PROT`): **+1** Resist-Magic modifier; the
  **Protection** power is spec-gated. (Also a combat evasion role — specializations.md.)
- **Illusion** (`PS_Illusion` / `PLRSPEC_ILLU`): **+6 `L`** on Haze, Fear, Terror and **+1**
  Hallucinate charge; **Confuse** is spec-gated.
- **Guardian** (`PS_Guardian` / `PLRSPEC_GRDN`): the **Guardian** power is spec-gated. It summons
  a charmed pet in one of three builds, all scaling with mystic level (`mystic.cpp:1491-1576`):
  **aggressive** (`OB = 13·L/5`, +STR/−WIL, high damage), **defensive** (`parry = 8+2L`,
  `dodge = 8+L`, tanky HP `~22·L/3`), or **mystic** (high WIL, no OB/parry, caster-like). HP
  `= base·(L + number(−3,3))/3`.
- *Teleportation/Fire/Cold/etc. are **mage** specs* (magic-system §3/§8), not mystic.

---

## 6. Worked example — a 30-mystic `will`-attacks an equal player

Caster: **human** 30 mystic, **WIL score 22**, **+10 willpower gear**. So `L = 30 + 22/5 ≈ 34`,
**`Wp_caster = 30 + 22 + 10 = 62`** (the §1 derived stat), and perception `= 30 + 30·2 = 90`.
Target: a similar human 30-mystic with **no willpower gear** → `Wp_victim = 30 + 22 = 52`,
perception 90.

- **Reach:** `Percep_c · Percep_v = 90·90 = 8100` vs `number(1,10000)` — connects ~81 % of attempts
  (perception is a real gate against players; most mobs can't be read at all). Note the victim's
  perception is a *factor in the product*, so it cuts both ways: a High-elf target (perception 160)
  gives `90·160 = 14400`, which **always** clears the roll — they're **always reachable** for
  will/curse, yet simultaneously **immune to fear/haze/terror** (that's the separate `saves_mystic`
  roll on their own perception). High perception is great defense against *effects*, but makes your
  mind trivially *reachable* for stat attrition.
- **Strike (`will`):** two `saves_power` rolls, each the victim resists when
  `number(0, 52²=2704) > number(0, 62²=3844)` → resist chance `2704/(2·3844) ≈ 35 %`, i.e. the
  attacker **lands ~65 % per roll**, so `damg` averages ~1.3 per `will`. **Concentrating first**
  raises power to `62 + 30/2 = 77` (`77²=5929`) → resist drops to ~23 %, ~1.5 dmg/will, and grants
  the same `+15` to your own defense while held.
- **Curse instead:** `count = (L + 20)·Percep_v/100/10 = 54·70/100/10 ≈ 3` stat-points scattered
  across the six stats (spending spirit each) — a burst that bypasses `will`'s two-roll cap.
- **Lethality:** ability scores are only **~20–25**, and **any one stat hitting 0 is an instant
  kill** — but `will` damages a *random* stat ~1–2 at a time, so spread damage rarely focuses one
  stat down fast. The path to a kill is **curse/concentrate to pile damage on**, and a victim with
  any stat dropping toward `MIN_SAFE_STAT = 3` flees. Mind Block can hard-stop 20 % of the
  mental-stat hits along the way.

---

## 7. Open questions / flags

- **Spirit has no passive regen — intentional (confirmed).** It's a "spend time to acquire, bank a
  large pool for when you need it" resource, refilled only by participating in kills. This system
  is acknowledged as likely needing a redesign, but that's a **design decision, not a bug** — do
  not change it autonomously.
- **Two "will" quantities** (§1): the WIL ability score (`GET_WILL`, ~20–25) feeds only caster
  level, while the derived `GET_WILLPOWER` (`cleric+WIL`, ~50–65) drives all saves and mental
  combat. The shared word is a footgun (mirror of the mage `saving_throw` collision, magic-system
  §5); a rewrite should rename one (e.g. `wil_score` vs `mental_power`).
- **`get_mystic_caster_level` uses `WIL/5` once**, unlike `get_magic_power` which double-counts INT
  (magic-system §1) — asymmetry between the two caster-level functions; intentional?
- **Restlessness can be lethal** via negative regen (`limits.cpp:1508` notes "characters can die to
  negative regen") — verify the dispel/refresh paths can't strand a victim at lethal negative move
  regen.
- **`spell_death_ward` ANTI path removes `SPELL_INSIGHT`** (`mystic.cpp:1421`, even tagged
  "What the fuck?") — almost certainly a copy-paste bug (should remove Death Ward).
- **`spell_protection` leaves a `fprintf(stderr, ...)` debug line** (`mystic.cpp:1736`) — strip.
- **`saves_poison` `magus_save` is hard-coded 0** in `spell_poison` (`mystic.cpp:1219,1254`), so
  `number(0,0) < 50` is always true — the intended Magus poison resistance is a no-op.
- Powers flagged for removal in code comments: **Shift** (immortal-only), and the room
  Haze/Poison variants overlap mage versions.
