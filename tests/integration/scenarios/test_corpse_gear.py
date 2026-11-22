"""Corpse gear: a death to poison or to a player gathers the wearable gear in the dead one's bags
into the top of its corpse."""

from __future__ import annotations

import pytest

from rots_harness import world
from rots_harness.casting import LOST_CONCENTRATION_REPLY, NO_ENERGY_REPLY
from rots_harness.characters import IMPLEMENTOR, Character
from rots_harness.game_text import CORPSE_CONTENTS_HEADER, IS_DEAD_FORMAT
from rots_harness.immortal_commands import goto, load_mob, load_object
from rots_harness.replies import reply_to
from rots_harness.session import GameSession
from rots_harness.stat_reading import (
    ANGER_AFFECT_PATTERN,
    CONSTITUTION_PATTERN,
    HIT_POINTS_UNSIGNED_MAXIMUM_PATTERN,
    MAGE_LEVEL_PATTERN,
    SAVING_THROW_PATTERN,
    read_stat,
)
from rots_harness.text_lines import first_number, lines_after

pytestmark = [pytest.mark.scenario, pytest.mark.corpse_gear]

# The frail taster (mob 2205, tests/integration/world/mob/22.mob): one hit point, constitution 2,
# no script, no resistances.
TASTER_VNUM = 2205
# The damage each poison tick deals (affect_update, limits.cpp).
POISON_TICK_DAMAGE = 5

# A level-30 human mage in the Order Hall who knows magic missile fully: 100 practice sessions
# put its knowledge at the cap of 100 (recalc_skills, spec_pro.cpp).
SLINGER = Character(
    name="Testslinger",
    race="human",
    sex="female",
    level=30,
    load_room=world.ROOM_ORDER_HALL,
    class_name="mage",
    skills=(("magic_missile", 100),),
)

# What the room reads as the taster dies.
TASTER_DEAD_MARKER = IS_DEAD_FORMAT.format("A frail taster")
# What `look in bag` prints before a carried bag's contents (show_obj_contents, act_info.cpp).
BAG_CONTENTS_HEADER = "bag (carried) :"
# Seconds to wait for a cast's outcome: magic missile's casting delay at mage level 30 is a few
# seconds (CASTING_TIME, spells.h); 20 leaves room for a loaded host.
CAST_TIMEOUT = 20.0
# The game hours of anger a player's blow at a mob gives (on_attacked_character,
# char_utils_combat.cpp).
ANGER_HOURS = 2
# Seconds to keep reading after an outcome, for text already sent to arrive.
SETTLE_SECONDS = 1.0


def hand_the_taster_a_bagged_cap(implementor: GameSession) -> None:
    """Brings the implementor to the Order Hall, loads the taster and gives it a bag with a cap in it."""
    goto(implementor, world.ROOM_ORDER_HALL, world.ROOM_ORDER_HALL_NAME)
    load_mob(implementor, TASTER_VNUM, "a frail taster")
    load_object(implementor, world.OBJECT_LEATHER_BAG, world.OBJECT_LEATHER_BAG_SHORT_DESCRIPTION)
    load_object(implementor, world.OBJECT_LEATHER_CAP, world.OBJECT_LEATHER_CAP_SHORT_DESCRIPTION)
    assert implementor.command("put cap bag").contains("You put a leather cap in a leather bag."), implementor.everything
    given = implementor.command("give bag taster")
    assert given.contains("You give a leather bag to a frail taster."), given.text


def assert_the_corpse_holds_the_cap_outside_the_bag(implementor: GameSession) -> None:
    """Asserts the taster's corpse lists the bag and the cap, and the bag no longer holds the cap."""
    corpse = implementor.command("look in corpse").text
    corpse_contents = lines_after(corpse, CORPSE_CONTENTS_HEADER)
    assert world.OBJECT_LEATHER_BAG_SHORT_DESCRIPTION in corpse_contents, corpse
    assert world.OBJECT_LEATHER_CAP_SHORT_DESCRIPTION in corpse_contents, corpse
    taken = implementor.command("get bag corpse")
    assert taken.contains("You get a leather bag from"), taken.text
    bag = implementor.command("look in bag").text
    assert world.OBJECT_LEATHER_CAP_SHORT_DESCRIPTION not in bag, bag


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR)
def test_a_mob_that_dies_of_poison_has_its_bagged_cap_gathered_into_its_corpse(server) -> None:
    """Pins a death to poison gathering the wearable cap out of the bag into the corpse.

    The taster eats the tainted bread; on the first advanced hour affect_update (limits.cpp)
    deals poison's 5 damage with the taster as its own killer. A mob killer gathers nothing, so
    the poison alone decides it (corpse_gear_for, fight.cpp), and move_wearables_to_corpse
    (fight.cpp) lifts the cap out of the bag. No roll is on the path: the poison tick and its
    damage are fixed, and the taster has no resistances.
    """
    implementor = server.login(IMPLEMENTOR.name)
    hand_the_taster_a_bagged_cap(implementor)
    load_object(implementor, world.OBJECT_TAINTED_BREAD, "a piece of tainted bread")
    assert implementor.command("give bread taster").contains("You give"), implementor.everything
    eaten = implementor.command("force taster eat bread")
    assert eaten.contains("Ok."), eaten.text

    taster = read_stat(implementor, "mob taster")
    hit_points = first_number(HIT_POINTS_UNSIGNED_MAXIMUM_PATTERN, taster)
    constitution = first_number(CONSTITUTION_PATTERN, taster)
    assert (hit_points, constitution) == (1, 2), taster
    # The poison tick kills (damage and update_pos, fight.cpp; the attacker is the taster, a mob,
    # so the hit points are not driven to -20):
    #   GET_HIT(victim) - dam          <= -GET_CON(victim) / 2
    #   = 1 - 5                        <= -2 / 2          (HP :[1/1+...], Con: [2/2/2])
    #     (dam: damage(i, i, 5, SPELL_POISON, 0) in affect_update; check_resistances is 0 for
    #      RES <NONE> VUL <NONE>, and no shield)
    #   = -4 <= -1: POSITION_DEAD.
    assert hit_points - POISON_TICK_DAMAGE <= -(constitution // 2)

    died = server.text_after_hours(implementor, expected_marker=TASTER_DEAD_MARKER)
    assert TASTER_DEAD_MARKER in died, died
    assert_the_corpse_holds_the_cap_outside_the_bag(implementor)


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, SLINGER)
def test_a_mob_a_player_kills_has_its_bagged_cap_gathered_into_its_corpse(server) -> None:
    """Pins a player's kill gathering the wearable cap out of the bag into the corpse.

    The mage kills the taster with a magic missile: the killer is a player, so
    move_wearables_to_corpse (fight.cpp) lifts the cap out of the bag (corpse_gear_for,
    fight.cpp). The concentration roll (do_cast, spell_pa.cpp) fails once in 101 at knowledge
    100 and only gates the attempt, so the cast is repeated after
    "You lost your concentration!", bounded by the mana each failure spends. The damage and
    save rolls cannot keep the taster alive (the margin below).
    """
    implementor = server.login(IMPLEMENTOR.name)
    slinger = server.login(SLINGER.name)
    hand_the_taster_a_bagged_cap(implementor)

    taster = read_stat(implementor, "mob taster")
    hit_points = first_number(HIT_POINTS_UNSIGNED_MAXIMUM_PATTERN, taster)
    saving_throw = first_number(SAVING_THROW_PATTERN, taster)
    slinger_stat = read_stat(implementor, f"player {SLINGER.name.lower()}")
    mage_level = first_number(MAGE_LEVEL_PATTERN, slinger_stat)
    assert (hit_points, saving_throw, mage_level) == (1, 0, 30)
    # The least damage the missile can deal (spell_magic_missile and apply_spell_damage, mage.cpp):
    #   dam = 12 + number(1, mag_power / 6)          >= 13  (mag_power is at least Mag:30)
    #   saved: dam >> 1                              >= 13 >> 1 = 6
    #   saving_throw = GET_SAVE(victim) - Mag / 5.0  = 0 - 30 / 5.0 = -6
    #                  (Saving throws 0; Mag:30; a player caster always applies spell penetration)
    #   multiplier = 2.0 - 20.0 / (20.0 - saving_throw) = 2.0 - 20.0 / 26.0 = 1.23
    #   dam = int(6 * 1.23)                          = 7
    #     (damage, fight.cpp: no resistance, vulnerability or shield on the taster)
    # A player's blow then drives GET_HIT(victim) = 1 - 7 = -6, below 0, to -20 or lower:
    #   -20 <= -GET_CON(victim) / 2 = -1: POSITION_DEAD.
    assert int((13 >> 1) * (2.0 - 20.0 / (20.0 - (saving_throw - mage_level / 5.0)))) == 7
    assert hit_points - 7 < 0

    while True:
        outcome = reply_to(slinger, "cast 'magic missile' taster", [TASTER_DEAD_MARKER, LOST_CONCENTRATION_REPLY, NO_ENERGY_REPLY], timeout=CAST_TIMEOUT)
        assert NO_ENERGY_REPLY not in outcome, "the mage ran out of mana before a cast held"
        if TASTER_DEAD_MARKER in outcome:
            break
    implementor.drain(SETTLE_SECONDS)
    assert_the_corpse_holds_the_cap_outside_the_bag(implementor)

    # The blow gave the mage anger (on_attacked_character, char_utils_combat.cpp) for 2 hours, and
    # do_quit (act_othe.cpp) refuses anyone angry, so the test's own quit waits for it to pass.
    angry = read_stat(implementor, f"player {SLINGER.name.lower()}")
    assert ANGER_AFFECT_PATTERN.search(angry), angry
    server.advance_hours(ANGER_HOURS + 1)
    slinger.drain(SETTLE_SECONDS)
    implementor.drain(SETTLE_SECONDS)
    calm = read_stat(implementor, f"player {SLINGER.name.lower()}")
    assert not ANGER_AFFECT_PATTERN.search(calm), calm
