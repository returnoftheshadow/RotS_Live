"""Wands and staves: a wand's spell on its own user and on an object, its charges, and a staff
with none left."""

from __future__ import annotations

import pytest

from rots_harness import world
from rots_harness.characters import IMPLEMENTOR, Character
from rots_harness.session import GameSession
from rots_harness.stat_reading import read_stat

pytestmark = [pytest.mark.scenario, pytest.mark.wands]

# Room 2102 in zone 21, where wands are tried out.
ROOM_WAND_ROOM = 2102
# A level-20 human warrior in the wand room, of alignment 0: neither good nor evil.
WIELDER = Character(
    name="Testwielder",
    race="human",
    sex="male",
    level=20,
    load_room=ROOM_WAND_ROOM,
    class_name="warrior",
)

# Zone 21's birch wand of insight, with 2 charges.
INSIGHT_WAND_VNUM = 2101
# Zone 21's oak wand of enchanting, with 1 charge.
ENCHANTING_WAND_VNUM = 2102
# Zone 21's ash staff of insight, with no charge left.
EMPTY_STAFF_VNUM = 2104

# What the user reads when pointing the insight wand at himself (do_use, act_othe.cpp).
POINT_INSIGHT_MARKER = "You point a birch wand of insight at Testwielder."
# What the room reads then.
ROOM_POINT_INSIGHT_MARKER = "Testwielder points a birch wand of insight at Testwielder."
# What the user reads when insight takes (spell_insight, mystic.cpp).
INSIGHT_MARKER = "The world seems to gain a few edges for you."
# What the user reads when insight finds him already insightful.
INSIGHT_ALREADY_MARKER = "He has insight already."
# What the user reads when pointing the enchanting wand at the sword.
POINT_ENCHANTING_MARKER = "You point an oak wand of enchanting at a plain steel sword."
# What the room reads then.
ROOM_POINT_ENCHANTING_MARKER = "Testwielder points an oak wand of enchanting at a plain steel sword."
# What a caster of alignment 0 reads when enchant weapon takes (spell_enchant_weapon, mystic.cpp).
GLOWS_MARKER = "A plain steel sword glows yellow."
# What the user reads when a wand has no charge left.
WAND_POWERLESS_MARKER = "The wand seems powerless."
# What the user reads when tapping the staff.
TAP_MARKER = "You tap an ash staff of insight three times on the ground."
# What the room reads then.
ROOM_TAP_MARKER = "Testwielder taps an ash staff of insight three times on the ground."
# What the user reads when a staff has no charge left.
STAFF_POWERLESS_MARKER = "The staff seems powerless."
# What do_use answers when a staff or a wand whose spell needs a victim has none.
NOTHING_HAPPENS_MARKER = "Nothing seems to happen."
# The stat line of insight's affect (do_stat_character, act_wiz.cpp).
INSIGHT_AFFECT_MARKER = "insight               +50 to PERCEPTION"
# The stat line of the enchanted sword's affect (do_stat_object, act_wiz.cpp).
ENCHANTED_AFFECT_MARKER = "Affections: +6 to OB"


def hand_object(implementor: GameSession, vnum: int, keyword: str) -> None:
    """Loads an object into the implementor's inventory and gives it to the wielder, in the same room."""
    created = implementor.command(f"load obj {vnum}")
    assert created.contains("You create"), created.text
    given = implementor.command(f"give {keyword} {WIELDER.name.lower()}")
    assert given.contains(f"to {WIELDER.name}."), given.text


def meet_the_wielder(server) -> tuple:
    """Logs in the implementor and the wielder and brings the implementor to the wand room; returns
    (implementor, wielder)."""
    implementor = server.login(IMPLEMENTOR.name)
    wielder = server.login(WIELDER.name)
    arrived = implementor.command(f"goto {ROOM_WAND_ROOM}")
    assert arrived.room_name() == "Wand Room", arrived.text
    return implementor, wielder


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, WIELDER)
def test_a_wand_of_insight_pointed_at_its_user_gives_insight_once_and_then_runs_out(server) -> None:
    """Pins a wand of a spell that needs a victim, used the ordinary way: pointed at a character.

    The wielder holds the wand of insight, 2 charges, and points it at himself three times. The
    first charge gives him insight, the second finds him insightful already, and the third finds
    the wand powerless; the room reads each pointing. Nothing on this path draws: do_use has no
    roll, and spell_insight rolls a save only for a victim other than the caster.
    """
    implementor, wielder = meet_the_wielder(server)
    hand_object(implementor, INSIGHT_WAND_VNUM, "wand")
    assert wielder.command("hold wand").contains("You grab a birch wand of insight.")

    outcomes = [INSIGHT_MARKER, INSIGHT_ALREADY_MARKER, WAND_POWERLESS_MARKER, NOTHING_HAPPENS_MARKER]
    expected = (INSIGHT_MARKER, INSIGHT_ALREADY_MARKER, WAND_POWERLESS_MARKER)
    for expected_outcome in expected:
        reply = wielder.command(f"use wand {WIELDER.name.lower()}").text
        assert reply.count(POINT_INSIGHT_MARKER) == 1, reply
        assert [outcome for outcome in outcomes if outcome in reply] == [expected_outcome], reply
        room = implementor.expect([ROOM_POINT_INSIGHT_MARKER])
        assert room.count(ROOM_POINT_INSIGHT_MARKER) == 1, room

    stat = read_stat(implementor, f"player {WIELDER.name.lower()}")
    assert stat.count(INSIGHT_AFFECT_MARKER) == 1, stat


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, WIELDER)
def test_a_wand_of_enchanting_pointed_at_a_sword_enchants_it_and_then_runs_out(server) -> None:
    """Pins a wand of an object spell pointed at an object.

    The wielder holds the wand of enchanting, 1 charge, and points it at a plain steel sword he
    carries, twice. The first charge makes the sword glow yellow (the wielder is neither good nor
    evil) and gives it +6 OB; the second finds the wand powerless. Nothing on this path draws.
    """
    implementor, wielder = meet_the_wielder(server)
    hand_object(implementor, ENCHANTING_WAND_VNUM, "wand")
    hand_object(implementor, world.OBJECT_PLAIN_STEEL_SWORD, "sword")
    assert wielder.command("hold wand").contains("You grab an oak wand of enchanting.")

    outcomes = [GLOWS_MARKER, WAND_POWERLESS_MARKER, NOTHING_HAPPENS_MARKER]
    for expected_outcome in (GLOWS_MARKER, WAND_POWERLESS_MARKER):
        reply = wielder.command("use wand sword").text
        assert reply.count(POINT_ENCHANTING_MARKER) == 1, reply
        assert [outcome for outcome in outcomes if outcome in reply] == [expected_outcome], reply
        room = implementor.expect([ROOM_POINT_ENCHANTING_MARKER])
        assert room.count(ROOM_POINT_ENCHANTING_MARKER) == 1, room

    stat = read_stat(implementor, "obj sword", last_line=ENCHANTED_AFFECT_MARKER)
    assert stat.count(ENCHANTED_AFFECT_MARKER) == 1, stat


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, WIELDER)
def test_a_staff_with_no_charge_left_is_tapped_and_seems_powerless(server) -> None:
    """Pins tapping a staff whose spell needs a victim, with no charge left.

    The wielder holds the ash staff of insight, which has no charge, and taps it: he and the room
    read the tapping, and he reads that the staff seems powerless; no spell is cast. The staff
    has no charge: one cast at nobody logs a SYSERR, which fails any scenario. Nothing on this
    path draws.
    """
    implementor, wielder = meet_the_wielder(server)
    hand_object(implementor, EMPTY_STAFF_VNUM, "staff")
    assert wielder.command("hold staff").contains("You grab an ash staff of insight.")

    reply = wielder.command("use staff").text
    assert reply.count(TAP_MARKER) == 1, reply
    assert reply.count(STAFF_POWERLESS_MARKER) == 1, reply
    assert NOTHING_HAPPENS_MARKER not in reply, reply
    room = implementor.expect([ROOM_TAP_MARKER])
    assert room.count(ROOM_TAP_MARKER) == 1, room
