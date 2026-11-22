"""The banker (mob program 34) and the staff commands `vault` and `gameoptions`.

Pins an account's vault per side, deposits and withdrawals of items and coins, the vault and
character files on disk, what bystanders see, the banker's refusals, its opening hours and
options, and the staff commands.
"""

from __future__ import annotations

import dataclasses
import itertools
import json
import re
from collections.abc import Sequence
from pathlib import Path

import pytest

from rots_harness import world
from rots_harness.characters import IMPLEMENTOR, Character, StoredValues
from rots_harness.game_log import log_lines_holding, read_game_log
from rots_harness.game_text import FIGHT_ONGOING_REPLY, PUT_FORMAT, UNRECOGNIZED_COMMAND_REPLY
from rots_harness.immortal_commands import give_new_object, goto, load_object, wizset
from rots_harness.replies import SETTLE_SECONDS, reply_to, text_seen_before_a_say
from rots_harness.session import GameSession
from rots_harness.stat_reading import read_stat
from rots_harness.text_lines import assert_lines

pytestmark = [pytest.mark.scenario, pytest.mark.bank]

# Room 1301, the lit bank hall in zone 13, where zone 13 resets the banker (mob 1301).
ROOM_BANK_HALL = 1301
# Room 1304, a DARK hall in zone 13, where zone 13 resets the dark banker (mob 1304).
ROOM_DARK_VAULT_HALL = 1304
# Room 1305 in zone 13, where zone 13 resets the morning banker (mob 1305), open hours 0 to 11.
ROOM_MORNING_BANK = 1305
# Room 1306 in zone 13, where zone 13 resets the evening banker (mob 1306), open hours 12 to 23.
ROOM_EVENING_BANK = 1306

# A level-20 human warrior in the bank hall: a mortal of the light side, which the banker serves.
DEPOSITOR = Character(
    name="Testsaver",
    race="human",
    sex="male",
    level=20,
    load_room=ROOM_BANK_HALL,
    class_name="warrior",
)
# A level-20 dwarf warrior in the bank hall: another light-side character of the same account.
KINSMAN = Character(
    name="Testkin",
    race="dwarf",
    sex="male",
    level=20,
    load_room=ROOM_BANK_HALL,
    class_name="warrior",
)
# A level-20 uruk-hai warrior in the bank hall: a character of the same account on the dark side.
OUTSIDER = Character(
    name="Testuruk",
    race="uruk-hai",
    sex="male",
    level=20,
    load_room=ROOM_BANK_HALL,
    class_name="warrior",
)
# The minimum levels of `vault` (LEVEL_GRGOD, 97) and `gameoptions` (LEVEL_IMPL, 100) are in the
# command table (interpre.cpp). An immortal one level below `vault`.
LESSER_GOD = Character(
    name="Testdemigod",
    race="human",
    sex="male",
    level=96,
    load_room=ROOM_BANK_HALL,
    class_name="warrior",
)
# An immortal at `vault`'s level.
GREATER_GOD = Character(
    name="Testgreatgod",
    race="human",
    sex="male",
    level=97,
    load_room=ROOM_BANK_HALL,
    class_name="warrior",
)
# An immortal one level below `gameoptions`.
ELDER_GOD = Character(
    name="Testeldergod",
    race="human",
    sex="male",
    level=99,
    load_room=ROOM_BANK_HALL,
    class_name="warrior",
)
# A level-20 human warrior in the dark vault hall: a human has no infravision, and neither does
# the dark banker, so neither sees the other there.
DARK_CUSTOMER = Character(
    name="Testgroper",
    race="human",
    sex="male",
    level=20,
    load_room=ROOM_DARK_VAULT_HALL,
    class_name="warrior",
)
# A level-20 human warrior before the morning banker (hours=0-12). Its opening hours and the
# evening banker's (hours=12-0) together cover the day once.
EARLY_CUSTOMER = Character(
    name="Testearly",
    race="human",
    sex="male",
    level=20,
    load_room=ROOM_MORNING_BANK,
    class_name="warrior",
)
# A level-20 human warrior before the evening banker.
LATE_CUSTOMER = Character(
    name="Testlate",
    race="human",
    sex="male",
    level=20,
    load_room=ROOM_EVENING_BANK,
    class_name="warrior",
)
# A level-20 human warrior in the broken market, before the banker its fee=0 option disables.
STRANDED_CUSTOMER = Character(
    name="Teststranded",
    race="human",
    sex="male",
    level=20,
    load_room=world.ROOM_BROKEN_MARKET,
    class_name="warrior",
)

# A brass key: type 18, ITEM_KEY, which Crash_is_unrentable (objsave.cpp) refuses.
KEY_VNUM = 1306
# The brass key's short description.
KEY_SHORT_DESCRIPTION = "a brass key"
# The copper in a purse: 10 gold.
PURSE_COPPER = 10000
# The copper in one gold coin (COPP_IN_GOLD, structs.h).
COPPER_IN_GOLD = 1000
# The depositor stored with a purse of PURSE_COPPER.
PURSED_DEPOSITOR = dataclasses.replace(DEPOSITOR, stored=StoredValues(gold=PURSE_COPPER))
# The depositor stored with 1500 gold, more than a vault holds.
RICH_DEPOSITOR = dataclasses.replace(DEPOSITOR, stored=StoredValues(gold=1500 * COPPER_IN_GOLD))
# The vault's slots in game_boot_options.cpp's defaults; the harness lib's
# lib/misc/game_boot_options.json sets only daily_reboot_hour_utc.
VAULT_SLOTS = 10
# The vault's coin limit in gold, from the same defaults.
VAULT_COIN_LIMIT_GOLD = 1000

# The banker's tells (bank_deposit, bank_withdraw, their coin forms and SPECIAL(banker),
# banker.cpp) and its refusal of an attack (keeper_protection, shopkeeper.cpp); format with the
# words told.
BANKER_TELL_FORMAT = "A banker tells you '{}'"
# The tell for a withdrawal that names nothing in the vault.
NOTHING_LIKE_THAT_REPLY = BANKER_TELL_FORMAT.format("I hold nothing like that for you.")
# The tell for a coin deposit the purse does not cover.
PURSE_SHORT_REPLY = BANKER_TELL_FORMAT.format("You don't have that much.")
# The tell for a coin withdrawal the vault does not cover.
VAULT_SHORT_REPLY = BANKER_TELL_FORMAT.format("You don't have that much with me.")
# The tell for a give aimed at the banker.
NO_GIFTS_REPLY = BANKER_TELL_FORMAT.format("I don't take gifts.")
# The tell for an attack on the banker.
NO_ATTACKS_REPLY = BANKER_TELL_FORMAT.format("Don't even think about it.")
# The tell for an item the banker cannot store.
CANNOT_KEEP_REPLY = BANKER_TELL_FORMAT.format("I can't keep that for you.")
# The tell for a deposit into a full vault.
VAULT_FULL_REPLY = BANKER_TELL_FORMAT.format("Your vault is full.")
# The tell for a coin deposit into a vault at its coin limit.
NO_MORE_COINS_REPLY = BANKER_TELL_FORMAT.format("Your vault can hold no more coins.")
# The banker's refusals to serve at all, said to the room (banker_admits and
# vendor_serves_customer, shopkeeper.cpp); format with the words said.
BANKER_SAYS_FORMAT = "A banker says '{}'"
# The refusal of a customer of no side.
NOT_YOUR_KIND_REPLY = BANKER_SAYS_FORMAT.format("I hold nothing for your kind.")
# The words of the refusal of a customer the banker cannot see, whoever the speaker reads as.
UNSEEN_CUSTOMER_PART = "I don't trade with someone I can't see!"
# The refusal of a customer the banker cannot see, from a banker the customer can see.
UNSEEN_CUSTOMER_REPLY = BANKER_SAYS_FORMAT.format(UNSEEN_CUSTOMER_PART)
# The words of a closed banker's refusal.
CLOSED_PART = "I'm closed. Come back later."
# The broken banker's refusal.
BANK_CLOSED_FOR_NOW_REPLY = "A broken banker says 'The bank is closed for now.'"
# The game hours after which the morning and evening bankers' hours swap.
SWAP_HOURS = 12
# The most hours a mortal can idle without being flagged AFK: point_update (limits.cpp) counts
# one idle tick per hour and flags the character at the fourth.
IDLE_SAFE_HOURS = 3
# The names look shows for the morning and the evening bank rooms (wld/13.wld).
BANK_ROOM_NAMES = ("Morning Bank", "Evening Bank")
# The name look shows for ROOM_BANK_HALL (wld/13.wld).
ROOM_BANK_HALL_NAME = "Bank Hall"
# The boot log line naming the broken banker's bad fee (parse_banker_options, vendor_problem_line
# in mob_options.cpp): fee is the first line of its options.
BAD_FEE_LOG_LINE = "MOB ERROR: mobile #1308, options line 1: bad fee - banker disabled"
# The part of a SYSERR line, which fails a harness run (CrashMonitor); a problem with a mob's
# options is logged without it.
SYSERR_PART = "SYSERR"
# The implementor's reply to `invis` with no argument (do_invis, act_wiz.cpp).
INVISIBLE_REPLY = f"Your invisibility level is {IMPLEMENTOR.level}."
# What `stat file` answers instead of a stat when it cannot show the character.
STAT_FILE_REFUSALS = ["There is no such player.", "Sorry, you can't do that."]
# Seconds staff_reply and the gameoptions listing keep reading after their outcome.
STAFF_SETTLE_SECONDS = 0.5

# The balance header (banker_balance and format_bank_balance, banker.cpp).
BALANCE_HEADER = "Your vault:"
# The item table's header below the coin and slot lines. No fee is configured, so the item table
# has no fee column.
ITEM_TABLE_HEADER = " #  Item"


def coins_line(coins: str) -> str:
    """Return the balance's coin line for an amount as money_message (handler.cpp) writes it."""
    return f"Coins: {coins} (limit {VAULT_COIN_LIMIT_GOLD} gold)"


def slots_line(used: int, slots: int = VAULT_SLOTS) -> str:
    """Return the balance's slot line."""
    return f"Slots: {used} of {slots} used"


def balance_lines(coins: str, item_rows: Sequence[str] = ()) -> tuple[str, ...]:
    """Return the whole balance for a vault holding coins and one row per item, in slot order."""
    lines = (BALANCE_HEADER, coins_line(coins), slots_line(len(item_rows)))
    if not item_rows:
        return lines
    numbered = tuple(f"{number:2d}  {row}" for number, row in enumerate(item_rows, start=1))
    return lines + ("", ITEM_TABLE_HEADER) + numbered


# The balance of an empty vault.
EMPTY_VAULT_LINES = balance_lines("0 copper")
# The balance of a vault holding the leather cap alone.
ONE_CAP_VAULT_LINES = balance_lines("0 copper", (world.OBJECT_LEATHER_CAP_SHORT_DESCRIPTION,))
# What the depositor reads when the banker takes an item (bank_deposit); format with the item.
DEPOSITED_FORMAT = "You hand over {}."
# What the depositor reads when the banker returns an item (bank_withdraw); format with the item.
WITHDRAWN_FORMAT = "You are handed {}."
# What the depositor reads as the banker takes the leather cap.
CAP_DEPOSITED_REPLY = DEPOSITED_FORMAT.format(world.OBJECT_LEATHER_CAP_SHORT_DESCRIPTION)
# What the depositor reads as the banker returns the leather cap.
CAP_WITHDRAWN_REPLY = WITHDRAWN_FORMAT.format(world.OBJECT_LEATHER_CAP_SHORT_DESCRIPTION)
# What bystanders read for an item deposit (bank_deposit); format with the name and the item.
DEPOSITS_FORMAT = "{} deposits {}."
# What bystanders read for an item withdrawal (bank_withdraw); format with the name and the item.
WITHDRAWS_FORMAT = "{} withdraws {}."
# What a coin deposit answers; format with the amount, such as "5 gold".
COINS_DEPOSITED_FORMAT = "You deposit {}."
# What a coin withdrawal answers; format with the amount.
COINS_WITHDRAWN_FORMAT = "You withdraw {}."
# The start of score's gold line; format with the gold in the purse.
SCORE_GOLD_FORMAT = "Gold: {},"
# The part the coin moves share, which a bystander must not read.
GOLD_PART = "gold"
# The start of the banker's tell for a withdrawal that names nothing in the vault.
HOLD_NOTHING_PART = "I hold nothing"
# What the bystander reads as the implementor gives the depositor the cap.
CAP_GIVEN_TO_DEPOSITOR_PART = f"gives {world.OBJECT_LEATHER_CAP_SHORT_DESCRIPTION} to {DEPOSITOR.name}."

# The light side's vault file (bank_side_file_name, banker.cpp), kept in the account's own
# folder beside account.json (vault_location, account_character_directory).
LIGHT_VAULT_FILE_NAME = "vault_light.json"
# The dark side's vault file, beside the light side's.
DARK_VAULT_FILE_NAME = "vault_dark.json"
# The account's own record in its folder.
ACCOUNT_FILE_NAME = "account.json"
# The settings file `gameoptions` writes (BOOT_OPTIONS_PATH, game_boot_options.h; the game's
# working directory is lib/).
BOOT_OPTIONS_FILE = Path("misc") / "game_boot_options.json"

# Staff replies: `vault` (vault_change, banker.cpp) and `gameoptions` (do_gameoptions and
# boot_option_set, game_boot_options.cpp). What `vault take` answers; format with what it took.
VAULT_TAKE_FORMAT = "You take {} from the vault."
# What `vault put` answers; format with what it put.
VAULT_PUT_FORMAT = "You put {} into the vault."
# What `vault take` answers for a slot the vault does not have.
NO_SUCH_SLOT_REPLY = "No such slot."
# What `vault take` answers for a coin amount that is not a number.
HOW_MUCH_REPLY = "How much?"
# What `vault take` answers for an account named by its email.
ACCOUNT_NAME_ONLY_REPLY = "Use the account name shown by 'vault <name>'."
# What `vault put` answers for a full vault.
STAFF_VAULT_FULL_REPLY = "That vault is full."
# The start of `vault <name>`'s view.
ACCOUNT_VIEW_START = "Account: "
# What `gameoptions bank_slots <n>` answers; format with n.
BANK_SLOTS_SET_FORMAT = "bank_slots set to {}. It takes effect at the next reboot."
# What `gameoptions bank_slots` answers for a value out of range.
BANK_SLOTS_RANGE_REPLY = "bank_slots must be 1-100."
# What `gameoptions` answers for an unknown setting.
NO_SUCH_SETTING_REPLY = "No such setting. Type 'gameoptions' for the list."
# What `gameoptions` answers for a missing or malformed number.
GAMEOPTIONS_USAGE_REPLY = "Usage: gameoptions <name> <number>"
# Every reply `vault` and `gameoptions` can give, so a staff command's expect lists each outcome.
STAFF_OUTCOMES = (
    "You take ",
    "You put ",
    NO_SUCH_SLOT_REPLY,
    HOW_MUCH_REPLY,
    ACCOUNT_NAME_ONLY_REPLY,
    STAFF_VAULT_FULL_REPLY,
    "The vault doesn't hold that much.",
    "That would pass the vault's coin limit.",
    "You don't have that.",
    "You don't have that much.",
    "The bank can't hold that.",
    "Usage: vault",
    "No account or character by that name.",
    " set to ",
    " must be ",
    NO_SUCH_SETTING_REPLY,
    GAMEOPTIONS_USAGE_REPLY,
    UNRECOGNIZED_COMMAND_REPLY,
    ACCOUNT_VIEW_START,
    "Return to your own body first.",
    "You have no connection.",
    "You can't carry that much money.",
    "could not be saved",
    "That slot can't be rebuilt",
    "That vault's file can't be read.",
    "That account has no folder.",
    "No vault for that account and side.",
    "Can't write the settings file",
    "The settings file was unreadable",
)


def staff_reply(session: GameSession, line: str) -> str:
    """Send a staff command and return its reply, without its carriage returns.

    The reply is awaited by its own text, not by a prompt: a multi-line staff reply can hold a
    line ending in `]` or `>`, where command() would stop reading.
    """
    reply = reply_to(session, line, list(STAFF_OUTCOMES), settle=STAFF_SETTLE_SECONDS)
    return reply.replace("\r", "")


def hand_object(implementor: GameSession, vnum: int, keyword: str, recipient: Character) -> None:
    """Load an object and give it to recipient, checking that the give reply names the recipient.

    The implementor must stand in the recipient's room.
    """
    give_new_object(implementor, vnum, keyword, recipient.name.lower(), shown_recipient=recipient.name)


def go_to_bank_hall(implementor: GameSession) -> None:
    """Bring the implementor to the bank hall."""
    goto(implementor, ROOM_BANK_HALL, ROOM_BANK_HALL_NAME)


def give_cap_in_bank_hall(implementor: GameSession, recipient: Character) -> None:
    """Bring the implementor to the bank hall and give recipient a leather cap there."""
    go_to_bank_hall(implementor)
    hand_object(implementor, world.OBJECT_LEATHER_CAP, "cap", recipient)


def set_gold(implementor: GameSession, character: Character, copper: int) -> None:
    """Set a character's purse with wizset and check the reply."""
    wizset(implementor, f"player {character.name.lower()}", "gold", copper)


def account_folder(lib_dir: Path) -> Path:
    """Return the harness account's folder: the one under lib/accounts holding an account file."""
    account_matches = (lib_dir / "accounts").glob(f"*/*/{ACCOUNT_FILE_NAME}")
    account_files = sorted(account_matches)
    assert len(account_files) == 1, account_files
    return account_files[0].parent


def read_json(path: Path) -> dict:
    """Return the JSON object in the file at path."""
    text = path.read_text(encoding="utf-8")
    return json.loads(text)


def account_record(lib_dir: Path) -> dict:
    """Return the harness account's account.json."""
    folder = account_folder(lib_dir)
    return read_json(folder / ACCOUNT_FILE_NAME)


def stored_vnums(path: Path) -> list[int]:
    """Return the item numbers of every object record in a vault or character objects file."""
    data = read_json(path)
    if "slots" in data:
        slot_objects = [slot["objects"] for slot in data["slots"]]
        all_objects = itertools.chain.from_iterable(slot_objects)
        return [stored["item_number"] for stored in all_objects]
    return [stored["item_number"] for stored in data["objects"]]


@pytest.mark.bank_deposits
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, DEPOSITOR)
def test_a_banker_keeps_an_item_lists_it_and_hands_it_back(server) -> None:
    """Pins depositing, listing and withdrawing an item at a banker.

    `deposit cap` takes the cap from loose inventory into the vault, `balance` lists it as slot
    1, `withdraw 1` hands it back, and a second `withdraw 1` finds the slot gone. The banker sets
    no fee, so no withdrawal depends on the wall-clock bank day; nothing on this path draws.
    """
    implementor = server.login(IMPLEMENTOR.name)
    depositor = server.login(DEPOSITOR.name)
    give_cap_in_bank_hall(implementor, DEPOSITOR)

    deposited = depositor.command("deposit cap")
    assert deposited.contains(CAP_DEPOSITED_REPLY), deposited.text
    inventory = depositor.command("inventory").text
    assert world.OBJECT_LEATHER_CAP_SHORT_DESCRIPTION not in inventory, inventory
    balance = depositor.command("balance")
    assert_lines(balance, ONE_CAP_VAULT_LINES)

    withdrawn = depositor.command("withdraw 1")
    assert withdrawn.contains(CAP_WITHDRAWN_REPLY), withdrawn.text
    inventory = depositor.command("inventory")
    assert inventory.contains(world.OBJECT_LEATHER_CAP_SHORT_DESCRIPTION), inventory.text
    balance = depositor.command("balance")
    assert_lines(balance, EMPTY_VAULT_LINES)

    withdrawn = depositor.command("withdraw 1")
    assert withdrawn.contains(NOTHING_LIKE_THAT_REPLY), withdrawn.text


@pytest.mark.bank_deposits
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(PURSED_DEPOSITOR)
def test_a_banker_keeps_coins_up_to_what_the_purse_and_the_vault_hold(server) -> None:
    """Pins coin deposits and withdrawals at a banker, and their refusals.

    With 10 gold in the purse, `deposit 20 gold` is refused and `deposit 5 gold` is taken; with
    5 gold in the vault, `withdraw 9 gold` is refused and `withdraw 2 gold` is paid out. score
    shows the purse after each change. Nothing on this path draws.
    """
    depositor = server.login(DEPOSITOR.name)
    score = depositor.command("score")
    assert score.contains(SCORE_GOLD_FORMAT.format(10)), score.text

    deposited = depositor.command("deposit 20 gold")
    assert deposited.contains(PURSE_SHORT_REPLY), deposited.text
    deposited = depositor.command("deposit 5 gold")
    assert deposited.contains(COINS_DEPOSITED_FORMAT.format("5 gold")), deposited.text
    score = depositor.command("score")
    assert score.contains(SCORE_GOLD_FORMAT.format(5)), score.text
    balance = depositor.command("balance")
    assert balance.contains(coins_line("5 gold")), balance.text

    withdrawn = depositor.command("withdraw 9 gold")
    assert withdrawn.contains(VAULT_SHORT_REPLY), withdrawn.text
    withdrawn = depositor.command("withdraw 2 gold")
    assert withdrawn.contains(COINS_WITHDRAWN_FORMAT.format("2 gold")), withdrawn.text
    score = depositor.command("score")
    assert score.contains(SCORE_GOLD_FORMAT.format(7)), score.text
    balance = depositor.command("balance")
    assert balance.contains(coins_line("3 gold")), balance.text


@pytest.mark.bank_deposits
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, DEPOSITOR, KINSMAN, OUTSIDER)
def test_an_accounts_characters_share_one_vault_per_side(server) -> None:
    """Pins that a vault belongs to the account and the side, not the character.

    All three mortals belong to the harness account (the implementor, above level 91, lets
    them be online together). After the human deposits the cap, the dwarf, also of the light
    side, sees it in its own `balance`; the uruk-hai, of the dark side, sees an empty vault.
    The uruk-hai's coin deposit then lands in the dark side's own file and leaves the light
    side's file as it was. Nothing on this path draws.
    """
    implementor = server.login(IMPLEMENTOR.name)
    depositor = server.login(DEPOSITOR.name)
    kinsman = server.login(KINSMAN.name)
    outsider = server.login(OUTSIDER.name)
    give_cap_in_bank_hall(implementor, DEPOSITOR)
    deposited = depositor.command("deposit cap")
    assert deposited.contains(CAP_DEPOSITED_REPLY), deposited.text

    balance = kinsman.command("balance")
    assert_lines(balance, ONE_CAP_VAULT_LINES)

    outsider_balance = outsider.command("balance")
    assert_lines(outsider_balance, EMPTY_VAULT_LINES)
    assert world.OBJECT_LEATHER_CAP_SHORT_DESCRIPTION not in outsider_balance.text, outsider_balance.text

    folder = account_folder(server.lib_dir)
    light_vault_bytes = (folder / LIGHT_VAULT_FILE_NAME).read_bytes()
    set_gold(implementor, OUTSIDER, PURSE_COPPER)
    deposited = outsider.command("deposit 5 gold")
    assert deposited.contains(COINS_DEPOSITED_FORMAT.format("5 gold")), deposited.text
    dark_vault = read_json(folder / DARK_VAULT_FILE_NAME)
    assert (dark_vault["coins"], dark_vault["slots"]) == (5 * COPPER_IN_GOLD, []), dark_vault
    light_vault_bytes_after = (folder / LIGHT_VAULT_FILE_NAME).read_bytes()
    assert light_vault_bytes_after == light_vault_bytes, light_vault_bytes_after


@pytest.mark.bank_deposits
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, DEPOSITOR)
def test_a_deposited_item_is_written_to_the_accounts_vault_file(server) -> None:
    """Pins that a deposit writes the light side's vault file in the account's folder.

    The vault is written before the character is saved, so the file holds the cap as soon as
    the deposit message arrives: one slot whose top object (nesting depth 0, kept in wear_pos)
    is the cap, and no coins. Nothing on this path draws.
    """
    implementor = server.login(IMPLEMENTOR.name)
    depositor = server.login(DEPOSITOR.name)
    vault_path = account_folder(server.lib_dir) / LIGHT_VAULT_FILE_NAME
    assert not vault_path.exists(), f"{vault_path} exists before any deposit"
    give_cap_in_bank_hall(implementor, DEPOSITOR)

    deposited = depositor.command("deposit cap")
    assert deposited.contains(CAP_DEPOSITED_REPLY), deposited.text

    vault = read_json(vault_path)
    assert (vault["version"], vault["coins"], len(vault["slots"])) == (1, 0, 1), vault
    stored_objects = vault["slots"][0]["objects"]
    stored_positions = [(stored["item_number"], stored["wear_pos"]) for stored in stored_objects]
    assert stored_positions == [(world.OBJECT_LEATHER_CAP, 0)], vault


@pytest.mark.bank_deposits
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, DEPOSITOR)
def test_a_deposit_and_a_withdrawal_save_the_characters_objects_file(server) -> None:
    """Pins the character half of a vault move: the character's objects file.

    A deposit saves the character without the item (bank_save_character runs save_char and
    Crash_crashsave), so its objects file no longer lists the cap; a withdrawal saves it with
    the cap, and the vault file then no longer holds it. Nothing on this path draws.
    """
    implementor = server.login(IMPLEMENTOR.name)
    depositor = server.login(DEPOSITOR.name)
    give_cap_in_bank_hall(implementor, DEPOSITOR)
    objects_file_name = f"{DEPOSITOR.name.lower()}.objects.json"

    deposited = depositor.command("deposit cap")
    assert deposited.contains(CAP_DEPOSITED_REPLY), deposited.text
    objects_matches = server.lib_dir.rglob(objects_file_name)
    objects_files = sorted(objects_matches)
    assert len(objects_files) == 1, objects_files
    carried_vnums = stored_vnums(objects_files[0])
    assert world.OBJECT_LEATHER_CAP not in carried_vnums, carried_vnums

    withdrawn = depositor.command("withdraw 1")
    assert withdrawn.contains(CAP_WITHDRAWN_REPLY), withdrawn.text
    carried_vnums = stored_vnums(objects_files[0])
    assert world.OBJECT_LEATHER_CAP in carried_vnums, carried_vnums
    vault_path = account_folder(server.lib_dir) / LIGHT_VAULT_FILE_NAME
    vault_vnums = stored_vnums(vault_path)
    assert world.OBJECT_LEATHER_CAP not in vault_vnums, vault_vnums


@pytest.mark.bank_refusals
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, DEPOSITOR)
def test_a_banker_refuses_a_gift(server) -> None:
    """Pins that a banker refuses what a player gives it, and the player keeps it.

    SPECIAL(banker) answers `give` aimed at itself with a tell before do_give runs. Nothing on
    this path draws.
    """
    implementor = server.login(IMPLEMENTOR.name)
    depositor = server.login(DEPOSITOR.name)
    give_cap_in_bank_hall(implementor, DEPOSITOR)

    given = depositor.command("give cap banker")
    assert given.contains(NO_GIFTS_REPLY), given.text
    inventory = depositor.command("inventory")
    assert inventory.contains(world.OBJECT_LEATHER_CAP_SHORT_DESCRIPTION), inventory.text


@pytest.mark.bank_refusals
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(DEPOSITOR)
def test_a_banker_refuses_an_attack_and_no_fight_starts(server) -> None:
    """Pins that a banker cancels an attack on it with a tell (the shared keeper protection).

    `kill banker` reaches hit() (fight.cpp). Whatever its rolls decide, every path asks the
    banker's program the SPECIAL_DAMAGE question before any damage lands: a blow through
    damage(), and a miss, dodge or parry after it. The only roll-decided path that skips it is a
    riposte, which needs a wielded weapon the banker does not have (check_riposte), so the
    refusal does not depend on a roll. keeper_protection (shopkeeper.cpp) answers with the tell,
    and damage() stops the attacker fighting, so a second `kill banker` is a new attack refused
    the same way, not do_hit's answer to a fight already under way.
    """
    depositor = server.login(DEPOSITOR.name)

    attacked = depositor.command("kill banker")
    assert attacked.contains(NO_ATTACKS_REPLY), attacked.text
    second_attack = depositor.command("kill banker")
    assert second_attack.contains(NO_ATTACKS_REPLY), second_attack.text
    assert FIGHT_ONGOING_REPLY not in second_attack.text, second_attack.text


@pytest.mark.bank_refusals
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, DEPOSITOR)
def test_a_banker_refuses_will_kill_and_blind_before_and_after_a_stat_file(server) -> None:
    """Fix pin (#369): keeper protection holds through the interpreter, also after `stat file`.

    `will banker` is refused by do_mental's keeper check (clerics.cpp) before its readiness
    matters or any roll. `stat file` builds and frees a character that never had a slot. #369
    made freeing it leave slot 0 alone; before it, that freed slot 0, and with it every
    SPECIAL_DAMAGE question, whose target carried slot 0, found no character and no keeper was
    asked (special(), interpre.cpp). After the `stat file`, the mental attack, `kill banker`
    (hit() asks before any damage, whatever its rolls) and `blind banker` (refused as the
    command's target, SPECIAL_TARGET, before do_blinding runs) are all still refused with the
    tell.
    """
    implementor = server.login(IMPLEMENTOR.name)
    depositor = server.login(DEPOSITOR.name)

    willed = depositor.command("will banker")
    assert willed.contains(NO_ATTACKS_REPLY), willed.text

    read_stat(implementor, f"file {DEPOSITOR.name.lower()}", other_outcomes=STAT_FILE_REFUSALS)

    for command in ("will banker", "kill banker", "blind banker"):
        reply = depositor.command(command)
        assert reply.contains(NO_ATTACKS_REPLY), f"{command}: {reply.text}"


@pytest.mark.bank_refusals
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, DEPOSITOR)
def test_a_banker_holds_nothing_for_an_immortal_and_says_so_to_the_room(server) -> None:
    """Pins that a banker refuses an immortal customer aloud.

    banker_admits gives an immortal no side twice over: by its level (91 and up), and by its
    race, which advance_level (profs.cpp) sets to RACE_GOD and bank_side_for_race maps to no
    side. The banker says it holds nothing for its kind: a `say`, which the depositor in the
    room hears too. Nothing on this path draws.
    """
    implementor = server.login(IMPLEMENTOR.name)
    depositor = server.login(DEPOSITOR.name)
    go_to_bank_hall(implementor)

    balance = implementor.command("balance")
    assert balance.contains(NOT_YOUR_KIND_REPLY), balance.text
    depositor.expect([NOT_YOUR_KIND_REPLY])


@pytest.mark.bank_refusals
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, DEPOSITOR)
def test_a_banker_refuses_a_customer_it_cannot_see_aloud(server) -> None:
    """Pins a banker's refusal of a customer it cannot see.

    The implementor turns invisible at its own level; the banker cannot see it, so
    vendor_serves_customer (shopkeeper.cpp) says so to the whole room before any other check.
    The depositor in the room hears it too. Nothing on this path draws.
    """
    implementor = server.login(IMPLEMENTOR.name)
    depositor = server.login(DEPOSITOR.name)
    go_to_bank_hall(implementor)
    invisible = implementor.command("invis")
    assert invisible.contains(INVISIBLE_REPLY), invisible.text

    balance = implementor.command("balance")
    assert balance.contains(UNSEEN_CUSTOMER_REPLY), balance.text
    depositor.expect([UNSEEN_CUSTOMER_REPLY])


@pytest.mark.bank_refusals
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(DARK_CUSTOMER)
def test_a_banker_in_a_dark_room_refuses_a_customer_it_cannot_see(server) -> None:
    """Pins that a banker in the dark does not serve a customer it cannot see.

    The dark vault hall has the DARK flag and nothing in it gives light; neither the dark banker
    nor the human customer has infravision, so CAN_SEE fails and the banker refuses aloud. The
    customer cannot see the banker either, so the speaker may read as someone. Nothing on this
    path draws.
    """
    customer = server.login(DARK_CUSTOMER.name)

    balance = customer.command("balance")
    assert balance.contains(f"says '{UNSEEN_CUSTOMER_PART}'"), balance.text


@pytest.mark.bank_refusals
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, PURSED_DEPOSITOR, KINSMAN)
def test_a_bystander_sees_item_moves_but_not_coin_moves_or_the_bankers_tells(server) -> None:
    """Pins what a same-side bystander sees of another's banking.

    An item deposit and an item withdrawal are shown to the room (`$n deposits $p.`,
    `$n withdraws $p.`). A coin deposit, a coin withdrawal and the banker's tells reach the
    customer alone. The depositor's `say` after each round reaches the bystander after anything
    the round showed the room, so the text before it shows what was sent. Nothing on this path
    draws.
    """
    implementor = server.login(IMPLEMENTOR.name)
    depositor = server.login(DEPOSITOR.name)
    bystander = server.login(KINSMAN.name)
    give_cap_in_bank_hall(implementor, DEPOSITOR)
    bystander.expect([CAP_GIVEN_TO_DEPOSITOR_PART])

    deposited = depositor.command("deposit 5 gold")
    assert deposited.contains(COINS_DEPOSITED_FORMAT.format("5 gold")), deposited.text
    withdrawn = depositor.command("withdraw 9")
    assert withdrawn.contains(NOTHING_LIKE_THAT_REPLY), withdrawn.text
    deposited = depositor.command("deposit cap")
    assert deposited.contains(CAP_DEPOSITED_REPLY), deposited.text
    seen_by_deposit = text_seen_before_a_say(depositor, DEPOSITOR.name, bystander)
    deposit_seen = DEPOSITS_FORMAT.format(DEPOSITOR.name, world.OBJECT_LEATHER_CAP_SHORT_DESCRIPTION)
    assert deposit_seen in seen_by_deposit, seen_by_deposit
    assert GOLD_PART not in seen_by_deposit, seen_by_deposit
    assert HOLD_NOTHING_PART not in seen_by_deposit, seen_by_deposit

    withdrawn = depositor.command("withdraw 2 gold")
    assert withdrawn.contains(COINS_WITHDRAWN_FORMAT.format("2 gold")), withdrawn.text
    withdrawn = depositor.command("withdraw 1")
    assert withdrawn.contains(CAP_WITHDRAWN_REPLY), withdrawn.text
    seen_by_withdrawal = text_seen_before_a_say(depositor, DEPOSITOR.name, bystander)
    withdrawal_seen = WITHDRAWS_FORMAT.format(DEPOSITOR.name, world.OBJECT_LEATHER_CAP_SHORT_DESCRIPTION)
    assert withdrawal_seen in seen_by_withdrawal, seen_by_withdrawal
    assert GOLD_PART not in seen_by_withdrawal, seen_by_withdrawal


@pytest.mark.bank_refusals
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, DEPOSITOR)
def test_a_banker_refuses_a_key_loose_or_inside_a_bag(server) -> None:
    """Pins that a banker refuses an item rent would refuse, also inside a container.

    A key is unrentable (Crash_is_unrentable, objsave.cpp), and bank_obj_storable checks a
    container's contents too, so the key and a bag holding it are both refused, and both stay
    with the depositor. Nothing on this path draws.
    """
    implementor = server.login(IMPLEMENTOR.name)
    depositor = server.login(DEPOSITOR.name)
    go_to_bank_hall(implementor)
    hand_object(implementor, KEY_VNUM, "key", DEPOSITOR)
    hand_object(implementor, world.OBJECT_LEATHER_BAG, "bag", DEPOSITOR)

    deposited = depositor.command("deposit key")
    assert deposited.contains(CANNOT_KEEP_REPLY), deposited.text
    put_reply = depositor.command("put key bag")
    key_put = PUT_FORMAT.format(KEY_SHORT_DESCRIPTION, world.OBJECT_LEATHER_BAG_SHORT_DESCRIPTION)
    assert put_reply.contains(key_put), put_reply.text
    deposited = depositor.command("deposit bag")
    assert deposited.contains(CANNOT_KEEP_REPLY), deposited.text
    inventory = depositor.command("inventory")
    assert inventory.contains(world.OBJECT_LEATHER_BAG_SHORT_DESCRIPTION), inventory.text
    balance = depositor.command("balance")
    assert_lines(balance, EMPTY_VAULT_LINES)


@pytest.mark.bank_refusals
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, DEPOSITOR)
def test_a_banker_refuses_a_cursed_item_inside_a_bag_or_loose(server) -> None:
    """Pins that a banker refuses a cursed (NODROP) item, also inside a container.

    give refuses a cursed item even from the implementor, so the ring arrives inside a bag.
    has_nodrop (banker.cpp) checks a container's contents, so the bag is refused; taken out, the
    ring itself is refused. Nothing on this path draws.
    """
    implementor = server.login(IMPLEMENTOR.name)
    depositor = server.login(DEPOSITOR.name)
    go_to_bank_hall(implementor)
    load_object(implementor, world.OBJECT_CURSED_RING)
    load_object(implementor, world.OBJECT_LEATHER_BAG)
    put_reply = implementor.command("put ring bag")
    ring_put = PUT_FORMAT.format(world.OBJECT_CURSED_RING_SHORT_DESCRIPTION, world.OBJECT_LEATHER_BAG_SHORT_DESCRIPTION)
    assert put_reply.contains(ring_put), put_reply.text
    given = implementor.command(f"give bag {DEPOSITOR.name.lower()}")
    assert given.contains(f"to {DEPOSITOR.name}."), given.text

    deposited = depositor.command("deposit bag")
    assert deposited.contains(CANNOT_KEEP_REPLY), deposited.text
    taken = depositor.command("get ring bag")
    assert taken.contains(f"You get {world.OBJECT_CURSED_RING_SHORT_DESCRIPTION}"), taken.text
    deposited = depositor.command("deposit ring")
    assert deposited.contains(CANNOT_KEEP_REPLY), deposited.text
    balance = depositor.command("balance")
    assert_lines(balance, EMPTY_VAULT_LINES)


@pytest.mark.bank_deposits
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, DEPOSITOR)
def test_a_vault_takes_its_tenth_item_and_refuses_the_eleventh(server) -> None:
    """Pins the vault's slot limit for a player and for staff.

    The implementor puts nine caps in with `vault put`; the depositor's deposit fills the tenth
    and last slot (bank_slots, 10 by default), the next is refused with the player's message,
    and a staff `vault put` into the full vault is refused with the staff message. Nothing on
    this path draws.
    """
    implementor = server.login(IMPLEMENTOR.name)
    depositor = server.login(DEPOSITOR.name)
    account_name = account_record(server.lib_dir)["account_name"]
    go_to_bank_hall(implementor)
    for _put in range(VAULT_SLOTS - 1):
        load_object(implementor, world.OBJECT_LEATHER_CAP)
        reply = staff_reply(implementor, f"vault put {account_name} 1 cap")
        assert VAULT_PUT_FORMAT.format(world.OBJECT_LEATHER_CAP_SHORT_DESCRIPTION) in reply, reply
    hand_object(implementor, world.OBJECT_LEATHER_CAP, "cap", DEPOSITOR)
    hand_object(implementor, world.OBJECT_LEATHER_CAP, "cap", DEPOSITOR)

    deposited = depositor.command("deposit cap")
    assert deposited.contains(CAP_DEPOSITED_REPLY), deposited.text
    balance = depositor.command("balance")
    assert balance.contains(slots_line(VAULT_SLOTS)), balance.text
    deposited = depositor.command("deposit cap")
    assert deposited.contains(VAULT_FULL_REPLY), deposited.text
    inventory = depositor.command("inventory")
    assert inventory.contains(world.OBJECT_LEATHER_CAP_SHORT_DESCRIPTION), inventory.text

    load_object(implementor, world.OBJECT_LEATHER_CAP)
    staff_view = staff_reply(implementor, f"vault put {account_name} 1 cap")
    assert STAFF_VAULT_FULL_REPLY in staff_view, staff_view


@pytest.mark.bank_deposits
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(RICH_DEPOSITOR)
def test_a_coin_deposit_past_the_vault_limit_is_cut_to_what_fits(server) -> None:
    """Pins the vault's coin limit for a player's deposit.

    With 1500 gold in the purse and an empty vault whose limit is 1000 gold
    (bank_coin_limit_gold), `deposit 1500 gold` takes 1000 and says the other 500 was refused;
    the purse keeps 500. A further `deposit 1 gold` is refused outright. Nothing on this path
    draws.
    """
    depositor = server.login(DEPOSITOR.name)

    deposited = depositor.command("deposit 1500 gold")
    assert_lines(
        deposited,
        (COINS_DEPOSITED_FORMAT.format(f"{VAULT_COIN_LIMIT_GOLD} gold"), "500 gold was refused: your vault is full."),
    )
    score = depositor.command("score")
    assert score.contains(SCORE_GOLD_FORMAT.format(500)), score.text
    deposited = depositor.command("deposit 1 gold")
    assert deposited.contains(NO_MORE_COINS_REPLY), deposited.text
    balance = depositor.command("balance")
    assert balance.contains(coins_line(f"{VAULT_COIN_LIMIT_GOLD} gold")), balance.text


@pytest.mark.bank_deposits
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, DEPOSITOR)
def test_a_withdrawal_finds_its_slot_by_number_keyword_or_abbreviation_and_renumbers(server) -> None:
    """Pins how `withdraw` picks a slot, and the renumbering after one is taken.

    The depositor's inventory lists the newest item first, so the leather cap, given last, is
    the first item `lea` matches ahead of the leather bag. With the leather cap (deposited by
    that abbreviation), the iron cap and the empty bag in slots 1 to 3, `withdraw 5` finds no
    slot, `withdraw 2.cap` takes the second slot answering to
    `cap` (the iron cap), the bag moves up to slot 2, and `withdraw lea` takes the leather cap
    by the same abbreviation deposit took. The empty bag shows as sealed with nothing inside.
    Nothing on this path draws.
    """
    implementor = server.login(IMPLEMENTOR.name)
    depositor = server.login(DEPOSITOR.name)
    go_to_bank_hall(implementor)
    hand_object(implementor, world.OBJECT_LEATHER_BAG, "bag", DEPOSITOR)
    hand_object(implementor, world.OBJECT_IRON_CAP, "cap", DEPOSITOR)
    hand_object(implementor, world.OBJECT_LEATHER_CAP, "cap", DEPOSITOR)
    empty_bag_row = f"{world.OBJECT_LEATHER_BAG_SHORT_DESCRIPTION} (sealed, 0 inside)"

    deposited = depositor.command("deposit lea")
    assert deposited.contains(CAP_DEPOSITED_REPLY), deposited.text
    deposited = depositor.command("deposit cap")
    assert deposited.contains(DEPOSITED_FORMAT.format(world.OBJECT_IRON_CAP_SHORT_DESCRIPTION)), deposited.text
    deposited = depositor.command("deposit bag")
    assert deposited.contains(DEPOSITED_FORMAT.format(world.OBJECT_LEATHER_BAG_SHORT_DESCRIPTION)), deposited.text
    balance = depositor.command("balance")
    three_slots = (world.OBJECT_LEATHER_CAP_SHORT_DESCRIPTION, world.OBJECT_IRON_CAP_SHORT_DESCRIPTION, empty_bag_row)
    expected_balance = balance_lines("0 copper", three_slots)
    assert_lines(balance, expected_balance)

    withdrawn = depositor.command("withdraw 5")
    assert withdrawn.contains(NOTHING_LIKE_THAT_REPLY), withdrawn.text
    withdrawn = depositor.command("withdraw 2.cap")
    assert withdrawn.contains(WITHDRAWN_FORMAT.format(world.OBJECT_IRON_CAP_SHORT_DESCRIPTION)), withdrawn.text
    balance = depositor.command("balance")
    expected_balance = balance_lines("0 copper", (world.OBJECT_LEATHER_CAP_SHORT_DESCRIPTION, empty_bag_row))
    assert_lines(balance, expected_balance)
    withdrawn = depositor.command("withdraw lea")
    assert withdrawn.contains(CAP_WITHDRAWN_REPLY), withdrawn.text
    balance = depositor.command("balance")
    expected_balance = balance_lines("0 copper", (empty_bag_row,))
    assert_lines(balance, expected_balance)


@pytest.mark.bank_deposits
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, DEPOSITOR)
def test_a_deposited_bag_keeps_its_contents_and_lists_as_sealed(server) -> None:
    """Pins depositing a container with something inside it.

    A bag holding a cap is one slot: `balance` shows it sealed with one thing inside, and the
    vault file holds the bag at nesting depth 0 and the cap at depth 1. Nothing on this path
    draws.
    """
    implementor = server.login(IMPLEMENTOR.name)
    depositor = server.login(DEPOSITOR.name)
    go_to_bank_hall(implementor)
    hand_object(implementor, world.OBJECT_LEATHER_CAP, "cap", DEPOSITOR)
    hand_object(implementor, world.OBJECT_LEATHER_BAG, "bag", DEPOSITOR)
    put_reply = depositor.command("put cap bag")
    cap_put = PUT_FORMAT.format(world.OBJECT_LEATHER_CAP_SHORT_DESCRIPTION, world.OBJECT_LEATHER_BAG_SHORT_DESCRIPTION)
    assert put_reply.contains(cap_put), put_reply.text

    deposited = depositor.command("deposit bag")
    assert deposited.contains(DEPOSITED_FORMAT.format(world.OBJECT_LEATHER_BAG_SHORT_DESCRIPTION)), deposited.text
    balance = depositor.command("balance")
    sealed_bag_row = f"{world.OBJECT_LEATHER_BAG_SHORT_DESCRIPTION} (sealed, 1 inside)"
    expected_balance = balance_lines("0 copper", (sealed_bag_row,))
    assert_lines(balance, expected_balance)
    folder = account_folder(server.lib_dir)
    vault = read_json(folder / LIGHT_VAULT_FILE_NAME)
    stored_objects = vault["slots"][0]["objects"]
    expected_objects = [(world.OBJECT_LEATHER_BAG, 0), (world.OBJECT_LEATHER_CAP, 1)]
    stored_positions = [(stored["item_number"], stored["wear_pos"]) for stored in stored_objects]
    assert stored_positions == expected_objects, vault


@pytest.mark.bank_staff
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, LESSER_GOD, GREATER_GOD, ELDER_GOD)
def test_vault_and_gameoptions_need_their_command_levels(server) -> None:
    """Pins the minimum levels of `vault` (97) and `gameoptions` (100).

    A level-96 immortal's `vault` and a level-99 immortal's `gameoptions` are unknown commands
    to them; at level 97 `vault` shows a vault, and the implementor's `gameoptions` lists the
    settings. Nothing on this path draws.
    """
    implementor = server.login(IMPLEMENTOR.name)
    lesser_god = server.login(LESSER_GOD.name)
    greater_god = server.login(GREATER_GOD.name)
    elder_god = server.login(ELDER_GOD.name)

    vault_reply = lesser_god.command(f"vault {LESSER_GOD.name.lower()}")
    assert vault_reply.contains(UNRECOGNIZED_COMMAND_REPLY), vault_reply.text
    staff_view = staff_reply(greater_god, f"vault {GREATER_GOD.name.lower()}")
    assert ACCOUNT_VIEW_START in staff_view, staff_view
    options = elder_god.command("gameoptions")
    assert options.contains(UNRECOGNIZED_COMMAND_REPLY), options.text
    reply_to(implementor, "gameoptions", ["bank_slots", UNRECOGNIZED_COMMAND_REPLY], settle=0)


@pytest.mark.bank_staff
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, DEPOSITOR)
def test_vault_shows_a_characters_account_side_and_slots(server) -> None:
    """Pins the staff view of a character's vault.

    `vault testsaver` names the account and its email, the character, and the character's own
    side (light, for a human) with its coins and slot count, then one line per slot. A slot's
    age in bank days is 0 unless a bank day ended between the deposit and the view, which the
    wall clock alone decides. Nothing on this path draws.
    """
    implementor = server.login(IMPLEMENTOR.name)
    depositor = server.login(DEPOSITOR.name)
    account = account_record(server.lib_dir)
    give_cap_in_bank_hall(implementor, DEPOSITOR)
    deposited = depositor.command("deposit cap")
    assert deposited.contains(CAP_DEPOSITED_REPLY), deposited.text

    view = staff_reply(implementor, f"vault {DEPOSITOR.name.lower()}")
    expected_header = (
        f"Account: {account['account_name']} ({account['normalized_email']})\n"
        f"Character: {DEPOSITOR.name}\n"
        f"Light vault: 0 copper, 1 of {VAULT_SLOTS} slots\n"
    )
    assert expected_header in view, view
    slot_row = re.search(r"^ 1  (.{40}) stored (\d+) days?$", view, re.MULTILINE)
    assert slot_row is not None, view
    slot_item = slot_row.group(1)
    slot_days = slot_row.group(2)
    cap = world.OBJECT_LEATHER_CAP_SHORT_DESCRIPTION
    assert (slot_item.rstrip(), slot_days) in ((cap, "0"), (cap, "1")), view


@pytest.mark.bank_staff
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, DEPOSITOR)
def test_vault_take_and_put_move_items_and_coins_and_the_owner_sees_it(server) -> None:
    """Pins staff moving items and coins in and out of a vault.

    `vault take <account> 1 1` takes the deposited cap into the implementor's inventory,
    `vault put <account> 1 cap` puts it back, and the coin forms put 5 gold in and take 2 out.
    The depositor's own `balance` shows the result. Nothing on this path draws.
    """
    implementor = server.login(IMPLEMENTOR.name)
    depositor = server.login(DEPOSITOR.name)
    account_name = account_record(server.lib_dir)["account_name"]
    give_cap_in_bank_hall(implementor, DEPOSITOR)
    deposited = depositor.command("deposit cap")
    assert deposited.contains(CAP_DEPOSITED_REPLY), deposited.text
    set_gold(implementor, IMPLEMENTOR, PURSE_COPPER)

    staff_view = staff_reply(implementor, f"vault take {account_name} 1 1")
    assert VAULT_TAKE_FORMAT.format(world.OBJECT_LEATHER_CAP_SHORT_DESCRIPTION) in staff_view, staff_view
    balance = depositor.command("balance")
    assert_lines(balance, EMPTY_VAULT_LINES)
    staff_view = staff_reply(implementor, f"vault put {account_name} 1 cap")
    assert VAULT_PUT_FORMAT.format(world.OBJECT_LEATHER_CAP_SHORT_DESCRIPTION) in staff_view, staff_view
    staff_view = staff_reply(implementor, f"vault put {account_name} 1 coins 5 gold")
    assert VAULT_PUT_FORMAT.format("5 gold") in staff_view, staff_view
    staff_view = staff_reply(implementor, f"vault take {account_name} 1 coins 2 gold")
    assert VAULT_TAKE_FORMAT.format("2 gold") in staff_view, staff_view

    balance = depositor.command("balance")
    expected_balance = balance_lines("3 gold", (world.OBJECT_LEATHER_CAP_SHORT_DESCRIPTION,))
    assert_lines(balance, expected_balance)


@pytest.mark.bank_staff
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, DEPOSITOR)
def test_vault_take_refuses_a_missing_slot_a_bad_amount_and_an_email(server) -> None:
    """Pins `vault take`'s refusals.

    A slot the vault does not have, a coin amount that is not a number, and an account named by
    its email rather than its name are each refused with their own reply, and the vault keeps
    its cap. Nothing on this path draws.
    """
    implementor = server.login(IMPLEMENTOR.name)
    depositor = server.login(DEPOSITOR.name)
    account = account_record(server.lib_dir)
    give_cap_in_bank_hall(implementor, DEPOSITOR)
    deposited = depositor.command("deposit cap")
    assert deposited.contains(CAP_DEPOSITED_REPLY), deposited.text

    staff_view = staff_reply(implementor, f"vault take {account['account_name']} 1 2")
    assert NO_SUCH_SLOT_REPLY in staff_view, staff_view
    staff_view = staff_reply(implementor, f"vault take {account['account_name']} 1 coins lots")
    assert HOW_MUCH_REPLY in staff_view, staff_view
    staff_view = staff_reply(implementor, f"vault take {account['normalized_email']} 1 1")
    assert ACCOUNT_NAME_ONLY_REPLY in staff_view, staff_view
    balance = depositor.command("balance")
    assert_lines(balance, ONE_CAP_VAULT_LINES)


@pytest.mark.bank_staff
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, DEPOSITOR)
def test_gameoptions_writes_bank_slots_for_the_next_reboot_only(server) -> None:
    """Pins `gameoptions bank_slots`.

    The setting is written to lib/misc/game_boot_options.json and takes effect at the next
    reboot: the `gameoptions` list shows 10 now and 5 after the reboot, and the depositor's
    `balance` still counts the running limit of 10 slots. Nothing on this path draws.
    """
    implementor = server.login(IMPLEMENTOR.name)
    depositor = server.login(DEPOSITOR.name)

    staff_view = staff_reply(implementor, "gameoptions bank_slots 5")
    assert BANK_SLOTS_SET_FORMAT.format(5) in staff_view, staff_view
    settings = read_json(server.lib_dir / BOOT_OPTIONS_FILE)
    assert settings["bank_slots"] == 5, settings
    listing_reply = reply_to(implementor, "gameoptions", ["daily_reboot_hour_utc"], settle=STAFF_SETTLE_SECONDS)
    listing = listing_reply.replace("\r", "")
    assert re.search(r"^bank_slots +10 +5 +10 +1-100$", listing, re.MULTILINE), listing
    balance = depositor.command("balance")
    assert balance.contains(slots_line(0, VAULT_SLOTS)), balance.text


@pytest.mark.bank_staff
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR)
def test_gameoptions_refuses_out_of_range_values_unknown_names_and_bad_numbers(server) -> None:
    """Pins `gameoptions`' refusals and its range ends.

    bank_slots accepts 1 to 100: 100 is set, 0 and 101 are refused with the range. An unknown
    setting, a missing number and a number with trailing text each get their own reply. Nothing
    on this path draws.
    """
    implementor = server.login(IMPLEMENTOR.name)

    staff_view = staff_reply(implementor, "gameoptions bank_slots 100")
    assert BANK_SLOTS_SET_FORMAT.format(100) in staff_view, staff_view
    staff_view = staff_reply(implementor, "gameoptions bank_slots 0")
    assert BANK_SLOTS_RANGE_REPLY in staff_view, staff_view
    staff_view = staff_reply(implementor, "gameoptions bank_slots 101")
    assert BANK_SLOTS_RANGE_REPLY in staff_view, staff_view
    staff_view = staff_reply(implementor, "gameoptions bank_size 5")
    assert NO_SUCH_SETTING_REPLY in staff_view, staff_view
    staff_view = staff_reply(implementor, "gameoptions bank_slots")
    assert GAMEOPTIONS_USAGE_REPLY in staff_view, staff_view
    staff_view = staff_reply(implementor, "gameoptions bank_slots 5x")
    assert GAMEOPTIONS_USAGE_REPLY in staff_view, staff_view
    settings = read_json(server.lib_dir / BOOT_OPTIONS_FILE)
    assert settings["bank_slots"] == 100, settings


@pytest.mark.bank_hours
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, EARLY_CUSTOMER, LATE_CUSTOMER)
def test_bankers_open_only_in_their_hours_and_swap_after_twelve_hours(server) -> None:
    """Pins a banker's hours= option.

    The morning banker is open from hour 0 to 11 and the evening banker from 12 to 23, so
    whatever hour the wall clock started the game at, exactly one of them is closed; twelve game
    hours later the other one is. A closed banker says so to the room. Under affect-hours the
    clock moves only when advanced, so both are asked at the same hour. The hours pass three at
    a time with a command between, so neither customer idles into AFK or the void. Nothing on
    this path draws.
    """
    server.login(IMPLEMENTOR.name)
    early = server.login(EARLY_CUSTOMER.name)
    late = server.login(LATE_CUSTOMER.name)

    def closed_flags() -> tuple[bool, bool]:
        """Return whether the early and the late banker are closed, asserting each answered."""
        flags = []
        for customer in (early, late):
            reply = customer.command("balance")
            assert reply.contains(CLOSED_PART) or reply.contains(BALANCE_HEADER), reply.text
            closed = reply.contains(CLOSED_PART)
            flags.append(closed)
        return tuple(flags)

    before = closed_flags()
    assert before in ((True, False), (False, True)), before

    for _step in range(SWAP_HOURS // IDLE_SAFE_HOURS):
        server.advance_hours(IDLE_SAFE_HOURS)
        for customer in (early, late):
            customer.drain(SETTLE_SECONDS)
            looked = customer.command("look")
            assert looked.room_name() in BANK_ROOM_NAMES, looked.text
    after = closed_flags()
    assert after == (not before[0], not before[1]), (before, after)


@pytest.mark.bank_hours
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(STRANDED_CUSTOMER)
def test_a_banker_with_a_bad_fee_is_logged_at_boot_and_closed(server) -> None:
    """Pins a banker its options disable.

    The broken banker's options set fee=0, outside 1 to BANKER_FEE_MAX: the boot logs a
    `MOB ERROR` line for it, not a SYSERR, and `balance` gets the closed-for-now answer, said to
    the room. Nothing on this path draws.
    """
    customer = server.login(STRANDED_CUSTOMER.name)

    problem_lines = log_lines_holding(server.handle.log_path, BAD_FEE_LOG_LINE)
    assert len(problem_lines) == 1, read_game_log(server.handle.log_path)[-3000:]
    assert SYSERR_PART not in problem_lines[0], problem_lines[0]

    balance = customer.command("balance")
    assert balance.contains(BANK_CLOSED_FOR_NOW_REPLY), balance.text
