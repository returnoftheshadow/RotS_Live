"""Telnet parsing and encoding for harness sessions.

It parses a telnet byte stream into text and command events, and encodes telnet commands to send.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Optional, Union

# Interpret As Command: starts every telnet command, and doubled stands for a data byte 255.
IAC = 255
# The command offering to enable an option; the option byte follows.
WILL = 251
# The command refusing an option; the option byte follows.
WONT = 252
# The command asking the other side to enable an option; the option byte follows.
DO = 253
# The command asking the other side to disable an option; the option byte follows.
DONT = 254
# Starts a subnegotiation, which IAC SE ends.
SB = 250
# Ends a subnegotiation.
SE = 240
# Carriage return.
CR = 13
# The byte that follows CR to make a bare carriage return.
NUL = 0

# Echo: the server sends WILL ECHO to hide a typed password.
OPTION_ECHO = 1
# Terminal type.
OPTION_TTYPE = 24
# Negotiate About Window Size.
OPTION_NAWS = 31
# Character set.
OPTION_CHARSET = 42
# MUD Server Data Protocol.
OPTION_MSDP = 69
# MUD Server Status Protocol.
OPTION_MSSP = 70
# MUD Client Compression Protocol, version 2.
OPTION_MCCP2 = 86
# MUD Sound Protocol.
OPTION_MSP = 90
# MUD eXtension Protocol.
OPTION_MXP = 91
# Achaea Telnet Client Protocol.
OPTION_ATCP = 200

# The negotiation commands, which each take one option byte.
NEGOTIATION_COMMANDS = (WILL, WONT, DO, DONT)

# Names describe_event prints for the negotiation commands.
COMMAND_NAMES = {WILL: "WILL", WONT: "WONT", DO: "DO", DONT: "DONT"}

# Names describe_event prints for the options above; any other option prints as its number.
OPTION_NAMES = {
    OPTION_ECHO: "ECHO",
    OPTION_TTYPE: "TTYPE",
    OPTION_NAWS: "NAWS",
    OPTION_CHARSET: "CHARSET",
    OPTION_MSDP: "MSDP",
    OPTION_MSSP: "MSSP",
    OPTION_MCCP2: "MCCP2",
    OPTION_MSP: "MSP",
    OPTION_MXP: "MXP",
    OPTION_ATCP: "ATCP",
}

# The most characters of text describe_event shows.
DESCRIBED_TEXT_LIMIT = 60


@dataclass(frozen=True)
class TelnetText:
    """Text bytes, with CR NUL already reduced to CR and IAC IAC to one 0xFF."""

    # The text bytes.
    data: bytes


@dataclass(frozen=True)
class TelnetNegotiation:
    """IAC followed by WILL, WONT, DO or DONT and an option."""

    # WILL, WONT, DO or DONT.
    command: int
    # The option negotiated.
    option: int


@dataclass(frozen=True)
class TelnetSubnegotiation:
    """IAC SB, an option and its payload, up to IAC SE."""

    # The option the payload belongs to.
    option: int
    # The payload with IAC IAC reduced to one 0xFF.
    payload: bytes


@dataclass(frozen=True)
class TelnetCommand:
    """IAC followed by a command byte that takes no option, such as GA (249)."""

    # The command byte.
    command: int


# One thing a telnet stream carries.
TelnetEvent = Union[TelnetText, TelnetNegotiation, TelnetSubnegotiation, TelnetCommand]


class TelnetStreamParser:
    """Turns a telnet byte stream fed in chunks into events; a sequence may be split anywhere."""

    def __init__(self) -> None:
        # The last byte was IAC outside a subnegotiation.
        self._pending_iac = False
        # The negotiation command waiting for its option byte, or None.
        self._pending_negotiation: Optional[int] = None
        # Inside IAC SB ... IAC SE.
        self._in_subnegotiation = False
        # The last byte inside the subnegotiation was IAC.
        self._subnegotiation_pending_iac = False
        # The subnegotiation's option byte and payload so far, IAC IAC already reduced.
        self._subnegotiation_bytes = bytearray()
        # A CR waits for the next byte so a following NUL can be dropped.
        self._pending_cr = False
        # Text bytes of the current feed() not yet closed into a TelnetText.
        self._text = bytearray()
        # Events of the current feed().
        self._events: list[TelnetEvent] = []

    def feed(self, chunk: bytes) -> list[TelnetEvent]:
        """Add a chunk and return the events it completed.

        Adjacent text comes back as one TelnetText.
        """
        for byte in chunk:
            self._feed_byte(byte)
        self._flush_text()
        events = self._events
        self._events = []
        return events

    def _feed_byte(self, byte: int) -> None:
        # CR NUL is a bare carriage return: keep the CR and drop the NUL.
        if self._pending_cr:
            self._text.append(CR)
            self._pending_cr = False
            if byte == NUL:
                return

        if self._in_subnegotiation:
            self._feed_subnegotiation_byte(byte)
            return

        if self._pending_negotiation is not None:
            self._emit(TelnetNegotiation(self._pending_negotiation, byte))
            self._pending_negotiation = None
            return

        if self._pending_iac:
            self._pending_iac = False
            if byte == IAC:
                self._text.append(IAC)
            elif byte in NEGOTIATION_COMMANDS:
                self._pending_negotiation = byte
            elif byte == SB:
                self._in_subnegotiation = True
                self._subnegotiation_pending_iac = False
                self._subnegotiation_bytes = bytearray()
            else:
                self._emit(TelnetCommand(byte))
            return

        if byte == IAC:
            self._pending_iac = True
        elif byte == CR:
            self._pending_cr = True
        else:
            self._text.append(byte)

    def _feed_subnegotiation_byte(self, byte: int) -> None:
        if not self._subnegotiation_pending_iac:
            if byte == IAC:
                self._subnegotiation_pending_iac = True
            else:
                self._subnegotiation_bytes.append(byte)
            return

        self._subnegotiation_pending_iac = False
        if byte == SE:
            self._in_subnegotiation = False
            # With no option byte there is nothing to report.
            if self._subnegotiation_bytes:
                option = self._subnegotiation_bytes[0]
                payload = bytes(self._subnegotiation_bytes[1:])
                self._emit(TelnetSubnegotiation(option, payload))
            return

        self._subnegotiation_bytes.append(IAC)
        # IAC IAC is one 0xFF; IAC before any other byte keeps both, so a malformed payload is not
        # lost.
        if byte != IAC:
            self._subnegotiation_bytes.append(byte)

    def _emit(self, event: TelnetEvent) -> None:
        self._flush_text()
        self._events.append(event)

    def _flush_text(self) -> None:
        if self._text:
            self._events.append(TelnetText(bytes(self._text)))
            self._text = bytearray()


def escape_iac(data: bytes) -> bytes:
    """Double every 0xFF so the receiver reads it as data, not as IAC."""
    return data.replace(bytes([IAC]), bytes([IAC, IAC]))


def negotiation_bytes(command: int, option: int) -> bytes:
    """Return IAC, the command (WILL, WONT, DO or DONT) and the option."""
    return bytes([IAC, command, option])


def subnegotiation_bytes(option: int, payload: bytes) -> bytes:
    """Return IAC SB, the option, the payload with 0xFF doubled, then IAC SE."""
    return bytes([IAC, SB, option]) + escape_iac(payload) + bytes([IAC, SE])


def encode_event(event: TelnetEvent) -> bytes:
    """Return the wire bytes for an event; text has 0xFF doubled and is otherwise sent as it is."""
    if isinstance(event, TelnetText):
        return escape_iac(event.data)
    if isinstance(event, TelnetNegotiation):
        return negotiation_bytes(event.command, event.option)
    if isinstance(event, TelnetSubnegotiation):
        return subnegotiation_bytes(event.option, event.payload)
    return bytes([IAC, event.command])


def describe_event(event: TelnetEvent) -> str:
    """Return a one-line description of an event, naming the commands and options listed above."""
    if isinstance(event, TelnetText):
        shown = event.data.decode("latin1")[:DESCRIBED_TEXT_LIMIT]
        return f"text {len(event.data)} bytes {shown!r}"
    if isinstance(event, TelnetNegotiation):
        command_name = COMMAND_NAMES.get(event.command, str(event.command))
        return f"{command_name} {_option_name(event.option)}"
    if isinstance(event, TelnetSubnegotiation):
        return f"SB {_option_name(event.option)} {event.payload!r}"
    return f"IAC {event.command}"


def _option_name(option: int) -> str:
    return OPTION_NAMES.get(option, str(option))


class TelnetStreamSanitizer:
    """The text of a telnet byte stream fed in chunks, with every telnet command dropped.

    It is the text-only view of the stream, for callers that want text without events.
    """

    def __init__(self) -> None:
        # Parser state survives between feed() calls, so a sequence split across chunks is still
        # dropped.
        self._parser = TelnetStreamParser()
        # Every text byte kept so far.
        self._sanitized = bytearray()
        # Bytes of _sanitized already returned by take_new_text().
        self._taken = 0

    def feed(self, chunk: bytes) -> str:
        """Add a chunk and return all text so far."""
        for event in self._parser.feed(chunk):
            if isinstance(event, TelnetText):
                self._sanitized.extend(event.data)

        return self.text

    @property
    def text(self) -> str:
        """Return all text kept so far, decoded as Latin-1."""
        return self._sanitized.decode("latin1", errors="ignore")

    def take_new_text(self) -> str:
        """Return the text kept since the last call, decoded as Latin-1."""
        fresh = self._sanitized[self._taken:]
        self._taken = len(self._sanitized)
        return fresh.decode("latin1", errors="ignore")
