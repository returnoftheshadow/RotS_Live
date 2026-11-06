"""The account menu before a character is chosen: logging out, the reset menu after wrong
passwords and the reset itself, the failed-login notice, new character names, and the character
roster's selection, sort and filter keys."""

from __future__ import annotations

import contextlib
import json
import re
import time
from typing import Iterator

import pytest

from rots_harness import world
from rots_harness.characters import HARNESS_EMAIL, HARNESS_PASSWORD, IMPLEMENTOR, Character
from rots_harness.launcher import LOGIN_EMAIL_PROMPT
from rots_harness.session import (
    ACCOUNT_MENU_PLAY,
    ACCOUNT_MENU_PROMPT,
    CHARACTER_CHOICE_PROMPT,
    CHARACTER_MENU_ENTER,
    CHARACTER_MENU_PROMPT,
    ENTER_GAME_MARKER,
    LOGIN_PASSWORD_PROMPT,
    GameSession,
)

pytestmark = [pytest.mark.scenario, pytest.mark.account_menu]

# Room 1190, the Uruk-hai idle room (mortal_idle_room, consts.cpp).
ROOM_URUK_IDLE = 1190

# A password the harness account does not have.
WRONG_PASSWORD = "Wrongpass9"
# A reset code that can never match: the game's codes are six digits
# (generate_numeric_verification_code, account_management.cpp).
WRONG_RESET_CODE = "notacode"
# The password a completed reset sets; it passes is_valid_password (account_management_identity.cpp).
NEW_PASSWORD = "Newpass77x"
# A password with an upper-case letter, a lower-case letter and a number, but too short.
SHORT_PASSWORD = "Short1"
# A valid password that differs from NEW_PASSWORD, for the retype prompt.
MISMATCHED_PASSWORD = "Newpass88x"
# The account menu's log-out choice (show_account_menu, interpre.cpp).
ACCOUNT_MENU_LOG_OUT = "0"
# The account menu's create-a-new-character choice (show_account_menu, interpre.cpp).
ACCOUNT_MENU_NEW_CHARACTER = "4"
# The character menu's back-to-the-account-menu choice (show_character_menu_impl, interpre.cpp).
CHARACTER_MENU_BACK = "0"
# The reset menu's reset choice (CON_ACCTPWDFAIL in nanny, interpre.cpp).
RESET_MENU_RESET = "1"
# Wrong passwords before the reset menu, and wrong reset codes before the connection closes
# (nanny, interpre.cpp).
ATTEMPTS_ALLOWED = 5
# Seconds a connection is given to close after its last line: the game loop closes it in the
# CON_CLOSE sweep of a later pass (game_loop, comm.cpp).
CLOSE_SECONDS = 3.0
# Seconds start_password_reset waits before it sends another code
# (PASSWORD_RESET_RESEND_COOLDOWN_SECONDS, account_management_types.h).
RESET_RESEND_COOLDOWN_SECONDS = 60
# The longest the cooldown scenario may take between its two requests: the cooldown less a margin
# for the server clock's whole seconds.
RESET_REQUESTS_TIME_ALLOWED = RESET_RESEND_COOLDOWN_SECONDS - 5

# The harness account's record in the lib, as rotstool writes it (account_file_path_from_email,
# account_management.cpp).
ACCOUNT_FILE = f"accounts/F-J/{HARNESS_EMAIL}/account.json"

# What account log-out prints before the connection closes (CON_ACCTMENU in nanny, interpre.cpp).
GOODBYE_MARKER = "Goodbye."
# What every wrong account password prints (CON_ACCTPWD in nanny, interpre.cpp).
INVALID_CREDENTIALS_MARKER = "Invalid account credentials."
# The line every account menu opens with (show_account_menu, interpre.cpp).
ACCOUNT_MENU_HEADER = f"Account: {HARNESS_EMAIL}"
# What a mortal reads when it quits to the character menu (do_quit, act_othe.cpp).
MORTAL_QUIT_MARKER = "As you quit, all your posessions drop to the ground!"
# The reset menu shown instead of the password prompt after the last wrong password
# (show_account_reset_menu, interpre.cpp).
RESET_MENU_LINES = (
    "1) Reset your account password",
    "0) Disconnect",
    "This menu will close in 90 seconds.",
)
# What a reset request prints whether or not a code was sent (CON_ACCTPWDFAIL in nanny).
RESET_REQUESTED_LINES = (
    "If an account exists for that address, a reset code has been sent to it.",
    "The code is valid for 15 minutes.",
)
# The prompt for the emailed reset code (CON_ACCTPWDFAIL and CON_ACCTFORGOTCODE in nanny).
RESET_CODE_PROMPT = "Reset code:"
# What each wrong reset code but the last prints (CON_ACCTFORGOTCODE in nanny).
INVALID_RESET_CODE_MARKER = "That reset code is invalid."
# What the last wrong reset code prints before the connection closes (CON_ACCTFORGOTCODE).
TOO_MANY_RESET_CODES_LINES = ("Too many invalid reset codes.", "Please reconnect and try again.")
# The prompts after a correct reset code (CON_ACCTFORGOTCODE and CON_ACCTFORGOTNEW in nanny).
NEW_PASSWORD_PROMPT = "New account password:"
RETYPE_PASSWORD_PROMPT = "Retype the new password:"
# What is_valid_password says of a password under eight characters (account_management_identity.cpp).
SHORT_PASSWORD_REFUSAL = "Passwords must be at least 8 characters long."
# What a retyped password that differs prints (CON_ACCTFORGOTCNF in nanny).
PASSWORD_MISMATCH_REFUSAL = "Passwords don't match... start over."
# What a completed reset prints before the connection closes (CON_ACCTFORGOTCNF).
PASSWORD_UPDATED_LINES = ("Your password has been updated.", "Please log in again with your new password.")
# The game.log lines a reset request writes (mudlog_account_event, interpre.cpp): requested when
# start_password_reset succeeds, which includes a request inside the cooldown, send failed when
# the server could not mail the code.
RESET_REQUEST_LOG_LINES = (
    f"Account password reset requested for {HARNESS_EMAIL} [",
    f"Account password reset send failed for {HARNESS_EMAIL} [",
)
# The game.log line account log-out writes (mudlog_account_event, interpre.cpp).
ACCOUNT_LOGOUT_LOG_LINE = f"Account logout for {HARNESS_EMAIL} ["
# The notice a login shows after two or more failed attempts; format with the count
# (format_account_login_failure_notice, account_management_presentation.cpp).
FAILED_LOGIN_NOTICE_FORMAT = "{} FAILED LOGIN ATTEMPTS SINCE YOUR LAST SUCCESSFUL LOGIN."
# The notice after exactly one failed attempt, in the singular (format_account_login_failure_notice).
ONE_FAILED_LOGIN_NOTICE = "1 FAILED LOGIN ATTEMPT SINCE YOUR LAST SUCCESSFUL LOGIN."
# The plural word the singular notice must not use.
FAILED_LOGIN_PLURAL_WORD = "ATTEMPTS"
# The words every failed-login notice contains, whatever the count.
FAILED_LOGIN_NOTICE_WORDS = "SINCE YOUR LAST SUCCESSFUL LOGIN."

# The prompt for a new character's name (CON_ACCTMENU and CON_ACCTNEWCHAR in nanny, interpre.cpp).
NEW_CHARACTER_NAME_PROMPT = "New character name:"
# What a name another account (or this one) already links prints (CON_ACCTNEWCHAR in nanny).
NAME_ALREADY_LINKED_REFUSAL = "That character name is already linked to an account."
# What a name with anything but letters prints (CON_ACCTNEWCHAR, _parse_name in interpre.cpp).
INVALID_NAME_REFUSAL = "Invalid character name."
# The end of the question a free name gets; format the full question with the name (CON_ACCTNEWCHAR).
ROLEPLAY_QUESTION_END = "a suitable name for roleplay in Middle-earth (Y/N)?"
ROLEPLAY_QUESTION_FORMAT = "Is {} a suitable name for roleplay in Middle-earth (Y/N)?"
# A free name for a new character: letters only, unused in every test world.
FREE_NEW_NAME = "Thorin"
# A name _parse_name refuses for its digit.
MALFORMED_NEW_NAME = "Bad1"

# What the roster prints for a selection that names no linked character
# (select_linked_character, account_management_identity.cpp).
ROSTER_SELECTION_ERROR = "Select a linked character by number or name, or enter 0 to return to the account menu."
# One roster row, `%zu) [%3d %s] %-12.12s`, or `%zu) [ ?? ???] %-12.12s` for a character whose file
# cannot be read: row, level, race, name (format_account_character_short_entry, account_management.cpp).
ROSTER_ROW_PATTERN = re.compile(r"(\d+)\) \[ *(\d+|\?\?) (\w+|\?\?\?)\] (\w+)")
# The level and race columns of a row whose character file cannot be read.
UNREADABLE_ROW_COLUMNS = "[ ?? ???]"
# The side sort's section headers (side_section_label, account_management.cpp).
GODS_SECTION_HEADER = "-- Gods --"
GOOD_SECTION_HEADER = "-- Good --"
EVIL_SECTION_HEADER = "-- Evil --"
UNAVAILABLE_SECTION_HEADER = "-- Unavailable --"
# Side names the roster's headers must never use.
FORBIDDEN_SIDE_WORDS = ("Lights", "Darks")
# What a filter that matches no linked character prints instead of rows
# (format_account_character_short_roster, account_management.cpp).
NO_FILTER_MATCH_MARKER = "No linked characters match that filter."
# The footer of an unfiltered one-character roster (format_account_character_short_roster).
ONE_CHARACTER_FOOTER = "1 character displayed."
# What selecting a linked character with no file in account storage or players/ prints
# (CON_ACCTSLCT in nanny, interpre.cpp).
NOT_READY_REFUSAL = "That linked character is not ready for account-backed play yet."
# A character name `account addchar` links although no file of it exists anywhere; it would sort
# first by name if its file could be read.
GHOST_NAME = "Abelghost"
# What `account addchar` prints once the name is linked (do_account, act_wiz.cpp).
CHARACTER_LINKED_REPLY = "Character linked to account."

# The roster's characters, in the order the account links them: the marker's order. Each sort
# key and each filter key shows them in an order no other key gives. The mage, the ranger and the
# uruk each start in a room no other roster character does, so entering the game shows which of
# them was selected.
ROSTER_MAGE = Character(
    name="Tarwen", race="wood elf", sex="female", level=30, load_room=world.ROOM_ARENA_EAST, class_name="mage"
)
ROSTER_ORC = Character(
    name="Grishak", race="orc", sex="male", level=18, load_room=world.ROOM_ARENA_CENTRE, class_name="warrior"
)
ROSTER_RANGER = Character(
    name="Aswin", race="human", sex="male", level=25, load_room=world.ROOM_ARENA_WEST, class_name="ranger"
)
ROSTER_URUK = Character(
    name="Mordak", race="uruk-hai", sex="male", level=12, load_room=ROOM_URUK_IDLE, class_name="warrior"
)
ROSTER_MYSTIC = Character(
    name="Helwyn", race="human", sex="female", level=8, load_room=world.ROOM_ARENA_CENTRE, class_name="mystic"
)
ROSTER = (ROSTER_MAGE, ROSTER_ORC, ROSTER_RANGER, ROSTER_URUK, ROSTER_MYSTIC)
# The account's link order, which the roster shows until a sort key is pressed.
LINK_ORDER = [character.name for character in ROSTER]
# Level order, highest first (ordered_roster_indices, account_management.cpp).
LEVEL_ORDER = ["Tarwen", "Aswin", "Grishak", "Mordak", "Helwyn"]
# The two warriors in race order: uruk-hai (11) before orc (13), the reverse of their link order.
WARRIORS_IN_RACE_ORDER = ["Mordak", "Grishak"]

# A level-20 human warrior who quits to the character menu and logs out from there.
LEAVER = Character(
    name="Testleaver", race="human", sex="male", level=20, load_room=world.ROOM_ARENA_CENTRE, class_name="warrior"
)
# A level-20 human warrior linked beside the ghost row; the only mortal in Arena West.
SURVIVOR = Character(
    name="Testsurvivor", race="human", sex="female", level=20, load_room=world.ROOM_ARENA_WEST, class_name="warrior"
)
# A level-20 human warrior the implementor renames while it plays.
RENAMEE = Character(
    name="Testrenamee", race="human", sex="male", level=20, load_room=world.ROOM_ARENA_CENTRE, class_name="warrior"
)
# The new name the implementor gives RENAMEE.
RENAMED_TO = "Testnewname"
# What `wizset ... name` prints on success and on a taken name (do_wizset, act_wiz.cpp; rename_char, db.cpp).
RENAME_DONE_REPLY = "You changed their name successfully."
RENAME_TAKEN_REFUSAL = "That rename was refused -- nothing was changed: that name is already taken."
# The start of the character list line of `account show`
# (format_account_summary, account_management_presentation.cpp).
ACCOUNT_SHOW_CHARACTERS_PREFIX = "Characters ("
# The three files account storage keeps for each linked character (account_management.cpp).
CHARACTER_FILE_KINDS = ("character", "objects", "exploits")


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


@contextlib.contextmanager
def playing_connection(server, character: Character) -> Iterator[GameSession]:
    """A connection playing character, closed when the block ends; the server fixture does not quit it."""
    connection = GameSession(server.handle, character.name, HARNESS_EMAIL, HARNESS_PASSWORD, server.run_dir)
    try:
        connection.login()
        yield connection
    finally:
        connection.close()


def answer_password(connection: GameSession, password: str) -> str:
    """Sends password at the password prompt; returns the reply up to the next prompt."""
    connection.send_line(password)
    return connection.expect([LOGIN_PASSWORD_PROMPT, ACCOUNT_MENU_PROMPT])


def enter_email(connection: GameSession) -> None:
    """Sends the harness email at the email prompt and waits for the password prompt."""
    connection.send_line(HARNESS_EMAIL)
    connection.expect([LOGIN_PASSWORD_PROMPT])


def sign_in(connection: GameSession) -> str:
    """Signs in with the harness account; returns everything read up to the account menu."""
    enter_email(connection)
    reply = answer_password(connection, HARNESS_PASSWORD)
    assert ACCOUNT_MENU_PROMPT in reply, reply
    return reply


def open_roster(connection: GameSession) -> str:
    """Signs in, chooses the roster from the account menu and returns its first rendering."""
    sign_in(connection)
    connection.send_line(ACCOUNT_MENU_PLAY)
    return connection.expect([CHARACTER_CHOICE_PROMPT])


def roster_answer(connection: GameSession, text: str) -> str:
    """Sends text at the roster prompt and returns the reply up to the next prompt."""
    connection.send_line(text)
    return connection.expect([CHARACTER_CHOICE_PROMPT, CHARACTER_MENU_PROMPT, ACCOUNT_MENU_PROMPT])


def roster_names(roster_text: str) -> list:
    """The character names of a roster rendering, in the order shown."""
    return [match[3] for match in ROSTER_ROW_PATTERN.findall(roster_text)]


def roster_row_numbers(roster_text: str) -> list:
    """The row numbers of a roster rendering, in the order shown."""
    return [int(match[0]) for match in ROSTER_ROW_PATTERN.findall(roster_text)]


def assert_rows_numbered_from_one(roster_text: str) -> None:
    """Fails unless the rendering's rows are numbered 1, 2, ... in the order shown."""
    row_numbers = roster_row_numbers(roster_text)
    assert row_numbers == list(range(1, len(row_numbers) + 1)), roster_text


def enter_selected_character(connection: GameSession) -> str:
    """Enters the game from the character menu and returns the name of the room the character stands in."""
    connection.send_line(CHARACTER_MENU_ENTER)
    connection.expect([ENTER_GAME_MARKER])
    return connection.command("look").room_name()


def new_name_answer(connection: GameSession, name: str) -> str:
    """Sends name at the new character name prompt and returns the reply up to the next prompt."""
    connection.send_line(name)
    return connection.expect([NEW_CHARACTER_NAME_PROMPT, ROLEPLAY_QUESTION_END, ACCOUNT_MENU_PROMPT, LOGIN_EMAIL_PROMPT])


def start_new_character(connection: GameSession) -> None:
    """Signs in and chooses to create a new character, up to the name prompt."""
    sign_in(connection)
    connection.send_line(ACCOUNT_MENU_NEW_CHARACTER)
    connection.expect([NEW_CHARACTER_NAME_PROMPT])


def closes_within(connection: GameSession, seconds: float) -> bool:
    """Whether the server closes the connection within seconds; text read meanwhile is discarded."""
    try:
        connection.drain(seconds)
    except ConnectionError:
        return True
    return False


def exhaust_passwords(connection: GameSession) -> list:
    """Enters the harness email, then wrong passwords until the attempts run out; returns each reply."""
    enter_email(connection)
    return [answer_password(connection, WRONG_PASSWORD) for _attempt in range(ATTEMPTS_ALLOWED)]


def request_reset(connection: GameSession) -> str:
    """Runs out the password attempts and asks the reset menu for a code; returns the request's reply."""
    replies = exhaust_passwords(connection)
    assert RESET_MENU_LINES[0] in replies[-1], replies[-1]
    connection.send_line(RESET_MENU_RESET)
    requested = connection.expect([RESET_CODE_PROMPT])
    for requested_line in RESET_REQUESTED_LINES:
        assert requested_line in requested, requested
    return requested


def stored_account(server) -> dict:
    """The harness account's record as the server last wrote it."""
    return json.loads((server.lib_dir / ACCOUNT_FILE).read_text(encoding="utf-8"))


def game_log_lines(server, marker: str) -> list:
    """The lines of the server's game.log so far that contain marker."""
    log_text = server.handle.log_path.read_text(encoding="latin-1")
    return [line for line in log_text.splitlines() if marker in line]


def account_show_characters(implementor: GameSession) -> str:
    """The character list line `account show` prints for the harness account."""
    reply = implementor.command(f"account show {HARNESS_EMAIL}").text
    lines = [line.strip() for line in reply.splitlines() if line.strip().startswith(ACCOUNT_SHOW_CHARACTERS_PREFIX)]
    assert len(lines) == 1, reply
    return lines[0]


@pytest.mark.account_menu_sessions
@pytest.mark.characters(IMPLEMENTOR)
def test_logging_out_of_the_account_menu_says_goodbye_and_closes_the_connection(server) -> None:
    """Pins #279: the account menu's 0 prints Goodbye. and then the server closes the connection."""
    with account_connection(server, "logout") as connection:
        sign_in(connection)
        connection.send_line(ACCOUNT_MENU_LOG_OUT)
        farewell = connection.expect([GOODBYE_MARKER, ACCOUNT_MENU_PROMPT])
        assert GOODBYE_MARKER in farewell, farewell
        assert closes_within(connection, CLOSE_SECONDS), f"the connection stayed open after {GOODBYE_MARKER!r}"


@pytest.mark.account_menu_sessions
@pytest.mark.characters(LEAVER)
def test_a_character_quits_back_through_both_menus_and_logs_out(server) -> None:
    """Pins #279: quitting a character leaves the connection at the character menu, whose 0 returns
    to the account menu, whose 0 says Goodbye., logs the account out in game.log and closes the
    connection."""
    with playing_connection(server, LEAVER) as connection:
        connection.send_line("quit")
        quit_reply = connection.expect([CHARACTER_MENU_PROMPT, ACCOUNT_MENU_PROMPT])
        assert MORTAL_QUIT_MARKER in quit_reply and CHARACTER_MENU_PROMPT in quit_reply, quit_reply
        connection.send_line(CHARACTER_MENU_BACK)
        account_menu = connection.expect([ACCOUNT_MENU_PROMPT, CHARACTER_MENU_PROMPT])
        assert ACCOUNT_MENU_HEADER in account_menu and ACCOUNT_MENU_PROMPT in account_menu, account_menu
        assert game_log_lines(server, ACCOUNT_LOGOUT_LOG_LINE) == [], "logged out before the account menu's 0"
        connection.send_line(ACCOUNT_MENU_LOG_OUT)
        farewell = connection.expect([GOODBYE_MARKER, ACCOUNT_MENU_PROMPT])
        assert GOODBYE_MARKER in farewell, farewell
        assert closes_within(connection, CLOSE_SECONDS), f"the connection stayed open after {GOODBYE_MARKER!r}"
    assert len(game_log_lines(server, ACCOUNT_LOGOUT_LOG_LINE)) == 1, server.handle.log_path.read_text(encoding="latin-1")


@pytest.mark.account_menu_passwords
@pytest.mark.characters(IMPLEMENTOR)
def test_the_fifth_wrong_password_opens_the_reset_menu(server) -> None:
    """Pins #284: the first four wrong passwords prompt again; the fifth shows the reset menu instead."""
    with account_connection(server, "resetmenu") as connection:
        replies = exhaust_passwords(connection)
        for earlier_reply in replies[:-1]:
            assert INVALID_CREDENTIALS_MARKER in earlier_reply, earlier_reply
            assert LOGIN_PASSWORD_PROMPT in earlier_reply, earlier_reply
            assert RESET_MENU_LINES[0] not in earlier_reply, earlier_reply
        last_reply = replies[-1]
        assert INVALID_CREDENTIALS_MARKER in last_reply, last_reply
        assert LOGIN_PASSWORD_PROMPT not in last_reply, last_reply
        for menu_line in RESET_MENU_LINES:
            assert menu_line in last_reply, last_reply


@pytest.mark.account_menu_passwords
@pytest.mark.characters(IMPLEMENTOR)
def test_five_wrong_reset_codes_end_the_reset_and_close_the_connection(server) -> None:
    """Pins #284: after a reset request, four wrong codes prompt again and the fifth closes the connection.

    The request's reply is the same whether or not a code was sent; the harness's server has no
    mail program, so none is.
    """
    with account_connection(server, "resetcode") as connection:
        request_reset(connection)
        for _attempt in range(ATTEMPTS_ALLOWED - 1):
            connection.send_line(WRONG_RESET_CODE)
            reply = connection.expect([RESET_CODE_PROMPT, TOO_MANY_RESET_CODES_LINES[1]])
            assert INVALID_RESET_CODE_MARKER in reply and RESET_CODE_PROMPT in reply, reply
        connection.send_line(WRONG_RESET_CODE)
        last_reply = connection.expect([RESET_CODE_PROMPT, TOO_MANY_RESET_CODES_LINES[1]])
        for closing_line in TOO_MANY_RESET_CODES_LINES:
            assert closing_line in last_reply, last_reply
        assert RESET_CODE_PROMPT not in last_reply, last_reply
        assert closes_within(connection, CLOSE_SECONDS), "the connection stayed open after the last wrong code"


def spend_reset_code(server, label: str) -> None:
    """Requests a reset code on a new connection, then enters wrong codes until the connection closes."""
    with account_connection(server, label) as connection:
        request_reset(connection)
        for _attempt in range(ATTEMPTS_ALLOWED):
            connection.send_line(WRONG_RESET_CODE)
            connection.expect([RESET_CODE_PROMPT, TOO_MANY_RESET_CODES_LINES[1]])
        assert closes_within(connection, CLOSE_SECONDS), "the connection stayed open after the last wrong code"


@pytest.mark.account_menu_passwords
@pytest.mark.characters(IMPLEMENTOR)
def test_five_wrong_reset_codes_clear_the_stored_code_but_keep_when_it_was_sent(server) -> None:
    """Pins #284: the account record keeps the spent attempts and when the code was sent, clears the
    code and its expiry, and counts the five wrong passwords before it."""
    spend_reset_code(server, "spentcode")
    account = stored_account(server)
    spent_fields = {
        field_name: account[field_name]
        for field_name in ("password_reset_attempt_count", "password_reset_code_hash", "password_reset_code_expires_at", "failed_login_count")
    }
    assert spent_fields == {
        "password_reset_attempt_count": ATTEMPTS_ALLOWED,
        "password_reset_code_hash": "",
        "password_reset_code_expires_at": 0,
        "failed_login_count": ATTEMPTS_ALLOWED,
    }, account
    assert account["password_reset_code_sent_at"] > 0, account


@pytest.mark.account_menu_passwords
@pytest.mark.characters(IMPLEMENTOR)
def test_a_second_reset_request_within_a_minute_sends_no_new_code(server) -> None:
    """Pins #284: a reset requested again inside the 60-second cooldown reads exactly like the first,
    but leaves the stored send time alone and logs a request; the next login counts all ten wrong
    passwords.

    The first request logs `send failed` when the server has no mail program and `requested` when
    it has one; the cooldown's request logs `requested` either way.
    """
    first_request_started = time.monotonic()
    spend_reset_code(server, "cooldownfirst")
    first_sent_at = stored_account(server)["password_reset_code_sent_at"]
    with account_connection(server, "cooldownsecond") as connection:
        request_reset(connection)
        elapsed = time.monotonic() - first_request_started
        if elapsed >= RESET_REQUESTS_TIME_ALLOWED:
            pytest.fail(f"the two reset requests took {elapsed:.0f}s; they must fall inside the {RESET_RESEND_COOLDOWN_SECONDS}s cooldown")
    account = stored_account(server)
    assert account["password_reset_code_sent_at"] == first_sent_at, account
    assert account["failed_login_count"] == 2 * ATTEMPTS_ALLOWED, account
    request_lines = [line for line in server.handle.log_path.read_text(encoding="latin-1").splitlines() if any(marker in line for marker in RESET_REQUEST_LOG_LINES)]
    assert len(request_lines) == 2 and RESET_REQUEST_LOG_LINES[0] in request_lines[1], request_lines

    with account_connection(server, "cooldownlogin") as connection:
        login = sign_in(connection)
        assert FAILED_LOGIN_NOTICE_FORMAT.format(2 * ATTEMPTS_ALLOWED) in login, login


def open_reset_with_known_code(server, connection: GameSession) -> None:
    """Requests a reset code, then stores the account's own password hash as the code's hash.

    The server mails the code and keeps only its hash, both made by the same crypt call
    (generate_hash_for_secret, account_management.cpp), and re-reads the record for every code it
    checks; the account's password is then the code.
    """
    request_reset(connection)
    account_path = server.lib_dir / ACCOUNT_FILE
    record = account_path.read_text(encoding="utf-8")
    account = json.loads(record)
    assert account["password_reset_code_hash"] and account["password_reset_code_expires_at"] > 0, account
    stored_code = f'"password_reset_code_hash": "{account["password_reset_code_hash"]}"'
    assert record.count(stored_code) == 1, record
    account_path.write_text(record.replace(stored_code, f'"password_reset_code_hash": "{account["password_hash"]}"'), encoding="utf-8")


@pytest.mark.account_menu_passwords
@pytest.mark.characters(IMPLEMENTOR)
def test_a_correct_reset_code_sets_a_new_password_and_the_old_one_stops_working(server) -> None:
    """Pins #284: a correct reset code asks for a new password twice, says it was updated and closes
    the connection; afterwards the old password is refused and the new one signs in."""
    with account_connection(server, "resetdone") as connection:
        open_reset_with_known_code(server, connection)
        connection.send_line(HARNESS_PASSWORD)
        code_accepted = connection.expect([NEW_PASSWORD_PROMPT, RESET_CODE_PROMPT, TOO_MANY_RESET_CODES_LINES[1]])
        assert NEW_PASSWORD_PROMPT in code_accepted and INVALID_RESET_CODE_MARKER not in code_accepted, code_accepted
        connection.send_line(NEW_PASSWORD)
        connection.expect([RETYPE_PASSWORD_PROMPT])
        connection.send_line(NEW_PASSWORD)
        updated = connection.expect([PASSWORD_UPDATED_LINES[1], NEW_PASSWORD_PROMPT])
        for updated_line in PASSWORD_UPDATED_LINES:
            assert updated_line in updated, updated
        assert closes_within(connection, CLOSE_SECONDS), "the connection stayed open after the password was updated"
    assert stored_account(server)["password_reset_by"] == "forgot-password"

    with account_connection(server, "resetlogin") as connection:
        enter_email(connection)
        old_password = answer_password(connection, HARNESS_PASSWORD)
        assert INVALID_CREDENTIALS_MARKER in old_password and LOGIN_PASSWORD_PROMPT in old_password, old_password
        new_password = answer_password(connection, NEW_PASSWORD)
        assert ACCOUNT_MENU_HEADER in new_password and ACCOUNT_MENU_PROMPT in new_password, new_password


@pytest.mark.account_menu_passwords
@pytest.mark.characters(IMPLEMENTOR)
def test_a_reset_refuses_a_short_new_password_and_a_mismatched_retype(server) -> None:
    """Pins #284: after a correct reset code, a password under eight characters and a retype that
    differs each say why and ask for the new password again; nothing is changed."""
    with account_connection(server, "resetrefused") as connection:
        open_reset_with_known_code(server, connection)
        connection.send_line(HARNESS_PASSWORD)
        connection.expect([NEW_PASSWORD_PROMPT])
        connection.send_line(SHORT_PASSWORD)
        short = connection.expect([NEW_PASSWORD_PROMPT, RETYPE_PASSWORD_PROMPT])
        assert SHORT_PASSWORD_REFUSAL in short and RETYPE_PASSWORD_PROMPT not in short, short
        connection.send_line(NEW_PASSWORD)
        connection.expect([RETYPE_PASSWORD_PROMPT])
        connection.send_line(MISMATCHED_PASSWORD)
        mismatch = connection.expect([NEW_PASSWORD_PROMPT, PASSWORD_UPDATED_LINES[1]])
        assert PASSWORD_MISMATCH_REFUSAL in mismatch and NEW_PASSWORD_PROMPT in mismatch, mismatch
        assert PASSWORD_UPDATED_LINES[0] not in mismatch, mismatch
    assert stored_account(server)["password_reset_by"] == ""


@pytest.mark.account_menu_passwords
@pytest.mark.characters(IMPLEMENTOR)
def test_failed_logins_are_reported_at_the_next_successful_login(server) -> None:
    """Pins #284: a login after two wrong passwords opens with the count of failed attempts.

    The notice's next line names the address the attempts came from, which differs between
    launchers, so only the count line is asserted.
    """
    failed_attempts = 2
    with account_connection(server, "notice") as connection:
        enter_email(connection)
        for _attempt in range(failed_attempts):
            reply = answer_password(connection, WRONG_PASSWORD)
            assert INVALID_CREDENTIALS_MARKER in reply, reply
        login = answer_password(connection, HARNESS_PASSWORD)
        assert ACCOUNT_MENU_PROMPT in login, login
        assert FAILED_LOGIN_NOTICE_FORMAT.format(failed_attempts) in login, login


@pytest.mark.account_menu_passwords
@pytest.mark.characters(IMPLEMENTOR)
def test_one_failed_login_is_reported_in_the_singular(server) -> None:
    """Pins #284: after a single wrong password the notice says ATTEMPT, not ATTEMPTS."""
    with account_connection(server, "noticesingular") as connection:
        enter_email(connection)
        wrong = answer_password(connection, WRONG_PASSWORD)
        assert INVALID_CREDENTIALS_MARKER in wrong, wrong
        login = answer_password(connection, HARNESS_PASSWORD)
        assert ONE_FAILED_LOGIN_NOTICE in login, login
        assert FAILED_LOGIN_PLURAL_WORD not in login, login


@pytest.mark.account_menu_passwords
@pytest.mark.characters(IMPLEMENTOR)
def test_the_failed_login_notice_is_shown_only_once(server) -> None:
    """Pins #284: the login that shows the failed-login notice clears the count; the next shows none."""
    with account_connection(server, "noticefirst") as connection:
        enter_email(connection)
        wrong = answer_password(connection, WRONG_PASSWORD)
        assert INVALID_CREDENTIALS_MARKER in wrong, wrong
        first_login = answer_password(connection, HARNESS_PASSWORD)
        assert FAILED_LOGIN_NOTICE_WORDS in first_login, first_login
        connection.send_line(ACCOUNT_MENU_LOG_OUT)
        connection.expect([GOODBYE_MARKER])
    with account_connection(server, "noticesecond") as connection:
        second_login = sign_in(connection)
        assert FAILED_LOGIN_NOTICE_WORDS not in second_login, second_login


@pytest.mark.account_menu_characters
@pytest.mark.characters(IMPLEMENTOR)
def test_a_new_character_name_already_linked_or_not_all_letters_is_refused(server) -> None:
    """Pins the account menu's new character prompt: a linked name and a name with a digit are each
    refused with their reason, and the name prompt comes back."""
    with account_connection(server, "newnamerefused") as connection:
        start_new_character(connection)
        linked = new_name_answer(connection, IMPLEMENTOR.name)
        assert NAME_ALREADY_LINKED_REFUSAL in linked and NEW_CHARACTER_NAME_PROMPT in linked, linked
        malformed = new_name_answer(connection, MALFORMED_NEW_NAME)
        assert INVALID_NAME_REFUSAL in malformed and NEW_CHARACTER_NAME_PROMPT in malformed, malformed


@pytest.mark.account_menu_characters
@pytest.mark.characters(IMPLEMENTOR)
def test_a_free_new_character_name_is_offered_for_confirmation_and_no_returns_to_the_account_menu(server) -> None:
    """Pins the account menu's new character prompt: a free name gets the roleplay question, and
    answering no returns to the account menu."""
    with account_connection(server, "newnamefree") as connection:
        start_new_character(connection)
        question = new_name_answer(connection, FREE_NEW_NAME)
        assert ROLEPLAY_QUESTION_FORMAT.format(FREE_NEW_NAME) in question, question
        connection.send_line("n")
        declined = connection.expect([ACCOUNT_MENU_PROMPT, NEW_CHARACTER_NAME_PROMPT, LOGIN_EMAIL_PROMPT])
        assert ACCOUNT_MENU_HEADER in declined and ACCOUNT_MENU_PROMPT in declined, declined
        assert NEW_CHARACTER_NAME_PROMPT not in declined, declined


@pytest.mark.account_menu_characters
@pytest.mark.characters(IMPLEMENTOR, RENAMEE)
def test_renaming_a_playing_character_moves_its_account_files_and_frees_the_old_name(server) -> None:
    """Pins #297: `wizset player <name> name <new>` on an account-native character renames it in the
    account (account show, and its three files in the account folder) and the new character prompt
    then treats the old name as free and the new one as linked."""
    implementor = server.login(IMPLEMENTOR.name)
    server.login(RENAMEE.name)
    account_folder = (server.lib_dir / ACCOUNT_FILE).parent
    renamed = implementor.command(f"wizset player {RENAMEE.name.lower()} name {RENAMED_TO}")
    assert RENAME_DONE_REPLY in renamed.text, renamed.text
    assert account_show_characters(implementor) == f"Characters (2): {IMPLEMENTOR.name.lower()}, {RENAMED_TO.lower()}"
    character_files = sorted(entry.name for entry in account_folder.iterdir() if entry.name.startswith((f"{RENAMEE.name.lower()}.", f"{RENAMED_TO.lower()}.")))
    assert character_files == sorted(f"{RENAMED_TO.lower()}.{kind}.json" for kind in CHARACTER_FILE_KINDS), character_files

    with account_connection(server, "renamedmenu") as connection:
        start_new_character(connection)
        new_name = new_name_answer(connection, RENAMED_TO)
        assert NAME_ALREADY_LINKED_REFUSAL in new_name, new_name
        old_name = new_name_answer(connection, RENAMEE.name)
        assert ROLEPLAY_QUESTION_FORMAT.format(RENAMEE.name) in old_name, old_name


@pytest.mark.account_menu_characters
@pytest.mark.characters(IMPLEMENTOR, RENAMEE)
def test_renaming_a_character_to_a_taken_name_is_refused_and_changes_nothing(server) -> None:
    """Pins #297: `wizset ... name` to another character's name is refused with its reason, and the
    account and its files keep the old name."""
    implementor = server.login(IMPLEMENTOR.name)
    server.login(RENAMEE.name)
    account_folder = (server.lib_dir / ACCOUNT_FILE).parent
    refused = implementor.command(f"wizset player {RENAMEE.name.lower()} name {IMPLEMENTOR.name}")
    assert RENAME_TAKEN_REFUSAL in refused.text and RENAME_DONE_REPLY not in refused.text, refused.text
    assert account_show_characters(implementor) == f"Characters (2): {IMPLEMENTOR.name.lower()}, {RENAMEE.name.lower()}"
    for kind in CHARACTER_FILE_KINDS:
        assert (account_folder / f"{RENAMEE.name.lower()}.{kind}.json").is_file(), kind


@pytest.mark.account_menu_roster
@pytest.mark.characters(*ROSTER)
def test_the_roster_selects_a_character_by_its_full_name_in_any_case(server) -> None:
    """Pins #285: a linked character's full name selects it at the roster, whatever the letters' case."""
    with account_connection(server, "byname") as connection:
        open_roster(connection)
        reply = roster_answer(connection, "aSWIN")
        assert CHARACTER_MENU_PROMPT in reply and ROSTER_SELECTION_ERROR not in reply, reply
        assert enter_selected_character(connection) == "Arena West"
        connection.quit()


@pytest.mark.account_menu_roster
@pytest.mark.characters(*ROSTER)
def test_the_roster_rejects_the_start_of_a_name(server) -> None:
    """Pins #285: selecting by name needs the whole name; its first letters select nobody."""
    with account_connection(server, "prefix") as connection:
        open_roster(connection)
        reply = roster_answer(connection, "Asw")
        assert ROSTER_SELECTION_ERROR in reply and CHARACTER_CHOICE_PROMPT in reply, reply
        assert CHARACTER_MENU_PROMPT not in reply, reply


@pytest.mark.account_menu_roster
@pytest.mark.parametrize(
    ("sort_key", "expected_names"),
    [
        pytest.param("A", ["Aswin", "Grishak", "Helwyn", "Mordak", "Tarwen"], id="name"),
        pytest.param("a", ["Aswin", "Grishak", "Helwyn", "Mordak", "Tarwen"], id="name-lower-case"),
        pytest.param("L", LEVEL_ORDER, id="level"),
        pytest.param("l", LEVEL_ORDER, id="level-lower-case"),
        pytest.param("C", ["Aswin", "Helwyn", "Tarwen", "Mordak", "Grishak"], id="race"),
    ],
)
@pytest.mark.characters(*ROSTER)
def test_a_sort_key_reorders_the_roster(server, sort_key: str, expected_names: list) -> None:
    """Pins #291: the roster shows the account's link order until a sort key, in either case,
    reorders it; the rows stay numbered from 1 in the order shown.

    Race order follows the game's race numbers (human 1, wood elf 3, uruk-hai 11, orc 13), in
    link order between equals. The side key has its own test, with its headers.
    """
    with account_connection(server, f"sort{sort_key}") as connection:
        first_rendering = open_roster(connection)
        assert roster_names(first_rendering) == LINK_ORDER, first_rendering
        assert_rows_numbered_from_one(first_rendering)
        sorted_rendering = roster_answer(connection, sort_key)
        assert roster_names(sorted_rendering) == expected_names, sorted_rendering
        assert_rows_numbered_from_one(sorted_rendering)


@pytest.mark.account_menu_roster
@pytest.mark.characters(*ROSTER)
def test_the_side_sort_lists_good_then_evil_under_their_headers(server) -> None:
    """Pins #291 and #296: the side key lists Good, then Evil, A-Z within each, under -- Good -- and
    -- Evil -- headers that never say Lights or Darks; row numbers run on across the headers."""
    with account_connection(server, "sideheaders") as connection:
        open_roster(connection)
        rendering = roster_answer(connection, "S")
        assert GOOD_SECTION_HEADER in rendering and EVIL_SECTION_HEADER in rendering, rendering
        good_section, evil_section = rendering.split(EVIL_SECTION_HEADER)
        assert roster_names(good_section) == ["Aswin", "Helwyn", "Tarwen"], rendering
        assert roster_names(evil_section) == ["Grishak", "Mordak"], rendering
        assert_rows_numbered_from_one(rendering)
        for forbidden_word in FORBIDDEN_SIDE_WORDS:
            assert forbidden_word not in rendering, rendering


@pytest.mark.account_menu_roster
@pytest.mark.parametrize(
    ("filter_key", "label", "expected_names"),
    [
        pytest.param("W", "Warrior", ["Grishak", "Mordak"], id="warrior"),
        pytest.param("w", "Warrior", ["Grishak", "Mordak"], id="warrior-lower-case"),
        pytest.param("R", "Ranger", ["Aswin"], id="ranger"),
        pytest.param("T", "Mystic", ["Helwyn"], id="mystic"),
        pytest.param("M", "Mage", ["Tarwen"], id="mage"),
    ],
)
@pytest.mark.characters(*ROSTER)
def test_a_filter_key_shows_only_that_class_and_counts_them(
    server, filter_key: str, label: str, expected_names: list
) -> None:
    """Pins #291: a filter key, in either case, lists only the characters whose strongest profession
    it names, numbered from 1, and a footer counts them against the whole roster."""
    with account_connection(server, f"filter{filter_key}") as connection:
        open_roster(connection)
        filtered = roster_answer(connection, filter_key)
        assert roster_names(filtered) == expected_names, filtered
        assert_rows_numbered_from_one(filtered)
        footer = f"{len(expected_names)} of {len(ROSTER)} characters shown ({label}).  Press {filter_key.upper()} to clear."
        assert footer in filtered, filtered


@pytest.mark.account_menu_roster
@pytest.mark.characters(*ROSTER)
def test_a_filter_after_a_sort_lists_that_class_in_the_sorted_order(server) -> None:
    """Pins #291: a filter keeps the active sort; under the race sort the warriors show uruk before
    orc, renumbered from 1, with the filter's footer."""
    with account_connection(server, "sortthenfilter") as connection:
        open_roster(connection)
        roster_answer(connection, "C")
        filtered = roster_answer(connection, "W")
        assert roster_names(filtered) == WARRIORS_IN_RACE_ORDER, filtered
        assert_rows_numbered_from_one(filtered)
        assert f"2 of {len(ROSTER)} characters shown (Warrior).  Press W to clear." in filtered, filtered


@pytest.mark.account_menu_roster
@pytest.mark.characters(*ROSTER)
def test_a_number_under_a_filter_selects_the_row_shown_with_that_number(server) -> None:
    """Pins #291: with the warrior filter on, 2 selects the second warrior shown, not the account's
    second character."""
    with account_connection(server, "filterednumber") as connection:
        open_roster(connection)
        filtered = roster_answer(connection, "W")
        assert roster_names(filtered) == ["Grishak", "Mordak"], filtered
        selected = roster_answer(connection, "2")
        assert CHARACTER_MENU_PROMPT in selected, selected
        assert enter_selected_character(connection) == "Uruk Idle"
        connection.quit()


@pytest.mark.account_menu_roster
@pytest.mark.characters(*ROSTER)
def test_a_name_the_filter_hides_still_selects_its_character(server) -> None:
    """Pins #291: a filter narrows the list but not the names it accepts; the mage, hidden by the
    warrior filter, is selected by name."""
    with account_connection(server, "filteredname") as connection:
        open_roster(connection)
        filtered = roster_answer(connection, "W")
        assert ROSTER_MAGE.name not in roster_names(filtered), filtered
        selected = roster_answer(connection, ROSTER_MAGE.name)
        assert CHARACTER_MENU_PROMPT in selected and ROSTER_SELECTION_ERROR not in selected, selected
        assert enter_selected_character(connection) == "Arena East"
        connection.quit()


@pytest.mark.account_menu_roster
@pytest.mark.characters(IMPLEMENTOR)
def test_a_filter_that_matches_nobody_says_so(server) -> None:
    """Pins #291: on an account whose only character is a warrior, the ranger filter lists no row and
    says no linked character matches."""
    with account_connection(server, "filternobody") as connection:
        open_roster(connection)
        filtered = roster_answer(connection, "R")
        assert NO_FILTER_MATCH_MARKER in filtered, filtered
        assert roster_names(filtered) == [], filtered


@pytest.mark.account_menu_roster
@pytest.mark.characters(IMPLEMENTOR)
def test_a_one_character_roster_counts_one_character(server) -> None:
    """Pins #291: the footer of a one-character roster is in the singular."""
    with account_connection(server, "onecharacter") as connection:
        rendering = open_roster(connection)
        assert roster_names(rendering) == [IMPLEMENTOR.name], rendering
        assert ONE_CHARACTER_FOOTER in rendering and "1 characters" not in rendering, rendering


@pytest.mark.account_menu_roster
@pytest.mark.characters(*ROSTER)
def test_pressing_a_filter_key_again_clears_the_filter(server) -> None:
    """Pins #291: the active filter's own key clears it and brings every character back."""
    with account_connection(server, "clearfilter") as connection:
        open_roster(connection)
        filtered = roster_answer(connection, "W")
        assert roster_names(filtered) == ["Grishak", "Mordak"], filtered
        cleared = roster_answer(connection, "W")
        assert roster_names(cleared) == LINK_ORDER, cleared
        assert f"{len(ROSTER)} characters displayed." in cleared, cleared


@pytest.mark.account_menu_roster
@pytest.mark.characters(*ROSTER)
def test_the_roster_keeps_its_sort_in_the_next_session(server) -> None:
    """Pins #291: a sort chosen before leaving the roster is stored on the account and is the order
    the account's next session shows first."""
    with account_connection(server, "sortfirst") as connection:
        open_roster(connection)
        level_sorted = roster_answer(connection, "L")
        assert roster_names(level_sorted) == LEVEL_ORDER, level_sorted
        back_at_menu = roster_answer(connection, "0")
        assert ACCOUNT_MENU_PROMPT in back_at_menu, back_at_menu
        assert stored_account(server)["roster_sort"] == "level"
        connection.send_line(ACCOUNT_MENU_LOG_OUT)
        connection.expect([GOODBYE_MARKER])
    with account_connection(server, "sortsecond") as connection:
        rendering = open_roster(connection)
        assert roster_names(rendering) == LEVEL_ORDER, rendering


@pytest.mark.account_menu_roster
@pytest.mark.characters(*ROSTER)
def test_a_sort_chosen_before_selecting_a_character_is_kept_in_the_next_session(server) -> None:
    """Pins #291: selecting a character is the other way off the roster that stores a changed sort;
    it survives playing and quitting that character, and the next session shows it first."""
    with account_connection(server, "sortselect") as connection:
        open_roster(connection)
        level_sorted = roster_answer(connection, "L")
        assert roster_names(level_sorted) == LEVEL_ORDER, level_sorted
        selected = roster_answer(connection, "1")
        assert CHARACTER_MENU_PROMPT in selected, selected
        assert stored_account(server)["roster_sort"] == "level"
        assert enter_selected_character(connection) == "Arena East"
        connection.quit()
    assert stored_account(server)["roster_sort"] == "level"
    with account_connection(server, "sortselectnext") as connection:
        rendering = open_roster(connection)
        assert roster_names(rendering) == LEVEL_ORDER, rendering


def link_ghost(server) -> None:
    """Has the implementor link GHOST_NAME, which has no file anywhere, to the harness account."""
    implementor = server.login(IMPLEMENTOR.name)
    linked = implementor.command(f"account addchar {HARNESS_EMAIL} {GHOST_NAME}")
    assert CHARACTER_LINKED_REPLY in linked.text, linked.text


@pytest.mark.account_menu_roster
@pytest.mark.characters(IMPLEMENTOR, SURVIVOR)
def test_a_linked_character_without_a_file_is_listed_last_as_unreadable_and_never_under_a_filter(server) -> None:
    """Pins #291 and #296: a linked character whose file cannot be read shows `[ ?? ???]`, sorts last
    under the name and level sorts although its name would sort first, has its own Unavailable
    section under the side sort, and is left out by a filter."""
    link_ghost(server)
    ghost_row = f"3) {UNREADABLE_ROW_COLUMNS} {GHOST_NAME}"
    readable_names = [IMPLEMENTOR.name, SURVIVOR.name]
    with account_connection(server, "ghostrows") as connection:
        linked_order = open_roster(connection)
        assert roster_names(linked_order) == [*readable_names, GHOST_NAME] and ghost_row in linked_order, linked_order
        for sort_key in ("A", "L"):
            rendering = roster_answer(connection, sort_key)
            assert roster_names(rendering) == [*readable_names, GHOST_NAME] and ghost_row in rendering, rendering
        side_sorted = roster_answer(connection, "S")
        headers = [line.strip() for line in side_sorted.splitlines() if line.strip().startswith("-- ")]
        assert headers == [GODS_SECTION_HEADER, GOOD_SECTION_HEADER, UNAVAILABLE_SECTION_HEADER], side_sorted
        assert roster_names(side_sorted.split(UNAVAILABLE_SECTION_HEADER)[1]) == [GHOST_NAME], side_sorted
        filtered = roster_answer(connection, "W")
        assert roster_names(filtered) == readable_names and UNREADABLE_ROW_COLUMNS not in filtered, filtered
        assert "2 of 3 characters shown (Warrior).  Press W to clear." in filtered, filtered


@pytest.mark.account_menu_roster
@pytest.mark.characters(IMPLEMENTOR, SURVIVOR)
def test_selecting_a_linked_character_without_a_file_is_refused_and_the_roster_still_works(server) -> None:
    """Pins #285: selecting a linked character with no file says it is not ready and shows the roster
    again, from which a real character is then selected and played."""
    link_ghost(server)
    with account_connection(server, "ghostselect") as connection:
        open_roster(connection)
        refused = roster_answer(connection, GHOST_NAME)
        assert NOT_READY_REFUSAL in refused and CHARACTER_CHOICE_PROMPT in refused, refused
        assert CHARACTER_MENU_PROMPT not in refused, refused
        selected = roster_answer(connection, SURVIVOR.name)
        assert CHARACTER_MENU_PROMPT in selected, selected
        assert enter_selected_character(connection) == "Arena West"
        connection.quit()
