"""Shows the scenario groups a local `make integration` would run, asks about them, and runs pytest with the groups kept.

The plan lists each selected group with its test count and estimated minutes, from the times
build/integration/durations.json recorded; a group with subgroups is a header over its selected
subgroups. At a terminal every group starts ticked: the developer can untick groups and
subgroups, start or cancel, and an unanswered plan starts by itself after 5 minutes.
Without a terminal a run estimated under ROTS_IT_CONFIRM_MINUTES (default 10) starts at once and
a longer one prints a NEEDS_CONFIRMATION_PREFIX line and stops with EXIT_NEEDS_CONFIRMATION unless
CONFIRM=yes. Under CI=true it runs the pytest command make integration always ran, leaving out
any SKIP_GROUPS, and prints nothing of its own.

Run from the repository root as `make integration` or `make integration-plan`.
"""

from __future__ import annotations

import argparse
import enum
import math
import os
import platform
import queue
import re
import shlex
import signal
import subprocess
import sys
import tempfile
import threading
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable, Iterable, Mapping, Optional, Sequence

from rots_harness import collect_report, durations, sharding
from rots_harness import groups as group_files
from rots_harness.collect_report import CollectedTest
from rots_harness.sharding import Shard

# tests/integration: the groups folder, the scenarios and the rots_harness package.
INTEGRATION_ROOT = Path(__file__).resolve().parents[1]
# The checkout whose scenarios run; every pytest command runs here.
REPO_ROOT = INTEGRATION_ROOT.parents[1]
# One <group>.txt file per registered scenario group, and a <group> folder of subgroup files.
GROUPS_DIR = INTEGRATION_ROOT / group_files.GROUPS_DIRECTORY_NAME
# The scenarios folder, relative to the repository root, as make integration names it.
SCENARIOS_RELATIVE = "tests/integration/scenarios"
# The pytest arguments writing each test's result and time, and listing the slowest tests.
REPORT_ARGUMENTS = ("--junitxml=build/integration/results.xml", "--durations=25")

# A SKIP_GROUPS, CONFIRM or ROTS_IT_CONFIRM_MINUTES value that cannot be used, or an unreadable group file.
EXIT_USAGE = 2
# No terminal to ask at, and the run's estimate reaches ROTS_IT_CONFIRM_MINUTES without CONFIRM=yes.
# Above pytest's own statuses (0 to 5), so a caller can tell it apart from a test result.
EXIT_NEEDS_CONFIRMATION = 10
# The developer cancelled the run at the prompt or while it collected the tests; nothing ran.
EXIT_CANCELLED = 11
# Starts the one line printed with EXIT_NEEDS_CONFIRMATION. make exits 2 for every failed command,
# so a caller of make finds this line rather than the status.
NEEDS_CONFIRMATION_PREFIX = "ROTS_IT_NEEDS_CONFIRMATION:"

# Seconds counted for a test with no recorded time.
DEFAULT_TEST_SECONDS = durations.DEFAULT_TEST_SECONDS
# The environment variable splitting a run into shards, which conftest.py applies to the selection.
SHARD_VARIABLE = "ROTS_IT_SHARD"
# The environment variable holding the estimate, in minutes, from which a run without a terminal needs CONFIRM=yes.
CONFIRM_MINUTES_VARIABLE = "ROTS_IT_CONFIRM_MINUTES"
# The confirmation threshold when CONFIRM_MINUTES_VARIABLE is unset.
DEFAULT_CONFIRM_MINUTES = 10.0
# Seconds an unanswered prompt waits before the run starts with the groups still ticked.
PROMPT_TIMEOUT_SECONDS = 300.0
# Seconds between the countdown lines of an unanswered prompt.
COUNTDOWN_STEP_SECONDS = 60.0
# The CONFIRM value that starts a run without asking.
CONFIRM_YES = "yes"
# JOBS values make integration runs without pytest-xdist.
SINGLE_WORKER_JOBS = frozenset({"", "0", "1"})
# JOBS values pytest-xdist reads as the number of CPUs.
CPU_COUNT_JOBS = frozenset({"auto", "logical"})
# The PYTHON the Makefile uses when none is given; the repeat line leaves it out.
DEFAULT_PYTHON = "python3"
# What separates names in SKIP_GROUPS and numbers in an answer.
SEPARATOR_PATTERN = re.compile(r"[\s,]+")
# A group number or a range of them in an answer, such as 3 or 3-5.
GROUP_RANGE_PATTERN = re.compile(r"(\d+)(?:-(\d+))?")
# The answers that start the run.
START_ANSWERS = frozenset({"", "y", "yes"})
# The prompt's one line of help.
PROMPT_HELP = (
    "Numbers (and ranges like 3-5) toggle groups, a header's number its subgroups together; a ticks all, n unticks all, "
    "Enter or y starts, q cancels."
)
# The plan's footer about tests in several groups.
UNTICK_FOOTER = "Unticking a group drops every test it marks, even one another ticked group also marks."
# The plan's footer under ROTS_IT_SHARD.
SHARD_FOOTER = "ROTS_IT_SHARD splits the tests left after the groups left out, so unticking a group changes which tests this shard runs."
# Seconds between checks on pytest while it runs, so a signal handled on the main thread is acted on promptly.
WAIT_POLL_SECONDS = 0.25
# What a subgroup's line adds in front of a group's, under its header.
SUBGROUP_INDENT = "    "


@dataclass
class RunPlan:
    """The selected tests, the groups they fall in, and what the recorded times say the run will take.

    The tests a run keeps are those no skipped or unticked group marks, split by the shard as
    conftest.py splits them, so the plan shows what pytest will run.
    """

    # Every test GROUP selects, in collection order, before any shard.
    tests: list[CollectedTest]
    # The groups and subgroups offered as checkboxes, in registration order; one no selected test is
    # in is left out, and a group with subgroups is offered through them instead.
    offered_groups: list[str]
    # The SKIP_GROUPS groups and subgroups with a selected test, in registration order; a subgroup of
    # a skipped group is left out.
    skipped_groups: list[str]
    # Every SKIP_GROUPS name; their tests never run.
    skipped_names: frozenset[str]
    # Recorded seconds by node id.
    recorded_seconds: Mapping[str, float]
    # The workers the run is divided between, at least 1.
    workers: int
    # The share of the kept tests ROTS_IT_SHARD runs, or None to run them all.
    shard: Optional[Shard] = None
    # The node ids of the selected tests that no registered group marks.
    ungrouped_ids: frozenset[str] = frozenset()
    # The group each registered subgroup belongs to.
    parent_of: Mapping[str, str] = field(default_factory=dict)
    # The committed seconds by node id that the shard is balanced by, as conftest.py balances it.
    shard_seconds: Mapping[str, float] = field(default_factory=dict)

    def kept_test_ids(self, unticked: Iterable[str]) -> list[str]:
        """The node ids of the tests the run keeps with the unticked groups left out, in collection order."""
        return self._run_test_ids(self.skipped_names | set(unticked))

    def group_test_ids(self, name: str, unticked: Iterable[str]) -> list[str]:
        """The node ids of the tests the group runs: those it marks that the run keeps with the group itself kept.

        For a ticked group these are among kept_test_ids; for an unticked or skipped group, the
        tests ticking it again would add.
        """
        excluded = (self.skipped_names | set(unticked)) - {name}
        run_ids = set(self._run_test_ids(excluded))
        return [test.node_id for test in self.tests if name in test.markers and test.node_id in run_ids]

    def header_test_ids(self, name: str, unticked: Iterable[str]) -> list[str]:
        """The node ids of the tests a group with subgroups runs through its ticked subgroups.

        With none of its offered subgroups ticked, the tests ticking them all again would add.
        """
        excluded = self.skipped_names | set(unticked)
        offered = {subgroup for subgroup in self.offered_groups if self.parent_of.get(subgroup) == name}
        if offered <= excluded:
            excluded -= offered
        run_ids = set(self._run_test_ids(excluded))
        return [test.node_id for test in self.tests if name in test.markers and test.node_id in run_ids]

    def _run_test_ids(self, excluded: frozenset[str]) -> list[str]:
        """The node ids of the tests no excluded group marks, then split by the shard, in collection order."""
        kept = [test.node_id for test in self.tests if not test.markers & excluded]
        if self.shard is None:
            return kept
        shard_ids = sharding.in_shard(kept, self.shard, self.shard_seconds)
        return [node_id for node_id in kept if node_id in shard_ids]

    def estimated_minutes(self, test_ids: Sequence[str]) -> float:
        """Minutes the tests take on the run's workers, no more workers than tests: recorded seconds, or DEFAULT_TEST_SECONDS for a test without."""
        if not test_ids:
            return 0.0
        seconds = sum(self.recorded_seconds.get(node_id, DEFAULT_TEST_SECONDS) for node_id in test_ids)
        return seconds / min(self.workers, len(test_ids)) / 60

    def untimed_count(self, test_ids: Iterable[str]) -> int:
        """How many of the tests have no recorded time."""
        return sum(1 for node_id in test_ids if node_id not in self.recorded_seconds)

    def ungrouped_count(self, unticked: Iterable[str]) -> int:
        """How many of the tests the run keeps no registered group marks."""
        return sum(1 for node_id in self.kept_test_ids(unticked) if node_id in self.ungrouped_ids)


@dataclass(frozen=True)
class RunContext:
    """What the plan's heading reports about how the run is made."""

    # The launcher ROTS_IT_LAUNCHER selects.
    launcher: str
    # JOBS as make integration passes it.
    jobs_text: str
    # ROTS_IT_SHARD, or None when unset.
    shard: Optional[str]


class RowKind(enum.Enum):
    """What a numbered line of the plan is."""

    # A group's own checkbox.
    GROUP = "group"
    # The line over a group's subgroups; its number toggles them together.
    HEADER = "header"
    # A subgroup's checkbox, indented under its group's header.
    SUBGROUP = "subgroup"


@dataclass(frozen=True)
class PlanRow:
    """One numbered line of the plan's checklist."""

    # The group or subgroup the line names.
    name: str
    # Whether the line is a checkbox of its own or a header.
    kind: RowKind
    # The positions in the ticks (one per offered group, in plan order) the line's number toggles:
    # its own for a checkbox, its subgroups' for a header.
    checkboxes: tuple[int, ...]


class AnswerOutcome(enum.Enum):
    """What an answer at the prompt does."""

    # Keep asking, with the ticks the answer left.
    CONTINUE = "continue"
    # Run the ticked groups.
    START = "start"
    # Run nothing.
    CANCEL = "cancel"


@dataclass(frozen=True)
class AnswerResult:
    """An answer's outcome, the ticks after it, and a message for an answer that changed nothing."""

    # What happens next.
    outcome: AnswerOutcome
    # One tick per offered group, in plan order.
    ticked: tuple[bool, ...]
    # Why the answer changed nothing, or None.
    message: Optional[str] = None


@dataclass(frozen=True)
class PromptResult:
    """How the prompt ended."""

    # START or CANCEL.
    outcome: AnswerOutcome
    # One tick per offered group, in plan order.
    ticked: tuple[bool, ...]
    # Whether the run starts because nobody answered in time.
    timed_out: bool = False


class Gate(enum.Enum):
    """What happens once the plan is shown."""

    # Run without asking.
    RUN = "run"
    # Ask at the terminal.
    ASK = "ask"
    # Stop with EXIT_NEEDS_CONFIRMATION.
    NEEDS_CONFIRMATION = "needs confirmation"


def parse_skip_groups(value: str, registered: Sequence[str]) -> list[str]:
    """The group names in SKIP_GROUPS, separated by spaces or commas, without repeats; raises ValueError for an unregistered name."""
    names: list[str] = []
    for name in SEPARATOR_PATTERN.split(value.strip()):
        if name and name not in names:
            names.append(name)
    unknown = [name for name in names if name not in registered]
    if unknown:
        raise ValueError(f"SKIP_GROUPS names {', '.join(unknown)}, which no group file registers; registered: {', '.join(registered)}")
    return names


def parse_confirm(value: str) -> bool:
    """Whether CONFIRM says yes; raises ValueError for anything but yes or nothing."""
    if value.strip().lower() == CONFIRM_YES:
        return True
    if not value.strip():
        return False
    raise ValueError(f"CONFIRM must be {CONFIRM_YES} or empty, not {value!r}")


def parse_confirm_minutes(value: Optional[str]) -> float:
    """The confirmation threshold in minutes from ROTS_IT_CONFIRM_MINUTES, or the default when unset; raises ValueError."""
    if value is None:
        return DEFAULT_CONFIRM_MINUTES
    problem = ValueError(f"{CONFIRM_MINUTES_VARIABLE} must be a positive number of minutes, not {value!r}")
    try:
        minutes = float(value)
    except ValueError:
        raise problem from None
    if not math.isfinite(minutes) or minutes <= 0:
        raise problem
    return minutes


def parse_jobs(value: str) -> int:
    """The workers a JOBS value runs the tests on, for the estimate: 1 for a value pytest-xdist would not run in parallel."""
    text = value.strip()
    if text in CPU_COUNT_JOBS:
        return os.cpu_count() or 1
    try:
        return max(int(text), 1)
    except ValueError:
        return 1


def is_ci(environ: Mapping[str, str]) -> bool:
    """Whether CI=true, which GitHub Actions sets."""
    return environ.get("CI", "").lower() == "true"


def launcher_name(environ: Mapping[str, str]) -> str:
    """The launcher the run uses, as conftest.launcher_mode chooses it; that function checks the value."""
    return environ.get("ROTS_IT_LAUNCHER") or ("local" if platform.system() == "Linux" else "docker")


def build_plan(
    tests: Iterable[CollectedTest],
    registered: Sequence[str],
    skipped: Iterable[str],
    recorded_seconds: Mapping[str, float],
    workers: int,
    shard: Optional[Shard] = None,
    subgroups: Optional[Mapping[str, Sequence[str]]] = None,
    shard_seconds: Optional[Mapping[str, float]] = None,
) -> RunPlan:
    """The plan for the tests GROUP selects, collected without a shard: a test counts toward every registered group it is marked with.

    registered lists the groups and subgroups in registration order, each group's subgroups right
    after it; subgroups gives each group that has subgroups, with them. shard_seconds gives the
    committed times the shard is balanced by (sharding.in_shard).
    """
    test_list = list(tests)
    skipped_names = frozenset(skipped)
    parent_of = {subgroup: parent for parent, names in (subgroups or {}).items() for subgroup in names}
    offered: list[str] = []
    skipped_groups: list[str] = []
    for name in registered:
        if not any(name in test.markers for test in test_list):
            continue
        if parent_of.get(name) in skipped_names:
            # Its group's skipped line covers it.
            continue
        if name in skipped_names:
            skipped_groups.append(name)
        elif not (subgroups and name in subgroups):
            offered.append(name)
    registered_names = set(registered)
    ungrouped_ids = frozenset(test.node_id for test in test_list if not test.markers & registered_names)
    return RunPlan(
        test_list, offered, skipped_groups, skipped_names, dict(recorded_seconds), max(workers, 1), shard, ungrouped_ids, parent_of, dict(shard_seconds or {}),
    )


def plan_rows(plan: RunPlan) -> list[PlanRow]:
    """The plan's numbered lines in order: each offered group, and for a group with subgroups a header followed by its offered subgroups."""
    rows: list[PlanRow] = []
    headed: set[str] = set()
    for position, name in enumerate(plan.offered_groups):
        parent = plan.parent_of.get(name)
        if parent is None:
            rows.append(PlanRow(name, RowKind.GROUP, (position,)))
            continue
        # Registration order puts a group's subgroups together, so the header comes right before them.
        if parent not in headed:
            headed.add(parent)
            members = tuple(index for index, offered in enumerate(plan.offered_groups) if plan.parent_of.get(offered) == parent)
            rows.append(PlanRow(parent, RowKind.HEADER, members))
        rows.append(PlanRow(name, RowKind.SUBGROUP, (position,)))
    return rows


def header_box(row: PlanRow, ticked: Sequence[bool]) -> str:
    """A header's box: x with all its subgroups ticked, - with some, blank with none."""
    ticked_count = sum(1 for position in row.checkboxes if ticked[position])
    if ticked_count == len(row.checkboxes):
        return "x"
    if ticked_count:
        return "-"
    return " "


def unticked_groups(plan: RunPlan, ticked: Sequence[bool]) -> list[str]:
    """The groups the unticked boxes leave out, in plan order: each unticked group and subgroup, or a
    group itself instead of its subgroups when every one it offers is unticked."""
    names: list[str] = []
    for row in plan_rows(plan):
        if row.kind is RowKind.HEADER:
            if not any(ticked[position] for position in row.checkboxes):
                names.append(row.name)
            continue
        if ticked[row.checkboxes[0]]:
            continue
        parent = plan.parent_of.get(row.name)
        if parent is None or parent not in names:
            names.append(row.name)
    return names


def count_text(count: int) -> str:
    """'1 test' or 'N tests'."""
    return f"{count} test" if count == 1 else f"{count} tests"


def format_minutes(minutes: float) -> str:
    """Minutes to one decimal below 10, whole minutes from 10."""
    if minutes < 10:
        return f"{minutes:.1f} min"
    return f"{int(minutes + 0.5)} min"


def format_plan(plan: RunPlan, ticked: Sequence[bool], context: RunContext) -> list[str]:
    """The plan's lines: a heading, one numbered line per row of plan_rows, the skipped groups, the total and the footer."""
    heading = f"Integration run plan: launcher {context.launcher}, JOBS {context.jobs_text or '1'}"
    if context.shard:
        heading += f", shard {context.shard}"
    lines = [heading]
    rows = plan_rows(plan)
    name_widths = [len(row.name) + (len(SUBGROUP_INDENT) if row.kind is RowKind.SUBGROUP else 0) for row in rows]
    name_widths.extend(len(name) for name in plan.skipped_groups)
    name_width = max(name_widths, default=0)
    number_width = len(str(len(rows)))
    unticked = [name for name, is_ticked in zip(plan.offered_groups, ticked) if not is_ticked]
    for number, row in enumerate(rows, start=1):
        indent = ""
        if row.kind is RowKind.HEADER:
            box = header_box(row, ticked)
            group_ids = plan.header_test_ids(row.name, unticked)
        else:
            box = "x" if ticked[row.checkboxes[0]] else " "
            group_ids = plan.group_test_ids(row.name, unticked)
            if row.kind is RowKind.SUBGROUP:
                indent = SUBGROUP_INDENT
        minutes = format_minutes(plan.estimated_minutes(group_ids))
        name_text = f"{row.name:<{name_width - len(indent)}}"
        lines.append(f"  {indent}[{box}] {number:>{number_width}}. {name_text}  {count_text(len(group_ids)):>9}  {minutes:>8}")
    for name in plan.skipped_groups:
        group_ids = plan.group_test_ids(name, unticked)
        lines.append(f"  skipped: {name} ({count_text(len(group_ids))})")
    ungrouped = plan.ungrouped_count(unticked)
    if ungrouped:
        lines.append(f"  {count_text(ungrouped)} in no group")
    kept = plan.kept_test_ids(unticked)
    untimed = plan.untimed_count(kept)
    if untimed:
        verb = "has" if untimed == 1 else "have"
        note = f"estimate; {count_text(untimed)} {verb} no recorded time and count {DEFAULT_TEST_SECONDS:g} s each"
    else:
        note = "estimate from recorded times"
    lines.append(f"Total: {count_text(len(kept))}, about {format_minutes(plan.estimated_minutes(kept))} ({note}).")
    if plan.offered_groups:
        lines.append(UNTICK_FOOTER)
    if plan.shard is not None:
        lines.append(SHARD_FOOTER)
    return lines


def run_expression(group: str, excluded: Sequence[str]) -> str:
    """The -m expression: the scenarios in GROUP (every scenario when empty), without any test an excluded group marks."""
    expression = "scenario"
    if group:
        expression += f" and ({group})"
    if excluded:
        expression += f" and not ({' or '.join(excluded)})"
    return expression


def pytest_run_command(
    python: str, expression: Optional[str], jobs_text: str, targets: Sequence[str] = (SCENARIOS_RELATIVE,),
) -> list[str]:
    """The pytest command make integration runs, with -n only for more than one worker.

    targets are the paths or node ids pytest runs, relative to the repository root; expression,
    when not None, is the -m expression that narrows them.
    """
    command = [python, "-m", "pytest", *targets, "-q"]
    if expression is not None:
        command += ["-m", expression]
    if jobs_text.strip() not in SINGLE_WORKER_JOBS:
        command += ["-n", jobs_text.strip()]
    return command + list(REPORT_ARGUMENTS)


def collect_command(python: str, group: str, targets: Optional[Sequence[str]] = None) -> list[str]:
    """The pytest command that collects GROUP's scenarios and reports them through collect_report.

    targets, when given, are paths or node ids relative to the repository root, collected as
    named instead of GROUP's selection.
    """
    command = [python, "-m", "pytest"]
    if targets is None:
        command += [SCENARIOS_RELATIVE, "-q", "--collect-only", "-p", collect_report.__name__, "-m", run_expression(group, [])]
    else:
        command += [*targets, "-q", "--collect-only", "-p", collect_report.__name__]
    return command


def repeat_command(python: str, group: str, excluded: Sequence[str], jobs_text: str) -> str:
    """The make integration command that repeats a run leaving out the excluded groups, without asking."""
    parts = ["make", "integration"]
    if python != DEFAULT_PYTHON:
        parts.append(f"PYTHON={shlex.quote(python)}")
    if group:
        parts.append(f"GROUP={shlex.quote(group)}")
    if jobs_text.strip() not in SINGLE_WORKER_JOBS:
        parts.append(f"JOBS={shlex.quote(jobs_text.strip())}")
    parts.append(f"SKIP_GROUPS={shlex.quote(' '.join(excluded))}")
    parts.append(f"CONFIRM={CONFIRM_YES}")
    return " ".join(parts)


def apply_answer(text: str, ticked: Sequence[bool], rows: Sequence[PlanRow]) -> AnswerResult:
    """What one line typed at the prompt does to the ticks.

    rows numbers the plan's lines from 1 (plan_rows). A number toggles its group or subgroup. A
    header's number unticks all its subgroups when any is ticked and ticks them all otherwise. A
    range toggles each checkbox it covers once, a header covering its subgroups. a ticks all, n
    unticks all, Enter or y starts and q cancels. Starting with nothing ticked, or an answer with
    any part that is not one of these, changes nothing and says why.
    """
    current = tuple(ticked)
    answer = text.strip().lower()
    if answer in START_ANSWERS:
        if not any(current):
            return AnswerResult(AnswerOutcome.CONTINUE, current, "nothing is ticked: tick a group, or q to cancel")
        return AnswerResult(AnswerOutcome.START, current)
    if answer == "q":
        return AnswerResult(AnswerOutcome.CANCEL, current)
    if answer == "a":
        return AnswerResult(AnswerOutcome.CONTINUE, (True,) * len(current))
    if answer == "n":
        return AnswerResult(AnswerOutcome.CONTINUE, (False,) * len(current))
    toggled = list(current)
    for token in SEPARATOR_PATTERN.split(answer):
        if not token:
            continue
        match = GROUP_RANGE_PATTERN.fullmatch(token)
        first = int(match.group(1)) if match else 0
        last = int(match.group(2) or first) if match else 0
        if not match or first < 1 or last < first or last > len(rows):
            return AnswerResult(AnswerOutcome.CONTINUE, current, f"{token!r} is not a group number from 1 to {len(rows)} or a range such as 2-3")
        if first == last and rows[first - 1].kind is RowKind.HEADER:
            members = rows[first - 1].checkboxes
            tick_all = not any(toggled[position] for position in members)
            for position in members:
                toggled[position] = tick_all
            continue
        positions: list[int] = []
        for row in rows[first - 1:last]:
            positions.extend(position for position in row.checkboxes if position not in positions)
        for position in positions:
            toggled[position] = not toggled[position]
    return AnswerResult(AnswerOutcome.CONTINUE, tuple(toggled))


def decide_gate(*, confirm: bool, interactive: bool, estimate_minutes: float, threshold_minutes: float) -> Gate:
    """Run when CONFIRM=yes, ask at a terminal, and otherwise run only an estimate under the threshold."""
    if confirm:
        return Gate.RUN
    if interactive:
        return Gate.ASK
    if estimate_minutes < threshold_minutes:
        return Gate.RUN
    return Gate.NEEDS_CONFIRMATION


class TimedLineReader:
    """Reads lines from a blocking source on a daemon thread, so a caller can wait for one with a timeout.

    The thread reads one line per request, never ahead: once a read has returned, nothing reads
    the source until the next request. A read that timed out stays outstanding (`reading`) and
    its line is kept for the next read; standard Python cannot cancel a blocked readline.
    """

    def __init__(self, read_line: Callable[[], str]) -> None:
        # Returns the next line, or "" at the end of input.
        self._read_line = read_line
        # One entry per line asked for.
        self._requests: queue.Queue[None] = queue.Queue()
        # The lines read, in order.
        self._lines: queue.Queue[str] = queue.Queue()
        # Whether a line was asked for and not yet returned.
        self.reading = False
        threading.Thread(target=self._serve, name="run-plan-input", daemon=True).start()

    def _serve(self) -> None:
        """Reads one line per request; a source that fails reads as the end of input."""
        while True:
            self._requests.get()
            try:
                line = self._read_line()
            except (OSError, ValueError):
                line = ""
            self._lines.put(line)

    def read(self, timeout: Optional[float]) -> Optional[str]:
        """The next line ("" at the end of input), or None when timeout seconds pass first; None waits for ever."""
        if not self.reading:
            self._requests.put(None)
            self.reading = True
        try:
            line = self._lines.get(timeout=timeout)
        except queue.Empty:
            return None
        self.reading = False
        return line


def format_wait(seconds: float) -> str:
    """A wait in whole minutes, or in seconds below one minute."""
    if seconds >= 60:
        return f"{math.ceil(seconds / 60)} min"
    return f"{math.ceil(seconds)} s"


def ask(
    group_count: int,
    render: Callable[[tuple[bool, ...]], list[str]],
    reader,
    write: Callable[[str], None],
    *,
    timeout_seconds: float,
    countdown_step_seconds: float,
    rows: Sequence[PlanRow],
) -> PromptResult:
    """Asks which of group_count groups to run, starting with all of them ticked.

    render gives the plan's lines for a set of ticks, and rows numbers them for apply_answer;
    reader.read(timeout) gives the next line, "" at the end of input, or None after timeout
    seconds. Until the first answer a line counts down each countdown_step_seconds, and the run
    starts with the groups still ticked once timeout_seconds pass; after it the prompt waits for
    ever. The end of input cancels.
    """
    ticked = (True,) * group_count
    for line in render(ticked):
        write(line)
    write(PROMPT_HELP)
    write(f"With no answer in {format_wait(timeout_seconds)}, the run starts.")
    deadline = time.monotonic() + timeout_seconds
    answered = False
    while True:
        if answered:
            line = reader.read(None)
        else:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                write(f"No answer in {format_wait(timeout_seconds)}: starting.")
                return PromptResult(AnswerOutcome.START, ticked, timed_out=True)
            line = reader.read(min(countdown_step_seconds, remaining))
            if line is None:
                remaining = deadline - time.monotonic()
                if remaining > 0:
                    write(f"No answer yet: the run starts by itself in {format_wait(remaining)}.")
                continue
            answered = True
        if line == "":
            write("Input closed: cancelled.")
            return PromptResult(AnswerOutcome.CANCEL, ticked)
        result = apply_answer(line, ticked, rows)
        if result.outcome is not AnswerOutcome.CONTINUE:
            return PromptResult(result.outcome, result.ticked)
        if result.message:
            write(result.message)
            continue
        ticked = result.ticked
        for plan_line in render(ticked):
            write(plan_line)


class ChildSignalForwarder:
    """While installed, passes the first SIGTERM or SIGHUP this process receives to a child process, and ignores Ctrl-C.

    pytest stops on SIGTERM and SIGHUP as on Ctrl-C, stopping its servers and releasing the Docker
    lock. GNU make, and anything else stopping this process, signals only this process, so the
    signal is passed on; a signal that arrives before the child exists is passed on once it is
    attached. Later stopping signals are ignored, since a second interrupt would cut pytest's
    cleanup short. Ctrl-C at a terminal reaches pytest directly, with its whole process group, so
    it is ignored here. A signal this process already ignores stays ignored, for the child to
    inherit. Install, attach and restore on the main thread.
    """

    # The signals passed on to the child; SIGHUP only where the platform has it.
    FORWARDED_SIGNAL_NAMES = ("SIGTERM", "SIGHUP")

    def __init__(self) -> None:
        # The child signals are passed to; None until attach.
        self._process: Optional[subprocess.Popen] = None
        # A signal received before attach, to pass on once the child exists.
        self._pending: Optional[int] = None
        # Whether a stopping signal has been received; later ones are ignored.
        self._received = False
        # The handler each signal had before install, which restore puts back.
        self._previous_handlers: dict[int, object] = {}

    def install(self) -> None:
        """Takes over SIGINT, SIGTERM and SIGHUP, except any this process ignores."""
        handlers = {"SIGINT": self._ignore}
        for signal_name in self.FORWARDED_SIGNAL_NAMES:
            handlers[signal_name] = self._forward
        for signal_name, handler in handlers.items():
            signal_number = getattr(signal, signal_name, None)
            if signal_number is None or signal.getsignal(signal_number) == signal.SIG_IGN:
                continue
            # A Python handler, unlike SIG_IGN, is not inherited: a child started after install
            # still gets each signal's default action.
            self._previous_handlers[signal_number] = signal.signal(signal_number, handler)

    def attach(self, process) -> None:
        """Makes process the child signals go to, passing on one that arrived before it existed."""
        self._process = process
        pending = self._pending
        self._pending = None
        if pending is not None:
            process.send_signal(pending)

    def restore(self) -> None:
        """Puts back the handlers install replaced."""
        for signal_number, handler in self._previous_handlers.items():
            signal.signal(signal_number, handler)
        self._previous_handlers.clear()

    def _ignore(self, signal_number: int, _frame: object) -> None:
        """Does nothing: the child receives Ctrl-C itself."""

    def _forward(self, signal_number: int, _frame: object) -> None:
        """Passes the first stopping signal to the child, or keeps it until the child is attached."""
        if self._received:
            return
        self._received = True
        if self._process is None:
            self._pending = signal_number
        else:
            self._process.send_signal(signal_number)


def run_in_foreground(command: list[str], keep_stdin: bool) -> int:
    """Runs command in the repository root and returns its exit status, 128 + N for a child killed by signal N.

    stdin is the terminal's only when keep_stdin. While it runs, ChildSignalForwarder passes the
    first SIGTERM or SIGHUP to it and Ctrl-C is left to it, and this process waits for it to end.
    """
    forwarder = ChildSignalForwarder()
    forwarder.install()
    try:
        process = subprocess.Popen(command, cwd=REPO_ROOT, stdin=None if keep_stdin else subprocess.DEVNULL)
        forwarder.attach(process)
        while True:
            # A signal handler runs on the main thread only; waiting in steps lets it run even
            # when the signal was delivered to another thread.
            try:
                status = process.wait(timeout=WAIT_POLL_SECONDS)
                break
            except subprocess.TimeoutExpired:
                continue
    finally:
        forwarder.restore()
    if status < 0:
        return 128 - status
    return status


def collect_selection(
    python: str, group: str, environ: Mapping[str, str], targets: Optional[Sequence[str]] = None, timeout: Optional[float] = None,
) -> tuple[Optional[list[CollectedTest]], int]:
    """The tests a run of GROUP selects in the environment environ, and pytest's exit status.

    targets, when given, are collected as named instead (collect_command). The tests are None
    when collection fails; pytest's output is then printed to stderr. Ctrl-C raises
    KeyboardInterrupt once the collecting pytest has been stopped, and a collection outliving
    timeout seconds raises subprocess.TimeoutExpired once it has been stopped.
    """
    with tempfile.TemporaryDirectory(prefix="rots-run-plan-") as folder:
        report_path = Path(folder) / "collected.json"
        child_environment = dict(environ)
        child_environment[collect_report.REPORT_PATH_VARIABLE] = str(report_path)
        # The plugin is imported before pytest.ini's pythonpath applies.
        child_environment["PYTHONPATH"] = os.pathsep.join(filter(None, [str(INTEGRATION_ROOT), environ.get("PYTHONPATH")]))
        completed = subprocess.run(
            collect_command(python, group, targets), cwd=REPO_ROOT, env=child_environment, stdin=subprocess.DEVNULL, capture_output=True,
            text=True, check=False, timeout=timeout,
        )
        # 5: nothing collected. The plugin still reports the empty selection.
        if completed.returncode in (0, 5):
            try:
                return collect_report.read_report(report_path), completed.returncode
            except (OSError, ValueError, KeyError, TypeError):
                pass
    sys.stderr.write(completed.stdout)
    sys.stderr.write(completed.stderr)
    print(f"error: collecting the selection failed (pytest exit status {completed.returncode})", file=sys.stderr)
    return None, completed.returncode or EXIT_USAGE


def parse_shard_variable(value: Optional[str]) -> Optional[Shard]:
    """The shard ROTS_IT_SHARD names, or None when it is unset; raises ValueError for a value conftest.py would refuse."""
    if value is None:
        return None
    try:
        return sharding.parse_shard(value)
    except ValueError as error:
        raise ValueError(f"{SHARD_VARIABLE}: {error}") from None


def run_ci(python: str, group: str, skip_groups: str, jobs_text: str, run_command: Callable[[list[str], bool], int]) -> int:
    """Runs pytest on GROUP's scenarios without the SKIP_GROUPS groups, with no plan and no question, and returns its status.

    This is the whole of a CI=true run. An unregistered SKIP_GROUPS name prints an error and
    returns EXIT_USAGE; an empty SKIP_GROUPS reads no group file.
    """
    skipped: list[str] = []
    if skip_groups.strip():
        try:
            registered = list(group_files.read_group_descriptions(GROUPS_DIR))
            skipped = parse_skip_groups(skip_groups, registered)
        except (ValueError, OSError) as error:
            print(f"error: {error}", file=sys.stderr)
            return EXIT_USAGE
    return run_command(pytest_run_command(python, run_expression(group, skipped), jobs_text), True)


def _print_lines(lines: Iterable[str]) -> None:
    """Prints each line and flushes, so the plan shows before anything a child process prints."""
    for line in lines:
        print(line)
    sys.stdout.flush()


def _write_line(line: str) -> None:
    """Prints one line at once."""
    print(line, flush=True)


def main(
    argv: Optional[list[str]] = None,
    *,
    environ: Optional[Mapping[str, str]] = None,
    run_command: Callable[[list[str], bool], int] = run_in_foreground,
    collect: Callable[[str, str, Mapping[str, str]], tuple[Optional[list[CollectedTest]], int]] = collect_selection,
    interactive: Optional[bool] = None,
    reader=None,
    durations_path: Optional[Path] = None,
    shard_durations_path: Optional[Path] = None,
    prompt_timeout_seconds: float = PROMPT_TIMEOUT_SECONDS,
    countdown_step_seconds: float = COUNTDOWN_STEP_SECONDS,
) -> int:
    """Shows the plan, applies the gate and returns pytest's exit status, or EXIT_USAGE, EXIT_NEEDS_CONFIRMATION or EXIT_CANCELLED.

    A failed collection returns pytest's status. --plan-only shows the plan and returns 0, CI or not.
    The keyword arguments replace the environment, the terminal and the subprocesses in tests.
    """
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--python", required=True, help="the Python that runs pytest")
    parser.add_argument("--group", default="", help="GROUP: a group or -m expression of groups; empty selects every scenario")
    parser.add_argument("--skip-groups", default="", help="SKIP_GROUPS: groups to leave out, separated by spaces or commas")
    parser.add_argument("--confirm", default="", help=f"CONFIRM: {CONFIRM_YES} starts the run without asking")
    parser.add_argument("--jobs", default="", help="the pytest-xdist workers; empty, 0 or 1 runs without them")
    parser.add_argument("--plan-only", action="store_true", help="print the plan and exit 0 without asking or running")
    arguments = parser.parse_args(argv)
    environment = os.environ if environ is None else environ
    if not arguments.plan_only and is_ci(environment):
        return run_ci(arguments.python, arguments.group, arguments.skip_groups, arguments.jobs, run_command)
    shard_text = environment.get(SHARD_VARIABLE)
    try:
        group_list = group_files.read_groups(GROUPS_DIR)
        registered = [group.name for group in group_list]
        skipped = parse_skip_groups(arguments.skip_groups, registered)
        confirm = parse_confirm(arguments.confirm)
        threshold_minutes = parse_confirm_minutes(environment.get(CONFIRM_MINUTES_VARIABLE))
        shard = parse_shard_variable(shard_text)
    except (ValueError, OSError) as error:
        print(f"error: {error}", file=sys.stderr)
        return EXIT_USAGE
    # The plan shards the tests left after the groups it leaves out, as the run's conftest.py
    # does, so the collection must see every selected test.
    collect_environment = {name: value for name, value in environment.items() if name != SHARD_VARIABLE}
    try:
        tests, status = collect(arguments.python, arguments.group, collect_environment)
    except KeyboardInterrupt:
        _write_line(f"Cancelled while collecting the tests; nothing ran (exit status {EXIT_CANCELLED}).")
        return EXIT_CANCELLED
    if tests is None:
        return status
    recorded = durations.read_durations(durations_path or REPO_ROOT / durations.DURATIONS_RELATIVE)
    shard_seconds: dict[str, float] = {}
    if shard is not None:
        shard_seconds = durations.read_durations(shard_durations_path or INTEGRATION_ROOT / sharding.SHARD_DURATIONS_RELATIVE)
    plan = build_plan(
        tests, registered, skipped, recorded, parse_jobs(arguments.jobs), shard, group_files.subgroups_by_parent(group_list), shard_seconds,
    )
    context = RunContext(launcher_name(environment), arguments.jobs, shard_text)

    def render(ticked: tuple[bool, ...]) -> list[str]:
        return format_plan(plan, ticked, context)

    ticked = (True,) * len(plan.offered_groups)
    if arguments.plan_only:
        _print_lines(render(ticked))
        return 0
    keep_stdin = True
    estimate = plan.estimated_minutes(plan.kept_test_ids([]))
    if interactive is None:
        interactive = sys.stdin.isatty() and sys.stdout.isatty()
    gate = decide_gate(confirm=confirm, interactive=interactive, estimate_minutes=estimate, threshold_minutes=threshold_minutes)
    if gate is Gate.ASK and plan.offered_groups:
        line_reader = reader if reader is not None else TimedLineReader(sys.stdin.readline)
        try:
            result = ask(
                len(plan.offered_groups), render, line_reader, _write_line, timeout_seconds=prompt_timeout_seconds, countdown_step_seconds=countdown_step_seconds,
                rows=plan_rows(plan),
            )
        except KeyboardInterrupt:
            # Ctrl-C at the prompt cancels like q.
            result = PromptResult(AnswerOutcome.CANCEL, ticked)
        if result.outcome is AnswerOutcome.CANCEL:
            _write_line(f"Cancelled; nothing ran (exit status {EXIT_CANCELLED}).")
            return EXIT_CANCELLED
        ticked = result.ticked
        # A readline still waiting would take a line typed for pytest (a breakpoint's pdb, say).
        keep_stdin = not line_reader.reading
        if not keep_stdin:
            _write_line("The run started by itself, so it reads no input from this terminal.")
    else:
        _print_lines(render(ticked))
        if gate is Gate.NEEDS_CONFIRMATION:
            _write_line(
                f"{NEEDS_CONFIRMATION_PREFIX} the run is estimated at about {format_minutes(estimate)}, reaching {CONFIRM_MINUTES_VARIABLE} "
                f"({threshold_minutes:g} min), with no terminal to ask at; ask first, then start it with CONFIRM={CONFIRM_YES}, "
                f"or choose fewer groups with GROUP= or SKIP_GROUPS= (exit status {EXIT_NEEDS_CONFIRMATION})."
            )
            return EXIT_NEEDS_CONFIRMATION
    excluded = skipped + [name for name in unticked_groups(plan, ticked) if name not in skipped]
    status = run_command(pytest_run_command(arguments.python, run_expression(arguments.group, excluded), arguments.jobs), keep_stdin)
    if excluded:
        _write_line(f"Repeat: {repeat_command(arguments.python, arguments.group, excluded, arguments.jobs)}")
    return status


if __name__ == "__main__":
    sys.exit(main())
