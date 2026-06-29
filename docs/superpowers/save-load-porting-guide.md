# RotS Save/Load Porting & Principles Guide

> **Purpose.** This guide carries the *design intent* of the `feature/savebench-finalize` branch (two features: **savebench atomic-finalize** and **consistent-snapshot autosave**) into a **different target branch whose save/load code has been heavily modified**. The commits on this branch **cannot be cleanly cherry-picked or rebased** onto that branch — the save/load surface there is different. Instead, **re-apply the principles by hand** to the new structure.
>
> Every `file:line` reference below is **where the code lived on `feature/savebench-finalize`** (base ref `7533913`, the release-frodo merge of master; `git merge-base master feature/savebench-finalize` = `d863aab`). On the new branch, find the *equivalent* location — the function that serializes a player, the function that finalizes the save, the heartbeat autosave gate — and apply the principle there. Do not assume the line numbers transfer.

---

## 1. Overview

Two independent features ship on this branch, sharing one set of atomic file-write primitives.

### Feature 1 — savebench atomic-finalize (crash-safe, shell-free saves)

RotS is a strictly **single-threaded C++17 DikuMUD**; every disk save runs inline on the 4-pulse/sec game loop (`TICS_PER_SECOND = 4`). The dominant on-thread save cost was **not serialization** but the **finalize** step of `save_player()`, which historically shelled out *twice per save*:

```
system("rm <base>.*");          // glob-delete prior versioned files
system("cp players/temp <versioned>");   // copy scratch into place
```

Each `system()` is a `fork()`+`exec()` of `/bin/sh` plus a blocking `waitpid()`. The autosave did this **sequentially for every connected player in one pulse**, freezing the heartbeat.

**Goal:** replace those two `system()` calls with direct, **atomic write-temp-then-rename** filesystem operations — eliminating the fork/exec cost, giving crash-safe atomic finalization, and serving as the first low-risk step toward an eventual background serialization thread.

**De-risking mechanism:** an implementor-only (`LEVEL_IMPL = 100`) in-game command **`savebench`** runs the legacy and new finalizers side-by-side against the invoking character's *real serialized bytes*, proves their output is **byte-identical AND that the new one leaves exactly one file** (the glob/stale-duplicate equivalence — the real bug surface), and profiles min/avg/max µs/call. An offline gtest pins the equivalence in CI.

The branch evolved **past** its spec's original "do NOT switch production" boundary: after A/B verification, production `save_player` *was* switched to the new finalizer, a generic `finalize_save_file()` was added and adopted by object/rent/exploit saves, and the whole thing was rewritten from POSIX to portable standard C++.

### Feature 2 — consistent-snapshot autosave (point-in-time crash recovery + anti-rollback)

Reworks the periodic autosave from a `PLR_CRASH`-dirty-bit-gated, per-player save into a **point-in-time consistent SNAPSHOT**: in one single-threaded heartbeat pass it saves **EVERY connected (`CON_PLYNG`, non-NPC) player**. When the server crashes during PvP or a group-boss ("smob") fight, all participants restore to the **same logical moment** (room + HP + XP + stats), instead of being scattered across different save-times/locations.

Cadence becomes a configurable **seconds** value (default 30s, floor 15s) driven by a small **unit-tested scheduling helper** wired into the heartbeat, replacing the old once-per-minute gate. Separately it unifies **anti-rollback** crash-proofing: it hooks `save_char` into `write_exploits` (the single sink for all exploit records), deletes the desyncing 10% kill-XP save, and adds a direct save on the angel reroll.

Crucially, Feature 2 **reuses** Feature 1's atomic primitives — it does not reimplement file writes.

---

## 2. Core principles to carry over — THE HEART

Each subsection is a principle plus its **WHY**, so you can adapt it to the new branch's structure rather than copy code.

### 2.1 Atomic write-temp-then-rename persistence

**Principle.** For every save, serialize to a **scratch/temp file**, then **atomically `rename()` it into the live destination**. Never truncate-or-overwrite the live file in place; never delete-then-write.

**Why.** `rename()` on the *same filesystem* is atomic. A reader (boot-time directory scan, or a concurrent save) never observes a half-written file, and a crash mid-finalize leaves the **previous good file intact**. This replaces both the `system("cp")` text-save path *and* the truncate-in-place binary-save paths. It is the foundation of everything else here.

**On the new branch.** Wherever the new code opens the live save file `"w"`/`"wb"` and writes directly, redirect it to a sibling `.tmp` and finish with a rename. The same-filesystem requirement means the temp must live in (or under) the **same directory tree** as the destination (`players/`, `plrobjs/<bucket>/`, `exploits/<bucket>/`).

### 2.2 Create-first / publish-before-cleanup ordering (never lose the live file)

**Principle.** When a finalize both **publishes a new file** and **cleans up stale siblings**, do the publish (rename-into-place) **FIRST**, then delete the stale files.

**Why.** The versioned player filename encodes mutable fields (level/logtime/flags), so a save can produce a *new* name while an old-named file still exists. `finalize_player_file_rename` renames the new file into place before scanning the bucket dir to remove other `<base>.`-prefixed siblings. A crash *between* the two steps leaves at worst a **harmless stale duplicate** (cleared next save), never the total file loss that delete-then-write risks.

This yields an **asymmetric return contract** you must preserve:
- **false BEFORE the rename** → nothing changed; the old save is intact.
- **false AFTER the rename** → the new file *is* written; only stale cleanup failed (a stale duplicate may linger, cleared next save).

### 2.3 Commit success only on success

**Principle.** Update `ch_file` / clear `PLR_CRASH` / advance any "saved" state **strictly after** the finalize succeeds. On failure, **log and re-converge** next cycle.

**Why.** Keeps the player_table pointing at the **previous, still-present** save on failure rather than at a gap. A failed save simply retries on the next periodic snapshot instead of leaving a hole. (On this branch: `save_player` updates `ch_file` only on success — `db.cpp:2592`; `Crash_crashsave` clears `PLR_CRASH` only on success — `objsave.cpp:1116/1146`.)

### 2.4 No fork/exec for saves

**Principle.** Eliminate `system("rm …")` / `system("cp …")`; use direct `unlink`/`rename` (via `std::filesystem`).

**Why.** Each `system()` was a `fork`+`exec` of `/bin/sh` plus a blocking `waitpid` on the single game thread; `savebench` measured roughly **hundreds-x** speedup from removing it. It is also a prerequisite for ever moving saving off-thread.

### 2.5 Portable, standard C++ for new functions

**Principle.** New code uses the C++ standard library, **not** POSIX/Linux-only APIs:
- filesystem: `std::filesystem` (`rename`/`remove`/`directory_iterator`/`copy_file`/`file_size`) — **not** `opendir`/`readdir`/`unlink`/`rename(2)`/`system`.
- time: `std::chrono::steady_clock` — **not** `gettimeofday`.
- file I/O / comparison: `<fstream>` + `std::istreambuf_iterator` + `std::equal` — **not** raw POSIX I/O.

**Always pass `std::error_code` and check it** — see 2.6.

**Why.** The project's long-term goal is removing the Linux dependency so it can cross-compile (per `CLAUDE.local.md`). This is a **new-functions-only** rule; do not rewrite existing legacy POSIX wholesale.

### 2.6 Non-throwing `std::error_code` overloads everywhere

**Principle.** This MUD is **not exception-oriented**. An unhandled `std::filesystem_error` crashes the process. Every `std::filesystem` call must pass an `std::error_code` and check it.

**Why + gotcha.** `directory_iterator::operator++` **throws** on I/O error. Use `it.increment(ec)` and **check `ec` AFTER the increment**, not at the top of the loop: on error libstdc++ resets the iterator to `end()`, so a top-of-loop `ec` check is skipped and the failure is silently swallowed. (See the patterns at `db.cpp:2484-2500` and `act_wiz.cpp:3818-3826`, established by review commit `3dd054f`.)

### 2.7 Point-in-time consistency via save-ALL in one single-threaded pass

**Principle.** A consistent snapshot = **save every connected player in one single-threaded loop** over `descriptor_list`, with no game logic in between.

**Why.** Because the MUD is single-threaded, one heartbeat pass captures every player at literally the same moment. *That* is what lets PvP/smob participants recover to the same room/HP/XP/stats. The single pass is itself the atomic capture — no two-phase machinery needed.

### 2.8 Why a dirty-bit can't deliver point-in-time consistency

**Principle.** Do **not** gate snapshot inclusion on the `PLR_CRASH` dirty bit.

**Why.** `PLR_CRASH` is set **only on object gain/loss** (`handler.cpp:1224` `obj_to_char`, `handler.cpp:1257` `obj_from_char`). **Location (`load_room`) and current HP change without ever setting it.** So a tank/healer who only moved and took damage is *skipped* and recovers at a stale room/HP, while looters get the fresh snapshot — exactly the desync the feature exists to fix. A dirty-gated snapshot is internally consistent but leaves the "different people at different locations" problem intact. Save-all is both correct **and simpler** (it deletes the dirty-bit + XP-routing machinery).

> Note: `PLR_CRASH` is now **vestigial for autosave** — still set on object gain/loss and still cleared by `Crash_crashsave` each cycle, but no longer gates the snapshot. Fully removing it is a separate, out-of-scope cleanup.

### 2.9 Anti-rollback: exploit-record hook + immediate saves on death/level-up/reroll

**Principle.** Persist crash-sensitive events **immediately**, unified as: **"save the character whenever an exploit record is written."**

**Why.** `write_exploits` is the **single sink** for every exploit record — PK trophy → killer, and death/level/stat/birth/mobdeath/poison/regen/achievement/retired/note → the character. One `save_char(ch, NOWHERE, 0)` hook there (after the atomic exploit-file finalize) crash-proofs *all* impactful events with the **correct** character (the killer for `EXPLOIT_PK`). This closes deliberate crash-to-undo exploits: a player cannot crash to undo a PK/death/level event before the next scheduled snapshot.

Keep the existing **immediate saves on death and level-up** as reliable anchors (`raw_kill`'s death save; `advance_level`'s trailing save). They become partially redundant with the hook but are cheap atomic saves; removing them is unnecessary risk. The **angel reroll needs its own direct save** because it records *no* exploit (see 4.x), so the hook won't catch it.

### 2.10 Skip-and-log, never all-or-none

**Principle.** In the save-all batch, each per-player save is **independent and idempotent**. A single failure **logs and leaves that one player to re-converge next cycle**; it never aborts the whole batch.

**Why.** Under save-all, a strict all-or-none abort would let one broken player (missing bucket dir, disk error) **stall EVERY player's snapshot indefinitely** — reintroducing server-wide staleness, the opposite of the goal. `PLR_CRASH` is cleared only on success, so the failed player retries next cycle. The point-in-time guarantee is unaffected because the snapshot is point-in-time regardless of one straggler.

### 2.11 Silent background saves

**Principle.** The periodic snapshot path passes **`notify = 0`** (silent).

**Why.** The old path sent `"Saving <name>."` to mortals. At a 30s cadence over *every* player, that is constant spam. `Emergency_save` remains a separate path and is unchanged.

### 2.12 A cadence floor

**Principle.** Clamp the interval so a mis-set config can never make the save fire every pulse. `autosave_interval_pulses` and `AutosaveTimer::tick` both clamp to ≥1; the seconds interval has a **15s floor** (raised from 1s).

**Why.** Bounds snapshot cost and prevents a too-small/zero/negative config from running the whole-server save pathologically often.

### 2.13 Deliberate link-dead (`CON_LINKLS`) exclusion

**Principle.** Link-dead characters are **deliberately excluded** from the snapshot — and this is documented in-code so a future reader doesn't "fix" it.

**Why.** `close_socket` saves the char on link loss, sets `connected = CON_LINKLS`, and **leaves `ch->desc` attached** (the detach `d->character->desc = 0;` is **commented out**, `comm.cpp:1689`). So `save_char` *would* work for them — including them is a one-line filter change, not a rewrite. They are left out **by choice**: they were already saved at link-loss, and their death/idle-out paths save them directly. Commit `4229583` adds the explanatory comment so this reads as intentional, not a bug.

> **Dependency to preserve:** the exclusion is currently a `CON_PLYNG` filter choice, *not* enforced by a null `desc`. If that commented-out detach in `close_socket` were ever re-enabled, `save_char`'s `!ch->desc` guard would change link-dead behavior — revisit the `CON_LINKLS` exclusion if so.

---

## 3. Building blocks inventory

The functions to **reuse/adapt** on the new branch. Separated into atomic primitives (port these) and the legacy A/B baseline (keep only as an oracle).

### 3.1 Atomic primitives (REUSE / adapt — do not reimplement)

| Name | Role | Where it lived |
|---|---|---|
| `write_player_text(ch, load_room, scratch_path)` | Path-parameterized PC serializer (`fopen` + `char_to_store` + ~60-line `fprintf` list + `fclose`). **Returns `bool`**; on any `fprintf` `ferror`/`fclose` failure it `remove()`s the partial scratch and returns `false` so the caller skips finalize. | `src/db.cpp:2310-2434` (decl `db.h:110`) |
| `finalize_player_file_rename(scratch, dir, base_name, versioned)` | Crash-safe finalize: `fs::rename` scratch→versioned **FIRST**, then `directory_iterator` over `dir` removing every **other** `<base_name>.`-prefixed entry (dot-anchored, `string_view` match, skipping the just-written file). All non-throwing `error_code`. | `src/db.cpp:2458-2509` (decl `db.h:113`) |
| `finalize_save_file(temp_path, dest_path)` | Generic one-line atomic replace: `fs::rename(temp, dest)` with `error_code`, returns `!ec`. Reused by all binary saves (object/rent) **and** the exploit save. | `src/db.cpp:2514-2519` (decl `db.h:118`) |
| `Crash_open_save_temp(name, dest_out, temp_out)` | Shared temp-opener for object savers: resolves `plrobjs/<bucket>/<name>.obj` as dest plus sibling `.tmp`, opens the `.tmp` `"wb"`, returns `FILE*`. Each object saver then does write-temp + `finalize_save_file`. | `src/objsave.cpp:145-159` |
| `autosave_interval_pulses(interval_seconds, tics_per_second)` / `struct AutosaveTimer { int pulses_since_fire; bool tick(int interval_pulses); }` | **Pure** scheduling logic: seconds→pulses (clamps tics ≥1, seconds ≥15s floor) and a per-pulse accumulator that fires+resets on elapse. **Isolated from game state so it unit-tests without linking the game loop.** | `src/crashsave_schedule.h:12-23`, `src/crashsave_schedule.cpp:8-27` |
| `Stopwatch` (`elapsed<Duration>()`) | Minimal `std::chrono::steady_clock` wrapper, templated `elapsed<>` defaulting to microseconds. Benchmark-only; replaced `gettimeofday`. | `src/stopwatch.h:10-29` |

### 3.2 Orchestrators / call sites (port the wiring, not verbatim lines)

| Name | Role | Where it lived |
|---|---|---|
| `save_player(ch, load_room, index_pos)` | Top-level PC saver: `write_player_text` → `players/temp`, build versioned name from `player_table` fields, derive bucket dir by truncating at last `/`, call `finalize_player_file_rename`, **update `ch_file` only on success**, else `log("save_player: could not finalize player file.")`. Must **bail before finalize if `write_player_text` returns false** (`db.cpp:2573-2576`). | `src/db.cpp:2521-2597` (production switch at `2592`) |
| `save_char(ch, load_room, notify)` | Public PC save entry: guards `IS_NPC`/`!ch->desc`, applies `load_room`/`NOWHERE` semantics, updates whois `player_table` entry, delegates to `save_player`. | `src/db.cpp:2599-2642` (NOWHERE logic `2608-2613`, decl `db.h:103`) |
| `Crash_crashsave` / `Crash_idlesave` / `Crash_rentsave` | Object/rent savers: write temp via `Crash_open_save_temp`, then `finalize_save_file`. `Crash_crashsave` clears `PLR_CRASH` **only on success**. | `src/objsave.cpp:1105-1151`, `1153-1202`, `1204-1252` |
| `Crash_save_all()` | The snapshot driver: iterate `descriptor_list`; for each `CON_PLYNG` non-NPC, `Crash_crashsave` + `save_char(ch, NOWHERE, 0)` (silent). Per-player independent; excludes `CON_LINKLS`. | `src/objsave.cpp:1687-1712` (decl `handler.h:146`) |
| heartbeat autosave gate | `if (autosave_timer.tick(autosave_interval_pulses(autosave_time, TICS_PER_SECOND))) Crash_save_all();` — timer is a local in the game loop. | `src/comm.cpp:849-850` (timer local `comm.cpp:484`) |
| `write_exploits(ch, record)` | Exploit saver: write record + appended prior records to a temp, `finalize_save_file` into `exploits/<BUCKET>/<name>.exploits`, then `save_char(ch, NOWHERE, 0)` (the anti-rollback hook, `db.cpp:3692`). | `src/db.cpp:3591-3694` |
| `add_exploit_record(type, victim, iIntParam, chParam)` | Exploit entry point: gates NPC/immortal, builds the record, routes to `write_exploits` for the right character (killer for PK). | `src/db.cpp:3696+` (decl `db.h:136`) |
| angel-reroll save | Direct `save_char(ch, NOWHERE, 0)` right after `ch->specials2.rerolls += 1`, **no exploit record**. | `src/spec_pro.cpp:2691-2697` |
| `autosave_time` config | Periodic snapshot interval **in seconds**, default 30 (repurposed from minutes(4)). Boot-read constant. | `src/config.cpp:40` |
| `TICS_PER_SECOND` | `= 4`; pulses-per-second feeding `autosave_interval_pulses`. | `src/structs.h:96` |

### 3.3 Legacy A/B baseline (KEEP as an oracle — do NOT reuse in production)

| Name | Role | Where it lived |
|---|---|---|
| `finalize_player_file_legacy(scratch, base, versioned)` | Historical shell-out: `system("rm <base>.*")` then `system("cp scratch versioned")`; returns true if both spawned (`rc != -1`). **Intentionally kept POSIX/`system()`** per the `CLAUDE.local.md` legacy-baseline exception. Production no longer calls it. | `src/db.cpp:2438-2448` (decl `db.h:111`) |
| `do_savebench` + helpers (`sb_copy_file`, `sb_files_identical`, `sb_count_files`, `sb_remove_dir`) | `LEVEL_IMPL` A/B+profiling command: serialize self once to a master scratch, run both finalizers into throwaway `players/SAVEBENCH_*`, assert **byte-identical + exactly-one-file**, profile N iters (default 100, clamp 1..10000) with `Stopwatch`, unconditional cleanup. All I/O confined to throwaway paths. | `src/act_wiz.cpp:3772-3963` (helpers `3777-3834`); registration `src/interpre.cpp:215, 549, 2225` |
| `player_finalize_tests.cpp` | Offline gtest: legacy-vs-rename byte-identity + "exactly one file remains" with a dot-anchored stale decoy; plus `finalize_save_file` replace/create/fail-preserve. | `src/tests/player_finalize_tests.cpp` |
| `crashsave_schedule_tests.cpp` | Offline gtest: `autosave_interval_pulses` clamping + `AutosaveTimer::tick` fire/reset (5 tests). | `src/tests/crashsave_schedule_tests.cpp` |

---

## 4. Design decisions & rationale (including REJECTED alternatives)

Re-state these so they aren't re-litigated on the new branch.

**D1 — Build legacy + rename finalizers side-by-side and A/B-benchmark before switching production.**
*Chosen:* validate byte-equivalence + exactly-one-file + speedup via `savebench`, pin equivalence in a gtest, *then* flip `save_player`.
*Rejected:* directly replacing the finalize with the rename path with no measured baseline.

**D2 — Prune via directory scan of ALL `<base>.`-prefixed siblings, not just the known `ch_file`.**
*Chosen:* the versioned filename encodes mutable fields, so a prior save under a *different* suffix can survive. At boot, `build_directory()` parses whichever `readdir()` returns first into `player_table` — non-deterministic order = a silent **save rollback**. The finalizer must delete **all** dot-anchored siblings.
*Rejected:* unlink only `ch_file` — reintroduces the stale-duplicate rollback hazard the legacy glob existed to prevent.

**D3 — `rename()` (move) the scratch into place, not `copy()` + leave the scratch.**
*Chosen:* rename is atomic and consumes the scratch — no leftover temp, no half-written-dest window. (Each profiling iteration re-copies the master scratch because rename consumes the source; that cost is charged to *both* paths to stay apples-to-apples.)
*Rejected:* keep `cp` semantics — non-atomic and leaves litter.

**D4 — Rewrite from POSIX (`opendir`/`readdir`/`unlink`, `gettimeofday`) to `std::filesystem` + `std::chrono`.**
*Chosen:* the cross-compilation goal; standard C++ with non-throwing `error_code` overloads achieves the same. (Commits `11a06c5`/`f834159`/`9abc7ec`.)
*Rejected:* keep the `dirent.h`/`gettimeofday` version from the original plan. (The legacy *finalizer* deliberately keeps `system()` — that is the baseline, not an oversight.)

**D5 — Switch production `save_player` to `finalize_player_file_rename` once verified (relaxing the spec's non-goal).**
*Chosen:* after local + test-server verification (`6f92ad8`); `finalize_save_file` then extended the atomic treatment to object/rent/exploit saves.
*Rejected:* leave production on the legacy finalizer indefinitely (the spec's original boundary, intentionally relaxed after A/B success).

**D6 — `Crash_save_all` saves every `CON_PLYNG` player every cycle; drop the `PLR_CRASH` gate.**
*Chosen:* a single-threaded pass is a globally consistent atomic capture (see 2.7/2.8).
*Rejected:* keep the dirty-flag gate (route XP through `PLR_CRASH`, mark dirty on movement/damage) — structurally cannot capture location/HP consistently; the "obvious approach that fails."

**D7 — Skip+log a failing player, continue the batch.**
*Rejected:* strict all-or-none batch (originally requested) — one broken player stalls everyone's snapshot indefinitely; consistency benefit is marginal since the snapshot is point-in-time regardless.

**D8 — Exclude `CON_LINKLS` deliberately.**
*Rejected:* including link-dead descriptors (`save_char` would work since `ch->desc` isn't nulled on link-loss). They're already saved at link-loss and on death/idle-out.

**D9 — Default `autosave_time = 30s`, clamp floor raised 1s → 15s.**
*Rejected:* leaving the 1s floor, allowing pathological save frequency.

**D10 — Remove the 10% kill-XP save (`fight.cpp`) outright, no replacement.**
*Chosen:* it committed only *some* kill participants at kill-time (a desync source); under save-all the next ≤30s snapshot captures XP for everyone consistently. (`gain_exp` at `fight.cpp:1297` remains; the save block that followed is gone.)
*Rejected:* replace with a "mark dirty on XP" bit — unnecessary under save-all.

**D11 — Protect the angel reroll with a direct `save_char` and NO exploit record.**
*Chosen:* the reroll calls the same `roll_abilities` stat reroll a level-up makes crash-proof, but records no exploit, so the `write_exploits` hook won't catch it; a player could crash to redo a bad roll and refund a capped attempt. Rerolls are too frequent to log.
*Rejected:* record an `EXPLOIT_STAT` (or any exploit) for the reroll — would route through the hook but spam the exploit log.

**D12 — Leave `check_stat_increase` recording `EXPLOIT_STAT` *before* each increment; rely on `advance_level`'s trailing `save_char` as the +1-stat anchor.**
*Chosen:* `advance_level` is the sole caller and its trailing save persists post-increment state; outcome is already correct.
*Rejected/skipped:* moving the `add_exploit_record(EXPLOIT_STAT,…)` calls to *after* each increment so the hook itself captures the gain — flagged as **optional** hardening (a doc inaccuracy, not a code bug) and **explicitly skipped** per the user. (This is the only acknowledged open item.)

**D13 — Reuse `save_char`/`Crash_crashsave` serially per player in the snapshot.**
*Chosen:* single-threaded serial commit makes the shared `players/temp` scratch safe with no new machinery.
*Rejected:* a two-phase serialize-then-commit batch with per-player temps — introduces a `ch->desc` NULL-deref hazard and a bucket-scan temp-reaping pitfall, for no benefit.

**D14 — Leave `Emergency_save` unchanged (best-effort, per-player).**
*Chosen:* it runs from `SIGSEGV`/`SIGBUS` handlers where the heap may be corrupt; adding `std::vector`/`std::filesystem` batch machinery is async-signal-unsafe and risks a secondary fault losing more players. Its job is **salvage, not consistency**.

**D15 — Cadence is a single boot-read seconds constant.**
*Rejected:* a live runtime cadence-tuning admin command — scope creep, out of scope unless requested.

---

## 5. Gotchas & environment

- **Build is Docker-only, and the Docker `g++` is the *authoritative* compiler.** The `Dockerfile` uses `FROM --platform=linux/386 i386/debian:bullseye` with `g++ make telnet procps ca-certificates libgtest-dev`. The game Makefile forces `-m32`, so it **cannot build natively on Apple Silicon/macOS**. Do **not** attempt a host compile.
- **Host clang/IDE diagnostics are FALSE POSITIVES.** Errors about `MAX`/`MIN`/`unlink`/`gtest`/`std::filesystem` from the host toolchain are noise — only the Docker `g++` build is authoritative. Do not "fix" code to satisfy host clang.
- **CRLF-vs-LF is per-file and load-bearing.** On this branch: `config.cpp`/`objsave.cpp`/`profs.cpp`/`spec_pro.cpp` are **CRLF**; `comm.cpp`/`fight.cpp`/`db.cpp`/`structs.h`/Makefiles/new files are **LF**. **An edit that flips a file's line endings is a defect.** Match each file's existing endings. *(The new branch may differ — check each target file's endings before editing it.)*
- **No clang-format.** The repo has a `.clang-format`, but `.claude/.no-autoformat` (empty marker) suppresses the global clang-format `PostToolUse` hook. Do **not** auto-format these files.
- **`save_char` `load_room`/`NOWHERE` semantics (load-bearing).** If `load_room == NOWHERE` and `ch->in_room != NOWHERE`, then `load_room = world[ch->in_room].number`; the result is stored to `ch->specials2.load_room` (`db.cpp:2608-2613`). `NOWHERE` means **"use current room if any."** The snapshot and anti-rollback hooks all call `save_char(ch, NOWHERE, 0)` and depend on this.
- **`save_char`'s `ch->desc` dependency + link-dead.** `save_char` guards `IS_NPC`/`!ch->desc`. Link-dead chars keep a valid `ch->desc` because `close_socket`'s detach (`d->character->desc = 0;`) is **commented out** (`comm.cpp:1689`). The `CON_LINKLS` snapshot exclusion is therefore a deliberate `CON_PLYNG`-filter choice, **not** enforced by a null `desc`. Any port must preserve this assumption or revisit the exclusion.
- **`write_player_text` must short-circuit finalize on failure** (`db.cpp:2574`). Otherwise the legacy path would `rm` the live file then fail the `cp`, losing the save. Preserve the bool-return → skip-finalize contract.
- **`directory_iterator` increment error check goes AFTER the increment** (see 2.6) — not at the top of the loop.
- **`finalize_player_file_legacy` is NOT a production primitive.** Keep it only as the A/B oracle (`CLAUDE.local.md` legacy-baseline exception). Do **not** "clean it up" to `std::filesystem`. New ports reuse `finalize_player_file_rename` / `finalize_save_file` / `write_player_text`.
- **`rename()` requires the same filesystem.** All temps must live under the destination's tree (`players/`, `plrobjs/<bucket>/`, `exploits/<bucket>/`).
- **Dot-anchor the prune match.** Match `base_name` followed by a literal `.`, or `bob.` would also delete `bobby.*`. Names are lowercased at save time (`db.cpp:2527-2528`), so match lowercased.
- **`savebench` command-index parity** (if you port the diagnostic): `"savebench"` must sit at `command[]` index 249 **and** its `COMMANDO` number must be 249 (`interpre.cpp:549` and `:2225`), within `MAX_CMD_LIST = 350`. A mismatch dispatches the wrong handler.
- **`savebench` must never touch live state** — never call `save_char`/`save_player`, never mutate `player_table[].ch_file`, never write a real bucket. `load_player` reads `ch_file` directly with no disambiguation, so any slip would repoint/destroy the live player file.
- **Two-file partial commit (object vs char).** A player's char file can rename OK while the object file rename fails (different bucket dirs), leaving that one player split (char at T, objects at T-1) for one cycle. Logged; re-converges next cycle; same risk class as a crash-mid-rename.
- **One rare early return in `write_exploits`** (temp could not be opened, `db.cpp:3607`) skips the new `save_char` — acceptable; the next snapshot covers it within the cadence.
- **Combat posture/position is NOT persisted** today (out of scope). "Same moment" = same room + HP + XP + stats only.

---

## 6. Testing approach

**Unit-testable (pure helpers, gtest under `src/tests`, no MUD globals):**
- `finalize_player_file_legacy` vs `finalize_player_file_rename` **byte-identity** + **"exactly one file remains"** with a dot-anchored stale decoy (the glob/stale-duplicate regression guard) → `player_finalize_tests.cpp`. Strengthened (`7c57362`) with decoy/dot-anchor/literal and move-vs-copy cases.
- `finalize_save_file` **atomic replace / create-new / fail-preserve** semantics → same file (`2c5ee31`).
- `autosave_interval_pulses` clamping (incl. the 15s floor) + `AutosaveTimer::tick` fire/reset → `crashsave_schedule_tests.cpp` (5 tests).

These are directly unit-testable because the **plain-path finalizers and the scheduler touch no MUD globals**. The scheduler was deliberately split (`crashsave_schedule.{h,cpp}`) so the seconds→pulses math and per-pulse accumulator test **without linking the game loop** — this is *the* unit-testable seam in an otherwise global-state-coupled change.

**Integration / dev-server-validated (not unit-testable):**
- The actual snapshot behavior (`Crash_save_all`, the heartbeat gate, link-dead exclusion, anti-rollback hooks) is coupled to `descriptor_list`/global game state and **validated on the dev server**, not in CI.
- The A/B equivalence + speedup was verified live via the `savebench` implementor command on the local and test servers **before** flipping production.

**gtest harness wiring (the model is `crashsave_schedule.o`):**
- Two Makefiles. Production `src/Makefile` (`CC = g++ -m32 -w -std=c++1z`) builds `../bin/ageland`. The gtest suite `src/tests/Makefile` (`CXX = g++`, `CXXFLAGS = -std=c++1z -Wall -Wextra -D TESTING`, `LDFLAGS = -lgtest -lgtest_main -lpthread`, output `../../bin/tests`) **recompiles the game `.o` files locally** (its own `OBJFILES`) and links them with the test `OBJS`.
- To wire a **new module + test**: add the `.cpp`/`.o` to `OBJFILES` in **both** Makefiles (with its dependency rule), and add the test `.cpp` to `SRCS` in `src/tests/Makefile`. (`SRCS` here: `CharPlayerDataBuilder`, `ObjFlagDataBuilder`, `obj_flag_data_tests`, `player_finalize_tests`, `crashsave_schedule_tests`, `gtest_main`.) gtest entry is `gtest_main.cpp`.

---

## 7. Commit map (reference)

Range: `7533913..feature/savebench-finalize` (36 commits). Grouped by feature/infra.

**Feature 1 — savebench atomic-finalize:**
- `b531dbc` Add legacy + rename player-file finalizers with equivalence gtest
- `2133679` Harden `finalize_player_file_rename` error handling (review)
- `c3ed24d` Refactor `save_player` to use `write_player_text` + `finalize_player_file_legacy`
- `e4bb0c9` Make `write_player_text` return bool; guard `save_player` against data loss (review)
- `b9203d4` Add `savebench` implementor command to A/B test finalize
- `c2bc1f6` savebench: unlink master scratch on `write_player_text` failure (review)
- `11a06c5` Refactor savebench helpers to `std::filesystem` (non-throwing ec overloads)
- `f834159` savebench: use `std::chrono::steady_clock` instead of `gettimeofday`
- `d8d52ae` Make `finalize_player_file_rename` crash-safe + harden `write_player_text`
- `9abc7ec` Add `Stopwatch` (templated elapsed); fix non-throwing `sb_count_files`
- `7c57362` Strengthen finalize gtest: dot-anchor decoy, literal, move-vs-copy
- `3dd054f` Check `directory_iterator` increment errors after the increment (review)
- `ae47d94` `finalize_player_file_rename`: match via `string_view`, drop per-entry temporaries
- `bbcfd19` Add `finalize_save_file`; make exploits save atomic (no `system("cp")`)
- `27d924f` Make object/rent saves atomic (write temp + rename, not truncate-in-place)
- `2c5ee31` Add gtests for `finalize_save_file` (atomic replace / create / fail-preserve)
- `0a4b53c` Gate `PLR_CRASH` on save success; log finalize failures (review)
- `6f92ad8` **MILESTONE:** switch production `save_player` to `finalize_player_file_rename`

**Feature 2 — consistent-snapshot autosave:**
- `d86ee31` Add crash-save scheduling helper (seconds→pulses + per-pulse timer) with gtests
- `e3ae190` Drive crash-save cadence from the configurable seconds interval (default 30s)
- `1f1ca7a` **MILESTONE:** `Crash_save_all` snapshots all connected players each cycle (silent, no dirty gate)
- `fe4d141` Remove the 10% kill-XP save
- `55dce62` Save the character whenever an exploit record is written (anti-rollback)
- `d84565b` Save immediately on angel reroll (close crash-to-reroll exploit)
- `d616a80` Whole-branch review: accurate config comment, test Makefile dep, clamp coverage
- `4aac35a` Raise the autosave interval floor from 1s to 15s
- `4229583` Document that link-dead (`CON_LINKLS`) exclusion is intentional **(HEAD)**

**Infra / process / docs:**
- `b0218f9` savebench A/B design spec · `650540d` savebench implementation plan
- `5cc1737` Add `libgtest-dev` to dev image · `b727fcf` Fix test suite link (objects + `-lpthread`)
- `e12b661` Disable clang-format auto-format (`.claude/.no-autoformat`)
- `72ec02b` Track `stopwatch.h` as an `act_wiz.o` prerequisite in both Makefiles
- `db9555b` Remove `.DS_Store` files and ignore them
- `4294bc2` Consistent-snapshot autosave design spec + implementation plan

> **Two base refs:** `git merge-base master feature/savebench-finalize` = `d863aab`, but the recorded working base is `7533913` (release-frodo's master merge). The 36-commit listing uses `7533913..feature/savebench-finalize`. **HEAD is `4229583`** — one commit beyond the last recorded merge (`4294bc2`); the 6 functional Feature-2 tasks were complete at `4294bc2`, and `4229583` only adds the link-dead documentation comments.

Design docs (read for full rationale): `docs/superpowers/specs/2026-06-27-savebench-finalize-ab-test-design.md`, `docs/superpowers/plans/2026-06-27-savebench-finalize.md`, `docs/superpowers/specs/2026-06-29-consistent-snapshot-autosave-design.md` (+ its plan). Note the savebench *plan*'s "do NOT switch production" boundary was deliberately relaxed by `6f92ad8`.

---

## 8. Porting guidance — applying this on a heavily-modified save/load branch

This is the actionable checklist. The commits will not rebase; **re-apply principles to the new structure.**

1. **Re-apply principles, don't rebase.** Find the new branch's *equivalents* — the function that serializes a player (≈ `write_player_text`), the one that finalizes/commits a save (≈ `finalize_player_file_rename`/`finalize_save_file`), the object/rent savers (≈ `Crash_*save`), the heartbeat autosave gate, `write_exploits`, the angel-reroll proc — using the role descriptions in §3, not the line numbers. Apply principles §2.1–§2.13 there.

2. **Establish the atomic seam first (Feature 1), then layer the snapshot (Feature 2) on top.** Feature 2 *reuses* Feature 1's primitives; porting the snapshot onto non-atomic saves reintroduces the corruption risk. Order: (a) split serialize from finalize with a bool-returning serializer; (b) add `finalize_save_file` (generic rename) and the player-file rename-then-prune finalizer; (c) route object/rent/exploit through them; (d) only then convert autosave to save-all.

3. **Verify atomicity on the new code.** Confirm: serialize→temp→`rename`; **publish before cleanup**; `error_code` checked on every `fs` call (increment-error check *after* the increment); state committed only on success; serializer returns false → finalize skipped. The dot-anchored prune (delete **all** `<base>.` siblings) is the real correctness surface — port the "exactly one file remains" gtest.

4. **Benchmark / A-B the new path against the old BEFORE switching production.** If the new branch's save path differs materially, re-run the equivalent of `savebench`: prove the new finalizer's output is byte-identical to the old and leaves exactly one file, and measure the speedup, on local + test servers. Pin equivalence in a gtest. Only then flip production (the analog of `6f92ad8`). Keep the old finalize as the A/B oracle until you've verified — do not delete the regression oracle.

5. **Preserve CRLF per file.** Check each target file's existing line endings before editing; never let an edit flip them. Remember clang-format is intentionally disabled here — do not reformat.

6. **Build in Docker; ignore host diagnostics.** Only the i386 `g++` Docker build is authoritative. Treat host clang errors about `std::filesystem`/`gtest`/`unlink`/`MAX`/`MIN` as false positives. The game Makefile is `-m32` and will not compile on macOS host.

7. **Re-establish the unit-testable seam.** Recreate the pure scheduler split (≈ `crashsave_schedule.{h,cpp}`) so seconds→pulses math + the per-pulse accumulator test without the game loop, with the **15s floor** and ≥1 clamps. Wire the new `.o` into **both** Makefiles (with a dep rule) and the test `.cpp` into `src/tests/Makefile`'s `SRCS`, using `crashsave_schedule.o` as the model. Drive `Crash_save_all` from a per-pulse `AutosaveTimer.tick(...)` gate, **not** a once-per-minute `pulse %` gate (that old gate would silently produce zero sub-minute saves under a seconds config).

8. **Re-apply the anti-rollback hooks at the new sinks.** Add `save_char(ch, NOWHERE, 0)` after the exploit-file finalize in the new `write_exploits` (the single exploit sink — confirm it still is on the new branch). Keep death/level-up immediate saves as anchors. Add the **direct** angel-reroll save (no exploit record). Remove the 10% kill-XP save if it still exists. Verify the new `save_char`'s `NOWHERE`/`!ch->desc` semantics still hold, and that `close_socket` still leaves `ch->desc` attached on link-loss — if the new branch nulls it, revisit the `CON_LINKLS` exclusion and document the decision in-code.