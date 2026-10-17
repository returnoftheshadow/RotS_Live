"""Chooses the scenario groups a CI run tests, from the pull request's description and changed files.

`boot` always runs. A description token `@integration-test-<group>` adds that group, and
`@integration-test-all` runs every group. A changed scenario file adds the groups it marks. A
change to the harness itself, or a push, runs every group.

Run from tests/integration as `python3 -m rots_harness.ci_groups`; the description arrives in
the PR_BODY environment variable so it is never part of a shell command.
"""

from __future__ import annotations

import argparse
import configparser
import os
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable, Iterable, Optional

from rots_harness.groups import MARKER_NAME_PATTERN

ALWAYS_RUN_GROUP = "boot"
ALL_GROUPS_TOKEN = "all"
# A token in the description; the name stops at the first character a marker name cannot hold.
DESCRIPTION_TOKEN = re.compile(r"(?<![\w-])@integration-test-([A-Za-z0-9_]+)")
# HTML comments are dropped first, so a pull request template can show the syntax.
HTML_COMMENT = re.compile(r"<!--.*?-->", re.S)
SCENARIO_MARKER = re.compile(r"pytest\.mark\.([A-Za-z_][A-Za-z0-9_]*)")
SCENARIO_DIRECTORY = "tests/integration/scenarios/"
# Markers that every scenario carries or that configure a test; never a group.
NON_GROUP_MARKERS = frozenset({"scenario"})
# Paths whose change can affect any scenario, so every group runs.
HARNESS_PATH_PREFIXES = (
    "tests/integration/rots_harness/",
    "tests/integration/world/",
    "src/harness/",
    "src/tools/rotstool/",
)
HARNESS_FILES = frozenset({
    "tests/integration/conftest.py",
    "tests/integration/pytest.ini",
    "tests/integration/requirements.txt",
    "Dockerfile",
    ".github/workflows/integration.yml",
})


class UnknownGroups(ValueError):
    """The description names groups pytest.ini does not register; the message lists the valid ones."""


@dataclass
class Selection:
    """The groups to run, None meaning every group, and one line per reason."""

    # The groups to run in order, or None to run every scenario.
    groups: Optional[list[str]]
    # Why each group or the whole suite was chosen, for the job summary.
    reasons: list[str] = field(default_factory=list)

    def expression(self) -> str:
        """The GROUP value for make integration; empty runs every scenario."""
        return "" if self.groups is None else " or ".join(self.groups)


def group_marker_names(ini_lines: Iterable[str]) -> list[str]:
    """The registered markers that name groups: those taking no arguments, other than scenario."""
    names = []
    for line in ini_lines:
        stripped = line.strip()
        match = MARKER_NAME_PATTERN.match(stripped)
        if match is None or stripped[match.end():].startswith("("):
            continue
        if match.group(0) not in NON_GROUP_MARKERS:
            names.append(match.group(0))
    return names


def read_marker_lines(ini_path: Path) -> list[str]:
    """The lines of pytest.ini's markers setting."""
    parser = configparser.ConfigParser(interpolation=None)
    parser.read(ini_path, encoding="utf-8")
    return [line for line in parser.get("pytest", "markers", fallback="").splitlines() if line.strip()]


def description_tokens(body: str) -> list[str]:
    """The group names the description's tokens give, in order and without repeats, outside HTML comments."""
    names = []
    for name in DESCRIPTION_TOKEN.findall(HTML_COMMENT.sub("", body)):
        if name not in names:
            names.append(name)
    return names


def scenario_groups(source: str, groups: Iterable[str]) -> list[str]:
    """The groups a scenario file's source marks."""
    known = set(groups)
    found = []
    for name in SCENARIO_MARKER.findall(source):
        if name in known and name not in found:
            found.append(name)
    return found


def is_harness_path(path: str) -> bool:
    """Whether a change to this repository path can affect every scenario."""
    if path in HARNESS_FILES or path.startswith(HARNESS_PATH_PREFIXES):
        return True
    # A scenario helper or conftest is shared by every scenario file.
    return path.startswith(SCENARIO_DIRECTORY) and not Path(path).name.startswith("test_")


def select_groups(
    event: str, body: str, changed_paths: Iterable[str], read_scenario: Callable[[str], Optional[str]], groups: list[str],
) -> Selection:
    """The groups a CI run tests.

    read_scenario returns a changed scenario file's source, or None when the change deleted it.
    Raises UnknownGroups when the description names a group pytest.ini does not register.
    """
    if event == "push":
        return Selection(None, ["every group: a push runs the whole suite"])
    tokens = description_tokens(body)
    unknown = [name for name in tokens if name != ALL_GROUPS_TOKEN and name not in groups]
    if unknown:
        raise UnknownGroups(
            f"the description names unknown integration groups: {', '.join(unknown)}; "
            f"registered groups: {', '.join(groups)} (or {ALL_GROUPS_TOKEN})"
        )
    if ALL_GROUPS_TOKEN in tokens:
        return Selection(None, [f"every group: the description has @integration-test-{ALL_GROUPS_TOKEN}"])
    changed = list(changed_paths)
    harness_changes = [path for path in changed if is_harness_path(path)]
    if harness_changes:
        return Selection(None, [f"every group: harness file {harness_changes[0]} changed"])
    selected = [ALWAYS_RUN_GROUP]
    reasons = [f"{ALWAYS_RUN_GROUP}: always runs"]
    for name in tokens:
        if name not in selected:
            selected.append(name)
            reasons.append(f"{name}: named in the description")
    for path in changed:
        if not (path.startswith(SCENARIO_DIRECTORY) and path.endswith(".py")):
            continue
        source = read_scenario(path)
        if source is None:
            continue
        for name in scenario_groups(source, groups):
            if name not in selected:
                selected.append(name)
                reasons.append(f"{name}: {Path(path).name} changed")
    return Selection(selected, reasons)


def summary_markdown(selection: Selection) -> str:
    """The job summary: what runs and why."""
    ran = "every group" if selection.groups is None else ", ".join(f"`{name}`" for name in selection.groups)
    lines = [f"### Integration groups: {ran}", ""]
    lines.extend(f"- {reason}" for reason in selection.reasons)
    return "\n".join(lines) + "\n"


def main(argv: Optional[list[str]] = None) -> int:
    """Writes `group=<expression>` to --output and the summary to --summary; 2 on unknown groups."""
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--event", required=True, help="the GitHub event name: pull_request or push")
    parser.add_argument("--changed-files", type=Path, help="a file listing the changed paths, one per line")
    parser.add_argument("--repo-root", type=Path, default=Path("../.."), help="the repository root")
    parser.add_argument("--output", type=Path, required=True, help="the file that receives group=<expression>")
    parser.add_argument("--summary", type=Path, help="the file that receives the markdown summary")
    arguments = parser.parse_args(argv)
    repo_root = arguments.repo_root
    groups = group_marker_names(read_marker_lines(repo_root / "tests/integration/pytest.ini"))
    changed = []
    if arguments.changed_files is not None:
        changed = [line.strip() for line in arguments.changed_files.read_text(encoding="utf-8").splitlines() if line.strip()]

    def read_scenario(path: str) -> Optional[str]:
        scenario_path = repo_root / path
        return scenario_path.read_text(encoding="utf-8") if scenario_path.is_file() else None

    try:
        selection = select_groups(arguments.event, os.environ.get("PR_BODY", ""), changed, read_scenario, groups)
    except UnknownGroups as unknown:
        print(f"error: {unknown}", file=sys.stderr)
        return 2
    with arguments.output.open("a", encoding="utf-8") as output:
        output.write(f"group={selection.expression()}\n")
    summary = summary_markdown(selection)
    if arguments.summary is not None:
        with arguments.summary.open("a", encoding="utf-8") as summary_file:
            summary_file.write(summary)
    print(summary, end="")
    return 0


if __name__ == "__main__":
    sys.exit(main())
