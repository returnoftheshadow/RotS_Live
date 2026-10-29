"""Each scenario's recorded run time, kept in build/integration/durations.json so the run plan can estimate a run before it starts."""

from __future__ import annotations

import json
import math
import os
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path
from typing import Mapping, Optional

# The durations file, relative to the repository root: a JSON object of node id -> seconds.
DURATIONS_RELATIVE = Path("build") / "integration" / "durations.json"
# The marker of the tests whose times are recorded; the harness's own unit tests are not.
SCENARIO_MARKER = "scenario"
# The merged file's permissions; mkstemp's own let only the owner read it.
DURATIONS_FILE_MODE = 0o644
# Seconds the run plan counts for a test with no recorded time; CI's measured average was 14.8.
DEFAULT_TEST_SECONDS = 15.0
# pytest.ExitCode.INTERRUPTED: Ctrl-C, SIGTERM or SIGHUP in the controller or any pytest-xdist
# worker, pytest-xdist's -x, or a pytest.exit such as a Docker lock that cannot be taken.
INTERRUPTED_EXIT_STATUS = 2


def read_durations(path: Path) -> dict[str, float]:
    """The seconds recorded for each node id in path.

    A missing or unreadable file, or one that is not a JSON object, reads as empty; an entry whose
    value is not a finite number of seconds of at least 0 is left out.
    """
    try:
        content = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return {}
    if not isinstance(content, dict):
        return {}
    recorded = {}
    for node_id, seconds in content.items():
        # bool is an int to Python, but true is no number of seconds.
        if isinstance(seconds, (int, float)) and not isinstance(seconds, bool) and math.isfinite(seconds) and seconds >= 0:
            recorded[node_id] = float(seconds)
    return recorded


def merge_durations(path: Path, replacing: Mapping[str, float], raising: Optional[Mapping[str, float]] = None) -> None:
    """Writes the new times into path, keeping every other entry; raises OSError.

    A time in replacing replaces its test's entry. A time in raising only raises the estimate:
    it replaces a smaller recorded time, and gives a test with no recorded time an entry only
    above DEFAULT_TEST_SECONDS, which the run plan counts for it. The merged file is written
    beside path and then moved over it, so an interrupted write never leaves a broken file.
    Two runs finishing together can each miss the other's entries; the next run records them
    again.
    """
    merged = read_durations(path)
    merged.update(replacing)
    for node_id, seconds in (raising or {}).items():
        if seconds > merged.get(node_id, DEFAULT_TEST_SECONDS):
            merged[node_id] = seconds
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary_name = tempfile.mkstemp(prefix=path.name + ".", suffix=".tmp", dir=path.parent)
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8") as temporary:
            json.dump(merged, temporary, indent=1, sort_keys=True)
        os.chmod(temporary_name, DURATIONS_FILE_MODE)
        os.replace(temporary_name, path)
    except BaseException:
        Path(temporary_name).unlink(missing_ok=True)
        raise


@dataclass
class _TestTiming:
    """The phases one test has reported so far."""

    # Seconds of the phases reported so far.
    seconds: float
    # Whether every phase so far allows recording the test.
    usable: bool = True
    # Whether the call failed, so its time may only raise the recorded one; None before the call reports.
    call_failed: Optional[bool] = None


class DurationRecorder:
    """A pytest plugin recording the seconds of each scenario that ran to the end, merged into a durations file when the session ends.

    Registered in the process the run started in, which also receives the reports of pytest-xdist
    workers. A scenario counts once its teardown has passed, after a setup that passed and a call
    that passed, failed or failed as an expected failure (xfail); a skipped one, or one whose setup
    or teardown failed, does not. An interrupted session merges nothing: its failures can be
    side effects of the interrupt.
    """

    def __init__(self, path: Path) -> None:
        # The durations file merge_durations updates when the session ends.
        self._path = path
        # The phases of each test whose teardown has not yet reported.
        self._running: dict[str, _TestTiming] = {}
        # Seconds per node id of the tests that passed or failed as expected; they replace the recorded times.
        self.seconds: dict[str, float] = {}
        # Seconds per node id of the tests whose call failed; they only raise the recorded times.
        self.raising_seconds: dict[str, float] = {}

    def pytest_runtest_logreport(self, report) -> None:
        """Adds a scenario's reported phase to its time, and keeps the time once its teardown passes."""
        if SCENARIO_MARKER not in report.keywords:
            return
        if report.when == "setup":
            self._running[report.nodeid] = _TestTiming(report.duration, usable=bool(report.passed))
            return
        timing = self._running.get(report.nodeid)
        if timing is None:
            return
        timing.seconds += report.duration
        if report.when == "call":
            # An expected failure reports as skipped, with the xfail reason in wasxfail.
            expected_failure = report.skipped and hasattr(report, "wasxfail")
            if report.skipped and not expected_failure:
                timing.usable = False
            timing.call_failed = bool(report.failed)
            return
        del self._running[report.nodeid]
        if not report.passed or not timing.usable or timing.call_failed is None:
            return
        if timing.call_failed:
            self.raising_seconds[report.nodeid] = max(timing.seconds, self.raising_seconds.get(report.nodeid, 0.0))
        else:
            self.seconds[report.nodeid] = timing.seconds

    def pytest_sessionfinish(self, session, exitstatus: int) -> None:
        """Merges the times kept into the durations file unless the session was interrupted; a file that cannot be written only prints a warning."""
        if int(exitstatus) == INTERRUPTED_EXIT_STATUS:
            return
        if not self.seconds and not self.raising_seconds:
            return
        try:
            merge_durations(self._path, self.seconds, self.raising_seconds)
        except OSError as error:
            print(f"warning: cannot record the test times in {self._path}: {error}", file=sys.stderr, flush=True)
