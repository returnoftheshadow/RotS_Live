#!/usr/bin/env python3
"""Build a configured branch on the game server every night and install it into the coders port.

    nightly_deploy.py              the nightly run: fetch, build, run every suite, install (what cron runs)
    nightly_deploy.py --dry-run    fetch and report what the nightly run would do; builds and installs nothing
    nightly_deploy.py <command>    one step by hand: fetch, build, test, integration, smoke, install,
                                   write-source or status

The job keeps its files under ~/nightly. config.json names the repository and branch to fetch (remote_url,
branch, and token_file for a private GitHub repository) and which optional suites block the install
(integration_gate, smoke_gate). state.json records what was last installed, and build-record.json the build
in the clone and each suite's result against it; install refuses a build whose unit tests, or gated suites,
have not passed on it. Each install also replaces the port's src/ with the installed commit's source and a
.source-commit file naming it, together with the binary. pytest for the integration suite is unpacked by hand
into ~/nightly/pysite. Every nightly run and command appends one line to deploy.log and writes its full output
to runs/, except status and a run that finds the lock held. The port is never restarted: the game's routine
reboot starts the installed binary.
"""

import argparse
import contextlib
import dataclasses
import datetime
import fcntl
import hashlib
import json
import os
import re
import resource
import shlex
import shutil
import signal
import subprocess
import sys
import tarfile
import traceback
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, Dict, Iterator, List, Optional, Sequence, TextIO, Tuple


PORT_BIN_DIR = Path("/rots/dev-coding4810/bin")
KEPT_BACKUPS = 7
KEPT_RUN_LOGS = 14
# Room for an incremental build, the proxy build, a test server's tree, and the staged binary and its backup.
MIN_FREE_BYTES = 3 * 1024 ** 3
# A -j1 build of both targets takes about 5 minutes on the server; the limits only stop a hung step.
GIT_TIMEOUT_SECONDS = 10 * 60
BUILD_TIMEOUT_SECONDS = 2 * 60 * 60
TEST_TIMEOUT_SECONDS = 20 * 60
# CI allows the slower sanitized integration run 45 minutes; the unsanitized one fits well inside that.
INTEGRATION_TIMEOUT_SECONDS = 45 * 60
# Each of the smoke step's two long commands, the proxy build and the flow.
SMOKE_TIMEOUT_SECONDS = 15 * 60
FAILURE_TAIL_LINES = 40
STATUS_LINES = 5
STAMP_FORMAT = "%Y%m%d_%H%M%S"
# The suites a build record holds results for, in the order a run runs them.
SUITES = ("unit", "integration", "smoke")
PASSED = "passed"
CONFIG_KEYS = {"remote_url", "branch", "token_file", "integration_gate", "smoke_gate"}
# Each switches one optional suite from report-only to blocking the install.
GATE_KEYS = ("integration_gate", "smoke_gate")
GITHUB_URL_PATTERN = re.compile(r"^https://github\.com/[A-Za-z0-9._-]+/[A-Za-z0-9._-]+$")


class NightlyError(Exception):
    """A run step failed; the run stops and reports it."""

    def __init__(self, step: str, reason: str, detail: str = ""):
        super().__init__(reason)
        # Short step name for the summary line, such as "fetch" or "build".
        self.step = step
        # One-line explanation for the summary line.
        self.reason = reason
        # Multi-line context written at the end of the run log; empty when the reason says it all.
        self.detail = detail


class CommandFailed(NightlyError):
    """An external command exited non-zero, could not start, or timed out."""

    def __init__(self, step: str, reason: str, output: str):
        super().__init__(step, reason, tail_lines(output, FAILURE_TAIL_LINES))
        # Everything the command printed; `detail` keeps only its last FAILURE_TAIL_LINES lines.
        self.output = output


def tail_lines(text: str, count: int) -> str:
    """The last `count` lines of `text`, without a trailing newline."""
    lines = text.rstrip("\n").splitlines()
    return "\n".join(lines[-count:])


@dataclass(frozen=True)
class NightlyPaths:
    """Where one installation of the job keeps its files, and the port bin/ it installs into."""

    home: Path  # ~/nightly on the server; a temporary directory in tests
    port_bin: Path  # the port's bin/ that BinaryInstaller installs into

    @property
    def config(self) -> Path:
        return self.home / "config.json"

    @property
    def state(self) -> Path:
        return self.home / "state.json"

    @property
    def clone(self) -> Path:
        return self.home / "RotS_Live"

    @property
    def build(self) -> Path:
        return self.home / "build"

    @property
    def summary_log(self) -> Path:
        return self.home / "deploy.log"

    @property
    def run_logs(self) -> Path:
        return self.home / "runs"

    @property
    def lock(self) -> Path:
        return self.home / "lock"

    @property
    def build_record(self) -> Path:
        return self.home / "build-record.json"

    @property
    def pysite(self) -> Path:
        return self.home / "pysite"

    @property
    def port_dir(self) -> Path:
        return self.port_bin.parent

    @property
    def source_archive(self) -> Path:
        return self.home / "source.tar"

    @property
    def built_server(self) -> Path:
        return self.clone / "bin" / "ageland"

    @property
    def built_tests(self) -> Path:
        return self.clone / "bin" / "tests"


def is_valid_branch_name(branch: object) -> bool:
    """True when `branch` is a string git accepts as a branch name."""
    # A leading '-' would reach git as an option, so it is refused before git sees it.
    if not isinstance(branch, str) or not branch or branch.startswith("-"):
        return False
    result = subprocess.run(["git", "check-ref-format", "--branch", branch], capture_output=True, text=True)
    return result.returncode == 0


@dataclass(frozen=True)
class NightlyConfig:
    """The source a run fetches and which optional suites block the install.

    The source is a repository URL, a branch in it, and the token file that unlocks it.
    """

    remote_url: str
    branch: str
    token_file: Optional[Path]  # None for a repository that needs no credentials
    integration_gate: bool = False  # True when an integration suite that has not passed stops the install
    smoke_gate: bool = False  # True when an account smoke flow that has not passed stops the install

    @staticmethod
    def load(path: Path) -> "NightlyConfig":
        """Reads and validates config.json. Raises NightlyError("config") naming the first problem found."""
        try:
            raw_text = path.read_text()
        except OSError as error:
            raise NightlyError("config", f"cannot read {path}: {error.strerror}")
        try:
            data = json.loads(raw_text)
        except json.JSONDecodeError as error:
            raise NightlyError("config", f"{path} is not valid JSON: {error}")
        if not isinstance(data, dict):
            raise NightlyError("config", f"{path} must hold a JSON object")
        # Unknown keys are refused so a misspelt key cannot silently leave the default in force.
        unknown_keys = sorted(set(data) - CONFIG_KEYS)
        if unknown_keys:
            raise NightlyError("config", f"unknown keys in {path}: {', '.join(unknown_keys)}")
        remote_url = data.get("remote_url")
        if not isinstance(remote_url, str) or not remote_url:
            raise NightlyError("config", "remote_url must be a non-empty string")
        branch = data.get("branch")
        if not is_valid_branch_name(branch):
            raise NightlyError("config", f"branch {branch!r} is not a valid branch name")
        for gate_key in GATE_KEYS:
            if gate_key in data and not isinstance(data[gate_key], bool):
                raise NightlyError("config", f"{gate_key} must be true or false")
        integration_gate = data.get("integration_gate", False)
        smoke_gate = data.get("smoke_gate", False)
        token_value = data.get("token_file")
        if token_value is None:
            return NightlyConfig(remote_url, branch, None, integration_gate, smoke_gate)
        if not isinstance(token_value, str) or not token_value:
            raise NightlyError("config", "token_file must be a non-empty string when present")
        if not GITHUB_URL_PATTERN.match(remote_url):
            raise NightlyError("config", f"remote_url {remote_url!r} must be an https://github.com/<owner>/<repo> "
                                         "URL when token_file is set")
        token_file = Path(token_value).expanduser()
        if not os.access(token_file, os.R_OK):
            raise NightlyError("config", f"token file {token_file} is missing or unreadable")
        return NightlyConfig(remote_url, branch, token_file, integration_gate, smoke_gate)


def write_json_atomically(path: Path, data: dict) -> None:
    """Writes `data` as JSON through a temporary file and a rename, so a crash never leaves it half-written."""
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_text(json.dumps(data, indent=2) + "\n")
    os.replace(temporary, path)


@dataclass(frozen=True)
class InstalledState:
    """What the job last installed into the port, as recorded in state.json."""

    commit: str  # full SHA of the installed build
    remote_url: str  # the source the commit was fetched from
    branch: str
    binary_sha256: str  # checksum of bin/ageland right after the install, compared to spot a manual deploy
    installed_at: str  # local time of the install, ISO 8601

    @staticmethod
    def load(path: Path) -> Optional["InstalledState"]:
        """The recorded state, or None when nothing has been installed yet.

        Raises NightlyError("state") when the file exists but is unreadable or malformed.
        """
        if not path.exists():
            return None
        try:
            data = json.loads(path.read_text())
            return InstalledState(**data)
        except (OSError, ValueError, TypeError) as error:
            raise NightlyError("state", f"{path} is unreadable or malformed ({error}); "
                                        "delete it to adopt the current bin/ageland")

    def save(self, path: Path) -> None:
        """Writes the state atomically."""
        write_json_atomically(path, dataclasses.asdict(self))


@dataclass(frozen=True)
class BuildRecord:
    """The build in the clone's bin/ and each suite's result against it, as recorded in build-record.json."""

    commit: str  # full SHA the clone had checked out when it was built
    binary_sha256: str  # checksum of the clone's bin/ageland right after the build
    # Suite name to "passed", "failed: <reason>" or "skipped: <reason>"; a suite not yet run is absent.
    results: Dict[str, str] = dataclasses.field(default_factory=dict)

    @staticmethod
    def load(path: Path) -> Optional["BuildRecord"]:
        """The recorded build, or None when nothing is recorded.

        Raises NightlyError("build record") when the file exists but is unreadable or malformed.
        """
        if not path.exists():
            return None
        try:
            data = json.loads(path.read_text())
            return BuildRecord(**data)
        except (OSError, ValueError, TypeError) as error:
            raise NightlyError("build record", f"{path} is unreadable or malformed ({error}); run build again")

    def save(self, path: Path) -> None:
        """Writes the record atomically."""
        write_json_atomically(path, dataclasses.asdict(self))

    def with_result(self, suite: str, result: str) -> "BuildRecord":
        """A copy of this record with `suite`'s result set to `result`."""
        return dataclasses.replace(self, results={**self.results, suite: result})


def program_name_of(command: Sequence[str]) -> str:
    """The name of the program `command` runs, looking past a leading `nice -n <priority>`."""
    program_index = 0
    if Path(command[0]).name == "nice" and len(command) > 3 and command[1] == "-n":
        program_index = 3
    return Path(command[program_index]).name


def kill_process_group(process: subprocess.Popen) -> None:
    """Kills every process still in `process`'s group, which may already be empty."""
    try:
        os.killpg(process.pid, signal.SIGKILL)
    except ProcessLookupError:
        pass


class CommandRunner:
    """Runs one run's external commands, appending each command line and its output to the run log."""

    def __init__(self, run_log: TextIO):
        # Caller-owned; stays open across every run() call.
        self.run_log = run_log

    def run(self, args: Sequence[object], step: str, timeout: int, cwd: Optional[Path] = None,
            env: Optional[dict] = None) -> str:
        """Runs `args` to completion and returns its combined stdout and stderr.

        Raises CommandFailed(step) when the command exits non-zero, cannot start, or runs longer than
        `timeout` seconds. On a timeout, or when this job is interrupted or terminated meanwhile, every process
        still in the command's process group is killed. `env` replaces the whole environment when given.
        """
        command = [str(arg) for arg in args]
        program_name = program_name_of(command)
        self.run_log.write(f"$ {shlex.join(command)}\n")
        self.run_log.flush()
        try:
            # A session of its own lets a timeout stop everything the command started, such as the game
            # servers the integration harness boots, not only the command itself.
            process = subprocess.Popen(command, cwd=cwd, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                       text=True, errors="replace", start_new_session=True)
        except OSError as error:
            raise CommandFailed(step, f"cannot run {command[0]}: {error.strerror}", "")
        try:
            output, _no_stderr = process.communicate(timeout=timeout)
        except subprocess.TimeoutExpired:
            kill_process_group(process)
            partial_output, _no_stderr = process.communicate()
            self.run_log.write(partial_output)
            raise CommandFailed(step, f"{program_name} timed out after {timeout} s", partial_output)
        except BaseException:
            # The command's own session keeps it from the terminal's Ctrl-C, so an interrupted or terminated
            # job must stop it here.
            kill_process_group(process)
            process.wait()
            raise
        self.run_log.write(output)
        self.run_log.flush()
        if process.returncode != 0:
            raise CommandFailed(step, f"{program_name} exited with status {process.returncode}", output)
        return output


def credential_helper(token_file: Path) -> str:
    """A git credential helper that answers a `get` with the token read from `token_file` at call time.

    The helper text names the file, never the token, so the git command line is safe to log.
    """
    quoted_path = shlex.quote(str(token_file))
    return ("!nightly_token() { test \"$1\" = get || return 0; echo username=x-access-token; "
            f"printf 'password=%s\\n' \"$(cat {quoted_path})\"; }}; nightly_token")


# Substrings of git and GitHub output that mean the credentials were refused.
AUTHENTICATION_MARKERS = ("Authentication failed", "Invalid username or", "could not read Username",
                          "The requested URL returned error: 403", "The requested URL returned error: 401")
# GitHub answers 404 rather than 401 or 403 when a token cannot see a private repository.
REPOSITORY_HIDDEN_PATTERN = re.compile(r"Repository not found|fatal: repository '[^']*' not found")


def describe_fetch_failure(failure: CommandFailed, branch: str) -> str:
    """The summary-line reason for a failed fetch, naming the likely cause when git's output shows it."""
    if any(marker in failure.output for marker in AUTHENTICATION_MARKERS):
        return "GitHub refused the token: it may have expired, been revoked, or not been approved yet"
    if REPOSITORY_HIDDEN_PATTERN.search(failure.output):
        return "repository not found, or the token cannot see it (not yet approved, the wrong repository, or revoked)"
    if "couldn't find remote ref" in failure.output:
        return f"branch {branch} was not found at the source"
    return failure.reason


class SourceClone:
    """Keeps the job's own clone pointed at the configured source, fetches the branch tip, and checks it out."""

    def __init__(self, runner: CommandRunner, clone_dir: Path):
        # Runs every git command, so each one lands in the run log.
        self.runner = runner
        # Used by this job alone, so it is re-pointed and force-checked-out without regard for local changes.
        self.clone_dir = clone_dir

    def fetch(self, config: NightlyConfig) -> str:
        """Points origin at config.remote_url, fetches config.branch, and returns the tip's full commit SHA.

        Creates the clone on first use. A force-pushed branch replaces the previous tip. Raises
        NightlyError("fetch") when the branch cannot be fetched.
        """
        self._point_origin_at(config.remote_url)
        remote_ref = f"refs/remotes/origin/{config.branch}"
        fetch_command: List[object] = ["git", "-C", self.clone_dir]
        if config.token_file is not None:
            helper = credential_helper(config.token_file)
            # The empty value first clears any helper inherited from the user's git config.
            fetch_command += ["-c", "credential.helper=", "-c", f"credential.helper={helper}"]
        fetch_command += ["fetch", "--no-tags", "origin", f"+refs/heads/{config.branch}:{remote_ref}"]
        # With no terminal under cron, a prompt would only hang until the timeout.
        fetch_environment = {**os.environ, "GIT_TERMINAL_PROMPT": "0"}
        try:
            self.runner.run(fetch_command, "fetch", GIT_TIMEOUT_SECONDS, env=fetch_environment)
        except CommandFailed as failure:
            raise NightlyError("fetch", describe_fetch_failure(failure, config.branch), failure.detail)
        tip = self.runner.run(["git", "-C", self.clone_dir, "rev-parse", "--verify", f"{remote_ref}^{{commit}}"],
                              "fetch", GIT_TIMEOUT_SECONDS)
        return tip.strip()

    def check_out(self, commit: str) -> None:
        """Detaches the work tree at `commit`, discarding tracked changes; untracked build output stays."""
        self.runner.run(["git", "-C", self.clone_dir, "checkout", "--quiet", "--force", "--detach", commit],
                        "checkout", GIT_TIMEOUT_SECONDS)

    def head(self) -> str:
        """The full SHA the clone has checked out. Raises NightlyError("build") when there is no clone yet."""
        if not (self.clone_dir / ".git").is_dir():
            raise NightlyError("build", f"there is no clone at {self.clone_dir} yet; run fetch first")
        head = self.runner.run(["git", "-C", self.clone_dir, "rev-parse", "--verify", "HEAD^{commit}"], "build",
                               GIT_TIMEOUT_SECONDS)
        return head.strip()

    def _point_origin_at(self, remote_url: str) -> None:
        """Creates the clone on first use and makes its origin remote_url."""
        if not (self.clone_dir / ".git").is_dir():
            self.runner.run(["git", "init", "--quiet", self.clone_dir], "fetch", GIT_TIMEOUT_SECONDS)
        remotes = self.runner.run(["git", "-C", self.clone_dir, "remote"], "fetch", GIT_TIMEOUT_SECONDS).split()
        if "origin" in remotes:
            # Set on every run, so an edited config.json takes effect on the next fetch.
            self.runner.run(["git", "-C", self.clone_dir, "remote", "set-url", "origin", remote_url], "fetch",
                            GIT_TIMEOUT_SECONDS)
        else:
            self.runner.run(["git", "-C", self.clone_dir, "remote", "add", "origin", remote_url], "fetch",
                            GIT_TIMEOUT_SECONDS)


BACKUP_GLOB = "ageland.bak.*.nightly-*"
# Names the commit a port's src/ was written from; promotions carry it from port to port.
SOURCE_COMMIT_FILE = ".source-commit"
# A port's next src/, staged beside the current one so the swap is two renames on one filesystem.
STAGED_SOURCE_NAME = ".src-new"
SOURCE_BACKUP_GLOB = "src.bak.*"
KEPT_SOURCE_BACKUPS = 1
# The coders group's modes for a port's src/: the world sync's D2770 and F660, keeping the execute bit on
# executable files.
SOURCE_DIRECTORY_MODE = 0o2770
SOURCE_FILE_MODE = 0o660
SOURCE_EXECUTABLE_MODE = 0o770


def sha256_of(path: Path) -> str:
    """The SHA-256 of the file at `path`, as lowercase hex."""
    digest = hashlib.sha256()
    with path.open("rb") as binary_file:
        for chunk in iter(lambda: binary_file.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


@dataclass(frozen=True)
class InstalledBinary:
    """What one BinaryInstaller.install() did."""

    checksum: str  # SHA-256 of the installed bin/ageland
    backup: Optional[Path]  # where the replaced binary was kept, or None when there was none


def nightly_backup_name(stamp: str, replaced_commit: Optional[str]) -> str:
    """The name the nightly job keeps a replaced binary under.

    ageland.bak.<stamp>.nightly-<sha7 of replaced_commit>, or ageland.bak.<stamp>.before-nightly when
    `replaced_commit` is None (no install recorded), a name BACKUP_GLOB never matches.
    """
    if replaced_commit is None:
        return f"ageland.bak.{stamp}.before-nightly"
    return f"ageland.bak.{stamp}.nightly-{replaced_commit[:7]}"


class BinaryInstaller:
    """Swaps new builds into a port's bin/ageland, and restores or prunes the backups those swaps leave."""

    def __init__(self, port_bin: Path, backup_glob: str = BACKUP_GLOB):
        # The port's bin/ that install() swaps binaries into.
        self.port_bin = port_bin
        # Matches the backups prune_backups() may delete; the default BACKUP_GLOB leaves hand-made and
        # before-nightly backups alone.
        self.backup_glob = backup_glob

    @property
    def current(self) -> Path:
        return self.port_bin / "ageland"

    def check_replaceable(self, state: Optional[InstalledState]) -> None:
        """Raises NightlyError("install") when bin/ageland is not the binary `state` records as installed.

        Returns quietly when nothing has been installed yet (`state` is None) or bin/ageland is missing.
        """
        if state is None or not self.current.exists():
            return
        current_checksum = sha256_of(self.current)
        if current_checksum != state.binary_sha256:
            raise NightlyError("install", f"refused: bin/ageland was replaced since {state.commit[:7]} was "
                                          "installed; delete state.json to install over it")

    def install(self, new_binary: Path, backup_name: str) -> InstalledBinary:
        """Installs `new_binary` as bin/ageland (mode 0775), keeping the binary it replaces as `backup_name`.

        bin/ageland exists at every moment, so autorun can never start a missing binary. Raises OSError when
        the copy, the backup or the swap fails, and bin/ageland is then unchanged.
        """
        staged = self.port_bin / "ageland.nightly-new"
        shutil.copyfile(new_binary, staged)
        os.chmod(staged, 0o775)
        # Taken before the swap, so nothing after the swap can fail and leave the install unrecorded.
        installed_checksum = sha256_of(staged)
        backup = None
        if self.current.exists():
            backup = self.port_bin / backup_name
            try:
                # A hard link keeps the running server's image under the backup name without copying it.
                os.link(self.current, backup)
            except PermissionError:
                # Linux's protected_hardlinks refuses to link another user's file that the job cannot write.
                shutil.copy2(self.current, backup)
        # os.replace() over the old name is atomic: the running process keeps its inode, and the next start
        # gets the new file.
        os.replace(staged, self.current)
        return InstalledBinary(installed_checksum, backup)

    def restore(self, installed: InstalledBinary) -> None:
        """Undoes `installed` by moving its backup back to bin/ageland atomically.

        Removes bin/ageland instead when the install replaced nothing. Raises OSError on failure.
        """
        if installed.backup is None:
            self.current.unlink(missing_ok=True)
            return
        os.replace(installed.backup, self.current)

    def prune_backups(self) -> List[Path]:
        """Deletes all but the newest KEPT_BACKUPS backups matching `backup_glob` and returns the paths deleted."""
        # The timestamp follows a fixed prefix, so name order is age order.
        backups = sorted(self.port_bin.glob(self.backup_glob), reverse=True)
        removed = backups[KEPT_BACKUPS:]
        for backup in removed:
            backup.unlink()
        return removed


# The signals that end a run by hand or from cron; a swap holds them so it is never cut between its steps.
TERMINATION_SIGNALS = (signal.SIGTERM, signal.SIGHUP, signal.SIGINT)


@contextlib.contextmanager
def termination_signals_held() -> Iterator[None]:
    """Holds TERMINATION_SIGNALS for the block; any that arrive are delivered once it ends.

    A swap of a port's binary and source takes several steps, and a signal between two of them would leave
    the port half changed.
    """
    previous_mask = signal.pthread_sigmask(signal.SIG_BLOCK, TERMINATION_SIGNALS)
    try:
        yield
    finally:
        signal.pthread_sigmask(signal.SIG_SETMASK, previous_mask)


def swap_source(port_dir: Path, new_source: Path, stamp: str) -> Optional[Path]:
    """Makes `new_source` the port's src/, keeping the old src/ as src.bak.<stamp>, and returns that backup.

    Returns None when the port had no src/. Raises OSError, or re-raises an interrupt, with the port's src/
    as before.
    """
    source = port_dir / "src"
    backup = None
    if source.exists():
        backup = port_dir / f"src.bak.{stamp}"
        os.rename(source, backup)
    try:
        os.rename(new_source, source)
    except BaseException:
        if backup is not None:
            os.rename(backup, source)
        raise
    return backup


@dataclass(frozen=True)
class PairedInstall:
    """What one install_with_source() did."""

    binary: InstalledBinary  # the binary swap
    source_backup: Optional[Path]  # where the old src/ was kept, or None when the port had none


def install_with_source(installer: BinaryInstaller, new_binary: Path, backup_name: str, port_dir: Path,
                        new_source: Path, stamp: str) -> PairedInstall:
    """Swaps in `new_binary`, then `new_source` as the port's src/, so the binary and its source change together.

    Raises OSError, or re-raises an interrupt, with the port's bin/ageland and src/ both as before.
    """
    installed = installer.install(new_binary, backup_name)
    try:
        source_backup = swap_source(port_dir, new_source, stamp)
    except BaseException:
        installer.restore(installed)
        raise
    return PairedInstall(installed, source_backup)


def prune_source_backups(port_dir: Path) -> List[Path]:
    """Deletes all but the newest KEPT_SOURCE_BACKUPS src.bak.* directories and returns those deleted."""
    # The timestamp follows a fixed prefix, so name order is age order.
    backups = sorted(port_dir.glob(SOURCE_BACKUP_GLOB), reverse=True)
    removed = backups[KEPT_SOURCE_BACKUPS:]
    for backup in removed:
        shutil.rmtree(backup)
    return removed


def prune_source_backups_after_install(port_dir: Path) -> str:
    """Prunes the port's source backups after a finished install, returning "" or a note for the summary line.

    The install has already succeeded, so a backup that cannot be deleted (one another user owns, say) is
    reported, not treated as a failed run.
    """
    try:
        prune_source_backups(port_dir)
    except OSError as error:
        return f"; could not delete an old source backup: {error}"
    return ""


def apply_source_modes(tree: Path) -> None:
    """Gives `tree` and every directory in it SOURCE_DIRECTORY_MODE, and every file SOURCE_FILE_MODE.

    Executable files get SOURCE_EXECUTABLE_MODE instead.
    """
    os.chmod(tree, SOURCE_DIRECTORY_MODE)
    for directory, subdirectory_names, file_names in os.walk(tree):
        for subdirectory_name in subdirectory_names:
            os.chmod(Path(directory) / subdirectory_name, SOURCE_DIRECTORY_MODE)
        for file_name in file_names:
            file_path = Path(directory) / file_name
            file_mode = SOURCE_FILE_MODE
            if file_path.stat().st_mode & 0o100:
                file_mode = SOURCE_EXECUTABLE_MODE
            os.chmod(file_path, file_mode)


def stage_port_source(runner: CommandRunner, paths: NightlyPaths, commit: str) -> Path:
    """Stages `commit`'s src/, with a .source-commit file naming the commit, as the port's .src-new.

    Replaces any leftover .src-new and returns its path. The source comes from `git archive`, so it holds no
    build output, and gets the coders group's modes.
    Raises NightlyError("source") when git or the extraction fails, leaving no .src-new behind.
    """
    staged = paths.port_dir / STAGED_SOURCE_NAME
    shutil.rmtree(staged, ignore_errors=True)
    runner.run(["git", "-C", paths.clone, "archive", "--format=tar", "-o", paths.source_archive, commit, "src"],
               "source", GIT_TIMEOUT_SECONDS)
    try:
        with tarfile.open(paths.source_archive) as archive:
            members = []
            for member in archive.getmembers():
                if not member.name.startswith("src/"):
                    continue
                member.name = member.name.removeprefix("src/")
                members.append(member)
            staged.mkdir()
            archive.extractall(staged, members=members, filter="data")
        (staged / SOURCE_COMMIT_FILE).write_text(commit + "\n")
        apply_source_modes(staged)
    except (OSError, tarfile.TarError) as error:
        shutil.rmtree(staged, ignore_errors=True)
        raise NightlyError("source", f"cannot stage {commit[:7]}'s source: {error}")
    finally:
        paths.source_archive.unlink(missing_ok=True)
    return staged


# GoogleTest's "[  FAILED  ] Suite.Name" lines; requiring a dot skips its "[  FAILED  ] 2 tests, listed below" line.
FAILED_TEST_PATTERN = re.compile(r"^\[  FAILED  \] (\S+\.\S+)", re.MULTILINE)
# pytest's short summary lines, "FAILED <node id> - <message>" and "ERROR <node id> - <message>".
PYTEST_FAILED_PATTERN = re.compile(r"^(?:FAILED|ERROR) (\S+)", re.MULTILINE)
# A summary line names at most this many failed tests, so one broken harness cannot flood deploy.log.
LISTED_FAILURES = 5
INTEGRATION_DIR = Path("tests") / "integration"
# The committed world the smoke server boots, since lib/world is not tracked.
TEST_WORLD = INTEGRATION_DIR / "world"
SMOKE_SCRIPT = Path("tools") / "account_smoke.py"


@dataclass(frozen=True)
class RunOutcome:
    """How a run that did not fail ended."""

    commit: str  # the fetched commit the run decided about
    message: str  # the summary-line result, such as "installed"


def build(runner: CommandRunner, paths: NightlyPaths) -> None:
    """Builds the server and the unit tests into the clone's bin/, configuring the build directory on first use.

    Runs at low priority on one core, because the live ports share the server's two cores.
    Raises NightlyError("configure" or "build") on failure.
    """
    if not (paths.build / "CMakeCache.txt").exists():
        runner.run(["cmake", "-S", paths.clone / "src", "-B", paths.build, "-DCMAKE_CXX_COMPILER=g++"],
                   "configure", BUILD_TIMEOUT_SECONDS)
    runner.run(["nice", "-n", "10", "cmake", "--build", paths.build, "--target", "ageland", "ageland_tests", "-j1"],
               "build", BUILD_TIMEOUT_SECONDS)
    if not paths.built_server.exists():
        raise NightlyError("build", f"the build finished but {paths.built_server} does not exist")


def build_and_record(runner: CommandRunner, paths: NightlyPaths, commit: str) -> BuildRecord:
    """Builds `commit`, already checked out in the clone, and records the new binary with no suite results.

    The old record goes first, so no result carries over to another binary, even when the build fails
    part-way. Raises NightlyError("configure" or "build") on failure.
    """
    paths.build_record.unlink(missing_ok=True)
    build(runner, paths)
    binary_checksum = sha256_of(paths.built_server)
    record = BuildRecord(commit, binary_checksum)
    record.save(paths.build_record)
    return record


def verified_build(paths: NightlyPaths, step: str) -> BuildRecord:
    """The build record, after checking that the clone's bin/ageland is still the binary it records.

    Raises NightlyError(step) when nothing is recorded or the binary is missing or has changed since.
    """
    record = BuildRecord.load(paths.build_record)
    if record is None or not paths.built_server.exists():
        raise NightlyError(step, "no build recorded; run build first")
    if sha256_of(paths.built_server) != record.binary_sha256:
        raise NightlyError(step, "bin/ageland no longer matches the recorded build; run build first")
    return record


def describe_failed_tests(names: Sequence[str]) -> str:
    """'N failed: a, b', naming at most LISTED_FAILURES tests."""
    listed = ", ".join(names[:LISTED_FAILURES])
    if len(names) > LISTED_FAILURES:
        listed += f" and {len(names) - LISTED_FAILURES} more"
    return f"{len(names)} failed: {listed}"


def run_unit_tests(runner: CommandRunner, paths: NightlyPaths) -> str:
    """Runs the unit-test binary from the clone's root, as CI does, and returns PASSED.

    Raises NightlyError("unit tests") naming the failed tests, or with the command's own reason when the
    output names none (a crash or a timeout).
    """
    try:
        runner.run([paths.built_tests], "unit tests", TEST_TIMEOUT_SECONDS, cwd=paths.clone)
    except CommandFailed as failure:
        failed_names = sorted(set(FAILED_TEST_PATTERN.findall(failure.output)))
        if not failed_names:
            raise
        raise NightlyError("unit tests", describe_failed_tests(failed_names), failure.detail)
    return PASSED


def run_integration_tests(runner: CommandRunner, paths: NightlyPaths) -> str:
    """Runs the clone's integration suite against its bin/ageland and returns the result.

    Returns PASSED, or a skip result when the checked-out branch has no suite. The harness boots its own
    server on a free loopback port and never touches a live port. Raises NightlyError("integration") when
    ~/nightly/pysite is missing, naming the failed tests, or with the command's own reason when the output
    names none (a crash, a timeout, or nothing collected).
    """
    if not (paths.clone / INTEGRATION_DIR).is_dir():
        return f"skipped: no {INTEGRATION_DIR} on this branch"
    if not paths.pysite.is_dir():
        raise NightlyError("integration", f"{paths.pysite} is missing; pytest must be unpacked there first")
    environment = {**os.environ, "PYTHONPATH": str(paths.pysite), "ROTS_IT_LAUNCHER": "local"}
    command = ["nice", "-n", "10", sys.executable, "-m", "pytest", "-p", "no:cacheprovider", INTEGRATION_DIR]
    try:
        runner.run(command, "integration", INTEGRATION_TIMEOUT_SECONDS, cwd=paths.clone, env=environment)
    except CommandFailed as failure:
        failed_ids = sorted(set(PYTEST_FAILED_PATTERN.findall(failure.output)))
        if not failed_ids:
            raise
        directory_prefix = INTEGRATION_DIR.as_posix() + "/"
        short_ids = [failed_id.removeprefix(directory_prefix) for failed_id in failed_ids]
        raise NightlyError("integration", describe_failed_tests(short_ids), failure.detail)
    return PASSED


def reset_runtime_lib(runner: CommandRunner, clone: Path) -> None:
    """Returns the clone's lib/ to the checked-out commit's files and deletes everything else in it.

    That removes the accounts, characters and world an earlier smoke run left. Raises NightlyError("smoke")
    when git fails.
    """
    runner.run(["git", "-C", clone, "clean", "-d", "-x", "--force", "--quiet", "--", "lib"], "smoke",
               GIT_TIMEOUT_SECONDS)
    runner.run(["git", "-C", clone, "checkout", "--quiet", "--", "lib"], "smoke", GIT_TIMEOUT_SECONDS)


def run_smoke_flow(runner: CommandRunner, paths: NightlyPaths) -> str:
    """Runs the account smoke flow against the clone's bin/ageland and returns the result.

    Returns PASSED, or a skip result when the checked-out branch lacks the flow or its test world. Builds the
    proxy, which the flow starts with `cargo run`, then resets lib/, runs the setup target and installs the
    test world. Raises NightlyError("smoke") when any of those commands or the flow fails.
    """
    if not (paths.clone / SMOKE_SCRIPT).is_file() or not (paths.clone / TEST_WORLD).is_dir():
        return f"skipped: no {SMOKE_SCRIPT} or {TEST_WORLD} on this branch"
    runner.run(["nice", "-n", "10", "cargo", "build", "--locked", "-p", "proxy", "-j1"], "smoke",
               SMOKE_TIMEOUT_SECONDS, cwd=paths.clone)
    reset_runtime_lib(runner, paths.clone)
    runner.run(["cmake", "--build", paths.build, "--target", "setup"], "smoke", BUILD_TIMEOUT_SECONDS)
    try:
        shutil.copytree(paths.clone / TEST_WORLD, paths.clone / "lib" / "world")
    except OSError as error:
        raise NightlyError("smoke", f"cannot install the test world: {error}")
    runner.run([sys.executable, SMOKE_SCRIPT], "smoke", SMOKE_TIMEOUT_SECONDS, cwd=paths.clone)
    return PASSED


def run_recorded_suite(suite: str, runner: CommandRunner, paths: NightlyPaths) -> str:
    """Runs `suite` ("unit", "integration" or "smoke") against the recorded build and records its result.

    Returns the result: PASSED, "failed: <reason>" or "skipped: <reason>". Raises NightlyError(suite) when
    there is no matching build to run it against, and NightlyError("build record") when the record is
    malformed.
    """
    record = verified_build(paths, suite)
    try:
        if suite == "unit":
            result = run_unit_tests(runner, paths)
        elif suite == "integration":
            result = run_integration_tests(runner, paths)
        else:
            result = run_smoke_flow(runner, paths)
    except NightlyError as error:
        result = f"failed: {error.reason}"
    record.with_result(suite, result).save(paths.build_record)
    return result


def check_free_space(home: Path, min_free_bytes: int = MIN_FREE_BYTES) -> None:
    """Raises NightlyError("disk") when the filesystem holding `home` has less than `min_free_bytes` free."""
    free_bytes = shutil.disk_usage(home).free
    if free_bytes < min_free_bytes:
        raise NightlyError("disk", f"only {free_bytes // 1024 ** 2} MB free under {home}; "
                                   f"a run needs {min_free_bytes // 1024 ** 2} MB")


def describe_suite_result(suite: str, result: str) -> str:
    """A suite's result for a summary line.

    For example 'integration passed', 'integration failed (<reason>)' or 'smoke skipped (<reason>)'.
    """
    outcome, _separator, reason = result.partition(": ")
    if not reason:
        return f"{suite} {outcome}"
    return f"{suite} {outcome} ({reason})"


def check_gates(record: BuildRecord, config: NightlyConfig) -> None:
    """Raises NightlyError("install") unless the unit tests and every gated suite passed on this build."""
    required_suites = ["unit"]
    if config.integration_gate:
        required_suites.append("integration")
    if config.smoke_gate:
        required_suites.append("smoke")
    for suite in required_suites:
        result = record.results.get(suite, "not run")
        if result != PASSED:
            raise NightlyError("install", f"refused: {suite} has not passed on this build ({result})")


def install_recorded_build(config: NightlyConfig, paths: NightlyPaths, installer: BinaryInstaller,
                           runner: CommandRunner, now: datetime.datetime) -> str:
    """Installs the recorded build into the port when its gates allow, and records it in state.json.

    The port's src/ is replaced with the build's source together with the binary. Returns "installed", or
    "skipped: already installed" when state.json already records this exact build as installed. Raises
    NightlyError("install") when the clone's binary no longer matches its record, a gate refuses it, the port's
    bin/ageland was replaced by hand since the last install, or the copy or either swap fails, and
    NightlyError("source") when
    the source cannot be staged.
    """
    record = verified_build(paths, "install")
    check_gates(record, config)
    state = InstalledState.load(paths.state)
    if state is not None and state.commit == record.commit and state.binary_sha256 == record.binary_sha256:
        return "skipped: already installed"
    # Checked here, not only before a build, because someone may have deployed by hand since.
    installer.check_replaceable(state)
    replaced_commit = None
    if state is not None:
        replaced_commit = state.commit
    stamp = now.strftime(STAMP_FORMAT)
    staged_source = stage_port_source(runner, paths, record.commit)
    backup_name = nightly_backup_name(stamp, replaced_commit)
    installed_at = now.isoformat(timespec="seconds")
    # Held until state.json records the swap, so a terminated run never leaves an install it did not record.
    with termination_signals_held():
        try:
            paired = install_with_source(installer, paths.built_server, backup_name, paths.port_dir,
                                         staged_source, stamp)
        except OSError as error:
            shutil.rmtree(staged_source, ignore_errors=True)
            raise NightlyError("install", f"could not install into {paths.port_dir}: {error}")
        new_state = InstalledState(record.commit, config.remote_url, config.branch, paired.binary.checksum,
                                   installed_at)
        new_state.save(paths.state)
    installer.prune_backups()
    return "installed" + prune_source_backups_after_install(paths.port_dir)


def write_installed_source(runner: CommandRunner, paths: NightlyPaths,
                           now: datetime.datetime) -> Tuple[str, str]:
    """Replaces the port's src/ with the source of the commit installed there.

    Returns that commit and "" or a note about a source backup that could not be pruned. The binary is not
    touched. Raises NightlyError("write-source") when nothing is recorded as installed, the port's bin/ageland
    is no longer the installed binary (so src/ only ever receives the source of the binary beside it), or the
    swap fails, and NightlyError("source") when the source cannot be staged.
    """
    state = InstalledState.load(paths.state)
    if state is None:
        raise NightlyError("write-source", "nothing has been installed yet")
    port_binary = paths.port_bin / "ageland"
    if not port_binary.exists() or sha256_of(port_binary) != state.binary_sha256:
        raise NightlyError("write-source", f"bin/ageland is not the {state.commit[:7]} build recorded as installed")
    staged_source = stage_port_source(runner, paths, state.commit)
    stamp = now.strftime(STAMP_FORMAT)
    try:
        with termination_signals_held():
            swap_source(paths.port_dir, staged_source, stamp)
    except OSError as error:
        shutil.rmtree(staged_source, ignore_errors=True)
        raise NightlyError("write-source", f"could not replace {paths.port_dir / 'src'}: {error}")
    return state.commit, prune_source_backups_after_install(paths.port_dir)


def nightly_run(config: NightlyConfig, commit: str, paths: NightlyPaths, runner: CommandRunner,
                clone: SourceClone, installer: BinaryInstaller, dry_run: bool,
                now: datetime.datetime) -> RunOutcome:
    """Decides what to do with the fetched `commit`, then checks out, builds, runs every suite and installs it.

    A commit already installed is skipped (recording a changed source unless `dry_run`). A dry run stops
    after the replaceability check. The unit tests, and each suite whose gate is on, stop the run when they
    do not pass; a report-only suite does not stop the run, and its result goes on the summary line. Raises
    NightlyError naming the failed step; the port's bin/ageland changes only when every blocking step succeeded.
    """
    state = InstalledState.load(paths.state)
    if state is not None and state.commit == commit:
        source_changed = (state.remote_url, state.branch) != (config.remote_url, config.branch)
        if source_changed and not dry_run:
            updated_state = dataclasses.replace(state, remote_url=config.remote_url, branch=config.branch)
            updated_state.save(paths.state)
        return RunOutcome(commit, "skipped: already installed")
    installer.check_replaceable(state)
    if dry_run:
        return RunOutcome(commit, "dry run: would build, test and install")
    check_free_space(paths.home)
    clone.check_out(commit)
    build_and_record(runner, paths, commit)
    unit_result = run_recorded_suite("unit", runner, paths)
    if unit_result != PASSED:
        raise NightlyError("unit tests", unit_result.removeprefix("failed: "))
    suite_notes = []
    for suite, gate_is_on in (("integration", config.integration_gate), ("smoke", config.smoke_gate)):
        result = run_recorded_suite(suite, runner, paths)
        if gate_is_on and result != PASSED:
            raise NightlyError(suite, f"{result.removeprefix('failed: ')} ({suite}_gate is on)")
        suite_notes.append(describe_suite_result(suite, result))
    message = install_recorded_build(config, paths, installer, runner, now)
    return RunOutcome(commit, "; ".join([message] + suite_notes))


# The suite each suite command runs.
SUITE_FOR_COMMAND = {"test": "unit", "integration": "integration", "smoke": "smoke"}


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="nightly_deploy.py",
                                     description="Build a configured branch and install it into the coders port. "
                                                 "With no command, run every step, as cron does.")
    parser.add_argument("--dry-run", action="store_true",
                        help="with no command: fetch and report what a run would do; build and install nothing")
    commands = parser.add_subparsers(dest="command", metavar="command")
    commands.add_parser("fetch", help="fetch the configured branch into the clone and check out its tip")
    commands.add_parser("build", help="build the checked-out commit and record the binary")
    commands.add_parser("test", help="run the unit tests against the recorded build")
    commands.add_parser("integration", help="run the integration suite against the recorded build")
    commands.add_parser("smoke", help="run the account smoke flow against the recorded build")
    commands.add_parser("install", help="install the recorded build into the port, if its gates allow")
    commands.add_parser("write-source", help="write the installed commit's source into the port's src/")
    commands.add_parser("status", help="print the installed build, the recorded build and the last few runs")
    return parser


def summary_line(when: datetime.datetime, config: Optional[NightlyConfig], commit: Optional[str],
                 result: str) -> str:
    """One deploy.log line: time, source, short commit and result, with '-' for what the run never learned."""
    source = "-"
    if config is not None:
        source = f"{config.remote_url} {config.branch}"
    short_commit = "-"
    if commit is not None:
        short_commit = commit[:7]
    return f"{when.strftime('%Y-%m-%d %H:%M:%S')}  {source}  {short_commit}  {result}"


def append_summary(paths: NightlyPaths, line: str) -> None:
    with paths.summary_log.open("a") as summary_log:
        summary_log.write(line + "\n")


def prune_run_logs(run_logs: Path) -> None:
    """Deletes all but the newest KEPT_RUN_LOGS run logs."""
    # Names start with the run's timestamp, so name order is age order.
    logs = sorted(run_logs.glob("*.log"), reverse=True)
    for old_log in logs[KEPT_RUN_LOGS:]:
        old_log.unlink()


def status_report(paths: NightlyPaths) -> str:
    """The installed build, the recorded build and the last STATUS_LINES summary lines."""
    lines = []
    try:
        state = InstalledState.load(paths.state)
    except NightlyError as error:
        lines.append(f"Installed: unknown ({error.reason})")
    else:
        if state is None:
            lines.append("Installed: nothing yet")
        else:
            lines.append(f"Installed: {state.commit[:7]} from {state.remote_url} {state.branch} "
                         f"at {state.installed_at}")
    try:
        record = BuildRecord.load(paths.build_record)
    except NightlyError as error:
        lines.append(f"Built: unknown ({error.reason})")
    else:
        if record is None:
            lines.append("Built: nothing recorded")
        else:
            results = ", ".join(describe_suite_result(suite, record.results.get(suite, "not run"))
                                for suite in SUITES)
            lines.append(f"Built: {record.commit[:7]} ({results})")
    if paths.summary_log.exists():
        recent = paths.summary_log.read_text().splitlines()[-STATUS_LINES:]
        lines.append("Recent runs:")
        lines.extend("  " + entry for entry in recent)
    else:
        lines.append("No runs recorded yet")
    return "\n".join(lines)


class RunContext:
    """What a run has learned so far, so its summary line names the source and commit even when it fails."""

    def __init__(self) -> None:
        # config.json once loaded, or None.
        self.config: Optional[NightlyConfig] = None
        # The commit the run is about once known, or None.
        self.commit: Optional[str] = None


def perform_command(command: Optional[str], dry_run: bool, context: RunContext, runner: CommandRunner,
                    paths: NightlyPaths, started: datetime.datetime) -> str:
    """Runs `command`, or the nightly run when it is None, and returns its summary-line result.

    Raises NightlyError naming the failed step. A suite command whose suite fails raises after recording
    the result, so the command exits non-zero.
    """
    clone = SourceClone(runner, paths.clone)
    installer = BinaryInstaller(paths.port_bin)
    if command is None:
        context.config = NightlyConfig.load(paths.config)
        context.commit = clone.fetch(context.config)
        return nightly_run(context.config, context.commit, paths, runner, clone, installer, dry_run, started).message
    if command == "fetch":
        context.config = NightlyConfig.load(paths.config)
        context.commit = clone.fetch(context.config)
        clone.check_out(context.commit)
        record = BuildRecord.load(paths.build_record)
        if record is not None and record.commit != context.commit:
            # Its binary was built from other sources than the checkout the suites would now run.
            paths.build_record.unlink()
            return "fetched; the previous build was of another commit and was discarded"
        return "fetched"
    if command == "build":
        check_free_space(paths.home)
        context.commit = clone.head()
        build_and_record(runner, paths, context.commit)
        return "built"
    if command == "write-source":
        context.commit, prune_note = write_installed_source(runner, paths, started)
        return "source written" + prune_note
    if command == "install":
        context.config = NightlyConfig.load(paths.config)
        context.commit = verified_build(paths, "install").commit
        return install_recorded_build(context.config, paths, installer, runner, started)
    suite = SUITE_FOR_COMMAND[command]
    context.commit = verified_build(paths, suite).commit
    result = run_recorded_suite(suite, runner, paths)
    if result.startswith("failed: "):
        raise NightlyError(suite, result.removeprefix("failed: "))
    return result


def run_command_logged(command: Optional[str], dry_run: bool, paths: NightlyPaths, run_log_path: Path,
                       started: datetime.datetime) -> int:
    """Runs `command` (None for the nightly run) with its output in `run_log_path`, then records the summary line.

    Returns 0 on success, a skip or a dry run, 1 for any failure, and 128 plus the signal number when a signal
    terminated the run. Every failure, expected or not, is
    recorded in deploy.log, because nobody reads the output of a cron run; the line is also printed for runs
    started by hand.
    """
    context = RunContext()
    with run_log_path.open("w") as run_log:
        runner = CommandRunner(run_log)
        try:
            result = perform_command(command, dry_run, context, runner, paths, started)
            exit_status = 0
        except NightlyError as error:
            result = f"failed: {error.step}: {error.reason}"
            run_log.write(f"\nFAILED at {error.step}: {error.reason}\n")
            if error.detail:
                run_log.write(error.detail + "\n")
            exit_status = 1
        except SystemExit as termination:
            # A signal handler's exit; the running command's process group was killed as the stack unwound.
            exit_status = 1
            if isinstance(termination.code, int):
                exit_status = termination.code
            result = f"failed: terminated (exit {exit_status})"
            run_log.write(f"\nTERMINATED with exit {exit_status}\n")
        except Exception:
            result = "failed: unexpected error; see the run log"
            run_log.write("\n" + traceback.format_exc())
            exit_status = 1
    if command is not None:
        result = f"{command}: {result}"
    line = summary_line(started, context.config, context.commit, result) + f"  (log: runs/{run_log_path.name})"
    append_summary(paths, line)
    print(line)
    return exit_status


def raise_system_exit(signal_number: int, frame: object) -> None:
    """Handler for SIGTERM, SIGHUP and SIGINT that unwinds like an exception.

    The running command's process group is then killed and the run is logged.
    """
    raise SystemExit(128 + signal_number)


def main(argv: Optional[Sequence[str]] = None, paths: Optional[NightlyPaths] = None,
         now: Callable[[], datetime.datetime] = datetime.datetime.now) -> int:
    """Runs the command line and returns the process exit status.

    `paths` defaults to ~/nightly and the coders port; tests pass their own, and a clock through `now`.
    """
    parser = build_parser()
    arguments = parser.parse_args(argv)
    if arguments.dry_run and arguments.command is not None:
        parser.error("--dry-run applies only to the nightly run, not to a command")
    if paths is None:
        paths = NightlyPaths(Path.home() / "nightly", PORT_BIN_DIR)
    if arguments.command == "status":
        print(status_report(paths))
        return 0
    # SIGHUP covers a hand run whose ssh session drops.
    for signal_number in (signal.SIGTERM, signal.SIGHUP, signal.SIGINT):
        signal.signal(signal_number, raise_system_exit)
    paths.run_logs.mkdir(parents=True, exist_ok=True)
    started = now()
    with paths.lock.open("a") as lock_file:
        try:
            fcntl.flock(lock_file, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            if arguments.command is None:
                append_summary(paths, summary_line(started, None, None, "skipped: another run holds the lock"))
                return 0
            print("another nightly run holds the lock; try again when it finishes", file=sys.stderr)
            return 1
        # Children inherit the limit, so a crashing test or build tool leaves no core file behind.
        resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
        run_log_name = started.strftime(STAMP_FORMAT)
        if arguments.dry_run:
            run_log_name += "-dry-run"
        elif arguments.command is not None:
            run_log_name += f"-{arguments.command}"
        exit_status = run_command_logged(arguments.command, arguments.dry_run, paths,
                                         paths.run_logs / f"{run_log_name}.log", started)
        prune_run_logs(paths.run_logs)
        return exit_status


if __name__ == "__main__":
    sys.exit(main())
