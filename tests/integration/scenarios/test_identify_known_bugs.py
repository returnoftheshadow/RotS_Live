"""Known bugs in identify's output: each test pins the correct behaviour and is a strict xfail
until the bug is fixed, when it starts failing as an unexpected pass."""

from __future__ import annotations

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

# The relic (tests/integration/world/obj/16.obj): wear flags TAKE and THROW (bit 15), and every
# extra flag from bit 0 to 28 but 5 (INVISIBLE), 7 (NODROP) and 13 (the retired !DONATE). Its
# WILLPOWER (bit 15) is gone by the time the mage holds it: the give clears it (obj_from_char,
# handler.cpp), which test_object_versions_known_bugs.py pins. NOINVIS (bit 14) stays.
RELIC_VNUM = 1609
RELIC_KEYWORD = "relic"
RELIC_SHORT_DESCRIPTION = "a strange relic"

# What the implementor reads when the load and the give go through (do_load, act_wiz.cpp; give).
CREATED_MARKER = f"You create {RELIC_SHORT_DESCRIPTION}."
GIVEN_MARKER = f"You give {RELIC_SHORT_DESCRIPTION} to {MAGE.name}."
# The first line identify prints (do_identify_object, act_info.cpp).
IDENTIFIED_MARKER = f"You feel certain the object you have is {RELIC_SHORT_DESCRIPTION}."
# What the caster reads when the concentration roll fails (do_cast, spell_pa.cpp):
# number(0, 100) >= 100 still happens once in 101 casts at full knowledge.
LOST_CONCENTRATION_MARKER = "You lost your concentration!"
# What the caster reads when its mana no longer covers a cast (can_cast_spell, spell_pa.cpp).
NO_ENERGY_MARKER = "You can't summon enough energy to cast the spell."
# What the caster reads when it does not carry the target (report_wrong_target, interpre.cpp).
NO_TARGET_MARKER = "You don't have that."
# What the caster reads when it does not know the spell (can_cast_spell).
UNKNOWN_SPELL_MARKER = "You don't know this spell."
# The header of identify's attribute list (sprintbit, utility.cpp), and the wear sentence's start
# for an object of type other (do_identify_object, item_messages[12]).
ATTRIBUTES_HEADER = "This item has the following attributes:"
WEAR_SENTENCE_START = "This item can be"
# Text that marks a placeholder rather than a description.
ERROR_FRAGMENT = "ERROR"
# What wear_messages[15] says today for the THROW bit.
LIGHT_SOURCE_FRAGMENT = "light source"

# Seconds to wait for a cast's outcome: identify's 50 beats at mage level 30 are 25
# (CASTING_TIME, spells.h), about three seconds; 15 leaves room for a loaded host.
CAST_TIMEOUT = 15.0
# Seconds to keep reading after an outcome, for text already sent to arrive.
SETTLE_SECONDS = 1.0


def identify_relic(server) -> list:
    """Hands the mage the relic, casts identify on it and returns the reply's lines, stripped.

    A lost concentration is the one roll that can stop the cast, so the cast is repeated after
    it. Each loss costs mana and nothing restores mana between requests under affect-hours, so
    the repeats end at NO_ENERGY_MARKER at the latest.
    """
    implementor = server.login(IMPLEMENTOR.name)
    mage: GameSession = server.login(MAGE.name)
    mage_keyword = MAGE.name.lower()
    created = implementor.command(f"load obj {RELIC_VNUM}")
    assert created.contains(CREATED_MARKER), created.text
    given = implementor.command(f"give {RELIC_KEYWORD} {mage_keyword}")
    assert given.contains(GIVEN_MARKER), given.text

    outcomes = [IDENTIFIED_MARKER, LOST_CONCENTRATION_MARKER, NO_ENERGY_MARKER, NO_TARGET_MARKER, UNKNOWN_SPELL_MARKER]
    while True:
        mage.send_line(f"cast 'identify' {RELIC_KEYWORD}")
        reply = mage.expect(outcomes, timeout=CAST_TIMEOUT) + mage.drain(SETTLE_SECONDS)
        if LOST_CONCENTRATION_MARKER not in reply:
            assert IDENTIFIED_MARKER in reply, reply
            return [line.strip() for line in reply.splitlines()]


@pytest.mark.xfail(strict=True, raises=AssertionError, reason="identify should describe the NOINVIS and WILLPOWER flags; today extra_messages[] prints 'ERROR: Please report' for them")
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, MAGE)
def test_identify_describes_every_flag_a_builder_can_set(server) -> None:
    """Pins that identify's attribute list has a description, not a placeholder, for every flag
    an object can carry.

    Bug: extra_messages[] (act_info.cpp:4301-4303) holds `ERROR: Please report` for bits 13 to
    15. Bit 13 (!DONATE) is retired, but bits 14 (NOINVIS, not allowed to cast invisibility on)
    and 15 (WILLPOWER, can harm a wraith; live obj 33601, a spectral rapier, has it) are set by
    builders, so players read the error line. #332 kept these on purpose; the owner may rule
    otherwise. The one draw that can stop the cast is the concentration roll in do_cast, which
    identify_relic repeats; the affect-hours schedule keeps everything else from drawing.
    """
    lines = identify_relic(server)
    assert ATTRIBUTES_HEADER in lines, lines
    attributes = []
    for line in lines[lines.index(ATTRIBUTES_HEADER) + 1:]:
        if not line:
            break
        attributes.append(line)
    assert not [line for line in attributes if ERROR_FRAGMENT in line], attributes


@pytest.mark.xfail(strict=True, raises=AssertionError, reason="identify should say a THROW item can be thrown; today wear_messages[15] says 'used as a light source'")
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, MAGE)
def test_identify_says_a_throwable_item_can_be_thrown_not_used_as_a_light(server) -> None:
    """Pins that identify's wear sentence names the THROW wear flag correctly.

    Bug: wear_messages[] (act_info.cpp:4214-4235) follows the wear bits one for one, and bit 15
    is ITEM_THROW (structs.h), but its entry (act_info.cpp:4231) reads `used as a light source`,
    so a throwable item is described as a light. No live object has THROW today. The one draw
    that can stop the cast is the concentration roll in do_cast, which identify_relic repeats;
    the affect-hours schedule keeps everything else from drawing.
    """
    lines = identify_relic(server)
    wear_sentences = [line for line in lines if line.startswith(WEAR_SENTENCE_START)]
    assert len(wear_sentences) == 1, lines
    assert LIGHT_SOURCE_FRAGMENT not in wear_sentences[0], wear_sentences
