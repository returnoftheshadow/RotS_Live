"""The vampires' tower door guard: it closes its open door, and logs a missing side of it.

With no light bearer near, it closes its open door on both sides once, and with a side of the
door missing it logs that and the game goes on.
"""

from __future__ import annotations

import pytest

from rots_harness.characters import IMPLEMENTOR, Character
from rots_harness.game_log import count_logged, logged_messages
from rots_harness.immortal_commands import goto, set_syslog
from rots_harness.replies import SETTLE_SECONDS, reply_to, settled_expect
from rots_harness.session import GameSession
from rots_harness.shaping import (
    COULD_NOT_OPEN_FILE_REPLY,
    ROOM_IMPLEMENTED_REPLY,
    ROOM_RELEASED_REPLY,
    release_editor,
)
from rots_harness.world_resets import MobReset

pytestmark = [pytest.mark.scenario, pytest.mark.vampire_tower]

# Room 15345, outside the tower: its south exit is the guarded door (keyword door), open at boot.
# vampire_doorkeep (spec_pro.cpp) reads this room and 15355 by these fixed vnums.
ROOM_BEFORE_THE_DOOR = 15345
# Room 15355, inside the tower: its north exit is the same door seen from inside (keyword gate).
ROOM_INSIDE_THE_DOOR = 15355
# The outer room's name, which look shows first.
ROOM_BEFORE_THE_DOOR_NAME = "Before the Tower Door"
# The doorwarden (mob 2000, world/mob/20.mob): act flag SPEC and program 22, vampire_doorkeep.
DOORWARDEN_VNUM = 2000

# What the outer room reads when vampire_doorkeep closes the door, in the words of its side.
DOOR_CLOSES_OUTSIDE = "The door blurs for a second... then closes."
# What the inner room reads then, in the words of its side.
DOOR_CLOSES_INSIDE = "The gate blurs for a second... then closes."
# The part of either message that is the same on both sides and in the opening message.
DOOR_BLURS_PART = "blurs for a second..."
# What a character reads walking into the closed door from inside (do_move, act_move.cpp).
GATE_IS_CLOSED_REPLY = "The gate seems to be closed."
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

# What the room editor prints as it loads the outer room (shaperom.cpp).
ROOM_LOADING_REPLY = f"loading room #{ROOM_BEFORE_THE_DOOR}"
# What it prints when it fails to load the room.
ROOM_LOAD_FAILURE_REPLIES = [COULD_NOT_OPEN_FILE_REPLY, "no room here."]
# What /7 asks: which exit to remove.
WHICH_EXIT_PROMPT = "Enter exit to remove (n e s w u d):"
# What the editor prints as it removes the south exit.
SOUTH_EXIT_REMOVED_REPLY = "Exit SOUTH removed."
# What the editor prints for an exit the room does not have.
NO_SUCH_EXIT_REPLY = "This exit did not exist already."
# What /implement prints when it cannot implement the room.
IMPLEMENT_REFUSALS = ["You may not do that in this zone.", "You have nothing to implement"]

# Seconds between two reads of game.log while waiting for a line to reach it.
LOG_POLL_SECONDS = 0.1


def doorwarden_at_the_door() -> pytest.MarkDecorator:
    """Return the world_resets marker that places the doorwarden outside the door at boot."""
    return pytest.mark.world_resets(MobReset(DOORWARDEN_VNUM, ROOM_BEFORE_THE_DOOR))


def turn_off_syslog(implementor: GameSession) -> None:
    """Turn the implementor's syslog off, so no '[ ... ]' log line can end a reply early."""
    set_syslog(implementor, "off")


def goto_the_door(implementor: GameSession) -> None:
    """Move the implementor outside the door; goto asks no procedure in either room."""
    reply = goto(implementor, ROOM_BEFORE_THE_DOOR, ROOM_BEFORE_THE_DOOR_NAME, exact=False)
    assert DOOR_BLURS_PART not in reply.text, reply.text


def remove_the_south_exit_outside_the_door(implementor: GameSession) -> None:
    """Remove room 15345's south exit, the door's outer side, in the live world, with the editor.

    /implement replaces the room's exits in the world with the editor's copy (implement_room,
    shaperom.cpp), freeing the one the editor removed; the room's file is left as it was.
    """
    loaded = reply_to(
        implementor, f"shape room {ROOM_BEFORE_THE_DOOR}", [ROOM_LOADING_REPLY, *ROOM_LOAD_FAILURE_REPLIES]
    )
    assert ROOM_LOADING_REPLY in loaded, loaded
    asked = reply_to(implementor, "/7", [WHICH_EXIT_PROMPT])
    assert WHICH_EXIT_PROMPT in asked, asked
    removed = reply_to(implementor, "s", [SOUTH_EXIT_REMOVED_REPLY, NO_SUCH_EXIT_REPLY])
    assert SOUTH_EXIT_REMOVED_REPLY in removed, removed
    implemented = reply_to(implementor, "/implement", [ROOM_IMPLEMENTED_REPLY, *IMPLEMENT_REFUSALS])
    assert ROOM_IMPLEMENTED_REPLY in implemented, implemented
    release_editor(implementor, ROOM_RELEASED_REPLY, settle=SETTLE_SECONDS)


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, VISITOR)
@doorwarden_at_the_door()
def test_the_door_guard_closes_its_open_door_on_both_sides_once(server) -> None:
    """Pins that a command typed beside the doorwarden closes the open tower door, each side once.

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
    assert ROOM_BEFORE_THE_DOOR_NAME in closing, closing
    watched = settled_expect(visitor, [DOOR_CLOSES_INSIDE])
    assert watched.count(DOOR_CLOSES_INSIDE) == 1, watched
    blocked = visitor.command("north")
    assert blocked.contains(GATE_IS_CLOSED_REPLY), blocked.text

    again = implementor.command("look").text
    assert ROOM_BEFORE_THE_DOOR_NAME in again and DOOR_BLURS_PART not in again, again
    watched = visitor.drain(SETTLE_SECONDS)
    assert DOOR_BLURS_PART not in watched, watched
    messages = logged_messages(server.handle.log_path)
    assert MISSING_SIDE_LOG not in messages, messages


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, VISITOR)
@doorwarden_at_the_door()
def test_the_door_guard_with_a_side_of_its_door_missing_logs_it_and_the_game_goes_on(server) -> None:
    """Fix pin (#559): with the door's outer side gone, the doorwarden logs it and touches nothing.

    vampire_doorkeep (spec_pro.cpp) runs with room 15345's south exit gone: a command beside the
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
    assert ROOM_BEFORE_THE_DOOR_NAME in looked and DOOR_BLURS_PART not in looked, looked
    missing_side_count = count_logged(server.handle.log_path, MISSING_SIDE_LOG, 1, poll=LOG_POLL_SECONDS)
    assert missing_side_count == 1, logged_messages(server.handle.log_path)
    watched = visitor.drain(SETTLE_SECONDS)
    assert DOOR_BLURS_PART not in watched, watched

    walked = visitor.command("north")
    assert walked.contains(ROOM_BEFORE_THE_DOOR_NAME), walked.text
    missing_side_count = count_logged(server.handle.log_path, MISSING_SIDE_LOG, 2, poll=LOG_POLL_SECONDS)
    assert missing_side_count == 2, logged_messages(server.handle.log_path)
