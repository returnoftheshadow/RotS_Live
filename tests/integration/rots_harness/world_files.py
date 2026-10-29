"""Reads the test world's files (tests/integration/world): each category's world files, the order
its index lists them in, their record vnums, and each zone's header and reset rows."""

from __future__ import annotations

import re
from dataclasses import dataclass, field
from pathlib import Path
from typing import Optional

# The world categories, each a folder of world files with an index; the harness copies each into a lib.
WORLD_CATEGORIES = ("wld", "mob", "obj", "zon", "shp", "scr", "mdl")
# The category whose files are zones, one per file.
ZONE_CATEGORY = "zon"
# The file in each category folder that lists the folder's world files in boot order (index_boot, db.cpp).
INDEX_FILE_NAME = "index"
# The line that ends an index.
INDEX_END = "$"
# Files in a category folder that are not world files: the index, git's placeholder and notes.
NON_WORLD_FILE_NAMES = frozenset({INDEX_FILE_NAME, ".gitkeep", "README"})
# World files are read byte for byte by the server; latin-1 decodes any byte.
WORLD_ENCODING = "latin-1"
# A record line as count_hash_records (db.cpp) counts one: a '#' after leading whitespace, then the vnum.
RECORD_LINE = re.compile(r"^[ \t]*#(\d+)", re.MULTILINE)
# A number as %d reads one.
NUMBER_PATTERN = re.compile(r"[-+]?\d+")
# The vnum of the record that ends a room, mob, object, script or mudlle file; never a fixture.
END_OF_FILE_VNUM = 99999
# The world file and zone a vnum belongs to are named after vnum / 100: the shaping editors open
# <vnum / 100>.<extension> (shapeobj.cpp, shapemob.cpp, shaperom.cpp) and check zone vnum / 100.
VNUMS_PER_ZONE_NUMBER = 100
# Reset commands followed by two more numbers than the six every row has (load_zones, zone.cpp).
RESET_COMMANDS_WITH_EIGHT_NUMBERS = frozenset("MNXHEKQ")
# Reset commands followed by one more number.
RESET_COMMANDS_WITH_SEVEN_NUMBERS = frozenset("P")
# The reset row that is only a section title (load_zones reads no numbers for it).
RESET_SECTION_COMMAND = "="
# The reset command that ends a zone's rows.
RESET_END_COMMAND = "S"


@dataclass(frozen=True)
class ResetRow:
    """One zone reset row: its command letter, its numbers and its trailing comment."""

    # The command letter, such as M (load a mob) or O (load an object).
    command: str
    # The row's numbers in file order, starting with the if flag.
    numbers: tuple[int, ...]
    # The rest of the line after the numbers, stripped; a section row's title.
    comment: str

    def summary(self) -> str:
        """A short line: mob or object and room for M and O rows, otherwise the letter and numbers."""
        if self.command == RESET_SECTION_COMMAND:
            return f"section {self.comment}"
        if self.command in ("M", "O") and len(self.numbers) >= 3:
            kind = "mob" if self.command == "M" else "object"
            text = f"{kind} {self.numbers[1]} to room {self.numbers[2]}"
        else:
            text = " ".join([self.command, *(str(number) for number in self.numbers)])
        return f"{text} ({self.comment})" if self.comment else text


@dataclass(frozen=True)
class Zone:
    """A zone file's header and reset rows (load_zones, zone.cpp)."""

    # The zone number, from its #<n> line.
    number: int
    # The zone's name.
    name: str
    # The zone's map square (x, y), from its "<symbol> <x> <y> <level>" line.
    map_square: tuple[int, int]
    # The highest room vnum the zone holds; the zone before it in the index ends just below its rooms.
    top: int
    # The zone's reset rows, in file order, without the closing S.
    resets: list[ResetRow] = field(default_factory=list)


class WorldFileError(ValueError):
    """A world file does not have the layout its loader expects."""


def read_world_text(path: Path) -> str:
    """A world file's text, decoded as latin-1 so every byte survives."""
    return path.read_text(encoding=WORLD_ENCODING)


def record_vnums(text: str) -> list[int]:
    """Every record vnum in a world file's text, in file order, the closing #99999 included."""
    return [int(match.group(1)) for match in RECORD_LINE.finditer(text)]


def fixture_vnums(text: str) -> list[int]:
    """The record vnums of a world file's fixtures, in file order, without the closing #99999."""
    return [vnum for vnum in record_vnums(text) if vnum < END_OF_FILE_VNUM]


def world_files(category_dir: Path) -> list[Path]:
    """The world files directly in a category folder, in name order; backup folders are not entered."""
    return sorted(path for path in category_dir.iterdir() if path.is_file() and path.name not in NON_WORLD_FILE_NAMES)


def index_order(category_dir: Path) -> list[str]:
    """The world files' names in the order their index must list them: by first vnum, then by name.

    A zone file's first vnum is its zone number, so zones follow their numbers. A file with no
    record comes last.
    """

    def order_key(path: Path) -> tuple[int, str]:
        vnums = fixture_vnums(read_world_text(path))
        return (vnums[0] if vnums else END_OF_FILE_VNUM, path.name)

    return [path.name for path in sorted(world_files(category_dir), key=order_key)]


def index_text(names: list[str]) -> str:
    """An index's text listing names, one per line, then the closing $."""
    return "".join(f"{name}\n" for name in [*names, INDEX_END])


def read_index(category_dir: Path) -> list[str]:
    """The file names a category's index lists before its closing $."""
    names = []
    for line in read_world_text(category_dir / INDEX_FILE_NAME).splitlines():
        name = line.strip()
        if name == INDEX_END:
            break
        if name:
            names.append(name)
    return names


def indexed_files(world_dir: Path, category: str) -> list[Path]:
    """The files a category's index lists, in index order."""
    category_dir = world_dir / category
    return [category_dir / name for name in read_index(category_dir)]


class TextCursor:
    """Reads a world file the way db.cpp's loaders do: whole lines, ~-terminated strings and numbers."""

    def __init__(self, text: str, context: str) -> None:
        self._text = text  # the whole file
        self._position = 0  # the index of the next unread character
        self._context = context  # names the file in errors

    def at_end(self) -> bool:
        """Whether only whitespace is left."""
        return not self._text[self._position:].strip()

    def skip_whitespace(self) -> None:
        """Moves past spaces and line ends, as a leading space in a scanf format does."""
        while self._position < len(self._text) and self._text[self._position].isspace():
            self._position += 1

    def peek_character(self) -> str:
        """The next character after whitespace, without reading it; empty at the end."""
        self.skip_whitespace()
        return self._text[self._position:self._position + 1]

    def read_line(self) -> str:
        """The rest of the current line, without its line end."""
        end = self._text.find("\n", self._position)
        end = len(self._text) if end < 0 else end
        line = self._text[self._position:end]
        self._position = min(end + 1, len(self._text))
        return line.rstrip("\r")

    def read_string(self) -> str:
        """A string as fread_string reads one: whole lines until one whose last non-space character is ~.

        Leading whitespace and blank lines are skipped first, as the loaders' scanf formats and
        fread_string's own blank-line skip do.
        """
        self.skip_whitespace()
        lines = []
        while True:
            if self._position >= len(self._text):
                raise WorldFileError(f"{self._context}: the file ends inside a ~-terminated string")
            line = self.read_line()
            stripped = line.rstrip()
            if stripped.endswith("~"):
                lines.append(stripped[:-1])
                return "\n".join(lines).strip()
            lines.append(line)

    def read_word(self) -> str:
        """The next run of non-space characters, as %s reads it."""
        self.skip_whitespace()
        start = self._position
        while self._position < len(self._text) and not self._text[self._position].isspace():
            self._position += 1
        return self._text[start:self._position]

    def read_int(self, default: Optional[int] = None) -> int:
        """The next number, as %d reads it; default when the next text is not a number, which stays unread."""
        self.skip_whitespace()
        match = NUMBER_PATTERN.match(self._text, self._position)
        if match is None:
            if default is not None:
                return default
            raise WorldFileError(f"{self._context}: expected a number at {self._text[self._position:self._position + 20]!r}")
        self._position = match.end()
        return int(match.group(0))


def read_zone(path: Path) -> Zone:
    """A zone file's header and reset rows; raises WorldFileError when the layout is not load_zones'."""
    cursor = TextCursor(read_world_text(path), str(path))
    header = cursor.read_word()
    if not re.fullmatch(r"#\d+", header):
        raise WorldFileError(f"{path}: a zone starts with #<number>, not {header!r}")
    cursor.read_line()
    name = cursor.read_string()
    cursor.read_string()  # description
    cursor.read_string()  # map
    while cursor.read_int() != 0:  # owners, ending in 0
        pass
    cursor.read_line()
    cursor.read_word()  # symbol
    map_square = (cursor.read_int(), cursor.read_int())
    cursor.read_int()  # level
    top = cursor.read_int()
    cursor.read_int()  # lifespan
    cursor.read_int()  # reset mode
    cursor.read_line()
    resets = []
    while True:
        if cursor.at_end():
            raise WorldFileError(f"{path}: the reset rows have no closing {RESET_END_COMMAND}")
        line = cursor.read_line().strip()
        if not line:
            continue
        command = line[0]
        if command == RESET_END_COMMAND:
            break
        resets.append(parse_reset_row(command, line[1:]))
    return Zone(int(header[1:]), name, map_square, top, resets)


def parse_reset_row(command: str, rest: str) -> ResetRow:
    """A reset row from its command letter and the rest of its line."""
    if command == RESET_SECTION_COMMAND:
        return ResetRow(command, (), rest.strip())
    count = 6
    if command in RESET_COMMANDS_WITH_EIGHT_NUMBERS:
        count = 8
    elif command in RESET_COMMANDS_WITH_SEVEN_NUMBERS:
        count = 7
    words = rest.split()
    numbers = []
    for word in words[:count]:
        if not NUMBER_PATTERN.fullmatch(word):
            break
        numbers.append(int(word))
    comment = " ".join(words[len(numbers):])
    return ResetRow(command, tuple(numbers), comment)


def read_zones(world_dir: Path) -> list[Zone]:
    """The zones the zone index lists, in index order."""
    return [read_zone(path) for path in indexed_files(world_dir, ZONE_CATEGORY)]


def zone_blocks(zones: list[Zone]) -> dict[int, tuple[int, int]]:
    """Each zone's own vnums: n * 100 to n * 100 + 99, the vnums the shaping editors keep in its
    files, cut to its room range (a zone whose top is lower ends there)."""
    blocks = {}
    for zone_number, (low, high) in zone_vnum_ranges(zones).items():
        block_low = zone_number * VNUMS_PER_ZONE_NUMBER
        blocks[zone_number] = (max(low, block_low), min(high, block_low + VNUMS_PER_ZONE_NUMBER - 1))
    return blocks


def zone_vnum_ranges(zones: list[Zone]) -> dict[int, tuple[int, int]]:
    """Each zone's vnum range as load_rooms (db.cpp) assigns rooms: from just above the previous
    zone's top (0 for the first zone) to its own top, in index order."""
    ranges = {}
    previous_top = -1
    for zone in zones:
        ranges[zone.number] = (previous_top + 1, zone.top)
        previous_top = zone.top
    return ranges
