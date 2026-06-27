# Design: `savebench` — A/B verification of player-file finalize

- **Date:** 2026-06-27
- **Status:** Approved (design); pending implementation plan
- **Topic:** Replace the `system()`-based player-file finalize with direct filesystem calls, behind an implementor-only in-game verification command — without changing the production save path.

## 1. Background

RotS is a strictly single-threaded MUD; `game_loop()` (`src/comm.cpp:471`) is a `select()`-based event loop at 4 pulses/sec, and **every disk save runs inline on that one thread**. The eventual goal is to move all saving off the game thread onto a background serialization thread fed by a queue of pointer-free snapshots. This spec is the **first, low-risk step** toward that goal.

Discovery found that the dominant on-thread cost of a character save is **not** the serialization — it is the *finalize* step of `save_player()` (`src/db.cpp:2302`), which shells out twice per save:

- `sprintf(temp, "rm %s.*", playerfname); system(temp);` (`src/db.cpp:2463-2464`)
- `sprintf(temp, "cp players/temp %s", playerfname); system(temp);` (`src/db.cpp:2470-2471`)

Each `system()` is a `fork()` + `exec()` of `/bin/sh` (which itself forks `rm`/`cp`) and a blocking `waitpid()`. The autosave (`Crash_save_all`, every 4 min) does this **sequentially for every connected player** in a single pulse, freezing the heartbeat for the duration. Replacing the two `system()` calls with `unlink()`/`rename()` removes this cost entirely, is fully single-threaded (zero concurrency risk), gives **atomic, crash-safe** finalization, and is a prerequisite the future background-save design needs anyway.

The codebase is C++17 (`-std=c++1z`), built 32-bit (`-m32`) for Linux (i386 Docker). `<stdio.h>` (for `rename`) is already included in `db.cpp:6`, and `unlink()` is already used in `db.cpp` (e.g. `db.cpp:3762`), so no new headers are required.

## 2. Goals & non-goals

### Goals
1. Extract the legacy finalize logic into a standalone, reusable function — a pure refactor with no behavior change.
2. Implement a new finalize function using direct filesystem calls (`unlink` + `rename`) that is **byte- and behavior-equivalent** to the legacy one.
3. Add an implementor-only (level 100) in-game command, `savebench`, that runs both implementations against the invoking character's serialized data, **proves their output is identical**, and **profiles** the cost of each (min/avg/max µs/call).
4. Add an offline unit test (gtest, `src/tests`) that pins the equivalence deterministically in CI.

### Non-goals (explicitly out of scope for this change)
- **Switching the production save path to the new finalizer.** Production `save_player`/`save_char` stay on the legacy implementation until the new one is verified locally *and* on the test server. Flipping production over is a separate, later change.
- Any background thread / async work (that is the future phase this de-risks).
- Object/rent file finalization (`objsave.cpp`), the autosave loop, world/zone state, boards, or mail.
- Changing the on-disk file format or the `players/temp` scratch convention used by production.

## 3. Guardrails (the test command must never)

The `savebench` command must **never**:
- write into a real bucket directory (`players/A-E/` … `players/ZZZ/`),
- run the `rm`-glob (or unlink-enumeration) against a real player base path,
- call `save_char` / `save_player`,
- mutate `player_table[index_pos].ch_file`.

Any of those would repoint or destroy the live player file, because `load_player()` (`src/db.cpp:1746-1759`) reads `player_table[].ch_file` **directly** with no directory scan or disambiguation at load time. All test I/O happens in throwaway directories under `players/`, removed on every exit path.

## 4. Detailed design

### 4.1 Code extraction (refactor of `save_player`)

Split the relevant parts of `save_player` (`src/db.cpp:2302`) into small free functions: **paths in, `bool` out, no globals, no `char_data`/`player_table` knowledge** in the finalize functions. Declared in `src/db.h`, implemented in `src/db.cpp` (both already in `OBJFILES` for the traditional build and `src/tests/Makefile`, and CMake auto-globs `src/*.cpp` per `src/CMakeLists.txt:13` — so no build-file edits are required).

```c
// Bucket logic (currently db.cpp:2315-2355) -> base path, e.g. "players/P-T/gandalf".
void player_base_path(const char *lowercased_name, char *out, size_t out_sz);

// Serialize one char to a scratch file: fopen + char_to_store + the ~60 fprintf block + fclose
// (currently db.cpp:2357-2462). Mirrors today's behavior exactly, including the affect_total
// REMOVE/SET around char_to_store (db.cpp:2104/2176) and the pwd/host copies (db.cpp:2359-2360).
bool write_player_text(struct char_data *ch, int load_room, const char *scratch_path);

// LEGACY finalize — unchanged behavior: system("rm <base>.*") then system("cp <scratch> <versioned>").
bool finalize_player_file_legacy(const char *scratch_path, const char *base_path,
                                 const char *versioned_path);

// NEW finalize — glob-equivalent: unlink every "<base_name>." entry in dir_path, then rename(scratch, versioned).
bool finalize_player_file_rename(const char *scratch_path, const char *dir_path,
                                 const char *base_name, const char *versioned_path);
```

After the refactor, production `save_player` becomes a thin orchestrator:
`player_base_path(...)` → `write_player_text(...)` to `players/temp` → build the versioned name (`name.level.race.idnum.logtime.flags`, the existing format at `db.cpp:2465-2469`, fields from `player_table[index_pos]`) → `finalize_player_file_legacy(...)` → set `player_table[index_pos].ch_file`.

This preserves today's behavior **byte-for-byte**. `finalize_player_file_rename` exists but **no core path calls it** — satisfying the "extract, don't switch" boundary.

> Note: this extracts the serialization (`write_player_text`) as well as the finalize, so the test can produce real scratch bytes without duplicating the ~60-line `fprintf` block. It is a pure refactor. (If an even more minimal change is preferred, `write_player_text` extraction can be dropped and the serialization left inline, but then the test must reproduce or otherwise obtain scratch bytes.)

### 4.2 New finalizer glob-equivalence (critical correctness)

The legacy `rm %s.*` is a shell **glob** that deletes *every* versioned file for a player. The versioned filename encodes **mutable** fields (`level`, `logtime`, `flags`), so a save can produce a *new* filename, and at boot `build_directory()` (`src/db.cpp:498-545`, via `read_filename_field` at `db.cpp:483-495`) parses bucket filenames into `player_table`. **If two files for one player coexist, `readdir()` order (non-deterministic) decides which wins** — a silent "save rollback."

Therefore `finalize_player_file_rename` MUST:
1. `opendir(dir_path)`, `readdir()`, and `unlink()` every entry whose name starts with **`base_name` followed by a literal `'.'`** (dot-anchored so `bob.` does not match `bobby.`; names are lowercased at `db.cpp:2312-2313`, so match lowercased). Deleting only the single known `ch_file` is **not** equivalent and reintroduces the stale-duplicate hazard.
2. `rename(scratch_path, versioned_path)` — atomic on the same filesystem (both live under `players/`), and **consumes** the scratch (unlike `cp`, which leaves it).
3. Check the `unlink` and `rename` return codes (legacy leaves `system()` returns unchecked — checking is a free robustness gain). On `rename` failure, report and do not claim success.

The resulting file is byte-identical to legacy (same scratch bytes, same destination path). The only observable differences: `rename` leaves no scratch behind, and requires same-filesystem (satisfied here).

### 4.3 The `savebench` command

**Registration** (four edits; handler in `act_wiz.cpp` needs no build-file change):
- Forward-declare `ACMD(do_savebench);` in the prototype block of `src/interpre.cpp` (near `do_show` ~`interpre.cpp:214`). The `ACMD` macro (`src/interpre.h:21-22`) expands to `void do_savebench(struct char_data *ch, char *argument, struct waiting_type *wtl, int cmd, int subcmd)`.
- Append `"savebench"` to the `command[]` table (`src/interpre.cpp:299+`) immediately **before** the `"\n"` terminator (current last entry `"mob2csv"` at `interpre.cpp:547`), making it index **249** (within `MAX_CMD_LIST = 350`, `interpre.h:19`).
- Register: `COMMANDO(249, POSITION_DEAD, do_savebench, LEVEL_IMPL, FALSE, 0, FULL_TARGET, FULL_TARGET, 0);` in the `COMMANDO` block (~`interpre.cpp:1730-2130`). `LEVEL_IMPL = 100` (`src/structs.h:45`) is enforced at `interpre.cpp:1334` (`GET_LEVEL(ch) < cmd_info[cmd].minimum_level` → rejected). `POSITION_DEAD` allows use from any position (matches diagnostics like `do_show`).
- Implement `ACMD(do_savebench)` in `src/act_wiz.cpp` (alongside `do_show` ~`act_wiz.cpp:2330`).

**Usage:** `savebench [N]` — operates on **self** (the invoking immortal); `N` = profiling iterations, default 100.

**Flow inside `do_savebench`:**
1. Reject NPCs / no-descriptor; parse optional `N` (clamp to a sane range, e.g. 1–10000).
2. `mkdir("players/ABTEST_LEGACY")` and `mkdir("players/ABTEST_NEW")` (synthetic dirs, never real buckets; both under `players/` so `rename` stays same-filesystem).
3. `write_player_text(ch, ..., "players/ABTEST_scratch")` once → real serialized bytes for this character. (A flat file under `players/`, not a subdirectory; removed in cleanup.)
4. Build the versioned suffix (`name.level.race.idnum.logtime.flags`) using the **char's live fields** (e.g. `GET_LEVEL(ch)` etc.), *not* `player_table` — same format string as production. The legacy and new outputs share the identical suffix, differing only by directory.
5. **Equivalence check:** copy the scratch into each dir, run `finalize_player_file_legacy` into `ABTEST_LEGACY` and `finalize_player_file_rename` into `ABTEST_NEW`, then `memcmp` the two output files (length + bytes) and assert **each dir contains exactly one file** (`readdir` count). Report `IDENTICAL`, or the first-differing offset / a file-count mismatch.
6. **Profiling:** loop each finalizer `N` times, regenerating the per-iteration scratch (required because `rename` consumes its source; the same scratch-regeneration cost is included for both paths, keeping it apples-to-apples). Measure with `gettimeofday` and integer-µs subtraction (`(t1.tv_sec - t0.tv_sec)*1000000 + (t1.tv_usec - t0.tv_usec)`); track **min / avg / max** µs/call per implementation. (`timediff()` at `comm.cpp:1043-1057` handles the usec borrow if a helper is preferred; the float-seconds `rots_clock` loses sub-ms precision and is avoided.)
7. **Cleanup (unconditional, including early-return/error paths):** unlink every produced file and scratch copy, then `rmdir` both ABTEST dirs. Structured so a mid-test failure cannot leak files under `players/`.

**Output:** `sprintf` into the global `buf` (`MAX_STRING_LENGTH = 8192`) then `send_to_char(buf, ch)` (`comm.h:20`), or the variadic `vsend_to_char` (`comm.h:38`). Float specifiers (`%.2f`) are already used in-tree. Example:

```
savebench: equivalence = IDENTICAL (one file each) [OK]
legacy (system rm+cp):  min 3902 / avg 4821 / max 31044 usec  (N=100)
new    (unlink+rename): min 9    / avg 12   / max 88    usec  (N=100)
speedup ~400x
```

### 4.4 Offline unit test (`src/tests`)

A deterministic gtest (no running MUD) that:
- creates a temp directory, drops a **stale** `name.<old-suffix>` file plus a scratch file with known bytes,
- calls `finalize_player_file_legacy` and `finalize_player_file_rename` into two temp dirs,
- asserts (1) the two outputs are **byte-identical**, and (2) the new finalizer's dir contains **exactly one file** (pinning the glob/stale-file equivalence).

This is the primary regression guard for §4.2 and runs in CI. The finalize functions take plain paths and touch no MUD globals, so they are directly unit-testable. (Build wiring: `db.o` is already in `src/tests/Makefile` `OBJFILES`; the new test source is added per the existing test harness's conventions.)

## 5. What is explicitly NOT changed

Production `save_player`/`save_char` behavior, the autosave loop (`Crash_save_all`), object/rent saving, world/zone state, and the `players/temp` scratch convention are unchanged. `finalize_player_file_rename` is reachable **only** via `savebench` and the gtest until a future change flips production over.

## 6. Verification plan

1. Build the i386 target; run the offline gtest — must pass (identical output + single file remaining).
2. Local MUD: run `savebench`, confirm `IDENTICAL`, sanity-check the timing gap, confirm no stray files remain under `players/`.
3. Test server: same checks, including the no-leftover-files check after repeated runs.
4. Only after both succeed: a *separate, later* change switches production `save_player` to `finalize_player_file_rename` (and, at that point, verify nothing downstream relies on `players/temp` surviving a save).

## 7. Risks & mitigations

- **Stale-duplicate / glob equivalence** — new finalizer must enumerate + unlink all `name.`-prefixed files, not just the known `ch_file`; the gtest's "exactly one file" assertion guards this.
- **`rename` is a move (consumes scratch)** — each A/B iteration regenerates its own scratch; in the future production switch, confirm nothing re-reads `players/temp` after a save.
- **Real-save corruption** — enforced by the §3 guardrails (no real bucket, no `save_char`, no `player_table` mutation) and unconditional cleanup.
- **Prefix collision** — dot-anchor on `name + '.'`, matched lowercased.
- **Same-filesystem requirement for `rename`** — keep ABTEST dirs under `players/`.
- **Fixed-size buffers** — `playerfname[100]` / `temp[255]` (`db.cpp:2304-2306`), `ch_file[80]` (`db.h:194`); keep throwaway paths short and bounded; use `snprintf`-style bounds in new code.
- **32-bit `%ld` format for `flags`** — reuse the identical versioned-name format string so the filename (and thus the file the loader would find) cannot diverge.
- **`command[]`/`COMMANDO` index parity** — the new name's array index must equal its `COMMANDO` number (249); insert before the `"\n"` terminator; stay within `MAX_CMD_LIST = 350`.
- **Behavior change from checked return codes** — the new finalizer checks `unlink`/`rename` returns where `system()` was unchecked; it reports failures rather than silently continuing. This affects only the new (test-only) path.

## 8. Future work (out of scope here)

Once verified, the natural follow-ups are: (a) switch production `save_player` to `finalize_player_file_rename`; (b) apply the same temp-file + atomic-rename treatment to object/rent saving (`objsave.cpp`); (c) amortize `Crash_save_all` across pulses; and ultimately (d) move serialization-to-POD + disk writes onto a background thread fed by a queue of self-contained `char_file_u` / `obj_file_elem` snapshots.
