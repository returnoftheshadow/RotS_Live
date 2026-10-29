"""Prints the test world as markdown tables: zones, rooms, mobs, objects and scripts, read from the
world files in index order, with flag numbers decoded.

Run from tests/integration as `python -m rots_harness.world_catalogue [--zone N] [--category C]`,
or `make world-catalogue [ZONE=N] [CATEGORY=C]` from the repository root. The per-fixture notes
behind the numbers are in world/notes/zone-<n>.md.
"""

from __future__ import annotations

import argparse
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable, Optional

from rots_harness import world_flags
from rots_harness.world_files import (
    END_OF_FILE_VNUM,
    TextCursor,
    WorldFileError,
    Zone,
    indexed_files,
    read_world_text,
    read_zones,
    zone_blocks,
)

# The committed test world, beside this package.
DEFAULT_WORLD_DIR = Path(__file__).resolve().parents[1] / "world"
# The catalogue's sections, in print order; --category picks one.
CATEGORIES = ("zones", "rooms", "mobs", "objects", "scripts")
# A mob whose type letter is one of these has the numeric lines load_mobiles reads (db.cpp).
NUMERIC_MOB_LETTERS = frozenset("MN")
# The mob type letter whose two death-cry strings follow the alignment line.
DEATH_CRY_MOB_LETTER = "N"
# The script row command type that ends a script (load_scripts, db.cpp).
SCRIPT_END_COMMAND = 999
# The act flag that makes a mob's program number name a special (MOB_SPEC, structs.h).
MOB_SPEC_FLAG = dict(world_flags.MOB_FLAGS.entries)["MOB_SPEC"]


class UnknownZone(ValueError):
    """--zone names a zone the world's zone index does not list."""


@dataclass(frozen=True)
class Exit:
    """One room exit, from its D<direction> block (setup_dir, db.cpp)."""

    # 0 north to 5 down.
    direction: int
    # The door's keyword; empty for a plain exit.
    keyword: str
    # The EX_ flags.
    flags: int
    # The room it leads to.
    to_room: int


@dataclass(frozen=True)
class Room:
    """A room's header and exits (load_rooms, db.cpp)."""

    # The room's vnum.
    vnum: int
    # The room's name.
    name: str
    # The room flags from its header.
    flags: int
    # The sector type from its header.
    sector: int
    # The exits in file order.
    exits: list[Exit] = field(default_factory=list)
    # The keywords of each extra description, in file order.
    extra_keywords: list[str] = field(default_factory=list)


@dataclass(frozen=True)
class Mob:
    """The fields of a mob the catalogue shows (load_mobiles, db.cpp)."""

    # The mob's vnum.
    vnum: int
    # The keywords players match it by.
    keywords: str
    # The short description act() shows.
    short_description: str
    # The act flags as the file holds them; the loader adds ISNPC.
    act_flags: int
    # The level; 0 for a mob without numeric lines.
    level: int
    # The program number on the weight line: a special's row when SPEC is set, else a mudlle program.
    program: int
    # The script the mob runs; 0 for none.
    script: int
    # The mob options text (read_mob_options, mob_options.cpp), lines joined; empty for none.
    options: str


@dataclass(frozen=True)
class GameObject:
    """The fields of an object the catalogue shows (load_objects, db.cpp)."""

    # The object's vnum.
    vnum: int
    # The keywords players match it by.
    keywords: str
    # The short description act() shows.
    short_description: str
    # The object type.
    item_type: int
    # The extra flags.
    extra_flags: int
    # The wear flags.
    wear_flags: int
    # (location, modifier) for each A line.
    affects: list[tuple[int, int]] = field(default_factory=list)


@dataclass(frozen=True)
class Script:
    """A script's name and the triggers among its rows (load_scripts, db.cpp)."""

    # The script's vnum.
    vnum: int
    # The script's name.
    name: str
    # The command type of each trigger row, in row order.
    triggers: list[int] = field(default_factory=list)


def read_rooms(path: Path) -> list[Room]:
    """The rooms of a .wld file; raises WorldFileError when the layout is not load_rooms'."""
    cursor = TextCursor(read_world_text(path), str(path))
    rooms = []
    while not cursor.at_end():
        vnum = record_number(cursor, path)
        if vnum is None:
            break
        name = cursor.read_string()
        cursor.read_string()  # description
        cursor.skip_whitespace()
        header = cursor.read_line().split()
        flags = int(header[1]) if len(header) > 1 else 0
        sector = int(header[2]) if len(header) > 2 else 0
        room = Room(vnum, name, flags, sector)
        while True:
            token = cursor.read_word()
            if token.startswith("D"):
                cursor.read_string()  # the exit's description
                keyword = cursor.read_string()
                exit_flags = cursor.read_int()
                cursor.read_int()  # key
                to_room = cursor.read_int()
                cursor.read_int()  # width
                room.exits.append(Exit(int(token[1:]), keyword, exit_flags, to_room))
            elif token.startswith("E"):
                room.extra_keywords.append(cursor.read_string())
                cursor.read_string()
            elif token.startswith("F"):
                cursor.skip_whitespace()
                cursor.read_line()
            elif token.startswith("S"):
                break
            else:
                raise WorldFileError(f"{path}: room #{vnum} has {token!r} where D, E, F or S belongs")
        rooms.append(room)
    return rooms


def record_number(cursor: TextCursor, path: Path) -> Optional[int]:
    """The next record's vnum, or None at the closing #99999 or $."""
    token = cursor.read_word()
    if token.startswith("$") or not token:
        return None
    if not token.startswith("#") or not token[1:].isdigit():
        raise WorldFileError(f"{path}: expected #<vnum>, found {token!r}")
    vnum = int(token[1:])
    return None if vnum >= END_OF_FILE_VNUM else vnum


def read_mobs(path: Path) -> list[Mob]:
    """The mobs of a .mob file; raises WorldFileError when the layout is not load_mobiles'."""
    cursor = TextCursor(read_world_text(path), str(path))
    mobs = []
    while not cursor.at_end():
        vnum = record_number(cursor, path)
        if vnum is None:
            break
        keywords = cursor.read_string()
        short_description = cursor.read_string()
        cursor.read_string()  # long description
        cursor.read_string()  # description
        act_flags = cursor.read_int()
        cursor.read_int()  # affected_by
        cursor.read_int()  # alignment
        letter = cursor.read_word()
        level = program = script = 0
        if letter == DEATH_CRY_MOB_LETTER:
            cursor.read_string()
            cursor.read_string()
        if letter in NUMERIC_MOB_LETTERS:
            level = cursor.read_int()
            skip_ints(cursor, 3 + 2 + 2 + 3 + 5)  # ob, parry, dodge; hit points; damage, regen; gold, exp, owner; position to pref
            skip_ints(cursor, 2)  # weight, height
            program = cursor.read_int()
            skip_ints(cursor, 3 + 4 + 1 + 6)  # butcher, corpse, rp; prof, mana, move, bodytype; saving throw; abilities
            skip_ints(cursor, 4)  # language, perception, resistance, vulnerability
            script = cursor.read_int(default=0)
            skip_ints(cursor, 2, default=0)  # spirit, will_teach
        options = ""
        if cursor.peek_character() not in ("#", "$", ""):
            options = "; ".join(line.strip() for line in cursor.read_string().splitlines() if line.strip())
        mobs.append(Mob(vnum, keywords, short_description, act_flags, level, program, script, options))
    return mobs


def skip_ints(cursor: TextCursor, count: int, default: Optional[int] = None) -> None:
    """Reads and drops count numbers; with a default, a missing number is not an error."""
    for _ in range(count):
        cursor.read_int(default=default)


def read_objects(path: Path) -> list[GameObject]:
    """The objects of a .obj file; raises WorldFileError when the layout is not load_objects'."""
    cursor = TextCursor(read_world_text(path), str(path))
    objects = []
    token = cursor.read_word()
    while token.startswith("#"):
        vnum = int(token[1:])
        if vnum >= END_OF_FILE_VNUM:
            break
        keywords = cursor.read_string()
        short_description = cursor.read_string()
        cursor.read_string()  # description
        cursor.read_string()  # action description
        item_type = cursor.read_int()
        extra_flags = cursor.read_int()
        wear_flags = cursor.read_int()
        skip_ints(cursor, 5 + 3 + 4)  # values; weight, cost, rent; level, rarity, material, script
        cursor.read_int(default=0)  # version, absent from older lines
        skip_ints(cursor, 2, default=0)  # skin base vnum and own fields, written only for skins
        game_object = GameObject(vnum, keywords, short_description, item_type, extra_flags, wear_flags)
        token = cursor.read_word()
        while token == "E":
            cursor.read_string()
            cursor.read_string()
            token = cursor.read_word()
        while token == "A":
            game_object.affects.append((cursor.read_int(), cursor.read_int()))
            token = cursor.read_word()
        objects.append(game_object)
    if token and not token.startswith(("#", "$")):
        raise WorldFileError(f"{path}: expected #<vnum>, E or A, found {token!r}")
    return objects


def read_scripts(path: Path) -> list[Script]:
    """The scripts of a .scr file; raises WorldFileError when the layout is not load_scripts'."""
    cursor = TextCursor(read_world_text(path), str(path))
    scripts = []
    trigger_types = {value for _, value in world_flags.SCRIPT_TRIGGERS.entries}
    while not cursor.at_end():
        vnum = record_number(cursor, path)
        if vnum is None:
            break
        script = Script(vnum, cursor.read_string())
        cursor.read_string()  # description
        while True:
            command = cursor.read_int()
            skip_ints(cursor, 7)
            if command == SCRIPT_END_COMMAND:
                break
            cursor.read_string()  # the row's text
            if command in trigger_types:
                script.triggers.append(command)
        scripts.append(script)
    return scripts


def cell(text: str) -> str:
    """Text made safe for one markdown table cell."""
    return text.replace("|", "\\|").replace("\n", " ") if text else "-"


def flags_cell(flags: int, table: world_flags.NameTable) -> str:
    """A flag number and its names, such as "3 (ISDOOR, CLOSED)", or "0"."""
    return f"{flags} ({', '.join(table.flag_names(flags))})" if flags else "0"


def markdown_table(title: str, headings: list[str], rows: list[list[str]]) -> str:
    """A level-2 heading and a markdown table; "none" when there are no rows."""
    lines = [f"## {title}", ""]
    if not rows:
        return "\n".join(lines + ["none", ""])
    lines.append("| " + " | ".join(headings) + " |")
    lines.append("| " + " | ".join("---" for _ in headings) + " |")
    lines.extend("| " + " | ".join(cell(value) for value in row) + " |" for row in rows)
    return "\n".join(lines + [""])


def exit_text(room_exit: Exit) -> str:
    """An exit as "north 1702", with its flags and keyword for a door."""
    text = f"{world_flags.DIRECTIONS.name_of(room_exit.direction).lower()} {room_exit.to_room}"
    details = []
    if room_exit.flags:
        details.append(f"flags {flags_cell(room_exit.flags, world_flags.EXIT_FLAGS)}")
    if room_exit.keyword:
        details.append(f"keyword {room_exit.keyword}")
    return f"{text} [{'; '.join(details)}]" if details else text


def affect_text(location: int, modifier: int) -> str:
    """An A line as "STR 2", naming the AFF_ bit an APPLY_BITVECTOR line sets."""
    text = f"{world_flags.APPLY_LOCATIONS.name_of(location)} {modifier}"
    if location == world_flags.APPLY_BITVECTOR and 0 <= modifier <= 31:
        text += f" ({', '.join(world_flags.AFFECT_FLAGS.flag_names(1 << modifier))})"
    return text


def zones_section(zones: list[Zone], blocks: dict[int, tuple[int, int]]) -> str:
    """The zones table; each zone's vnums are its own block (zone_blocks)."""
    rows = []
    for zone in zones:
        low, high = blocks[zone.number]
        resets = "; ".join(row.summary() for row in zone.resets) or "none"
        rows.append([str(zone.number), zone.name, f"({zone.map_square[0]}, {zone.map_square[1]})", f"{low}-{high}", resets])
    return markdown_table("Zones", ["Zone", "Name", "Map square", "Vnums", "Resets"], rows)


def rooms_section(rooms: list[Room]) -> str:
    """The rooms table."""
    rows = [
        [
            str(room.vnum),
            room.name,
            flags_cell(room.flags, world_flags.ROOM_FLAGS),
            f"{room.sector} ({world_flags.SECTORS.name_of(room.sector)})",
            "; ".join(exit_text(room_exit) for room_exit in room.exits) or "none",
        ]
        for room in rooms
    ]
    return markdown_table("Rooms", ["Vnum", "Name", "Flags", "Sector", "Exits"], rows)


def mobs_section(mobs: list[Mob]) -> str:
    """The mobs table; a special is shown only for a mob with the SPEC flag."""
    rows = [
        [
            str(mob.vnum),
            mob.keywords,
            mob.short_description,
            str(mob.level),
            flags_cell(mob.act_flags, world_flags.MOB_FLAGS),
            f"program {mob.program}" if mob.act_flags & MOB_SPEC_FLAG else "-",
            str(mob.script) if mob.script else "-",
            mob.options,
        ]
        for mob in mobs
    ]
    return markdown_table("Mobs", ["Vnum", "Keywords", "Short description", "Level", "Act flags", "Special", "Script", "Options"], rows)


def objects_section(objects: list[GameObject]) -> str:
    """The objects table."""
    rows = [
        [
            str(game_object.vnum),
            game_object.keywords,
            game_object.short_description,
            f"{game_object.item_type} ({world_flags.ITEM_TYPES.name_of(game_object.item_type)})",
            flags_cell(game_object.extra_flags, world_flags.ITEM_EXTRA_FLAGS),
            flags_cell(game_object.wear_flags, world_flags.ITEM_WEAR_FLAGS),
            "; ".join(affect_text(location, modifier) for location, modifier in game_object.affects),
        ]
        for game_object in objects
    ]
    return markdown_table("Objects", ["Vnum", "Keywords", "Short description", "Type", "Extra flags", "Wear flags", "Affects"], rows)


def triggers_text(triggers: list[int]) -> str:
    """Each trigger once, in first-row order, with its row count when it has several: "ON_ENTER (3 rows)"."""
    counts: dict[int, int] = {}
    for trigger in triggers:
        counts[trigger] = counts.get(trigger, 0) + 1
    parts = []
    for trigger, count in counts.items():
        name = world_flags.SCRIPT_TRIGGERS.name_of(trigger)
        parts.append(name if count == 1 else f"{name} ({count} rows)")
    return ", ".join(parts) or "none"


def scripts_section(scripts: list[Script]) -> str:
    """The scripts table."""
    rows = [[str(script.vnum), script.name, triggers_text(script.triggers)] for script in scripts]
    return markdown_table("Scripts", ["Vnum", "Name", "Triggers"], rows)


def read_category(world_dir: Path, category: str, reader: Callable[[Path], list]) -> list:
    """Every record of a category, reading its indexed files in index order."""
    records = []
    for path in indexed_files(world_dir, category):
        records.extend(reader(path))
    return records


def catalogue(world_dir: Path, zone_number: Optional[int] = None, category: Optional[str] = None) -> str:
    """The catalogue's markdown; zone_number keeps only that zone and the fixtures in its own vnums
    (n * 100 to n * 100 + 99, up to its top), wherever they are filed.

    Raises WorldFileError for a file the catalogue cannot read, and UnknownZone for a zone the world lacks.
    """
    zones = read_zones(world_dir)
    blocks = zone_blocks(zones)
    if zone_number is not None and zone_number not in blocks:
        raise UnknownZone(f"no zone {zone_number}; the zones are {', '.join(str(zone.number) for zone in zones)}")

    def wanted(vnum: int) -> bool:
        if zone_number is None:
            return True
        low, high = blocks[zone_number]
        return low <= vnum <= high

    sections = {
        "zones": lambda: zones_section([zone for zone in zones if zone_number in (None, zone.number)], blocks),
        "rooms": lambda: rooms_section([room for room in read_category(world_dir, "wld", read_rooms) if wanted(room.vnum)]),
        "mobs": lambda: mobs_section([mob for mob in read_category(world_dir, "mob", read_mobs) if wanted(mob.vnum)]),
        "objects": lambda: objects_section([item for item in read_category(world_dir, "obj", read_objects) if wanted(item.vnum)]),
        "scripts": lambda: scripts_section([script for script in read_category(world_dir, "scr", read_scripts) if wanted(script.vnum)]),
    }
    chosen = CATEGORIES if category is None else (category,)
    return "\n".join(sections[name]() for name in chosen)


def main(argv: Optional[list[str]] = None) -> int:
    """Prints the catalogue; 2 for an unknown zone or a world file it cannot read."""
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--world", type=Path, default=DEFAULT_WORLD_DIR, help="the world folder (default: the committed test world)")
    parser.add_argument("--zone", type=int, help="only this zone and the fixtures among its own vnums, n * 100 to n * 100 + 99")
    parser.add_argument("--category", choices=CATEGORIES, help="only this table")
    arguments = parser.parse_args(argv)
    try:
        text = catalogue(arguments.world, arguments.zone, arguments.category)
    except (UnknownZone, WorldFileError, OSError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 2
    print(text, end="")
    return 0


if __name__ == "__main__":
    sys.exit(main())
