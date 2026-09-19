"""Starts and stops one server process per execution environment and captures its log.

LocalProcessLauncher runs bin/ageland directly (Linux, CI). DockerComposeLauncher runs it
through `docker compose run` for hosts that cannot execute the i386 binary (macOS). Neither
touches the compose container_name, host port 1024, lib/, or bin/.
"""

from __future__ import annotations

import os
import socket
import subprocess
import time
import uuid
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path

LOOPBACK = "127.0.0.1"
LOCK_FILE_NAME = "uaf-port-harness-it.lock"
DEFAULT_LOCK_DIR = Path("/tmp/rots-docker-lock")


class DockerLockHeld(RuntimeError):
    pass


@dataclass
class ServerHandle:
    host: str
    port: int
    log_path: Path
    process: subprocess.Popen
    container_name: str | None = None

    def is_alive(self) -> bool:
        return self.process.poll() is None

    def exit_status(self) -> int | None:
        return self.process.poll()


def allocate_free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as probe:
        probe.bind((LOOPBACK, 0))
        return probe.getsockname()[1]


def read_log_tail(log_path: Path, max_bytes: int = 4000) -> str:
    if not log_path.exists():
        return ""
    data = log_path.read_bytes()
    return data[-max_bytes:].decode("latin-1", errors="replace")


def wait_for_port(host: str, port: int, timeout_seconds: float, process: subprocess.Popen, log_path: Path) -> None:
    deadline = time.monotonic() + timeout_seconds
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError(f"server exited with status {process.returncode} before listening; log tail:\n{read_log_tail(log_path)}")
        try:
            with socket.create_connection((host, port), timeout=1.0):
                return
        except OSError:
            time.sleep(0.25)
    raise RuntimeError(f"server did not listen on {host}:{port} within {timeout_seconds}s; log tail:\n{read_log_tail(log_path)}")


class ServerLauncher:
    def start(self, run_dir: Path, lib_dir: Path, port: int, seed: int) -> ServerHandle:
        raise NotImplementedError

    def stop(self, handle: ServerHandle) -> None:
        raise NotImplementedError


class LocalProcessLauncher(ServerLauncher):
    def __init__(self, binary: Path, startup_timeout: float = 60.0) -> None:
        self._binary = binary
        self._startup_timeout = startup_timeout

    def command(self, lib_dir: Path, port: int) -> list[str]:
        return [str(self._binary), "-t", "-d", str(lib_dir), str(port)]

    def start(self, run_dir: Path, lib_dir: Path, port: int, seed: int) -> ServerHandle:
        if not self._binary.exists():
            raise RuntimeError(f"server binary missing at {self._binary}; build it first")
        log_path = run_dir / "game.log"
        environment = {"PATH": os.environ.get("PATH", ""), "HOME": os.environ.get("HOME", ""), "ROTS_RANDOM_SEED": str(seed)}
        with log_path.open("wb") as log_file:
            process = subprocess.Popen(self.command(lib_dir, port), cwd=run_dir, env=environment, stdout=log_file, stderr=subprocess.STDOUT)
        handle = ServerHandle(LOOPBACK, port, log_path, process)
        wait_for_port(LOOPBACK, port, self._startup_timeout, process, log_path)
        return handle

    def stop(self, handle: ServerHandle) -> None:
        if handle.is_alive():
            handle.process.terminate()
            try:
                handle.process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                handle.process.kill()
                handle.process.wait(timeout=10)


class DockerComposeLauncher(ServerLauncher):
    def __init__(self, repo_root: Path, binary_relative: str = "bin/ageland", lock_dir: Path | None = DEFAULT_LOCK_DIR, service: str = "rots", startup_timeout: float = 300.0) -> None:
        self._repo_root = repo_root
        self._binary_relative = binary_relative
        self._lock_dir = lock_dir
        self._service = service
        self._startup_timeout = startup_timeout
        self._lock_path: Path | None = None

    def container_path(self, host_path: Path) -> str:
        relative = host_path.resolve().relative_to(self._repo_root.resolve())
        return "/rots/" + relative.as_posix()

    def command(self, run_dir: Path, lib_dir: Path, port: int, container_name: str, seed: int) -> list[str]:
        script = f"cd {self.container_path(run_dir)} && exec /rots/{self._binary_relative} -t -d {self.container_path(lib_dir)} {port}"
        return [
            "docker", "compose", "run", "--rm", "-T",
            "--name", container_name,
            "-p", f"{LOOPBACK}::{port}",
            "-e", f"ROTS_RANDOM_SEED={seed}",
            self._service, "bash", "-lc", script,
        ]

    def acquire_lock(self, purpose: str) -> None:
        if self._lock_dir is None or not self._lock_dir.is_dir():
            return
        others = [path for path in self._lock_dir.glob("*.lock") if path.name != LOCK_FILE_NAME]
        if others:
            names = ", ".join(path.name for path in others)
            raise DockerLockHeld(f"another session holds the Docker lock ({names}); see {self._lock_dir / 'README.txt'}")
        self._lock_path = self._lock_dir / LOCK_FILE_NAME
        started = datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
        self._lock_path.write_text(
            f"session: uaf-port-harness-it\nrepo: {self._repo_root}\nservice: {self._service}\npurpose: {purpose}\nstart: {started}\nduration: 30m\n",
            encoding="utf-8",
        )

    def release_lock(self) -> None:
        if self._lock_path is not None and self._lock_path.exists():
            self._lock_path.unlink()
        self._lock_path = None

    def start(self, run_dir: Path, lib_dir: Path, port: int, seed: int) -> ServerHandle:
        self.acquire_lock(purpose="integration test server")
        container_name = f"rots-it-{uuid.uuid4().hex[:12]}"
        log_path = run_dir / "game.log"
        environment = dict(os.environ)
        environment["ROTS_UID"] = str(os.getuid())
        environment["ROTS_GID"] = str(os.getgid())
        with log_path.open("wb") as log_file:
            process = subprocess.Popen(self.command(run_dir, lib_dir, port, container_name, seed), cwd=self._repo_root, env=environment, stdout=log_file, stderr=subprocess.STDOUT)
        host_port = self._wait_for_published_port(container_name, port, process, log_path)
        handle = ServerHandle(LOOPBACK, host_port, log_path, process, container_name)
        wait_for_port(LOOPBACK, host_port, self._startup_timeout, process, log_path)
        return handle

    def _wait_for_published_port(self, container_name: str, container_port: int, process: subprocess.Popen, log_path: Path) -> int:
        deadline = time.monotonic() + self._startup_timeout
        while time.monotonic() < deadline:
            if process.poll() is not None:
                raise RuntimeError(f"docker compose run exited with {process.returncode}; log tail:\n{read_log_tail(log_path)}")
            result = subprocess.run(["docker", "port", container_name, f"{container_port}/tcp"], capture_output=True, text=True)
            if result.returncode == 0 and ":" in result.stdout:
                return int(result.stdout.strip().rsplit(":", 1)[1])
            time.sleep(1.0)
        raise RuntimeError(f"container {container_name} never published port {container_port}; log tail:\n{read_log_tail(log_path)}")

    def stop(self, handle: ServerHandle) -> None:
        try:
            if handle.container_name:
                subprocess.run(["docker", "stop", "-t", "5", handle.container_name], capture_output=True)
            if handle.is_alive():
                try:
                    handle.process.wait(timeout=30)
                except subprocess.TimeoutExpired:
                    handle.process.kill()
        finally:
            self.release_lock()
