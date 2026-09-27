#!/usr/bin/env python3

import contextlib
import datetime
import fcntl
import hashlib
import importlib.util
import io
import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from typing import Callable, List, Optional, Tuple
from unittest import mock


MODULE_PATH = Path(__file__).resolve().parent / "nightly_deploy.py"
SPEC = importlib.util.spec_from_file_location("nightly_deploy", MODULE_PATH)
nightly = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
sys.modules["nightly_deploy"] = nightly
sys.dont_write_bytecode = True
SPEC.loader.exec_module(nightly)

GITHUB_URL = "https://github.com/returnoftheshadow/RotS_Live.git"
FIXED_NOW = datetime.datetime(2026, 9, 27, 1, 30, 5)


class TempDirTestCase(unittest.TestCase):
    """Gives each test a fresh temporary directory that is removed afterwards."""

    def setUp(self) -> None:
        temp_dir = tempfile.TemporaryDirectory()
        self.addCleanup(temp_dir.cleanup)
        # Resolved so paths compare equal on macOS, where the temporary directory sits behind a symlink.
        self.root = Path(temp_dir.name).resolve()


# ---------------------------------------------------------------------------------------------
# Config and state
# ---------------------------------------------------------------------------------------------


class ConfigTest(TempDirTestCase):
    def setUp(self) -> None:
        super().setUp()
        # A readable token file for configs that name one.
        self.token_file = self.root / "github-token"
        self.token_file.write_text("github_pat_example\n")
        # Where each test writes the config it loads.
        self.config_path = self.root / "config.json"

    def load(self, **fields: object) -> "nightly.NightlyConfig":
        self.config_path.write_text(json.dumps(fields))
        return nightly.NightlyConfig.load(self.config_path)

    def assert_config_error(self, expected_text: str, **fields: object) -> None:
        with self.assertRaises(nightly.NightlyError) as caught:
            self.load(**fields)
        self.assertEqual(caught.exception.step, "config")
        self.assertIn(expected_text, caught.exception.reason)

    def test_github_source_with_token_loads(self) -> None:
        config = self.load(remote_url=GITHUB_URL, branch="fix/spell-room-affect-uaf-port",
                           token_file=str(self.token_file))

        expected = nightly.NightlyConfig(GITHUB_URL, "fix/spell-room-affect-uaf-port", self.token_file)
        self.assertEqual(config, expected)

    def test_token_file_tilde_is_expanded(self) -> None:
        with mock.patch.dict(os.environ, {"HOME": str(self.root)}):
            config = self.load(remote_url=GITHUB_URL, branch="main", token_file="~/github-token")

        self.assertEqual(config.token_file, self.token_file)

    def test_source_without_token_may_be_any_url(self) -> None:
        config = self.load(remote_url="file:///srv/RotS_Live.git", branch="main")

        self.assertIsNone(config.token_file)

    def test_token_is_only_offered_to_github(self) -> None:
        self.assert_config_error("must be an https://github.com/", remote_url="https://example.org/RotS_Live.git",
                                 branch="main", token_file=str(self.token_file))

    def test_missing_token_file_is_named(self) -> None:
        missing = self.root / "absent-token"

        self.assert_config_error(f"token file {missing} is missing or unreadable", remote_url=GITHUB_URL,
                                 branch="main", token_file=str(missing))

    def test_unknown_key_is_refused(self) -> None:
        self.assert_config_error("unknown keys", remote_url=GITHUB_URL, branch="main", brnach="other")

    def test_missing_remote_url_is_refused(self) -> None:
        self.assert_config_error("remote_url must be a non-empty string", branch="main")

    def test_invalid_branch_names_are_refused(self) -> None:
        for branch in ("", "a..b", "-rf", "ends.lock", "has space", 7):
            with self.subTest(branch=branch):
                self.assert_config_error("is not a valid branch name", remote_url="file:///srv/repo.git",
                                         branch=branch)

    def test_invalid_json_is_refused(self) -> None:
        self.config_path.write_text("{")

        with self.assertRaises(nightly.NightlyError) as caught:
            nightly.NightlyConfig.load(self.config_path)

        self.assertIn("is not valid JSON", caught.exception.reason)

    def test_non_object_json_is_refused(self) -> None:
        self.config_path.write_text("[]")

        with self.assertRaises(nightly.NightlyError) as caught:
            nightly.NightlyConfig.load(self.config_path)

        self.assertIn("must hold a JSON object", caught.exception.reason)

    def test_missing_config_file_is_refused(self) -> None:
        with self.assertRaises(nightly.NightlyError) as caught:
            nightly.NightlyConfig.load(self.root / "absent.json")

        self.assertEqual(caught.exception.step, "config")
        self.assertIn("cannot read", caught.exception.reason)


class StateTest(TempDirTestCase):
    def test_saved_state_loads_back(self) -> None:
        state_path = self.root / "state.json"
        state = nightly.InstalledState("a" * 40, GITHUB_URL, "main", "b" * 64, "2026-09-27T01:30:05")

        state.save(state_path)

        self.assertEqual(nightly.InstalledState.load(state_path), state)
        self.assertEqual(sorted(path.name for path in self.root.iterdir()), ["state.json"])

    def test_missing_state_means_nothing_installed(self) -> None:
        self.assertIsNone(nightly.InstalledState.load(self.root / "state.json"))

    def test_malformed_state_says_how_to_recover(self) -> None:
        state_path = self.root / "state.json"
        state_path.write_text('{"commit": "abc"}')

        with self.assertRaises(nightly.NightlyError) as caught:
            nightly.InstalledState.load(state_path)

        self.assertEqual(caught.exception.step, "state")
        self.assertIn("delete it to adopt the current bin/ageland", caught.exception.reason)


class TailLinesTest(unittest.TestCase):
    def test_keeps_the_last_lines(self) -> None:
        self.assertEqual(nightly.tail_lines("one\ntwo\nthree\n", 2), "two\nthree")

    def test_command_failed_keeps_full_output_and_a_tail(self) -> None:
        output = "".join(f"line {number}\n" for number in range(100))

        failure = nightly.CommandFailed("build", "cmake exited with status 2", output)

        self.assertEqual(failure.output, output)
        self.assertEqual(failure.detail.splitlines()[0], "line 60")
        self.assertEqual(len(failure.detail.splitlines()), nightly.FAILURE_TAIL_LINES)


# ---------------------------------------------------------------------------------------------
# Commands and git
# ---------------------------------------------------------------------------------------------


def git(*args: str) -> str:
    """Runs a test-setup git command and returns its stripped stdout; a failure fails the test."""
    result = subprocess.run(["git", *args], capture_output=True, text=True, check=True)
    return result.stdout.strip()


class UpstreamRepo:
    """A bare repository standing in for GitHub, with a scratch work tree that pushes commits to it."""

    def __init__(self, root: Path, name: str):
        # The repository a SourceClone fetches from, addressed by its file:// URL.
        self.bare = root / f"{name}.git"
        # Where test commits are made before being pushed to the bare repository.
        self.work = root / f"{name}-work"
        git("init", "--quiet", "--bare", str(self.bare))
        git("init", "--quiet", str(self.work))
        for key, value in (("user.email", "tests@example.org"), ("user.name", "Tests"),
                           ("commit.gpgsign", "false"), ("core.hooksPath", "/dev/null")):
            git("-C", str(self.work), "config", key, value)
        git("-C", str(self.work), "remote", "add", "origin", str(self.bare))

    @property
    def url(self) -> str:
        return self.bare.as_uri()

    def commit(self, branch: str, content: str) -> str:
        """Points `branch` at the work tree's HEAD (resetting it if it exists), then commits `content`, pushes it,
        and returns the new SHA."""
        git("-C", str(self.work), "checkout", "--quiet", "-B", branch)
        return self._commit_and_push(branch, content, force=False)

    def rewrite(self, branch: str, content: str) -> str:
        """Replaces the tip of `branch` with a different commit and force-pushes it, as a rebase would."""
        git("-C", str(self.work), "checkout", "--quiet", branch)
        git("-C", str(self.work), "reset", "--quiet", "--hard", "HEAD~1")
        return self._commit_and_push(branch, content, force=True)

    def _commit_and_push(self, branch: str, content: str, force: bool) -> str:
        (self.work / "file.txt").write_text(content)
        git("-C", str(self.work), "add", "file.txt")
        git("-C", str(self.work), "commit", "--quiet", "-m", content)
        push_args = ["-C", str(self.work), "push", "--quiet"]
        if force:
            push_args.append("--force")
        git(*push_args, "origin", f"HEAD:refs/heads/{branch}")
        return git("-C", str(self.work), "rev-parse", "HEAD")


class CommandRunnerTest(unittest.TestCase):
    def setUp(self) -> None:
        # Stands in for the run log file.
        self.run_log = io.StringIO()
        # The runner under test, logging into run_log.
        self.runner = nightly.CommandRunner(self.run_log)

    def test_returns_combined_output_and_logs_the_command(self) -> None:
        script = "import sys; print('to stdout'); print('to stderr', file=sys.stderr)"

        output = self.runner.run([sys.executable, "-c", script], "probe", 30)

        self.assertIn("to stdout", output)
        self.assertIn("to stderr", output)
        logged = self.run_log.getvalue()
        self.assertIn("$ ", logged)
        self.assertIn("to stdout", logged)

    def test_non_zero_exit_raises_with_step_status_and_output(self) -> None:
        script = "print('boom'); raise SystemExit(3)"

        with self.assertRaises(nightly.CommandFailed) as caught:
            self.runner.run([sys.executable, "-c", script], "build", 30)

        self.assertEqual(caught.exception.step, "build")
        self.assertIn("exited with status 3", caught.exception.reason)
        self.assertIn("boom", caught.exception.output)

    def test_timeout_raises(self) -> None:
        with self.assertRaises(nightly.CommandFailed) as caught:
            self.runner.run([sys.executable, "-c", "import time; time.sleep(10)"], "unit tests", 1)

        self.assertEqual(caught.exception.step, "unit tests")
        self.assertIn("timed out after 1 s", caught.exception.reason)

    def test_missing_program_raises(self) -> None:
        with self.assertRaises(nightly.CommandFailed) as caught:
            self.runner.run(["/nonexistent/program"], "build", 30)

        self.assertIn("cannot run /nonexistent/program", caught.exception.reason)


class SourceCloneTest(TempDirTestCase):
    def setUp(self) -> None:
        super().setUp()
        # The default source every test fetches from.
        self.upstream = UpstreamRepo(self.root, "upstream")
        # Collects every command the clone runs, for assertions about what was logged.
        self.run_log = io.StringIO()
        # The clone under test, created by its first fetch.
        self.clone = nightly.SourceClone(nightly.CommandRunner(self.run_log), self.root / "clone")

    def config_for(self, repo: UpstreamRepo, branch: str,
                   token_file: Optional[Path] = None) -> "nightly.NightlyConfig":
        return nightly.NightlyConfig(repo.url, branch, token_file)

    def test_first_fetch_creates_the_clone_and_returns_the_tip(self) -> None:
        tip = self.upstream.commit("fix/uaf-port", "first")

        fetched = self.clone.fetch(self.config_for(self.upstream, "fix/uaf-port"))

        self.assertEqual(fetched, tip)
        self.assertTrue((self.root / "clone" / ".git").is_dir())

    def test_later_fetch_returns_the_new_tip(self) -> None:
        self.upstream.commit("main", "first")
        config = self.config_for(self.upstream, "main")
        self.clone.fetch(config)
        second = self.upstream.commit("main", "second")

        self.assertEqual(self.clone.fetch(config), second)

    def test_force_pushed_branch_fetches_the_new_tip(self) -> None:
        self.upstream.commit("main", "first")
        self.upstream.commit("main", "second")
        config = self.config_for(self.upstream, "main")
        self.clone.fetch(config)
        rewritten = self.upstream.rewrite("main", "rebased second")

        self.assertEqual(self.clone.fetch(config), rewritten)

    def test_changing_the_remote_url_fetches_from_the_new_source(self) -> None:
        self.upstream.commit("main", "upstream work")
        self.clone.fetch(self.config_for(self.upstream, "main"))
        fork = UpstreamRepo(self.root, "fork")
        fork_tip = fork.commit("main", "fork work")

        self.assertEqual(self.clone.fetch(self.config_for(fork, "main")), fork_tip)
        self.assertEqual(git("-C", str(self.root / "clone"), "remote", "get-url", "origin"), fork.url)

    def test_missing_branch_is_reported_as_not_found(self) -> None:
        self.upstream.commit("main", "first")

        with self.assertRaises(nightly.NightlyError) as caught:
            self.clone.fetch(self.config_for(self.upstream, "deleted-branch"))

        self.assertEqual(caught.exception.step, "fetch")
        self.assertIn("branch deleted-branch was not found", caught.exception.reason)

    def test_check_out_detaches_at_the_commit(self) -> None:
        first = self.upstream.commit("main", "first")
        self.upstream.commit("main", "second")
        self.clone.fetch(self.config_for(self.upstream, "main"))

        self.clone.check_out(first)

        clone_dir = str(self.root / "clone")
        self.assertEqual(git("-C", clone_dir, "rev-parse", "HEAD"), first)
        detached = subprocess.run(["git", "-C", clone_dir, "symbolic-ref", "-q", "HEAD"], capture_output=True)
        self.assertNotEqual(detached.returncode, 0)

    def test_token_never_appears_in_the_run_log(self) -> None:
        token_file = self.root / "github-token"
        token_file.write_text("github_pat_secret_value\n")
        self.upstream.commit("main", "first")

        self.clone.fetch(self.config_for(self.upstream, "main", token_file))

        logged = self.run_log.getvalue()
        self.assertIn("credential.helper", logged)
        self.assertNotIn("github_pat_secret_value", logged)


class CredentialHelperTest(TempDirTestCase):
    def run_helper(self, token_file: Path, operation: str) -> str:
        helper = nightly.credential_helper(token_file)
        self.assertTrue(helper.startswith("!"))
        # git runs the text after '!' through the shell with the operation appended.
        result = subprocess.run(["sh", "-c", helper[1:] + " " + operation], capture_output=True, text=True,
                                check=True)
        return result.stdout

    def test_get_answers_with_the_token_from_the_file(self) -> None:
        token_file = self.root / "token with space"
        token_file.write_text("github_pat_value\n")

        answer = self.run_helper(token_file, "get")

        self.assertEqual(answer, "username=x-access-token\npassword=github_pat_value\n")

    def test_other_operations_answer_nothing(self) -> None:
        token_file = self.root / "token"
        token_file.write_text("github_pat_value\n")

        self.assertEqual(self.run_helper(token_file, "store"), "")

    def test_helper_names_the_file_not_the_token(self) -> None:
        token_file = self.root / "token"
        token_file.write_text("github_pat_value\n")

        self.assertNotIn("github_pat_value", nightly.credential_helper(token_file))


class DescribeFetchFailureTest(unittest.TestCase):
    def test_authentication_failure_points_at_the_token(self) -> None:
        failure = nightly.CommandFailed("fetch", "git exited with status 128",
                                        "remote: Invalid username or token.\nfatal: Authentication failed for ...\n")

        reason = nightly.describe_fetch_failure(failure, "main")

        self.assertIn("GitHub refused the token", reason)

    def test_repository_hidden_from_the_token_points_at_the_token(self) -> None:
        # GitHub answers 404, not 401 or 403, when a token cannot see a private repository.
        failure = nightly.CommandFailed("fetch", "git exited with status 128",
                                        "remote: Repository not found.\n"
                                        "fatal: repository 'https://github.com/owner/repo.git/' not found\n")

        reason = nightly.describe_fetch_failure(failure, "main")

        self.assertIn("repository not found, or the token cannot see it", reason)

    def test_other_failures_keep_the_command_reason(self) -> None:
        failure = nightly.CommandFailed("fetch", "git exited with status 128",
                                        "fatal: unable to access: Could not resolve host\n")

        self.assertEqual(nightly.describe_fetch_failure(failure, "main"), "git exited with status 128")


# ---------------------------------------------------------------------------------------------
# Installing the binary
# ---------------------------------------------------------------------------------------------


def sha256_bytes(content: bytes) -> str:
    return hashlib.sha256(content).hexdigest()


class BinaryInstallerTest(TempDirTestCase):
    def setUp(self) -> None:
        super().setUp()
        # Stands in for PORT_BIN_DIR.
        self.port_bin = self.root / "bin"
        self.port_bin.mkdir()
        # The binary a run built, waiting to be installed.
        self.new_binary = self.root / "new-ageland"
        self.new_binary.write_bytes(b"new build")
        # The installer under test, pointed at port_bin.
        self.installer = nightly.BinaryInstaller(self.port_bin)

    def write_current(self, content: bytes) -> Path:
        current = self.port_bin / "ageland"
        current.write_bytes(content)
        return current

    def state_with_checksum(self, checksum: str) -> "nightly.InstalledState":
        return nightly.InstalledState("c" * 40, GITHUB_URL, "main", checksum, "2026-09-26T01:30:05")

    def test_install_replaces_the_binary_and_returns_its_checksum(self) -> None:
        self.write_current(b"old build")

        checksum = self.installer.install(self.new_binary, "abcdef1234567890", "20260927_013005")

        current = self.port_bin / "ageland"
        self.assertEqual(current.read_bytes(), b"new build")
        self.assertEqual(checksum, sha256_bytes(b"new build"))
        self.assertEqual(current.stat().st_mode & 0o777, 0o775)

    def test_old_binary_is_kept_as_a_hard_linked_nightly_backup(self) -> None:
        old_inode = self.write_current(b"old build").stat().st_ino

        self.installer.install(self.new_binary, "abcdef1234567890", "20260927_013005")

        backup = self.port_bin / "ageland.bak.20260927_013005.nightly-abcdef1"
        self.assertEqual(backup.read_bytes(), b"old build")
        self.assertEqual(backup.stat().st_ino, old_inode)

    def test_first_install_backup_is_kept_out_of_pruning(self) -> None:
        self.write_current(b"manual build")

        self.installer.install(self.new_binary, None, "20260927_013005")

        self.assertTrue((self.port_bin / "ageland.bak.20260927_013005.before-nightly").exists())
        self.assertEqual(list(self.port_bin.glob(nightly.BACKUP_GLOB)), [])

    def test_install_without_a_current_binary_makes_no_backup(self) -> None:
        self.installer.install(self.new_binary, None, "20260927_013005")

        self.assertEqual(sorted(path.name for path in self.port_bin.iterdir()), ["ageland"])

    def test_ageland_exists_at_the_moment_of_the_swap(self) -> None:
        self.write_current(b"old build")
        real_replace = os.replace
        present_during_swap: List[bool] = []

        def checking_replace(source: object, destination: object) -> None:
            present_during_swap.append((self.port_bin / "ageland").exists())
            real_replace(source, destination)

        with mock.patch.object(nightly.os, "replace", side_effect=checking_replace):
            self.installer.install(self.new_binary, "abcdef1234567890", "20260927_013005")

        self.assertEqual(present_during_swap, [True])

    def test_leftover_staged_file_is_overwritten(self) -> None:
        self.write_current(b"old build")
        (self.port_bin / "ageland.nightly-new").write_bytes(b"half-copied from a crashed run")

        self.installer.install(self.new_binary, "abcdef1234567890", "20260927_013005")

        self.assertEqual((self.port_bin / "ageland").read_bytes(), b"new build")
        self.assertFalse((self.port_bin / "ageland.nightly-new").exists())

    def test_refused_hard_link_falls_back_to_a_copied_backup(self) -> None:
        # Linux's protected_hardlinks refuses to link another user's file that the job cannot write.
        self.write_current(b"a teammate's build")

        with mock.patch.object(nightly.os, "link", side_effect=PermissionError(1, "Operation not permitted")):
            checksum = self.installer.install(self.new_binary, None, "20260927_013005")

        backup = self.port_bin / "ageland.bak.20260927_013005.before-nightly"
        self.assertEqual(backup.read_bytes(), b"a teammate's build")
        self.assertEqual((self.port_bin / "ageland").read_bytes(), b"new build")
        self.assertEqual(checksum, sha256_bytes(b"new build"))

    def test_nothing_installed_yet_is_replaceable(self) -> None:
        self.write_current(b"manual build")

        self.installer.check_replaceable(None)

    def test_the_binary_last_installed_is_replaceable(self) -> None:
        self.write_current(b"nightly build")

        self.installer.check_replaceable(self.state_with_checksum(sha256_bytes(b"nightly build")))

    def test_a_missing_binary_is_replaceable(self) -> None:
        self.installer.check_replaceable(self.state_with_checksum(sha256_bytes(b"nightly build")))

    def test_a_manual_deploy_is_refused(self) -> None:
        self.write_current(b"someone else's build")

        with self.assertRaises(nightly.NightlyError) as caught:
            self.installer.check_replaceable(self.state_with_checksum(sha256_bytes(b"nightly build")))

        self.assertEqual(caught.exception.step, "install")
        self.assertIn("bin/ageland was replaced since ccccccc", caught.exception.reason)
        self.assertIn("delete state.json", caught.exception.reason)

    def test_prune_keeps_the_newest_nightly_backups_only(self) -> None:
        nightly_backups = [f"ageland.bak.202609{day:02d}_013005.nightly-abc{day:04d}" for day in range(1, 11)]
        other_files = ["ageland", "ageland~", "ageland.bak", "ageland.bak.20260629_001923",
                       "ageland.bak.20260101_000000.before-nightly"]
        for name in nightly_backups + other_files:
            (self.port_bin / name).write_bytes(b"x")

        removed = self.installer.prune_backups()

        self.assertEqual(sorted(path.name for path in removed), nightly_backups[:3])
        remaining = sorted(path.name for path in self.port_bin.iterdir())
        self.assertEqual(remaining, sorted(nightly_backups[3:] + other_files))


# ---------------------------------------------------------------------------------------------
# One run
# ---------------------------------------------------------------------------------------------


class FakeRunner:
    """Records each command; the build step writes the binaries, and a scripted step can fail."""

    def __init__(self, paths: "nightly.NightlyPaths", fail_step: Optional[str] = None, failure_output: str = "",
                 creates_binaries: bool = True, during_build: Optional[Callable[[], None]] = None):
        # Where the build step writes the fake binaries.
        self.paths = paths
        # The step whose command raises CommandFailed, or None for a clean run.
        self.fail_step = fail_step
        # What the failing command "printed".
        self.failure_output = failure_output
        # False simulates a build that exits 0 without producing bin/ageland.
        self.creates_binaries = creates_binaries
        # Called while the build "runs", to simulate something else changing the port meanwhile.
        self.during_build = during_build
        # (step, command) for every command run, in order.
        self.commands: List[Tuple[str, List[str]]] = []

    def run(self, args: List[object], step: str, timeout: int, cwd: Optional[Path] = None,
            env: Optional[dict] = None) -> str:
        self.commands.append((step, [str(arg) for arg in args]))
        if step == self.fail_step:
            raise nightly.CommandFailed(step, f"{step} command exited with status 1", self.failure_output)
        if step == "build" and self.creates_binaries:
            self.paths.built_server.parent.mkdir(parents=True, exist_ok=True)
            self.paths.built_server.write_bytes(b"new build")
            self.paths.built_tests.write_bytes(b"tests")
        if step == "build" and self.during_build is not None:
            self.during_build()
        return ""

    def steps(self) -> List[str]:
        return [step for step, _command in self.commands]


class FakeClone:
    """Stands in for SourceClone once the commit is known; records the commit checked out."""

    def __init__(self) -> None:
        # The commit passed to check_out(), or None when nothing was checked out.
        self.checked_out: Optional[str] = None

    def check_out(self, commit: str) -> None:
        self.checked_out = commit


NEW_COMMIT = "1234567" + "0" * 33
OLD_COMMIT = "abcdef1" + "0" * 33


class NightlyRunTest(TempDirTestCase):
    def setUp(self) -> None:
        super().setUp()
        # A throwaway home and port bin/ that the run reads and installs into.
        self.paths = nightly.NightlyPaths(self.root / "nightly", self.root / "port-bin")
        self.paths.home.mkdir()
        self.paths.port_bin.mkdir()
        (self.paths.port_bin / "ageland").write_bytes(b"old nightly build")
        # The configured source for most tests.
        self.config = nightly.NightlyConfig(GITHUB_URL, "fix/uaf-port", None)
        # Records the checkout that nightly_run requests.
        self.clone = FakeClone()
        # A real installer over the temporary port bin/.
        self.installer = nightly.BinaryInstaller(self.paths.port_bin)
        # A fixed free-space reading, so the tests do not depend on the machine's disk.
        plenty_of_disk = mock.Mock(free=nightly.MIN_FREE_BYTES * 10)
        disk_patch = mock.patch.object(nightly.shutil, "disk_usage", return_value=plenty_of_disk)
        disk_patch.start()
        self.addCleanup(disk_patch.stop)

    def record_installed(self, commit: str, remote_url: str = GITHUB_URL, branch: str = "fix/uaf-port") -> None:
        checksum = sha256_bytes(b"old nightly build")
        nightly.InstalledState(commit, remote_url, branch, checksum, "2026-09-26T01:30:05").save(self.paths.state)

    def run_nightly(self, runner: FakeRunner, dry_run: bool = False) -> "nightly.RunOutcome":
        return nightly.nightly_run(self.config, NEW_COMMIT, self.paths, runner, self.clone, self.installer,
                                   dry_run, FIXED_NOW)

    def port_binary(self) -> bytes:
        return (self.paths.port_bin / "ageland").read_bytes()

    def test_new_commit_is_built_tested_and_installed(self) -> None:
        self.record_installed(OLD_COMMIT)
        runner = FakeRunner(self.paths)

        outcome = self.run_nightly(runner)

        self.assertEqual(outcome, nightly.RunOutcome(NEW_COMMIT, "installed"))
        self.assertEqual(self.clone.checked_out, NEW_COMMIT)
        self.assertEqual(runner.steps(), ["configure", "build", "unit tests"])
        self.assertEqual(self.port_binary(), b"new build")
        state = nightly.InstalledState.load(self.paths.state)
        expected_state = nightly.InstalledState(NEW_COMMIT, GITHUB_URL, "fix/uaf-port", sha256_bytes(b"new build"),
                                                "2026-09-27T01:30:05")
        self.assertEqual(state, expected_state)
        self.assertTrue((self.paths.port_bin / "ageland.bak.20260927_013005.nightly-abcdef1").exists())

    def test_build_is_niced_single_threaded_and_builds_both_targets(self) -> None:
        runner = FakeRunner(self.paths)

        self.run_nightly(runner)

        build_command = dict(runner.commands)["build"]
        self.assertEqual(build_command[:3], ["nice", "-n", "10"])
        self.assertIn("ageland_tests", build_command)
        self.assertEqual(build_command[-1], "-j1")
        test_command = dict(runner.commands)["unit tests"]
        self.assertEqual(test_command, [str(self.paths.built_tests)])

    def test_configure_runs_only_without_a_cmake_cache(self) -> None:
        self.paths.build.mkdir()
        (self.paths.build / "CMakeCache.txt").write_text("")
        runner = FakeRunner(self.paths)

        self.run_nightly(runner)

        self.assertEqual(runner.steps(), ["build", "unit tests"])

    def test_first_install_keeps_the_existing_binary_out_of_pruning(self) -> None:
        self.run_nightly(FakeRunner(self.paths))

        self.assertTrue((self.paths.port_bin / "ageland.bak.20260927_013005.before-nightly").exists())

    def test_installed_commit_is_skipped_without_building(self) -> None:
        self.record_installed(NEW_COMMIT)
        runner = FakeRunner(self.paths)

        outcome = self.run_nightly(runner)

        self.assertEqual(outcome.message, "skipped: already installed")
        self.assertEqual(runner.commands, [])
        self.assertEqual(self.port_binary(), b"old nightly build")

    def test_installed_commit_from_a_new_source_is_skipped_and_the_source_recorded(self) -> None:
        self.record_installed(NEW_COMMIT, branch="older-branch")

        outcome = self.run_nightly(FakeRunner(self.paths))

        self.assertEqual(outcome.message, "skipped: already installed")
        self.assertEqual(nightly.InstalledState.load(self.paths.state).branch, "fix/uaf-port")

    def test_dry_run_does_not_record_a_new_source(self) -> None:
        self.record_installed(NEW_COMMIT, branch="older-branch")

        self.run_nightly(FakeRunner(self.paths), dry_run=True)

        self.assertEqual(nightly.InstalledState.load(self.paths.state).branch, "older-branch")

    def test_dry_run_builds_and_installs_nothing(self) -> None:
        self.record_installed(OLD_COMMIT)
        runner = FakeRunner(self.paths)

        outcome = self.run_nightly(runner, dry_run=True)

        self.assertEqual(outcome.message, "dry run: would build, test and install")
        self.assertEqual(runner.commands, [])
        self.assertIsNone(self.clone.checked_out)
        self.assertEqual(self.port_binary(), b"old nightly build")
        self.assertEqual(nightly.InstalledState.load(self.paths.state).commit, OLD_COMMIT)

    def test_manual_deploy_is_refused_before_building(self) -> None:
        self.record_installed(OLD_COMMIT)
        (self.paths.port_bin / "ageland").write_bytes(b"someone else's build")
        runner = FakeRunner(self.paths)

        with self.assertRaises(nightly.NightlyError) as caught:
            self.run_nightly(runner)

        self.assertEqual(caught.exception.step, "install")
        self.assertEqual(runner.commands, [])

    def test_build_failure_leaves_the_port_and_state_alone(self) -> None:
        self.record_installed(OLD_COMMIT)
        runner = FakeRunner(self.paths, fail_step="build", failure_output="error: boom\n")

        with self.assertRaises(nightly.NightlyError) as caught:
            self.run_nightly(runner)

        self.assertEqual(caught.exception.step, "build")
        self.assertEqual(self.port_binary(), b"old nightly build")
        self.assertEqual(nightly.InstalledState.load(self.paths.state).commit, OLD_COMMIT)

    def test_build_that_produces_no_binary_fails(self) -> None:
        runner = FakeRunner(self.paths, creates_binaries=False)

        with self.assertRaises(nightly.NightlyError) as caught:
            self.run_nightly(runner)

        self.assertEqual(caught.exception.step, "build")
        self.assertIn("does not exist", caught.exception.reason)

    def test_failing_unit_tests_are_named(self) -> None:
        output = ("[  FAILED  ] PoisonTest.Stacks (3 ms)\n"
                  "[  FAILED  ] 2 tests, listed below:\n"
                  "[  FAILED  ] PoisonTest.Stacks\n"
                  "[  FAILED  ] FleeTest.FreedFleer\n")
        runner = FakeRunner(self.paths, fail_step="unit tests", failure_output=output)

        with self.assertRaises(nightly.NightlyError) as caught:
            self.run_nightly(runner)

        self.assertEqual(caught.exception.step, "unit tests")
        self.assertEqual(caught.exception.reason, "2 failed: FleeTest.FreedFleer, PoisonTest.Stacks")
        self.assertEqual(self.port_binary(), b"old nightly build")

    def test_manual_deploy_during_the_build_is_refused_at_the_swap(self) -> None:
        self.record_installed(OLD_COMMIT)
        teammate_binary = self.paths.port_bin / "ageland"
        runner = FakeRunner(self.paths, during_build=lambda: teammate_binary.write_bytes(b"a teammate's build"))

        with self.assertRaises(nightly.NightlyError) as caught:
            self.run_nightly(runner)

        self.assertEqual(caught.exception.step, "install")
        self.assertEqual(teammate_binary.read_bytes(), b"a teammate's build")
        self.assertEqual(list(self.paths.port_bin.glob("ageland.bak.*")), [])
        self.assertEqual(nightly.InstalledState.load(self.paths.state).commit, OLD_COMMIT)

    def test_crashing_unit_tests_keep_the_command_reason(self) -> None:
        runner = FakeRunner(self.paths, fail_step="unit tests", failure_output="Segmentation fault\n")

        with self.assertRaises(nightly.NightlyError) as caught:
            self.run_nightly(runner)

        self.assertEqual(caught.exception.reason, "unit tests command exited with status 1")

    def test_low_disk_stops_before_checking_out(self) -> None:
        low_disk = mock.Mock(free=1024)
        runner = FakeRunner(self.paths)

        with mock.patch.object(nightly.shutil, "disk_usage", return_value=low_disk):
            with self.assertRaises(nightly.NightlyError) as caught:
                self.run_nightly(runner)

        self.assertEqual(caught.exception.step, "disk")
        self.assertIsNone(self.clone.checked_out)
        self.assertEqual(runner.commands, [])


# ---------------------------------------------------------------------------------------------
# The command line
# ---------------------------------------------------------------------------------------------


def fake_build(runner: object, paths: "nightly.NightlyPaths") -> None:
    """Replaces the real CMake build in end-to-end tests: writes the binary a build would produce."""
    paths.built_server.parent.mkdir(parents=True, exist_ok=True)
    paths.built_server.write_bytes(b"built from the clone")


class MainTest(TempDirTestCase):
    def setUp(self) -> None:
        super().setUp()
        # A throwaway home and port bin/ that main() reads and installs into.
        self.paths = nightly.NightlyPaths(self.root / "nightly", self.root / "port-bin")
        self.paths.home.mkdir()
        self.paths.port_bin.mkdir()
        (self.paths.port_bin / "ageland").write_bytes(b"manual build")
        # A real local repository, so main() exercises a real fetch.
        self.upstream = UpstreamRepo(self.root, "upstream")
        # The branch tip each run is expected to fetch and install.
        self.tip = self.upstream.commit("fix/uaf-port", "work")
        # A fixed free-space reading, so the tests do not depend on the machine's disk.
        plenty_of_disk = mock.Mock(free=nightly.MIN_FREE_BYTES * 10)
        disk_patch = mock.patch.object(nightly.shutil, "disk_usage", return_value=plenty_of_disk)
        disk_patch.start()
        self.addCleanup(disk_patch.stop)

    def write_config(self) -> None:
        self.paths.config.write_text(json.dumps({"remote_url": self.upstream.url, "branch": "fix/uaf-port"}))

    def run_main(self, *argv: str) -> Tuple[int, str]:
        output = io.StringIO()
        with mock.patch.object(nightly, "build", side_effect=fake_build), \
                mock.patch.object(nightly, "run_unit_tests"), \
                contextlib.redirect_stdout(output):
            exit_status = nightly.main(list(argv), self.paths, lambda: FIXED_NOW)
        return exit_status, output.getvalue()

    def summary_lines(self) -> List[str]:
        return self.paths.summary_log.read_text().splitlines()

    def test_full_run_installs_and_writes_one_summary_line(self) -> None:
        self.write_config()

        exit_status, printed = self.run_main()

        self.assertEqual(exit_status, 0)
        self.assertEqual((self.paths.port_bin / "ageland").read_bytes(), b"built from the clone")
        expected = (f"2026-09-27 01:30:05  {self.upstream.url} fix/uaf-port  {self.tip[:7]}  installed"
                    "  (log: runs/20260927_013005.log)")
        self.assertEqual(self.summary_lines(), [expected])
        self.assertEqual(printed.strip(), expected)
        self.assertTrue((self.paths.run_logs / "20260927_013005.log").exists())

    def test_second_run_on_the_same_commit_skips(self) -> None:
        self.write_config()
        self.run_main()

        exit_status, _printed = self.run_main()

        self.assertEqual(exit_status, 0)
        self.assertIn("skipped: already installed", self.summary_lines()[-1])

    def test_dry_run_changes_nothing_and_names_its_log(self) -> None:
        self.write_config()

        exit_status, _printed = self.run_main("--dry-run")

        self.assertEqual(exit_status, 0)
        self.assertEqual((self.paths.port_bin / "ageland").read_bytes(), b"manual build")
        self.assertFalse(self.paths.state.exists())
        self.assertIn("dry run: would build, test and install", self.summary_lines()[-1])
        self.assertTrue((self.paths.run_logs / "20260927_013005-dry-run.log").exists())

    def test_failure_is_logged_with_its_step_and_exits_non_zero(self) -> None:
        exit_status, _printed = self.run_main()

        self.assertEqual(exit_status, 1)
        self.assertIn("  -  -  failed: config: cannot read", self.summary_lines()[-1])
        run_log = (self.paths.run_logs / "20260927_013005.log").read_text()
        self.assertIn("FAILED at config: cannot read", run_log)

    def test_deleted_branch_fails_at_fetch_and_leaves_the_port_alone(self) -> None:
        self.paths.config.write_text(json.dumps({"remote_url": self.upstream.url, "branch": "gone"}))

        exit_status, _printed = self.run_main()

        self.assertEqual(exit_status, 1)
        self.assertIn("failed: fetch: branch gone was not found", self.summary_lines()[-1])
        self.assertEqual((self.paths.port_bin / "ageland").read_bytes(), b"manual build")

    def test_unexpected_error_is_logged_with_its_traceback(self) -> None:
        self.write_config()

        with mock.patch.object(nightly, "nightly_run", side_effect=RuntimeError("boom")):
            exit_status, _printed = self.run_main()

        self.assertEqual(exit_status, 1)
        self.assertIn("failed: unexpected error; see the run log", self.summary_lines()[-1])
        run_log = (self.paths.run_logs / "20260927_013005.log").read_text()
        self.assertIn("RuntimeError: boom", run_log)

    def test_a_held_lock_skips_the_run(self) -> None:
        self.write_config()
        with self.paths.lock.open("a") as held_lock:
            fcntl.flock(held_lock, fcntl.LOCK_EX | fcntl.LOCK_NB)

            exit_status, _printed = self.run_main()

        self.assertEqual(exit_status, 0)
        self.assertIn("skipped: another run holds the lock", self.summary_lines()[-1])
        self.assertEqual((self.paths.port_bin / "ageland").read_bytes(), b"manual build")

    def test_run_logs_are_pruned_to_the_newest(self) -> None:
        self.paths.run_logs.mkdir()
        for minute in range(20):
            (self.paths.run_logs / f"20260901_00{minute:02d}00.log").write_text("")

        self.run_main()

        remaining = sorted(path.name for path in self.paths.run_logs.iterdir())
        self.assertEqual(len(remaining), nightly.KEPT_RUN_LOGS)
        self.assertEqual(remaining[-1], "20260927_013005.log")

    def test_status_shows_the_installed_build_and_recent_runs(self) -> None:
        nightly.InstalledState(self.tip, self.upstream.url, "fix/uaf-port", "0" * 64,
                               "2026-09-27T01:30:05").save(self.paths.state)
        self.paths.summary_log.write_text("".join(f"run {number}\n" for number in range(8)))

        exit_status, printed = self.run_main("--status")

        self.assertEqual(exit_status, 0)
        self.assertIn(f"Installed: {self.tip[:7]} from {self.upstream.url} fix/uaf-port at 2026-09-27T01:30:05",
                      printed)
        self.assertIn("run 7", printed)
        self.assertIn("run 3", printed)
        self.assertNotIn("run 2", printed)

    def test_status_with_nothing_installed(self) -> None:
        exit_status, printed = self.run_main("--status")

        self.assertEqual(exit_status, 0)
        self.assertIn("Installed: nothing yet", printed)
        self.assertIn("No runs recorded yet", printed)

    def test_dry_run_and_status_cannot_be_combined(self) -> None:
        with contextlib.redirect_stderr(io.StringIO()):
            with self.assertRaises(SystemExit):
                nightly.main(["--dry-run", "--status"], self.paths, lambda: FIXED_NOW)


if __name__ == "__main__":
    unittest.main()
