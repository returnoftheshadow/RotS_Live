from __future__ import annotations

from pathlib import Path
from typing import Optional

import pytest

from rots_harness import ci_groups

INI_LINES = [
    "scenario: boots its own server through the server fixture",
    "boot: the server boots on a harness lib and characters log in",
    "characters(*characters): the characters the test's server is built with",
    "schedule(name): runs the test's server as ageland_harness with the named schedule",
    "poison: poison ticks once per game hour for its duration, then wears off",
    "summon: summon cannot reach a player in a dark room",
]
GROUPS = ["boot", "poison", "summon"]
SCENARIO_SOURCES = {
    "tests/integration/scenarios/test_poison.py": "pytestmark = [pytest.mark.scenario, pytest.mark.poison]\n",
    "tests/integration/scenarios/test_summon.py": "pytestmark = [pytest.mark.scenario, pytest.mark.summon]\n@pytest.mark.characters(CASTER)\n",
}


def read_scenario(path: str) -> Optional[str]:
    return SCENARIO_SOURCES.get(path)


def select(body: str = "", changed: tuple[str, ...] = (), event: str = "pull_request") -> ci_groups.Selection:
    return ci_groups.select_groups(event, body, changed, read_scenario, GROUPS)


def test_groups_are_the_markers_without_arguments_other_than_scenario() -> None:
    assert ci_groups.group_marker_names(INI_LINES) == GROUPS


def test_the_real_pytest_ini_registers_boot_as_a_group() -> None:
    ini_path = Path(__file__).resolve().parents[1] / "pytest.ini"
    assert ci_groups.ALWAYS_RUN_GROUP in ci_groups.group_marker_names(ci_groups.read_marker_lines(ini_path))


def test_a_plain_pull_request_runs_boot_only() -> None:
    selection = select(changed=("src/spells.cpp",))
    assert selection.groups == ["boot"]
    assert selection.expression() == "boot"
    assert selection.reasons == ["boot: always runs"]


def test_description_tokens_add_their_groups_in_order_without_repeats() -> None:
    selection = select(body="Fixes summon.\n\n@integration-test-summon @integration-test-poison\n@integration-test-summon")
    assert selection.expression() == "boot or summon or poison"
    assert selection.reasons[1:] == ["summon: named in the description", "poison: named in the description"]


def test_naming_boot_does_not_repeat_it() -> None:
    assert select(body="@integration-test-boot").groups == ["boot"]


def test_all_runs_every_group() -> None:
    selection = select(body="@integration-test-all")
    assert selection.groups is None
    assert selection.expression() == ""


def test_an_unknown_group_stops_the_run_and_lists_the_registered_ones() -> None:
    with pytest.raises(ci_groups.UnknownGroups, match=r"unknown integration groups: posion; registered groups: boot, poison, summon \(or all\)"):
        select(body="@integration-test-posion")


def test_tokens_inside_html_comments_are_ignored() -> None:
    body = "<!-- Name groups like @integration-test-poison, or @integration-test-all. -->\nNo groups here."
    assert select(body=body).groups == ["boot"]


@pytest.mark.parametrize("text", ["foo@integration-test-poison", "x-@integration-test-poison", "@integration-test-<group>", "integration-test-poison"])
def test_text_that_is_not_a_token_is_ignored(text: str) -> None:
    assert select(body=text).groups == ["boot"]


def test_a_token_ends_at_punctuation() -> None:
    assert select(body="Run (@integration-test-poison).").groups == ["boot", "poison"]


def test_a_changed_scenario_file_adds_the_groups_it_marks() -> None:
    selection = select(changed=("tests/integration/scenarios/test_summon.py",))
    assert selection.groups == ["boot", "summon"]
    assert selection.reasons[1] == "summon: test_summon.py changed"


def test_a_deleted_scenario_file_adds_nothing() -> None:
    assert select(changed=("tests/integration/scenarios/test_gone.py",)).groups == ["boot"]


def test_a_group_both_named_and_changed_is_listed_once() -> None:
    selection = select(body="@integration-test-poison", changed=("tests/integration/scenarios/test_poison.py",))
    assert selection.groups == ["boot", "poison"]
    assert selection.reasons == ["boot: always runs", "poison: named in the description"]


@pytest.mark.parametrize(
    "path",
    [
        "tests/integration/rots_harness/launcher.py",
        "tests/integration/world/zon/11.zon",
        "tests/integration/conftest.py",
        "tests/integration/pytest.ini",
        "tests/integration/requirements.txt",
        "tests/integration/scenarios/conftest.py",
        "src/harness/main.cpp",
        "src/tools/rotstool/main.cpp",
        "Dockerfile",
        ".github/workflows/integration.yml",
    ],
)
def test_a_harness_change_runs_every_group(path: str) -> None:
    selection = select(changed=("src/spells.cpp", path))
    assert selection.groups is None
    assert selection.reasons == [f"every group: harness file {path} changed"]


@pytest.mark.parametrize("path", ["tests/integration/unit/test_launcher.py", "tests/integration/README.md", "src/handler.cpp", ".github/workflows/ci.yml"])
def test_other_changes_do_not_run_every_group(path: str) -> None:
    assert select(changed=(path,)).groups == ["boot"]


def test_a_push_runs_every_group_whatever_the_description() -> None:
    selection = select(body="@integration-test-posion", event="push")
    assert selection.groups is None
    assert selection.reasons == ["every group: a push runs the whole suite"]


def test_an_unknown_group_is_reported_even_when_the_harness_changed() -> None:
    with pytest.raises(ci_groups.UnknownGroups):
        select(body="@integration-test-posion", changed=("tests/integration/conftest.py",))


def test_the_summary_names_what_runs_and_why() -> None:
    selection = select(body="@integration-test-poison")
    assert ci_groups.summary_markdown(selection) == "### Integration groups: `boot`, `poison`\n\n- boot: always runs\n- poison: named in the description\n"
    assert ci_groups.summary_markdown(select(body="@integration-test-all")).startswith("### Integration groups: every group\n")


def test_main_writes_the_group_and_summary_from_the_environment(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    repo_root = tmp_path / "repo"
    (repo_root / "tests/integration/scenarios").mkdir(parents=True)
    (repo_root / "tests/integration/pytest.ini").write_text("[pytest]\nmarkers =\n    " + "\n    ".join(INI_LINES) + "\n", encoding="utf-8")
    (repo_root / "tests/integration/scenarios/test_summon.py").write_text(SCENARIO_SOURCES["tests/integration/scenarios/test_summon.py"], encoding="utf-8")
    changed_files = tmp_path / "changed.txt"
    changed_files.write_text("src/spells.cpp\ntests/integration/scenarios/test_summon.py\n\n", encoding="utf-8")
    output = tmp_path / "output"
    output.write_text("earlier=line\n", encoding="utf-8")
    summary = tmp_path / "summary.md"
    monkeypatch.setenv("PR_BODY", "$(rm -rf /) @integration-test-poison")
    status = ci_groups.main([
        "--event", "pull_request", "--changed-files", str(changed_files), "--repo-root", str(repo_root),
        "--output", str(output), "--summary", str(summary),
    ])
    assert status == 0
    assert output.read_text(encoding="utf-8") == "earlier=line\ngroup=boot or poison or summon\n"
    assert summary.read_text(encoding="utf-8").startswith("### Integration groups: `boot`, `poison`, `summon`\n")


def test_main_fails_with_status_2_on_an_unknown_group(tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]) -> None:
    repo_root = tmp_path / "repo"
    (repo_root / "tests/integration").mkdir(parents=True)
    (repo_root / "tests/integration/pytest.ini").write_text("[pytest]\nmarkers =\n    " + "\n    ".join(INI_LINES) + "\n", encoding="utf-8")
    output = tmp_path / "output"
    monkeypatch.setenv("PR_BODY", "@integration-test-posion")
    status = ci_groups.main(["--event", "pull_request", "--repo-root", str(repo_root), "--output", str(output)])
    assert status == 2
    assert "unknown integration groups: posion" in capsys.readouterr().err
    assert not output.exists()
