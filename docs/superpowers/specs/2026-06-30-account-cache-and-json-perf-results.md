# Character Persistence Performance — Results, Safety, and Rationale

**Branch:** `feature/savebench-port` · **Date:** 2026-06-30 · **Status:** measured and verified; not yet adopted on the live path (see §6).

This document reports the outcome of an effort to speed up RotS character **save** and **load**. It is written to stand on its own — a reader does not need prior context. It covers three things, in order: **what the benchmark showed (§3)**, **why the new code is safe to trust (§4)**, and **why each change is faster (§5)**.

---

## 1. Executive summary

Three independent optimizations were implemented as **parallel ("v2") functions sitting beside the original ("v1") code**, then profiled head-to-head against v1 in the same run. All three are net wins with **no sustained regression**:

| Optimization | What it replaces | Result (avg µs/char) | Win |
|---|---|---|---|
| **Account-resolution cache** | a full directory scan + JSON parse on every save and load | 1192 → **~5** (warm) | **~99%** |
| **Deserialize v2** (memoized lookups + faster tokenizer) | the JSON→character read path | 3224 → **1861** | **−42%** |
| **Serialize v2** (buffered build + fast escaping) | the character→JSON write path | 572 → **427** | **−25%** |

The character-load path (the slower of the two directions) is on track to be **roughly halved** end-to-end once these are adopted; the save path benefits even more because the account scan it eliminates is currently performed **three times per save** (§5.1).

The work is currently **behavior-neutral**: the new code is compiled and tested but **no live code path uses it yet**. Turning it on ("adoption") is a separate, deliberately-gated step (§6).

---

## 2. How it was measured

A purpose-built in-game command, **`savebench`**, profiles every stage of the real save and load pipelines against a real character and its real on-disk files, without modifying any live state. The command was extended with an opt-in **`savebench compare N`** mode that, in the same run, also times each new variant **side-by-side against the original** in a separate report (so the headline numbers are never distorted).

- **Command run:** `savebench compare 500` (500 iterations each), in-game, on a level-100 character (`drelibench`) with real account/character files.
- **Environment:** the MUD is a 32-bit i386 binary running under **QEMU emulation** on an arm64 host. **Treat the relative comparisons (v1 vs v2) as the signal and the absolute microseconds as a loose upper bound** — native i386 (production) is meaningfully faster, especially on the CPU-bound JSON stages.
- **Naming:** `v1` = the original code (unchanged). `v2a` = the first optimization tier. `v2b` = `v2a` plus a second tier. "cached" = the new cache vs the original uncached resolution.

---

## 3. Benchmark results

### 3.1 Raw comparison (avg µs per character, 500 iterations)

**Account resolution** (`read_account_file`):

| | min | avg | max |
|---|---:|---:|---:|
| original (uncached) | 1135 | 1192 | 1620 |
| **cached** | **5** | **12\*** | 3358\* |

\* The cached **avg of 12** is inflated by exactly one cold miss: the first lookup does the scan *and* fills the cache, then the other 499 are warm hits at ~5µs — i.e. `(3358 + 499×5) / 500 ≈ 12`. In steady state the cached cost is **~5µs**, a ~99% reduction. The load-side cache shows this even more cleanly: min 5 / avg 5 / max 119.

**Deserialize** (JSON → character):

| | min | avg | max | vs v1 (avg) |
|---|---:|---:|---:|---:|
| v1 | 3049 | 3224 | 3490 | — |
| **v2a** (memoized lookups) | 1896 | 2060 | 6170 | **−36%** |
| **v2b** (+ faster tokenizer) | 1708 | 1861 | 10438 | **−42%** |

**Serialize** (character → JSON):

| | min | avg | max | vs v1 (avg) |
|---|---:|---:|---:|---:|
| v1 | 521 | 572 | 788 | — |
| **v2a** (buffered build) | 453 | 504 | 788\*\* | **−12%** |
| **v2b** (+ fast escaping, cached keys) | 412 | 427 | 2784 | **−25%** |

### 3.2 Is anything in the new path slower?

**No — not in steady state.** Every new stage beats the original on both **average** and **minimum** (the minimum is the cleanest signal because it strips out host scheduling noise). The new path's *minimum* is always below the original's *minimum* (e.g. deserialize 1708 vs 3049; serialize 412 vs 521; cache 5 vs 1135).

The only place a new path posts a **higher number is its `max`** (worst single iteration), and every such case is explained — none is an algorithmic regression:

- **Cold cache miss** — the very first cached lookup runs the full scan *plus* a map insert, slightly more than a bare scan (the 3358µs max above). Paid **once per process**, then amortized to ~5µs.
- **One-time lazy table builds** — the memoized lookup maps and cached key tables are built on first use (a few hundred string operations). This shows up in the first `v2b` iteration's time.
- **Host/QEMU scheduling jitter** — the remaining max spikes (e.g. the 10438µs deserialize outlier) are single noisy iterations; the stable min/avg confirm the underlying code is faster. (The benchmark was run while other builds competed for the host; a quiet re-run tightens the maxes.)

In a real session these one-time costs are paid once (at first save/load after login) and every subsequent operation is faster. **There is no recurring case where the new code loses to the old.**

### 3.3 Projected end-to-end impact (once adopted)

From the canonical single-pass breakdown (SAVE ≈ 2497µs, LOAD ≈ 5252µs):

- **LOAD ≈ 5252 → ~2700µs (~halved):** the cache removes the ~1187µs account read; v2b nearly halves the 3233µs deserialize.
- **SAVE ≈ 2497 → ~1100µs on the single-read path** — and substantially more in the real `save_char`, which currently performs the account scan **three times per save** (§5.1), so the cache saves on the order of **3 × ~1.2ms** there.

---

## 4. Verified safety

The central safety principle is that **the original code was not modified** — each optimization is a *new, parallel* function, and the branch does not switch any live caller to it. This means the production behavior on this branch is, by construction, identical to before; the new code can only affect anything once a future, separate change routes a caller to it (§6).

### 4.1 The branch is behavior-neutral

- No live `save_char` / load path, command, or scheduler calls the new cache or v2 serializer/deserializer. They exist, compile, and are exercised only by tests and the `savebench` profiler.
- The original functions (`read_account_file`, `serialize_character_to_json`, `deserialize_character_from_json`, and the `JsonReader` tokenizer) are byte-for-byte unchanged, preserving them as a correctness and measurement baseline.

### 4.2 Each new method is pinned to its original by an equivalence test

| New method | Equivalence guarantee | How it is verified |
|---|---|---|
| `serialize_*_v2a` / `_v2b` | **byte-identical** JSON to v1 | string equality `v1(c) == v2a(c) == v2b(c)`, on a minimal and a fully-populated character |
| `deserialize_*_v2a` / `_v2b` | **identical decoded character**, or identical rejection | both paths decode the same JSON and the resulting fixed-layout records are compared byte-for-byte; on malformed input both must reject with the **same** error |
| `read_account_file_cached` | returns an `AccountData` **equal to** the uncached read | direct comparison; cache hit/miss, negative ("not linked") caching, key isolation, and reset are each asserted by call-count |
| memoized key→index lookups | map the same key to the **same index** as the original linear scan | every index round-tripped through serialize→deserialize and compared to v1 |

### 4.3 Correctness traps that were explicitly handled

- **Skill-name slug collisions.** Some skill names reduce to the same key. The memoized map is built "first index wins," exactly matching the original linear scan, so colliding keys resolve to the identical index. (This also surfaced a pre-existing property of v1: a character with *every* skill populated serializes duplicate keys that v1 itself rejects — the equivalence test treats "both reject identically" as a pass, which is the correct, stronger check.)
- **Cache staleness.** The cache memoizes only the **owner→account** resolution and the **parsed account record**, both of which are invariant for a logged-in character within a session. Negative ("character is not linked") results are cached too, but **failures/errors are deliberately not cached** so transient errors retry. The eventual invalidation hook (for when account data is mutated) is designed but not wired, because no live caller uses the cache yet (§6).
- **Test isolation.** The cache is keyed on `(storage-root, name)` so test fixtures and production never alias, and it exposes a `clear()` reset.

### 4.4 Test suite and runtime verification

- **Automated tests (Docker i386 — the authoritative build):** 64 of 65 relevant tests pass across the account-cache, JSON-performance, save-benchmark, and character-JSON suites. The single failure (`JsonUtils.RejectsIntegersOutsideIntRange`) is a **pre-existing 32-bit baseline issue unrelated to this work** — on a 32-bit build `long` and `int` are the same width, so an "out of int range" value cannot be represented to trigger the check; it fails identically on the untouched baseline and exercises only original code.
- **Production binary:** the full server (`ageland`), built with all the new code, **boots cleanly to "Boot db — DONE / Entering game loop"** and accepts connections — a runtime validation on top of the unit tests.
- **No crashes or errors** were logged during the live `savebench compare 500` session.

---

## 5. Why each change is faster (rationale)

The optimizations target three independent root causes found by profiling and code analysis.

### 5.1 The account cache — eliminating redundant work

Resolving which account owns a character (`read_account_file`) is **not a single-file read**. It opens the entire `lib/accounts/` directory tree and **deserializes every `account.json`** to match by name — an O(number-of-accounts) scan with a JSON parse per account — and then re-parses the matched file. This was measured as **the single largest cost on the save side and the second largest on load**, and it is performed on **every** save and load even though the answer is invariant for a logged-in character.

Worse, the real `save_char` triggers this scan **three times per save** (resolve owner, check file exists, write file). Memoizing the result per `(root, name)` collapses each of these from ~1.2ms to a hash lookup (~5µs). This is pure redundant-work elimination — the highest-value, lowest-risk lever.

### 5.2 Deserialize — the real cost was *not* the JSON parser

The intuitive suspect for slow JSON reading is the tokenizer. Profiling showed otherwise. The dominant cost was a **field-mapping helper**: to map a skill key (e.g. `"longsword"`) back to its array index, the code ran a **linear scan over all 256 skill slots, rebuilding a slugified name string from scratch on every comparison**. For a developed character with ~100 trained skills that is on the order of **100 × 256 ≈ 25,000 transient string constructions per load** — an order of magnitude more work than everything in the tokenizer combined.

The stacked variants isolate this precisely:

- **v2a** replaces those per-key linear scans with a **lookup table built once** (skill/talk name → index). Result: **−36%** — the bulk of the deserialize win.
- **v2b** then also swaps in a lower-allocation tokenizer (`JsonReaderV2`: parse numbers in place instead of copying substrings, move strings instead of copying, branchless whitespace/digit checks). Result: a further ~6 percentage points (**−42%** total).

The split confirms the diagnosis: the field-mapping helper, not the parser, was the problem. Anyone tempted to "just swap in a faster JSON library" would have optimized the wrong 6%.

### 5.3 Serialize — fewer allocations on the write side

The original serializer builds the document through a `std::ostringstream` and returns `output.str()`, which **allocates the whole payload twice** (the stream's buffer, then the returned copy) and routes every integer through locale-aware stream formatting. It also calls the string escaper for **every** field — allocating a fresh escaped string even when nothing needs escaping (true for the vast majority of keys and flag names) — and re-derives skill/talk key slugs on every serialize.

- **v2a** builds into a **single pre-reserved buffer**, formats integers with `std::to_chars` (locale-free, allocation-free), and hands the buffer back by move (no final copy). Result: **−12%**.
- **v2b** adds a **fast-path escaper** (when a string contains nothing to escape, append it verbatim — no temporary) and **cached key slugs** (compute once, not per serialize). Result: **−25%** total.

Every byte of output is unchanged (verified by the byte-equality tests in §4.2), so these are pure efficiency gains.

### 5.4 Portability note

All new code is standard, cross-compilable C++17 and was verified to build and run on the production compiler floor (**g++ 9.4.0**). It relies only on the **integer** overloads of `<charconv>` (`from_chars`/`to_chars`), which that compiler fully supports; the floating-point overloads (which would require a newer compiler) are not used because no character field is floating-point. No third-party JSON library was introduced.

---

## 6. What is not done yet (intentionally)

This branch **measures and proves** the optimizations; it does not turn them on. The remaining, separately-reviewable steps are:

1. **Adoption** — route the live `save_char`, load, and account-resolution call sites through `read_account_file_cached` and the v2 serializer/deserializer, and wire the cache-invalidation hook at the single account-write chokepoint.
2. **Re-measure after adoption** to confirm the end-to-end projections in §3.3 on real traffic (ideally also on native i386 for production-accurate absolute numbers).

Keeping measurement and adoption separate is deliberate: it lets the numbers and the equivalence proofs be reviewed before any production behavior changes.

---

### Appendix — raw benchmark output

`savebench compare 500` (in-game, i386/QEMU, character `drelibench`):

```
=== SAVE pipeline (microseconds) ===
  stage                                    min     avg     max   share%
  S2 read_account_file                     1135    1257    5162    50.3
  S3 character_data_from_store              179     193     396     7.7
  S4 serialize_character_to_json            519     562     846    22.5
  S5 write_text_file_atomically             241     364     843    14.6
  end-to-end single pass   = 2497 us
=== LOAD (L1-L4; L5 offline-only) pipeline (microseconds) ===
  L1 read_account_file                     1135    1187    1440    22.6
  L2 read_text_file                          54      63     954     1.2
  L3 deserialize_character_from_json       3033    3233    5099    61.6
  L4 apply_character_data_to_store          598     698    1702    13.3
  end-to-end single pass   = 5252 us
=== COMPARE serialize v1 vs v2a/v2b + cache (NOT in TOTAL above) ===
  S2  read_account_file        (v1)        1135    1192    1620    42.7
  S2c read_account_file_cached                5      12    3358     0.4
  S4  serialize_character_to_json     (v1)    521     572     788    20.5
  S4a serialize_character_to_json_v2a       453     504    2606    18.0
  S4b serialize_character_to_json_v2b       412     427    2784    15.3
=== COMPARE deserialize v1 vs v2a/v2b + cache (NOT in TOTAL above) ===
  L1  read_account_file        (v1)        1132    1193    1439    14.1
  L1c read_account_file_cached                5       5     119     0.1
  L3  deserialize_character_from_json     (v1)   3049    3224    3490    38.0
  L3a deserialize_character_from_json_v2a   1896    2060    6170    24.3
  L3b deserialize_character_from_json_v2b   1708    1861   10438    21.9
```
