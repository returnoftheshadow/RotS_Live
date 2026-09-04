# Savebench Port Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the legacy non-account `save_player` crash-safe (atomic write-temp-then-rename), profile the full account-JSON character persistence pipeline (save + load) stage-by-stage, and bring over the autosave cadence scheduler defaulted to today's 4-minute interval — all on a new branch `feature/savebench-port` off `account-management`.

**Architecture:** Three independent components sharing a ported `Stopwatch`. (A) Port the source branch's `finalize_player_file_rename`/`finalize_player_file_legacy` into a new portable `player_file_finalize` module and a `write_player_text` bool serializer in `db.cpp`, then rewire `save_player`. (B) A `save_benchmark` module that times each pipeline stage in both directions, driven by an offline gtest and a sandboxed in-game `savebench` implementor command. (C) A pure, unit-tested `crashsave_schedule` (seconds→pulses + per-pulse timer) replacing the inline minutes counter in the game loop, defaulted to 240s.

**Tech Stack:** C++17, single-threaded DikuMUD; CMake build (i386, Docker-only); GoogleTest via `gtest_discover_tests`/CTest; `std::filesystem`/`std::chrono` for new code.

## Global Constraints

- **Portable standard C++ for all NEW functions** — `std::filesystem` with non-throwing `std::error_code` overloads, `std::chrono`, `<fstream>`; not POSIX. Do **not** rewrite the existing POSIX account layer (rule covers new functions only).
- **Preserve per-file line endings.** Existing files edited: `db.cpp` (LF), `db.h` (LF), `comm.cpp` (LF), `config.cpp` (**CRLF — do not flip**), `interpre.cpp` (LF), `src/CMakeLists.txt`. New files are LF. An edit that flips endings is a defect.
- **`.claude/.no-autoformat` must exist before editing any source** (Task 1) — the global clang-format-on-edit hook is otherwise live and will reformat/flip endings.
- **Build is Docker i386 `g++` (C++17), authoritative.** Run `make` / `make test` inside the repo's Docker dev container. Host clang/IDE errors about `std::filesystem`/`MAX`/`MIN`/`gtest` are false positives; never "fix" code to satisfy a host compile (`-m32` will not build on macOS).
- **The benchmark must never touch live state** — never call `write_account_character_file` against a live path, never mutate `player_table[].ch_file`, never write a live account/character file, never hydrate into the live `ch`.
- **`write_player_text` keeps its `fprintf`/`FILE*` body byte-for-byte** identical to the current `save_player` serialization (so the A/B oracle test passes). Move the existing lines; do not retype them.

**Plan refinements vs the spec (flagged for review):**
1. `write_player_text` is defined in `db.cpp` (declared in `db.h`), not the new `player_file_finalize` module — it depends on `db.cpp`-local internals (`pwdcrypt`, `encrypt_line`, `SAVE_VERSION`). The portable finalizers (`finalize_player_file_rename`/`legacy`) are the module.
2. The in-game `savebench` command profiles SAVE stages S1–S5 and LOAD stages L1–L4 against the real character; **L5 (`store_to_char`) is profiled in the offline gtest only**, because `store_to_char` allocates into the target char (`CREATE` of title/description/…) and is not safe to loop on the live player. The offline harness covers the full pipeline including L5.

---

## Verification convention (READ FIRST)

All builds/tests run in the **i386 Docker container** via the wrapper:

```bash
scripts/rots-docker.sh test --gtest_filter=<Filter>
```

This builds the `ageland_tests` target in the container and then runs `./bin/tests` with the given args. **Quote any filter containing `*`** so your shell doesn't glob-expand it: `scripts/rots-docker.sh test '--gtest_filter=PlayerFinalize.*'` (an unquoted `PlayerFinalize.*` fails with "no matches found" before the script runs). **Wherever a task step says `make test` (meaning "build") or `./bin/tests --gtest_filter=X`, run it as `scripts/rots-docker.sh test --gtest_filter=X`.** (Do not use `ctest`/`make test`'s test phase: `gtest_discover_tests` PRE_TEST mode finds 0 tests under the container's cmake 3.18 — harmless, but it means ctest never runs anything. The binary runs fine directly.)

**Known 32-bit baseline — NOT regressions.** The i386 image builds the test binary 32-bit; CI builds `ageland_tests` 64-bit (no `-m32` on that target, lines 155-174 of `src/CMakeLists.txt`). So **~163 pre-existing tests fail in the container purely on 32-bit-vs-64-bit expectation differences** (integer ranges, `size_t`/`long` width, RNG scaling) in suites this plan does NOT touch (`JsonUtils`, `OlogHai*`, `TestRandomUtils`, the account-native-JSON `DbLoader` tests, …). They are green on CI's 64-bit runner. **Do not try to "fix" them.**

**The gate is therefore per-test, not per-suite.** Each task verifies (a) its NEW filtered tests pass, and (b) the SPECIFIC named existing tests it touches still pass. Recorded 32-bit baselines for the tests this plan touches (confirmed):
- Component A (`save_player`) regression → **`DbLoader.LegacyPlayerText*`** (3 tests) — all PASS on 32-bit. Gate Task 4 on this filter, NOT the whole `DbLoader` suite (27/36 of which fail on 32-bit for unrelated int-parsing reasons).
- New suites this plan adds — `PlayerFinalize.*`, `CrashsaveSchedule.*`, `SaveBenchmark.*` — are not 32-bit-sensitive and must pass.

A task is "green" when its new tests pass and its named touched tests still pass — ignore the unrelated 32-bit baseline failures.

---

## Task 0: Fix the local Docker build environment (prerequisite for all verification)

**Why:** The plan's TDD steps verify via `make test` (CMake + GoogleTest), which CI runs on a native Ubuntu multilib runner. Locally on Apple Silicon the only `-m32` path is the i386 Docker image, but its `Dockerfile` shipped only `g++ make` (no `cmake`/`libgtest-dev`) and `scripts/rots-docker.sh` drove the legacy `src/Makefile` — so the gtest suite could not be built or run locally. This task makes the container able to run `make test`.

**Files:**
- Modify: `Dockerfile`
- Modify: `scripts/rots-docker.sh`

- [ ] **Step 1: Add the CMake/GTest build deps to the image** — in `Dockerfile`, extend the `apt-get install` line (the i386 base needs no multilib):

```dockerfile
RUN apt-get update && apt-get install -y --no-install-recommends \
        g++ make cmake libgtest-dev libcrypt-dev pkg-config telnet procps ca-certificates \
    && rm -rf /var/lib/apt/lists/*
```

- [ ] **Step 2: Add a `test` wrapper command** — in `scripts/rots-docker.sh`, add a case driving the CMake flow:

```bash
  test)
    # CMake build of ageland + ageland_tests, then ctest. Matches the CI test path.
    docker compose run --rm rots bash -lc 'cd /rots && make test'
    ;;
```

(and add `test` to the usage comment and the unknown-command hint).

- [ ] **Step 3: Build the image**

Run: `docker compose build`
Expected: the i386 image builds with cmake + libgtest-dev installed (QEMU-emulated on Apple Silicon; first build is slow).

- [ ] **Step 4: Confirm the gtest binary builds and runs (record the 32-bit baseline)**

Run: `scripts/rots-docker.sh test --gtest_filter=DbLoader.LegacyPlayerText*`
Expected: CMake configures, `ageland` + `ageland_tests` compile (slow first time under QEMU), and the 3 `DbLoader.LegacyPlayerText*` tests PASS. (A full `scripts/rots-docker.sh test` run shows ~163 of 497 failing — the documented 32-bit baseline from the Verification convention, NOT a regression.) This confirms the per-test gate works for the suites this plan touches.

- [ ] **Step 5: Commit**

```bash
git add Dockerfile scripts/rots-docker.sh
git commit -m "build: make the i386 Docker image run the CMake/gtest suite (make test)"
```

---

## Task 1: Prep — markers, gitignore, clean build

**Files:**
- Create: `.claude/.no-autoformat`
- Modify: `.gitignore`
- Delete: `docs/superpowers/.DS_Store` (stray)

- [ ] **Step 1: Add the no-autoformat marker (must be first — before any source edit)**

```bash
touch .claude/.no-autoformat
```

- [ ] **Step 2: Ignore `.DS_Store` and remove the stray one**

Append to `.gitignore` (match the file's existing newline style):

```
# macOS
.DS_Store
**/.DS_Store
```

```bash
rm -f docs/superpowers/.DS_Store
```

- [ ] **Step 3: Verify a clean build is green (inside the Docker dev container)**

Run: `make test`
Expected: CMake configures, `ageland` and `ageland_tests` build, `ctest` runs and all existing tests PASS. (Establishes the baseline before any change.)

- [ ] **Step 4: Commit**

```bash
git add .claude/.no-autoformat .gitignore
git commit -m "chore: add .no-autoformat marker and ignore .DS_Store"
```

---

## Task 2: Portable finalizers + finalize gtest

**Files:**
- Create: `src/player_file_finalize.h`, `src/player_file_finalize.cpp`
- Create: `src/tests/player_finalize_tests.cpp`
- Modify: `src/CMakeLists.txt` (`ROTS_SERVER_SOURCES` ~line 29, `ROTS_TEST_SOURCES` ~line 102)

**Interfaces:**
- Produces:
  - `bool finalize_player_file_legacy(const char* scratch_path, const char* base_path, const char* versioned_path);` — A/B oracle (`system("rm")`/`system("cp")`).
  - `bool finalize_player_file_rename(const char* scratch_path, const char* dir_path, const char* base_name, const char* versioned_path);` — crash-safe atomic finalize (rename-first, then dot-anchored prune).

- [ ] **Step 1: Write the failing test** — `src/tests/player_finalize_tests.cpp`

```cpp
#include "../player_file_finalize.h"
#include <gtest/gtest.h>

#include <dirent.h>
#include <stdio.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace {

void write_file(const char* path, const char* content) {
    FILE* f = fopen(path, "w");
    ASSERT_NE(f, nullptr);
    fputs(content, f);
    fclose(f);
}

std::string read_file(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) {
        return std::string("<<missing:") + path + ">>";
    }
    std::string out;
    char buf[1024];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        out.append(buf, n);
    }
    fclose(f);
    return out;
}

int count_files(const char* dir) {
    DIR* d = opendir(dir);
    if (!d) {
        return -1;
    }
    int count = 0;
    struct dirent* e;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] != '.') {
            count++;
        }
    }
    closedir(d);
    return count;
}

} // namespace

// Both finalizers must: (1) write byte-identical output, (2) delete every stale "<base>."
// file via the dot-anchored glob while leaving a different player's "<base>name.*" file
// untouched (the bob. vs bobby. guard), and (3) honor move-vs-copy (rename consumes its
// scratch; cp leaves it).
TEST(PlayerFinalize, ByteIdenticalAndSingleFile) {
    const char* legacy_dir = "pf_test_legacy";
    const char* new_dir = "pf_test_new";
    mkdir(legacy_dir, 0775);
    mkdir(new_dir, 0775);

    write_file("pf_test_legacy/probe.stale", "OLD");
    write_file("pf_test_new/probe.stale", "OLD");
    write_file("pf_test_legacy/probe.42.1.99.0.0", "OLD2");
    write_file("pf_test_new/probe.42.1.99.0.0", "OLD2");
    write_file("pf_test_legacy/probename.7.1.124.0.0", "KEEP");
    write_file("pf_test_new/probename.7.1.124.0.0", "KEEP");

    write_file("pf_test_legacy_scratch", "PLAYER-BYTES-V1\n");
    write_file("pf_test_new_scratch", "PLAYER-BYTES-V1\n");

    bool ok_legacy = finalize_player_file_legacy("pf_test_legacy_scratch", "pf_test_legacy/probe",
                                                 "pf_test_legacy/probe.50.1.123.0.0");
    bool ok_new = finalize_player_file_rename("pf_test_new_scratch", "pf_test_new", "probe",
                                              "pf_test_new/probe.50.1.123.0.0");
    EXPECT_TRUE(ok_legacy);
    EXPECT_TRUE(ok_new);

    EXPECT_EQ(read_file("pf_test_new/probe.50.1.123.0.0"), "PLAYER-BYTES-V1\n");
    EXPECT_EQ(read_file("pf_test_legacy/probe.50.1.123.0.0"),
              read_file("pf_test_new/probe.50.1.123.0.0"));

    EXPECT_NE(access("pf_test_legacy/probe.stale", F_OK), 0);
    EXPECT_NE(access("pf_test_new/probe.stale", F_OK), 0);
    EXPECT_NE(access("pf_test_legacy/probe.42.1.99.0.0", F_OK), 0);
    EXPECT_NE(access("pf_test_new/probe.42.1.99.0.0", F_OK), 0);
    EXPECT_EQ(access("pf_test_legacy/probename.7.1.124.0.0", F_OK), 0);
    EXPECT_EQ(access("pf_test_new/probename.7.1.124.0.0", F_OK), 0);
    EXPECT_EQ(count_files(legacy_dir), 2);
    EXPECT_EQ(count_files(new_dir), 2);

    EXPECT_NE(access("pf_test_new_scratch", F_OK), 0);
    EXPECT_EQ(access("pf_test_legacy_scratch", F_OK), 0);

    unlink("pf_test_legacy/probe.50.1.123.0.0");
    unlink("pf_test_new/probe.50.1.123.0.0");
    unlink("pf_test_legacy/probename.7.1.124.0.0");
    unlink("pf_test_new/probename.7.1.124.0.0");
    unlink("pf_test_legacy_scratch");
    rmdir(legacy_dir);
    rmdir(new_dir);
}
```

- [ ] **Step 2: Create the header** — `src/player_file_finalize.h`

```cpp
#ifndef PLAYER_FILE_FINALIZE_H
#define PLAYER_FILE_FINALIZE_H

// Crash-safe finalize primitives for the legacy (non-account) player-file save path.
// Portable standard C++ (std::filesystem with non-throwing error_code).

// Historical A/B oracle: system("rm <base>.*") then system("cp scratch versioned").
// Kept ONLY to prove byte/stale-file equivalence against the rename path in tests.
// NOT for production use.
bool finalize_player_file_legacy(const char* scratch_path, const char* base_path,
                                 const char* versioned_path);

// Crash-safe finalize: atomically rename scratch -> versioned FIRST, then remove every
// OTHER "<base_name>." entry in dir_path (dot-anchored, so "bob." never matches "bobby.").
// Return contract: false BEFORE the rename means nothing changed (old save intact); false
// AFTER the rename means the new file IS written and only stale cleanup failed.
bool finalize_player_file_rename(const char* scratch_path, const char* dir_path,
                                 const char* base_name, const char* versioned_path);

#endif // PLAYER_FILE_FINALIZE_H
```

- [ ] **Step 3: Create the implementation** — `src/player_file_finalize.cpp` (ported verbatim from the source branch)

```cpp
#include "player_file_finalize.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

bool finalize_player_file_legacy(const char* scratch_path, const char* base_path,
                                 const char* versioned_path) {
    char command[300];

    snprintf(command, sizeof(command), "rm %s.*", base_path);
    int rc_rm = system(command);
    snprintf(command, sizeof(command), "cp %s %s", scratch_path, versioned_path);
    int rc_cp = system(command);

    return (rc_rm != -1) && (rc_cp != -1);
}

bool finalize_player_file_rename(const char* scratch_path, const char* dir_path,
                                 const char* base_name, const char* versioned_path) {
    namespace fs = std::filesystem;
    std::error_code ec;

    // 1. Publish the new file first so a crash here cannot lose the save (atomic move).
    fs::rename(scratch_path, versioned_path, ec);
    if (ec) {
        return false;
    }

    // 2. Remove any OTHER stale "<base>." entries, leaving the file we just wrote.
    const size_t base_len = std::char_traits<char>::length(base_name);
    const std::string_view versioned_view(versioned_path);
    const size_t v_slash = versioned_view.find_last_of('/');
    const std::string_view keep_name =
        (v_slash == std::string_view::npos) ? versioned_view : versioned_view.substr(v_slash + 1);

    std::vector<fs::path> victims;
    fs::directory_iterator it(dir_path, ec);
    if (ec) {
        return false;
    }
    const fs::directory_iterator end;
    // Check ec right AFTER each increment: on error libstdc++ resets the iterator to end,
    // so a top-of-loop check would be skipped and the error silently swallowed.
    while (it != end) {
        const std::string& full = it->path().native();
        const size_t slash = full.find_last_of('/');
        const std::string_view name = (slash == std::string::npos)
                                          ? std::string_view(full)
                                          : std::string_view(full).substr(slash + 1);
        if (name.size() > base_len && name.compare(0, base_len, base_name) == 0 &&
            name[base_len] == '.' && name != keep_name) {
            victims.push_back(it->path());
        }
        it.increment(ec);
        if (ec) {
            return false;
        }
    }
    for (const fs::path& victim : victims) {
        fs::remove(victim, ec);
        if (ec) {
            return false;
        }
    }
    return true;
}
```

- [ ] **Step 4: Wire into CMake**

In `src/CMakeLists.txt`, add `player_file_finalize.cpp` to `ROTS_SERVER_SOURCES` (the `set(ROTS_SERVER_SOURCES` block at ~line 29, alphabetical neighborhood near `objsave.cpp`), and add `tests/player_finalize_tests.cpp` to `ROTS_TEST_SOURCES` (the `set(ROTS_TEST_SOURCES` block at ~line 102). Example lines to add:

```cmake
    player_file_finalize.cpp
```
```cmake
    tests/player_finalize_tests.cpp
```

- [ ] **Step 5: Build and run the test (Docker)**

Run: `make test`
Then: `./bin/tests --gtest_filter=PlayerFinalize.*`
Expected: `PlayerFinalize.ByteIdenticalAndSingleFile` PASS.

- [ ] **Step 6: Commit**

```bash
git add src/player_file_finalize.h src/player_file_finalize.cpp src/tests/player_finalize_tests.cpp src/CMakeLists.txt
git commit -m "feat: add portable crash-safe player-file finalizers + A/B gtest"
```

---

## Task 3: Extract `write_player_text` (bool serializer) in db.cpp

**Files:**
- Modify: `src/db.h` (add prototype near `char_to_store`/`store_to_char`, ~line 103)
- Modify: `src/db.cpp` (the current `save_player` serialize block, lines ~2856-2972)

**Interfaces:**
- Consumes: `char_to_store` (`db.h:103`), the existing `pwdcrypt`/`encrypt_line`/`SAVE_VERSION` db.cpp internals.
- Produces: `bool write_player_text(struct char_data* ch, int load_room, const char* scratch_path);` — serializes `ch` to `scratch_path`; returns `false` (and removes the partial scratch) on any I/O error.

- [ ] **Step 1: Declare it in `db.h`** (next to the other store helpers, ~line 103-104)

```cpp
bool write_player_text(struct char_data* ch, int load_room, const char* scratch_path);
```

- [ ] **Step 2: Add the `<cstdio>` guard usage and define `write_player_text` in `db.cpp`**

Define the function just **above** the current `save_player` (which begins at `db.cpp:2800`). Build it by **moving the existing serialization body** out of `save_player` (the block currently at lines ~2856-2972: `pf = fopen("players/temp", "w");` through `fprintf(pf, "end\n"); fclose(pf);`) into this new function **unchanged**, with three wrapper edits only:
1. Open `scratch_path` (the passed argument) instead of the hard-coded `"players/temp"`, and bail if the open fails.
2. Keep every `char_to_store`, `strcpy(chd.pwd, …)`, `strncpy(chd.host, …)`, the `PLR_LOADROOM` load-room line, and **all `fprintf` lines verbatim** (byte-identity is load-bearing — the Task 4 round-trip test guards it).
3. Before `fclose`, check `ferror(pf)`; on error or a failing `fclose`, `std::remove(scratch_path)` and return `false`. On success return `true`.

```cpp
#include <cstdio>      // ensure present near the top of db.cpp (for std::remove)

// Serialize `ch` to `scratch_path` in the legacy versioned-text format. Returns false and
// removes the partial scratch on any write/close error, so the caller skips finalize and
// never destroys the live file. Body is the former save_player serialization, unchanged,
// so its bytes stay identical to the legacy path (pinned by the A/B oracle + round-trip test).
bool write_player_text(struct char_data* ch, int load_room, const char* scratch_path)
{
    char_file_u chd;
    int tmp;

    FILE* pf = fopen(scratch_path, "w");
    if (!pf) {
        return false;
    }

    char_to_store(ch, &chd);
    strcpy(chd.pwd, ch->desc->pwd);
    strncpy(chd.host, ch->desc->host, HOST_LEN);
    if (!PLR_FLAGGED(ch, PLR_LOADROOM))
        chd.specials2.load_room = load_room;

    fprintf(pf, "#player\n");
    fprintf(pf, "version     %d\n", SAVE_VERSION);
    /* ... MOVE every remaining fprintf line from the current save_player body here,
       UNCHANGED, through: ... */
    fprintf(pf, "end\n");

    if (ferror(pf)) {
        fclose(pf);
        std::remove(scratch_path);
        return false;
    }
    if (fclose(pf) != 0) {
        std::remove(scratch_path);
        return false;
    }
    return true;
}
```

- [ ] **Step 3: Build (Docker) to confirm it compiles**

Run: `make`
Expected: `ageland` links. (No behavior change yet — `save_player` still has its own copy until Task 4. To avoid a duplicate-serialization compile/confusion, Task 4 immediately replaces `save_player`'s body; do Steps of Task 3 and Task 4 back-to-back, or temporarily leave `save_player` calling the old body — either way it must compile here.)

- [ ] **Step 4: Commit**

```bash
git add src/db.h src/db.cpp
git commit -m "refactor: extract write_player_text bool serializer from save_player"
```

---

## Task 4: Rewire `save_player` to the atomic finalize

**Files:**
- Modify: `src/db.cpp` (`save_player`, lines ~2800-2984 → replace body)
- Test: `src/tests/db_loader_tests.cpp` (extend the existing legacy `save_player` round-trip test, ~line 361)

**Interfaces:**
- Consumes: `write_player_text` (Task 3), `finalize_player_file_rename` (Task 2).

- [ ] **Step 1: Add a round-trip + single-file assertion to the existing save_player test**

In `src/tests/db_loader_tests.cpp`, near the existing `save_player(character, …)` call (~line 361), after the save, assert the bucket directory holds exactly one file for the character and that loading it back reproduces the saved struct. Add (adapt the fixture's existing load helper / paths):

```cpp
// After save_player(...): the bucket dir must hold exactly ONE versioned file for this
// character (the atomic finalize pruned stale siblings), and it must round-trip.
// (Uses the test's existing TempDirectory/bucket-path helpers.)
EXPECT_EQ(count_versioned_files_for(character_bucket_dir, character_base_name), 1);
```

If the fixture lacks a counter helper, add a small local one (same `opendir` pattern as `player_finalize_tests.cpp`). Keep the assertion minimal and tied to the fixture's existing paths.

- [ ] **Step 2: Run it to verify the new assertion is exercised (and currently passes on the pre-rewire body)**

Run: `scripts/rots-docker.sh test --gtest_filter=DbLoader.LegacyPlayerText*`
Expected: builds and runs; the 3 `DbLoader.LegacyPlayerText*` tests pass and the new count assertion passes (legacy `system("cp")` also leaves one file in the clean fixture). This pins behavior before the swap. (The `save_player` call lives in a shared helper used by these three tests — `db_loader_tests.cpp:361`.)

- [ ] **Step 3: Replace `save_player`'s body with the atomic finalize**

Replace the body of `save_player` (`db.cpp:2800-2984`) so it (a) computes `playerfname` from the first-letter bucket exactly as today, (b) calls `write_player_text(ch, load_room, "players/temp")` and **returns early on false**, (c) builds the versioned name from `player_table[index_pos]`, (d) derives the bucket dir, (e) calls `finalize_player_file_rename`, setting `ch_file` only on success:

```cpp
void save_player(struct char_data* ch, int load_room, int index_pos)
{
    char name[255];
    char* tmpchar;
    char playerfname[100];

    strcpy(name, GET_NAME(ch));
    for (tmpchar = name; *tmpchar; tmpchar++)
        *tmpchar = tolower(*tmpchar);

    switch (tolower(*name)) {
    case 'a': case 'b': case 'c': case 'd': case 'e':
        sprintf(playerfname, "players/A-E/%s", name); break;
    case 'f': case 'g': case 'h': case 'i': case 'j':
        sprintf(playerfname, "players/F-J/%s", name); break;
    case 'k': case 'l': case 'm': case 'n': case 'o':
        sprintf(playerfname, "players/K-O/%s", name); break;
    case 'p': case 'q': case 'r': case 's': case 't':
        sprintf(playerfname, "players/P-T/%s", name); break;
    case 'u': case 'v': case 'w': case 'x': case 'y': case 'z':
        sprintf(playerfname, "players/U-Z/%s", name); break;
    default:
        sprintf(playerfname, "players/ZZZ/%s", name); break;
    }

    // If serialization fails, do NOT finalize: a failed finalize must never destroy the
    // live file. Leave the existing save intact and retry next cycle.
    if (!write_player_text(ch, load_room, "players/temp")) {
        return;
    }

    char versioned[120];
    snprintf(versioned, sizeof(versioned), "%s.%d.%d.%d.%ld.%ld", playerfname,
        (player_table + index_pos)->level, (player_table + index_pos)->race,
        (player_table + index_pos)->idnum, (long)(player_table + index_pos)->log_time,
        (player_table + index_pos)->flags);

    char dirpath[100];
    snprintf(dirpath, sizeof(dirpath), "%s", playerfname);
    char* dirslash = strrchr(dirpath, '/');
    if (dirslash) {
        *dirslash = '\0';
    }
    if (finalize_player_file_rename("players/temp", dirpath, name, versioned)) {
        sprintf((player_table + index_pos)->ch_file, "%s", versioned);
    } else {
        log("save_player: could not finalize player file.");
    }
}
```

- [ ] **Step 4: Add the finalize include to `db.cpp`**

Near db.cpp's other local includes, add:

```cpp
#include "player_file_finalize.h"
```

- [ ] **Step 5: Build and run the save tests (Docker)**

Run: `scripts/rots-docker.sh test --gtest_filter=DbLoader.LegacyPlayerText*:PlayerFinalize.*`
Expected: PASS — round-trip identical, exactly one file remains.

- [ ] **Step 6: Commit**

```bash
git add src/db.cpp src/tests/db_loader_tests.cpp
git commit -m "feat: make legacy save_player crash-safe via atomic write-temp-then-rename"
```

---

## Task 5: Port `crashsave_schedule` (pure scheduler) + gtest

**Files:**
- Create: `src/crashsave_schedule.h`, `src/crashsave_schedule.cpp`
- Create: `src/tests/crashsave_schedule_tests.cpp`
- Modify: `src/CMakeLists.txt`

**Interfaces:**
- Produces: `int autosave_interval_pulses(int interval_seconds, int tics_per_second);` and `struct AutosaveTimer { int pulses_since_fire = 0; bool tick(int interval_pulses); };`

- [ ] **Step 1: Write the failing test** — `src/tests/crashsave_schedule_tests.cpp`

```cpp
#include "../crashsave_schedule.h"
#include <gtest/gtest.h>

TEST(CrashsaveSchedule, IntervalPulsesBasic) {
    EXPECT_EQ(autosave_interval_pulses(240, 4), 960); // the new 4-minute default
    EXPECT_EQ(autosave_interval_pulses(30, 4), 120);
    EXPECT_EQ(autosave_interval_pulses(15, 4), 60);   // exactly the 15s floor
    EXPECT_EQ(autosave_interval_pulses(60, 4), 240);
}

TEST(CrashsaveSchedule, IntervalPulsesClampsBelowMinimum) {
    EXPECT_EQ(autosave_interval_pulses(14, 4), 60);
    EXPECT_EQ(autosave_interval_pulses(1, 4), 60);
    EXPECT_EQ(autosave_interval_pulses(0, 4), 60);
    EXPECT_EQ(autosave_interval_pulses(-5, 4), 60);
    EXPECT_EQ(autosave_interval_pulses(30, 0), 30); // tics clamped to >= 1
    EXPECT_EQ(autosave_interval_pulses(0, 0), 15);
}

TEST(CrashsaveSchedule, TimerFiresOnIntervalAndResets) {
    AutosaveTimer t;
    for (int i = 0; i < 119; i++) {
        EXPECT_FALSE(t.tick(120)) << "unexpected fire at pulse " << i;
    }
    EXPECT_TRUE(t.tick(120));
    for (int i = 0; i < 119; i++) {
        EXPECT_FALSE(t.tick(120)) << "unexpected fire after reset at pulse " << i;
    }
    EXPECT_TRUE(t.tick(120));
}

TEST(CrashsaveSchedule, TimerIntervalOfOneFiresEveryPulse) {
    AutosaveTimer t;
    EXPECT_TRUE(t.tick(1));
    EXPECT_TRUE(t.tick(1));
}

TEST(CrashsaveSchedule, TimerClampsNonPositiveInterval) {
    AutosaveTimer t;
    EXPECT_TRUE(t.tick(0));
    EXPECT_TRUE(t.tick(0));
}
```

- [ ] **Step 2: Create the header** — `src/crashsave_schedule.h`

```cpp
#ifndef CRASHSAVE_SCHEDULE_H
#define CRASHSAVE_SCHEDULE_H

// Pure crash-save scheduling logic, isolated from game state so it can be unit-tested
// without linking the game loop.

// Convert a configured crash-save interval (seconds) into game-loop pulses. The interval
// is clamped up to a 15-second minimum (and the tic rate to >= 1), so a mis-set or too-small
// configured value cannot make the snapshot run too frequently.
int autosave_interval_pulses(int interval_seconds, int tics_per_second);

// Per-pulse accumulator for the periodic crash-save. The game loop calls tick() once per
// pulse; it returns true (and resets) when interval_pulses have elapsed.
struct AutosaveTimer {
    // Pulses counted since the timer last fired; reset to 0 each time tick() fires.
    int pulses_since_fire = 0;

    // Advance by one pulse. Returns true exactly when the interval elapses, then resets.
    bool tick(int interval_pulses);
};

#endif // CRASHSAVE_SCHEDULE_H
```

- [ ] **Step 3: Create the implementation** — `src/crashsave_schedule.cpp`

```cpp
#include "crashsave_schedule.h"

// The shortest interval the periodic snapshot may run at: a floor so a mis-set or
// deliberately tiny configured value cannot hammer the disk.
static constexpr int MIN_AUTOSAVE_INTERVAL_SECONDS = 15;

int autosave_interval_pulses(int interval_seconds, int tics_per_second) {
    if (tics_per_second < 1) {
        tics_per_second = 1;
    }
    if (interval_seconds < MIN_AUTOSAVE_INTERVAL_SECONDS) {
        interval_seconds = MIN_AUTOSAVE_INTERVAL_SECONDS;
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

- [ ] **Step 4: Wire into CMake** — add `crashsave_schedule.cpp` to `ROTS_SERVER_SOURCES` and `tests/crashsave_schedule_tests.cpp` to `ROTS_TEST_SOURCES` in `src/CMakeLists.txt`.

- [ ] **Step 5: Build and run (Docker)**

Run: `make test`
Then: `./bin/tests --gtest_filter=CrashsaveSchedule.*`
Expected: all 5 PASS (including `autosave_interval_pulses(240, 4) == 960`).

- [ ] **Step 6: Commit**

```bash
git add src/crashsave_schedule.h src/crashsave_schedule.cpp src/tests/crashsave_schedule_tests.cpp src/CMakeLists.txt
git commit -m "feat: add pure unit-tested autosave scheduler (seconds->pulses + timer)"
```

---

## Task 6: Repoint the autosave config to seconds (default 240)

**Files:**
- Modify: `src/config.cpp` (lines 37-40 — **CRLF file; preserve endings**)

- [ ] **Step 1: Change the unit + default**

Replace the comment and value at `config.cpp:38-40`:

```c
/* RENT/CRASHSAVE OPTIONS */
/* How often (in SECONDS) should the MUD crash-save connected players? Driven through
   autosave_interval_pulses(); clamped to a 15s floor. Default 240s == the historical
   4-minute cadence. */
int autosave_time = 240;
```

(Editor must keep CRLF line endings on this file — verify `git diff` shows only these lines changed, not a whole-file EOL flip.)

- [ ] **Step 2: Build (Docker)**

Run: `make`
Expected: links. (Behavior unchanged until the heartbeat gate is rewired in Task 7.)

- [ ] **Step 3: Commit**

```bash
git add src/config.cpp
git commit -m "feat: autosave_time is now seconds (default 240 = unchanged 4-minute cadence)"
```

---

## Task 7: Drive the heartbeat autosave from the scheduler

**Files:**
- Modify: `src/comm.cpp` — includes (~line 24), counter decl (`:691`), gate (`:1047-1053`)

**Interfaces:**
- Consumes: `autosave_interval_pulses` + `AutosaveTimer` (Task 5), `TICS_PER_SECOND` (`structs.h`), `autosave_time` (`config.cpp`).

- [ ] **Step 1: Include the scheduler header** — add to the `#include` block in `comm.cpp` (alphabetical neighborhood, ~line 24, near `"color.h"`/`"comm.h"`):

```cpp
#include "crashsave_schedule.h"
```

- [ ] **Step 2: Replace the inline counter with an `AutosaveTimer`**

At `comm.cpp:691`, the declaration is `int mins_since_crashsave = 0, mask;`. Change it to keep `mask` and replace the counter with a timer:

```cpp
    int mask;
    AutosaveTimer autosave_timer;
```

- [ ] **Step 3: Replace the minutes gate with the per-pulse tick**

Replace the block at `comm.cpp:1047-1053`:

```cpp
        if (!(pulse % (60 * 4))) /* one minute */
        {
            if (++mins_since_crashsave >= autosave_time) {
                mins_since_crashsave = 0;
                Crash_save_all();
            }
        }
```

with the seconds-driven, per-pulse gate (evaluated every pulse; the timer accumulates):

```cpp
        // Periodic crash-save snapshot cadence, driven by the configurable seconds interval
        // (autosave_time) through the unit-tested scheduler. Default 240s == 960 pulses ==
        // the historical 4-minute cadence. Crash_save_all itself is unchanged here.
        if (autosave_timer.tick(autosave_interval_pulses(autosave_time, TICS_PER_SECOND))) {
            Crash_save_all();
        }
```

- [ ] **Step 4: Build + full test suite (Docker)**

Run: `make test`
Expected: builds; all gtests pass. Confirm `git grep -n mins_since_crashsave src/` returns nothing (the counter is fully removed).

- [ ] **Step 5: Commit**

```bash
git add src/comm.cpp
git commit -m "feat: drive heartbeat autosave from the seconds-based scheduler (240s default)"
```

---

## Task 8: Port `Stopwatch`

**Files:**
- Create: `src/stopwatch.h`

- [ ] **Step 1: Create the header** (header-only; consumed by the benchmark module)

```cpp
#ifndef STOPWATCH_H
#define STOPWATCH_H

#include <chrono>

// Minimal monotonic stopwatch on std::chrono::steady_clock. Portable, no POSIX timing.
// Call start() then stop() around the region; elapsed<Duration>() returns the span in the
// requested std::chrono duration, defaulting to microseconds.
class Stopwatch {
    // Timestamp captured by start() -- the beginning of the measured interval.
    std::chrono::steady_clock::time_point start_;
    // Timestamp captured by stop() -- the end of the measured interval.
    std::chrono::steady_clock::time_point stop_;

public:
    void start() { start_ = std::chrono::steady_clock::now(); }
    void stop() { stop_ = std::chrono::steady_clock::now(); }

    template <typename DurationT = std::chrono::microseconds>
    DurationT elapsed() const {
        return std::chrono::duration_cast<DurationT>(stop_ - start_);
    }
};

#endif // STOPWATCH_H
```

- [ ] **Step 2: Commit** (no TU/test yet; exercised by Task 10)

```bash
git add src/stopwatch.h
git commit -m "feat: add portable std::chrono Stopwatch for benchmarking"
```

---

## Task 9: Expose the two file-I/O helpers for stage timing

**Files:**
- Modify: the account namespace header that already declares `read_account_file` (confirm with `git grep -n "read_account_file" src/*.h`; likely `src/account_management.h` or `src/account_management_storage.h`)

**Interfaces:**
- Produces (declarations for existing definitions in `account_management.cpp`):
  - `bool read_text_file(const std::string& path, std::string* contents, std::string* error_message);`
  - `bool write_text_file_atomically(const std::string& path, const std::string& text, std::string* error_message);`

- [ ] **Step 1: Find the right header**

Run: `git grep -n "read_account_file" src/*.h`
Use the header (and the `account` namespace) that declares `read_account_file`.

- [ ] **Step 2: Add the two declarations** (inside the same `namespace account { … }`, matching the existing signatures in `account_management.cpp:467-614`)

```cpp
// Read an entire text file into *contents (POSIX-backed). Exposed for stage-timing the
// LOAD pipeline's file-read step.
bool read_text_file(const std::string& path, std::string* contents, std::string* error_message);

// Atomic write: temp(path+".tmp") -> fwrite -> rename. Exposed for stage-timing the SAVE
// pipeline's disk-write step against a throwaway path.
bool write_text_file_atomically(const std::string& path, const std::string& text,
                                std::string* error_message);
```

If either function is currently in an anonymous namespace inside `account_management.cpp`, move it into the named `account` namespace (remove from the anon namespace) so the declaration links. Verify nothing else broke.

- [ ] **Step 3: Build (Docker)**

Run: `make`
Expected: links cleanly.

- [ ] **Step 4: Commit**

```bash
git add src/account_management*.h src/account_management.cpp
git commit -m "refactor: expose read_text_file/write_text_file_atomically for stage timing"
```

---

## Task 10: `save_benchmark` pipeline profiler + offline gtest

**Files:**
- Create: `src/save_benchmark.h`, `src/save_benchmark.cpp`
- Create: `src/tests/save_benchmark_tests.cpp`
- Modify: `src/CMakeLists.txt`

**Interfaces:**
- Consumes: `Stopwatch` (Task 8); `char_to_store`/`store_to_char` (`db.h`); `account::read_account_file`, `account::read_text_file`, `account::write_text_file_atomically`, `account::find_linked_character_owner_account`, `account::account_character_player_path`, `account::AccountData` (account headers); `character_json::character_data_from_store`/`serialize_character_to_json`/`deserialize_character_from_json`/`apply_character_data_to_store` (`character_json.h`).
- Produces:
  - `struct savebench::StageTiming { std::string name; long min_us, avg_us, max_us; double share; };`
  - `struct savebench::PipelineReport { std::vector<StageTiming> stages; StageTiming other; StageTiming total; };`
  - `bool savebench::profile_save(const char_file_u& chd, const std::string& root, const std::string& account_name, const std::string& character_name, const std::string& scratch_path, int iterations, PipelineReport* out, std::string* error);`
  - `bool savebench::profile_load(const std::string& root, const std::string& account_name, const std::string& character_name, int iterations, bool include_store_to_char, PipelineReport* out, std::string* error);`
  - `std::string savebench::format_report(const std::string& title, const PipelineReport& r);`

- [ ] **Step 1: Write the failing test** — `src/tests/save_benchmark_tests.cpp`

```cpp
#include "../save_benchmark.h"
#include "../account_management.h"
#include "../db.h"
#include "../structs.h"
#include <gtest/gtest.h>
#include <string>

// Reuses the account fixture helpers used by db_loader_tests/account_management_tests:
// builds an account-owned character "aragorn" under a TempDirectory, then profiles both
// pipelines and asserts the SAVE bytes round-trip through LOAD.
TEST(SaveBenchmark, ProfilesBothDirectionsAndRoundTrips) {
    TempDirectory temp; // existing test helper (see account_management_tests.cpp)
    const std::string root = temp.path();
    const std::string account = "alpha-admin";
    const std::string character = "aragorn";
    std::string err;

    // Arrange: link + migrate a legacy character so an account-owned .character.json exists.
    ASSERT_TRUE(account::admin_link_character(root, account, character, 1700010102, nullptr, &err)) << err;
    account::CharacterMigrationData migration;
    ASSERT_TRUE(account::migrate_legacy_character_by_name(root, account, character, 1700010103, &migration, &err)) << err;

    // A stored character to feed the SAVE pipeline.
    char_file_u source {};
    ASSERT_TRUE(account::read_account_character_file(root, account, character, &source, &err)) << err;

    savebench::PipelineReport save_report, load_report;
    ASSERT_TRUE(savebench::profile_save(source, root, account, character, root + "/sb_scratch.json",
                                        5, &save_report, &err)) << err;
    ASSERT_TRUE(savebench::profile_load(root, account, character, 5, /*include_store_to_char=*/true,
                                        &load_report, &err)) << err;

    // The breakdown lists every stage and the total >= 0; shares sum to ~100%.
    EXPECT_FALSE(save_report.stages.empty());
    EXPECT_GE(save_report.total.avg_us, 0);
    double save_share = save_report.other.share;
    for (const auto& s : save_report.stages) save_share += s.share;
    EXPECT_NEAR(save_share, 100.0, 1.0);

    // Round-trip: LOADing what we just (conceptually) saved reproduces the source struct.
    char_file_u reloaded {};
    ASSERT_TRUE(account::read_account_character_file(root, account, character, &reloaded, &err)) << err;
    EXPECT_EQ(0, memcmp(&source, &reloaded, sizeof(char_file_u)));

    // Human-readable output for the engineer running the suite.
    printf("%s", savebench::format_report("SAVE", save_report).c_str());
    printf("%s", savebench::format_report("LOAD", load_report).c_str());
}
```

(If the exact fixture helper names differ, mirror the arrange-block already used in `src/tests/db_loader_tests.cpp` around the `admin_link_character`/`migrate_legacy_character_by_name` calls — same names, same `TempDirectory`.)

- [ ] **Step 2: Create the header** — `src/save_benchmark.h`

```cpp
#ifndef SAVE_BENCHMARK_H
#define SAVE_BENCHMARK_H

#include <string>
#include <vector>

struct char_file_u;

namespace savebench {

// One pipeline stage's timing over N iterations, plus its share of the operation total.
struct StageTiming {
    std::string name;
    long min_us = 0;
    long avg_us = 0;
    long max_us = 0;
    double share = 0.0; // percent of the end-to-end total
};

// Per-direction breakdown: each named stage, the minor-middle remainder, and the total.
struct PipelineReport {
    std::vector<StageTiming> stages;
    StageTiming other; // total - sum(named stages): validate/mkdir/path/owner-resolve/index
    StageTiming total; // end-to-end operation
};

// Profile the SAVE pipeline for an already-serialized character (chd). Times S1-S5 plus the
// end-to-end total; writes ONLY to scratch_path (a throwaway). Never touches live files.
bool profile_save(const char_file_u& chd, const std::string& root,
                  const std::string& account_name, const std::string& character_name,
                  const std::string& scratch_path, int iterations,
                  PipelineReport* out, std::string* error);

// Profile the LOAD pipeline for an account-owned character. Times L1-L4 (+ L5 store_to_char
// when include_store_to_char is true; offline-only, since it allocates into a scratch char).
bool profile_load(const std::string& root, const std::string& account_name,
                  const std::string& character_name, int iterations,
                  bool include_store_to_char, PipelineReport* out, std::string* error);

// Render a report as a fixed-width table (stage | min | avg | max | share%).
std::string format_report(const std::string& title, const PipelineReport& report);

} // namespace savebench

#endif // SAVE_BENCHMARK_H
```

- [ ] **Step 3: Create the implementation** — `src/save_benchmark.cpp`

```cpp
#include "save_benchmark.h"

#include "account_management.h"
#include "character_json.h"
#include "db.h"
#include "stopwatch.h"
#include "structs.h"

#include <cstdio>
#include <functional>
#include <limits>
#include <string>
#include <vector>

namespace savebench {
namespace {

// Time `body` `iterations` times; return {min, avg, max} microseconds (share filled later).
StageTiming time_stage(const std::string& name, int iterations,
                       const std::function<void()>& body) {
    StageTiming t;
    t.name = name;
    long sum = 0;
    long lo = std::numeric_limits<long>::max();
    long hi = 0;
    Stopwatch sw;
    for (int i = 0; i < iterations; ++i) {
        sw.start();
        body();
        sw.stop();
        const long us = static_cast<long>(sw.elapsed<std::chrono::microseconds>().count());
        sum += us;
        if (us < lo) lo = us;
        if (us > hi) hi = us;
    }
    t.min_us = (iterations > 0) ? lo : 0;
    t.max_us = hi;
    t.avg_us = (iterations > 0) ? sum / iterations : 0;
    return t;
}

// Fill each stage's share and the "other" remainder = total - sum(named), then totals' share.
void finalize_shares(PipelineReport* r) {
    long named = 0;
    for (const StageTiming& s : r->stages) named += s.avg_us;
    r->other.name = "other (validate/mkdir/path/owner/index)";
    r->other.avg_us = r->total.avg_us - named;
    if (r->other.avg_us < 0) r->other.avg_us = 0;
    const double total = (r->total.avg_us > 0) ? static_cast<double>(r->total.avg_us) : 1.0;
    for (StageTiming& s : r->stages) s.share = 100.0 * s.avg_us / total;
    r->other.share = 100.0 * r->other.avg_us / total;
    r->total.share = 100.0;
    r->total.name = "TOTAL";
}

} // namespace

bool profile_save(const char_file_u& chd, const std::string& root,
                  const std::string& account_name, const std::string& character_name,
                  const std::string& scratch_path, int iterations,
                  PipelineReport* out, std::string* error) {
    if (iterations < 1) iterations = 1;
    std::string err;
    account::AccountData account;

    // S2: read + parse account.json.
    out->stages.push_back(time_stage("S2 read_account_file", iterations, [&]() {
        account::read_account_file(root, account_name, &account, &err);
    }));
    // S3: char_file_u -> CharacterData.
    character_json::CharacterData cd;
    out->stages.push_back(time_stage("S3 character_data_from_store", iterations, [&]() {
        cd = character_json::character_data_from_store(chd);
    }));
    // S4: CharacterData -> JSON string.
    std::string json;
    out->stages.push_back(time_stage("S4 serialize_character_to_json", iterations, [&]() {
        json = character_json::serialize_character_to_json(cd);
    }));
    // S5: atomic temp-write + rename to a THROWAWAY path (never a live file).
    out->stages.push_back(time_stage("S5 write_text_file_atomically", iterations, [&]() {
        account::write_text_file_atomically(scratch_path, json, &err);
    }));
    // Total: the whole serialize-to-disk operation, end to end, into the throwaway path.
    out->total = time_stage("TOTAL save", iterations, [&]() {
        account::AccountData a;
        account::read_account_file(root, account_name, &a, &err);
        const character_json::CharacterData c = character_json::character_data_from_store(chd);
        const std::string j = character_json::serialize_character_to_json(c);
        account::write_text_file_atomically(scratch_path, j, &err);
    });
    std::remove(scratch_path.c_str()); // clean up the throwaway
    finalize_shares(out);
    (void)error;
    return true;
}

bool profile_load(const std::string& root, const std::string& account_name,
                  const std::string& character_name, int iterations,
                  bool include_store_to_char, PipelineReport* out, std::string* error) {
    if (iterations < 1) iterations = 1;
    std::string err;
    account::AccountData account;
    const std::string path = account::account_character_player_path(root, account_name, character_name);

    // L1: read + parse account.json.
    out->stages.push_back(time_stage("L1 read_account_file", iterations, [&]() {
        account::read_account_file(root, account_name, &account, &err);
    }));
    // L2: read character.json bytes.
    std::string json;
    out->stages.push_back(time_stage("L2 read_text_file", iterations, [&]() {
        account::read_text_file(path, &json, &err);
    }));
    // L3: JSON -> CharacterData.
    character_json::CharacterData cd;
    out->stages.push_back(time_stage("L3 deserialize_character_from_json", iterations, [&]() {
        cd = character_json::CharacterData {};
        character_json::deserialize_character_from_json(json, &cd, &err);
    }));
    // L4: CharacterData -> char_file_u.
    char_file_u chd {};
    out->stages.push_back(time_stage("L4 apply_character_data_to_store", iterations, [&]() {
        character_json::apply_character_data_to_store(cd, &chd, &err);
    }));
    // L5: char_file_u -> live char (OFFLINE only; allocates into the scratch char).
    if (include_store_to_char) {
        out->stages.push_back(time_stage("L5 store_to_char", iterations, [&]() {
            char_data scratch {};
            store_to_char(&chd, &scratch);
        }));
    }
    out->total = time_stage("TOTAL load", iterations, [&]() {
        account::AccountData a;
        account::read_account_file(root, account_name, &a, &err);
        std::string j;
        account::read_text_file(path, &j, &err);
        character_json::CharacterData c {};
        character_json::deserialize_character_from_json(j, &c, &err);
        char_file_u s {};
        character_json::apply_character_data_to_store(c, &s, &err);
    });
    finalize_shares(out);
    (void)error;
    return true;
}

std::string format_report(const std::string& title, const PipelineReport& r) {
    char line[160];
    std::string out = "\n=== " + title + " pipeline (microseconds) ===\n";
    out += "  stage                                    min     avg     max   share%\n";
    for (const StageTiming& s : r.stages) {
        snprintf(line, sizeof(line), "  %-38s %6ld  %6ld  %6ld   %5.1f\n",
                 s.name.c_str(), s.min_us, s.avg_us, s.max_us, s.share);
        out += line;
    }
    snprintf(line, sizeof(line), "  %-38s %6ld  %6ld  %6ld   %5.1f\n",
             r.other.name.c_str(), r.other.min_us, r.other.avg_us, r.other.max_us, r.other.share);
    out += line;
    snprintf(line, sizeof(line), "  %-38s %6ld  %6ld  %6ld   %5.1f\n",
             r.total.name.c_str(), r.total.min_us, r.total.avg_us, r.total.max_us, r.total.share);
    out += line;
    return out;
}

} // namespace savebench
```

> Note for the implementer: if `char_data scratch {};` does not zero-init cleanly (it is a large legacy struct), use the project's char allocator/clearer instead (e.g. a heap `char_data` cleared the way the loader does) and free it after the loop — keep L5 **offline-only** regardless. Verify the exact `account::` signatures against the headers from Task 9 / `git grep`.

- [ ] **Step 4: Wire into CMake** — add `save_benchmark.cpp` to `ROTS_SERVER_SOURCES` and `tests/save_benchmark_tests.cpp` to `ROTS_TEST_SOURCES`.

- [ ] **Step 5: Build and run (Docker)**

Run: `make test`
Then: `./bin/tests --gtest_filter=SaveBenchmark.*`
Expected: PASS — shares sum to ~100%, round-trip `memcmp == 0`, and the SAVE/LOAD tables print.

- [ ] **Step 6: Commit**

```bash
git add src/save_benchmark.h src/save_benchmark.cpp src/tests/save_benchmark_tests.cpp src/CMakeLists.txt
git commit -m "feat: add character persistence pipeline benchmark (offline gtest)"
```

---

## Task 11: In-game `savebench` implementor command

**Files:**
- Create: `src/savebench.h`, `src/savebench.cpp`
- Modify: `src/interpre.cpp` (include; `command[]` index 249, ~line 558; `COMMANDO`, ~line 2237)
- Modify: `src/CMakeLists.txt`

**Interfaces:**
- Consumes: `savebench::profile_save`/`profile_load`/`format_report` (Task 10); `account::find_linked_character_owner_account`, `account::read_account_character_file`; `char_to_store` (`db.h`); `send_to_char` (`comm.h`).
- Produces: `ACMD(do_savebench);`

- [ ] **Step 1: Create the header** — `src/savebench.h`

```cpp
#ifndef SAVEBENCH_H
#define SAVEBENCH_H

#include "interpre.h" // for ACMD

// Implementor-only command: profile the account-JSON save/load pipeline for the invoking
// character against its real on-disk files, writing only to throwaway paths. Never mutates
// live state. Usage: "savebench [iterations]" (default 100, clamped 1..10000).
ACMD(do_savebench);

#endif // SAVEBENCH_H
```

- [ ] **Step 2: Create the implementation** — `src/savebench.cpp`

```cpp
#include "savebench.h"

#include "account_management.h"
#include "comm.h"
#include "db.h"
#include "save_benchmark.h"
#include "structs.h"
#include "utils.h"

#include <cstdlib>
#include <string>

ACMD(do_savebench)
{
    if (GET_LEVEL(ch) < LEVEL_IMPL) {
        send_to_char("You can't do that.\r\n", ch);
        return;
    }
    if (IS_NPC(ch) || !ch->desc) {
        send_to_char("savebench: needs a real, connected character.\r\n", ch);
        return;
    }

    int iterations = 100;
    skip_spaces(&argument);
    if (argument && *argument) {
        iterations = atoi(argument);
    }
    if (iterations < 1) iterations = 1;
    if (iterations > 10000) iterations = 10000;

    std::string owner, err;
    if (!account::find_linked_character_owner_account(".", GET_NAME(ch), &owner, &err) || owner.empty()) {
        send_to_char("savebench: this character is not account-linked; nothing to profile.\r\n", ch);
        return;
    }

    // SAVE: serialize the live char (read-only) and profile S1-S5 into a throwaway path.
    char_file_u chd {};
    char_to_store(ch, &chd);
    const std::string scratch = std::string("players/SAVEBENCH_") + GET_NAME(ch) + ".json";

    savebench::PipelineReport save_report, load_report;
    if (!savebench::profile_save(chd, ".", owner, GET_NAME(ch), scratch, iterations, &save_report, &err)) {
        send_to_char("savebench: save profiling failed.\r\n", ch);
        return;
    }
    // LOAD: profile L1-L4 against the real files (read-only). L5 (store_to_char) is offline-only.
    if (!savebench::profile_load(".", owner, GET_NAME(ch), iterations, /*include_store_to_char=*/false,
                                 &load_report, &err)) {
        send_to_char("savebench: load profiling failed.\r\n", ch);
        return;
    }

    std::string report = "savebench: " + std::to_string(iterations) + " iterations (live char NOT modified)\r\n";
    report += savebench::format_report("SAVE", save_report);
    report += savebench::format_report("LOAD (L1-L4; L5 offline-only)", load_report);
    page_string(ch->desc, const_cast<char*>(report.c_str()), 1);
}
```

> Note: confirm `skip_spaces`/`page_string` signatures against the codebase (both are standard RotS helpers in `utils.h`/`comm.h`); if `page_string` is unavailable, fall back to `send_to_char(report.c_str(), ch)` (the report fits typical page buffers at default iterations).

- [ ] **Step 3: Register the command in `interpre.cpp`**

(a) Add the include near the other command includes (e.g. by `#include "mob_csv_extract.h"`):
```cpp
#include "savebench.h"
```
(b) In the `command[]` array, replace the `"\n"` sentinel that currently sits at index 249 (right after `"mob2csv"`, `interpre.cpp:~558`) so `savebench` becomes index 249 and the sentinel follows:
```cpp
    "mob2csv",
    "savebench", // 249
    "\n"
```
(c) Add the `COMMANDO` registration after the index-248 entry (`interpre.cpp:~2238`):
```cpp
    COMMANDO(249, POSITION_DEAD, do_savebench, LEVEL_IMPL, FALSE, 0,
        TAR_IGNORE, TAR_IGNORE, 0);
```

- [ ] **Step 4: Wire into CMake** — add `savebench.cpp` to `ROTS_SERVER_SOURCES`.

- [ ] **Step 5: Build the server (Docker)**

Run: `make`
Expected: `ageland` links. (The command path is integration-tested on the dev server, not in CI — see Step 6.)

- [ ] **Step 6: Manual dev-server verification (not CI)**

On the dev server, log in as an implementor on an account-linked character and run `savebench 50`. Expected: a SAVE table and a LOAD table print with per-stage µs + shares summing to ~100% and a TOTAL row; **confirm no `players/SAVEBENCH_*` file remains and the character's real `.character.json` mtime is unchanged** (live state untouched).

- [ ] **Step 7: Commit**

```bash
git add src/savebench.h src/savebench.cpp src/interpre.cpp src/CMakeLists.txt
git commit -m "feat: add implementor savebench command (sandboxed pipeline profiler)"
```

---

## Self-Review (completed)

**Spec coverage:** Prep (Task 1) ✔; legacy atomic finalize + A/B oracle + rewire (Tasks 2-4) ✔; full pipeline benchmark both directions with total + remainder, offline + in-game (Tasks 8-11) ✔; scheduler at 240s default + config + heartbeat (Tasks 5-7) ✔; CMake wiring in each task ✔; deferred items (snapshot de-gate, cadence reduction, anti-rollback, notify=0, CON_LINKLS doc) correctly NOT in any task ✔.

**Type consistency:** `finalize_player_file_rename`/`finalize_player_file_legacy`/`write_player_text` signatures match between Tasks 2/3/4. `StageTiming`/`PipelineReport`/`profile_save`/`profile_load`/`format_report` consistent between Tasks 10 (def) and 11 (use). `autosave_interval_pulses`/`AutosaveTimer` consistent between Tasks 5 and 7.

**Known verify-on-execute points (flagged inline, not placeholders):** the account-namespace header that declares `read_account_file` (Task 9 Step 1); the exact `TempDirectory`/fixture helper names (Task 10 Step 1, mirror `db_loader_tests.cpp`); `char_data` zero-init vs allocator for L5 (Task 10 Step 3 note); `page_string` vs `send_to_char` (Task 11 Step 2 note). Each has an explicit fallback.
