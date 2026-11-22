"""Unit tests for rots_harness.replies, against a fake session that records its calls."""

from __future__ import annotations

import pytest

from rots_harness.replies import SETTLE_SECONDS, reply_to, settled_expect, text_seen_before_a_say
from rots_harness.session import SessionTimeout
from unit.fakes import RecordingSession

# The text the fake session's expect returns.
EXPECTED_TEXT = "You are carrying:\r\na leather cap\r\n"
# The text the fake session's drain returns.
DRAINED_TEXT = "HP:Healthy > "


def answering_session() -> RecordingSession:
    """Return a fake session whose first expect answers EXPECTED_TEXT and whose drains answer DRAINED_TEXT."""
    return RecordingSession([EXPECTED_TEXT], DRAINED_TEXT)


def test_reply_to_sends_waits_with_expects_default_and_settles_one_second() -> None:
    session = answering_session()
    text = reply_to(session, "inventory", ["You are carrying:"])
    assert text == EXPECTED_TEXT + DRAINED_TEXT
    assert session.calls == [("send_line", "inventory"), ("expect", ["You are carrying:"], {}), ("drain", 1.0)]
    assert SETTLE_SECONDS == 1.0


def test_reply_to_passes_a_given_timeout_and_settle() -> None:
    session = answering_session()
    reply_to(session, "cast 'blink'", ["You blink."], settle=0.5, timeout=15.0)
    assert session.calls == [("send_line", "cast 'blink'"), ("expect", ["You blink."], {"timeout": 15.0}), ("drain", 0.5)]


def test_reply_to_with_settle_zero_returns_at_the_outcome() -> None:
    session = answering_session()
    text = reply_to(session, "save", ["Saving"], settle=0)
    assert text == EXPECTED_TEXT
    assert [call[0] for call in session.calls] == ["send_line", "expect"]


def test_reply_to_lets_a_timeout_through_and_does_not_settle() -> None:
    session = RecordingSession()
    with pytest.raises(SessionTimeout):
        reply_to(session, "look", ["Arena Centre"])
    assert [call[0] for call in session.calls] == ["send_line", "expect"]


def test_reply_to_does_not_discard_unread_text_first() -> None:
    session = answering_session()
    reply_to(session, "look", ["Arena Centre"])
    assert session.calls[0] == ("send_line", "look")


def test_reply_to_takes_settle_and_timeout_only_by_keyword() -> None:
    session = answering_session()
    with pytest.raises(TypeError):
        reply_to(session, "look", ["Arena Centre"], 0.5)  # type: ignore[misc]
    assert session.calls == []


def test_settled_expect_sends_nothing_waits_with_expects_default_and_settles_one_second() -> None:
    session = answering_session()
    text = settled_expect(session, ["You are carrying:"])
    assert text == EXPECTED_TEXT + DRAINED_TEXT
    assert session.calls == [("expect", ["You are carrying:"], {}), ("drain", 1.0)]


def test_settled_expect_passes_a_given_timeout_and_settle() -> None:
    session = answering_session()
    settled_expect(session, ["You blink."], settle=0.5, timeout=15.0)
    assert session.calls == [("expect", ["You blink."], {"timeout": 15.0}), ("drain", 0.5)]


def test_settled_expect_with_settle_zero_returns_at_the_outcome() -> None:
    session = answering_session()
    assert settled_expect(session, ["Saving"], settle=0) == EXPECTED_TEXT
    assert [call[0] for call in session.calls] == ["expect"]


def test_settled_expect_lets_a_timeout_through_and_does_not_settle() -> None:
    session = RecordingSession()
    with pytest.raises(SessionTimeout):
        settled_expect(session, ["Arena Centre"])
    assert [call[0] for call in session.calls] == ["expect"]


def test_text_seen_before_a_say_says_done_and_reads_to_the_say_without_carriage_returns() -> None:
    actor = RecordingSession()
    observer = RecordingSession(["Testbanker deposits coins.\r\nTestbanker says 'done'\r\n"])
    text = text_seen_before_a_say(actor, "Testbanker", observer)
    assert text == "Testbanker deposits coins.\nTestbanker says 'done'\n"
    assert actor.calls == [("command", "say done")]
    assert observer.calls == [("expect", ["Testbanker says '"], {})]
