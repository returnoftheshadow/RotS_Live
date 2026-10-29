"""The barter vendor (mob program 33): its stock list, a refused short payment, completed trades,
a deducted stock row, the vendor's refusal of gifts and attacks, and a vendor its options
disable."""

from __future__ import annotations

import pytest

from rots_harness import world
from rots_harness.characters import IMPLEMENTOR, Character
from rots_harness.session import GameSession, Transcript

pytestmark = [pytest.mark.scenario, pytest.mark.vendor]

# Room 1303, the trader's store room in zone 13, with no exits; zone 13 stocks one leather cap.
ROOM_STORE_ROOM = 1303
# Room 1307 in zone 13, where zone 13 resets the odd trader (mob 1307).
ROOM_ODD_TRADE_HALL = 1307

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
CURRENCY_VNUM = 1308
CURRENCY_KEYWORD = "bead"
CURRENCY_NAME = "a copper bead"
# The leather cap (tests/integration/world/obj/11.obj); its keywords are "cap leather".
LEATHER_CAP_NAME = "a leather cap"
# The iron cap and the lead ingot the odd trader sells (tests/integration/world/obj/13.obj). The
# ingot weighs 90000, more than anyone carries: CAN_CARRY_W (utils.h) is 2000 + 1000 * strength,
# and strength tops out at 40 (wizset's range), so at most 42000.
IRON_CAP_NAME = "an iron cap"
LEAD_INGOT_NAME = "a lead ingot"
# How many beads the trader asks for a leather cap, `price 1137 1308x2` (mob/13.mob).
CAP_PRICE_BEADS = 2
# The odd trader's prices (mob/13.mob): `price 1137 1308x2 deduct`, `price 1304 1308x1` and
# `price 1305 1308x1`, listed in that order.
ODD_LEATHER_CAP_PRICE_BEADS = 2
ODD_IRON_CAP_PRICE_BEADS = 1
ODD_INGOT_PRICE_BEADS = 1
# The odd trader's list= option.
ODD_LIST_HEADER = "Step right up!"
# The list header a trader shows without a list= option (VENDOR_DEFAULT_LIST_MESSAGE, shopkeeper.h).
DEFAULT_LIST_HEADER = "What would you like to trade?"
# The virtual numbers of the trader (mob/13.mob) and the leather cap, as the trade log names them.
TRADER_VNUM = 1302
LEATHER_CAP_VNUM = 1137

# The trader's tells (vendor_list, vendor_buy and SPECIAL(barter_vendor), shopkeeper.cpp), and its
# refusal of an attack (keeper_protection, shopkeeper.cpp).
TRADER_TELL_FORMAT = "A trader tells you '{}'"
SHORT_PAYMENT_MARKER = TRADER_TELL_FORMAT.format(
    f"You need {CAP_PRICE_BEADS} x {CURRENCY_NAME} (you have {CAP_PRICE_BEADS - 1})."
)
NO_GIFTS_MARKER = TRADER_TELL_FORMAT.format("I don't take gifts.")
NO_ATTACKS_MARKER = TRADER_TELL_FORMAT.format("Don't even think about it.")
NOTHING_TO_SELL_MARKER = TRADER_TELL_FORMAT.format("I have nothing to sell right now.")
NOT_STOCKED_MARKER = TRADER_TELL_FORMAT.format("I don't have that. Try 'list'.")
# What an attacker reads when a fight with its target has started (do_hit, act_offe.cpp).
FIGHT_ONGOING_MARKER = "You do the best you can!"
# What a buyer reads when the item would take it past its carrying weight (vendor_buy).
TOO_HEAVY_MARKER = "You can't carry that much weight."
# What the old shop commands answer where no shopkeeper serves them (do_not_here, act_othe.cpp).
NOT_HERE_MARKER = "Sorry, but you can't do that here!"
# What the room hears from a trader its options disable (SPECIAL(barter_vendor)), and the boot
# log line naming the missing store= option (parse_vendor_options, vendor_problem_line in
# mob_options.cpp).
NOT_TRADING_MARKER = "A broken trader says 'I'm not trading right now.'"
STORE_MISSING_LOG_LINE = "MOB ERROR: mobile #1309: store missing - vendor disabled"
# The marker of a SYSERR line, which fails a harness run (CrashMonitor); a problem with a mob's
# options is logged without it.
SYSERR_MARKER = "SYSERR"


def reply_text(reply: Transcript) -> str:
    """The reply with the carriage returns of the game's line ends removed."""
    return reply.text.replace("\r", "")


def assert_lines(reply: Transcript, lines: tuple) -> None:
    """Asserts the reply holds lines, consecutively and in order."""
    block = "\n".join(lines)
    assert block in reply_text(reply), f"expected:\n{block}\nin:\n{reply.text}"


def stock_lines(header: str, rows: tuple) -> tuple:
    """The lines `list` shows: the header, then one numbered row per (name, cost) pair.

    format_vendor_list (shopkeeper.cpp) pads every name to the longest and puts the cost two
    spaces after it; each cost here is one currency, so each row is one line.
    """
    width = max(len(name) for name, _ in rows)
    numbered = tuple(f"{number:2d}. {name:<{width}}  {cost}" for number, (name, cost) in enumerate(rows, start=1))
    return (header,) + numbered


def cost_text(beads: int) -> str:
    """One currency's cost as the list and the trade show it: "<quantity> x <short description>"."""
    return f"{beads} x {CURRENCY_NAME}"


def trade_lines(beads: int, item_name: str) -> tuple:
    """What the buyer reads when a trade for beads completes (vendor_buy): one line per currency."""
    return ("You hand over:", f"  {cost_text(beads)}", f"You now have {item_name}.")


def give_beads(implementor: GameSession, room_vnum: int, room_name: str, buyer: Character, count: int) -> None:
    """Brings the implementor to room_vnum and gives the buyer count copper beads there."""
    arrived = implementor.command(f"goto {room_vnum}")
    assert arrived.room_name() == room_name, arrived.text
    for bead_number in range(count):
        created = implementor.command(f"load obj {CURRENCY_VNUM}")
        assert created.contains(f"You create {CURRENCY_NAME}."), created.text
        given = implementor.command(f"give {CURRENCY_KEYWORD} {buyer.name.lower()}")
        assert given.contains(f"You give {CURRENCY_NAME} to {buyer.name}."), given.text


def text_seen_before_a_say(actor: GameSession, actor_character: Character, observer: GameSession) -> str:
    """What observer has read up to the actor's next `say`.

    The say reaches observer after anything the actor's earlier commands showed the room.
    """
    actor.command("say done")
    return observer.expect([f"{actor_character.name} says '"]).replace("\r", "")


def carried_count(buyer: GameSession, item_name: str) -> int:
    """How many copies of item_name the buyer's inventory lists, one line each."""
    lines = reply_text(buyer.command("inventory")).split("\n")
    return sum(1 for line in lines if line.strip() == item_name)


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(BUYER)
def test_a_vendor_lists_its_stocked_item_with_its_price(server) -> None:
    """Pins a barter vendor's `list` (PR #330).

    The trader lists each priced item with a copy in its store room once, with each currency's
    quantity and short description, under the default header. Nothing on this path draws.
    """
    buyer = server.login(BUYER.name)

    expected = stock_lines(DEFAULT_LIST_HEADER, ((LEATHER_CAP_NAME, cost_text(CAP_PRICE_BEADS)),))
    assert_lines(buyer.command("list"), expected)


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, BUYER)
def test_a_vendor_refuses_a_short_payment_and_takes_nothing(server) -> None:
    """Pins that a barter vendor names what is missing and takes nothing (PR #330).

    With one of the two beads the cap costs, `buy cap` gets one tell naming the shortfall, and
    the buyer keeps the bead and gets no cap. Nothing on this path draws.
    """
    implementor = server.login(IMPLEMENTOR.name)
    buyer = server.login(BUYER.name)
    give_beads(implementor, world.ROOM_TRADE_HALL, "Trade Hall", BUYER, CAP_PRICE_BEADS - 1)

    assert buyer.command("buy cap").contains(SHORT_PAYMENT_MARKER)
    assert carried_count(buyer, CURRENCY_NAME) == CAP_PRICE_BEADS - 1
    assert LEATHER_CAP_NAME not in buyer.command("inventory").text


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, BUYER)
def test_a_vendor_trades_a_fresh_copy_for_the_priced_beads_and_logs_the_trade(server) -> None:
    """Pins a completed barter trade, what the room sees and the trade log line (PR #330).

    With both beads in loose inventory, `buy cap` takes the two beads and hands over a cap,
    listing the payment one currency per line. The price line has no `deduct`, so the cap is a
    fresh copy and the store's copy stays listed. The implementor, in the same room, reads
    `$n buys $p.` before the buyer's next `say`, and game.log holds the `VENDOR:` line. Nothing
    on this path draws.
    """
    implementor = server.login(IMPLEMENTOR.name)
    buyer = server.login(BUYER.name)
    give_beads(implementor, world.ROOM_TRADE_HALL, "Trade Hall", BUYER, CAP_PRICE_BEADS)

    assert_lines(buyer.command("buy cap"), trade_lines(CAP_PRICE_BEADS, LEATHER_CAP_NAME))
    seen_by_implementor = text_seen_before_a_say(buyer, BUYER, implementor)
    assert f"{BUYER.name} buys {LEATHER_CAP_NAME}." in seen_by_implementor, seen_by_implementor
    assert carried_count(buyer, CURRENCY_NAME) == 0
    assert buyer.command("inventory").contains(LEATHER_CAP_NAME)
    still_listed = stock_lines(DEFAULT_LIST_HEADER, ((LEATHER_CAP_NAME, cost_text(CAP_PRICE_BEADS)),))
    assert_lines(buyer.command("list"), still_listed)

    trade_log_line = (
        f"VENDOR: {BUYER.name} buys {LEATHER_CAP_NAME} ({LEATHER_CAP_VNUM}) from mobile #{TRADER_VNUM}, "
        f"paid {cost_text(CAP_PRICE_BEADS)} ({CURRENCY_VNUM})"
    )
    game_log = (server.run_dir / "game.log").read_text(encoding="utf-8", errors="replace")
    assert trade_log_line in game_log, game_log[-3000:]


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, BUYER)
def test_a_vendor_with_an_empty_store_has_nothing_to_sell(server) -> None:
    """Pins a barter vendor whose store room is empty (PR #330).

    The implementor takes the store room's only leather cap. A row is listed only while its
    item has a copy in the store, so `list` gets the nothing-to-sell tell and `buy cap` the
    not-stocked tell. Nothing on this path draws.
    """
    implementor = server.login(IMPLEMENTOR.name)
    buyer = server.login(BUYER.name)
    arrived = implementor.command(f"goto {ROOM_STORE_ROOM}")
    assert arrived.room_name() == "Store Room", arrived.text
    assert implementor.command("get cap").contains(f"You get {LEATHER_CAP_NAME}.")

    assert buyer.command("list").contains(NOTHING_TO_SELL_MARKER)
    assert buyer.command("buy cap").contains(NOT_STOCKED_MARKER)


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, BUYER)
def test_a_vendor_refuses_a_gift(server) -> None:
    """Pins that a barter vendor refuses what a player gives it, and the player keeps it (PR #330).

    SPECIAL(barter_vendor) answers `give` aimed at itself with a tell before do_give runs.
    Nothing on this path draws.
    """
    implementor = server.login(IMPLEMENTOR.name)
    buyer = server.login(BUYER.name)
    give_beads(implementor, world.ROOM_TRADE_HALL, "Trade Hall", BUYER, 1)

    assert buyer.command(f"give {CURRENCY_KEYWORD} trader").contains(NO_GIFTS_MARKER)
    assert carried_count(buyer, CURRENCY_NAME) == 1


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(BUYER)
def test_a_vendor_refuses_an_attack_and_no_fight_starts(server) -> None:
    """Pins that a barter vendor cancels an attack on it with a tell (PR #330).

    `kill trader` reaches hit() (fight.cpp). Whatever its rolls decide, every path asks the
    trader's program the SPECIAL_DAMAGE question before any damage lands: a blow through
    damage(), and a miss, dodge or parry after it. The only roll-decided path that skips it is a
    riposte, which needs a wielded weapon the trader does not have (check_riposte), so the
    refusal does not depend on a roll. keeper_protection (shopkeeper.cpp) answers with the tell,
    and damage() stops the attacker fighting, so a second `kill trader` is a new attack refused
    the same way, not do_hit's answer to a fight already under way.
    """
    buyer = server.login(BUYER.name)

    assert buyer.command("kill trader").contains(NO_ATTACKS_MARKER)
    second_attack = buyer.command("kill trader")
    assert second_attack.contains(NO_ATTACKS_MARKER), second_attack.text
    assert FIGHT_ONGOING_MARKER not in second_attack.text, second_attack.text


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(ODD_BUYER)
def test_a_vendor_lists_its_header_and_counts_only_deducted_rows(server) -> None:
    """Pins a barter vendor's list= header and its `(N left)` count (PR #330).

    The odd trader lists its three priced items in price-line order under its own header. Only
    the leather cap's line has `deduct`, so only its row shows how many copies the store holds;
    the other rows sell fresh copies and show no count. Nothing on this path draws.
    """
    buyer = server.login(ODD_BUYER.name)

    expected = stock_lines(
        ODD_LIST_HEADER,
        (
            (f"{LEATHER_CAP_NAME} (1 left)", cost_text(ODD_LEATHER_CAP_PRICE_BEADS)),
            (IRON_CAP_NAME, cost_text(ODD_IRON_CAP_PRICE_BEADS)),
            (LEAD_INGOT_NAME, cost_text(ODD_INGOT_PRICE_BEADS)),
        ),
    )
    assert_lines(buyer.command("list"), expected)


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, ODD_BUYER)
def test_a_vendor_sells_by_list_number_and_by_numbered_keyword(server) -> None:
    """Pins `buy <list number>` and `buy <N>.<keyword>` at a barter vendor (PR #330).

    `buy 2` buys the second listed row, the iron cap. `buy 2.cap` buys the second listed row
    whose item answers to `cap`: the leather cap is the first, so the iron cap again. Nothing on
    this path draws.
    """
    implementor = server.login(IMPLEMENTOR.name)
    buyer = server.login(ODD_BUYER.name)
    give_beads(implementor, ROOM_ODD_TRADE_HALL, "Odd Trade Hall", ODD_BUYER, 2 * ODD_IRON_CAP_PRICE_BEADS)

    assert_lines(buyer.command("buy 2"), trade_lines(ODD_IRON_CAP_PRICE_BEADS, IRON_CAP_NAME))
    assert_lines(buyer.command("buy 2.cap"), trade_lines(ODD_IRON_CAP_PRICE_BEADS, IRON_CAP_NAME))
    assert carried_count(buyer, IRON_CAP_NAME) == 2
    assert carried_count(buyer, CURRENCY_NAME) == 0


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, ODD_BUYER)
def test_a_vendor_hands_over_a_deducted_store_copy_and_stops_listing_it(server) -> None:
    """Pins a `deduct` price line: the trade hands over the store's own copy (PR #330).

    `buy cap` matches the leather cap's row first. Its line has `deduct`, so the store's only
    copy is handed over, and the next `list` has the two other rows only. Nothing on this path
    draws.
    """
    implementor = server.login(IMPLEMENTOR.name)
    buyer = server.login(ODD_BUYER.name)
    give_beads(implementor, ROOM_ODD_TRADE_HALL, "Odd Trade Hall", ODD_BUYER, ODD_LEATHER_CAP_PRICE_BEADS)

    assert_lines(buyer.command("buy cap"), trade_lines(ODD_LEATHER_CAP_PRICE_BEADS, LEATHER_CAP_NAME))
    remaining = stock_lines(
        ODD_LIST_HEADER,
        (
            (IRON_CAP_NAME, cost_text(ODD_IRON_CAP_PRICE_BEADS)),
            (LEAD_INGOT_NAME, cost_text(ODD_INGOT_PRICE_BEADS)),
        ),
    )
    after = buyer.command("list")
    assert_lines(after, remaining)
    assert LEATHER_CAP_NAME not in after.text, after.text


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, ODD_BUYER)
def test_a_vendor_refuses_an_item_too_heavy_to_carry_and_keeps_no_payment(server) -> None:
    """Pins a barter vendor's weight check (PR #330).

    `buy 3` asks for the lead ingot, which weighs more than any character can carry. The trade
    stops with the weight message before any payment is taken, so the buyer keeps the bead and
    gets no ingot. Nothing on this path draws.
    """
    implementor = server.login(IMPLEMENTOR.name)
    buyer = server.login(ODD_BUYER.name)
    give_beads(implementor, ROOM_ODD_TRADE_HALL, "Odd Trade Hall", ODD_BUYER, ODD_INGOT_PRICE_BEADS)

    assert buyer.command("buy 3").contains(TOO_HEAVY_MARKER)
    assert carried_count(buyer, CURRENCY_NAME) == ODD_INGOT_PRICE_BEADS
    assert carried_count(buyer, LEAD_INGOT_NAME) == 0


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(STRANDED_BUYER)
def test_a_vendor_without_a_store_option_is_logged_at_boot_and_does_not_trade(server) -> None:
    """Pins a barter vendor its options disable (PR #330).

    The broken trader's options have no store= line. The boot logs a `MOB ERROR` line for it,
    not a SYSERR, and `list` gets the not-trading answer, said to the room. Nothing on this path
    draws.
    """
    buyer = server.login(STRANDED_BUYER.name)

    boot_log = (server.run_dir / "game.log").read_text(encoding="utf-8", errors="replace")
    problem_lines = [line for line in boot_log.splitlines() if STORE_MISSING_LOG_LINE in line]
    assert len(problem_lines) == 1, boot_log[-3000:]
    assert SYSERR_MARKER not in problem_lines[0], problem_lines[0]

    assert buyer.command("list").contains(NOT_TRADING_MARKER)


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(WANDERER)
def test_shop_commands_with_no_keeper_in_the_room_are_not_for_here(server) -> None:
    """Pins the answer to `buy`, `value` and `list` where no keeper serves them (the legacy interpreter).

    With no shopkeeper, vendor or banker in the room, nothing answers the command first, and
    do_not_here (act_othe.cpp) runs. Nothing on this path draws.
    """
    wanderer = server.login(WANDERER.name)

    for command in ("buy cap", "value cap", "list"):
        reply = wanderer.command(command)
        assert reply.contains(NOT_HERE_MARKER), f"{command}: {reply.text}"
