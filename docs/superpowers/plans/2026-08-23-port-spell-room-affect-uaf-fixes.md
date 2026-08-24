# Port of Spell/Room-Affect UAF Fixes (TASK-018/019/020/021/026) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replicate, on a new `fix/spell-room-affect-uaf-port` branch based on `release-frodo`, the five fixes proven in the RotS_Live_Modern depot: the fireball self-fumble use-after-free (TASK-018), the earthquake caster-fall use-after-free (TASK-019), the `affect_update()` freed-node walk (TASK-020), the room-affect caster-snapshot credit system (TASK-021), and the kill-contributors death record (TASK-026).

**Architecture:** The fixes are ported *by final shape* (not commit-by-commit) from a sibling depot on this machine whose code descends from this one. Every task names the exact source commits and files; the implementer reads the source with `git -C ~/Projects/GitHub/RotS_Live_Modern show <sha>` / `git -C ~/Projects/GitHub/RotS_Live_Modern diff 2869784b..fix/task-020-021-room-affects -- <file>` and adapts it using the translation table below. The two depots' code is byte-similar at every fix site; the differences are mechanical renames plus file placement.

**Tech Stack:** C++17 (`-std=c++1z` in the flat Makefiles; CMake build is the primary one), gtest (`make test` → ctest target `ageland_tests`), no exceptions (use non-throwing idioms).

**Spec:** The source depot's commits ARE the spec. Branch `fix/task-018-spell-fireball-uaf` (fork point `2869784b`, tip `720c6628`) and branch `fix/task-020-021-room-affects` (tip `df04dcb3`, contains the former). Read a commit's message with `git -C ~/Projects/GitHub/RotS_Live_Modern log -1 <sha>` — the messages carry full root-cause analyses and test rationale.

## Global Constraints

- **Source depot:** `~/Projects/GitHub/RotS_Live_Modern`. Full production diff: `git -C ~/Projects/GitHub/RotS_Live_Modern diff 2869784b..fix/task-020-021-room-affects -- src/ ':!src/tests' ':!*.md'`. All quoted "modern" code below comes from that range.
- **Target branch:** create `fix/spell-room-affect-uaf-port` from `release-frodo` in this depot before Task 1; every commit lands there.
- **Build/test commands (run from repo root):** `make build` (server), `make test` (builds `ageland` + `ageland_tests`, runs ctest). A single test: `./build/ageland_tests --gtest_filter='Fixture.Name'` (binary lives in `build/`; confirm exact path with `ctest --test-dir build -N` on first use).
- **New source files must be wired into THREE builds:** `src/CMakeLists.txt` (both the server source list and the `ageland_tests` list as appropriate), `src/Makefile` (OBJFILES + dependency stanza), and `src/tests/Makefile` (OBJFILES for production files, SRCS for test files). Follow the stanza style already in each file.
- **Binary-layout law:** `struct affected_type` is embedded in `char_file_u` (`src/structs.h:1846` region) and MUST NOT change size. The room-affect caster record therefore lives in a side map keyed by (room number, spell), never in `affected_type`. `char_special_data` (`ch->specials`) is NOT persisted — adding fields there is safe.
- **Translation table (modern → this depot).** Apply everywhere; do not import the modern names:
  | Modern | Here |
  |---|---|
  | `room_of(ch)` | `&world[ch->in_room]` |
  | `location_of(ch)` | `ch->in_room` |
  | `rots::entity::first_occupant(room)` | `room->people` |
  | `ch->ls_next_in_room_` | `ch->next_in_room` |
  | `rots::entity::dispatch_char_from_room(ch)` | `char_from_room(ch)` |
  | `room_by_id_total(rn)` | `&world[rn]` |
  | `std::format(...)` + `strcpy` | `sprintf` (match surrounding idiom; C++17 has no `<format>`) |
  | `mutable_arg("")` | `""` |
  | `rots::combat::gain_exp_regardless` | `gain_exp_regardless` |
  | `rots::persist::dispatch_exploit_capture(E, ...)` | `add_exploit_record(E, ...)` |
  | `rots::combat::pkill_create` | `pkill_create` |
  | `rots::combat::` namespace on new types | no namespace (plain globals, matching this depot) |
  | `#include "rots/core/caster_snapshot.h"` | `#include "caster_snapshot.h"` |
  | `src/entity/entity_lifecycle.cpp` (registry, affect fns) | `src/handler.cpp` |
  | `src/entity/char_utils.cpp` `other_side` | `src/handler.cpp:128` |
  | `src/entity/char_utils_combat.cpp` `saves_poison` | `src/spell_pa.cpp:319` |
  | `src/core/include/rots/core/character.h` additions | `src/structs.h` |
  | `src/combat/…` / `src/app/…` / `src/script/…` / `src/olc/…` | flat `src/…` |
  | `combat_hooks.h` `kill_contributor_list` | new `src/kill_contributors.h` |
  | LS1-ALLOW / rr-ledger / tier comments | omit (that depot's audit machinery; do not port the comments that reference it) |
- **Keep the fix comments:** the modern code's substantive comments (the TASK-0xx rationale blocks) are part of the fix; port them, minus ledger/tier references, and reword "TASK-0xx" references to keep the task number (they're the cross-depot trace).
- **Comment law (user convention):** every class-scoped data member added (caster_snapshot fields, kill_contributor_list fields, poisoned_by pair, test-fixture members) carries a role comment. The modern source already has these — keep them.
- **Portability law (project convention):** completely new functions are standard, cross-compilable C++ — the ported code already complies (`std::vector`, `std::map`, `snprintf`); don't introduce POSIX calls.
- **Test adaptation policy:** modern test files are the *behavior spec*; adapt them to this depot's harness (fixtures like `ensure_test_world`/`ZoneGuard` in `src/tests/mage_tests.cpp`, builders `CharPlayerDataBuilder`, RNG control via `test_random_utils.h`). This depot has NO extract_char test seam and its existing tests never drive a real death. Rule: (a) port every test that doesn't need a death; (b) for death-path pins, build NPCs the way the game does (heap char, `register_npc_char`, into `character_list` and a real room) so `extract_char`/`free_char` can run for real — follow `src/tests/interpre_account_menu_tests.cpp:3779` precedent; (c) if a specific death-path test cannot be made to run in this harness after a genuine attempt, keep the non-death ordering pins, note the gap in the commit message, and move on — do not silently drop a whole test file.
- **Red-first where the bug reproduces:** for Tasks 1, 2, 4 write the regression test against the CURRENT code first and observe the failure (or the ASan report) before applying the fix. If the plain build masks the UAF, a deliberate-sabotage check after the fix (revert the fix locally, see red, restore) is the fallback witness.
- **Commit style:** small commits, one per step-group as written in each task, message subject mirroring the modern depot's (e.g. `mage: end spell_fireball at the caster's own fumble death (TASK-018 port)`), with the standard Co-Authored-By/Claude-Session trailer.

---

### Task 1: TASK-018 — spell_fireball delivers the fumbled self-hit last

**Files:**
- Modify: `src/mage.cpp:1811-1881` (`is_friendly_taget`, `ASPELL(spell_fireball)`)
- Test: `src/tests/mage_tests.cpp` (append)

**Interfaces:**
- Consumes: nothing new.
- Produces: no signature changes; `spell_fireball` behavior change only in the self-fumble case.

**Source:** commits `d1556de5` and `00839403` on `fix/task-018-spell-fireball-uaf`; final shape = `git -C ~/Projects/GitHub/RotS_Live_Modern diff 2869784b..fix/task-020-021-room-affects -- src/combat/mage.cpp` (the `spell_fireball` hunk ONLY — the helper/blaze/mist hunks belong to later tasks). Read both commit messages first.

**Root cause being fixed:** the orc fumble arm (`victim = caster` at `src/mage.cpp:1833`) makes a lethal primary hit run `damage() → die() → raw_kill() → extract_char()`, freeing an NPC caster (or relocating a player), after which the splash loop reads `world[caster->in_room].people` and `is_friendly_taget(caster, …)` on freed memory.

- [ ] **Step 1: Write the failing regression test**

Append to `src/tests/mage_tests.cpp`, adapting the modern tests `FireballStopsAfterASelfFumbleKillsTheCaster` → renamed to the final-shape pin `FireballSplashesTheRoomBeforeASelfFumbleKillsTheCaster` and `FireballWithoutAFumbleStillDamagesTheVictimAndKeepsTheCaster` (source: `git -C ~/Projects/GitHub/RotS_Live_Modern show 00839403 -- src/tests/mage_tests.cpp` plus the earlier `d1556de5` test hunk). Per the test adaptation policy: orc NPC caster built on the heap and registered so the real death pipeline may free it; force the fumble by pinning `number(0, 9)` to 0 via `test_random_utils.h`; a bystander NPC in the room records (via its HP delta) that the splash landed. Assert: (a) the bystander was splashed, (b) after the spell returns nothing dereferences the caster (under the current code this test crashes or trips ASan; at minimum the ordering assertion fails).

- [ ] **Step 2: Run it, verify it fails against current code**

Run: `make test` (or the single filter). Expected: FAIL/crash on the new test; note the exact failure output.

- [ ] **Step 3: Apply the fix**

Port the modern `spell_fireball` body verbatim through the translation table. Shape (this is the complete logic; message strings unchanged from the current file):

```cpp
    bool is_fire_spec = utils::get_specialization(*caster) == game_types::PS_Fire;
    // Read before the primary hit: damage() can kill `victim`, and an NPC victim
    // is free_char()'d by extract_char() before this function resumes.
    const bool victim_is_friendly = is_fire_spec && is_friendly_taget(caster, victim);

    // The primary hit. damage() returns 1 once die() -> raw_kill() ->
    // extract_char() has run on the victim; the caller decides what may still
    // be read afterwards.
    const auto deliver_primary_hit = [&]() -> int {
        int save_bonus = get_save_bonus(*caster, *victim, game_types::PS_Fire, game_types::PS_Cold);
        bool saved = new_saves_spell(caster, victim, save_bonus);
        if (saved) {
            act("$N dodges off to the side, avoiding part of the blast!", FALSE, caster, 0, victim, TO_CHAR);
            act("You dodge to the side, avoiding part of the blast!", FALSE, caster, 0, victim, TO_VICT);
            return apply_spell_damage(caster, victim, fireball_damage * 2 / 3, SPELL_FIREBALL, 0);
        }
        return apply_spell_damage(caster, victim, fireball_damage, SPELL_FIREBALL, 0);
    };

    // TASK-018: when the orc fumble above made the caster its own victim, the
    // self-hit is delivered LAST (below the splash loop). A lethal self-hit
    // ends in extract_char(), which free_char()s an NPC caster and re-places a
    // player in the mortal start room; making it the spell's final act means
    // nothing can run on a dead caster, and the room still takes the splash
    // the fireball was invoked for. The ordinary hit keeps its place.
    const bool self_hit = victim == caster;
    if (!self_hit)
        deliver_primary_hit();
```

then in the splash loop replace `if (is_fire_spec && is_friendly_taget(caster, victim))` with `if (victim_is_friendly)` (NOTE: the current line tests `victim`, not `potential_victim` — that is a pre-existing quirk both depots preserve; the hoist keeps it byte-identical), and end the function with:

```cpp
    if (self_hit)
        deliver_primary_hit(); // may free or relocate `caster`; nothing reads it after this
```

- [ ] **Step 4: Run tests, verify pass**

Run: `make test`. Expected: new tests PASS, all pre-existing tests PASS.

- [ ] **Step 5: Commit**

```bash
git add src/mage.cpp src/tests/mage_tests.cpp
git commit -m "mage: deliver the fumbled fireball self-hit after the splash (TASK-018 port)"
```

---

### Task 2: TASK-019 — spell_earthquake defers the caster's own fall

**Files:**
- Modify: `src/mage.cpp:1693-1715` (the crack/fall loop in `ASPELL(spell_earthquake)`)
- Test: `src/tests/mage_tests.cpp` (append)

**Interfaces:** none new; behavior change only in WHEN the caster's own fall runs.

**Source:** commit `cd3a626c` (read its message; test source in the same commit).

**Root cause:** the fall loop moves the caster into the crevice and applies fall damage inside the occupant loop; a lethal fall frees an NPC caster while the loop keeps reading `caster` for every later occupant.

- [ ] **Step 1: Write the failing test** — adapt `EarthquakeLetsEveryOtherOccupantFallBeforeTheCastersOwnFall`: caster + bystander in a room with a crack exit; RNG pinned so both fall and the caster's fall is lethal; record the bystander's room at the instant the caster dies (modern did this by observing state after; here assert post-spell: bystander is IN the crack room and no crash occurred). Keep the RNG-sequence pin: the landing save for each occupant is drawn at the same point as before.
- [ ] **Step 2: Run, verify it fails** (crash/ASan or ordering assertion red).
- [ ] **Step 3: Apply the fix** — port the modern hunk: introduce the `fall` lambda (translation: `char_from_room(faller)` for `dispatch_char_from_room`), record `caster_falls`/`caster_landing_saved` when `tmpch == caster` and `continue`, then after the loop:

```cpp
        if (caster_falls)
            fall(caster, caster_landing_saved); // may free or relocate `caster`; nothing reads it after this
```

The landing save (`new_saves_spell(caster, tmpch, 0)`) must still be drawn INSIDE the loop at its original point for every faller including the caster, so every occupant's RNG sequence is unchanged; only when the caster's fall *runs* moves. Keep the existing condition's precedence exactly: `((!saved && (tmpch != caster)) || (!number(0, 1)))`.
- [ ] **Step 4: Run tests, verify pass.**
- [ ] **Step 5: Commit** — `mage: let every other occupant fall before the caster's own quake fall (TASK-019 port)`.

---

### Task 3: Character registry — `char_by_abs_number()` and pointer-carrying `set_char_exists()`

**Files:**
- Modify: `src/handler.cpp:2445-2490` (registry), `src/handler.h:166-168` (decls)
- Test: `src/tests/char_utils_tests.cpp` (append) or a new small `src/tests/char_registry_tests.cpp` (if new: wire into `src/CMakeLists.txt` + `src/tests/Makefile` SRCS)

**Interfaces:**
- Produces: `void set_char_exists(int num, struct char_data* ch);` and `struct char_data* char_by_abs_number(int num);` declared in `src/handler.h` next to the existing one-arg forms. Tasks 4, 5, 8 depend on these exact signatures.

**Source:** the registry hunk of `git -C ~/Projects/GitHub/RotS_Live_Modern show 34242526` plus the `characters_by_abs_number` portion of the `entity_lifecycle.cpp` diff (in `b1ddce54`'s range; simplest: read the final shape in the full-range diff for `src/entity/entity_lifecycle.cpp`).

- [ ] **Step 1: Write the test** — new-behavior unit test (no red-first needed; this is new API): registering a char under a slot makes `char_by_abs_number(slot)` return it; `remove_char_exists(slot)` makes it return nullptr; out-of-range (`-1`, `MAX_CHARACTERS`) returns nullptr; re-registering the slot to a different char returns the new one.
- [ ] **Step 2: Run, verify it fails to compile** (functions don't exist yet).
- [ ] **Step 3: Implement** — in `src/handler.cpp` beside `char_control_array`:

```cpp
// Parallel to char_control_array: the char_data* registered under each live
// abs_number slot (TASK-021 port). caster_snapshot::resolve() and
// affect_update()'s snapshot walk need to recover the CURRENT owner of a slot
// without ever dereferencing a stale pointer -- char_by_abs_number() is the
// only sanctioned way to do that lookup.
static char_data* characters_by_abs_number[MAX_CHARACTERS];
```

two-arg `set_char_exists(int num, char_data* ch)` calling the one-arg form then storing the pointer; `remove_char_exists` additionally nulls the slot; `char_by_abs_number` bounds-checks (`num < 0 || num >= MAX_CHARACTERS || !char_exists(num)` → nullptr) then returns the slot. Change `register_npc_char()`'s `set_char_exists(i)` (`src/handler.cpp:2479`) to `set_char_exists(i, mob)`. (`register_pc_char` forwards to `register_npc_char`, so it's covered.)
- [ ] **Step 4: Run tests, verify pass.**
- [ ] **Step 5: Commit** — `handler: char_by_abs_number() + pointer-carrying set_char_exists() (TASK-021 port, registry half)`.

---

### Task 4: TASK-020 — affect_update() snapshots the list and re-validates by identity

**Files:**
- Modify: `src/limits.cpp:1526-1553` (`affect_update`)
- Create: `src/tests/affect_update_tests.cpp`
- Modify: `src/CMakeLists.txt`, `src/tests/Makefile` (wire the new test file)

**Interfaces:**
- Consumes: `char_by_abs_number(int)` from Task 3.
- Produces: no signature change; `affect_update()` observable behavior unchanged for live entries.

**Source:** commits `27445303` (snapshot walk + test file) and `34242526` (identity validation) — port the FINAL shape in one go; the combined diff hunk for `src/combat/limits.cpp` in the full-range diff is authoritative. Read both messages: they document the freed-node mechanism (blaze tick kills occupant → `raw_kill` strips affects → `from_list_to_pool` frees the occupant's own `affected_list` node that the walk's saved `tmplist->next` points at) and the slot-recycling hazard that motivates identity validation.

- [ ] **Step 1: Write the failing regression tests** — port `src/tests/affect_update_tests.cpp` from the modern depot (final shape at branch tip: `git -C ~/Projects/GitHub/RotS_Live_Modern show fix/task-020-021-room-affects:src/tests/affect_update_tests.cpp`), adapting fixtures. Core tests: `SurvivesAnOccupantDyingToTheBlazeTickItIsProcessing` (orders `affected_list` as [blaze room, dying occupant, sentinel], drives the real tick; red under the old walk — plain-build may read freed memory "sanely", so ASan or the sabotage protocol is the witness), `DoesNotUpdateACharacterWhoseAbsNumberSlotWasRecycled`, `DoesNotDereferenceAFreedCharacterThroughARecycledSlot`. The modern `ScopedCharExists` helper uses the two-argument `set_char_exists()` — Task 3 provides it.
- [ ] **Step 2: Run, verify the survival test fails** against the current walk (or document the ASan witness per the red-first policy).
- [ ] **Step 3: Apply the fix** — replace `affect_update()` with the ported final shape: file-local `struct affected_list_entry { int type; int number; char_data* ch; room_data* room; }` (with the modern member comments), `drop_stale_character_entry()` helper, a reused `static std::vector<affected_list_entry> snapshot` filled before any body runs, then the walk: `TARGET_CHAR` entries update only when `char_by_abs_number(entry.number)` returns non-null AND pointer-equal to `entry.ch` AND `live->affected`; the housekeeping arm logs via the resolved pointer or "Unknown char" and calls `drop_stale_character_entry`; `TARGET_ROOM` entries always visit (`world[]` rooms are never freed). Add `#include <vector>`. Keep `sprintf` for the log lines. Delete the dead `tmplist3` locals.
- [ ] **Step 4: Run tests, verify pass; run the sabotage check** (temporarily restore the old walk body, see the survival test fail, restore the fix).
- [ ] **Step 5: Commit** — `limits: snapshot affected_list before affect_update walks it, validate by identity (TASK-020 port)`.

---

### Task 5: caster_snapshot — the cast-time copy of the formula inputs

**Files:**
- Create: `src/caster_snapshot.h`, `src/caster_snapshot.cpp`
- Modify: `src/structs.h` (add `max_race_prof_level()` beside the RACE constants), `src/utils.h` (retarget `GET_MAX_RACE_PROF_LEVEL`, add `race_is_*` helpers, retarget `RACE_GOOD/EVIL/EAST/MAGI`), `src/warrior_spec_handlers.h` + `src/battle_mage_handler.cpp` (static bonus forms), `src/handler.cpp:128` + `src/handler.h` (`other_side` snapshot overload via shared `other_side_impl`), `src/spell_pa.cpp:319` + `src/spells.h` (`saves_poison` snapshot form + both decls)
- Create: `src/tests/caster_snapshot_tests.cpp`
- Modify: `src/CMakeLists.txt`, `src/Makefile`, `src/tests/Makefile` (wire `caster_snapshot.cpp` + the test file)

**Interfaces:**
- Consumes: `char_by_abs_number` (Task 3).
- Produces (used by Tasks 6-12): `struct caster_snapshot` with `static caster_snapshot capture(const char_data&)`, `static caster_snapshot none()`, `bool is_none() const`, `bool same_character_as(const char_data&) const`, `char_data* resolve() const`, and the fields exactly as the modern header names them; `inline int max_race_prof_level(int prof, int race)`; `static int battle_mage_handler::get_bonus_spell_pen(game_types::player_specs, int tactics, int mage_level, int spell_pen)` and the matching `get_bonus_spell_power`; `int other_side(const caster_snapshot&, const char_data*)`; `char saves_poison(struct char_data* victim, const caster_snapshot& caster)`.

**Source:** `git -C ~/Projects/GitHub/RotS_Live_Modern show fix/task-020-021-room-affects:src/core/include/rots/core/caster_snapshot.h` and `...:src/entity/caster_snapshot.cpp`; the full-range diffs of `src/utils.h`, `src/core/include/rots/core/character.h`, `src/warrior_spec_handlers.h`, `src/combat/battle_mage_handler.cpp`, `src/entity/char_utils.cpp` (`other_side_impl`), `src/entity/char_utils_combat.cpp` (`saves_poison`); commits `cc23a423`, `b1ddce54`, `9f24c589`.

Placement decisions for THIS depot: `caster_snapshot.h` includes `"structs.h"` (that's where `game_types::player_specs` lives here) and forward-declares nothing else; `max_race_prof_level` goes in `src/structs.h` right after the RACE_* constants it reads, and `src/utils.h`'s `GET_MAX_RACE_PROF_LEVEL` becomes `#define GET_MAX_RACE_PROF_LEVEL(prof, ch) (max_race_prof_level((prof), GET_RACE(ch)))`; the `race_is_good/evil/east/magi` inline functions go in `src/utils.h` directly above the `RACE_GOOD/...` macros they replace the bodies of; the `other_side` snapshot overload is declared in `src/handler.h` next to the live decl (this depot has no `char_utils.h` split for it); both `saves_poison` decls go in `src/spells.h` (today the only decl is a local prototype in `src/mystic.cpp:59` — leave that one alone or drop it in favor of the header).

- [ ] **Step 1: Write the tests** — port `src/tests/caster_snapshot_tests.cpp` (modern file is 540 lines; port all tests that compile against this harness): capture() copies each formula input; `none()`/`is_none()`; `same_character_as` (true only for the very character, false after slot mismatch); `resolve()` returns the char while registered, nullptr after `remove_char_exists`, nullptr after the slot is re-registered to a different char (recycling); NPC-name capacity (long `short_descr` truncates safely at 63 chars); `max_race_prof_level` table (orc 20, uruk mage 27/other 30, default 30) equals the old macro for each race; static battle-mage bonus forms agree with the member forms; `other_side(snapshot, x) == other_side(live, x)` across the race matrix; `saves_poison(victim, snapshot)` agrees with the live form under pinned RNG.
- [ ] **Step 2: Run, verify compile failure** (types don't exist).
- [ ] **Step 3: Implement** — port the four files' changes through the translation table. `capture()` notes: route macro reads through `const char_data* const ch = &caster;` (the -Wnonnull-compare rationale holds for gcc here too); `GET_PERCEPTION(snap.identity_ptr)` needs the non-const pointer, as in the source. `battle_mage_handler`: statics own the bodies (`spec != game_types::PS_BattleMage` early-out), member forms forward passing `is_battle_spec ? PS_BattleMage : PS_None`. `other_side`: extract `other_side_impl(bool is_npc, bool is_charmed, int race, const char_data* other)` in `src/handler.cpp` as an anonymous-namespace function, both overloads forward (live form does NOT go through `capture()` — it's hot; see the modern comment). `saves_poison`: snapshot form owns the body reading `caster.willpower`/`caster.perception`; live form forwards through `capture()`.
- [ ] **Step 4: Run tests, verify pass; `make build` still links.**
- [ ] **Step 5: Commit** — `entity: caster_snapshot, a cast-time copy of the formula inputs (TASK-021 port)`.

---

### Task 6: Formula helpers gain snapshot forms; live forms forward

**Files:**
- Modify: `src/mage.cpp` (top-of-file helpers + `get_save_bonus:1307` + `is_friendly_taget:1811`), `src/mystic.cpp:68` (`get_mystic_caster_level`), `src/spell_pa.cpp:227,240` (`get_saving_throw_dc`, `new_saves_spell`), `src/spells.h` (the full overload-set declaration block)
- Test: `src/tests/mage_tests.cpp` (append equivalence tests)

**Interfaces:**
- Consumes: `caster_snapshot` (Task 5).
- Produces (Tasks 8-10 call these): snapshot overloads of `get_mage_caster_level`, `get_magic_power`, `should_apply_spell_penetration`, `get_spell_pen_value`, `get_victim_saving_throw`, `get_save_bonus`, `is_friendly_taget`, `get_mystic_caster_level`, `get_saving_throw_dc`, `new_saves_spell` — each snapshot form OWNS the body, each live form is a one-line forwarder through `caster_snapshot::capture()`; plus `static int scale_spell_damage(double saving_throw, int damage_dealt)` file-local in `src/mage.cpp` (the ONE place the multiplier lives — `apply_spell_damage` now calls it; Task 8 adds the credited form that shares it). Declare the whole overload set in `src/spells.h` as the modern `spells.h` diff does (adapted include).

**Source:** the full-range diffs of `src/combat/mage.cpp` (helper hunks only — NOT the blaze/mist hunks), `src/combat/mystic.cpp` (helper hunk only), `src/combat/spell_pa.cpp`, `src/spells.h`; commit `14a69a45`.

- [ ] **Step 1: Write tests** — for each helper: snapshot form on `capture(ch)` equals live form on `ch` under pinned RNG (the per-call rounding rolls stay inside the snapshot bodies — pin RNG and compare); `get_spell_pen_value` charmed-NPC-without-master yields the `master_mage_prof_level == 0` arm; `is_friendly_taget(snapshot, victim)` uses `same_character_as` for the self test.
- [ ] **Step 2: Run, verify compile failure.**
- [ ] **Step 3: Implement** — port each helper hunk verbatim (they are pure formula rewrites onto snapshot fields; the modern bodies list every field read). Keep the modern forwarding comments. `spell_fireball`/`spell_earthquake` from Tasks 1-2 are untouched here (they keep calling the live forms).
- [ ] **Step 4: Run tests, verify pass.**
- [ ] **Step 5: Commit** — `combat: formula helpers read a caster_snapshot; live forms forward (TASK-021 port)`.

---

### Task 7: Poison origin — record on the victim, resolve at the kill

**Files:**
- Modify: `src/structs.h:1101` (`char_special_data` — add the pair), `src/fight.cpp` (add `resolve_poisoner` + `record_poison_origin` above `raw_kill:877`), `src/handler.h` (decls beside `damage()`'s), `src/handler.cpp:708` (`affect_remove` — clear when the last SPELL_POISON goes), `src/db.cpp:3468` (`clear_char` — blank the pair), call sites: `src/mystic.cpp` (`spell_poison` victim arm, near :1360), `src/mage.cpp:2034` region (`spell_black_arrow` poison arm), `src/spec_pro.cpp:2990`+ (`vampire_huntress` poison arm), `src/act_obj2.cpp` (`do_drink` ~:195 and `do_eat` ~:270 poison arms — record with `nullptr` to CLEAR)
- Test: new `src/tests/poison_origin_tests.cpp` (wire into builds) — or append to `fight_proc_tests.cpp` if the fixture fits

**Interfaces:**
- Consumes: `char_by_abs_number` (Task 3).
- Produces (Tasks 8, 10, 12 consume): `struct char_data* resolve_poisoner(const struct char_data& victim);` and `void record_poison_origin(struct char_data* victim, struct char_data* poisoner);` declared in `src/handler.h`; fields `int poisoned_by_abs_number = -1; char_data* poisoned_by = nullptr;` in `char_special_data` with the modern role comments (adapted: this depot's `char_special_data` is likewise not persisted — verify `char_file_u` still only embeds `specials2`, then say so in the comment).

**Source:** full-range diffs of `src/core/include/rots/core/character.h` (the pair), `src/combat/fight.cpp` (the two functions ONLY), `src/entity/entity_lifecycle.cpp` (the `clear_char` and `affect_remove` hunks), `src/combat/mystic.cpp` (record call), `src/combat/mage.cpp` (`spell_black_arrow` hunk), `src/script/spec_pro.cpp`, `src/app/act_obj2.cpp`; commits `4ac23a8a` (partly), `78f5f8ca`, `f7b9fa50` (the record/resolve halves).

Adaptation notes: in `affect_remove` (`src/handler.cpp:708`), capture `const int removed_type = af->type;` FIRST (the modern comment explains: the unlink pools `*af`), and at the tail, after the existing logic and before `affect_total(ch)`:

```cpp
    // TASK-021 port: the poison origin outlives no poison. Once the last
    // SPELL_POISON affect is gone the record is stale -- the next poison,
    // from whoever casts it, records its own origin. A character whose
    // AFF_POISON bit comes from somewhere other than an affect (worn gear)
    // keeps whatever was recorded, because nothing was removed here that
    // owned it.
    if (removed_type == SPELL_POISON && affected_by_spell(ch, SPELL_POISON) == nullptr) {
        ch->specials.poisoned_by_abs_number = -1;
        ch->specials.poisoned_by = nullptr;
    }
```

In `spec_pro.cpp` the recorded poisoner is the SPECIAL's `host` mob. In `mystic.cpp` `spell_poison`'s single-victim arm and `mage.cpp` `spell_black_arrow`, record `caster` right after `affect_join`.

- [ ] **Step 1: Write tests** — record+resolve round-trip; resolve → nullptr after `remove_char_exists` (extraction) and after slot recycling; `record_poison_origin(v, nullptr)` clears; `affect_remove` of the last SPELL_POISON clears the record but a second concurrent SPELL_POISON affect keeps it; `clear_char` blanks it.
- [ ] **Step 2: Run, verify compile failure.**
- [ ] **Step 3: Implement** all files above; keep the modern "only sanctioned reader/writer" comments.
- [ ] **Step 4: Run tests + `make build`, verify pass.**
- [ ] **Step 5: Commit** — `fight+spells: poison remembers its origin; resolve_poisoner() reads it back (TASK-021 port)`.

---

### Task 8: damage_credited() — engagement split from kill credit

**Files:**
- Modify: `src/fight.cpp:1588-1936` (`damage` → `damage_credited` + forwarder), `src/handler.h` (decl), `src/mage.cpp` (add `apply_spell_damage_credited` beside `apply_spell_damage`, using Task 6's `scale_spell_damage`), `src/spells.h` (decl), `src/limits.cpp:728` (point_update gear-poison tick) and `src/limits.cpp:1348` (affect_update_person poison tick) — both become `damage_credited(i, i, resolve_poisoner(*i), 5, SPELL_POISON, 0)`
- Test: new `src/tests/fight_credit_tests.cpp` (wire into builds)

**Interfaces:**
- Consumes: `resolve_poisoner` (Task 7), `caster_snapshot` + `get_victim_saving_throw(snapshot,…)` + `scale_spell_damage` (Tasks 5-6).
- Produces (Tasks 10, 12 consume): `int damage_credited(struct char_data* ch, struct char_data* victim, struct char_data* credited_killer, int dam, int attacktype, int hit_location);` in `src/handler.h`; `int apply_spell_damage_credited(const caster_snapshot& who, char_data* attacker, char_data* victim, char_data* credited_killer, int damage_dealt, int spell_number, int hit_location);` in `src/spells.h`. `damage()` becomes the forwarder `return damage_credited(attacker, victim, attacker ? attacker : victim, dam, attacktype, hit_location);` (mirroring the body's `if (!attacker) attacker = victim;` emergency fix so credit follows the substitution).

**Source:** full-range diff of `src/combat/fight.cpp` (the `damage_credited` rename + death-branch hunk + forwarder) — port the FINAL shape including TASK-026's `engaged_opponent` capture and sourceless-kill fallback; the `apply_spell_damage_credited` hunk of `src/combat/mage.cpp`; commits `f7b9fa50`, `2316aa30` (the mage half), `92322251` (the fallback), `5559827c` (M-1 rationale — read it: room ticks credit but never engage).

Death-branch shape to port (at `src/fight.cpp:1919-1935`, translated):

```cpp
    // TASK-026: the opponent the victim is engaged with at the instant it dies,
    // captured HERE because the stop_fighting() call on the very next line
    // clears specials.fighting for a dead character.
    char_data* const engaged_opponent = victim->specials.fighting;

    if (!AWAKE(victim))
        if (victim->specials.fighting)
            stop_fighting(victim);

    if (GET_POS(victim) == POSITION_DEAD) {
        // TASK-021: the KILL is credited to `credited_killer`, not to the
        // character that engaged the victim. For damage() they are the same
        // pointer, so this block is byte-for-byte the old one.
        char_data* killer = credited_killer;
        // TASK-026: when nobody is credited -- a poison or room tick whose
        // caster can no longer be resolved -- the death is credited to whoever
        // the victim was fighting. A victim fighting nobody still credits
        // nobody: this never invents a killer.
        if (killer == nullptr && engaged_opponent != nullptr) {
            killer = engaged_opponent;
        }
        // Redirect the attacker as the pet's master if the master is in the same room as the pet.
        if (killer && IS_NPC(killer)) {
            if (killer->master && (MOB_FLAGGED(killer, MOB_PET) || MOB_FLAGGED(killer, MOB_ORC_FRIEND)) && killer->master->in_room == killer->in_room) {
                killer = killer->master;
            }
        }

        die(victim, killer, attacktype);
        return 1;
    } else {
        return 0;
    }
```

Everything ABOVE the death branch keeps using `attacker` exactly as today (engagement, messages, specials) — the rename touches only the signature and the death branch. Audit for other reads of `attacker` between the `engaged_opponent` capture and `die()`: there must be none of `credited_killer`.

- [ ] **Step 1: Write tests** — port the portable subset of the modern `fight_credit_tests.cpp` (1990 lines; the file's section comments name the behaviors). Minimum pins: `damage()` forwards with credit == attacker (an ordinary kill credits the attacker, byte-identical behavior incl. pet-master redirect); `damage_credited` with a remote credited_killer never engages it (`credited_killer->specials.fighting` untouched, victim fights only `attacker`); a null credit + engaged opponent falls back to that opponent; a null credit + no fight credits nobody (die receives NULL); poison DoT via `resolve_poisoner` credits the recorded poisoner (`DamageForwardsWithTheAttackerAsCredit` etc.).
- [ ] **Step 2: Run, verify compile failure / red.**
- [ ] **Step 3: Implement** as above plus the two `src/limits.cpp` poison-tick call sites (port the modern comment blocks) and `apply_spell_damage_credited` in `src/mage.cpp`.
- [ ] **Step 4: Run FULL suite** (`make test`) — this touches the main combat path; every pre-existing damage/fight test must stay green.
- [ ] **Step 5: Commit** — `fight: damage_credited() separates engagement from kill credit (TASK-021/026 port)`.

---

### Task 9: Room-affect caster store — (room, spell) → caster_snapshot

**Files:**
- Modify: `src/handler.cpp` (side map + `set_room_affect_caster`/`room_affect_caster`, 3-arg `affect_to_room`, `none()` backfill in the 2-arg form at :672, erase in `affect_remove_room` at :765), `src/handler.h:41-45` region (decls)
- Test: new `src/tests/room_affect_caster_tests.cpp` or fold into Task 10's `room_affect_tick_tests.cpp` (implementer's call; if new, wire into builds)

**Interfaces:**
- Consumes: `caster_snapshot` (Task 5).
- Produces (Tasks 10-11 consume): `void affect_to_room(struct room_data* room, struct affected_type* af, const caster_snapshot& caster);`, `const caster_snapshot* room_affect_caster(const room_data* room, int spell);`, `void set_room_affect_caster(room_data* room, int spell, const caster_snapshot& caster);` — all in `src/handler.h`.

**Source:** the `entity_lifecycle.cpp` hunks for `g_room_affect_casters` / `affect_to_room` / `affect_remove_room` in the full-range diff; commit `4ac23a8a`. Port the `std::map<std::pair<int,int>, caster_snapshot>` keyed by `room->number` and spell, with the modern comment about `affected_type` being frozen by the player-file layout (true here too — see Global Constraints). In `affect_remove_room`, capture `const int spell = af->location;` and `const bool is_room_spell = af->type == ROOMAFF_SPELL;` BEFORE the unlink, erase after `put_to_affected_type_pool(af)`. In the 2-arg `affect_to_room`, after `affect_total_room`, backfill `none()` when no record exists (builder/OLC path — `src/shaperom.cpp` `implement_room` needs no change; add the modern two-line comment there noting it records `none()` by construction). The 3-arg form calls the 2-arg form then overwrites the record when `af->type == ROOMAFF_SPELL`.

- [ ] **Step 1: Write tests** — 3-arg set records; 2-arg backfills `none()`; `affect_remove_room` erases; `room_affect_caster` returns nullptr for an unknown (room, spell); the returned pointer aims into the map (document the Task 10 copy-before-remove hazard with a test that removes and re-reads).
- [ ] **Step 2-4: red → implement → green** (`make test`).
- [ ] **Step 5: Commit** — `handler: room affects record their caster's snapshot (TASK-021 port)`.

---

### Task 10: room_affect_tick — ticks run from the snapshot; affect_update_room integration

**Files:**
- Create: `src/room_affect_tick.h`, `src/room_affect_tick.cpp`
- Modify: `src/limits.cpp:1443-1520` (`affect_update_room`), builds (`src/CMakeLists.txt`, `src/Makefile`, `src/tests/Makefile`)
- Create: `src/tests/room_affect_tick_tests.cpp`

**Interfaces:**
- Consumes: everything from Tasks 5-9 (`room_affect_caster`, `caster_snapshot`, snapshot formula helpers, `damage_credited`, `apply_spell_damage_credited`, `record_poison_origin`, 3-arg `affect_to_room`).
- Produces: `bool room_affect_tick(int spell, room_data* room, char_data* occupant, const affected_type& affect);` in `src/room_affect_tick.h`.

**Source:** `git -C ~/Projects/GitHub/RotS_Live_Modern show fix/task-020-021-room-affects:src/room_affect_tick.h` and `...:src/combat/room_affect_tick.cpp` (port near-verbatim through the translation table — the file banner comments are the design record; keep them, minus tier/ledger lines) and the `affect_update_room` hunk of the `limits.cpp` full-range diff; commits `2316aa30`, `72a16020`, `5559827c` (M-1: ticks credit but never engage — the engaging attacker is ALWAYS the occupant itself).

`saves_mystic` in this depot: find its definition (`grep -rn "char saves_mystic" src/`) and use a local extern declaration in `room_affect_tick.cpp` exactly as the modern file does if no header declares it.

`affect_update_room` integration (three changes, port the comment blocks):
1. The occupant tick becomes `if (!room_affect_tick(tmpaf->location, room, tmpch, *tmpaf))` falling back to the historical self-re-cast `(skills[tmpaf->location].spell_pointer)(tmpch, "", SPELL_TYPE_SPELL, tmpch, 0, 0, 0);`.
2. After the occupant loop: the defensive `if (!room->affected) continue;` guard.
3. The mist-move arm: copy the caster record into a local `const caster_snapshot moved_caster` BEFORE `affect_remove_room` (the returned pointer aims into the erased map entry), pass it to the 3-arg `affect_to_room(&world[roomnum], &newaf, moved_caster)`, and set `tmpaf = nullptr;` after `affect_remove_room(room, tmpaf)` — the modern comment documents that the existing `if (tmpaf)` test below read freed storage on every mist drift (this depot has the same bug at `src/limits.cpp:1506-1513`).

- [ ] **Step 1: Write tests** — port `room_affect_tick_tests.cpp` (modern file 1734 lines; the behavior pins per tick body): blaze tick damages from the SNAPSHOT's level (mutate the live caster after cast; tick damage unchanged), credits the recorded caster on a lethal tick but never engages it, falls back to occupant-self capture when no record exists (builder affect); poison tick records the resolved caster as poisoner (and nobody when the record is `none()`), saved-arm messages: victim line always delivered, caster line only when the caster is alive AND in the room; haze tick applies from snapshot level (+6 Illusion); mist tick renews durations and seeds neighbors with the caster carried; the mist MOVE keeps its caster and the old node is not re-read (the `tmpaf = nullptr` pin); unknown spell returns false (fallback re-cast still happens).
- [ ] **Step 2: red** (module absent).
- [ ] **Step 3: Implement** both new files + the three `limits.cpp` changes + build wiring.
- [ ] **Step 4: `make test` green.**
- [ ] **Step 5: Commit** — `combat: room affects tick from their caster's snapshot and credit kills (TASK-021 port)`.

---

### Task 11: The casting spells record their caster

**Files:**
- Modify: `src/mage.cpp` (`spell_blaze` — find with `grep -n "ASPELL(spell_blaze)" src/mage.cpp`; `spell_mist_of_baazunga` likewise), `src/mystic.cpp` (`spell_haze` ~:1157 arm, `spell_poison` room arm ~:1308)
- Test: extend `src/tests/room_affect_tick_tests.cpp` or `mage_tests.cpp`

**Interfaces:** consumes Tasks 5 and 9 only; no new surface.

**Source:** the `spell_blaze`/`spell_mist_of_baazunga` hunks of the `mage.cpp` full-range diff and the `spell_haze`/`spell_poison` hunks of the `mystic.cpp` diff; commits `78f5f8ca`, `a1449d14`.

Port per spell, keeping the modern renewal-rule comments:
- **blaze / haze / room-poison:** `const caster_snapshot who = caster_snapshot::capture(*caster);` + `room_data* const here = &world[caster->in_room];` at the top of the arm; fresh affect goes through the 3-arg `affect_to_room(here, &af, who)`; a renewal calls `set_room_affect_caster(here, SPELL_X, who)` ONLY inside the `oldaf->modifier < af.modifier` branch (a renewal takes the room over only when it RAISED the affect).
- **mist:** record follows the DURATION (its modifier carries the SHADOWY bit, not a level): `set_room_affect_caster` inside the `oldaf->duration < af.duration` branch, in the main room AND each adjacent-room renewal (`a1449d14` covered the adjacent-room case); fresh seeds use the 3-arg form. Hoist the repeated `&world[roomnum]` into `room_data* const next` as the source does.

- [ ] **Step 1: Write tests** — casting blaze records the caster; a WEAKER re-cast leaves the record; a stronger re-cast replaces it; mist: longer-duration renewal replaces the record in main and adjacent rooms.
- [ ] **Step 2-4: red → implement → green.**
- [ ] **Step 5: Commit** — `spells: blaze/mist/haze/poison record their caster (TASK-021 port)`.

---

### Task 12: TASK-026 — kill contributors

**Files:**
- Create: `src/kill_contributors.h`
- Modify: `src/fight.cpp` (list methods + `kill_contributors()` + `offer_kill_contributor()` above `raw_kill`; `die():1055-1071` rework; `raw_kill():922` `died_to_player`), `src/pkill.h:23`, `src/pkill.cpp:111,160,506,543` (signatures take the list), builds (new header needs no wiring; no new .cpp)
- Test: extend `src/tests/fight_credit_tests.cpp` + new pkill assertions

**Interfaces:**
- Consumes: `resolve_poisoner` (Task 7), `damage_credited`'s fallback (Task 8), `combat_list`/`next_fighting` (existing, `src/fight.cpp:41`).
- Produces: in `src/kill_contributors.h` (plain global, no namespace — this depot's style):

```cpp
#pragma once
// TASK-026 port: the set of characters that took part in one death, built by
// kill_contributors() (fight.cpp) and carried into pkill_create(). It exists
// because pkill.cpp's three record-building walks derived their own answer
// straight from combat_list, and such a walk cannot see two contributors this
// branch credits: the recorded poisoner (who may be rooms away) and the caster
// a room affect names when its tick lands the killing blow. Fixed capacity,
// no heap: built on the death path, consumed immediately, never outlives it.

struct char_data;

struct kill_contributor_list {
    // Ceiling on the distinct characters one death can credit. Comfortably
    // above any real fight -- add() refuses past it rather than overrunning.
    static constexpr int kCapacity = 32;

    // The contributors, in the order kill_contributors() found them: everyone
    // fighting the victim (combat_list order), then the recorded poisoner,
    // then the primary killer. Only the first `count` entries are live, and
    // no entry is ever null or repeated.
    char_data* entries[kCapacity] {};

    // How many of `entries` are live.
    int count = 0;

    // Latches the first refusal for want of room, so one very crowded death
    // logs a single overflow line instead of one per dropped contributor.
    bool overflow_logged = false;

    // True when `candidate` is already an entry. Null is never an entry.
    bool contains(const char_data* candidate) const;

    // Appends `candidate`, and answers whether it actually landed: false for
    // null, for a duplicate, and for a full list (which also logs, once).
    bool add(char_data* candidate);
};

// Builds that set for `victim`'s death. `primary` is the character die() was
// told did it -- it may be null, and may be standing anywhere. Membership
// rules, applied to every candidate: an NPC pet or orc-friend whose master
// stands in the same room contributes as its MASTER; the victim itself and
// immortals are never contributors.
kill_contributor_list kill_contributors(struct char_data* victim, struct char_data* primary);
```

**Source:** full-range diffs of `src/combat/fight.cpp` (the `offer_kill_contributor`/list/`kill_contributors` block, the `die()` hunk, the `raw_kill()` hunk), `src/combat_hooks.h` (the struct — quoted above already adapted), `src/app/pkill.cpp`, `src/pkill.h`; commits `92322251`, `8d527b37`, `fbd58c4b`, `1827900e`; read `e609841a`/`041e25a1`'s messages for the accepted consequences the review documented.

Key hunks to port:
- `offer_kill_contributor` (anonymous namespace, fight.cpp): pet redirect FIRST (`candidate->master->in_room == candidate->in_room` here), then exclude `candidate == victim` and `GET_LEVEL(candidate) >= LEVEL_IMMORT`, then `contributors.add(candidate)`.
- `kill_contributors`: walk `combat_list` for `fighter->specials.fighting == victim`, then offer `resolve_poisoner(*victim)`, then offer `primary`.
- `die()` (`src/fight.cpp:1055-1071`): delete the SPELL_POISON early-return block, keep `add_exploit_record(EXPLOIT_POISON, …)` unconditionally for poison deaths, then:

```cpp
        const kill_contributor_list contributors = kill_contributors(dead_man, killer);
        if (contributors.count > 0) {
            pkill_create(dead_man, contributors);
            add_exploit_record(EXPLOIT_PK, dead_man, 0, NULL); /* pk records to killers */
        }
```

(port the modern comment block explaining why "in combat" was never the right question). The `if (IS_NPC(killer))` mob-death-XP arm below is unchanged (`killer` is non-null in that branch).
- `raw_kill()` (`src/fight.cpp:922`): `bool died_to_player = killer != NULL && !IS_NPC(killer);` — drop the `attack_type == SPELL_POISON ||` term; port the comment (the origin IS tracked now).
- `pkill.cpp`: `pkill_weight`, `pkill_opponents`, `pkill_update_pkill_tab` iterate `contributors.entries[0..count)` instead of walking `combat_list` (breadth deliberately unchanged — NPC levels still count in weight); `pkill_create(struct char_data* victim, const kill_contributor_list& contributors)` threads it through. `pkill.h` forward-declares `struct kill_contributor_list;` and widens the decl. Overflow log: `vmudlog(BRF, "More than %d contributors to one kill: the surplus is dropped.", kCapacity);`.

- [ ] **Step 1: Write tests** — list unit pins: null/duplicate/overflow (33rd add refused, logged once), pet redirected to same-room master, immortal and victim never land; `kill_contributors` unions fighters + poisoner + primary without duplicates; die() path: a poison death with a live player poisoner creates a pkill record and takes the died-to-player restore arm; a poison death with no resolvable poisoner and no fight records nothing; a fighting victim's sourceless death credits the engaged opponent (Task 8's fallback feeding this).
- [ ] **Step 2-4: red → implement → green (full `make test`).**
- [ ] **Step 5: Commit** — `combat: die() records a kill when anybody took part (TASK-026 port)`.

---

### Task 13: Gates — full build, suite, smoke, review

**Files:** none (verification only).

- [ ] **Step 1:** `make build` from clean (`make clean` first) — zero new warnings in the changed files.
- [ ] **Step 2:** `make test` — full suite green.
- [ ] **Step 3:** If ASan is available on this platform (`CMAKE_CONFIGURE_ARGS` supports a sanitizer flag or a manual `g++ -fsanitize=address` test build), run the mage/affect_update/room_affect_tick test subset under it; otherwise note it as CI-only.
- [ ] **Step 4:** Boot smoke: `make setup && make run` (or the boot path CI uses) long enough to see a clean boot, then stop.
- [ ] **Step 5:** Cross-check against the source: `git -C ~/Projects/GitHub/RotS_Live_Modern diff 2869784b..fix/task-020-021-room-affects --stat -- src/ ':!src/tests' ':!*.md'` — for every production file in that list, confirm this branch has the corresponding change or a written reason it doesn't apply (e.g. `combat_hooks.*` — hook layer doesn't exist here; `CMakeLists`/`Makefile` — wiring differs).
- [ ] **Step 6:** Request code review per superpowers:requesting-code-review; fix findings; done.
