"""manual-test-plan.md item 1 (quit arm): a blaze keeps ticking, and can still kill, after its
caster quits. The caster quits and its socket closes, which frees the body, so the tick credits
nobody and the victim takes the gentle arm a tick death always took (the registration serial has
nothing left to resolve back to; contrast test_blaze_after_caster_gone.py's slain-caster arm,
where the body survives and the tick still credits the mage).

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

import poison_support
from blaze_support import LETHAL_HIT, BLAZE_CAST, tick_until_marker
from combat_support import BRUTE_ORC_VNUM, neutralize_melee, wait_for_engagement
from poison_support import DEATH_MARKER
from rots_harness import fixtures, records

pytestmark = pytest.mark.scenario

PRE_LOOP_SURVIVABLE_HIT = 600  # outlasts several pre-loop blaze ticks


def test_blaze_ticks_survive_the_casters_quit_and_credit_nobody(server, imp, mage, victim, harness) -> None:
    imp.command(f"goto {fixtures.ROOM_ARENA_CENTRE}")
    imp.command("transfer harnmage")
    imp.command("transfer harnvictim")
    imp.command("restore harnmage")
    # Real-time blaze ticks (up to ~130 each at Harnmage's level) land while the victim walks in,
    # before tick_until_marker's first refloor; a default 72-hit pool dies to one of them and
    # that refloor then lands on the respawned body, spoiling the post-death hit reading.
    imp.command(f"wizset harnvictim maxhit {PRE_LOOP_SURVIVABLE_HIT}")
    imp.command("restore harnvictim")

    victim.command("west")  # keep the victim out of the cast itself so nobody engages anybody
    mage.cast("blaze", success_markers=BLAZE_CAST)
    mage.quit()

    before = imp.command("stat harnvictim").abilities()
    assert before is not None
    victim.command("east")
    victim.expect_room("Arena Centre")

    # `imp` stands in the blazing room for every forced tick this loop issues and is otherwise
    # never healed; at Harnmage's raised level (fixtures.py) a tick can kill it outright, and its
    # auto-respawn to Immortal Start would then make room_still_burning() read the wrong room.
    tick_until_marker(harness, imp, victim, DEATH_MARKER, protect=(imp,), refloor=("harnvictim", LETHAL_HIT))
    victim.expect_room("Wood-elf Start")

    victim_records = records.read_exploits(server.lib_dir, "Harnvictim")
    assert not any(record.victim_name.lower() == "harnmage" for record in victim_records), f"a departed caster must never be named: {victim_records}"

    stat = imp.command("stat harnvictim")
    current, maximum = stat.hit_points()
    assert maximum // 4 <= current <= maximum // 4 + poison_support.REGEN_ALLOWANCE, (
        f"an uncredited tick death takes the gentle arm: hit must be max/4 plus regen, got {current}/{maximum}: {stat.text}"
    )
    after = stat.abilities()
    assert after == before, f"the gentle arm leaves abilities untouched: {before} -> {after}"
    assert not any(record.type == records.EXPLOIT_MOBDEATH for record in victim_records), f"nobody was credited, so no mob-death record: {victim_records}"


def test_blaze_death_while_fighting_a_mob_stays_gentle_when_the_caster_is_gone(server, imp, mage, victim, harness) -> None:
    """The victim is swinging at a defanged brute when the lethal tick lands. With no caster to
    credit, the death is not the brute's: gentle arm, no mob-death record, no full XP loss."""
    imp.command(f"goto {fixtures.ROOM_ARENA_CENTRE}")
    imp.command("transfer harnmage")
    imp.command("transfer harnvictim")
    imp.command("restore harnmage")
    imp.command(f"wizset harnvictim maxhit {PRE_LOOP_SURVIVABLE_HIT}")  # see the first test
    imp.command("restore harnvictim")
    victim.command("west")
    mage.cast("blaze", success_markers=BLAZE_CAST)
    mage.quit()

    imp.command(f"load mob {BRUTE_ORC_VNUM}")
    neutralize_melee(imp, "brute")
    # Blaze burns the brute too; a deep pool keeps it alive and engaged for the whole tick loop.
    imp.command("wizset brute maxhit 4000")
    imp.command("wizset brute hit 4000")
    before = imp.command("stat harnvictim").abilities()
    assert before is not None
    victim.command("east")
    victim.expect_room("Arena Centre")
    victim.command("kill brute")
    wait_for_engagement(imp, "brute", "Harnvictim")

    tick_until_marker(harness, imp, victim, DEATH_MARKER, protect=(imp,), refloor=("harnvictim", LETHAL_HIT))
    victim.expect_room("Wood-elf Start")

    stat = imp.command("stat harnvictim")
    current, maximum = stat.hit_points()
    assert maximum // 4 <= current <= maximum // 4 + poison_support.REGEN_ALLOWANCE, stat.text
    assert stat.abilities() == before
    victim_records = records.read_exploits(server.lib_dir, "Harnvictim")
    assert not any(record.type == records.EXPLOIT_MOBDEATH for record in victim_records), victim_records
    assert not any(record.victim_name.lower() == "harnmage" for record in victim_records), victim_records


def test_blaze_ticks_credit_nobody_while_the_caster_sits_at_the_menu(server, imp, mage, victim, harness) -> None:
    """The mage quits but keeps its connection at the character menu, so its body is still
    registered when the lethal tick lands. It is out of any room, so it must not be credited,
    and the victim takes the gentle arm."""
    imp.command(f"goto {fixtures.ROOM_ARENA_CENTRE}")
    imp.command("transfer harnmage")
    imp.command("transfer harnvictim")
    imp.command("restore harnmage")
    imp.command(f"wizset harnvictim maxhit {PRE_LOOP_SURVIVABLE_HIT}")  # see the first test
    imp.command("restore harnvictim")
    victim.command("west")
    mage.cast("blaze", success_markers=BLAZE_CAST)
    mage.quit_to_menu()
    # close() both frees the parked body and marks the session closed, so fixture teardown
    # does not send a second quit into the menu.
    try:
        before = imp.command("stat harnvictim").abilities()
        assert before is not None
        victim.command("east")
        victim.expect_room("Arena Centre")
        tick_until_marker(harness, imp, victim, DEATH_MARKER, protect=(imp,), refloor=("harnvictim", LETHAL_HIT))
        victim.expect_room("Wood-elf Start")

        victim_records = records.read_exploits(server.lib_dir, "Harnvictim")
        assert not any(record.victim_name.lower() == "harnmage" for record in victim_records), f"a parked caster must never be named: {victim_records}"
        mage_records = records.read_exploits(server.lib_dir, "Harnmage")
        assert not any(record.type == records.EXPLOIT_PK for record in mage_records), f"a parked caster earns no kill: {mage_records}"
        # stat harnmage cannot be read: the parked body is in no room.
        stat = imp.command("stat harnvictim")
        current, maximum = stat.hit_points()
        assert maximum // 4 <= current <= maximum // 4 + poison_support.REGEN_ALLOWANCE, stat.text
        assert stat.abilities() == before
    finally:
        mage.close()
