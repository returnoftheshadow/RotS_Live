#!/usr/bin/env python3
"""Knowledge % after 1..N practice sessions, computed the way recalc_skills() does (src/spec_pro.cpp:120-157).

    practice_curve.py --difficulty 30 [--sessions 30]                          a warrior skill
    practice_curve.py --difficulty 10 --coefficient 800 --level 14             a skill of another profession
    practice_curve.py --difficulty 30 --weapon-skill --mastery-sessions 8      a weapon skill with weapon mastery
    practice_curve.py --skill 1 [--repo DIR] [--mastery-sessions 8]            difficulty, level, profession
                                                                               and weapon rule from skills[1]

--coefficient is the character's GET_PROF_COOF for the skill's profession (0-1000, after the race adjustment;
general skills always use 1000). Results above 100 are flagged: the formula gives 101 when the remaining
distance is 0-14, and only a negative distance is clamped to 100.
"""

import argparse
import sys
from pathlib import Path
from typing import List, Optional, Sequence

sys.path.insert(0, str(Path(__file__).resolve().parent))
import rots_sources  # noqa: E402
from rots_sources import c_divide  # noqa: E402

PRACTICE_UNITS_PER_SESSION = 20
# recalc_skills replaces a learn_diff of 0 with 10.
DEFAULT_DIFFICULTY = 10
FULL_COEFFICIENT = 1000
WEAPON_MASTERY_SKILL = 10
# Skills 0-9 except 7 (natural attacks) and 8 (two-handed) gain from weapon mastery; the loop starts at 1, so
# skill 0 is never recalculated.
WEAPON_MASTERY_TARGETS = {1, 2, 3, 4, 5, 6, 9}


def adjusted_coefficient(coefficient: int, skill_level: int) -> int:
    """The multiplier recalc_skills applies to a non-warrior skill's practice (at most 1000)."""
    adjusted = coefficient - c_divide(skill_level * (FULL_COEFFICIENT - coefficient), 30)
    if skill_level < 20:
        adjusted = c_divide(adjusted * (80 + skill_level), 100) + 200 - skill_level * 10
    return min(FULL_COEFFICIENT, adjusted)


def knowledge(sessions: int, difficulty: int, warrior: bool = True, coefficient: int = FULL_COEFFICIENT,
              skill_level: int = 0, mastery_sessions: int = 0) -> int:
    """Knowledge % of a skill after `sessions` practices, plus `mastery_sessions` practices of weapon mastery
    for a skill that gains from it (pass 0 otherwise)."""
    difficulty = difficulty or DEFAULT_DIFFICULTY
    practice_used = sessions * PRACTICE_UNITS_PER_SESSION
    # Each practice of weapon mastery adds three-eighths of a practice to each weapon skill, so three give slightly
    # more than the one the source comment states; C evaluates m * 20 * 3 / 8 left to right.
    practice_used += c_divide(mastery_sessions * PRACTICE_UNITS_PER_SESSION * 3, 8)
    if warrior:
        practice_used *= 10000
    else:
        practice_used = practice_used * adjusted_coefficient(coefficient, skill_level) * 10
    remaining = 1000 - c_divide(practice_used, difficulty * 100)
    if remaining < 0:
        return 100
    return c_divide(10000 - c_divide(remaining * remaining, 100), 99)


def curve(max_sessions: int, difficulty: int, warrior: bool, coefficient: int, skill_level: int,
          mastery_sessions: int) -> List[int]:
    """Knowledge after 1..max_sessions sessions."""
    return [knowledge(sessions, difficulty, warrior, coefficient, skill_level, mastery_sessions)
            for sessions in range(1, max_sessions + 1)]


def format_curve(values: List[int]) -> List[str]:
    lines = ["sessions  knowledge"]
    for sessions, value in enumerate(values, 1):
        lines.append(f"{sessions:8d}  {value:9d}" + ("  above 100" if value > 100 else ""))
    return lines


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--repo", type=Path, default=rots_sources.default_repo(), help="repository root")
    parser.add_argument("--skill", type=int, help="take difficulty, level and profession from skills[SKILL]")
    parser.add_argument("--difficulty", type=int, help="the skill's learn_diff (0 means 10)")
    parser.add_argument("--coefficient", type=int,
                        help="GET_PROF_COOF for a non-warrior skill (0-1000); omit for a warrior skill")
    parser.add_argument("--level", type=int, default=0, help="the skill's level, for a non-warrior skill")
    parser.add_argument("--weapon-skill", action="store_true", help="the skill gains from weapon mastery")
    parser.add_argument("--mastery-sessions", type=int, default=0, help="practices of weapon mastery")
    parser.add_argument("--sessions", type=int, default=30, help="the most sessions to show")
    parser.add_argument("--out", type=Path, help="write the curve to this file instead of standard output")
    return parser


def main(argv: Optional[Sequence[str]] = None) -> int:
    parser = build_parser()
    arguments = parser.parse_args(argv)
    difficulty = arguments.difficulty
    warrior = arguments.coefficient is None
    skill_level = arguments.level
    gains_from_mastery = arguments.weapon_skill
    if arguments.skill is not None:
        try:
            skills = rots_sources.parse_skills(rots_sources.read_source(arguments.repo, "src/consts.cpp"))
        except (OSError, ValueError) as error:
            print(f"practice_curve: {error}", file=sys.stderr)
            return 1
        if not 0 <= arguments.skill < len(skills):
            parser.error(f"--skill must be 0-{len(skills) - 1}")
        skill = skills[arguments.skill]
        difficulty = skill["learn_diff"] if difficulty is None else difficulty
        warrior = skill["type"] == "warrior"
        skill_level = skill["level"]
        gains_from_mastery = arguments.skill in WEAPON_MASTERY_TARGETS
        if not warrior and arguments.coefficient is None:
            parser.error(f"skills[{arguments.skill}] is a {skill['type']} skill: give --coefficient")
        print(f"skills[{arguments.skill}] {skill['name']!r}: {skill['type']}, level {skill_level}, "
              f"learn_diff {skill['learn_diff']}")
    if difficulty is None:
        parser.error("give --difficulty or --skill")
    if arguments.mastery_sessions and not gains_from_mastery:
        parser.error("weapon mastery only counts for skills 1-6 and 9: add --weapon-skill, or a --skill among them")
    mastery_sessions = arguments.mastery_sessions if gains_from_mastery else 0
    coefficient = FULL_COEFFICIENT if arguments.coefficient is None else arguments.coefficient
    values = curve(arguments.sessions, difficulty, warrior, coefficient, skill_level, mastery_sessions)
    lines = format_curve(values)
    if arguments.out is None:
        print("\n".join(lines))
    else:
        arguments.out.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
