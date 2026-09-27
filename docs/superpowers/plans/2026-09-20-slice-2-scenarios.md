# Slice 2 Scenarios (Phase B of the slice 2 amendment) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make slow person affects deterministic under the harness (`harness affects`), drop the poison xfail, and automate every remaining row of the spec's scenario catalogue so the integration suite pins the branch's room-affect, poison, kill-credit, summon and earthquake behaviours end to end.

**Architecture:** One new harness subcommand and one flag gate the person-affect phase compare in `affect_update_person`; room affects stay probabilistic. The pytest side gains `Harness.affects()`, a fifth roster character (`Harncaller`, a human mage who shares a side with the victim so summon can succeed), a sink room under Arena West so the earthquake crevice opens deterministically, a `Transcript.abilities()` parser for the `stat` ability line, and a small `poison_support.py` shared by the poison scenarios. Each scenario is one file, marker-driven, asserting on transcripts and JSON records.

**Tech Stack:** C++17 server (`src/test_harness.*`, `src/limits.cpp` CRLF), GoogleTest (`ageland_tests`), Python 3.11 pytest harness under `tests/integration/`, i386 Docker container for local runs, `integration-asan` CI job as the sanitized gate.

**Spec:** `docs/superpowers/specs/2026-09-19-integration-test-harness-design.md`, section "Slice 2 amendment (owner, 2026-09-20)", Phase B (B1, B2, B3). Read it first; the plan argues from it. Phase A (`docs/superpowers/plans/2026-09-20-fix-wave.md`) must be complete and both CI jobs green before Task 1 here.

## Global Constraints

- Branch `fix/spell-room-affect-uaf-port`, worktree `.claude/worktrees/uaf-port`. Commit only your own files with an explicit pathspec: `git commit -m "..." -- <files>`. Never `git add -A`, never stash, never touch files you did not edit; report unexpected modified files.
- `src/limits.cpp` is CRLF. Edit it byte-preservingly (Python `read_bytes`/`write_bytes` with `\r\n` in the replaced fragment); `git diff --stat` and `git diff -w --stat` must match for it. Every other file in this plan is LF.
- `.clang-format` governs C++; meaningful names only; comments state a contract or reason once.
- Python: standard library plus pytest only; every wait is marker-driven with a timeout (`expect`, `command`, `cast`); `drain(seconds)` is the only fixed wait and is used to collect output, never to wait for an event.
- Server build for local runs (Docker is not shared today, no lock needed):
  ```sh
  docker compose run --rm -T rots bash -lc 'cd /rots && cmake --build build --target ageland ageland_tests -j8 && ./bin/tests --gtest_filter=<Filter>'
  ```
  A `test_harness.h` edit rebuilds every file that includes it (`limits.cpp`, `comm.cpp`, `interpre.cpp`); budget ten minutes.
- Scenario runs on this Mac use the Docker launcher and the venv pytest:
  ```sh
  ROTS_IT_LAUNCHER=docker build/integration-venv/bin/python -m pytest tests/integration/scenarios/<file>.py -q
  ```
  Host unit tests: `make integration-unit PYTHON=build/integration-venv/bin/python`. If the venv is missing: `python3 -m venv build/integration-venv && build/integration-venv/bin/pip install pytest`.
- Server message strings used as markers are copied verbatim from the source lines cited in each task; do not paraphrase them.
- Every scenario docstring cites its `manual-test-plan.md` item (section "The scenarios, mapped to each fix", items 1-6).
- Determinism contract (spec B1): each `affects()` call forces one tick per person affect; the wall-clock fast block still ticks affects on its own about once a minute, so scenarios assert monotonic outcomes (hit points strictly lower, death recorded), never exact tick counts or damage totals. Poison death budget: `ceil((hit + con // 2) / 5) + 2` calls; roster `con` is 11 (template), so with `wizset <name> hit 10` the budget is 5.
- Commit trailers, every commit:
  ```
  Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>
  Claude-Session: https://claude.ai/code/session_01E3mWDHen1U7cDdb2sQ634P
  ```

## File Structure

| File | Responsibility in this plan |
|---|---|
| `src/test_harness.h/.cpp` | `harness_force_affect_phase` flag; `harness affects` subcommand |
| `src/limits.cpp` (CRLF) | One-clause gate in `affect_update_person` |
| `src/tests/test_harness_tests.cpp` | gtest pinning the flag's effect and default |
| `tests/integration/conftest.py` | `Harness.affects()`, `caller` session fixture |
| `tests/integration/rots_harness/fixtures.py` | `Harncaller` roster entry, `ROOM_CREVICE_FLOOR` |
| `tests/integration/rots_harness/session.py` | `Transcript.abilities()` |
| `tests/integration/world/wld/11.wld` | Room 1136 and the DOWN exit from 1130 |
| `tests/integration/unit/test_libbuilder.py`, `test_session.py`, `test_world.py` | Roster count, ability parser, world exit |
| `tests/integration/scenarios/poison_support.py` | Shared poison markers and helpers |
| `tests/integration/scenarios/test_poison_remote_player.py` | Xfail removed; `affects()` loop |
| `tests/integration/scenarios/test_poison_attribution_when_poisoner_is_gone.py` | Quit / slain before the lethal tick |
| `tests/integration/scenarios/test_poison_punishment_snake.py` | Gentle (flee, die alone) and harsh (die fighting) |
| `tests/integration/scenarios/test_poison_punishment_player_poison_mob_fight.py` | Player poison, die fighting the brute |
| `tests/integration/scenarios/test_blaze_after_caster_gone.py` | Caster slain; caster link-drop plus relogin |
| `tests/integration/scenarios/test_kill_credit.py` | Non-engaged killing spell; splash bystander |
| `tests/integration/scenarios/test_summon.py` | Dark room by name; link-dead victim |
| `tests/integration/scenarios/test_earthquake_order.py` | Caster's fall line last |
| `tests/integration/scenarios/test_affect_expiry_with_death.py` | Death inside the same affect_update as a quit |
| `WIP.md` | Dated slice 2 status line (append only) |

---

### Task 1: `harness affects` subcommand, the person-affect gate, and the deterministic poison scenario (spec B1)

**Files:**
- Modify: `src/test_harness.h`, `src/test_harness.cpp` (`do_harness`, after the `tick` branch), `src/limits.cpp` (CRLF; `affect_update_person`, the `is_fast || (!mode && (time_phase == af->time_phase))` line ~1351)
- Test: `src/tests/test_harness_tests.cpp`
- Modify: `tests/integration/conftest.py` (`Harness`), `tests/integration/scenarios/test_poison_remote_player.py`
- Create: `tests/integration/scenarios/poison_support.py`

**Interfaces:**
- Produces: `extern int harness_force_affect_phase;` (C++); `Harness.affects() -> Transcript` (Python); `poison_support.poison_until_it_lands(caster, victim, target_word, attempts=8)`, `poison_support.affect_ticks_until_death(harness, victim, budget) -> bool`, `poison_support.death_tick_budget(hit, con=11) -> int`, constants `POISON_LANDED`, `POISON_RESISTED`, `DEATH_MARKER`, `BLAZE_CAST`.

- [ ] **Step 1: Write the failing gtest**

Append to `src/tests/test_harness_tests.cpp` (add `#include "../spells.h"` and `#include "../structs.h"` after the existing include, and these declarations after the includes):
```cpp
extern struct skill_data skills[];
void affect_update_person(struct char_data* i, int mode);
```
Then the test:
```cpp
namespace {

// A person affect whose switch arm in affect_update_person does nothing (SPELL_CURING), placed
// on a phase that never matches the test binary's pulse-0 phase, so only the harness flag can
// make it tick.
struct ForcedPhaseFixture {
    char_data character {};
    affected_type affect {};
    byte previous_is_fast = 0;

    ForcedPhaseFixture()
    {
        previous_is_fast = skills[SPELL_CURING].is_fast;
        skills[SPELL_CURING].is_fast = 0;
        affect.type = SPELL_CURING;
        affect.duration = 5;
        affect.time_phase = 1; // pulse is 0 in the test binary, so the live phase is 0
        character.affected = &affect;
    }

    ~ForcedPhaseFixture()
    {
        skills[SPELL_CURING].is_fast = previous_is_fast;
        harness_force_affect_phase = 0;
    }
};

} // namespace

TEST(HarnessAffects, FlagIsOffByDefault)
{
    EXPECT_EQ(harness_force_affect_phase, 0);
}

TEST(HarnessAffects, SlowAffectDoesNotTickOnAMismatchedPhaseWithoutTheFlag)
{
    ForcedPhaseFixture fixture;
    affect_update_person(&fixture.character, 0);
    EXPECT_EQ(fixture.affect.duration, 5);
}

TEST(HarnessAffects, FlagForcesExactlyOneTickPerCall)
{
    ForcedPhaseFixture fixture;
    harness_force_affect_phase = 1;
    affect_update_person(&fixture.character, 0);
    EXPECT_EQ(fixture.affect.duration, 4);
    affect_update_person(&fixture.character, 0);
    EXPECT_EQ(fixture.affect.duration, 3);
}
```

- [ ] **Step 2: Run it to verify it fails to compile**

Run: `docker compose run --rm -T rots bash -lc 'cd /rots && cmake --build build --target ageland_tests -j8 2>&1 | grep -m3 harness_force_affect_phase'`
Expected: undeclared identifier `harness_force_affect_phase`.

- [ ] **Step 3: Add the flag and the subcommand**

`src/test_harness.h`, after the `harness_mode` line:
```cpp
// 1 only while `harness affects` runs affect_update(): every slow person affect then treats
// the current time phase as matching, so one call is one tick per affect with no regen.
// Room affects keep their own rolls (spec B1).
extern int harness_force_affect_phase;
```
`src/test_harness.cpp`, after `int harness_mode = 0;`:
```cpp
int harness_force_affect_phase = 0;
```
and in `do_harness`, after the `tick` branch's closing brace and before the usage line:
```cpp
    if (argument && std::strncmp(argument, "affects", 7) == 0 && (argument[7] == '\0' || argument[7] == ' ')) {
        harness_force_affect_phase = 1;
        affect_update();
        clean_expose_elements();
        harness_force_affect_phase = 0;
        send_to_char("Harness: affect tick complete.\r\n", ch);
        return;
    }
```
Change the usage line to `send_to_char("Usage: harness tick | harness affects\r\n", ch);`.

- [ ] **Step 4: Gate the person-affect compare (byte-preserving)**

```sh
python3 - <<'EOF'
import pathlib
p = pathlib.Path('src/limits.cpp'); b = p.read_bytes()
old = b"        if (skills[af->type].is_fast || (!mode && (time_phase == af->time_phase))) {\r\n"
new = b"        if (skills[af->type].is_fast || (!mode && (time_phase == af->time_phase || harness_force_affect_phase))) {\r\n"
assert b.count(old) == 1, b.count(old)
p.write_bytes(b.replace(old, new))
EOF
git diff --stat src/limits.cpp; git diff -w --stat src/limits.cpp
```
Expected: one line changed in both stats. `limits.cpp` already includes `test_harness.h` (line ~23); confirm with `grep -n test_harness.h src/limits.cpp`.

- [ ] **Step 5: Build and run the gtests**

Run: `docker compose run --rm -T rots bash -lc 'cd /rots && cmake --build build --target ageland ageland_tests -j8 && ./bin/tests --gtest_filter="Harness*"'`
Expected: all `HarnessSeedTest.*` and `HarnessAffects.*` pass.

- [ ] **Step 6: Add `Harness.affects()` and the shared poison support**

In `tests/integration/conftest.py`, inside `class Harness` after `tick`:
```python
    def affects(self) -> Transcript:
        """One forced person-affect tick (spec B1): no regen, room affects untouched."""
        self._imp.drain(0.1)
        self._imp.send_line("harness affects")
        text = self._imp.expect(["Harness: affect tick complete."], 20.0)
        return Transcript(text)
```
Create `tests/integration/scenarios/poison_support.py`:
```python
"""Markers and loops shared by the poison scenarios (spec B1 determinism contract)."""

from __future__ import annotations

import pytest

from rots_harness.session import GameSession

POISON_LANDED = ("You feel very sick.",)  # spell_poison, mystic.cpp
POISON_RESISTED = ("You feel your body fend off the poison.",)
DEATH_MARKER = "You are dead!  Sorry..."  # fight.cpp damage()
BLAZE_CAST = ("You breathe out fire.",)
ROSTER_CON = 11  # tests/integration/fixtures/character.template.json abilities.con


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
```

- [ ] **Step 7: Rewrite the remote-poison scenario without the xfail**

Replace the whole of `tests/integration/scenarios/test_poison_remote_player.py` with:
```python
"""manual-test-plan.md item 2, control case: the poisoner stays online but out of the death
room; the victim dies of the tick alone and every record names the poisoner."""

from __future__ import annotations

import pytest

from poison_support import DEATH_MARKER, affect_ticks_until_death, death_tick_budget, poison_until_it_lands
from rots_harness import fixtures, records

pytestmark = pytest.mark.scenario


def test_remote_player_poison_death_is_gentle_and_fully_attributed(server, imp, mage, victim, harness) -> None:
    imp.command(f"goto {fixtures.ROOM_ARENA_CENTRE}")
    imp.command("transfer harnmage")
    imp.command("transfer harnvictim")
    imp.command("wizset harnvictim level 11")  # BB needs attacker(30) < defender*3; 11 gives 33 > 30
    imp.command("wizset harnvictim hit 10")

    poison_until_it_lands(mage, victim, "elf")
    # An offensive cast engages the mage; a wizard transfer disengages both sides
    # (char_from_room stops the fight), so the victim dies of the DoT alone.
    imp.command(f"goto {fixtures.ROOM_ARENA_WEST}")
    imp.command("transfer harnmage")
    assert mage.command("look").contains("Arena West"), mage.everything[-1500:]

    assert affect_ticks_until_death(harness, victim, death_tick_budget(10)), "the victim should have died of the forced poison ticks"

    look = victim.command("look")
    assert look.contains("Wood-elf Start"), look.text

    stat = imp.command("stat harnvictim")
    current, maximum = stat.hit_points()
    assert current == maximum // 4, f"gentle poison death revives at a quarter of {maximum} HP, got {current}: {stat.text}"

    victim_records = records.read_exploits(server.lib_dir, "Harnvictim")
    types = [record.type for record in victim_records]
    assert records.EXPLOIT_POISON in types, victim_records
    assert records.EXPLOIT_MOBDEATH not in types, victim_records
    deaths = [record for record in victim_records if record.type == records.EXPLOIT_DEATH]
    assert any(record.victim_name.lower() == "harnmage" for record in deaths), victim_records

    mage_records = records.read_exploits(server.lib_dir, "Harnmage")
    kills = [record for record in mage_records if record.type == records.EXPLOIT_PK]
    assert any(record.victim_name.lower() == "harnvictim" for record in kills), mage_records
```
(`pytest.ini` sets `pythonpath = .`, so `poison_support` imports as a top-level module from the `scenarios` directory when pytest's rootdir-relative import mode adds it; if the import fails, add an empty `tests/integration/scenarios/__init__.py` and import as `from scenarios.poison_support import ...` in every scenario that uses it, and say so in your report.)

- [ ] **Step 8: Run the scenario twice and the boot test**

```sh
ROTS_IT_LAUNCHER=docker build/integration-venv/bin/python -m pytest tests/integration/scenarios/test_poison_remote_player.py tests/integration/scenarios/test_boot.py -q
ROTS_IT_LAUNCHER=docker build/integration-venv/bin/python -m pytest tests/integration/scenarios/test_poison_remote_player.py -q
```
Expected: both runs pass, no xfail/xpass in the summary. Then `make integration-unit PYTHON=build/integration-venv/bin/python` passes.

- [ ] **Step 9: Commit**

```sh
git commit -m "harness: affects subcommand forces one person-affect tick; poison scenario is deterministic" -- src/test_harness.h src/test_harness.cpp src/limits.cpp src/tests/test_harness_tests.cpp tests/integration/conftest.py tests/integration/scenarios/poison_support.py tests/integration/scenarios/test_poison_remote_player.py
```

---

### Task 2: `Harncaller`, the crevice floor room, and the `stat` ability parser (spec B2 preamble)

**Files:**
- Modify: `tests/integration/rots_harness/fixtures.py` (`STANDARD_ROSTER`, room constants), `tests/integration/conftest.py` (session fixtures), `tests/integration/rots_harness/session.py` (`Transcript`), `tests/integration/world/wld/11.wld`
- Test: `tests/integration/unit/test_libbuilder.py`, `tests/integration/unit/test_session.py`, `tests/integration/unit/test_world.py`

**Interfaces:**
- Produces: roster entry `Harncaller` (`RACE_HUMAN`, level 30, `{"mage": 30}`, skills `{"summon": 100, "blaze": 100}`, idnum 9000005, load room `ROOM_ARENA_CENTRE`); fixture `caller`; `fixtures.ROOM_CREVICE_FLOOR = 1136`; `Transcript.abilities() -> dict[str, int] | None` mapping `str, int, wil, dex, con, lea` to their current values.

- [ ] **Step 1: Write the failing unit tests**

`tests/integration/unit/test_libbuilder.py`, change `test_build_seeds_the_roster`'s account assertion to:
```python
    assert account["characters"] == ["harnimp", "harnmage", "harnfighter", "harnvictim", "harncaller"]
```
`tests/integration/unit/test_session.py`, append:
```python
def test_transcript_parses_the_stat_ability_line() -> None:
    text = "Str:[14/14/14] Int:[10/10/10] Wil:[12/12/12] Dex:[13/13/13] Con: [11/11/11] Lea:[9/9/9]\n"
    assert Transcript(text).abilities() == {"str": 14, "int": 10, "wil": 12, "dex": 13, "con": 11, "lea": 9}


def test_transcript_abilities_is_none_without_the_line() -> None:
    assert Transcript("HP :[10/60]").abilities() is None
```
(import `Transcript` from `rots_harness.session` as the file already does.)
`tests/integration/unit/test_world.py`, append a test in the style of that file's existing world checks (read it first) that asserts room 1136 exists in `11.wld` and that room 1130 has a `D4` record to 1136 with exit info `0`.

- [ ] **Step 2: Run the unit suite to verify the three fail**

Run: `make integration-unit PYTHON=build/integration-venv/bin/python`. Expected: exactly those tests fail.

- [ ] **Step 3: Roster and room constant**

In `fixtures.py` add `ROOM_CREVICE_FLOOR = 1136` after `ROOM_CORRIDOR_TWO`, and append to `STANDARD_ROSTER`:
```python
    # Human, not magus: other_side() puts a magus on the opposite side from the wood-elf victim,
    # and spell_summon fails across sides. The summon and relogin scenarios use this caster.
    CharacterSpec("Harncaller", RACE_HUMAN, 30, ROOM_ARENA_CENTRE, {"mage": 30}, {"summon": 100, "blaze": 100}, 200, 600, 200, 9000005),
```
In `conftest.py` after `victim = _session_fixture("victim", "Harnvictim")`:
```python
caller = _session_fixture("caller", "Harncaller")
```

- [ ] **Step 4: World change**

In `tests/integration/world/wld/11.wld`, replace the `#1130` record's `D1` block and `S` with a `D1` block plus a `D4` block, and add room 1136 after `#1135`:
```
#1130
Arena West~
The western end of the harness arena. The arena continues east; a crevice floor lies below.
~
11 0 0 0
D1
~
~
0 0 1131 5
D4
~
~
0 0 1136 5
S
```
and
```
#1136
Crevice Floor~
The floor of a crevice beneath the arena. The only way is up.
~
11 0 0 0
D5
~
~
0 0 1130 5
S
```
(Direction numbers: 0 north, 1 east, 2 south, 3 west, 4 down, 5 up, as the existing records use. The exit-info field is the first `0` on the `0 0 <to_room> 5` line; it must stay `0` so `spell_earthquake` treats the way down as open.)

- [ ] **Step 5: `Transcript.abilities()`**

In `session.py` add after `HIT_POINT_PATTERN`:
```python
ABILITY_PATTERN = re.compile(r"Str:\[(\d+)/\d+/\d+\] Int:\[(\d+)/\d+/\d+\] Wil:\[(\d+)/\d+/\d+\] Dex:\[(\d+)/\d+/\d+\] Con: \[(\d+)/\d+/\d+\] Lea:\[(\d+)/\d+/\d+\]")
```
and in `Transcript`:
```python
    def abilities(self) -> dict[str, int] | None:
        """Current values from do_stat's ability line (act_wiz.cpp): name -> current."""
        match = ABILITY_PATTERN.search(self.text)
        if match is None:
            return None
        return dict(zip(("str", "int", "wil", "dex", "con", "lea"), (int(value) for value in match.groups())))
```

- [ ] **Step 6: Unit tests and a boot run**

```sh
make integration-unit PYTHON=build/integration-venv/bin/python
ROTS_IT_LAUNCHER=docker build/integration-venv/bin/python -m pytest tests/integration/scenarios/test_boot.py -q
```
Expected: unit suite green; the three boot tests pass (the roster test now iterates five characters). Also log in as the caller once by running:
```sh
ROTS_IT_LAUNCHER=docker build/integration-venv/bin/python -m pytest tests/integration/scenarios/test_boot.py -q -k roster
```

- [ ] **Step 7: Commit**

```sh
git commit -m "harness: Harncaller roster character, crevice floor room, stat ability parser" -- tests/integration/rots_harness/fixtures.py tests/integration/conftest.py tests/integration/rots_harness/session.py tests/integration/world/wld/11.wld tests/integration/unit/test_libbuilder.py tests/integration/unit/test_session.py tests/integration/unit/test_world.py
```

---

### Task 3: Poison attribution when the poisoner is gone (spec B2 rows 3-4)

**Files:**
- Create: `tests/integration/scenarios/test_poison_attribution_when_poisoner_is_gone.py`

**Interfaces:** consumes Task 1's `poison_support` and `harness.affects()`.

- [ ] **Step 1: Write the scenario file**

```python
"""manual-test-plan.md item 2: poison the victim, then remove the poisoner before the lethal
tick. A quit frees the body, so the tick credits nobody; an imp-slain player keeps its body and
registration serial, so resolve_poisoner still names the mage."""

from __future__ import annotations

import pytest

from poison_support import affect_ticks_until_death, death_tick_budget, poison_until_it_lands
from rots_harness import fixtures, records

pytestmark = pytest.mark.scenario


def _poison_then_separate(imp, mage, victim) -> None:
    imp.command(f"goto {fixtures.ROOM_ARENA_CENTRE}")
    imp.command("transfer harnmage")
    imp.command("transfer harnvictim")
    imp.command("wizset harnvictim level 11")
    imp.command("wizset harnvictim hit 10")
    poison_until_it_lands(mage, victim, "elf")
    imp.command(f"goto {fixtures.ROOM_ARENA_WEST}")
    imp.command("transfer harnmage")


def test_poisoner_who_quits_before_the_lethal_tick_is_credited_with_nothing(server, imp, mage, victim, harness) -> None:
    _poison_then_separate(imp, mage, victim)
    mage.quit()

    assert affect_ticks_until_death(harness, victim, death_tick_budget(10)), "the victim should die of the forced poison ticks"
    assert victim.command("look").contains("Wood-elf Start")

    victim_records = records.read_exploits(server.lib_dir, "Harnvictim")
    types = [record.type for record in victim_records]
    assert records.EXPLOIT_POISON in types, victim_records
    assert records.EXPLOIT_MOBDEATH not in types, victim_records
    assert not any(record.victim_name.lower() == "harnmage" for record in victim_records), f"a departed poisoner must never be named: {victim_records}"
    mage_records = records.read_exploits(server.lib_dir, "Harnmage")
    assert not any(record.type == records.EXPLOIT_PK for record in mage_records), mage_records


def test_poisoner_slain_before_the_lethal_tick_is_still_named(server, imp, mage, victim, harness) -> None:
    _poison_then_separate(imp, mage, victim)
    imp.command("slay harnmage")
    assert mage.command("look").room_name() is not None, "the slain mage keeps its body and stays logged in"

    assert affect_ticks_until_death(harness, victim, death_tick_budget(10)), "the victim should die of the forced poison ticks"

    victim_records = records.read_exploits(server.lib_dir, "Harnvictim")
    deaths = [record for record in victim_records if record.type == records.EXPLOIT_DEATH]
    assert any(record.victim_name.lower() == "harnmage" for record in deaths), victim_records
    assert records.EXPLOIT_POISON in [record.type for record in victim_records], victim_records
    mage_records = records.read_exploits(server.lib_dir, "Harnmage")
    assert any(record.type == records.EXPLOIT_PK and record.victim_name.lower() == "harnvictim" for record in mage_records), mage_records
```

- [ ] **Step 2: Run it**

```sh
ROTS_IT_LAUNCHER=docker build/integration-venv/bin/python -m pytest tests/integration/scenarios/test_poison_attribution_when_poisoner_is_gone.py -q
```
Expected: 2 passed. If `slay` prints a confirmation prompt or the mage's death message differs, read `build/integration/<id>/harnmage.txt` with `ROTS_IT_KEEP=1` and adjust only the `look` assertion, never the record assertions.

- [ ] **Step 3: Commit**

```sh
git commit -m "scenarios: poison attribution when the poisoner quits or is slain before the lethal tick" -- tests/integration/scenarios/test_poison_attribution_when_poisoner_is_gone.py
```

---

### Task 4: Snake poison punishment, gentle and harsh (spec B2 rows 5-6)

**Files:**
- Create: `tests/integration/scenarios/test_poison_punishment_snake.py`

**Interfaces:** `Transcript.abilities()` (Task 2), `poison_support` (Task 1). The snake (mob 1131) resets in Corridor One (1134) and bites its fight target with `spell_poison` at `number(0, 42 - level) < min(1 + level/4, 4)` per violence round (`spec_pro.cpp` `SPECIAL(snake)`); `wizset snake level 40` makes every round a bite.

- [ ] **Step 1: Write the scenario file**

```python
"""manual-test-plan.md item 2, punishment cases (a) and (b): a mob's poison, then death alone
(gentle: hit == max/4, mana 0, stats unchanged, EXPLOIT_POISON only) versus death still engaged
with the snake (harsh: hit == 1, stats scaled 2/3, EXPLOIT_MOBDEATH naming the snake)."""

from __future__ import annotations

import pytest

from poison_support import POISON_LANDED, affect_ticks_until_death, death_tick_budget
from rots_harness import fixtures, records

pytestmark = pytest.mark.scenario

BITE_MARKER = "bites you!"  # spec_pro.cpp SPECIAL(snake)


def _engage_snake_until_poisoned(imp, victim) -> dict[str, int]:
    imp.command(f"goto {fixtures.ROOM_CORRIDOR_ONE}")
    imp.command("transfer harnvictim")
    imp.command("wizset snake level 40")  # bite every round: number(0, 2) < 4
    imp.command("wizset snake maxhit 2000")
    imp.command("wizset snake hit 2000")
    imp.command("wizset harnvictim maxhit 400")
    imp.command("restore harnvictim")
    before = imp.command("stat harnvictim").abilities()
    assert before is not None
    victim.command("kill snake")
    victim.expect(POISON_LANDED, timeout=40.0)  # a bite that lands prints this; a resisted bite keeps fighting
    return before


def test_mob_poison_death_alone_is_gentle(server, imp, victim, harness) -> None:
    before = _engage_snake_until_poisoned(imp, victim)
    # Two rooms away and out of the fight: the transfer stops both sides (char_from_room).
    imp.command(f"goto {fixtures.ROOM_ARENA_EAST}")
    imp.command("transfer harnvictim")
    assert victim.command("look").room_name() == "Arena East"
    imp.command("wizset harnvictim hit 10")

    assert affect_ticks_until_death(harness, victim, death_tick_budget(10)), "the victim should die of the poison alone"

    stat = imp.command("stat harnvictim")
    current, maximum = stat.hit_points()
    assert current == maximum // 4, stat.text
    after = stat.abilities()
    assert after == before, f"gentle death must not scale stats: {before} -> {after}"

    victim_records = records.read_exploits(server.lib_dir, "Harnvictim")
    types = [record.type for record in victim_records]
    assert records.EXPLOIT_POISON in types, victim_records
    assert records.EXPLOIT_MOBDEATH not in types, victim_records
    assert records.EXPLOIT_PK not in types and records.EXPLOIT_DEATH not in types, victim_records


def test_death_while_still_fighting_the_snake_is_harsh(server, imp, victim, harness) -> None:
    before = _engage_snake_until_poisoned(imp, victim)
    imp.command("wizset harnvictim hit 10")  # still fighting; the next ticks or bites kill

    assert affect_ticks_until_death(harness, victim, death_tick_budget(10) + 4), "the victim should die engaged with the snake"

    stat = imp.command("stat harnvictim")
    current, _maximum = stat.hit_points()
    assert current == 1, stat.text
    after = stat.abilities()
    assert after is not None
    for name, value in before.items():
        assert after[name] == value * 2 // 3, f"{name}: {value} -> {after[name]} (expected {value * 2 // 3})"

    victim_records = records.read_exploits(server.lib_dir, "Harnvictim")
    mob_deaths = [record for record in victim_records if record.type == records.EXPLOIT_MOBDEATH]
    assert any("snake" in record.victim_name.lower() for record in mob_deaths), victim_records
```

- [ ] **Step 2: Run it**

```sh
ROTS_IT_LAUNCHER=docker build/integration-venv/bin/python -m pytest tests/integration/scenarios/test_poison_punishment_snake.py -q
```
Expected: 2 passed. If the harsh case dies to the snake's melee rather than a poison tick, the punishment is still harsh and the mob-death record still names the snake (`raw_kill` with a real-mob killer), so the assertions hold; note which happened in your report. If `stat`'s ability line shows base/const values that also change, compare only the current (first) numbers, which `abilities()` already returns.

- [ ] **Step 3: Commit**

```sh
git commit -m "scenarios: snake poison punishment, gentle when alone and harsh when engaged" -- tests/integration/scenarios/test_poison_punishment_snake.py
```

---

### Task 5: Player poison, death while fighting the brute (spec B2 row 7)

**Files:**
- Create: `tests/integration/scenarios/test_poison_punishment_player_poison_mob_fight.py`

- [ ] **Step 1: Write the scenario file**

```python
"""manual-test-plan.md item 2, punishment case (c): a player's poison, death while fighting an
unrelated real mob. Harsh penalty, EXPLOIT_MOBDEATH names the brute, the poisoner keeps EXPLOIT_PK."""

from __future__ import annotations

import pytest

from poison_support import affect_ticks_until_death, death_tick_budget, poison_until_it_lands
from rots_harness import fixtures, records

pytestmark = pytest.mark.scenario


def test_player_poison_death_in_a_mob_fight_is_harsh_and_keeps_the_pk_record(server, imp, mage, victim, harness) -> None:
    imp.command(f"goto {fixtures.ROOM_ARENA_CENTRE}")
    imp.command("transfer harnmage")
    imp.command("transfer harnvictim")
    imp.command("wizset harnvictim level 11")
    imp.command("wizset harnvictim maxhit 400")
    imp.command("restore harnvictim")
    before = imp.command("stat harnvictim").abilities()
    assert before is not None

    poison_until_it_lands(mage, victim, "elf")
    imp.command(f"goto {fixtures.ROOM_ARENA_WEST}")
    imp.command("transfer harnmage")  # the poisoner leaves; the fight it started ends with the transfer

    imp.command(f"goto {fixtures.ROOM_ARENA_CENTRE}")
    imp.command("load mob 1133")
    imp.command("wizset brute maxhit 4000")
    imp.command("wizset brute hit 4000")
    victim.command("kill brute")
    imp.command("wizset harnvictim hit 10")

    assert affect_ticks_until_death(harness, victim, death_tick_budget(10) + 4), "the victim should die engaged with the brute"

    stat = imp.command("stat harnvictim")
    assert stat.hit_points()[0] == 1, stat.text
    after = stat.abilities()
    for name, value in before.items():
        assert after[name] == value * 2 // 3, f"{name}: {value} -> {after[name]}"

    victim_records = records.read_exploits(server.lib_dir, "Harnvictim")
    mob_deaths = [record for record in victim_records if record.type == records.EXPLOIT_MOBDEATH]
    assert any("brute" in record.victim_name.lower() for record in mob_deaths), victim_records
    assert records.EXPLOIT_POISON in [record.type for record in victim_records] or mob_deaths, victim_records

    mage_records = records.read_exploits(server.lib_dir, "Harnmage")
    assert any(record.type == records.EXPLOIT_PK and record.victim_name.lower() == "harnvictim" for record in mage_records), mage_records
```
The `EXPLOIT_POISON` record is written only when the killing attack type is the poison tick; a brute blow that lands the kill skips it, which is why the assertion accepts either. The PK record is written from the contributor list, which includes the resolved poisoner either way.

- [ ] **Step 2: Run it and commit**

```sh
ROTS_IT_LAUNCHER=docker build/integration-venv/bin/python -m pytest tests/integration/scenarios/test_poison_punishment_player_poison_mob_fight.py -q
git commit -m "scenarios: player poison death in a mob fight is harsh and keeps the poisoner's PK record" -- tests/integration/scenarios/test_poison_punishment_player_poison_mob_fight.py
```

---

### Task 6: Blaze ticks after the caster is slain, and after a link drop with a relogin (spec B2 rows 1-2)

**Files:**
- Create: `tests/integration/scenarios/test_blaze_after_caster_gone.py`

**Interfaces:** `caller` fixture (Task 2); `GameSession.drop_link()`; `conftest._login` is private, so the relogin uses the `caller` fixture requested after the drop (fixtures are created before the test body; to log the caller in *after* the mage's link drops, the test takes `server` only and builds the session with `GameSession` directly as `_login` does).

- [ ] **Step 1: Write the scenario file**

```python
"""manual-test-plan.md item 1: a blaze keeps ticking after its caster is slain (the body and
registration serial survive, so the tick still credits the mage) and after the caster's link
drops while another character logs in (no misattribution to the new body)."""

from __future__ import annotations

import pytest

from poison_support import BLAZE_CAST, DEATH_MARKER
from rots_harness import fixtures, records
from rots_harness.session import GameSession

pytestmark = pytest.mark.scenario


def _blaze_the_centre(imp, mage, victim) -> int:
    imp.command(f"goto {fixtures.ROOM_ARENA_CENTRE}")
    imp.command("transfer harnmage")
    imp.command("transfer harnvictim")
    imp.command("restore harnmage")
    imp.command("wizset harnvictim maxhit 300")
    imp.command("restore harnvictim")
    victim.command("west")  # out of the cast so nobody engages anybody
    mage.cast("blaze", success_markers=BLAZE_CAST)
    return imp.command("stat harnvictim").hit_points()[0]


def _tick_until_dead(harness, imp, victim, before: int) -> None:
    victim.command("east")
    assert victim.command("look").room_name() == "Arena Centre"
    after = before
    for _tick in range(15):  # a room-affect tick fires probabilistically per pulse
        harness.tick()
        after = imp.command("stat harnvictim").hit_points()[0]
        if after < before:
            break
    assert after < before, f"the blaze should still tick ({before} -> {after})"
    died = False
    for _tick in range(30):
        harness.tick()
        if DEATH_MARKER in victim.drain(1.0):
            died = True
            break
    assert died, "the blaze ticks should eventually kill the victim"


def test_blaze_ticks_survive_the_casters_death_and_still_credit_the_mage(server, imp, mage, victim, harness) -> None:
    before = _blaze_the_centre(imp, mage, victim)
    imp.command("slay harnmage")
    assert mage.command("look").room_name() is not None, "a slain player keeps its body"

    _tick_until_dead(harness, imp, victim, before)

    victim_records = records.read_exploits(server.lib_dir, "Harnvictim")
    deaths = [record for record in victim_records if record.type == records.EXPLOIT_DEATH]
    assert any(record.victim_name.lower() == "harnmage" for record in deaths), victim_records


def test_blaze_ticks_after_a_link_drop_never_name_the_next_login(server, imp, mage, victim, harness) -> None:
    before = _blaze_the_centre(imp, mage, victim)
    mage.drop_link()
    caller = GameSession(server.handle, server.spec("Harncaller"), server.character_number("Harncaller"), server.run_dir)
    caller.login()
    try:
        _tick_until_dead(harness, imp, victim, before)
        victim_records = records.read_exploits(server.lib_dir, "Harnvictim")
        assert not any(record.victim_name.lower() == "harncaller" for record in victim_records), f"the new login must never be credited: {victim_records}"
        caller_records = records.read_exploits(server.lib_dir, "Harncaller")
        assert caller_records == [], caller_records
    finally:
        try:
            caller.quit()
        except Exception:
            caller.close()
```

- [ ] **Step 2: Run it and commit**

```sh
ROTS_IT_LAUNCHER=docker build/integration-venv/bin/python -m pytest tests/integration/scenarios/test_blaze_after_caster_gone.py -q
git commit -m "scenarios: blaze ticks after the caster is slain and after a link drop with a relogin" -- tests/integration/scenarios/test_blaze_after_caster_gone.py
```
Expected: 2 passed. A link-dropped mage's body may or may not still resolve for the tick (the server keeps a linkless body for a while); the test therefore asserts only what the spec pins: the new login is never named, and no crash.

---

### Task 7: Kill credit: non-engaged killing spell and splash bystander (spec B2 rows 8-9)

**Files:**
- Create: `tests/integration/scenarios/test_kill_credit.py`

- [ ] **Step 1: Write the scenario file**

```python
"""manual-test-plan.md item 3: a killing blow from a caster the mob was not fighting is still
recorded to that caster, and a bystander mob caught by splash manufactures no kill credit."""

from __future__ import annotations

import pytest

from poison_support import BLAZE_CAST
from rots_harness import fixtures, records

pytestmark = pytest.mark.scenario

SHARE_MARKER = "You receive your share of experience"


def test_killing_blow_from_an_unengaged_caster_is_credited_to_the_caster(server, imp, mage, fighter, harness) -> None:
    imp.command(f"goto {fixtures.ROOM_ARENA_WEST}")
    imp.command("transfer harnmage")
    imp.command("transfer harnfighter")
    imp.command("restore harnmage")
    imp.command("load mob 1130")
    imp.command("wizset orc maxhit 400")
    imp.command("wizset orc hit 400")

    fighter.command("kill orc")  # the orc tanks the fighter
    imp.command("wizset orc hit 9")  # below the smallest halved blaze tick
    mage.cast("blaze", success_markers=BLAZE_CAST)  # the mage never engages; the tick lands the kill
    harness.tick()
    fighter.expect([SHARE_MARKER], 30.0)

    mage_records = records.read_exploits(server.lib_dir, "Harnmage")
    assert mage_records, "the unengaged caster whose tick killed the orc must appear in a kill record"


def test_splash_bystander_manufactures_no_credit(server, imp, mage, fighter, harness) -> None:
    imp.command(f"goto {fixtures.ROOM_ARENA_WEST}")
    imp.command("transfer harnmage")
    imp.command("transfer harnfighter")
    imp.command("restore harnmage")
    imp.command("load mob 1130")
    imp.command("load mob 1132")  # the bystander
    imp.command("wizset orc hit 9")

    fighter.command("kill orc")
    mage.cast("blaze", success_markers=BLAZE_CAST)
    harness.tick()
    fighter.expect([SHARE_MARKER], 30.0)

    for name in ("Harnmage", "Harnfighter"):
        for record in records.read_exploits(server.lib_dir, name):
            assert "bystander" not in record.victim_name.lower(), f"{name}: {record}"
```
Before committing, confirm from one run with `ROTS_IT_KEEP=1` which mob record type the caster receives for a mob kill (read `harnmage.exploits.json` in the kept run directory) and tighten the first test's assertion to that type and `victim_name` containing `target orc`; write the value into the test and cite the file in your report.

- [ ] **Step 2: Run it and commit**

```sh
ROTS_IT_LAUNCHER=docker build/integration-venv/bin/python -m pytest tests/integration/scenarios/test_kill_credit.py -q
git commit -m "scenarios: kill credit for an unengaged killing spell; splash bystander earns nothing" -- tests/integration/scenarios/test_kill_credit.py
```

---

### Task 8: Summon by name in the dark room and summon of a link-dead character (spec B2 rows 10-11)

**Files:**
- Create: `tests/integration/scenarios/test_summon.py`

**Interfaces:** `caller` fixture (Task 2). Success lines from `spell_summon` (`mage.cpp`): the caster and room see `appears in the room.`; the victim sees `summons you!`; failure prints `You failed.`

- [ ] **Step 1: Write the scenario file**

```python
"""manual-test-plan.md item 4: summon targets by name in a dark room (TAR_DARK_OK), and a
link-dead victim is summoned without dereferencing its missing descriptor."""

from __future__ import annotations

import pytest

from rots_harness import fixtures

pytestmark = pytest.mark.scenario

SUMMON_SUCCESS = ("appears in the room.",)  # spell_summon act() to caster
SUMMONED_MARKER = "summons you!"


def test_summon_by_name_from_the_dark_room(server, imp, caller, victim) -> None:
    imp.command(f"goto {fixtures.ROOM_DARK_CELL}")
    imp.command("transfer harncaller")
    imp.command("restore harncaller")
    imp.command(f"goto {fixtures.ROOM_CORRIDOR_TWO}")
    imp.command("transfer harnvictim")
    assert victim.command("look").room_name() == "Corridor Two"

    caller.cast("summon", "harnvictim", success_markers=SUMMON_SUCCESS, attempts=10)
    victim.expect([SUMMONED_MARKER], 10.0)

    stat = imp.command("stat harnvictim")
    assert "Dark Cell" in stat.text or "[1133]" in stat.text, stat.text


def test_summon_of_a_link_dead_character_relocates_it_without_a_crash(server, imp, caller, victim) -> None:
    imp.command(f"goto {fixtures.ROOM_ARENA_EAST}")
    imp.command("transfer harncaller")
    imp.command("restore harncaller")
    imp.command(f"goto {fixtures.ROOM_CORRIDOR_TWO}")
    imp.command("transfer harnvictim")
    victim.drop_link()
    imp.drain(1.0)  # let the server log the link loss before the cast

    caller.cast("summon", "harnvictim", success_markers=SUMMON_SUCCESS, attempts=10)

    stat = imp.command("stat harnvictim")
    assert "Arena East" in stat.text or "[1132]" in stat.text, stat.text
    # The crash monitor (server teardown) fails the test on any signal or sanitizer report;
    # act() and do_look on the descriptor-less victim are the paths under test.
```
Read `do_stat`'s room line format once (`ROTS_IT_KEEP=1`, `harnimp.txt`) and keep only the `stat` assertion form that matches it.

- [ ] **Step 2: Run it and commit**

```sh
ROTS_IT_LAUNCHER=docker build/integration-venv/bin/python -m pytest tests/integration/scenarios/test_summon.py -q
git commit -m "scenarios: summon by name in the dark room and summon of a link-dead character" -- tests/integration/scenarios/test_summon.py
```
Expected: 2 passed. If ten casts never succeed, record the `You failed.` count against the seed in your report and raise `attempts` to 16 once; a persistent failure is a finding (the save roll or a side check), not a reason to weaken the assertion.

---

### Task 9: Earthquake message order (spec B2 row 12)

**Files:**
- Create: `tests/integration/scenarios/test_earthquake_order.py`

**Interfaces:** `ROOM_CREVICE_FLOOR` and the 1130 down exit (Task 2). Messages from `spell_earthquake` (`mage.cpp`): an occupant who falls sends `$n loses balance and falls down!` to the origin room and sees `The earthquake throws you down!`; after arriving in the crevice it sends `$n falls in.` there. Every other occupant falls before the caster.

- [ ] **Step 1: Write the scenario file**

```python
"""manual-test-plan.md item 5: in a crowded room where the caster also falls, every other
occupant's fall message precedes the caster's own."""

from __future__ import annotations

import pytest

from rots_harness import fixtures

pytestmark = pytest.mark.scenario

FALL_SUFFIX = " loses balance and falls down!"
FELL_IN_SUFFIX = " falls in."
CASTER = "Harnmage"


def _fall_lines(text: str) -> list[str]:
    return [line.strip() for line in text.splitlines() if line.strip().endswith((FALL_SUFFIX, FELL_IN_SUFFIX))]


def test_the_caster_falls_last(server, imp, mage, fighter, victim, harness) -> None:
    imp.command(f"goto {fixtures.ROOM_ARENA_WEST}")
    for name in ("harnmage", "harnfighter", "harnvictim"):
        imp.command(f"transfer {name}")
        imp.command(f"wizset {name} maxhit 2000")
        imp.command(f"restore {name}")
    imp.command(f"goto {fixtures.ROOM_IMMORTAL_START}")  # the imp is not an occupant

    for _attempt in range(12):
        mage.send_line("cast 'earthquake'")
        text = mage.drain(2.5) + fighter.drain(0.5) + victim.drain(0.5)
        observed = [line for line in (fighter.everything + victim.everything).splitlines() if line.strip().endswith((FALL_SUFFIX, FELL_IN_SUFFIX))]
        caster_fell = any(line.strip().startswith(CASTER) for line in observed)
        others_fell = any(not line.strip().startswith(CASTER) for line in observed)
        if caster_fell and others_fell:
            break
        # Put everyone back for the next cast.
        imp.command(f"goto {fixtures.ROOM_ARENA_WEST}")
        for name in ("harnmage", "harnfighter", "harnvictim"):
            imp.command(f"transfer {name}")
            imp.command(f"restore {name}")
        imp.command(f"goto {fixtures.ROOM_IMMORTAL_START}")
    else:
        pytest.fail(f"no cast produced both a caster fall and another fall; last text:\n{text[-1500:]}")

    for observer in (fighter, victim):
        lines = _fall_lines(observer.everything)
        caster_positions = [index for index, line in enumerate(lines) if line.startswith(CASTER)]
        if not caster_positions:
            continue
        assert caster_positions[-1] == len(lines) - 1, f"{observer.character.name} saw a fall after the caster's: {lines}"
```

- [ ] **Step 2: Run it and commit**

```sh
ROTS_IT_LAUNCHER=docker build/integration-venv/bin/python -m pytest tests/integration/scenarios/test_earthquake_order.py -q
git commit -m "scenarios: earthquake fall messages end with the caster's own fall" -- tests/integration/scenarios/test_earthquake_order.py
```
Expected: 1 passed. Each observer sees its own fall as `The earthquake throws you down!` (not counted) and the others' falls by name, so the assertion is on the named lines only; an observer that fell before the caster still sees the caster's `falls in.` line last in the crevice.

---

### Task 10: Affect expiry with a death inside the same `affect_update` (spec B2 row 13)

**Files:**
- Create: `tests/integration/scenarios/test_affect_expiry_with_death.py`

- [ ] **Step 1: Write the scenario file**

```python
"""manual-test-plan.md item 6: affects expire and a character dies inside one affect_update()
while another affected character has just quit. affect_update walks a snapshot; no crash is the
pass condition (the crash monitor and the CI sanitizer enforce it)."""

from __future__ import annotations

import pytest

from poison_support import BLAZE_CAST, affect_ticks_until_death, death_tick_budget, poison_until_it_lands
from rots_harness import fixtures, records

pytestmark = pytest.mark.scenario


def test_death_and_expiry_in_one_affect_update_after_a_quit(server, imp, mage, fighter, victim, harness) -> None:
    imp.command(f"goto {fixtures.ROOM_ARENA_CENTRE}")
    for name in ("harnmage", "harnfighter", "harnvictim"):
        imp.command(f"transfer {name}")
    imp.command("restore harnmage")
    imp.command("wizset harnvictim level 11")
    imp.command("wizset harnfighter maxhit 400")
    imp.command("restore harnfighter")

    poison_until_it_lands(mage, victim, "elf")  # a person affect on the victim
    poison_until_it_lands(mage, fighter, "harnfighter")  # and on the fighter
    mage.cast("blaze", success_markers=BLAZE_CAST)  # a room affect in the same room
    imp.command(f"goto {fixtures.ROOM_ARENA_WEST}")
    imp.command("transfer harnmage")
    imp.command("wizset harnvictim hit 10")

    fighter.quit()  # its poison node leaves affected_list just before the forced tick
    assert affect_ticks_until_death(harness, victim, death_tick_budget(10)), "the victim should die inside the forced ticks"

    assert records.EXPLOIT_POISON in [record.type for record in records.read_exploits(server.lib_dir, "Harnvictim")]
    assert imp.command("stat harnvictim").hit_points() is not None
```
The mage's second poison targets the fighter by name; if Big Brother refuses the cast (level 20 fighter vs level 30 mage: `30 < 20 * 3` holds, so it is allowed), the `poison_until_it_lands` failure text says so.

- [ ] **Step 2: Run it and commit**

```sh
ROTS_IT_LAUNCHER=docker build/integration-venv/bin/python -m pytest tests/integration/scenarios/test_affect_expiry_with_death.py -q
git commit -m "scenarios: affect expiry and a death in one affect_update after a quit" -- tests/integration/scenarios/test_affect_expiry_with_death.py
```

---

### Task 11: Full suite, CI timing, status (spec B3)

Controller task after Tasks 1-10 are reviewed.

**Files:**
- Modify: `WIP.md` (append), `.github/workflows/ci.yml` (only the timing comment on `integration-asan`, if the measured total changes the budget statement)

- [ ] **Step 1: Run the whole suite locally**

```sh
ROTS_IT_LAUNCHER=docker build/integration-venv/bin/python -m pytest tests/integration -q
```
Expected: all pass, no xfail, no xpass. Record the count.

- [ ] **Step 2: Push and measure CI**

```sh
git push origin fix/spell-room-affect-uaf-port
gh run list --branch fix/spell-room-affect-uaf-port --limit 1
gh run watch <id> --exit-status
```
Record the `Run integration suite` step duration from the job log. If the job total exceeds twenty minutes, revise `timeout-minutes` and its comment in `ci.yml` in this task and say so.

- [ ] **Step 3: Append the WIP.md status line and commit**

Under the harness section, append a dated paragraph: slice 2 complete; `harness affects` added; `Harncaller` and the crevice floor added; list the scenario files; suite counts (local and CI) and the measured CI duration; both jobs green at `<sha>`.
```sh
git commit -m "docs: slice 2 status; measured CI duration" -- WIP.md [.github/workflows/ci.yml]
git push origin fix/spell-room-affect-uaf-port
```

---

## Self-review

| Spec item | Task |
|---|---|
| B1 flag, subcommand, single gate in `affect_update_person`, gtest, `Harness.affects()`, xfail removal, budget formula | Task 1 |
| B2 pinned assertions (gentle/harsh, PK, slain-vs-quit attribution) | Tasks 3, 4, 5 |
| B2 `Harncaller` roster, unit-test count change | Task 2 |
| B2 blaze rows (slain caster; link drop plus relogin) | Task 6 |
| B2 poison rows (quit; slain; snake gentle/harsh; brute) | Tasks 3, 4, 5 |
| B2 kill-credit rows | Task 7 |
| B2 summon rows | Task 8 |
| B2 earthquake row, sink room with plain DOWN exit | Tasks 2, 9 |
| B2 mass-expiry row in its deterministic form | Task 10 |
| B2 CI budget measurement | Task 11 |
| B3 exit criteria (no xfail, every row covered, WIP status) | Tasks 1, 11 |

- **Placeholder scan:** no TBD/TODO. Two steps ask the implementer to confirm a server-produced string from a kept transcript (Task 7 record type, Task 8 `stat` room line) with the default form written in the code; both say what to write back.
- **Interface consistency:** `harness_force_affect_phase` (Task 1 C++), `Harness.affects()` (Tasks 1, 3-6, 10), `poison_support` names (Tasks 1, 3-7, 10), `Transcript.abilities()` (Tasks 2, 4, 5), `caller`/`Harncaller`/`ROOM_CREVICE_FLOOR` (Tasks 2, 6, 8, 9) are spelled the same throughout.
- **Controller ruling (2026-09-20):** Task 6's link-drop case: a link-dropped body persists as linkless in harness mode (idle force-rent is gated off), so the tick may or may not still resolve `Harnmage` depending on when the drop lands; the scenario asserts the hard requirements only (the new login `Harncaller` is never named; no crash) and records which of `Harnmage`/nobody it observed in the transcript. Original note: The spec row says "same [assertions], plus the login reusing the slot", but a link-dropped player's body persists as linkless for a period, so whether the tick still credits `Harnmage` is timing-dependent; the plan asserts only the spec's hard requirement (the new login is never named; no crash) and does not assert the mage is or is not named. Confirm or tighten.
- **Controller ruling (2026-09-20):** Task 8's link-dead summon keeps the relocation assertion under `GameSession.cast`'s retry budget; the seeded RNG makes the outcome reproducible, so a budget that never lands is a seed/budget problem the implementer reports (with the transcript) rather than a reason to weaken the test. Original note: `spell_summon` has no link-dead branch and `PRF_SUMMONABLE` is a failure condition; the plan asserts relocation plus no crash and uses `stat` to observe the room. If the seeded save roll makes the relocation fail across attempts, the no-crash half is the spec's stated assertion and the relocation assertion should be dropped, not retried indefinitely.
