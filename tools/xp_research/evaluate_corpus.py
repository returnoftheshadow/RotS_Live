"""Evaluates the whole mob corpus against xp_formulas.py at seven player tiers and writes the
resulting Markdown report to stdout.

Usage:
    python3 evaluate_corpus.py <mobs.csv> <zones.csv> <mob_loads.csv>

This script takes a third CSV, the zone-reset `M` load rows (parse_zones.py --mob-loads output),
because difficulty and the killer's zone come from the load row, not the mob file: each mob is
evaluated once per load row it appears in.

CSV choice: this script reads pre-generated CSVs (conventionally under $SCRATCH/xp/{mobs,zones,
mob_loads}.csv, produced by parse_mobs.py and parse_zones.py --mob-loads) rather than re-invoking
parse_mobs.parse_all / parse_zones.parse_all against lib/world directly, since those CSVs already
exist and re-parsing would duplicate work for the same result.
"""
from __future__ import annotations

import csv
import math
import statistics
import sys
from dataclasses import dataclass

from parse_mobs import (
    MOB_AGGRESSIVE,
    MOB_FAST,
    MOB_MEMORY,
    MOB_ORC_FRIEND,
    MOB_PET,
    MOB_SPEC,
    MOB_SWITCHING,
    MobRecord,
)
from xp_formulas import (
    death_loss,
    exp_with_modifiers,
    flee_loss,
    gain_exp_clamp,
    hit_xp_melee,
    kill_share,
    levelb,
    next_level_cost,
    solo_kill_xp,
    solo_pc_kill_xp,
    xp_to_level,
)

TIERS = [30, 40, 50, 60, 75, 89, 90]

# gain_exp only applies a positive gain while GET_LEVEL(ch) < LEVEL_IMMORT - 1 (90),
# src/limits.cpp:416 -- a level-90 (or higher) character therefore earns ZERO XP from any kill or
# hit; only losses still land (the negative gate is < LEVEL_IMMORT, 91). Tier 89 is included
# alongside 90 to show the top level that can still earn, against 90's live-truth zeros.

# Every table in this report evaluates a mob at raw MOB_AGE_TICKS == average_mob_life == 40, a
# mob that has lived an average life -- the neutral reference point for the age curve.
AGE_TICKS = 40
AVERAGE_MOB_LIFE = 40

# Reference killer for tables 1-3 and the duo/trio table: good-race
# (RACE_GOOD -- src/utils.h:636) and good-aligned (alignment 1000, IS_GOOD -- src/utils.h:657).
KILLER_IS_GOOD_RACE = True
KILLER_IS_GOOD_ALIGN = True
KILLER_IS_ORC = False

# "Neutral-or-evil mob" is mob alignment < 100 (below IS_GOOD's threshold).
GOOD_ALIGNMENT_THRESHOLD = 100

NO_EAST_ZONE_X = 8  # exp_with_modifiers only bonuses when zone_x > 8 (src/fight.cpp:1386)
EAST_BONUS_ZONE_X = 13  # min(13-8, 5)*3 = 15 percent: the capped east-of-the-river bonus

BUCKETS: list[tuple[int, int | None, str]] = [
    (1, 9, "1-9"), (10, 19, "10-19"), (20, 29, "20-29"), (30, 39, "30-39"),
    (40, 49, "40-49"), (50, 59, "50-59"), (60, None, "60+"),
]


def bucket_for_level(level: int) -> str | None:
    for low, high, label in BUCKETS:
        if high is None:
            if level >= low:
                return label
        elif low <= level <= high:
            return label
    return None  # level 0 (or negative): outside every bucket, reported as an anomaly


def bucket_for_tier(tier: int) -> str:
    """Maps a player tier to the mob-level bucket it is measured against, capped at "50-59" for
    every tier of 60 and up: the "60+" bucket is five spawn rows, shopkeepers/innkeepers/
    doorkeepers plus one boss, not level-appropriate content a tier-60+ player fights."""
    if tier >= 60:
        return "50-59"
    label = bucket_for_level(tier)
    assert label is not None, f"tier {tier} has no matching bucket"
    return label


@dataclass
class EvaluatedLoad:
    mob: MobRecord
    difficulty: int
    zone_x: int  # x of the zone containing the load room (the killer's own room)
    zone_number: int


def read_mobs(path: str) -> dict[int, MobRecord]:
    mobs: dict[int, MobRecord] = {}
    with open(path, newline="", encoding="utf-8") as handle:
        for row in csv.DictReader(handle):
            record = MobRecord(
                vnum=int(row["vnum"]), zone=int(row["zone"]), aliases=row["aliases"],
                short_descr=row["short_descr"], mob_flags=int(row["mob_flags"]),
                affected_by=int(row["affected_by"]), alignment=int(row["alignment"]),
                level=int(row["level"]), ob=int(row["ob"]), parry=int(row["parry"]),
                dodge=int(row["dodge"]), hit=int(row["hit"]), max_hit=int(row["max_hit"]),
                damage=int(row["damage"]), ene_regen=int(row["ene_regen"]), gold=int(row["gold"]),
                exp=int(row["exp"]), position=int(row["position"]),
                default_pos=int(row["default_pos"]), sex=int(row["sex"]), race=int(row["race"]),
                prog=int(row["prog"]), spirit=int(row["spirit"]),
            )
            mobs[record.vnum] = record
    return mobs


@dataclass
class ZoneInfo:
    number: int
    x: int
    top: int


def read_zones(path: str) -> list[ZoneInfo]:
    zones: list[ZoneInfo] = []
    with open(path, newline="", encoding="utf-8") as handle:
        for row in csv.DictReader(handle):
            zones.append(ZoneInfo(number=int(row["number"]), x=int(row["x"]), top=int(row["top"])))
    zones.sort(key=lambda zone: zone.number)
    return zones


def zone_for_room(zones: list[ZoneInfo], room: int) -> ZoneInfo | None:
    """Mirrors src/db.cpp:1185-1192: the first zone, in ascending zone-number order, whose
    `top` is at least the room number."""
    for zone in zones:
        if zone.top >= room:
            return zone
    return None


@dataclass
class MobLoadRow:
    zone: int
    vnum: int
    room: int
    max_existing: int
    load_percent: int
    difficulty: int


def read_mob_loads(path: str) -> list[MobLoadRow]:
    loads: list[MobLoadRow] = []
    with open(path, newline="", encoding="utf-8") as handle:
        for row in csv.DictReader(handle):
            loads.append(MobLoadRow(
                zone=int(row["zone"]), vnum=int(row["vnum"]), room=int(row["room"]),
                max_existing=int(row["max_existing"]), load_percent=int(row["load_percent"]),
                difficulty=int(row["difficulty"]),
            ))
    return loads


@dataclass
class CorpusExclusions:
    no_load_row_count: int = 0
    no_load_row_level_zero_count: int = 0
    pet_or_orc_friend_load_rows: int = 0
    unknown_vnum_load_rows: int = 0
    room_outside_any_zone_load_rows: int = 0
    evaluated_level_zero_outside_bucket_count: int = 0


def evaluate_loads(mobs: dict[int, MobRecord], zones: list[ZoneInfo],
                    loads: list[MobLoadRow]) -> tuple[list[EvaluatedLoad], CorpusExclusions]:
    exclusions = CorpusExclusions()

    loaded_vnums = {load.vnum for load in loads}
    for mob in mobs.values():
        if mob.vnum not in loaded_vnums:
            exclusions.no_load_row_count += 1
            if mob.level == 0:
                exclusions.no_load_row_level_zero_count += 1

    evaluated: list[EvaluatedLoad] = []
    evaluated_level_zero_vnums: set[int] = set()
    for load in loads:
        mob = mobs.get(load.vnum)
        if mob is None:
            exclusions.unknown_vnum_load_rows += 1
            continue
        if mob.is_pet or mob.is_orc_friend:
            exclusions.pet_or_orc_friend_load_rows += 1
            continue
        zone = zone_for_room(zones, load.room)
        if zone is None:
            exclusions.room_outside_any_zone_load_rows += 1
            continue
        # A loaded level-0 mob (a placeholder template that some zone still spawns) has no
        # BUCKETS entry -- bucket_for_level(0) returns None -- so it is evaluated (kept in the
        # returned list) but excluded from every bucketed table; counted here (by distinct vnum,
        # since one such template can be loaded at many rooms) so that exclusion is visible in
        # the report rather than silently shrinking bucket totals.
        if mob.level == 0:
            evaluated_level_zero_vnums.add(mob.vnum)
        evaluated.append(EvaluatedLoad(mob=mob, difficulty=load.difficulty, zone_x=zone.x,
                                        zone_number=load.zone))
    exclusions.evaluated_level_zero_outside_bucket_count = len(evaluated_level_zero_vnums)
    return evaluated, exclusions


def group_by_bucket(rows: list[EvaluatedLoad]) -> dict[str, list[EvaluatedLoad]]:
    grouped: dict[str, list[EvaluatedLoad]] = {label: [] for _, _, label in BUCKETS}
    for row in rows:
        label = bucket_for_level(row.mob.level)
        if label is not None:
            grouped[label].append(row)
    return grouped


def percentile_90(values: list[int]) -> int:
    ordered = sorted(values)
    index = min(len(ordered) - 1, max(0, math.ceil(0.9 * len(ordered)) - 1))
    return ordered[index]


def solo_kill_values(rows: list[EvaluatedLoad], tier: int, zone_x: int) -> list[int]:
    return [
        solo_kill_xp(tier, row.mob, zone_x, killer_is_good_race=KILLER_IS_GOOD_RACE,
                     killer_is_good_align=KILLER_IS_GOOD_ALIGN, killer_is_orc=KILLER_IS_ORC,
                     difficulty=row.difficulty, average_mob_life=AVERAGE_MOB_LIFE)
        for row in rows
    ]


def render_table1(rows_by_bucket: dict[str, list[EvaluatedLoad]]) -> str:
    lines = [
        "## Table 1: Kill XP by mob level bucket and tier",
        "",
        "Good-race, good-aligned killer (alignment 1000) against a neutral-or-evil mob "
        f"(alignment < {GOOD_ALIGNMENT_THRESHOLD}), difficulty taken from each load row, "
        "median mob's own good-aligned kills shown separately (good-on-good two-thirds penalty).",
        "",
        "| bucket | tier | n (neutral/evil) | median (no east) | p90 (no east) | "
        "median (+15% east) | p90 (+15% east) | median good-on-good |",
        "| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |",
    ]
    for _, _, label in BUCKETS:
        rows = rows_by_bucket[label]
        neutral_evil_rows = [row for row in rows if row.mob.alignment < GOOD_ALIGNMENT_THRESHOLD]
        good_rows = [row for row in rows if row.mob.alignment >= GOOD_ALIGNMENT_THRESHOLD]
        for tier in TIERS:
            if not neutral_evil_rows:
                lines.append(f"| {label} | {tier} | 0 | -- | -- | -- | -- | -- |")
                continue
            no_east = solo_kill_values(neutral_evil_rows, tier, NO_EAST_ZONE_X)
            east = solo_kill_values(neutral_evil_rows, tier, EAST_BONUS_ZONE_X)
            good_on_good = statistics.median(solo_kill_values(good_rows, tier, NO_EAST_ZONE_X)) \
                if good_rows else "--"
            lines.append(
                f"| {label} | {tier} | {len(neutral_evil_rows)} | {statistics.median(no_east)} | "
                f"{percentile_90(no_east)} | {statistics.median(east)} | {percentile_90(east)} | "
                f"{good_on_good} |"
            )
    return "\n".join(lines)


def render_table2(rows_by_bucket: dict[str, list[EvaluatedLoad]]) -> str:
    lines = [
        "## Table 2: Per-hit XP at the damage cap",
        "",
        "`hit_xp_melee(tier, victim_level, 10_000)`; damage is pinned far above every tier's cap "
        "(`20 + 2 * tier`) so `min(20 + 2 * tier, dam)` always resolves to the cap. \"per-hit XP\" "
        "is the formula value AFTER `gain_exp_clamp(value, tier)`, so it shows the actual gain a "
        "real hit lands, including the level-90 gate (src/limits.cpp:416) that zeroes every "
        "positive gain at tier >= 90. \"hits for a median kill\" divides the tier's own bucket "
        "median kill XP (table 1, no-east column) by the per-hit value, rounded up; at tier >= 90 "
        "the per-hit value is 0, so no finite number of hits ever completes a kill.",
        "",
        "| tier | victim level | per-hit XP | hits for a median level-matched kill |",
        "| ---: | ---: | ---: | ---: |",
    ]
    for tier in TIERS:
        bucket_label = bucket_for_tier(tier)
        neutral_evil_rows = [row for row in rows_by_bucket[bucket_label]
                              if row.mob.alignment < GOOD_ALIGNMENT_THRESHOLD]
        median_kill = statistics.median(solo_kill_values(neutral_evil_rows, tier, NO_EAST_ZONE_X)) \
            if neutral_evil_rows else 0
        for victim_level in sorted({15, 20, tier}):
            per_hit = gain_exp_clamp(hit_xp_melee(tier, victim_level, 10_000), tier)
            hits = math.ceil(median_kill / per_hit) if per_hit > 0 else "n/a (0 XP/hit)"
            lines.append(f"| {tier} | {victim_level} | {per_hit} | {hits} |")
    return "\n".join(lines)


def render_table3(rows_by_bucket: dict[str, list[EvaluatedLoad]], damage_note: str) -> str:
    lines = [
        "## Table 3: Hits available per mob",
        "",
        f"Damage figure: {damage_note}",
        "",
        "| bucket | n | median max_hit | median level | tier | damage/hit | hits to kill "
        "(ceil) | per-hit XP | total per-hit XP for the mob |",
        "| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |",
    ]
    for _, _, label in BUCKETS:
        rows = rows_by_bucket[label]
        if not rows:
            continue
        median_max_hit = statistics.median(row.mob.max_hit for row in rows)
        median_level = int(statistics.median(row.mob.level for row in rows))
        for tier in TIERS:
            damage_per_hit = 20 + 2 * tier
            hits_to_kill = math.ceil(median_max_hit / damage_per_hit)
            per_hit_xp = gain_exp_clamp(hit_xp_melee(tier, median_level, damage_per_hit), tier)
            total = hits_to_kill * per_hit_xp
            lines.append(
                f"| {label} | {len(rows)} | {median_max_hit:g} | {median_level} | {tier} | "
                f"{damage_per_hit} | {hits_to_kill} | {per_hit_xp} | {total} |"
            )
    return "\n".join(lines)


def render_table4(rows_by_bucket: dict[str, list[EvaluatedLoad]]) -> str:
    lines = [
        "## Table 4: Cost of failure",
        "",
        "`flee_loss` is the positive magnitude of the exp lost fleeing; `death_loss` figures use "
        f"`exp = xp_to_level(tier)`. \"kills\" counts the tier's own bucket median level-matched "
        "kill XP (table 1, no-east column).",
        "",
        "| tier | flee vs L15 | flee vs L(tier) | death tenth | death tenth % of level | "
        "death tenth (kills) | death full | death full % of level | death full (kills) |",
        "| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |",
    ]
    for tier in TIERS:
        bucket_label = bucket_for_tier(tier)
        neutral_evil_rows = [row for row in rows_by_bucket[bucket_label]
                              if row.mob.alignment < GOOD_ALIGNMENT_THRESHOLD]
        median_kill = statistics.median(solo_kill_values(neutral_evil_rows, tier, NO_EAST_ZONE_X)) \
            if neutral_evil_rows else 0

        flee_15 = flee_loss(tier, 15)
        flee_matched = flee_loss(tier, tier)
        exp_at_tier = xp_to_level(tier)
        tenth = death_loss(exp_at_tier, tier, full=False)
        full = death_loss(exp_at_tier, tier, full=True)
        level_cost = next_level_cost(tier)

        def as_kills(amount: int) -> str:
            return f"{abs(amount) / median_kill:.1f}" if median_kill else "--"

        lines.append(
            f"| {tier} | {flee_15} | {flee_matched} | {tenth} | "
            f"{abs(tenth) / level_cost * 100:.2f}% | {as_kills(tenth)} | {full} | "
            f"{abs(full) / level_cost * 100:.2f}% | {as_kills(full)} |"
        )
    return "\n".join(lines)


def render_table5() -> str:
    lines = [
        "## Table 5: Clamp pressure",
        "",
        "Minimum gain events per level forced by the 7000-per-event positive clamp "
        "(`gain_exp_clamp`). At tier 90 this count is purely theoretical: no positive gain ever "
        "lands at level >= 90 (src/limits.cpp:416), so a level-90 character cannot reach level 91 "
        "through kills or hits at all.",
        "",
        "| tier | next_level_cost | min gain events (ceil / 7000) |",
        "| ---: | ---: | ---: |",
    ]
    for tier in TIERS:
        cost = next_level_cost(tier)
        lines.append(f"| {tier} | {cost} | {math.ceil(cost / 7000)} |")
    return "\n".join(lines)


def mob_flag_labels(mob: MobRecord) -> str:
    labels = []
    if mob.mob_flags & MOB_AGGRESSIVE:
        labels.append("aggressive")
    if mob.mob_flags & MOB_FAST:
        labels.append("fast")
    if mob.mob_flags & MOB_SWITCHING:
        labels.append("switching")
    if mob.mob_flags & MOB_MEMORY:
        labels.append("memory")
    if mob.mob_flags & MOB_SPEC:
        labels.append("spec")
    if mob.mob_flags & MOB_ORC_FRIEND:
        labels.append("orc_friend")
    if mob.mob_flags & MOB_PET:
        labels.append("pet")
    return ",".join(labels) if labels else "--"


def render_table6_outliers(rows: list[EvaluatedLoad], tier: int) -> str:
    scored = [
        (solo_kill_xp(tier, row.mob, row.zone_x, killer_is_good_race=KILLER_IS_GOOD_RACE,
                      killer_is_good_align=KILLER_IS_GOOD_ALIGN, killer_is_orc=KILLER_IS_ORC,
                      difficulty=row.difficulty, average_mob_life=AVERAGE_MOB_LIFE), row)
        for row in rows
    ]
    scored.sort(key=lambda pair: pair[0], reverse=True)
    lines = [
        f"### Twenty highest solo_kill_xp at tier {tier} (each load row's own zone and difficulty)",
        "",
        "| solo_kill_xp | vnum | zone | mob level | mob exp | difficulty | flags |",
        "| ---: | ---: | ---: | ---: | ---: | ---: | --- |",
    ]
    for value, row in scored[:20]:
        lines.append(
            f"| {value} | {row.mob.vnum} | {row.zone_number} | {row.mob.level} | "
            f"{row.mob.exp} | {row.difficulty} | {mob_flag_labels(row.mob)} |"
        )
    return "\n".join(lines)


def render_table7() -> str:
    lines = [
        "## Table 7: Solo player-kill XP (killer tier x victim tier)",
        "",
        "PC-victim path: `solo_pc_kill_xp(killer_tier, victim_tier, xp_to_level(victim_tier))`, "
        "which returns right after the level-gap divisor for PC victims (no age/flag/alignment/"
        "difficulty/east-bonus/TEMPORARY steps), then the 7000 clamp.",
        "",
        "| killer tier \\ victim tier | " + " | ".join(str(tier) for tier in TIERS) + " |",
        "| ---: | " + " | ".join("---:" for _ in TIERS) + " |",
    ]
    for killer_tier in TIERS:
        row_values = [str(solo_pc_kill_xp(killer_tier, victim_tier, xp_to_level(victim_tier)))
                      for victim_tier in TIERS]
        lines.append(f"| {killer_tier} | " + " | ".join(row_values) + " |")
    return "\n".join(lines)


def synthetic_median_mob(rows_by_bucket: dict[str, list[EvaluatedLoad]], tier: int) -> MobRecord:
    """A level-matched, neutral-alignment, unflagged mob whose exp is the bucket median exp for
    `tier`'s own level bucket, standing, default difficulty 100 (100 is `exp_with_modifiers`'s
    unchanged value, per FORMULAS.md's `exp_with_modifiers` step 7)."""
    bucket_label = bucket_for_tier(tier)
    neutral_evil_rows = [row for row in rows_by_bucket[bucket_label]
                          if row.mob.alignment < GOOD_ALIGNMENT_THRESHOLD]
    median_exp = int(statistics.median(row.mob.exp for row in neutral_evil_rows)) \
        if neutral_evil_rows else 0
    return MobRecord(vnum=0, zone=0, aliases="", short_descr="", mob_flags=0, affected_by=0,
                      alignment=0, level=tier, ob=0, parry=0, dodge=0, hit=0, max_hit=0, damage=0,
                      ene_regen=0, gold=0, exp=median_exp, position=8, default_pos=8, sex=0,
                      race=0, prog=0, spirit=0)


def render_duo_trio_table(rows_by_bucket: dict[str, list[EvaluatedLoad]]) -> str:
    lines = [
        "## Duo and trio versus solo (hypothesis 6)",
        "",
        "Equal-level killers against the level-matched median neutral-or-evil mob for that tier "
        "(difficulty 100, zone x = 8). `attacked_level = levelb(tier)`, the same as any one "
        "member's own `levelb` (FORMULAS.md, \"Kill share\").",
        "",
        "| tier | mob exp | solo | duo per member | duo total | trio per member | trio total |",
        "| ---: | ---: | ---: | ---: | ---: | ---: | ---: |",
    ]
    for tier in TIERS:
        mob = synthetic_median_mob(rows_by_bucket, tier)
        attacked_level = levelb(tier)

        solo = solo_kill_xp(tier, mob, NO_EAST_ZONE_X, killer_is_good_race=KILLER_IS_GOOD_RACE,
                             killer_is_good_align=KILLER_IS_GOOD_ALIGN, killer_is_orc=KILLER_IS_ORC,
                             difficulty=100, average_mob_life=AVERAGE_MOB_LIFE)

        def group_awards(member_count: int) -> list[int]:
            bases = kill_share([tier] * member_count, mob, attacked_level)
            awards = []
            for base in bases:
                modified = exp_with_modifiers(tier, KILLER_IS_GOOD_RACE, KILLER_IS_GOOD_ALIGN,
                                               KILLER_IS_ORC, mob, NO_EAST_ZONE_X, 100,
                                               AGE_TICKS, AVERAGE_MOB_LIFE, base)
                awards.append(gain_exp_clamp(modified, tier))
            return awards

        duo_awards = group_awards(2)
        trio_awards = group_awards(3)
        lines.append(
            f"| {tier} | {mob.exp} | {solo} | {duo_awards[0]} | {sum(duo_awards)} | "
            f"{trio_awards[0]} | {sum(trio_awards)} |"
        )
    return "\n".join(lines)


def render_assumptions_header(exclusions: CorpusExclusions, evaluated_count: int,
                               total_mob_count: int, damage_note: str) -> str:
    return "\n".join([
        "# XP model over the corpus: tiers 30, 40, 50, 60, 75, 89, 90",
        "",
        "## Assumptions",
        "",
        f"- Age: every mob evaluated at raw `MOB_AGE_TICKS = average_mob_life = 40` "
        "(a mob that has lived an average life, the neutral reference point for the age curve). "
        "The DERIVED `age` inside `exp_with_modifiers` still depends on the victim's own level.",
        f"- Damage figure: {damage_note}",
        f"- Reference killer for tables 1-3 and the duo/trio table: good race (RACE_GOOD) and "
        f"good-aligned (alignment 1000, IS_GOOD) -- the reference killer race/alignment used "
        f"throughout this report.",
        "- \"Neutral-or-evil mob\" = mob alignment < 100 (below IS_GOOD's threshold).",
        "- Difficulty is read from each zone-reset `M` load row (0 and 100 both leave exp "
        "unchanged); difficulty is live tuning attached to the spawn command, not to the mob "
        "record itself.",
        "- The killer's zone x is the zone containing the mob's own load room (the killer stands "
        "in the spawn room), mapped by the first zone (ascending zone number) whose "
        "`top >= room`.",
        "- `gain_exp` only applies a POSITIVE gain while `GET_LEVEL(ch) < LEVEL_IMMORT - 1` (90) "
        "and a NEGATIVE gain while `GET_LEVEL(ch) < LEVEL_IMMORT` (91) (src/limits.cpp:410-426): "
        "a level-90-or-above character earns ZERO XP from any kill or hit (tables 1, 2, 3 and 7 "
        "go to 0 at tier 90), while losses (tables 4 and 5) still apply normally. Tier 89 is "
        "included to show the top level that can still earn, next to tier 90's live-truth zeros.",
        "",
        "## Excluded from all tables",
        "",
        f"- Mob prototypes with no zone-reset load row at all: {exclusions.no_load_row_count} "
        f"(of which {exclusions.no_load_row_level_zero_count} are level-0 placeholder templates).",
        f"- Load rows evaluated (pets and orc-friends excluded): {evaluated_count} of "
        f"{evaluated_count + exclusions.pet_or_orc_friend_load_rows} non-anomalous load rows.",
        f"- Load rows for a pet or orc-friend mob, excluded and counted separately: "
        f"{exclusions.pet_or_orc_friend_load_rows}.",
        f"- Load rows whose vnum has no matching mob record: {exclusions.unknown_vnum_load_rows}.",
        f"- Load rows whose room maps to no zone: {exclusions.room_outside_any_zone_load_rows}.",
        f"- Distinct loaded level-0 mobs (evaluated but outside every bucket, since BUCKETS starts "
        f"at 1-9, and therefore absent from every bucketed table): "
        f"{exclusions.evaluated_level_zero_outside_bucket_count}.",
        f"- Total mob records parsed: {total_mob_count}.",
        "",
    ])


def main(argv: list[str]) -> int:
    if len(argv) != 4:
        print(f"usage: {argv[0]} <mobs.csv> <zones.csv> <mob_loads.csv>", file=sys.stderr)
        return 2

    mobs = read_mobs(argv[1])
    zones = read_zones(argv[2])
    loads = read_mob_loads(argv[3])

    evaluated, exclusions = evaluate_loads(mobs, zones, loads)
    rows_by_bucket = group_by_bucket(evaluated)

    damage_note = (
        "docs/systems/combat-stat-examples.md:25-40 has no per-tier damage figure, so this "
        "report uses the OB/damage cap `20 + 2 * tier` as the attacker's damage per hit; above "
        "the cap, hit_xp_melee/mental no longer depend on damage."
    )

    sections = [
        render_assumptions_header(exclusions, len(evaluated), len(mobs), damage_note),
        render_table1(rows_by_bucket),
        "",
        render_table2(rows_by_bucket),
        "",
        render_table3(rows_by_bucket, damage_note),
        "",
        render_table4(rows_by_bucket),
        "",
        render_table5(),
        "",
        "## Table 6: Outliers",
        "",
        render_table6_outliers(evaluated, 60),
        "",
        render_table6_outliers(evaluated, 30),
        "",
        render_table7(),
        "",
        render_duo_trio_table(rows_by_bucket),
    ]
    print("\n".join(sections))
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
