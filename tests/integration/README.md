# Integration harness

## What it is

Each scenario boots its own `ageland` server on a throwaway lib, logs characters in over
telnet, and checks what they see. The lib is built from the small test world in
`tests/integration/world` plus an account and characters that `bin/rotstool` writes from the
characters the scenario declares. Of a developer's `lib/`, only two parts are copied in:
`lib/text` exactly as it stands in the working tree, and those `lib/misc` files that git
tracks. Only `lib/misc` is limited to tracked files.

A scenario fails when the server shows trouble, even if its own checks pass:

- a crash marker in the server log (a sanitizer report or the `Error: signal` line the
  server prints when it catches a fatal signal);
- a `SYSERR` line the harness does not expect;
- the server exiting during the test, or, when it is stopped, exiting with a non-zero status or
  without logging its shutdown notice.

`rots_harness/` holds the pieces (telnet, characters, launchers, the lib builder, crash
checks, sessions); `conftest.py` turns them into pytest fixtures; `scenarios/` holds the
scenarios; `unit/` tests the harness itself without a server.

## Setup

1. Build the server, the harness build of the server (`ageland_harness`) and `rotstool`:

   ```bash
   make configure && cmake --build build --target ageland ageland_harness rotstool
   ```

   The binaries are 32-bit Linux programs. On a Mac, build them inside the Docker container
   (Ubuntu 22.04, which builds them with `-m32`):

   ```bash
   docker compose run --rm -T rots bash -lc 'make configure && cmake --build build --target ageland ageland_harness rotstool'
   ```

   That build is itself a Docker job writing this checkout's `build/` tree, so lock
   `build:<checkout>/build` for it by hand first (see Shared Docker lock below) and remove the
   lock when the build ends. A run whose server binary, `ageland_harness` (for a test with a
   schedule) or `rotstool` is missing stops at once with the build command for its launcher.

2. Create the virtual environment once, from the repository root (Python 3.9 or later):

   ```bash
   python3 -m venv .venv && .venv/bin/pip install -r tests/integration/requirements.txt
   ```

## Running

```bash
make integration-unit PYTHON=.venv/bin/python           # the harness's own tests; no server
make integration PYTHON=.venv/bin/python                # every scenario
make integration PYTHON=.venv/bin/python GROUP=boot     # one group
```

A group is a marker registered in `tests/integration/pytest.ini`, such as `boot`. `GROUP` may
be an expression such as `GROUP="boot or other"`; it always stays inside the `scenario` filter. A
name that neither `pytest.ini` nor pytest itself registers stops the run with a usage error that
lists the registered names, so a misspelt group never passes by selecting nothing.

Run one harness session per checkout at a time: every session in a checkout shares the template
cache under `build/integration/libs/`.

## Continuous integration

The `Integration` workflow (`.github/workflows/integration.yml`) runs on pull requests into
`master` and `release-frodo`, and runs every scenario on each push to `release-frodo`. A pull
request runs:

- `boot`, always;
- each group its description names as `@integration-test-<group>`, such as
  `@integration-test-poison`, or every group for `@integration-test-all`;
- the groups of each scenario file it changes;
- every group when it changes the harness: `rots_harness/`, `conftest.py`, `pytest.ini`,
  `requirements.txt`, the test world, a scenario file other than a `test_*.py`, `src/harness/`,
  `src/tools/rotstool/`, the `Dockerfile` or the workflow itself.

A change to the game's own code adds no group by itself, so name the groups whose behaviour it
touches. A name `pytest.ini` does not register fails the job and lists the registered groups.
Tokens inside an HTML comment (`<!-- -->`) are ignored. Editing the description reruns the job,
and the job summary lists the groups that ran and why. When a scenario fails, its kept run
directories are uploaded as the `integration-runs` artifact.

## Launchers

The harness runs the i386 programs in one of two ways:

- `local` runs them directly. It is the default on Linux, which needs the i386 runtime for it
  (for example `libc6:i386`); without it, set `ROTS_IT_LAUNCHER=docker`.
- `docker` runs each server and each `rotstool` call through `docker compose run` with this
  checkout's `rots` service. It is the default everywhere else. Every container is named
  `rots-it-...` and publishes the server on a random loopback port, so the service's fixed
  `rots` container name and host port 1024 are never used. Everything a container touches
  lives under the checkout, which the service mounts at `/rots`.

Sanitizer variables (`ASAN_OPTIONS`, `LSAN_OPTIONS`, `UBSAN_OPTIONS`, `ASAN_SYMBOLIZER_PATH`)
are forwarded to the server by the `local` launcher only.

Environment variables:

| Variable | Default | Meaning |
| --- | --- | --- |
| `ROTS_IT_LAUNCHER` | `local` on Linux, `docker` elsewhere | `local` or `docker`; empty means the default |
| `ROTS_IT_BINARY` | `bin/ageland` | the server binary for tests without a schedule (a test with one runs `bin/ageland_harness`), relative to the repository root |
| `ROTS_IT_SEED` | `20261011` | the random seed for `rotstool` and every server |
| `ROTS_IT_KEEP` | unset | `1` keeps every run directory and template |
| `ROTS_IT_LOCK_WAIT` | `600` | seconds to wait for the Docker lock, a finite number |
| `ROTS_IT_DOCKER_LOCK_DIR` | `/tmp/rots-docker-lock` | the shared Docker lock directory |
| `ROTS_IT_HOUR_TIMEOUT` | `10.0` | seconds `server.advance_hours` allows per game hour, a positive finite number |

A `ROTS_IT_LAUNCHER`, `ROTS_IT_SEED`, `ROTS_IT_LOCK_WAIT`, `ROTS_IT_BINARY` or
`ROTS_IT_HOUR_TIMEOUT` value the harness cannot use stops the run before any test with a usage
error naming the variable and its value.

## Shared Docker lock

Docker jobs on one host share the lock directory `/tmp/rots-docker-lock` and run at the same
time. Each job locks only the resources it uses, so a harness run in one checkout runs while
another checkout builds, runs a game or runs its own scenarios.

| Resource | Lock it when the job | Conflict it prevents |
| --- | --- | --- |
| `build:<checkout>/<tree>`, e.g. `build:slot-3/build` | builds into that tree, or runs programs from it | a build replacing programs another job is running, or two builds corrupting one tree |
| `port:<n>`, e.g. `port:1024` | publishes that host port (`--service-ports`, `-p 1024:1024`, `docker compose up`) | two containers publishing one port; `up` also claims the service's fixed `rots` container name |
| `game:<checkout>` | runs a game on the checkout's own `lib/` | two games writing the same accounts, player files and logs |
| `timing` (optional) | is timing-sensitive | other jobs' builds starving it; builds wait for it or use `-j2` |

`<checkout>` is the checkout's folder name. A job starts only when no other lock file names a
resource it needs. Its lock file has a session-unique name, such as
`<session>-<purpose>.lock`, holds these lines and is deleted when the job ends, however it ends:

```text
session: <who is running it>
repo: <checkout path>
resources: <space-separated resources, e.g. build:slot-3/build>
image: <image>
purpose: <what it is for>
started: <ISO time>
expected: <expected duration>
```

A lock file with no `resources:` line follows the old one-job-at-a-time convention: it holds
its checkout's build trees and `port:1024`. One older than three hours is probably stale; ask
whoever wrote it before removing it. If the directory does not exist, create it
(`mkdir -p /tmp/rots-docker-lock`).

With the `docker` launcher the harness takes its own lock, `harness-it-<pid>-<id>.lock` on
`build:<checkout>/build`, holds it for the whole pytest session, and removes it at the end. It
waits only while another lock file holds that tree: a build into it, another harness session in
the same checkout, an old-style lock for the same checkout, or a lock file it cannot read. It
waits up to 10 minutes (`ROTS_IT_LOCK_WAIT`), printing which lock files hold the tree, then
fails.

Ctrl-C, SIGTERM and SIGHUP all end the run the same way: the harness stops the server it
started and removes its lock file. A `harness-it-<pid>-<id>.lock` whose process is no longer
running (a run that was killed outright) is removed automatically, and the removal is printed.
The harness never removes any other lock file.

Do not hold a lock on this checkout's `build/` tree around `make integration`: the harness would
wait for it and then fail. The `local` launcher takes no lock.

## Run directories

Each test gets `build/integration/runs/<id>/` holding:

- `test-id.txt`, the test's node id;
- `lib/`, the test's own copy of the lib;
- `game.log`, the server's output (its standard error only, for a test with a schedule);
- `stdout.log`, for a test with a schedule only: the server's ordinary standard output, without
  the `harness: ` replies to time requests;
- one transcript per character (`testimm.txt` for `Testimm`), the text the character read.

A passing test's directory is deleted. It is kept when the lib or server fails to set up after
the lib was copied in, when any test in the session has failed, when the server's checks fail,
when the run is interrupted (Ctrl-C, SIGTERM or SIGHUP), or with `ROTS_IT_KEEP=1`. Kept
directories are listed, each with its test, in a "kept run directories" section at the end of
the run. An interrupt from a terminal also reaches the server and ends it, so during an
interrupted run the server checks do not fail the test: what they found is listed under the
kept directory instead. Template libs, one per character set,
are built once per session under `build/integration/libs/` and deleted at the end unless
`ROTS_IT_KEEP=1`.
Containers use `build/integration/tmp` as their `TMPDIR`.

## Writing a scenario

Mark the test `scenario` plus its group, declare its characters with
`@pytest.mark.characters(...)` using the definitions in `rots_harness.characters`, and log in
through the `server` fixture:

```python
import pytest

from rots_harness import world
from rots_harness.characters import IMPLEMENTOR, custom_mage

pytestmark = [pytest.mark.scenario, pytest.mark.boot]

MAGE = custom_mage(level=30, load_room=world.ROOM_ARENA_CENTRE)


@pytest.mark.characters(IMPLEMENTOR, MAGE)
def test_the_mage_starts_in_the_arena(server) -> None:
    implementor = server.login(IMPLEMENTOR.name)
    mage = server.login(MAGE.name)
    assert mage.command("look").room_name() == "Arena Centre"
```

All of a test's characters belong to one harness account, listed in marker order.
`server.login(name)` picks the character by name from the account's character list, so the
order does not matter to logging in. It returns a session whose `command(text)` returns the
command's output; the
fixture quits every session after the test. The game lets an account have only one character
online unless the account owns a character above level 91, so a test that logs in two
characters at once also declares one above that level, such as `IMPLEMENTOR` (level 100).

A new group needs a line under `markers` in `pytest.ini`.

A plain server's game time follows the wall clock. A test marked
`@pytest.mark.schedule("affect-hours")` runs `bin/ageland_harness` instead, whose game time
stands still until `server.advance_hours(n)` runs `n` whole game hours of the hourly and fast
updates and returns once the server acknowledges them. Zone resets, mob activity, combat and
skill timers do not run under that schedule. `advance_hours` fails a test without the marker.

Before writing or changing a scenario, or the harness itself, read the
`rots-integration-scenario` skill (`.agents/skills/rots-integration-scenario/SKILL.md`).
