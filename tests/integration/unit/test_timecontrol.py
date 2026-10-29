from __future__ import annotations

import io
import os
import threading
import time
from pathlib import Path
from typing import BinaryIO, Iterator, Tuple

import pytest

from rots_harness.session import SessionTimeout
from rots_harness.timecontrol import (
    DEFAULT_HOUR_TIMEOUT_SECONDS,
    OUTPUT_LOG_NAME,
    REPLY_PREFIX,
    TimeControl,
    TimeControlError,
    parse_hour_timeout,
    schedule_name_from_marker,
)

# Long enough for a reader thread to pass a line on; short enough to keep timeout tests quick.
SHORT_TIMEOUT = 0.3
# Generous enough that a slow machine never fails a test that expects a reply.
REPLY_TIMEOUT = 5.0


class FakeServer:
    """The server's side of two pipes: reads the requests TimeControl writes, writes the lines it reads."""

    def __init__(self) -> None:
        request_read, request_write = os.pipe()
        reply_read, reply_write = os.pipe()
        # What the server reads: the requests TimeControl sends.
        self.requests_in: BinaryIO = os.fdopen(request_read, "rb", buffering=0)
        # What TimeControl writes requests to.
        self.requests_out: BinaryIO = os.fdopen(request_write, "wb", buffering=0)
        # What TimeControl reads replies from.
        self.replies_in: BinaryIO = os.fdopen(reply_read, "rb", buffering=0)
        # What the server writes its output to.
        self.replies_out: BinaryIO = os.fdopen(reply_write, "wb", buffering=0)

    def send(self, line: str) -> None:
        """Writes one output line, as the server's stdout would."""
        self.replies_out.write(line.encode("latin-1") + b"\n")

    def reply(self, text: str) -> None:
        """Writes one prefixed reply line."""
        self.send(REPLY_PREFIX + text)

    def read_request(self) -> str:
        """Returns the next request line TimeControl wrote, without its newline."""
        received = b""
        while not received.endswith(b"\n"):
            chunk = self.requests_in.read(1)
            if not chunk:
                break
            received += chunk
        return received.decode("ascii").rstrip("\n")

    def end_output(self) -> None:
        """Closes the server's stdout, as when the process exits."""
        self.replies_out.close()


@pytest.fixture
def fake_server_and_control(tmp_path: Path) -> Iterator[Tuple[FakeServer, TimeControl, Path]]:
    server = FakeServer()
    output_log = tmp_path / OUTPUT_LOG_NAME
    control = TimeControl(server.requests_out, server.replies_in, output_log)
    yield server, control, output_log
    if not server.replies_out.closed:
        server.end_output()
    control.close()
    for stream in (server.requests_in, server.requests_out, server.replies_in):
        stream.close()


class FailingReplies(io.RawIOBase):
    """A server stdout whose every read fails, as a broken pipe or closed descriptor would."""

    def readable(self) -> bool:
        return True

    def readline(self, size: int = -1) -> bytes:
        raise OSError("simulated read failure")


def answer_next_request(server: FakeServer, reply_text: str) -> threading.Thread:
    """Starts a thread that reads one request and answers it with reply_text."""
    def answer() -> None:
        server.read_request()
        server.reply(reply_text)

    answerer = threading.Thread(target=answer, daemon=True)
    answerer.start()
    return answerer


def test_wait_ready_accepts_the_ready_line_for_its_schedule(fake_server_and_control) -> None:
    server, control, _output_log = fake_server_and_control
    server.reply("ready schedule=affect-hours hours=0")
    control.wait_ready("affect-hours", REPLY_TIMEOUT)


def test_wait_ready_refuses_a_ready_line_for_another_schedule(fake_server_and_control) -> None:
    server, control, _output_log = fake_server_and_control
    server.reply("ready schedule=other hours=0")
    with pytest.raises(TimeControlError, match="ready schedule=other"):
        control.wait_ready("affect-hours", REPLY_TIMEOUT)


def test_wait_ready_times_out_when_no_ready_line_arrives(fake_server_and_control) -> None:
    _server, control, _output_log = fake_server_and_control
    with pytest.raises(SessionTimeout, match="ready"):
        control.wait_ready("affect-hours", SHORT_TIMEOUT)


def test_advance_hours_sends_the_request_and_returns_on_its_acknowledgement(fake_server_and_control) -> None:
    server, control, _output_log = fake_server_and_control
    requests_seen: list = []

    def answer() -> None:
        requests_seen.append(server.read_request())
        server.reply("done advance-hours 3 hours=3")

    answerer = threading.Thread(target=answer, daemon=True)
    answerer.start()
    control.advance_hours(3, REPLY_TIMEOUT)
    answerer.join(REPLY_TIMEOUT)
    assert requests_seen == ["advance-hours 3"]


def test_advance_hours_recognises_an_acknowledgement_after_a_stray_carriage_return(fake_server_and_control) -> None:
    server, control, _output_log = fake_server_and_control

    def answer() -> None:
        server.read_request()
        # A legacy line ending in "\n\r" leaves its carriage return at the start of the next line.
        server.send("\r" + REPLY_PREFIX + "done advance-hours 1 hours=1")

    answerer = threading.Thread(target=answer, daemon=True)
    answerer.start()
    control.advance_hours(1, REPLY_TIMEOUT)
    answerer.join(REPLY_TIMEOUT)


def test_advance_hours_raises_with_the_servers_words_on_an_error_reply(fake_server_and_control) -> None:
    server, control, _output_log = fake_server_and_control
    answer_next_request(server, "error advance-hours 3: at most 1000 hours may be pending")
    with pytest.raises(TimeControlError, match="at most 1000 hours may be pending"):
        control.advance_hours(3, REPLY_TIMEOUT)


def test_advance_hours_raises_on_an_acknowledgement_of_another_request(fake_server_and_control) -> None:
    server, control, _output_log = fake_server_and_control
    answer_next_request(server, "done advance-hours 2 hours=2")
    with pytest.raises(TimeControlError, match="done advance-hours 2"):
        control.advance_hours(3, REPLY_TIMEOUT)


def test_advance_hours_times_out_when_no_acknowledgement_arrives(fake_server_and_control) -> None:
    _server, control, _output_log = fake_server_and_control
    with pytest.raises(SessionTimeout, match="advance-hours 1"):
        control.advance_hours(1, SHORT_TIMEOUT)


def test_advance_hours_raises_when_the_servers_output_ends_first(fake_server_and_control) -> None:
    server, control, _output_log = fake_server_and_control
    server.end_output()
    with pytest.raises(TimeControlError, match="output ended"):
        control.advance_hours(1, REPLY_TIMEOUT)


def test_advance_hours_refuses_fewer_than_one_hour(fake_server_and_control) -> None:
    _server, control, _output_log = fake_server_and_control
    with pytest.raises(ValueError):
        control.advance_hours(0, REPLY_TIMEOUT)


def test_ordinary_output_goes_to_the_output_log_and_is_never_a_reply(fake_server_and_control) -> None:
    server, control, output_log = fake_server_and_control
    server.send("a legacy printf line")
    server.reply("ready schedule=affect-hours hours=0")
    control.wait_ready("affect-hours", REPLY_TIMEOUT)
    server.end_output()
    control.close()
    assert output_log.read_text(encoding="latin-1") == "a legacy printf line\n"


def test_keeps_reading_ordinary_output_while_no_request_is_awaited(fake_server_and_control) -> None:
    server, control, output_log = fake_server_and_control
    # More than a pipe buffer holds, so the writes below block unless something reads them.
    filler_lines = 2000

    def write_filler() -> None:
        for _line in range(filler_lines):
            server.send("x" * 80)

    writer = threading.Thread(target=write_filler, daemon=True)
    writer.start()
    writer.join(REPLY_TIMEOUT)
    assert not writer.is_alive(), "the reader stopped draining ordinary output"
    server.reply("ready schedule=affect-hours hours=0")
    control.wait_ready("affect-hours", REPLY_TIMEOUT)
    server.end_output()
    control.close()
    assert output_log.read_text(encoding="latin-1").count("\n") == filler_lines


def test_schedule_name_from_marker_takes_one_non_empty_name() -> None:
    assert schedule_name_from_marker(("affect-hours",)) == "affect-hours"
    for bad_args in ((), ("",), ("a", "b"), (3,)):
        with pytest.raises(ValueError):
            schedule_name_from_marker(bad_args)


def test_a_failing_read_raises_promptly_with_the_failure(tmp_path: Path) -> None:
    control = TimeControl(io.BytesIO(), FailingReplies(), tmp_path / OUTPUT_LOG_NAME)
    started = time.monotonic()
    with pytest.raises(TimeControlError, match="simulated read failure"):
        control.wait_ready("affect-hours", REPLY_TIMEOUT)
    assert time.monotonic() - started < REPLY_TIMEOUT / 2, "the failure waited out the timeout"
    control.close()


def test_an_output_log_that_cannot_be_opened_raises_from_the_constructor(tmp_path: Path) -> None:
    missing_directory_log = tmp_path / "missing" / OUTPUT_LOG_NAME
    with pytest.raises(OSError):
        TimeControl(io.BytesIO(), io.BytesIO(), missing_directory_log)


def test_hour_timeout_defaults_to_ten_seconds() -> None:
    assert parse_hour_timeout(None) == DEFAULT_HOUR_TIMEOUT_SECONDS == 10.0


def test_hour_timeout_reads_a_positive_number() -> None:
    assert parse_hour_timeout("25.5") == 25.5


@pytest.mark.parametrize("value", ["0", "-1", "nan", "inf", "soon"])
def test_hour_timeout_refuses_a_value_that_is_not_a_positive_finite_number(value: str) -> None:
    with pytest.raises(ValueError):
        parse_hour_timeout(value)
