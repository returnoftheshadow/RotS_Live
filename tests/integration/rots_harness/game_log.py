"""Reads the test server's game.log while it runs.

The server writes a log line before it sends the reply that caused it, but under the Docker
launcher the log reaches game.log through compose, so a line can arrive after the reply: a test
that needs a line waits for it. The log is read as latin-1, which decodes every byte, so a
count of an ASCII line is the same whatever else the log holds. Callers pass the server's
`handle.log_path`.
"""

from __future__ import annotations

import time
from pathlib import Path
from typing import Callable

# Seconds wait_for_log_count and count_logged wait by default.
LOG_TIMEOUT_SECONDS = 10.0
# Seconds between two reads of game.log while waiting, by default.
LOG_POLL_SECONDS = 0.2
# How much of the log's end a failed wait quotes when the caller gives no describe.
FAILURE_TAIL_CHARACTERS = 2000
# What separates mudlog's '<type>, <time>' from its message on each line (mudlog, utility.cpp).
MESSAGE_SEPARATOR = " :: "


def read_game_log(log_path: Path) -> str:
    """What game.log holds so far, decoded as latin-1 so no byte fails; empty before it exists."""
    if not log_path.exists():
        return ""

    return log_path.read_text(encoding="latin-1")


def count_in_log(log_path: Path, line: str) -> int:
    """How many times game.log holds line so far, anywhere in its text; nothing is waited for."""
    return read_game_log(log_path).count(line)


def log_lines_holding(log_path: Path, marker: str) -> list[str]:
    """The lines of game.log so far that contain marker, in log order; nothing is waited for."""
    lines = read_game_log(log_path).splitlines()
    return [log_line for log_line in lines if marker in log_line]


def wait_for_log_count(
    log_path: Path,
    line: str,
    count: int,
    *,
    exact: bool = False,
    timeout: float = LOG_TIMEOUT_SECONDS,
    poll: float = LOG_POLL_SECONDS,
    describe: Callable[[str], str] | None = None,
) -> str:
    """Waits until game.log holds line at least count times and returns the log's text.

    Re-reads the log every poll seconds. Fails with AssertionError, naming the line and the
    count seen, when the log does not hold count within timeout; with exact, it also fails when
    the log holds more than count once it holds count. The failure ends with describe(log text)
    when describe is given, or with the log's last FAILURE_TAIL_CHARACTERS otherwise. Whether
    the line means what the test expects is left to the caller.
    """
    deadline = time.monotonic() + timeout
    while True:
        text = read_game_log(log_path)
        seen = text.count(line)
        timed_out = time.monotonic() >= deadline
        if seen >= count or timed_out:
            if exact:
                passed = seen == count
            else:
                passed = seen >= count

            if passed:
                return text

            if describe is None:
                detail = text[-FAILURE_TAIL_CHARACTERS:]
            else:
                detail = describe(text)

            raise AssertionError(f"game.log has {seen} of {count} {line!r} within {timeout}s:\n{detail}")

        time.sleep(poll)


def logged_messages(log_path: Path) -> list[str]:
    """The messages mudlog wrote to game.log so far, each line being '<type>, <time> :: <message>'.

    Lines without the separator, such as the boot's own output, are left out.
    """
    lines = read_game_log(log_path).splitlines()
    return [log_line.split(MESSAGE_SEPARATOR, 1)[1] for log_line in lines if MESSAGE_SEPARATOR in log_line]


def count_logged(
    log_path: Path,
    message: str,
    expected_count: int,
    *,
    timeout: float = LOG_TIMEOUT_SECONDS,
    poll: float = LOG_POLL_SECONDS,
) -> int:
    """How many of logged_messages equal message, once expected_count do or timeout passes.

    Nothing is asserted: the caller compares the count it gets, which may be below
    expected_count after a timeout or above it.
    """
    deadline = time.monotonic() + timeout
    while logged_messages(log_path).count(message) < expected_count and time.monotonic() < deadline:
        time.sleep(poll)

    return logged_messages(log_path).count(message)
