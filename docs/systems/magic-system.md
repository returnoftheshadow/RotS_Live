# Magic system (mage spells)

**Source files:** spell effects `mage.cpp` (all `ASPELL(...)` functions); cast pipeline
`spell_pa.cpp::do_cast`/`do_prepare`; saving throws `spell_pa.cpp:169-248`
(`saves_spell`, `new_saves_spell`, `get_character_saving_throw`, `get_saving_throw_dc`); damage
application `mage.cpp:98-112` (`apply_spell_damage`) → live `damage()` in `fight.cpp:1588`;
spell table `consts.cpp:489-572`; spell ids `spells.h:118-155`; Battle-Mage handler
`battle_mage_handler.cpp`; element resistances `utility.cpp:1792` (`check_resistances`).
**Status:** ✅ mage offensive + utility spells, scaling, saves, resistance, penetration; **mana
regen** (§12). Cleric/mystic powers live in [cleric-mystic-system.md](cleric-mystic-system.md).

> **Live path note.** Unlike melee (where `combat_manager.cpp` is dead — see AGENTS.md), the mage
> code in `mage.cpp`/`spell_pa.cpp` *is* the live path. Spell damage is finalized by the **same
> live `damage()`** used by melee (combat-loop §3), so the "no armor on spells" and
> resistance/cap rules below are the real ones.

## How to read this doc
Every offensive mage spell runs the same four-stage pipeline. Get these four quantities and
stages straight and every spell below falls out of them:

1. **Caster level** `L` (`get_mage_caster_level`, `mage.cpp:30`) — the caster's *effective* mage
   level, used by utility spells and a few damage floors.
2. **Magic power** `P` (`get_magic_power`, `mage.cpp:43`) — the **spellpower** number that scales
   almost all damage rolls. Bigger `P` = bigger dice.
3. **Save stage** (`new_saves_spell`, `spell_pa.cpp:228`) — a **d20 binary check**: did the victim
   "save"? A save usually **halves** (sometimes ×⅔, ×⅓) the rolled damage *inside the spell
   function*. This is the **spell-penetration vs. saving-throw** contest.
4. **Resistance/mitigation stage** (`apply_spell_damage`, `mage.cpp:98`) — a **continuous damage
   multiplier** from the victim's `saving_throw` *stat* (gear/buffs), softened by the caster's
   *innate* spell penetration. Then `damage()` applies **elemental resistance** (×⅔ / ×3⁄2) and
   the global cap.

Throughout: `number(a,b)` is a uniform random **integer** in `[a,b]`; `number()` with no args is a
**double in `[0,1)`**. "Typical" figures assume `P ≈ 120` (a well-geared level-30 mage) and no
resistance — read the formula as truth, the numbers as feel. (LoL-style notation: *scaling tags
in italics*.)

---

## 1. The four scaling quantities

### Effective mage level `L` — `get_mage_caster_level` (`mage.cpp:30`)
```
L = prof_level(MAGE) + intel/5            (+1 random chance for the intel % 5 remainder)
```
So a 30-mage with 90 INT casts as `L ≈ 48`. Used directly by utility spells (`cure self`,
`vitalize self`, `relocate` range, `reveal`, `earthquake`/`blaze`/`mist` damage & duration) and
as a damage **floor** for Fire-spec firebolt.

### Magic power (spellpower) `P` — `get_magic_power` (`mage.cpp:43`)
```
P = prof_level(MAGE) + 2·(intel/5) + spell_power_stat + (max_race_mage_level · LEVELA / 30)
                                      └─ +Battle-Mage bonus (see §6)
```
- **`spell_power_stat`** = `points.spell_power`, granted by **gear** (`APPLY_SPELL_POW`,
  `handler.cpp:375`) — there is no innate/level growth; spellpower is an **itemization stat**.
- The `max_race_mage_level · LEVELA / 30` term is the dominant scaler: it ramps `P` with overall
  character level (`LEVELA`) gated by the race's mage cap. This is the mid-level "mages are weak
  offensively until they level" lever referenced all over the code comments.
- **INT is counted twice** here (once inside `L`, once again as `intel/5`) — intentional or not,
  it makes Intelligence a strong spellpower stat. Flag in Open questions.

### Innate spell penetration `mage/5` — `get_spell_pen_value` (`mage.cpp:70`)
```
spell_pen = prof_level(MAGE) / 5.0        (+ master's mage/3 /5 if a charmed NPC)
```
Used **only** in the resistance stage (§4). **Distinct from** the `spell_pen` *stat* below.

### Spell-penetration stat `points.spell_pen` — gear/PK
Granted by **gear** (`APPLY_SPELL_PEN`, `handler.cpp:371`) and the **PK-fame mage bonus**
(`assign_pk_mage_bonus`, `limits.cpp`: +3/+2/+1 at tiers 1/2/3). Used **only** in the save-DC
(§3). ⚠️ Do not confuse the two penetrations — see the table in §5.

---

## 2. The cast pipeline — `do_cast` (`spell_pa.cpp:524`)

1. **Select spell** by name (between `'...'`) or pre-parsed target; validate prof, race, and
   Big-Brother PK rules (`can_cast_spell`, `spell_pa.cpp:410`).
2. **Casting time** (`CASTING_TIME`, `spells.h:333`):
   ```
   beats_to_cast = (spell.beats · 30) / (30 + mage_level)
   ```
   Shrinks as you out-level the spell. **Fast casting** halves it, **slow casting** ×1.5
   (`GET_CASTING`, `spell_pa.cpp:727`). The caster enters `AFF_WAITING` for that many beats, then
   `do_cast` is re-entered to actually fire.
3. **Concentration / fail check** (`spell_pa.cpp:900`): roll `number(0,100) ≥ effective_knowledge`
   ⇒ **lose concentration**, spend **½ mana**, no spell. `effective_knowledge` =
   `GET_KNOWLEDGE` minus encumbrance penalty (`encumbrance/3 − 1`, ~10 % at max load) minus Power
   of Arda. (Encumbrance only bites a Battle-Mage probabilistically — §6.)
4. **Mana cost** (`USE_MANA`, `spells.h:328`):
   ```
   mana = max(spell.min_usesmana, 120 / (3 + max(-1, mage_level − spell_level)) − (REDUCED_MANA?5:0))
   ```
   Cost **falls** as you out-level the spell, floored at the spell's `min_usesmana`. Fast casting
   ×3⁄2 mana, slow ×½.
5. **Fire** the spell's `ASPELL` function.

**Prepare** (`do_prepare`, `spell_pa.cpp:997`): pre-cast a mage spell so it can be released
instantly later (skips the cast-time wait, costs a small after-lag). Gated by
`GET_KNOWLEDGE ≥ number(1,120)`. **Battle-Mages cannot prepare** (`can_prepare_spell`).

---

## 3. Save stage — the penetration vs. saving-throw contest

The **live** offensive save is `new_saves_spell` (`spell_pa.cpp:228`). It is a flat **d20**:
```
victim_save = get_character_saving_throw(victim) + save_bonus
caster_DC   = get_saving_throw_dc(caster)
saved       = ( number(1,20) + victim_save )  >  caster_DC
```

**Victim's saving throw** (`get_character_saving_throw`, `spell_pa.cpp:193`) — *derived from level
and INT, NOT from gear*:
```
victim_save = mage_level(victim)/3 + (INT − 8)/4 + (hobbit ? 1 : 0)
              └─ NPCs count as only ⅔ of their mage level
```

**Caster's DC** (`get_saving_throw_dc`, `spell_pa.cpp:216`):
```
caster_DC = 10 + mage_level(caster)/3 + (INT − 8)/4 + spell_pen_stat(+Battle-Mage bonus)
```

So the contest is **caster mage-level + INT + spell-pen gear vs. victim mage-level + INT** on a
d20. Each net point is a 5 % swing. `save_bonus` (passed per-spell) tilts it:

- **`save_bonus ≤ −20` ⇒ guaranteed *not* saved** (unsaveable spells — Spear of Darkness uses
  `−20` and calls `damage()` directly anyway).
- **`save_bonus ≥ +20` ⇒ guaranteed save.**
- Otherwise it just adds to `victim_save` (positive = easier for the victim to resist).

**Effect of a save** is decided *inside each spell* — almost always **damage ÷ 2** (a few use ×⅔
or, for Searing Darkness's fire half, ×⅓). It is **not** all-or-nothing.

> **Legacy save (`saves_spell`, `spell_pa.cpp:169`)** — the older formula
> `GET_SAVE + LEVELA − caster_level + INT/5 (+hobbit) > number(1,20)`. Still used by a **few**
> spells (`word of shock`, summon/reveal helpers). It keys off the **`GET_SAVE` gear stat**, unlike
> `new_saves_spell`. New offensive spells use `new_saves_spell`.

### Specialization save matrix — `get_save_bonus` (`mage.cpp:1304`)
Each elemental spell passes a `save_bonus` from this matrix (negative = caster-favored):

| Situation | `save_bonus` |
|---|---|
| Caster is the spell's **primary** spec, or **Arcane** | **−2** (harder to save) |
| Caster is the spell's **opposing** spec | **+2** |
| Victim is the spell's **primary** spec | **+2** (easier to save) |
| Victim is **opposing** spec or **Arcane** | **−2** |

Opposing pairs: **Fire↔Cold**, **Lightning↔Darkness**. (Chill Ray additionally hard-codes an extra
**−4** for a Cold-spec caster, `mage.cpp:1379`.)

---

## 4. Resistance / mitigation stage — `apply_spell_damage` (`mage.cpp:98`)

After the save stage sets `dam`, **every** damage spell funnels through `apply_spell_damage`, which
applies a **continuous multiplier** from the victim's **`saving_throw` stat** (`GET_SAVE` =
`specials2.saving_throw`, sourced from **gear and buff spells** like cleric *resist magic*), then
calls the live `damage()`.

```
save = victim.saving_throw_stat
if caster applies penetration (PC, or a PC's charmed orc-friend):
    save −= mage_level/5                       # innate spell penetration (§1)
    if victim is a PC:  save += LEVELA(victim)/5   # PCs get innate level-based DR

multiplier = 1                       if save == 0
           = 20 / (20 + save)        if save  > 0      # diminishing returns
           = 2 − 20 / (20 − save)    if save  < 0      # vulnerability, capped approaching ×2
dam = dam · multiplier
```

This is RotS's **"magic resistance"**: e.g. `save = 20` → ×0.50 damage; `save = 10` → ×0.67;
`save = 40` → ×0.33; a *negative* save (debuffed) ramps damage up toward ×2. The caster's
**innate penetration (`mage/5`)** directly cancels resistance points; **higher-level PC victims**
shrug off low-level casters (`+LEVELA/5`).

### Final `damage()` finalization (`fight.cpp:1588`, shared with melee)
1. **Elemental resistance** (`check_resistances`, `utility.cpp:1792`): matches the spell's element
   (`skills[spell].skill_spec`) against the victim's resist/vuln flags → **×⅔ if resistant,
   ×3⁄2 if vulnerable**, else ×1. (Untyped spells — `PLRSPEC_NONE` — are never elementally
   resisted.)
2. **Spells ignore armor.** Armor is subtracted only inside `hit()` for weapon swings
   (combat-loop §3); `apply_spell_damage`→`damage()` never touches it. So §3/§4 multipliers land on
   nearly the raw roll.
3. **Global cap 200** per hit (`dam = min(dam, 200)`), then `max(dam, 0)`.
4. PK-fame bonus vs. ranked players still applies (combat-loop).
5. The "Seether's shield" mana-soak block in `damage()` is **commented out** (dead) — `spell_shield`
   currently only sets `AFF_SHIELD`.

---

## 5. The two "penetration" and two "saving throw" values — don't conflate

This is the single biggest trap in the magic code: **two different quantities share each name.**

| Name in code | What it is | Where used | Scales with |
|---|---|---|---|
| `get_character_saving_throw` | victim's **derived** save | §3 d20 save check | victim mage level + INT |
| `GET_SAVE` / `saving_throw` *stat* | victim's **gear/buff** save | §4 damage multiplier (+ legacy `saves_spell`) | gear, *resist magic* |
| `points.spell_pen` *stat* | caster **gear/PK** penetration | §3 caster **DC** | gear, PK-fame |
| `get_spell_pen_value` (`mage/5`) | caster **innate** penetration | §4 (cancels victim's save stat) | caster mage level |

A high-`saving_throw`-stat target is hard to *damage* (§4) but no harder to *save-or-not* (§3),
and vice-versa. Gear that grants "spell penetration" helps you land the save (§3); your mage level
helps you cut through resistance (§4).

---

## 6. Battle-Mage — the melee/caster hybrid (`battle_mage_handler.cpp`)

`PS_BattleMage` (`PLRSPEC_BTLEMS`) trades casting consistency for fighting while casting. All
bonuses scale with **tactics** (aggression) and mage/warrior level:

- **+spellpower and +spell-pen stat** while aggressive:
  `bonus = tactics/2 + mage_level/12` added to both `spell_power` and `spell_pen`
  (`get_bonus_spell_power`/`get_bonus_spell_pen`). Feeds §1/§3.
- **Cannot prepare spells** (`can_prepare_spell` → false).
- **Resists interruption while ≥ Aggressive tactics.** Taking damage (`fight.cpp:1735`), mental
  attacks, or wearing armor each rolls against a chance built from
  `base_chance + warrior/100 + mage/100 + tactics·2/100` — non-Battle-Mages are interrupted/penalized
  unconditionally (`does_spell_get_interrupted`, `does_mental_attack_interrupt_spell`,
  `does_armor_fail_spell`). So a Battle-Mage can cast through melee that would break a normal mage.

Elemental/Arcane spec mechanics (Fire/Cold/Lightning/Darkness/Arcane) are summarized in §8 and in
[specializations.md](specializations.md) (mage stubs). Their `*_spec_data` structs
(`structs.h:1285-1379`) mostly **track statistics** (chill counts, energy sapped) for display — the
**gameplay** effects live in the spell functions and are listed per-spell below.

---

## 7. Offensive spell catalog

Damage uses `P` = magic power (§1), `L` = caster level (§1). All "save" effects are the §3 binary
check; all damage then passes §4. **Element** drives `check_resistances` (§4.1).

| Spell (`id`) | Element | Base damage roll | On save | Spec bonus |
|---|---|---|---|---|
| **Magic Missile** (71) | none | `12 + rand(1, P/6)` | ÷2 | — |
| **Chill Ray** (75) | Cold | `20 + rand(1,P)/2` | ÷2 | Cold: **−4 save** + applies **Chilled** |
| **Lightning Bolt** (78) | Lightning | `25 + rand(0,P)/2`, **+`4+rand(0,P)/4` if outdoors/Lght-spec** | ÷2 | Lightning: **+10 %**, ignores indoor penalty |
| **Dark Bolt** (84) | Darkness | `25 + rand(0,P)/2`, **+`4+rand(0,P)/4` if no sun penalty** | ÷2 | Darkness: **+10 %** |
| **Firebolt** (91) | Fire | `rand(1,65)+P/4+P/4+P/8+P/8+P/16+P/16` (each its own roll) | ÷2 | Fire: damage **floored at `L`** |
| **Cone of Cold** (93) | Cold | `25 + rand(1,P)/2 + P/4` | ×⅔ | Cold: applies **Chilled** on hit |
| **Fireball** (96) | Fire | `30 + 3·(rand(1,P)/2)` | ×⅔ | Fire: spares **friendly** splash targets |
| **Searing Darkness** (98) | Dark+Fire | `dark(15+rand/2 +sun bonus) + fire(15+rand/2)` | fire half ×⅓ | Dark **+10 %** dark / Fire **+50 %** fire |
| **Lightning Strike** (99) | Lightning | `40 + rand(0,P) + rand(0,P)/2` | ×⅔ | Lght: cast **without storm** at ×⅘ |
| **Spear of Darkness** (105) | Darkness | `30 + rand(8,P)/2 + rand(8,P)/2 + rand(8,P)/2 + rand(0,P)/5` (drop the `30+` term in sun) | **unsaveable** | Darkness: **+5 %** |
| **Earthquake** (81) | none | `rand(1,30) + L` (÷2 if it cracks ground); AoE | ÷2 | — (can open a chasm + knockdown) |
| **Word of Pain** (100) | none | `12 + rand(1, P/6)` | ÷2 | Uruk analog of Magic Missile |
| **Leach** (106) | none | `18 + rand(1, P/4)` | ÷2 | on hit: **drain moves + heal caster ½ dmg** |
| **Word of Agony** (102) | none | `20 + 2·(rand(1,P)/2)` | ×⅔ (victim **−2 save**) | applies **Chilled** on hit |
| **Shout of Pain** (103) | none | `rand(1,50) + P/2`; AoE | ÷2 | Uruk analog of Earthquake |
| **Black Arrow** (107) | Darkness | `13 + 2·(rand(1,P)/2)`, **+`rand(0,P/6)+2` if no sun/Dark-spec** | ÷2 | on hit: chance (`rand(1,50)<L`) to **poison** |

**Chilled effect** (`apply_chilled_effect`, `mage.cpp:1348`): drains
`energy/2 + energy_regen·4` from the victim — this is what lets a mage "perma-freeze" a mob's
action economy (Chill Ray / Cone of Cold (Cold-spec) / Word of Agony).

**Fireball splash** (`mage.cpp:1846`): everyone else in the room is rolled `number()` vs **0.2**
(**0.8** if fighting the caster); hit splash takes `fireball/5` (`/3` if fighting), itself
save-halved. Orc casters take **−5** damage and a **10 %** chance to hit themselves for ⅓.

**Sun penalty** (`SUN_PENALTY`, `utils.h:425`): Uruk/Orc/Olog/Magus lose the bonus term on
Dark Bolt / Black Arrow / Searing Darkness / Spear in daylight outdoors.

---

## 8. Utility & non-damage spells (brief)

`mage.cpp` also implements: **Create Light** (loads obj 7006), **Locate Living** /
**Reveal Life** / **Word of Sight** (room/area scans vs. hide, scaling with `L`), **Cure Self**
(`L/2 + 10` HP, **+5 Regen-spec**), **Vitalize Self** (`2L` move, **+10 Regen-spec**),
**Shield** (`AFF_SHIELD`, absorb `L·5 %`, **+5 levels Protection-spec**), **Flash** /
**Word of Shock** (AoE disengage + energy burn; Flash grants darkies *Power of Arda* malus),
**Summon / Blink / Relocate / Beacon** (teleportation; Tele-spec extends range — `dist 5 vs 3`
blink, +1 zone relocate), **Identify**, **Detect Evil**, **Expose Elements** (spec-only: marks a
mob so the spec's signature spell is **free/discounted** next cast — `spell_expose_elements`,
`mage.cpp:2411`), and the room-affect spells **Blaze** / **Mist of Baazunga** (several flagged
"needs to be removed" in `spells.h`).

---

## 9. How everything scales (summary)

- **Mage level** raises: `L` and `P` (§1), the save **DC** (§3), innate **penetration** `mage/5`
  (§4), shrinks **cast time** and **mana** (§2). It is the master stat.
- **Spellpower (`spell_power` gear stat)** raises **`P`** only → bigger damage dice on nearly every
  offensive spell. Pure itemization; no level growth.
- **Specialization** (§3 matrix, §7 table): primary-spec / Arcane casters land saves **−2** easier
  and get a per-spell damage/effect kicker (+10 % bolts, Fire firebolt floor & friendly-fire
  immunity, Cold chill application & −4 Chill-Ray save, etc.). Battle-Mage trades prep for combat
  casting + tactics-scaled power/pen (§6).
- **Intelligence** raises `L`, `P` (twice), and both sides of the d20 save — strong all-round mage
  stat.
- **Spell-pen gear / PK-fame** raises the save **DC** (§3) only.

---

## 10. Worked example

A level-30 mage, **90 INT**, **+40 spell_power gear**, **+2 spell_pen gear**, human (race mage cap
30), `LEVELA` 30, casting **Fireball** at an unspecialized mob with **0** `saving_throw` stat and
**20 INT**, no element resistance:

- `L = 30 + 90/5 = 48`.
- `P = 30 + 2·18 + 40 + (30·30/30) = 30 + 36 + 40 + 30 = 136`.
- Damage roll: `30 + 3·rand(1,136)/2` → average `30 + 3·34 ≈ 132`.
- Save (§3): victim_save `= 30/3·(⅔ npc)=6 + (20−8)/4=3 = 9`; caster_DC `= 10 + 30/3 + (90−8)/4 +
  2 = 10+10+20+2 = 42`. `saved = d20 + 9 > 42` → needs a 34+ → **never saves**. (Against a
  *player* mage of equal level the contest is far closer.)
- Mitigation (§4): `save_stat = 0 − 48/5 = −9` (caster penetration, victim is an NPC so no `+L/5`)
  → multiplier `2 − 20/(20−(−9)) = 2 − 0.69 = ×1.31`. So `132 → ~173`.
- `damage()`: no element resist, under the 200 cap → **~173 to the mob** (no armor step).

Swap the target for an equal-level **player** with a **+20 saving_throw** suit: §3 stays unsaveable
only if the caster out-levels them, and §4 becomes `save = 20 − 48/5 + 30/5 = 20 − 9 + 6 = 17` →
×`20/37 = 0.54`, roughly halving the hit. That's the magic-resistance gear at work.

---

## 11. Open questions / flags for maintainers

- **INT double-count in `get_magic_power`** (`mage.cpp:43`) — `intel/5` is added both inside `L`
  and again as `intel_factor`. Intentional (INT = strong spellpower stat) or a copy-paste bug?
- **Two `saving_throw`s, two `spell_pen`s** (§5) — the naming collision is a live footgun; a rewrite
  should rename (`derived_save` vs `resist_save_stat`; `pen_stat` vs `innate_pen`).
- **`saves_spell` (legacy) vs `new_saves_spell`** coexist; `word of shock` and the summon/reveal
  helpers still use the legacy formula keyed off `GET_SAVE`. Decide on one save model.
- Several spells are tagged **"needs to be removed"** in `spells.h` (Freeze, Mist, Blaze, Shift) —
  Blaze/Mist still have working `ASPELL` bodies.
- `spell_shield`'s mana-soak ("Seether's shield") is **commented out** in `damage()`; the in-game
  shield only sets `AFF_SHIELD` with no `damage()` interaction — confirm intended.
- Earthquake's fall logic has an operator-precedence quirk: `!saved && (tmpch != caster) ||
  (!number(0,1))` (`mage.cpp:1694`) — the `||` makes ~50 % of everyone (incl. possibly the caster)
  fall regardless of save. Verify intent.
- `get_magic_power` uses `GET_MAX_RACE_PROF_LEVEL(PROF_MAGE,...)`, so a race's mage **cap** (not the
  character's current mage level) drives the `LEVELA` term — confirm this is the intended "level
  ramp" lever.

---

## 12. Mana regeneration

Mana is the **mage** resource (`GET_MANA` = `tmpabilities.mana`); cleric/mystic powers spend
**spirit** instead (cleric-mystic-system §1). Mana regenerates on a timer via `mana_gain`
(`limits.cpp:128`), applied each `fast_update` tick.

**Per-MUD-hour gain, PCs** (`limits.cpp:131-170`):
```
gain = 8 + INT/2 + WIL/5 + prof_level(MAGE)/5 + prof_level(CLERIC)/5
gain ×= position:  sleeping ×2 · resting ×1.5 · sitting ×1.25 · (standing/fighting ×1)
gain ×= 0.25  if poisoned
gain ×= 0.25  if starving (FULL == 0) or parched (THIRST == 0)
gain  = adjust_regen_for_level(level, gain)        # level ≤ 10 only: × (2 − 0.1·level); no effect past 10
gain += points.mana_regen                          # flat bonus from gear (APPLY_MANA_REGEN)
```

### Mystic level *does* feed mana regen
Note the **two** profession terms: **`prof_level(MAGE)/5 + prof_level(CLERIC)/5`**. Mage *and*
mystic levels each contribute `level/5` mana per hour — so a character's **mystic level raises
their mana regen** exactly as much as an equal mage level would. Because both default builds carry
**45 total profession levels** (30 + 15), they get the **same** `45/5 = 9` mana/hour from
professions:

| Build | `MAGE/5` | `CLERIC/5` | prof mana/hr | `INT/2 + WIL/5` tilt |
|---|---|---|---|---|
| **Default mage** (30 mage / 15 mystic) | 6 | 3 | **9** | INT-heavy (INT/2 dominates) |
| **Default mystic** (30 mystic / 15 mage) | 3 | 6 | **9** | WIL-heavy, but WIL only /5 |

The profession contribution is identical; the practical difference is the **stat tilt** — a mage's
high INT (`INT/2`) regenerates noticeably more mana than a mystic's high WIL (`WIL/5`), which fits
the mage being the mana-dependent class and the mystic leaning on the (kill-fed) spirit pool.

### Timing — the per-hour number is per real minute
`fast_update` adds `mana_gain / FAST_UPDATE_RATE` each tick, with fractional **probabilistic
rounding** (`limits.cpp:1502-1505`), so over a full MUD hour the integer total equals `mana_gain`.
A **MUD hour is `SECS_PER_MUD_HOUR = 60` real seconds** (`structs.h:95`), so the per-hour figure is
effectively **mana per real minute**. NPCs use a flat `level`-based gain (×1.5 out of combat).
Regen can be **negative** (e.g. *restlessness*-style effects on the move/health channels) and the
same machinery can kill a character at negative health regen.

