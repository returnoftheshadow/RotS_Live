from __future__ import annotations

import pytest

from blaze_support import BLAZE_CAST
from rots_harness import fixtures

pytestmark = pytest.mark.scenario

SHARE_MARKER = "You receive your share of experience"


def test_engaged_fighter_gets_the_share_and_the_remote_caster_does_not(server, imp, mage, fighter, harness) -> None:
    imp.command(f"goto {fixtures.ROOM_ARENA_WEST}")
    imp.command("transfer harnmage")
    imp.command("transfer harnfighter")
    imp.command("restore harnmage")

    fighter.command("east")  # fighter waits in 1131 while the mage casts alone in 1130
    mage.cast("blaze", success_markers=BLAZE_CAST)  # empty room: the cast engages nobody
    mage.command("east")  # back to 1131, remote from the death room

    imp.command("load mob 1130")
    imp.command("wizset orc hit 9")  # below the smallest halved blaze tick, so the tick kills it

    fighter.command("west")
    fighter.command("kill orc")
    harness.tick()

    # The orc's death may come from the blaze tick or from the fighter's next blows, so wait
    # for the share message itself rather than a fixed interval after the tick.
    fighter_text = fighter.expect([SHARE_MARKER], 30.0)
    mage_text = mage.drain(2.0)
    assert SHARE_MARKER in fighter_text, fighter_text
    assert SHARE_MARKER not in mage_text, mage_text
