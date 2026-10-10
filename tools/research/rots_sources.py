#!/usr/bin/env python3
"""Shared parsing for the research tools: C source tables and the world files they refer to.

    import rots_sources                      from a tool in tools/research/
    rots_sources.default_repo()              the repository root two levels above this file

Nothing here writes files. Every reader takes the text it parses, so the tests can feed small fixtures; the
helpers that read the repository take its root as a Path. Files are read as latin-1 because the world files
are not UTF-8, and room files end their lines with "\\n\\r".
"""

import re
from pathlib import Path
from typing import Any, Dict, List, Optional, Sequence, Set, Tuple

Record = Dict[str, Any]

PROFESSION_NAMES = {"PROF_GENERAL": "general", "PROF_MAGE": "mage", "PROF_CLERIC": "mystic",
                    "PROF_RANGER": "ranger", "PROF_WARRIOR": "warrior"}
# Indexed by the PLRSPEC_* number.
SPECIALIZATION_NAMES = ["nothing", "fire", "cold", "regeneration", "protection", "animals", "stealth",
                        "wild fighting", "teleportation", "illusion", "lightning", "guardian",
                        "heavy fighting", "light fighting", "defending", "archery", "darkness", "arcane",
                        "weapon mastery", "battle magic"]
SPECIALIZATION_IDS = {"PLRSPEC_NONE": 0, "PLRSPEC_FIRE": 1, "PLRSPEC_COLD": 2, "PLRSPEC_REGN": 3,
                      "PLRSPEC_PROT": 4, "PLRSPEC_PETS": 5, "PLRSPEC_STLH": 6, "PLRSPEC_WILD": 7,
                      "PLRSPEC_TELE": 8, "PLRSPEC_ILLU": 9, "PLRSPEC_LGHT": 10, "PLRSPEC_GRDN": 11,
                      "PLRSPEC_HFGT": 12, "PLRSPEC_LFGT": 13, "PLRSPEC_DFND": 14, "PLRSPEC_ARCH": 15,
                      "PLRSPEC_DARK": 16, "PLRSPEC_ARCANE": 17, "PLRSPEC_WMSR": 18, "PLRSPEC_BTLEMS": 19}
# Indexed by the RACE_* number; bit N of a will_teach or rp_flag mask is race N.
RACE_NAMES = ["God", "Human", "Dwarf", "Wood Elf", "Hobbit", "High Elf", "Beorning", "UNDEF7",
              "UNDEF8", "UNDEF9", "UNDEF10", "Uruk-Hai", "Harad", "Orc", "Easterling",
              "Uruk-Lhuth", "Undead", "Olog-Hai", "Haradrim", "UNDEF19", "Troll"]
# The 40 integers after a mob's flags line, in the order src/db.cpp:1953-2033 reads them.
MOB_FIELDS = ["level", "ob", "parry", "dodge", "hit", "max_hit", "damage", "ene_regen", "gold",
              "exp", "owner", "position", "default_pos", "sex", "race", "pref", "weight",
              "height", "prog", "butcher", "corpse", "rp_flag", "prof", "mana", "move",
              "bodytype", "save", "str", "int", "wil", "dex", "con", "lea", "language",
              "perception", "resistance", "vulnerability", "script", "spirit", "will_teach"]
SKILLS_TABLE_START = "struct skill_data skills[MAX_SKILLS] = {"
GUILD_TABLE_START = "struct skill_teach_data guildmasters[] = {"
SKILL_FIELDS_AFTER_NAME = 12
OPENING_BRACKETS = "([{"
CLOSING_BRACKETS = ")]}"


def default_repo() -> Path:
    """The repository root, two levels above tools/research/."""
    return Path(__file__).resolve().parent.parent.parent


def read_source(repo: Path, relative_path: str) -> str:
    """One repository file as text, decoded as latin-1. Universal newlines apply, so the "\\n\\r" line endings of
    the world files read as "\\n\\n"."""
    return (repo / relative_path).read_text(encoding="latin-1")


def line_number_at(text: str, offset: int) -> int:
    """The 1-based line number of `offset` in `text`."""
    return text.count("\n", 0, offset) + 1


def blank_c_comments(text: str) -> str:
    """`text` with every C and C++ comment replaced by spaces, keeping newlines, so offsets and line numbers
    still match the original. Comment markers inside string and character literals are left alone."""
    output = list(text)
    position = 0
    length = len(text)
    while position < length:
        character = text[position]
        if character in "\"'":
            position = skip_literal(text, position)
            continue
        if text.startswith("//", position):
            line_end = text.find("\n", position)
            comment_end = length if line_end < 0 else line_end
        elif text.startswith("/*", position):
            close = text.find("*/", position + 2)
            comment_end = length if close < 0 else close + 2
        else:
            position += 1
            continue
        for blank_index in range(position, comment_end):
            if output[blank_index] != "\n":
                output[blank_index] = " "
        position = comment_end
    return "".join(output)


def skip_literal(text: str, opening_index: int) -> int:
    """The offset just past the string or character literal that starts at `opening_index`."""
    quote = text[opening_index]
    position = opening_index + 1
    while position < len(text):
        character = text[position]
        if character == "\\":
            position += 2
            continue
        if character == quote or character == "\n":
            return position + 1
        position += 1
    return position


def find_closing_parenthesis(text: str, opening_index: int) -> int:
    """The offset of the ")" that balances the "(" at `opening_index`; -1 when the text ends first."""
    depth = 0
    position = opening_index
    while position < len(text):
        character = text[position]
        if character in "\"'":
            position = skip_literal(text, position)
            continue
        if character == "(":
            depth += 1
        elif character == ")":
            depth -= 1
            if depth == 0:
                return position
        position += 1
    return -1


def split_arguments(argument_text: str) -> List[str]:
    """Macro or call arguments split at top-level commas, each with its whitespace collapsed to single
    spaces. Commas inside brackets or literals do not split."""
    arguments: List[str] = []
    depth = 0
    current_start = 0
    position = 0
    while position < len(argument_text):
        character = argument_text[position]
        if character in "\"'":
            position = skip_literal(argument_text, position)
            continue
        if character in OPENING_BRACKETS:
            depth += 1
        elif character in CLOSING_BRACKETS:
            depth -= 1
        elif character == "," and depth == 0:
            arguments.append(argument_text[current_start:position])
            current_start = position + 1
        position += 1
    arguments.append(argument_text[current_start:])
    return [re.sub(r"\s+", " ", argument).strip() for argument in arguments]


def find_macro_calls(text: str, macro_names: Sequence[str]) -> List[Tuple[str, int, List[str]]]:
    """Every call of one of `macro_names` in comment-free code as (name, offset, arguments). Names are
    matched whole, so WAIT_STATE does not match inside WAIT_STATE_FULL; #define lines are skipped."""
    code = blank_c_comments(text)
    alternatives = "|".join(sorted((re.escape(name) for name in macro_names), key=len, reverse=True))
    calls = []
    for match in re.finditer(r"\b(%s)\s*\(" % alternatives, code):
        line_start = code.rfind("\n", 0, match.start()) + 1
        if code[line_start:match.start()].strip().startswith("#"):
            continue
        close = find_closing_parenthesis(code, match.end() - 1)
        if close < 0:
            continue
        calls.append((match.group(1), match.start(), split_arguments(code[match.end():close])))
    return calls


def c_divide(numerator: int, denominator: int) -> int:
    """C integer division, which truncates toward zero; Python's // rounds toward negative infinity."""
    quotient = abs(numerator) // abs(denominator)
    return quotient if (numerator >= 0) == (denominator >= 0) else -quotient


def parse_skills(consts_text: str) -> List[Record]:
    """The rows of skills[] in src/consts.cpp, in table order, each with the source line of its row.

    A row is { "name", type, level, spell pointer, minimum position, min_usesmana, beats, targets,
    learn_diff, learn_type, is_fast, skill_spec, resist }. Row 151 has an empty name."""
    code = blank_c_comments(consts_text)
    start = code.index(SKILLS_TABLE_START)
    end = code.index("};", start)
    rows: List[Record] = []
    for match in re.finditer(r"\{\s*\"([^\"]*)\"(.*?)\}", code[start:end], re.S):
        fields = [field for field in split_arguments(match.group(2)) if field]
        if len(fields) != SKILL_FIELDS_AFTER_NAME:
            raise ValueError(f"skills row {match.group(1)!r}: expected {SKILL_FIELDS_AFTER_NAME} fields after "
                             f"the name, got {fields}")
        rows.append({
            "index": len(rows), "name": match.group(1), "type": PROFESSION_NAMES.get(fields[0], fields[0]),
            "level": int(fields[1]), "spell_pointer": fields[2], "minimum_position": fields[3],
            "min_usesmana": int(fields[4]), "beats": int(fields[5]), "targets": fields[6],
            "learn_diff": int(fields[7]), "learn_type": int(fields[8]), "is_fast": int(fields[9]),
            "skill_spec": SPECIALIZATION_NAMES[SPECIALIZATION_IDS[fields[10]]], "resist": fields[11],
            "source_line": line_number_at(consts_text, start + match.start())})
    return rows


def parse_skill_defines(header_texts: Sequence[str]) -> Dict[int, List[str]]:
    """SKILL_, SPELL_ and LANG_ #define names by value, in the order the headers list them."""
    pattern = r"#define\s+((?:SKILL|SPELL|LANG)_[A-Z0-9_]+)\s+(\d+)\b"
    names: Dict[int, List[str]] = {}
    for header_text in header_texts:
        for match in re.finditer(pattern, blank_c_comments(header_text)):
            names.setdefault(int(match.group(2)), []).append(match.group(1))
    return names


def parse_guilds(consts_text: str) -> List[Record]:
    """The entries of guildmasters[] in src/consts.cpp. Guild number = position counted from 1, which is
    what a guildmaster mob's program field selects. `knowledge` is the cap per skill number; an array
    shorter than MAX_SKILLS means 0 for the missing skills."""
    code = blank_c_comments(consts_text)
    start = code.index(GUILD_TABLE_START)
    end = code.index("\n};", start)
    first_line = line_number_at(consts_text, start)
    raw_lines = consts_text[start:end].split("\n")
    code_lines = code[start:end].split("\n")
    # Each entry opens with a line indented exactly four spaces and "{", as the table is formatted.
    entry_starts = [index for index, line in enumerate(raw_lines) if re.match(r"^    \{", line)]
    guilds: List[Record] = []
    for entry_position, first in enumerate(entry_starts):
        last = entry_starts[entry_position + 1] if entry_position + 1 < len(entry_starts) else len(raw_lines)
        raw_entry = "\n".join(raw_lines[first:last])
        arrays = re.findall(r"\{([-\d,\s]+)\}", "\n".join(code_lines[first:last]))
        if len(arrays) != 1:
            raise ValueError(f"guild entry at line {first_line + first}: expected one number array, "
                             f"got {len(arrays)}")
        guilds.append({"number": len(guilds) + 1, "label": guild_label(raw_entry),
                       "knowledge": [int(value) for value in arrays[0].split(",") if value.strip()],
                       "source_line": first_line + first})
    return guilds


def guild_label(raw_entry: str) -> str:
    """The comment that opens a guild entry, or its first string when it has none."""
    comment = re.match(r"\s*\{\s*(?://\s*([^\n]*)|/\*(.*?)\*/)", raw_entry, re.S)
    if comment and (comment.group(1) or comment.group(2)):
        return (comment.group(1) or comment.group(2)).strip()
    return "(no label) " + re.search(r"\"([^\"]*)\"", raw_entry).group(1)


def guild_cap(guild: Record, skill_index: int) -> int:
    """The guild's cap for one skill; 0 past the end of its array, as the zero-filled C array reads."""
    knowledge = guild["knowledge"]
    return knowledge[skill_index] if skill_index < len(knowledge) else 0


def parse_guildmaster_vnums(spec_ass_text: str) -> List[Record]:
    """Every ASSIGNMOB(<vnum>, guild) in src/spec_ass.cpp, with its trailing comment and line."""
    code_lines = blank_c_comments(spec_ass_text).split("\n")
    entries = []
    for line_number, raw_line in enumerate(spec_ass_text.split("\n"), 1):
        if not re.match(r"\s*ASSIGNMOB\(\d+,\s*guild\);", code_lines[line_number - 1]):
            continue
        match = re.match(r"\s*ASSIGNMOB\((\d+),\s*guild\);\s*(?://\s*(.*))?", raw_line)
        entries.append({"vnum": int(match.group(1)), "comment": (match.group(2) or "").strip(),
                        "spec_ass_line": line_number})
    return entries


def parse_mob_spec_shift(structs_text: str) -> Optional[int]:
    """The bit number of MOB_SPEC in src/structs.h, or None when the define is not found."""
    match = re.search(r"#define\s+MOB_SPEC\s+\(1\s*<<\s*(\d+)\)", structs_text)
    return int(match.group(1)) if match else None


def parse_mob_file(text: str, file_label: str, spec_shift: int = 0,
                   wanted: Optional[Set[int]] = None) -> Dict[int, Record]:
    """The mob records of one .mob file by vnum, limited to `wanted` when given.

    Record layout: "#<vnum>", four "~"-terminated strings (keywords, short, long, description), the flags line
    "<act> <affected> <alignment> <N|M>", for N two death-cry strings, then 40 integers (MOB_FIELDS)."""
    mobs: Dict[int, Record] = {}
    for match in re.finditer(r"^#(\d+)\n(.*?)(?=^#\d+\n|^\$)", text, re.S | re.M):
        vnum = int(match.group(1))
        if wanted is not None and vnum not in wanted:
            continue
        record = parse_mob_record(match.group(2), spec_shift)
        if record is None:
            continue
        record.update({"vnum": vnum, "file": file_label})
        mobs[vnum] = record
    return mobs


def parse_mob_record(body: str, spec_shift: int) -> Optional[Record]:
    """One mob record after its "#<vnum>" line; None when it is not in the expected layout."""
    strings = body.split("~", 4)
    if len(strings) < 5:
        return None
    rest = strings[4]
    flags = re.match(r"\s*(-?\d+)\s+(-?\d+)\s+(-?\d+)\s+([NM])", rest)
    if not flags:
        return None
    rest = rest[flags.end():]
    if flags.group(4) == "N":
        death_cries = rest.split("~", 2)
        if len(death_cries) < 3:
            return None
        rest = death_cries[2]
    numbers = leading_integers(rest.split(), len(MOB_FIELDS))
    record: Record = dict(zip(MOB_FIELDS, numbers))
    record["token_count"] = len(numbers)
    # The loader starts will_teach at 0 and keeps it when the record stops early (src/db.cpp:2016-2033).
    record.setdefault("will_teach", 0)
    act_flags = int(flags.group(1))
    record.update({"short_desc": strings[1].strip(), "act_flags": act_flags,
                   "spec_flag": bool(act_flags & (1 << spec_shift))})
    return record


def leading_integers(tokens: Sequence[str], limit: int) -> List[int]:
    """The integer tokens at the start of `tokens`, at most `limit`; fscanf("%d") stops at the first other."""
    numbers = []
    for token in tokens[:limit]:
        if not re.fullmatch(r"-?\d+", token):
            break
        numbers.append(int(token))
    return numbers


def world_files(repo: Path, subdirectory: str, pattern: str) -> List[Path]:
    """The world files in lib/world/<subdirectory> matching `pattern`, in zone-number order (15.zon before
    102.zon), so loads and listings come out in a stable order."""
    paths = (repo / "lib/world" / subdirectory).glob(pattern)
    return sorted(paths, key=lambda path: (not path.stem.isdigit(), int(path.stem) if path.stem.isdigit() else 0,
                                           path.name))


def parse_world_mobs(repo: Path, spec_shift: int = 0, wanted: Optional[Set[int]] = None) -> Dict[int, Record]:
    """Every mob in lib/world/mob/*.mob by vnum (only `wanted` ones when given)."""
    mobs: Dict[int, Record] = {}
    for path in world_files(repo, "mob", "*.mob"):
        mobs.update(parse_mob_file(path.read_text(encoding="latin-1"), str(path.relative_to(repo)),
                                   spec_shift, wanted))
    return mobs


def parse_room_names(text: str) -> Dict[int, str]:
    """Room names of one .wld file by vnum: the "~"-terminated string after each "#<vnum>" line
    (src/db.cpp:1373-1395). Room files end lines with "\\n\\r", so a raw "\\r" may start any line; text read
    with Python's universal newlines has "\\n\\n" there instead, and both forms parse."""
    return {int(match.group(1)): match.group(2).strip()
            for match in re.finditer(r"^\r?#(\d+)[ \t]*\r?\n\r?([^~]*)~", text, re.M)}


def parse_zone(text: str, zone_file: str) -> Tuple[str, List[Record]]:
    """A .zon file's name and its mob loads. An "M <if_flag> <mob vnum> <room vnum> ..." line loads the
    mob into the room (src/zone.cpp:117-135, 923-930)."""
    header = re.match(r"#(\d+)[ \t]+([^~\n]*)~", text)
    zone_name = header.group(2).strip() if header else zone_file
    loads = []
    for line in text.split("\n"):
        match = re.match(r"M\s+-?\d+\s+(\d+)\s+(\d+)", line)
        if match:
            loads.append({"mob": int(match.group(1)), "room": int(match.group(2)), "zone_file": zone_file,
                          "zone": zone_name})
    return zone_name, loads


def parse_world_rooms_and_loads(repo: Path) -> Tuple[Dict[int, str], Dict[int, List[Record]]]:
    """Room names by vnum, and every zone M load by mob vnum as {room, zone, zone_file, loaded_by}.

    `zone` names the zone the room belongs to, taken from the .wld file that holds the room; that matches the
    loader's zone ranges (src/db.cpp:1398-1413) because each zone's rooms sit in its own file. `loaded_by` names
    the zone whose reset file (`zone_file`) has the M line, which may load a mob into another zone's room."""
    zones = [(path.stem, *parse_zone(path.read_text(encoding="latin-1"), path.name))
             for path in world_files(repo, "zon", "*.zon")]
    zone_names = {stem: zone_name for stem, zone_name, _zone_loads in zones}
    rooms: Dict[int, str] = {}
    room_zones: Dict[int, str] = {}
    for path in world_files(repo, "wld", "*.wld"):
        for vnum, room_name in parse_room_names(path.read_text(encoding="latin-1")).items():
            rooms[vnum] = room_name
            room_zones[vnum] = zone_names.get(path.stem, "zone " + path.stem)
    loads: Dict[int, List[Record]] = {}
    for _stem, zone_name, zone_loads in zones:
        for load in zone_loads:
            load["loaded_by"] = zone_name
            load["zone"] = room_zones.get(load["room"], zone_name)
            loads.setdefault(load.pop("mob"), []).append(load)
    return rooms, loads


def race_names_in_mask(mask: int) -> List[str]:
    """The race names whose bits are set in a race mask."""
    return [RACE_NAMES[bit] for bit in range(len(RACE_NAMES)) if mask & (1 << bit)]


def function_end(lines: Sequence[str], start_index: int) -> int:
    """The index of the first line after `start_index` that is exactly "}", which closes a function body in
    this codebase's formatting; len(lines) when there is none."""
    index = start_index + 1
    while index < len(lines) and lines[index] != "}":
        index += 1
    return index


def find_handler_definitions(lines: Sequence[str], handler: str) -> List[int]:
    """Indexes of the lines that start the definition (not a declaration) of do_<handler>, written either
    as ACMD(do_<handler>) or as void do_<handler>(char_data..."""
    pattern = re.compile(r"^(ACMD\(do_%s\)|void do_%s\(char_data)" % (re.escape(handler), re.escape(handler)))
    return [index for index, line in enumerate(lines)
            if pattern.match(line) and not line.rstrip().endswith(";")]
