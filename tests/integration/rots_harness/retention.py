"""Decides whether a scenario's run directory survives fixture teardown."""

from __future__ import annotations


def keep_run_directory(keep_requested: bool, tests_failed_so_far: int, this_server_failed: bool) -> bool:
    """True when the directory must be kept for diagnosis.

    tests_failed_so_far is pytest's session counter. A failure raised during teardown, such
    as the autouse crash check, is reported only after every finalizer has run, so it is not
    yet counted here; this_server_failed carries that case.

    A server that fails to start never reaches this decision: the fixture keeps that
    directory itself by not deleting it on the way out.
    """
    return keep_requested or tests_failed_so_far > 0 or this_server_failed
