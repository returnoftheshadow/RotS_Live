from __future__ import annotations

import socket
import time
from pathlib import Path
from typing import Optional

import pytest

from rots_harness import session
from rots_harness.launcher import LOGIN_EMAIL_PROMPT, ServerHandle
from rots_harness.session import (
    GameSession,
    QuitRefused,
    SessionTimeout,
    Transcript,
    end_sessions,
    ends_with_prompt,
)
from rots_harness.telnet import (
    DO,
    IAC,
    OPTION_CHARSET,
    OPTION_MCCP2,
    OPTION_MSDP,
    OPTION_NAWS,
    OPTION_TTYPE,
    SB,
    SE,
    WILL,
    TelnetNegotiation,
    TelnetSubnegotiation,
    TelnetText,
    subnegotiation_bytes,
)
from rots_harness.telnet_payloads import TelnetPayloadError, encode_msdp
from rots_harness.telnet_profile import MSDP_CLIENT, SILENT_CLIENT, TelnetClientProfile
from rots_harness.telnet_record import TelnetDirection
from unit.fakes import FakeSocket


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


# What the server sends a new connection, ending in the email prompt.
GREETING = b"Log in with your account email address at the prompt.\r\n\r\nAccount email: "
# The account's character list and the prompt for the character to play.
CHARACTER_LIST = b"\r\n1) [100 Imm] Testimm     2) [ 30 Hum] Testmage\r\n\r\nCharacter number or name: "
# The character menu and its prompt.
CHARACTER_MENU = b"\r\nWelcome to Arda!\r\n0) Back to Account Menu.\r\n1) Enter the game.\r\n\r\n   Make your choice: "
# The room Testmage enters the game in, ending in a prompt.
ARENA = b"\r\nArena Centre    Exits are: N E S W\r\nThe centre of the harness arena.\r\n\r\nS:Powered>"
# What the character menu's enter choice answers before the room.
ARRIVAL = b"\r\nThere is no unread news.\r\n\r\nHere we go...\r\n"


def login_replies() -> dict[str, list[bytes | tuple[bytes, ...]]]:
    """Return a server that lets Testmage in when the session picks it by name.

    The session picks it from the character list.
    """
    return {
        "harness@example.com": [b"Account password: "],
        "Harness1x": [b"\r\nAccount: harness@example.com\r\n2) Play a linked character\r\n0) Log out\r\n\r\nChoice: "],
        "2": [CHARACTER_LIST],
        "Testmage": [CHARACTER_MENU],
        "1": [ARRIVAL + ARENA],
    }


def connect_fake_session(
    monkeypatch: pytest.MonkeyPatch,
    tmp_path: Path,
    replies: dict[str, list[bytes | tuple[bytes, ...]]],
    greeting: bytes | tuple[bytes, ...] = b"",
    *,
    client: TelnetClientProfile = SILENT_CLIENT,
    closed_by_server: bool = False,
) -> tuple[GameSession, FakeSocket]:
    """Return a session for Testmage, negotiating as `client`, connected to a FakeSocket.

    The socket answers with `replies` after sending `greeting`, and with `closed_by_server` reports
    the server closing the connection once nothing is pending.
    """
    fake_socket = FakeSocket(replies, greeting, closed_by_server=closed_by_server)
    monkeypatch.setattr(session.socket, "create_connection", lambda _address, timeout: fake_socket)
    handle = ServerHandle("127.0.0.1", 1, tmp_path / "game.log", None)  # type: ignore[arg-type]
    return GameSession(handle, "Testmage", "harness@example.com", "Harness1x", tmp_path, client=client), fake_socket


def test_login_walks_the_account_menu_and_enters_the_game(monkeypatch: pytest.MonkeyPatch, tmp_path: Path) -> None:
    game_session, fake_socket = connect_fake_session(monkeypatch, tmp_path, login_replies(), GREETING)
    game_session.login(timeout=1.0)
    assert "Arena Centre" in game_session.everything
    assert fake_socket.sent_lines[:4] == [
        "harness@example.com",
        "Harness1x",
        "2",
        "Testmage",
    ], "the character is picked by name"
    assert (tmp_path / "testmage.txt").read_text(encoding="utf-8").rstrip().endswith("S:Powered>")


def test_login_returns_at_the_arrival_prompt_without_sending_a_command(
    monkeypatch: pytest.MonkeyPatch, tmp_path: Path
) -> None:
    game_session, fake_socket = connect_fake_session(monkeypatch, tmp_path, login_replies(), GREETING)
    game_session.login(timeout=1.0)
    assert fake_socket.sent_lines == ["harness@example.com", "Harness1x", "2", "Testmage", "1"]
    assert game_session.drain(0.05) == "", "login read everything up to the arrival prompt"


def test_login_waits_for_an_arrival_prompt_that_comes_in_a_later_read(
    monkeypatch: pytest.MonkeyPatch, tmp_path: Path
) -> None:
    replies = login_replies()
    replies["1"] = [(ARRIVAL, ARENA)]
    game_session, fake_socket = connect_fake_session(monkeypatch, tmp_path, replies, GREETING)
    game_session.login(timeout=1.0)
    assert "look" not in fake_socket.sent_lines
    assert game_session.drain(0.05) == "", "login read the room and its prompt"
    assert game_session.everything.rstrip().endswith("S:Powered>")


def test_a_login_with_no_arrival_prompt_times_out_naming_the_arrival_marker(
    monkeypatch: pytest.MonkeyPatch, tmp_path: Path
) -> None:
    replies = login_replies()
    replies["1"] = [ARRIVAL + b"\r\nArena Centre    Exits are: N E S W\r\n"]
    game_session, _fake_socket = connect_fake_session(monkeypatch, tmp_path, replies, GREETING)
    with pytest.raises(session.SessionTimeout, match=r"no prompt after 'Here we go\.\.\.' within 0.3s"):
        game_session.login(timeout=0.3)


def test_bad_credentials_fail_the_login_at_once(monkeypatch: pytest.MonkeyPatch, tmp_path: Path) -> None:
    replies = login_replies()
    replies["Harness1x"] = [b"Invalid account credentials.\r\nAccount password: "]
    game_session, _fake_socket = connect_fake_session(monkeypatch, tmp_path, replies, GREETING)
    with pytest.raises(session.LoginRefused, match="Invalid account credentials"):
        game_session.login(timeout=1.0)


def test_the_one_character_online_rule_fails_the_login_with_a_pointer(
    monkeypatch: pytest.MonkeyPatch, tmp_path: Path
) -> None:
    replies = login_replies()
    replies["Testmage"][0] = (
        b"\r\nYou are already connected as Testimm. Reconnect to that character or log out before "
        b"playing another character.\r\n"
    )
    game_session, _fake_socket = connect_fake_session(monkeypatch, tmp_path, replies, GREETING)
    with pytest.raises(session.LoginRefused, match=r"(?s)already connected as Testimm.*above level 91"):
        game_session.login(timeout=1.0)


def test_a_login_that_never_reaches_the_game_times_out_with_the_pending_text(
    monkeypatch: pytest.MonkeyPatch, tmp_path: Path
) -> None:
    replies = login_replies()
    replies["1"] = []
    game_session, _fake_socket = connect_fake_session(monkeypatch, tmp_path, replies, GREETING)
    with pytest.raises(session.SessionTimeout, match="Here we go"):
        game_session.login(timeout=0.5)


def test_quit_the_server_never_answers_raises_quit_refused_and_closes_the_socket(
    monkeypatch: pytest.MonkeyPatch, tmp_path: Path
) -> None:
    game_session, fake_socket = connect_fake_session(monkeypatch, tmp_path, {})
    with pytest.raises(QuitRefused):
        game_session.quit(timeout=0.3)
    assert fake_socket.closed, "a refused quit must still release the connection"
    assert game_session.is_closed


def test_quit_answered_with_goodbye_returns_and_marks_the_session_closed(
    monkeypatch: pytest.MonkeyPatch, tmp_path: Path
) -> None:
    game_session, fake_socket = connect_fake_session(
        monkeypatch, tmp_path, {"quit": [b"Goodbye, friend.. Come back soon!\r\n"]}
    )
    game_session.quit(timeout=0.3)
    assert game_session.is_closed
    assert fake_socket.closed


class ScriptedSocket:
    """Stands in for the server connection.

    Each recv() returns the next scripted chunk, then times out.
    """

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


def connect_scripted_session(
    monkeypatch: pytest.MonkeyPatch, tmp_path: Path, chunks: list[bytes], close_after: bool = False
) -> GameSession:
    """Return a session for Testmage whose connection delivers chunks one recv() at a time."""
    scripted_socket = ScriptedSocket(chunks, close_after)
    monkeypatch.setattr(session.socket, "create_connection", lambda _address, timeout: scripted_socket)
    handle = ServerHandle("127.0.0.1", 1, tmp_path / "game.log", None)  # type: ignore[arg-type]
    return GameSession(handle, "Testmage", "harness@example.com", "Harness1x", tmp_path)


# A room with colour codes, ending in a prompt.
COLOURED_ROOM = b"\x1b[1;33mArena Centre\x1b[0m    Exits are: N\r\nS:Powered>"


@pytest.mark.parametrize("split_at", range(1, len(COLOURED_ROOM)))
def test_a_colour_code_split_across_reads_is_stripped_like_an_unsplit_one(
    monkeypatch: pytest.MonkeyPatch, tmp_path: Path, split_at: int
) -> None:
    game_session = connect_scripted_session(monkeypatch, tmp_path, [COLOURED_ROOM[:split_at], COLOURED_ROOM[split_at:]])
    text = game_session.expect(["S:Powered>"], timeout=1.0)
    assert text == "Arena Centre    Exits are: N\r\nS:Powered>"
    assert (tmp_path / "testmage.txt").read_bytes().decode("utf-8") == text


def test_text_after_a_colour_code_completed_by_a_later_read_is_not_lost(
    monkeypatch: pytest.MonkeyPatch, tmp_path: Path
) -> None:
    game_session = connect_scripted_session(
        monkeypatch, tmp_path, [b"Here we go...\x1b[1;3", b"3mArena Centre\x1b[0m\r\nS:Powered>"]
    )
    assert game_session.expect(["Here we go..."], timeout=1.0) == "Here we go..."
    assert game_session.expect(["S:Powered>"], timeout=1.0) == "Arena Centre\r\nS:Powered>"


def test_a_read_ending_in_a_newline_is_never_held_back(monkeypatch: pytest.MonkeyPatch, tmp_path: Path) -> None:
    game_session = connect_scripted_session(monkeypatch, tmp_path, [b"stray \x1b\n"])
    assert game_session.expect(["\n"], timeout=1.0) == "stray \x1b\n"


def test_the_clear_screen_sequence_is_stripped(monkeypatch: pytest.MonkeyPatch, tmp_path: Path) -> None:
    game_session = connect_scripted_session(monkeypatch, tmp_path, [b"\x1b[H\x1b", b"[JNews\r\nS:Powered>"])
    assert game_session.expect(["S:Powered>"], timeout=1.0) == "News\r\nS:Powered>"


def test_a_closed_connection_error_quotes_the_unread_text(monkeypatch: pytest.MonkeyPatch, tmp_path: Path) -> None:
    game_session = connect_scripted_session(
        monkeypatch, tmp_path, [b"The ground shakes violently.\r\n"], close_after=True
    )
    with pytest.raises(ConnectionError, match=r"(?s)server closed the connection.*The ground shakes violently"):
        game_session.expect(["never sent"], timeout=1.0)


def test_quit_is_not_satisfied_by_a_farewell_already_received(monkeypatch: pytest.MonkeyPatch, tmp_path: Path) -> None:
    game_session, fake_socket = connect_fake_session(
        monkeypatch, tmp_path, {}, greeting=b"Goodbye, friend.. Come back soon!\r\n"
    )
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
        # How many times quit() was called.
        self.quit_calls = 0
        # How many times close() was called.
        self.close_calls = 0
        # Whether the fake reached the game; end_sessions quits only a session that did.
        self.logged_in = True

    @property
    def is_closed(self) -> bool:
        """Return True once the fake connection is closed."""
        return self.closed

    def quit(self) -> None:
        """Count the quit and close the connection, then raise the configured failure, if any."""
        self.quit_calls += 1
        self.closed = True
        if self._quit_failure is not None:
            raise self._quit_failure

    def close(self) -> None:
        """Count the close and close the connection."""
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


# The server's first offer on connect.
DO_TTYPE = bytes([IAC, DO, OPTION_TTYPE])
# What a session that never answers negotiation sends during login().
SILENT_LOGIN_BYTES = b"harness@example.com\nHarness1x\n2\nTestmage\n1\n"
# The payload of an MSDP frame the server sends once the client agrees to MSDP.
MSDP_PAYLOAD = encode_msdp("ROOM_VNUM", "1100")
# That frame as it arrives.
MSDP_FRAME = subnegotiation_bytes(OPTION_MSDP, MSDP_PAYLOAD)


def test_a_silent_session_sends_only_its_lines(monkeypatch: pytest.MonkeyPatch, tmp_path: Path) -> None:
    game_session, fake_socket = connect_fake_session(monkeypatch, tmp_path, login_replies(), DO_TTYPE + GREETING)
    game_session.login(timeout=1.0)
    assert fake_socket.sent_bytes == SILENT_LOGIN_BYTES


def test_an_msdp_client_answers_do_ttype_before_the_email(monkeypatch: pytest.MonkeyPatch, tmp_path: Path) -> None:
    game_session, fake_socket = connect_fake_session(
        monkeypatch, tmp_path, login_replies(), DO_TTYPE + GREETING, client=MSDP_CLIENT
    )
    game_session.expect([LOGIN_EMAIL_PROMPT], timeout=1.0)
    assert fake_socket.sent_bytes == bytes([IAC, WILL, OPTION_TTYPE])


def test_a_negotiation_split_across_reads_is_answered_once(monkeypatch: pytest.MonkeyPatch, tmp_path: Path) -> None:
    chunks = (bytes([IAC]), bytes([DO]), bytes([OPTION_TTYPE]) + GREETING)
    game_session, fake_socket = connect_fake_session(
        monkeypatch, tmp_path, login_replies(), chunks, client=MSDP_CLIENT
    )
    game_session.expect([LOGIN_EMAIL_PROMPT], timeout=1.0)
    assert fake_socket.sent_bytes == bytes([IAC, WILL, OPTION_TTYPE])


def test_everything_raw_keeps_the_colour_that_everything_strips(
    monkeypatch: pytest.MonkeyPatch, tmp_path: Path
) -> None:
    game_session, _fake_socket = connect_fake_session(monkeypatch, tmp_path, {}, b"\x1b[33mArena\x1b[0m>")
    game_session.drain(0.1)
    assert game_session.everything.endswith("Arena>")
    assert "\x1b[33m" in game_session.everything_raw


def test_a_wait_finds_a_subnegotiation_that_arrived_before_it(
    monkeypatch: pytest.MonkeyPatch, tmp_path: Path
) -> None:
    game_session, _fake_socket = connect_fake_session(monkeypatch, tmp_path, {}, MSDP_FRAME + GREETING)
    game_session.drain(0.1)
    assert game_session.expect_subnegotiation(OPTION_MSDP).event.payload == MSDP_PAYLOAD
    with pytest.raises(SessionTimeout):
        game_session.expect_subnegotiation(OPTION_MSDP, timeout=0.3)


def test_waits_in_the_reverse_of_arrival_order_each_find_their_event(
    monkeypatch: pytest.MonkeyPatch, tmp_path: Path
) -> None:
    greeting = DO_TTYPE + bytes([IAC, WILL, OPTION_MSDP]) + bytes([IAC, DO, OPTION_NAWS]) + GREETING
    game_session, _fake_socket = connect_fake_session(monkeypatch, tmp_path, {}, greeting)
    naws_wait = game_session.expect_negotiation(DO, OPTION_NAWS, timeout=1.0)
    assert naws_wait.event == TelnetNegotiation(DO, OPTION_NAWS)
    msdp_wait = game_session.expect_negotiation(WILL, OPTION_MSDP, timeout=1.0)
    assert msdp_wait.event == TelnetNegotiation(WILL, OPTION_MSDP)
    ttype_wait = game_session.expect_negotiation(DO, OPTION_TTYPE, timeout=1.0)
    assert ttype_wait.event == TelnetNegotiation(DO, OPTION_TTYPE)
    email_text = game_session.expect([LOGIN_EMAIL_PROMPT], timeout=1.0)
    assert LOGIN_EMAIL_PROMPT in email_text, "a telnet wait leaves the text unread"


def test_a_subnegotiation_wait_skips_frames_its_test_rejects(monkeypatch: pytest.MonkeyPatch, tmp_path: Path) -> None:
    other_payload = encode_msdp("HEALTH", "10")
    greeting = subnegotiation_bytes(OPTION_MSDP, other_payload) + MSDP_FRAME
    game_session, _fake_socket = connect_fake_session(monkeypatch, tmp_path, {}, greeting)
    matching_wait = game_session.expect_subnegotiation(
        OPTION_MSDP, timeout=1.0, matching=lambda payload: payload == MSDP_PAYLOAD
    )
    assert matching_wait.event.payload == MSDP_PAYLOAD
    assert game_session.expect_subnegotiation(OPTION_MSDP, timeout=1.0).event.payload == other_payload


def test_a_wait_timeout_lists_the_unclaimed_events(monkeypatch: pytest.MonkeyPatch, tmp_path: Path) -> None:
    game_session, _fake_socket = connect_fake_session(
        monkeypatch, tmp_path, {}, bytes([IAC, WILL, OPTION_MSDP]) + GREETING
    )
    with pytest.raises(SessionTimeout, match=r"(?s)DO NAWS.*WILL MSDP") as timeout:
        game_session.expect_negotiation(DO, OPTION_NAWS, timeout=0.3)
    assert "Account email" not in str(timeout.value), "text is not listed among the unclaimed events"


def test_the_server_closing_during_a_telnet_wait_raises_connection_error(
    monkeypatch: pytest.MonkeyPatch, tmp_path: Path
) -> None:
    game_session, _fake_socket = connect_fake_session(monkeypatch, tmp_path, {}, GREETING, closed_by_server=True)
    with pytest.raises(ConnectionError, match=r"(?s)server closed the connection.*Account email"):
        game_session.expect_negotiation(DO, OPTION_TTYPE, timeout=1.0)


def test_a_start_compression_frame_raises(monkeypatch: pytest.MonkeyPatch, tmp_path: Path) -> None:
    game_session, _fake_socket = connect_fake_session(
        monkeypatch, tmp_path, {}, GREETING + bytes([IAC, SB, OPTION_MCCP2, IAC, SE])
    )
    with pytest.raises(session.TelnetCompressionUnsupported, match=r"(?s)MCCP2.*Account email"):
        game_session.drain(0.1)
    assert game_session.everything == GREETING.decode("latin-1"), "text before the frame still reaches the transcript"


def test_a_start_compression_frame_closes_the_session(monkeypatch: pytest.MonkeyPatch, tmp_path: Path) -> None:
    game_session, fake_socket = connect_fake_session(
        monkeypatch, tmp_path, {}, GREETING + bytes([IAC, SB, OPTION_MCCP2, IAC, SE])
    )
    with pytest.raises(session.TelnetCompressionUnsupported):
        game_session.drain(0.1)
    assert game_session.is_closed, "end_sessions skips the session instead of waiting out a quit"
    assert fake_socket.closed


def test_a_malformed_charset_request_still_records_the_rest_of_its_chunk(
    monkeypatch: pytest.MonkeyPatch, tmp_path: Path
) -> None:
    # A REQUEST with no separator, between the DO that turns CHARSET on and the text and offer
    # after it.
    chunk = bytes([IAC, DO, OPTION_CHARSET]) + subnegotiation_bytes(OPTION_CHARSET, b"\x01") + GREETING + DO_TTYPE
    game_session, fake_socket = connect_fake_session(monkeypatch, tmp_path, {}, chunk, client=MSDP_CLIENT)
    with pytest.raises(TelnetPayloadError, match="separator"):
        game_session.drain(0.1)
    received = [
        recorded.event for recorded in game_session.telnet_events if recorded.direction is TelnetDirection.RECEIVED
    ]
    assert received == [
        TelnetNegotiation(DO, OPTION_CHARSET),
        TelnetSubnegotiation(OPTION_CHARSET, b"\x01"),
        TelnetText(GREETING),
        TelnetNegotiation(DO, OPTION_TTYPE),
    ]
    assert game_session.everything == GREETING.decode("latin-1")
    assert fake_socket.sent_bytes == bytes(
        [IAC, WILL, OPTION_CHARSET, IAC, WILL, OPTION_TTYPE]
    ), "events after the failure are still answered"


def test_send_line_doubles_a_255(monkeypatch: pytest.MonkeyPatch, tmp_path: Path) -> None:
    game_session, fake_socket = connect_fake_session(monkeypatch, tmp_path, {})
    game_session.send_line("\xff")
    assert fake_socket.sent_bytes == b"\xff\xff\n"


def test_the_raw_senders_send_their_bytes_verbatim(monkeypatch: pytest.MonkeyPatch, tmp_path: Path) -> None:
    game_session, fake_socket = connect_fake_session(monkeypatch, tmp_path, {})
    game_session.send_bytes(b"\x00\xff\xff")
    game_session.send_negotiation(DO, OPTION_MSDP)
    game_session.send_subnegotiation(OPTION_NAWS, b"\x00\xff")
    game_session.send_msdp("LIST", "COMMANDS")
    assert fake_socket.sent_bytes == (
        b"\x00\xff\xff"
        + bytes([IAC, DO, OPTION_MSDP])
        + subnegotiation_bytes(OPTION_NAWS, b"\x00\xff")
        + subnegotiation_bytes(OPTION_MSDP, encode_msdp("LIST", "COMMANDS"))
    )
    sent_events = [
        recorded.event for recorded in game_session.telnet_events if recorded.direction is TelnetDirection.SENT
    ]
    assert sent_events == [
        TelnetText(b"\x00\xff"),
        TelnetNegotiation(DO, OPTION_MSDP),
        TelnetSubnegotiation(OPTION_NAWS, b"\x00\xff"),
        TelnetSubnegotiation(OPTION_MSDP, encode_msdp("LIST", "COMMANDS")),
    ]


def test_the_wire_files_hold_the_bytes_both_ways(monkeypatch: pytest.MonkeyPatch, tmp_path: Path) -> None:
    greeting = DO_TTYPE + GREETING
    game_session, fake_socket = connect_fake_session(
        monkeypatch, tmp_path, login_replies(), greeting, client=MSDP_CLIENT
    )
    game_session.expect([LOGIN_EMAIL_PROMPT], timeout=1.0)
    game_session.send_line("harness@example.com")
    assert game_session.received_bytes == greeting
    assert (tmp_path / "testmage.received.bin").read_bytes() == greeting
    assert (tmp_path / "testmage.sent.bin").read_bytes() == fake_socket.sent_bytes
    telnet_log = (tmp_path / "testmage.telnet.log").read_text(encoding="utf-8")
    assert "received DO TTYPE" in telnet_log
    assert "sent     WILL TTYPE" in telnet_log


def test_login_marks_the_session_logged_in(monkeypatch: pytest.MonkeyPatch, tmp_path: Path) -> None:
    game_session, _fake_socket = connect_fake_session(monkeypatch, tmp_path, login_replies(), GREETING)
    assert not game_session.logged_in
    game_session.login(timeout=1.0)
    assert game_session.logged_in


def test_end_sessions_closes_a_session_that_never_logged_in_without_quitting(
    monkeypatch: pytest.MonkeyPatch, tmp_path: Path
) -> None:
    game_session, fake_socket = connect_fake_session(
        monkeypatch, tmp_path, {"quit": [b"Goodbye, friend.. Come back soon!\r\n"]}, GREETING
    )
    assert end_sessions([game_session], server_alive=True) == []
    assert "quit" not in fake_socket.sent_lines
    assert game_session.is_closed
    assert fake_socket.closed


class SilentSocket:
    """Stands in for a server that sends nothing.

    Each recv() blocks for the timeout last set, then times out.
    """

    def __init__(self) -> None:
        # The timeout last set; recv() waits it out, as a real socket does.
        self._timeout: Optional[float] = None
        # The timeout in effect at each recv(), in order.
        self.read_timeouts: list[float] = []
        # The timeout in effect at each sendall(), in order.
        self.send_timeouts: list[Optional[float]] = []

    def settimeout(self, timeout: float) -> None:
        self._timeout = timeout

    def sendall(self, _data: bytes) -> None:
        self.send_timeouts.append(self._timeout)

    def recv(self, _size: int) -> bytes:
        assert (
            self._timeout is not None and self._timeout > 0
        ), "the session sets a positive read timeout before it reads"
        self.read_timeouts.append(self._timeout)
        time.sleep(self._timeout)
        raise socket.timeout()

    def close(self) -> None:
        pass


def connect_silent_session(monkeypatch: pytest.MonkeyPatch, tmp_path: Path) -> tuple[GameSession, SilentSocket]:
    """Return a session for Testmage connected to a SilentSocket."""
    silent_socket = SilentSocket()
    monkeypatch.setattr(session.socket, "create_connection", lambda _address, timeout: silent_socket)
    handle = ServerHandle("127.0.0.1", 1, tmp_path / "game.log", None)  # type: ignore[arg-type]
    return GameSession(handle, "Testmage", "harness@example.com", "Harness1x", tmp_path), silent_socket


def test_a_short_drain_reads_for_no_longer_than_its_own_time(monkeypatch: pytest.MonkeyPatch, tmp_path: Path) -> None:
    game_session, silent_socket = connect_silent_session(monkeypatch, tmp_path)
    started = time.monotonic()
    assert game_session.drain(0.1) == ""
    assert silent_socket.read_timeouts
    assert all(timeout <= 0.1 for timeout in silent_socket.read_timeouts)
    assert time.monotonic() - started < 0.2


def test_a_short_expect_reads_for_no_longer_than_its_own_time(monkeypatch: pytest.MonkeyPatch, tmp_path: Path) -> None:
    game_session, silent_socket = connect_silent_session(monkeypatch, tmp_path)
    started = time.monotonic()
    with pytest.raises(session.SessionTimeout, match=r"none of \['never sent'\] within 0.1s"):
        game_session.expect(["never sent"], timeout=0.1)
    assert all(timeout <= 0.1 for timeout in silent_socket.read_timeouts)
    assert time.monotonic() - started < 0.2


def test_a_short_command_reads_for_no_longer_than_its_drain_and_its_wait(
    monkeypatch: pytest.MonkeyPatch, tmp_path: Path
) -> None:
    game_session, silent_socket = connect_silent_session(monkeypatch, tmp_path)
    started = time.monotonic()
    with pytest.raises(session.SessionTimeout, match=r"no prompt after 'look' within 0.1s"):
        game_session.command("look", timeout=0.1)
    longest_wait = max(session.PRE_COMMAND_DRAIN_SECONDS, 0.1)
    assert all(timeout <= longest_wait for timeout in silent_socket.read_timeouts)
    # The command drains before it sends, then waits 0.1 s for the prompt.
    assert time.monotonic() - started < 2 * (session.PRE_COMMAND_DRAIN_SECONDS + 0.1)


def test_every_read_timeout_is_positive_and_no_longer_than_the_session_read_timeout(
    monkeypatch: pytest.MonkeyPatch, tmp_path: Path
) -> None:
    game_session, silent_socket = connect_silent_session(monkeypatch, tmp_path)
    game_session.drain(0.3)
    with pytest.raises(session.SessionTimeout):
        game_session.expect(["never sent"], timeout=0.05)
    assert silent_socket.read_timeouts
    assert all(0 < timeout <= session.READ_TIMEOUT_SECONDS for timeout in silent_socket.read_timeouts)


def test_a_send_after_a_short_read_uses_the_send_timeout(monkeypatch: pytest.MonkeyPatch, tmp_path: Path) -> None:
    game_session, silent_socket = connect_silent_session(monkeypatch, tmp_path)
    game_session.drain(0.01)
    game_session.send_line("look")
    assert silent_socket.send_timeouts == [session.SEND_TIMEOUT_SECONDS]
