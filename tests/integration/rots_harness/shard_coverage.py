"""Checks that a sharded CI run ran every selected scenario exactly once, and reports the selection's missing shard times.

`selection` collects the scenarios a run of GROUP selects, without any shard, as make integration
selects them under CI=true. It writes their node ids as a JSON list, and says in the summary how
many have no time in shard_durations.json (they count durations.DEFAULT_TEST_SECONDS in the split)
and how many of the file's entries name no selected test.

`check` reads that list and every shard's downloaded results.xml (one folder per shard, as
`integration-results-shard-<k>`). It fails when a shard's results are missing, or when a
selected test ran no times or more than once, or a test ran that was not selected.

Run from tests/integration as `python3 -m rots_harness.shard_coverage selection|check ...`.
"""

from __future__ import annotations

import argparse
import json
import os
import sys
import xml.etree.ElementTree as ElementTree
from collections import Counter
from pathlib import Path
from typing import Mapping, Optional, Sequence

from rots_harness import durations, run_plan, sharding, shard_durations

# tests/integration: the scenario files the results' class names are matched against.
INTEGRATION_ROOT = Path(__file__).resolve().parents[1]
# The folder each shard's results artifact is downloaded into, by shard number.
SHARD_FOLDER_FORMAT = "integration-results-shard-{number}"
# How many node ids a summary lists for each kind of problem before saying how many more there are.
LISTED_IDS = 20
# check's status when the run did not run every selected test exactly once.
EXIT_COVERAGE_FAILED = 1
# The status for arguments or files the tool cannot use.
EXIT_USAGE = 2


def coverage_problems(selected: Sequence[str], shard_runs: Mapping[int, Optional[Sequence[str]]], shard_count: int) -> list[str]:
    """One line per problem in a sharded run, or none when every selected test ran exactly once.

    shard_runs gives each shard number's node ids as its results list them, or None when its
    results are missing; a shard from 1 to shard_count that it leaves out counts as missing.
    """
    problems = []
    runs: Counter[str] = Counter()
    for number in range(1, shard_count + 1):
        node_ids = shard_runs.get(number)
        if node_ids is None:
            problems.append(f"shard {number}/{shard_count}: no results")
            continue
        runs.update(node_ids)
    selected_set = set(selected)
    never = [node_id for node_id in selected if runs[node_id] == 0]
    repeated = sorted(node_id for node_id, count in runs.items() if count > 1)
    unselected = sorted(node_id for node_id in runs if node_id not in selected_set)
    problems.extend(_listed("never ran", never))
    problems.extend(_listed("ran more than once", [f"{node_id} ({runs[node_id]} times)" for node_id in repeated]))
    problems.extend(_listed("ran but was not selected", unselected))
    return problems


def _listed(what: str, node_ids: Sequence[str]) -> list[str]:
    """One line per node id up to LISTED_IDS, each saying what happened, then how many more there are."""
    lines = [f"{what}: {node_id}" for node_id in node_ids[:LISTED_IDS]]
    if len(node_ids) > LISTED_IDS:
        lines.append(f"{what}: and {len(node_ids) - LISTED_IDS} more")
    return lines


def results_node_ids(results_path: Path, integration_root: Path) -> list[str]:
    """The node id of every testcase in a results file, whatever its outcome; raises OSError, ElementTree.ParseError or shard_durations.UnusableResults.

    A testcase whose class name names no scenario file is listed by its class and test name, so it still counts as a test that ran.
    """
    node_ids = []
    for case in shard_durations.read_results(results_path).iter("testcase"):
        class_name = case.get("classname", "")
        test_name = case.get("name", "")
        node_ids.append(shard_durations.node_id(class_name, test_name, integration_root) or f"{class_name}::{test_name}")
    return node_ids


def timing_report(selected: Sequence[str], recorded: Mapping[str, float]) -> list[str]:
    """Summary lines on how the committed shard times cover the selection: the selected tests without one, and the entries naming no selected test."""
    untimed = [node_id for node_id in selected if node_id not in recorded]
    selected_set = set(selected)
    stale = sorted(node_id for node_id in recorded if node_id not in selected_set)
    lines = [
        f"{len(selected)} selected tests; {len(untimed)} have no time in {sharding.SHARD_DURATIONS_RELATIVE} "
        f"and count {durations.DEFAULT_TEST_SECONDS:g} s in the shard split; {len(stale)} of its entries name no selected test.",
    ]
    if untimed or stale:
        lines.append("Refresh it with make integration-shard-durations (integration README, Continuous integration).")
    lines.extend(f"- no time: `{node_id}`" for node_id in untimed[:LISTED_IDS])
    lines.extend(f"- no such test: `{node_id}`" for node_id in stale[:LISTED_IDS])
    return lines


def _append(path: Optional[Path], lines: Sequence[str]) -> None:
    """Prints the lines and appends them to the summary file when one is given."""
    text = "\n".join(lines) + "\n"
    print(text, end="")
    if path is not None:
        with path.open("a", encoding="utf-8", errors="backslashreplace") as summary:
            summary.write(text)


def run_selection(arguments: argparse.Namespace) -> int:
    """Writes the selection's node ids to --output and the timing report to the summary; returns pytest's status when collection fails."""
    environment = {name: value for name, value in os.environ.items() if name != run_plan.SHARD_VARIABLE}
    tests, status = run_plan.collect_selection(arguments.python, arguments.group, environment)
    if tests is None:
        return status
    selected = [test.node_id for test in tests]
    arguments.output.write_text(json.dumps(selected, indent=1) + "\n", encoding="utf-8")
    recorded = durations.read_durations(INTEGRATION_ROOT / sharding.SHARD_DURATIONS_RELATIVE)
    _append(arguments.summary, ["### Shard times", "", *timing_report(selected, recorded)])
    return 0


def run_check(arguments: argparse.Namespace) -> int:
    """Checks every shard's results against the selection; returns EXIT_COVERAGE_FAILED with the problems listed."""
    try:
        selected = json.loads(arguments.selection.read_text(encoding="utf-8"))
        shard_numbers = json.loads(arguments.shards)
    except (OSError, ValueError) as error:
        print(f"error: {error}", file=sys.stderr)
        return EXIT_USAGE
    if not (isinstance(selected, list) and all(isinstance(node_id, str) for node_id in selected)):
        print(f"error: {arguments.selection} is not a JSON list of node ids", file=sys.stderr)
        return EXIT_USAGE
    if not (isinstance(shard_numbers, list) and shard_numbers and all(isinstance(number, int) and not isinstance(number, bool) for number in shard_numbers)):
        print(f"error: --shards must be a JSON list of shard numbers, not {arguments.shards!r}", file=sys.stderr)
        return EXIT_USAGE
    shard_count = len(shard_numbers)
    shard_runs: dict[int, Optional[list[str]]] = {}
    for number in range(1, shard_count + 1):
        results_path = arguments.results / SHARD_FOLDER_FORMAT.format(number=number) / shard_durations.RESULTS_FILE_NAME
        if not results_path.is_file():
            shard_runs[number] = None
            continue
        try:
            shard_runs[number] = results_node_ids(results_path, INTEGRATION_ROOT)
        except (OSError, ElementTree.ParseError, shard_durations.UnusableResults) as error:
            print(f"error: shard {number}/{shard_count}: {error}", file=sys.stderr)
            shard_runs[number] = None
    problems = coverage_problems(selected, shard_runs, shard_count)
    if problems:
        _append(arguments.summary, ["### Shard coverage: failed", "", *(f"- {problem}" for problem in problems)])
        return EXIT_COVERAGE_FAILED
    _append(arguments.summary, ["### Shard coverage", "", f"Each of the {len(selected)} selected tests ran exactly once across {shard_count} shards."])
    return 0


def main(argv: Optional[list[str]] = None) -> int:
    """Runs the selection or check command; see the module description for each one's statuses."""
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    commands = parser.add_subparsers(dest="command", required=True)
    selection = commands.add_parser("selection", help="collect the selection's node ids and report its missing shard times")
    selection.add_argument("--python", required=True, help="the Python whose pytest collects the scenarios")
    selection.add_argument("--group", default="", help="GROUP: a group or -m expression of groups; empty selects every scenario")
    selection.add_argument("--output", type=Path, required=True, help="the file that receives the node ids as a JSON list")
    selection.add_argument("--summary", type=Path, help="the file the timing report is appended to")
    check = commands.add_parser("check", help="check that every selected test ran exactly once across the shards")
    check.add_argument("--selection", type=Path, required=True, help="the JSON list of node ids selection wrote")
    check.add_argument("--results", type=Path, required=True, help="the folder holding one integration-results-shard-<k> folder per shard")
    check.add_argument("--shards", required=True, help="the shard numbers as a JSON list, as the select job's shards output gives them")
    check.add_argument("--summary", type=Path, help="the file the result is appended to")
    arguments = parser.parse_args(argv)
    if arguments.command == "selection":
        return run_selection(arguments)
    return run_check(arguments)


if __name__ == "__main__":
    sys.exit(main())
