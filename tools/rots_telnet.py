"""Telnet-level primitives shared by tools/account_smoke.py and tests/integration.

Everything here is protocol plumbing: stripping IAC negotiation, waiting for prompt
markers, sending lines. No game knowledge lives in this module.
"""

from __future__ import annotations

import socket
import time

IAC = 255


class TelnetStreamSanitizer:
    def __init__(self) -> None:
        self._pending_iac = False
        self._pending_option = False
        self._in_subnegotiation = False
        self._subnegotiation_pending_iac = False
        self._pending_cr = False
        self._sanitized = bytearray()

    def feed(self, chunk: bytes) -> str:
        for byte in chunk:
            if self._pending_cr:
                if byte != 0:
                    self._sanitized.append(13)
                self._pending_cr = False
                if byte == 0:
                    continue

            if self._in_subnegotiation:
                if self._subnegotiation_pending_iac:
                    if byte == 240:
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
                if byte in (251, 252, 253, 254):
                    self._pending_option = True
                    continue
                if byte == 250:
                    self._in_subnegotiation = True
                    self._subnegotiation_pending_iac = False
                    continue
                continue

            if byte == IAC:
                self._pending_iac = True
                continue

            if byte == 13:
                self._pending_cr = True
                continue

            self._sanitized.append(byte)

        return self.text

    @property
    def text(self) -> str:
        return self._sanitized.decode("latin1", errors="ignore")


def find_first_marker_end(text: str, markers: list[str]) -> int | None:
    matched_end = None
    matched_start = None
    for marker in markers:
        index = text.find(marker)
        if index < 0:
            continue
        marker_end = index + len(marker)
        if matched_start is None or index < matched_start or (index == matched_start and marker_end < matched_end):
            matched_start = index
            matched_end = marker_end
    return matched_end


class BufferedPromptReader:
    def __init__(self, sock: socket.socket) -> None:
        self._sock = sock
        self._sanitizer = TelnetStreamSanitizer()
        self._buffer = ""

    def recv_until(self, markers: list[str], timeout_seconds: float) -> str:
        deadline = time.time() + timeout_seconds
        raw_data = bytearray()
        self._sock.settimeout(0.5)

        while time.time() < deadline:
            marker_end = find_first_marker_end(self._buffer, markers)
            if marker_end is not None:
                text = self._buffer[:marker_end]
                self._buffer = self._buffer[marker_end:]
                return text

            try:
                chunk = self._sock.recv(4096)
            except socket.timeout:
                continue

            if not chunk:
                break

            raw_data.extend(chunk)
            previous_length = len(self._sanitizer.text)
            sanitized_text = self._sanitizer.feed(chunk)
            self._buffer += sanitized_text[previous_length:]
            marker_end = find_first_marker_end(self._buffer, markers)
            if marker_end is not None:
                text = self._buffer[:marker_end]
                self._buffer = self._buffer[marker_end:]
                return text

        marker_end = find_first_marker_end(self._buffer, markers)
        if marker_end is not None:
            text = self._buffer[:marker_end]
            self._buffer = self._buffer[marker_end:]
            return text

        text = self._buffer
        raw_tail = bytes(raw_data[-800:]).decode("latin1", errors="ignore")
        raise RuntimeError(
            "Timed out waiting for markers "
            + ", ".join(markers)
            + ". Last sanitized output was:\n"
            + text[-800:]
            + "\nRaw tail was:\n"
            + raw_tail
        )


def recv_until(sock: socket.socket, markers: list[str], timeout_seconds: float) -> str:
    deadline = time.time() + timeout_seconds
    raw_data = bytearray()
    sanitizer = TelnetStreamSanitizer()
    sock.settimeout(0.5)

    while time.time() < deadline:
        try:
            chunk = sock.recv(4096)
        except socket.timeout:
            continue

        if not chunk:
            break

        raw_data.extend(chunk)
        text = sanitizer.feed(chunk)
        if any(marker in text for marker in markers):
            return text

    text = sanitizer.text
    raw_tail = bytes(raw_data[-800:]).decode("latin1", errors="ignore")
    raise RuntimeError(
        "Timed out waiting for markers "
        + ", ".join(markers)
        + ". Last sanitized output was:\n"
        + text[-800:]
        + "\nRaw tail was:\n"
        + raw_tail
    )


def contains_any_marker(text: str, markers: list[str]) -> bool:
    return any(marker in text for marker in markers)


def require_markers(text: str, markers: list[str], context: str) -> str:
    missing_markers = [marker for marker in markers if marker not in text]
    if missing_markers:
        raise RuntimeError(
            f"{context} was missing expected markers: {', '.join(missing_markers)}. Full output was:\n{text[-800:]}"
        )
    return text


def send_line(sock: socket.socket, line: str) -> None:
    sock.sendall(line.encode("utf-8") + b"\n")
