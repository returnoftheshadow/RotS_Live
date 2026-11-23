"""Unit tests for rots_harness.stat_reading, against a fake session and stat text the game sent."""

from __future__ import annotations

import re
from pathlib import Path
import pytest

from rots_harness.session import SessionTimeout
from rots_harness.stat_reading import (
    AFFECT_BITS_AT_LINE_START_PATTERN,
    AFFECT_BITS_PATTERN,
    ANGER_AFFECT_PATTERN,
    CHARACTER_STAT_LAST_LINE,
    CONSTITUTION_PATTERN,
    HIT_POINTS_BEFORE_GAIN_PATTERN,
    HIT_POINTS_PATTERN,
    HIT_POINTS_UNSIGNED_MAXIMUM_PATTERN,
    LEARNING_PATTERN,
    LEVEL_PATTERN,
    MAGE_LEVEL_PATTERN,
    NO_SUCH_MOB_REPLY,
    OBJECT_STAT_LAST_LINE,
    PRACTICE_SESSIONS_PATTERN,
    SAVING_THROW_PATTERN,
    STRENGTH_PATTERN,
    affect_bits,
    read_stat,
)
from rots_harness.text_lines import first_number
from unit.fakes import RecordingSession

# The game's stat source, whose format strings the last-line markers come from.
ACT_WIZ_SOURCE = Path(__file__).resolve().parents[3] / "src" / "act_wiz.cpp"

# A kept transcript's `stat player testpupil` (a level-30 human mage), abridged to the lines the
# patterns read and their neighbours, each ended as the game ends lines, "\n\r".
CHARACTER_STAT_TEXT = "\n\r".join(
    [
        "FEMALE PC 'Testpupil'  IDNum: [9000002], Player table index: [1], In room [ 1131]",
        "Title: the Human",
        "Race: human, which is number 1",
        "L-Des: <None>",
        "Class coofs : Mag:1000 Cle:500 Ran:400 War:300",
        "Class levels: Mag:30 Cle:15 Ran:12 War:9,  mini-level: 3000,mml:3000",
        "Spec:(0) nothing, Lev: [30], XP: [1350000], Align: [   0]",
        "Created: [Sun Nov 22], Last Logon: [Sun Nov 22], Played [0h 0m], Age [17]",
        "Hometown: [2], Speaks: [0/0/0], (pracs left:[190])",
        "Str:[14/14/12] Int:[16/16/16] Wil:[16/16/16] Dex:[14/14/14] Con: [13/13/13] Lea:[15/15/15]",
        "HP :[213/213+20(0)]  Stamina :[68/124+28(0)]  Move :[114/125+22(0)] Spirit:[9/0+0]   "
        "Consts(hit/stamina/move): 68/40/80",
        "Encumbrance 0, Leg_encu 0, Perception 60, Willpower 31,",
        "Coins: [      235]",
        " OB: 72(0), parry: 31(0), dodge: 20(0), c_parry 0,  Saving throws 0,",
        " Mood 3, ambush awareness 0, rerolls done: 25, absorb 0",
        "Pos: Standing, Fighting: Nobody, Connected: Playing  Hide_value:0.",
        "AFF: <NONE>",
        "RES: <NONE> VUL: <NONE>",
        "",
    ]
)
# A dying mob's hit point line and an anger affect line, built from do_stat_character's format
# strings ("HP :[%d/%d+%0.f(%0.f)]" and "SPL: (%3dhr) %s%-21s%s "), which no kept transcript holds.
DYING_MOB_STAT_TEXT = "\n\r".join(
    [
        "HP :[-52/-52+0(0)]  Stamina :[0/0+0(0)]  Move :[50/50+0(0)]",
        "Saving throws -3,",
        "AFF:  POISON.",
        "RES: <NONE> VUL: <NONE>",
        "SPL: (  2hr) anger                 ",
        "",
    ]
)
# The text the fake session's expect returns.
EXPECTED_TEXT = "Class levels: Mag:30 Cle:15\n\rRES: <NONE> VUL: <NONE>"
# The text the fake session's drain returns.
DRAINED_TEXT = "\n\rS:Surging> "


def answering_session() -> RecordingSession:
    """Return a fake session whose first expect answers EXPECTED_TEXT.

    Its drains answer DRAINED_TEXT.
    """
    return RecordingSession([EXPECTED_TEXT], DRAINED_TEXT)


def test_read_stat_sends_stat_waits_for_the_character_stats_last_line_and_settles_one_second() -> None:
    session = answering_session()
    text = read_stat(session, "player testpupil")
    assert text == EXPECTED_TEXT + DRAINED_TEXT
    assert session.calls == [("send_line", "stat player testpupil"), ("expect", ["VUL:"], {}), ("drain", 1.0)]


def test_read_stat_waits_for_a_given_last_line_or_another_outcome_and_settles_as_given() -> None:
    session = answering_session()
    read_stat(
        session, "obj sword", last_line=OBJECT_STAT_LAST_LINE, other_outcomes=["No such object around."], settle=0.5
    )
    assert session.calls == [
        ("send_line", "stat obj sword"),
        ("expect", ["Affections:", "No such object around."], {}),
        ("drain", 0.5),
    ]


def test_read_stat_lets_a_timeout_through() -> None:
    session = RecordingSession()
    with pytest.raises(SessionTimeout):
        read_stat(session, "mob boar")
    assert [call[0] for call in session.calls] == ["send_line", "expect"]


@pytest.mark.parametrize(
    "marker, format_text",
    [
        (CHARACTER_STAT_LAST_LINE, '"VUL: %s%s%s\\n\\r"'),
        (OBJECT_STAT_LAST_LINE, '"\\n\\rAffections:"'),
        (OBJECT_STAT_LAST_LINE, '"Affections: None.\\n\\r"'),
    ],
)
def test_the_last_line_markers_start_the_games_format_strings(marker: str, format_text: str) -> None:
    source = ACT_WIZ_SOURCE.read_text(encoding="latin-1")
    assert format_text in source, f"no {format_text} in {ACT_WIZ_SOURCE}"
    assert marker in format_text


def test_the_no_such_mob_reply_is_what_stat_mob_sends() -> None:
    source = ACT_WIZ_SOURCE.read_text(encoding="latin-1")
    assert f'"{NO_SUCH_MOB_REPLY}\\n\\r"' in source, f"no {NO_SUCH_MOB_REPLY} in {ACT_WIZ_SOURCE}"


@pytest.mark.parametrize(
    "pattern, expected",
    [
        (HIT_POINTS_PATTERN, 213),
        (HIT_POINTS_BEFORE_GAIN_PATTERN, 213),
        (HIT_POINTS_UNSIGNED_MAXIMUM_PATTERN, 213),
        (CONSTITUTION_PATTERN, 13),
        (STRENGTH_PATTERN, 14),
        (LEARNING_PATTERN, 15),
        (LEVEL_PATTERN, 30),
        (PRACTICE_SESSIONS_PATTERN, 190),
        (MAGE_LEVEL_PATTERN, 30),
        (SAVING_THROW_PATTERN, 0),
    ],
)
def test_each_number_pattern_reads_its_first_number_from_a_character_stat(pattern: re.Pattern, expected: int) -> None:
    assert first_number(pattern, CHARACTER_STAT_TEXT) == expected


def test_the_three_number_patterns_read_current_base_and_constant() -> None:
    strength = STRENGTH_PATTERN.search(CHARACTER_STAT_TEXT)
    learning = LEARNING_PATTERN.search(CHARACTER_STAT_TEXT)
    assert strength is not None and learning is not None
    assert strength.groups() == ("14", "14", "12")
    assert learning.groups() == ("15", "15", "15")


def test_the_hit_point_patterns_differ_only_on_a_negative_maximum_and_the_gain() -> None:
    full = HIT_POINTS_PATTERN.search(DYING_MOB_STAT_TEXT)
    before_gain = HIT_POINTS_BEFORE_GAIN_PATTERN.search(DYING_MOB_STAT_TEXT)
    assert full is not None and before_gain is not None
    assert full.groups() == ("-52", "-52") == before_gain.groups()
    assert HIT_POINTS_UNSIGNED_MAXIMUM_PATTERN.search(DYING_MOB_STAT_TEXT) is None
    assert HIT_POINTS_PATTERN.search("HP :[5/9]") is not None
    assert HIT_POINTS_BEFORE_GAIN_PATTERN.search("HP :[5/9]") is None


def test_the_affect_bits_patterns_read_the_aff_line_and_the_anchored_one_only_at_a_line_start() -> None:
    bits = AFFECT_BITS_PATTERN.search(CHARACTER_STAT_TEXT)
    anchored_bits = AFFECT_BITS_AT_LINE_START_PATTERN.search(DYING_MOB_STAT_TEXT)
    assert bits is not None and anchored_bits is not None
    assert bits.group(1).strip() == "<NONE>"
    assert anchored_bits.group(1).strip() == "POISON."
    assert AFFECT_BITS_PATTERN.search("NOT AFF: X") is not None
    assert AFFECT_BITS_AT_LINE_START_PATTERN.search("NOT AFF: X") is None


def test_the_saving_throw_pattern_reads_a_negative_saving_throw() -> None:
    assert first_number(SAVING_THROW_PATTERN, DYING_MOB_STAT_TEXT) == -3


def test_the_anger_pattern_finds_only_an_anger_affect_line() -> None:
    assert ANGER_AFFECT_PATTERN.search(DYING_MOB_STAT_TEXT) is not None
    assert ANGER_AFFECT_PATTERN.search(CHARACTER_STAT_TEXT) is None


def test_affect_bits_reads_the_aff_lines_names_without_their_full_stops() -> None:
    assert affect_bits(DYING_MOB_STAT_TEXT) == ["POISON"]
    assert affect_bits(CHARACTER_STAT_TEXT) == ["<NONE>"]
    assert affect_bits(" AFF: CHARM. TWO-HANDED.\n\r") == ["CHARM", "TWO-HANDED"]


def test_affect_bits_fails_without_an_aff_line_at_a_line_start() -> None:
    with pytest.raises(AssertionError):
        affect_bits("NOT AFF: CHARM.")


def test_read_stat_takes_settle_only_by_keyword() -> None:
    session = answering_session()
    with pytest.raises(TypeError):
        read_stat(session, "mob boar", CHARACTER_STAT_LAST_LINE, (), 0.5)  # type: ignore[misc]
