# Account Store Index Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace every directory walk in the account subsystem with an in-memory index built at boot and maintained on write, without changing anything on disk.

**Architecture:** A new `account_index` translation unit owns three key maps (email → record path, account name → email, character name → email) plus a quarantine set. The boot walker in `db.cpp` fills it; `write_account_file` maintains it; the three existing resolvers read from it behind the same `set_enabled()` seam `account_cache` already uses. `AccountData` reads still go to disk — the index answers *where*, never *what*.

**Tech Stack:** C++17, 32-bit (`-m32`), GTest via `src/CMakeLists.txt`, no new dependencies.

**Spec:** `docs/superpowers/specs/2026-09-05-account-store-index-design.md`

## Global Constraints

- **No on-disk change.** Same JSON, same filenames, same paths, same write ordering. If a task changes a byte on disk, it is wrong.
- **No public signature changes** in `account_management_*.h`. Bodies change; callers do not.
- **The 305 existing tests must pass unchanged** (`account_management_tests` 174, `interpre_account_menu_tests` 117, `account_cache_tests` 9, `roster_cache_tests` 5). A test needing an edit to go green is a behaviour change to justify with the user, not a test to fix.
- **Error strings must match the current text exactly** where a resolver already produces one. The existing tests assert on several. Copy them verbatim from the code being replaced.
- **The index holds keys only** — never a copy of `AccountData`. Memory is a first-class constraint on this box.
- **Format only files you changed:** `cd src && clang-format -i -style=WebKit <changed files>`. Never run the bare `make format` target.
- **Build with** `scripts/rots-docker.sh compile`; **test with** `scripts/rots-docker.sh test --gtest_filter='<Suite>.*'`.
- **Baseline the test suite before Task 1** and keep the output. The full run has pre-existing failures and has historically aborted partway; you need to know which are yours.
- New source files must be added to **both** `src/Makefile` (object list + rule) and `src/CMakeLists.txt` (`ROTS_SERVER_SOURCES`, and the test file to the test target).
- **`account_management_*.cpp` are fragments, not translation units.** `account_management.cpp:1705-1736` `#include`s `_internal.cpp`, `_identity.cpp`, `_storage.cpp`, `_assets.cpp`, `_migration.cpp` and `_presentation.cpp`; only `account_management.cpp` is compiled. Two consequences: **(a)** every `#include "account_index.h"` for any of them goes in `account_management.cpp`'s own include block, exactly once, never in a fragment; **(b)** TU-private helpers such as `character_asset_slug` and `character_json_file_name` (`account_management.cpp:80-88`) are visible to every fragment but **not** to `db.cpp`. This is also why the bare `make format` target breaks the build — it reorders those fragment includes.

---

### Task 0: Baseline

**Files:** none modified.

- [ ] **Step 1: Record the current test result**

```bash
cd /home/ahumbert/u/games/rots_wip/.worktrees/account-store
scripts/rots-docker.sh test --gtest_filter='Account*:Roster*:InterpreAccount*' 2>&1 | tail -40 > /tmp/account-baseline.txt
cat /tmp/account-baseline.txt
```

Expected: a pass/fail tally. Keep this file. Every later task compares against it.

- [ ] **Step 2: Confirm the server still boots on this branch**

```bash
scripts/rots-docker.sh compile
```

Expected: `bin/ageland` builds clean. Do not start Task 1 until it does.

---

### Task 1: Index core — keys, upsert, lookups

**Files:**
- Create: `src/account_index.h`
- Create: `src/account_index.cpp`
- Test: `src/tests/account_index_tests.cpp`
- Modify: `src/Makefile:29` (object list), `src/Makefile` (new compile rule near `account_cache.o` at line 50)
- Modify: `src/CMakeLists.txt:40` (after `account_cache.cpp`), `src/CMakeLists.txt:111` (after `tests/account_cache_tests.cpp`)

**Interfaces:**
- Consumes: `account::AccountData` (`account_management_types.h`), `account::normalize_email` / `account::normalize_account_name` (`account_management_identity.h`).
- Produces: everything in `account_index.h` below. Later tasks call `upsert`, `quarantine`, `find_path_by_email`, `find_path_by_account_name`, `find_owner_email_by_character`, `is_quarantined`, `set_enabled`, `is_enabled`, `clear`, `size`.

**Why upsert re-derives instead of diffing:** a write hands us the whole record. Erasing every key that record owns and reinserting from the record it just became is unconditionally correct; a diff between old and new character lists is the kind of thing that is right until someone renames a character. Do not "optimise" this into a diff.

- [ ] **Step 1: Write the failing test**

Create `src/tests/account_index_tests.cpp`:

```cpp
#include "account_index.h"
#include "account_management_types.h"

#include <gtest/gtest.h>

namespace {

account::AccountData make_account(const std::string& email, const std::string& name,
    const std::vector<std::string>& characters)
{
    account::AccountData account;
    account.normalized_email = email;
    account.account_name = name;
    account.characters = characters;
    return account;
}

class AccountIndexTest : public ::testing::Test {
protected:
    void SetUp() override { account_index::clear(); }
    void TearDown() override { account_index::clear(); }
};

TEST_F(AccountIndexTest, UpsertMakesRecordFindableByAllThreeKeys)
{
    const account::AccountData account = make_account("player@example.com", "player", { "Frodo", "Sam" });
    account_index::upsert(account, "accounts/P-T/player@example.com/account.json");

    std::string path;
    ASSERT_TRUE(account_index::find_path_by_email("player@example.com", &path, nullptr));
    EXPECT_EQ(path, "accounts/P-T/player@example.com/account.json");

    path.clear();
    ASSERT_TRUE(account_index::find_path_by_account_name("player", &path, nullptr));
    EXPECT_EQ(path, "accounts/P-T/player@example.com/account.json");

    std::string owner_email;
    ASSERT_TRUE(account_index::find_owner_email_by_character("Frodo", &owner_email, nullptr));
    EXPECT_EQ(owner_email, "player@example.com");
    ASSERT_TRUE(account_index::find_owner_email_by_character("Sam", &owner_email, nullptr));
    EXPECT_EQ(owner_email, "player@example.com");
}

TEST_F(AccountIndexTest, LookupsAreCaseInsensitiveViaNormalization)
{
    const account::AccountData account = make_account("player@example.com", "player", { "Frodo" });
    account_index::upsert(account, "accounts/P-T/player@example.com/account.json");

    std::string path;
    EXPECT_TRUE(account_index::find_path_by_email("PLAYER@Example.COM", &path, nullptr));
    EXPECT_TRUE(account_index::find_path_by_account_name("PlAyEr", &path, nullptr));

    std::string owner_email;
    EXPECT_TRUE(account_index::find_owner_email_by_character("FRODO", &owner_email, nullptr));
}

TEST_F(AccountIndexTest, UnknownKeysReturnFalse)
{
    std::string path;
    EXPECT_FALSE(account_index::find_path_by_email("nobody@example.com", &path, nullptr));
    EXPECT_FALSE(account_index::find_path_by_account_name("nobody", &path, nullptr));

    std::string owner_email;
    EXPECT_FALSE(account_index::find_owner_email_by_character("Nobody", &owner_email, nullptr));
}

TEST_F(AccountIndexTest, UpsertDropsKeysTheRecordNoLongerOwns)
{
    account_index::upsert(make_account("player@example.com", "player", { "Frodo", "Sam" }),
        "accounts/P-T/player@example.com/account.json");
    account_index::upsert(make_account("player@example.com", "player", { "Frodo" }),
        "accounts/P-T/player@example.com/account.json");

    std::string owner_email;
    EXPECT_TRUE(account_index::find_owner_email_by_character("Frodo", &owner_email, nullptr));
    EXPECT_FALSE(account_index::find_owner_email_by_character("Sam", &owner_email, nullptr))
        << "an unlinked character must stop resolving, or a save writes to the wrong account";
}

TEST_F(AccountIndexTest, UpsertFollowsAnAccountNameChange)
{
    account_index::upsert(make_account("player@example.com", "oldname", { "Frodo" }),
        "accounts/P-T/player@example.com/account.json");
    account_index::upsert(make_account("player@example.com", "newname", { "Frodo" }),
        "accounts/P-T/player@example.com/account.json");

    std::string path;
    EXPECT_TRUE(account_index::find_path_by_account_name("newname", &path, nullptr));
    EXPECT_FALSE(account_index::find_path_by_account_name("oldname", &path, nullptr));
}

TEST_F(AccountIndexTest, EnabledFlagDefaultsOffAndToggles)
{
    EXPECT_FALSE(account_index::is_enabled());
    account_index::set_enabled(true);
    EXPECT_TRUE(account_index::is_enabled());
    account_index::set_enabled(false);
    EXPECT_FALSE(account_index::is_enabled());
}

TEST_F(AccountIndexTest, SizeCountsRecordsNotKeys)
{
    account_index::upsert(make_account("a@example.com", "aaa", { "One", "Two", "Three" }),
        "accounts/A-E/a@example.com/account.json");
    account_index::upsert(make_account("b@example.com", "bbb", {}),
        "accounts/A-E/b@example.com/account.json");
    EXPECT_EQ(account_index::size(), 2u);
}

} // namespace
```

- [ ] **Step 2: Run the test to verify it fails**

```bash
cd /home/ahumbert/u/games/rots_wip/.worktrees/account-store
scripts/rots-docker.sh test --gtest_filter='AccountIndexTest.*'
```

Expected: compile failure — `account_index.h` does not exist.

- [ ] **Step 3: Write the header**

Create `src/account_index.h`:

```cpp
#ifndef ACCOUNT_INDEX_H
#define ACCOUNT_INDEX_H

#include "account_management_types.h"

#include <cstddef>
#include <string>
#include <vector>

// In-memory index over the account records on disk. Holds KEYS ONLY: it answers "which file holds
// the account for this email / account name / character name", never what is inside that file.
// Reads of the record itself still go to disk, which is what keeps this bounded in memory and keeps
// the file the single source of truth.
namespace account_index {

// One indexed record. A quarantined entry still occupies its email so that a record we could not
// parse cannot be silently overwritten by a fresh account created at the same address.
struct Entry {
    std::string normalized_email;
    std::string record_path;
    std::string normalized_account_name;
    bool quarantined = false;
    std::string quarantine_reason;
};

// Re-derives every key this record owns from the record itself, dropping any key it owned before.
// Correct across link, unlink, rename and account-name change without diffing old against new.
void upsert(const account::AccountData& account, const std::string& record_path);

// Records an account file we could not use, keyed by its directory name. Its email stays occupied.
void quarantine(const std::string& normalized_email, const std::string& record_path,
    const std::string& reason);

// Lookups. Each returns false and sets *error_message (when non-null) if the key is unknown or the
// record behind it is quarantined.
bool find_path_by_email(const std::string& email, std::string* record_path,
    std::string* error_message);
bool find_path_by_account_name(const std::string& account_name, std::string* record_path,
    std::string* error_message);
bool find_owner_email_by_character(const std::string& character_name, std::string* owner_email,
    std::string* error_message);

bool is_quarantined(const std::string& email);
std::vector<Entry> quarantined_entries();

// Number of indexed records (not keys).
std::size_t size();

void clear();

// Whether the account resolvers consult this index. Default OFF so the test binary and any
// non-server caller keep the exact directory-scanning behaviour; the live server turns it on at
// boot once the index has been built.
void set_enabled(bool enabled);
bool is_enabled();

} // namespace account_index

#endif
```

- [ ] **Step 4: Write the implementation**

Create `src/account_index.cpp`:

```cpp
#include "account_index.h"

#include "account_management_identity.h"

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace account_index {

namespace {

// email -> entry. The email is the storage key on disk, so it is the identity here too.
std::unordered_map<std::string, Entry> g_entries;
// normalized account name -> email.
std::unordered_map<std::string, std::string> g_account_names;
// normalized character name -> email.
std::unordered_map<std::string, std::string> g_characters;
// email -> the character keys that email currently owns, so upsert can drop what it no longer owns.
std::unordered_map<std::string, std::vector<std::string>> g_owned_characters;

bool g_enabled = false;

void set_error(std::string* error_message, const std::string& text)
{
    if (error_message != nullptr)
        *error_message = text;
}

// Drops every key owned by this email except the entry itself.
void erase_owned_keys(const std::string& email)
{
    for (auto name_entry = g_account_names.begin(); name_entry != g_account_names.end();) {
        if (name_entry->second == email)
            name_entry = g_account_names.erase(name_entry);
        else
            ++name_entry;
    }

    const auto owned = g_owned_characters.find(email);
    if (owned != g_owned_characters.end()) {
        for (const std::string& character_key : owned->second) {
            const auto character_entry = g_characters.find(character_key);
            if (character_entry != g_characters.end() && character_entry->second == email)
                g_characters.erase(character_entry);
        }
        g_owned_characters.erase(owned);
    }
}

} // namespace

void upsert(const account::AccountData& account, const std::string& record_path)
{
    const std::string email = account::normalize_email(account.normalized_email);
    if (email.empty())
        return;

    erase_owned_keys(email);

    Entry entry;
    entry.normalized_email = email;
    entry.record_path = record_path;
    entry.normalized_account_name = account::normalize_account_name(account.account_name);
    g_entries[email] = entry;

    if (!entry.normalized_account_name.empty())
        g_account_names[entry.normalized_account_name] = email;

    std::vector<std::string> owned;
    owned.reserve(account.characters.size());
    for (const std::string& character_name : account.characters) {
        const std::string character_key = account::normalize_account_name(character_name);
        if (character_key.empty())
            continue;
        g_characters[character_key] = email;
        owned.push_back(character_key);
    }
    g_owned_characters[email] = std::move(owned);
}

void quarantine(const std::string& normalized_email, const std::string& record_path,
    const std::string& reason)
{
    const std::string email = account::normalize_email(normalized_email);
    if (email.empty())
        return;

    erase_owned_keys(email);

    Entry entry;
    entry.normalized_email = email;
    entry.record_path = record_path;
    entry.quarantined = true;
    entry.quarantine_reason = reason;
    g_entries[email] = entry;
}

bool find_path_by_email(const std::string& email, std::string* record_path,
    std::string* error_message)
{
    const auto entry = g_entries.find(account::normalize_email(email));
    if (entry == g_entries.end()) {
        set_error(error_message, "No account exists for that email address.");
        return false;
    }
    if (entry->second.quarantined) {
        set_error(error_message, "That account record could not be read.");
        return false;
    }
    if (record_path != nullptr)
        *record_path = entry->second.record_path;
    set_error(error_message, "");
    return true;
}

bool find_path_by_account_name(const std::string& account_name, std::string* record_path,
    std::string* error_message)
{
    const auto name_entry = g_account_names.find(account::normalize_account_name(account_name));
    if (name_entry == g_account_names.end()) {
        set_error(error_message, "No account exists with that name.");
        return false;
    }
    return find_path_by_email(name_entry->second, record_path, error_message);
}

bool find_owner_email_by_character(const std::string& character_name, std::string* owner_email,
    std::string* error_message)
{
    const auto character_entry = g_characters.find(account::normalize_account_name(character_name));
    if (character_entry == g_characters.end()) {
        set_error(error_message, "");
        return false;
    }

    const auto entry = g_entries.find(character_entry->second);
    if (entry == g_entries.end() || entry->second.quarantined) {
        set_error(error_message, "That account record could not be read.");
        return false;
    }

    if (owner_email != nullptr)
        *owner_email = character_entry->second;
    set_error(error_message, "");
    return true;
}

bool is_quarantined(const std::string& email)
{
    const auto entry = g_entries.find(account::normalize_email(email));
    return entry != g_entries.end() && entry->second.quarantined;
}

std::vector<Entry> quarantined_entries()
{
    std::vector<Entry> quarantined;
    for (const auto& entry : g_entries) {
        if (entry.second.quarantined)
            quarantined.push_back(entry.second);
    }
    return quarantined;
}

std::size_t size()
{
    return g_entries.size();
}

void clear()
{
    g_entries.clear();
    g_account_names.clear();
    g_characters.clear();
    g_owned_characters.clear();
}

void set_enabled(bool enabled)
{
    g_enabled = enabled;
}

bool is_enabled()
{
    return g_enabled;
}

} // namespace account_index
```

- [ ] **Step 5: Wire the build system**

In `src/Makefile:29`, add `account_index.o` immediately after `account_cache.o` in the object list. Then add a rule beside the `account_cache.o` rule (line 50):

```make
account_index.o : account_index.cpp account_index.h account_management_identity.h account_management_types.h
	$(CC) -c $(CFLAGS) account_index.cpp
```

In `src/CMakeLists.txt`, add `account_index.cpp` after `account_cache.cpp` (line 40) in `ROTS_SERVER_SOURCES`, and `tests/account_index_tests.cpp` after `tests/account_cache_tests.cpp` (line 111).

- [ ] **Step 6: Run the test to verify it passes**

```bash
scripts/rots-docker.sh test --gtest_filter='AccountIndexTest.*'
```

Expected: 7 tests, all PASS.

- [ ] **Step 7: Confirm nothing else moved**

```bash
scripts/rots-docker.sh test --gtest_filter='Account*:Roster*:InterpreAccount*' 2>&1 | tail -40
```

Expected: identical tally to `/tmp/account-baseline.txt` apart from the 7 new passes.

- [ ] **Step 8: Format and commit**

```bash
cd src && clang-format -i -style=WebKit account_index.cpp account_index.h tests/account_index_tests.cpp && cd ..
git add src/account_index.cpp src/account_index.h src/tests/account_index_tests.cpp src/Makefile src/CMakeLists.txt
git commit -m "feat(account): add a keys-only account index"
```

---

### Task 2: Quarantine policy and the boot threshold

**Files:**
- Modify: `src/account_index.h` (add the threshold constant and the counter)
- Modify: `src/account_index.cpp`
- Test: `src/tests/account_index_tests.cpp`

**Interfaces:**
- Consumes: `account_index::quarantine`, `account_index::is_quarantined` from Task 1.
- Produces: `account_index::MAX_QUARANTINED_RECORDS_AT_BOOT` (5), `account_index::quarantined_count()`. Task 3 reads both to decide whether to abort the boot.

- [ ] **Step 1: Write the failing test**

Append to `src/tests/account_index_tests.cpp`, inside the same anonymous namespace:

```cpp
TEST_F(AccountIndexTest, QuarantinedEmailStaysOccupiedSoItCannotBeOverwritten)
{
    account_index::quarantine("broken@example.com", "accounts/A-E/broken@example.com/account.json",
        "unparseable JSON");

    EXPECT_TRUE(account_index::is_quarantined("broken@example.com"));

    std::string path;
    std::string error_message;
    EXPECT_FALSE(account_index::find_path_by_email("broken@example.com", &path, &error_message));
    EXPECT_EQ(error_message, "That account record could not be read.")
        << "must NOT read as 'no account exists', or creation would write over a real record";
}

TEST_F(AccountIndexTest, QuarantineDropsTheKeysTheRecordHeldBefore)
{
    account_index::upsert(make_account("player@example.com", "player", { "Frodo" }),
        "accounts/P-T/player@example.com/account.json");
    account_index::quarantine("player@example.com", "accounts/P-T/player@example.com/account.json",
        "mismatched email");

    std::string owner_email;
    EXPECT_FALSE(account_index::find_owner_email_by_character("Frodo", &owner_email, nullptr));

    std::string path;
    EXPECT_FALSE(account_index::find_path_by_account_name("player", &path, nullptr));
}

TEST_F(AccountIndexTest, QuarantinedCountTracksOnlyBadRecords)
{
    account_index::upsert(make_account("good@example.com", "good", {}),
        "accounts/F-J/good@example.com/account.json");
    account_index::quarantine("bad1@example.com", "accounts/A-E/bad1@example.com/account.json", "x");
    account_index::quarantine("bad2@example.com", "accounts/A-E/bad2@example.com/account.json", "y");

    EXPECT_EQ(account_index::quarantined_count(), 2u);
    EXPECT_EQ(account_index::size(), 3u);
}

TEST_F(AccountIndexTest, QuarantineThresholdIsFive)
{
    EXPECT_EQ(account_index::MAX_QUARANTINED_RECORDS_AT_BOOT, 5u);
}

TEST_F(AccountIndexTest, QuarantinedEntriesReportPathAndReason)
{
    account_index::quarantine("bad@example.com", "accounts/A-E/bad@example.com/account.json",
        "unparseable JSON");

    const std::vector<account_index::Entry> entries = account_index::quarantined_entries();
    ASSERT_EQ(entries.size(), 1u);
    EXPECT_EQ(entries[0].normalized_email, "bad@example.com");
    EXPECT_EQ(entries[0].record_path, "accounts/A-E/bad@example.com/account.json");
    EXPECT_EQ(entries[0].quarantine_reason, "unparseable JSON");
}
```

- [ ] **Step 2: Run the test to verify it fails**

```bash
scripts/rots-docker.sh test --gtest_filter='AccountIndexTest.Quarantine*'
```

Expected: compile failure — `MAX_QUARANTINED_RECORDS_AT_BOOT` and `quarantined_count` are undeclared.

- [ ] **Step 3: Add the constant and counter to the header**

In `src/account_index.h`, inside `namespace account_index`, above `struct Entry`:

```cpp
// Boot refuses to continue past this many unusable account records. The threshold is a bug
// detector, not a corruption tolerance: the write path cannot produce a torn file, so the realistic
// causes of an unreadable record are ours (a serialization change, a normalize_email change, a
// normalize_email change) and they hit many records at once. One is a
// genuine one-off and must not take the game down; six means we shipped something.
static constexpr std::size_t MAX_QUARANTINED_RECORDS_AT_BOOT = 5;
```

And beside `quarantined_entries()`:

```cpp
std::size_t quarantined_count();
```

- [ ] **Step 4: Implement the counter**

In `src/account_index.cpp`, beside `size()`:

```cpp
std::size_t quarantined_count()
{
    std::size_t count = 0;
    for (const auto& entry : g_entries) {
        if (entry.second.quarantined)
            ++count;
    }
    return count;
}
```

- [ ] **Step 5: Run the tests to verify they pass**

```bash
scripts/rots-docker.sh test --gtest_filter='AccountIndexTest.*'
```

Expected: 12 tests, all PASS.

- [ ] **Step 6: Format and commit**

```bash
cd src && clang-format -i -style=WebKit account_index.cpp account_index.h tests/account_index_tests.cpp && cd ..
git add src/account_index.cpp src/account_index.h src/tests/account_index_tests.cpp
git commit -m "feat(account): quarantine unusable account records with a boot threshold"
```

---

### Task 3: Build the index at boot, and stop dying on one bad record

**Files:**
- Modify: `src/db.cpp:629-710` (`build_account_native_player_index`)
- Modify: `src/db.cpp:312-313` (enable the index alongside the caches)
- Test: `src/tests/account_index_tests.cpp` (the decision helper only — the walker itself is exercised live, see Step 6)

**Interfaces:**
- Consumes: `account_index::upsert`, `account_index::quarantine`, `account_index::quarantined_count`, `account_index::MAX_QUARANTINED_RECORDS_AT_BOOT`.
- Produces: a populated index after `boot_db`. Nothing reads it yet — Task 5 flips the resolvers. This ordering is deliberate: the index gets built and observed in the live log for one task before anything depends on it.

**Two things happen here.** The walker keeps what it reads instead of discarding it, and the four `exit(1)` sites become quarantine calls. There is also a quadratic to remove: the current loop calls `account_character_player_path(".", account_data.account_name, character_name)`, which goes through `resolve_account_storage_key` → `read_account_file` → a full tree walk, once per linked character. The record is already in hand at that point, so the path is derivable directly.

**Why `readdir` is not unit-tested here:** per the note in `account_cache.h`, the on-disk account-directory scan does not resolve under QEMU i386 emulation. That is why Tasks 1 and 2 drive the index through `upsert`/`quarantine` with records in memory — the walker calling those functions *is* the seam. The walker itself is verified live in Step 6.

- [ ] **Step 1: Write the failing test for the boot decision**

Append to `src/tests/account_index_tests.cpp`:

```cpp
TEST_F(AccountIndexTest, BootToleratesFailuresUpToTheThreshold)
{
    for (std::size_t i = 0; i < account_index::MAX_QUARANTINED_RECORDS_AT_BOOT; ++i) {
        const std::string email = "bad" + std::to_string(i) + "@example.com";
        account_index::quarantine(email, "accounts/A-E/" + email + "/account.json", "unparseable");
    }

    EXPECT_EQ(account_index::quarantined_count(), account_index::MAX_QUARANTINED_RECORDS_AT_BOOT);
    EXPECT_FALSE(account_index::quarantined_count() > account_index::MAX_QUARANTINED_RECORDS_AT_BOOT)
        << "exactly at the threshold must still boot";
}

TEST_F(AccountIndexTest, BootRefusesPastTheThreshold)
{
    for (std::size_t i = 0; i <= account_index::MAX_QUARANTINED_RECORDS_AT_BOOT; ++i) {
        const std::string email = "bad" + std::to_string(i) + "@example.com";
        account_index::quarantine(email, "accounts/A-E/" + email + "/account.json", "unparseable");
    }

    EXPECT_TRUE(account_index::quarantined_count() > account_index::MAX_QUARANTINED_RECORDS_AT_BOOT);
}
```

Add `#include <string>` to the test file's includes if it is not already there.

- [ ] **Step 2: Run to verify it passes** (these assert existing behaviour from Task 2; they exist so the threshold cannot be changed silently)

```bash
scripts/rots-docker.sh test --gtest_filter='AccountIndexTest.Boot*'
```

Expected: 2 tests PASS.

- [ ] **Step 3: Rewrite the boot walker**

In `src/db.cpp`, add `#include "account_index.h"` beside the existing `#include "account_cache.h"`.

Replace the body of `build_account_native_player_index` (db.cpp:629) so that each of the four current `exit(1)` sites instead calls `account_index::quarantine(...)` with the reason and `continue`s to the next record, and each successfully read record calls `account_index::upsert(account_data, account_json_path)`. The account-JSON failure and the email-mismatch failure quarantine the whole record and skip its characters; a character file that cannot be inspected or read quarantines the record it belongs to and stops processing that record's characters.

Replace the per-character path derivation. Current:

```cpp
const std::string character_path = account::account_character_player_path(".", account_data.account_name, character_name);
```

New — the record is in hand, so no lookup is needed:

```cpp
// The record we just parsed IS the account, so its directory is already known. Going through
// account_character_player_path here would call resolve_account_storage_key -> read_account_file,
// a full walk of the account tree, once per linked character.
//
// The name mirrors character_json_file_name (account_management.cpp:85), which is
// slug + ".character.json" where the slug is normalize_account_name. That helper is private to the
// account_management translation unit and is not visible here, so the two pieces are composed
// directly. If either ever changes, this line changes with it.
const std::string character_path = bucket_path + "/" + account_entry->d_name + "/"
    + account::normalize_account_name(character_name) + ".character.json";
```

Verify before moving on: `account_character_player_path` (`account_management_storage.cpp:318`) is
`account_character_directory(...) + "/" + character_json_file_name(character_name)`, and
`character_json_file_name` (`account_management.cpp:85`) is `character_asset_slug(name) + ".character.json"`,
with `character_asset_slug` (`account_management.cpp:80`) being `normalize_account_name`. The line
above must produce a byte-identical path, or boot looks in the wrong place. `normalize_account_name`
is declared in `account_management_identity.h`, which `db.cpp` can include.

At the end of the function, after `closedir(accounts_root)`:

```cpp
const std::size_t quarantined = account_index::quarantined_count();
if (quarantined > 0) {
    sprintf(buf, "Account index: %lu record(s) quarantined; use the account index wizard command to list them.",
        static_cast<unsigned long>(quarantined));
    log(buf);
    mudlog(buf, BRF, LEVEL_IMMORT, TRUE);
}

if (quarantined > account_index::MAX_QUARANTINED_RECORDS_AT_BOOT) {
    sprintf(buf, "Account index: %lu unusable account records exceeds the limit of %lu. Refusing to boot.",
        static_cast<unsigned long>(quarantined),
        static_cast<unsigned long>(account_index::MAX_QUARANTINED_RECORDS_AT_BOOT));
    log(buf);
    exit(1);
}

sprintf(buf, "Account index: %lu account(s) indexed.", static_cast<unsigned long>(account_index::size()));
log(buf);
```

- [ ] **Step 4: Enable the index at boot**

In `src/db.cpp`, beside line 312:

```cpp
    account_cache::set_enabled(true);
    roster_cache::set_enabled(true);
    account_index::set_enabled(true);
```

Confirm this runs *before* `build_player_index()` (db.cpp:971). If it does not, move the `account_index::set_enabled(true)` call so that it does.

This is the default-on half of the rollout. There is no runtime off switch and no going back to the scan-based system; the index is how accounts resolve from here on.

- [ ] **Step 5: Compile**

```bash
scripts/rots-docker.sh compile
```

Expected: clean build.

- [ ] **Step 6: Verify live — the index builds and the count is right**

```bash
find lib/accounts -name account.json | wc -l   # expect 14 on the dev fixtures
scripts/rots-docker.sh boot
grep 'Account index' log/syslog | tail -5
```

Expected: `Account index: 14 account(s) indexed.` and no quarantine line. Then shut the server down.

- [ ] **Step 7: Verify live — one bad record no longer stops the boot**

```bash
cp lib/accounts/*/*/account.json /tmp/account-backup.json   # pick one; note which
# corrupt a single record
echo 'not json' > lib/accounts/<bucket>/<email>/account.json
scripts/rots-docker.sh boot
grep 'Account index' log/syslog | tail -5
```

Expected: the server **boots**, logs `1 record(s) quarantined`, and indexes 13. Before this task it would have exited. Restore the record afterwards:

```bash
cp /tmp/account-backup.json lib/accounts/<bucket>/<email>/account.json
```

- [ ] **Step 8: Run the full account suite**

```bash
scripts/rots-docker.sh test --gtest_filter='Account*:Roster*:InterpreAccount*' 2>&1 | tail -40
```

Expected: matches `/tmp/account-baseline.txt` plus the new index tests.

- [ ] **Step 9: Format and commit**

```bash
cd src && clang-format -i -style=WebKit db.cpp && cd ..
git add src/db.cpp
git commit -m "feat(account): build the account index at boot instead of dying on one bad record"
```

Note: `clang-format` on `db.cpp` will reformat the whole file. If the diff is larger than your change, revert the formatting and commit `db.cpp` unformatted — the project explicitly tolerates this (see CLAUDE.md on `make format`).

---

### Task 4: Maintain the index on write

**Files:**
- Modify: `src/account_management_storage.cpp:170` (`write_account_file`, at the `invalidate_all()` line near the end)
- Test: `src/tests/account_management_tests.cpp`

**Interfaces:**
- Consumes: `account_index::upsert` (Task 1), `account::account_file_path_from_email` (already used by `write_account_file` to choose `final_path`).
- Produces: an index that tracks every successful write. Task 5 depends on this being in place first.

**The one place drift can enter.** `write_account_file` already ends with `invalidate_all()` on success, after the rename, only when every step worked. The index update goes on the same line, under the same condition. Do not add an index update anywhere else in this function — an update before the rename would index a record that may not land.

- [ ] **Step 1: Write the failing test**

Append to `src/tests/account_management_tests.cpp` (match the file's existing fixture style; it already has a fixture that writes accounts under a temp root):

```cpp
TEST_F(AccountManagementTest, WriteAccountFileIndexesTheRecord)
{
    account_index::clear();

    account::AccountData account;
    ASSERT_TRUE(account::initialize_new_account("indexed", "indexed@example.com", "password123",
        1000, &account, nullptr));
    ASSERT_TRUE(account::write_account_file(test_root(), account, nullptr));

    std::string owner_email;
    std::string path;
    EXPECT_TRUE(account_index::find_path_by_email("indexed@example.com", &path, nullptr));
    EXPECT_TRUE(account_index::find_path_by_account_name("indexed", &path, nullptr));

    account_index::clear();
}

TEST_F(AccountManagementTest, WriteAccountFileIndexesNewlyLinkedCharacters)
{
    account_index::clear();

    account::AccountData account;
    ASSERT_TRUE(account::initialize_new_account("linker", "linker@example.com", "password123",
        1000, &account, nullptr));
    ASSERT_TRUE(account::add_character_to_account(&account, "Frodo", nullptr));
    ASSERT_TRUE(account::write_account_file(test_root(), account, nullptr));

    std::string owner_email;
    ASSERT_TRUE(account_index::find_owner_email_by_character("Frodo", &owner_email, nullptr));
    EXPECT_EQ(owner_email, "linker@example.com");

    account_index::clear();
}

TEST_F(AccountManagementTest, AFailedWriteLeavesTheIndexAlone)
{
    account_index::clear();

    account::AccountData account;
    ASSERT_TRUE(account::initialize_new_account("failer", "failer@example.com", "password123",
        1000, &account, nullptr));
    account.account_name = "";   // rejected by validate_identifier_for_path before any file work

    EXPECT_FALSE(account::write_account_file(test_root(), account, nullptr));

    std::string path;
    EXPECT_FALSE(account_index::find_path_by_email("failer@example.com", &path, nullptr))
        << "a write that never landed must never appear in the index";

    account_index::clear();
}
```

Add `#include "account_index.h"` to the test file's includes. If the fixture's temp-root accessor is not called `test_root()`, use whatever the surrounding tests in that file use — read them first.

- [ ] **Step 2: Run to verify it fails**

```bash
scripts/rots-docker.sh test --gtest_filter='AccountManagementTest.*Index*:AccountManagementTest.AFailedWrite*'
```

Expected: FAIL — the index is empty after a successful write.

- [ ] **Step 3: Add the index update**

In `src/account_management_storage.cpp`, at the end of `write_account_file`, beside the existing cache flush:

```cpp
    // Single account.json write chokepoint: flush the cache so subsequent reads see the new state.
    if (account_cache::is_enabled())
        account_cache::invalidate_all();

    // Same chokepoint, same condition: after the rename, only on a fully successful write. The
    // index re-derives every key from the record just written, so link, unlink, rename and an
    // account-name change are all handled without diffing against what was there before.
    account_index::upsert(normalized_account, final_path);
```

Add `#include "account_index.h"` to **`account_management.cpp`'s** include block (see Global Constraints — `_storage.cpp` is a fragment of it, so an include there is wrong).

**This also covers deletion.** `admin_delete_linked_character` (`account_management_identity.cpp:1102`)
removes the character's files and then writes the account record through this same function, so an
unlinked character's key is dropped by the re-derive in `upsert`. There is no code path that removes
a whole account record, so there is nothing else to un-index. The `AccountIndexTest.UpsertDropsKeysTheRecordNoLongerOwns`
test from Task 1 is what pins this.

Note `normalized_account`, not `account` — the normalized copy is what was serialized to disk, and indexing anything else would key the record by a value the file does not contain.

- [ ] **Step 4: Run to verify it passes**

```bash
scripts/rots-docker.sh test --gtest_filter='AccountManagementTest.*Index*:AccountManagementTest.AFailedWrite*'
```

Expected: 3 tests PASS.

- [ ] **Step 5: Run the full account suite**

```bash
scripts/rots-docker.sh test --gtest_filter='Account*:Roster*:InterpreAccount*' 2>&1 | tail -40
```

Expected: matches the baseline plus the new tests. The index is unconditional here (not gated on `is_enabled`), so existing tests now populate it; if any existing test fails, it is because it depends on cross-test index state — clear the index in that fixture's `SetUp`, do not change the assertion.

- [ ] **Step 6: Format and commit**

```bash
cd src && clang-format -i -style=WebKit account_management_storage.cpp tests/account_management_tests.cpp && cd ..
git add src/account_management_storage.cpp src/tests/account_management_tests.cpp
git commit -m "feat(account): maintain the index at the account write chokepoint"
```

---

### Task 5: Resolve through the index

**Files:**
- Modify: `src/account_management.cpp:800` (`find_account_file_path_by_account_name`)
- Modify: `src/account_management.cpp:966` (`find_account_by_email_internal`)
- Modify: `src/account_management.cpp:744` (`resolve_account_storage_key`)
- Modify: `src/account_management_identity.cpp:903` (`find_linked_character_owner_account_uncached`)
- Test: `src/tests/account_index_tests.cpp`

**Interfaces:**
- Consumes: `account_index::find_path_by_email`, `find_path_by_account_name`, `find_owner_email_by_character`, `is_enabled` (Task 1); a populated index (Tasks 3 and 4).
- Produces: no new symbols. This is the task that makes the scans stop running.

**This is the payoff and the risk.** Each function gets an index fast path guarded by `account_index::is_enabled()`, falling through to the existing scan when the index is not authoritative for the caller's root. Do not delete the scans — the test binary never calls `boot_db`, and callers working against another tree still need them. They are not a way back to the old system.

**Error strings are load-bearing.** Before editing each function, read the exact text it sets on failure and make the index path produce the same string. Several existing tests assert on this text.

- [ ] **Step 1: Write the failing test**

Append to `src/tests/account_index_tests.cpp`:

```cpp
TEST_F(AccountIndexTest, DisabledIndexIsNotConsulted)
{
    account_index::set_enabled(false);
    account_index::upsert(make_account("player@example.com", "player", { "Frodo" }),
        "accounts/P-T/player@example.com/account.json");

    // The index still answers its own API when queried directly; is_enabled() only governs whether
    // the account resolvers consult it. This test pins that distinction so the flag is not
    // mistakenly wired into the lookups themselves.
    std::string path;
    EXPECT_TRUE(account_index::find_path_by_email("player@example.com", &path, nullptr));
    EXPECT_FALSE(account_index::is_enabled());
}

TEST_F(AccountIndexTest, UnknownEmailErrorMatchesTheScanText)
{
    std::string path;
    std::string error_message;
    EXPECT_FALSE(account_index::find_path_by_email("nobody@example.com", &path, &error_message));
    EXPECT_EQ(error_message, "No account exists for that email address.")
        << "must match find_account_by_email_internal's text verbatim; tests assert on it";
}
```

- [ ] **Step 2: Run to verify it passes** (these pin Task 1 behaviour before the risky edit)

```bash
scripts/rots-docker.sh test --gtest_filter='AccountIndexTest.DisabledIndex*:AccountIndexTest.UnknownEmailError*'
```

Expected: 2 tests PASS. If the error-text test fails, fix `account_index.cpp` to match the scan's exact string — not the other way round.

- [ ] **Step 3: Route the account-name lookup through the index**

In `src/account_management.cpp:800`, at the top of `find_account_file_path_by_account_name`, after the existing argument validation and before `opendir`:

```cpp
        if (account_index::is_enabled()) {
            std::string indexed_path;
            if (account_index::find_path_by_account_name(account_name, &indexed_path, error_message)) {
                if (account_path != nullptr)
                    *account_path = indexed_path;
                return true;
            }
            return false;
        }
```

The `#include "account_index.h"` added in Task 4 already covers this file. Do not add it again.

Read the existing failure path first: whatever string it sets when no account matches must be the string the index path sets. Adjust `account_index::find_path_by_account_name`'s text to match if they differ.

- [ ] **Step 4: Route the email lookup through the index**

In `src/account_management.cpp:966`, at the top of `find_account_by_email_internal`, before `opendir`:

```cpp
        if (account_index::is_enabled()) {
            std::string indexed_path;
            if (!account_index::find_path_by_email(email, &indexed_path, error_message))
                return false;
            return read_account_file_from_path(indexed_path, account, error_message);
        }
```

This is the change that removes the per-login full scan.

- [ ] **Step 5: Route path derivation through the index**

In `src/account_management.cpp:744`, `resolve_account_storage_key` currently calls `read_account_file` purely to learn a record's email. With the index that is a map hit:

```cpp
        if (account_index::is_enabled()) {
            std::string indexed_path;
            if (!account_index::find_path_by_account_name(account_identifier, &indexed_path, nullptr))
                return "";
            // The email IS the directory name, so derive it from the path rather than reading the file.
            const std::size_t slash = indexed_path.find_last_of('/');
            if (slash == std::string::npos || slash == 0)
                return "";
            const std::string directory = indexed_path.substr(0, slash);
            const std::size_t parent_slash = directory.find_last_of('/');
            return (parent_slash == std::string::npos) ? directory : directory.substr(parent_slash + 1);
        }
```

This is what stops `account_character_directory` from walking the tree to return a string.

- [ ] **Step 6: Route the character-owner lookup through the index**

In `src/account_management_identity.cpp:903`, at the top of `find_linked_character_owner_account_uncached`:

```cpp
    if (account_index::is_enabled()) {
        std::string owner_email;
        if (!account_index::find_owner_email_by_character(character_name, &owner_email, error_message)) {
            // Not linked is a valid, non-error outcome for this resolver — mirror what the scan
            // returns when it walks the whole tree and finds nothing.
            if (owner_account_name != nullptr)
                owner_account_name->clear();
            return true;
        }

        AccountData owner_account;
        if (!read_account_file_by_email(root_directory, owner_email, &owner_account, error_message))
            return false;
        if (owner_account_name != nullptr)
            *owner_account_name = owner_account.account_name;
        return true;
    }
```

No new include is needed — `_identity.cpp` is a fragment of `account_management.cpp`, which Task 4 already updated.

**Read the existing scan's return convention before writing this.** It distinguishes "resolved, not linked" (returns true with an empty owner) from a genuine error (returns false). The index path must reproduce that exactly — `account_cache` memoizes both outcomes and the negative case is the common one. Getting this backwards makes every unlinked character look like an error on the save path.

- [ ] **Step 7: Compile**

```bash
scripts/rots-docker.sh compile
```

- [ ] **Step 8: Run the full account suite — this is the gate**

```bash
scripts/rots-docker.sh test --gtest_filter='Account*:Roster*:InterpreAccount*' 2>&1 | tail -40
```

Expected: identical to the baseline plus new tests. **The index is off in the test binary** (`set_enabled` defaults false and only `boot_db` turns it on), so this run proves the fallback path is untouched. That is half the gate; Step 9 is the other half.

- [ ] **Step 9: Run the same suite with the index forced on**

Add a temporary `main`-level override, or run the subset of tests that call `account_index::set_enabled(true)` in their fixture. Simplest: add to `src/tests/account_index_tests.cpp` a fixture that enables the index, writes two accounts through `write_account_file`, then exercises `read_account_file_by_email`, `read_account_file`, and `find_linked_character_owner_account`, asserting the same results as the uncached calls:

```cpp
TEST_F(AccountIndexTest, ResolversAgreeWithTheScanWhenTheIndexIsOn)
{
    account_index::set_enabled(false);
    account::AccountData account;
    ASSERT_TRUE(account::initialize_new_account("agree", "agree@example.com", "password123",
        1000, &account, nullptr));
    ASSERT_TRUE(account::add_character_to_account(&account, "Frodo", nullptr));
    ASSERT_TRUE(account::write_account_file(".", account, nullptr));

    std::string scanned_owner;
    const bool scanned_ok = account::find_linked_character_owner_account_uncached(".", "Frodo",
        &scanned_owner, nullptr);

    account_index::set_enabled(true);
    std::string indexed_owner;
    const bool indexed_ok = account::find_linked_character_owner_account_uncached(".", "Frodo",
        &indexed_owner, nullptr);
    account_index::set_enabled(false);

    EXPECT_EQ(scanned_ok, indexed_ok);
    EXPECT_EQ(scanned_owner, indexed_owner);
}
```

If the on-disk write in this test cannot run under QEMU (see the `account_cache.h` note), drop this test and rely on Step 10 instead — say so in the commit message rather than leaving a disabled test behind.

- [ ] **Step 10: Verify live — this is the real gate**

```bash
scripts/rots-docker.sh boot
```

Then connect (`telnet localhost 1024`, no `-x`) and, using the Debugbot account from memory:

1. Log in by email. Expect no delay and a normal menu.
2. Enter a wrong password, then the right one.
3. Create a second account at a fresh address; confirm it appears under `lib/accounts/`.
4. Select a character, play briefly, `quit` **all the way out** — a character left in game makes later selections reconnect to it and fakes the result.
5. Link a character, check the roster shows it, delete it, check the roster no longer does.
6. Log out and back in; confirm the roster matches what step 5 left.

Watch `log/syslog` throughout for `Account index` lines and for any account error text.

- [ ] **Step 11: Format and commit**

```bash
cd src && clang-format -i -style=WebKit account_management.cpp account_management_identity.cpp tests/account_index_tests.cpp && cd ..
git add src/account_management.cpp src/account_management_identity.cpp src/tests/account_index_tests.cpp
git commit -m "feat(account): resolve accounts through the index instead of scanning"
```

---

### Task 6: Refuse to create an account over a quarantined record

**Files:**
- Modify: `src/account_management_identity.cpp:505` (`create_account_for_email`)
- Test: `src/tests/account_index_tests.cpp`

**Interfaces:**
- Consumes: `account_index::is_quarantined` (Task 2).
- Produces: no new symbols.

**Why this is its own task:** it is the one place where quarantine could turn a recoverable parse error into data loss. `create_account_for_email` decides an address is free when the lookup fails. A quarantined record's lookup fails. Without this check, the next person to type that address gets a brand-new account written over a real player's record.

- [ ] **Step 1: Write the failing test**

Append to `src/tests/account_index_tests.cpp`:

```cpp
TEST_F(AccountIndexTest, QuarantinedAddressIsNotFreeForCreation)
{
    account_index::set_enabled(true);
    account_index::quarantine("occupied@example.com",
        "accounts/K-O/occupied@example.com/account.json", "unparseable JSON");

    EXPECT_TRUE(account_index::is_quarantined("occupied@example.com"));

    account::AccountData created;
    std::string error_message;
    const bool ok = account::create_account_for_email(".", "occupied@example.com", "password123",
        1000, &created, &error_message);
    account_index::set_enabled(false);

    EXPECT_FALSE(ok) << "creating here would overwrite a real player's record";
    EXPECT_FALSE(error_message.empty());
}
```

- [ ] **Step 2: Run to verify it fails**

```bash
scripts/rots-docker.sh test --gtest_filter='AccountIndexTest.QuarantinedAddressIsNotFree*'
```

Expected: FAIL — creation succeeds.

- [ ] **Step 3: Add the guard**

In `src/account_management_identity.cpp`, at the top of `create_account_for_email` (line 505), after the email is validated and normalized and before any existence check:

```cpp
    if (account_index::is_enabled() && account_index::is_quarantined(email)) {
        set_error(error_message, "That email address cannot be used right now.");
        return false;
    }
```

No new include is needed — Task 4 added it to `account_management.cpp`, and `_identity.cpp` is a fragment of that file.

The message is deliberately vague: it must not tell an unauthenticated caller that a record exists at that address. Check what the surrounding code says on other creation failures and match its level of disclosure.

- [ ] **Step 4: Run to verify it passes**

```bash
scripts/rots-docker.sh test --gtest_filter='AccountIndexTest.*'
```

Expected: all index tests PASS.

- [ ] **Step 5: Run the full account suite**

```bash
scripts/rots-docker.sh test --gtest_filter='Account*:Roster*:InterpreAccount*' 2>&1 | tail -40
```

Expected: matches baseline plus new tests.

- [ ] **Step 6: Format and commit**

```bash
cd src && clang-format -i -style=WebKit account_management_identity.cpp tests/account_index_tests.cpp && cd ..
git add src/account_management_identity.cpp src/tests/account_index_tests.cpp
git commit -m "fix(account): refuse to create an account over a quarantined record"
```

---

### Task 7: Wizard visibility — list quarantined records and verify the index

> **Superseded 2026-09-08:** the `account index on|off` toggle described in this task was removed.
> It existed to switch the resolvers back to the directory scan, which is not something this project
> will ever do. Only `account index` and `account index verify` ship. The code below is kept as the
> record of how the task was built, not as a description of what the command does now.

**Files:**
- Modify: `src/account_index.h`, `src/account_index.cpp` (add `rebuild_report`)
- Modify: `src/account_management_storage.h`, `src/account_management_storage.cpp` (add `enumerate_account_records_on_disk`)
- Modify: `src/act_wiz.cpp:3161` (`do_account` — a new `index` subcommand, not a new command)
- Test: `src/tests/account_index_tests.cpp`
- **Not** modified: `src/interpre.cpp`. Extending an existing command avoids touching the command table.

**Interfaces:**
- Consumes: `account_index::quarantined_entries` (Task 2), `account_index::size` (Task 1).
- Produces: `account_index::Entry` rendering used only by the wizard command.

**Two things an immortal needs:** what got quarantined (or it rots unnoticed), and whether the live index still agrees with what is on disk (drift is silent otherwise). Verification is on demand, not on a timer — the deploy is when drift would be introduced, and that is when someone runs this.

- [ ] **Step 1: Write the failing test**

Append to `src/tests/account_index_tests.cpp`:

```cpp
TEST_F(AccountIndexTest, RebuildReportNamesEveryDisagreement)
{
    account_index::upsert(make_account("real@example.com", "real", { "Frodo" }),
        "accounts/P-T/real@example.com/account.json");

    // A record the live index knows about that a fresh scan would not produce.
    account_index::upsert(make_account("ghost@example.com", "ghost", {}),
        "accounts/F-J/ghost@example.com/account.json");

    std::vector<account_index::Entry> on_disk;
    account_index::Entry real_entry;
    real_entry.normalized_email = "real@example.com";
    real_entry.record_path = "accounts/P-T/real@example.com/account.json";
    real_entry.normalized_account_name = "real";
    on_disk.push_back(real_entry);

    const std::vector<std::string> disagreements = account_index::rebuild_report(on_disk);
    ASSERT_EQ(disagreements.size(), 1u);
    EXPECT_NE(disagreements[0].find("ghost@example.com"), std::string::npos);
}

TEST_F(AccountIndexTest, RebuildReportIsEmptyWhenTheIndexAgrees)
{
    account_index::upsert(make_account("real@example.com", "real", { "Frodo" }),
        "accounts/P-T/real@example.com/account.json");

    std::vector<account_index::Entry> on_disk;
    account_index::Entry real_entry;
    real_entry.normalized_email = "real@example.com";
    real_entry.record_path = "accounts/P-T/real@example.com/account.json";
    real_entry.normalized_account_name = "real";
    on_disk.push_back(real_entry);

    EXPECT_TRUE(account_index::rebuild_report(on_disk).empty());
}
```

- [ ] **Step 2: Run to verify it fails**

```bash
scripts/rots-docker.sh test --gtest_filter='AccountIndexTest.RebuildReport*'
```

Expected: compile failure — `rebuild_report` is undeclared.

- [ ] **Step 3: Declare and implement `rebuild_report`**

In `src/account_index.h`:

```cpp
// Compares the live index against a freshly enumerated view of what is on disk and returns one
// human-readable line per disagreement (missing, extra, or a differing path or account name).
// Empty means the index and the disk agree. Takes the disk view as an argument rather than reading
// it, so this is testable without a filesystem and so the caller owns the (readdir) enumeration.
std::vector<std::string> rebuild_report(const std::vector<Entry>& records_on_disk);
```

In `src/account_index.cpp`:

```cpp
std::vector<std::string> rebuild_report(const std::vector<Entry>& records_on_disk)
{
    std::vector<std::string> disagreements;
    std::unordered_set<std::string> seen;

    for (const Entry& record : records_on_disk) {
        seen.insert(record.normalized_email);
        const auto indexed = g_entries.find(record.normalized_email);
        if (indexed == g_entries.end()) {
            disagreements.push_back("missing from index: " + record.normalized_email);
            continue;
        }
        if (indexed->second.record_path != record.record_path) {
            disagreements.push_back("path differs for " + record.normalized_email + ": index has "
                + indexed->second.record_path + ", disk has " + record.record_path);
        }
        if (indexed->second.normalized_account_name != record.normalized_account_name) {
            disagreements.push_back("account name differs for " + record.normalized_email + ": index has "
                + indexed->second.normalized_account_name + ", disk has " + record.normalized_account_name);
        }
    }

    for (const auto& indexed : g_entries) {
        if (seen.find(indexed.first) == seen.end())
            disagreements.push_back("in index but not on disk: " + indexed.first);
    }

    return disagreements;
}
```

- [ ] **Step 4: Run to verify it passes**

```bash
scripts/rots-docker.sh test --gtest_filter='AccountIndexTest.RebuildReport*'
```

Expected: 2 tests PASS.

- [ ] **Step 5: Add an `index` subcommand to the existing `account` command**

**Do not add a new command.** `do_account` already exists at `act_wiz.cpp:3161`, registered as command index 221 at `LEVEL_GRGOD` (`interpre.cpp:2186`, name at `interpre.cpp:532`), and it already dispatches subcommands. Extending it needs no command-table change and so cannot produce a command-index collision — the class of bug that broke `color` against `CMD_SEND` in 45 rooms.

Placement matters. `do_account` requires a second word (`account_identifier`) and errors out before dispatch when it is missing, so the new subcommand must be handled **before** that guard. Insert immediately after the `if (!*subcommand)` usage block and before `if (!*account_identifier)`:

```cpp
    if (!str_cmp(subcommand, "index")) {
        char index_action[MAX_INPUT_LENGTH];
        half_chop(buf, index_action, value);

        sprintf(buf1, "Account index: %lu record(s) indexed, %lu quarantined.\n\r",
            static_cast<unsigned long>(account_index::size()),
            static_cast<unsigned long>(account_index::quarantined_count()));
        send_to_char(buf1, ch);

        for (const account_index::Entry& entry : account_index::quarantined_entries()) {
            sprintf(buf1, "  QUARANTINED %s (%s): %s\n\r", entry.normalized_email.c_str(),
                entry.record_path.c_str(), entry.quarantine_reason.c_str());
            send_to_char(buf1, ch);
        }

        if (!str_cmp(index_action, "on") || !str_cmp(index_action, "off")) {
            // The spec's rollback story is "one setting, not a redeploy". account_cache sets its own
            // flag at boot and offers no runtime control; the index needs better than that, because
            // turning it off is the response to discovering drift on the live port. Turning it back
            // ON is safe at any time only because the index is maintained on every write regardless
            // of this flag -- the flag governs whether the resolvers CONSULT it, never whether it is
            // kept current. If that ever stops being true, this toggle has to rebuild before it arms.
            const bool enable = !str_cmp(index_action, "on");
            account_index::set_enabled(enable);
            sprintf(buf1, "Account index lookups are now %s.\n\r", enable ? "ON" : "OFF");
            send_to_char(buf1, ch);
            sprintf(buf1, "%s turned the account index %s.", GET_NAME(ch), enable ? "on" : "off");
            mudlog(buf1, BRF, LEVEL_GRGOD, TRUE);
            return;
        }

        if (!str_cmp(index_action, "verify")) {
            std::vector<account_index::Entry> records_on_disk;
            if (!account::enumerate_account_records_on_disk(".", &records_on_disk)) {
                send_to_char("Could not read the accounts directory.\n\r", ch);
                return;
            }

            const std::vector<std::string> disagreements = account_index::rebuild_report(records_on_disk);
            if (disagreements.empty()) {
                send_to_char("Index agrees with disk.\n\r", ch);
                return;
            }

            for (const std::string& line : disagreements) {
                sprintf(buf1, "  DRIFT %s\n\r", line.c_str());
                send_to_char(buf1, ch);
            }
        }

        return;
    }
```

Add `#include "account_index.h"` to `act_wiz.cpp`. Check that `buf1` is the scratch buffer this file uses for `send_to_char` — read a neighbouring handler and match it rather than assuming.

Update the usage string on the line above to include the new subcommand:

```cpp
        send_to_char("Usage: account <show|verify|unverify|block|unblock|passwd|addchar|migratechar|unlockselect|index> <email-or-account> [value]\n\r", ch);
```

The `index` subcommand therefore takes three forms: `account index` (counts and the quarantine list), `account index verify` (compare the live index against disk), and `account index on|off` (the runtime kill switch). Mention all three in the help entry if you add one.

- [ ] **Step 5b: Add the disk enumerator the verify path needs**

`rebuild_report` deliberately takes the disk view as an argument, so something has to produce it. Add to `account_management_storage.h`:

```cpp
// Enumerates the account records on disk into index Entry form, for verifying the live index
// against the filesystem. Reads and parses every record, so this is a deliberate wizard-invoked
// operation, never something on a player path.
bool enumerate_account_records_on_disk(const std::string& root_directory,
    std::vector<account_index::Entry>* records, std::string* error_message = nullptr);
```

Add `#include "account_index.h"` to `account_management_storage.h` for the `Entry` type (no include cycle: `account_index.h` pulls only `account_management_types.h`). Implement it in `account_management_storage.cpp` by walking `accounts/` with the same two-level `opendir`/`readdir` structure `build_account_native_player_index` uses (`db.cpp:629`), filling `normalized_email` from the directory name, `record_path` from the composed path, and `normalized_account_name` from the parsed record. A record that fails to parse is reported with an empty account name — `rebuild_report` will surface it as a disagreement, which is the correct outcome for a verify pass.

This is the one new `readdir` this plan adds. It runs only when an immortal types `account index verify`.

- [ ] **Step 6: Compile and verify live**

```bash
scripts/rots-docker.sh compile && scripts/rots-docker.sh boot
```

Connect as an immortal, run the command with no argument (expect the counts and no quarantined entries on clean fixtures), then with `verify` (expect "index agrees with disk"). Then corrupt one record as in Task 3 Step 7, reboot, and confirm the command lists it. Restore the record.

- [ ] **Step 7: Run the full account suite and commit**

```bash
scripts/rots-docker.sh test --gtest_filter='Account*:Roster*:InterpreAccount*' 2>&1 | tail -40
cd src && clang-format -i -style=WebKit account_index.cpp account_index.h account_management_storage.h tests/account_index_tests.cpp && cd ..
git add src/account_index.cpp src/account_index.h src/account_management_storage.h src/account_management_storage.cpp src/act_wiz.cpp src/tests/account_index_tests.cpp
git commit -m "feat(account): add 'account index' to inspect and verify the account index"
```

Note: `clang-format` on `act_wiz.cpp` reformats the whole file. If the diff dwarfs your change, commit it unformatted.

---

### Task 8: Final verification pass

**Files:** none modified.

- [ ] **Step 1: Full test run against the baseline**

```bash
scripts/rots-docker.sh test 2>&1 | tail -60
```

Compare against `/tmp/account-baseline.txt`. Any failure not in the baseline is yours. The suite has pre-existing failures and may abort partway — that is expected and is why the baseline exists.

- [ ] **Step 2: Confirm the scans no longer run on the hot paths**

Boot the server, then with the index enabled, confirm by inspection that a login and a character save produce no `opendir` of `accounts/`. The simplest check: temporarily add a `log()` line at the top of the `opendir` branch in `find_account_by_email_internal` and `find_account_file_path_by_account_name`, boot, log in, save, and confirm neither line appears in `log/syslog`. **Remove the temporary logging before committing.**

- [ ] **Step 3: Confirm nothing on disk changed shape**

```bash
git status --short lib/          # expect no unexpected new files under lib/accounts
python3 -m json.tool lib/accounts/*/*/account.json > /dev/null && echo "all records still parse"
```

- [ ] **Step 4: Run the smoke scenarios**

```bash
python3 testing/scenarios.py
```

- [ ] **Step 5: Hand back for manual testing**

Do not push and do not open a PR. Report: what was implemented, the before/after test tallies, what was verified live, and what was not. The user tests manually before anything is pushed.

---

## Follow-ups this plan deliberately does not do

- **Delete the fallback scans.** Keep them for one release, then remove them in a separate change. Two live paths past that point is how drift hides.
- **Reconsider `account_cache`'s whole-record memoization.** Now that the index exists, `g_account_cache` holding full `AccountData` copies may be redundant or may still be earning its place. Decide with measurements, separately.
- **Rate-limit account creation.** A policy decision, tracked separately. Creation becoming linear removes the urgency, not the exposure.
- **The two parked PPC follow-ups** (unseeded-alt sibling scheme, the bare-integer colour clamp). They belong to the PPC rebase that follows this work, per the findings doc.
