"""A service stop: what SIGTERM saves for the players still logged in, read back by the next boot."""

from __future__ import annotations

import json
from pathlib import Path

import pytest

from rots_harness import world
from rots_harness.characters import IMPLEMENTOR, Character
from rots_harness.immortal_commands import give_new_object, goto
from rots_harness.saved_files import CARRIED_WEAR_POSITION, HEAD_WEAR_POSITION, account_file, saved_wear_positions

pytestmark = [pytest.mark.scenario, pytest.mark.service_stop]

# A mortal who starts in the middle of the arena and walks to its western end.
WAYFARER = Character(name="Wayfarer", race="human", sex="male", level=10, load_room=world.ROOM_ARENA_CENTRE, class_name="warrior")


def saved_load_room(lib_dir: Path, character: Character) -> int:
    """The room the character's saved file brings them back to."""
    character_path = account_file(lib_dir, character.name, "character")
    saved_text = character_path.read_text(encoding="utf-8")
    saved = json.loads(saved_text)
    return saved["state"]["load_room"]


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, WAYFARER)
def test_a_service_stop_saves_where_a_player_stands_and_what_they_wear_and_carry(server) -> None:
    """Pins what a service stop saves for a player still logged in (#535).

    The wayfarer is given a cap to wear and a bag to carry and walks from the arena's centre to
    its western end; then the server is stopped as a service stop stops it, with SIGTERM and the
    wayfarer still online, and booted again on the same lib. The wayfarer comes back in the west
    room, wearing the cap and carrying the bag. Under affect-hours the autosave never runs, so
    until the stop the saved files show neither the west room nor the cap and bag: the stop wrote
    them. Nothing on this path draws.
    """
    implementor = server.login(IMPLEMENTOR.name)
    wayfarer = server.login(WAYFARER.name)
    goto(implementor, world.ROOM_ARENA_CENTRE, "Arena Centre")
    give_new_object(implementor, world.OBJECT_LEATHER_CAP, "cap", WAYFARER.name.lower(), shown_recipient=WAYFARER.name)
    give_new_object(implementor, world.OBJECT_LEATHER_BAG, "bag", WAYFARER.name.lower(), shown_recipient=WAYFARER.name)
    worn = wayfarer.command("wear cap")
    assert worn.contains(world.OBJECT_LEATHER_CAP_SHORT_DESCRIPTION), worn.text
    walked = wayfarer.command("west")
    assert walked.room_name() == world.ROOM_ARENA_WEST_NAME, walked.text

    # The entry save stores a room-table index, not a vnum (#493); it only must not be the west room yet.
    assert saved_load_room(server.lib_dir, WAYFARER) != world.ROOM_ARENA_WEST
    before = saved_wear_positions(server.lib_dir, WAYFARER.name)
    assert world.OBJECT_LEATHER_CAP not in before and world.OBJECT_LEATHER_BAG not in before, before

    server.restart()

    assert saved_load_room(server.lib_dir, WAYFARER) == world.ROOM_ARENA_WEST
    assert saved_wear_positions(server.lib_dir, WAYFARER.name) == {world.OBJECT_LEATHER_CAP: HEAD_WEAR_POSITION, world.OBJECT_LEATHER_BAG: CARRIED_WEAR_POSITION}
    returned = server.login(WAYFARER.name)
    assert returned.command("look").room_name() == world.ROOM_ARENA_WEST_NAME
    assert returned.command("equipment").contains(world.OBJECT_LEATHER_CAP_SHORT_DESCRIPTION)
    assert returned.command("inventory").contains(world.OBJECT_LEATHER_BAG_SHORT_DESCRIPTION)
