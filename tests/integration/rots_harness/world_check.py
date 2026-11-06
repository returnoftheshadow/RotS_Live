"""Checks the test world's files for the mistakes that only show at boot, or that a merge leaves behind:
indexes out of step with their folders, vnums out of order, zones sharing a map square or holding
another zone's vnums, conflict markers, and a zone without its notes file.

Each check returns one line per problem, empty when the world passes; unit/test_world_files.py runs
them on the committed world.
"""

from __future__ import annotations

import re
from pathlib import Path
from typing import Iterable

from rots_harness.world_files import (
    END_OF_FILE_VNUM,
    INDEX_FILE_NAME,
    VNUMS_PER_ZONE_NUMBER,
    WORLD_CATEGORIES,
    ZONE_CATEGORY,
    WorldFileError,
    fixture_vnums,
    index_order,
    index_text,
    indexed_files,
    read_world_text,
    read_zones,
    record_vnums,
    world_files,
    zone_vnum_ranges,
)

# The folder under the world holding one notes file per zone; the harness does not copy it.
NOTES_DIRECTORY_NAME = "notes"
# A zone's notes file name; {number} is the zone number.
ZONE_NOTES_NAME = "zone-{number}.md"
# Categories whose files end with the #99999 record that count_hash_records (db.cpp) expects.
TERMINATED_CATEGORIES = ("wld", "mob", "obj", "scr", "mdl")
# The room category, whose every vnum must lie inside some zone or the boot stops.
ROOM_CATEGORY = "wld"
# A line git writes when a merge leaves a conflict: <<<<<<<, |||||||, ======= or >>>>>>>.
CONFLICT_MARKER_LINE = re.compile(r"^(?:(?:<{7}|\|{7}|>{7})(?: |$)|={7}$)", re.MULTILINE)


def index_problems(world_dir: Path) -> list[str]:
    """Each category whose index does not list exactly its world files, ordered by first vnum."""
    problems = []
    for category in WORLD_CATEGORIES:
        category_dir = world_dir / category
        index_path = category_dir / INDEX_FILE_NAME
        if not index_path.is_file():
            problems.append(f"{category}/{INDEX_FILE_NAME} is missing")
            continue
        expected = index_text(index_order(category_dir))
        if read_world_text(index_path) != expected:
            listing = expected.replace("\n", " ").strip()
            problems.append(f"{category}/{INDEX_FILE_NAME} must list the folder's world files by first vnum: {listing}")
    return problems


def vnum_order_problems(world_dir: Path) -> list[str]:
    """Each vnum that does not ascend across its category's indexed files, which real_mobile and
    friends binary-search, and each repeated vnum."""
    problems = []
    for category in WORLD_CATEGORIES:
        highest_vnum = -1
        highest_where = ""
        seen: dict[int, str] = {}
        for path in indexed_files(world_dir, category):
            if not path.is_file():
                continue
            where = f"{category}/{path.name}"
            for vnum in fixture_vnums(read_world_text(path)):
                if vnum in seen:
                    problems.append(f"{where}: #{vnum} repeats #{vnum} in {seen[vnum]}")
                elif vnum < highest_vnum:
                    problems.append(f"{where}: #{vnum} comes after #{highest_vnum} in {highest_where}; vnums ascend across the index")
                seen.setdefault(vnum, where)
                if vnum > highest_vnum:
                    highest_vnum = vnum
                    highest_where = where
    return problems


def end_record_problems(world_dir: Path) -> list[str]:
    """Each room, mob, object, script or mudlle file whose last record is not #99999."""
    problems = []
    for category in TERMINATED_CATEGORIES:
        for path in world_files(world_dir / category):
            vnums = record_vnums(read_world_text(path))
            if not vnums or vnums[-1] != END_OF_FILE_VNUM:
                problems.append(f"{category}/{path.name}: the last record must be #{END_OF_FILE_VNUM}")
    return problems


def zone_problems(world_dir: Path) -> list[str]:
    """Zones that share a map square or whose tops do not ascend, files <n>.* holding vnums outside
    zone n's range, and rooms outside every zone."""
    try:
        zones = read_zones(world_dir)
    except (WorldFileError, OSError) as error:
        return [f"cannot read the zones: {error}"]
    problems = []
    squares: dict[tuple[int, int], int] = {}
    previous_top = -1
    for zone in zones:
        if zone.map_square in squares:
            problems.append(f"zone {zone.number} shares map square {zone.map_square} with zone {squares[zone.map_square]}; relocate and the world map find a zone by its square")
        squares.setdefault(zone.map_square, zone.number)
        if zone.top <= previous_top:
            problems.append(f"zone {zone.number}'s top {zone.top} is not above the previous zone's top {previous_top}")
        previous_top = max(previous_top, zone.top)
    ranges = zone_vnum_ranges(zones)
    for category in WORLD_CATEGORIES:
        if category == ZONE_CATEGORY:
            continue
        for path in world_files(world_dir / category):
            if not path.stem.isdigit():
                continue
            file_number = int(path.stem)
            # A file named <n>.<extension> holds n * 100 to n * 100 + 99, where the shaping editors look.
            block = (file_number * VNUMS_PER_ZONE_NUMBER, file_number * VNUMS_PER_ZONE_NUMBER + VNUMS_PER_ZONE_NUMBER - 1)
            zone_range = ranges.get(file_number)
            for vnum in fixture_vnums(read_world_text(path)):
                if not block[0] <= vnum <= block[1]:
                    problems.append(f"{category}/{path.name}: #{vnum} is outside {block[0]}-{block[1]}, where the shaping editors look for it")
                elif zone_range is not None and not zone_range[0] <= vnum <= zone_range[1]:
                    problems.append(f"{category}/{path.name}: #{vnum} is outside zone {file_number}'s range {zone_range[0]}-{zone_range[1]}")
    last_top = zones[-1].top if zones else -1
    for path in indexed_files(world_dir, ROOM_CATEGORY):
        if not path.is_file():
            continue
        for vnum in fixture_vnums(read_world_text(path)):
            if vnum > last_top:
                problems.append(f"{ROOM_CATEGORY}/{path.name}: room #{vnum} is outside every zone; the boot stops")
    return problems


def conflict_marker_problems(roots: Iterable[Path]) -> list[str]:
    """Each file under roots holding a merge conflict marker line."""
    problems = []
    for root in roots:
        for path in sorted(root.rglob("*")):
            if path.is_file() and CONFLICT_MARKER_LINE.search(read_world_text(path)):
                problems.append(f"{path}: a merge conflict marker is left in the file")
    return problems


def notes_problems(world_dir: Path) -> list[str]:
    """Each zone without its notes file, notes/zone-<n>.md."""
    try:
        zones = read_zones(world_dir)
    except (WorldFileError, OSError) as error:
        return [f"cannot read the zones: {error}"]
    notes_dir = world_dir / NOTES_DIRECTORY_NAME
    problems = []
    for zone in zones:
        notes_name = ZONE_NOTES_NAME.format(number=zone.number)
        if not (notes_dir / notes_name).is_file():
            problems.append(f"zone {zone.number} has no notes file {NOTES_DIRECTORY_NAME}/{notes_name}")
    return problems
