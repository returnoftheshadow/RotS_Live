"""manual-test-plan.md item 1: a blaze keeps ticking after its caster is slain (the body and
registration serial survive, so the tick still credits the mage) and after the caster's link
drops while another character logs in (no misattribution to the new body).

Timing model: see blaze_support.py's module docstring -- blaze ticks from the real-time fast
block (comm.cpp, ~3s, unconditional regardless of harness_mode) and `harness tick`'s
`fast_update()`/`affect_update()` pair, never from `harness affects()` (which only forces the
one slow *person*-affect phase compare, and blaze's own application roll is gated on neither
that phase nor it). Both spend the same room affect's duration on every call they make and both
regenerate every character's hit points, so these scenarios re-floor the victim's hit every loop
iteration (`blaze_support.tick_until_hp_drops`/`tick_until_marker`'s `refloor`) so a single tick
stays lethal for the whole loop, and keep their tick budgets well under the affect's nominal
duration, with an explicit failure naming the cause if the affect runs out first under a loaded
run -- exactly as `test_blaze_after_quit.py` does.
"""

from __future__ import annotations

import pytest

from blaze_support import LETHAL_HIT, BLAZE_CAST, read_hp, tick_until_hp_drops, tick_until_marker
from poison_support import DEATH_MARKER
from rots_harness import fixtures, records
from rots_harness.session import GameSession

pytestmark = pytest.mark.scenario


def _blaze_the_centre(imp, mage, victim) -> int:
    imp.command(f"goto {fixtures.ROOM_ARENA_CENTRE}")
    imp.command("transfer harnmage")
    imp.command("transfer harnvictim")
    imp.command("restore harnmage")
    imp.command("restore harnvictim")
    victim.command("west")  # out of the cast so nobody engages anybody
    mage.cast("blaze", success_markers=BLAZE_CAST)
    return read_hp(imp, "harnvictim")


def _tick_until_dead(harness, imp, victim, before: int) -> None:
    victim.command("east")
    victim.expect_room("Arena Centre")
    after = tick_until_hp_drops(harness, imp, "harnvictim", before)
    assert after < before, f"the blaze should still tick ({before} -> {after})"
    tick_until_marker(harness, imp, victim, DEATH_MARKER, refloor=("harnvictim", LETHAL_HIT))


def test_blaze_ticks_survive_the_casters_death_and_still_credit_the_mage(server, imp, mage, victim, harness) -> None:
    before = _blaze_the_centre(imp, mage, victim)
    imp.command("slay harnmage")
    assert mage.command("look").room_name() is not None, "a slain player keeps its body and its registration serial"

    _tick_until_dead(harness, imp, victim, before)

    victim_records = records.read_exploits(server.lib_dir, "Harnvictim")
    deaths = [record for record in victim_records if record.type == records.EXPLOIT_DEATH]
    assert any(record.victim_name.lower() == "harnmage" for record in deaths), victim_records


def test_blaze_ticks_after_a_link_drop_never_name_the_next_login(server, imp, mage, victim, harness) -> None:
    """Controller ruling (2026-09-20 slice-2-scenarios plan self-review): a link-dropped body
    persists as linkless in harness mode (idle force-rent is gated off), so the room-affect
    owner check may or may not still resolve `Harnmage` for the kill depending on timing. Only
    the hard requirements are pinned here: `Harncaller`, who logs in after the drop and reuses
    the freed descriptor slot, is never credited as the killer in any exploit record, and there
    is no crash.

    `Harncaller`'s roster load room is Arena Centre (fixtures.py), the same room the blaze is
    burning, so logging in after the drop puts it in the blaze's own target pool alongside
    `Harnvictim`; it can take and even die from the room tick like any other occupant, and that
    is not a misattribution by itself -- only a record naming `harncaller` as a killer would be.

    Observed on the runs used to green this test: the tick still named `Harnmage` (the linkless
    body was not reaped before the kill landed), on `Harnvictim`'s own death record and, when
    the room tick also happened to catch `Harncaller`, on its death record too -- see
    task-6-report.md for the run transcripts.
    """
    before = _blaze_the_centre(imp, mage, victim)
    mage.drop_link()
    # `caller` is not requested as a fixture: fixtures log in before the test body runs, and the
    # relogin must happen after the drop so it reuses the freed descriptor state (brief, Task 6).
    caller = GameSession(server.handle, server.spec("Harncaller"), server.character_number("Harncaller"), server.run_dir)
    caller.login()
    try:
        _tick_until_dead(harness, imp, victim, before)
        victim_records = records.read_exploits(server.lib_dir, "Harnvictim")
        assert not any(record.victim_name.lower() == "harncaller" for record in victim_records), f"the new login must never be credited: {victim_records}"
        caller_records = records.read_exploits(server.lib_dir, "Harncaller")
        assert not any(record.victim_name.lower() == "harncaller" for record in caller_records), f"the new login must never be credited: {caller_records}"
    finally:
        try:
            caller.quit()
        except Exception:
            caller.close()
