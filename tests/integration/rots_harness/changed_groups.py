"""Runs the scenario groups this checkout's changes reach, chosen by CI's rules plus a local rule for game headers.

The changed paths are those committed since the merge base with --base, plus uncommitted changes
to tracked files and untracked files git does not ignore; a rename counts as both its old and its
new path. ci_groups' rules apply first (without a pull request description): `boot` always runs,
changed scenario and group files add their groups (only the subgroups a changed scenario file marks,
for a group whose subgroups it marks, and the groups of the scenario files that import it), and a
change to the harness, `src/CMakeLists.txt`
or a `lib/` file the harness copies into every test's lib runs every group. The one local-only
rule: a changed game header (`src/**/*.h` outside `src/harness/` and `src/tests/`) also runs every
group. CI leaves it out because most pull requests touch a header. Changed game code (`.cpp`)
selects no group; it is listed so the reader can name groups with GROUP= or run every scenario.

Run from the repository root as `make integration-changed`; it prints the choice and then runs
`make integration` with it.
"""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path
from typing import Callable, Iterable, Mapping, Optional, Sequence

from rots_harness import ci_groups
from rots_harness import groups as group_files

# The branch changes are measured from unless --base names another.
DEFAULT_BASE = "origin/release-frodo-next"
# The folder holding the game's sources.
GAME_SOURCE_DIRECTORY = "src/"
# Folders under src/ whose files are not game code: CI's rules cover the harness, and unit tests reach no scenario.
NON_GAME_SOURCE_PREFIXES = ("src/harness/", "src/tests/")
# Why a changed game source file is listed without adding a group.
GAME_CODE_NOTE = "a game-code change selects no group; name groups with GROUP= or run every scenario with make integration"


class GitFailed(RuntimeError):
    """A git command exited unsuccessfully; the message carries the command and git's words."""


def resolve_base(base: str, run_git: Callable[[list[str]], str]) -> str:
    """The full commit id base names; raises GitFailed (through run_git) when it names no commit.

    base is read only as a revision, never as an option, even when it starts with "-".
    """
    return run_git(["rev-parse", "--verify", "--end-of-options", f"{base}^{{commit}}"]).strip()


def changed_paths(base_commit: str, run_git: Callable[[list[str]], str]) -> list[str]:
    """The repository-relative paths changed since the merge base with base_commit, uncommitted, or untracked, in that order without repeats.

    A rename lists both its old and its new path. run_git runs git at the repository's top level
    with the given arguments and returns its standard output; it raises GitFailed on failure.
    """
    # -z lists each path whole and unquoted, whatever characters it holds.
    listings = [
        run_git(["diff", "--name-only", "-z", "--no-renames", f"{base_commit}...HEAD"]),
        run_git(["diff", "--name-only", "-z", "--no-renames", "HEAD"]),
        run_git(["ls-files", "-z", "--others", "--exclude-standard", "--full-name"]),
    ]
    paths: list[str] = []
    for listing in listings:
        for path in listing.split("\0"):
            if path and path not in paths:
                paths.append(path)
    return paths


def _is_game_source(path: str, suffix: str) -> bool:
    """Whether path is a game source file under src/ with the given suffix."""
    return path.startswith(GAME_SOURCE_DIRECTORY) and path.endswith(suffix) and not path.startswith(NON_GAME_SOURCE_PREFIXES)


def _local_every_group_reason(path: str) -> Optional[str]:
    """Why a change to path runs every group under the local-only rule, or None when it does not."""
    if _is_game_source(path, ".h"):
        return f"every group: header {path} changed, and constants and layouts in headers reach any scenario"
    return None


def select_local_groups(
    changed: Iterable[str],
    read_scenario: Callable[[str], Optional[str]],
    groups: list[str],
    subgroups: Optional[Mapping[str, Sequence[str]]] = None,
    scenario_paths: Iterable[str] = (),
) -> ci_groups.Selection:
    """The groups a local run of these changed paths tests.

    read_scenario returns a scenario file's source, or None when the file is not in the checkout.
    groups lists the registered groups and subgroups; subgroups gives each group that has
    subgroups, with them; scenario_paths lists every scenario file in the checkout.
    """
    changed_list = list(changed)
    selection = ci_groups.select_groups("pull_request", "", changed_list, read_scenario, groups, subgroups, scenario_paths)
    if selection.groups is None:
        return selection
    for path in changed_list:
        reason = _local_every_group_reason(path)
        if reason is not None:
            return ci_groups.Selection(None, [reason])
    game_code = [path for path in changed_list if _is_game_source(path, ".cpp")]
    selection.reasons.extend(f"{path} changed: {GAME_CODE_NOTE}" for path in game_code)
    return selection


def integration_command(make_program: str, python: str, selection: ci_groups.Selection) -> list[str]:
    """The make integration command that runs the selection.

    GROUP is always given, empty for every group, so a GROUP exported or passed to the parent make
    cannot narrow the run: a command-line variable overrides both.
    """
    return [make_program, "integration", f"PYTHON={python}", f"GROUP={selection.expression()}"]


def _git_runner(folder: Path) -> Callable[[list[str]], str]:
    """Runs git in folder with the given arguments and returns its standard output; raises GitFailed on failure."""

    def run_git(git_arguments: list[str]) -> str:
        # surrogateescape keeps a file name that is not valid UTF-8 intact through the round trip.
        completed = subprocess.run(
            ["git", "-C", str(folder), *git_arguments], capture_output=True, text=True, encoding="utf-8", errors="surrogateescape", check=False,
        )
        if completed.returncode != 0:
            raise GitFailed(f"git {' '.join(git_arguments)} failed: {completed.stderr.strip()}")
        return completed.stdout

    return run_git


def main(argv: Optional[list[str]] = None) -> int:
    """Prints the reasons and the group expression, then runs make integration with it and returns its exit status.

    Returns 0 without running anything under --list-only, and 2 when git fails or a group file cannot be read.
    """
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--base", default=DEFAULT_BASE, help=f"the branch the changes are measured from (default {DEFAULT_BASE})")
    parser.add_argument("--repo-root", type=Path, default=Path("."), help="a folder inside the repository")
    parser.add_argument("--python", default=sys.executable, help="the Python make integration runs the scenarios with")
    parser.add_argument("--make", default="make", help="the make program that runs make integration")
    parser.add_argument("--list-only", action="store_true", help="print the choice without running it")
    arguments = parser.parse_args(argv)
    try:
        top_level = _git_runner(arguments.repo_root)(["rev-parse", "--show-toplevel"]).strip()
        repo_root = Path(top_level)
        run_git = _git_runner(repo_root)
        base_commit = resolve_base(arguments.base, run_git)
        group_list = group_files.read_groups(repo_root / ci_groups.GROUPS_DIRECTORY)
        changed = changed_paths(base_commit, run_git)
    except (GitFailed, group_files.GroupFileError, OSError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 2

    def read_scenario(path: str) -> Optional[str]:
        scenario_path = repo_root / path
        return scenario_path.read_text(encoding="utf-8") if scenario_path.is_file() else None

    groups = [group.name for group in group_list]
    subgroups = group_files.subgroups_by_parent(group_list)
    selection = select_local_groups(changed, read_scenario, groups, subgroups, ci_groups.scenario_file_paths(repo_root))
    # The reasons quote changed paths, which may hold bytes that are not UTF-8.
    ci_groups.escape_unencodable_output(sys.stdout)
    # A short commit id shows when a remote-tracking base is out of date.
    print(f"Changes since {arguments.base} ({base_commit[:12]}), uncommitted changes and untracked files choose:")
    for reason in selection.reasons:
        print(f"- {reason}")
    if selection.groups is None:
        print("Choice: every group (GROUP empty)")
    else:
        print(f"Choice: GROUP={selection.expression()}")
    if arguments.list_only:
        return 0
    # Flushed so the choice prints before make's output, which goes straight to the terminal.
    sys.stdout.flush()
    command = integration_command(arguments.make, arguments.python, selection)
    return subprocess.run(command, cwd=repo_root, check=False).returncode


if __name__ == "__main__":
    sys.exit(main())
