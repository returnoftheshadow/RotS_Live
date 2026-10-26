"""Refreshes tests/integration/shard_durations.json, the per-test seconds CI's shards are balanced by, from CI's JUnit results.

Download the results of one or more full CI runs first, one folder per run, for example
`gh run download <run id> --pattern 'integration-results-shard-*' --dir <folder>/<run id>`.
Each test's time is the median of its times in every results.xml under the folder, leaving out
a run where it failed, errored or was skipped (an expected failure still counts). A test no
result file timed is left out of the file, and the shards count durations.DEFAULT_TEST_SECONDS
for it until the next refresh. A refresh that would cover far fewer tests than the file does
now, such as one from a single shard's results, is refused unless --force is given.

Run from the repository root as `make integration-shard-durations RESULTS=<folder>`.
"""

from __future__ import annotations

import argparse
import json
import math
import os
import re
import statistics
import sys
import tempfile
import xml.etree.ElementTree as ElementTree
from pathlib import Path
from typing import Callable, Iterable, Mapping, Optional

from rots_harness import durations, sharding

# tests/integration: the scenario files a JUnit class name is matched against, and the durations file.
INTEGRATION_ROOT = Path(__file__).resolve().parents[1]
# The JUnit file each CI shard uploads.
RESULTS_FILE_NAME = "results.xml"
# Testcase children that mark a run whose time does not show how long the test takes.
UNFINISHED_OUTCOMES = ("failure", "error")
# The skipped child of a skipped testcase; its time counts only for an expected failure.
SKIPPED_OUTCOME = "skipped"
# The skipped type pytest writes for an expected failure (xfail), which ran the whole test.
EXPECTED_FAILURE_TYPE = "pytest.xfail"
# Decimal places kept per time; CI's times vary by a few hundredths between runs, so finer
# places would only make every refresh rewrite every line.
SECONDS_DECIMALS = 1
# The largest results file read; a shard's file is about 100 KB.
MAX_RESULTS_BYTES = 16 * 1024 * 1024
# Markup a results file never holds that could make the parser expand entities.
REFUSED_MARKUP = ("<!DOCTYPE", "<!ENTITY")
# The byte-order marks of UTF-8, UTF-16 and UTF-32 (UTF-32's little-endian mark starts like
# UTF-16's). pytest writes its results as UTF-8 without one, and a file in another encoding could
# hide REFUSED_MARKUP from a search of its bytes while the parser still reads it.
BYTE_ORDER_MARKS = (b"\xef\xbb\xbf", b"\xff\xfe", b"\xfe\xff", b"\x00\x00\xfe\xff")
# The encoding an XML declaration names, if it names one.
DECLARED_ENCODING = re.compile(r"""<\?xml[^>]*\bencoding\s*=\s*["']([^"']*)["']""")
# The share of the current file's tests a refresh must cover unless --force is given.
MINIMUM_COVERED_SHARE = 0.9
# The written file's permissions; mkstemp's own let only the owner read it.
FILE_MODE = 0o644
# make integration-shard-durations exits with this when it finds no results, refuses them, or cannot write the file.
EXIT_USAGE = 2


class UnusableResults(ValueError):
    """A results file the tool refuses to read: too large, not plain UTF-8, or holding a DOCTYPE or entity declaration."""


def node_id(class_name: str, test_name: str, integration_root: Path) -> Optional[str]:
    """The pytest node id of a JUnit testcase, or None when no file under integration_root matches its class name.

    pytest writes `scenarios/test_bank.py::test_x` as class name `scenarios.test_bank` and name
    `test_x`, and a test class's name after the module's. The longest leading part of the class
    name that names a .py file inside integration_root is the module; an empty part never matches.
    """
    parts = class_name.split(".")
    if any(not part for part in parts):
        return None
    root = integration_root.resolve()
    for module_parts in range(len(parts), 0, -1):
        module_path = "/".join(parts[:module_parts]) + ".py"
        candidate = (root / module_path).resolve()
        if not candidate.is_relative_to(root):
            continue
        if candidate.is_file():
            return "::".join([module_path, *parts[module_parts:], test_name])
    return None


def read_results(results_path: Path) -> ElementTree.Element:
    """The parsed results file; raises OSError, ElementTree.ParseError, or UnusableResults.

    UnusableResults is raised for a file too large, one that is not UTF-8 without a byte-order mark
    (with no NUL byte, and naming no other encoding), or one holding a DOCTYPE or entity declaration.
    """
    if results_path.stat().st_size > MAX_RESULTS_BYTES:
        raise UnusableResults(f"{results_path} is larger than {MAX_RESULTS_BYTES} bytes")
    content = results_path.read_bytes()
    # The markup search below reads the text as UTF-8, so the parser must read the same text: no
    # byte-order mark or NUL byte (both point the parser at UTF-16 or UTF-32), valid UTF-8, and no
    # other encoding declared.
    if content.startswith(BYTE_ORDER_MARKS) or b"\x00" in content:
        raise UnusableResults(f"{results_path} is not UTF-8 without a byte-order mark, as pytest writes it")
    try:
        text = content.decode("utf-8")
    except UnicodeDecodeError:
        raise UnusableResults(f"{results_path} is not valid UTF-8, as pytest writes it") from None
    declaration = DECLARED_ENCODING.match(text)
    if declaration is not None and declaration.group(1).lower().replace("_", "-") not in ("utf-8", "utf8"):
        raise UnusableResults(f"{results_path} declares encoding {declaration.group(1)!r}, not the UTF-8 pytest writes")
    for markup in REFUSED_MARKUP:
        if markup in text:
            raise UnusableResults(f"{results_path} holds {markup}, which no pytest results file has")
    return ElementTree.fromstring(content)


def case_counts(case: ElementTree.Element) -> bool:
    """Whether a testcase's time shows how long its test takes: it passed or failed as expected."""
    if any(case.find(outcome) is not None for outcome in UNFINISHED_OUTCOMES):
        return False
    skipped = case.find(SKIPPED_OUTCOME)
    return skipped is None or skipped.get("type") == EXPECTED_FAILURE_TYPE


def finished_case_times(results_path: Path, integration_root: Path, warn: Callable[[str], None]) -> list[tuple[str, float]]:
    """Each node id and time in a JUnit file whose test passed or failed as expected; raises what read_results raises.

    A testcase whose class name matches no scenario file is left out, as is one whose time is not a
    finite number of seconds of at least 0, which warn reports.
    """
    times = []
    for case in read_results(results_path).iter("testcase"):
        if not case_counts(case):
            continue
        case_id = node_id(case.get("classname", ""), case.get("name", ""), integration_root)
        if case_id is None:
            continue
        time_text = case.get("time", "")
        try:
            seconds = float(time_text)
        except ValueError:
            seconds = math.nan
        if not math.isfinite(seconds) or seconds < 0:
            warn(f"warning: {results_path}: {case_id} has time {time_text!r}, which is not a number of seconds; left out")
            continue
        times.append((case_id, seconds))
    return times


def median_seconds(results_paths: Iterable[Path], integration_root: Path, warn: Callable[[str], None]) -> dict[str, float]:
    """Each node id's median time across the results files, rounded to SECONDS_DECIMALS; raises what read_results raises."""
    times_by_id: dict[str, list[float]] = {}
    for results_path in results_paths:
        for case_id, seconds in finished_case_times(results_path, integration_root, warn):
            times_by_id.setdefault(case_id, []).append(seconds)
    return {case_id: round(statistics.median(times), SECONDS_DECIMALS) for case_id, times in times_by_id.items()}


def write_seconds(path: Path, seconds: Mapping[str, float]) -> None:
    """Writes the times as a JSON object sorted by node id, one entry per line, replacing the file; raises OSError.

    The file is written beside path and then moved over it, so an interrupted write never leaves a broken file.
    """
    descriptor, temporary_name = tempfile.mkstemp(prefix=path.name + ".", suffix=".tmp", dir=path.parent)
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8") as temporary:
            temporary.write(json.dumps(dict(seconds), indent=1, sort_keys=True) + "\n")
        os.chmod(temporary_name, FILE_MODE)
        os.replace(temporary_name, path)
    except BaseException:
        Path(temporary_name).unlink(missing_ok=True)
        raise


def _warn(line: str) -> None:
    """Prints a warning line to stderr."""
    print(line, file=sys.stderr)


def main(argv: Optional[list[str]] = None, *, integration_root: Path = INTEGRATION_ROOT) -> int:
    """Replaces --output with the median times under the folders; returns EXIT_USAGE when there are none, they are refused, or a file cannot be read or written.

    integration_root holds the scenario files the class names are matched against; tests replace it.
    """
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    # Read as text, since Path("") would quietly become the current folder.
    parser.add_argument("folders", nargs="+", help="folders searched for results.xml files, such as one per downloaded run")
    parser.add_argument("--output", type=Path, default=integration_root / sharding.SHARD_DURATIONS_RELATIVE, help="the durations file to replace")
    parser.add_argument("--force", action="store_true", help="write the file even when it would cover far fewer tests than now")
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
        seconds = median_seconds(results_paths, integration_root, _warn)
    except (OSError, ElementTree.ParseError, UnusableResults) as error:
        print(f"error: {error}", file=sys.stderr)
        return EXIT_USAGE
    current_count = len(durations.read_durations(arguments.output))
    if not arguments.force and len(seconds) < current_count * MINIMUM_COVERED_SHARE:
        print(
            f"error: the results time {len(seconds)} tests, far fewer than the {current_count} in {arguments.output}; "
            "download every shard of a full run, or pass --force when tests were removed",
            file=sys.stderr,
        )
        return EXIT_USAGE
    try:
        write_seconds(arguments.output, seconds)
    except OSError as error:
        print(f"error: {error}", file=sys.stderr)
        return EXIT_USAGE
    print(f"{len(seconds)} tests from {len(results_paths)} results files, {sum(seconds.values()) / 60:.1f} minutes in all, written to {arguments.output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
