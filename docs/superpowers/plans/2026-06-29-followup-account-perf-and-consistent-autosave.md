# Follow-Up Plan — Account Persistence Optimization + Consistent-Snapshot Autosave

> **For the next session.** This plan continues the `feature/savebench-port` work. It (1) recaps what is already done, (2) details the **account load/save performance optimization** (do this first), and (3) details **reworking the periodic autosave into a point-in-time consistent snapshot** (the deferred Feature-2). Companion docs: design spec `docs/superpowers/specs/2026-06-29-savebench-port-design.md`, perf findings `docs/superpowers/specs/2026-06-29-savebench-pipeline-performance-findings.md`, and the principles guide on `feature/savebench-finalize` (`docs/superpowers/save-load-porting-guide.md`).
>
> All `file:line` references are current as of `feature/savebench-port` HEAD and **will shift as edits land** — re-grep the symbol before relying on a line number.

---

## 0. How to resume (environment handoff)

- **Branch:** `feature/savebench-port` (off `account-management`), ~19 commits, every task peer-reviewed, whole-branch review = ready-to-merge. Start the follow-up as a new branch off this one (or off `account-management` after this merges).
- **Build/test is Docker i386 only.** Use `scripts/rots-docker.sh test '--gtest_filter=Suite.*'` (build `ageland_tests` + run the binary; **quote filters** — the `*` globs otherwise). `ctest` finds 0 tests under the container's cmake 3.18 — run the binary directly (the wrapper does). Host clang/IDE diagnostics (`std::filesystem`/`gtest`/`MAX`/`MIN`/POSIX) are **false positives**.
- **32-bit test baseline:** the container builds `ageland_tests` 32-bit (CI builds it 64-bit), so ~163 pre-existing tests fail on 32-bit-vs-64-bit expectation diffs in suites this work doesn't touch. **Gate per-test, not per-suite** (run your new/affected tests by name).
- **Line endings (preserve per file):** `config.cpp` and `db.h` are **CRLF**; `comm.cpp`/`db.cpp`/`interpre.cpp`/`objsave.cpp`(mostly)/new files are **LF**. `.claude/.no-autoformat` exists — do not run clang-format.
- **Measuring tooling is in place:** the `savebench [iterations]` implementor command profiles the live save/load pipeline and mirrors its report to the MUD syslog. A disposable account-linked level-100 implementor exists for it: character **`drelibench`** (clone of `Drelidan`) under account **`david.gurley+1@gmail.com`** / password **`Savebench1`** (files under `lib/`, gitignored). To run it: build + boot the i386 container (`cmake --build build --target ageland` then `./bin/ageland`; port 1024 is published via docker-compose), `telnet localhost 1024`, log in by account email, play `drelibench`, run `savebench 500`. (See §4 for the re-measure loop.)

---

## 1. Completed work recap (what the port already delivered)

The `feature/savebench-port` branch carried the *intent* of `feature/savebench-finalize` onto the diverged `account-management` branch as a focused, data-gathering slice. Three components plus tooling, each TDD'd and reviewed:

- **A — legacy save crash-safety.** `save_player`'s `system("rm")`/`system("cp")` finalize → atomic write-temp-then-rename (`src/player_file_finalize.{h,cpp}`, portable `std::filesystem`/`error_code`); byte-identical serializer extraction (`write_player_text` in db.cpp, pinned by `DbLoader.LegacyPlayerText*`); the `system()` version kept only as a gtest A/B oracle. (The account-native JSON path was already atomic, so F1 was mostly verification.)
- **B — save/load pipeline benchmark.** `src/stopwatch.h`, the exposed `account::read_text_file`/`write_text_file_atomically`, `src/save_benchmark.{h,cpp}` (per-stage SAVE/LOAD timing + end-to-end total + reconciling remainder), the offline `SaveBenchmark` gtest, and the sandboxed in-game `do_savebench` command (`src/savebench.cpp`) — which also mirrors its report to the syslog.
- **C — autosave scheduler (behavior-neutral).** `src/crashsave_schedule.{h,cpp}` (pure, unit-tested `autosave_interval_pulses` + `AutosaveTimer`); `config.cpp` `autosave_time` repurposed to **seconds, default 240** (= the historical 4-minute cadence); the heartbeat gate in `comm.cpp:1052` driven by the scheduler. **`Crash_save_all` itself is unchanged — still `PLR_CRASH`-dirty-gated.** This is the seam Phase 2 below builds on.
- **Tooling:** the i386 Docker image now runs the CMake/gtest suite; spec + plan + the perf findings committed under `docs/superpowers/`.

**Explicitly deferred (this plan's Phases 1–2):** the account-link cache (performance), and the consistent-snapshot autosave + anti-rollback hooks. The whole-branch review confirmed none of the deferred behavioral changes leaked in (`objsave.cpp`/`fight.cpp`/`spec_pro.cpp`/`write_exploits` untouched; `Crash_save_all` still dirty-gated).

**First measurement (the reason Phase 1 comes first):** on the live i386/QEMU server, per character **SAVE ≈ 2.1 ms, LOAD ≈ 3.7 ms**. `read_account_file` is the dominant **and redundant** cost (**57.5% of save, 30.9% of load**); `deserialize_character_from_json` is the load bottleneck (48.6%). See the findings doc.

---

## 2. Phase 1 — Account persistence optimization (DO THIS FIRST)

**Why first:** a save-all snapshot (Phase 2) multiplies per-player save cost by the connected-player count every cadence. The dominant per-player cost is a *redundant* account lookup, so eliminating it is the precondition for a safe snapshot.

**Root cause (confirmed):** `account::read_account_file` (`account_management_storage.cpp:244`) is **not** a single-file read — it calls `find_account_file_path_by_account_name` (`account_management.cpp:628`), which `opendir`/`readdir`s the **entire** `lib/accounts/` tree and **deserializes every `account.json`** to match by name, then re-parses the matched file (`account_management.cpp:526`). `find_character_owner_account` (`account_management.cpp:720`) does the same full scan. And `save_char`'s account branch triggers it **three times per save**:
- `db.cpp:3072` — `find_linked_character_owner_account` (resolve owner) → scan #1
- `db.cpp:3076` — `account_character_file_exists` → `inspect_account_character_file` (`account_management_assets.cpp:79`) → `read_account_file` → scan #2
- `db.cpp:3079`/`3091` — `write_account_character_file` (`account_management_assets.cpp:9`) → `read_account_file` → scan #3

Load path (`read_account_character_file` `account_management_assets.cpp:40`, plus the parallel object/exploit reads) re-scans per character too; the boot index build (`db.cpp:663-667`) loops every character of an account and benefits most.

### Task 1.1 — Add an account-resolution cache module
**Files:** create `src/account_cache.{h,cpp}` (LF); wire into `src/CMakeLists.txt` (`ROTS_SERVER_SOURCES` + a `tests/account_cache_tests.cpp` in `ROTS_TEST_SOURCES`).
**Design (single-threaded — no locking):** memoize **both** resolutions so the full win is captured:
- character name → owner account name (**including the negative "not linked" result**, to short-circuit repeat misses).
- account name → resolved file path + parsed `account::AccountData`.
Key entries by `(root_directory, name)` — **`root_directory` is a parameter** (live code passes `"."`/`kAccountStorageRoot=="."`, but tests pass `TempDirectory` paths), so the cache must not bleed across roots, and must be resettable/clearable. Provide a `clear()` for tests (call it in fixture setup) — or disable the cache under `#ifdef TESTING` if simpler and the cache isn't itself under test.
**TDD:** test hit/miss, negative caching, and that two consecutive resolves do one scan (e.g. via a call-count seam or a temp-dir scan counter).

### Task 1.2 — Route resolution through the cache
**Files:** `src/account_management_identity.cpp:647` (`find_linked_character_owner_account`), `src/account_management_storage.cpp:244` (`read_account_file`).
Make each consult the cache: on hit return the memoized owner / copy the cached `AccountData`; on miss run the existing scan once and populate. Keep the public signatures unchanged so call sites don't churn. (Optionally also add `read_account_file` overloads / a `save_char` path that threads a single already-resolved `AccountData` through the three `db.cpp` calls — collapsing them to one resolution even on a cold cache; do this if the cache alone doesn't fully close the gap.)

### Task 1.3 — Invalidate at the single write chokepoint
**File:** `src/account_management_storage.cpp:158` (`write_account_file` — the **only** writer of `account.json`; atomic temp+rename). On a successful write, drop/refresh the cache entries for the written `account_name` and **every name in its `character_links`** (and clear stale negative char entries that the new links now satisfy). This one hook covers **all** mutations — create (`identity.cpp:471`), link/migrate (`:678`/`:720`), unlink/delete (`admin_delete_linked_character` `:837`, write `:931`), password reset (`:827`), block/unblock (`:733`/`:794`) — because they all persist through `write_account_file`. No per-API plumbing needed; assert coverage rather than instrument each API.
**Correctness traps:** a character relinked to a *different* account, and an account blocked/deleted mid-session — both go through `write_account_file`, so the hook handles them as long as it invalidates by the *old* link set too (refresh, don't just add).

### Task 1.4 — Re-measure (gate to Phase 2)
Re-run `savebench 500` on `drelibench` (see §4). **Expected:** S2/L1 (`read_account_file`) shares collapse toward ~0; SAVE total drops ~57%, LOAD ~31%. Record the new numbers in the findings doc. Also run the existing account suites by name (`AccountManagement.*`, `DbLoader.*` — note which are 32-bit-baseline reds) to confirm no behavioral regression.

### Task 1.5 (optional, secondary) — Faster character deserialize
Only if load latency still matters after the cache: `deserialize_character_from_json` (`src/character_json.cpp`) is 48.6% of LOAD and ~5× the serialize side. The hand-rolled `json_utils::JsonReader` is the hotspot. Profile within the parse before optimizing; out of scope unless the data justifies it.

---

## 3. Phase 2 — Consistent-snapshot autosave rework

**Goal (from the guide §2.7–2.13 / the consistent-snapshot design):** make the periodic autosave a **point-in-time snapshot** — in one single-threaded heartbeat pass, save **every** connected (`CON_PLYNG`, non-NPC) player — so PvP/group-boss participants recover to the *same* moment, instead of only inventory-dirty players being saved. Plus unify anti-rollback so impactful events persist immediately.

**Order:** do this **after** Phase 1 — de-gating to save-all without the cache would run 3 redundant full-tree scans × every player × every cadence.

### Task 2.1 — De-gate `Crash_save_all` to a true save-all snapshot
**File:** `src/objsave.cpp:1860-1875`. Remove the `PLR_FLAGGED(d->character, PLR_CRASH)` inclusion gate (line **1865**) so every `CON_PLYNG` non-NPC is saved each cadence. Pass **`notify=0` for all** players (drop the mortal `notify=1` branch at line **1868** — a silent full snapshot must not spam "Saving X." every interval). Add **explicit skip-and-log** when a player can't be saved (never abort the whole batch — one broken player must not stall everyone's snapshot). **Model on `Emergency_save` (`objsave.cpp:1877-1886`)**, which already iterates `CON_PLYNG` non-NPC un-gated with `Crash_crashsave` + `save_char(..., 0)`.
**Cost note (post-Phase-1):** every player now runs `Crash_crashsave` each cadence; `Crash_crashsave`/`idlesave`/`rentsave` (`objsave.cpp:1283`/`1326`/`1377`) still **truncate-in-place** the legacy `lib/plrobjs` file (`fopen "wb"`/`"w+b"`) and then **read it back** and push it through the account-native atomic writer via `refresh_account_backed_object_file` (`objsave.cpp:1322`/`1372`/`1422`) — a double write + read per player. Consider whether the legacy `plrobjs` write can be skipped for account-native chars (and note the truncate-in-place torn-write window) when sizing the snapshot.

### Task 2.2 — Neutralize the now-persisted `PLR_CRASH` flag on load
**Files:** `src/character_json.cpp:34` (the `{ "crash", PLR_CRASH }` entry in `kPlayerFlags`), decode at `:1587`/`:1641`. New on this branch, `PLR_CRASH` round-trips through the character JSON into `specials2.act`, so a stale persisted crash bit reloads verbatim. With the snapshot de-gated, the bit no longer controls inclusion, but a stale bit could still leak into other logic — **strip/normalize `PLR_CRASH` on load** (exclude it from `kPlayerFlags` serialization, or clear it in the load path). Re-evaluate the now-redundant double clear at `objsave.cpp:1323` and `1871`.

### Task 2.3 — Anti-rollback hooks
- **2.3a — exploit→save (the unifying hook).** `write_exploits` (`src/db.cpp:4082-4098`) is confirmed the **single** exploit sink (only `add_exploit_record` `db.cpp:4360-4498` calls it). Add `save_char(ch, NOWHERE, 0)` **after a confirmed write** — i.e. conditioned on `write_exploit_record_for_character` (`db.cpp:4257`) returning true (after line **~4097**), **not** on the orphaned-account early-return guard (`db.cpp:4089`) nor on a logged write failure (`~4095`). This one hook crash-proofs all 12 exploit record types (PK→killer, death/level/stat/birth/etc.→victim).
- **2.3b — angel-reroll direct save.** `resetter` proc, reroll branch `src/spec_pro.cpp:2662-2684` (`roll_abilities` at `2678`, `rerolls += 1` at `2679`). It records **no** exploit and does **no** save, so the hook above won't cover it. Add a direct `save_char(ch, NOWHERE, 0)` after the increment (~line **2682**) to close the crash-to-reroll exploit.
- **2.3c — remove the 10% kill-XP save.** `group_gain` `src/fight.cpp:1304-1307` (`if (number(0,9)==0) save_char(character, NOWHERE, 1)`). Remove it — **bundled with Task 2.1**, since removing it alone *reduces* XP-persistence frequency; the save-all snapshot replaces this coverage. (Also note it currently uses `notify=1` — visible "Saving X." — so its removal is a user-visible change too.)
- **Keep the anchors:** the death save in `raw_kill` (`fight.cpp:915` + `Crash_crashsave` at `916`) and the level-up save in `advance_level` (`profs.cpp:455-456`). Honor `should_defer_account_backed_birth_persistence` (`profs.cpp:40-57`) — it intentionally suppresses `advance_level`'s save + `EXPLOIT_LEVEL`/`EXPLOIT_BIRTH` writes during account-backed character creation (`CON_QSEX`…`CON_LATIN`); confirm the deferred level-1/6 anti-rollback is flushed once birth completes (the account-creation flow must persist it).
- `save_char` (`db.cpp:3023-3116`, decl `db.h:108`) — **no signature change**; all hooks rely on its `IS_NPC`/`!ch->desc` guard, `NOWHERE`=current-room semantics, and account-native routing.

### Task 2.4 — Document the deliberate `CON_LINKLS` exclusion
**File:** `src/comm.cpp:1912-1913` (the commented-out `// d->character->desc = 0;` + `connected = CON_LINKLS`). Link-dead players are already excluded from the snapshot (they're `CON_LINKLS`, not `CON_PLYNG`, and were flushed by the `save_char` at `comm.cpp:1906`). Add an in-code comment stating this is **deliberate** so a future reader doesn't "fix" it — and note the dependency: the exclusion is a `CON_PLYNG`-filter choice, not enforced by a null `desc`; if that detach is ever re-enabled, revisit.

### Task 2.5 — Lower the cadence (last)
**File:** `src/config.cpp:41` (`autosave_time = 240`). Reduce toward the source's 30s, **sized against the post-cache per-player cost** measured in Task 1.4 and the de-gated per-player object-save cost (Task 2.1 note): `per-player µs × max connected players` must sit comfortably inside the heartbeat budget (single-threaded, 250 ms/pulse at `TICS_PER_SECOND=4`). The scheduler (`comm.cpp:1052`) needs no change — it already reads `autosave_time` seconds with a 15s floor. Update the comment block at `comm.cpp:1049-1051` (it currently says `Crash_save_all` is "unchanged here" — no longer true).

---

## 4. Sequencing & verification

1. **Phase 1 (cache) → re-measure with `savebench` → record.** This both delivers the win and produces the per-player cost that sizes Phase 2's cadence. Gate Phase 2 on the cache landing.
2. **Phase 2 (snapshot + anti-rollback), then cadence reduction last.**

**Verification per change:**
- **Unit (CI-able):** new `account_cache_tests.cpp`; the existing `CrashsaveSchedule.*`/`PlayerFinalize.*`/`SaveBenchmark.*` plus the affected account suites by name. Run via `scripts/rots-docker.sh test '--gtest_filter=...'`.
- **Pipeline cost:** `savebench 500` before/after Phase 1 (S2/L1 should collapse); again after Phase 2's object-save changes.
- **Snapshot behavior** (`Crash_save_all` de-gate, anti-rollback hooks, link-dead exclusion) is coupled to live `descriptor_list`/global state — **validate on a running server** (the local i386 container + the `drelibench`/account fixture, or the dev server). Confirm: all connected players saved each cadence (not just dirty ones), no "Saving X." spam, an exploit/reroll persists immediately, and a stale persisted `PLR_CRASH` doesn't misbehave.
- Prefer a **test instance** over the live server for the snapshot/anti-rollback behavioral checks.

---

## 5. Risks & gotchas (carried forward)

- **Cache + `root_directory`:** it's a parameter; tests use temp dirs. Key on `(root, name)` and reset between tests, or `#ifdef TESTING`-disable — otherwise cross-test contamination.
- **Cache invalidation must refresh the OLD link set** (relink-to-different-account, delete) — invalidate by both old and new names, which `write_account_file`'s hook can do by diffing/refreshing the persisted account's `character_links`.
- **`write_exploits` save placement:** after the confirmed write only (two no-write exit paths to avoid: the `db.cpp:4089` orphan guard and a logged write failure).
- **Removing the 10% XP save without the snapshot is unsafe** — keep 2.3c bundled with 2.1.
- **Per-player object double-write under save-all** (Task 2.1 note) is the main new cost; the cache (Phase 1) addresses the *account-read* half, not the *object* write — watch it when sizing cadence.
- **EOL discipline:** `config.cpp`/`db.h` are CRLF; verify endings before editing and after (a flipped file is a defect).
- **Docker i386 is authoritative; the 32-bit suite has ~163 pre-existing reds** — gate per-test.
