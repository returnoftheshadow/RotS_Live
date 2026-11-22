"""Connections at the account menu that the server fixture does not manage.

HarnessServer.login logs a character in and quits it after the test. A scenario about the account
menu itself needs a connection that stops at the email prompt, or one it closes without quitting;
these helpers open one for the length of a with block and close it at the end, and hold the
account-menu text two or more account scenario files share. Callers pass the server's handle and
run directory (server.handle, server.run_dir), as GameSession takes them.
"""

from __future__ import annotations

import contextlib
from pathlib import Path
from typing import Iterator

from rots_harness.characters import HARNESS_EMAIL, HARNESS_PASSWORD, Character
from rots_harness.launcher import LOGIN_EMAIL_PROMPT, ServerHandle
from rots_harness.session import GameSession

# The account menu's create-a-new-character choice (show_account_menu, interpre.cpp).
ACCOUNT_MENU_NEW_CHARACTER = "4"
# The prompt for a new character's name (CON_ACCTMENU and CON_ACCTNEWCHAR in nanny, interpre.cpp).
NEW_CHARACTER_NAME_PROMPT = "New character name:"
# What a wrong account password prints, and a blocked account's correct one (CON_ACCTPWD in
# nanny, interpre.cpp; authenticate_account). The full stop makes it stricter than session.py's
# BAD_CREDENTIALS_MARKER.
INVALID_CREDENTIALS_MARKER = "Invalid account credentials."
# What a password too short for the account layer prints, at the reset menu's new password and
# for `account passwd` (is_valid_password, account_management_identity.cpp).
SHORT_PASSWORD_REFUSAL = "Passwords must be at least 8 characters long."


@contextlib.contextmanager
def account_connection(handle: ServerHandle, transcript_dir: Path, label: str) -> Iterator[GameSession]:
    """A connection at the account email prompt, closed when the block ends.

    Waits for the email prompt before the block starts, failing with SessionTimeout when it does
    not come. It plays no character, so the server fixture does not quit it; label names its
    transcript in transcript_dir.
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
