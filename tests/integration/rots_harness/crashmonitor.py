"""Reports crashes, unexpected SYSERR lines and expected SYSERR lines never logged, from a server's log and process state."""

from __future__ import annotations

from collections.abc import Iterable
from typing import Callable, Optional

from rots_harness.launcher import ServerHandle

# SYSERR fragments a harness boot is known to print; a clean boot prints none.
# Add an entry only with the reason it is harmless.
DEFAULT_ALLOWED: tuple[str, ...] = ()
# Lines that sanitizer runtimes write when they find an error.
SANITIZER_MARKERS = ("AddressSanitizer", "LeakSanitizer", "UndefinedBehaviorSanitizer", "runtime error:")
# sigsegv_handler() in comm.cpp prints this ahead of its backtrace.
SIGNAL_MARKER = "Error: signal"
# The game loop logs this when SIGHUP, SIGINT or SIGTERM stops the server; it is not a crash.
SHUTDOWN_SIGNAL_LINE = "Received SIGHUP, SIGINT, or SIGTERM"


def expected_syserr_fragments(marker_arguments: Iterable[tuple[object, ...]]) -> tuple[str, ...]:
    """The SYSERR fragments a test's @pytest.mark.expect_syserr(...) markers name, in order, each once.

    marker_arguments holds each marker's positional arguments. Raises ValueError for a marker with
    no fragment, a fragment that is not a non-empty string, or a fragment named twice.
    """
    fragments: list[str] = []
    for arguments in marker_arguments:
        if not arguments:
            raise ValueError("@pytest.mark.expect_syserr takes one or more SYSERR fragments, not none")
        for fragment in arguments:
            if not isinstance(fragment, str) or not fragment:
                raise ValueError(f"@pytest.mark.expect_syserr takes non-empty strings, not {fragment!r}")
            if fragment in fragments:
                raise ValueError(f"@pytest.mark.expect_syserr names {fragment!r} twice")
            fragments.append(fragment)
    return tuple(fragments)


class ExpectedSyserrs:
    """The SYSERR fragments one test expects, and which of them a SYSERR line has matched.

    One instance serves every server the test boots, so a fragment logged before a restart counts.
    """

    def __init__(self, fragments: tuple[str, ...] = ()) -> None:
        # What the test's @pytest.mark.expect_syserr(...) markers name, in order.
        self._fragments = fragments
        # The fragments a SYSERR line has contained so far.
        self._seen: set[str] = set()

    def record(self, line: str) -> bool:
        """Marks every fragment line contains as seen; returns whether line contained any."""
        contained = {fragment for fragment in self._fragments if fragment in line}
        self._seen |= contained
        return bool(contained)

    def missing(self) -> list[str]:
        """One problem per expected fragment that no SYSERR line has contained, in marker order."""
        return [f"expected SYSERR never logged: {fragment!r}" for fragment in self._fragments if fragment not in self._seen]


class CrashMonitor:
    """Watches one server for crash evidence, reporting each log line and exit at most once."""

    def __init__(
        self,
        handle: ServerHandle,
        allowed_syserr_fragments: tuple[str, ...] = DEFAULT_ALLOWED,
        expected: ExpectedSyserrs | None = None,
    ) -> None:
        # The server whose log and process are inspected.
        self._handle = handle
        # A SYSERR line containing any of these fragments is not reported.
        self._allowed = allowed_syserr_fragments
        # The SYSERRs the test expects: a line matching one is recorded there instead of reported.
        self._expected = expected if expected is not None else ExpectedSyserrs()
        # Bytes of the log already inspected; each check reads only what follows.
        self._log_offset = 0
        # Whether check() has reported the process's exit, so check_after_stop() does not repeat it.
        self._exit_reported = False
        # Whether any read has seen the shutdown notice that makes an exit status of 0 clean.
        self._shutdown_notice_seen = False

    def check(self) -> list[str]:
        """Problems since the last check: crash markers, unexpected SYSERRs, and a process exit."""
        problems = self._log_problems()
        status = self._handle.exit_status()
        if status is not None and not self._exit_reported:
            self._exit_reported = True
            problems.append(f"server process exited with status {status}")
        return problems

    def check_after_stop(self) -> list[str]:
        """Problems once the launcher has stopped the server.

        Reports crash markers and unexpected SYSERRs written since the last check, including an
        unterminated last line (the shutdown notice is expected), a process that has not exited,
        any exit status other than 0, and status 0 without the shutdown notice in the log. An exit
        check() already reported is not reported again.
        """
        problems = self._log_problems(ignore_line=lambda line: SHUTDOWN_SIGNAL_LINE in line, include_unterminated=True)
        if self._exit_reported:
            return problems
        status = self._handle.exit_status()
        if status is None:
            problems.append("server process still running after stop")
        elif status != 0:
            problems.append(f"server process exited with status {status} after stop")
        elif not self._shutdown_notice_seen:
            # A requested stop logs the notice before exit(0), but diesig() in signals.cpp ends
            # the server on SIGILL, SIGFPE or SIGBUS with exit(0) and no log line, so 0 alone is
            # not clean.
            problems.append("server exited with status 0 without logging the shutdown notice")
        return problems

    def _log_problems(self, ignore_line: Optional[Callable[[str], bool]] = None, include_unterminated: bool = False) -> list[str]:
        """Problems in the log lines written since the last read, skipping lines ignore_line accepts."""
        problems: list[str] = []
        for line in self._new_lines(include_unterminated):
            if SHUTDOWN_SIGNAL_LINE in line:
                self._shutdown_notice_seen = True
            if ignore_line is not None and ignore_line(line):
                continue
            if any(marker in line for marker in SANITIZER_MARKERS) or SIGNAL_MARKER in line:
                problems.append(f"crash marker in log: {line.strip()}")
            elif "SYSERR" in line:
                # Recorded first, so a line that is also on the allow-list still counts as seen.
                matched_expected = self._expected.record(line)
                if not matched_expected and not any(fragment in line for fragment in self._allowed):
                    problems.append(f"unexpected SYSERR: {line.strip()}")
        return problems

    def _new_lines(self, include_unterminated: bool) -> list[str]:
        """The complete log lines written since the last read; none while the log does not exist.

        A line still missing its newline is left for the next read, unless include_unterminated
        asks for it now because the server has stopped writing.
        """
        if not self._handle.log_path.exists():
            return []
        data = self._handle.log_path.read_bytes()
        fresh = data[self._log_offset:]
        if not include_unterminated:
            fresh = fresh[:fresh.rfind(b"\n") + 1]
        self._log_offset += len(fresh)
        return fresh.decode("latin-1", errors="replace").splitlines()
