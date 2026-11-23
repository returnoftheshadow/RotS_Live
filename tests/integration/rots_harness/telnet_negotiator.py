"""Answer the server's telnet option negotiation for one connection, as a client profile directs."""

from __future__ import annotations

from rots_harness.telnet import (
    DO,
    DONT,
    OPTION_CHARSET,
    OPTION_NAWS,
    OPTION_TTYPE,
    WILL,
    WONT,
    TelnetEvent,
    TelnetNegotiation,
    TelnetSubnegotiation,
)
from rots_harness.telnet_payloads import (
    CHARSET_ACCEPTED,
    CHARSET_REJECTED,
    CHARSET_REQUEST,
    TTYPE_IS,
    TTYPE_SEND,
    decode_charset_request,
    encode_naws,
)
from rots_harness.telnet_profile import TelnetClientProfile


class TelnetNegotiator:
    """Answers the server's negotiation for one connection according to a TelnetClientProfile.

    It follows the loop-free rule of RFC 1143: it replies to WILL, WONT, DO or DONT only when the
    option's state changes, or to refuse an option the profile does not accept, so the server's
    repeated offers (such as the extra WILL ECHO at a password prompt) get no second answer.
    Every option starts off on both sides.
    """

    def __init__(self, profile: TelnetClientProfile) -> None:
        # The profile deciding which options to accept and what the subnegotiations report.
        self._profile = profile
        # Options the server has agreed to use (its WILL accepted with our DO); absent means off.
        self._server_options: dict[int, bool] = {}
        # Options we have agreed to use (the server's DO accepted with our WILL); absent means off.
        self._client_options: dict[int, bool] = {}
        # The index into the profile's terminal types of the next TTYPE IS answer.
        self._terminal_type_index = 0

    def respond(self, event: TelnetEvent) -> list[TelnetEvent]:
        """Return the events to send in reply to one received event, in order; often none.

        A CHARSET REQUEST that does not follow its grammar raises TelnetPayloadError, so a test
        sees the protocol fault instead of a silent non-answer.
        """
        if not self._profile.answers_negotiation:
            return []

        if isinstance(event, TelnetNegotiation):
            return self._respond_to_negotiation(event.command, event.option)

        if isinstance(event, TelnetSubnegotiation):
            return self._respond_to_subnegotiation(event.option, event.payload)
        return []

    def _respond_to_negotiation(self, command: int, option: int) -> list[TelnetEvent]:
        if command == WILL:
            return self._respond_to_enable(self._server_options, option, DO, DONT)

        if command == DO:
            replies = self._respond_to_enable(self._client_options, option, WILL, WONT)
            if option == OPTION_NAWS and replies == [TelnetNegotiation(WILL, OPTION_NAWS)]:
                width, height = self._profile.window_size
                window_payload = encode_naws(width, height)
                replies.append(TelnetSubnegotiation(OPTION_NAWS, window_payload))

            return replies

        if command == WONT:
            return self._respond_to_disable(self._server_options, option, DONT)

        if command == DONT:
            return self._respond_to_disable(self._client_options, option, WONT)
        return []

    def _respond_to_enable(
        self, options: dict[int, bool], option: int, agree: int, refuse: int
    ) -> list[TelnetEvent]:
        if not self._profile.accepts(option):
            return [TelnetNegotiation(refuse, option)]

        if options.get(option, False):
            return []

        options[option] = True
        return [TelnetNegotiation(agree, option)]

    def _respond_to_disable(
        self, options: dict[int, bool], option: int, acknowledge: int
    ) -> list[TelnetEvent]:
        if not options.get(option, False):
            return []

        options[option] = False
        return [TelnetNegotiation(acknowledge, option)]

    def _respond_to_subnegotiation(self, option: int, payload: bytes) -> list[TelnetEvent]:
        if not self._client_options.get(option, False) or not payload:
            return []

        if option == OPTION_TTYPE and payload == bytes([TTYPE_SEND]):
            terminal_type_payload = self._next_terminal_type()
            return [TelnetSubnegotiation(OPTION_TTYPE, terminal_type_payload)]

        if option == OPTION_CHARSET and payload[0] == CHARSET_REQUEST:
            charset_payload = self._charset_answer(payload)
            return [TelnetSubnegotiation(OPTION_CHARSET, charset_payload)]

        return []

    def _next_terminal_type(self) -> bytes:
        """Return TTYPE IS and the next terminal type.

        Once the list runs out, the last one repeats.
        """
        terminal_types = self._profile.terminal_types
        last_index = len(terminal_types) - 1
        name = terminal_types[min(self._terminal_type_index, last_index)]
        self._terminal_type_index += 1
        return bytes([TTYPE_IS]) + name.encode("latin1")

    def _charset_answer(self, payload: bytes) -> bytes:
        """Return ACCEPTED and the profile's most preferred character set on offer, else REJECTED.

        Names are compared exactly as sent.
        """
        offered = decode_charset_request(payload)
        for charset in self._profile.charsets:
            if charset in offered:
                return bytes([CHARSET_ACCEPTED]) + charset.encode("latin1")

        return bytes([CHARSET_REJECTED])
