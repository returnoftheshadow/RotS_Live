"""Mobs and floor objects one test's world places in rooms at boot, written as reset rows into the
zone that owns each room."""

from __future__ import annotations

from collections import Counter
from dataclasses import dataclass
from pathlib import Path
from typing import Sequence, Union

from rots_harness.world_files import (
    RESET_END_COMMAND,
    WORLD_ENCODING,
    ZONE_CATEGORY,
    fixture_vnums,
    indexed_files,
    read_world_text,
    read_zone,
    zone_vnum_ranges,
)

# The comment every written reset row ends with, so a kept run's zone file shows which rows the test added.
RESET_COMMENT = "placed for this test"


class WorldResetError(ValueError):
    """A world reset names a room, mob or object the test world does not have."""


@dataclass(frozen=True)
class MobReset:
    """A mob the world places in a room when the server boots."""

    # Virtual number of the mob.
    mob_vnum: int
    # Virtual number of the room it is placed in.
    room_vnum: int

    def reset_row(self, ordinal: int) -> str:
        """The zone reset row that loads the mob; each row loads one mob, so ordinal is unused."""
        # if-flag 0, no world limit, always (100), difficulty 100, one alive from this row, trophy 1.
        return f"M 0 {self.mob_vnum} {self.room_vnum} 0 100 100 1 1 {RESET_COMMENT}"


@dataclass(frozen=True)
class ObjectReset:
    """An object the world places on a room's floor when the server boots."""

    # Virtual number of the object.
    object_vnum: int
    # Virtual number of the room whose floor it lies on.
    room_vnum: int

    def reset_row(self, ordinal: int) -> str:
        """The zone reset row that loads the object; ordinal counts this object's resets in this room
        from 1, and the row loads only while the floor holds fewer than ordinal of the object."""
        # if-flag 0, no world limit, always (100), at most ordinal on the floor.
        return f"O 0 {self.object_vnum} {self.room_vnum} 0 100 {ordinal} {RESET_COMMENT}"


# One mob or object a test's world places at boot.
WorldReset = Union[MobReset, ObjectReset]


def reset_rows(resets: Sequence[WorldReset]) -> list[str]:
    """The reset rows for resets, in order; repeated object resets in one room each load one more copy."""
    ordinals: Counter[WorldReset] = Counter()
    rows = []
    for reset in resets:
        ordinals[reset] += 1
        rows.append(reset.reset_row(ordinals[reset]))
    return rows


def resets_key_text(resets: Sequence[WorldReset]) -> str:
    """The resets as text for a template cache key: empty for none, otherwise one row per line."""
    return "".join(f"{row}\n" for row in reset_rows(resets))


def category_vnums(world_dir: Path, category: str) -> set[int]:
    """Every fixture vnum in the files a category's index lists."""
    vnums: set[int] = set()
    for path in indexed_files(world_dir, category):
        vnums.update(fixture_vnums(read_world_text(path)))
    return vnums


def check_resets(world_dir: Path, resets: Sequence[WorldReset]) -> None:
    """Raises WorldResetError naming the first room, mob or object vnum the world lacks."""
    rooms = category_vnums(world_dir, "wld")
    mobs = category_vnums(world_dir, "mob")
    objects = category_vnums(world_dir, "obj")
    for reset in resets:
        if reset.room_vnum not in rooms:
            raise WorldResetError(f"{reset}: the test world has no room {reset.room_vnum}")
        if isinstance(reset, MobReset) and reset.mob_vnum not in mobs:
            raise WorldResetError(f"{reset}: the test world has no mob {reset.mob_vnum}")
        if isinstance(reset, ObjectReset) and reset.object_vnum not in objects:
            raise WorldResetError(f"{reset}: the test world has no object {reset.object_vnum}")


def with_rows_before_end(zone_text: str, rows: list[str], context: str) -> str:
    """zone_text with rows inserted before its closing S line, in the file's own line ends; raises
    WorldResetError when its last line that is not blank is not the closing S."""
    lines = zone_text.splitlines(keepends=True)
    index = len(lines) - 1
    while index >= 0 and not lines[index].strip():
        index -= 1
    if index < 0 or not lines[index].strip().startswith(RESET_END_COMMAND):
        raise WorldResetError(f"{context}: no closing {RESET_END_COMMAND} line to add reset rows before")
    line_end = "\r\n" if lines[index].endswith("\r\n") else "\n"
    inserted = [f"{row}{line_end}" for row in rows]
    return "".join(lines[:index] + inserted + lines[index:])


def write_world_resets(world_dir: Path, resets: Sequence[WorldReset]) -> None:
    """Adds a reset row for each of resets to the zone file that owns its room, in world_dir.

    Raises WorldResetError, before changing any file, when a reset names a room, mob or object the
    world lacks.
    """
    if not resets:
        return
    check_resets(world_dir, resets)
    zone_paths = indexed_files(world_dir, ZONE_CATEGORY)
    zones = [read_zone(path) for path in zone_paths]
    ranges = zone_vnum_ranges(zones)
    rows_by_zone: dict[int, list[str]] = {}
    for reset, row in zip(resets, reset_rows(resets)):
        owner = next((number for number, (low, high) in ranges.items() if low <= reset.room_vnum <= high), None)
        if owner is None:
            raise WorldResetError(f"{reset}: no zone holds room {reset.room_vnum}")
        rows_by_zone.setdefault(owner, []).append(row)
    for zone, path in zip(zones, zone_paths):
        if zone.number in rows_by_zone:
            # Bytes, not read_text, so the file's own line ends survive the rewrite.
            text = with_rows_before_end(path.read_bytes().decode(WORLD_ENCODING), rows_by_zone[zone.number], str(path))
            path.write_bytes(text.encode(WORLD_ENCODING))
