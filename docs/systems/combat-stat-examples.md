# Combat stat examples — how stats move the numbers

Concrete worked comparisons of three warrior-leaning builds, and how each derived value
responds to a one-point stat change. Formulas and source citations are in
[`stats-and-character-power.md` §6, §10](stats-and-character-power.md) and
[`combat-loop.md`](combat-loop.md); this doc only plugs in numbers.

## Assumptions (held constant unless noted)
- **Character level 30**, **Human** (max class level 30, STR cap 22), **Normal** tactics.
- **Baseline stats: STR 20, CON 18, DEX 14** (all below caps, so `bal_str = STR`).
- No encumbrance (`skill_penalty = dodge_penalty = 0`), no daylight/confuse modifiers,
  `points.OB/parry/dodge = 0`.
- Representative gear/skills (needed only for *absolute* numbers; the **deltas** in the
  sensitivity tables are independent of these except energy regen):
  one-handed weapon, weight 150, **bulk 3**, parry rating `value[1] = 20`;
  weapon knowledge 80, parry knowledge 80, attack skill 80, dodge 60, stealth 40.
- `constHit ≈ 70` at level 30 (base 10 + ~2/level; it's random — see §4).

## The three builds
Point spend → class levels at L30 (`class_level = 3·√points`, §2):

| Build | M / C / R / W points | Class levels (M/C/R/W) | `class_HP` |
|-------|----------------------|------------------------|-----------:|
| **Default Warrior** (preset `w`) | 9 / 16 / 25 / 100 | 9 / 12 / 15 / 30 | √366·200 ≈ **3826** |
| **Barbarian** (preset `b`) | 0 / 4 / 25 / 121 | 0 / 6 / 15 / 33 | √417·200 ≈ **4084** |
| **All-in W36** | 0 / 0 / 0 / 150 | 0 / 0 / 0 / 36 | √450·200 ≈ **4243** |

## Baseline derived values (STR 20 / CON 18 / DEX 14)
Using the §6/§10 formulas:

| Value | Formula (at these assumptions) | Default W (W30,R15) | Barbarian (W33,R15) | All-in (W36,R0) |
|-------|--------------------------------|--------------------:|--------------------:|----------------:|
| **Max HP** | `40 + 0.9·constHit + class_HP·0.08143` | ≈ **416** | ≈ **437** | ≈ **449** |
| **OB** | `bal_str + 1.5·W + 45 + 14.7` | ≈ **124.7** | ≈ **129.2** | ≈ **133.7** |
| **PB** | `(2·W + 30 + bal_str)·0.5 + 33.6` | ≈ **88.6** | ≈ **91.6** | ≈ **94.6** |
| **DB** | `DEX + 0.7·R + 6.5` | ≈ **31.0** | ≈ **31.0** | ≈ **20.5** |
| **Energy regen** (attack speed) | harmonic(str_speed, null_speed²), §6 | ≈ **156** | ≈ **156** | ≈ **156** |
| Mana pool | `constMana + INT + WIL/2 + 2·M` | highest (M9) | low (M0) | lowest (M0) |

Takeaways:
- The **all-in W36** wins OB (+9 over default) and PB (+6) and HP (+33), but pays for it with
  **much lower DB** (no ranger levels: 20.5 vs 31) and **no mana/utility**. Energy regen is
  identical here because it depends on STR/DEX/weapon, not class points.
- The **Barbarian** sits between: 3 more warrior levels than default (+4.5 OB, +3 PB, slightly
  more HP from `class_HP`) while keeping ranger 15 for the same DB as the default warrior.

## Sensitivity — what one extra stat point does (Default Warrior, baseline)
"Exact" = independent of the gear/skill assumptions; "example" = depends on the weapon/skills
chosen above.

| Change | Max HP | OB | PB | DB | Energy regen | ~damage/hit |
|--------|-------:|---:|---:|---:|-------------:|------------:|
| **+1 STR** (20→21) | — | **+1.0** (exact) | **+0.5** (exact) | — | ≈ +1.7 (example) | **≈ +1.2–1.5 %** |
| **+1 CON** (18→19) | **≈ +11.7** (≈ constHit/20 + class_HP·0.00214) | — | — | — | — | — |
| **+1 DEX** (14→15) | — | — | — | **+1.0** (exact) | ≈ +1.3 (example) | tiny (via speed) |

Notes:
- **STR** is the broad combat stat: +1 OB *and* +0.5 PB *and* faster swings *and* ~+1.2–1.5 %
  damage per landed hit (the OB channel via `remaining_OB` **plus** the direct `133·bal_str`
  term inside the damage factor, see combat-loop) *and* fewer misses against dodge and parry —
  but **half-rate above STR 22** (`bal_str`). At baseline 20 you're still 1:1. *(A DEX-based
  Light Fighter gets the OB/damage channel from DEX instead — see specializations.md.)*
- **CON** buys ≈ **12 HP per point** here (more for the all-in build: `class_HP` is larger, so
  the `class_HP·0.00214` term grows — ≈ +12.6 HP/CON for W36). It does nothing for OB/PB/DB.
- **DEX** is the defensive/speed stat: +1 DB per point (1:1) and faster attacks; no direct
  offense. Its energy-regen value diminishes as DEX rises (the speed terms combine through a
  harmonic mean and a square root, §6).
- **Warrior class level** (build choice, not a stat): +1.5 OB and +1 PB per level — which is
  why W36 leads W30 by ≈ +9 OB / +6 PB. **Ranger class level**: +0.7 DB per level — why the
  all-in build's DB collapses.

## One STR point vs. one Warrior level (for damage)
A focused comparison: at **STR 18, character level 30, Normal tactics, a 1H weapon,
`EXTRA_DAMAGE` maxed**, is a point of Strength or a Warrior class level worth more *for
damage*? Both feed the live per-hit formula
`dam = base · (remaining_OB + 100) · F / 13.3M`, `F = 10000 + d100² + 133·bal_str`
(`fight.cpp:2516`, stats §10). Each lever hits different channels:

| Lever | OB channel (→ `remaining_OB`) | Direct damage channel | Find-weakness (×1.5 proc) |
|-------|-------------------------------|-----------------------|---------------------------|
| **+1 STR** (18→19) | +1 OB → **+0.875** remaining_OB | **+133 to `F`** ≈ **+0.85 %** (flat, every hit) | — |
| **+1 Warrior level** | +1.5 OB → **+1.3125** remaining_OB | — | **+~0.5 %** (below L30; ~+1 % above, via the kicker) |

The OB-channel value is `Δremaining_OB / (margin + 100)`, so it shrinks as your margin over the
target grows. Putting it together — **% extra damage per landed hit**:

| Target defense (remaining_OB) | +1 STR | +1 Warrior level |
|-------------------------------|-------:|-----------------:|
| **High** (margin ≈ 20) | 0.85 + 0.875/120 ≈ **+1.6 %** | 1.31/120 + 0.5 ≈ **+1.6 %** |
| **Moderate** (margin ≈ 100) | 0.85 + 0.875/200 ≈ **+1.3 %** | 1.31/200 + 0.5 ≈ **+1.2 %** |
| **Low** (margin ≈ 150) | 0.85 + 0.875/250 ≈ **+1.2 %** | 1.31/250 + 0.5 ≈ **+1.0 %** |

So **per landed hit they're remarkably close** — within a few tenths of a percent. STR's flat
`F`-channel keeps it slightly ahead at low/moderate defense; the Warrior level's larger OB swing
catches up against high-defense targets (where margin is small).

**Baselines (W30 vs W27).** Dropping to a **27-warrior** baseline barely changes the *marginal*
values above — it mainly lowers the standing numbers: find-weakness sits at **29 %** (vs 33 % at
W30) and `ob_bonus` is ~4.5 lower, so a W27's overall damage is a few percent under a W30's. The
**3-level gap W27→W30** is worth, at moderate defense, ≈ `3·1.31/200` (OB) + `(1.165/1.145 − 1)`
(find-weakness) ≈ **+3.7 % per hit** — i.e. **about the same as +3 STR** (18→21 ≈ +3.9 %).
Rule of thumb at moderate defense: **1 Warrior level ≈ 1 STR point** for raw per-hit damage.

**The tie-breakers (why it's not actually a wash):**
- **STR pulls ahead on throughput** — it also speeds up your swings (energy regen, §6), and
  *more swings multiply all your damage*; a Warrior level does nothing for attack speed. STR
  also adds +0.5 PB, carry, and lowers encumbrance. Caveat: STR is **capped at 22** (half-rate
  above), so this only holds below the cap.
- **A Warrior level pulls ahead vs. tough targets and at high class levels** — its bigger OB
  swing converts more *misses into hits* when the fight is close (a total-damage effect not
  shown in the per-hit table), it adds +1 PB (vs STR's +0.5), and **above class level 30 the
  find-weakness kicker roughly doubles** its damage value.

**Bottom line:** for a sub-cap warrior beating on ordinary targets, a STR point edges out a
Warrior level for total damage (mostly via attack speed); against high-defense opponents, or
once you're past class level 30, the Warrior level is at least its equal and often better.

### Continuing to W33 and W36: the find-weakness kicker
A Warrior level's *OB channel* is the same at every level (+1.3125 remaining_OB), but its
*find-weakness* channel **steepens above class level 30** because of the `prob += war − 30`
kicker (stats §10). Find-weakness chance (skill maxed) and the resulting per-level damage value:

| Class level | Find-weakness % | Find-weakness slope (per level) |
|------------:|----------------:|---------------------------------|
| 27 | 29 % | ~+0.5 % (below the kicker) |
| 30 | 33 % | ~+0.5 % |
| 33 | 39 % | **~+0.9 %** (kicker active) |
| 36 | 45 % | **~+0.9 %** |

So the value of **+1 Warrior level** for per-hit damage (Normal tactics, moderate margin ~100):

| Baseline | OB channel | Find-weakness | **+1 Warrior level total** | vs **+1 STR** (~+1.3 %) |
|----------|-----------:|--------------:|---------------------------:|:-----------------------:|
| W27 | +0.66 % | +0.5 % | **≈ +1.1 %** | STR wins |
| W30 | +0.66 % | +0.5 % | **≈ +1.1 %** | STR wins |
| W33 | +0.66 % | +0.9 % | **≈ +1.5 %** | **Warrior level wins** |
| W36 | +0.66 % | +0.9 % | **≈ +1.5 %** | **Warrior level wins** |

**The crossover is at class level 30.** Below it, a STR point out-damages a Warrior level per
hit (STR's flat `F`-channel + OB > the Warrior level's smaller find-weakness + OB). At 31+ the
kicker roughly doubles the find-weakness contribution and the Warrior level pulls ahead — on top
of its standing advantages (more misses→hits vs tough targets, +1 PB). STR still wins on
*throughput* (attack speed) and is the only one of the two that helps below the STR-22 cap.

### Factoring in tactics
Tactics scale the **OB channel of both levers equally** — the tactic multiplier is applied to
the whole `ob_bonus` (which contains both the STR/offense term and the `1.5·war` term) before
the universal ×⅞ (`get_real_OB` switch, `utility.cpp:708`). The **`F` (strength) and
find-weakness channels are tactics-independent.**

| Tactic | OB ×mult | A STR point's OB Δ | A Warrior level's OB Δ |
|--------|---------:|-------------------:|-----------------------:|
| Defensive | ×0.75 | +0.66 | +0.98 |
| Careful | ×0.875 | +0.77 | +1.15 |
| Normal | ×1.0 | +0.875 | +1.31 |
| Aggressive | ×1.0625 | +0.93 | +1.39 |
| Berserk | ×1.0625 (+5 flat, −defense) | +0.93 | +1.39 |

Per-hit damage value at a **moderate margin (~100)**, STR 18:

| Tactic | +1 STR | +1 War (≤L30) | +1 War (≥L33) |
|--------|-------:|--------------:|--------------:|
| Defensive | ~+1.18 % | ~+0.97 % | ~+1.37 % |
| Careful | ~+1.23 % | ~+1.05 % | ~+1.45 % |
| Normal | ~+1.29 % | ~+1.14 % | ~+1.54 % |
| Aggressive | ~+1.32 % | ~+1.18 % | ~+1.58 % |
| Berserk | ~+1.32 % | ~+1.18 % | ~+1.58 % |

Reading it: because a Warrior level is **more OB-weighted** than a STR point, **aggressive
stances (which pump the OB channel) tilt toward the Warrior level**, while **Defensive — which
shrinks OB but leaves STR's flat `F`-channel untouched — tilts toward STR**. The *crossover by
class level* (≤30 STR, ≥33 Warrior level) holds across every tactic; tactics just widen or
narrow the gap. (Caveats: a more aggressive stance also raises your own OB → larger margin →
slightly smaller % per OB point, which this fixed-margin table doesn't show; Berserk's +5 flat
OB, its halved parry/dodge, and any Wild-Fighting procs are separate.)

## How to regenerate these numbers
Plug your real stats, class levels, weapon (`value[1]` parry, bulk, weight), and knowledge
skills into the §6/§10 formulas. The **deltas** marked "exact" hold for any character below the
caps on Normal tactics; switch tactics by applying the tactics multipliers in the live
`get_real_OB`/`get_real_parry`/`get_real_dodge` (`utility.cpp`). Energy-regen and the absolute
OB/PB/DB constants shift with gear and skills, so treat the absolute columns as illustrative and
the deltas as the durable result.
