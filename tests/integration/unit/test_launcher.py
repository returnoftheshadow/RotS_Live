from __future__ import annotations

import subprocess
from pathlib import Path

import pytest

from rots_harness import launcher


def test_local_launcher_builds_the_server_command(tmp_path: Path) -> None:
    local = launcher.LocalProcessLauncher(binary=tmp_path / "bin" / "ageland")
    command = local.command(lib_dir=tmp_path / "run" / "lib", port=4321)
    assert command == [str(tmp_path / "bin" / "ageland"), "-t", "-d", str(tmp_path / "run" / "lib"), "4321"]


def test_docker_launcher_maps_run_dir_into_the_container_and_publishes_a_random_port(tmp_path: Path) -> None:
    repo_root = tmp_path
    run_dir = repo_root / "build" / "integration" / "abc123"
    docker = launcher.DockerComposeLauncher(repo_root=repo_root, binary_relative="bin/ageland", lock_dir=None)
    command = docker.command(run_dir=run_dir, lib_dir=run_dir / "lib", port=4321, container_name="rots-it-abc123", seed=7)
    assert command[:5] == ["docker", "compose", "run", "--rm", "-T"]
    assert "--name" in command and command[command.index("--name") + 1] == "rots-it-abc123"
    assert "-p" in command and command[command.index("-p") + 1] == "127.0.0.1::4321"
    assert "ROTS_RANDOM_SEED=7" in command
    assert "--service-ports" not in command
    shell_script = command[-1]
    assert "cd /rots/build/integration/abc123" in shell_script
    assert "exec /rots/bin/ageland -t -d /rots/build/integration/abc123/lib 4321" in shell_script


def test_docker_launcher_refuses_to_start_while_another_lock_exists(tmp_path: Path) -> None:
    lock_dir = tmp_path / "locks"
    lock_dir.mkdir()
    (lock_dir / "someone-else.lock").write_text("session: other\n", encoding="utf-8")
    docker = launcher.DockerComposeLauncher(repo_root=tmp_path, binary_relative="bin/ageland", lock_dir=lock_dir)
    with pytest.raises(launcher.DockerLockHeld, match="someone-else.lock"):
        docker.acquire_lock(purpose="unit test")


def test_docker_launcher_writes_and_removes_its_own_lock(tmp_path: Path) -> None:
    lock_dir = tmp_path / "locks"
    lock_dir.mkdir()
    docker = launcher.DockerComposeLauncher(repo_root=tmp_path, binary_relative="bin/ageland", lock_dir=lock_dir)
    docker.acquire_lock(purpose="unit test")
    lock_path = lock_dir / launcher.LOCK_FILE_NAME
    assert lock_path.is_file()
    assert "purpose: unit test" in lock_path.read_text(encoding="utf-8")
    docker.release_lock()
    assert not lock_path.exists()


def test_docker_launcher_skips_the_lock_when_the_directory_is_absent(tmp_path: Path) -> None:
    docker = launcher.DockerComposeLauncher(repo_root=tmp_path, binary_relative="bin/ageland", lock_dir=tmp_path / "missing")
    docker.acquire_lock(purpose="unit test")  # no exception, nothing written
    docker.release_lock()
    assert not (tmp_path / "missing").exists()


def test_allocate_free_port_returns_a_high_port() -> None:
    port = launcher.allocate_free_port()
    assert 1024 < port < 65536


def test_local_launcher_kills_the_process_when_the_port_never_opens(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    stalling_script = tmp_path / "ageland"
    stalling_script.write_text("#!/bin/sh\nsleep 60\n", encoding="utf-8")
    stalling_script.chmod(0o700)
    run_dir = tmp_path / "run"
    run_dir.mkdir()
    lib_dir = run_dir / "lib"
    lib_dir.mkdir()

    recorded_processes: list[subprocess.Popen] = []
    real_popen = launcher.subprocess.Popen

    def recording_popen(*args: object, **kwargs: object) -> subprocess.Popen:
        spawned_process = real_popen(*args, **kwargs)
        recorded_processes.append(spawned_process)
        return spawned_process

    monkeypatch.setattr(launcher.subprocess, "Popen", recording_popen)

    local = launcher.LocalProcessLauncher(binary=stalling_script, startup_timeout=1.0)
    with pytest.raises(RuntimeError):
        local.start(run_dir, lib_dir, port=launcher.allocate_free_port(), seed=1)

    assert len(recorded_processes) == 1
    assert recorded_processes[0].poll() is not None


def test_docker_launcher_releases_the_lock_and_stops_the_container_when_start_fails(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    class FakeComposeProcess:
        def __init__(self) -> None:
            self.returncode: int | None = None
            self.terminate_called = False
            self.kill_called = False

        def poll(self) -> int | None:
            return None

        def terminate(self) -> None:
            self.terminate_called = True

        def kill(self) -> None:
            self.kill_called = True

        def wait(self, timeout: float | None = None) -> int:
            return 0

    lock_dir = tmp_path / "locks"
    lock_dir.mkdir()
    run_dir = tmp_path / "run"
    run_dir.mkdir()
    lib_dir = run_dir / "lib"
    lib_dir.mkdir()

    fake_process = FakeComposeProcess()
    recorded_argv: list[list[str]] = []

    def fake_run(argv: list[str], **kwargs: object) -> subprocess.CompletedProcess[str]:
        recorded_argv.append(argv)
        return subprocess.CompletedProcess(argv, 1, "", "")

    monkeypatch.setattr(launcher.subprocess, "Popen", lambda *args, **kwargs: fake_process)
    monkeypatch.setattr(launcher.subprocess, "run", fake_run)

    docker = launcher.DockerComposeLauncher(repo_root=tmp_path, binary_relative="bin/ageland", lock_dir=lock_dir, startup_timeout=0.2)
    with pytest.raises(RuntimeError):
        docker.start(run_dir, lib_dir, port=4321, seed=1)

    assert not (lock_dir / launcher.LOCK_FILE_NAME).exists()

    stop_calls = [argv for argv in recorded_argv if argv[:4] == ["docker", "stop", "-t", "5"]]
    assert len(stop_calls) == 1
    assert stop_calls[0][4].startswith("rots-it-")
    assert fake_process.terminate_called or fake_process.kill_called
