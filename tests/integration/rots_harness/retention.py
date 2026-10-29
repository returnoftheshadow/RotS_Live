"""Decides whether a scenario's run directory survives fixture teardown, and lists the kept ones."""

from __future__ import annotations

from pathlib import Path
from typing import Iterable, TypedDict


def keep_run_directory(keep_requested: bool, tests_failed_so_far: int, this_server_failed: bool, interrupted: bool) -> bool:
    """True when the run directory must be kept for diagnosis.

    It is kept when the caller asked to keep run directories, when any test in the session has
    failed so far, when this server's own checks failed, or when the run was interrupted (Ctrl-C,
    SIGTERM or SIGHUP), which pytest never counts as a failure. tests_failed_so_far is pytest's
    session counter, which does not yet include a failure raised during teardown (such as the
    crash check), because pytest reports it only after every finalizer has run; the caller
    passes that case as this_server_failed.
    """
    return keep_requested or tests_failed_so_far > 0 or this_server_failed or interrupted


# How far a kept run directory's notes are indented under its summary line.
NOTE_INDENT = "    "


class WorkerEntry(TypedDict):
    """One kept run directory as a worker hands it to the controller, in the built-in types pytest-xdist carries between processes."""

    # The run directory's path as text.
    run_dir: str
    # The node id of the test that used it.
    node_id: str
    # The lines listed under it.
    notes: list[str]


class RunDirectoryReport:
    """Notes whether the run was interrupted and which run directories were kept, and lists them at the end of the run.

    Registered as a pytest plugin, so pytest calls its hook methods. Under pytest-xdist each
    process has its own report: a worker hands its kept directories to the controller
    (worker_entries), whose report merges them (merge_worker_entries) and lists them.
    """

    def __init__(self) -> None:
        # Set when Ctrl-C, SIGTERM or SIGHUP interrupts the run; pytest reports that before fixture teardown.
        self.interrupted = False
        # Each kept run directory with its test's node id and its notes, in the order they were kept.
        self.kept: list[tuple[Path, str, list[str]]] = []

    def record_kept(self, run_dir: Path, node_id: str, notes: Iterable[str] = ()) -> None:
        """Adds a run directory the end-of-run summary lists, with notes printed under it."""
        self.kept.append((run_dir, node_id, list(notes)))

    def worker_entries(self) -> list[WorkerEntry]:
        """The kept run directories, in the order they were kept, as entries merge_worker_entries accepts."""
        return [{"run_dir": str(run_dir), "node_id": node_id, "notes": list(notes)} for run_dir, node_id, notes in self.kept]

    def merge_worker_entries(self, entries: Iterable[WorkerEntry]) -> None:
        """Adds the kept run directories another process's worker_entries returned, after those already recorded."""
        for entry in entries:
            self.record_kept(Path(entry["run_dir"]), entry["node_id"], entry["notes"])

    def summary_lines(self) -> list[str]:
        """Per kept run directory, its path and its test's node id, then each line of its notes indented."""
        lines: list[str] = []
        for run_dir, node_id, notes in self.kept:
            lines.append(f"{run_dir}  {node_id}")
            for note in notes:
                lines.extend(NOTE_INDENT + note_line for note_line in note.splitlines())
        return lines

    def pytest_keyboard_interrupt(self, excinfo: object) -> None:
        """Records the interruption so teardown keeps the interrupted test's run directory."""
        self.interrupted = True

    def pytest_terminal_summary(self, terminalreporter) -> None:
        """Lists the kept run directories in their own section of the terminal summary."""
        if not self.kept:
            return
        terminalreporter.section("kept run directories")
        for line in self.summary_lines():
            terminalreporter.write_line(line)
