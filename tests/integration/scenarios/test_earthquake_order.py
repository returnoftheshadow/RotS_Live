"""manual-test-plan.md item 5: earthquake in a crowded room where the caster also falls --
every other occupant's fall message precedes the caster's own, which is now the spell's
deferred, final act.

`spell_earthquake` (src/mage.cpp) runs a fall loop over every room occupant (~1779-1794): the
`fall` lambda (~1755-1770) sends `$n loses balance and falls down!` TO_ROOM in the origin
room, then relocates the faller into the crevice and sends `$n falls in.` TO_ROOM there (the
faller itself never sees either line -- it gets "The earthquake throws you down!" instead,
send_to_char'd inside the same lambda). Pre-fix, the caster's own fall ran inline wherever the
room's occupant chain happened to place it; a lethal self-fall could free the caster (NPC) or
otherwise invalidate it mid-loop while later occupants were still being processed (the UAF
`src/tests/mage_tests.cpp`'s `EarthquakeLetsEveryOtherOccupantFallBeforeTheCastersOwnFall`
pins). The fix defers the caster's own `fall()` call (~1795-1796, "may free or relocate
`caster`; nothing reads it after this") until every other occupant has already fallen, so an
observer who stays in the origin room sees the caster's own "loses balance and falls down!"
broadcast last among the fall lines it can see, and an observer who fell earlier and is
already down in the crevice sees the caster's "falls in." line last instead -- either way, the
caster's fall is the final fall-related line in that observer's transcript.

Room: `Harnmage` (the roster's only earthquake-capable caster, `fixtures.MAGE_SKILLS`) casts
standing in `fixtures.ROOM_ARENA_WEST` (1130), which carries a plain, already-open DOWN exit to
`fixtures.ROOM_CREVICE_FLOOR` (1136, tests/integration/world/wld/11.wld) -- `spell_earthquake`'s
`!cur_room->dir_option[DOWN]->exit_info` check (mage.cpp ~1687-1688) makes the crack open
deterministically there, so every cast attempt targets the same crevice rather than rolling for
a fresh one. `Harnfighter`, `Harnvictim` and `Harncaller` fill out the room -- the "crowded
room" manual-test-plan.md item 5 asks for -- and observe it; earthquake's damage and fall loops
apply to every occupant `!= caster` unconditionally (no `other_side` gate, unlike fireball's
splash targeting: mage.cpp:1699-1710), so which side each occupant is on does not matter here.

Each occupant's fall (caster included) is `!saved || !number(0, 1)` (mage.cpp ~1786), so no
single cast attempt is guaranteed to produce both a caster fall and a bystander fall. The test
retries the cast, resetting every occupant's position/health between attempts, until one
attempt's transcript shows both, then asserts fall-line order on that ONE attempt's freshly
drained text -- not on each session's whole history, which would still carry stray fall lines
left over from any earlier, discarded attempts.

`Harnmage` is `RACE_MAGUS` (Uruk-Lhuth), and none of the observers (`Harnfighter`/Human,
`Harnvictim`/Wood-elf, `Harncaller`/Human) can ever see its real name in a third-person `act()`
message: `PERS()` (utility.cpp ~2179-2199) renders a target as its race's generic descriptor,
not `GET_NAME()`, whenever `other_side(observer, target)` is true, and its `RACE_MAGI(other)`
arm (handler.cpp ~158-159) makes that true for every non-Magus observer regardless of
good/evil side -- confirmed against a first run's actual transcript, which read "*an Uruk*
falls in." and "*an Uruk* is thrown to the ground by a sudden earthquake.", never "Harnmage
falls in.". `pc_star_types[RACE_MAGUS]` (consts.cpp ~2046-2049) is the fixed string "*an
Uruk*"; no other roster race renders that way (Human is "*a Human*", Wood-elf is "*an Elf*"),
so it is used below as the caster's fall-line marker instead of its character name.
"""

from __future__ import annotations

import pytest

from rots_harness import fixtures
from rots_harness.session import GameSession

pytestmark = pytest.mark.scenario

FALL_SUFFIX = " loses balance and falls down!"  # fall()'s act(...TO_ROOM) in the origin room (mage.cpp ~1756)
FELL_IN_SUFFIX = " falls in."  # fall()'s act(...TO_ROOM) once the faller lands in the crevice (mage.cpp ~1761)
CASTER = "*an Uruk*"  # pc_star_types[RACE_MAGUS]: how every non-Magus observer's act() sees Harnmage (see module docstring)
OCCUPANTS = ("harnmage", "harnfighter", "harnvictim", "harncaller")
CAST_ATTEMPTS = 12
CAST_RESOLUTION_TIMEOUT = 6.0  # CASTING_TIME(ch, SPELL_EARTHQUAKE) is ~10 heartbeats at 4/sec (~2.5s); generous margin
OBSERVER_DRAIN_TIMEOUT = 1.5


def _fall_lines(text: str) -> list[str]:
    return [line.strip() for line in text.splitlines() if line.strip().endswith((FALL_SUFFIX, FELL_IN_SUFFIX))]


def _reset_room(imp: GameSession) -> None:
    """Gathers every occupant back into the quake room at full health and standing position.

    `transfer` alone (act_wiz.cpp `do_trans`) leaves a character sitting wherever a previous
    attempt's fall last left its position -- it never calls `update_pos`. `restore`
    (`do_restore`) both heals to full and calls `update_pos`, which sets POSITION_STANDING for
    any character with positive hit and no active fight, so `transfer` must run first.
    """
    imp.command(f"goto {fixtures.ROOM_ARENA_WEST}")
    for name in OCCUPANTS:
        imp.command(f"transfer {name}")
        imp.command(f"wizset {name} maxhit 2000")
        imp.command(f"restore {name}")
    imp.command(f"goto {fixtures.ROOM_IMMORTAL_START}")  # the imp is not an occupant


def test_the_casters_fall_is_reported_last(server, imp, mage, fighter, victim, caller, harness) -> None:
    _reset_room(imp)
    for session in (mage, fighter, victim, caller):
        session.expect_room("Arena West")

    observer_text: dict[str, str] = {}
    for _attempt in range(CAST_ATTEMPTS):
        mage.send_line("cast 'earthquake'")
        mage.drain(CAST_RESOLUTION_TIMEOUT)  # covers "You start to concentrate." plus the delayed spell resolution
        observer_text = {
            "Harnfighter": fighter.drain(OBSERVER_DRAIN_TIMEOUT),
            "Harnvictim": victim.drain(OBSERVER_DRAIN_TIMEOUT),
            "Harncaller": caller.drain(OBSERVER_DRAIN_TIMEOUT),
        }
        fall_lines = _fall_lines("\n".join(observer_text.values()))
        caster_fell = any(line.startswith(CASTER) for line in fall_lines)
        others_fell = any(not line.startswith(CASTER) for line in fall_lines)
        if caster_fell and others_fell:
            break
        _reset_room(imp)
        for session in (mage, fighter, victim, caller):
            session.expect_room("Arena West")
    else:
        pytest.fail(
            f"no cast in {CAST_ATTEMPTS} attempts produced both the caster's fall and another "
            f"occupant's fall; last attempt's observer text:\n{observer_text}"
        )

    # Assert order on THIS attempt's freshly drained text alone (see module docstring) -- each
    # observer's own transcript preserves the true broadcast order it was sent in, so the
    # caster's fall line (whichever suffix it carries, depending on whether this observer had
    # already fallen into the crevice itself) must be the LAST fall line that observer saw.
    for observer_name, text in observer_text.items():
        lines = _fall_lines(text)
        caster_positions = [index for index, line in enumerate(lines) if line.startswith(CASTER)]
        if not caster_positions:
            continue  # this observer need not have witnessed the caster's fall itself
        assert caster_positions[-1] == len(lines) - 1, (
            f"{observer_name} saw a fall line after the caster's own: {lines}"
        )
