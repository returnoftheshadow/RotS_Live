# Specializations

**Source files:** spec enum `game_types::player_specs` (`structs.h:811-832`); per-spec data
`specialization_data`/`*_spec_data` (`structs.h:1467+`, set in `char_utils.cpp:1486-1505`);
warrior handlers `wild_fighting_handler.cpp`, `weapon_master_handler.cpp`,
`warrior_spec_handlers.h`. **Live** combat hooks live in `fight.cpp` (`heavy_fighting_effect`,
`defender_effect`, `wild_fighting_effect`, `get_evasion_malus`, `natural_attack_dam`),
`utility.cpp` (the light-fighting / weapon-master terms inside `get_real_OB`), `char_utils.cpp`
(encumbrance), `act_offe.cpp` (kick/swing/defend/maul/rescue), and `profs.cpp` (HP). Active spec
read via `utils::get_specialization`.
> ⚠️ `combat_manager.cpp` also contains spec hooks but is **dead code** — compiled, never
> invoked (see CLAUDE.md). Earlier drafts cited it for Protection's dodge bonus; the live path
> is `get_evasion_malus` in `fight.cpp`. Don't trust `combat_manager.cpp` line numbers.
**Status:** ✅ the five requested warrior-side specs detailed; others stubbed.

## What a specialization is
A character has **one active specialization** — a chosen focus layered on top of their
class-point build (§2 of `stats-and-character-power.md`) that reshapes how they fight or cast.
It's stored on the character and drives conditional bonuses throughout combat and magic
(extra damage, damage mitigation, procs, encumbrance handling, spell shaping). The full set
(`structs.h:811-832`):

```
PS_None
Warrior-side : PS_Defender, PS_WildFighting, PS_HeavyFighting, PS_LightFighting, PS_WeaponMaster
Defensive    : PS_Protection
Mage         : PS_Fire, PS_Cold, PS_Lightning, PS_Darkness, PS_Arcane, PS_BattleMage
Ranger/Mystic: PS_Regeneration, PS_Animals, PS_Stealth, PS_Archery, PS_Guardian,
               PS_Illusion, PS_Teleportation
```
(Profession groupings above are by observed usage; confirm against the spec-selection code
when documenting the stubbed ones.)

> **Two names for the same spec.** The codebase carries two parallel identifier sets that map
> 1:1 by index: the modern `game_types::player_specs` enum (`PS_WildFighting`, used by the live
> handlers in `fight.cpp` / `char_utils.cpp` / the `*_handler.cpp` files) and the older
> `PLRSPEC_*` `#define`s (`PLRSPEC_WILD = 7`, used in `consts.cpp`, `act_info.cpp`,
> `handler.cpp`). `warrior-skills.md` cites the `PLRSPEC_*` form; this doc uses the `PS_*` form
> — they refer to the same specs.

---

## Warrior specializations (detailed)

> **Every default melee build is also a partial Ranger.** Profession levels come from each
> class's fixed **class-point** split (`existing_profs`, `profs.cpp:37`), and a prof's level
> scales with the **square root** of its points (`square_root[]` in `GET_PROF_COOF`, leveling in
> `limits.cpp:advance_mini_level`). Both the **Warrior** (`'w'` = warrior **100** / ranger **25**)
> and **Barbarian** (`'b'` = warrior **121** / ranger **25**) classes carry ranger 25 = 5², which
> is one-quarter of warrior's points, so ranger ends at **half the warrior level**: at character
> level 30 a default Warrior or Barbarian is **W30 / R15**. This is not an optional "dip" — it is
> inherent to the class, and **two warrior specs cash it in automatically**: **Defender** (block
> chance keys off `min(warrior, ranger)`) and **Light Fighting** (OB folds in `ranger/3`). The
> R15 figures are used in those sections below.

### Wild Fighting (`PS_WildFighting`) — the berserker / glass cannon
**Role:** all-out aggression that gets *stronger the closer you are to death*. Rewards the
Aggressive/Berserk stances; pairs offense with risk.
- **Rush** (`wild_fighting_handler::do_rush`/`get_rush_chance`): a chance each hit to "rush
  forward wildly" and add **+½ the hit's damage** (≈ ×1.5). Chance scales with tactics:
  **Berserk 15 %, Aggressive 10 %, Normal 5 %** (0 on defensive stances).
- **Rage at low health** (Berserk, ≤ 45 % HP, `get_attack_speed_multiplier`): bonus attack
  speed that **scales from +15 % at 45 % HP up to ~+59 % at 1 % HP** — the more wounded, the
  faster you swing. Entering rage broadcasts a message.
- **Bloodlust heal on kill** (Berserk, victim ≥ 60 % of your *capped* level, `on_unit_killed`):
  heal **10 % of your missing HP** on the kill — sustain that only pays out while you stay in
  Berserk and keep dropping meaningful targets.
- **Wild swing** (`SKILL_SWING`, Wild-Fighting–gated): a telegraphed swing worth **1.5 ×** a
  kick's `S(M)`; while in **Berserk at ≤ 25 % HP**, `get_wild_swing_damage_multiplier` multiplies
  it a further **×1.33** (`act_offe.cpp:907`). Full formula in `warrior-skills.md`.
- Keeps **75 %** of natural-attack damage past level 11 (vs 50 % for most specs, full for Light
  Fighting; `natural_attack_dam`, `fight.cpp:2351`).
- **Note** the engine of the spec is the *auto-attack*: `rush` and the rage attack-speed bonus
  both ride every swing, so Wild Fighting snowballs in drawn-out fights, not from a single button.

### Heavy Fighting (`PS_HeavyFighting`) — the armored juggernaut
**Role:** wear the heaviest armor and swing the heaviest weapons with far less penalty; tanky
bruiser. Turns "too heavy to use well" into a strength.
- **Worn-weight soft cap** (`char_utils.cpp:634`): for each slot, weight above a per-slot
  threshold counts at only **⅓** — heavy armor encumbers a heavy fighter much less, preserving
  OB/dodge/attack-speed that encumbrance would otherwise sap.
- **Encumbrance soft cap** (`char_utils.cpp:696`): per-slot encumbrance above a cap is clamped
  to the cap, and the excess is currently **discarded entirely** (the intended `sqrt`-of-excess
  re-add is commented out — `// Drelidan: Removing this for now`), so stacking heavy gear costs
  a heavy fighter almost no encumbrance.
- **+10 % armor damage absorption** (`fight.cpp:2181`): incoming damage reduced by an extra
  tenth of what armor already blocks.
- **+5 % weapon damage** with heavy weapons (bulk ≥ 3 and weight over `LIGHT_WEAPON_WEIGHT_CUTOFF`,
  `heavy_fighting_effect`, `fight.cpp:2224`): `damage/20` bonus on every qualifying swing.
- **+20 % active-skill damage** (`do_skill_attack`, `act_offe.cpp:917`): kick, wild swing — any
  standard `S(M)` skill (`warrior-skills.md`) — hits **20 % harder** (`dam += dam/5`). Heavy
  Fighting is the only warrior spec that buffs *both* the auto-attack (via absorption/weapon
  bonus) and the active strikes.

### Light Fighting (`PS_LightFighting`) — the agile, dexterity-based duelist
**Role:** the fast, evasive fighter who fights on **Dexterity** rather than Strength. Wants a
**light, one-handed weapon** and minimal armor; trades the raw power of Heavy/Wild Fighting for
speed, extra strikes, and turning ranger levels into offense.

A weapon counts as "light" for these bonuses when **bulk ≤ 2, or bulk 3 with weight ≤
`LIGHT_WEAPON_WEIGHT_CUTOFF`** (`utility.cpp:668`, `fight.cpp:2648`).

- **Dexterity drives OB** (`get_real_OB`, `utility.cpp:666-677`): with a light weapon the OB
  "offense stat" becomes **`max(bal_str, DEX)`** — so a high-DEX light fighter uses Dexterity
  in place of Strength for offense. *Additionally* their effective warrior level for OB gains
  **+⅓ of their ranger level** (`warrior_level += ranger/3`), so ranger levels contribute to OB.
  **This is free for the default class:** a level-30 Warrior/Barbarian carries **R15** (see the
  callout above), so a light fighter's OB is computed as if they were **warrior level 35**
  (`30 + 15/3`) before the DEX stat term. (Recall OB ≈ `(warrior·3 + 3·max_war·level/30)/2 +
  offense_stat` — stats §10.)
- **Dexterity drives damage (indirectly):** live melee damage scales with `(OB + 100)`
  (stats §10 / combat-loop), and Light Fighting routes DEX into OB — so a DEX-built light
  fighter's damage rides on Dexterity instead of Strength. (The small explicit `bal_str` term
  in the damage formula is unchanged, but the dominant scaling is the OB channel.)
- **Passively boosts every OB-based skill — including kick** (`warrior-skills.md`): the hit
  margin `M = skill − ½DB − ½PB + ½·OB + 1d100 − 120` carries a **+½·OB** term, and the payload
  `S(M) ∝ (100 + M)` rises with it. Because Light Fighting's DEX substitution + `ranger/3` raise
  the *same*
  `get_real_OB` that `get_prob_skill` feeds on, a light fighter's **kick** (and bash land-chance,
  wild swing, bite, rend) all hit harder **for free**, with no skill points spent — a quiet
  second dividend of the OB the spec is already buying. The catch is the same as everywhere: it
  only applies while holding a **light** weapon (a 2H/heavy weapon drops the OB substitution, so
  the skill bonus evaporates with it).
- **Double strike (still active):** Light Fighting retains a **~20 % chance per attack to
  strike a second time** (`can_double_hit`/`does_double_hit_proc`, applied in the round driver
  `fight.cpp:2761`). Requirements: Light-Fighting spec, a **one-handed light weapon** (no
  two-handers), and a valid target in the room. The bonus strike is a full `hit()` and is
  **free** — the character's energy is restored before it fires ("you find an opening… and
  strike again rapidly"), and the extra damage is tracked in `light_fighting_data`.
- **Worn-weight reduction** (`char_utils.cpp:661`): subtracts a per-slot amount from each worn
  item's weight (floored at 0), driving encumbrance toward nothing so the dodge/OB/speed
  penalties from gear largely vanish.
- **Full natural-attack damage:** exempt from the post-level-11 natural-attack damage cut that
  hits other specs (`fight.cpp:2351`).
- Best with light/no armor and a light one-hander; a two-handed or heavy weapon disables the
  DEX-OB substitution *and* the double strike.

### Weapon Master (`PS_WeaponMaster`) — the per-weapon specialist
**Role:** mastery of *whatever weapon you hold* — each weapon type grants a distinct package of
speed, damage, penetration, or crowd-control. Rewards picking the right weapon for the job
(`weapon_master_handler.cpp`).

| Weapon type | What mastery grants |
|-------------|---------------------|
| **Piercing** | +15 % attack speed; **25 %** chance to **ignore armor** |
| **Whipping** | +15 % attack speed; **40 %** chance to **ignore shields** |
| **Flailing** | +15 % damage; **40 %** chance to **ignore shields** |
| **Cleaving** (1H/2H) | +15 % damage; **50 %** chance to **re-roll the damage roll and keep the higher** |
| **Slashing** (1H/2H) | +5 OB, +5 PB; **40 %** chance to **regain energy** (momentum → faster next swing) |
| **Stabbing** (spear) | +10 PB; **50 %** chance to **punch through armor** |
| **Bludgeoning** (1H/2H) | +10 OB; **25 %** chance to **drain the victim's energy** (10× damage) — staggers them |
| **Smiting** | +10 OB; chance (`damage·0.5 %`) to inflict **HAZE** on the victim |

### Protection (`PS_Protection`) — the mitigation spec *(not a warrior spec)*
**Role:** damage avoidance and protective magic. The live combat hook is `get_evasion_malus`
(`fight.cpp:2328`), which is built entirely around **`PROF_CLERIC`** level and perception and
only fires while the target is under **`AFF_EVASION`** — so Protection reads as a **Cleric-side**
spec, not a warrior one. It also surfaces in caster contexts (`mage.cpp:644`, `mystic.cpp:542`).
Kept here only to complete the set; it belongs in the caster/cleric doc.
- **+3 to the evasion malus** *while `AFF_EVASION` is up* (`get_evasion_malus`, `fight.cpp:2328`):
  it widens the dodge bonus the EVASION affect already grants. It is **not** an always-on flat
  +3 dodge, and it does nothing without the EVASION effect active.
- Strengthens protective spell effects in the caster paths above (exact magnitudes → magic doc).
- ⚠️ The mirror in `combat_manager.cpp:237` is **dead code** — ignore it.

### Defender (`PS_Defender`) — the shield tank
**Role:** the survivability/peel specialist — soak hits behind a shield and protect allies.
Built around blocking and rescuing rather than dealing damage.
- **+10 % max HP** (`profs.cpp:732`) — a flat survivability bump on top of the HP formula (§6).
- **Shield block** (`defender_effect`, `fight.cpp:2263`): the block math **requires an equipped
  `ITEM_SHIELD`** — no race exception here, even a Beorning needs the shield for it to run. Each
  block removes **30 % of the incoming hit's damage**, from two independent rolls that can stack
  into a **60 % "critical block"**:
  1. **Passive block** — chance(%) = `max(warrior, ranger) level + min(warrior, ranger)/2`,
     rolled `number(0,100)`. The `min(warrior, ranger)` half-term means the **baked-in R15**
     (see the callout) is pure profit: a default **W30/R15** Defender blocks at **30 + 15/2 ≈
     37 %** per incoming hit, *not* 30 %. (A hypothetical pure W30 with no ranger would sit at
     30 %, but no default melee build is actually pure.)
  2. **`DEFEND` skill** — a Defender-only active ("hunker down behind your shield", `do_defend`,
     `act_offe.cpp:948`) that lays a temporary affect whose block chance(%) **equals your
     `DEFEND` knowledge**; it lasts ~**6 s** (2 fast-update ticks) on a ~**12 s** cooldown. A
     **Beorning** may *invoke* `DEFEND` without a shield (they "brace"), but since the reduction
     still runs through `defender_effect`'s shield check, that brace is mostly flavor unless a
     shield is worn.
- **Delay-free rescue** (`act_offe.cpp:784`): Defenders skip the `WAIT_STATE` that normally
  follows a `rescue`, so they can peel attackers off allies repeatedly — the core tank action.
- **Maul** is cheaper and stronger: energy cost **5 vs 10** (`act_offe.cpp:1300`) and a better
  damage-reduction profile (`maul_db 1.25 vs 2.0`, duration **6 vs 2**, `maul_damage_reduction`,
  `fight.cpp:1557`) — more mitigation, longer.
- Net: low personal damage, but the best raw survivability (HP + block + maul) and the only
  spec that can chain rescues without delay.

---

## Warrior play-styles — how the five specs actually play

The five warrior specs separate along three axes: **what stat carries you** (Strength vs
Dexterity), **how heavy your kit is** (plate-and-greatsword vs leathers-and-a-dagger), and
**what you do to the fight** (delete it, outlast it, control it, or anchor for the group). Read
against the skills in `warrior-skills.md` and the OB / HP math in
`stats-and-character-power.md`, they resolve into five genuinely distinct archetypes — not five
flavors of "hit it harder."

### At a glance
| Spec | Carries on | Wants (gear) | Tactics home | Damage ↔ Defense | Plays like |
|------|-----------|--------------|--------------|------------------|------------|
| **Wild Fighting** | Strength | any weapon, light armor | Berserk / Aggressive | ◆◆◆◆◇ offense | berserker executioner — snowballs as the fight drags and as *you* bleed |
| **Heavy Fighting** | Strength | heaviest armor **+** heaviest weapon | Normal / Aggressive | ◆◆◆◆ bruiser | armored juggernaut — bring the biggest kit, pay no weight tax |
| **Light Fighting** | **Dexterity** (+ free R15) | light 1H, minimal armor | Normal / Aggressive | ◆◆◆ evasive offense | agile duelist — speed, double-strikes, hit-and-run |
| **Weapon Master** | the weapon's stat | the *right* weapon (carry several) | tactics-agnostic | ◆◆◆ toolbox | the technician — match the weapon to the target |
| **Defender** | CON / HP | shield + warrior(/ranger) levels | Defensive / Normal | ◆◇ tank/support | shield wall — soak hits and peel for the group |

### Wild Fighting — risk-it-all aggression
You fight on **Strength** and you fight forward. The whole kit rewards **Berserk** (and
secondarily Aggressive): `rush` fires on a flat **15 % / 10 % / 5 %** of *auto-attacks* for a
free **+50 % damage**, so faster weapons that throw more swings see more rush procs. The
signature is the **comeback curve** — below 45 % HP in Berserk your attack speed ramps from
**+15 % up to ~+59 %** at death's door, the **wild swing** skill jumps **×1.33** under 25 %, and
killing a worthy target (≥ 60 % of your capped level) **heals 10 % of your missing HP**. The
bill for all of that is Berserk itself: parry and dodge are gutted (stats §10) and you **can't
flee**. It is the classic **glass cannon** — terrifying in a fight it can finish, and a corpse
in one it can't. **Race fit:** an **Olog-Hai** is the natural shell — `frenzy` *forces* Berserk
(turning on rush/rage for free), adds +10 % damage and crit auto-attacks, and stacks the Olog
smash/cleave/overrun kit (`warrior-skills.md`). **Skills it leans on:** auto-attacks first, then
wild swing; bite/rend keep 75 % scaling. **Best at:** solo execution and snowball duels; weakest
when forced to play patient or kite.

### Heavy Fighting — the immovable bruiser
The fantasy is "too heavy to use well" turned into a strength. A heavy fighter wears the
**heaviest armor** and swings the **heaviest weapon** (bulk ≥ 3, over the weight cutoff) and
pays almost none of the encumbrance that would sap everyone else's OB, dodge, and swing speed —
worn weight over a per-slot cap counts at **⅓**, and over-cap encumbrance is dropped outright.
On top of that it is flatly **tankier** (**+10 %** of whatever armor already absorbs) and hits
harder on **both** channels: **+5 %** weapon damage on heavy swings *and* **+20 %** on its
active skills (kick, wild swing). It has no low-HP gimmick and no stance tax — it just performs
at a high, durable baseline from Normal or Aggressive, which makes it the **most forgiving**
warrior spec and the natural **frontline anchor**. **Race fit:** anything big — **Olog-Hai**
(raw STR + bulk) is the obvious one. **Best at:** sustained main-tank/bruiser duty in groups and
attrition fights; it trades Wild Fighting's burst ceiling for a floor nobody else has.

### Light Fighting — the dexterity duelist
The only warrior spec that **abandons Strength**: wielding a **light one-hander** (bulk ≤ 2, or
bulk 3 under the cutoff) your offense stat becomes **`max(STR, DEX)`**, so a DEX build routes
Dexterity into OB — and since live melee damage rides `(OB + 100)`, DEX into your damage too. It
also folds **⅓ of your ranger level** into OB — and since the default Warrior/Barbarian already
carries **R15** at level 30, that is a free **+5 effective warrior levels** of OB before you
spend a thing, which in turn quietly raises every OB-based skill (your **kick** included). It
also grants a **~20 % chance to strike twice** per attack (free, energy refunded). Gear is the
inverse of Heavy Fighting: **light weapon, little armor**, with worn weight and encumbrance
shaved toward zero so dodge/speed stay high. Crucially it **does not want Berserk** — that would
gut the dodge the whole spec is built on — so it plays at Normal/Aggressive as a slippery,
high-evasion **duelist/skirmisher**. It also keeps **full** natural-attack damage. The catch:
put a two-hander or a heavy weapon in its hands and you lose **both** the DEX-OB substitution
**and** the double strike — it is the most gear-disciplined of the five. **Race fit:** high-DEX
races and ranger multiclassers; a **Beorning's** `swipe` bonus-attack shares the same proc slot
as the double strike, so a Beorning gets a bonus hit either way. **Build note:** the ranger
contribution is automatic for a Warrior/Barbarian (R15 baked in) — you don't multiclass for it,
you just don't waste it. **Best at:** hit-and-run, kiting, and 1-v-1s where evasion compounds;
punished by being forced to stand and trade.

### Weapon Master — the toolbox technician
Mastery of **whatever weapon you're holding** — each weapon *type* unlocks a different package
(see the table above), so the spec rewards **reading the fight and swapping weapons** rather than
committing to one stat or stance. Piercing/whips shred **armored or shielded** targets; cleaving
re-rolls for **burst**; flailing/cleaving add flat **damage**; bludgeon and smite bring
**control** (energy-drain stagger, HAZE); slashing gives **balance** (+OB/+PB and momentum
refunds). Bonuses are live and always-on through `get_real_OB` and the damage hooks, so there is
no ramp and no risk gimmick — your power is **your weapon knowledge and your kit breadth**. This
is the **highest-skill, most flexible** PvP pick and the most loadout-dependent: a master with
one weapon is a master in one situation. **Best at:** players who want to answer the *specific*
enemy in front of them; weakest when stuck with the wrong tool for the job.

### Defender — the shield wall
The survivability/peel specialist, built to **soak and protect** rather than deal damage.
**+10 % max HP**, a shield **block** that strips **30 %** of an incoming hit (two rolls can stack
to a **60 %** crit-block; passive chance ≈ **37 %** at a default W30/R15, plus the `DEFEND`
active), the
only **delay-free `rescue`** in the game (chain-peel attackers off allies with no recovery), and
a **cheaper, stronger `maul`** (5 vs 10 move; more reduction, longer) round out the most durable
warrior in the roster. The flip side is the lowest personal damage and a dependence on **having
allies to protect** — its best tools (rescue, peel) are wasted solo. **Race fit:** **Beorning**,
which can `brace` to use `DEFEND` without a shield and synergizes with the maul mitigation.
**Best at:** group frontline and ally protection; it is the clearest "support" warrior and the
one that most changes how a *party* fights rather than how *you* fight.

### Reading the axes together
- **Stat:** Wild / Heavy / Weapon Master scale on **Strength**; **Light Fighting** is the lone
  **Dexterity** path (and the one that turns the class's free **R15** into OB).
- **The free R15:** every default Warrior/Barbarian is **W30/R15** at level 30 (class-point
  math, callout above). Only two specs cash it in — **Defender** (≈37 % passive block instead of
  30 %) and **Light Fighting** (+5 OB warrior-levels → harder kicks/skills) — for everyone else
  those ranger levels are inert.
- **Gear weight:** **Heavy** and **Light** are mirror images — one removes the penalty for going
  *heaviest*, the other removes it for going *lightest*; **Weapon Master** cares about weapon
  *type* over weight; **Wild** and **Defender** are defined by stance and shield respectively.
- **Risk curve:** **Wild Fighting** is the only spec that gets *stronger as it dies* (and pays
  for it by being unable to flee); the rest deliver a steady profile.
- **Solo vs group:** **Defender** is the most group-defining (rescue/peel); **Wild** and
  **Light** are the strongest solo/duelists; **Heavy** anchors a line; **Weapon Master** flexes
  to either.
- **Not a warrior spec:** **Protection** lives here only for completeness — its live hook keys
  off `PROF_CLERIC` and the `AFF_EVASION` effect, so it belongs to the caster/cleric side.

---

## Non-warrior specializations (stubs — to be detailed)

> Each needs its own pass against the magic / skills systems. Captured here so the set is
> complete.

**Mage (elemental & hybrid)** — shape the mage's offensive magic; the per-spec damage/save/effect
kickers are documented in detail in **[magic-system.md](magic-system.md)** (§3 save matrix, §6
Battle-Mage, §7 spell table, §9 scaling). Data classes in `structs.h:1285-1379` mostly track
statistics for display; the gameplay lives in the spell functions.
- **Fire** (`PS_Fire`), **Cold** (`PS_Cold`), **Lightning** (`PS_Lightning`),
  **Darkness** (`PS_Darkness`), **Arcane** (`PS_Arcane`) — element-themed spell specialists
  (e.g. `cold_spec_data` tracks chill/energy-sap state). ✅ see magic-system.md
- **Battle Mage** (`PS_BattleMage`) — melee-capable caster hybrid: tactics-scaled
  spellpower/penetration and resists casting interruption. ✅ see magic-system.md §6

**Mystic (cleric specs)** — detailed in **[cleric-mystic-system.md §5](cleric-mystic-system.md)**:
- **Regeneration** (`PS_Regeneration`) — +6 healing level on the regen powers. ✅
- **Protection** (`PS_Protection`) — +1 resist-magic; gates the Protection power. ✅
- **Illusion** (`PS_Illusion`) — +6 level on haze/fear/terror, +1 hallucinate; gates Confuse. ✅
- **Guardian** (`PS_Guardian`) — gates the Guardian summon (aggressive/defensive/mystic builds). ✅

**Ranger (utility & support)** — profession mapping to confirm:
- **Animals** (`PS_Animals`), **Stealth** (`PS_Stealth`), **Archery** (`PS_Archery`),
  **Teleportation** (`PS_Teleportation`) is a **mage** spec (magic-system.md). ⬜

## Open questions
- **How a spec is chosen/changed** (level requirement, command, profession gating) — trace the
  spec-selection code and confirm the warrior/mage/ranger/mystic groupings above.
- Concrete magnitudes for the **Protection** spell/mitigation bonuses (its combat hook now looks
  **Cleric-side** — `get_evasion_malus` keys off `PROF_CLERIC` + `AFF_EVASION`) and all
  **stubbed** specs.
- Per-slot values in the heavy/light `*_weight_table`/`*_encumb_table` (`char_utils.cpp`).
