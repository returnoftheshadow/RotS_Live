"""Decides whether a scenario's run directory survives fixture teardown."""

from __future__ import annotations


def keep_run_directory(keep_requested: bool, tests_failed_so_far: int, this_server_failed: bool) -> bool:
    """True when the directory must be kept for diagnosis.

    tests_failed_so_far is pytest's session counter. It is incremented only after every
    finalizer of the failing test has run, so a crash the autouse check finds during this
    test's own teardown is invisible in it; this_server_failed carries that case.

    A server that fails to start never reaches this decision: the fixture keeps that
    directory itself by not deleting it on the way out.
    """
    return keep_requested or tests_failed_so_far > 0 or this_server_failed
