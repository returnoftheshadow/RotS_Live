# Integration test harness

Boots a real `ageland` server against a throwaway lib directory seeded with fixture
characters, drives it over telnet, and asserts on the transcripts. `tests/integration/unit`
tests the harness modules themselves (`rots_harness/`) without starting a server.

## One-time setup

```sh
python3 -m venv build/integration-venv
build/integration-venv/bin/pip install pytest
```

(The host `python3` cannot take `--user` installs under PEP 668; use the venv instead.)

## Running

```sh
make integration-unit PYTHON=build/integration-venv/bin/python   # no server, 43 tests
make integration      PYTHON=build/integration-venv/bin/python   # boots a server
```

`make integration` needs either a native Linux build at `bin/ageland`, or Docker on
macOS: set `ROTS_IT_LAUNCHER=docker`. If the plain build crashes under QEMU, point at
the stack-protector build instead: `ROTS_IT_BINARY=build/sp/bin/ageland`.

## Environment variables

- `ROTS_IT_LAUNCHER` — `local` or `docker` (default: `local` on Linux, `docker` elsewhere).
- `ROTS_IT_BINARY` — server binary path, relative to the repo root (default: `bin/ageland`).
- `ROTS_IT_SEED` — deterministic RNG seed passed to the server (default: `20260919`).
- `ROTS_IT_KEEP=1` — keep the run directory after the session even if all tests passed.
- `ROTS_IT_DOCKER_LOCK_DIR` — override the shared Docker lock directory (default:
  `/tmp/rots-docker-lock`).

## AddressSanitizer

CI is the gate: the `integration-asan` job in `.github/workflows/ci.yml` builds the server
under AddressSanitizer on native Linux and runs this suite against it, so a use-after-free
fails the run. Docker runs on a Mac stay for development; the i386 container has no
sanitizer runtime.

To reproduce the CI build on Linux (the tree must be fresh; the configure rule only runs
when `<BUILD_DIR>/CMakeCache.txt` is absent, so changing `SANITIZE` needs a new directory):

```sh
sudo sysctl -w vm.mmap_rnd_bits=28   # Ubuntu 24.04 kernels: ASan aborts at start without it
make BUILD_DIR=build-asan SANITIZE=address configure
make BUILD_DIR=build-asan setup
cmake --build build-asan --target ageland ageland_tests -j"$(nproc)"
ASAN_OPTIONS=detect_leaks=0:handle_segv=2 make integration
```

Both build trees write the same `bin/ageland`; whichever was built last is what
`make integration` runs. `ageland` is 32-bit and uses `lib32asan`; `ageland_tests` is
64-bit and uses the ordinary `libasan`.

The local launcher forwards only `ASAN_OPTIONS`, `LSAN_OPTIONS`, `UBSAN_OPTIONS` and
`ASAN_SYMBOLIZER_PATH` from your environment to the server; everything else is dropped so
the server sees a clean environment. `detect_leaks=0` matters because the server exits
normally on SIGTERM (its handler saves and calls `exit(0)`), so LeakSanitizer would scan
the heap at every teardown; leaks are not what this suite is for. `handle_segv=2` keeps
ASan's own SEGV report instead of the server's legacy backtrace handler.

Two blind spots: a report raised inside the server's shutdown path (after the last crash
check, during the SIGTERM save) is not observed, and the run directory is removed on
success; and a report is only attributed to a test if it lands in `game.log` before that
test's teardown check.

A report found during a test's teardown, or a server that dies before it listens, keeps
its run directory. CI sets `ROTS_IT_KEEP=1` and uploads every run directory as the
`integration-run-directories` artifact.

## Shared Docker lock

Only one integration run can use the shared Docker host at a time. The launcher checks
`/tmp/rots-docker-lock` (see `/tmp/rots-docker-lock/README.txt`) and refuses to start while
any other `*.lock` file is present there. A successful start writes its own
`uaf-port-harness-it.lock`, removed when the run finishes.

## Run output

Each session creates `build/integration/<run-id>/`, containing the seeded `lib/`, the
server's `game.log`, and one transcript per logged-in character. This directory is deleted
after a passing run unless a test failed earlier in the session,
this test's own teardown check found a crash, the server failed to start, or
`ROTS_IT_KEEP=1` was set; in those cases it is kept and its path is printed.
