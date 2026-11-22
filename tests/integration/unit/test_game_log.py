"""Unit tests for rots_harness.game_log, against log files written to a temporary folder."""

from __future__ import annotations

import threading
from pathlib import Path

import pytest

from rots_harness.game_log import (
    FAILURE_TAIL_CHARACTERS,
    LOG_POLL_SECONDS,
    LOG_TIMEOUT_SECONDS,
    count_in_log,
    count_logged,
    log_lines_holding,
    logged_messages,
    read_game_log,
    wait_for_log_count,
)

# A boot line without mudlog's separator, then two mudlog lines with the same message.
SAMPLE_LOG = (
    "Booting the world.\nSYSERR, 12:00 :: Script #1719 tried to divide by 0.\n"
    "INFO, 12:01 :: Script #1719 tried to divide by 0.\n"
)
# The message both mudlog lines of SAMPLE_LOG carry.
SAMPLE_MESSAGE = "Script #1719 tried to divide by 0."
# A wait short enough for a test that expects to time out.
SHORT_TIMEOUT_SECONDS = 0.05
# A poll short enough to keep the waiting tests fast.
SHORT_POLL_SECONDS = 0.01


def write_log(log_path: Path, text: str) -> None:
    """Write text to log_path as latin-1 bytes, as the server's log holds them."""
    log_bytes = text.encode("latin-1")
    log_path.write_bytes(log_bytes)


def test_the_defaults_are_the_ten_seconds_and_fifth_of_a_second_most_files_use() -> None:
    assert LOG_TIMEOUT_SECONDS == 10.0
    assert LOG_POLL_SECONDS == 0.2


def test_read_game_log_is_empty_before_the_file_exists(tmp_path: Path) -> None:
    assert read_game_log(tmp_path / "game.log") == ""


def test_read_game_log_decodes_every_byte(tmp_path: Path) -> None:
    log_path = tmp_path / "game.log"
    log_path.write_bytes(b"caf\xe9 \xff\n")
    assert read_game_log(log_path) == "caf\u00e9 \u00ff\n"


def test_count_in_log_counts_the_line_anywhere(tmp_path: Path) -> None:
    log_path = tmp_path / "game.log"
    write_log(log_path, SAMPLE_LOG)
    assert count_in_log(log_path, SAMPLE_MESSAGE) == 2
    assert count_in_log(tmp_path / "missing.log", SAMPLE_MESSAGE) == 0


def test_log_lines_holding_keeps_whole_lines_in_order(tmp_path: Path) -> None:
    log_path = tmp_path / "game.log"
    write_log(log_path, SAMPLE_LOG)
    assert log_lines_holding(log_path, "12:0") == SAMPLE_LOG.splitlines()[1:]
    assert log_lines_holding(log_path, "absent") == []


def test_wait_for_log_count_returns_the_log_once_it_holds_the_count(tmp_path: Path) -> None:
    log_path = tmp_path / "game.log"
    write_log(log_path, SAMPLE_LOG)
    assert (
        wait_for_log_count(log_path, SAMPLE_MESSAGE, 1, timeout=SHORT_TIMEOUT_SECONDS, poll=SHORT_POLL_SECONDS)
        == SAMPLE_LOG
    )


def test_wait_for_log_count_waits_for_a_line_that_arrives_later(tmp_path: Path) -> None:
    log_path = tmp_path / "game.log"
    writer = threading.Timer(0.1, write_log, (log_path, SAMPLE_LOG))
    writer.start()
    try:
        text = wait_for_log_count(log_path, SAMPLE_MESSAGE, 2, timeout=5.0, poll=SHORT_POLL_SECONDS)
    finally:
        writer.join()
    assert text == SAMPLE_LOG


def test_wait_for_log_count_fails_naming_the_count_and_quoting_the_tail(tmp_path: Path) -> None:
    log_path = tmp_path / "game.log"
    long_log = "x" * (FAILURE_TAIL_CHARACTERS + 10) + "TAIL"
    write_log(log_path, long_log)
    with pytest.raises(AssertionError) as failure:
        wait_for_log_count(log_path, SAMPLE_MESSAGE, 1, timeout=SHORT_TIMEOUT_SECONDS, poll=SHORT_POLL_SECONDS)
    message = str(failure.value)
    assert f"game.log has 0 of 1 {SAMPLE_MESSAGE!r} within {SHORT_TIMEOUT_SECONDS}s:\n" in message
    assert message.endswith(long_log[-FAILURE_TAIL_CHARACTERS:])


def test_wait_for_log_count_ends_a_failure_with_describe(tmp_path: Path) -> None:
    log_path = tmp_path / "game.log"
    write_log(log_path, SAMPLE_LOG)
    with pytest.raises(AssertionError) as failure:
        wait_for_log_count(
            log_path,
            SAMPLE_MESSAGE,
            3,
            timeout=SHORT_TIMEOUT_SECONDS,
            poll=SHORT_POLL_SECONDS,
            describe=lambda text: f"{len(text)} characters",
        )
    assert str(failure.value).endswith(f"\n{len(SAMPLE_LOG)} characters")


def test_wait_for_log_count_accepts_more_unless_exact(tmp_path: Path) -> None:
    log_path = tmp_path / "game.log"
    write_log(log_path, SAMPLE_LOG)
    assert (
        wait_for_log_count(log_path, SAMPLE_MESSAGE, 1, timeout=SHORT_TIMEOUT_SECONDS, poll=SHORT_POLL_SECONDS)
        == SAMPLE_LOG
    )
    with pytest.raises(AssertionError, match="game.log has 2 of 1"):
        wait_for_log_count(
            log_path, SAMPLE_MESSAGE, 1, exact=True, timeout=SHORT_TIMEOUT_SECONDS, poll=SHORT_POLL_SECONDS
        )
    assert (
        wait_for_log_count(
            log_path, SAMPLE_MESSAGE, 2, exact=True, timeout=SHORT_TIMEOUT_SECONDS, poll=SHORT_POLL_SECONDS
        )
        == SAMPLE_LOG
    )


def test_wait_for_log_count_fails_on_a_log_that_never_appears(tmp_path: Path) -> None:
    with pytest.raises(AssertionError, match="game.log has 0 of 1"):
        wait_for_log_count(
            tmp_path / "game.log", SAMPLE_MESSAGE, 1, timeout=SHORT_TIMEOUT_SECONDS, poll=SHORT_POLL_SECONDS
        )


def test_logged_messages_drop_the_type_and_time_and_skip_other_lines(tmp_path: Path) -> None:
    log_path = tmp_path / "game.log"
    write_log(log_path, SAMPLE_LOG + "A :: B :: C\n")
    assert logged_messages(log_path) == [SAMPLE_MESSAGE, SAMPLE_MESSAGE, "B :: C"]


def test_count_logged_returns_the_count_once_it_is_reached(tmp_path: Path) -> None:
    log_path = tmp_path / "game.log"
    write_log(log_path, SAMPLE_LOG)
    assert count_logged(log_path, SAMPLE_MESSAGE, 1, timeout=SHORT_TIMEOUT_SECONDS, poll=SHORT_POLL_SECONDS) == 2


def test_count_logged_returns_a_lower_count_after_the_timeout_without_failing(tmp_path: Path) -> None:
    log_path = tmp_path / "game.log"
    write_log(log_path, SAMPLE_LOG)
    assert count_logged(log_path, SAMPLE_MESSAGE, 3, timeout=SHORT_TIMEOUT_SECONDS, poll=SHORT_POLL_SECONDS) == 2
    assert count_logged(log_path, "Script #1719", 1, timeout=SHORT_TIMEOUT_SECONDS, poll=SHORT_POLL_SECONDS) == 0
