from __future__ import annotations

import json
from pathlib import Path

import pytest

from rots_harness import shard_coverage

# Three selected tests, as the selection command lists them.
SELECTED = ["scenarios/test_boot.py::test_one", "scenarios/test_boot.py::test_two", "scenarios/test_bank.py::test_three"]


def test_every_selected_test_once_across_the_shards_has_no_problems() -> None:
    assert shard_coverage.coverage_problems(SELECTED, {1: SELECTED[:2], 2: SELECTED[2:]}, 2) == []


def test_an_empty_shard_with_results_is_not_a_problem() -> None:
    assert shard_coverage.coverage_problems(SELECTED, {1: SELECTED, 2: []}, 2) == []


def test_a_test_no_shard_ran_is_a_problem() -> None:
    assert shard_coverage.coverage_problems(SELECTED, {1: SELECTED[:1], 2: SELECTED[2:]}, 2) == [f"never ran: {SELECTED[1]}"]


def test_a_test_two_shards_ran_is_a_problem() -> None:
    problems = shard_coverage.coverage_problems(SELECTED, {1: SELECTED[:2], 2: SELECTED[1:]}, 2)
    assert problems == [f"ran in more than one shard: {SELECTED[1]} (2 shards)"]


def test_a_test_listed_twice_by_one_shard_counts_once() -> None:
    assert shard_coverage.coverage_problems(SELECTED, {1: [*SELECTED, SELECTED[0]]}, 1) == []


def test_a_test_outside_the_selection_is_a_problem() -> None:
    problems = shard_coverage.coverage_problems(SELECTED, {1: [*SELECTED, "scenarios/test_new.py::test_extra"]}, 1)
    assert problems == ["ran but was not selected: scenarios/test_new.py::test_extra"]


@pytest.mark.parametrize("shard_runs", [{1: SELECTED, 2: None}, {1: SELECTED}], ids=["missing", "absent"])
def test_a_shard_without_results_is_a_problem(shard_runs: dict) -> None:
    assert shard_coverage.coverage_problems(SELECTED, shard_runs, 2) == ["shard 2/2: no results"]


def test_a_long_list_of_problems_is_cut_short() -> None:
    selected = [f"scenarios/test_many.py::test_{index:03}" for index in range(shard_coverage.LISTED_IDS + 5)]
    problems = shard_coverage.coverage_problems(selected, {1: []}, 1)
    assert len(problems) == shard_coverage.LISTED_IDS + 1
    assert problems[-1] == "never ran: and 5 more"


def test_the_timing_report_counts_untimed_and_stale_entries() -> None:
    recorded = {SELECTED[0]: 5.0, "scenarios/test_gone.py::test_old": 9.0}
    lines = shard_coverage.timing_report(SELECTED, recorded)
    assert lines[0].startswith("3 selected tests; 2 have no time in")
    assert "1 of its entries name no selected test" in lines[0]
    assert f"- no time: `{SELECTED[1]}`" in lines
    assert "- no such test: `scenarios/test_gone.py::test_old`" in lines


def test_a_fully_timed_selection_asks_for_no_refresh() -> None:
    lines = shard_coverage.timing_report(SELECTED, {node_id: 1.0 for node_id in SELECTED})
    assert len(lines) == 1


def results_xml(cases: list[tuple[str, str]]) -> str:
    """A results.xml holding a passing testcase per class and test name."""
    body = "".join(f'<testcase classname="{class_name}" name="{name}" time="1.0" />' for class_name, name in cases)
    return f'<?xml version="1.0" encoding="utf-8"?><testsuites><testsuite name="pytest">{body}</testsuite></testsuites>'


def write_shard(folder: Path, number: int, cases: list[tuple[str, str]]) -> None:
    shard_folder = folder / shard_coverage.SHARD_FOLDER_FORMAT.format(number=number)
    shard_folder.mkdir(parents=True)
    (shard_folder / "results.xml").write_text(results_xml(cases), encoding="utf-8")


def run_check(tmp_path: Path, selected: list[str], shards: str) -> tuple[int, str]:
    selection = tmp_path / "selection.json"
    selection.write_text(json.dumps(selected), encoding="utf-8")
    summary = tmp_path / "summary.md"
    status = shard_coverage.main(["check", "--selection", str(selection), "--results", str(tmp_path / "results"), "--shards", shards, "--summary", str(summary)])
    return status, summary.read_text(encoding="utf-8") if summary.exists() else ""


def test_check_passes_when_the_real_scenario_files_ran_once(tmp_path: Path) -> None:
    # test_boot.py is a real scenario file, so the class name maps back to its node id.
    selected = ["scenarios/test_boot.py::test_a", "scenarios/test_boot.py::test_b"]
    write_shard(tmp_path / "results", 1, [("scenarios.test_boot", "test_a")])
    write_shard(tmp_path / "results", 2, [("scenarios.test_boot", "test_b")])
    status, summary = run_check(tmp_path, selected, "[1, 2]")
    assert status == 0
    assert "ran exactly once across 2 shards" in summary


def test_check_fails_for_a_missing_shard(tmp_path: Path) -> None:
    selected = ["scenarios/test_boot.py::test_a", "scenarios/test_boot.py::test_b"]
    write_shard(tmp_path / "results", 1, [("scenarios.test_boot", "test_a")])
    status, summary = run_check(tmp_path, selected, "[1, 2]")
    assert status == shard_coverage.EXIT_COVERAGE_FAILED
    assert "shard 2/2: no results" in summary
    assert "never ran: scenarios/test_boot.py::test_b" in summary


def test_check_fails_for_a_test_two_shards_ran(tmp_path: Path) -> None:
    selected = ["scenarios/test_boot.py::test_a", "scenarios/test_boot.py::test_b"]
    write_shard(tmp_path / "results", 1, [("scenarios.test_boot", "test_a"), ("scenarios.test_boot", "test_b")])
    write_shard(tmp_path / "results", 2, [("scenarios.test_boot", "test_a")])
    status, summary = run_check(tmp_path, selected, "[1, 2]")
    assert status == shard_coverage.EXIT_COVERAGE_FAILED
    assert "ran in more than one shard: scenarios/test_boot.py::test_a (2 shards)" in summary


# How pytest's JUnit file lists a test whose call failed and whose teardown then errored: two
# testcases with the same class and name, the first with the failure and the second with the error.
FAILED_THEN_TEARDOWN_ERROR_RESULTS = """<?xml version="1.0" encoding="utf-8"?><testsuites><testsuite name="pytest">
<testcase classname="scenarios.test_boot" name="test_a" time="3.0"><failure message="assert False">assert False</failure></testcase>
<testcase classname="scenarios.test_boot" name="test_a" time="0.5"><error message="failed on teardown">boom</error></testcase>
<testcase classname="scenarios.test_boot" name="test_b" time="1.0" />
</testsuite></testsuites>
"""


def test_check_passes_for_a_failed_test_whose_teardown_also_errored(tmp_path: Path) -> None:
    shard_folder = tmp_path / "results" / shard_coverage.SHARD_FOLDER_FORMAT.format(number=1)
    shard_folder.mkdir(parents=True)
    (shard_folder / "results.xml").write_text(FAILED_THEN_TEARDOWN_ERROR_RESULTS, encoding="utf-8")
    status, summary = run_check(tmp_path, ["scenarios/test_boot.py::test_a", "scenarios/test_boot.py::test_b"], "[1]")
    assert status == 0, summary


@pytest.mark.parametrize("shards", ["", "[]", "{}", '["1"]', "[true]"])
def test_check_refuses_shards_that_are_not_a_list_of_numbers(tmp_path: Path, shards: str) -> None:
    (tmp_path / "results").mkdir()
    status, _ = run_check(tmp_path, [], shards)
    assert status == shard_coverage.EXIT_USAGE
