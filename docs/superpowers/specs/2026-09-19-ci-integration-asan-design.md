# CI integration job with AddressSanitizer — design

**Date:** 2026-09-19 · **Branch:** `fix/spell-room-affect-uaf-port` · **Status:** approved in chat

This is slice 3 of the integration test harness
(`2026-09-19-integration-test-harness-design.md`). Slice 1 delivered the harness and a
green suite (41 passed, 1 xfailed) run locally through Docker. This slice makes CI the gate:
a native Linux job builds the server under AddressSanitizer and runs the suite, so a
use-after-free fails the run instead of depending on whether the freed memory happened to be
reused.

## Decisions taken during brainstorming

- A **separate job** rather than a step on the existing `build-test-smoke` job or a matrix.
  The existing job is untouched.
- The sanitized build compiles **both** `ageland` and `ageland_tests`. The gtest run is
  **non-blocking** (`continue-on-error`) so pre-existing sanitizer findings in the unit tests
  are visible without turning the new job red on day one. The integration suite is blocking.
- The sanitizer recipe lives in **CMake and the Makefile**, not only in the workflow YAML, so
  a developer on Linux reproduces the CI build with one variable.
- The 32-bit ASan runtime is packaged for Ubuntu 24.04 (`lib32asan8`, pulled in by
  `gcc-multilib`), so the fallback the slice 1 spec left open (plain build, ASan documented as
  local-only) is not needed. The job proves the runtime linked rather than assuming it.

## Build configuration

### CMake

`src/CMakeLists.txt` gains one cache option, declared next to `ROTS_SUPPRESS_TEST_WARNINGS`
in the same style:

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

A new optional variable `SANITIZE ?=`. When non-empty, the configure rule appends
`-DROTS_SANITIZE=$(SANITIZE)` to the CMake invocation. `BUILD_DIR` is already overridable on
the command line, so the full local recipe is:

```sh
make BUILD_DIR=build-asan SANITIZE=address configure
make BUILD_DIR=build-asan build
make BUILD_DIR=build-asan test
```

`make help` gains one line describing `SANITIZE`. No new targets.

## Harness change

`LocalProcessLauncher.start` (`tests/integration/rots_harness/launcher.py`) builds the server
environment from scratch with only `PATH`, `HOME`, and `ROTS_RANDOM_SEED`. It gains a fixed
passthrough list, `ASAN_OPTIONS` and `UBSAN_OPTIONS`, copied from the host environment when
present. The clean-environment guarantee stays; only sanitizer tuning crosses over. The Docker
launcher is unchanged (the i386 container never runs a sanitized binary).

One unit test in `tests/integration/unit/test_launcher.py`, following the existing pattern
there: with `ASAN_OPTIONS` set in the host environment the launcher's environment contains it;
with it unset the key is absent.

No crash-monitor change. Server stderr is already merged into `game.log`, which the monitor
reads, and the monitor already treats any line containing `AddressSanitizer` as a crash
marker. The post-test check runs before the server fixture sends SIGTERM, so shutdown never
masks a report. An ASan report therefore fails the test that triggered it, with the full
report in the kept run directory.

## CI job

A second job, `integration-asan`, in `.github/workflows/ci.yml`. Same triggers as the existing
job, no `needs` (both run in parallel), `ubuntu-24.04`, `timeout-minutes: 45`.

Job-level environment: `ASAN_OPTIONS=detect_leaks=0`. Leaks are not the target and the legacy
server leaks by design at exit; without this, the normally-exiting gtest binary would drown in
LeakSanitizer noise. Everything else stays at ASan defaults, so the first memory error prints
a report and exits non-zero, which the crash monitor catches through both the marker and the
exit status.

Steps, in order:

1. Check out; install the existing apt list plus `python3-pytest` (Ubuntu's Python is
   externally managed, so the apt package avoids a venv; the harness needs pytest 7 or newer
   for the `pythonpath` ini key, and noble ships 7.4).
2. `make BUILD_DIR=build-asan SANITIZE=address configure`, then
   `make BUILD_DIR=build-asan setup build`.
3. Proof that the sanitizer linked: `ldd bin/ageland | grep -q libasan`. Fails the job if the
   32-bit runtime is missing.
4. `make BUILD_DIR=build-asan test` with `continue-on-error: true`.
5. `make integration` with `ROTS_IT_LAUNCHER=local`, blocking.
6. On failure, `actions/upload-artifact@v4` with `build/integration/**` (each kept run's
   `game.log`, transcripts, and synthetic lib copy; nothing in there is real player data).

The existing job keeps its name, steps, and smoke flow. No `permissions` change is needed.

## Verification

Nothing in this slice can be executed end to end on the development Mac: the sanitized 32-bit
build needs a native Linux toolchain, and the i386 container is a different libc with no ASan
runtime. The verification is split accordingly.

- **Local:** the launcher unit test through the existing venv (`make integration-unit`). The
  CMake change is checked by a plain configure inside the container, which exercises the
  empty default and the fatal-error branch for a bad value without building.
- **CI:** the `ldd` proof step is the acceptance test for the build. The expected suite result
  matches the local baseline: all scenarios passing with the one xfail
  (`test_poison_remote_player`, the slice-2 timing item).
- **Negative check, manual and one-time:** no throwaway commit reintroducing a use-after-free
  is pushed. Instead `tests/integration/README.md` explains how to confirm the pipeline by
  running one scenario on Linux against an ASan build with a deliberately invalid
  `ASAN_OPTIONS` value. The runtime refuses to start, so the launcher reports the server
  exiting before it listened, with the sanitizer's parse error in the quoted log tail. That
  proves the runtime is active and its output is captured. Whether to run it once on the
  runner is the owner's call.

## Documentation

- `tests/integration/README.md`: the ASan recipe (the two make variables), the shared `bin/`
  caveat, `detect_leaks=0` and why, the manual negative check, and that the CI job is the gate
  while Docker runs stay for development.
- `WIP.md`: one dated line under the harness section.
- `2026-09-19-integration-test-harness-design.md`: the slice 3 paragraph is updated to record
  that the runtime is available, replacing the conditional wording.

## Out of scope

Slice 2 scenarios, the no-regen `harness` tick, the object-refresh finding, gtest under ASan
as a blocking step, and any change to the existing CI job.
