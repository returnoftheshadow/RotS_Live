"""Unit tests for rots_harness.accounts, against a fake server connection."""

from __future__ import annotations

from pathlib import Path

import pytest

from rots_harness import session
from rots_harness.accounts import (
    ACCOUNT_MENU_NEW_CHARACTER,
    ACCOUNT_SHOW_CHARACTERS_START,
    INVALID_CREDENTIALS_REPLY,
    NEW_CHARACTER_NAME_PROMPT,
    SHORT_PASSWORD_REPLY,
    UNREADABLE_ROW_PART,
    account_connection,
    account_show_characters,
    closes_within,
    playing_connection,
)
from rots_harness.characters import HARNESS_EMAIL, HARNESS_PASSWORD, Character
from rots_harness.launcher import ServerHandle
from unit.fakes import FakeSocket, RecordingSession

# The game's sources.
SOURCE_DIR = Path(__file__).resolve().parents[3] / "src"
# Each account text and the source file that prints it.
ACCOUNT_TEXT_SOURCES = [
    (NEW_CHARACTER_NAME_PROMPT, "interpre.cpp"),
    (INVALID_CREDENTIALS_REPLY, "interpre.cpp"),
    (SHORT_PASSWORD_REPLY, "account_management_identity.cpp"),
    (ACCOUNT_SHOW_CHARACTERS_START, "account_management_presentation.cpp"),
    (UNREADABLE_ROW_PART, "account_management.cpp"),
]
# A kept transcript's `account show harness@example.com`, abridged, ended as the game ends lines.
ACCOUNT_SHOW_REPLY = "\n\r".join(
    [
        "Account: harness@example.com",
        "  Characters (2): testimm, testalpha",
        "  Created: Sun Nov 22",
        "S:Surging> ",
    ]
)

# What the fake server sends on connect: the email prompt.
GREETING = b"Log in with your account email address at the prompt.\r\n\r\nAccount email: "
# What the fake server answers to each line of a login, in order.
LOGIN_REPLIES = {
    HARNESS_EMAIL: [b"Account password: "],
    HARNESS_PASSWORD: [b"\r\nAccount: harness@example.com\r\n2) Play a linked character\r\n\r\nChoice: "],
    "2": [b"\r\n1) [ 20 Hum] Testalpha\r\n\r\nCharacter number or name: "],
    "Testalpha": [b"\r\n0) Back to Account Menu.\r\n1) Enter the game.\r\n\r\n   Make your choice: "],
    "1": [b"\r\nHere we go...\r\n\r\nArena Centre    Exits are: N E S W\r\n\r\nS:Powered>"],
}
# The character the playing connection logs in.
ALPHA = Character(name="Testalpha", race="human", sex="male", level=20, load_room=1101, class_name="warrior")


def connect_to(monkeypatch: pytest.MonkeyPatch, fake_socket: FakeSocket) -> None:
    """Makes every new GameSession connect to fake_socket."""
    monkeypatch.setattr(session.socket, "create_connection", lambda _address, timeout: fake_socket)


def handle_in(tmp_path: Path) -> ServerHandle:
    """A handle for a server that is never contacted, with its log in tmp_path."""
    return ServerHandle("127.0.0.1", 1, tmp_path / "game.log", None)  # type: ignore[arg-type]


def test_account_connection_waits_for_the_email_prompt_and_closes_after_the_block(monkeypatch: pytest.MonkeyPatch, tmp_path: Path) -> None:
    fake_socket = FakeSocket({}, GREETING)
    connect_to(monkeypatch, fake_socket)
    handle = handle_in(tmp_path)
    with account_connection(handle, tmp_path, "menu") as connection:
        assert not fake_socket.closed
        assert fake_socket.sent_lines == []
        assert connection.character_name == "menu"
    assert fake_socket.closed
    assert (tmp_path / "menu.txt").read_text(encoding="utf-8").endswith("Account email: ")


def test_account_connection_closes_when_the_block_fails(monkeypatch: pytest.MonkeyPatch, tmp_path: Path) -> None:
    fake_socket = FakeSocket({}, GREETING)
    connect_to(monkeypatch, fake_socket)
    handle = handle_in(tmp_path)
    with pytest.raises(RuntimeError):
        with account_connection(handle, tmp_path, "menu"):
            raise RuntimeError("the block failed")
    assert fake_socket.closed


def test_playing_connection_logs_the_character_in_and_closes_without_quitting(monkeypatch: pytest.MonkeyPatch, tmp_path: Path) -> None:
    fake_socket = FakeSocket(LOGIN_REPLIES, GREETING)
    connect_to(monkeypatch, fake_socket)
    handle = handle_in(tmp_path)
    with playing_connection(handle, tmp_path, ALPHA):
        assert fake_socket.sent_lines == [HARNESS_EMAIL, HARNESS_PASSWORD, "2", "Testalpha", "1"]
    assert fake_socket.closed
    assert "quit" not in fake_socket.sent_lines


def test_closes_within_is_true_once_the_server_closes(monkeypatch: pytest.MonkeyPatch, tmp_path: Path) -> None:
    fake_socket = FakeSocket({}, GREETING, closed_by_server=True)
    connect_to(monkeypatch, fake_socket)
    handle = handle_in(tmp_path)
    with account_connection(handle, tmp_path, "menu") as connection:
        assert closes_within(connection, 1.0)


def test_closes_within_is_false_while_the_connection_stays_open(monkeypatch: pytest.MonkeyPatch, tmp_path: Path) -> None:
    fake_socket = FakeSocket({}, GREETING)
    connect_to(monkeypatch, fake_socket)
    handle = handle_in(tmp_path)
    with account_connection(handle, tmp_path, "menu") as connection:
        assert not closes_within(connection, 0.05)


def test_the_new_character_choice_is_the_menus_fourth() -> None:
    assert ACCOUNT_MENU_NEW_CHARACTER == "4"


@pytest.mark.parametrize("text, file_name", ACCOUNT_TEXT_SOURCES)
def test_each_account_text_is_printed_by_the_game(text: str, file_name: str) -> None:
    source_path = SOURCE_DIR / file_name
    source = source_path.read_text(encoding="latin-1")
    assert text in source, f"no {text!r} in {source_path}"


def test_account_show_characters_sends_account_show_and_returns_the_stripped_character_line() -> None:
    implementor = RecordingSession(command_replies={f"account show {HARNESS_EMAIL}": ACCOUNT_SHOW_REPLY})
    assert account_show_characters(implementor) == "Characters (2): testimm, testalpha"
    assert implementor.commands == [f"account show {HARNESS_EMAIL}"]


def test_account_show_characters_fails_without_exactly_one_character_line() -> None:
    implementor = RecordingSession(command_replies={f"account show {HARNESS_EMAIL}": "No such account.\n\r"})
    with pytest.raises(AssertionError):
        account_show_characters(implementor)
