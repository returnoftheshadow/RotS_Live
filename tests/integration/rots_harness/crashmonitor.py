"""Decides whether the server log or process state shows a crash since the last check."""

from __future__ import annotations

from typing import Callable

from rots_harness.launcher import ServerHandle

DEFAULT_ALLOWED: tuple[str, ...] = (
    "Could not open /judp/password",
    "Mail boot failed",
    "Could not open help file",
    "Unable to open banfile",
)
SANITIZER_MARKERS = ("AddressSanitizer", "LeakSanitizer", "UndefinedBehaviorSanitizer", "runtime error:")
SIGNAL_MARKER = "Error: signal"
SHUTDOWN_SIGNAL_LINE = "Received SIGHUP, SIGINT, or SIGTERM"  # hupsig(), signals.cpp


class CrashMonitor:
    def __init__(self, handle: ServerHandle, allowed_syserr_fragments: tuple[str, ...] = DEFAULT_ALLOWED) -> None:
        self._handle = handle
        self._allowed = allowed_syserr_fragments
        self._offset = 0
        self._exit_reported = False

    def check(self) -> list[str]:
        problems = self._log_problems()
        status = self._handle.exit_status()
        if status is not None and not self._exit_reported:
            self._exit_reported = True
            problems.append(f"server process exited with status {status}")
        return problems

    def check_after_stop(self) -> list[str]:
        """Problems visible only once the launcher has stopped the server: crash markers or
        unexpected SYSERRs written during shutdown (the SIGTERM notice itself is expected),
        and any exit status other than a clean 0. Emergency_save() ends in exit(0), so a
        signal death or a sanitizer abort on the way down shows as a non-zero status, and a
        process that never exited shows as no status at all. An exit that check() already
        reported happened before the stop and is not reported twice."""
        problems = self._log_problems(ignore_line=lambda line: SHUTDOWN_SIGNAL_LINE in line)
        if self._exit_reported:
            return problems
        status = self._handle.exit_status()
        if status is None:
            problems.append("server process still running after stop")
        elif status != 0:
            problems.append(f"server process exited with status {status} after stop")
        return problems

    def _log_problems(self, ignore_line: Callable[[str], bool] | None = None) -> list[str]:
        problems: list[str] = []
        for line in self._new_lines():
            if ignore_line is not None and ignore_line(line):
                continue
            if any(marker in line for marker in SANITIZER_MARKERS) or SIGNAL_MARKER in line:
                problems.append(f"crash marker in log: {line.strip()}")
            elif "SYSERR" in line and not any(fragment in line for fragment in self._allowed):
                problems.append(f"unexpected SYSERR: {line.strip()}")
        return problems

    def _new_lines(self) -> list[str]:
        if not self._handle.log_path.exists():
            return []
        data = self._handle.log_path.read_bytes()
        fresh = data[self._offset:]
        self._offset = len(data)
        return fresh.decode("latin-1", errors="replace").splitlines()
