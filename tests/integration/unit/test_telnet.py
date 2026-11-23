from __future__ import annotations

from rots_harness.telnet import (
    DONT,
    OPTION_ECHO,
    OPTION_MSDP,
    OPTION_MXP,
    OPTION_NAWS,
    OPTION_TTYPE,
    TelnetCommand,
    TelnetEvent,
    TelnetNegotiation,
    TelnetStreamParser,
    TelnetStreamSanitizer,
    TelnetSubnegotiation,
    TelnetText,
    describe_event,
    encode_event,
    escape_iac,
    subnegotiation_bytes,
)

# Interpret As Command: starts every telnet command.
IAC = 255
# The command offering to enable an option.
WILL = 251
# The command asking the other side to enable an option.
DO = 253
# Starts a subnegotiation.
SB = 250
# Ends a subnegotiation.
SE = 240
# The echo option.
ECHO = 1
# The Mud Server Data Protocol option.
MSDP = 69


def test_plain_text_passes_through() -> None:
    assert TelnetStreamSanitizer().feed(b"Account email: ") == "Account email: "


def test_option_negotiation_is_dropped() -> None:
    sanitizer = TelnetStreamSanitizer()
    assert sanitizer.feed(bytes([IAC, WILL, ECHO]) + b"pass" + bytes([IAC, DO, MSDP])) == "pass"


def test_subnegotiation_is_dropped_including_its_payload() -> None:
    sanitizer = TelnetStreamSanitizer()
    assert sanitizer.feed(bytes([IAC, SB, MSDP, 1]) + b"VAR" + bytes([IAC, SE]) + b"text") == "text"


def test_a_doubled_iac_is_a_literal_255() -> None:
    assert TelnetStreamSanitizer().feed(bytes([IAC, IAC])) == "\xff"


def test_carriage_return_nul_becomes_a_carriage_return_and_crlf_is_kept() -> None:
    sanitizer = TelnetStreamSanitizer()
    assert sanitizer.feed(b"a\r\x00b\r\nc") == "a\rb\r\nc"


def test_a_sequence_split_across_chunks_is_still_dropped() -> None:
    sanitizer = TelnetStreamSanitizer()
    sanitizer.feed(bytes([IAC]))
    sanitizer.feed(bytes([WILL]))
    assert sanitizer.feed(bytes([ECHO]) + b"ok") == "ok"


def test_text_accumulates_across_feeds() -> None:
    sanitizer = TelnetStreamSanitizer()
    sanitizer.feed(b"one ")
    sanitizer.feed(b"two")
    assert sanitizer.text == "one two"


def test_take_new_text_returns_only_what_arrived_since_the_last_take() -> None:
    sanitizer = TelnetStreamSanitizer()
    sanitizer.feed(b"one ")
    assert sanitizer.take_new_text() == "one "
    sanitizer.feed(b"two\r")
    assert sanitizer.take_new_text() == "two", "a CR waits for the byte after it"
    sanitizer.feed(b"\x00three")
    assert sanitizer.take_new_text() == "\rthree"
    assert sanitizer.text == "one two\rthree"


# A stream with text, a negotiation, a subnegotiation, a bare command and a CR NUL.
MIXED_STREAM = (
    b"hi"
    + bytes([IAC, WILL, OPTION_MSDP, IAC, SB, OPTION_TTYPE, 1, IAC, SE, IAC, 249])
    + b"\r\x00x"
)


def merge_adjacent_text(events: list[TelnetEvent]) -> list[TelnetEvent]:
    """Join neighbouring TelnetText events, since a chunk boundary may split one."""
    merged: list[TelnetEvent] = []
    for event in events:
        if merged and isinstance(event, TelnetText) and isinstance(merged[-1], TelnetText):
            merged[-1] = TelnetText(merged[-1].data + event.data)
        else:
            merged.append(event)
    return merged


def test_the_parser_yields_text_negotiation_subnegotiation_and_command_events() -> None:
    assert TelnetStreamParser().feed(MIXED_STREAM) == [
        TelnetText(b"hi"),
        TelnetNegotiation(WILL, OPTION_MSDP),
        TelnetSubnegotiation(OPTION_TTYPE, b"\x01"),
        TelnetCommand(249),
        TelnetText(b"\rx"),
    ]


def test_every_split_point_gives_the_same_events() -> None:
    expected = TelnetStreamParser().feed(MIXED_STREAM)
    for split in range(1, len(MIXED_STREAM)):
        parser = TelnetStreamParser()
        events = [*parser.feed(MIXED_STREAM[:split]), *parser.feed(MIXED_STREAM[split:])]
        assert merge_adjacent_text(events) == expected, f"split at {split}"


def test_echo_off_bytes_keep_their_nul_as_text() -> None:
    assert TelnetStreamParser().feed(bytes([IAC, WILL, OPTION_ECHO, 0])) == [
        TelnetNegotiation(WILL, OPTION_ECHO),
        TelnetText(b"\x00"),
    ]


def test_a_doubled_iac_inside_a_payload_is_one_255() -> None:
    stream = bytes([IAC, SB, OPTION_NAWS, 0, IAC, IAC, 0, 24, IAC, SE])
    assert TelnetStreamParser().feed(stream) == [TelnetSubnegotiation(OPTION_NAWS, b"\x00\xff\x00\x18")]


def test_an_iac_before_another_byte_inside_a_payload_keeps_both() -> None:
    stream = bytes([IAC, SB, OPTION_MSDP, 1, IAC, 2, IAC, SE])
    assert TelnetStreamParser().feed(stream) == [TelnetSubnegotiation(OPTION_MSDP, b"\x01\xff\x02")]


def test_a_subnegotiation_with_no_option_byte_yields_no_event() -> None:
    assert TelnetStreamParser().feed(bytes([IAC, SB, IAC, SE])) == []


def test_the_sanitizer_text_is_the_parser_text() -> None:
    events = TelnetStreamParser().feed(MIXED_STREAM)
    parser_text = b"".join(event.data for event in events if isinstance(event, TelnetText))
    assert TelnetStreamSanitizer().feed(MIXED_STREAM) == parser_text.decode("latin1")


def test_escape_iac_doubles_255() -> None:
    assert escape_iac(b"a\xffb") == b"a\xff\xffb"


def test_subnegotiation_bytes_escapes_the_payload() -> None:
    assert subnegotiation_bytes(OPTION_NAWS, b"\x00\xff\x00\x18") == bytes(
        [IAC, SB, OPTION_NAWS, 0, IAC, IAC, 0, 24, IAC, SE]
    )


def test_encode_event_round_trips_through_the_parser() -> None:
    events: list[TelnetEvent] = [
        TelnetText(b"a\xffb"),
        TelnetNegotiation(DONT, OPTION_MXP),
        TelnetSubnegotiation(OPTION_MSDP, b"\x01A\x02\xff"),
        TelnetCommand(249),
    ]
    for event in events:
        assert TelnetStreamParser().feed(encode_event(event)) == [event]


def test_describe_event_names_known_options() -> None:
    assert describe_event(TelnetNegotiation(WILL, OPTION_MSDP)) == "WILL MSDP"
    assert describe_event(TelnetNegotiation(DO, 99)) == "DO 99"
    assert describe_event(TelnetSubnegotiation(OPTION_TTYPE, b"\x01")) == "SB TTYPE b'\\x01'"
    assert describe_event(TelnetCommand(249)) == "IAC 249"
    assert describe_event(TelnetText(b"Account email: ")) == "text 15 bytes 'Account email: '"


def test_describe_event_cuts_text_to_sixty_characters() -> None:
    assert describe_event(TelnetText(b"x" * 70)) == f"text 70 bytes {'x' * 60!r}"
