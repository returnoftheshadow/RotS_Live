"""Mirror of the zone loader in src/zone.cpp (load_zones).

Field order is copied from the fscanf sequence; do not reorder without
re-reading zone.cpp. Strings end at '~'; the owner list and the reset
command list are whitespace integer streams. The reset command list ends
at the 'S' command (src/zone.cpp:100-166). Every 'M' command is a mobile
load whose difficulty coefficient (arg5) becomes GET_DIFFICULTY(mob) at
spawn time (src/zone.cpp:729); it is not present in the mob file itself.
"""
from __future__ import annotations
import csv
import sys
from dataclasses import dataclass
from pathlib import Path

EAST_OF_RIVER_THRESHOLD = 8
EAST_BONUS_PER_UNIT = 3
EAST_BONUS_CAP_UNITS = 5

# fread_string (src/db.cpp:3271-3320) strips a leading control character run
# (chars below ' ', 0x20) from each fgets'd line before appending it.
CONTROL_CHARACTERS = "".join(chr(code) for code in range(0x20))

# Commands whose base six integers (if_flag arg1 arg2 arg3 arg4 arg5) are
# followed by further integers, per the switch in src/zone.cpp:113-145.
COMMANDS_WITH_TWO_EXTRA_ARGS = {"M", "N", "X", "H", "E", "K", "Q"}
COMMANDS_WITH_ONE_EXTRA_ARG = {"P"}

ZONE_CSV_COLUMNS = [
    "number", "name", "symbol", "x", "y", "level", "top", "lifespan", "reset_mode",
    "is_east_of_river", "east_bonus_percent",
]
MOB_LOAD_CSV_COLUMNS = ["zone", "vnum", "room", "max_existing", "load_percent", "difficulty"]


@dataclass
class MobLoad:
    vnum: int
    room: int
    max_existing: int
    load_percent: int
    difficulty: int


@dataclass
class ZoneRecord:
    number: int
    name: str
    symbol: str
    x: int
    y: int
    level: int
    top: int
    lifespan: int
    reset_mode: int
    mob_loads: list[MobLoad]

    @property
    def is_east_of_river(self) -> bool:
        return self.x > EAST_OF_RIVER_THRESHOLD

    @property
    def east_bonus_percent(self) -> int:
        if not self.is_east_of_river:
            return 0
        return min(self.x - EAST_OF_RIVER_THRESHOLD, EAST_BONUS_CAP_UNITS) * EAST_BONUS_PER_UNIT


class _Cursor:
    """Sequential reader over one file's text with fread_string / fscanf semantics."""

    def __init__(self, text: str):
        self.text = text
        self.pos = 0

    def read_string(self) -> str:
        # Mirrors fread_string's do/while: it reads whole lines with fgets
        # and only terminates the string once a line's last non-whitespace
        # character is '~' -- not at the first literal '~' anywhere ahead,
        # which is wrong whenever a field spans several lines (a multi-line
        # map/description) or a line holds two adjacent '~' characters (an
        # empty field written as 'Name~~').
        accumulated = ""
        is_first_line = True
        while True:
            if self.pos >= len(self.text):
                raise ValueError("unterminated string: reached end of file before '~'")
            newline_index = self.text.find("\n", self.pos)
            line_end = len(self.text) if newline_index == -1 else newline_index + 1
            line = self.text[self.pos:line_end]
            self.pos = line_end
            if is_first_line:
                is_first_line = False
                if line.strip() == "":  # fread_string discards a blank leading line
                    line = ""
            accumulated += line.lstrip(CONTROL_CHARACTERS)
            trimmed = accumulated.rstrip()
            if trimmed.endswith("~"):
                return trimmed[:-1]

    def read_token(self) -> str:
        length = len(self.text)
        while self.pos < length and self.text[self.pos].isspace():
            self.pos += 1
        start = self.pos
        while self.pos < length and not self.text[self.pos].isspace():
            self.pos += 1
        return self.text[start:self.pos]

    def read_ints(self, count: int) -> list[int]:
        return [int(self.read_token()) for _ in range(count)]

    def skip_to_next_line(self) -> None:
        end = self.text.find("\n", self.pos)
        self.pos = len(self.text) if end == -1 else end + 1


def _read_mob_loads(cursor: _Cursor) -> list[MobLoad]:
    mob_loads: list[MobLoad] = []
    while True:
        command = cursor.read_token()
        if command in ("", "S"):
            return mob_loads
        if_flag, arg1, arg2, arg3, arg4, arg5 = cursor.read_ints(6)
        if command in COMMANDS_WITH_TWO_EXTRA_ARGS:
            cursor.read_ints(2)  # arg6, arg7
        elif command in COMMANDS_WITH_ONE_EXTRA_ARG:
            cursor.read_ints(1)  # arg6
        cursor.skip_to_next_line()  # discard the trailing free-text comment
        if command == "M":
            mob_loads.append(MobLoad(vnum=arg1, room=arg2, max_existing=arg3, load_percent=arg4, difficulty=arg5))


def parse_zone_file(path: Path) -> ZoneRecord:
    cursor = _Cursor(path.read_text(encoding="latin-1"))
    marker = cursor.read_token()
    if not marker.startswith("#"):
        raise ValueError(f"{path.name}: expected '#number', found {marker!r} near offset {cursor.pos}")
    number = int(marker[1:])
    name = cursor.read_string()
    cursor.read_string()  # description
    cursor.read_string()  # map

    while True:
        owner = int(cursor.read_token())
        if owner == 0:
            break
    cursor.skip_to_next_line()

    symbol = cursor.read_token()
    x, y, level = int(cursor.read_token()), int(cursor.read_token()), int(cursor.read_token())
    top = int(cursor.read_token())
    lifespan = int(cursor.read_token())
    reset_mode = int(cursor.read_token())

    mob_loads = _read_mob_loads(cursor)

    return ZoneRecord(number, name, symbol, x, y, level, top, lifespan, reset_mode, mob_loads)


def parse_all(zon_dir: Path) -> dict[int, ZoneRecord]:
    # lib/world/zon/index is the file the server's boot loop actually reads
    # (src/db.cpp:1109); everything with a non-numeric stem, such as nn.zon,
    # is not listed there and is not part of the live world.
    zones: dict[int, ZoneRecord] = {}
    zone_files = [path for path in zon_dir.glob("*.zon") if path.stem.isdigit()]
    for path in sorted(zone_files, key=lambda p: int(p.stem)):
        record = parse_zone_file(path)
        zones[record.number] = record
    return zones


def _write_zone_rows(zones: dict[int, ZoneRecord], writer: csv.writer) -> None:
    writer.writerow(ZONE_CSV_COLUMNS)
    for number in sorted(zones):
        record = zones[number]
        writer.writerow([
            record.number, record.name, record.symbol, record.x, record.y, record.level, record.top,
            record.lifespan, record.reset_mode, int(record.is_east_of_river), record.east_bonus_percent,
        ])


def _write_mob_load_rows(zones: dict[int, ZoneRecord], writer: csv.writer) -> None:
    writer.writerow(MOB_LOAD_CSV_COLUMNS)
    for number in sorted(zones):
        for load in zones[number].mob_loads:
            writer.writerow([number, load.vnum, load.room, load.max_existing, load.load_percent, load.difficulty])


def main(argv: list[str]) -> int:
    zones = parse_all(Path(argv[1]))
    mob_loads_mode = len(argv) > 2 and argv[2] == "--mob-loads"
    writer = csv.writer(sys.stdout)
    if mob_loads_mode:
        _write_mob_load_rows(zones, writer)
    else:
        _write_zone_rows(zones, writer)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
