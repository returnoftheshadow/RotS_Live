"""The vampires' tower door guard: with no light bearer near, it closes its open door on both
sides once, and with a side of the door missing it logs that and the game goes on."""

from __future__ import annotations

import time

import pytest

from rots_harness.characters import IMPLEMENTOR, Character
from rots_harness.session import GameSession
from rots_harness.world_resets import MobReset

pytestmark = [pytest.mark.scenario, pytest.mark.vampire_tower]

# Room 15345, outside the tower: its south exit is the guarded door (keyword door), open at boot.
# vampire_doorkeep (spec_pro.cpp) reads this room and 15355 by these fixed vnums.
ROOM_BEFORE_THE_DOOR = 15345
# Room 15355, inside the tower: its north exit is the same door seen from inside (keyword gate).
ROOM_INSIDE_THE_DOOR = 15355
# The outer room's name, which look shows first.
BEFORE_THE_DOOR_NAME = "Before the Tower Door"
# The doorwarden (mob 2000, world/mob/20.mob): act flag SPEC and program 22, vampire_doorkeep.
DOORWARDEN_VNUM = 2000

# What each room reads when vampire_doorkeep closes the door, in the words of that room's side.
DOOR_CLOSES_OUTSIDE = "The door blurs for a second... then closes."
DOOR_CLOSES_INSIDE = "The gate blurs for a second... then closes."
# The part of either message that is the same on both sides and in the opening message.
DOOR_BLURS = "blurs for a second..."
# What a character reads walking into the closed door from inside (do_move, act_move.cpp).
GATE_IS_CLOSED = "The gate seems to be closed."
# What vampire_doorkeep logs, instead of crashing, when a side of its door is missing.
MISSING_SIDE_LOG = "vampire_doorkeep: the door between rooms 15345 and 15355 is missing a side"

# A level-20 human warrior inside the tower, beyond the door from the doorwarden. He carries no
# light, so vampire_doorkeep never finds its light (object 7008) on him.
VISITOR = Character(
    name="Testvisitor",
    race="human",
    sex="male",
    level=20,
    load_room=ROOM_INSIDE_THE_DOOR,
    class_name="warrior",
)

# The room editor's replies (shaperom.cpp): loading the room, or failing to; asking which exit
# to remove and removing it; implementing the room; and releasing it.
ROOM_LOADED = f"loading room #{ROOM_BEFORE_THE_DOOR}"
ROOM_LOAD_FAILURES = ["could not open that file.", "no room here."]
WHICH_EXIT_TO_REMOVE = "Enter exit to remove (n e s w u d):"
SOUTH_EXIT_REMOVED = "Exit SOUTH removed."
NO_SUCH_EXIT = "This exit did not exist already."
ROOM_IMPLEMENTED = "Room implemented."
IMPLEMENT_REFUSALS = ["You may not do that in this zone.", "You have nothing to implement"]
ROOM_RELEASED = "You released the room and stopped shaping."

# Seconds to keep reading after an outcome, for text already sent to arrive.
SETTLE_SECONDS = 1.0
# Seconds to wait for a line to reach game.log, and between two reads of it.
LOG_TIMEOUT = 10.0
LOG_POLL_SECONDS = 0.1


def doorwarden_at_the_door() -> pytest.MarkDecorator:
    """The world_resets marker that places the doorwarden outside the door at boot."""
    return pytest.mark.world_resets(MobReset(DOORWARDEN_VNUM, ROOM_BEFORE_THE_DOOR))


def log_messages(server) -> list:
    """The messages mudlog wrote to game.log, each line being '<type>, <time> :: <message>' (utility.cpp)."""
    text = (server.run_dir / "game.log").read_text(encoding="utf-8", errors="replace")
    return [line.split(" :: ", 1)[1] for line in text.splitlines() if " :: " in line]


def count_logged(server, message: str, expected_count: int) -> int:
    """How many times game.log holds the message, once it holds it expected_count times or LOG_TIMEOUT passes.

    The server writes its log before it sends the reply that caused it; the Docker launcher
    relays the log through compose, so the line can reach the file a little later.
    """
    deadline = time.monotonic() + LOG_TIMEOUT
    while log_messages(server).count(message) < expected_count and time.monotonic() < deadline:
        time.sleep(LOG_POLL_SECONDS)
    return log_messages(server).count(message)


def turn_off_syslog(implementor: GameSession) -> None:
    """Turns the implementor's syslog off, so no '[ ... ]' log line can end a reply early."""
    reply = implementor.command("syslog off")
    assert reply.contains("Your syslog is now off."), reply.text


def goto_the_door(implementor: GameSession) -> None:
    """Moves the implementor outside the door. goto asks no procedure in either room."""
    reply = implementor.command(f"goto {ROOM_BEFORE_THE_DOOR}")
    assert reply.contains(BEFORE_THE_DOOR_NAME), reply.text
    assert DOOR_BLURS not in reply.text, reply.text


def editor_step(implementor: GameSession, line: str, outcomes: list) -> str:
    """Sends one room-editor line and returns what came back, up to and just after the first outcome.

    Editor prompts end in ':' or ']' mid-field, so command() cannot be used inside the editor.
    """
    implementor.send_line(line)
    return implementor.expect(outcomes) + implementor.drain(SETTLE_SECONDS)


def remove_the_south_exit_outside_the_door(implementor: GameSession) -> None:
    """Removes room 15345's south exit, the door's outer side, in the live world with the room editor.

    /implement replaces the room's exits in the world with the editor's copy (implement_room,
    shaperom.cpp), freeing the one the editor removed; the room's file is left as it was.
    """
    loaded = editor_step(implementor, f"shape room {ROOM_BEFORE_THE_DOOR}", [ROOM_LOADED, *ROOM_LOAD_FAILURES])
    assert ROOM_LOADED in loaded, loaded
    asked = editor_step(implementor, "/7", [WHICH_EXIT_TO_REMOVE])
    assert WHICH_EXIT_TO_REMOVE in asked, asked
    removed = editor_step(implementor, "s", [SOUTH_EXIT_REMOVED, NO_SUCH_EXIT])
    assert SOUTH_EXIT_REMOVED in removed, removed
    implemented = editor_step(implementor, "/implement", [ROOM_IMPLEMENTED, *IMPLEMENT_REFUSALS])
    assert ROOM_IMPLEMENTED in implemented, implemented
    released = editor_step(implementor, "/free", [ROOM_RELEASED])
    assert ROOM_RELEASED in released, released


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, VISITOR)
@doorwarden_at_the_door()
def test_the_door_guard_closes_its_open_door_on_both_sides_once(server) -> None:
    """A command typed beside the doorwarden closes the open tower door, and each side reads it once.

    vampire_doorkeep (spec_pro.cpp) ignores its command: every command typed in its room runs it
    (special, interpre.cpp). With no player in 15345 or 15355 wearing its light (object 7008) it
    closes the door's outer side and tells 15345, then the inner side and tells 15355, each in
    its own keyword. A closed door is left alone, so a second command says nothing. Under the
    affect-hours schedule the doorwarden never acts on its own turn. The one draw on the path,
    number(0, 100) for each player in 15355, only sends a wolf's charge to the others in 15355,
    and there are none.
    """
    implementor = server.login(IMPLEMENTOR.name)
    visitor = server.login(VISITOR.name)
    turn_off_syslog(implementor)
    goto_the_door(implementor)
    visitor.drain(SETTLE_SECONDS)

    closing = implementor.command("look").text
    assert closing.count(DOOR_CLOSES_OUTSIDE) == 1, closing
    assert BEFORE_THE_DOOR_NAME in closing, closing
    watched = visitor.expect([DOOR_CLOSES_INSIDE]) + visitor.drain(SETTLE_SECONDS)
    assert watched.count(DOOR_CLOSES_INSIDE) == 1, watched
    blocked = visitor.command("north")
    assert blocked.contains(GATE_IS_CLOSED), blocked.text

    again = implementor.command("look").text
    assert BEFORE_THE_DOOR_NAME in again and DOOR_BLURS not in again, again
    watched = visitor.drain(SETTLE_SECONDS)
    assert DOOR_BLURS not in watched, watched
    assert MISSING_SIDE_LOG not in log_messages(server), log_messages(server)


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, VISITOR)
@doorwarden_at_the_door()
def test_the_door_guard_with_a_side_of_its_door_missing_logs_it_and_the_game_goes_on(server) -> None:
    """With the door's outer side removed, the doorwarden logs the missing side and touches nothing.

    Pins vampire_doorkeep (spec_pro.cpp) with room 15345's south exit gone: a command beside the
    doorwarden logs the missing side (a mudlog, not a SYSERR) and returns before changing either
    side, so the inner side stays open and the visitor walks through it. Walking into 15345 runs
    the doorwarden again (SPECIAL_ENTER, do_move in act_move.cpp), which logs again. The one draw
    on the path, number(0, 100) for each player in 15355, only sends a wolf's charge to the
    others in 15355, and there are none. #559 made vampire_doorkeep log a missing side and
    return; before it, the doorwarden read the missing exit and crashed the server.
    """
    implementor = server.login(IMPLEMENTOR.name)
    visitor = server.login(VISITOR.name)
    turn_off_syslog(implementor)
    remove_the_south_exit_outside_the_door(implementor)
    goto_the_door(implementor)
    visitor.drain(SETTLE_SECONDS)

    looked = implementor.command("look").text
    assert BEFORE_THE_DOOR_NAME in looked and DOOR_BLURS not in looked, looked
    assert count_logged(server, MISSING_SIDE_LOG, 1) == 1, log_messages(server)
    watched = visitor.drain(SETTLE_SECONDS)
    assert DOOR_BLURS not in watched, watched

    walked = visitor.command("north")
    assert walked.contains(BEFORE_THE_DOOR_NAME), walked.text
    assert count_logged(server, MISSING_SIDE_LOG, 2) == 2, log_messages(server)
