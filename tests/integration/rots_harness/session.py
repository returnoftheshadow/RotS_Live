"""One logged-in telnet connection: sends commands, returns transcripts, parses the few lines scenarios read."""

from __future__ import annotations

import re
import socket
import sys
import time
from pathlib import Path

from rots_harness import fixtures
from rots_harness.launcher import ServerHandle

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "tools"))
from rots_telnet import TelnetStreamSanitizer  # noqa: E402

LOGIN_EMAIL_PROMPT = "Account email:"
LOGIN_PASSWORD_PROMPT = "Account password:"
ACCOUNT_MENU_PROMPT = "Choice:"
CHARACTER_NUMBER_PROMPT = "Character number"
CHARACTER_MENU_PROMPT = "Make your choice:"
ENTER_GAME_MARKER = "Here we go..."
PROMPT_TERMINATORS = (">", "]")
HIT_POINT_PATTERN = re.compile(r"HP :\[(\d+)/(\d+)")
ABILITY_PATTERN = re.compile(r"Str:\[(\d+)/\d+/\d+\] Int:\[(\d+)/\d+/\d+\] Wil:\[(\d+)/\d+/\d+\] Dex:\[(\d+)/\d+/\d+\] Con: \[(\d+)/\d+/\d+\] Lea:\[(\d+)/\d+/\d+\]")
ANSI_PATTERN = re.compile(r"\x1b\[[0-9;]*m")


class SessionTimeout(AssertionError):
    pass


def ends_with_prompt(text: str) -> bool:
    stripped = text.rstrip()
    return bool(stripped) and stripped.endswith(PROMPT_TERMINATORS)


class Transcript:
    def __init__(self, text: str) -> None:
        self.text = text

    def contains(self, marker: str) -> bool:
        return marker in self.text

    def hit_points(self) -> tuple[int, int] | None:
        match = HIT_POINT_PATTERN.search(self.text)
        if match is None:
            return None
        return int(match.group(1)), int(match.group(2))

    def abilities(self) -> dict[str, int] | None:
        """Current values from do_stat's ability line (act_wiz.cpp): name -> current."""
        match = ABILITY_PATTERN.search(self.text)
        if match is None:
            return None
        return dict(zip(("str", "int", "wil", "dex", "con", "lea"), (int(value) for value in match.groups())))

    def room_name(self) -> str | None:
        for line in self.text.splitlines():
            if line.strip():
                return re.split(r"\s{2,}", line.strip(), maxsplit=1)[0]
        return None


class GameSession:
    def __init__(self, handle: ServerHandle, character: fixtures.CharacterSpec, character_number: int, transcript_dir: Path) -> None:
        self.character = character
        self._character_number = character_number
        self._socket = socket.create_connection((handle.host, handle.port), timeout=5.0)
        self._socket.settimeout(0.25)
        self._sanitizer = TelnetStreamSanitizer()
        self._clean = ""
        self._consumed = 0
        transcript_dir.mkdir(parents=True, exist_ok=True)
        self._transcript_path = transcript_dir / f"{character.name.lower()}.txt"

    @property
    def everything(self) -> str:
        return self._clean

    def _pump(self) -> bool:
        try:
            chunk = self._socket.recv(4096)
        except socket.timeout:
            return False
        if not chunk:
            raise ConnectionError(f"{self.character.name}: server closed the connection")
        self._sanitizer.feed(chunk)
        self._clean = ANSI_PATTERN.sub("", self._sanitizer.text)
        self._transcript_path.write_text(self._clean, encoding="utf-8")
        return True

    def _unconsumed(self) -> str:
        return self._clean[self._consumed:]

    def _consume(self) -> str:
        text = self._unconsumed()
        self._consumed = len(self._clean)
        return text

    def expect(self, markers: tuple[str, ...] | list[str], timeout: float = 8.0) -> str:
        deadline = time.monotonic() + timeout
        while True:
            pending = self._unconsumed()
            if any(marker in pending for marker in markers):
                return self._consume()
            if time.monotonic() >= deadline:
                raise SessionTimeout(f"{self.character.name}: none of {markers!r} within {timeout}s; pending text:\n{pending[-1500:]}")
            self._pump()

    def drain(self, timeout: float = 0.5) -> str:
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            self._pump()
        return self._consume()

    def send_line(self, line: str) -> None:
        self._socket.sendall(line.encode("latin-1", errors="replace") + b"\n")

    def command(self, text: str, timeout: float = 8.0) -> Transcript:
        self.drain(0.1)
        self.send_line(text)
        deadline = time.monotonic() + timeout
        while True:
            pending = self._unconsumed()
            if ends_with_prompt(pending):
                return Transcript(self._consume())
            if time.monotonic() >= deadline:
                raise SessionTimeout(f"{self.character.name}: no prompt after {text!r} within {timeout}s; pending text:\n{pending[-1500:]}")
            self._pump()

    def login(self) -> None:
        self.expect([LOGIN_EMAIL_PROMPT], 15.0)
        self.send_line(fixtures.HARNESS_EMAIL)
        self.expect([LOGIN_PASSWORD_PROMPT])
        self.send_line(fixtures.HARNESS_PASSWORD)
        self.expect([ACCOUNT_MENU_PROMPT])
        self.send_line("2")
        self.expect([CHARACTER_NUMBER_PROMPT])
        self.send_line(str(self._character_number))
        self.expect([CHARACTER_MENU_PROMPT])
        self.send_line("1")
        self.expect([ENTER_GAME_MARKER], 15.0)
        self.command("look")

    def cast(self, spell: str, target: str | None = None, success_markers: tuple[str, ...] = (), attempts: int = 6, timeout: float = 12.0) -> Transcript:
        words = f"cast '{spell}'" + (f" {target}" if target else "")
        last = Transcript("")
        self.drain(0.1)
        for _attempt in range(attempts):
            self.send_line(words)
            deadline = time.monotonic() + timeout
            while time.monotonic() < deadline:
                self._pump()
                pending = self._unconsumed()
                if success_markers and any(marker in pending for marker in success_markers):
                    return Transcript(self._consume())
                if not success_markers and ends_with_prompt(pending):
                    return Transcript(self._consume())
            last = Transcript(self._consume())
        raise SessionTimeout(f"{self.character.name}: {words!r} never produced {success_markers!r} in {attempts} attempts; last transcript:\n{last.text[-1500:]}")

    def quit(self) -> None:
        self.send_line("quit")
        try:
            self.expect(["Goodbye", "As you quit"], 8.0)
        finally:
            self.close()

    def drop_link(self) -> None:
        self.close()

    def close(self) -> None:
        try:
            self._socket.close()
        except OSError:
            pass
