"""Knocking at a gatekeeper's door: the knock is heard on both sides, the gatekeeper beyond it
opens the door a moment later and closes it again, and the knocker never leaves their own room.
Asked from his own side with "say open", he does the same."""

from __future__ import annotations

import time

import pytest

from rots_harness.characters import IMPLEMENTOR, Character
from rots_harness.immortal_commands import goto
from rots_harness.session import GameSession
from rots_harness.text_lines import stripped_lines
from rots_harness.world_resets import MobReset

pytestmark = [pytest.mark.scenario, pytest.mark.knocking]

# The rooms either side of the door (tests/integration/world/wld/24.wld): the knocker's side, whose
# north exit is the door (keyword "door"), and the gatekeeper's side, whose south exit is the same
# door seen as "gate".
ROOM_OUTSIDE_THE_GATE = 2401
ROOM_GATEHOUSE = 2402
OUTSIDE_ROOM_NAME = "Outside the Gate"
# The stern gatekeeper (mob/24.mob): MOB_SPEC and program 2, the gatekeeper special (spec_pro.cpp).
GATEKEEPER_VNUM = 2400

# A level-10 human warrior outside the gate, who knocks. Human, so the gatekeeper (race 1, no
# preferences, no rp_flag) neither calls him an enemy (IS_AGGR_TO) nor refuses him
# (RP_RACE_CHECK).
KNOCKER = Character(
    name="Testknocker", race="human", sex="male", level=10, load_room=ROOM_OUTSIDE_THE_GATE, class_name="warrior"
)
# A level-10 human warrior beside the gatekeeper, who hears the knock or asks him to open.
WARDEN = Character(
    name="Testwarden", race="human", sex="female", level=10, load_room=ROOM_GATEHOUSE, class_name="warrior"
)

# What the knocker, a bystander beside them and anyone on the far side read (do_knock,
# act_othe.cpp).
KNOCKED_MARKER = "You knocked on the door."
BYSTANDER_KNOCK_MARKER = f"{KNOCKER.name} knocked on the door."
FAR_SIDE_KNOCK_MARKER = "You hear a knock on the gate."
# What do_knock prints when the word names no exit.
NOTHING_THERE_MARKER = "There is nothing like that here."
# What the gatekeeper's side reads as he opens and closes the gate (do_open, do_close,
# act_move.cpp), and what the other side reads as it moves.
GATE_OPENED_MARKER = "A stern gatekeeper opens the gate."
GATE_CLOSED_MARKER = "A stern gatekeeper closes the gate."
DOOR_OPENED_MARKER = "The door is opened from the other side."
DOOR_CLOSED_MARKER = "The door closes quietly."
# What the gatekeeper does instead of opening (gatekeeper, spec_pro.cpp): every other outcome of a
# knock or a request, from peering at a knocker beside him to finding no door or an open one.
PEERS_MARKER = "shifts uncertainly and peers at"
WHO_IS_THERE_MARKER = "Who is out there?"
ENEMY_MARKER = "To arms!"
FORGOT_MARKER = "Ugh, I forgot what I'm doing."
NOTHING_LIKE_THAT_MARKER = "says 'There is nothing like that here.'"
NO_DOOR_MARKER = "Ah, there is no door for me to open."
NOTHING_TO_OPEN_MARKER = "There is nothing such to open."
ALREADY_OPEN_MARKER = "Ah, the door is open. You are free to go."
LOITERING_MARKER = "Stop loitering here, I have a job to do."
OTHER_OUTCOMES = [
    PEERS_MARKER,
    WHO_IS_THERE_MARKER,
    ENEMY_MARKER,
    FORGOT_MARKER,
    NOTHING_LIKE_THAT_MARKER,
    NO_DOOR_MARKER,
    NOTHING_TO_OPEN_MARKER,
    ALREADY_OPEN_MARKER,
    LOITERING_MARKER,
]
# How the implementor sees the knocker in a room (show_char_to_char, act_info.cpp): the name
# starts the line and the title follows, so only the start is matched.
KNOCKER_LINE_START = f"{KNOCKER.name} "

# The gatekeeper waits number(2, 4) pulses before he opens and 15 pulses before he closes
# (gatekeeper, spec_pro.cpp), at 4 pulses a second (OPT_USEC, comm.cpp). The close
# therefore comes at least 3.75 seconds after the open; 3 seconds is the bound asserted, below
# any real-time server's, and the waits allow a loaded host 15 seconds for each step.
MINIMUM_SECONDS_OPEN = 3.0
GATE_TIMEOUT = 15.0
# Seconds to keep reading after a marker, for the rest of the text.
SETTLE_SECONDS = 1.0


def wait_for(session: GameSession, marker: str) -> str:
    """What the session reads up to and just after the marker, or any other gatekeeper outcome."""
    return session.expect([marker, *OTHER_OUTCOMES], timeout=GATE_TIMEOUT) + session.drain(SETTLE_SECONDS)


@pytest.mark.schedule("affect-hours")
@pytest.mark.world_resets(MobReset(GATEKEEPER_VNUM, ROOM_GATEHOUSE))
@pytest.mark.characters(IMPLEMENTOR, KNOCKER, WARDEN)
def test_a_knock_is_heard_on_both_sides_and_the_gatekeeper_opens_then_closes_the_gate(server) -> None:
    """The knocker reads that they knocked and a bystander that they did; the far side hears the
    knock on the gate. A moment later the gatekeeper opens the gate, each side reading its own
    line, and closes it again 15 pulses later. The knocker is still outside, and still listed
    there.

    Under affect-hours no mob turn runs, so the gatekeeper's day-and-night routine never moves
    the door; only the knock does. Its one draw, number(2, 4), only sets when he opens.
    """
    implementor = server.login(IMPLEMENTOR.name)
    knocker = server.login(KNOCKER.name)
    warden = server.login(WARDEN.name)
    goto(implementor, ROOM_OUTSIDE_THE_GATE, OUTSIDE_ROOM_NAME)
    implementor.drain(SETTLE_SECONDS)
    warden.drain(SETTLE_SECONDS)

    knocked = knocker.command("knock door").text
    assert KNOCKED_MARKER in stripped_lines(knocked), knocked
    assert NOTHING_THERE_MARKER not in knocked, knocked
    bystander = implementor.expect([BYSTANDER_KNOCK_MARKER])
    assert BYSTANDER_KNOCK_MARKER in stripped_lines(bystander), bystander
    far_side = warden.expect([FAR_SIDE_KNOCK_MARKER])
    assert FAR_SIDE_KNOCK_MARKER in stripped_lines(far_side), far_side

    opened_inside = wait_for(warden, GATE_OPENED_MARKER)
    assert GATE_OPENED_MARKER in stripped_lines(opened_inside), opened_inside
    opened_at = time.monotonic()
    opened_outside = wait_for(knocker, DOOR_OPENED_MARKER)
    assert DOOR_OPENED_MARKER in stripped_lines(opened_outside), opened_outside

    closed_inside = wait_for(warden, GATE_CLOSED_MARKER)
    assert GATE_CLOSED_MARKER in stripped_lines(closed_inside), closed_inside
    assert time.monotonic() - opened_at >= MINIMUM_SECONDS_OPEN, closed_inside
    closed_outside = wait_for(knocker, DOOR_CLOSED_MARKER)
    assert DOOR_CLOSED_MARKER in stripped_lines(closed_outside), closed_outside

    looked = knocker.command("look")
    assert looked.room_name() == OUTSIDE_ROOM_NAME, looked.text
    seen = implementor.command("look").text
    assert any(line.startswith(KNOCKER_LINE_START) for line in stripped_lines(seen)), seen


@pytest.mark.schedule("affect-hours")
@pytest.mark.world_resets(MobReset(GATEKEEPER_VNUM, ROOM_GATEHOUSE))
@pytest.mark.characters(IMPLEMENTOR, KNOCKER, WARDEN)
def test_say_open_beside_the_gatekeeper_has_him_open_then_close_the_gate(server) -> None:
    """From the gatekeeper's own side, "say open" has him open the gate, each side reading its own
    line, and close it again 15 pulses later.

    Only a say that does not start with "open" draws (number(1, 10) for his loitering line), so
    this one draws only number(2, 4), which sets when he opens.
    """
    knocker = server.login(KNOCKER.name)
    warden = server.login(WARDEN.name)
    knocker.drain(SETTLE_SECONDS)

    warden.send_line("say open")
    opened_inside = wait_for(warden, GATE_OPENED_MARKER)
    assert GATE_OPENED_MARKER in stripped_lines(opened_inside), opened_inside
    opened_at = time.monotonic()
    opened_outside = wait_for(knocker, DOOR_OPENED_MARKER)
    assert DOOR_OPENED_MARKER in stripped_lines(opened_outside), opened_outside

    closed_inside = wait_for(warden, GATE_CLOSED_MARKER)
    assert GATE_CLOSED_MARKER in stripped_lines(closed_inside), closed_inside
    assert time.monotonic() - opened_at >= MINIMUM_SECONDS_OPEN, closed_inside
    closed_outside = wait_for(knocker, DOOR_CLOSED_MARKER)
    assert DOOR_CLOSED_MARKER in stripped_lines(closed_outside), closed_outside
