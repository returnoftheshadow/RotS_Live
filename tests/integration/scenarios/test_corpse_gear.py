"""Corpse gear: a death to poison or to a player gathers the wearable gear in the dead one's bags
into the top of its corpse."""

from __future__ import annotations

import re

import pytest

from rots_harness import world
from rots_harness.characters import IMPLEMENTOR, Character
from rots_harness.session import GameSession

pytestmark = [pytest.mark.scenario, pytest.mark.corpse_gear]

# The frail taster (mob 2205, tests/integration/world/mob/22.mob): one hit point, constitution 2,
# no script, no resistances.
TASTER_VNUM = 2205
# The leather bag (object 1136), a container.
BAG_VNUM = 1136
# The leather cap (object 1137), head armour: wearable, so a gathering corpse lifts it out of
# the bag.
CAP_VNUM = 1137
# The tainted bread (object 1139): eaten, it poisons anyone below immortal level.
TAINTED_BREAD_VNUM = 1139
# How look names the Order Hall.
ORDER_HALL_NAME = "Order Hall"
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

# What the room reads as a mob dies (damage, fight.cpp); format with the mob.
IS_DEAD_FORMAT = "{} is dead!  R.I.P."
# What the room reads as the taster dies.
TASTER_DEAD_MARKER = IS_DEAD_FORMAT.format("A frail taster")
# What `look in corpse` prints before the corpse's contents (do_look, act_info.cpp).
CORPSE_CONTENTS_HEADER = "corpse (here) :"
# What `look in bag` prints before a carried bag's contents.
BAG_CONTENTS_HEADER = "bag (carried) :"
# What the caster reads when the concentration roll fails (do_cast, spell_pa.cpp).
LOST_CONCENTRATION_MARKER = "You lost your concentration!"
# What the caster reads when its mana does not cover the cast (can_cast_spell, spell_pa.cpp).
NO_ENERGY_MARKER = "You can't summon enough energy to cast the spell."
# Seconds to wait for a cast's outcome: magic missile's casting delay at mage level 30 is a few
# seconds (CASTING_TIME, spells.h); 20 leaves room for a loaded host.
CAST_TIMEOUT = 20.0
# The stat line just before the affect lines: once it arrives, the drain collects the rest.
STAT_LAST_LINE_MARKER = "VUL:"
# A stat line's current hit points (do_stat_character, act_wiz.cpp).
HIT_POINTS_PATTERN = re.compile(r"HP :\[(-?\d+)/(\d+)\+")
# A stat line's current constitution; `\s*` covers the space the format string puts after "Con:".
CONSTITUTION_PATTERN = re.compile(r"Con:\s*\[(-?\d+)/")
# The `stat mob` line of a mob's saving throw field.
SAVING_THROW_PATTERN = re.compile(r"Saving throws (-?\d+),")
# The `stat player` line of a character's profession levels.
MAGE_LEVEL_PATTERN = re.compile(r"Class levels: Mag:(\d+) ")
# The game hours of anger a player's blow at a mob gives (on_attacked_character,
# char_utils_combat.cpp).
ANGER_HOURS = 2
# The anger affect's row in `stat player` (do_stat_character, act_wiz.cpp).
ANGER_AFFECT_PATTERN = re.compile(r"^\s*SPL: \(\s*-?\d+hr\) anger", re.MULTILINE)
# Seconds to keep reading after an outcome, for text already sent to arrive.
SETTLE_SECONDS = 1.0


def load_object(implementor: GameSession, vnum: int, short_description: str) -> None:
    """Loads an object into the implementor's inventory and asserts the reply names it."""
    created = implementor.command(f"load obj {vnum}")
    assert created.contains(f"You create {short_description}."), created.text


def contents_lines(text: str, header: str) -> list:
    """The lines `look in` lists after header, up to the prompt."""
    after_header = text.split(header, 1)[1]
    return [line.strip() for line in after_header.splitlines()[1:] if line.strip() and not line.rstrip().endswith(">")]


def stat(implementor: GameSession, target: str) -> str:
    """The implementor's whole `stat <target>` reply, whose lines can end in `]`."""
    implementor.send_line(f"stat {target}")
    return implementor.expect([STAT_LAST_LINE_MARKER]) + implementor.drain(SETTLE_SECONDS)


def first_number(pattern: re.Pattern, text: str) -> int:
    """The first group of pattern's first match in text, as a number."""
    match = pattern.search(text)
    assert match is not None, text
    return int(match.group(1))


def hand_the_taster_a_bagged_cap(implementor: GameSession) -> None:
    """Brings the implementor to the Order Hall, loads the taster and gives it a bag with a cap in it."""
    arrived = implementor.command(f"goto {world.ROOM_ORDER_HALL}")
    assert arrived.room_name() == ORDER_HALL_NAME, arrived.text
    created = implementor.command(f"load mob {TASTER_VNUM}")
    assert created.contains("You create a frail taster."), created.text
    load_object(implementor, BAG_VNUM, "a leather bag")
    load_object(implementor, CAP_VNUM, "a leather cap")
    assert implementor.command("put cap bag").contains("You put a leather cap in a leather bag."), implementor.everything
    given = implementor.command("give bag taster")
    assert given.contains("You give a leather bag to a frail taster."), given.text


def assert_the_corpse_holds_the_cap_outside_the_bag(implementor: GameSession) -> None:
    """Asserts the taster's corpse lists the bag and the cap, and the bag no longer holds the cap."""
    corpse = implementor.command("look in corpse").text
    corpse_contents = contents_lines(corpse, CORPSE_CONTENTS_HEADER)
    assert "a leather bag" in corpse_contents, corpse
    assert "a leather cap" in corpse_contents, corpse
    taken = implementor.command("get bag corpse")
    assert taken.contains("You get a leather bag from"), taken.text
    bag = implementor.command("look in bag").text
    assert "a leather cap" not in bag, bag


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
    load_object(implementor, TAINTED_BREAD_VNUM, "a piece of tainted bread")
    assert implementor.command("give bread taster").contains("You give"), implementor.everything
    eaten = implementor.command("force taster eat bread")
    assert eaten.contains("Ok."), eaten.text

    taster = stat(implementor, "mob taster")
    hit_points = first_number(HIT_POINTS_PATTERN, taster)
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

    server.advance_hours(1)
    died = implementor.expect([TASTER_DEAD_MARKER]) + implementor.drain(SETTLE_SECONDS)
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

    taster = stat(implementor, "mob taster")
    hit_points = first_number(HIT_POINTS_PATTERN, taster)
    saving_throw = first_number(SAVING_THROW_PATTERN, taster)
    slinger_stat = stat(implementor, f"player {SLINGER.name.lower()}")
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
        slinger.send_line("cast 'magic missile' taster")
        outcome = slinger.expect([TASTER_DEAD_MARKER, LOST_CONCENTRATION_MARKER, NO_ENERGY_MARKER], timeout=CAST_TIMEOUT)
        outcome += slinger.drain(SETTLE_SECONDS)
        assert NO_ENERGY_MARKER not in outcome, "the mage ran out of mana before a cast held"
        if TASTER_DEAD_MARKER in outcome:
            break
    implementor.drain(SETTLE_SECONDS)
    assert_the_corpse_holds_the_cap_outside_the_bag(implementor)

    # The blow gave the mage anger (on_attacked_character, char_utils_combat.cpp) for 2 hours, and
    # do_quit (act_othe.cpp) refuses anyone angry, so the test's own quit waits for it to pass.
    angry = stat(implementor, f"player {SLINGER.name.lower()}")
    assert ANGER_AFFECT_PATTERN.search(angry), angry
    server.advance_hours(ANGER_HOURS + 1)
    slinger.drain(SETTLE_SECONDS)
    implementor.drain(SETTLE_SECONDS)
    calm = stat(implementor, f"player {SLINGER.name.lower()}")
    assert not ANGER_AFFECT_PATTERN.search(calm), calm
