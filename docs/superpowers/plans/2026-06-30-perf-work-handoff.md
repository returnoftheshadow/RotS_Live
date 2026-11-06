# Handoff — Character-Persistence Performance Work (account cache + parallel JSON)

> **Purpose.** Preserve the performance-optimization thread so a future session can resume it cleanly. The implementation is **done, committed, measured, and verified**; what remains is **adoption + re-measure** (§2). The original requester is concurrently pivoting to the *other* part of this branch (Phase 2 — consistent-snapshot autosave); see §6 for how the two threads interlock.
>
> **Branch:** `feature/savebench-port` · **Snapshot date:** 2026-06-30 · working tree clean.

---

## 0. Read these first (in order)

1. `docs/superpowers/specs/2026-06-30-account-cache-and-json-perf-design.md` — the design + the account-caching trade-off analysis + the JSON code analysis.
2. `docs/superpowers/plans/2026-06-30-account-cache-and-json-perf.md` — the 9-task implementation plan (top banner = implemented-status + the 3 deviations).
3. `docs/superpowers/specs/2026-06-30-account-cache-and-json-perf-results.md` — the standalone results write-up (benchmark numbers, verified safety, rationale).
4. `docs/superpowers/specs/2026-06-29-savebench-pipeline-performance-findings.md` — the original (pre-optimization) measurements.
5. `docs/superpowers/plans/2026-06-29-followup-account-perf-and-consistent-autosave.md` — the parent plan; **Phase 1 = this perf work, Phase 2 = the autosave rework (§3 there)**.

---

## 1. What is DONE (committed on `feature/savebench-port`)

Three optimizations, each built as a **parallel function beside the untouched original**, profiled head-to-head, and pinned by an equivalence test.

> **UPDATE 2026-07-01:** the **account cache is now ADOPTED / live** (commit `c8f8877`) — `read_account_file` / `find_linked_character_owner_account` delegate to it (enabled at `boot_db`; default-OFF so the test binary is unchanged, proven by a stash-diff), flushed on every `write_account_file`. The **JSON serialize/deserialize v2 winners remain switched OFF** (v1 is still the live path; §2b below still applies to them only). So the branch is no longer fully behavior-neutral: the account-read path is cached in the live server.

**Commits (this work, oldest→newest):**

```
5f07e9a docs(perf): design spec
cd58d2a docs(perf): g++ 9.4.0 compiler floor + integer-charconv-only guardrail
e3cfe9c docs(perf): implementation plan (9 TDD tasks)
3d904cf feat(account_cache): memoized read_account_file_cached + negative-cached owner resolver
bae643f feat(json): JsonReaderV2 + append_escaped_json_string
cbc3fd7 feat(character_json): deserialize v2a/v2b (memoized lookups + templated parsers + JsonReaderV2)
d241671 feat(character_json): serialize v2a/v2b (reserve+to_chars; escape-fastpath+cached keys)
5f3a304 feat(savebench): opt-in compare report + 'savebench compare N' subcommand
4d29826 docs(perf): mark plan complete
e395804 docs(perf): standalone results write-up
```

**New/changed source:** `src/account_cache.{h,cpp}` (new), `src/json_utils.{h,cpp}` (added `JsonReaderV2` + `append_escaped_json_string`), `src/character_json.{h,cpp}` (added `serialize_*_v2a/_v2b`, `deserialize_*_v2a/_v2b`, memoized `*_index_for_key_memoized`, cached `*_key_for_index_cached`, templated the nested deserialize parsers), `src/save_benchmark.{h,cpp}` (`compare` out-param), `src/savebench.cpp` (`savebench compare N` subcommand), tests `src/tests/{account_cache_tests,json_perf_tests}.cpp` (new) + `save_benchmark_tests.cpp`, `src/CMakeLists.txt`.

**Verification status:** 64/65 relevant tests pass (`AccountCache.*`, `JsonPerf.*`, `SaveBenchmark.*`, `CharacterJson.*`); the lone red is the **pre-existing** 32-bit baseline `JsonUtils.RejectsIntegersOutsideIntRange` (unrelated; v1 code only). The `ageland` server **boots clean** with all changes. Live `savebench compare 500` ran with no errors.

**Measured wins (i386/QEMU, avg µs/char — relative is the signal):**
- account `read_account_file`: **1192 → ~5 (warm)** ≈ **−99%** (and `save_char` pays this 3×/save).
- deserialize: **3224 → 2060 (v2a, memoized lookups) → 1861 (v2b, +tokenizer)** ≈ **−42%**.
- serialize: **572 → 504 (v2a) → 427 (v2b)** ≈ **−25%**.
- **No sustained regression** — only one-time warm-up (cold cache miss + lazy table builds); see results doc §3.2.

---

## 2. The FOLLOW-UP work (resume here) — Adoption + re-measure

The optimizations are proven but **switched off**. Adoption is a new, separately-reviewable branch (off `feature/savebench-port`, or off `account-management` after this merges). Confirm scope with the user before starting; the pieces:

### 2a. Adopt the account cache (biggest win)
- Route the live resolvers through the cached path. Primary sites (re-grep — lines shift): `save_char`'s three scans (`src/db.cpp` ~`3072` owner-resolve, ~`3076` `account_character_file_exists`, ~`3079/3091` `write_account_character_file`); the boot index loop (`db.cpp` ~`663-667`); the `read_account_file` callers in `account_management_assets.cpp` / `account_management_identity.cpp` / `interpre.cpp` (full caller map in the design spec §4 and the cache-surface analysis).
- **Add the invalidation hook** at the single `account.json` writer chokepoint — `write_account_file` (`src/account_management_storage.cpp` ~`158`/`212`). On a successful write, refresh/drop the cached account entry **and the owner entries for both the OLD and NEW `character_links`** (the relink/delete trap — read the prior cache entry before overwriting). The cache module already declares the intended shape; `account_cache::invalidate_account(root, written_account)` was specified but **not built** (no live caller used the cache, so it was out of scope). Build + unit-test it as part of adoption.
- Cache is keyed on `(root, name)` and has `clear()`. **Single-threaded — no locking.**

### 2b. Adopt the JSON winners
- **Easiest, highest-confidence:** the memoized skill/talk lookups are **byte-identical** and the name tables are immutable post-boot, so they can simply **replace the slow linear scans in v1 in-place** (benefits *all* callers, not just the JSON path) — no risk. Alternatively, switch the live serialize/deserialize callers to `serialize_character_to_json_v2b` / `deserialize_character_from_json_v2b`.
- Either way, the equivalence tests in `json_perf_tests.cpp` already pin correctness; keep them.

### 2c. Re-measure
- Re-run `savebench compare 500` after adoption to confirm the end-to-end projections (results doc §3.3): LOAD ≈ halved, SAVE substantially lower. Ideally also on **native i386** (the live server, not QEMU) for production-accurate absolute numbers. Append results to the findings doc.

---

## 3. Build / test / run (environment — IMPORTANT, non-obvious)

- **Docker i386 is the authoritative build.** Host clang/IDE diagnostics about `std::filesystem`/`std::from_chars`/`std::to_chars`/`gtest`/`MAX`/`MIN`/POSIX are **false positives** — ignore them.
- **Test:** `scripts/rots-docker.sh test '--gtest_filter=Suite.Name'` — **quote the filter** (`*` globs in the shell). It builds the `ageland_tests` CMake target and runs `./bin/tests`. `ctest` finds 0 tests under the container's cmake; the wrapper runs the binary directly.
- **The CMake build (`src/CMakeLists.txt`) is authoritative; the GNU `src/Makefile` is STALE** — it does NOT list `account_cache.cpp`, `save_benchmark.cpp`, `savebench.cpp`, etc., so **`make all` will not link this branch**. `scripts/rots-docker.sh boot` uses `make all` and will therefore fail. **Boot via CMake instead:**
  ```
  docker compose run --rm -T --service-ports --name rots_server rots bash -lc 'cd /rots && cmake -S src -B build && cmake --build build --target ageland -j16 && exec ./bin/ageland'
  ```
  (port 1024 is published; `lib/world/` must exist — it does. Stop with `docker rm -f rots_server`.)
- **Profile in-game:** `telnet localhost 1024`, log in by **account email `david.gurley+1@gmail.com` / password `Savebench1`**, play character **`drelibench`** (a disposable L100 clone, files gitignored), then `savebench compare 500`. The full report mirrors to the MUD syslog (stdout), so capturing the server's stdout captures it.
- **32-bit baseline:** the container builds 32-bit; **~163 pre-existing tests fail on 32-bit-vs-64-bit expectation diffs in untouched suites — gate PER-TEST by name, never per-suite.** Known relevant red: `JsonUtils.RejectsIntegersOutsideIntRange` (on i386 `long`==`int`, so the range check can't trigger — pre-existing, not ours).
- **Line endings / formatting:** `.claude/.no-autoformat` is present → clang-format does NOT run on edit. Hand-apply Allman braces / 4-space / mandatory braces / LF. New files are LF.

---

## 4. Hard-won learnings this session (so they aren't re-discovered)

- **The account directory scan does NOT resolve under QEMU i386 offline.** The `AccountManagement.*` on-disk suites are baseline reds for this reason. → The cache tests use a **`account_cache::set_backing_resolvers_for_testing` DI seam** with counting fakes (proves hit/miss/negative/keying by call count) instead of a disk fixture. Don't "fix" those baseline reds.
- **The deserialize bottleneck was NOT the tokenizer.** It was `skill_index_for_key` / `talk_index_for_key` — an O(256) linear scan that **rebuilt a slugified string per iteration** (~25k transient strings/load). Memoizing it (v2a) is the bulk of the −42%; the faster tokenizer (v2b) adds only ~6 points. A "swap in a fast JSON library" instinct would have optimized the wrong thing.
- **Skill-slug collisions are real.** The memoized map is built **first-index-wins** (`emplace`) to match the linear scan exactly. A character with *all* skills populated serializes **duplicate keys that v1 itself rejects** — so the deserialize equivalence tests compare **outcomes** (success-with-identical-struct OR identical rejection), which is the correct, stronger gate. Don't assume all-skills round-trips.
- **`serialize_*_v2b` reuses v1's `collect_*` intermediates** (identical order/filtering → byte-equality) rather than re-implementing inline iteration — chosen to guarantee byte-identity.
- **Compiler floor is g++ 9.4.0** (live), build image is g++ 10. **Integer `from_chars`/`to_chars` only** (GCC 8.1+, header-inline); never floating-point charconv (GCC 11). See [[rots-compiler-floor-gpp94]].
- **All new functions are portable standard C++** per `CLAUDE.local.md` ([[portable-std-cpp-new-functions]]); the cache memoizes the POSIX resolvers without rewriting them.

---

## 5. Relationship to Phase 2 — the "other part of the branch port" (IMPORTANT)

The parent plan (`2026-06-29-followup-account-perf-and-consistent-autosave.md`) sequences **Phase 1 (this cache work) → Phase 2 (consistent-snapshot autosave, §3 there: de-gate `Crash_save_all` to save *all* connected players each cadence, anti-rollback hooks, lower the cadence)**.

**The dependency that must not be lost:** Phase 2's save-all snapshot multiplies per-player save cost by the connected-player count every cadence — and each save currently runs the **3× full-tree account scan**. So **the cache must be *adopted* (not merely profiled) before Phase 2 lowers the cadence / de-gates `Crash_save_all`**, or the snapshot will run `3 × O(accounts) scans × every player × every cadence`. 

If the requester proceeds with Phase 2 first, that is fine for the **behavior** parts (de-gate, anti-rollback hooks, `PLR_CRASH` neutralization) — but **Task 2.5 "lower the cadence" is gated on the cache adoption (§2a here) landing first**, and the cost sizing in Task 2.5 should use post-adoption per-player numbers (re-measure, §2c). Cross-reference this handoff from the Phase 2 work.

---

## 6. One-line state for the resuming session

> Perf optimizations (account cache + JSON v2a/v2b) are **built, tested, profiled, and committed but switched OFF** on `feature/savebench-port`. Resume at **§2 (adopt + add the `write_account_file` invalidation hook + re-measure)**. Coordinate with Phase 2: cache adoption must precede any cadence reduction (§5).
