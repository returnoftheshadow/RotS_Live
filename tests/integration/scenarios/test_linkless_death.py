"""A linkless player killed by a room tick comes back with the gentle death restore saved, and the
server survives the death.

A dropped link leaves the body in the world with its descriptor still attached: close_socket()
(comm.cpp) saves, logs "Closing link to:" and marks the descriptor linkless without clearing the
character's `desc`. A blaze tick credited to Harnmage, who stays logged in, kills the body through
damage_credited() and die() into raw_kill() (fight.cpp). raw_kill() sets hit points to max/4 (the
gentle arm of a player-credited death) and calls extract_char() (handler.cpp), which saves the
restored body in its `if (ch->desc)` block. Finding no socket, extract_char() then calls
close_socket(), whose non-playing arm logs "Losing player:" and frees the character. Back in
raw_kill(), clearing PLR_WAS_KITTED writes into that freed character. A plain build does not notice
the write, so there this is an ordinary test of the saved restore. An AddressSanitizer build aborts
on it, so there the test is a strict expected failure that accepts only that report: a
heap-use-after-free whose first stack frame is raw_kill(). The clear is a read-modify-write
(REMOVE_BIT, utils.h), so the report names the READ that ASan checks first.

A gtest cannot reach this: it needs a descriptor that has lost its socket, the room sweep, and a
second login that reads the saved file back. The loop bound is tick_until_marker's default budget
of 12 ticks, far below Harnmage's blaze duration (mage level 120, `rots_harness/fixtures.py`).
"""

from __future__ import annotations

import itertools
import os
import subprocess
from pathlib import Path

import pytest

import poison_support
from blaze_support import BLAZE_CAST, LETHAL_HIT, tick_until_marker, wait_for_log_line
from rots_harness import fixtures, launcher
from rots_harness.session import GameSession


class KnownLinklessDeathWriteAfterFree(AssertionError):
    """The server's only problem was AddressSanitizer's heap-use-after-free report whose first frame is
    raw_kill(), the flag clear on the freed player."""


# The binary conftest.choose_launcher() boots.
SERVER_BINARY = Path(__file__).resolve().parents[3] / os.environ.get("ROTS_IT_BINARY", "bin/ageland")

pytestmark = [
    pytest.mark.scenario,
    pytest.mark.xfail(
        condition=launcher.server_binary_is_sanitized(SERVER_BINARY),
        strict=True,
        raises=KnownLinklessDeathWriteAfterFree,
        reason=(
            "write-after-free when a linkless player dies: raw_kill() clears a flag on the character "
            "that extract_char() has just freed through close_socket()"
        ),
    ),
]

PRE_LOOP_SURVIVABLE_HIT = 600  # outlasts the real-time ticks that land before the link has dropped
LINK_DROPPED = "Closing link to: Harnvictim"  # close_socket(), comm.cpp
DEATH_BROADCAST = "Harnvictim is dead!  R.I.P."  # damage_credited()'s POSITION_DEAD arm, fight.cpp
DEATH_CREDITED = "Harnvictim killed by Harnmage"  # die(), fight.cpp: the credit that selects the gentle arm
SERVER_EXIT_TIMEOUT = 30.0  # seconds for an aborting server to finish its report and exit
ASAN_REPORT_START = "ERROR: AddressSanitizer:"  # first line of every AddressSanitizer report
KNOWN_ERROR = "AddressSanitizer: heap-use-after-free"  # on the report's first and SUMMARY lines
KNOWN_ACCESSES = ("READ of size", "WRITE of size")  # the line after the first: the access that touched freed memory
FIRST_FRAME_FUNCTION = "raw_kill"  # fight.cpp: clears PLR_WAS_KITTED on the freed player
EXIT_PROBLEM = "server process exited with status"  # CrashMonitor.check(): the aborted server's exit


def test_a_linkless_player_killed_by_a_room_tick_comes_back_restored(server, imp, mage, victim, harness) -> None:
    imp.command(f"goto {fixtures.ROOM_ARENA_CENTRE}")
    imp.command("transfer harnmage")
    imp.command("transfer harnvictim")
    imp.command("restore harnmage")
    imp.command(f"wizset harnvictim maxhit {PRE_LOOP_SURVIVABLE_HIT}")
    imp.command("restore harnvictim")

    victim.command("west")  # out of the cast so nobody engages anybody
    victim.expect_room("Arena West")
    mage.cast("blaze", success_markers=BLAZE_CAST)
    mage.command("east")  # the caster stays logged in, out of its own fire
    mage.expect_room("Arena East")

    victim.command("east")
    victim.expect_room("Arena Centre")
    victim.drop_link()
    # Only once the server has closed the link does a death take the linkless path.
    wait_for_log_line(imp, server, LINK_DROPPED)

    relogin: GameSession | None = None
    try:
        tick_until_marker(harness, imp, imp, DEATH_BROADCAST, protect=(imp,), refloor=("harnvictim", LETHAL_HIT))
        wait_for_log_line(imp, server, DEATH_CREDITED)
        # Out of the fire, so no tick broadcast can cut the stat reply short.
        imp.command(f"goto {fixtures.ROOM_IMMORTAL_START}")
        imp.expect_room("Immortal Start")
        # A separate transcript directory keeps the linkless session's transcript intact.
        relogin = GameSession(server.handle, server.spec("Harnvictim"), server.character_number("Harnvictim"), server.run_dir / "relogin")
        relogin.login()
        stat = imp.command("stat harnvictim")
    finally:
        if relogin is not None:
            relogin.close()
        problems = _problems_once_the_server_settles(server)
        if problems:
            server.crash_detected = True  # keeps the run directory with the report
            imp.drop_link()  # the server is gone: the session fixtures must not quit on it
            mage.drop_link()
            if _is_the_known_write_after_free(problems, server.handle.log_path.read_text(encoding="latin-1", errors="replace")):
                raise KnownLinklessDeathWriteAfterFree("the known write-after-free on the linkless death:\n" + "\n".join(problems))
        # Asserted here so a server death reports as this failure, not as the lost connection.
        assert not problems, "the server reported a memory error or crash on the linkless death:\n" + "\n".join(problems)

    game_log = server.handle.log_path.read_text(encoding="latin-1", errors="replace")
    assert game_log.index(LINK_DROPPED) < game_log.index(DEATH_CREDITED), f"the victim must die after its link dropped: {game_log[-2000:]}"
    assert stat.hit_points() is not None, f"stat harnvictim returned no hit points: {stat.text}"
    current, maximum = stat.hit_points()
    assert maximum // 4 <= current <= maximum // 4 + poison_support.REGEN_ALLOWANCE, (
        f"the death restore was not saved: a relogged player credited to a player comes back at "
        f"max/4 plus regen, got {current}/{maximum}: {stat.text}"
    )


def _problems_once_the_server_settles(server) -> list[str]:
    """Crash-monitor problems, and when there are any, the exit status as well. An aborting server
    writes its report before it exits, so a check made the moment the socket closes can precede the
    exit; waiting consumes the status here instead of leaving it to a teardown check."""
    problems = server.monitor.check()
    if problems:
        try:
            server.handle.process.wait(timeout=SERVER_EXIT_TIMEOUT)
        except subprocess.TimeoutExpired:
            pass  # still running: the teardown checks report whatever follows
        problems += server.monitor.check()
    return problems


def _is_the_known_write_after_free(problems: list[str], game_log: str) -> bool:
    """True when game.log holds exactly one AddressSanitizer report, the known one, and every problem
    is a line of that report or the exit it caused. Any other report, crash marker or SYSERR makes
    this False, so the test fails."""
    lines = game_log.splitlines()
    report_starts = [index for index, line in enumerate(lines) if ASAN_REPORT_START in line]
    if len(report_starts) != 1 or KNOWN_ERROR not in lines[report_starts[0]]:
        return False
    after_error = lines[report_starts[0] + 1:]
    if not after_error or not after_error[0].lstrip().startswith(KNOWN_ACCESSES):
        return False
    access_stack = list(itertools.takewhile(lambda line: line.lstrip().startswith("#"), after_error[1:]))
    if not access_stack or not access_stack[0].lstrip().startswith("#0 ") or FIRST_FRAME_FUNCTION not in access_stack[0]:
        return False
    return all(KNOWN_ERROR in problem or problem.startswith(EXIT_PROBLEM) for problem in problems)
