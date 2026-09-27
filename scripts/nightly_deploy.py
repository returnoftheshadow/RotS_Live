#!/usr/bin/env python3
"""Build a configured branch on the game server every night and install it into the coders port.

    nightly_deploy.py              one run: fetch, build, unit-test and install (what cron runs)
    nightly_deploy.py --dry-run    fetch and report what a run would do; builds and installs nothing
    nightly_deploy.py --status     print the installed build and the last few runs

The job keeps its files under ~/nightly. config.json names the repository and branch to fetch
(remote_url, branch, and token_file for a private GitHub repository); state.json records what was
last installed. Every run appends one line to deploy.log and writes its full output to runs/.
The port is never restarted: the game's routine reboot starts the installed binary.
"""

import argparse
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
import subprocess
import sys
import traceback
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, List, Optional, Sequence, TextIO


PORT_BIN_DIR = Path("/rots/dev-coding4810/bin")
KEPT_BACKUPS = 7
KEPT_RUN_LOGS = 14
# Enough headroom for an incremental build plus the staged binary and its backup.
MIN_FREE_BYTES = 2 * 1024 ** 3
# A -j1 build of both targets takes about 5 minutes on the server; the limits only stop a hung step.
GIT_TIMEOUT_SECONDS = 10 * 60
BUILD_TIMEOUT_SECONDS = 2 * 60 * 60
TEST_TIMEOUT_SECONDS = 20 * 60
FAILURE_TAIL_LINES = 40
STATUS_LINES = 5
STAMP_FORMAT = "%Y%m%d_%H%M%S"
CONFIG_KEYS = {"remote_url", "branch", "token_file"}
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
    """The source a run fetches: a repository URL, a branch in it, and the token file that unlocks it."""

    remote_url: str
    branch: str
    token_file: Optional[Path]  # None for a repository that needs no credentials

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
        token_value = data.get("token_file")
        if token_value is None:
            return NightlyConfig(remote_url, branch, None)
        if not isinstance(token_value, str) or not token_value:
            raise NightlyError("config", "token_file must be a non-empty string when present")
        if not GITHUB_URL_PATTERN.match(remote_url):
            raise NightlyError("config", f"remote_url {remote_url!r} must be an https://github.com/<owner>/<repo> "
                                         "URL when token_file is set")
        token_file = Path(token_value).expanduser()
        if not os.access(token_file, os.R_OK):
            raise NightlyError("config", f"token file {token_file} is missing or unreadable")
        return NightlyConfig(remote_url, branch, token_file)


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
        """Writes the state through a temporary file and a rename, so a crash never leaves it half-written."""
        temporary = path.with_name(path.name + ".tmp")
        temporary.write_text(json.dumps(dataclasses.asdict(self), indent=2) + "\n")
        os.replace(temporary, path)


class CommandRunner:
    """Runs one run's external commands, appending each command line and its output to the run log."""

    def __init__(self, run_log: TextIO):
        # Caller-owned; stays open across every run() call.
        self.run_log = run_log

    def run(self, args: Sequence[object], step: str, timeout: int, cwd: Optional[Path] = None,
            env: Optional[dict] = None) -> str:
        """Runs `args` to completion and returns its combined stdout and stderr.

        Raises CommandFailed(step) when the command exits non-zero, cannot start, or runs longer than
        `timeout` seconds. `env` replaces the whole environment when given.
        """
        command = [str(arg) for arg in args]
        program_name = Path(command[0]).name
        self.run_log.write(f"$ {shlex.join(command)}\n")
        self.run_log.flush()
        try:
            result = subprocess.run(command, cwd=cwd, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                    text=True, errors="replace", timeout=timeout)
        except subprocess.TimeoutExpired as expired:
            # TimeoutExpired carries bytes even when text=True was asked for.
            partial_output = expired.stdout or b""
            if isinstance(partial_output, bytes):
                partial_output = partial_output.decode(errors="replace")
            self.run_log.write(partial_output)
            raise CommandFailed(step, f"{program_name} timed out after {timeout} s", partial_output)
        except OSError as error:
            raise CommandFailed(step, f"cannot run {command[0]}: {error.strerror}", "")
        self.run_log.write(result.stdout)
        self.run_log.flush()
        if result.returncode != 0:
            raise CommandFailed(step, f"{program_name} exited with status {result.returncode}", result.stdout)
        return result.stdout


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


def sha256_of(path: Path) -> str:
    """The SHA-256 of the file at `path`, as lowercase hex."""
    digest = hashlib.sha256()
    with path.open("rb") as binary_file:
        for chunk in iter(lambda: binary_file.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


class BinaryInstaller:
    """Swaps new builds into the port's bin/ageland and prunes the backups those swaps leave."""

    def __init__(self, port_bin: Path):
        # The port's bin/ that install() swaps binaries into.
        self.port_bin = port_bin

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

    def install(self, new_binary: Path, replaced_commit: Optional[str], stamp: str) -> str:
        """Installs `new_binary` as bin/ageland (mode 0775) and returns the installed file's SHA-256.

        The binary being replaced is kept as ageland.bak.<stamp>.nightly-<sha7 of replaced_commit>, or as
        ageland.bak.<stamp>.before-nightly when `replaced_commit` is None. bin/ageland exists at every moment,
        so autorun can never start a missing binary. Raises OSError when the copy, the backup or the swap
        fails, and bin/ageland is then unchanged.
        """
        staged = self.port_bin / "ageland.nightly-new"
        shutil.copyfile(new_binary, staged)
        os.chmod(staged, 0o775)
        # Taken before the swap, so nothing after the swap can fail and leave the install unrecorded.
        installed_checksum = sha256_of(staged)
        if self.current.exists():
            if replaced_commit is None:
                backup_name = f"ageland.bak.{stamp}.before-nightly"
            else:
                backup_name = f"ageland.bak.{stamp}.nightly-{replaced_commit[:7]}"
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
        return installed_checksum

    def prune_backups(self) -> List[Path]:
        """Deletes all but the newest KEPT_BACKUPS nightly backups and returns the paths deleted.

        Manual backups and the before-nightly backup never match and are never touched.
        """
        # The timestamp follows a fixed prefix, so name order is age order.
        backups = sorted(self.port_bin.glob(BACKUP_GLOB), reverse=True)
        removed = backups[KEPT_BACKUPS:]
        for backup in removed:
            backup.unlink()
        return removed


# GoogleTest's "[  FAILED  ] Suite.Name" lines; requiring a dot skips its "[  FAILED  ] 2 tests, listed below" line.
FAILED_TEST_PATTERN = re.compile(r"^\[  FAILED  \] (\S+\.\S+)", re.MULTILINE)


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


def run_unit_tests(runner: CommandRunner, paths: NightlyPaths) -> None:
    """Runs the unit-test binary from the clone's root, as CI does.

    Raises NightlyError("unit tests") naming the failed tests, or with the command's own reason when the
    output names none (a crash or a timeout).
    """
    try:
        runner.run([paths.built_tests], "unit tests", TEST_TIMEOUT_SECONDS, cwd=paths.clone)
    except CommandFailed as failure:
        failed_names = sorted(set(FAILED_TEST_PATTERN.findall(failure.output)))
        if not failed_names:
            raise
        raise NightlyError("unit tests", f"{len(failed_names)} failed: {', '.join(failed_names)}", failure.detail)


def check_free_space(home: Path) -> None:
    """Raises NightlyError("disk") when the filesystem holding `home` has less than MIN_FREE_BYTES free."""
    free_bytes = shutil.disk_usage(home).free
    if free_bytes < MIN_FREE_BYTES:
        raise NightlyError("disk", f"only {free_bytes // 1024 ** 2} MB free under {home}; "
                                   f"a run needs {MIN_FREE_BYTES // 1024 ** 2} MB")


def nightly_run(config: NightlyConfig, commit: str, paths: NightlyPaths, runner: CommandRunner,
                clone: SourceClone, installer: BinaryInstaller, dry_run: bool,
                now: datetime.datetime) -> RunOutcome:
    """Decides what to do with the fetched `commit`, then checks out, builds, tests and installs it.

    A commit already installed is skipped (recording a changed source unless `dry_run`). A dry run stops
    after the replaceability check, building and installing nothing. Raises NightlyError naming the failed step; the
    port's bin/ageland changes only when every earlier step succeeded.
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
    build(runner, paths)
    run_unit_tests(runner, paths)
    replaced_commit = None
    if state is not None:
        replaced_commit = state.commit
    stamp = now.strftime(STAMP_FORMAT)
    # Checked again here because someone may have deployed by hand while the build and tests ran.
    installer.check_replaceable(state)
    try:
        checksum = installer.install(paths.built_server, replaced_commit, stamp)
    except OSError as error:
        raise NightlyError("install", f"could not install into {paths.port_bin}: {error}")
    installed_at = now.isoformat(timespec="seconds")
    new_state = InstalledState(commit, config.remote_url, config.branch, checksum, installed_at)
    new_state.save(paths.state)
    installer.prune_backups()
    return RunOutcome(commit, "installed")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="nightly_deploy.py",
                                     description="Build a configured branch and install it into the coders port.")
    modes = parser.add_mutually_exclusive_group()
    modes.add_argument("--dry-run", action="store_true",
                       help="fetch and report what a run would do; build and install nothing")
    modes.add_argument("--status", action="store_true", help="print the installed build and the last few runs")
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
    """The installed build and the last STATUS_LINES summary lines, for --status."""
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
    if paths.summary_log.exists():
        recent = paths.summary_log.read_text().splitlines()[-STATUS_LINES:]
        lines.append("Recent runs:")
        lines.extend("  " + entry for entry in recent)
    else:
        lines.append("No runs recorded yet")
    return "\n".join(lines)


def run_logged(dry_run: bool, paths: NightlyPaths, run_log_path: Path, started: datetime.datetime) -> int:
    """Runs one fetch-to-install pass with its output in `run_log_path`, then records the summary line.

    Returns 0 for an install, a skip or a dry run, and 1 for any failure. Every failure, expected or not,
    is recorded in deploy.log, because nobody reads the output of a cron run; the line is also printed for
    runs started by hand.
    """
    config: Optional[NightlyConfig] = None
    commit: Optional[str] = None
    with run_log_path.open("w") as run_log:
        runner = CommandRunner(run_log)
        try:
            config = NightlyConfig.load(paths.config)
            clone = SourceClone(runner, paths.clone)
            commit = clone.fetch(config)
            installer = BinaryInstaller(paths.port_bin)
            outcome = nightly_run(config, commit, paths, runner, clone, installer, dry_run, started)
            result = outcome.message
            exit_status = 0
        except NightlyError as error:
            result = f"failed: {error.step}: {error.reason}"
            run_log.write(f"\nFAILED at {error.step}: {error.reason}\n")
            if error.detail:
                run_log.write(error.detail + "\n")
            exit_status = 1
        except Exception:
            result = "failed: unexpected error; see the run log"
            run_log.write("\n" + traceback.format_exc())
            exit_status = 1
    line = summary_line(started, config, commit, result) + f"  (log: runs/{run_log_path.name})"
    append_summary(paths, line)
    print(line)
    return exit_status


def main(argv: Optional[Sequence[str]] = None, paths: Optional[NightlyPaths] = None,
         now: Callable[[], datetime.datetime] = datetime.datetime.now) -> int:
    """Runs the command line and returns the process exit status.

    `paths` defaults to ~/nightly and the coders port; tests pass their own, and a clock through `now`.
    """
    arguments = build_parser().parse_args(argv)
    if paths is None:
        paths = NightlyPaths(Path.home() / "nightly", PORT_BIN_DIR)
    if arguments.status:
        print(status_report(paths))
        return 0
    paths.run_logs.mkdir(parents=True, exist_ok=True)
    started = now()
    with paths.lock.open("a") as lock_file:
        try:
            fcntl.flock(lock_file, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            append_summary(paths, summary_line(started, None, None, "skipped: another run holds the lock"))
            return 0
        # Children inherit the limit, so a crashing test or build tool leaves no core file behind.
        resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
        run_log_name = started.strftime(STAMP_FORMAT)
        if arguments.dry_run:
            run_log_name += "-dry-run"
        exit_status = run_logged(arguments.dry_run, paths, paths.run_logs / f"{run_log_name}.log", started)
        prune_run_logs(paths.run_logs)
        return exit_status


if __name__ == "__main__":
    sys.exit(main())
