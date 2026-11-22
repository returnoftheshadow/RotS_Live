"""Unit tests for rots_harness.accounts, against a fake server connection."""

from __future__ import annotations

import socket
from pathlib import Path

import pytest

from rots_harness import session
from rots_harness.accounts import (
    ACCOUNT_MENU_NEW_CHARACTER,
    INVALID_CREDENTIALS_MARKER,
    NEW_CHARACTER_NAME_PROMPT,
    SHORT_PASSWORD_REFUSAL,
    account_connection,
    closes_within,
    playing_connection,
)
from rots_harness.characters import HARNESS_EMAIL, HARNESS_PASSWORD, Character
from rots_harness.launcher import ServerHandle

# What the fake server sends on connect: the email prompt.
GREETING = b"Log in with your account email address at the prompt.\r\n\r\nAccount email: "
# What the fake server answers to each line of a login, in order.
LOGIN_REPLIES = {
    HARNESS_EMAIL: b"Account password: ",
    HARNESS_PASSWORD: b"\r\nAccount: harness@example.com\r\n2) Play a linked character\r\n\r\nChoice: ",
    "2": b"\r\n1) [ 20 Hum] Testalpha\r\n\r\nCharacter number or name: ",
    "Testalpha": b"\r\n0) Back to Account Menu.\r\n1) Enter the game.\r\n\r\n   Make your choice: ",
    "1": b"\r\nHere we go...\r\n\r\nArena Centre    Exits are: N E S W\r\n\r\nS:Powered>",
}
# The character the playing connection logs in.
ALPHA = Character(name="Testalpha", race="human", sex="male", level=20, load_room=1101, class_name="warrior")


class FakeSocket:
    """Stands in for the server connection: sends the greeting, answers lines from a table, and
    reports a closed connection once closed_by_server is set and nothing is left to read."""

    def __init__(self, replies: dict[str, bytes], *, closed_by_server: bool = False) -> None:
        """Answers each sent line from replies; closed_by_server makes recv report the close."""
        # What each sent line is answered with.
        self._replies = replies
        # Chunks the coming recv() calls return, one each.
        self._pending: list[bytes] = [GREETING]
        # Whether recv returns b"" once nothing is pending, as a socket the server closed does.
        self._closed_by_server = closed_by_server
        # Whether the session closed its side.
        self.closed = False
        # Every line the session sent, without the newline.
        self.sent_lines: list[str] = []

    def settimeout(self, _timeout: float) -> None:
        """Ignores the timeout: recv answers at once."""

    def sendall(self, data: bytes) -> None:
        """Records the line and queues its answer."""
        line = data.decode("latin-1").strip()
        self.sent_lines.append(line)
        if line in self._replies:
            self._pending.append(self._replies[line])

    def recv(self, _size: int) -> bytes:
        """The next pending chunk, b"" for a server-closed connection, or a timeout."""
        if self._pending:
            return self._pending.pop(0)

        if self._closed_by_server:
            return b""

        raise socket.timeout()

    def close(self) -> None:
        """Records that the session closed the connection."""
        self.closed = True


def connect_to(monkeypatch: pytest.MonkeyPatch, fake_socket: FakeSocket) -> None:
    """Makes every new GameSession connect to fake_socket."""
    monkeypatch.setattr(session.socket, "create_connection", lambda _address, timeout: fake_socket)


def handle_in(tmp_path: Path) -> ServerHandle:
    """A handle for a server that is never contacted, with its log in tmp_path."""
    return ServerHandle("127.0.0.1", 1, tmp_path / "game.log", None)  # type: ignore[arg-type]


def test_account_connection_waits_for_the_email_prompt_and_closes_after_the_block(monkeypatch: pytest.MonkeyPatch, tmp_path: Path) -> None:
    fake_socket = FakeSocket({})
    connect_to(monkeypatch, fake_socket)
    with account_connection(handle_in(tmp_path), tmp_path, "menu") as connection:
        assert not fake_socket.closed
        assert fake_socket.sent_lines == []
        assert connection.character_name == "menu"
    assert fake_socket.closed
    assert (tmp_path / "menu.txt").read_text(encoding="utf-8").endswith("Account email: ")


def test_account_connection_closes_when_the_block_fails(monkeypatch: pytest.MonkeyPatch, tmp_path: Path) -> None:
    fake_socket = FakeSocket({})
    connect_to(monkeypatch, fake_socket)
    with pytest.raises(RuntimeError):
        with account_connection(handle_in(tmp_path), tmp_path, "menu"):
            raise RuntimeError("the block failed")
    assert fake_socket.closed


def test_playing_connection_logs_the_character_in_and_closes_without_quitting(monkeypatch: pytest.MonkeyPatch, tmp_path: Path) -> None:
    fake_socket = FakeSocket(LOGIN_REPLIES)
    connect_to(monkeypatch, fake_socket)
    with playing_connection(handle_in(tmp_path), tmp_path, ALPHA):
        assert fake_socket.sent_lines == [HARNESS_EMAIL, HARNESS_PASSWORD, "2", "Testalpha", "1"]
    assert fake_socket.closed
    assert "quit" not in fake_socket.sent_lines


def test_closes_within_is_true_once_the_server_closes(monkeypatch: pytest.MonkeyPatch, tmp_path: Path) -> None:
    fake_socket = FakeSocket({}, closed_by_server=True)
    connect_to(monkeypatch, fake_socket)
    with account_connection(handle_in(tmp_path), tmp_path, "menu") as connection:
        assert closes_within(connection, 1.0)


def test_closes_within_is_false_while_the_connection_stays_open(monkeypatch: pytest.MonkeyPatch, tmp_path: Path) -> None:
    fake_socket = FakeSocket({})
    connect_to(monkeypatch, fake_socket)
    with account_connection(handle_in(tmp_path), tmp_path, "menu") as connection:
        assert not closes_within(connection, 0.05)


def test_the_account_menu_texts_are_the_games() -> None:
    assert ACCOUNT_MENU_NEW_CHARACTER == "4"
    assert NEW_CHARACTER_NAME_PROMPT == "New character name:"
    assert INVALID_CREDENTIALS_MARKER == "Invalid account credentials."
    assert SHORT_PASSWORD_REFUSAL == "Passwords must be at least 8 characters long."
