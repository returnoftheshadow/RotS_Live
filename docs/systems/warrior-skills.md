# Warrior skills

**Source files:** skill table `consts.cpp:382-403,459,573-623`; `SKILL_*` ids `spells.h:40-75`;
handlers in `act_offe.cpp` (kick/bash/bite/rend/maul/berserk), `fight.cpp` (find-weakness,
natural attacks, frenzy hooks), `olog_hai.cpp` (smash/cleave/overrun/frenzy),
`wild_fighting_handler.cpp` (wild-swing multiplier). Damage is applied through the **live**
`damage()` in `fight.cpp` (not the unused `combat_manager`).
**Status:** ✅ active/passive warrior skills; race-gated combat skills included with their gate.

## How to read this doc
Most active warrior skills share two building blocks:

- **Hit margin `M`** (`get_prob_skill`, `act_offe.cpp:793`):
  ```
  M = your_skill% − ½·target_DB − ½·target_PB + ½·your_OB + 1d100 − 120
  ```
  `M` is rolled per use. **`M < 0` ⇒ the skill misses (0 damage).** It captures both accuracy
  and quality: the more your skill and OB beat the target's dodge/parry, the higher `M`.
- **Standard skill damage `S(M)`** (`act_offe.cpp:903`):
  ```
  S(M) = (2 + WarriorLevel) · (100 + M) / 250      physical damage
  ```
  Heavy Fighting adds **+20 %** to the result.

Damage is applied via `damage()` (`fight.cpp:1588`), which — importantly — **does not apply
armor.** Armor (`armor_effect`) is subtracted only for ordinary weapon swings, inside `hit()`
*before* it calls `damage()` (combat-loop §3). **Every skill below calls `damage()` directly, so
skill damage ignores armor entirely** (the same is true of spells). It *is* still modified
inside `damage()` by:
- **Resistances / vulnerabilities** (`check_resistances`): **× ⅔ if the target resists, × 3⁄2 if
  vulnerable** to that damage type;
- **Beorning natural / maul** damage reduction (physical only); and
- the **PK-fame** bonus vs. ranked players.

So the figures below are close to the *actual* hit (no armor step), not "pre-armor." Throughout,
**"typical" ranges assume**: your Warrior level 30, the skill practiced to 100, your OB ≈ 150, a
target with DB 40 / PB 60 (so `M ≈ 6–105`, avg ~55), Normal tactics, and no resistance. They
scale up with your OB/level/skill and down against tougher defenses — read the formula as the
source of truth, the numbers as a feel. (LoL-style notation: **physical**, *scaling tags in
italics*.)

> **Recovery timing.** Several skills set a recovery via
> `WAIT_STATE_FULL(ch, PULSE_VIOLENCE·4/3 + number(0, PULSE_VIOLENCE), …)` — that's
> `16 + number(0,12)` pulses = **16–28 pulses**, and at ¼ s/pulse that's a **fixed 4.0 s plus a
> uniform-random 0–3.0 s = 4.0–7.0 s** before you can act again. So yes, the upper end is random
> (a 0–3 s jitter on top of the 4 s floor), not fixed.

---

## Weapon proficiencies (passive)
`barehanded, slashing, concussion (blunt), whips/flails, piercing, spears, axes, two-handed,
natural attacks, weapon mastery` (`consts.cpp:382-392`). These deal **no flat damage of their
own** — they govern how well you use the matching weapon: the relevant proficiency
(`knowledge`) feeds your **OB** and **PB** (stats §10) and gates the weapon's own damage rating
(combat-loop "weapon damage"). Practical effect per point: higher OB → bigger `remaining_OB` →
more auto-attack damage and fewer misses, plus higher parry. `two-handed` also adds the big
2H OB term and improves 2H parry; `weapon mastery` is the prerequisite tier for the
Weapon-Master specialization (specializations.md). **Natural attacks** sets your barehanded
damage — see below.

**Barehanded / natural attacks** (`natural_attack_dam`, `fight.cpp:2339`):
```
unarmed dmg = Level/3 + STR + WarriorLevel        (if the Natural Attack skill is known)
            × 0.50  (×0.75 Wild Fighting) if Level > 11 and not Light Fighting
```
*Scales: Level, STR, Warrior level.* Without the skill it's a flat `BAREHANDED_DAMAGE`.

---

## Passive / utility skills
- **Parry** (`SKILL_PARRY`) — feeds **PB** (stats §10); your active defense with a weapon.
- **Find weakness** (`SKILL_EXTRA_DAMAGE`, `check_find_weakness` `fight.cpp:2051`) — passive:
  each auto-attack has a chance to deal **×1.5 damage**. Chance = `skill/3 · WarriorLevel/30`
  (+`WarriorLevel−30` above class level 30). Maxed: **26 % @ W24 → 33 % @ W30 → 45 % @ W36**
  (full analysis in stats §10).
- **Rescue** (`SKILL_RESCUE`) — pull an ally's attacker onto yourself; **Defenders rescue with
  no recovery delay** (specializations.md).
- **Block exit** (`SKILL_BLOCK`) — stop targets from leaving the room.
- **Leadership** (`SKILL_WEAPONS`/leadership entry) — group/follower utility (no direct damage).
- **Berserk** (`SKILL_BERSERK`) — a **tactic/stance**, not an attack (set via `tactics`). While
  Berserk: OB gains `ob_bonus/16 + 5 + berserk_skill/8` (`utility.cpp:725`), you **can't flee**,
  parry/dodge are gutted (stats §10 tactics table), and it's the gateway to Wild-Fighting rage.
  The skill % gates how reliably you can *leave* the stance.

---

## Active attack skills (core)

### Kick — `SKILL_KICK`
A free off-hand strike. **`S(M)` physical** · *scales: Warrior level, Kick skill, OB; reduced
by target DB/PB* · **+20 % Heavy Fighting**.
- **Typical: ≈ 14–26** (avg ~20) at the reference build; ≈ 16–31 with Heavy Fighting.
- Cost: free. Recovery: **4.0 s + 0–3.0 s random** (see *Recovery timing* above; `act_offe.cpp:924`).

### Bash — `SKILL_BASH`
A shield/body slam that **knocks the target down** rather than dealing damage.
- **Damage: 1** (fixed) — the payload is the **knockdown**: `AFF_BASH` + can't-act for
  `PULSE_VIOLENCE·3/2 + 0–PULSE_VIOLENCE/2` ≈ **~4.5–6 s** (`act_offe.cpp:569`).
- Land chance (`act_offe.cpp:554`): `skill + 1.5·OB − DB − ½·PB − 3·targetLevel + WarriorLevel
  + d(35+OB/4) + d(−40..40) − 160`. **Auto-fails while you're in frenzy.**
- *Scales: skill, OB (×1.5), Warrior level; resisted by target level, DB, PB.* Free; sets up
  follow-ups like **rend**.

### Wild swing — `SKILL_SWING`  *(spec: Wild Fighting)*
A big telegraphed swing — **kick's formula × 1.5**, and **× 1.33 more** when in **Berserk at
≤ 25 % HP** (`wild_fighting_handler.cpp:109`).
- **`1.5 · S(M)` physical** (× 1.33 enraged) · **+20 % Heavy Fighting** · *scales as kick*.
- **Typical: ≈ 20–39** (avg ~30); up to **≈ 52** when enraged. Cost: free; **4.0 s + 0–3.0 s random** recovery.
- Gate: **Wild Fighting specialization** (`PLRSPEC_WILD`).

---

## Buffs / stances
- **Frenzy** (`SKILL_FRENZY`, `olog_hai.cpp:524`) *(Olog-Hai)* — enter a battle frenzy: forces
  **Berserk** tactics, grants **+10 % to all damage** (`frenzy_effect`, `fight.cpp:2244`) and
  makes your auto-attacks count as criticals (`is_frenzy_active` forces the to-hit roll to 35),
  but **disables Bash**. Short duration; **cooldown 600 s (10 min)**.
- **Berserk** — see Passive/utility above.
- **Defend** (`SKILL_DEFEND`) *(spec: Defender)* — shield-block buff; see specializations.md.

---

## Race-gated warrior skills
These live in the warrior skill table but require a specific race. Full race mechanics will live
in a races doc; damage formulas here.

### Beorning
- **Bite** (`SKILL_BITE`, `act_offe.cpp:1200`) — **`(S(M) + STR)` × 0.5** physical
  (**× 0.75** Wild Fighting, **× 1.0** Heavy Fighting). *Scales: Warrior level, STR, skill.*
  Typical ≈ 17–23 (normal), ≈ 34–46 (Heavy). Free; **4.0 s + 0–3.0 s random** recovery.
- **Rend** (`SKILL_REND`, `act_offe.cpp:1117`) — like Bite plus `weight/3500` (≈ 0 in practice):
  **`(S(M) + STR + weight/3500)` × 0.5** (×0.75 Wild, ×1.0 Heavy). **Requires the target to be
  bashed** (`AFF_BASH`). Same typical range as Bite. Free; **4.0 s + 0–3.0 s random** recovery.
- **Maul** (`SKILL_MAUL`, `act_offe.cpp:1336`) — **1–4** damage; the point is its **debuff**
  (target −5 dodge) and **self damage-reduction buff** (`maul_damage_reduction`, stacks). Costs
  **5 move** (Defender) / **10 move** otherwise.
- **Swipe** (`SKILL_SWIPE`) — a **passive proc**, not a command (`can_beorning_swipe`/
  `does_beorning_swipe_proc`, applied in the round driver `fight.cpp:2774`). Each of your
  auto-attacks has a chance to trigger a **free extra attack** (a full `hit()` with energy
  restored first — a normal weapon strike, not a special formula), exactly like Light
  Fighting's double strike. Proc chance = `(WarriorLevel/3 + SwipeSkill/10 + Level/10) / 100`,
  **capping around 31 %** for a level-90, 36-warrior Beorning with Swipe maxed
  (`fight.cpp:2680`). It's checked only when the Light-Fighting double strike doesn't fire
  (same `else if` chain), so a Beorning's bonus-attack slot is Swipe.

### Olog-Hai
All use the Olog base `B(M) = (2 + WarriorLevel)·(100 + M)/(1000 / tactics)` (`olog_hai.cpp`).
Note the denominator **shrinks as tactics get aggressive** (Defensive 1000 → Normal 333 →
Aggressive 250 → Berserk 200), so these hit harder the more aggressively you fight — equal to a
Kick at Aggressive, weaker below it, stronger at Berserk. *(Caveat: the intended two-handed
×1.5 is a **no-op bug** — `*= 3/2` truncates to `*= 1` in integer math, `olog_hai.cpp`.)*
These use a real-second cooldown timer (see **Cooldowns** below), plus a shared **2-second
global cooldown** on any timed skill.
- **Smash** (`SKILL_SMASH`) — `B(M)` (**+5** Wild Fighting). Can **dismount** a mounted target
  (80 %); dismounted targets take ×1.25. Typical ≈ 10–20 (Normal) to ≈ 16–33 (Berserk).
  **Cooldown 120 s (2 min).**
- **Cleave** (`SKILL_CLEAVE`) — `B(M)` (**+5** Heavy Fighting), **hits everyone in the room**.
  **Cooldown 60 s (1 min).**
- **Overrun** (`SKILL_OVERRUN`) — `B(M)` (**×1.10** Heavy Fighting, **×1.25** if riding),
  **charges through rooms** in a direction (distance `WarriorLevel/8 ± 1`), damaging and
  **knocking down** (`AFF_BASH`) everyone hit. Hitting a wall short of the distance deals **50
  self-damage** (and a longer recovery). **Cooldown 120 s (2 min).**
- **Frenzy** — see Buffs/stances. **Cooldown 600 s (10 min).**

---

## Notes & open questions
- All "typical" numbers assume the reference build (W30, skill 100, OB ~150, target DB 40/PB 60,
  no resistance). **Skills bypass armor** (they call `damage()` directly), so these are close to
  the real hit — but `damage()` still applies resistances/vulnerabilities (×⅔ / ×3⁄2),
  Beorning/maul reduction, and PK-fame. Use the formulas for exact values.
- **Cooldowns** (Olog skills, `skill_timer`): the timer counter is in **seconds** — it
  decrements by 1 on `update_skill_timer`, called once per second (`comm.cpp:850`,
  `!(pulse % 4)` at 4 pulses/sec; `report_skill_status` even prints the value labeled
  "(seconds)"). So `FRENZY 600 = 10 min`, `SMASH/OVERRUN 120 = 2 min`, `CLEAVE 60 = 1 min`.
  Any timed skill also triggers a shared **2-second global cooldown** (`GLOBAL_COOLDOWN_COUNTER`,
  `skill_timer.h`).
- The Olog two-handed `*= 3/2` integer-truncation bug means 2H gives Olog skills no bonus —
  flag for the maintainers (likely intended ×1.5).
- `get_prob_skill` uses the **live** `get_real_OB/dodge/parry` (`utility.cpp`).
