"""manual-test-plan.md item 1: a blaze keeps ticking after its caster is slain (the body and
registration serial survive, so the tick still credits the mage) and after the caster's link
drops and the body is purged while another character logs in (the tick credits nobody, and never
the new body).

Timing model: see blaze_support.py's module docstring -- blaze ticks from the real-time fast
block (comm.cpp, ~3s, unconditional regardless of harness_mode), `harness tick`, and
`harness affects()` alike (all three reach `affect_update_room` through the same
`affect_update()`; `affects()` is not a no-op for a room affect, it just does not force blaze's
own roll or run regen). These scenarios floor the victim's hit every loop iteration
(`tick_until_marker`'s `refloor`) so a single tick stays lethal for the whole loop -- regen would
otherwise claw the floor back before the next tick -- and keep their tick budget well under the
affect's nominal duration, with an explicit failure naming the cause if the affect runs out
first under a loaded run -- exactly as `test_blaze_after_quit.py` does. There is no separate
"did it tick at all" check: with the victim's hit floored this low, a landed tick and a death are
the same observable event, so a bare hit-point comparison against the floor would be satisfied
by the `wizset` itself, before any tick ever ran; the death marker is the only non-vacuous
signal.
"""

from __future__ import annotations

import time

import pytest

from blaze_support import LETHAL_HIT, BLAZE_CAST, tick_until_marker
from poison_support import DEATH_MARKER, MAGE_RESPAWN_ROOM
from rots_harness import fixtures, records
from rots_harness.session import GameSession

pytestmark = pytest.mark.scenario


def _blaze_the_centre(imp, mage, victim) -> None:
    imp.command(f"goto {fixtures.ROOM_ARENA_CENTRE}")
    imp.command("transfer harnmage")
    imp.command("transfer harnvictim")
    imp.command("restore harnmage")
    imp.command("restore harnvictim")
    victim.command("west")  # out of the cast so nobody engages anybody
    mage.cast("blaze", success_markers=BLAZE_CAST)


def _tick_until_dead(harness, imp, victim) -> None:
    victim.command("east")
    victim.expect_room("Arena Centre")
    # `imp` stands in the blazing room for every forced tick this loop issues and is otherwise
    # never healed; at Harnmage's raised level (fixtures.py) a tick can kill it outright, and its
    # auto-respawn to Immortal Start would then make room_still_burning() read the wrong room.
    tick_until_marker(harness, imp, victim, DEATH_MARKER, protect=(imp,), refloor=("harnvictim", LETHAL_HIT))


def test_blaze_ticks_survive_the_casters_death_and_still_credit_the_mage(server, imp, mage, victim, harness) -> None:
    _blaze_the_centre(imp, mage, victim)
    imp.command("slay harnmage")
    # A slain player keeps its body and its registration serial: it wakes in its start room.
    respawn_look = mage.expect_room(MAGE_RESPAWN_ROOM)
    assert "Arena Centre" not in respawn_look.text, f"the slain mage must have left the burning room: {respawn_look.text}"

    _tick_until_dead(harness, imp, victim)

    victim_records = records.read_exploits(server.lib_dir, "Harnvictim")
    deaths = [record for record in victim_records if record.type == records.EXPLOIT_DEATH]
    assert any(record.victim_name.lower() == "harnmage" for record in deaths), victim_records


def _await_linkless(imp: GameSession, name: str, timeout: float = 10.0) -> None:
    """Wait until the server has noticed the dropped socket, so the purge that follows takes
    the linkless-body path rather than closing a descriptor the server still holds."""
    deadline = time.monotonic() + timeout
    while True:
        look = imp.command("look")
        if any(name in line and "(linkless)" in line for line in look.text.splitlines()):
            return
        if time.monotonic() >= deadline:
            pytest.fail(f"{name} never showed as linkless in the imp's room: {look.text}")
        time.sleep(0.5)


def test_blaze_ticks_after_a_link_drop_and_purge_credit_nobody_and_never_the_next_login(server, imp, mage, victim, harness) -> None:
    """A link-dropped body persists as linkless in harness mode (idle force-rent is gated off),
    and `register_npc_char()` allocates slots upward, so a scenario cannot make a later login
    reuse the mage's slot; the recycled-slot rule is pinned by
    `CasterSnapshot.ResolveRejectsTheSameSlotAndAddressOnceReRegistered`. What this scenario pins
    is the freed-body arm: after the drop the imp purges the linkless body, so the caster no
    longer resolves, the tick credits nobody, and `Harncaller`, who logs in afterwards, is never
    named.

    `Harncaller`'s roster load room is Arena Centre (fixtures.py), the same room the blaze is
    burning, so logging in after the purge puts it in the blaze's own target pool alongside
    `Harnvictim`; it can take and even die from the room tick like any other occupant, and that
    is not a misattribution by itself -- only a record naming `harncaller` as a killer would be.
    """
    _blaze_the_centre(imp, mage, victim)
    mage.drop_link()
    _await_linkless(imp, "Harnmage")
    # The imp sees only "Ok." and possibly the (GC) mudlog: "disintegrates" goes TO_NOTVICT.
    purge = imp.command("purge harnmage")
    assert "Ok." in purge.text and "Fuuu" not in purge.text, f"the imp must purge the linkless mage: {purge.text}"
    # `caller` is not requested as a fixture: fixtures log in before the test body runs, and the
    # relogin must happen after the purge, once the mage's body and slot are gone.
    caller = GameSession(server.handle, server.spec("Harncaller"), server.character_number("Harncaller"), server.run_dir)
    caller.login()
    try:
        _tick_until_dead(harness, imp, victim)
        victim_records = records.read_exploits(server.lib_dir, "Harnvictim")
        assert not any(record.victim_name.lower() == "harncaller" for record in victim_records), f"the new login must never be credited: {victim_records}"
        assert not any(record.victim_name.lower() == "harnmage" for record in victim_records), f"the purged caster must never be credited: {victim_records}"
        caller_records = records.read_exploits(server.lib_dir, "Harncaller")
        assert not any(record.victim_name.lower() == "harncaller" for record in caller_records), f"the new login must never be credited: {caller_records}"
        caller.quit()
    finally:
        caller.close()
