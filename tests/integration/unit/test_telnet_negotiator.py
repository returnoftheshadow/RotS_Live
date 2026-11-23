"""Unit tests for rots_harness.telnet_negotiator, fed the server's events directly."""

from __future__ import annotations

from rots_harness.telnet import (
    DO,
    DONT,
    OPTION_ATCP,
    OPTION_CHARSET,
    OPTION_ECHO,
    OPTION_MSDP,
    OPTION_MSP,
    OPTION_MSSP,
    OPTION_MXP,
    OPTION_NAWS,
    OPTION_TTYPE,
    WILL,
    WONT,
    TelnetEvent,
    TelnetNegotiation,
    TelnetSubnegotiation,
)
from rots_harness.telnet_negotiator import TelnetNegotiator
from rots_harness.telnet_profile import (
    MSDP_CLIENT,
    REFUSING_CLIENT,
    SILENT_CLIENT,
    TelnetClientProfile,
)

# The server's TTYPE SEND request.
TTYPE_SEND_REQUEST = TelnetSubnegotiation(OPTION_TTYPE, b"\x01")

# The CHARSET request RotS sends: REQUEST, a space separator, then one character set.
LATIN1_CHARSET_REQUEST = TelnetSubnegotiation(OPTION_CHARSET, b"\x01 ISO-8859-1")


def answers(negotiator: TelnetNegotiator, *events: TelnetEvent) -> list[TelnetEvent]:
    """Return every reply the negotiator gives to events, in order."""
    replies: list[TelnetEvent] = []
    for event in events:
        replies.extend(negotiator.respond(event))
    return replies


def test_a_silent_client_never_answers() -> None:
    negotiator = TelnetNegotiator(SILENT_CLIENT)
    assert answers(negotiator, TelnetNegotiation(DO, OPTION_TTYPE)) == []
    assert answers(negotiator, TelnetNegotiation(WILL, OPTION_MSDP)) == []
    assert answers(negotiator, TTYPE_SEND_REQUEST) == []
    assert answers(negotiator, TelnetNegotiation(WILL, OPTION_ECHO)) == []


def test_an_msdp_client_answers_the_server_offers_in_order() -> None:
    negotiator = TelnetNegotiator(MSDP_CLIENT)
    replies = answers(
        negotiator,
        TelnetNegotiation(DO, OPTION_TTYPE),
        TTYPE_SEND_REQUEST,
        TelnetNegotiation(DO, OPTION_NAWS),
        TelnetNegotiation(DO, OPTION_CHARSET),
        TelnetNegotiation(WILL, OPTION_MSDP),
        TelnetNegotiation(WILL, OPTION_MSSP),
        TelnetNegotiation(DO, OPTION_ATCP),
        TelnetNegotiation(WILL, OPTION_MSP),
        TelnetNegotiation(DO, OPTION_MXP),
        LATIN1_CHARSET_REQUEST,
    )
    assert replies == [
        TelnetNegotiation(WILL, OPTION_TTYPE),
        TelnetSubnegotiation(OPTION_TTYPE, b"\x00ROTS-HARNESS"),
        TelnetNegotiation(WILL, OPTION_NAWS),
        TelnetSubnegotiation(OPTION_NAWS, b"\x00\x50\x00\x18"),
        TelnetNegotiation(WILL, OPTION_CHARSET),
        TelnetNegotiation(DO, OPTION_MSDP),
        TelnetNegotiation(DONT, OPTION_MSSP),
        TelnetNegotiation(WONT, OPTION_ATCP),
        TelnetNegotiation(DONT, OPTION_MSP),
        TelnetNegotiation(WONT, OPTION_MXP),
        TelnetSubnegotiation(OPTION_CHARSET, b"\x02ISO-8859-1"),
    ]


def test_the_ttype_cycle_repeats_the_last_name() -> None:
    negotiator = TelnetNegotiator(MSDP_CLIENT)
    answers(negotiator, TelnetNegotiation(DO, OPTION_TTYPE))
    replies = answers(negotiator, *([TTYPE_SEND_REQUEST] * 4))
    assert replies == [
        TelnetSubnegotiation(OPTION_TTYPE, b"\x00ROTS-HARNESS"),
        TelnetSubnegotiation(OPTION_TTYPE, b"\x00XTERM-256COLOR"),
        TelnetSubnegotiation(OPTION_TTYPE, b"\x00MTTS 9"),
        TelnetSubnegotiation(OPTION_TTYPE, b"\x00MTTS 9"),
    ]


def test_ttype_is_not_answered_before_ttype_is_agreed() -> None:
    negotiator = TelnetNegotiator(MSDP_CLIENT)
    assert answers(negotiator, TTYPE_SEND_REQUEST) == []
    assert answers(negotiator, TelnetNegotiation(DO, OPTION_TTYPE), TTYPE_SEND_REQUEST) == [
        TelnetNegotiation(WILL, OPTION_TTYPE),
        TelnetSubnegotiation(OPTION_TTYPE, b"\x00ROTS-HARNESS"),
    ]


def test_a_refusing_client_refuses_each_offer_once_and_ignores_the_servers_follow_ups() -> None:
    negotiator = TelnetNegotiator(REFUSING_CLIENT)
    assert answers(negotiator, TelnetNegotiation(DO, OPTION_TTYPE)) == [TelnetNegotiation(WONT, OPTION_TTYPE)]
    assert answers(negotiator, TelnetNegotiation(DO, OPTION_NAWS)) == [TelnetNegotiation(WONT, OPTION_NAWS)]
    assert answers(negotiator, TelnetNegotiation(WILL, OPTION_MSDP)) == [TelnetNegotiation(DONT, OPTION_MSDP)]
    assert answers(negotiator, TelnetNegotiation(WONT, OPTION_MSDP)) == []
    assert answers(negotiator, TelnetNegotiation(DO, OPTION_MXP)) == [TelnetNegotiation(WONT, OPTION_MXP)]
    assert answers(negotiator, TelnetNegotiation(WILL, OPTION_MXP)) == [TelnetNegotiation(DONT, OPTION_MXP)]
    assert answers(negotiator, TelnetNegotiation(DONT, OPTION_MXP)) == []


def test_the_password_echo_sequence_ends_without_a_loop() -> None:
    negotiator = TelnetNegotiator(MSDP_CLIENT)
    assert answers(negotiator, TelnetNegotiation(WILL, OPTION_ECHO)) == [TelnetNegotiation(DO, OPTION_ECHO)]
    assert answers(negotiator, TelnetNegotiation(WILL, OPTION_ECHO)) == []
    assert answers(negotiator, TelnetNegotiation(WONT, OPTION_ECHO)) == [TelnetNegotiation(DONT, OPTION_ECHO)]
    assert answers(negotiator, TelnetNegotiation(WONT, OPTION_ECHO)) == []
    assert answers(negotiator, TelnetNegotiation(WILL, OPTION_ECHO)) == [TelnetNegotiation(DO, OPTION_ECHO)]


def test_charset_is_rejected_when_no_profile_charset_is_offered() -> None:
    profile = TelnetClientProfile(
        name="utf8",
        answers_negotiation=True,
        accepted_options=frozenset({OPTION_CHARSET}),
        charsets=("UTF-8",),
    )
    negotiator = TelnetNegotiator(profile)
    assert answers(negotiator, TelnetNegotiation(DO, OPTION_CHARSET)) == [TelnetNegotiation(WILL, OPTION_CHARSET)]
    assert answers(negotiator, LATIN1_CHARSET_REQUEST) == [TelnetSubnegotiation(OPTION_CHARSET, b"\x03")]
