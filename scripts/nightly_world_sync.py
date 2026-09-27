#!/usr/bin/env python3
"""Mirror the builders port's world and player data into the coders port during its routine reboot.

    nightly_world_sync.py              one sync, if this start is the one just before the reboot (cron)
    nightly_world_sync.py --dry-run    report what a sync would change; touches nothing
    nightly_world_sync.py --now        sync a coders port that is already stopped

4802's data is the single source of truth: each sync makes 4810's lib/ and mobs/ match it, removing
whatever exists only on 4810, except the accounts, core dump and command logs, which stay 4810's own.
4810 is held down with autorun's pause file while the copy runs. Every run except the start that is
not the one before the reboot appends one line to ~/nightly/world-sync.log; a run that takes the lock
keeps its full output under ~/nightly/world-sync-runs/.
"""

import argparse
import datetime
import fcntl
import os
import shlex
import signal
import subprocess
import sys
import time
import traceback
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, List, Optional, Sequence, TextIO, Tuple


SOURCE_PORT = Path("/rots/dev-building4802")
DESTINATION_PORT = Path("/rots/dev-coding4810")
REBOOT_HOUR_UTC = 10
# Cron starts the job twice, an hour apart, so one start falls in this lead whatever the season.
EARLIEST_LEAD = datetime.timedelta(minutes=5)
LATEST_LEAD = datetime.timedelta(minutes=15)
# A port still running this long after the reboot time did not reboot; nothing is copied.
GIVE_UP_AFTER = datetime.timedelta(minutes=10)
# 4802 reboots at the same moment, and a port booted in minute 0 or 1 reboots again in minute 1:
# waiting past minute 1 lets 4802 finish its shutdown writes and keeps 4810 out of the second reboot.
COPY_NOT_BEFORE = datetime.timedelta(minutes=2)
POLL_SECONDS = 5.0
RSYNC_TIMEOUT_SECONDS = 20 * 60
RSYNC_STDERR_TAIL_CHARACTERS = 4000
KEPT_RUN_LOGS = 14
STAMP_FORMAT = "%Y%m%d_%H%M%S"
# New files and directories must be group-writable for the other coders, whatever cron's umask is.
JOB_UMASK = 0o007
WORLD_INDEX_TYPES = ("zon", "wld", "mob", "obj", "scr", "shp", "maz", "mdl")
# Kept out of the mirror, so --delete leaves 4810's copies alone: accounts are root-only on 4810 and
# partly unreadable on 4802, and the core dump and command logs are each port's own crash evidence.
LIB_EXCLUDES = ("/accounts/", "/core", "/last_cmds", "/crash_cmds")
# World files may hold any byte; Latin-1 reads and writes each byte unchanged.
WORLD_ENCODING = "latin-1"


class SyncError(Exception):
    """A run step was refused or failed; the run stops and reports it."""

    def __init__(self, step: str, reason: str, detail: str = ""):
        super().__init__(reason)
        # Short step name for the summary line, such as "copy" or "pause".
        self.step = step
        # One-line explanation for the summary line.
        self.reason = reason
        # Multi-line context for the end of the run log; empty when the reason says it all.
        self.detail = detail


@dataclass(frozen=True)
class SyncPaths:
    """The two port directories and the job's own files."""

    source_port: Path  # the port whose data is the source of truth (4802)
    destination_port: Path  # the port made to match it (4810)
    home: Path  # ~/nightly; this job's files are all named world-sync*

    @property
    def source_lib(self) -> Path:
        return self.source_port / "lib"

    @property
    def destination_lib(self) -> Path:
        return self.destination_port / "lib"

    @property
    def source_mobs(self) -> Path:
        return self.source_port / "mobs"

    @property
    def destination_mobs(self) -> Path:
        return self.destination_port / "mobs"

    @property
    def pause(self) -> Path:
        return self.destination_port / "pause"

    @property
    def pid_file(self) -> Path:
        return self.destination_port / ".ageland.pid"

    @property
    def lock(self) -> Path:
        return self.home / "world-sync.lock"

    @property
    def summary_log(self) -> Path:
        return self.home / "world-sync.log"

    @property
    def run_logs(self) -> Path:
        return self.home / "world-sync-runs"


def utc_now() -> datetime.datetime:
    return datetime.datetime.now(datetime.timezone.utc)


@dataclass(frozen=True)
class RebootWindow:
    """The routine reboot a run belongs to, and the times derived from it."""

    reboot_at: datetime.datetime  # REBOOT_HOUR_UTC on the reboot's day, timezone-aware

    @staticmethod
    def next_after(now: datetime.datetime) -> "RebootWindow":
        """The window of the first REBOOT_HOUR_UTC:00 UTC at or after the timezone-aware `now`."""
        now_utc = now.astimezone(datetime.timezone.utc)
        reboot_at = now_utc.replace(hour=REBOOT_HOUR_UTC, minute=0, second=0, microsecond=0)
        if reboot_at < now_utc:
            reboot_at += datetime.timedelta(days=1)
        return RebootWindow(reboot_at)

    def holds_reboot(self, now: datetime.datetime) -> bool:
        """True when `now` is between EARLIEST_LEAD and LATEST_LEAD before the reboot, so this start holds it."""
        lead = self.reboot_at - now
        return EARLIEST_LEAD <= lead <= LATEST_LEAD

    @property
    def give_up_at(self) -> datetime.datetime:
        return self.reboot_at + GIVE_UP_AFTER

    @property
    def copy_not_before(self) -> datetime.datetime:
        return self.reboot_at + COPY_NOT_BEFORE


class PauseHold:
    """Holds autorun's pause file for the length of a `with` block and always removes it.

    The file must not already exist: an existing one would be removed on exit, so callers check first.
    """

    def __init__(self, pause_path: Path):
        # autorun waits after every game exit for as long as this file exists.
        self.pause_path = pause_path

    def __enter__(self) -> "PauseHold":
        self.pause_path.touch()
        return self

    def __exit__(self, exception_type: object, exception: object, trace: object) -> bool:
        try:
            self.pause_path.unlink()
        except FileNotFoundError:
            pass
        return False


def process_alive(pid: int) -> bool:
    """True when a process with `pid` exists, including one owned by another user."""
    try:
        os.kill(pid, 0)
    except ProcessLookupError:
        return False
    except PermissionError:
        # The game runs as root: the signal is refused, which still proves the process exists.
        return True
    return True


def port_is_running(paths: SyncPaths, is_process_alive: Callable[[int], bool]) -> bool:
    """True when the destination port's pid file names a live process; a missing or malformed file means stopped."""
    try:
        pid_text = paths.pid_file.read_text().strip()
    except FileNotFoundError:
        return False
    # A pid of 0 would make os.kill() address this job's whole process group.
    if not pid_text.isdigit() or int(pid_text) <= 0:
        return False
    return is_process_alive(int(pid_text))


def read_index_entries(index_path: Path) -> List[str]:
    """The file names an index lists, in order, up to its `$` line; an absent index lists none."""
    try:
        text = index_path.read_text(encoding=WORLD_ENCODING)
    except FileNotFoundError:
        return []
    entries = []
    for line in text.splitlines():
        entry = line.strip()
        if entry == "$":
            break
        if entry:
            entries.append(entry)
    return entries


def write_index_entries(index_path: Path, entries: Sequence[str]) -> None:
    """Writes `entries` and the `$` line through a temporary file and a rename, so the game never
    reads half an index."""
    temporary = index_path.with_name(index_path.name + ".tmp")
    index_text = "".join(entry + "\n" for entry in entries) + "$\n"
    temporary.write_text(index_text, encoding=WORLD_ENCODING)
    # Matches the F660 rsync gives the files it copies.
    os.chmod(temporary, 0o660)
    os.replace(temporary, index_path)


def drop_missing_index_entries(world_dir: Path) -> List[Tuple[str, str]]:
    """Rewrites each world index without entries whose file is absent, and returns (type, entry) for each one dropped.

    An index with nothing missing is left byte for byte, and a world type without a directory is skipped.
    """
    dropped: List[Tuple[str, str]] = []
    for file_type in WORLD_INDEX_TYPES:
        type_dir = world_dir / file_type
        index_path = type_dir / "index"
        if not index_path.is_file():
            continue
        entries = read_index_entries(index_path)
        present = [entry for entry in entries if (type_dir / entry).is_file()]
        if len(present) == len(entries):
            continue
        for entry in entries:
            if entry not in present:
                dropped.append((file_type, entry))
        write_index_entries(index_path, present)
    return dropped


# rsync's exit status when source files disappeared mid-copy; 4802 keeps running while it is read.
RSYNC_VANISHED_FILES = 24


def rsync_command(source: Path, destination: Path, excludes: Sequence[str], dry_run: bool) -> List[str]:
    """The rsync argv that makes `destination` a mirror of `source`, apart from `excludes`; with `dry_run`,
    rsync only lists the changes."""
    # No -p and no directory times: the game creates root-owned paths in 4810 that this job cannot
    # chmod or touch. --chmod and the job umask still give new files the coders group's modes.
    command = ["rsync", "-rlt", "--omit-dir-times", "--delete", "--chmod=D2770,F660", "--itemize-changes"]
    if dry_run:
        command.append("--dry-run")
    for pattern in excludes:
        command.append(f"--exclude={pattern}")
    command += [f"{source}/", f"{destination}/"]
    return command


def run_rsync(command: Sequence[str], run_log: TextIO) -> Tuple[int, int]:
    """Runs one rsync, logging its itemized changes, and returns (files sent, paths removed).

    Raises SyncError("copy") when rsync cannot start, times out, or fails other than by vanished source files.
    """
    run_log.write("$ " + shlex.join(command) + "\n")
    try:
        result = subprocess.run(list(command), capture_output=True, text=True, errors="replace",
                                timeout=RSYNC_TIMEOUT_SECONDS)
    except subprocess.TimeoutExpired:
        raise SyncError("copy", f"rsync timed out after {RSYNC_TIMEOUT_SECONDS} s")
    except OSError as error:
        raise SyncError("copy", f"cannot run rsync: {error.strerror}")
    run_log.write(result.stdout)
    run_log.write(result.stderr)
    if result.returncode not in (0, RSYNC_VANISHED_FILES):
        stderr_tail = result.stderr[-RSYNC_STDERR_TAIL_CHARACTERS:]
        raise SyncError("copy", f"rsync exited with status {result.returncode}", stderr_tail)
    # Each file sent is itemized on a line starting ">f", and each removal on one starting "*deleting".
    output_lines = result.stdout.splitlines()
    files_sent = sum(1 for line in output_lines if line.startswith(">f"))
    paths_removed = sum(1 for line in output_lines if line.startswith("*deleting"))
    return files_sent, paths_removed


@dataclass(frozen=True)
class CopyReport:
    """What one copy changed, for the summary line."""

    files_copied: int  # regular files rsync sent into lib/ and mobs/
    files_removed: int  # paths rsync removed from 4810 because 4802 no longer has them
    dropped_entries: Tuple[Tuple[str, str], ...]  # (type, entry) index entries dropped for absent files

    def summary(self) -> str:
        return (f"{self.files_copied} files copied, {self.files_removed} removed, "
                f"{len(self.dropped_entries)} index entries dropped")


def copy_port_data(paths: SyncPaths, run_log: TextIO, dry_run: bool) -> CopyReport:
    """Mirrors 4802's lib/ and mobs/ into 4810, then drops index entries whose file did not arrive.

    With `dry_run`, rsync only lists what it would change and no index is touched. Raises
    SyncError("copy") when rsync fails.
    """
    lib_command = rsync_command(paths.source_lib, paths.destination_lib, LIB_EXCLUDES, dry_run)
    lib_sent, lib_removed = run_rsync(lib_command, run_log)
    mobs_command = rsync_command(paths.source_mobs, paths.destination_mobs, (), dry_run)
    mobs_sent, mobs_removed = run_rsync(mobs_command, run_log)
    dropped: List[Tuple[str, str]] = []
    if not dry_run:
        # A file 4802 added or removed while rsync read it can leave an index naming a file 4810
        # lacks, which stops the boot; the next night's mirror restores the entry.
        dropped = drop_missing_index_entries(paths.destination_lib / "world")
        for file_type, entry in dropped:
            run_log.write(f"index {file_type}: dropped {entry}: the file is missing on 4810\n")
    return CopyReport(lib_sent + mobs_sent, lib_removed + mobs_removed, tuple(dropped))


@dataclass(frozen=True)
class SyncOutcome:
    """How a run that did not fail ended."""

    message: str  # the summary-line result
    quiet: bool = False  # True when main() leaves no trace: no summary line, and the run log is deleted


def wait_until(target: datetime.datetime, clock: Callable[[], datetime.datetime],
               sleep: Callable[[float], None]) -> None:
    """Sleeps in polling steps until `clock()` reaches `target`."""
    while True:
        current_time = clock()
        remaining = (target - current_time).total_seconds()
        if remaining <= 0:
            return
        step_seconds = min(POLL_SECONDS, remaining)
        sleep(step_seconds)


def run_sync(paths: SyncPaths, clock: Callable[[], datetime.datetime], sleep: Callable[[float], None],
             is_process_alive: Callable[[int], bool], sync_data: Callable[[bool], CopyReport], dry_run: bool,
             now_mode: bool) -> SyncOutcome:
    """One run: decide whether this start holds the reboot, then hold 4810 down and copy.

    - A start that does not hold the reboot returns a quiet outcome and does nothing.
    - If 4810 is still running at the give-up time, the pause is released and nothing is copied.
    - A dry run reports the timing decision and what the copy would change, without holding or waiting.
    - `now_mode` copies straight away, and only when 4810 is stopped.

    Raises SyncError when the run is refused or a step fails. A pause file this run creates is removed
    on every path; one that existed before the run is never touched.
    """
    started = clock()
    window = RebootWindow.next_after(started)
    holds_reboot = window.holds_reboot(started)
    if dry_run:
        report = sync_data(True)
        if holds_reboot:
            timing = f"this start would hold the {window.reboot_at:%H:%M} UTC reboot"
        else:
            timing = "this start would not hold the reboot"
        return SyncOutcome(f"dry run: {timing}; would have: {report.summary()}")
    if now_mode:
        if paths.pause.exists():
            # Someone else's hold: copy under it and leave it for them to release.
            if port_is_running(paths, is_process_alive):
                raise SyncError("now", "4810 is running; --now copies only a stopped port")
            report = sync_data(False)
            return SyncOutcome(f"synced (--now, existing pause kept): {report.summary()}")
        # Pause first, then check: autorun deletes the pid file just before it checks for the pause.
        with PauseHold(paths.pause):
            if port_is_running(paths, is_process_alive):
                raise SyncError("now", "4810 is running; --now copies only a stopped port")
            report = sync_data(False)
        return SyncOutcome(f"synced (--now): {report.summary()}")
    if not holds_reboot:
        return SyncOutcome("skipped: not the run before the reboot", quiet=True)
    if paths.pause.exists():
        raise SyncError("pause", f"{paths.pause} already exists; someone is holding 4810, so it is left alone")
    with PauseHold(paths.pause):
        while port_is_running(paths, is_process_alive):
            if clock() >= window.give_up_at:
                return SyncOutcome(f"skipped: 4810 did not reboot by {window.give_up_at:%H:%M} UTC")
            sleep(POLL_SECONDS)
        wait_until(window.copy_not_before, clock, sleep)
        report = sync_data(False)
    return SyncOutcome(f"synced: {report.summary()}")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="nightly_world_sync.py",
                                     description="Mirror 4802's world and player data into 4810 during its reboot.")
    modes = parser.add_mutually_exclusive_group()
    modes.add_argument("--dry-run", action="store_true", help="report what a sync would change; touch nothing")
    modes.add_argument("--now", action="store_true", help="sync now; refused unless 4810 is already stopped")
    return parser


def raise_system_exit(signal_number: int, frame: object) -> None:
    """Handler for SIGTERM, SIGHUP and SIGINT: unwinds like an exception, so a held pause file is still removed."""
    raise SystemExit(128 + signal_number)


def summary_line(when: datetime.datetime, result: str) -> str:
    """One world-sync.log line, stamped in the server's local time."""
    return f"{when.astimezone():%Y-%m-%d %H:%M:%S}  {result}"


def append_summary(paths: SyncPaths, line: str) -> None:
    with paths.summary_log.open("a") as summary_log:
        summary_log.write(line + "\n")


def prune_run_logs(run_logs: Path) -> None:
    """Deletes all but the newest KEPT_RUN_LOGS run logs."""
    # Names start with the run's timestamp, so name order is age order.
    logs = sorted(run_logs.glob("*.log"), reverse=True)
    for old_log in logs[KEPT_RUN_LOGS:]:
        old_log.unlink()


def main(argv: Optional[Sequence[str]] = None, paths: Optional[SyncPaths] = None,
         clock: Callable[[], datetime.datetime] = utc_now, sleep: Callable[[float], None] = time.sleep,
         is_process_alive: Callable[[int], bool] = process_alive,
         copy: Callable[[SyncPaths, TextIO, bool], CopyReport] = copy_port_data) -> int:
    """Runs the command line and returns the process exit status (0 unless a run failed or was terminated).

    `paths` defaults to the real ports and ~/nightly; tests pass their own, with a fake clock and copy.
    """
    arguments = build_parser().parse_args(argv)
    if paths is None:
        paths = SyncPaths(SOURCE_PORT, DESTINATION_PORT, Path.home() / "nightly")
    # SIGHUP covers a manual run whose ssh session drops; without a handler the pause would stay.
    for signal_number in (signal.SIGTERM, signal.SIGHUP, signal.SIGINT):
        signal.signal(signal_number, raise_system_exit)
    os.umask(JOB_UMASK)
    paths.run_logs.mkdir(parents=True, exist_ok=True)
    started = clock()
    with paths.lock.open("a") as lock_file:
        try:
            fcntl.flock(lock_file, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            append_summary(paths, summary_line(started, "skipped: another sync holds the lock"))
            return 0
        run_log_name = started.astimezone().strftime(STAMP_FORMAT)
        if arguments.dry_run:
            run_log_name += "-dry-run"
        run_log_path = paths.run_logs / f"{run_log_name}.log"
        quiet = False
        with run_log_path.open("w") as run_log:

            def sync_data(dry_run: bool) -> CopyReport:
                return copy(paths, run_log, dry_run)

            try:
                outcome = run_sync(paths, clock, sleep, is_process_alive, sync_data, arguments.dry_run,
                                   arguments.now)
                result = outcome.message
                quiet = outcome.quiet
                exit_status = 0
            except SyncError as error:
                result = f"failed: {error.step}: {error.reason}"
                run_log.write(f"\nFAILED at {error.step}: {error.reason}\n")
                if error.detail:
                    run_log.write(error.detail + "\n")
                exit_status = 1
            except SystemExit as termination:
                # A signal handler's exit; the pause was already released as the stack unwound.
                exit_status = 1
                if isinstance(termination.code, int):
                    exit_status = termination.code
                result = f"failed: terminated (exit {exit_status})"
                run_log.write(f"\nTERMINATED with exit {exit_status}\n")
            except Exception:
                result = "failed: unexpected error; see the run log"
                run_log.write("\n" + traceback.format_exc())
                exit_status = 1
        if quiet:
            run_log_path.unlink()
        else:
            line = f"{summary_line(started, result)}  (log: {paths.run_logs.name}/{run_log_path.name})"
            append_summary(paths, line)
            print(line)
        prune_run_logs(paths.run_logs)
        return exit_status


if __name__ == "__main__":
    sys.exit(main())
