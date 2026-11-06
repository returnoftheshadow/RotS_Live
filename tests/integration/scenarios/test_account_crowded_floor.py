"""A crowded floor: a room's object list stops at 100 lines and counts the rest, so the people
in the room still reach the player."""

from __future__ import annotations

from typing import Optional

import pytest

from rots_harness import world
from rots_harness.characters import IMPLEMENTOR, Character

pytestmark = [pytest.mark.scenario, pytest.mark.account_menu, pytest.mark.account_menu_crowded_floor]

# A level-20 human warrior in the arena centre, who looks at the crowded floor.
ONLOOKER = Character(
    name="Testonlooker", race="human", sex="male", level=20, load_room=world.ROOM_ARENA_CENTRE, class_name="warrior"
)

# The leather cap (tests/integration/world/obj/11.obj): a plain object with no special procedure.
CAP_VNUM = 1137
# What `load obj` prints for each cap (do_load, act_wiz.cpp); it creates one object per command.
CAP_CREATED = "You create a leather cap."
# What `load obj` prints for a vnum the world lacks (do_load).
NO_SUCH_OBJECT = "There is no object with that number."
# What the implementor reads for each cap it drops (perform_drop, act_obj1.cpp).
CAP_DROPPED = "You drop a leather cap."
# The cap's line in a room's object list: its long description.
CAP_FLOOR_LINE = "A leather cap lies here."
# The most objects a room's floor lists (list_obj_to_char, act_info.cpp).
FLOOR_LIST_LIMIT = 100
# The start of every line that counts the objects past the limit (list_obj_to_char).
REMAINDER_LINE_START = "...and "
# How the onlooker sees the implementor in the room (show_char_to_char, act_info.cpp): its name, its
# title, and the end of the line. The title comes from rotstool, so only the ends are matched.
IMPLEMENTOR_LINE_START = f"{IMPLEMENTOR.name} "
IMPLEMENTOR_LINE_END = " is standing here."
# What write_to_output appends when a player's output buffer overflows (comm.cpp).
OVERFLOW_MARKER = "**OVERFLOW**"
# Seconds the onlooker reads before looking, so the drop messages are not taken for the look's reply.
SETTLE_SECONDS = 1.0


@pytest.mark.parametrize(
    ("caps_dropped", "remainder_line"),
    [
        pytest.param(FLOOR_LIST_LIMIT, None, id="exactly-100"),
        pytest.param(FLOOR_LIST_LIMIT + 1, "...and 1 more item is lying here.", id="101"),
        pytest.param(FLOOR_LIST_LIMIT + 5, "...and 5 more items are lying here.", id="105"),
    ],
)
@pytest.mark.characters(IMPLEMENTOR, ONLOOKER)
def test_a_crowded_floor_lists_100_objects_then_a_remainder_line_then_the_people(
    server, caps_dropped: int, remainder_line: Optional[str]
) -> None:
    """Pins #300: a room's floor lists at most 100 objects, then one line counting the rest (none when
    nothing is left over, singular for one), then the people in the room, with nothing dropped."""
    implementor = server.login(IMPLEMENTOR.name)
    onlooker = server.login(ONLOOKER.name)
    for _cap in range(caps_dropped):
        implementor.send_line(f"load obj {CAP_VNUM}")
        loaded = implementor.expect([CAP_CREATED, NO_SUCH_OBJECT])
        assert CAP_CREATED in loaded, loaded
    implementor.command(f"goto {world.ROOM_ARENA_CENTRE}")
    dropped = implementor.command("drop all")
    assert dropped.text.count(CAP_DROPPED) == caps_dropped, dropped.text
    onlooker.drain(SETTLE_SECONDS)

    looked = onlooker.command("look").text
    assert OVERFLOW_MARKER not in looked, looked
    lines = [line.strip() for line in looked.splitlines() if line.strip()]
    cap_lines = [index for index, line in enumerate(lines) if line == CAP_FLOOR_LINE]
    assert len(cap_lines) == FLOOR_LIST_LIMIT, looked
    remainder_lines = [index for index, line in enumerate(lines) if line.startswith(REMAINDER_LINE_START)]
    people_lines = [index for index, line in enumerate(lines) if line.startswith(IMPLEMENTOR_LINE_START) and line.endswith(IMPLEMENTOR_LINE_END)]
    assert len(people_lines) == 1, looked
    if remainder_line is None:
        assert remainder_lines == [], looked
        assert cap_lines[-1] < people_lines[0], looked
    else:
        assert [lines[index] for index in remainder_lines] == [remainder_line], looked
        assert cap_lines[-1] < remainder_lines[0] < people_lines[0], looked
