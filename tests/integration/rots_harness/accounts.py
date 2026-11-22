"""Connections the server fixture does not manage, and the account-menu text they read.

HarnessServer.login logs a character in and quits it after the test. A scenario about the account
menu itself needs a connection that stops at the email prompt, or one it closes without quitting;
these helpers open one for the length of a with block and close it at the end. The texts here
are the account-menu and `account` command texts two or more account scenario files share; the
login dialogue's own prompts are in session.py and launcher.py. Callers pass the server's handle
and run directory (server.handle, server.run_dir), as GameSession takes them.
"""

from __future__ import annotations

import contextlib
from collections.abc import Iterator
from pathlib import Path

from rots_harness.characters import HARNESS_EMAIL, HARNESS_PASSWORD, Character
from rots_harness.launcher import LOGIN_EMAIL_PROMPT, ServerHandle
from rots_harness.session import GameSession

# The account menu's create-a-new-character choice (show_account_menu, interpre.cpp).
ACCOUNT_MENU_NEW_CHARACTER = "4"
# The prompt for a new character's name (CON_ACCTMENU and CON_ACCTNEWCHAR in nanny, interpre.cpp).
NEW_CHARACTER_NAME_PROMPT = "New character name:"
# What a wrong account password prints, and a blocked account's correct one (CON_ACCTPWD in
# nanny, interpre.cpp; authenticate_account). The full stop makes it stricter than session.py's
# BAD_CREDENTIALS_START.
INVALID_CREDENTIALS_REPLY = "Invalid account credentials."
# What a password too short for the account layer prints, at the reset menu's new password and
# for `account passwd` (is_valid_password, account_management_identity.cpp).
SHORT_PASSWORD_REPLY = "Passwords must be at least 8 characters long."
# The start of the character list line of `account show` (format_account_summary,
# account_management_presentation.cpp).
ACCOUNT_SHOW_CHARACTERS_START = "Characters ("
# The level and race columns of a roster row whose character file cannot be read, between the
# row number and the name (format_account_character_short_entry, account_management.cpp).
UNREADABLE_ROW_PART = "[ ?? ???]"


@contextlib.contextmanager
def account_connection(handle: ServerHandle, transcript_dir: Path, label: str) -> Iterator[GameSession]:
    """A connection at the account email prompt, closed when the block ends.

    Waits for the email prompt before the block starts, failing with SessionTimeout when it does
    not come. label names its transcript in transcript_dir.
    """
    connection = GameSession(handle, label, HARNESS_EMAIL, HARNESS_PASSWORD, transcript_dir)
    try:
        connection.expect([LOGIN_EMAIL_PROMPT])
        yield connection
    finally:
        connection.close()


@contextlib.contextmanager
def playing_connection(handle: ServerHandle, transcript_dir: Path, character: Character) -> Iterator[GameSession]:
    """A connection logged in as character with GameSession.login, closed when the block ends.

    LoginRefused and transport errors from the login pass through. The server fixture does not
    quit it, so a block that ends without quitting leaves the character linkless.
    """
    connection = GameSession(handle, character.name, HARNESS_EMAIL, HARNESS_PASSWORD, transcript_dir)
    try:
        connection.login()
        yield connection
    finally:
        connection.close()


def closes_within(connection: GameSession, seconds: float) -> bool:
    """Whether the server closes the connection within seconds; text read meanwhile is discarded.

    Reads for the whole of seconds when the connection stays open. Nothing is asserted.
    """
    try:
        connection.drain(seconds)
    except ConnectionError:
        return True

    return False


def account_show_characters(implementor: GameSession) -> str:
    """Return the stripped character list line `account show` prints for the harness account.

    Sends the command through GameSession.command and asserts the reply holds exactly one line
    starting with ACCOUNT_SHOW_CHARACTERS_START. SessionTimeout passes through.
    """
    reply = implementor.command(f"account show {HARNESS_EMAIL}")
    reply_lines = reply.text.splitlines()
    lines = [line.strip() for line in reply_lines if line.strip().startswith(ACCOUNT_SHOW_CHARACTERS_START)]
    assert len(lines) == 1, reply.text
    return lines[0]
