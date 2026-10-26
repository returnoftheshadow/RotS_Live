"""Chooses and runs the tests of the on-request AddressSanitizer CI run (.github/workflows/asan.yml).

Nothing runs unless the pull request's description holds an `@asan-test-` token. HTML comments
are dropped first, as ci_groups drops them, and tokens are lower case:

- `@asan-test-all`: every unit test and every scenario, the scenarios split across shards. It
  wins over every other token, though the others are still checked.
- `@asan-test-<group>`: the scenarios of one registered group or subgroup; `all`, `unit` and
  `scenario` never name a group.
- `@asan-test-unit=<filter>`: the unit tests a GoogleTest filter matches. Repeatable; the filters
  are joined with ':' into one --gtest_filter, after each has been checked to match a test.
- `@asan-test-scenario=<id>`: a scenario file or test, as a pytest node id relative to
  tests/integration, such as `scenarios/test_bank.py::test_name`. Repeatable.

Unit tests run only for `all` or a unit token, and scenarios only for `all`, a group or a scenario
token. A value runs to the next whitespace and must match a strict pattern; a token that does not,
names an unknown group or a missing scenario file, or selects no test stops the run with the token
and the reason. Every pull request event with a token runs the tests.

Run with tests/integration on PYTHONPATH as `python3 -m rots_harness.asan_selection <command>`:
`select` reads the description from the event file GITHUB_EVENT_PATH names, so it is never part
of a command or of the environment; `check-scenarios` fails when a group or scenario token selects
no test; `run-unit` and `run-scenarios` run the selection inside the development image.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable, Iterable, Mapping, Optional, Sequence

from rots_harness import ci_groups, run_plan
from rots_harness import groups as group_files

# The token that runs everything; the same word ci_groups uses.
ALL_TOKEN = ci_groups.ALL_GROUPS_TOKEN
# The token name taking a GoogleTest filter after '='.
UNIT_TOKEN = "unit"
# The token name taking a scenario node id after '='.
SCENARIO_TOKEN = "scenario"
# Token names that never name a group.
RESERVED_NAMES = frozenset({ALL_TOKEN, UNIT_TOKEN, SCENARIO_TOKEN})
# What starts every token, for messages.
TOKEN_PREFIX = "@asan-test-"
# A token: the name stops at the first character a marker name cannot hold, as in ci_groups. The
# value after '=' runs to the next whitespace, so a stray character is refused, never dropped.
DESCRIPTION_TOKEN = re.compile(r"(?<![\w-])@asan-test-([A-Za-z0-9_]+)(?:=(\S*))?")
# The token prefix in any case, to point out one written in capitals, which DESCRIPTION_TOKEN skips.
ANY_CASE_PREFIX = re.compile(r"(?<![\w-])@asan-test-\S*", re.IGNORECASE)
# A GoogleTest filter of positive patterns only, separated by ':'.
GTEST_FILTER = re.compile(r"[A-Za-z0-9_.*?]+(?::[A-Za-z0-9_.*?]+)*")
# One character a GoogleTest filter may hold.
GTEST_FILTER_CHARACTER = re.compile(r"[A-Za-z0-9_.*?:]")
# The characters GTEST_FILTER_CHARACTER accepts, for messages.
GTEST_FILTER_CHARACTERS_TEXT = "letters, digits and _ . * ? :"
# Joins the unit filters into one --gtest_filter.
GTEST_FILTER_SEPARATOR = ":"
# The folder scenario ids start with, relative to tests/integration.
SCENARIO_ID_FOLDER = "scenarios/"
# A scenario node id: a scenario file, then optionally a test function and a parameter id. The
# suite's parameter ids hold letters, digits and _ - . / % ~; one with a space cannot be one token.
SCENARIO_ID = re.compile(r"scenarios/(test_[A-Za-z0-9_]+\.py)(?:::[A-Za-z_][A-Za-z0-9_]*(?:\[[A-Za-z0-9_.\-/%~]+\])?)?")
# Characters a value is often wrapped in or followed by in prose; a refused value holding one gets
# PLAIN_TEXT_HINT.
PROSE_CHARACTERS = frozenset("`.,;!)'\"")
# How to write a token whose value picked up the description's punctuation or Markdown.
PLAIN_TEXT_HINT = "write the token as plain text, outside backticks, with a space or line break after it"
# The characters description and log text are echoed with; every other character becomes '?', so
# the text cannot form a workflow command (## or ::), bidirectional or zero-width
# controls, or a terminal escape in the log.
ECHO_SAFE_CHARACTER = re.compile(r"[ A-Za-z0-9_.*?:/=@\-~%\[\]()$;|&`'\",!<>+{}^\\]")
# tests/integration relative to the repository root; scenario ids are relative to it.
INTEGRATION_RELATIVE = "tests/integration/"
# Where the harness keeps each test's run directory, relative to the repository root (conftest.py).
RUNS_RELATIVE = "build/integration/runs"
# The file in a run directory naming its test, and the server's log (conftest.py).
TEST_NAME_FILE = "test-id.txt"
GAME_LOG_NAME = "game.log"
# The line AddressSanitizer starts each report with.
SANITIZER_ERROR_TEXT = "ERROR: AddressSanitizer"
# The artifact the workflow uploads a shard's kept run directories as.
RUNS_ARTIFACT_FORMAT = "asan-runs-shard-{number}"
# The most report lines a scenario summary quotes.
SUMMARY_REPORT_LINE_LIMIT = 20
# This module's checkout: rots_harness is in tests/integration.
DEFAULT_REPO_ROOT = Path(__file__).resolve().parents[3]
# The variable naming the webhook event's JSON file on a GitHub runner.
EVENT_PATH_VARIABLE = "GITHUB_EVENT_PATH"
# What select writes for the unit tests and the scenarios: none, all, or the chosen ones.
MODE_NONE = "none"
MODE_ALL = "all"
MODE_FILTERS = "filters"
MODE_SELECTED = "selected"
# The variables the select job's outputs reach run-unit, run-scenarios and check-scenarios through.
UNIT_MODE_VARIABLE = "ASAN_UNIT"
UNIT_FILTERS_VARIABLE = "ASAN_UNIT_FILTERS"
SCENARIO_MODE_VARIABLE = "ASAN_SCENARIOS"
GROUPS_VARIABLE = "ASAN_GROUPS"
SCENARIO_IDS_VARIABLE = "ASAN_SCENARIO_IDS"
# The select output each variable carries.
VARIABLE_OUTPUTS = {
    UNIT_MODE_VARIABLE: "unit",
    UNIT_FILTERS_VARIABLE: "unit_filters",
    SCENARIO_MODE_VARIABLE: "scenarios",
    GROUPS_VARIABLE: "groups",
    SCENARIO_IDS_VARIABLE: "scenario_ids",
}
# The variables each command reads.
COMMAND_VARIABLES = {
    "run-unit": (UNIT_MODE_VARIABLE, UNIT_FILTERS_VARIABLE),
    "check-scenarios": (SCENARIO_MODE_VARIABLE, GROUPS_VARIABLE, SCENARIO_IDS_VARIABLE),
    "run-scenarios": (SCENARIO_MODE_VARIABLE, GROUPS_VARIABLE, SCENARIO_IDS_VARIABLE),
}
# Tokens or inputs that cannot be used, or a group file or event file that cannot be read.
EXIT_USAGE = 2
# A token that selects no test.
EXIT_NOTHING_SELECTED = 3
# The most test names a summary lists for one token before it counts the rest.
SUMMARY_NAME_LIMIT = 50
# Seconds one collection may take; it boots no server.
COLLECT_TIMEOUT_SECONDS = 300


class InvalidTokens(ValueError):
    """The description holds tokens that cannot be used; the message has one line per token, naming it and why."""


class NothingSelected(RuntimeError):
    """A token selects no test, or its tests could not be listed; the message names the token and what happened."""


@dataclass(frozen=True)
class Token:
    """One @asan-test token as the description writes it."""

    # The token as written, such as "@asan-test-unit=Foo.*".
    text: str
    # What follows the prefix up to '=' or the end of the name: all, unit, scenario or a group.
    name: str
    # The text after '=', or None when the token has none.
    value: Optional[str]


@dataclass
class AsanSelection:
    """What an ASan run tests, and one line per token saying why."""

    # Whether @asan-test-all asked for every unit test and every scenario.
    run_all: bool = False
    # The GoogleTest filters of the unit tokens, in order; each is checked to match even beside all.
    unit_filters: list[str] = field(default_factory=list)
    # The groups and subgroups the group tokens name, in order.
    groups: list[str] = field(default_factory=list)
    # The scenario node ids of the scenario tokens, relative to tests/integration, in order.
    scenario_ids: list[str] = field(default_factory=list)
    # What each token adds, for the job summary.
    reasons: list[str] = field(default_factory=list)

    def unit_mode(self) -> str:
        """MODE_ALL, MODE_FILTERS or MODE_NONE."""
        if self.run_all:
            return MODE_ALL
        return MODE_FILTERS if self.unit_filters else MODE_NONE

    def scenario_mode(self) -> str:
        """MODE_ALL, MODE_SELECTED or MODE_NONE."""
        if self.run_all:
            return MODE_ALL
        return MODE_SELECTED if self.groups or self.scenario_ids else MODE_NONE

    def shard_numbers(self, full_suite_shards: int) -> list[int]:
        """The scenario shards: 1 to full_suite_shards for every scenario, otherwise one."""
        return list(range(1, full_suite_shards + 1)) if self.run_all else [1]


def echo_safe(text: str) -> str:
    """text with every character outside ECHO_SAFE_CHARACTER replaced by '?', for echoing description text."""
    return "".join(character if ECHO_SAFE_CHARACTER.fullmatch(character) else "?" for character in text)


def read_description(event_path: Path) -> str:
    """The pull request description in the webhook event file at event_path; empty when it has none.

    Raises OSError or ValueError when the file cannot be read as an event.
    """
    event = json.loads(event_path.read_text(encoding="utf-8"))
    pull_request = event.get("pull_request") if isinstance(event, dict) else None
    if not isinstance(pull_request, dict):
        raise ValueError(f"{event_path} holds no pull_request object")
    body = pull_request.get("body")
    return body if isinstance(body, str) else ""


def description_tokens(body: str) -> list[Token]:
    """The description's tokens outside HTML comments, in order and without repeats."""
    tokens: list[Token] = []
    for match in DESCRIPTION_TOKEN.finditer(ci_groups.HTML_COMMENT.sub("", body)):
        token = Token(match.group(0), match.group(1), match.group(2))
        if token not in tokens:
            tokens.append(token)
    return tokens


def capitalised_prefixes(body: str) -> list[str]:
    """Each token-like word outside HTML comments whose prefix is not written in lower case, in order and without repeats."""
    found: list[str] = []
    for match in ANY_CASE_PREFIX.finditer(ci_groups.HTML_COMMENT.sub("", body)):
        if not match.group(0).startswith(TOKEN_PREFIX) and match.group(0) not in found:
            found.append(match.group(0))
    return found


def run_decision(action: str, tokens: Sequence[Token]) -> tuple[bool, str]:
    """Whether this pull request event runs the selected tests, and why: every event runs them while the description holds a token."""
    if not tokens:
        return False, "the description has no @asan-test token"
    return True, f"the pull request event is {action}, and the description has @asan-test tokens"


def with_prose_hint(problem: str, value: str) -> str:
    """problem, followed by PLAIN_TEXT_HINT when value holds or ends in a character prose wraps a token in."""
    if "`" in value or (value and value[-1] in PROSE_CHARACTERS):
        return f"{problem} ({PLAIN_TEXT_HINT})"
    return problem


def gtest_filter_problem(value: str) -> Optional[str]:
    """Why value cannot be a unit token's GoogleTest filter, or None when it can.

    No test name holds '-', so GoogleTest reads the first one as the start of negative patterns.
    Joined with the other unit tokens' filters, one token's negative part would exclude their
    tests too, and a leading '-' alone would run every test but a few; '-' is refused instead.
    ':' separates positive patterns, as repeating the token does.
    """
    if not value:
        return f"needs a GoogleTest filter after '=', such as {TOKEN_PREFIX}{UNIT_TOKEN}=SuiteName.*"
    if "-" in value:
        return (
            "may not hold '-': GoogleTest reads it as the start of negative patterns, which would also exclude "
            "the tests the other unit tokens name; name the tests to run instead"
        )
    for character in value:
        if not GTEST_FILTER_CHARACTER.fullmatch(character):
            return with_prose_hint(f"may hold only {GTEST_FILTER_CHARACTERS_TEXT}, not {echo_safe(repr(character))}", value)
    if not GTEST_FILTER.fullmatch(value):
        return "has an empty pattern beside a ':'"
    return None


def scenario_id_problem(value: str, repo_root: Path) -> Optional[str]:
    """Why value cannot be a scenario token's node id in the checkout at repo_root, or None when it can."""
    if not value:
        return f"needs a scenario id after '=', such as {TOKEN_PREFIX}{SCENARIO_TOKEN}={SCENARIO_ID_FOLDER}test_bank.py"
    if ".." in value:
        return "may not hold '..'"
    if value.startswith("/"):
        return "must be relative to tests/integration, not an absolute path"
    if value.startswith(INTEGRATION_RELATIVE):
        return f"must be relative to tests/integration: leave out the leading {INTEGRATION_RELATIVE}"
    match = SCENARIO_ID.fullmatch(value)
    if match is None:
        problem = (
            f"must be {SCENARIO_ID_FOLDER}test_<name>.py, optionally followed by ::<test name> and a [<parameter id>] "
            "of letters, digits and _ - . / % ~"
        )
        return with_prose_hint(problem, value)
    scenario_file = repo_root / INTEGRATION_RELATIVE / SCENARIO_ID_FOLDER / match.group(1)
    if not scenario_file.is_file():
        return f"names {INTEGRATION_RELATIVE}{SCENARIO_ID_FOLDER}{match.group(1)}, which does not exist"
    return None


def unknown_groups_text(unknown: Sequence[str], groups: Sequence[str]) -> str:
    """ci_groups' message for unknown group names, listing this workflow's other token forms."""
    return ci_groups.unknown_groups_message(unknown, groups, (ALL_TOKEN, f"{UNIT_TOKEN}=<filter>", f"{SCENARIO_TOKEN}=<id>"))


def select_tests(body: str, groups: Sequence[str], repo_root: Path) -> AsanSelection:
    """The tests the description's tokens ask for; groups lists the registered groups and subgroups.

    Every token is checked, even beside @asan-test-all. Raises InvalidTokens listing each token
    that cannot be used, with its text made echo-safe.
    """
    selection = AsanSelection()
    problems = [f"{echo_safe(text)}: tokens are lower case; write {TOKEN_PREFIX}" for text in capitalised_prefixes(body)]
    unknown: list[str] = []
    for token in description_tokens(body):
        problem: Optional[str] = None
        if token.name == UNIT_TOKEN:
            problem = gtest_filter_problem(token.value) if token.value is not None else "needs =<GoogleTest filter>"
            if problem is None and token.value not in selection.unit_filters:
                selection.unit_filters.append(token.value)
                selection.reasons.append(f"`{token.text}`: the unit tests matching `{token.value}`")
        elif token.name == SCENARIO_TOKEN:
            problem = scenario_id_problem(token.value, repo_root) if token.value is not None else "needs =<scenario id>"
            if problem is None and token.value not in selection.scenario_ids:
                selection.scenario_ids.append(token.value)
                selection.reasons.append(f"`{token.text}`: the scenarios of `{token.value}`")
        elif token.name.lower() in RESERVED_NAMES and token.name not in RESERVED_NAMES:
            problem = f"tokens are lower case; write {TOKEN_PREFIX}{token.name.lower()}"
        elif token.value is not None:
            problem = f"takes no '=': only {TOKEN_PREFIX}{UNIT_TOKEN}= and {TOKEN_PREFIX}{SCENARIO_TOKEN}= do"
        elif token.name == ALL_TOKEN:
            selection.run_all = True
            selection.reasons.append(f"`{token.text}`: every unit test and every scenario")
        elif token.name not in groups:
            unknown.append(token.name)
        elif token.name not in selection.groups:
            selection.groups.append(token.name)
            selection.reasons.append(f"`{token.text}`: the integration group `{token.name}`")
        if problem is not None:
            problems.append(f"{echo_safe(token.text)}: {problem}")
    if unknown:
        lower_case_hint = "; tokens are lower case" if any(name != name.lower() for name in unknown) else ""
        problems.append(unknown_groups_text(unknown, groups) + lower_case_hint)
    if problems:
        raise InvalidTokens("\n".join(problems))
    if selection.run_all:
        selection.reasons.append(f"{TOKEN_PREFIX}{ALL_TOKEN} covers every other token, which must still select tests")
    return selection


def output_lines(selection: AsanSelection, run: bool, full_suite_shards: int) -> list[str]:
    """The select job's outputs, one name=value line each; every list is JSON on one line."""
    if not run:
        selection = AsanSelection()
    return [
        f"run={'true' if run else 'false'}",
        f"unit={selection.unit_mode()}",
        f"unit_filters={json.dumps(selection.unit_filters)}",
        f"scenarios={selection.scenario_mode()}",
        f"groups={json.dumps(selection.groups)}",
        f"scenario_ids={json.dumps(selection.scenario_ids)}",
        f"shards={json.dumps(selection.shard_numbers(full_suite_shards))}",
    ]


def selection_summary(selection: AsanSelection, run: bool, why: str, shard_count: int) -> str:
    """The select job's summary: whether the run happens and why, and what each token adds."""
    if not run:
        return f"### ASan run: not started\n\n- {why}\n"
    unit = {MODE_ALL: "every unit test", MODE_FILTERS: "chosen unit tests", MODE_NONE: "no unit tests"}[selection.unit_mode()]
    scenarios = {
        MODE_ALL: f"every scenario, split across {shard_count} shards",
        MODE_SELECTED: "chosen scenarios",
        MODE_NONE: "no scenarios",
    }[selection.scenario_mode()]
    lines = [f"### ASan run: {unit}; {scenarios}", "", f"- Why: {why}"]
    lines.extend(f"- {reason}" for reason in selection.reasons)
    return "\n".join(lines) + "\n"


def quoted_lines(lines: Iterable[str]) -> str:
    """lines as an indented Markdown code block, so text in them renders literally."""
    return "".join(f"    {line}\n" for line in lines)


def problem_summary(heading: str, message: str) -> str:
    """A summary quoting message as an indented code block."""
    return f"### {heading}\n\n" + quoted_lines(message.splitlines())


def append_summary(path: Optional[Path], text: str) -> None:
    """Appends text to the summary file at path, when there is one."""
    if path is None:
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("a", encoding="utf-8", errors=ci_groups.REASON_ENCODING_ERRORS) as summary_file:
        summary_file.write(text)


def registered_groups(repo_root: Path) -> list[str]:
    """The registered groups and subgroups; raises group_files.GroupFileError or OSError."""
    return [group.name for group in group_files.read_groups(repo_root / ci_groups.GROUPS_DIRECTORY)]


def collect_node_ids(python: str, group: str, targets: Optional[Sequence[str]]) -> list[str]:
    """The node ids, relative to tests/integration, that a run of group collects, or targets when given (run_plan.collect_selection).

    No ROTS_IT_SHARD applies. Raises NothingSelected when collection fails or outlives
    COLLECT_TIMEOUT_SECONDS; pytest's output is then on stderr.
    """
    environment = {name: value for name, value in os.environ.items() if name != run_plan.SHARD_VARIABLE}
    try:
        tests, status = run_plan.collect_selection(python, group, environment, targets, COLLECT_TIMEOUT_SECONDS)
    except subprocess.TimeoutExpired:
        raise NothingSelected(f"collecting it took longer than {COLLECT_TIMEOUT_SECONDS} seconds") from None
    if tests is None:
        raise NothingSelected(f"pytest could not collect it (exit status {status}); its output is above")
    return [test.node_id for test in tests]


def scenario_token_tests(
    groups: Sequence[str], scenario_ids: Sequence[str], collect: Callable[[str, Optional[Sequence[str]]], list[str]],
) -> list[tuple[str, list[str]]]:
    """Each group and scenario token, as written, with the node ids it selects, in order.

    collect gives the node ids for a group, or for targets relative to the repository root.
    Raises NothingSelected naming every token that selects no test or cannot be collected.
    """
    selected: list[tuple[str, list[str]]] = []
    problems: list[str] = []
    requests: list[tuple[str, str, Optional[list[str]]]] = [(f"{TOKEN_PREFIX}{name}", name, None) for name in groups]
    requests += [(f"{TOKEN_PREFIX}{SCENARIO_TOKEN}={node_id}", "", [INTEGRATION_RELATIVE + node_id]) for node_id in scenario_ids]
    for token_text, group, targets in requests:
        try:
            node_ids = collect(group, targets)
        except NothingSelected as failure:
            problems.append(f"{token_text}: {failure}")
            continue
        if not node_ids:
            problems.append(f"{token_text}: selects no test")
            continue
        selected.append((token_text, node_ids))
    if problems:
        raise NothingSelected("\n".join(problems))
    return selected


def unique_node_ids(token_tests: Iterable[tuple[str, list[str]]]) -> list[str]:
    """Every node id the tokens select, in order, each once."""
    node_ids: list[str] = []
    for _, token_node_ids in token_tests:
        for node_id in token_node_ids:
            if node_id not in node_ids:
                node_ids.append(node_id)
    return node_ids


def names_markdown(names: Sequence[str]) -> list[str]:
    """Indented list lines for names, the first SUMMARY_NAME_LIMIT of them and a count of the rest."""
    lines = [f"  - `{name}`" for name in names[:SUMMARY_NAME_LIMIT]]
    if len(names) > SUMMARY_NAME_LIMIT:
        lines.append(f"  - and {len(names) - SUMMARY_NAME_LIMIT} more")
    return lines


def token_tests_summary(heading: str, token_tests: Sequence[tuple[str, list[str]]]) -> str:
    """A summary listing each token's tests."""
    lines = [f"### {heading}", ""]
    for token_text, node_ids in token_tests:
        lines.append(f"- `{token_text}`: {len(node_ids)} {'test' if len(node_ids) == 1 else 'tests'}")
        lines.extend(names_markdown(node_ids))
    return "\n".join(lines) + "\n"


def listed_tests(listing: str) -> list[str]:
    """The full test names (Suite.Test) in GoogleTest's --gtest_list_tests output."""
    names: list[str] = []
    suite = ""
    for line in listing.splitlines():
        if not line.strip():
            continue
        if line.startswith(" "):
            if suite:
                names.append(suite + line.split()[0])
        else:
            suite = line.split()[0]
    return names


def unit_filter_tests(
    binary: str, filters: Sequence[str], list_tests: Callable[[list[str]], tuple[int, str]],
) -> list[tuple[str, list[str]]]:
    """Each unit filter with the tests it matches; raises NothingSelected naming every filter that matches none.

    list_tests runs the binary with arguments and returns its exit status and output.
    """
    matched: list[tuple[str, list[str]]] = []
    problems: list[str] = []
    for unit_filter in filters:
        token_text = f"{TOKEN_PREFIX}{UNIT_TOKEN}={unit_filter}"
        status, output = list_tests([binary, "--gtest_list_tests", f"--gtest_filter={unit_filter}"])
        if status != 0:
            problems.append(f"{token_text}: listing the unit tests failed (exit status {status}):\n{output}")
            continue
        names = listed_tests(output)
        if not names:
            problems.append(f"{token_text}: {with_prose_hint('matches no unit test', unit_filter)}")
            continue
        matched.append((token_text, names))
    if problems:
        raise NothingSelected("\n".join(problems))
    return matched


def unit_test_command(binary: str, mode: str, filters: Sequence[str]) -> list[str]:
    """The unit tests binary's command: every test, each named as it runs, or the filters joined into one --gtest_filter."""
    if mode == MODE_ALL:
        return [binary]
    return [binary, f"--gtest_filter={GTEST_FILTER_SEPARATOR.join(filters)}"]


def read_string_list(environ: Mapping[str, str], name: str) -> list[str]:
    """The JSON list of strings in environ[name], empty when unset; raises ValueError naming the variable."""
    text = environ.get(name, "") or "[]"
    try:
        value = json.loads(text)
    except json.JSONDecodeError:
        raise ValueError(f"{name} is not a JSON list: {echo_safe(text)!r}") from None
    if not isinstance(value, list) or not all(isinstance(item, str) for item in value):
        raise ValueError(f"{name} is not a JSON list of strings: {echo_safe(text)!r}")
    return value


def read_mode(environ: Mapping[str, str], name: str, modes: Sequence[str]) -> str:
    """The mode in environ[name], which must be one of modes; raises ValueError naming the variable."""
    mode = environ.get(name)
    if mode not in modes:
        raise ValueError(f"{name} must be {', '.join(modes)}, not {echo_safe(repr(mode))}")
    return mode


def checked_unit_inputs(environ: Mapping[str, str]) -> tuple[str, list[str]]:
    """The unit mode and filters from the environment, checked again; raises ValueError."""
    mode = read_mode(environ, UNIT_MODE_VARIABLE, (MODE_NONE, MODE_ALL, MODE_FILTERS))
    filters = read_string_list(environ, UNIT_FILTERS_VARIABLE)
    if mode == MODE_FILTERS and not filters:
        raise ValueError(f"{UNIT_MODE_VARIABLE}={MODE_FILTERS} needs filters in {UNIT_FILTERS_VARIABLE}")
    problems = [f"{TOKEN_PREFIX}{UNIT_TOKEN}={echo_safe(value)}: {problem}" for value in filters if (problem := gtest_filter_problem(value))]
    if problems:
        raise ValueError("\n".join(problems))
    return mode, filters


def checked_scenario_inputs(environ: Mapping[str, str], repo_root: Path) -> tuple[str, list[str], list[str]]:
    """The scenario mode, groups and scenario ids from the environment, checked again; raises ValueError."""
    mode = read_mode(environ, SCENARIO_MODE_VARIABLE, (MODE_NONE, MODE_ALL, MODE_SELECTED))
    groups = read_string_list(environ, GROUPS_VARIABLE)
    scenario_ids = read_string_list(environ, SCENARIO_IDS_VARIABLE)
    registered = registered_groups(repo_root) if groups else []
    unknown = [name for name in groups if name not in registered]
    problems = [unknown_groups_text(unknown, registered)] if unknown else []
    problems += [
        f"{TOKEN_PREFIX}{SCENARIO_TOKEN}={echo_safe(value)}: {problem}"
        for value in scenario_ids if (problem := scenario_id_problem(value, repo_root))
    ]
    if problems:
        raise ValueError("\n".join(problems))
    if mode == MODE_SELECTED and not groups and not scenario_ids:
        raise ValueError(f"{SCENARIO_MODE_VARIABLE}={MODE_SELECTED} needs groups or scenario ids")
    return mode, groups, scenario_ids


def sanitizer_report_lines(runs_root: Path, since: float) -> list[str]:
    """The SANITIZER_ERROR_TEXT lines of every game.log under runs_root changed at or after since, each after its test's id.

    Unreadable files are skipped; the lines are made echo-safe.
    """
    lines: list[str] = []
    for log_path in sorted(runs_root.glob(f"*/{GAME_LOG_NAME}")):
        try:
            if log_path.stat().st_mtime < since:
                continue
            log_text = log_path.read_bytes().decode("latin-1")
            test_id_path = log_path.parent / TEST_NAME_FILE
            test_id = test_id_path.read_text(encoding="utf-8", errors="replace").strip() if test_id_path.is_file() else log_path.parent.name
        except OSError:
            continue
        for line in log_text.splitlines():
            if SANITIZER_ERROR_TEXT in line:
                lines.append(echo_safe(f"{test_id}: {line.strip()}"))
    return lines


def scenario_outcome_summary(status: int, shard_text: str, report_lines: Sequence[str]) -> str:
    """The run-scenarios outcome: pass or fail with pytest's status, the artifact holding the kept run directories, and the reports."""
    if status == 0:
        return "\nThe scenarios passed.\n"
    shard_number = shard_text.split("/", 1)[0] if shard_text else "1"
    lines = [
        "",
        f"The scenarios failed with pytest exit status {status}. Their kept run directories are in the "
        f"`{RUNS_ARTIFACT_FORMAT.format(number=shard_number)}` artifact.",
    ]
    if report_lines:
        lines += ["", "AddressSanitizer reports in their game.log files:", ""]
        text = quoted_lines(report_lines[:SUMMARY_REPORT_LINE_LIMIT]).rstrip("\n")
        lines.append(text)
        if len(report_lines) > SUMMARY_REPORT_LINE_LIMIT:
            lines.append(f"\nand {len(report_lines) - SUMMARY_REPORT_LINE_LIMIT} more report lines")
    else:
        lines.append("No game.log holds an AddressSanitizer report.")
    return "\n".join(lines) + "\n"


def run_captured(command: list[str], working_dir: Path) -> tuple[int, str]:
    """Runs command in working_dir and returns its exit status and combined output."""
    completed = subprocess.run(
        command, cwd=working_dir, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=False,
    )
    return completed.returncode, completed.stdout.decode("latin-1", errors="replace")


def command_select(arguments: argparse.Namespace, description: Optional[str]) -> int:
    """The select command: writes the outputs and the summary, or explains which tokens cannot be used.

    description replaces the event file's in tests.
    """
    if description is None:
        if arguments.event is None:
            print(f"error: no event file: set {EVENT_PATH_VARIABLE} or pass --event", file=sys.stderr)
            return EXIT_USAGE
        try:
            description = read_description(arguments.event)
        except (OSError, ValueError) as error:
            print(f"error: cannot read the pull request from the event file: {error}", file=sys.stderr)
            return EXIT_USAGE
    try:
        groups = registered_groups(arguments.repo_root)
    except (group_files.GroupFileError, OSError) as error:
        print(f"error: cannot read the scenario groups: {error}", file=sys.stderr)
        return EXIT_USAGE
    try:
        selection = select_tests(description, groups, arguments.repo_root)
    except InvalidTokens as invalid:
        print(f"error: the @asan-test tokens cannot be used:\n{invalid}", file=sys.stderr)
        append_summary(arguments.summary, problem_summary("ASan run: the @asan-test tokens cannot be used", str(invalid)))
        return EXIT_USAGE
    run, why = run_decision(arguments.action, description_tokens(description))
    with arguments.output.open("a", encoding="utf-8") as output:
        output.write("".join(f"{line}\n" for line in output_lines(selection, run, arguments.full_suite_shards)))
    summary = selection_summary(selection, run, why, arguments.full_suite_shards)
    append_summary(arguments.summary, summary)
    ci_groups.escape_unencodable_output(sys.stdout)
    print(summary, end="")
    return 0


def command_check_scenarios(
    arguments: argparse.Namespace, environ: Mapping[str, str], collect: Callable[[str, Optional[Sequence[str]]], list[str]],
) -> int:
    """The check-scenarios command: fails unless every group and scenario token selects a test, under @asan-test-all too."""
    try:
        _, groups, scenario_ids = checked_scenario_inputs(environ, arguments.repo_root)
        token_tests = scenario_token_tests(groups, scenario_ids, collect)
    except (ValueError, OSError, group_files.GroupFileError) as error:
        print(f"error: {error}", file=sys.stderr)
        return EXIT_USAGE
    except NothingSelected as nothing:
        print(f"error: a scenario token selects no test:\n{nothing}", file=sys.stderr)
        append_summary(arguments.summary, problem_summary("ASan scenarios: a token selects no test", str(nothing)))
        return EXIT_NOTHING_SELECTED
    summary = token_tests_summary("ASan scenarios each token selects", token_tests)
    append_summary(arguments.summary, summary)
    print(summary, end="")
    return 0


def command_run_unit(
    arguments: argparse.Namespace, environ: Mapping[str, str],
    list_tests: Callable[[list[str]], tuple[int, str]], run_command: Callable[[list[str], bool], int],
) -> int:
    """The run-unit command: checks every filter matches a test, then runs the unit tests once and returns their status."""
    try:
        mode, filters = checked_unit_inputs(environ)
    except ValueError as error:
        print(f"error: {error}", file=sys.stderr)
        return EXIT_USAGE
    if mode == MODE_NONE:
        print("No unit tests were asked for.")
        return 0
    if filters:
        try:
            matched = unit_filter_tests(arguments.binary, filters, list_tests)
        except NothingSelected as nothing:
            print(f"error: a unit token selects no test:\n{nothing}", file=sys.stderr)
            append_summary(arguments.summary, problem_summary("ASan unit tests: a token selects no test", str(nothing)))
            return EXIT_NOTHING_SELECTED
        append_summary(arguments.summary, token_tests_summary("ASan unit tests each token selects", matched))
    if mode == MODE_ALL:
        append_summary(arguments.summary, "### ASan unit tests: every unit test\n")
    command = unit_test_command(arguments.binary, mode, filters)
    print("Running: " + " ".join(command), flush=True)
    status = run_command(command, False)
    append_summary(arguments.summary, f"\nThe unit tests {'passed' if status == 0 else f'failed with exit status {status}'}.\n")
    return status


def command_run_scenarios(
    arguments: argparse.Namespace, environ: Mapping[str, str],
    collect: Callable[[str, Optional[Sequence[str]]], list[str]], run_command: Callable[[list[str], bool], int],
) -> int:
    """The run-scenarios command: runs every scenario in this shard's share, or the chosen ones once checked; returns pytest's status."""
    try:
        mode, groups, scenario_ids = checked_scenario_inputs(environ, arguments.repo_root)
    except (ValueError, OSError, group_files.GroupFileError) as error:
        print(f"error: {error}", file=sys.stderr)
        return EXIT_USAGE
    if mode == MODE_NONE:
        print("No scenarios were asked for.")
        return 0
    shard_text = environ.get(run_plan.SHARD_VARIABLE, "")
    if mode == MODE_ALL:
        # The select job has checked every group and scenario token beside @asan-test-all.
        append_summary(arguments.summary, f"### ASan scenarios: every scenario{f', shard {shard_text}' if shard_text else ''}\n")
        command = run_plan.pytest_run_command(arguments.python, run_plan.run_expression("", []), arguments.jobs)
    else:
        try:
            token_tests = scenario_token_tests(groups, scenario_ids, collect)
        except NothingSelected as nothing:
            print(f"error: a scenario token selects no test:\n{nothing}", file=sys.stderr)
            append_summary(arguments.summary, problem_summary("ASan scenarios: a token selects no test", str(nothing)))
            return EXIT_NOTHING_SELECTED
        append_summary(arguments.summary, token_tests_summary("ASan scenarios run", token_tests))
        targets = [INTEGRATION_RELATIVE + node_id for node_id in unique_node_ids(token_tests)]
        command = run_plan.pytest_run_command(arguments.python, None, arguments.jobs, targets)
    started = time.time()
    status = run_command(command, True)
    report_lines = sanitizer_report_lines(arguments.repo_root / RUNS_RELATIVE, started) if status != 0 else []
    append_summary(arguments.summary, scenario_outcome_summary(status, shard_text, report_lines))
    return status


def parse_arguments(argv: Optional[list[str]], environ: Mapping[str, str]) -> argparse.Namespace:
    """The command line; argparse exits with status 2 on a bad one."""
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--repo-root", type=Path, default=DEFAULT_REPO_ROOT, help="the repository root")
    parser.add_argument("--summary", type=Path, help="the file the markdown summary is appended to")
    commands = parser.add_subparsers(dest="command", required=True)
    select = commands.add_parser("select", help="choose the tests from the pull request's description")
    select.add_argument("--action", required=True, help="the pull_request event's action, for the summary")
    select.add_argument("--output", type=Path, required=True, help="the file the outputs are appended to, as GITHUB_OUTPUT")
    select.add_argument("--full-suite-shards", type=int, default=1, help="how many shards a run of every scenario uses")
    event_default = environ.get(EVENT_PATH_VARIABLE)
    select.add_argument(
        "--event", type=Path, default=Path(event_default) if event_default else None,
        help=f"the webhook event file holding the pull request (default: {EVENT_PATH_VARIABLE})",
    )
    check = commands.add_parser("check-scenarios", help="fail unless each group and scenario token selects a test")
    check.add_argument("--python", required=True, help="the Python whose pytest collects the scenarios")
    unit = commands.add_parser("run-unit", help="run the unit tests the select job chose")
    unit.add_argument("--binary", required=True, help="the unit tests binary, such as bin/tests")
    scenarios = commands.add_parser("run-scenarios", help="run the scenarios the select job chose")
    scenarios.add_argument("--python", required=True, help="the Python whose pytest runs the scenarios")
    scenarios.add_argument("--jobs", default="1", help="pytest-xdist workers, as make integration's JOBS")
    arguments = parser.parse_args(argv)
    if arguments.command == "select" and arguments.full_suite_shards < 1:
        parser.error("--full-suite-shards must be at least 1")
    return arguments


def main(
    argv: Optional[list[str]] = None,
    *,
    environ: Optional[Mapping[str, str]] = None,
    description: Optional[str] = None,
    list_tests: Optional[Callable[[list[str]], tuple[int, str]]] = None,
    collect: Optional[Callable[[str, Optional[Sequence[str]]], list[str]]] = None,
    run_command: Callable[[list[str], bool], int] = run_plan.run_in_foreground,
) -> int:
    """Runs one command and returns its exit status: 0, EXIT_USAGE, EXIT_NOTHING_SELECTED, or the tests' own status.

    The keyword arguments replace the environment, the event file's description and the
    subprocesses in tests: list_tests runs the unit tests binary to list tests, collect collects
    a group or targets (collect_node_ids), and run_command runs the tests.
    """
    environment = os.environ if environ is None else environ
    arguments = parse_arguments(argv, environment)
    if arguments.command == "select":
        return command_select(arguments, description)
    if arguments.command == "run-unit":
        unit_list_tests = list_tests or (lambda command: run_captured(command, arguments.repo_root))
        return command_run_unit(arguments, environment, unit_list_tests, run_command)
    scenario_collect = collect or (lambda group, targets: collect_node_ids(arguments.python, group, targets))
    if arguments.command == "check-scenarios":
        return command_check_scenarios(arguments, environment, scenario_collect)
    return command_run_scenarios(arguments, environment, scenario_collect, run_command)


if __name__ == "__main__":
    sys.exit(main())
