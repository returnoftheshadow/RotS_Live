# Player Bank Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add mob program 34, a banker that gives every account one vault per side for items
and coins, plus a game settings file, the `bootoptions` command and the immortal `vault`
command.

**Architecture:**
- **`src/game_boot_options.{h,cpp}`**: a small table of named integer settings read once at
  boot from `lib/misc/game_boot_options.json`, and the `bootoptions` command.
- **`src/mob_progs/banker.{h,cpp}`**: everything bank. In layers, each testable alone:
  1. pure helpers: options parser, side lookup, day counting, fee maths, balance formatter;
  2. the vault model and its JSON;
  3. the vault table: one in-memory copy per account + side, loaded on first use, written
     atomically on every change;
  4. `SPECIAL(banker)` (balance / deposit / withdraw, refusals, protection);
  5. `ACMD(do_vault)` for immortals.
- Game-coupled pieces the tests must control (where the account folder is, how a character
  is saved, what time it is) sit behind three replaceable hooks.
- Bankers reuse barter vendor code (`src/mob_progs/shopkeeper.{h,cpp}`): hours parsing, the
  refusal checks, `say`, the warning line format.

**Tech Stack:** C++17 (built `-m32`), GNU make (`src/Makefile`, canonical), CMake (IDE and
tests), GoogleTest, Docker i386 toolchain via `scripts/rots-docker.sh`.

**Spec:** `docs/superpowers/specs/2026-09-30-player-bank-design.md` (approved 2026-09-30,
nine written decisions confirmed). Read it before starting any task.

## Global Constraints

- Program number: **34** (`PROG_BANKER`). Options come from mob editor field **42**.
- Sides: **1** light = races 1-6; **2** dark = Uruk 11, Orc 13, Olog-hai 17; **3** third =
  Magus 15, Haradrim 18. Any other race (God, Easterling, ...) has **no side** and is refused.
- Vault files: `vault_light.json`, `vault_dark.json`, `vault_third.json` in the account's
  folder. A missing file is an empty vault. An unreadable file is **never overwritten**.
- Settings (name, default, range): `bank_slots` 10, 1-100; `bank_coin_limit_gold` 1000,
  0-100000; `bank_day_start_hour` 5, 0-23. File: `lib/misc/game_boot_options.json`. A missing
  file, missing key or bad value never stops boot.
- Banker options: `hours=` (vendor format), `fee=` 1-10000 copper, `maxdays=` 1-365,
  `racial_markup=` `yes` (30) or 1-300. Bad value, or `fee=` without `maxdays=` → banker does
  no business + warning. `racial_markup=` without `fee=` → `/imp` warning only.
- Fee = `min(days, maxdays) × fee × items`, marked up for another race, rounded **up** to a
  whole copper. Coins are never charged. A day starts at the day-start hour in server
  **local** time; count clock boundaries, never reboots.
- A container is **one slot**; its contents count for the fee only. Sealed while stored.
- Only **loose inventory** can be deposited. The bank refuses exactly what rent refuses
  (`Crash_is_unrentable`), for the item and everything inside it.
- **Write order (dupe, never loss):** deposit and `vault put` write the vault file first, then
  the character. Withdraw and `vault take` save the character first, then the vault file.
- **One in-memory copy per vault.** Nothing may read or write a vault file except through the
  vault table.
- Every message a player sees is at most **78 columns**.
- Warnings follow the house style: type + vnum + line, no names, no prose
  (`vendor_problem_line`).
- `vault` and its subcommands: level `LEVEL_GRGOD` (97). `bootoptions`: `LEVEL_IMPL` (100).
- Every `vault` command writes one `(GC)` log line. Every player deposit and withdrawal
  writes one `BANK:` log line.
- Helps must say fees are **in copper**.
- Refresh-on-withdraw (PR #343) is **not** part of this plan. Leave the one marked call site.
- Format only changed files: `cd src && clang-format -i -style=WebKit <files>`. Never
  `make format`.
- `src/` has mixed CRLF/LF line endings. Edit binary-safe and check `git diff --stat` shows
  only the lines you meant to change.
- Do not commit unless the task step says so, and never push.

## Review Focus

1. **A vault file that became unreadable** (hand edit, disk fault): the banker must refuse
   that vault and leave the file alone, and other vaults must keep working. Test in Task 4.
2. **An item whose prototype was deleted from the world** while stored: withdraw must refuse
   cleanly, keep the slot, and leave no half-built objects behind. Test in Task 5.
3. **Huge or odd coin amounts** (`deposit 0 gold`, `deposit 999999999 gold`, negative via
   overflow, more than carried): refused with a message, nothing changes. Test in Task 6.
4. **Lowering `bank_slots` or the coin limit below what a vault already holds:** nothing is
   removed; deposits are refused until it is back under. Test in Task 6.
5. **A character whose link drops, or who has no account name on the descriptor:** every
   bank command must refuse rather than open a vault with an empty key. Test in Task 5.

---

## File map

| File | Status | Holds |
|------|--------|-------|
| `src/game_boot_options.h` / `.cpp` | new | settings table, file read/write, `do_bootoptions` |
| `src/mob_progs/banker.h` / `.cpp` | new | banker program, vault table, `do_vault` |
| `src/mob_progs/shopkeeper.h` / `.cpp` | modify | export four helpers bankers reuse |
| `src/objects_json.h` / `.cpp` | modify | export the single-record JSON writer and reader |
| `src/interpre.h` / `.cpp` | modify | `CMD_BALANCE/DEPOSIT/WITHDRAW`, commands 253 `vault`, 254 `bootoptions` |
| `src/spec_ass.cpp` | modify | program 34 in both tables |
| `src/db.cpp` | modify | load settings and banker configs at boot |
| `src/shapemob.cpp` | modify | banker checks next to the vendor checks |
| `src/Makefile`, `src/CMakeLists.txt` | modify | new sources and tests |
| `src/tests/game_boot_options_tests.cpp` | new | settings tests |
| `src/tests/banker_tests.cpp` | new | all bank tests |
| `lib/text/help_tbl`, `lib/text/shap_tbl` | modify | helps |
| `docs/shape_mob.md`, `docs/systems/bank.md` | modify / new | docs |
| `testing/smoke_bank.py` | new (untracked, local) | in-game smoke test |

Run tests with `scripts/rots-docker.sh test --gtest_filter='<Suite>.*'`. If the link fails
with `__throw_bad_array_new_length`, delete host-built objects first: `rm -f src/*.o
src/mob_progs/*.o`.

---

### Task 1: Game settings file and `bootoptions`

**Files:**
- Create: `src/game_boot_options.h`, `src/game_boot_options.cpp`
- Create: `src/tests/game_boot_options_tests.cpp`
- Modify: `src/Makefile:74` (object list), `src/CMakeLists.txt:82` and `:122` (source and
  test lists), `src/db.cpp:484`, `src/interpre.cpp` (command table)

**Interfaces:**
- Produces:
  ```cpp
  enum { BOOT_BANK_SLOTS, BOOT_BANK_COIN_LIMIT_GOLD, BOOT_BANK_DAY_START_HOUR, BOOT_OPTION_COUNT };
  struct boot_option_def { const char* name; int def; int min; int max; const char* what; };
  extern const boot_option_def BOOT_OPTION_DEFS[BOOT_OPTION_COUNT];
  struct boot_options_values { int v[BOOT_OPTION_COUNT]; };
  boot_options_values boot_options_defaults();
  boot_options_values boot_options_parse(const std::string& json, std::vector<std::string>* warnings);
  std::string boot_options_serialize(const boot_options_values& values);
  int boot_option_index(const std::string& name);            /* -1 if unknown */
  void boot_options_load(const char* path = BOOT_OPTIONS_PATH);
  int boot_option(int index);                                /* value in use this boot */
  int boot_option_pending(int index);                        /* value in the file */
  bool boot_option_set(int index, int value, std::string* error, const char* path = BOOT_OPTIONS_PATH);
  void boot_options_set_running_for_tests(int index, int value);
  ACMD(do_bootoptions);
  ```

- [ ] **Step 1: Write the failing tests**

`src/tests/game_boot_options_tests.cpp`:

```cpp
#include "../game_boot_options.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <unistd.h>

namespace {
std::string temp_file()
{
    char path[] = "/tmp/bootopts_XXXXXX";
    int fd = mkstemp(path);
    EXPECT_GE(fd, 0);
    close(fd);
    std::remove(path); /* tests start with the file missing */
    return path;
}
} // namespace

TEST(BootOptions, DefaultsWhenTextIsEmpty)
{
    std::vector<std::string> warnings;
    boot_options_values v = boot_options_parse("", &warnings);
    EXPECT_EQ(v.v[BOOT_BANK_SLOTS], 10);
    EXPECT_EQ(v.v[BOOT_BANK_COIN_LIMIT_GOLD], 1000);
    EXPECT_EQ(v.v[BOOT_BANK_DAY_START_HOUR], 5);
    EXPECT_TRUE(warnings.empty());
}

TEST(BootOptions, MissingKeyKeepsItsDefault)
{
    std::vector<std::string> warnings;
    boot_options_values v = boot_options_parse("{\"bank_slots\": 20}", &warnings);
    EXPECT_EQ(v.v[BOOT_BANK_SLOTS], 20);
    EXPECT_EQ(v.v[BOOT_BANK_COIN_LIMIT_GOLD], 1000);
    EXPECT_TRUE(warnings.empty());
}

TEST(BootOptions, OutOfRangeValueUsesDefaultAndWarns)
{
    std::vector<std::string> warnings;
    boot_options_values v = boot_options_parse(
        "{\"bank_slots\": 0, \"bank_day_start_hour\": 24, \"bank_coin_limit_gold\": 500}", &warnings);
    EXPECT_EQ(v.v[BOOT_BANK_SLOTS], 10);
    EXPECT_EQ(v.v[BOOT_BANK_DAY_START_HOUR], 5);
    EXPECT_EQ(v.v[BOOT_BANK_COIN_LIMIT_GOLD], 500);
    ASSERT_EQ(warnings.size(), 2u);
    EXPECT_EQ(warnings[0], "BOOT OPTIONS: bank_slots: 0 out of range 1-100 - default 10 used");
}

TEST(BootOptions, UnknownKeyWarnsAndIsSkipped)
{
    std::vector<std::string> warnings;
    boot_options_values v = boot_options_parse("{\"bogus\": 3, \"bank_slots\": 12}", &warnings);
    EXPECT_EQ(v.v[BOOT_BANK_SLOTS], 12);
    ASSERT_EQ(warnings.size(), 1u);
    EXPECT_EQ(warnings[0], "BOOT OPTIONS: bogus: unknown setting - ignored");
}

TEST(BootOptions, BrokenFileUsesAllDefaultsAndWarnsOnce)
{
    const char* broken[] = { "{", "not json", "{\"bank_slots\": \"ten\"}", "[1,2]" };
    for (const char* text : broken) {
        std::vector<std::string> warnings;
        boot_options_values v = boot_options_parse(text, &warnings);
        EXPECT_EQ(v.v[BOOT_BANK_SLOTS], 10) << text;
        EXPECT_EQ(v.v[BOOT_BANK_COIN_LIMIT_GOLD], 1000) << text;
        ASSERT_EQ(warnings.size(), 1u) << text;
        EXPECT_EQ(warnings[0].compare(0, 37, "BOOT OPTIONS: file unreadable - defau"), 0) << warnings[0];
    }
}

TEST(BootOptions, SerializeRoundTrips)
{
    boot_options_values v = boot_options_defaults();
    v.v[BOOT_BANK_SLOTS] = 25;
    std::vector<std::string> warnings;
    boot_options_values back = boot_options_parse(boot_options_serialize(v), &warnings);
    EXPECT_TRUE(warnings.empty());
    EXPECT_EQ(back.v[BOOT_BANK_SLOTS], 25);
}

TEST(BootOptions, IndexByName)
{
    EXPECT_EQ(boot_option_index("bank_slots"), BOOT_BANK_SLOTS);
    EXPECT_EQ(boot_option_index("BANK_SLOTS"), -1);
    EXPECT_EQ(boot_option_index(""), -1);
}

TEST(BootOptions, MissingFileLoadsDefaults)
{
    std::string path = temp_file();
    boot_options_load(path.c_str());
    EXPECT_EQ(boot_option(BOOT_BANK_SLOTS), 10);
    EXPECT_EQ(boot_option_pending(BOOT_BANK_SLOTS), 10);
}

TEST(BootOptions, SetWritesTheFileButNotTheRunningValue)
{
    std::string path = temp_file();
    boot_options_load(path.c_str());
    std::string error;
    ASSERT_TRUE(boot_option_set(BOOT_BANK_SLOTS, 30, &error, path.c_str())) << error;
    EXPECT_EQ(boot_option(BOOT_BANK_SLOTS), 10);
    EXPECT_EQ(boot_option_pending(BOOT_BANK_SLOTS), 30);
    boot_options_load(path.c_str()); /* "reboot" */
    EXPECT_EQ(boot_option(BOOT_BANK_SLOTS), 30);
    std::remove(path.c_str());
}

TEST(BootOptions, SetRefusesOutOfRangeAndLeavesTheFileAlone)
{
    std::string path = temp_file();
    boot_options_load(path.c_str());
    std::string error;
    EXPECT_FALSE(boot_option_set(BOOT_BANK_SLOTS, 101, &error, path.c_str()));
    EXPECT_EQ(error, "bank_slots must be 1-100.");
    EXPECT_EQ(boot_option_pending(BOOT_BANK_SLOTS), 10);
    EXPECT_FALSE(std::ifstream(path).good());
}
```

- [ ] **Step 2: Add the files to both build lists, then run to see the tests fail**

`src/Makefile`, after the `mob_progs/shopkeeper.cpp \` line, add `game_boot_options.cpp \`.
`src/CMakeLists.txt`: add `game_boot_options.cpp` after `mob_progs/shopkeeper.cpp`, and
`tests/game_boot_options_tests.cpp` after `tests/barter_vendor_tests.cpp`.
(These are build changes: list them in the build-change record for the user, see Task 9.)

Run: `scripts/rots-docker.sh test --gtest_filter='BootOptions.*'`
Expected: build fails, `game_boot_options.h: No such file or directory`.

- [ ] **Step 3: Write the header**

`src/game_boot_options.h`:

```cpp
#ifndef GAME_BOOT_OPTIONS_H
#define GAME_BOOT_OPTIONS_H

/* Game-wide settings staff can change without a code change. Read once at
 * boot from lib/misc/game_boot_options.json; a change takes effect at the
 * next reboot. A missing file, missing key or bad value uses the built-in
 * default and can never stop the boot. */

#include "interpre.h"

#include <string>
#include <vector>

constexpr const char* BOOT_OPTIONS_PATH = "misc/game_boot_options.json"; /* cwd is lib/ */

enum { BOOT_BANK_SLOTS,
    BOOT_BANK_COIN_LIMIT_GOLD,
    BOOT_BANK_DAY_START_HOUR,
    BOOT_OPTION_COUNT };

struct boot_option_def {
    const char* name;
    int def;
    int min;
    int max;
    const char* what;
};
extern const boot_option_def BOOT_OPTION_DEFS[BOOT_OPTION_COUNT];

struct boot_options_values {
    int v[BOOT_OPTION_COUNT];
};

boot_options_values boot_options_defaults();
boot_options_values boot_options_parse(const std::string& json, std::vector<std::string>* warnings);
std::string boot_options_serialize(const boot_options_values& values);
int boot_option_index(const std::string& name); /* -1 if unknown */

void boot_options_load(const char* path = BOOT_OPTIONS_PATH); /* logs each warning */
int boot_option(int index); /* value in use this boot */
int boot_option_pending(int index); /* value in the file: in use after the next reboot */
bool boot_option_set(int index, int value, std::string* error, const char* path = BOOT_OPTIONS_PATH);
void boot_options_set_running_for_tests(int index, int value);

ACMD(do_bootoptions);

#endif
```

- [ ] **Step 4: Write the implementation**

`src/game_boot_options.cpp`:

```cpp
#include "game_boot_options.h"

#include "comm.h"
#include "json_utils.h"
#include "structs.h"
#include "utils.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

const boot_option_def BOOT_OPTION_DEFS[BOOT_OPTION_COUNT] = {
    { "bank_slots", 10, 1, 100, "item slots in each bank vault" },
    { "bank_coin_limit_gold", 1000, 0, 100000, "most gold a bank vault holds" },
    { "bank_day_start_hour", 5, 0, 23, "hour the bank's fee day starts" },
};

namespace {
boot_options_values g_running = boot_options_defaults();
boot_options_values g_pending = boot_options_defaults();

bool in_range(int index, long value)
{
    return value >= BOOT_OPTION_DEFS[index].min && value <= BOOT_OPTION_DEFS[index].max;
}
} // namespace

boot_options_values boot_options_defaults()
{
    boot_options_values values;
    for (int i = 0; i < BOOT_OPTION_COUNT; ++i)
        values.v[i] = BOOT_OPTION_DEFS[i].def;
    return values;
}

int boot_option_index(const std::string& name)
{
    for (int i = 0; i < BOOT_OPTION_COUNT; ++i)
        if (name == BOOT_OPTION_DEFS[i].name)
            return i;
    return -1;
}

boot_options_values boot_options_parse(const std::string& json, std::vector<std::string>* warnings)
{
    boot_options_values values = boot_options_defaults();
    if (json.find_first_not_of(" \t\r\n") == std::string::npos)
        return values;

    std::vector<std::string> found;
    std::string error;
    json_utils::JsonReader reader(json);
    bool ok = reader.parse_root_object(
        [&values, &found](const std::string& key, json_utils::JsonReader* nested, std::string* nested_error) {
            int index = boot_option_index(key);
            if (index < 0) {
                found.push_back("BOOT OPTIONS: " + key + ": unknown setting - ignored");
                return nested->skip_value(nested_error);
            }
            long value = 0;
            if (!nested->parse_long(&value, nested_error))
                return false;
            if (!in_range(index, value)) {
                const boot_option_def& def = BOOT_OPTION_DEFS[index];
                found.push_back("BOOT OPTIONS: " + key + ": " + std::to_string(value) + " out of range "
                    + std::to_string(def.min) + "-" + std::to_string(def.max) + " - default "
                    + std::to_string(def.def) + " used");
                return true;
            }
            values.v[index] = (int)value;
            return true;
        },
        &error);
    if (!ok) {
        if (warnings)
            warnings->push_back("BOOT OPTIONS: file unreadable - defaults used");
        return boot_options_defaults();
    }
    if (warnings)
        warnings->insert(warnings->end(), found.begin(), found.end());
    return values;
}

std::string boot_options_serialize(const boot_options_values& values)
{
    std::ostringstream out;
    out << "{\n";
    for (int i = 0; i < BOOT_OPTION_COUNT; ++i)
        out << "  \"" << BOOT_OPTION_DEFS[i].name << "\": " << values.v[i] << (i + 1 < BOOT_OPTION_COUNT ? ",\n" : "\n");
    out << "}\n";
    return out.str();
}

void boot_options_load(const char* path)
{
    std::string text;
    std::ifstream in(path, std::ios::binary);
    if (in.good()) {
        std::ostringstream buffer;
        buffer << in.rdbuf();
        text = buffer.str();
    }
    std::vector<std::string> warnings;
    g_running = boot_options_parse(text, &warnings);
    g_pending = g_running;
    for (const std::string& warning : warnings) {
        char line[512];
        snprintf(line, sizeof(line), "%s", warning.c_str());
        log(line);
    }
}

int boot_option(int index) { return g_running.v[index]; }
int boot_option_pending(int index) { return g_pending.v[index]; }
void boot_options_set_running_for_tests(int index, int value) { g_running.v[index] = value; }

bool boot_option_set(int index, int value, std::string* error, const char* path)
{
    const boot_option_def& def = BOOT_OPTION_DEFS[index];
    if (!in_range(index, value)) {
        *error = std::string(def.name) + " must be " + std::to_string(def.min) + "-" + std::to_string(def.max) + ".";
        return false;
    }
    boot_options_values next = g_pending;
    next.v[index] = value;
    std::string temp = std::string(path) + ".tmp";
    FILE* file = fopen(temp.c_str(), "w");
    if (!file) {
        *error = std::string("Can't write the settings file: ") + strerror(errno);
        return false;
    }
    std::string json = boot_options_serialize(next);
    size_t written = fwrite(json.data(), 1, json.size(), file);
    if (fclose(file) != 0 || written != json.size() || rename(temp.c_str(), path) != 0) {
        *error = std::string("Can't write the settings file: ") + strerror(errno);
        remove(temp.c_str());
        return false;
    }
    g_pending = next;
    return true;
}

ACMD(do_bootoptions)
{
    char name[MAX_INPUT_LENGTH], value[MAX_INPUT_LENGTH], line[256];
    half_chop(argument, name, value);

    if (!*name) {
        send_to_char("Setting                   Now  After reboot  Default  Range\n\r", ch);
        for (int i = 0; i < BOOT_OPTION_COUNT; ++i) {
            const boot_option_def& def = BOOT_OPTION_DEFS[i];
            char pending[16] = "";
            if (boot_option_pending(i) != boot_option(i))
                snprintf(pending, sizeof(pending), "%d", boot_option_pending(i));
            snprintf(line, sizeof(line), "%-22s %6d  %12s  %7d  %d-%d\n\r", def.name, boot_option(i), pending,
                def.def, def.min, def.max);
            send_to_char(line, ch);
            snprintf(line, sizeof(line), "  %s\n\r", def.what);
            send_to_char(line, ch);
        }
        return;
    }
    int index = boot_option_index(name);
    if (index < 0) {
        send_to_char("No such setting. Type 'bootoptions' for the list.\n\r", ch);
        return;
    }
    char* end = nullptr;
    long number = strtol(value, &end, 10);
    if (!*value || *end || number < -1000000 || number > 1000000) {
        send_to_char("Usage: bootoptions <name> <number>\n\r", ch);
        return;
    }
    std::string error;
    if (!boot_option_set(index, (int)number, &error)) {
        send_to_char((error + "\n\r").c_str(), ch);
        return;
    }
    snprintf(line, sizeof(line), "%s set to %ld. It takes effect at the next reboot.\n\r", name, number);
    send_to_char(line, ch);
    snprintf(line, sizeof(line), "(GC) %s set boot option %s to %ld.", GET_NAME(ch), name, number);
    mudlog(line, BRF, LEVEL_IMPL, TRUE);
}
```

- [ ] **Step 5: Run the tests**

Run: `scripts/rots-docker.sh test --gtest_filter='BootOptions.*'`
Expected: 10 tests pass.

- [ ] **Step 6: Load at boot and register the command**

`src/db.cpp`: add `#include "game_boot_options.h"` beside the `mob_progs/shopkeeper.h`
include (line 40). Immediately before `log("Checking barter vendors.");` (line 484) add:

```cpp
    log("Reading game boot options.");
    boot_options_load();
```

`src/interpre.cpp`:
- add `#include "game_boot_options.h"` with the other includes;
- in the command name array, replace the final `"unprotect", // 252` / `"\n"` pair with:

```cpp
    "unprotect", // 252
    "vault",
    "bootoptions", // 254
    "\n"
```

- after the `COMMANDO(252, ...)` entry add (the `vault` line is added in Task 7; until then
  bind 253 to `do_not_here` at `LEVEL_GRGOD` so the two arrays stay aligned):

```cpp
    COMMANDO(253, POSITION_DEAD, do_not_here, LEVEL_GRGOD, FALSE, 0,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(254, POSITION_DEAD, do_bootoptions, LEVEL_IMPL, FALSE, 0,
        FULL_TARGET, FULL_TARGET, 0);
```

- [ ] **Step 7: Compile the server and the whole suite**

Run: `scripts/rots-docker.sh compile` — expected: `bin/ageland` built, no new warnings.
Run: `scripts/rots-docker.sh test` — expected: only the 6 known 32-bit failures.

- [ ] **Step 8: Format and commit**

```bash
cd src && clang-format -i -style=WebKit game_boot_options.h game_boot_options.cpp tests/game_boot_options_tests.cpp && cd ..
git diff --stat   # db.cpp and interpre.cpp must show only the lines added above
git add src/game_boot_options.h src/game_boot_options.cpp src/tests/game_boot_options_tests.cpp \
        src/Makefile src/CMakeLists.txt src/db.cpp src/interpre.cpp
git commit -m "feat(bootoptions): game settings file and bootoptions command"
```

---

### Task 2: Banker options, sides, days and fees (pure)

**Files:**
- Create: `src/mob_progs/banker.h`, `src/mob_progs/banker.cpp`, `src/tests/banker_tests.cpp`
- Modify: `src/mob_progs/shopkeeper.h`, `src/mob_progs/shopkeeper.cpp`, `src/Makefile`,
  `src/CMakeLists.txt`

**Interfaces:**
- Consumes: `vendor_hours_parse`, `vendor_hours_window`, `vendor_problem` (shopkeeper.h).
- Produces (shopkeeper.h, moved out of its unnamed namespace, bodies unchanged):
  ```cpp
  bool vendor_hours_open(const std::vector<vendor_hours_window>& hours, int hour); /* empty = always */
  void vendor_say(struct char_data* vendor, const char* text);
  bool vendor_serves_customer(struct char_data* vendor, struct char_data* ch); /* the four non-hours checks */
  bool give_targets(struct char_data* vendor, struct char_data* ch, char* arg);
  void vendor_send(const std::string& line, struct char_data* builder);
  ```
- Produces (banker.h):
  ```cpp
  constexpr int PROG_BANKER = 34;
  constexpr int BANKER_FEE_MAX = 10000;
  constexpr int BANKER_MAXDAYS_MAX = 365;
  constexpr int BANKER_MARKUP_DEFAULT = 30;
  constexpr int BANKER_MARKUP_MAX = 300;
  enum { BANK_SIDE_NONE = 0, BANK_SIDE_LIGHT = 1, BANK_SIDE_DARK = 2, BANK_SIDE_THIRD = 3 };
  struct banker_config {
      bool ok = true;                          /* false: does no business */
      std::vector<vendor_hours_window> hours;  /* empty = always open */
      int fee = 0;                             /* copper per item per day; 0 = free */
      int maxdays = 0;
      int markup = 0;                          /* percent for another race; 0 = none */
  };
  banker_config parse_banker_options(const char* text, std::vector<vendor_problem>* problems);
  int bank_side_for_race(int race);
  const char* bank_side_file_name(int side);   /* nullptr for BANK_SIDE_NONE */
  int bank_days_stored(time_t deposited, time_t now, int start_hour);
  long long bank_fee(const banker_config& config, int days, int items, bool other_race);
  ```

- [ ] **Step 1: Export the vendor helpers**

In `src/mob_progs/shopkeeper.cpp`:
- Move `vendor_send` (line ~278), `vendor_say` (~440) and `give_targets` (~632) out of their
  unnamed namespaces. Bodies unchanged.
- Add `vendor_hours_open` and make `vendor_is_open` call it:

```cpp
bool vendor_hours_open(const std::vector<vendor_hours_window>& hours, int hour)
{
    if (hours.empty())
        return true;
    for (const vendor_hours_window& w : hours) {
        bool open = w.open < w.close ? (hour >= w.open && hour < w.close) : (hour >= w.open || hour < w.close);
        if (open)
            return true;
    }
    return false;
}

bool vendor_is_open(const vendor_config& config, int hour)
{
    return vendor_hours_open(config.hours, hour);
}
```

- Split `vendor_serves`: the first four checks become the exported
  `vendor_serves_customer(vendor, ch)` (same messages, same order); `vendor_serves` becomes:

```cpp
bool vendor_serves(struct char_data* vendor, struct char_data* ch, const vendor_config& config)
{
    if (!vendor_serves_customer(vendor, ch))
        return false;
    if (!vendor_is_open(config, time_info.hours)) {
        vendor_say(vendor, "I'm closed. Come back later.");
        return false;
    }
    return true;
}
```

Declare the five functions in `shopkeeper.h` under the existing declarations.

Run: `scripts/rots-docker.sh test --gtest_filter='Vendor*:BarterVendorTest.*'`
Expected: every vendor test still passes (this step changes no behaviour).

- [ ] **Step 2: Write the failing tests**

`src/tests/banker_tests.cpp`:

```cpp
#include "../mob_progs/banker.h"

#include "../structs.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <ctime>
#include <string>
#include <vector>

namespace {

std::vector<std::string> texts(const std::vector<vendor_problem>& problems)
{
    std::vector<std::string> out;
    for (const vendor_problem& p : problems)
        out.push_back(std::to_string(p.line) + ": " + p.text);
    return out;
}

/* US Central with daylight saving, written out so no tzdata is needed. */
struct CentralTime {
    CentralTime()
    {
        setenv("TZ", "CST6CDT,M3.2.0,M11.1.0", 1);
        tzset();
    }
};

time_t at(int year, int month, int day, int hour, int minute = 0)
{
    static CentralTime zone;
    struct tm tm { };
    tm.tm_year = year - 1900;
    tm.tm_mon = month - 1;
    tm.tm_mday = day;
    tm.tm_hour = hour;
    tm.tm_min = minute;
    tm.tm_isdst = -1;
    return mktime(&tm);
}

} // namespace

TEST(BankerParse, EmptyOptionsIsAFreeAlwaysOpenBanker)
{
    std::vector<vendor_problem> problems;
    banker_config c = parse_banker_options("", &problems);
    EXPECT_TRUE(c.ok);
    EXPECT_EQ(c.fee, 0);
    EXPECT_TRUE(c.hours.empty());
    EXPECT_TRUE(problems.empty());
    EXPECT_TRUE(parse_banker_options(nullptr, &problems).ok);
}

TEST(BankerParse, FullConfig)
{
    std::vector<vendor_problem> problems;
    banker_config c = parse_banker_options("hours=6-20\n\rfee=50\n\rmaxdays=30\n\rracial_markup=yes\n\r", &problems);
    EXPECT_TRUE(problems.empty()) << ::testing::PrintToString(texts(problems));
    EXPECT_TRUE(c.ok);
    ASSERT_EQ(c.hours.size(), 1u);
    EXPECT_EQ(c.fee, 50);
    EXPECT_EQ(c.maxdays, 30);
    EXPECT_EQ(c.markup, 30);
}

TEST(BankerParse, MarkupNumber)
{
    EXPECT_EQ(parse_banker_options("fee=1\nmaxdays=1\nracial_markup=300", nullptr).markup, 300);
    EXPECT_EQ(parse_banker_options("fee=1\nmaxdays=1\nracial_markup=1", nullptr).markup, 1);
}

TEST(BankerParse, BadValuesDisableStrictly)
{
    struct {
        const char* text;
        const char* problem;
    } cases[] = {
        { "hours=6", "1: bad hours - banker disabled" },
        { "fee=0\nmaxdays=5", "1: bad fee - banker disabled" },
        { "fee=10001\nmaxdays=5", "1: bad fee - banker disabled" },
        { "fee=abc\nmaxdays=5", "1: bad fee - banker disabled" },
        { "fee=-5\nmaxdays=5", "1: bad fee - banker disabled" },
        { "fee=5\nmaxdays=0", "2: bad maxdays - banker disabled" },
        { "fee=5\nmaxdays=366", "2: bad maxdays - banker disabled" },
        { "fee=5\nmaxdays=3\nracial_markup=0", "3: bad racial_markup - banker disabled" },
        { "fee=5\nmaxdays=3\nracial_markup=301", "3: bad racial_markup - banker disabled" },
        { "fee=5\nmaxdays=3\nracial_markup=no", "3: bad racial_markup - banker disabled" },
        { "fee=5", "0: fee without maxdays - banker disabled" },
    };
    for (const auto& c : cases) {
        std::vector<vendor_problem> problems;
        EXPECT_FALSE(parse_banker_options(c.text, &problems).ok) << c.text;
        ASSERT_FALSE(problems.empty()) << c.text;
        EXPECT_EQ(texts(problems)[0], c.problem) << c.text;
    }
}

TEST(BankerParse, CommentsBlanksDuplicatesAndUnknowns)
{
    std::vector<vendor_problem> problems;
    banker_config c = parse_banker_options("// note\n\nfee=5\nfee=9\nmaxdays=3\nstore=12\n", &problems);
    EXPECT_TRUE(c.ok);
    EXPECT_EQ(c.fee, 5);
    std::vector<std::string> expected = { "4: duplicate fee - line ignored", "6: unknown setting - line ignored" };
    EXPECT_EQ(texts(problems), expected);
}

TEST(BankerParse, MarkupWithoutFeeIsNotStrict)
{
    std::vector<vendor_problem> problems;
    banker_config c = parse_banker_options("racial_markup=yes", &problems);
    EXPECT_TRUE(c.ok);
    EXPECT_EQ(c.markup, 30);
    EXPECT_EQ(c.fee, 0);
    EXPECT_TRUE(problems.empty());
}

TEST(BankSide, EveryPlayableRace)
{
    for (int race : { RACE_HUMAN, RACE_DWARF, RACE_WOOD, RACE_HOBBIT, RACE_HIGH, RACE_BEORNING })
        EXPECT_EQ(bank_side_for_race(race), BANK_SIDE_LIGHT) << race;
    for (int race : { RACE_URUK, RACE_ORC, RACE_OLOGHAI })
        EXPECT_EQ(bank_side_for_race(race), BANK_SIDE_DARK) << race;
    for (int race : { RACE_MAGUS, RACE_HARADRIM })
        EXPECT_EQ(bank_side_for_race(race), BANK_SIDE_THIRD) << race;
    for (int race : { RACE_GOD, RACE_EASTERLING, RACE_HARAD, 7, 16, 19, -1, 200 })
        EXPECT_EQ(bank_side_for_race(race), BANK_SIDE_NONE) << race;
}

TEST(BankSide, FileNames)
{
    EXPECT_STREQ(bank_side_file_name(BANK_SIDE_LIGHT), "vault_light.json");
    EXPECT_STREQ(bank_side_file_name(BANK_SIDE_DARK), "vault_dark.json");
    EXPECT_STREQ(bank_side_file_name(BANK_SIDE_THIRD), "vault_third.json");
    EXPECT_EQ(bank_side_file_name(BANK_SIDE_NONE), nullptr);
    EXPECT_EQ(bank_side_file_name(4), nullptr);
}

TEST(BankDays, SameBankDayIsZero)
{
    EXPECT_EQ(bank_days_stored(at(2026, 9, 30, 10), at(2026, 9, 30, 23), 5), 0);
    EXPECT_EQ(bank_days_stored(at(2026, 9, 30, 23), at(2026, 10, 1, 4, 59), 5), 0);
}

TEST(BankDays, EachFiveAmCrossedAddsOne)
{
    EXPECT_EQ(bank_days_stored(at(2026, 9, 30, 23), at(2026, 10, 1, 5), 5), 1);
    EXPECT_EQ(bank_days_stored(at(2026, 9, 30, 4), at(2026, 9, 30, 6), 5), 1);
    EXPECT_EQ(bank_days_stored(at(2026, 9, 27, 12), at(2026, 9, 30, 12), 5), 3);
    EXPECT_EQ(bank_days_stored(at(2026, 12, 31, 12), at(2027, 1, 1, 12), 5), 1);
}

TEST(BankDays, DaylightSavingChangesStillCountOnePerDay)
{
    EXPECT_EQ(bank_days_stored(at(2026, 3, 7, 12), at(2026, 3, 8, 12), 5), 1); /* spring forward */
    EXPECT_EQ(bank_days_stored(at(2026, 3, 7, 12), at(2026, 3, 9, 4), 5), 1);
    EXPECT_EQ(bank_days_stored(at(2026, 10, 31, 12), at(2026, 11, 1, 12), 5), 1); /* fall back */
    EXPECT_EQ(bank_days_stored(at(2026, 10, 31, 12), at(2026, 11, 2, 12), 5), 2);
}

TEST(BankDays, ClockGoingBackwardsIsZeroNotNegative)
{
    EXPECT_EQ(bank_days_stored(at(2026, 9, 30, 12), at(2026, 9, 20, 12), 5), 0);
}

TEST(BankDays, OtherStartHours)
{
    EXPECT_EQ(bank_days_stored(at(2026, 9, 30, 23), at(2026, 10, 1, 0, 1), 0), 1);
    EXPECT_EQ(bank_days_stored(at(2026, 9, 30, 1), at(2026, 9, 30, 22), 23), 0);
    EXPECT_EQ(bank_days_stored(at(2026, 9, 30, 22), at(2026, 9, 30, 23), 23), 1);
}

TEST(BankFee, FreeBankerAndSameDay)
{
    banker_config free_banker;
    EXPECT_EQ(bank_fee(free_banker, 30, 4, true), 0);
    banker_config c;
    c.fee = 50;
    c.maxdays = 30;
    EXPECT_EQ(bank_fee(c, 0, 4, false), 0);
}

TEST(BankFee, DaysTimesFeeTimesItemsCappedAtMaxdays)
{
    banker_config c;
    c.fee = 50;
    c.maxdays = 30;
    EXPECT_EQ(bank_fee(c, 3, 1, false), 150);
    EXPECT_EQ(bank_fee(c, 3, 4, false), 600);
    EXPECT_EQ(bank_fee(c, 30, 1, false), 1500);
    EXPECT_EQ(bank_fee(c, 400, 1, false), 1500);
}

TEST(BankFee, MarkupOnlyForAnotherRaceRoundedUp)
{
    banker_config c;
    c.fee = 1;
    c.maxdays = 30;
    c.markup = 30;
    EXPECT_EQ(bank_fee(c, 1, 1, false), 1);
    EXPECT_EQ(bank_fee(c, 1, 1, true), 2); /* 1.3 -> 2 */
    EXPECT_EQ(bank_fee(c, 10, 1, true), 13); /* exact */
    c.markup = 300;
    EXPECT_EQ(bank_fee(c, 10, 1, true), 40);
}

TEST(BankFee, LargestPossibleFeeDoesNotOverflow)
{
    banker_config c;
    c.fee = BANKER_FEE_MAX;
    c.maxdays = BANKER_MAXDAYS_MAX;
    c.markup = BANKER_MARKUP_MAX;
    EXPECT_EQ(bank_fee(c, 365, 1000, true), 14600000000LL);
}
```

- [ ] **Step 3: Add to both build lists and run to see them fail**

`src/Makefile`: add `mob_progs/banker.cpp \` after `mob_progs/shopkeeper.cpp \`.
`src/CMakeLists.txt`: add `mob_progs/banker.cpp` after `mob_progs/shopkeeper.cpp`, and
`tests/banker_tests.cpp` after `tests/barter_vendor_tests.cpp`.

Run: `scripts/rots-docker.sh test --gtest_filter='Banker*:Bank*'`
Expected: build fails, `banker.h: No such file or directory`.

- [ ] **Step 4: Write the header**

`src/mob_progs/banker.h`:

```cpp
#ifndef MOB_PROGS_BANKER_H
#define MOB_PROGS_BANKER_H

/* Banker mob program (program 34): each account's vault of items and coins,
 * one per side, the same at every banker on that side. */

#include "shopkeeper.h"

#include <ctime>
#include <string>
#include <vector>

constexpr int PROG_BANKER = 34;
constexpr int BANKER_FEE_MAX = 10000; /* copper per item per day */
constexpr int BANKER_MAXDAYS_MAX = 365;
constexpr int BANKER_MARKUP_DEFAULT = 30; /* racial_markup=yes */
constexpr int BANKER_MARKUP_MAX = 300;

enum { BANK_SIDE_NONE = 0,
    BANK_SIDE_LIGHT = 1,
    BANK_SIDE_DARK = 2,
    BANK_SIDE_THIRD = 3 };

/* Banker settings: "hours=<windows>", "fee=<copper>", "maxdays=<n>",
 * "racial_markup=yes|<percent>". All optional. */
struct banker_config {
    bool ok = true; /* false: the banker does no business */
    std::vector<vendor_hours_window> hours; /* empty = always open */
    int fee = 0; /* copper per item per day; 0 = free */
    int maxdays = 0;
    int markup = 0; /* percent added for another race; 0 = none */
};

banker_config parse_banker_options(const char* text, std::vector<vendor_problem>* problems);

/* BANK_SIDE_NONE for any race with no vault (immortals, NPC-only races). */
int bank_side_for_race(int race);
const char* bank_side_file_name(int side); /* nullptr for no side */

/* How many day-start points (start_hour, server local time) lie between the
 * two times. Never negative. */
int bank_days_stored(time_t deposited, time_t now, int start_hour);

/* Copper to withdraw one slot holding `items` objects after `days` days. */
long long bank_fee(const banker_config& config, int days, int items, bool other_race);

#endif
```

- [ ] **Step 5: Write the implementation**

`src/mob_progs/banker.cpp`:

```cpp
#include "banker.h"

#include "../structs.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

using mob_options_detail::split_lines;
using mob_options_detail::trim;

namespace {

bool parse_number(const std::string& s, int* out)
{
    if (s.empty() || s.size() > 9)
        return false;
    for (char c : s)
        if (!isdigit((unsigned char)c))
            return false;
    *out = atoi(s.c_str());
    return true;
}

/* Days since 1970-01-01 for a calendar date (proleptic Gregorian). */
long days_from_civil(int y, int m, int d)
{
    y -= m <= 2;
    const long era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (long)doe - 719468;
}

/* The bank day a moment falls in: its local date, or the day before when
 * the local hour is still short of the start hour. */
long bank_day_index(time_t when, int start_hour)
{
    struct tm local { };
    localtime_r(&when, &local);
    long day = days_from_civil(local.tm_year + 1900, local.tm_mon + 1, local.tm_mday);
    if (local.tm_hour < start_hour)
        --day;
    return day;
}

} // namespace

banker_config parse_banker_options(const char* text, std::vector<vendor_problem>* problems)
{
    banker_config config;
    bool saw_hours = false, saw_fee = false, saw_maxdays = false, saw_markup = false;
    auto problem = [&](int line, const std::string& what) {
        if (problems)
            problems->push_back({ line, what });
    };
    auto strict = [&](int line, const char* key) {
        config.ok = false;
        problem(line, std::string("bad ") + key + " - banker disabled");
    };

    int line_no = 0;
    for (const std::string& raw : split_lines(text ? text : "")) {
        ++line_no;
        std::string line = trim(raw);
        if (line.empty() || line.compare(0, 2, "//") == 0)
            continue;
        size_t eq = line.find('=');
        std::string key = trim(eq == std::string::npos ? line : line.substr(0, eq));
        std::string value = eq == std::string::npos ? "" : trim(line.substr(eq + 1));
        bool* seen = key == "hours" ? &saw_hours : key == "fee" ? &saw_fee
            : key == "maxdays"                               ? &saw_maxdays
            : key == "racial_markup"                         ? &saw_markup
                                                             : nullptr;
        if (!seen || eq == std::string::npos) {
            problem(line_no, "unknown setting - line ignored");
            continue;
        }
        if (*seen) {
            problem(line_no, "duplicate " + key + " - line ignored");
            continue;
        }
        *seen = true;
        int number = 0;
        if (key == "hours") {
            if (!vendor_hours_parse(value, &config.hours)) {
                config.hours.clear();
                strict(line_no, "hours");
            }
        } else if (key == "fee") {
            if (!parse_number(value, &number) || number < 1 || number > BANKER_FEE_MAX)
                strict(line_no, "fee");
            else
                config.fee = number;
        } else if (key == "maxdays") {
            if (!parse_number(value, &number) || number < 1 || number > BANKER_MAXDAYS_MAX)
                strict(line_no, "maxdays");
            else
                config.maxdays = number;
        } else {
            if (value == "yes")
                config.markup = BANKER_MARKUP_DEFAULT;
            else if (!parse_number(value, &number) || number < 1 || number > BANKER_MARKUP_MAX)
                strict(line_no, "racial_markup");
            else
                config.markup = number;
        }
    }
    if (saw_fee && config.fee > 0 && !saw_maxdays) {
        config.ok = false;
        problem(0, "fee without maxdays - banker disabled");
    }
    return config;
}

int bank_side_for_race(int race)
{
    if (race >= RACE_HUMAN && race <= RACE_BEORNING)
        return BANK_SIDE_LIGHT;
    if (race == RACE_URUK || race == RACE_ORC || race == RACE_OLOGHAI)
        return BANK_SIDE_DARK;
    if (race == RACE_MAGUS || race == RACE_HARADRIM)
        return BANK_SIDE_THIRD;
    return BANK_SIDE_NONE;
}

const char* bank_side_file_name(int side)
{
    switch (side) {
    case BANK_SIDE_LIGHT:
        return "vault_light.json";
    case BANK_SIDE_DARK:
        return "vault_dark.json";
    case BANK_SIDE_THIRD:
        return "vault_third.json";
    default:
        return nullptr;
    }
}

int bank_days_stored(time_t deposited, time_t now, int start_hour)
{
    long days = bank_day_index(now, start_hour) - bank_day_index(deposited, start_hour);
    return days < 0 ? 0 : (int)days;
}

long long bank_fee(const banker_config& config, int days, int items, bool other_race)
{
    if (config.fee <= 0 || days <= 0 || items <= 0)
        return 0;
    long long fee = (long long)std::min(days, config.maxdays) * config.fee * items;
    if (other_race && config.markup > 0)
        fee = (fee * (100 + config.markup) + 99) / 100;
    return fee;
}
```

- [ ] **Step 6: Run the tests**

Run: `scripts/rots-docker.sh test --gtest_filter='Banker*:Bank*:Vendor*:BarterVendorTest.*'`
Expected: all pass. If `BankerParse.BadValuesDisableStrictly` fails on `fee=-5`, check that
`parse_number` rejects the minus sign (it must; the digit loop does).

- [ ] **Step 7: Format and commit**

```bash
cd src && clang-format -i -style=WebKit mob_progs/banker.h mob_progs/banker.cpp mob_progs/shopkeeper.h mob_progs/shopkeeper.cpp tests/banker_tests.cpp && cd ..
git diff --stat
git add src/mob_progs src/tests/banker_tests.cpp src/Makefile src/CMakeLists.txt
git commit -m "feat(bank): banker options, sides, bank days and fee maths"
```

---

### Task 3: Vault model and its JSON

**Files:**
- Modify: `src/objects_json.h`, `src/objects_json.cpp`, `src/mob_progs/banker.h`,
  `src/mob_progs/banker.cpp`, `src/tests/banker_tests.cpp`

**Interfaces:**
- Produces (objects_json.h, inside `namespace objects_json`):
  ```cpp
  void write_object_record_json(std::ostringstream& output, const ObjectRecord& record, const char* indent);
  bool parse_object_record_json(json_utils::JsonReader* reader, ObjectRecord* record, std::string* error_message);
  ```
- Produces (banker.h):
  ```cpp
  constexpr int BANK_VAULT_SCHEMA_VERSION = 1;
  struct bank_slot {
      long deposited = 0;                                   /* real time of deposit */
      std::vector<objects_json::ObjectRecord> objects;      /* [0] = the item; wear_pos = depth, 0 for it */
  };
  struct bank_vault {
      int coins = 0;                                        /* copper */
      std::vector<bank_slot> slots;
      bool readable = true;                                 /* false: file on disk could not be read */
  };
  std::string serialize_bank_vault(const bank_vault& vault);
  bool deserialize_bank_vault(const std::string& json, bank_vault* vault, std::string* error);
  ```

`ObjectRecord::wear_pos` is reused as **nesting depth**: 0 for the stored item, 1 for things
directly inside it, 2 inside those, and so on, in the order `Crash_save` walks them (an
object, then its contents, then its siblings).

- [ ] **Step 1: Write the failing tests** (append to `src/tests/banker_tests.cpp`; add
  `#include "../objects_json.h"` at the top)

```cpp
namespace {
objects_json::ObjectRecord record(int vnum, int depth)
{
    objects_json::ObjectRecord r;
    r.item_number = vnum;
    r.wear_pos = depth;
    r.values = { 1, 2, 3, 4, 5 };
    r.extra_flags = 64;
    r.weight = 30;
    r.timer = -1;
    r.bitvector = 8;
    r.loaded_by = 7;
    r.affects[0] = { 18, 6 };
    return r;
}
} // namespace

TEST(BankVaultJson, EmptyVaultRoundTrips)
{
    bank_vault vault, back;
    std::string error;
    ASSERT_TRUE(deserialize_bank_vault(serialize_bank_vault(vault), &back, &error)) << error;
    EXPECT_EQ(back.coins, 0);
    EXPECT_TRUE(back.slots.empty());
    EXPECT_TRUE(back.readable);
}

TEST(BankVaultJson, CoinsSlotsAndNestedObjectsRoundTrip)
{
    bank_vault vault;
    vault.coins = 142500;
    vault.slots.push_back({ 1790000000L, { record(100, 0) } });
    vault.slots.push_back({ 1790086400L, { record(200, 0), record(300, 1), record(400, 2), record(500, 1) } });
    bank_vault back;
    std::string error;
    ASSERT_TRUE(deserialize_bank_vault(serialize_bank_vault(vault), &back, &error)) << error;
    EXPECT_EQ(back.coins, 142500);
    ASSERT_EQ(back.slots.size(), 2u);
    EXPECT_EQ(back.slots[0].deposited, 1790000000L);
    ASSERT_EQ(back.slots[1].objects.size(), 4u);
    EXPECT_EQ(back.slots[1].objects[2].item_number, 400);
    EXPECT_EQ(back.slots[1].objects[2].wear_pos, 2);
    EXPECT_EQ(back.slots[1].objects[0].affects[0].modifier, 6);
    EXPECT_EQ(back.slots[1].objects[0].values[4], 5);
    EXPECT_EQ(back.slots[1].objects[0].bitvector, 8);
}

TEST(BankVaultJson, RejectsBrokenOrImpossibleFiles)
{
    bank_vault good;
    good.slots.push_back({ 5, { record(100, 0) } });
    std::string json = serialize_bank_vault(good);
    auto with = [&](const std::string& from, const std::string& to) {
        std::string copy = json;
        size_t at = copy.find(from);
        EXPECT_NE(at, std::string::npos) << from;
        return copy.replace(at, from.size(), to);
    };
    const std::string broken[] = {
        "",
        "{",
        "garbage",
        with("\"version\": 1", "\"version\": 2"), /* a newer format */
        with("\"coins\": 0", "\"coins\": -1"),
        with("\"wear_pos\": 0", "\"wear_pos\": 1"), /* first object must be depth 0 */
        with("\"deposited\": 5", "\"deposited\": -5"),
        "{\"version\": 1, \"coins\": 0, \"slots\": [{\"deposited\": 5, \"objects\": []}]}", /* empty slot */
        "{\"version\": 1, \"coins\": 0}", /* slots missing */
    };
    for (const std::string& text : broken) {
        bank_vault vault;
        vault.coins = 77;
        std::string error;
        EXPECT_FALSE(deserialize_bank_vault(text, &vault, &error)) << text;
        EXPECT_FALSE(error.empty()) << text;
        EXPECT_EQ(vault.coins, 77) << "a failed read must not touch the output";
    }
}

TEST(BankVaultJson, RejectsANestingJump)
{
    bank_vault vault;
    vault.slots.push_back({ 5, { record(100, 0), record(200, 2) } }); /* depth 0 -> 2 */
    bank_vault back;
    std::string error;
    EXPECT_FALSE(deserialize_bank_vault(serialize_bank_vault(vault), &back, &error));
}
```

- [ ] **Step 2: Run to see them fail**

Run: `scripts/rots-docker.sh test --gtest_filter='BankVaultJson.*'`
Expected: compile error, `bank_vault` not declared.

- [ ] **Step 3: Export the record reader and writer**

`src/objects_json.h`: add `#include "json_utils.h"` and `#include <sstream>`, and before
`first_differing_field` declare the two functions from the Interfaces block.

`src/objects_json.cpp`: after the unnamed namespace that holds `parse_object_record` and
`write_object_record` closes (line ~468), add:

```cpp
void write_object_record_json(std::ostringstream& output, const ObjectRecord& record, const char* indent)
{
    write_object_record(output, record, indent);
}

bool parse_object_record_json(json_utils::JsonReader* reader, ObjectRecord* record, std::string* error_message)
{
    return parse_object_record(reader, record, error_message);
}
```

- [ ] **Step 4: Add the model and JSON to the banker files**

`banker.h`: add `#include "../objects_json.h"` and the `bank_slot`, `bank_vault`,
`BANK_VAULT_SCHEMA_VERSION`, `serialize_bank_vault`, `deserialize_bank_vault` declarations
from the Interfaces block.

`banker.cpp`: add `#include "../json_utils.h"` and `#include <sstream>`, then:

```cpp
std::string serialize_bank_vault(const bank_vault& vault)
{
    std::ostringstream out;
    out << "{\n";
    out << "  \"version\": " << BANK_VAULT_SCHEMA_VERSION << ",\n";
    out << "  \"coins\": " << vault.coins << ",\n";
    out << "  \"slots\": [\n";
    for (size_t s = 0; s < vault.slots.size(); ++s) {
        const bank_slot& slot = vault.slots[s];
        out << "    {\n";
        out << "      \"deposited\": " << slot.deposited << ",\n";
        out << "      \"objects\": [\n";
        for (size_t o = 0; o < slot.objects.size(); ++o) {
            objects_json::write_object_record_json(out, slot.objects[o], "        ");
            out << (o + 1 < slot.objects.size() ? ",\n" : "\n");
        }
        out << "      ]\n";
        out << "    }" << (s + 1 < vault.slots.size() ? ",\n" : "\n");
    }
    out << "  ]\n";
    out << "}\n";
    return out.str();
}

bool deserialize_bank_vault(const std::string& json, bank_vault* vault, std::string* error)
{
    using json_utils::JsonReader;
    bank_vault parsed;
    int version = 0;
    bool saw_version = false, saw_coins = false, saw_slots = false;
    JsonReader reader(json);
    bool ok = reader.parse_root_object(
        [&](const std::string& key, JsonReader* r, std::string* e) {
            if (key == "version")
                return saw_version = true, r->parse_integer(&version, e);
            if (key == "coins")
                return saw_coins = true, r->parse_integer(&parsed.coins, e);
            if (key == "slots") {
                saw_slots = true;
                return r->parse_array(
                    [&parsed](JsonReader* slot_reader, std::string* slot_error) {
                        bank_slot slot;
                        bool saw_deposited = false;
                        if (!slot_reader->parse_object(
                                [&slot, &saw_deposited](const std::string& slot_key, JsonReader* sr, std::string* se) {
                                    if (slot_key == "deposited")
                                        return saw_deposited = true, sr->parse_long(&slot.deposited, se);
                                    if (slot_key == "objects")
                                        return sr->parse_array(
                                            [&slot](JsonReader* object_reader, std::string* object_error) {
                                                objects_json::ObjectRecord record;
                                                if (!objects_json::parse_object_record_json(object_reader, &record, object_error))
                                                    return false;
                                                slot.objects.push_back(record);
                                                return true;
                                            },
                                            se);
                                    return sr->skip_value(se);
                                },
                                slot_error))
                            return false;
                        if (!saw_deposited || slot.deposited < 0) {
                            *slot_error = "Vault slot has no valid deposit time.";
                            return false;
                        }
                        if (slot.objects.empty() || slot.objects[0].wear_pos != 0) {
                            *slot_error = "Vault slot has no item at depth 0.";
                            return false;
                        }
                        for (size_t i = 1; i < slot.objects.size(); ++i)
                            if (slot.objects[i].wear_pos < 1 || slot.objects[i].wear_pos > slot.objects[i - 1].wear_pos + 1) {
                                *slot_error = "Vault slot has an impossible nesting depth.";
                                return false;
                            }
                        parsed.slots.push_back(slot);
                        return true;
                    },
                    e);
            }
            return r->skip_value(e);
        },
        error);
    if (!ok) {
        if (error && error->empty())
            *error = "Vault file is not valid JSON.";
        return false;
    }
    if (!saw_version || version != BANK_VAULT_SCHEMA_VERSION) {
        *error = "Vault file has an unknown version.";
        return false;
    }
    if (!saw_coins || parsed.coins < 0 || !saw_slots) {
        *error = "Vault file is missing coins or slots.";
        return false;
    }
    *vault = parsed;
    return true;
}
```

- [ ] **Step 5: Run the tests**

Run: `scripts/rots-docker.sh test --gtest_filter='BankVaultJson.*:ObjectsJson*'`
Expected: all pass, and the existing objects JSON tests are unchanged.

If the empty-string case fails because `parse_root_object` leaves `*error` empty, the
`error->empty()` fallback above covers it; do not change `json_utils`.

- [ ] **Step 6: Format and commit**

```bash
cd src && clang-format -i -style=WebKit mob_progs/banker.h mob_progs/banker.cpp tests/banker_tests.cpp && cd ..
git diff --stat   # objects_json.{h,cpp}: only the added lines
git add src/objects_json.h src/objects_json.cpp src/mob_progs src/tests/banker_tests.cpp
git commit -m "feat(bank): vault model and JSON format"
```

---

### Task 4: The vault table (one copy in memory, atomic files)

**Files:**
- Modify: `src/mob_progs/banker.h`, `src/mob_progs/banker.cpp`, `src/tests/banker_tests.cpp`

**Interfaces:**
- Consumes: `serialize_bank_vault`, `deserialize_bank_vault`, `bank_side_file_name`.
- Produces:
  ```cpp
  /* Test hooks. Passing an empty function restores the game behaviour. */
  void bank_set_directory_resolver(std::function<std::string(const std::string& account_name)> resolver);
  void bank_set_character_saver(std::function<void(struct char_data*)> saver);
  void bank_set_clock(std::function<time_t()> clock);
  time_t bank_now();
  void bank_save_character(struct char_data* ch);

  /* The one in-memory copy. nullptr (with *error set) when the account has no
   * folder, the side is not 1-3, or the file on disk could not be read. */
  bank_vault* bank_vault_open(const std::string& account_name, int side, std::string* error);
  /* Writes the in-memory copy to its file (temp file, then rename). */
  bool bank_vault_write(const std::string& account_name, int side, std::string* error);
  void bank_vault_forget_all(); /* drops the table; tests and nothing else */
  ```

The game resolver is `account::account_character_directory(".", account_name, "")`
(`src/account_management_storage.h:34`), which returns the account's own folder
(`accounts/<bucket>/<email>`). The folder must already exist; the bank never creates one.

- [ ] **Step 1: Write the failing tests** (append to `banker_tests.cpp`; add `#include
  <fstream>`, `<sstream>`, `<sys/stat.h>`, `<unistd.h>`, `<functional>`)

```cpp
namespace {

std::string read_file(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

void write_file(const std::string& path, const std::string& text)
{
    std::ofstream out(path, std::ios::binary);
    out << text;
}

/* A temp directory standing in for the accounts tree: <root>/<account name>. */
class BankStoreTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        char path[] = "/tmp/bankstore_XXXXXX";
        ASSERT_NE(mkdtemp(path), nullptr);
        m_root = path;
        mkdir((m_root + "/tester").c_str(), 0700);
        mkdir((m_root + "/other").c_str(), 0700);
        bank_vault_forget_all();
        bank_set_directory_resolver([this](const std::string& name) {
            std::string dir = m_root + "/" + name;
            struct stat st { };
            return stat(dir.c_str(), &st) == 0 ? dir : std::string();
        });
    }
    void TearDown() override
    {
        bank_vault_forget_all();
        bank_set_directory_resolver(nullptr);
        std::string command = "rm -rf " + m_root;
        ASSERT_EQ(system(command.c_str()), 0);
    }
    std::string path(const char* account, const char* file) const { return m_root + "/" + account + "/" + file; }
    std::string m_root;
};

} // namespace

TEST_F(BankStoreTest, MissingFileIsAnEmptyVaultAndNothingIsWrittenByLooking)
{
    std::string error;
    bank_vault* vault = bank_vault_open("tester", BANK_SIDE_LIGHT, &error);
    ASSERT_NE(vault, nullptr) << error;
    EXPECT_EQ(vault->coins, 0);
    EXPECT_TRUE(vault->slots.empty());
    EXPECT_NE(access(path("tester", "vault_light.json").c_str(), F_OK), 0);
}

TEST_F(BankStoreTest, OpenTwiceIsTheSameCopy)
{
    std::string error;
    bank_vault* first = bank_vault_open("tester", BANK_SIDE_LIGHT, &error);
    first->coins = 500;
    EXPECT_EQ(bank_vault_open("tester", BANK_SIDE_LIGHT, &error), first);
    EXPECT_EQ(bank_vault_open("Tester", BANK_SIDE_LIGHT, &error), first) << "account names are case-blind";
    EXPECT_NE(bank_vault_open("tester", BANK_SIDE_DARK, &error), first);
    EXPECT_NE(bank_vault_open("other", BANK_SIDE_LIGHT, &error), first);
}

TEST_F(BankStoreTest, WriteThenForgetThenOpenReadsTheFile)
{
    std::string error;
    bank_vault* vault = bank_vault_open("tester", BANK_SIDE_DARK, &error);
    vault->coins = 1234;
    vault->slots.push_back({ 99, { record(100, 0) } });
    ASSERT_TRUE(bank_vault_write("tester", BANK_SIDE_DARK, &error)) << error;
    EXPECT_NE(access(path("tester", "vault_dark.json.tmp").c_str(), F_OK), 0) << "no temp file left";
    bank_vault_forget_all();
    vault = bank_vault_open("tester", BANK_SIDE_DARK, &error);
    ASSERT_NE(vault, nullptr) << error;
    EXPECT_EQ(vault->coins, 1234);
    ASSERT_EQ(vault->slots.size(), 1u);
    EXPECT_EQ(vault->slots[0].objects[0].item_number, 100);
}

TEST_F(BankStoreTest, UnreadableFileIsRefusedAndNeverOverwritten)
{
    write_file(path("tester", "vault_light.json"), "{ this is not a vault");
    std::string error;
    EXPECT_EQ(bank_vault_open("tester", BANK_SIDE_LIGHT, &error), nullptr);
    EXPECT_FALSE(error.empty());
    EXPECT_EQ(bank_vault_open("tester", BANK_SIDE_LIGHT, &error), nullptr) << "stays refused";
    EXPECT_FALSE(bank_vault_write("tester", BANK_SIDE_LIGHT, &error));
    EXPECT_EQ(read_file(path("tester", "vault_light.json")), "{ this is not a vault");
    EXPECT_NE(bank_vault_open("tester", BANK_SIDE_DARK, &error), nullptr) << "other vaults still work";
}

TEST_F(BankStoreTest, NoAccountFolderNoSideNoName)
{
    std::string error;
    EXPECT_EQ(bank_vault_open("nobody", BANK_SIDE_LIGHT, &error), nullptr);
    EXPECT_EQ(bank_vault_open("tester", BANK_SIDE_NONE, &error), nullptr);
    EXPECT_EQ(bank_vault_open("tester", 4, &error), nullptr);
    EXPECT_EQ(bank_vault_open("", BANK_SIDE_LIGHT, &error), nullptr);
    EXPECT_FALSE(bank_vault_write("nobody", BANK_SIDE_LIGHT, &error));
}

TEST_F(BankStoreTest, FailedWriteLeavesTheOldFile)
{
    std::string error;
    bank_vault* vault = bank_vault_open("tester", BANK_SIDE_LIGHT, &error);
    vault->coins = 10;
    ASSERT_TRUE(bank_vault_write("tester", BANK_SIDE_LIGHT, &error));
    std::string before = read_file(path("tester", "vault_light.json"));
    vault->coins = 20;
    /* a directory where the temp file must go makes the write fail */
    mkdir(path("tester", "vault_light.json.tmp").c_str(), 0700);
    EXPECT_FALSE(bank_vault_write("tester", BANK_SIDE_LIGHT, &error));
    EXPECT_EQ(read_file(path("tester", "vault_light.json")), before);
}

TEST(BankHooks, ClockAndSaverCanBeReplacedAndRestored)
{
    bank_set_clock([] { return (time_t)12345; });
    EXPECT_EQ(bank_now(), 12345);
    bank_set_clock(nullptr);
    EXPECT_GT(bank_now(), 1700000000);

    int saved = 0;
    bank_set_character_saver([&saved](struct char_data*) { ++saved; });
    bank_save_character(nullptr);
    EXPECT_EQ(saved, 1);
    bank_set_character_saver(nullptr);
}
```

- [ ] **Step 2: Run to see them fail**

Run: `scripts/rots-docker.sh test --gtest_filter='BankStoreTest.*:BankHooks.*'`
Expected: compile error, `bank_vault_open` not declared.

- [ ] **Step 3: Declare in `banker.h`** the functions from the Interfaces block (add
  `#include <functional>` and forward-declare `struct char_data;`).

- [ ] **Step 4: Implement in `banker.cpp`**

Add includes: `"../account_management_identity.h"`, `"../account_management_storage.h"`,
`"../db.h"`, `"../handler.h"`, `"../utils.h"`, `<cerrno>`, `<cstdio>`, `<cstring>`,
`<fcntl.h>`, `<fstream>`, `<map>`, `<sys/stat.h>`, `<unistd.h>`.

```cpp
namespace {

std::function<std::string(const std::string&)> g_directory_resolver;
std::function<void(struct char_data*)> g_character_saver;
std::function<time_t()> g_clock;

/* Keyed "<normalized account name>#<side>". The ONLY copy of each vault. */
std::map<std::string, bank_vault> g_vaults;

std::string game_account_directory(const std::string& account_name)
{
    std::string dir = account::account_character_directory(".", account_name, "");
    struct stat st { };
    if (dir.empty() || stat(dir.c_str(), &st) != 0 || !S_ISDIR(st.st_mode))
        return "";
    return dir;
}

bool vault_location(const std::string& account_name, int side, std::string* key, std::string* path, std::string* error)
{
    const char* file = bank_side_file_name(side);
    std::string name = account::normalize_account_name(account_name);
    if (!file || name.empty()) {
        *error = "No vault for that account and side.";
        return false;
    }
    std::string dir = g_directory_resolver ? g_directory_resolver(name) : game_account_directory(name);
    if (dir.empty()) {
        *error = "That account has no folder.";
        return false;
    }
    *key = name + "#" + std::to_string(side);
    *path = dir + "/" + file;
    return true;
}

} // namespace

void bank_set_directory_resolver(std::function<std::string(const std::string&)> resolver) { g_directory_resolver = resolver; }
void bank_set_character_saver(std::function<void(struct char_data*)> saver) { g_character_saver = saver; }
void bank_set_clock(std::function<time_t()> clock) { g_clock = clock; }
time_t bank_now() { return g_clock ? g_clock() : time(0); }

void bank_save_character(struct char_data* ch)
{
    if (g_character_saver) {
        g_character_saver(ch);
        return;
    }
    save_char(ch, NOWHERE, 0); /* the same pair do_save runs (act_othe.cpp) */
    Crash_crashsave(ch);
}

void bank_vault_forget_all() { g_vaults.clear(); }

bank_vault* bank_vault_open(const std::string& account_name, int side, std::string* error)
{
    std::string key, path;
    if (!vault_location(account_name, side, &key, &path, error))
        return nullptr;
    auto found = g_vaults.find(key);
    if (found == g_vaults.end()) {
        bank_vault vault;
        std::ifstream in(path, std::ios::binary);
        if (in.good()) {
            std::ostringstream buffer;
            buffer << in.rdbuf();
            std::string read_error;
            if (!deserialize_bank_vault(buffer.str(), &vault, &read_error)) {
                vault = bank_vault();
                vault.readable = false;
                char line[512];
                snprintf(line, sizeof(line), "SYSERR: bank: unreadable vault file %s: %s", path.c_str(), read_error.c_str());
                log(line); /* once: the refused copy stays in the table */
            }
        }
        found = g_vaults.emplace(key, vault).first;
    }
    if (!found->second.readable) {
        *error = "That vault's file can't be read.";
        return nullptr;
    }
    return &found->second;
}

bool bank_vault_write(const std::string& account_name, int side, std::string* error)
{
    std::string key, path;
    if (!vault_location(account_name, side, &key, &path, error))
        return false;
    auto found = g_vaults.find(key);
    if (found == g_vaults.end() || !found->second.readable) {
        *error = "That vault is not open.";
        return false;
    }
    const std::string json = serialize_bank_vault(found->second);
    const std::string temp = path + ".tmp";
    int fd = open(temp.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    FILE* file = fd >= 0 ? fdopen(fd, "w") : nullptr;
    if (!file) {
        if (fd >= 0)
            close(fd);
        *error = std::string("Can't write the vault file: ") + strerror(errno);
        return false;
    }
    size_t written = fwrite(json.data(), 1, json.size(), file);
    if (fclose(file) != 0 || written != json.size() || rename(temp.c_str(), path.c_str()) != 0) {
        *error = std::string("Can't write the vault file: ") + strerror(errno);
        remove(temp.c_str());
        return false;
    }
    return true;
}
```

- [ ] **Step 5: Run the tests**

Run: `scripts/rots-docker.sh test --gtest_filter='BankStoreTest.*:BankHooks.*'`
Expected: 7 pass.

- [ ] **Step 6: Check nothing else in the account code trips over vault files**

Run: `scripts/rots-docker.sh test --gtest_filter='Account*:InterpreAccountMenu*:RosterCache*'`
Expected: same results as before this task. Then read
`src/account_index.cpp` and `src/account_management.cpp` for directory scans
(`grep -n "readdir\|opendir" src/account_*.cpp`) and confirm each scan either matches by exact
file name or ignores names it doesn't know. If one would choke on `vault_*.json`, stop and
report it instead of working around it.

- [ ] **Step 7: Format and commit**

```bash
cd src && clang-format -i -style=WebKit mob_progs/banker.h mob_progs/banker.cpp tests/banker_tests.cpp && cd ..
git add src/mob_progs src/tests/banker_tests.cpp
git commit -m "feat(bank): vault table with atomic per-side files"
```

---

### Task 5: The banker program: registry, refusals, protection, `balance`

**Files:**
- Modify: `src/mob_progs/banker.h`, `src/mob_progs/banker.cpp`, `src/tests/banker_tests.cpp`,
  `src/interpre.h`, `src/spec_ass.cpp`, `src/db.cpp`, `src/shapemob.cpp`

**Interfaces:**
- Consumes: Tasks 1-4, and from shopkeeper.h `vendor_say`, `vendor_serves_customer`,
  `vendor_hours_open`, `give_targets`, `vendor_send`, `vendor_problem_line`.
- Produces:
  ```cpp
  #define CMD_BALANCE 138   /* interpre.h */
  #define CMD_DEPOSIT 139
  #define CMD_WITHDRAW 140

  int banker(struct char_data* host, struct char_data* ch, int cmd, char* arg, int callflag, struct waiting_type* wtl);
  void banker_config_boot();
  void banker_config_rebuild(int mob_rnum, struct char_data* builder, bool report = true);
  bool is_banker_candidate(const struct char_data* proto, int rnum);
  void banker_config_check(const struct char_data* proto, int mob_vnum, struct char_data* builder);
  void banker_implement_check(int mob_rnum, struct char_data* builder);
  const banker_config* banker_config_for(int mob_rnum);

  /* Stored-object helpers (used by Tasks 6 and 7). */
  void bank_records_from_obj(struct obj_data* obj, std::vector<objects_json::ObjectRecord>* out);
  struct obj_data* bank_obj_from_records(const std::vector<objects_json::ObjectRecord>& records);
  bool bank_obj_storable(struct obj_data* obj);     /* false if it or anything inside is unrentable */
  const char* bank_slot_name(const bank_slot& slot); /* short description of the stored item */

  struct bank_balance_row { std::string name; int inside; std::string fee; }; /* inside < 0: not a container */
  std::string format_bank_balance(const std::string& coins, int coin_limit_gold, int slots_used, int slots_max,
      const std::vector<bank_balance_row>& rows, bool show_fee);
  ```

- [ ] **Step 1: Write the failing formatter tests** (append to `banker_tests.cpp`)

```cpp
TEST(BankBalance, LayoutWithFees)
{
    std::vector<bank_balance_row> rows = {
        { "a bastard sword", -1, "1 silver and 50 copper" },
        { "a leather backpack", 3, "6 silver" },
        { "a crisp ticket", -1, "free" },
    };
    EXPECT_EQ(format_bank_balance("142 gold and 5 silver", 1000, 3, 10, rows, true),
        "Coins: 142 gold and 5 silver (limit 1000 gold)\n\r"
        "Slots: 3 of 10 used\n\r"
        "\n\r"
        " #  Item                                   Fee to withdraw\n\r"
        " 1  a bastard sword                        1 silver and 50 copper\n\r"
        " 2  a leather backpack (sealed, 3 inside)  6 silver\n\r"
        " 3  a crisp ticket                         free\n\r");
}

TEST(BankBalance, NoFeeColumnAtAFreeBankerAndNoTableWhenEmpty)
{
    std::vector<bank_balance_row> rows = { { "a bastard sword", -1, "" } };
    EXPECT_EQ(format_bank_balance("0 copper", 1000, 1, 10, rows, false),
        "Coins: 0 copper (limit 1000 gold)\n\r"
        "Slots: 1 of 10 used\n\r"
        "\n\r"
        " #  Item\n\r"
        " 1  a bastard sword\n\r");
    EXPECT_EQ(format_bank_balance("0 copper", 1000, 0, 10, {}, true),
        "Coins: 0 copper (limit 1000 gold)\n\r"
        "Slots: 0 of 10 used\n\r");
}

TEST(BankBalance, EveryLineFitsIn78Columns)
{
    std::vector<bank_balance_row> rows;
    for (int i = 0; i < 100; ++i)
        rows.push_back({ std::string(80, 'x'), 250, "100000 gold, 9 silver and 99 copper" });
    std::string out = format_bank_balance("100000 gold, 9 silver and 99 copper", 100000, 100, 100, rows, true);
    size_t start = 0;
    while (start < out.size()) {
        size_t end = out.find("\n\r", start);
        ASSERT_NE(end, std::string::npos);
        EXPECT_LE(end - start, 78u) << out.substr(start, end - start);
        start = end + 2;
    }
    EXPECT_NE(out.find("100  "), std::string::npos);
    EXPECT_NE(out.find("(sealed, 250 inside)"), std::string::npos) << "the sealed note survives a cut name";
}
```

- [ ] **Step 2: Write the failing program tests** (append). The fixture mirrors
  `BarterVendorTest` in `src/tests/barter_vendor_tests.cpp:289-470`; read that fixture first
  and copy its save/restore of the world globals exactly. Differences are listed here.

```cpp
namespace {

constexpr int kSwordVnum = 100;
constexpr int kPackVnum = 200;
constexpr int kKeyVnum = 300;
constexpr int kBankerVnum = 7100;

class BankerTest : public BankStoreTest {
protected:
    void SetUp() override
    {
        BankStoreTest::SetUp();
        /* --- world globals: save and replace, exactly as BarterVendorTest::SetUp does for
         * mob_proto, mob_index, top_of_mobt, obj_proto, obj_index, top_of_objt, object_list,
         * top_of_world, descriptor_list, world[0].{number,light,contents,people} --- */
        save_world();

        for (obj_data& proto : m_obj_proto)
            clear_object(&proto);
        set_proto(0, kSwordVnum, m_sword_name, m_sword_short, ITEM_WEAPON, 30);
        set_proto(1, kPackVnum, m_pack_name, m_pack_short, ITEM_CONTAINER, 10);
        set_proto(2, kKeyVnum, m_key_name, m_key_short, ITEM_KEY, 1);
        obj_proto = m_obj_proto;
        obj_index = m_obj_index;
        top_of_objt = 2;

        m_mob_proto[0].specials2.act = MOB_ISNPC | MOB_SPEC;
        m_mob_proto[0].specials.store_prog_number = PROG_BANKER;
        m_mob_proto[0].specials.mob_options = m_options;
        m_mob_proto[0].abilities.intel = 12;
        m_mob_proto[0].player.race = RACE_HUMAN;
        m_mob_index[0].virt = kBankerVnum;
        m_mob_index[0].func = nullptr;
        mob_proto = m_mob_proto;
        mob_index = m_mob_index;
        top_of_mobt = 0;
        banker_config_rebuild(0, nullptr);

        clear_char(&m_banker, MOB_ISNPC);
        m_banker.nr = 0;
        m_banker.specials2.act = MOB_ISNPC | MOB_SPEC;
        m_banker.player.name = m_banker_name;
        m_banker.player.short_descr = m_banker_short;
        m_banker.player.race = RACE_HUMAN;
        m_banker.tmpabilities.intel = 12;
        m_banker.in_room = 0;

        clear_char(&m_player, 0);
        m_player.player.name = m_player_name;
        m_player.player.race = RACE_HUMAN;
        m_player.player.level = 10;
        m_player.tmpabilities.str = 18;
        m_player.tmpabilities.dex = 18;
        m_player.in_room = 0;
        m_descriptor.output = m_descriptor.small_outbuf;
        m_descriptor.small_outbuf[0] = '\0';
        m_descriptor.bufspace = SMALL_BUFSIZE - 1;
        m_descriptor.connected = CON_PLYNG;
        m_descriptor.character = &m_player;
        std::strcpy(m_descriptor.account_name, "tester");
        m_descriptor.descriptor = 1; /* do_say only speaks to a connected, awake listener */
        GET_POS(&m_player) = POSITION_STANDING;
        m_player.desc = &m_descriptor;

        world[0].people = &m_banker;
        m_banker.next_in_room = &m_player;
        m_player.next_in_room = nullptr;

        m_now = at(2026, 9, 30, 12);
        bank_set_clock([this] { return m_now; });
        bank_set_character_saver([this](char_data* ch) {
            ++m_saves;
            m_file_at_save = read_file(path("tester", "vault_light.json"));
            (void)ch;
        });
        boot_options_set_running_for_tests(BOOT_BANK_SLOTS, 10);
        boot_options_set_running_for_tests(BOOT_BANK_COIN_LIMIT_GOLD, 1000);
        boot_options_set_running_for_tests(BOOT_BANK_DAY_START_HOUR, 5);
    }

    void TearDown() override
    {
        while (object_list != nullptr)
            extract_obj(object_list);
        m_mob_proto[0].specials.store_prog_number = 0;
        banker_config_rebuild(0, nullptr); /* erases the registry entry */
        bank_set_clock(nullptr);
        bank_set_character_saver(nullptr);
        restore_world();
        BankStoreTest::TearDown();
    }

    void set_proto(int rnum, int vnum, char* name, char* short_desc, int type, int weight)
    {
        m_obj_proto[rnum].item_number = rnum;
        m_obj_proto[rnum].name = name;
        m_obj_proto[rnum].short_description = short_desc;
        m_obj_proto[rnum].obj_flags.type_flag = type;
        m_obj_proto[rnum].obj_flags.weight = weight;
        m_obj_index[rnum].virt = vnum;
    }

    void options(const char* text)
    {
        std::strncpy(m_options, text, sizeof(m_options) - 1);
        banker_config_rebuild(0, nullptr);
    }

    obj_data* give(int rnum)
    {
        obj_data* obj = read_object(rnum, REAL);
        obj_to_char(obj, &m_player);
        return obj;
    }

    int call(int cmd, const char* text, int callflag = SPECIAL_COMMAND)
    {
        std::strncpy(m_arg, text, sizeof(m_arg) - 1);
        m_arg[sizeof(m_arg) - 1] = '\0';
        clear_output();
        return banker(&m_banker, &m_player, cmd, m_arg, callflag, nullptr);
    }

    bank_vault* vault(int side = BANK_SIDE_LIGHT)
    {
        std::string error;
        return bank_vault_open("tester", side, &error);
    }

    int carried(int rnum) const
    {
        int count = 0;
        for (obj_data* obj = m_player.carrying; obj; obj = obj->next_content)
            if (obj->item_number == rnum)
                ++count;
        return count;
    }
    std::string output() const { return std::string(m_descriptor.output); }
    void clear_output()
    {
        m_descriptor.small_outbuf[0] = '\0';
        m_descriptor.bufptr = 0;
        m_descriptor.bufspace = SMALL_BUFSIZE - 1;
    }

    /* save_world()/restore_world(): the m_saved_* members and code of BarterVendorTest. */
    void save_world();
    void restore_world();

    char m_options[256] = "";
    char m_sword_name[16] = "sword bastard";
    char m_sword_short[20] = "a bastard sword";
    char m_pack_name[20] = "backpack leather";
    char m_pack_short[24] = "a leather backpack";
    char m_key_name[16] = "key iron";
    char m_key_short[16] = "an iron key";
    char m_banker_name[16] = "banker griswold";
    char m_banker_short[16] = "the banker";
    char m_player_name[16] = "Player";
    char m_arg[MAX_INPUT_LENGTH] = "";
    obj_data m_obj_proto[3] {};
    index_data m_obj_index[3] {};
    char_data m_mob_proto[1] {};
    index_data m_mob_index[1] {};
    char_data m_banker {};
    char_data m_player {};
    descriptor_data m_descriptor {};
    time_t m_now = 0;
    int m_saves = 0;
    std::string m_file_at_save;
    /* plus the m_saved_* members copied from BarterVendorTest */
};

} // namespace

TEST_F(BankerTest, OnlyARegisteredBankerMobIsABanker)
{
    EXPECT_EQ(banker(&m_player, &m_player, CMD_BALANCE, m_arg, SPECIAL_COMMAND, nullptr), FALSE);
    m_mob_proto[0].specials.store_prog_number = 0;
    banker_config_rebuild(0, nullptr);
    EXPECT_EQ(call(CMD_BALANCE, ""), FALSE);
}

TEST_F(BankerTest, OtherCommandsPassThrough)
{
    EXPECT_EQ(call(CMD_LIST, ""), FALSE);
    EXPECT_EQ(call(CMD_BUY, "sword"), FALSE);
}

TEST_F(BankerTest, DamageDustAndGiftsAreRefusedLikeAVendor)
{
    EXPECT_EQ(call(0, "", SPECIAL_DAMAGE), TRUE);
    EXPECT_EQ(call(CMD_GIVE, "sword banker"), TRUE);
    EXPECT_NE(output().find("I don't take gifts."), std::string::npos);
    waiting_type wtl {};
    wtl.targ1.type = TARGET_CHAR;
    wtl.targ1.ptr.ch = &m_banker;
    EXPECT_EQ(banker(&m_banker, &m_player, CMD_BLINDING, m_arg, SPECIAL_TARGET, &wtl), TRUE);
}

TEST_F(BankerTest, BalanceOnAnEmptyVault)
{
    EXPECT_EQ(call(CMD_BALANCE, ""), TRUE);
    EXPECT_NE(output().find("Coins: 0 copper (limit 1000 gold)"), std::string::npos) << output();
    EXPECT_NE(output().find("Slots: 0 of 10 used"), std::string::npos);
    EXPECT_EQ(m_saves, 0) << "looking saves nothing";
}

TEST_F(BankerTest, ImmortalsAndSidelessRacesAreRefused)
{
    for (int race : { (int)RACE_GOD, (int)RACE_EASTERLING }) {
        m_player.player.race = race;
        EXPECT_EQ(call(CMD_BALANCE, ""), TRUE);
        EXPECT_NE(output().find("I hold nothing for your kind."), std::string::npos) << output();
        EXPECT_EQ(output().find("Coins:"), std::string::npos);
    }
}

TEST_F(BankerTest, NoAccountNameOrNoDescriptorIsRefusedWithoutOpeningAVault)
{
    m_descriptor.account_name[0] = '\0';
    EXPECT_EQ(call(CMD_BALANCE, ""), TRUE);
    EXPECT_NE(output().find("I can't find your account."), std::string::npos) << output();
    m_player.desc = nullptr;
    EXPECT_EQ(call(CMD_DEPOSIT, "5 gold"), TRUE); /* must not crash */
    m_player.desc = &m_descriptor;
}

TEST_F(BankerTest, ClosedBankerRefuses)
{
    extern struct time_info_data time_info;
    int saved_hour = time_info.hours;
    options("hours=6-20");
    time_info.hours = 22;
    EXPECT_EQ(call(CMD_BALANCE, ""), TRUE);
    EXPECT_NE(output().find("I'm closed. Come back later."), std::string::npos);
    time_info.hours = saved_hour;
}

TEST_F(BankerTest, BadOptionsMeanNoBusiness)
{
    options("fee=5");
    EXPECT_EQ(call(CMD_BALANCE, ""), TRUE);
    EXPECT_NE(output().find("The bank is closed for now."), std::string::npos) << output();
}

TEST_F(BankerTest, UnreadableVaultIsRefusedAndLeftAlone)
{
    write_file(path("tester", "vault_light.json"), "junk");
    EXPECT_EQ(call(CMD_BALANCE, ""), TRUE);
    EXPECT_NE(output().find("I can't open your vault right now."), std::string::npos) << output();
    EXPECT_EQ(read_file(path("tester", "vault_light.json")), "junk");
}

TEST_F(BankerTest, CandidateRule)
{
    EXPECT_TRUE(is_banker_candidate(&m_mob_proto[0], 0));
    m_mob_proto[0].specials2.act = MOB_ISNPC; /* no MOB_SPEC */
    EXPECT_FALSE(is_banker_candidate(&m_mob_proto[0], 0));
    m_mob_proto[0].specials2.act = MOB_ISNPC | MOB_SPEC;
    m_mob_proto[0].specials.store_prog_number = PROG_BARTER_VENDOR;
    EXPECT_FALSE(is_banker_candidate(&m_mob_proto[0], 0));
}

TEST_F(BankerTest, ObjectRecordsRoundTripANestedContainer)
{
    obj_data* pack = read_object(1, REAL);
    obj_data* inner = read_object(1, REAL);
    obj_to_obj(read_object(0, REAL), inner);
    obj_to_obj(inner, pack);
    obj_to_obj(read_object(0, REAL), pack);
    std::vector<objects_json::ObjectRecord> records;
    bank_records_from_obj(pack, &records);
    ASSERT_EQ(records.size(), 4u);
    EXPECT_EQ(records[0].item_number, kPackVnum);
    EXPECT_EQ(records[0].wear_pos, 0);
    extract_obj(pack);
    ASSERT_EQ(object_list, nullptr);

    obj_data* back = bank_obj_from_records(records);
    ASSERT_NE(back, nullptr);
    EXPECT_EQ(back->item_number, 1);
    int direct = 0, swords_inside_inner = 0;
    for (obj_data* o = back->contains; o; o = o->next_content) {
        ++direct;
        if (o->item_number == 1)
            for (obj_data* p = o->contains; p; p = p->next_content)
                swords_inside_inner += p->item_number == 0;
    }
    EXPECT_EQ(direct, 2);
    EXPECT_EQ(swords_inside_inner, 1);
    EXPECT_EQ(GET_OBJ_WEIGHT(back), 10 + 10 + 30 + 30) << "a container weighs itself plus contents";
    extract_obj(back);
}

TEST_F(BankerTest, AStoredItemWhosePrototypeIsGoneBuildsNothing)
{
    std::vector<objects_json::ObjectRecord> records = { record(kPackVnum, 0), record(9999, 1) };
    EXPECT_EQ(bank_obj_from_records(records), nullptr);
    EXPECT_EQ(object_list, nullptr) << "no half-built objects left behind";
}

TEST_F(BankerTest, StorableFollowsRentIncludingContents)
{
    obj_data* pack = read_object(1, REAL);
    EXPECT_TRUE(bank_obj_storable(pack));
    obj_to_obj(read_object(2, REAL), pack); /* a key: rent refuses keys */
    EXPECT_FALSE(bank_obj_storable(pack));
    extract_obj(pack);
}
```

Write `BankerTest::save_world()` and `restore_world()` as the save and restore halves of
`BarterVendorTest::SetUp` / `TearDown` (same globals, same order, same `create_bulk(1)`
guard). Add `#include "../comm.h"`, `"../db.h"`, `"../handler.h"`, `"../interpre.h"`,
`"../utils.h"`, `"../game_boot_options.h"`, `<cstring>` to the test file.

- [ ] **Step 3: Run to see them fail**

Run: `scripts/rots-docker.sh test --gtest_filter='BankBalance.*:BankerTest.*'`
Expected: compile error, `banker` / `format_bank_balance` not declared.

- [ ] **Step 4: Command numbers**

`src/interpre.h`, beside the other `CMD_` defines:

```cpp
#define CMD_BALANCE 138
#define CMD_DEPOSIT 139
#define CMD_WITHDRAW 140
```

- [ ] **Step 5: Declare in `banker.h`** everything in this task's Interfaces block (forward
  declare `struct obj_data;` and `struct waiting_type;`).

- [ ] **Step 6: Implement the formatter and the object helpers in `banker.cpp`**

Add includes `"../comm.h"`, `"../interpre.h"`, `"../game_boot_options.h"`, `<set>`,
`<unordered_map>`, and these externs near the top:

```cpp
extern struct char_data* mob_proto;
extern struct index_data* mob_index;
extern int top_of_mobt;
extern struct obj_data* obj_proto;
extern struct index_data* obj_index;
extern struct time_info_data time_info;
extern int no_specials;
extern int generic_scalp;

int Crash_is_unrentable(struct obj_data* obj);
struct obj_data* Crash_obj2char(struct char_data* ch, struct obj_file_elem* object);
```

(If `generic_scalp` is declared with a different type, match the declaration in
`src/objsave.cpp`.)

```cpp
constexpr size_t BANK_NAME_COLUMN = 37; /* " #  " + 37 + 2 + a 35-column fee = 78 */

std::string format_bank_balance(const std::string& coins, int coin_limit_gold, int slots_used, int slots_max,
    const std::vector<bank_balance_row>& rows, bool show_fee)
{
    std::string out = "Coins: " + coins + " (limit " + std::to_string(coin_limit_gold) + " gold)\n\r";
    out += "Slots: " + std::to_string(slots_used) + " of " + std::to_string(slots_max) + " used\n\r";
    if (rows.empty())
        return out;
    out += "\n\r";
    const size_t fee_column = 4 + BANK_NAME_COLUMN + 2; /* where the fee text starts */
    std::string header = " #  Item";
    if (show_fee) {
        header.append(fee_column - header.size(), ' ');
        header += "Fee to withdraw";
    }
    out += header + "\n\r";
    for (size_t i = 0; i < rows.size(); ++i) {
        char number[8];
        snprintf(number, sizeof(number), "%2d  ", (int)(i + 1));
        std::string line = number; /* 100 and up is one wider: the name gives way */
        size_t width = fee_column - 2 - line.size();
        std::string note = rows[i].inside >= 0 ? " (sealed, " + std::to_string(rows[i].inside) + " inside)" : "";
        std::string name = rows[i].name;
        if (name.size() + note.size() > width)
            name.resize(width - note.size()); /* the note always survives */
        line += name + note;
        if (show_fee) {
            line.append(fee_column - line.size(), ' ');
            line += rows[i].fee;
        }
        out += line + "\n\r";
    }
    return out;
}

bool bank_obj_storable(struct obj_data* obj)
{
    if (Crash_is_unrentable(obj))
        return false;
    for (struct obj_data* inside = obj->contains; inside; inside = inside->next_content)
        if (!bank_obj_storable(inside))
            return false;
    return true;
}

namespace {

/* Mirrors Crash_obj2store (objsave.cpp), with nesting depth in wear_pos. */
void append_record(struct obj_data* obj, int depth, std::vector<objects_json::ObjectRecord>* out)
{
    objects_json::ObjectRecord record;
    record.item_number = obj->item_number >= 0 ? obj_index[obj->item_number].virt : obj->item_number;
    for (int i = 0; i < 5; ++i)
        record.values[i] = obj->obj_flags.value[i];
    record.extra_flags = obj->obj_flags.extra_flags;
    record.weight = obj->obj_flags.weight;
    record.timer = obj->obj_flags.timer;
    record.bitvector = obj->obj_flags.bitvector;
    record.loaded_by = obj->loaded_by;
    for (int i = 0; i < MAX_OBJ_AFFECT; ++i)
        record.affects[i] = { obj->affected[i].location, obj->affected[i].modifier };
    record.wear_pos = depth;
    if (record.item_number == generic_scalp) /* same stash Crash_obj2store uses */
        record.extra_flags = obj->obj_flags.value[4];
    out->push_back(record);
    for (struct obj_data* inside = obj->contains; inside; inside = inside->next_content)
        append_record(inside, depth + 1, out);
}

} // namespace

void bank_records_from_obj(struct obj_data* obj, std::vector<objects_json::ObjectRecord>* out)
{
    append_record(obj, 0, out);
}

struct obj_data* bank_obj_from_records(const std::vector<objects_json::ObjectRecord>& records)
{
    std::vector<struct obj_data*> at_depth;
    for (const objects_json::ObjectRecord& record : records) {
        struct obj_file_elem elem { };
        elem.item_number = record.item_number;
        for (int i = 0; i < 5; ++i)
            elem.value[i] = (sh_int)record.values[i];
        elem.extra_flags = record.extra_flags;
        elem.weight = record.weight;
        elem.timer = record.timer;
        elem.bitvector = record.bitvector;
        elem.loaded_by = record.loaded_by;
        for (int i = 0; i < MAX_OBJ_AFFECT; ++i) {
            elem.affected[i].location = record.affects[i].location;
            elem.affected[i].modifier = record.affects[i].modifier;
        }
        int depth = record.wear_pos;
        struct obj_data* obj = depth >= 0 && depth <= (int)at_depth.size() && (depth == 0) == at_depth.empty()
            ? Crash_obj2char(nullptr, &elem)
            : nullptr;
        if (!obj) { /* prototype gone, or impossible nesting: build nothing */
            if (!at_depth.empty())
                extract_obj(at_depth[0]); /* extract_obj takes the contents with it */
            return nullptr;
        }
        obj->touched = 1;
        /* PR #343: once object versions are merged, refresh `obj` here, the
         * same call Crash_load makes after Crash_obj2char. */
        if (depth > 0)
            obj_to_obj(obj, at_depth[depth - 1], TRUE);
        at_depth.resize(depth);
        at_depth.push_back(obj);
    }
    return at_depth.empty() ? nullptr : at_depth[0];
}

const char* bank_slot_name(const bank_slot& slot)
{
    int rnum = slot.objects.empty() ? -1 : real_object(slot.objects[0].item_number);
    return rnum >= 0 ? obj_proto[rnum].short_description : "something";
}
```

Check the field names against `struct obj_affected_type` and `struct obj_file_elem`
(`src/structs.h:1958`) and against `Crash_obj2store` (`src/objsave.cpp:854`) before
compiling; they must copy the same fields.

- [ ] **Step 7: Implement the registry and the program in `banker.cpp`**

```cpp
namespace {

std::unordered_map<int, banker_config> g_banker_configs; /* by mob rnum */
std::set<int> g_banker_disabled_logged; /* by mob rnum; cleared on rebuild */

bool is_banker_proto(int rnum) { return is_banker_candidate(&mob_proto[rnum], rnum); }

void add_speech_problem(const char_data& proto, std::vector<vendor_problem>* problems)
{
    if (proto.abilities.intel < 6)
        problems->push_back({ 0, "intelligence below 6 - banker can't speak" });
}

void add_pref_problem(const char_data& proto, std::vector<vendor_problem>* problems)
{
    if (proto.specials2.pref != 0)
        problems->push_back({ 0, "pref set - banker attacks and can be hurt" });
}

struct bank_customer {
    std::string account;
    int side;
    bank_vault* vault;
};

/* Everything a banker checks before any business. Says why when refusing. */
bool banker_admits(struct char_data* host, struct char_data* ch, const banker_config& config, bank_customer* customer)
{
    if (!config.ok) {
        if (g_banker_disabled_logged.insert(host->nr).second)
            vendor_send(vendor_problem_line(host->nr >= 0 ? mob_index[host->nr].virt : -1,
                            { 0, "bad options - banker disabled" }),
                nullptr);
        vendor_say(host, "The bank is closed for now.");
        return false;
    }
    if (!vendor_serves_customer(host, ch))
        return false;
    if (!vendor_hours_open(config.hours, time_info.hours)) {
        vendor_say(host, "I'm closed. Come back later.");
        return false;
    }
    customer->side = IS_NPC(ch) ? BANK_SIDE_NONE : bank_side_for_race(GET_RACE(ch));
    if (customer->side == BANK_SIDE_NONE) {
        vendor_say(host, "I hold nothing for your kind.");
        return false;
    }
    if (!ch->desc || !*ch->desc->account_name) {
        vendor_say(host, "I can't find your account.");
        return false;
    }
    customer->account = ch->desc->account_name;
    std::string error;
    customer->vault = bank_vault_open(customer->account, customer->side, &error);
    if (!customer->vault) {
        vendor_say(host, "I can't open your vault right now.");
        return false;
    }
    return true;
}

long long slot_fee(const banker_config& config, const bank_slot& slot, struct char_data* host, struct char_data* ch)
{
    int days = bank_days_stored(slot.deposited, bank_now(), boot_option(BOOT_BANK_DAY_START_HOUR));
    return bank_fee(config, days, (int)slot.objects.size(), GET_RACE(ch) != GET_RACE(host));
}

void banker_balance(struct char_data* host, struct char_data* ch, const banker_config& config, const bank_customer& customer)
{
    std::vector<bank_balance_row> rows;
    for (const bank_slot& slot : customer.vault->slots) {
        long long fee = slot_fee(config, slot, host, ch);
        rows.push_back({ bank_slot_name(slot), slot.objects.size() > 1 ? (int)slot.objects.size() - 1 : -1,
            fee > 0 ? money_message((int)std::min<long long>(fee, 2000000000LL), 0) : "free" });
    }
    vendor_say(host, "Here is your vault.");
    std::string coins = money_message(customer.vault->coins, 0);
    send_to_char(format_bank_balance(coins, boot_option(BOOT_BANK_COIN_LIMIT_GOLD), (int)customer.vault->slots.size(),
                     boot_option(BOOT_BANK_SLOTS), rows, config.fee > 0)
                     .c_str(),
        ch);
}

} // namespace

bool is_banker_candidate(const struct char_data* proto, int rnum)
{
    if (no_specials)
        return false;
    if (!IS_SET(proto->specials2.act, MOB_SPEC) || proto->specials.store_prog_number != PROG_BANKER)
        return false;
    return rnum < 0 || !mob_index[rnum].func || mob_index[rnum].func == (special_func)banker;
}

void banker_config_check(const struct char_data* proto, int mob_vnum, struct char_data* builder)
{
    std::vector<vendor_problem> problems;
    parse_banker_options(proto->specials.mob_options, &problems);
    add_speech_problem(*proto, &problems);
    add_pref_problem(*proto, &problems);
    for (const vendor_problem& problem : problems)
        vendor_send(vendor_problem_line(mob_vnum, problem), builder);
}

/* Soft checks, on /imp only, to the builder alone (not at boot, not logged). */
void banker_implement_check(int mob_rnum, struct char_data* builder)
{
    if (!builder || mob_rnum < 0 || mob_rnum > top_of_mobt || !is_banker_proto(mob_rnum))
        return;
    char buf[128];
    if (!IS_SET(mob_proto[mob_rnum].specials2.act, MOB_NOBASH)) {
        snprintf(buf, sizeof(buf), "MOB WARNING: mobile #%d: nobash not set\n\r", mob_index[mob_rnum].virt);
        send_to_char(buf, builder);
    }
    banker_config config = parse_banker_options(mob_proto[mob_rnum].specials.mob_options, nullptr);
    if (config.markup > 0 && config.fee == 0) {
        snprintf(buf, sizeof(buf), "MOB WARNING: mobile #%d: racial_markup without fee\n\r", mob_index[mob_rnum].virt);
        send_to_char(buf, builder);
    }
}

void banker_config_rebuild(int mob_rnum, struct char_data* builder, bool report)
{
    g_banker_disabled_logged.erase(mob_rnum);
    if (mob_rnum < 0 || mob_rnum > top_of_mobt || !is_banker_proto(mob_rnum)) {
        g_banker_configs.erase(mob_rnum);
        return;
    }
    std::vector<vendor_problem> problems;
    g_banker_configs[mob_rnum] = parse_banker_options(mob_proto[mob_rnum].specials.mob_options, &problems);
    if (!report)
        return;
    add_speech_problem(mob_proto[mob_rnum], &problems);
    add_pref_problem(mob_proto[mob_rnum], &problems);
    for (const vendor_problem& problem : problems)
        vendor_send(vendor_problem_line(mob_index[mob_rnum].virt, problem), builder);
}

const banker_config* banker_config_for(int mob_rnum)
{
    auto it = g_banker_configs.find(mob_rnum);
    return it == g_banker_configs.end() ? nullptr : &it->second;
}

void banker_config_boot()
{
    g_banker_configs.clear();
    g_banker_disabled_logged.clear();
    for (int rnum = 0; rnum <= top_of_mobt; ++rnum)
        if (is_banker_proto(rnum))
            banker_config_rebuild(rnum, nullptr);
}

SPECIAL(banker)
{
    /* Only a registered banker mob is ever a banker (see barter_vendor). */
    if (!host || !IS_NPC(host))
        return FALSE;
    const banker_config* config = banker_config_for(host->nr);
    if (!config)
        return FALSE;
    if (callflag == SPECIAL_DAMAGE) { /* before ch == host: poison ticks are self-damage */
        if (ch && ch != host)
            vendor_say(host, "Don't even think about it.");
        return TRUE;
    }
    if (!ch || ch == host)
        return FALSE;
    if (callflag == SPECIAL_TARGET) { /* dust blinds even with its damage cancelled */
        if (cmd != CMD_BLINDING || !wtl || wtl->targ1.type != TARGET_CHAR || wtl->targ1.ptr.ch != host)
            return FALSE;
        vendor_say(host, "Don't even think about it.");
        return TRUE;
    }
    if (callflag != SPECIAL_COMMAND)
        return FALSE;
    if (cmd == CMD_GIVE) {
        if (!arg || !give_targets(host, ch, arg))
            return FALSE;
        vendor_say(host, "I don't take gifts.");
        return TRUE;
    }
    if (cmd != CMD_BALANCE && cmd != CMD_DEPOSIT && cmd != CMD_WITHDRAW)
        return FALSE;

    bank_customer customer;
    if (!banker_admits(host, ch, *config, &customer))
        return TRUE;
    if (cmd == CMD_BALANCE)
        banker_balance(host, ch, *config, customer);
    else
        vendor_say(host, "Not yet."); /* deposit and withdraw: Task 6 */
    return TRUE;
}
```

- [ ] **Step 8: Run the tests**

Run: `scripts/rots-docker.sh test --gtest_filter='BankBalance.*:BankerTest.*'`
Expected: all pass. If `BankBalance.LayoutWithFees` is off by a space, fix the code's padding
arithmetic, not the expected text: the name column is 37 wide and the fee text starts at column 44.

- [ ] **Step 9: Wire the program into the game**

`src/spec_ass.cpp`:
- add `#include "mob_progs/banker.h"` beside the shopkeeper include (line 16);
- `spec_pro_message[]` (line ~274): change the last entry `"" // 33 barter vendor` to
  `"", // 33 barter vendor` and add `"" // 34 banker` after it;
- in `virt_program_number` (line ~380) add `case 34: return (void*)banker;` after case 33;
- in the second table (line ~458) add `case 34: return &banker;` after case 33.

`src/db.cpp`: add `#include "mob_progs/banker.h"`, and after `vendor_config_boot();`:

```cpp
    log("Checking bankers.");
    banker_config_boot();
```

`src/shapemob.cpp`: add `#include "mob_progs/banker.h"`, and next to each vendor call add the
banker call with the same arguments:
- line ~2067 after `vendor_config_check(SHAPE_PROTO(ch)->proto, num, ch);` →
  `banker_config_check(SHAPE_PROTO(ch)->proto, num, ch);`
- line ~2180 after `vendor_config_check(SHAPE_PROTO(ch)->proto, i1 + 1, ch);` →
  `banker_config_check(SHAPE_PROTO(ch)->proto, i1 + 1, ch);`
- line ~2286 after the two vendor lines →
  `banker_config_rebuild(number, ch, report_vendor);` and `banker_implement_check(number, ch);`

`vendor_config_check` reports for any mob it is called on. Read the two call sites: if the
vendor call is guarded by `is_vendor_candidate(...)`, guard the banker call with
`is_banker_candidate(...)` the same way; if the vendor's own function does the guarding,
make `banker_config_check` return early when `!is_banker_candidate(proto, real_mobile(mob_vnum))`
to match.

- [ ] **Step 10: Compile, run everything, boot**

Run: `scripts/rots-docker.sh compile` — expected: clean build.
Run: `scripts/rots-docker.sh test` — expected: only the known 6 failures.
Run: `scripts/rots-docker.sh boot` and check the boot log shows `Reading game boot options.`
and `Checking bankers.` with no SYSERR after them. Stop the server.

- [ ] **Step 11: Format and commit**

```bash
cd src && clang-format -i -style=WebKit mob_progs/banker.h mob_progs/banker.cpp tests/banker_tests.cpp && cd ..
git diff --stat   # spec_ass.cpp, db.cpp, shapemob.cpp, interpre.h: only the added lines
git add src/mob_progs src/tests/banker_tests.cpp src/interpre.h src/spec_ass.cpp src/db.cpp src/shapemob.cpp
git commit -m "feat(bank): banker program 34 with balance, refusals and protection"
```

---

### Task 6: `deposit` and `withdraw`

**Files:**
- Modify: `src/mob_progs/banker.cpp`, `src/tests/banker_tests.cpp`

**Interfaces:**
- Consumes: everything from Tasks 1-5.
- Produces: no new exported names. `SPECIAL(banker)` now handles `CMD_DEPOSIT` and
  `CMD_WITHDRAW`.

**Command forms**

| Typed | Meaning |
|-------|---------|
| `deposit <item>` | an item in loose inventory, by keyword (`2.sword` works) |
| `deposit <N> gold` / `silver` / `copper` | coins; `coins` or `coin` means copper |
| `withdraw <#>` | the slot with that `balance` number |
| `withdraw <item>` | first slot whose item matches the keyword (`2.sword` works) |
| `withdraw <N> gold` / `silver` / `copper` | coins |

The second word decides: a number followed by a coin word is always coins.

**Messages** (each at most 78 columns; `<money>` is `money_message(amount, 0)`):

| Case | Text |
|------|------|
| no argument | say `What would you like to deposit?` / `... withdraw?` |
| item not carried | `You don't have that.` |
| item unrentable | say `I can't keep that for you.` |
| slots full | say `Your vault is full.` |
| deposited | `You hand <item> to <banker>.` + room `$n deposits $p.` |
| coin amount 0, or not a number | say `How much?` |
| not enough carried | `You don't have that much.` |
| vault at coin limit | say `Your vault can hold no more coins.` |
| coins deposited | `You deposit <money>.` and, when cut short, `<money> was refused: your vault is full.` |
| nothing matches | say `I hold nothing like that for you.` |
| item can't be rebuilt | say `I can't get that out right now.` |
| too many items | `You can't carry that many items.` |
| too heavy | `You can't carry that much weight.` |
| can't pay | say `That costs <money>. You don't have it.` |
| item withdrawn | `<Banker> hands you <item>.` + room `$n withdraws $p.`; when a fee was paid: `You pay <money> from your purse.` and/or `<money> comes out of your vault.` |
| vault has fewer coins | say `You don't have that much with me.` |
| coins withdrawn | `You withdraw <money>.` |
| vault write failed | say `I can't reach the vault right now.` |

- [ ] **Step 1: Write the failing tests** (append to `banker_tests.cpp`)

```cpp
TEST_F(BankerTest, DepositItemMovesItToTheVaultFileFirst)
{
    give(0);
    EXPECT_EQ(call(CMD_DEPOSIT, "sword"), TRUE);
    EXPECT_EQ(carried(0), 0);
    ASSERT_EQ(vault()->slots.size(), 1u);
    EXPECT_EQ(vault()->slots[0].objects[0].item_number, kSwordVnum);
    EXPECT_EQ(vault()->slots[0].deposited, (long)m_now);
    EXPECT_EQ(m_saves, 1);
    EXPECT_NE(m_file_at_save.find("\"item_number\": 100"), std::string::npos)
        << "the vault file already held the item when the character was saved";
    EXPECT_NE(output().find("You hand a bastard sword to the banker."), std::string::npos) << output();
}

TEST_F(BankerTest, DepositAContainerIsOneSlotWithItsContents)
{
    obj_data* pack = give(1);
    obj_to_obj(read_object(0, REAL), pack);
    obj_to_obj(read_object(0, REAL), pack);
    EXPECT_EQ(call(CMD_DEPOSIT, "backpack"), TRUE);
    ASSERT_EQ(vault()->slots.size(), 1u);
    EXPECT_EQ(vault()->slots[0].objects.size(), 3u);
    EXPECT_EQ(object_list, nullptr) << "the stored objects are gone from the world";
    EXPECT_EQ(IS_CARRYING_N(&m_player), 0);
    EXPECT_EQ(IS_CARRYING_W(&m_player), 0);
}

TEST_F(BankerTest, DepositRefusals)
{
    EXPECT_EQ(call(CMD_DEPOSIT, ""), TRUE);
    EXPECT_NE(output().find("What would you like to deposit?"), std::string::npos);
    EXPECT_EQ(call(CMD_DEPOSIT, "sword"), TRUE);
    EXPECT_NE(output().find("You don't have that."), std::string::npos);

    give(2); /* a key */
    EXPECT_EQ(call(CMD_DEPOSIT, "key"), TRUE);
    EXPECT_NE(output().find("I can't keep that for you."), std::string::npos);
    EXPECT_EQ(carried(2), 1);

    obj_data* pack = give(1);
    obj_to_obj(read_object(2, REAL), pack); /* a key hidden in a pack */
    EXPECT_EQ(call(CMD_DEPOSIT, "backpack"), TRUE);
    EXPECT_NE(output().find("I can't keep that for you."), std::string::npos);
    EXPECT_TRUE(vault()->slots.empty());
    EXPECT_EQ(m_saves, 0);
}

TEST_F(BankerTest, WornItemsCannotBeDeposited)
{
    obj_data* sword = read_object(0, REAL);
    equip_char(&m_player, sword, WIELD);
    EXPECT_EQ(call(CMD_DEPOSIT, "sword"), TRUE);
    EXPECT_NE(output().find("You don't have that."), std::string::npos);
    EXPECT_EQ(m_player.equipment[WIELD], sword);
    obj_to_char(unequip_char(&m_player, WIELD), &m_player);
}

TEST_F(BankerTest, FullVaultRefusesAndALoweredLimitRemovesNothing)
{
    boot_options_set_running_for_tests(BOOT_BANK_SLOTS, 2);
    for (int i = 0; i < 3; ++i)
        give(0);
    call(CMD_DEPOSIT, "sword");
    call(CMD_DEPOSIT, "sword");
    EXPECT_EQ(call(CMD_DEPOSIT, "sword"), TRUE);
    EXPECT_NE(output().find("Your vault is full."), std::string::npos);
    EXPECT_EQ(vault()->slots.size(), 2u);
    EXPECT_EQ(carried(0), 1);

    boot_options_set_running_for_tests(BOOT_BANK_SLOTS, 1); /* lowered below contents */
    call(CMD_DEPOSIT, "sword");
    EXPECT_EQ(vault()->slots.size(), 2u);
    call(CMD_BALANCE, "");
    EXPECT_NE(output().find("Slots: 2 of 1 used"), std::string::npos);
    EXPECT_EQ(call(CMD_WITHDRAW, "1"), TRUE);
    EXPECT_EQ(vault()->slots.size(), 1u) << "withdrawing still works over the limit";
}

TEST_F(BankerTest, DepositCoinsAndThePartialCase)
{
    GET_GOLD(&m_player) = 5 * COPP_IN_GOLD;
    EXPECT_EQ(call(CMD_DEPOSIT, "2 gold"), TRUE);
    EXPECT_EQ(vault()->coins, 2000);
    EXPECT_EQ(GET_GOLD(&m_player), 3000);
    EXPECT_NE(output().find("You deposit 2 gold."), std::string::npos) << output();
    EXPECT_NE(m_file_at_save.find("\"coins\": 2000"), std::string::npos);

    call(CMD_DEPOSIT, "5 silver");
    EXPECT_EQ(vault()->coins, 2500);
    call(CMD_DEPOSIT, "7 coins");
    EXPECT_EQ(vault()->coins, 2507);

    boot_options_set_running_for_tests(BOOT_BANK_COIN_LIMIT_GOLD, 3);
    call(CMD_DEPOSIT, "2 gold"); /* only 493 copper fit */
    EXPECT_EQ(vault()->coins, 3000);
    EXPECT_EQ(GET_GOLD(&m_player), 3000 - 500 - 7 - 493);
    EXPECT_NE(output().find("was refused: your vault is full."), std::string::npos) << output();
    call(CMD_DEPOSIT, "1 copper");
    EXPECT_NE(output().find("Your vault can hold no more coins."), std::string::npos);
}

TEST_F(BankerTest, OddCoinAmountsChangeNothing)
{
    GET_GOLD(&m_player) = 1000;
    const char* bad[] = { "0 gold", "999999999 gold", "99999999999999999999 gold", "-5 gold", "2 gold" /* > carried */ };
    for (const char* text : bad) {
        int saves = m_saves;
        EXPECT_EQ(call(CMD_DEPOSIT, text), TRUE) << text;
        EXPECT_EQ(vault()->coins, 0) << text;
        EXPECT_EQ(GET_GOLD(&m_player), 1000) << text;
        EXPECT_EQ(m_saves, saves) << text;
    }
    vault()->coins = 1000;
    for (const char* text : { "0 gold", "999999999 gold", "2 gold" }) {
        EXPECT_EQ(call(CMD_WITHDRAW, text), TRUE) << text;
        EXPECT_EQ(vault()->coins, 1000) << text;
        EXPECT_EQ(GET_GOLD(&m_player), 1000) << text;
    }
}

TEST_F(BankerTest, WithdrawCoinsSavesTheCharacterBeforeTheVaultFile)
{
    vault()->coins = 5000;
    std::string error;
    ASSERT_TRUE(bank_vault_write("tester", BANK_SIDE_LIGHT, &error));
    EXPECT_EQ(call(CMD_WITHDRAW, "3 gold"), TRUE);
    EXPECT_EQ(vault()->coins, 2000);
    EXPECT_EQ(GET_GOLD(&m_player), 3000);
    EXPECT_NE(m_file_at_save.find("\"coins\": 5000"), std::string::npos)
        << "when the character was saved the vault file still held the coins";
    EXPECT_NE(read_file(path("tester", "vault_light.json")).find("\"coins\": 2000"), std::string::npos);
}

TEST_F(BankerTest, WithdrawItemByNumberAndByKeyword)
{
    give(0);
    give(1);
    call(CMD_DEPOSIT, "sword");
    call(CMD_DEPOSIT, "backpack");
    EXPECT_EQ(call(CMD_WITHDRAW, "2"), TRUE);
    EXPECT_EQ(carried(1), 1);
    EXPECT_NE(output().find("The banker hands you a leather backpack."), std::string::npos) << output();
    EXPECT_EQ(call(CMD_WITHDRAW, "sword"), TRUE);
    EXPECT_EQ(carried(0), 1);
    EXPECT_TRUE(vault()->slots.empty());
    EXPECT_EQ(call(CMD_WITHDRAW, "sword"), TRUE);
    EXPECT_NE(output().find("I hold nothing like that for you."), std::string::npos);
    EXPECT_EQ(call(CMD_WITHDRAW, "7"), TRUE);
    EXPECT_NE(output().find("I hold nothing like that for you."), std::string::npos);
}

TEST_F(BankerTest, WithdrawItemSavesTheCharacterBeforeTheVaultFile)
{
    give(0);
    call(CMD_DEPOSIT, "sword");
    m_file_at_save.clear();
    call(CMD_WITHDRAW, "1");
    EXPECT_NE(m_file_at_save.find("\"item_number\": 100"), std::string::npos)
        << "the vault file still held the item when the character was saved";
    EXPECT_EQ(read_file(path("tester", "vault_light.json")).find("\"item_number\": 100"), std::string::npos);
}

TEST_F(BankerTest, FeeIsTakenFromThePurseFirstThenTheVault)
{
    options("fee=50\nmaxdays=30");
    give(0);
    call(CMD_DEPOSIT, "sword");
    m_now = at(2026, 10, 3, 12); /* three 5am points later: 150 copper */
    call(CMD_BALANCE, "");
    EXPECT_NE(output().find("1 silver and 50 copper"), std::string::npos) << output();

    GET_GOLD(&m_player) = 100;
    vault()->coins = 1000;
    EXPECT_EQ(call(CMD_WITHDRAW, "1"), TRUE);
    EXPECT_EQ(GET_GOLD(&m_player), 0);
    EXPECT_EQ(vault()->coins, 950);
    EXPECT_EQ(carried(0), 1);
    EXPECT_NE(output().find("You pay 1 silver from your purse."), std::string::npos) << output();
    EXPECT_NE(output().find("50 copper comes out of your vault."), std::string::npos) << output();
}

TEST_F(BankerTest, CannotPayTakesNothing)
{
    options("fee=50\nmaxdays=30");
    give(0);
    call(CMD_DEPOSIT, "sword");
    m_now = at(2026, 10, 3, 12);
    GET_GOLD(&m_player) = 100;
    vault()->coins = 49;
    int saves = m_saves;
    EXPECT_EQ(call(CMD_WITHDRAW, "1"), TRUE);
    EXPECT_NE(output().find("That costs 1 silver and 50 copper. You don't have it."), std::string::npos) << output();
    EXPECT_EQ(GET_GOLD(&m_player), 100);
    EXPECT_EQ(vault()->coins, 49);
    EXPECT_EQ(vault()->slots.size(), 1u);
    EXPECT_EQ(carried(0), 0);
    EXPECT_EQ(object_list, nullptr) << "the rebuilt item was destroyed again";
    EXPECT_EQ(m_saves, saves);
}

TEST_F(BankerTest, SameDayIsFreeAndAnotherRacePaysTheMarkup)
{
    options("fee=10\nmaxdays=30\nracial_markup=yes");
    give(0);
    call(CMD_DEPOSIT, "sword");
    call(CMD_BALANCE, "");
    EXPECT_NE(output().find("free"), std::string::npos) << output();
    m_now = at(2026, 10, 1, 12);
    m_player.player.race = RACE_DWARF; /* banker is human; still the light vault */
    call(CMD_BALANCE, "");
    EXPECT_NE(output().find("13 copper"), std::string::npos) << output();
}

TEST_F(BankerTest, CarryLimitsRefuseAndChargeNothing)
{
    options("fee=50\nmaxdays=30");
    give(0);
    call(CMD_DEPOSIT, "sword");
    m_now = at(2026, 10, 3, 12);
    GET_GOLD(&m_player) = 5000;
    m_player.specials.carry_items = CAN_CARRY_N(&m_player);
    EXPECT_EQ(call(CMD_WITHDRAW, "1"), TRUE);
    EXPECT_NE(output().find("You can't carry that many items."), std::string::npos);
    m_player.specials.carry_items = 0;
    m_player.specials.carry_weight = CAN_CARRY_W(&m_player);
    EXPECT_EQ(call(CMD_WITHDRAW, "1"), TRUE);
    EXPECT_NE(output().find("You can't carry that much weight."), std::string::npos);
    m_player.specials.carry_weight = 0;
    EXPECT_EQ(GET_GOLD(&m_player), 5000);
    EXPECT_EQ(vault()->slots.size(), 1u);
}

TEST_F(BankerTest, AStoredItemWithNoPrototypeStaysInTheVault)
{
    vault()->slots.push_back({ (long)m_now, { record(9999, 0) } });
    EXPECT_EQ(call(CMD_WITHDRAW, "1"), TRUE);
    EXPECT_NE(output().find("I can't get that out right now."), std::string::npos) << output();
    EXPECT_EQ(vault()->slots.size(), 1u);
    call(CMD_BALANCE, "");
    EXPECT_NE(output().find("something"), std::string::npos);
}

TEST_F(BankerTest, FailedVaultWriteRefusesTheDeposit)
{
    give(0);
    mkdir(path("tester", "vault_light.json.tmp").c_str(), 0700); /* blocks the temp file */
    EXPECT_EQ(call(CMD_DEPOSIT, "sword"), TRUE);
    EXPECT_NE(output().find("I can't reach the vault right now."), std::string::npos) << output();
    EXPECT_EQ(carried(0), 1);
    EXPECT_TRUE(vault()->slots.empty());
    EXPECT_EQ(m_saves, 0);
}

TEST_F(BankerTest, EachSideHasItsOwnVaultAndCharactersShareTheirSides)
{
    give(0);
    call(CMD_DEPOSIT, "sword");
    m_player.player.race = RACE_DWARF; /* another light character of the account */
    call(CMD_BALANCE, "");
    EXPECT_NE(output().find("a bastard sword"), std::string::npos);
    m_player.player.race = RACE_URUK;
    m_banker.player.race = RACE_URUK;
    call(CMD_BALANCE, "");
    EXPECT_EQ(output().find("a bastard sword"), std::string::npos);
    EXPECT_NE(output().find("Slots: 0 of 10 used"), std::string::npos);
}

TEST_F(BankerTest, TransactionsAreLogged)
{
    testing::internal::CaptureStderr();
    give(0);
    call(CMD_DEPOSIT, "sword");
    call(CMD_WITHDRAW, "1");
    std::string logged = testing::internal::GetCapturedStderr();
    EXPECT_NE(logged.find("BANK: Player deposits a bastard sword (100) at mobile #7100, account tester, side 1"),
        std::string::npos)
        << logged;
    EXPECT_NE(logged.find("BANK: Player withdraws a bastard sword (100) at mobile #7100, account tester, side 1, fee 0"),
        std::string::npos)
        << logged;
}
```

If `RACE_URUK` is refused by `vendor_serves_customer` in the last-but-one test because of
`IS_AGGR_TO` or `RP_RACE_CHECK` on the test mob, set the banker's `specials2.rp_flag` and
`pref` to 0 in that test; do not weaken the checks.

- [ ] **Step 2: Run to see them fail**

Run: `scripts/rots-docker.sh test --gtest_filter='BankerTest.*'`
Expected: the new tests fail (output is `Not yet.`).

- [ ] **Step 3: Implement** — in `banker.cpp`, inside the unnamed namespace above
  `SPECIAL(banker)`:

```cpp
void bank_log(const std::string& line)
{
    char buf[512];
    snprintf(buf, sizeof(buf), "%s", line.c_str());
    log(buf);
}

std::string bank_log_tail(struct char_data* host, const bank_customer& customer)
{
    return " at mobile #" + std::to_string(host->nr >= 0 ? mob_index[host->nr].virt : -1) + ", account "
        + account::normalize_account_name(customer.account) + ", side " + std::to_string(customer.side);
}

/* "<N> gold|silver|copper|coins|coin" -> copper. *is_coins says whether the
 * argument was a coin request at all; false with *is_coins set means a bad amount. */
bool parse_coins(const char* arg, bool* is_coins, long long* copper)
{
    char first[MAX_INPUT_LENGTH], second[MAX_INPUT_LENGTH], whole[MAX_INPUT_LENGTH];
    strncpy(whole, arg, sizeof(whole) - 1);
    whole[sizeof(whole) - 1] = 0;
    half_chop(whole, first, second);
    long long unit = !str_cmp(second, "gold") ? COPP_IN_GOLD : !str_cmp(second, "silver") ? COPP_IN_SILV
        : (!str_cmp(second, "copper") || !str_cmp(second, "coins") || !str_cmp(second, "coin"))      ? 1
                                                                                                    : 0;
    const char* digits = *first == '-' ? first + 1 : first;
    *is_coins = unit != 0 && *digits && strspn(digits, "0123456789") == strlen(digits);
    if (!*is_coins)
        return false;
    if (*first == '-' || strlen(first) > 9)
        return false;
    *copper = atoll(first) * unit;
    return *copper > 0 && *copper <= 2000000000LL;
}

void bank_deposit_coins(struct char_data* host, struct char_data* ch, const bank_customer& customer, long long copper)
{
    char buf[256];
    if (copper > GET_GOLD(ch)) {
        send_to_char("You don't have that much.\n\r", ch);
        return;
    }
    long long room = (long long)boot_option(BOOT_BANK_COIN_LIMIT_GOLD) * COPP_IN_GOLD - customer.vault->coins;
    if (room <= 0) {
        vendor_say(host, "Your vault can hold no more coins.");
        return;
    }
    long long take = std::min(copper, room);
    customer.vault->coins += (int)take;
    std::string error;
    if (!bank_vault_write(customer.account, customer.side, &error)) { /* vault first */
        customer.vault->coins -= (int)take;
        vendor_say(host, "I can't reach the vault right now.");
        return;
    }
    GET_GOLD(ch) -= (int)take;
    bank_save_character(ch);
    snprintf(buf, sizeof(buf), "You deposit %s.\n\r", money_message((int)take, 0));
    send_to_char(buf, ch);
    if (take < copper) {
        snprintf(buf, sizeof(buf), "%s was refused: your vault is full.\n\r", money_message((int)(copper - take), 0));
        send_to_char(CAP(buf), ch);
    }
    bank_log(std::string("BANK: ") + GET_NAME(ch) + " deposits " + std::to_string(take) + " copper"
        + bank_log_tail(host, customer));
}

void bank_deposit(struct char_data* host, struct char_data* ch, char* arg, const bank_customer& customer)
{
    char name[MAX_INPUT_LENGTH], buf[MAX_STRING_LENGTH];
    bool is_coins = false;
    long long copper = 0;
    if (parse_coins(arg, &is_coins, &copper)) {
        bank_deposit_coins(host, ch, customer, copper);
        return;
    }
    if (is_coins) {
        vendor_say(host, "How much?");
        return;
    }
    one_argument(arg, name);
    if (!*name) {
        vendor_say(host, "What would you like to deposit?");
        return;
    }
    struct obj_data* obj = get_obj_in_list_vis(ch, name, ch->carrying); /* loose inventory only */
    if (!obj) {
        send_to_char("You don't have that.\n\r", ch);
        return;
    }
    if (!bank_obj_storable(obj)) {
        vendor_say(host, "I can't keep that for you.");
        return;
    }
    if ((int)customer.vault->slots.size() >= boot_option(BOOT_BANK_SLOTS)) {
        vendor_say(host, "Your vault is full.");
        return;
    }
    bank_slot slot;
    slot.deposited = (long)bank_now();
    bank_records_from_obj(obj, &slot.objects);
    customer.vault->slots.push_back(slot);
    std::string error;
    if (!bank_vault_write(customer.account, customer.side, &error)) { /* vault first */
        customer.vault->slots.pop_back();
        vendor_say(host, "I can't reach the vault right now.");
        return;
    }
    snprintf(buf, sizeof(buf), "You hand %s to %s.\n\r", obj->short_description, GET_NAME(host));
    send_to_char(buf, ch);
    act("$n deposits $p.", FALSE, ch, obj, 0, TO_ROOM);
    bank_log(std::string("BANK: ") + GET_NAME(ch) + " deposits " + obj->short_description + " ("
        + std::to_string(slot.objects[0].item_number) + ")" + bank_log_tail(host, customer));
    obj_from_char(obj);
    extract_obj(obj); /* takes its contents with it */
    bank_save_character(ch);
}

void bank_withdraw_coins(struct char_data* host, struct char_data* ch, const bank_customer& customer, long long copper)
{
    char buf[256];
    if (copper > customer.vault->coins) {
        vendor_say(host, "You don't have that much with me.");
        return;
    }
    if ((long long)GET_GOLD(ch) + copper > 2000000000LL) {
        send_to_char("You can't carry that much money.\n\r", ch);
        return;
    }
    GET_GOLD(ch) += (int)copper;
    bank_save_character(ch); /* character first */
    customer.vault->coins -= (int)copper;
    std::string error;
    if (!bank_vault_write(customer.account, customer.side, &error))
        bank_log("SYSERR: bank: vault write failed after a coin withdrawal: " + error);
    snprintf(buf, sizeof(buf), "You withdraw %s.\n\r", money_message((int)copper, 0));
    send_to_char(buf, ch);
    bank_log(std::string("BANK: ") + GET_NAME(ch) + " withdraws " + std::to_string(copper) + " copper"
        + bank_log_tail(host, customer));
}

void bank_withdraw(struct char_data* host, struct char_data* ch, char* arg, const banker_config& config,
    const bank_customer& customer)
{
    char want[MAX_INPUT_LENGTH], buf[MAX_STRING_LENGTH];
    bool is_coins = false;
    long long copper = 0;
    if (parse_coins(arg, &is_coins, &copper)) {
        bank_withdraw_coins(host, ch, customer, copper);
        return;
    }
    if (is_coins) {
        vendor_say(host, "How much?");
        return;
    }
    one_argument(arg, want);
    if (!*want) {
        vendor_say(host, "What would you like to withdraw?");
        return;
    }
    std::vector<bank_slot>& slots = customer.vault->slots;
    int pick = -1;
    if (strspn(want, "0123456789") == strlen(want)) { /* a balance number */
        int n = strlen(want) <= 4 ? atoi(want) : 0;
        if (n >= 1 && n <= (int)slots.size())
            pick = n - 1;
    } else { /* a keyword; "2.sword" = the second matching slot */
        char* name = want;
        int nth = get_number(&name);
        for (size_t i = 0; i < slots.size() && pick < 0; ++i) {
            int rnum = real_object(slots[i].objects[0].item_number);
            if (rnum >= 0 && isname(name, obj_proto[rnum].name) && --nth == 0)
                pick = (int)i;
        }
    }
    if (pick < 0) {
        vendor_say(host, "I hold nothing like that for you.");
        return;
    }
    struct obj_data* obj = bank_obj_from_records(slots[pick].objects);
    if (!obj) {
        vendor_say(host, "I can't get that out right now.");
        snprintf(buf, sizeof(buf), "SYSERR: bank: stored object #%d can't be rebuilt",
            slots[pick].objects[0].item_number);
        bank_log(buf);
        return;
    }
    if (IS_CARRYING_N(ch) + 1 > CAN_CARRY_N(ch)) {
        send_to_char("You can't carry that many items.\n\r", ch);
        extract_obj(obj);
        return;
    }
    if (IS_CARRYING_W(ch) + GET_OBJ_WEIGHT(obj) > CAN_CARRY_W(ch)) {
        send_to_char("You can't carry that much weight.\n\r", ch);
        extract_obj(obj);
        return;
    }
    long long fee = slot_fee(config, slots[pick], host, ch);
    if (fee > (long long)GET_GOLD(ch) + customer.vault->coins) {
        snprintf(buf, sizeof(buf), "That costs %s. You don't have it.",
            money_message((int)std::min<long long>(fee, 2000000000LL), 0));
        vendor_say(host, buf);
        extract_obj(obj);
        return;
    }
    int from_purse = (int)std::min<long long>(fee, GET_GOLD(ch));
    int from_vault = (int)(fee - from_purse);
    int vnum = slots[pick].objects[0].item_number;

    GET_GOLD(ch) -= from_purse;
    obj_to_char(obj, ch);
    bank_save_character(ch); /* character first */
    customer.vault->coins -= from_vault;
    slots.erase(slots.begin() + pick);
    std::string error;
    if (!bank_vault_write(customer.account, customer.side, &error))
        bank_log("SYSERR: bank: vault write failed after a withdrawal: " + error);

    snprintf(buf, sizeof(buf), "%s hands you %s.\n\r", GET_NAME(host), obj->short_description);
    send_to_char(CAP(buf), ch);
    if (from_purse > 0) {
        snprintf(buf, sizeof(buf), "You pay %s from your purse.\n\r", money_message(from_purse, 0));
        send_to_char(buf, ch);
    }
    if (from_vault > 0) {
        snprintf(buf, sizeof(buf), "%s comes out of your vault.\n\r", money_message(from_vault, 0));
        send_to_char(CAP(buf), ch);
    }
    act("$n withdraws $p.", FALSE, ch, obj, 0, TO_ROOM);
    bank_log(std::string("BANK: ") + GET_NAME(ch) + " withdraws " + obj->short_description + " ("
        + std::to_string(vnum) + ")" + bank_log_tail(host, customer) + ", fee " + std::to_string(fee));
}
```

Add `int get_number(char** name);` beside the other forward declarations. In
`SPECIAL(banker)` replace the `Not yet.` branch:

```cpp
    if (cmd == CMD_BALANCE)
        banker_balance(host, ch, *config, customer);
    else if (cmd == CMD_DEPOSIT)
        bank_deposit(host, ch, arg ? arg : (char*)"", customer);
    else
        bank_withdraw(host, ch, arg ? arg : (char*)"", *config, customer);
    return TRUE;
```

Notes for the implementer:
- `get_obj_in_list_vis` is declared at `src/handler.h:108`; if it takes a fourth argument,
  pass what `do_drop` (`src/act_obj1.cpp`) passes.
- `GET_NAME(host)` for a mob is its short description ("the banker"); `CAP` capitalises the
  buffer in place. A colour escape at the start would defeat `CAP` (known, deferred); mob
  short descriptions used for bankers must not start with one.
- `money_message` returns a static buffer: never call it twice inside one `snprintf`.
- The character save cannot report failure (`save_char` and `Crash_crashsave` return
  nothing). A withdrawal therefore always goes on to the vault write. This is the one place
  the spec's "if the first write fails, refuse" cannot be honoured for the character side;
  it errs toward a duplicate, never a loss.

- [ ] **Step 4: Run the tests**

Run: `scripts/rots-docker.sh test --gtest_filter='BankerTest.*:BankBalance.*'`
Expected: all pass.

`TransactionsAreLogged` relies on `log()` writing to stderr. If `log()` writes elsewhere
under `-DTESTING`, read how `BarterVendorTest.PurchaseIsLoggedAndTheItemMarkedHandled`
(`barter_vendor_tests.cpp:937`) captures the vendor log and capture the same way.

- [ ] **Step 5: Full suite, format, commit**

Run: `scripts/rots-docker.sh test` — expected: only the known 6 failures.

```bash
cd src && clang-format -i -style=WebKit mob_progs/banker.cpp tests/banker_tests.cpp && cd ..
git add src/mob_progs/banker.cpp src/tests/banker_tests.cpp
git commit -m "feat(bank): deposit and withdraw with fees, limits and crash-safe save order"
```

---

### Task 7: The immortal `vault` command

**Files:**
- Modify: `src/mob_progs/banker.h`, `src/mob_progs/banker.cpp`, `src/interpre.cpp`,
  `src/tests/banker_tests.cpp`

**Interfaces:**
- Consumes: the vault table, `bank_obj_from_records`, `bank_records_from_obj`,
  `bank_obj_storable`, `bank_save_character`, `account::read_account_file_by_identifier`,
  `account::read_account_file`, `account::find_linked_character_owner_account`.
- Produces:
  ```cpp
  ACMD(do_vault);
  /* Test hook: how `vault` finds an account. Empty restores the game lookups. */
  struct bank_account_ref { std::string name; std::string email; };
  void bank_set_account_lookups(
      std::function<bool(const std::string& identifier, bank_account_ref* out)> by_email_or_name,
      std::function<bool(const std::string& exact_name, bank_account_ref* out)> by_name,
      std::function<bool(const std::string& character, bank_account_ref* out, int* race)> by_character);
  std::string format_vault_view(const bank_account_ref& account, const std::string& character, int side,
      const bank_vault& vault);
  ```

**Forms**

| Typed | Does |
|-------|------|
| `vault <character>` | that character's side |
| `vault <email or account>` | all three sides |
| `vault <email or account> <1\|2\|3>` | one side |
| `vault take <account> <1\|2\|3> <slot>` | item to the immortal, no fee |
| `vault take <account> <1\|2\|3> coins <N>` | N copper to the immortal |
| `vault put <account> <1\|2\|3> <item>` | item from the immortal's inventory |
| `vault put <account> <1\|2\|3> coins <N>` | N copper from the immortal |

Coins in `take`/`put` take the same `<N> gold|silver|copper` words as the player commands,
written after `coins`: `vault take bob 1 coins 5 gold`. A bare number means copper.

Lookup order for the viewing form: try the identifier as an email or account name
(`read_account_file_by_identifier`); if that fails, as a character name
(`find_linked_character_owner_account`). So a name that is both shows the account. For the
character form the side comes from the character's race: use the online character if there
is one (`get_player_vis`-free scan of `character_list` by name), otherwise the race stored in
the account's roster data. Read `src/account_management_presentation.h` and the roster
builder for the function that returns a linked character's race without loading it; if there
is none, load the character the way `do_stat`'s offline `file` form does
(`src/act_wiz.cpp`, search `"file"` in `do_stat`) and free it.

`take` and `put` accept **only** an exact account name (`account::read_account_file`). An
email or character name there gets:
`Use the account name shown by 'vault <name>'.`

**View layout** (78 columns):

```
Account: bobsmith (bob.smith@example.com)   Character: Thorin
Light vault: 142 gold and 5 silver, 2 of 10 slots
 1  a bastard sword                          stored 3 days
 2  a leather backpack                       stored 3 days
      a wolf hide
      a small pouch
        a ruby
```

The `Character:` part appears only for a character lookup. An empty vault prints its header
line and `  (empty)`. An unreadable vault prints `<Side> vault: FILE UNREADABLE`.
Contents are indented two spaces per depth under the item.

- [ ] **Step 1: Write the failing tests** (append). The fixture adds an immortal with a
  descriptor and fake account lookups.

```cpp
namespace {

class VaultCommandTest : public BankerTest {
protected:
    void SetUp() override
    {
        BankerTest::SetUp();
        clear_char(&m_imm, 0);
        m_imm.player.name = m_imm_name;
        m_imm.player.race = RACE_GOD;
        m_imm.player.level = LEVEL_GRGOD;
        m_imm.tmpabilities.str = 18;
        m_imm.tmpabilities.dex = 18;
        m_imm.in_room = 0;
        m_imm_descriptor.output = m_imm_descriptor.small_outbuf;
        m_imm_descriptor.small_outbuf[0] = '\0';
        m_imm_descriptor.bufspace = SMALL_BUFSIZE - 1;
        m_imm_descriptor.connected = CON_PLYNG;
        m_imm_descriptor.character = &m_imm;
        m_imm.desc = &m_imm_descriptor;
        bank_set_account_lookups(
            [](const std::string& id, bank_account_ref* out) {
                if (id != "tester" && id != "tester@example.com")
                    return false;
                *out = { "tester", "tester@example.com" };
                return true;
            },
            [](const std::string& name, bank_account_ref* out) {
                if (name != "tester")
                    return false;
                *out = { "tester", "tester@example.com" };
                return true;
            },
            [](const std::string& character, bank_account_ref* out, int* race) {
                if (character != "thorin")
                    return false;
                *out = { "tester", "tester@example.com" };
                *race = RACE_DWARF;
                return true;
            });
    }
    void TearDown() override
    {
        bank_set_account_lookups(nullptr, nullptr, nullptr);
        BankerTest::TearDown();
    }
    std::string run(const char* text)
    {
        std::strncpy(m_arg, text, sizeof(m_arg) - 1);
        m_imm_descriptor.small_outbuf[0] = '\0';
        m_imm_descriptor.bufptr = 0;
        m_imm_descriptor.bufspace = SMALL_BUFSIZE - 1;
        do_vault(&m_imm, m_arg, nullptr, 253, 0);
        return std::string(m_imm_descriptor.output);
    }
    int imm_carried(int rnum) const
    {
        int count = 0;
        for (obj_data* obj = m_imm.carrying; obj; obj = obj->next_content)
            count += obj->item_number == rnum;
        return count;
    }
    char m_imm_name[16] = "Forge";
    char_data m_imm {};
    descriptor_data m_imm_descriptor {};
};

} // namespace

TEST_F(VaultCommandTest, UsageWithNoArgument)
{
    EXPECT_NE(run("").find("Usage: vault <character | email | account> [1|2|3]"), std::string::npos);
}

TEST_F(VaultCommandTest, ViewByAccountShowsAllThreeSidesWithNameAndEmail)
{
    vault(BANK_SIDE_LIGHT)->coins = 142500;
    vault(BANK_SIDE_LIGHT)->slots.push_back({ (long)at(2026, 9, 27, 12), { record(kPackVnum, 0), record(kSwordVnum, 1) } });
    std::string out = run("tester");
    EXPECT_NE(out.find("Account: tester (tester@example.com)"), std::string::npos) << out;
    EXPECT_EQ(out.find("Character:"), std::string::npos);
    EXPECT_NE(out.find("Light vault: 142 gold and 5 silver, 1 of 10 slots"), std::string::npos) << out;
    EXPECT_NE(out.find(" 1  a leather backpack"), std::string::npos) << out;
    EXPECT_NE(out.find("stored 3 days"), std::string::npos) << out;
    EXPECT_NE(out.find("      a bastard sword"), std::string::npos) << "contents are indented";
    EXPECT_NE(out.find("Dark vault: 0 copper, 0 of 10 slots"), std::string::npos) << out;
    EXPECT_NE(out.find("Third vault:"), std::string::npos);
    EXPECT_NE(run("tester@example.com").find("Light vault:"), std::string::npos);
}

TEST_F(VaultCommandTest, ViewOneSideAndByCharacter)
{
    std::string out = run("tester 2");
    EXPECT_NE(out.find("Dark vault:"), std::string::npos);
    EXPECT_EQ(out.find("Light vault:"), std::string::npos);
    out = run("thorin");
    EXPECT_NE(out.find("Account: tester (tester@example.com)   Character: Thorin"), std::string::npos) << out;
    EXPECT_NE(out.find("Light vault:"), std::string::npos);
    EXPECT_EQ(out.find("Dark vault:"), std::string::npos);
    EXPECT_NE(run("nobody").find("No account or character by that name."), std::string::npos);
    EXPECT_NE(run("tester 4").find("Usage:"), std::string::npos);
}

TEST_F(VaultCommandTest, UnreadableSideIsShownAndTheOthersStillList)
{
    write_file(path("tester", "vault_dark.json"), "junk");
    std::string out = run("tester");
    EXPECT_NE(out.find("Dark vault: FILE UNREADABLE"), std::string::npos) << out;
    EXPECT_NE(out.find("Light vault:"), std::string::npos);
}

TEST_F(VaultCommandTest, TakeItemGivesItToTheImmortalFreeAndSavesTheImmortalFirst)
{
    options("fee=50\nmaxdays=30");
    give(0);
    call(CMD_DEPOSIT, "sword");
    m_now = at(2026, 10, 3, 12);
    m_file_at_save.clear();
    std::string out = run("take tester 1 1");
    EXPECT_NE(out.find("You take a bastard sword from the vault."), std::string::npos) << out;
    EXPECT_EQ(imm_carried(0), 1);
    EXPECT_TRUE(vault()->slots.empty());
    EXPECT_EQ(GET_GOLD(&m_imm), 0) << "no fee";
    EXPECT_NE(m_file_at_save.find("\"item_number\": 100"), std::string::npos) << "immortal saved before the vault file";
}

TEST_F(VaultCommandTest, ThePlayerSeesAnImmortalsChangeAtOnce)
{
    give(0);
    call(CMD_DEPOSIT, "sword");
    run("take tester 1 1");
    call(CMD_BALANCE, "");
    EXPECT_NE(output().find("Slots: 0 of 10 used"), std::string::npos) << output();
}

TEST_F(VaultCommandTest, TakeAndPutCoins)
{
    vault()->coins = 5000;
    EXPECT_NE(run("take tester 1 coins 2 gold").find("You take 2 gold from the vault."), std::string::npos);
    EXPECT_EQ(vault()->coins, 3000);
    EXPECT_EQ(GET_GOLD(&m_imm), 2000);
    EXPECT_NE(run("put tester 1 coins 500").find("You put 5 silver into the vault."), std::string::npos);
    EXPECT_EQ(vault()->coins, 3500);
    EXPECT_NE(run("take tester 1 coins 9 gold").find("The vault doesn't hold that much."), std::string::npos);
    EXPECT_NE(run("put tester 1 coins 9 gold").find("You don't have that much."), std::string::npos);
    boot_options_set_running_for_tests(BOOT_BANK_COIN_LIMIT_GOLD, 3);
    GET_GOLD(&m_imm) = 9000;
    EXPECT_NE(run("put tester 1 coins 1 gold").find("That would pass the vault's coin limit."), std::string::npos);
    EXPECT_EQ(vault()->coins, 3500);
}

TEST_F(VaultCommandTest, PutItemStoresItAsDepositedNowVaultFileFirst)
{
    obj_data* sword = read_object(0, REAL);
    obj_to_char(sword, &m_imm);
    m_file_at_save.clear();
    std::string out = run("put tester 1 sword");
    EXPECT_NE(out.find("You put a bastard sword into the vault."), std::string::npos) << out;
    ASSERT_EQ(vault()->slots.size(), 1u);
    EXPECT_EQ(vault()->slots[0].deposited, (long)m_now);
    EXPECT_EQ(imm_carried(0), 0);
    EXPECT_NE(m_file_at_save.find("\"item_number\": 100"), std::string::npos);
}

TEST_F(VaultCommandTest, PutRefusals)
{
    obj_to_char(read_object(2, REAL), &m_imm);
    EXPECT_NE(run("put tester 1 key").find("The bank can't hold that."), std::string::npos);
    EXPECT_NE(run("put tester 1 sword").find("You don't have that."), std::string::npos);
    boot_options_set_running_for_tests(BOOT_BANK_SLOTS, 0 + 1);
    vault()->slots.push_back({ 5, { record(kSwordVnum, 0) } });
    obj_to_char(read_object(0, REAL), &m_imm);
    EXPECT_NE(run("put tester 1 sword").find("That vault is full."), std::string::npos);
    EXPECT_EQ(imm_carried(0), 1);
}

TEST_F(VaultCommandTest, TakeAndPutNeedTheExactAccountName)
{
    for (const char* text : { "take tester@example.com 1 1", "take thorin 1 1", "put thorin 1 sword", "take nobody 1 1" })
        EXPECT_NE(run(text).find("Use the account name shown by 'vault <name>'."), std::string::npos) << text;
    for (const char* text : { "take tester", "take tester 1", "take tester 4 1", "take tester 1 0", "take tester 1 9",
             "put tester 1", "take tester 1 coins", "take tester 1 coins 0" })
        EXPECT_FALSE(run(text).empty()) << text; /* a message, no crash, nothing changed */
    EXPECT_TRUE(vault()->slots.empty());
}

TEST_F(VaultCommandTest, EveryVaultCommandIsLoggedOnceWithoutItsOutput)
{
    vault()->slots.push_back({ 5, { record(kSwordVnum, 0) } });
    testing::internal::CaptureStderr();
    run("tester");
    run("take tester 1 1");
    std::string logged = testing::internal::GetCapturedStderr();
    EXPECT_NE(logged.find("(GC) Forge: vault tester"), std::string::npos) << logged;
    EXPECT_NE(logged.find("(GC) Forge: vault take tester 1 1"), std::string::npos) << logged;
    EXPECT_EQ(logged.find("a bastard sword"), std::string::npos) << "output is not logged";
}
```

- [ ] **Step 2: Run to see them fail**

Run: `scripts/rots-docker.sh test --gtest_filter='VaultCommandTest.*'`
Expected: compile error, `do_vault` not declared.

- [ ] **Step 3: Implement in `banker.cpp`**

Declare the Interfaces block in `banker.h`. In `banker.cpp`:

```cpp
namespace {

std::function<bool(const std::string&, bank_account_ref*)> g_lookup_identifier;
std::function<bool(const std::string&, bank_account_ref*)> g_lookup_name;
std::function<bool(const std::string&, bank_account_ref*, int*)> g_lookup_character;

bool game_lookup_identifier(const std::string& identifier, bank_account_ref* out)
{
    account::AccountData data;
    if (!account::read_account_file_by_identifier(".", identifier, &data, nullptr))
        return false;
    *out = { data.account_name, data.normalized_email };
    return true;
}

bool game_lookup_name(const std::string& name, bank_account_ref* out)
{
    account::AccountData data;
    if (name.find('@') != std::string::npos || !account::read_account_file(".", name, &data, nullptr))
        return false;
    *out = { data.account_name, data.normalized_email };
    return true;
}

const char* side_title(int side)
{
    return side == BANK_SIDE_LIGHT ? "Light" : side == BANK_SIDE_DARK ? "Dark" : "Third";
}

std::string stored_name(const objects_json::ObjectRecord& record)
{
    int rnum = real_object(record.item_number);
    return rnum >= 0 ? obj_proto[rnum].short_description : "something";
}

} // namespace

void bank_set_account_lookups(std::function<bool(const std::string&, bank_account_ref*)> by_email_or_name,
    std::function<bool(const std::string&, bank_account_ref*)> by_name,
    std::function<bool(const std::string&, bank_account_ref*, int*)> by_character)
{
    g_lookup_identifier = by_email_or_name;
    g_lookup_name = by_name;
    g_lookup_character = by_character;
}

std::string format_vault_view(const bank_account_ref&, const std::string&, int side, const bank_vault& vault)
{
    char line[160];
    std::string coins = money_message(vault.coins, 0);
    snprintf(line, sizeof(line), "%s vault: %s, %d of %d slots\n\r", side_title(side), coins.c_str(),
        (int)vault.slots.size(), boot_option(BOOT_BANK_SLOTS));
    std::string out = line;
    if (vault.slots.empty())
        return out + "  (empty)\n\r";
    for (size_t i = 0; i < vault.slots.size(); ++i) {
        const bank_slot& slot = vault.slots[i];
        int days = bank_days_stored(slot.deposited, bank_now(), boot_option(BOOT_BANK_DAY_START_HOUR));
        snprintf(line, sizeof(line), "%2d  %-40.40s stored %d day%s\n\r", (int)(i + 1), stored_name(slot.objects[0]).c_str(),
            days, days == 1 ? "" : "s");
        out += line;
        for (size_t o = 1; o < slot.objects.size(); ++o) {
            int indent = std::min(4 + 2 * slot.objects[o].wear_pos, 30);
            snprintf(line, sizeof(line), "%*s%.*s\n\r", indent, "", 78 - indent, stored_name(slot.objects[o]).c_str());
            out += line;
        }
    }
    return out;
}
```

Then `ACMD(do_vault)`. Structure (write it out in full; every branch ends by sending one
message):

```cpp
ACMD(do_vault)
{
    char first[MAX_INPUT_LENGTH], rest[MAX_INPUT_LENGTH], buf[MAX_STRING_LENGTH];
    skip_spaces(&argument);
    half_chop(argument, first, rest);
    if (!*first) {
        send_to_char("Usage: vault <character | email | account> [1|2|3]\n\r"
                     "       vault take <account> <1|2|3> <slot>\n\r"
                     "       vault take <account> <1|2|3> coins <amount>\n\r"
                     "       vault put <account> <1|2|3> <item>\n\r"
                     "       vault put <account> <1|2|3> coins <amount>\n\r",
            ch);
        return;
    }
    snprintf(buf, sizeof(buf), "(GC) %s: vault %s", GET_NAME(ch), argument);
    mudlog(buf, BRF, (sh_int)MAX(LEVEL_GRGOD, GET_INVIS_LEV(ch)), TRUE);

    if (!str_cmp(first, "take") || !str_cmp(first, "put")) {
        vault_change(ch, !str_cmp(first, "take"), rest);
        return;
    }
    vault_view(ch, first, rest);
}
```

`vault_view(ch, identifier, side_text)`:
1. `side_text`, when present, must be exactly `1`, `2` or `3`; otherwise send the usage.
2. Resolve: identifier lookup (hook or `game_lookup_identifier`); on failure the character
   lookup (hook or the game one described above), which also gives the race and so the side;
   on failure send `No account or character by that name.`
3. Send `Account: <name> (<email>)` plus, for a character lookup, `   Character: <Name>`
   (first letter capitalised).
4. For a character: show `bank_side_for_race(race)`; if that is `BANK_SIDE_NONE` send
   `That character has no vault.` For an account: the one side asked for, or all three.
5. For each side shown: `bank_vault_open`; if null send `<Side> vault: FILE UNREADABLE`
   when the error is the unreadable one, else the error text; otherwise send
   `format_vault_view(...)`. Use `page_string` if the total passes `MAX_STRING_LENGTH`;
   otherwise `send_to_char` per side.

`vault_change(ch, take, text)`:
1. Split `text` into `<account> <side> <what...>`. Missing parts → usage.
2. Account: name lookup (hook or `game_lookup_name`) on the exact word; failure →
   `Use the account name shown by 'vault <name>'.`
3. Side must be `1`, `2` or `3` → else usage. `bank_vault_open`; null → the error text.
4. `what` starting with the word `coins`: parse the remainder with `parse_coins`, treating a
   bare number as copper (append `" copper"` when the remainder is all digits). Bad or zero →
   `How much?`
   - take: more than the vault holds → `The vault doesn't hold that much.` Otherwise add to
     `GET_GOLD(ch)`, `bank_save_character(ch)`, subtract from the vault, `bank_vault_write`,
     send `You take <money> from the vault.`
   - put: more than `GET_GOLD(ch)` → `You don't have that much.` Vault coins + amount over
     the limit → `That would pass the vault's coin limit.` Otherwise add to the vault,
     `bank_vault_write` (on failure undo and send the error), subtract from `GET_GOLD(ch)`,
     `bank_save_character(ch)`, send `You put <money> into the vault.`
5. take, otherwise: `what` must be a slot number 1..slots → else `No such slot.`
   `bank_obj_from_records`; null → `That item can't be rebuilt (missing prototype).`
   Otherwise `obj_to_char(obj, ch)`, `bank_save_character(ch)`, erase the slot,
   `bank_vault_write`, send `You take <item> from the vault.`
6. put, otherwise: `get_obj_in_list_vis(ch, what, ch->carrying)`; null →
   `You don't have that.` `!bank_obj_storable` → `The bank can't hold that.` Slots at the
   limit → `That vault is full.` Otherwise push a slot with `deposited = bank_now()`,
   `bank_vault_write` (on failure pop and send the error), send
   `You put <item> into the vault.`, `obj_from_char`, `extract_obj`,
   `bank_save_character(ch)`.

- [ ] **Step 4: Register the command**

`src/interpre.cpp`: add `ACMD(do_vault);` with the other forward declarations (or include
`mob_progs/banker.h`), and change the 253 entry added in Task 1 from `do_not_here` to
`do_vault`:

```cpp
    COMMANDO(253, POSITION_DEAD, do_vault, LEVEL_GRGOD, FALSE, 0,
        FULL_TARGET, FULL_TARGET, 0);
```

- [ ] **Step 5: Run the tests**

Run: `scripts/rots-docker.sh test --gtest_filter='VaultCommandTest.*:BankerTest.*'`
Expected: all pass.

- [ ] **Step 6: Full suite, compile, format, commit**

Run: `scripts/rots-docker.sh test` then `scripts/rots-docker.sh compile`.

```bash
cd src && clang-format -i -style=WebKit mob_progs/banker.h mob_progs/banker.cpp tests/banker_tests.cpp && cd ..
git diff --stat
git add src/mob_progs src/tests/banker_tests.cpp src/interpre.cpp
git commit -m "feat(bank): immortal vault command (view, take, put)"
```

---

### Task 8: Helps and docs

**Files:**
- Modify: `lib/text/help_tbl`, `lib/text/shap_tbl`, `docs/shape_mob.md`
- Create: `docs/systems/bank.md`

Both help files use `\n\r` line endings and a fixed entry format. Before editing, read the
entry for `SHOPKEEPER` in `lib/text/shap_tbl` (line ~1749) and one player command entry in
`lib/text/help_tbl`, and copy their exact framing (keyword line, body, terminator). Check
with `file lib/text/help_tbl` and keep the line endings byte-for-byte. Every help line is at
most 78 columns.

- [ ] **Step 1: Player helps in `lib/text/help_tbl`**

Keywords `"BALANCE" "DEPOSIT" "WITHDRAW" "BANK" "BANKER" "VAULT"` (if `VAULT` must stay free
for the immortal help, drop it here and say so in the commit message):

```
Bankers keep a vault for you. It belongs to your account, so all your
characters on the same side share it, at every banker on that side.

  balance                 What is in your vault and what it costs to take out.
  deposit <item>          Store an item you are carrying (not one you wear).
  deposit <N> gold        Store coins. Also: silver, copper.
  withdraw <item or #>    Take an item back. # is its number from 'balance'.
  withdraw <N> gold       Take coins back. Also: silver, copper.

A vault holds a set number of items and a set amount of coins; 'balance'
shows both limits. A container counts as one item however full it is, and
stays sealed while stored.

Storing coins is free. Some bankers charge a fee on items, in copper, for
each day stored; you pay it when you take the item out, from the coins you
carry first, then from your vault. Taking something out the day you stored
it is free. A bank day starts in the early morning, server time.

The bank refuses anything you could not keep when you rent.
```

- [ ] **Step 2: Immortal helps in `lib/text/help_tbl`**

`"VAULT"` (immortal section, level 97) and `"BOOTOPTIONS"` (level 100), using the forms table
of Task 7 and the list/set forms of Task 1. Include these two lines verbatim:

```
take and put need the exact account name that 'vault <name>' shows.
Every vault command is logged.
```

and for `BOOTOPTIONS`:

```
A change is written to lib/misc/game_boot_options.json at once and takes
effect at the next reboot.
```

- [ ] **Step 3: Builder helps in `lib/text/shap_tbl`**

- In the MOB2 29 program list (line ~1636) add after the 33 line:
  `34: banker. Keeps account vaults (MAN SHAPE BANKER).`
- New entry `"BANKER" "BANKERS" "BANK"`:

```
A banker is a mob with MOB_SPEC and program 34. Give it NOBASH too.
Its settings go in the options field (MOB2 42), one per line, all optional:

  hours=6-20          Opening hours, as for a barter vendor.
  fee=50              Storage fee IN COPPER, per item, per day.
  maxdays=30          The most days a fee is charged for. Needed with fee=.
  racial_markup=yes   Customers of another race than the banker pay 30%
                      more. A number from 1 to 300 sets the percentage.

fee= is 1 to 10000 copper. maxdays= is 1 to 365. Leave fee= out for a
free banker.

A bad value, or fee= without maxdays=, closes the banker and logs a
warning until it is fixed. racial_markup= without fee= only warns on /imp.

Every banker on a side opens the same vaults; a banker has no stock of
its own. Bankers refuse customers the way barter vendors do, refuse
immortals, and can't be hurt.
```

- [ ] **Step 4: Docs**

`docs/shape_mob.md`: where program 33 is described, add program 34 with the same options
table as the help above.

`docs/systems/bank.md` (use `docs/_TEMPLATE.md` for the frame): vault model and sides, file
names and location, the JSON shape (an example with one plain item and one container), the
single-copy rule, the save-order table from the spec, the fee and bank-day rules, the
settings file, the `vault` and `bootoptions` commands, and `file:line` citations into
`src/mob_progs/banker.cpp` and `src/game_boot_options.cpp`. Add its row to `docs/README.md`.

- [ ] **Step 5: Check in game and commit**

Boot (`scripts/rots-docker.sh boot`), then: `help bank`, `help vault`, `help bootoptions`,
`man shape banker`, `man shape mob2 29`. Each shows the new text, nothing wraps past 78
columns, and neighbouring entries are intact.

```bash
git diff --stat   # help files: only added lines
git add lib/text/help_tbl lib/text/shap_tbl docs/shape_mob.md docs/systems/bank.md docs/README.md
git commit -m "docs(bank): player, immortal and builder helps; system doc"
```

(`lib/text/` files that are git-ignored are not committed; list them in the final report so
the user can copy them to the test server.)

---

### Task 9: In-game verification, soak, build-change record

**Files:**
- Create: `testing/smoke_bank.py` (local, untracked — like the other `testing/smoke_*.py`)
- Create: `reports/2026-09-30-player-bank/build-env-changes.md` (local, git-ignored)

Before writing the rig, read the memories `live_world_test_boot_recipe`,
`test_server_user_account_setup`, `rots_debug_test_account`, `rots_test_char_base_setup`,
`smoke_test_in_game_characters_must_quit`, `dmg_bench_harness_relogin_trap`,
`feedback_imm_invis_when_testing` and `feedback_test_server_port_4071`, and read
`testing/smoke_vendor.py` for the harness pattern. Reuse; do not improvise logins.

- [ ] **Step 1: Test world**

In a scratch copy of the test lib: two banker mobs in room 1120 (the test arena) and one in a
dark-side room —
- #1194 free banker, human, NOBASH, no options;
- #1195 fee banker, human, NOBASH, `fee=50` / `maxdays=30` / `racial_markup=yes` /
  `hours=6-20`;
- #1196 broken banker, `fee=5` only (for the boot warning);
- an Uruk banker with `fee=50` / `maxdays=30`.

Port 4071 after a light in-use check; another port if busy.

- [ ] **Step 2: `testing/smoke_bank.py`** — each numbered line is one PASS/FAIL check:

1. Boot log has `MOB ERROR: mobile #1196: fee without maxdays - banker disabled` and no
   SYSERR from the bank.
2. `balance` at #1194: empty vault text, limits 10 slots / 1000 gold.
3. `deposit sword`, `balance` lists it, `vault_light.json` exists in the account folder and
   contains the sword's vnum.
4. A second character of the same account and side sees the sword; an Uruk character of the
   account at the Uruk banker does not, and `vault_dark.json` is separate.
5. A filled backpack: deposit, `balance` shows `(sealed, N inside)`, withdraw, contents and
   `stat` weights are as before.
6. Coins: deposit 5 gold, `score`/`balance` agree; deposit past 1000 gold is cut short with
   the refused message; withdraw is free.
7. Fee: set the vault file's `deposited` back three days while the server is **down**, boot,
   `balance` at #1195 shows `1 silver and 50 copper`; a Dwarf sees `1 silver and 95 copper`;
   withdraw takes purse first, then vault, and says so.
8. Can't pay: empty purse and vault coins → refused, item still listed.
9. Unrentable item (a key) and a pack holding a key: both refused.
10. Worn item: `deposit` of a wielded sword says `You don't have that.`
11. Closed hours at #1195 (`set time` to 22): refused; open again at 8.
12. Protection: `kill`, `bash`, poison and blinding dust on a banker; banker unhurt and
    still serving. A mortal victim is used for the control (gods can't fight).
13. Immortal (`invis 100`): `vault <character>`, `vault <account>`, `vault <account> 2`,
    `vault take`, `vault put`, both coin forms; the player's next `balance` shows each
    change; the syslog has one `(GC)` line per command and no item names from the output.
14. Crash order: deposit, then `kill -9` the server straight away, boot: the item is in the
    vault. Withdraw, `kill -9`, boot: the item is on the character or in both, never
    neither.
15. Unreadable file: write junk into `vault_third.json` while down; boot; that side is
    refused at the banker and shown `FILE UNREADABLE` by `vault`; the file is byte-identical
    afterwards; the other sides work.
16. `bootoptions`: list; `bootoptions bank_slots 12`; list shows `After reboot 12`; reboot;
    `balance` says `of 12`; `bootoptions bank_slots 500` refused; the JSON file is valid.
17. `/imp` on a banker without NOBASH and on one with `racial_markup=yes` only: both
    warnings, to the builder only.
18. The account commands still work with vault files present: `account show`, `account
    index`, the roster, login/logout of a second character, `tools/account_smoke.py`.
19. Every test character quits at the end.

`--soak-minutes N` repeats checks 3, 5, 6 and 13 in a loop and records RSS at the start and
end.

- [ ] **Step 3: Run it red, then green**

Red check: run the script against a server built from `upstream/release-frodo` (no bank).
Expected: checks 2 onward FAIL (commands answer "you can't do that here"), proving the rig
detects absence. Then against this branch. Expected: all PASS.

- [ ] **Step 4: Final build runs**

- `scripts/rots-docker.sh test` — expected: only the known 6 32-bit failures; record the
  pass count.
- `testing/smoke_vendor.py`, `testing/smoke_specialchar.py`, `testing/scenarios.py` — still
  green (vendor code was touched in Task 2).
- `testing/smoke_bank.py --soak-minutes 10` — all PASS, RSS steady against a 10-minute
  control soak without bank traffic.
- Stop every test server this session started.

- [ ] **Step 5: Build-change record for the user**

`reports/2026-09-30-player-bank/build-env-changes.md`, one entry each, for individual review:
- A. `src/Makefile`: `game_boot_options.cpp` and `mob_progs/banker.cpp` in the object list.
- B. `src/CMakeLists.txt`: the same two sources.
- C. `src/CMakeLists.txt`: two new test files.
- D. New runtime file `lib/misc/game_boot_options.json` (created by `bootoptions`, not
  shipped; absent = defaults). Check whether the deploy and backup scripts treat `lib/misc`
  as data that survives a deploy, and report what you find rather than changing them.
- E. New files in account folders (`vault_*.json`): same question for backups.
- F. Two new commands, numbers 253 and 254.

- [ ] **Step 6: Report**

No push, no PR. Report to the user: what was built, test counts, smoke results, the
build-change record path, the test server details for their hand test, and a "Still open:"
list (at least: user hand test; build-change walkthrough; refresh-on-withdraw waiting on
PR #343; proposal page example uses comma money text while the game prints "and").

---

## Self-review notes

- **Spec coverage:** vaults/sides (T2, T4, T6); storage and single copy (T3, T4); player
  commands (T5, T6); fees and bank day (T2, T6); banker setup, strict and soft checks,
  refusals, protection (T2, T5); settings file and `bootoptions` (T1); `vault` view, take,
  put, logging (T7); save order (T6, T7, smoke 14); helps and docs (T8); testing section
  (T1-T7 unit, T9 in game). Refresh-on-withdraw is deliberately left as a marked call site
  (T5, `bank_obj_from_records`).
- **Spec corrections found while planning:** the spec's outline cites
  `src/act_wiz.cpp:2964` and `src/shapemob.cpp:1284` as places that list program 33. They
  are not (a `set` field and editor field 33); nothing is needed there. The real program-33
  touch points are `spec_ass.cpp`, `db.cpp:485` and `shapemob.cpp:2067/2180/2286`.
- **Two choices the spec left open, made here:**
  1. Coin amounts are typed as `<N> gold|silver|copper` (`coins` = copper). CONFIRMED by the
     user 2026-09-30: players name the coin and never have to work an amount out in copper.
     The spec now says the same.
  2. Money is printed with the game's own `money_message` ("1 silver and 50 copper").
     CONFIRMED by the user 2026-09-30: game text is meant to read as a story. The spec's
     example now matches.
- **One spec line that cannot be met as written:** "if the first write fails, the
  transaction is refused" holds for the vault file. A character save has no failure result,
  so on withdraw the vault write always follows; the outcome is still dupe-not-loss.
- **A broken settings file** (not valid JSON, or a non-number value) falls back to *all*
  defaults with one warning, because the JSON reader cannot resume after a bad value. An
  out-of-range number only resets that one setting.
