"""Wands and staves, used until their charges run out.

A wand's spell on its own user and on an object, its charges, a staff with none left, and a
spell that needs a victim cast with nobody to work on.
"""

from __future__ import annotations

import pytest

from rots_harness import world
from rots_harness.characters import IMPLEMENTOR, Character
from rots_harness.game_log import wait_for_log_count
from rots_harness.immortal_commands import give_new_object, goto
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
# The keyword the implementor and the wielder name the wielder by.
WIELDER_KEYWORD = WIELDER.name.lower()

# Zone 21's birch wand of insight, with 2 charges.
INSIGHT_WAND_VNUM = 2101
# Zone 21's oak wand of enchanting, with 1 charge.
ENCHANTING_WAND_VNUM = 2102
# Zone 21's ash staff of insight, with no charge left.
EMPTY_STAFF_VNUM = 2104
# Zone 21's elm staff of insight, with 1 charge.
CHARGED_STAFF_VNUM = 2105

# What the wielder reads as he holds the insight wand (wear_message, act_obj2.cpp).
GRAB_INSIGHT_WAND_REPLY = "You grab a birch wand of insight."
# What the wielder reads as he holds the enchanting wand.
GRAB_ENCHANTING_WAND_REPLY = "You grab an oak wand of enchanting."
# What the wielder reads as he holds the staff.
GRAB_STAFF_REPLY = "You grab an ash staff of insight."
# What the wielder reads as he holds the charged staff.
GRAB_CHARGED_STAFF_REPLY = "You grab an elm staff of insight."

# What the user reads when pointing the insight wand at himself (do_use, act_othe.cpp).
POINT_INSIGHT_REPLY = "You point a birch wand of insight at Testwielder."
# What the room reads then.
WIELDER_POINTS_INSIGHT = "Testwielder points a birch wand of insight at Testwielder."
# What the user reads when pointing the insight wand at the sword.
POINT_INSIGHT_AT_SWORD_REPLY = "You point a birch wand of insight at a plain steel sword."
# What the room reads then.
WIELDER_POINTS_INSIGHT_AT_SWORD = "Testwielder points a birch wand of insight at a plain steel sword."
# What the user reads when insight takes (spell_insight, mystic.cpp).
INSIGHT_REPLY = "The world seems to gain a few edges for you."
# What the user reads when insight finds him already insightful.
INSIGHT_ALREADY_REPLY = "He has insight already."
# What the user reads when pointing the enchanting wand at the sword.
POINT_ENCHANTING_REPLY = "You point an oak wand of enchanting at a plain steel sword."
# What the room reads then.
WIELDER_POINTS_ENCHANTING = "Testwielder points an oak wand of enchanting at a plain steel sword."
# What a caster of alignment 0 reads when enchant weapon takes (spell_enchant_weapon, mystic.cpp).
SWORD_GLOWS_REPLY = "A plain steel sword glows yellow."
# What the user reads when a wand has no charge left.
WAND_POWERLESS_REPLY = "The wand seems powerless."
# What the user reads when tapping the staff.
TAP_REPLY = "You tap an ash staff of insight three times on the ground."
# What the room reads then.
WIELDER_TAPS = "Testwielder taps an ash staff of insight three times on the ground."
# What the user reads when tapping the charged staff.
CHARGED_TAP_REPLY = "You tap an elm staff of insight three times on the ground."
# What the room reads then.
WIELDER_TAPS_CHARGED = "Testwielder taps an elm staff of insight three times on the ground."
# What the user reads when a staff has no charge left.
STAFF_POWERLESS_REPLY = "The staff seems powerless."
# What do_use answers when a staff or a wand whose spell needs a victim has none.
NOTHING_HAPPENS_REPLY = "Nothing seems to happen."
# The part of the SYSERR line spell_lacks_victim (spell_pa.cpp) logs when a spell that needs a
# victim has none.
NO_VICTIM_SYSERR_PART = "has nobody to work on"
# The stat line of insight's affect (do_stat_character, act_wiz.cpp).
INSIGHT_AFFECT_LINE = "insight               +50 to PERCEPTION"
# The stat line of the enchanted sword's affect (do_stat_object, act_wiz.cpp).
ENCHANTED_AFFECT_LINE = "Affections: +6 to OB"


def hand_object(implementor: GameSession, vnum: int, keyword: str) -> None:
    """Load an object into the implementor's inventory and give it to the wielder in the same room.

    Checks that the give reply names the wielder.
    """
    give_new_object(implementor, vnum, keyword, WIELDER_KEYWORD, shown_recipient=WIELDER.name)


def meet_the_wielder(server) -> tuple[GameSession, GameSession]:
    """Log in the implementor and the wielder and bring the implementor to the wand room.

    Returns (implementor, wielder).
    """
    implementor = server.login(IMPLEMENTOR.name)
    wielder = server.login(WIELDER.name)
    goto(implementor, ROOM_WAND_ROOM, "Wand Room")
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
    held = wielder.command("hold wand")
    assert held.contains(GRAB_INSIGHT_WAND_REPLY), held.text

    outcomes = [INSIGHT_REPLY, INSIGHT_ALREADY_REPLY, WAND_POWERLESS_REPLY, NOTHING_HAPPENS_REPLY]
    expected = (INSIGHT_REPLY, INSIGHT_ALREADY_REPLY, WAND_POWERLESS_REPLY)
    for expected_outcome in expected:
        used = wielder.command(f"use wand {WIELDER_KEYWORD}")
        reply = used.text
        assert reply.count(POINT_INSIGHT_REPLY) == 1, reply
        assert [outcome for outcome in outcomes if outcome in reply] == [expected_outcome], reply
        room = implementor.expect([WIELDER_POINTS_INSIGHT])
        assert room.count(WIELDER_POINTS_INSIGHT) == 1, room

    stat = read_stat(implementor, f"player {WIELDER_KEYWORD}")
    assert stat.count(INSIGHT_AFFECT_LINE) == 1, stat


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
    held = wielder.command("hold wand")
    assert held.contains(GRAB_ENCHANTING_WAND_REPLY), held.text

    outcomes = [SWORD_GLOWS_REPLY, WAND_POWERLESS_REPLY, NOTHING_HAPPENS_REPLY]
    for expected_outcome in (SWORD_GLOWS_REPLY, WAND_POWERLESS_REPLY):
        used = wielder.command("use wand sword")
        reply = used.text
        assert reply.count(POINT_ENCHANTING_REPLY) == 1, reply
        assert [outcome for outcome in outcomes if outcome in reply] == [expected_outcome], reply
        room = implementor.expect([WIELDER_POINTS_ENCHANTING])
        assert room.count(WIELDER_POINTS_ENCHANTING) == 1, room

    stat = read_stat(implementor, "obj sword", last_line=ENCHANTED_AFFECT_LINE)
    assert stat.count(ENCHANTED_AFFECT_LINE) == 1, stat


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, WIELDER)
def test_a_staff_with_no_charge_left_is_tapped_and_seems_powerless(server) -> None:
    """Pins tapping a staff whose spell needs a victim, with no charge left.

    The wielder holds the ash staff of insight, which has no charge, and taps it: he and the room
    read the tapping, and he reads that the staff seems powerless; no spell is cast, so nothing is
    logged. Nothing on this path draws.
    """
    implementor, wielder = meet_the_wielder(server)
    hand_object(implementor, EMPTY_STAFF_VNUM, "staff")
    held = wielder.command("hold staff")
    assert held.contains(GRAB_STAFF_REPLY), held.text

    used = wielder.command("use staff")
    reply = used.text
    assert reply.count(TAP_REPLY) == 1, reply
    assert reply.count(STAFF_POWERLESS_REPLY) == 1, reply
    assert NOTHING_HAPPENS_REPLY not in reply, reply
    room = implementor.expect([WIELDER_TAPS])
    assert room.count(WIELDER_TAPS) == 1, room


@pytest.mark.schedule("affect-hours")
@pytest.mark.expect_syserr(NO_VICTIM_SYSERR_PART)
@pytest.mark.characters(IMPLEMENTOR, WIELDER)
def test_a_wand_of_insight_pointed_at_a_sword_does_nothing_and_then_runs_out(server) -> None:
    """Fix pin (#561): a wand whose spell needs a victim, pointed at an object, does nothing.

    The wielder points the wand of insight, 2 charges, at a plain steel sword he carries, three
    times. Each charge is spent, and each time he reads that nothing seems to happen and the game
    logs one SYSERR; the third finds the wand powerless. He never gains insight, and the server
    is still up to show his stat. #561 made the spell refuse the missing victim; before it,
    insight used the missing victim and crashed the server. Nothing on this path draws: do_use
    has no roll, and spell_lacks_victim stops insight before its save.
    """
    implementor, wielder = meet_the_wielder(server)
    hand_object(implementor, INSIGHT_WAND_VNUM, "wand")
    hand_object(implementor, world.OBJECT_PLAIN_STEEL_SWORD, "sword")
    held = wielder.command("hold wand")
    assert held.contains(GRAB_INSIGHT_WAND_REPLY), held.text

    outcomes = [INSIGHT_REPLY, INSIGHT_ALREADY_REPLY, WAND_POWERLESS_REPLY, NOTHING_HAPPENS_REPLY]
    expected = (NOTHING_HAPPENS_REPLY, NOTHING_HAPPENS_REPLY, WAND_POWERLESS_REPLY)
    for expected_outcome in expected:
        used = wielder.command("use wand sword")
        reply = used.text
        assert reply.count(POINT_INSIGHT_AT_SWORD_REPLY) == 1, reply
        assert [outcome for outcome in outcomes if outcome in reply] == [expected_outcome], reply
        room = implementor.expect([WIELDER_POINTS_INSIGHT_AT_SWORD])
        assert room.count(WIELDER_POINTS_INSIGHT_AT_SWORD) == 1, room

    wait_for_log_count(server.handle.log_path, NO_VICTIM_SYSERR_PART, 2, exact=True)
    stat = read_stat(implementor, f"player {WIELDER_KEYWORD}")
    assert INSIGHT_AFFECT_LINE not in stat, stat


@pytest.mark.schedule("affect-hours")
@pytest.mark.expect_syserr(NO_VICTIM_SYSERR_PART)
@pytest.mark.characters(IMPLEMENTOR, WIELDER)
def test_a_staff_of_insight_with_a_charge_does_nothing_and_then_seems_powerless(server) -> None:
    """Fix pin (#561): a staff whose spell needs a victim casts it at nobody, and nothing happens.

    The wielder taps the elm staff of insight, 1 charge, twice. The first tap spends the charge:
    he reads that nothing seems to happen and the game logs one SYSERR. The second finds the
    staff powerless. He never gains insight, and the server is still up to show his stat. #561
    made the spell refuse the missing victim; before it, insight used the missing victim and
    crashed the server. Nothing on this path draws.
    """
    implementor, wielder = meet_the_wielder(server)
    hand_object(implementor, CHARGED_STAFF_VNUM, "staff")
    held = wielder.command("hold staff")
    assert held.contains(GRAB_CHARGED_STAFF_REPLY), held.text

    outcomes = [INSIGHT_REPLY, STAFF_POWERLESS_REPLY, NOTHING_HAPPENS_REPLY]
    for expected_outcome in (NOTHING_HAPPENS_REPLY, STAFF_POWERLESS_REPLY):
        used = wielder.command("use staff")
        reply = used.text
        assert reply.count(CHARGED_TAP_REPLY) == 1, reply
        assert [outcome for outcome in outcomes if outcome in reply] == [expected_outcome], reply
        room = implementor.expect([WIELDER_TAPS_CHARGED])
        assert room.count(WIELDER_TAPS_CHARGED) == 1, room

    wait_for_log_count(server.handle.log_path, NO_VICTIM_SYSERR_PART, 1, exact=True)
    stat = read_stat(implementor, f"player {WIELDER_KEYWORD}")
    assert INSIGHT_AFFECT_LINE not in stat, stat
