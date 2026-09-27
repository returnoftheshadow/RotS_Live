"""A wearer of an item that sets the poison bit stays poisoned, and keeps losing hit points, after a
timed poison expires; taking the item off ends it.

Mechanism: equip_char() (handler.cpp) applies the worn amulet's APPLY_BITVECTOR 11 line, setting
AFF_POISON with no SPELL_POISON affect behind it. When the spell poison expires,
affect_update_person() (limits.cpp) calls affect_remove() (handler.cpp), which clears the bit and
then re-applies every worn item through affect_total(). point_update()'s gear-poison arm
(limits.cpp) deals 5 damage each hourly tick to a character with the bit and no poison affect.
unequip_char() clears the bit.

A gtest pins the flag (src/tests/gear_poison_tests.cpp); this scenario adds the world file's item,
the wear and remove commands, a real mystic's poison, and the hourly tick's damage. The expiry
loop's budget is the poison's remaining duration, read from `stat`, plus two ticks for the expiry
itself. Harnvictim's maximum is raised first so that the whole poison cannot kill it.
"""

from __future__ import annotations

import pytest

import poison_support
from combat_support import VICTIM_LEVEL, move_out_of_the_fight, quit_once_anger_allows, read_affect_listing
from poison_support import AMULET_REMOVED, affect_flags, hit_points, poison_until_it_lands, spell_affects, wear_the_sickly_amulet
from rots_harness import fixtures

pytestmark = pytest.mark.scenario

GEAR_POISON_DAMAGE = 5  # point_update()'s gear-poison arm, limits.cpp
# wizset maxhit writes constabilities.hit; recalc_abilities() scales it by CON, so this gives about
# 1100 hit points (gotchas.md), far above 5 per tick for the poison's whole duration.
VICTIM_MAX_HIT = 2000


def test_a_poison_item_keeps_its_wearer_poisoned_after_a_spell_poison_expires(server, imp, mage, victim, harness) -> None:
    imp.command(f"goto {fixtures.ROOM_ARENA_CENTRE}")
    imp.command("transfer harnmage")
    imp.command("transfer harnvictim")
    mage.expect_room("Arena Centre")
    victim.expect_room("Arena Centre")
    imp.command(f"wizset harnvictim level {VICTIM_LEVEL}")  # Big Brother: attacker 30 < 3 * 11
    imp.command(f"wizset harnvictim maxhit {VICTIM_MAX_HIT}")
    imp.command("restore harnvictim")

    wear_the_sickly_amulet(imp, victim, "harnvictim")
    worn = read_affect_listing(imp, "harnvictim")
    assert "POISON" in affect_flags(worn), f"the worn amulet must set the POISON flag: {worn}"
    assert not spell_affects(worn, "poison"), f"the amulet's poison is a bare flag, with no poison affect: {worn}"

    poison_until_it_lands(mage, victim, "elf")
    move_out_of_the_fight(imp, mage, "harnmage", "harnvictim")  # melee would muddy the hit points
    poisoned = read_affect_listing(imp, "harnvictim")
    running = spell_affects(poisoned, "poison")
    assert len(running) == 1, f"the landed poison must show as one poison affect: {poisoned}"

    budget = running[0].duration + 2
    listing = poisoned
    for _tick in range(budget):
        harness.affects()
        listing = read_affect_listing(imp, "harnvictim")
        if not spell_affects(listing, "poison"):
            break
    else:
        pytest.fail(f"the spell poison never expired in {budget} forced ticks: {listing}")

    assert "POISON" in affect_flags(listing), f"the amulet's POISON flag must survive the spell poison's expiry: {listing}"

    harness.tick()
    after_tick = read_affect_listing(imp, "harnvictim")
    hit_before = hit_points(listing)
    hit_after = hit_points(after_tick)
    assert hit_after <= hit_before - GEAR_POISON_DAMAGE + poison_support.REGEN_ALLOWANCE, (
        f"an hourly tick must still take {GEAR_POISON_DAMAGE} hit points from the wearer, less up to "
        f"{poison_support.REGEN_ALLOWANCE} of regen: {hit_before} -> {hit_after}"
    )

    victim.send_line("remove amulet")
    victim.expect((AMULET_REMOVED,))
    removed = read_affect_listing(imp, "harnvictim")
    assert "POISON" not in affect_flags(removed), f"taking the amulet off must clear the POISON flag: {removed}"

    quit_once_anger_allows(mage, harness)  # the poison cast angered the caster
    quit_once_anger_allows(victim, harness)  # fighting back before the fight ended angered the victim
