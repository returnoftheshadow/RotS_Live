from __future__ import annotations

import socket
from pathlib import Path

import pytest

from rots_harness import fixtures, session
from rots_harness.launcher import ServerHandle
from rots_harness.session import GameSession, QuitRefused, Transcript, ends_with_prompt


def test_transcript_parses_the_stat_hit_point_line() -> None:
    transcript = Transcript("Str:[10/10(10)]\nHP :[15/60+3(0)]  Stamina :[0/40+1(0)]  Move :[120/120+2(0)] Spirit:[100/100+0]\n")
    assert transcript.hit_points() == (15, 60)


def test_transcript_reports_no_hit_points_when_the_line_is_absent() -> None:
    assert Transcript("nothing here").hit_points() is None


def test_transcript_room_name_is_the_first_non_empty_line() -> None:
    assert Transcript("\nWood-elf Start\n   A glade where wood elves begin.\n") .room_name() == "Wood-elf Start"


def test_transcript_room_name_drops_the_exits_suffix() -> None:
    transcript = Transcript("\nArena Centre    Exits are: N E S W\nThe centre...\n")
    assert transcript.room_name() == "Arena Centre"


# GameSession.expect_room has no fake/stub to exercise it against (GameSession opens a real
# socket to a ServerHandle; no test double for it exists in this file or elsewhere under
# tests/integration/unit/), so this covers the room-name matching it relies on instead: a
# PRF_ROOMFLAGS/PRF_ADVANCED_VIEW room line breaks room_name()'s exact match (a single space,
# not the two-or-more it splits on, separates the name from the trailing "(#vnum) [...]"), which
# is exactly the case expect_room's first-non-empty-line fallback (str.startswith) is for.
def test_transcript_room_name_does_not_match_a_flagged_room_line() -> None:
    transcript = Transcript("\nImmortal Start (#11) [ Inside ]    Exits are: N E S W\nA room for immortals.\n")
    assert transcript.room_name() != "Immortal Start"
    first_line = next(line.strip() for line in transcript.text.splitlines() if line.strip())
    assert first_line.startswith("Immortal Start")


def test_ansi_pattern_strips_colour_codes() -> None:
    assert session.ANSI_PATTERN.sub("", "\x1b[33mArena Centre\x1b[0m") == "Arena Centre"


def test_prompt_detection_accepts_trailing_whitespace_and_both_terminators() -> None:
    assert ends_with_prompt("You breathe out fire.\nHP:Healthy > ")
    assert ends_with_prompt("some output\n]")
    assert not ends_with_prompt("You start to cast a spell...")


def test_character_number_prompt_matches_both_server_wordings() -> None:
    assert session.CHARACTER_NUMBER_PROMPT in "\n\rCharacter number: "
    assert session.CHARACTER_NUMBER_PROMPT in "\n\rCharacter number or name: "


def test_transcript_parses_the_stat_ability_line() -> None:
    text = "Str:[14/14/14] Int:[10/10/10] Wil:[12/12/12] Dex:[13/13/13] Con: [11/11/11] Lea:[9/9/9]\n"
    assert Transcript(text).abilities() == {"str": 14, "int": 10, "wil": 12, "dex": 13, "con": 11, "lea": 9}


def test_transcript_abilities_is_none_without_the_line() -> None:
    assert Transcript("HP :[10/60]").abilities() is None


def test_transcript_parses_experience_from_the_stat_level_line() -> None:
    text = (
        "PC 'Harnvictim'  IDNum: [9000004], In room [1131]\n"
        "Spec:(0) None, Lev: [10], XP: [ 200000], Align: [   0]\n"
        "HP :[60/60+3(0)]  Stamina :[40/40+1(0)]  Move :[120/120+2(0)] Spirit:[0/0+0]\n"
    )
    assert Transcript(text).experience() == 200000


def test_transcript_experience_is_none_without_the_level_line() -> None:
    assert Transcript("HP :[10/60]\nAu: 0         Exp: 200000    Align: 0\n").experience() is None


class FakeSocket:
    """Stands in for the server connection: `replies` maps a line the session sends to the
    bytes the server answers with; a line with no entry is never answered."""

    def __init__(self, replies: dict[str, bytes]) -> None:
        self._replies = replies
        self._pending = b""
        self.closed = False

    def settimeout(self, _timeout: float) -> None:
        pass

    def sendall(self, data: bytes) -> None:
        self._pending += self._replies.get(data.decode("latin-1").strip(), b"")

    def recv(self, _size: int) -> bytes:
        if not self._pending:
            raise socket.timeout()
        chunk, self._pending = self._pending, b""
        return chunk

    def close(self) -> None:
        self.closed = True


def connect_fake_session(monkeypatch: pytest.MonkeyPatch, tmp_path: Path, replies: dict[str, bytes]) -> tuple[GameSession, FakeSocket]:
    fake_socket = FakeSocket(replies)
    monkeypatch.setattr(session.socket, "create_connection", lambda _address, timeout: fake_socket)
    handle = ServerHandle("127.0.0.1", 1, tmp_path / "game.log", None)  # type: ignore[arg-type]
    game_session = GameSession(handle, fixtures.STANDARD_ROSTER[1], 2, tmp_path)
    return game_session, fake_socket


def test_quit_the_server_never_answers_raises_quit_refused_and_closes_the_socket(monkeypatch: pytest.MonkeyPatch, tmp_path: Path) -> None:
    game_session, fake_socket = connect_fake_session(monkeypatch, tmp_path, {})
    with pytest.raises(QuitRefused):
        game_session.quit(timeout=0.3)
    assert fake_socket.closed, "a refused quit must still release the connection"
    assert game_session.is_closed


def test_quit_answered_with_goodbye_returns_and_marks_the_session_closed(monkeypatch: pytest.MonkeyPatch, tmp_path: Path) -> None:
    game_session, fake_socket = connect_fake_session(monkeypatch, tmp_path, {"quit": b"Goodbye, friend.. Come back soon!\r\n"})
    game_session.quit(timeout=0.3)
    assert game_session.is_closed
    assert fake_socket.closed


def test_quit_to_menu_reaching_the_menu_leaves_the_socket_open(monkeypatch: pytest.MonkeyPatch, tmp_path: Path) -> None:
    game_session, fake_socket = connect_fake_session(monkeypatch, tmp_path, {"quit": b"Goodbye, friend.. Come back soon!\r\n\r\nMake your choice: "})
    game_session.quit_to_menu(timeout=0.3)
    assert not fake_socket.closed, "the parked body stays registered only while the socket is open"
    assert not game_session.is_closed


def test_drop_link_marks_the_session_closed(monkeypatch: pytest.MonkeyPatch, tmp_path: Path) -> None:
    game_session, fake_socket = connect_fake_session(monkeypatch, tmp_path, {})
    assert not game_session.is_closed
    game_session.drop_link()
    assert game_session.is_closed
    assert fake_socket.closed
