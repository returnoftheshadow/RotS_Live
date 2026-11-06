"""Turns a telnet byte stream into the text a player would read, dropping option negotiation."""

from __future__ import annotations

IAC = 255
WILL = 251
WONT = 252
DO = 253
DONT = 254
SB = 250
SE = 240
CR = 13
NUL = 0


class TelnetStreamSanitizer:
    """Strips telnet negotiation from a byte stream fed in chunks, keeping the text."""

    def __init__(self) -> None:
        # Parser state survives between feed() calls so a sequence split across chunks is still dropped.
        self._pending_iac = False
        self._pending_option = False
        self._in_subnegotiation = False
        self._subnegotiation_pending_iac = False
        self._pending_cr = False
        # Every text byte kept so far.
        self._sanitized = bytearray()
        # Bytes of _sanitized already returned by take_new_text().
        self._taken = 0

    def feed(self, chunk: bytes) -> str:
        """Adds a chunk and returns all text so far."""
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
        """All text kept so far, decoded as Latin-1."""
        return self._sanitized.decode("latin1", errors="ignore")

    def take_new_text(self) -> str:
        """The text kept since the last call, decoded as Latin-1."""
        fresh = self._sanitized[self._taken:]
        self._taken = len(self._sanitized)
        return fresh.decode("latin1", errors="ignore")
