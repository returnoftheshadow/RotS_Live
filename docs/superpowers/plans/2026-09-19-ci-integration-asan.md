# CI Integration Job with AddressSanitizer Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a GitHub Actions job that builds the 32-bit server under AddressSanitizer and runs the integration harness against it, so a use-after-free fails CI, and repair the existing job, which has been red since 2026-08-14.

**Architecture:** A `ROTS_SANITIZE` CMake cache variable and a `SANITIZE` Makefile variable make the sanitized build reproducible with one variable. The harness gains a pure launcher-environment method that forwards sanitizer tuning to the server, and a run-directory retention rule that keeps evidence when a report is found during teardown or boot. A second workflow job builds both targets under ASan, proves the runtime linked, runs gtest non-blocking and the integration suite blocking, and uploads run directories on every outcome.

**Tech Stack:** CMake 3.16+, GNU make, GCC on `ubuntu-24.04` with `gcc-multilib`, AddressSanitizer (`lib32asan8` for the 32-bit server, `libasan8` for the 64-bit test binary), GitHub Actions, pytest 7.4 on Python 3.12 (runner) / pytest 9.1 on Python 3.14 (local venv), `actionlint` (local, `/opt/homebrew/bin/actionlint`).

**Spec:** `docs/superpowers/specs/2026-09-19-ci-integration-asan-design.md`

## Global Constraints

- Worktree: `/Users/drelidan/Projects/GitHub/RotS_Live/.claude/worktrees/uaf-port`, branch `fix/spell-room-affect-uaf-port`. Run every command from there. Never `cd` to the main repository.
- Commit with an explicit pathspec every time: `git commit -m "..." -- <files>`. Never `git add -A`. Never stash. Other sessions share this checkout; unrelated modified or untracked files are another writer's work.
- Never run two implementer subagents at once; the git index is shared.
- Every file this plan edits is LF today (verified by counting CR bytes). Keep it that way.
- The server compiles 32-bit only inside the i386 container. Any `docker compose run` follows `/tmp/rots-docker-lock/README.txt`: `ls /tmp/rots-docker-lock/`, refuse to start while any other `*.lock` exists, write your own uniquely named lock, remove it when done. This plan's manual container jobs use `uaf-port-ci-asan-task<N>.lock`.
- The local pytest lives in the git-ignored venv: `build/integration-venv/bin/python`. If it is missing: `python3 -m venv build/integration-venv && build/integration-venv/bin/pip install pytest`.
- Local baseline before this plan: `make integration-unit PYTHON=build/integration-venv/bin/python` → 36 passed.
- CI runs only on pushes to `master` and pull requests to `master`. The owner authorised: push the branch and open **one draft pull request** to `master` so every push triggers both jobs. Never merge it. Never push to `master`.
- Gate policy (owner ruling): the integration step is blocking from day one. If the first sanitized run reports a pre-existing memory error in a scenario path, diagnose it; fix it on this branch only when the root cause is local and the fix is small and testable (with a regression test); otherwise document the report and stop for the owner.
- `ASAN_OPTIONS` values are exactly: job-level `detect_leaks=0:handle_segv=2`; ctest step `detect_leaks=0:handle_segv=2:detect_container_overflow=0`.
- `timeout-minutes: 45` is provisional and must be revised from the first measured run.
- Commit message attribution trailer on every commit:
  ```
  Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>
  Claude-Session: https://claude.ai/code/session_01Kekwr9zhJai9gbEKFWnLup
  ```

---

## File structure

| File | Responsibility | Change |
| --- | --- | --- |
| `src/CMakeLists.txt` | Build definition for `ageland` (32-bit) and `ageland_tests` (64-bit) | Add `ROTS_SANITIZE` cache variable and apply its flags to both targets |
| `Makefile` | Developer entry point wrapping CMake | Add `SANITIZE` variable, always pass it at configure, help line |
| `tests/integration/rots_harness/launcher.py` | Starts and stops one server per launcher | Pure `environment(seed)` method with sanitizer passthrough; larger log tail |
| `tests/integration/unit/test_launcher.py` | Launcher unit tests | Tests for `environment` and `read_log_tail` |
| `tests/integration/rots_harness/retention.py` | **New.** One decision: does a run directory survive teardown | `keep_run_directory` |
| `tests/integration/unit/test_retention.py` | **New.** Tests for that decision | Four cases |
| `tests/integration/conftest.py` | Server fixture, crash check, launcher choice | Keep run dir on boot failure and on teardown-detected crash; mark the fixture-order dependency |
| `tests/integration/pytest.ini` | pytest config | Correct the `scenario` marker text |
| `.github/workflows/ci.yml` | CI | i386 crypt in the shared install step; new `integration-asan` job |
| `tests/integration/README.md` | Harness user docs | ASan recipe, caveats, blind spots, CI as the gate |
| `WIP.md` | Shared status file | One dated line |
| `docs/superpowers/specs/2026-09-19-integration-test-harness-design.md` | Parent spec | Replace the conditional slice 3 paragraph |

---

### Task 1: `ROTS_SANITIZE` in CMake and `SANITIZE` in the Makefile

**Files:**
- Modify: `src/CMakeLists.txt:22` (declare after `ROTS_SUPPRESS_TEST_WARNINGS`), `:161-174` (`ageland` options), `:179-202` (`ageland_tests` options)
- Modify: `Makefile:1-36`

**Interfaces:**
- Produces: CMake cache variable `ROTS_SANITIZE` (string, `""` or `"address"`); Makefile variable `SANITIZE` forwarded as `-DROTS_SANITIZE=$(SANITIZE)` on every configure. Task 4 relies on `make BUILD_DIR=build-asan SANITIZE=address configure`.

- [ ] **Step 1: Add the cache variable and derived option lists to `src/CMakeLists.txt`**

Insert directly after the existing line 22 (`option(ROTS_SUPPRESS_TEST_WARNINGS ...)`):

```cmake

# A CACHE STRING rather than an option(): it names a sanitizer, so a boolean will not do.
# Empty keeps the plain build. Anything but "address" is rejected here so a typo cannot
# silently configure an unsanitized tree that then passes the CI proof step.
set(ROTS_SANITIZE "" CACHE STRING "Sanitizer to enable for ageland and ageland_tests (empty or address)")
if(ROTS_SANITIZE STREQUAL "")
    set(ROTS_SANITIZER_COMPILE_OPTIONS "")
    set(ROTS_SANITIZER_LINK_OPTIONS "")
elseif(ROTS_SANITIZE STREQUAL "address")
    set(ROTS_SANITIZER_COMPILE_OPTIONS -fsanitize=address -fno-omit-frame-pointer)
    set(ROTS_SANITIZER_LINK_OPTIONS -fsanitize=address)
else()
    message(FATAL_ERROR "ROTS_SANITIZE must be empty or 'address', not '${ROTS_SANITIZE}'")
endif()
```

- [ ] **Step 2: Apply the options to `ageland`**

Directly after the existing `target_link_options(ageland PRIVATE -m32 -g -rdynamic)` line, add:

```cmake
if(ROTS_SANITIZE)
    target_compile_options(ageland PRIVATE ${ROTS_SANITIZER_COMPILE_OPTIONS})
    target_link_options(ageland PRIVATE ${ROTS_SANITIZER_LINK_OPTIONS})
endif()
```

- [ ] **Step 3: Apply the options to `ageland_tests`**

Directly after the existing `target_link_options(ageland_tests PRIVATE -Wl,--wrap=_Z6numberv -Wl,--wrap=_Z6numberii)` block (it ends with `)` on its own line), add:

```cmake
if(ROTS_SANITIZE)
    target_compile_options(ageland_tests PRIVATE ${ROTS_SANITIZER_COMPILE_OPTIONS})
    target_link_options(ageland_tests PRIVATE ${ROTS_SANITIZER_LINK_OPTIONS})
endif()
```

- [ ] **Step 4: Add `SANITIZE` to the Makefile**

Change the variable block at the top of `Makefile` from:

```make
BUILD_DIR := build
SRC_DIR := src
CMAKE := cmake
CMAKE_CONFIGURE_ARGS ?= -DCMAKE_CXX_COMPILER=g++
CMAKE_CACHE := $(BUILD_DIR)/CMakeCache.txt
PYTHON ?= python3
```

to:

```make
BUILD_DIR := build
SRC_DIR := src
CMAKE := cmake
CMAKE_CONFIGURE_ARGS ?= -DCMAKE_CXX_COMPILER=g++
# Empty or "address". Always forwarded so a stale cache cannot keep a stale value silently;
# the configure rule only runs on a fresh BUILD_DIR, so changing SANITIZE needs a new one.
SANITIZE ?=
CMAKE_CACHE := $(BUILD_DIR)/CMakeCache.txt
PYTHON ?= python3
```

Change the configure recipe from:

```make
$(CMAKE_CACHE):
	$(CMAKE) -S $(SRC_DIR) -B $(BUILD_DIR) $(CMAKE_CONFIGURE_ARGS)
```

to:

```make
$(CMAKE_CACHE):
	$(CMAKE) -S $(SRC_DIR) -B $(BUILD_DIR) $(CMAKE_CONFIGURE_ARGS) -DROTS_SANITIZE=$(SANITIZE)
```

In the `help` target, directly after the `make configure` line, add:

```make
	@printf "  make configure SANITIZE=address BUILD_DIR=build-asan  Configure an AddressSanitizer tree (fresh BUILD_DIR)\n"
```

(Recipe lines are tab-indented. Keep the tab.)

- [ ] **Step 5: Verify the Makefile forwards the variable (host, dry run, no CMake executed)**

Run:
```sh
make -n BUILD_DIR=build/cfg-make SANITIZE=address configure | grep -- '-DROTS_SANITIZE=address'
make -n BUILD_DIR=build/cfg-make configure | grep -E -- '-DROTS_SANITIZE=$'
make help | grep -F 'SANITIZE=address'
```
Expected: each command prints one matching line and exits 0. `build/cfg-make` must not exist beforehand (`ls build/cfg-make` → no such file); if it does, pick another name.

- [ ] **Step 6: Verify the CMake branches inside the container (configure only, no build)**

Acquire the lock, run the three configures, release the lock:
```sh
ls /tmp/rots-docker-lock/
# Proceed only if no *.lock is listed. Otherwise wait and retry; never remove another lock.
printf 'session: uaf-port ci-asan task 1\npurpose: cmake configure check\n' > /tmp/rots-docker-lock/uaf-port-ci-asan-task1.lock
docker compose run --rm -T rots bash -lc '
  set -e
  cd /rots
  cmake -S src -B build/cfg-plain -DCMAKE_CXX_COMPILER=g++ > /dev/null
  if grep -q fsanitize build/cfg-plain/CMakeFiles/ageland.dir/flags.make build/cfg-plain/CMakeFiles/ageland_tests.dir/flags.make; then
    echo "plain tree carries sanitizer flags"; exit 1
  fi
  cmake -S src -B build/cfg-address -DCMAKE_CXX_COMPILER=g++ -DROTS_SANITIZE=address > /dev/null
  grep -q -- "-fsanitize=address" build/cfg-address/CMakeFiles/ageland.dir/flags.make
  grep -q -- "-fno-omit-frame-pointer" build/cfg-address/CMakeFiles/ageland.dir/flags.make
  grep -q -- "-fsanitize=address" build/cfg-address/CMakeFiles/ageland.dir/link.txt
  grep -q -- "-fsanitize=address" build/cfg-address/CMakeFiles/ageland_tests.dir/flags.make
  grep -q -- "-fsanitize=address" build/cfg-address/CMakeFiles/ageland_tests.dir/link.txt
  if cmake -S src -B build/cfg-bogus -DCMAKE_CXX_COMPILER=g++ -DROTS_SANITIZE=bogus > build/cfg-bogus.log 2>&1; then
    echo "bogus value was accepted"; exit 1
  fi
  grep -q "ROTS_SANITIZE must be empty or" build/cfg-bogus.log
  echo CMAKE-BRANCHES-OK
'
rm -f /tmp/rots-docker-lock/uaf-port-ci-asan-task1.lock
rm -rf build/cfg-plain build/cfg-address build/cfg-bogus build/cfg-bogus.log
```
Expected: the last line of container output is `CMAKE-BRANCHES-OK`. The container's CMake does not test-compile with the sanitizer flags (they are target options), so this check needs no ASan runtime. If `docker compose run` is denied by the permission system, stop and report the exact denial; do not retry it.

- [ ] **Step 7: Confirm nothing else changed and line endings held**

Run:
```sh
git status --porcelain
git diff --stat -- src/CMakeLists.txt Makefile
grep -c $'\r' src/CMakeLists.txt Makefile
```
Expected: only the two files are modified; both CR counts are 0.

- [ ] **Step 8: Commit**

```sh
git commit -m "build: ROTS_SANITIZE CMake variable and SANITIZE make variable for AddressSanitizer trees

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01Kekwr9zhJai9gbEKFWnLup" -- src/CMakeLists.txt Makefile
```

---

### Task 2: Launcher environment method with sanitizer passthrough

**Files:**
- Modify: `tests/integration/rots_harness/launcher.py:19-21` (constants), `:49-53` (`read_log_tail`), `:88-109` (`LocalProcessLauncher`)
- Test: `tests/integration/unit/test_launcher.py`

**Interfaces:**
- Produces: `launcher.SANITIZER_ENVIRONMENT_VARIABLES: tuple[str, ...]`, `launcher.LOG_TAIL_BYTES: int = 16000`, `LocalProcessLauncher.environment(self, seed: int) -> dict[str, str]`. Task 3's conftest does not call these directly; Task 4's job relies on the passthrough so `ASAN_OPTIONS` reaches the server.

- [ ] **Step 1: Write the failing tests**

Append to `tests/integration/unit/test_launcher.py`:

```python


def test_local_launcher_environment_is_clean_apart_from_seed_and_sanitizer_tuning(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    monkeypatch.setenv("ASAN_OPTIONS", "detect_leaks=0:handle_segv=2")
    monkeypatch.setenv("ASAN_SYMBOLIZER_PATH", "/usr/bin/llvm-symbolizer")
    monkeypatch.delenv("LSAN_OPTIONS", raising=False)
    monkeypatch.delenv("UBSAN_OPTIONS", raising=False)
    monkeypatch.setenv("ROTS_IT_SEED", "5")  # a host-only harness variable must not leak through

    local = launcher.LocalProcessLauncher(binary=tmp_path / "ageland")
    environment = local.environment(seed=42)

    assert environment["ROTS_RANDOM_SEED"] == "42"
    assert environment["ASAN_OPTIONS"] == "detect_leaks=0:handle_segv=2"
    assert environment["ASAN_SYMBOLIZER_PATH"] == "/usr/bin/llvm-symbolizer"
    assert "LSAN_OPTIONS" not in environment
    assert "UBSAN_OPTIONS" not in environment
    assert "ROTS_IT_SEED" not in environment
    assert set(environment) <= {"PATH", "HOME", "ROTS_RANDOM_SEED", *launcher.SANITIZER_ENVIRONMENT_VARIABLES}


def test_local_launcher_environment_has_no_sanitizer_keys_when_the_host_sets_none(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    for name in launcher.SANITIZER_ENVIRONMENT_VARIABLES:
        monkeypatch.delenv(name, raising=False)

    environment = launcher.LocalProcessLauncher(binary=tmp_path / "ageland").environment(seed=1)

    assert set(environment) == {"PATH", "HOME", "ROTS_RANDOM_SEED"}


def test_read_log_tail_returns_only_the_last_bytes_and_defaults_to_the_documented_size(tmp_path: Path) -> None:
    log_path = tmp_path / "game.log"
    log_path.write_bytes(b"x" * (launcher.LOG_TAIL_BYTES + 100) + b"TAIL")

    assert launcher.read_log_tail(log_path, max_bytes=8) == "xxxxTAIL"
    assert len(launcher.read_log_tail(log_path)) == launcher.LOG_TAIL_BYTES
    assert launcher.LOG_TAIL_BYTES == 16000
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `build/integration-venv/bin/python -m pytest tests/integration/unit/test_launcher.py -q -k "environment or read_log_tail"`
Expected: 3 failed, with `AttributeError: ... has no attribute 'SANITIZER_ENVIRONMENT_VARIABLES'` / `'environment'` / `'LOG_TAIL_BYTES'`.

- [ ] **Step 3: Implement the constants, the method, and the larger tail**

In `tests/integration/rots_harness/launcher.py`, after `DEFAULT_LOCK_DIR = Path("/tmp/rots-docker-lock")` add:

```python
# Host variables the local launcher forwards to the server. Everything else is dropped on
# purpose: the server must see a clean environment, and only sanitizer tuning (leak checks,
# signal handling, symbolizer location) is a legitimate host-to-server channel.
SANITIZER_ENVIRONMENT_VARIABLES = ("ASAN_OPTIONS", "LSAN_OPTIONS", "UBSAN_OPTIONS", "ASAN_SYMBOLIZER_PATH")
# Enough for a whole AddressSanitizer report (header, two stacks, shadow map) when the
# server dies before it listens and the tail is the only evidence that survives.
LOG_TAIL_BYTES = 16000
```

Change `read_log_tail`'s signature from `def read_log_tail(log_path: Path, max_bytes: int = 4000) -> str:` to `def read_log_tail(log_path: Path, max_bytes: int = LOG_TAIL_BYTES) -> str:`.

In `LocalProcessLauncher`, add after `command`:

```python
    def environment(self, seed: int) -> dict[str, str]:
        environment = {"PATH": os.environ.get("PATH", ""), "HOME": os.environ.get("HOME", ""), "ROTS_RANDOM_SEED": str(seed)}
        for name in SANITIZER_ENVIRONMENT_VARIABLES:
            if name in os.environ:
                environment[name] = os.environ[name]
        return environment
```

In `start`, replace the line

```python
        environment = {"PATH": os.environ.get("PATH", ""), "HOME": os.environ.get("HOME", ""), "ROTS_RANDOM_SEED": str(seed)}
```

with

```python
        environment = self.environment(seed)
```

- [ ] **Step 4: Run the whole harness unit suite**

Run: `build/integration-venv/bin/python -m pytest tests/integration/unit -q`
Expected: 39 passed (36 baseline + 3 new).

- [ ] **Step 5: Commit**

```sh
git commit -m "harness: pure launcher environment with sanitizer passthrough; 16 KB log tail

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01Kekwr9zhJai9gbEKFWnLup" -- tests/integration/rots_harness/launcher.py tests/integration/unit/test_launcher.py
```

---

### Task 3: Run-directory retention on boot failure and teardown-detected crashes

**Files:**
- Create: `tests/integration/rots_harness/retention.py`
- Create: `tests/integration/unit/test_retention.py`
- Modify: `tests/integration/conftest.py:24-36` (`HarnessServer`), `:51-72` (`server` fixture), `:75-84` (`fail_on_server_crash`)
- Modify: `tests/integration/pytest.ini:5`

**Interfaces:**
- Produces: `retention.keep_run_directory(keep_requested: bool, tests_failed_so_far: int, this_server_failed: bool) -> bool`; `HarnessServer.crash_detected: bool`.
- Consumes: nothing from Tasks 1 and 2.

- [ ] **Step 1: Write the failing tests**

Create `tests/integration/unit/test_retention.py`:

```python
from __future__ import annotations

from rots_harness.retention import keep_run_directory


def test_a_clean_run_with_no_failures_is_discarded() -> None:
    assert keep_run_directory(keep_requested=False, tests_failed_so_far=0, this_server_failed=False) is False


def test_keep_requested_wins_regardless_of_outcome() -> None:
    assert keep_run_directory(keep_requested=True, tests_failed_so_far=0, this_server_failed=False) is True


def test_an_earlier_failure_in_the_session_keeps_later_run_directories() -> None:
    assert keep_run_directory(keep_requested=False, tests_failed_so_far=1, this_server_failed=False) is True


def test_a_crash_found_during_this_tests_own_teardown_keeps_its_directory() -> None:
    # pytest's session counter has not been incremented yet at this point, so the
    # per-server flag is the only signal.
    assert keep_run_directory(keep_requested=False, tests_failed_so_far=0, this_server_failed=True) is True
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `build/integration-venv/bin/python -m pytest tests/integration/unit/test_retention.py -q`
Expected: collection error, `ModuleNotFoundError: No module named 'rots_harness.retention'`.

- [ ] **Step 3: Create the retention module**

Create `tests/integration/rots_harness/retention.py`:

```python
"""Decides whether a scenario's run directory survives fixture teardown."""

from __future__ import annotations


def keep_run_directory(keep_requested: bool, tests_failed_so_far: int, this_server_failed: bool) -> bool:
    """True when the directory must be kept for diagnosis.

    tests_failed_so_far is pytest's session counter. It is incremented only after every
    finalizer of the failing test has run, so a crash the autouse check finds during this
    test's own teardown is invisible in it; this_server_failed carries that case, and the
    boot-failure case, so the game.log holding the report is never deleted.
    """
    return keep_requested or tests_failed_so_far > 0 or this_server_failed
```

- [ ] **Step 4: Run the retention tests**

Run: `build/integration-venv/bin/python -m pytest tests/integration/unit/test_retention.py -q`
Expected: 4 passed.

- [ ] **Step 5: Wire the fixture**

In `tests/integration/conftest.py`:

Add to the imports, after `from rots_harness.libbuilder import RunLibBuilder`:

```python
from rots_harness.retention import keep_run_directory
```

Change `HarnessServer` to carry the flag (add the last field):

```python
@dataclass
class HarnessServer:
    handle: ServerHandle
    lib_dir: Path
    run_dir: Path
    roster: tuple[fixtures.CharacterSpec, ...]
    monitor: CrashMonitor
    crash_detected: bool = False  # set by fail_on_server_crash before it fails the test
```

Replace the whole `server` fixture with:

```python
@pytest.fixture  # each test gets a fresh server: no command reliably strips a room affect, so isolation is by reboot
def server(request: pytest.FixtureRequest) -> HarnessServer:
    run_dir = REPO_ROOT / "build" / "integration" / uuid.uuid4().hex[:12]
    run_dir.mkdir(parents=True)
    try:
        built = RunLibBuilder(REPO_ROOT, INTEGRATION_ROOT / "world", INTEGRATION_ROOT / "fixtures" / "character.template.json").build(run_dir, fixtures.STANDARD_ROSTER)
        launcher = choose_launcher()
        seed = int(os.environ.get("ROTS_IT_SEED", DEFAULT_SEED))
        handle = launcher.start(run_dir, built.lib_dir, allocate_free_port(), seed)
    except Exception:
        # A server that died before listening (a sanitizer report at boot, for example) has
        # left its evidence in run_dir/game.log; keep it rather than reduce it to a log tail.
        print(f"\nserver failed to start; run directory kept at {run_dir}")
        raise
    harness_server = HarnessServer(handle, built.lib_dir, run_dir, built.roster, CrashMonitor(handle))
    try:
        yield harness_server
    finally:
        launcher.stop(handle)
        keep = keep_run_directory(
            keep_requested=os.environ.get("ROTS_IT_KEEP") == "1",
            tests_failed_so_far=request.session.testsfailed,
            this_server_failed=harness_server.crash_detected,
        )
        if keep:
            print(f"\nrun directory kept at {run_dir}")
        else:
            shutil.rmtree(run_dir, ignore_errors=True)
```

Replace the whole `fail_on_server_crash` fixture with:

```python
@pytest.fixture(autouse=True)
def fail_on_server_crash(request: pytest.FixtureRequest):
    # Requesting `server` here, inside the body, registers the server finalizer before this
    # one; finalizers run last-in-first-out, so the crash check below runs while the server
    # is still alive and before launcher.stop sends SIGTERM. The ordering depends on this
    # getfixturevalue call, not on autouse placement.
    harness_server = None
    if "server" in request.fixturenames:
        harness_server = request.getfixturevalue("server")
    yield
    if harness_server is not None:
        problems = harness_server.monitor.check()
        if problems:
            harness_server.crash_detected = True
            pytest.fail("server problems during this test:\n" + "\n".join(problems))
```

- [ ] **Step 6: Correct the marker text**

In `tests/integration/pytest.ini`, change

```
    scenario: needs a running server (built by the session-scoped server fixture)
```

to

```
    scenario: needs a running server (each test boots its own through the server fixture)
```

- [ ] **Step 7: Run the whole unit suite and a collection-only pass of the scenarios**

Run:
```sh
build/integration-venv/bin/python -m pytest tests/integration/unit -q
build/integration-venv/bin/python -m pytest tests/integration/scenarios --collect-only -q | tail -3
```
Expected: 43 passed (39 + 4); the collection lists the scenario tests with no errors (the conftest imports and parses). The fixture wiring itself is exercised by the CI run in Task 5; there is no server on this Mac outside Docker, and a Docker run is not required for this task.

- [ ] **Step 8: Commit**

```sh
git commit -m "harness: keep run directories on boot failure and teardown-detected crashes

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01Kekwr9zhJai9gbEKFWnLup" -- tests/integration/rots_harness/retention.py tests/integration/unit/test_retention.py tests/integration/conftest.py tests/integration/pytest.ini
```

---

### Task 4: The workflow: repair the shared install step, add `integration-asan`, open the draft PR

**Files:**
- Modify: `.github/workflows/ci.yml` (whole file; content below)

**Interfaces:**
- Consumes: `make BUILD_DIR=build-asan SANITIZE=address configure` (Task 1); `ASAN_OPTIONS` passthrough (Task 2); `ROTS_IT_KEEP` retention (Task 3).
- Produces: a draft pull request whose runs Task 5 reads.

- [ ] **Step 1: Write the workflow**

Replace the entire content of `.github/workflows/ci.yml` with:

```yaml
name: CI

on:
  push:
    branches:
      - master
  pull_request:
    branches:
      - master

permissions:
  contents: read

jobs:
  build-test-smoke:
    name: Build, Unit Tests, and Smoke Tests
    runs-on: ubuntu-24.04
    timeout-minutes: 30

    steps:
      - name: Check out repository
        uses: actions/checkout@v4
        with:
          persist-credentials: false

      - name: Install system dependencies
        # ageland links -lcrypt as a 32-bit binary, so the i386 crypt development package is
        # required alongside the 64-bit one (README, "build prerequisites").
        run: |
          sudo dpkg --add-architecture i386
          sudo apt-get update
          sudo apt-get install -y \
            build-essential \
            clang-format \
            cmake \
            cargo \
            gcc-multilib \
            g++-multilib \
            libc6-dev-i386 \
            libcrypt-dev \
            libcrypt-dev:i386 \
            libgtest-dev \
            make \
            pkg-config \
            python3 \
            rustc

      - name: Configure build tree
        run: make configure

      - name: Run C++ unit tests
        run: make test

      - name: Run account smoke flow
        run: make smoke-account

  integration-asan:
    name: Integration Suite under AddressSanitizer
    runs-on: ubuntu-24.04
    # Provisional budget: the plain job's last green run took about 4 minutes end to end;
    # this job's time is 42 tests each booting a sanitized server, unmeasured before the
    # first run. Revise from that run.
    timeout-minutes: 45
    env:
      # detect_leaks=0: both binaries exit normally (the server's SIGTERM handler ends in
      # exit(0)), so LeakSanitizer would otherwise scan the heap at every teardown; leaks
      # are not the target. handle_segv=2: the server installs its own SIGSEGV handler in
      # main(), which would replace ASan's report with the legacy backtrace.
      ASAN_OPTIONS: detect_leaks=0:handle_segv=2

    steps:
      - name: Check out repository
        uses: actions/checkout@v4
        with:
          persist-credentials: false

      - name: Install system dependencies
        # lib32asan8 is the 32-bit runtime for ageland; libasan8 the 64-bit one for
        # ageland_tests. Both are named explicitly rather than trusted as transitive
        # dependencies of gcc-multilib. python3-pytest is the harness runner (pytest 7.4;
        # the harness needs 7.0+ for the pythonpath ini key).
        run: |
          sudo dpkg --add-architecture i386
          sudo apt-get update
          sudo apt-get install -y \
            build-essential \
            cmake \
            gcc-multilib \
            g++-multilib \
            lib32asan8 \
            libasan8 \
            libc6-dev-i386 \
            libcrypt-dev \
            libcrypt-dev:i386 \
            libgtest-dev \
            make \
            python3 \
            python3-pytest

      - name: Lower ASLR entropy for AddressSanitizer
        # Ubuntu 24.04's raised vm.mmap_rnd_bits is a known ASan startup failure ("shadow
        # memory range interleaves with an existing memory mapping"). Runner-local.
        run: sudo sysctl -w vm.mmap_rnd_bits=28

      - name: Configure sanitized build tree
        run: make BUILD_DIR=build-asan SANITIZE=address configure

      - name: Build server and unit tests under AddressSanitizer
        # Both targets build here, blocking, so a compile or link break under the sanitizer
        # is never hidden inside the non-blocking ctest step below.
        run: |
          make BUILD_DIR=build-asan setup
          cmake --build build-asan --target ageland ageland_tests -j"$(nproc)"

      - name: Prove the 32-bit sanitizer runtime is linked and initialises
        # The ldd line guards the silent-plain-build case (a missing runtime already fails
        # at link time). The help=1 line proves the runtime starts and writes to stderr;
        # the server itself exits on the nonexistent lib dir, which is irrelevant here.
        run: |
          ldd bin/ageland | grep -E 'libasan\.so[^ ]* => /'
          (timeout 30 env ASAN_OPTIONS=help=1 ./bin/ageland -t -d /nonexistent 1 2>&1 || true) | grep -q 'Available flags for AddressSanitizer'

      - name: Run C++ unit tests under AddressSanitizer (non-blocking)
        # ageland_tests is 64-bit and links the uninstrumented system gtest, so container
        # overflow checks would report spurious findings against shared std containers.
        continue-on-error: true
        env:
          ASAN_OPTIONS: detect_leaks=0:handle_segv=2:detect_container_overflow=0
        run: ctest --test-dir build-asan --output-on-failure

      - name: Run integration suite
        env:
          ROTS_IT_LAUNCHER: local
          ROTS_IT_KEEP: "1"
        run: make integration

      - name: Upload run directories
        # always(): a job that hits timeout-minutes is cancelled, and failure() steps do
        # not run on cancellation. The directories hold game.log, transcripts, and a copy
        # of the tracked lib/text, lib/misc and synthetic world; no real player data.
        if: always()
        uses: actions/upload-artifact@v4
        with:
          name: integration-run-directories
          path: build/integration/
          if-no-files-found: ignore
          retention-days: 7
```

- [ ] **Step 2: Validate the workflow locally**

Run:
```sh
actionlint .github/workflows/ci.yml
python3 -c "import yaml, sys; doc = yaml.safe_load(open('.github/workflows/ci.yml')); print(sorted(doc['jobs']))"
grep -c $'\r' .github/workflows/ci.yml
```
Expected: `actionlint` prints nothing and exits 0; the second prints `['build-test-smoke', 'integration-asan']`; the CR count is 0.

- [ ] **Step 3: Commit**

```sh
git commit -m "ci: repair the i386 crypt link in the shared install step; add the integration-asan job

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01Kekwr9zhJai9gbEKFWnLup" -- .github/workflows/ci.yml
```

- [ ] **Step 4: Push the branch and open the draft pull request (controller step, owner-authorised)**

```sh
git push origin fix/spell-room-affect-uaf-port
gh pr create --repo returnoftheshadow/RotS_Live --draft --base master --head fix/spell-room-affect-uaf-port \
  --title "Integration harness slice 3: CI job under AddressSanitizer (draft, do not merge)" \
  --body-file - <<'EOF'
Draft opened to run CI on the integration harness branch. Not for merge as-is.

- Repairs the shared install step (i386 crypt library), which has kept the existing job red since 2026-08-14.
- Adds `integration-asan`: builds `ageland` (32-bit) and `ageland_tests` (64-bit) under AddressSanitizer, proves the runtime linked, runs gtest non-blocking and the integration suite blocking, uploads run directories on every outcome.

Spec: `docs/superpowers/specs/2026-09-19-ci-integration-asan-design.md`
Plan: `docs/superpowers/plans/2026-09-19-ci-integration-asan.md`

🤖 Generated with [Claude Code](https://claude.com/claude-code)

https://claude.ai/code/session_01Kekwr9zhJai9gbEKFWnLup
EOF
```
Expected: a PR URL. Record it in the plan's Task 5 notes.

---

### Task 5: First diagnostic run and triage (controller task; not delegated)

**Files:**
- Read only, unless a finding is bounded (see policy).

**Interfaces:**
- Consumes: the draft PR from Task 4.
- Produces: the observed suite duration (feeds Task 6's timeout revision), any regression fixes, and the judgment-call log for the owner.

- [ ] **Step 1: Watch the run**

```sh
gh run list --repo returnoftheshadow/RotS_Live --branch fix/spell-room-affect-uaf-port --limit 3
gh run watch --repo returnoftheshadow/RotS_Live <run-id> --exit-status
```
Do not poll on a short loop; a run takes minutes. Read the step timings afterwards:
```sh
gh run view --repo returnoftheshadow/RotS_Live <run-id> --json jobs --jq '.jobs[] | {name, conclusion, started: .startedAt, completed: .completedAt, steps: [.steps[] | {name, conclusion}]}'
```

- [ ] **Step 2: Classify the outcome of `integration-asan` and act by the spec's table**

| Outcome | Action |
| --- | --- |
| Install step fails on a package name | Read the apt error. If `lib32asan8`/`libasan8` are versioned differently on the image, use `apt-cache search lib32asan` output from the failed log to pick the right major; commit and push. |
| Proof step fails on `ldd` | The configured tree did not enable the sanitizer. Inspect the configure step log for `ROTS_SANITIZE`; fix Task 1's Makefile forwarding. |
| Proof step fails on `help=1` | The runtime did not initialise. Check for the ASLR message in the log; if present with the sysctl already applied, capture the log and stop for the owner. |
| ctest step yellow | Expected and non-blocking. Note the count of sanitizer findings in the log for the owner; no action. |
| All scenarios pass, one xfail or xpass | Done. Record the suite duration. |
| An ASan report in a scenario path | Download the artifact: `gh run download --repo returnoftheshadow/RotS_Live <run-id> -n integration-run-directories -D <scratchpad>/run-<id>`. Read the report's `game.log`. Diagnose. If the root cause is local and the fix is small and testable, fix it on this branch with a regression test (`src/tests/` gtest or a scenario assertion), rebuild in the container only if needed for a gtest, commit, push. Otherwise write the report and stack into the judgment-call log and stop for the owner. |
| Timeouts only (harness `expect` deadlines) | Make the fixed timeouts env-tunable: read `ROTS_IT_TIMEOUT_SCALE` (float, default `1.0`) in `session.py` and multiply every fixed timeout; set it to `3` in the workflow's integration step; unit-test the scaling; commit; push; rerun. |
| Job cancelled by `timeout-minutes` | Read the uploaded artifacts to see how far it got; raise the budget in proportion to the measured per-test time; commit; push. |
| `build-test-smoke` now green | Note it. If still red for a reason other than crypt, record it; that job is otherwise out of scope. |

- [ ] **Step 3: Record**

Write every decision taken here into the judgment-call log (a section appended to `.superpowers/sdd/2026-09-19-integration-test-harness/HANDOFF.md`, which is git-ignored) with the run id, the observed durations, and the outcome class.

---

### Task 6: Documentation, status, and parent-spec update

**Files:**
- Modify: `tests/integration/README.md`
- Modify: `WIP.md` (append under the harness section; touch nothing else in the file)
- Modify: `docs/superpowers/specs/2026-09-19-integration-test-harness-design.md:215-219`
- Modify: `.github/workflows/ci.yml` (only if Task 5 measured a duration that changes the 45-minute budget)

**Interfaces:**
- Consumes: Task 5's measured durations and the PR URL.

- [ ] **Step 1: Rewrite the Running and Environment sections of `tests/integration/README.md` and add the sanitizer section**

Replace the paragraph beginning "`make integration` needs either a native Linux build" with:

```markdown
`make integration` needs either a native Linux build at `bin/ageland`, or Docker on
macOS: set `ROTS_IT_LAUNCHER=docker`. If the plain build crashes under QEMU, point at
the stack-protector build instead: `ROTS_IT_BINARY=build/sp/bin/ageland`.
```

Insert after the "Environment variables" list:

```markdown
## AddressSanitizer

CI is the gate: the `integration-asan` job in `.github/workflows/ci.yml` builds the server
under AddressSanitizer on native Linux and runs this suite against it, so a use-after-free
fails the run. Docker runs on a Mac stay for development; the i386 container has no
sanitizer runtime.

To reproduce the CI build on Linux (the tree must be fresh; the configure rule only runs
when `<BUILD_DIR>/CMakeCache.txt` is absent, so changing `SANITIZE` needs a new directory):

```sh
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
```

Replace the "Run output" paragraph's last sentence ("This directory is deleted after a passing run unless a test failed or `ROTS_IT_KEEP=1` was set, in which case it is kept and its path is printed.") with:

```markdown
This directory is deleted after a passing run unless a test failed earlier in the session,
this test's own teardown check found a crash, the server failed to start, or
`ROTS_IT_KEEP=1` was set; in those cases it is kept and its path is printed.
```

Update the unit-test count on the `make integration-unit` line from `32 tests` to `43 tests`.

- [ ] **Step 2: Update the parent spec**

In `docs/superpowers/specs/2026-09-19-integration-test-harness-design.md`, replace the bullet

```markdown
- CI adds an `integration` job on `ubuntu-24.04`: build `ageland` with
  `-fsanitize=address`, run the suite with the local launcher, upload run directories on
  failure. Whether the 32-bit ASan runtime is available on the runner is verified in slice
  3; if it is not, the job runs the plain build and the ASan variant is documented as
  local-only.
```

with

```markdown
- CI adds an `integration-asan` job on `ubuntu-24.04` (slice 3, own spec:
  `2026-09-19-ci-integration-asan-design.md`): build both targets with
  `-fsanitize=address`, prove the 32-bit runtime linked, run the suite with the local
  launcher, upload run directories on every outcome. The runtime is packaged for the
  runner (`lib32asan8`), so no plain-build fallback exists. The same change repaired the
  existing job's 32-bit crypt link, which had kept it red since 2026-08-14.
```

- [ ] **Step 3: Append the status line to `WIP.md`**

Append at the end of the file (after the last "Update (2026-09-19, final)" paragraph):

```markdown

Update (2026-09-19, slice 3): CI job `integration-asan` added (docs/superpowers/specs/2026-09-19-ci-integration-asan-design.md): both targets built under AddressSanitizer on ubuntu-24.04, runtime proven by ldd and help=1, gtest non-blocking, integration suite blocking, run directories uploaded on every outcome. The shared install step now installs libcrypt-dev:i386, which fixes the existing job (red since 2026-08-14). First run: <RESULT FROM TASK 5>. Draft PR: <URL FROM TASK 4>.
```

Replace the two angle-bracket fields with Task 5's outcome and Task 4's URL before committing; the line must not be committed with placeholders.

- [ ] **Step 4: Revise the timeout if Task 5 measured it**

If the measured `integration-asan` duration is under 15 minutes, change `timeout-minutes: 45` to `timeout-minutes: 30` and reword its comment to cite the measured figure; if over 30, raise it to measured time plus 50 percent. Otherwise leave it and update only the comment to cite the measurement.

- [ ] **Step 5: Verify and commit**

Run:
```sh
build/integration-venv/bin/python -m pytest tests/integration/unit -q
actionlint .github/workflows/ci.yml
grep -c $'\r' tests/integration/README.md WIP.md docs/superpowers/specs/2026-09-19-integration-test-harness-design.md .github/workflows/ci.yml
git diff --stat
```
Expected: 43 passed; actionlint silent; all CR counts 0; only the files named in this task are changed.

```sh
git commit -m "docs: AddressSanitizer recipe and blind spots in the harness README; slice 3 status

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01Kekwr9zhJai9gbEKFWnLup" -- tests/integration/README.md WIP.md docs/superpowers/specs/2026-09-19-integration-test-harness-design.md .github/workflows/ci.yml
git push origin fix/spell-room-affect-uaf-port
```

(If `.github/workflows/ci.yml` was not changed in Step 4, drop it from the pathspec.)
