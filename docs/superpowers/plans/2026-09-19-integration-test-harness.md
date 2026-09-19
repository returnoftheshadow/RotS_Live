# Integration Test Harness (Slice 1) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Boot the real server on a committed synthetic test world, drive it over telnet from pytest, and prove three of this branch's behaviours end to end without a human at a client.

**Architecture:** A `-t` startup flag adds an implementor-only `harness tick` command and a seedable RNG to the server. A host-side Python package assembles a throwaway lib directory (tracked text and misc data, the test world, account-native JSON fixtures), launches one server per pytest session either as a local process or through `docker compose run`, and exposes logged-in `GameSession` objects plus on-disk record readers to scenario tests.

**Tech Stack:** C++17 server (i386, CMake and the legacy `src/Makefile`), GoogleTest, Python 3.11+ standard library, pytest.

**Spec:** `docs/superpowers/specs/2026-09-19-integration-test-harness-design.md`

## Global Constraints

- Slice 1 only: server seam, test world, harness package, boot test, three pilot scenarios. Deferred to later plans by decision: the full scenario catalogue and the CI job (slices 2 and 3), the library-extraction tool `tools/testworld_extract.py`, a Linux-only round-trip test of the character template through the server loader, and any `harness violence` subcommand.
- New C++ is standard C++ (project-local rule); `srandom()` is the one POSIX call, because `number()` in `src/utility.cpp` reads `random()` and nothing else seeds it.
- C++ formatting follows the repository's `.clang-format` (WebKit style, 4 spaces, ~100 columns). Run `cmake --build build --target format` inside the container before reporting a task done.
- No production behaviour changes outside harness mode: every new server path is gated on `harness_mode`.
- Python: 3.11+ syntax, type hints, descriptive names (no one- or two-letter identifiers), standard library plus pytest only.
- Never touch `lib/`, `bin/`, the compose `container_name` `rots`, or host port 1024. Run directories live under `build/integration/<id>/` (git-ignored by the `*build*/` pattern).
- **Container job protocol** (shared Docker, agreed with sibling sessions on 2026-09-19): before any `docker compose run`, `ls /tmp/rots-docker-lock/`; if any `*.lock` exists, wait, do not start. Then write `/tmp/rots-docker-lock/uaf-port-harness-session.lock` containing session name, repo path, service (`rots`), purpose, ISO start time, expected duration; run the job; delete the lock. Never remove another session's lock.
- Builds run inside the i386 container: `docker compose run --rm -T rots bash -lc 'cd /rots && cmake -S src -B build && cmake --build build --target <target> -j8'`. A `comm.h` or `structs.h` edit recompiles nearly everything (20+ minutes); batch the C++ tasks into one build.
- Commits: only when the session holds commit authorization; otherwise leave the task's files staged and report. Commit messages end with the session's attribution lines.
- Other agents share this worktree. Before editing `FEATURES.md`, `WIP.md`, or any file you did not create, run `git status --short` and leave unrelated changes alone.

---

## File Structure

**Server (C++)**
- Modify `src/comm.h`: `StartupOptions.harness_mode`.
- Modify `src/comm.cpp`: parse `-t`; publish the flag; seed hook; usage text.
- Create `src/test_harness.h`, `src/test_harness.cpp`: harness-mode state, `seed_random_from_environment()`, `do_harness`.
- Modify `src/interpre.cpp`: register command 250 `harness`.
- Modify `src/CMakeLists.txt`, `src/Makefile`: add the new source; add the new test source.
- Modify `src/tests/startup_options_tests.cpp`; create `src/tests/test_harness_tests.cpp`.

**Shared telnet primitives (Python)**
- Create `tools/rots_telnet.py` (moved from `tools/account_smoke.py`); modify `tools/account_smoke.py` to import them.

**Test world (data)**
- Create `tests/integration/world/{wld,mob,obj,zon,shp,scr,mdl}/index` and the zone 11 files listed in Task 4.

**Harness package (Python)** under `tests/integration/`
- `rots_harness/__init__.py` (empty), `fixtures.py`, `libbuilder.py`, `launcher.py`, `crashmonitor.py`, `session.py`, `records.py`.
- `fixtures/character.template.json`.
- `conftest.py`, `pytest.ini`, `README.md`.
- `unit/test_fixtures.py`, `unit/test_libbuilder.py`, `unit/test_launcher.py`, `unit/test_crashmonitor.py`, `unit/test_session.py`, `unit/test_records.py`, `unit/test_world.py`.
- `scenarios/test_boot.py`, `scenarios/test_poison_remote_player.py`, `scenarios/test_blaze_after_quit.py`, `scenarios/test_remote_credit_xp_split.py`.

**Build glue**
- Modify `Makefile`: `integration-unit` and `integration` targets.

---

### Task 1: `-t` startup flag

**Files:**
- Modify: `src/comm.h:19-28`
- Modify: `src/comm.cpp:228-330` (`parse_startup_options`), `src/comm.cpp:412-417` (usage text)
- Test: `src/tests/startup_options_tests.cpp`

**Interfaces:**
- Produces: `StartupOptions::harness_mode` (`bool`, default `false`), set by `-t`.

- [ ] **Step 1: Write the failing tests**

Append to `src/tests/startup_options_tests.cpp`, after `AcceptsExplicitProxyFlagWithDashPPort`:

```cpp
TEST(StartupOptions, HarnessModeIsOffByDefault)
{
    StartupOptions options {};
    std::string error_message;
    std::vector<std::string> args = { "ageland" };
    std::vector<char*> argv = build_argv(&args);

    ASSERT_TRUE(parse_startup_options(static_cast<int>(argv.size()), argv.data(), &options, &error_message))
        << error_message;

    EXPECT_FALSE(options.harness_mode);
}

TEST(StartupOptions, AcceptsHarnessModeFlagWithDirectoryAndPositionalPort)
{
    StartupOptions options {};
    std::string error_message;
    std::vector<std::string> args = { "ageland", "-t", "-d", "/tmp/harness-lib", "4321" };
    std::vector<char*> argv = build_argv(&args);

    ASSERT_TRUE(parse_startup_options(static_cast<int>(argv.size()), argv.data(), &options, &error_message))
        << error_message;

    EXPECT_TRUE(options.harness_mode);
    EXPECT_EQ(options.dir, "/tmp/harness-lib");
    EXPECT_EQ(options.port, 4321);
    EXPECT_FALSE(options.has_proxy);
}
```

- [ ] **Step 2: Add the field and the option**

`src/comm.h`, inside `struct StartupOptions` after `bool has_proxy;`:

```cpp
    bool harness_mode; // -t: enable the implementor-only harness command and ROTS_RANDOM_SEED
```

`src/comm.cpp`, in `parse_startup_options`: after `parsed_options.has_proxy = false;` add `parsed_options.harness_mode = false;`. In the `switch`, after `case 'x':` block add:

```cpp
        case 't':
            parsed_options.harness_mode = true;
            break;
```

Update the usage line in `main` to `"Usage: %s [-m] [-q] [-r] [-s] [-t] [-x] [-d pathname] [-p port #] [ port # ]\n"`.

- [ ] **Step 3: Defer the build**

Do not build yet; Task 2 edits the same translation units. Task 2 Step 6 runs both tasks' tests in one container build.

---

### Task 2: Harness module, command registration, seed hook

**Files:**
- Create: `src/test_harness.h`, `src/test_harness.cpp`
- Modify: `src/comm.cpp:398-462` (`main`), `src/interpre.cpp:32` (includes), `src/interpre.cpp:560-561` (name list), `src/interpre.cpp:2242-2244` (`COMMANDO` list)
- Modify: `src/CMakeLists.txt:87` area (`ROTS_SERVER_SOURCES`) and the `ROTS_TEST_SOURCES` list; `src/Makefile:31` (object list) and the dependency rules near line 190
- Test: `src/tests/test_harness_tests.cpp`

**Interfaces:**
- Produces: `extern int harness_mode;` `bool seed_random_from_environment();` `ACMD(do_harness);` and the in-game command `harness tick`, which prints `Harness: hourly tick complete.`

- [ ] **Step 1: Write the failing tests**

Create `src/tests/test_harness_tests.cpp`:

```cpp
#include "../test_harness.h"

#include <gtest/gtest.h>

#include <cstdlib>

namespace {

class HarnessSeedTest : public ::testing::Test {
protected:
    void TearDown() override
    {
        harness_mode = 0;
        unsetenv("ROTS_RANDOM_SEED");
    }
};

} // namespace

TEST_F(HarnessSeedTest, SeedIsIgnoredOutsideHarnessMode)
{
    harness_mode = 0;
    setenv("ROTS_RANDOM_SEED", "42", 1);

    EXPECT_FALSE(seed_random_from_environment()) << "a seed must not apply unless -t was given";
}

TEST_F(HarnessSeedTest, SeedIsIgnoredWhenTheVariableIsUnset)
{
    harness_mode = 1;
    unsetenv("ROTS_RANDOM_SEED");

    EXPECT_FALSE(seed_random_from_environment());
}

TEST_F(HarnessSeedTest, RejectsANonNumericSeed)
{
    harness_mode = 1;
    setenv("ROTS_RANDOM_SEED", "forty-two", 1);

    EXPECT_FALSE(seed_random_from_environment());
}

TEST_F(HarnessSeedTest, SeedMakesTheRandomSequenceRepeatable)
{
    harness_mode = 1;
    setenv("ROTS_RANDOM_SEED", "42", 1);

    ASSERT_TRUE(seed_random_from_environment());
    const long first_draw = random();
    const long second_draw = random();

    ASSERT_TRUE(seed_random_from_environment());
    EXPECT_EQ(random(), first_draw) << "reseeding with the same value must replay the sequence";
    EXPECT_EQ(random(), second_draw);
}
```

- [ ] **Step 2: Create the module**

`src/test_harness.h`:

```cpp
#pragma once

#include "interpre.h"

/*
 * Harness mode: enabled by the -t startup flag (StartupOptions::harness_mode).
 * Off in every autorun deployment. While on, ROTS_RANDOM_SEED seeds the
 * generators and implementors may run `harness tick` to fire the game loop's
 * hourly block on demand, so an integration test never waits a real minute
 * for a poison or room-affect tick.
 */
extern int harness_mode; // 1 while the server runs under the integration harness

// Seeds std::rand() and random() from ROTS_RANDOM_SEED. Applies only in harness
// mode; returns true when a seed was applied.
bool seed_random_from_environment();

ACMD(do_harness);
```

`src/test_harness.cpp`:

```cpp
#include "test_harness.h"

#include "comm.h"
#include "structs.h"
#include "utils.h"

#include <cstdlib>
#include <cstring>
#include <string>

int harness_mode = 0;

// The three calls comm.cpp's game loop makes every SECS_PER_MUD_HOUR * 4 pulses.
void weather_and_time(int mode);
void point_update(void);
void stat_update();

bool seed_random_from_environment()
{
    if (!harness_mode) {
        return false;
    }

    const char* seed_text = std::getenv("ROTS_RANDOM_SEED");
    if (seed_text == nullptr || *seed_text == '\0') {
        return false;
    }

    char* end_of_number = nullptr;
    const unsigned long seed = std::strtoul(seed_text, &end_of_number, 10);
    if (end_of_number == seed_text || *end_of_number != '\0') {
        log("Harness mode: ignoring ROTS_RANDOM_SEED, it is not an unsigned integer.");
        return false;
    }

    std::srand(static_cast<unsigned>(seed));
    srandom(static_cast<unsigned>(seed));

    const std::string message = "Harness mode: random number generators seeded with " + std::to_string(seed) + ".";
    log(message.c_str());
    return true;
}

ACMD(do_harness)
{
    if (!harness_mode) {
        send_to_char("The harness command only exists when the server was started with -t.\r\n", ch);
        return;
    }
    if (GET_LEVEL(ch) < LEVEL_IMPL || IS_NPC(ch) || !ch->desc) {
        send_to_char("You can't do that.\r\n", ch);
        return;
    }

    while (argument && *argument == ' ') {
        ++argument;
    }

    if (argument && std::strncmp(argument, "tick", 4) == 0) {
        // Same order as the game loop's hourly block, so a forced tick is
        // indistinguishable from a real one to everything downstream.
        weather_and_time(1);
        point_update();
        stat_update();
        send_to_char("Harness: hourly tick complete.\r\n", ch);
        return;
    }

    send_to_char("Usage: harness tick\r\n", ch);
}
```

- [ ] **Step 3: Wire main and the command table**

`src/comm.cpp`: add `#include "test_harness.h"` with the other local includes. In `main`, after `no_specials = startup_options.no_specials ? 1 : 0;` add:

```cpp
    harness_mode = startup_options.harness_mode ? 1 : 0;
    if (harness_mode)
        log("Harness mode: -t given; the harness command is enabled.");
```

Immediately after the existing `srandom(time(0));` line (the seed must come after the clock seed, not before) add:

```cpp
    seed_random_from_environment();
```

`src/interpre.cpp`: add `#include "test_harness.h"` after `#include "savebench.h"`. In the command name array, replace

```cpp
    "savebench", // 249
    "\n"
```

with

```cpp
    "savebench", // 249
    "harness", // 250
    "\n"
```

After the `COMMANDO(249, ...)` entry add:

```cpp
    COMMANDO(250, POSITION_DEAD, do_harness, LEVEL_IMPL, FALSE, 0,
        TAR_IGNORE, TAR_IGNORE, 0);
```

Check the table has room: run `grep -n "MAX_CMD_LIST\|cmd_info\[" src/interpre.h src/interpre.cpp src/structs.h`. If `cmd_info` is sized by a constant that is not greater than 250, raise that constant by 10 in the same edit and note it in the commit message.

- [ ] **Step 4: Add the sources to both build systems**

`src/CMakeLists.txt`: add `    test_harness.cpp` to `ROTS_SERVER_SOURCES` next to `savebench.cpp`, and `    tests/test_harness_tests.cpp` to `ROTS_TEST_SOURCES` next to `tests/startup_options_tests.cpp`.

`src/Makefile`: add `test_harness.o` to the object list on line 31 after `savebench.o`, and add a rule next to the `savebench.o` rule:

```make
test_harness.o : test_harness.cpp test_harness.h comm.h interpre.h structs.h utils.h
	$(CC) -c $(CFLAGS) test_harness.cpp
```

- [ ] **Step 5: Format**

Inside the container (container job protocol): `cmake --build build --target format`. Confirm with `git diff --stat` that only the intended files changed.

- [ ] **Step 6: Build and run the C++ tests (container job protocol)**

```bash
docker compose run --rm -T rots bash -lc 'cd /rots && cmake -S src -B build && cmake --build build --target ageland ageland_tests -j8 && ./bin/tests --gtest_filter="StartupOptions.*:HarnessSeedTest.*"'
```

Expected: every `StartupOptions` and `HarnessSeedTest` test passes. Then run the full `./bin/tests` and compare the failing-test list with the 211-failure QEMU baseline recorded in `WIP.md`; the list must be identical.

- [ ] **Step 7: Commit**

```bash
git add src/comm.h src/comm.cpp src/test_harness.h src/test_harness.cpp src/interpre.cpp src/CMakeLists.txt src/Makefile src/tests/startup_options_tests.cpp src/tests/test_harness_tests.cpp
git commit -m "harness: -t startup mode with seeded RNG and an implementor 'harness tick' command"
```

---

### Task 3: Shared telnet primitives

**Files:**
- Create: `tools/rots_telnet.py`
- Modify: `tools/account_smoke.py:32` (`IAC`), `:74-204` (`TelnetStreamSanitizer`, `find_first_marker_end`, `BufferedPromptReader`), `:358-401` (`recv_until`, `contains_any_marker`, `require_markers`), `:417-419` (`send_line`)
- Test: `tools/account_smoke_tests.py` (unchanged, must keep passing)

**Interfaces:**
- Produces: `tools/rots_telnet.py` exporting `IAC`, `TelnetStreamSanitizer`, `find_first_marker_end`, `BufferedPromptReader`, `recv_until`, `contains_any_marker`, `require_markers`, `send_line` with their current signatures.

- [ ] **Step 1: Run the existing tests to record the baseline**

Run: `python3 tools/account_smoke_tests.py`
Expected: all pass (record the count).

- [ ] **Step 2: Move the definitions**

Create `tools/rots_telnet.py` with this header, then paste the eight definitions verbatim from `tools/account_smoke.py` at the line ranges above (they only import `socket` and `time`):

```python
"""Telnet-level primitives shared by tools/account_smoke.py and tests/integration.

Everything here is protocol plumbing: stripping IAC negotiation, waiting for prompt
markers, sending lines. No game knowledge lives in this module.
"""

from __future__ import annotations

import socket
import time

IAC = 255
```

In `tools/account_smoke.py`, delete the moved definitions and add, after the standard-library imports:

```python
sys.path.insert(0, str(Path(__file__).resolve().parent))
from rots_telnet import (  # noqa: E402  (re-exported so account_smoke_tests keeps its names)
    IAC,
    BufferedPromptReader,
    TelnetStreamSanitizer,
    contains_any_marker,
    find_first_marker_end,
    recv_until,
    require_markers,
    send_line,
)
```

- [ ] **Step 3: Run the tests again**

Run: `python3 tools/account_smoke_tests.py`
Expected: same count, all pass. Also `python3 -c "import ast,sys; ast.parse(open('tools/account_smoke.py').read())"` parses.

- [ ] **Step 4: Commit**

```bash
git add tools/rots_telnet.py tools/account_smoke.py
git commit -m "tools: move telnet primitives into rots_telnet.py for reuse"
```

---

### Task 4: Test world

**Files:**
- Create: `tests/integration/world/wld/index`, `wld/11.wld`, `mob/index`, `mob/11.mob`, `obj/index`, `obj/11.obj`, `zon/index`, `zon/11.zon`, `shp/index`, `scr/index`, `scr/11.scr`, `mdl/index`, `mdl/11.mdl`
- Test: `tests/integration/unit/test_world.py`

**Interfaces:**
- Produces: the vnums below, relied on by every scenario. Rooms 1130 arena west, 1131 arena centre, 1132 arena east, 1133 dark cell (north of 1131), 1134 corridor one (south of 1131), 1135 corridor two (south of 1134). Mobs 1130 target orc (zone-loaded into 1132), 1131 snake (zone-loaded into 1134), 1132 bystander, 1133 brute, 1134 pet.

Grammar notes (from `src/db.cpp` `load_rooms`/`load_mobiles`/`load_objects` and `src/zone.cpp` `load_zones`): every string ends with `~`; a room's numeric line is `<zone placeholder> <room_flags> <sector> <level>`; an exit is `D<dir>` then `<description>~`, `<keyword>~`, then `<exit_info> <key> <to_room> <exit_width>`; directions 0..5 are N E S W U D; `DARK` is room flag 1; `SECT_INSIDE` is sector 0. Mob numbers are whitespace-separated in the loader's order, so the line grouping below is for readers. A zone `M` line is `M <if_flag> <mob vnum> <room vnum> <max in world, 0 = none> <percent> <difficulty> <max from this line> <trophy line> <comment>`.

- [ ] **Step 1: Write the structural test**

`tests/integration/unit/test_world.py`:

```python
from __future__ import annotations

import re
from pathlib import Path

WORLD_ROOT = Path(__file__).resolve().parents[1] / "world"


def read_index(category: str) -> list[str]:
    lines = (WORLD_ROOT / category / "index").read_text(encoding="latin-1").splitlines()
    assert lines[-1] == "$", f"{category}/index must end with a $ line"
    return lines[:-1]


def test_every_category_has_an_index_that_names_existing_files() -> None:
    for category in ("wld", "mob", "obj", "zon", "shp", "scr", "mdl"):
        for file_name in read_index(category):
            assert (WORLD_ROOT / category / file_name).is_file(), f"{category}/{file_name} listed but missing"


def test_rooms_are_ascending_and_inside_zone_eleven() -> None:
    text = (WORLD_ROOT / "wld" / "11.wld").read_text(encoding="latin-1")
    vnums = [int(match) for match in re.findall(r"^#(\d+)\s*$", text, flags=re.MULTILINE)]
    assert vnums[-1] == 99999
    room_vnums = vnums[:-1]
    assert room_vnums == sorted(room_vnums)
    assert all(1101 <= vnum <= 1199 for vnum in room_vnums)


def test_every_exit_points_at_a_room_in_the_file() -> None:
    text = (WORLD_ROOT / "wld" / "11.wld").read_text(encoding="latin-1")
    room_vnums = {int(match) for match in re.findall(r"^#(\d+)\s*$", text, flags=re.MULTILINE)}
    exit_targets = [int(match) for match in re.findall(r"^D[0-5]\n.*?~\n.*?~\n-?\d+ -?\d+ (\d+) \d+", text, flags=re.MULTILINE | re.DOTALL)]
    assert exit_targets, "expected at least one exit"
    for target in exit_targets:
        assert target in room_vnums, f"exit to {target} has no room"


def test_zone_loads_the_target_and_the_snake() -> None:
    text = (WORLD_ROOT / "zon" / "11.zon").read_text(encoding="latin-1")
    assert re.search(r"^M 0 1130 1132 ", text, flags=re.MULTILINE), "target orc must load into 1132"
    assert re.search(r"^M 0 1131 1134 ", text, flags=re.MULTILINE), "snake must load into 1134"
    assert text.rstrip().endswith("S")
```

Run: `python3 -m pytest tests/integration/unit/test_world.py -q` (install pytest first: `python3 -m pip install --user pytest`). Expected: FAIL, files missing.

- [ ] **Step 2: Write the index files**

Each `index` is the file name(s) then a `$` line. `wld/index`: `11.wld`, `$`. `mob/index`: `11.mob`, `$`. `obj/index`: `11.obj`, `$`. `zon/index`: `11.zon`, `$`. `scr/index`: `11.scr`, `$`. `mdl/index`: `11.mdl`, `$`. `shp/index`: only `$` (shops are counted differently and may be empty).

- [ ] **Step 3: Write `zon/11.zon`**

```
#11
Harness arena~
Synthetic zone for the integration harness.
~
~
0
? 0 0 5
1199
999
2
M 0 1130 1132 0 100 100 1 1 target orc waits in arena east
M 0 1131 1134 0 100 100 1 1 snake waits in corridor one
S
```

- [ ] **Step 4: Write `wld/11.wld`**

Rooms in ascending order. Start rooms first (each a one-line description, sector 0, no exits), then the arena. Write each room in this exact shape; the arena rooms carry the exits.

```
#1101
Immortal Start~
A quiet white room where implementors arrive.
~
11 0 0 0
S
#1102
Immortal Idle~
A side room for idle immortals.
~
11 0 0 0
S
#1110
Frozen Room~
A cold cell for frozen characters.
~
11 0 0 0
S
#1129
Olog-hai Start~
A dark cave mouth where trolls begin.
~
11 0 0 0
S
#1130
Arena West~
The western end of the harness arena. The arena continues east.
~
11 0 0 0
D1
~
~
0 0 1131 5
S
#1131
Arena Centre~
The centre of the harness arena. Exits lead west, east, north to a dark cell and south to a corridor.
~
11 0 0 0
D0
~
~
0 0 1133 5
D1
~
~
0 0 1132 5
D2
~
~
0 0 1134 5
D3
~
~
0 0 1130 5
S
#1132
Arena East~
The eastern end of the harness arena. The arena continues west.
~
11 0 0 0
D3
~
~
0 0 1131 5
S
#1133
Dark Cell~
You cannot see a thing in here.
~
11 1 0 0
D2
~
~
0 0 1131 5
S
#1134
Corridor One~
A plain corridor. The arena lies north; the corridor continues south.
~
11 0 0 0
D0
~
~
0 0 1131 5
D2
~
~
0 0 1135 5
S
#1135
Corridor Two~
The end of the corridor. The only way is north.
~
11 0 0 0
D0
~
~
0 0 1134 5
S
#1151
Retirement Home~
A peaceful room for retired characters.
~
11 0 0 0
S
#1152
Lost and Found~
Characters with a bad load room arrive here.
~
11 0 0 0
S
#1160
Human Start~
A plain hall where humans, dwarves, hobbits and high elves begin.
~
11 0 0 0
S
#1170
Wood-elf Start~
A glade where wood elves begin.
~
11 0 0 0
S
#1184
Beorning Start~
A wooden lodge where Beornings begin.
~
11 0 0 0
S
#1190
Uruk Idle~
An idle room for Uruk-hai.
~
11 0 0 0
S
#1191
Orc Idle~
An idle room for orcs.
~
11 0 0 0
S
#99999
$~
```

- [ ] **Step 5: Write `mob/11.mob`**

Five `M`-type mobs. Field order per line: level OB parry dodge / hit hit_max / damage regen / gold exp owner / position default_position sex race pref / weight height prog butcher corpse rp_flag / prof mana move bodytype / saving_throw / str int wil dex con lea / language perception resistance vulnerability script spirit will_teach. Act flags: `2` is `MOB_SENTINEL`; `2097154` is `MOB_SENTINEL | MOB_PET`. Race 13 is the common orc (evil side, so good-side characters may attack it). Prog `1` on the snake selects `SPECIAL(snake)` through `real_program()`.

```
#1130
target orc~
a target orc~
A target orc stands here, waiting to be hit.~
It exists so the harness can kill something predictable.
~
2
0 -500 M
5 10 0 0
30 30
2 0
0 5000 0
8 8 1 13 0
150 70 0 0 0 0
0 0 50 1
0
10 10 10 10 10 10
1 10 0 0 0 100 0
#1131
snake harness~
a harness snake~
A harness snake coils here, ready to bite whoever fights it.~
Its bite carries a poison whose origin the server records.
~
2
0 -500 M
10 20 0 0
80 80
3 0
0 2000 0
8 8 1 13 0
20 10 1 0 0 0
0 0 50 1
0
10 10 10 10 10 10
1 10 0 0 0 100 0
#1132
bystander orc~
a bystander orc~
A bystander orc loiters here, involved in nothing.~
It is here to catch splash damage.
~
2
0 0 M
5 10 0 0
60 60
2 0
0 1000 0
8 8 1 13 0
150 70 0 0 0 0
0 0 50 1
0
10 10 10 10 10 10
1 10 0 0 0 100 0
#1133
brute orc~
a brute orc~
A brute orc glowers here, hitting very hard.~
It exists to kill a player who stands and fights.
~
2
0 -800 M
20 60 10 10
400 400
15 0
0 8000 0
8 8 1 13 0
250 80 0 0 0 0
0 0 50 1
0
18 10 10 12 16 10
1 10 0 0 0 100 0
#1134
pet orc~
a pet orc~
A pet orc waits here, flagged as somebody's pet.~
Its MOB_PET flag keeps it out of the real-mob engagement test.
~
2097154
0 -500 M
5 10 0 0
30 30
2 0
0 100 0
8 8 1 13 0
150 70 0 0 0 0
0 0 50 1
0
10 10 10 10 10 10
1 10 0 0 0 100 0
#99999
```

- [ ] **Step 6: Write `obj/11.obj`**

One item, type 12 (`ITEM_OTHER`), takeable (wear flag 1), so the object category counts a record.

```
#1130
harness token~
a harness token~
A small harness token lies here.~
~
12 0 1
0 0 0 0 0
1 0 0
0 0 0 0 0
#99999
```

- [ ] **Step 7: Write the script and mudlle stubs**

`scr/11.scr` (the loader reads `#<num> <name>~`, `<description>~`, then command lines of eight integers until the first integer is 999):

```
#1100 harness stub~
~
999 0 0 0 0 0 0 0
#99999
```

`mdl/11.mdl` (the loader reads `#<num> ...` then program text up to the next `#`; an empty program converts to an empty program):

```
#1101 harness stub
#99999
```

- [ ] **Step 8: Run the structural test**

Run: `python3 -m pytest tests/integration/unit/test_world.py -q`
Expected: 4 passed.

- [ ] **Step 9: Commit**

```bash
git add tests/integration/world tests/integration/unit/test_world.py
git commit -m "tests: synthetic zone 11 world for the integration harness"
```

---

### Task 5: Account-native fixtures

**Files:**
- Create: `tests/integration/fixtures/character.template.json`, `tests/integration/rots_harness/__init__.py`, `tests/integration/rots_harness/fixtures.py`
- Test: `tests/integration/unit/test_fixtures.py`

**Interfaces:**
- Produces:
  - `HARNESS_EMAIL = "harness@example.com"`, `HARNESS_ACCOUNT_NAME = "harness"`, `HARNESS_PASSWORD = "Harness1x"`, `HARNESS_PASSWORD_HASH` (SHA-512 crypt).
  - `@dataclass(frozen=True) class CharacterSpec(name, race, level, load_room, professions: dict[str, int], skills: dict[str, int], hit: int, mana: int, move: int, idnum: int)`.
  - `STANDARD_ROSTER: tuple[CharacterSpec, ...]` = imp, mage, fighter, victim in that order (account menu character numbers 1..4).
  - `account_bucket_for_name(name) -> str`, `account_directory(lib_root, email) -> Path`.
  - `load_character_template(path) -> dict` (raises `ValueError` unless `schema_version == 1`).
  - `write_account(lib_root, roster) -> Path`, `write_character(lib_root, spec, template) -> Path`.

- [ ] **Step 1: Capture the template**

Run from the worktree root (the source is this worktree's git-ignored local data; it holds no secret):

```bash
mkdir -p tests/integration/fixtures
python3 - <<'PY'
import json, pathlib
source = pathlib.Path("lib/accounts/A-E/david.gurley@gmail.com/galadrimor.character.json")
template = json.loads(source.read_text(encoding="utf-8"))
template["character_name"] = "Template"
template["title"] = "the Harness Fixture"
template["description"] = ""
template["identity"]["idnum"] = 0
template["points"]["experience"] = 0
template["timers"] = {"birth": 0, "last_logon": 0, "played_seconds": 0, "retired_on": 0}
template["skills"] = {}
template["affects"] = []
template["flags"]["player"] = []
template["flags"]["affected"] = []
template["state"]["load_room"] = 1101
pathlib.Path("tests/integration/fixtures/character.template.json").write_text(json.dumps(template, indent=2) + "\n", encoding="utf-8")
PY
```

Open the result and confirm it still has every top-level section the loader requires: `schema_version`, `character_name`, `title`, `description`, `identity`, `progression`, `abilities`, `points`, `professions`, `flags`, `conditions`, `timers`, `perception`, `state`, `talks`, `skills`, `affects` (`src/character_json.cpp` `has_all_required_character_sections`).

- [ ] **Step 2: Write the failing tests**

`tests/integration/unit/test_fixtures.py`:

```python
from __future__ import annotations

import json
from pathlib import Path

import pytest

from rots_harness import fixtures

TEMPLATE_PATH = Path(__file__).resolve().parents[1] / "fixtures" / "character.template.json"


def test_bucket_follows_the_servers_alphabet_split() -> None:
    assert fixtures.account_bucket_for_name("harness@example.com") == "F-J"
    assert fixtures.account_bucket_for_name("Alpha") == "A-E"
    assert fixtures.account_bucket_for_name("zed") == "U-Z"
    assert fixtures.account_bucket_for_name("9lives") == "ZZZ"


def test_template_guard_rejects_an_unknown_schema(tmp_path: Path) -> None:
    bad = tmp_path / "template.json"
    bad.write_text(json.dumps({"schema_version": 2}), encoding="utf-8")
    with pytest.raises(ValueError, match="schema_version"):
        fixtures.load_character_template(bad)


def test_write_account_lists_every_roster_character_with_links(tmp_path: Path) -> None:
    account_path = fixtures.write_account(tmp_path, fixtures.STANDARD_ROSTER)

    assert account_path == tmp_path / "accounts" / "F-J" / "harness@example.com" / "account.json"
    data = json.loads(account_path.read_text(encoding="utf-8"))
    assert data["normalized_email"] == fixtures.HARNESS_EMAIL
    assert data["email_verified"] is True
    assert data["password_hash"].startswith("$6$harnesssalt$")
    assert data["characters"] == [spec.name.lower() for spec in fixtures.STANDARD_ROSTER]
    first_link = data["character_links"][0]
    assert first_link == {
        "character_name": "harnessimp",
        "character_path": "harnessimp.character.json",
        "object_path": "harnessimp.objects.json",
        "exploits_path": "harnessimp.exploits.json",
    }


def test_write_character_substitutes_the_spec_and_keeps_every_section(tmp_path: Path) -> None:
    template = fixtures.load_character_template(TEMPLATE_PATH)
    mage = next(spec for spec in fixtures.STANDARD_ROSTER if spec.name == "Harnessmage")

    character_path = fixtures.write_character(tmp_path, mage, template)

    data = json.loads(character_path.read_text(encoding="utf-8"))
    assert character_path.name == "harnessmage.character.json"
    assert data["character_name"] == "Harnessmage"
    assert data["identity"]["idnum"] == mage.idnum
    assert data["identity"]["race"] == mage.race
    assert data["progression"]["level"] == mage.level
    assert data["progression"]["mini_level"] == mage.level * 100
    assert data["professions"]["mage"]["level"] == 30
    assert data["skills"]["blaze"] == 100
    assert data["state"]["load_room"] == mage.load_room
    assert data["abilities"]["temporary"]["hit"] == mage.hit
    assert "prompt" in data["flags"]["preferences"]
    for section in ("identity", "progression", "abilities", "points", "professions", "flags", "conditions", "timers", "perception", "state", "talks", "skills", "affects"):
        assert section in data, section
    assert (character_path.parent / "harnessmage.objects.json").exists()
    assert json.loads((character_path.parent / "harnessmage.exploits.json").read_text(encoding="utf-8")) == {"version": 1, "records": []}
```

Run: `python3 -m pytest tests/integration/unit/test_fixtures.py -q` from the worktree root with `PYTHONPATH=tests/integration`. Expected: FAIL, module missing.

- [ ] **Step 3: Implement**

`tests/integration/rots_harness/__init__.py`: empty.

`tests/integration/rots_harness/fixtures.py`:

```python
"""Account-native character fixtures written straight into a run's lib directory.

Seeding JSON before boot skips the login-flow account creation (email verification,
and the QEMU crash in that path) and gives every character explicit spell knowledge.
"""

from __future__ import annotations

import copy
import json
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Sequence

HARNESS_EMAIL = "harness@example.com"
HARNESS_ACCOUNT_NAME = "harness"
HARNESS_PASSWORD = "Harness1x"
# openssl passwd -6 -salt harnesssalt Harness1x ; the server verifies with crypt(3).
HARNESS_PASSWORD_HASH = "$6$harnesssalt$Zg5ujyEGeH6uCYrL2SwKp4kILHQ1c4vjwbBieWFGPwiir7Cse6SaAzA1okLWkqYwrzW9fWT2gduXOoTT7cSUY/"
HARNESS_PASSWORD_SALT = "harnesssalt"
EXPECTED_SCHEMA_VERSION = 1

RACE_HUMAN = 1
RACE_WOOD_ELF = 3
RACE_MAGUS = 15  # Uruk-Lhuth: evil side, so it may attack the elf victim

ROOM_IMMORTAL_START = 1101
ROOM_ARENA_WEST = 1130
ROOM_ARENA_CENTRE = 1131
ROOM_ARENA_EAST = 1132
ROOM_DARK_CELL = 1133
ROOM_CORRIDOR_ONE = 1134
ROOM_CORRIDOR_TWO = 1135
ROOM_WOOD_ELF_START = 1170


@dataclass(frozen=True)
class CharacterSpec:
    name: str
    race: int
    level: int
    load_room: int
    professions: dict[str, int] = field(default_factory=dict)
    skills: dict[str, int] = field(default_factory=dict)
    hit: int = 100
    mana: int = 100
    move: int = 100
    idnum: int = 0


MAGE_SKILLS = {
    "blaze": 100,
    "poison": 100,
    "mist_of_baazunga": 100,
    "haze": 100,
    "summon": 100,
    "earthquake": 100,
}

STANDARD_ROSTER: tuple[CharacterSpec, ...] = (
    CharacterSpec("Harnessimp", RACE_HUMAN, 100, ROOM_IMMORTAL_START, {"mage": 30, "mystic": 30, "ranger": 30, "warrior": 30}, {}, 1000, 1000, 1000, 9000001),
    CharacterSpec("Harnessmage", RACE_MAGUS, 30, ROOM_ARENA_CENTRE, {"mage": 30, "mystic": 30}, MAGE_SKILLS, 200, 600, 200, 9000002),
    CharacterSpec("Harnessfighter", RACE_HUMAN, 20, ROOM_ARENA_CENTRE, {"warrior": 20}, {}, 200, 50, 200, 9000003),
    CharacterSpec("Harnessvictim", RACE_WOOD_ELF, 10, ROOM_ARENA_CENTRE, {"ranger": 10}, {}, 60, 40, 120, 9000004),
)


def account_bucket_for_name(name: str) -> str:
    normalized = name.strip().lower()
    if not normalized:
        return "ZZZ"
    first = normalized[0]
    for letters, bucket in (("abcde", "A-E"), ("fghij", "F-J"), ("klmno", "K-O"), ("pqrst", "P-T"), ("uvwxyz", "U-Z")):
        if first in letters:
            return bucket
    return "ZZZ"


def account_directory(lib_root: Path, email: str = HARNESS_EMAIL) -> Path:
    return lib_root / "accounts" / account_bucket_for_name(email) / email


def load_character_template(path: Path) -> dict:
    template = json.loads(path.read_text(encoding="utf-8"))
    version = template.get("schema_version")
    if version != EXPECTED_SCHEMA_VERSION:
        raise ValueError(f"character template schema_version {version!r} != {EXPECTED_SCHEMA_VERSION}; recapture the template from a current save")
    return template


def _character_link(name: str) -> dict[str, str]:
    lowered = name.lower()
    return {
        "character_name": lowered,
        "character_path": f"{lowered}.character.json",
        "object_path": f"{lowered}.objects.json",
        "exploits_path": f"{lowered}.exploits.json",
    }


def write_account(lib_root: Path, roster: Sequence[CharacterSpec]) -> Path:
    directory = account_directory(lib_root)
    directory.mkdir(parents=True, exist_ok=True)
    now = int(time.time())
    account = {
        "version": 1,
        "account_name": HARNESS_ACCOUNT_NAME,
        "normalized_email": HARNESS_EMAIL,
        "password_hash": HARNESS_PASSWORD_HASH,
        "password_salt": HARNESS_PASSWORD_SALT,
        "characters": [spec.name.lower() for spec in roster],
        "character_links": [_character_link(spec.name) for spec in roster],
        "email_verified": True,
        "email_verified_by": "harness-fixture",
        "email_verified_at": now,
        "verification_code_hash": "",
        "verification_code_sent_at": 0,
        "verification_code_expires_at": 0,
        "verification_attempt_count": 0,
        "verification_last_attempt_at": 0,
        "blocked": False,
        "block_reason": "",
        "blocked_by": "",
        "blocked_at": 0,
        "created_at": now,
        "updated_at": now,
        "password_reset_at": 0,
        "password_reset_by": "",
        "failed_login_count": 0,
        "failed_login_last_at": 0,
        "failed_login_last_host": "",
        "password_reset_code_hash": "",
        "password_reset_code_sent_at": 0,
        "password_reset_code_expires_at": 0,
        "password_reset_attempt_count": 0,
    }
    account_path = directory / "account.json"
    account_path.write_text(json.dumps(account, indent=2) + "\n", encoding="utf-8")
    return account_path


def write_character(lib_root: Path, spec: CharacterSpec, template: dict) -> Path:
    directory = account_directory(lib_root)
    directory.mkdir(parents=True, exist_ok=True)
    data = copy.deepcopy(template)
    now = int(time.time())

    data["character_name"] = spec.name
    data["title"] = "the Harness Fixture"
    data["identity"]["idnum"] = spec.idnum
    data["identity"]["race"] = spec.race
    data["progression"]["level"] = spec.level
    data["progression"]["mini_level"] = spec.level * 100
    data["progression"]["max_mini_level"] = spec.level * 100
    for pool in ("temporary", "rolled"):
        data["abilities"][pool]["hit"] = spec.hit
        data["abilities"][pool]["mana"] = spec.mana
        data["abilities"][pool]["move"] = spec.move
    for profession in data["professions"]:
        level = spec.professions.get(profession, 0)
        data["professions"][profession] = {"level": level, "points": level, "coeff": level, "experience": 0}
    data["skills"] = dict(spec.skills)
    data["state"]["load_room"] = spec.load_room
    data["timers"] = {"birth": now, "last_logon": now, "played_seconds": 0, "retired_on": 0}
    preferences = data["flags"].setdefault("preferences", [])
    if "prompt" not in preferences:
        preferences.append("prompt")

    lowered = spec.name.lower()
    character_path = directory / f"{lowered}.character.json"
    character_path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
    (directory / f"{lowered}.objects.json").write_text(json.dumps({"version": 1, "objects": []}) + "\n", encoding="utf-8")
    (directory / f"{lowered}.exploits.json").write_text(json.dumps({"version": 1, "records": []}) + "\n", encoding="utf-8")
    return character_path
```

Before finishing, open a real `*.objects.json` from `lib/accounts/A-E/david.gurley@gmail.com/` and match its empty shape exactly (top-level keys); adjust the `objects.json` literal above if the real file uses different keys, and add an assertion for it to `test_write_character_substitutes_the_spec_and_keeps_every_section`.

- [ ] **Step 4: Run the tests**

Run: `PYTHONPATH=tests/integration python3 -m pytest tests/integration/unit/test_fixtures.py -q`
Expected: 4 passed.

- [ ] **Step 5: Commit**

```bash
git add tests/integration/fixtures tests/integration/rots_harness/__init__.py tests/integration/rots_harness/fixtures.py tests/integration/unit/test_fixtures.py
git commit -m "harness: account-native character fixtures and template"
```

---

### Task 6: TestLibBuilder

**Files:**
- Create: `tests/integration/rots_harness/libbuilder.py`
- Test: `tests/integration/unit/test_libbuilder.py`

**Interfaces:**
- Consumes: `fixtures.write_account`, `fixtures.write_character`, `fixtures.load_character_template`, `fixtures.CharacterSpec`.
- Produces: `@dataclass class BuiltLib(lib_dir: Path, roster: tuple[CharacterSpec, ...])`; `class TestLibBuilder(repo_root: Path, world_dir: Path, template_path: Path)` with `build(run_dir: Path, roster: Sequence[CharacterSpec]) -> BuiltLib`.

- [ ] **Step 1: Write the failing tests**

`tests/integration/unit/test_libbuilder.py`:

```python
from __future__ import annotations

import json
from pathlib import Path

from rots_harness import fixtures
from rots_harness.libbuilder import TestLibBuilder

INTEGRATION_ROOT = Path(__file__).resolve().parents[1]
REPO_ROOT = INTEGRATION_ROOT.parents[1]


def build_into(tmp_path: Path):
    builder = TestLibBuilder(REPO_ROOT, INTEGRATION_ROOT / "world", INTEGRATION_ROOT / "fixtures" / "character.template.json")
    return builder.build(tmp_path / "run", fixtures.STANDARD_ROSTER)


def test_build_copies_tracked_text_and_misc_and_the_test_world(tmp_path: Path) -> None:
    built = build_into(tmp_path)

    assert built.lib_dir == tmp_path / "run" / "lib"
    assert (built.lib_dir / "text" / "motd").is_file()
    assert (built.lib_dir / "misc" / "messages").is_file()
    assert (built.lib_dir / "misc" / "socials").is_file()
    assert not (built.lib_dir / "misc" / "pklist").exists(), "git-ignored developer data must not leak in"
    assert (built.lib_dir / "world" / "wld" / "11.wld").is_file()
    assert (built.lib_dir / "world" / "shp" / "index").read_text(encoding="latin-1").strip() == "$"


def test_build_creates_every_runtime_directory_the_server_expects(tmp_path: Path) -> None:
    built = build_into(tmp_path)
    for parent in ("players", "accounts", "account_characters", "plrobjs", "exploits"):
        for bucket in ("A-E", "F-J", "K-O", "P-T", "U-Z", "ZZZ"):
            assert (built.lib_dir / parent / bucket).is_dir(), f"{parent}/{bucket}"
    assert (built.lib_dir / "boards").is_dir()


def test_build_seeds_the_roster(tmp_path: Path) -> None:
    built = build_into(tmp_path)
    account = json.loads((fixtures.account_directory(built.lib_dir) / "account.json").read_text(encoding="utf-8"))
    assert account["characters"] == ["harnessimp", "harnessmage", "harnessfighter", "harnessvictim"]
    assert (fixtures.account_directory(built.lib_dir) / "harnessvictim.character.json").is_file()
    assert built.roster == fixtures.STANDARD_ROSTER
```

Run: `PYTHONPATH=tests/integration python3 -m pytest tests/integration/unit/test_libbuilder.py -q`. Expected: FAIL, module missing.

- [ ] **Step 2: Implement**

`tests/integration/rots_harness/libbuilder.py`:

```python
"""Assembles one run's lib directory: tracked data, the test world, and the fixture roster."""

from __future__ import annotations

import shutil
from dataclasses import dataclass
from pathlib import Path
from typing import Sequence

from rots_harness import fixtures

BUCKETS = ("A-E", "F-J", "K-O", "P-T", "U-Z", "ZZZ")
BUCKETED_DIRECTORIES = ("players", "accounts", "account_characters", "plrobjs", "exploits")
# The lib/misc files git tracks (see `git ls-files lib/misc`); everything else in a
# developer's lib/misc is live data and must not leak into a run.
TRACKED_MISC_FILES = ("badsites", "crimelist", "messages", "mudlle", "mudlle.old", "socials", "wizlist")
WORLD_CATEGORIES = ("wld", "mob", "obj", "zon", "shp", "scr", "mdl")


@dataclass(frozen=True)
class BuiltLib:
    lib_dir: Path
    roster: tuple[fixtures.CharacterSpec, ...]


class TestLibBuilder:
    def __init__(self, repo_root: Path, world_dir: Path, template_path: Path) -> None:
        self._repo_root = repo_root
        self._world_dir = world_dir
        self._template_path = template_path

    def build(self, run_dir: Path, roster: Sequence[fixtures.CharacterSpec]) -> BuiltLib:
        lib_dir = run_dir / "lib"
        if lib_dir.exists():
            shutil.rmtree(lib_dir)
        lib_dir.mkdir(parents=True)

        shutil.copytree(self._repo_root / "lib" / "text", lib_dir / "text")
        misc_dir = lib_dir / "misc"
        misc_dir.mkdir()
        for file_name in TRACKED_MISC_FILES:
            shutil.copy2(self._repo_root / "lib" / "misc" / file_name, misc_dir / file_name)

        for parent in BUCKETED_DIRECTORIES:
            for bucket in BUCKETS:
                (lib_dir / parent / bucket).mkdir(parents=True)
        (lib_dir / "boards").mkdir()

        world_dir = lib_dir / "world"
        world_dir.mkdir()
        for category in WORLD_CATEGORIES:
            shutil.copytree(self._world_dir / category, world_dir / category)

        template = fixtures.load_character_template(self._template_path)
        fixtures.write_account(lib_dir, roster)
        for spec in roster:
            fixtures.write_character(lib_dir, spec, template)

        return BuiltLib(lib_dir=lib_dir, roster=tuple(roster))
```

- [ ] **Step 3: Run the tests**

Run: `PYTHONPATH=tests/integration python3 -m pytest tests/integration/unit/test_libbuilder.py -q`
Expected: 3 passed.

- [ ] **Step 4: Commit**

```bash
git add tests/integration/rots_harness/libbuilder.py tests/integration/unit/test_libbuilder.py
git commit -m "harness: TestLibBuilder assembles a run's lib directory"
```

---

### Task 7: Server launchers

**Files:**
- Create: `tests/integration/rots_harness/launcher.py`
- Test: `tests/integration/unit/test_launcher.py`

**Interfaces:**
- Produces:
  - `@dataclass class ServerHandle(host: str, port: int, log_path: Path, process: subprocess.Popen, container_name: str | None)` with `is_alive() -> bool`, `exit_status() -> int | None`.
  - `class ServerLauncher` (abstract): `start(run_dir: Path, lib_dir: Path, port: int, seed: int) -> ServerHandle`; `stop(handle: ServerHandle) -> None`.
  - `LocalProcessLauncher(binary: Path)`; `DockerComposeLauncher(repo_root: Path, binary_relative: str, lock_dir: Path | None)`.
  - `allocate_free_port() -> int`; `wait_for_port(host, port, timeout, process, log_path) -> None` (raises `RuntimeError` with the log tail on failure).
  - Module constants `LOCK_FILE_NAME = "uaf-port-harness-it.lock"`, `DEFAULT_LOCK_DIR = Path("/tmp/rots-docker-lock")`.

- [ ] **Step 1: Write the failing tests**

`tests/integration/unit/test_launcher.py`:

```python
from __future__ import annotations

from pathlib import Path

import pytest

from rots_harness import launcher


def test_local_launcher_builds_the_server_command(tmp_path: Path) -> None:
    local = launcher.LocalProcessLauncher(binary=tmp_path / "bin" / "ageland")
    command = local.command(lib_dir=tmp_path / "run" / "lib", port=4321)
    assert command == [str(tmp_path / "bin" / "ageland"), "-t", "-d", str(tmp_path / "run" / "lib"), "4321"]


def test_docker_launcher_maps_run_dir_into_the_container_and_publishes_a_random_port(tmp_path: Path) -> None:
    repo_root = tmp_path
    run_dir = repo_root / "build" / "integration" / "abc123"
    docker = launcher.DockerComposeLauncher(repo_root=repo_root, binary_relative="bin/ageland", lock_dir=None)
    command = docker.command(run_dir=run_dir, lib_dir=run_dir / "lib", port=4321, container_name="rots-it-abc123", seed=7)
    assert command[:5] == ["docker", "compose", "run", "--rm", "-T"]
    assert "--name" in command and command[command.index("--name") + 1] == "rots-it-abc123"
    assert "-p" in command and command[command.index("-p") + 1] == "127.0.0.1::4321"
    assert "ROTS_RANDOM_SEED=7" in command
    assert "--service-ports" not in command
    shell_script = command[-1]
    assert "cd /rots/build/integration/abc123" in shell_script
    assert "exec /rots/bin/ageland -t -d /rots/build/integration/abc123/lib 4321" in shell_script


def test_docker_launcher_refuses_to_start_while_another_lock_exists(tmp_path: Path) -> None:
    lock_dir = tmp_path / "locks"
    lock_dir.mkdir()
    (lock_dir / "someone-else.lock").write_text("session: other\n", encoding="utf-8")
    docker = launcher.DockerComposeLauncher(repo_root=tmp_path, binary_relative="bin/ageland", lock_dir=lock_dir)
    with pytest.raises(launcher.DockerLockHeld, match="someone-else.lock"):
        docker.acquire_lock(purpose="unit test")


def test_docker_launcher_writes_and_removes_its_own_lock(tmp_path: Path) -> None:
    lock_dir = tmp_path / "locks"
    lock_dir.mkdir()
    docker = launcher.DockerComposeLauncher(repo_root=tmp_path, binary_relative="bin/ageland", lock_dir=lock_dir)
    docker.acquire_lock(purpose="unit test")
    lock_path = lock_dir / launcher.LOCK_FILE_NAME
    assert lock_path.is_file()
    assert "purpose: unit test" in lock_path.read_text(encoding="utf-8")
    docker.release_lock()
    assert not lock_path.exists()


def test_docker_launcher_skips_the_lock_when_the_directory_is_absent(tmp_path: Path) -> None:
    docker = launcher.DockerComposeLauncher(repo_root=tmp_path, binary_relative="bin/ageland", lock_dir=tmp_path / "missing")
    docker.acquire_lock(purpose="unit test")  # no exception, nothing written
    docker.release_lock()
    assert not (tmp_path / "missing").exists()


def test_allocate_free_port_returns_a_high_port() -> None:
    port = launcher.allocate_free_port()
    assert 1024 < port < 65536
```

Run: `PYTHONPATH=tests/integration python3 -m pytest tests/integration/unit/test_launcher.py -q`. Expected: FAIL, module missing.

- [ ] **Step 2: Implement**

`tests/integration/rots_harness/launcher.py`:

```python
"""Starts and stops one server process per execution environment and captures its log.

LocalProcessLauncher runs bin/ageland directly (Linux, CI). DockerComposeLauncher runs it
through `docker compose run` for hosts that cannot execute the i386 binary (macOS). Neither
touches the compose container_name, host port 1024, lib/, or bin/.
"""

from __future__ import annotations

import os
import socket
import subprocess
import time
import uuid
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path

LOOPBACK = "127.0.0.1"
LOCK_FILE_NAME = "uaf-port-harness-it.lock"
DEFAULT_LOCK_DIR = Path("/tmp/rots-docker-lock")


class DockerLockHeld(RuntimeError):
    pass


@dataclass
class ServerHandle:
    host: str
    port: int
    log_path: Path
    process: subprocess.Popen
    container_name: str | None = None

    def is_alive(self) -> bool:
        return self.process.poll() is None

    def exit_status(self) -> int | None:
        return self.process.poll()


def allocate_free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as probe:
        probe.bind((LOOPBACK, 0))
        return probe.getsockname()[1]


def read_log_tail(log_path: Path, max_bytes: int = 4000) -> str:
    if not log_path.exists():
        return ""
    data = log_path.read_bytes()
    return data[-max_bytes:].decode("latin-1", errors="replace")


def wait_for_port(host: str, port: int, timeout_seconds: float, process: subprocess.Popen, log_path: Path) -> None:
    deadline = time.monotonic() + timeout_seconds
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError(f"server exited with status {process.returncode} before listening; log tail:\n{read_log_tail(log_path)}")
        try:
            with socket.create_connection((host, port), timeout=1.0):
                return
        except OSError:
            time.sleep(0.25)
    raise RuntimeError(f"server did not listen on {host}:{port} within {timeout_seconds}s; log tail:\n{read_log_tail(log_path)}")


class ServerLauncher:
    def start(self, run_dir: Path, lib_dir: Path, port: int, seed: int) -> ServerHandle:
        raise NotImplementedError

    def stop(self, handle: ServerHandle) -> None:
        raise NotImplementedError


class LocalProcessLauncher(ServerLauncher):
    def __init__(self, binary: Path, startup_timeout: float = 60.0) -> None:
        self._binary = binary
        self._startup_timeout = startup_timeout

    def command(self, lib_dir: Path, port: int) -> list[str]:
        return [str(self._binary), "-t", "-d", str(lib_dir), str(port)]

    def start(self, run_dir: Path, lib_dir: Path, port: int, seed: int) -> ServerHandle:
        if not self._binary.exists():
            raise RuntimeError(f"server binary missing at {self._binary}; build it first")
        log_path = run_dir / "game.log"
        environment = {"PATH": os.environ.get("PATH", ""), "HOME": os.environ.get("HOME", ""), "ROTS_RANDOM_SEED": str(seed)}
        with log_path.open("wb") as log_file:
            process = subprocess.Popen(self.command(lib_dir, port), cwd=run_dir, env=environment, stdout=log_file, stderr=subprocess.STDOUT)
        handle = ServerHandle(LOOPBACK, port, log_path, process)
        wait_for_port(LOOPBACK, port, self._startup_timeout, process, log_path)
        return handle

    def stop(self, handle: ServerHandle) -> None:
        if handle.is_alive():
            handle.process.terminate()
            try:
                handle.process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                handle.process.kill()
                handle.process.wait(timeout=10)


class DockerComposeLauncher(ServerLauncher):
    def __init__(self, repo_root: Path, binary_relative: str = "bin/ageland", lock_dir: Path | None = DEFAULT_LOCK_DIR, service: str = "rots", startup_timeout: float = 300.0) -> None:
        self._repo_root = repo_root
        self._binary_relative = binary_relative
        self._lock_dir = lock_dir
        self._service = service
        self._startup_timeout = startup_timeout
        self._lock_path: Path | None = None

    def container_path(self, host_path: Path) -> str:
        relative = host_path.resolve().relative_to(self._repo_root.resolve())
        return "/rots/" + relative.as_posix()

    def command(self, run_dir: Path, lib_dir: Path, port: int, container_name: str, seed: int) -> list[str]:
        script = f"cd {self.container_path(run_dir)} && exec /rots/{self._binary_relative} -t -d {self.container_path(lib_dir)} {port}"
        return [
            "docker", "compose", "run", "--rm", "-T",
            "--name", container_name,
            "-p", f"{LOOPBACK}::{port}",
            "-e", f"ROTS_RANDOM_SEED={seed}",
            self._service, "bash", "-lc", script,
        ]

    def acquire_lock(self, purpose: str) -> None:
        if self._lock_dir is None or not self._lock_dir.is_dir():
            return
        others = [path for path in self._lock_dir.glob("*.lock") if path.name != LOCK_FILE_NAME]
        if others:
            names = ", ".join(path.name for path in others)
            raise DockerLockHeld(f"another session holds the Docker lock ({names}); see {self._lock_dir / 'README.txt'}")
        self._lock_path = self._lock_dir / LOCK_FILE_NAME
        started = datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
        self._lock_path.write_text(
            f"session: uaf-port-harness-it\nrepo: {self._repo_root}\nservice: {self._service}\npurpose: {purpose}\nstart: {started}\nduration: 30m\n",
            encoding="utf-8",
        )

    def release_lock(self) -> None:
        if self._lock_path is not None and self._lock_path.exists():
            self._lock_path.unlink()
        self._lock_path = None

    def start(self, run_dir: Path, lib_dir: Path, port: int, seed: int) -> ServerHandle:
        self.acquire_lock(purpose="integration test server")
        container_name = f"rots-it-{uuid.uuid4().hex[:12]}"
        log_path = run_dir / "game.log"
        environment = dict(os.environ)
        environment["ROTS_UID"] = str(os.getuid())
        environment["ROTS_GID"] = str(os.getgid())
        with log_path.open("wb") as log_file:
            process = subprocess.Popen(self.command(run_dir, lib_dir, port, container_name, seed), cwd=self._repo_root, env=environment, stdout=log_file, stderr=subprocess.STDOUT)
        host_port = self._wait_for_published_port(container_name, port, process, log_path)
        handle = ServerHandle(LOOPBACK, host_port, log_path, process, container_name)
        wait_for_port(LOOPBACK, host_port, self._startup_timeout, process, log_path)
        return handle

    def _wait_for_published_port(self, container_name: str, container_port: int, process: subprocess.Popen, log_path: Path) -> int:
        deadline = time.monotonic() + self._startup_timeout
        while time.monotonic() < deadline:
            if process.poll() is not None:
                raise RuntimeError(f"docker compose run exited with {process.returncode}; log tail:\n{read_log_tail(log_path)}")
            result = subprocess.run(["docker", "port", container_name, f"{container_port}/tcp"], capture_output=True, text=True)
            if result.returncode == 0 and ":" in result.stdout:
                return int(result.stdout.strip().rsplit(":", 1)[1])
            time.sleep(1.0)
        raise RuntimeError(f"container {container_name} never published port {container_port}; log tail:\n{read_log_tail(log_path)}")

    def stop(self, handle: ServerHandle) -> None:
        try:
            if handle.container_name:
                subprocess.run(["docker", "stop", "-t", "5", handle.container_name], capture_output=True)
            if handle.is_alive():
                try:
                    handle.process.wait(timeout=30)
                except subprocess.TimeoutExpired:
                    handle.process.kill()
        finally:
            self.release_lock()
```

- [ ] **Step 3: Run the tests**

Run: `PYTHONPATH=tests/integration python3 -m pytest tests/integration/unit/test_launcher.py -q`
Expected: 6 passed.

- [ ] **Step 4: Commit**

```bash
git add tests/integration/rots_harness/launcher.py tests/integration/unit/test_launcher.py
git commit -m "harness: local and docker compose server launchers with the shared lock convention"
```

---

### Task 8: CrashMonitor

**Files:**
- Create: `tests/integration/rots_harness/crashmonitor.py`
- Test: `tests/integration/unit/test_crashmonitor.py`

**Interfaces:**
- Consumes: `launcher.ServerHandle`.
- Produces: `class CrashMonitor(handle: ServerHandle, allowed_syserr_fragments: tuple[str, ...] = DEFAULT_ALLOWED)` with `check() -> list[str]` (new problems since the previous call; empty means healthy) and `DEFAULT_ALLOWED`.

- [ ] **Step 1: Write the failing tests**

`tests/integration/unit/test_crashmonitor.py`:

```python
from __future__ import annotations

import subprocess
from pathlib import Path

from rots_harness.crashmonitor import CrashMonitor
from rots_harness.launcher import ServerHandle


class FakeProcess:
    def __init__(self, returncode: int | None = None) -> None:
        self.returncode = returncode

    def poll(self) -> int | None:
        return self.returncode


def make_handle(tmp_path: Path, returncode: int | None = None) -> ServerHandle:
    log_path = tmp_path / "game.log"
    log_path.write_text("", encoding="latin-1")
    return ServerHandle("127.0.0.1", 1, log_path, FakeProcess(returncode))  # type: ignore[arg-type]


def test_clean_log_reports_nothing(tmp_path: Path) -> None:
    handle = make_handle(tmp_path)
    handle.log_path.write_text("Sep 19 :: Boot db -- DONE.\n", encoding="latin-1")
    assert CrashMonitor(handle).check() == []


def test_allowed_syserr_lines_are_ignored_but_others_are_reported(tmp_path: Path) -> None:
    handle = make_handle(tmp_path)
    handle.log_path.write_text("x :: Could not open /judp/password, disabling JUDP.\nx :: SYSERR: boot error - 0 records counted\n", encoding="latin-1")
    problems = CrashMonitor(handle).check()
    assert len(problems) == 1 and "0 records counted" in problems[0]


def test_sanitizer_report_and_process_exit_are_reported(tmp_path: Path) -> None:
    handle = make_handle(tmp_path, returncode=-11)
    handle.log_path.write_text("==12==ERROR: AddressSanitizer: heap-use-after-free on address\n", encoding="latin-1")
    problems = CrashMonitor(handle).check()
    assert any("AddressSanitizer" in problem for problem in problems)
    assert any("exited" in problem for problem in problems)


def test_check_only_reports_new_lines(tmp_path: Path) -> None:
    handle = make_handle(tmp_path)
    monitor = CrashMonitor(handle)
    handle.log_path.write_text("x :: SYSERR: first\n", encoding="latin-1")
    assert len(monitor.check()) == 1
    assert monitor.check() == []
    with handle.log_path.open("a", encoding="latin-1") as log_file:
        log_file.write("x :: SYSERR: second\n")
    assert len(monitor.check()) == 1
```

Run: `PYTHONPATH=tests/integration python3 -m pytest tests/integration/unit/test_crashmonitor.py -q`. Expected: FAIL.

- [ ] **Step 2: Implement**

`tests/integration/rots_harness/crashmonitor.py`:

```python
"""Decides whether the server log or process state shows a crash since the last check."""

from __future__ import annotations

from rots_harness.launcher import ServerHandle

DEFAULT_ALLOWED: tuple[str, ...] = (
    "Could not open /judp/password",
    "Mail boot failed",
    "Could not open help file",
    "Unable to open banfile",
)
SANITIZER_MARKERS = ("AddressSanitizer", "LeakSanitizer", "UndefinedBehaviorSanitizer", "runtime error:")
SIGNAL_MARKER = "Error: signal"


class CrashMonitor:
    def __init__(self, handle: ServerHandle, allowed_syserr_fragments: tuple[str, ...] = DEFAULT_ALLOWED) -> None:
        self._handle = handle
        self._allowed = allowed_syserr_fragments
        self._offset = 0
        self._exit_reported = False

    def check(self) -> list[str]:
        problems: list[str] = []
        for line in self._new_lines():
            if any(marker in line for marker in SANITIZER_MARKERS) or SIGNAL_MARKER in line:
                problems.append(f"crash marker in log: {line.strip()}")
            elif "SYSERR" in line and not any(fragment in line for fragment in self._allowed):
                problems.append(f"unexpected SYSERR: {line.strip()}")
        status = self._handle.exit_status()
        if status is not None and not self._exit_reported:
            self._exit_reported = True
            problems.append(f"server process exited with status {status}")
        return problems

    def _new_lines(self) -> list[str]:
        if not self._handle.log_path.exists():
            return []
        data = self._handle.log_path.read_bytes()
        fresh = data[self._offset:]
        self._offset = len(data)
        return fresh.decode("latin-1", errors="replace").splitlines()
```

- [ ] **Step 3: Run the tests**

Run: `PYTHONPATH=tests/integration python3 -m pytest tests/integration/unit/test_crashmonitor.py -q`
Expected: 4 passed.

- [ ] **Step 4: Commit**

```bash
git add tests/integration/rots_harness/crashmonitor.py tests/integration/unit/test_crashmonitor.py
git commit -m "harness: CrashMonitor classifies log and process state"
```

---

### Task 9: GameSession and Transcript

**Files:**
- Create: `tests/integration/rots_harness/session.py`
- Test: `tests/integration/unit/test_session.py`

**Interfaces:**
- Consumes: `tools/rots_telnet.TelnetStreamSanitizer`, `fixtures.CharacterSpec`, `fixtures.HARNESS_EMAIL`, `fixtures.HARNESS_PASSWORD`, `launcher.ServerHandle`.
- Produces:
  - `class Transcript(text: str)`: `contains(marker) -> bool`, `hit_points() -> tuple[int, int] | None` (parses `HP :[cur/max` from `stat`), `room_name() -> str | None` (first non-empty line of a `look`).
  - `class GameSession(handle, character: CharacterSpec, character_number: int, transcript_dir: Path)`: `login()`, `command(text, timeout=8.0) -> Transcript`, `expect(markers, timeout) -> str`, `drain(timeout=0.5) -> str`, `cast(spell, target=None, success_markers=(), attempts=6) -> Transcript`, `quit()`, `drop_link()`, `close()`; property `everything` (all text received so far).
  - Login markers are module constants: `LOGIN_EMAIL_PROMPT = "Account email:"`, `LOGIN_PASSWORD_PROMPT = "Account password:"`, `ACCOUNT_MENU_PROMPT = "Choice:"`, `CHARACTER_NUMBER_PROMPT = "Character number:"`, `CHARACTER_MENU_PROMPT = "Make your choice:"`, `ENTER_GAME_MARKER = "Here we go..."`, `PROMPT_TERMINATORS = (">", "]")`.

- [ ] **Step 1: Write the failing tests**

`tests/integration/unit/test_session.py`:

```python
from __future__ import annotations

from rots_harness.session import Transcript, ends_with_prompt


def test_transcript_parses_the_stat_hit_point_line() -> None:
    transcript = Transcript("Str:[10/10(10)]\nHP :[15/60+3(0)]  Stamina :[0/40+1(0)]  Move :[120/120+2(0)] Spirit:[100/100+0]\n")
    assert transcript.hit_points() == (15, 60)


def test_transcript_reports_no_hit_points_when_the_line_is_absent() -> None:
    assert Transcript("nothing here").hit_points() is None


def test_transcript_room_name_is_the_first_non_empty_line() -> None:
    assert Transcript("\nWood-elf Start\n   A glade where wood elves begin.\n") .room_name() == "Wood-elf Start"


def test_prompt_detection_accepts_trailing_whitespace_and_both_terminators() -> None:
    assert ends_with_prompt("You breathe out fire.\nHP:Healthy > ")
    assert ends_with_prompt("some output\n]")
    assert not ends_with_prompt("You start to cast a spell...")
```

Run: `PYTHONPATH=tests/integration python3 -m pytest tests/integration/unit/test_session.py -q`. Expected: FAIL.

- [ ] **Step 2: Implement**

`tests/integration/rots_harness/session.py`:

```python
"""One logged-in telnet connection: sends commands, returns transcripts, parses the few lines scenarios read."""

from __future__ import annotations

import re
import socket
import sys
import time
from pathlib import Path

from rots_harness import fixtures
from rots_harness.launcher import ServerHandle

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "tools"))
from rots_telnet import TelnetStreamSanitizer  # noqa: E402

LOGIN_EMAIL_PROMPT = "Account email:"
LOGIN_PASSWORD_PROMPT = "Account password:"
ACCOUNT_MENU_PROMPT = "Choice:"
CHARACTER_NUMBER_PROMPT = "Character number:"
CHARACTER_MENU_PROMPT = "Make your choice:"
ENTER_GAME_MARKER = "Here we go..."
PROMPT_TERMINATORS = (">", "]")
HIT_POINT_PATTERN = re.compile(r"HP :\[(\d+)/(\d+)")


class SessionTimeout(AssertionError):
    pass


def ends_with_prompt(text: str) -> bool:
    stripped = text.rstrip()
    return bool(stripped) and stripped.endswith(PROMPT_TERMINATORS)


class Transcript:
    def __init__(self, text: str) -> None:
        self.text = text

    def contains(self, marker: str) -> bool:
        return marker in self.text

    def hit_points(self) -> tuple[int, int] | None:
        match = HIT_POINT_PATTERN.search(self.text)
        if match is None:
            return None
        return int(match.group(1)), int(match.group(2))

    def room_name(self) -> str | None:
        for line in self.text.splitlines():
            if line.strip():
                return line.strip()
        return None


class GameSession:
    def __init__(self, handle: ServerHandle, character: fixtures.CharacterSpec, character_number: int, transcript_dir: Path) -> None:
        self.character = character
        self._character_number = character_number
        self._socket = socket.create_connection((handle.host, handle.port), timeout=5.0)
        self._socket.settimeout(0.25)
        self._sanitizer = TelnetStreamSanitizer()
        self._consumed = 0
        self._transcript_path = transcript_dir / f"{character.name.lower()}.txt"

    @property
    def everything(self) -> str:
        return self._sanitizer.text

    def _pump(self) -> bool:
        try:
            chunk = self._socket.recv(4096)
        except socket.timeout:
            return False
        if not chunk:
            raise ConnectionError(f"{self.character.name}: server closed the connection")
        self._sanitizer.feed(chunk)
        self._transcript_path.write_text(self._sanitizer.text, encoding="utf-8")
        return True

    def _unconsumed(self) -> str:
        return self._sanitizer.text[self._consumed:]

    def _consume(self) -> str:
        text = self._unconsumed()
        self._consumed = len(self._sanitizer.text)
        return text

    def expect(self, markers: tuple[str, ...] | list[str], timeout: float = 8.0) -> str:
        deadline = time.monotonic() + timeout
        while True:
            pending = self._unconsumed()
            if any(marker in pending for marker in markers):
                return self._consume()
            if time.monotonic() >= deadline:
                raise SessionTimeout(f"{self.character.name}: none of {markers!r} within {timeout}s; pending text:\n{pending[-1500:]}")
            self._pump()

    def drain(self, timeout: float = 0.5) -> str:
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            self._pump()
        return self._consume()

    def send_line(self, line: str) -> None:
        self._socket.sendall(line.encode("latin-1", errors="replace") + b"\n")

    def command(self, text: str, timeout: float = 8.0) -> Transcript:
        self.drain(0.1)
        self.send_line(text)
        deadline = time.monotonic() + timeout
        while True:
            pending = self._unconsumed()
            if ends_with_prompt(pending):
                return Transcript(self._consume())
            if time.monotonic() >= deadline:
                raise SessionTimeout(f"{self.character.name}: no prompt after {text!r} within {timeout}s; pending text:\n{pending[-1500:]}")
            self._pump()

    def login(self) -> None:
        self.expect([LOGIN_EMAIL_PROMPT], 15.0)
        self.send_line(fixtures.HARNESS_EMAIL)
        self.expect([LOGIN_PASSWORD_PROMPT])
        self.send_line(fixtures.HARNESS_PASSWORD)
        self.expect([ACCOUNT_MENU_PROMPT])
        self.send_line("2")
        self.expect([CHARACTER_NUMBER_PROMPT])
        self.send_line(str(self._character_number))
        self.expect([CHARACTER_MENU_PROMPT])
        self.send_line("1")
        self.expect([ENTER_GAME_MARKER], 15.0)
        self.command("look")

    def cast(self, spell: str, target: str | None = None, success_markers: tuple[str, ...] = (), attempts: int = 6, timeout: float = 12.0) -> Transcript:
        words = f"cast '{spell}'" + (f" {target}" if target else "")
        last = Transcript("")
        for _attempt in range(attempts):
            self.send_line(words)
            deadline = time.monotonic() + timeout
            while time.monotonic() < deadline:
                self._pump()
                pending = self._unconsumed()
                if success_markers and any(marker in pending for marker in success_markers):
                    return Transcript(self._consume())
                if not success_markers and ends_with_prompt(pending):
                    return Transcript(self._consume())
            last = Transcript(self._consume())
        raise SessionTimeout(f"{self.character.name}: {words!r} never produced {success_markers!r} in {attempts} attempts; last transcript:\n{last.text[-1500:]}")

    def quit(self) -> None:
        self.send_line("quit")
        try:
            self.expect(["Goodbye", "As you quit"], 8.0)
        finally:
            self.close()

    def drop_link(self) -> None:
        self.close()

    def close(self) -> None:
        try:
            self._socket.close()
        except OSError:
            pass
```

- [ ] **Step 3: Run the tests**

Run: `PYTHONPATH=tests/integration python3 -m pytest tests/integration/unit/test_session.py -q`
Expected: 4 passed.

- [ ] **Step 4: Commit**

```bash
git add tests/integration/rots_harness/session.py tests/integration/unit/test_session.py
git commit -m "harness: GameSession drives one logged-in telnet connection"
```

---

### Task 10: Records

**Files:**
- Create: `tests/integration/rots_harness/records.py`
- Test: `tests/integration/unit/test_records.py`

**Interfaces:**
- Consumes: `fixtures.account_directory`.
- Produces: constants `EXPLOIT_PK = 1`, `EXPLOIT_DEATH = 2`, `EXPLOIT_MOBDEATH = 6`, `EXPLOIT_POISON = 11`; `@dataclass(frozen=True) class ExploitRecord(type: int, victim_name: str, victim_level: int, killer_level: int, int_param: int)`; `read_exploits(lib_dir, name) -> list[ExploitRecord]`; `read_character(lib_dir, name) -> dict`.

- [ ] **Step 1: Write the failing tests**

`tests/integration/unit/test_records.py`:

```python
from __future__ import annotations

import json
from pathlib import Path

from rots_harness import fixtures, records


def test_read_exploits_returns_typed_records_in_file_order(tmp_path: Path) -> None:
    directory = fixtures.account_directory(tmp_path)
    directory.mkdir(parents=True)
    (directory / "harnessvictim.exploits.json").write_text(json.dumps({
        "version": 1,
        "records": [
            {"type": 11, "chtime": "now", "victim_id": 0, "victim_name": "", "victim_level": 10, "killer_level": 0, "int_param": 0},
            {"type": 2, "chtime": "now", "victim_id": 9000002, "victim_name": "Harnessmage", "victim_level": 10, "killer_level": 30, "int_param": 0},
        ],
    }), encoding="utf-8")

    result = records.read_exploits(tmp_path, "Harnessvictim")

    assert [record.type for record in result] == [records.EXPLOIT_POISON, records.EXPLOIT_DEATH]
    assert result[1].victim_name == "Harnessmage"
    assert result[1].killer_level == 30


def test_read_exploits_of_a_character_without_a_file_is_empty(tmp_path: Path) -> None:
    assert records.read_exploits(tmp_path, "Nobody") == []
```

Run: `PYTHONPATH=tests/integration python3 -m pytest tests/integration/unit/test_records.py -q`. Expected: FAIL.

- [ ] **Step 2: Implement**

`tests/integration/rots_harness/records.py`:

```python
"""Reads a character's on-disk JSON records for assertions."""

from __future__ import annotations

import json
from dataclasses import dataclass
from pathlib import Path

from rots_harness import fixtures

EXPLOIT_PK = 1
EXPLOIT_DEATH = 2
EXPLOIT_MOBDEATH = 6
EXPLOIT_POISON = 11


@dataclass(frozen=True)
class ExploitRecord:
    type: int
    victim_name: str
    victim_level: int
    killer_level: int
    int_param: int


def read_exploits(lib_dir: Path, name: str) -> list[ExploitRecord]:
    path = fixtures.account_directory(lib_dir) / f"{name.lower()}.exploits.json"
    if not path.exists():
        return []
    data = json.loads(path.read_text(encoding="utf-8"))
    return [
        ExploitRecord(int(entry["type"]), str(entry.get("victim_name", "")), int(entry.get("victim_level", 0)), int(entry.get("killer_level", 0)), int(entry.get("int_param", 0)))
        for entry in data.get("records", [])
    ]


def read_character(lib_dir: Path, name: str) -> dict:
    path = fixtures.account_directory(lib_dir) / f"{name.lower()}.character.json"
    return json.loads(path.read_text(encoding="utf-8"))
```

- [ ] **Step 3: Run the tests**

Run: `PYTHONPATH=tests/integration python3 -m pytest tests/integration/unit/test_records.py -q`
Expected: 2 passed.

- [ ] **Step 4: Commit**

```bash
git add tests/integration/rots_harness/records.py tests/integration/unit/test_records.py
git commit -m "harness: typed readers for exploit and character JSON"
```

---

### Task 11: pytest wiring, Makefile targets, README

**Files:**
- Create: `tests/integration/conftest.py`, `tests/integration/pytest.ini`, `tests/integration/README.md`
- Modify: `Makefile` (help text, `.PHONY`, two targets)

**Interfaces:**
- Consumes: everything from Tasks 5 to 10.
- Produces pytest fixtures: `server` (session; `HarnessServer` with `.handle`, `.lib_dir`, `.roster`, `.monitor`), `imp`, `mage`, `fighter`, `victim` (function-scoped logged-in `GameSession`s, quit on teardown), `harness` (`Harness.tick()` runs `harness tick` as the imp and returns its `Transcript`). Environment: `ROTS_IT_LAUNCHER=local|docker`, `ROTS_IT_BINARY`, `ROTS_IT_SEED` (default `20260919`), `ROTS_IT_KEEP=1`, `ROTS_IT_DOCKER_LOCK_DIR`.

- [ ] **Step 1: pytest.ini**

`tests/integration/pytest.ini`:

```ini
[pytest]
testpaths = unit scenarios
pythonpath = .
markers =
    scenario: needs a running server (built by the session-scoped server fixture)
```

- [ ] **Step 2: conftest.py**

```python
from __future__ import annotations

import os
import platform
import shutil
import uuid
from dataclasses import dataclass
from pathlib import Path

import pytest

from rots_harness import fixtures
from rots_harness.crashmonitor import CrashMonitor
from rots_harness.launcher import DEFAULT_LOCK_DIR, DockerComposeLauncher, LocalProcessLauncher, ServerHandle, ServerLauncher, allocate_free_port
from rots_harness.libbuilder import TestLibBuilder
from rots_harness.session import GameSession, Transcript

INTEGRATION_ROOT = Path(__file__).resolve().parent
REPO_ROOT = INTEGRATION_ROOT.parents[1]
DEFAULT_SEED = 20260919


@dataclass
class HarnessServer:
    handle: ServerHandle
    lib_dir: Path
    run_dir: Path
    roster: tuple[fixtures.CharacterSpec, ...]
    monitor: CrashMonitor

    def character_number(self, name: str) -> int:
        return next(index for index, spec in enumerate(self.roster, start=1) if spec.name == name)

    def spec(self, name: str) -> fixtures.CharacterSpec:
        return next(spec for spec in self.roster if spec.name == name)


def choose_launcher() -> ServerLauncher:
    binary_relative = os.environ.get("ROTS_IT_BINARY", "bin/ageland")
    mode = os.environ.get("ROTS_IT_LAUNCHER")
    if mode is None:
        mode = "local" if platform.system() == "Linux" else "docker"
    if mode == "local":
        return LocalProcessLauncher(REPO_ROOT / binary_relative)
    if mode == "docker":
        lock_dir = Path(os.environ["ROTS_IT_DOCKER_LOCK_DIR"]) if "ROTS_IT_DOCKER_LOCK_DIR" in os.environ else DEFAULT_LOCK_DIR
        return DockerComposeLauncher(REPO_ROOT, binary_relative, lock_dir)
    raise RuntimeError(f"ROTS_IT_LAUNCHER must be 'local' or 'docker', not {mode!r}")


@pytest.fixture(scope="session")
def server(request: pytest.FixtureRequest) -> HarnessServer:
    run_dir = REPO_ROOT / "build" / "integration" / uuid.uuid4().hex[:12]
    run_dir.mkdir(parents=True)
    built = TestLibBuilder(REPO_ROOT, INTEGRATION_ROOT / "world", INTEGRATION_ROOT / "fixtures" / "character.template.json").build(run_dir, fixtures.STANDARD_ROSTER)
    launcher = choose_launcher()
    seed = int(os.environ.get("ROTS_IT_SEED", DEFAULT_SEED))
    handle = launcher.start(run_dir, built.lib_dir, allocate_free_port(), seed)
    harness_server = HarnessServer(handle, built.lib_dir, run_dir, built.roster, CrashMonitor(handle))
    try:
        yield harness_server
    finally:
        launcher.stop(handle)
        keep = os.environ.get("ROTS_IT_KEEP") == "1" or request.session.testsfailed > 0
        if keep:
            print(f"\nrun directory kept at {run_dir}")
        else:
            shutil.rmtree(run_dir, ignore_errors=True)


@pytest.fixture(autouse=True)
def fail_on_server_crash(request: pytest.FixtureRequest):
    yield
    if "server" in request.fixturenames:
        harness_server: HarnessServer = request.getfixturevalue("server")
        problems = harness_server.monitor.check()
        if problems:
            pytest.fail("server problems during this test:\n" + "\n".join(problems))


def _login(server: HarnessServer, name: str) -> GameSession:
    session = GameSession(server.handle, server.spec(name), server.character_number(name), server.run_dir)
    session.login()
    return session


def _session_fixture(fixture_name: str, character_name: str):
    @pytest.fixture(name=fixture_name)
    def session(server: HarnessServer):
        game_session = _login(server, character_name)
        yield game_session
        try:
            game_session.quit()
        except Exception:
            game_session.close()
    return session


imp = _session_fixture("imp", "Harnessimp")
mage = _session_fixture("mage", "Harnessmage")
fighter = _session_fixture("fighter", "Harnessfighter")
victim = _session_fixture("victim", "Harnessvictim")


class Harness:
    def __init__(self, imp_session: GameSession) -> None:
        self._imp = imp_session

    def tick(self) -> Transcript:
        transcript = self._imp.command("harness tick", timeout=20.0)
        assert transcript.contains("Harness: hourly tick complete."), transcript.text
        return transcript


@pytest.fixture
def harness(imp: GameSession) -> Harness:
    return Harness(imp)
```

- [ ] **Step 3: Makefile targets**

Add `integration-unit integration` to `.PHONY`, two help lines, and:

```make
integration-unit:
	python3 -m pytest tests/integration/unit -q

integration:
	python3 -m pytest tests/integration -q
```

- [ ] **Step 4: README**

`tests/integration/README.md` (short): what the harness is, the one-time `python3 -m pip install --user pytest`, `make integration-unit` (no server), `make integration` (builds a run lib under `build/integration/`, boots the server; on macOS through Docker with `ROTS_IT_BINARY=build/sp/bin/ageland` if the plain build crashes under QEMU), the environment variables listed in this task's Interfaces, the shared-Docker lock rule, and where transcripts and `game.log` land (`build/integration/<id>/`, kept with `ROTS_IT_KEEP=1`).

- [ ] **Step 5: Run the unit suite through the Makefile**

Run: `make integration-unit`
Expected: every unit test from Tasks 4 to 10 passes (27 tests).

- [ ] **Step 6: Commit**

```bash
git add tests/integration/conftest.py tests/integration/pytest.ini tests/integration/README.md Makefile
git commit -m "harness: pytest fixtures, Makefile targets and README"
```

---

### Task 12: Boot scenario (slice 1 acceptance)

**Files:**
- Create: `tests/integration/scenarios/test_boot.py`

**Interfaces:**
- Consumes: fixtures `server`, `imp`, `victim`, `harness`.

- [ ] **Step 1: Write the scenario**

```python
from __future__ import annotations

import pytest

from rots_harness import fixtures, records

pytestmark = pytest.mark.scenario


def test_server_boots_on_the_test_world_and_the_imp_can_tick(server, imp, harness) -> None:
    look = imp.command("look")
    assert look.contains("Immortal Start"), look.text

    tick = harness.tick()
    assert tick.contains("Harness: hourly tick complete.")


def test_wizset_and_stat_agree_on_a_victims_hit_points(server, imp, victim) -> None:
    imp.command(f"goto {fixtures.ROOM_ARENA_CENTRE}")
    imp.command("wizset harnessvictim hit 5")

    stat = imp.command("stat harnessvictim")
    assert stat.hit_points() is not None, stat.text
    current, maximum = stat.hit_points()
    assert current == 5
    assert maximum >= 5

    imp.command("restore harnessvictim")
    assert imp.command("stat harnessvictim").hit_points()[0] == maximum


def test_roster_records_start_empty(server) -> None:
    for spec in server.roster:
        assert records.read_exploits(server.lib_dir, spec.name) == []
```

- [ ] **Step 2: Run it for real**

On a Linux host with a native build: `make integration` after `make build`.
On this Mac (container job protocol; the launcher writes its own lock, so only confirm no other lock exists first): `ROTS_IT_LAUNCHER=docker python3 -m pytest tests/integration/scenarios/test_boot.py -q -s`.

Expected: 3 passed. If boot fails, read `build/integration/<id>/game.log`: a world-file grammar error names the file and record; fix the world file, rerun. If the login stalls, keep the run dir (`ROTS_IT_KEEP=1`) and read `build/integration/<id>/harnessimp.txt`. Iterate until green; every fix goes into the world files, fixtures, or session markers, never into the server.

- [ ] **Step 3: Tighten the allow-list**

Any `SYSERR` line the boot emits on the test world that is expected (record it with the line) goes into `DEFAULT_ALLOWED` in `crashmonitor.py` with a one-line reason in the tuple's comment, and the corresponding unit test gets the fragment added.

- [ ] **Step 4: Commit**

```bash
git add tests/integration/scenarios/test_boot.py tests/integration/rots_harness/crashmonitor.py tests/integration/unit/test_crashmonitor.py
git commit -m "harness: boot scenario proves login, wizset/stat and harness tick end to end"
```

---

### Task 13: Pilot A: remote player poison, unengaged death

**Files:**
- Create: `tests/integration/scenarios/test_poison_remote_player.py`

Behaviour under test (spec scenario 2, control and punishment case for a player poisoner): the mage poisons the victim and walks away; the victim dies of the poison tick alone. Expected: gentle penalty (hit points at a quarter of maximum, mana zero), `EXPLOIT_POISON` and an `EXPLOIT_DEATH` naming the mage on the victim, `EXPLOIT_PK` naming the victim on the mage, no `EXPLOIT_MOBDEATH`.

- [ ] **Step 1: Write the scenario**

```python
from __future__ import annotations

import pytest

from rots_harness import fixtures, records

pytestmark = pytest.mark.scenario

POISON_LANDED = ("You feel very sick.",)
POISON_RESISTED = ("You feel your body fend off the poison.",)
DEATH_MARKER = "You are dead!  Sorry..."


def poison_until_it_lands(mage, victim, attempts: int = 8) -> None:
    for _attempt in range(attempts):
        mage.send_line("cast 'poison' harnessvictim")
        try:
            text = victim.expect(POISON_LANDED + POISON_RESISTED, timeout=12.0)
        except AssertionError as timeout:
            pytest.fail(f"{timeout}\nmage side:\n{mage.drain(0.5)[-1500:]}")
        if POISON_LANDED[0] in text:
            return
        mage.drain(0.5)
    pytest.fail(f"poison never landed in {attempts} casts")


def test_remote_player_poison_death_is_gentle_and_fully_attributed(server, imp, mage, victim, harness) -> None:
    imp.command(f"goto {fixtures.ROOM_ARENA_CENTRE}")
    imp.command("restore harnessmage")
    imp.command("restore harnessvictim")
    imp.command("wizset harnessvictim hit 12")  # the cast lands 5, each tick lands 5

    poison_until_it_lands(mage, victim)
    mage.command("west")  # the poisoner is now rooms away and never in the victim's fight
    assert mage.command("look").room_name() == "Arena West"

    died = False
    for _tick in range(4):
        harness.tick()
        text = victim.drain(1.0)
        if DEATH_MARKER in text:
            died = True
            break
    assert died, "the victim should have died of the poison ticks"

    look = victim.command("look")
    assert look.room_name() == "Wood-elf Start", look.text

    stat = imp.command("stat harnessvictim")
    current, maximum = stat.hit_points()
    assert current == maximum // 4, f"gentle revive expected hp {maximum // 4}, got {current}: {stat.text}"

    victim_records = records.read_exploits(server.lib_dir, "Harnessvictim")
    types = [record.type for record in victim_records]
    assert records.EXPLOIT_POISON in types, victim_records
    assert records.EXPLOIT_MOBDEATH not in types, victim_records
    deaths = [record for record in victim_records if record.type == records.EXPLOIT_DEATH]
    assert any(record.victim_name.lower() == "harnessmage" for record in deaths), victim_records

    mage_records = records.read_exploits(server.lib_dir, "Harnessmage")
    kills = [record for record in mage_records if record.type == records.EXPLOIT_PK]
    assert any(record.victim_name.lower() == "harnessvictim" for record in kills), mage_records
```

- [ ] **Step 2: Run it**

Same launch command as Task 12 with `tests/integration/scenarios/test_poison_remote_player.py`.
Expected: 1 passed. If the cast is refused (position, mana, or a side restriction), read the mage's transcript in the run dir; adjust the roster (mana, race) in `fixtures.py`, not the server. If the death record shape differs from the assertion (for example the death entry carries the killer in another field), open the victim's `harnessvictim.exploits.json` in the run dir, correct the assertion to the actual contract, and note the finding in the commit message.

- [ ] **Step 3: Commit**

```bash
git add tests/integration/scenarios/test_poison_remote_player.py
git commit -m "harness: pilot scenario for remote player poison with an unengaged death"
```

---

### Task 14: Pilot B: blaze keeps ticking after the caster quits

**Files:**
- Create: `tests/integration/scenarios/test_blaze_after_quit.py`

Behaviour under test (spec scenario 1, quit variant): a room affect outlives its caster. Expected: ticks still land on the victim after the mage has quit, the eventual death credits nobody (no death record naming the mage), the server survives every tick.

- [ ] **Step 1: Write the scenario**

```python
from __future__ import annotations

import pytest

from rots_harness import fixtures, records

pytestmark = pytest.mark.scenario

BLAZE_CAST = ("You breathe out fire.",)
DEATH_MARKER = "You are dead!  Sorry..."


def test_blaze_ticks_survive_the_casters_quit_and_credit_nobody(server, imp, mage, victim, harness) -> None:
    imp.command(f"goto {fixtures.ROOM_ARENA_CENTRE}")
    imp.command("restore harnessmage")
    imp.command("wizset harnessvictim maxhit 300")
    imp.command("restore harnessvictim")

    victim.command("west")  # keep the victim out of the cast itself so nobody engages anybody
    mage.cast("blaze", success_markers=BLAZE_CAST)
    mage.quit()

    victim.command("east")
    assert victim.command("look").room_name() == "Arena Centre"
    before = imp.command("stat harnessvictim").hit_points()[0]

    harness.tick()
    after = imp.command("stat harnessvictim").hit_points()[0]
    assert after < before, f"the blaze should still tick after its caster quit ({before} -> {after})"

    died = False
    for _tick in range(30):
        harness.tick()
        if DEATH_MARKER in victim.drain(1.0):
            died = True
            break
    assert died, "the blaze ticks should eventually kill the victim"
    assert victim.command("look").room_name() == "Wood-elf Start"

    victim_records = records.read_exploits(server.lib_dir, "Harnessvictim")
    assert not any(record.victim_name.lower() == "harnessmage" for record in victim_records), f"a departed caster must never be named: {victim_records}"
```

Note: the `mage` fixture's teardown calls `quit()` on an already closed session; the fixture catches that and closes. Keep it.

- [ ] **Step 2: Run it**

Expected: 1 passed. If the blaze expires before the victim dies (duration is the caster level in ticks), lower `maxhit` in the scenario rather than raising the tick budget.

- [ ] **Step 3: Commit**

```bash
git add tests/integration/scenarios/test_blaze_after_quit.py
git commit -m "harness: pilot scenario for a room affect outliving its caster"
```

---

### Task 15: Pilot C: remote-credit XP split

**Files:**
- Create: `tests/integration/scenarios/test_remote_credit_xp_split.py`

Behaviour under test (spec scenario 3, remote-credit split): a blaze tick kills a mob that a fighter is engaged with while the caster stands elsewhere. Expected: the fighter sees `You receive your share of experience`, the mage does not, the server survives.

- [ ] **Step 1: Write the scenario**

```python
from __future__ import annotations

import pytest

from rots_harness import fixtures

pytestmark = pytest.mark.scenario

BLAZE_CAST = ("You breathe out fire.",)
SHARE_MARKER = "You receive your share of experience"


def test_engaged_fighter_gets_the_share_and_the_remote_caster_does_not(server, imp, mage, fighter, harness) -> None:
    imp.command(f"goto {fixtures.ROOM_ARENA_WEST}")
    imp.command("restore harnessmage")

    mage.command("west")  # 1131 -> 1130 with the imp
    mage.cast("blaze", success_markers=BLAZE_CAST)  # empty room: the cast engages nobody
    mage.command("east")  # back to 1131, remote from the death room

    imp.command("load mob 1130")
    imp.command("wizset orc hit 9")  # below the smallest halved blaze tick, so the tick kills it

    fighter.command("west")
    fighter.command("kill orc")
    harness.tick()

    fighter_text = fighter.drain(2.0)
    mage_text = mage.drain(2.0)
    assert SHARE_MARKER in fighter_text, fighter_text
    assert SHARE_MARKER not in mage_text, mage_text
```

- [ ] **Step 2: Run it**

Expected: 1 passed. If the fighter's first melee round kills the orc before the tick (the fighter's transcript shows the orc dying before `Harness: hourly tick complete.` appears in the imp's), add `imp.command("wizset harnessfighter damage 0")` before `kill orc` so melee cannot land the kill, and record in the commit message that melee damage was zeroed for this scenario.

- [ ] **Step 3: Commit**

```bash
git add tests/integration/scenarios/test_remote_credit_xp_split.py
git commit -m "harness: pilot scenario for the remote-credit XP split"
```

---

### Task 16: Wrap-up

- [ ] **Step 1: Full runs**

`make integration-unit` (host), then the three scenario files plus `test_boot.py` in one `make integration` run on the chosen launcher. Expected: all pass; the run directory is removed.

- [ ] **Step 2: Repository logs**

Append to `WIP.md` a dated entry for slice 1 with: the four scenario names, the launcher used, the pass counts, the C++ test baseline comparison from Task 2 Step 6, and any allow-list additions. `FEATURES.md` already carries the scope entry; tick its slice-1 checklist item.

- [ ] **Step 3: Reviews**

Run `/style-pass` on the C++ files and `/architecture-pass` on the harness package per the repository review workflow; record outcomes in `WIP.md`.

- [ ] **Step 4: Commit**

```bash
git add WIP.md FEATURES.md
git commit -m "docs: integration harness slice 1 delivered"
```
