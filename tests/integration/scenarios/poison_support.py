"""Markers and loops shared by the poison scenarios (spec B1 determinism contract)."""

from __future__ import annotations

import pytest

from rots_harness.session import GameSession

POISON_LANDED = ("You feel very sick.",)  # spell_poison, mystic.cpp
POISON_RESISTED = ("You feel your body fend off the poison.",)
DEATH_MARKER = "You are dead!  Sorry..."  # fight.cpp damage()
# Where a dead or slain Harnmage wakes: raw_kill() sends a mortal to r_mortal_start_room[race], and
# the Uruk-Lhuth start room (13626, consts.cpp) is not in the harness world, so check_start_rooms()
# (db.cpp) falls back to real room 0, the world's first room (#1101).
MAGE_RESPAWN_ROOM = "Immortal Start"
ROSTER_CON = 11  # tests/integration/fixtures/character.template.json abilities.con
# comm.cpp's real-time fast block (PULSE_FAST_UPDATE, every 3s) keeps regenerating hit points
# independently of the forced affect ticks (spec B1 is monotonic, not tick-exact), so a post-death
# hit-point assertion taken some wall-clock time after the death tick allows a small margin above
# the pinned revival value instead of an exact match.
REGEN_ALLOWANCE = 3


def death_tick_budget(hit: int, con: int = ROSTER_CON) -> int:
    """Forced ticks needed for 5-damage poison to reach hit <= -con/2 (fight.cpp update_pos), plus two spare."""
    return -(-(hit + con // 2) // 5) + 2


def poison_until_it_lands(caster: GameSession, victim: GameSession, target_word: str, attempts: int = 8) -> None:
    for _attempt in range(attempts):
        victim.command("look")  # clears any AFK flag so Big Brother does not shield the victim
        caster.send_line(f"cast 'poison' {target_word}")
        try:
            text = victim.expect(POISON_LANDED + POISON_RESISTED, timeout=12.0)
        except AssertionError as timeout:
            pytest.fail(f"{timeout}\ncaster side:\n{caster.drain(0.5)[-1500:]}")
        if POISON_LANDED[0] in text:
            return
        caster.drain(0.5)
    pytest.fail(f"poison never landed in {attempts} casts")


def affect_ticks_until_death(harness, victim: GameSession, budget: int) -> bool:
    for _tick in range(budget):
        harness.affects()
        if DEATH_MARKER in victim.drain(0.5):
            return True
    return False
