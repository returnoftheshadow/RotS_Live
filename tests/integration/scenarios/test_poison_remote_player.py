from __future__ import annotations

import pytest

from rots_harness import fixtures, records

pytestmark = pytest.mark.scenario

POISON_LANDED = ("You feel very sick.",)
POISON_RESISTED = ("You feel your body fend off the poison.",)
DEATH_MARKER = "You are dead!  Sorry..."


def poison_until_it_lands(mage, victim, attempts: int = 8) -> None:
    for _attempt in range(attempts):
        mage.send_line("cast 'poison' harnvictim")
        try:
            text = victim.expect(POISON_LANDED + POISON_RESISTED, timeout=12.0)
        except AssertionError as timeout:
            pytest.fail(f"{timeout}\nmage side:\n{mage.drain(0.5)[-1500:]}")
        if POISON_LANDED[0] in text:
            return
        mage.drain(0.5)
    pytest.fail(f"poison never landed in {attempts} casts")


def test_remote_player_poison_death_is_gentle_and_fully_attributed(server, imp, mage, victim, harness) -> None:
    imp.command(f"goto {fixtures.ROOM_ARENA_CENTRE}")
    imp.command("restore harnmage")
    imp.command("restore harnvictim")
    imp.command("wizset harnvictim hit 12")  # the cast lands 5, each tick lands 5

    poison_until_it_lands(mage, victim)
    mage.command("west")  # the poisoner is now rooms away and never in the victim's fight
    assert mage.command("look").room_name() == "Arena West"

    died = False
    for _tick in range(4):
        harness.tick()
        text = victim.drain(1.0)
        if DEATH_MARKER in text:
            died = True
            break
    assert died, "the victim should have died of the poison ticks"

    look = victim.command("look")
    assert look.room_name() == "Wood-elf Start", look.text

    stat = imp.command("stat harnvictim")
    current, maximum = stat.hit_points()
    assert current == maximum // 4, f"gentle revive expected hp {maximum // 4}, got {current}: {stat.text}"

    victim_records = records.read_exploits(server.lib_dir, "Harnvictim")
    types = [record.type for record in victim_records]
    assert records.EXPLOIT_POISON in types, victim_records
    assert records.EXPLOIT_MOBDEATH not in types, victim_records
    deaths = [record for record in victim_records if record.type == records.EXPLOIT_DEATH]
    assert any(record.victim_name.lower() == "harnmage" for record in deaths), victim_records

    mage_records = records.read_exploits(server.lib_dir, "Harnmage")
    kills = [record for record in mage_records if record.type == records.EXPLOIT_PK]
    assert any(record.victim_name.lower() == "harnvictim" for record in kills), mage_records
