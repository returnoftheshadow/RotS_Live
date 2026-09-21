"""manual-test-plan.md item 2, punishment case (c): a player's poison, death while engaged with an
unrelated real mob (the brute, mob 1133). Harsh penalty, EXPLOIT_MOBDEATH names the brute, and the
poisoner keeps EXPLOIT_PK even though it left the room before the lethal tick -- kill_contributors()
(fight.cpp) always offers resolve_poisoner()'s answer as a contributor, which reads the poison
origin recorded on the victim when the spell landed, independent of who is still fighting at death.

classify_pc_death() (fight.cpp) only takes the poison-carveout branch (mob_death, the harsh path
this scenario exists to pin) when the *killing* attack_type is SPELL_POISON; a death from the
brute's own melee instead falls through to the pre-existing `legacy` branch, which happens to read
out identically here (the killer is a real mob, so death_counts_as_player_kill() and
mobdeath_record_mob() both land on the same harsh-and-name-the-mob answers) but would never
exercise the new mob_death branch. So the brute is deliberately defanged with
`combat_support.neutralize_melee` and the forced `harness affects` ticks are what actually carry
the poison DoT to the kill; EXPLOIT_POISON showing up in the victim's own record (added in die()
only when attack_type == SPELL_POISON) is the read-back confirmation that the poison tick, not a
stray landed swing, delivered the fatal blow.
"""

from __future__ import annotations

import pytest

import poison_support
from combat_support import neutralize_melee, wait_for_engagement
from poison_support import affect_ticks_until_death, death_tick_budget, poison_until_it_lands
from rots_harness import fixtures, records

pytestmark = pytest.mark.scenario


def test_player_poison_death_while_engaged_with_the_brute_is_harsh_and_keeps_the_pk_record(server, imp, mage, victim, harness) -> None:
    imp.command(f"goto {fixtures.ROOM_ARENA_CENTRE}")
    imp.command("transfer harnmage")
    imp.command("transfer harnvictim")
    imp.command("wizset harnvictim level 11")  # BB needs attacker(30) < defender*3; 11 gives 33 > 30
    imp.command("wizset harnvictim maxhit 400")
    imp.command("restore harnvictim")
    before = imp.command("stat harnvictim").abilities()
    assert before is not None

    poison_until_it_lands(mage, victim, "elf")
    # An offensive cast engages the mage; a wizard transfer disengages both sides (char_from_room
    # stops the fight), so the mage plays no further part except as the recorded poison origin
    # that resolve_poisoner() reads back at the death tick.
    imp.command(f"goto {fixtures.ROOM_ARENA_WEST}")
    imp.command("transfer harnmage")
    mage.expect_room("Arena West")

    imp.command(f"goto {fixtures.ROOM_ARENA_CENTRE}")
    imp.command("load mob 1133")
    imp.command("wizset brute maxhit 4000")
    imp.command("wizset brute hit 4000")  # survives the victim's own attacks for the whole test
    neutralize_melee(imp, "brute")
    victim.command("kill brute")
    wait_for_engagement(imp, "brute", "Harnvictim")
    imp.command("wizset harnvictim hit 10")

    assert affect_ticks_until_death(harness, victim, death_tick_budget(10) + 4), "the victim should die engaged with the brute"
    victim.expect_room("Wood-elf Start")

    stat = imp.command("stat harnvictim")
    current, _maximum = stat.hit_points()
    assert 1 <= current <= 1 + poison_support.REGEN_ALLOWANCE, (
        f"harsh poison death revives at 1 HP, plus up to {poison_support.REGEN_ALLOWANCE} for "
        f"real-time regen since the death tick, got {current}: {stat.text}"
    )
    after = stat.abilities()
    assert after is not None
    for name, value in before.items():
        assert after[name] == value * 2 // 3, f"{name}: {value} -> {after[name]} (expected {value * 2 // 3})"

    victim_records = records.read_exploits(server.lib_dir, "Harnvictim")
    mob_deaths = [record for record in victim_records if record.type == records.EXPLOIT_MOBDEATH]
    assert any("brute" in record.victim_name.lower() for record in mob_deaths), victim_records
    assert records.EXPLOIT_POISON in [record.type for record in victim_records], (
        f"the brute was defanged so only the poison tick could deliver the kill: {victim_records}"
    )

    mage_records = records.read_exploits(server.lib_dir, "Harnmage")
    assert any(record.type == records.EXPLOIT_PK and record.victim_name.lower() == "harnvictim" for record in mage_records), mage_records
