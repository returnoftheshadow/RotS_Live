"""Checks a pytest -m expression against the registered markers, so a misspelt group stops the run."""

from __future__ import annotations

import re
from typing import Iterable

# A marker name as pytest's -m grammar reads an identifier.
MARKER_NAME_PATTERN = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")
# Words of the -m grammar that are operators, not marker names.
EXPRESSION_KEYWORDS = frozenset({"and", "or", "not"})


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
