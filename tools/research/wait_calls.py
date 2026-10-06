#!/usr/bin/env python3
"""List every WAIT_STATE, WAIT_STATE_BRIEF and WAIT_STATE_FULL call in src/*.cpp with its arguments.

    wait_calls.py [--repo DIR] [--out FILE]                  one line per call, by file and line
    wait_calls.py [--repo DIR] --by-priority                 the same calls grouped by priority

The macros are in src/utils.h:
    WAIT_STATE(ch, cycle)                                      = WAIT_STATE_FULL(ch, cycle, 0, 0, 50, 0, 0, 0,
                                                                                 AFF_WAITING, TARGET_IGNORE)
    WAIT_STATE_BRIEF(ch, cycle, cmd, subcmd, priority, flags)
    WAIT_STATE_FULL(ch, cycle, cmd, subcmd, priority, flg, number, pointer, flags, data_type)
Calls inside comments and #define lines are skipped, as is src/wait_functions.cpp. The repository is only read.
"""

import argparse
import sys
from pathlib import Path
from typing import Dict, List, Optional, Sequence

sys.path.insert(0, str(Path(__file__).resolve().parent))
import rots_sources  # noqa: E402
from rots_sources import Record  # noqa: E402

WAIT_MACROS = ["WAIT_STATE_FULL", "WAIT_STATE_BRIEF", "WAIT_STATE"]
# Argument positions of (cycle, command, subcommand, priority, flags) for each macro.
ARGUMENT_POSITIONS = {"WAIT_STATE_BRIEF": (1, 2, 3, 4, 5), "WAIT_STATE_FULL": (1, 2, 3, 4, 8)}
# What WAIT_STATE passes to WAIT_STATE_FULL for the arguments it does not take (src/utils.h).
WAIT_STATE_IMPLICIT = {"command": "0", "subcommand": "0", "priority": "50", "flags": "AFF_WAITING"}
SKIPPED_FILE_MARKER = "wait_functions"


def wait_calls_in(text: str, file_label: str) -> List[Record]:
    """The wait calls of one source file, in order."""
    calls = []
    for macro, offset, arguments in rots_sources.find_macro_calls(text, WAIT_MACROS):
        call: Record = {"file": file_label, "line": rots_sources.line_number_at(text, offset), "macro": macro,
                        "implicit": macro == "WAIT_STATE"}
        if macro == "WAIT_STATE":
            call.update(WAIT_STATE_IMPLICIT, cycle=arguments[1] if len(arguments) > 1 else "?")
        else:
            positions = ARGUMENT_POSITIONS[macro]
            values = [arguments[position] if position < len(arguments) else "?" for position in positions]
            call.update(zip(("cycle", "command", "subcommand", "priority", "flags"), values))
        calls.append(call)
    return calls


def repository_wait_calls(repo: Path) -> List[Record]:
    calls = []
    for path in sorted((repo / "src").glob("*.cpp")):
        if SKIPPED_FILE_MARKER in path.name:
            continue
        calls += wait_calls_in(path.read_text(encoding="latin-1"), str(path.relative_to(repo)))
    return calls


def format_call(call: Record) -> str:
    priority = f"{call['priority']} (implicit)" if call["implicit"] else call["priority"]
    return (f"{call['file']}:{call['line']} {call['macro']} cycle={call['cycle']} cmd={call['command']} "
            f"sub={call['subcommand']} prio={priority} flags={call['flags']}")


def format_by_priority(calls: List[Record]) -> List[str]:
    groups: Dict[str, List[Record]] = {}
    for call in calls:
        groups.setdefault(call["priority"], []).append(call)
    lines = []
    for priority in sorted(groups, key=lambda value: (not value.lstrip("-").isdigit(),
                                                      int(value) if value.lstrip("-").isdigit() else 0, value)):
        lines += [f"== priority {priority} ({len(groups[priority])} calls)"]
        lines += ["   " + format_call(call) for call in groups[priority]]
    return lines


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--repo", type=Path, default=rots_sources.default_repo(), help="repository root")
    parser.add_argument("--out", type=Path, help="write the list to this file instead of standard output")
    parser.add_argument("--by-priority", action="store_true", help="group the calls by priority")
    return parser


def main(argv: Optional[Sequence[str]] = None) -> int:
    arguments = build_parser().parse_args(argv)
    try:
        calls = repository_wait_calls(arguments.repo)
    except OSError as error:
        print(f"wait_calls: {error}", file=sys.stderr)
        return 1
    lines = format_by_priority(calls) if arguments.by_priority else [format_call(call) for call in calls]
    lines.append(f"{len(calls)} calls")
    if arguments.out is None:
        print("\n".join(lines))
    else:
        arguments.out.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
