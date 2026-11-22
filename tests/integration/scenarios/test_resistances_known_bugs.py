"""Resistance bugs: a strength-0 resist item left behind by a level change, pinned as a strict
xfail of the correct behaviour, and a weak protection lowering a flag-only resistance, fixed
for issue #452 and kept here as its regression test."""

from __future__ import annotations

import pytest

from rots_harness import world
from rots_harness.casting import NO_ENERGY_REPLY, cast_until_it_lands
from rots_harness.characters import IMPLEMENTOR, Character, StoredValues
from rots_harness.game_text import STOP_USING_FORMAT
from rots_harness.immortal_commands import give_new_object, goto, wizset
from rots_harness.session import GameSession
from test_resistances import (
    ALREADY_PROTECTED_MARKER,
    FLAG_RESIST_ITEM_KEYWORD,
    FLAG_RESIST_ITEM_VNUM,
    FLAG_RESIST_ITEM_WORN_MARKER,
    FLAG_RESIST_PERCENT,
    NOT_AFFECTED_MARKER,
    RESIST_LEVEL_BONUS,
    RESIST_LEVEL_CAP,
    WEARER,
    affections,
    cleric_level,
    resistance_line,
)

pytestmark = [pytest.mark.scenario, pytest.mark.resistances, pytest.mark.resistances_worn]

# The spirit the novice is stored with: protection costs 5 (skills[] in consts.cpp) and a lost
# concentration 2, so this pays for the cast and leaves room for losses.
NOVICE_SPIRIT = 40
# A level-10 human mystic in the arena centre who knows protection: ten practice sessions put
# protection's knowledge at the cap of 100 (learn_diff 1, recalc_skills in spec_pro.cpp), and its
# protection is weaker than the flat resistance of a flag-only item.
NOVICE = Character(
    name="Testnovice",
    race="human",
    sex="female",
    level=10,
    load_room=world.ROOM_ARENA_CENTRE,
    class_name="mystic",
    skills=(("protection", 10),),
    stored=StoredValues(spirit=NOVICE_SPIRIT),
)

# A wrist item whose `A 27 161` has a strength of 0, which affect_modify's APPLY_SPELL decode
# (handler.cpp) replaces with the wearer's level.
LEVEL_ITEM_VNUM = 1404
LEVEL_ITEM_KEYWORD = "bracelet"
LEVEL_ITEM_SHORT = "a kindling bracelet"
LEVEL_ITEM_WORN_MARKER = "You put a kindling bracelet on around your right wrist."
# The level the implementor gives the wearer after it puts on the level item.
RAISED_LEVEL = 25

# What the protected character, or the wearer of a resist item, reads (spell_protection and
# do_resist_spell, mystic.cpp).
FIRE_RESISTANT_MARKER = "You feel resistant to fire!"


def hand_item_to(implementor: GameSession, recipient: Character, vnum: int, keyword: str) -> None:
    """Moves the implementor to the recipient in the arena centre and gives it a new item."""
    goto(implementor, recipient.name.lower(), world.ROOM_ARENA_CENTRE_NAME)
    give_new_object(implementor, vnum, keyword, recipient.name.lower())


def cast_protection_fire(caster: GameSession) -> str:
    """Casts protection from fire on the caster; returns what the caster read after.

    A lost concentration is the one roll that can stop the cast, so the cast is repeated after
    it. Each loss costs spirit, and nothing regenerates spirit between requests under
    affect-hours, so the repeats end at NO_ENERGY_REPLY at the latest.
    """
    outcomes = [FIRE_RESISTANT_MARKER, ALREADY_PROTECTED_MARKER, NO_ENERGY_REPLY]
    return cast_until_it_lands(caster, "cast 'protection' fire", outcomes)


@pytest.mark.xfail(
    strict=True,
    raises=AssertionError,
    reason="removing a strength-0 resist item must end its resistance even after the wearer's level "
    "changed; today the resistance stays for good",
)
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, WEARER)
def test_removing_a_level_strength_item_after_a_level_change_leaves_no_resistance(server) -> None:
    """Bug: a strength-0 resist item outlives its removal after a level change (PR #315 code).

    The item's strength falls back to the wearer's level when it goes on (affect_modify,
    handler.cpp:461-464): 20 here. Removal in do_resist_spell (mystic.cpp:1825-1827) compares
    the installed strength with the level at removal time, so after `wizset ... level 25` (which
    sets only the overall level, act_wiz.cpp:2988) it returns early and leaves a permanent
    resistance with no item behind it. Wearing and removing draw nothing.
    """
    implementor = server.login(IMPLEMENTOR.name)
    wearer = server.login(WEARER.name)
    hand_item_to(implementor, WEARER, LEVEL_ITEM_VNUM, LEVEL_ITEM_KEYWORD)
    worn = wearer.command(f"wear {LEVEL_ITEM_KEYWORD}")
    assert worn.contains(LEVEL_ITEM_WORN_MARKER) and worn.contains(FIRE_RESISTANT_MARKER), worn.text
    assert resistance_line("fire", WEARER.level) in affections(wearer)

    wizset(implementor, f"player {WEARER.name.lower()}", "level", RAISED_LEVEL, shown_name=WEARER.name)

    removed = wearer.command(f"remove {LEVEL_ITEM_KEYWORD}")
    assert removed.contains(STOP_USING_FORMAT.format(LEVEL_ITEM_SHORT)), removed.text
    shown = affections(wearer)
    assert NOT_AFFECTED_MARKER in shown, shown


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, NOVICE)
def test_a_weak_protection_does_not_lower_a_flag_only_fire_resistance(server) -> None:
    """A weak protection does not lower a flag-only fire resistance (issue #452).

    effective_resist_magnitude (fight.cpp) weighs the ring's flag at the flat 33% against the
    largest affect strength, and the higher wins; damage() and sprintbit_resistances both read
    it. Before the fix, any strength replaced the flag, so the novice's 20% protection showed
    and applied as 20%. The one draw that can stop the cast is the concentration roll in do_cast,
    which cast_protection_fire repeats.
    """
    implementor = server.login(IMPLEMENTOR.name)
    novice = server.login(NOVICE.name)
    hand_item_to(implementor, NOVICE, FLAG_RESIST_ITEM_VNUM, FLAG_RESIST_ITEM_KEYWORD)
    assert novice.command(f"wear {FLAG_RESIST_ITEM_KEYWORD}").contains(FLAG_RESIST_ITEM_WORN_MARKER)
    assert resistance_line("fire", FLAG_RESIST_PERCENT) in affections(novice)

    # Testnovice (level-10 mystic):
    #   effect_modifier = cast_resist_magnitude(get_prof_level(PROF_CLERIC, caster))  (mystic.cpp)
    #                   = (10 > 30 ? 30 : 10) + 10        (Class levels: Cle:10)
    #                   = 20, below the flat 33%
    level = cleric_level(implementor, NOVICE)
    assert level == NOVICE.level, level
    assert min(level, RESIST_LEVEL_CAP) + RESIST_LEVEL_BONUS == 20

    reply = cast_protection_fire(novice)
    assert FIRE_RESISTANT_MARKER in reply, reply

    shown = affections(novice)
    assert resistance_line("fire", FLAG_RESIST_PERCENT) in shown, shown
