"""Known bugs in buying and selling, each pinned as a strict xfail of the correct behaviour: the
pin fails the run once the bug is fixed, so the fix also removes the xfail."""

from __future__ import annotations

import pytest

from rots_harness import world
from rots_harness.characters import IMPLEMENTOR
from rots_harness.game_text import NOT_HERE_REPLY
from rots_harness.immortal_commands import give_new_object, goto
from rots_harness.text_lines import reply_text
from test_vendor import BUYER, CAP_PRICE_BEADS, WANDERER

pytestmark = [pytest.mark.scenario, pytest.mark.vendor]

# The copper bead, the currency of the trader's price line `price 1137 1308x2`
# (tests/integration/world/mob/13.mob and obj/13.obj).
CURRENCY_VNUM = 1308
CURRENCY_KEYWORD = "bead"
CURRENCY_NAME = world.OBJECT_COPPER_BEAD_SHORT_DESCRIPTION
# The three-letter abbreviation of the cap's keyword `leather`.
CAP_ABBREVIATION = "lea"

# What the buyer reads when the trade completes (vendor_buy, shopkeeper.cpp).
TRADE_LINES = (
    "You hand over:",
    f"  {CAP_PRICE_BEADS} x {CURRENCY_NAME}",
    f"You now have {world.OBJECT_LEATHER_CAP_SHORT_DESCRIPTION}.",
)


@pytest.mark.xfail(
    strict=True,
    raises=AssertionError,
    reason="`sell` where no shopkeeper serves it should answer 'Sorry, but you can't do that here!' as buy, value and list do",
)
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, WANDERER)
def test_sell_with_no_keeper_in_the_room_is_not_for_here(server) -> None:
    """Bug: `sell` where no keeper serves it is swallowed without a word.

    command_interpreter (interpre.cpp:1429-1433) sets may_not_perform for every CMD_SELL that no
    SPECIAL_COMMAND program answered, a TEMPORARY guard against a crash in the SPECIAL_TARGET
    call, so do_not_here (act_othe.cpp:140) never runs for `sell`. `buy`, `value` and `list`
    in the same room do reach it (test_vendor.py's no-keeper pin). The wanderer, holding a cap
    so the command has something to sell, reads only the prompt. Nothing on this path draws.
    """
    implementor = server.login(IMPLEMENTOR.name)
    wanderer = server.login(WANDERER.name)
    goto(implementor, world.ROOM_ARENA_WEST, world.ROOM_ARENA_WEST_NAME)
    give_new_object(implementor, world.OBJECT_LEATHER_CAP, "cap", WANDERER.name.lower(), shown_recipient=WANDERER.name, short_description=world.OBJECT_LEATHER_CAP_SHORT_DESCRIPTION)

    reply = wanderer.command("sell cap")
    assert reply.contains(NOT_HERE_REPLY), reply.text


@pytest.mark.xfail(
    strict=True,
    raises=AssertionError,
    reason="`buy lea` should buy the leather cap, matching an abbreviated keyword as deposit and withdraw do",
)
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, BUYER)
def test_a_vendor_sells_by_an_abbreviated_keyword(server) -> None:
    """Bug: a barter vendor refuses an abbreviated keyword that the banker accepts.

    vendor_buy (shopkeeper.cpp:679) matches with `isname(name, s.copy->name)`, whose `full`
    defaults to 1 (handler.h), so `lea` does not match the cap's keyword `leather` and the
    trader answers 'I don't have that. Try 'list'.' bank_deposit finds items through
    get_obj_in_list_vis (handler.cpp:2252) and bank_withdraw through `isname(..., 0)`
    (banker.cpp:1004); isname (handler.cpp:210-223) accepts a 3- or 4-letter prefix there, so
    `deposit lea` and `withdraw lea` take the cap (test_bank.py's withdrawal pin). Nothing on
    this path draws.
    """
    implementor = server.login(IMPLEMENTOR.name)
    buyer = server.login(BUYER.name)
    goto(implementor, world.ROOM_TRADE_HALL, world.ROOM_TRADE_HALL_NAME)
    for bead_number in range(CAP_PRICE_BEADS):
        give_new_object(implementor, CURRENCY_VNUM, CURRENCY_KEYWORD, BUYER.name.lower(), shown_recipient=BUYER.name, short_description=CURRENCY_NAME)

    reply = buyer.command(f"buy {CAP_ABBREVIATION}")
    block = "\n".join(TRADE_LINES)
    assert block in reply_text(reply), reply.text
