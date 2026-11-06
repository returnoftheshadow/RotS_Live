from __future__ import annotations

from rots_harness.telnet import TelnetStreamSanitizer

IAC = 255
WILL = 251
DO = 253
SB = 250
SE = 240
ECHO = 1
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
