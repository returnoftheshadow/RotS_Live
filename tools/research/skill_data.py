#!/usr/bin/env python3
"""Extract the skills table, the guild teaching table and the guildmaster mobs as JSON.

    skill_data.py [--repo DIR] [--out DIR]              write skill_data.json
    skill_data.py [--repo DIR] [--out DIR] --summary    also write skill_data.txt, a readable summary

Sources: src/consts.cpp (skills[], guildmasters[]), src/spells.h and src/structs.h (SKILL_/SPELL_/LANG_
defines, MOB_SPEC), src/spec_ass.cpp (ASSIGNMOB(<vnum>, guild)), lib/world/mob/*.mob, lib/world/zon/*.zon
(M lines) and lib/world/wld/*.wld (room names). The repository is only read.
"""

import argparse
import json
import sys
from pathlib import Path
from typing import Any, Dict, List, Optional, Sequence

sys.path.insert(0, str(Path(__file__).resolve().parent))
import rots_sources  # noqa: E402
from rots_sources import Record  # noqa: E402

JSON_NAME = "skill_data.json"
SUMMARY_NAME = "skill_data.txt"
# The mob fields copied onto each guildmaster entry.
GUILDMASTER_MOB_FIELDS = ("short_desc", "prog", "race", "spec_flag", "file", "will_teach", "rp_flag", "level")


def describe_guildmaster(entry: Record, mob: Optional[Record], guilds: List[Record],
                         rooms: Dict[int, str], loads: Dict[int, List[Record]]) -> None:
    """Adds the mob's fields, its guild and its zone loads to one ASSIGNMOB entry, in place."""
    entry["found_in_world"] = mob is not None
    if mob:
        entry.update({key: mob.get(key) for key in GUILDMASTER_MOB_FIELDS})
        entry["token_count"] = mob.get("token_count")
        race = mob.get("race")
        entry["race_name"] = (rots_sources.RACE_NAMES[race]
                              if race is not None and 0 <= race < len(rots_sources.RACE_NAMES) else race)
        entry["teaches_races"] = rots_sources.race_names_in_mask(mob["will_teach"])
        entry["rp_races"] = rots_sources.race_names_in_mask(mob["rp_flag"]) if mob.get("rp_flag") else "any"
        # The guild special uses the mob's program field as a 1-based guildmasters[] row.
        program = mob.get("prog")
        entry["guild"] = program
        entry["guild_label"] = (guilds[program - 1]["label"]
                                if program and 1 <= program <= len(guilds) else "OUT OF RANGE")
    entry["loads"] = [dict(load, room_name=rooms.get(load["room"], "?")) for load in loads.get(entry["vnum"], [])]


def build_skill_data(repo: Path) -> Dict[str, Any]:
    """Skills, guilds and guildmasters (joined with their mobs, guilds and load rooms) read from `repo`."""
    consts_text = rots_sources.read_source(repo, "src/consts.cpp")
    structs_text = rots_sources.read_source(repo, "src/structs.h")
    skills = rots_sources.parse_skills(consts_text)
    defines = rots_sources.parse_skill_defines([rots_sources.read_source(repo, "src/spells.h"), structs_text])
    for skill in skills:
        skill["defines"] = defines.get(skill["index"], [])
    guilds = rots_sources.parse_guilds(consts_text)
    guildmasters = rots_sources.parse_guildmaster_vnums(rots_sources.read_source(repo, "src/spec_ass.cpp"))
    spec_shift = rots_sources.parse_mob_spec_shift(structs_text)
    mobs = rots_sources.parse_world_mobs(repo, spec_shift if spec_shift is not None else 0,
                                         {entry["vnum"] for entry in guildmasters})
    rooms, loads = rots_sources.parse_world_rooms_and_loads(repo)
    for entry in guildmasters:
        describe_guildmaster(entry, mobs.get(entry["vnum"]), guilds, rooms, loads)
    return {"skills": skills, "guilds": guilds, "guildmasters": guildmasters, "mob_spec_shift": spec_shift}


def skill_label(skills: List[Record], skill_index: int) -> str:
    """A skill's name, or #<index> past the end of the table."""
    return skills[skill_index]["name"] if skill_index < len(skills) else "#" + str(skill_index)


def format_summary(data: Dict[str, Any]) -> List[str]:
    """The readable summary: counts, one line per skill, per guild and per guildmaster."""
    skills = data["skills"]
    lines = [f"skills rows: {len(skills)}; guild rows: {len(data['guilds'])}; "
             f"guildmaster vnums: {len(data['guildmasters'])}", "",
             "== skills (index name | type lvl | mana beats targets | learn_diff learn_type | spec | defines | line)"]
    for skill in skills:
        lines.append(f"{skill['index']:3d} {skill['name']:<22} | {skill['type']:<8} {skill['level']:2d} | "
                     f"{skill['min_usesmana']:3d} {skill['beats']:3d} {skill['targets']:<14} | "
                     f"{skill['learn_diff']:3d} {skill['learn_type']:3d} | {skill['skill_spec']:<14} | "
                     f"{','.join(skill['defines'])} | consts.cpp:{skill['source_line']}")
    lines += ["", "== guilds (number label: skill=pct ...)"]
    for guild in data["guilds"]:
        taught = [f"{skill_label(skills, skill_index)}={cap}"
                  for skill_index, cap in enumerate(guild["knowledge"]) if cap > 0]
        lines.append(f"{guild['number']:2d} {guild['label']}: {len(guild['knowledge'])} values; " + "; ".join(taught))
    lines += ["", "== guildmasters (vnum | short | guild | race | spec | teaches races | rp | loads)"]
    for entry in data["guildmasters"]:
        if not entry["found_in_world"]:
            lines.append(f"{entry['vnum']} NOT IN WORLD FILES ({entry['comment']})")
            continue
        load_text = "; ".join(f"{load['room']} {load['room_name']} [{load['zone']}]"
                              for load in entry["loads"]) or "never loaded"
        lines.append(f"{entry['vnum']} | {entry['short_desc']} | guild {entry['guild']} {entry['guild_label']} | "
                     f"{entry['race_name']} | spec={entry['spec_flag']} | teaches {entry['teaches_races']} | "
                     f"rp {entry['rp_races']} | {load_text}")
    return lines


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--repo", type=Path, default=rots_sources.default_repo(), help="repository root")
    parser.add_argument("--out", type=Path, default=Path("."), help="directory for the output files")
    parser.add_argument("--summary", action="store_true", help=f"also write {SUMMARY_NAME}")
    return parser


def main(argv: Optional[Sequence[str]] = None) -> int:
    arguments = build_parser().parse_args(argv)
    try:
        data = build_skill_data(arguments.repo)
    except (OSError, ValueError) as error:
        print(f"skill_data: {error}", file=sys.stderr)
        return 1
    arguments.out.mkdir(parents=True, exist_ok=True)
    with open(arguments.out / JSON_NAME, "w", encoding="utf-8") as handle:
        json.dump(data, handle, indent=1)
    summary = format_summary(data)
    if arguments.summary:
        (arguments.out / SUMMARY_NAME).write_text("\n".join(summary) + "\n", encoding="utf-8")
    print(summary[0])
    return 0


if __name__ == "__main__":
    sys.exit(main())
