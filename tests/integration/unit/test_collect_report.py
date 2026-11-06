from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path

import pytest

from rots_harness import collect_report
from rots_harness.collect_report import CollectedTest


@dataclass
class FakeMarker:
    """A pytest Mark: only its name is read."""

    # The marker's name.
    name: str


@dataclass
class FakeItem:
    """A collected pytest item: its node id and the markers iter_markers yields."""

    # The test's node id.
    nodeid: str
    # Its markers, closest first, with repeats as pytest yields them.
    markers: list[FakeMarker] = field(default_factory=list)

    def iter_markers(self):
        return iter(self.markers)


@dataclass
class FakeSession:
    """A pytest Session after collection: only its kept items are read."""

    # The tests left after deselection.
    items: list[FakeItem]


def test_a_report_round_trips(tmp_path: Path) -> None:
    path = tmp_path / "report.json"
    tests = [CollectedTest("a.py::one", frozenset({"scenario", "boot"})), CollectedTest("a.py::two", frozenset())]
    collect_report.write_report(path, tests)
    assert collect_report.read_report(path) == tests


def test_collection_finish_writes_each_kept_item_and_its_marker_names(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    path = tmp_path / "report.json"
    monkeypatch.setenv(collect_report.REPORT_PATH_VARIABLE, str(path))
    session = FakeSession([
        FakeItem("a.py::one", [FakeMarker("characters"), FakeMarker("scenario"), FakeMarker("boot"), FakeMarker("scenario")]),
        FakeItem("b.py::two", [FakeMarker("poison")]),
    ])
    collect_report.pytest_collection_finish(session)
    assert collect_report.read_report(path) == [
        CollectedTest("a.py::one", frozenset({"characters", "scenario", "boot"})),
        CollectedTest("b.py::two", frozenset({"poison"})),
    ]


def test_collection_finish_without_a_report_path_writes_nothing(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.delenv(collect_report.REPORT_PATH_VARIABLE, raising=False)
    collect_report.pytest_collection_finish(FakeSession([FakeItem("a.py::one")]))
    assert list(tmp_path.iterdir()) == []
