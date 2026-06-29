# Consistent-Snapshot Autosave Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the periodic autosave a point-in-time consistent snapshot — every connected player saved together each cycle (default 30s, configurable) — so a crash during PvP/group fights restores everyone to the same moment; and crash-proof impactful events by saving on every exploit record.

**Architecture:** Replace `Crash_save_all`'s dirty-gated per-player loop with a save-*all*-connected-players loop reusing the existing atomic `save_char`/`Crash_crashsave` (no new batch/temp machinery — serial saves make the shared scratch safe). Drive the cadence from a small pure scheduling helper (the one unit-testable seam) wired into the heartbeat. Delete the desyncing 10% kill-XP save. Add a single `save_char` hook at the end of `write_exploits` (the sole sink for exploit records) plus one direct save in the angel-reroll path.

**Tech Stack:** C++17, single-threaded DikuMUD/CircleMUD-derived MUD, 32-bit, Linux. Build is **Docker-only** (`docker compose run --rm rots …`); gtest suite under `src/tests/`.

**Spec:** `docs/superpowers/specs/2026-06-29-consistent-snapshot-autosave-design.md` (read it; it carries the rationale and the rejected alternatives).

## Global Constraints

- **New functions must be portable, cross-compilable standard C++** (`std::filesystem` w/ `std::error_code`, `std::chrono`, `<fstream>`) — not POSIX-only. (The new helper here is pure integer logic, so this is trivially satisfied.)
- **Preserve existing line endings per file.** CRLF: `config.cpp`, `objsave.cpp`, `profs.cpp`, `spec_pro.cpp`. LF: `comm.cpp`, `fight.cpp`, `db.cpp`, `structs.h`, both `Makefile`s. New files (`crashsave_schedule.*`, the new test) are LF. Verify with `grep -c $'\r' <file>` before and after editing; an edit that flips endings is a defect.
- **Do NOT run clang-format.** Repo disables it (`.claude/.no-autoformat`). Match surrounding style by hand. Allman braces in `objsave.cpp` (its existing style); K&R/`profs.cpp`-local style elsewhere — follow each file.
- **The Docker g++ build is the only authority.** Host-side clang/IDE diagnostics (e.g. `MAX`/`MIN`/`unlink` "undeclared", gtest-not-found, `std::filesystem` namespace errors) are **false positives** — ignore them; trust only the Docker build output.
- **Reuse the shipped atomic primitives** — `save_char`→`save_player`→`finalize_player_file_rename` and `Crash_crashsave`→`finalize_save_file`. Do not reimplement file writes.
- **Document every class/struct data member** with a `//` comment describing its role (project C++ convention).

## File Structure

| File | Change | Responsibility |
|---|---|---|
| `src/crashsave_schedule.h` / `.cpp` | **new** | Pure crash-save scheduling math: seconds→pulses (clamped) + a per-pulse accumulator. The unit-testable seam. |
| `src/tests/crashsave_schedule_tests.cpp` | **new** | gtests for the scheduling helper. |
| `src/Makefile` | modify | Add `crashsave_schedule.o` to `OBJFILES` + build rule; add the header to `comm.o`'s deps. |
| `src/tests/Makefile` | modify | Add `crashsave_schedule.o` to `OBJFILES` + build rule; add the test to `SRCS`. |
| `src/config.cpp` | modify (CRLF) | Repurpose `autosave_time` from minutes(4) to **seconds(30)**. |
| `src/comm.cpp` | modify (LF) | Replace the minute-granular gate + `mins_since_crashsave` with the helper-driven timer. |
| `src/objsave.cpp` | modify (CRLF) | `Crash_save_all`: save all connected players, `notify=0`, drop the dirty gate. |
| `src/fight.cpp` | modify (LF) | Delete the 10% kill-XP `save_char`. |
| `src/db.cpp` | modify (LF) | `write_exploits`: `save_char` the recorded character before returning. |
| `src/spec_pro.cpp` | modify (CRLF) | Angel reroll: `save_char` immediately after the reroll increment. |

**Unchanged by design:** `Emergency_save` (signal-handler safety), `raw_kill`'s death save, `advance_level`'s level-up save.

## Build & Test commands (used throughout)

```bash
# Build the game (authoritative compile):
docker compose run --rm rots bash -c "cd /rots/src && make all 2>&1 | tail -6"
# Expect: ends by linking ../bin/ageland, no "error:" lines.

# Build + run the gtest suite:
docker compose run --rm rots bash -c "cd /rots/src/tests && make all 2>&1 | tail -6 && ../../bin/tests 2>&1 | tail -10"
# Expect: "[  PASSED  ] N tests."
```

Run from the repo root (`/Users/drelidan/Documents/GitHub/RotS_Live`). The existing suite is **8 tests** (5 `SaveFileFinalize`/`PlayerFinalize` + 3 others); Task 1 adds 5 more → 13.

---

### Task 1: Crash-save scheduling helper (new, unit-tested)

**Files:**
- Create: `src/crashsave_schedule.h`, `src/crashsave_schedule.cpp`
- Create: `src/tests/crashsave_schedule_tests.cpp`
- Modify: `src/Makefile`, `src/tests/Makefile`

**Interfaces:**
- Produces: `int autosave_interval_pulses(int interval_seconds, int tics_per_second)` and `struct AutosaveTimer { int pulses_since_fire; bool tick(int interval_pulses); }` — consumed by Task 2 (`comm.cpp`).

- [ ] **Step 1: Create the header** `src/crashsave_schedule.h` (LF):

```cpp
#ifndef CRASHSAVE_SCHEDULE_H
#define CRASHSAVE_SCHEDULE_H

// Pure crash-save scheduling logic, isolated from game state so it can be
// unit-tested without linking the game loop. See
// docs/superpowers/specs/2026-06-29-consistent-snapshot-autosave-design.md.

// Convert a configured crash-save interval (seconds) into game-loop pulses.
// Clamped so a mis-set 0/negative interval (or tic rate) still yields at least
// a one-second cadence rather than firing every pulse.
int autosave_interval_pulses(int interval_seconds, int tics_per_second);

// Per-pulse accumulator for the periodic crash-save. The game loop calls tick()
// once per pulse; it returns true (and resets) when interval_pulses have elapsed.
struct AutosaveTimer {
    // Pulses counted since the timer last fired; reset to 0 each time tick() fires.
    int pulses_since_fire = 0;

    // Advance by one pulse. Returns true exactly when the interval elapses,
    // then resets the counter. interval_pulses is clamped to >= 1.
    bool tick(int interval_pulses);
};

#endif // CRASHSAVE_SCHEDULE_H
```

- [ ] **Step 2: Create the implementation** `src/crashsave_schedule.cpp` (LF):

```cpp
#include "crashsave_schedule.h"

int autosave_interval_pulses(int interval_seconds, int tics_per_second) {
    if (tics_per_second < 1) {
        tics_per_second = 1;
    }
    if (interval_seconds < 1) {
        interval_seconds = 1; // never faster than once per second
    }
    return interval_seconds * tics_per_second;
}

bool AutosaveTimer::tick(int interval_pulses) {
    if (interval_pulses < 1) {
        interval_pulses = 1;
    }
    if (++pulses_since_fire >= interval_pulses) {
        pulses_since_fire = 0;
        return true;
    }
    return false;
}
```

- [ ] **Step 3: Write the failing tests** `src/tests/crashsave_schedule_tests.cpp` (LF):

```cpp
#include "../crashsave_schedule.h"
#include <gtest/gtest.h>

TEST(CrashsaveSchedule, IntervalPulsesBasic) {
    EXPECT_EQ(autosave_interval_pulses(30, 4), 120);
    EXPECT_EQ(autosave_interval_pulses(1, 4), 4);
    EXPECT_EQ(autosave_interval_pulses(60, 4), 240);
}

TEST(CrashsaveSchedule, IntervalPulsesClampsNonPositive) {
    EXPECT_EQ(autosave_interval_pulses(0, 4), 4);   // seconds clamped to >= 1
    EXPECT_EQ(autosave_interval_pulses(-5, 4), 4);
    EXPECT_EQ(autosave_interval_pulses(30, 0), 30); // tics clamped to >= 1
}

TEST(CrashsaveSchedule, TimerFiresOnIntervalAndResets) {
    AutosaveTimer t;
    for (int i = 0; i < 119; i++) {
        EXPECT_FALSE(t.tick(120)) << "unexpected fire at pulse " << i;
    }
    EXPECT_TRUE(t.tick(120)); // 120th pulse fires
    for (int i = 0; i < 119; i++) {
        EXPECT_FALSE(t.tick(120)) << "unexpected fire after reset at pulse " << i;
    }
    EXPECT_TRUE(t.tick(120)); // fires again after reset
}

TEST(CrashsaveSchedule, TimerIntervalOfOneFiresEveryPulse) {
    AutosaveTimer t;
    EXPECT_TRUE(t.tick(1));
    EXPECT_TRUE(t.tick(1));
}

TEST(CrashsaveSchedule, TimerClampsNonPositiveInterval) {
    AutosaveTimer t;
    EXPECT_TRUE(t.tick(0)); // clamped to 1 -> fires every pulse
}
```

- [ ] **Step 4: Wire the new file into `src/Makefile`.** In `OBJFILES` (lines 25-31), append `crashsave_schedule.o` to the last line (after `zone.o`). Add a build rule near the other rules (e.g. after the `clock.o` rule at line 45-46):

```make
crashsave_schedule.o : crashsave_schedule.cpp crashsave_schedule.h
	$(CC) -c $(CFLAGS) crashsave_schedule.cpp
```

- [ ] **Step 5: Wire the new file into `src/tests/Makefile`.** Append `crashsave_schedule.o` to `OBJFILES` (lines 5-11). Add a build rule (after the `clock.o` rule at line 16-17):

```make
crashsave_schedule.o : ../crashsave_schedule.cpp ../crashsave_schedule.h
	$(CXX) -c $(CXXFLAGS) ../crashsave_schedule.cpp
```

Add the test to `SRCS` (line 171-172) — append `crashsave_schedule_tests.cpp`:

```make
SRCS = CharPlayerDataBuilder.h CharPlayerDataBuilder.cpp ObjFlagDataBuilder.h ObjFlagDataBuilder.cpp \
 	   obj_flag_data_tests.cpp player_finalize_tests.cpp crashsave_schedule_tests.cpp gtest_main.cpp
```

- [ ] **Step 6: Build + run tests.**

```bash
docker compose run --rm rots bash -c "cd /rots/src/tests && make all 2>&1 | tail -6 && ../../bin/tests 2>&1 | tail -12"
```
Expect: `[  PASSED  ] 13 tests.` (8 existing + 5 new). Also build the game (Step verifies the new `.o` joins the game link):
```bash
docker compose run --rm rots bash -c "cd /rots/src && make all 2>&1 | tail -4"
```
Expect: links `../bin/ageland`, no errors.

- [ ] **Step 7: Commit.**

```bash
git add src/crashsave_schedule.h src/crashsave_schedule.cpp src/tests/crashsave_schedule_tests.cpp src/Makefile src/tests/Makefile
git commit -m "Add crash-save scheduling helper (seconds->pulses + per-pulse timer) with gtests"
```

---

### Task 2: Configurable sub-minute cadence in the game loop

**Files:**
- Modify: `src/config.cpp:40` (CRLF)
- Modify: `src/comm.cpp:99` (extern, context only), `src/comm.cpp:479` (local), `src/comm.cpp:842-848` (gate) (LF)
- Modify: `src/Makefile` (add header to `comm.o` deps)

**Interfaces:**
- Consumes: `autosave_interval_pulses`, `AutosaveTimer` from Task 1; `autosave_time` (now seconds); `TICS_PER_SECOND` (`structs.h:96`, value 4).

- [ ] **Step 1: Repurpose `autosave_time` to seconds** in `src/config.cpp` (CRLF — preserve!). Change line 40 from:

```cpp
int autosave_time = 4;
```
to:
```cpp
int autosave_time = 30; /* periodic crash-save (snapshot) interval, in SECONDS */
```
(Leave the existing comment block above it; this inline comment marks the units change. Default 30s per spec.)

- [ ] **Step 2: Add the include** to `src/comm.cpp` near its other project includes (top of file, alongside `#include "handler.h"` etc.):

```cpp
#include "crashsave_schedule.h"
```

- [ ] **Step 3: Replace the `mins_since_crashsave` local.** `src/comm.cpp:479` currently:

```cpp
    int mins_since_crashsave = 0, mask;
```
Change to:
```cpp
    int mask;
    // Accumulates heartbeat pulses and fires the periodic crash-save snapshot
    // once per configured interval (autosave_time seconds). Persists across the
    // game-loop iterations below.
    AutosaveTimer autosave_timer;
```

- [ ] **Step 4: Replace the gate.** `src/comm.cpp:842-848` currently:

```cpp
        if (!(pulse % (60 * 4))) /* one minute */
        {
            if (++mins_since_crashsave >= autosave_time) {
                mins_since_crashsave = 0;
                Crash_save_all();
            }
        }
```
Replace with (runs every pulse; the timer fires on the configured boundary):
```cpp
        // Periodic point-in-time snapshot of all connected players. Fires every
        // autosave_time seconds (clamped to >= 1s); see crashsave_schedule.h.
        if (autosave_timer.tick(autosave_interval_pulses(autosave_time, TICS_PER_SECOND))) {
            Crash_save_all();
        }
```

- [ ] **Step 5: Add the header to `comm.o`'s dependency line** in `src/Makefile` (lines 48-49) so `comm.o` rebuilds when the header changes — append `crashsave_schedule.h`:

```make
comm.o : comm.cpp structs.h utils.h comm.h interpre.h handler.h db.h \
	limits.h clock.h protocol.h crashsave_schedule.h
	$(CC) -c $(CFLAGS) $(COMMFLAGS) comm.cpp
```

- [ ] **Step 6: Build.**

```bash
docker compose run --rm rots bash -c "cd /rots/src && make all 2>&1 | tail -6"
```
Expect: clean link, no errors. (Cadence math is unit-tested in Task 1; the wiring is verified by clean compile + the runtime watcher during final validation.) Confirm CRLF preserved: `grep -c $'\r' src/config.cpp` (expect non-zero, unchanged ~120).

- [ ] **Step 7: Commit.**

```bash
git add src/config.cpp src/comm.cpp src/Makefile
git commit -m "Drive crash-save cadence from the configurable seconds interval (default 30s)"
```

---

### Task 3: Save-all snapshot in `Crash_save_all`

**Files:**
- Modify: `src/objsave.cpp:1687-1702` (CRLF — preserve!)

**Interfaces:**
- Consumes: existing `Crash_crashsave`, `save_char`. No new symbols.

- [ ] **Step 1: Rewrite the loop body.** `src/objsave.cpp:1687-1702` currently:

```cpp
void Crash_save_all(void)
{
    struct descriptor_data* d;
    for (d = descriptor_list; d; d = d->next) {
        if ((d->connected == CON_PLYNG) && !IS_NPC(d->character)) {
            if (PLR_FLAGGED(d->character, PLR_CRASH)) {
                Crash_crashsave(d->character);
                if (GET_LEVEL(d->character) < LEVEL_IMMORT)
                    save_char(d->character, NOWHERE, 1);
                else
                    save_char(d->character, NOWHERE, 0);
                REMOVE_BIT(PLR_FLAGS(d->character), PLR_CRASH);
            }
        }
    }
}
```
Replace with:
```cpp
void Crash_save_all(void)
{
    struct descriptor_data* d;
    // Point-in-time snapshot: save EVERY connected player each cycle (not just
    // PLR_CRASH-dirty ones), so PvP/smob participants are recovered at the same
    // moment regardless of who looted or gained XP last interval. Single-threaded,
    // so this one pass is an atomic capture. notify=0: silent background save (no
    // "Saving X." spam at the 30s cadence). Saves are per-player and independent --
    // a failure inside save_char/Crash_crashsave logs and leaves that one player to
    // re-converge next cycle (Crash_crashsave clears PLR_CRASH only on success); it
    // never aborts the whole batch.
    for (d = descriptor_list; d; d = d->next) {
        if ((d->connected == CON_PLYNG) && !IS_NPC(d->character)) {
            Crash_crashsave(d->character);
            save_char(d->character, NOWHERE, 0);
        }
    }
}
```

- [ ] **Step 2: Build + run the existing suite (no regression).**

```bash
docker compose run --rm rots bash -c "cd /rots/src && make all 2>&1 | tail -4 && cd /rots/src/tests && make all 2>&1 | tail -4 && ../../bin/tests 2>&1 | tail -8"
```
Expect: clean link; `[  PASSED  ] 13 tests.` Confirm CRLF: `grep -c $'\r' src/objsave.cpp` (expect ~1728, unchanged).

> Behavioral verification (every player saved each cycle, no notify spam, no crash) is an integration check done on the dev server in **Final Validation** — it cannot be unit-tested (needs `descriptor_list`/`char_data`).

- [ ] **Step 3: Commit.**

```bash
git add src/objsave.cpp
git commit -m "Crash_save_all: snapshot all connected players each cycle (silent, no dirty gate)"
```

---

### Task 4: Remove the desyncing 10% kill-XP save

**Files:**
- Modify: `src/fight.cpp:1304-1307` (LF)

- [ ] **Step 1: Delete the block.** `src/fight.cpp:1304-1307` currently:

```cpp
        /* save only 10% of the time to avoid lag in big groups */
        if (number(0, 9) == 0) {
            save_char(character, NOWHERE, 1);
        }
```
Delete all four lines. (The surrounding `for (auto character : ...)` loop and its closing braces at 1308-1309 remain.) The next ≤30s snapshot persists the XP for everyone; no replacement is added.

- [ ] **Step 2: Build.**

```bash
docker compose run --rm rots bash -c "cd /rots/src && make all 2>&1 | tail -4"
```
Expect: clean link, no errors.

- [ ] **Step 3: Commit.**

```bash
git add src/fight.cpp
git commit -m "Remove the 10% kill-XP save (snapshot now captures XP consistently)"
```

---

### Task 5: Save the character on every exploit record

**Files:**
- Modify: `src/db.cpp:3682-3687` (LF)

**Interfaces:**
- Consumes: `save_char` (declared `db.h:103`), `NOWHERE`. `write_exploits` is the sole sink for all `add_exploit_record` branches (PK trophy → killer; death/level/stat/birth/etc. → the char), so this one hook covers them all.

- [ ] **Step 1: Add the save before the final return.** `src/db.cpp:3682-3687` currently:

```cpp
  // Atomically move the fully-written temp into place (no shell-out; crash-safe).
  if (!finalize_save_file(tempfname, playerfname)) {
    mudlog("**ERROR: Could not move temp exploit file into place.", NRM, LEVEL_IMMORT, TRUE);
  }
  return;
}
```
Change to:
```cpp
  // Atomically move the fully-written temp into place (no shell-out; crash-safe).
  if (!finalize_save_file(tempfname, playerfname)) {
    mudlog("**ERROR: Could not move temp exploit file into place.", NRM, LEVEL_IMMORT, TRUE);
  }
  // An exploit record means an impactful, crash-sensitive event happened to this
  // character (PK trophy, death, level-up, stat gain, birth, achievement...).
  // Persist the character immediately so it cannot be undone by deliberately
  // crashing the game before the next periodic snapshot. ch is the character the
  // record belongs to (the killer for EXPLOIT_PK), and add_exploit_record already
  // gated out NPCs/immortals; save_char additionally guards IS_NPC/!ch->desc.
  save_char(ch, NOWHERE, 0);
  return;
}
```
(Note: the rare early `return;` at db.cpp:3607 — temp file could not be opened — skips this save; acceptable, the next snapshot covers it within the cadence.)

- [ ] **Step 2: Build + run the existing suite (no regression).**

```bash
docker compose run --rm rots bash -c "cd /rots/src && make all 2>&1 | tail -4 && cd /rots/src/tests && make all 2>&1 | tail -4 && ../../bin/tests 2>&1 | tail -8"
```
Expect: clean link; `[  PASSED  ] 13 tests.`

- [ ] **Step 3: Commit.**

```bash
git add src/db.cpp
git commit -m "Save the character whenever an exploit record is written (anti-rollback)"
```

---

### Task 6: Crash-proof the angel reroll

**Files:**
- Modify: `src/spec_pro.cpp:~2691` (CRLF — preserve!)

**Interfaces:**
- Consumes: `save_char`, `NOWHERE` (`spec_pro.cpp` includes `db.h` per its Makefile deps).

- [ ] **Step 1: Add the immediate save** after the reroll increment. In the angel-reroll branch, the lines currently read:

```cpp
            roll_abilities(ch, 80, 93);
            ch->specials2.rerolls += 1;
            reroll_count = 41 - ch->specials2.rerolls;
```
Insert a `save_char` directly after the increment:
```cpp
            roll_abilities(ch, 80, 93);
            ch->specials2.rerolls += 1;
            // Persist the reroll immediately so a player cannot crash the game to
            // undo an unlucky roll and refund a capped reroll attempt. No exploit
            // record is written here -- rerolls are too frequent to log (per design).
            save_char(ch, NOWHERE, 0);
            reroll_count = 41 - ch->specials2.rerolls;
```

- [ ] **Step 2: Build.**

```bash
docker compose run --rm rots bash -c "cd /rots/src && make all 2>&1 | tail -4"
```
Expect: clean link, no errors. Confirm CRLF: `grep -c $'\r' src/spec_pro.cpp` (expect ~3556, unchanged).

- [ ] **Step 3: Commit.**

```bash
git add src/spec_pro.cpp
git commit -m "Save immediately on angel reroll (close crash-to-reroll exploit)"
```

---

### Final Validation (integration — performed on the dev server)

The behavioral changes (Tasks 2, 3, 5, 6) are coupled to live game state and cannot be unit-tested; validate them on the `dev-coding4810` instance as with the prior save work:

- [ ] Build the branch, deploy `bin/ageland` to `/rots/dev-coding4810/bin/` (backup first), `shutdown reboot`, and confirm a clean boot via the syslog watcher.
- [ ] Confirm **no "Saving X." spam** appears at the 30s cadence (Task 3 notify=0).
- [ ] Log in two characters; move one, leave the other idle; after one cadence, verify **both** player files' mtimes advanced (save-all includes the non-acting player) — i.e. location consistency is captured.
- [ ] Trigger an exploit event (level-up or a kill that records one) and confirm the character file is saved immediately (Task 5).
- [ ] Watch the syslog for `could not finalize` / crash markers throughout (the existing runtime watcher pattern).

---

## Self-Review

**Spec coverage:**
- Save-all snapshot every 30s configurable → Tasks 1, 2, 3. ✓
- Skip-and-log on per-player failure → Task 3 (inherent in the per-player loop + the existing internal failure logs in `save_char`/`Crash_crashsave`; no whole-batch abort). ✓
- Delete the 10% kill-XP save (no dirty-bit replacement) → Task 4. ✓
- Exploit→`save_char` hook → Task 5. ✓
- Angel-reroll direct save, no exploit record → Task 6. ✓
- `Emergency_save` / `raw_kill` / `advance_level` unchanged → not touched (explicit in File Structure). ✓
- Cadence is a boot-read config, no live command → Task 2 (`autosave_time` seconds). ✓
- Documented v1 gaps (link-dead, one-cycle rename-split) → carried in the spec; no task needed (accepted residuals). ✓

**Placeholder scan:** No TBD/TODO; every code step shows complete before/after; build/test commands are exact. ✓

**Type consistency:** `autosave_interval_pulses(int,int)` and `AutosaveTimer::tick(int)` defined in Task 1 are used with matching signatures in Task 2. `save_char(ch, NOWHERE, 0)` matches the existing `save_char(struct char_data*, int, int)` signature used elsewhere. ✓

**Testability note (honest):** Only Task 1 is unit-testable (pure logic). Tasks 2-6 are global-state-coupled; their gate is **clean Docker compile + no regression in the existing suite + the dev-server Final Validation**. This is a property of the legacy codebase (no DI/mocking harness), acknowledged in the spec — fake unit tests are intentionally not written.
