"""Markers and loops shared by the poison scenarios (spec B1 determinism contract)."""

from __future__ import annotations

import re
import time
from dataclasses import dataclass

import pytest

from rots_harness import fixtures
from rots_harness.session import GameSession, Transcript

POISON_LANDED = ("You feel very sick.",)  # spell_poison, mystic.cpp
POISON_RESISTED = ("You feel your body fend off the poison.",)
POISON_EXTENDED = ("You feel sicker as the poison lingers in your blood.",)  # send_poison_outcome_messages, poison.cpp: an equal poison extended the running one
POISON_BLOCKED = ("Your body is already fighting a stronger poison.",)  # send_poison_outcome_messages, poison.cpp: a weaker poison was refused
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
# modifier is non-zero. %-21s pads the name to at least 21 characters; no skills[] name (consts.cpp)
# is longer, so the column is 21 wide. The server ends lines with "\n\r", so each line after the
# first starts with a carriage return.
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
# wil / 5 is 5, a multiple of 5, so get_mystic_caster_level() (mystic.cpp) adds no random extra level.
DETERMINISTIC_WILLPOWER = 25
VICTIM_WILLPOWER = 0  # lowers saves_poison()'s defense below the caster's lowest offence roll
WOOD_ELF_POISON_BONUS = 30  # saves_poison(), poison.cpp
MAGE_MYSTIC_LEVEL = next(spec for spec in fixtures.STANDARD_ROSTER if spec.name == "Harnmage").professions["mystic"]
# A poison affect's own real-time tick (affect_update_person(), limits.cpp) repeats every 60 s on
# the time phase recorded when it landed, so after the landing pulse the next comes 60 s minus at
# most one fast-update period (3 s) later; 55 s leaves a margin for the landing line's arrival.
SLOW_TICK_FREE_WINDOW = 55.0


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


def hit_points(stat_text: str) -> int:
    """The current hit points of a `stat` reply, failing by name when it carries none."""
    reading = Transcript(stat_text).hit_points()
    assert reading is not None, f"stat reply has no hit points: {stat_text}"
    return reading[0]


def read_stat_with_willpower(imp: GameSession, name: str) -> Transcript:
    """A `stat` reply that carries both the ability line and the perception/willpower line."""
    # combat_support imports this module, so it is imported here rather than at the top.
    from combat_support import stat_replies

    replies = stat_replies(
        imp, name, lambda text: Transcript(text).abilities() is not None and Transcript(text).perception_and_willpower() is not None
    )
    reading = Transcript(replies[-1])
    assert reading.abilities() is not None and reading.perception_and_willpower() is not None, (
        f"stat {name} never printed its ability and willpower lines: {replies}"
    )
    return reading


def make_every_cast_land_alike(imp: GameSession) -> int:
    """Sets both willpowers so every poison Harnmage casts on Harnvictim lands and lasts the same
    number of ticks, asserts both from `stat`, and returns that duration:
    poison_victim_affect_at_level() (poison.cpp) gives level + 1.

    wizset's "will" sets the base value and calls affect_total(); do_restore (act_wiz.cpp) then
    copies the base to the current one, but only after its own affect_total(). The second wizset
    runs affect_total() again, so affect_naked() (handler.cpp) derives the Willpower that
    saves_poison() reads from the new current value."""
    for name, willpower in (("harnmage", DETERMINISTIC_WILLPOWER), ("harnvictim", VICTIM_WILLPOWER)):
        imp.command(f"wizset {name} will {willpower}")
        imp.command(f"restore {name}")
        imp.command(f"wizset {name} will {willpower}")

    mage_stat = read_stat_with_willpower(imp, "harnmage")
    will_factor = mage_stat.abilities()["wil"] // 5
    assert will_factor % 5 == 0, (
        f"precondition: Harnmage's willpower {mage_stat.abilities()['wil']} must give a will factor with "
        f"no random extra level ((wil / 5) % 5 == 0)"
    )

    mage_perception, mage_willpower = mage_stat.perception_and_willpower()
    victim_stat = read_stat_with_willpower(imp, "harnvictim")
    _victim_perception, victim_willpower = victim_stat.perception_and_willpower()
    offence = (mage_willpower * 8 * mage_perception) // 100
    defense = victim_stat.abilities()["con"] * 5 + victim_willpower * 3 + WOOD_ELF_POISON_BONUS
    assert offence // 3 >= defense, (
        f"precondition: the caster's lowest offence roll {offence // 3} must reach the victim's highest "
        f"defense roll {defense} (willpower {mage_willpower}, perception {mage_perception} against "
        f"CON {victim_stat.abilities()['con']}, willpower {victim_willpower})"
    )
    return MAGE_MYSTIC_LEVEL + will_factor + 1


def assert_no_slow_tick_yet(landed_at: float) -> None:
    """Fails by name once SLOW_TICK_FREE_WINDOW has passed since the monotonic `landed_at`, when
    the poison's own real-time tick may have changed the durations a test compares."""
    elapsed = time.monotonic() - landed_at
    if elapsed >= SLOW_TICK_FREE_WINDOW:
        pytest.fail(
            f"the durations were read {elapsed:.1f}s after the first poison landed, past the "
            f"{SLOW_TICK_FREE_WINDOW}s before its own real-time tick can fire, so they cannot be compared exactly"
        )


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
