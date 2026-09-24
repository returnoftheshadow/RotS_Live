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

The mob_death punishment, not the credited killer (the mage), also decides three more outcomes.
die() (fight.cpp) takes the full mob-death experience loss because death_takes_full_mob_xp_loss()
holds for mob_death. raw_kill() hands the punishment to make_corpse(), whose
death_strips_corpse_containers() then leaves worn-type items inside their containers, so the cap
stays in the bag. pkill_create() (pkill.cpp) builds PK table rows from die()'s contributor list, so
the mage, fighting nobody at the death, still gets a row in lib/misc/pklist. No gtest can drive
die() or raw_kill() with a player victim (fight_credit_tests.cpp, above the kill_contributor_list
tests). The victim's own swings at the brute earn hit experience (fight.cpp damage()) between the
experience reading and the death, so the loss check allows HIT_EXPERIENCE_SLACK below the full
loss; the tenth alone (1,515 here) never reaches that floor.
"""

from __future__ import annotations

import pytest

import poison_support
from combat_support import VICTIM_LEVEL, neutralize_melee, quit_once_anger_allows, wait_for_engagement
from poison_support import affect_ticks_until_death, death_loss, death_tick_budget, poison_until_it_lands
from rots_harness import fixtures, records

pytestmark = pytest.mark.scenario

# Inside level 11's band, xp_to_level(11) = 181,500 to xp_to_level(12) = 216,000 (limits.cpp), and
# above the level-drop floor of 161,500 even after the full loss.
VICTIM_EXPERIENCE = 200000
HIT_EXPERIENCE_SLACK = 2000
BAG_VNUM = 1136  # world/obj/11.obj: a leather bag, open container
CAP_VNUM = 1137  # world/obj/11.obj: a leather cap, head armour
# Short descriptions, as `look in` lists a container's direct contents (act_info.cpp do_look).
BAG = "a leather bag"
CAP = "a leather cap"
IN_BAG = f"In object: {BAG}"  # act_wiz.cpp do_stat_object: the object's direct container


def test_player_poison_death_while_engaged_with_the_brute_is_harsh_and_keeps_the_pk_record(server, imp, mage, victim, harness) -> None:
    imp.command(f"goto {fixtures.ROOM_ARENA_CENTRE}")
    imp.command("transfer harnmage")
    imp.command("transfer harnvictim")
    imp.command(f"wizset harnvictim level {VICTIM_LEVEL}")
    imp.command(f"wizset harnvictim exp {VICTIM_EXPERIENCE}")
    imp.command("wizset harnvictim maxhit 400")
    imp.command("restore harnvictim")
    before_stat = imp.command("stat harnvictim")
    before = before_stat.abilities()
    assert before is not None
    before_exp = before_stat.experience()
    assert before_exp == VICTIM_EXPERIENCE, before_stat.text

    for vnum in (BAG_VNUM, CAP_VNUM):
        imp.command(f"load obj {vnum}")
    imp.command("give bag harnvictim")
    imp.command("give cap harnvictim")
    victim.command("put cap bag")
    in_bag = victim.command("look in bag")
    assert CAP in in_bag.text, f"the victim should carry the cap inside the bag before dying: {in_bag.text}"

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

    after_exp = stat.experience()
    assert after_exp is not None, stat.text
    full_loss = death_loss(before_exp, VICTIM_LEVEL, full=True)
    assert full_loss - HIT_EXPERIENCE_SLACK <= before_exp - after_exp <= full_loss, (
        f"a poison death while engaged with a real mob takes the full mob-death loss ({full_loss} from "
        f"{before_exp}), at most {HIT_EXPERIENCE_SLACK} less for hit experience and never more: "
        f"expected a loss from {full_loss - HIT_EXPERIENCE_SLACK} to {full_loss}, got {before_exp} -> {after_exp}"
    )

    in_corpse = imp.command("look in corpse")
    assert BAG in in_corpse.text and CAP not in in_corpse.text, (
        f"the mob-death corpse holds the bag and leaves the cap inside it: {in_corpse.text}"
    )
    cap_stat = imp.command("stat object cap")
    assert IN_BAG in cap_stat.text, f"the cap should still be inside the bag: {cap_stat.text}"

    victim_id = server.spec("Harnvictim").idnum
    mage_id = server.spec("Harnmage").idnum
    pkills = records.read_pkills(server.lib_dir)
    assert any(
        pkill.killer_id == mage_id and pkill.victim_id == victim_id and pkill.victim_level == VICTIM_LEVEL for pkill in pkills
    ), f"pkill_create() should write a PK row for the remote poisoner ({mage_id} killed {victim_id} at level {VICTIM_LEVEL}): {pkills}"

    quit_once_anger_allows(mage, harness)  # the poison cast angered the caster
