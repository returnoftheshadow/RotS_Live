"""The account email prompt: a blank line ends the connection, and the server goes on serving."""

from __future__ import annotations

import time

import pytest

from rots_harness.characters import IMPLEMENTOR
from rots_harness.launcher import LOGIN_EMAIL_PROMPT
from rots_harness.session import GameSession

pytestmark = [pytest.mark.scenario, pytest.mark.account_login]

# Names the bare connection's transcript file; letters only, like a character's name.
BLANK_LINE_CONNECTION_NAME = "blankemail"
# Seconds the server has to close the connection after the blank line; it closes it on the next
# pass of the game loop.
CLOSE_TIMEOUT_SECONDS = 8.0


def server_closes_connection(session: GameSession, timeout: float) -> bool:
    """Reads until the server closes the connection; returns False when it is still open after timeout seconds."""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            session.drain(0.25)
        except ConnectionError:
            return True
    return False


@pytest.mark.characters(IMPLEMENTOR)
def test_a_blank_email_line_closes_the_connection_and_the_server_still_serves(server) -> None:
    connection = GameSession(server.handle, BLANK_LINE_CONNECTION_NAME, server.character_set.email, server.character_set.password, server.run_dir)
    try:
        connection.expect([LOGIN_EMAIL_PROMPT])
        prompt_count = connection.everything.count(LOGIN_EMAIL_PROMPT)
        connection.send_line("")
        assert server_closes_connection(connection, CLOSE_TIMEOUT_SECONDS), connection.everything
        assert connection.everything.count(LOGIN_EMAIL_PROMPT) == prompt_count, connection.everything
    finally:
        connection.close()

    implementor = server.login(IMPLEMENTOR.name)
    assert implementor.command("look").room_name() == "Immortal Start"
