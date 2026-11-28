"""Unit tests for rots_harness.telnet_profile's presets and validation."""

from __future__ import annotations

import pytest

from rots_harness.telnet import (
    OPTION_CHARSET,
    OPTION_MCCP2,
    OPTION_MSDP,
    OPTION_NAWS,
    OPTION_TTYPE,
)
from rots_harness.telnet_profile import (
    FULL_CLIENT,
    MSDP_CLIENT,
    REFUSING_CLIENT,
    SILENT_CLIENT,
    TelnetClientProfile,
)


def test_the_presets_are_valid_and_named() -> None:
    presets = {
        "SILENT_CLIENT": SILENT_CLIENT,
        "REFUSING_CLIENT": REFUSING_CLIENT,
        "MSDP_CLIENT": MSDP_CLIENT,
        "FULL_CLIENT": FULL_CLIENT,
    }
    for constant_name, profile in presets.items():
        assert isinstance(profile, TelnetClientProfile)
        assert profile.name == constant_name.lower().removesuffix("_client")


def test_only_the_full_client_accepts_mccp2() -> None:
    assert FULL_CLIENT.accepts(OPTION_MCCP2)
    assert not MSDP_CLIENT.accepts(OPTION_MCCP2)
    assert not REFUSING_CLIENT.accepts(OPTION_MCCP2)
    assert not SILENT_CLIENT.accepts(OPTION_MCCP2)


def test_a_silent_profile_may_not_accept_options() -> None:
    with pytest.raises(ValueError):
        TelnetClientProfile(
            name="silent_msdp", answers_negotiation=False, accepted_options=frozenset({OPTION_MSDP})
        )


def test_accepting_ttype_needs_terminal_types() -> None:
    with pytest.raises(ValueError):
        TelnetClientProfile(
            name="no_ttype", answers_negotiation=True, accepted_options=frozenset({OPTION_TTYPE})
        )


def test_accepting_naws_needs_a_window_size() -> None:
    with pytest.raises(ValueError):
        TelnetClientProfile(
            name="no_naws", answers_negotiation=True, accepted_options=frozenset({OPTION_NAWS})
        )


def test_accepting_charset_needs_a_charset() -> None:
    with pytest.raises(ValueError):
        TelnetClientProfile(
            name="no_charset", answers_negotiation=True, accepted_options=frozenset({OPTION_CHARSET})
        )


def test_a_window_size_must_hold_exactly_two_values() -> None:
    for window_size in ((80,), (80, 24, 1)):
        with pytest.raises(ValueError, match="exactly a width and a height"):
            TelnetClientProfile(
                name="misshapen",
                answers_negotiation=True,
                accepted_options=frozenset({OPTION_NAWS}),
                window_size=window_size,  # type: ignore[arg-type]
            )


def test_window_dimensions_must_fit_sixteen_bits() -> None:
    for window_size in ((65536, 24), (80, 65536), (-1, 24), (80, -1)):
        with pytest.raises(ValueError):
            TelnetClientProfile(
                name="wide",
                answers_negotiation=True,
                accepted_options=frozenset({OPTION_NAWS}),
                window_size=window_size,
            )
    widest = TelnetClientProfile(
        name="widest",
        answers_negotiation=True,
        accepted_options=frozenset({OPTION_NAWS}),
        window_size=(65535, 0),
    )
    assert widest.window_size == (65535, 0)
