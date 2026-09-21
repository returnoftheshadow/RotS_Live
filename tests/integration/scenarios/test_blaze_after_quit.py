"""manual-test-plan.md item 1 (quit arm): a blaze keeps ticking, and can still kill, after its
caster quits -- and credits nobody, since a quit frees the caster's body (the registration
serial has nothing left to resolve back to; contrast test_blaze_after_caster_gone.py's slain-
caster arm, where the body survives and the tick still credits the mage).

Timing model: see blaze_support.py's module docstring -- blaze ticks from the real-time fast
block (comm.cpp, ~3s, unconditional regardless of harness_mode), this harness's own
`harness tick`, and `harness affects()` alike (all three reach `affect_update_room` through the
same `affect_update()`; `affects()` is not a no-op for a room affect, it just does not force
blaze's own roll or run regen). This scenario floors the victim's hit every loop iteration
(`tick_until_marker`'s `refloor`) so a single successful tick stays lethal for the whole loop --
regen would otherwise claw the floor back before the next tick -- and keeps its tick budget well
under the affect's nominal duration, with an explicit failure naming the cause if the affect
runs out first under a loaded run. There is no separate "did it tick at all" check here: with the
victim's hit floored this low, a landed tick and a death are the same observable event (see
`blaze_support`'s `LETHAL_HIT` note on the rare non-lethal case), so the death marker below is
the only signal that is not vacuous -- a bare hit-point comparison against the floor value would
be satisfied by the `wizset` itself, before any tick ever ran.
"""

from __future__ import annotations

import pytest

from blaze_support import LETHAL_HIT, BLAZE_CAST, tick_until_marker
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

    tick_until_marker(harness, imp, victim, DEATH_MARKER, refloor=("harnvictim", LETHAL_HIT))
    victim.expect_room("Wood-elf Start")

    victim_records = records.read_exploits(server.lib_dir, "Harnvictim")
    assert not any(record.victim_name.lower() == "harnmage" for record in victim_records), f"a departed caster must never be named: {victim_records}"
