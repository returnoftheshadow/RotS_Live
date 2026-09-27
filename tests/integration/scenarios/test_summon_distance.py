"""manual-test-plan.md item 4 (summon at zone distance): the zone-distance save bonus is the
straight-line distance between the two zones' map squares, so a summon five squares away lands.
spell_summon (mage.cpp ~859-867) adds that distance, rounded down, to the victim's save bonus,
and new_saves_spell (spell_pa.cpp ~252-271) returns a save unconditionally once the bonus
reaches 20. Zone 12 sits at (3,4) against zone 11's (0,0) (tests/integration/world/zon/12.zon):
straight-line 5, while the sum of squared deltas, 25, would force every save. The gtests pin the
formula (mage_tests.cpp SummonDistanceBonusIs*); this pins the live coordinate path.

The landing is deterministic: the victim saves when d20 + save_value > caster DC. Harncaller's DC
is 10 + mage level 30 / 3 + (int 40 - 8) / 4 = 28 (get_saving_throw_dc); Harnvictim, with no
mage levels and int 0, has save_value (0 - 8) / 4 + 5 = 3 (get_character_saving_throw), so its
best roll reaches 23. Both are set with `wizset ... int` and copied into the live ability by
`restore` (do_restore, act_wiz.cpp ~1618), which the stat check below confirms.

Bound: CAST_BUDGET casts; a fizzle spends a cast without reaching the save, and the odds of that
many fizzles in a row are negligible. Each cast waits on its own resolution line, so the loop
cannot outrun the caster's mana.
"""
from __future__ import annotations

import pytest

from rots_harness import fixtures
from rots_harness.session import GameSession
from test_summon import SUMMON_SUCCESS, _room_line

pytestmark = pytest.mark.scenario

SUMMON_FAILED = "You failed."  # spell_summon's send_to_char on a saved roll (mage.cpp:886)
CONCENTRATION_LOST = "You lost your concentration!"  # do_cast, spell_pa.cpp:921
CAST_BUDGET = 5
CASTER_INT = 40  # wizset's ceiling for int (act_wiz.cpp, field 13)
VICTIM_INT = 0


def _stage(imp: GameSession, victim: GameSession, victim_room: int, victim_room_name: str) -> None:
    imp.command(f"goto {fixtures.ROOM_ARENA_EAST}")
    imp.command("transfer harncaller")
    imp.command("restore harncaller")
    imp.command(f"goto {victim_room}")
    imp.command("transfer harnvictim")
    victim.expect_room(victim_room_name)


def _set_intelligence(imp: GameSession, name: str, value: int) -> None:
    imp.command(f"wizset {name} int {value}")
    imp.command(f"restore {name}")
    stat = imp.command(f"stat {name}")
    abilities = stat.abilities()
    assert abilities is not None and abilities["int"] == value, f"{name}'s int never became {value}:\n{stat.text}"


def test_summon_across_five_squares_lands(server, imp, caller, victim) -> None:
    _stage(imp, victim, fixtures.ROOM_DISTANT_CELL, "Distant Cell")
    caller.expect_room("Arena East")
    _set_intelligence(imp, "harncaller", CASTER_INT)
    _set_intelligence(imp, "harnvictim", VICTIM_INT)

    for cast in range(1, CAST_BUDGET + 1):
        caller.send_line("cast 'summon' harnvictim")
        reply = caller.expect([SUMMON_FAILED, CONCENTRATION_LOST, *SUMMON_SUCCESS], timeout=12.0)
        if CONCENTRATION_LOST in reply:
            continue  # a fizzle never reaches the save
        assert SUMMON_FAILED not in reply, f"cast {cast}: a distance of 5 squares cannot win this save:\n{reply}"
        break
    else:
        pytest.fail(f"every one of {CAST_BUDGET} summon casts fizzled")
    victim.expect_room("Arena East")
    stat = imp.command("stat harnvictim")
    assert _room_line(fixtures.ROOM_ARENA_EAST) in stat.text, stat.text


def test_summon_inside_the_zone_still_lands(server, imp, caller, victim) -> None:
    _stage(imp, victim, fixtures.ROOM_CORRIDOR_TWO, "Corridor Two")
    caller.expect_room("Arena East")
    caller.cast("summon", "harnvictim", success_markers=SUMMON_SUCCESS, attempts=10)
    victim.expect_room("Arena East")
