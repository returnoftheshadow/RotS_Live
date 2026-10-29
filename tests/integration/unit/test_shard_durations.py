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


# A run with a plain skip, three unusable times and one good one.
ODD_RESULTS = """<?xml version="1.0" encoding="utf-8"?><testsuites name="pytest tests"><testsuite name="pytest">
<testcase classname="scenarios.test_bank" name="test_skipped" time="0.0"><skipped type="pytest.skip" message="not here" /></testcase>
<testcase classname="scenarios.test_bank" name="test_not_a_number" time="soon" />
<testcase classname="scenarios.test_bank" name="test_infinite" time="inf" />
<testcase classname="scenarios.test_bank" name="test_negative" time="-1.0" />
<testcase classname="scenarios.test_bank" name="test_a_deposit" time="10.0" />
</testsuite></testsuites>
"""


def refresh(integration_root: Path, tmp_path: Path, content: str, *extra_arguments: str) -> tuple[int, Path]:
    """Runs the tool on one shard holding content, writing tmp_path/shard_durations.json; returns its status and the file."""
    results = tmp_path / "results"
    if not results.exists():
        write_run(results, 1, content)
    output = tmp_path / "shard_durations.json"
    status = shard_durations.main([str(results), "--output", str(output), *extra_arguments], integration_root=integration_root)
    return status, output


def test_a_plain_skip_and_unusable_times_are_left_out_with_a_warning(integration_root: Path, tmp_path: Path, capsys: pytest.CaptureFixture) -> None:
    status, output = refresh(integration_root, tmp_path, ODD_RESULTS)
    assert status == 0
    assert durations.read_durations(output) == {"scenarios/test_bank.py::test_a_deposit": 10.0}
    errors = capsys.readouterr().err
    for name in ("test_not_a_number", "test_infinite", "test_negative"):
        assert f"{name} has time" in errors
    assert "test_skipped" not in errors


@pytest.mark.parametrize("markup", ['<!DOCTYPE lol [<!ENTITY a "aaaa">]>', '<!ENTITY a "aaaa">'])
def test_a_results_file_with_a_doctype_or_entity_is_refused(integration_root: Path, tmp_path: Path, markup: str, capsys: pytest.CaptureFixture) -> None:
    content = FIRST_RESULTS.replace("<testsuites", markup + "<testsuites", 1)
    status, output = refresh(integration_root, tmp_path, content)
    assert status == shard_durations.EXIT_USAGE
    assert not output.exists()
    assert "error:" in capsys.readouterr().err


def test_a_results_file_over_the_size_cap_is_refused(integration_root: Path, tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr(shard_durations, "MAX_RESULTS_BYTES", 100)
    status, output = refresh(integration_root, tmp_path, FIRST_RESULTS)
    assert status == shard_durations.EXIT_USAGE
    assert not output.exists()


@pytest.mark.parametrize("class_name", ["..integration.scenarios.test_bank", ".scenarios.test_bank", "scenarios..test_bank"])
def test_a_class_name_with_an_empty_part_has_no_node_id(integration_root: Path, class_name: str) -> None:
    assert shard_durations.node_id(class_name, "test_a_deposit", integration_root) is None


def test_a_module_outside_the_integration_folder_never_matches(integration_root: Path, tmp_path: Path) -> None:
    # A link inside the folder leading to a scenario file outside it.
    outside = tmp_path / "outside"
    outside.mkdir()
    (outside / "test_secret.py").write_text("", encoding="utf-8")
    (integration_root / "linked").symlink_to(outside, target_is_directory=True)
    assert shard_durations.node_id("linked.test_secret", "test_x", integration_root) is None


def test_a_refresh_covering_far_fewer_tests_is_refused_unless_forced(integration_root: Path, tmp_path: Path, capsys: pytest.CaptureFixture) -> None:
    output = tmp_path / "shard_durations.json"
    existing = {f"scenarios/test_bank.py::test_{index}": 5.0 for index in range(10)}
    output.write_text(json.dumps(existing), encoding="utf-8")
    status, _ = refresh(integration_root, tmp_path, FIRST_RESULTS)
    assert status == shard_durations.EXIT_USAGE
    assert durations.read_durations(output) == existing
    assert "--force" in capsys.readouterr().err
    status, _ = refresh(integration_root, tmp_path, FIRST_RESULTS, "--force")
    assert status == 0
    assert len(durations.read_durations(output)) == 3


def test_writing_leaves_no_temporary_file_behind(integration_root: Path, tmp_path: Path) -> None:
    status, output = refresh(integration_root, tmp_path, FIRST_RESULTS)
    assert status == 0
    assert sorted(path.name for path in tmp_path.iterdir()) == ["integration", "results", output.name]


# A results file whose DOCTYPE declares an entity that the testcase's time uses.
DOCTYPE_RESULTS = """<?xml version="1.0" encoding="{encoding}"?>
<!DOCTYPE testsuites [<!ENTITY seconds "1.0">]>
<testsuites><testsuite name="pytest"><testcase classname="scenarios.test_bank" name="test_a_deposit" time="&seconds;" /></testsuite></testsuites>
"""


def write_results_bytes(tmp_path: Path, content: bytes) -> Path:
    """Writes one shard's results.xml as raw bytes and returns the results folder."""
    shard_folder = tmp_path / "results" / "integration-results-shard-1"
    shard_folder.mkdir(parents=True)
    (shard_folder / shard_durations.RESULTS_FILE_NAME).write_bytes(content)
    return tmp_path / "results"


@pytest.mark.parametrize(
    ("encoding", "content_encoding"),
    [("UTF-16", "utf-16"), ("UTF-16", "utf-16-le"), ("UTF-16", "utf-16-be"), ("UTF-32", "utf-32"), ("UTF-8", "utf-8-sig")],
    ids=["utf-16 with mark", "utf-16-le without mark", "utf-16-be without mark", "utf-32", "utf-8 with mark"],
)
def test_a_doctype_in_a_file_not_plain_utf8_is_refused(
    integration_root: Path, tmp_path: Path, encoding: str, content_encoding: str, capsys: pytest.CaptureFixture,
) -> None:
    results = write_results_bytes(tmp_path, DOCTYPE_RESULTS.format(encoding=encoding).encode(content_encoding))
    output = tmp_path / "shard_durations.json"
    assert shard_durations.main([str(results), "--output", str(output)], integration_root=integration_root) == shard_durations.EXIT_USAGE
    assert not output.exists()
    assert "UTF-8" in capsys.readouterr().err


@pytest.mark.parametrize("content", [b"<testsuites>\xff</testsuites>", b'<?xml version="1.0" encoding="latin-1"?><testsuites/>'], ids=["invalid utf-8", "other encoding declared"])
def test_a_file_that_is_not_utf8_is_refused(integration_root: Path, tmp_path: Path, content: bytes) -> None:
    results = write_results_bytes(tmp_path, content)
    output = tmp_path / "shard_durations.json"
    assert shard_durations.main([str(results), "--output", str(output)], integration_root=integration_root) == shard_durations.EXIT_USAGE
    assert not output.exists()


def test_a_declared_utf8_file_is_read(integration_root: Path, tmp_path: Path) -> None:
    results = write_results_bytes(tmp_path, FIRST_RESULTS.replace('encoding="utf-8"', 'encoding="UTF-8"').encode("utf-8"))
    output = tmp_path / "shard_durations.json"
    assert shard_durations.main([str(results), "--output", str(output)], integration_root=integration_root) == 0
    assert len(durations.read_durations(output)) == 3
