"""Casts a spell until it gets past the concentration roll.

do_cast (spell_pa.cpp) loses the caster's concentration when number(0, 100) >= 100, once in 101
casts even at full knowledge, with its own message. A scenario that needs the spell to go off
repeats the cast after that message.
"""

from __future__ import annotations

from collections.abc import Sequence

from rots_harness.replies import SETTLE_SECONDS, reply_to
from rots_harness.session import GameSession

# What the caster reads when the concentration roll fails (do_cast, spell_pa.cpp).
LOST_CONCENTRATION_REPLY = "You lost your concentration!"
# What the caster reads when its spirit or mana does not cover the cast (can_cast_spell,
# spell_pa.cpp).
NO_ENERGY_REPLY = "You can't summon enough energy to cast the spell."

# Seconds each attempt waits for its reply, which comes after the spell's casting time; pass
# timeout= for a longer spell.
CAST_TIMEOUT_SECONDS = 15.0


def cast_until_it_lands(
    caster: GameSession,
    line: str,
    outcomes: Sequence[str],
    *,
    timeout: float = CAST_TIMEOUT_SECONDS,
    settle: float = SETTLE_SECONDS,
) -> str:
    """Send line until its reply holds no LOST_CONCENTRATION_REPLY, and return that reply.

    Each attempt is reply_to(caster, line, [LOST_CONCENTRATION_REPLY, *outcomes], settle=settle,
    timeout=timeout). Each attempt costs spirit or mana, which nothing restores between requests
    under affect-hours, so a run of losses ends in a refusal such as NO_ENERGY_REPLY. Only the lost
    concentration is acted on: which of outcomes arrived, including such a refusal, is left to the
    caller. Fails with SessionTimeout when an attempt's reply holds none of them within timeout.
    """
    markers = [LOST_CONCENTRATION_REPLY, *outcomes]
    while True:
        text = reply_to(caster, line, markers, settle=settle, timeout=timeout)
        if LOST_CONCENTRATION_REPLY not in text:
            return text
