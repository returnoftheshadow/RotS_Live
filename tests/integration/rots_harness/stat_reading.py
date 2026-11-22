"""Reads an implementor's stat replies and the numbers in them.

stat prints lines ending in "]", where GameSession.command would stop early, so read_stat waits
for the reply's last line instead. The patterns are the stat lines two or more scenario files
read with the same pattern text; one a single file reads stays in that file.
"""

from __future__ import annotations

import re
from typing import Sequence

from rots_harness.replies import SETTLE_SECONDS, reply_to
from rots_harness.session import GameSession

# The line of a character's stat just before its affect lines (do_stat_character, act_wiz.cpp);
# the settle reads the affect lines after it.
CHARACTER_STAT_LAST_LINE = "VUL:"
# The last line of an object's or a room's stat, which lists their affections (do_stat_object,
# do_stat_room, act_wiz.cpp). A room's affections follow it on lines of their own.
OBJECT_STAT_LAST_LINE = "Affections:"

# The hit point line's current and maximum hit points (do_stat_character, act_wiz.cpp).
HIT_POINTS_PATTERN = re.compile(r"HP :\[(-?\d+)/(-?\d+)")
# The hit point line's current and maximum hit points, matching only where the gain's "+"
# follows the maximum.
HIT_POINTS_BEFORE_GAIN_PATTERN = re.compile(r"HP :\[(-?\d+)/(-?\d+)\+")
# The hit point line's current hit points and an unsigned maximum, which the gain's "+" follows.
HIT_POINTS_UNSIGNED_MAXIMUM_PATTERN = re.compile(r"HP :\[(-?\d+)/(\d+)\+")
# The ability line's current constitution; `\s*` covers the space the format puts after "Con:".
CONSTITUTION_PATTERN = re.compile(r"Con:\s*\[(-?\d+)/")
# The ability line's strength: current, base and constant.
STRENGTH_PATTERN = re.compile(r"Str:\[(-?\d+)/(-?\d+)/(-?\d+)\]")
# The ability line's learning: current, base and constant.
LEARNING_PATTERN = re.compile(r"Lea:\[(-?\d+)/(-?\d+)/(-?\d+)\]")
# The level on the line that also shows experience and alignment.
LEVEL_PATTERN = re.compile(r"Lev: \[\s*(\d+)\]")
# The practice sessions left, on the hometown line.
PRACTICE_SESSIONS_PATTERN = re.compile(r"\(pracs left:\[(-?\d+)\]\)")
# The mage level on the profession levels line.
MAGE_LEVEL_PATTERN = re.compile(r"Class levels: Mag:(\d+) ")
# The cleric level on the profession levels line.
CLERIC_LEVEL_PATTERN = re.compile(r"Class levels: Mag:\d+ Cle:(\d+) ")
# The saving throw on the combat line.
SAVING_THROW_PATTERN = re.compile(r"Saving throws (-?\d+),")
# The affect bits line's flag names, such as "POISON.", or "<NONE>".
AFFECT_BITS_PATTERN = re.compile(r"AFF: (.*)")
# The affect bits line's flag names, matching only where "AFF:" starts a line after any spaces.
AFFECT_BITS_AT_LINE_START_PATTERN = re.compile(r"^\s*AFF: (.*)$", re.MULTILINE)
# The anger affect's line among a character's affect lines.
ANGER_AFFECT_PATTERN = re.compile(r"^\s*SPL: \(\s*-?\d+hr\) anger", re.MULTILINE)


def read_stat(
    implementor: GameSession,
    target: str,
    last_line: str = CHARACTER_STAT_LAST_LINE,
    other_outcomes: Sequence[str] = (),
    settle: float = SETTLE_SECONDS,
) -> str:
    """The implementor's reply to `stat <target>`, such as target "player testsaint", "mob boar" or "obj sword".

    Reads up to the first of last_line and other_outcomes, then settle seconds more (reply_to).
    other_outcomes are answers the caller tells apart, such as "No such mobile around."; the
    reply is returned whichever arrives, and nothing in it is asserted. Fails with
    SessionTimeout when none arrives within expect's default timeout.
    """
    outcomes = [last_line, *other_outcomes]
    return reply_to(implementor, f"stat {target}", outcomes, settle=settle)
