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
    docker = launcher.DockerComposeLauncher(repo_root=repo_root, binary_relative="bin/ageland")
    command = docker.command(run_dir=run_dir, lib_dir=run_dir / "lib", port=4321, container_name="rots-it-abc123", seed=7)
    assert command[:5] == ["docker", "compose", "run", "--rm", "-T"]
    assert "--name" in command and command[command.index("--name") + 1] == "rots-it-abc123"
    assert "-p" in command and command[command.index("-p") + 1] == "127.0.0.1::4321"
    assert "ROTS_RANDOM_SEED=7" in command
    assert "--service-ports" not in command
    shell_script = command[-1]
    assert "cd /rots/build/integration/abc123" in shell_script
    assert "exec /rots/bin/ageland -t -d /rots/build/integration/abc123/lib 4321" in shell_script


def test_docker_lock_refuses_while_another_lock_exists(tmp_path: Path) -> None:
    lock_dir = tmp_path / "locks"
    lock_dir.mkdir()
    (lock_dir / "someone-else.lock").write_text("session: other\n", encoding="utf-8")
    lock = launcher.DockerLock(lock_dir)
    with pytest.raises(launcher.DockerLockHeld, match="someone-else.lock"):
        lock.acquire(purpose="unit test")
    assert lock.path is None
    assert sorted(path.name for path in lock_dir.iterdir()) == ["someone-else.lock"]


def test_docker_lock_writes_a_uniquely_named_file_and_removes_it_on_release(tmp_path: Path) -> None:
    lock_dir = tmp_path / "locks"
    lock_dir.mkdir()
    lock = launcher.DockerLock(lock_dir)
    lock.acquire(purpose="unit test")
    lock_path = lock.path
    assert lock_path is not None and lock_path.is_file()
    assert lock_path.parent == lock_dir
    assert lock_path.name.startswith(f"{launcher.LOCK_FILE_PREFIX}-") and lock_path.name.endswith(".lock")
    assert "purpose: unit test" in lock_path.read_text(encoding="utf-8")
    lock.release()
    assert not lock_path.exists()
    assert lock.path is None


def test_second_docker_lock_refuses_while_the_first_is_held(tmp_path: Path) -> None:
    lock_dir = tmp_path / "locks"
    lock_dir.mkdir()
    first = launcher.DockerLock(lock_dir)
    first.acquire(purpose="first session")
    assert first.path is not None
    second = launcher.DockerLock(lock_dir)
    with pytest.raises(launcher.DockerLockHeld, match=first.path.name):
        second.acquire(purpose="second session")
    assert second.path is None
    first.release()


def test_docker_lock_fails_when_the_directory_is_missing(tmp_path: Path) -> None:
    lock = launcher.DockerLock(tmp_path / "missing")
    with pytest.raises(RuntimeError, match="missing"):
        lock.acquire(purpose="unit test")
    assert not (tmp_path / "missing").exists()


def test_docker_lock_without_a_directory_does_nothing() -> None:
    lock = launcher.DockerLock(None)
    lock.acquire(purpose="unit test")
    assert lock.path is None
    lock.release()


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


def test_docker_launcher_stops_the_container_when_start_fails(
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

    docker = launcher.DockerComposeLauncher(repo_root=tmp_path, binary_relative="bin/ageland", startup_timeout=0.2)
    with pytest.raises(RuntimeError):
        docker.start(run_dir, lib_dir, port=4321, seed=1)

    stop_calls = [argv for argv in recorded_argv if argv[:4] == ["docker", "stop", "-t", "5"]]
    assert len(stop_calls) == 1
    assert stop_calls[0][4].startswith("rots-it-")
    assert fake_process.terminate_called or fake_process.kill_called


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


def test_terminate_process_warns_instead_of_raising_when_the_process_survives_sigkill(
    capsys: pytest.CaptureFixture[str],
) -> None:
    class UnreapableProcess:
        def __init__(self) -> None:
            self.pid = 4242
            self.returncode: int | None = None
            self.terminate_calls = 0
            self.kill_calls = 0

        def poll(self) -> int | None:
            return None

        def terminate(self) -> None:
            self.terminate_calls += 1

        def kill(self) -> None:
            self.kill_calls += 1

        def wait(self, timeout: float | None = None) -> int:
            raise subprocess.TimeoutExpired(cmd="ageland", timeout=timeout or 0)

    stuck_process = UnreapableProcess()

    launcher.terminate_process(stuck_process)  # type: ignore[arg-type]

    assert stuck_process.terminate_calls == 1
    assert stuck_process.kill_calls == 1, "terminate_process must escalate to SIGKILL exactly once"
    assert "survived SIGKILL" in capsys.readouterr().err
