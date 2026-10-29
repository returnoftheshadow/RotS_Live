from __future__ import annotations

from dataclasses import dataclass, field

import pytest

# The integration conftest.py, which pytest has already loaded as the module conftest.
import conftest

# shaping's subgroups, as the fake check is given them.
SUBGROUPS = {"shaping": ["shaping_commands", "shaping_zone_editor"]}


@dataclass(frozen=True)
class FakeMarker:
    """A marker as item.iter_markers yields it; the check reads only its name."""

    # The marker's name.
    name: str


@dataclass
class FakeItem:
    """A collected test with a node id and markers, as the check reads them."""

    # The test's node id.
    nodeid: str
    # The names of the markers on the test.
    marker_names: tuple[str, ...]

    def iter_markers(self):
        return (FakeMarker(name) for name in self.marker_names)


@dataclass
class FakeSession:
    """A session whose shouldstop the check sets."""

    # What the check asks a pytest-xdist controller to stop with; False until then.
    shouldstop: object = field(default=False)


def test_the_check_passes_tests_that_follow_the_rules_and_leaves_the_session_running() -> None:
    session = FakeSession()
    items = [FakeItem("test_a.py::test_one", ("scenario", "shaping", "shaping_commands")), FakeItem("test_b.py::test_two", ("boot",))]
    conftest.SubgroupMarkingCheck(SUBGROUPS).pytest_collection_modifyitems(session=session, items=items)
    assert session.shouldstop is False


def test_the_check_stops_the_run_naming_each_test_that_breaks_the_rules() -> None:
    session = FakeSession()
    items = [FakeItem("test_a.py::test_one", ("scenario", "shaping")), FakeItem("test_a.py::test_two", ("shaping_commands",))]
    with pytest.raises(pytest.UsageError) as raised:
        conftest.SubgroupMarkingCheck(SUBGROUPS).pytest_collection_modifyitems(session=session, items=items)
    message = str(raised.value)
    assert "test_a.py::test_one: marks shaping but none of its subgroups" in message
    assert "test_a.py::test_two: marks the subgroup shaping_commands without its group shaping" in message
    # A pytest-xdist worker hands shouldstop to the controller, so the message reaches the terminal.
    assert session.shouldstop == message


def test_pytest_configure_registers_the_check_to_run_before_any_deselection(pytestconfig: pytest.Config) -> None:
    check = pytestconfig.pluginmanager.get_plugin(conftest.SUBGROUP_CHECK_PLUGIN_NAME)
    assert isinstance(check, conftest.SubgroupMarkingCheck)
    implementations = pytestconfig.pluginmanager.hook.pytest_collection_modifyitems.get_hookimpls()
    assert [implementation.tryfirst for implementation in implementations if implementation.plugin is check] == [True]
