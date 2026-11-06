"""Identify's output: race-restriction and stay-zone flags each get their own line, the list ends
without a lone full stop, and the weapon, food and affection sections read as #332 left them."""

from __future__ import annotations

import re

import pytest

from rots_harness import world
from rots_harness.characters import IMPLEMENTOR, Character, StoredValues
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

# The objects identified, each with its keyword and short description (tests/integration/world):
# the dwarven stone (obj/16.obj), whose extra flags are DWARF (bit 18) and STAY_ZONE (bit 28) and
# nothing else; the plain sword (obj/16.obj), a slashing weapon with OB 5, parry 2 and bulk 2;
# the tainted bread (obj/11.obj), food that fills 2 and is poisoned; and the sickly amulet
# (obj/11.obj), the one object with an affect.
DWARVEN_STONE_VNUM = 1602
STONE_KEYWORD = "stone"
STONE_SHORT_DESCRIPTION = "a dwarven stone"
PLAIN_SWORD_VNUM = 1601
SWORD_KEYWORD = "sword"
SWORD_SHORT_DESCRIPTION = "a plain sword"
BREAD_VNUM = 1139
BREAD_KEYWORD = "bread"
BREAD_SHORT_DESCRIPTION = "a piece of tainted bread"
AMULET_VNUM = 1138
AMULET_KEYWORD = "amulet"
AMULET_SHORT_DESCRIPTION = "a sickly amulet"

# What the implementor reads when the load and the give go through (do_load, act_wiz.cpp; give).
CREATED_MARKER_FORMAT = "You create {}."
GIVEN_MARKER_FORMAT = "You give {} to {}."
# The first line identify prints (do_identify_object, act_info.cpp).
IDENTIFIED_MARKER_FORMAT = "You feel certain the object you have is {}."
# What the caster reads when the concentration roll fails (do_cast, spell_pa.cpp):
# number(0, 100) >= 100 still happens once in 101 casts at full knowledge.
LOST_CONCENTRATION_MARKER = "You lost your concentration!"
# What the caster reads when its mana no longer covers a cast (can_cast_spell, spell_pa.cpp).
NO_ENERGY_MARKER = "You can't summon enough energy to cast the spell."
# What the caster reads when it does not carry the target (report_wrong_target, interpre.cpp).
NO_TARGET_MARKER = "You don't have that."
# What the caster reads when it does not know the spell (can_cast_spell).
UNKNOWN_SPELL_MARKER = "You don't know this spell."
# The header of identify's attribute list and the lines the stone's two flags print, in bit
# order (sprintbit, utility.cpp, with extra_messages[], act_info.cpp).
ATTRIBUTES_HEADER = "This item has the following attributes:"
EXPECTED_ATTRIBUTE_LINES = ["Dwarves only", "It cannot leave this area"]
# What identify says of an object without extra flags (sprintbit's identify wording).
NO_ATTRIBUTES_LINE = "This item has no additional attributes."
# The sword's weapon sentence (do_weapon_display, weapon_types[3]) and its stat rows: each label
# padded to 16 columns, then two spaces before a number that is not negative (send_identify_stat).
# The damage rating comes from get_weapon_damage (the sword's own value is 0), so only its form
# is pinned.
WEAPON_SENTENCE = "The weapon you hold is a slashing weapon."
DAMAGE_RATING_PATTERN = re.compile(r"Damage Rating {5}\d+/10")
WEAPON_STAT_ROWS = ["Offensive Bonus   5", "Parry Bonus       2", "Bulk              2"]
# The bread's food sentence, over two lines (food_messages[0] holds a line break), starting with
# the short description given a capital (do_food_display).
FOOD_SENTENCE_LINES = [
    "A piece of tainted bread is barely a morsel of food, and will",
    "do little to aid against the pangs of hunger. However it seems to be of a less than wholesome quality.",
]
# The header of identify's affection list (do_identify_object).
AFFECTIONS_HEADER = "This item has the following affections:"

# Seconds to wait for a cast's outcome: identify's 50 beats at mage level 30 are 25
# (CASTING_TIME, spells.h), about three seconds; 15 leaves room for a loaded host.
CAST_TIMEOUT = 15.0
# Seconds to keep reading after an outcome, for text already sent to arrive.
SETTLE_SECONDS = 1.0


def prepare_mage_with_object(implementor: GameSession, vnum: int, keyword: str, short_description: str) -> None:
    """Loads the object and hands it to the mage."""
    mage_keyword = MAGE.name.lower()
    created = implementor.command(f"load obj {vnum}")
    assert created.contains(CREATED_MARKER_FORMAT.format(short_description)), created.text
    given = implementor.command(f"give {keyword} {mage_keyword}")
    assert given.contains(GIVEN_MARKER_FORMAT.format(short_description, MAGE.name)), given.text


def cast_identify(caster: GameSession, keyword: str, short_description: str) -> str:
    """Casts identify on the object; returns what the caster read once the spell went off.

    A lost concentration is the one roll that can stop the cast, so the cast is repeated after
    it. Each loss costs mana and nothing restores mana between requests under affect-hours, so
    the repeats end at NO_ENERGY_MARKER at the latest.
    """
    identified_marker = IDENTIFIED_MARKER_FORMAT.format(short_description)
    outcomes = [identified_marker, LOST_CONCENTRATION_MARKER, NO_ENERGY_MARKER, NO_TARGET_MARKER, UNKNOWN_SPELL_MARKER]
    while True:
        caster.send_line(f"cast 'identify' {keyword}")
        reply = caster.expect(outcomes, timeout=CAST_TIMEOUT) + caster.drain(SETTLE_SECONDS)
        if LOST_CONCENTRATION_MARKER not in reply:
            assert identified_marker in reply, reply
            return reply


def identify_new_object(server, vnum: int, keyword: str, short_description: str) -> str:
    """Logs in the implementor and the mage, hands the mage the object and returns its identify."""
    implementor = server.login(IMPLEMENTOR.name)
    mage = server.login(MAGE.name)
    prepare_mage_with_object(implementor, vnum, keyword, short_description)
    return cast_identify(mage, keyword, short_description)


def attribute_lines(reply: str) -> list:
    """The lines after the attribute header, up to the first blank line, stripped."""
    lines = [line.strip() for line in reply.splitlines()]
    assert ATTRIBUTES_HEADER in lines, reply
    attributes = []
    for line in lines[lines.index(ATTRIBUTES_HEADER) + 1:]:
        if not line:
            break
        attributes.append(line)
    return attributes


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, MAGE)
def test_identify_lists_race_restriction_and_stay_zone_lines_without_a_lone_full_stop(server) -> None:
    """Pins #332: identify gives each race-restriction and STAY_ZONE flag its own line, and no
    line is a lone full stop.

    Before #332, extra_messages[] stopped at bit 15, so these flags printed empty lines and the
    list ended in a line holding only `.`. The one draw that can stop the cast is the
    concentration roll in do_cast, which cast_identify repeats; the affect-hours schedule keeps
    everything else from drawing.
    """
    reply = identify_new_object(server, DWARVEN_STONE_VNUM, STONE_KEYWORD, STONE_SHORT_DESCRIPTION)
    assert attribute_lines(reply) == EXPECTED_ATTRIBUTE_LINES, reply
    assert "." not in [line.strip() for line in reply.splitlines()], reply


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, MAGE)
def test_identify_describes_a_weapon_with_its_type_and_aligned_stat_rows(server) -> None:
    """Pins #332's weapon section: the weapon type sentence, then a blank line and the damage
    rating, offensive bonus, parry bonus and bulk rows, each number in the same column with no
    full stop after it; a weapon without extra flags has no attribute list.

    The one draw that can stop the cast is the concentration roll in do_cast, which
    cast_identify repeats; the affect-hours schedule keeps everything else from drawing.
    """
    reply = identify_new_object(server, PLAIN_SWORD_VNUM, SWORD_KEYWORD, SWORD_SHORT_DESCRIPTION)
    lines = [line.strip() for line in reply.splitlines()]
    assert WEAPON_SENTENCE in lines, reply
    sentence_index = lines.index(WEAPON_SENTENCE)
    assert lines[sentence_index + 1] == "", reply
    assert DAMAGE_RATING_PATTERN.fullmatch(lines[sentence_index + 2]), reply
    assert lines[sentence_index + 3:sentence_index + 6] == WEAPON_STAT_ROWS, reply
    assert NO_ATTRIBUTES_LINE in lines, reply


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, MAGE)
def test_identify_starts_the_food_sentence_with_a_capital(server) -> None:
    """Pins #332's food sentence: it starts with the food's short description given a capital,
    and says how filling and how wholesome the food is.

    The one draw that can stop the cast is the concentration roll in do_cast, which
    cast_identify repeats; the affect-hours schedule keeps everything else from drawing.
    """
    reply = identify_new_object(server, BREAD_VNUM, BREAD_KEYWORD, BREAD_SHORT_DESCRIPTION)
    lines = [line.strip() for line in reply.splitlines()]
    assert FOOD_SENTENCE_LINES[0] in lines, reply
    sentence_index = lines.index(FOOD_SENTENCE_LINES[0])
    assert lines[sentence_index:sentence_index + 2] == FOOD_SENTENCE_LINES, reply


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, MAGE)
def test_identify_heads_the_affection_list_with_a_colon_and_no_leading_space(server) -> None:
    """Pins #332's affection list: its header ends in a colon, and the affection line after it
    starts with the modifier's sign, without the space it once had.

    The one draw that can stop the cast is the concentration roll in do_cast, which
    cast_identify repeats; the affect-hours schedule keeps everything else from drawing.
    """
    reply = identify_new_object(server, AMULET_VNUM, AMULET_KEYWORD, AMULET_SHORT_DESCRIPTION)
    raw_lines = reply.splitlines()
    stripped_lines = [line.strip() for line in raw_lines]
    assert AFFECTIONS_HEADER in stripped_lines, reply
    affection_line = raw_lines[stripped_lines.index(AFFECTIONS_HEADER) + 1]
    assert affection_line.startswith("+"), reply
