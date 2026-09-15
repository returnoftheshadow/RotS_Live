#!/usr/bin/env python3

import contextlib
import datetime
import importlib.util
import io
import os
import shlex
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path
from unittest import mock


MODULE_PATH = Path(__file__).resolve().parent / "deploy.py"
SPEC = importlib.util.spec_from_file_location("deploy", MODULE_PATH)
deploy = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
sys.modules["deploy"] = deploy
sys.dont_write_bytecode = True
SPEC.loader.exec_module(deploy)

TEST_ENV = deploy.ENVS["zzz-forge-test"]
SERVER = deploy.Server("someone", "example.org", 2222)


# ---------------------------------------------------------------------------------------------
# Environments and arguments
# ---------------------------------------------------------------------------------------------


class EnvTableTest(unittest.TestCase):
    def test_every_env_dir_passes_the_remote_path_guard(self) -> None:
        for env in deploy.ENVS.values():
            self.assertEqual(deploy.port_dir(env), f"/rots/{env.dir_name}")

    def test_only_the_4k_envs_turn_big_brother_off(self) -> None:
        with_edits = sorted(name for name, env in deploy.ENVS.items() if env.source_edits)

        self.assertEqual(with_edits, ["4k", "zzz-forge-test-4k"])
        self.assertEqual(deploy.ENVS["4k"].source_edits, (deploy.BIG_BROTHER_OFF,))
        self.assertEqual(deploy.BIG_BROTHER_OFF.old_line, "#define USE_BIG_BROTHER 1")
        self.assertEqual(deploy.BIG_BROTHER_OFF.new_line, "#define USE_BIG_BROTHER 0")

    def test_coders_is_the_only_env_without_a_backup(self) -> None:
        self.assertEqual([name for name, env in deploy.ENVS.items() if not env.backup], ["coders"])

    def test_real_ports_are_tagged(self) -> None:
        for name in ("live", "4k", "test", "coders"):
            self.assertEqual(deploy.ENVS[name].tag_prefix, name + "-")

    def test_only_the_test_port_allows_any_branch(self) -> None:
        for name in ("live", "4k", "coders"):
            self.assertTrue(deploy.ENVS[name].require_branch)
        self.assertFalse(deploy.ENVS["test"].require_branch)

    def test_test_targets_are_untagged_and_allow_any_branch(self) -> None:
        for name in ("zzz-forge-test", "zzz-forge-test-4k"):
            self.assertEqual(deploy.ENVS[name].dir_name, "zzz-forge-test")
            self.assertIsNone(deploy.ENVS[name].tag_prefix)
            self.assertFalse(deploy.ENVS[name].require_branch)

    def test_only_test_and_the_forge_test_targets_are_deployable(self) -> None:
        self.assertEqual([name for name, env in deploy.ENVS.items() if env.deployable],
                         ["test", "zzz-forge-test", "zzz-forge-test-4k"])

    def test_only_test_restarts_and_it_restarts_rotsbuilding(self) -> None:
        self.assertEqual({name: env.restart_service for name, env in deploy.ENVS.items() if env.restart_service},
                         {"test": "rotsbuilding"})

    def test_port_dir_guard_rejects_anything_but_a_plain_name(self) -> None:
        for bad in ("../etc", "a/b", "", "Live", "a b", "x;rm"):
            env = deploy.Env("bad", bad, deploy.CYAN, backup=True, tag_prefix=None)
            with self.assertRaises(deploy.DeployError):
                deploy.port_dir(env)


class ArgumentsTest(unittest.TestCase):
    def parse(self, *argv: str):
        with contextlib.redirect_stderr(io.StringIO()):
            return deploy.build_parser().parse_args(argv)

    def assert_usage_error(self, *argv: str) -> None:
        with self.assertRaises(SystemExit) as caught:
            self.parse(*argv)
        self.assertEqual(caught.exception.code, 2)

    def test_parses_env_login_and_port_in_order(self) -> None:
        args = self.parse("deploy", "live", "someone@example.org", "2222")

        self.assertEqual((args.env, args.login, args.port, args.dry_run),
                         ("live", ("someone", "example.org"), 2222, False))

    def test_dry_run_flag(self) -> None:
        self.assertTrue(self.parse("deploy", "4k", "someone@example.org", "2222", "--dry-run").dry_run)

    def test_restart_flag_is_off_unless_given(self) -> None:
        self.assertFalse(self.parse("deploy", "test", "someone@example.org", "2222").restart)
        self.assertTrue(self.parse("deploy", "test", "someone@example.org", "2222", "--restart").restart)

    def test_revert_has_no_restart_flag(self) -> None:
        self.assert_usage_error("revert", "test", "someone@example.org", "2222", "--restart")

    def test_every_argument_is_required(self) -> None:
        self.assert_usage_error()
        self.assert_usage_error("deploy")
        self.assert_usage_error("deploy", "live")
        self.assert_usage_error("deploy", "live", "someone@example.org")

    def test_unknown_env_is_rejected(self) -> None:
        self.assert_usage_error("deploy", "prod", "someone@example.org", "2222")

    def test_login_needs_exactly_one_at_with_both_sides(self) -> None:
        for bad in ("example.org", "a@b@c", "@example.org", "someone@"):
            self.assert_usage_error("deploy", "live", bad, "2222")

    def test_login_cannot_smuggle_ssh_options(self) -> None:
        for bad in ("-oProxyCommand=x@example.org", "someone@-oProxyCommand=x", "some one@example.org"):
            self.assert_usage_error("deploy", "live", bad, "2222")

    def test_login_with_dots_underscores_and_hyphens_parses(self) -> None:
        args = self.parse("deploy", "live", "some.one_2@host-1.example.org", "2222")

        self.assertEqual(args.login, ("some.one_2", "host-1.example.org"))

    def test_port_must_be_a_valid_number(self) -> None:
        for bad in ("ssh", "0", "70000", "-1", "22.0"):
            self.assert_usage_error("deploy", "live", "someone@example.org", bad)

    def test_revert_takes_env_login_and_port(self) -> None:
        args = self.parse("revert", "zzz-forge-test", "someone@example.org", "2222")

        self.assertEqual((args.command, args.env, args.login, args.port),
                         ("revert", "zzz-forge-test", ("someone", "example.org"), 2222))

    def test_revert_needs_every_argument_and_a_known_env(self) -> None:
        self.assert_usage_error("revert")
        self.assert_usage_error("revert", "live")
        self.assert_usage_error("revert", "live", "someone@example.org")
        self.assert_usage_error("revert", "prod", "someone@example.org", "2222")
        self.assert_usage_error("revert", "live", "-oProxyCommand=x@example.org", "2222")


# ---------------------------------------------------------------------------------------------
# Help files
# ---------------------------------------------------------------------------------------------

CONSTS_SNIPPET = """int help_summary_length = 2;

struct help_index_summary help_content[] = {
    { "general", "General information", "text/help_tbl", 0, 0, 0, 0 },
    { "specializations", "Becoming an expert in a field of your choice", "text/spec_tbl", 0, 0, 0,
        0 },
};

const char* unrelated = "text/not_a_chapter";
"""


class HelpFilesTest(unittest.TestCase):
    def test_selects_tracked_help_tables_and_the_help_page(self) -> None:
        tracked = ["lib/text/help", "lib/text/help_tbl", "lib/text/help_tbl.old", "lib/text/motd",
                   "lib/text/new_tbl", "lib/text/sub/deep_tbl", "src/other_tbl"]

        self.assertEqual(deploy.help_files(tracked), ["help", "help_tbl", "new_tbl"])


class HelpChaptersTest(unittest.TestCase):
    def test_reads_the_text_entries_of_help_content(self) -> None:
        self.assertEqual(deploy.help_chapters(CONSTS_SNIPPET), ["help_tbl", "spec_tbl"])

    def test_missing_table_is_an_error(self) -> None:
        with self.assertRaises(deploy.DeployError):
            deploy.help_chapters("int nothing_here;")

    def test_the_real_consts_cpp_lists_nine_chapters(self) -> None:
        text = (deploy.REPO_ROOT / "src" / "consts.cpp").read_text(errors="replace")

        self.assertEqual(deploy.help_chapters(text), ["help_tbl", "spel_tbl", "pray_tbl", "skil_tbl", "spec_tbl",
                                                      "wizh_tbl", "shap_tbl", "scr_tbl", "msdp_tbl"])


class HelpFormatTest(unittest.TestCase):
    def test_well_formed_file_passes(self) -> None:
        self.assertEqual(deploy.check_help_format("help_tbl", "KILL HIT\n\nStart a fight.\n#\nFLEE\nRun.\n#~\n"), [])

    def test_separator_with_trailing_space_passes(self) -> None:
        self.assertEqual(deploy.check_help_format("help_tbl", "A\ntext\n# \nB\ntext\n#~"), [])

    def test_line_starting_with_hash_fails_and_names_the_line(self) -> None:
        problems = deploy.check_help_format("help_tbl", "COLOR\nHex values must look like\n#RRGGBB.\n#~\n")

        self.assertEqual(len(problems), 1)
        self.assertIn("lib/text/help_tbl:3:", problems[0])
        self.assertIn("#RRGGBB.", problems[0])

    def test_missing_end_marker_fails(self) -> None:
        problems = deploy.check_help_format("spel_tbl", "A\ntext\n#\n")

        self.assertEqual(problems, ["lib/text/spel_tbl: does not end with a '#~' line"])

    def test_end_marker_before_the_end_fails(self) -> None:
        problems = deploy.check_help_format("help_tbl", "A\n#~\nB\n#~\n")

        self.assertEqual(len(problems), 1)
        self.assertIn("lib/text/help_tbl:2:", problems[0])


class CheckHelpTablesTest(unittest.TestCase):
    def setUp(self) -> None:
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.repo = Path(temp.name)
        (self.repo / "src").mkdir()
        (self.repo / "lib" / "text").mkdir(parents=True)
        (self.repo / "src" / "consts.cpp").write_text(CONSTS_SNIPPET)

    def test_reports_bad_chapters_and_untracked_chapters(self) -> None:
        (self.repo / "lib" / "text" / "help_tbl").write_text("A\n#oops\n#~\n")

        problems = deploy.check_help_tables(self.repo, ["lib/text/help_tbl"])

        self.assertEqual(len(problems), 2)
        self.assertIn("lib/text/help_tbl:2:", problems[0])
        self.assertIn("lib/text/spec_tbl: listed in src/consts.cpp", problems[1])

    def test_clean_chapters_pass(self) -> None:
        for name in ("help_tbl", "spec_tbl"):
            (self.repo / "lib" / "text" / name).write_text("A\ntext\n#~\n")

        self.assertEqual(deploy.check_help_tables(self.repo, ["lib/text/help_tbl", "lib/text/spec_tbl"]), [])


# ---------------------------------------------------------------------------------------------
# The local checkout
# ---------------------------------------------------------------------------------------------


class GitRepoTestCase(unittest.TestCase):
    def setUp(self) -> None:
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.repo = Path(temp.name)
        environment = mock.patch.dict(os.environ, {
            "GIT_CONFIG_GLOBAL": os.devnull, "GIT_CONFIG_NOSYSTEM": "1",
            "GIT_AUTHOR_NAME": "Test", "GIT_AUTHOR_EMAIL": "test@example.org",
            "GIT_COMMITTER_NAME": "Test", "GIT_COMMITTER_EMAIL": "test@example.org",
        })
        environment.start()
        self.addCleanup(environment.stop)
        self.run_git("init", "-q", "-b", "release-frodo")
        self.write("src/game.cpp", "int main() {}\n")
        self.write("src/.gitignore", "*.o\n.remember/\n")
        self.write("lib/text/help_tbl", "A\n#~\n")
        self.write("lib/text/motd", "Welcome.\n")
        self.commit("initial")
        self.checkout = deploy.Checkout(self.repo)

    def run_git(self, *args: str) -> str:
        return subprocess.run(["git", "-C", str(self.repo), *args], check=True, capture_output=True, text=True).stdout

    def write(self, relative: str, text: str) -> None:
        path = self.repo / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)

    def commit(self, message: str) -> str:
        self.run_git("add", "-A")
        self.run_git("commit", "-q", "-m", message)
        return self.run_git("rev-parse", "HEAD").strip()


class CheckCheckoutTest(GitRepoTestCase):
    def test_clean_checkout_on_the_deploy_branch_passes(self) -> None:
        self.assertEqual(deploy.check_checkout(self.repo, deploy.ENVS["live"]), [])

    def test_uncommitted_change_stops(self) -> None:
        self.write("src/game.cpp", "int main() { return 1; }\n")

        with self.assertRaisesRegex(deploy.DeployError, "uncommitted"):
            deploy.check_checkout(self.repo, deploy.ENVS["live"])

    def test_ignored_object_file_in_src_stops(self) -> None:
        self.write("src/game.o", "binary")

        with self.assertRaisesRegex(deploy.DeployError, "src/game.o"):
            deploy.check_checkout(self.repo, deploy.ENVS["live"])

    def test_untracked_source_file_in_src_stops(self) -> None:
        self.write("src/new_feature.cpp", "// not committed\n")

        with self.assertRaisesRegex(deploy.DeployError, "src/new_feature.cpp"):
            deploy.check_checkout(self.repo, deploy.ENVS["live"])

    def test_dot_directories_in_src_and_files_outside_src_pass(self) -> None:
        self.write("src/.remember/now.md", "notes\n")
        self.write("notes.txt", "scratch\n")

        self.assertEqual(deploy.check_checkout(self.repo, deploy.ENVS["live"]), [])

    def test_other_branch_stops_a_real_port(self) -> None:
        self.run_git("checkout", "-q", "-b", "feat/x")

        with self.assertRaisesRegex(deploy.DeployError, "feat/x"):
            deploy.check_checkout(self.repo, deploy.ENVS["live"])

    def test_other_branch_only_warns_for_a_test_target(self) -> None:
        self.run_git("checkout", "-q", "-b", "feat/x")

        warnings = deploy.check_checkout(self.repo, TEST_ENV)

        self.assertEqual(len(warnings), 1)
        self.assertIn("feat/x", warnings[0])

    def test_other_branch_only_warns_for_the_test_port(self) -> None:
        self.run_git("checkout", "-q", "-b", "feat/x")

        warnings = deploy.check_checkout(self.repo, deploy.ENVS["test"])

        self.assertEqual(len(warnings), 1)
        self.assertIn("feat/x", warnings[0])
        self.assertIn("test allows any branch", warnings[0])


class CheckoutTest(GitRepoTestCase):
    def test_head_returns_sha_and_subject(self) -> None:
        sha = self.run_git("rev-parse", "HEAD").strip()

        self.assertEqual(self.checkout.head(), (sha, "initial"))

    def test_help_files_come_from_git(self) -> None:
        self.write("lib/text/untracked_tbl", "A\n#~\n")

        self.assertEqual(self.checkout.help_files(), ["help_tbl"])

    def test_dry_run_does_not_pull(self) -> None:
        # There is no remote, so a pull would fail.
        self.assertEqual(self.checkout.prepare(deploy.ENVS["live"], dry_run=True), [])

    def test_real_run_pulls_on_the_deploy_branch(self) -> None:
        with self.assertRaisesRegex(deploy.DeployError, "git pull --ff-only failed"):
            self.checkout.prepare(deploy.ENVS["live"], dry_run=False)

    def test_test_target_on_another_branch_does_not_pull(self) -> None:
        self.run_git("checkout", "-q", "-b", "feat/x")

        self.assertEqual(len(self.checkout.prepare(TEST_ENV, dry_run=False)), 1)


class TagNamingTest(unittest.TestCase):
    DAY = datetime.date(2026, 9, 13)

    def test_first_tag_of_the_day(self) -> None:
        self.assertEqual(deploy.next_tag_name("test-", self.DAY, ["test-2026-09-12"]), "test-2026-09-13")

    def test_second_and_third_tags_of_the_day(self) -> None:
        self.assertEqual(deploy.next_tag_name("test-", self.DAY, ["test-2026-09-13"]), "test-2026-09-13-2")
        self.assertEqual(deploy.next_tag_name("test-", self.DAY, ["test-2026-09-13", "test-2026-09-13-2"]),
                         "test-2026-09-13-3")

    def test_previous_tag_is_the_latest_date_and_suffix_for_the_prefix(self) -> None:
        tags = ["live-2026-09-11", "live-2026-09-13", "live-2026-09-13-2", "live-2026-09-13-10",
                "test-2026-09-20", "live-hotfix", "4k-2026-10-01"]

        self.assertEqual(deploy.previous_tag("live-", tags), "live-2026-09-13-10")
        self.assertIsNone(deploy.previous_tag("coders-", tags))

    def test_help_changes_line(self) -> None:
        self.assertEqual(deploy.help_changes_line(None, []), "Help changes: first tagged deploy")
        self.assertEqual(deploy.help_changes_line("test-2026-09-13", []), "Help changes since test-2026-09-13: none")
        self.assertEqual(deploy.help_changes_line("test-2026-09-13", ["help_tbl", "shap_tbl"]),
                         "Help changes since test-2026-09-13: help_tbl, shap_tbl")


class CreateTagTest(GitRepoTestCase):
    def tag_contents(self, name: str) -> str:
        return self.run_git("tag", "-l", "--format=%(contents)", name)

    def test_first_tag_records_env_dir_sha_and_first_deploy(self) -> None:
        sha = self.run_git("rev-parse", "HEAD").strip()

        name = self.checkout.create_tag(deploy.ENVS["test"], sha, ["help_tbl"], datetime.date(2026, 9, 13))

        self.assertEqual(name, "test-2026-09-13")
        contents = self.tag_contents(name)
        self.assertIn(f"Deployed {sha} to test (/rots/dev-building4802).", contents)
        self.assertIn("Help changes: first tagged deploy", contents)
        self.assertNotIn("someone", contents)

    def test_later_tag_lists_only_help_files_changed_since_the_previous_tag(self) -> None:
        env = deploy.ENVS["test"]
        first = self.run_git("rev-parse", "HEAD").strip()
        self.checkout.create_tag(env, first, ["help_tbl"], datetime.date(2026, 9, 13))
        self.write("lib/text/help_tbl", "A\nnew text\n#~\n")
        self.write("lib/text/motd", "Changed on purpose.\n")
        second = self.commit("help update")

        name = self.checkout.create_tag(env, second, ["help_tbl"], datetime.date(2026, 9, 14))

        self.assertEqual(name, "test-2026-09-14")
        self.assertIn("Help changes since test-2026-09-13: help_tbl", self.tag_contents(name))

    def test_same_day_redeploy_gets_a_suffix_and_no_help_changes(self) -> None:
        env = deploy.ENVS["test"]
        sha = self.run_git("rev-parse", "HEAD").strip()
        self.checkout.create_tag(env, sha, ["help_tbl"], datetime.date(2026, 9, 13))

        name = self.checkout.create_tag(env, sha, ["help_tbl"], datetime.date(2026, 9, 13))

        self.assertEqual(name, "test-2026-09-13-2")
        self.assertIn("Help changes since test-2026-09-13: none", self.tag_contents(name))


class TagRemoteTest(GitRepoTestCase):
    """Deploy tags go to the main repo; a local bare repo stands in for it."""

    def setUp(self) -> None:
        super().setUp()
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.remote = Path(temp.name) / "main.git"
        subprocess.run(["git", "init", "-q", "--bare", str(self.remote)], check=True)
        self.checkout = deploy.Checkout(self.repo, tag_remote=str(self.remote))
        self.sha = self.run_git("rev-parse", "HEAD").strip()

    def remote_tags(self):
        output = subprocess.run(["git", "ls-remote", "--tags", "--refs", str(self.remote)], check=True,
                                capture_output=True, text=True).stdout
        return sorted(line.split("refs/tags/")[1] for line in output.splitlines())

    def publish_and_forget(self, *names: str) -> None:
        """Tags another deployer pushed: on the main repo, not in this checkout."""
        for name in names:
            self.run_git("tag", "-a", name, self.sha, "-m", "pushed by someone else")
            self.run_git("push", "-q", str(self.remote), f"refs/tags/{name}")
            self.run_git("tag", "-d", name)

    def test_the_main_repo_is_the_default_tag_remote(self) -> None:
        self.assertEqual(deploy.TAG_REMOTE, "git@github.com:returnoftheshadow/RotS_Live.git")
        self.assertEqual(deploy.Checkout(self.repo).tag_remote, deploy.TAG_REMOTE)

    def test_push_tag_puts_the_tag_on_the_main_repo(self) -> None:
        name = self.checkout.create_tag(deploy.ENVS["test"], self.sha, ["help_tbl"], datetime.date(2026, 9, 13))

        self.checkout.push_tag(name)

        self.assertEqual(self.remote_tags(), ["test-2026-09-13"])

    def test_fetched_tags_from_other_deployers_move_the_next_name_along(self) -> None:
        self.publish_and_forget("test-2026-09-13")

        self.checkout.fetch_tags("test-")
        name = self.checkout.create_tag(deploy.ENVS["test"], self.sha, ["help_tbl"], datetime.date(2026, 9, 13))

        self.assertEqual(name, "test-2026-09-13-2")

    def test_fetch_tags_only_fetches_the_env_prefix(self) -> None:
        self.publish_and_forget("live-2026-09-11", "test-2026-09-13", "unrelated")

        self.checkout.fetch_tags("test-")

        self.assertEqual(self.run_git("tag", "--list").split(), ["test-2026-09-13"])

    def test_push_and_fetch_failures_raise_a_deploy_error(self) -> None:
        missing = deploy.Checkout(self.repo, tag_remote=str(self.remote.parent / "missing.git"))
        self.run_git("tag", "-a", "test-2026-09-13", self.sha, "-m", "local")

        with self.assertRaisesRegex(deploy.DeployError, "git push"):
            missing.push_tag("test-2026-09-13")
        with self.assertRaisesRegex(deploy.DeployError, "git fetch"):
            missing.fetch_tags("test-")


# ---------------------------------------------------------------------------------------------
# Remote commands, run locally with sh against a fake port directory
# ---------------------------------------------------------------------------------------------


class RemoteCommandTestCase(unittest.TestCase):
    def setUp(self) -> None:
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.root = Path(temp.name) / "port"
        for sub in ("src", "bin", "lib/text"):
            (self.root / sub).mkdir(parents=True)
        (self.root / "src" / "game.cpp").write_text("old source\n")
        (self.root / "src" / "game.o").write_text("old object\n")
        (self.root / "lib" / "text" / "help_tbl").write_text("old help\n#~\n")

    def sh(self, command: str) -> subprocess.CompletedProcess:
        local = command.replace(deploy.port_dir(TEST_ENV), str(self.root))
        self.assertNotIn("/rots/", local)
        return subprocess.run(["sh", "-c", local], capture_output=True, text=True)


class MissingDirsCommandTest(RemoteCommandTestCase):
    def test_passes_when_src_bin_and_lib_text_exist(self) -> None:
        self.assertEqual(self.sh(deploy.missing_dirs_command(TEST_ENV)).returncode, 0)

    def test_names_a_missing_directory(self) -> None:
        (self.root / "bin").rmdir()

        result = self.sh(deploy.missing_dirs_command(TEST_ENV))

        self.assertNotEqual(result.returncode, 0)
        self.assertIn("missing directory:", result.stdout)
        self.assertIn("/bin", result.stdout)


class OutsideLinksCommandTest(RemoteCommandTestCase):
    def setUp(self) -> None:
        super().setUp()
        self.outside = self.root.parent / "outside"
        self.outside.mkdir()
        (self.outside / "file").write_text("outside\n")
        (self.outside / "dir").mkdir()

    def check(self) -> subprocess.CompletedProcess:
        return self.sh(deploy.outside_links_command(TEST_ENV))

    def assert_refused_naming(self, link: Path) -> None:
        result = self.check()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("outside", result.stdout)
        self.assertIn(str(link), result.stdout)

    def test_passes_without_symlinks(self) -> None:
        result = self.check()

        self.assertEqual((result.returncode, result.stdout), (0, ""))

    def test_passes_with_a_symlink_that_stays_inside_the_port(self) -> None:
        os.symlink("../lib/text/help_tbl", self.root / "src" / "help_link")

        result = self.check()

        self.assertEqual((result.returncode, result.stdout), (0, ""))

    def test_refuses_a_file_symlink_in_src_that_points_outside(self) -> None:
        link = self.root / "src" / "outside_file"
        os.symlink(self.outside / "file", link)

        self.assert_refused_naming(link)

    def test_refuses_a_directory_symlink_in_bin_that_points_outside(self) -> None:
        link = self.root / "bin" / "outside_dir"
        os.symlink(self.outside / "dir", link)

        self.assert_refused_naming(link)

    def test_refuses_a_help_file_symlinked_outside(self) -> None:
        link = self.root / "lib" / "text" / "help_tbl"
        link.unlink()
        os.symlink(self.outside / "file", link)

        self.assert_refused_naming(link)

    def test_refuses_lib_text_itself_symlinked_outside(self) -> None:
        text_dir = self.root / "lib" / "text"
        (text_dir / "help_tbl").unlink()
        text_dir.rmdir()
        os.symlink(self.outside / "dir", text_dir)

        self.assert_refused_naming(text_dir)

    def test_refuses_a_dangling_symlink_that_points_outside(self) -> None:
        link = self.root / "src" / "dangling"
        os.symlink(self.outside / "missing", link)

        self.assert_refused_naming(link)

    def test_checks_inside_a_src_that_is_itself_a_symlink_within_the_port(self) -> None:
        (self.root / "src").rename(self.root / "src-real")
        os.symlink("src-real", self.root / "src")
        os.symlink(self.outside / "file", self.root / "src-real" / "outside_file")

        self.assert_refused_naming(self.root / "src" / "outside_file")


class DeployMarkerCommandTest(RemoteCommandTestCase):
    def marker(self) -> Path:
        return self.root / "src" / deploy.DEPLOY_MARKER

    def test_unfinished_check_passes_without_the_marker(self) -> None:
        self.assertEqual(self.sh(deploy.unfinished_deploy_command(TEST_ENV, SERVER)).returncode, 0)

    def test_unfinished_check_fails_and_names_the_revert_command_when_the_marker_is_present(self) -> None:
        self.marker().write_text("")

        result = self.sh(deploy.unfinished_deploy_command(TEST_ENV, SERVER))

        self.assertNotEqual(result.returncode, 0)
        self.assertIn("DEPLOY_IN_PROGRESS", result.stdout)
        self.assertIn("did not finish", result.stdout)
        self.assertIn("scripts/deploy.py revert zzz-forge-test someone@example.org 2222", result.stdout)
        self.assertNotIn("see the failure report", result.stdout)

    def test_mark_started_creates_the_marker(self) -> None:
        result = self.sh(deploy.mark_deploy_started_command(TEST_ENV))

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(self.marker().exists())

    def test_mark_finished_removes_the_marker(self) -> None:
        self.marker().write_text("")

        result = self.sh(deploy.mark_deploy_finished_command(TEST_ENV))

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse(self.marker().exists())

    def test_mark_finished_is_a_no_op_when_the_marker_is_already_gone(self) -> None:
        self.assertEqual(self.sh(deploy.mark_deploy_finished_command(TEST_ENV)).returncode, 0)


@unittest.skipIf(os.geteuid() == 0, "root can write everything")
class UnwritableCommandTest(RemoteCommandTestCase):
    def test_prints_nothing_when_everything_is_writable(self) -> None:
        result = self.sh(deploy.unwritable_command(TEST_ENV, ["help_tbl", "not_there_tbl"]))

        self.assertEqual((result.returncode, result.stdout), (0, ""))

    def test_lists_unwritable_source_and_help_files(self) -> None:
        (self.root / "src" / "game.cpp").chmod(0o444)
        (self.root / "lib" / "text" / "help_tbl").chmod(0o444)

        output = self.sh(deploy.unwritable_command(TEST_ENV, ["help_tbl"])).stdout

        self.assertIn(f"{self.root}/src/game.cpp", output)
        self.assertIn(f"{self.root}/lib/text/help_tbl", output)


OTHER_GROUPS = [group for group in os.getgroups() if group != os.getgid()]


class ChownCommandTest(RemoteCommandTestCase):
    def test_is_valid_sh_changes_only_what_is_inside_the_folders_and_never_follows_symlinks(self) -> None:
        command = deploy.chown_command(TEST_ENV, "someone", ["help_tbl"])

        self.assertEqual(subprocess.run(["sh", "-n", "-c", command]).returncode, 0)
        self.assertIn("sudo find -H /rots/zzz-forge-test/src /rots/zzz-forge-test/bin -mindepth 1 "
                      "-exec chown -h someone {} +", command)
        self.assertIn('sudo chown -h someone "$f"', command)
        self.assertNotIn("chown -hR", command)
        self.assertNotRegex(command, r"chown (?!-h)")
        self.assertNotRegex(command, r"chown -h someone /rots/zzz-forge-test/(src|bin|lib/text)( |$)")

    @unittest.skipUnless(OTHER_GROUPS, "needs a second group to change files to without sudo")
    def test_changes_contents_but_not_the_folders_or_anything_outside_when_run_as_chgrp(self) -> None:
        # chown needs sudo; chgrp takes the same -h flag and symlink handling, so it stands in here.
        import grp
        outside = self.root.parent / "outside"
        outside.mkdir()
        victims = [outside / "via_src", outside / "via_help"]
        for victim in victims:
            victim.write_text("outside\n")
        os.symlink(victims[0], self.root / "src" / "link_to_outside")
        (self.root / "lib" / "text" / "help_tbl").unlink()
        os.symlink(victims[1], self.root / "lib" / "text" / "help_tbl")
        (self.root / "src" / "tests").mkdir()
        (self.root / "src" / "tests" / "t.cpp").write_text("t\n")
        (self.root / "lib" / "text" / "spel_tbl").write_text("A\n#~\n")
        group = grp.getgrgid(OTHER_GROUPS[0])
        command = deploy.chown_command(TEST_ENV, "USER", ["help_tbl", "spel_tbl"])
        command = command.replace("sudo ", "").replace("chown -h USER", f"chgrp -h {group.gr_name}")

        result = self.sh(command)

        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        for inside in ("src/game.cpp", "src/tests", "src/tests/t.cpp", "lib/text/spel_tbl"):
            self.assertEqual((self.root / inside).stat().st_gid, group.gr_gid, inside)
        for folder in ("src", "bin", "lib/text"):
            self.assertEqual((self.root / folder).stat().st_gid, os.getgid(), folder)
        for victim in victims:
            self.assertEqual(victim.stat().st_gid, os.getgid(), victim)


class BackupCommandTest(RemoteCommandTestCase):
    def test_copies_src_and_help_files_keeping_timestamps(self) -> None:
        stamp = time.time() - 3600
        os.utime(self.root / "src" / "game.o", (stamp, stamp))

        result = self.sh(deploy.backup_command(TEST_ENV, ["help_tbl", "not_there_tbl"]))

        self.assertEqual(result.returncode, 0, result.stderr)
        backup = self.root / "src" / "backup"
        self.assertEqual((backup / "game.cpp").read_text(), "old source\n")
        self.assertEqual(int((backup / "game.o").stat().st_mtime), int(stamp))
        self.assertEqual((backup / "lib-text" / "help_tbl").read_text(), "old help\n#~\n")
        self.assertFalse((backup / "lib-text" / "not_there_tbl").exists())
        self.assertFalse((self.root / "src" / "backup.new").exists())

    def test_second_run_replaces_the_backup_without_nesting(self) -> None:
        self.assertEqual(self.sh(deploy.backup_command(TEST_ENV, ["help_tbl"])).returncode, 0)
        (self.root / "src" / "backup" / "stale.txt").write_text("from an older backup\n")
        (self.root / "src" / "game.cpp").write_text("newer source\n")

        self.assertEqual(self.sh(deploy.backup_command(TEST_ENV, ["help_tbl"])).returncode, 0)

        backup = self.root / "src" / "backup"
        self.assertEqual((backup / "game.cpp").read_text(), "newer source\n")
        self.assertFalse((backup / "stale.txt").exists())
        self.assertFalse((backup / "backup").exists())

    @unittest.skipIf(os.geteuid() == 0, "root can read everything")
    def test_failed_copy_keeps_the_previous_backup(self) -> None:
        self.assertEqual(self.sh(deploy.backup_command(TEST_ENV, ["help_tbl"])).returncode, 0)
        (self.root / "src" / "backup" / "marker.txt").write_text("previous backup\n")
        unreadable = self.root / "src" / "secret.cpp"
        unreadable.write_text("x\n")
        unreadable.chmod(0o000)
        self.addCleanup(unreadable.chmod, 0o644)

        result = self.sh(deploy.backup_command(TEST_ENV, ["help_tbl"]))

        self.assertNotEqual(result.returncode, 0)
        self.assertTrue((self.root / "src" / "backup" / "marker.txt").exists())

    def test_revert_restores_source_and_help_files(self) -> None:
        (self.root / "src" / "Makefile").write_text("all:\n\ttouch ../bin/ageland\n")
        self.assertEqual(self.sh(deploy.backup_command(TEST_ENV, ["help_tbl"])).returncode, 0)
        (self.root / "src" / "game.cpp").write_text("broken new source\n")
        (self.root / "lib" / "text" / "help_tbl").write_text("broken new help\n")
        (self.root / "src" / deploy.DEPLOY_MARKER).write_text("")

        result = self.sh(deploy.revert_command(TEST_ENV))

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual((self.root / "src" / "game.cpp").read_text(), "old source\n")
        self.assertEqual((self.root / "lib" / "text" / "help_tbl").read_text(), "old help\n#~\n")
        self.assertFalse((self.root / "src" / "lib-text").exists())
        self.assertFalse((self.root / "src" / deploy.DEPLOY_MARKER).exists())

    def test_revert_relinks_even_when_the_binary_is_newer_than_the_restored_objects(self) -> None:
        (self.root / "src" / "Makefile").write_text(
            "all: ../bin/ageland\n../bin/ageland: game.o\n\ttouch ../bin/ageland\n")
        stale = time.time() - 3600
        os.utime(self.root / "src" / "game.o", (stale, stale))
        self.assertEqual(self.sh(deploy.backup_command(TEST_ENV, ["help_tbl"])).returncode, 0)
        binary = self.root / "bin" / "ageland"
        binary.write_text("newer than the backed-up object")
        newer = time.time() - 10
        os.utime(binary, (newer, newer))

        result = self.sh(deploy.revert_command(TEST_ENV))

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertGreater(binary.stat().st_mtime, newer)

    def test_revert_tolerates_an_empty_help_backup(self) -> None:
        (self.root / "src" / "Makefile").write_text("all:\n\ttouch ../bin/ageland\n")
        # A help name that does not exist on the server, so backup/lib-text ends up empty.
        self.assertEqual(self.sh(deploy.backup_command(TEST_ENV, ["not_there_tbl"])).returncode, 0)
        self.assertEqual(list((self.root / "src" / "backup" / "lib-text").iterdir()), [])

        result = self.sh(deploy.revert_command(TEST_ENV))

        self.assertEqual(result.returncode, 0, result.stderr)

    def test_rejects_an_unsafe_env(self) -> None:
        with self.assertRaises(deploy.DeployError):
            deploy.backup_command(deploy.Env("bad", "../etc", deploy.CYAN, backup=True, tag_prefix=None), [])


class SourceEditCommandTest(RemoteCommandTestCase):
    def setUp(self) -> None:
        super().setUp()
        self.header = self.root / "src" / "big_brother.h"
        self.header.write_text("#ifndef USE_BIG_BROTHER\n#define USE_BIG_BROTHER 1\n#endif\n")
        self.command = deploy.source_edit_command(TEST_ENV, deploy.BIG_BROTHER_OFF)

    def test_turns_big_brother_off(self) -> None:
        result = self.sh(self.command)

        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(self.header.read_text(), "#ifndef USE_BIG_BROTHER\n#define USE_BIG_BROTHER 0\n#endif\n")

    def test_stops_when_the_line_is_already_changed(self) -> None:
        self.sh(self.command)

        result = self.sh(self.command)

        self.assertNotEqual(result.returncode, 0)
        self.assertIn("expected exactly one line: #define USE_BIG_BROTHER 1", result.stdout)

    def test_stops_without_editing_when_the_line_appears_twice(self) -> None:
        original = "#define USE_BIG_BROTHER 1\n#define USE_BIG_BROTHER 1\n"
        self.header.write_text(original)

        self.assertNotEqual(self.sh(self.command).returncode, 0)
        self.assertEqual(self.header.read_text(), original)

    def test_turns_big_brother_off_in_a_crlf_file_and_keeps_its_line_endings(self) -> None:
        # src/big_brother.h is stored in git with CRLF line endings, so the uploaded copy ends in "\r\n".
        self.header.write_bytes(b"#ifndef USE_BIG_BROTHER\r\n#define USE_BIG_BROTHER 1\r\n#endif\r\n")

        result = self.sh(self.command)

        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(self.header.read_bytes(), b"#ifndef USE_BIG_BROTHER\r\n#define USE_BIG_BROTHER 0\r\n#endif\r\n")

    def test_stops_without_editing_when_a_crlf_file_is_already_changed(self) -> None:
        original = b"#define USE_BIG_BROTHER 0\r\n"
        self.header.write_bytes(original)

        result = self.sh(self.command)

        self.assertNotEqual(result.returncode, 0)
        self.assertIn("expected exactly one line: #define USE_BIG_BROTHER 1", result.stdout)
        self.assertEqual(self.header.read_bytes(), original)

    def test_edits_the_real_header_from_the_repo(self) -> None:
        self.header.write_bytes((deploy.REPO_ROOT / "src" / "big_brother.h").read_bytes())

        result = self.sh(self.command)

        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        lines = self.header.read_bytes().splitlines()
        self.assertEqual(lines.count(b"#define USE_BIG_BROTHER 0\r") + lines.count(b"#define USE_BIG_BROTHER 0"), 1)
        self.assertNotIn(b"#define USE_BIG_BROTHER 1", self.header.read_bytes())


class RestartCommandTest(unittest.TestCase):
    def test_restarts_the_service_with_sudo_and_changes_no_file(self) -> None:
        command = deploy.restart_command(deploy.ENVS["test"])

        self.assertEqual(command, "sudo systemctl restart rotsbuilding")
        self.assertNotIn("chown", command)
        self.assertNotIn("chmod", command)

    def test_an_env_without_a_service_has_no_restart_command(self) -> None:
        with self.assertRaises(deploy.DeployError):
            deploy.restart_command(TEST_ENV)


class BuildCommandTest(RemoteCommandTestCase):
    def test_build_and_revert_run_make_with_two_jobs(self) -> None:
        for command in (deploy.build_command(TEST_ENV), deploy.revert_command(TEST_ENV)):
            self.assertIn("make all -j2", command)
            self.assertNotIn("-j6", command)

    def test_passes_when_make_rebuilds_the_binary(self) -> None:
        (self.root / "src" / "Makefile").write_text("clean:\n\trm -f *.o\nall:\n\ttouch ../bin/ageland\n")

        result = self.sh(deploy.build_command(TEST_ENV))

        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertFalse((self.root / "src" / "game.o").exists())

    def test_fails_when_the_binary_is_stale(self) -> None:
        (self.root / "src" / "Makefile").write_text("clean:\n\t@true\nall:\n\t@true\n")
        binary = self.root / "bin" / "ageland"
        binary.write_text("old")
        os.utime(binary, (time.time() - 3600, time.time() - 3600))

        result = self.sh(deploy.build_command(TEST_ENV))

        self.assertNotEqual(result.returncode, 0)
        self.assertIn("not rebuilt", result.stdout)

    def test_fails_when_make_fails(self) -> None:
        (self.root / "src" / "Makefile").write_text("clean:\n\t@true\nall:\n\t@false\n")

        self.assertNotEqual(self.sh(deploy.build_command(TEST_ENV)).returncode, 0)


class RevertRemoteCommandTest(RemoteCommandTestCase):
    def setUp(self) -> None:
        super().setUp()
        (self.root / "src" / "Makefile").write_text("all: ../bin/ageland\n../bin/ageland: game.o\n\ttouch ../bin/ageland\n")
        self.assertEqual(self.sh(deploy.backup_command(TEST_ENV, ["help_tbl"])).returncode, 0)
        self.backup = self.root / "src" / "backup"
        # The backed-up object is older than the running binary, so only a forced relink rebuilds it.
        os.utime(self.backup / "game.o", (time.time() - 3600, time.time() - 3600))
        self.binary = self.root / "bin" / "ageland"
        self.binary.write_text("binary from the bad deploy")
        os.utime(self.binary, (time.time() - 100, time.time() - 100))
        (self.root / "src" / "game.cpp").write_text("broken new source\n")
        (self.root / "lib" / "text" / "help_tbl").write_text("broken new help\n")
        self.marker = self.root / "src" / deploy.DEPLOY_MARKER
        self.marker.write_text("")
        self.command = deploy.revert_remote_command(TEST_ENV)

    def test_refuses_when_a_symlink_points_outside_the_port(self) -> None:
        outside = self.root.parent / "outside"
        outside.mkdir()
        os.symlink(outside, self.root / "src" / "link_to_outside")

        result = self.sh(self.command)

        self.assertNotEqual(result.returncode, 0)
        self.assertIn("outside", result.stdout)
        self.assertEqual((self.root / "src" / "game.cpp").read_text(), "broken new source\n")
        self.assertTrue(self.marker.exists())

    def test_restores_relinks_and_clears_the_marker(self) -> None:
        start = int(time.time())

        result = self.sh(self.command)

        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual((self.root / "src" / "game.cpp").read_text(), "old source\n")
        self.assertEqual((self.root / "lib" / "text" / "help_tbl").read_text(), "old help\n#~\n")
        self.assertGreaterEqual(int(self.binary.stat().st_mtime), start)
        self.assertFalse(self.marker.exists())
        self.assertFalse((self.root / "src" / "lib-text").exists())

    def test_refuses_when_there_is_no_backup(self) -> None:
        subprocess.run(["rm", "-rf", str(self.backup)], check=True)

        result = self.sh(self.command)

        self.assertNotEqual(result.returncode, 0)
        self.assertIn("no backup", result.stdout)
        self.assertEqual((self.root / "src" / "game.cpp").read_text(), "broken new source\n")
        self.assertTrue(self.marker.exists())

    def test_failed_make_keeps_the_marker(self) -> None:
        (self.backup / "Makefile").write_text("all:\n\t@false\n")

        self.assertNotEqual(self.sh(self.command).returncode, 0)

        self.assertTrue(self.marker.exists())

    def test_reports_a_binary_that_was_not_relinked(self) -> None:
        (self.backup / "Makefile").write_text("all:\n\t@true\n")

        result = self.sh(self.command)

        self.assertNotEqual(result.returncode, 0)
        self.assertIn("not relinked", result.stdout)


class SftpBatchTest(unittest.TestCase):
    def test_uploads_src_then_each_help_file(self) -> None:
        batch = deploy.sftp_batch(TEST_ENV, Path("/home/me/RotS"), ["help", "help_tbl"])

        self.assertEqual(batch, "\n".join([
            'lcd "/home/me/RotS/src"',
            'cd "/rots/zzz-forge-test/src"',
            "put -r *",
            'lcd "/home/me/RotS/lib/text"',
            'cd "/rots/zzz-forge-test/lib/text"',
            'put "help"',
            'put "help_tbl"',
        ]) + "\n")


# ---------------------------------------------------------------------------------------------
# Running commands
# ---------------------------------------------------------------------------------------------


class SshRunnerTest(unittest.TestCase):
    def setUp(self) -> None:
        self.runner = deploy.SshRunner(SERVER, Path("/tmp/rots-deploy-test"))

    def test_connect_opens_a_master_connection(self) -> None:
        self.assertEqual(self.runner.connect_args(), ["ssh", "-M", "-S", "/tmp/rots-deploy-test/ssh-master", "-fN",
                                                      "-p", "2222", "someone@example.org"])

    def test_remote_reuses_the_master_and_can_request_a_tty(self) -> None:
        self.assertEqual(self.runner.remote_args("make"), ["ssh", "-S", "/tmp/rots-deploy-test/ssh-master", "-p", "2222",
                                                           "someone@example.org", "sh -c make"])
        self.assertEqual(self.runner.remote_args("sudo true", tty=True)[3], "-t")

    def test_remote_wraps_the_command_in_sh_dash_c_so_it_survives_any_login_shell(self) -> None:
        command = '[ -d "$d" ] || { echo "a b"; exit 1; }'

        args = self.runner.remote_args(command)

        self.assertEqual(shlex.split(args[-1]), ["sh", "-c", command])

    def test_sftp_reuses_the_master(self) -> None:
        self.assertEqual(self.runner.sftp_args(Path("/tmp/b")), ["sftp", "-o",
                         "ControlPath=/tmp/rots-deploy-test/ssh-master", "-P", "2222", "-b", "/tmp/b",
                         "someone@example.org"])

    def test_sftp_writes_the_batch_file_and_runs_it(self) -> None:
        with tempfile.TemporaryDirectory() as work_dir:
            runner = deploy.SshRunner(SERVER, Path(work_dir))
            with mock.patch.object(deploy, "run_command") as run:
                runner.sftp("put -r *\n")

            batch_path = Path(work_dir) / "upload.sftp"
            run.assert_called_once_with(runner.sftp_args(batch_path), "sftp upload")
            self.assertEqual(batch_path.read_text(), "put -r *\n")

    def test_close_only_runs_after_connecting(self) -> None:
        with mock.patch.object(deploy.subprocess, "run") as run:
            self.runner.close()
            run.assert_not_called()
            self.runner.connected = True
            self.runner.close()
            run.assert_called_once()
            self.assertFalse(self.runner.connected)


class RunCommandTest(unittest.TestCase):
    def test_returns_captured_output(self) -> None:
        self.assertEqual(deploy.run_command([sys.executable, "-c", "print('hello')"], "hello", capture=True), "hello\n")

    def test_failure_raises_with_status_and_output(self) -> None:
        with self.assertRaisesRegex(deploy.DeployError, "thing exited with status 3: oops"):
            deploy.run_command([sys.executable, "-c", "import sys; print('oops'); sys.exit(3)"], "thing", capture=True)


# ---------------------------------------------------------------------------------------------
# The deploy
# ---------------------------------------------------------------------------------------------


class FailureReportTest(unittest.TestCase):
    def test_steps_1_to_4_say_contents_are_unchanged(self) -> None:
        for step in (1, 2, 3, 4):
            with self.subTest(step=step):
                report = deploy.failure_report(TEST_ENV, step, "boom", color=False)

                self.assertIn("the source and help file contents on the server are unchanged", report)

    def test_steps_5_to_7_on_a_backup_env_warn_the_next_deploy_will_refuse_to_run(self) -> None:
        for step in (5, 6, 7):
            with self.subTest(step=step):
                report = deploy.failure_report(TEST_ENV, step, "boom", color=False)

                self.assertIn(f"To revert: {deploy.revert_command(TEST_ENV)}", report)
                self.assertIn(deploy.DEPLOY_MARKER, report)
                self.assertIn("refuse to run", report)

    def test_step_7_warns_the_binary_may_have_been_relinked_away(self) -> None:
        report = deploy.failure_report(TEST_ENV, 7, "boom", color=False)

        self.assertIn("../bin/ageland~", report)
        self.assertIn("restart", report)

    def test_step_7_interrupted_warns_the_remote_make_may_still_be_running(self) -> None:
        report = deploy.failure_report(TEST_ENV, 7, "interrupted", color=False)

        self.assertIn("remote make may still be running", report)

    def test_step_7_not_interrupted_has_no_still_running_warning(self) -> None:
        report = deploy.failure_report(TEST_ENV, 7, "boom", color=False)

        self.assertNotIn("still be running", report)

    def test_step_8_is_unchanged(self) -> None:
        report = deploy.failure_report(TEST_ENV, 8, "boom", color=False)

        self.assertIn("only the local tag failed", report)
        self.assertNotIn("restart", report)

    def test_step_8_with_restart_says_the_port_was_not_restarted(self) -> None:
        report = deploy.failure_report(deploy.ENVS["test"], 8, "boom", color=False, restart=True)

        self.assertIn("only the local tag failed", report)
        self.assertIn("not restarted; on the server: sudo systemctl restart rotsbuilding", report)

    def test_step_9_names_the_restart_command_and_no_revert(self) -> None:
        report = deploy.failure_report(deploy.ENVS["test"], 9, "boom", color=False, server=SERVER, restart=True)

        self.assertIn("FAILED at step 9 (restart)", report)
        self.assertIn("only the restart failed", report)
        self.assertIn("sudo systemctl restart rotsbuilding", report)
        self.assertNotIn("revert", report)

    def test_coders_keeps_the_no_backup_line_plus_the_ageland_warning_at_step_7(self) -> None:
        coders = deploy.ENVS["coders"]

        report = deploy.failure_report(coders, 7, "boom", color=False)

        self.assertIn("keeps no backup", report)
        self.assertIn("../bin/ageland~", report)

    def test_coders_at_steps_5_and_6_has_no_ageland_warning(self) -> None:
        for step in (5, 6):
            with self.subTest(step=step):
                report = deploy.failure_report(deploy.ENVS["coders"], step, "boom", color=False)

                self.assertIn("keeps no backup", report)
                self.assertNotIn("ageland~", report)


class FakeCheckout:
    repo = Path("/home/me/RotS")
    tag_remote = "git@github.com:returnoftheshadow/RotS_Live.git"

    def __init__(self, problems=(), warnings=(), fetch_error=None, push_error=None) -> None:
        self.problems = list(problems)
        self.warnings = list(warnings)
        self.prepared = []
        self.tags = []
        self.events = []
        self.fetch_error = fetch_error
        self.push_error = push_error

    def fetch_tags(self, prefix):
        self.events.append(("fetch", prefix))
        if self.fetch_error:
            raise deploy.DeployError(self.fetch_error)

    def push_tag(self, name):
        self.events.append(("push", name))
        if self.push_error:
            raise deploy.DeployError(self.push_error)

    def prepare(self, env, dry_run):
        self.prepared.append(dry_run)
        return self.warnings

    def head(self):
        return "abc1234def5678", "Merge pull request #300"

    def help_files(self):
        return ["help", "help_tbl"]

    def help_problems(self):
        return self.problems

    def create_tag(self, env, sha, help_names, day):
        self.tags.append((env.name, sha, list(help_names), day))
        name = f"{env.tag_prefix}{day.isoformat()}"
        self.events.append(("tag", name))
        return name


class FakeRunner:
    def __init__(self, fail_when=lambda call: False, unwritable=("",)) -> None:
        self.calls = []
        self.fail_when = fail_when
        self.unwritable = list(unwritable)

    def _record(self, call):
        self.calls.append(call)
        if self.fail_when(call):
            raise deploy.DeployError("boom")

    def connect(self):
        self._record(("connect",))

    def remote(self, command, capture=False, tty=False):
        self._record(("remote", command, tty))
        if capture:
            return self.unwritable.pop(0) if len(self.unwritable) > 1 else self.unwritable[0]
        return ""

    def sftp(self, batch):
        self._record(("sftp", batch))

    def close(self):
        self.calls.append(("close",))

    def kinds(self):
        labels = []
        for call in self.calls:
            if call[0] != "remote":
                labels.append(call[0])
            elif "find backup -mindepth" in call[1]:
                labels.append("revert")
            elif "readlink -m" in call[1]:
                labels.append("links")
            elif call[1].startswith("touch ") and deploy.DEPLOY_MARKER in call[1]:
                labels.append("mark")
            elif call[1].startswith("rm -f ") and deploy.DEPLOY_MARKER in call[1]:
                labels.append("finish")
            elif deploy.DEPLOY_MARKER in call[1]:
                labels.append("unfinished")
            elif "sudo chown" in call[1]:
                labels.append("chown")
            elif "backup.new" in call[1]:
                labels.append("backup")
            elif "! -writable" in call[1]:
                labels.append("unwritable")
            elif "missing directory" in call[1]:
                labels.append("dirs")
            elif "sed -i" in call[1]:
                labels.append("edit")
            elif "systemctl restart" in call[1]:
                labels.append("restart")
            elif "make all" in call[1]:
                labels.append("build")
            else:
                labels.append("remote?")
        return labels


class DeployTest(unittest.TestCase):
    DAY = datetime.date(2026, 9, 13)

    def run_deploy(self, env_name, checkout=None, runner=None, dry_run=False, restart=False):
        self.checkout = checkout or FakeCheckout()
        self.runner = runner or FakeRunner()
        self.output = []
        status = deploy.deploy(deploy.ENVS[env_name], SERVER, self.checkout, self.runner, dry_run=dry_run,
                               restart=restart, out=self.output.append, today=self.DAY)
        return status

    def text(self):
        return "\n".join(self.output)

    def test_successful_deploy_runs_every_step_in_order_and_tags(self) -> None:
        status = self.run_deploy("test")

        self.assertEqual(status, 0)
        self.assertEqual(self.runner.kinds(), ["connect", "dirs", "links", "unfinished", "unwritable", "backup",
                                               "mark", "sftp", "build", "finish", "close"])
        self.assertEqual(self.checkout.tags, [("test", "abc1234def5678", ["help", "help_tbl"], self.DAY)])
        self.assertEqual(self.checkout.prepared, [False])
        self.assertIn("Tag: test-2026-09-13.", self.text())
        self.assertIn("someone@example.org:/rots/dev-building4802", self.text())
        self.assertIn("Restart the port to run the new build.", self.text())

    def test_restart_runs_after_the_tag_through_a_tty(self) -> None:
        self.assertEqual(self.run_deploy("test", restart=True), 0)

        self.assertEqual(self.runner.kinds(), ["connect", "dirs", "links", "unfinished", "unwritable", "backup",
                                               "mark", "sftp", "build", "finish", "restart", "close"])
        self.assertEqual(len(self.checkout.tags), 1)
        restart = [call for call in self.runner.calls if call[0] == "remote" and "systemctl" in call[1]][0]
        self.assertEqual(restart[1:], ("sudo systemctl restart rotsbuilding", True))
        self.assertIn("Restarted rotsbuilding.", self.text())
        self.assertNotIn("Restart the port", self.text())

    def test_failed_restart_reports_step_9_after_tagging(self) -> None:
        runner = FakeRunner(fail_when=lambda call: call[0] == "remote" and "systemctl" in call[1])

        self.assertEqual(self.run_deploy("test", runner=runner, restart=True), 1)

        self.assertEqual(self.runner.kinds()[-2:], ["restart", "close"])
        self.assertEqual(len(self.checkout.tags), 1)
        self.assertIn("FAILED at step 9 (restart)", self.text())
        self.assertNotIn("Deployed abc1234", self.text())

    def test_failed_tag_does_not_restart(self) -> None:
        checkout = FakeCheckout()
        checkout.create_tag = mock.Mock(side_effect=deploy.DeployError("tag boom"))

        self.assertEqual(self.run_deploy("test", checkout=checkout, restart=True), 1)

        self.assertNotIn("restart", self.runner.kinds())
        self.assertIn("not restarted", self.text())

    def test_tagged_deploy_fetches_tags_then_tags_then_pushes_the_tag(self) -> None:
        self.assertEqual(self.run_deploy("test"), 0)

        self.assertEqual(self.checkout.events,
                         [("fetch", "test-"), ("tag", "test-2026-09-13"), ("push", "test-2026-09-13")])
        self.assertIn("Pushed tag test-2026-09-13 to the main repo.", self.text())
        self.assertIn("Tag: test-2026-09-13.", self.text())
        self.assertNotIn("warning", self.text())

    def test_failed_push_warns_with_the_push_command_and_still_restarts(self) -> None:
        checkout = FakeCheckout(push_error="git push failed: Permission denied")

        self.assertEqual(self.run_deploy("test", checkout=checkout, restart=True), 0)

        self.assertIn("restart", self.runner.kinds())
        self.assertIn("warning: tag test-2026-09-13 was not pushed: git push failed: Permission denied", self.text())
        self.assertIn("git push git@github.com:returnoftheshadow/RotS_Live.git refs/tags/test-2026-09-13",
                      self.text())
        self.assertIn("Tag: test-2026-09-13 (local only; the push failed).", self.text())

    def test_failed_fetch_warns_and_still_tags_and_pushes(self) -> None:
        checkout = FakeCheckout(fetch_error="git fetch failed: Could not resolve host")

        self.assertEqual(self.run_deploy("test", checkout=checkout), 0)

        self.assertEqual([event[0] for event in checkout.events], ["fetch", "tag", "push"])
        self.assertIn("warning: could not fetch test-* tags from the main repo", self.text())
        self.assertIn("Could not resolve host", self.text())

    def test_untagged_test_target_neither_fetches_nor_pushes(self) -> None:
        self.assertEqual(self.run_deploy("zzz-forge-test"), 0)

        self.assertEqual(self.checkout.events, [])

    def test_4k_edits_the_source_after_upload_and_before_build(self) -> None:
        self.assertEqual(self.run_deploy("4k"), 0)

        self.assertEqual(self.runner.kinds(), ["connect", "dirs", "links", "unfinished", "unwritable", "backup",
                                               "mark", "sftp", "edit", "build", "finish", "close"])
        self.assertIn("USE_BIG_BROTHER", self.text())

    def test_coders_skips_the_backup(self) -> None:
        self.assertEqual(self.run_deploy("coders"), 0)

        self.assertEqual(self.runner.kinds(), ["connect", "dirs", "links", "unwritable", "sftp", "build", "close"])

    def test_test_target_is_not_tagged(self) -> None:
        self.assertEqual(self.run_deploy("zzz-forge-test"), 0)

        self.assertEqual(self.checkout.tags, [])

    def test_help_problems_stop_before_connecting(self) -> None:
        status = self.run_deploy("live", checkout=FakeCheckout(problems=["lib/text/help_tbl:92: bad"]))

        self.assertEqual(status, 1)
        self.assertEqual(self.runner.kinds(), ["close"])
        self.assertIn("lib/text/help_tbl:92: bad", self.text())
        self.assertIn("FAILED at step 1", self.text())

    def test_failed_pre_check_uploads_nothing(self) -> None:
        runner = FakeRunner(fail_when=lambda call: call[0] == "remote" and "missing directory" in call[1])

        self.assertEqual(self.run_deploy("test", runner=runner), 1)

        self.assertEqual(self.runner.kinds(), ["connect", "dirs", "close"])
        self.assertIn("FAILED at step 3", self.text())
        self.assertIn("Nothing was uploaded", self.text())
        self.assertEqual(self.checkout.tags, [])

    def test_symlink_pointing_outside_the_port_stops_before_anything_changes(self) -> None:
        runner = FakeRunner(fail_when=lambda call: call[0] == "remote" and "readlink -m" in call[1])

        self.assertEqual(self.run_deploy("test", runner=runner), 1)

        self.assertEqual(self.runner.kinds(), ["connect", "dirs", "links", "close"])
        self.assertIn("FAILED at step 3", self.text())
        self.assertIn("Nothing was uploaded", self.text())
        self.assertEqual(self.checkout.tags, [])

    def test_unfinished_deploy_marker_stops_before_backup(self) -> None:
        runner = FakeRunner(fail_when=lambda call: call[0] == "remote" and "did not finish" in call[1])

        self.assertEqual(self.run_deploy("test", runner=runner), 1)

        self.assertEqual(self.runner.kinds(), ["connect", "dirs", "links", "unfinished", "close"])
        self.assertIn("FAILED at step 3", self.text())
        self.assertEqual(self.checkout.tags, [])

    def test_unwritable_files_are_chowned_with_a_tty_then_rechecked(self) -> None:
        runner = FakeRunner(unwritable=["/rots/dev-building4802/src/game.cpp\n", ""])

        self.assertEqual(self.run_deploy("test", runner=runner), 0)

        self.assertEqual(self.runner.kinds()[:8],
                         ["connect", "dirs", "links", "unfinished", "unwritable", "chown", "unwritable", "backup"])
        chown = [call for call in self.runner.calls if call[0] == "remote" and "sudo chown" in call[1]][0]
        self.assertTrue(chown[2])
        self.assertIn("sudo may ask for a password", self.text())
        self.assertIn("/rots/dev-building4802/src/game.cpp", self.text())

    def test_an_unwritable_folder_stops_without_asking_for_sudo(self) -> None:
        runner = FakeRunner(unwritable=["/rots/dev-building4802/lib/text\n"])

        self.assertEqual(self.run_deploy("test", runner=runner), 1)

        self.assertNotIn("chown", self.runner.kinds())
        self.assertNotIn("backup", self.runner.kinds())
        self.assertIn("FAILED at step 3", self.text())
        self.assertIn("/rots/dev-building4802/lib/text", self.text())
        self.assertIn("fix", self.text())

    def test_still_unwritable_after_chown_stops_before_backup(self) -> None:
        runner = FakeRunner(unwritable=["/rots/dev-building4802/src/game.cpp\n"])

        self.assertEqual(self.run_deploy("test", runner=runner), 1)

        self.assertIn("chown", self.runner.kinds())
        self.assertNotIn("backup", self.runner.kinds())
        self.assertIn("still not writable", self.text())

    def test_failed_upload_prints_the_revert_command_and_closes(self) -> None:
        runner = FakeRunner(fail_when=lambda call: call[0] == "sftp")

        self.assertEqual(self.run_deploy("test", runner=runner), 1)

        self.assertEqual(self.runner.kinds()[-1], "close")
        self.assertIn("FAILED at step 5", self.text())
        self.assertIn("To revert: scripts/deploy.py revert test someone@example.org 2222", self.text())
        self.assertIn(deploy.revert_command(deploy.ENVS["test"]), self.text())
        self.assertEqual(self.checkout.tags, [])

    def test_failed_build_on_coders_says_there_is_no_backup(self) -> None:
        runner = FakeRunner(fail_when=lambda call: call[0] == "remote" and "make all" in call[1])

        self.assertEqual(self.run_deploy("coders", runner=runner), 1)

        self.assertIn("keeps no backup", self.text())

    def test_interrupt_closes_the_connection(self) -> None:
        runner = FakeRunner()
        runner.connect = mock.Mock(side_effect=KeyboardInterrupt)

        self.assertEqual(self.run_deploy("test", runner=runner), 130)

        self.assertEqual(self.runner.kinds(), ["close"])
        self.assertIn("FAILED at step 2 (connect): interrupted", self.text())

    def test_unexpected_exception_still_produces_a_report_and_closes(self) -> None:
        runner = FakeRunner()

        def raising_sftp(batch):
            runner.calls.append(("sftp", batch))
            raise OSError("disk full")

        runner.sftp = raising_sftp

        with self.assertRaises(OSError):
            self.run_deploy("test", runner=runner)

        self.assertIn("FAILED at step 5", self.text())
        self.assertIn("unexpected error", self.text())
        self.assertEqual(self.runner.kinds()[-1], "close")

    def test_dry_run_prints_every_step_and_runs_nothing_remote(self) -> None:
        for name in deploy.ENVS:
            with self.subTest(env=name):
                self.assertEqual(self.run_deploy(name, dry_run=True), 0)

                self.assertEqual(self.runner.kinds(), ["close"])
                self.assertEqual(self.checkout.prepared, [True])
                self.assertEqual(self.checkout.tags, [])
                for number in range(1, 10):
                    self.assertIn(f"== {number}. {deploy.STEP_TITLES[number]}", self.text())
                self.assertIn("none: --restart not given", self.text())
                self.assertNotIn("systemctl", self.text())
                self.assertIn("ssh -M -S", self.text())
                self.assertIn('put "help_tbl"', self.text())
                self.assertIn("git pull --ff-only", self.text())
                self.assertNotIn("chmod", self.text())
                self.assertIn("-mindepth 1 -exec chown -h someone {} +", self.text())
                self.assertIn("readlink -m", self.text())
                env = deploy.ENVS[name]
                self.assertEqual(self.checkout.events, [])
                if env.tag_prefix:
                    self.assertIn(f"  git fetch --no-tags git@github.com:returnoftheshadow/RotS_Live.git "
                                  f"'refs/tags/{env.tag_prefix}*:refs/tags/{env.tag_prefix}*'", self.text())
                    self.assertIn(f"  git push git@github.com:returnoftheshadow/RotS_Live.git "
                                  f"refs/tags/{env.tag_prefix}YYYY-MM-DD[-N]", self.text())
                else:
                    self.assertNotIn("git push", self.text())
                    self.assertNotIn("git fetch", self.text())
                shell = deploy.SshRunner(SERVER, Path(deploy.SOCKET_PARENT) / "rots-deploy-XXXXXX")

                def ssh_line(command: str) -> str:
                    return "  " + shlex.join(shell.remote_args(command))

                marker_lines = [ssh_line(deploy.unfinished_deploy_command(env, SERVER)),
                                ssh_line(deploy.mark_deploy_started_command(env)),
                                ssh_line(deploy.mark_deploy_finished_command(env))]
                for line in marker_lines:
                    if env.backup:
                        self.assertIn(line, self.text())
                    else:
                        self.assertNotIn(line, self.text())

    def test_dry_run_with_restart_prints_the_restart_and_runs_nothing(self) -> None:
        self.assertEqual(self.run_deploy("test", dry_run=True, restart=True), 0)

        self.assertEqual(self.runner.kinds(), ["close"])
        self.assertIn("== 9. restart", self.text())
        self.assertIn("-t -p 2222 someone@example.org 'sh -c '\"'\"'sudo systemctl restart rotsbuilding", self.text())

    def test_warnings_are_shown(self) -> None:
        self.run_deploy("zzz-forge-test", checkout=FakeCheckout(warnings=["on 'feat/x'"]))

        self.assertIn("warning: on 'feat/x'", self.text())


class MainTest(unittest.TestCase):
    def assert_refused(self, argv, message):
        stderr = io.StringIO()
        with mock.patch.object(deploy, "deploy") as not_deploy, mock.patch.object(deploy, "revert") as not_revert, \
                contextlib.redirect_stderr(stderr), self.assertRaises(SystemExit) as caught:
            deploy.main(argv)
        self.assertEqual(caught.exception.code, 2)
        self.assertIn(message, stderr.getvalue())
        not_deploy.assert_not_called()
        not_revert.assert_not_called()

    def test_wires_arguments_into_deploy(self) -> None:
        with mock.patch.object(deploy, "deploy", return_value=0) as run:
            self.assertEqual(deploy.main(["deploy", "zzz-forge-test-4k", "someone@example.org", "2222", "--dry-run"]), 0)

        env, server, checkout, runner = run.call_args.args
        self.assertEqual(env, deploy.ENVS["zzz-forge-test-4k"])
        self.assertEqual(server, SERVER)
        self.assertEqual(checkout.repo, deploy.REPO_ROOT)
        self.assertEqual(checkout.tag_remote, "git@github.com:returnoftheshadow/RotS_Live.git")
        self.assertTrue(str(runner.socket).startswith("/tmp/rots-deploy-"))
        self.assertTrue(run.call_args.kwargs["dry_run"])
        self.assertFalse(run.call_args.kwargs["restart"])

    def test_wires_restart_into_deploy_for_test(self) -> None:
        with mock.patch.object(deploy, "deploy", return_value=0) as run:
            self.assertEqual(deploy.main(["deploy", "test", "someone@example.org", "2222", "--restart"]), 0)

        self.assertTrue(run.call_args.kwargs["restart"])

    def test_envs_that_are_not_approved_cannot_deploy_or_revert(self) -> None:
        for command in ("deploy", "revert"):
            for name in ("live", "4k", "coders"):
                with self.subTest(command=command, env=name):
                    self.assert_refused([command, name, "someone@example.org", "2222"],
                                        f"{name} cannot be deployed or reverted for now; "
                                        "only test, zzz-forge-test, zzz-forge-test-4k can")
        self.assert_refused(["deploy", "live", "someone@example.org", "2222", "--dry-run"], "live cannot be deployed")

    def test_restart_is_refused_for_the_forge_test_targets(self) -> None:
        for name in ("zzz-forge-test", "zzz-forge-test-4k"):
            with self.subTest(env=name):
                self.assert_refused(["deploy", name, "someone@example.org", "2222", "--restart"],
                                    f"--restart is not available for {name}")

    def test_wires_revert_arguments_into_revert(self) -> None:
        with mock.patch.object(deploy, "revert", return_value=0) as run, \
                mock.patch.object(deploy, "deploy") as not_deploy:
            self.assertEqual(deploy.main(["revert", "zzz-forge-test", "someone@example.org", "2222"]), 0)

        env, server, runner = run.call_args.args
        self.assertEqual(env, deploy.ENVS["zzz-forge-test"])
        self.assertEqual(server, SERVER)
        self.assertTrue(str(runner.socket).startswith("/tmp/rots-deploy-"))
        not_deploy.assert_not_called()


class RevertTest(unittest.TestCase):
    def run_revert(self, env_name, runner=None):
        self.runner = runner or FakeRunner()
        self.output = []
        return deploy.revert(deploy.ENVS[env_name], SERVER, self.runner, out=self.output.append)

    def text(self):
        return "\n".join(self.output)

    def test_connects_runs_the_revert_and_closes(self) -> None:
        self.assertEqual(self.run_revert("test"), 0)

        self.assertEqual(self.runner.kinds(), ["connect", "revert", "close"])
        self.assertEqual(self.runner.calls[1][1], deploy.revert_remote_command(deploy.ENVS["test"]))
        self.assertIn("someone@example.org:/rots/dev-building4802", self.text())
        self.assertIn("Reverted test", self.text())

    def test_coders_has_no_backup_and_never_connects(self) -> None:
        self.assertEqual(self.run_revert("coders"), 1)

        self.assertEqual(self.runner.calls, [])
        self.assertIn("keeps no backup", self.text())

    def test_remote_failure_is_reported_and_closes(self) -> None:
        runner = FakeRunner(fail_when=lambda call: call[0] == "remote")

        self.assertEqual(self.run_revert("test", runner=runner), 1)

        self.assertEqual(self.runner.kinds(), ["connect", "revert", "close"])
        self.assertIn("FAILED revert: boom", self.text())

    def test_interrupt_closes_the_connection(self) -> None:
        runner = FakeRunner()
        runner.connect = mock.Mock(side_effect=KeyboardInterrupt)

        self.assertEqual(self.run_revert("test", runner=runner), 130)

        self.assertEqual(self.runner.kinds(), ["close"])
        self.assertIn("interrupted", self.text())


if __name__ == "__main__":
    unittest.main()
