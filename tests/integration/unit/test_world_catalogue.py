from __future__ import annotations

import re
from pathlib import Path

import pytest

from rots_harness import world_catalogue, world_flags
from rots_harness.world_files import WORLD_CATEGORIES

# The game's sources, whose headers the name tables copy.
SOURCE_DIR = Path(__file__).resolve().parents[3] / "src"
# A constant definition in a game header: `inline constexpr int NAME = <value>;`.
CONSTANT_DEFINITION = re.compile(r"^\s*inline constexpr int (\w+) = ([^;]+);", re.MULTILINE)
# A value written as a shift, `(1 << n)`.
SHIFT_VALUE = re.compile(r"\(\s*1\s*<<\s*(\d+)\s*\)")
# Zone 21, with one mob reset.
ZONE_TEXT = """#21
Catalogue zone~
A zone for the catalogue tests.
~
~
0
? 4 5 5
2199
999
2
M 0 2101 2102 0 100 100 1 1 the keeper waits
S
"""
# Two rooms joined by a door one way and a plain exit back; the first has an extra description.
ROOMS_TEXT = """#2101
First Room~
The first room.
~
21 1 3 0
D1
~
door~
3 0 2102 5
E
plaque sign~
The plaque reads FIRST.
~
S
#2102
Second Room~
The second room,
on two lines.
~
21 32768 0 0
D3
~
~
0 0 2101 5
S
#99999
$~
"""
# A trader with options and a script, and an N mob with death cries and no options.
MOBS_TEXT = """#2101
keeper shop~
a shop keeper~
A shop keeper stands here.~
It keeps the shop.
~
19
0 0 M
7 10 0 0
30 30
2 0
0 100 0
8 8 1 1 0
150 70 33 0 0 0
0 0 50 1
0
10 10 10 10 10 10
0 10 0 0 2105 100 0
store=2102
attacks=no
~
#2102
crier town~
the town crier~
The town crier shouts here.~
~
2
0 0 N
He dies loudly.~
His echo fades.~
3 10 0 0
30 30
2 0
0 100 0
8 8 1 1 0
150 70 0 0 0 0
0 0 50 1
0
10 10 10 10 10 10
1 10 0 0 0 100 0
#99999
"""
# An amulet with an extra description and two A lines, and a pebble with the older four-number line.
OBJECTS_TEXT = """#2101
amulet sickly~
a sickly amulet~
A sickly amulet lies here.~
~
11 4096 5
0 0 0 0 0
5 10 -1
0 0 2 0 0
E
amulet~
It looks ill.~
A
28 11
A
1 2
#2102
pebble~
a pebble~
A pebble lies here.~
~
12 0 1
0 0 0 0 0
1 0 0
0 0 0 0
#99999
"""
# A script with two ON_ENTER rows, one ON_HEAR_SAY row and a say row.
SCRIPTS_TEXT = """#2101 Catalogue greeter~
Greets twice.~
11 1 0 0 0 0 0 0
 first~
17 2 0 0 0 0 0 0
 heard~
11 3 0 0 0 0 0 0
 second~
30 4 100 0 0 0 0 0
 Hello.~
999 0 0 0 0 0 0 0
#99999
"""


@pytest.fixture
def world(tmp_path: Path) -> Path:
    """A one-zone world with two rooms, two mobs, two objects and a script."""
    world_dir = tmp_path / "world"
    files = {"zon": ("21.zon", ZONE_TEXT), "wld": ("21.wld", ROOMS_TEXT), "mob": ("21.mob", MOBS_TEXT), "obj": ("21.obj", OBJECTS_TEXT), "scr": ("21.scr", SCRIPTS_TEXT)}
    for category in WORLD_CATEGORIES:
        (world_dir / category).mkdir(parents=True)
        if category in files:
            file_name, text = files[category]
            (world_dir / category / file_name).write_text(text, encoding="latin-1")
            (world_dir / category / "index").write_text(f"{file_name}\n$\n", encoding="latin-1")
        else:
            (world_dir / category / "index").write_text("$\n", encoding="latin-1")
    return world_dir


def table_rows(markdown: str, title: str) -> list[list[str]]:
    """The cells of each data row of the table under ## title."""
    section = markdown.split(f"## {title}\n", 1)[1].split("\n## ", 1)[0]
    lines = [line for line in section.splitlines() if line.startswith("| ")][2:]
    return [[cell.strip() for cell in line.strip("|").split(" | ")] for line in lines]


def test_the_zone_row_gives_its_square_range_and_resets(world: Path) -> None:
    rows = table_rows(world_catalogue.catalogue(world), "Zones")
    assert rows == [["21", "Catalogue zone", "(4, 5)", "2100-2199", "mob 2101 to room 2102 (the keeper waits)"]]


def test_rooms_decode_flags_sectors_and_exits(world: Path) -> None:
    rows = table_rows(world_catalogue.catalogue(world, category="rooms"), "Rooms")
    assert rows == [
        ["2101", "First Room", "1 (DARK)", "3 (FOREST)", "east 2102 [flags 3 (ISDOOR, CLOSED); keyword door]"],
        ["2102", "Second Room", "32768 (PEACEROOM)", "0 (INSIDE)", "west 2101"],
    ]


def test_mobs_show_level_flags_special_script_and_options(world: Path) -> None:
    rows = table_rows(world_catalogue.catalogue(world, category="mobs"), "Mobs")
    assert rows == [
        ["2101", "keeper shop", "a shop keeper", "7", "19 (SPEC, SENTINEL, NOBASH)", "program 33", "2105", "store=2102; attacks=no"],
        ["2102", "crier town", "the town crier", "3", "2 (SENTINEL)", "-", "-", "-"],
    ]


def test_objects_decode_type_flags_and_affects(world: Path) -> None:
    rows = table_rows(world_catalogue.catalogue(world, category="objects"), "Objects")
    assert rows == [
        ["2101", "amulet sickly", "a sickly amulet", "11 (WORN)", "4096 (NORENT)", "5 (TAKE, WEAR_NECK)", "BITVECTOR 11 (POISON); STR 2"],
        ["2102", "pebble", "a pebble", "12 (OTHER)", "0", "1 (TAKE)", "-"],
    ]


def test_scripts_list_each_trigger_once_with_its_row_count(world: Path) -> None:
    rows = table_rows(world_catalogue.catalogue(world, category="scripts"), "Scripts")
    assert rows == [["2101", "Catalogue greeter", "ON_ENTER (2 rows), ON_HEAR_SAY"]]


def test_a_zone_filter_keeps_only_the_fixtures_among_its_own_vnums(world: Path) -> None:
    (world / "mob" / "guildmasters.mob").write_text(MOBS_TEXT.replace("#2101", "#3101").replace("#2102", "#3102"), encoding="latin-1")
    (world / "mob" / "index").write_text("21.mob\nguildmasters.mob\n$\n", encoding="latin-1")
    every_mob = table_rows(world_catalogue.catalogue(world, category="mobs"), "Mobs")
    zone_mobs = table_rows(world_catalogue.catalogue(world, zone_number=21, category="mobs"), "Mobs")
    assert [row[0] for row in every_mob] == ["2101", "2102", "3101", "3102"]
    assert [row[0] for row in zone_mobs] == ["2101", "2102"]


def test_main_refuses_an_unknown_zone(world: Path, capsys: pytest.CaptureFixture[str]) -> None:
    assert world_catalogue.main(["--world", str(world), "--zone", "99"]) == 2
    assert "no zone 99; the zones are 21" in capsys.readouterr().err


def test_main_prints_every_table(world: Path, capsys: pytest.CaptureFixture[str]) -> None:
    assert world_catalogue.main(["--world", str(world)]) == 0
    titles = re.findall(r"^## (\w+)$", capsys.readouterr().out, re.MULTILINE)
    assert titles == ["Zones", "Rooms", "Mobs", "Objects", "Scripts"]


def test_the_committed_world_catalogues_without_errors() -> None:
    text = world_catalogue.catalogue(world_catalogue.DEFAULT_WORLD_DIR)
    assert "| 1131 | Arena Centre |" in text


def header_constants(header: str) -> dict[str, int]:
    """Every `inline constexpr int` in a game header, with its value."""
    constants = {}
    for name, expression in CONSTANT_DEFINITION.findall((SOURCE_DIR / header).read_text(encoding="latin-1")):
        shift = SHIFT_VALUE.fullmatch(expression.strip())
        if shift:
            constants[name] = 1 << int(shift.group(1))
        elif re.fullmatch(r"-?\d+", expression.strip()):
            constants[name] = int(expression)
    return constants


@pytest.mark.parametrize("table", world_flags.ALL_TABLES, ids=lambda table: table.entries[0][0])
def test_each_name_table_matches_the_game_header(table: world_flags.NameTable) -> None:
    constants = header_constants(table.header)
    mismatched = [(name, value, constants.get(name)) for name, value in table.entries if constants.get(name) != value]
    assert mismatched == []


@pytest.mark.parametrize(
    "table, prefix, values",
    [
        (world_flags.MOB_FLAGS, "MOB_", lambda value: value > 0),
        (world_flags.SECTORS, "SECT_", lambda value: True),
        (world_flags.EXIT_FLAGS, "EX_", lambda value: True),
        (world_flags.APPLY_LOCATIONS, "APPLY_", lambda value: True),
        (world_flags.AFFECT_FLAGS, "AFF_", lambda value: True),
    ],
    ids=["MOB_", "SECT_", "EX_", "APPLY_", "AFF_"],
)
def test_prefixed_tables_name_every_constant_of_their_prefix(table: world_flags.NameTable, prefix: str, values) -> None:
    constants = header_constants(table.header)
    expected = {name for name, value in constants.items() if name.startswith(prefix) and values(value)}
    assert expected - {name for name, _ in table.entries} == set()


def test_the_item_type_table_names_every_item_type() -> None:
    """The item types are the run of ITEM_ constants in structs.h from ITEM_LIGHT = 1 to the
    first line that is not one (ITEM_ is shared with the extra and wear flags, so the
    prefix test above cannot cover them)."""
    lines = (SOURCE_DIR / "structs.h").read_text(encoding="latin-1").splitlines()
    start = next(i for i, line in enumerate(lines) if re.search(r"\bITEM_LIGHT = 1;", line))
    types = []
    for line in lines[start:]:
        match = re.match(r"\s*inline constexpr int (ITEM_\w+) = \d+;", line)
        if not match:
            break
        types.append(match.group(1))
    assert len(types) >= 26  # the run was found
    assert set(types) - {name for name, _ in world_flags.ITEM_TYPES.entries} == set()


def test_the_trigger_table_names_every_on_constant() -> None:
    constants = header_constants("script.h")
    assert {name for name in constants if name.startswith("ON_")} == {name for name, _ in world_flags.SCRIPT_TRIGGERS.entries}
