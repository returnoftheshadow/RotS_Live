"""Known bugs in mob scripts, each pinned as a strict xfail of the behaviour scripts should have:
a script whose last row is a wait repeats its tail, a mob that moves itself while hearing a say
cuts off every listener after it, a false IF lands in the wrong place when its block holds a
nested block that ends its block or has an else branch, and LOAD_OBJ_X needs a value in the
parameter the script editor calls unused."""

from __future__ import annotations

from dataclasses import dataclass

import pytest

from rots_harness import world
from rots_harness.characters import IMPLEMENTOR
from rots_harness.session import GameSession
from rots_harness.world_resets import MobReset

pytestmark = [pytest.mark.scenario, pytest.mark.scripts]


@dataclass(frozen=True)
class ScriptMob:
    """A test-world mob that runs a script (tests/integration/world/mob/17.mob, scr/17.scr)."""

    # The mob's virtual number, which is also its script's number.
    vnum: int
    # The keyword that finds this mob and no other in the room.
    keyword: str
    # The mob's short description.
    short_description: str


# The crier (mob 1730): hearing speech, says "First.", waits four passes, says "Second." and
# waits four passes again as the script's last row.
CRIER = ScriptMob(vnum=1730, keyword="crier", short_description="a crier")
# The wanderer (mob 1723), which teleports himself to room 1201 when he hears speech, and the
# listener (mob 1724), which answers it.
WANDERER = ScriptMob(vnum=1723, keyword="wanderer", short_description="a wanderer")
LISTENER = ScriptMob(vnum=1724, keyword="listener", short_description="a listener")
LISTENER_LINE = "I heard that."
# The clerks (mobs 1731 and 1732), whose ON_RECEIVE scripts open with an IF that is false
# (int1 = 1, int2 = 0, IF int1 == int2), then an outer else branch saying "Outer else." and a
# last row saying "After.". The first clerk's block ends with a nested IF / BEGIN / ... / END;
# the second clerk's holds a nested IF / BEGIN / ... / END_ELSE_BEGIN / ... / END followed by a
# row saying "Outer tail.".
FIRST_CLERK = ScriptMob(vnum=1731, keyword="first", short_description="a first clerk")
SECOND_CLERK = ScriptMob(vnum=1732, keyword="second", short_description="a second clerk")
OUTER_ELSE_LINE = "Outer else."
AFTER_LINE = "After."
# What the clerks say only inside the block the false IF skips.
SKIPPED_LINES = ("Nested body.", "Nested then.", "Nested else.", "Outer tail.")
# The careless copyist (mob 1733): the scribe's script (1715: copy the given object with
# LOAD_OBJ_X, take the copy, give it back) with 0 in LOAD_OBJ_X's first parameter.
CARELESS_COPYIST = ScriptMob(vnum=1733, keyword="careless", short_description="a careless copyist")
# A grey pebble (tests/integration/world/obj/17.obj) and the leather cap (obj/11.obj).
PEBBLE_VNUM = 1701
LEATHER_CAP_VNUM = 1137
# Seconds to wait for the crier's second line: four passes are one second (OPT_USEC, comm.cpp);
# the rest leaves room for a loaded host.
WAIT_TIMEOUT = 10.0
# Seconds to keep listening after the crier's second line: long enough for three more of its
# one-second waits to end.
REPEAT_WINDOW_SECONDS = 3.5


def said(short_description: str, line: str) -> str:
    """What the implementor reads when a mob says a line (say_to_char, act_comm.cpp).

    act() capitalises the first letter of the line it sends.
    """
    return f"{short_description[0].upper()}{short_description[1:]} says '{line}'"


def placed(*mobs: ScriptMob) -> pytest.MarkDecorator:
    """The world_resets marker that places each mob in the implementors' room at boot, in order."""
    return pytest.mark.world_resets(*(MobReset(mob.vnum, world.ROOM_IMMORTAL_START) for mob in mobs))


def give_new_object(implementor: GameSession, vnum: int, keyword: str, mob: ScriptMob) -> str:
    """Loads an object and gives it to the mob; returns the give's reply, which holds what its ON_RECEIVE script did."""
    assert implementor.command(f"load obj {vnum}").contains("You create")
    reply = implementor.command(f"give {keyword} {mob.keyword}")
    assert reply.contains("You give"), reply.text
    return reply.text


@pytest.mark.scripts_delays
@pytest.mark.xfail(strict=True, raises=AssertionError, reason="a script whose last row is DO_WAIT should end when that wait ends")
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR)
@placed(CRIER)
def test_a_script_ending_in_a_wait_ends_when_the_wait_does(server) -> None:
    """A script whose last row is DO_WAIT should stop after that wait, not repeat the rows after its previous wait.

    Bug: run_script's DO_WAIT (script.cpp:1378-1385) sets info->next_command only when the wait
    has a next row, and clear_script_info (script.cpp:365-382) never clears it, so when the
    last row's wait ends continue_char_script (script.cpp:2009-2017) resumes at the row after
    the previous wait: the crier says "Second." once a second for as long as it exists.
    Nothing here draws a random number.
    """
    implementor = server.login(IMPLEMENTOR.name)

    spoken = implementor.command("say Go on.").text
    assert said(CRIER.short_description, "First.") in spoken, spoken
    second_line = said(CRIER.short_description, "Second.")
    heard = implementor.expect([second_line], timeout=WAIT_TIMEOUT)
    heard += implementor.drain(REPEAT_WINDOW_SECONDS)
    assert heard.count(second_line) == 1, heard


@pytest.mark.scripts_actions
@pytest.mark.xfail(strict=True, raises=AssertionError, reason="every mob in the room should hear a say, even after one that moves itself on hearing it")
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR)
@placed(WANDERER, LISTENER)
def test_a_listener_after_a_mob_that_leaves_on_speech_still_answers(server) -> None:
    """A mob whose script moves itself out of the room while it hears a say should not stop the mobs after it hearing.

    Bug: do_say (act_comm.cpp:113-115) walks the room with s = s->next_in_room after running
    each mob's ON_HEAR_SAY trigger. The wanderer's TELEPORT_CHAR_X moves it with
    char_from_room, which sets its next_in_room to 0 (handler.cpp:1233), and char_to_room,
    which puts it at the end of room 1201's list (handler.cpp:1252-1260), so the walk ends at
    the wanderer and the listener loaded after it never hears. Loaded the other way round, the
    listener answers (test_scripts.py). Nothing here draws a random number.
    """
    implementor = server.login(IMPLEMENTOR.name)

    reply = implementor.command("say Anyone there?").text
    assert reply.count(said(LISTENER.short_description, LISTENER_LINE)) == 1, reply


@pytest.mark.scripts_control_flow
@pytest.mark.xfail(strict=True, raises=AssertionError, reason="a false IF should run its else branch when its block ends with a nested block without an else")
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR)
@placed(FIRST_CLERK)
def test_a_false_if_ending_in_a_nested_block_without_an_else_runs_its_else_branch(server) -> None:
    """A false IF whose block ends with a nested IF / BEGIN / ... / END should run its else branch, then go on.

    Bug: get_next_command (script.cpp:1063-1084) always takes one extra step after a nested
    block, which is right only when the nested block ends with END_ELSE_BEGIN and an empty
    else branch. Here the nested block ends with END, so the recursive call returns the outer
    END_ELSE_BEGIN itself, the extra step passes over it, and the scan stops at the outer else
    branch's END instead: the first clerk skips "Outer else." and says only "After.". Nothing
    here draws a random number.
    """
    implementor = server.login(IMPLEMENTOR.name)

    reply = give_new_object(implementor, PEBBLE_VNUM, "pebble", FIRST_CLERK)
    outer_else = said(FIRST_CLERK.short_description, OUTER_ELSE_LINE)
    after = said(FIRST_CLERK.short_description, AFTER_LINE)
    assert outer_else in reply and after in reply, reply
    assert reply.index(outer_else) < reply.index(after), reply
    assert not any(line in reply for line in SKIPPED_LINES), reply


@pytest.mark.scripts_control_flow
@pytest.mark.xfail(strict=True, raises=AssertionError, reason="a false IF should skip its whole block when a nested block in it has a non-empty else branch")
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR)
@placed(SECOND_CLERK)
def test_a_false_if_skips_a_nested_block_with_a_non_empty_else_branch(server) -> None:
    """A false IF whose block holds a nested IF / BEGIN / ... / END_ELSE_BEGIN / ... / END should skip all of its block.

    Bug: get_next_command (script.cpp:1063-1084) resumes the scan at the nested else branch's
    first row and its extra step lands on that branch's END, which it takes for the end of
    the outer block: the second clerk runs "Outer tail." from inside the skipped block, then
    skips the outer else branch, saying "Outer tail." and "After." instead of "Outer else."
    and "After.". #305 fixed the empty-else shape (test_scripts.py) and left this one. Nothing
    here draws a random number.
    """
    implementor = server.login(IMPLEMENTOR.name)

    reply = give_new_object(implementor, PEBBLE_VNUM, "pebble", SECOND_CLERK)
    outer_else = said(SECOND_CLERK.short_description, OUTER_ELSE_LINE)
    after = said(SECOND_CLERK.short_description, AFTER_LINE)
    assert outer_else in reply and after in reply, reply
    assert reply.index(outer_else) < reply.index(after), reply
    assert not any(line in reply for line in SKIPPED_LINES), reply


@pytest.mark.scripts_reports
@pytest.mark.xfail(strict=True, raises=AssertionError, reason="LOAD_OBJ_X should copy ob1 whatever its first parameter, which the script editor calls unused")
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR)
@placed(CARELESS_COPYIST)
def test_load_obj_x_copies_with_0_in_its_unused_parameter(server) -> None:
    """LOAD_OBJ_X should copy ob1 when its first parameter is 0, since the editor calls that parameter unused.

    Bug: the script editor prompts "LOAD_OBJ_X: (unused) object-var" (shapescript.cpp:2145) and
    its listing never shows the first parameter (shapescript.cpp:841-843), but run_script runs
    the line only when that parameter is non-zero (script.cpp:1743), so with 0 it silently
    does nothing: the careless copyist gives nothing back, where the scribe, with 1 there,
    gives the copy (test_scripts.py). Nothing here draws a random number.
    """
    implementor = server.login(IMPLEMENTOR.name)

    reply = give_new_object(implementor, LEATHER_CAP_VNUM, "cap", CARELESS_COPYIST)
    assert "A careless copyist gives you a leather cap." in reply, reply
