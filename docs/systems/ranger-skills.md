# Ranger skills

**Source files:** skill table `consts.cpp:390-427,471,538,566,586,590`; `SKILL_*` ids
`spells.h:45,59-79,164-167`; almost every handler lives in `ranger.cpp`; passive defense/offense
math in `utility.cpp` (`get_real_dodge`, `get_real_OB`, `get_real_parry`); movement/regen hooks in
`act_move.cpp` and `profs.cpp`. Damage is applied through the **live** `damage()` /
`fight.cpp::hit()` path (the `combat_manager` is unused dead code — see CLAUDE.md).
**Status:** ✅ ranger skill catalog with gates, plus the DEX-vs-ranger-level analysis for dodge and
skill effectiveness.

## How to read this doc

Every ranger skill is one row of `struct skill_data skills[]` (`consts.cpp:380`, struct at
`spells.h:355`). The columns used below, in table order:

```
{ name, type, level, spell_ptr, min_position, min_usesmana, beats, targets, learn_diff, learn_type, is_fast, skill_spec }
```

- **`level`** — minimum **ranger profession level** (`PROF_RANGER`) before the skill can be
  practiced. Ranger level is the proficiency level, *not* the character's overall level.
- **`min_usesmana`** — mana cost (only the Haradrim racial powers + Mark spend mana).
- **`beats`** — base recovery in heartbeats/pulses (4 pulses ≈ 1 s); several skills scale this down
  by ranger level (see each handler).
- **`learn_diff`** — practice difficulty.
- **`learn_type`** — `1` = normally learnable; **`65` = specialization-gated** (only obtainable
  through the matching spec).
- **`skill_spec`** — `PLRSPEC_PETS` (animal handling), `PLRSPEC_STLH` (stealth), or `PLRSPEC_NONE`.

For active skills the recurring pattern is an integer **success roll** of the shape
`skill + ranger_level + dex/offense − target_defenses + 1dN − constant`; if it comes out negative,
the skill fails. The exact constants are given per skill.

---

## Skill catalog

### Passive combat / defense

| Skill | `SKILL_*` | Min R-lvl | Notes |
|-------|-----------|:---------:|-------|
| **Dodge** | `SKILL_DODGE` (21) | 1 | Core of the player dodge formula (`utility.cpp:870`). Scales with ranger level; see §"DEX vs ranger level". |
| **Stealth** | `SKILL_STEALTH` (31) | 2 | `PLRSPEC_STLH`. Feeds dodge, hiding, ambush, and shooting-from-hiding. `get_real_stealth` is used throughout `ranger.cpp`. |
| **Awareness** | `SKILL_AWARENESS` (32) | 1 | Defensive. Halved value is subtracted from an attacker's ambush success (`ranger.cpp:812`) and gates seeing hidden chars. |
| **Riposte** | `SKILL_RIPOSTE` (20) | 20 | Counter-attack after a successful parry; resolved in the live melee path (`fight.cpp`). |
| **Fast attack** | `SKILL_ATTACK` (22) | 3 | Passive extra-attack / speed contributor in the round loop. |
| **Accuracy** | `SKILL_ACCURACY` (34) | 11 | Improves to-hit; folded into weapon/archery OB math. |
| **Parry** | `SKILL_PARRY` (11) | — | Shared warrior skill, but it is the dominant term in `get_real_parry` for everyone (`utility.cpp:801`). |

### Stealth & movement

| Skill | `SKILL_*` | Min R-lvl | Notes |
|-------|-----------|:---------:|-------|
| **Sneak** | `SKILL_SNEAK` (23) | 5 | Toggle: move without announcing yourself. |
| **Hide** | `SKILL_HIDE` (24) | 2 | Conceal in room; "hide well" trades wait time for stronger concealment. Detection is `awareness · RangerLevel/30` (`ranger.cpp:1964`). |
| **Stalking** | `SKILL_STALK` (38) | 1 | **Spec-only** (`learn_type 65`, `PLRSPEC_STLH`). Move a direction leaving obscured/no tracks. |
| **Track** | `SKILL_TRACK` (26) | 0 | Read footprints in a room to see who passed and which way. |
| **Search** | `SKILL_SEARCH` (28) | 0 | Find hidden objects/exits. |
| **Pick lock** | `SKILL_PICK_LOCK` (27) | 1 | Open locked doors/containers; can fail audibly. |
| **Swimming** | `SKILL_SWIM` (8) | 2 | Water movement; move budget = `RangerLevel + SKILL_SWIM` (`act_move.cpp:227`). |
| **Travelling** | `SKILL_TRAVELLING` (39) | 6 | Boosts the move-point pool: `+ SKILL_TRAVELLING/4` (`profs.cpp:745`); reduces travel fatigue. |
| **Ride** | `SKILL_RIDE` (33) | 0 | Mount `MOB_MOUNT` animals; supports multiple riders. |

> Also: when a ranger **sneaks into** a room a small automatic hide can trigger
> (`GET_PROF_LEVEL(PROF_RANGER) > number(0,60)`, `ranger.cpp:1892`); the Stealth spec halves the
> associated wait.

**Hide vs. hide well — and how stealth feeds it.** Hiding does **not** change `get_real_stealth`;
instead `get_real_stealth` is a *1:1 input* to the concealment level. `hide_prof`
(`ranger.cpp:1937`) computes the ceiling:
```
hide_prof = SKILL_HIDE + get_real_stealth(hider) + RangerLevel − 30
```
The actual stored concealment `GET_HIDING` is a random fraction of that ceiling, and **that
fraction is the only thing "well" changes** (`ranger.cpp:722-730`):

| Mode | `GET_HIDING` roll | Base recovery (`beats=4`) |
|------|-------------------|---------------------------|
| **hide** | `number(hide_prof/2, hide_prof·3/4)` — 50–75 % | `4·(0·2+1)` = 4 beats (3 if just snuck in) |
| **hide well** | `number(hide_prof·3/4, hide_prof)` — 75–100 % | `4·(1·2+1)` = 12 beats (7 if just snuck in) |
| auto-hide on sneak-in | `number(hide_prof/3, hide_prof·4/5)` — 33–80 % | (folded into the sneak move) |

So "hide well" buys a higher and more reliable `GET_HIDING` (top quartile of the ceiling instead of
the 50–75 % band) at ~3× the wait. Every point of `get_real_stealth` raises the ceiling one-for-one,
so terrain/spec/race/encumbrance flow straight through into how well you hide. Per the code's own
calibration (`ranger.cpp:1926`): a 30r with 100 % hide/stealth on average terrain lands `GET_HIDING`
in ~[75,125]; a 36r hobbit maxed in the best terrain reaches ~[135,180] ("legend hide"). `GET_HIDING`
is later worn down by observers' Awareness (`act_info.cpp:3378`) and mage detection (`mage.cpp`).

### Active offense

- **Ambush** — `SKILL_AMBUSH` (25), min R-lvl 6. Surprise strike from stealth.
  - **Success** (`ambush_calculate_success`, `ranger.cpp:806`):
    ```
    P = 1d(-100..0) + 1d(-20..20)
        − target_Level − ½·target_Awareness        (targets only)
        + RangerLevel + 15
        + SKILL_AMBUSH + real_stealth
        + 25·(FIGHTING − target_position)           (bonus if target sitting/resting)
        − target_AmbushedCounter − your_encumbrance
    ```
  - **Damage cap** scales with ranger level: `RangerLevel·10`, and past `LEVEL_MAX` only `+4`
    per level (`calculate_ambush_damage_cap`, `ranger.cpp:828`).
  - **Damage** (`ambush_calculate_damage`, `ranger.cpp:843`) is `(60 + modifier)` scaled by
    `min(victimHP, RangerLevel·20)`, divided by an encumbrance term, `+ RangerLevel − victimLevel + 10`;
    Stealth spec multiplies by 3/2.

> **Does being hidden boost ambush? Not directly — but the setup hiding enables does.**
> Nothing in the ambush path reads `AFF_HIDE`/`GET_HIDING`: the success formula uses
> `get_real_stealth` (identical whether you're hidden or standing in the open), and there is no
> "from hiding" multiplier. The experiential "hidden ambush hits much harder" comes from **two
> success-modifier terms that hiding lets you line up**, plus an initiation gate:
> - **`− GET_AMBUSHED(victim)` (`ranger.cpp:819`)** — a per-victim "already jumped / alert"
>   counter. Each ambush adds **+50 (NPC) / +75 (PC), +15** more if you're not aggro
>   (`ranger.cpp:1011`) and it decays only ~10 %/tick for PCs (`limits.cpp:673`). So your **first**
>   strike on a fresh target subtracts **0**; a follow-up minutes later subtracts ~75+, frequently
>   driving the modifier negative → **0 damage**. Hiding is how you deliver that clean opener.
> - **`+ 25·(FIGHTING − position)` (`ranger.cpp:816`)** — **+25 sitting, +50 resting, +75
>   sleeping**. A lurking, hidden ranger waits for the target to sit and heal, then strikes; an
>   open ambusher faces a standing, alert target and gets none of this.
> - **Initiation gate** — you can't ambush a target that is already fighting (`"too alert"`,
>   `ranger.cpp:921`) and you must be able to see them. Being hidden lets you stage the
>   out-of-combat first hit instead of being mid-melee.
>
> Because the modifier feeds damage **directly** (`dmg = (60 + modifier)·…`, and `modifier ≤ 0`
> ⇒ 0), these ±50–150 swings are as large as the ranger-level term — so "from hiding" is really
> "fresh `GET_AMBUSHED` + a resting target," which correlates almost perfectly with attacking from
> concealment. (Minor extra: if you *snuck in*, `get_real_stealth` adds `+ SKILL_SNEAK/20`,
> `utility.cpp:590` — a stealth-approach bonus, again tied to the setup, not the `AFF_HIDE` flag.)
>
> **What the defender can do about it:**
> - **Awareness — yes, it cuts both the hit chance *and* the damage.** The term
>   `− ½·SKILL_AWARENESS` (PCs only, `ranger.cpp:812`) is subtracted straight from the modifier.
>   There is **no separate to-hit roll** — the modifier *is* both the success value (it must be
>   `> 0` for the energy/parry strip to land, `ranger.cpp:1003`) and the damage input (`dmg =
>   (60 + modifier)·…`, `0` if `modifier ≤ 0`). So full awareness (100) shaves **−50** off the
>   modifier, which simultaneously makes a clean ambush less likely *and* lowers the damage when
>   one does land — and can push marginal ambushes to flat 0. It's the single strongest defense in
>   the formula.
> - **"Detect hidden" — no, it does nothing to ambush.** The spell/power (cleric
>   `spell_detect_hidden`, mystic `mystic.cpp:431`; `AFF_DETECT_HIDDEN`) only powers `can_sense`
>   (`utility.cpp:1546`), a pure **visibility** check — whether you can *perceive* a hidden
>   character given your perception vs. their `GET_HIDING`. The ambush success/damage code never
>   reads `AFF_DETECT_HIDDEN`, `GET_PERCEPTION`, `can_sense`, or even whether the victim can see the
>   attacker (recall ambush doesn't require the attacker to be hidden in the first place). So detect
>   hidden grants **zero** ambush mitigation; its only value is letting a would-be victim *notice*
>   a lurking attacker and react first — player agency, not a coded damage reduction.
- **Trap** (`do_trap`, `CMD_TRAP`) — set an ambush trap that fires when a target enters; ~half of
  direct-ambush damage; needs a light weapon.
- **Archery / Shoot** — `SKILL_ARCHERY` (61), min R-lvl 1. Fire arrows from a bow.
  - **Damage** (`shoot_calculate_damage`, `ranger.cpp:2087`):
    ```
    damage = RangerLevel·0.5·rand(0.5,1.0)
           + (rand(0..(arrow_todam+bow_dmg)·1.25) + (STR−10)·0.5) · level_multiplier
    ```
    where **`level_multiplier`** (`get_ranger_level_multiplier`, `ranger.cpp:2059`) is `0.8` up to
    R-lvl 20, then `+0.02` per level (so 0.9 at 25r, 1.0 at 30r). Armor is reduced per hit
    location; an "accurate shot" ignores armor.
  - **Recovery** shrinks with ranger level and energy regen: `total_beats −= RangerLevel/12`
    (`shoot_calculate_wait`, `ranger.cpp:2120`); Wood-elves/Haradrim get −1 more.
  - **Recover** — retrieve your tagged arrows from the room/corpses.

### Foraging & utility

- **Gather herbs** — `SKILL_GATHER_FOOD` (30), min R-lvl 5. Forage food, light, healing/energy
  herbs, bows, arrows, dust, and poison; yield depends on skill and terrain (`ranger.cpp`).

### Animal handling (`PLRSPEC_PETS`)

- **Animals** — `SKILL_ANIMALS` (29), min R-lvl 10. Foundational knowledge; adds half its value
  to taming strength.
- **Calm** — `SKILL_CALM` (36), min R-lvl 1. Success if `calm_skill > 1d150`; clears
  `MOB_AGGRESSIVE` and applies `AFF_CALM`. Wait time shrinks with ranger level:
  `beats·2·victimLevel / (RangerLevel + calm_skill/15)` (`ranger.cpp:1388`).
- **Tame** — `SKILL_TAME` (35), min R-lvl 2. Requires being "strong enough":
  `RangerLevel/3 + (SKILL_TAME + SKILL_ANIMALS/2)/30 − animalLevel` must exceed current
  followers (`is_strong_enough_to_tame`, `ranger.cpp:1432`). Duration grows with ranger level.
  **⚠ Bug — see [Known bugs](#known-bugs).**
- **Whistle** — `SKILL_WHISTLE` (37), min R-lvl 1. **Spec-only**. Zone-wide pet recall; grants
  pets `AFF_HUNT` speed.

### Haradrim / divine racial powers (cost mana)

| Skill | `SKILL_*` | Min R-lvl | Mana | Gate / effect |
|-------|-----------|:---------:|:----:|----------------|
| **Mark** | `SKILL_MARK` (124) | 15 | 20 | Spoken; marks a target for bonus damage. Success `= SKILL_MARK − targetDodge − ½·targetParry + ½·yourOB + ½·RangerLevel + 1d100 − 120` (`ranger.cpp:2945`). Duration `≈ RangerLevel − 10` (`ranger.cpp:2879`). |
| **Blind (dust)** | `SKILL_BLINDING` (110) | 27 | 20 | Haradrim. Throw gathered dust → `AFF_BLIND`. Uses the Haradrim save (below). |
| **Wind blast** | `SKILL_WINDBLAST` (128) | 24 | 20 | Haradrim. Room-wide knockback (`TAR_IGNORE`), +moves. |
| **Bend time** | `SKILL_BEND_TIME` (95) | 30 | 6+ | Haradrim. Doubles energy, +20 OB for a duration; costs mana + moves. |

The Haradrim power save (`harad_skill_calculate_save`, `ranger.cpp:3254`) is the clearest example
of the standard opposed roll:
```
attack  = caster_skill + caster_RangerLevel + (caster_DEX + 10) + 1d100
defense = victim_DEX + victim_dodge + victim_RangerLevel + victim_CON/4
hit if (attack − defense) > 0
```

---

## DEX vs ranger level: who drives dodge and skill effectiveness

This is the headline question, so it gets its own section. All formulas below are the **live**
ones (`utility.cpp`), not the dead `char_utils_combat.cpp` variants.

### Dodge (`get_real_dodge`, `utility.cpp:860`)

**Players** (`utility.cpp:870-909`) compute:
```
dodge = (SKILL_DODGE + SKILL_STEALTH/2 + 60) · RangerLevel / 200       ← ranger-level term
      + (SKILL_DODGE + SKILL_STEALTH/4) / 20                            ← small flat skill term
      − dodge_penalty(encumbrance) + 3
      [+20 Beorning] [Berserk halves the running subtotal] [sun/Arda race scaling]

return dodge + GET_DODGE(ch) + tactics_offset + GET_DEX(ch)            ← DEX added at the very end
```
with `tactics_offset` = +6 Defensive / +4 Careful / 0 Normal / −4 Aggressive / −4 Berserk (Berserk
also adds only `DEX/2`).

**How the two compare.** Think of the ranger-level term as a *gain* on your dodge/stealth skills:
at 100/100 skill, `(100 + 50 + 60)·R/200 = 210·R/200 ≈ 1.05·R`. So the **ranger-level contribution
is roughly equal to your ranger level in dodge points** when your skills are maxed (≈105 at 30r),
and proportionally less while skills are still being trained. **DEX contributes its full value once,
flat** (a typical 17–22 DEX = +17 to +22). 

Bottom line for a maxed 30r: ranger level supplies the **bulk** of dodge (~100+ points scaled by
skill), DEX a **fixed top-up** of ~20, and `GET_DODGE` (gear/affects) the rest. Early on — low
ranger level or unpracticed Dodge/Stealth — DEX is proportionally far more important because the
level term is small. DEX never scales with level here; it is a constant addend.

> **NPCs** use a much simpler line: `GET_DODGE + DEX − 5 + Level/2` (`utility.cpp:868`) — there DEX
> and half the mob's level matter directly and skills don't exist.

### OB — where DEX can replace strength (`get_real_OB`, `utility.cpp:647`)

Normally OB is strength-driven (`offense_stat = GET_BAL_STR`). **Only the Light-Fighting
specialization** lets a ranger lean on DEX (`utility.cpp:667-677`): with a light enough weapon,
```
offense_stat = max(BAL_STR, DEX)            ← DEX can now drive OB
warrior_level += RangerLevel / 3            ← ⅓ of ranger level counts as warrior level for OB
```
The base bonus is `ob_bonus = (warrior_level·3 + 3·maxWarrior·LEVELA/30)/2 + offense_stat`. So for a
Light-Fighting ranger, **ranger level adds ⅓-for-1 into the warrior-level OB term, and DEX (if it
exceeds balanced STR) becomes the offense stat.** Outside Light Fighting, DEX does *not* feed OB and
ranger level only contributes through the `/3` rule when it applies.

### Parry (`get_real_parry`, `utility.cpp:761`)

Parry is **warrior-level and strength** based — `bonus = PROF_WARRIOR·2 + min(30, Level) + BAL_STR`
(`utility.cpp:775`), weighted heavily by the Parry skill (`utility.cpp:801`). **DEX is not used in
parry, and ranger level does not feed it directly.** A pure ranger parries mainly off the Parry
skill, weapon, and overall level.

### Skill-effectiveness rolls

Across the active ranger skills, **ranger level and DEX both enter the opposed success roll, but
ranger level is the term that scales the most**:

| Skill | Ranger-level term | DEX term |
|-------|-------------------|----------|
| Ambush success (`ranger.cpp:806`) | `+ RangerLevel + 15` (1:1) | none directly (DEX rides in via stealth/dodge) |
| Ambush damage (`ranger.cpp:843`) | scales `min(hp, RangerLevel·20)`, `+RangerLevel` | none |
| Mark success (`ranger.cpp:2945`) | `+ RangerLevel/2` | indirect (via your OB) |
| Mark damage (`ranger.cpp:2892`) | `RangerLevel` (1:1) `+ STR/5 + DEX/3` | `DEX/3` |
| Haradrim save (`ranger.cpp:3254`) | `+ RangerLevel` (1:1) | `+ DEX (+10)` (1:1) |
| Archery damage (`ranger.cpp:2087`) | `·0.5` term + 0.8→1.0 multiplier | `(STR−10)·0.5` (STR, not DEX) |
| Calm/Tame timing & strength | `RangerLevel`, `RangerLevel/3` | none |

**Summary:** ranger level is the primary scaler for ranger abilities — it gains your dodge skills,
caps and grows ambush/archery damage, shortens cooldowns, and adds 1:1 to most success rolls.
**DEX is a flat secondary contributor**: a constant ~+20 to dodge, a 1:1 term in opposed magical
saves, a `/3` term in Mark damage, and — only under Light Fighting — a potential replacement for
strength in OB. DEX matters most at low ranger level (when the level-scaled terms are small);
ranger level dominates once the character is high level with practiced skills.

---

## Known bugs

### Tame: existing followers don't reduce taming capacity (`ranger.cpp:1432`)

`is_strong_enough_to_tame` computes how many "levels over the requirement" the ranger has, and is
*supposed* to subtract the level of animals already being led when
`include_current_followers` is true:

```cpp
int levels_over_required =
    (GET_PROF_LEVEL(PROF_RANGER, tamer) / 3 + tame_skill / 30 - GET_LEVEL(animal));
if (include_current_followers) {
    levels_over_required - get_followers_level(tamer);   // ⚠ result discarded
}
```

The subtraction is a bare expression statement — its value is thrown away because it is never
assigned back to `levels_over_required` (it should be `levels_over_required -= …`). As written, the
`include_current_followers` branch is a no-op: a ranger's already-tamed followers impose **no
penalty** on taming additional animals, so the intended cap on simultaneous pets is not enforced
through this path. (Compilers will typically warn `-Wunused-value` here, but the project builds with
warnings suppressed — see CLAUDE.md.) The fix is `levels_over_required -= get_followers_level(tamer);`.

---

## Illustrative examples: stealth → ambush

> **Two corrections to the premise first.** `get_real_stealth` (`utility.cpp:574`) depends on
> **neither DEX nor ranger level**. Its inputs are: the **Stealth skill %** (`/4`), the Stealth
> spec (+5), an active sneak/snuck-in bonus, **sector type**, **race**, and **encumbrance**. And
> the **ambush** path uses **no DEX at all** — success keys off ranger level + ambush skill +
> stealth, and damage off ranger level + victim HP + encumbrance + weapon. So the DEX figures in
> the request (18/20/22) don't move either number; the examples below vary the things that *do*.

### `get_real_stealth` for the requested rangers

With Stealth practiced to 100, on open ground (`SECT_FIELD`, +0), human, unencumbered, not mid-sneak,
no Stealth spec:

```
get_real_stealth = SKILL_STEALTH/4 = 100/4 = 25
```

…which is **25 for every one of 24r/18dex, 27r/20dex, 30r/20dex, 33r/22dex, 36r/22dex** — it does
not change with ranger level or DEX. What actually shifts it (cumulative):

| Factor | Δ to stealth |
|--------|:---:|
| Stealth skill (per point) | `+skill/4` (so 100 → +25, 60 → +15) |
| Stealth specialization (`PLRSPEC_STLH`) | +5 |
| Dense forest / forest / hills / swamp | +20 / +15 / +5 / +5 |
| City / road / crack | −10 / −5 / −10 |
| Inside / any water | −20 |
| Hobbit / Beorning / Haradrim · Dwarf | +5 · −10 |
| Encumbrance | `− leg_enc − enc/4` |
| Sneaking having "snuck in" | `+ SKILL_SNEAK/20` |

So a maxed Stealth-spec wood-ranger in dense forest reads `25 + 5 + 20 = 50`; the same ranger
indoors in heavy armor could be near 0.

### Ambush success modifier (the value passed as `modifier` to damage)

`ambush_calculate_success` (`ranger.cpp:806`) returns
```
mod = 1d(−100..0) + 1d(−20..20) + RangerLevel + 15 + SKILL_AMBUSH + get_real_stealth
      − victimLevel − ½·victimAwareness(PC only) + 25·(FIGHTING−pos if resting) − ambushed − enc
```
**Assumptions for the grid:** SKILL_AMBUSH 100, stealth 25 (from above), victim standing & fresh
(`GET_AMBUSHED 0`), attacker unencumbered. "Awareness" is the victim's Awareness **skill** (0 = none,
100 = full; only counts for PCs). The two random rolls average **−50**, so these are *expected*
modifiers (actual range is roughly −70 to +20 around the deterministic part). The "Nr" target is
read as victim total level = N. If `mod ≤ 0` the ambush does **0** damage.

| atk ↓ / target → | 9r no-awr | 9r full-awr | 15r no-awr | 15r full-awr | 24r no-awr | 24r full-awr | 30r no-awr | 30r full-awr |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| **24r** | 105 | 55 | 99 | 49 | 90 | 40 | 84 | 34 |
| **27r** | 108 | 58 | 102 | 52 | 93 | 43 | 87 | 37 |
| **30r** | 111 | 61 | 105 | 55 | 96 | 46 | 90 | 40 |
| **33r** | 114 | 64 | 108 | 58 | 99 | 49 | 93 | 43 |
| **36r** | 117 | 67 | 111 | 61 | 102 | 52 | 96 | 46 |

Full awareness costs the target ~50 modifier (the `−½·awareness` term); a tougher (higher-level)
target costs 1 per level.

### Ambush damage

`ambush_calculate_damage` (`ranger.cpp:843`):
```
dmg = (60 + mod) · min(victimHP, RangerLevel·20) / (400 + 5·(enc+leg_enc+bulk²))
    + RangerLevel − victimLevel + 10
    + (weaponDmg²/100)·LevelA/30
    [× 3/2 vs NPC or × 10/8 vs PC, if Stealth spec]
soft cap = RangerLevel·10; amounts above the cap are divided by 3.
```
**Assumptions:** healthy victim (`HP > RangerLevel·20`, so the HP term = `RangerLevel·20`), light
weapon `bulk 2` / `weaponDmg 20`, attacker unencumbered, **no** Stealth spec, `LevelA = ranger
level`. Using the expected modifiers above:

| atk ↓ / target → | 9r no-awr | 9r full-awr | 15r no-awr | 15r full-awr | 24r no-awr | 24r full-awr | 30r no-awr | 30r full-awr |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| **24r** | 216 | 159 | 203 | 146 | 184 | 127 | 171 | 114 |
| **27r** | 247 | 182 | 233 | 169 | 212 | 148 | 199 | 134 |
| **30r** | 279 | 207 | 264 | 193 | 242 | 171 | 228 | 156 |
| **33r** | 311 | 232 | 296 | 217 | 272 | 194 | 257 | 178 |
| **36r** | 344 | 258 | 328 | 242 | 303 | 218 | 287 | 201 |

Reading the numbers:
- **Ranger level is the dominant lever**: it scales the HP multiplier (`·R·20`), adds a flat
  `+R`, and raises the soft cap (`R·10`). Each +3 ranger levels here adds ~30 damage to the soft
  targets.
- **Target awareness is the strongest defense**: dropping the modifier by ~50 takes the 30r
  attacker from 228 (vs a 30r no-awareness target) to 156 (vs a 30r full-awareness target) — about
  a **32% cut**, and the same ~57-point drop separates every no-awr/full-awr column pair.
- **The soft cap (`R·10`) doesn't bite at these *expected* values** — the hottest cell, 36r vs the
  9r target, is 344 against a 360 cap. But **best-case rolls do** breach it: a `+20` modifier roll
  on that cell computes ~464 and is clamped to `360 + (464−360)/3 ≈ 394` (excess `/3`). So good
  rolls add roughly **+50–60** on the awareness columns but flatten out near `R·10 + a third` on the
  easy columns.
- **HP scaling matters against real targets.** Against a *wounded* or genuinely low-HP victim the
  `min(victimHP, R·20)` term collapses to their current HP, scaling the whole result down
  proportionally; the grid assumes targets healthy enough that `R·20` is the binding cap.
- **Stealth spec** (not applied above) would multiply every cell by `10/8` vs players (`3/2` vs
  mobs) *before* the soft cap.

> Cross-check: the in-code comment at `ranger.cpp:837` cites "modifier ~60 on average for a 30r
> with stealth vs a no-awareness opponent." That older estimate assumes lower effective
> skill/terrain than the maxed, open-ground setup here (which yields ~90–96); treat the formula as
> truth and these figures as one calibrated point.

---

## Illustrative examples: archery (shoot) damage

> **Premise correction (again): archery damage scales with STRENGTH, not DEX.** Despite the
> function's own doc-comment claiming damage is "based on the archer's ranger level, dexterity
> modifier, the arrows…", the live `shoot_calculate_damage` (`ranger.cpp:2085`) uses
> `strength_factor = (get_cur_str() − 10)·0.5` — **`get_cur_dex()` is never read** in the damage,
> accuracy, hit-location, or success path. DEX touches archery in exactly one place: a gate that
> stops NPC orc-followers with `dex < 18` from using a bow (`can_ch_shoot`, `ranger.cpp:2270`). So
> a DEX-breakpoint table would be five identical columns. The table below therefore uses
> **STRENGTH** on the same 18/20/22/24 breakpoints — that is the stat that actually moves the
> number. **(Code/comment mismatch worth a ticket: the comment promises DEX scaling the code
> doesn't implement.)**

### The formula (`ranger.cpp:2085`)
```
damage = RangerLevel·0.5·rand(0.5,1.0)                       ← "ranger level factor", E = 0.375·R
       + ( rand(0 .. (arrowDmg+bowDmg)·1.25) + (STR−10)·0.5 ) · levelMultiplier
levelMultiplier = 0.8 below 21r, then +0.02 per level (0.88 @24r, 1.0 @30r, 1.12 @36r)
```
**Assumptions for the grid:** a strong endgame setup — `bowDmg 19`, `arrowDmg 5` ⇒ random cap
`(19+5)·1.25 = 30`, so the bow-roll averages **15** (this matches the in-code "~30 absolute max"
note). Damage shown is **pre-armor**, i.e. against an unarmored hit location *or* on an accurate
shot (which ignores armor); metal/chain armor on the struck slot subtracts afterward unless the
accuracy roll — `RangerLevel·0.01 · (Accuracy/100)`, ~30 % at 30r/100-acc — connects.

### Expected per-arrow damage (pre-armor)

| atk ↓ / STR → | STR 18 | STR 20 | STR 22 | STR 24 |
|---|---:|---:|---:|---:|
| **24r** | 25 | 26 | 27 | 28 |
| **27r** | 27 | 28 | 29 | 30 |
| **30r** | 30 | 31 | 32 | 33 |
| **33r** | 32 | 33 | 34 | 35 |
| **36r** | 34 | 35 | 37 | 38 |

### Full roll range (both random terms at their extremes)

| atk ↓ / STR → | STR 18 | STR 20 | STR 22 | STR 24 |
|---|---:|---:|---:|---:|
| **24r** | 9–41 | 10–42 | 11–43 | 12–44 |
| **27r** | 10–45 | 11–46 | 12–47 | 13–48 |
| **30r** | 11–49 | 12–50 | 13–51 | 14–52 |
| **33r** | 12–52 | 13–53 | 14–54 | 15–55 |
| **36r** | 13–56 | 14–57 | 15–58 | 16–59 |

Reading the numbers:
- **Strength is a weak lever**: +2 STR is `+1·multiplier` ≈ +1 damage. Across the whole 18→24 STR
  span the expected hit moves only ~3 points — strength is a minor additive term, not a driver.
- **Ranger level does more, but gently**: each +3 levels adds `+1.1` to the ranger-level factor and
  `+0.06` to the multiplier, ~+2 expected per step. Its bigger effect on archery is elsewhere —
  faster shots (`shoot_calculate_wait`) and a higher armor-bypass chance — not raw per-arrow size.
- **Variance dominates a single arrow**: the bow roll `0..30` swings each shot far more than any
  stat choice; the spreads above (e.g. 12–50 at 30r/STR20) are mostly that uniform roll. Archery's
  damage comes from *volume and armor-bypass*, not big individual hits.
- **Armor**: against an armored slot without an accurate shot, subtract the slot's reduction
  (chain counts half vs arrows, `ranger.cpp:2042`), floored at 1.

### Damage per second (cadence)

DPS = per-arrow damage ÷ seconds-per-shot. The cadence is set by `shoot_calculate_wait`
(`ranger.cpp:2120`), in **pulses of 0.25 s** (`OPT_USEC`, `comm.cpp:45`; base 12 beats = one 3.0 s
round):
```
beats = max(4, 24 − ⌊ENE_regen/12⌋ − ⌊RangerLevel/12⌋)        (normal mode)
        − 1 more for Wood-elf / Haradrim
fast mode: beats/2 (min 3) ;  slow mode: beats·2 (min 8)       (GET_SHOOTING, ranger.cpp:2131)
seconds_per_shot = beats · 0.25
```

> **Where the speed actually comes from — and DEX sneaks back in here.** Within 24→36r the
> ranger-level term `⌊R/12⌋` only moves from 2 to 3, i.e. **one beat (0.25 s) across the whole
> range** — ranger level barely changes fire rate. The dominant input is **`ENE_regen`** (energy
> regen). And `ENE_regen` for a wielded weapon (`profs.cpp:767-799`) is built from
> `null_speed = 3·DEX + ⅔·(SKILL_ATTACK + SKILL_STEALTH/2) + 100`, a strength/weight term, and a
> `dex_speed` term scaled by bow weight & bulk. So **DEX — which does nothing to per-arrow damage —
> *does* raise DPS by shortening the interval between shots.** Exact `ENE_regen` depends on the bow's
> weight/bulk and your `SKILL_ATTACK`/`SKILL_STEALTH`, so rather than guess it the cadence is
> treated as a parameter below.

Representative cadence (`seconds(beats)`, normal mode) for a few `ENE_regen` tiers:

| ENE_regen | 24r | 27r | 30r | 33r | 36r |
|---|---|---|---|---|---|
| 60 | 4.25s | 4.25s | 4.25s | 4.25s | 4.00s |
| 120 | 3.00s | 3.00s | 3.00s | 3.00s | 2.75s |
| 180 | 1.75s | 1.75s | 1.75s | 1.75s | 1.50s |
| 240+ | 1.00s | 1.00s | 1.00s | 1.00s | 1.00s (4-beat floor) |

**DPS formula (STR floating):**
```
DPS(R, STR, t) = [ 0.375·R + (15 + 0.5·(STR−10)) · mult(R) ] / t
                 mult(R) = 0.8 + 0.02·(R−20)   ;   t = seconds_per_shot
```
(the `15` is the average bow roll for the `bowDmg 19 / arrowDmg 5` setup; swap in `cap/2` for other gear.)

**DPS @ 1.0 s/shot** (4-beat floor — a fast endgame archer, high energy regen):

| atk ↓ / STR → | STR 18 | STR 20 | STR 22 | STR 24 |
|---|---:|---:|---:|---:|
| **24r** | 25.7 | 26.6 | 27.5 | 28.4 |
| **27r** | 28.0 | 28.9 | 29.9 | 30.8 |
| **30r** | 30.2 | 31.2 | 32.2 | 33.2 |
| **33r** | 32.5 | 33.6 | 34.6 | 35.7 |
| **36r** | 34.8 | 35.9 | 37.0 | 38.1 |

**DPS @ 2.0 s/shot** (8 beats — mid energy regen):

| atk ↓ / STR → | STR 18 | STR 20 | STR 22 | STR 24 |
|---|---:|---:|---:|---:|
| **24r** | 12.9 | 13.3 | 13.7 | 14.2 |
| **27r** | 14.0 | 14.5 | 14.9 | 15.4 |
| **30r** | 15.1 | 15.6 | 16.1 | 16.6 |
| **33r** | 16.3 | 16.8 | 17.3 | 17.8 |
| **36r** | 17.4 | 18.0 | 18.5 | 19.1 |

**DPS @ 3.0 s/shot** (12 beats — base cadence, low energy regen):

| atk ↓ / STR → | STR 18 | STR 20 | STR 22 | STR 24 |
|---|---:|---:|---:|---:|
| **24r** | 8.6 | 8.9 | 9.2 | 9.5 |
| **27r** | 9.3 | 9.6 | 10.0 | 10.3 |
| **30r** | 10.1 | 10.4 | 10.8 | 11.1 |
| **33r** | 10.8 | 11.2 | 11.5 | 11.9 |
| **36r** | 11.6 | 12.0 | 12.3 | 12.7 |

Reading the DPS numbers:
- **Cadence dwarfs every stat.** Going from 3.0 s to 1.0 s per shot **triples** DPS — a far bigger
  swing than the entire 24→36r or 18→24 STR span (each worth only a few points of per-arrow
  damage). Energy regen (hence DEX, bow weight, `SKILL_ATTACK`/`STEALTH`) and the fast/slow toggle
  are the real DPS levers.
- **Strength is marginal for DPS**: ~+0.5–1.0 DPS per +2 STR at a 1 s cadence, less when slower.
- **Fast/slow mode is ~DPS-neutral** — it trades hit size for frequency, *not* total output (see
  next). The tables above are **normal mode**.
- These are **per-arrow, pre-armor, single-target** DPS; armored slots without an accurate shot
  reduce the numerator, and arrows can break/deplete (`does_arrow_break`, `ranger.cpp:2147`).

### Fast vs. slow shooting (`GET_SHOOTING`)

`GET_SHOOTING` changes **both** the per-shot damage and the cadence, in opposite directions:

| Mode | Per-shot damage (`ranger.cpp:2398`) | Cadence (`ranger.cpp:2131`) | Net DPS |
|------|-------------------------------------|------------------------------|---------|
| **slow** | **×2** | beats **×2** (min 8) | ≈ unchanged |
| normal | ×1 | beats ×1 (min 4) | baseline |
| **fast** | **÷2** | beats **÷2** (min 3) | ≈ unchanged (slightly *worse* at the floor) |

So **yes — shooting slowly literally doubles the damage of each arrow** (the multiply happens in
`on_arrow_hit` *after* armor is applied). But because it also doubles the time between shots, your
**damage per second is essentially the same** as normal; slow shooting does not raise sustained
output. (At the fast end the floors don't line up — fast bottoms out at 3 beats / 0.75 s while
normal floors at 4 beats / 1.0 s — so a maxed-cadence archer actually *loses* DPS in fast mode,
~0.67× baseline, since damage is halved but the interval isn't quite.)

**Real benefits of shooting slowly** (bigger hits at equal DPS):
- **Arrow economy** — half as many shots for the same total damage means **half the arrows spent**
  and half the metal/chain break checks (`does_arrow_break`, `ranger.cpp:2150`). Arrows are a
  foraged/bought consumable, so this is the most concrete win.
- **Burst per shot** — one large hit races a target's regen/heals/flee better than the same damage
  dribbled out, and when an **accurate shot** (armor bypass, `check_archery_accuracy`) lands it's a
  doubled armor-ignoring hit.
- **Fewer actions / less command spam.**

What slow does **not** buy you: no extra DPS, and **no armor efficiency** — armor is subtracted from
the 1× damage and the result is then doubled, which is arithmetically identical to two normal shots,
so flat + percentage mitigation scale right along with the damage.

**Benefits of shooting fast** (the mirror): more independent **accuracy / accurate-shot rolls** and
more on-target events per unit time, and finer control to retarget — at the cost of **more arrows
consumed**, more break risk, and the floor-induced DPS dip above.

### Getting hit interrupts the shot (full cancel)

A shot is a two-phase command: `shoot` arms a wait-wheel delay (`AFF_WAITING | AFF_WAITWHEEL`,
priority **30**), and the arrow only looses when the delay matures. **Taking any damage during that
wind-up cancels the shot outright** — it does not merely slow it. The chain (`fight.cpp:1733`):
```
if (dam > 0 && IS_AFFECTED(victim, AFF_WAITWHEEL) && GET_WAIT_PRIORITY(victim) <= 40)
    if (battle_mage_handler.does_spell_get_interrupted())   // returns TRUE for any non-battle-mage
        break_spell(victim);                                // delay.wait_value = 0; delay.subcmd = −1
```
`break_spell` forces the queued command to run with `subcmd −1`, and `do_shoot`'s abort branch
(`ranger.cpp:2521`) prints *"You could not concentrate on shooting anymore!"*, resets the swing
timer (`ENERGY = min(ENERGY, 0)`), and returns **without firing**. You lose the whole wind-up and
must re-issue `shoot`. Notes:
- **Only damaging hits interrupt** (`dam > 0`). A miss, dodge, parry, or fully-absorbed hit deals 0
  and leaves the shot intact.
- **Priority gate ≤ 40**, and shoot is 30, so it's always in the interruptible band.
- **Rangers have no resistance to it.** Despite the `break_spell` / `does_spell_get_interrupted`
  naming, this is the generic wait-wheel interrupt; the only mitigation is the **Battle-Mage** spec's
  resist roll (`is_battle_spec`), which a ranger never has — so for a ranger the interrupt is
  effectively automatic on any damage taken.
- Practical upshot: **archery is a poor toe-to-toe trade** — you want to shoot before melee is
  joined, from a pet/ally screen, or while the target can't hit back. A slow shot's longer wind-up
  is also a *larger* interrupt window, an extra hidden cost of slow mode.

---

## Cross-references
- Melee resolution and how OB/PB/DB are consumed: [combat-loop.md](combat-loop.md).
- Full OB/PB/DB derivation and level/proficiency model: [stats-and-character-power.md](stats-and-character-power.md) §10.
- Light Fighting / Stealth / Pets spec mechanics: [specializations.md](specializations.md).
- Warrior-side skills that share the table/handlers: [warrior-skills.md](warrior-skills.md).
