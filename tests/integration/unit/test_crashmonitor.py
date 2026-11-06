from __future__ import annotations

from pathlib import Path

import pytest

from rots_harness import crashmonitor
from rots_harness.crashmonitor import CrashMonitor
from rots_harness.launcher import ServerHandle


class FakeProcess:
    """Stands in for a Popen: poll() reports a settable exit status."""

    def __init__(self, returncode: int | None = None) -> None:
        # The exit status poll() reports; None while the fake process is running.
        self.returncode = returncode

    def poll(self) -> int | None:
        return self.returncode


def make_handle(tmp_path: Path, returncode: int | None = None) -> ServerHandle:
    """A handle over an empty log file and a fake process with the given exit status."""
    log_path = tmp_path / "game.log"
    log_path.write_text("", encoding="latin-1")
    return ServerHandle("127.0.0.1", 1, log_path, FakeProcess(returncode))  # type: ignore[arg-type]


def append_log(handle: ServerHandle, text: str) -> None:
    """Appends text to the handle's log."""
    with handle.log_path.open("a", encoding="latin-1") as log_file:
        log_file.write(text)


def test_clean_log_reports_nothing(tmp_path: Path) -> None:
    handle = make_handle(tmp_path)
    handle.log_path.write_text("Sep 19 :: Boot db -- DONE.\n", encoding="latin-1")
    assert CrashMonitor(handle).check() == []


def test_the_default_list_allows_no_syserr() -> None:
    assert crashmonitor.DEFAULT_ALLOWED == ()


def test_harmless_boot_lines_are_not_problems(tmp_path: Path) -> None:
    handle = make_handle(tmp_path)
    handle.log_path.write_text(
        "x :: file_to_string_read_lines: could not open ../log/lastdeath\n"
        "x :: Could not open /judp/password, disabling JUDP.\n"
        "x :: Error reading board: No such file or directory\n",
        encoding="latin-1",
    )
    assert CrashMonitor(handle).check() == []


def test_allowed_syserr_lines_are_ignored_but_others_are_reported(tmp_path: Path) -> None:
    handle = make_handle(tmp_path)
    handle.log_path.write_text(
        "x :: SYSERR: tolerated fragment here\nx :: SYSERR: boot error - 0 records counted\n",
        encoding="latin-1",
    )
    problems = CrashMonitor(handle, allowed_syserr_fragments=("tolerated fragment",)).check()
    assert len(problems) == 1 and "0 records counted" in problems[0]


def test_object_refresh_syserr_is_reported_by_default(tmp_path: Path) -> None:
    handle = make_handle(tmp_path)
    handle.log_path.write_text(
        "x :: SYSERR: failed to refresh account-native object file for Testmage: Permission denied\n",
        encoding="latin-1",
    )
    problems = CrashMonitor(handle).check()
    assert len(problems) == 1 and "Permission denied" in problems[0]


def test_sanitizer_report_and_process_exit_are_reported(tmp_path: Path) -> None:
    handle = make_handle(tmp_path, returncode=-11)
    handle.log_path.write_text("==12==ERROR: AddressSanitizer: heap-use-after-free on address\n", encoding="latin-1")
    problems = CrashMonitor(handle).check()
    assert any("AddressSanitizer" in problem for problem in problems)
    assert any("exited" in problem for problem in problems)


def test_check_only_reports_new_lines(tmp_path: Path) -> None:
    handle = make_handle(tmp_path)
    monitor = CrashMonitor(handle)
    handle.log_path.write_text("x :: SYSERR: first\n", encoding="latin-1")
    assert len(monitor.check()) == 1
    assert monitor.check() == []
    append_log(handle, "x :: SYSERR: second\n")
    assert len(monitor.check()) == 1


def test_missing_log_file_reports_nothing_until_the_process_exits(tmp_path: Path) -> None:
    process = FakeProcess()
    handle = ServerHandle("127.0.0.1", 1, tmp_path / "missing.log", process)  # type: ignore[arg-type]
    monitor = CrashMonitor(handle)
    assert monitor.check() == []

    process.returncode = 0
    problems = monitor.check()
    assert len(problems) == 1
    assert "exited" in problems[0]


def test_clean_shutdown_with_only_the_signal_notice_reports_nothing(tmp_path: Path) -> None:
    handle = make_handle(tmp_path, returncode=0)
    append_log(handle, "Sep 24 :: Received SIGHUP, SIGINT, or SIGTERM.  Shutting down...\n")
    assert CrashMonitor(handle).check_after_stop() == []


def test_sanitizer_report_written_during_shutdown_is_reported(tmp_path: Path) -> None:
    handle = make_handle(tmp_path, returncode=0)
    append_log(
        handle,
        "Sep 24 :: Received SIGHUP, SIGINT, or SIGTERM.  Shutting down...\n"
        "==12==ERROR: AddressSanitizer: heap-use-after-free on address\n",
    )
    problems = CrashMonitor(handle).check_after_stop()
    assert len(problems) == 1, problems
    assert problems[0].startswith("crash marker in log") and "AddressSanitizer" in problems[0], problems


def test_signal_death_after_stop_is_reported_with_its_status(tmp_path: Path) -> None:
    handle = make_handle(tmp_path, returncode=137)
    problems = CrashMonitor(handle).check_after_stop()
    assert len(problems) == 1, problems
    assert "137" in problems[0], problems


def test_process_still_running_after_stop_is_reported(tmp_path: Path) -> None:
    handle = make_handle(tmp_path, returncode=None)
    problems = CrashMonitor(handle).check_after_stop()
    assert len(problems) == 1, problems
    assert "still running" in problems[0], problems


def test_each_crash_line_is_reported_once_across_check_and_check_after_stop(tmp_path: Path) -> None:
    handle = make_handle(tmp_path, returncode=None)
    monitor = CrashMonitor(handle)
    append_log(handle, "Sep 24 :: SYSERR: during the test\n")
    during_test = monitor.check()
    assert len(during_test) == 1 and "during the test" in during_test[0], during_test

    append_log(
        handle,
        "Sep 24 :: Received SIGHUP, SIGINT, or SIGTERM.  Shutting down...\n"
        "example.cpp:12:5: runtime error: signed integer overflow\n",
    )
    handle.process.returncode = 0  # type: ignore[attr-defined]
    after_stop = monitor.check_after_stop()
    assert after_stop == ["crash marker in log: example.cpp:12:5: runtime error: signed integer overflow"], (
        f"only the line written during shutdown belongs to the post-stop check: {after_stop}"
    )


def test_exit_already_reported_during_the_test_is_not_reported_again_after_stop(tmp_path: Path) -> None:
    handle = make_handle(tmp_path, returncode=-11)
    monitor = CrashMonitor(handle)
    assert any("exited" in problem for problem in monitor.check())
    assert monitor.check_after_stop() == [], "the mid-test exit is already the test's failure"


def test_the_fatal_signal_line_is_a_crash_marker(tmp_path: Path) -> None:
    handle = make_handle(tmp_path)
    append_log(handle, "Sep 24 :: Error: signal 11\n")
    assert CrashMonitor(handle).check() == ["crash marker in log: Sep 24 :: Error: signal 11"]


def test_a_syserr_line_split_across_two_checks_is_reported_once_whole(tmp_path: Path) -> None:
    handle = make_handle(tmp_path)
    monitor = CrashMonitor(handle)
    append_log(handle, "Sep 24 :: SYSERR: half a")
    assert monitor.check() == [], "an unterminated line waits for the rest"
    append_log(handle, " line\n")
    assert monitor.check() == ["unexpected SYSERR: Sep 24 :: SYSERR: half a line"]
    assert monitor.check() == []


def test_an_unterminated_last_line_is_checked_after_stop(tmp_path: Path) -> None:
    handle = make_handle(tmp_path, returncode=0)
    append_log(handle, "Sep 24 :: Received SIGHUP, SIGINT, or SIGTERM.  Shutting down...\nSep 24 :: SYSERR: no newline")
    assert CrashMonitor(handle).check_after_stop() == ["unexpected SYSERR: Sep 24 :: SYSERR: no newline"]


def test_exit_status_zero_without_the_shutdown_notice_is_reported(tmp_path: Path) -> None:
    handle = make_handle(tmp_path, returncode=0)
    append_log(handle, "Sep 24 :: Boot db -- DONE.\n")
    assert CrashMonitor(handle).check_after_stop() == ["server exited with status 0 without logging the shutdown notice"]


def test_the_shutdown_notice_seen_by_an_earlier_check_still_counts_after_stop(tmp_path: Path) -> None:
    handle = make_handle(tmp_path, returncode=None)
    monitor = CrashMonitor(handle)
    append_log(handle, "Sep 24 :: Received SIGHUP, SIGINT, or SIGTERM.  Shutting down...\n")
    monitor.check()
    handle.process.returncode = 0  # type: ignore[attr-defined]
    assert monitor.check_after_stop() == []


@pytest.mark.parametrize("marker", ["AddressSanitizer", "LeakSanitizer", "UndefinedBehaviorSanitizer", "runtime error:"])
def test_each_sanitizer_marker_is_a_crash_marker_on_its_own(tmp_path: Path, marker: str) -> None:
    handle = make_handle(tmp_path)
    append_log(handle, f"Sep 24 :: {marker} found something\n")
    assert CrashMonitor(handle).check() == [f"crash marker in log: Sep 24 :: {marker} found something"]
