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

Approved in chat on 2026-09-20. This section supersedes the two places above and in the
slice 3 spec that describe the sanitized ctest step as non-blocking and the poison scenario
as an xfail. Everything lands on `fix/spell-room-affect-uaf-port` and draft PR #309; the
plan runs in two phases with a CI-green checkpoint between them.

### Phase A: fix wave

Ordering rule: server defects first, then unit-test fixtures, then the CI gate flip. A
task that finds a real server bug inside a class that was assumed to be test-only fixes it
as a server defect with a regression gtest and records the reclassification in the ledger.

**A1. `fread_string` size check (`src/db.cpp`).** The check `strlen(tmppoint) +
strlen(buf) > MAX_STRING_LENGTH` admits equality, after which the non-terminator branch
writes `'\r'` at index `MAX_STRING_LENGTH` and `'\0'` at `MAX_STRING_LENGTH + 1`, two bytes
past `buf`. The fix rejects equality. Because `strcat` needs its own terminator and the
branch appends two bytes, the invariant the check must hold is
`strlen(buf) + strlen(tmppoint) + 2 <= MAX_STRING_LENGTH`; the task states the exact
expression it chooses in the ledger and the regression gtest feeds a line that lands on
every boundary value (fits, equal, one over). Behaviour for every shorter input is
unchanged.

**A2. Object-file refresh round-trip (`src/objsave.cpp`).** Root cause, established by
reading the three writers: `Crash_crashsave` and `Crash_rentsave` write rent code, objects,
aliases and then `Crash_follower_save` (which always ends with the `-17` follower
sentinel); `Crash_idlesave` writes rent code, objects and aliases and stops. The refresh
then reads the idle file with the strict `object_save_data_from_binary`, which requires the
follower section, and reports "Truncated objects data while reading follower record". The
legacy reader (`legacy_object_save_data_from_binary`) tolerates the missing section, which
is why the on-disk file loads at login and the defect was invisible before the harness.

Fix: `Crash_idlesave` calls `Crash_follower_save(ch, fp)` before closing, matching the other
two writers, so every file the server writes has the follower section. The three writers
share the section order already; no reader change. A gtest builds the idle-save byte
stream for an empty-inventory character through the real writer functions into a temp
file and asserts the strict reader accepts it; a second case covers a character with one
follower so the section's content, not only its presence, round-trips. After the fix the
"Truncated objects data while reading follower record" fragment is removed from the crash
monitor's allow-list in `tests/integration/rots_harness/crashmonitor.py`, so any refresh
SYSERR fails a scenario again, and the WIP.md finding is closed.

Open question for the task, not for this spec: `Crash_follower_save` skips followers not in
the character's room and does not extract them; whether idle rent should also extract
saved followers as `Crash_rentsave` does is a behaviour question. The task keeps the change
to the file format (add the section) and records the extraction question as a finding.

**A3. The four `OlogHaiHelpers` gtests.** Pre-existing failures in
`src/tests/olog_hai_tests.cpp` that keep the plain `build-test-smoke` job red. Root-cause
each; a test asserting stale behaviour is corrected, a real defect is fixed in the server.
The task reports which of the two it found for each case.

**A4. Test character allocation.** The server allocates `char_data` with `CREATE`
(calloc) everywhere and `free_char` releases with `free`. About 42 test sites across eight
files (`interpre_account_menu_tests.cpp` holds 33) allocate with `new char_data {}` and
release through `free_char`, which AddressSanitizer reports as alloc-dealloc-mismatch.
Design: one new header `src/tests/test_character_support.h` with

- `char_data* allocate_test_character()`: `CREATE`-style calloc plus `clear_char(…, MOB_VOID)`,
  the exact pair every current site performs after `new`.
- `void release_test_character(char_data*)`: `free_char` for characters the test hands to
  server code that expects to free them, so the helper documents the ownership rule in one
  place.

Every `new char_data {}` in `src/tests/` is converted; `delete` of a `char_data` disappears
from the tests. `ScopedObjectPrototypeTable` in `db_loader_tests.cpp` deletes `obj_data`
nodes the loader created with `CREATE`; it releases them with `free` (or the server's own
release call when one exists), and its `obj_proto`/`obj_index` arrays stay `new[]`/`delete[]`
because the fixture itself allocates them.

**A5. Remaining sanitized gtest classes.** Triage by grouping the ASan reports from one
local sanitized run of `ageland_tests` (`make BUILD_DIR=build-asan SANITIZE=address` recipe
in the harness README) by top frame:

- 49 global-buffer-overflow reports under `printf_common`: a format call reading past a
  global; expected to be a small number of root causes (a fixed-width global name or table
  formatted with `%s` without a terminator, or a table indexed one past its end).
- 4 SEGV in `act()` (`src/comm.cpp`), 1 SEGV in `obj_from_room` (`src/handler.cpp`), 2
  heap-use-after-free in `json_utils::JsonReader::skip_whitespace` (`src/json_utils.cpp`),
  about 12 plain assertion failures.

Each root cause becomes one task with its own regression test. The two JSON-reader
use-after-frees are treated as server defects until proven otherwise, because the reader
runs on player files. The counts above are from the slice 3 ledger and may have moved
after A4; the triage task records the actual grouping before fixing.

**A6. CI gate.** When the sanitized ctest run reports zero failures on the PR, the
`integration-asan` job's ctest step loses `continue-on-error: true`. `ASAN_OPTIONS` keeps
`alloc_dealloc_mismatch` at its default (on); the fixtures were fixed rather than the
check silenced. The slice 3 spec's "ctest run is non-blocking" sentence is superseded by
this paragraph. Checkpoint: both CI jobs green on PR #309 before Phase B starts.

### Phase B: harness change and scenario suite

**B1. `harness affects` subcommand.** Why `harness tick` cannot make a slow affect
deterministic: `get_current_time_phase()` derives the phase from the real-time `pulse`
counter, so `affect_update_person` and `affect_update_room` tick a slow affect
(`is_fast == 0`, poison among them) only on the one matching phase per game hour, while
`harness tick` runs at whatever pulse the command arrives on and also runs `point_update`,
whose regen competes with the damage.

Design: a harness-only global `harness_force_affect_phase` (in `test_harness.h/.cpp`,
default off) that the two phase comparisons in `src/limits.cpp` OR into their condition:
`(time_phase == af->time_phase) || harness_force_affect_phase`. `do_harness affects` sets
the flag, calls `affect_update()` then `clean_expose_elements()`, clears the flag, and
answers "Harness: affect tick complete." One call is exactly one duration decrement and one
damage or effect application per affect, person and room, with no `point_update`,
`stat_update`, `fast_update` or weather. `harness tick` is unchanged. The gate is two
one-line edits in `limits.cpp` (CRLF; byte-preserving edit) plus the flag; no other server
code changes. The command's usage text lists both subcommands.

The `harness` pytest fixture gains `affects()` beside `tick()`. The existing poison
scenario replaces its "tick until dead or budget spent" loop with `affects()` calls and
drops the xfail. The `-t` gate stays: the flag can only be set by the command, which only
exists in harness mode.

**B2. Scenario suite.** The catalogue above, minus the three scenarios already present
(blaze after quit, remote poison, remote-credit split). Each scenario is one pytest file
named for the behaviour it pins, uses only marker-driven waits, and asserts on transcripts
and JSON records as the pilots do:

| Scenario | Pins | New need |
|---|---|---|
| Blaze tick after the caster dies (slain by imp) | tick fires, victim's exploits name nobody, no crash | none |
| Blaze tick after the caster's link drops and another character logs in | same, plus the relogin reusing the slot | a fourth roster character or a second login of the fighter |
| Poison, poisoner dies before the lethal tick | victim death attributed to nobody | `affects()` |
| Poison, control with the poisoner online | victim death attributed to the mage, `EXPLOIT_POISON` | `affects()` |
| Snake poison, flee two rooms, die alone | gentle: hit points at a quarter, no stat loss, `EXPLOIT_POISON` only | snake reset, lit exit chain (present) |
| Die still fighting the snake | harsh penalty, mob-death record | real combat rounds |
| Player poison, die fighting the brute | harsh penalty, poisoner keeps the PK record | brute mob 1133 (in the world spec) |
| Killing blow from a non-engaged spell | credit to the caster, no melee credit | none |
| Splash bystander manufactures no credit | bystander 1132 has no record | none |
| Summon by name in the dark room | summon succeeds, message set | `DARK` arena room (present) |
| Summon a link-dead character | summon refuses or succeeds per branch rule, no crash | socket drop helper (present) |
| Earthquake message order | the caster's fall line is last | three or more targets in room |
| Mass affect expiry while a character quits in the same tick | no crash under ASan | `affects()` and a quit racing it |

The world file changes are limited to what the last column names; the plan's first Phase B
task diffs the current `tests/integration/world/` against the world table above and adds
only the missing records. The library extraction tool stays deferred. Where a scenario
depends on a harsh-penalty or attribution rule the branch defines in `manual-test-plan.md`,
the scenario cites the plan item in its docstring so a later rule change points at the test
to update.

**B3. Exit criteria.** Both CI jobs green with the ctest step blocking; the integration
suite has no xfail; every catalogue row above has a scenario or a recorded reason it cannot
be automated; WIP.md carries a dated slice 2 status line and the object-refresh finding is
closed.
