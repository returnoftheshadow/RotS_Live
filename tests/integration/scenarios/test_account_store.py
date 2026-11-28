"""The account store from inside the game.

The tests cover deleting a linked character, the immortal `account` command's reports, refusals,
blocks and error log, and display settings shared by an account's characters.
"""

from __future__ import annotations

import re

import pytest

from rots_harness import world
from rots_harness.accounts import (
    INVALID_CREDENTIALS_REPLY,
    SHORT_PASSWORD_REPLY,
    UNREADABLE_ROW_PART,
    account_connection,
    account_show_characters,
)
from rots_harness.characters import HARNESS_EMAIL, HARNESS_PASSWORD, IMPLEMENTOR, Character
from rots_harness.game_log import log_lines_holding
from rots_harness.saved_files import CHARACTER_FILE_KINDS, HARNESS_ACCOUNT_FOLDER, stored_account
from rots_harness.session import (
    ACCOUNT_MENU_PLAY,
    ACCOUNT_MENU_PROMPT,
    CHARACTER_CHOICE_PROMPT,
    LOGIN_PASSWORD_PROMPT,
    GameSession,
)

pytestmark = [pytest.mark.scenario, pytest.mark.account_menu, pytest.mark.account_menu_store, pytest.mark.real_login]

# A level-12 human warrior linked to the harness account, which the implementor deletes; never
# online.
VICTIM = Character(
    name="Testdoomed", race="human", sex="male", level=12, load_room=world.ROOM_ARENA_CENTRE, class_name="warrior"
)
# A level-20 human warrior on the harness account, online beside the implementor.
MORTAL = Character(
    name="Testsibling", race="human", sex="female", level=20, load_room=world.ROOM_ARENA_CENTRE, class_name="warrior"
)

# The password do_delete requires after the character's name (act_wiz.cpp).
DELETE_PASSWORD = "rots"
# What do_delete prints for a wrong or missing password (act_wiz.cpp); on success it prints nothing.
DELETE_WRONG_PASSWORD_REPLY = "Incorrect or missing password"
# What do_delete prints for a player that does not exist (act_wiz.cpp).
DELETE_NO_SUCH_PLAYER_REPLY = "No such player"
# Every refusal do_delete prints (act_wiz.cpp).
DELETE_REFUSALS = (
    DELETE_WRONG_PASSWORD_REPLY,
    DELETE_NO_SUCH_PLAYER_REPLY,
    "That character could not be deleted; check the log.",
)
# A name no player in the test has.
UNKNOWN_PLAYER = "nosuch"
# The folder delete_player_character_by_index copies a linked character's file into (db.cpp).
ARCHIVE_FOLDER = "players/ZZZ"
# One readable roster row, `%zu) [%3d %s] %-12.12s`: row, level, race, name
# (account_management.cpp).
ROSTER_ROW_PATTERN = re.compile(r"(\d+)\) \[ *(\d+) (\w+)\] (\w+)")

# What `account index` reports when the boot indexed the one account cleanly (do_account,
# act_wiz.cpp).
CLEAN_INDEX_REPLY = "Account index: 1 record(s) indexed, 0 quarantined, 0 unreadable since boot."
# What `account errors` reports when nothing has been recorded since boot (do_account).
NO_ACCOUNT_ERRORS_REPLY = "No account errors recorded since boot."
# Each `account` usage mistake that refuses before touching an account, and its reply (do_account;
# the unknown address's reply is read_account_file_by_identifier's).
ACCOUNT_COMMAND_REFUSALS = (
    (
        "account",
        "Usage: account <show|verify|unverify|block|unblock|passwd|addchar|migratechar|unlockselect|index|errors> "
        "<email-or-account> [value]",
    ),
    ("account show nobody@example.com", "No account exists for that email address."),
    ("account index foo", "Usage: account index"),
    ("account errors 0", "Usage: account errors [rows]"),
    (f"account block {HARNESS_EMAIL}", "Usage: account block <email-or-account> <reason>"),
)
# A password `account passwd` refuses for its length (is_valid_password,
# account_management_identity.cpp).
SHORT_PASSWORD = "short"
# What `account addchar` prints for a character the account already links
# (add_character_to_account).
ALREADY_LINKED_REPLY = "Character is already linked to this account."
# The reply to `account block` (do_account).
ACCOUNT_BLOCKED_REPLY = "Account blocked."
# The reply to `account unblock` (do_account).
ACCOUNT_UNBLOCKED_REPLY = "Account unblocked."
# The reason given for the block.
BLOCK_REASON = "scenario"
# A character with no file in players/, so `account migratechar` fails and records the failure.
UNMIGRATABLE_NAME = "Nosuchchar"
# The `account errors` header after one recorded failure (do_account).
ONE_ACCOUNT_ERROR_HEADER = "Account errors since boot: 1 recorded, showing 1 (newest first)."
# The harness account's internal name, which the rows and log lines show (rotstool).
HARNESS_ACCOUNT_NAME = "harness"
# The source, account, character and actor of the failed migration, as a row and a log line show
# them.
MIGRATION_ERROR_SUBJECT = f"migration acct={HARNESS_ACCOUNT_NAME} char={UNMIGRATABLE_NAME} by={IMPLEMENTOR.name}:"
# MIGRATION_ERROR_SUBJECT as a regular expression.
MIGRATION_ERROR_SUBJECT_REGEX = re.escape(MIGRATION_ERROR_SUBJECT)
# One `account errors` row, `  [%s]%s %-9s acct=%s char=%s%s%s: %s`: time, repeat count, source,
# account, character, actor and reason (do_account).
ACCOUNT_ERROR_ROW_PATTERN = re.compile(rf"^  \[[^\]]+\]( x\d+)? {MIGRATION_ERROR_SUBJECT_REGEX} (.+)$", re.MULTILINE)
# The game.log line account_errors::record writes for the failed migration (account_errors.cpp).
ACCOUNT_ERROR_LOG_LINE = f"ACCTERR {MIGRATION_ERROR_SUBJECT}"

# What `set brief on` prints to the character who types it (tog_messages, act_othe.cpp).
BRIEF_TURNED_ON_REPLY = "Brief mode now on."
# What `set brief` without an argument prints while brief mode is on (flag_void, act_othe.cpp).
BRIEF_IS_ON_REPLY = "Brief mode on."
# What `set brief` without an argument prints while brief mode is off.
BRIEF_IS_OFF_REPLY = "Brief mode off."
# What `set notell on` prints (tog_messages, act_othe.cpp).
NOTELL_TURNED_ON_REPLY = "You are now deaf to tells."
# What `set notell` without an argument prints while the character hears tells.
CAN_HEAR_TELLS_REPLY = "You can hear tells."
# What `set notell` without an argument prints while the character is deaf to tells.
DEAF_TO_TELLS_REPLY = "You are deaf to tells."


def password_reply(connection: GameSession) -> str:
    """Send the harness email and its correct password; return the reply up to the next prompt."""
    connection.send_line(HARNESS_EMAIL)
    connection.expect([LOGIN_PASSWORD_PROMPT])
    connection.send_line(HARNESS_PASSWORD)
    return connection.expect([LOGIN_PASSWORD_PROMPT, ACCOUNT_MENU_PROMPT])


@pytest.mark.characters(IMPLEMENTOR, VICTIM)
def test_deleting_a_linked_character_unlinks_it_and_archives_its_file(server) -> None:
    """Pins that `delete` unlinks a linked character and archives its file in players/ZZZ.

    The character goes from its account (account show, the record's character links and the
    roster, which shows no unreadable row) and out of the account's folder.
    """
    implementor = server.login(IMPLEMENTOR.name)
    victim_key = VICTIM.name.lower()
    account_folder = server.lib_dir / HARNESS_ACCOUNT_FOLDER
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
    account = stored_account(server.lib_dir)
    character_links = account["character_links"]
    linked_names = [link["character_name"] for link in character_links]
    assert linked_names == [IMPLEMENTOR.name.lower()], linked_names
    left_in_account = sorted(entry.name for entry in account_folder.iterdir() if entry.name.startswith(victim_key))
    assert left_in_account == [], left_in_account
    archive_folder = server.lib_dir / ARCHIVE_FOLDER
    archived = sorted(entry.name for entry in archive_folder.iterdir())
    assert archived == [victim_key], archived
    with account_connection(server.handle, server.run_dir, "afterdelete") as connection:
        admitted = password_reply(connection)
        assert ACCOUNT_MENU_PROMPT in admitted, admitted
        connection.send_line(ACCOUNT_MENU_PLAY)
        roster = connection.expect([CHARACTER_CHOICE_PROMPT])
        rows = ROSTER_ROW_PATTERN.findall(roster)
        assert [match[3] for match in rows] == [IMPLEMENTOR.name], roster
        assert UNREADABLE_ROW_PART not in roster, roster
        assert "1 character displayed." in roster, roster


@pytest.mark.characters(IMPLEMENTOR, VICTIM)
def test_delete_refuses_an_unknown_player_and_a_wrong_password_and_changes_nothing(server) -> None:
    """Pins `delete`'s refusals of an unknown player and a wrong password, which change nothing.

    Each refusal is named, and the character stays linked with its files.
    """
    implementor = server.login(IMPLEMENTOR.name)
    victim_key = VICTIM.name.lower()
    unknown = implementor.command(f"delete {UNKNOWN_PLAYER} {DELETE_PASSWORD}")
    assert DELETE_NO_SUCH_PLAYER_REPLY in unknown.text, unknown.text
    wrong_password = implementor.command(f"delete {victim_key} {DELETE_PASSWORD}x")
    assert DELETE_WRONG_PASSWORD_REPLY in wrong_password.text, wrong_password.text
    shown = account_show_characters(implementor)
    assert shown == f"Characters (2): {IMPLEMENTOR.name.lower()}, {victim_key}", shown
    account_folder = server.lib_dir / HARNESS_ACCOUNT_FOLDER
    for kind in CHARACTER_FILE_KINDS:
        assert (account_folder / f"{victim_key}.{kind}.json").is_file(), kind


@pytest.mark.characters(IMPLEMENTOR)
def test_account_index_reports_one_clean_record_after_a_clean_boot(server) -> None:
    """Pins `account index` after a clean boot: one record, none quarantined or unreadable."""
    implementor = server.login(IMPLEMENTOR.name)
    reply = implementor.command("account index")
    assert CLEAN_INDEX_REPLY in reply.text, reply.text


@pytest.mark.characters(IMPLEMENTOR)
def test_account_errors_reports_none_after_a_clean_boot(server) -> None:
    """Pins `account errors` after a clean boot: it says none are recorded, not an empty list."""
    implementor = server.login(IMPLEMENTOR.name)
    reply = implementor.command("account errors")
    assert NO_ACCOUNT_ERRORS_REPLY in reply.text, reply.text


@pytest.mark.characters(IMPLEMENTOR)
def test_account_usage_mistakes_are_refused_with_their_usage_line(server) -> None:
    """Pins `account`'s usage refusals, each with its own line.

    The mistakes are no subcommand, an unknown address, a word after `index`, a row count of 0,
    and a block with no reason.
    """
    implementor = server.login(IMPLEMENTOR.name)
    for command, refusal in ACCOUNT_COMMAND_REFUSALS:
        reply = implementor.command(command)
        assert refusal in reply.text, f"{command!r}: {reply.text}"

    account = stored_account(server.lib_dir)
    assert account["blocked"] is False, account


@pytest.mark.characters(IMPLEMENTOR)
def test_account_passwd_refuses_a_short_password_and_keeps_the_old_one(server) -> None:
    """Pins `account passwd`'s refusal of a password under eight characters, keeping the old one."""
    implementor = server.login(IMPLEMENTOR.name)
    account_before = stored_account(server.lib_dir)
    password_hash = account_before["password_hash"]
    reply = implementor.command(f"account passwd {HARNESS_EMAIL} {SHORT_PASSWORD}")
    assert SHORT_PASSWORD_REPLY in reply.text, reply.text
    account_after = stored_account(server.lib_dir)
    assert account_after["password_hash"] == password_hash, account_after


@pytest.mark.characters(IMPLEMENTOR)
def test_account_addchar_refuses_a_character_the_account_already_links(server) -> None:
    """Pins `account addchar` of a linked character: it says so and links nothing twice."""
    implementor = server.login(IMPLEMENTOR.name)
    reply = implementor.command(f"account addchar {HARNESS_EMAIL} {IMPLEMENTOR.name}")
    assert ALREADY_LINKED_REPLY in reply.text, reply.text
    shown = account_show_characters(implementor)
    assert shown == f"Characters (1): {IMPLEMENTOR.name.lower()}", shown


@pytest.mark.characters(IMPLEMENTOR)
def test_a_blocked_account_cannot_sign_in_until_it_is_unblocked(server) -> None:
    """Pins `account block` and `account unblock`: a blocked account cannot sign in until unblocked.

    While blocked, the account's correct password reads as invalid credentials.
    """
    implementor = server.login(IMPLEMENTOR.name)
    blocked = implementor.command(f"account block {HARNESS_EMAIL} {BLOCK_REASON}")
    assert ACCOUNT_BLOCKED_REPLY in blocked.text, blocked.text
    with account_connection(server.handle, server.run_dir, "blockedlogin") as connection:
        refused = password_reply(connection)
        assert INVALID_CREDENTIALS_REPLY in refused and ACCOUNT_MENU_PROMPT not in refused, refused
    unblocked = implementor.command(f"account unblock {HARNESS_EMAIL}")
    assert ACCOUNT_UNBLOCKED_REPLY in unblocked.text, unblocked.text
    with account_connection(server.handle, server.run_dir, "unblockedlogin") as connection:
        admitted = password_reply(connection)
        assert ACCOUNT_MENU_PROMPT in admitted and INVALID_CREDENTIALS_REPLY not in admitted, admitted


@pytest.mark.characters(IMPLEMENTOR)
def test_a_failed_migration_is_listed_by_account_errors_and_a_repeat_is_counted_on_its_row(server) -> None:
    """Pins `account errors` for a failed migration: one row, and a repeat counted on it as x2.

    A failed `account migratechar` shows its reason to the immortal, and `account errors` lists it
    under a one-record header with the same reason. The same failure again is counted as x2 on
    that row while game.log keeps the one line of its first occurrence.
    """
    implementor = server.login(IMPLEMENTOR.name)
    migrate_command = f"account migratechar {HARNESS_EMAIL} {UNMIGRATABLE_NAME}"
    failed = implementor.command(migrate_command).text
    first_report = implementor.command("account errors").text
    assert ONE_ACCOUNT_ERROR_HEADER in first_report, first_report
    first_report_text = first_report.replace("\r", "")
    rows = ACCOUNT_ERROR_ROW_PATTERN.findall(first_report_text)
    assert len(rows) == 1 and rows[0][0] == "", first_report
    assert rows[0][1] in failed, (failed, rows)

    implementor.command(migrate_command)
    second_report = implementor.command("account errors").text
    assert ONE_ACCOUNT_ERROR_HEADER in second_report, second_report
    second_report_text = second_report.replace("\r", "")
    repeated_rows = ACCOUNT_ERROR_ROW_PATTERN.findall(second_report_text)
    assert [repeat for repeat, _reason in repeated_rows] == [" x2"], second_report
    log_lines = log_lines_holding(server.handle.log_path, ACCOUNT_ERROR_LOG_LINE)
    assert len(log_lines) == 1, log_lines


@pytest.mark.characters(IMPLEMENTOR, MORTAL)
def test_a_display_setting_changed_by_one_character_shows_on_the_accounts_other_online_character(server) -> None:
    """Pins that brief mode turned on by one character is on for the account's other character.

    The other character is online. Brief mode is one of the display settings the account holds
    (PPC_PRF_MASK, structs.h); the harness account starts with it off.
    """
    implementor = server.login(IMPLEMENTOR.name)
    mortal = server.login(MORTAL.name)
    before = mortal.command("set brief")
    assert BRIEF_IS_OFF_REPLY in before.text, before.text

    turned_on = implementor.command("set brief on")
    assert BRIEF_TURNED_ON_REPLY in turned_on.text, turned_on.text

    after = mortal.command("set brief")
    assert BRIEF_IS_ON_REPLY in after.text and BRIEF_IS_OFF_REPLY not in after.text, after.text


@pytest.mark.characters(IMPLEMENTOR, MORTAL)
def test_a_setting_outside_the_shared_display_settings_stays_with_the_character_that_changed_it(server) -> None:
    """Pins that deafness to tells stays with the character that turned it on.

    It is not one of the account's display settings (PPC_PRF_MASK, structs.h), so the account's
    other online character still hears tells.
    """
    implementor = server.login(IMPLEMENTOR.name)
    mortal = server.login(MORTAL.name)
    before = mortal.command("set notell")
    assert CAN_HEAR_TELLS_REPLY in before.text, before.text

    turned_on = implementor.command("set notell on")
    assert NOTELL_TURNED_ON_REPLY in turned_on.text, turned_on.text

    after = mortal.command("set notell")
    assert CAN_HEAR_TELLS_REPLY in after.text and DEAF_TO_TELLS_REPLY not in after.text, after.text
