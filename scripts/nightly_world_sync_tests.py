#!/usr/bin/env python3

import contextlib
import datetime
import fcntl
import importlib.util
import io
import os
import signal
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from typing import List, Optional
from unittest import mock


MODULE_PATH = Path(__file__).resolve().parent / "nightly_world_sync.py"
SPEC = importlib.util.spec_from_file_location("nightly_world_sync", MODULE_PATH)
sync = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
sys.modules["nightly_world_sync"] = sync
sys.dont_write_bytecode = True
SPEC.loader.exec_module(sync)


def utc(hour: int, minute: int, second: int = 0, day: int = 27) -> datetime.datetime:
    """A timezone-aware UTC time on a day in September 2026."""
    return datetime.datetime(2026, 9, day, hour, minute, second, tzinfo=datetime.timezone.utc)


class TempDirTestCase(unittest.TestCase):
    """Gives each test a fresh temporary directory that is removed afterwards."""

    def setUp(self) -> None:
        temp_dir = tempfile.TemporaryDirectory()
        self.addCleanup(temp_dir.cleanup)
        # Resolved so paths compare equal on macOS, where the temporary directory sits behind a symlink.
        self.root = Path(temp_dir.name).resolve()


# ---------------------------------------------------------------------------------------------
# Reboot window and pause hold
# ---------------------------------------------------------------------------------------------


class RebootWindowTest(unittest.TestCase):
    def test_next_reboot_is_today_before_ten_utc(self) -> None:
        window = sync.RebootWindow.next_after(utc(9, 50))

        self.assertEqual(window.reboot_at, utc(10, 0))

    def test_next_reboot_is_tomorrow_after_ten_utc(self) -> None:
        window = sync.RebootWindow.next_after(utc(10, 5))

        self.assertEqual(window.reboot_at, utc(10, 0, day=28))

    def test_local_times_are_converted_to_utc(self) -> None:
        central_daylight = datetime.timezone(datetime.timedelta(hours=-5))
        started = datetime.datetime(2026, 9, 27, 4, 50, tzinfo=central_daylight)

        self.assertEqual(sync.RebootWindow.next_after(started).reboot_at, utc(10, 0))

    def test_the_0450_cdt_start_holds_the_reboot(self) -> None:
        started = utc(9, 50)

        self.assertTrue(sync.RebootWindow.next_after(started).holds_reboot(started))

    def test_the_0350_cdt_start_does_not_hold_the_reboot(self) -> None:
        started = utc(8, 50)

        self.assertFalse(sync.RebootWindow.next_after(started).holds_reboot(started))

    def test_the_0350_cst_start_holds_the_reboot(self) -> None:
        central_standard = datetime.timezone(datetime.timedelta(hours=-6))
        started = datetime.datetime(2026, 12, 1, 3, 50, tzinfo=central_standard)

        self.assertTrue(sync.RebootWindow.next_after(started).holds_reboot(started))

    def test_a_start_under_five_minutes_before_is_too_late(self) -> None:
        started = utc(9, 56)

        self.assertFalse(sync.RebootWindow.next_after(started).holds_reboot(started))

    def test_give_up_and_copy_times(self) -> None:
        window = sync.RebootWindow.next_after(utc(9, 50))

        self.assertEqual(window.give_up_at, utc(10, 10))
        self.assertEqual(window.copy_not_before, utc(10, 2))


class PauseHoldTest(TempDirTestCase):
    def test_pause_exists_inside_the_block_and_is_removed_after(self) -> None:
        pause = self.root / "pause"

        with sync.PauseHold(pause):
            self.assertTrue(pause.exists())

        self.assertFalse(pause.exists())

    def test_pause_is_removed_when_the_block_raises(self) -> None:
        pause = self.root / "pause"

        with self.assertRaises(RuntimeError):
            with sync.PauseHold(pause):
                raise RuntimeError("copy failed")

        self.assertFalse(pause.exists())

    def test_a_pause_removed_by_someone_else_is_not_an_error(self) -> None:
        pause = self.root / "pause"

        with sync.PauseHold(pause):
            pause.unlink()


class PortRunningTest(TempDirTestCase):
    def setUp(self) -> None:
        super().setUp()
        # A throwaway port pair and home for the path helpers.
        self.paths = sync.SyncPaths(self.root / "4802", self.root / "4810", self.root / "home")
        self.paths.destination_port.mkdir()

    def test_no_pid_file_means_stopped(self) -> None:
        self.assertFalse(sync.port_is_running(self.paths, lambda pid: True))

    def test_a_live_pid_means_running(self) -> None:
        self.paths.pid_file.write_text("4242\n")

        self.assertTrue(sync.port_is_running(self.paths, lambda pid: pid == 4242))

    def test_a_dead_pid_means_stopped(self) -> None:
        self.paths.pid_file.write_text("4242\n")

        self.assertFalse(sync.port_is_running(self.paths, lambda pid: False))

    def test_a_malformed_pid_file_means_stopped(self) -> None:
        self.paths.pid_file.write_text("garbage\n")

        self.assertFalse(sync.port_is_running(self.paths, lambda pid: True))

    def test_process_alive_sees_this_process_and_not_a_finished_one(self) -> None:
        finished = subprocess.run([sys.executable, "-c", "import os; print(os.getpid())"], capture_output=True,
                                  text=True, check=True)

        self.assertTrue(sync.process_alive(os.getpid()))
        self.assertFalse(sync.process_alive(int(finished.stdout)))


# ---------------------------------------------------------------------------------------------
# World index files
# ---------------------------------------------------------------------------------------------


def write_world_file(directory: Path, name: str, text: str) -> Path:
    """Writes one world or index file, creating its directory."""
    directory.mkdir(parents=True, exist_ok=True)
    path = directory / name
    path.write_text(text, encoding="latin-1")
    return path


class MissingIndexEntriesTest(TempDirTestCase):
    def test_entries_naming_absent_files_are_dropped_and_reported(self) -> None:
        world = self.root / "world"
        write_world_file(world / "wld", "index", "11.wld\n12.wld\n$\n")
        write_world_file(world / "wld", "11.wld", "#1101\n")

        dropped = sync.drop_missing_index_entries(world)

        self.assertEqual(dropped, [("wld", "12.wld")])
        self.assertEqual((world / "wld" / "index").read_text(encoding="latin-1"), "11.wld\n$\n")

    def test_complete_indexes_are_left_byte_for_byte(self) -> None:
        world = self.root / "world"
        write_world_file(world / "obj", "index", "11.obj\n$\n\n")
        write_world_file(world / "obj", "11.obj", "#1101\n")

        dropped = sync.drop_missing_index_entries(world)

        self.assertEqual(dropped, [])
        self.assertEqual((world / "obj" / "index").read_text(encoding="latin-1"), "11.obj\n$\n\n")

    def test_missing_entries_are_listed_without_changing_the_index(self) -> None:
        world = self.root / "world"
        write_world_file(world / "wld", "index", "11.wld\n12.wld\n$\n")
        write_world_file(world / "wld", "11.wld", "#1101\n")

        self.assertEqual(sync.missing_index_entries(world), [("wld", "12.wld")])
        self.assertEqual((world / "wld" / "index").read_text(encoding="latin-1"), "11.wld\n12.wld\n$\n")

    def test_world_types_without_a_directory_are_skipped(self) -> None:
        self.assertEqual(sync.drop_missing_index_entries(self.root / "world"), [])


class IndexFileTest(TempDirTestCase):
    def test_entries_stop_at_the_dollar_line_and_skip_blank_lines(self) -> None:
        index = write_world_file(self.root, "index", "11.obj\n\n13.obj\n$\n\n")

        self.assertEqual(sync.read_index_entries(index), ["11.obj", "13.obj"])

    def test_a_missing_index_lists_nothing(self) -> None:
        self.assertEqual(sync.read_index_entries(self.root / "index"), [])

    def test_written_index_ends_with_the_dollar_line(self) -> None:
        index = self.root / "index"

        sync.write_index_entries(index, ["11.wld", "13.wld"])

        self.assertEqual(index.read_text(encoding="latin-1"), "11.wld\n13.wld\n$\n")
        self.assertEqual(sorted(path.name for path in self.root.iterdir()), ["index"])


# ---------------------------------------------------------------------------------------------
# The copy
# ---------------------------------------------------------------------------------------------


def rsync3_available() -> bool:
    """True when rsync 3.x is on PATH; macOS ships openrsync, which lacks options the copy uses."""
    try:
        result = subprocess.run(["rsync", "--version"], capture_output=True, text=True)
    except OSError:
        return False
    return result.stdout.startswith("rsync  version 3")


RSYNC_3_AVAILABLE = rsync3_available()


def make_port(root: Path, name: str) -> Path:
    """Creates a minimal port tree under `root / name` whose file contents name the port, so a test can
    tell whose copy it reads."""
    port = root / name
    write_world_file(port / "lib" / "world" / "wld", "index", "11.wld\n$\n")
    write_world_file(port / "lib" / "world" / "wld", "11.wld", f"#1101\n{name} room~\nS\n#99999\n")
    write_world_file(port / "lib" / "players" / "A-E", "alice", f"alice on {name}\n")
    write_world_file(port / "lib" / "accounts" / "A-E", "someone@example.org", f"account on {name}\n")
    write_world_file(port / "lib", "core", "core dump\n")
    write_world_file(port / "mobs", "mob.csv", f"mobs on {name}\n")
    return port


class RsyncCommandTest(unittest.TestCase):
    def test_copy_is_a_mirror_that_never_sets_existing_modes_or_directory_times(self) -> None:
        command = sync.rsync_command(Path("/src"), Path("/dst"), sync.LIB_EXCLUDES, dry_run=False)

        self.assertEqual(command[1], "-rlt")
        self.assertIn("--delete", command)
        self.assertIn("--omit-dir-times", command)
        self.assertIn("--chmod=D2770,F660", command)
        self.assertEqual(command[-2:], ["/src/", "/dst/"])
        self.assertNotIn("--dry-run", command)

    def test_lib_excludes_accounts_core_and_command_logs_only(self) -> None:
        command = sync.rsync_command(Path("/src"), Path("/dst"), sync.LIB_EXCLUDES, dry_run=True)

        for pattern in ("/accounts/", "/core", "/last_cmds", "/crash_cmds"):
            self.assertIn(f"--exclude={pattern}", command)
        self.assertFalse(any("index" in argument for argument in command))
        self.assertIn("--dry-run", command)


class RunRsyncTest(unittest.TestCase):
    def completed(self, returncode: int, stdout: str = "", stderr: str = "") -> subprocess.CompletedProcess:
        return subprocess.CompletedProcess(["rsync"], returncode, stdout, stderr)

    def test_counts_sent_and_removed_files(self) -> None:
        output = (">f+++++++++ players/A-E/alice\ncd+++++++++ mobs/\n>f.st...... world/wld/11.wld\n"
                  "*deleting   players/K-O/kim\n")

        with mock.patch.object(sync.subprocess, "run", return_value=self.completed(0, output)):
            self.assertEqual(sync.run_rsync(["rsync"], io.StringIO()), (2, 1))

    def test_vanished_source_files_are_not_a_failure(self) -> None:
        with mock.patch.object(sync.subprocess, "run", return_value=self.completed(24, ">f+++++++++ a\n")):
            self.assertEqual(sync.run_rsync(["rsync"], io.StringIO()), (1, 0))

    def test_a_missing_rsync_is_a_copy_failure(self) -> None:
        with mock.patch.object(sync.subprocess, "run", side_effect=FileNotFoundError(2, "No such file")):
            with self.assertRaises(sync.SyncError) as caught:
                sync.run_rsync(["rsync"], io.StringIO())

        self.assertEqual(caught.exception.step, "copy")
        self.assertIn("cannot run rsync", caught.exception.reason)

    def test_other_failures_raise(self) -> None:
        with mock.patch.object(sync.subprocess, "run", return_value=self.completed(23, "", "partial transfer")):
            with self.assertRaises(sync.SyncError) as caught:
                sync.run_rsync(["rsync"], io.StringIO())

        self.assertEqual(caught.exception.step, "copy")
        self.assertIn("status 23", caught.exception.reason)


@unittest.skipUnless(RSYNC_3_AVAILABLE, "needs rsync 3.x (runs on rotsmud)")
class CopyTest(TempDirTestCase):
    def setUp(self) -> None:
        super().setUp()
        make_port(self.root, "4802")
        make_port(self.root, "4810")
        # The pair the copy runs between.
        self.paths = sync.SyncPaths(self.root / "4802", self.root / "4810", self.root / "home")
        # 4810-only data, which the mirror removes.
        write_world_file(self.paths.destination_lib / "players" / "K-O", "kim", "only on 4810\n")
        write_world_file(self.paths.destination_lib / "world" / "wld", "index", "11.wld\n40.wld\n$\n")
        write_world_file(self.paths.destination_lib / "world" / "wld", "40.wld", "#4001\nroom~\nS\n#99999\n")
        # Both ports' files were written within the same second at the same sizes, which rsync's size-and-time
        # check would treat as unchanged; age 4810's copies so 4802's differ as they would in real use.
        for destination_file in self.paths.destination_port.rglob("*"):
            if destination_file.is_file():
                os.utime(destination_file, (1_000_000_000, 1_000_000_000))

    def read_destination(self, *parts: str) -> str:
        return self.paths.destination_port.joinpath(*parts).read_text(encoding="latin-1")

    def test_copy_mirrors_4802(self) -> None:
        report = sync.copy_port_data(self.paths, io.StringIO(), dry_run=False)

        self.assertEqual(self.read_destination("lib", "players", "A-E", "alice"), "alice on 4802\n")
        self.assertEqual(self.read_destination("mobs", "mob.csv"), "mobs on 4802\n")
        self.assertFalse((self.paths.destination_lib / "players" / "K-O" / "kim").exists())
        self.assertFalse((self.paths.destination_lib / "world" / "wld" / "40.wld").exists())
        self.assertEqual(self.read_destination("lib", "world", "wld", "index"), "11.wld\n$\n")
        self.assertGreaterEqual(report.files_copied, 2)
        self.assertGreaterEqual(report.files_removed, 2)

    def test_accounts_core_and_command_logs_on_4810_survive(self) -> None:
        write_world_file(self.paths.destination_lib, "last_cmds", "4810 commands\n")

        sync.copy_port_data(self.paths, io.StringIO(), dry_run=False)

        self.assertEqual(self.read_destination("lib", "accounts", "A-E", "someone@example.org"), "account on 4810\n")
        self.assertEqual(self.read_destination("lib", "core"), "core dump\n")
        self.assertEqual(self.read_destination("lib", "last_cmds"), "4810 commands\n")

    def test_new_files_are_group_writable_under_the_job_umask(self) -> None:
        (self.paths.source_lib / "players" / "A-E" / "alice").chmod(0o644)
        (self.paths.destination_lib / "players" / "A-E" / "alice").unlink()
        previous_umask = os.umask(sync.JOB_UMASK)
        self.addCleanup(os.umask, previous_umask)

        sync.copy_port_data(self.paths, io.StringIO(), dry_run=False)

        copied_mode = (self.paths.destination_lib / "players" / "A-E" / "alice").stat().st_mode & 0o777
        self.assertEqual(copied_mode, 0o660)

    def test_dry_run_changes_nothing(self) -> None:
        report = sync.copy_port_data(self.paths, io.StringIO(), dry_run=True)

        self.assertEqual(self.read_destination("lib", "players", "A-E", "alice"), "alice on 4810\n")
        self.assertEqual(self.read_destination("lib", "players", "K-O", "kim"), "only on 4810\n")
        self.assertEqual(self.read_destination("lib", "world", "wld", "index"), "11.wld\n40.wld\n$\n")
        self.assertGreaterEqual(report.files_copied, 2)


# ---------------------------------------------------------------------------------------------
# One run
# ---------------------------------------------------------------------------------------------


class FakeClock:
    """A clock that moves only when the code under test sleeps."""

    def __init__(self, start: datetime.datetime):
        # The current fake time, advanced by sleep().
        self.current = start

    def now(self) -> datetime.datetime:
        return self.current

    def sleep(self, seconds: float) -> None:
        self.current += datetime.timedelta(seconds=seconds)


class SyncStepTest(TempDirTestCase):
    def setUp(self) -> None:
        super().setUp()
        # A throwaway port pair; only the destination's pid and pause files matter here.
        self.paths = sync.SyncPaths(self.root / "4802", self.root / "4810", self.root / "home")
        self.paths.destination_port.mkdir()
        self.paths.pid_file.write_text("4242\n")
        # Each copy a step asks for: (fake time, dry_run flag, whether pause existed).
        self.copies: List[tuple] = []
        # When the fake game process exits; None keeps it running forever.
        self.game_exits_at: Optional[datetime.datetime] = utc(10, 0, 30)

    def fakes(self, start: datetime.datetime):
        """A fake clock, liveness check and copy that record into this test."""
        clock = FakeClock(start)

        def alive(pid: int) -> bool:
            if self.game_exits_at is None:
                return True
            return clock.now() < self.game_exits_at

        def sync_data(dry_run_copy: bool) -> "sync.CopyReport":
            self.copies.append((clock.now(), dry_run_copy, self.paths.pause.exists()))
            return sync.CopyReport(3, 0, ())

        return clock, alive, sync_data

    def run_nightly(self, start: datetime.datetime):
        clock, alive, sync_data = self.fakes(start)
        return sync.nightly_sync(self.paths, clock.now, clock.sleep, alive, sync_data), clock

    def run_preview(self, start: datetime.datetime) -> "sync.SyncOutcome":
        clock, _alive, sync_data = self.fakes(start)
        return sync.preview_sync(self.paths, clock.now, sync_data)

    def run_copy(self) -> "sync.SyncOutcome":
        _clock, alive, sync_data = self.fakes(utc(14, 0))
        return sync.copy_now(self.paths, alive, sync_data)

    def test_the_start_before_the_reboot_holds_it_and_copies_after_1002(self) -> None:
        outcome, _clock = self.run_nightly(utc(9, 50))

        self.assertEqual(outcome.message, "synced: 3 files copied, 0 removed, 0 index entries dropped")
        self.assertEqual(len(self.copies), 1)
        copied_at, dry_run_copy, pause_during_copy = self.copies[0]
        self.assertGreaterEqual(copied_at, utc(10, 2))
        self.assertFalse(dry_run_copy)
        self.assertTrue(pause_during_copy)
        self.assertFalse(self.paths.pause.exists())

    def test_the_other_start_does_nothing_quietly(self) -> None:
        outcome, _clock = self.run_nightly(utc(8, 50))

        self.assertTrue(outcome.quiet)
        self.assertEqual(self.copies, [])
        self.assertFalse(self.paths.pause.exists())

    def test_port_that_never_stops_is_released_without_copying(self) -> None:
        self.game_exits_at = None

        outcome, clock = self.run_nightly(utc(9, 50))

        self.assertEqual(outcome.message, "skipped: 4810 did not reboot by 10:10 UTC")
        self.assertEqual(self.copies, [])
        self.assertFalse(self.paths.pause.exists())
        self.assertGreaterEqual(clock.now(), utc(10, 10))

    def test_existing_pause_is_left_alone_and_reported(self) -> None:
        self.paths.pause.write_text("held by a person\n")

        with self.assertRaises(sync.SyncError) as caught:
            self.run_nightly(utc(9, 50))

        self.assertEqual(caught.exception.step, "pause")
        self.assertEqual(self.paths.pause.read_text(), "held by a person\n")
        self.assertEqual(self.copies, [])

    def test_a_failed_copy_still_releases_the_pause(self) -> None:
        clock = FakeClock(utc(9, 50))

        def failing_copy(dry_run_copy: bool) -> "sync.CopyReport":
            raise sync.SyncError("copy", "rsync exited with status 23")

        with self.assertRaises(sync.SyncError):
            sync.nightly_sync(self.paths, clock.now, clock.sleep, lambda pid: clock.now() < utc(10, 0, 30),
                              failing_copy)

        self.assertFalse(self.paths.pause.exists())

    def test_preview_copies_nothing_holds_nothing_and_reports_timing(self) -> None:
        outcome = self.run_preview(utc(9, 50))

        self.assertTrue(outcome.message.startswith("a nightly run started now would hold the 10:00 UTC reboot; "
                                                   "would have: 3 files copied"))
        self.assertEqual([copy[1:] for copy in self.copies], [(True, False)])
        self.assertFalse(self.paths.pause.exists())

    def test_preview_at_another_time_says_it_would_not_hold(self) -> None:
        outcome = self.run_preview(utc(14, 0))

        self.assertTrue(outcome.message.startswith("a nightly run started now would not hold the reboot"))

    def test_copy_refuses_a_running_port(self) -> None:
        self.game_exits_at = None

        with self.assertRaises(sync.SyncError) as caught:
            self.run_copy()

        self.assertEqual(caught.exception.step, "copy")
        self.assertIn("4810 is running", caught.exception.reason)
        self.assertEqual(self.copies, [])
        self.assertFalse(self.paths.pause.exists())

    def test_copy_of_a_stopped_port_runs_under_its_own_pause(self) -> None:
        self.paths.pid_file.unlink()

        outcome = self.run_copy()

        self.assertEqual(outcome.message, "synced: 3 files copied, 0 removed, 0 index entries dropped")
        self.assertTrue(self.copies[0][2])
        self.assertFalse(self.paths.pause.exists())

    def test_copy_takes_the_pause_before_checking_the_port(self) -> None:
        pause_seen_by_check: List[bool] = []

        def watching_alive(pid: int) -> bool:
            pause_seen_by_check.append(self.paths.pause.exists())
            return False

        def one_file_copy(dry_run_copy: bool) -> "sync.CopyReport":
            return sync.CopyReport(1, 0, ())

        sync.copy_now(self.paths, watching_alive, one_file_copy)

        self.assertEqual(pause_seen_by_check, [True])
        self.assertFalse(self.paths.pause.exists())

    def test_copy_under_someone_elses_pause_keeps_it(self) -> None:
        self.paths.pid_file.unlink()
        self.paths.pause.write_text("held by a person\n")

        outcome = self.run_copy()

        self.assertTrue(outcome.message.startswith("synced (existing pause kept): "))
        self.assertTrue(self.paths.pause.exists())

    def test_a_pid_of_zero_is_not_a_running_port(self) -> None:
        self.paths.pid_file.write_text("0\n")

        self.assertFalse(sync.port_is_running(self.paths, lambda pid: True))


class CheckIndexesTest(TempDirTestCase):
    def setUp(self) -> None:
        super().setUp()
        # A destination port whose wld index names one file that exists and one that does not.
        self.paths = sync.SyncPaths(self.root / "4802", self.root / "4810", self.root / "home")
        # The destination's wld/ directory, whose index the tests read back.
        self.wld_dir = self.paths.destination_lib / "world" / "wld"
        write_world_file(self.wld_dir, "index", "11.wld\n12.wld\n$\n")
        write_world_file(self.wld_dir, "11.wld", "#1101\n")

    def test_the_report_lists_missing_entries_changes_nothing_and_fails(self) -> None:
        run_log = io.StringIO()

        outcome = sync.check_indexes(self.paths, run_log, fix=False)

        self.assertEqual(outcome.message,
                         "1 index entries name missing files: wld/12.wld; run with --fix to drop them")
        self.assertFalse(outcome.succeeded)
        self.assertEqual((self.wld_dir / "index").read_text(encoding="latin-1"), "11.wld\n12.wld\n$\n")
        self.assertIn("index wld: 12.wld: the file is missing on 4810", run_log.getvalue())

    def test_fix_drops_them_and_succeeds(self) -> None:
        outcome = sync.check_indexes(self.paths, io.StringIO(), fix=True)

        self.assertEqual(outcome.message, "dropped 1 index entries: wld/12.wld")
        self.assertTrue(outcome.succeeded)
        self.assertEqual((self.wld_dir / "index").read_text(encoding="latin-1"), "11.wld\n$\n")

    def test_a_complete_world_is_reported_as_such(self) -> None:
        write_world_file(self.wld_dir, "12.wld", "#1201\n")

        outcome = sync.check_indexes(self.paths, io.StringIO(), fix=False)

        self.assertEqual(outcome.message, "every index entry names a file that exists")
        self.assertTrue(outcome.succeeded)


class WaitUntilTest(unittest.TestCase):
    def test_sleeps_until_the_target_without_overshooting_a_poll(self) -> None:
        clock = FakeClock(utc(10, 0, 58))

        sync.wait_until(utc(10, 2), clock.now, clock.sleep)

        self.assertEqual(clock.now(), utc(10, 2))


# ---------------------------------------------------------------------------------------------
# The command line
# ---------------------------------------------------------------------------------------------


class MainTest(TempDirTestCase):
    def setUp(self) -> None:
        super().setUp()
        # A throwaway port pair and home; the copy itself is replaced by fake_copy.
        self.paths = sync.SyncPaths(self.root / "4802", self.root / "4810", self.root / "home")
        self.paths.destination_port.mkdir()
        self.paths.home.mkdir()
        # The fake clock every main() call in a test shares.
        self.clock = FakeClock(utc(9, 50))

    def fake_copy(self, paths: "sync.SyncPaths", run_log, dry_run: bool) -> "sync.CopyReport":
        run_log.write("fake copy ran\n")
        return sync.CopyReport(5, 0, ())

    def run_main(self, *argv: str, copy=None) -> int:
        if copy is None:
            copy = self.fake_copy
        with contextlib.redirect_stdout(io.StringIO()):
            return sync.main(list(argv), self.paths, self.clock.now, self.clock.sleep, lambda pid: False, copy)

    def summary_lines(self) -> List[str]:
        return self.paths.summary_log.read_text().splitlines()

    def test_a_sync_writes_one_summary_line_and_a_run_log(self) -> None:
        exit_status = self.run_main()

        self.assertEqual(exit_status, 0)
        self.assertEqual(len(self.summary_lines()), 1)
        self.assertIn("synced: 5 files copied", self.summary_lines()[0])
        run_logs = list(self.paths.run_logs.iterdir())
        self.assertEqual(len(run_logs), 1)
        self.assertIn("fake copy ran", run_logs[0].read_text())

    def test_the_other_start_leaves_no_trace(self) -> None:
        self.clock = FakeClock(utc(8, 50))

        exit_status = self.run_main()

        self.assertEqual(exit_status, 0)
        self.assertFalse(self.paths.summary_log.exists())
        self.assertEqual(list(self.paths.run_logs.iterdir()), [])

    def test_a_failure_is_logged_with_its_step_and_exits_non_zero(self) -> None:
        self.paths.pause.write_text("held\n")

        exit_status = self.run_main()

        self.assertEqual(exit_status, 1)
        self.assertIn("failed: pause:", self.summary_lines()[0])

    def test_an_unexpected_error_is_logged_with_its_traceback(self) -> None:
        def broken_copy(paths, run_log, dry_run):
            raise RuntimeError("boom")

        exit_status = self.run_main(copy=broken_copy)

        self.assertEqual(exit_status, 1)
        self.assertIn("failed: unexpected error; see the run log", self.summary_lines()[0])
        run_log_text = next(self.paths.run_logs.iterdir()).read_text()
        self.assertIn("RuntimeError: boom", run_log_text)
        self.assertFalse(self.paths.pause.exists())

    def test_a_held_lock_skips_the_run(self) -> None:
        with self.paths.lock.open("a") as held_lock:
            fcntl.flock(held_lock, fcntl.LOCK_EX | fcntl.LOCK_NB)

            exit_status = self.run_main()

        self.assertEqual(exit_status, 0)
        self.assertIn("skipped: another sync holds the lock", self.summary_lines()[0])

    def test_run_logs_are_pruned_to_the_newest(self) -> None:
        self.paths.run_logs.mkdir()
        for minute in range(20):
            (self.paths.run_logs / f"20260901_00{minute:02d}00.log").write_text("")

        self.run_main()

        self.assertEqual(len(list(self.paths.run_logs.iterdir())), sync.KEPT_RUN_LOGS)

    def test_the_old_flags_are_gone(self) -> None:
        for old_flag in ("--dry-run", "--now"):
            with self.subTest(old_flag=old_flag):
                with contextlib.redirect_stderr(io.StringIO()):
                    with self.assertRaises(SystemExit):
                        self.run_main(old_flag)

    def test_preview_changes_nothing_and_names_its_log(self) -> None:
        exit_status = self.run_main("preview")

        self.assertEqual(exit_status, 0)
        self.assertIn("preview: a nightly run started now would hold the 10:00 UTC reboot; would have: 5 files",
                      self.summary_lines()[0])
        run_log_names = [path.name for path in self.paths.run_logs.iterdir()]
        self.assertEqual(len(run_log_names), 1)
        self.assertTrue(run_log_names[0].endswith("-preview.log"))
        self.assertFalse(self.paths.pause.exists())

    def test_copy_by_hand_of_a_stopped_port(self) -> None:
        exit_status = self.run_main("copy")

        self.assertEqual(exit_status, 0)
        self.assertIn("copy: synced: 5 files copied", self.summary_lines()[0])
        self.assertFalse(self.paths.pause.exists())

    def test_check_indexes_reports_by_default_and_fixes_on_request(self) -> None:
        wld_dir = self.paths.destination_lib / "world" / "wld"
        write_world_file(wld_dir, "index", "11.wld\n12.wld\n$\n")
        write_world_file(wld_dir, "11.wld", "#1101\n")

        report_status = self.run_main("check-indexes")
        fix_status = self.run_main("check-indexes", "--fix")

        self.assertEqual((report_status, fix_status), (1, 0))
        self.assertIn("check-indexes: 1 index entries name missing files: wld/12.wld", self.summary_lines()[0])
        self.assertIn("check-indexes: dropped 1 index entries: wld/12.wld", self.summary_lines()[1])
        self.assertEqual((wld_dir / "index").read_text(encoding="latin-1"), "11.wld\n$\n")

    def test_a_command_run_while_the_lock_is_held_exits_1_without_logging(self) -> None:
        errors = io.StringIO()
        with self.paths.lock.open("a") as held_lock:
            fcntl.flock(held_lock, fcntl.LOCK_EX | fcntl.LOCK_NB)

            with contextlib.redirect_stderr(errors):
                exit_status = self.run_main("copy")

        self.assertEqual(exit_status, 1)
        self.assertIn("another world sync holds the lock", errors.getvalue())
        self.assertFalse(self.paths.summary_log.exists())

    def test_status_shows_the_port_the_pause_and_recent_runs(self) -> None:
        self.paths.pause.write_text("")
        self.paths.summary_log.write_text("".join(f"run {number}\n" for number in range(8)))
        printed = io.StringIO()

        with contextlib.redirect_stdout(printed):
            exit_status = sync.main(["status"], self.paths, self.clock.now, self.clock.sleep, lambda pid: False,
                                    self.fake_copy)

        self.assertEqual(exit_status, 0)
        self.assertIn("4810: stopped", printed.getvalue())
        self.assertIn("Pause: held since", printed.getvalue())
        self.assertIn("run 7", printed.getvalue())
        self.assertNotIn("run 2", printed.getvalue())

    def test_a_termination_is_logged_and_releases_the_pause(self) -> None:
        def terminated_copy(paths, run_log, dry_run):
            raise SystemExit(128 + signal.SIGTERM)

        exit_status = self.run_main(copy=terminated_copy)

        self.assertEqual(exit_status, 128 + signal.SIGTERM)
        self.assertIn(f"failed: terminated (exit {128 + signal.SIGTERM})", self.summary_lines()[0])
        self.assertFalse(self.paths.pause.exists())

    def test_hangup_interrupt_and_terminate_all_unwind(self) -> None:
        installed = {}

        def record_handler(signal_number, handler):
            installed[signal_number] = handler

        with mock.patch.object(sync.signal, "signal", side_effect=record_handler):
            self.run_main()

        for signal_number in (signal.SIGTERM, signal.SIGHUP, signal.SIGINT):
            self.assertIs(installed[signal_number], sync.raise_system_exit)

    def test_main_sets_the_group_writable_umask(self) -> None:
        with mock.patch.object(sync.os, "umask", return_value=0o022) as umask:
            self.run_main()

        umask.assert_called_once_with(0o007)

    def test_sigterm_handler_unwinds_like_an_exception(self) -> None:
        pause = self.root / "pause"

        with self.assertRaises(SystemExit) as caught:
            with sync.PauseHold(pause):
                sync.raise_system_exit(signal.SIGTERM, None)

        self.assertEqual(caught.exception.code, 128 + signal.SIGTERM)
        self.assertFalse(pause.exists())


if __name__ == "__main__":
    unittest.main()
