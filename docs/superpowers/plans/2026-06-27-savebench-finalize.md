# savebench Finalize A/B Test — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add an implementor-only in-game command, `savebench`, that proves a new `unlink()`+`rename()` player-file finalizer is byte-equivalent to the legacy `system("rm")`/`system("cp")` one and profiles both — without changing the production save path.

**Architecture:** Extract the serialization and the legacy finalize logic out of `save_player()` into reusable free functions in `db.cpp`/`db.h`; add a new `rename`-based finalize function beside them (not wired into production); cover the two finalizers with an offline gtest; then build the `savebench` command on top. Production `save_player` continues to call the legacy finalizer until the new one is verified locally and on the test server.

**Tech Stack:** C++17 (`-std=c++1z`), 32-bit Linux (`-m32`, i386 Docker), POSIX file APIs (`dirent.h`, `unistd.h`, `sys/stat.h`), GoogleTest (`-lgtest -lgtest_main`).

## Global Constraints

- **Language/build:** C++17 (`-std=c++1z`), 32-bit (`-m32`), Linux-only (i386 Docker). `bool`, `snprintf`, `std::string` are all available.
- **Formatting:** repo `.clang-format` — `BasedOnStyle: LLVM`, `IndentWidth: 4`, `ColumnLimit: 100`. This overrides any global Allman preference. A `PostToolUse` hook auto-runs `clang-format -i` on each edited `.cpp`/`.h`, so write reasonable code and let the hook normalize it.
- **Permission:** the `savebench` command is `LEVEL_IMPL` (= 100, `src/structs.h:45`) only.
- **Do NOT switch production:** `save_player()` must keep calling `finalize_player_file_legacy()`. `finalize_player_file_rename()` is reachable only from `savebench` and the gtest. Flipping production over is explicitly out of scope here.
- **Guardrails for the command:** never write into a real bucket dir (`players/A-E/`…`players/ZZZ/`), never run the glob against a real player path, never call `save_char`/`save_player`, never mutate `player_table[].ch_file`. All test I/O lives in throwaway paths under `players/` and is removed on every exit path.
- **Spec:** `docs/superpowers/specs/2026-06-27-savebench-finalize-ab-test-design.md`.

---

## File Structure

| File | Change | Responsibility |
| --- | --- | --- |
| `src/db.h` | Modify (after line 103) | Declare `write_player_text`, `finalize_player_file_legacy`, `finalize_player_file_rename`. |
| `src/db.cpp` | Modify (add functions before `save_player` ~2302; refactor `save_player` body 2357-2472) | Implement the three helpers; make `save_player` call the extracted serializer + legacy finalizer (behavior-preserving). |
| `src/tests/player_finalize_tests.cpp` | Create | gtest proving byte-equivalence + "exactly one file remains". |
| `src/tests/Makefile` | Modify (line 167-168, `SRCS`) | Add the new test source to the test build. |
| `src/interpre.cpp` | Modify (proto ~214, `command[]` ~547, `COMMANDO` ~2222) | Register the `savebench` command at index 249, level 100. |
| `src/act_wiz.cpp` | Modify (add 2 includes; add handler + static helpers) | Implement `do_savebench`. |

No changes to `src/Makefile` or `src/CMakeLists.txt` are needed: `db.o` and `act_wiz.o` are already built and linked; CMake auto-globs `src/*.cpp` (`src/CMakeLists.txt:13`).

---

## Task 1: New finalize functions + offline gtest

Create both finalize functions in `db.cpp` (not yet wired into `save_player`) and an offline gtest that proves they produce identical bytes and that the new one leaves exactly one file (the glob/stale-file equivalence — the real bug surface).

**Files:**
- Modify: `src/db.h` (after line 103)
- Modify: `src/db.cpp` (add functions just before `void save_player(...)` at line 2302)
- Create: `src/tests/player_finalize_tests.cpp`
- Modify: `src/tests/Makefile` (`SRCS`, line 167-168)

**Interfaces:**
- Produces:
  - `bool finalize_player_file_legacy(const char *scratch_path, const char *base_path, const char *versioned_path);`
  - `bool finalize_player_file_rename(const char *scratch_path, const char *dir_path, const char *base_name, const char *versioned_path);`
- Consumes: nothing from earlier tasks (POSIX + libc only).

- [ ] **Step 1: Declare the two finalize functions in `db.h`**

In `src/db.h`, immediately after line 103 (`void save_char(struct char_data *, int, int);`), add:

```c
/* Player-file finalize helpers. See
 * docs/superpowers/specs/2026-06-27-savebench-finalize-ab-test-design.md.
 * Both move a fully-written scratch file into its versioned destination; they
 * MUST produce byte-identical results. legacy uses system("rm")/system("cp");
 * rename uses a portable readdir/unlink glob then rename(). */
bool finalize_player_file_legacy(const char *scratch_path, const char *base_path,
                                 const char *versioned_path);
bool finalize_player_file_rename(const char *scratch_path, const char *dir_path,
                                 const char *base_name, const char *versioned_path);
```

- [ ] **Step 2: Write the failing test**

Create `src/tests/player_finalize_tests.cpp`:

```cpp
#include "../db.h"
#include <gtest/gtest.h>

#include <dirent.h>
#include <stdio.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace {

void write_file(const char *path, const char *content) {
    FILE *f = fopen(path, "w");
    ASSERT_NE(f, nullptr);
    fputs(content, f);
    fclose(f);
}

std::string read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        return std::string("<<missing:") + path + ">>";
    }
    std::string out;
    char buf[1024];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        out.append(buf, n);
    }
    fclose(f);
    return out;
}

int count_files(const char *dir) {
    DIR *d = opendir(dir);
    if (!d) {
        return -1;
    }
    int count = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] != '.') {
            count++;
        }
    }
    closedir(d);
    return count;
}

} // namespace

// Both finalizers must (1) yield byte-identical output and (2) leave exactly one
// file in their directory (the stale, differently-suffixed seed must be removed).
TEST(PlayerFinalize, ByteIdenticalAndSingleFile) {
    const char *legacy_dir = "pf_test_legacy";
    const char *new_dir = "pf_test_new";
    mkdir(legacy_dir, 0775);
    mkdir(new_dir, 0775);

    // Stale versioned files (suffix ".stale", same "probe." prefix) that the glob must delete.
    write_file("pf_test_legacy/probe.stale", "OLD");
    write_file("pf_test_new/probe.stale", "OLD");

    // Each finalizer gets its own scratch (rename consumes its source); scratches live
    // OUTSIDE the target dirs, mirroring production's players/temp vs the bucket dir.
    write_file("pf_test_legacy_scratch", "PLAYER-BYTES-V1\n");
    write_file("pf_test_new_scratch", "PLAYER-BYTES-V1\n");

    bool ok_legacy = finalize_player_file_legacy(
        "pf_test_legacy_scratch", "pf_test_legacy/probe", "pf_test_legacy/probe.50.1.123.0.0");
    bool ok_new = finalize_player_file_rename("pf_test_new_scratch", "pf_test_new", "probe",
                                              "pf_test_new/probe.50.1.123.0.0");

    EXPECT_TRUE(ok_legacy);
    EXPECT_TRUE(ok_new);

    EXPECT_EQ(read_file("pf_test_legacy/probe.50.1.123.0.0"),
              read_file("pf_test_new/probe.50.1.123.0.0"));

    EXPECT_EQ(count_files(legacy_dir), 1);
    EXPECT_EQ(count_files(new_dir), 1);

    // Cleanup.
    unlink("pf_test_legacy/probe.50.1.123.0.0");
    unlink("pf_test_new/probe.50.1.123.0.0");
    unlink("pf_test_legacy_scratch"); // cp left this; rename already consumed the new scratch.
    rmdir(legacy_dir);
    rmdir(new_dir);
}
```

- [ ] **Step 3: Register the test source in the test Makefile**

In `src/tests/Makefile`, change the `SRCS` list (lines 167-168) from:

```make
SRCS = CharPlayerDataBuilder.h CharPlayerDataBuilder.cpp ObjFlagDataBuilder.h ObjFlagDataBuilder.cpp \
 	   obj_flag_data_tests.cpp gtest_main.cpp
```

to:

```make
SRCS = CharPlayerDataBuilder.h CharPlayerDataBuilder.cpp ObjFlagDataBuilder.h ObjFlagDataBuilder.cpp \
 	   obj_flag_data_tests.cpp player_finalize_tests.cpp gtest_main.cpp
```

- [ ] **Step 4: Run the test to verify it fails (link error — functions undefined)**

The MUD object files must exist first — build the MUD with `cd src && make` (default target `ageland` → `bin/ageland`; on the i386 Docker setup, run that `make` inside the container). Then:

```bash
cd src/tests && make tests
```

Expected: **link failure** — `undefined reference to 'finalize_player_file_legacy'` and `finalize_player_file_rename` (declared in `db.h`, not yet implemented in `db.cpp`).

- [ ] **Step 5: Implement the two finalize functions in `db.cpp`**

In `src/db.cpp`, just before `void save_player(struct char_data *ch, int load_room, int index_pos) {` (line 2302), add:

```cpp
// Legacy finalize: shell out to rm (glob) + cp, exactly as save_player did historically.
// Returns true if both system() calls were able to spawn (return code != -1).
bool finalize_player_file_legacy(const char *scratch_path, const char *base_path,
                                 const char *versioned_path) {
    char command[300];

    snprintf(command, sizeof(command), "rm %s.*", base_path);
    int rc_rm = system(command);
    snprintf(command, sizeof(command), "cp %s %s", scratch_path, versioned_path);
    int rc_cp = system(command);

    return (rc_rm != -1) && (rc_cp != -1);
}

// New finalize: reproduce the legacy "rm <base>.*" glob with a portable directory scan
// (unlink every entry whose name starts with base_name + '.'), then atomically move the
// scratch into place with rename(). The dot anchor ensures "bob." never matches "bobby.".
bool finalize_player_file_rename(const char *scratch_path, const char *dir_path,
                                 const char *base_name, const char *versioned_path) {
    char prefix[256];
    snprintf(prefix, sizeof(prefix), "%s.", base_name);
    size_t prefix_len = strlen(prefix);

    DIR *dir = opendir(dir_path);
    if (dir != NULL) {
        struct dirent *entry;
        char victim[512];
        while ((entry = readdir(dir)) != NULL) {
            if (strncmp(entry->d_name, prefix, prefix_len) == 0) {
                snprintf(victim, sizeof(victim), "%s/%s", dir_path, entry->d_name);
                unlink(victim);
            }
        }
        closedir(dir);
    }

    return rename(scratch_path, versioned_path) == 0;
}
```

(`<dirent.h>` and `<stdio.h>` are already included at `db.cpp:5-6`; `unlink`/`rename` resolve via `platdef.h`/`<stdio.h>`. No new includes needed.)

- [ ] **Step 6: Run the test to verify it passes**

```bash
cd src/tests && make tests && ../../bin/tests --gtest_filter='PlayerFinalize.*'
```

Expected: `[  PASSED  ] 1 test.` — byte-identical output and exactly one file in each dir.

- [ ] **Step 7: Commit**

```bash
git add src/db.h src/db.cpp src/tests/player_finalize_tests.cpp src/tests/Makefile
git commit -m "Add legacy + rename player-file finalizers with equivalence gtest"
```

---

## Task 2: Refactor `save_player` to use the extracted serializer + legacy finalizer

Behavior-preserving refactor: pull the serialization block out of `save_player` into `write_player_text`, and replace the inline `rm`/`cp` finalize with a call to `finalize_player_file_legacy`. Production still produces byte-for-byte the same files.

**Files:**
- Modify: `src/db.h` (add `write_player_text` declaration after line 103, with the finalize decls from Task 1)
- Modify: `src/db.cpp` (add `write_player_text` before `save_player`; rewrite `save_player` body lines 2357-2472)

**Interfaces:**
- Produces: `void write_player_text(struct char_data *ch, int load_room, const char *scratch_path);`
- Consumes: `finalize_player_file_legacy` (Task 1).

- [ ] **Step 1: Declare `write_player_text` in `db.h`**

In `src/db.h`, directly above the `finalize_player_file_legacy` declaration added in Task 1, add:

```c
// Serialize one PC to scratch_path (the fopen + char_to_store + fprintf block).
void write_player_text(struct char_data *ch, int load_room, const char *scratch_path);
```

- [ ] **Step 2: Implement `write_player_text` in `db.cpp`**

In `src/db.cpp`, just above `finalize_player_file_legacy` (added in Task 1), add a function that contains exactly today's serialization block (current `db.cpp:2357-2462`), parameterized on `scratch_path` instead of the hard-coded `"players/temp"`:

```cpp
// Serialize ch into scratch_path as the player text record. This is the exact block
// that used to live inline in save_player (fopen + char_to_store + the fprintf list +
// fclose); only the destination path is now a parameter. Uses the file-global pwdcrypt
// (db.cpp:192). No-op (no crash) if the scratch file cannot be opened.
void write_player_text(struct char_data *ch, int load_room, const char *scratch_path) {
    FILE *pf = fopen(scratch_path, "w");
    if (pf == NULL) {
        return;
    }

    struct char_file_u chd;
    int tmp;

    char_to_store(ch, &chd);
    strcpy(chd.pwd, ch->desc->pwd);
    strncpy(chd.host, ch->desc->host, HOST_LEN);
    if (!PLR_FLAGGED(ch, PLR_LOADROOM)) {
        chd.specials2.load_room = load_room;
    }

    fprintf(pf, "#player\n"); // so we can have other #sections later...
    fprintf(pf, "version     %d\n", SAVE_VERSION);
    fprintf(pf, "name        %s\n", chd.name);
    fprintf(pf, "sex         %d\n", chd.sex);
    fprintf(pf, "prof        %d\n", chd.prof);
    fprintf(pf, "race        %d\n", chd.race);
    fprintf(pf, "bodytype    %d\n", chd.bodytype);
    fprintf(pf, "level       %d\n", chd.level);
    fprintf(pf, "language    %d\n", chd.language);
    fprintf(pf, "birth       %ld\n", chd.birth);
    fprintf(pf, "played      %d\n", chd.played);
    fprintf(pf, "weight      %d\n", chd.weight);
    fprintf(pf, "height      %d\n", chd.height);
    fprintf(pf, "title       %s\n", chd.title);
    fprintf(pf, "hometown    %d\n", chd.hometown);
    fprintf(pf, "description \n%s~\n", chd.description);
    fprintf(pf, "last_logon  %ld\n", chd.last_logon);
    memcpy(pwdcrypt, chd.pwd, MAX_PWD_LENGTH);
    encrypt_line((unsigned char *)pwdcrypt, MAX_PWD_LENGTH);
    fprintf(pf, "password    %s\n", pwdcrypt);
    fprintf(pf, "host        %s\n", chd.host);
    fprintf(pf, "idnum       %ld\n", chd.specials2.idnum);
    fprintf(pf, "load_room   %d\n", chd.specials2.load_room);
    fprintf(pf, "sp_to_learn %d\n", chd.specials2.spells_to_learn);
    fprintf(pf, "alignment   %d\n", chd.specials2.alignment);
    fprintf(pf, "act         %ld\n", chd.specials2.act);
    fprintf(pf, "pref        %ld\n", chd.specials2.pref);
    fprintf(pf, "wimpy       %d\n", chd.specials2.wimp_level);
    fprintf(pf, "freeze_lvl  %d\n", chd.specials2.freeze_level);
    fprintf(pf, "bad_pws     %d\n", chd.specials2.bad_pws);
    fprintf(pf, "conditions0 %d\n", chd.specials2.conditions[0]);
    fprintf(pf, "conditions1 %d\n", chd.specials2.conditions[1]);
    fprintf(pf, "conditions2 %d\n", chd.specials2.conditions[2]);
    fprintf(pf, "mini_lvl    %d\n", chd.specials2.mini_level);
    fprintf(pf, "morale      %d\n", chd.specials2.morale);
    fprintf(pf, "owner       %d\n", chd.specials2.owner);
    fprintf(pf, "rerolls     %d\n", chd.specials2.rerolls);
    fprintf(pf, "max_mini_lv %d\n", chd.specials2.max_mini_level);
    fprintf(pf, "perception  %d\n", chd.specials2.perception);
    fprintf(pf, "rp_flag     %d\n", chd.specials2.rp_flag);
    fprintf(pf, "retiredon   %d\n", chd.specials2.retiredon);
    fprintf(pf, "ob          %d\n", chd.points.OB);
    fprintf(pf, "damage      %d\n", chd.points.damage);
    fprintf(pf, "ENE_regen   %d\n", chd.points.ENE_regen);
    fprintf(pf, "parry       %d\n", chd.points.parry);
    fprintf(pf, "dodge       %d\n", chd.points.dodge);
    fprintf(pf, "gold        %d\n", chd.points.gold);
    fprintf(pf, "exp         %d\n", chd.points.exp);
    fprintf(pf, "encumb      %d\n", chd.points.encumb);
    fprintf(pf, "spec        %d\n", chd.profs.specialization);

    for (tmp = 0; tmp < MAX_COLOR_FIELDS; ++tmp)
        if (chd.profs.colors[tmp] != CNRM)
            fprintf(pf, "color       %d %d\n", tmp, chd.profs.colors[tmp]);

    for (tmp = 0; tmp < MAX_TOUNGE; tmp++)
        fprintf(pf, "talks       %d %d\n", tmp, chd.talks[tmp]);

    for (tmp = 0; tmp < MAX_SKILLS; tmp++)
        if (chd.skills[tmp])
            fprintf(pf, "skills      %d %d\n", tmp, chd.skills[tmp]);

    for (tmp = 0; tmp < MAX_AFFECT; tmp++)
        if (chd.affected[tmp].duration != 0) {
            fprintf(pf, "affect      %d %d %d %d %d %ld\n", tmp, chd.affected[tmp].type,
                    chd.affected[tmp].duration, chd.affected[tmp].modifier,
                    chd.affected[tmp].location, chd.affected[tmp].bitvector);
        }

    for (tmp = 0; tmp < MAX_BODYPARTS; tmp++)
        fprintf(pf, "bodyparts   %d %d\n", tmp, chd.points.bodypart_hit[tmp]);

    fprintf(pf, "tmpstats    %d %d %d %d %d %d\n", chd.tmpabilities.str, chd.tmpabilities.lea,
            chd.tmpabilities.intel, chd.tmpabilities.wil, chd.tmpabilities.dex,
            chd.tmpabilities.con);

    fprintf(pf, "tmpabil     %d %d %d %d\n", chd.tmpabilities.hit, chd.tmpabilities.mana,
            chd.tmpabilities.move, chd.points.spirit);

    fprintf(pf, "permstats    %d %d %d %d %d %d\n", chd.constabilities.str,
            chd.constabilities.lea, chd.constabilities.intel, chd.constabilities.wil,
            chd.constabilities.dex, chd.constabilities.con);

    fprintf(pf, "permabil     %d %d %d %d\n", chd.constabilities.hit, chd.constabilities.mana,
            chd.constabilities.move, 0);

    for (tmp = 0; tmp < MAX_PROFS + 1; tmp++)
        fprintf(pf, "prof_coef   %d %d\n", tmp, chd.profs.prof_coof[tmp]);

    for (tmp = 0; tmp < MAX_PROFS + 1; tmp++)
        fprintf(pf, "prof_level  %d %d\n", tmp, chd.profs.prof_level[tmp]);

    for (tmp = 0; tmp < MAX_PROFS + 1; tmp++)
        fprintf(pf, "prof_exp    %d %ld\n", tmp, chd.profs.prof_exp[tmp]);

    fprintf(pf, "end\n");
    fclose(pf);
}
```

- [ ] **Step 3: Rewrite the body of `save_player` to call the extracted functions**

In `src/db.cpp`, replace the block from line 2357 (`pf = fopen("players/temp", "w");`) through line 2472 (`sprintf((player_table + index_pos)->ch_file, "%s", playerfname);`) with:

```cpp
    write_player_text(ch, load_room, "players/temp");

    char versioned[120];
    snprintf(versioned, sizeof(versioned), "%s.%d.%d.%d.%ld.%ld", playerfname,
             (player_table + index_pos)->level, (player_table + index_pos)->race,
             (player_table + index_pos)->idnum, (long)(player_table + index_pos)->log_time,
             (player_table + index_pos)->flags);
    finalize_player_file_legacy("players/temp", playerfname, versioned);
    sprintf((player_table + index_pos)->ch_file, "%s", versioned);
```

Then remove the now-unused locals from the top of `save_player` (lines 2304-2308): delete `char temp[255];`, `FILE *pf = NULL;`, `struct char_file_u chd;`, and `int tmp;`. Keep `char name[255];`, `char *tmpchar;`, and `char playerfname[100];`. (The bucket switch at 2315-2355 that fills `playerfname` is unchanged.)

- [ ] **Step 4: Build the MUD and confirm it compiles**

Build with `cd src && make` (the same toolchain used for the test server; run inside the i386 Docker container if that's your setup). Expected: clean compile, no warnings about unused `temp`/`pf`/`chd`/`tmp` in `save_player`.

- [ ] **Step 5: Re-run the Task 1 gtest (still green)**

```bash
cd src/tests && make tests && ../../bin/tests --gtest_filter='PlayerFinalize.*'
```

Expected: `[  PASSED  ] 1 test.` (the extracted `finalize_player_file_legacy` that `save_player` now uses is the same one under test).

- [ ] **Step 6: Manual smoke test — a normal save still works**

Run the MUD locally. Log in a character, change something trivial (e.g. pick up an item or change a setting), trigger a save (the `save` command, or quit), and confirm: the player file under `players/<BUCKET>/<name>.<...>` is updated normally and the character loads back correctly on next login. Expected: identical behavior to before the refactor.

- [ ] **Step 7: Commit**

```bash
git add src/db.h src/db.cpp
git commit -m "Refactor save_player to use write_player_text + finalize_player_file_legacy"
```

---

## Task 3: `savebench` implementor command

Add the level-100 `savebench` command that serializes the invoking character once, runs both finalizers into throwaway dirs, proves byte-equivalence + exactly-one-file, profiles min/avg/max µs per call, and cleans up.

**Files:**
- Modify: `src/interpre.cpp` (prototype ~214, `command[]` ~547, `COMMANDO` ~2222)
- Modify: `src/act_wiz.cpp` (add includes; add static helpers + `do_savebench`)

**Interfaces:**
- Consumes: `write_player_text` (Task 2), `finalize_player_file_legacy` + `finalize_player_file_rename` (Task 1); `send_to_char` (`comm.h:20`); global `buf`; `GET_LEVEL`/`GET_RACE`/`GET_IDNUM`/`IS_NPC` (`utils.h`).
- Produces: the `savebench` command (no symbols consumed by later tasks).

- [ ] **Step 1: Forward-declare the handler in `interpre.cpp`**

In `src/interpre.cpp`, in the `ACMD(...)` prototype block, directly after `ACMD(do_show);` (line 214), add:

```c
ACMD(do_savebench);
```

- [ ] **Step 2: Add the command name to the `command[]` table**

In `src/interpre.cpp`, in the `command[]` array, after `"mob2csv",` (line 547) and before the `"\n"` terminator (line 548), add `"savebench",` so it becomes command index 249:

```c
    "renounce",
    "mob2csv",
    "savebench",
    "\n"
};
```

- [ ] **Step 3: Register the command with `COMMANDO` (level 100)**

In `src/interpre.cpp`, immediately after the `do_mob_csv_extract` registration (the last `COMMANDO`, lines 2221-2222) and before the closing `}` on line 2223, add:

```c
    COMMANDO(249, POSITION_DEAD, do_savebench, LEVEL_IMPL, FALSE, 0, FULL_TARGET, FULL_TARGET,
        0);
```

- [ ] **Step 4: Add the required POSIX includes to `act_wiz.cpp`**

In `src/act_wiz.cpp`, after the existing `#include <string.h>` (line 15), add the headers `do_savebench` needs for `mkdir`, `opendir`/`readdir`, and `struct timeval` (`unistd.h`/`sys/time.h` arrive via `platdef.h`, already included):

```c
#include <dirent.h>
#include <sys/stat.h>
```

- [ ] **Step 5: Add the static helpers + `do_savebench` handler to `act_wiz.cpp`**

In `src/act_wiz.cpp`, near the other `ACMD` handlers (e.g. after `do_show` ~line 2389), add:

```cpp
// --- savebench helpers (diagnostic command; see specs/2026-06-27-savebench-finalize-...) ---

// Byte-for-byte copy src -> dst. Returns true on success.
static bool sb_copy_file(const char *src, const char *dst) {
    FILE *in = fopen(src, "rb");
    if (!in) {
        return false;
    }
    FILE *out = fopen(dst, "wb");
    if (!out) {
        fclose(in);
        return false;
    }
    char chunk[4096];
    size_t n;
    bool ok = true;
    while ((n = fread(chunk, 1, sizeof(chunk), in)) > 0) {
        if (fwrite(chunk, 1, n, out) != n) {
            ok = false;
            break;
        }
    }
    fclose(in);
    fclose(out);
    return ok;
}

// True if both files exist and have identical contents.
static bool sb_files_identical(const char *a, const char *b) {
    FILE *fa = fopen(a, "rb");
    FILE *fb = fopen(b, "rb");
    if (!fa || !fb) {
        if (fa) {
            fclose(fa);
        }
        if (fb) {
            fclose(fb);
        }
        return false;
    }
    bool ok = true;
    int ca, cb;
    do {
        ca = fgetc(fa);
        cb = fgetc(fb);
        if (ca != cb) {
            ok = false;
            break;
        }
    } while (ca != EOF);
    fclose(fa);
    fclose(fb);
    return ok;
}

// Count non-dot entries in dir (-1 if it cannot be opened).
static int sb_count_files(const char *dir) {
    DIR *d = opendir(dir);
    if (!d) {
        return -1;
    }
    int count = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] != '.') {
            count++;
        }
    }
    closedir(d);
    return count;
}

// Remove every non-dot entry in dir, then rmdir it.
static void sb_remove_dir(const char *dir) {
    DIR *d = opendir(dir);
    if (d) {
        struct dirent *e;
        char path[512];
        while ((e = readdir(d)) != NULL) {
            if (e->d_name[0] == '.') {
                continue;
            }
            snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
            unlink(path);
        }
        closedir(d);
    }
    rmdir(dir);
}

// Microseconds elapsed from t0 to t1.
static long sb_usec_diff(const struct timeval *t0, const struct timeval *t1) {
    return (long)(t1->tv_sec - t0->tv_sec) * 1000000L + (t1->tv_usec - t0->tv_usec);
}

ACMD(do_savebench) {
    if (IS_NPC(ch) || !ch->desc) {
        send_to_char("savebench is not available.\n\r", ch);
        return;
    }

    int iterations = 100;
    while (*argument == ' ') {
        argument++;
    }
    if (*argument) {
        int n = atoi(argument);
        if (n > 0) {
            iterations = n;
        }
    }
    if (iterations < 1) {
        iterations = 1;
    }
    if (iterations > 10000) {
        iterations = 10000;
    }

    // All paths under players/ so rename() stays same-filesystem. base_name is synthetic
    // ("probe") and dot-anchored; it can never match a real player's files.
    const char *legacy_dir = "players/SAVEBENCH_LEGACY";
    const char *new_dir = "players/SAVEBENCH_NEW";
    const char *master = "players/savebench_master";
    const char *legacy_scratch = "players/savebench_legacy_scratch";
    const char *new_scratch = "players/savebench_new_scratch";
    const char *base_name = "probe";

    char legacy_base[128], legacy_versioned[200], new_versioned[200];
    char stale_legacy[160], stale_new[160];
    snprintf(legacy_base, sizeof(legacy_base), "%s/%s", legacy_dir, base_name);
    snprintf(legacy_versioned, sizeof(legacy_versioned), "%s.%d.%d.%ld.0.0", legacy_base,
             (int)GET_LEVEL(ch), (int)GET_RACE(ch), (long)GET_IDNUM(ch));
    snprintf(new_versioned, sizeof(new_versioned), "%s/%s.%d.%d.%ld.0.0", new_dir, base_name,
             (int)GET_LEVEL(ch), (int)GET_RACE(ch), (long)GET_IDNUM(ch));
    snprintf(stale_legacy, sizeof(stale_legacy), "%s/%s.stale", legacy_dir, base_name);
    snprintf(stale_new, sizeof(stale_new), "%s/%s.stale", new_dir, base_name);

    mkdir(legacy_dir, 0775);
    mkdir(new_dir, 0775);

    // Serialize THIS character once; copy those exact bytes to each finalizer so the
    // equivalence comparison is immune to any time-based field drift between serializations.
    write_player_text(ch, 0, master);

    // ---- Equivalence check ----
    sb_copy_file(master, stale_legacy); // stale, differently-suffixed seed the glob must remove
    sb_copy_file(master, stale_new);
    sb_copy_file(master, legacy_scratch);
    sb_copy_file(master, new_scratch);

    finalize_player_file_legacy(legacy_scratch, legacy_base, legacy_versioned);
    finalize_player_file_rename(new_scratch, new_dir, base_name, new_versioned);

    bool identical = sb_files_identical(legacy_versioned, new_versioned);
    int legacy_count = sb_count_files(legacy_dir);
    int new_count = sb_count_files(new_dir);

    // ---- Profiling: time ONLY the finalize call; regenerate scratch outside timing. ----
    long lmin = -1, lmax = 0, ltot = 0;
    long nmin = -1, nmax = 0, ntot = 0;
    struct timeval t0, t1;

    for (int i = 0; i < iterations; i++) {
        sb_copy_file(master, legacy_scratch);
        gettimeofday(&t0, NULL);
        finalize_player_file_legacy(legacy_scratch, legacy_base, legacy_versioned);
        gettimeofday(&t1, NULL);
        long us = sb_usec_diff(&t0, &t1);
        ltot += us;
        if (lmin < 0 || us < lmin) {
            lmin = us;
        }
        if (us > lmax) {
            lmax = us;
        }
    }

    for (int i = 0; i < iterations; i++) {
        sb_copy_file(master, new_scratch);
        gettimeofday(&t0, NULL);
        finalize_player_file_rename(new_scratch, new_dir, base_name, new_versioned);
        gettimeofday(&t1, NULL);
        long us = sb_usec_diff(&t0, &t1);
        ntot += us;
        if (nmin < 0 || us < nmin) {
            nmin = us;
        }
        if (us > nmax) {
            nmax = us;
        }
    }

    double lavg = (double)ltot / iterations;
    double navg = (double)ntot / iterations;

    // ---- Report ----
    sprintf(buf,
            "savebench (self, N=%d)\n\r"
            "  equivalence : %s\n\r"
            "  files left  : legacy=%d  new=%d  (expect 1 each)\n\r"
            "  legacy (system rm+cp) : min %ld / avg %.1f / max %ld usec\n\r"
            "  new    (unlink+rename): min %ld / avg %.1f / max %ld usec\n\r",
            iterations, identical ? "IDENTICAL" : "*** DIFFERENT ***", legacy_count, new_count,
            lmin, lavg, lmax, nmin, navg, nmax);
    if (navg > 0.0) {
        sprintf(buf + strlen(buf), "  speedup     : ~%.0fx\n\r", lavg / navg);
    }
    send_to_char(buf, ch);

    // ---- Cleanup (unconditional) ----
    unlink(master);
    unlink(legacy_scratch);
    unlink(new_scratch);
    sb_remove_dir(legacy_dir);
    sb_remove_dir(new_dir);
}
```

- [ ] **Step 6: Build the MUD**

Build with `cd src && make` (i386 Docker container if applicable). Expected: clean compile (the new includes provide `mkdir`/`opendir`; `gettimeofday`/`struct timeval` come from `platdef.h`).

- [ ] **Step 7: Manual test in-game as an implementor (level 100)**

Log in as a level-100 character and run:

```
savebench
savebench 500
```

Expected output (numbers will vary; the new path should be far faster):

```
savebench (self, N=100)
  equivalence : IDENTICAL
  files left  : legacy=1  new=1  (expect 1 each)
  legacy (system rm+cp) : min 3902 / avg 4821.0 / max 31044 usec
  new    (unlink+rename): min 9 / avg 12.0 / max 88 usec
  speedup     : ~400x
```

Then confirm no leftover files:

```bash
ls players/ | grep -i savebench   # expect no output
ls players/ | grep -i SAVEBENCH    # expect no output
```

Also confirm a non-implementor (level < 100) cannot see or run `savebench`.

- [ ] **Step 8: Commit**

```bash
git add src/interpre.cpp src/act_wiz.cpp
git commit -m "Add savebench implementor command to A/B test player-file finalize"
```

---

## Verification (whole feature)

1. Offline gtest green: `cd src/tests && make tests && ../../bin/tests --gtest_filter='PlayerFinalize.*'` → 1 passed.
2. Local MUD: `savebench` reports `IDENTICAL`, `files left 1/1`, new path much faster; no leftover `players/SAVEBENCH_*` or `players/savebench_*`.
3. Test server: same checks, including no-leftover-files after repeated runs, and that a normal player save/login still works (Task 2 smoke).
4. Only after all of the above succeed on **both** local and test server should a *separate, future* change switch production `save_player` to `finalize_player_file_rename`.

## Notes / troubleshooting

- If `src/tests/player_finalize_tests.cpp` fails to compile on `#include "../db.h"` (heavy transitive includes), replace the include with forward declarations of the two functions matching the `db.h` signatures exactly, plus `#include <string>`. Keep the signatures identical to avoid drift.
- The test binary links the full set of MUD object files (`$(OBJFILES)` in `src/tests/Makefile`); those `.o` files must already be built by the normal MUD build (`cd src && make`) before `make tests`.
- **Deliberate deviation from spec §4.1:** the plan does *not* extract `player_base_path` (the bucket-letter switch). That switch is left inline in `save_player` to keep churn to the production save path minimal; the test never needs it (it uses a synthetic throwaway directory). The serializer and both finalizers — the parts the test and the goal actually require — are still extracted.
- `players/temp` remains production's scratch path (Task 2 keeps it); the future production switch to `rename` must re-verify nothing re-reads `players/temp` after a save (it does not today — it is overwritten next save).
