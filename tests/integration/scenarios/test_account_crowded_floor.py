"""A crowded floor: a room's object list stops at 100 lines and counts the rest.

The people in the room still reach the player after it.
"""

from __future__ import annotations

import pytest

from rots_harness import world
from rots_harness.characters import IMPLEMENTOR, Character
from rots_harness.game_text import DROP_FORMAT, OUTPUT_OVERFLOW
from rots_harness.immortal_commands import CREATED_FORMAT, goto
from rots_harness.replies import SETTLE_SECONDS
from rots_harness.text_lines import stripped_lines

pytestmark = [pytest.mark.scenario, pytest.mark.account_menu, pytest.mark.account_menu_crowded_floor]

# A level-20 human warrior in the arena centre, who looks at the crowded floor.
ONLOOKER = Character(
    name="Testonlooker", race="human", sex="male", level=20, load_room=world.ROOM_ARENA_CENTRE, class_name="warrior"
)

# What `load obj` prints for each leather cap (do_load, act_wiz.cpp); it creates one object per
# command.
CAP_CREATED_REPLY = CREATED_FORMAT.format(world.OBJECT_LEATHER_CAP_SHORT_DESCRIPTION)
# What `load obj` prints for a vnum the world lacks (do_load).
NO_SUCH_OBJECT_REPLY = "There is no object with that number."
# What the implementor reads for each cap it drops (perform_drop, act_obj1.cpp).
CAP_DROPPED_REPLY = DROP_FORMAT.format(world.OBJECT_LEATHER_CAP_SHORT_DESCRIPTION)
# The cap's line in a room's object list: its long description.
CAP_FLOOR_LINE = "A leather cap lies here."
# The most objects a room's floor lists (list_obj_to_char, act_info.cpp).
FLOOR_LIST_LIMIT = 100
# The start of every line that counts the objects past the limit (list_obj_to_char).
REMAINDER_LINE_START = "...and "
# The start of the line the onlooker sees the implementor by (show_char_to_char, act_info.cpp):
# its name, before the title rotstool gives it, so only the line's ends are matched.
IMPLEMENTOR_LINE_START = f"{IMPLEMENTOR.name} "
# The end of the line the onlooker sees the implementor by, after its title.
IMPLEMENTOR_LINE_END = " is standing here."


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
    server, caps_dropped: int, remainder_line: str | None
) -> None:
    """Pins the floor list: at most 100 objects, then one line counting the rest, then the people.

    No remainder line appears when nothing is left over, the line is singular for one, and nothing
    is dropped.
    """
    implementor = server.login(IMPLEMENTOR.name)
    onlooker = server.login(ONLOOKER.name)
    for _cap in range(caps_dropped):
        implementor.send_line(f"load obj {world.OBJECT_LEATHER_CAP}")
        loaded = implementor.expect([CAP_CREATED_REPLY, NO_SUCH_OBJECT_REPLY])
        assert CAP_CREATED_REPLY in loaded, loaded

    goto(implementor, world.ROOM_ARENA_CENTRE)
    dropped = implementor.command("drop all")
    assert dropped.text.count(CAP_DROPPED_REPLY) == caps_dropped, dropped.text
    # The onlooker reads the drop messages first, so they are not taken for the look's reply.
    onlooker.drain(SETTLE_SECONDS)

    looked = onlooker.command("look").text
    assert OUTPUT_OVERFLOW not in looked, looked
    lines = stripped_lines(looked)
    cap_lines = [index for index, line in enumerate(lines) if line == CAP_FLOOR_LINE]
    assert len(cap_lines) == FLOOR_LIST_LIMIT, looked
    remainder_lines = [index for index, line in enumerate(lines) if line.startswith(REMAINDER_LINE_START)]
    people_lines = [
        index
        for index, line in enumerate(lines)
        if line.startswith(IMPLEMENTOR_LINE_START) and line.endswith(IMPLEMENTOR_LINE_END)
    ]
    assert len(people_lines) == 1, looked
    if remainder_line is None:
        assert remainder_lines == [], looked
        assert cap_lines[-1] < people_lines[0], looked
    else:
        assert [lines[index] for index in remainder_lines] == [remainder_line], looked
        assert cap_lines[-1] < remainder_lines[0] < people_lines[0], looked
