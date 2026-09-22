"""manual-test-plan.md item 4 (summon at zone distance): a summon across a squared map distance
of 20 always fails; the same summon inside the zone succeeds. spell_summon (mage.cpp ~850-863)
adds dx*dx + dy*dy between the two zones' map coordinates to the victim's save bonus, and
new_saves_spell (spell_pa.cpp ~262) returns a save unconditionally once that bonus reaches 20.
Zone 12 sits at (4,2) against zone 11's (0,0)
(tests/integration/world/zon/12.zon), so the cross-zone case is deterministic; the old XOR
arithmetic gave 6 here and a random outcome. A gtest pins the formula
(mage_tests.cpp SummonSaveBonusUsesSquaredZoneDistance); this pins the live coordinate path.

Bound: FAILED_ATTEMPTS forced saves, allowed up to FAILED_ATTEMPTS + 3 casts (a fizzle spends a
cast without forcing a save, and the knowledge-check odds of more than three fizzles in six
summon casts are negligible); each cast waited on its own resolution line, so the loop cannot
outrun the caster's mana (600 at the fixture, summon costs far less per cast).
"""
from __future__ import annotations

import pytest

from rots_harness import fixtures
from rots_harness.session import GameSession
from test_summon import SUMMON_SUCCESS, _room_line

pytestmark = pytest.mark.scenario

SUMMON_FAILED = "You failed."  # spell_summon's send_to_char on a saved roll (mage.cpp:882)
CONCENTRATION_LOST = "You lost your concentration!"  # do_cast, spell_pa.cpp:932
FAILED_ATTEMPTS = 6
CAST_BUDGET = FAILED_ATTEMPTS + 3


def _stage(imp: GameSession, victim: GameSession, victim_room: int, victim_room_name: str) -> None:
    imp.command(f"goto {fixtures.ROOM_ARENA_EAST}")
    imp.command("transfer harncaller")
    imp.command("restore harncaller")
    imp.command(f"goto {victim_room}")
    imp.command("transfer harnvictim")
    victim.expect_room(victim_room_name)


def test_summon_across_squared_distance_twenty_always_fails(server, imp, caller, victim) -> None:
    _stage(imp, victim, fixtures.ROOM_DISTANT_CELL, "Distant Cell")
    caller.expect_room("Arena East")
    forced_failures = 0
    for cast in range(1, CAST_BUDGET + 1):
        caller.send_line("cast 'summon' harnvictim")
        reply = caller.expect([SUMMON_FAILED, CONCENTRATION_LOST, *SUMMON_SUCCESS], timeout=12.0)
        if CONCENTRATION_LOST in reply:
            continue  # a fizzle is a spent cast, not a forced failure
        assert SUMMON_FAILED in reply, f"cast {cast}: a squared distance of 20 must force the save:\n{reply}"
        forced_failures += 1
        if forced_failures == FAILED_ATTEMPTS:
            break
    else:
        pytest.fail(f"only {forced_failures} of {FAILED_ATTEMPTS} forced saves in {CAST_BUDGET} casts")
    stat = imp.command("stat harnvictim")
    assert _room_line(fixtures.ROOM_DISTANT_CELL) in stat.text, stat.text


def test_summon_inside_the_zone_still_lands(server, imp, caller, victim) -> None:
    _stage(imp, victim, fixtures.ROOM_CORRIDOR_TWO, "Corridor Two")
    caller.expect_room("Arena East")
    caller.cast("summon", "harnvictim", success_markers=SUMMON_SUCCESS, attempts=10)
    victim.expect_room("Arena East")
