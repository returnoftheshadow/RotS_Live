from __future__ import annotations

import json
from pathlib import Path

import pytest

from rots_harness import durations, shard_durations

# A results.xml as a CI shard writes it: two passing tests, one failing, one skipped.
FIRST_RESULTS = """<?xml version="1.0" encoding="utf-8"?><testsuites name="pytest tests"><testsuite name="pytest">
<testcase classname="scenarios.test_bank" name="test_a_deposit" time="10.0" />
<testcase classname="scenarios.test_bank" name="test_a_gift[a.b]" time="4.0" />
<testcase classname="scenarios.test_bank" name="test_a_refusal" time="2.0"><failure message="boom">boom</failure></testcase>
<testcase classname="scenarios.test_bank" name="test_a_pin" time="6.0"><skipped type="pytest.xfail" message="pinned" /></testcase>
</testsuite></testsuites>
"""
# The same tests in a later run: the failing test now passes.
SECOND_RESULTS = """<?xml version="1.0" encoding="utf-8"?><testsuites name="pytest tests"><testsuite name="pytest">
<testcase classname="scenarios.test_bank" name="test_a_deposit" time="12.0" />
<testcase classname="scenarios.test_bank" name="test_a_gift[a.b]" time="4.04" />
<testcase classname="scenarios.test_bank" name="test_a_refusal" time="3.0" />
<testcase classname="scenarios.test_gone" name="test_a_removed_file" time="9.0" />
</testsuite></testsuites>
"""
# A third run, so the deposit's median is its middle time.
THIRD_RESULTS = """<?xml version="1.0" encoding="utf-8"?><testsuites name="pytest tests"><testsuite name="pytest">
<testcase classname="scenarios.test_bank" name="test_a_deposit" time="30.0" />
</testsuite></testsuites>
"""


@pytest.fixture
def integration_root(tmp_path: Path) -> Path:
    """A tests/integration folder holding only scenarios/test_bank.py."""
    root = tmp_path / "integration"
    (root / "scenarios").mkdir(parents=True)
    (root / "scenarios" / "test_bank.py").write_text("", encoding="utf-8")
    return root


def write_run(folder: Path, shard: int, content: str) -> None:
    """Writes one shard's results.xml as gh run download lays it out."""
    shard_folder = folder / f"integration-results-shard-{shard}"
    shard_folder.mkdir(parents=True)
    (shard_folder / shard_durations.RESULTS_FILE_NAME).write_text(content, encoding="utf-8")


def test_a_module_level_test_gets_its_node_id(integration_root: Path) -> None:
    assert shard_durations.node_id("scenarios.test_bank", "test_a_gift[a.b]", integration_root) == "scenarios/test_bank.py::test_a_gift[a.b]"


def test_a_class_test_gets_its_class_in_the_node_id(integration_root: Path) -> None:
    assert shard_durations.node_id("scenarios.test_bank.TestTellers", "test_a_deposit", integration_root) == "scenarios/test_bank.py::TestTellers::test_a_deposit"


def test_a_class_name_matching_no_file_has_no_node_id(integration_root: Path) -> None:
    assert shard_durations.node_id("scenarios.test_gone", "test_a_removed_file", integration_root) is None


def test_each_test_gets_its_median_time_leaving_out_failed_runs(integration_root: Path, tmp_path: Path) -> None:
    results = tmp_path / "results"
    write_run(results / "first", 1, FIRST_RESULTS)
    write_run(results / "second", 2, SECOND_RESULTS)
    write_run(results / "third", 1, THIRD_RESULTS)
    output = tmp_path / "shard_durations.json"
    assert shard_durations.main([str(results), "--output", str(output)], integration_root=integration_root) == 0
    assert durations.read_durations(output) == {
        "scenarios/test_bank.py::test_a_deposit": 12.0,
        "scenarios/test_bank.py::test_a_gift[a.b]": 4.0,
        "scenarios/test_bank.py::test_a_refusal": 3.0,
        "scenarios/test_bank.py::test_a_pin": 6.0,
    }


def test_the_file_lists_one_test_per_line_sorted_by_node_id(integration_root: Path, tmp_path: Path) -> None:
    write_run(tmp_path / "results", 1, FIRST_RESULTS)
    output = tmp_path / "shard_durations.json"
    shard_durations.main([str(tmp_path / "results"), "--output", str(output)], integration_root=integration_root)
    lines = output.read_text(encoding="utf-8").splitlines()
    entries = [line for line in lines if line.startswith(' "')]
    assert entries == sorted(entries)
    assert len(entries) == len(json.loads(output.read_text(encoding="utf-8")))


def test_a_folder_with_no_results_changes_nothing(integration_root: Path, tmp_path: Path, capsys: pytest.CaptureFixture) -> None:
    output = tmp_path / "shard_durations.json"
    output.write_text("{}\n", encoding="utf-8")
    (tmp_path / "empty").mkdir()
    assert shard_durations.main([str(tmp_path / "empty"), "--output", str(output)], integration_root=integration_root) == shard_durations.EXIT_USAGE
    assert output.read_text(encoding="utf-8") == "{}\n"
    assert "no results.xml" in capsys.readouterr().err


@pytest.mark.parametrize("folder", ["", "missing"])
def test_an_empty_or_missing_folder_is_a_usage_error(integration_root: Path, tmp_path: Path, folder: str, capsys: pytest.CaptureFixture) -> None:
    argument = folder and str(tmp_path / folder)
    output = tmp_path / "shard_durations.json"
    assert shard_durations.main([argument, "--output", str(output)], integration_root=integration_root) == shard_durations.EXIT_USAGE
    assert not output.exists()
    assert "not a folder" in capsys.readouterr().err


def test_an_unreadable_results_file_is_a_usage_error(integration_root: Path, tmp_path: Path, capsys: pytest.CaptureFixture) -> None:
    write_run(tmp_path / "results", 1, "<testsuites>")
    output = tmp_path / "shard_durations.json"
    assert shard_durations.main([str(tmp_path / "results"), "--output", str(output)], integration_root=integration_root) == shard_durations.EXIT_USAGE
    assert not output.exists()
    assert "error:" in capsys.readouterr().err
