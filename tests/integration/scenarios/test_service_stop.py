"""A service stop: what SIGTERM saves for the players still logged in, read back by the next boot."""

from __future__ import annotations

import json
from pathlib import Path
import pytest

from rots_harness import world
from rots_harness.characters import IMPLEMENTOR, Character
from rots_harness.session import GameSession

pytestmark = [pytest.mark.scenario, pytest.mark.service_stop]

# A mortal who starts in the middle of the arena and walks to its western end.
WAYFARER = Character(name="Wayfarer", race="human", sex="male", level=10, load_room=world.ROOM_ARENA_CENTRE, class_name="warrior")
# Zone 11's leather cap, head armour, which the wayfarer wears.
CAP_VNUM = 1137
# How the game names the cap in equipment and messages.
CAP_NAME = "a leather cap"
# Zone 11's leather bag, which the wayfarer carries.
BAG_VNUM = 1136
# How the game names the bag in the inventory.
BAG_NAME = "a leather bag"
# The wear position a saved object records for the head (WEAR_HEAD, src/structs.h).
HEAD_WEAR_POSITION = 6
# The wear position a saved object records when it is carried rather than worn (MAX_WEAR).
CARRIED_WEAR_POSITION = 22


def account_file(lib_dir: Path, character: Character, kind: str) -> Path:
    """The character's <name>.<kind>.json in the harness account's folder."""
    paths = sorted((lib_dir / "accounts").glob(f"*/*/{character.name.lower()}.{kind}.json"))
    assert len(paths) == 1, paths
    return paths[0]


def saved_load_room(lib_dir: Path, character: Character) -> int:
    """The room the character's saved file brings them back to."""
    saved = json.loads(account_file(lib_dir, character, "character").read_text(encoding="utf-8"))
    return saved["state"]["load_room"]


def saved_wear_positions(lib_dir: Path, character: Character) -> dict[int, int]:
    """Each object vnum in the character's saved objects file, with the wear position it was saved at."""
    saved = json.loads(account_file(lib_dir, character, "objects").read_text(encoding="utf-8"))
    return {entry["item_number"]: entry["wear_pos"] for entry in saved["objects"]}


def hand_object(implementor: GameSession, vnum: int, keyword: str, recipient: Character) -> None:
    """Loads an object into the implementor's inventory and gives it to recipient, in the same room."""
    created = implementor.command(f"load obj {vnum}")
    assert created.contains("You create"), created.text
    given = implementor.command(f"give {keyword} {recipient.name.lower()}")
    assert given.contains(f"to {recipient.name}."), given.text


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, WAYFARER)
def test_a_service_stop_saves_where_a_player_stands_and_what_they_wear_and_carry(server) -> None:
    """Pins what a service stop saves for a player still logged in.

    The wayfarer is given a cap to wear and a bag to carry and walks from the arena's centre to
    its western end; then the server is stopped as a service stop stops it, with SIGTERM and the
    wayfarer still online, and booted again on the same lib. The wayfarer comes back in the west
    room, wearing the cap and carrying the bag. Under affect-hours the autosave never runs, so
    until the stop the saved files show neither the west room nor the cap and bag: the stop wrote
    them. Nothing on this path draws.

    Today's behaviour, before the stop signal only records the stop and the game loop saves
    (#535); it passes before and after that change.
    """
    implementor = server.login(IMPLEMENTOR.name)
    wayfarer = server.login(WAYFARER.name)
    arrived = implementor.command(f"goto {world.ROOM_ARENA_CENTRE}")
    assert arrived.room_name() == "Arena Centre", arrived.text
    hand_object(implementor, CAP_VNUM, "cap", WAYFARER)
    hand_object(implementor, BAG_VNUM, "bag", WAYFARER)
    worn = wayfarer.command("wear cap")
    assert worn.contains(CAP_NAME), worn.text
    walked = wayfarer.command("west")
    assert walked.room_name() == "Arena West", walked.text

    # The save on entering the game records the room's place in the room table, not its number,
    # so the file does not hold the centre either; it only must not hold the west room yet.
    assert saved_load_room(server.lib_dir, WAYFARER) != world.ROOM_ARENA_WEST
    before = saved_wear_positions(server.lib_dir, WAYFARER)
    assert CAP_VNUM not in before and BAG_VNUM not in before, before

    server.restart()

    assert saved_load_room(server.lib_dir, WAYFARER) == world.ROOM_ARENA_WEST
    assert saved_wear_positions(server.lib_dir, WAYFARER) == {CAP_VNUM: HEAD_WEAR_POSITION, BAG_VNUM: CARRIED_WEAR_POSITION}
    returned = server.login(WAYFARER.name)
    assert returned.command("look").room_name() == "Arena West"
    assert returned.command("equipment").contains(CAP_NAME)
    assert returned.command("inventory").contains(BAG_NAME)
