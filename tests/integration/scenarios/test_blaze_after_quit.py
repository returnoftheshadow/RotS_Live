"""manual-test-plan.md item 1 (quit arm): a blaze keeps ticking, and can still kill, after its
caster quits. The caster quits and its socket closes, which frees the body, so the tick credits
nobody and the victim takes the gentle arm a tick death always took (the registration serial has
nothing left to resolve back to; contrast test_blaze_after_caster_gone.py's slain-caster arm,
where the body survives and the tick still credits the mage).

Timing model: see blaze_support.py's module docstring -- blaze ticks from the real-time fast
block (comm.cpp, ~3s, unconditional regardless of harness_mode), this harness's own
`harness tick`, and `harness affects()` alike (all three reach `affect_update_room` through the
same `affect_update()`; `affects()` is not a no-op for a room affect, it just does not force
blaze's own roll or run regen). This scenario floors the victim's hit every loop iteration
(`tick_until_marker`'s `refloor`) so a single successful tick stays lethal for the whole loop --
regen would otherwise claw the floor back before the next tick -- and keeps its tick budget
under the affect's nominal duration, with an explicit failure naming the cause if the affect
runs out first under a loaded run. There is no separate "did it tick at all" check here: with the
victim's hit floored this low, a landed tick and a death are the same observable event (see
`blaze_support`'s `LETHAL_HIT` note on the rare non-lethal case), so the death marker below is
the only signal that is not vacuous -- a bare hit-point comparison against the floor value would
be satisfied by the `wizset` itself, before any tick ever ran.
"""

from __future__ import annotations

import re

import pytest

import poison_support
from blaze_support import LETHAL_HIT, BLAZE_CAST, tick_until_marker, wait_for_log_line
from combat_support import BRUTE_ORC_VNUM, VICTIM_LEVEL, neutralize_melee, quit_once_anger_allows, stat_replies, wait_for_engagement
from poison_support import DEATH_MARKER
from rots_harness import fixtures, records
from rots_harness.session import GameSession, Transcript

pytestmark = pytest.mark.scenario

PRE_LOOP_SURVIVABLE_HIT = 600  # outlasts several pre-loop blaze ticks
# record_spell_damage() (spell_pa.cpp): a blaze tick names the occupant as its own attacker.
BLAZE_TICK_ON_VICTIM = re.compile(r"spell=blaze, damage=\d+, from Harnvictim\(\d+\) to Harnvictim\(")
# die() (fight.cpp) logs this only when somebody is credited with the kill.
VICTIM_CREDITED_DEATH = "Harnvictim killed by"


def test_blaze_ticks_survive_the_casters_quit_and_credit_nobody(server, imp, mage, victim, harness) -> None:
    imp.command(f"goto {fixtures.ROOM_ARENA_CENTRE}")
    imp.command("transfer harnmage")
    imp.command("transfer harnvictim")
    imp.command("restore harnmage")
    # Real-time blaze ticks (up to ~130 each at Harnmage's level) land while the victim walks in,
    # before tick_until_marker's first refloor; a default 72-hit pool dies to one of them and
    # that refloor then lands on the respawned body, spoiling the post-death hit reading.
    imp.command(f"wizset harnvictim maxhit {PRE_LOOP_SURVIVABLE_HIT}")
    imp.command("restore harnvictim")

    victim.command("west")  # keep the victim out of the cast itself so nobody engages anybody
    mage.cast("blaze", success_markers=BLAZE_CAST)
    mage.quit()

    before = imp.command("stat harnvictim").abilities()
    assert before is not None
    victim.command("east")
    victim.expect_room("Arena Centre")

    # `imp` stands in the blazing room for every forced tick this loop issues and is otherwise
    # never healed; at Harnmage's raised level (fixtures.py) a tick can kill it outright, and its
    # auto-respawn to Immortal Start would then make room_still_burning() read the wrong room.
    tick_until_marker(harness, imp, victim, DEATH_MARKER, protect=(imp,), refloor=("harnvictim", LETHAL_HIT))
    victim.expect_room("Wood-elf Start")

    victim_records = records.read_exploits(server.lib_dir, "Harnvictim")
    assert not any(record.victim_name.lower() == "harnmage" for record in victim_records), f"a departed caster must never be named: {victim_records}"

    stat = imp.command("stat harnvictim")
    current, maximum = stat.hit_points()
    assert maximum // 4 <= current <= maximum // 4 + poison_support.REGEN_ALLOWANCE, (
        f"an uncredited tick death takes the gentle arm: hit must be max/4 plus regen, got {current}/{maximum}: {stat.text}"
    )
    after = stat.abilities()
    assert after == before, f"the gentle arm leaves abilities untouched: {before} -> {after}"
    assert not any(record.type == records.EXPLOIT_MOBDEATH for record in victim_records), f"nobody was credited, so no mob-death record: {victim_records}"


def test_blaze_death_while_fighting_a_mob_stays_gentle_when_the_caster_is_gone(server, imp, mage, victim, harness) -> None:
    """The victim is swinging at a defanged brute when the lethal tick lands. With no caster to
    credit, the death is not the brute's: gentle arm, no mob-death record, no full XP loss."""
    imp.command(f"goto {fixtures.ROOM_ARENA_CENTRE}")
    imp.command("transfer harnmage")
    imp.command("transfer harnvictim")
    imp.command("restore harnmage")
    imp.command(f"wizset harnvictim maxhit {PRE_LOOP_SURVIVABLE_HIT}")  # see the first test
    imp.command("restore harnvictim")
    victim.command("west")
    mage.cast("blaze", success_markers=BLAZE_CAST)
    mage.quit()

    imp.command(f"load mob {BRUTE_ORC_VNUM}")
    neutralize_melee(imp, "brute")
    # Blaze burns the brute too; a deep pool keeps it alive and engaged for the whole tick loop.
    imp.command("wizset brute maxhit 4000")
    imp.command("wizset brute hit 4000")
    before = imp.command("stat harnvictim").abilities()
    assert before is not None
    victim.command("east")
    victim.expect_room("Arena Centre")
    victim.command("kill brute")
    wait_for_engagement(imp, "brute", "Harnvictim")

    tick_until_marker(harness, imp, victim, DEATH_MARKER, protect=(imp,), refloor=("harnvictim", LETHAL_HIT))
    victim.expect_room("Wood-elf Start")

    stat = imp.command("stat harnvictim")
    current, maximum = stat.hit_points()
    assert maximum // 4 <= current <= maximum // 4 + poison_support.REGEN_ALLOWANCE, stat.text
    assert stat.abilities() == before
    victim_records = records.read_exploits(server.lib_dir, "Harnvictim")
    assert not any(record.type == records.EXPLOIT_MOBDEATH for record in victim_records), victim_records
    assert not any(record.victim_name.lower() == "harnmage" for record in victim_records), victim_records


def test_blaze_ticks_credit_nobody_while_the_caster_sits_at_the_menu(server, imp, mage, victim, harness) -> None:
    """The mage quits but keeps its connection at the character menu, so its body is still
    registered when the lethal tick lands. It is out of any room, so it must not be credited,
    and the victim takes the gentle arm."""
    imp.command(f"goto {fixtures.ROOM_ARENA_CENTRE}")
    imp.command("transfer harnmage")
    imp.command("transfer harnvictim")
    imp.command("restore harnmage")
    imp.command(f"wizset harnvictim maxhit {PRE_LOOP_SURVIVABLE_HIT}")  # see the first test
    imp.command("restore harnvictim")
    victim.command("west")
    mage.cast("blaze", success_markers=BLAZE_CAST)
    mage.quit_to_menu()
    # close() both frees the parked body and marks the session closed, so fixture teardown
    # does not send a second quit into the menu.
    try:
        before = imp.command("stat harnvictim").abilities()
        assert before is not None
        victim.command("east")
        victim.expect_room("Arena Centre")
        tick_until_marker(harness, imp, victim, DEATH_MARKER, protect=(imp,), refloor=("harnvictim", LETHAL_HIT))
        victim.expect_room("Wood-elf Start")

        victim_records = records.read_exploits(server.lib_dir, "Harnvictim")
        assert not any(record.victim_name.lower() == "harnmage" for record in victim_records), f"a parked caster must never be named: {victim_records}"
        mage_records = records.read_exploits(server.lib_dir, "Harnmage")
        assert not any(record.type == records.EXPLOIT_PK for record in mage_records), f"a parked caster earns no kill: {mage_records}"
        # stat harnmage cannot be read: the parked body is in no room.
        stat = imp.command("stat harnvictim")
        current, maximum = stat.hit_points()
        assert maximum // 4 <= current <= maximum // 4 + poison_support.REGEN_ALLOWANCE, stat.text
        assert stat.abilities() == before
    finally:
        mage.close()


def _stat_victim(imp: GameSession) -> Transcript:
    """`stat harnvictim` from the burning room, retried past a tick broadcast's early prompt."""
    replies = stat_replies(imp, "harnvictim", lambda text: "'harnvictim'" in text.lower() and Transcript(text).hit_points() is not None)
    stat = Transcript(replies[-1])
    assert stat.hit_points() is not None, f"stat harnvictim never returned a parseable reply: {replies}"
    return stat


def test_blaze_death_while_fighting_a_player_records_that_player_when_the_caster_is_gone(server, imp, mage, victim, caller, harness) -> None:
    """Harncaller's blaze kills Harnvictim, who is fighting Harnmage, after Harncaller has quit.
    damage_credited() (fight.cpp) keeps the killer null for a player victim of an uncredited
    non-poison tick (death_credit_falls_back_to_opponent()), so the victim takes the gentle arm,
    yet die() still builds kill_contributors() from everyone fighting the victim: the victim's
    death entry names Harnmage and Harnmage earns the PK trophy. No gtest can drive die() with a
    player victim (fight_credit_tests.cpp, the comment above the kill_contributor_list tests).

    Harncaller's blaze lasts 33 or 34 units (spell_blaze(), mage.cpp: get_mage_caster_level() is
    mage 30 plus INT 18 / 5), and every real-time sweep spends one, so all setup that does not
    need the fire runs before the cast. tick_until_marker's 12 forced ticks, with about one
    real-time sweep per iteration and the few before the loop, spend about 26 of those units."""
    imp.command(f"wizset harnvictim level {VICTIM_LEVEL}")
    for name in ("harnmage", "harnvictim"):
        imp.command(f"wizset {name} maxhit {PRE_LOOP_SURVIVABLE_HIT}")  # see the first test
        imp.command(f"restore {name}")
    neutralize_melee(imp, "harnmage")
    before = _stat_victim(imp).abilities()
    assert before is not None

    mage.command("west")
    victim.command("west")
    mage.expect_room("Arena West")
    victim.expect_room("Arena West")
    # Alone in Arena Centre, the burst engages nobody, so no anger blocks the caller's quit.
    caller.cast("blaze", success_markers=BLAZE_CAST)
    caller.quit()
    # close_socket() (comm.cpp) frees the body only on this line; before it, a parked body exists.
    wait_for_log_line(imp, server, "Losing player: Harncaller")

    imp.command(f"goto {fixtures.ROOM_ARENA_CENTRE}")
    mage.command("east")
    victim.command("east")
    mage.expect_room("Arena Centre")
    victim.expect_room("Arena Centre")
    mage.command("kill elf")  # the mage sees Harnvictim only as "*an Elf*"
    wait_for_engagement(imp, "harnmage", "Harnvictim")

    tick_until_marker(harness, imp, victim, DEATH_MARKER, protect=(imp, mage), refloor=("harnvictim", LETHAL_HIT))
    victim.expect_room("Wood-elf Start")

    stat = _stat_victim(imp)
    current, maximum = stat.hit_points()
    assert maximum // 4 <= current <= maximum // 4 + poison_support.REGEN_ALLOWANCE, (
        f"nobody is credited, so the death takes the gentle arm: hit must be max/4 plus regen, got {current}/{maximum}: {stat.text}"
    )
    after = stat.abilities()
    assert after == before, f"the gentle arm leaves abilities untouched: {before} -> {after}"

    game_log = server.handle.log_path.read_text(encoding="latin-1", errors="replace")
    assert BLAZE_TICK_ON_VICTIM.search(game_log), f"the log must show the blaze ticking on the victim: {game_log[-2000:]}"
    # A kill by Harnmage's melee, or a credit falling back to it, would log this line.
    assert VICTIM_CREDITED_DEATH not in game_log, f"the lethal tick must credit nobody: {game_log[-2000:]}"

    victim_records = records.read_exploits(server.lib_dir, "Harnvictim")
    killed_by = [record.victim_name.lower() for record in victim_records if record.type == records.EXPLOIT_DEATH]
    assert killed_by == ["harnmage"], (
        f"the uncredited death still names the player fighting the victim, and only that player "
        f"(never the departed caster): {victim_records}"
    )
    mage_records = records.read_exploits(server.lib_dir, "Harnmage")
    trophies = [record.victim_name.lower() for record in mage_records if record.type == records.EXPLOIT_PK]
    assert trophies == ["harnvictim"], f"the player fighting the victim keeps its PK trophy though nobody was credited: {mage_records}"
    caller_records = records.read_exploits(server.lib_dir, "Harncaller")
    assert not any(record.type == records.EXPLOIT_PK for record in caller_records), f"the departed caster earns no trophy: {caller_records}"

    quit_once_anger_allows(mage, harness)  # attacking the victim angered the mage
