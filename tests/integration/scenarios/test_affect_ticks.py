"""The hourly poison tick and what it does within one affect pass.

Pins a mob it kills, the characters after it in the same pass, and a sanctuary it breaks.
"""

from __future__ import annotations

import re

import pytest

from rots_harness import world
from rots_harness.characters import IMPLEMENTOR, Character, StoredValues
from rots_harness.game_text import DEATH_CRY_FORMAT, EATEN_POISON_REPLY, FORCED_REPLY, IS_DEAD_FORMAT, POISON_TICK
from rots_harness.immortal_commands import give_new_object, goto, load_mob
from rots_harness.replies import SETTLE_SECONDS, reply_to
from rots_harness.session import GameSession
from rots_harness.stat_reading import AFFECT_BITS_PATTERN, CONSTITUTION_PATTERN, HIT_POINTS_PATTERN, read_stat
from rots_harness.text_lines import first_number

pytestmark = [pytest.mark.scenario, pytest.mark.affect_ticks]

# Room 2101 in zone 21, where the poisoned are watched.
ROOM_SICK_WARD = 2101
# How look names the sick ward.
ROOM_SICK_WARD_NAME = "Sick Ward"
# A level-20 human warrior in the sick ward, hungry enough to eat (above 20 is too full): a
# mortal, since immortals are not poisoned by food.
TASTER = Character(
    name="Testtaster",
    race="human",
    sex="male",
    level=20,
    load_room=ROOM_SICK_WARD,
    class_name="warrior",
    stored=StoredValues(hunger=10),
)
# The saint's alignment: sanctuary keeps its caster's alignment as its
# strength, and a negative one cannot hold against its own wearer's blow (check_sanctuary).
SAINT_ALIGNMENT = -500
# A level-20 human warrior with a negative alignment, who gives himself sanctuary.
SAINT = Character(
    name="Testsaint",
    race="human",
    sex="male",
    level=20,
    load_room=ROOM_SICK_WARD,
    class_name="warrior",
    stored=StoredValues(hunger=10, alignment=SAINT_ALIGNMENT),
)

# Zone 21's ivory wand of sanctuary, with one charge.
SANCTUARY_WAND_VNUM = 2103
# The damage of a poison tick (affect_update_person, limits.cpp).
POISON_TICK_DAMAGE = 5

# The straw scarecrow's short description (world.MOB_STRAW_SCARECROW).
SCARECROW_SHORT_DESCRIPTION = "a straw scarecrow"
# What the saint reads as he holds the wand of sanctuary (wear_message, act_obj2.cpp).
WAND_HELD_REPLY = "You grab an ivory wand of sanctuary."
# What the room reads when the scarecrow eats the bread.
SCARECROW_COUGHS = "A straw scarecrow coughs and utters some strange sounds."
# What the room reads when the scarecrow puts the sash on.
SCARECROW_WEARS_SASH = "A straw scarecrow wears a draining grey sash about his body."
# What the room reads on the scarecrow's poison tick (lib/misc/messages).
SCARECROW_WRITHES = "A straw scarecrow writhes in pain as the poison takes hold."
# What the room reads when the scarecrow dies (damage, fight.cpp).
SCARECROW_IS_DEAD = IS_DEAD_FORMAT.format("A straw scarecrow")
# What the room reads of the scarecrow's death cry (death_cry, fight.cpp).
SCARECROW_DEATH_CRY = DEATH_CRY_FORMAT.format(SCARECROW_SHORT_DESCRIPTION)
# How look shows the scarecrow's corpse after a poison death (get_corpse_desc, fight.cpp).
SCARECROW_CORPSE = "The rancid corpse of a straw scarecrow is lying here."
# The start of how look shows the scarecrow while it lives.
SCARECROW_STANDS_START = "A straw scarecrow stands here"
# What the room reads on the taster's poison tick.
TASTER_WRITHES = f"{TASTER.name} writhes in pain as the poison takes hold."
# What the room reads on the saint's poison tick.
SAINT_WRITHES = f"{SAINT.name} writhes in pain as the poison takes hold."
# What the saint reads when the wand's spell takes (spell_sanctuary, mystic.cpp).
SANCTUARY_REPLY = "You are surrounded by a bright aura."
# What the saint reads when his own poison breaks his sanctuary (check_sanctuary, fight.cpp).
SANCTUARY_BROKEN = "Your sanctuary is broken!"
# What the room reads then.
SAINT_SANCTUARY_BROKEN = f"{SAINT.name}'s sanctuary is broken!"
# The end of what the room reads when a sanctuary holds against a blow.
AURA_FLICKERS_END = "aura flickers."

# One stat line per affect, in the order of the character's affect list: hours left and name.
STAT_SPELL_PATTERN = re.compile(r"SPL: \(\s*(-?\d+)hr\) (\w+)")
# The sanctuary affect's stat line, whose modifier is the strength check_sanctuary wears down.
STAT_SANCTUARY_PATTERN = re.compile(r"SPL: \(\s*-?\d+hr\) sanctuary\s+(-?\d+) to NONE")


def stat_affects(text: str) -> tuple[str, tuple[tuple[int, str], ...]]:
    """Return the affect bits and the (hours, spell) affect lines of a stat reply."""
    affects = AFFECT_BITS_PATTERN.search(text)
    assert affects is not None, f"no AFF line in stat:\n{text}"
    spell_lines = STAT_SPELL_PATTERN.findall(text)
    spells = tuple((int(hours), name) for hours, name in spell_lines)
    bits_text = affects.group(1)
    return bits_text.strip(), spells


def force_scarecrow(implementor: GameSession, command: str, room_marker: str) -> None:
    """Have the scarecrow run command and wait for what the room reads of it, room_marker."""
    reply = reply_to(implementor, f"force scarecrow {command}", [room_marker])
    assert reply.count(FORCED_REPLY) == 1 and reply.count(room_marker) == 1, reply


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, TASTER)
def test_a_poison_tick_that_kills_a_mob_leaves_its_corpse_and_the_next_poisoned_mortal_still_ticks(server) -> None:
    """Pins a poison tick that kills a mob, and the tick of the character after it in the same pass.

    The taster eats tainted bread, then a straw scarecrow is made to eat some and put on the
    draining sash, which holds it at -52 hit points. The scarecrow ate last, so its entry heads
    the affected list (affect_to_char adds at the head) and the hour's affect pass ticks it first:
    the ward reads its poison tick, its death and its death cry, and then the taster's tick, which
    the taster reads too. The scarecrow leaves a rancid corpse holding the sash. Nothing on this
    path draws a number that changes the outcome.
    """
    implementor = server.login(IMPLEMENTOR.name)
    taster = server.login(TASTER.name)
    goto(implementor, ROOM_SICK_WARD, ROOM_SICK_WARD_NAME)

    give_new_object(implementor, world.OBJECT_TAINTED_BREAD, "bread", TASTER.name.lower())
    eaten = taster.command("eat bread")
    assert eaten.contains(EATEN_POISON_REPLY), eaten.text
    load_mob(implementor, world.MOB_STRAW_SCARECROW, SCARECROW_SHORT_DESCRIPTION)
    give_new_object(implementor, world.OBJECT_TAINTED_BREAD, "bread", "scarecrow")
    force_scarecrow(implementor, "eat bread", SCARECROW_COUGHS)
    give_new_object(implementor, world.OBJECT_GREY_SASH, "sash", "scarecrow")
    force_scarecrow(implementor, "wear sash", SCARECROW_WEARS_SASH)

    scarecrow = read_stat(implementor, "mob scarecrow")
    hit = first_number(HIT_POINTS_PATTERN, scarecrow)
    constitution = first_number(CONSTITUTION_PATTERN, scarecrow)
    # Scarecrow's tick, a mob's own poison (attacker and victim the scarecrow, so not a player's
    # blow):
    #   hit at the tick = min(hit + regeneration, maximum) <= -52               (HP :[-52/-52])
    #   after the tick  = -52 - POISON_TICK_DAMAGE = -57
    #   update_pos      : -57 <= -Con / 2 = -10 / 2 = -5                        (Con: [10/10/10])
    #                   = POSITION_DEAD, and damage calls die.
    assert (hit, constitution) == (-52, 10), scarecrow
    assert hit - POISON_TICK_DAMAGE <= -(constitution // 2), scarecrow
    assert stat_affects(scarecrow) == ("POISON.", ((5, "poison"),)), scarecrow

    first_hour = server.text_after_hours(implementor)
    for line in (SCARECROW_WRITHES, SCARECROW_IS_DEAD, SCARECROW_DEATH_CRY, TASTER_WRITHES):
        assert first_hour.count(line) == 1, f"{line!r}:\n{first_hour}"
    assert first_hour.index(SCARECROW_DEATH_CRY) < first_hour.index(TASTER_WRITHES), first_hour
    taster_hour = taster.drain(SETTLE_SECONDS)
    assert taster_hour.count(POISON_TICK) == 1, taster_hour
    assert taster_hour.count(SCARECROW_IS_DEAD) == 1, taster_hour

    ward = implementor.command("look")
    assert ward.contains(SCARECROW_CORPSE), ward.text
    assert SCARECROW_STANDS_START not in ward.text, ward.text
    corpse = implementor.command("look in corpse")
    assert corpse.contains(world.OBJECT_GREY_SASH_SHORT_DESCRIPTION), corpse.text

    second_hour = server.text_after_hours(implementor)
    assert second_hour.count(TASTER_WRITHES) == 1, second_hour
    assert SCARECROW_WRITHES not in second_hour, second_hour


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, SAINT)
def test_a_poison_tick_breaks_its_victims_own_sanctuary_and_still_poisons_him(server) -> None:
    """Fix pin (#561): a poison tick breaks the poisoned character's sanctuary in the same pass.

    The saint points the wand of sanctuary at himself, then eats tainted bread. His affect list
    is then poison, then sanctuary (affect_to_char adds at the head). In the next hour the poison
    tick's blow is his own, and check_sanctuary (fight.cpp) wears his sanctuary down by his own
    alignment and breaks it, freeing the sanctuary affect the pass would read next. He reads
    the break and the tick, the ward reads both, and the sanctuary is gone; the next hour's tick
    is an ordinary one. #561 made the pass skip the freed sanctuary affect; before it, the pass
    read it, a heap-use-after-free under AddressSanitizer. Nothing on this path draws a number
    that changes the outcome: do_use and spell_sanctuary draw nothing, and the break's draw only
    adds to a strength already below zero.
    """
    implementor = server.login(IMPLEMENTOR.name)
    saint = server.login(SAINT.name)
    goto(implementor, ROOM_SICK_WARD, ROOM_SICK_WARD_NAME)

    give_new_object(implementor, SANCTUARY_WAND_VNUM, "wand", SAINT.name.lower())
    held = saint.command("hold wand")
    assert held.contains(WAND_HELD_REPLY), held.text
    used = saint.command(f"use wand {SAINT.name.lower()}")
    assert used.contains(SANCTUARY_REPLY), used.text
    give_new_object(implementor, world.OBJECT_TAINTED_BREAD, "bread", SAINT.name.lower())
    eaten = saint.command("eat bread")
    assert eaten.contains(EATEN_POISON_REPLY), eaten.text

    before = read_stat(implementor, f"player {SAINT.name.lower()}")
    strength = first_number(STAT_SANCTUARY_PATTERN, before)
    # Saint's tick, his own poison as the attacker (check_sanctuary(ch, victim), both the saint):
    #   aff->modifier = aff->modifier - number(0, (-1 * GET_ALIGNMENT(ch)) / 2)
    #                 = -500          - number(0, (-1 * -500) / 2)
    #                   (SPL: sanctuary -500 to NONE; Align: [-500], stored)
    #                 = -500 - (0 to 250) <= -500, below 0: the sanctuary breaks whatever is drawn.
    assert strength == SAINT_ALIGNMENT, before
    assert stat_affects(before) == ("SANCT POISON.", ((5, "poison"), (9, "sanctuary"))), before

    first_hour = server.text_after_hours(saint)
    assert first_hour.count(SANCTUARY_BROKEN) == 1, first_hour
    assert first_hour.count(POISON_TICK) == 1, first_hour
    assert first_hour.index(SANCTUARY_BROKEN) < first_hour.index(POISON_TICK), first_hour
    ward = implementor.drain(SETTLE_SECONDS)
    assert ward.count(SAINT_SANCTUARY_BROKEN) == 1, ward
    assert ward.count(SAINT_WRITHES) == 1, ward
    assert AURA_FLICKERS_END not in ward, ward
    after = read_stat(implementor, f"player {SAINT.name.lower()}")
    assert stat_affects(after) == ("POISON.", ((4, "poison"),)), after

    second_hour = server.text_after_hours(saint)
    assert second_hour.count(POISON_TICK) == 1, second_hour
    assert SANCTUARY_BROKEN not in second_hour, second_hour
