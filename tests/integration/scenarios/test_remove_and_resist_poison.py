"""Remove poison ends a spell poison but not a worn item's poison, and resist poison matches a
running spell poison and shortens it every tick; neither spell sees a worn item's poison.

Mechanism, all in mystic.cpp. spell_remove_poison() removes every SPELL_POISON affect from a
poisoned victim through affect_from_char(), then tells the victim and the room; with no poison
affect it does nothing. affect_remove() (handler.cpp) re-applies worn items, so an item's
AFF_POISON survives the cure. spell_resist_poison() adds a SPELL_RESIST_POISON affect with the
poison's remaining duration and the caster's mystic level as modifier, refuses a second one, and
answers "not poisoned" when there is no SPELL_POISON affect. affect_update_person() (limits.cpp)
then takes the modifier off the poison on top of the normal decrement, never below 0, and copies the
result to the resist affect.

A gtest cannot drive the cast path, the messages to three observers, and the forced affect tick
together. Durations are read from `stat` just before and after one forced tick. A poison affect's
own slow tick fires about once a minute of wall clock after it lands (utility.cpp
get_current_time_phase()); every test reads its before and after durations well inside that.
"""

from __future__ import annotations

import pytest

import poison_support
from combat_support import VICTIM_LEVEL, move_out_of_the_fight, quit_once_anger_allows, read_affect_listing
from poison_support import (
    CAST_COMPLETED,
    REMOVE_POISON_CURED,
    REMOVE_POISON_ROOM,
    RESIST_POISON_ALREADY,
    RESIST_POISON_CASTER,
    RESIST_POISON_SELF_UNPOISONED,
    RESIST_POISON_STARTED,
    RESIST_POISON_TARGET_UNPOISONED,
    affect_flags,
    poison_until_it_lands,
    spell_affects,
    wear_the_sickly_amulet,
)
from rots_harness import fixtures
from rots_harness.session import GameSession, Transcript

pytestmark = pytest.mark.scenario

POISON_TICK_DAMAGE = 5  # affect_update_person()'s SPELL_POISON arm, limits.cpp
MAGE_MYSTIC_LEVEL = next(spec for spec in fixtures.STANDARD_ROSTER if spec.name == "Harnmage").professions["mystic"]


def _gather_in_arena_centre(imp: GameSession, mage: GameSession, victim: GameSession) -> None:
    imp.command(f"goto {fixtures.ROOM_ARENA_CENTRE}")
    imp.command("transfer harnmage")
    imp.command("transfer harnvictim")
    mage.expect_room("Arena Centre")
    victim.expect_room("Arena Centre")
    imp.command(f"wizset harnvictim level {VICTIM_LEVEL}")  # Big Brother: caster 30 < 3 * 11


def _poison_and_end_the_fight(imp: GameSession, mage: GameSession, victim: GameSession) -> None:
    """Lands a mystic poison on Harnvictim, ends the fight the cast started, and brings the mage
    and the imp back to Arena Centre, so the next cast is not made from melee."""
    poison_until_it_lands(mage, victim, "elf")
    move_out_of_the_fight(imp, mage, "harnmage", "harnvictim")
    imp.command(f"goto {fixtures.ROOM_ARENA_CENTRE}")
    imp.command("transfer harnmage")
    mage.expect_room("Arena Centre")


def _hit_points(stat_text: str) -> int:
    reading = Transcript(stat_text).hit_points()
    assert reading is not None, f"stat reply has no hit points: {stat_text}"
    return reading[0]


def _cast_remove_poison_on_the_victim(mage: GameSession) -> None:
    """The caster's own completion line; spell_remove_poison() says nothing to the caster."""
    mage.cast("remove poison", "elf", success_markers=(CAST_COMPLETED,))


def test_remove_poison_cures_a_spell_poison(server, imp, mage, victim, harness) -> None:
    _gather_in_arena_centre(imp, mage, victim)
    _poison_and_end_the_fight(imp, mage, victim)

    before_tick = read_affect_listing(imp, "harnvictim")
    assert spell_affects(before_tick, "poison"), f"precondition: the poison is running: {before_tick}"
    harness.affects()
    after_tick = read_affect_listing(imp, "harnvictim")
    assert _hit_points(after_tick) <= _hit_points(before_tick) - POISON_TICK_DAMAGE + poison_support.REGEN_ALLOWANCE, (
        f"control: a tick of the running poison takes hit points: {_hit_points(before_tick)} -> {_hit_points(after_tick)}"
    )

    imp_mark = len(imp.everything)
    _cast_remove_poison_on_the_victim(mage)
    victim.expect((REMOVE_POISON_CURED,))
    imp.command("look")
    assert REMOVE_POISON_ROOM in imp.everything[imp_mark:], "the room must see the victim look better"

    cured = read_affect_listing(imp, "harnvictim")
    assert not spell_affects(cured, "poison"), f"remove poison must end the poison affect: {cured}"
    assert "POISON" not in affect_flags(cured), f"remove poison must clear the POISON flag: {cured}"

    harness.affects()
    after_cure_tick = read_affect_listing(imp, "harnvictim")
    assert _hit_points(after_cure_tick) >= _hit_points(cured), (
        f"a tick after the cure must take no hit points: {_hit_points(cured)} -> {_hit_points(after_cure_tick)}"
    )

    quit_once_anger_allows(mage, harness)  # the poison cast angered the caster
    quit_once_anger_allows(victim, harness)  # fighting back before the fight ended angered the victim


def test_remove_poison_cannot_cure_gear_poison(server, imp, mage, victim, harness) -> None:
    _gather_in_arena_centre(imp, mage, victim)
    wear_the_sickly_amulet(imp, victim, "harnvictim")
    _poison_and_end_the_fight(imp, mage, victim)
    poisoned = read_affect_listing(imp, "harnvictim")
    assert spell_affects(poisoned, "poison"), f"precondition: the spell poison is running: {poisoned}"

    _cast_remove_poison_on_the_victim(mage)
    victim.expect((REMOVE_POISON_CURED,))

    cured = read_affect_listing(imp, "harnvictim")
    assert not spell_affects(cured, "poison"), f"remove poison must end the spell poison: {cured}"
    assert "POISON" in affect_flags(cured), f"the worn amulet's POISON flag must survive remove poison: {cured}"

    quit_once_anger_allows(mage, harness)  # the poison cast angered the caster
    quit_once_anger_allows(victim, harness)  # fighting back before the fight ended angered the victim


def test_remove_poison_on_an_unpoisoned_target_does_nothing(server, imp, mage, victim) -> None:
    _gather_in_arena_centre(imp, mage, victim)
    before = read_affect_listing(imp, "harnvictim")
    assert not spell_affects(before, "poison"), f"precondition: the victim is not poisoned: {before}"

    marks = {session: len(session.everything) for session in (mage, victim, imp)}
    _cast_remove_poison_on_the_victim(mage)
    # The spell body runs in the pulse that sent "Ok.", so any line it sent precedes each reply below.
    for session in (mage, victim, imp):
        session.command("look")
    heard = {session.character.name: session.everything[marks[session]:] for session in (mage, victim, imp)}

    for name, text in heard.items():
        assert REMOVE_POISON_CURED not in text, f"{name} must not hear a cure for an unpoisoned victim: {text}"
        assert REMOVE_POISON_ROOM not in text, f"{name} must not see an unpoisoned victim look better: {text}"
    after = read_affect_listing(imp, "harnvictim")
    assert affect_flags(after) == affect_flags(before), f"the flags must not change: {before} -> {after}"
    spell_lines_before = poison_support.SPELL_AFFECT_LINE.findall(before)
    spell_lines_after = poison_support.SPELL_AFFECT_LINE.findall(after)
    assert spell_lines_after == spell_lines_before, f"the affect listing must not change: {before} -> {after}"


def test_resist_poison_matches_the_poison_and_refuses_a_second_cast(server, imp, mage, victim, harness) -> None:
    _gather_in_arena_centre(imp, mage, victim)
    _poison_and_end_the_fight(imp, mage, victim)

    mage.cast("resist poison", "elf", success_markers=(RESIST_POISON_CASTER,))
    victim.expect((RESIST_POISON_STARTED,))
    resisting = read_affect_listing(imp, "harnvictim")
    poisons = spell_affects(resisting, "poison")
    resists = spell_affects(resisting, "resist poison")
    assert len(poisons) == 1, f"precondition: one poison affect: {resisting}"
    assert len(resists) == 1, f"resist poison must add one resist affect: {resisting}"
    assert resists[0].duration == poisons[0].duration, f"the resist affect must last as long as the poison: {resisting}"
    assert resists[0].modifier == MAGE_MYSTIC_LEVEL, f"the resist modifier must be the caster's mystic level {MAGE_MYSTIC_LEVEL}: {resisting}"

    mage.cast("resist poison", "elf", success_markers=(RESIST_POISON_ALREADY,))
    again = read_affect_listing(imp, "harnvictim")
    assert len(spell_affects(again, "resist poison")) == 1, f"a second cast must not add a second resist affect: {again}"

    quit_once_anger_allows(mage, harness)  # the poison cast angered the caster
    quit_once_anger_allows(victim, harness)  # fighting back before the fight ended angered the victim


def test_resist_poison_shortens_the_poison_each_tick(server, imp, mage, victim, harness) -> None:
    _gather_in_arena_centre(imp, mage, victim)
    _poison_and_end_the_fight(imp, mage, victim)
    mage.cast("resist poison", "elf", success_markers=(RESIST_POISON_CASTER,))

    before = read_affect_listing(imp, "harnvictim")
    poisons = spell_affects(before, "poison")
    resists = spell_affects(before, "resist poison")
    assert len(poisons) == 1 and len(resists) == 1, f"precondition: one poison and one resist affect: {before}"
    assert resists[0].modifier == MAGE_MYSTIC_LEVEL, f"precondition: the resist modifier is the mystic level: {before}"

    harness.affects()
    after = read_affect_listing(imp, "harnvictim")
    expected = max(poisons[0].duration - 1 - resists[0].modifier, 0)
    poisons_after = spell_affects(after, "poison")
    resists_after = spell_affects(after, "resist poison")
    assert len(poisons_after) == 1 and len(resists_after) == 1, f"both affects must still be listed after one tick: {after}"
    assert poisons_after[0].duration == expected, (
        f"one tick must take 1 + {resists[0].modifier} off the poison's {poisons[0].duration}, not below 0: "
        f"expected {expected}: {after}"
    )
    assert resists_after[0].duration == poisons_after[0].duration, f"the resist affect must follow the poison: {after}"

    quit_once_anger_allows(mage, harness)  # the poison cast angered the caster
    quit_once_anger_allows(victim, harness)  # fighting back before the fight ended angered the victim


def test_resist_poison_on_an_unpoisoned_target_says_so(server, imp, mage, victim) -> None:
    _gather_in_arena_centre(imp, mage, victim)

    mage.cast("resist poison", "elf", success_markers=(RESIST_POISON_TARGET_UNPOISONED,))
    mage.cast("resist poison", None, success_markers=(RESIST_POISON_SELF_UNPOISONED,))

    target = read_affect_listing(imp, "harnvictim")
    caster = read_affect_listing(imp, "harnmage")
    assert not spell_affects(target, "resist poison"), f"no resist affect on an unpoisoned target: {target}"
    assert not spell_affects(caster, "resist poison"), f"no resist affect on an unpoisoned caster: {caster}"


def test_resist_poison_does_not_see_gear_poison(server, imp, mage, victim) -> None:
    _gather_in_arena_centre(imp, mage, victim)
    wear_the_sickly_amulet(imp, victim, "harnvictim")
    worn = read_affect_listing(imp, "harnvictim")
    assert "POISON" in affect_flags(worn), f"precondition: the amulet sets the POISON flag: {worn}"

    mage.cast("resist poison", "elf", success_markers=(RESIST_POISON_TARGET_UNPOISONED,))

    after = read_affect_listing(imp, "harnvictim")
    assert not spell_affects(after, "resist poison"), f"gear poison alone must not start a resistance: {after}"
