#!/usr/bin/env python3

import contextlib
import datetime
import fcntl
import hashlib
import importlib.util
import io
import json
import os
import shutil
import signal
import subprocess
import sys
import tarfile
import tempfile
import threading
import time
import unittest
from pathlib import Path
from typing import Callable, Dict, List, Optional, Tuple
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

    def test_gates_default_to_off(self) -> None:
        config = self.load(remote_url=GITHUB_URL, branch="main")

        self.assertFalse(config.integration_gate)
        self.assertFalse(config.smoke_gate)

    def test_gates_can_be_switched_on(self) -> None:
        config = self.load(remote_url=GITHUB_URL, branch="main", integration_gate=True, smoke_gate=True)

        self.assertTrue(config.integration_gate)
        self.assertTrue(config.smoke_gate)

    def test_gates_accept_only_true_or_false(self) -> None:
        for key in nightly.GATE_KEYS:
            for value in ("true", 1, None):
                with self.subTest(key=key, value=value):
                    self.assert_config_error(f"{key} must be true or false", remote_url=GITHUB_URL,
                                             branch="main", **{key: value})

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


class BuildRecordTest(TempDirTestCase):
    def test_saved_record_loads_back(self) -> None:
        record_path = self.root / "build-record.json"
        record = nightly.BuildRecord("a" * 40, "b" * 64, {"unit": "passed"})

        record.save(record_path)

        self.assertEqual(nightly.BuildRecord.load(record_path), record)
        self.assertEqual(sorted(path.name for path in self.root.iterdir()), ["build-record.json"])

    def test_missing_record_means_nothing_built(self) -> None:
        self.assertIsNone(nightly.BuildRecord.load(self.root / "build-record.json"))

    def test_malformed_record_says_to_rebuild(self) -> None:
        record_path = self.root / "build-record.json"
        record_path.write_text('{"commit": "abc"}')

        with self.assertRaises(nightly.NightlyError) as caught:
            nightly.BuildRecord.load(record_path)

        self.assertEqual(caught.exception.step, "build record")
        self.assertIn("run build again", caught.exception.reason)

    def test_with_result_leaves_the_original_unchanged(self) -> None:
        record = nightly.BuildRecord("a" * 40, "b" * 64)

        updated = record.with_result("unit", "passed")

        self.assertEqual(updated.results, {"unit": "passed"})
        self.assertEqual(record.results, {})


class VerifiedBuildTest(TempDirTestCase):
    def setUp(self) -> None:
        super().setUp()
        # A throwaway home whose clone holds the build under test.
        self.paths = nightly.NightlyPaths(self.root / "nightly", self.root / "port" / "bin")
        self.paths.home.mkdir()

    def test_no_record_says_to_build_first(self) -> None:
        with self.assertRaises(nightly.NightlyError) as caught:
            nightly.verified_build(self.paths, "install")

        self.assertEqual(caught.exception.step, "install")
        self.assertIn("run build first", caught.exception.reason)

    def test_a_rebuilt_binary_no_longer_matches(self) -> None:
        record_build(self.paths)
        self.paths.built_server.write_bytes(b"built again by hand")

        with self.assertRaises(nightly.NightlyError) as caught:
            nightly.verified_build(self.paths, "integration")

        self.assertIn("no longer matches the recorded build", caught.exception.reason)

    def test_a_missing_binary_says_to_build_first(self) -> None:
        record_build(self.paths)
        self.paths.built_server.unlink()

        with self.assertRaises(nightly.NightlyError) as caught:
            nightly.verified_build(self.paths, "smoke")

        self.assertIn("run build first", caught.exception.reason)

    def test_matching_binary_returns_the_record(self) -> None:
        record = record_build(self.paths)

        self.assertEqual(nightly.verified_build(self.paths, "install"), record)


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
        (self.work / "src").mkdir(exist_ok=True)
        (self.work / "src" / "comm.cpp").write_text(f"// {content}\n")
        git("-C", str(self.work), "add", "file.txt", "src")
        git("-C", str(self.work), "commit", "--quiet", "-m", content)
        push_args = ["-C", str(self.work), "push", "--quiet"]
        if force:
            push_args.append("--force")
        git(*push_args, "origin", f"HEAD:refs/heads/{branch}")
        return git("-C", str(self.work), "rev-parse", "HEAD")


def process_exists(pid: int) -> bool:
    """True while a process with `pid` exists."""
    try:
        os.kill(pid, 0)
    except ProcessLookupError:
        return False
    return True


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

    def test_a_niced_command_is_named_by_its_program_not_by_nice(self) -> None:
        program_name = Path(sys.executable).name

        with self.assertRaises(nightly.CommandFailed) as caught:
            self.runner.run(["nice", "-n", "10", sys.executable, "-c", "raise SystemExit(5)"], "integration", 30)

        self.assertEqual(caught.exception.reason, f"{program_name} exited with status 5")

    def test_a_niced_timeout_is_named_by_its_program(self) -> None:
        program_name = Path(sys.executable).name

        with self.assertRaises(nightly.CommandFailed) as caught:
            self.runner.run(["nice", "-n", "10", sys.executable, "-c", "import time; time.sleep(10)"], "smoke", 1)

        self.assertEqual(caught.exception.reason, f"{program_name} timed out after 1 s")

    def test_timeout_raises(self) -> None:
        with self.assertRaises(nightly.CommandFailed) as caught:
            self.runner.run([sys.executable, "-c", "import time; time.sleep(10)"], "unit tests", 1)

        self.assertEqual(caught.exception.step, "unit tests")
        self.assertIn("timed out after 1 s", caught.exception.reason)

    def test_timeout_also_stops_what_the_command_started(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            pid_file = Path(temp_dir) / "grandchild.pid"
            # The grandchild's output goes elsewhere, as the integration harness's servers' does, so only
            # the process group decides whether it survives.
            script = ("import subprocess, sys, time\n"
                      "child = subprocess.Popen([sys.executable, '-c', 'import time; time.sleep(60)'],"
                      " stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)\n"
                      f"open({str(pid_file)!r}, 'w').write(str(child.pid))\n"
                      "time.sleep(60)\n")

            with self.assertRaises(nightly.CommandFailed):
                self.runner.run([sys.executable, "-c", script], "integration", 2)

            grandchild_pid = int(pid_file.read_text())
        # The killed grandchild is reaped by init shortly after it dies.
        deadline = time.monotonic() + 5
        while process_exists(grandchild_pid) and time.monotonic() < deadline:
            time.sleep(0.1)
        self.assertFalse(process_exists(grandchild_pid))

    def test_an_interrupt_also_stops_what_the_command_started(self) -> None:
        # Python's own SIGINT handler, whatever an earlier main() call installed, so the interrupt surfaces here.
        previous_handler = signal.signal(signal.SIGINT, signal.default_int_handler)
        self.addCleanup(signal.signal, signal.SIGINT, previous_handler)
        with tempfile.TemporaryDirectory() as temp_dir:
            pid_file = Path(temp_dir) / "grandchild.pid"
            script = ("import subprocess, sys, time\n"
                      "child = subprocess.Popen([sys.executable, '-c', 'import time; time.sleep(60)'],"
                      " stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)\n"
                      f"open({str(pid_file)!r}, 'w').write(str(child.pid))\n"
                      "time.sleep(60)\n")

            def interrupt_once_started() -> None:
                deadline = time.monotonic() + 10
                while not pid_file.exists() and time.monotonic() < deadline:
                    time.sleep(0.05)
                time.sleep(0.2)
                os.kill(os.getpid(), signal.SIGINT)

            interrupter = threading.Thread(target=interrupt_once_started)
            interrupter.start()
            with self.assertRaises(KeyboardInterrupt):
                self.runner.run([sys.executable, "-c", script], "integration", 60)
            interrupter.join()

            grandchild_pid = int(pid_file.read_text())
        deadline = time.monotonic() + 5
        while process_exists(grandchild_pid) and time.monotonic() < deadline:
            time.sleep(0.1)
        self.assertFalse(process_exists(grandchild_pid))

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

    def test_head_is_the_checked_out_commit(self) -> None:
        first = self.upstream.commit("main", "first")
        self.upstream.commit("main", "second")
        self.clone.fetch(self.config_for(self.upstream, "main"))
        self.clone.check_out(first)

        self.assertEqual(self.clone.head(), first)

    def test_head_without_a_clone_says_to_fetch(self) -> None:
        with self.assertRaises(nightly.NightlyError) as caught:
            self.clone.head()

        self.assertEqual(caught.exception.step, "build")
        self.assertIn("run fetch first", caught.exception.reason)


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

        backup_name = nightly.nightly_backup_name("20260927_013005", "abcdef1234567890")
        checksum = self.installer.install(self.new_binary, backup_name).checksum

        current = self.port_bin / "ageland"
        self.assertEqual(current.read_bytes(), b"new build")
        self.assertEqual(checksum, sha256_bytes(b"new build"))
        self.assertEqual(current.stat().st_mode & 0o777, 0o775)

    def test_old_binary_is_kept_as_a_hard_linked_nightly_backup(self) -> None:
        old_inode = self.write_current(b"old build").stat().st_ino

        self.installer.install(self.new_binary, nightly.nightly_backup_name("20260927_013005", "abcdef1234567890"))

        backup = self.port_bin / "ageland.bak.20260927_013005.nightly-abcdef1"
        self.assertEqual(backup.read_bytes(), b"old build")
        self.assertEqual(backup.stat().st_ino, old_inode)

    def test_first_install_backup_is_kept_out_of_pruning(self) -> None:
        self.write_current(b"manual build")

        self.installer.install(self.new_binary, nightly.nightly_backup_name("20260927_013005", None))

        self.assertTrue((self.port_bin / "ageland.bak.20260927_013005.before-nightly").exists())
        self.assertEqual(list(self.port_bin.glob(nightly.BACKUP_GLOB)), [])

    def test_install_without_a_current_binary_makes_no_backup(self) -> None:
        self.installer.install(self.new_binary, nightly.nightly_backup_name("20260927_013005", None))

        self.assertEqual(sorted(path.name for path in self.port_bin.iterdir()), ["ageland"])

    def test_ageland_exists_at_the_moment_of_the_swap(self) -> None:
        self.write_current(b"old build")
        real_replace = os.replace
        present_during_swap: List[bool] = []

        def checking_replace(source: object, destination: object) -> None:
            present_during_swap.append((self.port_bin / "ageland").exists())
            real_replace(source, destination)

        with mock.patch.object(nightly.os, "replace", side_effect=checking_replace):
            self.installer.install(self.new_binary, nightly.nightly_backup_name("20260927_013005", "abcdef1234567890"))

        self.assertEqual(present_during_swap, [True])

    def test_leftover_staged_file_is_overwritten(self) -> None:
        self.write_current(b"old build")
        (self.port_bin / "ageland.nightly-new").write_bytes(b"half-copied from a crashed run")

        self.installer.install(self.new_binary, nightly.nightly_backup_name("20260927_013005", "abcdef1234567890"))

        self.assertEqual((self.port_bin / "ageland").read_bytes(), b"new build")
        self.assertFalse((self.port_bin / "ageland.nightly-new").exists())

    def test_refused_hard_link_falls_back_to_a_copied_backup(self) -> None:
        # Linux's protected_hardlinks refuses to link another user's file that the job cannot write.
        self.write_current(b"a teammate's build")

        with mock.patch.object(nightly.os, "link", side_effect=PermissionError(1, "Operation not permitted")):
            backup_name = nightly.nightly_backup_name("20260927_013005", None)
            checksum = self.installer.install(self.new_binary, backup_name).checksum

        backup = self.port_bin / "ageland.bak.20260927_013005.before-nightly"
        self.assertEqual(backup.read_bytes(), b"a teammate's build")
        self.assertEqual((self.port_bin / "ageland").read_bytes(), b"new build")
        self.assertEqual(checksum, sha256_bytes(b"new build"))

    def test_install_reports_the_backup_it_kept(self) -> None:
        self.write_current(b"old build")

        installed = self.installer.install(self.new_binary, "ageland.bak.test")

        self.assertEqual(installed.backup, self.port_bin / "ageland.bak.test")
        self.assertEqual(installed.backup.read_bytes(), b"old build")

    def test_install_without_a_current_binary_reports_no_backup(self) -> None:
        installed = self.installer.install(self.new_binary, "ageland.bak.test")

        self.assertIsNone(installed.backup)

    def test_restore_puts_the_replaced_binary_back(self) -> None:
        self.write_current(b"old build")
        installed = self.installer.install(self.new_binary, "ageland.bak.test")

        self.installer.restore(installed)

        self.assertEqual((self.port_bin / "ageland").read_bytes(), b"old build")
        self.assertFalse((self.port_bin / "ageland.bak.test").exists())

    def test_restore_without_a_backup_removes_the_new_binary(self) -> None:
        installed = self.installer.install(self.new_binary, "ageland.bak.test")

        self.installer.restore(installed)

        self.assertFalse((self.port_bin / "ageland").exists())

    def test_nightly_backup_names(self) -> None:
        self.assertEqual(nightly.nightly_backup_name("20260927_013005", "abcdef1234567890"),
                         "ageland.bak.20260927_013005.nightly-abcdef1")
        self.assertEqual(nightly.nightly_backup_name("20260927_013005", None),
                         "ageland.bak.20260927_013005.before-nightly")

    def test_prune_uses_the_installers_own_backup_glob(self) -> None:
        promotion_backups = [f"ageland.bak.202609{day:02d}_013005.promotion-4810-to-4802" for day in range(1, 11)]
        nightly_backup = "ageland.bak.20260901_013005.nightly-abc0001"
        for name in promotion_backups + [nightly_backup]:
            (self.port_bin / name).write_bytes(b"x")
        installer = nightly.BinaryInstaller(self.port_bin, "ageland.bak.*.promotion-*")

        removed = installer.prune_backups()

        self.assertEqual(sorted(path.name for path in removed), promotion_backups[:3])
        self.assertTrue((self.port_bin / nightly_backup).exists())

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


class PairedInstallTest(TempDirTestCase):
    def setUp(self) -> None:
        super().setUp()
        # A port with a binary and a src/.
        self.port_dir = self.root / "port"
        (self.port_dir / "bin").mkdir(parents=True)
        (self.port_dir / "bin" / "ageland").write_bytes(b"old build")
        (self.port_dir / "src").mkdir()
        (self.port_dir / "src" / "comm.cpp").write_text("old source\n")
        # The staged source install_with_source() swaps in.
        self.new_source = self.port_dir / ".src-new"
        self.new_source.mkdir()
        (self.new_source / "comm.cpp").write_text("new source\n")
        # The binary the swap installs.
        self.new_binary = self.root / "new-ageland"
        self.new_binary.write_bytes(b"new build")
        # The installer under test, pointed at the port's bin/.
        self.installer = nightly.BinaryInstaller(self.port_dir / "bin")

    def install(self) -> "nightly.PairedInstall":
        return nightly.install_with_source(self.installer, self.new_binary, "ageland.bak.test", self.port_dir,
                                           self.new_source, "20260927_013005")

    def assert_port_unchanged(self) -> None:
        self.assertEqual((self.port_dir / "bin" / "ageland").read_bytes(), b"old build")
        self.assertEqual((self.port_dir / "src" / "comm.cpp").read_text(), "old source\n")
        self.assertEqual(list(self.port_dir.glob("src.bak.*")), [])

    def test_binary_and_source_change_together(self) -> None:
        paired = self.install()

        self.assertEqual((self.port_dir / "bin" / "ageland").read_bytes(), b"new build")
        self.assertEqual((self.port_dir / "src" / "comm.cpp").read_text(), "new source\n")
        self.assertEqual(paired.source_backup, self.port_dir / "src.bak.20260927_013005")
        self.assertEqual((paired.source_backup / "comm.cpp").read_text(), "old source\n")
        self.assertFalse(self.new_source.exists())

    def test_a_failed_source_swap_restores_binary_and_source(self) -> None:
        real_rename = os.rename

        def failing_rename(source: object, destination: object) -> None:
            if Path(source) == self.new_source:
                raise OSError(18, "Invalid cross-device link")
            real_rename(source, destination)

        with mock.patch.object(nightly.os, "rename", side_effect=failing_rename):
            with self.assertRaises(OSError):
                self.install()

        self.assert_port_unchanged()

    def test_an_interrupt_during_the_source_swap_restores_the_binary(self) -> None:
        with mock.patch.object(nightly, "swap_source", side_effect=SystemExit(143)):
            with self.assertRaises(SystemExit):
                self.install()

        self.assert_port_unchanged()

    def test_a_port_without_src_gets_one(self) -> None:
        shutil.rmtree(self.port_dir / "src")

        paired = self.install()

        self.assertIsNone(paired.source_backup)
        self.assertEqual((self.port_dir / "src" / "comm.cpp").read_text(), "new source\n")

    def test_prune_keeps_the_newest_source_backup(self) -> None:
        for day in (1, 2, 3):
            (self.port_dir / f"src.bak.202609{day:02d}_013005").mkdir()

        removed = nightly.prune_source_backups(self.port_dir)

        self.assertEqual(sorted(path.name for path in removed),
                         ["src.bak.20260901_013005", "src.bak.20260902_013005"])
        self.assertTrue((self.port_dir / "src.bak.20260903_013005").is_dir())


# ---------------------------------------------------------------------------------------------
# One run
# ---------------------------------------------------------------------------------------------


class FakeRunner:
    """Records each command; the build step writes the binaries, the source step writes the tar `git archive`
    would, and a scripted step can fail."""

    def __init__(self, paths: "nightly.NightlyPaths", fail_step: Optional[str] = None, failure_output: str = "",
                 creates_binaries: bool = True, during_build: Optional[Callable[[], None]] = None,
                 failure_reason: Optional[str] = None):
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
        # The failing command's reason; None gives "<step> command exited with status 1".
        self.failure_reason = failure_reason
        # (step, command) for every command run, in order.
        self.commands: List[Tuple[str, List[str]]] = []
        # The working directory and environment of the last command each step ran.
        self.cwd_by_step: Dict[str, Optional[Path]] = {}
        self.env_by_step: Dict[str, Optional[dict]] = {}

    def run(self, args: List[object], step: str, timeout: int, cwd: Optional[Path] = None,
            env: Optional[dict] = None) -> str:
        self.commands.append((step, [str(arg) for arg in args]))
        self.cwd_by_step[step] = cwd
        self.env_by_step[step] = env
        if step == self.fail_step:
            reason = self.failure_reason
            if reason is None:
                reason = f"{step} command exited with status 1"
            raise nightly.CommandFailed(step, reason, self.failure_output)
        if step == "source":
            command = [str(arg) for arg in args]
            write_source_archive(Path(command[command.index("-o") + 1]), command[-2])
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


def record_build(paths: "nightly.NightlyPaths", content: bytes = b"new build",
                 results: Optional[dict] = None) -> "nightly.BuildRecord":
    """Writes a built bin/ageland into the clone and a matching build record, as a finished build leaves them."""
    paths.built_server.parent.mkdir(parents=True, exist_ok=True)
    paths.built_server.write_bytes(content)
    record = nightly.BuildRecord(NEW_COMMIT, sha256_bytes(content), dict(results or {}))
    record.save(paths.build_record)
    return record


def write_source_archive(archive_path: Path, commit: str) -> None:
    """Writes what `git archive <commit> src` would: a tar holding src/comm.cpp."""
    content = f"// built from {commit}\n".encode()
    with tarfile.open(archive_path, "w") as archive:
        directory = tarfile.TarInfo("src")
        directory.type = tarfile.DIRTYPE
        directory.mode = 0o755
        archive.addfile(directory)
        member = tarfile.TarInfo("src/comm.cpp")
        member.size = len(content)
        member.mode = 0o644
        archive.addfile(member, io.BytesIO(content))


def add_suites(paths: "nightly.NightlyPaths") -> None:
    """Gives the clone an integration suite with its test world and the smoke script, and creates the pysite."""
    world_wld = paths.clone / "tests" / "integration" / "world" / "wld"
    world_wld.mkdir(parents=True)
    (world_wld / "index").write_text("$\n")
    (paths.clone / "tools").mkdir(parents=True)
    (paths.clone / "tools" / "account_smoke.py").write_text("")
    paths.pysite.mkdir(parents=True)


class NightlyRunTest(TempDirTestCase):
    # The summary-line notes for a clone with neither the integration suite nor the smoke flow.
    SKIPPED_SUITES = ("integration skipped (no tests/integration on this branch); "
                      "smoke skipped (no tools/account_smoke.py or tests/integration/world on this branch)")

    def setUp(self) -> None:
        super().setUp()
        # A throwaway home and port bin/ that the run reads and installs into.
        self.paths = nightly.NightlyPaths(self.root / "nightly", self.root / "port" / "bin")
        self.paths.home.mkdir()
        self.paths.port_bin.mkdir(parents=True)
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

        self.assertEqual(outcome, nightly.RunOutcome(NEW_COMMIT, "installed; " + self.SKIPPED_SUITES))
        self.assertEqual(self.clone.checked_out, NEW_COMMIT)
        self.assertEqual(runner.steps(), ["configure", "build", "unit tests", "source"])
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

        self.assertEqual(runner.steps(), ["build", "unit tests", "source"])

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

    def test_under_three_gigabytes_free_stops_the_run(self) -> None:
        two_and_a_half_gigabytes = mock.Mock(free=5 * 1024 ** 3 // 2)

        with mock.patch.object(nightly.shutil, "disk_usage", return_value=two_and_a_half_gigabytes):
            with self.assertRaises(nightly.NightlyError) as caught:
                self.run_nightly(FakeRunner(self.paths))

        self.assertEqual(caught.exception.step, "disk")

    def test_build_records_the_built_binary(self) -> None:
        self.run_nightly(FakeRunner(self.paths))

        record = nightly.BuildRecord.load(self.paths.build_record)
        self.assertEqual(record.commit, NEW_COMMIT)
        self.assertEqual(record.binary_sha256, sha256_bytes(b"new build"))

    def test_every_suite_runs_in_order_before_the_install(self) -> None:
        add_suites(self.paths)
        runner = FakeRunner(self.paths)

        outcome = self.run_nightly(runner)

        self.assertEqual(outcome.message, "installed; integration passed; smoke passed")
        expected_steps = ["configure", "build", "unit tests", "integration"] + ["smoke"] * 5 + ["source"]
        self.assertEqual(runner.steps(), expected_steps)

    def test_a_report_only_failure_is_on_the_summary_and_the_build_installs(self) -> None:
        add_suites(self.paths)
        output = "FAILED tests/integration/scenarios/test_blaze.py::test_after_quit - AssertionError\n"
        runner = FakeRunner(self.paths, fail_step="integration", failure_output=output)

        outcome = self.run_nightly(runner)

        self.assertEqual(outcome.message, "installed; integration failed (1 failed: "
                                          "scenarios/test_blaze.py::test_after_quit); smoke passed")
        self.assertEqual(self.port_binary(), b"new build")

    def test_a_gated_failure_stops_the_run_before_the_next_suite(self) -> None:
        add_suites(self.paths)
        self.config = nightly.NightlyConfig(GITHUB_URL, "fix/uaf-port", None, integration_gate=True)
        output = "FAILED tests/integration/scenarios/test_blaze.py::test_after_quit - AssertionError\n"
        runner = FakeRunner(self.paths, fail_step="integration", failure_output=output)

        with self.assertRaises(nightly.NightlyError) as caught:
            self.run_nightly(runner)

        self.assertEqual(caught.exception.step, "integration")
        self.assertEqual(caught.exception.reason,
                         "1 failed: scenarios/test_blaze.py::test_after_quit (integration_gate is on)")
        self.assertNotIn("smoke", runner.steps())
        self.assertEqual(self.port_binary(), b"old nightly build")

    def test_a_gated_suite_the_branch_lacks_stops_the_run(self) -> None:
        self.config = nightly.NightlyConfig(GITHUB_URL, "fix/uaf-port", None, smoke_gate=True)

        with self.assertRaises(nightly.NightlyError) as caught:
            self.run_nightly(FakeRunner(self.paths))

        self.assertEqual(caught.exception.step, "smoke")
        self.assertIn("skipped: no tools/account_smoke.py", caught.exception.reason)
        self.assertEqual(self.port_binary(), b"old nightly build")

    def test_failing_unit_tests_stop_before_the_other_suites(self) -> None:
        add_suites(self.paths)
        runner = FakeRunner(self.paths, fail_step="unit tests", failure_output="[  FAILED  ] PoisonTest.Stacks\n")

        with self.assertRaises(nightly.NightlyError):
            self.run_nightly(runner)

        self.assertEqual(runner.steps(), ["configure", "build", "unit tests"])

    def test_a_skipped_night_leaves_the_source_alone(self) -> None:
        self.record_installed(NEW_COMMIT)
        (self.paths.port_dir / "src").mkdir()
        (self.paths.port_dir / "src" / "old.cc").write_text("2020 source\n")

        self.run_nightly(FakeRunner(self.paths))

        self.assertEqual(sorted(path.name for path in (self.paths.port_dir / "src").iterdir()), ["old.cc"])

    def test_failed_unit_tests_leave_the_source_alone(self) -> None:
        (self.paths.port_dir / "src").mkdir()
        (self.paths.port_dir / "src" / "old.cc").write_text("2020 source\n")
        runner = FakeRunner(self.paths, fail_step="unit tests", failure_output="[  FAILED  ] PoisonTest.Stacks\n")

        with self.assertRaises(nightly.NightlyError):
            self.run_nightly(runner)

        self.assertEqual(sorted(path.name for path in (self.paths.port_dir / "src").iterdir()), ["old.cc"])

    def test_a_failed_build_leaves_no_record(self) -> None:
        record_build(self.paths, b"previous build", {"unit": "passed"})

        with self.assertRaises(nightly.NightlyError):
            self.run_nightly(FakeRunner(self.paths, fail_step="build"))

        self.assertFalse(self.paths.build_record.exists())


class DescribeFailedTestsTest(unittest.TestCase):
    def test_short_lists_are_named_in_full(self) -> None:
        self.assertEqual(nightly.describe_failed_tests(["A.b", "C.d"]), "2 failed: A.b, C.d")

    def test_long_lists_are_cut_to_five_names(self) -> None:
        names = [f"test_{number}" for number in range(8)]

        self.assertEqual(nightly.describe_failed_tests(names),
                         "8 failed: test_0, test_1, test_2, test_3, test_4 and 3 more")


class SuiteTest(TempDirTestCase):
    def setUp(self) -> None:
        super().setUp()
        # A throwaway home whose clone holds a recorded build.
        self.paths = nightly.NightlyPaths(self.root / "nightly", self.root / "port" / "bin")
        self.paths.home.mkdir()
        record_build(self.paths)

    def recorded(self, suite: str) -> str:
        return nightly.BuildRecord.load(self.paths.build_record).results[suite]

    def test_a_unit_pass_is_recorded(self) -> None:
        result = nightly.run_recorded_suite("unit", FakeRunner(self.paths), self.paths)

        self.assertEqual(result, "passed")
        self.assertEqual(self.recorded("unit"), "passed")

    def test_a_unit_failure_is_recorded_with_the_names(self) -> None:
        output = "[  FAILED  ] PoisonTest.Stacks\n[  FAILED  ] FleeTest.FreedFleer\n"
        runner = FakeRunner(self.paths, fail_step="unit tests", failure_output=output)

        result = nightly.run_recorded_suite("unit", runner, self.paths)

        self.assertEqual(result, "failed: 2 failed: FleeTest.FreedFleer, PoisonTest.Stacks")
        self.assertEqual(self.recorded("unit"), result)

    def test_results_of_other_suites_are_kept(self) -> None:
        nightly.run_recorded_suite("unit", FakeRunner(self.paths), self.paths)

        nightly.run_recorded_suite("integration", FakeRunner(self.paths), self.paths)

        self.assertEqual(self.recorded("unit"), "passed")

    def test_a_suite_without_a_matching_build_is_refused(self) -> None:
        self.paths.built_server.write_bytes(b"built again by hand")

        with self.assertRaises(nightly.NightlyError) as caught:
            nightly.run_recorded_suite("integration", FakeRunner(self.paths), self.paths)

        self.assertEqual(caught.exception.step, "integration")
        self.assertIn("run build first", caught.exception.reason)

    def test_integration_is_skipped_on_a_branch_without_a_suite(self) -> None:
        runner = FakeRunner(self.paths)

        result = nightly.run_recorded_suite("integration", runner, self.paths)

        self.assertEqual(result, "skipped: no tests/integration on this branch")
        self.assertEqual(runner.commands, [])

    def test_integration_runs_pytest_niced_from_the_clone_with_the_pysite(self) -> None:
        add_suites(self.paths)
        runner = FakeRunner(self.paths)

        result = nightly.run_recorded_suite("integration", runner, self.paths)

        self.assertEqual(result, "passed")
        expected = ["nice", "-n", "10", sys.executable, "-m", "pytest", "-p", "no:cacheprovider", "tests/integration"]
        self.assertEqual(runner.commands, [("integration", expected)])
        self.assertEqual(runner.cwd_by_step["integration"], self.paths.clone)
        environment = runner.env_by_step["integration"]
        self.assertEqual(environment["PYTHONPATH"], str(self.paths.pysite))
        self.assertEqual(environment["ROTS_IT_LAUNCHER"], "local")

    def test_integration_without_the_pysite_fails_with_a_setup_hint(self) -> None:
        add_suites(self.paths)
        self.paths.pysite.rmdir()

        result = nightly.run_recorded_suite("integration", FakeRunner(self.paths), self.paths)

        self.assertEqual(result, f"failed: {self.paths.pysite} is missing; pytest must be unpacked there first")

    def test_integration_failures_are_named_without_the_directory(self) -> None:
        add_suites(self.paths)
        output = ("FAILED tests/integration/scenarios/test_blaze.py::test_after_quit - AssertionError\n"
                  "ERROR tests/integration/scenarios/test_flee.py::test_freed - RuntimeError\n")
        runner = FakeRunner(self.paths, fail_step="integration", failure_output=output)

        result = nightly.run_recorded_suite("integration", runner, self.paths)

        self.assertEqual(result, "failed: 2 failed: scenarios/test_blaze.py::test_after_quit, "
                                 "scenarios/test_flee.py::test_freed")

    def test_integration_that_names_no_test_keeps_the_command_reason(self) -> None:
        # pytest exits 5 when it collects nothing; that must never count as passed.
        add_suites(self.paths)
        runner = FakeRunner(self.paths, fail_step="integration", failure_reason="python3 exited with status 5",
                            failure_output="no tests ran\n")

        result = nightly.run_recorded_suite("integration", runner, self.paths)

        self.assertEqual(result, "failed: python3 exited with status 5")

    def test_a_suite_that_times_out_is_recorded_as_failed(self) -> None:
        add_suites(self.paths)
        runner = FakeRunner(self.paths, fail_step="integration", failure_reason="python3 timed out after 2700 s")

        result = nightly.run_recorded_suite("integration", runner, self.paths)

        self.assertEqual(result, "failed: python3 timed out after 2700 s")
        self.assertEqual(self.recorded("integration"), result)

    def test_smoke_is_skipped_on_a_branch_without_the_flow_or_its_world(self) -> None:
        runner = FakeRunner(self.paths)

        result = nightly.run_recorded_suite("smoke", runner, self.paths)

        self.assertEqual(result, "skipped: no tools/account_smoke.py or tests/integration/world on this branch")
        self.assertEqual(runner.commands, [])

    def test_smoke_builds_the_proxy_resets_lib_and_installs_the_world_before_the_flow(self) -> None:
        add_suites(self.paths)
        world_index = self.paths.clone / "lib" / "world" / "wld" / "index"
        world_present_for_flow: List[bool] = []

        class WorldCheckingRunner(FakeRunner):
            def run(self, args: List[object], step: str, timeout: int, cwd: Optional[Path] = None,
                    env: Optional[dict] = None) -> str:
                if str(args[-1]).endswith("account_smoke.py"):
                    world_present_for_flow.append(world_index.exists())
                return super().run(args, step, timeout, cwd, env)

        runner = WorldCheckingRunner(self.paths)

        result = nightly.run_recorded_suite("smoke", runner, self.paths)

        self.assertEqual(result, "passed")
        clone = str(self.paths.clone)
        self.assertEqual([command for _step, command in runner.commands], [
            ["nice", "-n", "10", "cargo", "build", "--locked", "-p", "proxy", "-j1"],
            ["git", "-C", clone, "clean", "-d", "-x", "--force", "--quiet", "--", "lib"],
            ["git", "-C", clone, "checkout", "--quiet", "--", "lib"],
            ["cmake", "--build", str(self.paths.build), "--target", "setup"],
            [sys.executable, "tools/account_smoke.py"],
        ])
        self.assertEqual(set(runner.steps()), {"smoke"})
        self.assertEqual(runner.cwd_by_step["smoke"], self.paths.clone)
        self.assertEqual(world_present_for_flow, [True])

    def test_a_failed_proxy_build_fails_the_smoke_run(self) -> None:
        add_suites(self.paths)
        runner = FakeRunner(self.paths, fail_step="smoke", failure_reason="cargo exited with status 101")

        result = nightly.run_recorded_suite("smoke", runner, self.paths)

        self.assertEqual(result, "failed: cargo exited with status 101")
        self.assertEqual(len(runner.commands), 1)


class InstallTest(TempDirTestCase):
    def setUp(self) -> None:
        super().setUp()
        # A throwaway home and port bin/ holding a manually deployed binary.
        self.paths = nightly.NightlyPaths(self.root / "nightly", self.root / "port" / "bin")
        self.paths.home.mkdir()
        self.paths.port_bin.mkdir(parents=True)
        (self.paths.port_bin / "ageland").write_bytes(b"manual build")
        # The installer under test, pointed at the temporary port bin/.
        self.installer = nightly.BinaryInstaller(self.paths.port_bin)
        # The port's current src/, which an install replaces.
        (self.paths.port_dir / "src").mkdir()
        (self.paths.port_dir / "src" / "old.cc").write_text("2020 source\n")

    def install(self, config: Optional["nightly.NightlyConfig"] = None,
                runner: Optional[FakeRunner] = None) -> str:
        if config is None:
            config = nightly.NightlyConfig(GITHUB_URL, "fix/uaf-port", None)
        if runner is None:
            runner = FakeRunner(self.paths)
        return nightly.install_recorded_build(config, self.paths, self.installer, runner, FIXED_NOW)

    def assert_refused(self, expected_text: str, config: Optional["nightly.NightlyConfig"] = None) -> None:
        with self.assertRaises(nightly.NightlyError) as caught:
            self.install(config)
        self.assertEqual(caught.exception.step, "install")
        self.assertIn(expected_text, caught.exception.reason)
        self.assertEqual((self.paths.port_bin / "ageland").read_bytes(), b"manual build")

    def test_a_build_whose_unit_tests_passed_is_installed_and_recorded(self) -> None:
        record_build(self.paths, results={"unit": "passed"})

        self.assertEqual(self.install(), "installed")

        self.assertEqual((self.paths.port_bin / "ageland").read_bytes(), b"new build")
        self.assertEqual(nightly.InstalledState.load(self.paths.state).commit, NEW_COMMIT)

    def test_without_a_unit_pass_it_is_refused(self) -> None:
        record_build(self.paths)

        self.assert_refused("refused: unit has not passed on this build (not run)")

    def test_a_gated_suite_that_has_not_passed_is_refused(self) -> None:
        gated = nightly.NightlyConfig(GITHUB_URL, "fix/uaf-port", None, integration_gate=True)
        for integration_result in ("failed: 1 failed: test_x", "skipped: no tests/integration on this branch",
                                   None):
            with self.subTest(integration_result=integration_result):
                results = {"unit": "passed"}
                if integration_result is not None:
                    results["integration"] = integration_result
                record_build(self.paths, results=results)

                self.assert_refused("refused: integration has not passed on this build", gated)

    def test_a_failed_report_only_suite_does_not_stop_the_install(self) -> None:
        record_build(self.paths, results={"unit": "passed", "integration": "failed: 1 failed: test_x",
                                          "smoke": "failed: cargo exited with status 101"})

        self.assertEqual(self.install(), "installed")

    def test_the_installed_build_is_not_installed_twice(self) -> None:
        record_build(self.paths, results={"unit": "passed"})
        self.install()

        self.assertEqual(self.install(), "skipped: already installed")
        self.assertEqual(list(self.paths.port_bin.glob("ageland.bak.*.nightly-*")), [])

    def test_an_install_writes_the_ports_source_and_commit(self) -> None:
        record_build(self.paths, results={"unit": "passed"})

        self.install()

        port_source = self.paths.port_dir / "src"
        self.assertEqual((port_source / "comm.cpp").read_text(), f"// built from {NEW_COMMIT}\n")
        self.assertEqual((port_source / ".source-commit").read_text(), NEW_COMMIT + "\n")
        self.assertEqual((port_source / "comm.cpp").stat().st_mode & 0o777, 0o660)
        self.assertEqual([path.name for path in self.paths.port_dir.glob("src.bak.*")],
                         ["src.bak.20260927_013005"])
        self.assertFalse((self.paths.port_dir / ".src-new").exists())
        self.assertFalse(self.paths.source_archive.exists())

    def test_a_refused_install_leaves_the_source_alone(self) -> None:
        record_build(self.paths)

        with self.assertRaises(nightly.NightlyError):
            self.install()

        self.assertEqual(sorted(path.name for path in (self.paths.port_dir / "src").iterdir()), ["old.cc"])

    def test_a_failed_source_staging_changes_nothing(self) -> None:
        record_build(self.paths, results={"unit": "passed"})

        with self.assertRaises(nightly.NightlyError) as caught:
            self.install(runner=FakeRunner(self.paths, fail_step="source"))

        self.assertEqual(caught.exception.step, "source")
        self.assertEqual((self.paths.port_bin / "ageland").read_bytes(), b"manual build")
        self.assertEqual(sorted(path.name for path in (self.paths.port_dir / "src").iterdir()), ["old.cc"])
        self.assertFalse((self.paths.port_dir / ".src-new").exists())

    def test_a_source_backup_that_cannot_be_pruned_is_noted_not_failed(self) -> None:
        record_build(self.paths, results={"unit": "passed"})
        for day in (1, 2):
            (self.paths.port_dir / f"src.bak.2026090{day}_013005").mkdir()
        real_rmtree = shutil.rmtree

        def refusing_rmtree(path: object, *args: object, **kwargs: object) -> None:
            if Path(path).name.startswith("src.bak."):
                raise PermissionError(13, "Permission denied", str(path))
            real_rmtree(path, *args, **kwargs)

        with mock.patch.object(nightly.shutil, "rmtree", side_effect=refusing_rmtree):
            result = self.install()

        self.assertTrue(result.startswith("installed; could not delete an old source backup: "))
        self.assertEqual(nightly.InstalledState.load(self.paths.state).commit, NEW_COMMIT)

    def test_the_swap_and_its_record_run_with_termination_signals_held(self) -> None:
        record_build(self.paths, results={"unit": "passed"})
        masks_during_install: List[set] = []
        real_install = self.installer.install

        def watching_install(new_binary: Path, backup_name: str) -> "nightly.InstalledBinary":
            masks_during_install.append(signal.pthread_sigmask(signal.SIG_BLOCK, []))
            return real_install(new_binary, backup_name)

        with mock.patch.object(self.installer, "install", side_effect=watching_install):
            self.install()

        self.assertTrue({signal.SIGTERM, signal.SIGHUP, signal.SIGINT} <= masks_during_install[0])
        self.assertNotIn(signal.SIGTERM, signal.pthread_sigmask(signal.SIG_BLOCK, []))

    def test_a_binary_rebuilt_since_the_record_is_refused(self) -> None:
        record_build(self.paths, results={"unit": "passed"})
        self.paths.built_server.write_bytes(b"built again by hand")

        self.assert_refused("no longer matches the recorded build")


class DescribeSuiteResultTest(unittest.TestCase):
    def test_each_kind_of_result(self) -> None:
        self.assertEqual(nightly.describe_suite_result("integration", "passed"), "integration passed")
        self.assertEqual(nightly.describe_suite_result("smoke", "skipped: no flow"), "smoke skipped (no flow)")
        self.assertEqual(nightly.describe_suite_result("integration", "failed: 2 failed: a, b"),
                         "integration failed (2 failed: a, b)")
        self.assertEqual(nightly.describe_suite_result("unit", "not run"), "unit not run")


class ResetRuntimeLibTest(TempDirTestCase):
    def test_lib_returns_to_the_commit_and_nothing_outside_it_changes(self) -> None:
        clone = self.root / "clone"
        git("init", "--quiet", str(clone))
        (clone / "lib" / "text").mkdir(parents=True)
        (clone / "lib" / "text" / "bugs").write_text("tracked\n")
        (clone / ".gitignore").write_text("lib/players/\n")
        git("-C", str(clone), "add", "lib", ".gitignore")
        git("-C", str(clone), "-c", "user.email=tests@example.org", "-c", "user.name=Tests",
            "-c", "commit.gpgsign=false", "commit", "--quiet", "-m", "lib")
        (clone / "lib" / "text" / "bugs").write_text("tracked\nappended by the game\n")
        (clone / "lib" / "players" / "A-E").mkdir(parents=True)
        (clone / "lib" / "players" / "A-E" / "alice").write_text("a smoke character\n")
        (clone / "lib" / "account_characters").mkdir()
        (clone / "lib" / "account_characters" / "index").write_text("untracked, not ignored\n")
        (clone / "notes.txt").write_text("outside lib\n")

        nightly.reset_runtime_lib(nightly.CommandRunner(io.StringIO()), clone)

        self.assertEqual((clone / "lib" / "text" / "bugs").read_text(), "tracked\n")
        self.assertFalse((clone / "lib" / "players").exists())
        self.assertFalse((clone / "lib" / "account_characters").exists())
        self.assertEqual((clone / "notes.txt").read_text(), "outside lib\n")


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
        self.paths = nightly.NightlyPaths(self.root / "nightly", self.root / "port" / "bin")
        self.paths.home.mkdir()
        self.paths.port_bin.mkdir(parents=True)
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

    def run_main(self, *argv: str, integration_effect: Optional[BaseException] = None) -> Tuple[int, str]:
        output = io.StringIO()
        with mock.patch.object(nightly, "build", side_effect=fake_build), \
                mock.patch.object(nightly, "run_unit_tests", return_value="passed"), \
                mock.patch.object(nightly, "run_integration_tests", return_value="passed",
                                  side_effect=integration_effect), \
                mock.patch.object(nightly, "run_smoke_flow", return_value="passed"), \
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
        expected = (f"2026-09-27 01:30:05  {self.upstream.url} fix/uaf-port  {self.tip[:7]}  "
                    "installed; integration passed; smoke passed  (log: runs/20260927_013005.log)")
        self.assertEqual(self.summary_lines(), [expected])
        self.assertEqual(printed.strip(), expected)
        self.assertTrue((self.paths.run_logs / "20260927_013005.log").exists())
        self.assertEqual((self.paths.port_dir / "src" / "comm.cpp").read_text(), "// work\n")
        self.assertEqual((self.paths.port_dir / "src" / ".source-commit").read_text(), self.tip + "\n")

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

    def test_a_termination_is_logged_and_exits_with_its_status(self) -> None:
        self.write_config()
        self.run_main("fetch")

        with mock.patch.object(nightly, "build_and_record", side_effect=SystemExit(128 + signal.SIGTERM)):
            exit_status, _printed = self.run_main("build")

        self.assertEqual(exit_status, 128 + signal.SIGTERM)
        self.assertIn(f"build: failed: terminated (exit {128 + signal.SIGTERM})", self.summary_lines()[-1])

    def test_hangup_interrupt_and_terminate_all_unwind(self) -> None:
        installed = {}

        def record_handler(signal_number: int, handler: object) -> None:
            installed[signal_number] = handler

        with mock.patch.object(nightly.signal, "signal", side_effect=record_handler):
            self.run_main("build")

        for signal_number in (signal.SIGTERM, signal.SIGHUP, signal.SIGINT):
            self.assertIs(installed[signal_number], nightly.raise_system_exit)

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

        exit_status, printed = self.run_main("status")

        self.assertEqual(exit_status, 0)
        self.assertIn(f"Installed: {self.tip[:7]} from {self.upstream.url} fix/uaf-port at 2026-09-27T01:30:05",
                      printed)
        self.assertIn("run 7", printed)
        self.assertIn("run 3", printed)
        self.assertNotIn("run 2", printed)

    def test_status_with_nothing_installed(self) -> None:
        exit_status, printed = self.run_main("status")

        self.assertEqual(exit_status, 0)
        self.assertIn("Installed: nothing yet", printed)
        self.assertIn("Built: nothing recorded", printed)
        self.assertIn("No runs recorded yet", printed)

    def test_dry_run_with_a_command_is_refused(self) -> None:
        errors = io.StringIO()

        with contextlib.redirect_stderr(errors):
            with self.assertRaises(SystemExit):
                nightly.main(["--dry-run", "build"], self.paths, lambda: FIXED_NOW)

        self.assertIn("--dry-run applies only to the nightly run", errors.getvalue())

    def test_each_step_by_hand_installs_the_fetched_build(self) -> None:
        self.write_config()

        statuses = [self.run_main(command)[0] for command in ("fetch", "build", "test", "install")]

        self.assertEqual(statuses, [0, 0, 0, 0])
        self.assertEqual((self.paths.port_bin / "ageland").read_bytes(), b"built from the clone")
        results = [line.split("  ")[3] for line in self.summary_lines()]
        self.assertEqual(results, ["fetch: fetched", "build: built", "test: passed", "install: installed"])
        self.assertTrue(all(self.tip[:7] in line for line in self.summary_lines()))
        self.assertTrue((self.paths.run_logs / "20260927_013005-install.log").exists())

    def test_install_by_hand_without_the_unit_tests_is_refused(self) -> None:
        self.write_config()
        self.run_main("fetch")
        self.run_main("build")

        exit_status, _printed = self.run_main("install")

        self.assertEqual(exit_status, 1)
        self.assertIn("install: failed: install: refused: unit has not passed on this build (not run)",
                      self.summary_lines()[-1])
        self.assertEqual((self.paths.port_bin / "ageland").read_bytes(), b"manual build")

    def test_a_failing_suite_by_hand_exits_1_and_is_recorded(self) -> None:
        self.write_config()
        self.run_main("fetch")
        self.run_main("build")

        exit_status, _printed = self.run_main(
            "integration", integration_effect=nightly.NightlyError("integration", "1 failed: test_x"))

        self.assertEqual(exit_status, 1)
        self.assertIn("integration: failed: integration: 1 failed: test_x", self.summary_lines()[-1])
        record = nightly.BuildRecord.load(self.paths.build_record)
        self.assertEqual(record.results["integration"], "failed: 1 failed: test_x")

    def test_fetch_discards_a_build_of_another_commit(self) -> None:
        self.write_config()
        self.run_main("fetch")
        self.run_main("build")
        newer_tip = self.upstream.commit("fix/uaf-port", "newer work")

        exit_status, _printed = self.run_main("fetch")

        self.assertEqual(exit_status, 0)
        self.assertIn(f"{newer_tip[:7]}  fetch: fetched; the previous build was of another commit and was discarded",
                      self.summary_lines()[-1])
        self.assertFalse(self.paths.build_record.exists())
        exit_status, _printed = self.run_main("test")
        self.assertEqual(exit_status, 1)
        self.assertIn("run build first", self.summary_lines()[-1])

    def test_build_by_hand_without_a_clone_says_to_fetch(self) -> None:
        exit_status, _printed = self.run_main("build")

        self.assertEqual(exit_status, 1)
        self.assertIn("build: failed: build: there is no clone", self.summary_lines()[-1])

    def test_a_command_run_while_the_lock_is_held_exits_1_without_logging(self) -> None:
        errors = io.StringIO()
        with self.paths.lock.open("a") as held_lock:
            fcntl.flock(held_lock, fcntl.LOCK_EX | fcntl.LOCK_NB)

            with contextlib.redirect_stderr(errors):
                exit_status, _printed = self.run_main("build")

        self.assertEqual(exit_status, 1)
        self.assertIn("another nightly run holds the lock", errors.getvalue())
        self.assertFalse(self.paths.summary_log.exists())

    def test_write_source_writes_the_installed_commits_source(self) -> None:
        self.write_config()
        self.run_main()
        shutil.rmtree(self.paths.port_dir / "src")

        exit_status, _printed = self.run_main("write-source")

        self.assertEqual(exit_status, 0)
        self.assertIn(f"{self.tip[:7]}  write-source: source written", self.summary_lines()[-1])
        self.assertEqual((self.paths.port_dir / "src" / "comm.cpp").read_text(), "// work\n")
        self.assertEqual((self.paths.port_dir / "src" / ".source-commit").read_text(), self.tip + "\n")

    def test_write_source_refuses_a_binary_replaced_since_the_install(self) -> None:
        self.write_config()
        self.run_main()
        (self.paths.port_bin / "ageland").write_bytes(b"a hand build")

        exit_status, _printed = self.run_main("write-source")

        self.assertEqual(exit_status, 1)
        self.assertIn(f"write-source: failed: write-source: bin/ageland is not the {self.tip[:7]} build",
                      self.summary_lines()[-1])

    def test_write_source_with_nothing_installed_refuses(self) -> None:
        exit_status, _printed = self.run_main("write-source")

        self.assertEqual(exit_status, 1)
        self.assertIn("write-source: failed: write-source: nothing has been installed yet", self.summary_lines()[-1])

    def test_status_shows_the_recorded_build(self) -> None:
        record = nightly.BuildRecord("a" * 40, "0" * 64, {"unit": "passed", "integration": "failed: 1 failed: test_x"})
        record.save(self.paths.build_record)

        _exit_status, printed = self.run_main("status")

        self.assertIn("Built: aaaaaaa (unit passed, integration failed (1 failed: test_x), smoke not run)", printed)


if __name__ == "__main__":
    unittest.main()
