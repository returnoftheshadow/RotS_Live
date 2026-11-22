"""The barter vendor (mob program 33): its stock list, its trades and its refusals.

The tests cover the stock list, a refused short payment, completed trades, a deducted stock row,
the vendor's refusal of gifts and attacks, and a vendor its options disable.
"""

from __future__ import annotations

from collections.abc import Sequence

import pytest

from rots_harness import world
from rots_harness.characters import IMPLEMENTOR, Character
from rots_harness.game_log import log_lines_holding, read_game_log
from rots_harness.game_text import FIGHT_ONGOING_REPLY, GET_FORMAT, NOT_HERE_REPLY
from rots_harness.immortal_commands import give_new_object, goto
from rots_harness.replies import text_seen_before_a_say
from rots_harness.session import GameSession
from rots_harness.text_lines import assert_lines, reply_text

pytestmark = [pytest.mark.scenario, pytest.mark.vendor]

# Room 1303, the trader's store room in zone 13, with no exits; zone 13 stocks one leather cap.
ROOM_STORE_ROOM = 1303
# The name goto shows for ROOM_STORE_ROOM.
ROOM_STORE_ROOM_NAME = "Store Room"
# Room 1307 in zone 13, where zone 13 resets the odd trader (mob 1307).
ROOM_ODD_TRADE_HALL = 1307
# The name goto shows for ROOM_ODD_TRADE_HALL.
ROOM_ODD_TRADE_HALL_NAME = "Odd Trade Hall"

# A level-20 human warrior in the trade hall: a mortal the trader serves.
BUYER = Character(
    name="Testbuyer",
    race="human",
    sex="male",
    level=20,
    load_room=world.ROOM_TRADE_HALL,
    class_name="warrior",
)
# A level-20 human warrior in the odd trade hall: a mortal the odd trader serves.
ODD_BUYER = Character(
    name="Testhaggler",
    race="human",
    sex="male",
    level=20,
    load_room=ROOM_ODD_TRADE_HALL,
    class_name="warrior",
)
# A level-20 human warrior in the arena's west end, where no keeper of any kind stands.
WANDERER = Character(
    name="Testwanderer",
    race="human",
    sex="male",
    level=20,
    load_room=world.ROOM_ARENA_WEST,
    class_name="warrior",
)
# A level-20 human warrior in the broken market, beside the trader with no store room.
STRANDED_BUYER = Character(
    name="Teststranded",
    race="human",
    sex="male",
    level=20,
    load_room=world.ROOM_BROKEN_MARKET,
    class_name="warrior",
)

# The copper bead, the currency of every price line in zone 13 (tests/integration/world/obj/13.obj).
CURRENCY_VNUM = world.OBJECT_COPPER_BEAD
# The keyword the currency answers to.
CURRENCY_KEYWORD = "bead"
# The currency's short description, as the list and the trade show it.
CURRENCY_NAME = world.OBJECT_COPPER_BEAD_SHORT_DESCRIPTION
# The lead ingot the odd trader sells (tests/integration/world/obj/13.obj). It weighs 90000, more
# than anyone carries: CAN_CARRY_W (utils.h) is 2000 + 1000 * strength, and strength tops out at
# 40 (wizset's range), so at most 42000.
LEAD_INGOT_NAME = "a lead ingot"
# How many beads the trader asks for a leather cap, `price 1137 1308x2` (mob/13.mob).
CAP_PRICE_BEADS = 2
# The odd trader's price for the leather cap, its first listed line, `price 1137 1308x2 deduct`
# (mob/13.mob).
ODD_LEATHER_CAP_PRICE_BEADS = 2
# The odd trader's price for the iron cap, its second line, `price 1304 1308x1`.
ODD_IRON_CAP_PRICE_BEADS = 1
# The odd trader's price for the lead ingot, its third line, `price 1305 1308x1`.
ODD_INGOT_PRICE_BEADS = 1
# The odd trader's list= option.
ODD_LIST_HEADER = "Step right up!"
# The list header a trader shows without a list= option (VENDOR_DEFAULT_LIST_MESSAGE,
# shopkeeper.h).
DEFAULT_LIST_HEADER = "What would you like to trade?"
# The trader's virtual number (mob/13.mob), as the trade log names it, and the leather cap's is
# world.OBJECT_LEATHER_CAP.
TRADER_VNUM = 1302

# The trader's tells (vendor_list, vendor_buy and SPECIAL(barter_vendor), shopkeeper.cpp); format
# with what it says.
TRADER_TELL_FORMAT = "A trader tells you '{}'"
# The trader's tell to a buyer one bead short of the cap's price (vendor_buy).
SHORT_PAYMENT_REPLY = TRADER_TELL_FORMAT.format(
    f"You need {CAP_PRICE_BEADS} x {CURRENCY_NAME} (you have {CAP_PRICE_BEADS - 1})."
)
# The trader's tell to a player giving it something (SPECIAL(barter_vendor)).
NO_GIFTS_REPLY = TRADER_TELL_FORMAT.format("I don't take gifts.")
# The trader's refusal of an attack (keeper_protection, shopkeeper.cpp).
NO_ATTACKS_REPLY = TRADER_TELL_FORMAT.format("Don't even think about it.")
# The trader's tell to `list` with nothing in its store (vendor_list).
NOTHING_TO_SELL_REPLY = TRADER_TELL_FORMAT.format("I have nothing to sell right now.")
# The trader's tell to `buy` for an item not in its store (vendor_buy).
NOT_STOCKED_REPLY = TRADER_TELL_FORMAT.format("I don't have that. Try 'list'.")
# What a buyer reads when the item would take it past its carrying weight (vendor_buy).
TOO_HEAVY_REPLY = "You can't carry that much weight."
# What the room hears from a trader its options disable (SPECIAL(barter_vendor)).
NOT_TRADING = "A broken trader says 'I'm not trading right now.'"
# The boot log line naming the missing store= option (parse_vendor_options, vendor_problem_line in
# mob_options.cpp).
STORE_MISSING_LOG_LINE = "MOB ERROR: mobile #1309: store missing - vendor disabled"
# The tag of a SYSERR line, which fails a harness run (CrashMonitor); a problem with a mob's
# options is logged without it.
SYSERR_TAG = "SYSERR"
# How much of the end of game.log a failure message shows.
LOG_TAIL_CHARACTERS = 3000


def stock_lines(header: str, rows: Sequence[tuple[str, str]]) -> tuple[str, ...]:
    """Return the lines `list` shows: the header, then one numbered row per (name, cost) pair.

    format_vendor_list (shopkeeper.cpp) pads every name to the longest and puts the cost two
    spaces after it; each cost here is one currency, so each row is one line.
    """
    width = max(len(name) for name, _ in rows)
    numbered = tuple(
        f"{number:2d}. {name:<{width}}  {cost}" for number, (name, cost) in enumerate(rows, start=1)
    )
    return (header,) + numbered


def cost_text(beads: int) -> str:
    """Return one currency's cost as the list and the trade show it: "<quantity> x <name>"."""
    return f"{beads} x {CURRENCY_NAME}"


def trade_lines(beads: int, item_name: str) -> tuple[str, str, str]:
    """Return what the buyer reads when a trade for beads completes (vendor_buy), one line each."""
    return ("You hand over:", f"  {cost_text(beads)}", f"You now have {item_name}.")


def give_beads(implementor: GameSession, room_vnum: int, room_name: str, buyer: Character, count: int) -> None:
    """Bring the implementor to room_vnum and give the buyer count copper beads there."""
    goto(implementor, room_vnum, room_name)
    for _bead in range(count):
        give_new_object(
            implementor,
            CURRENCY_VNUM,
            CURRENCY_KEYWORD,
            buyer.name.lower(),
            shown_recipient=buyer.name,
            short_description=CURRENCY_NAME,
        )


def carried_count(buyer: GameSession, item_name: str) -> int:
    """Return how many copies of item_name the buyer's inventory lists, one line each."""
    inventory = buyer.command("inventory")
    inventory_text = reply_text(inventory)
    lines = inventory_text.split("\n")
    return sum(1 for line in lines if line.strip() == item_name)


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(BUYER)
def test_a_vendor_lists_its_stocked_item_with_its_price(server) -> None:
    """Pins a barter vendor's `list`.

    The trader lists each priced item with a copy in its store room once, with each currency's
    quantity and short description, under the default header. Nothing on this path draws.
    """
    buyer = server.login(BUYER.name)

    cap_row = (world.OBJECT_LEATHER_CAP_SHORT_DESCRIPTION, cost_text(CAP_PRICE_BEADS))
    expected = stock_lines(DEFAULT_LIST_HEADER, (cap_row,))
    listed = buyer.command("list")
    assert_lines(listed, expected)


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, BUYER)
def test_a_vendor_refuses_a_short_payment_and_takes_nothing(server) -> None:
    """Pins that a barter vendor names what is missing and takes nothing.

    With one of the two beads the cap costs, `buy cap` gets one tell naming the shortfall, and
    the buyer keeps the bead and gets no cap. Nothing on this path draws.
    """
    implementor = server.login(IMPLEMENTOR.name)
    buyer = server.login(BUYER.name)
    give_beads(implementor, world.ROOM_TRADE_HALL, world.ROOM_TRADE_HALL_NAME, BUYER, CAP_PRICE_BEADS - 1)

    refused = buyer.command("buy cap")
    assert refused.contains(SHORT_PAYMENT_REPLY), refused.text
    beads = carried_count(buyer, CURRENCY_NAME)
    assert beads == CAP_PRICE_BEADS - 1, beads
    inventory = buyer.command("inventory")
    assert world.OBJECT_LEATHER_CAP_SHORT_DESCRIPTION not in inventory.text, inventory.text


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, BUYER)
def test_a_vendor_trades_a_fresh_copy_for_the_priced_beads_and_logs_the_trade(server) -> None:
    """Pins a completed barter trade, what the room sees and the trade log line.

    With both beads in loose inventory, `buy cap` takes the two beads and hands over a cap,
    listing the payment one currency per line. The price line has no `deduct`, so the cap is a
    fresh copy and the store's copy stays listed. The implementor, in the same room, reads
    `$n buys $p.` before the buyer's next `say`, and game.log holds the `VENDOR:` line. Nothing
    on this path draws.
    """
    implementor = server.login(IMPLEMENTOR.name)
    buyer = server.login(BUYER.name)
    give_beads(implementor, world.ROOM_TRADE_HALL, world.ROOM_TRADE_HALL_NAME, BUYER, CAP_PRICE_BEADS)

    bought = buyer.command("buy cap")
    assert_lines(bought, trade_lines(CAP_PRICE_BEADS, world.OBJECT_LEATHER_CAP_SHORT_DESCRIPTION))
    seen_by_implementor = text_seen_before_a_say(buyer, BUYER.name, implementor)
    bought_seen = f"{BUYER.name} buys {world.OBJECT_LEATHER_CAP_SHORT_DESCRIPTION}."
    assert bought_seen in seen_by_implementor, seen_by_implementor
    beads = carried_count(buyer, CURRENCY_NAME)
    assert beads == 0, beads
    inventory = buyer.command("inventory")
    assert inventory.contains(world.OBJECT_LEATHER_CAP_SHORT_DESCRIPTION), inventory.text
    cap_row = (world.OBJECT_LEATHER_CAP_SHORT_DESCRIPTION, cost_text(CAP_PRICE_BEADS))
    still_listed = stock_lines(DEFAULT_LIST_HEADER, (cap_row,))
    listed = buyer.command("list")
    assert_lines(listed, still_listed)

    trade_log_line = (
        f"VENDOR: {BUYER.name} buys {world.OBJECT_LEATHER_CAP_SHORT_DESCRIPTION} ({world.OBJECT_LEATHER_CAP}) "
        f"from mobile #{TRADER_VNUM}, paid {cost_text(CAP_PRICE_BEADS)} ({CURRENCY_VNUM})"
    )
    game_log = read_game_log(server.handle.log_path)
    assert trade_log_line in game_log, game_log[-LOG_TAIL_CHARACTERS:]


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, BUYER)
def test_a_vendor_with_an_empty_store_has_nothing_to_sell(server) -> None:
    """Pins a barter vendor whose store room is empty.

    The implementor takes the store room's only leather cap. A row is listed only while its
    item has a copy in the store, so `list` gets the nothing-to-sell tell and `buy cap` the
    not-stocked tell. Nothing on this path draws.
    """
    implementor = server.login(IMPLEMENTOR.name)
    buyer = server.login(BUYER.name)
    goto(implementor, ROOM_STORE_ROOM, ROOM_STORE_ROOM_NAME)
    taken = implementor.command("get cap")
    assert taken.contains(GET_FORMAT.format(world.OBJECT_LEATHER_CAP_SHORT_DESCRIPTION)), taken.text

    listed = buyer.command("list")
    assert listed.contains(NOTHING_TO_SELL_REPLY), listed.text
    refused = buyer.command("buy cap")
    assert refused.contains(NOT_STOCKED_REPLY), refused.text


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, BUYER)
def test_a_vendor_refuses_a_gift(server) -> None:
    """Pins that a barter vendor refuses what a player gives it, which the player keeps.

    SPECIAL(barter_vendor) answers `give` aimed at itself with a tell before do_give runs.
    Nothing on this path draws.
    """
    implementor = server.login(IMPLEMENTOR.name)
    buyer = server.login(BUYER.name)
    give_beads(implementor, world.ROOM_TRADE_HALL, world.ROOM_TRADE_HALL_NAME, BUYER, 1)

    refused = buyer.command(f"give {CURRENCY_KEYWORD} trader")
    assert refused.contains(NO_GIFTS_REPLY), refused.text
    beads = carried_count(buyer, CURRENCY_NAME)
    assert beads == 1, beads


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(BUYER)
def test_a_vendor_refuses_an_attack_and_no_fight_starts(server) -> None:
    """Pins that a barter vendor cancels an attack on it with a tell.

    `kill trader` reaches hit() (fight.cpp). Whatever its rolls decide, every path asks the
    trader's program the SPECIAL_DAMAGE question before any damage lands: a blow through
    damage(), and a miss, dodge or parry after it. The only roll-decided path that skips it is a
    riposte, which needs a wielded weapon the trader does not have (check_riposte), so the
    refusal does not depend on a roll. keeper_protection (shopkeeper.cpp) answers with the tell,
    and damage() stops the attacker fighting, so a second `kill trader` is a new attack refused
    the same way, not do_hit's answer to a fight already under way.
    """
    buyer = server.login(BUYER.name)

    first_attack = buyer.command("kill trader")
    assert first_attack.contains(NO_ATTACKS_REPLY), first_attack.text
    second_attack = buyer.command("kill trader")
    assert second_attack.contains(NO_ATTACKS_REPLY), second_attack.text
    assert FIGHT_ONGOING_REPLY not in second_attack.text, second_attack.text


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(ODD_BUYER)
def test_a_vendor_lists_its_header_and_counts_only_deducted_rows(server) -> None:
    """Pins a barter vendor's list= header and its `(N left)` count.

    The odd trader lists its three priced items in price-line order under its own header. Only
    the leather cap's line has `deduct`, so only its row shows how many copies the store holds;
    the other rows sell fresh copies and show no count. Nothing on this path draws.
    """
    buyer = server.login(ODD_BUYER.name)

    expected = stock_lines(
        ODD_LIST_HEADER,
        (
            (f"{world.OBJECT_LEATHER_CAP_SHORT_DESCRIPTION} (1 left)", cost_text(ODD_LEATHER_CAP_PRICE_BEADS)),
            (world.OBJECT_IRON_CAP_SHORT_DESCRIPTION, cost_text(ODD_IRON_CAP_PRICE_BEADS)),
            (LEAD_INGOT_NAME, cost_text(ODD_INGOT_PRICE_BEADS)),
        ),
    )
    listed = buyer.command("list")
    assert_lines(listed, expected)


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, ODD_BUYER)
def test_a_vendor_sells_by_list_number_and_by_numbered_keyword(server) -> None:
    """Pins `buy <list number>` and `buy <N>.<keyword>` at a barter vendor.

    `buy 2` buys the second listed row, the iron cap. `buy 2.cap` buys the second listed row
    whose item answers to `cap`: the leather cap is the first, so the iron cap again. Nothing on
    this path draws.
    """
    implementor = server.login(IMPLEMENTOR.name)
    buyer = server.login(ODD_BUYER.name)
    give_beads(implementor, ROOM_ODD_TRADE_HALL, ROOM_ODD_TRADE_HALL_NAME, ODD_BUYER, 2 * ODD_IRON_CAP_PRICE_BEADS)

    iron_cap_trade = trade_lines(ODD_IRON_CAP_PRICE_BEADS, world.OBJECT_IRON_CAP_SHORT_DESCRIPTION)
    by_number = buyer.command("buy 2")
    assert_lines(by_number, iron_cap_trade)
    by_keyword = buyer.command("buy 2.cap")
    assert_lines(by_keyword, iron_cap_trade)
    caps = carried_count(buyer, world.OBJECT_IRON_CAP_SHORT_DESCRIPTION)
    assert caps == 2, caps
    beads = carried_count(buyer, CURRENCY_NAME)
    assert beads == 0, beads


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, ODD_BUYER)
def test_a_vendor_hands_over_a_deducted_store_copy_and_stops_listing_it(server) -> None:
    """Pins a `deduct` price line: the trade hands over the store's own copy.

    `buy cap` matches the leather cap's row first. Its line has `deduct`, so the store's only
    copy is handed over, and the next `list` has the two other rows only. Nothing on this path
    draws.
    """
    implementor = server.login(IMPLEMENTOR.name)
    buyer = server.login(ODD_BUYER.name)
    give_beads(implementor, ROOM_ODD_TRADE_HALL, ROOM_ODD_TRADE_HALL_NAME, ODD_BUYER, ODD_LEATHER_CAP_PRICE_BEADS)

    bought = buyer.command("buy cap")
    assert_lines(bought, trade_lines(ODD_LEATHER_CAP_PRICE_BEADS, world.OBJECT_LEATHER_CAP_SHORT_DESCRIPTION))
    remaining = stock_lines(
        ODD_LIST_HEADER,
        (
            (world.OBJECT_IRON_CAP_SHORT_DESCRIPTION, cost_text(ODD_IRON_CAP_PRICE_BEADS)),
            (LEAD_INGOT_NAME, cost_text(ODD_INGOT_PRICE_BEADS)),
        ),
    )
    after = buyer.command("list")
    assert_lines(after, remaining)
    assert world.OBJECT_LEATHER_CAP_SHORT_DESCRIPTION not in after.text, after.text


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, ODD_BUYER)
def test_a_vendor_refuses_an_item_too_heavy_to_carry_and_keeps_no_payment(server) -> None:
    """Pins a barter vendor's weight check.

    `buy 3` asks for the lead ingot, which weighs more than any character can carry. The trade
    stops with the weight message before any payment is taken, so the buyer keeps the bead and
    gets no ingot. Nothing on this path draws.
    """
    implementor = server.login(IMPLEMENTOR.name)
    buyer = server.login(ODD_BUYER.name)
    give_beads(implementor, ROOM_ODD_TRADE_HALL, ROOM_ODD_TRADE_HALL_NAME, ODD_BUYER, ODD_INGOT_PRICE_BEADS)

    refused = buyer.command("buy 3")
    assert refused.contains(TOO_HEAVY_REPLY), refused.text
    beads = carried_count(buyer, CURRENCY_NAME)
    assert beads == ODD_INGOT_PRICE_BEADS, beads
    ingots = carried_count(buyer, LEAD_INGOT_NAME)
    assert ingots == 0, ingots


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(STRANDED_BUYER)
def test_a_vendor_without_a_store_option_is_logged_at_boot_and_does_not_trade(server) -> None:
    """Pins a barter vendor its options disable.

    The broken trader's options have no store= line. The boot logs a `MOB ERROR` line for it,
    not a SYSERR, and `list` gets the not-trading answer, said to the room. Nothing on this path
    draws.
    """
    buyer = server.login(STRANDED_BUYER.name)

    problem_lines = log_lines_holding(server.handle.log_path, STORE_MISSING_LOG_LINE)
    assert len(problem_lines) == 1, read_game_log(server.handle.log_path)[-LOG_TAIL_CHARACTERS:]
    assert SYSERR_TAG not in problem_lines[0], problem_lines[0]

    listed = buyer.command("list")
    assert listed.contains(NOT_TRADING), listed.text


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(WANDERER)
def test_shop_commands_with_no_keeper_in_the_room_are_not_for_here(server) -> None:
    """Pins the answer to `buy`, `value` and `list` where no keeper serves them.

    With no shopkeeper, vendor or banker in the room, nothing answers the command first, and the
    legacy interpreter's do_not_here (act_othe.cpp) runs. Nothing on this path draws.
    """
    wanderer = server.login(WANDERER.name)

    for command in ("buy cap", "value cap", "list"):
        reply = wanderer.command(command)
        assert reply.contains(NOT_HERE_REPLY), f"{command}: {reply.text}"
