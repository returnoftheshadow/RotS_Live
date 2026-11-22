"""Divination's report: the room's number, sector and flags, each exit with any door, and who and
what is in the room; on a crowded floor the object list is cut short with "and more."."""

from __future__ import annotations

import pytest

from rots_harness import world
from rots_harness.casting import CAST_TIMEOUT_SECONDS, LOST_CONCENTRATION, NO_ENERGY
from rots_harness.characters import IMPLEMENTOR, Character, StoredValues
from rots_harness.game_text import UNKNOWN_SPELL_REPLY
from rots_harness.immortal_commands import goto
from rots_harness.replies import reply_to
from rots_harness.session import GameSession
from rots_harness.text_lines import stripped_lines
from rots_harness.world_resets import MobReset, ObjectReset

pytestmark = [pytest.mark.scenario, pytest.mark.divination]

# The seer's room (tests/integration/world/wld/24.wld): an open exit north to the cluttered room
# and a closed door east (keyword "hatch", key 0) to the store cupboard.
ROOM_SEERS_ROOM = 2408
ROOM_CLUTTERED_ROOM = 2409
CLUTTERED_ROOM_NAME = "Cluttered Room"
# What the seer's room holds from boot: the practice dummy (mob/24.mob) and a flask
# (world.OBJECT_FLASK).
DUMMY_VNUM = 2404

# The base and current mana the seer is stored with, enough for many casts; a lost concentration
# costs half of the spell's mana (do_cast, spell_pa.cpp).
SEER_MANA = 500
# A level-30 human mystic in the seer's room who knows divination fully. Divination is a level-13
# mystic spell that targets nothing (skills[] in consts.cpp).
SEER = Character(
    name="Testseer",
    race="human",
    sex="female",
    level=30,
    load_room=ROOM_SEERS_ROOM,
    class_name="mystic",
    skills=(("divination", 100),),
    mana=SEER_MANA,
    stored=StoredValues(maxstamina=SEER_MANA),
)

# The report in the seer's room (spell_divination, mystic.cpp), line by line: the header with the
# room's number, sector ("Floor" for sector 0) and flags ("<NONE>"); each exit as
# "%5s: to <room> (#<vnum>)", a door adding its keyword and key; the people, in the room's order,
# where char_to_room (handler.cpp) appends the seer after the dummy placed at boot; the objects.
SEERS_ROOM_REPORT = [
    "You feel confident about your location.",
    "(#2408) [ Floor, <NONE>], Exits are:",
    "north: to Cluttered Room (#2409)",
    "east: to Store Cupboard (#2410)",
    "door 'hatch', key 'None'.",
    "Living beings in the room:",
    f"a practice dummy, {SEER.name}.",
    "Objects in the room:",
    "a flask.",
]
# The report's header lines in the cluttered room, which has one open exit back south.
CLUTTERED_ROOM_HEADER = [
    "You feel confident about your location.",
    "(#2409) [ Floor, <NONE>], Exits are:",
    "south: to Seer's Room (#2408)",
    "Living beings in the room:",
    f"{SEER.name}.",
    "Objects in the room:",
]

# Leather caps (world.OBJECT_LEATHER_CAP), dropped by the implementor until the cluttered room's
# object list passes divination's limit of 999 characters (DIVINATION_LIST_LIMIT, spells.h): each
# cap adds "a leather cap, " (15 characters), so 80 make 22 + 80 * 15 = 1222 with the header.
CAPS_DROPPED = 80
CAP_ENTRY = "a leather cap"
CAP_CREATED = "You create a leather cap."
# The list keeps the entries after which "and more.\n\r" (11 characters) still fits the limit,
# then ends with it (limit_divination_list, mystic.cpp): 22 + 64 * 15 = 982 and
# 982 + 11 = 993 <= 999, while 22 + 65 * 15 + 11 = 1008 does not fit.
LIST_CUT = "and more."
CAPS_KEPT = 64
LIST_LIMIT = 999

# The last line of every report: the object list, whatever it holds.
OBJECTS_HEADER = "Objects in the room:"


def divine(seer: GameSession) -> list:
    """Casts divination, repeating it after a lost concentration; returns the report's lines from
    its first line on."""
    while True:
        reply = reply_to(seer, "cast 'divination'", [OBJECTS_HEADER, LOST_CONCENTRATION, NO_ENERGY, UNKNOWN_SPELL_REPLY], timeout=CAST_TIMEOUT_SECONDS)
        if LOST_CONCENTRATION in reply:
            continue
        lines = stripped_lines(reply)
        assert SEERS_ROOM_REPORT[0] in lines, reply
        return lines[lines.index(SEERS_ROOM_REPORT[0]):]


@pytest.mark.schedule("affect-hours")
@pytest.mark.world_resets(MobReset(DUMMY_VNUM, ROOM_SEERS_ROOM), ObjectReset(world.OBJECT_FLASK, ROOM_SEERS_ROOM))
@pytest.mark.characters(SEER)
def test_divination_reports_the_room_its_exits_and_who_and_what_is_there(server) -> None:
    """In the seer's room divination reports the room's number, sector and flags, its two exits with
    the hatch's keyword and key, the seer and the dummy, and the flask, line for line.
    The concentration roll is the one draw; a lost cast is repeated."""
    seer = server.login(SEER.name)

    report = divine(seer)
    assert report[:len(SEERS_ROOM_REPORT)] == SEERS_ROOM_REPORT, report


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, SEER)
def test_divination_cuts_a_crowded_object_list_short_with_and_more(server) -> None:
    """Fix pin (#534): on a floor of 80 leather caps the object list keeps 64 of them and ends
    "and more.", within 999 characters, and the header and the people above it are not cut. #534
    made the list stop at "and more."; before it, a crowded room overran divination's 1000-byte
    buffer. The concentration roll is the one draw; a lost cast is repeated."""
    implementor = server.login(IMPLEMENTOR.name)
    seer = server.login(SEER.name)
    goto(implementor, ROOM_CLUTTERED_ROOM, CLUTTERED_ROOM_NAME)
    for _cap in range(CAPS_DROPPED):
        implementor.send_line(f"load obj {world.OBJECT_LEATHER_CAP}")
        implementor.expect([CAP_CREATED])
    implementor.command("drop all")
    implementor.command(f"goto {ROOM_SEERS_ROOM}")
    moved = seer.command("north")
    assert moved.room_name() == CLUTTERED_ROOM_NAME, moved.text

    report = divine(seer)
    assert report[:len(CLUTTERED_ROOM_HEADER)] == CLUTTERED_ROOM_HEADER, report
    object_list = report[len(CLUTTERED_ROOM_HEADER)]
    assert object_list.endswith(LIST_CUT), report
    assert object_list.count(CAP_ENTRY) == CAPS_KEPT, report
    assert len(OBJECTS_HEADER) + 2 + len(object_list) + 2 <= LIST_LIMIT, report
