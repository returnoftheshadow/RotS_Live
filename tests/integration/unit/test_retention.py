from __future__ import annotations

from pathlib import Path

from rots_harness.retention import RunDirectoryReport, keep_run_directory


def test_a_clean_run_with_no_failures_is_discarded() -> None:
    assert keep_run_directory(keep_requested=False, tests_failed_so_far=0, this_server_failed=False, interrupted=False) is False


def test_keep_requested_wins_regardless_of_outcome() -> None:
    assert keep_run_directory(keep_requested=True, tests_failed_so_far=0, this_server_failed=False, interrupted=False) is True


def test_an_earlier_failure_in_the_session_keeps_later_run_directories() -> None:
    assert keep_run_directory(keep_requested=False, tests_failed_so_far=1, this_server_failed=False, interrupted=False) is True


def test_a_crash_found_during_this_tests_own_teardown_keeps_its_directory() -> None:
    # pytest's session counter has not been incremented yet at this point, so the
    # per-server flag is the only signal.
    assert keep_run_directory(keep_requested=False, tests_failed_so_far=0, this_server_failed=True, interrupted=False) is True


def test_an_interrupted_run_keeps_its_directory() -> None:
    # An interrupted test is never counted as failed, so the interruption is its own reason.
    assert keep_run_directory(keep_requested=False, tests_failed_so_far=0, this_server_failed=False, interrupted=True) is True


def test_the_report_lists_each_kept_directory_with_its_test(tmp_path: Path) -> None:
    report = RunDirectoryReport()
    report.record_kept(tmp_path / "first", "scenarios/test_boot.py::test_one")
    report.record_kept(tmp_path / "second", "scenarios/test_boot.py::test_two")
    assert report.summary_lines() == [
        f"{tmp_path / 'first'}  scenarios/test_boot.py::test_one",
        f"{tmp_path / 'second'}  scenarios/test_boot.py::test_two",
    ]


def test_notes_are_listed_indented_under_their_directory(tmp_path: Path) -> None:
    report = RunDirectoryReport()
    report.record_kept(tmp_path / "first", "test_one", ["server process exited with status 0", "Testimm: server closed the connection; unread text:\nYou flee"])
    report.record_kept(tmp_path / "second", "test_two")
    assert report.summary_lines() == [
        f"{tmp_path / 'first'}  test_one",
        "    server process exited with status 0",
        "    Testimm: server closed the connection; unread text:",
        "    You flee",
        f"{tmp_path / 'second'}  test_two",
    ]


def test_the_report_notes_an_interrupt() -> None:
    report = RunDirectoryReport()
    assert report.interrupted is False
    report.pytest_keyboard_interrupt(excinfo=None)
    assert report.interrupted is True
