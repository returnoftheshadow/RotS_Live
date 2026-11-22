"""Unit tests for rots_harness.replies, against a fake session that records its calls."""

from __future__ import annotations

from typing import Sequence

import pytest

from rots_harness.characters import Character
from rots_harness.replies import SETTLE_SECONDS, reply_to, text_seen_before_a_say
from rots_harness.session import SessionTimeout, Transcript

# The text the fake session's expect returns.
EXPECTED_TEXT = "You are carrying:\r\na leather cap\r\n"
# The text the fake session's drain returns.
DRAINED_TEXT = "HP:Healthy > "


class FakeSession:
    """Stands in for a GameSession: records each call, in order, and answers with fixed text."""

    def __init__(self, expect_text: str = EXPECTED_TEXT, *, timeout_on_expect: bool = False) -> None:
        """Answers expect with expect_text, or raises SessionTimeout when timeout_on_expect."""
        # Each call as (method name, arguments), in the order they were made.
        self.calls: list[tuple] = []
        # What expect returns.
        self._expect_text = expect_text
        # Whether expect raises SessionTimeout instead of returning.
        self._timeout_on_expect = timeout_on_expect

    def send_line(self, line: str) -> None:
        """Records the line."""
        self.calls.append(("send_line", line))

    def expect(self, markers: Sequence[str], **keywords: float) -> str:
        """Records the markers and any keyword (timeout), then answers or times out."""
        self.calls.append(("expect", list(markers), keywords))
        if self._timeout_on_expect:
            raise SessionTimeout("none of the markers arrived")

        return self._expect_text

    def drain(self, timeout: float) -> str:
        """Records the settle time."""
        self.calls.append(("drain", timeout))
        return DRAINED_TEXT

    def command(self, text: str) -> Transcript:
        """Records the command."""
        self.calls.append(("command", text))
        return Transcript("Ok.\r\n")


def test_reply_to_sends_waits_with_expects_default_and_settles_one_second() -> None:
    session = FakeSession()
    text = reply_to(session, "inventory", ["You are carrying:"])
    assert text == EXPECTED_TEXT + DRAINED_TEXT
    assert session.calls == [("send_line", "inventory"), ("expect", ["You are carrying:"], {}), ("drain", 1.0)]
    assert SETTLE_SECONDS == 1.0


def test_reply_to_passes_a_given_timeout_and_settle() -> None:
    session = FakeSession()
    reply_to(session, "cast 'blink'", ["You blink."], settle=0.5, timeout=15.0)
    assert session.calls == [("send_line", "cast 'blink'"), ("expect", ["You blink."], {"timeout": 15.0}), ("drain", 0.5)]


def test_reply_to_with_settle_zero_returns_at_the_outcome() -> None:
    session = FakeSession()
    text = reply_to(session, "save", ["Saving"], settle=0)
    assert text == EXPECTED_TEXT
    assert [call[0] for call in session.calls] == ["send_line", "expect"]


def test_reply_to_lets_a_timeout_through_and_does_not_settle() -> None:
    session = FakeSession(timeout_on_expect=True)
    with pytest.raises(SessionTimeout):
        reply_to(session, "look", ["Arena Centre"])
    assert [call[0] for call in session.calls] == ["send_line", "expect"]


def test_reply_to_does_not_discard_unread_text_first() -> None:
    session = FakeSession()
    reply_to(session, "look", ["Arena Centre"])
    assert session.calls[0] == ("send_line", "look")


def test_text_seen_before_a_say_says_done_and_reads_to_the_say_without_carriage_returns() -> None:
    actor = FakeSession()
    observer = FakeSession(expect_text="Testbanker deposits coins.\r\nTestbanker says 'done'\r\n")
    actor_character = Character(name="Testbanker", race="human", sex="male", level=10, load_room=1101, class_name="warrior")
    text = text_seen_before_a_say(actor, actor_character, observer)
    assert text == "Testbanker deposits coins.\nTestbanker says 'done'\n"
    assert actor.calls == [("command", "say done")]
    assert observer.calls == [("expect", ["Testbanker says '"], {})]
