"""A zone reset that dresses its mob: the squire of zone 26 and its M, E, K, G and P rows.

What the squire wears, carries and holds in his satchel once the rows run through the
implementor's `zreset`, and the E row for a wear slot he has already filled, which makes no item.
"""

from __future__ import annotations

import pytest

from rots_harness.characters import IMPLEMENTOR
from rots_harness.game_log import wait_for_log_count
from rots_harness.game_text import NOTHING_FOUND_REPLY
from rots_harness.immortal_commands import goto
from rots_harness.replies import reply_to
from rots_harness.session import GameSession
from rots_harness.stat_reading import read_stat
from rots_harness.text_lines import lines_after, stripped_lines

pytestmark = [pytest.mark.scenario, pytest.mark.zone_dressing]

# The zone zreset takes: zone 26, the harness dressing hall (world/notes/zone-26.md).
DRESSING_ZONE = 26
# Room 2601, the Dressing Hall, where zone 26's M row loads the squire. Its rows run only while
# a player is in zone 26, so the implementor stands here for the reset.
ROOM_DRESSING_HALL = 2601
# What the implementor reads once zreset has run every row (do_zreset, act_wiz.cpp), after the
# zone's real index.
ZONE_RESET_REPLY_END = ": Harness dressing hall."
# How that reply starts; the room's lines come before it.
ZONE_RESET_REPLY_START = "Reset zone "
# The keyword that finds the squire, mob 2600, and nothing else in the world.
SQUIRE_KEYWORD = "squire"
# What the room reads as the M row puts the squire in the hall (reset_zone, zone.cpp).
SQUIRE_ARRIVES_LINE = "A dressed squire arrives."
# What the room reads as the K row's `wear all` puts on what it gave him (wear_message,
# act_obj2.cpp): the K row gives the sabatons, the tabard and the whetstone in that order, each
# to the head of his inventory, so `wear all` meets the tabard before the sabatons and skips the
# whetstone, which nothing can wear.
SQUIRE_WEARS_LINES = (
    "A dressed squire wears a blue tabard about his body.",
    "A dressed squire wears a pair of steel sabatons on his feet.",
)
# The heading of what `look squire` lists as worn (show_char_to_char, act_info.cpp): act() puts
# "\n\r" before the name, so the name is not capitalised.
EQUIPMENT_HEADING = "a dressed squire is using:"
# The lines `look squire` gives under that heading (show_equipment_to_char, act_info.cpp), in slot
# order: the E row's sallet, then what the K row's `wear all` put on.
SQUIRE_EQUIPMENT_LINES = (
    "<worn on head>         an iron sallet",
    "<worn on feet>         a pair of steel sabatons",
    "<worn about body>      a blue tabard",
)
# The heading of the inventory an immortal's `look` peeks at (show_char_to_char, act_info.cpp).
# Each item is listed when number(0, 20) is below the looker's level, which the implementor's 100
# always is.
INVENTORY_PEEK_HEADING = "You attempt to peek at the inventory:"
# What the peek lists: the G row's satchel, given last and so first, then the whetstone the K
# row gave and nothing could wear.
SQUIRE_INVENTORY_LINES = ("a leather satchel", "a whetstone")
# The `stat object acorn` line placing the acorn (do_stat_object, act_wiz.cpp): the P row after
# the G row puts it in the last object loaded, the satchel.
ACORN_LOCATION_LINE = "In object: a leather satchel, Carried by: Nobody"
# The object vnum of the E row refused for the filled head slot.
SPARE_BASCINET_VNUM = 2611
# The object vnum of the P row after it, which puts a flint in the last object loaded.
STRAY_FLINT_VNUM = 2612
# What the reset logs for the second E row into the head slot (reset_zone, zone.cpp).
ALREADY_EQUIPPED_SYSERR = "SYSERR: Char is already equipped: a dressed squire, a spare bascinet"


def reset_dressing_zone(implementor: GameSession) -> str:
    """Bring the implementor to the Dressing Hall, reset zone 26, and return what it read.

    reset_zone runs inside the command, so the room's lines arrive before zreset's own reply.
    """
    goto(implementor, ROOM_DRESSING_HALL, "Dressing Hall")
    reply = implementor.command(f"zreset {DRESSING_ZONE}")
    assert reply.contains(ZONE_RESET_REPLY_END), reply.text
    return reply.text


@pytest.mark.schedule("affect-hours")
@pytest.mark.expect_syserr(ALREADY_EQUIPPED_SYSERR)
@pytest.mark.characters(IMPLEMENTOR)
def test_a_zone_reset_dresses_its_mob_from_its_equip_kit_give_and_put_rows(server) -> None:
    """Pins that zone 26's rows dress the squire as builders wrote them.

    The E row's sallet is worn on his head, the K row's sabatons and tabard are worn and its
    whetstone carried, the G row's satchel carried, and the P row after it puts the acorn in the
    satchel. The room reads him arrive and put on the K row's gear, and nothing else. The reset
    also refuses the second E row into his head slot and logs that it did (the fix pin below).
    No draw decides any row: each E, G and P row loads at 100 percent, and the inventory peek's
    number(0, 20) is always below the implementor's level.
    """
    implementor = server.login(IMPLEMENTOR.name)
    reset_text = reset_dressing_zone(implementor)
    room_text = reset_text.split(ZONE_RESET_REPLY_START, 1)[0]
    room_lines = stripped_lines(room_text)
    assert room_lines == [SQUIRE_ARRIVES_LINE, *SQUIRE_WEARS_LINES], reset_text

    looked = reply_to(implementor, f"look {SQUIRE_KEYWORD}", [INVENTORY_PEEK_HEADING])
    equipment = lines_after(looked, EQUIPMENT_HEADING, INVENTORY_PEEK_HEADING, drop_prompts=False)
    assert equipment == list(SQUIRE_EQUIPMENT_LINES), looked
    # The last line is the prompt, which lines_after drops.
    inventory = lines_after(looked, INVENTORY_PEEK_HEADING)
    assert inventory == list(SQUIRE_INVENTORY_LINES), looked

    acorn = read_stat(implementor, "object acorn", last_line="Carried by:")
    assert ACORN_LOCATION_LINE in acorn, acorn


@pytest.mark.schedule("affect-hours")
@pytest.mark.expect_syserr(ALREADY_EQUIPPED_SYSERR)
@pytest.mark.characters(IMPLEMENTOR)
def test_a_reset_row_for_a_wear_slot_its_mob_has_filled_makes_no_item(server) -> None:
    """Fix pin (#534): the E row for the head slot the sallet already fills makes no bascinet.

    The reset logs the refusal once, and the P row after it, which puts a flint into the last
    object loaded, has nothing to go in, so no flint is made either. #534 made the row make no
    bascinet; before it, the reset made the bascinet anyway and equip_char refused it, leaving it
    held by nobody, at room -1 and stored in itself, with the flint stored in it.
    """
    implementor = server.login(IMPLEMENTOR.name)
    reset_dressing_zone(implementor)

    for vnum in (SPARE_BASCINET_VNUM, STRAY_FLINT_VNUM):
        where = implementor.command(f"where {vnum}")
        assert where.contains(NOTHING_FOUND_REPLY), where.text

    wait_for_log_count(server.handle.log_path, ALREADY_EQUIPPED_SYSERR, 1, exact=True)
