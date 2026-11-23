"""Telnet client profiles: how a harness session answers the server's option negotiation.

A profile is data only; the negotiator reads it to decide which options to accept and what to
send when the server asks for a terminal type, a window size or a character set.
"""

from __future__ import annotations

from dataclasses import dataclass

from rots_harness.telnet import (
    OPTION_ATCP,
    OPTION_CHARSET,
    OPTION_ECHO,
    OPTION_MCCP2,
    OPTION_MSDP,
    OPTION_MSP,
    OPTION_MSSP,
    OPTION_MXP,
    OPTION_NAMES,
    OPTION_NAWS,
    OPTION_TTYPE,
)
from rots_harness.telnet_payloads import NAWS_DIMENSION_LIMIT


@dataclass(frozen=True)
class TelnetClientProfile:
    """One telnet client's negotiation behaviour.

    Construction raises ValueError for a profile the negotiator cannot honour.
    """

    # The profile's name, as shown in test output and recordings.
    name: str
    # False sends nothing back to any negotiation; True answers each one, accepting or refusing.
    answers_negotiation: bool
    # The options the client agrees to; every other option is refused.
    accepted_options: frozenset[int] = frozenset()
    # The terminal types TTYPE reports, in the order successive requests receive them.
    terminal_types: tuple[str, ...] = ()
    # The width and height NAWS reports, or None when NAWS is not accepted.
    window_size: tuple[int, int] | None = None
    # The character sets the client takes from a CHARSET request, most preferred first.
    charsets: tuple[str, ...] = ()

    def __post_init__(self) -> None:
        if not self.answers_negotiation and self.accepted_options:
            ordered_options = sorted(self.accepted_options)
            option_names = [OPTION_NAMES.get(option, str(option)) for option in ordered_options]
            accepted = ", ".join(option_names)
            raise ValueError(
                f"profile {self.name!r} does not answer negotiation, so it cannot accept {accepted}"
            )

        if OPTION_TTYPE in self.accepted_options and not self.terminal_types:
            raise ValueError(f"profile {self.name!r} accepts TTYPE but has no terminal types")

        if OPTION_NAWS in self.accepted_options and self.window_size is None:
            raise ValueError(f"profile {self.name!r} accepts NAWS but has no window size")

        if OPTION_CHARSET in self.accepted_options and not self.charsets:
            raise ValueError(f"profile {self.name!r} accepts CHARSET but has no character sets")

        if self.window_size is not None:
            if len(self.window_size) != 2:
                raise ValueError(
                    f"profile {self.name!r} window size {self.window_size} must hold exactly a "
                    "width and a height"
                )

            for dimension in self.window_size:
                if not 0 <= dimension <= NAWS_DIMENSION_LIMIT:
                    raise ValueError(
                        f"profile {self.name!r} window size {self.window_size} does not fit "
                        f"0 to {NAWS_DIMENSION_LIMIT}"
                    )

    def accepts(self, option: int) -> bool:
        """Return whether the client agrees to the option when the server offers or requests it."""
        return option in self.accepted_options


# The terminal types MSDP_CLIENT and FULL_CLIENT report: a client name, a terminal, then MTTS
# flags 9 (ANSI and 256 colours).
_PROTOCOL_TERMINAL_TYPES = ("ROTS-HARNESS", "XTERM-256COLOR", "MTTS 9")

# The window MSDP_CLIENT and FULL_CLIENT report.
_PROTOCOL_WINDOW_SIZE = (80, 24)

# The character set MSDP_CLIENT and FULL_CLIENT accept.
_PROTOCOL_CHARSETS = ("ISO-8859-1",)

# The options MSDP_CLIENT accepts; none of them changes the stream's framing.
_MSDP_CLIENT_OPTIONS = frozenset(
    (OPTION_TTYPE, OPTION_ECHO, OPTION_NAWS, OPTION_CHARSET, OPTION_MSDP)
)

# The options FULL_CLIENT accepts beyond MSDP_CLIENT's.
_FULL_CLIENT_EXTRA_OPTIONS = frozenset(
    (OPTION_MSSP, OPTION_ATCP, OPTION_MSP, OPTION_MXP, OPTION_MCCP2)
)

# The session default: it never answers a negotiation, so the session sends only the test's own
# lines.
SILENT_CLIENT = TelnetClientProfile(name="silent", answers_negotiation=False)

# A plain telnet client that refuses every option.
REFUSING_CLIENT = TelnetClientProfile(name="refusing", answers_negotiation=True)

# A protocol client that keeps the stream uncompressed by not accepting MCCP2.
MSDP_CLIENT = TelnetClientProfile(
    name="msdp",
    answers_negotiation=True,
    accepted_options=_MSDP_CLIENT_OPTIONS,
    terminal_types=_PROTOCOL_TERMINAL_TYPES,
    window_size=_PROTOCOL_WINDOW_SIZE,
    charsets=_PROTOCOL_CHARSETS,
)

# A client that turns on every option RotS knows, MCCP2 included; on RotS the stream still stays
# plain, because the server never compresses.
FULL_CLIENT = TelnetClientProfile(
    name="full",
    answers_negotiation=True,
    accepted_options=_MSDP_CLIENT_OPTIONS | _FULL_CLIENT_EXTRA_OPTIONS,
    terminal_types=_PROTOCOL_TERMINAL_TYPES,
    window_size=_PROTOCOL_WINDOW_SIZE,
    charsets=_PROTOCOL_CHARSETS,
)
