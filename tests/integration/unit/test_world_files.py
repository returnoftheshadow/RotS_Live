from __future__ import annotations

from pathlib import Path

import pytest

from rots_harness import world_check
from rots_harness.world_files import (
    WORLD_CATEGORIES,
    ResetRow,
    index_order,
    index_text,
    read_index,
    read_zone,
    zone_blocks,
    zone_vnum_ranges,
    read_zones,
)

# tests/integration, whose world/ and groups/ the committed-world tests check.
INTEGRATION_ROOT = Path(__file__).resolve().parents[1]
# The committed test world.
WORLD_DIR = INTEGRATION_ROOT / "world"
# A zone file in load_zones' layout; {number}, {x}, {y} and {top} are filled in.
ZONE_TEMPLATE = """#{number}
Zone {number}~
A synthetic zone.
~
~
0
? {x} {y} 5
{top}
999
2
M 0 {mob} {room} 0 100 100 1 1 a mob waits
= Section title
O 0 {mob} {room} 0 100 1
S
"""


def write_zone(world_dir: Path, number: int, square: tuple[int, int], top: int) -> None:
    """Writes zon/<number>.zon with one M row, one section row and one O row."""
    text = ZONE_TEMPLATE.format(number=number, x=square[0], y=square[1], top=top, mob=number * 100 + 1, room=number * 100 + 2)
    (world_dir / "zon" / f"{number}.zon").write_text(text, encoding="latin-1")


def write_records(world_dir: Path, category: str, file_name: str, vnums: list[int]) -> None:
    """Writes a world file holding one bare record per vnum and the closing #99999."""
    text = "".join(f"#{vnum}\nrecord {vnum}~\n" for vnum in vnums) + "#99999\n"
    (world_dir / category / file_name).write_text(text, encoding="latin-1")


def write_indexes(world_dir: Path) -> None:
    """Writes every category's index in the order the world check expects."""
    for category in WORLD_CATEGORIES:
        (world_dir / category / "index").write_text(index_text(index_order(world_dir / category)), encoding="latin-1")


@pytest.fixture
def world(tmp_path: Path) -> Path:
    """A small world that passes every check: zones 11 and 12 with rooms, mobs and their notes."""
    world_dir = tmp_path / "world"
    for category in WORLD_CATEGORIES:
        (world_dir / category).mkdir(parents=True)
    (world_dir / "wld" / "oldroms").mkdir()
    (world_dir / "wld" / "oldroms" / ".gitkeep").write_text("", encoding="latin-1")
    write_zone(world_dir, 11, (0, 0), 1199)
    write_zone(world_dir, 12, (3, 4), 1299)
    write_records(world_dir, "wld", "11.wld", [1101, 1102])
    write_records(world_dir, "wld", "12.wld", [1201])
    write_records(world_dir, "mob", "11.mob", [1101])
    write_records(world_dir, "mob", "guildmasters.mob", [2043])
    write_records(world_dir, "mob", "12.mob", [1201])
    write_indexes(world_dir)
    (world_dir / "notes").mkdir()
    for number in (11, 12):
        (world_dir / "notes" / f"zone-{number}.md").write_text("# notes\n", encoding="utf-8")
    return world_dir


def all_problems(world_dir: Path) -> list[str]:
    """Every check's problems for a world, the conflict markers included."""
    return (
        world_check.index_problems(world_dir)
        + world_check.vnum_order_problems(world_dir)
        + world_check.end_record_problems(world_dir)
        + world_check.zone_problems(world_dir)
        + world_check.notes_problems(world_dir)
        + world_check.conflict_marker_problems([world_dir])
    )


@pytest.mark.parametrize(
    "check",
    [
        world_check.index_problems,
        world_check.vnum_order_problems,
        world_check.end_record_problems,
        world_check.zone_problems,
        world_check.notes_problems,
    ],
)
def test_the_committed_world_passes(check) -> None:
    assert check(WORLD_DIR) == []


def test_the_committed_world_and_group_files_hold_no_conflict_markers() -> None:
    assert world_check.conflict_marker_problems([WORLD_DIR, INTEGRATION_ROOT / "groups"]) == []


def test_the_synthetic_world_passes(world: Path) -> None:
    assert all_problems(world) == []


def test_files_are_indexed_by_first_vnum_with_backup_folders_left_out(world: Path) -> None:
    assert index_order(world / "mob") == ["11.mob", "12.mob", "guildmasters.mob"]
    assert read_index(world / "wld") == ["11.wld", "12.wld"]
    assert index_text([]) == "$\n"


def test_an_unindexed_file_is_reported(world: Path) -> None:
    write_records(world, "wld", "13.wld", [1301])
    assert world_check.index_problems(world) == ["wld/index must list the folder's world files by first vnum: 11.wld 12.wld 13.wld $"]


def test_a_misordered_index_is_reported(world: Path) -> None:
    (world / "mob" / "index").write_text("12.mob\n11.mob\nguildmasters.mob\n$\n", encoding="latin-1")
    assert world_check.index_problems(world) == ["mob/index must list the folder's world files by first vnum: 11.mob 12.mob guildmasters.mob $"]
    assert world_check.vnum_order_problems(world) == ["mob/11.mob: #1101 comes after #1201 in mob/12.mob; vnums ascend across the index"]


def test_a_missing_index_is_reported(world: Path) -> None:
    (world / "shp" / "index").unlink()
    assert world_check.index_problems(world) == ["shp/index is missing"]


def test_a_vnum_out_of_order_within_a_file_is_reported(world: Path) -> None:
    write_records(world, "wld", "11.wld", [1102, 1101])
    assert world_check.vnum_order_problems(world) == ["wld/11.wld: #1101 comes after #1102 in wld/11.wld; vnums ascend across the index"]


def test_a_repeated_vnum_is_reported(world: Path) -> None:
    write_records(world, "mob", "guildmasters.mob", [1201, 2043])
    write_indexes(world)
    assert world_check.vnum_order_problems(world) == ["mob/guildmasters.mob: #1201 repeats #1201 in mob/12.mob"]


def test_a_file_without_its_closing_record_is_reported(world: Path) -> None:
    (world / "wld" / "12.wld").write_text("#1201\nrecord~\n", encoding="latin-1")
    assert world_check.end_record_problems(world) == ["wld/12.wld: the last record must be #99999"]


def test_a_shared_map_square_is_reported(world: Path) -> None:
    write_zone(world, 12, (0, 0), 1299)
    assert world_check.zone_problems(world) == ["zone 12 shares map square (0, 0) with zone 11; relocate and the world map find a zone by its square"]


def test_a_vnum_outside_its_zone_is_reported(world: Path) -> None:
    write_zone(world, 12, (3, 4), 1250)
    write_records(world, "wld", "12.wld", [1201, 1260])
    assert world_check.zone_problems(world) == [
        "wld/12.wld: #1260 is outside zone 12's range 1200-1250",
        "wld/12.wld: room #1260 is outside every zone; the boot stops",
    ]


def test_a_vnum_outside_its_file_number_block_is_reported(world: Path) -> None:
    write_records(world, "mob", "12.mob", [1201, 1301])
    assert world_check.zone_problems(world) == ["mob/12.mob: #1301 is outside 1200-1299, where the shaping editors look for it"]


def test_zone_tops_must_ascend(world: Path) -> None:
    write_zone(world, 12, (3, 4), 1199)
    problems = world_check.zone_problems(world)
    assert problems[0] == "zone 12's top 1199 is not above the previous zone's top 1199"


def test_a_conflict_marker_is_reported(world: Path) -> None:
    path = world / "wld" / "11.wld"
    path.write_text("<<<<<<< HEAD\n" + path.read_text(encoding="latin-1") + "=======\n>>>>>>> branch\n", encoding="latin-1")
    assert world_check.conflict_marker_problems([world]) == [f"{path}: a merge conflict marker is left in the file"]


def test_text_that_only_resembles_a_conflict_marker_is_not_one(world: Path) -> None:
    (world / "notes" / "zone-11.md").write_text("a <<<<<<< in a line\n========\n<<<<<<<<\n", encoding="utf-8")
    assert world_check.conflict_marker_problems([world]) == []


def test_a_zone_without_notes_is_reported(world: Path) -> None:
    (world / "notes" / "zone-12.md").unlink()
    assert world_check.notes_problems(world) == ["zone 12 has no notes file notes/zone-12.md"]


def test_a_zone_file_is_read_with_its_square_top_and_resets(world: Path) -> None:
    zone = read_zone(world / "zon" / "11.zon")
    assert (zone.number, zone.name, zone.map_square, zone.top) == (11, "Zone 11", (0, 0), 1199)
    assert zone.resets == [
        ResetRow("M", (0, 1101, 1102, 0, 100, 100, 1, 1), "a mob waits"),
        ResetRow("=", (), "Section title"),
        ResetRow("O", (0, 1101, 1102, 0, 100, 1), ""),
    ]
    assert [row.summary() for row in zone.resets] == ["mob 1101 to room 1102 (a mob waits)", "section Section title", "object 1101 to room 1102"]


def test_zone_ranges_start_above_the_previous_zones_top(world: Path) -> None:
    assert zone_vnum_ranges(read_zones(world)) == {11: (0, 1199), 12: (1200, 1299)}


def test_a_zones_own_vnums_are_its_hundred_cut_to_its_top(world: Path) -> None:
    write_zone(world, 12, (3, 4), 1250)
    assert zone_blocks(read_zones(world)) == {11: (1100, 1199), 12: (1200, 1250)}
