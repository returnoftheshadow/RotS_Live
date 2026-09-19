from __future__ import annotations

import subprocess
from pathlib import Path

from rots_harness.crashmonitor import CrashMonitor
from rots_harness.launcher import ServerHandle


class FakeProcess:
    def __init__(self, returncode: int | None = None) -> None:
        self.returncode = returncode

    def poll(self) -> int | None:
        return self.returncode


def make_handle(tmp_path: Path, returncode: int | None = None) -> ServerHandle:
    log_path = tmp_path / "game.log"
    log_path.write_text("", encoding="latin-1")
    return ServerHandle("127.0.0.1", 1, log_path, FakeProcess(returncode))  # type: ignore[arg-type]


def test_clean_log_reports_nothing(tmp_path: Path) -> None:
    handle = make_handle(tmp_path)
    handle.log_path.write_text("Sep 19 :: Boot db -- DONE.\n", encoding="latin-1")
    assert CrashMonitor(handle).check() == []


def test_allowed_syserr_lines_are_ignored_but_others_are_reported(tmp_path: Path) -> None:
    handle = make_handle(tmp_path)
    handle.log_path.write_text("x :: Could not open /judp/password, disabling JUDP.\nx :: SYSERR: boot error - 0 records counted\n", encoding="latin-1")
    problems = CrashMonitor(handle).check()
    assert len(problems) == 1 and "0 records counted" in problems[0]


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
    with handle.log_path.open("a", encoding="latin-1") as log_file:
        log_file.write("x :: SYSERR: second\n")
    assert len(monitor.check()) == 1
