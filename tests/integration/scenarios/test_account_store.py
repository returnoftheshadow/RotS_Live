"""The account store from inside the game: deleting a linked character, the immortal `account`
command's reports, refusals, blocks and error log, and display settings shared by an account's
characters."""

from __future__ import annotations

import contextlib
import json
import re
from typing import Iterator

import pytest

from rots_harness import world
from rots_harness.characters import HARNESS_EMAIL, HARNESS_PASSWORD, IMPLEMENTOR, Character
from rots_harness.launcher import LOGIN_EMAIL_PROMPT
from rots_harness.session import (
    ACCOUNT_MENU_PLAY,
    ACCOUNT_MENU_PROMPT,
    CHARACTER_CHOICE_PROMPT,
    LOGIN_PASSWORD_PROMPT,
    GameSession,
)

pytestmark = [pytest.mark.scenario, pytest.mark.account_menu, pytest.mark.account_menu_store]

# A level-12 human warrior linked to the harness account, which the implementor deletes; never online.
VICTIM = Character(
    name="Testdoomed", race="human", sex="male", level=12, load_room=world.ROOM_ARENA_CENTRE, class_name="warrior"
)
# A level-20 human warrior on the harness account, online beside the implementor.
MORTAL = Character(
    name="Testsibling", race="human", sex="female", level=20, load_room=world.ROOM_ARENA_CENTRE, class_name="warrior"
)

# The password do_delete requires after the character's name (act_wiz.cpp).
DELETE_PASSWORD = "rots"
# What do_delete prints when it refuses (act_wiz.cpp); on success it prints nothing.
DELETE_WRONG_PASSWORD_REFUSAL = "Incorrect or missing password"
DELETE_NO_SUCH_PLAYER_REFUSAL = "No such player"
DELETE_REFUSALS = (
    DELETE_WRONG_PASSWORD_REFUSAL,
    DELETE_NO_SUCH_PLAYER_REFUSAL,
    "That character could not be deleted; check the log.",
)
# A name no player in the test has.
UNKNOWN_PLAYER = "nosuch"
# The harness account's folder in the lib, accounts/<letter range>/<email>, as rotstool writes it
# for the server's account layer (account_management.cpp).
ACCOUNT_FOLDER = f"accounts/F-J/{HARNESS_EMAIL}"
# The account's record in that folder (account_file_path_from_email, account_management.cpp).
ACCOUNT_FILE = f"{ACCOUNT_FOLDER}/account.json"
# The folder delete_player_character_by_index copies a linked character's file into (db.cpp).
ARCHIVE_FOLDER = "players/ZZZ"
# The start of the character list line of `account show`
# (format_account_summary, account_management_presentation.cpp).
ACCOUNT_SHOW_CHARACTERS_PREFIX = "Characters ("
# The three files account storage keeps for each linked character (account_management.cpp).
CHARACTER_FILE_KINDS = ("character", "objects", "exploits")
# The level and race columns of a roster row whose character file cannot be read
# (format_account_character_short_entry, account_management.cpp).
UNREADABLE_ROW_COLUMNS = "[ ?? ???]"
# One readable roster row, `%zu) [%3d %s] %-12.12s`: row, level, race, name (account_management.cpp).
ROSTER_ROW_PATTERN = re.compile(r"(\d+)\) \[ *(\d+) (\w+)\] (\w+)")

# What `account index` reports when the boot indexed the one account cleanly (do_account, act_wiz.cpp).
CLEAN_INDEX_REPORT = "Account index: 1 record(s) indexed, 0 quarantined, 0 unreadable since boot."
# What `account errors` reports when nothing has been recorded since boot (do_account).
NO_ACCOUNT_ERRORS_REPORT = "No account errors recorded since boot."
# Each `account` usage mistake that refuses before touching an account, and its reply (do_account;
# the unknown address's reply is read_account_file_by_identifier's).
ACCOUNT_COMMAND_REFUSALS = (
    ("account", "Usage: account <show|verify|unverify|block|unblock|passwd|addchar|migratechar|unlockselect|index|errors> <email-or-account> [value]"),
    ("account show nobody@example.com", "No account exists for that email address."),
    ("account index foo", "Usage: account index"),
    ("account errors 0", "Usage: account errors [rows]"),
    (f"account block {HARNESS_EMAIL}", "Usage: account block <email-or-account> <reason>"),
)
# A password `account passwd` refuses for its length, and the refusal (is_valid_password,
# account_management_identity.cpp).
SHORT_PASSWORD = "short"
SHORT_PASSWORD_REFUSAL = "Passwords must be at least 8 characters long."
# What `account addchar` prints for a character the account already links (add_character_to_account).
ALREADY_LINKED_REFUSAL = "Character is already linked to this account."
# The replies to `account block` and `account unblock` (do_account).
ACCOUNT_BLOCKED_REPLY = "Account blocked."
ACCOUNT_UNBLOCKED_REPLY = "Account unblocked."
# The reason given for the block.
BLOCK_REASON = "scenario"
# What a blocked account's correct password prints, the same as a wrong one (CON_ACCTPWD in nanny,
# interpre.cpp; authenticate_account).
INVALID_CREDENTIALS_MARKER = "Invalid account credentials."
# A character with no file in players/, so `account migratechar` fails and records the failure.
UNMIGRATABLE_NAME = "Nosuchchar"
# The `account errors` header after one recorded failure (do_account).
ONE_ACCOUNT_ERROR_HEADER = "Account errors since boot: 1 recorded, showing 1 (newest first)."
# The harness account's internal name, which the rows and log lines show (rotstool).
HARNESS_ACCOUNT_NAME = "harness"
# The source, account, character and actor of the failed migration, as a row and a log line show them.
MIGRATION_ERROR_SUBJECT = f"migration acct={HARNESS_ACCOUNT_NAME} char={UNMIGRATABLE_NAME} by={IMPLEMENTOR.name}:"
# One `account errors` row, `  [%s]%s %-9s acct=%s char=%s%s%s: %s`: time, repeat count, source,
# account, character, actor and reason (do_account).
ACCOUNT_ERROR_ROW_PATTERN = re.compile(rf"^  \[[^\]]+\]( x\d+)? {re.escape(MIGRATION_ERROR_SUBJECT)} (.+)$", re.MULTILINE)
# The game.log line account_errors::record writes for the failed migration (account_errors.cpp).
ACCOUNT_ERROR_LOG_LINE = f"ACCTERR {MIGRATION_ERROR_SUBJECT}"

# What `set brief on` prints to the character who types it (tog_messages, act_othe.cpp).
BRIEF_TURNED_ON = "Brief mode now on."
# What `set brief` without an argument prints for each state (flag_void, act_othe.cpp).
BRIEF_IS_ON = "Brief mode on."
BRIEF_IS_OFF = "Brief mode off."
# What `set notell on` prints, and what `set notell` prints for each state (tog_messages).
NOTELL_TURNED_ON = "You are now deaf to tells."
CAN_HEAR_TELLS = "You can hear tells."
DEAF_TO_TELLS = "You are deaf to tells."


@contextlib.contextmanager
def account_connection(server, label: str) -> Iterator[GameSession]:
    """A connection at the account email prompt, closed when the block ends.

    It plays no character, so the server fixture does not quit it; label names its transcript.
    """
    connection = GameSession(server.handle, label, HARNESS_EMAIL, HARNESS_PASSWORD, server.run_dir)
    try:
        connection.expect([LOGIN_EMAIL_PROMPT])
        yield connection
    finally:
        connection.close()


def password_reply(connection: GameSession) -> str:
    """Sends the harness email and its correct password; returns the reply up to the next prompt."""
    connection.send_line(HARNESS_EMAIL)
    connection.expect([LOGIN_PASSWORD_PROMPT])
    connection.send_line(HARNESS_PASSWORD)
    return connection.expect([LOGIN_PASSWORD_PROMPT, ACCOUNT_MENU_PROMPT])


def account_show_characters(implementor: GameSession) -> str:
    """The character list line `account show` prints for the harness account."""
    reply = implementor.command(f"account show {HARNESS_EMAIL}").text
    lines = [line.strip() for line in reply.splitlines() if line.strip().startswith(ACCOUNT_SHOW_CHARACTERS_PREFIX)]
    assert len(lines) == 1, reply
    return lines[0]


def stored_account(server) -> dict:
    """The harness account's record as the server last wrote it."""
    return json.loads((server.lib_dir / ACCOUNT_FILE).read_text(encoding="utf-8"))


@pytest.mark.characters(IMPLEMENTOR, VICTIM)
def test_deleting_a_linked_character_unlinks_it_and_archives_its_file(server) -> None:
    """Pins #282: `delete` takes a linked character off its account (account show, the record's
    character links and the roster, which shows no unreadable row) and out of the account's folder,
    keeping a copy of its file in players/ZZZ."""
    implementor = server.login(IMPLEMENTOR.name)
    victim_key = VICTIM.name.lower()
    account_folder = server.lib_dir / ACCOUNT_FOLDER
    victim_files = [account_folder / f"{victim_key}.{kind}.json" for kind in CHARACTER_FILE_KINDS]
    for victim_file in victim_files:
        assert victim_file.is_file(), f"missing before the delete: {victim_file}"
    before = account_show_characters(implementor)
    assert before == f"Characters (2): {IMPLEMENTOR.name.lower()}, {victim_key}", before

    reply = implementor.command(f"delete {victim_key} {DELETE_PASSWORD}")
    for refusal in DELETE_REFUSALS:
        assert refusal not in reply.text, reply.text

    after = account_show_characters(implementor)
    assert after == f"Characters (1): {IMPLEMENTOR.name.lower()}", after
    linked_names = [link["character_name"] for link in stored_account(server)["character_links"]]
    assert linked_names == [IMPLEMENTOR.name.lower()], linked_names
    left_in_account = sorted(entry.name for entry in account_folder.iterdir() if entry.name.startswith(victim_key))
    assert left_in_account == [], left_in_account
    archived = sorted(entry.name for entry in (server.lib_dir / ARCHIVE_FOLDER).iterdir())
    assert archived == [victim_key], archived
    with account_connection(server, "afterdelete") as connection:
        assert ACCOUNT_MENU_PROMPT in password_reply(connection)
        connection.send_line(ACCOUNT_MENU_PLAY)
        roster = connection.expect([CHARACTER_CHOICE_PROMPT])
        assert [match[3] for match in ROSTER_ROW_PATTERN.findall(roster)] == [IMPLEMENTOR.name], roster
        assert UNREADABLE_ROW_COLUMNS not in roster, roster
        assert "1 character displayed." in roster, roster


@pytest.mark.characters(IMPLEMENTOR, VICTIM)
def test_delete_refuses_an_unknown_player_and_a_wrong_password_and_changes_nothing(server) -> None:
    """Pins #282: `delete` names its refusal for a player that does not exist and for a wrong
    password, and the character stays linked with its files."""
    implementor = server.login(IMPLEMENTOR.name)
    victim_key = VICTIM.name.lower()
    unknown = implementor.command(f"delete {UNKNOWN_PLAYER} {DELETE_PASSWORD}")
    assert DELETE_NO_SUCH_PLAYER_REFUSAL in unknown.text, unknown.text
    wrong_password = implementor.command(f"delete {victim_key} {DELETE_PASSWORD}x")
    assert DELETE_WRONG_PASSWORD_REFUSAL in wrong_password.text, wrong_password.text
    assert account_show_characters(implementor) == f"Characters (2): {IMPLEMENTOR.name.lower()}, {victim_key}"
    for kind in CHARACTER_FILE_KINDS:
        assert (server.lib_dir / ACCOUNT_FOLDER / f"{victim_key}.{kind}.json").is_file(), kind


@pytest.mark.characters(IMPLEMENTOR)
def test_account_index_reports_one_clean_record_after_a_clean_boot(server) -> None:
    """Pins #297: `account index` counts the harness account's record, none quarantined or unreadable."""
    implementor = server.login(IMPLEMENTOR.name)
    reply = implementor.command("account index")
    assert CLEAN_INDEX_REPORT in reply.text, reply.text


@pytest.mark.characters(IMPLEMENTOR)
def test_account_errors_reports_none_after_a_clean_boot(server) -> None:
    """Pins #297: `account errors` on a boot with nothing recorded says so instead of printing an empty list."""
    implementor = server.login(IMPLEMENTOR.name)
    reply = implementor.command("account errors")
    assert NO_ACCOUNT_ERRORS_REPORT in reply.text, reply.text


@pytest.mark.characters(IMPLEMENTOR)
def test_account_usage_mistakes_are_refused_with_their_usage_line(server) -> None:
    """Pins #297: `account` with no subcommand, an unknown address, a word after `index`, a row count
    of 0, and a block with no reason are each refused with their own line."""
    implementor = server.login(IMPLEMENTOR.name)
    for command, refusal in ACCOUNT_COMMAND_REFUSALS:
        reply = implementor.command(command)
        assert refusal in reply.text, f"{command!r}: {reply.text}"
    assert stored_account(server)["blocked"] is False


@pytest.mark.characters(IMPLEMENTOR)
def test_account_passwd_refuses_a_short_password_and_keeps_the_old_one(server) -> None:
    """Pins #297: `account passwd` with a password under eight characters says why and leaves the
    stored password as it was."""
    implementor = server.login(IMPLEMENTOR.name)
    password_hash = stored_account(server)["password_hash"]
    reply = implementor.command(f"account passwd {HARNESS_EMAIL} {SHORT_PASSWORD}")
    assert SHORT_PASSWORD_REFUSAL in reply.text, reply.text
    assert stored_account(server)["password_hash"] == password_hash


@pytest.mark.characters(IMPLEMENTOR)
def test_account_addchar_refuses_a_character_the_account_already_links(server) -> None:
    """Pins #297: `account addchar` of a character already on the account says so and links nothing twice."""
    implementor = server.login(IMPLEMENTOR.name)
    reply = implementor.command(f"account addchar {HARNESS_EMAIL} {IMPLEMENTOR.name}")
    assert ALREADY_LINKED_REFUSAL in reply.text, reply.text
    assert account_show_characters(implementor) == f"Characters (1): {IMPLEMENTOR.name.lower()}"


@pytest.mark.characters(IMPLEMENTOR)
def test_a_blocked_account_cannot_sign_in_until_it_is_unblocked(server) -> None:
    """Pins #297: `account block` makes the account's correct password read as invalid credentials,
    and `account unblock` lets it sign in again."""
    implementor = server.login(IMPLEMENTOR.name)
    blocked = implementor.command(f"account block {HARNESS_EMAIL} {BLOCK_REASON}")
    assert ACCOUNT_BLOCKED_REPLY in blocked.text, blocked.text
    with account_connection(server, "blockedlogin") as connection:
        refused = password_reply(connection)
        assert INVALID_CREDENTIALS_MARKER in refused and ACCOUNT_MENU_PROMPT not in refused, refused
    unblocked = implementor.command(f"account unblock {HARNESS_EMAIL}")
    assert ACCOUNT_UNBLOCKED_REPLY in unblocked.text, unblocked.text
    with account_connection(server, "unblockedlogin") as connection:
        admitted = password_reply(connection)
        assert ACCOUNT_MENU_PROMPT in admitted and INVALID_CREDENTIALS_MARKER not in admitted, admitted


@pytest.mark.characters(IMPLEMENTOR)
def test_a_failed_migration_is_listed_by_account_errors_and_a_repeat_is_counted_on_its_row(server) -> None:
    """Pins #297: a failed `account migratechar` shows its reason to the immortal, `account errors`
    lists it under a one-record header with the same reason, and the same failure again is counted
    as x2 on that row while game.log keeps the one line of its first occurrence."""
    implementor = server.login(IMPLEMENTOR.name)
    migrate_command = f"account migratechar {HARNESS_EMAIL} {UNMIGRATABLE_NAME}"
    failed = implementor.command(migrate_command).text
    first_report = implementor.command("account errors").text
    assert ONE_ACCOUNT_ERROR_HEADER in first_report, first_report
    rows = ACCOUNT_ERROR_ROW_PATTERN.findall(first_report.replace("\r", ""))
    assert len(rows) == 1 and rows[0][0] == "", first_report
    assert rows[0][1] in failed, (failed, rows)

    implementor.command(migrate_command)
    second_report = implementor.command("account errors").text
    assert ONE_ACCOUNT_ERROR_HEADER in second_report, second_report
    repeated_rows = ACCOUNT_ERROR_ROW_PATTERN.findall(second_report.replace("\r", ""))
    assert [repeat for repeat, _reason in repeated_rows] == [" x2"], second_report
    log_lines = [line for line in server.handle.log_path.read_text(encoding="latin-1").splitlines() if ACCOUNT_ERROR_LOG_LINE in line]
    assert len(log_lines) == 1, log_lines


@pytest.mark.characters(IMPLEMENTOR, MORTAL)
def test_a_display_setting_changed_by_one_character_shows_on_the_accounts_other_online_character(server) -> None:
    """Pins #294: brief mode turned on by one character is on for the same account's other online character.

    Brief mode is one of the display settings the account holds (PPC_PRF_MASK, structs.h); the
    harness account starts with it off.
    """
    implementor = server.login(IMPLEMENTOR.name)
    mortal = server.login(MORTAL.name)
    before = mortal.command("set brief")
    assert BRIEF_IS_OFF in before.text, before.text

    turned_on = implementor.command("set brief on")
    assert BRIEF_TURNED_ON in turned_on.text, turned_on.text

    after = mortal.command("set brief")
    assert BRIEF_IS_ON in after.text and BRIEF_IS_OFF not in after.text, after.text


@pytest.mark.characters(IMPLEMENTOR, MORTAL)
def test_a_setting_outside_the_shared_display_settings_stays_with_the_character_that_changed_it(server) -> None:
    """Pins #294: deafness to tells is not one of the account's display settings (PPC_PRF_MASK,
    structs.h), so turning it on for one character leaves the account's other online character
    hearing tells."""
    implementor = server.login(IMPLEMENTOR.name)
    mortal = server.login(MORTAL.name)
    before = mortal.command("set notell")
    assert CAN_HEAR_TELLS in before.text, before.text

    turned_on = implementor.command("set notell on")
    assert NOTELL_TURNED_ON in turned_on.text, turned_on.text

    after = mortal.command("set notell")
    assert CAN_HEAR_TELLS in after.text and DEAF_TO_TELLS not in after.text, after.text
