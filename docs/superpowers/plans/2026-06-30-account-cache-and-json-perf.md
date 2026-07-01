# Account-Cache + Parallel JSON Perf — Implementation Plan

> **STATUS: IMPLEMENTED 2026-06-30.** All 9 tasks landed across 5 commits (`3d904cf` cache, `bae643f` JsonReaderV2, `cbc3fd7` deserialize v2a/v2b, `d241671` serialize v2a/v2b, `5f3a304` savebench compare wiring). Full suite green: `AccountCache.*` (7), `JsonPerf.*` (12), `SaveBenchmark.*` (2), `CharacterJson.*` all pass; the only red is the pre-existing 32-bit baseline `JsonUtils.RejectsIntegersOutsideIntRange`. Three deviations from the drafted tasks (all improvements):
> 1. **Cache tests use a `set_backing_resolvers_for_testing` DI seam** (counting fakes) instead of a disk fixture — the real account-directory `readdir` scan does not resolve under QEMU i386 (the `AccountManagement.*` on-disk suites are baseline reds for the same reason); call counts make "served from cache, no rescan" directly observable.
> 2. **Deserialize equivalence gates compare v1/v2 *outcomes*** (success-with-identical-struct OR identical rejection) rather than asserting success — the all-skills "heavy" tier intentionally produces duplicate slugified skill keys that v1 itself rejects, so this is a stronger gate covering the rejection path.
> 3. **`serialize_*_v2b` reuses v1's `collect_*` intermediates** (guaranteeing identical order/filtering → byte-equality) and differs only by the escape fast-path + cached keys, rather than the riskier inline-iteration rewrite.
>
> **Gated follow-ups (NOT done here, per the behavior-neutral scope):** run `savebench compare 500` on `drelibench` and record the A/B numbers in the perf-findings doc (spec §9.3); then adoption — route live callers through the winners + add the `write_account_file` invalidation hook + re-measure (spec §4.3/§9.4).

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add profileable, parallel (v2) implementations of account-file resolution and character JSON serialize/deserialize beside the untouched originals, wired into the savebench A/B "COMPARE" report, so each optimization is measured head-to-head before any live path adopts it.

**Architecture:** Behavior-neutral branch. A new `account_cache` module memoizes the redundant `read_account_file` resolution; new `JsonReaderV2` + stacked `serialize_*_v2a/_v2b` / `deserialize_*_v2a/_v2b` functions isolate the memoized-lookup win (the dominant cost) from the tokenizer win. The existing nested deserialize parsers become reader-generic templates so `JsonReaderV2` flows through them while v1's output stays byte-identical. savebench gains a `compare` out-param that builds a **separate** report (never polluting the canonical pipeline TOTAL). No live caller is switched; adoption is a gated follow-up.

**Tech Stack:** C++17 (g++ i386 `-m32`, Docker-authoritative), `<charconv>`/`<unordered_map>`/`<string_view>`, GoogleTest, CMake.

## Global Constraints

- **Compiler floor = g++ 9.4.0** (live runtime; Docker build image is g++ 10). **Integer `std::from_chars`/`std::to_chars` ONLY** — never floating-point charconv (GCC 11). No character JSON field is `double`/`float`, so the integer path is always sufficient.
- **Portable, cross-compilable standard C++ for all new functions** (`CLAUDE.local.md`): `<charconv>`, `<unordered_map>`, `<string_view>`, `<algorithm>`. No POSIX in new code. The cache memoizes the existing POSIX resolvers; it does not rewrite them.
- **All new/touched files here are LF.** `.claude/.no-autoformat` is present → clang-format does NOT run on edit. Hand-apply Allman braces, 4-space indent, mandatory braces on every control-flow body.
- **Every class-scoped / member variable carries a `//` comment** describing its role and how the class uses it (global C++ convention).
- **Docker i386 `g++` build is authoritative.** Host clang/IDE diagnostics about `std::filesystem`/`std::from_chars`/`MAX`/`MIN`/`gtest` are false positives. Build/test only via `scripts/rots-docker.sh test '--gtest_filter=Suite.Name'` (build `ageland_tests`, run `./bin/tests`; **quote the filter** — `*` globs in the shell otherwise).
- **The 32-bit test build has ~163 pre-existing baseline reds** in untouched suites (32-bit-vs-64-bit expectation diffs, e.g. `JsonUtils.RejectsIntegersOutsideIntRange`). **Gate PER-TEST by name**, never per-suite.
- **Behavior-neutral:** add parallel functions only; do not switch any live caller; do not add cache invalidation (deferred to adoption). The in-game command keeps its sandbox invariant — read-only / scratch-path only, never a live write.
- **Commit after each task.** End commit messages with `Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>`.

## File Structure

| File | Responsibility | New? |
|---|---|---|
| `src/account_cache.h` / `.cpp` | `read_account_file_cached`, `find_linked_character_owner_account_cached`, `clear()` — single-threaded `(root,name)`-keyed memoization of the POSIX resolvers (full `AccountData` + char→owner incl. negative). | **new** |
| `src/json_utils.h` / `.cpp` | Add `class JsonReaderV2` (same interface as `JsonReader`, faster internals) and `append_escaped_json_string(std::string&, const std::string&)` fast-path. `JsonReader`/`escape_json_string` untouched. | modify |
| `src/character_json.h` / `.cpp` | Add `serialize_*_v2a/_v2b`, `deserialize_*_v2a/_v2b`; file-local memoized `*_index_for_key_memoized` + cached `*_key_for_index_cached`; template the nested deserialize parsers on reader type; new `deserialize_character_v2_dispatch<Reader>`. v1 functions untouched. | modify |
| `src/save_benchmark.h` / `.cpp` | Add `PipelineReport* compare = nullptr` to `profile_save`/`profile_load`; build the separate v1-vs-variant + cached-vs-uncached compare report inside the `.cpp`. | modify |
| `src/savebench.cpp` | `savebench [compare] N` subcommand; print the extra COMPARE sections. Sandbox invariant preserved. | modify |
| `src/tests/account_cache_tests.cpp` | `AccountCache` suite — hit/miss/negative/`clear()`/`(root,name)` isolation. | **new** |
| `src/tests/json_perf_tests.cpp` | `JsonPerf` suite — serialize byte-equality (v1==v2a==v2b), deserialize `char_file_u` equivalence, memoized==slow lookup. | **new** |
| `src/CMakeLists.txt` | Wire `account_cache.cpp` into `ROTS_SERVER_SOURCES` (after line 42); `account_cache_tests.cpp` + `json_perf_tests.cpp` into `ROTS_TEST_SOURCES` (after line 128). | modify |

---

## Implementation coordination notes (cross-task seams)

The task bodies below were drafted per-workstream; these three seams cross workstreams and are resolved here (execute tasks in numeric order 1→9):

1. **`src/tests/json_perf_tests.cpp` is created once in Task 3**, then **appended** by Tasks 4, 6, and 7 (never recreated/overwritten). Its single `ROTS_TEST_SOURCES` entry (after `tests/json_utils_tests.cpp`, `CMakeLists.txt:128`) is added in Task 3; later tasks **skip** re-adding it.
2. **`TEST(JsonPerf, AppendEscapedMatchesEscapeJsonString)` is defined in both Task 3 (line ~566) and Task 7 (line ~2134).** Implement it **once**, using **Task 7's richer version** (more escape inputs + the `"PREFIX:"` append-into-buffer assertion). When doing Task 3, write Task 7's version; when doing Task 7, **skip** re-adding it. A duplicate `TEST` name in one binary is a link/registration error.
3. **`src/CMakeLists.txt` source-list insertion points** (order within a list is build-irrelevant): `account_cache.cpp` after `account_management.cpp` in `ROTS_SERVER_SOURCES`; `tests/account_cache_tests.cpp` after `tests/account_management_tests.cpp` and `tests/json_perf_tests.cpp` after `tests/json_utils_tests.cpp` in `ROTS_TEST_SOURCES`.

After Task 9, run the full suite once (`scripts/rots-docker.sh test '--gtest_filter=AccountCache.*:JsonPerf.*:SaveBenchmark.*'`) and confirm only pre-existing 32-bit baseline reds remain. Spec §9.3 (re-measure `savebench compare` on `drelibench` and append the numbers to `docs/superpowers/specs/2026-06-29-savebench-pipeline-performance-findings.md`) and §4.3/§9.4 (adoption: route live callers + add the `write_account_file` invalidation hook) are the **gated follow-ups** — not part of this behavior-neutral build.

<!-- TASKS: assembled from workstream drafts below -->

I have everything I need. Here is the confirmed "not linked" semantic: `find_linked_character_owner_account` (identity.cpp:647) delegates to `find_character_owner_account` (account_management.cpp:720), which **returns `true` with `*owner_account_name` cleared to `""`** when no account claims the character (account_management.cpp:727, 789-791) — including when the accounts dir is absent (ENOENT → true/empty, :732-734). A genuine error (null out-param, ambiguous ownership, read failure) returns `false` with an error string. So the negative-cache struct must capture all three: the bool return, the owner name (empty == "not linked"), and the error string.

Below are Tasks 1-2.

---

### Task 1: account_cache module — account map (`read_account_file_cached`, `clear`) + CMake wiring
**Files:**
- Create `src/account_cache.h` (new, LF) — public surface for `namespace account_cache`.
- Create `src/account_cache.cpp` (new, LF) — implementation (account map this task; owner cache deferred to Task 2).
- Create `src/tests/account_cache_tests.cpp` (new, LF) — gtest suite `AccountCache`.
- Modify `src/CMakeLists.txt:39` (add `account_cache.cpp` to `ROTS_SERVER_SOURCES`) and `:108` (add `tests/account_cache_tests.cpp` to `ROTS_TEST_SOURCES`).

**Interfaces:**
- Consumes (contract A): `account::read_account_file` (decl `account_management_storage.h:23`, def `account_management_storage.cpp:244`), `account::AccountData` (`account_management_types.h:20-51`), `account::serialize_account_to_json` (`account_management_storage.h:19`) for byte-equality in tests.
- Produces (later tasks + Task 2 rely on these EXACT signatures):
  ```cpp
  bool account_cache::read_account_file_cached(const std::string& root_directory,
        const std::string& account_name, account::AccountData* account, std::string* error_message);
  bool account_cache::find_linked_character_owner_account_cached(const std::string& root_directory,
        const std::string& character_name, std::string* owner_account_name, std::string* error_message);
  void account_cache::clear();
  ```
  (Task 1 ships `read_account_file_cached`/`clear` memoized and `find_linked_character_owner_account_cached` as a non-memoizing pass-through; Task 2 memoizes the latter.)

- [ ] **Step 1: Write the failing test** — create `src/tests/account_cache_tests.cpp` (LF, Allman, 4-space, mandatory braces). The "served from cache after the backing tree is removed" case is the oracle for *no rescan*: once `lib/accounts` is deleted, an uncached `account::read_account_file` must fail, so a still-succeeding cached read can only have come from the memo map.
  ```cpp
  #include "../account_cache.h"
  #include "../account_management.h"

  #include <gtest/gtest.h>

  #include <cstring>
  #include <dirent.h>
  #include <filesystem>
  #include <string>
  #include <sys/stat.h>
  #include <sys/types.h>
  #include <system_error>
  #include <unistd.h>

  namespace {

  // Creates a unique scratch dir under /tmp and removes its whole tree on destruction, giving each
  // AccountCache case an isolated on-disk lib/accounts root. Local copy of the helper in
  // account_management_tests.cpp:25-76 (anonymous-namespace; test helpers are not shared across TUs).
  class TemporaryDirectory {
  public:
      TemporaryDirectory()
      {
          char directory_template[] = "/tmp/rots-account-cache-tests-XXXXXX";
          char* created_path = mkdtemp(directory_template);
          EXPECT_NE(created_path, nullptr) << "Expected mkdtemp to create a temporary directory for account-cache tests.";
          if (created_path)
          {
              m_path = created_path;
          }
      }

      ~TemporaryDirectory()
      {
          if (!m_path.empty())
          {
              std::error_code remove_error;
              std::filesystem::remove_all(std::filesystem::path(m_path), remove_error);
          }
      }

      const std::string& path() const
      {
          return m_path;
      }

  private:
      // Absolute path of the scratch directory; empty if mkdtemp failed. Used as the cache "root".
      std::string m_path;
  };

  // AccountCache suite fixture: clears the process-wide memo maps before every case so cached
  // entries never bleed between tests (the module's only reset hook is account_cache::clear()).
  class AccountCache : public ::testing::Test {
  protected:
      void SetUp() override
      {
          account_cache::clear();
      }
  };

  } // namespace

  TEST_F(AccountCache, CachedReadReturnsSameAccountDataAsDirectRead) {
      TemporaryDirectory temp_directory;
      std::string error_message;
      ASSERT_TRUE(account::create_account(temp_directory.path(), "alpha-admin", "player@example.com", "ValidPass1", 1700100000, nullptr, &error_message)) << error_message;

      account::AccountData direct;
      ASSERT_TRUE(account::read_account_file(temp_directory.path(), "alpha-admin", &direct, &error_message)) << error_message;

      account::AccountData cached;
      ASSERT_TRUE(account_cache::read_account_file_cached(temp_directory.path(), "alpha-admin", &cached, &error_message)) << error_message;

      EXPECT_EQ(account::serialize_account_to_json(cached), account::serialize_account_to_json(direct));
  }

  TEST_F(AccountCache, SecondCachedReadIsServedFromCacheAfterBackingTreeRemoved) {
      TemporaryDirectory temp_directory;
      std::string error_message;
      ASSERT_TRUE(account::create_account(temp_directory.path(), "alpha-admin", "player@example.com", "ValidPass1", 1700100001, nullptr, &error_message)) << error_message;

      account::AccountData first;
      ASSERT_TRUE(account_cache::read_account_file_cached(temp_directory.path(), "alpha-admin", &first, &error_message)) << error_message;

      // Destroy the on-disk accounts tree so any rescan would now fail with ENOENT.
      std::error_code remove_error;
      std::filesystem::remove_all(std::filesystem::path(temp_directory.path()) / "accounts", remove_error);
      ASSERT_FALSE(remove_error) << remove_error.message();

      account::AccountData probe;
      EXPECT_FALSE(account::read_account_file(temp_directory.path(), "alpha-admin", &probe, &error_message))
          << "Backing account file should be gone, proving the next cached read cannot have rescanned.";

      account::AccountData second;
      ASSERT_TRUE(account_cache::read_account_file_cached(temp_directory.path(), "alpha-admin", &second, &error_message)) << error_message;
      EXPECT_EQ(account::serialize_account_to_json(second), account::serialize_account_to_json(first));
  }

  TEST_F(AccountCache, DoesNotBleedAcrossRootsForSameAccountName) {
      TemporaryDirectory root_a;
      TemporaryDirectory root_b;
      std::string error_message;
      ASSERT_TRUE(account::create_account(root_a.path(), "alpha-admin", "a@example.com", "ValidPass1", 1700100002, nullptr, &error_message)) << error_message;
      ASSERT_TRUE(account::create_account(root_b.path(), "alpha-admin", "b@example.com", "ValidPass1", 1700100003, nullptr, &error_message)) << error_message;

      account::AccountData from_a;
      account::AccountData from_b;
      ASSERT_TRUE(account_cache::read_account_file_cached(root_a.path(), "alpha-admin", &from_a, &error_message)) << error_message;
      ASSERT_TRUE(account_cache::read_account_file_cached(root_b.path(), "alpha-admin", &from_b, &error_message)) << error_message;

      EXPECT_EQ(from_a.normalized_email, "a@example.com");
      EXPECT_EQ(from_b.normalized_email, "b@example.com");
  }
  ```

- [ ] **Step 2: Add the module header + a non-memoizing scaffold + CMake wiring so the suite compiles.** Create `src/account_cache.h` with the full contract surface (the header is the locked interface — write it once, never churn):
  ```cpp
  #ifndef ACCOUNT_CACHE_H
  #define ACCOUNT_CACHE_H

  #include "account_management_types.h"

  #include <string>

  namespace account_cache {

  // Memoized parallel of account::read_account_file. On a hit, copies the cached AccountData with
  // zero filesystem work; on a miss, delegates to account::read_account_file, stores a copy, returns.
  bool read_account_file_cached(const std::string& root_directory, const std::string& account_name,
                                account::AccountData* account, std::string* error_message);

  // Memoized parallel of account::find_linked_character_owner_account, with negative ("not linked")
  // caching so a repeat unlinked lookup short-circuits the O(N) account-directory scan.
  bool find_linked_character_owner_account_cached(const std::string& root_directory, const std::string& character_name,
                                                  std::string* owner_account_name, std::string* error_message);

  // Empties both memo maps. Call in test-fixture SetUp() for isolation; no live invalidation in this branch.
  void clear();

  } // namespace account_cache

  #endif
  ```
  Create `src/account_cache.cpp` with the account map already wired but a pass-through for the owner resolver (this makes `SecondCachedReadIsServedFromCacheAfterBackingTreeRemoved` the single red and keeps the owner half deferred to Task 2 — but to keep the TDD honest, ship the account read as a *pass-through too* in this step so the cache-hit test genuinely fails first):
  ```cpp
  #include "account_cache.h"

  #include "account_management.h"

  namespace account_cache {

  bool read_account_file_cached(const std::string& root_directory, const std::string& account_name,
                                account::AccountData* account, std::string* error_message)
  {
      // SCAFFOLD (red): pass straight through; Step 4 replaces this body with the memoized account map.
      return account::read_account_file(root_directory, account_name, account, error_message);
  }

  bool find_linked_character_owner_account_cached(const std::string& root_directory, const std::string& character_name,
                                                  std::string* owner_account_name, std::string* error_message)
  {
      // Non-memoizing pass-through; Task 2 adds the negative owner cache.
      return account::find_linked_character_owner_account(root_directory, character_name, owner_account_name, error_message);
  }

  void clear()
  {
  }

  } // namespace account_cache
  ```
  Then wire CMake: in `src/CMakeLists.txt`, add `    account_cache.cpp` immediately after `account_management.cpp` (`:39`) inside `ROTS_SERVER_SOURCES`, and add `    tests/account_cache_tests.cpp` immediately after `tests/account_management_tests.cpp` (`:108`) inside `ROTS_TEST_SOURCES`.

- [ ] **Step 3: Run it, expect FAIL.** Command: `scripts/rots-docker.sh test '--gtest_filter=AccountCache.*'`. Expected: suite COMPILES and links; `CachedReadReturnsSameAccountDataAsDirectRead` and `DoesNotBleedAcrossRootsForSameAccountName` PASS (pass-through reads the right file), but `SecondCachedReadIsServedFromCacheAfterBackingTreeRemoved` FAILS at the final `ASSERT_TRUE` (the second read rescans a deleted tree → false). `[  FAILED  ] AccountCache.SecondCachedReadIsServedFromCacheAfterBackingTreeRemoved`.

- [ ] **Step 4: Implement the account-map memoization.** Replace the bodies in `src/account_cache.cpp`: add an anonymous-namespace file-local map + key composer, memoize `read_account_file_cached` (cache only successful reads; on a hit clear `error_message` to mirror the success "" contract), and make `clear()` empty the map. Leave `find_linked_character_owner_account_cached` as the pass-through (Task 2 owns it). Final Task-1 state of the top of the file + the two changed functions:
  ```cpp
  #include "account_cache.h"

  #include "account_management.h"

  #include <string>
  #include <unordered_map>

  namespace account_cache {

  namespace {

  // Memoized successful account reads keyed by compose_key(root, account_name); value is the full
  // parsed AccountData. A hit returns a copy with zero filesystem work; misses are not cached.
  // Emptied by clear() for test isolation.
  std::unordered_map<std::string, account::AccountData> g_account_cache;

  // Unit-separator byte joining root and name into a single map key; chosen because it cannot appear
  // in a filesystem path component, so (root, name) pairs never collide or bleed across roots.
  const char kKeySeparator = '\x1f';

  std::string compose_key(const std::string& root_directory, const std::string& name)
  {
      std::string key;
      key.reserve(root_directory.size() + 1 + name.size());
      key.append(root_directory);
      key.push_back(kKeySeparator);
      key.append(name);
      return key;
  }

  } // namespace

  bool read_account_file_cached(const std::string& root_directory, const std::string& account_name,
                                account::AccountData* account, std::string* error_message)
  {
      if (account == nullptr)
      {
          return account::read_account_file(root_directory, account_name, account, error_message);
      }

      const std::string key = compose_key(root_directory, account_name);
      const auto cached_entry = g_account_cache.find(key);
      if (cached_entry != g_account_cache.end())
      {
          *account = cached_entry->second;
          if (error_message != nullptr)
          {
              error_message->clear();
          }
          return true;
      }

      account::AccountData loaded_account;
      if (!account::read_account_file(root_directory, account_name, &loaded_account, error_message))
      {
          return false;
      }

      g_account_cache.emplace(key, loaded_account);
      *account = loaded_account;
      return true;
  }
  ```
  And change `clear()` to:
  ```cpp
  void clear()
  {
      g_account_cache.clear();
  }
  ```
  (Leave `find_linked_character_owner_account_cached` unchanged from Step 2.) Hand-format: LF, Allman, 4-space, mandatory braces (`.claude/.no-autoformat` is present — nothing auto-fixes).

- [ ] **Step 5: Run tests, expect PASS.** Command: `scripts/rots-docker.sh test '--gtest_filter=AccountCache.*'`. Expected: `[  PASSED  ] 3 tests.` — all three of `CachedReadReturnsSameAccountDataAsDirectRead`, `SecondCachedReadIsServedFromCacheAfterBackingTreeRemoved`, `DoesNotBleedAcrossRootsForSameAccountName`.

- [ ] **Step 6: Commit.** `git add src/account_cache.h src/account_cache.cpp src/tests/account_cache_tests.cpp src/CMakeLists.txt` then `git commit -m "feat(account_cache): memoized read_account_file_cached + clear (account map)" -m "Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"`.

---

### Task 2: account_cache module — owner map with negative caching (`find_linked_character_owner_account_cached`)
**Files:**
- Modify `src/account_cache.cpp` (add `OwnerResolution` + `g_owner_cache`; memoize `find_linked_character_owner_account_cached`; extend `clear()`).
- Modify `src/tests/account_cache_tests.cpp` (append two owner-side cases to suite `AccountCache`).
- No header or CMake change (`account_cache.h` already declares the function from Task 1).

**Interfaces:**
- Consumes (Task 1 + contract A): `account_cache::find_linked_character_owner_account_cached`/`clear` (decls in `account_cache.h`), the `g_account_cache`/`compose_key` file-locals from Task 1; `account::find_linked_character_owner_account` (decl `account_management_identity.h:35`, def `account_management_identity.cpp:647` → `find_character_owner_account` `account_management.cpp:720`). Confirmed "not linked" semantics: returns **`true` with empty owner** (account_management.cpp:727, 789-791); genuine errors return `false` with a message. So the negative-cache struct stores the full outcome `{bool resolved, std::string owner_account_name, std::string error_message}`.
- Produces: the memoized owner resolver; `clear()` now empties both maps. No new public symbols (signatures locked in Task 1).

- [ ] **Step 1: Write the failing test** — append these two `TEST_F`s to `src/tests/account_cache_tests.cpp` (after the Task 1 cases). The negative test is the genuine red: it caches the unlinked outcome, then links the character on disk, then proves an uncached scan now sees the owner while the cached path still returns empty. `admin_link_character` works right after `create_account` with no email-verify (see `account_management_tests.cpp:1289-1290`).
  ```cpp
  TEST_F(AccountCache, NegativeOwnerResultIsCachedAndShortCircuitsRescan) {
      TemporaryDirectory temp_directory;
      std::string error_message;
      ASSERT_TRUE(account::create_account(temp_directory.path(), "alpha-admin", "player@example.com", "ValidPass1", 1700100010, nullptr, &error_message)) << error_message;

      // "aragorn" is not linked yet -> the cached resolver records the negative (true, empty) outcome.
      std::string owner_first;
      ASSERT_TRUE(account_cache::find_linked_character_owner_account_cached(temp_directory.path(), "aragorn", &owner_first, &error_message)) << error_message;
      EXPECT_TRUE(owner_first.empty());

      // Link aragorn on disk; an uncached scan now resolves the owner, but the cache must NOT.
      ASSERT_TRUE(account::admin_link_character(temp_directory.path(), "alpha-admin", "aragorn", 1700100011, nullptr, &error_message)) << error_message;

      std::string owner_probe;
      ASSERT_TRUE(account::find_linked_character_owner_account(temp_directory.path(), "aragorn", &owner_probe, &error_message)) << error_message;
      EXPECT_EQ(owner_probe, "alpha-admin") << "Disk now links aragorn, so a real rescan sees the owner.";

      std::string owner_second;
      ASSERT_TRUE(account_cache::find_linked_character_owner_account_cached(temp_directory.path(), "aragorn", &owner_second, &error_message)) << error_message;
      EXPECT_TRUE(owner_second.empty()) << "The negative result must have been cached (no rescan).";
  }

  TEST_F(AccountCache, ClearResetsOwnerCacheSoNextResolveRescans) {
      TemporaryDirectory temp_directory;
      std::string error_message;
      ASSERT_TRUE(account::create_account(temp_directory.path(), "alpha-admin", "player@example.com", "ValidPass1", 1700100020, nullptr, &error_message)) << error_message;

      std::string owner_first;
      ASSERT_TRUE(account_cache::find_linked_character_owner_account_cached(temp_directory.path(), "aragorn", &owner_first, &error_message)) << error_message;
      EXPECT_TRUE(owner_first.empty());

      ASSERT_TRUE(account::admin_link_character(temp_directory.path(), "alpha-admin", "aragorn", 1700100021, nullptr, &error_message)) << error_message;

      account_cache::clear();

      std::string owner_after_clear;
      ASSERT_TRUE(account_cache::find_linked_character_owner_account_cached(temp_directory.path(), "aragorn", &owner_after_clear, &error_message)) << error_message;
      EXPECT_EQ(owner_after_clear, "alpha-admin") << "After clear() the resolver must rescan and see the new link.";
  }
  ```

- [ ] **Step 2: Run it, expect FAIL.** Command: `scripts/rots-docker.sh test '--gtest_filter=AccountCache.*'`. Expected: `NegativeOwnerResultIsCachedAndShortCircuitsRescan` FAILS at the final `EXPECT_TRUE(owner_second.empty())` — Task 1's pass-through rescans and returns `"alpha-admin"`. (`ClearResetsOwnerCacheSoNextResolveRescans` passes trivially against the pass-through; it becomes a real guard once caching lands — it would go red if a later `clear()` failed to empty the owner map.) The three Task 1 cases still pass. `[  FAILED  ] AccountCache.NegativeOwnerResultIsCachedAndShortCircuitsRescan`.

- [ ] **Step 3: Implement the owner cache.** In `src/account_cache.cpp`: (a) add the `OwnerResolution` struct **before** the maps in the anonymous namespace; (b) add `g_owner_cache`; (c) replace the pass-through body of `find_linked_character_owner_account_cached`; (d) extend `clear()` to empty `g_owner_cache`. Add to the anon-namespace block (above `g_account_cache`):
  ```cpp
  // Full memoized outcome of account::find_linked_character_owner_account for one (root, character):
  // its bool return, the owner name (empty string is the valid "character is not linked" result we
  // cache), and the error string — all replayed verbatim on a hit so the cached path is byte-identical.
  struct OwnerResolution
  {
      // Return value of the underlying resolver; replayed on a hit.
      bool resolved = false;
      // Resolved owner account name; empty == the negative "not linked" outcome.
      std::string owner_account_name;
      // Error string the underlying resolver produced; replayed on a hit.
      std::string error_message;
  };

  // Memoized owner resolutions keyed by compose_key(root, character_name); value is the full
  // OwnerResolution, so a repeat unlinked lookup short-circuits the O(N) account scan. Emptied by clear().
  std::unordered_map<std::string, OwnerResolution> g_owner_cache;
  ```
  Replace the `find_linked_character_owner_account_cached` body with:
  ```cpp
  bool find_linked_character_owner_account_cached(const std::string& root_directory, const std::string& character_name,
                                                  std::string* owner_account_name, std::string* error_message)
  {
      if (owner_account_name == nullptr)
      {
          // Preserve the underlying null-guard behavior exactly; nothing to cache without an out-param.
          return account::find_linked_character_owner_account(root_directory, character_name, owner_account_name, error_message);
      }

      const std::string key = compose_key(root_directory, character_name);
      const auto cached_entry = g_owner_cache.find(key);
      if (cached_entry != g_owner_cache.end())
      {
          *owner_account_name = cached_entry->second.owner_account_name;
          if (error_message != nullptr)
          {
              *error_message = cached_entry->second.error_message;
          }
          return cached_entry->second.resolved;
      }

      OwnerResolution outcome;
      std::string resolved_error;
      outcome.resolved = account::find_linked_character_owner_account(root_directory, character_name, &outcome.owner_account_name, &resolved_error);
      outcome.error_message = resolved_error;
      const auto inserted = g_owner_cache.emplace(key, std::move(outcome));

      *owner_account_name = inserted.first->second.owner_account_name;
      if (error_message != nullptr)
      {
          *error_message = inserted.first->second.error_message;
      }
      return inserted.first->second.resolved;
  }
  ```
  Extend `clear()`:
  ```cpp
  void clear()
  {
      g_account_cache.clear();
      g_owner_cache.clear();
  }
  ```
  Hand-format LF/Allman/4-space/braces (no autoformat).

- [ ] **Step 4: Run tests, expect PASS.** Command: `scripts/rots-docker.sh test '--gtest_filter=AccountCache.*'`. Expected: `[  PASSED  ] 5 tests.` — the three Task 1 cases plus `NegativeOwnerResultIsCachedAndShortCircuitsRescan` and `ClearResetsOwnerCacheSoNextResolveRescans`.

- [ ] **Step 5: Commit.** `git add src/account_cache.cpp src/tests/account_cache_tests.cpp` then `git commit -m "feat(account_cache): negative-cached find_linked_character_owner_account_cached" -m "Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"`.

---

### Task 3: `JsonReaderV2` + `append_escaped_json_string` (json_utils, contract §B)
**Files:**
- Modify `src/json_utils.h:10` (add `append_escaped_json_string` decl) and `src/json_utils.h:38-40` (add `JsonReaderV2` class between `JsonReader`'s closing `};` and `} // namespace`).
- Modify `src/json_utils.cpp:1-10` (add `#include <charconv>`, `#include <utility>`), `:45` (branchless helpers in the anon namespace), `:88` (add `append_escaped_json_string` after `escape_json_string`), `:457` (add full `JsonReaderV2` impl before `} // namespace json_utils`).
- Create `src/tests/json_perf_tests.cpp` (suite `JsonPerf`).
- Modify `src/CMakeLists.txt:128` (add `tests/json_perf_tests.cpp` to `ROTS_TEST_SOURCES`).

**Interfaces:**
- Produces (later tasks/serialize workstream rely on these EXACT signatures):
  - `class json_utils::JsonReaderV2` with the same public surface as `JsonReader` but `using ObjectPropertyParser = std::function<bool(const std::string&, JsonReaderV2*, std::string*)>;` / `using ArrayValueParser = std::function<bool(JsonReaderV2*, std::string*)>;`.
  - `void json_utils::append_escaped_json_string(std::string& out, const std::string& value);`
- Consumes: existing `json_utils::JsonReader`, `json_utils::escape_json_string` (as the equivalence oracle).

- [ ] **Step 1: Write the failing test.** Create `src/tests/json_perf_tests.cpp`:
```cpp
#include "../json_utils.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace {

// Aggregates one value of every type the readers expose, so a JsonReader vs JsonReaderV2 parse of
// the same document can be compared field by field.
struct ParsedDocument {
    std::string name;
    int level = 0;
    long big = 0;
    bool flag_true = false;
    bool flag_false = false;
    std::vector<std::string> tags;
    int nested_x = 0;
    int nested_y = 0;
};

// Drives a full root-object parse through whichever reader Reader names; both readers share the
// public surface, so the same lambda body deduces against each reader's own nested typedefs.
template <class Reader>
bool parse_document(const std::string& json, ParsedDocument* out, std::string* error_message)
{
    Reader reader(json);
    return reader.parse_root_object([out](const std::string& key, Reader* nested_reader, std::string* nested_error_message) {
        if (key == "name")
            return nested_reader->parse_string(&out->name, nested_error_message);
        if (key == "level")
            return nested_reader->parse_integer(&out->level, nested_error_message);
        if (key == "big")
            return nested_reader->parse_long(&out->big, nested_error_message);
        if (key == "flag_true")
            return nested_reader->parse_bool(&out->flag_true, nested_error_message);
        if (key == "flag_false")
            return nested_reader->parse_bool(&out->flag_false, nested_error_message);
        if (key == "tags")
            return nested_reader->parse_string_array(&out->tags, nested_error_message);
        if (key == "nested") {
            return nested_reader->parse_object([out](const std::string& nested_key, Reader* inner_reader, std::string* inner_error_message) {
                if (nested_key == "x")
                    return inner_reader->parse_integer(&out->nested_x, inner_error_message);
                if (nested_key == "y")
                    return inner_reader->parse_integer(&out->nested_y, inner_error_message);
                return inner_reader->skip_value(inner_error_message);
            },
                nested_error_message);
        }
        return nested_reader->skip_value(nested_error_message);
    },
        error_message);
}

const char* const kSampleJson = R"JSON({
  "name": "Frodo \"the\" Brave\n\tBaggins",
  "level": 42,
  "big": 2147483000,
  "flag_true": true,
  "flag_false": false,
  "tags": ["alpha", "beta", "gamma"],
  "nested": { "x": -5, "y": 10 },
  "ignored": [1, 2, {"z": 3}]
})JSON";

} // namespace

TEST(JsonPerf, JsonReaderV2MatchesBaselineParse)
{
    const std::string json = kSampleJson;

    ParsedDocument v1;
    std::string v1_error;
    ASSERT_TRUE(parse_document<json_utils::JsonReader>(json, &v1, &v1_error)) << v1_error;

    ParsedDocument v2;
    std::string v2_error;
    ASSERT_TRUE(parse_document<json_utils::JsonReaderV2>(json, &v2, &v2_error)) << v2_error;

    EXPECT_EQ(v1.name, v2.name);
    EXPECT_EQ(v1.level, v2.level);
    EXPECT_EQ(v1.big, v2.big);
    EXPECT_EQ(v1.flag_true, v2.flag_true);
    EXPECT_EQ(v1.flag_false, v2.flag_false);
    EXPECT_EQ(v1.tags, v2.tags);
    EXPECT_EQ(v1.nested_x, v2.nested_x);
    EXPECT_EQ(v1.nested_y, v2.nested_y);

    // Pin the decoded values too, so a matching-but-wrong parse can't slip through.
    EXPECT_EQ("Frodo \"the\" Brave\n\tBaggins", v2.name);
    EXPECT_EQ(42, v2.level);
    EXPECT_EQ(2147483000L, v2.big);
    EXPECT_TRUE(v2.flag_true);
    EXPECT_FALSE(v2.flag_false);
    ASSERT_EQ(3u, v2.tags.size());
    EXPECT_EQ("beta", v2.tags[1]);
    EXPECT_EQ(-5, v2.nested_x);
    EXPECT_EQ(10, v2.nested_y);
}

TEST(JsonPerf, AppendEscapedMatchesEscapeJsonString)
{
    const std::vector<std::string> samples = {
        "plain_slug",
        "has \"quote\"",
        "back\\slash",
        std::string("control\x01char"),
        "newline\nand\ttab",
        "",
    };
    for (const std::string& sample : samples) {
        std::string appended = "PREFIX:";
        json_utils::append_escaped_json_string(appended, sample);
        EXPECT_EQ("PREFIX:" + json_utils::escape_json_string(sample), appended) << "sample=" << sample;
    }
}
```
Then add `tests/json_perf_tests.cpp` to `ROTS_TEST_SOURCES` in `src/CMakeLists.txt` (insert immediately after `tests/json_utils_tests.cpp` at line 128). (Coordination note: the serialize workstream appends its serialize byte-equality `JsonPerf.*` cases to this same file and shares this one CMake entry — do not add a second entry.)

- [ ] **Step 2: Run it, expect FAIL (compile error).** `scripts/rots-docker.sh test '--gtest_filter=JsonPerf.*'` → build fails before any test runs: `error: 'JsonReaderV2' is not a member of 'json_utils'` and `'append_escaped_json_string' is not a member of 'json_utils'`.

- [ ] **Step 3: Implement.**
  (a) `src/json_utils.h` — add after the `escape_json_string` decl (line 10):
```cpp
// Fast-path JSON string escaper for serialize v2. Scans value once; if it contains no character
// that needs escaping ('"', '\\', or a control char < 0x20) the bytes are appended to out verbatim,
// otherwise it falls back to the same escaping escape_json_string produces. Appends (no return copy).
void append_escaped_json_string(std::string& out, const std::string& value);
```
  and add this class between `JsonReader`'s closing `};` (line 38) and `} // namespace json_utils` (line 40):
```cpp
// Optimized drop-in equivalent of JsonReader used by the v2 character deserializers: identical
// public surface and observable behavior, but lower-allocation internals (from_chars integer parse,
// move-out / no-escape-fast-path strings, branchless whitespace/digit tests, strlen-free literal
// match). Kept as a separate non-virtual class so JsonReader stays an untouched measurement baseline.
class JsonReaderV2 {
public:
    using ObjectPropertyParser = std::function<bool(const std::string&, JsonReaderV2*, std::string*)>;
    using ArrayValueParser = std::function<bool(JsonReaderV2*, std::string*)>;

    explicit JsonReaderV2(const std::string& input);

    bool parse_root_object(const ObjectPropertyParser& property_parser, std::string* error_message);
    bool parse_object(const ObjectPropertyParser& property_parser, std::string* error_message);
    bool parse_array(const ArrayValueParser& value_parser, std::string* error_message);
    bool parse_string(std::string* value, std::string* error_message);
    bool parse_bool(bool* value, std::string* error_message);
    bool parse_integer(int* value, std::string* error_message);
    bool parse_long(long* value, std::string* error_message);
    bool parse_string_array(std::vector<std::string>* values, std::string* error_message);
    bool skip_value(std::string* error_message);

private:
    bool parse_object_body(const ObjectPropertyParser& property_parser, std::string* error_message);
    bool consume(char expected);
    bool match_literal(const char* literal, size_t literal_length);
    void skip_whitespace();
    bool is_at_end() const;

    // Immutable view of the JSON text being parsed; owned by the caller and must outlive this reader.
    const std::string& m_input;
    // Cursor into m_input of the next unconsumed character; advanced by every consume/parse step.
    size_t m_position = 0;
};
```
  (b) `src/json_utils.cpp` — add `#include <charconv>` and `#include <utility>` to the include block (lines 1-10). Inside the existing anonymous namespace (before the `} // namespace` at line 45) add:
```cpp
    inline bool is_json_space(char character)
    {
        return character == ' ' || character == '\t' || character == '\n' || character == '\r';
    }

    inline bool is_json_digit(char character)
    {
        return character >= '0' && character <= '9';
    }
```
  After `escape_json_string` (line 88) add:
```cpp
void append_escaped_json_string(std::string& out, const std::string& value)
{
    bool needs_escape = false;
    for (char character : value) {
        if (character == '"' || character == '\\' || static_cast<unsigned char>(character) < 0x20) {
            needs_escape = true;
            break;
        }
    }

    if (!needs_escape) {
        out += value;
        return;
    }

    for (char character : value) {
        switch (character) {
        case '\\':
            out += "\\\\";
            break;
        case '"':
            out += "\\\"";
            break;
        case '\b':
            out += "\\b";
            break;
        case '\f':
            out += "\\f";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (static_cast<unsigned char>(character) < 0x20) {
                out += "\\u00";
                out += hex_digit((static_cast<unsigned char>(character) >> 4) & 0x0f);
                out += hex_digit(static_cast<unsigned char>(character) & 0x0f);
            } else {
                out += character;
            }
            break;
        }
    }
}
```
  Before `} // namespace json_utils` (line 459) add the full `JsonReaderV2` implementation — structurally identical to `JsonReader` (lines 90-457) with exactly these changes: `parse_long` uses `std::from_chars`; `parse_string` has the no-escape fast path + `std::move`; `parse_string_array` uses `push_back(std::move(value))`; `match_literal` takes a precomputed length; `skip_whitespace`/digit tests use `is_json_space`/`is_json_digit`:
```cpp
JsonReaderV2::JsonReaderV2(const std::string& input)
    : m_input(input)
{
}

bool JsonReaderV2::parse_root_object(const ObjectPropertyParser& property_parser, std::string* error_message)
{
    if (!parse_object(property_parser, error_message))
        return false;

    skip_whitespace();
    if (!is_at_end()) {
        set_error(error_message, "Unexpected trailing characters after JSON object.");
        return false;
    }

    return true;
}

bool JsonReaderV2::parse_object(const ObjectPropertyParser& property_parser, std::string* error_message)
{
    skip_whitespace();
    if (!consume('{')) {
        set_error(error_message, "Expected JSON object.");
        return false;
    }

    return parse_object_body(property_parser, error_message);
}

bool JsonReaderV2::parse_array(const ArrayValueParser& value_parser, std::string* error_message)
{
    if (!consume('[')) {
        set_error(error_message, "Expected array value.");
        return false;
    }

    bool first_value = true;
    while (true) {
        skip_whitespace();
        if (consume(']'))
            return true;

        if (!first_value) {
            if (!consume(',')) {
                set_error(error_message, "Expected ',' between array values.");
                return false;
            }
            skip_whitespace();
        }

        if (!value_parser(this, error_message))
            return false;

        first_value = false;
    }
}

bool JsonReaderV2::parse_string(std::string* value, std::string* error_message)
{
    if (value == nullptr) {
        set_error(error_message, "String output parameter must not be null.");
        return false;
    }

    if (!consume('"')) {
        set_error(error_message, "Expected string value.");
        return false;
    }

    // Fast path: scan to the closing quote; if the whole span holds no escape ('\') or control
    // character, assign it in one shot rather than appending character by character.
    const size_t size = m_input.size();
    size_t scan = m_position;
    while (scan < size) {
        const char character = m_input[scan];
        if (character == '"') {
            value->assign(m_input, m_position, scan - m_position);
            m_position = scan + 1;
            return true;
        }
        if (character == '\\' || static_cast<unsigned char>(character) < 0x20)
            break;
        ++scan;
    }

    // Slow path: an escape (or a control char, which is an error) is present. Seed the buffer with
    // the clean prefix already scanned, then resume the original character-by-character loop.
    std::string parsed;
    parsed.reserve((scan - m_position) + 16);
    parsed.assign(m_input, m_position, scan - m_position);
    m_position = scan;
    while (!is_at_end()) {
        const char character = m_input[m_position++];
        if (character == '"') {
            *value = std::move(parsed);
            return true;
        }

        if (character == '\\') {
            if (is_at_end()) {
                set_error(error_message, "Unexpected end of input in string escape sequence.");
                return false;
            }

            const char escaped = m_input[m_position++];
            switch (escaped) {
            case '"':
            case '\\':
            case '/':
                parsed += escaped;
                break;
            case 'b':
                parsed += '\b';
                break;
            case 'f':
                parsed += '\f';
                break;
            case 'n':
                parsed += '\n';
                break;
            case 'r':
                parsed += '\r';
                break;
            case 't':
                parsed += '\t';
                break;
            case 'u': {
                if (m_position + 4 > m_input.size()) {
                    set_error(error_message, "Incomplete unicode escape sequence in JSON string.");
                    return false;
                }

                unsigned int code_point = 0;
                for (int index = 0; index < 4; ++index) {
                    unsigned int nibble = 0;
                    if (!parse_hex_digit(m_input[m_position + index], &nibble)) {
                        set_error(error_message, "Invalid unicode escape sequence in JSON string.");
                        return false;
                    }
                    code_point = (code_point << 4) | nibble;
                }
                m_position += 4;

                if (code_point > 0x7f) {
                    set_error(error_message, "Unsupported unicode escape sequence in JSON string.");
                    return false;
                }

                parsed += static_cast<char>(code_point);
                break;
            }
            default:
                set_error(error_message, "Unsupported escape sequence in JSON string.");
                return false;
            }
        } else {
            if (static_cast<unsigned char>(character) < 0x20) {
                set_error(error_message, "JSON strings must escape control characters.");
                return false;
            }
            parsed += character;
        }
    }

    set_error(error_message, "Unterminated JSON string.");
    return false;
}

bool JsonReaderV2::parse_bool(bool* value, std::string* error_message)
{
    if (value == nullptr) {
        set_error(error_message, "Boolean output parameter must not be null.");
        return false;
    }

    if (match_literal("true", 4)) {
        *value = true;
        return true;
    }

    if (match_literal("false", 5)) {
        *value = false;
        return true;
    }

    set_error(error_message, "Expected boolean value.");
    return false;
}

bool JsonReaderV2::parse_integer(int* value, std::string* error_message)
{
    if (value == nullptr) {
        set_error(error_message, "Integer output parameter must not be null.");
        return false;
    }

    long parsed = 0;
    if (!parse_long(&parsed, error_message))
        return false;

    if (parsed < std::numeric_limits<int>::min() || parsed > std::numeric_limits<int>::max()) {
        set_error(error_message, "Integer value is out of range.");
        return false;
    }

    *value = static_cast<int>(parsed);
    return true;
}

bool JsonReaderV2::parse_long(long* value, std::string* error_message)
{
    if (value == nullptr) {
        set_error(error_message, "Long output parameter must not be null.");
        return false;
    }

    if (is_at_end()) {
        set_error(error_message, "Expected integer value.");
        return false;
    }

    const size_t start = m_position;
    if (m_input[m_position] == '-')
        ++m_position;

    if (m_position >= m_input.size() || !is_json_digit(m_input[m_position])) {
        set_error(error_message, "Expected integer value.");
        return false;
    }

    while (m_position < m_input.size() && is_json_digit(m_input[m_position]))
        ++m_position;

    const char* const first = m_input.data() + start;
    const char* const last = m_input.data() + m_position;
    long parsed = 0;
    const std::from_chars_result result = std::from_chars(first, last, parsed, 10);
    if (result.ec != std::errc() || result.ptr != last) {
        set_error(error_message, "Invalid integer value.");
        return false;
    }

    *value = parsed;
    return true;
}

bool JsonReaderV2::parse_string_array(std::vector<std::string>* values, std::string* error_message)
{
    if (values == nullptr) {
        set_error(error_message, "Array output parameter must not be null.");
        return false;
    }

    values->clear();
    return parse_array([values](JsonReaderV2* reader, std::string* nested_error_message) {
        std::string value;
        if (!reader->parse_string(&value, nested_error_message))
            return false;
        values->push_back(std::move(value));
        return true;
    },
        error_message);
}

bool JsonReaderV2::skip_value(std::string* error_message)
{
    skip_whitespace();
    if (is_at_end()) {
        set_error(error_message, "Expected JSON value.");
        return false;
    }

    const char current = m_input[m_position];
    if (current == '"') {
        std::string ignored;
        return parse_string(&ignored, error_message);
    }

    if (current == '{') {
        ++m_position;
        return parse_object_body([](const std::string&, JsonReaderV2* reader, std::string* nested_error_message) {
            return reader->skip_value(nested_error_message);
        },
            error_message);
    }

    if (current == '[') {
        ++m_position;
        bool first_value = true;
        while (true) {
            skip_whitespace();
            if (consume(']'))
                return true;

            if (!first_value) {
                if (!consume(',')) {
                    set_error(error_message, "Expected ',' between nested array values.");
                    return false;
                }
                skip_whitespace();
            }

            if (!skip_value(error_message))
                return false;

            first_value = false;
        }
    }

    if (current == 't' || current == 'f') {
        bool ignored = false;
        return parse_bool(&ignored, error_message);
    }

    if (current == '-' || is_json_digit(current)) {
        long ignored = 0;
        return parse_long(&ignored, error_message);
    }

    set_error(error_message, "Unsupported JSON value.");
    return false;
}

bool JsonReaderV2::parse_object_body(const ObjectPropertyParser& property_parser, std::string* error_message)
{
    bool first_property = true;
    while (true) {
        skip_whitespace();

        if (consume('}'))
            return true;

        if (!first_property) {
            if (!consume(',')) {
                set_error(error_message, "Expected ',' between object properties.");
                return false;
            }

            skip_whitespace();
        }

        std::string key;
        if (!parse_string(&key, error_message))
            return false;

        skip_whitespace();
        if (!consume(':')) {
            set_error(error_message, "Expected ':' after object key.");
            return false;
        }

        skip_whitespace();
        if (!property_parser(key, this, error_message))
            return false;

        first_property = false;
    }
}

bool JsonReaderV2::consume(char expected)
{
    if (m_position >= m_input.size() || m_input[m_position] != expected)
        return false;

    ++m_position;
    return true;
}

bool JsonReaderV2::match_literal(const char* literal, size_t literal_length)
{
    if (m_input.compare(m_position, literal_length, literal) != 0)
        return false;

    m_position += literal_length;
    return true;
}

void JsonReaderV2::skip_whitespace()
{
    while (m_position < m_input.size() && is_json_space(m_input[m_position]))
        ++m_position;
}

bool JsonReaderV2::is_at_end() const
{
    return m_position >= m_input.size();
}
```

- [ ] **Step 4: Run tests, expect PASS.** `scripts/rots-docker.sh test '--gtest_filter=JsonPerf.*'` → `[  PASSED  ] 2 tests.` (`JsonPerf.JsonReaderV2MatchesBaselineParse`, `JsonPerf.AppendEscapedMatchesEscapeJsonString`). Also confirm no regression in the existing reader suite: `scripts/rots-docker.sh test '--gtest_filter=JsonUtils.*'` (still green).

- [ ] **Step 5: Commit.** `git add src/json_utils.h src/json_utils.cpp src/tests/json_perf_tests.cpp src/CMakeLists.txt && git commit -m "feat(json): add JsonReaderV2 + append_escaped_json_string (parallel v2 baseline)

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"`

---

### Task 4: Memoized skill/talk lookups + templated nested parsers + `deserialize..._v2a` (contract §C-deserialize, part 1)
**Files:**
- Modify `src/character_json.cpp:1` (add `#include <unordered_map>`), `:679` (add memoized lookups after `color_index_for_key`), and the ~20 nested-parser definitions in `:733-1461` (template the reader type), `:1482` (add `deserialize_character_v2_dispatch` template just before the anon-namespace close), `:1989` (add `deserialize_character_from_json_v2a` after v1).
- Modify `src/character_json.h:151` (add v2a decl).
- Modify `src/tests/json_perf_tests.cpp` (add character includes + fixtures + three tests).

**Interfaces:**
- Consumes: `json_utils::JsonReader` (Task 3 leaves it untouched), `character_json::serialize_character_to_json`, `character_data_from_store`, `apply_character_data_to_store` (v1, all existing).
- Produces: `bool character_json::deserialize_character_from_json_v2a(const std::string&, CharacterData*, std::string* = nullptr);`; file-local `template <class Reader> bool deserialize_character_v2_dispatch(const std::string&, CharacterData*, std::string*)` and `int skill_index_for_key_memoized(const std::string&)` / `int talk_index_for_key_memoized(const std::string&)` (Task 5 reuses the dispatch template + the templated parsers).

- [ ] **Step 1: Write the failing tests.** Append to `src/tests/json_perf_tests.cpp` — add to the include block (top of file): `#include "../character_json.h"`, `#include "../structs.h"`, `#include "../utils.h"`, `#include <cstring>`. Then append at end of file:
```cpp
namespace {

// Builds a valid char_file_u to exercise the deserialize paths. heavy=true populates every skill
// slot (and all talks) to stress the per-key index lookups that dominate load cost. Mirrors the
// proven-round-tripping fixture in save_benchmark_tests.cpp; no affects are set, so affected_flags
// stay trivially consistent.
char_file_u make_perf_character(bool heavy)
{
    char_file_u stored {};
    std::snprintf(stored.name, sizeof(stored.name), "%s", "aragorn");
    std::snprintf(stored.title, sizeof(stored.title), "%s", "the Ranger");
    std::snprintf(stored.description, sizeof(stored.description), "%s", "A ranger from the north.");
    stored.sex = SEX_MALE;
    stored.race = RACE_HUMAN;
    stored.bodytype = 1;
    stored.level = 12;
    stored.language = LANG_HUMAN;
    stored.birth = 1700000000;
    stored.played = 456;
    stored.weight = 190;
    stored.height = 72;
    stored.hometown = 7;
    stored.last_logon = 1700000100;
    stored.points.gold = 1234;
    stored.points.exp = 5678;
    stored.specials2.idnum = 4242;
    stored.specials2.load_room = 3001;
    stored.specials2.tactics = TACTICS_BERSERK;
    stored.specials2.shooting = SHOOTING_FAST;
    stored.specials2.casting = CASTING_SLOW;
    stored.specials2.two_handed = 1;
    stored.profs.colors[COLOR_MAGIC] = CBMAG;
    stored.profs.prof_level[PROF_WARRIOR] = 12;
    stored.profs.prof_coof[PROF_WARRIOR] = 34;
    stored.skills[0] = 1;
    stored.skills[1] = 10;
    stored.skills[2] = 95;
    stored.talks[0] = 100;
    stored.talks[1] = 75;
    if (heavy) {
        for (int index = 0; index < MAX_SKILLS; ++index)
            stored.skills[index] = (index % 50) + 1;
        for (int index = 0; index < MAX_TOUNGE; ++index)
            stored.talks[index] = (index * 30) + 10;
    }
    return stored;
}

using DeserializeFn = bool (*)(const std::string&, character_json::CharacterData*, std::string*);

// Deserialize json with fn, then apply into a zeroed char_file_u so two paths can be byte-compared.
bool deserialize_and_apply(DeserializeFn fn, const std::string& json, char_file_u* out, std::string* error)
{
    character_json::CharacterData character;
    if (!fn(json, &character, error))
        return false;
    return character_json::apply_character_data_to_store(character, out, error);
}

// Serialize a v1 char of the given tier, then assert the v2 deserializer yields a byte-identical
// stored struct vs the v1 deserializer. (If the heavy ASSERT on v1 ever fails, a skill-name slug
// collision exists in the game tables -- not expected with current data; trim the populated set.)
void expect_deserialize_matches_v1(DeserializeFn v2_fn, bool heavy)
{
    const char_file_u stored = make_perf_character(heavy);
    const character_json::CharacterData character = character_json::character_data_from_store(stored);
    const std::string json = character_json::serialize_character_to_json(character);

    char_file_u s1 {};
    char_file_u s2 {};
    std::string e1;
    std::string e2;
    ASSERT_TRUE(deserialize_and_apply(&character_json::deserialize_character_from_json, json, &s1, &e1)) << e1;
    ASSERT_TRUE(deserialize_and_apply(v2_fn, json, &s2, &e2)) << e2;
    EXPECT_EQ(0, std::memcmp(&s1, &s2, sizeof(char_file_u)));
}

} // namespace

TEST(JsonPerf, DeserializeV2aMatchesV1)
{
    expect_deserialize_matches_v1(&character_json::deserialize_character_from_json_v2a, /*heavy=*/false);
    expect_deserialize_matches_v1(&character_json::deserialize_character_from_json_v2a, /*heavy=*/true);
}

TEST(JsonPerf, MemoizedSkillTalkLookupMatchesSlowScan)
{
    // The memoized lookups are file-local to character_json.cpp, so they are exercised through the
    // public dispatch: v1 routes skill/talk keys through the slow linear scan, v2a routes them
    // through the memoized maps. For every index we serialize a single-entry skills/talks object
    // (its canonical key) and confirm v1 and v2a map it back to the SAME index.
    const character_json::CharacterData base = character_json::character_data_from_store(make_perf_character(false));

    for (int index = 0; index < MAX_SKILLS; ++index) {
        character_json::CharacterData character = base;
        character.skills.assign(MAX_SKILLS, 0);
        character.talks.assign(MAX_TOUNGE, 0);
        character.skills[index] = 7;
        const std::string json = character_json::serialize_character_to_json(character);

        character_json::CharacterData v1;
        character_json::CharacterData v2a;
        std::string e1;
        std::string e2;
        ASSERT_TRUE(character_json::deserialize_character_from_json(json, &v1, &e1)) << "skill " << index << ": " << e1;
        ASSERT_TRUE(character_json::deserialize_character_from_json_v2a(json, &v2a, &e2)) << "skill " << index << ": " << e2;
        EXPECT_EQ(v1.skills, v2a.skills) << "skill index " << index;
        EXPECT_EQ(7, v2a.skills[index]) << "skill index " << index;
    }

    for (int index = 0; index < MAX_TOUNGE; ++index) {
        character_json::CharacterData character = base;
        character.skills.assign(MAX_SKILLS, 0);
        character.talks.assign(MAX_TOUNGE, 0);
        character.talks[index] = 9;
        const std::string json = character_json::serialize_character_to_json(character);

        character_json::CharacterData v1;
        character_json::CharacterData v2a;
        std::string e1;
        std::string e2;
        ASSERT_TRUE(character_json::deserialize_character_from_json(json, &v1, &e1)) << "talk " << index << ": " << e1;
        ASSERT_TRUE(character_json::deserialize_character_from_json_v2a(json, &v2a, &e2)) << "talk " << index << ": " << e2;
        EXPECT_EQ(v1.talks, v2a.talks) << "talk index " << index;
        EXPECT_EQ(9, v2a.talks[index]) << "talk index " << index;
    }
}

TEST(JsonPerf, UnknownSkillKeyFailsIdenticallyForV1AndV2a)
{
    // Unknown key -> index -1 -> "Unknown skill key" error, the same for the slow scan (v1) and the
    // memoized map (v2a). Build a skills-empty character (serializes `"skills": {}`), inject a bogus
    // key, and assert both paths reject it with the identical message.
    character_json::CharacterData character = character_json::character_data_from_store(make_perf_character(false));
    character.skills.assign(MAX_SKILLS, 0);
    character.talks.assign(MAX_TOUNGE, 0);
    std::string json = character_json::serialize_character_to_json(character);

    const std::string empty_skills = "\"skills\": {}";
    const std::string bogus_skills = "\"skills\": {\"totally_not_a_real_skill\": 5}";
    const size_t at = json.find(empty_skills);
    ASSERT_NE(std::string::npos, at);
    json.replace(at, empty_skills.size(), bogus_skills);

    character_json::CharacterData parsed;
    std::string e1;
    std::string e2;
    EXPECT_FALSE(character_json::deserialize_character_from_json(json, &parsed, &e1));
    EXPECT_FALSE(character_json::deserialize_character_from_json_v2a(json, &parsed, &e2));
    EXPECT_EQ(e1, e2);
}
```

- [ ] **Step 2: Run them, expect FAIL (compile error).** `scripts/rots-docker.sh test '--gtest_filter=JsonPerf.Deserialize*:JsonPerf.Memoized*:JsonPerf.Unknown*'` → build fails: `error: 'deserialize_character_from_json_v2a' is not a member of 'character_json'`.

- [ ] **Step 3a: Add the memoized lookups.** In `src/character_json.cpp` add `#include <unordered_map>` to the include block (after line 7). Immediately after `color_index_for_key` (ends line 679) add:
```cpp
    int skill_index_for_key_memoized(const std::string& key)
    {
        // Lazy slug->index map for skills; built once, INSERT-IF-ABSENT so the LOWEST index wins on a
        // slug collision -- exactly matching skill_index_for_key's first-match linear scan.
        static const std::unordered_map<std::string, int> index_by_key = [] {
            std::unordered_map<std::string, int> table;
            table.reserve(MAX_SKILLS * 2);
            for (int index = 0; index < MAX_SKILLS; ++index)
                table.emplace(skill_key_for_index(index), index);
            return table;
        }();
        const auto found = index_by_key.find(key);
        return found != index_by_key.end() ? found->second : -1;
    }

    int talk_index_for_key_memoized(const std::string& key)
    {
        // Lazy slug->index map for talks; INSERT-IF-ABSENT (lowest index wins) like talk_index_for_key.
        static const std::unordered_map<std::string, int> index_by_key = [] {
            std::unordered_map<std::string, int> table;
            for (int index = 0; index < MAX_TOUNGE; ++index)
                table.emplace(talk_key_for_index(index), index);
            return table;
        }();
        const auto found = index_by_key.find(key);
        return found != index_by_key.end() ? found->second : -1;
    }
```
(`unordered_map::emplace` is a no-op when the key already exists, so the first/lowest insertion wins — byte-identical to the slow scan.)

- [ ] **Step 3b: Template the nested parsers (mechanical mirror).** Apply the SAME edit to each parser listed below: prefix the definition with `template <class Reader>`, change its leading `json_utils::JsonReader* reader` parameter to `Reader* reader`, and change every inner lambda's `json_utils::JsonReader* nested_reader` parameter to `Reader* nested_reader`. **Change nothing else** — all bodies, captures, error strings, and call sites stay verbatim. Leave `parse_named_integer_object`'s `const std::function<int(const std::string&)>& index_for_key` parameter unchanged. Definition order already has every callee above its caller, so unqualified lookup at template-definition time resolves them — do not reorder.

  Parsers to template (current lines): `parse_color_value_object` (:733), `parse_color_setting_value` (:799), `parse_colors_object` (:839), `parse_string_array` (:909), `parse_integer_array` (:918), `parse_named_integer_object` (:940), `parse_ability_object` (:972), `parse_points_object` (:1020), `parse_conditions_object` (:1089), `parse_timers_object` (:1119), `parse_profession_object` (:1152), `parse_affect_object` (:1173), `parse_affects_array` (:1215), `parse_identity_object` (:1237), `parse_progression_object` (:1277), `parse_abilities_object` (:1311), `parse_professions_object` (:1333), `parse_flags_object` (:1365), `parse_perception_object` (:1393), `parse_state_object` (:1415).

  Representative transformed helper (the rest follow this exact pattern) — `parse_identity_object` after the edit:
```cpp
    template <class Reader>
    bool parse_identity_object(Reader* reader, CharacterData* character, std::string* error_message)
    {
        bool saw_idnum = false;
        bool saw_race = false;
        bool saw_sex = false;
        bool saw_bodytype = false;
        bool saw_language = false;
        bool saw_hometown = false;
        bool saw_weight = false;
        bool saw_height = false;
        if (!reader->parse_object([character, &saw_idnum, &saw_race, &saw_sex, &saw_bodytype, &saw_language, &saw_hometown, &saw_weight, &saw_height](const std::string& key, Reader* nested_reader, std::string* nested_error_message) {
            if (key == "idnum")
                return saw_idnum = true, nested_reader->parse_long(&character->idnum, nested_error_message);
            if (key == "race")
                return saw_race = true, nested_reader->parse_integer(&character->race, nested_error_message);
            if (key == "sex")
                return saw_sex = true, nested_reader->parse_integer(&character->sex, nested_error_message);
            if (key == "bodytype")
                return saw_bodytype = true, nested_reader->parse_integer(&character->bodytype, nested_error_message);
            if (key == "language")
                return saw_language = true, nested_reader->parse_integer(&character->language, nested_error_message);
            if (key == "hometown")
                return saw_hometown = true, nested_reader->parse_integer(&character->hometown, nested_error_message);
            if (key == "weight")
                return saw_weight = true, nested_reader->parse_integer(&character->weight, nested_error_message);
            if (key == "height")
                return saw_height = true, nested_reader->parse_integer(&character->height, nested_error_message);
            return nested_reader->skip_value(nested_error_message);
        },
            error_message))
            return false;

        if (!saw_idnum || !saw_race || !saw_sex || !saw_bodytype || !saw_language || !saw_hometown || !saw_weight || !saw_height) {
            set_error(error_message, "Identity object was missing one or more required fields.");
            return false;
        }

        return true;
    }
```
  Note: v1's `deserialize_character_from_json` (:1869-1989) is **NOT** edited — its call sites still pass `json_utils::JsonReader*`, so `Reader` deduces `JsonReader` and behavior is byte-identical (the contract's measurement-integrity requirement, pinned by `CharacterJson.SerializesAndDeserializesCharacterJsonRoundTrip`).

- [ ] **Step 3c: Add the dispatch template + v2a.** Just before the anonymous-namespace close (`} // namespace` at :1484, after `has_all_required_character_sections`) add `deserialize_character_v2_dispatch`. Its body is a **verbatim copy of v1 `deserialize_character_from_json` lines 1871-1989** (everything from the null check through `return true;`), wrapped as a template, with exactly these four textual changes:
  1. `json_utils::JsonReader reader(json);` → `Reader reader(json);` (v1 line 1877).
  2. The root lambda signature `...](const std::string& key, json_utils::JsonReader* nested_reader, std::string* nested_error_message) {` → `Reader* nested_reader` (v1 line 1899).
  3. talks call site (v1 line 1933): `..., "talk", talk_index_for_key, nested_error_message)` → `talk_index_for_key_memoized`.
  4. skills call site (v1 line 1935): `..., "skill", skill_index_for_key, nested_error_message)` → `skill_index_for_key_memoized`.

  The `colors` call (v1 line 1925) stays unchanged — `parse_colors_object` keeps using the slow `color_index_for_key` internally (negligible, per contract). The signature is:
```cpp
    template <class Reader>
    bool deserialize_character_v2_dispatch(const std::string& json, CharacterData* character, std::string* error_message)
    {
        // <<< verbatim copy of deserialize_character_from_json body (character_json.cpp:1871-1989)
        //     with the 4 changes above: Reader reader(json); Reader* nested_reader; and the talks/
        //     skills lookups swapped to *_memoized. >>>
    }
```
  Then in the public section, immediately after v1 `deserialize_character_from_json` closes (:1989) add:
```cpp
bool deserialize_character_from_json_v2a(const std::string& json, CharacterData* character, std::string* error_message)
{
    return deserialize_character_v2_dispatch<json_utils::JsonReader>(json, character, error_message);
}
```
  And add the declaration to `src/character_json.h` after line 151:
```cpp
bool deserialize_character_from_json_v2a(const std::string& json, CharacterData* character, std::string* error_message = nullptr);
```

- [ ] **Step 4: Run tests, expect PASS.** `scripts/rots-docker.sh test '--gtest_filter=JsonPerf.Deserialize*:JsonPerf.Memoized*:JsonPerf.Unknown*'` → `[  PASSED  ] 3 tests.` Then run the v1 pinning oracle to prove templating stayed byte-neutral: `scripts/rots-docker.sh test '--gtest_filter=CharacterJson.*'` and `scripts/rots-docker.sh test '--gtest_filter=SaveBenchmark.ProfilesBothDirectionsAndRoundTrips'` — both still green (any pre-existing 32-bit baseline red here is unrelated; gate by the specific names above).

- [ ] **Step 5: Commit.** `git add src/character_json.cpp src/character_json.h src/tests/json_perf_tests.cpp && git commit -m "feat(character_json): memoized skill/talk lookups + templated parsers + deserialize v2a

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"`

---

### Task 5: `deserialize..._v2b` (JsonReaderV2 through the templated parsers) (contract §C-deserialize, part 2)
**Files:**
- Modify `src/character_json.cpp` (add `deserialize_character_from_json_v2b` after `deserialize_character_from_json_v2a`).
- Modify `src/character_json.h` (add v2b decl after the v2a decl).
- Modify `src/tests/json_perf_tests.cpp` (add the v2b equivalence test).

**Interfaces:**
- Consumes: `deserialize_character_v2_dispatch<...>` (Task 4), the templated nested parsers (Task 4), `json_utils::JsonReaderV2` (Task 3), and the `make_perf_character` / `deserialize_and_apply` / `expect_deserialize_matches_v1` fixtures (Task 4).
- Produces: `bool character_json::deserialize_character_from_json_v2b(const std::string&, CharacterData*, std::string* = nullptr);` (consumed by the savebench L3b compare stage in the wiring workstream).

- [ ] **Step 1: Write the failing test.** Append to `src/tests/json_perf_tests.cpp`:
```cpp
TEST(JsonPerf, DeserializeV2bMatchesV1)
{
    expect_deserialize_matches_v1(&character_json::deserialize_character_from_json_v2b, /*heavy=*/false);
    expect_deserialize_matches_v1(&character_json::deserialize_character_from_json_v2b, /*heavy=*/true);
}

TEST(JsonPerf, DeserializeV2bMatchesV2a)
{
    // v2a (JsonReader + memoized) vs v2b (JsonReaderV2 + memoized): the only difference is the
    // reader, so the applied structs must match byte-for-byte across both tiers.
    for (bool heavy : { false, true }) {
        const char_file_u stored = make_perf_character(heavy);
        const character_json::CharacterData character = character_json::character_data_from_store(stored);
        const std::string json = character_json::serialize_character_to_json(character);

        char_file_u sa {};
        char_file_u sb {};
        std::string ea;
        std::string eb;
        ASSERT_TRUE(deserialize_and_apply(&character_json::deserialize_character_from_json_v2a, json, &sa, &ea)) << ea;
        ASSERT_TRUE(deserialize_and_apply(&character_json::deserialize_character_from_json_v2b, json, &sb, &eb)) << eb;
        EXPECT_EQ(0, std::memcmp(&sa, &sb, sizeof(char_file_u))) << "heavy=" << heavy;
    }
}
```

- [ ] **Step 2: Run it, expect FAIL (compile error).** `scripts/rots-docker.sh test '--gtest_filter=JsonPerf.DeserializeV2b*'` → build fails: `error: 'deserialize_character_from_json_v2b' is not a member of 'character_json'`.

- [ ] **Step 3: Implement v2b.** In `src/character_json.cpp`, immediately after `deserialize_character_from_json_v2a` add:
```cpp
bool deserialize_character_from_json_v2b(const std::string& json, CharacterData* character, std::string* error_message)
{
    return deserialize_character_v2_dispatch<json_utils::JsonReaderV2>(json, character, error_message);
}
```
  This is the first instantiation of the templated parsers with `JsonReaderV2`; if any helper signature was missed in Task 4 Step 3b it surfaces here as a template-instantiation error. Add the declaration to `src/character_json.h` after the v2a decl:
```cpp
bool deserialize_character_from_json_v2b(const std::string& json, CharacterData* character, std::string* error_message = nullptr);
```

- [ ] **Step 4: Run tests, expect PASS.** `scripts/rots-docker.sh test '--gtest_filter=JsonPerf.*'` → all `JsonPerf.*` green (`JsonReaderV2MatchesBaselineParse`, `AppendEscapedMatchesEscapeJsonString`, `DeserializeV2aMatchesV1`, `MemoizedSkillTalkLookupMatchesSlowScan`, `UnknownSkillKeyFailsIdenticallyForV1AndV2a`, `DeserializeV2bMatchesV1`, `DeserializeV2bMatchesV2a`). Re-confirm the v1 oracle: `scripts/rots-docker.sh test '--gtest_filter=CharacterJson.SerializesAndDeserializesCharacterJsonRoundTrip'` still green.

- [ ] **Step 5: Commit.** `git add src/character_json.cpp src/character_json.h src/tests/json_perf_tests.cpp && git commit -m "feat(character_json): deserialize v2b (JsonReaderV2 path) + equivalence gates

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"`

---

### Task 6: serialize_character_to_json_v2a (reserve + std::to_chars, slow escape/keys)

**Files:**
- Modify `src/character_json.h:150` — add the `_v2a` declaration after the v1 `serialize_character_to_json` decl.
- Modify `src/character_json.cpp:1-11` (includes: add `<charconv>`, `<cstddef>`); insert the new anon-namespace helpers after `write_affect` (`character_json.cpp:907`, just before `parse_string_array` at `:909`); insert `serialize_character_to_json_v2a` at `character_json` scope right after `serialize_character_to_json` (after `character_json.cpp:1867`).
- Create `src/tests/json_perf_tests.cpp` (suite `JsonPerf`) — if the deserialize workstream already created it, APPEND these cases + helpers instead of recreating.
- Modify `src/CMakeLists.txt:128` — add `tests/json_perf_tests.cpp` to `ROTS_TEST_SOURCES` (skip if already added).

**Interfaces:**
- Consumes (from contract / existing v1): `character_json::character_data_from_store` (`character_json.h:147`); v1 `serialize_character_to_json` (`character_json.cpp:1729`) as the byte oracle; anon helpers `collect_non_default_color_slots` (`:714`), `collect_non_zero_named_values` (`:859`), `skill_key_for_index`/`talk_key_for_index` (`:642`/`:635`), `color_key_for_index` (`:647`), `default_color_setting`/`is_default_color_value`/`normalize_color_setting` (`:146`/`:173`/`:203`), `json_utils::escape_json_string` (`json_utils.cpp:47`); the v1 write-helper byte layouts (`write_ability :870`, `write_profession :885`, `write_affect :895`, `write_string_array :591`, `write_integer_array :602`, `write_named_integer_object :681`, `write_color_value :692`, `write_color_setting :705`).
- Produces (later tasks rely on these EXACT signatures): `std::string character_json::serialize_character_to_json_v2a(const CharacterData&)`; anon-namespace `JsonWriter` + shared non-escaping helpers `write_ability_v2`/`write_profession_v2`/`write_integer_array_v2`/`write_color_value_v2`/`write_color_setting_v2` (reused by Task 7); the `JsonPerf` test fixture helpers `make_light_data()` / `make_heavy_data()` (reused by Task 7).

- [ ] **Step 1: Write the failing test** — create `src/tests/json_perf_tests.cpp`, add `tests/json_perf_tests.cpp` to `ROTS_TEST_SOURCES` after `tests/json_utils_tests.cpp` (`CMakeLists.txt:128`), add the v2a decl + a one-line empty stub so it links and the assertion (not the linker) goes red. Decl to add at `character_json.h` after line 150:
```cpp
std::string serialize_character_to_json_v2a(const CharacterData& character);
```
Temporary stub to add after `serialize_character_to_json` (`character_json.cpp:1867`), to be replaced in Step 3:
```cpp
std::string serialize_character_to_json_v2a(const CharacterData& character)
{
    (void)character;
    return std::string();
}
```
Full test file (`src/tests/json_perf_tests.cpp`, LF):
```cpp
#include "../character_json.h"

#include "../color.h"
#include "../structs.h"
#include "../utils.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <string>

namespace {

// Minimal stored character: name only, every collection empty. Exercises the empty-object
// ("colors"/"talks"/"skills" == {}) and empty-array ("affects" == []) serialize paths.
character_json::CharacterData make_light_data()
{
    char_file_u stored {};
    std::snprintf(stored.name, sizeof(stored.name), "%s", "frodo");
    return character_json::character_data_from_store(stored);
}

// Fully-populated stored character: all 256 skills + all talks non-zero, truecolor color
// settings, preference/tactics flags, and a description carrying ", \\, \n, \t so the escape
// slow path is exercised. Two affects are appended afterward to exercise the affects array
// and write_affect (one with flags, one empty).
character_json::CharacterData make_heavy_data()
{
    char_file_u stored {};
    std::snprintf(stored.name, sizeof(stored.name), "%s", "aragorn");
    std::snprintf(stored.title, sizeof(stored.title), "%s", "the Ranger");
    std::snprintf(stored.description, sizeof(stored.description), "%s",
        "Quote: \" Backslash: \\ Newline:\n Tab:\t done.");
    stored.sex = SEX_MALE;
    stored.race = RACE_HUMAN;
    stored.bodytype = 1;
    stored.level = 12;
    stored.language = LANG_HUMAN;
    stored.birth = 1700000000;
    stored.played = 456;
    stored.weight = 190;
    stored.height = 72;
    stored.hometown = 7;
    stored.last_logon = 1700000100;
    stored.points.gold = 1234;
    stored.points.exp = 5678;
    stored.specials2.idnum = 4242;
    stored.specials2.load_room = 3001;
    stored.specials2.act = 0;
    stored.specials2.pref = 1L << 5;
    stored.specials2.tactics = TACTICS_BERSERK;
    stored.specials2.shooting = SHOOTING_FAST;
    stored.specials2.casting = CASTING_SLOW;
    stored.specials2.two_handed = 1;
    stored.profs.colors[COLOR_MAGIC] = CBMAG;
    stored.profs.colors[COLOR_WEATHER] = CBCYN;
    stored.profs.color_settings[COLOR_MAGIC].foreground.mode = COLOR_VALUE_TRUECOLOR;
    stored.profs.color_settings[COLOR_MAGIC].foreground.ansi = CBMAG;
    stored.profs.color_settings[COLOR_MAGIC].foreground.red = 180;
    stored.profs.color_settings[COLOR_MAGIC].foreground.green = 80;
    stored.profs.color_settings[COLOR_MAGIC].foreground.blue = 255;
    stored.profs.color_settings[COLOR_WEATHER].background.mode = COLOR_VALUE_TRUECOLOR;
    stored.profs.color_settings[COLOR_WEATHER].background.ansi = CBBLU;
    stored.profs.color_settings[COLOR_WEATHER].background.red = 10;
    stored.profs.color_settings[COLOR_WEATHER].background.green = 20;
    stored.profs.color_settings[COLOR_WEATHER].background.blue = 35;
    stored.profs.prof_level[PROF_WARRIOR] = 12;
    stored.profs.prof_coof[PROF_WARRIOR] = 34;
    for (int index = 0; index < MAX_SKILLS; ++index)
        stored.skills[index] = static_cast<byte>((index % 200) + 1);
    for (int index = 0; index < MAX_TOUNGE; ++index)
        stored.talks[index] = static_cast<byte>((index * 30) + 5);

    character_json::CharacterData character = character_json::character_data_from_store(stored);

    character_json::AffectData first_affect;
    first_affect.type = 5;
    first_affect.duration = 10;
    first_affect.time_phase = 1;
    first_affect.modifier = -2;
    first_affect.location = 3;
    first_affect.counter = 4;
    first_affect.flags = { "detect_hidden", "infrared" };
    character.affects.push_back(first_affect);

    character_json::AffectData second_affect;
    second_affect.type = 9;
    second_affect.duration = -1;
    second_affect.time_phase = 0;
    second_affect.modifier = 7;
    second_affect.location = 1;
    second_affect.counter = 0;
    character.affects.push_back(second_affect);

    return character;
}

} // namespace

TEST(JsonPerf, SerializeV2aMatchesV1Light)
{
    const character_json::CharacterData character = make_light_data();
    EXPECT_EQ(character_json::serialize_character_to_json(character),
        character_json::serialize_character_to_json_v2a(character));
}

TEST(JsonPerf, SerializeV2aMatchesV1Heavy)
{
    const character_json::CharacterData character = make_heavy_data();
    EXPECT_EQ(character_json::serialize_character_to_json(character),
        character_json::serialize_character_to_json_v2a(character));
}
```

- [ ] **Step 2: Run it, expect FAIL** — `scripts/rots-docker.sh test '--gtest_filter=JsonPerf.SerializeV2a*'`. Expected: builds, then both cases RED with `Expected equality of these values:` where the v2a side is the empty string (stub) and the v1 side is the full JSON document.

- [ ] **Step 3: Implement** — add includes `#include <charconv>` and `#include <cstddef>` after `#include <algorithm>` (`character_json.cpp:7`). Insert the helper block in the anonymous namespace after `write_affect` (after `character_json.cpp:907`). This is NET-NEW string-appending logic mirroring the v1 ostringstream helpers — each `writer.number(x)` replaces `output << x` and each `writer.raw("…")` replaces `output << "…"`, so byte output is unchanged (oracle = the Step-1 byte-equality test):
```cpp
// ---- v2 string-appending serialize support (parallel to the ostringstream write_* helpers) ----

// Initial reserve() size for the v2 serialize buffer; a perf hint only (the buffer grows if a
// character serializes larger), so it never affects output bytes.
constexpr std::size_t kSerializeReserveHint = 8192;

// Reusable JSON text accumulator for the v2 serializers: owns the growing output buffer and one
// scratch buffer reused for every integer->text conversion, so a serialize pass does no per-field
// heap churn (v1's ostringstream + .str() double-allocates). One instance per serialize call.
class JsonWriter {
public:
    // reserve_hint pre-sizes the buffer to a typical serialized document so growth reallocs are rare.
    explicit JsonWriter(std::size_t reserve_hint)
    {
        m_out.reserve(reserve_hint);
    }

    // Appends a NUL-terminated literal fragment verbatim (punctuation, field names, bool literals).
    void raw(const char* literal)
    {
        m_out.append(literal);
    }

    // Appends a single character verbatim.
    void raw(char character)
    {
        m_out.push_back(character);
    }

    // Appends a std::string fragment verbatim (no escaping).
    void raw(const std::string& fragment)
    {
        m_out.append(fragment);
    }

    // Appends the decimal text of a signed integer via std::to_chars (locale-free, allocation-free),
    // reusing m_scratch. Templated so int and long share one body; integer overloads only (g++ 9.4).
    template <class IntT>
    void number(IntT value)
    {
        const std::to_chars_result result = std::to_chars(m_scratch, m_scratch + sizeof(m_scratch), value);
        m_out.append(m_scratch, result.ptr);
    }

    // Direct buffer access for in-place escape helpers (append_escaped_json_string, Task 7).
    std::string& buffer()
    {
        return m_out;
    }

    // Hands the finished buffer to the caller by move (no final whole-buffer copy like v1's .str()).
    std::string take()
    {
        return std::move(m_out);
    }

private:
    // The growing serialized-JSON text; reserved up front, returned by take() at the end.
    std::string m_out;
    // Reusable scratch for std::to_chars; 24 bytes covers any 64-bit value plus sign.
    char m_scratch[24];
};

void write_ability_v2(JsonWriter& writer, const AbilityData& ability)
{
    writer.raw("{\n");
    writer.raw("      \"str\": ");
    writer.number(ability.str);
    writer.raw(",\n");
    writer.raw("      \"lea\": ");
    writer.number(ability.lea);
    writer.raw(",\n");
    writer.raw("      \"intel\": ");
    writer.number(ability.intel);
    writer.raw(",\n");
    writer.raw("      \"wil\": ");
    writer.number(ability.wil);
    writer.raw(",\n");
    writer.raw("      \"dex\": ");
    writer.number(ability.dex);
    writer.raw(",\n");
    writer.raw("      \"con\": ");
    writer.number(ability.con);
    writer.raw(",\n");
    writer.raw("      \"hit\": ");
    writer.number(ability.hit);
    writer.raw(",\n");
    writer.raw("      \"mana\": ");
    writer.number(ability.mana);
    writer.raw(",\n");
    writer.raw("      \"move\": ");
    writer.number(ability.move);
    writer.raw("\n");
    writer.raw("    }");
}

void write_profession_v2(JsonWriter& writer, const char* name, const ProfessionData& profession)
{
    writer.raw("    \"");
    writer.raw(name);
    writer.raw("\": {\n");
    writer.raw("      \"level\": ");
    writer.number(profession.level);
    writer.raw(",\n");
    writer.raw("      \"points\": ");
    writer.number(profession.points);
    writer.raw(",\n");
    writer.raw("      \"coeff\": ");
    writer.number(profession.coeff);
    writer.raw(",\n");
    writer.raw("      \"experience\": ");
    writer.number(profession.experience);
    writer.raw("\n");
    writer.raw("    }");
}

void write_integer_array_v2(JsonWriter& writer, const std::vector<int>& values)
{
    writer.raw('[');
    for (size_t index = 0; index < values.size(); ++index) {
        if (index > 0)
            writer.raw(", ");
        writer.number(values[index]);
    }
    writer.raw(']');
}

void write_color_value_v2(JsonWriter& writer, const ColorValueData& value)
{
    writer.raw('{');
    if (value.mode == COLOR_VALUE_DEFAULT) {
        writer.raw("\"mode\": \"default\"");
    } else if (value.mode == COLOR_VALUE_ANSI16) {
        writer.raw("\"mode\": \"ansi16\", \"value\": ");
        writer.number(value.value);
    } else {
        writer.raw("\"mode\": \"truecolor\", \"value\": ");
        writer.number(value.value);
        writer.raw(", \"r\": ");
        writer.number(value.red);
        writer.raw(", \"g\": ");
        writer.number(value.green);
        writer.raw(", \"b\": ");
        writer.number(value.blue);
    }
    writer.raw('}');
}

void write_color_setting_v2(JsonWriter& writer, const ColorSettingData& setting)
{
    writer.raw("{\"foreground\": ");
    write_color_value_v2(writer, setting.foreground);
    writer.raw(", \"background\": ");
    write_color_value_v2(writer, setting.background);
    writer.raw('}');
}

void write_string_array_v2a(JsonWriter& writer, const std::vector<std::string>& values)
{
    writer.raw('[');
    for (size_t index = 0; index < values.size(); ++index) {
        if (index > 0)
            writer.raw(", ");
        writer.raw('"');
        writer.raw(json_utils::escape_json_string(values[index]));
        writer.raw('"');
    }
    writer.raw(']');
}

void write_named_integer_object_v2a(JsonWriter& writer, const std::vector<NamedValue>& values)
{
    writer.raw('{');
    for (size_t index = 0; index < values.size(); ++index) {
        if (index > 0)
            writer.raw(", ");
        writer.raw('"');
        writer.raw(json_utils::escape_json_string(values[index].key));
        writer.raw("\": ");
        writer.number(values[index].value);
    }
    writer.raw('}');
}

void write_affect_v2a(JsonWriter& writer, const AffectData& affect)
{
    writer.raw("{\"type\": ");
    writer.number(affect.type);
    writer.raw(", \"duration\": ");
    writer.number(affect.duration);
    writer.raw(", \"time_phase\": ");
    writer.number(affect.time_phase);
    writer.raw(", \"modifier\": ");
    writer.number(affect.modifier);
    writer.raw(", \"location\": ");
    writer.number(affect.location);
    writer.raw(", \"counter\": ");
    writer.number(affect.counter);
    writer.raw(", \"flags\": ");
    write_string_array_v2a(writer, affect.flags);
    writer.raw('}');
}
```
Then REPLACE the Step-1 stub with the full v2a body (after `character_json.cpp:1867`). Each line maps 1:1 to v1 `serialize_character_to_json` (`:1732-1865`); the colors/talks/skills/affects blocks reuse v1's exact intermediates (`collect_non_default_color_slots`, `collect_non_zero_named_values`, `talk_key_for_index`/`skill_key_for_index`, `escape_json_string`):
```cpp
std::string serialize_character_to_json_v2a(const CharacterData& character)
{
    JsonWriter writer(kSerializeReserveHint);
    writer.raw("{\n");
    writer.raw("  \"schema_version\": ");
    writer.number(character.schema_version);
    writer.raw(",\n");
    writer.raw("  \"character_name\": \"");
    writer.raw(json_utils::escape_json_string(character.character_name));
    writer.raw("\",\n");
    writer.raw("  \"title\": \"");
    writer.raw(json_utils::escape_json_string(character.title));
    writer.raw("\",\n");
    writer.raw("  \"description\": \"");
    writer.raw(json_utils::escape_json_string(character.description));
    writer.raw("\",\n");
    writer.raw("  \"identity\": {\n");
    writer.raw("    \"idnum\": ");
    writer.number(character.idnum);
    writer.raw(",\n");
    writer.raw("    \"race\": ");
    writer.number(character.race);
    writer.raw(",\n");
    writer.raw("    \"sex\": ");
    writer.number(character.sex);
    writer.raw(",\n");
    writer.raw("    \"bodytype\": ");
    writer.number(character.bodytype);
    writer.raw(",\n");
    writer.raw("    \"language\": ");
    writer.number(character.language);
    writer.raw(",\n");
    writer.raw("    \"hometown\": ");
    writer.number(character.hometown);
    writer.raw(",\n");
    writer.raw("    \"weight\": ");
    writer.number(character.weight);
    writer.raw(",\n");
    writer.raw("    \"height\": ");
    writer.number(character.height);
    writer.raw("\n");
    writer.raw("  },\n");
    writer.raw("  \"progression\": {\n");
    writer.raw("    \"level\": ");
    writer.number(character.level);
    writer.raw(",\n");
    writer.raw("    \"alignment\": ");
    writer.number(character.alignment);
    writer.raw(",\n");
    writer.raw("    \"mini_level\": ");
    writer.number(character.mini_level);
    writer.raw(",\n");
    writer.raw("    \"max_mini_level\": ");
    writer.number(character.max_mini_level);
    writer.raw(",\n");
    writer.raw("    \"spells_to_learn\": ");
    writer.number(character.spells_to_learn);
    writer.raw(",\n");
    writer.raw("    \"rerolls\": ");
    writer.number(character.rerolls);
    writer.raw("\n");
    writer.raw("  },\n");
    writer.raw("  \"abilities\": {\n");
    writer.raw("    \"temporary\": ");
    write_ability_v2(writer, character.temporary_abilities);
    writer.raw(",\n");
    writer.raw("    \"rolled\": ");
    write_ability_v2(writer, character.rolled_abilities);
    writer.raw("\n  },\n");
    writer.raw("  \"points\": {\n");
    writer.raw("    \"bodypart_hit\": ");
    write_integer_array_v2(writer, character.points.bodypart_hit);
    writer.raw(",\n");
    writer.raw("    \"gold\": ");
    writer.number(character.points.gold);
    writer.raw(",\n");
    writer.raw("    \"experience\": ");
    writer.number(character.points.experience);
    writer.raw(",\n");
    writer.raw("    \"spirit\": ");
    writer.number(character.points.spirit);
    writer.raw(",\n");
    writer.raw("    \"mana_regen\": ");
    writer.number(character.points.mana_regen);
    writer.raw(",\n");
    writer.raw("    \"health_regen\": ");
    writer.number(character.points.health_regen);
    writer.raw(",\n");
    writer.raw("    \"move_regen\": ");
    writer.number(character.points.move_regen);
    writer.raw(",\n");
    writer.raw("    \"ob\": ");
    writer.number(character.points.ob);
    writer.raw(",\n");
    writer.raw("    \"damage\": ");
    writer.number(character.points.damage);
    writer.raw(",\n");
    writer.raw("    \"energy_regen\": ");
    writer.number(character.points.energy_regen);
    writer.raw(",\n");
    writer.raw("    \"parry\": ");
    writer.number(character.points.parry);
    writer.raw(",\n");
    writer.raw("    \"dodge\": ");
    writer.number(character.points.dodge);
    writer.raw(",\n");
    writer.raw("    \"encumbrance\": ");
    writer.number(character.points.encumbrance);
    writer.raw(",\n");
    writer.raw("    \"willpower\": ");
    writer.number(character.points.willpower);
    writer.raw(",\n");
    writer.raw("    \"spell_pen\": ");
    writer.number(character.points.spell_pen);
    writer.raw(",\n");
    writer.raw("    \"spell_power\": ");
    writer.number(character.points.spell_power);
    writer.raw("\n");
    writer.raw("  },\n");
    writer.raw("  \"professions\": {\n");
    write_profession_v2(writer, "mage", character.mage);
    writer.raw(",\n");
    write_profession_v2(writer, "mystic", character.mystic);
    writer.raw(",\n");
    write_profession_v2(writer, "ranger", character.ranger);
    writer.raw(",\n");
    write_profession_v2(writer, "warrior", character.warrior);
    writer.raw("\n  },\n");
    writer.raw("  \"flags\": {\n");
    writer.raw("    \"player\": ");
    write_string_array_v2a(writer, character.player_flags);
    writer.raw(",\n");
    writer.raw("    \"preferences\": ");
    write_string_array_v2a(writer, character.preference_flags);
    writer.raw(",\n");
    writer.raw("    \"affected\": ");
    write_string_array_v2a(writer, character.affected_flags);
    writer.raw(",\n");
    writer.raw("    \"hide\": ");
    write_string_array_v2a(writer, character.hide_flags);
    writer.raw("\n  },\n");
    writer.raw("  \"conditions\": {\n");
    writer.raw("    \"drunk\": ");
    writer.number(character.conditions.drunk);
    writer.raw(",\n");
    writer.raw("    \"full\": ");
    writer.number(character.conditions.full);
    writer.raw(",\n");
    writer.raw("    \"thirst\": ");
    writer.number(character.conditions.thirst);
    writer.raw("\n");
    writer.raw("  },\n");
    writer.raw("  \"color_mask\": ");
    writer.number(character.color_mask);
    writer.raw(",\n");
    writer.raw("  \"colors\": {");
    const std::vector<NamedValue> color_slots = collect_non_default_color_slots(character);
    for (size_t index = 0; index < color_slots.size(); ++index) {
        if (index > 0)
            writer.raw(", ");
        const int slot_index = color_slots[index].value;
        ColorSettingData setting = (slot_index < static_cast<int>(character.color_settings.size()))
            ? character.color_settings[slot_index]
            : default_color_setting();
        if (is_default_color_value(setting.foreground) && slot_index < static_cast<int>(character.colors.size()) && character.colors[slot_index] != CNRM) {
            setting.foreground.mode = COLOR_VALUE_ANSI16;
            setting.foreground.value = character.colors[slot_index];
        }
        normalize_color_setting(&setting);
        writer.raw('"');
        writer.raw(json_utils::escape_json_string(color_slots[index].key));
        writer.raw("\": ");
        write_color_setting_v2(writer, setting);
    }
    writer.raw("},\n");
    writer.raw("  \"timers\": {\n");
    writer.raw("    \"birth\": ");
    writer.number(character.timers.birth);
    writer.raw(",\n");
    writer.raw("    \"last_logon\": ");
    writer.number(character.timers.last_logon);
    writer.raw(",\n");
    writer.raw("    \"played_seconds\": ");
    writer.number(character.timers.played_seconds);
    writer.raw(",\n");
    writer.raw("    \"retired_on\": ");
    writer.number(character.timers.retired_on);
    writer.raw("\n");
    writer.raw("  },\n");
    writer.raw("  \"perception\": {\n");
    writer.raw("    \"raw\": ");
    writer.number(character.raw_perception);
    writer.raw(",\n");
    writer.raw("    \"current\": ");
    writer.number(character.perception);
    writer.raw("\n");
    writer.raw("  },\n");
    writer.raw("  \"state\": {\n");
    writer.raw("    \"load_room\": ");
    writer.number(character.load_room);
    writer.raw(",\n");
    writer.raw("    \"wimp_level\": ");
    writer.number(character.wimp_level);
    writer.raw(",\n");
    writer.raw("    \"freeze_level\": ");
    writer.number(character.freeze_level);
    writer.raw(",\n");
    writer.raw("    \"morale\": ");
    writer.number(character.morale);
    writer.raw(",\n");
    writer.raw("    \"owner\": ");
    writer.number(character.owner);
    writer.raw(",\n");
    writer.raw("    \"leg_encumbrance\": ");
    writer.number(character.leg_encumbrance);
    writer.raw(",\n");
    writer.raw("    \"rp_flag\": ");
    writer.number(character.rp_flag);
    writer.raw(",\n");
    writer.raw("    \"will_teach\": ");
    writer.number(character.will_teach);
    writer.raw(",\n");
    writer.raw("    \"tactics\": ");
    writer.number(character.tactics);
    writer.raw(",\n");
    writer.raw("    \"shooting\": ");
    writer.number(character.shooting);
    writer.raw(",\n");
    writer.raw("    \"casting\": ");
    writer.number(character.casting);
    writer.raw(",\n");
    writer.raw("    \"two_handed\": ");
    writer.raw(character.two_handed ? "true" : "false");
    writer.raw("\n");
    writer.raw("  },\n");
    writer.raw("  \"talks\": ");
    write_named_integer_object_v2a(writer, collect_non_zero_named_values(character.talks, MAX_TOUNGE, talk_key_for_index));
    writer.raw(",\n");
    writer.raw("  \"skills\": ");
    write_named_integer_object_v2a(writer, collect_non_zero_named_values(character.skills, MAX_SKILLS, skill_key_for_index));
    writer.raw(",\n");
    writer.raw("  \"affects\": [");
    for (size_t index = 0; index < character.affects.size(); ++index) {
        if (index > 0)
            writer.raw(", ");
        write_affect_v2a(writer, character.affects[index]);
    }
    writer.raw("]\n");
    writer.raw("}\n");
    return writer.take();
}
```

- [ ] **Step 4: Run tests, expect PASS** — `scripts/rots-docker.sh test '--gtest_filter=JsonPerf.SerializeV2a*'`. Expected: `[  PASSED  ] 2 tests.` (both light and heavy report byte-identical v1/v2a output). Sanity-check the canonical suites still green per-test: `scripts/rots-docker.sh test '--gtest_filter=CharacterJson.*:SaveBenchmark.*'`.

- [ ] **Step 5: Commit** — `git add src/character_json.h src/character_json.cpp src/tests/json_perf_tests.cpp src/CMakeLists.txt && git commit -m "feat(character_json): add serialize_character_to_json_v2a (reserve+to_chars), JsonPerf byte-equality gate" -m "Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"`

---

### Task 7: serialize_character_to_json_v2b (append_escaped_json_string fast path + cached key tables + drop vector intermediates)

**Files:**
- Modify `src/json_utils.h:10` — add `append_escaped_json_string` declaration after `escape_json_string`.
- Modify `src/json_utils.cpp:88` — add `append_escaped_json_string` definition after `escape_json_string` (same TU as the file-local `hex_digit` at `:13`).
- Modify `src/character_json.h:151` — add the `_v2b` declaration after the v2a decl.
- Modify `src/character_json.cpp` — extend the anon-namespace helper block (added in Task 6, after `:907`) with `skill_key_for_index_cached`/`talk_key_for_index_cached` + `write_string_array_v2b`/`write_affect_v2b`; insert `serialize_character_to_json_v2b` at `character_json` scope right after `serialize_character_to_json_v2a`.
- Modify `src/tests/json_perf_tests.cpp` — append the v2b byte-equality cases + the `append_escaped_json_string` direct case (reuse Task 6's `make_light_data`/`make_heavy_data`).

**Interfaces:**
- Consumes (from contract / Task 6): `JsonWriter`, `write_ability_v2`/`write_profession_v2`/`write_integer_array_v2`/`write_color_value_v2`/`write_color_setting_v2`, `kSerializeReserveHint`, `make_light_data()`/`make_heavy_data()`; anon helpers `skill_key_for_index`/`talk_key_for_index` (`character_json.cpp:642`/`:635`), `color_key_for_index` (`:647`), `default_color_setting`/`is_default_color_value`/`is_default_color_setting`/`normalize_color_setting`; v1 byte oracle (`:1729`); `hex_digit` (`json_utils.cpp:13`); `MAX_SKILLS`/`MAX_TOUNGE`/`MAX_COLOR_FIELDS` (`structs.h:707-708`,`color.h:7`).
- Produces: `std::string character_json::serialize_character_to_json_v2b(const CharacterData&)`; `void json_utils::append_escaped_json_string(std::string&, const std::string&)`.

- [ ] **Step 1: Write the failing test** — add the two decls + the json_utils decl + a one-line v2b stub (replaced in Step 3). `json_utils.h` after line 10:
```cpp
void append_escaped_json_string(std::string& out, const std::string& value);
```
`character_json.h` after the Task-6 v2a decl (`:151`):
```cpp
std::string serialize_character_to_json_v2b(const CharacterData& character);
```
Temporary stub after `serialize_character_to_json_v2a` (Task 6's insertion point):
```cpp
std::string serialize_character_to_json_v2b(const CharacterData& character)
{
    (void)character;
    return std::string();
}
```
And implement the REAL `append_escaped_json_string` now (it is independently testable; the byte-equality + direct test below are its oracle). After `escape_json_string` (`json_utils.cpp:88`):
```cpp
void append_escaped_json_string(std::string& out, const std::string& value)
{
    // Fast path: if no byte needs escaping ({ '"', '\\', anything < 0x20 }), append the whole span
    // unchanged (no temporary, no per-char branch). Provably true for slugged keys / flag names.
    bool needs_escape = false;
    for (char character : value) {
        if (character == '"' || character == '\\' || static_cast<unsigned char>(character) < 0x20) {
            needs_escape = true;
            break;
        }
    }
    if (!needs_escape) {
        out.append(value);
        return;
    }

    // Slow path: byte-identical to escape_json_string, appended in place instead of returned.
    for (char character : value) {
        switch (character) {
        case '\\':
            out += "\\\\";
            break;
        case '"':
            out += "\\\"";
            break;
        case '\b':
            out += "\\b";
            break;
        case '\f':
            out += "\\f";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (static_cast<unsigned char>(character) < 0x20) {
                out += "\\u00";
                out += hex_digit((static_cast<unsigned char>(character) >> 4) & 0x0f);
                out += hex_digit(static_cast<unsigned char>(character) & 0x0f);
            } else {
                out += character;
            }
            break;
        }
    }
}
```
Append these test cases to `src/tests/json_perf_tests.cpp` (add `#include "../json_utils.h"` to the includes):
```cpp
TEST(JsonPerf, AppendEscapedMatchesEscapeJsonString)
{
    const std::string inputs[] = {
        "",
        "plain_slug_key",
        "needs \" quote",
        "needs \\ backslash",
        std::string("control\x01\x1f end", 13),
        "tab\tnewline\nreturn\rmix",
    };
    for (const std::string& input : inputs) {
        std::string appended = "PREFIX:";
        json_utils::append_escaped_json_string(appended, input);
        EXPECT_EQ(std::string("PREFIX:") + json_utils::escape_json_string(input), appended);
    }
}

TEST(JsonPerf, SerializeV2bMatchesV1Light)
{
    const character_json::CharacterData character = make_light_data();
    EXPECT_EQ(character_json::serialize_character_to_json(character),
        character_json::serialize_character_to_json_v2b(character));
}

TEST(JsonPerf, SerializeV2bMatchesV1Heavy)
{
    const character_json::CharacterData character = make_heavy_data();
    EXPECT_EQ(character_json::serialize_character_to_json(character),
        character_json::serialize_character_to_json_v2b(character));
}

TEST(JsonPerf, SerializeV2aEqualsV2bHeavy)
{
    const character_json::CharacterData character = make_heavy_data();
    EXPECT_EQ(character_json::serialize_character_to_json_v2a(character),
        character_json::serialize_character_to_json_v2b(character));
}
```

- [ ] **Step 2: Run it, expect FAIL** — `scripts/rots-docker.sh test '--gtest_filter=JsonPerf.AppendEscaped*:JsonPerf.SerializeV2b*:JsonPerf.SerializeV2aEqualsV2bHeavy'`. Expected: `AppendEscaped*` PASSES (real impl shipped in Step 1), the three v2b cases RED (`Expected equality…`, v2b side empty from the stub).

- [ ] **Step 3: Implement** — extend the anon-namespace helper block (after Task 6's `write_affect_v2a`, before `parse_string_array` at `character_json.cpp:909`) with the cached key tables + v2b escaping helpers. The tables are lazy, immutable-after-boot, and built from the EXACT v1 key functions, so `*_cached(i) == *_for_index(i)` for every i (oracle = the byte-equality test):
```cpp
const std::string& skill_key_for_index_cached(int index)
{
    // Lazily-built, immutable-after-boot table of skill key slugs (one per skill index) so the v2b
    // serializer never re-runs slugify_key per field. Each entry equals skill_key_for_index(i)
    // exactly. Callers index in [0, MAX_SKILLS).
    static const std::vector<std::string> table = [] {
        std::vector<std::string> built;
        built.reserve(MAX_SKILLS);
        for (int slug_index = 0; slug_index < MAX_SKILLS; ++slug_index)
            built.push_back(skill_key_for_index(slug_index));
        return built;
    }();
    return table[index];
}

const std::string& talk_key_for_index_cached(int index)
{
    // Lazily-built, immutable-after-boot table of talk key slugs. Built from talk_key_for_index,
    // which reads language_number/language_skills (fixed after boot). Callers index in [0, MAX_TOUNGE).
    static const std::vector<std::string> table = [] {
        std::vector<std::string> built;
        built.reserve(MAX_TOUNGE);
        for (int slug_index = 0; slug_index < MAX_TOUNGE; ++slug_index)
            built.push_back(talk_key_for_index(slug_index));
        return built;
    }();
    return table[index];
}

void write_string_array_v2b(JsonWriter& writer, const std::vector<std::string>& values)
{
    writer.raw('[');
    for (size_t index = 0; index < values.size(); ++index) {
        if (index > 0)
            writer.raw(", ");
        writer.raw('"');
        json_utils::append_escaped_json_string(writer.buffer(), values[index]);
        writer.raw('"');
    }
    writer.raw(']');
}

void write_affect_v2b(JsonWriter& writer, const AffectData& affect)
{
    writer.raw("{\"type\": ");
    writer.number(affect.type);
    writer.raw(", \"duration\": ");
    writer.number(affect.duration);
    writer.raw(", \"time_phase\": ");
    writer.number(affect.time_phase);
    writer.raw(", \"modifier\": ");
    writer.number(affect.modifier);
    writer.raw(", \"location\": ");
    writer.number(affect.location);
    writer.raw(", \"counter\": ");
    writer.number(affect.counter);
    writer.raw(", \"flags\": ");
    write_string_array_v2b(writer, affect.flags);
    writer.raw('}');
}
```
Then REPLACE the Step-1 v2b stub. v2b is a mechanical mirror of `serialize_character_to_json_v2a` (Task 6): the entire span from `"{\n"` through the `"  \"state\": {…"` block (i.e. identity, progression, abilities, points, professions, conditions, color_mask, timers, perception, state — none of which escape) is BYTE-IDENTICAL — copy it from v2a unchanged. Apply ONLY these substitutions (each preserves bytes; the colors/talks/skills blocks drop v1's `std::vector<NamedValue>` intermediates by iterating indices inline with the same order + zero/default skipping):
  1. The three top-level strings use the fast-path escape directly into the buffer instead of `escape_json_string`:
```cpp
    writer.raw("  \"character_name\": \"");
    json_utils::append_escaped_json_string(writer.buffer(), character.character_name);
    writer.raw("\",\n");
    writer.raw("  \"title\": \"");
    json_utils::append_escaped_json_string(writer.buffer(), character.title);
    writer.raw("\",\n");
    writer.raw("  \"description\": \"");
    json_utils::append_escaped_json_string(writer.buffer(), character.description);
    writer.raw("\",\n");
```
  2. The four `flags` arrays call `write_string_array_v2b` instead of `write_string_array_v2a` (same `"    \"player\": "` / `"    \"preferences\": "` / `"    \"affected\": "` / `"    \"hide\": "` literals around them).
  3. The `colors` block — replace v2a's `collect_non_default_color_slots` + loop with the inline iteration (the per-slot setting derivation is identical to v1's `collect_non_default_color_slots` + main loop, so skip-if-default and emitted bytes match):
```cpp
    writer.raw("  \"colors\": {");
    bool first_color = true;
    for (int index = 0; index < MAX_COLOR_FIELDS; ++index) {
        ColorSettingData setting = default_color_setting();
        if (index < static_cast<int>(character.color_settings.size()))
            setting = character.color_settings[index];
        if (is_default_color_value(setting.foreground) && index < static_cast<int>(character.colors.size()) && character.colors[index] != CNRM) {
            setting.foreground.mode = COLOR_VALUE_ANSI16;
            setting.foreground.value = character.colors[index];
        }
        normalize_color_setting(&setting);
        if (is_default_color_setting(setting))
            continue;
        if (!first_color)
            writer.raw(", ");
        first_color = false;
        writer.raw('"');
        json_utils::append_escaped_json_string(writer.buffer(), color_key_for_index(index));
        writer.raw("\": ");
        write_color_setting_v2(writer, setting);
    }
    writer.raw("},\n");
```
  4. The `talks` and `skills` objects — replace v2a's `collect_non_zero_named_values` + `write_named_integer_object_v2a` with inline iteration using the cached keys (loop bound `min(expected, size)` and zero-skip match `collect_non_zero_named_values` exactly; the timers block is unchanged and follows):
```cpp
    writer.raw("  \"talks\": {");
    bool first_talk = true;
    const int talk_limit = std::min(MAX_TOUNGE, static_cast<int>(character.talks.size()));
    for (int index = 0; index < talk_limit; ++index) {
        if (character.talks[index] == 0)
            continue;
        if (!first_talk)
            writer.raw(", ");
        first_talk = false;
        writer.raw('"');
        json_utils::append_escaped_json_string(writer.buffer(), talk_key_for_index_cached(index));
        writer.raw("\": ");
        writer.number(character.talks[index]);
    }
    writer.raw("},\n");
    writer.raw("  \"skills\": {");
    bool first_skill = true;
    const int skill_limit = std::min(MAX_SKILLS, static_cast<int>(character.skills.size()));
    for (int index = 0; index < skill_limit; ++index) {
        if (character.skills[index] == 0)
            continue;
        if (!first_skill)
            writer.raw(", ");
        first_skill = false;
        writer.raw('"');
        json_utils::append_escaped_json_string(writer.buffer(), skill_key_for_index_cached(index));
        writer.raw("\": ");
        writer.number(character.skills[index]);
    }
    writer.raw("},\n");
```
  5. The `affects` loop calls `write_affect_v2b` instead of `write_affect_v2a`.
  6. Function opens `JsonWriter writer(kSerializeReserveHint);` and closes `writer.raw("]\n"); writer.raw("}\n"); return writer.take();` — identical to v2a.
(`<algorithm>` for `std::min` is already included at `character_json.cpp:7`.)

- [ ] **Step 4: Run tests, expect PASS** — `scripts/rots-docker.sh test '--gtest_filter=JsonPerf.*'`. Expected: `[  PASSED  ] 7 tests.` (Task 6's 2 v2a cases + this task's `AppendEscapedMatchesEscapeJsonString`, 2 v2b cases, `SerializeV2aEqualsV2bHeavy`). Re-check canonical green per-test: `scripts/rots-docker.sh test '--gtest_filter=CharacterJson.*:SaveBenchmark.*:JsonUtils.*'`.

- [ ] **Step 5: Commit** — `git add src/json_utils.h src/json_utils.cpp src/character_json.h src/character_json.cpp src/tests/json_perf_tests.cpp && git commit -m "feat(character_json): add serialize_character_to_json_v2b (fast escape + cached key tables, drop vector intermediates) + json_utils::append_escaped_json_string" -m "Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"`

---

### Task 8: savebench `compare` out-param — cache + variant A/B in a separate report
**Files:**
- Modify `src/save_benchmark.h:40-43` (profile_save decl), `src/save_benchmark.h:49-51` (profile_load decl).
- Modify `src/save_benchmark.cpp:1-13` (add include), `:66-109` (profile_save body), `:111-180` (profile_load body).
- Test `src/tests/save_benchmark_tests.cpp` (add `#include "../account_cache.h"` + one new `TEST`).

**Interfaces:**
- *Consumes (from earlier tasks/contract):*
  - Task A: `bool account_cache::read_account_file_cached(const std::string& root_directory, const std::string& account_name, account::AccountData* account, std::string* error_message);` and `void account_cache::clear();`
  - Tasks 6-7 (serialize): `std::string character_json::serialize_character_to_json_v2a(const CharacterData&);` and `..._v2b(...)`.
  - Tasks 4-5 (deserialize): `bool character_json::deserialize_character_from_json_v2a(const std::string& json, CharacterData* character, std::string* error_message = nullptr);` and `..._v2b(...)`.
  - Existing (untouched v1): `account::read_account_file`, `character_json::serialize_character_to_json`, `character_json::deserialize_character_from_json`, and the file-local `time_stage`/`finalize_shares` (`save_benchmark.cpp:19`,`:45`).
- *Produces (Task 9 + tests rely on these EXACT signatures):*
  - `bool savebench::profile_save(const char_file_u& chd, const std::string& root, const std::string& account_name, const std::string& character_name, const std::string& scratch_path, int iterations, PipelineReport* out, std::string* error, PipelineReport* compare = nullptr);`
  - `bool savebench::profile_load(const std::string& root, const std::string& account_name, const std::string& character_name, int iterations, bool include_store_to_char, PipelineReport* out, std::string* error, PipelineReport* compare = nullptr);`
  - Compare stage labels (byte-exact) — save: `"S2  read_account_file        (v1)"`, `"S2c read_account_file_cached"`, `"S4  serialize_character_to_json     (v1)"`, `"S4a serialize_character_to_json_v2a"`, `"S4b serialize_character_to_json_v2b"`; load: `"L1  read_account_file        (v1)"`, `"L1c read_account_file_cached"`, `"L3  deserialize_character_from_json     (v1)"`, `"L3a deserialize_character_from_json_v2a"`, `"L3b deserialize_character_from_json_v2b"`.

- [ ] **Step 1: Write the failing test.** Add `#include "../account_cache.h"` to `src/tests/save_benchmark_tests.cpp` (alongside the other `../` includes at the top, after `#include "../account_management.h"`), then append this `TEST` after the existing one (`:178`). It reuses `TemporaryDirectory` / `make_account_character_directory` / `make_stored_character` / `read_character_file_directly` already in the file's anon namespace.
```cpp
// Exercises the opt-in COMPARE report: profile_save/profile_load populate a SEPARATE report
// with the cache + serialize/deserialize A/B stages (byte-exact labels), leave the canonical
// breakdown untouched, and the compare report's own shares reconcile to ~100% because its
// TOTAL runs each compared item exactly once. Timing is not asserted (QEMU-flaky).
TEST(SaveBenchmark, CompareReportPopulatesVariantStages)
{
    account_cache::clear(); // contract: clear the memo maps for test isolation
    TemporaryDirectory temp;
    const std::string root = temp.path();
    const std::string account = "alpha-admin";
    const std::string character = "aragorn";
    std::string err;

    make_account_character_directory(root, account, character);
    const std::string character_path = account::account_character_player_path(root, account, character);
    const char_file_u original = make_stored_character("aragorn");
    const std::string json =
        character_json::serialize_character_to_json(character_json::character_data_from_store(original));
    ASSERT_TRUE(account::write_text_file_atomically(character_path, json, &err)) << err;

    char_file_u source {};
    ASSERT_TRUE(read_character_file_directly(character_path, &source, &err)) << err;

    savebench::PipelineReport save_report, load_report;
    savebench::PipelineReport save_compare, load_compare;
    ASSERT_TRUE(savebench::profile_save(source, root, account, character, root + "/sb_scratch.json",
                    3, &save_report, &err, &save_compare))
        << err;
    ASSERT_TRUE(savebench::profile_load(root, account, character, 3, /*include_store_to_char=*/false,
                    &load_report, &err, &load_compare))
        << err;

    // Canonical report is unaffected by the compare opt-in (still S2,S3,S4,S5).
    EXPECT_EQ(save_report.stages.size(), 4u);

    // Compare reports carry exactly the five A/B stages with the contract's labels.
    ASSERT_EQ(save_compare.stages.size(), 5u);
    EXPECT_EQ(save_compare.stages[0].name, "S2  read_account_file        (v1)");
    EXPECT_EQ(save_compare.stages[1].name, "S2c read_account_file_cached");
    EXPECT_EQ(save_compare.stages[2].name, "S4  serialize_character_to_json     (v1)");
    EXPECT_EQ(save_compare.stages[3].name, "S4a serialize_character_to_json_v2a");
    EXPECT_EQ(save_compare.stages[4].name, "S4b serialize_character_to_json_v2b");

    ASSERT_EQ(load_compare.stages.size(), 5u);
    EXPECT_EQ(load_compare.stages[0].name, "L1  read_account_file        (v1)");
    EXPECT_EQ(load_compare.stages[1].name, "L1c read_account_file_cached");
    EXPECT_EQ(load_compare.stages[2].name, "L3  deserialize_character_from_json     (v1)");
    EXPECT_EQ(load_compare.stages[3].name, "L3a deserialize_character_from_json_v2a");
    EXPECT_EQ(load_compare.stages[4].name, "L3b deserialize_character_from_json_v2b");

    // The compare report's per-stage + other shares reconcile to ~100% (its TOTAL runs each once).
    double save_cmp_share = save_compare.other.share;
    for (const auto& s : save_compare.stages)
        save_cmp_share += s.share;
    EXPECT_NEAR(save_cmp_share, 100.0, 1.0);
    double load_cmp_share = load_compare.other.share;
    for (const auto& s : load_compare.stages)
        load_cmp_share += s.share;
    EXPECT_NEAR(load_cmp_share, 100.0, 1.0);

    // Opt-in only: omitting the compare arg (default nullptr) leaves a passed report empty.
    savebench::PipelineReport plain_report, never_filled;
    ASSERT_TRUE(savebench::profile_save(source, root, account, character, root + "/sb_scratch2.json",
                    3, &plain_report, &err))
        << err;
    EXPECT_TRUE(never_filled.stages.empty());
}
```

- [ ] **Step 2: Run it, expect FAIL (compile error).** `scripts/rots-docker.sh test '--gtest_filter=SaveBenchmark.CompareReportPopulatesVariantStages'`. Expected: the `ageland_tests` build fails to compile — `error: too many arguments to function 'bool savebench::profile_save(...)'` (the 9-arg call has no matching 8-arg declaration yet). This proves the test exercises the new param.

- [ ] **Step 3: Implement the `compare` param.** Four edits; logic is net-new but each compare block is a mechanical mirror of the existing canonical stages, pinned by the Step-1 oracle.

  **3a — header (`src/save_benchmark.h`).** Append `, PipelineReport* compare = nullptr` to both declarations:
  - `:43` change `PipelineReport* out, std::string* error);` → `PipelineReport* out, std::string* error, PipelineReport* compare = nullptr);`
  - `:51` change `bool include_store_to_char, PipelineReport* out, std::string* error);` → `bool include_store_to_char, PipelineReport* out, std::string* error, PipelineReport* compare = nullptr);`

  **3b — include (`src/save_benchmark.cpp:3`).** After `#include "account_management.h"` add `#include "account_cache.h"` (character_json.h is already included at `:4`, so the v2 serialize/deserialize decls are visible).

  **3c — `profile_save` (`src/save_benchmark.cpp`).** Change the definition signature (`:66-69`) to match the header (add `, PipelineReport* compare`). Then INSERT this block immediately after `std::remove(scratch_path.c_str());` (`:101`) and before `finalize_shares(out);` (`:102`). `cd` (`:79`) is in scope for the serialize variants:
```cpp
    // COMPARE (opt-in): A/B the parallel cache + serialize variants against v1 in a SEPARATE
    // report so finalize_shares/format_report stay valid on the canonical breakdown above.
    // Pure in-memory: the resolvers read read-only; serialize is a string transform — no live
    // write. compare->total runs every compared item once so its shares reconcile to ~100%.
    if (compare) {
        std::string cmp_err;
        account::AccountData cmp_account;
        compare->stages.push_back(time_stage("S2  read_account_file        (v1)", iterations, [&]() {
            account::read_account_file(root, account_name, &cmp_account, &cmp_err);
        }));
        compare->stages.push_back(time_stage("S2c read_account_file_cached", iterations, [&]() {
            account_cache::read_account_file_cached(root, account_name, &cmp_account, &cmp_err);
        }));
        std::string cmp_json;
        compare->stages.push_back(time_stage("S4  serialize_character_to_json     (v1)", iterations, [&]() {
            cmp_json = character_json::serialize_character_to_json(cd);
        }));
        compare->stages.push_back(time_stage("S4a serialize_character_to_json_v2a", iterations, [&]() {
            cmp_json = character_json::serialize_character_to_json_v2a(cd);
        }));
        compare->stages.push_back(time_stage("S4b serialize_character_to_json_v2b", iterations, [&]() {
            cmp_json = character_json::serialize_character_to_json_v2b(cd);
        }));
        compare->total = time_stage("TOTAL save compare", iterations, [&]() {
            account::AccountData a;
            account::read_account_file(root, account_name, &a, &cmp_err);
            account_cache::read_account_file_cached(root, account_name, &a, &cmp_err);
            const std::string j1 = character_json::serialize_character_to_json(cd);
            const std::string j2 = character_json::serialize_character_to_json_v2a(cd);
            const std::string j3 = character_json::serialize_character_to_json_v2b(cd);
        });
        finalize_shares(compare);
    }
```
  (The `j1/j2/j3` locals are unused-but-named std::strings — like the existing `const std::string j` at `:98`, GCC does not warn on unused non-trivial types. `cmp_account`/`cmp_err`/`cmp_json` are reused across stages per the reuse-scoped-objects convention.)

  **3d — `profile_load` (`src/save_benchmark.cpp`).** Change the definition signature (`:111-113`) to match the header. Then INSERT this block immediately after `finalize_shares(out);` (`:166`) and before the `if (!err_L2.empty())` check (`:167`). `json` (`:124`) is in scope:
```cpp
    // COMPARE (opt-in): A/B the cache + deserialize variants against v1 in a SEPARATE report.
    // Pure in-memory (deserialize over the already-read `json`); compare->total runs each item
    // once so its shares reconcile to ~100%. Canonical out/* error semantics are unchanged.
    if (compare) {
        std::string cmp_err;
        account::AccountData cmp_account;
        compare->stages.push_back(time_stage("L1  read_account_file        (v1)", iterations, [&]() {
            account::read_account_file(root, account_name, &cmp_account, &cmp_err);
        }));
        compare->stages.push_back(time_stage("L1c read_account_file_cached", iterations, [&]() {
            account_cache::read_account_file_cached(root, account_name, &cmp_account, &cmp_err);
        }));
        character_json::CharacterData cmp_cd;
        compare->stages.push_back(time_stage("L3  deserialize_character_from_json     (v1)", iterations, [&]() {
            cmp_cd = character_json::CharacterData {};
            character_json::deserialize_character_from_json(json, &cmp_cd, &cmp_err);
        }));
        compare->stages.push_back(time_stage("L3a deserialize_character_from_json_v2a", iterations, [&]() {
            cmp_cd = character_json::CharacterData {};
            character_json::deserialize_character_from_json_v2a(json, &cmp_cd, &cmp_err);
        }));
        compare->stages.push_back(time_stage("L3b deserialize_character_from_json_v2b", iterations, [&]() {
            cmp_cd = character_json::CharacterData {};
            character_json::deserialize_character_from_json_v2b(json, &cmp_cd, &cmp_err);
        }));
        compare->total = time_stage("TOTAL load compare", iterations, [&]() {
            account::AccountData a;
            account::read_account_file(root, account_name, &a, &cmp_err);
            account_cache::read_account_file_cached(root, account_name, &a, &cmp_err);
            character_json::CharacterData c1 {};
            character_json::deserialize_character_from_json(json, &c1, &cmp_err);
            character_json::CharacterData c2 {};
            character_json::deserialize_character_from_json_v2a(json, &c2, &cmp_err);
            character_json::CharacterData c3 {};
            character_json::deserialize_character_from_json_v2b(json, &c3, &cmp_err);
        });
        finalize_shares(compare);
    }
```
  Hand-apply LF + Allman + 4-space + mandatory braces (no auto-format). Add no `//`-comments to the lambda/local temporaries (locals are out of scope for the field-comment rule); the `compare` parameter is also a local.

- [ ] **Step 4: Run tests, expect PASS.** `scripts/rots-docker.sh test '--gtest_filter=SaveBenchmark.*'`. Expected: `[  PASSED  ] 2 tests.` — both `ProfilesBothDirectionsAndRoundTrips` (unchanged, proves the defaulted param kept the canonical report byte-identical) and the new `CompareReportPopulatesVariantStages` pass. (If any of the ~163 known 32-bit baseline reds appear, confirm they are NOT in `SaveBenchmark.*` — gate per-test by name.)

- [ ] **Step 5: Commit.**
```
git add src/save_benchmark.h src/save_benchmark.cpp src/tests/save_benchmark_tests.cpp
git commit -m "feat(savebench): add opt-in compare report (cache + JSON v2 A/B stages)

profile_save/profile_load take a trailing PipelineReport* compare=nullptr;
when set they populate a separate report (own TOTAL runs each compared item
once) with S2/S2c/S4/S4a/S4b and L1/L1c/L3/L3a/L3b stages. Canonical
breakdown unchanged; reuses time_stage/finalize_shares.

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

### Task 9: `savebench compare N` subcommand + COMPARE print sections
**Files:** Modify `src/savebench.cpp:10-12` (include), `:24-32` (arg parse), `:45-61` (profile calls + report assembly).
**Interfaces:**
- *Consumes (from Task 8):* `savebench::profile_save(..., PipelineReport* compare = nullptr)`, `savebench::profile_load(..., PipelineReport* compare = nullptr)`, and `savebench::format_report(const std::string& title, const PipelineReport&)` (`save_benchmark.h:54`, unchanged).
- *Produces:* the in-game `savebench compare N` command path; `savebench N` (no token) keeps passing `nullptr` and stays cheap. No new public symbols (ACMD body only).

> Note: the `do_savebench` ACMD has no offline gtest harness (it needs a live `ch`/`ch->desc`, `page_string`, `log`). Its oracle is therefore (a) Task 8's gtest, which pins the exact compare-report content that this command prints, and (b) a clean `ageland_tests` build — `savebench.cpp` is in `ROTS_SERVER_SOURCES` (`src/CMakeLists.txt:84`) and is compiled into the `ageland_tests` target (`:162`), so the test build is a real compile gate for this file. An in-game `savebench compare 5` smoke is optional and out-of-band (live server).

- [ ] **Step 1: Confirm the oracle / baseline green.** `scripts/rots-docker.sh test '--gtest_filter=SaveBenchmark.*'`. Expected: `[  PASSED  ] 2 tests.` This confirms `savebench.cpp` currently compiles into `ageland_tests` and the compare-report behavior the new command prints is already pinned by Task 8 (no new gtest is added for the ACMD arg-parse).

- [ ] **Step 2: Implement the subcommand.** Three edits to `src/savebench.cpp`:

  **2a — include (`:10-12`).** After `#include <cstdlib>` add `#include <cstring>` (for `strncmp`).

  **2b — arg parse (`:24-28`).** Replace:
```cpp
    int iterations = 100;
    if (argument && *argument) {
        while (*argument == ' ') ++argument;
        iterations = atoi(argument);
    }
```
  with:
```cpp
    // Optional leading "compare" token opts into the A/B variant report; plain "savebench N"
    // stays on the cheap canonical breakdown (the stacked variants run extra transforms/iter).
    bool compare_mode = false;
    int iterations = 100;
    if (argument && *argument) {
        while (*argument == ' ') ++argument;
        if (!strncmp(argument, "compare", 7) && (argument[7] == ' ' || argument[7] == '\0')) {
            compare_mode = true;
            argument += 7;
            while (*argument == ' ') ++argument;
        }
        if (*argument)
            iterations = atoi(argument);
    }
```
  (Leaving `iterations` at its default 100 when only `compare` is given. The existing `iterations < 1` / `> 10000` clamp at `:29-32` is unchanged.)

  **2c — profile calls + report assembly (`:45-61`).** Replace:
```cpp
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
```
  with:
```cpp
    savebench::PipelineReport save_report, load_report;
    savebench::PipelineReport save_compare, load_compare;
    if (!savebench::profile_save(chd, ".", owner, GET_NAME(ch), scratch, iterations, &save_report, &err,
            compare_mode ? &save_compare : nullptr)) {
        send_to_char("savebench: save profiling failed.\r\n", ch);
        return;
    }

    // LOAD: profile L1-L4 against the real files (read-only). L5 (store_to_char) is offline-only.
    if (!savebench::profile_load(".", owner, GET_NAME(ch), iterations, /*include_store_to_char=*/false,
            &load_report, &err, compare_mode ? &load_compare : nullptr)) {
        send_to_char("savebench: load profiling failed.\r\n", ch);
        return;
    }

    std::string report = "savebench: " + std::to_string(iterations) + " iterations (live char NOT modified)\r\n";
    report += savebench::format_report("SAVE", save_report);
    report += savebench::format_report("LOAD (L1-L4; L5 offline-only)", load_report);
    if (compare_mode) {
        report += savebench::format_report("COMPARE SAVE (cache + serialize variants)", save_compare);
        report += savebench::format_report("COMPARE LOAD (cache + deserialize variants)", load_compare);
    }
    page_string(ch->desc, const_cast<char*>(report.c_str()), 1);
```
  Sandbox invariant preserved: the only write is profile_save's S5 to `scratch` (`players/SAVEBENCH_<name>.json`, set at `:43`) — unchanged; compare stages add no writes. The COMPARE sections are appended into the same `report` string, so the existing syslog-mirror loop (`:65-82`) and `page_string` emit them as extra `\n`-delimited sections with no further change. Hand-apply LF + Allman + 4-space + mandatory braces; `compare_mode`, `save_compare`, `load_compare` are locals (no field-comment required).

- [ ] **Step 3: Build (compile gate for savebench.cpp) + regression run.** `scripts/rots-docker.sh test '--gtest_filter=SaveBenchmark.*'`. Expected: the `ageland_tests` build succeeds (proving the `savebench.cpp` edits compile and link against the Task-8 signatures) and `[  PASSED  ] 2 tests.` The SaveBenchmark suite is unaffected by the ACMD edit; this run is the regression guard. (Optional, out-of-band: boot the i386 server and run `savebench compare 5` in-game — expect SAVE, LOAD, COMPARE SAVE, COMPARE LOAD sections in both the pager and syslog; `savebench 5` shows only SAVE+LOAD.)

- [ ] **Step 4: Commit.**
```
git add src/savebench.cpp
git commit -m "feat(savebench): add 'savebench compare N' subcommand

Parse an optional leading 'compare' token before the iteration count; when
present, pass &save_compare/&load_compare to profile_save/profile_load and
append two COMPARE format_report sections (not summed into canonical TOTALs).
Plain 'savebench N' passes nullptr. Sandbox invariant preserved (scratch only).

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

