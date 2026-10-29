"""Tame and whistle bugs: strict xfails of the correct behaviour, each failing on today's code."""

from __future__ import annotations

import re

import pytest

from rots_harness import world
from rots_harness.characters import IMPLEMENTOR, Character
from test_tame import (
    ACTIVITY_AFFECT_PATTERN,
    BOAR_KEYWORD,
    BOAR_ZONE,
    FLED_MARKER,
    SELF_TAME_MARKER,
    SETTLE_SECONDS,
    TAME_REPLY_MARKERS,
    TAME_TIMEOUT,
    TAMER,
    TAMER_AT_GATE,
    affect_bits,
    boar_rooms,
    check_tame_chance,
    goto,
    set_tame_on_hardy_orc,
    stat_mob,
    tame_and_walk_to_hub,
    tame_boar,
    whistle,
)

pytestmark = [pytest.mark.scenario, pytest.mark.tame]

# A level-91 human ranger who knows tame fully. It is an immortal because only an immortal's quit
# saves its followers (do_quit, act_othe.cpp): a mortal's quit releases them before the save. An
# immortal enters the game at the immortal start whatever its load room.
WARDEN = Character(
    name="Testwarden",
    race="human",
    sex="male",
    level=91,
    load_room=world.ROOM_IMMORTAL_START,
    class_name="ranger",
    skills=(("tame", 100),),
)

# The whole reply `zreset 18` should give: the zone's number, as the implementor typed it and as
# the log line beside it prints it.
ZONE_RESET_BY_NUMBER_MARKER = "Reset zone 18: Harness boar pen."
# A mob's current and maximum moves in `stat mob` (do_stat_character, act_wiz.cpp).
MOVE_PATTERN = re.compile(r"Move :\[(-?\d+)/(-?\d+)\+")


@pytest.mark.xfail(strict=True, raises=AssertionError, reason="`tame <own name>` answers \"Aren't we funny today...\"")
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, TAMER_AT_GATE)
def test_tame_on_yourself_answers_with_its_own_joke(server) -> None:
    """Bug pin: `tame <own name>` never reaches do_tame's answer for taming yourself.

    do_tame (ranger.cpp:1554-1557) answers "Aren't we funny today..." when victim == ch, but the
    animals-only check before it (ranger.cpp:1524-1527) refuses every player, the tamer
    included, with "You can only tame animals.", so the branch never runs. No roll is on the
    path.
    """
    server.login(IMPLEMENTOR.name)
    tamer = server.login(TAMER_AT_GATE.name)

    tamer.send_line(f"tame {TAMER_AT_GATE.name.lower()}")
    reply = tamer.expect(TAME_REPLY_MARKERS, timeout=TAME_TIMEOUT) + tamer.drain(SETTLE_SECONDS)
    assert SELF_TAME_MARKER in reply, reply


@pytest.mark.xfail(strict=True, raises=AssertionError, reason="a tame that flees on a whistle is recalled only by the next whistle")
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, TAMER)
def test_a_tame_that_flees_on_a_whistle_is_not_recalled_by_the_same_whistle(server) -> None:
    """Bug pin: a whistle recalls the fighting tame it has just made flee.

    do_whistle (ranger.cpp:1687) walks the zone's rooms in ascending index and makes a fighting
    tame flee (ranger.cpp:1706-1711), leaving the recall for the next whistle (#273). When the
    tame flees into a room the walk has not reached yet, the walk finds it there, standing, and
    applies the recall (ranger.cpp:1717-1727): the activity affect, which sets HUNT, sends it
    straight back to its master. Every spoke comes after the hub in the world, so every flee here
    lands in such a room; a flee into an earlier room would not be recalled, so the outcome
    depends on room order. The boar's hit and the flee draw, but none of their draws decides
    this outcome (set_tame_on_hardy_orc and test_whistle_makes_a_fighting_tame_flee_its_fight in
    test_tame.py); the tame's success roll is a gating roll that tame_boar repeats.
    """
    implementor = server.login(IMPLEMENTOR.name)
    tamer = server.login(TAMER.name)

    tame_and_walk_to_hub(implementor, tamer)
    set_tame_on_hardy_orc(implementor, tamer)
    whistled = whistle(tamer)
    assert FLED_MARKER in whistled, whistled

    fled = stat_mob(implementor, BOAR_KEYWORD)
    assert not ACTIVITY_AFFECT_PATTERN.search(fled), fled
    assert "HUNT" not in affect_bits(fled), fled


@pytest.mark.xfail(strict=True, raises=AssertionError, reason="a tame restored at login keeps the maximum moves it had when saved")
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, WARDEN)
def test_a_tame_restored_at_login_keeps_its_tamed_maximum_moves(server) -> None:
    """Bug pin: a tame restored at its owner's login has 50 maximum moves fewer than when saved.

    do_tame (ranger.cpp:1636) adds 50 to the tame's maximum moves, and stop_follower
    (handler.cpp:1077-1080) takes 50 off whenever a tame is released. Crash_follower_load's
    FOL_TAMED case (objsave.cpp:1137-1167) restores the saved tame as a fresh copy of the mob
    with the tame affect but without the 50, so it comes back with its prototype's maximum, and
    50 below it once released. The owner is an immortal, whose quit saves the followers in its
    room (do_quit, act_othe.cpp:111-112). The copy the quit leaves in the world (see
    test_an_immortals_quit_leaves_no_copy_of_its_tame_behind) is purged before the login, so
    `stat mob` finds the restored one. The tame's success roll is the one draw, a gating roll
    that tame_boar repeats.
    """
    implementor = server.login(IMPLEMENTOR.name)
    warden = server.login(WARDEN.name)

    wild_move = MOVE_PATTERN.search(stat_mob(implementor, BOAR_KEYWORD))
    check_tame_chance(implementor, warden, WARDEN)
    goto(warden, world.ROOM_BOAR_PEN)
    tame_boar(warden)
    tamed_stat = stat_mob(implementor, BOAR_KEYWORD)
    tamed_move = MOVE_PATTERN.search(tamed_stat)
    assert wild_move and tamed_move, tamed_stat
    assert int(tamed_move.group(2)) == int(wild_move.group(2)) + 50, tamed_stat

    warden.quit()
    goto(implementor, world.ROOM_BOAR_PEN)
    purged = implementor.command(f"purge {BOAR_KEYWORD}")
    assert purged.contains("Ok."), purged.text
    assert boar_rooms(implementor) == []

    server.login(WARDEN.name)
    assert len(boar_rooms(implementor)) == 1
    restored = stat_mob(implementor, BOAR_KEYWORD)
    assert f"Master is: {WARDEN.name}," in restored, restored
    restored_move = MOVE_PATTERN.search(restored)
    assert restored_move is not None, restored
    assert int(restored_move.group(2)) == int(tamed_move.group(2)), restored


@pytest.mark.xfail(strict=True, raises=AssertionError, reason="an immortal's quit takes its saved tame out of the world, so its login brings back the only copy")
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, WARDEN)
def test_an_immortals_quit_leaves_no_copy_of_its_tame_behind(server) -> None:
    """Bug pin: an immortal who quits with a tame and logs in again has duplicated it.

    do_quit (act_othe.cpp:112) saves an immortal's followers with Crash_crashsave and then
    extracts the immortal (act_othe.cpp:119), whose die_follower (handler.cpp:1991) releases the
    tame in the room; the rent path (Crash_rentsave, objsave.cpp:1559-1560) takes the followers
    out of the world after saving them. The login restores the saved tame (Crash_follower_load),
    so the boar exists twice: the released one in the pen and the restored one with its owner.
    The tame's success roll is the one draw, a gating roll that tame_boar repeats.
    """
    implementor = server.login(IMPLEMENTOR.name)
    warden = server.login(WARDEN.name)

    check_tame_chance(implementor, warden, WARDEN)
    goto(warden, world.ROOM_BOAR_PEN)
    tame_boar(warden)
    warden.quit()
    server.login(WARDEN.name)
    assert len(boar_rooms(implementor)) == 1


@pytest.mark.xfail(strict=True, raises=AssertionError, reason="`zreset 18` answers with zone 18, the number the implementor typed")
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR)
def test_zreset_names_the_zone_by_its_number(server) -> None:
    """Bug pin: `zreset 18` answers with the zone's place in the zone table, not its number.

    do_zreset (act_wiz.cpp:2138) finds the zone by its number but prints the loop index, its
    place in the zone table (2 for zone 18 in the test world), while the log line beside it
    (act_wiz.cpp:2140) prints zone_table[i].number. No roll is on the path.
    """
    implementor = server.login(IMPLEMENTOR.name)

    reply = implementor.command(f"zreset {BOAR_ZONE}")
    assert reply.contains(ZONE_RESET_BY_NUMBER_MARKER), reply.text
