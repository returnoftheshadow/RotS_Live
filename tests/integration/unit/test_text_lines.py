"""Unit tests for rots_harness.text_lines."""

from __future__ import annotations

import re

import pytest

from rots_harness.session import Transcript
from rots_harness.text_lines import (
    assert_in_order,
    assert_lines,
    capitalised,
    first_number,
    lines_after,
    raw_lines,
    reply_text,
    stripped_lines,
)

# A `look in corpse` reply: the header line ends in a space, two objects, a blank line, a prompt.
CORPSE_REPLY = "corpse (here) : \r\n  a gold amulet\r\n\r\n  a leather cap  \r\nHP:Healthy > "
# A `look <mob>` reply with an equipment section followed by an inventory section.
LOOK_REPLY = (
    "\n\rA squire is using:\n\r<worn on head> a sallet\n\r\n\r"
    "You attempt to peek at the inventory:\n\ran acorn\n\rHP:Healthy > "
)


def test_raw_lines_drop_carriage_returns_and_keep_blanks_and_spacing() -> None:
    assert raw_lines("one\r\n\r\n  two \r\n") == ["one", "", "  two ", ""]


def test_stripped_lines_strip_and_drop_blanks() -> None:
    assert stripped_lines("one\r\n\r\n  two \r\n   \n") == ["one", "two"]


def test_reply_text_removes_carriage_returns() -> None:
    assert reply_text(Transcript("one\r\ntwo\r\n")) == "one\ntwo\n"


def test_assert_lines_finds_consecutive_lines() -> None:
    reply = Transcript("head\r\none\r\ntwo\r\n")
    assert_lines(reply, ("one", "two"))


def test_assert_lines_fails_when_the_lines_are_not_consecutive() -> None:
    with pytest.raises(AssertionError, match="expected:"):
        reply = Transcript("one\r\nbetween\r\ntwo\r\n")
        assert_lines(reply, ("one", "two"))


def test_lines_after_drops_the_header_line_blanks_and_prompts() -> None:
    assert lines_after(CORPSE_REPLY, "corpse (here) :") == ["a gold amulet", "a leather cap"]


def test_lines_after_drops_the_rest_of_the_header_line() -> None:
    assert lines_after("Header: trailing words\r\nnext\r\n", "Header:") == ["next"]


def test_lines_after_keeps_prompts_when_asked() -> None:
    assert lines_after(CORPSE_REPLY, "corpse (here) :", drop_prompts=False) == [
        "a gold amulet",
        "a leather cap",
        "HP:Healthy >",
    ]


def test_lines_after_stops_before_the_until_line() -> None:
    equipment = lines_after(LOOK_REPLY, "is using:", "You attempt to peek at the inventory:", drop_prompts=False)
    assert equipment == ["<worn on head> a sallet"]


def test_lines_after_reads_from_the_first_header() -> None:
    assert lines_after("H\none\nH\ntwo\n", "H") == ["one", "H", "two"]


def test_lines_after_with_nothing_following_is_empty() -> None:
    assert lines_after("You are carrying:\r\nHP:Healthy > ", "You are carrying:") == []


def test_lines_after_fails_without_the_header() -> None:
    with pytest.raises(AssertionError, match="no 'missing header'"):
        lines_after(CORPSE_REPLY, "missing header")


def test_lines_after_fails_without_a_given_until() -> None:
    with pytest.raises(AssertionError, match="no 'missing until' after"):
        lines_after(LOOK_REPLY, "is using:", "missing until")


def test_lines_after_fails_when_until_is_only_before_the_header() -> None:
    with pytest.raises(AssertionError, match="after"):
        lines_after("until\nheader\nline\n", "header", "until")


def test_assert_in_order_passes_lines_in_order() -> None:
    assert_in_order("first\nsecond\nthird\n", ["first", "second", "third"])


def test_assert_in_order_fails_lines_out_of_order() -> None:
    with pytest.raises(AssertionError, match="out of order"):
        assert_in_order("first\nsecond\n", ["second", "first"])


def test_assert_in_order_fails_a_line_seen_twice() -> None:
    with pytest.raises(AssertionError, match="once"):
        assert_in_order("first\nfirst\n", ["first"])


def test_assert_in_order_fails_a_missing_line() -> None:
    with pytest.raises(AssertionError, match="once"):
        assert_in_order("first\n", ["first", "second"])


def test_assert_in_order_fails_a_tie_where_one_line_is_a_prefix_of_the_next() -> None:
    with pytest.raises(AssertionError, match="out of order"):
        assert_in_order("Ok. done\n", ["Ok. done", "Ok."])


def test_first_number_reads_the_first_group() -> None:
    assert first_number(re.compile(r"HP :\[(-?\d+)/"), "HP :[-3/40+5(") == -3


def test_first_number_fails_naming_the_pattern() -> None:
    with pytest.raises(AssertionError, match="Con:"):
        constitution_pattern = re.compile(r"Con:\[(\d+)")
        first_number(constitution_pattern, "HP :[3/40]")


def test_capitalised_raises_only_the_first_letter() -> None:
    assert capitalised("an elm lackey of Bree") == "An elm lackey of Bree"
