"""manual-test-plan.md item 3: a caster in the room but never engaged with the mob still earns
XP credit for the kill, and a bystander mob caught by splash manufactures no player-facing
record.

Uses `Harncaller`, not `Harnmage`, as the caster: `Harnmage` is RACE_MAGUS, other_side of
`Harnfighter`'s RACE_HUMAN (`other_side_impl`, handler.cpp:143-172), so a blaze cast by Harnmage
in the fighter's room would also burn and engage the fighter as a second hostile target -- an
early run of this scenario showed the fighter switching straight from the dead orc onto
"*an Uruk*" (the mage) once the orc died. `Harncaller` shares the fighter's side, so blaze's
`is_friendly_taget()` check (mage.cpp:2314) skips it.

blaze's on-cast room-wide burst (mage.cpp:2291-2358) also skips ordinary NPCs outright:
`other_side_impl`'s first check (handler.cpp:145) treats an un-charmed NPC as never "the other
side," so `is_friendly_taget()` is always true for the orc there. Only the room affect's later
TICKS (room_affect_tick.cpp:66-79, `blaze_tick()`, driven by `harness tick`'s `affect_update()`
sweep) reach an NPC, and they credit the recorded caster independent of who is engaged. The
fighter's own melee is floored with `wizset ... OB/damage -20` (the brute idiom from
test_poison_punishment_player_poison_mob_fight.py's `_neutralize_brute_melee`) so the orc's low
`hit` can only reach zero from a blaze tick and not a stray fighter swing -- proven necessary by
that same early run, where the fighter's own hit finished the orc before any tick could.

Checked against a kept run's `harnmage.exploits.json` (`ROTS_IT_KEEP=1`, run f479e44cddf3 under
`build/integration/`): it read back `{"version": 1, "records": []}` even in a run where the
fighter *did* receive its share line. die() explains why: for an NPC `dead_man` it returns via
raw_kill() at fight.cpp:1272-1275, before any of the function's four add_exploit_record() calls
(1290/1300/1314/1320) -- every one of them gated to `!IS_NPC(dead_man)`. Exploit records exist
only for a player's death, never a mob's, regardless of who is credited; the brief for this task
expected a mob-kill record type to confirm and tighten an assertion to, and this is the
confirmation that no such record exists. "Kill credit" for a mob kill is observable only through
group_gain()'s XP-share line (fight.cpp:1408-1553): it pays the present, credited killer (line
1428, `killer_is_present && killer != dead_man`) and, separately, anyone still fighting the dead
mob at the death instant (the room walk at 1446-1453, which reads survivors' own un-cleared
`specials.fighting` pointers -- the dead mob's OWN pointer is cleared by its stop_fighting() call
before die() runs, but nothing clears the reverse pointer on whoever was still swinging at it).
So both the engaged fighter and the unengaged-but-present Harncaller receive the share line --
the same "caster standing in the room, unengaged -- they join the XP split" case
manual-test-plan.md item 3's last bullet documents for the remote-credit scenario, exercised here
mid-group-fight instead of solo. This pins the regression item 3's first bullet names: pre-fix,
"kills could go unrecorded when the killer wasn't the engaged opponent" -- before
damage_credited() split the engaging attacker from the credited killer (6669551/e994796).
"""

from __future__ import annotations

import time

import pytest

from poison_support import BLAZE_CAST
from rots_harness import fixtures, records
from rots_harness.session import GameSession

pytestmark = pytest.mark.scenario

SHARE_MARKER = "You receive your share of experience"
ORC_DEATH_MARKER = "A target orc is dead"


def _neutralize_fighter_melee(imp: GameSession) -> None:
    """Floors Harnfighter's OB/damage (act_wiz.cpp wizset, both `BOTH`-scoped fields) so its own
    hits land for ~0 damage, the same `dam = max(0, dam)` clamp
    test_poison_punishment_player_poison_mob_fight.py's `_neutralize_brute_melee` relies on --
    here applied to the melee fighter instead of the mob, so only a blaze tick can finish the
    orc.
    """
    imp.command("wizset harnfighter OB -20")
    imp.command("wizset harnfighter damage -20")


def _wait_for_engagement(imp: GameSession, mob_name: str, victim_name: str, timeout: float = 10.0) -> None:
    """Polls `stat <mob_name>` for `Fighting: <victim_name>` before trusting combat_list state --
    test_poison_punishment_player_poison_mob_fight.py's identical helper explains why `kill`
    alone does not guarantee set_fighting() has run yet.
    """
    deadline = time.monotonic() + timeout
    last_text = ""
    while True:
        stat = imp.command(f"stat {mob_name}")
        last_text = stat.text
        if f"fighting: {victim_name.lower()}" in stat.text.lower():
            return
        if time.monotonic() >= deadline:
            pytest.fail(f"{mob_name} never engaged {victim_name} within {timeout}s: {last_text}")
        imp.drain(0.5)


def _tick_until_marker(harness, imp: GameSession, marker: str, budget: int = 25) -> None:
    """Forces `harness tick` (test_harness.cpp; drives `affect_update()`'s room-affect sweep) up
    to `budget` times, checking `harness.tick()`'s OWN returned transcript for `marker` first --
    the room-tick damage (and its "$n is dead!" broadcast) happens synchronously inside the
    command's own engine call, before the server sends "Harness: hourly tick complete.", so
    `Harness.tick()`'s `expect()` already consumes it; a separate `imp.drain()` afterward sees
    nothing and is only a fallback for the rare case where the fast real-time block (also
    active in harness mode, see test_blaze_after_caster_gone.py's module docstring) delivers it
    off-tick. blaze is a fast room affect: its per-occupant roll is roughly a 38% chance per
    tick (limits.cpp's `number(0, 12)` plus the fast-spell `number(0, 2)` chance,
    affect_update_room), so one or two ticks cannot be trusted to land it -- but the room affect
    burns every occupant, PCs included, so the loop must stop the instant the marker lands
    rather than run the full budget regardless.
    """
    for _tick in range(budget):
        if marker in harness.tick().text:
            return
        if marker in imp.drain(1.0):
            return
    pytest.fail(f"{marker!r} never appeared within {budget} harness ticks")


def _set_up_arena_west_fight(server, imp: GameSession, caller: GameSession, fighter: GameSession) -> None:
    imp.command(f"goto {fixtures.ROOM_ARENA_WEST}")
    imp.command("transfer harncaller")
    imp.command("transfer harnfighter")
    caller.expect_room("Arena West")
    fighter.expect_room("Arena West")
    imp.command("restore harncaller")
    _neutralize_fighter_melee(imp)


def test_killing_blow_from_an_unengaged_caster_is_credited_to_the_caster(server, imp, caller, fighter, harness) -> None:
    _set_up_arena_west_fight(server, imp, caller, fighter)
    imp.command("load mob 1130")
    imp.command("wizset target hit 9")  # below the smallest halved blaze tick

    fighter.command("kill target")  # the orc tanks the fighter, who cannot actually hurt it
    _wait_for_engagement(imp, "target", "Harnfighter")

    caller.cast("blaze", success_markers=BLAZE_CAST)  # the caster never engages the orc
    _tick_until_marker(harness, imp, ORC_DEATH_MARKER)

    # Both the still-engaged fighter and the unengaged, present caster are paid: see the module
    # docstring's group_gain() citation for why credit is not exclusive to one of them.
    fighter.expect([SHARE_MARKER], 10.0)
    caller.expect([SHARE_MARKER], 10.0)

    for name in ("Harncaller", "Harnfighter"):
        assert records.read_exploits(server.lib_dir, name) == [], (
            "an NPC death never manufactures an exploit record for anybody (fight.cpp "
            "1272-1275, 1288-1320); the XP-share lines above are the only observable credit"
        )


def test_splash_bystander_manufactures_no_credit(server, imp, caller, fighter, harness) -> None:
    _set_up_arena_west_fight(server, imp, caller, fighter)
    imp.command("load mob 1130")
    imp.command("load mob 1132")  # the bystander, never fighting anybody
    imp.command("wizset target hit 9")

    fighter.command("kill target")
    _wait_for_engagement(imp, "target", "Harnfighter")

    caller.cast("blaze", success_markers=BLAZE_CAST)
    _tick_until_marker(harness, imp, ORC_DEATH_MARKER)

    fighter.expect([SHARE_MARKER], 10.0)
    caller.expect([SHARE_MARKER], 10.0)

    # room_affect_tick.cpp's blaze_tick() always hands damage_credited() the occupant as its own
    # attacker (room_affect_tick.cpp:16-21's file banner), so a splash hit never engages the
    # bystander with anybody; if it is still alive, its own Fighting: line proves that.
    bystander_stat = imp.command("stat bystander")
    if "fighting:" in bystander_stat.text.lower():
        assert "fighting: nobody" in bystander_stat.text.lower(), (
            f"splash damage must never drag the bystander into a fight: {bystander_stat.text}"
        )

    for name in ("Harncaller", "Harnfighter"):
        for record in records.read_exploits(server.lib_dir, name):
            assert "bystander" not in record.victim_name.lower(), f"{name}: {record}"
