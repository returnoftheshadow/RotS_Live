# Account-Level Player Preferred Config (PPC) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Move the twelve display/protocol `set` options plus the full colour configuration off the character and onto the account, so a player configures one scheme and every character on the account uses it.

**Architecture:** The account is the store; the character struct stays the runtime representation. Load copies account → character, save copies character → account (only when they differ), and a settings change propagates immediately to any other character on the account that is online. Nothing that *reads* a PPC value changes.

**Tech Stack:** C++17, 32-bit (`-m32`) CircleMUD derivative. GoogleTest via `src/CMakeLists.txt`. Hand-rolled JSON (`json_utils.h`, `character_json.cpp`, `account_management_storage.cpp`). Build and test through `scripts/rots-docker.sh`.

**Spec:** `docs/superpowers/specs/2026-09-05-account-level-player-preferred-config-design.md`

## Global Constraints

- **Do NOT bump `ACCOUNT_SCHEMA_VERSION`.** It stays `1`. `deserialize_account_from_json` rejects any file whose version is not exactly `ACCOUNT_SCHEMA_VERSION` (`account_management_storage.cpp:143`); a bump would make every existing account file fail to load. `preferences` is an additive optional key, exactly as `roster_sort` was.
- **The PPC mask is the single definition of membership.** Every apply, read and compare is masked. Bits outside it are never read from or written to the account.
- **Save must compare before writing.** `write_account_file` triggers `account_cache::invalidate_all()` (a full flush). The cache's header states account mutations "are rare and never happen on the hot save/load path" (`account_cache.h:30`). An unconditional write on every save would violate that.
- **Never run bare `make format`.** It reorders `account_management.cpp`'s fragment `#include`s and breaks the build. Format only files you changed: `cd src && clang-format -i -style=WebKit <files>`.
- **Build:** `scripts/rots-docker.sh compile`. **Test:** `scripts/rots-docker.sh test --gtest_filter='<Suite>.*'`.
- **Baseline the test suite before blaming your change.** The full run has a pre-existing segfault partway through (`InterpreAccountMenu.UnlockSelect...`) on this branch. Always use `--gtest_filter`.
- **Commit trailer:** end each commit message with `Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>`. Do **not** add a `Claude-Session:` line.
- **Do not push and do not open a PR.** The user pushes and tests manually, on explicit request only.
- `MAX_COLOR_FIELDS` is 16. `char_prof_data::colors` is `char colors[16]` (signed), `color_settings` is `color_slot_data color_settings[MAX_COLOR_FIELDS]`.

## File Structure

| File | Responsibility |
|---|---|
| `src/structs.h` | `PPC_PRF_MASK` constant, beside the `PRF_*` defines it is built from |
| `src/account_management_types.h` | `AccountPreferences` struct; `AccountData::preferences` |
| `src/character_json.h` / `.cpp` | Expose the existing colour-slot encoder/parser so account code reuses one colour format |
| `src/account_management_storage.cpp` | Emit the `preferences` object |
| `src/account_management.cpp` | Parse the `preferences` key in `parse_account_property` |
| `src/account_ppc.h` / `.cpp` | **New.** Pure PPC logic: read from / write to a character, compare, propagate. The unit that everything else calls. |
| `src/db.cpp` | Compare-and-write the account PPC inside `save_char` |
| `src/interpre.cpp` | Apply/seed on the enter-game paths; the two creation helpers and the prompt skip |
| `src/act_othe.cpp` | `ppc_propagate_from` at the tail of `do_gen_tog` and `do_inventory_sort` |
| `src/color.cpp` | `ppc_propagate_from` at the tail of `do_color` |
| `src/tests/account_ppc_tests.cpp` | **New.** All unit tests for this feature |

---

### Task 1: The PPC mask and the `AccountPreferences` type

**Files:**
- Modify: `src/structs.h` (near the `PRF_*` defines, around line 998)
- Modify: `src/account_management_types.h:54-95` (the `AccountData` struct)
- Modify: `src/CMakeLists.txt:112` (register the new test file)
- Test: `src/tests/account_ppc_tests.cpp` (create)

**Interfaces:**
- Consumes: nothing.
- Produces: `PPC_PRF_MASK` (a `long` constant in `structs.h`); `account::AccountPreferences` with fields `bool present`, `long preference_flags`, `char colors[MAX_COLOR_FIELDS]`, `color_slot_data color_settings[MAX_COLOR_FIELDS]`; `AccountData::preferences`.

- [ ] **Step 1: Write the failing test**

Create `src/tests/account_ppc_tests.cpp`:

```cpp
#include "../account_management_types.h"
#include "../color.h"
#include "../structs.h"

#include <gtest/gtest.h>

namespace {

TEST(AccountPpcMask, CoversEveryPpcOptionAndNothingElse)
{
    const long expected = PRF_PROMPT | PRF_ADVANCED_PROMPT | PRF_ADVANCED_VIEW
        | PRF_BRIEF | PRF_COMPACT | PRF_SPAM | PRF_WRAP | PRF_ECHO
        | PRF_MSDP | PRF_LATIN1 | PRF_SPINNER
        | PRF_INV_SORT1 | PRF_INV_SORT2 | PRF_COLOR;
    EXPECT_EQ(PPC_PRF_MASK, expected);
}

TEST(AccountPpcMask, ExcludesCharacterAndMoodSettings)
{
    EXPECT_EQ(PPC_PRF_MASK & PRF_NOTELL, 0);
    EXPECT_EQ(PPC_PRF_MASK & PRF_NARRATE, 0);
    EXPECT_EQ(PPC_PRF_MASK & PRF_CHAT, 0);
    EXPECT_EQ(PPC_PRF_MASK & PRF_SING, 0);
    EXPECT_EQ(PPC_PRF_MASK & PRF_MENTAL, 0);
    EXPECT_EQ(PPC_PRF_MASK & PRF_SWIM, 0);
    EXPECT_EQ(PPC_PRF_MASK & PRF_SUMMONABLE, 0);
    EXPECT_EQ(PPC_PRF_MASK & PRF_HOLYLIGHT, 0);
    EXPECT_EQ(PPC_PRF_MASK & PRF_ROOMFLAGS, 0);
    EXPECT_EQ(PPC_PRF_MASK & PRF_WIZ, 0);
    EXPECT_EQ(PPC_PRF_MASK & PRF_LOG1, 0);
    EXPECT_EQ(PPC_PRF_MASK & PRF_LOG2, 0);
    EXPECT_EQ(PPC_PRF_MASK & PRF_LOG3, 0);
}

TEST(AccountPreferences, DefaultsToAbsent)
{
    account::AccountData account;
    EXPECT_FALSE(account.preferences.present);
    EXPECT_EQ(account.preferences.preference_flags, 0);
}

TEST(AccountPreferences, ColourArraysAreZeroInitialised)
{
    account::AccountPreferences preferences;
    for (int index = 0; index < MAX_COLOR_FIELDS; ++index) {
        EXPECT_EQ(preferences.colors[index], 0) << "slot " << index;
        EXPECT_EQ(preferences.color_settings[index].foreground.mode, COLOR_VALUE_DEFAULT);
        EXPECT_EQ(preferences.color_settings[index].background.mode, COLOR_VALUE_DEFAULT);
    }
}

} // namespace
```

Register it in `src/CMakeLists.txt` by adding this line immediately after `tests/roster_cache_tests.cpp` (line 112):

```cmake
    tests/account_ppc_tests.cpp
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `scripts/rots-docker.sh test --gtest_filter='AccountPpc*'`
Expected: compile failure — `PPC_PRF_MASK` undeclared, and `AccountData` has no member `preferences`.

- [ ] **Step 3: Add the mask**

In `src/structs.h`, immediately after the last `PRF_*` define (`PRF_ADVANCED_PROMPT`, line 998):

```c
/* Player Preferred Config (PPC): the settings that describe how a player wants to see
   and hear the game, rather than anything about a character. These live on the account
   and are shared by every character on it; see
   docs/superpowers/specs/2026-09-05-account-level-player-preferred-config-design.md.
   This mask is the single definition of PPC membership -- every apply, read and compare
   against the account is masked with it, so no other preference bit is ever touched. */
#define PPC_PRF_MASK (PRF_PROMPT | PRF_ADVANCED_PROMPT | PRF_ADVANCED_VIEW      \
    | PRF_BRIEF | PRF_COMPACT | PRF_SPAM | PRF_WRAP | PRF_ECHO | PRF_MSDP      \
    | PRF_LATIN1 | PRF_SPINNER | PRF_INV_SORT1 | PRF_INV_SORT2 | PRF_COLOR)
```

- [ ] **Step 4: Add the type**

In `src/account_management_types.h`, add `#include "color.h"` to the existing includes, then define the struct immediately above `struct AccountData`:

```c++
// A player's preferred configuration: how they want to see and hear the game. Shared by
// every character on the account. `present` is false for an account that has never stored
// one -- that is the signal to seed it from the next character that plays.
struct AccountPreferences {
    bool present = false;
    long preference_flags = 0;
    char colors[MAX_COLOR_FIELDS] = {};
    color_slot_data color_settings[MAX_COLOR_FIELDS] = {};
};
```

and add this member to `AccountData`, immediately after `std::string roster_sort;`:

```c++
    AccountPreferences preferences;
```

- [ ] **Step 5: Run the tests to verify they pass**

Run: `scripts/rots-docker.sh test --gtest_filter='AccountPpc*'`
Expected: 4 tests PASS.

- [ ] **Step 6: Verify the server still builds**

Run: `scripts/rots-docker.sh compile`
Expected: builds to `bin/ageland` with no new warnings.

- [ ] **Step 7: Format and commit**

```bash
cd src && clang-format -i -style=WebKit structs.h account_management_types.h tests/account_ppc_tests.cpp && cd ..
git add src/structs.h src/account_management_types.h src/CMakeLists.txt src/tests/account_ppc_tests.cpp
git commit -m "feat(ppc): add the PPC mask and AccountPreferences type

PPC_PRF_MASK is the single definition of which settings are player-wide
rather than per-character, so every apply and compare can be masked and
no other preference bit is ever touched.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

### Task 2: Expose the colour-slot encoder and parser

The colour JSON emitter and parser already exist in `character_json.cpp`, but inside the anonymous namespace that opens at line 19 and closes at line 1827, so account code cannot call them. Expose thin wrappers so there is exactly one colour format in the codebase.

The existing emitter (`character_json.cpp:2160`) writes a **sparse** object keyed by slot name — only non-default slots appear:

```json
"colors": {"narrate": {"fg": {...}}, "tell": {"fg": {...}, "bg": {...}}}
```

**Files:**
- Modify: `src/character_json.h:163-171` (add two declarations next to the existing `encode_*`/`decode_*` exports)
- Modify: `src/character_json.cpp` (add definitions after the anonymous namespace closes at line 1827)
- Test: `src/tests/account_ppc_tests.cpp`

**Interfaces:**
- Consumes: Task 1's `MAX_COLOR_FIELDS`-sized arrays.
- Produces:
  - `std::string character_json::encode_color_slots_object(const char* colors, const color_slot_data* color_settings);` — returns the object **body without braces**, e.g. `"narrate": {...}, "tell": {...}`, so a caller can wrap it.
  - `bool character_json::parse_color_slots_object(json_utils::JsonReader* reader, char* colors, color_slot_data* color_settings, std::string* error_message);` — parses one `{...}` object from the reader into the two 16-element arrays, resetting both to defaults first.

- [ ] **Step 1: Write the failing test**

Append to `src/tests/account_ppc_tests.cpp` (inside the anonymous namespace), and add `#include "../character_json.h"` and `#include "../json_utils.h"` at the top:

```cpp
TEST(AccountPpcColorSlots, RoundTripsAnsiAndTruecolorSlots)
{
    char colors[MAX_COLOR_FIELDS] = {};
    color_slot_data settings[MAX_COLOR_FIELDS] = {};

    colors[COLOR_NARR] = CRED;
    settings[COLOR_NARR].foreground.mode = COLOR_VALUE_ANSI16;
    settings[COLOR_NARR].foreground.ansi = CRED;

    settings[COLOR_TELL].foreground.mode = COLOR_VALUE_TRUECOLOR;
    settings[COLOR_TELL].foreground.red = 18;
    settings[COLOR_TELL].foreground.green = 200;
    settings[COLOR_TELL].foreground.blue = 7;
    settings[COLOR_TELL].background.mode = COLOR_VALUE_ANSI16;
    settings[COLOR_TELL].background.ansi = CBBLU;

    const std::string body = character_json::encode_color_slots_object(colors, settings);
    const std::string document = "{\"colors\": {" + body + "}}";

    char decoded_colors[MAX_COLOR_FIELDS] = {};
    color_slot_data decoded_settings[MAX_COLOR_FIELDS] = {};
    std::string error_message;
    json_utils::JsonReader reader(document);
    const bool parsed = reader.parse_root_object(
        [&](const std::string& key, json_utils::JsonReader* nested, std::string* nested_error) {
            if (key == "colors")
                return character_json::parse_color_slots_object(nested, decoded_colors, decoded_settings, nested_error);
            return nested->skip_value(nested_error);
        },
        &error_message);

    ASSERT_TRUE(parsed) << error_message;
    EXPECT_EQ(decoded_colors[COLOR_NARR], CRED);
    EXPECT_EQ(decoded_settings[COLOR_TELL].foreground.mode, COLOR_VALUE_TRUECOLOR);
    EXPECT_EQ(decoded_settings[COLOR_TELL].foreground.red, 18);
    EXPECT_EQ(decoded_settings[COLOR_TELL].foreground.green, 200);
    EXPECT_EQ(decoded_settings[COLOR_TELL].foreground.blue, 7);
    EXPECT_EQ(decoded_settings[COLOR_TELL].background.mode, COLOR_VALUE_ANSI16);
    EXPECT_EQ(decoded_settings[COLOR_TELL].background.ansi, CBBLU);
}

TEST(AccountPpcColorSlots, OmitsDefaultSlotsAndResetsOnParse)
{
    char colors[MAX_COLOR_FIELDS] = {};
    color_slot_data settings[MAX_COLOR_FIELDS] = {};
    EXPECT_EQ(character_json::encode_color_slots_object(colors, settings), "");

    char decoded_colors[MAX_COLOR_FIELDS];
    color_slot_data decoded_settings[MAX_COLOR_FIELDS];
    for (int index = 0; index < MAX_COLOR_FIELDS; ++index) {
        decoded_colors[index] = CWHT;
        decoded_settings[index].foreground.mode = COLOR_VALUE_ANSI16;
    }

    std::string error_message;
    json_utils::JsonReader reader("{\"colors\": {}}");
    ASSERT_TRUE(reader.parse_root_object(
        [&](const std::string& key, json_utils::JsonReader* nested, std::string* nested_error) {
            if (key == "colors")
                return character_json::parse_color_slots_object(nested, decoded_colors, decoded_settings, nested_error);
            return nested->skip_value(nested_error);
        },
        &error_message)) << error_message;

    for (int index = 0; index < MAX_COLOR_FIELDS; ++index) {
        EXPECT_EQ(decoded_colors[index], 0) << "slot " << index;
        EXPECT_EQ(decoded_settings[index].foreground.mode, COLOR_VALUE_DEFAULT) << "slot " << index;
    }
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `scripts/rots-docker.sh test --gtest_filter='AccountPpcColorSlots.*'`
Expected: compile failure — `encode_color_slots_object` and `parse_color_slots_object` are not members of `character_json`.

- [ ] **Step 3: Declare the wrappers**

In `src/character_json.h`, after the `decode_hide_flags` declaration (line 171), add:

```c++
// Colour-slot JSON, shared with the account-level PPC store so the codebase has one
// colour format. encode returns the object BODY (no surrounding braces), sparse: only
// slots that differ from the default appear. parse resets both arrays to defaults and
// then fills them from one JSON object; both arrays must hold MAX_COLOR_FIELDS entries.
std::string encode_color_slots_object(const char* colors, const color_slot_data* color_settings);
bool parse_color_slots_object(json_utils::JsonReader* reader, char* colors,
    color_slot_data* color_settings, std::string* error_message = nullptr);
```

Ensure `character_json.h` includes `json_utils.h` and `color.h`; add whichever is missing.

- [ ] **Step 4: Define the wrappers**

In `src/character_json.cpp`, immediately after the anonymous namespace closes (`} // namespace`, line 1827), add definitions that reuse the existing private helpers:

```c++
std::string encode_color_slots_object(const char* colors, const color_slot_data* color_settings)
{
    if (colors == nullptr || color_settings == nullptr)
        return std::string();

    std::ostringstream output;
    bool wrote_any = false;
    for (int index = 0; index < MAX_COLOR_FIELDS; ++index) {
        ColorSettingData setting;
        setting.foreground = color_value_from_store(color_settings[index].foreground);
        setting.background = color_value_from_store(color_settings[index].background);
        if (is_default_color_value(setting.foreground) && colors[index] != CNRM) {
            setting.foreground.mode = COLOR_VALUE_ANSI16;
            setting.foreground.value = colors[index];
        }
        normalize_color_setting(&setting);
        if (is_default_color_value(setting.foreground) && is_default_color_value(setting.background))
            continue;

        if (wrote_any)
            output << ", ";
        output << "\"" << json_utils::escape_json_string(color_key_for_index(index)) << "\": ";
        write_color_setting(output, setting);
        wrote_any = true;
    }
    return output.str();
}

bool parse_color_slots_object(json_utils::JsonReader* reader, char* colors,
    color_slot_data* color_settings, std::string* error_message)
{
    if (reader == nullptr || colors == nullptr || color_settings == nullptr) {
        set_error(error_message, "Colour slot parser requires non-null parameters.");
        return false;
    }

    std::vector<int> parsed_colors(MAX_COLOR_FIELDS, 0);
    std::vector<ColorSettingData> parsed_settings(MAX_COLOR_FIELDS, default_color_setting());

    if (!reader->parse_object([&parsed_colors, &parsed_settings](const std::string& key,
                                  json_utils::JsonReader* nested_reader, std::string* nested_error_message) {
            const int index = color_index_for_key(key);
            if (index < 0) {
                set_error(nested_error_message, "Unknown color key.");
                return false;
            }
            return parse_color_setting_value(nested_reader, &parsed_settings[index], &parsed_colors, index, nested_error_message);
        },
            error_message))
        return false;

    for (int index = 0; index < MAX_COLOR_FIELDS; ++index) {
        colors[index] = static_cast<char>(parsed_colors[index]);
        color_settings[index].foreground = color_value_to_store(parsed_settings[index].foreground);
        color_settings[index].background = color_value_to_store(parsed_settings[index].background);
    }
    return true;
}
```

Every helper used above already exists in that anonymous namespace and was verified against this branch: `color_key_for_index(int)` (`character_json.cpp:651`), `write_color_setting(std::ostringstream&, const ColorSettingData&)` (`:746`), `normalize_color_setting`, `is_default_color_value`, `default_color_setting`, `color_index_for_key` (`:679`), `parse_color_setting_value` (`:842`), `color_value_to_store`, `color_value_from_store`, and `set_error`. Reuse them; do not duplicate any of their logic.

- [ ] **Step 5: Run the tests to verify they pass**

Run: `scripts/rots-docker.sh test --gtest_filter='AccountPpcColorSlots.*'`
Expected: 2 tests PASS.

- [ ] **Step 6: Verify no character-file regression**

Run: `scripts/rots-docker.sh test --gtest_filter='CharacterJson*'`
Expected: same pass/fail set as before this task. Compare against a baseline run on `HEAD~1` if anything looks off.

- [ ] **Step 7: Format and commit**

```bash
cd src && clang-format -i -style=WebKit character_json.h character_json.cpp tests/account_ppc_tests.cpp && cd ..
git add src/character_json.h src/character_json.cpp src/tests/account_ppc_tests.cpp
git commit -m "feat(ppc): expose the colour-slot encoder and parser

The account PPC store needs the same colour JSON the character file
already uses. Expose thin wrappers over the existing private helpers
rather than growing a second colour format.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

### Task 3: Persist `preferences` in `account.json`

**Files:**
- Modify: `src/account_management_storage.cpp:118-126` (the serializer tail, beside `roster_sort`)
- Modify: `src/account_management.cpp:1487-1489` (`parse_account_property`, beside the `roster_sort` case)
- Test: `src/tests/account_ppc_tests.cpp`

**Interfaces:**
- Consumes: `account::AccountPreferences` (Task 1); `character_json::encode_color_slots_object` / `parse_color_slots_object` (Task 2); the already-exported `character_json::encode_preference_flags(long)` and `character_json::decode_preference_flags(const std::vector<std::string>&, long*, std::string*)` (`character_json.h:164`, `:169`).
- Produces: a `preferences` key in the account JSON, round-tripping through the existing `serialize_account_to_json` / `deserialize_account_from_json`.

Emitted shape:

```json
"preferences": {"flags": ["brief", "compact"], "colors": {"narrate": {...}}},
```

- [ ] **Step 1: Write the failing test**

Append to `src/tests/account_ppc_tests.cpp`, adding `#include "../account_management.h"` at the top:

```cpp
account::AccountData make_minimal_account()
{
    account::AccountData account;
    account.account_name = "ppctester";
    account.normalized_email = "ppctester@example.com";
    account.password_hash = "hash";
    account.password_salt = "salt";
    return account;
}

TEST(AccountPpcStorage, RoundTripsPreferences)
{
    account::AccountData account = make_minimal_account();
    account.preferences.present = true;
    account.preferences.preference_flags = PRF_BRIEF | PRF_COMPACT | PRF_MSDP;
    account.preferences.colors[COLOR_NARR] = CRED;
    account.preferences.color_settings[COLOR_NARR].foreground.mode = COLOR_VALUE_ANSI16;
    account.preferences.color_settings[COLOR_NARR].foreground.ansi = CRED;

    const std::string json = account::serialize_account_to_json(account);
    account::AccountData reloaded;
    std::string error_message;
    ASSERT_TRUE(account::deserialize_account_from_json(json, &reloaded, &error_message)) << error_message;

    EXPECT_TRUE(reloaded.preferences.present);
    EXPECT_EQ(reloaded.preferences.preference_flags, PRF_BRIEF | PRF_COMPACT | PRF_MSDP);
    EXPECT_EQ(reloaded.preferences.colors[COLOR_NARR], CRED);
    EXPECT_EQ(reloaded.preferences.color_settings[COLOR_NARR].foreground.mode, COLOR_VALUE_ANSI16);
    EXPECT_EQ(reloaded.preferences.color_settings[COLOR_NARR].foreground.ansi, CRED);
}

TEST(AccountPpcStorage, AbsentPreferencesKeyYieldsNotPresent)
{
    account::AccountData account = make_minimal_account();
    const std::string json = account::serialize_account_to_json(account);
    ASSERT_EQ(json.find("\"preferences\""), std::string::npos)
        << "An account with no PPC must not emit a preferences key.";

    account::AccountData reloaded;
    std::string error_message;
    ASSERT_TRUE(account::deserialize_account_from_json(json, &reloaded, &error_message)) << error_message;
    EXPECT_FALSE(reloaded.preferences.present);
    EXPECT_EQ(reloaded.version, 1);
}

TEST(AccountPpcStorage, SchemaVersionIsNotBumped)
{
    EXPECT_EQ(account::ACCOUNT_SCHEMA_VERSION, 1)
        << "Bumping the version makes deserialize reject every existing account file.";

    account::AccountData account = make_minimal_account();
    account.preferences.present = true;
    account.preferences.preference_flags = PRF_BRIEF;
    account::AccountData reloaded;
    std::string error_message;
    ASSERT_TRUE(account::deserialize_account_from_json(
        account::serialize_account_to_json(account), &reloaded, &error_message)) << error_message;
    EXPECT_EQ(reloaded.version, 1);
}

TEST(AccountPpcStorage, PreferencesKeyIsNotTheFinalField)
{
    account::AccountData account = make_minimal_account();
    account.preferences.present = true;
    account.preferences.preference_flags = PRF_BRIEF;
    const std::string json = account::serialize_account_to_json(account);
    EXPECT_LT(json.find("\"preferences\""), json.find("\"password_reset_attempt_count\""))
        << "password_reset_attempt_count must stay the last, comma-less field.";
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `scripts/rots-docker.sh test --gtest_filter='AccountPpcStorage.*'`
Expected: `RoundTripsPreferences` FAILS (`present` is false after reload) and `PreferencesKeyIsNotTheFinalField` FAILS (`npos` for the missing key). The other two pass already.

- [ ] **Step 3: Emit the key**

In `src/account_management_storage.cpp`, immediately after the `roster_sort` line and before `password_reset_attempt_count` (respecting the existing comment about the final comma-less field), add:

```c++
    if (account.preferences.present) {
        output << "  \"preferences\": {\"flags\": [";
        const std::vector<std::string> flag_names
            = character_json::encode_preference_flags(account.preferences.preference_flags & PPC_PRF_MASK);
        for (size_t index = 0; index < flag_names.size(); ++index) {
            if (index > 0)
                output << ", ";
            output << "\"" << json_utils::escape_json_string(flag_names[index]) << "\"";
        }
        output << "], \"colors\": {"
               << character_json::encode_color_slots_object(
                      account.preferences.colors, account.preferences.color_settings)
               << "}},\n";
    }
```

Add `#include "character_json.h"` and `#include "structs.h"` to this file if they are not already present.

- [ ] **Step 4: Parse the key**

In `src/account_management.cpp`, in `parse_account_property`, immediately after the `roster_sort` case and before the final `return reader->skip_value(error_message);`:

```c++
        if (key == "preferences") {
            account->preferences.present = true;
            return reader->parse_object([account](const std::string& nested_key,
                                            json_utils::JsonReader* nested_reader, std::string* nested_error_message) {
                if (nested_key == "flags") {
                    std::vector<std::string> flag_names;
                    if (!nested_reader->parse_string_array(&flag_names, nested_error_message))
                        return false;
                    long flags = 0;
                    if (!character_json::decode_preference_flags(flag_names, &flags, nested_error_message))
                        return false;
                    account->preferences.preference_flags = flags & PPC_PRF_MASK;
                    return true;
                }
                if (nested_key == "colors") {
                    return character_json::parse_color_slots_object(nested_reader,
                        account->preferences.colors, account->preferences.color_settings, nested_error_message);
                }
                return nested_reader->skip_value(nested_error_message);
            },
                error_message);
        }
```

- [ ] **Step 5: Run the tests to verify they pass**

Run: `scripts/rots-docker.sh test --gtest_filter='AccountPpcStorage.*'`
Expected: 4 tests PASS.

- [ ] **Step 6: Verify no account regression**

Run: `scripts/rots-docker.sh test --gtest_filter='AccountManagement*:AccountCache*:RosterCache*'`
Expected: same pass/fail set as the pre-task baseline.

- [ ] **Step 7: Format and commit**

```bash
cd src && clang-format -i -style=WebKit account_management_storage.cpp account_management.cpp tests/account_ppc_tests.cpp && cd ..
git add src/account_management_storage.cpp src/account_management.cpp src/tests/account_ppc_tests.cpp
git commit -m "feat(ppc): persist preferences in account.json

Additive optional key at schema version 1, following the roster_sort
precedent: an unrecognised key is skipped by parse_account_property, so
old files load and a rolled-back binary ignores the new key. Bumping the
version instead would make deserialize reject every existing account.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

### Task 4: The `account_ppc` unit

The pure logic, with no I/O and no descriptor walking, so it is fully unit-testable.

**Files:**
- Create: `src/account_ppc.h`, `src/account_ppc.cpp`
- Modify: `src/CMakeLists.txt:40` (add `account_ppc.cpp` beside `account_cache.cpp`)
- Modify: `src/Makefile:29` (add `account_ppc.o` to the object list) and add a build rule beside the `account_cache.o` rule at line 50
- Test: `src/tests/account_ppc_tests.cpp`

**Interfaces:**
- Consumes: `account::AccountPreferences`, `PPC_PRF_MASK`, `char_data`.
- Produces:
  - `account::AccountPreferences ppc_read_from_character(const struct char_data* ch);` — returns `present = true` with the character's masked flags and both colour arrays. Returns a default-constructed (`present = false`) value when `ch` or `ch->profs` is null.
  - `void ppc_write_to_character(const account::AccountPreferences& preferences, struct char_data* ch);` — applies masked flags and both colour arrays. No-op when `preferences.present` is false, or `ch`/`ch->profs` is null.
  - `bool ppc_equal(const account::AccountPreferences& left, const account::AccountPreferences& right);` — compares masked flags and both colour arrays. Ignores `present`.

- [ ] **Step 1: Write the failing test**

Append to `src/tests/account_ppc_tests.cpp`, adding `#include "../account_ppc.h"` and `#include "../db.h"` at the top:

```cpp
struct PpcTestCharacter {
    PpcTestCharacter()
        : character(new char_data {})
    {
        clear_char(character, MOB_VOID);
        character->profs = &prof_storage;
    }
    ~PpcTestCharacter() { delete character; }

    char_prof_data prof_storage {};
    char_data* character;
};

TEST(AccountPpcApply, PreservesNonPpcPreferenceBits)
{
    PpcTestCharacter subject;
    PRF_FLAGS(subject.character) = PRF_NOTELL | PRF_SING | PRF_MENTAL | PRF_SWIM | PRF_BRIEF;

    account::AccountPreferences preferences;
    preferences.present = true;
    preferences.preference_flags = PRF_COMPACT | PRF_MSDP;

    ppc_write_to_character(preferences, subject.character);

    EXPECT_TRUE(PRF_FLAGGED(subject.character, PRF_NOTELL));
    EXPECT_TRUE(PRF_FLAGGED(subject.character, PRF_SING));
    EXPECT_TRUE(PRF_FLAGGED(subject.character, PRF_MENTAL));
    EXPECT_TRUE(PRF_FLAGGED(subject.character, PRF_SWIM));
    EXPECT_TRUE(PRF_FLAGGED(subject.character, PRF_COMPACT));
    EXPECT_TRUE(PRF_FLAGGED(subject.character, PRF_MSDP));
    EXPECT_FALSE(PRF_FLAGGED(subject.character, PRF_BRIEF))
        << "A PPC bit clear on the account must be cleared on the character.";
}

TEST(AccountPpcApply, IsANoOpWhenPreferencesAreAbsent)
{
    PpcTestCharacter subject;
    PRF_FLAGS(subject.character) = PRF_BRIEF | PRF_NOTELL;

    account::AccountPreferences preferences;
    ppc_write_to_character(preferences, subject.character);

    EXPECT_TRUE(PRF_FLAGGED(subject.character, PRF_BRIEF));
    EXPECT_TRUE(PRF_FLAGGED(subject.character, PRF_NOTELL));
}

TEST(AccountPpcApply, RoundTripsThroughACharacter)
{
    PpcTestCharacter source;
    PRF_FLAGS(source.character) = PRF_BRIEF | PRF_WRAP | PRF_COLOR | PRF_NOTELL;
    source.character->profs->colors[COLOR_CHAT] = CGRN;
    source.character->profs->color_settings[COLOR_CHAT].foreground.mode = COLOR_VALUE_ANSI16;
    source.character->profs->color_settings[COLOR_CHAT].foreground.ansi = CGRN;

    const account::AccountPreferences preferences = ppc_read_from_character(source.character);
    EXPECT_TRUE(preferences.present);
    EXPECT_EQ(preferences.preference_flags & PRF_NOTELL, 0)
        << "ppc_read_from_character must mask out non-PPC bits.";

    PpcTestCharacter destination;
    ppc_write_to_character(preferences, destination.character);

    EXPECT_TRUE(PRF_FLAGGED(destination.character, PRF_BRIEF));
    EXPECT_TRUE(PRF_FLAGGED(destination.character, PRF_WRAP));
    EXPECT_TRUE(PRF_FLAGGED(destination.character, PRF_COLOR));
    EXPECT_EQ(destination.character->profs->colors[COLOR_CHAT], CGRN);
    EXPECT_EQ(destination.character->profs->color_settings[COLOR_CHAT].foreground.ansi, CGRN);
}

TEST(AccountPpcEqual, DetectsFlagAndColourDifferences)
{
    account::AccountPreferences left;
    left.present = true;
    left.preference_flags = PRF_BRIEF;
    account::AccountPreferences right = left;
    EXPECT_TRUE(ppc_equal(left, right));

    right.preference_flags |= PRF_COMPACT;
    EXPECT_FALSE(ppc_equal(left, right));

    right = left;
    right.colors[COLOR_SAY] = CYEL;
    EXPECT_FALSE(ppc_equal(left, right));

    right = left;
    right.color_settings[COLOR_SAY].background.mode = COLOR_VALUE_TRUECOLOR;
    EXPECT_FALSE(ppc_equal(left, right));
}

TEST(AccountPpcEqual, IgnoresNonPpcBitsAndPresence)
{
    account::AccountPreferences left;
    left.present = true;
    left.preference_flags = PRF_BRIEF | PRF_NOTELL;
    account::AccountPreferences right;
    right.present = false;
    right.preference_flags = PRF_BRIEF | PRF_SING;
    EXPECT_TRUE(ppc_equal(left, right));
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `scripts/rots-docker.sh test --gtest_filter='AccountPpcApply.*:AccountPpcEqual.*'`
Expected: compile failure — `account_ppc.h` does not exist.

- [ ] **Step 3: Write the header**

Create `src/account_ppc.h`:

```c++
#ifndef ACCOUNT_PPC_H
#define ACCOUNT_PPC_H

#include "account_management_types.h"

struct char_data;

/* Player Preferred Config: the account-owned settings that describe how a player wants to
   see and hear the game. The account is the store; the character struct stays the runtime
   representation, so nothing that reads a PPC value has to change. Every operation here is
   masked with PPC_PRF_MASK, so no other preference bit is ever read or written.
   See docs/superpowers/specs/2026-09-05-account-level-player-preferred-config-design.md. */

/* Snapshot a character's PPC. Returns present = true on success, or a default-constructed
   (present = false) value when ch or ch->profs is null. */
account::AccountPreferences ppc_read_from_character(const struct char_data* ch);

/* Apply stored preferences onto a character, leaving every non-PPC bit untouched.
   Does nothing when preferences.present is false, or ch/ch->profs is null. */
void ppc_write_to_character(const account::AccountPreferences& preferences, struct char_data* ch);

/* Compare two PPCs by value: masked flags plus both colour arrays. Ignores `present`.
   save_char uses this to write the account only when something actually changed. */
bool ppc_equal(const account::AccountPreferences& left, const account::AccountPreferences& right);

#endif /* ACCOUNT_PPC_H */
```

- [ ] **Step 4: Write the implementation**

Create `src/account_ppc.cpp`:

```c++
#include "account_ppc.h"

#include "color.h"
#include "structs.h"
#include "utils.h"

#include <cstring>

account::AccountPreferences ppc_read_from_character(const struct char_data* ch)
{
    account::AccountPreferences preferences;
    if (ch == nullptr || ch->profs == nullptr)
        return preferences;

    preferences.present = true;
    preferences.preference_flags = PRF_FLAGS(ch) & PPC_PRF_MASK;
    std::memcpy(preferences.colors, ch->profs->colors, sizeof(preferences.colors));
    std::memcpy(preferences.color_settings, ch->profs->color_settings, sizeof(preferences.color_settings));
    return preferences;
}

void ppc_write_to_character(const account::AccountPreferences& preferences, struct char_data* ch)
{
    if (!preferences.present || ch == nullptr || ch->profs == nullptr)
        return;

    PRF_FLAGS(ch) = (PRF_FLAGS(ch) & ~PPC_PRF_MASK) | (preferences.preference_flags & PPC_PRF_MASK);
    std::memcpy(ch->profs->colors, preferences.colors, sizeof(preferences.colors));
    std::memcpy(ch->profs->color_settings, preferences.color_settings, sizeof(preferences.color_settings));
}

bool ppc_equal(const account::AccountPreferences& left, const account::AccountPreferences& right)
{
    if ((left.preference_flags & PPC_PRF_MASK) != (right.preference_flags & PPC_PRF_MASK))
        return false;
    if (std::memcmp(left.colors, right.colors, sizeof(left.colors)) != 0)
        return false;
    return std::memcmp(left.color_settings, right.color_settings, sizeof(left.color_settings)) == 0;
}
```

`PRF_FLAGS(ch)` is used as an lvalue elsewhere in the tree (for example `REMOVE_BIT(PRF_FLAGS(ch), ...)` in `act_othe.cpp`), so the assignment above is valid. `color_slot_data` is a plain aggregate of scalars, so `memcmp` is a sound comparison for it.

Register the new source file. In `src/CMakeLists.txt`, after `account_cache.cpp` (line 40):

```cmake
    account_ppc.cpp
```

In `src/Makefile`, add `account_ppc.o` to the object list on line 29 (next to `account_cache.o`), and add this rule beside the `account_cache.o` rule at line 50:

```make
account_ppc.o : account_ppc.cpp account_ppc.h account_management_types.h color.h structs.h utils.h
	$(CC) -c $(CFLAGS) account_ppc.cpp
```

- [ ] **Step 5: Run the tests to verify they pass**

Run: `scripts/rots-docker.sh test --gtest_filter='AccountPpcApply.*:AccountPpcEqual.*'`
Expected: 5 tests PASS.

- [ ] **Step 6: Verify both build systems**

Run: `scripts/rots-docker.sh compile`
Expected: builds to `bin/ageland`. This exercises the `Makefile` path; CMake was exercised by the test run.

- [ ] **Step 7: Format and commit**

```bash
cd src && clang-format -i -style=WebKit account_ppc.h account_ppc.cpp tests/account_ppc_tests.cpp && cd ..
git add src/account_ppc.h src/account_ppc.cpp src/CMakeLists.txt src/Makefile src/tests/account_ppc_tests.cpp
git commit -m "feat(ppc): add the account_ppc unit

Pure read/write/compare between a character and stored preferences, with
no I/O, so the masking rule that protects every non-PPC bit is covered by
unit tests rather than only observable on a booted server.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

### Task 5: Apply and seed on login

**Files:**
- Modify: `src/account_ppc.h`, `src/account_ppc.cpp` (add the account-aware entry point)
- Modify: `src/interpre.cpp` — the three enter-game sites: the unswitched-reconnect path (`STATE(d) = CON_PLYNG` at :2763), the reconnect/usurp path (:2800), and the fresh enter-game path (:3522-3542)
- Test: `src/tests/account_ppc_tests.cpp`

**Interfaces:**
- Consumes: `ppc_read_from_character`, `ppc_write_to_character` (Task 4); `account::read_account_file`, `account::write_account_file` (`account_management_storage.h:22-23`).
- Produces: `void ppc_apply_account_to_character(const char* account_name, struct char_data* ch);` — reads the account, applies its PPC if present, otherwise seeds the account from this character and writes it once. Silent no-op on a null/empty account name, a null character, or a failed read; a failed write is logged and otherwise ignored.

- [ ] **Step 1: Write the failing test**

Append to `src/tests/account_ppc_tests.cpp`. It uses the `TemporaryDirectory` pattern already established in `src/tests/account_management_tests.cpp:27` — copy that helper class into this file's anonymous namespace if it is not shared.

```cpp
TEST(AccountPpcLogin, SeedsAnAccountThatHasNoPreferences)
{
    TemporaryDirectory root;
    account::AccountData account = make_minimal_account();
    ASSERT_TRUE(account::write_account_file(root.path(), account, nullptr));

    PpcTestCharacter subject;
    PRF_FLAGS(subject.character) = PRF_BRIEF | PRF_WRAP | PRF_NOTELL;
    subject.character->profs->colors[COLOR_SAY] = CYEL;

    ppc_apply_account_to_character_in(root.path(), account.account_name.c_str(), subject.character);

    account::AccountData reloaded;
    ASSERT_TRUE(account::read_account_file_uncached(root.path(), account.account_name, &reloaded, nullptr));
    EXPECT_TRUE(reloaded.preferences.present);
    EXPECT_EQ(reloaded.preferences.preference_flags, PRF_BRIEF | PRF_WRAP);
    EXPECT_EQ(reloaded.preferences.colors[COLOR_SAY], CYEL);
    EXPECT_TRUE(PRF_FLAGGED(subject.character, PRF_NOTELL))
        << "Seeding must not disturb the character.";
}

TEST(AccountPpcLogin, AppliesStoredPreferencesToTheCharacter)
{
    TemporaryDirectory root;
    account::AccountData account = make_minimal_account();
    account.preferences.present = true;
    account.preferences.preference_flags = PRF_COMPACT | PRF_MSDP;
    account.preferences.colors[COLOR_TELL] = CBCYN;
    ASSERT_TRUE(account::write_account_file(root.path(), account, nullptr));

    PpcTestCharacter subject;
    PRF_FLAGS(subject.character) = PRF_BRIEF | PRF_SING;

    ppc_apply_account_to_character_in(root.path(), account.account_name.c_str(), subject.character);

    EXPECT_TRUE(PRF_FLAGGED(subject.character, PRF_COMPACT));
    EXPECT_TRUE(PRF_FLAGGED(subject.character, PRF_MSDP));
    EXPECT_FALSE(PRF_FLAGGED(subject.character, PRF_BRIEF));
    EXPECT_TRUE(PRF_FLAGGED(subject.character, PRF_SING)) << "Non-PPC bits survive.";
    EXPECT_EQ(subject.character->profs->colors[COLOR_TELL], CBCYN);
}

TEST(AccountPpcLogin, IsSafeWhenTheAccountCannotBeRead)
{
    TemporaryDirectory root;
    PpcTestCharacter subject;
    PRF_FLAGS(subject.character) = PRF_BRIEF | PRF_NOTELL;

    ppc_apply_account_to_character_in(root.path(), "nosuchaccount", subject.character);

    EXPECT_TRUE(PRF_FLAGGED(subject.character, PRF_BRIEF))
        << "An unreadable account must never blank a player's settings.";
    EXPECT_TRUE(PRF_FLAGGED(subject.character, PRF_NOTELL));
}

TEST(AccountPpcLogin, IsSafeWithNoAccountName)
{
    TemporaryDirectory root;
    PpcTestCharacter subject;
    PRF_FLAGS(subject.character) = PRF_BRIEF;
    ppc_apply_account_to_character_in(root.path(), "", subject.character);
    ppc_apply_account_to_character_in(root.path(), nullptr, subject.character);
    EXPECT_TRUE(PRF_FLAGGED(subject.character, PRF_BRIEF));
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `scripts/rots-docker.sh test --gtest_filter='AccountPpcLogin.*'`
Expected: compile failure — `ppc_apply_account_to_character_in` is not declared.

- [ ] **Step 3: Implement the entry point**

Add to `src/account_ppc.h`:

```c++
/* Apply the account's stored PPC to a character at login. When the account has none yet,
   seed it from this character and write it once -- that is the migration path, and the
   seeding character is by definition the account's most recently played one.
   Never blocks login: a null/empty account name, a null character, or a failed read is a
   silent no-op, and a failed write is logged and otherwise ignored. */
void ppc_apply_account_to_character(const char* account_name, struct char_data* ch);

/* Same, against an explicit storage root. Production calls the "." form above; this exists
   so tests can drive a temporary directory. */
void ppc_apply_account_to_character_in(const std::string& root_directory,
    const char* account_name, struct char_data* ch);
```

Add `#include <string>` to the header.

Add to `src/account_ppc.cpp`:

```c++
#include "account_management.h"
#include "comm.h"

void ppc_apply_account_to_character_in(const std::string& root_directory,
    const char* account_name, struct char_data* ch)
{
    if (account_name == nullptr || *account_name == '\0' || ch == nullptr || ch->profs == nullptr)
        return;

    account::AccountData account;
    std::string error_message;
    if (!account::read_account_file(root_directory, account_name, &account, &error_message)) {
        vmudlog(NRM, "ppc: could not read account %s to apply preferences: %s",
            account_name, error_message.c_str());
        return;
    }

    if (account.preferences.present) {
        ppc_write_to_character(account.preferences, ch);
        return;
    }

    /* No stored PPC yet: seed it from the character being played. */
    account.preferences = ppc_read_from_character(ch);
    if (!account.preferences.present)
        return;
    if (!account::write_account_file(root_directory, account, &error_message)) {
        vmudlog(NRM, "ppc: could not seed preferences for account %s: %s",
            account_name, error_message.c_str());
    }
}

void ppc_apply_account_to_character(const char* account_name, struct char_data* ch)
{
    ppc_apply_account_to_character_in(".", account_name, ch);
}
```

Confirm `vmudlog`'s declaring header before writing the include — `grep -rn "void vmudlog" src/*.h`. Use whatever header declares it.

- [ ] **Step 4: Run the tests to verify they pass**

Run: `scripts/rots-docker.sh test --gtest_filter='AccountPpcLogin.*'`
Expected: 4 tests PASS.

- [ ] **Step 5: Wire the three enter-game sites**

In `src/interpre.cpp`, add `#include "account_ppc.h"` to the includes, then insert this call at each of the three points where a character enters play, immediately **before** `STATE(d) = CON_PLYNG;` so the settings are live for the first output of the session:

```c++
    ppc_apply_account_to_character(d->account_name, d->character);
```

The three sites (verify each by reading the surrounding lines — line numbers drift):
1. `interpre.cpp:2763` — reconnect to an unswitched character. Note `d->character` has just been reassigned to `tmp_ch->desc->original`; place the call after that reassignment.
2. `interpre.cpp:2800` — reconnect/usurp. `d->character` has just been reassigned to `tmp_ch`; place the call after that reassignment.
3. `interpre.cpp:3522-3542` — the fresh enter-game path. Add the call before each `STATE(d) = CON_PLYNG;` in that block.

The function is idempotent, so applying again on a reconnect to an already-configured in-memory body is harmless.

- [ ] **Step 6: Verify the build and the full account suite**

Run: `scripts/rots-docker.sh compile`
Then: `scripts/rots-docker.sh test --gtest_filter='AccountPpc*:AccountManagement*:Interpre*'`
Expected: build succeeds; `AccountPpc*` all pass; the others match the pre-task baseline.

- [ ] **Step 7: Format and commit**

```bash
cd src && clang-format -i -style=WebKit account_ppc.h account_ppc.cpp interpre.cpp tests/account_ppc_tests.cpp && cd ..
git add src/account_ppc.h src/account_ppc.cpp src/interpre.cpp src/tests/account_ppc_tests.cpp
git commit -m "feat(ppc): apply the account PPC at login, seeding when absent

Migration is implicit: an account with no stored preferences is seeded
from the first character that plays, which is by definition its most
recently played one. A failed account read never blocks login and never
blanks a player's settings.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

### Task 6: Write the account back on save, only when it changed

**Files:**
- Modify: `src/account_ppc.h`, `src/account_ppc.cpp`
- Modify: `src/db.cpp:3219-3240` (inside `save_char`, after `owner_account_name` is resolved)
- Test: `src/tests/account_ppc_tests.cpp`

**Interfaces:**
- Consumes: `ppc_read_from_character`, `ppc_equal` (Task 4); `owner_account_name` already resolved inside `save_char` (`db.cpp:3219`).
- Produces: `bool ppc_store_character_to_account_in(const std::string& root_directory, const std::string& account_name, const struct char_data* ch);` — returns `true` when it wrote, `false` when nothing changed or on failure. Plus `void ppc_store_character_to_account(const std::string& account_name, const struct char_data* ch);` for production use.

- [ ] **Step 1: Write the failing test**

Append to `src/tests/account_ppc_tests.cpp`:

```cpp
TEST(AccountPpcSave, DoesNotWriteWhenNothingChanged)
{
    TemporaryDirectory root;
    account::AccountData account = make_minimal_account();
    account.preferences.present = true;
    account.preferences.preference_flags = PRF_BRIEF | PRF_WRAP;
    ASSERT_TRUE(account::write_account_file(root.path(), account, nullptr));

    PpcTestCharacter subject;
    PRF_FLAGS(subject.character) = PRF_BRIEF | PRF_WRAP | PRF_NOTELL;

    EXPECT_FALSE(ppc_store_character_to_account_in(root.path(), account.account_name, subject.character))
        << "An unchanged PPC must not write the account: every write flushes the account cache.";
}

TEST(AccountPpcSave, WritesWhenAFlagChanged)
{
    TemporaryDirectory root;
    account::AccountData account = make_minimal_account();
    account.preferences.present = true;
    account.preferences.preference_flags = PRF_BRIEF;
    ASSERT_TRUE(account::write_account_file(root.path(), account, nullptr));

    PpcTestCharacter subject;
    PRF_FLAGS(subject.character) = PRF_BRIEF | PRF_COMPACT;

    EXPECT_TRUE(ppc_store_character_to_account_in(root.path(), account.account_name, subject.character));

    account::AccountData reloaded;
    ASSERT_TRUE(account::read_account_file_uncached(root.path(), account.account_name, &reloaded, nullptr));
    EXPECT_EQ(reloaded.preferences.preference_flags, PRF_BRIEF | PRF_COMPACT);
}

TEST(AccountPpcSave, WritesWhenAColourChanged)
{
    TemporaryDirectory root;
    account::AccountData account = make_minimal_account();
    account.preferences.present = true;
    account.preferences.preference_flags = PRF_BRIEF;
    ASSERT_TRUE(account::write_account_file(root.path(), account, nullptr));

    PpcTestCharacter subject;
    PRF_FLAGS(subject.character) = PRF_BRIEF;
    subject.character->profs->colors[COLOR_YELL] = CBMAG;
    subject.character->profs->color_settings[COLOR_YELL].foreground.mode = COLOR_VALUE_ANSI16;
    subject.character->profs->color_settings[COLOR_YELL].foreground.ansi = CBMAG;

    EXPECT_TRUE(ppc_store_character_to_account_in(root.path(), account.account_name, subject.character));

    account::AccountData reloaded;
    ASSERT_TRUE(account::read_account_file_uncached(root.path(), account.account_name, &reloaded, nullptr));
    EXPECT_EQ(reloaded.preferences.colors[COLOR_YELL], CBMAG);
}

TEST(AccountPpcSave, PreservesUnrelatedAccountFields)
{
    TemporaryDirectory root;
    account::AccountData account = make_minimal_account();
    account.roster_sort = "level";
    account.email_verified = true;
    account.characters.push_back("someone");
    account.preferences.present = true;
    account.preferences.preference_flags = PRF_BRIEF;
    ASSERT_TRUE(account::write_account_file(root.path(), account, nullptr));

    PpcTestCharacter subject;
    PRF_FLAGS(subject.character) = PRF_BRIEF | PRF_COMPACT;
    ASSERT_TRUE(ppc_store_character_to_account_in(root.path(), account.account_name, subject.character));

    account::AccountData reloaded;
    ASSERT_TRUE(account::read_account_file_uncached(root.path(), account.account_name, &reloaded, nullptr));
    EXPECT_EQ(reloaded.roster_sort, "level");
    EXPECT_TRUE(reloaded.email_verified);
    ASSERT_EQ(reloaded.characters.size(), 1u);
    EXPECT_EQ(reloaded.characters[0], "someone");
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `scripts/rots-docker.sh test --gtest_filter='AccountPpcSave.*'`
Expected: compile failure — `ppc_store_character_to_account_in` is not declared.

- [ ] **Step 3: Implement**

Add to `src/account_ppc.h`:

```c++
/* Write a character's PPC back to its account, but only when it actually differs from what
   is stored. The compare matters: write_account_file triggers a full account_cache flush,
   and the cache assumes account mutations never happen on the save path. Returns true only
   when a write occurred. */
bool ppc_store_character_to_account_in(const std::string& root_directory,
    const std::string& account_name, const struct char_data* ch);
void ppc_store_character_to_account(const std::string& account_name, const struct char_data* ch);
```

Add to `src/account_ppc.cpp`:

```c++
bool ppc_store_character_to_account_in(const std::string& root_directory,
    const std::string& account_name, const struct char_data* ch)
{
    if (account_name.empty() || ch == nullptr || ch->profs == nullptr)
        return false;

    const account::AccountPreferences current = ppc_read_from_character(ch);
    if (!current.present)
        return false;

    account::AccountData account;
    std::string error_message;
    if (!account::read_account_file(root_directory, account_name, &account, &error_message)) {
        vmudlog(NRM, "ppc: could not read account %s to store preferences: %s",
            account_name.c_str(), error_message.c_str());
        return false;
    }

    if (account.preferences.present && ppc_equal(account.preferences, current))
        return false;

    account.preferences = current;
    if (!account::write_account_file(root_directory, account, &error_message)) {
        vmudlog(NRM, "ppc: could not store preferences for account %s: %s",
            account_name.c_str(), error_message.c_str());
        return false;
    }
    return true;
}

void ppc_store_character_to_account(const std::string& account_name, const struct char_data* ch)
{
    ppc_store_character_to_account_in(".", account_name, ch);
}
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `scripts/rots-docker.sh test --gtest_filter='AccountPpcSave.*'`
Expected: 4 tests PASS.

- [ ] **Step 5: Wire it into `save_char`**

In `src/db.cpp`, add `#include "account_ppc.h"`. Inside `save_char`, in the `if (linked_character) {` block that begins at line 3222 — where `owner_account_name` is known to be non-empty — add:

```c++
        ppc_store_character_to_account(owner_account_name, ch);
```

Place it at the end of that block, after the existing account-character-file write, so a PPC failure cannot prevent the character itself from being saved.

- [ ] **Step 6: Verify the build and that saving did not regress**

Run: `scripts/rots-docker.sh compile`
Then: `scripts/rots-docker.sh test --gtest_filter='AccountPpc*:AccountManagement*:CharacterJson*'`
Expected: build succeeds; `AccountPpc*` all pass; the others match the pre-task baseline.

- [ ] **Step 7: Format and commit**

```bash
cd src && clang-format -i -style=WebKit account_ppc.h account_ppc.cpp db.cpp tests/account_ppc_tests.cpp && cd ..
git add src/account_ppc.h src/account_ppc.cpp src/db.cpp src/tests/account_ppc_tests.cpp
git commit -m "feat(ppc): store the PPC back to the account on save

Compares before writing. write_account_file triggers a full account_cache
flush and the cache assumes account mutations never occur on the save
path, so an unconditional write would flush the cache on every save and
undo the roster cache work.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

### Task 7: Propagate a change to the account's other online characters

Without this, a player with two characters online who changes a setting on one has it silently reverted when the other saves its stale copy.

**Files:**
- Modify: `src/account_ppc.h`, `src/account_ppc.cpp`
- Modify: `src/act_othe.cpp` — end of `do_gen_tog` (:1014-1163) and end of `do_inventory_sort` (:1360)
- Modify: `src/color.cpp` — end of `do_color` (:495)
- Test: `src/tests/account_ppc_tests.cpp`

**Interfaces:**
- Consumes: `ppc_read_from_character`, `ppc_write_to_character` (Task 4).
- Produces:
  - `void ppc_copy_between_characters(const struct char_data* source, struct char_data* destination);` — pure, testable; copies masked flags and both colour arrays from source to destination. No-op if either is null or lacks `profs`, or if they are the same character.
  - `void ppc_propagate_from(const struct char_data* ch);` — walks `descriptor_list` and calls the above for every other playing character on the same account.

- [ ] **Step 1: Write the failing test**

Append to `src/tests/account_ppc_tests.cpp`:

```cpp
TEST(AccountPpcPropagate, CopiesPpcBitsAndColoursBetweenCharacters)
{
    PpcTestCharacter source;
    PpcTestCharacter destination;
    PRF_FLAGS(source.character) = PRF_BRIEF | PRF_COMPACT | PRF_NOTELL;
    source.character->profs->colors[COLOR_HIT] = CBRED;
    PRF_FLAGS(destination.character) = PRF_SPAM | PRF_SING;

    ppc_copy_between_characters(source.character, destination.character);

    EXPECT_TRUE(PRF_FLAGGED(destination.character, PRF_BRIEF));
    EXPECT_TRUE(PRF_FLAGGED(destination.character, PRF_COMPACT));
    EXPECT_FALSE(PRF_FLAGGED(destination.character, PRF_SPAM))
        << "A PPC bit clear on the source must be cleared on the destination.";
    EXPECT_TRUE(PRF_FLAGGED(destination.character, PRF_SING))
        << "Non-PPC bits belong to the character and must survive.";
    EXPECT_FALSE(PRF_FLAGGED(destination.character, PRF_NOTELL))
        << "A non-PPC bit must not be carried across from the source.";
    EXPECT_EQ(destination.character->profs->colors[COLOR_HIT], CBRED);
}

TEST(AccountPpcPropagate, IsSafeForNullAndSelf)
{
    PpcTestCharacter subject;
    PRF_FLAGS(subject.character) = PRF_BRIEF;
    ppc_copy_between_characters(nullptr, subject.character);
    ppc_copy_between_characters(subject.character, nullptr);
    ppc_copy_between_characters(subject.character, subject.character);
    EXPECT_TRUE(PRF_FLAGGED(subject.character, PRF_BRIEF));
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `scripts/rots-docker.sh test --gtest_filter='AccountPpcPropagate.*'`
Expected: compile failure — `ppc_copy_between_characters` is not declared.

- [ ] **Step 3: Implement**

Add to `src/account_ppc.h`:

```c++
/* Copy one character's PPC onto another, masked. Used to keep an account's characters in
   step while more than one is online. */
void ppc_copy_between_characters(const struct char_data* source, struct char_data* destination);

/* Push this character's PPC to every other playing character on the same account. Called
   after any command that can change a PPC value. Without it, a second character's save
   would write its stale copy over the change the player just made. Walks descriptor_list --
   connected sockets, not linked characters -- so the cost is bounded by players online. */
void ppc_propagate_from(const struct char_data* ch);
```

Add to `src/account_ppc.cpp`:

```c++
void ppc_copy_between_characters(const struct char_data* source, struct char_data* destination)
{
    if (source == nullptr || destination == nullptr || source == destination)
        return;
    ppc_write_to_character(ppc_read_from_character(source), destination);
}

void ppc_propagate_from(const struct char_data* ch)
{
    if (ch == nullptr || ch->desc == nullptr || *ch->desc->account_name == '\0')
        return;

    const std::string account_name = account::normalize_account_name(ch->desc->account_name);
    if (account_name.empty())
        return;

    for (descriptor_data* descriptor = descriptor_list; descriptor; descriptor = descriptor->next) {
        if (descriptor == ch->desc || descriptor->connected != CON_PLYNG)
            continue;
        char_data* other = descriptor->character;
        if (other == nullptr || other == ch || IS_NPC(other) || other->desc != descriptor)
            continue;
        if (account::normalize_account_name(descriptor->account_name) != account_name)
            continue;
        ppc_copy_between_characters(ch, other);
    }
}
```

Add `extern struct descriptor_data* descriptor_list;` to `account_ppc.cpp` (or include the header that declares it — check with `grep -rn "descriptor_data \*descriptor_list\|descriptor_data\* descriptor_list" src/*.h src/comm.cpp`).

- [ ] **Step 4: Run the tests to verify they pass**

Run: `scripts/rots-docker.sh test --gtest_filter='AccountPpcPropagate.*'`
Expected: 2 tests PASS.

- [ ] **Step 5: Call it from the three command handlers**

Add `#include "account_ppc.h"` to `src/act_othe.cpp` and `src/color.cpp`, then add `ppc_propagate_from(ch);` as the last statement of each of:

1. `do_gen_tog` (`act_othe.cpp:1014`) — at the very end of the function, after the `switch (subcmd)` block. Note the `default:` case does an early `return`; that is fine, since it changes nothing.
2. `do_inventory_sort` (`act_othe.cpp:1360`) — at the very end of the function.
3. `do_color` (`color.cpp:495`) — at the very end of the function. If it has early `return`s on error or usage paths, leave those alone; only the fall-through end needs the call.

It is called unconditionally rather than per-subcommand so there is no membership list to keep in sync with `PPC_PRF_MASK`.

- [ ] **Step 6: Verify the build**

Run: `scripts/rots-docker.sh compile`
Then: `scripts/rots-docker.sh test --gtest_filter='AccountPpc*'`
Expected: build succeeds, all `AccountPpc*` tests pass.

- [ ] **Step 7: Format and commit**

```bash
cd src && clang-format -i -style=WebKit account_ppc.h account_ppc.cpp act_othe.cpp color.cpp tests/account_ppc_tests.cpp && cd ..
git add src/account_ppc.h src/account_ppc.cpp src/act_othe.cpp src/color.cpp src/tests/account_ppc_tests.cpp
git commit -m "feat(ppc): propagate a settings change to the account's other online characters

An account can have two characters online at once, so without this the
second character's save writes its stale copy over the change the player
just made, and the setting silently reverts.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

### Task 8: Skip the creation prompts when the account already has a PPC

`CON_COLOR` and `CON_LATIN` ask every new character two PPC questions. On a player's second character these are redundant, and answering them would overwrite a scheme they already tuned.

**Files:**
- Modify: `src/interpre.cpp` — the shared colour blurb and both entry points (`interpre.cpp:4108-4122` in `CON_QPROF`, and `interpre.cpp:4582-4600` in `CON_CREATE`), the `CON_LATIN` tail (`interpre.cpp:4148-4167`)
- Modify: `src/account_ppc.h`, `src/account_ppc.cpp` (the "does this account have a PPC" query)
- Test: manual, plus the existing `Interpre*` suite for regressions

**Interfaces:**
- Consumes: `ppc_apply_account_to_character` (Task 5).
- Produces:
  - `bool ppc_account_has_preferences(const char* account_name);` — true when the account exists and its stored PPC is present.
  - `int begin_creation_appearance_prompts(struct descriptor_data* d);` — returns the next connection state. Returns `CON_COLOR` after sending the colour blurb when the account has no PPC; otherwise applies the account PPC and completes creation, returning `CON_SLCT`.
  - `void finish_new_character_creation(struct descriptor_data* d);` — the existing `CON_LATIN` tail, extracted.

- [ ] **Step 1: Add the account query with a test**

Append to `src/tests/account_ppc_tests.cpp`:

```cpp
TEST(AccountPpcQuery, ReportsWhetherAnAccountHasPreferences)
{
    TemporaryDirectory root;
    account::AccountData account = make_minimal_account();
    ASSERT_TRUE(account::write_account_file(root.path(), account, nullptr));
    EXPECT_FALSE(ppc_account_has_preferences_in(root.path(), account.account_name.c_str()));

    account.preferences.present = true;
    account.preferences.preference_flags = PRF_BRIEF;
    ASSERT_TRUE(account::write_account_file(root.path(), account, nullptr));
    EXPECT_TRUE(ppc_account_has_preferences_in(root.path(), account.account_name.c_str()));

    EXPECT_FALSE(ppc_account_has_preferences_in(root.path(), "nosuchaccount"));
    EXPECT_FALSE(ppc_account_has_preferences_in(root.path(), ""));
    EXPECT_FALSE(ppc_account_has_preferences_in(root.path(), nullptr));
}
```

Run: `scripts/rots-docker.sh test --gtest_filter='AccountPpcQuery.*'` — expect a compile failure.

Then add to `src/account_ppc.h`:

```c++
/* True when this account already has a stored PPC. Character creation uses it to skip the
   colour and latin-1 prompts, which would otherwise overwrite a scheme the player has
   already tuned. False for an unknown or unreadable account. */
bool ppc_account_has_preferences(const char* account_name);
bool ppc_account_has_preferences_in(const std::string& root_directory, const char* account_name);
```

and to `src/account_ppc.cpp`:

```c++
bool ppc_account_has_preferences_in(const std::string& root_directory, const char* account_name)
{
    if (account_name == nullptr || *account_name == '\0')
        return false;

    account::AccountData account;
    if (!account::read_account_file(root_directory, account_name, &account, nullptr))
        return false;
    return account.preferences.present;
}

bool ppc_account_has_preferences(const char* account_name)
{
    return ppc_account_has_preferences_in(".", account_name);
}
```

Run: `scripts/rots-docker.sh test --gtest_filter='AccountPpcQuery.*'` — expect 1 test PASS.

- [ ] **Step 2: Extract the creation tail**

In `src/interpre.cpp`, add this function above `nanny` (or wherever the `CON_*` state machine lives), moving the body verbatim from the end of the `CON_LATIN` case (`interpre.cpp:4159-4167`):

```c++
/* The tail of character creation, shared by the prompted and the skipped path so the two
   cannot drift apart. */
static void finish_new_character_creation(struct descriptor_data* d)
{
    /* Give them an autowimpy of 10 */
    WIMP_LEVEL(d->character) = 10;
    introduce_char(d);
    show_character_menu(d);
    STATE(d) = CON_SLCT;
    vmudlog(NRM, "%s [%s] new player.", GET_NAME(d->character), d->host);
}
```

Replace those lines in the `CON_LATIN` case with a call to it, followed by `break;`.

- [ ] **Step 3: Extract the shared blurb and the skip decision**

Still in `src/interpre.cpp`, above the state machine:

```c++
static const char* const kDefaultColourSetPrompt =
    "\r\n"
    "On RotS, you are allowed to create your own colour scheme.\r\n"
    "However, many new players find this burdensome, and prefer\r\n"
    "a quick way to enable colours; thus, we have made available\r\n"
    "a default set of colours to satisfy those players who rather\r\n"
    "dive into the game than muck around in the RotS manual pages.\r\n"
    "\r\n"
    "Please note that you may disable ANSI colours at any time by\r\n"
    "typing 'colour off'; furthermore, we encourage you to read\r\n"
    "our manual entry on how to define your own, personalized set\r\n"
    "of colours via the 'help colour' or 'manual general colour'\r\n"
    "commands.\r\n"
    "\r\n"
    "Do you wish to enable the default colour set (Y/N)? ";

/* A player's colour scheme and latin-1 choice live on the account, so ask for them only
   on the account's first character. On any later character, answering would overwrite a
   scheme the player already tuned; inherit it silently instead. */
static int begin_creation_appearance_prompts(struct descriptor_data* d)
{
    if (ppc_account_has_preferences(d->account_name)) {
        ppc_apply_account_to_character(d->account_name, d->character);
        finish_new_character_creation(d);
        return CON_SLCT;
    }

    SEND_TO_Q(kDefaultColourSetPrompt, d);
    return CON_COLOR;
}
```

- [ ] **Step 4: Route both entry points through it**

1. In the `CON_QPROF` case (`interpre.cpp:4108-4122`), replace the inline `SEND_TO_Q(...)` blurb and `STATE(d) = CON_COLOR;` with:

```c++
        STATE(d) = begin_creation_appearance_prompts(d);
        break;
```

2. In the `CON_CREATE` case's `=` branch (`interpre.cpp:4582-4600`), replace the duplicated blurb and `return CON_COLOR;` with:

```c++
                return begin_creation_appearance_prompts(d);
```

Delete the now-obsolete `/* Must sync with message in CON_COLOR above */` comment — there is only one copy of the message now.

Note the two call sites differ: `CON_QPROF` assigns to `STATE(d)`, while `CON_CREATE` is inside a function that *returns* the next state. `begin_creation_appearance_prompts` sets `STATE(d)` itself on the skip path (via `finish_new_character_creation`) and also returns it, so both forms are correct.

- [ ] **Step 5: Verify the build and check for regressions**

Run: `scripts/rots-docker.sh compile`
Then: `scripts/rots-docker.sh test --gtest_filter='Interpre*:AccountPpc*'`
Expected: build succeeds; `AccountPpc*` pass; `Interpre*` matches the pre-task baseline (remember the known pre-existing segfault in `InterpreAccountMenu.UnlockSelect...`).

- [ ] **Step 6: Format and commit**

```bash
cd src && clang-format -i -style=WebKit account_ppc.h account_ppc.cpp interpre.cpp tests/account_ppc_tests.cpp && cd ..
git add src/account_ppc.h src/account_ppc.cpp src/interpre.cpp src/tests/account_ppc_tests.cpp
git commit -m "feat(ppc): skip the colour and latin-1 prompts once the account has a PPC

Asking on a player's fifth alt is redundant, and answering would
overwrite the scheme they already tuned. Both entry points and the
creation tail are now shared functions, which also collapses the colour
blurb that was duplicated at two sites behind a 'must sync' comment.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

### Task 9: Live validation on a booted server

Unit tests cannot observe what a player actually sees. MSDP, prompts and colour are only observable on the wire.

**Files:** none modified. This task produces evidence.

- [ ] **Step 1: Baseline the full test suite**

Run: `scripts/rots-docker.sh test 2>&1 | tail -40`
Record the pass/fail counts and where it aborts. Compare against a run on `upstream/release-frodo` if anything looks new — the suite has a pre-existing segfault partway through.

- [ ] **Step 2: Boot the server**

Run: `scripts/rots-docker.sh boot`
Expected: the server starts on port 1024 and the log shows no new errors at boot.

- [ ] **Step 3: Scenario — settings follow the player across characters**

Using the `Debugbot` test account (see the `rots_debug_test_account` notes) and two of its characters:
1. Log in as character A. Run `set brief on`, `set compact on`, `colour narrate red`.
2. `quit` — the character must actually leave the world, or a later selection reconnects to it and fakes the result.
3. Log in as character B on the same account.
4. Run `set` with no argument.

Expected: B shows Brief ON and Compact ON, and B's narrate colour is red.

- [ ] **Step 4: Scenario — live propagation between two online characters**

1. Open two connections, log in as A and B on the same account (this needs an account with a high-level linked character, or the selection unlock — see `first_restricting_active_account_session_for_account`).
2. On A: `set wrap off`.
3. On B, without relogging: `set`.

Expected: B shows Wrap OFF.

- [ ] **Step 5: Scenario — a new alt skips the prompts**

On an account that already has a PPC, create a new character through the account menu.

Expected: neither the "Do you wish to enable the default colour set" nor the "Do you see an 'a' with a pair of dots above it" prompt appears; creation goes straight to the character menu; and the new character's `set` output already matches the account's scheme.

- [ ] **Step 6: Scenario — a fresh account still gets asked**

Create a brand-new account and its first character.

Expected: both prompts appear as before; the answers stick; and after entering the game, `set` reflects them.

- [ ] **Step 7: Scenario — latin-1 applies early enough**

Log in with latin-1 enabled on the account and watch the login text.

Expected: accented characters render correctly from the first screen, not only after the first command.

- [ ] **Step 8: Inspect the account file**

Run `cat lib/accounts/<bucket>/<account>/account.json` for a configured account.

Expected: a `preferences` object with a named `flags` array and a sparse `colors` object; `"version": 1`; and `password_reset_attempt_count` still the final field.

- [ ] **Step 9: Quit every test character and record results**

Leave no character in the world. Write the observed results of steps 3-8 into the PR description draft or a comment on the branch. If any scenario fails, stop and report rather than patching past it.

---

## Self-Review

**Spec coverage:**

| Spec requirement | Task |
|---|---|
| `AccountPreferences` struct, `AccountData::preferences` | 1 |
| PPC mask as the single definition of membership | 1 |
| Schema version stays 1; additive optional key | 3 |
| Flags stored by name; one colour format reused | 2, 3 |
| `preferences` not the final serialized field | 3 |
| Load applies account → character | 5 |
| Implicit migration by seeding from the first character to play | 5 |
| Save compares before writing (cache invariant) | 6 |
| Live propagation to other online characters | 7 |
| `CON_COLOR` / `CON_LATIN` skip; two extracted helpers; blurb de-duplicated | 8 |
| Failure handling: read failure never blocks login, write failure logged only | 5, 6 |
| All six unit-test cases from the spec | 1, 3, 4, 5, 6, 7 |
| All five live smoke scenarios from the spec | 9 |

**Type consistency:** `account::AccountPreferences` fields (`present`, `preference_flags`, `colors`, `color_settings`) are used identically in Tasks 1, 3, 4, 5, 6 and 8. `colors` is `char[MAX_COLOR_FIELDS]` throughout, matching `char_prof_data::colors` (`char colors[16]`). Function names are stable across tasks: `ppc_read_from_character`, `ppc_write_to_character`, `ppc_equal`, `ppc_copy_between_characters`, `ppc_propagate_from`, `ppc_apply_account_to_character`(`_in`), `ppc_store_character_to_account`(`_in`), `ppc_account_has_preferences`(`_in`), `encode_color_slots_object`, `parse_color_slots_object`, `begin_creation_appearance_prompts`, `finish_new_character_creation`.

**Helper names verified:** all eleven private helpers Task 2 reuses were confirmed present in `character_json.cpp` on this branch, with `color_key_for_index` at line 651 and `write_color_setting` at line 746.

**Remaining risk:** the three enter-game call sites in Task 5 and the command tails in Task 7 are given by line number, and those drift. Each step says to confirm by reading the surrounding code rather than trusting the number.
