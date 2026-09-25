"""A linkless player killed by a room tick comes back with the gentle death restore saved, and the
server survives the death.

A dropped link leaves the body in the world with its descriptor still attached: close_socket()
(comm.cpp) saves, logs "Closing link to:" and marks the descriptor linkless without clearing the
character's `desc`. A blaze tick credited to Harnmage, who stays logged in, kills the body through
damage_credited() and die() into raw_kill() (fight.cpp). raw_kill() sets hit points to max/4 (the
gentle arm of a player-credited death) and calls extract_char() (handler.cpp), which saves the
restored body and, finding no socket, frees it through close_socket(). The scenario pins that the
saved restore reaches a second login and that nothing touches the freed body. The loop bound is
tick_until_marker's default budget of 12 ticks, far below Harnmage's blaze duration (mage level
120, `rots_harness/fixtures.py`).
"""

from __future__ import annotations

import pytest

import poison_support
from blaze_support import BLAZE_CAST, LETHAL_HIT, tick_until_marker, wait_for_log_line
from rots_harness import fixtures
from rots_harness.session import GameSession

pytestmark = pytest.mark.scenario

PRE_LOOP_SURVIVABLE_HIT = 600  # outlasts the real-time ticks that land before the link has dropped
LINK_DROPPED = "Closing link to: Harnvictim"  # close_socket(), comm.cpp
DEATH_BROADCAST = "Harnvictim is dead!  R.I.P."  # damage_credited()'s POSITION_DEAD arm, fight.cpp
DEATH_CREDITED = "Harnvictim killed by Harnmage"  # die(), fight.cpp: the credit that selects the gentle arm


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

    game_log = server.handle.log_path.read_text(encoding="latin-1", errors="replace")
    assert game_log.index(LINK_DROPPED) < game_log.index(DEATH_CREDITED), f"the victim must die after its link dropped: {game_log[-2000:]}"
    assert stat.hit_points() is not None, f"stat harnvictim returned no hit points: {stat.text}"
    current, maximum = stat.hit_points()
    assert maximum // 4 <= current <= maximum // 4 + poison_support.REGEN_ALLOWANCE, (
        f"the death restore was not saved: a relogged player credited to a player comes back at "
        f"max/4 plus regen, got {current}/{maximum}: {stat.text}"
    )

