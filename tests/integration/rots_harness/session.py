"""One character's telnet connection to the game: logs in, sends commands, records what the player reads, and ends a test's connections."""

from __future__ import annotations

import re
import socket
import time
from pathlib import Path
from typing import Iterable, List, Optional, Sequence

from rots_harness.launcher import LOGIN_EMAIL_PROMPT, ServerHandle
from rots_harness.telnet import TelnetStreamSanitizer

LOGIN_PASSWORD_PROMPT = "Account password:"
ACCOUNT_MENU_PROMPT = "Choice:"
ACCOUNT_MENU_PLAY = "2"
CHARACTER_CHOICE_PROMPT = "Character number or name:"
CHARACTER_MENU_PROMPT = "Make your choice:"
CHARACTER_MENU_ENTER = "1"
ENTER_GAME_MARKER = "Here we go..."
BAD_CREDENTIALS_MARKER = "Invalid account credentials"
ONE_CHARACTER_ONLINE_MARKER = "You are already connected as"
ONE_CHARACTER_ONLINE_RULE = "an account may have one character online unless it owns a character above level 91 (interpre.cpp, first_restricting_active_account_session_for_account)"
PROMPT_TERMINATORS = (">", "]")
# A complete control sequence: colour codes and the clear-screen pair ESC[H ESC[J.
CSI_PATTERN = re.compile(r"\x1b\[[0-9;?]*[A-Za-z]")
# A control sequence cut off at the end of the received text, still waiting for its final letter.
INCOMPLETE_CSI_PATTERN = re.compile(r"\x1b(\[[0-9;?]*)?\Z")
# How much of the unread text an error message quotes.
PENDING_TEXT_QUOTED = 1500
FAREWELL_MARKERS = ("Goodbye", "As you quit")


class SessionTimeout(AssertionError):
    """The server did not send an awaited marker or prompt in time; the message names it and quotes the pending text."""


class QuitRefused(RuntimeError):
    """The server did not answer `quit` with a farewell, so the character may still be in the game."""


class LoginRefused(AssertionError):
    """The game refused the login; the message carries the game's words."""


def ends_with_prompt(text: str) -> bool:
    """Whether text, ignoring trailing whitespace, ends in a game prompt."""
    stripped = text.rstrip()
    return bool(stripped) and stripped.endswith(PROMPT_TERMINATORS)


class Transcript:
    """The text one command produced, with lookups for the lines scenarios read."""

    def __init__(self, text: str) -> None:
        # The sanitized, colour-free text the command produced.
        self.text = text

    def contains(self, marker: str) -> bool:
        """Whether marker appears anywhere in the text."""
        return marker in self.text

    def room_name(self) -> Optional[str]:
        """The first non-empty line without its exits suffix, or None when the text is blank."""
        for line in self.text.splitlines():
            if line.strip():
                # Two or more spaces separate a room's name from its "Exits are:" list.
                return re.split(r"\s{2,}", line.strip(), maxsplit=1)[0]
        return None


class GameSession:
    """A character's connection to a running server; every byte read is mirrored to a transcript file."""

    def __init__(self, handle: ServerHandle, character_name: str, email: str, password: str, transcript_dir: Path) -> None:
        """Connects to the server; login() then brings the character into the game, picking it from the account's list by name."""
        # Names the character in errors and in the transcript file name, and picks it at the character list.
        self._character_name = character_name
        # The account credentials login() sends.
        self._email = email
        self._password = password
        # The game connection; owned by this session and released by close().
        self._socket = socket.create_connection((handle.host, handle.port), timeout=5.0)
        # A short read timeout lets the wait loops check their deadlines between reads.
        self._socket.settimeout(0.25)
        # Turns received bytes into text, carrying telnet negotiation split across reads.
        self._sanitizer = TelnetStreamSanitizer()
        # Everything received so far, without telnet negotiation or control sequences; only ever appended to.
        self._clean = ""
        # The start of a control sequence split across reads, held back until its final letter arrives.
        self._held_escape = ""
        # Length of the prefix of _clean already returned by expect, drain or command.
        self._consumed = 0
        # Set once close() runs; the socket is gone from then on.
        self._closed = False
        transcript_dir.mkdir(parents=True, exist_ok=True)
        # Appended to after every read so a failed run keeps what the character saw.
        self._transcript_path = transcript_dir / f"{character_name.lower()}.txt"

    @property
    def character_name(self) -> str:
        """The name of the character this session plays."""
        return self._character_name

    @property
    def everything(self) -> str:
        """All text received so far, without telnet negotiation or colour codes."""
        return self._clean

    @property
    def is_closed(self) -> bool:
        """True once the session has closed its connection."""
        return self._closed

    def _pump(self) -> bool:
        """Reads one chunk if any arrives within the read timeout; returns whether one did."""
        try:
            chunk = self._socket.recv(4096)
        except socket.timeout:
            return False
        if not chunk:
            raise ConnectionError(f"{self.character_name}: server closed the connection; unread text:\n{self._unconsumed()[-PENDING_TEXT_QUOTED:]}")
        self._sanitizer.feed(chunk)
        received = self._held_escape + self._sanitizer.take_new_text()
        incomplete = INCOMPLETE_CSI_PATTERN.search(received)
        if incomplete is None:
            self._held_escape = ""
        else:
            self._held_escape = received[incomplete.start():]
            received = received[:incomplete.start()]
        fresh = CSI_PATTERN.sub("", received)
        self._clean += fresh
        with self._transcript_path.open("a", encoding="utf-8", newline="") as transcript_file:
            transcript_file.write(fresh)
        return True

    def _unconsumed(self) -> str:
        return self._clean[self._consumed:]

    def _consume(self) -> str:
        text = self._unconsumed()
        self._consumed = len(self._clean)
        return text

    def expect(self, markers: Sequence[str], timeout: float = 8.0) -> str:
        """Waits until any marker appears in the unread text, then returns all of it as read; raises SessionTimeout."""
        deadline = time.monotonic() + timeout
        while True:
            pending = self._unconsumed()
            if any(marker in pending for marker in markers):
                return self._consume()
            if time.monotonic() >= deadline:
                raise SessionTimeout(f"{self.character_name}: none of {list(markers)!r} within {timeout}s; pending text:\n{pending[-PENDING_TEXT_QUOTED:]}")
            self._pump()

    def drain(self, timeout: float = 0.5) -> str:
        """Reads for timeout seconds and returns all unread text as read."""
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            self._pump()
        return self._consume()

    def send_line(self, line: str) -> None:
        """Sends line followed by a newline; characters outside Latin-1 become '?'."""
        self._socket.sendall(line.encode("latin-1", errors="replace") + b"\n")

    def command(self, text: str, timeout: float = 8.0) -> Transcript:
        """Discards unread text, sends text and returns its output up to the next prompt; raises SessionTimeout."""
        self.drain(0.1)
        self.send_line(text)
        deadline = time.monotonic() + timeout
        while True:
            pending = self._unconsumed()
            if ends_with_prompt(pending):
                return Transcript(self._consume())
            if time.monotonic() >= deadline:
                raise SessionTimeout(f"{self.character_name}: no prompt after {text!r} within {timeout}s; pending text:\n{pending[-PENDING_TEXT_QUOTED:]}")
            self._pump()

    def login(self, timeout: float = 15.0) -> None:
        """Logs in through the account menu, picks the character by name and enters the game; raises LoginRefused with the game's words."""
        self.expect([LOGIN_EMAIL_PROMPT], timeout)
        self.send_line(self._email)
        self.expect([LOGIN_PASSWORD_PROMPT], timeout)
        self.send_line(self._password)
        self._expect_or_refuse([ACCOUNT_MENU_PROMPT], timeout)
        self.send_line(ACCOUNT_MENU_PLAY)
        self._expect_or_refuse([CHARACTER_CHOICE_PROMPT], timeout)
        # The server matches the name against the whole roster ignoring case, so menu order does not matter.
        self.send_line(self._character_name)
        self._expect_or_refuse([CHARACTER_MENU_PROMPT], timeout)
        self.send_line(CHARACTER_MENU_ENTER)
        self._expect_or_refuse([ENTER_GAME_MARKER], timeout)
        self.command("look", timeout)

    def _expect_or_refuse(self, markers: List[str], timeout: float) -> str:
        """Like expect, but raises LoginRefused as soon as the game refuses the login instead of waiting out the timeout."""
        text = self.expect([*markers, BAD_CREDENTIALS_MARKER, ONE_CHARACTER_ONLINE_MARKER], timeout)
        if BAD_CREDENTIALS_MARKER in text:
            raise LoginRefused(f"{self.character_name}: the server refused the account login: {BAD_CREDENTIALS_MARKER}")
        if ONE_CHARACTER_ONLINE_MARKER in text:
            refusal = next(line.strip() for line in text.splitlines() if ONE_CHARACTER_ONLINE_MARKER in line)
            raise LoginRefused(f"{self.character_name}: {refusal} The game's rule: {ONE_CHARACTER_ONLINE_RULE}.")
        return text

    def quit(self, timeout: float = 8.0) -> None:
        """Quits the game and closes the connection either way; raises QuitRefused when no farewell arrives.

        Text received before the quit is discarded first, so only a farewell that answers this quit counts.
        """
        try:
            self.drain(0.1)
            self.send_line("quit")
            self.expect(FAREWELL_MARKERS, timeout)
        except SessionTimeout as refusal:
            raise QuitRefused(f"{self.character_name}: quit was refused or never answered") from refusal
        finally:
            # The game returns a quitting character to the character menu with the socket
            # still open; closing it is what ends the session.
            self.close()

    def close(self) -> None:
        """Closes the connection; later calls do nothing more."""
        self._closed = True
        try:
            self._socket.close()
        except OSError:
            pass


def end_sessions(sessions: Iterable[GameSession], server_alive: bool) -> list[str]:
    """Quits each open session, or only closes it when the server has exited; returns the quit failures' messages.

    Sessions already closed are skipped. A failed quit does not stop the rest from ending.
    """
    problems: list[str] = []
    for game_session in sessions:
        if game_session.is_closed:
            continue
        if not server_alive:
            # An exited server cannot answer quit, and the caller's crash checks report the exit;
            # a quit attempt would only add a second, misleading failure.
            game_session.close()
            continue
        try:
            game_session.quit()
        except (QuitRefused, ConnectionError) as quit_failure:
            problems.append(str(quit_failure))
    return problems
