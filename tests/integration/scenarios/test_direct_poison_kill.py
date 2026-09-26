"""A player's direct poison hit that lands the killing blow is that player's kill, even when the
victim was fighting a mob at the time: the victim takes the gentle arm and the caster earns the
PK record; no mob-death record names the brute. Only a poison TICK is classified by engagement.
"""

from __future__ import annotations

import pytest

import poison_support
from combat_support import BRUTE_ORC_VNUM, neutralize_melee, quit_once_anger_allows, wait_for_engagement
from rots_harness import fixtures, records

pytestmark = pytest.mark.scenario

# spell_poison (mystic.cpp) says nothing to the caster when the poison lands, so the victim's
# session is the one that tells a landed cast from a shrugged one.
CAST_OUTCOMES = poison_support.POISON_LANDED + poison_support.POISON_RESISTED
CAST_ATTEMPTS = 8


def test_direct_poison_killing_blow_is_the_casters_kill(server, imp, mage, victim, harness) -> None:
    imp.command(f"goto {fixtures.ROOM_ARENA_CENTRE}")
    imp.command("transfer harnmage")
    imp.command("transfer harnvictim")
    imp.command("wizset harnvictim level 11")  # BB needs attacker(30) < defender*3; 11 gives 33 > 30
    imp.command("restore harnmage")
    imp.command(f"load mob {BRUTE_ORC_VNUM}")
    neutralize_melee(imp, "brute")
    imp.command("wizset brute maxhit 4000")
    imp.command("wizset brute hit 4000")
    before = imp.command("stat harnvictim").abilities()
    assert before is not None

    victim.command("kill brute")
    wait_for_engagement(imp, "brute", "Harnvictim")

    for _attempt in range(CAST_ATTEMPTS):
        # Under the direct hit's 5 damage, so a landing cast kills; re-applied each attempt
        # because real-time regen could otherwise lift the victim out of one-hit range.
        imp.command("wizset harnvictim hit 3")
        mage.send_line("cast 'poison' elf")  # the mage sees Harnvictim only as "*an Elf*"
        text = victim.expect(CAST_OUTCOMES, timeout=12.0)
        if poison_support.POISON_LANDED[0] in text:
            break
        mage.drain(0.5)
    else:
        pytest.fail(f"the poison never landed in {CAST_ATTEMPTS} casts")

    # expect() consumes the whole reply, so the death line may already be in the landing text.
    if poison_support.DEATH_MARKER not in text:
        victim.expect([poison_support.DEATH_MARKER], 8.0)
    victim.expect_room("Wood-elf Start")
    stat = imp.command("stat harnvictim")
    current, maximum = stat.hit_points()
    assert maximum // 4 <= current <= maximum // 4 + poison_support.REGEN_ALLOWANCE, stat.text
    assert stat.abilities() == before

    victim_records = records.read_exploits(server.lib_dir, "Harnvictim")
    assert not any(record.type == records.EXPLOIT_MOBDEATH for record in victim_records), victim_records
    assert any(record.type == records.EXPLOIT_DEATH and record.victim_name.lower() == "harnmage" for record in victim_records), victim_records
    mage_records = records.read_exploits(server.lib_dir, "Harnmage")
    assert any(record.type == records.EXPLOIT_PK and record.victim_name.lower() == "harnvictim" for record in mage_records), mage_records

    quit_once_anger_allows(mage, harness)  # the poison cast angered the caster
