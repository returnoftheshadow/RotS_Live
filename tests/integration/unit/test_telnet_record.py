"""Unit tests for rots_harness.telnet_record, writing its log to a temporary folder."""

from __future__ import annotations

from pathlib import Path

from rots_harness.telnet import (
    DO,
    OPTION_MSDP,
    OPTION_NAWS,
    WILL,
    TelnetNegotiation,
    TelnetText,
)
from rots_harness.telnet_record import (
    RecordedTelnetEvent,
    TelnetDirection,
    TelnetRecord,
)

# The server offering MSDP.
WILL_MSDP = TelnetNegotiation(WILL, OPTION_MSDP)

# A client accepting MSDP.
DO_MSDP = TelnetNegotiation(DO, OPTION_MSDP)

# The server asking for the window size.
DO_NAWS = TelnetNegotiation(DO, OPTION_NAWS)


def new_record(tmp_path: Path) -> TelnetRecord:
    """Return an empty record logging to telnet.log in tmp_path."""
    return TelnetRecord(tmp_path / "telnet.log")


def test_events_get_increasing_sequence_numbers_and_one_log_line_each(tmp_path: Path) -> None:
    log_path = tmp_path / "telnet.log"
    record = TelnetRecord(log_path)

    first = record.add(TelnetDirection.RECEIVED, WILL_MSDP)
    second = record.add(TelnetDirection.SENT, DO_MSDP)

    assert first == RecordedTelnetEvent(1, TelnetDirection.RECEIVED, WILL_MSDP)
    assert second == RecordedTelnetEvent(2, TelnetDirection.SENT, DO_MSDP)
    assert record.events == (first, second)
    assert log_path.read_text(encoding="utf-8") == (
        "000001 received WILL MSDP\n"
        "000002 sent     DO MSDP\n"
    )


def test_claim_first_returns_each_event_once(tmp_path: Path) -> None:
    record = new_record(tmp_path)
    offered = record.add(TelnetDirection.RECEIVED, WILL_MSDP)

    def is_will_msdp(recorded: RecordedTelnetEvent) -> bool:
        return recorded.event == WILL_MSDP

    assert record.claim_first(is_will_msdp) == offered
    assert record.claim_first(is_will_msdp) is None


def test_claim_first_skips_sent_events(tmp_path: Path) -> None:
    record = new_record(tmp_path)
    record.add(TelnetDirection.SENT, WILL_MSDP)

    assert record.claim_first(lambda recorded: recorded.event == WILL_MSDP) is None

    received = record.add(TelnetDirection.RECEIVED, WILL_MSDP)
    assert record.claim_first(lambda recorded: recorded.event == WILL_MSDP) == received


def test_claims_need_not_follow_arrival_order(tmp_path: Path) -> None:
    record = new_record(tmp_path)
    offered = record.add(TelnetDirection.RECEIVED, WILL_MSDP)
    asked = record.add(TelnetDirection.RECEIVED, DO_NAWS)

    assert record.claim_first(lambda recorded: recorded.event == DO_NAWS) == asked
    assert record.claim_first(lambda recorded: recorded.event == WILL_MSDP) == offered
    assert record.claim_first(lambda recorded: True) is None


def test_unclaimed_received_lists_what_no_wait_took(tmp_path: Path) -> None:
    record = new_record(tmp_path)
    record.add(TelnetDirection.RECEIVED, TelnetText(b"Account email: "))
    offered = record.add(TelnetDirection.RECEIVED, WILL_MSDP)
    record.add(TelnetDirection.RECEIVED, DO_NAWS)

    record.claim_first(lambda recorded: recorded.event == DO_NAWS)

    assert record.unclaimed_received() == [offered]
