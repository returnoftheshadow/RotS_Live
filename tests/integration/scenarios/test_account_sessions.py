"""One character online per account, and the level that lifts the rule.

An account of mortals plays one character at a time, until it owns a character above level 91; a
linkless character still holds the place, and the account can take back a body it is already
playing.
"""

from __future__ import annotations

import re

import pytest

from rots_harness import world
from rots_harness.accounts import (
    ACCOUNT_MENU_NEW_CHARACTER,
    NEW_CHARACTER_NAME_PROMPT,
    account_connection,
    closes_within,
    playing_connection,
)
from rots_harness.characters import HARNESS_EMAIL, HARNESS_PASSWORD, Character
from rots_harness.game_log import wait_for_log_count
from rots_harness.game_text import QUIT_DROP_REPLY
from rots_harness.session import (
    ACCOUNT_MENU_PLAY,
    ACCOUNT_MENU_PROMPT,
    CHARACTER_CHOICE_PROMPT,
    CHARACTER_MENU_ENTER,
    CHARACTER_MENU_PROMPT,
    ENTER_GAME_REPLY,
    LOGIN_PASSWORD_PROMPT,
    ONE_CHARACTER_ONLINE_START,
    GameSession,
    LoginRefused,
)

pytestmark = [pytest.mark.scenario, pytest.mark.account_menu, pytest.mark.account_menu_sessions]

# The account's first character, a level-20 human warrior.
ALPHA = Character(
    name="Testalpha", race="human", sex="male", level=20, load_room=world.ROOM_ARENA_CENTRE, class_name="warrior"
)
# The account's second character, a level-15 human warrior.
BETA = Character(
    name="Testbeta", race="human", sex="female", level=15, load_room=world.ROOM_ARENA_WEST, class_name="warrior"
)
# The highest level that leaves the rule in force
# (kAccountActiveSessionAlternateCharacterLevel, interpre.cpp).
HIGHEST_RESTRICTED_LEVEL = 91
# The lowest level that lifts the rule.
LOWEST_UNRESTRICTED_LEVEL = 92
# A stored character of the account at HIGHEST_RESTRICTED_LEVEL, which never logs in.
HOLDER_AT_HIGHEST_RESTRICTED_LEVEL = Character(
    name="Testholder",
    race="human",
    sex="male",
    level=HIGHEST_RESTRICTED_LEVEL,
    load_room=world.ROOM_IMMORTAL_START,
    class_name="warrior",
)
# A stored character of the account at LOWEST_UNRESTRICTED_LEVEL, which never logs in.
HOLDER_AT_LOWEST_UNRESTRICTED_LEVEL = Character(
    name="Testholder",
    race="human",
    sex="male",
    level=LOWEST_UNRESTRICTED_LEVEL,
    load_room=world.ROOM_IMMORTAL_START,
    class_name="warrior",
)

# What the one-character rule prints; format with the playing character's name
# (active_account_session_restriction_message_for_session, interpre.cpp).
ALREADY_CONNECTED_FORMAT = ONE_CHARACTER_ONLINE_START + " {}."
# The account menu's line for one active character; format with its name, level and state
# (append_active_account_session_status, interpre.cpp).
ACTIVE_CHARACTER_FORMAT = "Active character: {} (level {}, {})"
# What selecting a linkless character prints (CON_SLCT in nanny, interpre.cpp).
RECONNECTING_REPLY = "Reconnecting."
# What selecting a character another connection plays prints to the new connection (CON_SLCT).
USURPER_REPLY = "You take over your own body, already in use!"
# What the old connection reads then.
BODY_USURPED = "This body has been usurped!"
# The game.log line written when a playing character's connection closes (close_socket, comm.cpp).
CLOSING_LINK_LOG_FORMAT = "Closing link to: {} ["
# Seconds the server is given to notice a closed connection, or to close one.
CLOSE_TIMEOUT_SECONDS = 5.0
# Seconds between two reads of game.log while waiting for the closing line.
CLOSING_LINE_POLL_SECONDS = 0.1


def sign_in(connection: GameSession) -> str:
    """Sign in with the harness account and return everything read up to the account menu."""
    connection.send_line(HARNESS_EMAIL)
    connection.expect([LOGIN_PASSWORD_PROMPT])
    connection.send_line(HARNESS_PASSWORD)
    return connection.expect([ACCOUNT_MENU_PROMPT])


@pytest.mark.characters(ALPHA, BETA)
def test_a_second_character_of_an_all_mortal_account_is_refused_while_the_first_plays(server) -> None:
    """Pins the one-character rule: a second character is refused while the first plays.

    While one character of an account whose characters are all level 91 or below plays, another of
    its characters cannot be selected.
    """
    server.login(ALPHA.name)
    refusal = ALREADY_CONNECTED_FORMAT.format(ALPHA.name)
    refusal_pattern = re.escape(refusal)
    with pytest.raises(LoginRefused, match=refusal_pattern):
        server.login(BETA.name)


@pytest.mark.characters(ALPHA, BETA)
def test_creating_a_character_is_refused_while_another_of_the_account_plays(server) -> None:
    """Pins the one-character rule on the account menu's new character choice.

    The choice gives the same refusal and shows the account menu again instead of the name prompt.
    """
    server.login(ALPHA.name)
    with account_connection(server.handle, server.run_dir, "newwhileplaying") as connection:
        sign_in(connection)
        connection.send_line(ACCOUNT_MENU_NEW_CHARACTER)
        refused = connection.expect([ACCOUNT_MENU_PROMPT, NEW_CHARACTER_NAME_PROMPT])
        assert ALREADY_CONNECTED_FORMAT.format(ALPHA.name) in refused and ACCOUNT_MENU_PROMPT in refused, refused
        assert NEW_CHARACTER_NAME_PROMPT not in refused, refused


@pytest.mark.characters(ALPHA, BETA)
def test_quitting_to_the_character_menu_frees_the_account_for_another_character(server) -> None:
    """Pins the one-character rule for a character that quits to the character menu.

    It no longer holds the account, so another logs in; entering the game from that menu is then
    refused, and the account menu shown instead names the character now playing.
    """
    with playing_connection(server.handle, server.run_dir, ALPHA) as alpha:
        alpha.send_line("quit")
        quit_reply = alpha.expect([CHARACTER_MENU_PROMPT])
        assert QUIT_DROP_REPLY in quit_reply, quit_reply
        server.login(BETA.name)
        alpha.send_line(CHARACTER_MENU_ENTER)
        refused = alpha.expect([ACCOUNT_MENU_PROMPT, CHARACTER_MENU_PROMPT, ENTER_GAME_REPLY])
        assert ALREADY_CONNECTED_FORMAT.format(BETA.name) in refused and ACCOUNT_MENU_PROMPT in refused, refused
        assert ACTIVE_CHARACTER_FORMAT.format(BETA.name, BETA.level, "playing") in refused, refused


@pytest.mark.characters(ALPHA, BETA, HOLDER_AT_HIGHEST_RESTRICTED_LEVEL)
def test_an_offline_level_91_character_leaves_the_rule_in_force(server) -> None:
    """Pins the rule's threshold: an offline level-91 character leaves the rule in force.

    An account owning a level-91 character, which never logs in, still plays one character at a
    time.
    """
    server.login(ALPHA.name)
    refusal = ALREADY_CONNECTED_FORMAT.format(ALPHA.name)
    refusal_pattern = re.escape(refusal)
    with pytest.raises(LoginRefused, match=refusal_pattern):
        server.login(BETA.name)


@pytest.mark.characters(ALPHA, BETA, HOLDER_AT_LOWEST_UNRESTRICTED_LEVEL)
def test_an_offline_level_92_character_lifts_the_rule(server) -> None:
    """Pins the rule's threshold: an offline level-92 character lifts the rule.

    An account owning a level-92 character, which never logs in, may play two of its other
    characters at once.
    """
    server.login(ALPHA.name)
    beta = server.login(BETA.name)
    looked = beta.command("look")
    assert looked.room_name() == world.ROOM_ARENA_WEST_NAME, looked.text


@pytest.mark.characters(ALPHA, BETA)
def test_a_linkless_character_still_holds_the_account_and_reconnects_when_selected(server) -> None:
    """Pins the one-character rule for a linkless character.

    A character whose connection dropped stays in the game as linkless; it still keeps the
    account's other character out, the account menu shows it as linkless, and selecting it
    reconnects to it.
    """
    with playing_connection(server.handle, server.run_dir, ALPHA) as alpha:
        # Closing the connection without quitting leaves the character in the game, linkless.
        alpha.close()

    closing_line = CLOSING_LINK_LOG_FORMAT.format(ALPHA.name)
    wait_for_log_count(
        server.handle.log_path, closing_line, 1, timeout=CLOSE_TIMEOUT_SECONDS, poll=CLOSING_LINE_POLL_SECONDS
    )
    refusal = ALREADY_CONNECTED_FORMAT.format(ALPHA.name)
    refusal_pattern = re.escape(refusal)
    with pytest.raises(LoginRefused, match=refusal_pattern):
        server.login(BETA.name)

    with account_connection(server.handle, server.run_dir, "linkless") as connection:
        menu = sign_in(connection)
        assert ACTIVE_CHARACTER_FORMAT.format(ALPHA.name, ALPHA.level, "linkless") in menu, menu
        connection.send_line(ACCOUNT_MENU_PLAY)
        connection.expect([CHARACTER_CHOICE_PROMPT])
        connection.send_line(ALPHA.name)
        reconnected = connection.expect(
            [RECONNECTING_REPLY, USURPER_REPLY, CHARACTER_CHOICE_PROMPT, CHARACTER_MENU_PROMPT]
        )
        assert RECONNECTING_REPLY in reconnected, reconnected
        connection.quit()


@pytest.mark.characters(ALPHA)
def test_selecting_a_character_another_connection_plays_takes_the_body_over(server) -> None:
    """Pins that the account may select the character it is already playing.

    The new connection takes over the body and the old one is told and closed.
    """
    with playing_connection(server.handle, server.run_dir, ALPHA) as first_connection:
        with account_connection(server.handle, server.run_dir, "usurper") as connection:
            sign_in(connection)
            connection.send_line(ACCOUNT_MENU_PLAY)
            connection.expect([CHARACTER_CHOICE_PROMPT])
            connection.send_line(ALPHA.name)
            taken_over = connection.expect(
                [USURPER_REPLY, RECONNECTING_REPLY, CHARACTER_CHOICE_PROMPT, CHARACTER_MENU_PROMPT]
            )
            assert USURPER_REPLY in taken_over, taken_over
            told = first_connection.expect([BODY_USURPED])
            assert BODY_USURPED in told, told
            assert closes_within(first_connection, CLOSE_TIMEOUT_SECONDS), "the usurped connection stayed open"
            connection.quit()
