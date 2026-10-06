#!/usr/bin/env python3
"""List the command registrations in assign_command_pointers() and summarise them.

    command_table.py [--repo DIR] [--out FILE]              table, tallies, gaps and the sort_commands() stop
    command_table.py [--repo DIR] --summary-only            everything except the per-command table

Reads every COMMANDO(number, min_pos, pointer, min_level, retired, subcommand, targfl1, targfl2,
special_mask) in src/interpre.cpp (the macro at the top of that file), resolves CMD_* numbers from
src/interpre.h and names each command from command[] (entry N-1 is command N). The repository is only read.
"""

import argparse
import collections
import re
import sys
from pathlib import Path
from typing import Dict, List, Optional, Sequence, Set

sys.path.insert(0, str(Path(__file__).resolve().parent))
import rots_sources  # noqa: E402
from rots_sources import Record  # noqa: E402

COMMANDO_FIELDS = ["number_text", "position", "handler", "level", "retired", "subcmd", "target1", "target2", "mask"]
REGISTRATION_FUNCTION = "void assign_command_pointers(void)"
COMMAND_ARRAY_START = "const char* command[] = {"
# command[] ends with this sentinel string, written "\n" in the source.
COMMAND_LIST_END = "\\n"
NO_HANDLER = {"0", "NULL", "nullptr"}
DEFAULT_MAX_CMD_LIST = 350


def parse_command_names(interpreter_text: str) -> List[str]:
    """command[] in order, up to its "\\n" sentinel; command number N is entry N-1."""
    code = rots_sources.blank_c_comments(interpreter_text)
    start = code.index(COMMAND_ARRAY_START) + len(COMMAND_ARRAY_START)
    end = code.index("};", start)
    names = []
    for match in re.finditer(r"\"((?:[^\"\\]|\\.)*)\"", code[start:end]):
        if match.group(1) == COMMAND_LIST_END:
            break
        names.append(match.group(1))
    return names


def parse_number_defines(header_text: str) -> Dict[str, int]:
    """Integer #defines of a header by name, such as CMD_DEFEND and MAX_CMD_LIST."""
    code = rots_sources.blank_c_comments(header_text)
    return {match.group(1): int(match.group(2))
            for match in re.finditer(r"^\s*#define\s+(\w+)\s+(-?\d+)\s*$", code, re.M)}


def registration_body(interpreter_text: str) -> str:
    """The text of assign_command_pointers(), up to the "}" line that closes it."""
    start = interpreter_text.index(REGISTRATION_FUNCTION)
    end = interpreter_text.index("\n}\n", start)
    return interpreter_text[start:end]


def parse_registrations(interpreter_text: str, defines: Dict[str, int]) -> List[Record]:
    """One record per COMMANDO call, in source order, with the resolved number and the command's name."""
    names = parse_command_names(interpreter_text)
    rows = []
    for _macro, _offset, arguments in rots_sources.find_macro_calls(registration_body(interpreter_text), ["COMMANDO"]):
        if len(arguments) != len(COMMANDO_FIELDS):
            raise ValueError(f"COMMANDO with {len(arguments)} arguments: {arguments}")
        row: Record = dict(zip(COMMANDO_FIELDS, arguments))
        number_text = row["number_text"]
        row["number"] = int(number_text) if re.fullmatch(r"\d+", number_text) else defines.get(number_text)
        if row["number"] is None:
            raise ValueError(f"COMMANDO number {number_text!r} is not an integer or a known #define")
        row["name"] = names[row["number"] - 1] if 1 <= row["number"] <= len(names) else "?"
        rows.append(row)
    return rows


def registered_numbers(rows: List[Record]) -> Set[int]:
    """Numbers that some COMMANDO gives a handler."""
    return {row["number"] for row in rows if row["handler"] not in NO_HANDLER}


def unregistered_numbers(rows: List[Record]) -> List[int]:
    """Numbers between 1 and the highest registered one that have no handler."""
    registered = registered_numbers(rows)
    highest = max(registered) if registered else 0
    return [number for number in range(1, highest + 1) if number not in registered]


def sort_stop(rows: List[Record], max_cmd_list: int) -> int:
    """The last command sort_commands() includes: it walks up from 1 and stops at the first number without a
    handler (src/act_info.cpp:2957-2967), so later commands are left out of its sorted list."""
    registered = registered_numbers(rows)
    number = 1
    while number < max_cmd_list and number in registered:
        number += 1
    return number - 1


def format_report(rows: List[Record], names: List[str], max_cmd_list: int, include_table: bool) -> List[str]:
    lines = [f"COMMANDO registrations: {len(rows)}"]
    if include_table:
        lines += ["", "| number | name | position | handler | level | retired | subcmd | target1 | target2 | mask |",
                  "| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |"]
        for row in sorted(rows, key=lambda row: row["number"]):
            number = row["number"] if row["number_text"].isdigit() else f"{row['number']} ({row['number_text']})"
            lines.append(f"| {number} | {row['name']} | {row['position']} | {row['handler']} | {row['level']} | "
                         f"{row['retired']} | {row['subcmd']} | {row['target1']} | {row['target2']} | {row['mask']} |")
    for title, field in (("minimum position", "position"), ("minimum level", "level"),
                         ("retired allowed", "retired"), ("mask", "mask")):
        lines += ["", f"== by {title}"]
        lines += [f"{count:4d}  {value}" for value, count in collections.Counter(row[field] for row in rows).most_common()]
    both_full = sum(1 for row in rows if row["target1"] == "FULL_TARGET" and row["target2"] == "FULL_TARGET")
    lines += ["", f"both target masks FULL_TARGET: {both_full}"]
    duplicates = [number for number, count in collections.Counter(row["number"] for row in rows).items() if count > 1]
    if duplicates:
        lines.append("registered more than once: " + ", ".join(str(number) for number in sorted(duplicates)))
    lines += ["", "== unregistered numbers (name in command[])"]
    for number in unregistered_numbers(rows):
        name = names[number - 1] if 1 <= number <= len(names) else "?"
        lines.append(f"{number:4d}  {name!r}")
    stop = sort_stop(rows, max_cmd_list)
    lines += ["", f"sort_commands() stops after {stop}: commands {stop + 1} and above are not in its sorted list"]
    return lines


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--repo", type=Path, default=rots_sources.default_repo(), help="repository root")
    parser.add_argument("--out", type=Path, help="write the report to this file instead of standard output")
    parser.add_argument("--summary-only", action="store_true", help="leave out the per-command table")
    return parser


def main(argv: Optional[Sequence[str]] = None) -> int:
    arguments = build_parser().parse_args(argv)
    try:
        interpreter_text = rots_sources.read_source(arguments.repo, "src/interpre.cpp")
        defines = parse_number_defines(rots_sources.read_source(arguments.repo, "src/interpre.h"))
        rows = parse_registrations(interpreter_text, defines)
    except (OSError, ValueError) as error:
        print(f"command_table: {error}", file=sys.stderr)
        return 1
    report = format_report(rows, parse_command_names(interpreter_text),
                           defines.get("MAX_CMD_LIST", DEFAULT_MAX_CMD_LIST), not arguments.summary_only)
    if arguments.out is None:
        print("\n".join(report))
    else:
        arguments.out.write_text("\n".join(report) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
