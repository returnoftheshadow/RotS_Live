"""One character's telnet connection to the game.

It logs in, sends commands, records what the player reads, and ends a test's connections.
"""

from __future__ import annotations

import re
import socket
import time
from pathlib import Path
from typing import Iterable, List, Optional, Sequence

from rots_harness.launcher import LOGIN_EMAIL_PROMPT, ServerHandle
from rots_harness.telnet import TelnetStreamSanitizer

# The prompt for the account password, after the email.
LOGIN_PASSWORD_PROMPT = "Account password:"
# The account menu's prompt.
ACCOUNT_MENU_PROMPT = "Choice:"
# The account menu choice that plays a character.
ACCOUNT_MENU_PLAY = "2"
# The prompt for the character to play, under the account's character list.
CHARACTER_CHOICE_PROMPT = "Character number or name:"
# The character menu's prompt.
CHARACTER_MENU_PROMPT = "Make your choice:"
# The character menu choice that enters the game.
CHARACTER_MENU_ENTER = "1"
# What the character menu's enter choice answers as the character enters the game (WELC_MESSG,
# config.cpp).
ENTER_GAME_REPLY = "Here we go..."
# The start of what a refused account password prints (CON_ACCTPWD in nanny, interpre.cpp).
BAD_CREDENTIALS_START = "Invalid account credentials"
# The start of what choosing a second character to play prints while one is online
# (active_account_session_restriction_message_for_session, interpre.cpp).
ONE_CHARACTER_ONLINE_START = "You are already connected as"
# The first word of the line that enters a character straight from the email prompt; only the
# harness binary accepts it, and only with its run's entry token.
ENTRY_WORD = "harness-enter"
# The start of what the harness binary sends before it closes a connection whose entry line it
# refused (TokenEntryPolicy::REFUSAL_PREFIX, harness/token_entry_policy.h).
ENTRY_REFUSED_START = "Harness entry refused:"
# The rule LoginRefused cites when the game refuses a second character online.
ONE_CHARACTER_ONLINE_RULE = (
    "an account may have one character online unless it owns a character above level 91 "
    "(interpre.cpp, first_restricting_active_account_session_for_account)"
)
# The characters a game prompt ends in.
PROMPT_TERMINATORS = (">", "]")
# A complete control sequence: colour codes and the clear-screen pair ESC[H ESC[J.
CSI_PATTERN = re.compile(r"\x1b\[[0-9;?]*[A-Za-z]")
# A control sequence cut off at the end of the received text, still waiting for its final letter.
INCOMPLETE_CSI_PATTERN = re.compile(r"\x1b(\[[0-9;?]*)?\Z")
# How much of the unread text an error message quotes.
PENDING_TEXT_QUOTED = 1500
# The starts of the lines quit answers with as it lets the character go (do_quit, act_othe.cpp).
FAREWELL_STARTS = ("Goodbye", "As you quit")
# The longest one read blocks, so a wait loop checks its deadline at least this often.
READ_TIMEOUT_SECONDS = 0.25
# The shortest read timeout a wait loop sets: settimeout(0) would make the socket non-blocking.
MINIMUM_READ_TIMEOUT_SECONDS = 0.005
# The longest one send blocks; a read sets its own shorter timeout, so every send sets this first.
SEND_TIMEOUT_SECONDS = 5.0
# How long command() and quit() read before they send, to discard text that arrived before them.
PRE_COMMAND_DRAIN_SECONDS = 0.1
# How long a login refusal cut short by a split read waits for the rest of its line.
REFUSAL_LINE_SECONDS = 2.0


class SessionTimeout(AssertionError):
    """The server did not send an awaited marker or prompt in time.

    The message names it and quotes the pending text.
    """


class QuitRefused(RuntimeError):
    """The server answered `quit` with no farewell, so the character may still be in the game."""


class LoginRefused(AssertionError):
    """The game refused the login; the message carries the game's words."""


def ends_with_prompt(text: str) -> bool:
    """Return whether text, ignoring trailing whitespace, ends in a game prompt."""
    stripped = text.rstrip()
    return bool(stripped) and stripped.endswith(PROMPT_TERMINATORS)


class Transcript:
    """The text one command produced, with lookups for the lines scenarios read."""

    def __init__(self, text: str) -> None:
        # The sanitized, colour-free text the command produced.
        self.text = text

    def contains(self, marker: str) -> bool:
        """Return whether marker appears anywhere in the text."""
        return marker in self.text

    def room_name(self) -> Optional[str]:
        """Return the first non-empty line without its exits suffix; None when the text is blank."""
        for line in self.text.splitlines():
            if line.strip():
                # Two or more spaces separate a room's name from its "Exits are:" list.
                return re.split(r"\s{2,}", line.strip(), maxsplit=1)[0]
        return None


class GameSession:
    """A character's connection to a running server.

    Every byte read is mirrored to a transcript file.
    """

    def __init__(
        self,
        handle: ServerHandle,
        character_name: str,
        email: str,
        password: str,
        transcript_dir: Path,
        entry_token: Optional[str] = None,
    ) -> None:
        """Connect to the server.

        login() then brings the character into the game: with entry_token when given, else
        through the account menu, picking it from the account's list by name.
        """
        # Names the character in errors and in the transcript file name, and picks it at the
        # character list.
        self._character_name = character_name
        # The account email login() sends.
        self._email = email
        # The account password login() sends.
        self._password = password
        # The harness binary's run token, which login() enters with instead of the account menu;
        # None for the dialogue.
        self._entry_token = entry_token
        # The game connection; owned by this session and released by close().
        self._socket = socket.create_connection((handle.host, handle.port), timeout=5.0)
        # A short read timeout lets the wait loops check their deadlines between reads.
        self._socket.settimeout(READ_TIMEOUT_SECONDS)
        # Turns received bytes into text, carrying telnet negotiation split across reads.
        self._sanitizer = TelnetStreamSanitizer()
        # Everything received so far, without telnet negotiation or control sequences; only ever
        # appended to.
        self._clean = ""
        # The start of a control sequence split across reads, held back until its final letter
        # arrives.
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
        """Return the name of the character this session plays."""
        return self._character_name

    @property
    def everything(self) -> str:
        """Return all text received so far, without telnet negotiation or colour codes."""
        return self._clean

    @property
    def is_closed(self) -> bool:
        """Return True once the session has closed its connection."""
        return self._closed

    def _pump(self, deadline: float) -> bool:
        """Read one chunk if any arrives in time, and return whether one did.

        A chunk is in time when it arrives before the monotonic deadline or within
        READ_TIMEOUT_SECONDS, whichever is sooner.
        """
        time_left = max(MINIMUM_READ_TIMEOUT_SECONDS, deadline - time.monotonic())
        read_timeout = min(READ_TIMEOUT_SECONDS, time_left)
        self._socket.settimeout(read_timeout)
        try:
            chunk = self._socket.recv(4096)
        except socket.timeout:
            return False
        if not chunk:
            raise ConnectionError(
                f"{self.character_name}: server closed the connection; unread "
                f"text:\n{self._unconsumed()[-PENDING_TEXT_QUOTED:]}"
            )
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
        """Wait until any marker appears in the unread text, then return all of it as read.

        Raise SessionTimeout.
        """
        deadline = time.monotonic() + timeout
        while True:
            pending = self._unconsumed()
            if any(marker in pending for marker in markers):
                return self._consume()
            if time.monotonic() >= deadline:
                raise SessionTimeout(
                    f"{self.character_name}: none of {list(markers)!r} within {timeout}s; pending "
                    f"text:\n{pending[-PENDING_TEXT_QUOTED:]}"
                )
            self._pump(deadline)

    def drain(self, timeout: float = 0.5) -> str:
        """Read for timeout seconds and return all unread text as read."""
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            self._pump(deadline)
        return self._consume()

    def send_line(self, line: str) -> None:
        """Send line followed by a newline; characters outside Latin-1 become '?'."""
        self._socket.settimeout(SEND_TIMEOUT_SECONDS)
        self._socket.sendall(line.encode("latin-1", errors="replace") + b"\n")

    def command(self, text: str, timeout: float = 8.0) -> Transcript:
        """Discard unread text, send text and return its output up to the next prompt.

        Raise SessionTimeout.
        """
        self.drain(PRE_COMMAND_DRAIN_SECONDS)
        self.send_line(text)
        return self.wait_for_prompt(timeout, after=text)

    def wait_for_prompt(self, timeout: float = 8.0, after: str = "") -> Transcript:
        """Wait until the unread text ends in a prompt and return it as read.

        Raise SessionTimeout naming what it waited after.
        """
        deadline = time.monotonic() + timeout
        while True:
            pending = self._unconsumed()
            if ends_with_prompt(pending):
                return Transcript(self._consume())
            if time.monotonic() >= deadline:
                raise SessionTimeout(
                    f"{self.character_name}: no prompt after {after!r} within {timeout}s; pending "
                    f"text:\n{pending[-PENDING_TEXT_QUOTED:]}"
                )
            self._pump(deadline)

    def login(self, timeout: float = 15.0) -> None:
        """Bring the character into the game and read up to the arrival prompt.

        With an entry token it enters straight from the email prompt (enter); without one it logs
        in through the account menu and picks the character by name. Raise LoginRefused with the
        game's words.
        """
        if self._entry_token is not None:
            self.enter(self._entry_token, timeout)
            return
        self.expect([LOGIN_EMAIL_PROMPT], timeout)
        self.send_line(self._email)
        self.expect([LOGIN_PASSWORD_PROMPT], timeout)
        self.send_line(self._password)
        self._expect_or_refuse([ACCOUNT_MENU_PROMPT], timeout)
        self.send_line(ACCOUNT_MENU_PLAY)
        self._expect_or_refuse([CHARACTER_CHOICE_PROMPT], timeout)
        # The server matches the name against the whole roster ignoring case, so menu order does not
        # matter.
        self.send_line(self._character_name)
        self._expect_or_refuse([CHARACTER_MENU_PROMPT], timeout)
        self.send_line(CHARACTER_MENU_ENTER)
        arrival = self._expect_or_refuse([ENTER_GAME_REPLY], timeout)
        self._finish_arrival(arrival, timeout)

    def enter(self, entry_token: str, timeout: float = 15.0) -> None:
        """Enter the game straight from the email prompt with this run's entry token.

        Read up to the arrival prompt; raise LoginRefused with the game's words.
        """
        self.expect([LOGIN_EMAIL_PROMPT], timeout)
        self.send_line(f"{ENTRY_WORD} {entry_token} {self._email} {self._character_name}")
        arrival = self._expect_or_refuse([ENTER_GAME_REPLY], timeout)
        self._finish_arrival(arrival, timeout)

    def _finish_arrival(self, arrival: str, timeout: float) -> None:
        """Read up to the prompt that follows the room shown on entering the game.

        Read nothing when arrival already ends in it; raise SessionTimeout.
        """
        if not ends_with_prompt(arrival):
            self.wait_for_prompt(timeout, after=ENTER_GAME_REPLY)

    def _expect_or_refuse(self, markers: List[str], timeout: float) -> str:
        """Wait as expect does, but raise LoginRefused as soon as the game refuses the login.

        It does not wait out the timeout.
        """
        refusal_starts = [ENTRY_REFUSED_START, BAD_CREDENTIALS_START, ONE_CHARACTER_ONLINE_START]
        text = self.expect([*markers, *refusal_starts], timeout)
        if ENTRY_REFUSED_START in text:
            refusal = self._whole_line(text, ENTRY_REFUSED_START)
            raise LoginRefused(f"{self.character_name}: {refusal}")
        if BAD_CREDENTIALS_START in text:
            raise LoginRefused(f"{self.character_name}: the server refused the account login: {BAD_CREDENTIALS_START}")
        if ONE_CHARACTER_ONLINE_START in text:
            refusal = self._whole_line(text, ONE_CHARACTER_ONLINE_START)
            raise LoginRefused(f"{self.character_name}: {refusal} The game's rule: {ONE_CHARACTER_ONLINE_RULE}.")
        return text

    def _whole_line(self, text: str, marker: str) -> str:
        """Return the line of text holding marker, stripped.

        When a split read cut the line short, read on until its line ending, the connection's
        close or REFUSAL_LINE_SECONDS.
        """
        deadline = time.monotonic() + REFUSAL_LINE_SECONDS
        line_start = text.index(marker)
        while not any(ending in text[line_start:] for ending in "\r\n") and time.monotonic() < deadline:
            try:
                self._pump(deadline)
            except ConnectionError:
                break
            text += self._consume()
        return next(line.strip() for line in text[line_start:].splitlines())

    def quit(self, timeout: float = 8.0) -> None:
        """Quit the game and close the connection either way.

        Raise QuitRefused when no farewell arrives.

        Text received before the quit is discarded first, so only a farewell that answers this quit
        counts.
        """
        try:
            self.drain(PRE_COMMAND_DRAIN_SECONDS)
            self.send_line("quit")
            self.expect(FAREWELL_STARTS, timeout)
        except SessionTimeout as refusal:
            raise QuitRefused(f"{self.character_name}: quit was refused or never answered") from refusal
        finally:
            # The game returns a quitting character to the character menu with the socket
            # still open; closing it is what ends the session.
            self.close()

    def close(self) -> None:
        """Close the connection; later calls do nothing more."""
        self._closed = True
        try:
            self._socket.close()
        except OSError:
            pass


def end_sessions(sessions: Iterable[GameSession], server_alive: bool) -> list[str]:
    """Quit each open session, or only close it when the server has exited.

    Return the quit failures' messages.

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
