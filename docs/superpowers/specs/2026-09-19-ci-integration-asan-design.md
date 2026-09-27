# CI integration job with AddressSanitizer — design

**Date:** 2026-09-19 · **Branch:** `fix/spell-room-affect-uaf-port` · **Status:** approved in
chat; revised after a dual adversarial review (Fable and Opus reviewers, 2026-09-19)

This is slice 3 of the integration test harness
(`2026-09-19-integration-test-harness-design.md`). Slice 1 delivered the harness and a
green suite (41 passed, 1 xfailed) run locally through Docker. This slice makes CI the gate:
a native Linux job builds the server under AddressSanitizer and runs the suite, so a
use-after-free fails the run instead of depending on whether the freed memory happened to be
reused.

## Decisions taken

- A **separate job** rather than a step on the existing `build-test-smoke` job or a matrix.
- The sanitized build compiles **both** `ageland` and `ageland_tests`. Both builds are
  blocking. The **ctest run** is blocking since 2026-09-21, after the fix wave took the
  sanitized unit suite to zero failures, and runs after the integration suite. The integration
  suite is **blocking from day one**: a pre-existing ASan finding in a scenario path is a real
  defect and is fixed on this branch before merge.
- The sanitizer recipe lives in **CMake and the Makefile**, not only in the workflow YAML, so
  a developer on Linux reproduces the CI build with one variable.
- The **existing job is repaired**, not left untouched. It has been red since 2026-08-14
  (34 of the last 40 runs failed) because the apt list installs only the 64-bit crypt library
  and the server links `-lcrypt` as `-m32`. Both jobs share the fix.
- The two targets are **different ABIs**: `ageland` is 32-bit (`-m32`, `src/CMakeLists.txt`)
  and `ageland_tests` is 64-bit (no `-m32`, links the host `GTest::gtest`). The server uses
  the 32-bit runtime (`lib32asan8` on Ubuntu 24.04); the test binary uses the ordinary 64-bit
  `libasan8`. Neither package is installed explicitly today; both are pulled in transitively
  by `gcc-multilib` and `g++` on noble. This cannot be verified offline, so the job installs
  both by name and proves the server binary linked its runtime (see the proof steps).

## Build configuration

### CMake

`src/CMakeLists.txt` gains one cache variable, declared next to `ROTS_SUPPRESS_TEST_WARNINGS`
(that one is a boolean `option()`; this is a `CACHE STRING` because it takes a name):

```cmake
set(ROTS_SANITIZE "" CACHE STRING "Sanitizer to enable for ageland and ageland_tests (empty or address)")
```

- Empty (the default) changes nothing.
- `address` adds `-fsanitize=address -fno-omit-frame-pointer` to the compile options and
  `-fsanitize=address` to the link options of both `ageland` and `ageland_tests`, through the
  existing `target_compile_options` / `target_link_options` calls.
- Any other non-empty value is a `FATAL_ERROR` at configure time, so a typo cannot silently
  produce a plain build.

Both targets keep writing to `bin/`, so an ASan tree and a plain tree overwrite the same
`bin/ageland`. That is acceptable: each CI job is its own runner, and the local caveat is
documented.

### Makefile

A new optional variable `SANITIZE ?=`. The configure rule **always** passes
`-DROTS_SANITIZE=$(SANITIZE)` (empty re-asserts the default). The rule only runs when
`$(BUILD_DIR)/CMakeCache.txt` is absent, so changing `SANITIZE` needs a fresh `BUILD_DIR`;
`make help` and the README say so. `BUILD_DIR` is overridable on the command line despite
its `:=` (verified), so the local recipe is:

```sh
make BUILD_DIR=build-asan SANITIZE=address configure   # fresh directory
make BUILD_DIR=build-asan build
make BUILD_DIR=build-asan test
```

No new targets.

## Harness changes

### Launcher environment

`LocalProcessLauncher.start` (`tests/integration/rots_harness/launcher.py`) builds the server
environment from scratch with only `PATH`, `HOME`, and `ROTS_RANDOM_SEED`. The construction
moves into a pure method, `LocalProcessLauncher.environment(seed) -> dict[str, str]`, which
`start` calls; the unit test calls it directly, the way `command()` is already tested, with
no spawned process. It gains a fixed passthrough list copied from the host environment when
present: `ASAN_OPTIONS`, `LSAN_OPTIONS`, `UBSAN_OPTIONS`, `ASAN_SYMBOLIZER_PATH`. The
clean-environment guarantee is a property of the local launcher only; the Docker launcher
already hands the host environment to the compose client and forwards only the seed into the
container. It is unchanged.

Unit test in `tests/integration/unit/test_launcher.py`: with `ASAN_OPTIONS` set in the host
environment the returned mapping contains it; with it unset the key is absent; unrelated host
variables never appear.

### Run-directory retention

Two paths currently delete the evidence an ASan report produces:

- **Teardown detection.** The server fixture keeps the run directory only when
  `request.session.testsfailed > 0`. A report found by the autouse crash check during
  teardown is logged by pytest *after* all finalizers, so the count is still zero when the
  fixture decides, and `game.log` is removed. (Reproduced by the Opus reviewer with a
  synthetic fixture of the same shape.)
- **Boot failure.** If the server exits before it listens, `launcher.start` raises and the
  fixture removes the directory; only the last 4000 bytes of the log survive in the exception
  text, which can cut off a report's header.

Fix in `conftest.py`: the run directory is kept whenever `ROTS_IT_KEEP=1`, whenever any test
has failed so far, **or whenever this fixture's own start or crash check raised**. The
CI step also sets `ROTS_IT_KEEP=1` so every run directory is uploaded on a red run
(each holds about 1.3 MB of lib copy; the artifact is bounded with `retention-days: 7`).
`read_log_tail`'s default rises to 16 KB so a boot-time report fits in the exception text.

### Crash monitor

Unchanged. Server stderr is already merged into `game.log`, which the monitor reads, and any
line containing `AddressSanitizer` is a crash marker. The post-test check runs before the
server fixture stops the server: `fail_on_server_crash` requests `server` through
`getfixturevalue` inside its body, so the server finalizer is registered first and finalizers
run last-in-first-out. This depends on that arrangement, not on autouse ordering, and the
conftest comment says so.

Two known blind spots, recorded in the README rather than fixed here: a report raised inside
the server's own shutdown path (SIGTERM runs `hupsig`, which saves every player and calls
`exit(0)`) happens after the last check, and the run directory is removed on success; and the
server installs its own `SIGSEGV` handler in `main`, which replaces ASan's unless
`handle_segv=2` is set (the job sets it, see below).

## CI job

### Shared install step

Both jobs' apt step adds `sudo dpkg --add-architecture i386` before `apt-get update` and
installs `libcrypt-dev:i386` (the README's documented prerequisite; the fallback package name
`libxcrypt-dev:i386` is noted). The new job also installs `python3-pytest`, `lib32asan8` and
`libasan8` explicitly, and omits `cargo`, `rustc`, and `clang-format`, which it does not use.
This repairs the plain job as a side effect; it is the one change to that job.

### `integration-asan`

A second job in `.github/workflows/ci.yml` (triggers are workflow-level, so both jobs share
them). No `needs`, so the jobs run in parallel. `ubuntu-24.04`,
`timeout-minutes: 45` as a **provisional** budget: the last green run of the existing job took
about 4 minutes end to end, so the new job's time is dominated by 42 tests each booting and
stopping a sanitized server, which has never been measured natively. The figure is revised
from the first run.

Job-level environment:

```
ASAN_OPTIONS: detect_leaks=0:handle_segv=2
```

`detect_leaks=0` matters for **both** binaries: the gtest binary exits normally, and so does
the server, whose SIGTERM handler ends in `exit(0)`, so LeakSanitizer (supported on i386
Linux) would otherwise scan the heap at every fixture teardown, print a leak report into
`game.log`, and slow shutdown against the launcher's 10-second grace. Leaks are not the
target. `handle_segv=2` keeps ASan's own SEGV report instead of the server's legacy
backtrace, so a wild-pointer fault is classified by address. The ctest step additionally
appends `detect_container_overflow=0`, because the 64-bit test binary links the
uninstrumented system gtest and shares standard containers with instrumented code, the
classic source of spurious container-overflow reports.

Steps, in order:

1. Check out; shared install step as above.
2. `sudo sysctl -w vm.mmap_rnd_bits=28`. Ubuntu 24.04's raised ASLR entropy is a known ASan
   startup failure ("shadow memory range interleaves with an existing memory mapping"); this
   is the standard mitigation, runner-local and harmless. Whether the current image still
   needs it cannot be checked offline.
3. `make BUILD_DIR=build-asan SANITIZE=address configure`, then
   `make BUILD_DIR=build-asan setup` and a build of **both** targets
   (`cmake --build build-asan --target ageland ageland_tests`), blocking, so a compile or link
   break under ASan is never hidden inside the ctest step.
4. Proof steps, blocking:
   - `ldd bin/ageland | grep -E 'libasan\.so[^ ]* => /'` proves the configured tree really
     enabled the sanitizer and the 32-bit runtime resolved. (A missing runtime fails at link
     time in step 3; this step guards the silent-plain-build case, not the missing-package
     case.)
   - `(timeout 30 env ASAN_OPTIONS=help=1 ./bin/ageland -t -d /nonexistent 1 2>&1 || true) | grep -q 'Available flags for AddressSanitizer'`
     proves the runtime initialises and its output reaches stderr, on every run. This replaces
     the manual negative check the earlier draft asked a human to perform.
5. `make integration` with `ROTS_IT_LAUNCHER=local` and `ROTS_IT_KEEP=1`, blocking.
6. `ctest --test-dir build-asan --output-on-failure` with the container-overflow flag appended,
   blocking since 2026-09-21 (the fix wave took the sanitized unit run to zero failures); it runs
   after the integration suite so a unit regression never hides the integration signal.
   `gtest_discover_tests` runs in `PRE_TEST` mode, so the test binary is not executed at build
   time; discovery happens inside this step.
7. `actions/upload-artifact@v4` with `build/integration/**`, `if: always()` (a job that hits
   `timeout-minutes` is cancelled, and `failure()` steps do not run on cancellation),
   `if-no-files-found: ignore`, `retention-days: 7`. The directories hold each run's
   `game.log`, transcripts, and a copy of the tracked `lib/text`, `lib/misc`, and synthetic
   world; nothing in there is real player data.

`make integration` picks the right binary: `ROTS_IT_BINARY` defaults to `bin/ageland` under
the repo root, which is where the sanitized server lands regardless of `BUILD_DIR`, and run
directories are always `build/integration/<id>`. `permissions: contents: read` is sufficient
for the upload action.

## Verification

Nothing in this slice can be executed end to end on the development Mac: the sanitized 32-bit
build needs a native Linux toolchain, and the i386 container is a different libc with no ASan
runtime. The verification is split accordingly.

- **Local:** the launcher and conftest unit tests through the existing venv
  (`make integration-unit`). The CMake change is checked by a plain configure inside the
  container, which exercises the empty default and the fatal-error branch for a bad value
  without building.
- **CI:** the two proof steps are the acceptance test for the build. The **first run is
  diagnostic**, not assumed green: the server has never run under any sanitizer, the harness
  timeouts are fixed (8 s per command, 12 s per cast, 15 s per login, 60 s startup) and ASan
  costs two to three times the CPU on a shared runner. Expected outcomes and what each means:
  - all scenarios pass, one xfail (non-strict, so an XPASS is also fine): done;
  - an ASan report in a scenario path: a defect, fixed on this branch before merge;
  - timeouts only: the fixed harness timeouts become env-tunable in a follow-up commit on
    this branch and the run is repeated.
- **pytest on the runner:** the harness needs pytest 7 or newer (the `pythonpath` ini key)
  and Python 3.10 or newer; nothing newer is used. The local baseline ran on pytest 9.1 and
  Python 3.14, so 7.4 on 3.12 is untested until the first run. Ubuntu's Python is externally
  managed, so the apt package avoids a venv.

## Documentation

- `tests/integration/README.md`: the ASan recipe (the two make variables and the
  fresh-directory rule), the shared `bin/` caveat, the `ASAN_OPTIONS` values and why, the two
  blind spots above, and that the CI job is the gate while Docker runs stay for development.
  Also correct the pre-existing wording that calls `build/sp` a "sanitizer-free build" (it is
  the stack-protector build) while the file is open.
- `tests/integration/pytest.ini`: the marker text says "session-scoped server fixture"; the
  fixture is function-scoped. Fixed while touching the harness.
- `WIP.md`: one dated line under the harness section.
- `2026-09-19-integration-test-harness-design.md`: the slice 3 paragraph is updated to record
  that the runtime is available and that the existing job was repaired, replacing the
  conditional wording.

## Out of scope

Slice 2 scenarios, the no-regen `harness` tick, the object-refresh finding, gtest under ASan
as a blocking step, making the harness timeouts tunable (unless the first run shows it is
needed), and any change to the existing CI job beyond the shared install step.
