"""Drives a harness server's game time over its standard streams: one request line in, one prefixed reply line out."""

from __future__ import annotations

import math
import queue
import re
import threading
from pathlib import Path
from typing import BinaryIO, Optional, TextIO

from rots_harness.session import SessionTimeout

# The text that starts every reply line the harness binary writes; other stdout lines are ordinary output.
REPLY_PREFIX = "harness: "
# The run-directory file that receives the server's ordinary stdout lines.
OUTPUT_LOG_NAME = "stdout.log"
# Seconds advance_hours allows per game hour when ROTS_IT_HOUR_TIMEOUT is unset: about twice an
# hour's 20 passes at four passes a second, for the i386 emulator.
DEFAULT_HOUR_TIMEOUT_SECONDS = 10.0


class TimeControlError(AssertionError):
    """The server refused a time request, acknowledged a different one, or stopped writing; the message quotes what it sent."""


def schedule_name_from_marker(args: tuple) -> str:
    """The schedule name a @pytest.mark.schedule(...) marker gives; raises ValueError unless it is exactly one non-empty string."""
    if len(args) != 1 or not isinstance(args[0], str) or not args[0]:
        raise ValueError(f"@pytest.mark.schedule takes one schedule name, not {args!r}")
    return args[0]


def parse_hour_timeout(value: Optional[str]) -> float:
    """Seconds allowed per game hour that value gives, or the default for None; raises ValueError unless positive and finite."""
    if value is None:
        return DEFAULT_HOUR_TIMEOUT_SECONDS
    try:
        seconds = float(value)
    except ValueError:
        seconds = math.nan
    if not math.isfinite(seconds) or seconds <= 0:
        raise ValueError(f"{value!r} is not a positive, finite number of seconds")
    return seconds


class TimeControl:
    """Sends time requests to a harness server and waits for its acknowledgements.

    A reader thread takes every line the server writes to stdout: replies are queued for the
    waiting call, and every other line is appended to output_log. The thread keeps reading
    whether or not a call is waiting, so the server never blocks on a full pipe. If reading or
    logging fails, the thread stops and the waiting call raises TimeControlError naming the
    failure.
    """

    def __init__(self, requests: BinaryIO, replies: BinaryIO, output_log: Path) -> None:
        """Starts reading replies; requests is the server's stdin and replies its stdout.

        Raises OSError when output_log cannot be opened for appending.
        """
        # The server's stdin, where requests are written.
        self._requests = requests
        # Reply texts without the prefix, in arrival order; None once the server's output ends.
        self._replies: "queue.Queue[Optional[str]]" = queue.Queue()
        # What stopped the reader early; None while it reads or after a clean end of output.
        self._read_failure: Optional[BaseException] = None
        log_file = output_log.open("a", encoding="latin-1", newline="")
        # Reads the server's stdout until it ends; owns and closes log_file.
        self._reader = threading.Thread(
            target=self._read_output, args=(replies, log_file), daemon=True
        )
        self._reader.start()

    def _read_output(self, replies: BinaryIO, log_file: TextIO) -> None:
        """The reader thread: queues each reply's text and logs other lines until replies ends.

        Strips carriage returns and newlines from both ends of every line. Records a read or log
        failure for _next_reply, always queues the end marker last and closes log_file; never
        raises.
        """
        try:
            with log_file:
                for raw_line in iter(replies.readline, b""):
                    line = raw_line.decode("latin-1").strip("\r\n")
                    if line.startswith(REPLY_PREFIX):
                        self._replies.put(line[len(REPLY_PREFIX):])
                    else:
                        log_file.write(line + "\n")
                        log_file.flush()
        except BaseException as failure:
            # Recorded before the end marker is queued, so the call that takes the marker sees it.
            self._read_failure = failure
        finally:
            self._replies.put(None)

    def _next_reply(self, timeout: float, awaited: str) -> str:
        """The next reply's text without its prefix; awaited names it in error messages.

        Raises SessionTimeout when no reply arrives within timeout seconds, and TimeControlError
        when the server's output has ended or the reader failed before a reply.
        """
        try:
            reply = self._replies.get(timeout=timeout)
        except queue.Empty:
            raise SessionTimeout(f"no {awaited} from the server within {timeout}s") from None
        if reply is None:
            failure = self._read_failure
            if failure is not None:
                raise TimeControlError(
                    f"reading the server's output failed before {awaited}: {failure!r}"
                ) from failure
            raise TimeControlError(f"the server's output ended before {awaited}")
        return reply

    def wait_ready(self, schedule_name: str, timeout: float) -> None:
        """Waits for the server's ready line and checks it names schedule_name; raises TimeControlError or SessionTimeout."""
        reply = self._next_reply(timeout, "ready line")
        expected = f"ready schedule={schedule_name} hours=0"
        if reply != expected:
            raise TimeControlError(f"expected {expected!r}, the server sent {reply!r}")

    def advance_hours(self, hours: int, timeout: float) -> None:
        """Asks the server to run hours game hours and returns once it acknowledges them.

        Raises ValueError for fewer than 1 hour, TimeControlError on a refusal, a different
        acknowledgement or a closed pipe, and SessionTimeout when no reply arrives in time.
        """
        if hours < 1:
            raise ValueError(f"hours must be at least 1, not {hours}")
        request = f"advance-hours {hours}"
        try:
            self._requests.write((request + "\n").encode("ascii"))
            self._requests.flush()
        except (BrokenPipeError, ValueError) as closed:
            raise TimeControlError(f"the server's input is closed; {request!r} was not sent") from closed
        reply = self._next_reply(timeout, f"acknowledgement of {request!r}")
        if reply.startswith("error "):
            raise TimeControlError(f"the server refused {request!r}: {reply}")
        if not re.fullmatch(rf"done {re.escape(request)} hours=\d+", reply):
            raise TimeControlError(f"expected the acknowledgement of {request!r}, the server sent {reply!r}")

    def close(self, timeout: float = 5.0) -> None:
        """Waits up to timeout for the reader to finish, which it does once the server's stdout ends. Never raises."""
        self._reader.join(timeout)
