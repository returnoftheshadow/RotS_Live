# Object Skins Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let one item have many looks: a "skin" object has its own vnum and its own text, and takes every stat from a base object.

**Architecture:** Two new fields on `obj_flag_data` mark a prototype as a skin (`skin_base` vnum + `skin_own` bits). A new module `object_skins.cpp` owns every skin rule: copying stats from the base, the boot pass, the family test and the restyle swap. The world-file loader, the object editor, the imm `restyle` command, imm `stat` and the new script action `RESTYLE_OBJ` call into that module. Nothing else in the game changes, because a loaded skin copy is an ordinary object with its own vnum.

**Tech Stack:** C++17 server (32-bit, `src/Makefile` and `src/CMakeLists.txt`), GoogleTest (`ageland_tests`), Docker toolchain (`scripts/rots-docker.sh`).

**Spec:** the "Object skins" doc https://claude.ai/code/artifact/e062cdf2-5af0-4d1f-a111-2f829b4e3708 and the working doc https://claude.ai/artifact/GsAWjwBg6R3SZxxSre4HvQ (decisions 1-23 plus the 2026-10-16 review). Both were decided with the user on 2026-10-16.

## Global Constraints

- Keys can't have skins. A base can't be a key, and a key can't become a base.
- A base is always an ordinary object, never a skin. There are no chains.
- A skin owns its text (`/1`-`/5`). Every other field comes from the base, except **material** and **cost**, which follow the base unless the skin sets its own (`skin_own` bits).
- Making a skin never refreshes owned copies and never bumps a version. Builders bump the base's version by hand.
- A restyle stays within one family: the base and its skins. The new copy takes the target's text, material and cost and keeps everything else (enchants, broken state, timer, loaded_by, owner, contents, location: worn slot, inventory, bag or room).
- Load limits stay per vnum. The game code goes live before any skin file is deployed: an old binary exits on a 7-number level line.
- Messages fit in 78 columns. Logs use house style (type, vnum, line; no prose).
- Format only what you touched: `cd src && clang-format -i -style=WebKit -lines=<a>:<b> <file>`. Never run bare `make format`. `src/objsave.cpp` and `src/color.cpp` have mixed CRLF; keep their endings.
- `structs.h` changes, so run `make clean` before the first server build (`scripts/rots-docker.sh compile` after `docker compose run --rm rots bash -lc 'cd /rots/src && make clean'`).
- New `.cpp` files go into **both** `src/Makefile` `SOURCES` and `src/CMakeLists.txt` (`ROTS_SERVER_SOURCES`; tests go in `ROTS_TEST_SOURCES`).
- Tests: `scripts/rots-docker.sh test --gtest_filter='<Suite>.*'`. The full run has known failures that were there before this work (`AccountManagement.FormatsOutOfRangeSummaryTimestampsAsInvalid`, and a segfault at `InterpreAccountMenu.UnlockSelect...`), so take a baseline before blaming a change.

### File format (decided here, within decision 18)

A skin's level line has **7** numbers: `level rarity material script version base own`. `own` is the bits `SKIN_OWN_COST = 1` and `SKIN_OWN_MATERIAL = 2`. Ordinary objects keep writing 5 numbers, and old 4-number lines still read as version 0. A skin's file still holds full stats, copied from the base when it's saved, as the fallback if the base is missing.

## Review Focus

1. **Restyling a worn item** (a lit light, a weapon, armor with affects): the wearer's stats, light count, weight and encumbrance must be the same before and after. The swap is a pointer swap with no `unequip_char`/`equip_char`, so no "zap" or "too heavy" messages appear. Test in Task 4.
2. **Restyling a container with contents, or a drink container with liquid**: the contents move to the new copy, nothing is extracted, and `obj_index[].number` counts stay correct. Test in Task 4.
3. **A world file whose base is missing, is a key, or is itself a skin**: boot continues, there is one `OBJ ERROR` line per skin, and the skin acts as a plain object. Test in Task 1.
4. **Editing a base while its skins are live**: `implement` on the base updates every skin prototype at once, but not copies already loaded, as today. A skin with its own cost keeps it. Test in Task 3.
5. **A script param literal of 953-955**: these collide with the INT1-3 codes. Treat them as INT1-3, as the editor's `v` kind already does, and say so in the scr_tbl help. Test in Task 6.

---

### Task 1: Skin fields and the `object_skins` module

**Files:**
- Modify: `src/structs.h:438` (after `int version;` in `obj_flag_data`)
- Create: `src/object_skins.h`, `src/object_skins.cpp`
- Modify: `src/Makefile` (SOURCES), `src/CMakeLists.txt` (server + test lists)
- Modify: `src/db.cpp:426` (boot hook after `index_boot(DB_BOOT_OBJ)`)
- Test: `src/tests/object_skins_tests.cpp` (new)

**Interfaces:**
- Produces (in `object_skins.h`):
  - `obj_flag_data` fields: `int skin_base;` (base vnum, 0 = not a skin), `ubyte skin_own;`
  - `constexpr int SKIN_OWN_COST = 1, SKIN_OWN_MATERIAL = 2;`
  - `bool obj_is_skin(const obj_data& proto);`
  - `void copy_skin_stats_from_base(obj_data& skin, const obj_data& base);` copies type_flag, extra_flags, wear_flags, value[0..4], weight, cost_per_day, level, rarity, script_number, version and affected[]. It copies cost unless `SKIN_OWN_COST` is set and material unless `SKIN_OWN_MATERIAL` is set. It never touches text, `skin_base` or `skin_own`.
  - `std::string skin_base_problem(int skin_vnum, int base_vnum);` returns "" if valid. Otherwise one of: `"base obj %d not found"`, `"base obj %d is a key"`, `"base obj %d is a skin of obj %d"`, `"an object can't be its own base"`.
  - `int skin_family_rnum(int rnum);` returns the base's rnum for a valid skin, `rnum` otherwise, and -1 for a negative rnum.
  - `std::vector<int> skin_rnums_of(int base_rnum);` lists the rnums whose `skin_base` equals that base's vnum, in rnum order.
  - `void apply_object_skins();` is the boot pass. For each skin proto it either copies the stats from the base or, if `skin_base_problem` returns text, logs `OBJ ERROR: object #<vnum>: <problem> - loaded as plain object` (mudlog NRM, LEVEL_IMMORT, as `MOB ERROR` at db.cpp:2010) and sets `skin_base = 0, skin_own = 0` on the proto.
  - `void refresh_skins_of_base(int base_rnum);` copies the base's stats into every skin of it.

- [ ] **Step 1: Write the failing tests** in `tests/object_skins_tests.cpp`, using a 4-to-5-proto fake table fixture modelled on `ObjectTableWithVnumZero` (script_command_tests.cpp:389). Vnums: 4512 base sword (ITEM_WEAPON, cost 100, material 4, affected[0] = APPLY_OB +2, version 3), 4513 skin of 4512 (own cost 250), 4514 skin of 4512, 4600 key (ITEM_KEY), 4601 skin of 4600.
  - `ObjectSkins.CopyTakesEveryStatButText`: after the copy, 4514's value[], weight, affected[0], version and cost equal 4512's, and its `short_description` pointer is unchanged.
  - `ObjectSkins.OwnCostAndMaterialSurviveCopy`: 4513 keeps cost 250. Set `SKIN_OWN_MATERIAL` with material 1, and it stays 1.
  - `ObjectSkins.BaseProblems`: the key gives `"base obj 4600 is a key"`; 9999 gives `"base obj 9999 not found"`; base 4513 gives `"base obj 4513 is a skin of obj 4512"`; 4512→4512 gives the own-base text.
  - `ObjectSkins.BootPassDemotesBadSkins`: after `apply_object_skins()`, 4601 has `skin_base == 0` and kept its own file stats; 4513 and 4514 carry 4512's stats.
  - `ObjectSkins.FamilyAndList`: `skin_family_rnum` of 4513 and of 4514 equals 4512's rnum; 4600's family is itself; `skin_rnums_of(rnum 4512)` gives {rnum 4513, rnum 4514}.
  - `ObjectSkins.RefreshSkinsOfBase`: change base weight to 9, call `refresh_skins_of_base`, and both skins have weight 9.
- [ ] **Step 2: Run** `scripts/rots-docker.sh test --gtest_filter='ObjectSkins.*'`. Expect a compile failure (no `object_skins.h`).
- [ ] **Step 3: Implement** the fields, the module and the boot hook. The boot hook in db.cpp, after line 426: `log("Applying object skins."); apply_object_skins();`.
- [ ] **Step 4: Run** the same filter. Expect all PASS. Then run `--gtest_filter='DbLoader.*:ObjectUtils*'` and expect no new failures.
- [ ] **Step 5: Commit** `feat(objects): skin fields and object_skins module`

### Task 2: World-file format (read and write the 7-number line)

**Files:**
- Modify: `src/object_skins.h/.cpp` (line reader)
- Modify: `src/db.cpp:2208-2215` (`load_objects`)
- Modify: `src/shapeobj.cpp:169-170` (`write_object`), `src/shapeobj.cpp:1432-1450` (`load_object`)
- Test: `src/tests/object_skins_tests.cpp`, `src/tests/db_loader_tests.cpp`

**Interfaces:**
- Produces: `int read_object_level_line(FILE* f, int nums[7]);` It skips leading whitespace (including the `\r` left by the previous `\n\r` line), reads the rest of one line, and returns how many integers it parsed (0-7). Missing ones are set to 0.
- Consumes: Task 1 fields.

- [ ] **Step 1: Write failing tests**
  - `ObjectSkins.LevelLineReader`, using a `tmpfile()` containing `"\n\r12 0 4 0 3 4512 1\n\rE\n"`: it returns 7 and nums = {12,0,4,0,3,4512,1}, and the next `fscanf(" %s")` reads `E`. For `"5 0 1 0\n\r#4600\n"` it returns 4, nums[4] is 0, and the next token is `#4600`. For a 5-number line it returns 5.
  - `DbLoader.SkinLevelLineWrittenOnlyForSkins`: `write_object` into a `tmpfile()` gives a level line with 5 numbers for an ordinary object, and `"12 0 4 0 3 4512 1"` for a skin with own cost. Don't call `load_objects`; db_loader_tests.cpp:2341 says only one test may.
- [ ] **Step 2: Run** `--gtest_filter='ObjectSkins.LevelLine*:DbLoader.SkinLevel*'`. Expect FAIL.
- [ ] **Step 3: Implement.** In `load_objects`, replace the 5-number `fscanf` with `read_object_level_line`, keep the "fewer than 5 → version 0" rule, and set `skin_base = nums[5]` and `skin_own = nums[6]`. Make the same change in the editor's `load_object`. `write_object` adds `" %d %d"` only when `skin_base > 0`.
- [ ] **Step 4: Run** the filter plus `DbLoader.*`. Expect PASS. `WorldObjectVersionSurvivesWriteAndLoad...` must still pass.
- [ ] **Step 5: Commit** `feat(objects): skin base and own bits in world files`

### Task 3: Object editor `/23`, the skin rules, display and help

**Files:**
- Modify: `src/shapeobj.cpp`:
  - new `case 23` in `shape_center_obj` near 1035
  - stat cases 9-13, 15-17, 19-22 refuse on a skin; 14 and 18 set an own bit; 9 refuses a key on a base with skins
  - `implement_object` (192-274)
  - `replace_object` before `write_object` at 1734
  - `list_object` (1118-1230), `list_help_obj` (1072-1114)
- Modify: `lib/text/shap_tbl` (new `"OBJ 23"` entry after `"OBJ 22"` at 760-792; add skin notes to `"OBJ 14"` and `"OBJ 18"`)
- Test: `src/tests/db_loader_tests.cpp` (editor tests sit with `ObjectEditorVersionFieldTakesExactlyBOrO` at 2403; copy its harness)

**Interfaces:**
- Consumes: everything in Task 1. The live base proto is `obj_proto[real_object(base)]`.
- Produces: none for later tasks.

Exact behavior and copy (each message is one `send_to_char`, `\n\r`-terminated):
- `/23` prompt: `BASE (vnum of the object this is a skin of, 0 = none)`.
  - `/23 <v>` on success copies the base's stats into the working copy, clears `skin_own`, and prints:
    `This object is now a skin of obj 4512 (a long steel sword).` / `Stats, values, affects and flags now come from 4512.`
    It then adds one line for each of cost and material that differed: `Cost was 90, now follows base (100).` / `Material was 2, now follows base (4).`
  - Refused, with the field unchanged:
    - `There is no obj <v>.`
    - `Keys can't have skins.`
    - `Obj <v> is a skin of obj <b>; use /23 <b>.`
    - `An object can't be its own base.`
    - `Obj <this> has skins (4513, 4514); it can't become a skin.` (the list comes from `skin_rnums_of`)
  - `/23 0` on a skin keeps the current stats, cost and material, sets `skin_base = skin_own = 0`, and prints `This object is no longer a skin; its stats are now its own.`
- A stat edit on a skin (/9-/13, /15-/17, /19-/22, and /49's steps 9, 11, 12, 13 and 15) prints `Obj 4513 is a skin of obj 4512; edit stats on 4512.` and changes nothing.
- `/14` and `/18` on a skin take a number (setting `SKIN_OWN_COST` or `SKIN_OWN_MATERIAL`), or the word `base`, which clears the bit and copies the base's value back: `Cost now follows base (100).`
- `/9` to ITEM_KEY on an object that has skins: `Obj 4512 has skins (4513, 4514); it can't become a key.`
- `implement_object`: if the working copy is a skin with a valid base, copy the base's stats into the live proto after the existing copy. If the live proto is a base, call `refresh_skins_of_base`. Copy `skin_base` and `skin_own` with the other fields.
- `replace_object`: if the working copy is a skin, copy the live base's stats into it just before `write_object`, so the file's fallback stats are current.
- `list_object`: on a skin, the stat lines get the suffix ` (from 4512)`, and there is a line `(23) Skin of: obj 4512 (a long steel sword)` with `[own]` after cost/material when set. On a base, the line is `(23) Skins: 4513, 4514`; otherwise `(23) Skin of: none`. `list_help_obj` adds `23 - base object (makes this a skin)`.

- [ ] **Step 1: Write failing tests**, each driving `shape_center_obj` the way the version test does:
  - `DbLoader.ObjectEditorSetsSkinBase` (stats copied, both message lines, the cost-differs line)
  - `DbLoader.ObjectEditorRefusesKeyBase`
  - `DbLoader.ObjectEditorRefusesSkinAsBase` (message names the real base)
  - `DbLoader.ObjectEditorRefusesStatEditOnSkin` (/13)
  - `DbLoader.ObjectEditorOwnCostThenBase` (/14 250 sets the bit; /14 base clears it, back to 100)
  - `DbLoader.ObjectEditorUnskinKeepsStats` (/23 0)
  - `DbLoader.ObjectEditorRefusesKeyTypeOnBaseWithSkins`
  - `DbLoader.ImplementBaseRefreshesSkinProtos`
- [ ] **Step 2: Run** `--gtest_filter='DbLoader.ObjectEditor*:DbLoader.ImplementBase*'`. Expect FAIL.
- [ ] **Step 3: Implement** the behavior above, plus the shap_tbl entries. The `"OBJ 23"` help explains skins in under 20 lines at 78 columns. It must say: material and cost may be set, `base` resets them; keys can't have skins; and bump the base's version (/22) by hand when players' converted copies need new stats.
- [ ] **Step 4: Run** the filter plus `DbLoader.*`. Expect PASS.
- [ ] **Step 5: Commit** `feat(shaping): /23 skin base in the object editor`

### Task 4: The restyle swap

**Files:**
- Modify: `src/object_skins.h/.cpp`
- Test: `src/tests/object_skins_tests.cpp`

**Interfaces:**
- Produces:
  - `enum class restyle_result { ok, no_such_object, same_object, not_same_family, not_a_prototype };`
  - `restyle_result restyle_object(obj_data* old_obj, int target_rnum, obj_data** new_obj);`
    - `target_rnum < 0` → `no_such_object`; `old_obj->item_number < 0` → `not_a_prototype`; same rnum → `same_object`; different `skin_family_rnum` → `not_same_family`. On every refusal nothing changes and `*new_obj` is left alone.
    - On `ok`:
      - `n = read_object(target_rnum, REAL)`, then `n->obj_flags = old->obj_flags`, with `cost`, `material`, `skin_base` and `skin_own` then taken from the target proto.
      - Copy `affected[]`, `enchants[]`, `loaded_by`, `owner` and `touched`. Set `old->obj_flags.script_info = 0` so the old copy's free can't touch it.
      - Move `contains` to `n` (re-point each child's `in_obj`).
      - Put `n` where `old` was, in the same position: `ch->equipment[slot]`, `ch->carrying` chain, `in_obj->contains` chain, or `world[room].contents` chain. Use a pointer swap in the `next_content` chain, with no `equip_char`/`obj_to_*`, so the character's totals don't change.
      - Clear `old`'s location fields, then `extract_obj(old)`. Set `*new_obj = n`.
- Consumes: Task 1 `skin_family_rnum`.

- [ ] **Step 1: Write failing tests**, with a fixture made from Task 1's table plus the `WornObject` idea (script_command_tests.cpp:243):
  - `ObjectSkins.RestyleInInventoryKeepsPosition`: carrying is [a, sword, b], and after the restyle it is [a, new, b].
  - `ObjectSkins.RestyleWornKeepsSlotAndStats`: worn in WEAR_WIELD; OB, weight and `equipment[WIELD]` point at the new copy; `GET_OB` is unchanged.
  - `ObjectSkins.RestyleKeepsStateTakesCostMaterial`: enchant, ITEM_BROKEN, timer and loaded_by are kept; cost is 250 (4513's own cost).
  - `ObjectSkins.RestyleMovesContents`: a container with 2 items; the new copy contains both; `obj_index[].number` is down 1 for the old vnum and up 1 for the new.
  - `ObjectSkins.RestyleRefusals`: each enum value, with the object untouched.
- [ ] **Step 2: Run** `--gtest_filter='ObjectSkins.Restyle*'`. Expect FAIL.
- [ ] **Step 3: Implement** `restyle_object`.
- [ ] **Step 4: Run** `--gtest_filter='ObjectSkins.*'`. Expect PASS.
- [ ] **Step 5: Commit** `feat(objects): restyle swap within a skin family`

### Task 5: Imm `restyle` command and `stat` lines

**Files:**
- Modify: `src/act_wiz.cpp` (new `ACMD(do_restyle)` after `do_wizstat`; `do_stat_object` adds a line after the VNum line at 581)
- Modify: `src/interpre.cpp` (declare at ~149; `"restyle", // 255` before `"\n"` at 574; `COMMANDO(255, POSITION_DEAD, do_restyle, LEVEL_GRGOD, FALSE, 0, FULL_TARGET, FULL_TARGET, 0);` after 2275)
- Modify: `lib/text/wizh_tbl` (new `RESTYLE` entry, in the `VAULT` format)
- Test: `src/tests/act_wiz_tests.cpp`

**Interfaces:**
- Consumes: `restyle_object`, `restyle_result`, `skin_rnums_of`, `obj_is_skin` (Tasks 1 and 4).

Behavior:
- Syntax: `restyle <object> <vnum>` searches the imm's inventory, then equipment. `restyle <player> <object> <vnum>` uses `get_player_vis` and searches that player's inventory, then equipment, with `get_object_in_equip_vis` (handler.cpp:2293).
- Replies:
  - success: `Restyled obj 4512 (a long steel sword) to obj 4513 (a slender elven blade).`
  - `Restyle what to which vnum?`
  - `No such player.`
  - `You don't see that.`
  - `There is no obj 4600.`
  - `It is already obj 4513.`
  - `Obj 4600 is not a look of obj 4512.`
- The player isn't told.
- Log: `(GC) restyle: obj 4512 -> 4513, player idnum 1234, by <imm name>`, via `mudlog(buf, BRF, MAX(LEVEL_GOD, GET_INVIS_LEV(ch)), TRUE)`. The idnum is the owner (the imm's own when no player is given).
- `stat` on an object shows `Skin of: obj 4512 (a long steel sword)` on a skin, `Skins: 4513, 4514` on a base, and nothing for an ordinary object.

- [ ] **Step 1: Write failing tests**:
  - `ActWiz.RestyleOwnItem`
  - `ActWiz.RestylePlayerWornItem`
  - `ActWiz.RestyleRefusesOutOfFamily` (the reply text, and the item unchanged)
  - `ActWiz.StatShowsSkinLines`
  
  Capture output the way the existing act_wiz_tests do.
- [ ] **Step 2: Run** `--gtest_filter='ActWiz.Restyle*:ActWiz.StatShowsSkin*'`. Expect FAIL.
- [ ] **Step 3: Implement** the command, the registration, the help and the stat lines.
- [ ] **Step 4: Run** the same filter. Expect PASS.
- [ ] **Step 5: Commit** `feat(wiz): restyle command and skin lines in stat`

### Task 6: Script action `RESTYLE_OBJ` (79)

**Files:**
- Modify: `src/script.h:98` (`#define SCRIPT_RESTYLE_OBJ 79`)
- Modify: `src/script.cpp`:
  - new case after EXTRACT_OBJ at 1457
  - `script_cmd_name` (91-119) gains `"RESTYLE_OBJ"`
  - `check_script_vnums` (235-259) checks a literal target vnum
- Modify: `src/shapescript.cpp` (the four entries):
  - `format_command` (~770): `[%d] SYS RESTYLE_OBJ      Restyle: %s to %s (%s)`
  - the /4 prompt (~2010): `SCRIPTPARAMS("RESTYLE_OBJ: object, new look's vnum  e.g. ob1 4513", 0, "vv")`
  - `get_command` (~2780)
  - `script_type_groups` "Create:" group (1377)
- Modify: `lib/text/scr_tbl` (the list at 101, plus an entry modelled on EXTRACT_OBJ at 867), `docs/shape_script.md:212`
- Test: `src/tests/script_command_tests.cpp`

**Interfaces:**
- Consumes: `restyle_object`, `restyle_result`.
- Runtime:
  - `param[0]` is OB1-3, resolved with `get_obj_param`.
  - `param[1]` is INT1-3 (953-955, read through `get_int_param`) or a literal vnum.
  - On `ok`, `assign_obj_param(param[0], info, new_obj)`.
  - On any refusal, the line does nothing, and the log gets (mudlog NRM, LEVEL_AREAGOD, the format at script.cpp:130):
    `SCRIPT ERROR: script #812, line 14 (RESTYLE_OBJ): obj 4512 -> 4600 not same family`
    The other reasons are `obj 4600 not found`, `no object`, `already obj 4513`. The script carries on.

- [ ] **Step 1: Write failing tests**, using the `Script` builder (script_command_tests.cpp:89):
  - `ScriptRestyle.SwapsAndRepointsOb1` (OB1 is the new copy, worn in the same slot)
  - `ScriptRestyle.TargetFromInt1`
  - `ScriptRestyle.OutOfFamilyDoesNothingAndContinues` (the next line, SET_INT_VALUE, still runs)
  - `ScriptRestyle.NullObjectDoesNothing`
- [ ] **Step 2: Run** `--gtest_filter='ScriptRestyle.*'`. Expect FAIL.
- [ ] **Step 3: Implement** the case, the editor entries and the help.
- [ ] **Step 4: Run** `--gtest_filter='ScriptRestyle.*:ScriptCommand*'`. Expect PASS.
- [ ] **Step 5: Commit** `feat(scripts): RESTYLE_OBJ action`

### Task 7: Docs and in-game check

**Files:**
- Create: `docs/systems/object-skins.md` (the design as built: fields, file line, editor rules, restyle, logs)
- Modify: `docs/data-formats/world-files.md` (the level line: 5 numbers, or 7 for a skin)
- Modify: `docs/README.md` (status row)

- [ ] **Step 1: Write the docs.** Cite `file:line` in the same style as the other `docs/systems/*.md` pages.
- [ ] **Step 2: Clean build.** `make clean`, then `scripts/rots-docker.sh compile`. Expect exit 0.
- [ ] **Step 3: Run the unit suite** (`ObjectSkins.*:DbLoader.*:ActWiz.*:Script*`) and the full run. Compare the full run against the known baseline failures.
- [ ] **Step 4: In-game check on port 4071** (local world, test imm with `invis 100`; follow the test recipe memory):
  1. In shaping, make two skins of a basic weapon, with `/23`, own text and an own cost on one.
  2. `implement`, `save`, reboot.
  3. `load obj` each skin, then `stat` and `identify`.
  4. `restyle` a worn copy and a bag-held copy.
  5. Run a one-line RESTYLE_OBJ script.
  6. Hand-edit a skin's base to a missing vnum and boot: expect the `OBJ ERROR` line, and the skin loads plain.
- [ ] **Step 5: Commit** `docs(objects): object skins`

Outside this repo, for the user to schedule: the external world-file script and `obj2html` must learn the 7-number level line before any skin file ships.
