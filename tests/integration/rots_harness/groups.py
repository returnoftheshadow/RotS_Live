"""The scenario groups, one file each under tests/integration/groups, and the check that a pytest -m
expression names only registered markers, so a misspelt group stops the run.

A group file groups/<group>.txt registers the marker <group>. A large group can be split into
subgroups, one level deep: groups/<group>/<sub>.txt registers the marker <group>_<sub>, a subgroup
of <group>. Every test of a group with subgroups also marks exactly one of them, and a test marking
a subgroup also marks its group (subgroup_marking_problems).
"""

from __future__ import annotations

import re
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, Mapping, Optional, Sequence

# A marker name as pytest's -m grammar reads an identifier.
MARKER_NAME_PATTERN = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")
# Words of the -m grammar that are operators, not marker names.
EXPRESSION_KEYWORDS = frozenset({"and", "or", "not"})
# The folder under tests/integration holding one <group>.txt file per scenario group, and a <group>
# folder of subgroup files for each group with subgroups.
GROUPS_DIRECTORY_NAME = "groups"
# The suffix of a group file; the file's name without it is the group's marker name, after
# <group>_ for a subgroup.
GROUP_FILE_SUFFIX = ".txt"
# Folders a tool may leave in the groups folder or a subgroup folder, which the reader ignores
# along with every hidden (dot) folder.
IGNORED_FOLDER_NAMES = frozenset({"__pycache__"})


class GroupFileError(ValueError):
    """The groups folder does not follow the group file rules; the message names the file or folder and the rule."""


@dataclass(frozen=True)
class Group:
    """One registered scenario group: a top-level group, or a subgroup of one."""

    # The group's marker name.
    name: str
    # The one-line description its group file holds.
    description: str
    # The top-level group a subgroup belongs to; None for a top-level group.
    parent: Optional[str] = None


def registered_marker_names(ini_lines: Iterable[str]) -> list[str]:
    """Marker names from pytest.ini's markers lines ("name: help" or "name(args): help")."""
    names = []
    for line in ini_lines:
        match = MARKER_NAME_PATTERN.match(line.strip())
        if match:
            names.append(match.group(0))
    return names


def unknown_marker_names(expression: str, registered: Iterable[str]) -> list[str]:
    """Names in a -m expression that no marker registers, in the order they appear."""
    known = set(registered)
    unknown = []
    for name in MARKER_NAME_PATTERN.findall(expression):
        if name not in EXPRESSION_KEYWORDS and name not in known and name not in unknown:
            unknown.append(name)
    return unknown


def read_groups(groups_dir: Path) -> list[Group]:
    """Every group the folder registers, in registration order: each top-level group in name order,
    followed at once by its subgroups in name order.

    groups_dir/<name>.txt registers a top-level group, and groups_dir/<name>/<sub>.txt registers
    the subgroup <name>_<sub>. Files with another suffix, hidden folders and __pycache__ are
    ignored. Raises GroupFileError for a file whose name is not a marker name or that does not
    hold exactly one non-empty line, any other folder with no group file of its own name beside
    it, a folder inside a subfolder, or two files registering one name; raises OSError when a
    folder cannot be read.
    """
    entries = sorted(groups_dir.iterdir())
    group_files = [path for path in entries if _is_group_file(path)]
    group_file_names = {path.stem for path in group_files}
    subgroup_files: dict[str, list[Path]] = {}
    for folder in entries:
        if not folder.is_dir() or _is_ignored_folder(folder):
            continue
        shown = f"{groups_dir.name}/{folder.name}"
        if folder.suffix == GROUP_FILE_SUFFIX:
            raise GroupFileError(f"folder {shown} is named like a group file; a subgroup folder takes its group's name without {GROUP_FILE_SUFFIX}")
        if folder.name not in group_file_names:
            raise GroupFileError(f"folder {shown} has no group file {shown}{GROUP_FILE_SUFFIX}; a subgroup folder sits beside its group's file")
        subgroup_files[folder.name] = _subgroup_files(folder)
    groups = []
    registered_by: dict[str, Path] = {}
    for group_path in group_files:
        parent = group_path.stem
        named_files = [(group_path, parent, None)]
        named_files.extend((path, f"{parent}_{path.stem}", parent) for path in subgroup_files.get(parent, []))
        for path, name, group_parent in named_files:
            group = _read_group_file(path, name, group_parent)
            if name in registered_by:
                raise GroupFileError(f"{path} registers {name}, which {registered_by[name]} also registers")
            registered_by[name] = path
            groups.append(group)
    return groups


def _is_ignored_folder(path: Path) -> bool:
    """Whether path is a hidden or cache folder the reader skips."""
    return path.name.startswith(".") or path.name in IGNORED_FOLDER_NAMES


def _is_group_file(path: Path) -> bool:
    """Whether path is a file the reader registers: a regular file with the group file suffix."""
    return path.is_file() and path.suffix == GROUP_FILE_SUFFIX


def _subgroup_files(folder: Path) -> list[Path]:
    """The subgroup files in a group's subfolder, in name order; raises GroupFileError for a folder inside it."""
    paths = []
    for path in sorted(folder.iterdir()):
        if path.is_dir() and not _is_ignored_folder(path):
            raise GroupFileError(f"{path}: a subgroup has no subgroups, so a subgroup folder holds no folder")
        if _is_group_file(path):
            paths.append(path)
    return paths


def _read_group_file(path: Path, name: str, parent: Optional[str]) -> Group:
    """The group path registers under name; raises GroupFileError unless its stem is a marker name and it holds one non-empty line."""
    if MARKER_NAME_PATTERN.fullmatch(path.stem) is None or path.stem in EXPRESSION_KEYWORDS:
        raise GroupFileError(f"{path}: {path.stem!r} is not a marker name (letters, digits and underscores, not starting with a digit)")
    lines = [line.strip() for line in path.read_text(encoding="utf-8").splitlines() if line.strip()]
    if len(lines) != 1:
        raise GroupFileError(f"{path}: a group file holds its description on one line; found {len(lines)} non-empty lines")
    return Group(name, lines[0], parent)


def read_group_descriptions(groups_dir: Path) -> dict[str, str]:
    """Each registered group's name and one-line description, subgroups included, in read_groups' registration order.

    Raises what read_groups raises.
    """
    return {group.name: group.description for group in read_groups(groups_dir)}


def subgroups_by_parent(groups: Iterable[Group]) -> dict[str, list[str]]:
    """Each group that has subgroups, with its subgroups' names in the order given; a group without subgroups is left out."""
    subgroups: dict[str, list[str]] = {}
    for group in groups:
        if group.parent is not None:
            subgroups.setdefault(group.parent, []).append(group.name)
    return subgroups


def subgroup_marking_problems(tests: Iterable[tuple[str, Iterable[str]]], subgroups: Mapping[str, Sequence[str]]) -> list[str]:
    """One line per way a test's markers break the subgroup rules, as "<node id>: <what is wrong>", in test order.

    tests gives each test's node id and marker names; subgroups gives each group that has
    subgroups, with them (subgroups_by_parent). A test marking such a group must mark exactly one of
    its subgroups, and a test marking a subgroup must mark its group.
    """
    problems = []
    for node_id, marker_names in tests:
        markers = set(marker_names)
        for parent, names in subgroups.items():
            marked = [name for name in names if name in markers]
            if parent in markers and not marked:
                problems.append(f"{node_id}: marks {parent} but none of its subgroups ({', '.join(names)})")
            if len(marked) > 1:
                problems.append(f"{node_id}: marks {len(marked)} subgroups of {parent} ({', '.join(marked)}); a test marks exactly one")
            if parent not in markers:
                problems.extend(f"{node_id}: marks the subgroup {name} without its group {parent}" for name in marked)
    return problems
