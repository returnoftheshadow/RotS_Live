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
from rots_harness.launcher import DEFAULT_LOCK_DIR, DockerComposeLauncher, LocalProcessLauncher, ServerHandle, ServerLauncher, allocate_free_port
from rots_harness.libbuilder import RunLibBuilder
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
        lock_dir = Path(os.environ["ROTS_IT_DOCKER_LOCK_DIR"]) if "ROTS_IT_DOCKER_LOCK_DIR" in os.environ else DEFAULT_LOCK_DIR
        return DockerComposeLauncher(REPO_ROOT, binary_relative, lock_dir)
    raise RuntimeError(f"ROTS_IT_LAUNCHER must be 'local' or 'docker', not {mode!r}")


@pytest.fixture(scope="session")
def server(request: pytest.FixtureRequest) -> HarnessServer:
    run_dir = REPO_ROOT / "build" / "integration" / uuid.uuid4().hex[:12]
    run_dir.mkdir(parents=True)
    built = RunLibBuilder(REPO_ROOT, INTEGRATION_ROOT / "world", INTEGRATION_ROOT / "fixtures" / "character.template.json").build(run_dir, fixtures.STANDARD_ROSTER)
    launcher = choose_launcher()
    seed = int(os.environ.get("ROTS_IT_SEED", DEFAULT_SEED))
    handle = launcher.start(run_dir, built.lib_dir, allocate_free_port(), seed)
    harness_server = HarnessServer(handle, built.lib_dir, run_dir, built.roster, CrashMonitor(handle))
    try:
        yield harness_server
    finally:
        launcher.stop(handle)
        keep = os.environ.get("ROTS_IT_KEEP") == "1" or request.session.testsfailed > 0
        if keep:
            print(f"\nrun directory kept at {run_dir}")
        else:
            shutil.rmtree(run_dir, ignore_errors=True)


@pytest.fixture(autouse=True)
def fail_on_server_crash(request: pytest.FixtureRequest):
    yield
    if "server" in request.fixturenames:
        harness_server: HarnessServer = request.getfixturevalue("server")
        problems = harness_server.monitor.check()
        if problems:
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
        try:
            game_session.quit()
        except Exception:
            game_session.close()
    return session


imp = _session_fixture("imp", "Harnimp")
mage = _session_fixture("mage", "Harnmage")
fighter = _session_fixture("fighter", "Harnfighter")
victim = _session_fixture("victim", "Harnvictim")


class Harness:
    def __init__(self, imp_session: GameSession) -> None:
        self._imp = imp_session

    def tick(self) -> Transcript:
        transcript = self._imp.command("harness tick", timeout=20.0)
        assert transcript.contains("Harness: hourly tick complete."), transcript.text
        return transcript


@pytest.fixture
def harness(imp: GameSession) -> Harness:
    return Harness(imp)
