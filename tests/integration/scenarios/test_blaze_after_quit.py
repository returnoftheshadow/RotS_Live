from __future__ import annotations

import pytest

from rots_harness import fixtures, records

pytestmark = pytest.mark.scenario

BLAZE_CAST = ("You breathe out fire.",)
DEATH_MARKER = "You are dead!  Sorry..."


def test_blaze_ticks_survive_the_casters_quit_and_credit_nobody(server, imp, mage, victim, harness) -> None:
    imp.command(f"goto {fixtures.ROOM_ARENA_CENTRE}")
    imp.command("transfer harnmage")
    imp.command("transfer harnvictim")
    imp.command("restore harnmage")
    imp.command("wizset harnvictim maxhit 300")
    imp.command("restore harnvictim")

    victim.command("west")  # keep the victim out of the cast itself so nobody engages anybody
    mage.cast("blaze", success_markers=BLAZE_CAST)
    mage.quit()

    victim.command("east")
    assert victim.command("look").room_name() == "Arena Centre"
    before = imp.command("stat harnvictim").hit_points()[0]

    harness.tick()
    after = imp.command("stat harnvictim").hit_points()[0]
    assert after < before, f"the blaze should still tick after its caster quit ({before} -> {after})"

    died = False
    for _tick in range(30):
        harness.tick()
        if DEATH_MARKER in victim.drain(1.0):
            died = True
            break
    assert died, "the blaze ticks should eventually kill the victim"
    assert victim.command("look").room_name() == "Wood-elf Start"

    victim_records = records.read_exploits(server.lib_dir, "Harnvictim")
    assert not any(record.victim_name.lower() == "harnmage" for record in victim_records), f"a departed caster must never be named: {victim_records}"
