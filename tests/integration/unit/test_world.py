"""Unit tests for rots_harness.world: each vnum names a fixture of the test world, and each room
name is the one its room's file holds."""

from __future__ import annotations

from pathlib import Path
from typing import Callable

import pytest

from rots_harness import world, world_catalogue
from rots_harness.world_files import indexed_files

# Each object constant and the short description its fixture holds.
OBJECT_SHORT_DESCRIPTIONS = [
    (world.OBJECT_LEATHER_BAG, "a leather bag"),
    (world.OBJECT_LEATHER_CAP, "a leather cap"),
    (world.OBJECT_SICKLY_AMULET, "a sickly amulet"),
    (world.OBJECT_TAINTED_BREAD, "a piece of tainted bread"),
    (world.OBJECT_PLAIN_STEEL_SWORD, "a plain steel sword"),
    (world.OBJECT_STONE_GIRDLE, "a stone girdle"),
    (world.OBJECT_CHAIN_SHIRT, "a chain shirt"),
    (world.OBJECT_COPPER_BEAD, "a copper bead"),
    (world.OBJECT_PLAIN_SWORD, "a plain sword"),
    (world.OBJECT_DWARVEN_STONE, "a dwarven stone"),
    (world.OBJECT_LEATHER_HELM, "a leather helm"),
    (world.OBJECT_STRANGE_RELIC, "a strange relic"),
    (world.OBJECT_FLASK, "a flask"),
]
# Each mob constant and the short description its fixture holds.
MOB_SHORT_DESCRIPTIONS = [
    (world.MOB_INNKEEPER, "the harness innkeeper"),
    (world.MOB_BYSTANDER_ORC, "a bystander orc"),
    (world.MOB_SHINING_ANGEL, "a shining angel"),
    (world.MOB_STRAW_SCARECROW, "a straw scarecrow"),
    (world.MOB_MYSTERIOUS_TRAVELLER, "a mysterious traveller"),
    (world.MOB_PORTER, "a porter"),
]
# Each room name constant and the room it names.
ROOM_NAMES = [
    (world.ROOM_ARENA_CENTRE, world.ROOM_ARENA_CENTRE_NAME),
    (world.ROOM_HUMAN_START, world.ROOM_HUMAN_START_NAME),
]


def records_by_vnum(category: str, reader: Callable[[Path], list]) -> dict:
    """Every record of one world category, by vnum, read from the committed test world."""
    records = {}
    paths = indexed_files(world_catalogue.DEFAULT_WORLD_DIR, category)
    for path in paths:
        file_records = reader(path)
        for record in file_records:
            records[record.vnum] = record
    return records


def world_constants(prefix: str) -> set[str]:
    """The names of world.py's integer constants that start with prefix."""
    bindings = vars(world).items()
    return {name for name, value in bindings if name.startswith(prefix) and isinstance(value, int)}


def test_every_room_constant_is_a_room_of_the_test_world() -> None:
    rooms = records_by_vnum("wld", world_catalogue.read_rooms)
    room_constants = world_constants("ROOM_")
    missing = sorted(name for name in room_constants if getattr(world, name) not in rooms)
    assert not missing, f"no room in the test world for {missing}"


def test_every_object_and_mob_constant_is_checked_here() -> None:
    checked_objects = {vnum for vnum, _ in OBJECT_SHORT_DESCRIPTIONS}
    checked_mobs = {vnum for vnum, _ in MOB_SHORT_DESCRIPTIONS}
    object_constants = world_constants("OBJECT_")
    mob_constants = world_constants("MOB_")
    unchecked = sorted(name for name in object_constants if getattr(world, name) not in checked_objects)
    unchecked += sorted(name for name in mob_constants if getattr(world, name) not in checked_mobs)
    assert not unchecked, f"add {unchecked} to this file's tables"


@pytest.mark.parametrize("vnum, short_description", OBJECT_SHORT_DESCRIPTIONS)
def test_each_object_constant_names_its_fixture(vnum: int, short_description: str) -> None:
    objects = records_by_vnum("obj", world_catalogue.read_objects)
    assert vnum in objects, f"no object {vnum} in the test world"
    assert objects[vnum].short_description == short_description


@pytest.mark.parametrize("vnum, short_description", MOB_SHORT_DESCRIPTIONS)
def test_each_mob_constant_names_its_fixture(vnum: int, short_description: str) -> None:
    mobs = records_by_vnum("mob", world_catalogue.read_mobs)
    assert vnum in mobs, f"no mob {vnum} in the test world"
    assert mobs[vnum].short_description == short_description


@pytest.mark.parametrize("vnum, name", ROOM_NAMES)
def test_each_room_name_is_its_rooms(vnum: int, name: str) -> None:
    rooms = records_by_vnum("wld", world_catalogue.read_rooms)
    assert rooms[vnum].name == name
