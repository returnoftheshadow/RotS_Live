from __future__ import annotations

import json
import os
import stat
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Optional

import pytest

from rots_harness import durations

# tests/integration, which a pytest run in another folder needs on its path to import rots_harness.
INTEGRATION_ROOT = Path(__file__).resolve().parents[1]


@dataclass
class FakeReport:
    """The parts of a pytest TestReport the recorder reads."""

    # The test's node id.
    nodeid: str
    # The phase: setup, call or teardown.
    when: str
    # Seconds the phase took.
    duration: float
    # passed, failed or skipped, as pytest reports it.
    outcome: str = "passed"
    # The test's keywords, its marker names among them.
    keywords: dict = field(default_factory=lambda: {"scenario": 1})
    # The xfail reason pytest sets on a report of a test marked xfail, or None for no such attribute.
    xfail_reason: Optional[str] = None

    def __post_init__(self) -> None:
        if self.xfail_reason is not None:
            self.wasxfail = self.xfail_reason

    @property
    def passed(self) -> bool:
        return self.outcome == "passed"

    @property
    def failed(self) -> bool:
        return self.outcome == "failed"

    @property
    def skipped(self) -> bool:
        return self.outcome == "skipped"


def report_phases(recorder: durations.DurationRecorder, reports: list[FakeReport]) -> None:
    for report in reports:
        recorder.pytest_runtest_logreport(report)


def full_run(node_id: str, call_outcome: str = "passed", xfail_reason: Optional[str] = None) -> list[FakeReport]:
    """A test's three reports: 1 s of setup, 2.5 s of call with the given outcome, 0.5 s of teardown."""
    return [
        FakeReport(node_id, "setup", 1.0),
        FakeReport(node_id, "call", 2.5, outcome=call_outcome, xfail_reason=xfail_reason),
        FakeReport(node_id, "teardown", 0.5),
    ]


def test_a_missing_file_reads_as_empty(tmp_path: Path) -> None:
    assert durations.read_durations(tmp_path / "durations.json") == {}


@pytest.mark.parametrize("content", ["{not json", "[1, 2]", '"text"'])
def test_an_unreadable_file_reads_as_empty(tmp_path: Path, content: str) -> None:
    path = tmp_path / "durations.json"
    path.write_text(content, encoding="utf-8")
    assert durations.read_durations(path) == {}


def test_entries_that_are_not_a_number_of_seconds_are_ignored(tmp_path: Path) -> None:
    path = tmp_path / "durations.json"
    path.write_text(json.dumps({"good": 3.5, "whole": 2, "text": "slow", "flag": True, "negative": -1}), encoding="utf-8")
    assert durations.read_durations(path) == {"good": 3.5, "whole": 2.0}


def test_merge_keeps_untouched_entries_and_replaces_recorded_ones(tmp_path: Path) -> None:
    path = tmp_path / "durations.json"
    path.write_text(json.dumps({"kept": 4.0, "rerun": 9.0}), encoding="utf-8")
    durations.merge_durations(path, {"rerun": 2.5, "new": 1.0})
    assert durations.read_durations(path) == {"kept": 4.0, "rerun": 2.5, "new": 1.0}


def test_merge_over_an_unreadable_file_starts_afresh(tmp_path: Path) -> None:
    path = tmp_path / "durations.json"
    path.write_text("{broken", encoding="utf-8")
    durations.merge_durations(path, {"new": 1.0})
    assert durations.read_durations(path) == {"new": 1.0}


def test_merge_creates_the_folder(tmp_path: Path) -> None:
    path = tmp_path / "build" / "integration" / "durations.json"
    durations.merge_durations(path, {"new": 1.0})
    assert durations.read_durations(path) == {"new": 1.0}


def test_a_failed_replace_leaves_the_old_file_and_no_temporary_file(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    path = tmp_path / "durations.json"
    path.write_text(json.dumps({"kept": 4.0}), encoding="utf-8")

    def failing_replace(source, destination) -> None:
        raise OSError("disk full")

    monkeypatch.setattr(os, "replace", failing_replace)
    with pytest.raises(OSError):
        durations.merge_durations(path, {"new": 1.0})
    assert durations.read_durations(path) == {"kept": 4.0}
    assert [entry.name for entry in tmp_path.iterdir()] == ["durations.json"]


def test_the_recorder_adds_setup_call_and_teardown_of_a_test_that_passed(tmp_path: Path) -> None:
    recorder = durations.DurationRecorder(tmp_path / "durations.json")
    report_phases(recorder, full_run("first"))
    assert recorder.seconds == {"first": 4.0}
    assert recorder.raising_seconds == {}


def test_the_recorder_keeps_the_time_of_a_known_bug_that_failed_as_expected(tmp_path: Path) -> None:
    recorder = durations.DurationRecorder(tmp_path / "durations.json")
    report_phases(recorder, full_run("pinned", call_outcome="skipped", xfail_reason="a known bug"))
    assert recorder.seconds == {"pinned": 4.0}


def test_the_recorder_keeps_a_failed_test_only_as_a_time_that_can_raise(tmp_path: Path) -> None:
    recorder = durations.DurationRecorder(tmp_path / "durations.json")
    report_phases(recorder, full_run("broken", call_outcome="failed"))
    assert recorder.seconds == {}
    assert recorder.raising_seconds == {"broken": 4.0}


def test_the_recorder_ignores_a_test_whose_setup_failed(tmp_path: Path) -> None:
    recorder = durations.DurationRecorder(tmp_path / "durations.json")
    report_phases(recorder, [FakeReport("stale", "setup", 0.01, outcome="failed"), FakeReport("stale", "teardown", 0.0)])
    assert recorder.seconds == {}
    assert recorder.raising_seconds == {}


def test_the_recorder_ignores_a_test_skipped_at_setup_or_in_its_call(tmp_path: Path) -> None:
    recorder = durations.DurationRecorder(tmp_path / "durations.json")
    report_phases(recorder, [FakeReport("marked", "setup", 0.0, outcome="skipped"), FakeReport("marked", "teardown", 0.0)])
    report_phases(recorder, full_run("skips_itself", call_outcome="skipped"))
    assert recorder.seconds == {}
    assert recorder.raising_seconds == {}


def test_the_recorder_ignores_a_test_that_never_reported_its_teardown(tmp_path: Path) -> None:
    recorder = durations.DurationRecorder(tmp_path / "durations.json")
    report_phases(recorder, [FakeReport("interrupted", "setup", 20.0)])
    report_phases(recorder, full_run("interrupted_in_call")[:2])
    assert recorder.seconds == {}
    assert recorder.raising_seconds == {}


def test_the_recorder_ignores_a_test_whose_teardown_failed(tmp_path: Path) -> None:
    recorder = durations.DurationRecorder(tmp_path / "durations.json")
    reports = full_run("leaky")
    reports[2].outcome = "failed"
    report_phases(recorder, reports)
    assert recorder.seconds == {}
    assert recorder.raising_seconds == {}


def test_the_recorder_ignores_tests_that_are_not_scenarios(tmp_path: Path) -> None:
    recorder = durations.DurationRecorder(tmp_path / "durations.json")
    for report in full_run("unit/test_x.py::test_y"):
        report.keywords = {"test_y": 1}
        recorder.pytest_runtest_logreport(report)
    assert recorder.seconds == {}


def test_a_time_that_can_raise_only_raises_a_recorded_time(tmp_path: Path) -> None:
    path = tmp_path / "durations.json"
    path.write_text(json.dumps({"slow": 100.0, "quick": 10.0}), encoding="utf-8")
    durations.merge_durations(path, {}, raising={"slow": 3.0, "quick": 50.0})
    assert durations.read_durations(path) == {"slow": 100.0, "quick": 50.0}


def test_a_time_that_can_raise_records_an_untimed_test_only_above_the_default(tmp_path: Path) -> None:
    path = tmp_path / "durations.json"
    above_default = durations.DEFAULT_TEST_SECONDS + 25
    durations.merge_durations(path, {}, raising={"fails_fast": 0.5, "fails_slowly": above_default})
    assert durations.read_durations(path) == {"fails_slowly": above_default}


def test_the_session_merges_its_times_into_the_file(tmp_path: Path) -> None:
    path = tmp_path / "durations.json"
    path.write_text(json.dumps({"kept": 7.0}), encoding="utf-8")
    recorder = durations.DurationRecorder(path)
    report_phases(recorder, full_run("first"))
    recorder.pytest_sessionfinish(None, 0)
    assert durations.read_durations(path) == {"kept": 7.0, "first": 4.0}


def test_an_interrupted_session_leaves_the_file_alone(tmp_path: Path) -> None:
    path = tmp_path / "durations.json"
    path.write_text(json.dumps({"first": 300.0}), encoding="utf-8")
    recorder = durations.DurationRecorder(path)
    report_phases(recorder, full_run("first"))
    recorder.pytest_sessionfinish(None, durations.INTERRUPTED_EXIT_STATUS)
    assert durations.read_durations(path) == {"first": 300.0}


def test_a_file_that_cannot_be_written_only_prints_a_warning(tmp_path: Path, capsys: pytest.CaptureFixture) -> None:
    blocker = tmp_path / "not-a-folder"
    blocker.write_text("", encoding="utf-8")
    recorder = durations.DurationRecorder(blocker / "durations.json")
    report_phases(recorder, full_run("first"))
    recorder.pytest_sessionfinish(None, 0)
    assert "cannot record the test times" in capsys.readouterr().err


# A conftest.py for a pytest run in its own folder: registers the recorder in the controller only,
# as the harness's conftest.py does, and offers a session fixture that fails like a stale program.
RECORDING_CONFTEST = """
from pathlib import Path

import pytest

from rots_harness import durations


def pytest_configure(config):
    config.addinivalue_line("markers", "scenario: a scenario")
    if not hasattr(config, "workerinput"):
        config.pluginmanager.register(durations.DurationRecorder(Path(__file__).parent / "durations.json"))


@pytest.fixture(scope="session")
def stale_program():
    raise RuntimeError("the program is older than its sources")
"""

# Scenarios with every outcome a run records or ignores.
OUTCOME_TESTS = """
import pytest

pytestmark = pytest.mark.scenario


def test_passes():
    pass


@pytest.mark.xfail(reason="a known bug", strict=True)
def test_known_bug():
    assert False


def test_fails():
    assert False


def test_errors_at_setup(stale_program):
    pass
"""

# A scenario that passes, then one interrupted by Ctrl-C midway.
INTERRUPTED_TESTS = """
import signal

import pytest

pytestmark = pytest.mark.scenario


def test_passes():
    pass


def test_interrupted():
    signal.raise_signal(signal.SIGINT)
"""


def run_pytest_folder(folder: Path, tests: str, extra_arguments: list[str]) -> subprocess.CompletedProcess:
    """Runs pytest on tests in folder, with RECORDING_CONFTEST and rots_harness importable."""
    (folder / "pytest.ini").write_text("[pytest]\n", encoding="utf-8")
    (folder / "conftest.py").write_text(RECORDING_CONFTEST, encoding="utf-8")
    (folder / "test_recorded.py").write_text(tests, encoding="utf-8")
    environment = dict(os.environ)
    environment["PYTHONPATH"] = os.pathsep.join(filter(None, [str(INTEGRATION_ROOT), os.environ.get("PYTHONPATH")]))
    command = [sys.executable, "-m", "pytest", "-q", "-p", "no:cacheprovider", *extra_arguments, str(folder)]
    return subprocess.run(command, cwd=folder, env=environment, stdin=subprocess.DEVNULL, capture_output=True, text=True, timeout=60, check=False)


@pytest.mark.parametrize("extra_arguments", [[], ["-n", "1"]], ids=["one process", "xdist"])
def test_a_run_records_only_complete_times_and_never_lowers_one_with_a_failure(tmp_path: Path, extra_arguments: list[str]) -> None:
    path = tmp_path / "durations.json"
    path.write_text(json.dumps({"test_recorded.py::test_fails": 200.0, "test_recorded.py::test_errors_at_setup": 120.0}), encoding="utf-8")
    completed = run_pytest_folder(tmp_path, OUTCOME_TESTS, extra_arguments)
    assert completed.returncode == 1, completed.stdout + completed.stderr
    recorded = durations.read_durations(path)
    assert sorted(recorded) == ["test_recorded.py::test_errors_at_setup", "test_recorded.py::test_fails", "test_recorded.py::test_known_bug", "test_recorded.py::test_passes"]
    assert recorded["test_recorded.py::test_fails"] == 200.0
    assert recorded["test_recorded.py::test_errors_at_setup"] == 120.0


@pytest.mark.parametrize("extra_arguments", [[], ["-n", "1"]], ids=["one process", "xdist"])
def test_an_interrupted_run_records_nothing(tmp_path: Path, extra_arguments: list[str]) -> None:
    path = tmp_path / "durations.json"
    path.write_text(json.dumps({"test_recorded.py::test_passes": 300.0}), encoding="utf-8")
    completed = run_pytest_folder(tmp_path, INTERRUPTED_TESTS, extra_arguments)
    assert completed.returncode == durations.INTERRUPTED_EXIT_STATUS, completed.stdout + completed.stderr
    assert durations.read_durations(path) == {"test_recorded.py::test_passes": 300.0}


def test_the_merged_file_is_readable_by_others(tmp_path: Path) -> None:
    path = tmp_path / "durations.json"
    durations.merge_durations(path, {"new": 1.0})
    assert path.stat().st_mode & stat.S_IROTH
