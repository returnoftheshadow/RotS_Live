"""Unit tests for rots_harness.telnet_payloads' decoders and encoders."""

from __future__ import annotations

import pytest

from rots_harness.telnet_payloads import (
    TelnetPayloadError,
    decode_charset_request,
    decode_msdp,
    decode_mssp,
    encode_msdp,
    encode_naws,
)


def test_decode_msdp_reads_a_string() -> None:
    assert decode_msdp(b"\x01ROOM_VNUM\x021100") == {"ROOM_VNUM": "1100"}


def test_decode_msdp_reads_an_empty_value() -> None:
    assert decode_msdp(b"\x01ROOM_EXITS\x02") == {"ROOM_EXITS": ""}


def test_decode_msdp_reads_a_table_holding_an_empty_array() -> None:
    assert decode_msdp(b"\x01GROUP\x02\x03\x01MEMBERS\x02\x05\x06\x04") == {"GROUP": {"MEMBERS": []}}


def test_decode_msdp_reads_the_commands_list() -> None:
    payload = b"\x01COMMANDS\x02\x05\x02LIST\x02REPORT\x02RESET\x02SEND\x02UNREPORT\x06"
    assert decode_msdp(payload) == {"COMMANDS": ["LIST", "REPORT", "RESET", "SEND", "UNREPORT"]}


def test_decode_msdp_reads_a_room_table_with_an_exits_array() -> None:
    payload = b"\x01ROOM\x02\x03\x01VNUM\x021100\x01EXITS\x02\x05\x021101\x06\x04"
    assert decode_msdp(payload) == {"ROOM": {"VNUM": "1100", "EXITS": ["1101"]}}


def test_decode_msdp_reads_repeated_vals_as_a_list() -> None:
    assert decode_msdp(b"\x01REPORT\x02HEALTH\x02MANA") == {"REPORT": ["HEALTH", "MANA"]}


@pytest.mark.parametrize(
    "payload",
    [
        b"\x02x",
        b"\x01A\x02\x03",
        b"\x01A\x02\x05\x02x",
        b"\x01A\x021\x01A\x022",
    ],
)
def test_decode_msdp_refuses_malformed_frames(payload: bytes) -> None:
    with pytest.raises(TelnetPayloadError):
        decode_msdp(payload)


def test_decode_msdp_error_names_the_byte_offset() -> None:
    with pytest.raises(TelnetPayloadError, match="byte 4"):
        decode_msdp(b"\x01A\x021\x01A\x022")


def test_encode_msdp_writes_var_then_vals() -> None:
    assert encode_msdp("LIST", "COMMANDS") == b"\x01LIST\x02COMMANDS"
    assert encode_msdp("REPORT", "HEALTH", "MANA") == b"\x01REPORT\x02HEALTH\x02MANA"
    with pytest.raises(ValueError):
        encode_msdp("A\x02", "x")


def test_decode_mssp_reads_every_field() -> None:
    payload = b"\x01NAME\x02Return of the Shadow\x01PLAYERS\x020"
    assert decode_mssp(payload) == {"NAME": ["Return of the Shadow"], "PLAYERS": ["0"]}


def test_decode_charset_request_splits_on_its_separator() -> None:
    assert decode_charset_request(b"\x01 ISO-8859-1") == ["ISO-8859-1"]
    assert decode_charset_request(b"\x01;UTF-8;ISO-8859-1") == ["UTF-8", "ISO-8859-1"]
    with pytest.raises(TelnetPayloadError):
        decode_charset_request(b"\x02x")


def test_encode_naws_is_big_endian() -> None:
    assert encode_naws(80, 24) == b"\x00\x50\x00\x18"
    with pytest.raises(ValueError):
        encode_naws(65536, 24)
