"""The banker (mob program 34): an account's vault per side, deposits and withdrawals of items
and coins, the vault and character files on disk, what bystanders see, the banker's refusals,
its opening hours and options, and the staff commands `vault` and `gameoptions`."""

from __future__ import annotations

import dataclasses
import json
import re
from pathlib import Path

import pytest

from rots_harness import world
from rots_harness.characters import IMPLEMENTOR, Character, StoredValues
from rots_harness.session import GameSession, Transcript

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
# Immortals one level below `vault` (LEVEL_GRGOD, 97), at it, and one below `gameoptions`
# (LEVEL_IMPL, 100): the minimum levels in the command table (interpre.cpp).
LESSER_GOD = Character(
    name="Testdemigod",
    race="human",
    sex="male",
    level=96,
    load_room=ROOM_BANK_HALL,
    class_name="warrior",
)
GREATER_GOD = Character(
    name="Testgreatgod",
    race="human",
    sex="male",
    level=97,
    load_room=ROOM_BANK_HALL,
    class_name="warrior",
)
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
# Level-20 human warriors before the morning banker (hours=0-12) and the evening banker
# (hours=12-0), whose opening hours together cover the day once.
EARLY_CUSTOMER = Character(
    name="Testearly",
    race="human",
    sex="male",
    level=20,
    load_room=ROOM_MORNING_BANK,
    class_name="warrior",
)
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

# Objects of the test world (tests/integration/world/obj/11.obj and 13.obj), with the keyword
# each is handed over by and its short description.
CAP_VNUM = 1137
CAP_NAME = "a leather cap"
BAG_VNUM = 1136
BAG_NAME = "a leather bag"
IRON_CAP_VNUM = 1304
IRON_CAP_NAME = "an iron cap"
# A brass key: type 18, ITEM_KEY, which Crash_is_unrentable (objsave.cpp) refuses.
KEY_VNUM = 1306
KEY_NAME = "a brass key"
# A cursed ring: extra flags 128, ITEM_NODROP.
CURSED_RING_VNUM = 1307
CURSED_RING_NAME = "a cursed ring"
# The copper in a purse: 10 gold (COPP_IN_GOLD is 1000, structs.h).
PURSE_COPPER = 10000
COPPER_IN_GOLD = 1000
# The depositor stored with a purse of PURSE_COPPER.
PURSED_DEPOSITOR = dataclasses.replace(DEPOSITOR, stored=StoredValues(gold=PURSE_COPPER))
# The depositor stored with 1500 gold, more than a vault holds.
RICH_DEPOSITOR = dataclasses.replace(DEPOSITOR, stored=StoredValues(gold=1500 * COPPER_IN_GOLD))
# The limits in game_boot_options.cpp's defaults; the harness lib's
# lib/misc/game_boot_options.json sets only daily_reboot_hour_utc.
VAULT_SLOTS = 10
VAULT_COIN_LIMIT_GOLD = 1000

# The banker's tells (bank_deposit, bank_withdraw, their coin forms and SPECIAL(banker),
# banker.cpp) and its refusal of an attack (keeper_protection, shopkeeper.cpp).
BANKER_TELL_FORMAT = "A banker tells you '{}'"
NOTHING_LIKE_THAT_MARKER = BANKER_TELL_FORMAT.format("I hold nothing like that for you.")
PURSE_SHORT_MARKER = BANKER_TELL_FORMAT.format("You don't have that much.")
VAULT_SHORT_MARKER = BANKER_TELL_FORMAT.format("You don't have that much with me.")
NO_GIFTS_MARKER = BANKER_TELL_FORMAT.format("I don't take gifts.")
NO_ATTACKS_MARKER = BANKER_TELL_FORMAT.format("Don't even think about it.")
CANNOT_KEEP_MARKER = BANKER_TELL_FORMAT.format("I can't keep that for you.")
VAULT_FULL_MARKER = BANKER_TELL_FORMAT.format("Your vault is full.")
NO_MORE_COINS_MARKER = BANKER_TELL_FORMAT.format("Your vault can hold no more coins.")
# The banker's refusals to serve at all, said to the room (banker_admits and
# vendor_serves_customer, shopkeeper.cpp).
BANKER_SAYS_FORMAT = "A banker says '{}'"
NOT_YOUR_KIND_MARKER = BANKER_SAYS_FORMAT.format("I hold nothing for your kind.")
UNSEEN_CUSTOMER_TEXT = "I don't trade with someone I can't see!"
UNSEEN_CUSTOMER_MARKER = BANKER_SAYS_FORMAT.format(UNSEEN_CUSTOMER_TEXT)
CLOSED_TEXT = "I'm closed. Come back later."
BANK_CLOSED_FOR_NOW_MARKER = "A broken banker says 'The bank is closed for now.'"
# The game hours after which the morning and evening bankers' hours swap, and the most hours a
# mortal can idle without being flagged AFK: point_update (limits.cpp) counts one idle tick
# per hour and flags the character at the fourth.
SWAP_HOURS = 12
IDLE_SAFE_HOURS = 3
# The boot log line naming the broken banker's bad fee (parse_banker_options, vendor_problem_line
# in mob_options.cpp): fee is the first line of its options.
BAD_FEE_LOG_LINE = "MOB ERROR: mobile #1308, options line 1: bad fee - banker disabled"
# The marker of a SYSERR line, which fails a harness run (CrashMonitor); a problem with a mob's
# options is logged without it.
SYSERR_MARKER = "SYSERR"
# What the attacker reads when a fight with its target has started (do_hit, act_offe.cpp).
FIGHT_ONGOING_MARKER = "You do the best you can!"
# What a character reads for a command above its level (command_interpreter, interpre.cpp).
UNRECOGNIZED_MARKER = "Unrecognized command."
# The implementor's reply to `invis` with no argument (do_invis, act_wiz.cpp).
INVISIBLE_MARKER = f"Your invisibility level is {IMPLEMENTOR.level}."
# The last line `stat file` prints before the affect lines (do_stat_character, act_wiz.cpp).
STAT_LAST_LINE_MARKER = "VUL:"

# The balance header, then its coin and slot lines (banker_balance and format_bank_balance,
# banker.cpp). No fee is configured, so the item table has no fee column.
BALANCE_HEADER = "Your vault:"
ITEM_TABLE_HEADER = " #  Item"


def coins_line(coins: str) -> str:
    """The balance's coin line for an amount as money_message (handler.cpp) writes it."""
    return f"Coins: {coins} (limit {VAULT_COIN_LIMIT_GOLD} gold)"


def slots_line(used: int, slots: int = VAULT_SLOTS) -> str:
    """The balance's slot line."""
    return f"Slots: {used} of {slots} used"


def balance_lines(coins: str, item_rows: tuple = ()) -> tuple:
    """The whole balance for a vault holding coins and one row per item, in slot order."""
    lines = (BALANCE_HEADER, coins_line(coins), slots_line(len(item_rows)))
    if not item_rows:
        return lines
    numbered = tuple(f"{number:2d}  {row}" for number, row in enumerate(item_rows, start=1))
    return lines + ("", ITEM_TABLE_HEADER) + numbered


EMPTY_VAULT_LINES = balance_lines("0 copper")
ONE_CAP_VAULT_LINES = balance_lines("0 copper", (CAP_NAME,))
# What the depositor reads when the banker takes or returns an item (bank_deposit, bank_withdraw).
DEPOSITED_FORMAT = "You hand over {}."
WITHDRAWN_FORMAT = "You are handed {}."
DEPOSITED_CAP_MARKER = DEPOSITED_FORMAT.format(CAP_NAME)
WITHDRAWN_CAP_MARKER = WITHDRAWN_FORMAT.format(CAP_NAME)
# What bystanders read for an item deposit or withdrawal (bank_deposit, bank_withdraw).
ROOM_DEPOSIT_FORMAT = "{} deposits {}."
ROOM_WITHDRAW_FORMAT = "{} withdraws {}."

# The vault files of each side (bank_side_file_name, banker.cpp), kept in the account's own
# folder beside account.json (vault_location, account_character_directory).
LIGHT_VAULT_FILE_NAME = "vault_light.json"
DARK_VAULT_FILE_NAME = "vault_dark.json"
ACCOUNT_FILE_NAME = "account.json"
# The settings file `gameoptions` writes (BOOT_OPTIONS_PATH, game_boot_options.h; the game's
# working directory is lib/).
BOOT_OPTIONS_FILE = Path("misc") / "game_boot_options.json"

# Staff replies: `vault` (vault_change, banker.cpp) and `gameoptions` (do_gameoptions and
# boot_option_set, game_boot_options.cpp).
VAULT_TAKE_FORMAT = "You take {} from the vault."
VAULT_PUT_FORMAT = "You put {} into the vault."
NO_SUCH_SLOT_MARKER = "No such slot."
HOW_MUCH_MARKER = "How much?"
ACCOUNT_NAME_ONLY_MARKER = "Use the account name shown by 'vault <name>'."
STAFF_VAULT_FULL_MARKER = "That vault is full."
BANK_SLOTS_SET_FORMAT = "bank_slots set to {}. It takes effect at the next reboot."
BANK_SLOTS_RANGE_MARKER = "bank_slots must be 1-100."
NO_SUCH_SETTING_MARKER = "No such setting. Type 'gameoptions' for the list."
GAMEOPTIONS_USAGE_MARKER = "Usage: gameoptions <name> <number>"
# Every reply `vault` and `gameoptions` can give, so a staff command's expect lists each outcome.
STAFF_OUTCOMES = (
    "You take ",
    "You put ",
    NO_SUCH_SLOT_MARKER,
    HOW_MUCH_MARKER,
    ACCOUNT_NAME_ONLY_MARKER,
    STAFF_VAULT_FULL_MARKER,
    "The vault doesn't hold that much.",
    "That would pass the vault's coin limit.",
    "You don't have that.",
    "You don't have that much.",
    "The bank can't hold that.",
    "Usage: vault",
    "No account or character by that name.",
    " set to ",
    " must be ",
    NO_SUCH_SETTING_MARKER,
    GAMEOPTIONS_USAGE_MARKER,
    UNRECOGNIZED_MARKER,
    "Account: ",
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


def reply_text(reply: Transcript) -> str:
    """The reply with the carriage returns of the game's line ends removed."""
    return reply.text.replace("\r", "")


def assert_lines(reply: Transcript, lines: tuple) -> None:
    """Asserts the reply holds lines, consecutively and in order."""
    block = "\n".join(lines)
    assert block in reply_text(reply), f"expected:\n{block}\nin:\n{reply.text}"


def staff_reply(session: GameSession, line: str) -> str:
    """Sends a staff command and returns its reply, without its carriage returns.

    The reply is awaited by its own text, not by a prompt: a multi-line staff reply can hold a
    line ending in `]` or `>`, where command() would stop reading.
    """
    session.send_line(line)
    return (session.expect(list(STAFF_OUTCOMES)) + session.drain(0.5)).replace("\r", "")


def text_seen_before_a_say(actor: GameSession, actor_character: Character, observer: GameSession) -> str:
    """What observer has read up to the actor's next `say`.

    The say reaches observer after anything the actor's earlier commands showed the room.
    """
    actor.command("say done")
    return observer.expect([f"{actor_character.name} says '"]).replace("\r", "")


def hand_object(implementor: GameSession, vnum: int, keyword: str, recipient: Character) -> None:
    """Loads an object into the implementor's inventory and gives it to recipient.

    The implementor must stand in the recipient's room.
    """
    created = implementor.command(f"load obj {vnum}")
    assert created.contains("You create"), created.text
    given = implementor.command(f"give {keyword} {recipient.name.lower()}")
    assert given.contains(f"to {recipient.name}."), given.text


def go_to_bank_hall(implementor: GameSession) -> None:
    """Brings the implementor to the bank hall."""
    arrived = implementor.command(f"goto {ROOM_BANK_HALL}")
    assert arrived.room_name() == "Bank Hall", arrived.text


def give_cap_in_bank_hall(implementor: GameSession, recipient: Character) -> None:
    """Brings the implementor to the bank hall and gives recipient a leather cap there."""
    go_to_bank_hall(implementor)
    hand_object(implementor, CAP_VNUM, "cap", recipient)


def set_gold(implementor: GameSession, character: Character, copper: int) -> None:
    """Sets a character's purse with wizset and checks the reply."""
    reply = implementor.command(f"wizset player {character.name.lower()} gold {copper}")
    assert reply.contains(f"gold set to {copper}."), reply.text


def account_folder(lib_dir: Path) -> Path:
    """The harness account's folder: the one folder under lib/accounts holding an account file."""
    account_files = sorted((lib_dir / "accounts").glob(f"*/*/{ACCOUNT_FILE_NAME}"))
    assert len(account_files) == 1, account_files
    return account_files[0].parent


def account_record(lib_dir: Path) -> dict:
    """The harness account's account.json."""
    return json.loads((account_folder(lib_dir) / ACCOUNT_FILE_NAME).read_text(encoding="utf-8"))


def stored_vnums(path: Path) -> list:
    """The item numbers of every object record in a vault or character objects file."""
    data = json.loads(path.read_text(encoding="utf-8"))
    if "slots" in data:
        return [stored["item_number"] for slot in data["slots"] for stored in slot["objects"]]
    return [stored["item_number"] for stored in data["objects"]]


@pytest.mark.bank_deposits
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, DEPOSITOR)
def test_a_banker_keeps_an_item_lists_it_and_hands_it_back(server) -> None:
    """Pins depositing, listing and withdrawing an item at a banker (PR #369).

    `deposit cap` takes the cap from loose inventory into the vault, `balance` lists it as slot
    1, `withdraw 1` hands it back, and a second `withdraw 1` finds the slot gone. The banker sets
    no fee, so no withdrawal depends on the wall-clock bank day; nothing on this path draws.
    """
    implementor = server.login(IMPLEMENTOR.name)
    depositor = server.login(DEPOSITOR.name)
    give_cap_in_bank_hall(implementor, DEPOSITOR)

    assert depositor.command("deposit cap").contains(DEPOSITED_CAP_MARKER)
    assert CAP_NAME not in depositor.command("inventory").text
    assert_lines(depositor.command("balance"), ONE_CAP_VAULT_LINES)

    assert depositor.command("withdraw 1").contains(WITHDRAWN_CAP_MARKER)
    assert depositor.command("inventory").contains(CAP_NAME)
    assert_lines(depositor.command("balance"), EMPTY_VAULT_LINES)

    assert depositor.command("withdraw 1").contains(NOTHING_LIKE_THAT_MARKER)


@pytest.mark.bank_deposits
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(PURSED_DEPOSITOR)
def test_a_banker_keeps_coins_up_to_what_the_purse_and_the_vault_hold(server) -> None:
    """Pins coin deposits and withdrawals at a banker, and their refusals (PR #369).

    With 10 gold in the purse, `deposit 20 gold` is refused and `deposit 5 gold` is taken; with
    5 gold in the vault, `withdraw 9 gold` is refused and `withdraw 2 gold` is paid out. score
    shows the purse after each change. Nothing on this path draws.
    """
    depositor = server.login(DEPOSITOR.name)
    assert depositor.command("score").contains("Gold: 10,")

    assert depositor.command("deposit 20 gold").contains(PURSE_SHORT_MARKER)
    assert depositor.command("deposit 5 gold").contains("You deposit 5 gold.")
    assert depositor.command("score").contains("Gold: 5,")
    assert depositor.command("balance").contains(coins_line("5 gold"))

    assert depositor.command("withdraw 9 gold").contains(VAULT_SHORT_MARKER)
    assert depositor.command("withdraw 2 gold").contains("You withdraw 2 gold.")
    assert depositor.command("score").contains("Gold: 7,")
    assert depositor.command("balance").contains(coins_line("3 gold"))


@pytest.mark.bank_deposits
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, DEPOSITOR, KINSMAN, OUTSIDER)
def test_an_accounts_characters_share_one_vault_per_side(server) -> None:
    """Pins that a vault belongs to the account and the side, not the character (PR #369).

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
    assert depositor.command("deposit cap").contains(DEPOSITED_CAP_MARKER)

    assert_lines(kinsman.command("balance"), ONE_CAP_VAULT_LINES)

    outsider_balance = outsider.command("balance")
    assert_lines(outsider_balance, EMPTY_VAULT_LINES)
    assert CAP_NAME not in outsider_balance.text, outsider_balance.text

    folder = account_folder(server.lib_dir)
    light_vault_bytes = (folder / LIGHT_VAULT_FILE_NAME).read_bytes()
    set_gold(implementor, OUTSIDER, PURSE_COPPER)
    assert outsider.command("deposit 5 gold").contains("You deposit 5 gold.")
    dark_vault = json.loads((folder / DARK_VAULT_FILE_NAME).read_text(encoding="utf-8"))
    assert (dark_vault["coins"], dark_vault["slots"]) == (5 * COPPER_IN_GOLD, []), dark_vault
    assert (folder / LIGHT_VAULT_FILE_NAME).read_bytes() == light_vault_bytes


@pytest.mark.bank_deposits
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, DEPOSITOR)
def test_a_deposited_item_is_written_to_the_accounts_vault_file(server) -> None:
    """Pins that a deposit writes the light side's vault file in the account's folder (PR #369).

    The vault is written before the character is saved, so the file holds the cap as soon as
    the deposit message arrives: one slot whose top object (nesting depth 0, kept in wear_pos)
    is the cap, and no coins. Nothing on this path draws.
    """
    implementor = server.login(IMPLEMENTOR.name)
    depositor = server.login(DEPOSITOR.name)
    vault_path = account_folder(server.lib_dir) / LIGHT_VAULT_FILE_NAME
    assert not vault_path.exists(), f"{vault_path} exists before any deposit"
    give_cap_in_bank_hall(implementor, DEPOSITOR)

    assert depositor.command("deposit cap").contains(DEPOSITED_CAP_MARKER)

    vault = json.loads(vault_path.read_text(encoding="utf-8"))
    assert (vault["version"], vault["coins"], len(vault["slots"])) == (1, 0, 1), vault
    stored_objects = vault["slots"][0]["objects"]
    assert [(stored["item_number"], stored["wear_pos"]) for stored in stored_objects] == [(CAP_VNUM, 0)], vault


@pytest.mark.bank_deposits
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, DEPOSITOR)
def test_a_deposit_and_a_withdrawal_save_the_characters_objects_file(server) -> None:
    """Pins the character half of a vault move: the character's objects file (PR #369).

    A deposit saves the character without the item (bank_save_character runs save_char and
    Crash_crashsave), so its objects file no longer lists the cap; a withdrawal saves it with
    the cap, and the vault file then no longer holds it. Nothing on this path draws.
    """
    implementor = server.login(IMPLEMENTOR.name)
    depositor = server.login(DEPOSITOR.name)
    give_cap_in_bank_hall(implementor, DEPOSITOR)
    objects_file_name = f"{DEPOSITOR.name.lower()}.objects.json"

    assert depositor.command("deposit cap").contains(DEPOSITED_CAP_MARKER)
    objects_files = sorted(server.lib_dir.rglob(objects_file_name))
    assert len(objects_files) == 1, objects_files
    assert CAP_VNUM not in stored_vnums(objects_files[0])

    assert depositor.command("withdraw 1").contains(WITHDRAWN_CAP_MARKER)
    assert CAP_VNUM in stored_vnums(objects_files[0])
    assert CAP_VNUM not in stored_vnums(account_folder(server.lib_dir) / LIGHT_VAULT_FILE_NAME)


@pytest.mark.bank_refusals
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, DEPOSITOR)
def test_a_banker_refuses_a_gift(server) -> None:
    """Pins that a banker refuses what a player gives it, and the player keeps it (PR #369).

    SPECIAL(banker) answers `give` aimed at itself with a tell before do_give runs. Nothing on
    this path draws.
    """
    implementor = server.login(IMPLEMENTOR.name)
    depositor = server.login(DEPOSITOR.name)
    give_cap_in_bank_hall(implementor, DEPOSITOR)

    assert depositor.command("give cap banker").contains(NO_GIFTS_MARKER)
    assert depositor.command("inventory").contains(CAP_NAME)


@pytest.mark.bank_refusals
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(DEPOSITOR)
def test_a_banker_refuses_an_attack_and_no_fight_starts(server) -> None:
    """Pins that a banker cancels an attack on it with a tell (PR #369's shared keeper protection).

    `kill banker` reaches hit() (fight.cpp). Whatever its rolls decide, every path asks the
    banker's program the SPECIAL_DAMAGE question before any damage lands: a blow through
    damage(), and a miss, dodge or parry after it. The only roll-decided path that skips it is a
    riposte, which needs a wielded weapon the banker does not have (check_riposte), so the
    refusal does not depend on a roll. keeper_protection (shopkeeper.cpp) answers with the tell,
    and damage() stops the attacker fighting, so a second `kill banker` is a new attack refused
    the same way, not do_hit's answer to a fight already under way.
    """
    depositor = server.login(DEPOSITOR.name)

    assert depositor.command("kill banker").contains(NO_ATTACKS_MARKER)
    second_attack = depositor.command("kill banker")
    assert second_attack.contains(NO_ATTACKS_MARKER), second_attack.text
    assert FIGHT_ONGOING_MARKER not in second_attack.text, second_attack.text


@pytest.mark.bank_refusals
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, DEPOSITOR)
def test_a_banker_refuses_will_kill_and_blind_before_and_after_a_stat_file(server) -> None:
    """Pins keeper protection through the interpreter, also after `stat file` (PR #369).

    `will banker` is refused by do_mental's keeper check (clerics.cpp) before its readiness
    matters or any roll. `stat file` builds and frees a character that never had a slot; before
    PR #369 that freed slot 0, and with it every SPECIAL_DAMAGE question, whose target carried
    slot 0, found no character and no keeper was asked (special(), interpre.cpp). After it, the
    mental attack, `kill banker` (hit() asks before any damage, whatever its rolls) and
    `blind banker` (refused as the command's target, SPECIAL_TARGET, before do_blinding runs)
    are all still refused with the tell.
    """
    implementor = server.login(IMPLEMENTOR.name)
    depositor = server.login(DEPOSITOR.name)

    assert depositor.command("will banker").contains(NO_ATTACKS_MARKER)

    implementor.send_line(f"stat file {DEPOSITOR.name.lower()}")
    implementor.expect([STAT_LAST_LINE_MARKER, "There is no such player.", "Sorry, you can't do that."])
    implementor.drain(1.0)

    for command in ("will banker", "kill banker", "blind banker"):
        reply = depositor.command(command)
        assert reply.contains(NO_ATTACKS_MARKER), f"{command}: {reply.text}"


@pytest.mark.bank_refusals
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, DEPOSITOR)
def test_a_banker_holds_nothing_for_an_immortal_and_says_so_to_the_room(server) -> None:
    """Pins that a banker refuses an immortal customer aloud (PR #369).

    banker_admits gives an immortal no side twice over: by its level (91 and up), and by its
    race, which advance_level (profs.cpp) sets to RACE_GOD and bank_side_for_race maps to no
    side. The banker says it holds nothing for its kind: a `say`, which the depositor in the
    room hears too. Nothing on this path draws.
    """
    implementor = server.login(IMPLEMENTOR.name)
    depositor = server.login(DEPOSITOR.name)
    go_to_bank_hall(implementor)

    assert implementor.command("balance").contains(NOT_YOUR_KIND_MARKER)
    depositor.expect([NOT_YOUR_KIND_MARKER])


@pytest.mark.bank_refusals
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, DEPOSITOR)
def test_a_banker_refuses_a_customer_it_cannot_see_aloud(server) -> None:
    """Pins a banker's refusal of a customer it cannot see (PR #369).

    The implementor turns invisible at its own level; the banker cannot see it, so
    vendor_serves_customer (shopkeeper.cpp) says so to the whole room before any other check.
    The depositor in the room hears it too. Nothing on this path draws.
    """
    implementor = server.login(IMPLEMENTOR.name)
    depositor = server.login(DEPOSITOR.name)
    go_to_bank_hall(implementor)
    assert implementor.command("invis").contains(INVISIBLE_MARKER)

    assert implementor.command("balance").contains(UNSEEN_CUSTOMER_MARKER)
    depositor.expect([UNSEEN_CUSTOMER_MARKER])


@pytest.mark.bank_refusals
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(DARK_CUSTOMER)
def test_a_banker_in_a_dark_room_refuses_a_customer_it_cannot_see(server) -> None:
    """Pins that a banker in the dark does not serve a customer it cannot see (PR #369).

    The dark vault hall has the DARK flag and nothing in it gives light; neither the dark banker
    nor the human customer has infravision, so CAN_SEE fails and the banker refuses aloud. The
    customer cannot see the banker either, so the speaker may read as someone. Nothing on this
    path draws.
    """
    customer = server.login(DARK_CUSTOMER.name)

    assert customer.command("balance").contains(f"says '{UNSEEN_CUSTOMER_TEXT}'")


@pytest.mark.bank_refusals
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, PURSED_DEPOSITOR, KINSMAN)
def test_a_bystander_sees_item_moves_but_not_coin_moves_or_the_bankers_tells(server) -> None:
    """Pins what a same-side bystander sees of another's banking (PR #369).

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
    bystander.expect([f"gives {CAP_NAME} to {DEPOSITOR.name}."])

    assert depositor.command("deposit 5 gold").contains("You deposit 5 gold.")
    assert depositor.command("withdraw 9").contains(NOTHING_LIKE_THAT_MARKER)
    assert depositor.command("deposit cap").contains(DEPOSITED_CAP_MARKER)
    seen_by_deposit = text_seen_before_a_say(depositor, DEPOSITOR, bystander)
    assert ROOM_DEPOSIT_FORMAT.format(DEPOSITOR.name, CAP_NAME) in seen_by_deposit, seen_by_deposit
    assert "gold" not in seen_by_deposit, seen_by_deposit
    assert "I hold nothing" not in seen_by_deposit, seen_by_deposit

    assert depositor.command("withdraw 2 gold").contains("You withdraw 2 gold.")
    assert depositor.command("withdraw 1").contains(WITHDRAWN_CAP_MARKER)
    seen_by_withdrawal = text_seen_before_a_say(depositor, DEPOSITOR, bystander)
    assert ROOM_WITHDRAW_FORMAT.format(DEPOSITOR.name, CAP_NAME) in seen_by_withdrawal, seen_by_withdrawal
    assert "gold" not in seen_by_withdrawal, seen_by_withdrawal


@pytest.mark.bank_refusals
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, DEPOSITOR)
def test_a_banker_refuses_a_key_loose_or_inside_a_bag(server) -> None:
    """Pins that a banker refuses an item rent would refuse, also inside a container (PR #369).

    A key is unrentable (Crash_is_unrentable, objsave.cpp), and bank_obj_storable checks a
    container's contents too, so the key and a bag holding it are both refused, and both stay
    with the depositor. Nothing on this path draws.
    """
    implementor = server.login(IMPLEMENTOR.name)
    depositor = server.login(DEPOSITOR.name)
    go_to_bank_hall(implementor)
    hand_object(implementor, KEY_VNUM, "key", DEPOSITOR)
    hand_object(implementor, BAG_VNUM, "bag", DEPOSITOR)

    assert depositor.command("deposit key").contains(CANNOT_KEEP_MARKER)
    assert depositor.command("put key bag").contains(f"You put {KEY_NAME} in {BAG_NAME}.")
    assert depositor.command("deposit bag").contains(CANNOT_KEEP_MARKER)
    assert depositor.command("inventory").contains(BAG_NAME)
    assert_lines(depositor.command("balance"), EMPTY_VAULT_LINES)


@pytest.mark.bank_refusals
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, DEPOSITOR)
def test_a_banker_refuses_a_cursed_item_inside_a_bag_or_loose(server) -> None:
    """Pins that a banker refuses a cursed (NODROP) item, also inside a container (PR #369).

    give refuses a cursed item even from the implementor, so the ring arrives inside a bag.
    has_nodrop (banker.cpp) checks a container's contents, so the bag is refused; taken out, the
    ring itself is refused. Nothing on this path draws.
    """
    implementor = server.login(IMPLEMENTOR.name)
    depositor = server.login(DEPOSITOR.name)
    go_to_bank_hall(implementor)
    assert implementor.command(f"load obj {CURSED_RING_VNUM}").contains("You create")
    assert implementor.command(f"load obj {BAG_VNUM}").contains("You create")
    assert implementor.command("put ring bag").contains(f"You put {CURSED_RING_NAME} in {BAG_NAME}.")
    given = implementor.command(f"give bag {DEPOSITOR.name.lower()}")
    assert given.contains(f"to {DEPOSITOR.name}."), given.text

    assert depositor.command("deposit bag").contains(CANNOT_KEEP_MARKER)
    assert depositor.command("get ring bag").contains(f"You get {CURSED_RING_NAME}")
    assert depositor.command("deposit ring").contains(CANNOT_KEEP_MARKER)
    assert_lines(depositor.command("balance"), EMPTY_VAULT_LINES)


@pytest.mark.bank_deposits
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, DEPOSITOR)
def test_a_vault_takes_its_tenth_item_and_refuses_the_eleventh(server) -> None:
    """Pins the vault's slot limit for a player and for staff (PR #369).

    The implementor puts nine caps in with `vault put`; the depositor's deposit fills the tenth
    and last slot (bank_slots, 10 by default), the next is refused with the player's message,
    and a staff `vault put` into the full vault is refused with the staff message. Nothing on
    this path draws.
    """
    implementor = server.login(IMPLEMENTOR.name)
    depositor = server.login(DEPOSITOR.name)
    account_name = account_record(server.lib_dir)["account_name"]
    go_to_bank_hall(implementor)
    for put_number in range(VAULT_SLOTS - 1):
        assert implementor.command(f"load obj {CAP_VNUM}").contains("You create")
        reply = staff_reply(implementor, f"vault put {account_name} 1 cap")
        assert VAULT_PUT_FORMAT.format(CAP_NAME) in reply, reply
    hand_object(implementor, CAP_VNUM, "cap", DEPOSITOR)
    hand_object(implementor, CAP_VNUM, "cap", DEPOSITOR)

    assert depositor.command("deposit cap").contains(DEPOSITED_CAP_MARKER)
    assert depositor.command("balance").contains(slots_line(VAULT_SLOTS))
    assert depositor.command("deposit cap").contains(VAULT_FULL_MARKER)
    assert depositor.command("inventory").contains(CAP_NAME)

    assert implementor.command(f"load obj {CAP_VNUM}").contains("You create")
    assert STAFF_VAULT_FULL_MARKER in staff_reply(implementor, f"vault put {account_name} 1 cap")


@pytest.mark.bank_deposits
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(RICH_DEPOSITOR)
def test_a_coin_deposit_past_the_vault_limit_is_cut_to_what_fits(server) -> None:
    """Pins the vault's coin limit for a player's deposit (PR #369).

    With 1500 gold in the purse and an empty vault whose limit is 1000 gold
    (bank_coin_limit_gold), `deposit 1500 gold` takes 1000 and says the other 500 was refused;
    the purse keeps 500. A further `deposit 1 gold` is refused outright. Nothing on this path
    draws.
    """
    depositor = server.login(DEPOSITOR.name)

    assert_lines(
        depositor.command("deposit 1500 gold"),
        (f"You deposit {VAULT_COIN_LIMIT_GOLD} gold.", "500 gold was refused: your vault is full."),
    )
    assert depositor.command("score").contains("Gold: 500,")
    assert depositor.command("deposit 1 gold").contains(NO_MORE_COINS_MARKER)
    assert depositor.command("balance").contains(coins_line(f"{VAULT_COIN_LIMIT_GOLD} gold"))


@pytest.mark.bank_deposits
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, DEPOSITOR)
def test_a_withdrawal_finds_its_slot_by_number_keyword_or_abbreviation_and_renumbers(server) -> None:
    """Pins how `withdraw` picks a slot, and the renumbering after one is taken (PR #369).

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
    hand_object(implementor, BAG_VNUM, "bag", DEPOSITOR)
    hand_object(implementor, IRON_CAP_VNUM, "cap", DEPOSITOR)
    hand_object(implementor, CAP_VNUM, "cap", DEPOSITOR)
    empty_bag_row = f"{BAG_NAME} (sealed, 0 inside)"

    assert depositor.command("deposit lea").contains(DEPOSITED_CAP_MARKER)
    assert depositor.command("deposit cap").contains(DEPOSITED_FORMAT.format(IRON_CAP_NAME))
    assert depositor.command("deposit bag").contains(DEPOSITED_FORMAT.format(BAG_NAME))
    assert_lines(depositor.command("balance"), balance_lines("0 copper", (CAP_NAME, IRON_CAP_NAME, empty_bag_row)))

    assert depositor.command("withdraw 5").contains(NOTHING_LIKE_THAT_MARKER)
    assert depositor.command("withdraw 2.cap").contains(WITHDRAWN_FORMAT.format(IRON_CAP_NAME))
    assert_lines(depositor.command("balance"), balance_lines("0 copper", (CAP_NAME, empty_bag_row)))
    assert depositor.command("withdraw lea").contains(WITHDRAWN_CAP_MARKER)
    assert_lines(depositor.command("balance"), balance_lines("0 copper", (empty_bag_row,)))


@pytest.mark.bank_deposits
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, DEPOSITOR)
def test_a_deposited_bag_keeps_its_contents_and_lists_as_sealed(server) -> None:
    """Pins depositing a container with something inside it (PR #369).

    A bag holding a cap is one slot: `balance` shows it sealed with one thing inside, and the
    vault file holds the bag at nesting depth 0 and the cap at depth 1. Nothing on this path
    draws.
    """
    implementor = server.login(IMPLEMENTOR.name)
    depositor = server.login(DEPOSITOR.name)
    go_to_bank_hall(implementor)
    hand_object(implementor, CAP_VNUM, "cap", DEPOSITOR)
    hand_object(implementor, BAG_VNUM, "bag", DEPOSITOR)
    assert depositor.command("put cap bag").contains(f"You put {CAP_NAME} in {BAG_NAME}.")

    assert depositor.command("deposit bag").contains(DEPOSITED_FORMAT.format(BAG_NAME))
    assert_lines(depositor.command("balance"), balance_lines("0 copper", (f"{BAG_NAME} (sealed, 1 inside)",)))
    vault = json.loads((account_folder(server.lib_dir) / LIGHT_VAULT_FILE_NAME).read_text(encoding="utf-8"))
    stored_objects = vault["slots"][0]["objects"]
    assert [(stored["item_number"], stored["wear_pos"]) for stored in stored_objects] == [(BAG_VNUM, 0), (CAP_VNUM, 1)]


@pytest.mark.bank_staff
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, LESSER_GOD, GREATER_GOD, ELDER_GOD)
def test_vault_and_gameoptions_need_their_command_levels(server) -> None:
    """Pins the minimum levels of `vault` (97) and `gameoptions` (100) (PR #369).

    A level-96 immortal's `vault` and a level-99 immortal's `gameoptions` are unknown commands
    to them; at level 97 `vault` shows a vault, and the implementor's `gameoptions` lists the
    settings. Nothing on this path draws.
    """
    implementor = server.login(IMPLEMENTOR.name)
    lesser_god = server.login(LESSER_GOD.name)
    greater_god = server.login(GREATER_GOD.name)
    elder_god = server.login(ELDER_GOD.name)

    assert lesser_god.command(f"vault {LESSER_GOD.name.lower()}").contains(UNRECOGNIZED_MARKER)
    assert "Account: " in staff_reply(greater_god, f"vault {GREATER_GOD.name.lower()}")
    assert elder_god.command("gameoptions").contains(UNRECOGNIZED_MARKER)
    implementor.send_line("gameoptions")
    implementor.expect(["bank_slots", UNRECOGNIZED_MARKER])


@pytest.mark.bank_staff
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, DEPOSITOR)
def test_vault_shows_a_characters_account_side_and_slots(server) -> None:
    """Pins the staff view of a character's vault (PR #369).

    `vault testsaver` names the account and its email, the character, and the character's own
    side (light, for a human) with its coins and slot count, then one line per slot. A slot's
    age in bank days is 0 unless a bank day ended between the deposit and the view, which the
    wall clock alone decides. Nothing on this path draws.
    """
    implementor = server.login(IMPLEMENTOR.name)
    depositor = server.login(DEPOSITOR.name)
    account = account_record(server.lib_dir)
    give_cap_in_bank_hall(implementor, DEPOSITOR)
    assert depositor.command("deposit cap").contains(DEPOSITED_CAP_MARKER)

    view = staff_reply(implementor, f"vault {DEPOSITOR.name.lower()}")
    expected_header = (
        f"Account: {account['account_name']} ({account['normalized_email']})\n"
        f"Character: {DEPOSITOR.name}\n"
        f"Light vault: 0 copper, 1 of {VAULT_SLOTS} slots\n"
    )
    assert expected_header in view, view
    slot_row = re.search(r"^ 1  (.{40}) stored (\d+) days?$", view, re.MULTILINE)
    assert slot_row is not None, view
    assert (slot_row.group(1).rstrip(), slot_row.group(2)) in ((CAP_NAME, "0"), (CAP_NAME, "1")), view


@pytest.mark.bank_staff
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, DEPOSITOR)
def test_vault_take_and_put_move_items_and_coins_and_the_owner_sees_it(server) -> None:
    """Pins staff moving items and coins in and out of a vault (PR #369).

    `vault take <account> 1 1` takes the deposited cap into the implementor's inventory,
    `vault put <account> 1 cap` puts it back, and the coin forms put 5 gold in and take 2 out.
    The depositor's own `balance` shows the result. Nothing on this path draws.
    """
    implementor = server.login(IMPLEMENTOR.name)
    depositor = server.login(DEPOSITOR.name)
    account_name = account_record(server.lib_dir)["account_name"]
    give_cap_in_bank_hall(implementor, DEPOSITOR)
    assert depositor.command("deposit cap").contains(DEPOSITED_CAP_MARKER)
    set_gold(implementor, IMPLEMENTOR, PURSE_COPPER)

    assert VAULT_TAKE_FORMAT.format(CAP_NAME) in staff_reply(implementor, f"vault take {account_name} 1 1")
    assert_lines(depositor.command("balance"), EMPTY_VAULT_LINES)
    assert VAULT_PUT_FORMAT.format(CAP_NAME) in staff_reply(implementor, f"vault put {account_name} 1 cap")
    assert VAULT_PUT_FORMAT.format("5 gold") in staff_reply(implementor, f"vault put {account_name} 1 coins 5 gold")
    assert VAULT_TAKE_FORMAT.format("2 gold") in staff_reply(implementor, f"vault take {account_name} 1 coins 2 gold")

    assert_lines(depositor.command("balance"), balance_lines("3 gold", (CAP_NAME,)))


@pytest.mark.bank_staff
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, DEPOSITOR)
def test_vault_take_refuses_a_missing_slot_a_bad_amount_and_an_email(server) -> None:
    """Pins `vault take`'s refusals (PR #369).

    A slot the vault does not have, a coin amount that is not a number, and an account named by
    its email rather than its name are each refused with their own reply, and the vault keeps
    its cap. Nothing on this path draws.
    """
    implementor = server.login(IMPLEMENTOR.name)
    depositor = server.login(DEPOSITOR.name)
    account = account_record(server.lib_dir)
    give_cap_in_bank_hall(implementor, DEPOSITOR)
    assert depositor.command("deposit cap").contains(DEPOSITED_CAP_MARKER)

    assert NO_SUCH_SLOT_MARKER in staff_reply(implementor, f"vault take {account['account_name']} 1 2")
    assert HOW_MUCH_MARKER in staff_reply(implementor, f"vault take {account['account_name']} 1 coins lots")
    assert ACCOUNT_NAME_ONLY_MARKER in staff_reply(implementor, f"vault take {account['normalized_email']} 1 1")
    assert_lines(depositor.command("balance"), ONE_CAP_VAULT_LINES)


@pytest.mark.bank_staff
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, DEPOSITOR)
def test_gameoptions_writes_bank_slots_for_the_next_reboot_only(server) -> None:
    """Pins `gameoptions bank_slots` (PR #369).

    The setting is written to lib/misc/game_boot_options.json and takes effect at the next
    reboot: the `gameoptions` list shows 10 now and 5 after the reboot, and the depositor's
    `balance` still counts the running limit of 10 slots. Nothing on this path draws.
    """
    implementor = server.login(IMPLEMENTOR.name)
    depositor = server.login(DEPOSITOR.name)

    assert BANK_SLOTS_SET_FORMAT.format(5) in staff_reply(implementor, "gameoptions bank_slots 5")
    settings = json.loads((server.lib_dir / BOOT_OPTIONS_FILE).read_text(encoding="utf-8"))
    assert settings["bank_slots"] == 5, settings
    implementor.send_line("gameoptions")
    listing = (implementor.expect(["daily_reboot_hour_utc"]) + implementor.drain(0.5)).replace("\r", "")
    assert re.search(r"^bank_slots +10 +5 +10 +1-100$", listing, re.MULTILINE), listing
    assert depositor.command("balance").contains(slots_line(0, VAULT_SLOTS))


@pytest.mark.bank_staff
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR)
def test_gameoptions_refuses_out_of_range_values_unknown_names_and_bad_numbers(server) -> None:
    """Pins `gameoptions`' refusals and its range ends (PR #369).

    bank_slots accepts 1 to 100: 100 is set, 0 and 101 are refused with the range. An unknown
    setting, a missing number and a number with trailing text each get their own reply. Nothing
    on this path draws.
    """
    implementor = server.login(IMPLEMENTOR.name)

    assert BANK_SLOTS_SET_FORMAT.format(100) in staff_reply(implementor, "gameoptions bank_slots 100")
    assert BANK_SLOTS_RANGE_MARKER in staff_reply(implementor, "gameoptions bank_slots 0")
    assert BANK_SLOTS_RANGE_MARKER in staff_reply(implementor, "gameoptions bank_slots 101")
    assert NO_SUCH_SETTING_MARKER in staff_reply(implementor, "gameoptions bank_size 5")
    assert GAMEOPTIONS_USAGE_MARKER in staff_reply(implementor, "gameoptions bank_slots")
    assert GAMEOPTIONS_USAGE_MARKER in staff_reply(implementor, "gameoptions bank_slots 5x")
    settings = json.loads((server.lib_dir / BOOT_OPTIONS_FILE).read_text(encoding="utf-8"))
    assert settings["bank_slots"] == 100, settings


@pytest.mark.bank_hours
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, EARLY_CUSTOMER, LATE_CUSTOMER)
def test_bankers_open_only_in_their_hours_and_swap_after_twelve_hours(server) -> None:
    """Pins a banker's hours= option (PR #369).

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

    def closed_flags() -> tuple:
        flags = []
        for customer in (early, late):
            reply = customer.command("balance")
            assert reply.contains(CLOSED_TEXT) or reply.contains(BALANCE_HEADER), reply.text
            flags.append(reply.contains(CLOSED_TEXT))
        return tuple(flags)

    before = closed_flags()
    assert before in ((True, False), (False, True)), before

    for step in range(SWAP_HOURS // IDLE_SAFE_HOURS):
        server.advance_hours(IDLE_SAFE_HOURS)
        for customer in (early, late):
            customer.drain(1.0)
            assert customer.command("look").room_name() in ("Morning Bank", "Evening Bank")
    after = closed_flags()
    assert after == (not before[0], not before[1]), (before, after)


@pytest.mark.bank_hours
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(STRANDED_CUSTOMER)
def test_a_banker_with_a_bad_fee_is_logged_at_boot_and_closed(server) -> None:
    """Pins a banker its options disable (PR #369).

    The broken banker's options set fee=0, outside 1 to BANKER_FEE_MAX: the boot logs a
    `MOB ERROR` line for it, not a SYSERR, and `balance` gets the closed-for-now answer, said to
    the room. Nothing on this path draws.
    """
    customer = server.login(STRANDED_CUSTOMER.name)

    boot_log = (server.run_dir / "game.log").read_text(encoding="utf-8", errors="replace")
    problem_lines = [line for line in boot_log.splitlines() if BAD_FEE_LOG_LINE in line]
    assert len(problem_lines) == 1, boot_log[-3000:]
    assert SYSERR_MARKER not in problem_lines[0], problem_lines[0]

    assert customer.command("balance").contains(BANK_CLOSED_FOR_NOW_MARKER)
