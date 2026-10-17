from __future__ import annotations

import socket
from pathlib import Path
from typing import Optional

import pytest

from rots_harness import session
from rots_harness.launcher import ServerHandle
from rots_harness.session import GameSession, QuitRefused, Transcript, end_sessions, ends_with_prompt


def test_transcript_room_name_is_the_first_non_empty_line() -> None:
    assert Transcript("\nWood-elf Start\n   A glade where wood elves begin.\n").room_name() == "Wood-elf Start"


def test_transcript_room_name_drops_the_exits_suffix() -> None:
    transcript = Transcript("\nArena Centre    Exits are: N E S W\nThe centre...\n")
    assert transcript.room_name() == "Arena Centre"


def test_csi_pattern_strips_colour_codes_and_the_clear_screen() -> None:
    assert session.CSI_PATTERN.sub("", "\x1b[33mArena Centre\x1b[0m") == "Arena Centre"
    assert session.CSI_PATTERN.sub("", "\x1b[H\x1b[JArena Centre") == "Arena Centre"


def test_prompt_detection_accepts_trailing_whitespace_and_both_terminators() -> None:
    assert ends_with_prompt("You breathe out fire.\nHP:Healthy > ")
    assert ends_with_prompt("some output\n]")
    assert not ends_with_prompt("You start to cast a spell...")


class FakeSocket:
    """Stands in for the server connection. `replies` maps a line the session sends to the answers
    for its first, second, ... send; a line with no answer left is never answered. `greeting` is
    what the server sends on connect."""

    def __init__(self, replies: dict[str, list[bytes]], greeting: bytes = b"") -> None:
        # Answers still to give per sent line, in send order; copied so the caller's lists stay intact.
        self._replies = {line: list(answers) for line, answers in replies.items()}
        # Bytes the next recv() returns; empty means recv() times out.
        self._pending = greeting
        # Whether the session has closed the connection.
        self.closed = False
        # Every line the session sent, in order and without the newline.
        self.sent_lines: list[str] = []

    def settimeout(self, _timeout: float) -> None:
        pass

    def sendall(self, data: bytes) -> None:
        line = data.decode("latin-1").strip()
        self.sent_lines.append(line)
        answers = self._replies.get(line, [])
        if answers:
            self._pending += answers.pop(0)

    def recv(self, _size: int) -> bytes:
        if not self._pending:
            raise socket.timeout()
        chunk, self._pending = self._pending, b""
        return chunk

    def close(self) -> None:
        self.closed = True


GREETING = b"Log in with your account email address at the prompt.\r\n\r\nAccount email: "
CHARACTER_LIST = b"\r\n1) [100 Imm] Testimm     2) [ 30 Hum] Testmage\r\n\r\nCharacter number or name: "
CHARACTER_MENU = b"\r\nWelcome to Arda!\r\n0) Back to Account Menu.\r\n1) Enter the game.\r\n\r\n   Make your choice: "
ARENA = b"\r\nArena Centre    Exits are: N E S W\r\nThe centre of the harness arena.\r\n\r\nS:Powered>"


def login_replies() -> dict[str, list[bytes]]:
    """A server that lets Testmage in when the session picks it by name from the character list."""
    return {
        "harness@example.com": [b"Account password: "],
        "Harness1x": [b"\r\nAccount: harness@example.com\r\n2) Play a linked character\r\n0) Log out\r\n\r\nChoice: "],
        "2": [CHARACTER_LIST],
        "Testmage": [CHARACTER_MENU],
        "1": [b"\r\nThere is no unread news.\r\n\r\nHere we go...\r\n" + ARENA],
        "look": [ARENA],
    }


def connect_fake_session(monkeypatch: pytest.MonkeyPatch, tmp_path: Path, replies: dict[str, list[bytes]], greeting: bytes = b"") -> tuple[GameSession, FakeSocket]:
    """A session for Testmage connected to a FakeSocket that answers with `replies` after sending `greeting`."""
    fake_socket = FakeSocket(replies, greeting)
    monkeypatch.setattr(session.socket, "create_connection", lambda _address, timeout: fake_socket)
    handle = ServerHandle("127.0.0.1", 1, tmp_path / "game.log", None)  # type: ignore[arg-type]
    return GameSession(handle, "Testmage", "harness@example.com", "Harness1x", tmp_path), fake_socket


def test_login_walks_the_account_menu_and_enters_the_game(monkeypatch: pytest.MonkeyPatch, tmp_path: Path) -> None:
    game_session, fake_socket = connect_fake_session(monkeypatch, tmp_path, login_replies(), GREETING)
    game_session.login(timeout=1.0)
    assert "Arena Centre" in game_session.everything
    assert fake_socket.sent_lines[:4] == ["harness@example.com", "Harness1x", "2", "Testmage"], "the character is picked by name"
    assert (tmp_path / "testmage.txt").read_text(encoding="utf-8").rstrip().endswith("S:Powered>")


def test_bad_credentials_fail_the_login_at_once(monkeypatch: pytest.MonkeyPatch, tmp_path: Path) -> None:
    replies = login_replies()
    replies["Harness1x"] = [b"Invalid account credentials.\r\nAccount password: "]
    game_session, _fake_socket = connect_fake_session(monkeypatch, tmp_path, replies, GREETING)
    with pytest.raises(session.LoginRefused, match="Invalid account credentials"):
        game_session.login(timeout=1.0)


def test_the_one_character_online_rule_fails_the_login_with_a_pointer(monkeypatch: pytest.MonkeyPatch, tmp_path: Path) -> None:
    replies = login_replies()
    replies["Testmage"][0] = b"\r\nYou are already connected as Testimm. Reconnect to that character or log out before playing another character.\r\n"
    game_session, _fake_socket = connect_fake_session(monkeypatch, tmp_path, replies, GREETING)
    with pytest.raises(session.LoginRefused, match=r"(?s)already connected as Testimm.*above level 91"):
        game_session.login(timeout=1.0)


def test_a_login_that_never_reaches_the_game_times_out_with_the_pending_text(monkeypatch: pytest.MonkeyPatch, tmp_path: Path) -> None:
    replies = login_replies()
    replies["1"] = []
    game_session, _fake_socket = connect_fake_session(monkeypatch, tmp_path, replies, GREETING)
    with pytest.raises(session.SessionTimeout, match="Here we go"):
        game_session.login(timeout=0.5)


def test_quit_the_server_never_answers_raises_quit_refused_and_closes_the_socket(monkeypatch: pytest.MonkeyPatch, tmp_path: Path) -> None:
    game_session, fake_socket = connect_fake_session(monkeypatch, tmp_path, {})
    with pytest.raises(QuitRefused):
        game_session.quit(timeout=0.3)
    assert fake_socket.closed, "a refused quit must still release the connection"
    assert game_session.is_closed


def test_quit_answered_with_goodbye_returns_and_marks_the_session_closed(monkeypatch: pytest.MonkeyPatch, tmp_path: Path) -> None:
    game_session, fake_socket = connect_fake_session(monkeypatch, tmp_path, {"quit": [b"Goodbye, friend.. Come back soon!\r\n"]})
    game_session.quit(timeout=0.3)
    assert game_session.is_closed
    assert fake_socket.closed


class ScriptedSocket:
    """Stands in for the server connection: each recv() returns the next scripted chunk, then times out."""

    def __init__(self, chunks: list[bytes], close_after: bool = False) -> None:
        # Chunks still to deliver, in order.
        self._chunks = list(chunks)
        # Whether recv() reports the server closing the connection once the chunks run out.
        self._close_after = close_after

    def settimeout(self, _timeout: float) -> None:
        pass

    def sendall(self, _data: bytes) -> None:
        pass

    def recv(self, _size: int) -> bytes:
        if self._chunks:
            return self._chunks.pop(0)
        if self._close_after:
            return b""
        raise socket.timeout()

    def close(self) -> None:
        pass


def connect_scripted_session(monkeypatch: pytest.MonkeyPatch, tmp_path: Path, chunks: list[bytes], close_after: bool = False) -> GameSession:
    """A session for Testmage whose connection delivers chunks one recv() at a time."""
    scripted_socket = ScriptedSocket(chunks, close_after)
    monkeypatch.setattr(session.socket, "create_connection", lambda _address, timeout: scripted_socket)
    handle = ServerHandle("127.0.0.1", 1, tmp_path / "game.log", None)  # type: ignore[arg-type]
    return GameSession(handle, "Testmage", "harness@example.com", "Harness1x", tmp_path)


COLOURED_ROOM = b"\x1b[1;33mArena Centre\x1b[0m    Exits are: N\r\nS:Powered>"


@pytest.mark.parametrize("split_at", range(1, len(COLOURED_ROOM)))
def test_a_colour_code_split_across_reads_is_stripped_like_an_unsplit_one(monkeypatch: pytest.MonkeyPatch, tmp_path: Path, split_at: int) -> None:
    game_session = connect_scripted_session(monkeypatch, tmp_path, [COLOURED_ROOM[:split_at], COLOURED_ROOM[split_at:]])
    text = game_session.expect(["S:Powered>"], timeout=1.0)
    assert text == "Arena Centre    Exits are: N\r\nS:Powered>"
    assert (tmp_path / "testmage.txt").read_bytes().decode("utf-8") == text


def test_text_after_a_colour_code_completed_by_a_later_read_is_not_lost(monkeypatch: pytest.MonkeyPatch, tmp_path: Path) -> None:
    game_session = connect_scripted_session(monkeypatch, tmp_path, [b"Here we go...\x1b[1;3", b"3mArena Centre\x1b[0m\r\nS:Powered>"])
    assert game_session.expect(["Here we go..."], timeout=1.0) == "Here we go..."
    assert game_session.expect(["S:Powered>"], timeout=1.0) == "Arena Centre\r\nS:Powered>"


def test_a_read_ending_in_a_newline_is_never_held_back(monkeypatch: pytest.MonkeyPatch, tmp_path: Path) -> None:
    game_session = connect_scripted_session(monkeypatch, tmp_path, [b"stray \x1b\n"])
    assert game_session.expect(["\n"], timeout=1.0) == "stray \x1b\n"


def test_the_clear_screen_sequence_is_stripped(monkeypatch: pytest.MonkeyPatch, tmp_path: Path) -> None:
    game_session = connect_scripted_session(monkeypatch, tmp_path, [b"\x1b[H\x1b", b"[JNews\r\nS:Powered>"])
    assert game_session.expect(["S:Powered>"], timeout=1.0) == "News\r\nS:Powered>"


def test_a_closed_connection_error_quotes_the_unread_text(monkeypatch: pytest.MonkeyPatch, tmp_path: Path) -> None:
    game_session = connect_scripted_session(monkeypatch, tmp_path, [b"The ground shakes violently.\r\n"], close_after=True)
    with pytest.raises(ConnectionError, match=r"(?s)server closed the connection.*The ground shakes violently"):
        game_session.expect(["never sent"], timeout=1.0)


def test_quit_is_not_satisfied_by_a_farewell_already_received(monkeypatch: pytest.MonkeyPatch, tmp_path: Path) -> None:
    game_session, fake_socket = connect_fake_session(monkeypatch, tmp_path, {}, greeting=b"Goodbye, friend.. Come back soon!\r\n")
    with pytest.raises(QuitRefused):
        game_session.quit(timeout=0.3)
    assert fake_socket.closed


class FakeEndingSession:
    """Stands in for a GameSession at teardown: records quit and close, and can refuse the quit."""

    def __init__(self, closed: bool = False, quit_failure: Optional[Exception] = None) -> None:
        # Whether the connection is closed; close() and quit() set it.
        self.closed = closed
        # Raised by quit() after closing, as GameSession.quit does when no farewell arrives.
        self._quit_failure = quit_failure
        # How many times quit() and close() were called.
        self.quit_calls = 0
        self.close_calls = 0

    @property
    def is_closed(self) -> bool:
        """True once the fake connection is closed."""
        return self.closed

    def quit(self) -> None:
        """Counts the quit and closes the connection, then raises the configured failure, if any."""
        self.quit_calls += 1
        self.closed = True
        if self._quit_failure is not None:
            raise self._quit_failure

    def close(self) -> None:
        """Counts the close and closes the connection."""
        self.close_calls += 1
        self.closed = True


def test_ending_sessions_on_a_live_server_quits_every_session() -> None:
    sessions = [FakeEndingSession(), FakeEndingSession()]
    assert end_sessions(sessions, server_alive=True) == []
    assert [fake.quit_calls for fake in sessions] == [1, 1]
    assert all(fake.is_closed for fake in sessions)


def test_ending_sessions_on_an_exited_server_closes_them_without_quitting() -> None:
    sessions = [FakeEndingSession(), FakeEndingSession()]
    assert end_sessions(sessions, server_alive=False) == []
    assert [fake.quit_calls for fake in sessions] == [0, 0]
    assert [fake.close_calls for fake in sessions] == [1, 1]


def test_ending_sessions_skips_sessions_already_closed() -> None:
    already_closed = FakeEndingSession(closed=True)
    for server_alive in (True, False):
        assert end_sessions([already_closed], server_alive=server_alive) == []
    assert already_closed.quit_calls == 0
    assert already_closed.close_calls == 0


def test_ending_sessions_collects_quit_failures_and_still_quits_the_rest() -> None:
    refused = FakeEndingSession(quit_failure=QuitRefused("Testimm: quit was refused or never answered"))
    disconnected = FakeEndingSession(quit_failure=ConnectionError("Testmage: server closed the connection"))
    last = FakeEndingSession()
    problems = end_sessions([refused, disconnected, last], server_alive=True)
    assert problems == ["Testimm: quit was refused or never answered", "Testmage: server closed the connection"]
    assert last.quit_calls == 1
