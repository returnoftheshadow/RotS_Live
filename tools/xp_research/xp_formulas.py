"""Python mirror of the live server's experience gain and loss formulas.

Every function here cites the FORMULAS.md row (Task 1, tools/xp_research/FORMULAS.md) it mirrors
plus the C++ source it was traced from. This module does no I/O; evaluate_corpus.py reads the
CSVs and calls into these functions.

Assumption carried by every caller in this module that passes an `age_ticks` argument: the mob
being killed has lived exactly `average_mob_life` (40) raw MOB_AGE_TICKS (src/utils.h:677) -- "a
mob that has lived an average life" (controller decision 4). The DERIVED `age` used inside
exp_with_modifiers still depends on the victim's level (see that function's docstring), so this
raw-ticks assumption does not mean every mob gets the same age bonus.
"""
from __future__ import annotations

from parse_mobs import (
    MOB_AGGRESSIVE,
    MOB_FAST,
    MOB_MEMORY,
    MOB_SPEC,
    MOB_SWITCHING,
    MobRecord,
)

LEVEL_MAX = 30  # src/structs.h:45-52 ("legend")
LEVEL_IMMORT = 91  # src/structs.h:45-52
POSITION_STANDING = 8  # src/structs.h:930


def cdiv(numerator: int, denominator: int) -> int:
    """Integer division truncating toward zero, matching C++'s `/` on `int` operands.

    Python's `//` floors toward negative infinity, which disagrees with C++ whenever exactly one
    operand is negative (e.g. -7 // 2 == -4 in Python but -7 / 2 == -3 in C++). Every division in
    this module whose numerator can be negative (death_loss, and any exp_with_modifiers step fed
    a negative base_exp) goes through this helper instead of `//`.
    """
    quotient_magnitude = abs(numerator) // abs(denominator)
    if (numerator < 0) != (denominator < 0):
        return -quotient_magnitude
    return quotient_magnitude


def xp_to_level(level: int) -> int:
    """FORMULAS.md `xp_to_level`; src/limits.cpp:90: `return lvl * lvl * 1500;`."""
    return level * level * 1500


def levelb(level: int) -> int:
    """FORMULAS.md `levelb`; src/utils.h:315 `GET_LEVELB`.

    `min(level, LEVEL_MAX * 2 / 3 + level / 3)` for a PC; NPCs use their raw level (callers pass
    the NPC's own level directly rather than calling this function for mob killers).
    """
    return min(level, LEVEL_MAX * 2 // 3 + level // 3)


def gain_exp_clamp(gain: int, level: int) -> int:
    """FORMULAS.md `gain_exp_clamp`; src/limits.cpp:410-426 (`gain_exp`).

    Mirrors `gain_exp` exactly, including its level gates: a positive gain only reaches
    `gain_exp_regardless` while `GET_LEVEL(ch) < LEVEL_IMMORT - 1` (i.e. level < 90,
    src/limits.cpp:416); a negative gain only reaches it while `GET_LEVEL(ch) < LEVEL_IMMORT`
    (level < 91, src/limits.cpp:421). Outside those windows the call is a no-op, so this returns
    0 rather than the clamped magnitude -- in particular, a level-90-or-above character gains
    ZERO XP from any positive event (a kill or a hit); only losses still land at exactly level 90.
    Inside the windows, positive gains clamp to at most 7000 per event and negative gains clamp
    to at least -10000 per event.
    """
    if gain > 0:
        if level >= LEVEL_IMMORT - 1:
            return 0
        return min(7000, gain)
    if gain < 0:
        if level >= LEVEL_IMMORT:
            return 0
        return max(-10000, gain)
    return gain


def hit_xp_melee(attacker_level: int, victim_level: int, damage: int) -> int:
    """FORMULAS.md `hit_xp_melee`; src/fight.cpp:2097-2099 (inside `damage_credited`).

    `(1 + L_victim) * min(20 + 2 * L_attacker, dam) / (1 + L_attacker)`.
    """
    return (1 + victim_level) * min(20 + 2 * attacker_level, damage) // (1 + attacker_level)


def hit_xp_mental(attacker_level: int, victim_level: int, damage: int) -> int:
    """FORMULAS.md `hit_xp_mental`; src/clerics.cpp:226 (inside `do_mental`).

    Identical to `hit_xp_melee` with `dam` replaced by `damg * 5`.
    """
    return hit_xp_melee(attacker_level, victim_level, damage * 5)


def flee_loss(fleeing_level: int, opponent_level: int) -> int:
    """FORMULAS.md `flee_loss`; src/act_offe.cpp:388-390.

    `loose = L_fleeing + L_opponent`, applied to the fleeing character as `gain_exp(ch, -loose)`.
    This function returns the positive magnitude of that loss (the "cost" reported by
    evaluate_corpus.py's table 4), not the signed exp delta.
    """
    return fleeing_level + opponent_level


def death_loss(exp: int, level: int, full: bool) -> int:
    """FORMULAS.md `death_loss`; src/fight.cpp:1293-1325 (`die`).

    `base = -(exp - 3000) / (level + 2)` (C++ truncation toward zero); every PC death applies
    `min(0, base / 10)`, and `death_takes_full_mob_xp_loss` additionally applies `min(0, base)`
    when the death is classified as a real-mob kill (src/fight.cpp:1010-1047). `full=True` mirrors
    that second application being reached; the returned value is tenth-only when `full=False` and
    tenth-plus-full when `full=True`.
    """
    base = cdiv(-(exp - 3000), level + 2)
    tenth = min(0, cdiv(base, 10))
    if not full:
        return tenth
    return tenth + min(0, base)


def next_level_cost(level: int) -> int:
    """FORMULAS.md derived quantity: `xp_to_level(level + 1) - xp_to_level(level)`."""
    return xp_to_level(level + 1) - xp_to_level(level)


def kill_share(killer_levels: list[int], mob: MobRecord, attacked_level: int) -> list[int]:
    """FORMULAS.md "Kill share" (`group_gain`, src/fight.cpp:1490-1541).

    Returns each killer's `base` (share * levelb + group_bonus) BEFORE `exp_with_modifiers`, in
    the same order as `killer_levels`. `attacked_level` is the highest `levelb` that has damaged
    the mob (src/fight.cpp:1987-1988); for a solo kill this equals `levelb(killer_levels[0])`, per
    FORMULAS.md's "Solo kill: ... `level_total = 2 * levelb`" note.

    ```
    level_total = sum(levelb(k) for k in killer_levels) + attacked_level
    share       = mob.exp / 10
    share       = share * (n + 1) / n                      # NPC victim, n = len(killer_levels)
    share       = share / level_total
    group_bonus = min(share * levelb(k) / 2,
                       (level_total - attacked_level - levelb(k)) * share / 4)
    base(k)     = share * levelb(k) + group_bonus
    ```
    """
    levelb_values = [levelb(level) for level in killer_levels]
    level_total = sum(levelb_values) + attacked_level

    number_of_killers = len(killer_levels)
    share = mob.exp // 10
    share = share * (number_of_killers + 1) // number_of_killers
    share = cdiv(share, level_total)

    bases: list[int] = []
    for killer_levelb in levelb_values:
        first_operand = cdiv(share * killer_levelb, 2)
        second_operand = cdiv((level_total - attacked_level - killer_levelb) * share, 4)
        group_bonus = min(first_operand, second_operand)
        bases.append(share * killer_levelb + group_bonus)
    return bases


def exp_with_modifiers(killer_level: int, killer_is_good_race: bool, killer_is_good_align: bool,
                        killer_is_orc: bool, mob: MobRecord, zone_x: int, difficulty: int,
                        age_ticks: int, average_mob_life: int, base_exp: int) -> int:
    """FORMULAS.md `exp_with_modifiers`; src/fight.cpp:1334-1390.

    `zone_x` is the x coordinate of the zone containing the killer's room (the `M` command's load
    room, mapped to its zone the way src/db.cpp:1185-1192 does); `difficulty` is that same load
    row's `M` command `arg5` (src/zone.cpp:729). `age_ticks` is the raw `MOB_AGE_TICKS` value
    (src/utils.h:677), from which the function derives its own local `age` exactly as the C++
    does -- these are NOT the same number except when the victim is level 20
    (`age_ticks * 40 / (victim_level + 20) == age_ticks` only then).

    Two flag/alignment inputs the C++ reads off `character` are collapsed to explicit booleans
    here because this module has no PC record to read them from: `killer_is_good_race` is
    `RACE_GOOD(character)` (src/utils.h:636, used only by the step 8 east bonus) and
    `killer_is_good_align` is `IS_GOOD(character)` (src/utils.h:657, used only by the step 6
    good-on-good penalty); the victim's own `IS_GOOD` is computed from `mob.alignment` directly.

    Known simplification: step 6's `MOB_FLAGGED(dead_man, MOB_AGGRESSIVE) || IS_AGGR_TO(dead_man,
    character)` only checks the `MOB_AGGRESSIVE` flag here. `IS_AGGR_TO` (src/utils.h:661) reads
    the mob's `specials2.pref` race-aggression bitfield, which Task 2's `parse_mobs.py` already
    discards while parsing the `N` record (its `_pref` field, read and thrown away) -- so no
    caller of this module has that data available to model the race-conditional half of the
    bonus.

    Known simplification: step "MOB_SPEC with a live proc or program" gates on `MOB_FLAGGED(dead_
    man, MOB_SPEC) && dead_man->nr >= 0 && (mob_index[dead_man->nr].func || dead_man->specials.
    store_prog_number)` (src/fight.cpp:1378). This mirrors the `store_prog_number` half only
    (`mob.prog`, the mob file's own prog-number field -- see parse_mobs.py's fscanf field order,
    which matches src/db.cpp:1762-1765's `store_prog_number = tmp3` load) via `mob.prog != 0`.
    `mob_index[nr].func` is a hardcoded C function pointer assigned by vnum at boot
    (`ASSIGNMOB` in src/spec_ass.cpp), invisible to this offline model -- a mob whose only special
    trigger is a `.func` assignment (prog == 0) is therefore under-modeled: the live server grants
    the `+base_exp/10` bonus for it and this mirror does not.
    """
    if killer_is_orc and mob.is_orc_friend:
        return 0

    base_exp = cdiv(base_exp, max(killer_level + 1, mob.level - 2))

    if mob.level + 6 < killer_level:
        base_exp = cdiv(6 * base_exp, killer_level - mob.level)

    exp = base_exp
    age = cdiv(age_ticks * 40, mob.level + 20)

    if mob.level > 5:
        if age < average_mob_life:
            exp = cdiv(exp * (average_mob_life * 60 + age * 40), average_mob_life * 100)
        else:
            exp = cdiv(exp * (140 - cdiv(40 * average_mob_life, age)), 100)

    mob_is_aggressive = bool(mob.mob_flags & MOB_AGGRESSIVE)
    if mob_is_aggressive:
        exp += cdiv(base_exp, 5)

    if mob.mob_flags & MOB_FAST:
        exp += cdiv(base_exp, 10)

    if mob.mob_flags & MOB_SWITCHING:
        exp += cdiv(base_exp, 10)

    if mob.mob_flags & MOB_MEMORY:
        exp += cdiv(base_exp, 20)

    if mob.default_pos < POSITION_STANDING:
        exp -= cdiv(base_exp, 20)

    mob_is_good = mob.alignment >= 100
    if killer_is_good_align and mob_is_good:
        exp = cdiv(exp * 2, 3)

    if (mob.mob_flags & MOB_SPEC) and mob.prog != 0:
        exp += cdiv(base_exp, 10)

    if difficulty:
        exp = cdiv(exp * difficulty, 100)

    if killer_is_good_race and zone_x > 8:
        exp += cdiv(exp * min(zone_x - 8, 5) * 3, 100)

    exp += cdiv(2 * exp, max(1, killer_level - 1))

    return exp


def solo_kill_xp(killer_level: int, mob: MobRecord, zone_x: int, killer_is_good_race: bool,
                  killer_is_good_align: bool, killer_is_orc: bool, difficulty: int = 0,
                  average_mob_life: int = 40) -> int:
    """FORMULAS.md composition: `kill_share` (solo) + `exp_with_modifiers` + `gain_exp_clamp`.

    `attacked_level = levelb(killer_level)` (a solo killer is the only one who has damaged the
    mob, so `attacked_level` equals their own `levelb`); age uses `average_mob_life` raw
    `MOB_AGE_TICKS`, per controller decision 4 (see this module's header docstring).
    """
    attacked_level = levelb(killer_level)
    base_exp = kill_share([killer_level], mob, attacked_level)[0]
    modified = exp_with_modifiers(killer_level, killer_is_good_race, killer_is_good_align,
                                   killer_is_orc, mob, zone_x, difficulty, average_mob_life,
                                   average_mob_life, base_exp)
    return gain_exp_clamp(modified, killer_level)


def solo_pc_kill_xp(killer_level: int, victim_level: int, victim_exp: int) -> int:
    """Solo player-kill XP: the PC-victim path through `group_gain` and `exp_with_modifiers`.

    For a PC victim, `group_gain` (src/fight.cpp:1490-1541) omits the `(n + 1) / n` NPC-only
    factor and the `attacked_level` malus (both gated on `IS_NPC(dead_man)`), so for a solo killer
    `level_total = levelb(killer_level)` and the `group_bonus` second `min` operand is always 0
    exactly as in the NPC solo case (FORMULAS.md, "Kill share"). `exp_with_modifiers` then returns
    immediately after its own level-gap divisor for a PC victim (src/fight.cpp:1344-1346, step 3):
    none of the age/flag/alignment/difficulty/east-bonus/TEMPORARY steps apply to a player kill.
    """
    killer_levelb = levelb(killer_level)
    level_total = killer_levelb  # solo PC-victim kill: no attacked_level malus, one killer
    share = cdiv(victim_exp // 10, level_total)
    base = share * killer_levelb  # group_bonus == 0 for a solo kill
    base = cdiv(base, max(killer_level + 1, victim_level - 2))
    return gain_exp_clamp(base, killer_level)
