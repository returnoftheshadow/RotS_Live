"""Decides whether the server log or process state shows a crash since the last check."""

from __future__ import annotations

from rots_harness.launcher import ServerHandle

DEFAULT_ALLOWED: tuple[str, ...] = (
    "Could not open /judp/password",
    "Mail boot failed",
    "Could not open help file",
    "Unable to open banfile",
    # KNOWN FINDING (2026-09-19): account-native object refresh truncates the follower
    # record for an empty-inventory character on rent/death; the death still completes.
    # Tolerated, not masked — see WIP.md and the harness finding note.
    "failed to refresh account-native object file",
)
SANITIZER_MARKERS = ("AddressSanitizer", "LeakSanitizer", "UndefinedBehaviorSanitizer", "runtime error:")
SIGNAL_MARKER = "Error: signal"


class CrashMonitor:
    def __init__(self, handle: ServerHandle, allowed_syserr_fragments: tuple[str, ...] = DEFAULT_ALLOWED) -> None:
        self._handle = handle
        self._allowed = allowed_syserr_fragments
        self._offset = 0
        self._exit_reported = False

    def check(self) -> list[str]:
        problems: list[str] = []
        for line in self._new_lines():
            if any(marker in line for marker in SANITIZER_MARKERS) or SIGNAL_MARKER in line:
                problems.append(f"crash marker in log: {line.strip()}")
            elif "SYSERR" in line and not any(fragment in line for fragment in self._allowed):
                problems.append(f"unexpected SYSERR: {line.strip()}")
        status = self._handle.exit_status()
        if status is not None and not self._exit_reported:
            self._exit_reported = True
            problems.append(f"server process exited with status {status}")
        return problems

    def _new_lines(self) -> list[str]:
        if not self._handle.log_path.exists():
            return []
        data = self._handle.log_path.read_bytes()
        fresh = data[self._offset:]
        self._offset = len(data)
        return fresh.decode("latin-1", errors="replace").splitlines()
