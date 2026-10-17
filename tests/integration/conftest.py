"""Fixtures that give each scenario a fresh server built from the characters its marker declares."""

from __future__ import annotations

import os
import platform
import shutil
import sys
import uuid
from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable, Optional

import pytest

from rots_harness import groups, interrupts
from rots_harness.characters import CharacterSet
from rots_harness.crashmonitor import CrashMonitor
from rots_harness.dockerlock import DEFAULT_LOCK_DIR, DEFAULT_WAIT_SECONDS, DockerLock, parse_wait_seconds
from rots_harness.launcher import (
    HARNESS_BINARY_RELATIVE,
    DockerComposeLauncher,
    LocalProcessLauncher,
    ServerHandle,
    ServerLauncher,
    allocate_free_port,
    check_repo_relative,
)
from rots_harness.libbuilder import ROTSTOOL_RELATIVE, LibTemplateBuilder, RunLibProvider
from rots_harness.retention import RunDirectoryReport, keep_run_directory
from rots_harness.session import GameSession, end_sessions
from rots_harness.timecontrol import OUTPUT_LOG_NAME, TimeControl, parse_hour_timeout, schedule_name_from_marker

# tests/integration: this file, pytest.ini and the test world.
INTEGRATION_ROOT = Path(__file__).resolve().parent
# The checkout whose bin/ and lib/ the harness uses; containers mount it at /rots.
REPO_ROOT = INTEGRATION_ROOT.parents[1]
# One directory per test: its lib, game.log and one transcript per character.
RUNS_ROOT = REPO_ROOT / "build" / "integration" / "runs"
# One template lib per character set, built once per session.
TEMPLATES_ROOT = REPO_ROOT / "build" / "integration" / "libs"
# The seed rotstool and every server use unless ROTS_IT_SEED gives another.
DEFAULT_SEED = 20261011
# The server binary when ROTS_IT_BINARY is unset.
DEFAULT_BINARY_RELATIVE = "bin/ageland"
# Names the test that used a run directory; written when the directory is made. The hyphen keeps
# it apart from the transcripts, which are named after characters, whose names are letters only.
TEST_NAME_FILE = "test-id.txt"
# Where pytest_configure keeps the function that puts back the signal handlers it replaced.
RESTORE_SIGNAL_HANDLERS_KEY = pytest.StashKey[Callable[[], None]]()
# Seconds added to every advance_hours wait for the reply to travel.
HOUR_TIMEOUT_MARGIN_SECONDS = 10.0
# Seconds the server fixture waits for a harness server's ready line once its login prompt has arrived.
READY_TIMEOUT_SECONDS = 30.0


# Where pytest_configure keeps the run's RunDirectoryReport.
RUN_DIRECTORY_REPORT_KEY = pytest.StashKey[RunDirectoryReport]()


@pytest.hookimpl(trylast=True)
def pytest_configure(config: pytest.Config) -> None:
    """Stops the run with a usage error for an unregistered marker in -m or a bad ROTS_IT_* variable.

    Runs after the built-in plugins have registered their markers, so -m may name them. When
    the checks pass, makes SIGTERM and SIGHUP interrupt the run like Ctrl-C, so the Docker lock
    is released and every started server stopped, and starts the kept run directory report.
    """
    expression = config.getoption("markexpr", default="") or ""
    registered = groups.registered_marker_names(config.getini("markers"))
    unknown = groups.unknown_marker_names(expression, registered)
    if unknown:
        raise pytest.UsageError(f"unknown group or marker {', '.join(unknown)} in -m {expression!r}; registered: {', '.join(registered)}")
    validate_environment()
    config.stash[RESTORE_SIGNAL_HANDLERS_KEY] = interrupts.install_interrupt_handlers()
    report = RunDirectoryReport()
    config.pluginmanager.register(report)
    config.stash[RUN_DIRECTORY_REPORT_KEY] = report


def pytest_unconfigure(config: pytest.Config) -> None:
    """Puts back the signal handlers pytest_configure replaced.

    Prints the kept run directories to stderr when no terminal summary listed them.
    """
    restore_signal_handlers = config.stash.get(RESTORE_SIGNAL_HANDLERS_KEY, None)
    if restore_signal_handlers is not None:
        restore_signal_handlers()
    report = config.stash.get(RUN_DIRECTORY_REPORT_KEY, None)
    if report is not None and report.kept and config.pluginmanager.get_plugin("terminalreporter") is None:
        print("kept run directories:", *report.summary_lines(), sep="\n", file=sys.stderr, flush=True)


def validate_environment() -> None:
    """Raises pytest.UsageError naming the first ROTS_IT_* variable whose value the harness cannot use."""
    launcher_mode()
    session_seed()
    lock_wait_seconds()
    binary_relative()
    hour_timeout_seconds()


def terminal_line_writer(config: pytest.Config) -> Callable[[str], None]:
    """A function that writes one line to the terminal at once, even while pytest captures output.

    Falls back to stderr when the capture or terminal plugin is absent (for example under -p no:terminal).
    """
    capture_manager = config.pluginmanager.get_plugin("capturemanager")
    terminal_reporter = config.pluginmanager.get_plugin("terminalreporter")
    if capture_manager is None or terminal_reporter is None:
        return lambda message: print(message, file=sys.stderr, flush=True)

    def write_line(message: str) -> None:
        with capture_manager.global_and_fixture_disabled():
            terminal_reporter.write_line(message)
            # Piped or redirected output is block-buffered; without the flush the line waits there.
            terminal_reporter.flush()

    return write_line


def launcher_mode() -> str:
    """'local' or 'docker' from ROTS_IT_LAUNCHER, defaulting to local on Linux and Docker elsewhere."""
    mode = os.environ.get("ROTS_IT_LAUNCHER") or ("local" if platform.system() == "Linux" else "docker")
    if mode not in ("local", "docker"):
        raise pytest.UsageError(f"ROTS_IT_LAUNCHER must be 'local' or 'docker', not {mode!r}")
    return mode


def keep_requested() -> bool:
    """Whether ROTS_IT_KEEP=1 asks to keep every run directory and template."""
    return os.environ.get("ROTS_IT_KEEP") == "1"


def session_seed() -> int:
    """The random seed for rotstool and every server, from ROTS_IT_SEED or the default; raises pytest.UsageError."""
    value = os.environ.get("ROTS_IT_SEED")
    if value is None:
        return DEFAULT_SEED
    try:
        return int(value)
    except ValueError:
        raise pytest.UsageError(f"ROTS_IT_SEED must be an integer, not {value!r}") from None


def lock_wait_seconds() -> float:
    """Seconds to wait for the Docker lock, from ROTS_IT_LOCK_WAIT or the default; raises pytest.UsageError."""
    value = os.environ.get("ROTS_IT_LOCK_WAIT")
    if value is None:
        return DEFAULT_WAIT_SECONDS
    try:
        return parse_wait_seconds(value)
    except ValueError:
        raise pytest.UsageError(f"ROTS_IT_LOCK_WAIT must be a finite number of seconds of at least 0, not {value!r}") from None


def binary_relative() -> str:
    """The server binary from ROTS_IT_BINARY or the default; raises pytest.UsageError unless it names a path below the repo root."""
    value = os.environ.get("ROTS_IT_BINARY", DEFAULT_BINARY_RELATIVE)
    try:
        check_repo_relative(value)
    except ValueError:
        raise pytest.UsageError(f"ROTS_IT_BINARY must name a file relative to the repository root and inside it, not {value!r}") from None
    return value


def hour_timeout_seconds() -> float:
    """Seconds advance_hours allows per game hour, from ROTS_IT_HOUR_TIMEOUT or the default; raises pytest.UsageError."""
    value = os.environ.get("ROTS_IT_HOUR_TIMEOUT")
    try:
        return parse_hour_timeout(value)
    except ValueError:
        raise pytest.UsageError(f"ROTS_IT_HOUR_TIMEOUT must be a positive, finite number of seconds, not {value!r}") from None


@dataclass
class HarnessServer:
    """One test's running server, its lib and run directory, and the sessions logged in to it."""

    # The started server; the server fixture stops it after the test.
    handle: ServerHandle
    # This test's own copy of the character set's template lib.
    lib_dir: Path
    # Holds the lib, game.log and one transcript per character.
    run_dir: Path
    # The characters the test's marker declared; login() looks names up here.
    character_set: CharacterSet
    # Reads the server's log and exit state for crash evidence.
    monitor: CrashMonitor
    # Every session login() opened, quit by the server fixture after the test.
    sessions: list[GameSession] = field(default_factory=list)
    # Set by the crash checks; keeps the run directory.
    crash_detected: bool = False
    # What the crash check found during an interrupted run, listed under the kept run directory
    # instead of failing the test.
    interrupted_problems: list[str] = field(default_factory=list)
    # Drives game time on a server started with @pytest.mark.schedule(...); None otherwise.
    time_control: Optional[TimeControl] = None

    def login(self, name: str) -> GameSession:
        """Logs the named character in and returns its session; the fixture quits it after the test."""
        character = self.character_set.character(name)
        game_session = GameSession(self.handle, character.name, self.character_set.email, self.character_set.password, self.run_dir)
        try:
            game_session.login()
        except BaseException:
            game_session.close()
            raise
        self.sessions.append(game_session)
        return game_session

    def advance_hours(self, hours: int) -> None:
        """Runs hours game hours on a harness server and returns once the server acknowledges them.

        Fails the test when the test has no @pytest.mark.schedule(...), since a plain server's time cannot be driven.
        """
        if self.time_control is None:
            pytest.fail("advance_hours needs @pytest.mark.schedule(...) on the test", pytrace=False)
        self.time_control.advance_hours(hours, hours * hour_timeout_seconds() + HOUR_TIMEOUT_MARGIN_SECONDS)


@pytest.fixture(scope="session")
def harness_launcher() -> ServerLauncher:
    """The launcher ROTS_IT_LAUNCHER selects, running ROTS_IT_BINARY (default bin/ageland).

    Raises RuntimeError with that launcher's build command when the server or rotstool is missing.
    """
    server_binary = binary_relative()
    if launcher_mode() == "local":
        selected: ServerLauncher = LocalProcessLauncher(REPO_ROOT, server_binary)
    else:
        selected = DockerComposeLauncher(REPO_ROOT, server_binary)
    selected.require_program(server_binary)
    selected.require_program(ROTSTOOL_RELATIVE)
    return selected


@pytest.fixture(scope="session")
def docker_lock(pytestconfig: pytest.Config, harness_launcher: ServerLauncher):
    """The shared Docker lock, held for the whole session when the Docker launcher is in use; None otherwise.

    Requests harness_launcher so a missing build fails before the lock wait, not after it.
    """
    if launcher_mode() != "docker":
        yield None
        return
    lock_dir = Path(os.environ.get("ROTS_IT_DOCKER_LOCK_DIR", str(DEFAULT_LOCK_DIR)))
    lock = DockerLock(lock_dir, REPO_ROOT, wait_seconds=lock_wait_seconds(), report=terminal_line_writer(pytestconfig))
    lock.acquire(purpose="integration test session")
    try:
        yield lock
    finally:
        lock.release()


@pytest.fixture(scope="session")
def lib_provider(harness_launcher: ServerLauncher, docker_lock: DockerLock | None):
    """Builds each character set's template once per session; the templates are deleted at the end unless ROTS_IT_KEEP=1."""
    builder = LibTemplateBuilder(REPO_ROOT, INTEGRATION_ROOT / "world", harness_launcher)
    provider = RunLibProvider(builder, TEMPLATES_ROOT, session_seed())
    try:
        yield provider
    finally:
        if not keep_requested():
            provider.discard_templates()


def character_set_for(request: pytest.FixtureRequest) -> CharacterSet:
    """The characters the test's @pytest.mark.characters(...) declares; fails the test when it has none."""
    marker = request.node.get_closest_marker("characters")
    if marker is None or not marker.args:
        pytest.fail(f"{request.node.nodeid} uses the server fixture without @pytest.mark.characters(...)", pytrace=False)
    return CharacterSet(tuple(marker.args))


def schedule_for(request: pytest.FixtureRequest) -> Optional[str]:
    """The schedule the test's @pytest.mark.schedule(...) names, or None without one; fails the test on a malformed marker."""
    marker = request.node.get_closest_marker("schedule")
    if marker is None:
        return None
    try:
        return schedule_name_from_marker(marker.args)
    except ValueError as malformed:
        pytest.fail(f"{request.node.nodeid}: {malformed}", pytrace=False)


@pytest.fixture
def server(request: pytest.FixtureRequest, harness_launcher: ServerLauncher, lib_provider: RunLibProvider):
    """A fresh server on a lib holding the test's declared characters, as a HarnessServer.

    After the test it quits every session, stops the server, and fails the test when a session
    does not quit or the shutdown shows problems; during an interrupted run those problems are
    listed under the kept run directory instead. The run directory, whose test-id.txt names the
    test, is deleted unless the lib or server failed to set up after the lib was copied, any
    test in the session has failed, this server's checks failed, the run was interrupted, or
    ROTS_IT_KEEP=1. Kept directories are listed at the end of the run. A test marked
    @pytest.mark.schedule(name) gets the harness binary with that schedule, whose time
    advance_hours drives.
    """
    report = request.config.stash[RUN_DIRECTORY_REPORT_KEY]
    character_set = character_set_for(request)
    schedule = schedule_for(request)
    if schedule is not None:
        harness_launcher.require_program(HARNESS_BINARY_RELATIVE)
    run_dir = RUNS_ROOT / uuid.uuid4().hex[:12]
    run_dir.mkdir(parents=True)
    (run_dir / TEST_NAME_FILE).write_text(request.node.nodeid + "\n", encoding="utf-8")
    try:
        lib_dir = lib_provider.lib_for(character_set, run_dir)
        handle = harness_launcher.start(run_dir, lib_dir, allocate_free_port(), session_seed(), schedule=schedule)
        time_control = None
        if schedule is not None:
            # Any failure here stops the server, including the constructor's OSError for an
            # output log it cannot open.
            try:
                time_control = TimeControl(handle.process.stdin, handle.process.stdout, run_dir / OUTPUT_LOG_NAME)
                time_control.wait_ready(schedule, READY_TIMEOUT_SECONDS)
            except BaseException:
                harness_launcher.stop(handle)
                if time_control is not None:
                    time_control.close()
                raise
    except BaseException:
        # BaseException so an interrupted setup keeps its evidence too. keep_run_directory never
        # sees this case: a run directory holding more than test-id.txt is always kept, because its
        # game.log and lib are the only evidence of why the server did not start. A failure
        # before the lib was copied (a rotstool refusal) leaves nothing else in it.
        if any(entry.name != TEST_NAME_FILE for entry in run_dir.iterdir()):
            report.record_kept(run_dir, request.node.nodeid)
        else:
            shutil.rmtree(run_dir, ignore_errors=True)
        raise
    harness_server = HarnessServer(handle, lib_dir, run_dir, character_set, CrashMonitor(handle), time_control=time_control)
    try:
        yield harness_server
    finally:
        interruption: KeyboardInterrupt | None = None
        teardown_problems: list[str] = []
        try:
            try:
                teardown_problems = end_sessions(harness_server.sessions, handle.is_alive())
            finally:
                harness_launcher.stop(handle)
        except KeyboardInterrupt as caught:
            # Ctrl-C, SIGTERM or SIGHUP while sessions quit or the server stops. Stopping again
            # finishes a stop the interrupt cut short; the interrupt is raised once the run
            # directory is recorded.
            interruption = caught
            harness_launcher.stop(handle)
        if harness_server.time_control is not None:
            harness_server.time_control.close()
        problems = teardown_problems + harness_server.monitor.check_after_stop()
        interrupted = report.interrupted or interruption is not None
        keep = keep_run_directory(keep_requested(), request.session.testsfailed, harness_server.crash_detected or bool(problems), interrupted)
        if keep:
            notes = harness_server.interrupted_problems + problems if interrupted else []
            report.record_kept(run_dir, request.node.nodeid, notes)
        else:
            shutil.rmtree(run_dir, ignore_errors=True)
        if interruption is not None:
            raise interruption
        # An interrupt that reaches the server too ends it, so during one the problems are only
        # listed with the kept run directory and the run's exit status reports the interrupt.
        if problems and not interrupted:
            pytest.fail("problems while ending the test:\n" + "\n".join(problems))


@pytest.fixture(autouse=True)
def fail_on_server_crash(request: pytest.FixtureRequest):
    """Fails a test that uses the server fixture when the server shows crash evidence by the end of the test.

    During an interrupted run the evidence is listed with the kept run directory instead.
    """
    # Requesting server here registers its finalizer first; finalizers run last in, first out,
    # so this check runs while the server is still up, before the server fixture quits sessions.
    harness_server = request.getfixturevalue("server") if "server" in request.fixturenames else None
    yield
    if harness_server is not None:
        problems = harness_server.monitor.check()
        if problems:
            harness_server.crash_detected = True
            if request.config.stash[RUN_DIRECTORY_REPORT_KEY].interrupted:
                harness_server.interrupted_problems.extend(problems)
            else:
                pytest.fail("server problems during this test:\n" + "\n".join(problems))
