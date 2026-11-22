"""Fake game connections the harness's unit tests share: a session that records its calls, and a
socket that stands in for the server."""

from __future__ import annotations

import socket
from collections.abc import Mapping, Sequence

from rots_harness.session import SessionTimeout, Transcript


class RecordingSession:
    """Stands in for a GameSession: records each call, in order, and answers from fixed text."""

    def __init__(
        self,
        expect_replies: Sequence[str] = (),
        drained_text: str = "",
        *,
        command_replies: Mapping[str, str] | None = None,
        time_out_commands: bool = False,
    ) -> None:
        """Answer the coming expects with expect_replies in turn, and each drain with drained_text.

        An expect past the last of expect_replies raises SessionTimeout. command answers each line
        with command_replies[line], or "" when it has none, and raises SessionTimeout instead when
        time_out_commands.
        """
        # Each call as (method name, arguments), in the order they were made.
        self.calls: list[tuple] = []
        # The answers the coming expects give, first first.
        self._expect_replies = list(expect_replies)
        # What every drain returns.
        self._drained_text = drained_text
        # The reply text for each command line, as sent.
        self._command_replies = command_replies if command_replies is not None else {}
        # Whether command raises SessionTimeout instead of answering.
        self._time_out_commands = time_out_commands

    @property
    def commands(self) -> list[str]:
        """Return each line sent through command, in order."""
        return [call[1] for call in self.calls if call[0] == "command"]

    def send_line(self, line: str) -> None:
        """Record the line."""
        self.calls.append(("send_line", line))

    def expect(self, markers: Sequence[str], **keywords: float) -> str:
        """Record the markers and any keyword (timeout), then give the next reply or time out."""
        marker_list = list(markers)
        self.calls.append(("expect", marker_list, keywords))
        if not self._expect_replies:
            raise SessionTimeout("none of the markers arrived")

        return self._expect_replies.pop(0)

    def drain(self, timeout: float) -> str:
        """Record the settle time and return the drained text."""
        self.calls.append(("drain", timeout))
        return self._drained_text

    def command(self, text: str) -> Transcript:
        """Record the command, then answer it from the table or time out."""
        self.calls.append(("command", text))
        if self._time_out_commands:
            raise SessionTimeout("no prompt")

        reply_text = self._command_replies.get(text, "")
        return Transcript(reply_text)


class FakeSocket:
    """Stands in for the server connection.

    replies maps a line the session sends to the answers for its first, second, ... send; a line
    with no answer left is never answered. An answer given as a tuple of chunks arrives one chunk
    per recv(). greeting is what the server sends on connect.
    """

    def __init__(
        self,
        replies: Mapping[str, Sequence[bytes | tuple[bytes, ...]]],
        greeting: bytes = b"",
        *,
        closed_by_server: bool = False,
    ) -> None:
        """Answer from replies after greeting.

        closed_by_server makes recv report a closed connection once nothing is pending.
        """
        # Answers still to give per sent line, in send order; copied so the caller's lists stay
        # intact.
        self._replies = {line: list(answers) for line, answers in replies.items()}
        # Chunks the coming recv() calls return, one each; when empty, recv() times out or reports
        # the close.
        self._pending: list[bytes] = [greeting] if greeting else []
        # Whether recv returns b"" once nothing is pending, as a socket the server closed does.
        self._closed_by_server = closed_by_server
        # Whether the session has closed the connection.
        self.closed = False
        # Every line the session sent, in order and without the newline.
        self.sent_lines: list[str] = []

    def settimeout(self, _timeout: float) -> None:
        """Ignore the timeout: recv answers at once."""

    def sendall(self, data: bytes) -> None:
        """Record the line and queue its next answer."""
        line = data.decode("latin-1").strip()
        self.sent_lines.append(line)
        answers = self._replies.get(line, [])
        if answers:
            answer = answers.pop(0)
            chunks = answer if isinstance(answer, tuple) else [answer]
            self._pending.extend(chunks)

    def recv(self, _size: int) -> bytes:
        """Return the next pending chunk, b"" for a server-closed connection, or time out."""
        if self._pending:
            return self._pending.pop(0)

        if self._closed_by_server:
            return b""

        raise socket.timeout()

    def close(self) -> None:
        """Record that the session closed the connection."""
        self.closed = True
