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

   `make integration-build` runs that build (`make configure`, then the three targets with
   `--parallel $(JOBS)`), on Linux or inside the container
   (`docker compose run --rm -T rots bash -lc 'make integration-build'`). A program older than
   the newest `.cpp` or `.h` file it is built from also stops the run with the build command.
   That is any such file under `src/` except `src/tests/`, with `src/harness/` counting only for
   `ageland_harness`, `src/tools/rotstool/` only for `rotstool` and `src/main.cpp` only for
   `ageland`. `ROTS_IT_ALLOW_STALE=1` runs it anyway. The check compares modification times
   only, so a deleted source goes unnoticed and files copied with their old times can read as
   fresh; `ROTS_IT_ALLOW_STALE=1` is also the way past a wrong verdict.

2. Create the virtual environment once, from the repository root (Python 3.9 or later):

   ```bash
   python3 -m venv .venv && .venv/bin/pip install -r tests/integration/requirements.txt
   ```

## Running

```bash
make integration-unit PYTHON=.venv/bin/python           # the harness's own tests; no server
make integration PYTHON=.venv/bin/python                # every scenario
make integration PYTHON=.venv/bin/python GROUP=boot     # one group
make integration PYTHON=.venv/bin/python GROUP=shaping_zone_editor  # one subgroup of a large group
make integration PYTHON=.venv/bin/python JOBS=8         # every scenario, on 8 parallel workers
make integration-plan PYTHON=.venv/bin/python           # only show what make integration would run
```

A group is a marker such as `boot`, registered by its file `tests/integration/groups/<group>.txt`,
which holds the group's one-line description. `GROUP` may be an expression such as
`GROUP="boot or other"`; it always stays inside the `scenario` filter. A name that no group file,
`pytest.ini` or pytest itself registers stops the run with a usage error that lists the registered
names, so a misspelt group never passes by selecting nothing.

A large group is split into subgroups, one behaviour each. A file
`tests/integration/groups/<group>/<sub>.txt`, beside `<group>.txt`, registers the subgroup
`<group>_<sub>` with its one-line description; a subgroup has no subgroups of its own. Every test
of a group with subgroups marks the group and exactly one of its subgroups, and a test marking a
subgroup also marks its group. Under `make integration` collection stops with a usage error naming
each test that breaks either rule; pytest with `-n` (so `CI=true` with `JOBS` above 1) ends with
`Interrupted:` and the same list, exit status 2. `make integration-unit` checks the rules too. So
`GROUP=shaping` still runs every shaping test, and `GROUP=shaping_zone_editor` runs the zone
editor's alone.

`JOBS=<n>` runs the selected scenarios on `n` pytest-xdist worker processes, each scenario still
on its own fresh server. A scenario spends most of its time waiting on an idle server, so the
run's time falls almost in proportion to `n`. `JOBS` unset or `1` runs them one at a time in
one process. Only a `JOBS` given on the command line or in the environment counts: the default
`make build` uses (the number of cores) never makes `make integration` parallel.
Every run writes each test's result and time to `build/integration/results.xml` (JUnit XML) and
lists the 25 slowest tests at the end.

The workers of one run share:

- the Docker lock, which the controlling pytest process takes once for the whole run (see Shared
  Docker lock below); the workers never take it;
- the run's template folder, `build/integration/libs/<session id>/`: a worker that needs a
  character set's template no worker has finished builds it, and once one is complete every
  worker uses it.

Every run has its own template folder and run directories, so two runs in one checkout do not
disturb each other's tests. With the `docker` launcher the second run waits for the first's lock;
with the `local` launcher both run at once. Both runs write `build/integration/results.xml`, so
it holds the results of whichever finished last.

### The run plan

`make integration` (`rots_harness/run_plan.py`) first prints its plan: one numbered checkbox per
selected group with its test count and estimated minutes, then the total, the launcher, `JOBS`
and any `ROTS_IT_SHARD`. A group with subgroups is a numbered header with its totals, followed by
its selected subgroups, indented, each a numbered checkbox. A test counts toward every group that
marks it; the total counts it once. Each group counts only the tests that will run, so a test
another left-out group also marks is not counted. With `ROTS_IT_SHARD` the plan shows this
shard's share of the tests left after the groups left out, which is what the run then runs.
`make integration-plan` prints the plan and exits 0 without asking or running anything.

- At a terminal every group starts ticked. Numbers (and ranges such as `3-5`) toggle groups, `a`
  ticks all, `n` unticks all, Enter or `y` starts and `q` (or Ctrl-C) cancels. A header's number
  unticks all its subgroups when any is ticked and ticks them all otherwise; its box shows `x`
  for all ticked, `-` for some and blank for none. A range toggles each subgroup it covers once,
  a header covering its subgroups. With no answer
  for 5 minutes the run starts with the groups still ticked; the first answer stops that
  countdown. Unticking a group drops every test it marks, even one another ticked group marks.
  A run that started by itself gets no input from the terminal.
- Without a terminal (scripts, agents, `docker compose run -T`), a run estimated under
  `ROTS_IT_CONFIRM_MINUTES` (default 10) starts at once. A longer one, unless `CONFIRM=yes` is
  given, prints the plan and one line starting `ROTS_IT_NEEDS_CONFIRMATION:`, and stops with exit
  status 10, so whoever runs it can ask first or choose fewer groups. Through `make` the status is
  2 (below), so a script looks for that line instead.
- `CONFIRM=yes` starts the run as soon as the plan prints, without asking.
- `SKIP_GROUPS="poison summon"` (spaces or commas) leaves groups out; they are listed as skipped.
  A group leaves out all its subgroups, and a subgroup only itself. A name no group file
  registers stops the run with the registered names.
- A run that left groups out ends with a `Repeat: make integration ... CONFIRM=yes` line that
  repeats the choice. It names each unticked subgroup, or the group when all its subgroups were
  unticked.
- With `CI=true` (GitHub Actions sets it) none of this happens: the run is the plain pytest
  command, with no plan. A `SKIP_GROUPS` given there still leaves its groups out, and an
  unregistered name in it still stops the run.

| Exit status | Meaning |
| --- | --- |
| 2 | a `SKIP_GROUPS`, `CONFIRM`, `ROTS_IT_CONFIRM_MINUTES` or `ROTS_IT_SHARD` value it cannot use (pytest also uses 2 for an interrupted run) |
| 10 | no terminal, and the estimate reached `ROTS_IT_CONFIRM_MINUTES` without `CONFIRM=yes`; the `ROTS_IT_NEEDS_CONFIRMATION:` line says so |
| 11 | cancelled at the prompt or while collecting the tests; nothing ran |
| anything else | pytest's own status |

These are the statuses of `rots_harness/run_plan.py`. `make` itself exits 2 whenever its command
fails, and only names the command's status in its message (`Error 10`), so through `make` a
needed confirmation shows only in the `ROTS_IT_NEEDS_CONFIRMATION:` line.

Estimates come from `build/integration/durations.json`. A run records the time (setup, call and
teardown) of each scenario that ran to the end: one that passed or failed as an expected
failure replaces its recorded time, and one that failed only raises it (or, with no time yet,
gets one only above 15 seconds). A scenario that errored
in setup or teardown, or was skipped, keeps its earlier time, and an interrupted run records
nothing. A scenario with no recorded time counts 15 seconds. The minutes are divided between
the `JOBS` workers, never more workers than tests, so they are only approximate; the plan says
how many tests had no recorded time.

### The groups your changes reach

```bash
make integration-changed PYTHON=.venv/bin/python                    # changes since origin/release-frodo-next
make integration-changed PYTHON=.venv/bin/python BASE=origin/master
```

`make integration-changed` (`rots_harness/changed_groups.py`) lists the files changed since the
merge base with `BASE` (default `origin/release-frodo-next`; only a `BASE` on the make command
line counts, not an exported one), uncommitted changes to tracked files and untracked files git
does not ignore. A renamed file counts as both its old and its new path. It chooses groups by
the pull request rules under Continuous integration below, without a description, prints why
(with the commit `BASE` resolved to, so an out-of-date `origin/...` shows), and runs
`make integration` with the result, which shows its plan as any run does. An inherited `GROUP`
never narrows it; `SKIP_GROUPS` and `CONFIRM` on its command line reach that run. One rule is
local only: a changed game header (`src/**/*.h` outside `src/harness/` and `src/tests/`) also
runs every group, since constants and layouts in headers reach any scenario. CI leaves it out
because most pull requests touch a header. Changed game code (`.cpp`) is listed but selects no
group: name its groups with `GROUP=` or run every scenario. To see the choice without running it:

```bash
PYTHONPATH=tests/integration .venv/bin/python -m rots_harness.changed_groups --list-only
```

## Continuous integration

The `Integration` workflow (`.github/workflows/integration.yml`) runs on pull requests into
`master`, `release-frodo` and `release-frodo-next`, and runs every scenario on each push to
`release-frodo` or `release-frodo-next`. Its "Choose the integration groups" job first runs
`make integration-unit`, which includes the test world check (`unit/test_world_files.py`), so a
broken world fails in seconds. A pull request runs:

- `boot`, always;
- each group or subgroup its description names as `@integration-test-<group>`, such as
  `@integration-test-poison` or `@integration-test-shaping_zone_editor`, or every group for
  `@integration-test-all`;
- the groups of each scenario file it changes, and the group of each group file it changes; for
  a group whose subgroups the file also marks, only those subgroups, not the whole group;
- the groups of each scenario file that imports a changed scenario file (one level);
- every group when it changes the harness: `rots_harness/`, `conftest.py`, `pytest.ini`,
  `requirements.txt`, the test world (other than its notes), a scenario file other than a
  `test_*.py`, `src/harness/`, `src/tools/rotstool/`, the `Dockerfile`, the image action
  (`.github/actions/dev-image/`) or the workflow itself;
- every group when it changes `src/CMakeLists.txt`, whose compile settings reach every program,
  or a `lib/` file the harness copies into every test's lib (`lib/text/` and the tracked
  `lib/misc` files in `rots_harness/libbuilder.py`).

A renamed file counts as both its old and its new path, so moving a file out of one of these
folders still counts. In CI, a change to the game's own code adds no group by itself, headers
included (locally a header runs every group, as above), so name the groups whose behaviour it
touches. A name no group file registers fails the job and lists the registered groups.
Tokens inside an HTML comment (`<!-- -->`) are ignored. Editing the description reruns the jobs,
and the "Choose the integration groups" job's summary lists the groups that run and why. A run of
every group is split across `INTEGRATION_SHARDS` parallel jobs, `shard 1/N` to `shard N/N`, through
`ROTS_IT_SHARD`; a run of chosen groups is one job. Every job runs its scenarios on
`INTEGRATION_JOBS_PER_SHARD` pytest-xdist workers (`JOBS`), the one job of a run of chosen groups
included. Both values are set once, at the top of `.github/workflows/integration.yml`. A GitHub-hosted runner has 4 CPUs, and every job takes one
runner from the account's shared limit on concurrent jobs. Each job uploads its
`build/integration/results.xml`, with every test's result and time, as its
`integration-results-shard-<k>` artifact, pass or fail. When a scenario fails, its kept run
directories are uploaded as that job's `integration-runs-shard-<k>` artifact.

A sharded run also checks its own coverage. The select job lists the selected scenarios
(`rots_harness/shard_coverage.py`), uploads the list as the `integration-selection` artifact,
and says in its summary how many of them have no time in `shard_durations.json`. After the
shards finish, pass or fail, the "Check every selected scenario ran once" job compares every
shard's `results.xml` with that list. It fails when a shard uploaded no results, or when a
selected scenario ran no times or more than once, or a scenario ran that was not selected.

The shards are balanced by each test's time in `tests/integration/shard_durations.json`, so
every shard takes about as long; a test the file lacks counts 15 seconds. Each test still runs in
exactly one shard. Refresh the file when tests are added or their times change, from every
shard's results of one or more recent full runs that ran with the workflow's current
`INTEGRATION_SHARDS` and `INTEGRATION_JOBS_PER_SHARD`. Take them only from a push to
`release-frodo` or `release-frodo-next`, or from a pull request whose branch is in this repository
(not a fork), since the file decides how CI splits every later run:

```bash
gh run download <run id> --pattern 'integration-results-shard-*' --dir build/ci-results/<run id>
make integration-shard-durations PYTHON=.venv/bin/python RESULTS=build/ci-results
```

Each test's time is its median over every `results.xml` under `RESULTS`, leaving out runs where it
failed, errored or was skipped (an expected failure counts); a test no result file timed is dropped
from the file. A results file holding a DOCTYPE or entity declaration, or larger than 16 MB, stops
the refresh, and a time that is not a number of seconds is left out with a warning. A refresh that
would time fewer than 90% of the tests the file holds now, such as one missing a shard, is
refused; after removing tests on purpose, run
`PYTHONPATH=tests/integration .venv/bin/python -m rots_harness.shard_durations --force build/ci-results`.

Each job loads the development image from the Actions cache, or from the repository's
`dev-image-jammy-<hash>` pre-release for the `Dockerfile`'s hash, and builds it from the Ubuntu
snapshot only when neither has it, so a snapshot outage stops only a pull request that changes
the `Dockerfile`. `.github/workflows/dev-image.yml` publishes that pre-release on a push.

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
| `ROTS_IT_SHARD` | unset | `k/N`, such as `2/4`: runs only the k-th of N shares of the selected tests, balanced by `shard_durations.json` (see Continuous integration); a shard left with no tests passes, with or without `JOBS` workers |
| `ROTS_IT_ALLOW_STALE` | unset | `1` runs a program older than its sources, for example to test an old build on purpose |
| `ROTS_IT_CONFIRM_MINUTES` | `10` | the estimate, in minutes, from which `make integration` without a terminal needs `CONFIRM=yes`; a positive number (see The run plan) |

A `ROTS_IT_LAUNCHER`, `ROTS_IT_SEED`, `ROTS_IT_LOCK_WAIT`, `ROTS_IT_BINARY`,
`ROTS_IT_HOUR_TIMEOUT` or `ROTS_IT_SHARD` value the harness cannot use stops the run before any test with a usage
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
`build:<checkout>/build`, before the first test runs, holds it until every worker and server has
stopped, and then removes it. The controlling pytest process takes it, once per run however
many workers `JOBS` starts. A run that selects no scenario, `--collect-only`, and a run whose
server or `rotstool` is missing take no lock (each scenario then fails at once with the build
command); a run on workers takes it whatever it selects, because its controlling process
does not see the selection. It waits only while another lock file holds that tree: a build
into it, another harness run in the same checkout, an old-style lock for the same checkout, or a
lock file it cannot read. It waits up to 10 minutes (`ROTS_IT_LOCK_WAIT`), printing which lock
files hold the tree, then stops the run before any test.

Ctrl-C, SIGTERM and SIGHUP all end the run the same way: the harness stops the server it
started and removes its lock file. Under `make integration`, a SIGTERM or SIGHUP sent to `make`
reaches pytest through the run plan, which passes on the first one. A `harness-it-<pid>-<id>.lock` whose process is no longer
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
when the run is interrupted (Ctrl-C, SIGTERM or SIGHUP), or with `ROTS_IT_KEEP=1`. With `JOBS`,
"any test in the session" means any earlier test on the same worker: the failing test's
directory and every later one on that worker are kept, while the other workers keep only their
own. Kept directories are listed, each with its test, in a "kept run directories" section at the
end of the run, once, whichever worker kept them. An interrupt from a terminal also reaches the
server and ends it, so during an interrupted run the server checks do not fail the test: what
they found is listed under the kept directory instead. Template libs, one per character set,
are built once per run under `build/integration/libs/<session id>/` and deleted at the end
unless `ROTS_IT_KEEP=1`.
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

A character can also start with stored values that `wizset` would otherwise set after login:
`Character(..., stored=StoredValues(gold=500, lea=40, specialization="fire"))`, each named and
limited like its `wizset` field. Mobs and floor objects the test needs from the start go in the
test's world with `@pytest.mark.world_resets(MobReset(mob_vnum, room_vnum),
ObjectReset(object_vnum, room_vnum))` (`rots_harness/world_resets.py`): boot places them before
the first login. A server on the wall clock resets a zone again when its lifespan ends, which
brings back a placed mob a test has purged or killed; a test that relies on a mob being gone
loads it with `load mob` instead. The skill's harness reference lists the fields and their
limits.

A new group needs its own file, `groups/<group>.txt`, holding one line that says what the group
pins; `conftest.py` registers a marker for each file. A test in a group with subgroups (a folder
`groups/<group>/`) also marks one of them: add it to `pytestmark` when the whole file belongs to
one, or as `@pytest.mark.<group>_<sub>` on each test otherwise.

The test world's fixtures are listed by `make world-catalogue PYTHON=.venv/bin/python`
(`ZONE=<n>` for one zone, `CATEGORY=rooms` for one table), and explained in one notes file per
zone, `world/notes/zone-<n>.md`. A new scenario area adds its own zone (its world files, vnums,
map square and notes file), its own group file, and keeps its room constants in its scenario
file; `rots_harness/world.py` holds only the rooms two or more scenario files share. `make
integration-unit` checks the world's files (`unit/test_world_files.py`): index order, ascending
vnums, zone ranges and map squares, notes files and leftover conflict markers.

A plain server's game time follows the wall clock. A test marked
`@pytest.mark.schedule("affect-hours")` runs `bin/ageland_harness` instead, whose game time
stands still until `server.advance_hours(n)` runs `n` whole game hours of the hourly and fast
updates and returns once the server acknowledges them. Zone resets, mob activity, combat and
skill timers do not run under that schedule. `advance_hours` fails a test without the marker.

Before writing or changing a scenario, or the harness itself, read the
`rots-integration-scenario` skill (`.agents/skills/rots-integration-scenario/SKILL.md`).
