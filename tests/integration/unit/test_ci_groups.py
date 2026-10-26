from __future__ import annotations

from pathlib import Path
from typing import Optional

import pytest

from rots_harness import ci_groups, groups, libbuilder

# Each group file's description, by group name.
GROUP_DESCRIPTIONS = {
    "boot": "the server boots on a harness lib and characters log in",
    "poison": "poison ticks once per game hour for its duration, then wears off",
    "summon": "summon cannot reach a player in a dark room",
}
GROUPS = ["boot", "poison", "summon"]
SCENARIO_SOURCES = {
    "tests/integration/scenarios/test_poison.py": "pytestmark = [pytest.mark.scenario, pytest.mark.poison]\n",
    "tests/integration/scenarios/test_summon.py": "pytestmark = [pytest.mark.scenario, pytest.mark.summon]\n@pytest.mark.characters(CASTER)\n",
}


# The registered groups when shaping has two subgroups, in registration order.
GROUPS_WITH_SUBGROUPS = ["boot", "shaping", "shaping_commands", "shaping_zone_editor", "summon"]
# shaping's subgroups.
SUBGROUPS = {"shaping": ["shaping_commands", "shaping_zone_editor"]}
# A scenario file marking shaping for every test and a subgroup per test.
SPLIT_SCENARIO_SOURCE = (
    "pytestmark = [pytest.mark.scenario, pytest.mark.shaping]\n"
    "@pytest.mark.shaping_zone_editor\ndef test_one(server): pass\n"
    "@pytest.mark.shaping_commands\ndef test_two(server): pass\n"
)


def read_scenario(path: str) -> Optional[str]:
    return SCENARIO_SOURCES.get(path)


def select_with_subgroups(body: str = "", changed: tuple[str, ...] = (), sources: Optional[dict[str, str]] = None) -> ci_groups.Selection:
    """select_groups for a pull request over GROUPS_WITH_SUBGROUPS, reading scenario files from sources."""
    scenario_sources = sources or {}
    return ci_groups.select_groups("pull_request", body, changed, scenario_sources.get, GROUPS_WITH_SUBGROUPS, SUBGROUPS)


def select(body: str = "", changed: tuple[str, ...] = (), event: str = "pull_request") -> ci_groups.Selection:
    return ci_groups.select_groups(event, body, changed, read_scenario, GROUPS)


def write_repo(repo_root: Path) -> None:
    """A repository with the integration folder and one file per group in GROUP_DESCRIPTIONS."""
    groups_dir = repo_root / ci_groups.GROUPS_DIRECTORY
    groups_dir.mkdir(parents=True)
    (repo_root / "tests/integration/scenarios").mkdir(parents=True)
    for name, description in GROUP_DESCRIPTIONS.items():
        (groups_dir / f"{name}.txt").write_text(description + "\n", encoding="utf-8")


def test_the_real_group_folder_registers_boot() -> None:
    groups_dir = Path(__file__).resolve().parents[1] / "groups"
    assert ci_groups.ALWAYS_RUN_GROUP in groups.read_group_descriptions(groups_dir)


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


def test_a_changed_group_file_adds_its_group() -> None:
    selection = select(changed=("tests/integration/groups/poison.txt",))
    assert selection.groups == ["boot", "poison"]
    assert selection.reasons[1] == "poison: its group file poison.txt changed"


def test_a_deleted_group_file_adds_nothing() -> None:
    assert select(changed=("tests/integration/groups/gone.txt",)).groups == ["boot"]


@pytest.mark.parametrize("path", ["tests/integration/groups/README.md", "tests/integration/groups/nested/poison.txt"])
def test_other_files_in_the_group_folder_add_nothing(path: str) -> None:
    assert select(changed=(path,)).groups == ["boot"]


@pytest.mark.parametrize("path, name", [
    ("tests/integration/groups/shaping.txt", "shaping"),
    ("tests/integration/groups/shaping/zone_editor.txt", "shaping_zone_editor"),
    ("tests/integration/groups/shaping/deeper/zone_editor.txt", None),
    ("tests/integration/groups/shaping/README.md", None),
])
def test_a_group_file_path_names_its_group_or_subgroup(path: str, name: Optional[str]) -> None:
    assert ci_groups.changed_group_file(path) == name


def test_a_changed_subgroup_file_adds_its_subgroup() -> None:
    selection = select_with_subgroups(changed=("tests/integration/groups/shaping/zone_editor.txt",))
    assert selection.groups == ["boot", "shaping_zone_editor"]
    assert selection.reasons[1] == "shaping_zone_editor: its group file shaping/zone_editor.txt changed"


def test_a_scenario_file_marking_a_group_and_its_subgroups_adds_only_the_subgroups() -> None:
    sources = {"tests/integration/scenarios/test_shaping.py": SPLIT_SCENARIO_SOURCE}
    selection = select_with_subgroups(changed=tuple(sources), sources=sources)
    assert selection.groups == ["boot", "shaping_zone_editor", "shaping_commands"]
    assert selection.reasons[1:] == ["shaping_zone_editor: test_shaping.py changed", "shaping_commands: test_shaping.py changed"]


def test_a_scenario_file_marking_only_a_group_with_subgroups_adds_the_group() -> None:
    sources = {"tests/integration/scenarios/test_shaping.py": "pytestmark = [pytest.mark.scenario, pytest.mark.shaping]\n"}
    assert select_with_subgroups(changed=tuple(sources), sources=sources).groups == ["boot", "shaping"]


def test_scenario_groups_keeps_a_group_without_subgroups_beside_a_subgroup() -> None:
    source = SPLIT_SCENARIO_SOURCE + "@pytest.mark.summon\ndef test_three(server): pass\n"
    assert ci_groups.scenario_groups(source, GROUPS_WITH_SUBGROUPS, SUBGROUPS) == ["shaping_zone_editor", "shaping_commands", "summon"]


# A scenario file marking tame, which other scenario files import.
TAME_SCENARIO = "tests/integration/scenarios/test_tame.py"
# A scenario file marking summon that imports TAME_SCENARIO.
IMPORTER_SCENARIO = "tests/integration/scenarios/test_importer.py"
# The registered groups the import tests choose from.
IMPORT_GROUPS = ["boot", "poison", "shaping", "shaping_commands", "shaping_zone_editor", "summon", "tame"]


def select_with_importers(changed: tuple[str, ...], sources: dict[str, str]) -> ci_groups.Selection:
    """select_groups for a pull request over IMPORT_GROUPS, with every scenario file in sources in the checkout."""
    scenario_paths = sorted(sources)
    return ci_groups.select_groups("pull_request", "", changed, sources.get, IMPORT_GROUPS, SUBGROUPS, scenario_paths)


@pytest.mark.parametrize("import_line", [
    "from test_tame import TAMER",
    "from test_tame import (\n    TAMER,\n)",
    "import test_tame",
    "import test_tame as tame_scenario",
    "from . import test_tame",
    "from .test_tame import TAMER",
    "from scenarios import test_tame",
    "from scenarios.test_tame import TAMER",
    "def helper():\n    from test_tame import TAMER\n    return TAMER",
])
def test_a_changed_scenario_file_also_adds_the_groups_of_the_files_that_import_it(import_line: str) -> None:
    sources = {
        TAME_SCENARIO: "pytestmark = [pytest.mark.scenario, pytest.mark.tame]\n",
        IMPORTER_SCENARIO: f"{import_line}\npytestmark = [pytest.mark.scenario, pytest.mark.summon]\n",
    }
    selection = select_with_importers((TAME_SCENARIO,), sources)
    assert selection.groups == ["boot", "tame", "summon"]
    assert selection.reasons[2] == "summon: test_importer.py imports test_tame.py"


@pytest.mark.parametrize("import_line", ["from rots_harness import world", "import test_tame_extra", "from test_tamer import TAMER", "# from test_tame import TAMER"])
def test_a_file_importing_something_else_adds_nothing(import_line: str) -> None:
    sources = {
        TAME_SCENARIO: "pytestmark = [pytest.mark.scenario, pytest.mark.tame]\n",
        IMPORTER_SCENARIO: f"{import_line}\npytestmark = [pytest.mark.scenario, pytest.mark.summon]\n",
    }
    assert select_with_importers((TAME_SCENARIO,), sources).groups == ["boot", "tame"]


def test_a_deleted_scenario_file_still_adds_the_groups_of_the_files_that_import_it() -> None:
    sources = {IMPORTER_SCENARIO: "from test_tame import TAMER\npytestmark = [pytest.mark.scenario, pytest.mark.summon]\n"}
    assert select_with_importers((TAME_SCENARIO,), sources).groups == ["boot", "summon"]


def test_an_importer_that_does_not_parse_counts_as_importing() -> None:
    sources = {
        TAME_SCENARIO: "pytestmark = [pytest.mark.scenario, pytest.mark.tame]\n",
        IMPORTER_SCENARIO: "def broken(:\npytestmark = [pytest.mark.scenario, pytest.mark.summon]\n",
    }
    selection = select_with_importers((TAME_SCENARIO,), sources)
    assert selection.groups == ["boot", "tame", "summon"]
    assert selection.reasons[2] == "summon: test_importer.py does not parse, so it may import test_tame.py"


def test_an_importer_with_subgroups_adds_only_its_subgroups() -> None:
    sources = {
        TAME_SCENARIO: "pytestmark = [pytest.mark.scenario, pytest.mark.tame]\n",
        IMPORTER_SCENARIO: "from test_tame import TAMER\n" + SPLIT_SCENARIO_SOURCE,
    }
    assert select_with_importers((TAME_SCENARIO,), sources).groups == ["boot", "tame", "shaping_zone_editor", "shaping_commands"]


def test_an_importers_own_imports_are_not_followed() -> None:
    # One level: a file importing the importer is not reached.
    second = "tests/integration/scenarios/test_second.py"
    sources = {
        TAME_SCENARIO: "pytestmark = [pytest.mark.scenario, pytest.mark.tame]\n",
        IMPORTER_SCENARIO: "from test_tame import TAMER\npytestmark = [pytest.mark.scenario, pytest.mark.summon]\n",
        second: "from test_importer import TAMER\npytestmark = [pytest.mark.scenario, pytest.mark.poison]\n",
    }
    assert select_with_importers((TAME_SCENARIO,), sources).groups == ["boot", "tame", "summon"]


def test_scenario_file_paths_lists_the_test_files_in_the_scenarios_folder(tmp_path: Path) -> None:
    scenarios = tmp_path / "tests/integration/scenarios"
    scenarios.mkdir(parents=True)
    for name in ("test_b.py", "test_a.py", "conftest.py", "helpers.py", "test_notes.md"):
        (scenarios / name).write_text("", encoding="utf-8")
    assert ci_groups.scenario_file_paths(tmp_path) == ["tests/integration/scenarios/test_a.py", "tests/integration/scenarios/test_b.py"]


def test_a_subgroup_token_adds_the_subgroup() -> None:
    selection = select_with_subgroups(body="@integration-test-shaping_zone_editor")
    assert selection.groups == ["boot", "shaping_zone_editor"]
    assert selection.expression() == "boot or shaping_zone_editor"


def test_an_unknown_token_lists_the_subgroups_among_the_registered_groups() -> None:
    expected = r"unknown integration groups: shaping_zone; registered groups: boot, shaping, shaping_commands, shaping_zone_editor, summon \(or all\)"
    with pytest.raises(ci_groups.UnknownGroups, match=expected):
        select_with_subgroups(body="@integration-test-shaping_zone")


def test_a_group_file_changed_with_its_scenario_file_is_listed_once() -> None:
    selection = select(changed=("tests/integration/groups/summon.txt", "tests/integration/scenarios/test_summon.py"))
    assert selection.groups == ["boot", "summon"]
    assert selection.reasons == ["boot: always runs", "summon: its group file summon.txt changed"]


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
        ".github/actions/dev-image/action.yml",
    ],
)
def test_a_harness_change_runs_every_group(path: str) -> None:
    selection = select(changed=("src/spells.cpp", path))
    assert selection.groups is None
    assert selection.reasons == [f"every group: harness file {path} changed"]


@pytest.mark.parametrize(
    "path",
    [
        "tests/integration/unit/test_launcher.py",
        "tests/integration/README.md",
        "tests/integration/groups/boot.txt",
        "tests/integration/world/notes/zone-11.md",
        "src/handler.cpp",
        ".github/workflows/ci.yml",
        # The on-request ASan run builds its own programs and never changes how this workflow runs a scenario.
        ".github/workflows/asan.yml",
        "lib/misc/game_boot_options.json",
        "lib/plrobjs/a/alice",
        "lib/world/wld/11.wld",
        # A game header runs every group only locally (changed_groups); in CI it selects boot.
        "src/structs.h",
        # Only src/CMakeLists.txt, lib/text/ and the tracked lib/misc names match, not their look-alikes.
        "src/tools/CMakeLists.txt",
        "lib/text",
        "lib/textfoo",
        "lib/misc/mudlle2",
    ],
)
def test_other_changes_do_not_run_every_group(path: str) -> None:
    assert select(changed=(path,)).groups == ["boot"]


def test_a_changed_build_file_runs_every_group() -> None:
    selection = select(changed=("src/spells.cpp", "src/CMakeLists.txt"))
    assert selection.groups is None
    assert selection.reasons == ["every group: src/CMakeLists.txt changed, and its compile settings reach every program"]


@pytest.mark.parametrize("path", ["lib/text/news", "lib/text/help", "lib/text/help/spells", *(f"lib/misc/{name}" for name in libbuilder.TRACKED_MISC_FILES)])
def test_a_changed_lib_file_the_harness_copies_runs_every_group(path: str) -> None:
    selection = select(changed=("src/spells.cpp", path))
    assert selection.groups is None
    assert selection.reasons == [f"every group: {path} changed, and the harness copies it into every test's lib"]


def test_the_old_mudlle_file_the_harness_copies_runs_every_group() -> None:
    assert select(changed=("lib/misc/mudlle.old",)).groups is None


def test_read_changed_paths_keeps_a_newline_inside_one_path() -> None:
    assert ci_groups.read_changed_paths("docs/odd\nname.md\0src/spells.cpp\0") == ["docs/odd\nname.md", "src/spells.cpp"]


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
    write_repo(repo_root)
    (repo_root / "tests/integration/scenarios/test_summon.py").write_text(SCENARIO_SOURCES["tests/integration/scenarios/test_summon.py"], encoding="utf-8")
    changed_files = tmp_path / "changed.txt"
    changed_files.write_text("src/spells.cpp\0tests/integration/scenarios/test_summon.py\0", encoding="utf-8")
    output = tmp_path / "output"
    output.write_text("earlier=line\n", encoding="utf-8")
    summary = tmp_path / "summary.md"
    monkeypatch.setenv("PR_BODY", "$(rm -rf /) @integration-test-poison")
    status = ci_groups.main([
        "--event", "pull_request", "--changed-files", str(changed_files), "--repo-root", str(repo_root),
        "--output", str(output), "--summary", str(summary), "--full-suite-shards", "8",
    ])
    assert status == 0
    assert output.read_text(encoding="utf-8") == "earlier=line\ngroup=boot or poison or summon\nshards=[1]\n"
    assert summary.read_text(encoding="utf-8").startswith("### Integration groups: `boot`, `poison`, `summon`\n")
    assert "parallel shards" not in summary.read_text(encoding="utf-8")


def test_main_reads_the_subgroups_from_the_group_folder(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    repo_root = tmp_path / "repo"
    write_repo(repo_root)
    groups_dir = repo_root / ci_groups.GROUPS_DIRECTORY
    (groups_dir / "shaping.txt").write_text("the shaping editors\n", encoding="utf-8")
    (groups_dir / "shaping").mkdir()
    for name in ("commands", "zone_editor"):
        (groups_dir / "shaping" / f"{name}.txt").write_text(f"the {name}\n", encoding="utf-8")
    scenario = "tests/integration/scenarios/test_shaping.py"
    (repo_root / scenario).write_text(SPLIT_SCENARIO_SOURCE, encoding="utf-8")
    changed_files = tmp_path / "changed"
    changed_files.write_text(f"{scenario}\0", encoding="utf-8")
    output = tmp_path / "output"
    monkeypatch.setenv("PR_BODY", "")
    status = ci_groups.main(["--event", "pull_request", "--changed-files", str(changed_files), "--repo-root", str(repo_root), "--output", str(output)])
    assert status == 0
    assert output.read_text(encoding="utf-8") == "group=boot or shaping_zone_editor or shaping_commands\nshards=[1]\n"


def test_main_adds_the_groups_of_scenario_files_that_import_a_changed_one(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    repo_root = tmp_path / "repo"
    write_repo(repo_root)
    (repo_root / "tests/integration/scenarios/test_poison.py").write_text(SCENARIO_SOURCES["tests/integration/scenarios/test_poison.py"], encoding="utf-8")
    importer = "from test_poison import DOSE\n" + SCENARIO_SOURCES["tests/integration/scenarios/test_summon.py"]
    (repo_root / "tests/integration/scenarios/test_summon.py").write_text(importer, encoding="utf-8")
    listing = b"tests/integration/scenarios/test_poison.py\0"
    assert run_main_on_changed_files(tmp_path, monkeypatch, listing) == "group=boot or poison or summon\nshards=[1]\n"


def run_main_on_changed_files(tmp_path: Path, monkeypatch: pytest.MonkeyPatch, listing: bytes) -> str:
    """Runs main for a pull request with no description on a changed-files file holding listing; returns the output file's text."""
    changed_files = tmp_path / "changed"
    changed_files.write_bytes(listing)
    output = tmp_path / "output"
    monkeypatch.setenv("PR_BODY", "")
    status = ci_groups.main([
        "--event", "pull_request", "--changed-files", str(changed_files), "--repo-root", str(tmp_path / "repo"),
        "--output", str(output), "--full-suite-shards", "8",
    ])
    assert status == 0
    return output.read_text(encoding="utf-8")


def test_main_reads_nul_separated_paths_whole(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    # git diff -z lists a path with a space or a non-ASCII letter unquoted; neither may split or miss its rule.
    scenario = "tests/integration/scenarios/test_sümmon spells.py"
    write_repo(tmp_path / "repo")
    (tmp_path / "repo" / scenario).write_text(SCENARIO_SOURCES["tests/integration/scenarios/test_summon.py"], encoding="utf-8")
    listing = f"docs/release notes.md\0{scenario}\0".encode("utf-8")
    assert run_main_on_changed_files(tmp_path, monkeypatch, listing) == "group=boot or summon\nshards=[1]\n"


def test_main_runs_every_group_for_a_path_renamed_out_of_the_harness(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    # With --no-renames git lists a move from src/harness/main.cpp to docs/main.cpp as both paths.
    write_repo(tmp_path / "repo")
    output = run_main_on_changed_files(tmp_path, monkeypatch, b"docs/main.cpp\0src/harness/main.cpp\0")
    assert output == "group=\nshards=[1, 2, 3, 4, 5, 6, 7, 8]\n"


def test_main_reads_an_empty_changed_files_file_as_no_paths(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    write_repo(tmp_path / "repo")
    assert run_main_on_changed_files(tmp_path, monkeypatch, b"") == "group=boot\nshards=[1]\n"


def test_main_reads_newlines_as_part_of_a_path(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    # The file is NUL-separated only, so a newline-separated list is one path that matches no rule.
    write_repo(tmp_path / "repo")
    listing = b"src/spells.cpp\nsrc/harness/main.cpp\n"
    assert run_main_on_changed_files(tmp_path, monkeypatch, listing) == "group=boot\nshards=[1]\n"


def test_main_reads_a_newline_inside_a_nul_terminated_path_as_part_of_it(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    # One path, docs/<newline>src/harness/main.cpp, so the harness rule does not see src/harness/main.cpp.
    write_repo(tmp_path / "repo")
    listing = b"docs/\nsrc/harness/main.cpp\0"
    assert run_main_on_changed_files(tmp_path, monkeypatch, listing) == "group=boot\nshards=[1]\n"


def test_main_adds_no_group_for_a_deleted_scenario_file(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    # The changed scenario file is not in the checkout, so read_scenario returns None.
    write_repo(tmp_path / "repo")
    listing = b"tests/integration/scenarios/test_summon.py\0"
    assert run_main_on_changed_files(tmp_path, monkeypatch, listing) == "group=boot\nshards=[1]\n"


def test_main_escapes_a_file_name_that_is_not_utf8_in_the_summary_and_its_output(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str],
) -> None:
    # git -z lists the name's bytes as they are; the reason quoting it must not stop the job.
    write_repo(tmp_path / "repo")
    changed_files = tmp_path / "changed"
    changed_files.write_bytes(b"lib/text/caf\xe9\0")
    output = tmp_path / "output"
    summary = tmp_path / "summary.md"
    monkeypatch.setenv("PR_BODY", "")
    status = ci_groups.main([
        "--event", "pull_request", "--changed-files", str(changed_files), "--repo-root", str(tmp_path / "repo"),
        "--output", str(output), "--summary", str(summary),
    ])
    assert status == 0
    assert output.read_text(encoding="utf-8") == "group=\nshards=[1]\n"
    escaped_reason = "every group: lib/text/caf\\udce9 changed"
    assert escaped_reason in summary.read_text(encoding="utf-8")
    assert escaped_reason in capsys.readouterr().out


def test_main_shards_a_run_of_every_group(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    repo_root = tmp_path / "repo"
    write_repo(repo_root)
    output = tmp_path / "output"
    summary = tmp_path / "summary.md"
    monkeypatch.setenv("PR_BODY", "@integration-test-all")
    status = ci_groups.main([
        "--event", "pull_request", "--repo-root", str(repo_root), "--output", str(output), "--summary", str(summary),
        "--full-suite-shards", "8",
    ])
    assert status == 0
    assert output.read_text(encoding="utf-8") == "group=\nshards=[1, 2, 3, 4, 5, 6, 7, 8]\n"
    assert summary.read_text(encoding="utf-8").endswith("\nSplit across 8 parallel shards.\n")


def test_main_refuses_fewer_than_one_full_suite_shard(tmp_path: Path) -> None:
    with pytest.raises(SystemExit):
        ci_groups.main(["--event", "push", "--output", str(tmp_path / "output"), "--full-suite-shards", "0"])


def test_only_a_run_of_every_group_is_sharded() -> None:
    assert select(event="push").shard_numbers(8) == [1, 2, 3, 4, 5, 6, 7, 8]
    assert select(body="@integration-test-all").shard_numbers(3) == [1, 2, 3]
    assert select(body="@integration-test-poison").shard_numbers(8) == [1]
    assert select().shard_numbers(8) == [1]


def test_main_fails_with_status_2_on_an_unknown_group(tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]) -> None:
    repo_root = tmp_path / "repo"
    write_repo(repo_root)
    output = tmp_path / "output"
    monkeypatch.setenv("PR_BODY", "@integration-test-posion")
    status = ci_groups.main(["--event", "pull_request", "--repo-root", str(repo_root), "--output", str(output)])
    assert status == 2
    assert "unknown integration groups: posion" in capsys.readouterr().err
    assert not output.exists()


def test_main_fails_with_status_2_on_an_unreadable_group_file(tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]) -> None:
    repo_root = tmp_path / "repo"
    write_repo(repo_root)
    (repo_root / ci_groups.GROUPS_DIRECTORY / "bad-name.txt").write_text("a group\n", encoding="utf-8")
    output = tmp_path / "output"
    monkeypatch.setenv("PR_BODY", "")
    status = ci_groups.main(["--event", "pull_request", "--repo-root", str(repo_root), "--output", str(output)])
    assert status == 2
    assert "cannot read the scenario groups" in capsys.readouterr().err
    assert not output.exists()
