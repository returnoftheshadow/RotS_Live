"""manual-test-plan.md item 2, control case: the poisoner stays online but out of the death
room; the victim dies of the tick alone and every record names the poisoner."""

from __future__ import annotations

import pytest

import poison_support
from combat_support import quit_once_anger_allows
from poison_support import affect_ticks_until_death, death_tick_budget, poison_until_it_lands
from rots_harness import fixtures, records

pytestmark = pytest.mark.scenario


def test_remote_player_poison_death_is_gentle_and_fully_attributed(server, imp, mage, victim, harness) -> None:
    imp.command(f"goto {fixtures.ROOM_ARENA_CENTRE}")
    imp.command("transfer harnmage")
    imp.command("transfer harnvictim")
    imp.command("wizset harnvictim level 11")  # BB needs attacker(30) < defender*3; 11 gives 33 > 30
    imp.command("wizset harnvictim hit 10")

    poison_until_it_lands(mage, victim, "elf")
    # An offensive cast engages the mage; a wizard transfer disengages both sides
    # (char_from_room stops the fight), so the victim dies of the DoT alone.
    imp.command(f"goto {fixtures.ROOM_ARENA_WEST}")
    imp.command("transfer harnmage")
    mage.expect_room("Arena West")

    assert affect_ticks_until_death(harness, victim, death_tick_budget(10)), "the victim should have died of the forced poison ticks"

    victim.expect_room("Wood-elf Start")

    stat = imp.command("stat harnvictim")
    current, maximum = stat.hit_points()
    pinned = maximum // 4
    assert pinned <= current <= pinned + poison_support.REGEN_ALLOWANCE, (
        f"gentle poison death revives at a quarter of {maximum} HP ({pinned}), plus up to "
        f"{poison_support.REGEN_ALLOWANCE} for real-time regen since the death tick, got {current}: {stat.text}"
    )

    victim_records = records.read_exploits(server.lib_dir, "Harnvictim")
    types = [record.type for record in victim_records]
    assert records.EXPLOIT_POISON in types, victim_records
    assert records.EXPLOIT_MOBDEATH not in types, victim_records
    deaths = [record for record in victim_records if record.type == records.EXPLOIT_DEATH]
    assert any(record.victim_name.lower() == "harnmage" for record in deaths), victim_records

    mage_records = records.read_exploits(server.lib_dir, "Harnmage")
    kills = [record for record in mage_records if record.type == records.EXPLOIT_PK]
    assert any(record.victim_name.lower() == "harnvictim" for record in kills), mage_records

    quit_once_anger_allows(mage, harness)  # the poison cast angered the caster
