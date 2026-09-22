# Harness Follow-up: Summon Distance, Fireball Splash, Fix-wave Findings — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. Scenario tasks follow the repository skill `rots-integration-scenario` (`.agents/skills/rots-integration-scenario/SKILL.md`).

**Goal:** Close the two manual-test-plan rows that the integration suite covers only through unit tests (summon's squared-distance save bonus, fireball splash engagement), and fix the four code findings the fix wave recorded but did not touch.

**Architecture:** Two new scenarios on the existing harness. Summon distance needs a second zone in the test world at a known map distance; fireball splash needs the fireball skill on Harncaller. The four findings are one-file changes each: a terminator and width in `write_player_text`, an entry skip in `Crash_alias_save`, a comment correction in the mage test context, and a dead class removed.

**Tech Stack:** C++ server and GoogleTest under `src/`; pytest harness under `tests/integration/`; server built in the container or natively; CI's `integration-asan` job is the gate.

**Spec:** `docs/superpowers/specs/2026-09-19-integration-test-harness-design.md` (scenario catalogue rows 3 and 4) and `manual-test-plan.md` rows 3 and 4. The four findings are recorded in the fix-wave ledger (`.superpowers/sdd/2026-09-20-fix-wave/progress.md`, lines 96-97, 144, 184). The Design section below is the binding text for what those sources leave open.

## Global Constraints

- Branch `fix/spell-room-affect-uaf-port`, draft PR #309; controller pushes after each task's review; no merges.
- No production behaviour outside harness mode changes except the two named fixes (Tasks 5 and 6); no new harness sub-command.
- `src/objsave.cpp` has mixed CRLF and LF line endings: edit byte-preservingly and keep each edited line's own ending. `src/db.cpp` and the test files are LF.
- Every container command runs with `ulimit -c 0`; the test binary under `timeout 900`.
- Scenarios wait on markers, never on fixed delays; every loop states its bound; docstrings state one fact once with the source citation.
- Delete only run directories your own runs kept.
- Commit trailers: `Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>` and `Claude-Session: https://claude.ai/code/session_01E3mWDHen1U7cDdb2sQ634P`.

## Design

**Summon distance.** `spell_summon` (`src/mage.cpp:850-863`) reads each zone's map coordinates from `zone_table[].x/.y` and adds `dx*dx + dy*dy` to the victim's save bonus. `new_saves_spell` (`src/spell_pa.cpp:245-266`) returns "saved" unconditionally when the bonus is 20 or more, so a summon across a squared distance of 20 or more fails on every attempt, deterministically. Zone 11 sits at (0,0) (`tests/integration/world/zon/11.zon`, header `? 0 0 5`: symbol, x, y, level per `src/zone.cpp:76-80`). A second zone at (4,2) gives exactly 20. Under the old XOR arithmetic the same placement gave `(4^2)+(2^2) = 6`, a random outcome, so the pin discriminates the fix. The control is the existing same-zone summon (`test_summon.py`), whose bonus is 0.

**Fireball splash.** `spell_fireball` (`src/mage.cpp:1960-1990`) rolls each other occupant for splash: probability 0.2 when the occupant is not fighting the caster, 0.8 when it is. A splash hit goes through `apply_spell_damage` with attack type `SPELL_FIREBALL2` (201), whose caster-side hit line is `The heat of your fireball burns $N as well.` (`lib/misc/messages:485`), and engages the bystander with the caster through `damage_credited`'s ordinary bookkeeping. Two bystanders raise the per-cast chance of at least one splash to 0.36; fifteen casts leave an all-miss chance of 0.64^15, about 0.1%, stated in the docstring. The invariant (manual plan row 3): the splash-engaged bystander is fighting the caster, appears in no player's exploit records when the primary target dies, and its own later death credits the fighter through the share line without any player record naming it.

**Findings.** (5) `write_player_text` (`src/db.cpp:2964`, `:2990`) fills `chd.host` with `strncpy(..., HOST_LEN)` and prints it with `%s`; a hostname of 30 or more characters leaves no terminator. Fix: terminate after the copy and print with `%.*s` and `HOST_LEN`. (6) `Crash_alias_save` (`src/objsave.cpp:1164-1183`) writes an alias's 20-byte keyword, then `continue`s when the command is empty without writing a length; `Crash_alias_load` (`:1109-1123`) then reads the next keyword's bytes as the length and fails the whole load. Fix: skip the entry before writing its keyword. (7) `MageTestContext` (`src/tests/mage_tests.cpp:93`) holds stack `char_data` members; the comment at `:843-852` says the bystander needs `MOB_ISNPC` to avoid `exp_with_modifiers`, but the load-bearing reason is that a non-NPC death routes through `extract_char` and `free_char` on a stack object. Fix: correct that comment and state the contract on the struct. (8) `ScopedPlayerTableEntry` in `src/tests/account_management_tests.cpp:102-125` has no users; delete it.

## File Structure

- Create: `tests/integration/world/zon/12.zon`, `tests/integration/world/wld/12.wld`; modify `zon/index`, `wld/index`.
- Modify: `tests/integration/rots_harness/fixtures.py` (room constant, Harncaller skills), `tests/integration/unit/test_world.py`, `tests/integration/unit/test_fixtures.py`.
- Create: `tests/integration/scenarios/test_summon_distance.py`, `tests/integration/scenarios/test_fireball_splash.py`.
- Modify: `src/db.cpp`, `src/tests/db_loader_tests.cpp`; `src/objsave.cpp`, `src/tests/db_loader_tests.cpp` (alias round trip); `src/tests/mage_tests.cpp`; `src/tests/account_management_tests.cpp`.
- Modify: `WIP.md`, `FEATURES.md`.

---

### Task 1: Zone 12 at map distance 20

**Files:**
- Create: `tests/integration/world/zon/12.zon`, `tests/integration/world/wld/12.wld`
- Modify: `tests/integration/world/zon/index`, `tests/integration/world/wld/index`, `tests/integration/rots_harness/fixtures.py:36`
- Test: `tests/integration/unit/test_world.py`

**Interfaces:**
- Produces: `fixtures.ROOM_DISTANT_CELL = 1201`; zone 12 at (4,2) with one room 1201 "Distant Cell", no exits, no mobs.

- [ ] **Step 1: Write the failing unit tests**

Append to `tests/integration/unit/test_world.py`:

```python
def test_zone_twelve_sits_at_squared_distance_twenty_from_zone_eleven() -> None:
    """spell_summon adds dx*dx + dy*dy to the save bonus (mage.cpp ~861); new_saves_spell treats a
    bonus of 20 or more as an automatic save (spell_pa.cpp ~262), so this placement makes a
    cross-zone summon fail deterministically."""
    eleven = (WORLD_ROOT / "zon" / "11.zon").read_text(encoding="latin-1")
    twelve = (WORLD_ROOT / "zon" / "12.zon").read_text(encoding="latin-1")
    header = re.compile(r"^\? (-?\d+) (-?\d+) \d+\s*$", flags=re.MULTILINE)
    x1, y1 = (int(value) for value in header.search(eleven).groups())
    x2, y2 = (int(value) for value in header.search(twelve).groups())
    assert (x2 - x1) ** 2 + (y2 - y1) ** 2 == 20


def test_zone_twelve_holds_only_the_distant_cell() -> None:
    text = (WORLD_ROOT / "wld" / "12.wld").read_text(encoding="latin-1")
    vnums = [int(match) for match in re.findall(r"^#(\d+)\s*$", text, flags=re.MULTILINE)]
    assert vnums == [1201, 99999]
    assert "Distant Cell" in text
    assert not re.search(r"^D[0-5]$", text, flags=re.MULTILINE), "the cell has no exits"
```

Also widen `test_rooms_are_ascending_and_inside_zone_eleven` so it still describes `11.wld` only (it already reads `11.wld`; no change needed unless it asserts on the index). Extend `test_every_category_has_an_index_that_names_existing_files` if it enumerates zone files by count.

- [ ] **Step 2: Run them to verify they fail**

Run: `build/integration-venv/bin/python -m pytest tests/integration/unit/test_world.py -q`
Expected: the two new tests fail with `FileNotFoundError` for `12.zon`.

- [ ] **Step 3: Write the zone and room files**

`tests/integration/world/zon/12.zon` (copy zone 11's header shape; the third header line is `symbol x y level`):

```
#12
Harness distant zone~
Second zone for the summon-distance scenario: squared map distance 20 from zone 11.
~
~
0
? 4 2 5
1299
999
2
S
$
```

Confirm the trailing lines against `11.zon` (`cat -A`); keep whatever terminator sequence zone 11 uses after its last command, since `load_zones` (`src/zone.cpp:45`) reads them positionally.

`tests/integration/world/wld/12.wld` (copy room 1133's record shape from `11.wld` and remove the exits):

```
#1201
Distant Cell~
A bare stone cell far from the arena, used to test summon across the map.
~
12 0 0
S
#99999
$~
```

Match the exact flag/sector line format of `11.wld` (zone number, room flags, sector) and the file terminator that `test_rooms_are_ascending_and_inside_zone_eleven` expects (`#99999`).

Add `12.zon` and `12.wld` to the two `index` files before `$`.

Add to `tests/integration/rots_harness/fixtures.py` after `ROOM_CREVICE_FLOOR`:

```python
ROOM_DISTANT_CELL = 1201  # zone 12 at map (4,2): squared distance 20 from zone 11 (test_world.py)
```

- [ ] **Step 4: Run the unit tests and the boot scenario**

Run: `build/integration-venv/bin/python -m pytest tests/integration/unit -q`
Expected: all pass.
Run: `ROTS_IT_LAUNCHER=<yours> build/integration-venv/bin/python -m pytest tests/integration -q -k test_boot`
Expected: the server boots with both zones (a zone file that fails to parse aborts boot, which the crash monitor reports). Then `imp.command("goto 1201")` from a kept run should show "Distant Cell"; verify once with `ROTS_IT_KEEP=1` and cite the room in the commit message.

- [ ] **Step 5: Commit**

```bash
git add tests/integration/world tests/integration/rots_harness/fixtures.py tests/integration/unit/test_world.py
git commit -m "harness world: zone 12 at squared map distance 20 for the summon-distance scenario"
```

---

### Task 2: Summon distance scenario

**Files:**
- Create: `tests/integration/scenarios/test_summon_distance.py`

**Interfaces:**
- Consumes: `fixtures.ROOM_DISTANT_CELL`, `fixtures.ROOM_ARENA_EAST`, `GameSession.send_line/expect/expect_room`, `test_summon.SUMMON_SUCCESS`.

- [ ] **Step 1: Write the scenario**

```python
"""A summon across a squared map distance of 20 always fails; the same summon inside the zone
succeeds. spell_summon (mage.cpp ~850-863) adds dx*dx + dy*dy between the two zones' map
coordinates to the victim's save bonus, and new_saves_spell (spell_pa.cpp ~262) returns a save
unconditionally once that bonus reaches 20. Zone 12 sits at (4,2) against zone 11's (0,0)
(tests/integration/world/zon/12.zon), so the cross-zone case is deterministic; the old XOR
arithmetic gave 6 here and a random outcome. A gtest pins the formula
(mage_tests.cpp SummonSaveBonusUsesSquaredZoneDistance); this pins the live coordinate path.

Bound: FAILED_ATTEMPTS casts, each waited on its own resolution line, so the loop cannot
outrun the caster's mana (600 at the fixture, summon costs far less per cast).
"""
from __future__ import annotations

import pytest

from rots_harness import fixtures
from rots_harness.session import GameSession
from test_summon import SUMMON_SUCCESS

pytestmark = pytest.mark.scenario

SUMMON_FAILED = "You failed."  # spell_summon's send_to_char on a saved roll (mage.cpp ~879)
FAILED_ATTEMPTS = 6


def _stage(imp: GameSession, victim: GameSession, victim_room: int, victim_room_name: str) -> None:
    imp.command(f"goto {fixtures.ROOM_ARENA_EAST}")
    imp.command("transfer harncaller")
    imp.command("restore harncaller")
    imp.command(f"goto {victim_room}")
    imp.command("transfer harnvictim")
    victim.expect_room(victim_room_name)


def test_summon_across_squared_distance_twenty_always_fails(server, imp, caller, victim) -> None:
    _stage(imp, victim, fixtures.ROOM_DISTANT_CELL, "Distant Cell")
    caller.expect_room("Arena East")
    for attempt in range(FAILED_ATTEMPTS):
        caller.send_line("cast 'summon' harnvictim")
        reply = caller.expect([SUMMON_FAILED, *SUMMON_SUCCESS], timeout=12.0)
        assert SUMMON_FAILED in reply, f"attempt {attempt}: a squared distance of 20 must force the save:\n{reply}"
        assert not any(marker in reply for marker in SUMMON_SUCCESS), reply
    stat = imp.command("stat harnvictim")
    assert f"In room [{fixtures.ROOM_DISTANT_CELL:5d}]" in stat.text, stat.text


def test_summon_inside_the_zone_still_lands(server, imp, caller, victim) -> None:
    _stage(imp, victim, fixtures.ROOM_CORRIDOR_TWO, "Corridor Two")
    caller.expect_room("Arena East")
    caller.cast("summon", "harnvictim", success_markers=SUMMON_SUCCESS, attempts=10)
    victim.expect_room("Arena East")
```

Check `cast`'s casting delay: `send_line` followed by `expect` must outlast `CASTING_TIME` for summon; use the kept transcript from a `ROTS_IT_KEEP=1` run to confirm the failure line arrives inside 12 s, and cite the actual `mage.cpp` line of `"You failed."` in `SUMMON_FAILED`'s comment. If `PRF_SUMMONABLE` or `NO_TELEPORT` ever produce the same text, the same-zone control test proves the failure branch is the save and not a flag.

- [ ] **Step 2: Run it**

Run: `ROTS_IT_LAUNCHER=<yours> build/integration-venv/bin/python -m pytest tests/integration -q -k summon`
Expected: 4 passed (two existing, two new).

- [ ] **Step 3: Commit**

```bash
git add tests/integration/scenarios/test_summon_distance.py
git commit -m "scenarios: a summon across squared map distance 20 always fails; in-zone control lands"
```

---

### Task 3: Fireball on Harncaller

**Files:**
- Modify: `tests/integration/rots_harness/fixtures.py:76`
- Test: `tests/integration/unit/test_fixtures.py`

**Interfaces:**
- Produces: Harncaller's skills `{"summon": 100, "blaze": 100, "fireball": 100}` (`fireball` is a mage skill at profession level 21, `src/consts.cpp:540`; Harncaller's mage level is 30).

- [ ] **Step 1: Write the failing test**

Append to `tests/integration/unit/test_fixtures.py` next to the existing Harncaller assertions:

```python
def test_harncaller_carries_fireball_for_the_splash_scenario() -> None:
    caller = next(spec for spec in fixtures.STANDARD_ROSTER if spec.name == "Harncaller")
    assert caller.skills["fireball"] == 100
    assert caller.professions["mage"] >= 21  # fireball's profession level, consts.cpp skills[]
```

- [ ] **Step 2: Run it to verify it fails** — `KeyError: 'fireball'`.

- [ ] **Step 3: Add the skill**

Change Harncaller's skills dict to `{"summon": 100, "blaze": 100, "fireball": 100}` and extend the comment above the spec: the splash scenario needs a human caster with fireball.

- [ ] **Step 4: Run the unit tests and the existing kill-credit and summon scenarios** (they log Harncaller in):

Run: `build/integration-venv/bin/python -m pytest tests/integration/unit -q` then `... -k "kill_credit or summon"`.
Expected: all pass.

- [ ] **Step 5: Commit**

```bash
git add tests/integration/rots_harness/fixtures.py tests/integration/unit/test_fixtures.py
git commit -m "harness fixtures: Harncaller learns fireball for the splash scenario"
```

---

### Task 4: Fireball splash scenario

**Files:**
- Create: `tests/integration/scenarios/test_fireball_splash.py`

**Interfaces:**
- Consumes: `combat_support.wait_for_engagement`, `combat_support.stat_replies`, `blaze_support.floor_hit`, `records.read_exploits`, mobs 1130 (target) and 1132 (bystander) from `tests/integration/world/mob/11.mob`.

- [ ] **Step 1: Write the scenario**

```python
"""A fireball's splash engages a bystander with the caster and nothing else: the bystander earns
no place in any player's records when the primary target dies, and its own death later credits
the fighter through the share line alone. spell_fireball (mage.cpp ~1960-1990) rolls every other
occupant at 0.2 (0.8 once it fights the caster) and delivers a hit as SPELL_FIREBALL2 through
apply_spell_damage, whose damage_credited() bookkeeping sets the bystander fighting the caster.
Kill credit for a mob death is observable only through group_gain()'s share line
(test_kill_credit.py's module docstring), and no exploit record is written for a mob's death.

Bound: two bystanders make a cast's chance of at least one splash 0.36; SPLASH_ATTEMPTS casts
leave an all-miss chance of 0.64**15, about 0.1%, reported as a failed precondition. Every mob's
hit is reset above fireball's maximum before each cast so no splash or primary hit kills early.
"""
from __future__ import annotations

import pytest

from blaze_support import floor_hit
from combat_support import stat_replies, wait_for_engagement
from rots_harness import fixtures, records
from rots_harness.session import GameSession

pytestmark = pytest.mark.scenario

SPLASH_HIT = "The heat of your fireball burns"  # caster-side hit line, attack type 201, lib/misc/messages:485
SPLASH_ATTEMPTS = 15
SAFE_HIT = 400  # above fireball's primary and splash maximum for a level-30 caster (mage.cpp spell_fireball)
ORC_DEATH_MARKER = "A target orc is dead"
BYSTANDER_DEATH_MARKER = "is dead"  # the bystander's own death line; confirm the mob's short description in 11.mob


def _reset_mobs(imp: GameSession, names: tuple[str, ...]) -> None:
    for name in names:
        imp.command(f"wizset {name} hit {SAFE_HIT}")


def _fighting_line(imp: GameSession, target: str) -> str:
    replies = stat_replies(imp, target, lambda text: "Fighting:" in text)
    return next(line for line in replies[-1].splitlines() if "Fighting:" in line)


def test_splash_engages_the_bystander_with_the_caster_and_manufactures_no_credit(server, imp, caller, fighter, harness) -> None:
    imp.command(f"goto {fixtures.ROOM_ARENA_WEST}")
    for name in ("harncaller", "harnfighter"):
        imp.command(f"transfer {name}")
        imp.command(f"restore {name}")
    caller.expect_room("Arena West")
    fighter.expect_room("Arena West")
    imp.command("load mob 1130")
    imp.command("load mob 1132")
    imp.command("load mob 1132")

    fighter.command("kill target")
    wait_for_engagement(imp, "target", "Harnfighter")

    splashed = False
    for attempt in range(SPLASH_ATTEMPTS):
        _reset_mobs(imp, ("target", "1.bystander", "2.bystander"))
        caller.send_line("cast 'fireball' target")
        reply = caller.expect(["fireball"], timeout=12.0)  # any resolution line for attack type 96 or 201
        reply += caller.drain(0.5)
        if SPLASH_HIT in reply:
            splashed = True
            break
    assert splashed, f"no splash landed in {SPLASH_ATTEMPTS} casts (all-miss chance ~0.1%)"

    bystander = next(name for name in ("1.bystander", "2.bystander") if "Harncaller" in _fighting_line(imp, name))
    assert "Harnfighter" in _fighting_line(imp, "target")

    floor_hit(imp, "target")
    fighter.expect([ORC_DEATH_MARKER], timeout=20.0)
    for name in ("Harncaller", "Harnfighter"):
        assert records.read_exploits(server.lib_dir, name) == [], name
    assert "Harncaller" in _fighting_line(imp, bystander), "the splash engagement outlives the target's death"

    imp.command(f"wizset {bystander} hit 1")
    fighter.command(f"kill {bystander}")
    share = fighter.expect(["You receive your share of experience"], timeout=20.0)
    assert "You receive your share of experience" in share
    for name in ("Harncaller", "Harnfighter"):
        assert records.read_exploits(server.lib_dir, name) == [], name
```

Before finalising: (a) confirm mob 1132's keyword and short description in `mob/11.mob` and replace `bystander` with its keyword; (b) read the caster-side lines of attack types 96 and 201 in `lib/misc/messages` and make `expect`'s marker list the exact resolution lines, not the substring `"fireball"`, so a cast that fizzles is not mistaken for a resolution; (c) confirm `wizset <mob> hit` accepts the `N.keyword` form or address the two bystanders by distinct loads (load 1132 in two rooms is not an option; if numbering fails, use one bystander and `SPLASH_ATTEMPTS = 25`, all-miss 0.8**25 about 0.4%, and update the docstring); (d) whether the share line reaches Harncaller too (present and fighting the bystander) is not asserted, by design.

- [ ] **Step 2: Run it, then the suite**

Run: `ROTS_IT_LAUNCHER=<yours> build/integration-venv/bin/python -m pytest tests/integration -q -k fireball` then the whole suite.
Expected: 1 new pass; suite 71 passed.

- [ ] **Step 3: Commit**

```bash
git add tests/integration/scenarios/test_fireball_splash.py
git commit -m "scenarios: a fireball splash engages the bystander with the caster and manufactures no credit"
```

---

### Task 5: `write_player_text` host field: terminator and width

**Files:**
- Modify: `src/db.cpp:2964`, `src/db.cpp:2990`
- Test: `src/tests/db_loader_tests.cpp` (next to `WritePlayerTextEmitsExactlyThePasswordFieldWidth`, line 546)

- [ ] **Step 1: Write the failing test**

Model it on `WritePlayerTextEmitsExactlyThePasswordFieldWidth` (read it first for the fixture shape):

```cpp
// write_player_text() fills chd.host with strncpy(..., HOST_LEN) and printed it with %s: a
// hostname of HOST_LEN or more characters left no terminator inside the field.
TEST(DbLoader, WritePlayerTextTruncatesAnOverlongHostToTheFieldWidth)
{
    // ... same character/descriptor fixture as the password-width test ...
    const std::string long_host(HOST_LEN + 12, 'h');
    strncpy(character.desc->host, long_host.c_str(), sizeof(character.desc->host) - 1);
    character.desc->host[sizeof(character.desc->host) - 1] = '\0';

    ASSERT_TRUE(write_player_text(&character, 3001, "players/aragorn.txt"));
    const std::string text = /* read the scratch file the way the password test does */;
    const std::string host_line = "host        " + std::string(HOST_LEN, 'h') + "\n";
    EXPECT_NE(text.find(host_line), std::string::npos) << text;
    EXPECT_EQ(text.find(std::string(HOST_LEN + 1, 'h')), std::string::npos) << "host field must be at most HOST_LEN bytes";
}
```

`descriptor_data::host` is `char host[50]` (`src/structs.h:2056`), so a 42-character host fits the descriptor and overflows `chd.host` (`HOST_LEN + 1`, `:1912`).

- [ ] **Step 2: Build and run in the container to verify it fails** (or passes by luck when the stack byte after the field is zero: an ASan run in CI is the real proof; the width assertion still pins the output).

- [ ] **Step 3: Fix**

At `src/db.cpp:2964`:

```cpp
    strncpy(chd.host, ch->desc->host, HOST_LEN);
    chd.host[HOST_LEN] = '\0'; // strncpy leaves no terminator for a host of HOST_LEN or more bytes
```

At `:2990`:

```cpp
    fprintf(pf, "host        %.*s\n", HOST_LEN, chd.host);
```

- [ ] **Step 4: Run the DbLoader suite** — `./bin/tests --gtest_filter="DbLoader.WritePlayerText*"` all pass.

- [ ] **Step 5: Commit**

```bash
git add src/db.cpp src/tests/db_loader_tests.cpp
git commit -m "db: terminate and bound the host field in write_player_text"
```

---

### Task 6: `Crash_alias_save` skips an empty-command alias whole

**Files:**
- Modify: `src/objsave.cpp:1164-1183` (byte-preserving; check each line's ending with `cat -A`)
- Test: `src/tests/db_loader_tests.cpp` (or the objsave test file if one exists; `grep -ln Crash_follower_save src/tests/*.cpp`)

- [ ] **Step 1: Write the failing test**

```cpp
// Crash_alias_save() wrote an alias's 20-byte keyword and then skipped the length for an empty
// command; Crash_alias_load() (objsave.cpp) reads the next keyword's bytes as that length and
// fails the load, losing every alias after the empty one.
TEST(ObjSave, AliasSaveSkipsAnEmptyCommandSoTheLoaderReadsTheRest)
{
    char_data* character = test_support::allocate_test_character(MOB_VOID);
    alias_list first {}; std::strncpy(first.keyword, "a", sizeof(first.keyword)); first.command = strdup("look");
    alias_list empty {}; std::strncpy(empty.keyword, "b", sizeof(empty.keyword)); empty.command = strdup("");
    alias_list last {};  std::strncpy(last.keyword, "c", sizeof(last.keyword));  last.command = strdup("who");
    first.next = &empty; empty.next = &last; last.next = nullptr;
    character->specials.alias = &first;

    FILE* file = tmpfile();
    ASSERT_NE(file, nullptr);
    ASSERT_TRUE(Crash_alias_save(character, file));
    rewind(file);
    // Crash_alias_save writes the sentinel object first; skip it the way Crash_load does before
    // calling Crash_alias_load (read one obj_file_elem), then load.
    obj_file_elem sentinel {};
    ASSERT_EQ(fread(&sentinel, sizeof(sentinel), 1, file), 1u);
    character->specials.alias = nullptr;
    ASSERT_TRUE(Crash_alias_load(character, file));

    std::vector<std::string> keywords;
    for (alias_list* entry = character->specials.alias; entry != nullptr; entry = entry->next) {
        keywords.push_back(entry->keyword);
    }
    EXPECT_EQ(keywords, (std::vector<std::string> { "a", "c" }));
    // release: free the loaded list the way the server does, then the character
    fclose(file);
    test_support::release_test_character(character);
}
```

Read `Crash_obj2store` and `Crash_load` (`src/objsave.cpp:684-760`) to reproduce exactly what precedes `Crash_alias_load` in the stream; if the sentinel is written by `Crash_obj2store` in a different size, adjust the skip. Also read how the loaded list is freed (`alias_list` release in `free_char` or `do_alias`) and mirror it, or leak-check under ASan in CI. Confirm the load order of the loaded list (the loader may prepend).

- [ ] **Step 2: Build and run to verify it fails** — the loader logs `Alias_load error!` and returns FALSE, or reads `"c"` as garbage.

- [ ] **Step 3: Fix**

Move the empty check above the keyword write:

```cpp
        for (list = ch->specials.alias; list; list = list->next) {
            tmp = strlen(list->command);
            if (tmp <= 0)
                continue; // the loader reads keyword, length, command as one record; write none of it

            if (fwrite(&(list->keyword), 20, 1, fp) < 1) {
```

and delete the later `tmp = strlen(...)` / `if (tmp <= 0) continue;` pair.

- [ ] **Step 4: Run** `./bin/tests --gtest_filter="ObjSave.*:DbLoader.*"` — all pass.

- [ ] **Step 5: Commit**

```bash
git add src/objsave.cpp src/tests/db_loader_tests.cpp
git commit -m "objsave: skip an empty-command alias whole so the loader stays aligned"
```

---

### Task 7: `MageTestContext` contract and the bystander comment

**Files:**
- Modify: `src/tests/mage_tests.cpp:93-104`, `:843-852`

- [ ] **Step 1: State the contract on the struct**

Above `struct MageTestContext {` add:

```cpp
// caster, victim and master are stack objects. No test may let damage() kill one of them:
// a lethal hit routes through extract_char() and free_char(), which would free a stack
// address. Tests that need a killable body allocate it with make_fireball_caster() or
// test_support::allocate_test_character() instead.
```

- [ ] **Step 2: Correct the bystander comment**

Replace the sentence in `:846-852` that attributes the `MOB_ISNPC` flag to `exp_with_modifiers` dereferencing an unbooted `zone_table` with the load-bearing reason: a non-NPC bystander that dies in the splash would be extracted and freed as a player body, and `master` is a stack object (see the struct comment). Keep the first sentence about position and hit points. One fact once.

- [ ] **Step 3: Build and run** `./bin/tests --gtest_filter="MageProcTest.*"` — unchanged pass count.

- [ ] **Step 4: Commit**

```bash
git add src/tests/mage_tests.cpp
git commit -m "tests: state MageTestContext's stack-object contract; correct the splash bystander comment"
```

---

### Task 8: Remove the dead `ScopedPlayerTableEntry`

**Files:**
- Modify: `src/tests/account_management_tests.cpp:102-125`

- [ ] **Step 1: Confirm it is unreferenced** — `grep -n "ScopedPlayerTableEntry" src/tests/account_management_tests.cpp` shows only the definition.
- [ ] **Step 2: Delete the class** and any `extern` declarations or includes that only it used (`player_table`, `top_of_p_table` externs if nothing else in the file names them).
- [ ] **Step 3: Build and run** `./bin/tests --gtest_filter="AccountManagement.*"` — unchanged pass count.
- [ ] **Step 4: Commit**

```bash
git add src/tests/account_management_tests.cpp
git commit -m "tests: remove the unused ScopedPlayerTableEntry from account_management_tests"
```

---

### Task 9: Close out

**Files:**
- Modify: `WIP.md` (the "Current state" section), `FEATURES.md` (the follow-up checkbox)

- [ ] **Step 1: Run the whole suite locally** — `make integration PYTHON=build/integration-venv/bin/python`; expected 71 passed. Run the unit binary; expected all pass.
- [ ] **Step 2: Push and read CI** — both jobs green; note the run id.
- [ ] **Step 3: Update the docs** — tick the follow-up box in `FEATURES.md` with the date; add one sentence to `WIP.md`'s "Current state" naming the two scenarios, the two server fixes, and the CI run.
- [ ] **Step 4: Commit**

```bash
git add WIP.md FEATURES.md
git commit -m "docs: harness follow-up complete"
```

## Self-review

- Spec coverage: manual plan row 3 (splash bystander) → Task 4; row 4 (distance falloff) → Tasks 1-2; ledger findings 96-97 → Task 5, 184 alias → Task 6, 144 → Task 7, 184 dead class → Task 8.
- Placeholders: the fireball resolution markers and the bystander keyword are named as values to confirm against `lib/misc/messages` and `mob/11.mob` with the exact procedure; the alias test's stream prefix is to be read from `Crash_load`. No TBDs.
- Type consistency: `ROOM_DISTANT_CELL` (Task 1) is what Task 2 reads; `SUMMON_SUCCESS` is imported from `test_summon`; `stat_replies` and `wait_for_engagement` signatures match `combat_support.py`.

## Deviations by ruling (2026-09-21)

- Tasks 4b (`get_number`) and 4c (`find_all_dots`) were inserted mid-plan: the sanitized CI job found an overlapping `strcpy` in each, live in production on every `N.keyword`/`all.x` input, fixed with `memmove` (ledger `.superpowers/sdd/2026-09-21-harness-follow-up/progress.md` lines 51, 65).
- Task 4 was redesigned in round 3: the melee partner became Harnvictim (level 10) instead of Harnfighter (level 20), because Big Brother refuses a level-30 caster's splash against a level-10 player before either bystander is reached (`big_brother.cpp:381-394`; ledger lines 79-81).
- The same redesign dropped Task 4's finishing-kill phases: no mob death writes an exploit record, so they proved only liveness against regen, PvP and prompt spam, not the invariant under test (ledger line 79).
- The account-migration snapshot decision: it stays in its legacy 80-byte encoding and is not a rollback source for exploit ids; no code change, already stated at `account_management_migration.cpp:150-153` (ledger line 3).
