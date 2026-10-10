#!/usr/bin/env python3
"""Write one data pack per class: every skill's row and every trainer that teaches it.

    class_packs.py [--repo DIR] [--out DIR]                    read the repository directly
    class_packs.py --data skill_data.json [--out DIR]          read skill_data.py's output instead

Writes pack_<class>.md and pack_<class>.json for warrior, ranger, mystic, mage and general. A trainer line
gives the guild's cap for the skill, the mob, its race, the races it teaches, where zone resets load it, and
the problems that stop it teaching (missing mob, no SPEC flag, program number outside the guild table,
will_teach 0, never loaded).
"""

import argparse
import json
import sys
from pathlib import Path
from typing import Any, Dict, List, Optional, Sequence

sys.path.insert(0, str(Path(__file__).resolve().parent))
import rots_sources  # noqa: E402
import skill_data  # noqa: E402
from rots_sources import Record  # noqa: E402

CLASS_NAMES = ["warrior", "ranger", "mystic", "mage", "general"]
# Rows no player trains: internal effects, placeholders and the unnamed row (except the trap row below).
INTERNAL_SKILL_NAMES = {"asphyxiation", "Power of Arda", "activity", "rage", "anger", "Fame War", "nothing",
                        "trash", ""}
# General-type skills documented under a class.
CLASS_OVERRIDES = {"stomp": "warrior", "recruit": "warrior"}
# skills[151] has an empty name and type PROF_GENERAL but is the ranger trap skill (SKILL_TRAP); packs file it
# under ranger and keep its source type.
TRAP_ROW_INDEX = 151
TRAP_ROW_NAME = "trap (row 151 has an empty name)"
PACK_INTRODUCTION = ("Every row comes from src/consts.cpp (skills[], guildmasters[]), src/spec_ass.cpp (guildmaster "
                     "vnums), lib/world/mob/*.mob (guild number = program field, race, will_teach race mask), "
                     "lib/world/zon/*.zon (M reset lines) and lib/world/wld/*.wld (room names). A trainer is usable "
                     "only when it has no listed problem.")


def has_valid_guild(guildmaster: Record, guild_count: int) -> bool:
    """Whether the mob's program number selects a row of guildmasters[]."""
    program = guildmaster.get("guild")
    return bool(program) and 1 <= program <= guild_count


def trainer_problems(guildmaster: Record, guild_count: int, brief: bool = False) -> List[str]:
    """Why a guildmaster cannot teach, in long (pack) or brief (table) wording; empty when it can."""
    problems = []
    if not guildmaster.get("found_in_world"):
        problems.append("not in world files" if brief else "mob not in world files")
    # The guild special is only called for mobs with MOB_SPEC (act bit 0, src/structs.h).
    if not guildmaster.get("spec_flag"):
        problems.append("no SPEC flag" if brief else "no SPEC flag, so the guild special never runs")
    if not has_valid_guild(guildmaster, guild_count):
        problems.append("program number is not a guild" if brief
                        else "program number %r is not a guild" % guildmaster.get("guild"))
    if guildmaster.get("will_teach") in (0, None):
        problems.append("teaches no race (will_teach 0)" if brief
                        else "will_teach race mask is 0, so it teaches nobody")
    if not guildmaster.get("loads"):
        problems.append("never loaded by a zone reset" if brief else "no zone reset loads it")
    return problems


def describe_load(load: Record) -> str:
    """The room a reset loads the mob into, its zone, and the reset file (naming its zone when it differs)."""
    loaded_by = load.get("loaded_by", load["zone"])
    reset_file = load["zone_file"] if loaded_by == load["zone"] else f"loaded by {loaded_by}, {load['zone_file']}"
    return f"room {load['room']} '{load['room_name']}' ({load['zone']}, {reset_file})"


def skill_trainers(skill_index: int, guilds: List[Record], trainers_by_guild: Dict[Any, List[Record]]) -> List[Record]:
    """Every guild that teaches the skill, once per guildmaster mob using it (or once with mob None)."""
    rows: List[Record] = []
    for guild in guilds:
        cap = rots_sources.guild_cap(guild, skill_index)
        if cap <= 0:
            continue
        mobs = trainers_by_guild.get(guild["number"], [])
        if not mobs:
            rows.append({"guild": guild["number"], "guild_label": guild["label"], "pct": cap, "mob": None,
                         "problems": ["no guildmaster mob uses this guild"]})
        for guildmaster in mobs:
            rows.append({"guild": guild["number"], "guild_label": guild["label"], "pct": cap,
                         "mob": {"vnum": guildmaster["vnum"], "name": guildmaster.get("short_desc"),
                                 "race": guildmaster.get("race_name"),
                                 "teaches_races": guildmaster.get("teaches_races"),
                                 "loads": [describe_load(load) for load in guildmaster.get("loads", [])]},
                         "problems": trainer_problems(guildmaster, len(guilds))})
    return rows


def build_packs(data: Dict[str, Any]) -> Dict[str, List[Record]]:
    """Each class's skills in table order, each with its trainers."""
    guilds = data["guilds"]
    trainers_by_guild: Dict[Any, List[Record]] = {}
    for guildmaster in data["guildmasters"]:
        trainers_by_guild.setdefault(guildmaster.get("guild"), []).append(guildmaster)
    packs: Dict[str, List[Record]] = {class_name: [] for class_name in CLASS_NAMES}
    for skill in data["skills"]:
        row = dict(skill)
        if skill["index"] == TRAP_ROW_INDEX:
            row["name"] = TRAP_ROW_NAME
            class_name = "ranger"
        elif skill["name"] in INTERNAL_SKILL_NAMES:
            continue
        else:
            class_name = CLASS_OVERRIDES.get(row["name"], row["type"])
        row["trainers"] = skill_trainers(skill["index"], guilds, trainers_by_guild)
        packs.setdefault(class_name, []).append(row)
    return packs


def format_trainer(trainer: Record) -> str:
    if trainer["mob"] is None:
        return (f"- guild {trainer['guild']} '{trainer['guild_label']}' teaches to {trainer['pct']}%: "
                f"no guildmaster mob uses this guild")
    mob = trainer["mob"]
    problems = ("; PROBLEMS: " + "; ".join(trainer["problems"])) if trainer["problems"] else ""
    return (f"- {trainer['pct']}%: {mob['name']} (mob {mob['vnum']}, {mob['race']}), guild {trainer['guild']} "
            f"'{trainer['guild_label']}', teaches {mob['teaches_races']}, "
            f"loads in {'; '.join(mob['loads']) or 'nowhere'}{problems}")


def format_pack(class_name: str, rows: List[Record]) -> str:
    """The markdown pack for one class."""
    lines = [f"# Data pack: {class_name} ({len(rows)} skills)", "", PACK_INTRODUCTION, ""]
    for skill in rows:
        lines.append(f"## {skill['index']} {skill['name']}  [{', '.join(skill['defines'])}]")
        lines.append(f"type {skill['type']}, level {skill['level']}, min_usesmana {skill['min_usesmana']}, "
                     f"beats {skill['beats']}, targets {skill['targets']}, min position {skill['minimum_position']}, "
                     f"learn_diff {skill['learn_diff']}, learn_type {skill['learn_type']}, "
                     f"spec {skill['skill_spec']}, spell function {skill['spell_pointer']}, "
                     f"row at src/consts.cpp:{skill['source_line']}")
        if not skill["trainers"]:
            lines.append("- trainers: NONE (no guild row teaches it)")
        lines.extend(format_trainer(trainer) for trainer in skill["trainers"])
        lines.append("")
    return "\n".join(lines)


def load_data(repo: Path, data_path: Optional[Path]) -> Dict[str, Any]:
    """skill_data.py's JSON when a path is given, otherwise the same data read from the repository."""
    if data_path is not None:
        with open(data_path, encoding="utf-8") as handle:
            return json.load(handle)
    return skill_data.build_skill_data(repo)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--repo", type=Path, default=rots_sources.default_repo(), help="repository root")
    parser.add_argument("--data", type=Path, help="skill_data.json to read instead of the repository")
    parser.add_argument("--out", type=Path, default=Path("."), help="directory for the pack files")
    return parser


def main(argv: Optional[Sequence[str]] = None) -> int:
    arguments = build_parser().parse_args(argv)
    try:
        data = load_data(arguments.repo, arguments.data)
    except (OSError, ValueError) as error:
        print(f"class_packs: {error}", file=sys.stderr)
        return 1
    arguments.out.mkdir(parents=True, exist_ok=True)
    for class_name, rows in build_packs(data).items():
        with open(arguments.out / f"pack_{class_name}.json", "w", encoding="utf-8") as handle:
            json.dump(rows, handle, indent=1)
        (arguments.out / f"pack_{class_name}.md").write_text(format_pack(class_name, rows), encoding="utf-8")
        print(class_name, len(rows))
    return 0


if __name__ == "__main__":
    sys.exit(main())
