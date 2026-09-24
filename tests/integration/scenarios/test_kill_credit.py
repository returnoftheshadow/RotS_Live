"""manual-test-plan.md item 3: a caster in the room but never engaged with the mob still earns
XP credit for the kill, and a bystander mob caught by splash manufactures no player-facing
record.

Timing model: see blaze_support.py's module docstring for how blaze ticks -- the real-time fast
block, `harness tick`, and `harness affects()` all reach `affect_update_room` through the same
`affect_update()`, so none of the three is a no-op for a room affect, though only the first two
also run `fast_update()`'s regen -- and why a fixed tick budget needs to stay well under the
room affect's duration, with an explicit affect-gone failure instead of a bare timeout. This
file's tick loop
therefore re-floors the orc's hit every iteration (`tick_until_marker`'s `refloor` argument) so
it stays a one-tick kill for the whole loop, not just at the moment it was first set, and
`restore`s `caller` and `fighter` every iteration too (`protect`) so a longer-than-expected loop
cannot kill the very participants the XP-share assertions below depend on -- the room affect
burns every occupant it rolls on, not just the orc.

Uses `Harncaller`, not `Harnmage`, as the caster: `Harnmage` is RACE_MAGUS, other_side of
`Harnfighter`'s RACE_HUMAN (`other_side_impl`, handler.cpp:143-172), so a blaze cast by Harnmage
in the fighter's room would also burn and engage the fighter as a second hostile target -- an
early run of this scenario showed the fighter switching straight from the dead orc onto
"*an Uruk*" (the mage) once the orc died. `Harncaller` shares the fighter's side, so blaze's
`is_spared_by_room_blast()` check (mage.cpp) skips it.

blaze's on-cast room-wide burst does burn an orc (`is_spared_by_room_blast()`, mage.cpp: an
other-side race), and a burst hit engages the caster with whatever it hits. So both tests cast
blaze BEFORE loading any orc, while the room holds only players the burst spares; the orcs arrive
into the burning room, and only the room affect's later TICKS (room_affect_tick.cpp,
`blaze_tick()`, driven by `harness tick`'s `affect_update()` sweep) reach them. Those ticks credit
the recorded caster independent of who is engaged. The
fighter's own melee is floored with `combat_support.neutralize_melee` so the orc's low `hit`
can only reach zero from a blaze tick and not a stray fighter swing -- proven necessary by that
same early run, where the fighter's own hit finished the orc before any tick could.

Confirmed empirically, not just from reading die(): the caster's and the fighter's exploits files
(`harncaller.exploits.json`, `harnfighter.exploits.json`) read back the unmodified
account-creation stub `{"version": 1, "records": []}` even in a run where the fighter *did*
receive its share line. die() explains why: for an NPC `dead_man` it returns via
raw_kill() at fight.cpp:1272-1275, before any of the function's four add_exploit_record() calls
(1290/1300/1314/1320) -- every one of them gated to `!IS_NPC(dead_man)`. Exploit records exist
only for a player's death, never a mob's, regardless of who is credited; the brief for this task
expected a mob-kill record type to confirm and tighten an assertion to, and this is the
confirmation that no such record exists. Because an empty read would equally follow from reading
the wrong directory, each test starts by having Harnvictim die to the brute orc
(`combat_support.prove_exploit_reader_with_a_brute_death`) and reading that record back, before
the blaze is cast (a burning room would otherwise reach the participants while it waits).
"Kill credit" for a mob kill is observable only through group_gain()'s XP-share line
(fight.cpp:1408-1553): it pays the present, credited killer (line 1428,
`killer_is_present && killer != dead_man`) and, separately, anyone still fighting the dead
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

import pytest

from blaze_support import LETHAL_HIT, BLAZE_CAST, tick_until_marker
from combat_support import neutralize_melee, prove_exploit_reader_with_a_brute_death, quit_once_anger_allows, stat_replies, wait_for_engagement
from rots_harness import fixtures, records
from rots_harness.session import GameSession

pytestmark = pytest.mark.scenario

SHARE_MARKER = "You receive your share of experience"
ORC_DEATH_MARKER = "A target orc is dead"


def _is_genuine_mob_reply(text: str) -> bool:
    """A genuine `stat <mob_name>` reply: either a `Fighting:` line (do_stat_character,
    act_wiz.cpp) if it is still alive, or "Nothing around by that name." (do_wizstat's
    bare-name fallthrough, act_wiz.cpp:1133-1140) if it has died.
    """
    lowered = text.lower()
    return "nothing around by that name" in lowered or "fighting:" in lowered


def _set_up_arena_west_fight(imp: GameSession, caller: GameSession, fighter: GameSession) -> None:
    imp.command(f"goto {fixtures.ROOM_ARENA_WEST}")
    imp.command("transfer harncaller")
    imp.command("transfer harnfighter")
    caller.expect_room("Arena West")
    fighter.expect_room("Arena West")
    imp.command("restore harncaller")
    neutralize_melee(imp, "harnfighter")


def _leave_the_fire_and_quit(imp: GameSession, caller: GameSession, fighter: GameSession, harness) -> None:
    """Walks everybody out of the burning Arena West before the fighter's quit loop.

    quit_once_anger_allows fires forced `harness affects` ticks, and each one also runs the
    room-affect sweep (limits.cpp affect_update_room), which burns any occupant of the blaze
    room with no exemption for the caster or an immortal. The assertions are done by now, so
    a burn cannot change a verdict, but it could kill Harncaller after the scenario passed.
    """
    caller.command("east")
    fighter.command("east")
    imp.command(f"goto {fixtures.ROOM_ARENA_CENTRE}")
    caller.expect_room("Arena Centre")
    fighter.expect_room("Arena Centre")
    quit_once_anger_allows(fighter, harness)  # attacking the orc angered the fighter


def test_killing_blow_from_an_unengaged_caster_is_credited_to_the_caster(server, imp, caller, fighter, victim, harness) -> None:
    prove_exploit_reader_with_a_brute_death(server, imp, victim, "Harnvictim")  # before any fire burns
    _set_up_arena_west_fight(imp, caller, fighter)
    caller.cast("blaze", success_markers=BLAZE_CAST)  # before the orc arrives: the caster never engages it
    imp.command("load mob 1130")

    fighter.command("kill target")  # the orc tanks the fighter, who cannot actually hurt it
    wait_for_engagement(imp, "target", "Harnfighter")

    tick_until_marker(harness, imp, imp, ORC_DEATH_MARKER, protect=(caller, fighter), refloor=("target", LETHAL_HIT))

    # Both the still-engaged fighter and the unengaged, present caster are paid: see the module
    # docstring's group_gain() citation for why credit is not exclusive to one of them. `protect`
    # above kept both of them alive through the tick loop, so neither share line can be missing
    # merely because the room's own fire got to them first.
    fighter.expect([SHARE_MARKER], 10.0)
    caller.expect([SHARE_MARKER], 10.0)

    for name in ("Harncaller", "Harnfighter"):
        assert records.read_exploits(server.lib_dir, name) == [], (
            "an NPC death never manufactures an exploit record for anybody (fight.cpp "
            "1272-1275, 1288-1320); the XP-share lines above are the only observable credit"
        )

    _leave_the_fire_and_quit(imp, caller, fighter, harness)


def test_splash_bystander_manufactures_no_credit(server, imp, caller, fighter, victim, harness) -> None:
    prove_exploit_reader_with_a_brute_death(server, imp, victim, "Harnvictim")  # before any fire burns
    _set_up_arena_west_fight(imp, caller, fighter)
    caller.cast("blaze", success_markers=BLAZE_CAST)  # before the orcs arrive, as above
    imp.command("load mob 1130")
    imp.command("load mob 1132")  # the bystander, never fighting anybody

    fighter.command("kill target")
    wait_for_engagement(imp, "target", "Harnfighter")

    tick_until_marker(harness, imp, imp, ORC_DEATH_MARKER, protect=(caller, fighter), refloor=("target", LETHAL_HIT))

    fighter.expect([SHARE_MARKER], 10.0)
    caller.expect([SHARE_MARKER], 10.0)

    # room_affect_tick.cpp's blaze_tick() always hands damage_credited() the occupant as its own
    # attacker (room_affect_tick.cpp:16-21's file banner), so a splash hit never engages the
    # bystander with anybody. The bystander's own tick roll is independent of the orc's, so by
    # the time the loop above stops it may have taken no damage, some, or died outright; both
    # outcomes are asserted explicitly here rather than one of them being silently skipped, per
    # the task's own ruling ("it survived... or it died..."). A bare `stat <name>` (no "mob"
    # prefix) falls through do_wizstat's final `else` (act_wiz.cpp:1133-1140), whose
    # "Nothing around by that name." is the one text that confirms it is the "died" half, not a
    # stale keyword lookup; the exploits check just below covers both halves unconditionally.
    bystander_replies = stat_replies(imp, "bystander", _is_genuine_mob_reply)
    if not _is_genuine_mob_reply(bystander_replies[-1]):
        pytest.fail(f"stat bystander never returned a parseable reply in {len(bystander_replies)} attempts")
    bystander_stat = bystander_replies[-1]
    bystander_text = bystander_stat.lower()
    bystander_gone = "nothing around by that name" in bystander_text
    if not bystander_gone:
        assert "fighting: nobody" in bystander_text, (
            f"splash damage must never drag the bystander into a fight: {bystander_stat}"
        )

    for name in ("Harncaller", "Harnfighter"):
        for record in records.read_exploits(server.lib_dir, name):
            assert "bystander" not in record.victim_name.lower(), f"{name}: {record}"

    _leave_the_fire_and_quit(imp, caller, fighter, harness)
