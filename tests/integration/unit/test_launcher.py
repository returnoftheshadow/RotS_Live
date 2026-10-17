from __future__ import annotations

import io
import shlex
import subprocess
import sys
import time
from pathlib import Path
from typing import Iterator, List, Optional

import pytest

from rots_harness import launcher


def test_local_launcher_builds_the_server_command(tmp_path: Path) -> None:
    local = launcher.LocalProcessLauncher(repo_root=tmp_path, binary_relative="bin/ageland")
    lib_dir = tmp_path / "run" / "lib"
    command = local.command(lib_dir=lib_dir, port=4321, seed=7)
    assert command == [str(tmp_path / "bin" / "ageland"), "--random-seed", "7", "-d", str(lib_dir), "4321"]


def test_docker_launcher_maps_run_dir_into_the_container_and_publishes_a_random_port(tmp_path: Path) -> None:
    repo_root = tmp_path
    run_dir = repo_root / "build" / "integration" / "abc123"
    docker = launcher.DockerComposeLauncher(repo_root=repo_root, binary_relative="bin/ageland")
    command = docker.server_command(run_dir=run_dir, lib_dir=run_dir / "lib", port=4321, container_name="rots-it-abc123", seed=7)
    assert command[:5] == ["docker", "compose", "run", "--rm", "-T"]
    assert "--name" in command and command[command.index("--name") + 1] == "rots-it-abc123"
    assert "-p" in command and command[command.index("-p") + 1] == "127.0.0.1::4321"
    assert "TMPDIR=/rots/build/integration/tmp" in command
    assert command[command.index("TMPDIR=/rots/build/integration/tmp") - 1] == "-e"
    assert "--service-ports" not in command
    shell_script = command[-1]
    assert "ulimit -c 0;" in shell_script
    assert "cd /rots/build/integration/abc123" in shell_script
    assert "exec /rots/bin/ageland --random-seed 7 -d /rots/build/integration/abc123/lib 4321" in shell_script


def test_container_path_refuses_a_path_outside_the_repo(tmp_path: Path) -> None:
    docker = launcher.DockerComposeLauncher(repo_root=tmp_path / "repo")
    with pytest.raises(ValueError, match="outside the repository"):
        docker.container_path(tmp_path / "elsewhere" / "lib")


def test_docker_tool_command_runs_the_program_in_the_mapped_directory(tmp_path: Path) -> None:
    docker = launcher.DockerComposeLauncher(repo_root=tmp_path)
    command = docker.tool_command("bin/rotstool", ["fixtures", "write", "--lib", "lib", "spec.json"], tmp_path / "build" / "integration" / "libs" / "k1", "rots-it-tool1")
    assert command[:5] == ["docker", "compose", "run", "--rm", "-T"]
    assert "-p" not in command
    assert command[-1] == "ulimit -c 0; cd /rots/build/integration/libs/k1 && exec /rots/bin/rotstool fixtures write --lib lib spec.json"


def test_local_run_to_completion_returns_status_and_combined_output(tmp_path: Path) -> None:
    program = tmp_path / "bin" / "tool"
    program.parent.mkdir()
    program.write_text("#!/bin/sh\necho out; echo err >&2; exit 3\n", encoding="utf-8")
    program.chmod(0o700)
    result = launcher.LocalProcessLauncher(repo_root=tmp_path).run_to_completion("bin/tool", [], tmp_path, timeout=10)
    assert result.exit_status == 3
    assert "out" in result.output and "err" in result.output


def test_require_program_names_the_build_command_when_missing(tmp_path: Path) -> None:
    with pytest.raises(RuntimeError) as missing:
        launcher.LocalProcessLauncher(repo_root=tmp_path).require_program("bin/rotstool")
    assert str(missing.value) == "bin/rotstool is missing; build it first: make configure && cmake --build build --target ageland ageland_harness rotstool"


def test_docker_require_program_names_the_build_command_inside_the_container(tmp_path: Path) -> None:
    with pytest.raises(RuntimeError) as missing:
        launcher.DockerComposeLauncher(repo_root=tmp_path).require_program("bin/ageland")
    assert str(missing.value) == (
        "bin/ageland is missing; build it first: "
        "docker compose run --rm -T rots bash -lc 'make configure && cmake --build build --target ageland ageland_harness rotstool'"
    )


def test_local_start_gives_the_server_a_standard_input_pipe(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    recorded_kwargs: list[dict] = []

    class StopAfterRecording(Exception):
        pass

    def recording_popen(*args: object, **kwargs: object):
        recorded_kwargs.append(kwargs)
        raise StopAfterRecording()

    binary = tmp_path / "bin" / "ageland"
    binary.parent.mkdir()
    binary.write_text("", encoding="utf-8")
    monkeypatch.setattr(launcher.subprocess, "Popen", recording_popen)
    run_dir = tmp_path / "run"
    run_dir.mkdir()
    with pytest.raises(StopAfterRecording):
        launcher.LocalProcessLauncher(repo_root=tmp_path).start(run_dir, run_dir / "lib", port=4321, seed=1)
    assert recorded_kwargs[0]["stdin"] == launcher.subprocess.PIPE


def test_allocate_free_port_returns_a_high_port() -> None:
    port = launcher.allocate_free_port()
    assert 1024 < port < 65536


def test_local_launcher_kills_the_process_when_the_port_never_opens(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    stalling_script = tmp_path / "bin" / "ageland"
    stalling_script.parent.mkdir()
    stalling_script.write_text("#!/bin/sh\nexec sleep 60\n", encoding="utf-8")
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

    local = launcher.LocalProcessLauncher(repo_root=tmp_path, binary_relative="bin/ageland", startup_timeout=1.0)
    with pytest.raises(RuntimeError):
        local.start(run_dir, lib_dir, port=launcher.allocate_free_port(), seed=1)

    assert len(recorded_processes) == 1
    assert recorded_processes[0].poll() is not None


def test_docker_launcher_stops_the_container_when_start_fails(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    class FakeComposeProcess:
        def __init__(self) -> None:
            # Never set: the fake compose process is always still running.
            self.returncode: int | None = None
            # Whether start's cleanup asked the process to terminate.
            self.terminate_called = False
            # Whether start's cleanup escalated to a kill.
            self.kill_called = False
            # Stands in for the pipe start opens; cleanup must close it.
            self.stdin = io.BytesIO()

        def poll(self) -> int | None:
            return None

        def terminate(self) -> None:
            self.terminate_called = True

        def kill(self) -> None:
            self.kill_called = True

        def wait(self, timeout: float | None = None) -> int:
            return 0

    # A placeholder binary keeps start past require_program, so the failure is the unpublished port.
    binary = tmp_path / "bin" / "ageland"
    binary.parent.mkdir()
    binary.write_text("", encoding="utf-8")
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

    cleanup_calls = [argv for argv in recorded_argv if argv[:2] != ["docker", "port"]]
    assert len(cleanup_calls) == 2
    container_name = cleanup_calls[0][-1]
    assert container_name.startswith("rots-it-")
    assert cleanup_calls == [["docker", "stop", "-t", "5", container_name], ["docker", "rm", "-f", container_name]]
    assert fake_process.terminate_called or fake_process.kill_called
    assert fake_process.stdin.closed


def test_local_launcher_kills_the_process_when_interrupted_while_waiting(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    stalling_script = tmp_path / "bin" / "ageland"
    stalling_script.parent.mkdir()
    stalling_script.write_text("#!/bin/sh\nexec sleep 60\n", encoding="utf-8")
    stalling_script.chmod(0o700)
    run_dir = tmp_path / "run"
    run_dir.mkdir()

    recorded_processes: list[subprocess.Popen] = []
    real_popen = launcher.subprocess.Popen

    def recording_popen(*args: object, **kwargs: object) -> subprocess.Popen:
        spawned_process = real_popen(*args, **kwargs)
        recorded_processes.append(spawned_process)
        return spawned_process

    def interrupted_wait(*args: object, **kwargs: object) -> None:
        raise KeyboardInterrupt()

    monkeypatch.setattr(launcher.subprocess, "Popen", recording_popen)
    monkeypatch.setattr(launcher, "wait_for_login_prompt", interrupted_wait)

    local = launcher.LocalProcessLauncher(repo_root=tmp_path, binary_relative="bin/ageland")
    with pytest.raises(KeyboardInterrupt):
        local.start(run_dir, run_dir / "lib", port=launcher.allocate_free_port(), seed=1)

    assert len(recorded_processes) == 1
    assert recorded_processes[0].poll() is not None


def test_docker_launcher_stops_the_container_when_interrupted_while_waiting(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    binary = tmp_path / "bin" / "ageland"
    binary.parent.mkdir()
    binary.write_text("", encoding="utf-8")
    run_dir = tmp_path / "run"
    run_dir.mkdir()

    compose_process = subprocess.Popen(["sleep", "60"], stdin=subprocess.PIPE)
    recorded_argv: list[list[str]] = []

    def interrupted_port_query(argv: list[str], **kwargs: object) -> subprocess.CompletedProcess[str]:
        recorded_argv.append(argv)
        if argv[:2] == ["docker", "port"]:
            raise KeyboardInterrupt()
        return subprocess.CompletedProcess(argv, 0, "", "")

    monkeypatch.setattr(launcher.subprocess, "Popen", lambda *args, **kwargs: compose_process)
    monkeypatch.setattr(launcher.subprocess, "run", interrupted_port_query)

    docker = launcher.DockerComposeLauncher(repo_root=tmp_path, binary_relative="bin/ageland")
    try:
        with pytest.raises(KeyboardInterrupt):
            docker.start(run_dir, run_dir / "lib", port=4321, seed=1)

        stop_calls = [argv for argv in recorded_argv if argv[:4] == ["docker", "stop", "-t", "5"]]
        assert len(stop_calls) == 1
        assert stop_calls[0][4].startswith("rots-it-")
        assert compose_process.poll() is not None
    finally:
        if compose_process.poll() is None:
            compose_process.kill()
            compose_process.wait()


def test_docker_run_to_completion_stops_the_container_when_interrupted(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    program = tmp_path / "bin" / "rotstool"
    program.parent.mkdir()
    program.write_text("", encoding="utf-8")
    recorded_argv: list[list[str]] = []

    def interrupted_compose_run(argv: list[str], **kwargs: object) -> subprocess.CompletedProcess[str]:
        recorded_argv.append(argv)
        if argv[:3] == ["docker", "compose", "run"]:
            raise KeyboardInterrupt()
        return subprocess.CompletedProcess(argv, 0, "", "")

    monkeypatch.setattr(launcher.subprocess, "run", interrupted_compose_run)

    docker = launcher.DockerComposeLauncher(repo_root=tmp_path)
    with pytest.raises(KeyboardInterrupt):
        docker.run_to_completion("bin/rotstool", [], tmp_path, timeout=10)

    cleanup_calls = recorded_argv[1:]
    assert len(cleanup_calls) == 2
    container_name = cleanup_calls[0][-1]
    assert container_name.startswith("rots-it-tool-")
    assert cleanup_calls == [["docker", "stop", "-t", "5", container_name], ["docker", "rm", "-f", container_name]]


def test_local_launcher_environment_is_clean_apart_from_sanitizer_tuning(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    monkeypatch.setenv("ASAN_OPTIONS", "detect_leaks=0:handle_segv=2")
    monkeypatch.setenv("ASAN_SYMBOLIZER_PATH", "/usr/bin/llvm-symbolizer")
    monkeypatch.delenv("LSAN_OPTIONS", raising=False)
    monkeypatch.delenv("UBSAN_OPTIONS", raising=False)
    # A host-only harness variable must not reach the server.
    monkeypatch.setenv("ROTS_IT_SEED", "5")

    environment = launcher.LocalProcessLauncher(repo_root=tmp_path).environment()

    assert environment["ASAN_OPTIONS"] == "detect_leaks=0:handle_segv=2"
    assert environment["ASAN_SYMBOLIZER_PATH"] == "/usr/bin/llvm-symbolizer"
    assert "LSAN_OPTIONS" not in environment
    assert "UBSAN_OPTIONS" not in environment
    assert "ROTS_IT_SEED" not in environment
    assert set(environment) <= {"PATH", "HOME", *launcher.SANITIZER_ENVIRONMENT_VARIABLES}


def test_local_launcher_environment_has_no_sanitizer_keys_when_the_host_sets_none(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    for name in launcher.SANITIZER_ENVIRONMENT_VARIABLES:
        monkeypatch.delenv(name, raising=False)

    environment = launcher.LocalProcessLauncher(repo_root=tmp_path).environment()

    assert set(environment) == {"PATH", "HOME"}


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
            # Reported in the warning that names the surviving process.
            self.pid = 4242
            # Never set: the process never exits.
            self.returncode: int | None = None
            # How many times terminate_process asked the process to terminate.
            self.terminate_calls = 0
            # How many times terminate_process escalated to a kill.
            self.kill_calls = 0
            # Stands in for the server's stdin pipe; closed even though the process survives.
            self.stdin = io.BytesIO()

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
    assert stuck_process.stdin.closed


class ExitedProcess:
    """A server process that has already exited, holding the stdin pipe its launcher opened."""

    def __init__(self) -> None:
        # Stands in for the pipe the launcher opened; stop must close it.
        self.stdin = io.BytesIO()
        # The exit status poll reports.
        self.returncode = 0

    def poll(self) -> int:
        return self.returncode

    def wait(self, timeout: float | None = None) -> int:
        return self.returncode


def test_local_stop_closes_the_server_standard_input(tmp_path: Path) -> None:
    exited_process = ExitedProcess()
    handle = launcher.ServerHandle(launcher.LOOPBACK, 4321, tmp_path / "game.log", exited_process)  # type: ignore[arg-type]

    launcher.LocalProcessLauncher(repo_root=tmp_path).stop(handle)

    assert exited_process.stdin.closed


def test_docker_stop_closes_the_server_standard_input(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr(launcher.subprocess, "run", lambda argv, **kwargs: subprocess.CompletedProcess(argv, 0, "", ""))
    exited_process = ExitedProcess()
    handle = launcher.ServerHandle(launcher.LOOPBACK, 4321, tmp_path / "game.log", exited_process, "rots-it-stop")  # type: ignore[arg-type]

    launcher.DockerComposeLauncher(repo_root=tmp_path).stop(handle)

    assert exited_process.stdin.closed


# A stand-in for ageland's listener: argv is the port, then a directory for the files the test reads.
FAKE_SERVER_PREAMBLE = """
import socket
import sys
import time
from pathlib import Path

port = int(sys.argv[1])
evidence_dir = Path(sys.argv[2])
listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
listener.bind(("127.0.0.1", port))
listener.listen(5)
print("fake server listening", flush=True)
# Telnet negotiation (IAC WILL ECHO) comes before the greeting, as from the real server.
NEGOTIATION = bytes([255, 251, 1])
"""

# Accepts at once but sends the prompt, split across two writes, only after a delay.
DELAYED_PROMPT_SERVER = FAKE_SERVER_PREAMBLE + """
connection, _address = listener.accept()
time.sleep(0.5)
(evidence_dir / "prompt-sent").write_text("yes")
connection.sendall(NEGOTIATION + b"Welcome\\r\\n\\r\\nAccount em")
time.sleep(0.2)
connection.sendall(b"ail: ")
time.sleep(30)
"""

# Closes the first connection unanswered, as a port proxy does before the server listens.
CLOSES_FIRST_CONNECTION_SERVER = FAKE_SERVER_PREAMBLE + """
first_connection, _address = listener.accept()
first_connection.close()
second_connection, _address = listener.accept()
(evidence_dir / "connections").write_text("2")
second_connection.sendall(NEGOTIATION + b"Account email: ")
time.sleep(30)
"""

# Accepts connections and never sends anything.
SILENT_SERVER = FAKE_SERVER_PREAMBLE + """
held_connections = []
while True:
    connection, _address = listener.accept()
    held_connections.append(connection)
"""

# Fails during boot without ever listening.
EXITS_BEFORE_LISTENING_SERVER = """
import sys
print("SYSERR: boot failed: zone 999 is missing", flush=True)
sys.exit(3)
"""


class FakeServers:
    """Starts fake server scripts with this test's Python and kills any still running at teardown."""

    def __init__(self, work_dir: Path) -> None:
        # Holds each script, its log and the evidence files it writes.
        self._work_dir = work_dir
        # Every process started, killed at teardown if still running.
        self._processes: List[subprocess.Popen] = []

    def start(self, source: str, port: int) -> subprocess.Popen:
        """Runs source as a script with port and the evidence directory as arguments, logging to fake.log."""
        script = self._work_dir / f"fake_server_{len(self._processes)}.py"
        script.write_text(source, encoding="utf-8")
        with self.log_path.open("ab") as log_file:
            process = subprocess.Popen(
                [sys.executable, str(script), str(port), str(self._work_dir)], stdin=subprocess.DEVNULL, stdout=log_file, stderr=subprocess.STDOUT,
            )
        self._processes.append(process)
        return process

    @property
    def log_path(self) -> Path:
        """The log every fake server writes to."""
        return self._work_dir / "fake.log"

    def kill_all(self) -> None:
        """Kills every started process that is still running."""
        for process in self._processes:
            if process.poll() is None:
                process.kill()
            process.wait()


@pytest.fixture
def fake_servers(tmp_path: Path) -> Iterator[FakeServers]:
    """Fake servers for one test, all killed when it ends, whatever its outcome."""
    servers = FakeServers(tmp_path)
    try:
        yield servers
    finally:
        servers.kill_all()


def test_ready_waits_for_the_login_prompt_not_the_listening_port(tmp_path: Path, fake_servers: FakeServers) -> None:
    port = launcher.allocate_free_port()
    process = fake_servers.start(DELAYED_PROMPT_SERVER, port)

    launcher.wait_for_login_prompt(launcher.LOOPBACK, port, 10.0, process, fake_servers.log_path)

    assert (tmp_path / "prompt-sent").exists()


def test_ready_retries_a_connection_closed_before_the_prompt(tmp_path: Path, fake_servers: FakeServers) -> None:
    port = launcher.allocate_free_port()
    process = fake_servers.start(CLOSES_FIRST_CONNECTION_SERVER, port)

    launcher.wait_for_login_prompt(launcher.LOOPBACK, port, 10.0, process, fake_servers.log_path)

    assert (tmp_path / "connections").read_text() == "2"


def test_ready_reports_a_server_that_exits_before_listening_with_its_log_tail(fake_servers: FakeServers) -> None:
    port = launcher.allocate_free_port()
    process = fake_servers.start(EXITS_BEFORE_LISTENING_SERVER, port)

    with pytest.raises(RuntimeError) as raised:
        launcher.wait_for_login_prompt(launcher.LOOPBACK, port, 10.0, process, fake_servers.log_path)

    message = str(raised.value)
    assert message.startswith("server exited with status 3 before it was ready")
    assert "boot failed: zone 999 is missing" in message


def test_ready_gives_up_on_a_server_that_never_sends_its_prompt(fake_servers: FakeServers) -> None:
    port = launcher.allocate_free_port()
    process = fake_servers.start(SILENT_SERVER, port)

    started = time.monotonic()
    with pytest.raises(RuntimeError) as raised:
        launcher.wait_for_login_prompt(launcher.LOOPBACK, port, 1.5, process, fake_servers.log_path)

    assert time.monotonic() - started < 5.0
    message = str(raised.value)
    assert message.startswith(f"the server on {launcher.LOOPBACK}:{port} did not send its login prompt within 1.5s")
    assert "fake server listening" in message


def test_the_session_waits_for_the_same_login_prompt_as_the_launcher() -> None:
    from rots_harness import session

    assert launcher.LOGIN_EMAIL_PROMPT == "Account email:"
    assert session.LOGIN_EMAIL_PROMPT is launcher.LOGIN_EMAIL_PROMPT


class FakeComposeClient:
    """A `docker compose run` client that records stop's calls in a shared event list."""

    def __init__(self, events: list, waits_out_the_first_wait: bool = False) -> None:
        # The ordered record of docker commands and process calls the test inspects.
        self._events = events
        # Whether the first wait times out, as a client that outlives docker stop does.
        self._waits_out_the_first_wait = waits_out_the_first_wait
        # How many times wait was called.
        self._wait_calls = 0
        # None while the client runs; set by kill or a successful wait.
        self.returncode: Optional[int] = None
        # Stands in for the pipe start opens; stop must close it.
        self.stdin = io.BytesIO()
        # Named in stop's warning if the client survives its kill.
        self.pid = 4242

    def poll(self) -> Optional[int]:
        return self.returncode

    def wait(self, timeout: Optional[float] = None) -> int:
        self._events.append(("wait", timeout))
        self._wait_calls += 1
        if self._waits_out_the_first_wait and self._wait_calls == 1:
            raise subprocess.TimeoutExpired(cmd="docker", timeout=timeout or 0)
        self.returncode = 0
        return 0

    def kill(self) -> None:
        self._events.append(("kill",))
        self.returncode = -9


def recording_docker(events: list, failing_command: Optional[str] = None, failure: Optional[BaseException] = None):
    """A subprocess.run stand-in that records each docker argv; failing_command fails with failure or status 1."""

    def fake_run(argv: List[str], **kwargs: object) -> subprocess.CompletedProcess:
        events.append(("run", argv))
        if argv[1] == failing_command:
            if failure is not None:
                raise failure
            return subprocess.CompletedProcess(argv, 1, b"", b"")
        return subprocess.CompletedProcess(argv, 0, b"", b"")

    return fake_run


def test_docker_stop_stops_the_container_then_waits_for_the_client(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    events: list = []
    monkeypatch.setattr(launcher.subprocess, "run", recording_docker(events))
    client = FakeComposeClient(events)
    handle = launcher.ServerHandle(launcher.LOOPBACK, 4321, tmp_path / "game.log", client, "rots-it-stop")  # type: ignore[arg-type]

    launcher.DockerComposeLauncher(repo_root=tmp_path).stop(handle)

    assert events == [("run", ["docker", "stop", "-t", "5", "rots-it-stop"]), ("wait", 30)]
    assert client.stdin.closed


def test_docker_stop_kills_a_client_that_outlives_the_wait_and_removes_the_container(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    events: list = []
    monkeypatch.setattr(launcher.subprocess, "run", recording_docker(events))
    client = FakeComposeClient(events, waits_out_the_first_wait=True)
    handle = launcher.ServerHandle(launcher.LOOPBACK, 4321, tmp_path / "game.log", client, "rots-it-stop")  # type: ignore[arg-type]

    launcher.DockerComposeLauncher(repo_root=tmp_path).stop(handle)

    assert events[:3] == [("run", ["docker", "stop", "-t", "5", "rots-it-stop"]), ("wait", 30), ("kill",)]
    post_kill_wait = events[3]
    assert post_kill_wait[0] == "wait" and post_kill_wait[1] is not None and post_kill_wait[1] > 0
    assert events[4:] == [("run", ["docker", "rm", "-f", "rots-it-stop"])]
    assert client.stdin.closed


@pytest.mark.parametrize(
    "failure",
    [None, subprocess.TimeoutExpired(cmd="docker", timeout=15), FileNotFoundError("docker")],
    ids=["non-zero-exit", "timeout", "docker-missing"],
)
def test_docker_stop_removes_the_container_when_docker_stop_fails(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, failure: Optional[BaseException]
) -> None:
    events: list = []
    monkeypatch.setattr(launcher.subprocess, "run", recording_docker(events, failing_command="stop", failure=failure))
    client = FakeComposeClient(events)
    handle = launcher.ServerHandle(launcher.LOOPBACK, 4321, tmp_path / "game.log", client, "rots-it-stop")  # type: ignore[arg-type]

    launcher.DockerComposeLauncher(repo_root=tmp_path).stop(handle)

    assert events[:2] == [("run", ["docker", "stop", "-t", "5", "rots-it-stop"]), ("run", ["docker", "rm", "-f", "rots-it-stop"])]
    assert client.stdin.closed


@pytest.mark.parametrize(
    "failure",
    [None, subprocess.TimeoutExpired(cmd="docker", timeout=15), FileNotFoundError("docker")],
    ids=["non-zero-exit", "timeout", "docker-missing"],
)
def test_a_failed_docker_rm_is_ignored(tmp_path: Path, monkeypatch: pytest.MonkeyPatch, failure: Optional[BaseException]) -> None:
    events: list = []
    monkeypatch.setattr(launcher.subprocess, "run", recording_docker(events, failing_command="rm", failure=failure))
    docker = launcher.DockerComposeLauncher(repo_root=tmp_path)

    docker._remove_container("rots-it-gone")

    assert events == [("run", ["docker", "rm", "-f", "rots-it-gone"])]


def test_docker_reads_the_published_port_from_docker_port(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    recorded_argv: List[List[str]] = []

    def fake_port_query(argv: List[str], **kwargs: object) -> subprocess.CompletedProcess:
        recorded_argv.append(argv)
        return subprocess.CompletedProcess(argv, 0, "127.0.0.1:49153\n", "")

    monkeypatch.setattr(launcher.subprocess, "run", fake_port_query)
    docker = launcher.DockerComposeLauncher(repo_root=tmp_path)

    host_port = docker._wait_for_published_port("rots-it-port", 4321, FakeComposeClient([]), tmp_path / "game.log", time.monotonic() + 5.0)  # type: ignore[arg-type]

    assert host_port == 49153
    assert recorded_argv == [["docker", "port", "rots-it-port", "4321/tcp"]]


def test_docker_runs_the_container_as_the_host_user(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr(launcher.os, "getuid", lambda: 1234)
    monkeypatch.setattr(launcher.os, "getgid", lambda: 5678)

    environment = launcher.DockerComposeLauncher(repo_root=tmp_path)._docker_environment()

    assert environment["ROTS_UID"] == "1234"
    assert environment["ROTS_GID"] == "5678"


@pytest.mark.parametrize("launcher_class", [launcher.LocalProcessLauncher, launcher.DockerComposeLauncher])
def test_run_to_completion_gives_the_program_no_standard_input(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, launcher_class: type
) -> None:
    program = tmp_path / "bin" / "rotstool"
    program.parent.mkdir()
    program.write_text("", encoding="utf-8")
    recorded_kwargs: List[dict] = []

    def recording_run(argv: List[str], **kwargs: object) -> subprocess.CompletedProcess:
        recorded_kwargs.append(kwargs)
        return subprocess.CompletedProcess(argv, 0, b"done", None)

    monkeypatch.setattr(launcher.subprocess, "run", recording_run)

    result = launcher_class(repo_root=tmp_path).run_to_completion("bin/rotstool", [], tmp_path, timeout=10)

    assert result == launcher.ToolResult(0, "done")
    assert recorded_kwargs[0]["stdin"] == subprocess.DEVNULL


def write_program_with_a_missing_interpreter(repo_root: Path, program_relative: str) -> Path:
    """An executable script whose interpreter does not exist, as an i386 binary looks without its loader."""
    program = repo_root / program_relative
    program.parent.mkdir(parents=True, exist_ok=True)
    program.write_text("#!/nonexistent/i386-loader\n", encoding="utf-8")
    program.chmod(0o700)
    return program


def test_local_run_to_completion_explains_a_program_this_host_cannot_execute(tmp_path: Path) -> None:
    program = write_program_with_a_missing_interpreter(tmp_path, "bin/rotstool")

    with pytest.raises(RuntimeError) as raised:
        launcher.LocalProcessLauncher(repo_root=tmp_path).run_to_completion("bin/rotstool", [], tmp_path, timeout=10)

    assert str(raised.value) == (
        f"cannot execute {program}: this host cannot run the i386 binaries; "
        "install the 32-bit runtime (e.g. libc6:i386) or set ROTS_IT_LAUNCHER=docker"
    )


def test_local_start_explains_a_server_this_host_cannot_execute(tmp_path: Path) -> None:
    program = write_program_with_a_missing_interpreter(tmp_path, "bin/ageland")
    run_dir = tmp_path / "run"
    run_dir.mkdir()

    with pytest.raises(RuntimeError, match=f"cannot execute {program}: this host cannot run the i386 binaries"):
        launcher.LocalProcessLauncher(repo_root=tmp_path).start(run_dir, run_dir / "lib", port=4321, seed=1)


def test_local_run_to_completion_in_a_missing_directory_is_not_blamed_on_the_host(tmp_path: Path) -> None:
    program = tmp_path / "bin" / "tool"
    program.parent.mkdir()
    program.write_text("#!/bin/sh\nexit 0\n", encoding="utf-8")
    program.chmod(0o700)

    with pytest.raises(FileNotFoundError):
        launcher.LocalProcessLauncher(repo_root=tmp_path).run_to_completion("bin/tool", [], tmp_path / "missing", timeout=10)


@pytest.mark.parametrize("launcher_class", [launcher.LocalProcessLauncher, launcher.DockerComposeLauncher])
@pytest.mark.parametrize("binary_relative", ["/usr/local/bin/ageland", "../ageland", "bin/../../ageland"])
def test_launchers_refuse_a_binary_outside_the_repo(tmp_path: Path, launcher_class: type, binary_relative: str) -> None:
    with pytest.raises(ValueError, match="relative to the repository root"):
        launcher_class(repo_root=tmp_path, binary_relative=binary_relative)


def test_docker_server_script_quotes_every_path(tmp_path: Path) -> None:
    run_dir = tmp_path / "build" / "integration" / "run dir"
    docker = launcher.DockerComposeLauncher(repo_root=tmp_path, binary_relative="bin/age land")

    command = docker.server_command(run_dir=run_dir, lib_dir=run_dir / "lib", port=4321, container_name="rots-it-q", seed=7)

    assert shlex.split(command[-1]) == [
        "ulimit", "-c", "0;", "cd", "/rots/build/integration/run dir", "&&",
        "exec", "/rots/bin/age land", "--random-seed", "7", "-d", "/rots/build/integration/run dir/lib", "4321",
    ]


def test_docker_tool_script_quotes_every_path(tmp_path: Path) -> None:
    docker = launcher.DockerComposeLauncher(repo_root=tmp_path)

    command = docker.tool_command("bin/rots tool", ["fixtures", "write"], tmp_path / "build" / "work dir", "rots-it-tool-q")

    assert shlex.split(command[-1]) == [
        "ulimit", "-c", "0;", "cd", "/rots/build/work dir", "&&", "exec", "/rots/bin/rots tool", "fixtures", "write",
    ]


class FakeClock:
    """Stands in for the time module: monotonic() reads a settable value and sleep() advances it."""

    def __init__(self, now: float) -> None:
        # The value monotonic() returns.
        self.now = now

    def monotonic(self) -> float:
        return self.now

    def sleep(self, seconds: float) -> None:
        self.now += seconds


def test_docker_start_gives_both_waits_one_startup_deadline(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    clock = FakeClock(now=1000.0)
    monkeypatch.setattr(launcher, "time", clock)
    monkeypatch.setattr(launcher.subprocess, "Popen", lambda *args, **kwargs: FakeComposeClient([]))
    binary = tmp_path / "bin" / "ageland"
    binary.parent.mkdir()
    binary.write_text("", encoding="utf-8")
    run_dir = tmp_path / "run"
    run_dir.mkdir()
    docker = launcher.DockerComposeLauncher(repo_root=tmp_path, startup_timeout=300.0)
    port_wait_deadlines: List[float] = []
    login_wait_timeouts: List[float] = []

    def slow_port_wait(container_name: str, container_port: int, process: object, log_path: Path, deadline: float) -> int:
        port_wait_deadlines.append(deadline)
        clock.now += 200.0
        return 49153

    def recording_login_wait(host: str, port: int, timeout_seconds: float, process: object, log_path: Path, time_allowed: Optional[str] = None) -> None:
        login_wait_timeouts.append(timeout_seconds)

    monkeypatch.setattr(docker, "_wait_for_published_port", slow_port_wait)
    monkeypatch.setattr(launcher, "wait_for_login_prompt", recording_login_wait)

    handle = docker.start(run_dir, run_dir / "lib", port=4321, seed=1)

    assert handle.port == 49153
    assert port_wait_deadlines == [1300.0]
    assert login_wait_timeouts == [100.0], "the login wait gets only what the port wait left"


@pytest.mark.parametrize("launcher_class", [launcher.LocalProcessLauncher, launcher.DockerComposeLauncher])
@pytest.mark.parametrize("binary_relative", ["", ".", "bin/.."])
def test_launchers_refuse_a_binary_that_names_the_repo_root(tmp_path: Path, launcher_class: type, binary_relative: str) -> None:
    with pytest.raises(ValueError, match="must name a file"):
        launcher_class(repo_root=tmp_path, binary_relative=binary_relative)


def test_require_program_counts_a_directory_as_missing(tmp_path: Path) -> None:
    (tmp_path / "bin").mkdir()
    with pytest.raises(RuntimeError, match="^bin is missing; build it first: "):
        launcher.LocalProcessLauncher(repo_root=tmp_path).require_program("bin")


def test_local_run_to_completion_explains_a_program_in_a_format_this_host_cannot_execute(tmp_path: Path) -> None:
    # No interpreter line and no machine code this host knows: exec fails with ENOEXEC, as an
    # i386 binary does on a Mac.
    program = tmp_path / "bin" / "rotstool"
    program.parent.mkdir()
    program.write_bytes(b"\x7fELF not a program for this host\n")
    program.chmod(0o700)

    with pytest.raises(RuntimeError) as raised:
        launcher.LocalProcessLauncher(repo_root=tmp_path).run_to_completion("bin/rotstool", [], tmp_path, timeout=10)

    assert str(raised.value) == f"cannot execute {program}: this host cannot run the i386 binaries; set ROTS_IT_LAUNCHER=docker"


def test_a_docker_start_whose_port_wait_spent_the_budget_names_the_whole_startup_timeout(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    clock = FakeClock(now=1000.0)
    monkeypatch.setattr(launcher, "time", clock)
    monkeypatch.setattr(launcher.subprocess, "Popen", lambda *args, **kwargs: FakeComposeClient([]))
    monkeypatch.setattr(launcher.subprocess, "run", recording_docker([]))
    monkeypatch.setattr(launcher, "terminate_process", lambda process: None)
    binary = tmp_path / "bin" / "ageland"
    binary.parent.mkdir()
    binary.write_text("", encoding="utf-8")
    run_dir = tmp_path / "run"
    run_dir.mkdir()
    docker = launcher.DockerComposeLauncher(repo_root=tmp_path, startup_timeout=300.0)

    def port_wait_using_the_whole_budget(container_name: str, container_port: int, process: object, log_path: Path, deadline: float) -> int:
        clock.now = deadline
        return 49153

    monkeypatch.setattr(docker, "_wait_for_published_port", port_wait_using_the_whole_budget)

    with pytest.raises(RuntimeError) as raised:
        docker.start(run_dir, run_dir / "lib", port=4321, seed=1)

    assert str(raised.value).startswith(
        f"the server on {launcher.LOOPBACK}:49153 did not send its login prompt within the 300.0s startup timeout, "
        "part of which the wait for the published port used; log tail:"
    )


# Sends a banner at once but the login prompt only after a delay, keeping the connection open.
TEXT_BEFORE_PROMPT_SERVER = FAKE_SERVER_PREAMBLE + """
connection, _address = listener.accept()
connection.sendall(NEGOTIATION + b"Return of the Shadow\\r\\n")
time.sleep(0.5)
(evidence_dir / "prompt-sent").write_text("yes")
connection.sendall(b"Account email: ")
time.sleep(30)
"""


def test_ready_needs_the_prompt_itself_not_just_some_text(tmp_path: Path, fake_servers: FakeServers) -> None:
    port = launcher.allocate_free_port()
    process = fake_servers.start(TEXT_BEFORE_PROMPT_SERVER, port)

    launcher.wait_for_login_prompt(launcher.LOOPBACK, port, 10.0, process, fake_servers.log_path)

    assert (tmp_path / "prompt-sent").exists()


def test_local_launcher_builds_the_harness_binary_command_for_a_schedule(tmp_path: Path) -> None:
    local = launcher.LocalProcessLauncher(repo_root=tmp_path, binary_relative="bin/ageland")
    lib_dir = tmp_path / "run" / "lib"
    command = local.command(lib_dir=lib_dir, port=4321, seed=7, schedule="affect-hours")
    assert command == [str(tmp_path / "bin" / "ageland_harness"), "--schedule", "affect-hours", "--random-seed", "7", "-d", str(lib_dir), "4321"]


def test_docker_launcher_runs_the_harness_binary_for_a_schedule(tmp_path: Path) -> None:
    run_dir = tmp_path / "build" / "integration" / "abc123"
    docker = launcher.DockerComposeLauncher(repo_root=tmp_path, binary_relative="bin/ageland")
    command = docker.server_command(run_dir=run_dir, lib_dir=run_dir / "lib", port=4321, container_name="rots-it-abc123", seed=7, schedule="affect-hours")
    assert "-i" not in command
    assert "exec /rots/bin/ageland_harness --schedule affect-hours --random-seed 7 -d /rots/build/integration/abc123/lib 4321" in command[-1]


def test_the_build_command_builds_the_harness_binary() -> None:
    assert "ageland_harness" in launcher.BUILD_COMMAND


@pytest.mark.parametrize("launcher_class", [launcher.LocalProcessLauncher, launcher.DockerComposeLauncher])
@pytest.mark.parametrize("schedule", ["affect-hours", None])
def test_start_pipes_stdout_only_with_a_schedule_and_always_logs_stderr(tmp_path: Path, monkeypatch: pytest.MonkeyPatch, launcher_class: type, schedule: Optional[str]) -> None:
    recorded_kwargs: list[dict] = []

    class StopAfterRecording(Exception):
        pass

    def recording_popen(*args: object, **kwargs: object):
        recorded_kwargs.append(kwargs)
        raise StopAfterRecording()

    harness_binary = tmp_path / launcher.HARNESS_BINARY_RELATIVE
    harness_binary.parent.mkdir()
    harness_binary.write_text("", encoding="utf-8")
    (tmp_path / "bin" / "ageland").write_text("", encoding="utf-8")
    monkeypatch.setattr(launcher.subprocess, "Popen", recording_popen)
    monkeypatch.setattr(launcher.DockerComposeLauncher, "_stop_container", lambda self, name: True)
    monkeypatch.setattr(launcher.DockerComposeLauncher, "_remove_container", lambda self, name: None)
    run_dir = tmp_path / "run"
    run_dir.mkdir()
    with pytest.raises(StopAfterRecording):
        launcher_class(repo_root=tmp_path).start(run_dir, run_dir / "lib", port=4321, seed=1, schedule=schedule)
    assert recorded_kwargs[0]["stdin"] == launcher.subprocess.PIPE
    if schedule is not None:
        assert recorded_kwargs[0]["stdout"] == launcher.subprocess.PIPE
        assert recorded_kwargs[0]["stderr"].name == str(run_dir / "game.log")
    else:
        assert recorded_kwargs[0]["stdout"].name == str(run_dir / "game.log")
        assert recorded_kwargs[0]["stderr"] == launcher.subprocess.STDOUT


class RecordingStandardInput(io.BytesIO):
    """A stdin pipe that records its close in the shared event list."""

    def __init__(self, events: list) -> None:
        super().__init__()
        # The ordered record of the stop's steps the test inspects.
        self._events = events

    def close(self) -> None:
        if not self.closed:
            self._events.append("close stdin")
        super().close()


class HarnessServerProcess:
    """A harness server that records its stdin close and the signals a stop sends it."""

    def __init__(self, events: list, exited: bool = False, has_stdin: bool = True) -> None:
        # The ordered record of the stop's steps the test inspects.
        self._events = events
        # None while the server runs; set when it has exited or is stopped.
        self.returncode: Optional[int] = 0 if exited else None
        # The pipe close_control_channel closes; None for a server started without one.
        self.stdin = RecordingStandardInput(events) if has_stdin else None
        # Named in a stop's warning if the server survives its kill.
        self.pid = 4242

    def poll(self) -> Optional[int]:
        return self.returncode

    def terminate(self) -> None:
        self._events.append("terminate")
        self.returncode = -15

    def kill(self) -> None:
        self._events.append("kill")
        self.returncode = -9

    def wait(self, timeout: Optional[float] = None) -> int:
        self._events.append("wait")
        return self.returncode or 0


class LoggingClock(FakeClock):
    """A FakeClock that records each sleep and logs the input-closed line on the sleep numbered line_on_sleep."""

    def __init__(
        self, events: list, log_path: Path, line_on_sleep: Optional[int] = None, interrupt_on_sleep: Optional[int] = None,
        split_at: Optional[int] = None,
    ) -> None:
        super().__init__(now=1000.0)
        # The ordered record of the stop's steps the test inspects.
        self._events = events
        # The game.log the server writes its input-closed line to.
        self._log_path = log_path
        # The sleep after which the server logs the line; None when it never does.
        self._line_on_sleep = line_on_sleep
        # The sleep that raises KeyboardInterrupt, as a Ctrl-C during the wait does; None for none.
        self._interrupt_on_sleep = interrupt_on_sleep
        # When set, the line is logged in two writes: its first split_at characters on sleep
        # line_on_sleep and the rest on the next sleep.
        self._split_at = split_at
        # How many times the wait has slept.
        self.sleeps = 0

    def sleep(self, seconds: float) -> None:
        super().sleep(seconds)
        self.sleeps += 1
        self._events.append("sleep")
        if self.sleeps == self._interrupt_on_sleep:
            raise KeyboardInterrupt
        logged_line = f"Tue Oct 13 12:00:00 2026 :: {launcher.INPUT_CLOSED_LOG_LINE}\n"
        if self._line_on_sleep is None:
            return
        if self._split_at is None:
            parts = {self._line_on_sleep: logged_line}
        else:
            split_index = len(logged_line) - len(launcher.INPUT_CLOSED_LOG_LINE) - 1 + self._split_at
            parts = {self._line_on_sleep: logged_line[:split_index], self._line_on_sleep + 1: logged_line[split_index:]}
        if self.sleeps in parts:
            with self._log_path.open("ab") as log_file:
                log_file.write(parts[self.sleeps].encode("ascii"))


def harness_handle(tmp_path: Path, process: HarnessServerProcess, container_name: Optional[str] = None) -> launcher.ServerHandle:
    """A handle on process for a server started with a schedule, whose game.log already holds boot output."""
    log_path = tmp_path / "game.log"
    log_path.write_bytes(b"Booting the world.\n")
    return launcher.ServerHandle(launcher.LOOPBACK, 4321, log_path, process, container_name, has_control_channel=True)  # type: ignore[arg-type]


def test_close_control_channel_closes_stdin_then_waits_for_the_input_closed_line(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    events: list = []
    log_path = tmp_path / "game.log"
    log_path.write_bytes(b"Booting the world.\n")
    clock = LoggingClock(events, log_path, line_on_sleep=3)
    monkeypatch.setattr(launcher, "time", clock)

    saw_line = launcher.close_control_channel(HarnessServerProcess(events), log_path)  # type: ignore[arg-type]

    assert saw_line
    assert events == ["close stdin", "sleep", "sleep", "sleep"]


def test_local_stop_terminates_a_harness_server_once_it_logs_that_its_input_closed(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    events: list = []
    process = HarnessServerProcess(events)
    handle = harness_handle(tmp_path, process)
    monkeypatch.setattr(launcher, "time", LoggingClock(events, handle.log_path, line_on_sleep=2))

    launcher.LocalProcessLauncher(repo_root=tmp_path).stop(handle)

    assert events == ["close stdin", "sleep", "sleep", "terminate", "wait"]


def test_docker_stop_stops_a_harness_container_once_it_logs_that_its_input_closed(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    events: list = []
    process = HarnessServerProcess(events)
    handle = harness_handle(tmp_path, process, "rots-it-stop")
    monkeypatch.setattr(launcher, "time", LoggingClock(events, handle.log_path, line_on_sleep=2))
    monkeypatch.setattr(launcher.subprocess, "run", recording_docker(events))

    launcher.DockerComposeLauncher(repo_root=tmp_path).stop(handle)

    assert events[:4] == ["close stdin", "sleep", "sleep", ("run", ["docker", "stop", "-t", "5", "rots-it-stop"])]


def test_a_stop_goes_ahead_after_the_timeout_when_the_line_never_arrives(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    events: list = []
    process = HarnessServerProcess(events)
    handle = harness_handle(tmp_path, process)
    clock = LoggingClock(events, handle.log_path)
    monkeypatch.setattr(launcher, "time", clock)

    launcher.LocalProcessLauncher(repo_root=tmp_path).stop(handle)

    assert events[0] == "close stdin"
    assert set(events[1:-2]) == {"sleep"}
    assert events[-2:] == ["terminate", "wait"]
    waited = clock.now - 1000.0
    assert launcher.INPUT_CLOSED_WAIT_SECONDS <= waited < launcher.INPUT_CLOSED_WAIT_SECONDS + 2 * launcher.INPUT_CLOSED_POLL_SECONDS


def test_close_control_channel_honours_a_shorter_timeout(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    events: list = []
    log_path = tmp_path / "game.log"
    clock = LoggingClock(events, log_path)
    monkeypatch.setattr(launcher, "time", clock)

    saw_line = launcher.close_control_channel(HarnessServerProcess(events), log_path, timeout_seconds=0.2)  # type: ignore[arg-type]

    assert not saw_line
    assert 0.2 <= clock.now - 1000.0 < 0.2 + 2 * launcher.INPUT_CLOSED_POLL_SECONDS


def test_a_stop_does_not_wait_for_a_server_that_has_already_exited(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    events: list = []
    process = HarnessServerProcess(events, exited=True)
    handle = harness_handle(tmp_path, process)
    monkeypatch.setattr(launcher, "time", LoggingClock(events, handle.log_path))

    launcher.LocalProcessLauncher(repo_root=tmp_path).stop(handle)

    # terminate_process does not signal a process that has exited.
    assert events == ["close stdin"]


def test_close_control_channel_does_not_wait_without_a_stdin_pipe(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    events: list = []
    log_path = tmp_path / "game.log"
    monkeypatch.setattr(launcher, "time", LoggingClock(events, log_path))

    saw_line = launcher.close_control_channel(HarnessServerProcess(events, has_stdin=False), log_path)  # type: ignore[arg-type]

    assert not saw_line
    assert events == []


def test_a_server_without_a_schedule_stops_without_waiting(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    events: list = []
    process = HarnessServerProcess(events)
    handle = launcher.ServerHandle(launcher.LOOPBACK, 4321, tmp_path / "game.log", process)  # type: ignore[arg-type]
    monkeypatch.setattr(launcher, "time", LoggingClock(events, handle.log_path))

    launcher.LocalProcessLauncher(repo_root=tmp_path).stop(handle)

    assert events == ["terminate", "wait", "close stdin"]


def test_a_ctrl_c_during_the_wait_still_stops_the_server_and_a_second_stop_does_not_wait_again(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch,
) -> None:
    events: list = []
    process = HarnessServerProcess(events)
    handle = harness_handle(tmp_path, process)
    monkeypatch.setattr(launcher, "time", LoggingClock(events, handle.log_path, interrupt_on_sleep=2))
    local = launcher.LocalProcessLauncher(repo_root=tmp_path)

    with pytest.raises(KeyboardInterrupt):
        local.stop(handle)
    local.stop(handle)

    assert events == ["close stdin", "sleep", "sleep", "terminate", "wait"]


# Reads stdin to its end, then logs the input-closed line on stderr and stays up until signalled.
READS_STDIN_TO_THE_END_SERVER = """
import sys
import time
sys.stdin.buffer.read()
sys.stderr.write("Tue Oct 13 12:00:00 2026 :: " + sys.argv[1] + "\\n")
sys.stderr.flush()
time.sleep(30)
"""


def test_close_control_channel_ignores_a_line_already_in_the_log(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    events: list = []
    log_path = tmp_path / "game.log"
    log_path.write_bytes(f"Tue Oct 13 11:00:00 2026 :: {launcher.INPUT_CLOSED_LOG_LINE}\n".encode("ascii"))
    clock = LoggingClock(events, log_path)
    monkeypatch.setattr(launcher, "time", clock)

    saw_line = launcher.close_control_channel(HarnessServerProcess(events), log_path)  # type: ignore[arg-type]

    assert not saw_line
    assert clock.now - 1000.0 >= launcher.INPUT_CLOSED_WAIT_SECONDS


def test_close_control_channel_finds_a_line_split_across_two_reads(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    events: list = []
    log_path = tmp_path / "game.log"
    log_path.write_bytes(b"Booting the world.\n")
    monkeypatch.setattr(launcher, "time", LoggingClock(events, log_path, line_on_sleep=1, split_at=20))

    saw_line = launcher.close_control_channel(HarnessServerProcess(events), log_path)  # type: ignore[arg-type]

    assert saw_line
    assert events == ["close stdin", "sleep", "sleep"]
    assert log_path.read_bytes().count(launcher.INPUT_CLOSED_LOG_LINE.encode("ascii")) == 1


def test_close_control_channel_sees_the_line_a_real_process_logs_at_end_of_input(tmp_path: Path) -> None:
    log_path = tmp_path / "game.log"
    with log_path.open("wb") as log_file:
        process = subprocess.Popen(
            [sys.executable, "-c", READS_STDIN_TO_THE_END_SERVER, launcher.INPUT_CLOSED_LOG_LINE],
            stdin=subprocess.PIPE, stdout=subprocess.DEVNULL, stderr=log_file,
        )
    try:
        # The one test on real time: a stated timeout, generous for a slow (emulated) host, keeps it
        # independent of the production wait.
        assert launcher.close_control_channel(process, log_path, timeout_seconds=10.0)
        assert process.poll() is None, "the line must arrive before the stop, not because the process exited"
    finally:
        launcher.terminate_process(process)
