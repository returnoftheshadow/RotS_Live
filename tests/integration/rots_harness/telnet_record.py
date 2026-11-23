"""Record a session's telnet events in order, log each one, and let waits claim received ones."""

from __future__ import annotations

from dataclasses import dataclass
from enum import Enum
from collections.abc import Callable
from pathlib import Path

from rots_harness.telnet import TelnetEvent, TelnetText, describe_event


class TelnetDirection(Enum):
    """Whether the session read an event from the server or wrote it to the server."""

    # Read from the server.
    RECEIVED = "received"
    # Written to the server.
    SENT = "sent"


@dataclass(frozen=True)
class RecordedTelnetEvent:
    """One event with its place in the session's record and its direction."""

    # Position in the record, counting from 1.
    sequence: int
    # Whether the event was read or written.
    direction: TelnetDirection
    # The event itself.
    event: TelnetEvent


class TelnetRecord:
    """One session's telnet events, in order and with their direction.

    Each event is mirrored to the session's telnet log as it is added, and each matching event is
    handed to at most one wait.
    """

    def __init__(self, log_path: Path) -> None:
        # The telnet log; created by the first add() and appended to by each one.
        self._log_path = log_path
        # Every event added, oldest first.
        self._events: list[RecordedTelnetEvent] = []
        # Sequence numbers of the events a wait has claimed.
        self._claimed: set[int] = set()

    def add(self, direction: TelnetDirection, event: TelnetEvent) -> RecordedTelnetEvent:
        """Record an event, append its log line at once and return the recorded event.

        The line is written straight away so a crashed run keeps the log up to the crash.
        """
        sequence = len(self._events) + 1
        recorded = RecordedTelnetEvent(sequence, direction, event)
        self._events.append(recorded)
        description = describe_event(event)
        with self._log_path.open("a", encoding="utf-8", newline="") as log_file:
            log_file.write(f"{recorded.sequence:06d} {direction.value:<8} {description}\n")

        return recorded

    @property
    def events(self) -> tuple[RecordedTelnetEvent, ...]:
        """Return every event added, oldest first."""
        return tuple(self._events)

    def claim_first(self, matches: Callable[[RecordedTelnetEvent], bool]) -> RecordedTelnetEvent | None:
        """Claim and return the oldest received, unclaimed event that matches; None if none does."""
        for recorded in self._events:
            if recorded.direction is not TelnetDirection.RECEIVED or recorded.sequence in self._claimed:
                continue

            if matches(recorded):
                self._claimed.add(recorded.sequence)
                return recorded

        return None

    def unclaimed_received(self) -> list[RecordedTelnetEvent]:
        """Return the received events other than text that no wait has claimed, oldest first.

        Text is left out because no wait claims it, and it would crowd out the negotiation a
        timeout message is meant to show.
        """
        return [
            recorded
            for recorded in self._events
            if recorded.direction is TelnetDirection.RECEIVED
            and recorded.sequence not in self._claimed
            and not isinstance(recorded.event, TelnetText)
        ]
