# Account-Resolution Cache + Parallel JSON Perf — Design (profile-before-adopt)

**Status:** Design — approved 2026-06-30, awaiting written-spec review before planning.
**Branch:** continues `feature/savebench-port` (start a new branch off it, or off `account-management` after that merges).
**Companion docs:** followup plan `docs/superpowers/plans/2026-06-29-followup-account-perf-and-consistent-autosave.md`; perf findings `docs/superpowers/specs/2026-06-29-savebench-pipeline-performance-findings.md`; savebench design `docs/superpowers/specs/2026-06-29-savebench-port-design.md`.

> All `file:line` references are current as of `feature/savebench-port` HEAD and **will shift as edits land** — re-grep the symbol before relying on a line number.

---

## 1. Goal

Deliver the Phase-1 performance work from the followup plan — account-persistence optimization and faster character JSON — as a set of **parallel implementations that are profiled head-to-head against today's code before anything replaces the live path.** Three workstreams (account cache, deserialize, serialize), each:

1. ships a **new function beside the untouched original** (no rewrite of `read_account_file`, `serialize_character_to_json`, or `deserialize_character_from_json`),
2. is wired into a **new savebench "COMPARE" report** for in-the-same-run A/B timing, and
3. is pinned by an **equivalence test** proving the new path produces the same result as the old.

This branch is **behavior-neutral**: the live save/load paths are not switched to the fast code here. **Adoption** — flipping live callers to the measured winners and adding cache invalidation — is a separate follow-up, gated on the numbers this branch produces. This mirrors the project's established pattern (gate the cost-bearing change on measured data).

### 1.1 Decisions taken (2026-06-30)

- **Hand-rolled v2 only — no third-party JSON library.** simdjson is disqualified (64-bit only) and glaze (needs C++20/23 reflection vs the image's g++10). Only yyjson would have been viable, but the existing parser streams **straight into the struct** (SAX-style, single pass), so a DOM library *adds* a tree-build step rather than removing one, and the bespoke field mapping (flags, named-integer `skills`/`talks`, `affects`, colors) is library-invariant. A tuned hand-rolled parser is the right target; no vendoring precedent exists in the build and none is introduced.
- **Stacked / incremental variants.** Each direction gets layered variants so savebench attributes the speedup to each change separately, rather than one bundled number.
- **Build the account cache here, profiled.** `read_account_file_cached` is added as a parallel path and A/B'd against the uncached O(N) scan in savebench. This supersedes the followup plan's Phase-1 cache *tasks* with a profile-first version; the followup plan's invalidation + live-routing design is carried forward into the (deferred) adoption step.

---

## 2. Measured context and the reframe

From `2026-06-29-savebench-pipeline-performance-findings.md` (live i386/QEMU, `savebench 500`): per character **SAVE ≈ 2.1 ms, LOAD ≈ 3.7 ms**. `read_account_file` is **57.5% of SAVE and 30.9% of LOAD** and is **redundant** (re-resolves an invariant mapping every operation). `deserialize_character_from_json` is **48.6% of LOAD**, ~5× the serialize side.

A code analysis (5-strand) refined two of these:

- **The dominant deserialize cost is not the tokenizer.** It is `skill_index_for_key` / `talk_index_for_key` / `color_index_for_key` (`character_json.cpp:654-679`): an **O(MAX_SKILLS=256) linear scan that rebuilds a slugified string char-by-char on every iteration** (`slugify_key`, `:613-633`, with locale `isalnum`/`tolower` per char), called once per skill key. A developed character (~100 non-zero skills) ⇒ **~25,600 transient string builds per load** — an order of magnitude more than every allocation in `JsonReader` combined. The skill/talk/color name tables are immutable after boot, so the lookups are pure functions recomputed from scratch every call. **The same `slugify` rebuild also runs on the serialize side** (`skill_key_for_index` via `collect_non_zero_named_values`, `:859-868`).
- **`read_account_file` is paid three times per `save_char`.** The account branch fires `find_linked_character_owner_account` (`db.cpp:3072`), `account_character_file_exists` → `read_account_file` (`:3076`), and `write_account_character_file` → `read_account_file` (`:3079`/`:3091`) — **≈ 3N+2 full `account.json` parses per save**, where N = number of accounts on disk (each resolver `opendir`/`readdir`s the whole `lib/accounts/` tree and deserializes every `account.json`, with **no early-exit on match**).

These two facts shape the variant design: the biggest deserialize win lives in the **field-mapping helpers**, not `JsonReader`, so the variants isolate it first.

---

## 3. Global constraints

- **Portable, cross-compilable standard C++ for all new functions** (`CLAUDE.local.md`): `<charconv>` (`std::from_chars`/`std::to_chars`), `<unordered_map>`, `<string_view>`, `<algorithm>` — all C++17 and g++ i386-safe. No POSIX in new code. The cache module **memoizes results of** the existing POSIX resolvers; it does not rewrite them (a mixed style at that seam is accepted, as the savebench design already established).
- **Preserve per-file line endings; all touched/new files here are LF** (`json_utils.*`, `character_json.*`, `save_benchmark.*`, `savebench.cpp`, the test files, and new modules). `.claude/.no-autoformat` is present, so clang-format does **not** run on edit — hand-apply Allman braces / 4-space / mandatory braces / LF; nothing auto-corrects.
- **Docker i386 `g++` (C++17) is authoritative.** Host clang/IDE diagnostics about `std::filesystem`/`std::from_chars`/`MAX`/`MIN`/`gtest` are false positives. Build/test via `scripts/rots-docker.sh test '--gtest_filter=...'` (quote filters). The 32-bit test build has ~163 pre-existing baseline reds in untouched suites — **gate per-test, not per-suite.**
- **The in-game command must never touch live state** — compare stages are pure in-memory transforms over already-read scratch buffers; never a live write, never `write_account_character_file` against a live path, never mutate `player_table[].ch_file`.
- **Class-scoped variables get a `//` comment** describing role + use (per the global C++ conventions) — applies to the new cache struct's members and `JsonReaderV2`'s state.

---

## 4. Workstream 1 — Account-resolution cache (parallel, profiled)

### 4.1 New module `src/account_cache.{h,cpp}` (LF, pure standard C++)

A single-threaded memoization layer (the MUD heartbeat is single-threaded — no locking). Keyed on `(root_directory, name)` because **live code passes `root="."`** (`kAccountStorageRoot`, `interpre.cpp:2283`) while **tests pass `TempDirectory` paths** — a temp-dir account and a `"."` account can share a name and must not alias.

Two maps:

- **`account_cache`**: account name → `{ resolved path (std::string), parsed AccountData }`. A hit returns a copy of the cached `AccountData` with **zero filesystem work**, collapsing the 57.5%/30.9% `read_account_file` cost. `AccountData` ≈ 1 KB/account on 32-bit (`account_management_types.h:20-51`), so even thousands of accounts is single-digit MB — caching the full parsed struct is the right call (path-only caching still pays one read+parse on every hit; see §4.4 trade-off).
- **`owner_cache`**: character name → owner account name, **including a negative "unlinked" sentinel**. `save_char` runs `find_linked_character_owner_account` on **every** save including non-account legacy characters, paying a full N-parse scan just to learn "still not linked" — the negative cache short-circuits that.

Public surface (illustrative; finalize in planning):

```cpp
namespace account_cache {
    // Memoized parallel of account::read_account_file. Hit -> copy cached AccountData (no FS);
    // miss -> delegate to account::read_account_file, populate, return.
    bool read_account_file_cached(const std::string& root, const std::string& account_name,
                                  account::AccountData* out, std::string* error);

    // Memoized parallel of find_linked_character_owner_account, with negative caching.
    bool find_linked_character_owner_account_cached(const std::string& root, const std::string& character_name,
                                                    std::string* owner_account_name, std::string* error);

    // Invalidate by (root, account_name) AND every character name in BOTH the prior and the
    // newly-written link sets (the relink/delete trap, §4.3). Takes the post-write AccountData;
    // reads the existing cache entry to recover the prior link set before overwriting.
    // (Used only at adoption — see §4.3. Declared now so the hook site is designed.)
    void invalidate_account(const std::string& root, const account::AccountData& written_account);

    void clear(); // test isolation — call in fixture SetUp().
}
```

### 4.2 Profiled, not adopted, in this branch

Only savebench calls the `_cached` paths. Over N iterations `time_stage` reports `min/avg/max`, so the warm-cache **hit** shows as `min` (the win) and the cold **miss** as `max` — the A/B captures both naturally. Live `save_char`/load paths are **unchanged**, so the branch adds no behavior and no risk.

### 4.3 Invalidation + live routing — designed here, deferred to adoption

Carried forward from followup-plan Tasks 1.2–1.4, now data-driven:

- **Single invalidation chokepoint confirmed:** `account.json` is serialized only by `serialize_account_to_json`, whose sole non-test caller is `write_account_file` (`account_management_storage.cpp:158`/`:212`). Every mutation (create/link/migrate/block/unblock/verify/reset/delete/email-verify) funnels through it; the analysis found **no production writer that bypasses it**. So one hook covers all in-process mutations.
- **The relink/delete trap:** the hook must invalidate by the **old** link set as well as the new — a character relinked to a different account, or removed from `character_links`, leaves a stale `owner_cache`/`account_cache` entry otherwise. Read the prior cache entry before overwriting so removed names are cleared; drop negative entries the new links now satisfy.
- **Adoption** = route live callers (`read_account_file` sites, `save_char`'s three scans, the boot index loop `db.cpp:663-667`) through the cached path **+** add the `write_account_file` hook **+** re-measure with `savebench`. Out of scope for this branch; gated on §4.2's measured win.

### 4.4 Trade-off summary (what to cache)

| Option | Memory | Removes of 57.5% SAVE / 30.9% LOAD | Note |
|---|---|---|---|
| char→owner name (+negative) | ~30–80 B/char | **~0% of `read_account_file`** | Keyed on char, not account — kills `save_char` scan #1 + legacy negative scans (hidden in "other"), but not S2/L1. Necessary, insufficient alone. |
| account→path | ~40–90 B/acct | most, not all | Skips the O(N) walk but still does 1 read + 1 parse per hit. |
| **full parsed `AccountData`** | **~1 KB/acct** | **essentially all — S2/L1 → ~0** | Chosen. Returns a copy, zero FS. Data changes on every `write_account_file`, but the single chokepoint makes invalidation complete. |

Chosen: **char→owner (incl. negative) AND full `AccountData`** — the two together cover all three of `save_char`'s scans.

---

## 5. Workstream 2 — Deserialize, stacked variants (v1 untouched)

`parse_named_integer_object` (`character_json.cpp:940`) already takes the index-lookup as a `std::function<int(const std::string&)>` parameter, so a v2 deserialize passes a **memoized** lookup while v1 keeps passing the slow scan — no duplication of the shared helper, v1 stays byte-for-byte intact.

- **`deserialize_character_from_json_v2a`** — v1 `JsonReader` + dispatch, but skill/talk/color lookups route through **new memoized helpers** backed by a lazy `static std::unordered_map<std::string,int>` (slug → index). **"First index wins" on slug collision** to exactly match the current linear scan (which returns the lowest matching index), preserving byte-identity. Preserves "unknown key → -1". Isolates the ~25,600-string-build win.
- **`deserialize_character_from_json_v2b`** — v2a **+ a drop-in `JsonReaderV2`** (identical public interface to `JsonReader`, `json_utils.h:12-38`):
  - `parse_long`: `std::from_chars(data+start, data+pos, value)` over the existing buffer — no `substr` copy, no `strtol` rescan/`errno`. Map `from_chars` `errc` to the same error strings; keep the `int` range check in `parse_integer`.
  - `parse_string`: `*value = std::move(parsed)` (not copy); `parse_string_array` `push_back(std::move(value))`; optional reserve/fast-path (assign the substring directly when no `\` present).
  - `skip_whitespace`/digit tests: branchless (`c==' '||'\t'||'\n'||'\r'`; `c>='0'&&c<='9'`) instead of locale `std::isspace`/`std::isdigit`. (Benign narrowing: JSON whitespace is exactly those four chars — `\v`/`\f` were never valid JSON whitespace.)
- **Deferred tier (only if v2b still shows the parser hot):** `std::string_view` keys (changes the `ObjectPropertyParser` signature + every lambda) and/or templating `parse_object`/`parse_array` to remove `std::function`. The analysis expects these won't matter once v2a lands; not built by default.

savebench compare shows **L3 (v1) / L3a (v2a) / L3b (v2b)** → the gain is attributed to memoization vs. tokenizer.

---

## 6. Workstream 3 — Serialize, stacked variants (byte-identical)

Every serialize optimization is **byte-output-preserving**, so variants are gated by exact string equality against v1.

- **`serialize_character_to_json_v2a`** — replace `std::ostringstream` (`character_json.cpp:1731`) + the final `.str()` whole-buffer copy (`:1866`) with a **`reserve()`d `std::string`** built by `append`/`+=`, integers via **`std::to_chars`** into one reusable `char buf[24]`. Removes the streambuf↔`.str()` double allocation and the per-field locale `num_put` cost. (Locale caveat: identical under the default "C" locale, the normal case; `to_chars` is locale-free by construction, so a non-C global numeric locale can't diverge it.)
- **`serialize_character_to_json_v2b`** — v2a **+**:
  - **`escape_json_string` fast-path**: scan once for `{ '"', '\\', <0x20 }`; if none, append the unescaped span directly (no temporary). Today it allocates + appends char-by-char + returns by value on **every** call (`json_utils.cpp:47-88`), ~100–200×/serialize on strings that are provably escape-free (slugged keys, flag names, color keys). Only `character_name`/`title`/`description` can actually need escaping.
  - **Cached key strings**: precompute the skill/talk/color key slugs once (shared with §5's memoized tables), so `skill_key_for_index` etc. don't re-`slugify` every serialize.
  - **Drop the `std::vector<NamedValue>` intermediates** for skills/talks/colors — iterate indices, skip zeros/defaults, append `"<cached_key>": <value>` directly (same iteration order + zero/default skipping ⇒ identical bytes).

savebench compare shows **S4 (v1) / S4a (v2a) / S4b (v2b)**.

---

## 7. Workstream 4 — savebench A/B wiring + equivalence gates

### 7.1 Separate compare report (don't corrupt the canonical breakdown)

Appending `S4b`/`L3b` into the existing `out->stages` would double-count in `finalize_shares` (`save_benchmark.cpp:45-62`): `stage_sum` inflates, `other_us` clamps to 0, every real stage's `share%` is understated, and the footer mislabels the v2 time as "instrumentation overhead." So:

- Add a **`PipelineReport* compare = nullptr`** out-param to `profile_save`/`profile_load` (`save_benchmark.h:40-43`/`:49-51`). The variant stages and a `TOTAL` that runs **exactly the compared variants once** live in a separate report, assembled **inside `save_benchmark.cpp`** (`time_stage`/`finalize_shares` are file-local). Because that report's `TOTAL ≈ Σ(its stages)`, `finalize_shares` reconciles and `format_report` needs no change. The **canonical SAVE/LOAD report stays byte-for-byte identical to today.**
- The **cache A/B** (`read_account_file` vs `read_account_file_cached`) goes in the same compare report as S2-compare / L1-compare stages.
- **In-game gate:** a **`savebench compare N`** subcommand (parse a leading `compare` token before the `atoi`, `savebench.cpp:24-28`) builds the compare report; plain `savebench N` passes `compare=nullptr` and stays cheap (the stacked variants run several extra transforms per iteration). The existing syslog-mirror + `page_string` handle the extra `\n`-delimited sections automatically.

### 7.2 Equivalence / correctness gates (offline gtest)

Reuse the suite's `memcmp(&char_file_u…)` oracle (`tests/save_benchmark_tests.cpp:170-173`) and `make_stored_character`/`read_character_file_directly` helpers; new cases in `save_benchmark_tests.cpp` and/or a new `src/tests/json_perf_tests.cpp`:

- **Serialize v2a/v2b:** `EXPECT_EQ(serialize_v1(cd), serialize_v2x(cd))` — exact `std::string` byte equality across light + heavy character tiers. (Catches a CRLF slip too — the payload must stay `\n`.)
- **Deserialize v2a/v2b:** from the *same* JSON bytes, run v1 and v2x, push both through `apply_character_data_to_store`, and `EXPECT_EQ(0, memcmp(&s1, &s2, sizeof(char_file_u)))`. Parameterize the test body over a deserialize function pointer so v1/v2 share the path.
- **Cache:** `read_account_file_cached` returns an `AccountData` equal to `read_account_file` (hit and miss), and `clear()` resets state between cases.

CI-pinned; timing remains informational (loosely bounded), so variance never red-flags CI.

---

## 8. New files / touch-points (for planning)

| Change | File |
|---|---|
| Account cache module | `src/account_cache.{h,cpp}` (new) |
| `JsonReaderV2` (new class beside `JsonReader`) | `src/json_utils.{h,cpp}` |
| `serialize_*_v2a/v2b`, `deserialize_*_v2a/v2b`, memoized key helpers | `src/character_json.{h,cpp}` (new fns; v1 untouched) |
| `compare` out-param + variant/cache stages | `src/save_benchmark.{h,cpp}` |
| `compare` subcommand + extra report prints | `src/savebench.cpp` |
| Equivalence + A/B tests | `src/tests/save_benchmark_tests.cpp`; `src/tests/json_perf_tests.cpp` (new) |
| Build wiring (`ROTS_SERVER_SOURCES` + `ROTS_TEST_SOURCES`) | `src/CMakeLists.txt` |

No production-binary dependency is added; nothing new ships in `ageland` until adoption.

---

## 9. Sequencing & verification

1. **Cache module + cached reads + savebench cache A/B** (behavior-neutral) → `savebench compare 500` on `drelibench` → record.
2. **JSON v2a/v2b** (serialize + deserialize) + A/B wiring + equivalence gtests → `savebench compare 500` → record.
3. **Record numbers** in `2026-06-29-savebench-pipeline-performance-findings.md` (append a Phase-1 results section).
4. **Adoption (separate follow-up, gated):** route live callers to the winners; add the `write_account_file` invalidation hook; re-measure. The memoized lookups are byte-identical and benefit **all** callers — an obvious in-place adoption for v1 once profiled.

**Verification per change:** new/affected gtests by name (`SaveBenchmark.*`, the new `JsonPerf.*`, `AccountManagement.*`, `DbLoader.*` — note which are 32-bit-baseline reds) via the Docker wrapper; the `savebench compare` A/B for pipeline cost.

---

## 10. Risks & gotchas

- **Slug-collision order:** the memoized maps must replicate "lowest index wins" or serialize/deserialize byte-identity breaks. Build with insert-if-absent.
- **Immutable-table assumption:** cached keys/maps assume skill/talk/color name tables are fixed after boot (true today). If a table could reload mid-run, the caches need invalidation — note it where the tables are defined.
- **`from_chars` span:** the scanner already restricts the span to `[-]?digits`, matching `from_chars` (which rejects leading `+`/whitespace) — but verify the integer overload is exercised, not the float one.
- **Cache cross-test bleed:** key on `(root, name)` and `clear()` in fixture `SetUp()`; any test that seeds `account.json` directly on disk (bypassing `write_account_file`) must `clear()` after seeding.
- **Compare-report "other" label:** `finalize_shares` hardcodes `"other (validate/mkdir/path/owner/index)"`; on a compare report that prints ~0/~0% — harmless, optionally parameterize the label.
- **EOL + no-autoformat:** all new files LF; hand-apply formatting (nothing auto-fixes); a CRLF slip in serialized output is caught by the `EXPECT_EQ` byte test.
- **Sandbox:** compare stages are in-memory only; never add a v2 *write* stage that targets a live path (a v2 atomic-write profile, if ever added, must use the scratch path like S5).
- **Docker i386 authoritative; ~163 pre-existing 32-bit reds** — gate per-test.

---

## 11. Key file:line index (re-grep before use)

- Parser: `src/json_utils.cpp` — `parse_long` substr/strtol `:303-306`; `parse_string` build/copy `:160`,`:164`,`:230`; `parse_string_array` copy `:328`; `skip_whitespace`/`isspace` `:448-452`; `escape_json_string` `:47-88`. `JsonReader` interface `src/json_utils.h:12-38`.
- Field-mapping hotspot: `src/character_json.cpp` — `slugify_key` `:613-633`; `skill_key_for_index` `:642`; `skill_index_for_key` O(256) scan `:663-670`; `talk_index_for_key` `:654`; `color_index_for_key` `:672`; `parse_named_integer_object` (lookup is a param) `:940`; `collect_non_zero_named_values` `:859-868`. Serialize `:1729-1867` (ostringstream `:1731`, `.str()` `:1866`); deserialize `:1869+` (root 19-key chain `:1900-1937`).
- Account resolution: `src/account_management.cpp` — `find_account_file_path_by_account_name` `:628` (no early-exit `:677-704`), `find_character_owner_account` `:720`. `src/account_management_storage.cpp` — `read_account_file` `:244`; `write_account_file` (sole `account.json` writer) `:158`/`:212`. `AccountData` `src/account_management_types.h:20-51`. `save_char` 3 scans `src/db.cpp:3072`/`:3076`/`:3079`. `kAccountStorageRoot="."` `src/interpre.cpp:2283`.
- savebench: `src/save_benchmark.{h,cpp}` — `time_stage`/`finalize_shares` `:18-62`, `profile_save` compare site after `:100`, `profile_load` after `:165`, `format_report` `:182-215`. `src/savebench.cpp` subcommand gate `:24-28`, prints `:45-60`. gtest oracle `src/tests/save_benchmark_tests.cpp:170-173`, helpers `:48-119`.
- Sizes: `MAX_SKILLS 256` / `MAX_TOUNGE 3` / `MAX_AFFECT 32` `src/structs.h:707-710`; `MAX_BODYPARTS 11` `:73`; `MAX_COLOR_FIELDS 16` `src/color.h:7`.
