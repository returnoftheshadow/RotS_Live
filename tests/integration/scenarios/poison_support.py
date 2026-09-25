"""Markers and loops shared by the poison scenarios (spec B1 determinism contract)."""

from __future__ import annotations

import re
from dataclasses import dataclass

import pytest

from rots_harness.session import GameSession

POISON_LANDED = ("You feel very sick.",)  # spell_poison, mystic.cpp
POISON_RESISTED = ("You feel your body fend off the poison.",)
DEATH_MARKER = "You are dead!  Sorry..."  # fight.cpp damage()
CAST_COMPLETED = "Ok."  # spell_pa.cpp do_cast: sent to the caster just before the spell body runs
REMOVE_POISON_CURED = "A warm feeling runs through your body."  # spell_remove_poison, mystic.cpp: to the victim
REMOVE_POISON_ROOM = "looks better."  # spell_remove_poison, mystic.cpp: "$N looks better." to the room but the caster
RESIST_POISON_STARTED = "You begin to resist the poison."  # spell_resist_poison, mystic.cpp: to the victim
RESIST_POISON_CASTER = "They begin to resist the poison."  # spell_resist_poison, mystic.cpp: to a caster who is not the victim
RESIST_POISON_ALREADY = "The poison is already being resisted."  # spell_resist_poison, mystic.cpp: to the caster
RESIST_POISON_TARGET_UNPOISONED = "But they are not poisoned!"  # spell_resist_poison, mystic.cpp: to the caster
RESIST_POISON_SELF_UNPOISONED = "But you have not been poisoned!"  # spell_resist_poison, mystic.cpp: caster is the target
# tests/integration/world/obj/11.obj #1138: a neck item whose APPLY_BITVECTOR 11 line sets AFF_POISON while worn.
SICKLY_AMULET_VNUM = 1138
AMULET_WORN = "You wear a sickly amulet around your neck."  # act_obj2.cpp wear messages, neck slot
AMULET_REMOVED = "You stop using a sickly amulet."  # act_obj2.cpp perform_remove
AMULET_GIVEN = "You give a sickly amulet to"  # act_obj1.cpp perform_give, the giver's line
# act_wiz.cpp do_stat_character: "SPL: (%3dhr) %-21s" with duration + 1, then "%+d to <apply>" when the
# modifier is non-zero. The name column is exactly 21 characters wide for every skills[] name, and
# the server ends lines with "\n\r", so each line after the first starts with a carriage return.
SPELL_AFFECT_LINE = re.compile(r"^\r?SPL: \(\s*(-?\d+)hr\) (.{21})(.*)$", re.MULTILINE)
SPELL_AFFECT_MODIFIER = re.compile(r"^\s*([+-]\d+) to ")
# Where a dead or slain Harnmage wakes: raw_kill() sends a mortal to r_mortal_start_room[race], and
# the Uruk-Lhuth start room (13626, consts.cpp) is not in the harness world, so check_start_rooms()
# (db.cpp) falls back to real room 0, the world's first room (#1101).
MAGE_RESPAWN_ROOM = "Immortal Start"
ROSTER_CON = 11  # tests/integration/fixtures/character.template.json abilities.con
# comm.cpp's real-time fast block (PULSE_FAST_UPDATE, every 3s) keeps regenerating hit points
# independently of the forced affect ticks (spec B1 is monotonic, not tick-exact), so a post-death
# hit-point assertion taken some wall-clock time after the death tick allows a small margin above
# the pinned revival value instead of an exact match.
REGEN_ALLOWANCE = 3


def death_tick_budget(hit: int, con: int = ROSTER_CON) -> int:
    """Forced ticks needed for 5-damage poison to reach hit <= -con/2 (fight.cpp update_pos), plus two spare."""
    return -(-(hit + con // 2) // 5) + 2


def xp_to_level(level: int) -> int:
    """Experience at which `level` begins (limits.cpp xp_to_level)."""
    return level * level * 1500


def death_loss(exp: int, level: int, full: bool) -> int:
    """Experience a player death takes (fight.cpp die()): a tenth of `base` always, plus all of
    `base` when death_takes_full_mob_xp_loss() holds. int(x / y) mirrors C++ truncation toward zero.
    """
    base = int(-(exp - 3000) / (level + 2))
    loss = min(0, int(base / 10))
    if full:
        loss += min(0, base)
    return -loss


def poison_until_it_lands(caster: GameSession, victim: GameSession, target_word: str, attempts: int = 8) -> None:
    for _attempt in range(attempts):
        victim.command("look")  # clears any AFK flag so Big Brother does not shield the victim
        caster.send_line(f"cast 'poison' {target_word}")
        try:
            text = victim.expect(POISON_LANDED + POISON_RESISTED, timeout=12.0)
        except AssertionError as timeout:
            pytest.fail(f"{timeout}\ncaster side:\n{caster.drain(0.5)[-1500:]}")
        if POISON_LANDED[0] in text:
            return
        caster.drain(0.5)
    pytest.fail(f"poison never landed in {attempts} casts")


def affect_ticks_until_death(harness, victim: GameSession, budget: int) -> bool:
    for _tick in range(budget):
        harness.affects()
        if DEATH_MARKER in victim.drain(0.5):
            return True
    return False


@dataclass(frozen=True)
class SpellAffect:
    """One `SPL:` line of a `stat` reply."""

    name: str
    duration: int  # the affect's remaining duration; stat prints one more than this
    modifier: int | None  # None when stat prints no modifier (it is 0)


def spell_affects(stat_text: str, name: str) -> list[SpellAffect]:
    """Every `SPL:` line of `stat_text` whose skills[] name is exactly `name`."""
    found = []
    for match in SPELL_AFFECT_LINE.finditer(stat_text):
        if match.group(2).strip() != name:
            continue
        modifier = SPELL_AFFECT_MODIFIER.match(match.group(3))
        found.append(SpellAffect(name, int(match.group(1)) - 1, int(modifier.group(1)) if modifier else None))
    return found


def affect_flags(stat_text: str) -> set[str]:
    """The names on do_stat_character's `AFF:` line (act_wiz.cpp), which sprintbit (utility.cpp)
    writes space-separated with a closing full stop."""
    for line in stat_text.splitlines():
        if line.startswith("AFF:"):
            return set(line[len("AFF:"):].strip().rstrip(".").split())
    pytest.fail(f"stat reply has no AFF: line: {stat_text}")


def wear_the_sickly_amulet(imp: GameSession, wearer: GameSession, wearer_name: str) -> None:
    """The imp loads the poison-bit amulet and hands it to `wearer`, who stands in its room and
    wears it. Each step waits on its own reply, so the next is never sent before it has run.
    """
    imp.command(f"load obj {SICKLY_AMULET_VNUM}")
    imp.send_line(f"give amulet {wearer_name}")
    imp.expect((AMULET_GIVEN,))
    wearer.send_line("wear amulet")
    wearer.expect((AMULET_WORN,))
