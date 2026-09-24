"""manual-test-plan.md item 2: poison the victim, then remove the poisoner before the lethal
tick. A quit frees the body, so the tick credits nobody; an imp-slain player keeps its body and
registration serial, so resolve_poisoner still names the mage."""

from __future__ import annotations

import pytest

import poison_support
from combat_support import quit_once_anger_allows
from poison_support import MAGE_RESPAWN_ROOM, affect_ticks_until_death, death_tick_budget, poison_until_it_lands
from rots_harness import fixtures, records

pytestmark = pytest.mark.scenario


def _poison_then_separate(imp, mage, victim, victim_hit: int | None = None) -> None:
    imp.command(f"goto {fixtures.ROOM_ARENA_CENTRE}")
    imp.command("transfer harnmage")
    imp.command("transfer harnvictim")
    imp.command("wizset harnvictim level 11")  # BB needs attacker(30) < defender*3; 11 gives 33 > 30
    if victim_hit is not None:
        imp.command(f"wizset harnvictim hit {victim_hit}")
    poison_until_it_lands(mage, victim, "elf")
    # An offensive cast engages the mage; a wizard transfer disengages both sides
    # (char_from_room stops the fight), so the victim's death is the DoT alone.
    imp.command(f"goto {fixtures.ROOM_ARENA_WEST}")
    imp.command("transfer harnmage")
    mage.expect_room("Arena West")


def test_poisoner_who_quits_before_the_lethal_tick_is_credited_with_nothing(server, imp, mage, victim, harness) -> None:
    _poison_then_separate(imp, mage, victim)
    # The victim's hit stays at its roster default (well above the few points of damage these
    # forced ticks deal) so the anger-clearing ticks cannot land the kill first; it is lowered
    # for the death countdown only afterwards.
    quit_once_anger_allows(mage, harness)

    imp.command("wizset harnvictim hit 10")
    assert affect_ticks_until_death(harness, victim, death_tick_budget(10)), "the victim should die of the forced poison ticks"
    victim.expect_room("Wood-elf Start")

    victim_records = records.read_exploits(server.lib_dir, "Harnvictim")
    types = [record.type for record in victim_records]
    assert records.EXPLOIT_POISON in types, victim_records
    assert records.EXPLOIT_MOBDEATH not in types, victim_records
    assert records.EXPLOIT_DEATH not in types, f"a departed poisoner must leave no death record naming anyone: {victim_records}"
    assert not any(record.victim_name.lower() == "harnmage" for record in victim_records), f"a departed poisoner must never be named: {victim_records}"

    mage_records = records.read_exploits(server.lib_dir, "Harnmage")
    assert not any(record.type == records.EXPLOIT_PK for record in mage_records), mage_records


def test_poisoner_slain_before_the_lethal_tick_is_still_named(server, imp, mage, victim, harness) -> None:
    _poison_then_separate(imp, mage, victim, victim_hit=10)
    imp.command("slay harnmage")
    # The slain mage keeps its body and stays logged in, waking in its start room.
    respawn_look = mage.expect_room(MAGE_RESPAWN_ROOM)
    assert "Arena West" not in respawn_look.text, f"the slain mage must have left the room it was slain in: {respawn_look.text}"

    assert affect_ticks_until_death(harness, victim, death_tick_budget(10)), "the victim should die of the forced poison ticks"

    stat = imp.command("stat harnvictim")
    current, maximum = stat.hit_points()
    pinned = maximum // 4
    assert pinned <= current <= pinned + poison_support.REGEN_ALLOWANCE, (
        f"gentle poison death revives at a quarter of {maximum} HP ({pinned}), plus up to "
        f"{poison_support.REGEN_ALLOWANCE} for real-time regen since the death tick, got {current}: {stat.text}"
    )

    victim_records = records.read_exploits(server.lib_dir, "Harnvictim")
    deaths = [record for record in victim_records if record.type == records.EXPLOIT_DEATH]
    assert any(record.victim_name.lower() == "harnmage" for record in deaths), victim_records
    assert records.EXPLOIT_POISON in [record.type for record in victim_records], victim_records

    mage_records = records.read_exploits(server.lib_dir, "Harnmage")
    assert any(record.type == records.EXPLOIT_PK and record.victim_name.lower() == "harnvictim" for record in mage_records), mage_records
