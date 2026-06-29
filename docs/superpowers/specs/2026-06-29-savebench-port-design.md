# Savebench Port — Legacy Atomic Finalize + New-System Save-Cost Benchmark

**Status:** Design — approved 2026-06-29, awaiting written-spec review before planning.
**Branch:** `feature/savebench-port` (off `account-management`).
**Source of intent:** `feature/savebench-finalize` (and its duplicate `feature/consistent-snapshot-autosave`), distilled in `docs/superpowers/save-load-porting-guide.md` on that branch.

---

## 1. Goal

Carry the *design intent* of the `feature/savebench-finalize` branch onto `account-management`, which has diverged heavily (account-native JSON persistence). This is a **small, focused, data-gathering branch**, not a full port. It does three things:

1. **Make the legacy non-account save path crash-safe/atomic** — replace its `system("rm")`/`system("cp")` finalize with the ported write-temp-then-atomic-rename finalizer, in portable standard C++.
2. **Profile the full character persistence pipeline** — build a benchmark (offline harness + in-game command) that times every stage of *saving and loading* a character (transform → serialize → account-read → atomic-write, and the reverse), plus the end-to-end total per direction, so we know the real cost and which stage to optimize *before* deciding whether to port any save-frequency change.
3. **Bring over the autosave cadence scheduler as a behavior-neutral refactor** — replace the inline minutes counter with the portable, unit-tested seconds-based scheduler, but **default it to 240s (4 minutes)** so the effective cadence is identical to today. `Crash_save_all` stays `PLR_CRASH`-dirty-gated and the per-fire save set is unchanged, so this adds no save frequency and no cost — it only pre-positions the Feature-2 building block.

The remaining Feature-2 *behavioral* changes (de-gating `Crash_save_all` to a save-all snapshot, lowering the cadence below 4 minutes, `notify=0`, and all anti-rollback hooks) are **deliberately deferred** pending the benchmark results.

## 2. Why this scope (the reframe)

A 6-dimension structural map of `account-management`'s save/load surface, scored against the guide's 13 principles, established:

- **Feature 1 (atomic, shell-free saves) is ~90% already satisfied** — not by porting, but because the account-JSON rewrite independently landed the same primitives. `write_text_file_atomically` (`src/account_management.cpp:591-614`) is exactly the guide's `finalize_save_file`: serialize → `<path>.tmp` → `std::rename` → `remove` temp on failure, never truncating the live file. Publish-before-cleanup ordering exists in `write_account_file`; the per-character file is a single canonical `.character.json`, so there are no versioned siblings to prune (the atomic replace subsumes principle 2.2). Exploit and object writes are atomic; the success-gated `ch_file` commit exists.
- **What remains of F1 is residual**: a still-live legacy `save_player` (`src/db.cpp:2800-2984`) for *non-account* characters that still does `system("rm <base>.*")` then `system("cp …")` — non-atomic, prune-*before*-publish, errors ignored. This is the one place on the branch where the original savebench code maps almost 1:1, because the legacy path is still the *same versioned-text format* the savebench branch was built against.
- **Feature 2 is essentially un-ported** at the start (still `PLR_CRASH`-dirty-gated autosave, minutes cadence via an inline counter, no scheduler, no exploit→save hook, `notify=1` spam). Its **cost-bearing** parts (de-gating to a save-all snapshot, tightening the cadence) are **gated on cost data the user wants first**, because on this branch each save is much heavier (JSON serialize + a `read_account_file` per character), so a save-all snapshot is O(N) JSON parses per pass. Only the **cost-neutral cadence *mechanism*** is brought now (Component C): the portable, unit-tested seconds-based scheduler at a 240s default, which reproduces today's 4-minute cadence and dirty-gated save set exactly.

## 3. Reachability of the legacy path

`save_char` (`src/db.cpp:2986-3079`) branches three ways:
- **Account-linked** (`find_linked_character_owner_account` resolves an owner) → account-native JSON write (`write_account_character_file`).
- **Account-native but unresolved** (`ch_file` ends `.character.json`, no owner) → **refuse + log, no save** (`db.cpp:3070-3075`).
- **else** → `save_player(ch, load_room, tmp)` — the legacy `system()` path (`db.cpp:3076-3077`).

Migration/linking happens at login/character-select (`ensure_character_migration` `src/interpre.cpp:3031`, `link_and_migrate_character:3110`). So any character that completed the account flow is linked and never reaches `save_player`. The legacy path is reachable only for a character saved while **not yet account-linked** (mid-creation, migration transition, or non-account-flow play). Whether that set is empty in production is a live-deployment invariant. **Hardening it is cheap insurance regardless** — if effectively dead it costs nothing at runtime; if ever hit it removes a real data-loss window — so the port proceeds unconditionally, with the A/B equivalence gtest as the primary value (a regression guard on the stale-duplicate rollback hazard).

## 4. Global constraints

- **Portable, cross-compilable standard C++ for all new functions** (per `CLAUDE.local.md`): `std::filesystem` with non-throwing `std::error_code` overloads, `std::chrono`, `<fstream>` — not POSIX. The surrounding account layer is pure POSIX; we do **not** rewrite it (the rule covers new functions only), so a mixed style at the seam is accepted.
- **Preserve per-file line endings.** `config.cpp`/`objsave.cpp` are CRLF; `db.cpp`/`comm.cpp` and all `account_management_*`/JSON modules are LF; new files are LF. An edit that flips a file's endings is a defect. New code lives in new LF files; the existing files edited are `db.cpp` (LF), `comm.cpp` (LF), `config.cpp` (**CRLF** — take care), `interpre.cpp` (LF), and `src/CMakeLists.txt` — verify each file's endings before editing.
- **The clang-format-on-edit hook is live here** (`.claude/.no-autoformat` is absent, unlike the source branch). Prep adds the marker before any source edit.
- **Build is Docker i386 `g++` (C++17), authoritative.** Host clang/IDE diagnostics about `std::filesystem`/`MAX`/`MIN`/`gtest` are false positives. Do not build natively (the build is `-m32`).
- **The benchmark must never touch live state** — never call `write_account_character_file` against a live path, never mutate `player_table[].ch_file`, never write a real account/character file.

## 5. Prep (first commit — hygiene only, no logic)

- Add an empty `.claude/.no-autoformat` marker to suppress the global clang-format-on-edit hook for this repo.
- Add `.DS_Store` (and `**/.DS_Store`) to `.gitignore`, and remove the stray tracked/untracked `docs/superpowers/.DS_Store`. (Mirrors the source branch's "Remove .DS_store files and ignore them" hygiene.)
- Confirm a clean CMake build is green before editing any source.

## 6. Component A — legacy `save_player` atomic port

The legacy path today (`src/db.cpp:2800-2984`): `fopen("players/temp","w")` → ~60 `fprintf` lines → `fclose` → `system("rm <base>.*")` (`:2974-2975`, prune FIRST) → build versioned name `<base>.<level>.<race>.<idnum>.<log_time>.<flags>` (`:2976-2980`) → `system("cp players/temp <versioned>")` (`:2981-2982`) → set `ch_file` unconditionally (`:2983`). Bucket dir is the first-letter prefix `players/A-E/`, `players/F-J/`, … `players/ZZZ/`.

### New module: `src/player_file_finalize.{h,cpp}` (portable std C++)

- **`bool write_player_text(const char_data* ch, int load_room, const std::string& scratch_path)`** — the `fprintf` serializer extracted verbatim (including the `chd.pwd`/`chd.host` copy from `ch->desc` and the `PLR_LOADROOM` load-room handling), writing to `scratch_path`. Returns `false` — and `std::filesystem::remove`s the partial scratch — on any `ferror`/`fclose` failure, so the caller skips finalize and the live file is never destroyed. **The `fprintf`/`FILE*` body is kept exactly** (not rewritten to `<fstream>`) because the produced bytes must stay **identical to the legacy oracle** for the A/B equivalence gtest; the portable-std-C++ rule is honored on the *new* logic — the finalize and the partial-scratch cleanup use `std::filesystem`, not the field-by-field serialization. This matches the source branch, whose `write_player_text` likewise stayed `fopen`+`fprintf`+`fclose`.
- **`bool finalize_player_file_rename(const std::string& scratch, const std::string& dir, const std::string& base_name, const std::string& versioned)`** — crash-safe finalize: `fs::rename(scratch, versioned)` **FIRST**; then `fs::directory_iterator(dir, ec)` removing every *other* entry whose filename begins with `base_name` followed by a literal `.` (dot-anchored, lowercased, `string_view` match), skipping the just-written file. Every `fs` call passes and checks an `std::error_code`; the `directory_iterator` increment error is checked **after** `it.increment(ec)`, not at the top of the loop. Preserves the asymmetric return contract: `false` *before* the rename → nothing changed (old save intact); `false` *after* the rename → new file is written, only stale-sibling cleanup failed (a stale duplicate may linger, cleared next save).
- **`bool finalize_player_file_legacy(const std::string& scratch, const std::string& base_name, const std::string& versioned)`** — the `system("rm")`/`system("cp")` version, kept **only as the A/B oracle** (the `CLAUDE.local.md` legacy-baseline exception). Production never calls it.

### Rewire `save_player`

`write_player_text(ch, load_room, "players/temp")` → if `false`, log and return (no finalize). Otherwise compute `dir`/`base_name`/`versioned` from the existing `player_table[index_pos]` fields, call `finalize_player_file_rename`, and set `ch_file` **only on success**; on failure `log("save_player: could not finalize player file.")` and leave `ch_file` pointing at the previous, still-present save.

## 7. Component B — character persistence pipeline benchmark

The benchmark profiles the **full character persistence pipeline in both directions, stage by stage**, so we can see exactly where the time goes and pick the right target for future optimization. Every stage is already its own function, so this is non-invasive timing around existing calls — **no serializer refactor, no format change, no per-JSON-section instrumentation.**

**SAVE pipeline** (live char → disk; `save_char` account branch → `write_account_character_file`, `account_management_assets.cpp:1-24`):

| # | Stage | Function | Where |
|---|---|---|---|
| S1 | live char → `char_file_u` | `char_to_store` | `db.cpp:2524` (`db.h:103`) |
| S2 | read + parse `account.json` | `read_account_file` | `account_management.cpp` (header-exposed) |
| S3 | `char_file_u` → `CharacterData` | `character_data_from_store` | `character_json.h:147` |
| S4 | `CharacterData` → JSON string | `serialize_character_to_json` | `character_json.cpp:1729` |
| S5 | temp write + atomic rename | `write_text_file_atomically` | `account_management.cpp:591` (**file-local**) |

**LOAD pipeline** (disk → live char; `read_account_character_file` `account_management_assets.cpp:40-66`, then `store_to_char`):

| # | Stage | Function | Where |
|---|---|---|---|
| L1 | read + parse `account.json` | `read_account_file` | account header |
| L2 | read `character.json` bytes | `read_text_file` | `account_management.cpp` (**file-local**) |
| L3 | JSON → `CharacterData` | `deserialize_character_from_json` | `character_json.h:151` |
| L4 | `CharacterData` → `char_file_u` | `apply_character_data_to_store` | `character_json.h:148` |
| L5 | `char_file_u` → live char | `store_to_char` | `db.cpp:2413` (`db.h:104`) |

The in-memory transforms (S1, S3, S4, L3, L4, L5) and `read_account_file` (S2/L1) are header-exposed and timed directly. The two disk-I/O helpers (`write_text_file_atomically` S5, `read_text_file` L2) are currently **file-local**; to time the real code we **add header declarations for them** in the account namespace (a trivial, low-risk exposure).

**Whole-operation total + reconciling remainder.** Each direction is *also* timed **end-to-end as one operation** — SAVE = `char_to_store` + full `write_account_character_file` + the `ch_file` index update; LOAD = full `read_account_character_file` + `store_to_char`. The driver then reports an explicit **"other (minor middle)" remainder = total − Σ(named stages)** that absorbs the small uninstrumented steps (owner/link resolution, `validate_account_owned_character_path`, `create_directory_if_missing`×3, path/`resolved_character_path` resolution, the index update). So every breakdown lists each named stage, the minor-middle remainder, **and the TOTAL**, with shares summing to 100% — the parts always sum to the whole.

### `src/stopwatch.h` (header-only, portable)
Direct port of the source `stopwatch.h`: a minimal `std::chrono::steady_clock` wrapper, templated `elapsed<Duration>()` default µs. Benchmark-only.

### `src/save_benchmark.{h,cpp}` (reusable; links db / character_json / account)
A `time_stage(callable, N) → {min, avg, max}` µs helper, plus a driver that runs all SAVE and LOAD stages `N` times into scratch buffers and returns, for each direction, a per-stage table (µs + share of total) **together with the end-to-end operation total and the minor-middle remainder, so the rows sum to 100%**. Shared by both instruments.

### Offline gtest: `src/tests/save_benchmark_tests.cpp`
Builds an account-owned character in a `TempDirectory` (reusing the `admin_link_character` + `migrate_legacy_character_by_name` fixtures from `db_loader_tests.cpp`/`account_management_tests.cpp`). Runs the full SAVE and LOAD pipelines stage-by-stage across a couple of complexity tiers (light vs heavy inventory/affects), printing the per-stage breakdown. Reproducible, CI-runnable, zero live-state risk; timing is informational (loosely bounded) so variance never red-flags CI, while **round-trip correctness** (`char_file_u` equality save→load) is asserted.

### In-game command: `do_savebench` in `src/savebench.cpp` (new LF file)
A `LEVEL_IMPL` command that profiles the pipeline against the invoking **real** character and its **real** on-disk files, reporting per-stage min/avg/max µs + share of total for both directions. **Sandbox invariant — never mutates live state:** SAVE stages read the live char read-only and write only to a **throwaway** path; LOAD stages read the real `account.json`/`character.json` read-only and hydrate into a **scratch** `char_file_u` + **scratch** `char_data` — never the live player (`store_to_char` into the live char would overwrite their in-memory state). Never calls `write_account_character_file` against a live path, never mutates `player_table[].ch_file`. `N` defaults to 100, clamped `1..10000`; throwaway I/O is cleaned up unconditionally.

## 8. Component C — autosave cadence scheduler (behavior-neutral)

Today's cadence is an inline minutes counter in the game loop: `if (!(pulse % (60*4))) { if (++mins_since_crashsave >= autosave_time) { mins_since_crashsave = 0; Crash_save_all(); } }` (`src/comm.cpp:1047-1053`, counter declared `:691`), driven by `int autosave_time = 4;` "in minutes" (`src/config.cpp:38-40`). This brings the source branch's portable, unit-tested scheduler in its place — **without** changing the effective cadence or the per-fire save set.

### New module: `src/crashsave_schedule.{h,cpp}` (pure, no MUD globals)

Direct port of the source helper:
- `int autosave_interval_pulses(int interval_seconds, int tics_per_second)` — seconds→pulses, clamping `tics_per_second ≥ 1`, `interval_seconds ≥ 15` (the floor), and the result `≥ 1`.
- `struct AutosaveTimer { int pulses_since_fire; bool tick(int interval_pulses); }` — a per-pulse accumulator that returns `true` (and resets) when `interval_pulses` have elapsed, clamping `interval_pulses ≥ 1`.

Pure logic, isolated from game state, so it unit-tests without linking the game loop — *the* unit-testable seam of the cadence change.

### Config + heartbeat gate

- Repurpose `autosave_time` to **seconds, default 240** (4 minutes), and update its comment from "in minutes" so the unit isn't misread. `autosave_interval_pulses(240, TICS_PER_SECOND=4) = 960 pulses = 240s`, identical to today's 4-minute cadence.
- Replace the inline `pulse % (60*4)` + `mins_since_crashsave` block in `game_loop` with a game-loop-local `AutosaveTimer` and `if (autosave_timer.tick(autosave_interval_pulses(autosave_time, TICS_PER_SECOND))) Crash_save_all();`.

### What this deliberately does NOT change (stays deferred)

- `Crash_save_all` (`src/objsave.cpp:1860-1875`) **remains `PLR_CRASH`-dirty-gated** — the same players are saved per fire as today.
- `notify` stays `1` for mortals — unchanged spam profile at the unchanged 4-minute cadence.
- The **15s floor** is the source's safety clamp; the default 240s sits far above it. An admin could still set `autosave_time` as low as 15s, but the default introduces no frequency change. (Adjustable if a higher floor is preferred.)

Net effect at the default: the same autosave behavior as today, but on a portable, unit-tested, seconds-based mechanism — so the later branch only has to lower `autosave_time` and de-gate `Crash_save_all` once the benchmark justifies it.

## 9. Build, test, and command-registration wiring

- **CMake is authoritative** (`src/CMakeLists.txt`; the GNU Makefiles are vestigial and not synced unless requested). Add `player_file_finalize.cpp`, `save_benchmark.cpp`, `savebench.cpp`, and `crashsave_schedule.cpp` to `ROTS_SERVER_SOURCES`; add `save_benchmark_tests.cpp`, `player_finalize_tests.cpp`, and `crashsave_schedule_tests.cpp` to `ROTS_TEST_SOURCES`. Tests auto-register via `gtest_discover_tests(ageland_tests …)`; the single `main()` is `tests/gtest_main.cpp`. Run with `make test` (`ctest`). `stopwatch.h` and the new headers are header/compile-unit additions; follow the existing `*_header_compile.cpp` pattern if a header needs its own TU.
- **Command registration** (`src/interpre.cpp`): add `"savebench"` to the `command[]` array (`:309`) at a free index `< MAX_CMD_LIST (350)` and a matching `COMMANDO(<same index>, POSITION_DEAD, do_savebench, LEVEL_IMPL, TRUE, 0, …)` (`:1740+`). The name's array index **must equal** its `COMMANDO` number or dispatch hits the wrong handler — verify parity.

## 10. Testing approach

- **Pinned in CI (gtest):**
  - `player_finalize_tests.cpp` — `finalize_player_file_legacy` vs `finalize_player_file_rename` **byte-identity** of the published file, and **"exactly one file remains"** with a dot-anchored stale decoy (the glob/stale-duplicate regression guard — the real correctness surface), plus the move-vs-copy / literal-decoy cases.
  - `save_benchmark_tests.cpp` — runs the full SAVE and LOAD pipelines stage-by-stage (plus per-direction total + minor-middle remainder) in a temp account; timing informational (loosely bounded), with round-trip `char_file_u` save→load equality asserted.
  - `crashsave_schedule_tests.cpp` — `autosave_interval_pulses` clamping (incl. the 15s floor, `tics ≥ 1`, result `≥ 1`) and `AutosaveTimer::tick` fire/reset, including that `autosave_interval_pulses(240, 4) == 960` so the 4-minute default cadence is preserved.
- **Dev-server validated (not unit-testable):** the `savebench` command run on real characters for the numbers that gate the deferred frequency work; the legacy-path port verified by inducing a non-account save if a reachable case exists.
- Docker i386 `g++` build authoritative; host diagnostics ignored.

## 11. How the results gate the next branch

The benchmark yields the per-stage SAVE/LOAD breakdown and the end-to-end totals per direction. Decision inputs for the deferred consistent-snapshot autosave:
- `per-player µs × max connected players ≤ heartbeat budget` (single-threaded; the snapshot runs inline on the game loop) — sizes a safe cadence and the `≥15s` floor.
- Whether `read_account_file` per save is large enough to justify caching the owner→account link before a save-all pass.

We reconvene with numbers before porting any frequency change.

## 12. Deferred to a later branch (explicitly out of scope here)

Each is part of the original Feature 2 and is held pending benchmark data:
- **Save-all snapshot** — de-gate `Crash_save_all` (`src/objsave.cpp:1860-1875`) from the `PLR_CRASH` dirty bit to save every `CON_PLYNG` non-NPC player in one pass; handle the now-JSON-persisted stale `PLR_CRASH` flag (`character_json.cpp:34`). *(The cadence scheduler this would lean on is already brought in by Component C; only the de-gating is deferred.)*
- **Cadence reduction** — lowering `autosave_time` below the 240s default (toward the source's 30s). The scheduler mechanism is in place (Component C); reducing the interval is the cost-increasing move held for benchmark data.
- **`notify=0`** for the periodic batch (`objsave.cpp:1868`) — only needed once the cadence tightens or the snapshot de-gates; harmless to leave at the unchanged 4-minute cadence.
- **Anti-rollback hooks** — `save_char` after the exploit finalize in `write_exploits` (`db.cpp:4045`); **angel-reroll** direct save (`spec_pro.cpp:2678`); **remove the 10% kill-XP save** (`group_gain` `fight.cpp:1304-1306`) — note this must stay bundled with the snapshot, since removing it alone *reduces* XP-persistence frequency and increases rollback exposure.
- **CON_LINKLS exclusion** in-code documentation.

## 13. Risks & gotchas

- **In-game command sandboxing is the main hazard.** Strict throwaway-path discipline; no live writes; `load_player`/account resolution reads `ch_file` directly with no disambiguation, so any slip could repoint/destroy a live player or account file. **The LOAD pipeline must hydrate into a scratch `char_file_u` + scratch `char_data`, never the invoking player** — `store_to_char`/`apply_character_data_to_store` into the live char would overwrite their in-memory state.
- **EOL flips** on `db.cpp`/`comm.cpp`/`interpre.cpp` (LF) and especially `config.cpp` (**CRLF**) from the active format hook — mitigated by adding `.no-autoformat` first and verifying endings before edit.
- **`autosave_time` is a semantic unit change** (minutes → seconds). The default flips `4` → `240` to keep the same 4-minute cadence; the in-code comment must change from "in minutes" so a future reader can't reintroduce a 60× error. The `crashsave_schedule_tests.cpp` assertion `autosave_interval_pulses(240, 4) == 960` pins this.
- **Dot-anchor the prune match** — match `base_name` + a literal `.`, or `bob.` would also delete `bobby.*`. Names are lowercased at save time; match lowercased.
- **`rename()` requires the same filesystem** — the scratch (`players/temp`) and the versioned destination live under `players/`, so this holds for the legacy path.
- **`directory_iterator` increment error check goes AFTER the increment** — on error libstdc++ resets the iterator to `end()`, so a top-of-loop `ec` check is silently skipped.
- **Stale `.o` artifacts** — leftover `src/crashsave_schedule.o` and `src/tests/*.o` from a prior `feature/savebench-finalize` build sit in the tree (gitignored); a clean CMake `build/` tree avoids linking them. Do not be misled into thinking those modules already exist here.
- **Mixed portability style at the seam** — new std::filesystem helpers neighbor the POSIX account layer; accepted per `CLAUDE.local.md` (new functions only).

## 14. File touch-points (for planning)

| Change | File |
|---|---|
| `.no-autoformat` marker | `.claude/.no-autoformat` (new) |
| `.DS_Store` ignore + cleanup | `.gitignore`; remove `docs/superpowers/.DS_Store` |
| Ported finalizers + bool serializer | `src/player_file_finalize.{h,cpp}` (new) |
| Rewire `save_player` | `src/db.cpp:2800-2984` |
| Timer | `src/stopwatch.h` (new) |
| Reusable pipeline-benchmark helpers | `src/save_benchmark.{h,cpp}` (new) |
| Expose `write_text_file_atomically`/`read_text_file` decls (for direct stage timing) | account namespace header (e.g. `src/account_management.h`) |
| In-game command | `src/savebench.cpp` (new) |
| Command registration | `src/interpre.cpp` (`command[]` + `COMMANDO`) |
| Finalize equivalence gtest | `src/tests/player_finalize_tests.cpp` (new) |
| Save-cost gtest | `src/tests/save_benchmark_tests.cpp` (new) |
| Autosave cadence scheduler | `src/crashsave_schedule.{h,cpp}` (new) |
| Heartbeat gate (replace inline counter) | `src/comm.cpp:1047-1053` |
| Autosave interval config (→ seconds, default 240) | `src/config.cpp:38-40` |
| Scheduler gtest | `src/tests/crashsave_schedule_tests.cpp` (new) |
| Build wiring | `src/CMakeLists.txt` (`ROTS_SERVER_SOURCES`, `ROTS_TEST_SOURCES`) |
