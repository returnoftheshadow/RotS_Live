from __future__ import annotations

import shutil
from pathlib import Path

import pytest

from rots_harness import world
from rots_harness.world_files import ResetRow, read_zone
from rots_harness.world_resets import (
    RESET_COMMENT,
    MobReset,
    ObjectReset,
    WorldResetError,
    reset_rows,
    resets_key_text,
    write_world_resets,
)

# The committed test world, copied before each test changes it.
WORLD_DIR = Path(__file__).resolve().parents[1] / "world"
# The target orc, a mob in the committed world.
ORC_VNUM = 1130
# An object in the committed world.
OBJECT_VNUM = 1130


@pytest.fixture
def world_copy(tmp_path: Path) -> Path:
    """A copy of the committed test world that a test may change."""
    copied = tmp_path / "world"
    shutil.copytree(WORLD_DIR, copied)
    return copied


def test_a_mob_reset_is_an_m_row_in_the_zone_that_owns_its_room(world_copy: Path) -> None:
    rows_before = read_zone(world_copy / "zon" / "13.zon").resets
    write_world_resets(world_copy, [MobReset(ORC_VNUM, world.ROOM_TRADE_HALL)])
    rows_after = read_zone(world_copy / "zon" / "13.zon").resets
    assert rows_after == [*rows_before, ResetRow("M", (0, ORC_VNUM, world.ROOM_TRADE_HALL, 0, 100, 100, 1, 1), RESET_COMMENT)]
    assert (world_copy / "zon" / "11.zon").read_bytes() == (WORLD_DIR / "zon" / "11.zon").read_bytes()


def test_an_object_reset_is_an_o_row_and_repeats_raise_the_floor_count(world_copy: Path) -> None:
    reset = ObjectReset(OBJECT_VNUM, world.ROOM_ARENA_CENTRE)
    write_world_resets(world_copy, [reset, reset])
    added = read_zone(world_copy / "zon" / "11.zon").resets[-2:]
    assert added == [
        ResetRow("O", (0, OBJECT_VNUM, world.ROOM_ARENA_CENTRE, 0, 100, 1), RESET_COMMENT),
        ResetRow("O", (0, OBJECT_VNUM, world.ROOM_ARENA_CENTRE, 0, 100, 2), RESET_COMMENT),
    ]


def test_the_zone_still_ends_with_its_closing_s(world_copy: Path) -> None:
    write_world_resets(world_copy, [MobReset(ORC_VNUM, world.ROOM_ARENA_CENTRE)])
    assert (world_copy / "zon" / "11.zon").read_text(encoding="latin-1").endswith(f"M 0 {ORC_VNUM} {world.ROOM_ARENA_CENTRE} 0 100 100 1 1 {RESET_COMMENT}\nS\n")


@pytest.mark.parametrize(
    "reset, named",
    [
        (MobReset(ORC_VNUM, 1198), "no room 1198"),
        (MobReset(1199, world.ROOM_ARENA_CENTRE), "no mob 1199"),
        (ObjectReset(1199, world.ROOM_ARENA_CENTRE), "no object 1199"),
    ],
)
def test_a_reset_naming_a_vnum_the_world_lacks_is_refused_before_any_file_changes(world_copy: Path, reset: MobReset | ObjectReset, named: str) -> None:
    with pytest.raises(WorldResetError, match=named):
        write_world_resets(world_copy, [MobReset(ORC_VNUM, world.ROOM_ARENA_CENTRE), reset])
    assert (world_copy / "zon" / "11.zon").read_bytes() == (WORLD_DIR / "zon" / "11.zon").read_bytes()


def test_no_resets_add_no_key_text_and_change_no_file(world_copy: Path) -> None:
    write_world_resets(world_copy, [])
    assert resets_key_text([]) == ""
    assert (world_copy / "zon" / "11.zon").read_bytes() == (WORLD_DIR / "zon" / "11.zon").read_bytes()


def test_the_key_text_is_the_rows_one_per_line() -> None:
    resets = [MobReset(ORC_VNUM, world.ROOM_ARENA_CENTRE), ObjectReset(OBJECT_VNUM, world.ROOM_ARENA_CENTRE)]
    assert resets_key_text(resets) == "".join(f"{row}\n" for row in reset_rows(resets))
