"""A zone reset that dresses its mob: what the squire of zone 26 wears, carries and holds in his
satchel once its M, E, K, G and P rows run through the implementor's `zreset`, and the E row for
a wear slot he has already filled, which makes no item."""

from __future__ import annotations

import time
from pathlib import Path

import pytest

from rots_harness.characters import IMPLEMENTOR
from rots_harness.launcher import GAME_LOG_NAME
from rots_harness.session import GameSession

pytestmark = [pytest.mark.scenario, pytest.mark.zone_dressing]

# The zone zreset takes: zone 26, the harness dressing hall (world/notes/zone-26.md).
DRESSING_ZONE = 26
# Room 2601, the Dressing Hall, where zone 26's M row loads the squire. Its rows run only while
# a player is in zone 26, so the implementor stands here for the reset.
ROOM_DRESSING_HALL = 2601
# What the implementor reads once zreset has run every row (do_zreset, act_wiz.cpp), after the
# zone's real index.
ZONE_RESET_MARKER = ": Harness dressing hall."
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
# What the implementor's `where <vnum>` prints when no object or mob has that vnum
# (perform_immort_where, act_info.cpp).
NOTHING_FOUND_LINE = "Couldn't find any such thing."
# What the reset logs for the second E row into the head slot (reset_zone, zone.cpp).
ALREADY_EQUIPPED_SYSERR = "SYSERR: Char is already equipped: a dressed squire, a spare bascinet"
# Seconds to keep reading after an outcome, for text already sent to arrive.
SETTLE_SECONDS = 1.0
# Seconds to wait for a line to reach game.log: the Docker launcher relays the server's stderr
# apart from the game text, so a line logged before a reply can arrive after it.
LOG_TIMEOUT_SECONDS = 10.0
# Seconds between two reads of game.log while waiting for a line.
LOG_POLL_SECONDS = 0.2


def reset_dressing_zone(implementor: GameSession) -> str:
    """Brings the implementor to the Dressing Hall, resets zone 26, and returns what it read.

    reset_zone runs inside the command, so the room's lines arrive before zreset's own reply.
    """
    arrived = implementor.command(f"goto {ROOM_DRESSING_HALL}")
    assert arrived.room_name() == "Dressing Hall", arrived.text
    reply = implementor.command(f"zreset {DRESSING_ZONE}")
    assert reply.contains(ZONE_RESET_MARKER), reply.text
    return reply.text


def section_lines(text: str, heading: str, next_heading: str | None) -> list[str]:
    """The non-empty lines of text after heading, up to next_heading or the end; fails without heading."""
    assert heading in text, text
    section = text.split(heading, 1)[1]
    if next_heading is not None:
        assert next_heading in section, text
        section = section.split(next_heading, 1)[0]
    return [line.strip() for line in section.splitlines() if line.strip()]


def wait_for_log_count(log_path: Path, line: str, count: int) -> str:
    """Waits until game.log holds line at least count times and returns the log; fails naming the line."""
    deadline = time.monotonic() + LOG_TIMEOUT_SECONDS
    while True:
        text = log_path.read_text(encoding="latin-1")
        if text.count(line) >= count or time.monotonic() >= deadline:
            assert text.count(line) == count, f"game.log has {text.count(line)} of {count} {line!r}:\n{text[-2000:]}"
            return text
        time.sleep(LOG_POLL_SECONDS)


@pytest.mark.schedule("affect-hours")
@pytest.mark.expect_syserr(ALREADY_EQUIPPED_SYSERR)
@pytest.mark.characters(IMPLEMENTOR)
def test_a_zone_reset_dresses_its_mob_from_its_equip_kit_give_and_put_rows(server) -> None:
    """Behaviour pin: zone 26's rows dress the squire as builders wrote them.

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
    room_lines = [line.strip() for line in room_text.splitlines() if line.strip()]
    assert room_lines == [SQUIRE_ARRIVES_LINE, *SQUIRE_WEARS_LINES], reset_text

    implementor.send_line(f"look {SQUIRE_KEYWORD}")
    looked = implementor.expect([INVENTORY_PEEK_HEADING]) + implementor.drain(SETTLE_SECONDS)
    equipment = section_lines(looked, EQUIPMENT_HEADING, INVENTORY_PEEK_HEADING)
    assert equipment == list(SQUIRE_EQUIPMENT_LINES), looked
    peeked_lines = section_lines(looked, INVENTORY_PEEK_HEADING, None)
    # The last line is the prompt.
    inventory = [line for line in peeked_lines if not line.endswith(">")]
    assert inventory == list(SQUIRE_INVENTORY_LINES), looked

    implementor.send_line("stat object acorn")
    acorn = implementor.expect(["Carried by:"]) + implementor.drain(SETTLE_SECONDS)
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
        assert where.contains(NOTHING_FOUND_LINE), where.text
    wait_for_log_count(server.run_dir / GAME_LOG_NAME, ALREADY_EQUIPPED_SYSERR, 1)
