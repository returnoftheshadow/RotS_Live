"""Unit tests for rots_harness.casting, against a fake session that answers each cast in turn."""

from __future__ import annotations

from pathlib import Path

import pytest

from rots_harness.casting import CAST_TIMEOUT_SECONDS, LOST_CONCENTRATION_REPLY, NO_ENERGY_REPLY, cast_until_it_lands
from rots_harness.session import SessionTimeout
from unit.fakes import RecordingSession

# The repository root, where the game's sources are.
REPOSITORY_ROOT = Path(__file__).resolve().parents[3]
# The outcome the fake caster's spell reports when it goes off.
LANDED = "You feel better."
# The cast line every test sends.
CAST_LINE = "cast 'cure self'"


def test_the_texts_are_the_games() -> None:
    spell_source = (REPOSITORY_ROOT / "src" / "spell_pa.cpp").read_text(encoding="latin-1")
    assert LOST_CONCENTRATION_REPLY in spell_source
    assert NO_ENERGY_REPLY in spell_source


def test_a_cast_that_lands_at_once_is_sent_once_with_the_default_timeout_and_settle() -> None:
    caster = RecordingSession([LANDED])
    assert cast_until_it_lands(caster, CAST_LINE, [LANDED, NO_ENERGY_REPLY]) == LANDED
    assert caster.calls == [
        ("send_line", CAST_LINE),
        ("expect", [LOST_CONCENTRATION_REPLY, LANDED, NO_ENERGY_REPLY], {"timeout": CAST_TIMEOUT_SECONDS}),
        ("drain", 1.0),
    ]
    assert CAST_TIMEOUT_SECONDS == 15.0


def test_a_lost_concentration_is_cast_again_until_it_lands() -> None:
    caster = RecordingSession([LOST_CONCENTRATION_REPLY, LOST_CONCENTRATION_REPLY, LANDED])
    assert cast_until_it_lands(caster, CAST_LINE, [LANDED], timeout=20.0, settle=0.5) == LANDED
    sent = [call[1] for call in caster.calls if call[0] == "send_line"]
    assert sent == [CAST_LINE] * 3
    assert ("expect", [LOST_CONCENTRATION_REPLY, LANDED], {"timeout": 20.0}) in caster.calls
    assert ("drain", 0.5) in caster.calls


def test_a_refusal_among_the_outcomes_is_returned_for_the_caller_to_check() -> None:
    caster = RecordingSession([LOST_CONCENTRATION_REPLY, NO_ENERGY_REPLY])
    assert cast_until_it_lands(caster, CAST_LINE, [LANDED, NO_ENERGY_REPLY]) == NO_ENERGY_REPLY


def test_timeout_and_settle_are_taken_only_by_keyword() -> None:
    caster = RecordingSession([LANDED])
    with pytest.raises(TypeError):
        cast_until_it_lands(caster, CAST_LINE, [LANDED], 20.0)  # type: ignore[misc]
    assert caster.calls == []


def test_a_timeout_passes_through() -> None:
    caster = RecordingSession([LOST_CONCENTRATION_REPLY])
    with pytest.raises(SessionTimeout):
        cast_until_it_lands(caster, CAST_LINE, [LANDED])
