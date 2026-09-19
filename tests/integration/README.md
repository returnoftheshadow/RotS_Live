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
make integration-unit PYTHON=build/integration-venv/bin/python   # no server, 32 tests
make integration      PYTHON=build/integration-venv/bin/python   # boots a server
```

`make integration` needs either a native Linux build at `bin/ageland`, or Docker on
macOS: set `ROTS_IT_LAUNCHER=docker`. If the plain build crashes under QEMU, point at
the sanitizer-free build instead: `ROTS_IT_BINARY=build/sp/bin/ageland`.

## Environment variables

- `ROTS_IT_LAUNCHER` — `local` or `docker` (default: `local` on Linux, `docker` elsewhere).
- `ROTS_IT_BINARY` — server binary path, relative to the repo root (default: `bin/ageland`).
- `ROTS_IT_SEED` — deterministic RNG seed passed to the server (default: `20260919`).
- `ROTS_IT_KEEP=1` — keep the run directory after the session even if all tests passed.
- `ROTS_IT_DOCKER_LOCK_DIR` — override the shared Docker lock directory (default:
  `/tmp/rots-docker-lock`).

## Shared Docker lock

Only one integration run can use the shared Docker host at a time. The launcher checks
`/tmp/rots-docker-lock` (see `/tmp/rots-docker-lock/README.txt`) and refuses to start while
any other `*.lock` file is present there. A successful start writes its own
`uaf-port-harness-it.lock`, removed when the run finishes.

## Run output

Each session creates `build/integration/<run-id>/`, containing the seeded `lib/`, the
server's `game.log`, and one transcript per logged-in character. This directory is deleted
after a passing run unless a test failed or `ROTS_IT_KEEP=1` was set, in which case it is
kept and its path is printed.
