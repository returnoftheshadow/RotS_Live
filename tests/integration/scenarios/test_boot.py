"""Boot and login: a harness lib boots, rotstool's characters log in, the server stops cleanly."""

from __future__ import annotations

import pytest

from rots_harness import world
from rots_harness.characters import IMPLEMENTOR, custom_mage

pytestmark = [pytest.mark.scenario, pytest.mark.boot]

# A mage with a custom profession split, waiting in the arena for the implementor to find.
MAGE = custom_mage(level=30, load_room=world.ROOM_ARENA_CENTRE)


@pytest.mark.characters(IMPLEMENTOR)
def test_an_implementor_logs_in_to_immortal_start(server) -> None:
    implementor = server.login(IMPLEMENTOR.name)
    looked = implementor.command("look")
    assert looked.room_name() == world.ROOM_IMMORTAL_START_NAME, looked.text


@pytest.mark.characters(IMPLEMENTOR, MAGE)
def test_a_custom_mage_logs_in_to_its_load_room_beside_the_implementor(server) -> None:
    implementor = server.login(IMPLEMENTOR.name)
    mage = server.login(MAGE.name)
    looked = mage.command("look")
    assert looked.room_name() == world.ROOM_ARENA_CENTRE_NAME, looked.text

    where = implementor.command(f"where {MAGE.name.lower()}")
    where_row = f"[ {world.ROOM_ARENA_CENTRE}] {world.ROOM_ARENA_CENTRE_NAME}"
    assert MAGE.name in where.text and where_row in where.text, where.text
