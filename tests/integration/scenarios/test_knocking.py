"""Knocking at a gatekeeper's door, and asking him to open it.

The knock is heard on both sides, the gatekeeper beyond it opens the door a moment later and
closes it again, and the knocker never leaves their own room. Asked from his own side with
"say open", he does the same.
"""

from __future__ import annotations

import time

import pytest

from rots_harness.characters import IMPLEMENTOR, Character
from rots_harness.immortal_commands import goto
from rots_harness.replies import SETTLE_SECONDS, settled_expect
from rots_harness.session import GameSession
from rots_harness.text_lines import stripped_lines
from rots_harness.world_resets import MobReset

pytestmark = [pytest.mark.scenario, pytest.mark.knocking]

# The rooms either side of the door (tests/integration/world/wld/24.wld): the knocker's side, whose
# north exit is the door (keyword "door"), and the gatekeeper's side, whose south exit is the same
# door seen as "gate".
ROOM_OUTSIDE_THE_GATE = 2401
# The gatekeeper's side of the door.
ROOM_GATEHOUSE = 2402
# The name look shows for ROOM_OUTSIDE_THE_GATE (wld/24.wld).
ROOM_OUTSIDE_THE_GATE_NAME = "Outside the Gate"
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

# What the knocker reads (do_knock, act_othe.cpp).
KNOCKED_REPLY = "You knocked on the door."
# What a bystander beside the knocker reads (do_knock).
BYSTANDER_KNOCK = f"{KNOCKER.name} knocked on the door."
# What anyone on the far side of the door reads (do_knock).
FAR_SIDE_KNOCK = "You hear a knock on the gate."
# What do_knock prints when the word names no exit.
NOTHING_THERE_REPLY = "There is nothing like that here."
# What the gatekeeper's side reads as he opens the gate (do_open, act_move.cpp).
GATE_OPENED = "A stern gatekeeper opens the gate."
# What the gatekeeper's side reads as he closes the gate (do_close, act_move.cpp).
GATE_CLOSED = "A stern gatekeeper closes the gate."
# What the other side reads as the door opens.
DOOR_OPENED = "The door is opened from the other side."
# What the other side reads as the door closes.
DOOR_CLOSED = "The door closes quietly."
# The gatekeeper's other outcomes, instead of opening (gatekeeper, spec_pro.cpp), each a part of
# his act, say or yell. He peers at a knocker beside him.
PEERS_PART = "shifts uncertainly and peers at"
# He yells for whoever knocked unseen.
WHO_IS_THERE_PART = "Who is out there?"
# He cries out at an enemy.
ENEMY_PART = "To arms!"
# He loses track of what he was doing.
FORGOT_PART = "Ugh, I forgot what I'm doing."
# He finds nothing by the word he was asked to open.
NOTHING_LIKE_THAT_PART = "says 'There is nothing like that here.'"
# He finds no door by that word.
NO_DOOR_PART = "Ah, there is no door for me to open."
# He finds nothing to open.
NOTHING_TO_OPEN_PART = "There is nothing such to open."
# He finds the door already open.
ALREADY_OPEN_PART = "Ah, the door is open. You are free to go."
# He answers a say that does not ask him to open.
LOITERING_PART = "Stop loitering here, I have a job to do."
# Every other outcome of a knock or a request, from peering at a knocker beside him to finding no
# door or an open one.
OTHER_OUTCOMES = [
    PEERS_PART,
    WHO_IS_THERE_PART,
    ENEMY_PART,
    FORGOT_PART,
    NOTHING_LIKE_THAT_PART,
    NO_DOOR_PART,
    NOTHING_TO_OPEN_PART,
    ALREADY_OPEN_PART,
    LOITERING_PART,
]
# How the implementor sees the knocker in a room (show_char_to_char, act_info.cpp): the name
# starts the line and the title follows, so only the start is matched.
KNOCKER_LINE_START = f"{KNOCKER.name} "

# The gatekeeper waits number(2, 4) pulses before he opens and 15 pulses before he closes
# (gatekeeper, spec_pro.cpp), at 4 pulses a second (OPT_USEC, comm.cpp). The close
# therefore comes at least 3.75 seconds after the open; 3 seconds is the bound asserted, below
# any real-time server's, and the waits allow a loaded host 15 seconds for each step.
MINIMUM_SECONDS_OPEN = 3.0
# Seconds each wait for the gatekeeper's next step allows, by the sums above.
GATE_TIMEOUT_SECONDS = 15.0


def wait_for(session: GameSession, outcome: str) -> str:
    """Return what session reads up to and just after outcome, or any other gatekeeper outcome."""
    return settled_expect(session, [outcome, *OTHER_OUTCOMES], timeout=GATE_TIMEOUT_SECONDS)


@pytest.mark.schedule("affect-hours")
@pytest.mark.world_resets(MobReset(GATEKEEPER_VNUM, ROOM_GATEHOUSE))
@pytest.mark.characters(IMPLEMENTOR, KNOCKER, WARDEN)
def test_a_knock_is_heard_on_both_sides_and_the_gatekeeper_opens_then_closes_the_gate(server) -> None:
    """Pins a knock heard on both sides, and the gatekeeper opening, then closing, the gate.

    The knocker reads that they knocked and a bystander that they did; the far side hears the
    knock on the gate. A moment later the gatekeeper opens the gate, each side reading its own
    line, and closes it again 15 pulses later. The knocker is still outside, and still listed
    there.

    Under affect-hours no mob turn runs, so the gatekeeper's day-and-night routine never moves
    the door; only the knock does. Its one draw, number(2, 4), only sets when he opens.
    """
    implementor = server.login(IMPLEMENTOR.name)
    knocker = server.login(KNOCKER.name)
    warden = server.login(WARDEN.name)
    goto(implementor, ROOM_OUTSIDE_THE_GATE, ROOM_OUTSIDE_THE_GATE_NAME)
    implementor.drain(SETTLE_SECONDS)
    warden.drain(SETTLE_SECONDS)

    knocked = knocker.command("knock door").text
    assert KNOCKED_REPLY in stripped_lines(knocked), knocked
    assert NOTHING_THERE_REPLY not in knocked, knocked
    bystander = implementor.expect([BYSTANDER_KNOCK])
    assert BYSTANDER_KNOCK in stripped_lines(bystander), bystander
    far_side = warden.expect([FAR_SIDE_KNOCK])
    assert FAR_SIDE_KNOCK in stripped_lines(far_side), far_side

    opened_inside = wait_for(warden, GATE_OPENED)
    assert GATE_OPENED in stripped_lines(opened_inside), opened_inside
    opened_at = time.monotonic()
    opened_outside = wait_for(knocker, DOOR_OPENED)
    assert DOOR_OPENED in stripped_lines(opened_outside), opened_outside

    closed_inside = wait_for(warden, GATE_CLOSED)
    assert GATE_CLOSED in stripped_lines(closed_inside), closed_inside
    assert time.monotonic() - opened_at >= MINIMUM_SECONDS_OPEN, closed_inside
    closed_outside = wait_for(knocker, DOOR_CLOSED)
    assert DOOR_CLOSED in stripped_lines(closed_outside), closed_outside

    looked = knocker.command("look")
    assert looked.room_name() == ROOM_OUTSIDE_THE_GATE_NAME, looked.text
    seen = implementor.command("look").text
    seen_lines = stripped_lines(seen)
    assert any(line.startswith(KNOCKER_LINE_START) for line in seen_lines), seen


@pytest.mark.schedule("affect-hours")
@pytest.mark.world_resets(MobReset(GATEKEEPER_VNUM, ROOM_GATEHOUSE))
@pytest.mark.characters(IMPLEMENTOR, KNOCKER, WARDEN)
def test_say_open_beside_the_gatekeeper_has_him_open_then_close_the_gate(server) -> None:
    """Pins "say open" beside the gatekeeper having him open, then close, the gate.

    From the gatekeeper's own side, "say open" has him open the gate, each side reading its own
    line, and close it again 15 pulses later.

    Only a say that does not start with "open" draws (number(1, 10) for his loitering line), so
    this one draws only number(2, 4), which sets when he opens.
    """
    knocker = server.login(KNOCKER.name)
    warden = server.login(WARDEN.name)
    knocker.drain(SETTLE_SECONDS)

    warden.send_line("say open")
    opened_inside = wait_for(warden, GATE_OPENED)
    assert GATE_OPENED in stripped_lines(opened_inside), opened_inside
    opened_at = time.monotonic()
    opened_outside = wait_for(knocker, DOOR_OPENED)
    assert DOOR_OPENED in stripped_lines(opened_outside), opened_outside

    closed_inside = wait_for(warden, GATE_CLOSED)
    assert GATE_CLOSED in stripped_lines(closed_inside), closed_inside
    assert time.monotonic() - opened_at >= MINIMUM_SECONDS_OPEN, closed_inside
    closed_outside = wait_for(knocker, DOOR_CLOSED)
    assert DOOR_CLOSED in stripped_lines(closed_outside), closed_outside
