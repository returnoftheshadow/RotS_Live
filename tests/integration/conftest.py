from __future__ import annotations

import os
import platform
import shutil
import uuid
from dataclasses import dataclass
from pathlib import Path

import pytest

from rots_harness import fixtures
from rots_harness.crashmonitor import CrashMonitor
from rots_harness.launcher import DEFAULT_LOCK_DIR, DockerComposeLauncher, DockerLock, LocalProcessLauncher, ServerHandle, ServerLauncher, allocate_free_port
from rots_harness.libbuilder import RunLibBuilder
from rots_harness.retention import keep_run_directory
from rots_harness.session import GameSession, Transcript

INTEGRATION_ROOT = Path(__file__).resolve().parent
REPO_ROOT = INTEGRATION_ROOT.parents[1]
DEFAULT_SEED = 20260919


@dataclass
class HarnessServer:
    handle: ServerHandle
    lib_dir: Path
    run_dir: Path
    roster: tuple[fixtures.CharacterSpec, ...]
    monitor: CrashMonitor
    crash_detected: bool = False  # set by fail_on_server_crash before it fails the test

    def character_number(self, name: str) -> int:
        return next(index for index, spec in enumerate(self.roster, start=1) if spec.name == name)

    def spec(self, name: str) -> fixtures.CharacterSpec:
        return next(spec for spec in self.roster if spec.name == name)


def choose_launcher() -> ServerLauncher:
    binary_relative = os.environ.get("ROTS_IT_BINARY", "bin/ageland")
    mode = os.environ.get("ROTS_IT_LAUNCHER")
    if mode is None:
        mode = "local" if platform.system() == "Linux" else "docker"
    if mode == "local":
        return LocalProcessLauncher(REPO_ROOT / binary_relative)
    if mode == "docker":
        return DockerComposeLauncher(REPO_ROOT, binary_relative)
    raise RuntimeError(f"ROTS_IT_LAUNCHER must be 'local' or 'docker', not {mode!r}")


@pytest.fixture(scope="session")
def docker_lock():
    """Held for the whole pytest session when the docker launcher is in use."""
    mode = os.environ.get("ROTS_IT_LAUNCHER") or ("local" if platform.system() == "Linux" else "docker")
    if mode != "docker":
        yield None
        return
    lock_dir = Path(os.environ["ROTS_IT_DOCKER_LOCK_DIR"]) if "ROTS_IT_DOCKER_LOCK_DIR" in os.environ else DEFAULT_LOCK_DIR
    lock = DockerLock(lock_dir)
    lock.acquire(purpose="integration test session")
    try:
        yield lock
    finally:
        lock.release()


@pytest.fixture  # each test gets a fresh server: no command reliably strips a room affect, so isolation is by reboot
def server(request: pytest.FixtureRequest, docker_lock: DockerLock | None) -> HarnessServer:  # docker_lock: requested only so the session lock is held first
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
        shutdown_problems = harness_server.monitor.check_after_stop()
        if shutdown_problems:
            harness_server.crash_detected = True
        keep = keep_run_directory(
            keep_requested=os.environ.get("ROTS_IT_KEEP") == "1",
            tests_failed_so_far=request.session.testsfailed,
            this_server_failed=harness_server.crash_detected,
        )
        if keep:
            print(f"\nrun directory kept at {run_dir}")
        else:
            shutil.rmtree(run_dir, ignore_errors=True)
        if shutdown_problems:
            pytest.fail("server problems during shutdown:\n" + "\n".join(shutdown_problems))


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


def _login(server: HarnessServer, name: str) -> GameSession:
    session = GameSession(server.handle, server.spec(name), server.character_number(name), server.run_dir)
    session.login()
    return session


def _session_fixture(fixture_name: str, character_name: str):
    @pytest.fixture(name=fixture_name)
    def session(server: HarnessServer):
        game_session = _login(server, character_name)
        yield game_session
        if game_session.is_closed:
            return  # the test quit or dropped the link itself
        game_session.quit()  # QuitRefused fails the test: a character left fighting is a scenario bug
    return session


imp = _session_fixture("imp", "Harnimp")
mage = _session_fixture("mage", "Harnmage")
fighter = _session_fixture("fighter", "Harnfighter")
victim = _session_fixture("victim", "Harnvictim")
caller = _session_fixture("caller", "Harncaller")
pupil = _session_fixture("pupil", "Harnpupil")
novice = _session_fixture("novice", "Harnnovice")


class Harness:
    def __init__(self, imp_session: GameSession) -> None:
        self._imp = imp_session

    def tick(self) -> Transcript:
        self._imp.drain(0.1)
        self._imp.send_line("harness tick")
        text = self._imp.expect(["Harness: hourly tick complete."], 20.0)
        return Transcript(text)

    def affects(self) -> Transcript:
        """One forced affect_update() pass (spec B1): forces the one slow *person*-affect phase
        compare and skips fast_update()'s regen. This still runs the room sweep every call --
        affect_update() also walks TARGET_ROOM entries into affect_update_room (limits.cpp), so a
        room affect (blaze, mist, poison's room arm) still rolls occupants and spends its own
        duration on this call; only its application roll and any regen are left unforced/skipped.
        See scenarios/blaze_support.py's module docstring for the full account.
        """
        self._imp.drain(0.1)
        self._imp.send_line("harness affects")
        text = self._imp.expect(["Harness: affect tick complete."], 20.0)
        return Transcript(text)


@pytest.fixture
def harness(imp: GameSession) -> Harness:
    return Harness(imp)
