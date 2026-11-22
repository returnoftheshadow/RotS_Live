"""Resistance bugs: a strength-0 resist item a level change leaves behind, and a weak protection.

The first is pinned as a strict xfail of the correct behaviour. The second, a weak protection that
leaves a flag-only resistance as it is, is the fix pin for #452.
"""

from __future__ import annotations

import pytest

from rots_harness import world
from rots_harness.casting import NO_ENERGY_REPLY, cast_until_it_lands
from rots_harness.characters import IMPLEMENTOR, Character, StoredValues
from rots_harness.game_text import STOP_USING_FORMAT
from rots_harness.immortal_commands import give_new_object, goto, wizset
from rots_harness.session import GameSession
from test_resistances import (
    ALREADY_PROTECTED_REPLY,
    FIRE_RESISTANT,
    FLAG_RESIST_ITEM_KEYWORD,
    FLAG_RESIST_ITEM_VNUM,
    FLAG_RESIST_ITEM_WORN_REPLY,
    FLAG_RESIST_PERCENT,
    NOT_AFFECTED_REPLY,
    WEARER,
    affections,
    cleric_level,
    resist_percent,
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
# The keyword the level item answers to.
LEVEL_ITEM_KEYWORD = "bracelet"
# The level item's short description.
LEVEL_ITEM_SHORT_DESCRIPTION = "a kindling bracelet"
# What wearing the level item prints (wear_message, act_obj2.cpp).
LEVEL_ITEM_WORN_REPLY = "You put a kindling bracelet on around your right wrist."
# The level the implementor gives the wearer after it puts on the level item.
RAISED_LEVEL = 25


def hand_item_to(implementor: GameSession, recipient: Character, vnum: int, keyword: str) -> None:
    """Move the implementor to the recipient in the arena centre and give it a new item."""
    goto(implementor, recipient.name.lower(), world.ROOM_ARENA_CENTRE_NAME)
    give_new_object(implementor, vnum, keyword, recipient.name.lower())


def cast_protection_fire(caster: GameSession) -> str:
    """Cast protection from fire on the caster and return what the caster read after.

    Repeats the cast past a lost concentration (cast_until_it_lands).
    """
    outcomes = [FIRE_RESISTANT, ALREADY_PROTECTED_REPLY, NO_ENERGY_REPLY]
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
    """Bug: a strength-0 resist item outlives its removal after a level change.

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
    assert worn.contains(LEVEL_ITEM_WORN_REPLY) and worn.contains(FIRE_RESISTANT), worn.text
    shown = affections(wearer)
    assert resistance_line("fire", WEARER.level) in shown, shown

    wizset(implementor, f"player {WEARER.name.lower()}", "level", RAISED_LEVEL, shown_name=WEARER.name)

    removed = wearer.command(f"remove {LEVEL_ITEM_KEYWORD}")
    assert removed.contains(STOP_USING_FORMAT.format(LEVEL_ITEM_SHORT_DESCRIPTION)), removed.text
    shown = affections(wearer)
    assert NOT_AFFECTED_REPLY in shown, shown


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, NOVICE)
def test_a_weak_protection_does_not_lower_a_flag_only_fire_resistance(server) -> None:
    """Fix pin (#452): a weak protection does not lower a flag-only fire resistance.

    effective_resist_magnitude (fight.cpp) weighs the ring's flag at the flat 33% against the
    largest affect strength, and damage() and sprintbit_resistances both read it. #452 made the
    higher of the two win; before it, any strength replaced the flag, so the novice's 20% protection
    showed and applied as 20%. The one draw that can stop the cast is the concentration roll in
    do_cast, which cast_protection_fire repeats.
    """
    implementor = server.login(IMPLEMENTOR.name)
    novice = server.login(NOVICE.name)
    hand_item_to(implementor, NOVICE, FLAG_RESIST_ITEM_VNUM, FLAG_RESIST_ITEM_KEYWORD)
    worn = novice.command(f"wear {FLAG_RESIST_ITEM_KEYWORD}")
    assert worn.contains(FLAG_RESIST_ITEM_WORN_REPLY), worn.text
    shown = affections(novice)
    assert resistance_line("fire", FLAG_RESIST_PERCENT) in shown, shown

    # Testnovice (level-10 mystic):
    #   effect_modifier = cast_resist_magnitude(get_prof_level(PROF_CLERIC, caster))  (mystic.cpp)
    #                   = (10 > 30 ? 30 : 10) + 10        (Class levels: Cle:10)
    #                   = 20, below the flat 33%
    level = cleric_level(implementor, NOVICE)
    assert level == NOVICE.level, level
    expected_percent = resist_percent(level)
    assert expected_percent == 20, expected_percent

    reply = cast_protection_fire(novice)
    assert FIRE_RESISTANT in reply, reply

    shown = affections(novice)
    assert resistance_line("fire", FLAG_RESIST_PERCENT) in shown, shown
