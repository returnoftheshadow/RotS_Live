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

Every bite lands, whatever the RNG draws. spell_poison() resists a bite when saves_poison()
(poison.cpp) finds number(offence / 3, offence) < number(defense / 2, defense), with offence =
snake willpower * 8 * perception / 100 and defense = victim CON * 5 + willpower * 3 + 30 for a wood
elf. The #1131 record gives the snake perception 100 and the setup raises its willpower to 99, so
offence is 792 and its lowest roll, 264, beats Harnvictim's highest defence, 139 (CON 11,
willpower 18). The setup reads all four values through `stat` and asserts offence / 3 > defense
before the fight starts; the bite wait (90s of wall-clock time for the snake's own violence rounds)
names them if no bite lands.

The gentle row also takes only the tenth of die()'s experience loss (fight.cpp): the poison is
classified player_death, so death_takes_full_mob_xp_loss() withholds the full loss even though the
credited killer is the snake, a real mob. Its experience is set inside the victim's level band
after the fight ends, so no hit experience moves it, or the level that divides the loss, before the
death.
"""

from __future__ import annotations

import time
from dataclasses import dataclass

import pytest

import poison_support
from combat_support import stat_replies, wait_for_disengagement
from poison_support import POISON_LANDED, POISON_RESISTED, affect_ticks_until_death, death_loss, death_tick_budget, xp_to_level
from rots_harness import fixtures, records
from rots_harness.session import GameSession, Transcript

pytestmark = pytest.mark.scenario

BITE_MARKER = "bites you!"  # spec_pro.cpp SPECIAL(snake)
POISON_WAIT_BUDGET = 90.0  # wall-clock seconds tolerated for the snake to land a bite
WOOD_ELF_POISON_BONUS = 30  # saves_poison(), poison.cpp
# do_stat_character's first line (act_wiz.cpp), lower-cased: "MALE MOB 'a harness snake'  IDNum: ...".
SNAKE_STAT_HEADER = "mob 'a harness snake'"
VICTIM_STAT_HEADER = "pc 'harnvictim'"


@dataclass(frozen=True)
class PoisonSaveInputs:
    """The four values saves_poison() (poison.cpp) reads, as `stat` printed them."""

    snake_perception: int  # the snake's "Perception" on its stat line
    snake_willpower: int  # the snake's "Willpower" on its stat line
    victim_con: int  # the victim's current Con from its ability line
    victim_willpower: int  # the victim's "Willpower" on its stat line

    @classmethod
    def read(cls, snake_stat: Transcript, victim_stat: Transcript) -> PoisonSaveInputs:
        snake_perception, snake_willpower = snake_stat.perception_and_willpower()
        _victim_perception, victim_willpower = victim_stat.perception_and_willpower()
        return cls(snake_perception, snake_willpower, victim_stat.abilities()["con"], victim_willpower)

    @property
    def offence(self) -> int:
        return self.snake_willpower * 8 * self.snake_perception // 100

    @property
    def defense(self) -> int:
        return self.victim_con * 5 + self.victim_willpower * 3 + WOOD_ELF_POISON_BONUS

    def every_bite_lands(self) -> bool:
        return self.offence // 3 > self.defense

    def describe(self) -> str:
        return (
            f"snake perception {self.snake_perception}, willpower {self.snake_willpower}; "
            f"victim con {self.victim_con}, willpower {self.victim_willpower}; "
            f"offence roll {self.offence // 3}..{self.offence} against defence roll {self.defense // 2}..{self.defense}"
        )


def _own_stat(imp: GameSession, target: str, header: str) -> Transcript:
    """`stat <target>` cut to start at its own header, retried until that part carries both the
    ability line and the perception line; stale text ahead of the header is dropped."""

    def own_part(text: str) -> Transcript | None:
        start = text.lower().find(header)
        if start < 0:
            return None
        part = Transcript(text[start:])
        if part.abilities() is None or part.perception_and_willpower() is None:
            return None
        return part

    replies = stat_replies(imp, target, lambda text: own_part(text) is not None)
    part = own_part(replies[-1])
    assert part is not None, f"stat {target} never returned its own ability and perception lines: {replies}"
    return part


def _wait_for_snake_poison_to_land(victim: GameSession, inputs: PoisonSaveInputs, budget: float = POISON_WAIT_BUDGET) -> str:
    """Waits for the first bite to land, reading past any resisted one so a failure shows every
    bite the victim saw. The bite comes from the snake's own real-time mobile_activity() pulse,
    so the only bound is wall-clock time."""
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
    pytest.fail(f"the snake never landed a poisoned bite within {budget}s ({inputs.describe()}): {accumulated[-2000:]}")


def _engage_snake_until_poisoned(imp, victim) -> dict[str, int]:
    imp.command(f"goto {fixtures.ROOM_CORRIDOR_ONE}")
    imp.command("transfer harnvictim")
    # Bite chance is number(0, 42 - level) < min(1 + level/4, 4) (spec_pro.cpp SPECIAL(snake)):
    # level 89 makes every violence round's bite check a bite. The level also sets the snake's
    # willpower: get_naked_willpower() (utility.cpp) adds its WIL of 10 to GET_PROF_LEVEL (utils.h),
    # which is an NPC's own level, giving 99. affect_naked() (handler.cpp) recomputes it only inside
    # affect_total(), which wizset's "level" field never calls, so the "will" wizset follows as the
    # trigger: it cannot change an NPC's WIL (recalc_abilities() skips NPCs) but does call
    # affect_total(). Without it the snake keeps its spawn-time willpower of 20.
    imp.command("wizset snake level 89")
    imp.command("wizset snake will 89")
    imp.command("wizset harnvictim maxhit 400")
    imp.command("restore harnvictim")
    victim_stat = _own_stat(imp, "harnvictim", VICTIM_STAT_HEADER)
    inputs = PoisonSaveInputs.read(_own_stat(imp, "snake", SNAKE_STAT_HEADER), victim_stat)
    assert inputs.every_bite_lands(), f"the snake's lowest offence roll must beat the victim's highest defence roll: {inputs.describe()}"
    victim.command("kill snake")
    text = _wait_for_snake_poison_to_land(victim, inputs)
    assert BITE_MARKER in text, text
    return victim_stat.abilities()


def test_mob_poison_death_alone_is_gentle(server, imp, victim, harness) -> None:
    before = _engage_snake_until_poisoned(imp, victim)
    # Two rooms away and out of the fight: the transfer itself only moves the victim's room;
    # the next violence pulse's room-mismatch check is what actually stops both sides (see
    # combat_support.wait_for_disengagement).
    imp.command(f"goto {fixtures.ROOM_ARENA_EAST}")
    imp.command("transfer harnvictim")
    victim.expect_room("Arena East")
    wait_for_disengagement(imp, ("snake", "harnvictim"))
    level = server.spec("Harnvictim").level
    band_experience = (xp_to_level(level) + xp_to_level(level + 1)) // 2
    imp.command(f"wizset harnvictim exp {band_experience}")
    before_stat = imp.command("stat harnvictim")
    before_exp = before_stat.experience()
    assert before_exp == band_experience, before_stat.text
    imp.command("wizset harnvictim hit 10")

    assert affect_ticks_until_death(harness, victim, death_tick_budget(10)), "the victim should die of the poison alone"
    victim.expect_room("Wood-elf Start")

    stat = imp.command("stat harnvictim")
    after_exp = stat.experience()
    assert after_exp is not None, stat.text
    tenth_loss = death_loss(before_exp, level, full=False)
    assert before_exp - after_exp == tenth_loss, (
        f"a mob's poison death alone takes exactly the tenth ({tenth_loss}), not the full loss "
        f"({death_loss(before_exp, level, full=True)}): {before_exp} -> {after_exp}"
    )
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
