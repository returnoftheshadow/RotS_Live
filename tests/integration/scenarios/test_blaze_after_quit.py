"""manual-test-plan.md item 1 (quit arm): a blaze keeps ticking, and can still kill, after its
caster quits -- and credits nobody, since a quit frees the caster's body (the registration
serial has nothing left to resolve back to; contrast test_blaze_after_caster_gone.py's slain-
caster arm, where the body survives and the tick still credits the mage).

Timing model: see blaze_support.py's module docstring -- blaze ticks from the real-time fast
block (comm.cpp, ~3s, unconditional regardless of harness_mode) and this harness's own
`harness tick`, never from `harness affects()`. Both spend the same room affect's duration on
every call they make and both regenerate every character's hit points, so this scenario
re-floors the victim's hit every loop iteration (`blaze_support.floor_hit`/`tick_until_hp_drops`/
`tick_until_marker`'s `refloor`) so a single successful tick stays lethal for the whole loop, and
keeps its tick budgets well under the affect's nominal duration, with an explicit failure naming
the cause if the affect runs out first under a loaded run.
"""

from __future__ import annotations

import pytest

from blaze_support import LETHAL_HIT, BLAZE_CAST, tick_until_hp_drops, tick_until_marker
from poison_support import DEATH_MARKER
from rots_harness import fixtures, records

pytestmark = pytest.mark.scenario


def test_blaze_ticks_survive_the_casters_quit_and_credit_nobody(server, imp, mage, victim, harness) -> None:
    imp.command(f"goto {fixtures.ROOM_ARENA_CENTRE}")
    imp.command("transfer harnmage")
    imp.command("transfer harnvictim")
    imp.command("restore harnmage")
    imp.command("restore harnvictim")

    victim.command("west")  # keep the victim out of the cast itself so nobody engages anybody
    mage.cast("blaze", success_markers=BLAZE_CAST)
    mage.quit()

    victim.command("east")
    victim.expect_room("Arena Centre")
    before = imp.command("stat harnvictim").hit_points()[0]

    after = tick_until_hp_drops(harness, imp, "harnvictim", before)
    assert after < before, f"the blaze should still tick after its caster quit ({before} -> {after})"

    tick_until_marker(harness, imp, victim, DEATH_MARKER, refloor=("harnvictim", LETHAL_HIT))
    victim.expect_room("Wood-elf Start")

    victim_records = records.read_exploits(server.lib_dir, "Harnvictim")
    assert not any(record.victim_name.lower() == "harnmage" for record in victim_records), f"a departed caster must never be named: {victim_records}"
