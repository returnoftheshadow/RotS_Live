"""Refreshes tests/integration/shard_durations.json, the per-test seconds CI's shards are balanced by, from CI's JUnit results.

Download the results of one or more full CI runs first, one folder per run, for example
`gh run download <run id> --pattern 'integration-results-shard-*' --dir <folder>/<run id>`.
Each test's time is the median of its times in every results.xml under the folder, leaving out
a run where it failed or errored. A test no result file timed is left out of the file, and the
shards count durations.DEFAULT_TEST_SECONDS for it until the next refresh.

Run from the repository root as `make integration-shard-durations RESULTS=<folder>`.
"""

from __future__ import annotations

import argparse
import json
import statistics
import sys
import xml.etree.ElementTree as ElementTree
from pathlib import Path
from typing import Iterable, Mapping, Optional

from rots_harness import sharding

# tests/integration: the scenario files a JUnit class name is matched against, and the durations file.
INTEGRATION_ROOT = Path(__file__).resolve().parents[1]
# The JUnit file each CI shard uploads.
RESULTS_FILE_NAME = "results.xml"
# A testcase child that marks a run whose time is cut short.
UNFINISHED_OUTCOMES = ("failure", "error")
# Decimal places kept per time; CI's times vary by a few hundredths between runs, so finer
# places would only make every refresh rewrite every line.
SECONDS_DECIMALS = 1
# make integration-shard-durations exits with this when it finds no results or cannot write the file.
EXIT_USAGE = 2


def node_id(class_name: str, test_name: str, integration_root: Path) -> Optional[str]:
    """The pytest node id of a JUnit testcase, or None when no file under integration_root matches its class name.

    pytest writes `scenarios/test_bank.py::test_x` as class name `scenarios.test_bank` and name
    `test_x`, and a test class's name after the module's. The longest leading part of the class
    name that names a .py file is the module.
    """
    parts = class_name.split(".")
    for module_parts in range(len(parts), 0, -1):
        module_path = "/".join(parts[:module_parts]) + ".py"
        if (integration_root / module_path).is_file():
            return "::".join([module_path, *parts[module_parts:], test_name])
    return None


def finished_case_times(results_path: Path, integration_root: Path) -> list[tuple[str, float]]:
    """Each node id and time in a JUnit file whose test did not fail or error; raises OSError or ElementTree.ParseError.

    A testcase whose class name matches no scenario file is left out.
    """
    times = []
    for case in ElementTree.parse(results_path).iter("testcase"):
        if any(case.find(outcome) is not None for outcome in UNFINISHED_OUTCOMES):
            continue
        case_id = node_id(case.get("classname", ""), case.get("name", ""), integration_root)
        if case_id is None:
            continue
        times.append((case_id, float(case.get("time", "0"))))
    return times


def median_seconds(results_paths: Iterable[Path], integration_root: Path) -> dict[str, float]:
    """Each node id's median time across the results files, rounded to SECONDS_DECIMALS; raises OSError or ElementTree.ParseError."""
    times_by_id: dict[str, list[float]] = {}
    for results_path in results_paths:
        for case_id, seconds in finished_case_times(results_path, integration_root):
            times_by_id.setdefault(case_id, []).append(seconds)
    return {case_id: round(statistics.median(times), SECONDS_DECIMALS) for case_id, times in times_by_id.items()}


def write_seconds(path: Path, seconds: Mapping[str, float]) -> None:
    """Writes the times as a JSON object sorted by node id, one entry per line, replacing the file; raises OSError."""
    path.write_text(json.dumps(dict(seconds), indent=1, sort_keys=True) + "\n", encoding="utf-8")


def main(argv: Optional[list[str]] = None, *, integration_root: Path = INTEGRATION_ROOT) -> int:
    """Replaces --output with the median times under the folders; returns EXIT_USAGE when there are none or a file cannot be read or written.

    integration_root holds the scenario files the class names are matched against; tests replace it.
    """
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    # Read as text, since Path("") would quietly become the current folder.
    parser.add_argument("folders", nargs="+", help="folders searched for results.xml files, such as one per downloaded run")
    parser.add_argument("--output", type=Path, default=integration_root / sharding.SHARD_DURATIONS_RELATIVE, help="the durations file to replace")
    arguments = parser.parse_args(argv)
    results_paths = []
    for folder_text in arguments.folders:
        folder = Path(folder_text)
        if not folder_text or not folder.is_dir():
            print(f"error: {folder_text!r} is not a folder of downloaded CI results", file=sys.stderr)
            return EXIT_USAGE
        results_paths.extend(sorted(folder.rglob(RESULTS_FILE_NAME)))
    if not results_paths:
        print(f"error: no {RESULTS_FILE_NAME} under {', '.join(arguments.folders)}", file=sys.stderr)
        return EXIT_USAGE
    try:
        seconds = median_seconds(results_paths, integration_root)
        write_seconds(arguments.output, seconds)
    except (OSError, ElementTree.ParseError) as error:
        print(f"error: {error}", file=sys.stderr)
        return EXIT_USAGE
    print(f"{len(seconds)} tests from {len(results_paths)} results files, {sum(seconds.values()) / 60:.1f} minutes in all, written to {arguments.output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
