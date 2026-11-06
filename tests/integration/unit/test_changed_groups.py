from __future__ import annotations

import subprocess
from pathlib import Path
from typing import Optional

import pytest

from rots_harness import changed_groups, ci_groups, libbuilder

# The registered groups select_local_groups chooses from.
GROUPS = ["boot", "poison", "summon"]
# Each group file's description, by group name.
GROUP_DESCRIPTIONS = {
    "boot": "the server boots on a harness lib and characters log in",
    "poison": "poison ticks once per game hour for its duration, then wears off",
    "summon": "summon cannot reach a player in a dark room",
}
# The one scenario file read_scenario knows, marking the poison group.
POISON_SCENARIO = "tests/integration/scenarios/test_poison.py"
# The poison scenario's source.
POISON_SOURCE = "pytestmark = [pytest.mark.scenario, pytest.mark.poison]\n"
# The base the tests name, and the commit it resolves to.
BASE = "origin/release-frodo-next"
BASE_COMMIT = "0123456789abcdef0123456789abcdef01234567"
# The git argument lists changed_paths runs: committed changes since the base's merge base,
# uncommitted changes to tracked files, and untracked files.
COMMITTED_COMMAND = ["diff", "--name-only", "-z", "--no-renames", f"{BASE_COMMIT}...HEAD"]
UNCOMMITTED_COMMAND = ["diff", "--name-only", "-z", "--no-renames", "HEAD"]
UNTRACKED_COMMAND = ["ls-files", "-z", "--others", "--exclude-standard", "--full-name"]
# The git argument list resolve_base runs for BASE.
RESOLVE_COMMAND = ["rev-parse", "--verify", "--end-of-options", f"{BASE}^{{commit}}"]


def read_scenario(path: str) -> Optional[str]:
    return POISON_SOURCE if path == POISON_SCENARIO else None


def select(*changed: str) -> ci_groups.Selection:
    return changed_groups.select_local_groups(changed, read_scenario, GROUPS)


class FakeGit:
    """Answers the git commands changed_paths runs with canned output, recording each argument list."""

    def __init__(self, outputs: dict[tuple[str, ...], str]) -> None:
        # The standard output each git argument list prints.
        self._outputs = outputs
        # Every argument list run, in order.
        self.commands: list[list[str]] = []

    def __call__(self, arguments: list[str]) -> str:
        self.commands.append(arguments)
        return self._outputs[tuple(arguments)]


def test_changed_paths_joins_committed_uncommitted_and_untracked_files_without_repeats() -> None:
    fake_git = FakeGit({
        tuple(COMMITTED_COMMAND): "src/spells.cpp\0src/structs.h\0",
        tuple(UNCOMMITTED_COMMAND): "src/structs.h\0README.md\0",
        tuple(UNTRACKED_COMMAND): "tests/integration/scenarios/test_new.py\0",
    })
    assert changed_groups.changed_paths(BASE_COMMIT, fake_git) == [
        "src/spells.cpp", "src/structs.h", "README.md", "tests/integration/scenarios/test_new.py",
    ]
    assert fake_git.commands == [COMMITTED_COMMAND, UNCOMMITTED_COMMAND, UNTRACKED_COMMAND]


def test_changed_paths_keeps_unusual_file_names_whole() -> None:
    fake_git = FakeGit({
        tuple(COMMITTED_COMMAND): "lib/text/café news\0src/odd\nname.h\0",
        tuple(UNCOMMITTED_COMMAND): "",
        tuple(UNTRACKED_COMMAND): "",
    })
    assert changed_groups.changed_paths(BASE_COMMIT, fake_git) == ["lib/text/café news", "src/odd\nname.h"]


def test_a_file_renamed_out_of_the_harness_runs_every_group() -> None:
    # Without renames, git lists a move as the deletion of the old path and the addition of the new.
    fake_git = FakeGit({
        tuple(COMMITTED_COMMAND): "docs/old_main.cpp\0src/harness/main.cpp\0",
        tuple(UNCOMMITTED_COMMAND): "",
        tuple(UNTRACKED_COMMAND): "",
    })
    selection = select(*changed_groups.changed_paths(BASE_COMMIT, fake_git))
    assert selection.groups is None


def test_resolve_base_names_the_commit_with_options_ended() -> None:
    fake_git = FakeGit({tuple(RESOLVE_COMMAND): f"{BASE_COMMIT}\n"})
    assert changed_groups.resolve_base(BASE, fake_git) == BASE_COMMIT


def test_no_changes_runs_boot_only() -> None:
    selection = select()
    assert selection.groups == ["boot"]
    assert selection.reasons == ["boot: always runs"]


def test_a_changed_scenario_adds_its_groups_as_in_ci() -> None:
    assert select(POISON_SCENARIO).groups == ["boot", "poison"]


def test_a_changed_harness_file_runs_every_group_as_in_ci() -> None:
    selection = select("tests/integration/rots_harness/session.py", "src/spells.cpp")
    assert selection.groups is None
    assert selection.reasons == ["every group: harness file tests/integration/rots_harness/session.py changed"]


def test_a_changed_scenario_file_with_subgroups_runs_only_its_subgroups() -> None:
    scenario = "tests/integration/scenarios/test_shaping.py"
    source = (
        "pytestmark = [pytest.mark.scenario, pytest.mark.shaping]\n"
        "@pytest.mark.shaping_zone_editor\ndef test_one(server): pass\n"
    )
    groups = ["boot", "shaping", "shaping_commands", "shaping_zone_editor"]
    subgroups = {"shaping": ["shaping_commands", "shaping_zone_editor"]}
    selection = changed_groups.select_local_groups([scenario], {scenario: source}.get, groups, subgroups)
    assert selection.groups == ["boot", "shaping_zone_editor"]
    command = changed_groups.integration_command("make", "python3", selection)
    assert command == ["make", "integration", "PYTHON=python3", "GROUP=boot or shaping_zone_editor"]


def test_a_changed_scenario_file_also_runs_the_groups_of_the_files_that_import_it() -> None:
    importer = "tests/integration/scenarios/test_summon.py"
    sources = {POISON_SCENARIO: POISON_SOURCE, importer: "from test_poison import DOSE\npytestmark = [pytest.mark.scenario, pytest.mark.summon]\n"}
    selection = changed_groups.select_local_groups([POISON_SCENARIO], sources.get, GROUPS, None, sorted(sources))
    assert selection.groups == ["boot", "poison", "summon"]


def test_a_changed_game_header_runs_every_group() -> None:
    selection = select(POISON_SCENARIO, "src/structs.h")
    assert selection.groups is None
    assert selection.reasons == ["every group: header src/structs.h changed, and constants and layouts in headers reach any scenario"]


def test_a_changed_game_header_runs_every_group_only_locally() -> None:
    assert ci_groups.select_groups("pull_request", "", ["src/structs.h"], read_scenario, GROUPS).groups == ["boot"]
    assert select("src/structs.h").groups is None


@pytest.mark.parametrize("path", ["src/CMakeLists.txt", "lib/text/news", "lib/misc/socials"])
def test_ci_and_local_runs_agree_on_the_build_file_and_copied_lib_files(path: str) -> None:
    assert select(path) == ci_groups.select_groups("pull_request", "", [path], read_scenario, GROUPS)


def test_a_changed_unit_test_header_selects_no_group() -> None:
    assert select("src/tests/game_tick_schedule_contract.h").groups == ["boot"]


def test_a_changed_build_file_runs_every_group() -> None:
    selection = select("src/CMakeLists.txt")
    assert selection.groups is None
    assert selection.reasons == ["every group: src/CMakeLists.txt changed, and its compile settings reach every program"]


@pytest.mark.parametrize("path", ["lib/text/news", "lib/text/help/spells", *(f"lib/misc/{name}" for name in libbuilder.TRACKED_MISC_FILES)])
def test_a_changed_lib_file_the_harness_copies_runs_every_group(path: str) -> None:
    selection = select(path)
    assert selection.groups is None
    assert selection.reasons == [f"every group: {path} changed, and the harness copies it into every test's lib"]


def test_a_changed_lib_file_the_harness_does_not_copy_selects_no_group() -> None:
    assert select("lib/misc/game_boot_options.json", "lib/plrobjs/a/alice").groups == ["boot"]


def test_changed_game_code_is_listed_without_selecting_a_group() -> None:
    selection = select("src/spells.cpp", "src/tests/spells_tests.cpp", POISON_SCENARIO)
    assert selection.groups == ["boot", "poison"]
    assert selection.reasons == [
        "boot: always runs",
        "poison: test_poison.py changed",
        f"src/spells.cpp changed: {changed_groups.GAME_CODE_NOTE}",
    ]


def write_repo(repo_root: Path) -> None:
    """A repository with one file per group in GROUP_DESCRIPTIONS and the poison scenario."""
    groups_dir = repo_root / ci_groups.GROUPS_DIRECTORY
    groups_dir.mkdir(parents=True)
    for name, description in GROUP_DESCRIPTIONS.items():
        (groups_dir / f"{name}.txt").write_text(description + "\n", encoding="utf-8")
    scenario = repo_root / POISON_SCENARIO
    scenario.parent.mkdir(parents=True)
    scenario.write_text(POISON_SOURCE, encoding="utf-8")


class FakeRun:
    """Stands in for subprocess.run: answers git as a repository at repo_root would, and records every other command."""

    def __init__(self, repo_root: Path, committed: str, make_status: int = 0, base_known: bool = True) -> None:
        # The repository's top level, which rev-parse --show-toplevel prints.
        self._repo_root = repo_root
        # What the committed-changes diff prints; the other two git lists print nothing.
        self._committed = committed
        # The exit status the make command returns.
        self._make_status = make_status
        # Whether the base resolves to BASE_COMMIT; otherwise rev-parse --verify fails.
        self._base_known = base_known
        # Every git argument list run, without its "git -C <folder>" prefix.
        self.git_commands: list[list[str]] = []
        # Every non-git command run, with its working directory.
        self.commands: list[tuple[list[str], Optional[Path]]] = []

    def __call__(self, arguments: list[str], **options: object) -> subprocess.CompletedProcess:
        if arguments[0] != "git":
            self.commands.append((arguments, options.get("cwd")))  # type: ignore[arg-type]
            return subprocess.CompletedProcess(arguments, self._make_status)
        git_arguments = arguments[3:]
        self.git_commands.append(git_arguments)
        if git_arguments == ["rev-parse", "--show-toplevel"]:
            return subprocess.CompletedProcess(arguments, 0, f"{self._repo_root}\n", "")
        if git_arguments == RESOLVE_COMMAND:
            if not self._base_known:
                return subprocess.CompletedProcess(arguments, 128, "", "fatal: Needed a single revision")
            return subprocess.CompletedProcess(arguments, 0, f"{BASE_COMMIT}\n", "")
        output = self._committed if git_arguments == COMMITTED_COMMAND else ""
        return subprocess.CompletedProcess(arguments, 0, output, "")


def run_main(tmp_path: Path, monkeypatch: pytest.MonkeyPatch, fake_run: FakeRun, *extra: str) -> int:
    monkeypatch.setattr(changed_groups.subprocess, "run", fake_run)
    return changed_groups.main(["--repo-root", str(tmp_path / "repo" / "tests"), "--base", BASE, "--python", ".venv/bin/python", "--make", "gmake", *extra])


def make_fake_run(tmp_path: Path, committed: str, **options: object) -> FakeRun:
    repo_root = tmp_path / "repo"
    write_repo(repo_root)
    return FakeRun(repo_root, committed, **options)  # type: ignore[arg-type]


def test_main_runs_make_integration_with_the_chosen_groups(tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]) -> None:
    fake_run = make_fake_run(tmp_path, f"{POISON_SCENARIO}\0", make_status=1)
    assert run_main(tmp_path, monkeypatch, fake_run) == 1
    assert fake_run.commands == [(["gmake", "integration", "PYTHON=.venv/bin/python", "GROUP=boot or poison"], tmp_path / "repo")]
    printed = capsys.readouterr().out
    assert f"since {BASE} ({BASE_COMMIT[:12]})" in printed
    assert "- poison: test_poison.py changed\n" in printed
    assert "GROUP=boot or poison" in printed


def test_main_runs_every_scenario_with_an_empty_group(tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]) -> None:
    fake_run = make_fake_run(tmp_path, "src/structs.h\0")
    assert run_main(tmp_path, monkeypatch, fake_run) == 0
    # An empty GROUP on the command line overrides one exported or passed to the parent make.
    assert fake_run.commands == [(["gmake", "integration", "PYTHON=.venv/bin/python", "GROUP="], tmp_path / "repo")]
    assert "every group" in capsys.readouterr().out


def test_main_runs_git_at_the_top_level_against_the_resolved_base(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    fake_run = make_fake_run(tmp_path, "")
    run_main(tmp_path, monkeypatch, fake_run, "--list-only")
    assert fake_run.git_commands == [["rev-parse", "--show-toplevel"], RESOLVE_COMMAND, COMMITTED_COMMAND, UNCOMMITTED_COMMAND, UNTRACKED_COMMAND]


def test_main_list_only_prints_the_choice_and_runs_nothing(tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]) -> None:
    fake_run = make_fake_run(tmp_path, f"{POISON_SCENARIO}\0")
    assert run_main(tmp_path, monkeypatch, fake_run, "--list-only") == 0
    assert fake_run.commands == []
    assert "GROUP=boot or poison" in capsys.readouterr().out


def test_main_escapes_a_file_name_that_is_not_utf8_in_its_output(tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]) -> None:
    # git's output is decoded with surrogateescape, so the name's 0xE9 byte arrives as the surrogate U+DCE9.
    fake_run = make_fake_run(tmp_path, "lib/text/caf\udce9\0")
    assert run_main(tmp_path, monkeypatch, fake_run, "--list-only") == 0
    assert "- every group: lib/text/caf\\udce9 changed" in capsys.readouterr().out


def test_main_fails_with_status_2_and_git_words_when_the_base_does_not_resolve(tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]) -> None:
    fake_run = make_fake_run(tmp_path, "", base_known=False)
    assert run_main(tmp_path, monkeypatch, fake_run) == 2
    assert fake_run.commands == []
    assert "fatal: Needed a single revision" in capsys.readouterr().err
