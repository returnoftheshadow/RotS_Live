#!/usr/bin/env python3
"""Print the markdown "Where to train" table for one class.

    trainer_tables.py warrior [--repo DIR] [--out FILE]
    trainer_tables.py mage --data skill_data.json [--out FILE]

One row per usable guildmaster whose guild teaches at least one of the class's skills (as class_packs.py
assigns them): the trainer, where zone resets load it, the races it teaches and the skills with their caps.
Warrior rows summarise the ten weapon skills in one column. Unusable guildmasters follow the table as
UNUSABLE lines; one whose program number is not a guild is listed for every class, because what it would
teach cannot be known.
"""

import argparse
import sys
from pathlib import Path
from typing import Any, Dict, List, Optional, Sequence, Tuple

sys.path.insert(0, str(Path(__file__).resolve().parent))
import class_packs  # noqa: E402
import rots_sources  # noqa: E402
from rots_sources import Record  # noqa: E402

LIGHT_RACES = {"Human", "Dwarf", "Wood Elf", "Hobbit", "High Elf"}
WEAPON_SKILLS = ["barehanded", "slashing", "concussion", "whips/flails", "piercing", "spears", "axes",
                 "natural attacks", "two-handed", "weapon mastery"]
TABLE_CLASSES = ["warrior", "ranger", "mystic", "mage", "general"]


def describe_taught_races(races: Any) -> str:
    """'Light races', 'Gods only', 'nobody' or the sorted race names (God left out)."""
    if not races or races == "PARSE?":
        return "nobody"
    mortal_races = set(races) - {"God"}
    if mortal_races == LIGHT_RACES:
        return "Light races"
    return ", ".join(sorted(mortal_races)) or "Gods only"


def describe_location(load: Record) -> str:
    """One load room with its zone, and the zone whose reset loads it when that is another zone."""
    text = f"{load['room_name']} (room {load['room']}), {load['zone']}"
    loaded_by = load.get("loaded_by", load["zone"])
    return text if loaded_by == load["zone"] else f"{text} (loaded by the {loaded_by} zone)"


def describe_locations(guildmaster: Record) -> str:
    return "; ".join(describe_location(load) for load in guildmaster.get("loads", [])) or "nowhere"


def taught_skills(guild: Record, skill_names: Dict[int, str]) -> List[Tuple[str, int]]:
    """(skill name, cap) for each of the class's skills the guild teaches, in skill order."""
    return [(skill_names[skill_index], rots_sources.guild_cap(guild, skill_index))
            for skill_index in skill_names if rots_sources.guild_cap(guild, skill_index) > 0]


def row_cells(class_name: str, taught: List[Tuple[str, int]]) -> List[str]:
    """The skill cells of one row: for warriors a weapon summary and the other skills, else one list."""
    if class_name != "warrior":
        return [", ".join(f"{name} {cap}" for name, cap in taught)]
    weapons = [(name, cap) for name, cap in taught if name in WEAPON_SKILLS]
    others = [(name, cap) for name, cap in taught if name not in WEAPON_SKILLS]
    weapon_text = f"{len(weapons)} of 10, up to {max(cap for _name, cap in weapons)}" if weapons else "none"
    return [weapon_text, ", ".join(f"{name} {cap}" for name, cap in others) or "none"]


def build_table(class_name: str, data: Dict[str, Any]) -> str:
    """The markdown table and UNUSABLE lines for one class."""
    guilds = data["guilds"]
    skill_names = {skill["index"]: skill["name"] for skill in class_packs.build_packs(data).get(class_name, [])}
    rows: List[Tuple[Record, List[str]]] = []
    unusable: List[Tuple[Record, List[str]]] = []
    for guildmaster in data["guildmasters"]:
        problems = class_packs.trainer_problems(guildmaster, len(guilds), brief=True)
        if not class_packs.has_valid_guild(guildmaster, len(guilds)):
            unusable.append((guildmaster, problems))
            continue
        taught = taught_skills(guilds[guildmaster["guild"] - 1], skill_names)
        if not taught:
            continue
        if problems:
            unusable.append((guildmaster, problems))
            continue
        rows.append((guildmaster, row_cells(class_name, taught)))
    rows.sort(key=lambda row: (describe_taught_races(row[0]["teaches_races"]), row[0]["short_desc"]))
    if class_name == "warrior":
        lines = ["| Trainer | Where | Teaches | Weapon skills | Other warrior skills (cap %) |",
                 "| --- | --- | --- | --- | --- |"]
    else:
        lines = [f"| Trainer | Where | Teaches | {class_name.capitalize()} skills (cap %) |", "| --- | --- | --- | --- |"]
    for guildmaster, cells in rows:
        lines.append(f"| {guildmaster['short_desc']} (mob {guildmaster['vnum']}) | {describe_locations(guildmaster)} | "
                     f"{describe_taught_races(guildmaster['teaches_races'])} | " + " | ".join(cells) + " |")
    lines.append("")
    for guildmaster, problems in unusable:
        lines.append(f"UNUSABLE: {guildmaster.get('short_desc')} (mob {guildmaster['vnum']}): {'; '.join(problems)}")
    return "\n".join(lines)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("class_name", choices=TABLE_CLASSES, help="the class to tabulate")
    parser.add_argument("--repo", type=Path, default=rots_sources.default_repo(), help="repository root")
    parser.add_argument("--data", type=Path, help="skill_data.json to read instead of the repository")
    parser.add_argument("--out", type=Path, help="write the table to this file instead of standard output")
    return parser


def main(argv: Optional[Sequence[str]] = None) -> int:
    arguments = build_parser().parse_args(argv)
    try:
        data = class_packs.load_data(arguments.repo, arguments.data)
    except (OSError, ValueError) as error:
        print(f"trainer_tables: {error}", file=sys.stderr)
        return 1
    table = build_table(arguments.class_name, data)
    if arguments.out is None:
        print(table)
    else:
        arguments.out.write_text(table + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
