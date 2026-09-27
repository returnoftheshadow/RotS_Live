"""Starts and stops one server process per execution environment and captures its log.

LocalProcessLauncher runs bin/ageland directly (Linux, CI). DockerComposeLauncher runs it
through `docker compose run` for hosts that cannot execute the i386 binary (macOS). Neither
touches the compose container_name, host port 1024, lib/, or bin/.
"""

from __future__ import annotations

import os
import socket
import subprocess
import sys
import time
import uuid
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path

LOOPBACK = "127.0.0.1"
LOCK_FILE_PREFIX = "harness-it"
DEFAULT_LOCK_DIR = Path("/tmp/rots-docker-lock")

# Host variables the local launcher forwards to the server. Everything else is dropped on
# purpose: the server must see a clean environment, and only sanitizer tuning (leak checks,
# signal handling, symbolizer location) is a legitimate host-to-server channel.
SANITIZER_ENVIRONMENT_VARIABLES = ("ASAN_OPTIONS", "LSAN_OPTIONS", "UBSAN_OPTIONS", "ASAN_SYMBOLIZER_PATH")
# Enough for a whole AddressSanitizer report (header, two stacks, shadow map) when the
# server dies before it listens and the tail is the only evidence that survives.
LOG_TAIL_BYTES = 16000


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


def read_log_tail(log_path: Path, max_bytes: int = LOG_TAIL_BYTES) -> str:
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


def terminate_process(process: subprocess.Popen) -> None:
    if process.poll() is not None:
        return
    process.terminate()
    try:
        process.wait(timeout=10)
    except subprocess.TimeoutExpired:
        process.kill()
        try:
            process.wait(timeout=10)
        except subprocess.TimeoutExpired:
            # A process the kernel will not reap (stuck in uninterruptible I/O) must not
            # turn fixture teardown into a traceback; the post-stop crash check reports
            # the missing exit status.
            print(f"server process {process.pid} survived SIGKILL for 10s", file=sys.stderr)


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

    def environment(self, seed: int) -> dict[str, str]:
        server_environment = {"PATH": os.environ.get("PATH", ""), "HOME": os.environ.get("HOME", ""), "ROTS_RANDOM_SEED": str(seed)}
        for name in SANITIZER_ENVIRONMENT_VARIABLES:
            if name in os.environ:
                server_environment[name] = os.environ[name]
        return server_environment

    def start(self, run_dir: Path, lib_dir: Path, port: int, seed: int) -> ServerHandle:
        if not self._binary.exists():
            raise RuntimeError(f"server binary missing at {self._binary}; build it first")
        log_path = run_dir / "game.log"
        environment = self.environment(seed)
        with log_path.open("wb") as log_file:
            process = subprocess.Popen(self.command(lib_dir, port), cwd=run_dir, env=environment, stdout=log_file, stderr=subprocess.STDOUT)
        try:
            handle = ServerHandle(LOOPBACK, port, log_path, process)
            wait_for_port(LOOPBACK, port, self._startup_timeout, process, log_path)
            return handle
        except Exception:
            terminate_process(process)
            raise

    def stop(self, handle: ServerHandle) -> None:
        terminate_process(handle.process)


class DockerLock:
    """One lock file per pytest session in the shared lock directory, named uniquely so two
    harness sessions on one host see each other, and held for the whole session so another job
    cannot slip in between two tests. The file is created first and the directory rechecked
    afterwards, so two sessions starting at once may both back off but can never both run."""

    def __init__(self, lock_dir: Path | None) -> None:
        self._lock_dir = lock_dir  # None disables locking (unit tests, the local launcher)
        self._lock_path: Path | None = None  # our own file while held

    @property
    def path(self) -> Path | None:
        return self._lock_path

    def acquire(self, purpose: str) -> None:
        if self._lock_dir is None:
            return
        if not self._lock_dir.is_dir():
            raise RuntimeError(f"Docker lock directory {self._lock_dir} is missing; create it (see tests/integration/README.md, Shared Docker lock)")
        others = sorted(path.name for path in self._lock_dir.glob("*.lock"))
        if others:
            raise DockerLockHeld(f"another session holds the Docker lock ({', '.join(others)}); see tests/integration/README.md")
        candidate = self._lock_dir / f"{LOCK_FILE_PREFIX}-{os.getpid()}-{uuid.uuid4().hex[:8]}.lock"
        started = datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
        body = f"session: {LOCK_FILE_PREFIX}\npid: {os.getpid()}\npurpose: {purpose}\nstart: {started}\nduration: session\n"
        try:
            with candidate.open("x", encoding="utf-8") as handle:  # O_EXCL: never overwrites
                handle.write(body)
        except FileExistsError as collision:
            raise DockerLockHeld(f"lock name collision on {candidate.name}") from collision
        self._lock_path = candidate
        # The first glob alone leaves a window in which a concurrent session also sees an empty
        # directory; a recheck after our file exists means at least one of the two sees the other.
        others = sorted(path.name for path in self._lock_dir.glob("*.lock") if path != candidate)
        if others:
            self.release()
            raise DockerLockHeld(f"another session took the Docker lock at the same time ({', '.join(others)}); see tests/integration/README.md")

    def release(self) -> None:
        if self._lock_path is not None:
            try:
                self._lock_path.unlink()
            except FileNotFoundError:
                pass
        self._lock_path = None


class DockerComposeLauncher(ServerLauncher):
    def __init__(self, repo_root: Path, binary_relative: str = "bin/ageland", service: str = "rots", startup_timeout: float = 300.0) -> None:
        self._repo_root = repo_root
        self._binary_relative = binary_relative
        self._service = service
        self._startup_timeout = startup_timeout

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

    def start(self, run_dir: Path, lib_dir: Path, port: int, seed: int) -> ServerHandle:
        process: subprocess.Popen | None = None
        try:
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
        except Exception:
            try:
                subprocess.run(["docker", "stop", "-t", "5", container_name], capture_output=True, timeout=15)
            except subprocess.TimeoutExpired:
                pass
            if process is not None:
                terminate_process(process)
            raise

    def _wait_for_published_port(self, container_name: str, container_port: int, process: subprocess.Popen, log_path: Path) -> int:
        deadline = time.monotonic() + self._startup_timeout
        while time.monotonic() < deadline:
            if process.poll() is not None:
                raise RuntimeError(f"docker compose run exited with {process.returncode}; log tail:\n{read_log_tail(log_path)}")
            try:
                result = subprocess.run(["docker", "port", container_name, f"{container_port}/tcp"], capture_output=True, text=True, timeout=10)
            except subprocess.TimeoutExpired:
                time.sleep(1.0)
                continue
            if result.returncode == 0 and ":" in result.stdout:
                return int(result.stdout.strip().rsplit(":", 1)[1])
            time.sleep(1.0)
        raise RuntimeError(f"container {container_name} never published port {container_port}; log tail:\n{read_log_tail(log_path)}")

    def stop(self, handle: ServerHandle) -> None:
        if handle.container_name:
            try:
                subprocess.run(["docker", "stop", "-t", "5", handle.container_name], capture_output=True, timeout=15)
            except subprocess.TimeoutExpired:
                pass
        if handle.is_alive():
            try:
                handle.process.wait(timeout=30)
            except subprocess.TimeoutExpired:
                handle.process.kill()
