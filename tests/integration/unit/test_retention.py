from __future__ import annotations

from rots_harness.retention import keep_run_directory


def test_a_clean_run_with_no_failures_is_discarded() -> None:
    assert keep_run_directory(keep_requested=False, tests_failed_so_far=0, this_server_failed=False) is False


def test_keep_requested_wins_regardless_of_outcome() -> None:
    assert keep_run_directory(keep_requested=True, tests_failed_so_far=0, this_server_failed=False) is True


def test_an_earlier_failure_in_the_session_keeps_later_run_directories() -> None:
    assert keep_run_directory(keep_requested=False, tests_failed_so_far=1, this_server_failed=False) is True


def test_a_crash_found_during_this_tests_own_teardown_keeps_its_directory() -> None:
    # pytest's session counter has not been incremented yet at this point, so the
    # per-server flag is the only signal.
    assert keep_run_directory(keep_requested=False, tests_failed_so_far=0, this_server_failed=True) is True
