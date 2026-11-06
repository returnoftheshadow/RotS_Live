"""One character online per account: an account of mortals plays one character at a time, until it
owns a character above level 91; a linkless character still holds the place, and the account can
take back a body it is already playing."""

from __future__ import annotations

import contextlib
import re
import time
from typing import Iterator

import pytest

from rots_harness import world
from rots_harness.characters import HARNESS_EMAIL, HARNESS_PASSWORD, Character
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
    LoginRefused,
)

pytestmark = [pytest.mark.scenario, pytest.mark.account_menu, pytest.mark.account_menu_sessions]

# The account's first character, a level-20 human warrior.
ALPHA = Character(name="Testalpha", race="human", sex="male", level=20, load_room=world.ROOM_ARENA_CENTRE, class_name="warrior")
# The account's second character, a level-15 human warrior.
BETA = Character(name="Testbeta", race="human", sex="female", level=15, load_room=world.ROOM_ARENA_WEST, class_name="warrior")
# The highest level that leaves the rule in force, and the lowest that lifts it
# (kAccountActiveSessionAlternateCharacterLevel, interpre.cpp).
HIGHEST_RESTRICTED_LEVEL = 91
LOWEST_UNRESTRICTED_LEVEL = 92
# A stored character of the account at each of those levels, which never logs in.
HOLDER_AT_HIGHEST_RESTRICTED_LEVEL = Character(
    name="Testholder", race="human", sex="male", level=HIGHEST_RESTRICTED_LEVEL, load_room=world.ROOM_IMMORTAL_START, class_name="warrior"
)
HOLDER_AT_LOWEST_UNRESTRICTED_LEVEL = Character(
    name="Testholder", race="human", sex="male", level=LOWEST_UNRESTRICTED_LEVEL, load_room=world.ROOM_IMMORTAL_START, class_name="warrior"
)

# The account menu's create-a-new-character choice (show_account_menu, interpre.cpp).
ACCOUNT_MENU_NEW_CHARACTER = "4"
# What the one-character rule prints; format with the playing character's name
# (active_account_session_restriction_message_for_session, interpre.cpp).
ALREADY_CONNECTED_FORMAT = "You are already connected as {}."
# The account menu's line for one active character; format with its name, level and state
# (append_active_account_session_status, interpre.cpp).
ACTIVE_CHARACTER_FORMAT = "Active character: {} (level {}, {})"
# The prompt for a new character's name (CON_ACCTMENU in nanny, interpre.cpp).
NEW_CHARACTER_NAME_PROMPT = "New character name:"
# What a mortal reads when it quits to the character menu (do_quit, act_othe.cpp).
MORTAL_QUIT_MARKER = "As you quit, all your posessions drop to the ground!"
# What selecting a linkless character prints (CON_SLCT in nanny, interpre.cpp).
RECONNECTING_MARKER = "Reconnecting."
# What selecting a character another connection plays prints to each connection (CON_SLCT).
USURPER_MARKER = "You take over your own body, already in use!"
USURPED_MARKER = "This body has been usurped!"
# The game.log line written when a playing character's connection closes (close_socket, comm.cpp).
CLOSING_LINK_LOG_FORMAT = "Closing link to: {} ["
# Seconds the server is given to notice a closed connection, or to close one.
CLOSE_SECONDS = 5.0


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


def sign_in(connection: GameSession) -> str:
    """Signs in with the harness account; returns everything read up to the account menu."""
    connection.send_line(HARNESS_EMAIL)
    connection.expect([LOGIN_PASSWORD_PROMPT])
    connection.send_line(HARNESS_PASSWORD)
    return connection.expect([ACCOUNT_MENU_PROMPT])


def closes_within(connection: GameSession, seconds: float) -> bool:
    """Whether the server closes the connection within seconds; text read meanwhile is discarded."""
    try:
        connection.drain(seconds)
    except ConnectionError:
        return True
    return False


def wait_for_log_line(server, marker: str, seconds: float) -> None:
    """Returns once game.log holds a line containing marker; fails the test after seconds."""
    deadline = time.monotonic() + seconds
    while marker not in server.handle.log_path.read_text(encoding="latin-1"):
        if time.monotonic() >= deadline:
            pytest.fail(f"game.log never logged {marker!r} within {seconds}s")
        time.sleep(0.1)


@pytest.mark.characters(ALPHA, BETA)
def test_a_second_character_of_an_all_mortal_account_is_refused_while_the_first_plays(server) -> None:
    """Pins the one-character rule (#285): while one character of an account whose characters are all
    level 91 or below plays, another of its characters cannot be selected."""
    server.login(ALPHA.name)
    with pytest.raises(LoginRefused, match=re.escape(ALREADY_CONNECTED_FORMAT.format(ALPHA.name))):
        server.login(BETA.name)


@pytest.mark.characters(ALPHA, BETA)
def test_creating_a_character_is_refused_while_another_of_the_account_plays(server) -> None:
    """Pins the one-character rule (#285): the account menu's new character choice gives the same
    refusal and shows the account menu again instead of the name prompt."""
    server.login(ALPHA.name)
    with account_connection(server, "newwhileplaying") as connection:
        sign_in(connection)
        connection.send_line(ACCOUNT_MENU_NEW_CHARACTER)
        refused = connection.expect([ACCOUNT_MENU_PROMPT, NEW_CHARACTER_NAME_PROMPT])
        assert ALREADY_CONNECTED_FORMAT.format(ALPHA.name) in refused and ACCOUNT_MENU_PROMPT in refused, refused
        assert NEW_CHARACTER_NAME_PROMPT not in refused, refused


@pytest.mark.characters(ALPHA, BETA)
def test_quitting_to_the_character_menu_frees_the_account_for_another_character(server) -> None:
    """Pins the one-character rule (#285): a character that quits to the character menu no longer
    holds the account, so another logs in; entering the game from that menu is then refused, and the
    account menu shown instead names the character now playing."""
    with playing_connection(server, ALPHA) as alpha:
        alpha.send_line("quit")
        quit_reply = alpha.expect([CHARACTER_MENU_PROMPT])
        assert MORTAL_QUIT_MARKER in quit_reply, quit_reply
        server.login(BETA.name)
        alpha.send_line(CHARACTER_MENU_ENTER)
        refused = alpha.expect([ACCOUNT_MENU_PROMPT, CHARACTER_MENU_PROMPT, ENTER_GAME_MARKER])
        assert ALREADY_CONNECTED_FORMAT.format(BETA.name) in refused and ACCOUNT_MENU_PROMPT in refused, refused
        assert ACTIVE_CHARACTER_FORMAT.format(BETA.name, BETA.level, "playing") in refused, refused


@pytest.mark.characters(ALPHA, BETA, HOLDER_AT_HIGHEST_RESTRICTED_LEVEL)
def test_an_offline_level_91_character_leaves_the_rule_in_force(server) -> None:
    """Pins the rule's threshold (#285): an account owning a level-91 character, which never logs
    in, still plays one character at a time."""
    server.login(ALPHA.name)
    with pytest.raises(LoginRefused, match=re.escape(ALREADY_CONNECTED_FORMAT.format(ALPHA.name))):
        server.login(BETA.name)


@pytest.mark.characters(ALPHA, BETA, HOLDER_AT_LOWEST_UNRESTRICTED_LEVEL)
def test_an_offline_level_92_character_lifts_the_rule(server) -> None:
    """Pins the rule's threshold (#285): an account owning a level-92 character, which never logs in,
    may play two of its other characters at once."""
    server.login(ALPHA.name)
    beta = server.login(BETA.name)
    assert beta.command("look").room_name() == "Arena West"


@pytest.mark.characters(ALPHA, BETA)
def test_a_linkless_character_still_holds_the_account_and_reconnects_when_selected(server) -> None:
    """Pins the one-character rule (#285): a character whose connection dropped stays in the game as
    linkless; it still keeps the account's other character out, the account menu shows it as
    linkless, and selecting it reconnects to it."""
    with playing_connection(server, ALPHA) as alpha:
        # Closing the connection without quitting leaves the character in the game, linkless.
        alpha.close()
    wait_for_log_line(server, CLOSING_LINK_LOG_FORMAT.format(ALPHA.name), CLOSE_SECONDS)
    with pytest.raises(LoginRefused, match=re.escape(ALREADY_CONNECTED_FORMAT.format(ALPHA.name))):
        server.login(BETA.name)
    with account_connection(server, "linkless") as connection:
        menu = sign_in(connection)
        assert ACTIVE_CHARACTER_FORMAT.format(ALPHA.name, ALPHA.level, "linkless") in menu, menu
        connection.send_line(ACCOUNT_MENU_PLAY)
        connection.expect([CHARACTER_CHOICE_PROMPT])
        connection.send_line(ALPHA.name)
        reconnected = connection.expect([RECONNECTING_MARKER, USURPER_MARKER, CHARACTER_CHOICE_PROMPT, CHARACTER_MENU_PROMPT])
        assert RECONNECTING_MARKER in reconnected, reconnected
        connection.quit()


@pytest.mark.characters(ALPHA)
def test_selecting_a_character_another_connection_plays_takes_the_body_over(server) -> None:
    """Pins #285: the account may select the character it is already playing; the new connection
    takes over the body and the old one is told and closed."""
    with playing_connection(server, ALPHA) as first_connection:
        with account_connection(server, "usurper") as connection:
            sign_in(connection)
            connection.send_line(ACCOUNT_MENU_PLAY)
            connection.expect([CHARACTER_CHOICE_PROMPT])
            connection.send_line(ALPHA.name)
            taken_over = connection.expect([USURPER_MARKER, RECONNECTING_MARKER, CHARACTER_CHOICE_PROMPT, CHARACTER_MENU_PROMPT])
            assert USURPER_MARKER in taken_over, taken_over
            told = first_connection.expect([USURPED_MARKER])
            assert USURPED_MARKER in told, told
            assert closes_within(first_connection, CLOSE_SECONDS), "the usurped connection stayed open"
            connection.quit()
