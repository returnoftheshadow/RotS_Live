"""A blaze never burns a player its caster could not attack with a direct cast. Before each burn,
room_affect_tick() (room_affect_tick.cpp) asks big_brother::is_target_valid() whether the recorded
caster may harm the occupant, and skips a refused one silently; the level band refuses a level-30
caster a level-10 player (big_brother.cpp, is_level_range_appropriate()).

The gtests pin one tick each; this pins the rule over a live room's sweeps, beside a level-11
control whom the same caster may attack. A sweep burns each occupant with probability about 0.38
(affect_update_room(), limits.cpp: number(0, 12) == 0, or number(0, 2) == 0 for a fast spell), so
the loop runs until the control has burned CONTROL_BURNS times, about ten sweeps, over which an
unprotected victim would escape every burn about once in 160 runs. The loop is bounded by the
blaze itself: Harncaller's lasts 33 or 34 sweeps (spell_blaze(), mage.cpp), and a blaze that burns
out first fails the test by name.
"""

from __future__ import annotations

import re

import pytest

from blaze_support import BLAZE_CAST, room_still_burning
from combat_support import VICTIM_LEVEL, stat_replies
from rots_harness import fixtures
from rots_harness.session import GameSession, Transcript

pytestmark = pytest.mark.scenario

CONTROL_BURNS = 4
# More forced ticks than the blaze has sweeps, so the burn-out check, not this, ends a bad run.
TICK_BUDGET = 40
# damage_credited()'s refusal line (fight.cpp); a skipped occupant must not produce it.
HAND_STAYED = "Your hand is stayed."


def _blaze_burns(game_log: str, name: str) -> int:
    """record_spell_damage() (spell_pa.cpp) logs every blaze burn; a tick names the occupant as
    its own attacker."""
    return len(re.findall(rf"spell=blaze, damage=\d+, from {name}\(\d+\) to {name}\(", game_log))


def _game_log(server) -> str:
    return server.handle.log_path.read_text(encoding="latin-1", errors="replace")


def _stat_hit_points(imp: GameSession, name: str) -> tuple[int, int]:
    """`stat <name>` from the burning room, retried past a tick broadcast's early prompt."""
    replies = stat_replies(imp, name, lambda text: f"'{name}'" in text.lower() and Transcript(text).hit_points() is not None)
    hit_points = Transcript(replies[-1]).hit_points()
    assert hit_points is not None, f"stat {name} never returned a parseable reply: {replies}"
    return hit_points


def test_blaze_never_burns_a_player_its_caster_could_not_attack(server, imp, caller, victim, novice, harness) -> None:
    imp.command(f"wizset harnnovice level {VICTIM_LEVEL}")  # the band's lowest: 30 < 3 * 11
    imp.command("wizset harnnovice maxhit 2000")  # about 1100 hit, more than the whole blaze deals
    imp.command("restore harnnovice")
    imp.command("restore harnvictim")  # a login starts below the maximum

    # Both victims leave while the fire is lit, so the cast engages nobody, and the caster then
    # leaves it: the ticks judge a live caster standing elsewhere.
    victim.command("west")
    novice.command("west")
    victim.expect_room("Arena West")
    novice.expect_room("Arena West")
    caller.cast("blaze", success_markers=BLAZE_CAST)
    caller.command("east")
    caller.expect_room("Arena East")
    imp.command(f"goto {fixtures.ROOM_ARENA_CENTRE}")
    victim.command("east")
    novice.command("east")
    victim.expect_room("Arena Centre")
    novice.expect_room("Arena Centre")

    for _ in range(TICK_BUDGET):
        if _blaze_burns(_game_log(server), "Harnnovice") >= CONTROL_BURNS:
            break
        if not room_still_burning(imp, "Arena Centre"):
            pytest.fail(f"the blaze burned out before the level-{VICTIM_LEVEL} control burned {CONTROL_BURNS} times")
        harness.tick()
    else:
        pytest.fail(f"the control burned fewer than {CONTROL_BURNS} times in {TICK_BUDGET} forced ticks")

    game_log = _game_log(server)
    assert _blaze_burns(game_log, "Harnnovice") >= CONTROL_BURNS
    assert _blaze_burns(game_log, "Harnvictim") == 0, f"the level-10 player must never burn: {game_log[-2000:]}"
    current, maximum = _stat_hit_points(imp, "harnvictim")
    assert current == maximum, f"the level-10 player keeps its hit points, got {current}/{maximum}"

    caller.drain(0.2)
    victim.drain(0.2)
    assert HAND_STAYED not in caller.everything, "a skipped occupant is skipped silently"
    assert HAND_STAYED not in victim.everything
