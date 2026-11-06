"""A pytest plugin, loaded with -p, that writes each collected test's node id and marker names as JSON.

The run plan collects the selection with it before a run. The file is written only when the
ROTS_IT_COLLECT_REPORT environment variable names it.
"""

from __future__ import annotations

import json
import os
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable

# The environment variable naming the file the plugin writes.
REPORT_PATH_VARIABLE = "ROTS_IT_COLLECT_REPORT"


@dataclass(frozen=True)
class CollectedTest:
    """One test a run would run, and the names of the markers on it."""

    # The test's node id, relative to tests/integration.
    node_id: str
    # The names of every marker on the test, its module's included.
    markers: frozenset[str]


def write_report(path: Path, tests: Iterable[CollectedTest]) -> None:
    """Writes the tests to path as a JSON list, in order; raises OSError."""
    entries = [{"node_id": test.node_id, "markers": sorted(test.markers)} for test in tests]
    path.write_text(json.dumps(entries), encoding="utf-8")


def read_report(path: Path) -> list[CollectedTest]:
    """The tests write_report wrote to path, in order; raises OSError or ValueError."""
    entries = json.loads(path.read_text(encoding="utf-8"))
    return [CollectedTest(entry["node_id"], frozenset(entry["markers"])) for entry in entries]


def pytest_collection_finish(session) -> None:
    """Writes the tests left after deselection, sharding included, to the file ROTS_IT_COLLECT_REPORT names."""
    path = os.environ.get(REPORT_PATH_VARIABLE)
    if not path:
        return
    tests = [CollectedTest(item.nodeid, frozenset(marker.name for marker in item.iter_markers())) for item in session.items]
    write_report(Path(path), tests)
