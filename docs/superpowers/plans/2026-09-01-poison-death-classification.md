# Poison-Death Classification Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A PC's poison-tick death is punished by whether they were engaged with a real mob at the instant of death (engaged → full mob death; unengaged → legacy gentle treatment), source-agnostic, without touching attribution records.

**Architecture:** Six pure free functions plus one `enum class death_punishment` carry every decision (declared in `src/handler.h`, defined in `src/fight.cpp`); `die()` and `raw_kill()` gain 4-arg forms consuming them, with 3-arg forwarders preserving all other callers unchanged; `damage_credited()` hands its already-captured `engaged_opponent` into `die()`.

**Tech Stack:** C++17 (`-std=c++1z`), GoogleTest, CMake + raw Makefile dual build, Docker i386 test container (`scripts/rots-docker.sh`).

**Spec:** `docs/superpowers/specs/2026-09-01-poison-death-classification-design.md` (committed, `f4296c7`)

## Global Constraints

- Format every touched C++ file before committing: `clang-format -i -style=WebKit src/<file>` (repo precedent; the repo `.clang-format` is WebKit-based).
- Match `fight.cpp`'s existing style: brace-less single-statement early returns are the file's convention; other control-flow bodies get braces on the same line (`if (x) {`). No ternaries; hoist intermediates into named locals.
- Inline comments: one line, only for a non-obvious why. Function doc comments: contract only, 2–6 lines.
- `IS_NPC(ch)` null-checks its argument (`src/utils.h:209`) — `is_real_mob(nullptr)` is safe by construction.
- Build/test in the Docker container: `scripts/rots-docker.sh test --gtest_filter='<Filter>'` (always quote filters). The container build is QEMU-emulated and slow; use focused filters per task, full suite only in Task 7.
- Never modify files outside each task's **Files** list. Never touch `Note(dgurley):` comments (none are in scope).
- Non-poison deaths and NPC-victim deaths must remain byte-for-byte identical in behavior.

## Class responsibilities

This plan changes no class shapes: no new classes, and no members added to or removed from any
existing class. The one new type is `enum class death_punishment`, a value type with no
behavior — it names the punishment class of a PC death. The materially modified units are free
functions:

- `die()` — applies a PC death's records and XP consequences and hands off to raw_kill.
- `raw_kill()` — performs the mechanical death: corpse, penalties, state cleanup.
- `damage_credited()` — applies damage from an engaging attacker while crediting a possibly
  different killer.
- New free functions (each one sentence, listed in their tasks): `is_real_mob`,
  `find_engaged_real_mob`, `classify_pc_death`, `death_takes_full_mob_xp_loss`,
  `death_counts_as_player_kill`, `mobdeath_record_mob`.

---

### Task 1: `death_punishment` enum, `is_real_mob()`, `classify_pc_death()`, and the test TU

**Files:**
- Modify: `src/handler.h` (the "prototypes from fight.c" block, after the `record_poison_origin` declaration ~line 199)
- Modify: `src/fight.cpp` (definitions after `kill_contributors()` ends ~line 1007, before `raw_kill()`)
- Create: `src/tests/death_classification_tests.cpp`
- Modify: `src/CMakeLists.txt` (add `tests/death_classification_tests.cpp` to `ROTS_TEST_SOURCES`, alphabetically near `tests/damage_tests.cpp` ~line 128)
- Modify: `src/tests/Makefile` (add `death_classification_tests.cpp` to the `SRCS` line 191, after `db_loader_tests.cpp`)

**Interfaces:**
- Consumes: `IS_NPC`, `MOB_FLAGGED`, `MOB_PET`, `MOB_ORC_FRIEND` (utils.h), `SPELL_POISON` (spells.h).
- Produces (later tasks rely on these exact signatures):
  - `enum class death_punishment { legacy, mob_death, player_death };`
  - `bool is_real_mob(const struct char_data* character);`
  - `death_punishment classify_pc_death(int attack_type, bool engaged_with_real_mob);`

- [ ] **Step 1: Create the test TU with failing tests**

Create `src/tests/death_classification_tests.cpp`:

```cpp
// Pins for the poison-death classification carveout: the pure decision
// functions that decide how a PC poison death is punished (spec:
// docs/superpowers/specs/2026-09-01-poison-death-classification-design.md).
// The harness cannot drive a PC-victim death end-to-end (see the banner in
// fight_credit_tests.cpp), so these pins target the extracted functions
// directly with stack fixtures -- no rooms, no death pipeline.
#include "../handler.h"
#include "../spells.h"
#include "../structs.h"
#include "../utils.h"
#include <gtest/gtest.h>

extern char_data* combat_list;

namespace {

// Saves/restores the global combat_list around an engagement pin, so one
// test's fighters never leak into another suite's walk. Per-suite copy,
// following fight_credit_tests.cpp's stated precedent.
struct CombatListGuard {
    char_data* previous; // combat_list found before the test; restored on scope exit
    CombatListGuard()
        : previous(combat_list)
    {
        combat_list = nullptr;
    }
    ~CombatListGuard() { combat_list = previous; }
};

void make_plain_mob(char_data& mob)
{
    mob.specials2.act = MOB_ISNPC;
}

void make_pet_mob(char_data& mob)
{
    mob.specials2.act = MOB_ISNPC | MOB_PET;
}

void make_orc_friend_mob(char_data& mob)
{
    mob.specials2.act = MOB_ISNPC | MOB_ORC_FRIEND;
}

} // namespace

TEST(IsRealMob, NullPlayersPetsAndOrcFriendsAreNotRealMobs)
{
    EXPECT_FALSE(is_real_mob(nullptr));

    char_data player {};
    player.player.level = 20;
    EXPECT_FALSE(is_real_mob(&player)) << "a PC is never a real mob";

    char_data pet {};
    make_pet_mob(pet);
    EXPECT_FALSE(is_real_mob(&pet)) << "a MOB_PET is player-combat context";

    char_data orc_friend {};
    make_orc_friend_mob(orc_friend);
    EXPECT_FALSE(is_real_mob(&orc_friend)) << "a MOB_ORC_FRIEND is player-combat context";
}

TEST(IsRealMob, PlainNpcIsARealMob)
{
    char_data mob {};
    make_plain_mob(mob);
    EXPECT_TRUE(is_real_mob(&mob));
}

TEST(ClassifyPcDeath, NonPoisonIsLegacyRegardlessOfEngagement)
{
    EXPECT_EQ(classify_pc_death(TYPE_HIT, true), death_punishment::legacy);
    EXPECT_EQ(classify_pc_death(TYPE_HIT, false), death_punishment::legacy);
    EXPECT_EQ(classify_pc_death(TYPE_UNDEFINED, true), death_punishment::legacy);
    EXPECT_EQ(classify_pc_death(TYPE_UNDEFINED, false), death_punishment::legacy);
    EXPECT_EQ(classify_pc_death(SPELL_BLAZE, true), death_punishment::legacy)
        << "room-affect damage ticks are outside the carveout";
}

TEST(ClassifyPcDeath, PoisonEngagedIsMobDeathUnengagedIsPlayerDeath)
{
    EXPECT_EQ(classify_pc_death(SPELL_POISON, true), death_punishment::mob_death);
    EXPECT_EQ(classify_pc_death(SPELL_POISON, false), death_punishment::player_death);
}
```

(The `CombatListGuard` and the three `make_*` helpers are consumed by Task 2's tests in this
same TU; defining them now keeps this TU's namespace block stable across tasks.)

- [ ] **Step 2: Register the TU in both build systems**

In `src/CMakeLists.txt`, inside `set(ROTS_TEST_SOURCES` (starts line 109), add in file order
next to the other `d`-named tests (after `tests/db_loader_tests.cpp`):

```cmake
    tests/death_classification_tests.cpp
```

In `src/tests/Makefile` line 191, add `death_classification_tests.cpp ` into the `SRCS` list
directly after `db_loader_tests.cpp `.

- [ ] **Step 3: Run to verify the tests fail**

Run: `scripts/rots-docker.sh test --gtest_filter='IsRealMob.*:ClassifyPcDeath.*'`
Expected: BUILD FAILURE — `is_real_mob`, `classify_pc_death`, and `death_punishment` are
undeclared. A compile failure is this step's "red".

- [ ] **Step 4: Declare in handler.h**

In `src/handler.h`, in the "prototypes from fight.c" block, directly after the
`record_poison_origin` declaration, add:

```cpp
// Punishment class for a PC death. legacy = not a poison carveout: die()/
// raw_kill() decide from the credited killer exactly as they always have.
// The two poison values override the killer-based rules (spec:
// docs/superpowers/specs/2026-09-01-poison-death-classification-design.md).
enum class death_punishment {
    legacy,
    mob_death,
    player_death,
};

// A mob acting for itself: an NPC that is neither MOB_PET nor MOB_ORC_FRIEND.
// Null and players answer false.
bool is_real_mob(const struct char_data* character);

// Only SPELL_POISON classifies away from legacy; engagement with a real mob
// then decides mob_death vs player_death regardless of the poison's source.
death_punishment classify_pc_death(int attack_type, bool engaged_with_real_mob);
```

- [ ] **Step 5: Define in fight.cpp**

In `src/fight.cpp`, after the closing brace of `kill_contributors()` (~line 1007), add:

```cpp
bool is_real_mob(const char_data* character)
{
    if (!IS_NPC(character)) {
        return false;
    }
    return !MOB_FLAGGED(character, MOB_PET) && !MOB_FLAGGED(character, MOB_ORC_FRIEND);
}

death_punishment classify_pc_death(int attack_type, bool engaged_with_real_mob)
{
    if (attack_type != SPELL_POISON) {
        return death_punishment::legacy;
    }
    if (engaged_with_real_mob) {
        return death_punishment::mob_death;
    }
    return death_punishment::player_death;
}
```

- [ ] **Step 6: Run to verify the tests pass**

Run: `scripts/rots-docker.sh test --gtest_filter='IsRealMob.*:ClassifyPcDeath.*'`
Expected: 4 tests PASS.

- [ ] **Step 7: Format and commit**

```bash
clang-format -i -style=WebKit src/handler.h src/fight.cpp src/tests/death_classification_tests.cpp
git add src/handler.h src/fight.cpp src/tests/death_classification_tests.cpp src/CMakeLists.txt src/tests/Makefile
git commit -m "fight: death_punishment classification primitives (poison carveout)"
```

---

### Task 2: `find_engaged_real_mob()`

**Files:**
- Modify: `src/handler.h` (after the Task 1 declarations)
- Modify: `src/fight.cpp` (after the Task 1 definitions)
- Modify: `src/tests/death_classification_tests.cpp`

**Interfaces:**
- Consumes: `is_real_mob()` (Task 1); global `combat_list` / `next_fighting` chain.
- Produces: `struct char_data* find_engaged_real_mob(struct char_data* victim, struct char_data* engaged_opponent);`

- [ ] **Step 1: Add failing tests**

Append to `src/tests/death_classification_tests.cpp`:

```cpp
TEST(FindEngagedRealMob, EngagedOpponentRealMobIsFound)
{
    CombatListGuard combat_guard;
    char_data victim {};
    char_data mob {};
    make_plain_mob(mob);

    EXPECT_EQ(find_engaged_real_mob(&victim, &mob), &mob)
        << "the victim's own target engages even with an empty combat_list";
}

TEST(FindEngagedRealMob, EngagedOpponentPetOrPlayerDoesNotEngage)
{
    CombatListGuard combat_guard;
    char_data victim {};

    char_data pet {};
    make_pet_mob(pet);
    EXPECT_EQ(find_engaged_real_mob(&victim, &pet), nullptr);

    char_data player {};
    player.player.level = 20;
    EXPECT_EQ(find_engaged_real_mob(&victim, &player), nullptr);

    EXPECT_EQ(find_engaged_real_mob(&victim, nullptr), nullptr);
}

TEST(FindEngagedRealMob, CombatListMobFightingTheVictimEngagesWithoutBeingTargeted)
{
    CombatListGuard combat_guard;
    char_data victim {};
    char_data mob {};
    make_plain_mob(mob);
    mob.specials.fighting = &victim;
    combat_list = &mob;
    mob.next_fighting = nullptr;

    EXPECT_EQ(find_engaged_real_mob(&victim, nullptr), &mob)
        << "a mob beating on a victim who targets nobody still engages (the fled/stunned case)";
}

TEST(FindEngagedRealMob, CombatListWalkSkipsPetsOrcFriendsPlayersAndMobsFightingOthers)
{
    CombatListGuard combat_guard;
    char_data victim {};
    char_data bystander {};

    char_data pet {};
    make_pet_mob(pet);
    pet.specials.fighting = &victim;

    char_data orc_friend {};
    make_orc_friend_mob(orc_friend);
    orc_friend.specials.fighting = &victim;

    char_data player {};
    player.player.level = 20;
    player.specials.fighting = &victim;

    char_data distracted_mob {};
    make_plain_mob(distracted_mob);
    distracted_mob.specials.fighting = &bystander;

    combat_list = &pet;
    pet.next_fighting = &orc_friend;
    orc_friend.next_fighting = &player;
    player.next_fighting = &distracted_mob;
    distracted_mob.next_fighting = nullptr;

    EXPECT_EQ(find_engaged_real_mob(&victim, nullptr), nullptr);
}

TEST(FindEngagedRealMob, EngagedOpponentIsPreferredOverCombatListMob)
{
    CombatListGuard combat_guard;
    char_data victim {};
    char_data targeted_mob {};
    make_plain_mob(targeted_mob);
    char_data list_mob {};
    make_plain_mob(list_mob);
    list_mob.specials.fighting = &victim;
    combat_list = &list_mob;
    list_mob.next_fighting = nullptr;

    EXPECT_EQ(find_engaged_real_mob(&victim, &targeted_mob), &targeted_mob)
        << "the victim's own target names the EXPLOIT_MOBDEATH mob deterministically";
}
```

- [ ] **Step 2: Run to verify failure**

Run: `scripts/rots-docker.sh test --gtest_filter='FindEngagedRealMob.*'`
Expected: BUILD FAILURE — `find_engaged_real_mob` undeclared.

- [ ] **Step 3: Declare and define**

`src/handler.h`, after the `classify_pc_death` declaration:

```cpp
// The real mob `victim` counts as engaged with at the instant of death, or
// null. `engaged_opponent` -- the victim's own target, captured before
// stop_fighting() -- is checked first; then combat_list is walked for a real
// mob fighting the victim. No visibility check: an unseen attacker engages.
struct char_data* find_engaged_real_mob(struct char_data* victim, struct char_data* engaged_opponent);
```

`src/fight.cpp`, after `classify_pc_death`:

```cpp
char_data* find_engaged_real_mob(char_data* victim, char_data* engaged_opponent)
{
    if (is_real_mob(engaged_opponent)) {
        return engaged_opponent;
    }
    for (char_data* fighter = combat_list; fighter != nullptr; fighter = fighter->next_fighting) {
        if (fighter->specials.fighting == victim && is_real_mob(fighter)) {
            return fighter;
        }
    }
    return nullptr;
}
```

- [ ] **Step 4: Run to verify pass**

Run: `scripts/rots-docker.sh test --gtest_filter='FindEngagedRealMob.*'`
Expected: 5 tests PASS.

- [ ] **Step 5: Format and commit**

```bash
clang-format -i -style=WebKit src/handler.h src/fight.cpp src/tests/death_classification_tests.cpp
git add src/handler.h src/fight.cpp src/tests/death_classification_tests.cpp
git commit -m "fight: find_engaged_real_mob() -- either-direction engagement probe"
```

---

### Task 3: punishment selectors (`death_takes_full_mob_xp_loss`, `death_counts_as_player_kill`)

**Files:**
- Modify: `src/handler.h`, `src/fight.cpp`, `src/tests/death_classification_tests.cpp`

**Interfaces:**
- Consumes: `death_punishment`, `is_real_mob()` (Task 1).
- Produces:
  - `bool death_takes_full_mob_xp_loss(const struct char_data* killer, death_punishment punishment);`
  - `bool death_counts_as_player_kill(const struct char_data* killer, death_punishment punishment);`

- [ ] **Step 1: Add failing tests**

Append to the test TU:

```cpp
TEST(DeathTakesFullMobXpLoss, LegacyDerivesFromKiller)
{
    char_data mob {};
    make_plain_mob(mob);
    char_data pet {};
    make_pet_mob(pet);
    char_data orc_friend {};
    make_orc_friend_mob(orc_friend);
    char_data player {};
    player.player.level = 20;

    EXPECT_TRUE(death_takes_full_mob_xp_loss(&mob, death_punishment::legacy));
    EXPECT_FALSE(death_takes_full_mob_xp_loss(&pet, death_punishment::legacy));
    EXPECT_FALSE(death_takes_full_mob_xp_loss(&orc_friend, death_punishment::legacy));
    EXPECT_FALSE(death_takes_full_mob_xp_loss(&player, death_punishment::legacy));
    EXPECT_FALSE(death_takes_full_mob_xp_loss(nullptr, death_punishment::legacy))
        << "byte-compatible with die()'s removed IS_NPC(killer) branch, killer == null included";
}

TEST(DeathTakesFullMobXpLoss, MobDeathAlwaysLosesPlayerDeathNeverLoses)
{
    char_data mob {};
    make_plain_mob(mob);
    char_data player {};
    player.player.level = 20;

    EXPECT_TRUE(death_takes_full_mob_xp_loss(nullptr, death_punishment::mob_death));
    EXPECT_TRUE(death_takes_full_mob_xp_loss(&player, death_punishment::mob_death));
    EXPECT_TRUE(death_takes_full_mob_xp_loss(&mob, death_punishment::mob_death));

    EXPECT_FALSE(death_takes_full_mob_xp_loss(nullptr, death_punishment::player_death));
    EXPECT_FALSE(death_takes_full_mob_xp_loss(&player, death_punishment::player_death));
    EXPECT_FALSE(death_takes_full_mob_xp_loss(&mob, death_punishment::player_death))
        << "an unengaged mob poisoner takes the legacy small loss only";
}

TEST(DeathCountsAsPlayerKill, LegacyDerivesFromKiller)
{
    char_data mob {};
    make_plain_mob(mob);
    char_data player {};
    player.player.level = 20;

    EXPECT_FALSE(death_counts_as_player_kill(nullptr, death_punishment::legacy))
        << "raw_kill()'s removed expression: null killer is harsh, and the old"
           " attack_type == SPELL_POISON short-circuit stays gone from legacy";
    EXPECT_FALSE(death_counts_as_player_kill(&mob, death_punishment::legacy));
    EXPECT_TRUE(death_counts_as_player_kill(&player, death_punishment::legacy));
}

TEST(DeathCountsAsPlayerKill, MobDeathIsHarshAndPlayerDeathIsGentleRegardlessOfKiller)
{
    char_data mob {};
    make_plain_mob(mob);
    char_data player {};
    player.player.level = 20;

    EXPECT_FALSE(death_counts_as_player_kill(&player, death_punishment::mob_death))
        << "player poison while engaged with a mob is still punished as a mob death";
    EXPECT_FALSE(death_counts_as_player_kill(nullptr, death_punishment::mob_death));

    EXPECT_TRUE(death_counts_as_player_kill(&mob, death_punishment::player_death))
        << "an unengaged mob poisoner still yields the gentle player-kill penalty";
    EXPECT_TRUE(death_counts_as_player_kill(nullptr, death_punishment::player_death));
}
```

- [ ] **Step 2: Run to verify failure**

Run: `scripts/rots-docker.sh test --gtest_filter='DeathTakesFullMobXpLoss.*:DeathCountsAsPlayerKill.*'`
Expected: BUILD FAILURE — both selectors undeclared.

- [ ] **Step 3: Declare and define**

`src/handler.h`, after `find_engaged_real_mob`:

```cpp
// Whether the death takes the full mob-death XP loss on top of the
// unconditional tenth. legacy derives from the killer as die() always has.
bool death_takes_full_mob_xp_loss(const struct char_data* killer, death_punishment punishment);

// raw_kill()'s gentle-vs-harsh arm: true selects the gentle player-kill
// penalty. legacy derives from the killer as raw_kill() always has.
bool death_counts_as_player_kill(const struct char_data* killer, death_punishment punishment);
```

`src/fight.cpp`, after `find_engaged_real_mob`:

```cpp
bool death_takes_full_mob_xp_loss(const char_data* killer, death_punishment punishment)
{
    if (punishment == death_punishment::mob_death) {
        return true;
    }
    if (punishment == death_punishment::player_death) {
        return false;
    }
    return is_real_mob(killer);
}

bool death_counts_as_player_kill(const char_data* killer, death_punishment punishment)
{
    if (punishment == death_punishment::mob_death) {
        return false;
    }
    if (punishment == death_punishment::player_death) {
        return true;
    }
    return killer != nullptr && !IS_NPC(killer);
}
```

- [ ] **Step 4: Run to verify pass**

Run: `scripts/rots-docker.sh test --gtest_filter='DeathTakesFullMobXpLoss.*:DeathCountsAsPlayerKill.*'`
Expected: 4 tests PASS.

- [ ] **Step 5: Format and commit**

```bash
clang-format -i -style=WebKit src/handler.h src/fight.cpp src/tests/death_classification_tests.cpp
git add src/handler.h src/fight.cpp src/tests/death_classification_tests.cpp
git commit -m "fight: punishment selectors for XP loss and the raw_kill penalty arm"
```

---

### Task 4: `mobdeath_record_mob()`

**Files:**
- Modify: `src/handler.h`, `src/fight.cpp`, `src/tests/death_classification_tests.cpp`

**Interfaces:**
- Consumes: `death_punishment`, `is_real_mob()`.
- Produces: `struct char_data* mobdeath_record_mob(struct char_data* killer, struct char_data* engaged_mob, death_punishment punishment);`

- [ ] **Step 1: Add failing tests**

```cpp
TEST(MobdeathRecordMob, LegacyNamesOnlyARealMobKiller)
{
    char_data mob {};
    make_plain_mob(mob);
    char_data pet {};
    make_pet_mob(pet);
    char_data player {};
    player.player.level = 20;
    char_data engaged_mob {};
    make_plain_mob(engaged_mob);

    EXPECT_EQ(mobdeath_record_mob(&mob, &engaged_mob, death_punishment::legacy), &mob);
    EXPECT_EQ(mobdeath_record_mob(&pet, &engaged_mob, death_punishment::legacy), nullptr);
    EXPECT_EQ(mobdeath_record_mob(&player, &engaged_mob, death_punishment::legacy), nullptr);
    EXPECT_EQ(mobdeath_record_mob(nullptr, &engaged_mob, death_punishment::legacy), nullptr)
        << "legacy never names the engaged mob -- byte-compatible with the removed guard";
}

TEST(MobdeathRecordMob, MobDeathNamesRealMobKillerElseTheEngagedMob)
{
    char_data mob {};
    make_plain_mob(mob);
    char_data player {};
    player.player.level = 20;
    char_data engaged_mob {};
    make_plain_mob(engaged_mob);

    EXPECT_EQ(mobdeath_record_mob(&mob, &engaged_mob, death_punishment::mob_death), &mob);
    EXPECT_EQ(mobdeath_record_mob(&player, &engaged_mob, death_punishment::mob_death), &engaged_mob);
    EXPECT_EQ(mobdeath_record_mob(nullptr, &engaged_mob, death_punishment::mob_death), &engaged_mob);
}

TEST(MobdeathRecordMob, PlayerDeathSuppressesTheRecordEvenForAMobKiller)
{
    char_data mob {};
    make_plain_mob(mob);

    EXPECT_EQ(mobdeath_record_mob(&mob, nullptr, death_punishment::player_death), nullptr)
        << "an unengaged mob-poison death is not recorded as a mob death";
}
```

- [ ] **Step 2: Run to verify failure**

Run: `scripts/rots-docker.sh test --gtest_filter='MobdeathRecordMob.*'`
Expected: BUILD FAILURE — `mobdeath_record_mob` undeclared.

- [ ] **Step 3: Declare and define**

`src/handler.h`, after `death_counts_as_player_kill`:

```cpp
// The NPC an EXPLOIT_MOBDEATH record names, or null when no record is due.
// legacy names a real-mob killer only; mob_death names the killer when it is
// a real mob, else engaged_mob; player_death suppresses the record.
struct char_data* mobdeath_record_mob(struct char_data* killer, struct char_data* engaged_mob, death_punishment punishment);
```

`src/fight.cpp`, after `death_counts_as_player_kill`:

```cpp
char_data* mobdeath_record_mob(char_data* killer, char_data* engaged_mob, death_punishment punishment)
{
    if (punishment == death_punishment::player_death) {
        return nullptr;
    }
    if (is_real_mob(killer)) {
        return killer;
    }
    if (punishment == death_punishment::mob_death) {
        // Non-null by construction: mob_death classification implies an engaged mob.
        return engaged_mob;
    }
    return nullptr;
}
```

- [ ] **Step 4: Run to verify pass**

Run: `scripts/rots-docker.sh test --gtest_filter='MobdeathRecordMob.*'`
Expected: 3 tests PASS.

- [ ] **Step 5: Format and commit**

```bash
clang-format -i -style=WebKit src/handler.h src/fight.cpp src/tests/death_classification_tests.cpp
git add src/handler.h src/fight.cpp src/tests/death_classification_tests.cpp
git commit -m "fight: mobdeath_record_mob() -- EXPLOIT_MOBDEATH follows the classification"
```

---

### Task 5: `raw_kill()` 4-arg form + forwarder

**Files:**
- Modify: `src/fight.cpp` (`raw_kill()` definition, ~line 1009 after Tasks 1–4 shifted it)

**Interfaces:**
- Consumes: `death_punishment`, `death_counts_as_player_kill()` (Task 3).
- Produces: `void raw_kill(char_data* dead_man, char_data* killer, int attack_type, death_punishment punishment);` — plus the unchanged 3-arg `raw_kill(...)` now forwarding with `death_punishment::legacy`. Task 6's die() calls the 4-arg form for PC victims.

No new unit tests: the PC arm of raw_kill cannot run in the harness (see the test-TU banner);
the selector's truth table is already pinned in Task 3, and the forwarder preserves every
existing caller byte-for-byte — proven by the existing suites staying green.

- [ ] **Step 1: Change the signature and add the forwarder**

In `src/fight.cpp`, change:

```cpp
void raw_kill(char_data* dead_man, char_data* killer, int attack_type)
{
```

to:

```cpp
void raw_kill(char_data* dead_man, char_data* killer, int attack_type)
{
    raw_kill(dead_man, killer, attack_type, death_punishment::legacy);
}

// The punishment classification is computed once in die(); every direct
// caller goes through the 3-arg forwarder above and keeps the killer-based
// legacy rule.
void raw_kill(char_data* dead_man, char_data* killer, int attack_type, death_punishment punishment)
{
```

- [ ] **Step 2: Swap the died_to_player computation**

In the same function, replace:

```cpp
        bool died_to_player = killer != NULL && !IS_NPC(killer);
```

with:

```cpp
        const bool died_to_player = death_counts_as_player_kill(killer, punishment);
```

Leave the `TASK-021/026 port:` comment block directly above it intact, and append one line to
its end: `// The poison carveout (death_punishment) can override this in either direction.`

- [ ] **Step 3: Run the neighboring suites to verify nothing moved**

Run: `scripts/rots-docker.sh test --gtest_filter='FightCredit.*:GroupGain.*:Damage*.*:PoisonOrigin*.*:KillContributor*.*'`
Expected: all PASS (raw_kill behavior is unchanged — every caller reaches `legacy`).

- [ ] **Step 4: Format and commit**

```bash
clang-format -i -style=WebKit src/fight.cpp
git add src/fight.cpp
git commit -m "fight: raw_kill() takes a death_punishment; 3-arg form forwards legacy"
```

---

### Task 6: `die()` restructure + `damage_credited()` call site

**Files:**
- Modify: `src/fight.cpp` (`die()` definition ~line 1135; `damage_credited()` ~lines 2074–2117)

**Interfaces:**
- Consumes: everything from Tasks 1–5 (exact names as declared).
- Produces: `void die(char_data* dead_man, char_data* killer, int attack_type, char_data* engaged_opponent);` with the 3-arg form forwarding `nullptr`. External callers (act_othe.cpp, clerics.cpp, limits.cpp — 3-arg local externs) are untouched.

- [ ] **Step 1: Change die()'s signature and add the forwarder**

Change:

```cpp
void die(char_data* dead_man, char_data* killer, int attack_type)
{
```

to:

```cpp
void die(char_data* dead_man, char_data* killer, int attack_type)
{
    die(dead_man, killer, attack_type, nullptr);
}

// `engaged_opponent` is the victim's own target captured before
// stop_fighting() -- the one engagement direction a combat_list walk cannot
// recover. Null from every caller except damage_credited()'s death branch.
void die(char_data* dead_man, char_data* killer, int attack_type, char_data* engaged_opponent)
{
```

- [ ] **Step 2: Restructure the PC region**

Everything from the ON_DIE trigger through the NPC-victim early return
(`if (IS_NPC(dead_man)) { raw_kill(dead_man, killer, attack_type); return; }`) stays
byte-for-byte (the NPC arm keeps the 3-arg raw_kill call). Then replace the remainder of the
function — from the current `/* log mobdeaths */` block down to (but NOT including) the
`GET_COND(dead_man, FULL) = 24;` line — with:

```cpp
    // Poison carveout: engagement with a real mob at the instant of death,
    // not the credited killer, decides how a PC poison death is punished.
    char_data* const engaged_mob = find_engaged_real_mob(dead_man, engaged_opponent);
    const death_punishment punishment = classify_pc_death(attack_type, engaged_mob != nullptr);

    /* log mobdeaths */
    char_data* const mobdeath_mob = mobdeath_record_mob(killer, engaged_mob, punishment);
    if (mobdeath_mob != nullptr) {
        add_exploit_record(EXPLOIT_MOBDEATH, dead_man, GET_IDNUM(mobdeath_mob), GET_NAME(mobdeath_mob));
    }

    int base_xp_gain = -(dead_man->points.exp - 3000) / (dead_man->player.level + 2);

    // Both historical arms began with this tenth; hoisted unchanged.
    gain_exp_regardless(dead_man, std::min(0, base_xp_gain / 10));

    // Recorded for every PC poison death -- the killerless arm used to skip it.
    if (attack_type == SPELL_POISON) {
        add_exploit_record(EXPLOIT_POISON, dead_man, 0, NULL);
    }

    if (killer) {
        // TASK-026 port: who took part is decided here, once, and handed to
        // the record builder -- pkill.cpp's own combat_list walks could not
        // see a poisoner or a remote room-affect caster.
        //
        // PK records are created regardless of death cause, but then early out
        // if it's all NPCs killing the character.  Heh...
        const kill_contributor_list contributors = kill_contributors(dead_man, killer);
        if (contributors.count > 0) {
            pkill_create(dead_man, contributors);
            add_exploit_record(EXPLOIT_PK, dead_man, 0, NULL); /* pk records to killers */
        }

        /* add death records to dead player */
        /* Fingolfin: Jul 19: since we record mobdeaths earlier */
        if (!IS_NPC(killer)) {
            add_exploit_record(EXPLOIT_DEATH, dead_man, 0, NULL);
        }
    }

    if (death_takes_full_mob_xp_loss(killer, punishment)) {
        gain_exp_regardless(dead_man, std::min(0, base_xp_gain));
    }
```

Notes for the implementer:
- This DELETES the old `if (!killer) { ... } else { ... }` split (the `/10` loss was the first
  statement of both arms — hoisting it is a pure refactor), the old
  `attack_type == SPELL_POISON` record inside the else arm, the long TASK-026 comment
  paragraph about the replaced early-return (its content now lives in the spec), the old
  `if (IS_NPC(killer)) { if (!MOB_FLAGGED(...)) ... }` full-loss branch and its
  "Only grant mob_death XP" comment (encoded in `death_takes_full_mob_xp_loss`), and the old
  top-of-region `EXPLOIT_MOBDEATH` guard (replaced by `mobdeath_record_mob`).
- The `if (killer && !IS_NPC(killer))` EXPLOIT_DEATH guard simplifies to `if (!IS_NPC(killer))`
  because it now sits inside `if (killer)`.
- The trailing `GET_COND` resets and the final `raw_kill` line remain; the raw_kill call
  becomes 4-arg in the next step.

- [ ] **Step 3: Pass the classification to raw_kill**

At the end of die(), change:

```cpp
    raw_kill(dead_man, killer, attack_type);
```

to:

```cpp
    raw_kill(dead_man, killer, attack_type, punishment);
```

- [ ] **Step 4: Hand engaged_opponent into die() from damage_credited()**

In `damage_credited()` (~line 2112), change:

```cpp
        die(victim, killer, attacktype);
```

to:

```cpp
        die(victim, killer, attacktype, engaged_opponent);
```

And in the capture comment block above (~lines 2074–2084), replace the sentences claiming
stop_fighting "clears" the field and "Only the credit fallback reads it" with:

```cpp
    // TASK-026 port: the opponent the victim is engaged with at the instant it
    // dies, captured HERE because the stop_fighting() call on the very next
    // line retargets or clears specials.fighting for a dead character. The
    // credit fallback below and die()'s poison-death engagement classification
    // both read this captured value, never the post-stop pointer.
```

- [ ] **Step 5: Run the neighboring suites**

Run: `scripts/rots-docker.sh test --gtest_filter='FightCredit.*:GroupGain.*:Damage*.*:PoisonOrigin*.*:KillContributor*.*:RoomAffectTick.*:AffectUpdateWalk.*'`
Expected: all PASS. (Every existing pin drives NPC victims or non-poison paths, so
classification lands on `legacy`/NPC-arm and nothing observable moves.)

- [ ] **Step 6: Format and commit**

```bash
clang-format -i -style=WebKit src/fight.cpp
git add src/fight.cpp
git commit -m "fight: die() classifies PC poison deaths by real-mob engagement"
```

---

### Task 7: full-suite gate + docs

**Files:**
- Modify: `manual-test-plan.md` (worktree root)

**Interfaces:** none produced; consumes the finished behavior.

- [ ] **Step 1: Full container suite**

Run: `scripts/rots-docker.sh test`
Expected: every test passes (the suite count grows by 16 new pins). If anything fails, stop
and report — do not patch around it.

- [ ] **Step 2: Update the manual test plan**

In `manual-test-plan.md`, section "4. Poison deaths are attributed to the poisoner", append:

```markdown
**Punishment carveout (poison only):** how a poison death *punishes* is decided by engagement
with a real mob (not a pet/orc-friend) at the instant of death, in either direction — you
fighting it, or it fighting you:
- Not engaged with any real mob → legacy treatment regardless of poison source: small XP loss,
  gentle penalty (revive at hp/4, no stat loss), `EXPLOIT_POISON` record, **no** mob-death
  record — even when a mob's poison did the killing.
- Engaged with a real mob → full mob death regardless of poison source: full XP loss, harsh
  2/3-stat penalty, mob-death record (naming the poisoning mob, else the engaged mob). A
  player poisoner still keeps their PK/kill records alongside.
Manual checks on 4810: (a) mob poisons you, flee two rooms, die alone → gentle + small loss +
poison record only; (b) same poison, die still swinging at the mob → harsh + full loss +
mob-death record; (c) player poisons you, you die fighting an unrelated mob → harsh + full
loss, poisoner keeps PK record.
```

- [ ] **Step 3: Commit docs**

```bash
git add manual-test-plan.md
git commit -m "docs: poison punishment carveout in the manual test plan"
```

---

## Self-review (done at authoring time)

- **Spec coverage:** rulings 1 (Task 2), 2–3 (Tasks 3–6), 4/record policy (Tasks 4, 6),
  5/unchanged surfaces (Tasks 5–6 regression filters + Task 7 full gate); matrix rows all fall
  out of the selector truth tables pinned in Tasks 3–4. Mist nuance needs no code (spec §Ruling 4).
- **Placeholders:** none; every step carries exact code/commands.
- **Type consistency:** `death_punishment` spelled identically across Tasks 1/3/4/5/6;
  4-arg signatures in Tasks 5/6 match their Produces blocks.
