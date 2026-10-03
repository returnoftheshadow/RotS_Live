#!/usr/bin/env python3

import contextlib
import datetime
import fcntl
import importlib.util
import io
import os
import shutil
import signal
import sys
import tempfile
import unittest
from pathlib import Path
from typing import Callable, Dict, List, Optional
from unittest import mock


SCRIPTS_DIR = Path(__file__).resolve().parent
sys.dont_write_bytecode = True
sys.path.insert(0, str(SCRIPTS_DIR))
MODULE_PATH = SCRIPTS_DIR / "promote_port.py"
SPEC = importlib.util.spec_from_file_location("promote_port", MODULE_PATH)
promote = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
sys.modules["promote_port"] = promote
SPEC.loader.exec_module(promote)
nightly_deploy = sys.modules["nightly_deploy"]

COMMIT_4810 = "abc1234" + "0" * 33
RUNNING_AS_ROOT = os.geteuid() == 0


class TempDirTestCase(unittest.TestCase):
    """Gives each test a fresh temporary directory that is removed afterwards."""

    def setUp(self) -> None:
        temp_dir = tempfile.TemporaryDirectory()
        self.addCleanup(temp_dir.cleanup)
        # Resolved so paths compare equal on macOS, where the temporary directory sits behind a symlink.
        self.root = Path(temp_dir.name).resolve()


def make_port(directory: Path, source_text: str, binary: bytes, commit: Optional[str] = None,
              object_file: bool = False) -> "promote.Port":
    """Creates a port with src/ (comm.cpp, tests/comm_tests.cpp), bin/ageland and lib/ data."""
    (directory / "src" / "tests").mkdir(parents=True)
    (directory / "src" / "comm.cpp").write_text(source_text)
    (directory / "src" / "tests" / "comm_tests.cpp").write_text("// tests\n")
    if commit is not None:
        (directory / "src" / ".source-commit").write_text(commit + "\n")
    if object_file:
        (directory / "src" / "comm.o").write_bytes(b"object")
    (directory / "bin").mkdir()
    (directory / "bin" / "ageland").write_bytes(binary)
    (directory / "lib").mkdir()
    (directory / "lib" / "world.txt").write_text("world data\n")
    return promote.Port(directory.name, directory)


def tree_snapshot(root: Path) -> Dict[str, bytes]:
    """Every file under `root`, by relative path, with its bytes."""
    return {str(path.relative_to(root)): path.read_bytes() for path in root.rglob("*") if path.is_file()}


class RecordTest(TempDirTestCase):
    def test_saved_record_loads_back(self) -> None:
        record = promote.PromotionRecord("4810-to-4802", COMMIT_4810, "a" * 64, "2026-09-27T14:00:00", "dgurley",
                                         "ageland.bak.x", "src.bak.x", None)

        record.save(self.root / "promotion.json")

        self.assertEqual(promote.PromotionRecord.load(self.root / "promotion.json"), record)

    def test_missing_record_is_none(self) -> None:
        self.assertIsNone(promote.PromotionRecord.load(self.root / "promotion.json"))

    def test_malformed_record_is_refused(self) -> None:
        (self.root / "promotion.json").write_text("{}")

        with self.assertRaises(nightly_deploy.NightlyError) as caught:
            promote.PromotionRecord.load(self.root / "promotion.json")

        self.assertEqual(caught.exception.step, "record")


@unittest.skipIf(RUNNING_AS_ROOT, "root passes every permission check")
class GateTest(TempDirTestCase):
    def setUp(self) -> None:
        super().setUp()
        # A source and a destination port the runner can use.
        self.source = make_port(self.root / "4810", "// coders\n", b"4810 build", COMMIT_4810)
        self.destination = make_port(self.root / "4802", "// builders\n", b"4802 build")

    def lock_down(self, path: Path, mode: int) -> None:
        original_mode = path.stat().st_mode & 0o7777
        os.chmod(path, mode)
        self.addCleanup(os.chmod, path, original_mode)

    def test_a_usable_pair_passes(self) -> None:
        promote.check_gate(self.source, self.destination)

    def test_each_unwritable_destination_directory_is_refused(self) -> None:
        for directory in (self.destination.directory, self.destination.bin, self.destination.src,
                          self.destination.src / "tests"):
            with self.subTest(directory=directory):
                original_mode = directory.stat().st_mode & 0o7777
                os.chmod(directory, 0o555)
                try:
                    with self.assertRaises(nightly_deploy.NightlyError) as caught:
                        promote.check_gate(self.source, self.destination)
                finally:
                    os.chmod(directory, original_mode)
                self.assertEqual(caught.exception.step, "gate")
                self.assertIn(f"cannot write {directory} (owner ", caught.exception.reason)

    def test_an_unreadable_source_is_refused(self) -> None:
        self.lock_down(self.source.src, 0o300)

        with self.assertRaises(nightly_deploy.NightlyError) as caught:
            promote.check_gate(self.source, self.destination)

        self.assertIn(f"cannot read {self.source.src}", caught.exception.reason)


class ConfirmTest(TempDirTestCase):
    def setUp(self) -> None:
        super().setUp()
        # The destination whose binary the prompt describes.
        self.port = make_port(self.root / "3791", "// live\n", b"hotfix build")
        # The timeouts the prompt passed to read_answer.
        self.timeouts: List[float] = []

    def confirm(self, answer: Optional[str]) -> None:
        def read_answer(timeout_seconds: float) -> Optional[str]:
            self.timeouts.append(timeout_seconds)
            return answer

        with contextlib.redirect_stdout(io.StringIO()):
            promote.confirm_overwrite(self.port, read_answer)

    def test_the_exact_phrase_proceeds(self) -> None:
        self.confirm("overwrite 3791")

        self.assertEqual(self.timeouts, [180])

    def test_other_text_is_refused(self) -> None:
        with self.assertRaises(nightly_deploy.NightlyError) as caught:
            self.confirm("yes")

        self.assertEqual(caught.exception.step, "confirm")
        self.assertIn("'yes' is not 'overwrite 3791'", caught.exception.reason)

    def test_no_answer_is_refused(self) -> None:
        with self.assertRaises(nightly_deploy.NightlyError) as caught:
            self.confirm(None)

        self.assertIn("no confirmation", caught.exception.reason)

    def test_a_missing_terminal_reads_no_answer(self) -> None:
        with mock.patch.object(promote.sys, "stdin", io.StringIO("overwrite 3791\n")):
            self.assertIsNone(promote.read_answer_from_terminal(1))


class LockTest(TempDirTestCase):
    def test_a_held_lock_is_refused(self) -> None:
        port = make_port(self.root / "4802", "// builders\n", b"4802 build")
        with port.lock.open("a") as held_lock:
            fcntl.flock(held_lock, fcntl.LOCK_EX | fcntl.LOCK_NB)

            with self.assertRaises(nightly_deploy.NightlyError) as caught:
                with promote.port_lock(port):
                    pass

        self.assertEqual(caught.exception.step, "lock")


class SyncTreeTest(TempDirTestCase):
    def setUp(self) -> None:
        super().setUp()
        # The tree to copy, with an object file the copy leaves out.
        self.source = self.root / "source"
        (self.source / "tests").mkdir(parents=True)
        (self.source / "comm.cpp").write_text("new\n")
        (self.source / "same.cpp").write_text("unchanged\n")
        (self.source / "comm.o").write_bytes(b"object")
        (self.source / "tests" / "comm_tests.cpp").write_text("// tests\n")
        # The staging copy from an earlier run.
        self.destination = self.root / "destination"
        (self.destination / "gone").mkdir(parents=True)
        (self.destination / "comm.cpp").write_text("old\n")
        (self.destination / "same.cpp").write_text("unchanged\n")
        (self.destination / "gone" / "old.cpp").write_text("removed upstream\n")
        for staged_file in ("comm.cpp", "same.cpp"):
            os.utime(self.destination / staged_file, (1_000_000_000, 1_000_000_000))

    def test_the_copy_matches_the_source_without_object_files(self) -> None:
        promote.sync_tree(self.source, self.destination)

        self.assertEqual(tree_snapshot(self.destination),
                         {"comm.cpp": b"new\n", "same.cpp": b"unchanged\n", "tests/comm_tests.cpp": b"// tests\n"})
        self.assertFalse((self.destination / "gone").exists())

    def test_only_changed_files_get_a_new_time(self) -> None:
        promote.sync_tree(self.source, self.destination)

        self.assertEqual((self.destination / "same.cpp").stat().st_mtime, 1_000_000_000)
        self.assertGreater((self.destination / "comm.cpp").stat().st_mtime, 1_000_000_000)

    def test_source_commit_is_read_or_unknown(self) -> None:
        self.assertEqual(promote.read_source_commit(self.source), "unknown")
        (self.source / ".source-commit").write_text(COMMIT_4810 + "\n")

        self.assertEqual(promote.read_source_commit(self.source), COMMIT_4810)

    def test_compare_trees_lists_added_changed_and_removed_files(self) -> None:
        promote.sync_tree(self.source, self.root / "new")

        changes = promote.compare_trees(self.root / "new", self.destination)

        self.assertEqual(changes.added, ["tests/comm_tests.cpp"])
        self.assertEqual(changes.changed, ["comm.cpp"])
        self.assertEqual(changes.removed, ["gone/old.cpp"])
        self.assertEqual(changes.summary(), "1 added, 1 changed, 1 removed")


FIRST_TIME = datetime.datetime(2026, 9, 27, 14, 0, 0)
SECOND_TIME = datetime.datetime(2026, 9, 27, 15, 0, 0)


class PromotionTestCase(TempDirTestCase):
    """Three fake ports and a runner's home, with the CMake build and unit tests faked."""

    def setUp(self) -> None:
        super().setUp()
        # The three ports, under a fake /rots.
        self.port_dirs = {number: self.root / "rots" / number for number in ("4810", "4802", "3791")}
        # The coders port, whose src/ names its commit.
        self.coders = make_port(self.port_dirs["4810"], "// coders source\n", b"4810 build", COMMIT_4810)
        # The builders port, built in place by hand, so its src/ holds an object file.
        self.builders = make_port(self.port_dirs["4802"], "// builders source\n", b"4802 hand build",
                                  object_file=True)
        # The live port, running a hand-deployed hotfix.
        self.live = make_port(self.port_dirs["3791"], "// live source\n", b"3791 hotfix build")
        # The runner's ~/promote.
        self.promote_home = self.root / "home" / "promote"
        # Answers the fake terminal gives, in order; an empty queue answers None.
        self.answers: List[Optional[str]] = []
        # Called while the fake build runs, to simulate something changing meanwhile.
        self.during_build: Optional[Callable[[], None]] = None
        # Raised by the fake unit tests, or None for a pass.
        self.unit_effect: Optional[BaseException] = None

    def read_answer(self, timeout_seconds: float) -> Optional[str]:
        if not self.answers:
            return None
        return self.answers.pop(0)

    def fake_build(self, runner: object, staging: "nightly_deploy.NightlyPaths") -> None:
        staging.built_server.parent.mkdir(parents=True, exist_ok=True)
        staging.built_server.write_bytes(b"built from " + (staging.clone / "src" / "comm.cpp").read_bytes())
        if self.during_build is not None:
            self.during_build()

    def run_main(self, *argv: str, now: datetime.datetime = FIRST_TIME) -> int:
        with mock.patch.object(nightly_deploy, "build", side_effect=self.fake_build), \
                mock.patch.object(nightly_deploy, "run_unit_tests", return_value="passed",
                                  side_effect=self.unit_effect), \
                mock.patch.object(nightly_deploy, "check_free_space"), \
                mock.patch.object(promote.signal, "signal"), \
                contextlib.redirect_stdout(io.StringIO()):
            return promote.main(list(argv), self.port_dirs, self.promote_home, lambda: now, self.read_answer)

    def log_lines(self, port: "promote.Port") -> List[str]:
        return port.log.read_text().splitlines()


class PromoteTest(PromotionTestCase):
    def test_a_promotion_rebuilds_tests_and_installs_binary_and_source(self) -> None:
        self.answers = ["overwrite 4802"]

        exit_status = self.run_main("4810-to-4802")

        self.assertEqual(exit_status, 0)
        self.assertEqual(self.builders.binary.read_bytes(), b"built from // coders source\n")
        self.assertEqual(tree_snapshot(self.builders.src), {
            "comm.cpp": b"// coders source\n", "tests/comm_tests.cpp": b"// tests\n",
            ".source-commit": (COMMIT_4810 + "\n").encode()})
        backup = self.builders.directory / "src.bak.20260927_140000"
        self.assertEqual((backup / "comm.cpp").read_text(), "// builders source\n")
        self.assertEqual((self.builders.directory / "lib" / "world.txt").read_text(), "world data\n")
        record = promote.PromotionRecord.load(self.builders.record)
        self.assertEqual((record.route, record.source_commit, record.installed_by),
                         ("4810-to-4802", COMMIT_4810, promote.current_user()))
        self.assertEqual(record.binary_backup, "ageland.bak.20260927_140000.promotion-4810-to-4802")
        self.assertEqual((self.builders.bin / record.binary_backup).read_bytes(), b"4802 hand build")
        self.assertIn(f"4810-to-4802  {promote.current_user()}  abc1234  promoted",
                      self.log_lines(self.builders)[-1])

    def test_a_wrong_answer_changes_nothing(self) -> None:
        self.answers = ["yes"]

        exit_status = self.run_main("4810-to-4802")

        self.assertEqual(exit_status, 1)
        self.assertEqual(self.builders.binary.read_bytes(), b"4802 hand build")
        self.assertEqual((self.builders.src / "comm.cpp").read_text(), "// builders source\n")
        self.assertIn("failed: confirm: refused: 'yes' is not 'overwrite 4802'", self.log_lines(self.builders)[-1])

    def test_no_answer_changes_nothing(self) -> None:
        exit_status = self.run_main("4810-to-4802")

        self.assertEqual(exit_status, 1)
        self.assertEqual(self.builders.binary.read_bytes(), b"4802 hand build")

    def test_a_promoted_binary_is_replaced_without_asking(self) -> None:
        self.answers = ["overwrite 4802"]
        self.run_main("4810-to-4802")

        exit_status = self.run_main("4810-to-4802", now=SECOND_TIME)

        self.assertEqual(exit_status, 0)
        self.assertEqual(self.answers, [])

    def test_a_hand_replaced_binary_is_asked_about_again(self) -> None:
        self.answers = ["overwrite 4802"]
        self.run_main("4810-to-4802")
        self.builders.binary.write_bytes(b"another hand build")

        exit_status = self.run_main("4810-to-4802", now=SECOND_TIME)

        self.assertEqual(exit_status, 1)
        self.assertEqual(self.builders.binary.read_bytes(), b"another hand build")

    def test_failed_unit_tests_leave_the_port_untouched(self) -> None:
        self.answers = ["overwrite 4802"]
        self.unit_effect = nightly_deploy.NightlyError("unit tests", "1 failed: PoisonTest.Stacks")

        exit_status = self.run_main("4810-to-4802")

        self.assertEqual(exit_status, 1)
        self.assertEqual(self.builders.binary.read_bytes(), b"4802 hand build")
        self.assertEqual((self.builders.src / "comm.cpp").read_text(), "// builders source\n")
        self.assertFalse(self.builders.staged_source.exists())
        self.assertIn("failed: unit tests: 1 failed: PoisonTest.Stacks", self.log_lines(self.builders)[-1])

    def test_the_4810_route_refuses_a_source_without_its_commit(self) -> None:
        (self.coders.src / ".source-commit").unlink()
        self.answers = ["overwrite 4802"]

        exit_status = self.run_main("4810-to-4802")

        self.assertEqual(exit_status, 1)
        self.assertIn("failed: snapshot:", self.log_lines(self.builders)[-1])
        self.assertEqual(self.builders.binary.read_bytes(), b"4802 hand build")

    def test_the_3791_route_records_an_unknown_commit(self) -> None:
        self.answers = ["overwrite 3791"]

        exit_status = self.run_main("4802-to-3791")

        self.assertEqual(exit_status, 0)
        self.assertEqual(promote.PromotionRecord.load(self.live.record).source_commit, "unknown")
        self.assertFalse((self.live.src / "comm.o").exists())

    def test_a_failed_source_swap_restores_the_binary(self) -> None:
        self.answers = ["overwrite 4802"]
        real_rename = os.rename

        def failing_rename(source: object, destination: object) -> None:
            if Path(source).name == ".src-new":
                raise OSError(18, "Invalid cross-device link")
            real_rename(source, destination)

        with mock.patch.object(nightly_deploy.os, "rename", side_effect=failing_rename):
            exit_status = self.run_main("4810-to-4802")

        self.assertEqual(exit_status, 1)
        self.assertEqual(self.builders.binary.read_bytes(), b"4802 hand build")
        self.assertEqual((self.builders.src / "comm.cpp").read_text(), "// builders source\n")
        self.assertFalse(self.builders.staged_source.exists())

    def test_a_binary_changed_after_the_check_is_refused(self) -> None:
        self.answers = ["overwrite 4802"]
        self.during_build = lambda: self.builders.binary.write_bytes(b"deployed during the build")

        exit_status = self.run_main("4810-to-4802")

        self.assertEqual(exit_status, 1)
        self.assertEqual(self.builders.binary.read_bytes(), b"deployed during the build")
        self.assertIn("changed since it was checked", self.log_lines(self.builders)[-1])
        self.assertFalse(self.builders.staged_source.exists())

    def test_a_binary_replaced_during_the_prompt_is_refused(self) -> None:
        def answer_after_a_hand_deploy(timeout_seconds: float) -> Optional[str]:
            self.builders.binary.write_bytes(b"deployed during the prompt")
            return "overwrite 4802"

        self.read_answer = answer_after_a_hand_deploy

        exit_status = self.run_main("4810-to-4802")

        self.assertEqual(exit_status, 1)
        self.assertEqual(self.builders.binary.read_bytes(), b"deployed during the prompt")
        self.assertIn("changed since it was checked", self.log_lines(self.builders)[-1])

    def test_a_source_replaced_during_the_snapshot_is_refused(self) -> None:
        self.answers = ["overwrite 4802"]
        real_sync_tree = promote.sync_tree

        def swapping_sync_tree(source: Path, destination: Path) -> None:
            real_sync_tree(source, destination)
            os.rename(self.coders.src, self.coders.directory / "src.bak.swapped")
            shutil.copytree(self.coders.directory / "src.bak.swapped", self.coders.src)

        with mock.patch.object(promote, "sync_tree", side_effect=swapping_sync_tree):
            exit_status = self.run_main("4810-to-4802")

        self.assertEqual(exit_status, 1)
        self.assertIn("failed: snapshot:", self.log_lines(self.builders)[-1])
        self.assertIn("was replaced while it was copied", self.log_lines(self.builders)[-1])
        self.assertEqual(self.builders.binary.read_bytes(), b"4802 hand build")

    def test_the_destinations_src_directory_modes_are_kept(self) -> None:
        for directory in (self.live.src, self.live.src / "tests"):
            os.chmod(directory, 0o755)
        self.answers = ["overwrite 3791"]

        exit_status = self.run_main("4802-to-3791")

        self.assertEqual(exit_status, 0)
        self.assertEqual(self.live.src.stat().st_mode & 0o7777, 0o755)
        self.assertEqual((self.live.src / "tests").stat().st_mode & 0o7777, 0o755)

    def test_a_source_backup_that_cannot_be_pruned_is_noted_not_failed(self) -> None:
        (self.builders.directory / "src.bak.20260901_000000").mkdir()
        self.answers = ["overwrite 4802"]
        real_rmtree = shutil.rmtree

        def refusing_rmtree(path: object, *args: object, **kwargs: object) -> None:
            if Path(path).name == "src.bak.20260901_000000":
                raise PermissionError(13, "Permission denied", str(path))
            real_rmtree(path, *args, **kwargs)

        with mock.patch.object(nightly_deploy.shutil, "rmtree", side_effect=refusing_rmtree):
            exit_status = self.run_main("4810-to-4802")

        self.assertEqual(exit_status, 0)
        self.assertIn("promoted; could not delete an old source backup: ", self.log_lines(self.builders)[-1])

    def test_a_held_lock_refuses(self) -> None:
        with self.builders.lock.open("a") as held_lock:
            fcntl.flock(held_lock, fcntl.LOCK_EX | fcntl.LOCK_NB)

            exit_status = self.run_main("4810-to-4802")

        self.assertEqual(exit_status, 1)
        self.assertIn("failed: lock:", self.log_lines(self.builders)[-1])

    def test_preview_changes_nothing(self) -> None:
        port_before = tree_snapshot(self.builders.directory)

        exit_status = self.run_main("4810-to-4802", "--preview")

        self.assertEqual(exit_status, 0)
        port_after = tree_snapshot(self.builders.directory)
        port_after.pop("bin/promote.log")
        self.assertEqual(port_after, port_before)
        self.assertIn("4810-to-4802 --preview", self.log_lines(self.builders)[-1])
        self.assertIn("1 added, 1 changed, 1 removed", self.log_lines(self.builders)[-1])
        self.assertIn("would ask to overwrite", self.log_lines(self.builders)[-1])

    def test_backups_are_pruned(self) -> None:
        self.answers = ["overwrite 4802"]
        for hour in range(8):
            self.run_main("4810-to-4802", now=datetime.datetime(2026, 9, 27, 10 + hour, 0, 0))

        self.assertEqual(len(list(self.builders.bin.glob("ageland.bak.*.promotion-*"))), 7)
        self.assertEqual(len(list(self.builders.directory.glob("src.bak.*"))), 1)

    def test_main_sets_the_group_usable_umask(self) -> None:
        with mock.patch.object(promote.os, "umask", return_value=0o022) as umask:
            self.run_main("4810-to-4802", "--preview")

        umask.assert_called_once_with(0o007)


class RollbackTest(PromotionTestCase):
    def test_rollback_restores_the_exact_binary_and_source(self) -> None:
        port_before = tree_snapshot(self.builders.directory)
        self.answers = ["overwrite 4802"]
        self.run_main("4810-to-4802")

        exit_status = self.run_main("rollback", "4802", now=SECOND_TIME)

        self.assertEqual(exit_status, 0)
        port_after = tree_snapshot(self.builders.directory)
        port_after.pop("bin/promote.log")
        port_after.pop("bin/.promote.lock")
        self.assertEqual(port_after, port_before)
        self.assertIn(f"rollback 4802  {promote.current_user()}  abc1234  rolled back 4810-to-4802",
                      self.log_lines(self.builders)[-1])

    def test_rollback_in_the_same_second_as_its_promotion(self) -> None:
        port_before = tree_snapshot(self.builders.directory)
        self.answers = ["overwrite 4802"]
        self.run_main("4810-to-4802")

        exit_status = self.run_main("rollback", "4802")

        self.assertEqual(exit_status, 0)
        self.assertEqual(self.builders.binary.read_bytes(), port_before["bin/ageland"])

    def test_rollback_restores_the_previous_record(self) -> None:
        self.answers = ["overwrite 4802"]
        self.run_main("4810-to-4802")
        first_record = promote.PromotionRecord.load(self.builders.record)
        first_binary = self.builders.binary.read_bytes()
        self.run_main("4810-to-4802", now=SECOND_TIME)

        self.run_main("rollback", "4802", now=datetime.datetime(2026, 9, 27, 16, 0, 0))

        self.assertEqual(promote.PromotionRecord.load(self.builders.record), first_record)
        self.assertEqual(self.builders.binary.read_bytes(), first_binary)

    def test_a_binary_replaced_during_the_rollback_prompt_is_refused(self) -> None:
        self.answers = ["overwrite 4802"]
        self.run_main("4810-to-4802")
        promoted_source = tree_snapshot(self.builders.src)
        self.builders.binary.write_bytes(b"another hand build")

        def answer_after_a_hand_deploy(timeout_seconds: float) -> Optional[str]:
            self.builders.binary.write_bytes(b"deployed during the prompt")
            return "overwrite 4802"

        self.read_answer = answer_after_a_hand_deploy

        exit_status = self.run_main("rollback", "4802", now=SECOND_TIME)

        self.assertEqual(exit_status, 1)
        self.assertEqual(self.builders.binary.read_bytes(), b"deployed during the prompt")
        self.assertEqual(tree_snapshot(self.builders.src), promoted_source)
        self.assertIn("changed since it was checked", self.log_lines(self.builders)[-1])

    def test_the_rollback_swap_runs_with_termination_signals_held(self) -> None:
        self.answers = ["overwrite 4802"]
        self.run_main("4810-to-4802")
        masks_during_replace: List[set] = []
        real_replace = os.replace

        def watching_replace(source: object, destination: object) -> None:
            masks_during_replace.append(signal.pthread_sigmask(signal.SIG_BLOCK, []))
            real_replace(source, destination)

        with mock.patch.object(promote.os, "replace", side_effect=watching_replace):
            self.run_main("rollback", "4802", now=SECOND_TIME)

        self.assertIn(signal.SIGTERM, masks_during_replace[0])

    def test_rollback_without_a_promotion_refuses(self) -> None:
        exit_status = self.run_main("rollback", "4802")

        self.assertEqual(exit_status, 1)
        self.assertIn("failed: rollback: no promotion to 4802 is recorded", self.log_lines(self.builders)[-1])

    def test_rollback_of_a_hand_replaced_binary_asks(self) -> None:
        self.answers = ["overwrite 4802"]
        self.run_main("4810-to-4802")
        self.builders.binary.write_bytes(b"another hand build")

        exit_status = self.run_main("rollback", "4802", now=SECOND_TIME)

        self.assertEqual(exit_status, 1)
        self.assertIn("failed: confirm:", self.log_lines(self.builders)[-1])
        self.assertEqual(self.builders.binary.read_bytes(), b"another hand build")


if __name__ == "__main__":
    unittest.main()
