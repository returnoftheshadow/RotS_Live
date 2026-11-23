"""Identify's output: its attribute list and its weapon, food and affection sections.

Race-restriction and stay-zone flags each get their own line, and the list ends without a lone
full stop.
"""

from __future__ import annotations

import re

import pytest

from rots_harness import world
from rots_harness.casting import NO_ENERGY_REPLY, cast_until_it_lands
from rots_harness.characters import IMPLEMENTOR, Character, StoredValues
from rots_harness.game_text import TARGET_NOT_CARRIED_REPLY, UNKNOWN_SPELL_REPLY
from rots_harness.immortal_commands import give_new_object
from rots_harness.session import GameSession

pytestmark = [pytest.mark.scenario, pytest.mark.identify]

# The base and current mana the mage is stored with: a fresh level-30 mage has less than
# identify's 70 (USE_MANA, spells.h), and a lost concentration costs half of it (do_cast).
MAGE_MAXIMUM_MANA = 500
# A level-30 human mage in the implementors' room who knows identify fully. Identify is a
# level-18 mage spell that targets an object the caster carries (skills[] in consts.cpp).
MAGE = Character(
    name="Testsage",
    race="human",
    sex="female",
    level=30,
    load_room=world.ROOM_IMMORTAL_START,
    class_name="mage",
    skills=(("identify", 100),),
    mana=MAGE_MAXIMUM_MANA,
    stored=StoredValues(maxstamina=MAGE_MAXIMUM_MANA),
)

# The keyword of the dwarven stone (world.OBJECT_DWARVEN_STONE), whose extra flags are DWARF
# (bit 18) and STAY_ZONE (bit 28) and nothing else.
STONE_KEYWORD = "stone"
# The keyword of the plain sword (world.OBJECT_PLAIN_SWORD), a slashing weapon with OB 5, parry 2
# and bulk 2.
SWORD_KEYWORD = "sword"
# The keyword of the tainted bread (world.OBJECT_TAINTED_BREAD), food that fills 2 and is
# poisoned.
BREAD_KEYWORD = "bread"
# The keyword of the sickly amulet (world.OBJECT_SICKLY_AMULET), the one object with an affect.
AMULET_KEYWORD = "amulet"

# The first line identify prints (do_identify_object, act_info.cpp); format with the object's
# short description.
IDENTIFIED_FORMAT = "You feel certain the object you have is {}."
# The header of identify's attribute list (do_identify_object, act_info.cpp).
ATTRIBUTES_HEADER = "This item has the following attributes:"
# The lines the stone's two flags print, in bit order (sprintbit, utility.cpp, with
# extra_messages[], act_info.cpp).
EXPECTED_ATTRIBUTE_LINES = ["Dwarves only", "It cannot leave this area"]
# What identify says of an object without extra flags (sprintbit's identify wording).
NO_ATTRIBUTES_LINE = "This item has no additional attributes."
# The sword's weapon sentence (do_weapon_display, weapon_types[3]).
WEAPON_SENTENCE = "The weapon you hold is a slashing weapon."
# The sword's damage rating row. The rating comes from get_weapon_damage (the sword's own value
# is 0), so only its form is pinned.
DAMAGE_RATING_PATTERN = re.compile(r"Damage Rating {5}\d+/10")
# The sword's other stat rows: each label padded to 16 columns, then two spaces before a number
# that is not negative (send_identify_stat).
WEAPON_STAT_ROWS = ["Offensive Bonus   5", "Parry Bonus       2", "Bulk              2"]
# The bread's food sentence, over two lines (food_messages[0] holds a line break), starting with
# the short description given a capital (do_food_display).
FOOD_SENTENCE_LINES = [
    "A piece of tainted bread is barely a morsel of food, and will",
    "do little to aid against the pangs of hunger. However it seems to be of a less than wholesome quality.",
]
# The header of identify's affection list (do_identify_object).
AFFECTIONS_HEADER = "This item has the following affections:"


def prepare_mage_with_object(implementor: GameSession, vnum: int, keyword: str, short_description: str) -> None:
    """Load the object and hand it to the mage."""
    give_new_object(
        implementor,
        vnum,
        keyword,
        MAGE.name.lower(),
        shown_recipient=MAGE.name,
        short_description=short_description,
    )


def cast_identify(caster: GameSession, keyword: str, short_description: str) -> str:
    """Cast identify on the object and return what the caster read once the spell went off.

    The cast is repeated past a lost concentration (cast_until_it_lands).
    """
    identified_marker = IDENTIFIED_FORMAT.format(short_description)
    outcomes = [identified_marker, NO_ENERGY_REPLY, TARGET_NOT_CARRIED_REPLY, UNKNOWN_SPELL_REPLY]
    reply = cast_until_it_lands(caster, f"cast 'identify' {keyword}", outcomes)
    assert identified_marker in reply, reply
    return reply


def identify_new_object(server, vnum: int, keyword: str, short_description: str) -> str:
    """Log in the implementor and the mage, hand the mage the object and return its identify."""
    implementor = server.login(IMPLEMENTOR.name)
    mage = server.login(MAGE.name)
    prepare_mage_with_object(implementor, vnum, keyword, short_description)
    return cast_identify(mage, keyword, short_description)


def attribute_lines(reply: str) -> list[str]:
    """Return the lines after the attribute header, up to the first blank line, stripped."""
    reply_lines = reply.splitlines()
    lines = [line.strip() for line in reply_lines]
    assert ATTRIBUTES_HEADER in lines, reply
    attributes = []
    attributes_start = lines.index(ATTRIBUTES_HEADER) + 1
    for line in lines[attributes_start:]:
        if not line:
            break
        attributes.append(line)
    return attributes


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, MAGE)
def test_identify_lists_race_restriction_and_stay_zone_lines_without_a_lone_full_stop(server) -> None:
    """Fix pin (#332): each race-restriction and STAY_ZONE flag has its own line, none a lone stop.

    #332 made each of these flags print its own line; before it, extra_messages[] stopped at bit 15,
    so they printed empty lines and the list ended in a line holding only `.`. The one draw that can
    stop the cast is the concentration roll in do_cast, which cast_identify repeats; the
    affect-hours schedule keeps everything else from drawing.
    """
    reply = identify_new_object(
        server,
        world.OBJECT_DWARVEN_STONE,
        STONE_KEYWORD,
        world.OBJECT_DWARVEN_STONE_SHORT_DESCRIPTION,
    )
    assert attribute_lines(reply) == EXPECTED_ATTRIBUTE_LINES, reply
    reply_lines = reply.splitlines()
    assert "." not in [line.strip() for line in reply_lines], reply


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, MAGE)
def test_identify_describes_a_weapon_with_its_type_and_aligned_stat_rows(server) -> None:
    """Pins identify's weapon section: the type sentence, then the stat rows in one column.

    A blank line follows the sentence, then the damage rating, offensive bonus, parry bonus and
    bulk rows, each number in the same column with no full stop after it; a weapon without extra
    flags has no attribute list.

    The one draw that can stop the cast is the concentration roll in do_cast, which
    cast_identify repeats; the affect-hours schedule keeps everything else from drawing.
    """
    reply = identify_new_object(
        server,
        world.OBJECT_PLAIN_SWORD,
        SWORD_KEYWORD,
        world.OBJECT_PLAIN_SWORD_SHORT_DESCRIPTION,
    )
    reply_lines = reply.splitlines()
    lines = [line.strip() for line in reply_lines]
    assert WEAPON_SENTENCE in lines, reply
    sentence_index = lines.index(WEAPON_SENTENCE)
    assert lines[sentence_index + 1] == "", reply
    assert DAMAGE_RATING_PATTERN.fullmatch(lines[sentence_index + 2]), reply
    assert lines[sentence_index + 3:sentence_index + 6] == WEAPON_STAT_ROWS, reply
    assert NO_ATTRIBUTES_LINE in lines, reply


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, MAGE)
def test_identify_starts_the_food_sentence_with_a_capital(server) -> None:
    """Pins identify's food sentence: it starts with the food's short description, capitalised.

    It also says how filling and how wholesome the food is.

    The one draw that can stop the cast is the concentration roll in do_cast, which
    cast_identify repeats; the affect-hours schedule keeps everything else from drawing.
    """
    reply = identify_new_object(
        server, world.OBJECT_TAINTED_BREAD, BREAD_KEYWORD, world.OBJECT_TAINTED_BREAD_SHORT_DESCRIPTION
    )
    reply_lines = reply.splitlines()
    lines = [line.strip() for line in reply_lines]
    assert FOOD_SENTENCE_LINES[0] in lines, reply
    sentence_index = lines.index(FOOD_SENTENCE_LINES[0])
    assert lines[sentence_index:sentence_index + 2] == FOOD_SENTENCE_LINES, reply


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, MAGE)
def test_identify_heads_the_affection_list_with_a_colon_and_no_leading_space(server) -> None:
    """Pins identify's affection list: a header ending in a colon, then a line opening with a sign.

    The affection line after the header starts with the modifier's sign.

    The one draw that can stop the cast is the concentration roll in do_cast, which
    cast_identify repeats; the affect-hours schedule keeps everything else from drawing.
    """
    reply = identify_new_object(
        server,
        world.OBJECT_SICKLY_AMULET,
        AMULET_KEYWORD,
        world.OBJECT_SICKLY_AMULET_SHORT_DESCRIPTION,
    )
    raw_lines = reply.splitlines()
    stripped_lines = [line.strip() for line in raw_lines]
    assert AFFECTIONS_HEADER in stripped_lines, reply
    affection_line = raw_lines[stripped_lines.index(AFFECTIONS_HEADER) + 1]
    assert affection_line.startswith("+"), reply
