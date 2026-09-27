"""A second, equal mystic poison extends the running one: the victim hears the extension line and
not the fresh-poison line, the duration rises by half the new cast's duration but never past the
running poison's initial duration, the poisoner still takes the kill, and a running resist poison
affect stays in place and keeps shortening the extended poison.

Mechanism. spell_poison() (mystic.cpp) builds the affect through poison_victim_affect() and hands
it to apply_poison() (poison.cpp). With an equal strength (-2 STR) and a duration no longer than
initial_duration_of() the running poison, apply_poison() raises the running duration by half the
new one, capped at that initial duration, and keeps a poisoner that resolve_poisoner() still finds
(hand_over_record_if_poisoner_gone()); send_poison_outcome_messages() then sends the extension line
in place of "You feel very sick.". tick_poison_affect() and deal_poison_tick_damage() credit each
tick to the resolved poisoner through damage_credited() (fight.cpp), whose death writes the PK and
exploit records. start_poison_resistance() adds a separate SPELL_RESIST_POISON affect, which the
extension never touches. Both casts here are Harnmage's, so the kill credit shows the poisoner
survives an extension; whether the record is kept or re-recorded is pinned by the gtests in
poison_rules_tests.cpp, which use two different sources.

Determinism. get_mystic_caster_level() (mystic.cpp) adds wil / 5 and, when (wil / 5) % 5 is
non-zero, a random extra level, so a second cast could last one tick longer than the first and
replace it. make_every_cast_land_alike() (poison_support.py) sets Harnmage's willpower to 25,
which gives no random level. saves_poison() (poison.cpp) lets the victim shrug a cast off when
number(offence / 3, offence) < number(defense / 2, defense); the same helper sets Harnvictim's
willpower to 0 and asserts from `stat` that offence / 3 >= defense, so every cast lands
(spell_poison()'s magus_save is always 0).

Timing. The real-time fast block still runs in harness mode: game_loop() (comm.cpp) gates only the
hourly block on harness_mode and calls affect_update() every PULSE_FAST_UPDATE pulses, and
affect_update_person() (limits.cpp) ticks a slow affect whenever get_current_time_phase()
(utility.cpp) matches the phase affect_to_char() (handler.cpp) recorded when it landed. That phase
repeats every 60 s, and an extension changes the running affect in place, so the poison's own tick
fires at most once in the landing pulse and then not again for at least 57 s. With resist poison
running, that tick would take 1 + the resist modifier, not 1. Every duration this file compares
is therefore read within SLOW_TICK_FREE_WINDOW (poison_support.py) seconds of the first landing,
and each test checks the elapsed time before its exact duration assertions, failing with that
cause if it ran over.

A gtest cannot drive the cast path's choice of message, a real second cast from a connected
caster, a death from a forced tick written to the on-disk records, or the resist affect across
the cast path.

Bounds. The first poison is cast by poison_until_it_lands() (at most eight casts, 12 s each); the
window opens when it lands. Inside the window: _return_to_arena_centre()'s
wait_for_disengagement() (combat_support.py, 10 s), TICKS_BEFORE_EXTENSION forced ticks and the
extension cast (one, 12 s); the resist test adds the resist cast's mage.cast() retries (at most
six, 12 s each), move_out_of_the_fight()'s wait_for_disengagement() (10 s) and one forced tick.
A cast is lost to concentration about once in 101 (do_cast, spell_pa.cpp: number(0, 100) >= 100
at full knowledge). A lost poison cast, first or extension, reaches the victim with no line, so
the test fails on the 12 s wait with the caster's transcript. A lost resist cast makes mage.cast()
cast again 12 s later; if the retries push the reads past the window, assert_no_slow_tick_yet()
fails with that cause. The poison lasts 36 forced ticks; the setup
spends TICKS_BEFORE_EXTENSION of them and the death needs death_tick_budget().
"""

from __future__ import annotations

import time

import pytest

from combat_support import VICTIM_LEVEL, move_out_of_the_fight, quit_once_anger_allows, read_affect_listing
from poison_support import (
    MAGE_MYSTIC_LEVEL,
    POISON_BLOCKED,
    POISON_EXTENDED,
    POISON_LANDED,
    POISON_RESISTED,
    RESIST_POISON_CASTER,
    RESIST_POISON_STARTED,
    SpellAffect,
    affect_ticks_until_death,
    assert_no_slow_tick_yet,
    death_tick_budget,
    make_every_cast_land_alike,
    poison_until_it_lands,
    spell_affects,
)
from rots_harness import fixtures, records
from rots_harness.session import GameSession, SessionTimeout

pytestmark = pytest.mark.scenario

# Leaves the poison two below its initial duration: the extension visibly raises it, and adding
# half a cast's duration would overshoot the initial duration if nothing capped it.
TICKS_BEFORE_EXTENSION = 2
VICTIM_CAST_HIT = 60  # re-floored before the extension: the casts, the fight and the ticks cost hit points
LETHAL_HIT = 10
CAST_OUTCOMES = POISON_LANDED + POISON_EXTENDED + POISON_RESISTED + POISON_BLOCKED


def _gather_in_arena_centre(imp: GameSession, mage: GameSession, victim: GameSession) -> None:
    imp.command(f"goto {fixtures.ROOM_ARENA_CENTRE}")
    imp.command("transfer harnmage")
    imp.command("transfer harnvictim")
    mage.expect_room("Arena Centre")
    victim.expect_room("Arena Centre")
    imp.command(f"wizset harnvictim level {VICTIM_LEVEL}")  # Big Brother: caster 30 < 3 * 11


def _only_poison(stat_text: str) -> SpellAffect:
    poisons = spell_affects(stat_text, "poison")
    assert len(poisons) == 1, f"exactly one poison affect must be running: {stat_text}"
    return poisons[0]


def _return_to_arena_centre(imp: GameSession, mage: GameSession) -> None:
    """Ends the fight a poison cast started and brings the mage and the imp back to Arena Centre,
    so the next cast is not made from melee."""
    move_out_of_the_fight(imp, mage, "harnmage", "harnvictim")
    imp.command(f"goto {fixtures.ROOM_ARENA_CENTRE}")
    imp.command("transfer harnmage")
    mage.expect_room("Arena Centre")


def _cast_the_extension(imp: GameSession, mage: GameSession, victim: GameSession) -> None:
    """Casts one more poison on the already-poisoned victim; it must land as an extension. The
    fight the cast started is still running when this returns."""
    imp.command(f"wizset harnvictim hit {VICTIM_CAST_HIT}")
    victim.command("look")  # clears any AFK flag so Big Brother does not shield the victim
    mage.send_line("cast 'poison' elf")  # the mage sees Harnvictim only as "*an Elf*"
    try:
        text = victim.expect(CAST_OUTCOMES, timeout=12.0)
    except SessionTimeout as timeout:
        pytest.fail(f"{timeout}\ncaster side:\n{mage.drain(0.5)[-1500:]}")
    if POISON_RESISTED[0] in text:
        pytest.fail(f"the victim shrugged off the second poison although no save could succeed: {text}")
    if POISON_LANDED[0] in text:
        pytest.fail(f"an equal poison restarted the running one instead of extending it: {text}")
    if POISON_BLOCKED[0] in text:
        pytest.fail(f"an equal poison was refused as weaker than the running one: {text}")
    assert POISON_EXTENDED[0] in text, f"the second poison must be extended: {text}"


def _land_the_first_poison(imp: GameSession, mage: GameSession, victim: GameSession, cast_duration: int) -> float:
    """Lands the first poison, checks its duration, ends the fight, and returns the monotonic time
    the landing line arrived. The fast block of the landing pulse may already have ticked the new
    poison once, so the duration is the cast's or one less."""
    poison_until_it_lands(mage, victim, "elf")
    landed_at = time.monotonic()
    first = _only_poison(read_affect_listing(imp, "harnvictim"))
    assert cast_duration - 1 <= first.duration <= cast_duration, (
        f"precondition: the first poison lasts level + 1 = {cast_duration} ticks, less a landing-pulse tick: {first}"
    )
    _return_to_arena_centre(imp, mage)
    return landed_at


def _age_the_poison(imp: GameSession, harness, cast_duration: int) -> int:
    """Forces TICKS_BEFORE_EXTENSION affect ticks and returns the poison's remaining duration."""
    for _tick in range(TICKS_BEFORE_EXTENSION):
        harness.affects()
    aged = _only_poison(read_affect_listing(imp, "harnvictim")).duration
    assert aged <= cast_duration - TICKS_BEFORE_EXTENSION, (
        f"precondition: {TICKS_BEFORE_EXTENSION} forced ticks must shorten the poison below {cast_duration}: {aged}"
    )
    assert aged + cast_duration // 2 > cast_duration, (
        f"precondition: an uncapped extension of {aged} by {cast_duration // 2} must overshoot the cap {cast_duration}"
    )
    return aged


def _assert_extended_to_the_cap(extended: SpellAffect, aged: int, cast_duration: int) -> None:
    assert extended.duration == cast_duration, (
        f"an extension of {aged} by {cast_duration // 2} must stop at the running poison's initial duration "
        f"{cast_duration}: got {extended.duration}"
    )


def test_an_equal_poison_extends_and_its_caster_takes_the_kill(server, imp, mage, victim, harness) -> None:
    _gather_in_arena_centre(imp, mage, victim)
    cast_duration = make_every_cast_land_alike(imp)
    landed_at = _land_the_first_poison(imp, mage, victim, cast_duration)
    aged = _age_the_poison(imp, harness, cast_duration)

    _cast_the_extension(imp, mage, victim)
    extended = _only_poison(read_affect_listing(imp, "harnvictim"))
    assert_no_slow_tick_yet(landed_at)
    _assert_extended_to_the_cap(extended, aged, cast_duration)

    # Out of the room, so only the forced poison ticks can kill the victim.
    move_out_of_the_fight(imp, mage, "harnmage", "harnvictim")
    imp.command(f"wizset harnvictim hit {LETHAL_HIT}")
    assert affect_ticks_until_death(harness, victim, death_tick_budget(LETHAL_HIT)), (
        "the victim should have died of the forced ticks of the extended poison"
    )

    victim_records = records.read_exploits(server.lib_dir, "Harnvictim")
    types = [record.type for record in victim_records]
    assert records.EXPLOIT_POISON in types, f"the death must be recorded as a poison death: {victim_records}"
    deaths = [record for record in victim_records if record.type == records.EXPLOIT_DEATH]
    assert any(record.victim_name.lower() == "harnmage" for record in deaths), (
        f"the extended poison's death must name its poisoner: {victim_records}"
    )
    mage_records = records.read_exploits(server.lib_dir, "Harnmage")
    kills = [record for record in mage_records if record.type == records.EXPLOIT_PK]
    assert any(record.victim_name.lower() == "harnvictim" for record in kills), (
        f"the poisoner must be credited with the kill: {mage_records}"
    )

    quit_once_anger_allows(mage, harness)  # the poison casts angered the caster


def test_an_extension_leaves_resist_poison_in_place(server, imp, mage, victim, harness) -> None:
    _gather_in_arena_centre(imp, mage, victim)
    cast_duration = make_every_cast_land_alike(imp)
    landed_at = _land_the_first_poison(imp, mage, victim, cast_duration)
    _age_the_poison(imp, harness, cast_duration)

    mage.cast("resist poison", "elf", success_markers=(RESIST_POISON_CASTER,))
    victim.expect((RESIST_POISON_STARTED,))
    resisting = read_affect_listing(imp, "harnvictim")
    resists = spell_affects(resisting, "resist poison")
    assert len(resists) == 1 and resists[0].modifier == MAGE_MYSTIC_LEVEL, (
        f"precondition: one resist affect with the caster's mystic level {MAGE_MYSTIC_LEVEL}: {resisting}"
    )
    aged = _only_poison(resisting).duration

    _cast_the_extension(imp, mage, victim)
    extended_text = read_affect_listing(imp, "harnvictim")
    move_out_of_the_fight(imp, mage, "harnmage", "harnvictim")
    harness.affects()
    ticked_text = read_affect_listing(imp, "harnvictim")
    assert_no_slow_tick_yet(landed_at)

    extended = _only_poison(extended_text)
    _assert_extended_to_the_cap(extended, aged, cast_duration)
    resists_after = spell_affects(extended_text, "resist poison")
    assert len(resists_after) == 1, f"the extension must leave exactly one resist affect: {extended_text}"
    assert resists_after[0].modifier == MAGE_MYSTIC_LEVEL, f"the extension must not change the resist modifier: {extended_text}"

    ticked = spell_affects(ticked_text, "poison")
    ticked_resists = spell_affects(ticked_text, "resist poison")
    assert len(ticked) == 1 and len(ticked_resists) == 1, f"both affects must still be listed after one tick: {ticked_text}"
    # affect_update_person() (limits.cpp) takes 1 and tick_poison_affect() (poison.cpp) the modifier.
    expected = max(extended.duration - 1 - MAGE_MYSTIC_LEVEL, 0)
    assert ticked[0].duration == expected, (
        f"the resist affect must still shorten the extended poison by 1 + {MAGE_MYSTIC_LEVEL} a tick: "
        f"{extended.duration} -> {ticked[0].duration}, expected {expected}: {ticked_text}"
    )
    assert ticked_resists[0].duration == ticked[0].duration, f"the resist affect must follow the poison: {ticked_text}"

    quit_once_anger_allows(mage, harness)  # the poison casts angered the caster
    quit_once_anger_allows(victim, harness)  # fighting back before the fight ended angered the victim
