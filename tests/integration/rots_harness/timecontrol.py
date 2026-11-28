"""Drives a harness server's game time over its standard streams.

One request line goes in, one prefixed reply line comes out.
"""

from __future__ import annotations

import math
import queue
import re
import threading
from pathlib import Path
from typing import BinaryIO, Optional, TextIO

from rots_harness.session import SessionTimeout

# The text that starts every reply line the harness binary writes; other stdout lines are ordinary
# output.
REPLY_PREFIX = "harness: "
# The run-directory file that receives the server's ordinary stdout lines.
OUTPUT_LOG_NAME = "stdout.log"
# Seconds advance_hours allows per game hour when ROTS_IT_HOUR_TIMEOUT is unset: about twice an
# hour's 20 passes at four passes a second, for the i386 emulator.
DEFAULT_HOUR_TIMEOUT_SECONDS = 10.0
# The schedule a scenario without @pytest.mark.schedule(...) or @pytest.mark.real_login runs: the
# live server's real-time world, which accepts no time requests.
REAL_TIME_SCHEDULE = "real-time"
# The schedules whose ready line reports the game hours run so far, which must be 0 at the start.
SCHEDULES_REPORTING_HOURS = frozenset({"affect-hours"})
# A ready line: the schedule's name, an optional hours report and the run's entry token.
READY_LINE_PATTERN = re.compile(r"ready schedule=(?P<schedule>\S+)(?P<hours> hours=0)? entry=(?P<token>[0-9a-f]{32})")


class TimeControlError(AssertionError):
    """The server refused a time request, acknowledged a different one, or stopped writing.

    The message quotes what it sent.
    """


def schedule_name_from_marker(args: tuple) -> str:
    """Return the schedule name a @pytest.mark.schedule(...) marker gives.

    Raise ValueError unless it is exactly one non-empty string.
    """
    if len(args) != 1 or not isinstance(args[0], str) or not args[0]:
        raise ValueError(f"@pytest.mark.schedule takes one schedule name, not {args!r}")
    return args[0]


def server_schedule(schedule_args: Optional[tuple], real_login: bool) -> Optional[str]:
    """Return the schedule a test's server runs, or None for plain bin/ageland.

    schedule_args are the test's @pytest.mark.schedule(...) arguments, None without the marker;
    real_login says whether it carries @pytest.mark.real_login. A schedule marker names the
    schedule, real_login alone asks for plain bin/ageland, and neither gives REAL_TIME_SCHEDULE.
    Raise ValueError for a malformed schedule marker or for both markers on one test.
    """
    if schedule_args is None:
        return None if real_login else REAL_TIME_SCHEDULE
    if real_login:
        raise ValueError("@pytest.mark.real_login runs plain bin/ageland, which takes no @pytest.mark.schedule(...)")
    return schedule_name_from_marker(schedule_args)


def entry_token_from_ready_line(reply: str, schedule_name: str) -> str:
    """Return the entry token in reply, the ready line for schedule_name.

    A schedule in SCHEDULES_REPORTING_HOURS must report hours=0. Raise TimeControlError when
    reply is not that line.
    """
    match = READY_LINE_PATTERN.fullmatch(reply)
    if match is None or match["schedule"] != schedule_name:
        raise TimeControlError(
            f"expected 'ready schedule={schedule_name} [hours=0] entry=<32 hex digits>', the server sent {reply!r}"
        )
    if schedule_name in SCHEDULES_REPORTING_HOURS and match["hours"] is None:
        raise TimeControlError(f"the ready line for {schedule_name!r} does not report hours=0: {reply!r}")
    return match["token"]


def parse_hour_timeout(value: Optional[str]) -> float:
    """Return the seconds allowed per game hour that value gives, or the default for None.

    Raises ValueError unless positive and finite.
    """
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
        """Start reading replies; requests is the server's stdin and replies its stdout.

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
        """Run the reader thread: queue each reply's text and log other lines until replies ends.

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
        """Return the next reply's text without its prefix; awaited names it in error messages.

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

    def wait_ready(self, schedule_name: str, timeout: float) -> str:
        """Wait for the server's ready line for schedule_name and return its entry token.

        Raise TimeControlError or SessionTimeout.
        """
        reply = self._next_reply(timeout, "ready line")
        return entry_token_from_ready_line(reply, schedule_name)

    def advance_hours(self, hours: int, timeout: float) -> None:
        """Ask the server to run hours game hours and return once it acknowledges them.

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
        """Wait up to timeout for the reader to finish, which it does once the server's stdout ends.

        Never raises.
        """
        self._reader.join(timeout)
