# Account Roster Sorting, Coefficient Filtering, and Summary Cache — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let a player order and narrow the "Play a linked character" roster with single-letter keys, remember their sort on the account, and stop re-parsing every character file on every redraw.

**Architecture:** One function builds the ordered, filtered list of roster positions; both the renderer and the selector consume that same list, so display and selection can never disagree. A new in-memory `roster_cache` memoises the four fields the roster actually needs (level, race, readable, coefficients), keyed by account, invalidated per character at the `write_account_character_file` chokepoint. Sort choice is a new string field on `AccountData`, written when the player leaves the roster.

**Tech Stack:** C++17 compiled 32-bit (`-m32`, no `-O`), GoogleTest via `src/CMakeLists.txt`, Docker i386 toolchain (`scripts/rots-docker.sh`), hand-rolled JSON in `json_utils.h`.

**Spec:** `docs/superpowers/specs/2026-09-02-account-roster-sort-and-filter-design.md`

## Global Constraints

- Build and test **only** through `scripts/rots-docker.sh` (`compile`, `test`, `boot`). The server is 32-bit.
- **Never run `make format` or bare `clang-format` over `account_management.cpp`.** It reorders the fragment `#include`s at lines 1456-1459 (`assets` ahead of `internal`), which breaks the build, and churns unrelated lines in the test files. Format by hand, matching `-style=WebKit`.
- `account_management_identity.cpp`, `_storage.cpp`, `_assets.cpp`, `_internal.cpp`, `_presentation.cpp`, `_migration.cpp` are **`#include`d into `account_management.cpp`** as fragments (lines 1456-1459, 1486-1487). They share one translation unit; anonymous-namespace symbols in `account_management.cpp` are visible to them. Do not add them to `CMakeLists.txt` or the `Makefile`.
- Baseline the test suite before starting. `AccountManagement.FormatsOutOfRangeSummaryTimestampsAsInvalid` fails on clean `release-frodo`, and the full run aborts at `InterpreAccountMenu.UnlockSelectAllowsOneDifferentLinkedCharacterSelectionAndConsumesAtEntry`. Both are pre-existing. Run subsets with `--gtest_filter`.
- `ACCOUNT_SCHEMA_VERSION` stays `1`. No migration.
- Sort keys: `a` name A-Z, `l` level high-first, `c` race, `s` side. Filter keys: `w` Warrior, `r` Ranger, `t` Mystic/cleric, `m` Mage. `0` is back. These letters are fixed by player convention.
- Profession indices (`structs.h:800-807`): `PROF_MAGE` 1, `PROF_CLERIC` 2, `PROF_RANGER` 3 (== `PROF_THIEF`), `PROF_WARRIOR` 4, `MAX_PROFS` 4.
- Do not commit, push, or open a PR unless explicitly asked.

---

### Task 1: Roster summary cache

**Files:**
- Create: `src/roster_cache.h`, `src/roster_cache.cpp`
- Modify: `src/CMakeLists.txt:40` (add `roster_cache.cpp` after `account_cache.cpp`), `src/Makefile:29` (add `roster_cache.o` to `OBJFILES`), `src/Makefile` (add a build rule after the `account_cache.o` rule at line 50)
- Test: `src/tests/roster_cache_tests.cpp` (new), registered in `src/CMakeLists.txt:110` after `tests/account_cache_tests.cpp`

**Interfaces:**
- Consumes: nothing from other tasks.
- Produces: `roster_cache::RosterSummary` (fields `level`, `race`, `readable`, `prof_coof[MAX_PROFS + 1]`); `roster_cache::get(root, account_name, character_name, RosterSummary* out)` returning `bool`; `roster_cache::invalidate_character(root, character_name)`; `roster_cache::clear()`; `roster_cache::set_enabled(bool)`; `roster_cache::is_enabled()`; `roster_cache::set_backing_reader_for_testing(ReaderFn)`.

This mirrors `account_cache.{h,cpp}` deliberately: default-off so the test binary keeps uncached behaviour, a testing seam for the backing reader, and `clear()` for fixture isolation.

- [ ] **Step 1: Write the failing test**

Create `src/tests/roster_cache_tests.cpp`:

```cpp
#include "../roster_cache.h"
#include "../account_management.h"

#include <gtest/gtest.h>

namespace {

int g_reader_calls = 0;
char_file_u g_reader_result {};
bool g_reader_succeeds = true;

bool fake_reader(const std::string&, const std::string&, const std::string&,
    char_file_u* stored_character, std::string* error_message)
{
    ++g_reader_calls;
    if (!g_reader_succeeds) {
        if (error_message)
            *error_message = "fake read failure";
        return false;
    }
    *stored_character = g_reader_result;
    if (error_message)
        *error_message = "";
    return true;
}

class RosterCacheTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        roster_cache::clear();
        roster_cache::set_backing_reader_for_testing(&fake_reader);
        roster_cache::set_enabled(true);
        g_reader_calls = 0;
        g_reader_succeeds = true;
        g_reader_result = char_file_u {};
        g_reader_result.level = 42;
        g_reader_result.race = RACE_WOOD;
        g_reader_result.profs.prof_coof[PROF_WARRIOR] = 150;
    }

    void TearDown() override
    {
        roster_cache::set_backing_reader_for_testing(nullptr);
        roster_cache::set_enabled(false);
        roster_cache::clear();
    }
};

} // namespace

TEST_F(RosterCacheTest, SecondReadOfTheSameCharacterDoesNotHitTheBackingReader)
{
    roster_cache::RosterSummary first {};
    ASSERT_TRUE(roster_cache::get(".", "acct", "aragorn", &first));
    EXPECT_EQ(g_reader_calls, 1);
    EXPECT_EQ(first.level, 42);
    EXPECT_EQ(first.race, RACE_WOOD);
    EXPECT_TRUE(first.readable);
    EXPECT_EQ(first.prof_coof[PROF_WARRIOR], 150);

    roster_cache::RosterSummary second {};
    ASSERT_TRUE(roster_cache::get(".", "acct", "aragorn", &second));
    EXPECT_EQ(g_reader_calls, 1) << "second get() re-read the character file";
    EXPECT_EQ(second.level, 42);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `scripts/rots-docker.sh test --gtest_filter='RosterCacheTest.*'`
Expected: compile failure — `roster_cache.h` does not exist.

- [ ] **Step 3: Write the header**

Create `src/roster_cache.h`:

```cpp
#ifndef ROSTER_CACHE_H
#define ROSTER_CACHE_H

#include "structs.h"

#include <string>

namespace roster_cache {

// The only fields the account roster renders or sorts by. The display NAME is deliberately absent:
// it comes from account.characters[index], which account_cache already memoizes. SIDE is absent
// too: it derives from race via other_side_num(). Storing 14 bytes here replaces a ~4 KB JSON
// read-and-parse per rendered row.
struct RosterSummary {
    unsigned char level = 0;
    unsigned char race = 0;
    // False when the character file could not be read or parsed. The roster renders those as
    // "[ ?? ???]"; caching the failure stops the most useless rows being the most expensive.
    bool readable = false;
    // Raw square_root[] index, domain 0..170. Held as short rather than a byte because nothing
    // validates the range on load (character_json.cpp reads it into an int unclamped), and a byte
    // would silently truncate a corrupt value into a plausible-looking wrong sort position.
    short prof_coof[MAX_PROFS + 1] = { 0 };
};

// Returns the summary for one linked character, reading and parsing the character file only on a
// miss. Returns false only when the character cannot be resolved at all; an unreadable character
// is a successful call with readable == false.
bool get(const std::string& root_directory, const std::string& account_name,
    const std::string& character_name, RosterSummary* summary);

// Drops the entry for exactly one character. Called from the write_account_character_file
// chokepoint. Deliberately NOT a global flush: character saves are frequent (autosave), so a
// coarse flush like account_cache's would keep this cache permanently cold and useless.
void invalidate_character(const std::string& root_directory, const std::string& character_name);

// Empties the map. Call in test-fixture SetUp() for isolation.
void clear();

// Whether get() memoizes. Default OFF so the test binary and non-server callers keep exact
// uncached behavior; the live server calls set_enabled(true) once at boot.
void set_enabled(bool enabled);
bool is_enabled();

// Signature of the on-disk reader get() delegates to on a miss.
using ReaderFn = bool (*)(const std::string&, const std::string&, const std::string&,
    char_file_u*, std::string*);

// Test-only seam: override the backing reader. Pass nullptr to restore the real
// account::read_account_character_file. Not thread-safe (the MUD and the tests are single-threaded).
void set_backing_reader_for_testing(ReaderFn reader);

} // namespace roster_cache

#endif
```

- [ ] **Step 4: Write the implementation**

Create `src/roster_cache.cpp`:

```cpp
#include "roster_cache.h"

#include "account_management.h"

#include <algorithm>
#include <string>
#include <unordered_map>

namespace roster_cache {

namespace {

    std::unordered_map<std::string, RosterSummary> g_summaries;

    // Unit separator: cannot appear in a filesystem path component, so (root, name) pairs never
    // collide or bleed across roots. Same rationale as account_cache's key.
    const char kKeySeparator = '\x1f';

    std::string compose_key(const std::string& root_directory, const std::string& character_name)
    {
        std::string key;
        key.reserve(root_directory.size() + 1 + character_name.size());
        key.append(root_directory);
        key.push_back(kKeySeparator);
        key.append(account::normalize_account_name(character_name));
        return key;
    }

    bool g_enabled = false;

    bool default_reader(const std::string& root_directory, const std::string& account_name,
        const std::string& character_name, char_file_u* stored_character, std::string* error_message)
    {
        return account::read_account_character_file(
            root_directory, account_name, character_name, stored_character, error_message);
    }

    ReaderFn g_reader = &default_reader;

    short clamp_coefficient(int value)
    {
        // Domain of square_root[] is 0..170 (consts.cpp). Clamp so a corrupt file cannot produce a
        // wild sort key or, later, an out-of-bounds index.
        if (value < 0)
            return 0;
        if (value > 170)
            return 170;
        return static_cast<short>(value);
    }

    RosterSummary summarize(const char_file_u& stored_character)
    {
        RosterSummary summary;
        summary.readable = true;
        summary.level = static_cast<unsigned char>(stored_character.level);
        summary.race = static_cast<unsigned char>(stored_character.race);
        for (int profession = 0; profession <= MAX_PROFS; ++profession)
            summary.prof_coof[profession] = clamp_coefficient(stored_character.profs.prof_coof[profession]);
        return summary;
    }

} // namespace

bool get(const std::string& root_directory, const std::string& account_name,
    const std::string& character_name, RosterSummary* summary)
{
    if (summary == nullptr)
        return false;

    const std::string key = compose_key(root_directory, character_name);
    if (g_enabled) {
        const auto cached_entry = g_summaries.find(key);
        if (cached_entry != g_summaries.end()) {
            *summary = cached_entry->second;
            return true;
        }
    }

    char_file_u stored_character {};
    std::string read_error;
    RosterSummary loaded;
    if (g_reader(root_directory, account_name, character_name, &stored_character, &read_error))
        loaded = summarize(stored_character);
    // else: loaded keeps its defaults, readable == false -- the "[ ?? ???]" row.

    if (g_enabled)
        g_summaries[key] = loaded;

    *summary = loaded;
    return true;
}

void invalidate_character(const std::string& root_directory, const std::string& character_name)
{
    g_summaries.erase(compose_key(root_directory, character_name));
}

void clear()
{
    g_summaries.clear();
}

void set_enabled(bool enabled)
{
    g_enabled = enabled;
}

bool is_enabled()
{
    return g_enabled;
}

void set_backing_reader_for_testing(ReaderFn reader)
{
    g_reader = (reader == nullptr) ? &default_reader : reader;
}

} // namespace roster_cache
```

- [ ] **Step 5: Wire the build**

In `src/CMakeLists.txt`, after line 40 (`account_cache.cpp`), add:

```cmake
    roster_cache.cpp
```

In the same file, after `tests/account_cache_tests.cpp` (line 110), add:

```cmake
    tests/roster_cache_tests.cpp
```

In `src/Makefile` line 29, add `roster_cache.o` immediately after `account_cache.o`. Then after the `account_cache.o` rule (lines 50-51) add:

```make
roster_cache.o : roster_cache.cpp roster_cache.h account_management.h structs.h
	$(CC) -c $(CFLAGS) roster_cache.cpp
```

- [ ] **Step 6: Run test to verify it passes**

Run: `scripts/rots-docker.sh test --gtest_filter='RosterCacheTest.*'`
Expected: PASS.

- [ ] **Step 7: Add the invalidation and unreadable tests**

Append to `src/tests/roster_cache_tests.cpp`:

```cpp
TEST_F(RosterCacheTest, InvalidatingOneCharacterLeavesOtherEntriesIntact)
{
    roster_cache::RosterSummary summary {};
    ASSERT_TRUE(roster_cache::get(".", "acct", "aragorn", &summary));
    ASSERT_TRUE(roster_cache::get(".", "acct", "legolas", &summary));
    ASSERT_EQ(g_reader_calls, 2);

    roster_cache::invalidate_character(".", "aragorn");

    ASSERT_TRUE(roster_cache::get(".", "acct", "legolas", &summary));
    EXPECT_EQ(g_reader_calls, 2) << "invalidating aragorn also dropped legolas";

    ASSERT_TRUE(roster_cache::get(".", "acct", "aragorn", &summary));
    EXPECT_EQ(g_reader_calls, 3) << "aragorn was not re-read after invalidation";
}

TEST_F(RosterCacheTest, UnreadableCharacterIsCachedAsUnreadableAndNotReparsed)
{
    g_reader_succeeds = false;

    roster_cache::RosterSummary summary {};
    ASSERT_TRUE(roster_cache::get(".", "acct", "broken", &summary));
    EXPECT_FALSE(summary.readable);
    EXPECT_EQ(g_reader_calls, 1);

    ASSERT_TRUE(roster_cache::get(".", "acct", "broken", &summary));
    EXPECT_FALSE(summary.readable);
    EXPECT_EQ(g_reader_calls, 1) << "an unreadable character was re-parsed on every render";
}

TEST_F(RosterCacheTest, CoefficientsAreClampedToTheSquareRootTableDomain)
{
    g_reader_result.profs.prof_coof[PROF_MAGE] = 99999;
    g_reader_result.profs.prof_coof[PROF_CLERIC] = -5;

    roster_cache::RosterSummary summary {};
    ASSERT_TRUE(roster_cache::get(".", "acct", "corrupt", &summary));
    EXPECT_EQ(summary.prof_coof[PROF_MAGE], 170);
    EXPECT_EQ(summary.prof_coof[PROF_CLERIC], 0);
}

TEST_F(RosterCacheTest, DisabledCacheAlwaysReadsThrough)
{
    roster_cache::set_enabled(false);

    roster_cache::RosterSummary summary {};
    ASSERT_TRUE(roster_cache::get(".", "acct", "aragorn", &summary));
    ASSERT_TRUE(roster_cache::get(".", "acct", "aragorn", &summary));
    EXPECT_EQ(g_reader_calls, 2);
}
```

- [ ] **Step 8: Run tests to verify they pass**

Run: `scripts/rots-docker.sh test --gtest_filter='RosterCacheTest.*'`
Expected: 5 tests PASS.

- [ ] **Step 9: Commit**

```bash
git add src/roster_cache.h src/roster_cache.cpp src/tests/roster_cache_tests.cpp src/CMakeLists.txt src/Makefile
git commit -m "feat(account): add roster summary cache

Memoizes the four fields the account roster actually needs -- level, race,
readability, and profession coefficients -- so rendering a row stops costing
a ~4 KB character-file read and parse to extract two bytes.

Invalidation is per character rather than account_cache's global flush:
character saves are frequent (autosave), so a coarse flush would keep this
cache permanently cold.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

### Task 2: Cache invalidation on character write, and enable at boot

**Files:**
- Modify: `src/account_management_assets.cpp:29` (end of `write_account_character_file`)
- Modify: `src/account_management.cpp:1-7` (add `#include "roster_cache.h"`)
- Modify: `src/db.cpp:311` (add `roster_cache::set_enabled(true);` beside `account_cache::set_enabled(true);`), `src/db.cpp:28` (add the include)
- Test: `src/tests/account_management_tests.cpp`

**Interfaces:**
- Consumes: `roster_cache::invalidate_character`, `roster_cache::get`, `roster_cache::set_enabled` from Task 1.
- Produces: the guarantee that a character's summary is dropped whenever its file is written. Task 3 relies on this.

- [ ] **Step 1: Write the failing test**

Append to `src/tests/account_management_tests.cpp`:

```cpp
// write_account_character_file is the single chokepoint for character-file writes (5 call sites,
// including save_char's autosave path). If it does not drop the cached summary, a level-up would
// leave the roster showing the old level indefinitely.
TEST(AccountManagement, WritingACharacterFileDropsItsCachedRosterSummary)
{
    TemporaryDirectory temp_directory;
    ScopedWorkingDirectory working_directory(temp_directory.path());
    ASSERT_EQ(mkdir("accounts", 0700), 0);
    ASSERT_EQ(mkdir("accounts/A-E", 0700), 0);

    account::AccountData account_data = make_account();
    std::string error_message;
    ASSERT_TRUE(account::create_account(".", account_data.account_name, account_data.normalized_email,
        "ValidPass1", 1700010200, nullptr, &error_message)) << error_message;

    char_file_u aragorn = make_stored_character("aragorn");
    aragorn.level = 10;
    aragorn.race = RACE_WOOD;
    ASSERT_TRUE(account::write_account_character_file(".", account_data.account_name, aragorn, &error_message))
        << error_message;

    roster_cache::clear();
    roster_cache::set_enabled(true);

    roster_cache::RosterSummary summary {};
    ASSERT_TRUE(roster_cache::get(".", account_data.account_name, "aragorn", &summary));
    ASSERT_EQ(summary.level, 10);

    aragorn.level = 11;
    ASSERT_TRUE(account::write_account_character_file(".", account_data.account_name, aragorn, &error_message))
        << error_message;

    ASSERT_TRUE(roster_cache::get(".", account_data.account_name, "aragorn", &summary));
    EXPECT_EQ(summary.level, 11) << "cached summary survived a character write";

    roster_cache::set_enabled(false);
    roster_cache::clear();
}
```

Add `#include "../roster_cache.h"` to the includes at the top of `src/tests/account_management_tests.cpp`.

- [ ] **Step 2: Run test to verify it fails**

Run: `scripts/rots-docker.sh test --gtest_filter='AccountManagement.WritingACharacterFileDropsItsCachedRosterSummary'`
Expected: FAIL with `cached summary survived a character write`, `summary.level` being 10.

- [ ] **Step 3: Add the invalidation**

In `src/account_management.cpp`, add after line 7 (`#include "utils.h"`):

```cpp
#include "roster_cache.h"
```

In `src/account_management_assets.cpp`, replace the final line of `write_account_character_file`:

```cpp
    return write_text_file_atomically(final_path, json, error_message);
```

with:

```cpp
    if (!write_text_file_atomically(final_path, json, error_message))
        return false;

    // Single character-file write chokepoint: drop this character's cached roster summary so the
    // next render reflects the new level/race/coefficients. Only this character -- see roster_cache.h.
    roster_cache::invalidate_character(root_directory, stored_character.name);
    return true;
```

- [ ] **Step 4: Run test to verify it passes**

Run: `scripts/rots-docker.sh test --gtest_filter='AccountManagement.WritingACharacterFileDropsItsCachedRosterSummary'`
Expected: PASS.

- [ ] **Step 5: Enable the cache on the live server**

In `src/db.cpp`, add beside the existing `#include "account_cache.h"` on line 28:

```cpp
#include "roster_cache.h"
```

Immediately after line 311 (`account_cache::set_enabled(true);`) add:

```cpp
    roster_cache::set_enabled(true);
```

- [ ] **Step 6: Verify the server still builds**

Run: `scripts/rots-docker.sh compile`
Expected: links `bin/ageland` with no errors.

- [ ] **Step 7: Run the account suites**

Run: `scripts/rots-docker.sh test --gtest_filter='AccountManagement.*:RosterCacheTest.*'`
Expected: all pass except the pre-existing `FormatsOutOfRangeSummaryTimestampsAsInvalid`.

- [ ] **Step 8: Commit**

```bash
git add src/account_management.cpp src/account_management_assets.cpp src/db.cpp src/tests/account_management_tests.cpp
git commit -m "feat(account): invalidate roster summaries on character write

Hooks the single write_account_character_file chokepoint so a save drops
exactly that character's summary, and enables the cache on the live server
at boot beside account_cache.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

### Task 3: Ordering and filtering

**Files:**
- Modify: `src/account_management.cpp` (anonymous namespace, near `format_account_character_short_roster` at line 120)
- Modify: `src/account_management.h` (export the enums and the ordering function)
- Test: `src/tests/account_management_tests.cpp`

**Interfaces:**
- Consumes: `roster_cache::get`, `roster_cache::RosterSummary` from Task 1.
- Produces:
  - `enum class account::RosterSort { Account, Name, Level, Race, Side }`
  - `enum class account::RosterFilter { None, Warrior, Ranger, Mystic, Mage }`
  - `std::vector<size_t> account::ordered_roster_indices(const std::string& root_directory, const AccountData& account, RosterSort sort, RosterFilter filter)` — returns indices into `account.characters`, already sorted, already filtered, already truncated to the display cap. **Tasks 4 and 5 both consume this and must not re-derive it.**
  - `const char* account::roster_sort_to_string(RosterSort)` / `bool account::roster_sort_from_string(const std::string&, RosterSort*)`

- [ ] **Step 1: Write the failing test**

Append to `src/tests/account_management_tests.cpp`:

```cpp
namespace {

// Builds an account whose characters are deliberately NOT in name, level, or race order, so a
// passing sort test cannot be an accident of insertion order.
account::AccountData make_sortable_account()
{
    account::AccountData account_data = make_account();
    account_data.characters = { "gimli", "aragorn", "legolas" };
    return account_data;
}

// Backing reader for ordering tests: level/race/coefficients per character name.
bool sortable_reader(const std::string&, const std::string&, const std::string& character_name,
    char_file_u* stored_character, std::string* error_message)
{
    *stored_character = char_file_u {};
    if (character_name == "gimli") {
        stored_character->level = 30;
        stored_character->race = RACE_DWARF;
        stored_character->profs.prof_coof[PROF_WARRIOR] = 160;
    } else if (character_name == "aragorn") {
        stored_character->level = 50;
        stored_character->race = RACE_HUMAN;
        stored_character->profs.prof_coof[PROF_RANGER] = 160;
    } else if (character_name == "legolas") {
        stored_character->level = 40;
        stored_character->race = RACE_WOOD;
        stored_character->profs.prof_coof[PROF_MAGE] = 160;
    } else {
        if (error_message)
            *error_message = "unknown character";
        return false;
    }
    if (error_message)
        *error_message = "";
    return true;
}

std::vector<std::string> names_in_order(const account::AccountData& account_data,
    account::RosterSort sort, account::RosterFilter filter)
{
    const std::vector<size_t> indices = account::ordered_roster_indices(".", account_data, sort, filter);
    std::vector<std::string> names;
    for (size_t index : indices)
        names.push_back(account_data.characters[index]);
    return names;
}

class RosterOrderTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        roster_cache::clear();
        roster_cache::set_backing_reader_for_testing(&sortable_reader);
        roster_cache::set_enabled(true);
    }
    void TearDown() override
    {
        roster_cache::set_backing_reader_for_testing(nullptr);
        roster_cache::set_enabled(false);
        roster_cache::clear();
    }
};

} // namespace

TEST_F(RosterOrderTest, AccountSortPreservesInsertionOrder)
{
    const account::AccountData account_data = make_sortable_account();
    EXPECT_EQ(names_in_order(account_data, account::RosterSort::Account, account::RosterFilter::None),
        (std::vector<std::string> { "gimli", "aragorn", "legolas" }));
}

TEST_F(RosterOrderTest, NameSortIsAlphabetical)
{
    const account::AccountData account_data = make_sortable_account();
    EXPECT_EQ(names_in_order(account_data, account::RosterSort::Name, account::RosterFilter::None),
        (std::vector<std::string> { "aragorn", "gimli", "legolas" }));
}

TEST_F(RosterOrderTest, LevelSortIsHighestFirst)
{
    const account::AccountData account_data = make_sortable_account();
    EXPECT_EQ(names_in_order(account_data, account::RosterSort::Level, account::RosterFilter::None),
        (std::vector<std::string> { "aragorn", "legolas", "gimli" }));
}

TEST_F(RosterOrderTest, RaceSortIsAscendingByRaceIndex)
{
    // RACE_HUMAN 1 < RACE_DWARF 2 < RACE_WOOD 3
    const account::AccountData account_data = make_sortable_account();
    EXPECT_EQ(names_in_order(account_data, account::RosterSort::Race, account::RosterFilter::None),
        (std::vector<std::string> { "aragorn", "gimli", "legolas" }));
}

TEST_F(RosterOrderTest, FilterKeepsOnlyCharactersWhoseHighestCoefficientMatches)
{
    const account::AccountData account_data = make_sortable_account();
    EXPECT_EQ(names_in_order(account_data, account::RosterSort::Account, account::RosterFilter::Warrior),
        (std::vector<std::string> { "gimli" }));
    EXPECT_EQ(names_in_order(account_data, account::RosterSort::Account, account::RosterFilter::Ranger),
        (std::vector<std::string> { "aragorn" }));
    EXPECT_EQ(names_in_order(account_data, account::RosterSort::Account, account::RosterFilter::Mage),
        (std::vector<std::string> { "legolas" }));
    EXPECT_TRUE(names_in_order(account_data, account::RosterSort::Account, account::RosterFilter::Mystic).empty());
}

TEST_F(RosterOrderTest, FilterAndSortCompose)
{
    account::AccountData account_data = make_sortable_account();
    account_data.characters.push_back("gimli");   // duplicate name is fine: same summary, same filter
    EXPECT_EQ(names_in_order(account_data, account::RosterSort::Level, account::RosterFilter::Warrior),
        (std::vector<std::string> { "gimli", "gimli" }));
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `scripts/rots-docker.sh test --gtest_filter='RosterOrderTest.*'`
Expected: compile failure — `account::ordered_roster_indices` and the enums do not exist.

- [ ] **Step 3: Declare the interface**

In `src/account_management.h`, inside `namespace account`, add:

```cpp
enum class RosterSort {
    Account, // insertion order: the pre-feature behaviour, and the default for accounts that never chose
    Name,
    Level,
    Race,
    Side,
};

enum class RosterFilter {
    None,
    Warrior,
    Ranger,
    Mystic,
    Mage,
};

// The single ordered, filtered, capped list of positions into account.characters. Rendering and
// selection MUST both consume this rather than each deriving an order, or a character can be shown
// at a number that selects someone else.
std::vector<size_t> ordered_roster_indices(const std::string& root_directory,
    const AccountData& account, RosterSort sort, RosterFilter filter);

const char* roster_sort_to_string(RosterSort sort);
bool roster_sort_from_string(const std::string& value, RosterSort* sort);
```

Ensure `#include <vector>` is present in that header.

- [ ] **Step 4: Implement**

In `src/account_management.cpp`, inside the anonymous namespace above `format_account_character_short_roster` (line 120), add:

```cpp
    // Derived coefficient for one profession, mirroring get_prof_coof (char_utils.cpp:363) but
    // reading the cached raw value instead of a live char_data. The race adjustments matter: they
    // change WHICH profession is highest for Orcs and Uruk mages, so filtering on raw values would
    // put those characters under the wrong letter.
    int derived_prof_coof(const roster_cache::RosterSummary& summary, int profession)
    {
        extern sh_int square_root[];
        const short raw = summary.prof_coof[profession];
        int derived = square_root[raw];
        if (summary.race == RACE_ORC)
            derived = (derived * 2 + 2) / 3;
        else if (summary.race == RACE_URUK && profession == PROF_MAGE)
            derived -= 100;
        return derived;
    }

    // Side ordering: gods, lights, darks, third side. Derived from race, never stored.
    int side_rank_for_race(int race)
    {
        if (race == RACE_GOD)
            return 0;
        if (race >= RACE_HUMAN && race <= RACE_BEORNING)
            return 1;
        if (race == RACE_MAGUS || race == RACE_HARADRIM)
            return 3;
        return 2;
    }

    bool summary_matches_filter(const roster_cache::RosterSummary& summary, RosterFilter filter)
    {
        if (filter == RosterFilter::None)
            return true;
        if (!summary.readable)
            return false; // no coefficients known; spec says unreadable rows are excluded by filters

        int wanted = PROF_WARRIOR;
        if (filter == RosterFilter::Ranger)
            wanted = PROF_RANGER;
        else if (filter == RosterFilter::Mystic)
            wanted = PROF_CLERIC;
        else if (filter == RosterFilter::Mage)
            wanted = PROF_MAGE;

        const int wanted_value = derived_prof_coof(summary, wanted);
        for (int profession = 1; profession <= MAX_PROFS; ++profession) {
            if (profession == wanted)
                continue;
            if (derived_prof_coof(summary, profession) > wanted_value)
                return false;
        }
        // >= every other profession, so ties match under every tied letter.
        return true;
    }
```

Then add the public function, outside the anonymous namespace but inside `namespace account`, next to the other public functions in `account_management.cpp`:

```cpp
std::vector<size_t> ordered_roster_indices(const std::string& root_directory,
    const AccountData& account, RosterSort sort, RosterFilter filter)
{
    std::vector<size_t> indices;
    std::vector<roster_cache::RosterSummary> summaries(account.characters.size());

    for (size_t index = 0; index < account.characters.size(); ++index) {
        roster_cache::get(root_directory, account.account_name, account.characters[index], &summaries[index]);
        if (summary_matches_filter(summaries[index], filter))
            indices.push_back(index);
    }

    // stable_sort so equal keys keep insertion order and a redraw never reshuffles them.
    // Unreadable characters have no level/race/coefficients and sort last under every ordering.
    if (sort != RosterSort::Account) {
        std::stable_sort(indices.begin(), indices.end(),
            [&](size_t left, size_t right) {
                const roster_cache::RosterSummary& a = summaries[left];
                const roster_cache::RosterSummary& b = summaries[right];
                if (a.readable != b.readable)
                    return a.readable;
                if (!a.readable)
                    return false;

                if (sort == RosterSort::Name)
                    return to_lower_copy(account.characters[left]) < to_lower_copy(account.characters[right]);
                if (sort == RosterSort::Level)
                    return a.level > b.level;
                if (sort == RosterSort::Race)
                    return a.race < b.race;
                return side_rank_for_race(a.race) < side_rank_for_race(b.race);
            });
    }

    if (indices.size() > kMaxDisplayedAccountCharacters)
        indices.resize(kMaxDisplayedAccountCharacters);
    return indices;
}

const char* roster_sort_to_string(RosterSort sort)
{
    switch (sort) {
    case RosterSort::Name:
        return "name";
    case RosterSort::Level:
        return "level";
    case RosterSort::Race:
        return "race";
    case RosterSort::Side:
        return "side";
    default:
        return "";
    }
}

bool roster_sort_from_string(const std::string& value, RosterSort* sort)
{
    if (sort == nullptr)
        return false;
    if (value.empty()) {
        *sort = RosterSort::Account;
        return true;
    }
    if (value == "name") {
        *sort = RosterSort::Name;
        return true;
    }
    if (value == "level") {
        *sort = RosterSort::Level;
        return true;
    }
    if (value == "race") {
        *sort = RosterSort::Race;
        return true;
    }
    if (value == "side") {
        *sort = RosterSort::Side;
        return true;
    }
    return false;
}
```

Add `#include <vector>` and confirm `<algorithm>` is already included (it is, line 21).

- [ ] **Step 5: Run tests to verify they pass**

Run: `scripts/rots-docker.sh test --gtest_filter='RosterOrderTest.*'`
Expected: 6 tests PASS.

- [ ] **Step 6: Add the side, tie, and unreadable tests**

Append to `src/tests/account_management_tests.cpp`:

```cpp
TEST_F(RosterOrderTest, SideSortOrdersGodsLightsDarksThenThirdSide)
{
    account::AccountData account_data = make_account();
    account_data.characters = { "magus1", "orc1", "human1" };

    roster_cache::set_backing_reader_for_testing(
        [](const std::string&, const std::string&, const std::string& character_name,
            char_file_u* stored_character, std::string* error_message) -> bool {
            *stored_character = char_file_u {};
            if (character_name == "magus1")
                stored_character->race = RACE_MAGUS;
            else if (character_name == "orc1")
                stored_character->race = RACE_ORC;
            else
                stored_character->race = RACE_HUMAN;
            if (error_message)
                *error_message = "";
            return true;
        });
    roster_cache::clear();

    EXPECT_EQ(names_in_order(account_data, account::RosterSort::Side, account::RosterFilter::None),
        (std::vector<std::string> { "human1", "orc1", "magus1" }));
}

TEST_F(RosterOrderTest, TiedHighestCoefficientAppearsUnderEveryTiedFilter)
{
    account::AccountData account_data = make_account();
    account_data.characters = { "twinned" };

    roster_cache::set_backing_reader_for_testing(
        [](const std::string&, const std::string&, const std::string&,
            char_file_u* stored_character, std::string* error_message) -> bool {
            *stored_character = char_file_u {};
            stored_character->race = RACE_HUMAN;
            stored_character->profs.prof_coof[PROF_WARRIOR] = 150;
            stored_character->profs.prof_coof[PROF_MAGE] = 150;
            if (error_message)
                *error_message = "";
            return true;
        });
    roster_cache::clear();

    EXPECT_EQ(names_in_order(account_data, account::RosterSort::Account, account::RosterFilter::Warrior).size(), 1u);
    EXPECT_EQ(names_in_order(account_data, account::RosterSort::Account, account::RosterFilter::Mage).size(), 1u);
    EXPECT_TRUE(names_in_order(account_data, account::RosterSort::Account, account::RosterFilter::Ranger).empty());
}

TEST_F(RosterOrderTest, UnreadableCharactersSortLastAndAreExcludedByFilters)
{
    account::AccountData account_data = make_sortable_account();
    account_data.characters.insert(account_data.characters.begin(), "brokenchar");

    // sortable_reader returns false for "brokenchar".
    const std::vector<std::string> by_level =
        names_in_order(account_data, account::RosterSort::Level, account::RosterFilter::None);
    ASSERT_EQ(by_level.size(), 4u);
    EXPECT_EQ(by_level.back(), "brokenchar");

    const std::vector<std::string> warriors =
        names_in_order(account_data, account::RosterSort::Account, account::RosterFilter::Warrior);
    EXPECT_EQ(warriors, (std::vector<std::string> { "gimli" }));
}

TEST_F(RosterOrderTest, OrderingIsCappedAtTheDisplayedRosterLimit)
{
    account::AccountData account_data = make_account();
    account_data.characters.clear();
    for (int index = 1; index <= 250; ++index)
        account_data.characters.push_back("character" + std::to_string(index));

    roster_cache::set_backing_reader_for_testing(
        [](const std::string&, const std::string&, const std::string&,
            char_file_u* stored_character, std::string* error_message) -> bool {
            *stored_character = char_file_u {};
            stored_character->level = 1;
            stored_character->race = RACE_HUMAN;
            if (error_message)
                *error_message = "";
            return true;
        });
    roster_cache::clear();

    EXPECT_EQ(account::ordered_roster_indices(".", account_data,
                  account::RosterSort::Name, account::RosterFilter::None).size(), 200u);
}
```

- [ ] **Step 7: Run tests to verify they pass**

Run: `scripts/rots-docker.sh test --gtest_filter='RosterOrderTest.*'`
Expected: 10 tests PASS.

- [ ] **Step 8: Commit**

```bash
git add src/account_management.h src/account_management.cpp src/tests/account_management_tests.cpp
git commit -m "feat(account): order and filter the linked-character roster

Adds ordered_roster_indices as the single source of roster order: sorted,
filtered, and capped. Coefficient filters compare DERIVED values so the Orc
and Uruk-mage race adjustments decide which profession is highest, and ties
match under every tied letter.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

### Task 4: Render and select from the ordered list

**Files:**
- Modify: `src/account_management.cpp:102-141` (`format_account_character_short_entry`, `format_account_character_short_roster`)
- Modify: `src/account_management_presentation.cpp:26-46` (`format_account_character_prompt`, `format_account_character_list`)
- Modify: `src/account_management_presentation.h`
- Modify: `src/account_management_identity.cpp:327-377` (`select_linked_character`)
- Modify: `src/account_management_identity.h`
- Test: `src/tests/account_management_tests.cpp`

**Interfaces:**
- Consumes: `account::ordered_roster_indices`, `RosterSort`, `RosterFilter` from Task 3.
- Produces: `format_account_character_prompt(root, account, RosterSort, RosterFilter)` and `select_linked_character(account, input, RosterSort, RosterFilter, std::string* out, std::string* error)`. Task 5 calls both.

The old 2-argument `format_account_character_prompt` and 3-argument `select_linked_character` are replaced, not overloaded — leaving the old ones would let a caller silently bypass the ordering and reintroduce the display/selection mismatch.

- [ ] **Step 1: Write the failing test**

Append to `src/tests/account_management_tests.cpp`:

```cpp
// The PR #289 invariant, generalised: whatever row N shows must be what typing N selects, under
// every sort and every filter. This is the single most important test in the feature.
TEST_F(RosterOrderTest, SelectionByNumberMatchesTheRenderedRowUnderEverySortAndFilter)
{
    const account::AccountData account_data = make_sortable_account();

    const account::RosterSort sorts[] = { account::RosterSort::Account, account::RosterSort::Name,
        account::RosterSort::Level, account::RosterSort::Race, account::RosterSort::Side };
    const account::RosterFilter filters[] = { account::RosterFilter::None,
        account::RosterFilter::Warrior, account::RosterFilter::Ranger, account::RosterFilter::Mage };

    for (account::RosterSort sort : sorts) {
        for (account::RosterFilter filter : filters) {
            const std::vector<std::string> displayed = names_in_order(account_data, sort, filter);
            for (size_t row = 0; row < displayed.size(); ++row) {
                std::string selected;
                std::string error_message;
                ASSERT_TRUE(account::select_linked_character(account_data,
                    std::to_string(row + 1), sort, filter, &selected, &error_message))
                    << "row " << (row + 1) << ": " << error_message;
                EXPECT_EQ(selected, displayed[row])
                    << "row " << (row + 1) << " renders " << displayed[row]
                    << " but selects " << selected;
            }

            std::string selected;
            std::string error_message;
            EXPECT_FALSE(account::select_linked_character(account_data,
                std::to_string(displayed.size() + 1), sort, filter, &selected, &error_message))
                << "a row past the end of the displayed list was selectable";
        }
    }
}

TEST_F(RosterOrderTest, SelectionByNameWorksForACharacterHiddenByTheActiveFilter)
{
    const account::AccountData account_data = make_sortable_account();

    std::string selected;
    std::string error_message;
    // "aragorn" is a Ranger, so the Warrior filter hides them; selecting by name must still work,
    // because a filter must never make one of a player's characters unreachable.
    ASSERT_TRUE(account::select_linked_character(account_data, "aragorn",
        account::RosterSort::Account, account::RosterFilter::Warrior, &selected, &error_message))
        << error_message;
    EXPECT_EQ(selected, "aragorn");
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `scripts/rots-docker.sh test --gtest_filter='RosterOrderTest.Selection*'`
Expected: compile failure — `select_linked_character` has no 6-argument form.

- [ ] **Step 3: Change the entry formatter to take a summary**

In `src/account_management.cpp`, replace `format_account_character_short_entry` (lines 102-118) with:

```cpp
    std::string format_account_character_short_entry(size_t display_row,
        const std::string& character_name, const roster_cache::RosterSummary& summary)
    {
        const std::string display_name = format_character_name_for_display(character_name);

        char line[256];
        if (!summary.readable) {
            std::snprintf(line, sizeof(line), "%zu) [ ?? ???] %-12.12s", display_row, display_name.c_str());
            return line;
        }

        std::snprintf(line, sizeof(line), "%zu) [%3d %s] %-12.12s", display_row,
            summary.level, safe_race_abbrev(summary.race), display_name.c_str());
        return line;
    }
```

- [ ] **Step 4: Change the roster formatter to walk the ordered list**

Replace `format_account_character_short_roster` (lines 120-141) with:

```cpp
    std::string format_account_character_short_roster(const std::string& root_directory,
        const AccountData& account, RosterSort sort, RosterFilter filter)
    {
        if (account.characters.empty())
            return "\n\rNo linked characters yet.\n\r";

        const std::vector<size_t> indices = ordered_roster_indices(root_directory, account, sort, filter);
        if (indices.empty())
            return "\n\rNo linked characters match that filter.\n\r";

        std::ostringstream output;
        for (size_t row = 0; row < indices.size(); ++row) {
            roster_cache::RosterSummary summary {};
            roster_cache::get(root_directory, account.account_name, account.characters[indices[row]], &summary);
            output << format_account_character_short_entry(row + 1, account.characters[indices[row]], summary);
            if ((row + 1) % 2 == 0)
                output << "\n\r";
        }

        if (indices.size() % 2 != 0)
            output << "\n\r";

        output << "\n\r";
        if (filter != RosterFilter::None) {
            // Never let a filtered roster be mistaken for the whole roster.
            output << indices.size() << " of " << account.characters.size()
                   << " characters shown (" << roster_filter_label(filter) << ").  Press "
                   << roster_filter_key(filter) << " to clear.\n\r";
        } else {
            if (account.characters.size() > indices.size())
                output << "... and " << (account.characters.size() - indices.size()) << " more\n\r\n\r";
            output << indices.size() << " character" << (indices.size() == 1 ? "" : "s") << " displayed.\n\r";
        }
        return output.str();
    }
```

Add these two helpers to the same anonymous namespace, above that function:

```cpp
    const char* roster_filter_label(RosterFilter filter)
    {
        switch (filter) {
        case RosterFilter::Warrior:
            return "Warrior";
        case RosterFilter::Ranger:
            return "Ranger";
        case RosterFilter::Mystic:
            return "Mystic";
        case RosterFilter::Mage:
            return "Mage";
        default:
            return "";
        }
    }

    char roster_filter_key(RosterFilter filter)
    {
        switch (filter) {
        case RosterFilter::Warrior:
            return 'W';
        case RosterFilter::Ranger:
            return 'R';
        case RosterFilter::Mystic:
            return 'T';
        case RosterFilter::Mage:
            return 'M';
        default:
            return ' ';
        }
    }
```

- [ ] **Step 5: Update the prompt and list formatters**

In `src/account_management_presentation.h`, replace the two declarations with:

```cpp
std::string format_account_character_prompt(const std::string& root_directory,
    const AccountData& account, RosterSort sort, RosterFilter filter);
std::string format_account_character_list(const std::string& root_directory,
    const AccountData& account, RosterSort sort);
```

In `src/account_management_presentation.cpp`, replace both function bodies:

```cpp
std::string format_account_character_prompt(const std::string& root_directory,
    const AccountData& account, RosterSort sort, RosterFilter filter)
{
    std::ostringstream output;
    output << "\n\rLinked characters for your account:\n\r";
    output << format_account_character_short_roster(root_directory, account, sort, filter);
    output << "\n\rSort: (A)-Z  (L)evel  ra(C)e  (S)ide      Show only: (W)arrior (R)anger (T)mystic (M)age\n\r";
    output << "0) Back to Account Menu.\n\r";
    output << "\n\rCharacter number or name: ";
    return output.str();
}

std::string format_account_character_list(const std::string& root_directory,
    const AccountData& account, RosterSort sort)
{
    if (account.characters.empty())
        return "\n\rNo linked characters yet.\n\r";

    std::ostringstream output;
    output << "\n\rLinked characters:\n\r";
    output << format_account_character_short_roster(root_directory, account, sort, RosterFilter::None);
    return output.str();
}
```

- [ ] **Step 6: Update selection to resolve against the same list**

In `src/account_management_identity.h`, replace the declaration with:

```cpp
bool select_linked_character(const AccountData& account, const std::string& character_name,
    RosterSort sort, RosterFilter filter, std::string* normalized_character_name,
    std::string* error_message = nullptr);
```

In `src/account_management_identity.cpp`, replace `select_linked_character` (lines 327-377) with:

```cpp
bool select_linked_character(const AccountData& account, const std::string& character_name,
    RosterSort sort, RosterFilter filter, std::string* normalized_character_name,
    std::string* error_message)
{
    if (normalized_character_name == nullptr) {
        set_error(error_message, "Character output parameter must not be null.");
        return false;
    }

    const std::string trimmed_selection = trim_copy(character_name);
    if (trimmed_selection.empty()) {
        set_error(error_message, "Character selection must not be empty.");
        return false;
    }

    bool selection_is_numeric = true;
    for (char character : trimmed_selection) {
        if (!std::isdigit(static_cast<unsigned char>(character))) {
            selection_is_numeric = false;
            break;
        }
    }

    // The SAME ordered list the roster rendered, so row N always selects the character shown at
    // row N. Deriving an order here independently is what would reintroduce the PR #289 class of bug.
    const std::vector<size_t> indices = ordered_roster_indices(".", account, sort, filter);

    if (selection_is_numeric) {
        char* end_ptr = nullptr;
        const unsigned long selected_row = std::strtoul(trimmed_selection.c_str(), &end_ptr, 10);
        if (end_ptr == nullptr || *end_ptr != '\0' || selected_row == 0 || selected_row > indices.size()) {
            set_error(error_message, "Select a linked character by number or name, or enter 0 to return to the account menu.");
            return false;
        }

        *normalized_character_name = normalize_account_name(account.characters[indices[selected_row - 1]]);
        set_error(error_message, "");
        return true;
    }

    // By name, the active filter is deliberately ignored: a filter narrows what is LISTED, it must
    // never make one of the player's own characters unreachable. Bounded by the display cap so a
    // character past it stays as unselectable as it is invisible.
    const std::string normalized_selection = normalize_account_name(trimmed_selection);
    const size_t searchable_count = std::min(account.characters.size(), kMaxDisplayedAccountCharacters);
    for (size_t index = 0; index < searchable_count; ++index) {
        const std::string normalized_linked_name = normalize_account_name(account.characters[index]);
        if (normalized_linked_name != normalized_selection)
            continue;

        *normalized_character_name = normalized_linked_name;
        set_error(error_message, "");
        return true;
    }

    set_error(error_message, "Select a linked character by number or name, or enter 0 to return to the account menu.");
    return false;
}
```

- [ ] **Step 7: Fix the existing callers so the tree compiles**

`src/interpre.cpp:2828` and `:3400` and `:3656` now pass the wrong argument counts. For this task only, pass the defaults so the tree builds; Task 5 replaces them with real state:

- `interpre.cpp:2828` → `account::format_account_character_prompt(kAccountStorageRoot, account_data, account::RosterSort::Account, account::RosterFilter::None)`
- `interpre.cpp:3400` → `account::select_linked_character(account_data, arg, account::RosterSort::Account, account::RosterFilter::None, &selected_character_name, &error_message)`
- `interpre.cpp:2723` → `account::format_account_character_list(kAccountStorageRoot, account_data, account::RosterSort::Account)`

Update the existing tests that call the old signatures: `AccountManagement.SelectsOnlyCharactersLinkedToTheAccount` (line 664), `SelectsLinkedCharactersByFullName` (679), `SelectsLinkedCharactersByNameIgnoringCaseAndSurroundingSpaces` (692), `RejectsSelectionsBeyondTheDisplayedRosterRange` (721), `RendersAFullRosterWithinTheOutputBuffer`, `FormatsCharacterPromptWithLinkedCharacterList` (1000), and `InterpreAccountMenu.ShowAccountCharacterListTruncatesVeryLargeRenderedLists` (620) — add `account::RosterSort::Account` and, where required, `account::RosterFilter::None`.

`FormatsCharacterPromptWithLinkedCharacterList` also asserts the exact prompt text; add the new `Sort:` line to its expectation.

- [ ] **Step 8: Run tests to verify they pass**

Run: `scripts/rots-docker.sh test --gtest_filter='RosterOrderTest.*:AccountManagement.*:InterpreAccountMenu.*'`
Expected: all pass except the pre-existing `FormatsOutOfRangeSummaryTimestampsAsInvalid`.

- [ ] **Step 9: Verify the buffer guard still holds with the longer footer**

Run: `scripts/rots-docker.sh test --gtest_filter='AccountManagement.RendersAFullRosterWithinTheOutputBuffer'`
Expected: PASS. The prompt grew by the `Sort:` legend (~95 bytes); at 200 rows the roster is ~5.7 KB against `LARGE_BUFSIZE` 16384, so headroom remains large.

- [ ] **Step 10: Commit**

```bash
git add src/account_management.cpp src/account_management_presentation.cpp src/account_management_presentation.h src/account_management_identity.cpp src/account_management_identity.h src/interpre.cpp src/tests/account_management_tests.cpp src/tests/interpre_account_menu_tests.cpp
git commit -m "feat(account): render and select from one ordered roster list

Both the renderer and select_linked_character now consume
ordered_roster_indices, so row N always selects the character shown at row N
under any sort or filter. Selection by name ignores the active filter, so a
filter can never make one of a player's characters unreachable.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

### Task 5: Wire the keys and persist the sort

**Files:**
- Modify: `src/account_management_types.h:44` (add `roster_sort` to `AccountData`)
- Modify: `src/account_management_storage.cpp:119` (serialise), `src/account_management.cpp:1370` (parse)
- Modify: `src/structs.h` (add two fields to `descriptor_data`), `src/interpre.cpp:3369` (`CON_ACCTSLCT` handler)
- Test: `src/tests/account_management_tests.cpp`, `src/tests/interpre_account_menu_tests.cpp`

**Interfaces:**
- Consumes: everything from Tasks 3 and 4.
- Produces: the finished feature. Nothing depends on this task.

- [ ] **Step 1: Write the failing persistence test**

Append to `src/tests/account_management_tests.cpp`:

```cpp
TEST(AccountManagement, RosterSortRoundTripsThroughAccountJson)
{
    account::AccountData account_data = make_account();
    account_data.roster_sort = "level";

    const std::string json = account::serialize_account_to_json(account_data);
    EXPECT_NE(json.find("\"roster_sort\": \"level\""), std::string::npos) << json;

    account::AccountData parsed_account;
    std::string error_message;
    ASSERT_TRUE(account::deserialize_account_from_json(json, &parsed_account, &error_message)) << error_message;
    EXPECT_EQ(parsed_account.roster_sort, "level");
}

// Existing account files predate this field. They must load and fall back to insertion order,
// which is why no schema-version bump or migration is needed.
TEST(AccountManagement, AccountJsonWithoutRosterSortLoadsWithInsertionOrder)
{
    account::AccountData account_data = make_account();
    std::string json = account::serialize_account_to_json(account_data);

    const size_t field_start = json.find("  \"roster_sort\"");
    ASSERT_NE(field_start, std::string::npos);
    const size_t field_end = json.find('\n', field_start);
    json.erase(field_start, field_end - field_start + 1);

    account::AccountData parsed_account;
    std::string error_message;
    ASSERT_TRUE(account::deserialize_account_from_json(json, &parsed_account, &error_message)) << error_message;
    EXPECT_TRUE(parsed_account.roster_sort.empty());

    account::RosterSort sort = account::RosterSort::Name;
    ASSERT_TRUE(account::roster_sort_from_string(parsed_account.roster_sort, &sort));
    EXPECT_EQ(sort, account::RosterSort::Account);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `scripts/rots-docker.sh test --gtest_filter='AccountManagement.*RosterSort*'`
Expected: compile failure — `AccountData` has no `roster_sort`.

- [ ] **Step 3: Add the field, serialiser, and parser**

In `src/account_management_types.h`, after `std::vector<CharacterLinkReference> character_links;` (line 44):

```cpp
    // Empty means "never chosen": the roster keeps insertion order, matching pre-feature behaviour.
    // One of "", "name", "level", "race", "side".
    std::string roster_sort;
```

In `src/account_management_storage.cpp`, change the last serialised line (currently `password_reset_attempt_count` with a trailing `\n` and no comma) to add a comma, then append the new field as the last entry:

```cpp
    output << "  \"password_reset_attempt_count\": " << account.password_reset_attempt_count << ",\n";
    output << "  \"roster_sort\": \"" << json_utils::escape_json_string(account.roster_sort) << "\"\n";
```

In `src/account_management.cpp`, in `parse_account_property`, before the final `return reader->skip_value(error_message);` (line 1372):

```cpp
        if (key == "roster_sort")
            return reader->parse_string(&account->roster_sort, error_message);
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `scripts/rots-docker.sh test --gtest_filter='AccountManagement.*RosterSort*'`
Expected: 2 tests PASS.

- [ ] **Step 5: Add per-descriptor roster state**

In `src/structs.h`, inside `struct descriptor_data`, beside the other account fields:

```cpp
    int roster_sort; /* account::RosterSort for this session's roster */
    int roster_filter; /* account::RosterFilter; transient, never persisted */
    bool roster_sort_dirty; /* sort changed this visit; write it on leaving the roster */
```

Initialise all three in `comm.cpp` where the descriptor is set up (the same place `bufspace` is initialised, around line 1613): `pnewd->roster_sort = 0; pnewd->roster_filter = 0; pnewd->roster_sort_dirty = false;`

- [ ] **Step 6: Handle the keys**

In `src/interpre.cpp`, in `case CON_ACCTSLCT:` (line 3369), after the account is reloaded and before the `"0"` check, insert:

```cpp
            // Single-letter sort and filter keys. Unambiguous because character names are a minimum
            // of 3 characters, enforced by valid_name (ban.cpp) and is_valid_account_name.
            if (strlen(arg) == 1) {
                const char key = LOWER(*arg);
                account::RosterSort new_sort = static_cast<account::RosterSort>(d->roster_sort);
                account::RosterFilter new_filter = static_cast<account::RosterFilter>(d->roster_filter);
                bool handled = true;
                bool sort_changed = false;

                switch (key) {
                case 'a': new_sort = account::RosterSort::Name; sort_changed = true; break;
                case 'l': new_sort = account::RosterSort::Level; sort_changed = true; break;
                case 'c': new_sort = account::RosterSort::Race; sort_changed = true; break;
                case 's': new_sort = account::RosterSort::Side; sort_changed = true; break;
                case 'w': new_filter = (new_filter == account::RosterFilter::Warrior) ? account::RosterFilter::None : account::RosterFilter::Warrior; break;
                case 'r': new_filter = (new_filter == account::RosterFilter::Ranger) ? account::RosterFilter::None : account::RosterFilter::Ranger; break;
                case 't': new_filter = (new_filter == account::RosterFilter::Mystic) ? account::RosterFilter::None : account::RosterFilter::Mystic; break;
                case 'm': new_filter = (new_filter == account::RosterFilter::Mage) ? account::RosterFilter::None : account::RosterFilter::Mage; break;
                default: handled = false; break;
                }

                if (handled) {
                    d->roster_sort = static_cast<int>(new_sort);
                    d->roster_filter = static_cast<int>(new_filter);
                    if (sort_changed)
                        d->roster_sort_dirty = true;
                    show_account_character_prompt(d, account_data);
                    return;
                }
            }
```

Change `show_account_character_prompt` (line 2826) to pass the descriptor's state:

```cpp
void show_account_character_prompt(struct descriptor_data* d, const account::AccountData& account_data)
{
    const std::string prompt = account::format_account_character_prompt(kAccountStorageRoot, account_data,
        static_cast<account::RosterSort>(d->roster_sort),
        static_cast<account::RosterFilter>(d->roster_filter));
    SEND_TO_Q(prompt.c_str(), d);
}
```

Change the `select_linked_character` call at line 3400 to pass the same state.

- [ ] **Step 7: Persist on leaving the roster**

Still in `case CON_ACCTSLCT:`, inside the `if (!strcmp(arg, "0"))` branch, before `show_account_menu`:

```cpp
                // Write the sort only on leaving, never per keypress: write_account_file calls
                // account_cache::invalidate_all(), which drops every account's entry globally, and
                // save_char consumes that cache on every save (db.cpp) where a miss is a full scan
                // of every account.json on disk.
                if (d->roster_sort_dirty) {
                    account::RosterSort current_sort = static_cast<account::RosterSort>(d->roster_sort);
                    account_data.roster_sort = account::roster_sort_to_string(current_sort);
                    std::string persist_error;
                    if (!account::write_account_file(kAccountStorageRoot, account_data, &persist_error))
                        vmudlog(BRF, "Failed to persist roster sort for %s: %s",
                            d->account_name, persist_error.c_str());
                    d->roster_sort_dirty = false;
                }
```

Where the account menu enters the roster (the `case '2':` branch near line 3659), load the stored sort into the descriptor before showing the prompt:

```cpp
                account::RosterSort stored_sort = account::RosterSort::Account;
                account::roster_sort_from_string(account_data.roster_sort, &stored_sort);
                d->roster_sort = static_cast<int>(stored_sort);
                d->roster_filter = static_cast<int>(account::RosterFilter::None);
                d->roster_sort_dirty = false;
```

- [ ] **Step 8: Build and run the full account suites**

Run: `scripts/rots-docker.sh compile && scripts/rots-docker.sh test --gtest_filter='AccountManagement.*:InterpreAccountMenu.*:RosterOrderTest.*:RosterCacheTest.*'`
Expected: all pass except the pre-existing `FormatsOutOfRangeSummaryTimestampsAsInvalid`.

- [ ] **Step 9: Wire smoke coverage**

Extend `testing/smoke_roster_cap.py` into `testing/smoke_roster_sort.py`, reusing its login sequence (account `clauded3bugbot@example.com` / `TestPass123!`, menu choice `2`). Assert, against a booted server with an account of 150+ characters:

1. Pressing `a` renders rows in ascending name order.
2. Pressing `l` renders rows in descending level order.
3. Pressing `w` renders a footer matching `\d+ of \d+ characters shown \(Warrior\)`.
4. With `w` active, selecting `1` loads the character rendered at row 1 — not the first character of the unfiltered roster.
5. With `w` active, a character hidden by the filter is still selectable by name.
6. Pressing `w` again clears the filter and restores the full count.
7. Returning to the account menu with `0` and re-entering with `2` shows the roster still in the chosen sort.

- [ ] **Step 10: Boot and run the smoke test**

```bash
# 1024 may be in use by another worktree's server; pick a free port.
ROTS_UID=$(id -u) ROTS_GID=$(id -g) docker compose run --rm -p 1035:1035 rots \
  bash -lc 'cd /rots && exec ./bin/ageland 1035' &
python3 testing/smoke_roster_sort.py 1035
```

Expected: all checks pass. Stop the container afterwards.

- [ ] **Step 11: Commit**

```bash
git add src/account_management_types.h src/account_management_storage.cpp src/account_management.cpp src/structs.h src/comm.cpp src/interpre.cpp src/tests/account_management_tests.cpp testing/smoke_roster_sort.py
git commit -m "feat(account): sort and filter keys on the character roster

Adds a/l/c/s sort keys and w/r/t/m coefficient filters at the roster prompt,
and persists the sort on the account. The sort is written when the player
leaves the roster rather than per keypress: write_account_file flushes the
whole account cache globally, and save_char consumes that cache on every
save where a miss rescans every account.json on disk.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

## Self-Review

**Spec coverage.** Interface and key table → Task 5. Ordering within each sort, side order, unreadable-last → Task 3. Filter behaviour, toggle, footer, numbering-follows-display → Tasks 3 and 4. Highest-coefficient rule with derived values → Task 3. Persistence and the write-on-leaving rationale → Task 5. Summary cache, struct, footprint, per-character invalidation → Tasks 1 and 2. Testing items 1-11 → Tasks 1, 3, 4; item 12 → Task 5. Every "Files" row in the spec appears in a task.

**Placeholders.** None. Every code step carries the code.

**Type consistency.** `RosterSummary` fields (`level`, `race`, `readable`, `prof_coof`) are identical in Tasks 1, 3, 4. `ordered_roster_indices` keeps one signature across Tasks 3, 4, 5. `RosterSort` / `RosterFilter` enumerators are unchanged throughout. `roster_sort_to_string` / `roster_sort_from_string` are defined in Task 3 and used in Task 5.

**One risk the executor must not smooth over.** Task 4 Step 7 changes existing call sites to compile. Those are placeholders only in the sense that Task 5 replaces them with real descriptor state — if Task 5 is skipped, the feature silently does nothing while every test still passes. Tasks 4 and 5 ship together.
