from __future__ import annotations

import re
from pathlib import Path

WORLD_ROOT = Path(__file__).resolve().parents[1] / "world"


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


def test_crevice_floor_is_a_plain_down_exit_from_arena_west() -> None:
    text = (WORLD_ROOT / "wld" / "11.wld").read_text(encoding="latin-1")
    assert re.search(r"^#1136\s*$", text, flags=re.MULTILINE), "room 1136 (Crevice Floor) must exist"
    room_1130 = text.split("#1130", 1)[1].split("#1131", 1)[0]
    match = re.search(r"^D5\n.*?~\n.*?~\n(\d+) \d+ 1136 \d+", room_1130, flags=re.MULTILINE | re.DOTALL)
    assert match is not None, "room 1130 must have a D5 (down) exit to 1136"
    assert match.group(1) == "0", "the way down must start open for spell_earthquake"
