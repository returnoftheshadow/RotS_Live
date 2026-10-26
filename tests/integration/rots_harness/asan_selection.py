"""Chooses and runs the tests of the on-request AddressSanitizer CI run (.github/workflows/asan.yml).

Nothing runs unless the pull request's description holds an `@asan-test-` token. HTML comments
are dropped first, as ci_groups drops them:

- `@asan-test-all`: every unit test and every scenario, the scenarios split across shards. It
  wins over every other token.
- `@asan-test-<group>`: the scenarios of one registered group or subgroup; `all`, `unit` and
  `scenario` never name a group.
- `@asan-test-unit=<filter>`: the unit tests a GoogleTest filter matches. Repeatable; the filters
  are joined with ':' into one --gtest_filter, after each has been checked to match a test.
- `@asan-test-scenario=<id>`: a scenario file or test, as a pytest node id relative to
  tests/integration, such as `scenarios/test_bank.py::test_name`. Repeatable.

Unit tests run only for `all` or a unit token, and scenarios only for `all`, a group or a scenario
token. A value runs to the next whitespace and must match a strict pattern; a token that does not,
or names an unknown group or a missing scenario file, stops the run with the token and the reason.
A description edit reruns only when it changes the set of tokens.

Run with tests/integration on PYTHONPATH as `python3 -m rots_harness.asan_selection <command>`:
`select` reads the description from PR_BODY, PR_PREVIOUS_BODY and PR_BODY_CHANGED, so it is never
part of a shell command; `check-scenarios` fails when a group or scenario token selects no test;
`run-unit` and `run-scenarios` run the selection inside the development image.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
import tempfile
from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable, Iterable, Mapping, Optional, Sequence

from rots_harness import ci_groups, collect_report, run_plan
from rots_harness import groups as group_files

# The token that runs everything; the same word ci_groups uses.
ALL_TOKEN = ci_groups.ALL_GROUPS_TOKEN
# The token name taking a GoogleTest filter after '='.
UNIT_TOKEN = "unit"
# The token name taking a scenario node id after '='.
SCENARIO_TOKEN = "scenario"
# What starts every token, for messages.
TOKEN_PREFIX = "@asan-test-"
# A token: the name stops at the first character a marker name cannot hold, as in ci_groups. The
# value after '=' runs to the next whitespace, so a stray character is refused, never dropped.
DESCRIPTION_TOKEN = re.compile(r"(?<![\w-])@asan-test-([A-Za-z0-9_]+)(?:=(\S*))?")
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
# tests/integration relative to the repository root; scenario ids are relative to it.
INTEGRATION_RELATIVE = "tests/integration/"
# This module's checkout: rots_harness is in tests/integration.
DEFAULT_REPO_ROOT = Path(__file__).resolve().parents[3]
# The environment variable holding the description now, and the one holding it before an edit.
BODY_VARIABLE = "PR_BODY"
PREVIOUS_BODY_VARIABLE = "PR_PREVIOUS_BODY"
# "true" when the event is an edit that changed the description, so PREVIOUS_BODY_VARIABLE holds it.
BODY_CHANGED_VARIABLE = "PR_BODY_CHANGED"
# The pull request event action that is a description, title or base edit.
EDITED_ACTION = "edited"
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
# Tokens or inputs that cannot be used, or a group file that cannot be read.
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

    # The token as written, such as "@asan-test-unit=Foo.*"; two tokens are the same when this is.
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
    # The GoogleTest filters of the unit tokens, in order.
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


def description_tokens(body: str) -> list[Token]:
    """The description's tokens outside HTML comments, in order and without repeats."""
    tokens: list[Token] = []
    for match in DESCRIPTION_TOKEN.finditer(ci_groups.HTML_COMMENT.sub("", body)):
        token = Token(match.group(0), match.group(1), match.group(2))
        if token not in tokens:
            tokens.append(token)
    return tokens


def token_texts(body: str) -> frozenset[str]:
    """The set of tokens the description holds, as written; their order and repeats do not count."""
    return frozenset(token.text for token in description_tokens(body))


def restart_decision(action: str, body: str, body_changed: bool, previous_body: str) -> tuple[bool, str]:
    """Whether this pull request event runs the selected tests, and why.

    An edit runs them only when it changed the description's set of tokens; previous_body is the
    description before the edit, read only when body_changed. Any other action runs them while
    the description holds a token.
    """
    current = token_texts(body)
    if not current:
        return False, "the description has no @asan-test token"
    if action != EDITED_ACTION:
        return True, f"the pull request event is {action}, and the description has @asan-test tokens"
    if not body_changed:
        return False, "the edit did not change the description"
    if token_texts(previous_body) == current:
        return False, "the edit left the @asan-test tokens as they were, so the earlier run stands"
    return True, "the edit changed the @asan-test tokens"


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
            return f"may hold only {GTEST_FILTER_CHARACTERS_TEXT}, not {character!r}"
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
        return (
            f"must be {SCENARIO_ID_FOLDER}test_<name>.py, optionally followed by ::<test name> and a [<parameter id>] "
            "of letters, digits and _ - . / % ~"
        )
    scenario_file = repo_root / INTEGRATION_RELATIVE / SCENARIO_ID_FOLDER / match.group(1)
    if not scenario_file.is_file():
        return f"names {INTEGRATION_RELATIVE}{SCENARIO_ID_FOLDER}{match.group(1)}, which does not exist"
    return None


def unknown_groups_text(unknown: Sequence[str], groups: Sequence[str]) -> str:
    """The message for group tokens no group file registers, listing the registered groups as ci_groups does."""
    return (
        f"the description names unknown integration groups: {', '.join(unknown)}; "
        f"registered groups: {', '.join(groups)} (or {ALL_TOKEN}, {UNIT_TOKEN}=<filter>, {SCENARIO_TOKEN}=<id>)"
    )


def select_tests(tokens: Sequence[Token], groups: Sequence[str], repo_root: Path) -> AsanSelection:
    """The tests the tokens ask for; groups lists the registered groups and subgroups.

    Every token is checked, even beside @asan-test-all. Raises InvalidTokens listing each token
    that cannot be used.
    """
    selection = AsanSelection()
    problems: list[str] = []
    unknown: list[str] = []
    for token in tokens:
        problem: Optional[str] = None
        if token.name == UNIT_TOKEN:
            problem = gtest_filter_problem(token.value or "") if token.value is not None else "needs =<GoogleTest filter>"
            if problem is None and token.value not in selection.unit_filters:
                selection.unit_filters.append(token.value)
                selection.reasons.append(f"`{token.text}`: the unit tests matching `{token.value}`")
        elif token.name == SCENARIO_TOKEN:
            problem = scenario_id_problem(token.value or "", repo_root) if token.value is not None else "needs =<scenario id>"
            if problem is None and token.value not in selection.scenario_ids:
                selection.scenario_ids.append(token.value)
                selection.reasons.append(f"`{token.text}`: the scenarios of `{token.value}`")
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
            problems.append(f"{token.text}: {problem}")
    if unknown:
        problems.append(unknown_groups_text(unknown, groups))
    if problems:
        raise InvalidTokens("\n".join(problems))
    if selection.run_all:
        selection.reasons.append(f"{TOKEN_PREFIX}{ALL_TOKEN} covers every other token")
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


def problem_summary(heading: str, message: str) -> str:
    """A summary quoting message as an indented code block, so description text in it renders literally."""
    return f"### {heading}\n\n" + "".join(f"    {line}\n" for line in message.splitlines())


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


def collect_node_ids(python: str, arguments: Sequence[str], repo_root: Path) -> list[str]:
    """The node ids, relative to tests/integration, that pytest collects for arguments run from repo_root.

    No ROTS_IT_SHARD applies. Raises NothingSelected with pytest's output when collection fails.
    """
    with tempfile.TemporaryDirectory(prefix="rots-asan-collect-") as folder:
        report_path = Path(folder) / "collected.json"
        environment = {name: value for name, value in os.environ.items() if name != run_plan.SHARD_VARIABLE}
        environment[collect_report.REPORT_PATH_VARIABLE] = str(report_path)
        # The plugin is imported before pytest.ini's pythonpath applies.
        integration_root = str(repo_root / INTEGRATION_RELATIVE)
        environment["PYTHONPATH"] = os.pathsep.join(filter(None, [integration_root, os.environ.get("PYTHONPATH")]))
        command = [python, "-m", "pytest", "-q", "--collect-only", "-p", collect_report.__name__, *arguments]
        completed = subprocess.run(
            command, cwd=repo_root, env=environment, stdin=subprocess.DEVNULL, capture_output=True, text=True,
            check=False, timeout=COLLECT_TIMEOUT_SECONDS,
        )
        # 5: nothing collected, which the caller reports for the token.
        if completed.returncode in (0, 5) and report_path.is_file():
            return [test.node_id for test in collect_report.read_report(report_path)]
    raise NothingSelected(f"pytest could not collect it (exit status {completed.returncode}):\n{completed.stdout}{completed.stderr}")


def scenario_token_tests(
    groups: Sequence[str], scenario_ids: Sequence[str], collect: Callable[[list[str]], list[str]],
) -> list[tuple[str, list[str]]]:
    """Each group and scenario token, as written, with the node ids it selects, in order.

    collect gives the node ids for pytest arguments relative to the repository root. Raises
    NothingSelected naming every token that selects no test or cannot be collected.
    """
    selected: list[tuple[str, list[str]]] = []
    problems: list[str] = []
    requests = [(f"{TOKEN_PREFIX}{name}", [run_plan.SCENARIOS_RELATIVE, "-m", run_plan.run_expression(name, [])]) for name in groups]
    requests += [(f"{TOKEN_PREFIX}{SCENARIO_TOKEN}={node_id}", [INTEGRATION_RELATIVE + node_id]) for node_id in scenario_ids]
    for token_text, arguments in requests:
        try:
            node_ids = collect(arguments)
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
            problems.append(f"{token_text}: matches no unit test")
            continue
        matched.append((token_text, names))
    if problems:
        raise NothingSelected("\n".join(problems))
    return matched


def unit_test_command(binary: str, mode: str, filters: Sequence[str]) -> list[str]:
    """The unit tests binary's command: every test, briefly reported, or the filters joined into one --gtest_filter."""
    if mode == MODE_ALL:
        return [binary, "--gtest_brief=1"]
    return [binary, f"--gtest_filter={GTEST_FILTER_SEPARATOR.join(filters)}"]


def scenario_run_command(python: str, node_ids: Sequence[str], jobs_text: str) -> list[str]:
    """The pytest command running node_ids from the repository root, with make integration's report arguments."""
    command = [python, "-m", "pytest", *(INTEGRATION_RELATIVE + node_id for node_id in node_ids), "-q"]
    if jobs_text.strip() not in run_plan.SINGLE_WORKER_JOBS:
        command += ["-n", jobs_text.strip()]
    return command + list(run_plan.REPORT_ARGUMENTS)


def read_string_list(environ: Mapping[str, str], name: str) -> list[str]:
    """The JSON list of strings in environ[name], empty when unset; raises ValueError naming the variable."""
    text = environ.get(name, "") or "[]"
    try:
        value = json.loads(text)
    except json.JSONDecodeError:
        raise ValueError(f"{name} is not a JSON list: {text!r}") from None
    if not isinstance(value, list) or not all(isinstance(item, str) for item in value):
        raise ValueError(f"{name} is not a JSON list of strings: {text!r}")
    return value


def checked_unit_inputs(environ: Mapping[str, str]) -> tuple[str, list[str]]:
    """The unit mode and filters from the environment, checked again; raises ValueError."""
    mode = environ.get(UNIT_MODE_VARIABLE, "")
    if mode not in (MODE_NONE, MODE_ALL, MODE_FILTERS):
        raise ValueError(f"{UNIT_MODE_VARIABLE} must be {MODE_NONE}, {MODE_ALL} or {MODE_FILTERS}, not {mode!r}")
    filters = read_string_list(environ, UNIT_FILTERS_VARIABLE)
    if mode == MODE_FILTERS and not filters:
        raise ValueError(f"{UNIT_MODE_VARIABLE}={MODE_FILTERS} needs filters in {UNIT_FILTERS_VARIABLE}")
    problems = [f"{TOKEN_PREFIX}{UNIT_TOKEN}={value}: {problem}" for value in filters if (problem := gtest_filter_problem(value))]
    if problems:
        raise ValueError("\n".join(problems))
    return mode, filters


def checked_scenario_inputs(environ: Mapping[str, str], repo_root: Path) -> tuple[str, list[str], list[str]]:
    """The scenario mode, groups and scenario ids from the environment, checked again; raises ValueError."""
    mode = environ.get(SCENARIO_MODE_VARIABLE, MODE_SELECTED)
    if mode not in (MODE_NONE, MODE_ALL, MODE_SELECTED):
        raise ValueError(f"{SCENARIO_MODE_VARIABLE} must be {MODE_NONE}, {MODE_ALL} or {MODE_SELECTED}, not {mode!r}")
    groups = read_string_list(environ, GROUPS_VARIABLE)
    scenario_ids = read_string_list(environ, SCENARIO_IDS_VARIABLE)
    registered = registered_groups(repo_root) if groups else []
    unknown = [name for name in groups if name not in registered]
    problems = [unknown_groups_text(unknown, registered)] if unknown else []
    problems += [f"{TOKEN_PREFIX}{SCENARIO_TOKEN}={value}: {problem}" for value in scenario_ids if (problem := scenario_id_problem(value, repo_root))]
    if problems:
        raise ValueError("\n".join(problems))
    if mode == MODE_SELECTED and not groups and not scenario_ids:
        raise ValueError(f"{SCENARIO_MODE_VARIABLE}={MODE_SELECTED} needs groups or scenario ids")
    return mode, groups, scenario_ids


def run_captured(command: list[str], working_dir: Path) -> tuple[int, str]:
    """Runs command in working_dir and returns its exit status and combined output."""
    completed = subprocess.run(
        command, cwd=working_dir, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=False,
    )
    return completed.returncode, completed.stdout.decode("latin-1", errors="replace")


def command_select(arguments: argparse.Namespace, environ: Mapping[str, str]) -> int:
    """The select command: writes the outputs and the summary, or explains which tokens cannot be used."""
    body = environ.get(BODY_VARIABLE, "")
    try:
        groups = registered_groups(arguments.repo_root)
    except (group_files.GroupFileError, OSError) as error:
        print(f"error: cannot read the scenario groups: {error}", file=sys.stderr)
        return EXIT_USAGE
    try:
        selection = select_tests(description_tokens(body), groups, arguments.repo_root)
    except InvalidTokens as invalid:
        print(f"error: the @asan-test tokens cannot be used:\n{invalid}", file=sys.stderr)
        append_summary(arguments.summary, problem_summary("ASan run: the @asan-test tokens cannot be used", str(invalid)))
        return EXIT_USAGE
    run, why = restart_decision(arguments.action, body, environ.get(BODY_CHANGED_VARIABLE) == "true", environ.get(PREVIOUS_BODY_VARIABLE, ""))
    with arguments.output.open("a", encoding="utf-8") as output:
        output.write("".join(f"{line}\n" for line in output_lines(selection, run, arguments.full_suite_shards)))
    summary = selection_summary(selection, run, why, arguments.full_suite_shards)
    append_summary(arguments.summary, summary)
    ci_groups.escape_unencodable_output(sys.stdout)
    print(summary, end="")
    return 0


def command_check_scenarios(arguments: argparse.Namespace, environ: Mapping[str, str], collect: Callable[[list[str]], list[str]]) -> int:
    """The check-scenarios command: fails unless every group and scenario token selects a test."""
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
    if mode == MODE_FILTERS:
        try:
            matched = unit_filter_tests(arguments.binary, filters, list_tests)
        except NothingSelected as nothing:
            print(f"error: a unit token selects no test:\n{nothing}", file=sys.stderr)
            append_summary(arguments.summary, problem_summary("ASan unit tests: a token selects no test", str(nothing)))
            return EXIT_NOTHING_SELECTED
        append_summary(arguments.summary, token_tests_summary("ASan unit tests each token selects", matched))
    else:
        append_summary(arguments.summary, "### ASan unit tests: every unit test\n")
    command = unit_test_command(arguments.binary, mode, filters)
    print("Running: " + " ".join(command), flush=True)
    status = run_command(command, False)
    append_summary(arguments.summary, f"\nThe unit tests {'passed' if status == 0 else f'failed with exit status {status}'}.\n")
    return status


def command_run_scenarios(
    arguments: argparse.Namespace, environ: Mapping[str, str],
    collect: Callable[[list[str]], list[str]], run_command: Callable[[list[str], bool], int],
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
    if mode == MODE_ALL:
        shard = environ.get(run_plan.SHARD_VARIABLE, "")
        append_summary(arguments.summary, f"### ASan scenarios: every scenario{f', shard {shard}' if shard else ''}\n")
        return run_plan.run_ci(arguments.python, "", "", arguments.jobs, run_command)
    try:
        token_tests = scenario_token_tests(groups, scenario_ids, collect)
    except NothingSelected as nothing:
        print(f"error: a scenario token selects no test:\n{nothing}", file=sys.stderr)
        append_summary(arguments.summary, problem_summary("ASan scenarios: a token selects no test", str(nothing)))
        return EXIT_NOTHING_SELECTED
    append_summary(arguments.summary, token_tests_summary("ASan scenarios run", token_tests))
    return run_command(scenario_run_command(arguments.python, unique_node_ids(token_tests), arguments.jobs), True)


def parse_arguments(argv: Optional[list[str]]) -> argparse.Namespace:
    """The command line; argparse exits with status 2 on a bad one."""
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--repo-root", type=Path, default=DEFAULT_REPO_ROOT, help="the repository root")
    parser.add_argument("--summary", type=Path, help="the file the markdown summary is appended to")
    commands = parser.add_subparsers(dest="command", required=True)
    select = commands.add_parser("select", help="choose the tests from the description in PR_BODY")
    select.add_argument("--action", required=True, help="the pull_request event's action: opened, synchronize, reopened or edited")
    select.add_argument("--output", type=Path, required=True, help="the file the outputs are appended to, as GITHUB_OUTPUT")
    select.add_argument("--full-suite-shards", type=int, default=1, help="how many shards a run of every scenario uses")
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
    list_tests: Optional[Callable[[list[str]], tuple[int, str]]] = None,
    collect: Optional[Callable[[list[str]], list[str]]] = None,
    run_command: Callable[[list[str], bool], int] = run_plan.run_in_foreground,
) -> int:
    """Runs one command and returns its exit status: 0, EXIT_USAGE, EXIT_NOTHING_SELECTED, or the tests' own status.

    The keyword arguments replace the environment and the subprocesses in tests: list_tests runs
    the unit tests binary to list tests, collect collects scenarios for pytest arguments, and
    run_command runs the tests.
    """
    arguments = parse_arguments(argv)
    environment = os.environ if environ is None else environ
    if arguments.command == "select":
        return command_select(arguments, environment)
    if arguments.command == "run-unit":
        unit_list_tests = list_tests or (lambda command: run_captured(command, arguments.repo_root))
        return command_run_unit(arguments, environment, unit_list_tests, run_command)
    scenario_collect = collect or (lambda pytest_arguments: collect_node_ids(arguments.python, pytest_arguments, arguments.repo_root))
    if arguments.command == "check-scenarios":
        return command_check_scenarios(arguments, environment, scenario_collect)
    return command_run_scenarios(arguments, environment, scenario_collect, run_command)


if __name__ == "__main__":
    sys.exit(main())
