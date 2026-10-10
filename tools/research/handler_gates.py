#!/usr/bin/env python3
"""Show the gate checks inside command handlers: the lines of a handler's body that match a pattern.

    handler_gates.py bash kick rescue [--repo DIR] [--out FILE]     the named handlers (with or without do_)
    handler_gates.py [--repo DIR]                                    every ACMD(do_...) handler in src/*.cpp
    handler_gates.py hide --pattern 'GET_SKILL|IS_SHADOW'            a different pattern (a Python regex)

A handler is defined as ACMD(do_<name>) or void do_<name>(char_data..., not ending in ";". Its body runs to the
first line that is exactly "}", which is how this codebase closes a function. Lines that start with "//" are
not reported. The default pattern finds skill, race, mana, move, spirit, specialization, level, riding,
peace-room, shadow and target checks. The repository is only read.
"""

import argparse
import re
import sys
from pathlib import Path
from typing import Dict, List, Optional, Sequence

sys.path.insert(0, str(Path(__file__).resolve().parent))
import rots_sources  # noqa: E402

DEFAULT_PATTERN = (r"GET_SKILL\(|get_skill\(|GET_RACE|get_race|is_skill_valid|is_skill_allowed|add_skill_timer|"
                   r"GET_MANA|tmpabilities\.mana|GET_MOVE|GET_SPIRIT|add_spirits|PEACEROOM|IS_SHADOW|is_shadow|"
                   r"is_target_valid|GET_SPEC|get_specialization|PROF_LEVEL|_spec\(\)|RIDING")
LINE_WIDTH = 150


def all_handler_names(sources: Dict[str, List[str]]) -> List[str]:
    """The name after do_ of every ACMD(do_...) definition, sorted and without repeats."""
    names = set()
    for lines in sources.values():
        for line in lines:
            match = re.match(r"^ACMD\(do_(\w+)\)", line)
            if match and not line.rstrip().endswith(";"):
                names.add(match.group(1))
    return sorted(names)


def handler_report(handler: str, sources: Dict[str, List[str]], pattern: "re.Pattern[str]") -> List[str]:
    """The header line for each definition of do_<handler> and its matching body lines."""
    report = []
    for file_label, lines in sources.items():
        for start in rots_sources.find_handler_definitions(lines, handler):
            end = rots_sources.function_end(lines, start)
            report.append(f"== do_{handler} {file_label}:{start + 1}-{end + 1}")
            for index in range(start, end):
                if pattern.search(lines[index]) and not lines[index].strip().startswith("//"):
                    report.append(f"   {index + 1}: {lines[index].strip()[:LINE_WIDTH]}")
    return report or [f"== do_{handler} NOT FOUND"]


def read_sources(repo: Path) -> Dict[str, List[str]]:
    return {str(path.relative_to(repo)): path.read_text(encoding="latin-1").split("\n")
            for path in sorted((repo / "src").glob("*.cpp"))}


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("handlers", nargs="*", help="handler names, such as bash or do_bash; default: all ACMD")
    parser.add_argument("--repo", type=Path, default=rots_sources.default_repo(), help="repository root")
    parser.add_argument("--pattern", default=DEFAULT_PATTERN, help="regular expression for a gate line")
    parser.add_argument("--out", type=Path, help="write the report to this file instead of standard output")
    return parser


def main(argv: Optional[Sequence[str]] = None) -> int:
    arguments = build_parser().parse_args(argv)
    try:
        pattern = re.compile(arguments.pattern)
        sources = read_sources(arguments.repo)
    except (OSError, re.error) as error:
        print(f"handler_gates: {error}", file=sys.stderr)
        return 1
    handlers = [re.sub(r"^do_", "", name) for name in arguments.handlers] or all_handler_names(sources)
    lines = [line for handler in handlers for line in handler_report(handler, sources, pattern)]
    if arguments.out is None:
        print("\n".join(lines))
    else:
        arguments.out.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
