"""Turns a telnet byte stream into the text a player would read, dropping option negotiation."""

from __future__ import annotations

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


class TelnetStreamSanitizer:
    """Strips telnet negotiation from a byte stream fed in chunks, keeping the text."""

    def __init__(self) -> None:
        # Whether the last byte was an IAC outside a subnegotiation. Like the four flags below, it
        # survives between feed() calls, so a sequence split across chunks is still dropped.
        self._pending_iac = False
        # Whether an option byte after WILL, WONT, DO or DONT is still to come.
        self._pending_option = False
        # Whether the stream is inside a subnegotiation, between IAC SB and IAC SE.
        self._in_subnegotiation = False
        # Whether the last byte inside a subnegotiation was an IAC.
        self._subnegotiation_pending_iac = False
        # Whether a CR is held back until the next byte shows whether a NUL follows it.
        self._pending_cr = False
        # Every text byte kept so far.
        self._sanitized = bytearray()
        # Bytes of _sanitized already returned by take_new_text().
        self._taken = 0

    def feed(self, chunk: bytes) -> str:
        """Add a chunk and return all text so far."""
        for byte in chunk:
            # CR NUL is a bare carriage return: keep the CR and drop the NUL.
            if self._pending_cr:
                self._sanitized.append(CR)
                self._pending_cr = False
                if byte == NUL:
                    continue

            if self._in_subnegotiation:
                if self._subnegotiation_pending_iac:
                    if byte == SE:
                        self._in_subnegotiation = False
                    self._subnegotiation_pending_iac = False
                    continue

                if byte == IAC:
                    self._subnegotiation_pending_iac = True
                continue

            if self._pending_option:
                self._pending_option = False
                continue

            if self._pending_iac:
                self._pending_iac = False
                if byte == IAC:
                    self._sanitized.append(IAC)
                    continue
                if byte in (WILL, WONT, DO, DONT):
                    self._pending_option = True
                    continue
                if byte == SB:
                    self._in_subnegotiation = True
                    self._subnegotiation_pending_iac = False
                    continue
                continue

            if byte == IAC:
                self._pending_iac = True
                continue

            if byte == CR:
                self._pending_cr = True
                continue

            self._sanitized.append(byte)

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
