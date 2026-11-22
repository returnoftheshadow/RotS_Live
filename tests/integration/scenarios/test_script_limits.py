"""Runaway scripts: mobs whose speech scripts answer each other in one room stop at the limits.

The limits are MAX_SCRIPT_NESTING and MAX_SCRIPT_RUNS_PER_EVENT (script.h); each logs one SYSERR
per event, instead of crashing the server.
"""

from __future__ import annotations

import re
import time
from pathlib import Path

import pytest

from rots_harness.characters import IMPLEMENTOR, Character
from rots_harness.game_log import LOG_POLL_SECONDS, LOG_TIMEOUT_SECONDS, read_game_log
from rots_harness.game_text import FORCED_REPLY, OUTPUT_OVERFLOW
from rots_harness.immortal_commands import goto
from rots_harness.replies import SETTLE_SECONDS, reply_to
from rots_harness.world_resets import MobReset

pytestmark = [pytest.mark.scenario, pytest.mark.script_limits]

# Room 2602, the Echo Room in zone 26: a room of its own for the echo and the mirror, two mobs
# that each say "Hi." whenever they hear anything said (script 2600, world/notes/zone-26.md).
ROOM_ECHO = 2602
# The name look shows for ROOM_ECHO.
ROOM_ECHO_NAME = "Echo Room"
# Room 2603, the Chanting Room in zone 26: a room of its own for three chanters running the same
# script.
ROOM_CHANTING = 2603
# The name look and goto show for ROOM_CHANTING.
ROOM_CHANTING_NAME = "Chanting Room"
# Mob 2601, the echo, one of the two mobs in the Echo Room.
ECHO_VNUM = 2601
# Mob 2602, the mirror, the other one.
MIRROR_VNUM = 2602
# Mob 2603, the chanter.
CHANTER_VNUM = 2603
# How many chanters the Chanting Room holds.
CHANTER_COUNT = 3
# How deep scripts may run inside one another (script.h).
MAX_SCRIPT_NESTING = 256
# How many script runs one event may start in all (script.h).
MAX_SCRIPT_RUNS_PER_EVENT = 10000
# The words every echo-script mob says (script 2600).
ANSWER = "Hi."
# What a listener reads as the echo answers (say_to_char, act_comm.cpp).
ECHO_SAYS_LINE = f"An echo says '{ANSWER}'"
# What a listener reads as the mirror answers.
MIRROR_SAYS_LINE = f"A mirror says '{ANSWER}'"
# The echo's line in a look at its room.
ECHO_IN_ROOM_LINE = "An echo hangs in the air here."
# The mirror's line in a look at its room.
MIRROR_IN_ROOM_LINE = "A mirror stands here, listening."
# Each chanter's line in a look at its room.
CHANTER_IN_ROOM_LINE = "A chanter stands here, listening."
# The refusal at the nesting limit (running_script_frame::refusal_reason and report_refused_run,
# script.cpp), as far as it is the same for every refused run.
NESTING_REFUSAL_PART = (
    f"too many scripts are running inside one another (limits {MAX_SCRIPT_NESTING} deep,"
    f" {MAX_SCRIPT_RUNS_PER_EVENT} runs;"
)
# The refusal at the limit on runs in one event, likewise.
RUNS_REFUSAL_PART = (
    f"too many scripts have run for one event (limits {MAX_SCRIPT_NESTING} deep,"
    f" {MAX_SCRIPT_RUNS_PER_EVENT} runs;"
)
# A whole refusal line: the refused script is the echo script, and ch1 is the mob it was to run
# for; group 1 is the refusal, group 2 that mob's name.
REFUSAL_LINE_PATTERN = re.compile(
    r"SYSERR: report_refused_run: script #2600 not run: (too many [^(]+\(limits [^;]+;) ch1 ([a-z ]+), ob1 none\)"
)
# The mortal who speaks in the Echo Room: a level-10 human warrior standing there from the start.
SPEAKER = Character(
    name="Testspeaker", race="human", sex="male", level=10, load_room=ROOM_ECHO, class_name="warrior"
)
# The word the speaker says, and the forced chanter too: any word sets the scripts off.
HELLO = "hello"


def refusals_in(log_path: Path) -> tuple[list[tuple[str, str]], str]:
    """Return every refusal in game.log as (refusal, mob name) in log order, and the log itself."""
    log_text = read_game_log(log_path)
    matches = REFUSAL_LINE_PATTERN.finditer(log_text)
    return [(match.group(1), match.group(2)) for match in matches], log_text


def wait_for_refusals(log_path: Path, count: int) -> list[tuple[str, str]]:
    """Wait until game.log holds count refusal lines, then a little longer, and return them.

    Each is returned as (refusal, mob name) in log order. Fails with the log's tail unless exactly
    count arrived and every report_refused_run line is one of them.
    """
    deadline = time.monotonic() + LOG_TIMEOUT_SECONDS
    refusals, log_text = refusals_in(log_path)
    while len(refusals) < count and time.monotonic() < deadline:
        time.sleep(LOG_POLL_SECONDS)
        refusals, log_text = refusals_in(log_path)
    time.sleep(SETTLE_SECONDS)
    refusals, log_text = refusals_in(log_path)
    assert len(refusals) == count, f"{len(refusals)} refusal lines, not {count}:\n{log_text[-3000:]}"
    assert log_text.count("report_refused_run") == count, log_text[-3000:]
    return refusals


@pytest.mark.schedule("affect-hours")
@pytest.mark.expect_syserr(NESTING_REFUSAL_PART)
@pytest.mark.world_resets(MobReset(ECHO_VNUM, ROOM_ECHO), MobReset(MIRROR_VNUM, ROOM_ECHO))
@pytest.mark.characters(SPEAKER)
def test_two_mobs_answering_each_other_stop_at_the_nesting_limit(server) -> None:
    """Fix pin (#555): the echo and the mirror answer each other only as deep as scripts nest.

    A player's say sets off each mob's script in turn (do_say, act_comm.cpp), two events. In
    each, every answer is another say that sets off the other mob's script inside the first, so
    MAX_SCRIPT_NESTING runs answer, half each, and the next is refused with one SYSERR. The
    speaker reads every answer, 256 from each mob, and the server goes on. #555 made the answers
    stop at the limit; before it, nothing stopped them, and the server's stack overflowed. No draw
    is on the path.
    """
    speaker = server.login(SPEAKER.name)
    said = speaker.command(f"say {HELLO}")
    answers = said.text + speaker.drain(SETTLE_SECONDS)
    assert OUTPUT_OVERFLOW not in answers, answers[-1500:]
    assert answers.count(ECHO_SAYS_LINE) == MAX_SCRIPT_NESTING, answers[-1500:]
    assert answers.count(MIRROR_SAYS_LINE) == MAX_SCRIPT_NESTING, answers[-1500:]

    refusals = wait_for_refusals(server.handle.log_path, 2)
    assert [refusal for refusal, _ in refusals] == [NESTING_REFUSAL_PART, NESTING_REFUSAL_PART], refusals
    assert sorted(name for _, name in refusals) == ["a mirror", "an echo"], refusals

    looked = speaker.command("look")
    assert looked.room_name() == ROOM_ECHO_NAME, looked.text
    assert looked.contains(ECHO_IN_ROOM_LINE), looked.text
    assert looked.contains(MIRROR_IN_ROOM_LINE), looked.text


@pytest.mark.schedule("affect-hours")
@pytest.mark.expect_syserr(NESTING_REFUSAL_PART, RUNS_REFUSAL_PART)
@pytest.mark.world_resets(*(MobReset(CHANTER_VNUM, ROOM_CHANTING) for _ in range(CHANTER_COUNT)))
@pytest.mark.characters(IMPLEMENTOR)
def test_three_mobs_answering_each_other_stop_at_both_limits(server) -> None:
    """Fix pin (#555): three chanters answering each other stop at the nesting limit, then the runs.

    Each limit is logged once per event; the second is the limit on runs in one event. The
    implementor, elsewhere, forces one chanter to speak; the other two hear it, two events.
    In each, every answer sets off both other chanters' scripts, so the runs branch: the first
    chain reaches MAX_SCRIPT_NESTING and logs its refusal once, and the branches after it reach
    MAX_SCRIPT_RUNS_PER_EVENT and log that refusal once, after which every run of the event is
    refused unlogged. Nobody is in the room to read the answers. #555 made the runs stop at the
    limits; before it, the first chain overflowed the server's stack. No draw is on the path.
    """
    implementor = server.login(IMPLEMENTOR.name)
    forced = reply_to(implementor, f"force chanter say {HELLO}", [FORCED_REPLY])
    assert FORCED_REPLY in forced, forced

    refusals = wait_for_refusals(server.handle.log_path, 4)
    assert [refusal for refusal, _ in refusals] == [NESTING_REFUSAL_PART, RUNS_REFUSAL_PART] * 2, refusals

    arrived = goto(implementor, ROOM_CHANTING, ROOM_CHANTING_NAME)
    assert arrived.text.count(CHANTER_IN_ROOM_LINE) == CHANTER_COUNT, arrived.text
