"""Known bugs in identify's output, each pinned as a strict xfail of the correct behaviour.

Each test starts failing as an unexpected pass once its bug is fixed.
"""

from __future__ import annotations

import pytest

from rots_harness import world
from rots_harness.characters import IMPLEMENTOR
from test_identify import ATTRIBUTES_HEADER, MAGE, identify_new_object

pytestmark = [pytest.mark.scenario, pytest.mark.identify]

# The strange relic (world.OBJECT_STRANGE_RELIC) has the wear flags TAKE and THROW (bit 15), and
# every extra flag from bit 0 to 28 but 5 (INVISIBLE), 7 (NODROP) and 13 (the retired !DONATE).
# Its WILLPOWER (bit 15) is gone by the time the mage holds it: the give clears it
# (obj_from_char, handler.cpp), which test_object_versions_known_bugs.py pins. NOINVIS (bit 14)
# stays.

# The start of identify's wear sentence for an object of type other (do_identify_object,
# item_messages[12], act_info.cpp).
WEAR_SENTENCE_START = "This item can be"
# Text that marks a placeholder rather than a description.
ERROR_PART = "ERROR"
# What wear_messages[15] says today for the THROW bit.
LIGHT_SOURCE_PART = "light source"


def identify_relic(server) -> list[str]:
    """Hand the mage the relic, cast identify on it and return the reply's lines, stripped.

    The cast is repeated past a lost concentration (identify_new_object).
    """
    reply = identify_new_object(
        server,
        world.OBJECT_STRANGE_RELIC,
        world.OBJECT_STRANGE_RELIC_KEYWORD,
        world.OBJECT_STRANGE_RELIC_SHORT_DESCRIPTION,
    )
    reply_lines = reply.splitlines()
    return [line.strip() for line in reply_lines]


@pytest.mark.xfail(
    strict=True,
    raises=AssertionError,
    reason="identify should describe the NOINVIS and WILLPOWER flags; today extra_messages[] prints "
    "'ERROR: Please report' for them",
)
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, MAGE)
def test_identify_describes_every_flag_a_builder_can_set(server) -> None:
    """Bug: identify's attribute list prints a placeholder for two flags builders set.

    Every flag an object can carry should have a description. extra_messages[] (act_info.cpp)
    holds `ERROR: Please report` for bits 13 to 15. Bit 13 (!DONATE) is retired, but bits 14
    (NOINVIS, not allowed to cast invisibility on) and 15 (WILLPOWER, can harm a wraith; live obj
    33601, a spectral rapier, has it) are set by builders, so players read the error line. The one
    draw that can stop the cast is the concentration roll in do_cast, which identify_relic
    repeats; the affect-hours schedule keeps everything else from drawing.
    """
    lines = identify_relic(server)
    assert ATTRIBUTES_HEADER in lines, lines
    attributes = []
    attributes_start = lines.index(ATTRIBUTES_HEADER) + 1
    for line in lines[attributes_start:]:
        if not line:
            break
        attributes.append(line)
    assert not [line for line in attributes if ERROR_PART in line], attributes


@pytest.mark.xfail(
    strict=True,
    raises=AssertionError,
    reason="identify should say a THROW item can be thrown; today wear_messages[15] says 'used as a "
    "light source'",
)
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, MAGE)
def test_identify_says_a_throwable_item_can_be_thrown_not_used_as_a_light(server) -> None:
    """Bug: identify's wear sentence describes a throwable item as a light source.

    It should name the THROW wear flag. wear_messages[] (act_info.cpp) follows the wear bits one
    for one, and bit 15 is ITEM_THROW (structs.h), but its entry reads `used as a light source`,
    so a throwable item is described as a light. No live object has THROW today. The one draw
    that can stop the cast is the concentration roll in do_cast, which identify_relic repeats;
    the affect-hours schedule keeps everything else from drawing.
    """
    lines = identify_relic(server)
    wear_sentences = [line for line in lines if line.startswith(WEAR_SENTENCE_START)]
    assert len(wear_sentences) == 1, lines
    assert LIGHT_SOURCE_PART not in wear_sentences[0], wear_sentences
