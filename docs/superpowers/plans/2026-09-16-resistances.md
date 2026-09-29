# Resistances Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give resistances and vulnerabilities a per-affect magnitude, make the six resist spells work end to end for both casting and worn items, and make all of it persist.

**Architecture:** `affected_type` gains an `effect_modifier` percentage that rides on spell-applied affects only. `skill_data` gains an explicit `resist` id so the damage path can match an attack's element to the victim's resistance instead of reusing the drifted spec id. Slot ownership follows the existing `spell_evasion` rule: one affect per resist type, item replaces spell.

**Tech Stack:** C++17 compiled 32-bit (`-m32`), GTest via the `ageland_tests` CMake target, Docker toolchain (`scripts/rots-docker.sh`).

**Spec:** `docs/superpowers/specs/2026-09-16-resistances-design.md`

## Global Constraints

- Build is 32-bit: `cd src && make all` inside the container, or `scripts/rots-docker.sh compile`.
- Tests: `scripts/rots-docker.sh test --gtest_filter='Suite.*'`. New test files must be added to the explicit list in `src/CMakeLists.txt` (there is no glob).
- Formatting: `cd src && clang-format -i -style=WebKit <changed files>` on changed files only. **Never** run the bare `make format` target.
- Any header edit requires `make clean` before rebuilding — the Makefile tracks no header dependencies and a struct-layout change otherwise SIGSEGVs at runtime while tests stay green.
- `src/Makefile` is **not** modified. `-Wall -Wextra -Werror` may be added locally for one build and must be reverted before committing.
- Diagnostics are kept, never pruned: `debug_flag`, `debug_flag_msg`, `mudlog_debug_mob_or_player` and all their call sites.
- `MAX_AFFECT` is 32 and `MAX_OBJ_AFFECT` is 2; both are marked `*DO*NOT*CHANGE*` and stay unchanged.
- Reference material, read-only: `import/resist-live-ah` is authoritative, `import/resist-b4-cleanup` is intent only.

---

### Task 1: `effect_modifier` on affects, and JSON persistence

**Files:**
- Modify: `src/structs.h` (`struct affected_type`)
- Modify: `src/character_json.h` (`struct AffectData`)
- Modify: `src/character_json.cpp` (`write_affect`, `write_affect_v2a`, `write_affect_v2b`, `parse_affect_object`, `character_data_from_store`, `apply_character_data_to_store`)
- Test: `src/tests/character_json_tests.cpp`

**Interfaces:**
- Consumes: nothing.
- Produces: `affected_type::effect_modifier` (`int`, defaults to 0) and `character_json::AffectData::effect_modifier` (`int`, defaults to 0). Every later task relies on a freshly declared `affected_type` having `effect_modifier == 0` without the author setting it.

- [ ] **Step 1: Write the failing round-trip test**

Append to `src/tests/character_json_tests.cpp`:

```cpp
TEST(CharacterJson, RoundTripsTheAffectEffectModifier)
{
    character_json::CharacterData character = character_json::character_data_from_store(make_stored_character());
    character.affects.clear();
    character_json::AffectData affect;
    affect.type = SPELL_ARMOR; // 42, exists today; the resist spells do not until Task 4
    affect.duration = -1;
    affect.modifier = 1;
    affect.location = APPLY_RESIST;
    affect.counter = 0;
    affect.effect_modifier = 30;
    character.affects.push_back(affect);

    const std::string json = character_json::serialize_character_to_json(character);
    ASSERT_NE(json.find("\"effect_modifier\": 30"), std::string::npos) << json;

    character_json::CharacterData parsed;
    std::string error_message;
    ASSERT_TRUE(character_json::deserialize_character_from_json(json, &parsed, &error_message)) << error_message;
    ASSERT_FALSE(parsed.affects.empty());
    EXPECT_EQ(parsed.affects[0].effect_modifier, 30);
}

TEST(CharacterJson, ReadsAnAffectWithNoEffectModifierAsZero)
{
    // Every account character on disk today predates the key. Absent must mean 0, not a parse
    // failure and not a stale value.
    character_json::CharacterData character = character_json::character_data_from_store(make_stored_character());
    character.affects.clear();
    character_json::AffectData affect;
    affect.type = SPELL_ARMOR;
    affect.duration = 12;
    affect.modifier = 1;
    affect.location = APPLY_RESIST;
    affect.counter = 0;
    affect.effect_modifier = 0;
    character.affects.push_back(affect);

    std::string json = character_json::serialize_character_to_json(character);
    const std::string key = "\"effect_modifier\": 0, ";
    const std::size_t at = json.find(key);
    ASSERT_NE(at, std::string::npos) << "writer must emit the key so this test can remove it";
    json.erase(at, key.size());

    character_json::CharacterData parsed;
    std::string error_message;
    ASSERT_TRUE(character_json::deserialize_character_from_json(json, &parsed, &error_message)) << error_message;
    ASSERT_FALSE(parsed.affects.empty());
    EXPECT_EQ(parsed.affects[0].effect_modifier, 0);
    EXPECT_EQ(parsed.affects[0].type, character.affects[0].type) << "other fields must be untouched";
}
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `scripts/rots-docker.sh test --gtest_filter='CharacterJson.RoundTripsTheAffectEffectModifier:CharacterJson.ReadsAnAffectWithNoEffectModifierAsZero'`
Expected: FAIL to compile — `AffectData` has no member `effect_modifier`.

- [ ] **Step 3: Add the field to both structs**

In `src/structs.h`, inside `struct affected_type`, after `sh_int counter;`:

```c
    int effect_modifier = 0; /* percentage magnitude for resistances; 0 = none */
```

The default member initialiser is deliberate: roughly forty places build an `affected_type` field
by field, and only the resist spells set this one. A default of 0 means none of them can leak a
stale value. `affected_type` stays trivially copyable, so the `memset` of `char_file_u` in
`db.cpp` and its array use are unaffected.

In `src/character_json.h`, inside `struct AffectData`, after `int counter = 0;`:

```cpp
    int effect_modifier = 0;
```

- [ ] **Step 4: Emit the key from all three serializers**

`src/character_json.cpp`, in `write_affect`, after the `counter` line:

```cpp
        output << "\"effect_modifier\": " << affect.effect_modifier << ", ";
```

In `write_affect_v2a`, after the `counter` pair:

```cpp
        writer.raw(", \"effect_modifier\": ");
        writer.number(affect.effect_modifier);
```

Apply the identical change to `write_affect_v2b`. All three must emit the key in the same position
with the same spacing: `json_perf_tests.cpp` asserts the three serializers produce byte-identical
output.

- [ ] **Step 5: Parse the key as optional**

In `parse_affect_object`, add a handler alongside the others:

```cpp
            if (key == "effect_modifier")
                return nested_reader->parse_integer(&affect->effect_modifier, nested_error_message);
```

Do **not** add a `saw_effect_modifier` flag and do **not** add it to the
`if (!saw_type || !saw_duration || ...)` required-field check. Absent is legal and means 0, which
the struct default already supplies.

- [ ] **Step 6: Carry it through the store conversions**

In `character_data_from_store`, in the loop that fills `AffectData` from
`stored_character.affected[index]`, after `affect_data.counter = affect.counter;`:

```cpp
        affect_data.effect_modifier = affect.effect_modifier;
```

In `apply_character_data_to_store`, in the loop that fills `stored_character->affected[index]`,
after the `counter` assignment:

```cpp
        affect.effect_modifier = json_character.affects[index].effect_modifier;
```

- [ ] **Step 7: Run the tests to verify they pass**

Run: `scripts/rots-docker.sh test --gtest_filter='CharacterJson.*'`
Expected: PASS, including the pre-existing cases.

- [ ] **Step 8: Run the serializer-equivalence tests**

Run: `scripts/rots-docker.sh test --gtest_filter='JsonPerf.*'`
Expected: PASS. A failure here means the three writers disagree — fix the odd one out.

- [ ] **Step 9: Format and commit**

```bash
cd src && clang-format -i -style=WebKit structs.h character_json.h character_json.cpp tests/character_json_tests.cpp
cd .. && git add src/structs.h src/character_json.h src/character_json.cpp src/tests/character_json_tests.cpp
git commit -m "feat(affects): give affected_type an effect_modifier and persist it"
```

---

### Task 2: Resist identity — `RESIST_*`, `skills[].resist`, and attack-to-element lookup

**Files:**
- Modify: `src/structs.h` (add the `RESIST_*` block above `PLRSPEC_NONE`)
- Modify: `src/spells.h` (`struct skill_data`)
- Modify: `src/consts.cpp` (every `skills[]` row; `resistance_name[]`; `vulnerability_name[]`)
- Modify: `src/utility.cpp` (`check_resistances`, new `resist_type_for_attack`)
- Modify: `src/utils.h` (declare `resist_type_for_attack`)
- Create: `src/tests/resistance_tests.cpp`
- Modify: `src/CMakeLists.txt` (register the new test file)

**Interfaces:**
- Consumes: nothing from Task 1.
- Produces: `RESIST_*` constants; `skill_data::resist` (`char`, 13th field, after `skill_spec`);
  `int resist_type_for_attack(int attack_type)` returning a `RESIST_*` value.

- [ ] **Step 1: Write the failing test**

Create `src/tests/resistance_tests.cpp`:

```cpp
#include "gtest/gtest.h"

#include "spells.h"
#include "structs.h"
#include "utils.h"

extern struct skill_data skills[];

TEST(ResistanceIdentity, DamagingSpellsCarryTheirElement)
{
    EXPECT_EQ(skills[SPELL_FIREBOLT].resist, RESIST_FIRE);
    EXPECT_EQ(skills[SPELL_FIREBALL].resist, RESIST_FIRE);
    EXPECT_EQ(skills[SPELL_BLAZE].resist, RESIST_FIRE);
    EXPECT_EQ(skills[SPELL_CHILL_RAY].resist, RESIST_COLD);
    EXPECT_EQ(skills[SPELL_CONE_OF_COLD].resist, RESIST_COLD);
    EXPECT_EQ(skills[SPELL_LIGHTNING_BOLT].resist, RESIST_LGHT);
    EXPECT_EQ(skills[SPELL_LIGHTNING_STRIKE].resist, RESIST_LGHT);
    EXPECT_EQ(skills[SPELL_DARK_BOLT].resist, RESIST_DARK);
    EXPECT_EQ(skills[SPELL_SEARING_DARKNESS].resist, RESIST_DARK);
    EXPECT_EQ(skills[SPELL_SPEAR_OF_DARKNESS].resist, RESIST_DARK);
    EXPECT_EQ(skills[SPELL_BLACK_ARROW].resist, RESIST_DARK);
}

TEST(ResistanceIdentity, DarkIsNotTheSpecIndex)
{
    // The whole point of the separate field: PLRSPEC_DARK is 16, the dark resistance bit is 12.
    EXPECT_EQ(RESIST_DARK, 12);
    EXPECT_EQ(PLRSPEC_DARK, 16);
    EXPECT_NE(static_cast<int>(skills[SPELL_DARK_BOLT].resist), static_cast<int>(skills[SPELL_DARK_BOLT].skill_spec));
}

TEST(ResistTypeForAttack, MapsSpellsWeaponsAndUnknowns)
{
    EXPECT_EQ(resist_type_for_attack(SPELL_FIREBOLT), RESIST_FIRE);
    EXPECT_EQ(resist_type_for_attack(SPELL_MAGIC_MISSILE), RESIST_NONE);

    EXPECT_EQ(resist_type_for_attack(TYPE_HIT), RESIST_PHYS);
    EXPECT_EQ(resist_type_for_attack(TYPE_CRUSH), RESIST_PHYS);
    EXPECT_EQ(resist_type_for_attack(SKILL_ARCHERY), RESIST_PHYS);

    EXPECT_EQ(resist_type_for_attack(TYPE_SUFFERING), RESIST_NONE);
    EXPECT_EQ(resist_type_for_attack(MAX_SKILLS + 10), RESIST_NONE);
    EXPECT_EQ(resist_type_for_attack(-1), RESIST_NONE);
}
```

Register it in `src/CMakeLists.txt` next to the other entries:

```cmake
    tests/resistance_tests.cpp
```

- [ ] **Step 2: Run to verify it fails**

Run: `scripts/rots-docker.sh test --gtest_filter='Resist*'`
Expected: FAIL to compile — `RESIST_FIRE` undeclared, `skill_data` has no member `resist`.

- [ ] **Step 3: Add the `RESIST_*` ids**

In `src/structs.h`, immediately above `#define PLRSPEC_NONE 0`:

```c
/* Resistance / vulnerability ids. These index resistance_name[] and vulnerability_name[]
   and are what IS_RESISTANT / IS_VULNERABLE shift by. They are NOT the PLRSPEC_* spec ids:
   the two lists diverged, which is why skill_data carries this id explicitly. */
#define RESIST_NONE 0
#define RESIST_FIRE 1
#define RESIST_COLD 2
#define RESIST_REGN 3
#define RESIST_PROT 4
#define RESIST_PETS 5
#define RESIST_STLH 6
#define RESIST_PHYS 7
#define RESIST_TELE 8
#define RESIST_ILLU 9
#define RESIST_LGHT 10
#define RESIST_MIND 11
#define RESIST_DARK 12
#define RESIST_LFGT 13
```

- [ ] **Step 4: Add the field and fill every row**

In `src/spells.h`, in `struct skill_data`, after `char skill_spec;`:

```c
    char resist; /* RESIST_* this skill's damage is resisted as */
```

In `src/consts.cpp`, append a 13th value to every row of `skills[]`. Take the values from
`import/resist-live-ah` rather than retyping them:

```bash
git show import/resist-live-ah:src/consts.cpp > /tmp/liveah-consts.cpp
```

Every row gets one, `RESIST_NONE` unless the row is one of these:

| skill | resist |
|---|---|
| wild swing | `RESIST_PHYS` |
| animals | `RESIST_PETS` |
| stealth, stalking | `RESIST_STLH` |
| curing saturation, restlessness, vitality, refresh all, regeneration, cure self, vitalize self | `RESIST_REGN` |
| haze, fear, terror | `RESIST_ILLU` |
| chill ray, freeze, cone of cold | `RESIST_COLD` |
| lightning bolt, flash, lightning strike | `RESIST_LGHT` |
| blaze, firebolt, fireball | `RESIST_FIRE` |
| dark bolt, mist of baazunga, searing darkness, spear of darkness, black arrow | `RESIST_DARK` |

Also update the two name tables in the same file so index 7 and index 12 are correct:

```c
char *resistance_name[] = {"UNGROUPED",                                             /* 0 */
                           "FIRE",      "COLD",     "REGEN",     "PROT", "ANIMALS", /* 5 */
                           "STEALTH",
                           "PHYSICAL",                             // also resistance to hit-crush
                           "TELEPORT",  "ILLUSION", "LIGHTNING", /* 10*/
                           "MIND",      "DARK",         "",          "",     "", /* 15*/
                           "\n"};
```

and the same two edits in `vulnerability_name[]`: `"V-WILD"` becomes `"V-PHYSICAL"`, and the empty
string at index 12 becomes `"V-DARK"`.

- [ ] **Step 5: Add the lookup and rewire `check_resistances`**

In `src/utility.cpp`, above `check_resistances`:

```cpp
/* The RESIST_* an attack is resisted as. Weapon damage types and archery have no skills[]
   row of their own and all count as physical. */
int resist_type_for_attack(int attack_type)
{
    extern skill_data skills[];

    if (((attack_type >= TYPE_HIT) && (attack_type <= TYPE_CRUSH)) || (attack_type == SKILL_ARCHERY))
        return RESIST_PHYS;

    if ((attack_type >= 0) && (attack_type < MAX_SKILLS))
        return skills[attack_type].resist;

    return RESIST_NONE;
}
```

Note the ordering: the weapon-type check comes **first**, so `SKILL_DEFEND`, which collides with
`TYPE_BLUDGEON` at 131, can no longer make a defend-resistant victim immune to bludgeons.

Then replace the body of `check_resistances`:

```cpp
int check_resistances(char_data* victim, int attack_type)
{
    const int resist_type = resist_type_for_attack(attack_type);

    if (IS_RESISTANT(victim, resist_type))
        return 1;

    if (IS_VULNERABLE(victim, resist_type))
        return -1;

    return 0;
}
```

Declare it in `src/utils.h` beside the other free functions:

```c
int resist_type_for_attack(int attack_type);
```

- [ ] **Step 6: Run the tests to verify they pass**

Run: `scripts/rots-docker.sh test --gtest_filter='Resist*'`
Expected: PASS, all three cases.

- [ ] **Step 7: Rebuild fully, because headers changed**

Run: `scripts/rots-docker.sh shell -c 'cd src && make clean && make all'`
Expected: builds `../bin/ageland`.

- [ ] **Step 8: Run the whole suite against the baseline**

Run: `scripts/rots-docker.sh test`
Expected: the same pass/fail set as the baseline taken on the clean tree. Any new failure is yours.

- [ ] **Step 9: Format and commit**

```bash
cd src && clang-format -i -style=WebKit structs.h spells.h consts.cpp utility.cpp utils.h tests/resistance_tests.cpp
cd .. && git add src/structs.h src/spells.h src/consts.cpp src/utility.cpp src/utils.h src/tests/resistance_tests.cpp src/CMakeLists.txt
git commit -m "feat(resist): give every skill an explicit resist id and match attacks to it"
```

---

### Task 3: Diagnostics plumbing

Everything after this task calls `debug_flag_msg`, so it lands first.

**Files:**
- Modify: `src/structs.h` (`struct char_data`)
- Modify: `src/utility.cpp` (`has_debug_flag`, `debug_flag_msg`, `mudlog_debug_mob_or_player`)
- Modify: `src/utils.h` (declarations)
- Modify: `src/act_wiz.cpp` (`do_debug`)
- Modify: `src/interpre.cpp` (command table entry and prototype)

**Interfaces:**
- Consumes: nothing.
- Produces: `int has_debug_flag(char_data* ch)`, `void debug_flag_msg(char* buf, char_data* ch)`,
  `void mudlog_debug_mob_or_player(char* buf, char_data* ch, char_data* vict)`, and a `debug`
  command at index 250.

- [ ] **Step 1: Add the flag**

In `src/structs.h`, in `struct char_data`, beside `interrupt_time`:

```c
    int debug_flag = 0; /* Imms may set this for increased debug output */
```

- [ ] **Step 2: Add the helpers**

In `src/utility.cpp`, next to `mudlog_debug_mob`:

```cpp
int has_debug_flag(char_data* ch)
{
    return ch ? ch->debug_flag : 0;
}

void debug_flag_msg(char* buf, char_data* ch)
{
    if (has_debug_flag(ch))
        send_to_char(buf, ch);
}

void mudlog_debug_mob_or_player(char* buf, char_data* ch, char_data* vict)
{
    mudlog_debug_mob(buf, ch);
    mudlog_debug_mob(buf, vict);
}
```

The null guard in `has_debug_flag` is a deliberate deviation from the source branch: the damage
path calls it with a victim that callers do not always guarantee.

Declare all three in `src/utils.h`.

- [ ] **Step 3: Add the command**

In `src/act_wiz.cpp`, after `do_goto`:

```cpp
// non-persistent, used to flag output of debugging messages
ACMD(do_debug)
{
    if (ch->debug_flag == 0) {
        ch->debug_flag = 1;
        send_to_char("Debug flag on.\n\r", ch);
    } else {
        ch->debug_flag = 0;
        send_to_char("Debug flag off.\n\r", ch);
    }
}
```

In `src/interpre.cpp`: add `ACMD(do_debug);` beside the other prototypes, add `"debug",` to the end
of the `command[]` table immediately before `"\n"`, and register it in `assign_command_pointers`:

```cpp
    COMMANDO(250, POSITION_STANDING, do_debug, LEVEL_GRGOD, TRUE, 0,
        TAR_NONE_OK, TAR_IGNORE, 0);
```

Check the highest existing `COMMANDO` index before using 250 — it must be the next free slot, and
the `command[]` string position must line up with it.

- [ ] **Step 4: Rebuild and smoke it**

Run: `scripts/rots-docker.sh shell -c 'cd src && make clean && make all'`
Then boot (`scripts/rots-docker.sh boot`), log in an immortal of `LEVEL_GRGOD` or above, and type
`debug` twice.
Expected: "Debug flag on." then "Debug flag off."

- [ ] **Step 5: Format and commit**

```bash
cd src && clang-format -i -style=WebKit structs.h utility.cpp utils.h act_wiz.cpp interpre.cpp
cd .. && git add src/structs.h src/utility.cpp src/utils.h src/act_wiz.cpp src/interpre.cpp
git commit -m "feat(debug): per-character debug flag and the debug command"
```

---

### Task 4: The resist spells

**Files:**
- Modify: `src/spells.h` (spell numbers, `ASPELL` prototypes)
- Modify: `src/mystic.cpp` (`do_resist_spell`, six wrappers, `spell_protection`, `do_unprotect`)
- Modify: `src/handler.cpp` (`eff_mod` transport in the `APPLY_SPELL` case)
- Modify: `src/consts.cpp` (rows 161–166)
- Modify: `src/interpre.cpp` (`unprotect` command)
- Test: `src/tests/resistance_tests.cpp`

**Interfaces:**
- Consumes: `RESIST_*` and `skill_data::resist` from Task 2; `affected_type::effect_modifier` from
  Task 1; `debug_flag_msg` from Task 3.
- Produces: `SPELL_RESIST_FIRE` 161, `SPELL_RESIST_COLD` 162, `SPELL_RESIST_LIGHT` 163,
  `SPELL_RESIST_ILLUSION` 164, `SPELL_RESIST_PHYSICAL` 165, `SPELL_RESIST_DARK` 166; affects with
  `location == APPLY_RESIST`, `modifier == RESIST_*`, `effect_modifier ==` the percentage.

- [ ] **Step 1: Write the failing test for the cast magnitude**

Add to `src/tests/resistance_tests.cpp`:

```cpp
TEST(ResistSpell, CastMagnitudeIsLevelPlusTenCappedAtForty)
{
    EXPECT_EQ(cast_resist_magnitude(1), 11);
    EXPECT_EQ(cast_resist_magnitude(29), 39);
    EXPECT_EQ(cast_resist_magnitude(30), 40);
    EXPECT_EQ(cast_resist_magnitude(60), 40) << "a cast resist is capped at 40%";
}
```

and declare the helper at the top of the file:

```cpp
int cast_resist_magnitude(int caster_level);
```

- [ ] **Step 2: Run to verify it fails**

Run: `scripts/rots-docker.sh test --gtest_filter='ResistSpell.*'`
Expected: FAIL to link — `cast_resist_magnitude` undefined.

- [ ] **Step 3: Add the magnitude helper**

In `src/mystic.cpp`, above `do_resist_spell`:

```cpp
/* A cast resistance is worth the caster's level + 10, capped at 40%. An item's value is
   whatever the builder encoded and is not derived from anyone's level. */
int cast_resist_magnitude(int caster_level)
{
    int level = caster_level;
    if (level > 30)
        level = 30;
    return level + 10;
}
```

- [ ] **Step 4: Run to verify it passes**

Run: `scripts/rots-docker.sh test --gtest_filter='ResistSpell.*'`
Expected: PASS.

- [ ] **Step 5: Add the spell numbers and prototypes**

In `src/spells.h`, in the Mystic block:

```c
#define SPELL_RESIST_FIRE 161
#define SPELL_RESIST_COLD 162
#define SPELL_RESIST_LIGHT 163
#define SPELL_RESIST_ILLUSION 164
#define SPELL_RESIST_PHYSICAL 165
#define SPELL_RESIST_DARK 166
```

and beside the other `ASPELL` declarations:

```c
ASPELL(spell_resist_fire);
ASPELL(spell_resist_cold);
ASPELL(spell_resist_light);
ASPELL(spell_resist_illusion);
ASPELL(spell_resist_physical);
ASPELL(spell_resist_dark);
```

- [ ] **Step 6: Write the shared spell and its wrappers**

In `src/mystic.cpp`, with `extern int eff_mod;` added near the other externs at the top:

```cpp
/* One implementation for all six resist spells. Slot ownership follows spell_evasion:
   an item or an unequip strips whatever holds the slot first, so an item always wins it
   and removing the item always clears it. */
void do_resist_spell(int resist_type, int modifier, char_data* caster, char_data* victim,
    int type, int is_object, const char* str)
{
    affected_type newaf;
    affected_type* current_effect = affected_by_spell(victim, resist_type);

    if ((type == SPELL_TYPE_ANTI) || is_object) {
        if (current_effect != NULL)
            affect_remove(victim, current_effect);
        if (type == SPELL_TYPE_ANTI)
            return;
        current_effect = NULL;
    }

    if (current_effect)
        return;

    const int level = get_mystic_caster_level(caster);

    newaf.type = resist_type;
    newaf.duration = (is_object) ? -1 : level * 2;
    newaf.modifier = modifier;
    newaf.location = APPLY_RESIST;
    newaf.bitvector = 0;
    newaf.counter = 0;
    newaf.effect_modifier = (is_object) ? eff_mod : cast_resist_magnitude(GET_LEVEL(caster));

    sprintf(buf, "::RESIST::apply type %d modifier %d eff_mod %d duration %d\n\r",
        newaf.type, newaf.modifier, newaf.effect_modifier, newaf.duration);
    debug_flag_msg(buf, victim);

    affect_to_char(victim, &newaf);
    sprintf(buf, "You feel resistant to %s!\n\r", str);
    send_to_char(buf, victim);
}

ASPELL(spell_resist_fire) { do_resist_spell(SPELL_RESIST_FIRE, RESIST_FIRE, caster, victim, type, is_object, "fire"); }
ASPELL(spell_resist_cold) { do_resist_spell(SPELL_RESIST_COLD, RESIST_COLD, caster, victim, type, is_object, "cold"); }
ASPELL(spell_resist_light) { do_resist_spell(SPELL_RESIST_LIGHT, RESIST_LGHT, caster, victim, type, is_object, "lightning"); }
ASPELL(spell_resist_illusion) { do_resist_spell(SPELL_RESIST_ILLUSION, RESIST_ILLU, caster, victim, type, is_object, "illusion"); }
ASPELL(spell_resist_physical) { do_resist_spell(SPELL_RESIST_PHYSICAL, RESIST_PHYS, caster, victim, type, is_object, "physical harm"); }
ASPELL(spell_resist_dark) { do_resist_spell(SPELL_RESIST_DARK, RESIST_DARK, caster, victim, type, is_object, "dark"); }
```

Three deliberate differences from `import/resist-live-ah`: the strip-then-add ownership rule
replaces its `if (current_effect) return;` refusal; the `newaf.duration = -1;` line that
overwrote the computed duration is gone; and `affect_remove` is called on `victim` rather than
`caster`, which are the same character on the item path but not in general.

- [ ] **Step 7: Make the transport explicit**

In `src/handler.cpp`, at file scope near the top:

```cpp
/* The only channel an item has for telling a spell how strong it is. An object's affect
   carries level*256+spellnum and the spell is invoked with obj = 0, so it cannot read
   anything off the item itself. Set immediately before the call, cleared on every exit. */
int eff_mod = 0;
```

and in the `APPLY_SPELL` case of `affect_modify`, keeping every existing early exit:

```cpp
    case APPLY_SPELL:
        if (!add)
            mod = -mod;
        tmp = mod & 255; // spell number, in skills[] table
        tmp2 = mod / 256; // spell level
        if (!tmp2)
            tmp2 = GET_LEVEL(ch);

        eff_mod = tmp2;

        sprintf(buf, "--APPLY_SPELL: spell %d level %d\n\r", tmp, tmp2);
        debug_flag_msg(buf, ch);

        if (tmp >= MAX_SKILLS || !skills[tmp].spell_pointer) {
            eff_mod = 0;
            break;
        }

        skills[tmp].spell_pointer(ch, "", add ? SPELL_TYPE_SPELL : SPELL_TYPE_ANTI, ch, 0, 0, 1);
        eff_mod = 0;
        break;
```

`import/resist-live-ah` used `tmp >= 220` here where the original was `tmp >= 128`; `MAX_SKILLS`
is the bound that matches the array being indexed.

- [ ] **Step 7b: Carry `removeable_spell_affection`**

The spec keeps this helper even though the evasion rule makes it unnecessary. Add it to
`src/handler.cpp` beside `affected_by_spell`, and declare it in `src/handler.h`:

```cpp
/* Returns aff's counterpart on ch only when it is the sole affect of that type, i.e. when
   removing it cannot strip a slot something else still depends on. Unused: slot ownership
   is settled by the strip-then-add rule in do_resist_spell. Kept for reference. */
affected_type* removeable_spell_affection(const char_data* ch, affected_type* aff, affected_type* start_affect)
{
    int match_count = 0;
    int count = 0;
    affected_type* found = NULL;

    for (affected_type* status_affect = start_affect; status_affect && (count < MAX_AFFECT);
         status_affect = status_affect->next, count++) {
        if (status_affect->type == aff->type) {
            found = status_affect;
            match_count++;
        }
    }

    return (match_count == 1) ? found : NULL;
}
```

`found` is initialised to `NULL` here; the source branch left it uninitialised.

- [ ] **Step 8: Add the `skills[]` rows**

In `src/consts.cpp`, at indices 161–166. Copy the rows from `import/resist-live-ah` and apply one
correction: fire, cold and lightning have `learn_diff` and `learn_type` transposed relative to the
other three. All six read:

```c
    {"resist fire", PROF_CLERIC, 0, spell_resist_fire, POSITION_STANDING, 5, 21, 32, 10, 1, 0,
     PLRSPEC_PROT, RESIST_NONE},
```

with the name and function changed per spell. `RESIST_NONE` is correct: these spells deal no
damage, so nothing resists them. The level, mana and beats values are placeholders the spec
defers.

- [ ] **Step 9: Convert `spell_protection` and add `unprotect`**

In `spell_protection`, change each sphere's `newaf.modifier` from `PLRSPEC_*` to the matching
`RESIST_*` (`RESIST_FIRE`, `RESIST_COLD`, `RESIST_LGHT`, `RESIST_PHYS`), add
`newaf.effect_modifier = cast_resist_magnitude(GET_LEVEL(caster));` to each, and add the illusion
sphere as `case 4` writing `RESIST_ILLU`. Fix the sphere table, which is missing a comma:

```cpp
    static char* protection_sphere[] = {
        "fire",
        "cold",
        "lightning",
        "physical",
        "illusion",
        "\n"
    };
```

Without that comma `"illusion"` and `"\n"` concatenate and the table loses its terminator.

Add `do_unprotect` at the end of `src/mystic.cpp`:

```cpp
void do_unprotect(char_data* character, char* argument, waiting_type* wait_list, int command, int sub_command)
{
    if (utils::is_affected_by_spell(*character, SPELL_PROTECTION)) {
        send_to_char("You renounce your protection!\n\r", character);
        act("$n renounces $s protection!", FALSE, character, nullptr, nullptr, TO_ROOM);
        affect_from_char(character, SPELL_PROTECTION);
        return;
    }

    send_to_char("You renounce yourself to the world, but nothing happens...\n\r", character);
}
```

The source branch also did `REMOVE_BIT(character->specials.affected_by, SPELL_PROTECTION)`, which
mixes a spell number into an `AFF_*` bitfield; `affect_from_char` already unwinds the affect
properly, so that line is not carried.

Register it in `src/interpre.cpp` the same way as `debug`, at index 249 with a prototype and a
`"unprotect",` entry in `command[]`.

- [ ] **Step 10: Rebuild and run the suite**

Run: `scripts/rots-docker.sh shell -c 'cd src && make clean && make all'` then
`scripts/rots-docker.sh test`
Expected: build succeeds; suite matches baseline.

- [ ] **Step 11: Live-check the slot rule**

Boot, then with an immortal:
1. `debug` on.
2. Load 2069 (`load obj 2069`), wear it. `affections` lists a fire resistance.
3. Remove it. The resistance is gone.
4. Set knowledge in resist fire, cast it, then wear 2069. The item's value replaces the cast one.
5. Control: repeat the cast/wear/remove sequence with 6393 and evasion. It must behave exactly as
   it does on a clean build.

- [ ] **Step 12: Format and commit**

```bash
cd src && clang-format -i -style=WebKit spells.h mystic.cpp handler.cpp consts.cpp interpre.cpp tests/resistance_tests.cpp
cd .. && git add src/spells.h src/mystic.cpp src/handler.cpp src/consts.cpp src/interpre.cpp src/tests/resistance_tests.cpp
git commit -m "feat(spells): six resist spells with magnitudes and evasion-style slot ownership"
```

---

### Task 5: The damage path

**Files:**
- Modify: `src/fight.cpp` (`damage`; replace `calculate_resist_spell_damage` and
  `get_resisted_damage` with `apply_resistance` and `resist_magnitude_for`)
- Test: `src/tests/resistance_tests.cpp`

Both new helpers live in `fight.cpp` with external linkage so the test file can declare and call
them; no header change is needed.

**Interfaces:**
- Consumes: `resist_type_for_attack` (Task 2), `effect_modifier` (Task 1), affects written by
  Task 4.
- Produces: `int apply_resistance(int dam, int magnitude)`.

- [ ] **Step 1: Write the failing test for the arithmetic**

Add to `src/tests/resistance_tests.cpp`:

```cpp
TEST(ApplyResistance, ReducesByThePercentageAndRoundsHalfAway)
{
    EXPECT_EQ(apply_resistance(100, 40), 60);
    EXPECT_EQ(apply_resistance(100, 30), 70);
    EXPECT_EQ(apply_resistance(10, 33), 7) << "3.3 rounds to 3";
    EXPECT_EQ(apply_resistance(11, 50), 5) << "5.5 rounds to 6, leaving 5";
    EXPECT_EQ(apply_resistance(100, 0), 100) << "no magnitude means no reduction here";
    EXPECT_EQ(apply_resistance(1, 100), 0);
}
```

and declare it at the top of the file:

```cpp
int apply_resistance(int dam, int magnitude);
```

- [ ] **Step 2: Run to verify it fails**

Run: `scripts/rots-docker.sh test --gtest_filter='ApplyResistance.*'`
Expected: FAIL to link.

- [ ] **Step 3: Implement the arithmetic**

In `src/fight.cpp`, replacing `calculate_resist_spell_damage`:

```cpp
/* Reduce dam by magnitude percent. Kept as a free function so the arithmetic is testable
   without a character. */
int apply_resistance(int dam, int magnitude)
{
    if (magnitude <= 0)
        return dam;

    const double reduced = round((double)dam * ((double)magnitude / 100.0));
    return dam - (int)reduced;
}
```

- [ ] **Step 4: Run to verify it passes**

Run: `scripts/rots-docker.sh test --gtest_filter='ApplyResistance.*'`
Expected: PASS.

- [ ] **Step 5: Add the element-matched magnitude lookup**

In `src/fight.cpp`:

```cpp
/* The magnitude the victim has against this element, or 0 if the resistance came from a
   mob or object flag rather than a spell affect. Matching on location+modifier finds both
   SPELL_RESIST_* and SPELL_PROTECTION, which write the same shape. */
int resist_magnitude_for(char_data* victim, int resist_type)
{
    int count = 0;
    for (affected_type* aff = victim->affected; aff && count < MAX_AFFECT; aff = aff->next, count++) {
        if (aff->location == APPLY_RESIST && aff->modifier == resist_type)
            return aff->effect_modifier;
    }

    return 0;
}
```

- [ ] **Step 6: Replace the resistance block in `damage`**

Replace the `check_resistances` / `get_resisted_damage` block with:

```cpp
    const int resist_type = resist_type_for_attack(attacktype);
    tmp = check_resistances(victim, attacktype);

    sprintf(buf, "::DAMAGE:: attacktype %d resist_type %d check %d dam %d\n\r",
        attacktype, resist_type, tmp, dam);
    debug_flag_msg(buf, victim);

    if (tmp > 0) {
        const int magnitude = resist_magnitude_for(victim, resist_type);
        if (magnitude > 0) {
            dam = apply_resistance(dam, magnitude);
            sprintf(buf, "::DAMAGE:: resisted %d%% -> dam %d\n\r", magnitude, dam);
            debug_flag_msg(buf, victim);
            send_to_char("You resist a lot.\n\r", victim);
            act("$n resists a lot.\n\r", TRUE, victim, 0, 0, TO_ROOM);
        } else if (!(number(0, 2) == 0 && IS_PHYSICAL(attacktype))) {
            /* Flag resistance from a mob record or an APPLY_RESIST item: no magnitude
               exists, so the original flat rule applies unchanged, including the 1-in-3
               chance that a physical resistance does not fire at all. */
            dam = dam * 2 / 3;
            debug_flag_msg("::DAMAGE:: flag resistance, flat 1/3\n\r", victim);
            send_to_char("You resist a lot.\n\r", victim);
            act("$n resists a lot.\n\r", TRUE, victim, 0, 0, TO_ROOM);
        }
    }

    if (tmp < 0) {
        send_to_char("You feel it a lot.\n\r", victim);
        dam = dam * 3 / 2;
    }
```

`get_resisted_damage` is removed along with its five-way `affected_by_spell` chain, its
`is_resistant` by-value assignment, and the physical special case — all three are subsumed by
matching on the element.

`#include <math.h>` is needed at the top of `fight.cpp` for `round`.

- [ ] **Step 7: Rebuild and run the suite**

Run: `scripts/rots-docker.sh shell -c 'cd src && make clean && make all'` then
`scripts/rots-docker.sh test`
Expected: build succeeds; suite matches baseline.

- [ ] **Step 8: Live-check the damage numbers**

Boot. In arena 1120, with `debug` on and the test weapon 2068 (script 2393):
1. Take fire damage with no resistance; record the number.
2. Wear 2069 (fire 30) and repeat: damage is 30% lower, and the debug line shows
   `resist_type 1 ... resisted 30%`.
3. **Take cold damage while wearing the fire item: damage must be unreduced.** That is the bug
   this task fixes.
4. Attack a mob whose `.mob` record carries a resistance bit: it still takes ⅓ less.
5. Attack something vulnerable: 50% more.

- [ ] **Step 9: Format and commit**

```bash
cd src && clang-format -i -style=WebKit fight.cpp tests/resistance_tests.cpp
cd .. && git add src/fight.cpp src/tests/resistance_tests.cpp
git commit -m "fix(combat): match a resistance to the element of the incoming attack"
```

---

### Task 6: The affections display

**Files:**
- Modify: `src/utility.cpp` (`sprintbit_affections`, `lowercase`, `remove_pattern`)
- Modify: `src/utils.h` (declarations)
- Modify: `src/act_info.cpp` (`do_affections`)
- Test: `src/tests/resistance_tests.cpp`

**Interfaces:**
- Consumes: `resist_magnitude_for` semantics from Task 5 (same lookup, reimplemented against the
  character's affect list), `RESIST_*` names from Task 2.
- Produces: `void sprintbit_resistances(char_data* ch, long vektor, char* names[], char* result, int default_percent)`.

- [ ] **Step 1: Write the failing test**

Add to `src/tests/resistance_tests.cpp`:

```cpp
TEST(RemovePattern, StripsTheVulnerabilityPrefix)
{
    char out[64];
    char pattern[] = "V-";
    char input[] = "V-FIRE";
    remove_pattern(input, out, pattern);
    EXPECT_STREQ(out, "FIRE");

    char untouched[] = "FIRE";
    remove_pattern(untouched, out, pattern);
    EXPECT_STREQ(out, "FIRE");
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `scripts/rots-docker.sh test --gtest_filter='RemovePattern.*'`
Expected: FAIL to link.

- [ ] **Step 3: Add the string helpers**

In `src/utility.cpp`, taken from `import/resist-live-ah`:

```cpp
void lowercase(char* str)
{
    for (int i = 0; str[i]; i++)
        str[i] = tolower(str[i]);
}

void remove_pattern(char* str, char* result, char* patern)
{
    int i, j = 0, k = 0, n = 0, flag = 0;

    for (i = 0; str[i] != '\0'; i++) {
        k = i;
        while (str[i] == patern[j]) {
            i++, j++;
            if (j == (int)strlen(patern)) {
                flag = 1;
                break;
            }
        }
        j = 0;

        if (flag == 0)
            i = k;
        else
            flag = 0;

        result[n++] = str[i];
    }
    result[n] = '\0';
}
```

Declare both in `src/utils.h`.

- [ ] **Step 4: Run to verify it passes**

Run: `scripts/rots-docker.sh test --gtest_filter='RemovePattern.*'`
Expected: PASS.

- [ ] **Step 5: Write the resistance renderer**

In `src/utility.cpp`:

```cpp
/* Render a resistance or vulnerability bitvector, one per line, with the strength that
   actually applies: the affect's effect_modifier when a spell or item put it there, and the
   flat legacy default when the bit came from a mob record or an APPLY_RESIST item. */
void sprintbit_resistances(char_data* ch, long vektor, char* names[], char* result, int default_percent)
{
    char tmp[255];
    int nr = 0;

    *result = '\0';
    if (vektor < 1)
        return;

    for (; vektor; vektor >>= 1, nr++) {
        if (!(vektor & 1))
            continue;
        if (*names[nr] == '\n')
            break;

        remove_pattern(names[nr], tmp, (char*)"V-");
        lowercase(tmp);

        int percent = default_percent;
        int count = 0;
        for (affected_type* aff = ch->affected; aff && count < MAX_AFFECT; aff = aff->next, count++) {
            if (aff->location == APPLY_RESIST && aff->modifier == nr && aff->effect_modifier > 0) {
                percent = aff->effect_modifier;
                break;
            }
        }

        sprintf(result, "%s   %s (%d%%)\r\n", result, tmp, percent);
    }
}
```

Declare it in `src/utils.h`. This replaces `live-ah`'s `sprintbit_affections`, which had its
`IS_SET` arguments reversed, compared a `char` against the multi-character constant `'\r\n'`, and
had no way to show a percentage.

- [ ] **Step 6: Wire it into `do_affections`**

In `src/act_info.cpp`, add the externs beside the others:

```cpp
extern char* resistance_name[];
extern char* vulnerability_name[];
```

At the top of `do_affections`, before anything writes to `buf`:

```cpp
    // buf carried leftover content into the first line of this command's output
    buf[0] = '\0';
```

Then replace the "not affected by anything" branch:

```cpp
    if (!ch->affected && !GET_RESISTANCES(ch) && !GET_VULNERABILITIES(ch)) {
        sprintf(buf, "You are not affected by anything.\n\r");
    } else {
        if (GET_RESISTANCES(ch)) {
            sprintf(buf, "%sYou are resistant to:\n\r", buf);
            sprintbit_resistances(ch, GET_RESISTANCES(ch), resistance_name, buf2, 33);
            sprintf(buf, "%s%s", buf, buf2);
        }
        if (GET_VULNERABILITIES(ch)) {
            sprintf(buf, "%sYou are vulnerable to:\n\r", buf);
            sprintbit_resistances(ch, GET_VULNERABILITIES(ch), vulnerability_name, buf2, 50);
            sprintf(buf, "%s%s", buf, buf2);
        }
        sprintf(buf, "%sYou are affected by:\n\r", buf);
        for (tmpaff = ch->affected; tmpaff; tmpaff = tmpaff->next) {
            report_affection(tmpaff, str);
            sprintf(buf, "%s%s", buf, str);
        }
    }
```

Also change the `strcat(buf, "You feel weak under the intensity of light.\n\r")` near the end to
`sprintf(buf, "%s...", buf)` form, matching the rest of the function.

- [ ] **Step 7: Rebuild and check on the wire**

Run: `scripts/rots-docker.sh shell -c 'cd src && make clean && make all'`, boot, and with a test
character:
1. `affections` with nothing on — "You are not affected by anything."
2. Wear 2069 — `fire (30%)` under "You are resistant to:".
3. Cast resist cold at level 20 — `cold (30%)`.
4. Find a mob with a flag resistance, `switch` into it, `affections` — the element with `(33%)`.
5. Confirm no stray characters lead the first line (the `buf[0]` fix).

- [ ] **Step 8: Format and commit**

```bash
cd src && clang-format -i -style=WebKit utility.cpp utils.h act_info.cpp tests/resistance_tests.cpp
cd .. && git add src/utility.cpp src/utils.h src/act_info.cpp src/tests/resistance_tests.cpp
git commit -m "feat(affections): show resistances and vulnerabilities with their strength"
```

---

### Task 7: Carried-over fixes, completeness check, full verification

**Files:**
- Modify: `src/objsave.cpp` (`Crash_load`)
- Modify: `src/clerics.cpp` (`do_mental`)

**Interfaces:**
- Consumes: everything above.
- Produces: nothing new.

- [ ] **Step 1: Carry the `Crash_load` array fix**

In `src/objsave.cpp`, in `Crash_load`:

```cpp
    struct obj_data* equip_array[20];
```

It was `[11]`. Check `MAX_WEAR` before committing to 20 — the array is indexed by wear position,
and if `MAX_WEAR` exceeds 11 the original could write past the end. Note the finding in the commit
message either way.

- [ ] **Step 2: Carry the illusion save in `do_mental`**

In `src/clerics.cpp`, in the successful-hit branch, replace `tmp = number(0, 6);` with:

```cpp
        const int will_stat = 2;
        if (utils::is_resistant(*victim, RESIST_ILLU)) {
            tmp = number(0, 6);
            if (number(1, 3) > 2) {
                sprintf(buf, "SAVE::ILLU--> atkr: %s, vict: %s", GET_NAME(ch), GET_NAME(victim));
                mudlog_debug_mob_or_player(buf, ch, victim);
                while (tmp == will_stat)
                    tmp = number(0, 6);
            }
        } else {
            tmp = number(0, 6);
        }
```

This is the one place illusion resistance does anything, since no illusion spell deals damage.
Note the source branch passed `PLRSPEC_ILLU` here; `RESIST_ILLU` is the correct id and happens to
share the value.

- [ ] **Step 3: Build once with warnings as errors**

```bash
cd src && sed -i 's/^MYFLAGS = .*/MYFLAGS = -fstrict-aliasing -funsigned-char -Wall -Wextra -Werror/' Makefile
cd .. && scripts/rots-docker.sh shell -c 'cd src && make clean && make all' 2>&1 | tail -40
cd src && git checkout Makefile
```

Fix anything the compiler finds in files this branch touched. Do not chase warnings in untouched
files. **`src/Makefile` must be reverted before committing** — the flags are not part of the PR.

- [ ] **Step 4: Run the full suite against the baseline**

```bash
scripts/rots-docker.sh test 2>&1 | tail -30
```

Compare with the baseline captured on the clean tree before Task 1. The failure set must be
identical apart from the new suites passing.

- [ ] **Step 5: Completeness check against the trial merge**

```bash
git merge --no-commit --no-ff import/resist-live-ah
git diff --name-only --diff-filter=U    # conflicts, expected
git diff HEAD                            # what the merge would have brought
git merge --abort
```

Walk the result. For every hunk the merge would have introduced that this branch does not have,
decide: deliberate (record it in the PR body) or missed (fix it). The known-deliberate omissions
are the `src/Makefile` flags, the legacy text-format 7th field, the unconditional `mudlog`
instrumentation in `db.cpp` and `nanny`, the `%lu`-on-a-struct line, `affect_modify`'s unused
`effect_modifier` parameter, and the `APPLY_RESIST_*` 40–44 defines.

- [ ] **Step 6: Run the live scenario end to end**

Boot with world files, and in one session:
1. Create or load a test character; cast resist fire; `affections` shows `fire (40%)`.
2. Quit. Log back in. `affections` unchanged. **This is the failure that stopped the original work.**
3. Shut the server down, boot it again, log in. Still unchanged.
4. Wear 2069; the item's 30% replaces the cast 40%.
5. Remove it; the slot is empty.
6. Take fire and cold damage in arena 1120 with `debug` on and confirm the numbers.

- [ ] **Step 7: Format and commit**

```bash
cd src && clang-format -i -style=WebKit objsave.cpp clerics.cpp
cd .. && git add src/objsave.cpp src/clerics.cpp
git commit -m "fix(resist): illusion resistance saves against mental, widen Crash_load equip array"
```

---

## Deferred (recorded in the spec, not implemented here)

- Balance numbers on the six `skills[]` rows: minimum level, mana, beats.
- Guildmaster `knowledge[]` entries for 161-166. Today **nothing teaches any of them**: the "ALL SKILLS" guildmaster's array has 161 entries (0-160), so all six sit in its zero tail.
- `SPELL_FIREBALL2` and `SPELL_DRAGONSBREATH` having no `skills[]` rows.
- Re-authoring 6518 and 6531 so their resistances carry a magnitude.
- Elements beyond the six.
