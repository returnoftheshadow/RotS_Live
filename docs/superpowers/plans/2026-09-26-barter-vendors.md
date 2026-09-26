# Barter Vendors Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add mob program 33, a barter vendor that sells store-room stock for item
currencies. It's configured through a new persisted multi-line mob **options** field.

**Architecture:**
- **`src/mob_options.{h,cpp}`**: general text-field helpers (lookup, storable check, file
  read/write) and a *pure* vendor parser. Game lookups are injected as callbacks, so it's
  fully unit-testable.
- **`src/mob_progs/passive.{h,cpp}`**, a new source subfolder, holds the vendor:
  - a registry of parsed configs keyed by mob rnum, rebuilt at boot and on mob-editor
    implement;
  - a pure list formatter;
  - `SPECIAL(barter_vendor)`.
- **The field itself** is `char* mob_options` in `char_special_data`. That struct is never
  saved to player files, and loaded mobs share the prototype's pointer.

**Tech Stack:** C++17 (built `-m32`), GNU make (`src/Makefile`, canonical), CMake (IDE and
tests), GoogleTest, Docker i386 toolchain via `scripts/rots-docker.sh`.

**Spec:** `docs/superpowers/specs/2026-09-26-item-currency-shopkeeper-design.md` (approved
2026-09-26). Read it before starting any task.

## Global Constraints

- Program number: **33** (`PROG_BARTER_VENDOR`). Mob editor field: **42** ("options").
- **Options field:**
  - multi-line text, max **4000** characters;
  - must not contain `~`;
  - first non-space character must not be `#` or `$`;
  - written to the mob file **only when non-empty** (keeps the rollback blast radius to vendor
    mobs).
- **Limits:** **30** price lines per vendor, **4** currencies per line, quantity **1–100**.
- **Validation:**
  - Bad `price` line → **skipped + warning**, the rest works.
  - Missing or bad `store=` → vendor disabled.
  - Bad `hours=` → vendor disabled (**strict**).
  - Validation only checks that vnums exist. It never checks zone data.
- **Warnings (house style):** `MOB ERROR: mobile #<vnum>, options line <n>: <what> - <effect>`,
  or `MOB ERROR: mobile #<vnum>: <what> - <effect>` for whole-config problems.
  - Sent via `mudlog(buf, NRM, LEVEL_AREAGOD, TRUE)`, plus `send_to_char` to the builder only
    when `!mudlog_reaches(builder, LEVEL_AREAGOD, NRM)`.
  - Report **every** occurrence; no once-per-boot suppression.
- **Messages:** all player-facing message text we write is ≤ 78 columns. The `list` name
  column is capped at **38**, so costs start at column ≤ 44.
- **Build and environment files** (`src/Makefile`, `src/CMakeLists.txt`, Docker, deploy,
  format) are changed **only after the user approves each change individually**. The steps
  that touch them say so, and the executor must stop and ask.
- **Formatting:** `cd src && clang-format -i -style=WebKit <changed files>` only. **Never**
  bare `make format`.
- **Source line endings:** `src/` mixes CRLF and LF. Preserve each file's existing endings
  when editing, and check `git diff --stat` for whole-file churn.
- **`#define`s:** grep the whole tree (`grep -rn "NAME" src/`, **not** `-w`-only) before
  adding any `#define`, to avoid silent redefinition.
- **Commits** end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`. No
  `Claude-Session` or "Generated with" lines. **No push, no PR** unless the user asks.

## Review Focus

1. **A currency item that is a container with things inside.** Paying must never destroy the
   contents: only **empty** copies count as payment (Task 6, test in Task 8).
2. **Options text starting with `#`/`$` or containing `~`.** Saved as-is, it would corrupt the
   mob file on the next boot. The editor must reject it and keep the old value (Task 4).
3. **World files with `\n\r` line endings.** The loader's peek must skip `\r`, and the parser
   must strip `\r` from every line (Tasks 1 and 2).
4. **Two vendors in the same room.** `list`/`buy` is answered by the first vendor the room
   list reaches, as with the old shops. A player must get one coherent answer, never two
   interleaved lists (Task 8).
5. **List numbers shifting.** When a `deduct` item sells out between one player's `list` and
   `buy 3`, the number means the **current** list. The purchase must show exactly what was
   bought and what was paid, so nothing is silent (Task 6 message, Task 8 check).

---

## File Structure

| File | Status | Responsibility |
|---|---|---|
| `src/mob_options.h/.cpp` | Create | General options helpers + pure vendor parser (no game globals) |
| `src/mob_progs/passive.h/.cpp` | Create | Vendor registry, boot/implement hooks, list formatter, `SPECIAL(barter_vendor)` |
| `src/tests/mob_options_tests.cpp` | Create | Tests for helpers, file read/write, parser, hours |
| `src/tests/barter_vendor_tests.cpp` | Create | Tests for list formatter and shortfall helper |
| `src/structs.h` | Modify | `char* mob_options` in `char_special_data` |
| `src/db.cpp` | Modify | Load options after the mob record; free for PCs/unindexed mobs; call `vendor_config_boot()` |
| `src/shapemob.cpp` | Modify | Field 42 edit/display/help; `write_proto`, `load_proto`, `free_proto`, `implement_proto`; check on save |
| `src/spec_ass.cpp` | Modify | Program 33 in both tables; `spec_pro_message[]` gets index 33 |
| `src/interpre.h` | Modify | `CMD_GIVE 54` |
| `src/act_wiz.cpp` | Modify | Show options in mob `stat` |
| `src/Makefile`, `src/CMakeLists.txt` | Modify (**user-approved only**) | New objects/sources, `clean` covers `mob_progs/*.o` |
| `docs/data-formats/world-files.md`, `docs/systems/barter-vendors.md` | Modify/Create | Format + builder guide |

---

### Task 1: Options field helpers (`mob_options`)

**Files:**
- Create: `src/mob_options.h`, `src/mob_options.cpp`, `src/tests/mob_options_tests.cpp`
- Modify (**after user approval**): `src/Makefile`, `src/CMakeLists.txt`

**Interfaces:**
- Produces:
  - `constexpr int MOB_OPTIONS_MAX = 4000;`
  - `bool mob_option_find(const char* options, const char* key, std::string* value);`
  - `bool mob_options_storable(const char* text, const char** why);`
  - `char* read_mob_options(FILE* f, char* context);` returns a `CREATE`-allocated string,
    or `nullptr` if the next token starts a record or the text is empty.
  - `void write_mob_options(FILE* f, const char* options);` writes nothing when the text is
    null or empty.

- [ ] **Step 1: Ask the user to approve build change A.** Show them exactly this and wait for
  a yes:
  - `src/Makefile`: append `mob_options.o` to `OBJFILES` (after `mob_csv_extract.o`), and add:
    ```make
    mob_options.o : mob_options.cpp mob_options.h db.h
    	$(CC) -c $(CFLAGS) mob_options.cpp
    ```
  - `src/CMakeLists.txt`:
    - add `mob_options.cpp` to `ROTS_SERVER_SOURCES` (alphabetical, after `mob_csv_extract.cpp`);
    - add `tests/mob_options_tests.cpp` to `ROTS_TEST_SOURCES`.

- [ ] **Step 2: Write the failing tests** in `src/tests/mob_options_tests.cpp`:

```cpp
#include "../mob_options.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <cstring>
#include <string>

namespace {

FILE* file_with(const char* text)
{
    FILE* f = tmpfile();
    fputs(text, f);
    rewind(f);
    return f;
}

} // namespace

TEST(MobOptionFind, FindsBareKeyAndKeyValue)
{
    std::string value;
    EXPECT_TRUE(mob_option_find("conj\n\rstore=12\n\r", "conj", &value));
    EXPECT_EQ(value, "");
    EXPECT_TRUE(mob_option_find("conj\n\rstore = 12 \n\r", "store", &value));
    EXPECT_EQ(value, "12");
    EXPECT_FALSE(mob_option_find("storeroom=1\n\r", "store", &value));
    EXPECT_FALSE(mob_option_find(nullptr, "store", &value));
    EXPECT_FALSE(mob_option_find("// conj\n\r", "// conj", &value)); // comments never match
}

TEST(MobOptionsStorable, RejectsTildeLeadingHashDollarAndOverLength)
{
    const char* why = nullptr;
    EXPECT_TRUE(mob_options_storable("store=1\n\rprice 1 2x3\n\r", &why));
    EXPECT_TRUE(mob_options_storable(nullptr, &why));
    EXPECT_FALSE(mob_options_storable("store=1~", &why));
    EXPECT_FALSE(mob_options_storable("  #store=1", &why));
    EXPECT_FALSE(mob_options_storable("\n\r$", &why));
    EXPECT_FALSE(mob_options_storable(std::string(MOB_OPTIONS_MAX + 1, 'a').c_str(), &why));
    EXPECT_TRUE(mob_options_storable(std::string(MOB_OPTIONS_MAX, 'a').c_str(), &why));
}

TEST(ReadMobOptions, NoOptionsWhenNextRecordFollows)
{
    char ctx[] = "test";
    FILE* f = file_with("\n\r#1235\n");
    EXPECT_EQ(read_mob_options(f, ctx), nullptr);
    char next[16];
    ASSERT_EQ(fscanf(f, "%15s", next), 1);
    EXPECT_STREQ(next, "#1235"); // the next record is left unread
    fclose(f);

    f = file_with("  \r\n$~\n");
    EXPECT_EQ(read_mob_options(f, ctx), nullptr);
    fclose(f);

    f = file_with("");
    EXPECT_EQ(read_mob_options(f, ctx), nullptr);
    fclose(f);
}

TEST(ReadMobOptions, ReadsTextThenStopsBeforeNextRecord)
{
    char ctx[] = "test";
    FILE* f = file_with("store=5\n\rprice 1 2x3~\n\r#1235\n");
    char* text = read_mob_options(f, ctx);
    ASSERT_NE(text, nullptr);
    std::string value;
    EXPECT_TRUE(mob_option_find(text, "store", &value));
    EXPECT_EQ(value, "5");
    char next[16];
    ASSERT_EQ(fscanf(f, "%15s", next), 1);
    EXPECT_STREQ(next, "#1235");
    fclose(f);
}

TEST(WriteMobOptions, WritesOnlyNonEmptyAndRoundTrips)
{
    char ctx[] = "test";
    FILE* f = tmpfile();
    write_mob_options(f, nullptr);
    write_mob_options(f, "");
    EXPECT_EQ(ftell(f), 0);
    write_mob_options(f, "store=5\n\rhours=6-20");
    fputs("#2\n", f);
    rewind(f);
    char* text = read_mob_options(f, ctx);
    ASSERT_NE(text, nullptr);
    std::string value;
    EXPECT_TRUE(mob_option_find(text, "hours", &value));
    EXPECT_EQ(value, "6-20");
    fclose(f);
}
```

- [ ] **Step 3: Run the tests to verify they fail**

Run: `scripts/rots-docker.sh test --gtest_filter='MobOption*:ReadMobOptions.*:WriteMobOptions.*'`
Expected: build failure, `mob_options.h: No such file`.

- [ ] **Step 4: Implement.** `src/mob_options.h`:

```cpp
#ifndef MOB_OPTIONS_H
#define MOB_OPTIONS_H

#include <cstdio>
#include <functional>
#include <string>
#include <vector>

/* Mob options: a persisted multi-line text field for mob programs' settings
 * (one setting per line: "key", "key=value", or a keyword line like "price ...").
 * A line starting with // is a comment and is ignored. */

constexpr int MOB_OPTIONS_MAX = 4000;

/* True if a line is exactly `key` or `key=value` (spaces around '=' allowed);
 * *value receives the trimmed text after '=' (empty for a bare key). */
bool mob_option_find(const char* options, const char* key, std::string* value);

/* False, with a reason in *why, if saving `text` would corrupt a mob file. */
bool mob_options_storable(const char* text, const char** why);

/* Reads the optional options string that may follow a mob record. Returns
 * nullptr, leaving the stream at the next token, if that token starts the
 * next record ('#' or '$') or the file ends, or if the text is empty. */
char* read_mob_options(FILE* f, char* context);

/* Writes the options string; writes nothing for null/empty text. */
void write_mob_options(FILE* f, const char* options);

#endif
```

`src/mob_options.cpp`:

```cpp
#include "mob_options.h"

#include "db.h"
#include "utils.h"

#include <cctype>
#include <cstring>

namespace mob_options_detail {

std::string trim(const std::string& s)
{
    const char* space = " \t\r\n";
    size_t b = s.find_first_not_of(space);
    if (b == std::string::npos)
        return "";
    return s.substr(b, s.find_last_not_of(space) - b + 1);
}

std::vector<std::string> split_lines(const char* text)
{
    std::vector<std::string> lines;
    if (!text)
        return lines;
    std::string current;
    for (const char* p = text; *p; ++p) {
        if (*p == '\n') {
            lines.push_back(current);
            current.clear();
        } else if (*p != '\r') {
            current += *p;
        }
    }
    if (!current.empty())
        lines.push_back(current);
    return lines;
}

} // namespace mob_options_detail

using mob_options_detail::split_lines;
using mob_options_detail::trim;

bool mob_option_find(const char* options, const char* key, std::string* value)
{
    for (const std::string& raw : split_lines(options)) {
        std::string line = trim(raw);
        if (line.compare(0, 2, "//") == 0) /* comment */
            continue;
        size_t eq = line.find('=');
        std::string name = trim(eq == std::string::npos ? line : line.substr(0, eq));
        if (name != key)
            continue;
        if (value)
            *value = eq == std::string::npos ? "" : trim(line.substr(eq + 1));
        return true;
    }
    return false;
}

bool mob_options_storable(const char* text, const char** why)
{
    if (!text)
        return true;
    if (strlen(text) > (size_t)MOB_OPTIONS_MAX) {
        *why = "options are too long (max 4000 characters)";
        return false;
    }
    if (strchr(text, '~')) {
        *why = "options can't contain ~";
        return false;
    }
    const char* p = text;
    while (*p && isspace((unsigned char)*p))
        ++p;
    if (*p == '#' || *p == '$') {
        *why = "options can't start with # or $";
        return false;
    }
    return true;
}

char* read_mob_options(FILE* f, char* context)
{
    int c;
    do {
        c = fgetc(f);
    } while (c != EOF && isspace(c));
    if (c == EOF)
        return nullptr;
    ungetc(c, f);
    if (c == '#' || c == '$')
        return nullptr;
    char* text = fread_string(f, context);
    if (text && !*text) {
        RELEASE(text);
        return nullptr;
    }
    return text;
}

void write_mob_options(FILE* f, const char* options)
{
    if (!options || !*options)
        return;
    fprintf(f, "%s~\n", options);
}
```

(`split_lines`/`trim` live in a named `mob_options_detail` namespace, not an anonymous one,
because Task 2 reuses them.)

- [ ] **Step 5: Apply build change A** exactly as approved in Step 1.

- [ ] **Step 6: Run the tests to verify they pass**

Run: `scripts/rots-docker.sh test --gtest_filter='MobOption*:ReadMobOptions.*:WriteMobOptions.*'`
Expected: all PASS. Also run `scripts/rots-docker.sh compile`; it must link.

- [ ] **Step 7: Format and commit**

```bash
cd src && clang-format -i -style=WebKit mob_options.h mob_options.cpp tests/mob_options_tests.cpp && cd ..
git add src/mob_options.h src/mob_options.cpp src/tests/mob_options_tests.cpp src/Makefile src/CMakeLists.txt
git commit -m "feat(mob_options): options text helpers and file read/write

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 2: Vendor settings parser and hours

**Files:**
- Modify: `src/mob_options.h`, `src/mob_options.cpp`, `src/tests/mob_options_tests.cpp`

**Interfaces:**
- Consumes: `mob_options_detail::trim`, `mob_options_detail::split_lines` (Task 1).
- Produces:

```cpp
constexpr int VENDOR_MAX_PRICE_LINES = 30;
constexpr int VENDOR_MAX_CURRENCIES = 4;
constexpr int VENDOR_MAX_QTY = 100;

struct vendor_cost { int obj_vnum; int qty; };
struct vendor_price { int item_vnum; std::vector<vendor_cost> costs; bool deduct; int line; };
struct vendor_hours_window { int open; int close; }; /* open at `open`, closed from `close` */
struct vendor_config {
    int store_vnum = -1;
    bool store_ok = false;
    bool hours_ok = true;
    std::vector<vendor_hours_window> hours; /* empty = always open */
    std::vector<vendor_price> prices;       /* in options order */
    bool usable() const { return store_ok && hours_ok; }
};
struct vendor_problem { int line; std::string text; }; /* line 0 = whole config */
struct vendor_lookups {
    std::function<bool(int)> obj_exists;
    std::function<bool(int)> room_exists;
};

vendor_config parse_vendor_options(const char* text, const vendor_lookups& lookups,
    std::vector<vendor_problem>* problems);
bool vendor_hours_parse(const std::string& value, std::vector<vendor_hours_window>* out);
bool vendor_is_open(const vendor_config& config, int hour);
```

- [ ] **Step 1: Write the failing tests**. Append to `src/tests/mob_options_tests.cpp`:

```cpp
namespace {

vendor_lookups everything_exists()
{
    vendor_lookups l;
    l.obj_exists = [](int v) { return v != 9999; };
    l.room_exists = [](int v) { return v != 9998; };
    return l;
}

std::vector<std::string> problem_texts(const std::vector<vendor_problem>& problems)
{
    std::vector<std::string> out;
    for (const vendor_problem& p : problems)
        out.push_back(std::to_string(p.line) + ": " + p.text);
    return out;
}

} // namespace

TEST(VendorParse, GoodConfig)
{
    std::vector<vendor_problem> problems;
    vendor_config c = parse_vendor_options(
        "store=12345\n\rhours=6-12,14-20\n\rprice 5001 2222x1 3333x2 deduct\n\rprice 5002 3333x4\n\r",
        everything_exists(), &problems);
    EXPECT_TRUE(problems.empty()) << ::testing::PrintToString(problem_texts(problems));
    EXPECT_TRUE(c.usable());
    EXPECT_EQ(c.store_vnum, 12345);
    ASSERT_EQ(c.prices.size(), 2u);
    EXPECT_EQ(c.prices[0].item_vnum, 5001);
    ASSERT_EQ(c.prices[0].costs.size(), 2u);
    EXPECT_EQ(c.prices[0].costs[1].obj_vnum, 3333);
    EXPECT_EQ(c.prices[0].costs[1].qty, 2);
    EXPECT_TRUE(c.prices[0].deduct);
    EXPECT_FALSE(c.prices[1].deduct);
    EXPECT_EQ(c.prices[1].line, 4);
}

TEST(VendorParse, StoreMissingOrBadDisables)
{
    std::vector<vendor_problem> problems;
    EXPECT_FALSE(parse_vendor_options("price 1 2x1", everything_exists(), &problems).usable());
    EXPECT_EQ(problem_texts(problems).back(), "0: store missing - vendor disabled");

    problems.clear();
    EXPECT_FALSE(parse_vendor_options("store=9998", everything_exists(), &problems).usable());
    EXPECT_EQ(problem_texts(problems)[0], "1: store room vnum 9998 not found - vendor disabled");

    problems.clear();
    EXPECT_FALSE(parse_vendor_options("store=abc", everything_exists(), &problems).usable());
    EXPECT_EQ(problem_texts(problems)[0], "1: bad store - vendor disabled");
}

TEST(VendorParse, BadHoursDisablesStrictly)
{
    const char* bad[] = { "hours=6", "hours=24-2", "hours=5-5", "hours=a-b", "hours=6-20,", "hours=" };
    for (const char* line : bad) {
        std::vector<vendor_problem> problems;
        std::string text = std::string("store=1\n\r") + line;
        vendor_config c = parse_vendor_options(text.c_str(), everything_exists(), &problems);
        EXPECT_FALSE(c.usable()) << line;
        ASSERT_FALSE(problems.empty()) << line;
        EXPECT_EQ(problems[0].text, "bad hours - vendor disabled") << line;
    }
}

TEST(VendorParse, BadPriceLinesAreSkippedOthersKept)
{
    std::vector<vendor_problem> problems;
    vendor_config c = parse_vendor_options(
        "store=1\n\r"
        "price 10 20x1\n\r"                     // 2 ok
        "price 11\n\r"                          // 3 no costs
        "price 12 20x0\n\r"                     // 4 qty low
        "price 13 20x101\n\r"                   // 5 qty high
        "price 14 20x1 21x1 22x1 23x1 24x1\n\r" // 6 five currencies
        "price 15 9999x1\n\r"                   // 7 unknown currency
        "price 9999 20x1\n\r"                   // 8 unknown item
        "price 10 20x2\n\r"                     // 9 duplicate item
        "price 16 20x1 20x2\n\r"                // 10 currency twice
        "price 17 20X1\n\r"                     // 11 bad token
        "price 18 deduct 20x1\n\r"              // 12 deduct not last
        "bogus=1\n\r",                          // 13 unknown
        everything_exists(), &problems);
    EXPECT_TRUE(c.usable());
    ASSERT_EQ(c.prices.size(), 1u);
    EXPECT_EQ(c.prices[0].item_vnum, 10);
    std::vector<std::string> expected = {
        "3: price: bad format - line skipped",
        "4: price: quantity 0 out of range - line skipped",
        "5: price: quantity 101 out of range - line skipped",
        "6: price: more than 4 currencies - line skipped",
        "7: price: object vnum 9999 not found - line skipped",
        "8: price: object vnum 9999 not found - line skipped",
        "9: price: duplicate item vnum 10 - line skipped",
        "10: price: currency vnum 20 listed twice - line skipped",
        "11: price: bad format - line skipped",
        "12: price: bad format - line skipped",
        "13: unknown setting - line ignored",
    };
    EXPECT_EQ(problem_texts(problems), expected);
}

TEST(VendorParse, ThirtyLineLimit)
{
    std::string text = "store=1\n\r";
    for (int i = 0; i < 31; ++i)
        text += "price " + std::to_string(100 + i) + " 20x1\n\r";
    std::vector<vendor_problem> problems;
    vendor_config c = parse_vendor_options(text.c_str(), everything_exists(), &problems);
    EXPECT_EQ(c.prices.size(), 30u);
    ASSERT_EQ(problems.size(), 1u);
    EXPECT_EQ(problem_texts(problems)[0], "32: price: more than 30 lines - line skipped");
}

TEST(VendorParse, CrLfAndBlankLinesAndDuplicateSettings)
{
    std::vector<vendor_problem> problems;
    vendor_config c = parse_vendor_options("\r\n  store = 7 \r\n\r\nstore=8\r\nprice 1 2x3 \r\n",
        everything_exists(), &problems);
    EXPECT_EQ(c.store_vnum, 7);
    ASSERT_EQ(c.prices.size(), 1u);
    EXPECT_EQ(c.prices[0].line, 5);
    EXPECT_EQ(problem_texts(problems), std::vector<std::string> { "4: duplicate store - line ignored" });
}

TEST(VendorParse, CommentLinesAreSkippedAndNeverWarned)
{
    std::vector<vendor_problem> problems;
    vendor_config c = parse_vendor_options(
        "// winter stock\n\rstore=1\n\r  // price 1 9999x1 (off for now)\n\rprice 2 3x1\n\r",
        everything_exists(), &problems);
    EXPECT_TRUE(problems.empty()) << ::testing::PrintToString(problem_texts(problems));
    ASSERT_EQ(c.prices.size(), 1u);
    EXPECT_EQ(c.prices[0].line, 4);
}

TEST(VendorHours, OpenWindows)
{
    vendor_config c;
    ASSERT_TRUE(vendor_hours_parse("6-12,14-20", &c.hours));
    EXPECT_FALSE(vendor_is_open(c, 5));
    EXPECT_TRUE(vendor_is_open(c, 6));
    EXPECT_TRUE(vendor_is_open(c, 11));
    EXPECT_FALSE(vendor_is_open(c, 12));
    EXPECT_FALSE(vendor_is_open(c, 13));
    EXPECT_TRUE(vendor_is_open(c, 14));
    EXPECT_FALSE(vendor_is_open(c, 20));

    ASSERT_TRUE(vendor_hours_parse("20-4", &c.hours));
    EXPECT_TRUE(vendor_is_open(c, 20));
    EXPECT_TRUE(vendor_is_open(c, 23));
    EXPECT_TRUE(vendor_is_open(c, 0));
    EXPECT_TRUE(vendor_is_open(c, 3));
    EXPECT_FALSE(vendor_is_open(c, 4));
    EXPECT_FALSE(vendor_is_open(c, 12));

    c.hours.clear();
    EXPECT_TRUE(vendor_is_open(c, 12)); // no hours = always open
}
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `scripts/rots-docker.sh test --gtest_filter='VendorParse.*:VendorHours.*'`
Expected: compile errors, `parse_vendor_options` not declared.

- [ ] **Step 3: Implement.** Add the declarations from **Interfaces** to `src/mob_options.h`,
  before `#endif`. Append to `src/mob_options.cpp`:

```cpp
#include <cstdlib>
#include <set>
#include <sstream>

namespace {

bool parse_int(const std::string& s, int* out)
{
    if (s.empty() || s.size() > 9)
        return false;
    for (char c : s)
        if (!isdigit((unsigned char)c))
            return false;
    *out = atoi(s.c_str());
    return true;
}

std::vector<std::string> split_words(const std::string& s)
{
    std::istringstream in(s);
    std::vector<std::string> words;
    std::string w;
    while (in >> w)
        words.push_back(w);
    return words;
}

bool parse_cost(const std::string& token, vendor_cost* out)
{
    size_t x = token.find('x');
    if (x == std::string::npos)
        return false;
    return parse_int(token.substr(0, x), &out->obj_vnum) && parse_int(token.substr(x + 1), &out->qty);
}

} // namespace

bool vendor_hours_parse(const std::string& value, std::vector<vendor_hours_window>* out)
{
    out->clear();
    std::string v = trim(value);
    if (v.empty())
        return false;
    size_t start = 0;
    for (;;) {
        size_t comma = v.find(',', start);
        std::string part = trim(v.substr(start, comma == std::string::npos ? std::string::npos : comma - start));
        size_t dash = part.find('-');
        int open, close;
        if (dash == std::string::npos || !parse_int(trim(part.substr(0, dash)), &open)
            || !parse_int(trim(part.substr(dash + 1)), &close) || open > 23 || close > 23 || open == close)
            return false;
        out->push_back({ open, close });
        if (comma == std::string::npos)
            return true;
        start = comma + 1;
    }
}

bool vendor_is_open(const vendor_config& config, int hour)
{
    if (config.hours.empty())
        return true;
    for (const vendor_hours_window& w : config.hours) {
        bool open = w.open < w.close ? (hour >= w.open && hour < w.close) : (hour >= w.open || hour < w.close);
        if (open)
            return true;
    }
    return false;
}

vendor_config parse_vendor_options(const char* text, const vendor_lookups& lookups,
    std::vector<vendor_problem>* problems)
{
    vendor_config config;
    bool saw_store = false, saw_hours = false;
    std::set<int> priced_items;
    auto problem = [&](int line, const std::string& what) {
        if (problems)
            problems->push_back({ line, what });
    };

    int line_no = 0;
    for (const std::string& raw : split_lines(text)) {
        ++line_no;
        std::string line = trim(raw);
        if (line.empty() || line.compare(0, 2, "//") == 0) /* blank or comment */
            continue;
        std::vector<std::string> words = split_words(line);

        if (words[0] == "price") {
            vendor_price price { 0, {}, false, line_no };
            size_t end = words.size();
            if (end > 1 && words[end - 1] == "deduct") {
                price.deduct = true;
                --end;
            }
            bool ok = end >= 3 && parse_int(words[1], &price.item_vnum);
            for (size_t i = 2; ok && i < end; ++i) {
                vendor_cost cost;
                ok = parse_cost(words[i], &cost);
                if (ok)
                    price.costs.push_back(cost);
            }
            if (!ok) {
                problem(line_no, "price: bad format - line skipped");
                continue;
            }
            if ((int)price.costs.size() > VENDOR_MAX_CURRENCIES) {
                problem(line_no, "price: more than 4 currencies - line skipped");
                continue;
            }
            std::string bad;
            std::set<int> seen;
            for (const vendor_cost& cost : price.costs) {
                if (cost.qty < 1 || cost.qty > VENDOR_MAX_QTY) {
                    bad = "price: quantity " + std::to_string(cost.qty) + " out of range - line skipped";
                    break;
                }
                if (!seen.insert(cost.obj_vnum).second) {
                    bad = "price: currency vnum " + std::to_string(cost.obj_vnum) + " listed twice - line skipped";
                    break;
                }
            }
            if (bad.empty() && !lookups.obj_exists(price.item_vnum))
                bad = "price: object vnum " + std::to_string(price.item_vnum) + " not found - line skipped";
            for (size_t i = 0; bad.empty() && i < price.costs.size(); ++i)
                if (!lookups.obj_exists(price.costs[i].obj_vnum))
                    bad = "price: object vnum " + std::to_string(price.costs[i].obj_vnum) + " not found - line skipped";
            if (bad.empty() && priced_items.count(price.item_vnum))
                bad = "price: duplicate item vnum " + std::to_string(price.item_vnum) + " - line skipped";
            if (bad.empty() && (int)config.prices.size() >= VENDOR_MAX_PRICE_LINES)
                bad = "price: more than 30 lines - line skipped";
            if (!bad.empty()) {
                problem(line_no, bad);
                continue;
            }
            priced_items.insert(price.item_vnum);
            config.prices.push_back(price);
            continue;
        }

        size_t eq = line.find('=');
        std::string key = trim(eq == std::string::npos ? line : line.substr(0, eq));
        std::string value = eq == std::string::npos ? "" : trim(line.substr(eq + 1));
        if (key == "store" && eq != std::string::npos) {
            if (saw_store) {
                problem(line_no, "duplicate store - line ignored");
                continue;
            }
            saw_store = true;
            int vnum;
            if (!parse_int(value, &vnum)) {
                problem(line_no, "bad store - vendor disabled");
                continue;
            }
            config.store_vnum = vnum;
            if (!lookups.room_exists(vnum)) {
                problem(line_no, "store room vnum " + std::to_string(vnum) + " not found - vendor disabled");
                continue;
            }
            config.store_ok = true;
        } else if (key == "hours" && eq != std::string::npos) {
            if (saw_hours) {
                problem(line_no, "duplicate hours - line ignored");
                continue;
            }
            saw_hours = true;
            if (!vendor_hours_parse(value, &config.hours)) {
                config.hours.clear();
                config.hours_ok = false;
                problem(line_no, "bad hours - vendor disabled");
            }
        } else {
            problem(line_no, "unknown setting - line ignored");
        }
    }
    if (!saw_store)
        problem(0, "store missing - vendor disabled");
    return config;
}
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `scripts/rots-docker.sh test --gtest_filter='VendorParse.*:VendorHours.*:MobOption*:ReadMobOptions.*:WriteMobOptions.*'`
Expected: all PASS.

- [ ] **Step 5: Format and commit**

```bash
cd src && clang-format -i -style=WebKit mob_options.h mob_options.cpp tests/mob_options_tests.cpp && cd ..
git add src/mob_options.h src/mob_options.cpp src/tests/mob_options_tests.cpp
git commit -m "feat(mob_options): barter vendor settings parser and hours

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 3: `mob_progs/` folder, list formatter, shortfall helper

**Files:**
- Create: `src/mob_progs/passive.h`, `src/mob_progs/passive.cpp`, `src/tests/barter_vendor_tests.cpp`
- Modify (**after user approval**): `src/Makefile`, `src/CMakeLists.txt`

**Interfaces:**
- Consumes: `vendor_cost` (Task 2).
- Produces (in `passive.h`):

```cpp
constexpr int PROG_BARTER_VENDOR = 33;
constexpr size_t VENDOR_LIST_NAME_COLUMN_MAX = 38;
struct vendor_list_cost { int qty; std::string name; };
struct vendor_list_row { std::string name; int left; std::vector<vendor_list_cost> costs; }; /* left < 0: not a deduct item */
std::string format_vendor_list(const std::vector<vendor_list_row>& rows);
struct vendor_shortfall { int obj_vnum; int need; int have; };
std::vector<vendor_shortfall> vendor_shortfalls(const std::vector<vendor_cost>& costs,
    const std::function<int(int)>& have_count);
```

- [ ] **Step 1: Ask the user to approve build changes B1–B4**, **one at a time**. Show each,
  wait for a yes (or their alternative) before showing the next:
  - **B1, Makefile objects:** append `mob_progs/passive.o` to `OBJFILES`, and add this rule.
    `-o` is required: without it the compiler writes `passive.o` into `src/`:
    ```make
    mob_progs/passive.o : mob_progs/passive.cpp mob_progs/passive.h mob_options.h structs.h utils.h comm.h interpre.h handler.h db.h
    	$(CC) -c $(CFLAGS) mob_progs/passive.cpp -o mob_progs/passive.o
    ```
  - **B2, Makefile clean:** change `rm -f *.o` to `rm -f *.o mob_progs/*.o`.
  - **B3, includes:** folder files use `#include "../structs.h"` (as `src/tests/` does), with
    no compiler-flag change. The alternative is `-I.` in the Makefile and CMake.
  - **B4, CMake:** add `mob_progs/passive.cpp` to `ROTS_SERVER_SOURCES`, and
    `tests/barter_vendor_tests.cpp` to `ROTS_TEST_SOURCES`.

  Then say: the deploy script (`put -r *`, recursive backup) needs no change but should be
  confirmed on the server, and Docker needs no change. Record their answers in the commit
  message.

- [ ] **Step 2: Write the failing tests** in `src/tests/barter_vendor_tests.cpp`:

```cpp
#include "../mob_progs/passive.h"

#include <gtest/gtest.h>

#include <map>
#include <string>

TEST(VendorList, AlignsCostsInOneColumnWithExtraCostsBelow)
{
    std::vector<vendor_list_row> rows = {
        { "a hunter's belt", 2, { { 1, "a leather belt" }, { 2, "a grey wolf hide" } } },
        { "a fur-lined cloak", -1, { { 4, "a grey wolf hide" } } },
    };
    std::string expected =
        " 1. a hunter's belt (2 left)  1 x a leather belt\n\r"
        "                              2 x a grey wolf hide\n\r"
        " 2. a fur-lined cloak         4 x a grey wolf hide\n\r";
    EXPECT_EQ(format_vendor_list(rows), expected);
}

TEST(VendorList, NumbersRightAlignPastNine)
{
    std::vector<vendor_list_row> rows;
    for (int i = 0; i < 10; ++i)
        rows.push_back({ "a pebble", -1, { { 1, "a coin" } } });
    std::string out = format_vendor_list(rows);
    EXPECT_NE(out.find(" 9. a pebble  1 x a coin\n\r"), std::string::npos);
    EXPECT_NE(out.find("10. a pebble  1 x a coin\n\r"), std::string::npos);
}

TEST(VendorList, LongNamesWrapAndCostsStayWithin78)
{
    std::vector<vendor_list_row> rows = {
        { "an enormous two-handed executioner's axe of Angmar", -1,
            { { 100, "a black arrowhead" }, { 2, "a grey wolf hide" } } },
    };
    std::string out = format_vendor_list(rows);
    size_t start = 0;
    int lines = 0;
    while (start < out.size()) {
        size_t end = out.find("\n\r", start);
        ASSERT_NE(end, std::string::npos);
        std::string line = out.substr(start, end - start);
        EXPECT_LE(line.size(), 78u) << line;
        if (line.size() > 44)
            EXPECT_TRUE(line.compare(44, 1, "1") == 0 || line.compare(44, 1, "2") == 0) << line; // costs at column 44
        start = end + 2;
        ++lines;
    }
    EXPECT_EQ(lines, 2);
    EXPECT_EQ(out.find(" 1. an enormous two-handed executioner's"), 0u);
}

TEST(VendorShortfalls, ListsEveryShortCurrencyAndNothingWhenCovered)
{
    std::map<int, int> have = { { 2222, 1 }, { 3333, 5 } };
    auto count = [&](int vnum) { return have.count(vnum) ? have[vnum] : 0; };
    std::vector<vendor_cost> costs = { { 2222, 2 }, { 3333, 2 }, { 4444, 1 } };
    std::vector<vendor_shortfall> s = vendor_shortfalls(costs, count);
    ASSERT_EQ(s.size(), 2u);
    EXPECT_EQ(s[0].obj_vnum, 2222);
    EXPECT_EQ(s[0].need, 2);
    EXPECT_EQ(s[0].have, 1);
    EXPECT_EQ(s[1].obj_vnum, 4444);
    EXPECT_TRUE(vendor_shortfalls({ { 3333, 5 } }, count).empty());
}
```

- [ ] **Step 3: Run the tests to verify they fail**

Run: `scripts/rots-docker.sh test --gtest_filter='VendorList.*:VendorShortfalls.*'`
Expected: build failure, `mob_progs/passive.h: No such file`.

- [ ] **Step 4: Implement.** `src/mob_progs/passive.h`:

```cpp
#ifndef MOB_PROGS_PASSIVE_H
#define MOB_PROGS_PASSIVE_H

/* Passive/service mob programs: they react to what players do and don't
 * fight or roam. First resident: the barter vendor (program 33). */

#include "../mob_options.h"

#include <functional>
#include <string>
#include <vector>

constexpr int PROG_BARTER_VENDOR = 33;
constexpr size_t VENDOR_LIST_NAME_COLUMN_MAX = 38;

struct vendor_list_cost {
    int qty;
    std::string name;
};
struct vendor_list_row {
    std::string name;
    int left; /* copies in stock for a deduct item; < 0 otherwise */
    std::vector<vendor_list_cost> costs;
};
std::string format_vendor_list(const std::vector<vendor_list_row>& rows);

struct vendor_shortfall {
    int obj_vnum;
    int need;
    int have;
};
std::vector<vendor_shortfall> vendor_shortfalls(const std::vector<vendor_cost>& costs,
    const std::function<int(int)>& have_count);

#endif
```

`src/mob_progs/passive.cpp`:

```cpp
#include "passive.h"

#include <algorithm>
#include <cstdio>

namespace {

std::vector<std::string> wrap_words(const std::string& text, size_t width)
{
    std::vector<std::string> chunks;
    std::string current;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t space = text.find(' ', pos);
        std::string word = text.substr(pos, space == std::string::npos ? std::string::npos : space - pos);
        pos = space == std::string::npos ? text.size() : space + 1;
        while (word.size() > width) { /* a single over-long word is cut */
            if (!current.empty()) {
                chunks.push_back(current);
                current.clear();
            }
            chunks.push_back(word.substr(0, width));
            word = word.substr(width);
        }
        if (current.empty())
            current = word;
        else if (current.size() + 1 + word.size() <= width)
            current += " " + word;
        else {
            chunks.push_back(current);
            current = word;
        }
    }
    if (!current.empty() || chunks.empty())
        chunks.push_back(current);
    return chunks;
}

} // namespace

std::string format_vendor_list(const std::vector<vendor_list_row>& rows)
{
    std::vector<std::string> names;
    size_t width = 0;
    for (const vendor_list_row& row : rows) {
        std::string name = row.name;
        if (row.left >= 0)
            name += " (" + std::to_string(row.left) + " left)";
        width = std::max(width, std::min(name.size(), VENDOR_LIST_NAME_COLUMN_MAX));
        names.push_back(name);
    }
    std::string out;
    for (size_t i = 0; i < rows.size(); ++i) {
        std::vector<std::string> chunks = wrap_words(names[i], VENDOR_LIST_NAME_COLUMN_MAX);
        size_t lines = std::max(chunks.size(), rows[i].costs.size());
        for (size_t l = 0; l < lines; ++l) {
            char number[8];
            snprintf(number, sizeof(number), "%2d. ", (int)(i + 1));
            std::string line = l == 0 ? number : "    ";
            std::string chunk = l < chunks.size() ? chunks[l] : "";
            line += chunk;
            line.append(width - chunk.size() + 2, ' ');
            if (l < rows[i].costs.size())
                line += std::to_string(rows[i].costs[l].qty) + " x " + rows[i].costs[l].name;
            line.erase(line.find_last_not_of(' ') + 1);
            out += line + "\n\r";
        }
    }
    return out;
}

std::vector<vendor_shortfall> vendor_shortfalls(const std::vector<vendor_cost>& costs,
    const std::function<int(int)>& have_count)
{
    std::vector<vendor_shortfall> short_of;
    for (const vendor_cost& cost : costs) {
        int have = have_count(cost.obj_vnum);
        if (have < cost.qty)
            short_of.push_back({ cost.obj_vnum, cost.qty, have });
    }
    return short_of;
}
```

- [ ] **Step 5: Apply build changes B1–B4** exactly as the user approved them.

- [ ] **Step 6: Run the tests to verify they pass**

Run: `scripts/rots-docker.sh test --gtest_filter='VendorList.*:VendorShortfalls.*'`
Expected: PASS.

Then run:
```bash
scripts/rots-docker.sh compile
scripts/rots-docker.sh shell   # inside: cd src && make clean && ls mob_progs/*.o; exit
```
Expected: it links; after `make clean`, `ls` reports no `.o` files in `mob_progs/`.

- [ ] **Step 7: Format and commit**

```bash
cd src && clang-format -i -style=WebKit mob_progs/passive.h mob_progs/passive.cpp tests/barter_vendor_tests.cpp && cd ..
git add src/mob_progs src/tests/barter_vendor_tests.cpp src/Makefile src/CMakeLists.txt
git commit -m "feat(mob_progs): passive programs folder; vendor list formatter

Build changes approved individually by the user: <record B1-B4 answers>.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 4: The persisted options field (load, save, edit, free, stat)

This is the high-risk task (the spec's "extra testing" section). Keep it isolated; nothing
reads the field yet except the editor and `stat`.

**Files:**
- Modify:
  - `src/structs.h` (`char_special_data`, next to `store_prog_number`);
  - `src/db.cpp`:
    - mob loader, after `fscanf(mob_f, " \n");` near the will-teach read;
    - `free_char`, inside the `!IS_NPC || nr == -1` block;
  - `src/shapemob.cpp`:
    - `write_proto`;
    - `load_proto`, after the will-teach read;
    - `free_proto`;
    - `implement_proto`;
    - `shape_center_proto`: a new `case 42`, placed after `case 4` and before
      `#undef DESCRCHANGE`;
    - `list_help` and `list_proto`;
  - `src/act_wiz.cpp`: mob stat, after the "Special prog_number" block.

**Interfaces:**
- Consumes: `read_mob_options`, `write_mob_options`, `mob_options_storable` (Task 1).
- Produces: `char* char_special_data::mob_options`. Prototypes own it. Loaded mobs share the
  pointer, which is never freed for `nr >= 0`, matching the other mob strings.

- [ ] **Step 1: Add the field.** In `struct char_special_data` (`src/structs.h`), right after
  `int store_prog_number;`:

```cpp
    char* mob_options; /* mobs: options text for mob programs (prototype-owned, shared); 0 = none */
```

- [ ] **Step 2: Boot loader.** In `src/db.cpp`'s mob loader, right after the
  `fscanf(mob_f, " \n");` that follows the language/perception/…/will_teach read:

```cpp
                mob_proto[i].specials.mob_options = read_mob_options(mob_f, buf2);
```

  Add `#include "mob_options.h"` to `db.cpp`'s includes.

- [ ] **Step 3: Free.** In `free_char` (`src/db.cpp`), inside the
  `if (!IS_NPC(ch) || (IS_NPC(ch) && ch->nr == -1)) {` block, after
  `RELEASE(ch->player.description);`:

```cpp
        RELEASE(ch->specials.mob_options);
```

  Do **not** free it for mobs with `nr >= 0`: they share the prototype's pointer, exactly
  like `player.name`.

- [ ] **Step 4: Editor save.** At the end of `write_proto` (`src/shapemob.cpp`), after the
  final `fprintf(... will_teach);`:

```cpp
    write_mob_options(f, m->specials.mob_options);
```

  Add `#include "mob_options.h"` and `#include "mob_progs/passive.h"` to `shapemob.cpp`.

- [ ] **Step 5: Editor load.** In `load_proto`, right after the `will_teach` `fscanf` block:

```cpp
        SHAPE_PROTO(ch)->proto->specials.mob_options = read_mob_options(file, "shaping");
```

  (`read_mob_options` takes `char*`. Pass a local `char ctx[] = "shaping";` if the compiler
  rejects the literal.)

- [ ] **Step 6: Editor free.** In `free_proto`, after
  `RELEASE(SHAPE_PROTO(ch)->proto->player.description);`:

```cpp
        RELEASE(SHAPE_PROTO(ch)->proto->specials.mob_options);
```

- [ ] **Step 7: Implement copies the text.** In `implement_proto`, right after the
  `strcpy(proto->player.description, ...)` line:

```cpp
    /* Never free the old text: loaded copies of this mob still point at it
     * (the same reason the other strings above are not freed). */
    proto->specials.mob_options = SHAPE_PROTO(ch)->proto->specials.mob_options
        ? str_dup(SHAPE_PROTO(ch)->proto->specials.mob_options)
        : 0;
```

- [ ] **Step 8: Field 42 in the editor.** In `shape_center_proto`, add this case after
  `case 4:`'s `break;` and **before** the `#undef DESCRCHANGE` line. `DESCRCHANGE` assigns the
  edited text directly, so validate afterwards and roll back to the previous value if it
  isn't storable:

```cpp
        case 42: {
            char* before = SHAPE_PROTO(ch)->proto->specials.mob_options;
            bool finishing = IS_SET(SHAPE_PROTO(ch)->flags, SHAPE_SIMPLE_ACTIVE);
            DESCRCHANGE("OPTIONS, settings for the mob program, one per line", SHAPE_PROTO(ch)->proto->specials.mob_options)
            if (finishing) {
                /* The editor copy owns its strings (load_proto allocated them),
                 * so whichever of before/after is dropped is freed here. */
                char* after = SHAPE_PROTO(ch)->proto->specials.mob_options;
                const char* why = 0;
                if (after != before) {
                    if (!mob_options_storable(after, &why)) {
                        send_to_char("Options not changed: ", ch);
                        send_to_char(why, ch);
                        send_to_char(".\n\r", ch);
                        RELEASE(after);
                        SHAPE_PROTO(ch)->proto->specials.mob_options = before;
                    } else {
                        RELEASE(before);
                        if (after && !*after)
                            RELEASE(SHAPE_PROTO(ch)->proto->specials.mob_options);
                    }
                }
            }
        } break;
```

  - Show it in `list_help` by adding `send_to_char("42 - options;\n\r", ch);` after the
    `41 - will teach` line.
  - At the end of `list_proto` (after the `(41) will teach` lines), add:

```cpp
    send_to_char("(42) options        :\n\r", ch);
    if (mob->specials.mob_options)
        send_to_char(mob->specials.mob_options, ch);
    send_to_char("\n\r", ch);
```

  Note: `case 48` maps to editflag 0 ("sorry, it seems necessary"); 42 has no such
  collision. Confirm with `grep -n "== 42\|proto_chain\[4[2-9]\]" src/shapemob.cpp`. It must
  return nothing that conflicts.

- [ ] **Step 9: `stat`.** In `src/act_wiz.cpp`, after the `Special prog_number:` block's
  `send_to_char` (find it with `grep -n "Special prog_number" src/act_wiz.cpp`):

```cpp
    if (IS_NPC(k) && k->specials.mob_options && *k->specials.mob_options) {
        send_to_char("Options:\n\r", ch);
        send_to_char(k->specials.mob_options, ch);
        send_to_char("\n\r", ch);
    }
```

- [ ] **Step 10: Build and run the full suite against the baseline.**

Run first on a clean checkout of the base commit, then on this branch:
`scripts/rots-docker.sh test 2>&1 | tail -5`
Expected: same pass/fail counts as the baseline (the known ~10 pre-existing failures), plus
the new tests passing.

- [ ] **Step 11: Boot on real world files.** Nothing has options yet, so this proves the
  loader leaves every existing file alone:
  1. Follow the memory recipe `live_world_test_boot_recipe` and `test_server_user_account_setup`:
     port 4071, `-d <testlib>`.
  2. Boot. Expected: the boot completes and the log is otherwise identical to a baseline boot
     (`diff <(grep -v '^[0-9:]' base.log) <(grep -v '^[0-9:]' new.log)` shows only
     timestamps).

- [ ] **Step 12: Editor round trip on the test server.** As the lvl-100 test char:
  1. `shape mobile <vnum of any mob in a small test zone>`
  2. `42`, enter `store=1\n\rprice 1 2x1`, then `@`
  3. `/save`
  4. `/implement`
  5. `stat <mob>`, which should show the options
  6. copy the mob file aside
  7. reboot
  8. `shape mobile <vnum>` again, which should show the same options
  9. `/save`, then diff the file against the copy: it must be identical
  10. Then set the options empty and `/save`: the options line must be gone from the file.

  Also try the rejects:
  - text starting with `#`, which should print "Options not changed" and keep the old value;
  - text containing `~`.

- [ ] **Step 13: Memory checks.** On the test server, with that mob:
  - `load mob` it 3 times;
  - `purge` one;
  - kill one;
  - edit and `/implement` while 1 is still loaded, then `stat` the loaded one (it keeps its
    old text, with no crash);
  - `purge` the last one.

  Watch the log and `gdb`-less stability for a few minutes. Optionally run the server under
  `valgrind --leak-check=no` (i386 image) for invalid-free detection.

- [ ] **Step 14: Commit.** Do **not** run clang-format on `db.cpp`, `shapemob.cpp` or
  `act_wiz.cpp`: they have large pre-existing format drift (CLAUDE.md warns about
  `db.cpp`/`act_wiz.cpp`). Hand-format only the new lines, in WebKit style matching their
  surroundings. Check that `git diff --stat` shows only the intended lines.

```bash
git add src/structs.h src/db.cpp src/shapemob.cpp src/act_wiz.cpp
git commit -m "feat(mobs): persisted options text field (load, save, editor field 42, stat)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 5: Program 33 wiring, config registry, boot and editor validation

**Files:**
- Modify:
  - `src/mob_progs/passive.h`, `src/mob_progs/passive.cpp`;
  - `src/spec_ass.cpp`: the two switch tables, `spec_pro_message[]`, the declaration;
  - `src/db.cpp`: `boot_db`, after the "Assigning function pointers" block;
  - `src/shapemob.cpp`: end of `implement_proto`, plus `replace_proto`/`append_proto` after a
    successful write.

**Interfaces:**
- Consumes: `parse_vendor_options`, `vendor_config`, `vendor_problem`, `vendor_lookups`
  (Task 2); `PROG_BARTER_VENDOR` (Task 3); `char_special_data::mob_options` (Task 4).
- Produces (add to `passive.h`):

```cpp
struct char_data;
struct waiting_type;
int barter_vendor(struct char_data* host, struct char_data* ch, int cmd, char* arg, int callflag, struct waiting_type* wtl);
void vendor_config_boot();                                  /* all program-33 prototypes */
void vendor_config_rebuild(int mob_rnum, struct char_data* builder); /* parse + report + store, or erase */
void vendor_config_check(const struct char_data* proto, int mob_vnum, struct char_data* builder); /* report only */
const vendor_config* vendor_config_for(int mob_rnum);       /* nullptr if none */
std::string vendor_problem_line(int mob_vnum, const vendor_problem& problem); /* formatted warning */
```

- [ ] **Step 1: Write the failing test** for the warning format. Append to
  `src/tests/barter_vendor_tests.cpp`:

```cpp
TEST(VendorProblemLine, HouseStyle)
{
    EXPECT_EQ(vendor_problem_line(1234, { 3, "price: bad format - line skipped" }),
        "MOB ERROR: mobile #1234, options line 3: price: bad format - line skipped");
    EXPECT_EQ(vendor_problem_line(1234, { 0, "store missing - vendor disabled" }),
        "MOB ERROR: mobile #1234: store missing - vendor disabled");
}
```

- [ ] **Step 2: Run it to verify it fails**

Run: `scripts/rots-docker.sh test --gtest_filter='VendorProblemLine.*'`
Expected: compile error, `vendor_problem_line` not declared.

- [ ] **Step 3: Implement the registry.** Append to `src/mob_progs/passive.cpp`, and add its
  includes to the top of the file:

```cpp
#include "../comm.h"
#include "../db.h"
#include "../handler.h"
#include "../interpre.h"
#include "../structs.h"
#include "../utils.h"

#include <unordered_map>

extern struct char_data* mob_proto;
extern struct index_data* mob_index;
extern int top_of_mobt;
extern int no_specials;

namespace {

std::unordered_map<int, vendor_config> g_vendor_configs; /* by mob rnum */

vendor_lookups game_lookups()
{
    vendor_lookups lookups;
    lookups.obj_exists = [](int vnum) { return real_object(vnum) >= 0; };
    lookups.room_exists = [](int vnum) { return real_room(vnum) >= 0; };
    return lookups;
}

void vendor_send(const std::string& line, struct char_data* builder)
{
    char buf[512];
    snprintf(buf, sizeof(buf), "%s", line.c_str());
    mudlog(buf, NRM, LEVEL_AREAGOD, TRUE);
    if (builder && !mudlog_reaches(builder, LEVEL_AREAGOD, NRM)) {
        send_to_char(buf, builder);
        send_to_char("\n\r", builder);
    }
}

bool is_vendor_proto(const char_data& proto)
{
    return IS_SET(proto.specials2.act, MOB_SPEC) && proto.specials.store_prog_number == PROG_BARTER_VENDOR;
}

} // namespace

std::string vendor_problem_line(int mob_vnum, const vendor_problem& problem)
{
    char buf[512];
    if (problem.line > 0)
        snprintf(buf, sizeof(buf), "MOB ERROR: mobile #%d, options line %d: %s", mob_vnum, problem.line, problem.text.c_str());
    else
        snprintf(buf, sizeof(buf), "MOB ERROR: mobile #%d: %s", mob_vnum, problem.text.c_str());
    return buf;
}

/* do_say refuses mobs with INT < 6 ("too stupid to talk"), which would leave
 * a vendor unable to answer. Warn the builder rather than special-case say. */
void vendor_add_speech_problem(const char_data& proto, std::vector<vendor_problem>* problems)
{
    if (proto.abilities.intel < 6)
        problems->push_back({ 0, "intelligence below 6 - vendor can't speak" });
}

void vendor_config_check(const struct char_data* proto, int mob_vnum, struct char_data* builder)
{
    std::vector<vendor_problem> problems;
    parse_vendor_options(proto->specials.mob_options, game_lookups(), &problems);
    vendor_add_speech_problem(*proto, &problems);
    for (const vendor_problem& problem : problems)
        vendor_send(vendor_problem_line(mob_vnum, problem), builder);
}

void vendor_config_rebuild(int mob_rnum, struct char_data* builder)
{
    if (mob_rnum < 0 || mob_rnum > top_of_mobt || !is_vendor_proto(mob_proto[mob_rnum])) {
        g_vendor_configs.erase(mob_rnum);
        return;
    }
    std::vector<vendor_problem> problems;
    g_vendor_configs[mob_rnum] = parse_vendor_options(mob_proto[mob_rnum].specials.mob_options, game_lookups(), &problems);
    vendor_add_speech_problem(mob_proto[mob_rnum], &problems);
    for (const vendor_problem& problem : problems)
        vendor_send(vendor_problem_line(mob_index[mob_rnum].virt, problem), builder);
}

const vendor_config* vendor_config_for(int mob_rnum)
{
    auto it = g_vendor_configs.find(mob_rnum);
    return it == g_vendor_configs.end() ? nullptr : &it->second;
}

void vendor_config_boot()
{
    g_vendor_configs.clear();
    for (int rnum = 0; rnum <= top_of_mobt; ++rnum) {
        if (!is_vendor_proto(mob_proto[rnum]))
            continue;
        vendor_config_rebuild(rnum, nullptr);
        if (!no_specials && mob_index[rnum].func && mob_index[rnum].func != (special_func)barter_vendor)
            vendor_send(vendor_problem_line(mob_index[rnum].virt,
                            { 0, "program 33 overridden by hard-coded procedure" }),
                nullptr);
    }
}

SPECIAL(barter_vendor)
{
    return FALSE; /* Task 6 fills this in */
}
```

  `vendor_add_speech_problem` must sit above `vendor_config_check`; put it inside the same
  anonymous namespace as `vendor_send`. The editor copy stores INT in `abilities.intel`; confirm
  with `grep -n "GET_INT" src/utils.h` and use the same field.

  Check the `index_data::func` type with `grep -n "func" src/structs.h | grep -i index`. If
  it's `special_func`, the cast compiles; otherwise cast to that type.

- [ ] **Step 4: Wire program 33.** In `src/spec_ass.cpp`:
  - Include `"mob_progs/passive.h"`.
  - In `virt_program_number`, after `case 32: return (void*)mob_ranger_new;`, add
    `case 33: return (void*)barter_vendor;`.
  - In `get_special_function`, after `case 32: return &mob_ranger_new;`, add
    `case 33: return &barter_vendor;`.
  - In `spec_pro_message[]`, add one more `""` entry after the last one, with the comment
    `// 33 barter vendor`. The array must then have 34 entries (0–33). Count them.

- [ ] **Step 5: Boot hook.** In `src/db.cpp` `boot_db`, right after the closing `}` of
  `if (!no_specials) { ... assign_rooms(); }`:

```cpp
    log("Checking barter vendors.");
    vendor_config_boot();
```

  and include `"mob_progs/passive.h"`.

- [ ] **Step 6: Editor hooks.**
  - At the very end of `implement_proto` (after the `virt_assignmob` / `real_program`
    if/else):

    ```cpp
        vendor_config_rebuild(number, ch);
    ```

    This also erases a config when the builder turns program 33 off.
  - In `replace_proto` and `append_proto`, right after the `write_proto(f2, SHAPE_PROTO(ch)->proto, num);`
    call, add a check so a saved-but-not-implemented mob still gets its warnings:

    ```cpp
        if (IS_SET(SHAPE_PROTO(ch)->proto->specials2.act, MOB_SPEC)
            && SHAPE_PROTO(ch)->proto->specials.store_prog_number == PROG_BARTER_VENDOR)
            vendor_config_check(SHAPE_PROTO(ch)->proto, num, ch);
    ```

    Find the calls with `grep -n "write_proto(" src/shapemob.cpp`, and use whatever vnum
    variable that function passes to `write_proto` (`num` in `replace_proto`).

- [ ] **Step 7: Run the tests**

Run: `scripts/rots-docker.sh test --gtest_filter='Vendor*:MobOption*:ReadMobOptions.*:WriteMobOptions.*'`
Expected: PASS. Then run `scripts/rots-docker.sh compile`, which must link.

- [ ] **Step 8: Boot check on the test server.**
  1. Give one test mob `MOB_SPEC`, program 33, and options `store=<real room>\n\rprice 99999 1x1\n\rbogus`.
  2. `/save`, then reboot.
  3. Expected in the log:
     - `MOB ERROR: mobile #<vnum>, options line 2: price: object vnum 99999 not found - line skipped`
     - `MOB ERROR: mobile #<vnum>, options line 3: unknown setting - line ignored`
  4. The same lines reach the builder on `/implement` and on `/save`, and only once each for
     an imm who also gets mudlog.
  5. Set the mob's INT to 5 and `/implement`: expect
     `MOB ERROR: mobile #<vnum>: intelligence below 6 - vendor can't speak`.

- [ ] **Step 9: Commit**

```bash
git add src/mob_progs src/spec_ass.cpp src/db.cpp src/shapemob.cpp src/tests/barter_vendor_tests.cpp
git commit -m "feat(vendor): program 33 wiring, config registry, boot/editor warnings

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 6: `SPECIAL(barter_vendor)`: list, buy, refusals, protection, give

**Files:**
- Modify: `src/mob_progs/passive.cpp`, `src/interpre.h` (`CMD_GIVE`)

**Interfaces:**
- Consumes: `vendor_config_for`, `vendor_is_open`, `format_vendor_list`,
  `vendor_shortfalls`, `vendor_problem_line`.
- Produces: the finished `barter_vendor`.

- [ ] **Step 1: Add `CMD_GIVE`.**
  1. Check it doesn't exist: `grep -rn "CMD_GIVE" src/` must return nothing.
  2. Verify the number: `"save", /* 51 */` is followed by `"hit"` (52, `CMD_HIT`),
     `"petitio"` (53) and `"give"` (54) in `interpre.cpp`'s `command[]`.
  3. In `src/interpre.h`, next to `#define CMD_LIST 41`:

```cpp
#define CMD_GIVE 54
```

- [ ] **Step 2: Implement.** Replace the stub `SPECIAL(barter_vendor)` in
  `src/mob_progs/passive.cpp` with the code below, and add
  `extern struct room_data world; extern struct obj_data* obj_proto; extern struct index_data* obj_index; extern struct time_info_data time_info;`
  near the other externs, `ACMD(do_say);` after the includes, and add `#include <cctype>` and `#include <cstring>` to the file's includes:

```cpp
namespace {

/* The vendor speaks with the normal `say`, heard by the room, like any mob.
 * (A vendor with INT < 6 can't; that's warned at boot/save/implement.)
 * Messages about the buyer's own inventory go to the buyer with send_to_char. */
void vendor_say(struct char_data* vendor, const char* text)
{
    char buf[MAX_INPUT_LENGTH];
    snprintf(buf, sizeof(buf), "%s", text);
    do_say(vendor, buf, 0, 0, 0);
}

bool vendor_serves(struct char_data* vendor, struct char_data* ch, const vendor_config& config)
{
    if (IS_AGGR_TO(vendor, ch)) {
        vendor_say(vendor, "Go away, I won't deal with you!");
        return false;
    }
    if (IS_SHADOW(ch)) {
        vendor_say(vendor, "Ugh! I'm not serving you!");
        return false;
    }
    if (!RP_RACE_CHECK(vendor, ch)) {
        vendor_say(vendor, "Sorry, I can't serve you!");
        return false;
    }
    if (!CAN_SEE(vendor, ch)) {
        vendor_say(vendor, "I don't trade with someone I can't see!");
        return false;
    }
    if (!vendor_is_open(config, time_info.hours)) {
        vendor_say(vendor, "I'm closed. Come back later.");
        return false;
    }
    return true;
}

struct stock_row {
    const vendor_price* price;
    struct obj_data* copy; /* first visible copy in the store room */
    int count;
};

std::vector<stock_row> vendor_stock(struct char_data* ch, const vendor_config& config)
{
    std::vector<stock_row> rows;
    int room = real_room(config.store_vnum);
    if (room < 0)
        return rows;
    for (const vendor_price& price : config.prices) {
        int item_rnum = real_object(price.item_vnum);
        if (item_rnum < 0)
            continue;
        stock_row row { &price, 0, 0 };
        for (struct obj_data* obj = world[room].contents; obj; obj = obj->next_content)
            if (obj->item_number == item_rnum && CAN_SEE_OBJ(ch, obj)) {
                if (!row.copy)
                    row.copy = obj;
                ++row.count;
            }
        if (row.copy)
            rows.push_back(row);
    }
    return rows;
}

const char* obj_vnum_short(int vnum)
{
    int rnum = real_object(vnum);
    return rnum >= 0 ? obj_proto[rnum].short_description : "something";
}

/* Loose, EMPTY copies only: a container with things in it is never taken
 * as payment, so its contents can't be destroyed. */
std::vector<struct obj_data*> payable_copies(struct char_data* ch, int vnum)
{
    std::vector<struct obj_data*> copies;
    int rnum = real_object(vnum);
    for (struct obj_data* obj = ch->carrying; obj && rnum >= 0; obj = obj->next_content)
        if (obj->item_number == rnum && !obj->contains)
            copies.push_back(obj);
    return copies;
}

void vendor_list(struct char_data* vendor, struct char_data* ch, const vendor_config& config)
{
    std::vector<stock_row> stock = vendor_stock(ch, config);
    if (stock.empty()) {
        vendor_say(vendor, "I have nothing to sell right now.");
        return;
    }
    std::vector<vendor_list_row> rows;
    for (const stock_row& s : stock) {
        vendor_list_row row { s.copy->short_description, s.price->deduct ? s.count : -1, {} };
        for (const vendor_cost& cost : s.price->costs)
            row.costs.push_back({ cost.qty, obj_vnum_short(cost.obj_vnum) });
        rows.push_back(row);
    }
    send_to_char(format_vendor_list(rows).c_str(), ch);
}

void vendor_buy(struct char_data* vendor, struct char_data* ch, char* arg, const vendor_config& config)
{
    char want[MAX_INPUT_LENGTH], buf[MAX_STRING_LENGTH];
    one_argument(arg, want);
    if (!*want) {
        vendor_say(vendor, "What do you want to buy?");
        return;
    }
    std::vector<stock_row> stock = vendor_stock(ch, config);
    const stock_row* pick = 0;
    if (isdigit((unsigned char)*want)) {
        int n = atoi(want);
        if (n >= 1 && n <= (int)stock.size())
            pick = &stock[n - 1];
    } else {
        for (const stock_row& s : stock)
            if (isname(want, s.copy->name)) {
                pick = &s;
                break;
            }
    }
    if (!pick) {
        vendor_say(vendor, "I don't have that. Try 'list'.");
        return;
    }
    if (IS_CARRYING_N(ch) + 1 > CAN_CARRY_N(ch)) {
        send_to_char("You can't carry that many items.\n\r", ch);
        return;
    }
    if (IS_CARRYING_W(ch) + GET_OBJ_WEIGHT(pick->copy) > CAN_CARRY_W(ch)) {
        send_to_char("You can't carry that much weight.\n\r", ch);
        return;
    }

    std::vector<vendor_shortfall> short_of = vendor_shortfalls(pick->price->costs,
        [ch](int vnum) { return (int)payable_copies(ch, vnum).size(); });
    if (!short_of.empty()) {
        for (const vendor_shortfall& s : short_of) {
            snprintf(buf, sizeof(buf), "You need %d x %s and have %d.\n\r", s.need, obj_vnum_short(s.obj_vnum), s.have);
            send_to_char(buf, ch);
        }
        return;
    }

    /* Gather every payment object first; destroy nothing until all are in hand. */
    std::vector<struct obj_data*> payment;
    for (const vendor_cost& cost : pick->price->costs) {
        std::vector<struct obj_data*> copies = payable_copies(ch, cost.obj_vnum);
        if ((int)copies.size() < cost.qty) {
            snprintf(buf, sizeof(buf), "SYSERR: barter_vendor: payment count changed for obj #%d", cost.obj_vnum);
            mudlog(buf, NRM, LEVEL_IMMORT, TRUE);
            return;
        }
        payment.insert(payment.end(), copies.begin(), copies.begin() + cost.qty);
    }
    for (struct obj_data* obj : payment) {
        obj_from_char(obj);
        extract_obj(obj);
    }

    std::string paid;
    for (const vendor_cost& cost : pick->price->costs) {
        if (!paid.empty())
            paid += ", ";
        paid += std::to_string(cost.qty) + " x " + obj_vnum_short(cost.obj_vnum);
    }
    struct obj_data* bought = read_object(pick->copy->item_number, REAL);
    obj_to_char(bought, ch);
    snprintf(buf, sizeof(buf), "You hand over %s.\n\rYou now have %s.\n\r", paid.c_str(), bought->short_description);
    send_to_char(buf, ch);
    act("$n buys $p.", FALSE, ch, bought, 0, TO_ROOM);

    if (pick->price->deduct) {
        struct obj_data* copy = pick->copy;
        obj_from_room(copy);
        extract_obj(copy);
    }
}

/* True if `give ... <target>` names this vendor: the target is the last word. */
bool give_targets(struct char_data* vendor, struct char_data* ch, char* arg)
{
    char buf[MAX_INPUT_LENGTH];
    strncpy(buf, arg, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = 0;
    char* end = buf + strlen(buf);
    while (end > buf && isspace((unsigned char)end[-1]))
        *--end = 0;
    char* last = end;
    while (last > buf && !isspace((unsigned char)last[-1]))
        --last;
    return *last && get_char_room_vis(ch, last, 0) == vendor;
}

} // namespace

SPECIAL(barter_vendor)
{
    if (!host || !ch || ch == host)
        return FALSE;

    if (callflag == SPECIAL_DAMAGE) {
        vendor_say(host, "Don't even think about it.");
        return TRUE;
    }
    if (callflag != SPECIAL_COMMAND)
        return FALSE;
    if (cmd != CMD_LIST && cmd != CMD_BUY && cmd != CMD_GIVE)
        return FALSE;

    if (cmd == CMD_GIVE) {
        if (!arg || !give_targets(host, ch, arg))
            return FALSE;
        vendor_say(host, "I don't take gifts.");
        return TRUE;
    }

    const vendor_config* config = vendor_config_for(host->nr);
    if (!config || !config->usable()) {
        vendor_send(vendor_problem_line(host->nr >= 0 ? mob_index[host->nr].virt : -1,
                        { 0, "bad options - vendor disabled" }),
            nullptr);
        vendor_say(host, "I'm not trading right now.");
        return TRUE;
    }
    if (!vendor_serves(host, ch, *config))
        return TRUE;

    if (cmd == CMD_LIST)
        vendor_list(host, ch, *config);
    else
        vendor_buy(host, ch, arg ? arg : (char*)"", *config);
    return TRUE;
}
```

  Before compiling, verify the helper signatures you call (fix any mismatch at the call
  site, not the helper):
  - `grep -n "void act(" src/comm.h`
  - `grep -n "one_argument\|isname\|get_char_room_vis" src/interpre.h src/handler.h`
  - `grep -n "read_object(" src/db.h`
  - `grep -n "extract_obj\|obj_from_char\|obj_from_room\|obj_to_char" src/handler.h`

  The vendor's spoken lines go through `do_say` (heard by the room). Shortfall, carry-limit
  and purchase lines go only to the buyer through `send_to_char`.

- [ ] **Step 3: Build and run the unit tests**

Run: `scripts/rots-docker.sh compile && scripts/rots-docker.sh test --gtest_filter='Vendor*:MobOption*:ReadMobOptions.*:WriteMobOptions.*'`
Expected: links; PASS.

- [ ] **Step 4: Smoke on the test server.** Keep this short; Task 8 is the full pass.
  1. One vendor with a store room holding 2 copies of a `deduct` item and 1 copy of an
     unlimited item.
  2. `list`, then `buy 1` without the currency (shortfall lines).
  3. `load obj` the currency, then `buy 1` (paid, item received, "(1 left)" on the next
     `list`).

- [ ] **Step 5: Commit**

```bash
git add src/mob_progs/passive.cpp src/interpre.h
git commit -m "feat(vendor): barter vendor list, buy, refusals, protection, give refusal

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 7: Documentation

**Files:**
- Modify: `docs/data-formats/world-files.md` (mob record section)
- Create: `docs/systems/barter-vendors.md`

- [ ] **Step 1: Mob file format.** In `docs/data-formats/world-files.md`, in the mob record
  format, after the last numeric line (language … will_teach), document:

```markdown
- **Options (optional, RotS 2026-09):** one `~`-terminated text block after the last number
  line, present only when the mob has options. The loader reads it only when the next
  non-space character isn't `#` or `$` (`read_mob_options`, `src/mob_options.cpp`). Text
  can't contain `~` or start with `#`/`$`; max 4000 characters. Used by mob programs; see
  `docs/systems/barter-vendors.md`. **Rollback:** a server older than this change can't read
  a mob file containing an options block.
```

- [ ] **Step 2: Builder guide.** Create `docs/systems/barter-vendors.md` with:
  1. Setup steps (MOB_SPEC, program 33, field 42 options, store room via zone `O` lines).
  2. The settings table (`store=`, `hours=`, `price … [deduct]`), with limits and what
     happens on bad values.
  3. Shared store rooms and `deduct`.
  4. Builder notes: keep the store room unreachable; sold items add to world counts; don't
     put world limits on store-room lines; remove any `.shp` entry when converting.
  5. The exact warning lines a builder may see (copy them from Task 2's tests, plus the
     intelligence one from Task 5).
  6. Comments: a line starting with `//` is ignored and never warned about.
  7. The vendor speaks with `say`, so his INT must be 6 or more (warned otherwise).

  Copy wording from the spec sections "How builders set one up", "Vendor settings", "Shared
  store rooms" and "Builder notes"; don't invent new rules.

- [ ] **Step 3: Commit**

```bash
git add docs/data-formats/world-files.md docs/systems/barter-vendors.md
git commit -m "docs: barter vendor builder guide and mob options format

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 8: Full in-game test pass and timing

Run on the local test server: port 4071, the live-world test boot recipe, and the drew_humbert
account copy plus Forge at lvl 100 per memory. Use `testing/mudclient.py` scripts where
practical, and quit test characters at the end of every script. Record results in
`reports/2026-09-26-barter-vendors-test.md` (gitignored).

**Setup:**
- Store room S (unreachable) with:
  - 2 × item A (priced `deduct`);
  - 1 × item B;
  - 1 × item C, with no price line on vendor 1.
- Vendor 1 at room R: `store=S`, `hours=6-20`, price lines for A (2 currencies), B (1), and
  a line for an item never loaded.
- Vendor 2 elsewhere: `store=S`, a different price for B, and a price for C.

- [ ] **Step 1: Listing and buying**
  - `list` on both vendors: each shows only its priced, in-stock items. Columns align;
    "(2 left)" shows on A.
  - `buy` by name and by number: payment taken, item received, the room sees "$n buys".
  - Currency short in one of two currencies: shortfall lines, **nothing taken** (check
    inventory before and after).
  - Currency inside a bag: not counted.
  - Currency item that is a non-empty container (Review Focus 1): not counted, contents
    untouched.
- [ ] **Step 2: Stock**
  - `deduct` runs out: A leaves both vendors' lists after 2 buys.
  - Stale number (Review Focus 5): `list` on 2 clients, one buys the last A, the other types
    `buy 1`. Record what they got and that the message states exactly what was paid.
  - A returns after a zone reset.
- [ ] **Step 3: Refusals**
  - Carry limits (count and weight).
  - Closed hours, using the game's time command or waiting a game hour.
  - Other side / `pref` / `rp_flag` / shadow / invisible buyer.
- [ ] **Step 4: Protection**
  - `kill`/`hit`, a damage spell, `bash`, `kick` against the vendor: damage cancelled, "Don't
    even think about it.", the attacker stops fighting. **Record what bash does to his
    position** (untested against the old hook too).
  - `give <obj> vendor`, `give 10 coins vendor`, `give all vendor`: all refused, nothing
    changes hands.
  - `give <obj> <someone else>` in the same room still works.
- [ ] **Step 5: Two vendors in one room** (Review Focus 4): `goto` vendor 2 into vendor 1's
  room. `list` gives exactly one list. Record which vendor answered.
- [ ] **Step 5b: Comments and speech.** A `//` line in the options is ignored, with no
  warning at `/implement`. The vendor's refusals and closed message are heard as `say` by
  everyone in the room.
- [ ] **Step 6: Bad config at use**
  - Implement a vendor with a bad `hours=`: `list` says "I'm not trading right now" and the
    log gets the `bad options - vendor disabled` line **each time**.
- [ ] **Step 7: The new-field extra pass** (spec "New mob field"). Repeat Task 4 Steps 11–13
  with vendor mobs:
  - reboot twice and diff the mob files;
  - a mob file whose last record has options (right before `#99999`/`$`);
  - a file saved with `\n\r` endings (convert a copy with `sed 's/$/\r/'` and boot it).
- [ ] **Step 8: Rollback.** Build the base commit's binary and boot it on the test lib that
  now contains an options block. Record the exact failure. This documents what a revert
  needs: the old mob files restored, or options blocks removed first.
- [ ] **Step 9: Timing.** Vendor at the limits:
  - 30 price lines, 4 currencies each;
  - a store room with 200 objects;
  - a buyer carrying 100 objects.

  Time `list` and `buy` with a temporary `std::chrono::steady_clock` mudlog around the
  special's body (**not committed**). Expected: well under 1 ms each. Record the numbers.
- [ ] **Step 10: Soak.** Leave the final build running with 2 scripted clients doing
  list/buy in a loop for 10 minutes. No crashes, no growth in RSS beyond noise
  (`ps -o rss`).
- [ ] **Step 11:** Report the results to the user. **Don't push or open a PR**; the user
  manual-tests first.
