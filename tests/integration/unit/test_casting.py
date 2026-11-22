"""Unit tests for rots_harness.casting, against a fake session that answers each cast in turn."""

from __future__ import annotations

from pathlib import Path
from typing import Sequence

import pytest

from rots_harness.casting import CAST_TIMEOUT_SECONDS, LOST_CONCENTRATION, NO_ENERGY, cast_until_it_lands
from rots_harness.session import SessionTimeout

# The repository root, where the game's sources are.
REPOSITORY_ROOT = Path(__file__).resolve().parents[3]
# The outcome the fake caster's spell reports when it goes off.
LANDED = "You feel better."
# The cast line every test sends.
CAST_LINE = "cast 'cure self'"


class FakeCaster:
    """Stands in for a GameSession: answers each expect with the next of its replies, in order,
    and records each call."""

    def __init__(self, replies: Sequence[str]) -> None:
        """Answers the coming expects with replies; one past the last times out."""
        # The answers still to give, first first.
        self._replies = list(replies)
        # Each call as (method name, arguments), in order.
        self.calls: list[tuple] = []

    def send_line(self, line: str) -> None:
        """Records the line."""
        self.calls.append(("send_line", line))

    def expect(self, markers: Sequence[str], **keywords: float) -> str:
        """Records the markers and timeout, then gives the next reply or times out."""
        self.calls.append(("expect", list(markers), keywords))
        if not self._replies:
            raise SessionTimeout("no reply left")

        return self._replies.pop(0)

    def drain(self, timeout: float) -> str:
        """Records the settle; nothing more arrives."""
        self.calls.append(("drain", timeout))
        return ""


def test_the_texts_are_the_games() -> None:
    spell_source = (REPOSITORY_ROOT / "src" / "spell_pa.cpp").read_text(encoding="latin-1")
    assert LOST_CONCENTRATION in spell_source
    assert NO_ENERGY in spell_source


def test_a_cast_that_lands_at_once_is_sent_once_with_the_default_timeout_and_settle() -> None:
    caster = FakeCaster([LANDED])
    assert cast_until_it_lands(caster, CAST_LINE, [LANDED, NO_ENERGY]) == LANDED
    assert caster.calls == [
        ("send_line", CAST_LINE),
        ("expect", [LOST_CONCENTRATION, LANDED, NO_ENERGY], {"timeout": CAST_TIMEOUT_SECONDS}),
        ("drain", 1.0),
    ]
    assert CAST_TIMEOUT_SECONDS == 15.0


def test_a_lost_concentration_is_cast_again_until_it_lands() -> None:
    caster = FakeCaster([LOST_CONCENTRATION, LOST_CONCENTRATION, LANDED])
    assert cast_until_it_lands(caster, CAST_LINE, [LANDED], timeout=20.0, settle=0.5) == LANDED
    sent = [call[1] for call in caster.calls if call[0] == "send_line"]
    assert sent == [CAST_LINE] * 3
    assert ("expect", [LOST_CONCENTRATION, LANDED], {"timeout": 20.0}) in caster.calls
    assert ("drain", 0.5) in caster.calls


def test_a_refusal_among_the_outcomes_is_returned_for_the_caller_to_check() -> None:
    caster = FakeCaster([LOST_CONCENTRATION, NO_ENERGY])
    assert cast_until_it_lands(caster, CAST_LINE, [LANDED, NO_ENERGY]) == NO_ENERGY


def test_a_timeout_passes_through() -> None:
    caster = FakeCaster([LOST_CONCENTRATION])
    with pytest.raises(SessionTimeout):
        cast_until_it_lands(caster, CAST_LINE, [LANDED])
