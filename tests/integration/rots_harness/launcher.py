"""Runs the repo's i386 programs in one execution environment: the server until stopped, a tool to completion."""

from __future__ import annotations

import errno
import os
import random
import re
import shlex
import socket
import subprocess
import sys
import time
import uuid
from contextlib import contextmanager
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path
from typing import Any, BinaryIO, Callable, Iterator, Optional

LOOPBACK = "127.0.0.1"
INTEGRATION_TMP_RELATIVE = "build/integration/tmp"
BUILD_COMMAND = "make configure && cmake --build build --target ageland ageland_harness rotstool"
# The test build of the server that a scenario with @pytest.mark.schedule(...) runs.
HARNESS_BINARY_RELATIVE = "bin/ageland_harness"

# Host variables the local launcher forwards to the server. Everything else is dropped so the
# server sees a clean environment; sanitizer tuning (leak checks, signal handling, symbolizer
# location) is the only legitimate host-to-server channel.
SANITIZER_ENVIRONMENT_VARIABLES = ("ASAN_OPTIONS", "LSAN_OPTIONS", "UBSAN_OPTIONS", "ASAN_SYMBOLIZER_PATH")
# Enough for a whole AddressSanitizer report (header, two stacks, shadow map) when the server
# dies before it listens and the tail is the only evidence left.
LOG_TAIL_BYTES = 16000
# The first prompt the server sends a new connection, once its game loop accepts connections. The
# server listens before it boots the world, so readiness waits for this rather than the port.
LOGIN_EMAIL_PROMPT = "Account email:"
# The pause between two readiness attempts, and the longest one read blocks before the readiness
# wait checks the process again.
READY_RETRY_SECONDS = 0.25
# The pause before the readiness wait retries a refused connection: a refusal comes back at once,
# so the server is polled often enough to be found within moments of listening.
REFUSED_RETRY_SECONDS = 0.05
# The line the harness binary logs once its stdin reaches end of input; its control channel's
# reader thread has finished by then.
INPUT_CLOSED_LOG_LINE = "Harness: the control channel's input has closed; no more time requests will arrive."
# The longest a stop waits for INPUT_CLOSED_LOG_LINE before it stops the server anyway.
INPUT_CLOSED_WAIT_SECONDS = 5.0
# The pause between two reads of game.log while a stop waits for INPUT_CLOSED_LOG_LINE.
INPUT_CLOSED_POLL_SECONDS = 0.05
# What the server logs before it exits when another process holds its port: init_socket in
# src/comm.cpp reports the failed bind with perror.
PORT_IN_USE_LOG_TEXT = "bind: Address already in use"
# How many ports start_on_a_free_port tries before it gives up on a port taken each time.
PORT_ATTEMPTS = 3
# What the server logs once it has bound its port and booted the world, before it can send any
# login prompt (run_the_game in src/comm.cpp). A prompt read while the local server's own log
# lacks it came from another process listening on the same port.
GAME_LOOP_LOG_TEXT = "Entering game loop."
# Seconds the local launcher waits, after reading the login prompt, for its own server's log to
# hold GAME_LOOP_LOG_TEXT; the server logs it before it sends a prompt, so this only bounds a
# prompt from another process.
OWN_SERVER_CONFIRM_SECONDS = 5.0
# The variable pytest-xdist sets in each worker to its name, gw0, gw1 and so on; unset outside a worker.
XDIST_WORKER_VARIABLE = "PYTEST_XDIST_WORKER"
# A pytest-xdist worker name and its number.
XDIST_WORKER_PATTERN = re.compile(r"gw([0-9]+)")
# Each pytest-xdist worker of a run takes its local servers' ports from its own band of
# WORKER_PORT_BAND_SIZE ports from WORKER_PORT_BASE, so no two workers are ever given the same
# port. The bands end below every common ephemeral range (Linux from 32768, macOS and Windows from
# 49152), so no outgoing connection is handed one either.
WORKER_PORT_BASE = 20000
# How many ports each worker's band holds.
WORKER_PORT_BAND_SIZE = 400
# How many bands fit below the ephemeral ranges; worker numbers past it wrap round to the first bands.
WORKER_PORT_BANDS = 30
# Picks where in its band a worker starts looking, so a band's lowest ports are not always tried
# first; its own generator, so the harness never moves a scenario's random state.
_PORT_CHOOSER = random.Random()
# Set to 1 to run a program older than its sources, for example to test an old build on purpose.
ALLOW_STALE_VARIABLE = "ROTS_IT_ALLOW_STALE"
# The folder, relative to the repository root, holding the sources the programs are built from.
SOURCE_DIRECTORY_RELATIVE = "src"
# The suffixes of the source files whose changes make a program stale.
SOURCE_SUFFIXES = (".cpp", ".h")
# The unit tests' folder under SOURCE_DIRECTORY_RELATIVE; no program the harness runs is built from it.
UNIT_TEST_SOURCES_RELATIVE = "tests"
# Folders and files under SOURCE_DIRECTORY_RELATIVE built into one program only, mapped to that program;
# every other program ignores them. Each program has its own main.cpp.
PROGRAM_ONLY_SOURCES = {"harness": HARNESS_BINARY_RELATIVE, "tools/rotstool": "bin/rotstool", "main.cpp": "bin/ageland"}


class PortInUse(RuntimeError):
    """The local server exited before it was ready because another process held the port it was given."""


@dataclass
class ServerHandle:
    """A running server: where to connect, where its output goes, and the process to stop."""

    host: str
    port: int
    log_path: Path
    process: subprocess.Popen
    container_name: Optional[str] = None
    # Whether the server is the harness binary, which reads time requests on its stdin pipe; a
    # stop closes that pipe and lets the reader finish before signalling the server.
    has_control_channel: bool = False

    def is_alive(self) -> bool:
        """Whether the server process (or the compose run wrapping it) is still running."""
        return self.process.poll() is None

    def exit_status(self) -> int | None:
        """The process's exit status, or None while it runs."""
        return self.process.poll()


@dataclass(frozen=True)
class ToolResult:
    """How a program run to completion ended: its exit status and its combined output."""

    exit_status: int
    output: str


def worker_port_band(worker_name: Optional[str]) -> Optional[range]:
    """The ports a pytest-xdist worker named worker_name (gw0, gw1, ...) gives its servers, or None outside a worker or for another name."""
    if worker_name is None:
        return None
    match = XDIST_WORKER_PATTERN.fullmatch(worker_name)
    if match is None:
        return None
    band_start = WORKER_PORT_BASE + (int(match.group(1)) % WORKER_PORT_BANDS) * WORKER_PORT_BAND_SIZE
    return range(band_start, band_start + WORKER_PORT_BAND_SIZE)


def _port_is_free(port: int) -> bool:
    """Whether a loopback TCP socket can bind port now."""
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as probe:
        try:
            probe.bind((LOOPBACK, port))
        except OSError:
            return False
    return True


def allocate_free_port() -> int:
    """A loopback TCP port that was free when probed; another process may take it afterwards.

    In a pytest-xdist worker the port comes from the worker's own band (worker_port_band), so the
    run's other workers never get it; elsewhere the system picks one. Raises RuntimeError when no
    port in the worker's band is free.
    """
    band = worker_port_band(os.environ.get(XDIST_WORKER_VARIABLE))
    if band is None:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as probe:
            probe.bind((LOOPBACK, 0))
            return probe.getsockname()[1]
    first_offset = _PORT_CHOOSER.randrange(len(band))
    for step in range(len(band)):
        port = band[(first_offset + step) % len(band)]
        if _port_is_free(port):
            return port
    raise RuntimeError(f"no port from {band.start} to {band.stop - 1} is free for this pytest-xdist worker")


def confirm_own_server(process: subprocess.Popen, log_path: Path, timeout_seconds: Optional[float] = None) -> None:
    """Returns once process is running and its log holds GAME_LOOP_LOG_TEXT, proving the login prompt read came from it.

    Raises RuntimeError with the log tail when the process exits first, as a server does when
    another process holds its port, or when the line does not appear within timeout_seconds
    (OWN_SERVER_CONFIRM_SECONDS when None).
    """
    if timeout_seconds is None:
        timeout_seconds = OWN_SERVER_CONFIRM_SECONDS
    line = GAME_LOOP_LOG_TEXT.encode("ascii")
    deadline = time.monotonic() + timeout_seconds
    while True:
        _raise_if_exited(process, log_path)
        if line in _read_from(log_path, 0):
            return
        if time.monotonic() >= deadline:
            raise RuntimeError(
                f"a login prompt arrived, but this server never logged {GAME_LOOP_LOG_TEXT!r}, so another process "
                f"answered on its port; log tail:\n{read_log_tail(log_path)}"
            )
        time.sleep(INPUT_CLOSED_POLL_SECONDS)


def read_log_tail(log_path: Path, max_bytes: int = LOG_TAIL_BYTES) -> str:
    """The last max_bytes of the log as text, or an empty string when the log does not exist."""
    if not log_path.exists():
        return ""
    data = log_path.read_bytes()
    return data[-max_bytes:].decode("latin-1", errors="replace")


def check_repo_relative(relative_path: str) -> None:
    """Raises ValueError unless relative_path is relative and names something below the repository root, not the root itself."""
    # normpath turns an empty path into ".", so the root check covers it too.
    normalized = os.path.normpath(relative_path)
    if os.path.isabs(relative_path) or normalized in (os.curdir, os.pardir) or normalized.startswith(os.pardir + os.sep):
        raise ValueError(f"{relative_path!r} must name a file by a path relative to the repository root that stays inside it")


def newest_source(source_root: Path, program_relative: str) -> Optional[tuple[Path, float]]:
    """The newest .cpp or .h file under source_root that program_relative is built from, with its modification time.

    Skips the unit tests and the folders and files built only into another program; None when no source is found.
    """
    excluded = {UNIT_TEST_SOURCES_RELATIVE}
    excluded.update(folder for folder, owner in PROGRAM_ONLY_SOURCES.items() if owner != program_relative)
    newest: Optional[tuple[Path, float]] = None
    for directory, subdirectories, file_names in os.walk(source_root):
        directory_path = Path(directory)
        relative_directory = directory_path.relative_to(source_root)
        # Pruned in place, so os.walk never descends into an excluded folder.
        subdirectories[:] = [name for name in subdirectories if (relative_directory / name).as_posix() not in excluded]
        for file_name in file_names:
            if not file_name.endswith(SOURCE_SUFFIXES) or (relative_directory / file_name).as_posix() in excluded:
                continue
            source_path = directory_path / file_name
            try:
                modified = source_path.stat().st_mtime
            except OSError:
                # A file removed during the walk cannot make the program stale.
                continue
            if newest is None or modified > newest[1]:
                newest = (source_path, modified)
    return newest


def wait_for_login_prompt(
    host: str, port: int, timeout_seconds: float, process: subprocess.Popen, log_path: Path, time_allowed: Optional[str] = None,
) -> None:
    """Returns once a connection to host:port receives the server's login prompt.

    A refused connection, or one closed before the prompt arrives (as a Docker port proxy does
    while nothing listens behind it), is retried. Raises RuntimeError with the log tail when the
    process exits first or the timeout passes; time_allowed, when given, replaces
    "within <timeout_seconds>s" in the timeout's message.
    """
    deadline = time.monotonic() + timeout_seconds
    while time.monotonic() < deadline:
        _raise_if_exited(process, log_path)
        try:
            connection = socket.create_connection((host, port), timeout=1.0)
        except ConnectionRefusedError:
            time.sleep(REFUSED_RETRY_SECONDS)
            continue
        except OSError:
            time.sleep(READY_RETRY_SECONDS)
            continue
        with connection:
            if _receives_login_prompt(connection, deadline, process, log_path):
                return
        time.sleep(READY_RETRY_SECONDS)
    _raise_if_exited(process, log_path)
    if time_allowed is None:
        time_allowed = f"within {timeout_seconds}s"
    raise RuntimeError(f"the server on {host}:{port} did not send its login prompt {time_allowed}; log tail:\n{read_log_tail(log_path)}")


def _raise_if_exited(process: subprocess.Popen, log_path: Path) -> None:
    """Raises RuntimeError with the log tail when the process has exited."""
    if process.poll() is not None:
        raise RuntimeError(f"server exited with status {process.returncode} before it was ready; log tail:\n{read_log_tail(log_path)}")


def _receives_login_prompt(connection: socket.socket, deadline: float, process: subprocess.Popen, log_path: Path) -> bool:
    """Reads until the login prompt arrives (True), or the connection closes or the deadline passes (False)."""
    prompt = LOGIN_EMAIL_PROMPT.encode("ascii")
    connection.settimeout(READY_RETRY_SECONDS)
    received = b""
    while time.monotonic() < deadline:
        _raise_if_exited(process, log_path)
        try:
            chunk = connection.recv(4096)
        except socket.timeout:
            continue
        except OSError:
            return False
        if not chunk:
            return False
        # Telnet negotiation comes before the prompt; a byte search passes over it.
        received += chunk
        if prompt in received:
            return True
        # Keeping one byte less than the prompt still finds a prompt split across two reads.
        received = received[-(len(prompt) - 1):]
    return False


@contextmanager
def _explaining_unexecutable(program_path: Path) -> Iterator[None]:
    """Turns the exec error for an existing program this host cannot execute into a RuntimeError saying so."""
    try:
        yield
    except OSError as failure:
        # The error names the program when exec itself failed; a missing working directory names
        # the directory instead.
        if failure.filename != str(program_path):
            raise
        if failure.errno == errno.ENOENT:
            # A Linux host without the i386 loader fails the exec of a program that exists this way.
            raise RuntimeError(
                f"cannot execute {program_path}: this host cannot run the i386 binaries; "
                "install the 32-bit runtime (e.g. libc6:i386) or set ROTS_IT_LAUNCHER=docker"
            ) from failure
        if failure.errno == errno.ENOEXEC:
            # A host that cannot load i386 Linux programs at all, such as a Mac.
            raise RuntimeError(f"cannot execute {program_path}: this host cannot run the i386 binaries; set ROTS_IT_LAUNCHER=docker") from failure
        raise


def _output_streams(log_file: BinaryIO, schedule: Optional[str]) -> dict[str, Any]:
    """Popen's stdout and stderr: a stdout pipe and stderr in log_file with a schedule, both in log_file without."""
    if schedule is not None:
        return {"stdout": subprocess.PIPE, "stderr": log_file}
    return {"stdout": log_file, "stderr": subprocess.STDOUT}


def close_standard_input(process: subprocess.Popen) -> None:
    """Closes the process's stdin pipe, if it has one; never raises."""
    if process.stdin is None:
        return
    try:
        process.stdin.close()
    except OSError:
        # Closing flushes buffered input; a process that has gone leaves a broken pipe.
        pass


def close_control_channel(process: subprocess.Popen, log_path: Path, timeout_seconds: float = INPUT_CLOSED_WAIT_SECONDS) -> bool:
    """Closes the harness server's stdin, then waits up to timeout_seconds for log_path to hold INPUT_CLOSED_LOG_LINE.

    True once the line is logged. False when the timeout passes or the process exits first, and
    False without waiting when there is no open stdin pipe to close. Never raises; a Ctrl-C during
    the wait propagates, so the caller must still stop the server.
    """
    if process.stdin is None or process.stdin.closed:
        # An already closed pipe means an earlier stop waited (or was interrupted waiting); not
        # waiting again bounds an interrupted stop to a single wait.
        return False
    # Measured before the close, so a line logged at once after it is still read.
    offset = _file_size(log_path)
    close_standard_input(process)
    line = INPUT_CLOSED_LOG_LINE.encode("ascii")
    deadline = time.monotonic() + timeout_seconds
    received = b""
    while True:
        chunk = _read_from(log_path, offset)
        offset += len(chunk)
        # Keeping one byte less than the line still finds a line split across two reads.
        received = received[-(len(line) - 1):] + chunk
        if line in received:
            return True
        if process.poll() is not None or time.monotonic() >= deadline:
            return False
        time.sleep(INPUT_CLOSED_POLL_SECONDS)


def _file_size(path: Path) -> int:
    """The file's size in bytes, or 0 when it cannot be read."""
    try:
        return path.stat().st_size
    except OSError:
        return 0


def _read_from(path: Path, offset: int) -> bytes:
    """The file's bytes from offset to its current end, or no bytes when it cannot be read."""
    try:
        with path.open("rb") as file:
            file.seek(offset)
            return file.read()
    except OSError:
        return b""


def terminate_process(process: subprocess.Popen) -> None:
    """Terminates the process, escalating to a kill, and closes its stdin pipe.

    Never raises; warns on stderr if the process survives the kill.
    """
    try:
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
                # A process the kernel will not reap (stuck in uninterruptible I/O) must not turn
                # fixture teardown into a traceback; the missing exit status reports it instead.
                print(f"server process {process.pid} survived SIGKILL for 10s", file=sys.stderr)
    finally:
        close_standard_input(process)


class ServerLauncher:
    """Runs the repo's programs in one execution environment; each subclass sets _repo_root."""

    # The checkout whose bin/ holds the programs; set by each subclass's constructor.
    _repo_root: Path

    def start(self, run_dir: Path, lib_dir: Path, port: int, seed: int, schedule: Optional[str] = None) -> ServerHandle:
        """Starts the server on lib_dir, logging to run_dir/game.log, and returns once it sends its login prompt.

        With a schedule it runs the harness binary with --schedule, with stdin and stdout as pipes
        for TimeControl and stderr in game.log. Raises RuntimeError, after stopping whatever it
        started, when the server exits or the prompt does not arrive within the launcher's
        startup timeout.
        """
        raise NotImplementedError

    def stop(self, handle: ServerHandle) -> None:
        """Stops the server behind handle and closes its stdin pipe.

        Does not signal a server that has already exited. A harness server first gets
        close_control_channel, so its stop can come up to INPUT_CLOSED_WAIT_SECONDS later.
        """
        raise NotImplementedError

    def run_to_completion(self, program_relative: str, arguments: list[str], working_dir: Path, timeout: float) -> ToolResult:
        """Runs the repo's program in working_dir and returns its status and combined output.

        Raises subprocess.TimeoutExpired, after stopping the program, when it outlives timeout.
        """
        raise NotImplementedError

    def build_hint(self) -> str:
        """The command that builds the programs where this launcher runs them."""
        return BUILD_COMMAND

    def require_program(self, program_relative: str) -> None:
        """Raises RuntimeError naming build_hint() when the repo's program is missing, is not a file, or is stale.

        A program is stale when it is older than the newest source it is built from (newest_source).
        Setting ROTS_IT_ALLOW_STALE=1 skips that check.
        """
        program_path = self._repo_root / program_relative
        if not program_path.is_file():
            raise RuntimeError(f"{program_relative} is missing; build it first: {self.build_hint()}")
        if os.environ.get(ALLOW_STALE_VARIABLE) == "1":
            return
        newest = newest_source(self._repo_root / SOURCE_DIRECTORY_RELATIVE, program_relative)
        if newest is None:
            return
        source_path, source_modified = newest
        if program_path.stat().st_mtime >= source_modified:
            return
        source_relative = source_path.relative_to(self._repo_root).as_posix()
        modified_text = datetime.fromtimestamp(source_modified).isoformat(sep=" ", timespec="seconds")
        raise RuntimeError(
            f"{program_relative} is older than {source_relative} (modified {modified_text}); rebuild it: {self.build_hint()}, "
            f"or set {ALLOW_STALE_VARIABLE}=1 to run it as it is"
        )


class LocalProcessLauncher(ServerLauncher):
    """Runs the programs directly on a host that can execute the i386 binaries (Linux, CI)."""

    def __init__(self, repo_root: Path, binary_relative: str = "bin/ageland", startup_timeout: float = 60.0) -> None:
        """Prepares a launcher for the server at repo_root/binary_relative; starts nothing.

        Raises ValueError when binary_relative is absolute, leaves the repo root or names the root itself.
        """
        check_repo_relative(binary_relative)
        # The checkout whose bin/ holds the programs.
        self._repo_root = repo_root
        # The server binary, relative to the repo root.
        self._binary_relative = binary_relative
        # Seconds start waits for the server's login prompt.
        self._startup_timeout = startup_timeout

    def command(self, lib_dir: Path, port: int, seed: int, schedule: Optional[str] = None) -> list[str]:
        """The server's argument vector for lib_dir, port and random seed; the harness binary's with a schedule."""
        if schedule is not None:
            return [
                str(self._repo_root / HARNESS_BINARY_RELATIVE), "--schedule", schedule,
                "--random-seed", str(seed), "-d", str(lib_dir), str(port),
            ]
        return [str(self._repo_root / self._binary_relative), "--random-seed", str(seed), "-d", str(lib_dir), str(port)]

    def environment(self) -> dict[str, str]:
        """PATH, HOME and whichever sanitizer variables the host sets; nothing else."""
        server_environment = {"PATH": os.environ.get("PATH", ""), "HOME": os.environ.get("HOME", "")}
        for name in SANITIZER_ENVIRONMENT_VARIABLES:
            if name in os.environ:
                server_environment[name] = os.environ[name]
        return server_environment

    def start(self, run_dir: Path, lib_dir: Path, port: int, seed: int, schedule: Optional[str] = None) -> ServerHandle:
        """See ServerLauncher.start; also fails with the build command when the binary is missing.

        Raises PortInUse when the server exits because another process holds port, and
        RuntimeError naming the i386 runtime when this host cannot execute the binary.
        """
        program_relative = HARNESS_BINARY_RELATIVE if schedule is not None else self._binary_relative
        self.require_program(program_relative)
        log_path = run_dir / "game.log"
        with log_path.open("wb") as log_file, _explaining_unexecutable(self._repo_root / program_relative):
            process = subprocess.Popen(
                self.command(lib_dir, port, seed, schedule),
                cwd=run_dir, env=self.environment(), stdin=subprocess.PIPE, **_output_streams(log_file, schedule),
            )
        try:
            handle = ServerHandle(LOOPBACK, port, log_path, process, has_control_channel=schedule is not None)
            wait_for_login_prompt(LOOPBACK, port, self._startup_timeout, process, log_path)
            # Another process on the same port, such as another run's server, can send the prompt
            # before this server has failed to bind it and exited.
            confirm_own_server(process, log_path)
            return handle
        except RuntimeError as failure:
            # The port was free when allocate_free_port probed it, but every local server shares
            # the host's ports, so another process can take it before this server binds it.
            port_taken = process.poll() is not None and PORT_IN_USE_LOG_TEXT in read_log_tail(log_path)
            terminate_process(process)
            if port_taken:
                raise PortInUse(f"port {port} was taken before the server could listen on it: {failure}") from failure
            raise
        except BaseException:
            # BaseException so a Ctrl-C during the wait also stops the server: the fixture never
            # received the handle, so nothing else would. No close_control_channel first: a server
            # that failed to start may never log its input closing.
            terminate_process(process)
            raise

    def stop(self, handle: ServerHandle) -> None:
        """See ServerLauncher.stop."""
        try:
            if handle.has_control_channel:
                close_control_channel(handle.process, handle.log_path)
        finally:
            terminate_process(handle.process)

    def run_to_completion(self, program_relative: str, arguments: list[str], working_dir: Path, timeout: float) -> ToolResult:
        """See ServerLauncher.run_to_completion; also fails with the build command when the program is missing.

        Raises RuntimeError naming the i386 runtime when this host cannot execute the program.
        """
        self.require_program(program_relative)
        program_path = self._repo_root / program_relative
        with _explaining_unexecutable(program_path):
            completed = subprocess.run(
                [str(program_path), *arguments],
                cwd=working_dir, env=self.environment(), stdin=subprocess.DEVNULL,
                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=timeout,
            )
        return ToolResult(completed.returncode, completed.stdout.decode("latin-1", errors="replace"))


class DockerComposeLauncher(ServerLauncher):
    """Runs the programs through `docker compose run` on hosts that cannot execute i386 binaries (macOS).

    Every container gets a unique name and a random loopback port, so the compose service's fixed
    container name and host port 1024 are never used. Everything a container reads or writes
    must live under the repo root, which the service mounts at /rots.
    """

    def __init__(self, repo_root: Path, binary_relative: str = "bin/ageland", service: str = "rots", startup_timeout: float = 300.0) -> None:
        """Prepares a launcher for the compose service in repo_root; starts nothing.

        Raises ValueError when binary_relative is absolute, leaves the repo root or names the root itself.
        """
        check_repo_relative(binary_relative)
        # The checkout holding docker-compose.yml, mounted at /rots inside the container.
        self._repo_root = repo_root
        # The server binary, relative to the repo root.
        self._binary_relative = binary_relative
        # The compose service every container runs.
        self._service = service
        # Seconds start waits in all for the port to be published and then for the server's login
        # prompt; generous because the i386 server runs under emulation.
        self._startup_timeout = startup_timeout

    def container_path(self, host_path: Path) -> str:
        """The path the container sees for host_path; the repo root is mounted at /rots."""
        try:
            relative = host_path.resolve().relative_to(self._repo_root.resolve())
        except ValueError as outside:
            raise ValueError(f"{host_path} is outside the repository at {self._repo_root}; the container cannot see it") from outside
        return "/rots/" + relative.as_posix()

    def build_hint(self) -> str:
        """The build command run inside the compose service, where the i386 toolchain is."""
        return f"docker compose run --rm -T {self._service} bash -lc {shlex.quote(BUILD_COMMAND)}"

    def _run_prefix(self, container_name: str) -> list[str]:
        return [
            "docker", "compose", "run", "--rm", "-T",
            "--name", container_name,
            "-e", f"TMPDIR=/rots/{INTEGRATION_TMP_RELATIVE}",
        ]

    def server_command(
        self, run_dir: Path, lib_dir: Path, port: int, container_name: str, seed: int, schedule: Optional[str] = None,
    ) -> list[str]:
        """The compose command that runs the server in run_dir and publishes port on a random loopback port.

        With a schedule it runs the harness binary with --schedule.
        """
        # ulimit -c 0 keeps a crashing i386 process under QEMU from hanging while it writes a core.
        working_dir = shlex.quote(self.container_path(run_dir))
        lib = shlex.quote(self.container_path(lib_dir))
        if schedule is not None:
            program = f"{shlex.quote(f'/rots/{HARNESS_BINARY_RELATIVE}')} --schedule {shlex.quote(schedule)}"
        else:
            program = shlex.quote(f"/rots/{self._binary_relative}")
        script = f"ulimit -c 0; cd {working_dir} && exec {program} --random-seed {seed} -d {lib} {port}"
        return [*self._run_prefix(container_name), "-p", f"{LOOPBACK}::{port}", self._service, "bash", "-lc", script]

    def tool_command(self, program_relative: str, arguments: list[str], working_dir: Path, container_name: str) -> list[str]:
        """The compose command that runs the repo's program in working_dir, publishing no port."""
        container_dir = shlex.quote(self.container_path(working_dir))
        program = shlex.quote(f"/rots/{program_relative}")
        script = f"ulimit -c 0; cd {container_dir} && exec {program} {shlex.join(arguments)}"
        return [*self._run_prefix(container_name), self._service, "bash", "-lc", script]

    def _docker_environment(self) -> dict[str, str]:
        # The compose file runs the container as ROTS_UID:ROTS_GID so files it writes under the
        # mount belong to the host user.
        environment = dict(os.environ)
        environment["ROTS_UID"] = str(os.getuid())
        environment["ROTS_GID"] = str(os.getgid())
        return environment

    def _stop_container(self, container_name: str) -> bool:
        """Runs docker stop on the container; True when docker reports it stopped. Never raises."""
        try:
            result = subprocess.run(["docker", "stop", "-t", "5", container_name], capture_output=True, timeout=15)
        except (subprocess.TimeoutExpired, OSError):
            return False
        return result.returncode == 0

    def _remove_container(self, container_name: str) -> None:
        """Force-removes the container, ignoring every failure.

        Compose applies --rm from its client, so a container whose client was killed, or that
        docker stop could not stop, is left behind unless removed here.
        """
        try:
            subprocess.run(["docker", "rm", "-f", container_name], capture_output=True, timeout=15)
        except (subprocess.TimeoutExpired, OSError):
            pass

    def start(self, run_dir: Path, lib_dir: Path, port: int, seed: int, schedule: Optional[str] = None) -> ServerHandle:
        """See ServerLauncher.start; also fails with the build command when the binary is missing.

        In both modes compose's own stderr lands in game.log; with a schedule, stdout is a pipe
        carrying only the server's.
        """
        (self._repo_root / INTEGRATION_TMP_RELATIVE).mkdir(parents=True, exist_ok=True)
        self.require_program(HARNESS_BINARY_RELATIVE if schedule is not None else self._binary_relative)
        container_name = f"rots-it-{uuid.uuid4().hex[:12]}"
        process: Optional[subprocess.Popen] = None
        # One deadline covers both waits, so a slow port publication shortens the prompt wait.
        deadline = time.monotonic() + self._startup_timeout
        try:
            log_path = run_dir / "game.log"
            with log_path.open("wb") as log_file:
                process = subprocess.Popen(
                    self.server_command(run_dir, lib_dir, port, container_name, seed, schedule),
                    cwd=self._repo_root, env=self._docker_environment(), stdin=subprocess.PIPE, **_output_streams(log_file, schedule),
                )
            host_port = self._wait_for_published_port(container_name, port, process, log_path, deadline)
            handle = ServerHandle(LOOPBACK, host_port, log_path, process, container_name, has_control_channel=schedule is not None)
            time_allowed = f"within the {self._startup_timeout}s startup timeout, part of which the wait for the published port used"
            wait_for_login_prompt(LOOPBACK, host_port, max(0.0, deadline - time.monotonic()), process, log_path, time_allowed)
            return handle
        except BaseException:
            # BaseException so a Ctrl-C during the waits also stops the container: the fixture
            # never received the handle, so nothing else would. No close_control_channel first: a
            # server that failed to start may never log its input closing.
            self._stop_container(container_name)
            self._remove_container(container_name)
            if process is not None:
                terminate_process(process)
            raise

    def _wait_for_published_port(self, container_name: str, container_port: int, process: subprocess.Popen, log_path: Path, deadline: float) -> int:
        """The host port Docker published for container_port; raises RuntimeError if compose exits or the monotonic deadline passes."""
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
        """See ServerLauncher.stop; stops the container, then waits for (or kills) the compose run.

        Removes the container itself when docker stop fails or the compose run has to be killed.
        """
        try:
            if handle.has_control_channel:
                close_control_channel(handle.process, handle.log_path)
        finally:
            self._stop_container_and_client(handle)

    def _stop_container_and_client(self, handle: ServerHandle) -> None:
        """Stops the container, waits for (or kills) the compose run, and closes its stdin pipe."""
        if handle.container_name and not self._stop_container(handle.container_name):
            self._remove_container(handle.container_name)
        if handle.is_alive():
            try:
                handle.process.wait(timeout=30)
            except subprocess.TimeoutExpired:
                handle.process.kill()
                try:
                    handle.process.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    print(f"docker compose run process {handle.process.pid} survived SIGKILL for 10s", file=sys.stderr)
                if handle.container_name:
                    self._remove_container(handle.container_name)
        close_standard_input(handle.process)

    def run_to_completion(self, program_relative: str, arguments: list[str], working_dir: Path, timeout: float) -> ToolResult:
        """See ServerLauncher.run_to_completion; also fails with the build command when the program is missing."""
        self.require_program(program_relative)
        (self._repo_root / INTEGRATION_TMP_RELATIVE).mkdir(parents=True, exist_ok=True)
        container_name = f"rots-it-tool-{uuid.uuid4().hex[:12]}"
        try:
            completed = subprocess.run(
                self.tool_command(program_relative, arguments, working_dir, container_name),
                cwd=self._repo_root, env=self._docker_environment(), stdin=subprocess.DEVNULL,
                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=timeout,
            )
        except BaseException:
            # A timeout or a Ctrl-C kills only the compose client; the container keeps running
            # until stopped here, and stays until removed here because its --rm died with the client.
            self._stop_container(container_name)
            self._remove_container(container_name)
            raise
        return ToolResult(completed.returncode, completed.stdout.decode("latin-1", errors="replace"))


def start_on_a_free_port(
    server_launcher: ServerLauncher, run_dir: Path, lib_dir: Path, seed: int, schedule: Optional[str] = None,
    attempts: int = PORT_ATTEMPTS, allocate_port: Callable[[], int] = allocate_free_port,
) -> ServerHandle:
    """Starts the server through server_launcher, as ServerLauncher.start does, on a port from allocate_port.

    When start raises PortInUse, starts again on a newly allocated port, up to attempts starts in
    all, and raises the last PortInUse when every one found its port taken. Any other failure
    propagates from the first start without a retry.
    """
    attempt = 1
    while True:
        try:
            return server_launcher.start(run_dir, lib_dir, allocate_port(), seed, schedule=schedule)
        except PortInUse:
            if attempt >= attempts:
                raise
            attempt += 1
