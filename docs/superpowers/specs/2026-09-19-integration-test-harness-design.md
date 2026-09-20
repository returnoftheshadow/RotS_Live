# Integration Test Harness — Design

**Date:** 2026-09-19 · **Owner:** drelidan · **Branch:** `fix/spell-room-affect-uaf-port`
**Status:** approved design (chat, 2026-09-19); slice 1 plan in
`docs/superpowers/plans/2026-09-19-integration-test-harness.md`; slice 3 spec in
`2026-09-19-ci-integration-asan-design.md`; the slice 2 amendment below was approved in chat
on 2026-09-20 and its plan is `docs/superpowers/plans/2026-09-20-fix-wave-and-slice-2.md`.

## Problem

Behaviour changes on this branch (room-affect ticks, poison attribution and punishment,
kill-credit separation, summon, earthquake ordering) are verified by hand against the 4810
dev port following `manual-test-plan.md`. Each scenario needs two or three characters, a
deploy, and a wait for real ticks. Manual verification is now the slowest step of every
change, and it cannot be repeated on demand after a follow-up commit.

The unit suite (`bin/tests`, GoogleTest) pins the pieces but cannot exercise a PC death end
to end: `raw_kill()` saves player files, `Crash_crashsave()` writes relative to the cwd, and
the pulse loop, login, and networking are outside its reach.

## Goals

- Run the real server binary against a small, self-contained test world and drive it over
  telnet exactly as a tester would, then assert on what each character sees, on `stat` and
  `exploits` output, and on the JSON records the server writes.
- Make the branch's manual scenarios repeatable with one command on a Mac (Docker) and in
  CI (native Linux), with a sanitizer build so a use-after-free fails the run instead of
  depending on luck.
- Keep scenarios short: no minute-long waits for a game hour, and reproducible dice.

## Non-goals

- Replacing the unit suite or the account smoke flow. Both stay as they are.
- Simulating clients beyond plain telnet (no MSDP, no proxy header, no web client).
- Testing against 4810 or any live port. The harness only ever talks to a server it started.
- Log-channel verbosity. Recorded as future work; the harness reads game output and files.

## Decisions (owner, 2026-09-19)

1. **Server-side harness mode is approved.** A `-t` startup flag enables an implementor-only
   `harness` command and honours `ROTS_RANDOM_SEED`. Off by default; the flag never ships
   in an autorun script.
2. **Test world is synthetic and committed**, claiming zone 11's vnum range so the start
   rooms in `consts.cpp`/`config.cpp` resolve unchanged. A separate tool can copy real
   mob/object prototypes by vnum from the local, git-ignored `lib/world` into the test
   files; committing the copied prototypes is the owner's call at that time.
3. **pytest** is the runner (new dev dependency on the host and in CI). The existing
   `tools/account_smoke_tests.py` stays on `unittest`.
4. **Three delivery slices**: harness + world + three pilot scenarios; the full branch
   scenario suite; the CI job with an ASan build.
5. **Shared Docker.** Other agents use the same Docker image and repository. The Docker
   launcher never uses the compose `container_name` or `--service-ports`; every run gets a
   unique container name, a random published host port, and its own temp directories. It
   also honours the machine-local lock convention in `/tmp/rots-docker-lock/README.txt`
   (proposed by a sibling session on 2026-09-19): it refuses to start while any `*.lock`
   file exists there and writes `rots-live-uaf-port-it.lock` for the life of its server.

## Architecture

```
host (macOS or Linux)                         server process
┌──────────────────────────────┐   telnet   ┌─────────────────────────────┐
│ pytest scenarios             │ ─────────▶ │ bin/ageland -t -d <run lib> │
│  └ GameSession × N           │            │   cwd = <run dir>           │
│ ServerLauncher (local|docker)│ ─ spawn ─▶ │   stderr → <run dir>/game.log│
│ TestLibBuilder → <run lib>   │            │   lib = tracked text+misc   │
│ CrashMonitor ← game.log      │            │         + tests/…/world     │
└──────────────────────────────┘            │         + seeded accounts   │
                                            └─────────────────────────────┘
```

One server per pytest session. Each scenario gets fresh characters where it needs them
(fixtures are cheap: they are JSON files written before boot) and cleans up with `purge`
and `zreset` rather than a reboot. A scenario that must observe boot behaviour asks for a
function-scoped server instead.

### Test world (`tests/integration/world/`)

Standard world files in the documented formats (`docs/data-formats/world-files.md`), one
directory per category with its own `index`. Boot exits when any of scr/zon/mdl/wld/mob/obj
counts zero records, so every category holds at least one record; shp has an empty index.

Zone 11, top vnum 1199, lifespan long enough that resets never fire mid-scenario:

| Vnum(s) | Purpose |
|---|---|
| 1101, 1102, 1110 | immortal start, immortal idle, frozen room (config values) |
| 1129, 1160, 1170, 1184 | mortal start rooms: Olog-hai, human group, wood elf, Beorning |
| 1151, 1152, 1190, 1191 | retirement, bugged-start, orc idle rooms |
| 1130–1139 | arena: a 3-room east–west corridor, a `DARK`-flagged room, two rooms with a lit exit chain for "walk two rooms away", an `INSIDE` room for weather-independent light |

Start rooms for races outside zone 11 (Uruk-hai, common orc, Haradrim) fall back to real
room 0, which is harmless; fixtures use human, wood elf, and Olog-hai.

Mobs (all `M`-type, no death cries, low regen so hit points are predictable):

- `1130` **target**: level 5, few hit points, non-aggressive. The victim for kill-credit
  and room-affect scenarios.
- `1131` **snake**: prog number 1 selects `SPECIAL(snake)` (spec_pro.cpp), which casts
  `spell_poison` on its fight target and therefore records a resolvable mob poison origin.
- `1132` **bystander**: neutral, non-aggressive, for splash-damage cases.
- `1133` **brute**: level 20, hits hard, for "die while still swinging".
- `1134` **pet**: same as target with `MOB_PET`, to prove the real-mob test excludes it.

Objects: one light source and one plain weapon, so scenarios never depend on library gear.
Zone reset commands place one target and one snake in fixed arena rooms; scenarios that
need more use `load mob <vnum>`.

Scripts and mudlle: one minimal record each, modelled on the smallest real files, attached
to nothing.

**Library extraction** (`tools/testworld_extract.py`): given vnums, copies mob and object
prototype records verbatim from `lib/world` in the main checkout into
`tests/integration/world/extracted.{mob,obj}` and adds the files to the indexes. It refuses
to run when the source directory is missing and never edits the source.

### Fixtures: account-native characters

Characters are seeded as account-native JSON before boot, not created through the login
flow. This avoids the email-verification round trip, the QEMU account-creation crash noted
in the Docker memory, and gives every character explicit spell knowledge.

- One account per run under `lib/accounts/<bucket>/<email>/account.json` with
  `email_verified: true`, a fixed SHA-512 crypt `password_hash` for a fixed harness
  password (committed as a constant; the server verifies with `crypt()`), and a
  `character_links` entry per character.
- One `<name>.character.json` per character from a committed template captured from a real
  account-native save, with name, idnum, race, profession, level, hit/mana/move, load room,
  and `skills` substituted. Empty `objects.json` and `exploits.json` beside it. The
  template carries `schema_version`; the builder refuses a template whose version differs
  from the one `src/character_json.cpp` writes, so schema drift fails loudly at build time.
- Standard roster: `Harnessimp` (level 100), `Harnessmage` (level 30, knows poison, blaze,
  mist of baazunga, haze, summon, earthquake), `Harnessfighter` (level 20 warrior),
  `Harnessvictim` (level 10). Scenarios can request additional characters by name.

Spell knowledge below 120 still fails the roll at `spell_pa.cpp:1152` some of the time;
with `ROTS_RANDOM_SEED` the outcome is reproducible, and the session helper retries a cast
until its success line appears or a small budget is spent.

### Harness package (`tests/integration/rots_harness/`)

Python 3.11+, standard library plus pytest. Telnet primitives move from
`tools/account_smoke.py` into `tools/rots_telnet.py` (`TelnetStreamSanitizer`,
`BufferedPromptReader`, `recv_until`, `send_line`); `account_smoke.py` imports and
re-exports them so its unit tests and CI flow are unchanged.

- `TestLibBuilder`: builds one run's lib directory: copies tracked `lib/text` and
  `lib/misc`, creates the runtime directories the CMake `setup` target creates, copies
  the test world, writes the account and character fixtures. Returns the lib path and the
  roster. Pure file work, unit-tested against a temp directory.
- `ServerLauncher` (abstract): `start() -> ServerHandle`, `stop()`. Owns process lifetime
  and log capture. `LocalProcessLauncher` runs `bin/ageland -t -d <lib> <port>` with the run
  dir as cwd (the server writes `.ageland.pid` to cwd before it `chdir`s). `DockerComposeLauncher`
  runs the same command through `docker compose run --rm --name rots-it-<id> -p 127.0.0.1::<port>`
  and reads the published host port back from `docker port`. Before starting it applies
  the lock convention from decision 5 (`ROTS_IT_DOCKER_LOCK_DIR` overrides the directory;
  an absent directory disables the check, so CI and other machines are unaffected).
- `ServerHandle`: host, port, log path, `is_alive()`, `exit_status()`.
- `CrashMonitor`: tails the log and fails the scenario on process exit, a signal, an
  AddressSanitizer report, or a `SYSERR` line not on an allow-list (a few boot warnings
  such as missing JUDP are expected).
- `GameSession`: one connection logged in as a roster character through the account menu.
  `command(text, until=...) -> Transcript`, `expect(markers, timeout)`, `stat(name)`,
  `exploits(name)`, `cast(spell, target)` with the retry described above,
  `walk(directions)`. Closes cleanly with `quit` or by dropping the socket for
  link-death cases.
- `Transcript`: the sanitized text of one exchange with small parsers for the `stat`
  hit-point line and the `exploits` listing.
- `Records`: reads `<name>.exploits.json` and `<name>.character.json` from the run lib and
  exposes typed records (`type`, `victim_name`, `victim_level`, `killer_level`,
  `int_param`).
- `conftest.py` fixtures: `server` (session), `imp`, `mage`, `fighter`, `victim`, and
  `harness` (the `-t` command surface: `tick()`).

### Server-side harness mode

New `src/test_harness.h/.cpp` (standard C++ only, per the project-local rule):

- `StartupOptions.harness_mode` set by `-t`; `parse_startup_options` gains the case and a
  test in `startup_options_tests.cpp`.
- At startup, when harness mode is on and `ROTS_RANDOM_SEED` is set, seed both `srandom`
  and `std::srand` with it instead of the clock and log the seed. Ignored otherwise.
- `ACMD(do_harness)`, command 250 `harness`, `LEVEL_IMPL`. Refuses with a message when
  harness mode is off. Subcommand `tick` runs the game loop's hourly block in its own
  order: `weather_and_time(1)`, `point_update()`, `stat_update()`. This runs inside the
  imp's command, the same hazard class as `slay` or `purge`, which already extract other
  characters mid-command; the imp itself is level 100 and cannot die in the block.
- Combat rounds (`PULSE_VIOLENCE`, three seconds) are not forced. Scenarios that need melee
  wait for real rounds; a `harness violence` subcommand is deferred until a scenario
  needs it.

### Class responsibilities

- `TestLibBuilder`: assembles one run's lib directory from tracked data, the test world,
  and the fixture roster.
- `ServerLauncher` / `LocalProcessLauncher` / `DockerComposeLauncher`: starts and stops one
  server process and captures its log, for one execution environment each.
- `ServerHandle`: reports where a running server is and whether it is still alive.
- `CrashMonitor`: decides whether the server log or process state shows a crash.
- `GameSession`: drives one logged-in telnet connection and returns transcripts.
- `Transcript`: holds one exchange's sanitized text and parses the few lines scenarios read.
- `Records`: reads a character's on-disk JSON records for assertions.
- `StartupOptions` (existing): gains the `harness_mode` field; its responsibility is
  unchanged.
- `do_harness` and the seed hook are free functions; no other class shapes change.

### Run modes

- `make integration` runs `pytest tests/integration` on the host. Launcher selection:
  `ROTS_IT_LAUNCHER=local|docker`, defaulting to `local` when `bin/ageland` is a runnable
  ELF on this host and `docker` otherwise. `ROTS_IT_BINARY` overrides the binary path
  (needed on a Mac to point at the stack-protector build). `ROTS_IT_SEED` sets the RNG seed
  (default fixed). `ROTS_IT_KEEP=1` keeps run directories after a failure.
- The Docker launcher passes the repository bind mount as compose does, but the run
  directory lives under the repository's `build/integration/<id>/` so it is visible
  inside the container; it is git-ignored by the existing `*build*/` pattern.
- CI adds an `integration-asan` job on `ubuntu-24.04` (slice 3, own spec:
  `2026-09-19-ci-integration-asan-design.md`): build both targets with
  `-fsanitize=address`, prove the 32-bit runtime linked, run the suite with the local
  launcher, upload run directories on every outcome. The runtime is packaged for the
  runner (`lib32asan8`), so no plain-build fallback exists. The same change repaired the
  existing job's 32-bit crypt link, which had failed since 2026-08-14; that job now reaches
  its unit tests and fails there on four pre-existing OlogHaiHelpers cases.

### Scenario catalogue (slice 2, mapped to `manual-test-plan.md`)

1. Blaze tick after the caster dies; after the caster quits; after the caster's socket
   drops and another character logs in. Asserts: tick fires, victim's `exploits` names the
   mage or nobody, no crash.
2. Poison: poisoner logs out before the lethal tick; poisoner dies first; control with the
   poisoner online. Punishment cases (a) snake poison, flee two rooms, die alone → hit
   points at a quarter, no stat loss, `EXPLOIT_POISON` only; (b) die still fighting the
   snake → harsh penalty, mob-death record; (c) player poison, die fighting the brute →
   harsh penalty, poisoner keeps the PK record.
3. Kill credit: killing blow from a non-engaged spell; splash bystander manufactures no
   credit; remote-credit XP split where the two engaged fighters see the share line and the
   remote mage does not.
4. Summon by name in the dark room; summon a link-dead character.
5. Earthquake message order with the caster falling last.
6. Mass affect expiry while a character quits in the same tick (no crash).

Not automatable from a client: forcing memory reuse of a freed caster. Under ASan the
stale read itself fails the run, which is the stronger check.

### Testing the harness

- Unit tests (pytest, no server): lib builder layout, fixture substitution and schema
  guard, transcript parsers, crash-monitor classification, launcher command construction.
- `test_boot.py`: the server boots on the test lib with harness mode, the imp logs in,
  `harness tick` answers, a character dies to `slay` and revives at its start room. This is
  the slice 1 acceptance test.

## Risks and mitigations

- **World-file mistakes exit the server at boot.** The boot test runs first and the log is
  attached to the failure.
- **Schema drift in the character template.** The version guard in the builder plus a unit
  test that round-trips the template through the server's loader on Linux.
- **QEMU flakiness on a Mac.** Docker runs are for development; CI is the gate.
- **Timing.** Every wait is marker-driven with a timeout, never a fixed sleep; three-second
  combat rounds are the floor for melee scenarios.
- **Concurrent agents.** Unique names, random ports, per-run directories; nothing touches
  `lib/`, `bin/`, or the `rots` container.

## Slice 2 amendment (owner, 2026-09-20): fix wave, deterministic affects, full scenario suite

Approved in chat on 2026-09-20 and revised the same day after a dual adversarial review
(Opus architecture reviewer and a Fable reviewer, both read-only). This section supersedes
the sentences above and in the slice 3 spec that describe the sanitized ctest step as
non-blocking and the poison scenario as an xfail. Everything lands on
`fix/spell-room-affect-uaf-port` and draft PR #309; the plan runs in two phases with a
CI-green checkpoint between them. Per-task review diffs stay in the plan's SDD workspace so
the final whole-branch review can be read task by task.

Source of the sanitized-gtest figures below: CI run 35491250002 on head 3aa2ddd, the
`Run C++ unit tests under AddressSanitizer` step, grouped by sanitizer kind and first
project frame. 123 failing cases.

Corrections to earlier sections that the review surfaced: the server is function-scoped in
`conftest.py` (each scenario boots its own server), not one per session; the roster is
implemented as `Harnimp`, `Harnmage`, `Harnfighter`, `Harnvictim`; the CI job timeout is 30
minutes. Files touched by this amendment that use CRLF: `src/objsave.cpp`, `src/limits.cpp`,
`src/handler.cpp`. All edits to them are byte-preserving.

### Phase A: fix wave

Order: A5's triage confirmation first (it is already done offline, the task re-runs the
grouping on the first Linux build), then the allocation sweep A4 (it changes the report
set), then the server defects A1-A3 and the named A5 fixes in any order, then the gate flip
A6. A task that finds a real server bug inside a class assumed test-only fixes it as a
server defect with a regression gtest and records the reclassification in the ledger.

**A1. `fread_string` size check (`src/db.cpp`).** With `L = strlen(buf)` and
`T = strlen(tmppoint)`, `strcat` writes its terminator at index `L + T`, and the
non-terminator branch then writes `'\r'` at `L + T` and `'\0'` at `L + T + 1`. `buf` has
`MAX_STRING_LENGTH` bytes, so the invariant is `L + T + 2 <= MAX_STRING_LENGTH`. The current
check `L + T > MAX_STRING_LENGTH` admits two bytes too many; merely rejecting equality still
admits one. The fix is the literal condition
`if (strlen(tmppoint) + strlen(buf) + 2 > MAX_STRING_LENGTH)`.

The rejection path is `exit(0)`, and `gtest_discover_tests` runs each case in its own
process, so a plain gtest cannot observe it. The regression test uses `EXPECT_EXIT` with
`ExitedWithCode(0)` and the "string too large" message for the reject cases
(`L + T == MAX_STRING_LENGTH - 1` and `== MAX_STRING_LENGTH`) and a normal read for the
accept case (`L + T == MAX_STRING_LENGTH - 2`). Death tests run under ASan in CI; the task
verifies they pass there. Trade-off: a world, shop or text string whose accumulated length
lands in the two-byte band boots today and will refuse to boot after the fix. The task
scans the main checkout's git-ignored `lib/world` (when present) for `~`-terminated
strings within two bytes of 8192 before the change lands and records the result.

**A2. Idle rent writes an incomplete object file (`src/objsave.cpp`, `src/act_wiz.cpp`).**
Root cause: `Crash_crashsave` and `Crash_rentsave` write rent code, objects, aliases, then
`Crash_follower_save` (which always ends with the `-17` follower sentinel); `Crash_idlesave`
stops after the aliases. The refresh reads the file with the strict
`object_save_data_from_binary`, which requires the follower section, and reports "Truncated
objects data while reading follower record". The harness evidence log
(`finding-run5-game.log`, 2026-09-19 16:53) shows the SYSERR immediately before "Harnvictim
force-rented and extracted (idle)", the idle path's own log line; the harness idle gate
(`fe0c9b9`, 18:11 the same day) was added afterwards, so the allow-list fragment in the crash
monitor has been dead since then. Consequence today: at account character selection the
JSON copy is authoritative and the legacy binary is deleted, so an idle force-rent whose
refresh fails restores the inventory from the last successful save at next login. That is
silent data loss bounded by the autosave cadence, not an invisible defect.

Fix, four parts:

1. `Crash_idlesave` mirrors `Crash_rentsave`: `Crash_follower_save(ch, fp)` then
   `extract_followers(ch)` before closing. An idle rent is a rent; today its NPC followers
   are orphaned in the world when the character is extracted, and saving without
   extracting would duplicate them at next login. Behaviour delta, stated: after the fix
   the account-native `objects.json` is rewritten on idle rent with `RENT_TIMEDOUT` and the
   post-`Crash_extract_norents` inventory, and followers are saved and extracted. This is
   consistent with the earlier decision (WIP.md, legacy-file retirement) to keep the
   writer strict rather than loosen the reader.
2. `FILE` ownership moves to the caller. `Crash_follower_save` closes the caller's `fp` on a
   nested `Crash_save` failure and `Crash_follower_load` closes it on a short read, while
   every caller closes it again: a latent double `fclose` that every follower-less legacy
   file on disk already hits at login. Remove the three inner `fclose` calls and state
   "the caller owns `fp`" at both declarations.
3. `do_purge` in `src/act_wiz.cpp` calls `Crash_idlesave(ch)` for a purged player: it saves
   the purging immortal, not the victim. Fix the argument to `vict`. This also gives the
   harness a reachable trigger for the idle-save path (see the scenario below).
4. Remove the "Truncated objects data while reading follower record" fragment from the
   crash monitor's allow-list once the scenario below is green.

Tests: a gtest that runs the real `Crash_idlesave` on an empty-inventory character into a
temp directory and asserts the strict reader accepts the bytes and the resulting JSON holds
`RENT_TIMEDOUT` with no objects; a second case with one saved follower so the section's
content round-trips; a gtest for `Crash_follower_load` on a short read that asserts the
caller's `FILE` is still open. Integration: a scenario in which the imp purges a logged-in
roster character and asserts no SYSERR, the character's `objects.json` refreshed, and a
clean relogin.

Findings recorded, not fixed here: the alias writer emits a keyword and then skips the
length and command when the command is empty (`Crash_alias_save`), which the reader
misparses; not reachable by the harness.

**A3. The four `OlogHaiHelpers` gtests (`src/tests/olog_hai_tests.cpp`,
`src/olog_hai.cpp`).** Established from the CI output: `TwoHandedStyleAppliesCurrent
BaseDamageMultiplier` expects 19 and gets 13 because `get_base_skill_damage` multiplies by
the integer expression `3 / 2`, which is 1: a real server defect, fixed as `* 3 / 2`.
`HeavyFightingAndRidingAdjustOverrunDamage` expects 14 and gets 17 (both the heavy-fighting
and riding multipliers apply); the task decides whether the test's expectation or the
riding check is stale and says which. `ResolvesTextTargetsUsingRoomVisibilityLookup` and
`UsesCurrentFightTargetWhenSmashTargetIsOmitted` get a null victim from
`get_char_room_vis` and the smash fallback; the task finds whether the fixture lacks room
or visibility state or the lookup changed. Each case ends with a passing test and a
one-line classification (test stale vs server defect) in the ledger.

**A4. Test character allocation.** The server allocates `char_data` through `CREATE`
(a `calloc` wrapper) and `free_char` releases with `free`; the one server-side `new
char_data` is the offline `save_benchmark.cpp`, which pairs it with `delete`. 42 test sites
across eight files (33 in `interpre_account_menu_tests.cpp`) allocate with `new char_data {}`
and release through `free_char`: 60 alloc-dealloc-mismatch reports. Design: a new pair
`src/tests/test_character_support.h` and `.cpp`, registered in `ROTS_TEST_SOURCES` beside
`test_random_utils.cpp` (the test binary links every test source into one executable, so
header-only non-inline definitions would be duplicate symbols), providing

- `char_data* allocate_test_character(int clear_mode)`: `calloc` plus
  `clear_char(character, clear_mode)`. The mode is load-bearing: `MOB_VOID` allocates the
  skills and knowledge arrays and `MOB_ISNPC` does not, and `free_char` logs a SYSERR for an
  NPC that has them. Three sites use `MOB_ISNPC` (`mage_tests.cpp`,
  `affect_update_tests.cpp`, `room_affect_tick_tests.cpp`).
- `void release_test_character(char_data*)`: `free_char`, for sites that today release
  through `free_char`. Documents the ownership rule in one place.

Sites converted: every `new char_data {}` whose release is `free_char`. Two sites keep
`new`/`delete` on purpose and gain a comment saying why: `affect_update_tests.cpp` (the
freed-sentinel allocation without `clear_char`, whose `free_char` would unregister the very
slot the test then reuses) and `act_wiz_tests.cpp` (a `store_to_char` round-trip released
with `delete`, which does not walk the affect list). `ScopedObjectPrototypeTable` in
`db_loader_tests.cpp` deletes `obj_data` nodes the loader created with `CREATE`; it releases
them with `free_obj` (which frees the strings only for `item_number == -1`, so the task
checks what the loader attached) or `free`, while its own `obj_proto`/`obj_index` arrays stay
`new[]`/`delete[]`.

**A5. Remaining sanitized gtest classes: the named fix set.** From the run cited above:

| Count | Class | Root cause | Fix |
|---|---|---|---|
| 49 | global-buffer-overflow in `printf_common` | `write_player_text` (`src/db.cpp`) encrypts the 10-byte global `pwdcrypt` in place, terminator included, then prints it with `%s`; the reader loads the field with a fixed length | Server defect. Print exactly `MAX_PWD_LENGTH` bytes in a form the reader still decodes; the task reads the `KEY_STR("password", …)` loader and any decrypt step first, keeps the on-disk format loadable, and adds a gtest that writes and reloads a player text record under ASan |
| 3 | alloc-dealloc-mismatch, `operator delete` on `calloc` | `ScopedObjectPrototypeTable` | Covered by A4 |
| 2 | heap-use-after-free `JsonReader::skip_whitespace` | `JsonReader` and `JsonReaderV2` store `const std::string& m_input`; two tests bind a string literal temporary. All server call sites pass named lvalues | API hardening: `explicit JsonReader(std::string&&) = delete;` on both classes turns the trap into a compile error; the two tests hold a named `std::string` |
| 4 | SEGV in `act()` via the "$n has reconnected." and momentum messages | `act` walks `world[ch->in_room].people` for `TO_ROOM`; the fixtures in `interpre_account_menu_tests.cpp` (3) and `weapon_master_handler_tests.cpp` (1) place characters in rooms the test `world` does not populate | Test fixture: give each a real room entry, following the `ensure_test_world_room` pattern in `db_loader_tests.cpp` |
| 1 | SEGV in `obj_from_room` | The object is not in `world[in_room].contents`, the previous-element walk ends null and dereferences it; the fireball test in `mage_tests.cpp` triggers it | Server null guard with a SYSERR log plus the fixture placing the object in the room's list; gtest for the guard |
| 4 | OlogHai assertions | See A3 | A3 |
| 60 | alloc-dealloc-mismatch via `free_char` | See A4 | A4 |

Every failing case is in this table; there is no unclassified tail. If a fix turns out to
need more than its task's bounded scope, the task stops and reports; a `GTEST_SKIP` with a
recorded reason is allowed only with the owner's explicit sign-off in the ledger, and the
gate flip in A6 waits for it either way.

**A6. CI gate.** When the sanitized ctest run reports zero failures on the PR, the
`integration-asan` job's ctest step loses `continue-on-error: true` and moves after the
`Run integration suite` step, so a unit-test regression never hides the integration signal
the job exists for; artifacts already upload on `always()`. `ASAN_OPTIONS` keeps
`alloc_dealloc_mismatch` at its default (on). The plain job's `make test` step is already
blocking. Checkpoint: both CI jobs green on PR #309 before Phase B starts.

### Phase B: harness change and scenario suite

**B1. `harness affects` subcommand, person affects only.** Why `harness tick` cannot make
a slow person affect deterministic: `get_current_time_phase()` derives the phase from the
real-time `pulse` counter (20 phases of 12 pulses; a game hour is 60 s real), so
`affect_update_person` ticks a slow affect (`is_fast == 0`, poison among them) only on the
one matching phase per game hour, and `harness tick` also runs `point_update` regen.

Design: a harness-only global `harness_force_affect_phase` in `test_harness.h/.cpp`,
default off, OR-ed into the single person-affect phase compare in `affect_update_person`
(`src/limits.cpp`, the `!mode && time_phase == af->time_phase` clause). `do_harness affects`
sets the flag, calls `affect_update()` then `clean_expose_elements()`, clears the flag, and
answers "Harness: affect tick complete." `harness tick` is unchanged; the usage text lists
both subcommands. A gtest in `test_harness_tests.cpp` pins that the flag makes a slow
person affect tick once and that it is off by default.

Room affects are explicitly out of this subcommand's guarantee. Their application roll
(`number(0, 12)` per occupant, plus a one-in-three roll for fast spells) is not phase-gated,
and blaze is a fast spell, so the flag changes nothing for blaze; forcing the room duration
compare would burn duration on slow room affects (mist, poison's room arm) faster than they
fire. The two room-affect compares in `affect_update_room` are left alone; blaze and mist
scenarios keep marker-driven waits under the seeded RNG.

Guarantee, stated precisely: each `affects()` call forces one tick per person affect. The
wall-clock fast block still runs `affect_update` every three seconds in harness mode (an
explicit earlier decision: `aabcc1f` reverted gating it because two scenarios depend on the
spontaneous ticks), so a slow affect can also tick on its own about once a minute.
Scenarios therefore assert monotonic outcomes (hit points strictly lower, affect gone,
death recorded), never exact tick counts or damage totals. Poison scenario contract: set the
victim's hit points low with `wizset`, then issue `affects()` calls back to back until the
death marker or a small budget; death needs `hit <= -CON/2` at 5 damage per tick, so the
budget is `ceil((hit + CON/2) / 5) + 2`. The existing xfail is removed and its reason text
(which wrongly says the phase advances only on `harness tick`) goes with it.

**B2. Scenario suite.** Each scenario is one pytest file named for the behaviour it pins,
uses only marker-driven waits, cites the `manual-test-plan.md` item it automates in its
docstring, and asserts on transcripts and JSON records as the pilots do. Existing files:
`test_blaze_after_quit.py`, `test_poison_remote_player.py` (poisoner online, transferred out
of the room), `test_remote_credit_xp_split.py`.

Pinned assertions (from `src/fight.cpp`): gentle poison death is `hit == max_hit / 4`,
mana 0, stats unchanged, `EXPLOIT_POISON` only; harsh death is `hit == 1`, each stat scaled
by two thirds, an `EXPLOIT_MOBDEATH` record naming the poisoning mob or else the engaged
mob; a player poisoner keeps an `EXPLOIT_PK` record. A slain player with a live descriptor
keeps the same `char_data` (extract_char re-places the body), so `resolve_poisoner` still
resolves a poisoner who died; only a quit (which frees the body) makes attribution "nobody".

Roster: a fifth character `Harncaller`, a human level-30 mage knowing `summon` and `blaze`.
Needed because `spell_summon` fails whenever caster and victim are on different sides and
`Harnmage` is a magus while the rest of the roster is not; a human summoner shares a side
with the wood-elf victim. The same character is the "another character logs in" body for
the blaze relogin row. `STANDARD_ROSTER` in `fixtures.py` gains the entry; the unit tests
that count roster files move by one.

| Scenario | Pins | New need |
|---|---|---|
| Blaze tick after the caster is slain by the imp | tick fires and kills the victim; the victim's exploits name the mage, because a slain player keeps its body and registration serial and the room-affect owner check (`affect_update`, pointer plus serial) still resolves it; no crash | none |
| Blaze tick after the caster's link drops and `Harncaller` logs in | same, plus the login reusing the slot | `Harncaller` |
| Poison, poisoner quits before the lethal tick | `EXPLOIT_POISON` present, no death record naming anyone, no PK record for the mage, no crash | `affects()` |
| Poison, poisoner slain by the imp before the lethal tick | records still name the mage (same assertions as the existing online scenario) | `affects()` |
| Snake poison, flee two rooms, die alone | gentle assertions above | none (snake 1131, corridor 1134/1135 present) |
| Die still fighting the snake | harsh assertions, `EXPLOIT_MOBDEATH` names the snake | real combat rounds |
| Player poison, die fighting the brute | harsh assertions, `EXPLOIT_MOBDEATH` names the brute, mage keeps `EXPLOIT_PK` | none (brute 1133 present) |
| Killing blow from a non-engaged spell | credit to the caster, no melee credit | none |
| Splash bystander manufactures no credit | bystander 1132 has no record | none |
| Summon by name in the dark room | victim relocates to the caster's room (imp `stat`), both see the arrival lines | `Harncaller`; cast retry budget for the seeded save roll |
| Summon a link-dead character | no crash under ASan, victim relocates, relogin lands in the caster's room; the null `desc` paths in `act` and `do_look` are the ones under test (manual-test-plan item 25) | `Harncaller`, `drop_link` (present) |
| Earthquake message order | the caster's fall line is last among the fall lines | one arena room gains a plain `DOWN` exit to a new sink room (a plain down exit makes the crevice open deterministically); the imp stays out of the room; casts retry within a budget until at least one other fall line appears |
| Mass affect expiry with a death in the same `affect_update` | a victim whose own affects are at duration 1 dies to the blaze room tick inside the same `affect_update()` in which they expire, after a quit issued just before `affects()`; no crash under ASan | `affects()` |

The server is single-threaded, so "quits in the same tick" is realised as the ordering
above, not a race. The world change is the sink room and its exit; everything else in the
table is present (mobs 1130-1134, the `DARK` room 1133, the corridor chain). The library
extraction tool stays deferred. CI budget: the current suite runs in 142 s sanitized; the
added scenarios each boot their own server, so the task measures the new total on the first
green run and records it against the 30-minute job timeout.

**B3. Exit criteria.** Both CI jobs green with the ctest step blocking; the integration
suite has no xfail; every catalogue row above has a scenario or a recorded reason it cannot
be automated; WIP.md carries a dated slice 2 status line and the object-refresh finding is
closed.
