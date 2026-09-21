"""manual-test-plan.md item 2, punishment cases (a) and (b): a mob's poison, then death alone
(gentle: hit == max/4, mana 0, stats unchanged, EXPLOIT_POISON only) versus death still engaged
with the snake (harsh: hit == 1, stats scaled 2/3, EXPLOIT_MOBDEATH naming the snake).

Harness mode does not disable the server's real-time combat or regen: comm.cpp calls
perform_violence() every game pulse (~0.25s) regardless of harness mode, which drives combat
rounds and the room-mismatch disengagement check (fight.cpp's stop_fighting, see the gentle
row's disengagement wait below); separately, its PULSE_FAST_UPDATE block (every 12 pulses,
~3s) drives fast_update()'s hit/mana/move regen and, outside harness mode, the real-time
affect sweep. What harness mode changes is the *poison DoT*: it is a slow affect that only
ages on its own matching phase, so `harness affects` (poison_support.affect_ticks_until_death)
forces the ticks that carry the victim to death instead of waiting on wall-clock time.

The snake's bite (spec_pro.cpp SPECIAL(snake)) only binds at all because the harness world's
tests/integration/world/mob/11.mob record for #1131 sets MOB_SPEC (act-flags bit 0); without it
mobact.cpp's one_mobile_activity() never reaches spec_ass.cpp's virt_program_number() lookup for
its store_prog_number, and the special silently never fires (no ASSIGNMOB entry was needed --
the dispatch is data-driven once MOB_SPEC is set, per mobact.cpp:116-134).
"""

from __future__ import annotations

import time

import pytest

import poison_support
from poison_support import POISON_LANDED, POISON_RESISTED, affect_ticks_until_death, death_tick_budget
from rots_harness import fixtures, records
from rots_harness.session import GameSession

pytestmark = pytest.mark.scenario

BITE_MARKER = "bites you!"  # spec_pro.cpp SPECIAL(snake)
POISON_WAIT_BUDGET = 90.0  # wall-clock seconds tolerated for the snake to land a bite


def _wait_for_snake_poison_to_land(victim: GameSession, budget: float = POISON_WAIT_BUDGET) -> str:
    """Waits out resisted bites until one lands, rather than assuming the first one does.

    A landed bite still runs spell_poison()'s saves_poison() roll (spell_pa.cpp); wizset-ing the
    snake's level (see _engage_snake_until_poisoned) only raises the odds, it does not force a
    land. This mirrors poison_support.poison_until_it_lands()'s retry idiom for the player-cast
    scenarios, adapted for a bite the harness cannot re-trigger on demand (it is driven by the
    mob's own real-time mobile_activity() pulse, not a command this session can repeat).
    """
    deadline = time.monotonic() + budget
    accumulated = ""
    while time.monotonic() < deadline:
        remaining = deadline - time.monotonic()
        try:
            text = victim.expect(POISON_LANDED + POISON_RESISTED, timeout=min(remaining, 30.0))
        except AssertionError:
            continue
        accumulated += text
        if POISON_LANDED[0] in text:
            return accumulated
    pytest.fail(f"the snake never landed a poisoned bite within {budget}s: {accumulated[-2000:]}")


def _wait_for_disengagement(imp, names: tuple[str, ...], timeout: float = 10.0) -> None:
    """Waits for every name's `stat` to read `Fighting: Nobody`, not just for a transfer to
    have happened.

    char_from_room/char_to_room/do_trans (called by the wizard `transfer`) never touch
    specials.fighting -- disengagement only happens on a later violence pulse, in
    perform_violence's per-fighter room-mismatch branch ("Not in same room" ->
    stop_fighting(fighter), fight.cpp ~3084), which runs once per game pulse (~0.25s) for
    every entry in the global combat_list. find_engaged_real_mob (fight.cpp ~1024) walks that
    same combat_list at the death instant, so the gentle classification needs both the
    victim's and the snake's fighting pointers cleared before the lethal forced tick, not
    merely the transfer having moved the victim's room.
    """
    deadline = time.monotonic() + timeout
    pending = list(names)
    last_text = ""
    while pending:
        for name in list(pending):
            stat = imp.command(f"stat {name}")
            last_text = stat.text
            if "Fighting: Nobody" in stat.text:
                pending.remove(name)
        if not pending:
            return
        if time.monotonic() >= deadline:
            pytest.fail(f"{', '.join(pending)} never disengaged from combat within {timeout}s: {last_text}")
        imp.drain(0.5)


def _engage_snake_until_poisoned(imp, victim) -> dict[str, int]:
    imp.command(f"goto {fixtures.ROOM_CORRIDOR_ONE}")
    imp.command("transfer harnvictim")
    # Bite chance is number(0, 42 - level) < min(1 + level/4, 4) (spec_pro.cpp SPECIAL(snake)):
    # level 89 makes every violence round in which the bite check runs a bite attempt. Level 89
    # is also load-bearing for a second reason: get_naked_willpower() (utility.cpp) is
    # GET_PROF_LEVEL(cleric, mob) + GET_WILL(mob), and GET_PROF_LEVEL (utils.h) returns
    # GET_LEVEL(ch) outright for any NPC -- so a mob's *naked willpower* tracks its level, not
    # its fixed WIL ability (10 in this .mob file). saves_poison() (spell_pa.cpp) rolls the
    # snake's (willpower * 8 * perception) offence against the wood-elf victim's (con*5 +
    # willpower*3 + a flat +30 race bonus) defense; at the level the bite-frequency check alone
    # needs (40, naked willpower 50) the offence's range never reaches the defense's range and
    # the bite can never land -- confirmed empirically (zero landings across 9 resisted bites in
    # 175s of real time).
    #
    # Setting the level alone is not enough: naked willpower is only *recomputed* by
    # affect_naked() (handler.cpp), which affect_total()'s default mode calls unconditionally
    # (unlike recalc_abilities(), which skips NPCs) -- but wizset's "level" field (case 34,
    # act_wiz.cpp) never calls affect_total() at all, so a mob's cached willpower is whatever it
    # was at spawn (level 10 here) until something else forces the recompute. The "will" wizset
    # below is that trigger: it cannot change an NPC's WIL ability (recalc_abilities() skips
    # them), but it does call affect_total(), which reads the just-raised level and recomputes
    # willpower to 99 -- confirmed empirically (stat snake mid-fight) and load-bearing: without
    # it, the snake keeps its spawn-time willpower (20) and the bite never lands (zero landings
    # across 3+ resisted bites per run, in three separate runs, some run past 300s). With it,
    # 99 lands reliably against this fixed-seed harness (landed on the first bite, ~8.5-30s in,
    # across six separate runs) while _wait_for_snake_poison_to_land still tolerates a resisted
    # one for robustness against any future change to this sequence's RNG consumption.
    imp.command("wizset snake level 89")
    imp.command("wizset snake will 89")
    imp.command("wizset harnvictim maxhit 400")
    imp.command("restore harnvictim")
    before = imp.command("stat harnvictim").abilities()
    assert before is not None
    victim.command("kill snake")
    text = _wait_for_snake_poison_to_land(victim)
    assert BITE_MARKER in text, text
    return before


def test_mob_poison_death_alone_is_gentle(server, imp, victim, harness) -> None:
    before = _engage_snake_until_poisoned(imp, victim)
    # Two rooms away and out of the fight: the transfer itself only moves the victim's room;
    # the next violence pulse's room-mismatch check is what actually stops both sides (see
    # _wait_for_disengagement).
    imp.command(f"goto {fixtures.ROOM_ARENA_EAST}")
    imp.command("transfer harnvictim")
    victim.expect_room("Arena East")
    _wait_for_disengagement(imp, ("snake", "harnvictim"))
    imp.command("wizset harnvictim hit 10")

    assert affect_ticks_until_death(harness, victim, death_tick_budget(10)), "the victim should die of the poison alone"
    victim.expect_room("Wood-elf Start")

    stat = imp.command("stat harnvictim")
    current, maximum = stat.hit_points()
    pinned = maximum // 4
    assert pinned <= current <= pinned + poison_support.REGEN_ALLOWANCE, (
        f"gentle poison death revives at a quarter of {maximum} HP ({pinned}), plus up to "
        f"{poison_support.REGEN_ALLOWANCE} for real-time regen since the death tick, got {current}: {stat.text}"
    )
    after = stat.abilities()
    assert after == before, f"gentle death must not scale stats: {before} -> {after}"

    victim_records = records.read_exploits(server.lib_dir, "Harnvictim")
    types = [record.type for record in victim_records]
    assert records.EXPLOIT_POISON in types, victim_records
    assert records.EXPLOIT_MOBDEATH not in types, victim_records
    assert records.EXPLOIT_PK not in types and records.EXPLOIT_DEATH not in types, victim_records


def test_death_while_still_fighting_the_snake_is_harsh(server, imp, victim, harness) -> None:
    before = _engage_snake_until_poisoned(imp, victim)
    imp.command("wizset harnvictim hit 10")  # still fighting; the next tick or bite kills

    assert affect_ticks_until_death(harness, victim, death_tick_budget(10) + 4), "the victim should die engaged with the snake"
    victim.expect_room("Wood-elf Start")

    stat = imp.command("stat harnvictim")
    current, _maximum = stat.hit_points()
    assert 1 <= current <= 1 + poison_support.REGEN_ALLOWANCE, (
        f"harsh poison death revives at 1 HP, plus up to {poison_support.REGEN_ALLOWANCE} for "
        f"real-time regen since the death tick, got {current}: {stat.text}"
    )
    after = stat.abilities()
    assert after is not None
    for name, value in before.items():
        assert after[name] == value * 2 // 3, f"{name}: {value} -> {after[name]} (expected {value * 2 // 3})"

    victim_records = records.read_exploits(server.lib_dir, "Harnvictim")
    mob_deaths = [record for record in victim_records if record.type == records.EXPLOIT_MOBDEATH]
    assert any("snake" in record.victim_name.lower() for record in mob_deaths), victim_records
