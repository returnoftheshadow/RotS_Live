#!/usr/bin/env python3
"""Aggregate reporting on where the live player base sits on the level curve.

Player-data hygiene rule: this script prints AGGREGATE COUNTS ONLY. It must
never print a character name, idnum, host, or any other per-character
identifier. Player save filenames are parsed only for their numeric metadata
(level, idnum, log_time); the name component and every other filename field
are discarded immediately after the split and never surface in output. When
a save file's body is read (the Step 3 sample), the reader scans only for
the `level` and `exp` lines and stops as soon as both are found -- it never
reads name, password, host, description, or any other field.

Usage:
    python3 player_levels.py <players_root>

<players_root> is a `lib/players`-style directory containing `players.csv`
and the bucket subdirectories (A-E, F-J, K-O, P-T, U-Z, ZZZ) described in
docs/data-formats/player-save.md. All output goes to stdout as Markdown.
"""

import random
import sys
from pathlib import Path

# Canonical player-save bucket directories, per docs/data-formats/player-save.md.
# Other top-level entries under the players root (players.csv, temp, save,
# stale-duplicates, too-long, and any stray file not inside one of these
# buckets) are deliberately not globbed: they are not part of the documented
# live-save layout, and stale-duplicates/too-long/save hold retired or
# malformed copies that would double-count characters already represented
# here.
BUCKET_DIRECTORY_NAMES = ("A-E", "F-J", "K-O", "P-T", "U-Z", "ZZZ")

# (band label, low level, high level or None for open-ended)
LEVEL_BANDS = (
    ("1-9", 1, 9),
    ("10-19", 10, 19),
    ("20-29", 20, 29),
    ("30-39", 30, 39),
    ("40-49", 40, 49),
    ("50-59", 50, 59),
    ("60-74", 60, 74),
    ("75-89", 75, 89),
    ("90", 90, 90),
    ("91+", 91, None),
)

# Levels at or above each of these thresholds are reported as a running tier.
TIER_THRESHOLDS = (30, 40, 50, 60, 75, 90)

# Immortals are level 91 and above; mortals are level 90 and below.
IMMORTAL_LEVEL_FLOOR = 91

# Activity windows for Step 2, in days before the newest observed log_time.
ACTIVITY_WINDOW_DAYS = (90, 365, 730)
SECONDS_PER_DAY = 86400

# Step 3 sampling: the ten highest-level mortals, plus a fixed few per tier band.
SAMPLE_TOP_MORTAL_COUNT = 10
SAMPLE_PER_TIER_BAND = 3
SAMPLE_TIER_BANDS = ("30-39", "40-49", "50-59", "60-74", "75-89")
# Fixed seed: the tier-band sample only needs to be reproducible, not secret.
SAMPLE_RANDOM_SEED = 20260919

# A character can sit below its level's XP floor by up to this much before the
# server would actually delevel it, so small negative deltas are expected and
# are reported as their own band rather than folded into "0-25%".
DELEVEL_TOLERANCE_XP = 20000

XP_POSITION_BAND_LABELS = ("below level floor", "0-25%", "25-50%", "50-75%", "75-100%")


def xp_to_level(level):
    """Return the total XP required to reach the given level."""
    return 1500 * level * level


def next_level_cost(level):
    """Return the XP distance from `level` to `level + 1`."""
    return xp_to_level(level + 1) - xp_to_level(level)


def band_for_level(level):
    """Return the LEVEL_BANDS label containing `level`, or None if out of range."""
    for band_label, low_level, high_level in LEVEL_BANDS:
        if high_level is None:
            if level >= low_level:
                return band_label
        elif low_level <= level <= high_level:
            return band_label
    return None


def read_csv_levels(csv_path):
    """Read players.csv (columns name,level,race; no header) and return levels only.

    Returns (levels, skipped_row_count). A row is skipped -- and counted, not
    silently dropped -- when it is blank or missing the level field.
    """
    levels = []
    skipped_row_count = 0
    with csv_path.open("r", encoding="utf-8", errors="replace") as csv_file:
        for row_line in csv_file:
            row_line = row_line.strip()
            if not row_line:
                skipped_row_count += 1
                continue
            fields = row_line.split(",")
            if len(fields) < 2:
                skipped_row_count += 1
                continue
            try:
                levels.append(int(fields[1]))
            except ValueError:
                skipped_row_count += 1
                continue
    return levels, skipped_row_count


def histogram_by_band(levels):
    """Return {band_label: count} across LEVEL_BANDS for the given levels."""
    band_counts = {band_label: 0 for band_label, _low, _high in LEVEL_BANDS}
    for level in levels:
        band_label = band_for_level(level)
        if band_label is not None:
            band_counts[band_label] += 1
    return band_counts


def tier_counts(levels):
    """Return {threshold: count of levels >= threshold} for TIER_THRESHOLDS."""
    return {
        threshold: sum(1 for level in levels if level >= threshold)
        for threshold in TIER_THRESHOLDS
    }


def parse_player_filename(file_name):
    """Parse a `<name>.<level>.<race>.<idnum>.<log_time>.<flags>` filename.

    Returns (level, log_time) on success. Returns None for anything that does
    not match the documented six-field layout with numeric level, idnum, and
    log_time -- this also filters out non-save files encountered while
    globbing (e.g. stray text with no metadata suffix). The name field is
    discarded immediately and never returned.
    """
    fields = file_name.split(".")
    if len(fields) != 6:
        return None
    _name, level_text, _race, idnum_text, log_time_text, _flags = fields
    try:
        level = int(level_text)
        int(idnum_text)
        log_time = int(log_time_text)
    except ValueError:
        return None
    return level, log_time


def collect_filename_records(players_root):
    """Glob the canonical bucket directories and parse each filename's metadata.

    Returns (filename_records, skipped_entry_count). filename_records is a list
    of (level, log_time, file_path) tuples. skipped_entry_count counts every
    directory entry under the six bucket directories that was not a save file
    matching the documented six-field layout (for example the non-save-file
    entries observed in ZZZ) -- these are counted, not silently dropped. Only
    the filename is read here -- file bodies are never opened by this function.
    """
    filename_records = []
    skipped_entry_count = 0
    for bucket_name in BUCKET_DIRECTORY_NAMES:
        bucket_path = players_root / bucket_name
        if not bucket_path.is_dir():
            continue
        for directory_entry in bucket_path.iterdir():
            if not directory_entry.is_file():
                skipped_entry_count += 1
                continue
            parsed_metadata = parse_player_filename(directory_entry.name)
            if parsed_metadata is None:
                skipped_entry_count += 1
                continue
            level, log_time = parsed_metadata
            filename_records.append((level, log_time, directory_entry))
    return filename_records, skipped_entry_count


def levels_within_activity_window(filename_records, newest_log_time, window_days):
    """Return the levels of records whose log_time falls within `window_days`
    of `newest_log_time`."""
    cutoff_log_time = newest_log_time - window_days * SECONDS_PER_DAY
    return [
        level
        for level, log_time, _file_path in filename_records
        if log_time >= cutoff_log_time
    ]


def select_top_mortal_records(filename_records, count):
    """Return the `count` highest-level records with level <= 90 (mortals)."""
    mortal_records = [
        record for record in filename_records if record[0] <= IMMORTAL_LEVEL_FLOOR - 1
    ]
    mortal_records.sort(key=lambda record: (record[0], record[1]), reverse=True)
    return mortal_records[:count]


def select_tier_band_sample(filename_records, band_label, sample_size, random_source):
    """Return up to `sample_size` records whose level falls in `band_label`."""
    band_records = [
        record for record in filename_records if band_for_level(record[0]) == band_label
    ]
    if len(band_records) <= sample_size:
        return band_records
    return random_source.sample(band_records, sample_size)


def build_xp_position_sample(filename_records):
    """Assemble the Step 3 sample: top mortals plus a per-tier-band sample.

    Returns a de-duplicated list of (level, log_time, file_path) records --
    a record picked by more than one selection rule is only read once.
    """
    random_source = random.Random(SAMPLE_RANDOM_SEED)
    selected_records = list(
        select_top_mortal_records(filename_records, SAMPLE_TOP_MORTAL_COUNT)
    )
    for band_label in SAMPLE_TIER_BANDS:
        selected_records.extend(
            select_tier_band_sample(
                filename_records, band_label, SAMPLE_PER_TIER_BAND, random_source
            )
        )

    de_duplicated_records = []
    seen_file_paths = set()
    for record in selected_records:
        file_path = record[2]
        if file_path in seen_file_paths:
            continue
        seen_file_paths.add(file_path)
        de_duplicated_records.append(record)
    return de_duplicated_records


def read_level_and_exp(file_path):
    """Read only the `level` and `exp` lines from a player save file body.

    Stops scanning as soon as both keys are found. Opened in binary mode and
    decoded permissively because save bodies contain a raw-bytes password
    field that is not valid text; binary mode also keeps line-splitting
    anchored to '\\n' only, so stray '\\r' bytes inside that binary field
    cannot be mistaken for line breaks. Returns (level, exp), using None for
    either value not found before end of file.
    """
    level = None
    exp = None
    with file_path.open("rb") as save_file:
        for raw_line in save_file:
            decoded_line = raw_line.decode("latin-1", errors="replace").rstrip("\n")
            if level is None and decoded_line.startswith("level "):
                try:
                    level = int(decoded_line.split()[1])
                except (IndexError, ValueError):
                    pass
            elif exp is None and decoded_line.startswith("exp "):
                try:
                    exp = int(decoded_line.split()[1])
                except (IndexError, ValueError):
                    pass
            if level is not None and exp is not None:
                break
    return level, exp


def classify_xp_position(level, exp):
    """Classify how far `exp` sits between `level`'s floor and the next level."""
    level_floor = xp_to_level(level)
    xp_past_floor = exp - level_floor
    if xp_past_floor < 0:
        return "below level floor"
    level_cost = next_level_cost(level)
    fraction_percent = (xp_past_floor / level_cost) * 100 if level_cost else 0.0
    if fraction_percent < 25:
        return "0-25%"
    if fraction_percent < 50:
        return "25-50%"
    if fraction_percent < 75:
        return "50-75%"
    return "75-100%"


def format_band_table(title, band_counts, total_count):
    """Format a LEVEL_BANDS histogram as a Markdown table."""
    table_lines = [f"### {title}", "", "| Level band | Count |", "| --- | --- |"]
    for band_label, _low, _high in LEVEL_BANDS:
        table_lines.append(f"| {band_label} | {band_counts[band_label]} |")
    table_lines.append(f"| **Total** | **{total_count}** |")
    return "\n".join(table_lines)


def format_tier_table(title, tier_count_map):
    """Format a TIER_THRESHOLDS running-count table as Markdown."""
    table_lines = [f"### {title}", "", "| At or above level | Count |", "| --- | --- |"]
    for threshold in TIER_THRESHOLDS:
        table_lines.append(f"| {threshold} | {tier_count_map[threshold]} |")
    return "\n".join(table_lines)


def format_xp_position_table(title, position_counts, sample_size):
    """Format the Step 3 XP-position-within-level distribution as Markdown."""
    table_lines = [f"### {title}", "", "| XP position in level | Count |", "| --- | --- |"]
    for band_label in XP_POSITION_BAND_LABELS:
        table_lines.append(f"| {band_label} | {position_counts.get(band_label, 0)} |")
    table_lines.append(f"| **Sample size** | **{sample_size}** |")
    return "\n".join(table_lines)


def main(argv):
    if len(argv) != 2:
        print("usage: player_levels.py <players_root>", file=sys.stderr)
        return 2

    players_root = Path(argv[1])
    csv_path = players_root / "players.csv"
    if not csv_path.is_file():
        print(f"error: {csv_path} not found", file=sys.stderr)
        return 1

    print("# Player level distribution")
    print()
    print(
        "Aggregate counts only -- no character names or idnums appear below, "
        "per the player-data hygiene rule."
    )
    print()

    # Step 1: level histogram from players.csv.
    csv_levels, csv_skipped_row_count = read_csv_levels(csv_path)
    csv_band_counts = histogram_by_band(csv_levels)
    csv_tier_counts = tier_counts(csv_levels)
    mortal_60_to_89_count = csv_band_counts["60-74"] + csv_band_counts["75-89"]
    immortal_count = csv_band_counts["90"] + csv_band_counts["91+"]
    csv_row_count_seen = len(csv_levels) + csv_skipped_row_count

    print(f"## Step 1: Level histogram from `{csv_path.name}` ({len(csv_levels)} rows)")
    print()
    print(format_band_table("Level bands", csv_band_counts, len(csv_levels)))
    print()
    print(format_tier_table("At or above tier", csv_tier_counts))
    print()
    print(f"- Immortals (level {IMMORTAL_LEVEL_FLOOR}+): {csv_band_counts['91+']}")
    print(f"- Level 90 or above (tier-90 count above): {csv_tier_counts[90]}")
    print(f"- Mortal 60-89 band: {mortal_60_to_89_count}")
    print(f"- All level 90+ (staff-heavy tail, band '90' + '91+'): {immortal_count}")
    print(
        f"- {csv_skipped_row_count} CSV rows skipped (blank or short) out of "
        f"{csv_row_count_seen} rows read"
    )
    print()

    # Step 2: activity-weighted histogram from filenames.
    filename_records, filename_skipped_entry_count = collect_filename_records(players_root)
    filename_entries_seen = len(filename_records) + filename_skipped_entry_count
    print(f"## Step 2: Activity-weighted histogram from filenames ({len(filename_records)} save files)")
    print()
    print(
        f"- {filename_skipped_entry_count} directory entries skipped (filename does not "
        f"match the six-field save layout) out of {filename_entries_seen} entries seen "
        f"across the six bucket directories"
    )
    print()
    if not filename_records:
        print("No player save files found under the canonical bucket directories.")
        print()
    else:
        newest_log_time = max(log_time for _level, log_time, _path in filename_records)
        for window_days in ACTIVITY_WINDOW_DAYS:
            window_levels = levels_within_activity_window(
                filename_records, newest_log_time, window_days
            )
            print(f"### Active within {window_days} days of the newest log_time")
            print()
            print(format_band_table("Level bands", histogram_by_band(window_levels), len(window_levels)))
            print()
            print(format_tier_table("At or above tier", tier_counts(window_levels)))
            print()

    # Step 3: XP position within level for a small sample of mortals.
    print("## Step 3: XP position within level (sample)")
    print()
    sample_records = build_xp_position_sample(filename_records)
    position_counts = {}
    read_failure_count = 0
    for _level, _log_time, file_path in sample_records:
        body_level, body_exp = read_level_and_exp(file_path)
        if body_level is None or body_exp is None:
            read_failure_count += 1
            continue
        position_label = classify_xp_position(body_level, body_exp)
        position_counts[position_label] = position_counts.get(position_label, 0) + 1

    classified_count = sum(position_counts.values())
    print(
        format_xp_position_table(
            "XP position distribution", position_counts, classified_count
        )
    )
    print()
    print(
        f"- Sample selection: top {SAMPLE_TOP_MORTAL_COUNT} highest-level mortals plus "
        f"{SAMPLE_PER_TIER_BAND} per tier band {SAMPLE_TIER_BANDS}, de-duplicated "
        f"({len(sample_records)} unique files considered)."
    )
    if read_failure_count:
        print(f"- {read_failure_count} sampled file(s) had no readable level/exp line and were skipped.")

    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
