#!/usr/bin/env python3
"""Count the world's mobs by one numeric field of their record (level by default).

    mob_census.py [--repo DIR] [--field level] [--top 20]          the most common values with their counts
    mob_census.py [--repo DIR] --field race --value 13             also list the vnums with race 13

Fields are the 40 integers after a mob's flags line, as src/db.cpp:1953-2033 reads them: level, ob, parry,
dodge, hit, max_hit, damage, ene_regen, gold, exp, owner, position, default_pos, sex, race, pref, weight,
height, prog, butcher, corpse, rp_flag, prof, mana, move, bodytype, save, str, int, wil, dex, con, lea,
language, perception, resistance, vulnerability, script, spirit, will_teach. A record that stops early has
no value for the missing fields (and is not counted for them), except will_teach, which the loader leaves
at 0. The repository is only read.
"""

import argparse
import collections
import sys
from pathlib import Path
from typing import Dict, List, Optional, Sequence

sys.path.insert(0, str(Path(__file__).resolve().parent))
import rots_sources  # noqa: E402
from rots_sources import Record  # noqa: E402


def census(mobs: Dict[int, Record], field: str) -> "collections.Counter[int]":
    """How many mobs have each value of `field`."""
    return collections.Counter(mob[field] for mob in mobs.values() if field in mob)


def vnums_with(mobs: Dict[int, Record], field: str, value: int) -> List[int]:
    return sorted(vnum for vnum, mob in mobs.items() if mob.get(field) == value)


def format_census(counts: "collections.Counter[int]", field: str, top: int, mob_total: int) -> List[str]:
    lines = [f"{mob_total} mobs; {sum(counts.values())} with a {field}; {len(counts)} distinct values",
             f"{'count':>6}  {field}"]
    lines += [f"{count:6d}  {value}" for value, count in counts.most_common(top if top > 0 else None)]
    return lines


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--repo", type=Path, default=rots_sources.default_repo(), help="repository root")
    parser.add_argument("--field", default="level", choices=rots_sources.MOB_FIELDS, help="the field to count")
    parser.add_argument("--top", type=int, default=20, help="how many values to show; 0 shows all")
    parser.add_argument("--value", type=int, help="also list the vnums of the mobs with this value")
    parser.add_argument("--out", type=Path, help="write the census to this file instead of standard output")
    return parser


def main(argv: Optional[Sequence[str]] = None) -> int:
    arguments = build_parser().parse_args(argv)
    try:
        mobs = rots_sources.parse_world_mobs(arguments.repo)
    except OSError as error:
        print(f"mob_census: {error}", file=sys.stderr)
        return 1
    counts = census(mobs, arguments.field)
    lines = format_census(counts, arguments.field, arguments.top, len(mobs))
    if arguments.value is not None:
        vnums = vnums_with(mobs, arguments.field, arguments.value)
        lines += ["", f"{len(vnums)} mobs with {arguments.field} {arguments.value}: " + " ".join(map(str, vnums))]
    if arguments.out is None:
        print("\n".join(lines))
    else:
        arguments.out.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
