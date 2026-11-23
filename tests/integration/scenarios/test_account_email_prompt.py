"""The account email prompt: a blank line ends the connection, and the server goes on serving."""

from __future__ import annotations

import pytest

from rots_harness import world
from rots_harness.accounts import account_connection, closes_within
from rots_harness.characters import IMPLEMENTOR
from rots_harness.launcher import LOGIN_EMAIL_PROMPT

pytestmark = [pytest.mark.scenario, pytest.mark.account_login]

# Names the bare connection's transcript file; letters only, like a character's name.
BLANK_LINE_CONNECTION_NAME = "blankemail"
# Seconds the server has to close the connection after the blank line; it closes it on the next
# pass of the game loop.
CLOSE_TIMEOUT_SECONDS = 8.0


@pytest.mark.characters(IMPLEMENTOR)
def test_a_blank_email_line_closes_the_connection_and_the_server_still_serves(server) -> None:
    """Pins that a blank line at the email prompt closes the connection without asking again.

    The server then still logs a character in, who arrives in the immortal start room.
    """
    with account_connection(server.handle, server.run_dir, BLANK_LINE_CONNECTION_NAME) as connection:
        prompt_count = connection.everything.count(LOGIN_EMAIL_PROMPT)
        connection.send_line("")
        assert closes_within(connection, CLOSE_TIMEOUT_SECONDS), connection.everything
        assert connection.everything.count(LOGIN_EMAIL_PROMPT) == prompt_count, connection.everything

    implementor = server.login(IMPLEMENTOR.name)
    looked = implementor.command("look")
    assert looked.room_name() == world.ROOM_IMMORTAL_START_NAME, looked.text
