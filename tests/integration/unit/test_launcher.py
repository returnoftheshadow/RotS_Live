from __future__ import annotations

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
