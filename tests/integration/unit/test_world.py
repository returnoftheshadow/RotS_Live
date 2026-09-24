from __future__ import annotations

import math
import re
from pathlib import Path

WORLD_ROOT = Path(__file__).resolve().parents[1] / "world"
SPEC_ASSIGN_SOURCE = Path(__file__).resolve().parents[3] / "src" / "spec_ass.cpp"
BAG_VNUM = 1136
CAP_VNUM = 1137


def read_index(category: str) -> list[str]:
    lines = (WORLD_ROOT / category / "index").read_text(encoding="latin-1").splitlines()
    assert lines[-1] == "$", f"{category}/index must end with a $ line"
    return lines[:-1]


def test_every_category_has_an_index_that_names_existing_files() -> None:
    for category in ("wld", "mob", "obj", "zon", "shp", "scr", "mdl"):
        for file_name in read_index(category):
            assert (WORLD_ROOT / category / file_name).is_file(), f"{category}/{file_name} listed but missing"


def test_rooms_are_ascending_and_inside_zone_eleven() -> None:
    text = (WORLD_ROOT / "wld" / "11.wld").read_text(encoding="latin-1")
    vnums = [int(match) for match in re.findall(r"^#(\d+)\s*$", text, flags=re.MULTILINE)]
    assert vnums[-1] == 99999
    room_vnums = vnums[:-1]
    assert room_vnums == sorted(room_vnums)
    assert all(1101 <= vnum <= 1199 for vnum in room_vnums)


def test_every_exit_points_at_a_room_in_the_file() -> None:
    text = (WORLD_ROOT / "wld" / "11.wld").read_text(encoding="latin-1")
    room_vnums = {int(match) for match in re.findall(r"^#(\d+)\s*$", text, flags=re.MULTILINE)}
    exit_targets = [int(match) for match in re.findall(r"^D[0-5]\n.*?~\n.*?~\n-?\d+ -?\d+ (\d+) \d+", text, flags=re.MULTILINE | re.DOTALL)]
    assert exit_targets, "expected at least one exit"
    for target in exit_targets:
        assert target in room_vnums, f"exit to {target} has no room"


def test_zone_loads_the_target_and_the_snake() -> None:
    text = (WORLD_ROOT / "zon" / "11.zon").read_text(encoding="latin-1")
    assert re.search(r"^M 0 1130 1132 ", text, flags=re.MULTILINE), "target orc must load into 1132"
    assert re.search(r"^M 0 1131 1134 ", text, flags=re.MULTILINE), "snake must load into 1134"
    assert text.rstrip().endswith("S")


def test_snake_mob_binds_its_bite_special() -> None:
    """SPECIAL(snake) (spec_pro.cpp) only fires when the mob's act-flags line sets MOB_SPEC
    (bit 0, structs.h:934); mobact.cpp:116-134 requires it before dispatching through
    spec_ass.cpp's virt_program_number()/get_special_function() tables. Without MOB_SPEC, the
    mobile file's <store_prog_number> field is instead treated as a Mudlle script program
    number and handed to real_program() (db.cpp ~1765-1768), which looks it up in this zone's
    (empty) .mdl file and silently returns 0 -- the snake would never bite or poison. See
    docs/data-formats/world-files.md's mobile-file field order for the line layout below.
    """
    text = (WORLD_ROOT / "mob" / "11.mob").read_text(encoding="latin-1")
    record = text.split("#1131", 1)[1].split("#1132", 1)[0]
    lines = [line for line in record.splitlines() if line.strip()]
    act_flags = int(lines[5])  # <mob_action_flags>, right after the four tilde-terminated strings
    assert act_flags & 1, f"snake mob_action_flags {act_flags} must set MOB_SPEC (bit 0) or its special never binds"
    weight_height_prog_line = lines[12]  # <weight> <height> <store_prog_number> <butcher_item> <corpse_num> <rp_flag>
    program_number = int(weight_height_prog_line.split()[2])
    assert program_number == 1, f"snake store_prog_number {program_number} must be 1 (spec_ass.cpp's virt_program_number/get_special_function select SPECIAL(snake) for 1)"


def test_zone_twelve_sits_twenty_squares_from_zone_eleven() -> None:
    """spell_summon adds the straight-line map distance, rounded down, to the save bonus
    (mage.cpp ~867); new_saves_spell treats a bonus of 20 or more as an automatic save
    (spell_pa.cpp ~262), so this placement makes a cross-zone summon fail deterministically."""
    eleven = (WORLD_ROOT / "zon" / "11.zon").read_text(encoding="latin-1")
    twelve = (WORLD_ROOT / "zon" / "12.zon").read_text(encoding="latin-1")
    header = re.compile(r"^\? (-?\d+) (-?\d+) \d+\s*$", flags=re.MULTILINE)
    x1, y1 = (int(value) for value in header.search(eleven).groups())
    x2, y2 = (int(value) for value in header.search(twelve).groups())
    assert math.isqrt((x2 - x1) ** 2 + (y2 - y1) ** 2) == 20


def test_zone_twelve_holds_only_the_distant_cell() -> None:
    text = (WORLD_ROOT / "wld" / "12.wld").read_text(encoding="latin-1")
    vnums = [int(match) for match in re.findall(r"^#(\d+)\s*$", text, flags=re.MULTILINE)]
    assert vnums == [1201, 99999]
    assert "Distant Cell" in text
    assert not re.search(r"^D[0-5]$", text, flags=re.MULTILINE), "the cell has no exits"


def test_crevice_floor_is_a_plain_down_exit_from_arena_west() -> None:
    text = (WORLD_ROOT / "wld" / "11.wld").read_text(encoding="latin-1")
    assert re.search(r"^#1136\s*$", text, flags=re.MULTILINE), "room 1136 (Crevice Floor) must exist"
    room_1130 = text.split("#1130", 1)[1].split("#1131", 1)[0]
    match = re.search(r"^D5\n.*?~\n.*?~\n(\d+) \d+ 1136 \d+", room_1130, flags=re.MULTILINE | re.DOTALL)
    assert match is not None, "room 1130 must have a D5 (down) exit to 1136"
    assert match.group(1) == "0", "the way down must start open for spell_earthquake"


def object_numbers(vnum: int) -> tuple[list[int], list[int]]:
    """The type/extra/wear line and the five values of object `vnum` in 11.obj.

    load_objects (db.cpp) reads four tilde-terminated strings, then <type> <extra_flags>
    <wear_flags>, then <value0>..<value4>, then <weight> <cost> <cost_per_day>, then
    <level> <rarity> <material> <script_number> <unused>.
    """
    text = (WORLD_ROOT / "obj" / "11.obj").read_text(encoding="latin-1")
    match = re.search(rf"^#{vnum}\n(?:[^~]*~\n){{4}}([^\n]+)\n([^\n]+)\n", text, flags=re.MULTILINE)
    assert match is not None, f"object {vnum} missing from 11.obj or its four strings are malformed"
    return [int(field) for field in match.group(1).split()], [int(field) for field in match.group(2).split()]


def test_leather_bag_is_a_takeable_container_that_is_not_a_corpse() -> None:
    """ITEM_CONTAINER is type 15 and ITEM_TAKE wear bit 1 (structs.h). value0 is the weight
    capacity (act_obj1.cpp checks contents plus item against it); value3 == 1 would make
    is_corpse() (act_obj1.cpp) treat the bag as a corpse."""
    (item_type, extra_flags, wear_flags), values = object_numbers(BAG_VNUM)
    assert item_type == 15, f"object {BAG_VNUM} type {item_type} must be ITEM_CONTAINER (15)"
    assert extra_flags == 0, f"object {BAG_VNUM} extra_flags {extra_flags} must be 0"
    assert wear_flags & 1, f"object {BAG_VNUM} wear_flags {wear_flags} must include ITEM_TAKE (1)"
    assert values[0] >= 100, f"object {BAG_VNUM} capacity {values[0]} must hold the cap"
    assert values[1] == 0, f"object {BAG_VNUM} container flags {values[1]} must leave it open and not closeable"
    assert values[3] == 0, f"object {BAG_VNUM} value3 {values[3]} must be 0 or is_corpse() matches it"


def test_leather_cap_is_takeable_head_armour() -> None:
    """ITEM_ARMOR is type 9; ITEM_TAKE is wear bit 1 and ITEM_WEAR_HEAD wear bit 16 (structs.h)."""
    (item_type, extra_flags, wear_flags), _values = object_numbers(CAP_VNUM)
    assert item_type == 9, f"object {CAP_VNUM} type {item_type} must be ITEM_ARMOR (9)"
    assert extra_flags == 0, f"object {CAP_VNUM} extra_flags {extra_flags} must be 0"
    assert wear_flags == 1 | 16, f"object {CAP_VNUM} wear_flags {wear_flags} must be ITEM_TAKE | ITEM_WEAR_HEAD (17)"


def test_harness_objects_avoid_hard_wired_object_specials() -> None:
    """assign_objects() (spec_ass.cpp) binds a special to fixed object vnums, most of them
    gen_board message boards; a harness object on one of those vnums runs the special on every
    look and, lying in a room, is taken for the board by find_board() (boards.cpp)."""
    source = SPEC_ASSIGN_SOURCE.read_text(encoding="latin-1")
    assigned = {int(match) for match in re.findall(r"^\s*ASSIGNOBJ\((\d+),", source, flags=re.MULTILINE)}
    assert assigned, f"no ASSIGNOBJ lines found in {SPEC_ASSIGN_SOURCE}"
    for vnum in (BAG_VNUM, CAP_VNUM):
        assert vnum not in assigned, f"object {vnum} has a special hard-wired by spec_ass.cpp's ASSIGNOBJ"
