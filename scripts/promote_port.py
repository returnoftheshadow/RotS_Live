#!/usr/bin/env python3
"""Promote a port's source to the next port, rebuild and unit-test it, and install it; or roll that back.

    promote_port.py 4810-to-4802 [--preview]    the coders port's source to the builders port
    promote_port.py 4802-to-3791 [--preview]    the builders port's source to the live port
    promote_port.py rollback <4802|3791>        undo the most recent promotion to that port

Of a port's game files only bin/ageland and src/ change, always together; world and player data are never
touched. The runner must be able to write the destination's port directory, bin/ and every directory in its
src/, and read the source port's src/. A destination binary no promotion installed is replaced only after the
runner types "overwrite <port>". Every run appends a line to the destination's bin/promote.log, and keeps its
full output under ~/promote/<route>/runs/ (~/promote/rollback-<port>/runs/ for a rollback). No port is
restarted: the routine reboot starts the new binary.
"""

import argparse
import contextlib
import dataclasses
import datetime
import fcntl
import filecmp
import grp
import json
import os
import pwd
import select
import shutil
import signal
import sys
import traceback
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, Dict, Iterator, List, Optional, Sequence, TextIO

sys.path.insert(0, str(Path(__file__).resolve().parent))
import nightly_deploy  # noqa: E402  (the shared runner, build, tests, installer and paired swap)
from nightly_deploy import NightlyError  # noqa: E402


PORT_DIRS = {"4810": Path("/rots/dev-coding4810"), "4802": Path("/rots/dev-building4802"),
             "3791": Path("/rots/live-default3791")}
# Route name to (source port, destination port).
ROUTES = {"4810-to-4802": ("4810", "4802"), "4802-to-3791": ("4802", "3791")}
# The coders port's src/ is known to match its binary only when the nightly job wrote it, which it marks with
# .source-commit.
ROUTES_REQUIRING_SOURCE_COMMIT = {"4810-to-4802"}
CONFIRM_TIMEOUT_SECONDS = 180
PROMOTION_BACKUP_GLOB = "ageland.bak.*.promotion-*"
# Room for the staging build directory and binaries in the runner's home.
MIN_FREE_BYTES = 2 * 1024 ** 3
UNKNOWN_COMMIT = "unknown"
# Files a promotion creates in a shared port must stay usable by the next person's promotion.
JOB_UMASK = 0o007
# Where a rollback parks the promoted src/ while it restores the old one; a fixed name cannot collide with
# the promotion's own src.bak.<stamp>.
ROLLED_BACK_SOURCE_NAME = ".src-rolled-back"


@dataclass(frozen=True)
class Port:
    """One game port's directory, and the promotion files it keeps in bin/."""

    number: str  # the port's name in routes and messages, such as "4802"
    directory: Path  # /rots/<port directory>; a temporary directory in tests

    @property
    def src(self) -> Path:
        return self.directory / "src"

    @property
    def bin(self) -> Path:
        return self.directory / "bin"

    @property
    def binary(self) -> Path:
        return self.bin / "ageland"

    @property
    def record(self) -> Path:
        return self.bin / "promotion.json"

    @property
    def log(self) -> Path:
        return self.bin / "promote.log"

    @property
    def lock(self) -> Path:
        return self.bin / ".promote.lock"

    @property
    def staged_source(self) -> Path:
        return self.directory / nightly_deploy.STAGED_SOURCE_NAME


@dataclass(frozen=True)
class PromotionRecord:
    """What the most recent promotion installed into a port, as bin/promotion.json records it."""

    route: str  # such as "4810-to-4802"
    source_commit: str  # the promoted source's .source-commit, or UNKNOWN_COMMIT
    binary_sha256: str  # checksum of the installed bin/ageland
    installed_at: str  # local time of the promotion, ISO 8601
    installed_by: str  # the user who ran it
    binary_backup: Optional[str]  # file name in bin/ of the binary it replaced, or None when there was none
    source_backup: Optional[str]  # directory name in the port of the src/ it replaced, or None
    previous: Optional[dict]  # the record it replaced, without that record's own `previous`, or None

    @staticmethod
    def load(path: Path) -> Optional["PromotionRecord"]:
        """The record at `path`, or None when there is none. Raises NightlyError("record") when it is malformed."""
        if not path.exists():
            return None
        try:
            return PromotionRecord(**json.loads(path.read_text()))
        except (OSError, ValueError, TypeError) as error:
            raise NightlyError("record", f"{path} is unreadable or malformed ({error})")

    def save(self, path: Path) -> None:
        """Writes the record atomically."""
        nightly_deploy.write_json_atomically(path, dataclasses.asdict(self))


def current_user() -> str:
    """The name of the user running the script."""
    return pwd.getpwuid(os.geteuid()).pw_name


def owner_name(user_id: int) -> str:
    """The login name for `user_id`, or the number when the system has no such user."""
    try:
        return pwd.getpwuid(user_id).pw_name
    except KeyError:
        return str(user_id)


def describe_path(path: Path) -> str:
    """`path` with its owner, group and mode, for refusal messages."""
    try:
        status = path.stat()
    except FileNotFoundError:
        return f"{path} (missing)"
    try:
        group = grp.getgrgid(status.st_gid).gr_name
    except KeyError:
        group = str(status.st_gid)
    return f"{path} (owner {owner_name(status.st_uid)}, group {group}, mode {status.st_mode & 0o7777:o})"


def describe_binary(path: Path) -> str:
    """A binary's short checksum, owner and modification time, for the confirmation prompt."""
    if not path.exists():
        return "missing"
    status = path.stat()
    modified = datetime.datetime.fromtimestamp(status.st_mtime)
    return f"sha {nightly_deploy.sha256_of(path)[:7]}, owner {owner_name(status.st_uid)}, {modified:%b %d %H:%M}"


def directories_under(root: Path) -> List[Path]:
    """`root` and every directory below it."""
    directories = [root]
    for directory, subdirectory_names, _file_names in os.walk(root):
        directories.extend(Path(directory) / name for name in subdirectory_names)
    return directories


def check_gate(source: Optional[Port], destination: Port) -> None:
    """Raises NightlyError("gate") unless the runner can change the destination and read the source.

    The destination's port directory, bin/, src/ and every directory under src/ must be writable and
    searchable; the source's src/ and every directory under it must be readable and searchable. A None
    `source` (a rollback) checks only the destination.
    """
    user = current_user()
    writable = [destination.directory, destination.bin] + directories_under(destination.src)
    for directory in writable:
        if not os.access(directory, os.W_OK | os.X_OK):
            raise NightlyError("gate", f"refused: {user} cannot write {describe_path(directory)}")
    if source is None:
        return
    for directory in directories_under(source.src):
        if not os.access(directory, os.R_OK | os.X_OK):
            raise NightlyError("gate", f"refused: {user} cannot read {describe_path(directory)}")


def checksum_or_none(path: Path) -> Optional[str]:
    """The SHA-256 of the file at `path`, or None when it does not exist."""
    if not path.exists():
        return None
    return nightly_deploy.sha256_of(path)


def is_promoted_binary(port: Port, record: Optional[PromotionRecord]) -> bool:
    """True when the port's bin/ageland is the binary `record` says the last promotion installed."""
    if record is None:
        return False
    return checksum_or_none(port.binary) == record.binary_sha256


def read_answer_from_terminal(timeout_seconds: float) -> Optional[str]:
    """One line typed at the terminal, or None when stdin is not a terminal or nothing arrives in time."""
    if not sys.stdin.isatty():
        return None
    ready, _unused_writable, _unused_errors = select.select([sys.stdin], [], [], timeout_seconds)
    if not ready:
        return None
    return sys.stdin.readline().strip()


def confirm_overwrite(port: Port, read_answer: Callable[[float], Optional[str]]) -> None:
    """Asks the runner to type "overwrite <port>" before a binary no promotion installed is replaced.

    Raises NightlyError("confirm") on any other answer, or when nothing is typed within
    CONFIRM_TIMEOUT_SECONDS or there is no terminal to ask.
    """
    expected = f"overwrite {port.number}"
    print(f"{port.number}'s bin/ageland ({describe_binary(port.binary)}) was not installed by a promotion.")
    print(f"Type '{expected}' within {CONFIRM_TIMEOUT_SECONDS // 60} minutes to replace it: ", end="", flush=True)
    answer = read_answer(CONFIRM_TIMEOUT_SECONDS)
    if answer is None:
        raise NightlyError("confirm", "refused: no confirmation typed in time, or no terminal to ask")
    if answer != expected:
        raise NightlyError("confirm", f"refused: {answer!r} is not {expected!r}")


@contextlib.contextmanager
def port_lock(port: Port) -> Iterator[None]:
    """Holds the port's promotion lock for the block. Raises NightlyError("lock") when another run holds it."""
    # Read-only, so a lock file another user created is still usable.
    lock_descriptor = os.open(port.lock, os.O_RDONLY | os.O_CREAT, 0o660)
    try:
        try:
            fcntl.flock(lock_descriptor, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            raise NightlyError("lock", f"another promotion or rollback to {port.number} is running")
        yield
    finally:
        os.close(lock_descriptor)


def sync_tree(source: Path, destination: Path) -> None:
    """Makes `destination` hold `source`'s files, leaving out .o files.

    A file whose bytes are unchanged keeps its old modification time, and a changed file gets the current
    time, so an incremental build of `destination` rebuilds exactly what changed.
    """
    wanted = set()
    for directory, _subdirectory_names, file_names in os.walk(source):
        relative_directory = Path(directory).relative_to(source)
        (destination / relative_directory).mkdir(parents=True, exist_ok=True)
        for file_name in file_names:
            if file_name.endswith(".o"):
                continue
            relative_path = relative_directory / file_name
            wanted.add(relative_path)
            source_file = Path(directory) / file_name
            target = destination / relative_path
            if target.is_file() and filecmp.cmp(source_file, target, shallow=False):
                continue
            target.unlink(missing_ok=True)
            # copyfile() does not carry the source's modification time over.
            shutil.copyfile(source_file, target)
            shutil.copymode(source_file, target)
    for directory, _subdirectory_names, file_names in os.walk(destination, topdown=False):
        relative_directory = Path(directory).relative_to(destination)
        for file_name in file_names:
            if relative_directory / file_name not in wanted:
                (Path(directory) / file_name).unlink()
        if not (source / relative_directory).is_dir():
            Path(directory).rmdir()


def read_source_commit(source: Path) -> str:
    """The commit a source tree's .source-commit names, or UNKNOWN_COMMIT when it has none."""
    commit_file = source / nightly_deploy.SOURCE_COMMIT_FILE
    if not commit_file.is_file():
        return UNKNOWN_COMMIT
    return commit_file.read_text().strip()


def relative_files(root: Path) -> Dict[str, Path]:
    """Every file under `root`, by its path relative to `root`."""
    return {str(path.relative_to(root)): path for path in root.rglob("*") if path.is_file()}


@dataclass(frozen=True)
class TreeChanges:
    """How a new source tree differs from the one it would replace, by relative path."""

    added: List[str]  # files only the new tree has
    changed: List[str]  # files both trees have, with different bytes
    removed: List[str]  # files only the old tree has, .o files included

    def summary(self) -> str:
        """'N added, N changed, N removed', for the preview's summary line."""
        return f"{len(self.added)} added, {len(self.changed)} changed, {len(self.removed)} removed"


def compare_trees(new: Path, old: Path) -> TreeChanges:
    """How the tree at `new` differs from the tree at `old`."""
    new_files = relative_files(new)
    old_files = relative_files(old)
    added = sorted(set(new_files) - set(old_files))
    removed = sorted(set(old_files) - set(new_files))
    changed = sorted(name for name in set(new_files) & set(old_files)
                     if not filecmp.cmp(new_files[name], old_files[name], shallow=False))
    return TreeChanges(added, changed, removed)


class RunContext:
    """What a run has learned so far, so its log line names the source commit even when it fails."""

    def __init__(self) -> None:
        # The promoted (or rolled-back) source commit once known, or None.
        self.commit: Optional[str] = None


def short_commit(commit: Optional[str]) -> str:
    """"-" when `commit` is None, UNKNOWN_COMMIT unchanged, otherwise the first seven characters."""
    if commit is None:
        return "-"
    if commit == UNKNOWN_COMMIT:
        return commit
    return commit[:7]


def stage_new_source(snapshot: Path, destination: Port) -> Path:
    """Copies the snapshot to the destination's .src-new, beside its src/, and returns that path.

    Every directory in the copy gets the mode the destination's src/ has now, setgid bit included, so a
    promotion never changes who may write the port's source, and so who passes the next promotion's gate.
    """
    directory_mode = destination.src.stat().st_mode & 0o7777
    shutil.rmtree(destination.staged_source, ignore_errors=True)
    shutil.copytree(snapshot, destination.staged_source)
    for directory in directories_under(destination.staged_source):
        os.chmod(directory, directory_mode)
    return destination.staged_source


def take_snapshot(route_name: str, source: Port, staging: nightly_deploy.NightlyPaths,
                  context: RunContext) -> Path:
    """Copies the source port's src/ into the staging checkout and returns it, recording its commit in `context`.

    Raises NightlyError("snapshot") when the route needs a named commit and the source has none.
    """
    snapshot = staging.clone / "src"
    source_identity = source.src.stat().st_ino
    sync_tree(source.src, snapshot)
    # The nightly job or another promotion may swap the source's src/ meanwhile; a copy straddling that swap
    # could mix two trees under one .source-commit.
    if source.src.stat().st_ino != source_identity:
        raise NightlyError("snapshot", f"{source.src} was replaced while it was copied; run the promotion again")
    context.commit = read_source_commit(snapshot)
    if context.commit == UNKNOWN_COMMIT and route_name in ROUTES_REQUIRING_SOURCE_COMMIT:
        raise NightlyError("snapshot", f"{source.src} has no {nightly_deploy.SOURCE_COMMIT_FILE}; run "
                                       "nightly_deploy.py write-source so it holds the running binary's source")
    return snapshot


def promote(route_name: str, source: Port, destination: Port, staging: nightly_deploy.NightlyPaths,
            runner: nightly_deploy.CommandRunner, read_answer: Callable[[float], Optional[str]],
            now: datetime.datetime, context: RunContext) -> str:
    """Promotes the source port's src/ to the destination, rebuilt and unit-tested, and returns "promoted".

    Raises NightlyError naming the step that refused or failed. The destination's binary and src/ change
    only in the final paired swap, and together.
    """
    check_gate(source, destination)
    with port_lock(destination):
        record = PromotionRecord.load(destination.record)
        # Taken before the prompt, so a hand deploy while the prompt waits is caught at the swap.
        checked_checksum = checksum_or_none(destination.binary)
        if not is_promoted_binary(destination, record):
            confirm_overwrite(destination, read_answer)
        nightly_deploy.check_free_space(staging.home, MIN_FREE_BYTES)
        snapshot = take_snapshot(route_name, source, staging, context)
        nightly_deploy.build(runner, staging)
        nightly_deploy.run_unit_tests(runner, staging)
        staged_source = stage_new_source(snapshot, destination)
        stamp = now.strftime(nightly_deploy.STAMP_FORMAT)
        installer = nightly_deploy.BinaryInstaller(destination.bin, PROMOTION_BACKUP_GLOB)
        # Held until the record names the swap, so a terminated run never leaves a promotion unrecorded.
        with nightly_deploy.termination_signals_held():
            try:
                if checksum_or_none(destination.binary) != checked_checksum:
                    raise NightlyError("install", "refused: bin/ageland changed since it was checked")
                paired = nightly_deploy.install_with_source(installer, staging.built_server,
                                                            f"ageland.bak.{stamp}.promotion-{route_name}",
                                                            destination.directory, staged_source, stamp)
            except OSError as error:
                shutil.rmtree(staged_source, ignore_errors=True)
                raise NightlyError("install", f"could not install into {destination.directory}: {error}")
            except BaseException:
                shutil.rmtree(staged_source, ignore_errors=True)
                raise
            previous = None
            if record is not None:
                previous = dataclasses.asdict(dataclasses.replace(record, previous=None))
            binary_backup = None
            if paired.binary.backup is not None:
                binary_backup = paired.binary.backup.name
            source_backup = None
            if paired.source_backup is not None:
                source_backup = paired.source_backup.name
            new_record = PromotionRecord(route_name, context.commit, paired.binary.checksum,
                                         now.isoformat(timespec="seconds"), current_user(), binary_backup,
                                         source_backup, previous)
            new_record.save(destination.record)
        installer.prune_backups()
        prune_note = nightly_deploy.prune_source_backups_after_install(destination.directory)
    return "promoted" + prune_note


def preview_promotion(route_name: str, source: Port, destination: Port, staging: nightly_deploy.NightlyPaths,
                      run_log: TextIO, context: RunContext) -> str:
    """Reports what promoting `route_name` would change, changing nothing outside the staging checkout."""
    check_gate(source, destination)
    record = PromotionRecord.load(destination.record)
    if is_promoted_binary(destination, record):
        prompt_note = "bin/ageland is the last promotion's; no confirmation needed"
    else:
        prompt_note = (f"would ask to overwrite {destination.number}'s bin/ageland "
                       f"({describe_binary(destination.binary)})")
    snapshot = take_snapshot(route_name, source, staging, context)
    changes = compare_trees(snapshot, destination.src)
    for label, names in (("added", changes.added), ("changed", changes.changed), ("removed", changes.removed)):
        for name in names:
            run_log.write(f"{label}: {name}\n")
    return f"src/ {changes.summary()}; {prompt_note}"


def roll_back(destination: Port, read_answer: Callable[[float], Optional[str]], context: RunContext) -> str:
    """Undoes the most recent promotion to `destination`, restoring its binary, src/ and previous record.

    Raises NightlyError("rollback") when no promotion is recorded or its backups are gone, and the gate,
    lock and confirmation errors a promotion raises. The binary and src/ change together.
    """
    check_gate(None, destination)
    with port_lock(destination):
        record = PromotionRecord.load(destination.record)
        if record is None:
            raise NightlyError("rollback", f"no promotion to {destination.number} is recorded")
        context.commit = record.source_commit
        if record.binary_backup is None or record.source_backup is None:
            raise NightlyError("rollback", "the last promotion replaced nothing, so there is nothing to restore")
        binary_backup = destination.bin / record.binary_backup
        source_backup = destination.directory / record.source_backup
        for backup in (binary_backup, source_backup):
            if not backup.exists():
                raise NightlyError("rollback", f"{backup} is gone; nothing to restore")
        # Taken before the prompt, so a hand deploy while the prompt waits is caught before the swap.
        checked_checksum = checksum_or_none(destination.binary)
        if not is_promoted_binary(destination, record):
            confirm_overwrite(destination, read_answer)
        promoted_source = destination.directory / ROLLED_BACK_SOURCE_NAME
        shutil.rmtree(promoted_source, ignore_errors=True)
        with nightly_deploy.termination_signals_held():
            if checksum_or_none(destination.binary) != checked_checksum:
                raise NightlyError("rollback", "refused: bin/ageland changed since it was checked")
            # The source goes first: its renames can be undone, and the binary swap after it is one atomic
            # replace.
            os.rename(destination.src, promoted_source)
            try:
                os.rename(source_backup, destination.src)
                try:
                    os.replace(binary_backup, destination.binary)
                except BaseException:
                    os.rename(destination.src, source_backup)
                    raise
            except BaseException:
                os.rename(promoted_source, destination.src)
                raise
            if record.previous is None:
                destination.record.unlink()
            else:
                PromotionRecord(**record.previous).save(destination.record)
        shutil.rmtree(promoted_source)
    return f"rolled back {record.route}"


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="promote_port.py",
                                     description="Promote a port's source to the next port, rebuilt and "
                                                 "unit-tested, or roll the last promotion back.")
    parser.set_defaults(preview=False)
    commands = parser.add_subparsers(dest="command", metavar="command", required=True)
    for route_name, (source_number, destination_number) in ROUTES.items():
        route_parser = commands.add_parser(route_name,
                                           help=f"promote {source_number}'s src/ to {destination_number}")
        route_parser.add_argument("--preview", action="store_true",
                                  help="report what the promotion would change; change nothing")
    rollback_parser = commands.add_parser("rollback", help="undo the most recent promotion to a port")
    rollback_parser.add_argument("port", choices=sorted({number for _source, number in ROUTES.values()}))
    return parser


def append_log_line(port: Port, line: str) -> None:
    """Appends `line` to the port's promote.log, or warns on stderr when the runner cannot write it."""
    try:
        with port.log.open("a") as promote_log:
            promote_log.write(line + "\n")
    except OSError as error:
        print(f"could not write {port.log}: {error}", file=sys.stderr)


def main(argv: Optional[Sequence[str]] = None, port_dirs: Optional[Dict[str, Path]] = None,
         promote_home: Optional[Path] = None, now: Callable[[], datetime.datetime] = datetime.datetime.now,
         read_answer: Callable[[float], Optional[str]] = read_answer_from_terminal) -> int:
    """Runs the command line and returns the process exit status.

    `port_dirs` defaults to the real ports and `promote_home` to ~/promote; tests pass their own, with a clock
    through `now` and the confirmation's input through `read_answer`.
    """
    arguments = build_parser().parse_args(argv)
    if port_dirs is None:
        port_dirs = PORT_DIRS
    if promote_home is None:
        promote_home = Path.home() / "promote"
    ports = {number: Port(number, directory) for number, directory in port_dirs.items()}
    # SIGHUP covers a run whose ssh session drops.
    for signal_number in (signal.SIGTERM, signal.SIGHUP, signal.SIGINT):
        signal.signal(signal_number, nightly_deploy.raise_system_exit)
    os.umask(JOB_UMASK)
    if arguments.command == "rollback":
        source = None
        destination = ports[arguments.port]
        job_name = f"rollback-{arguments.port}"
        label = f"rollback {arguments.port}"
    else:
        source_number, destination_number = ROUTES[arguments.command]
        source = ports[source_number]
        destination = ports[destination_number]
        job_name = arguments.command
        label = job_name
    if arguments.preview:
        label += " --preview"
    staging = nightly_deploy.NightlyPaths(promote_home / job_name, destination.bin)
    staging.run_logs.mkdir(parents=True, exist_ok=True)
    started = now()
    run_log_name = started.strftime(nightly_deploy.STAMP_FORMAT)
    if arguments.preview:
        run_log_name += "-preview"
    run_log_path = staging.run_logs / f"{run_log_name}.log"
    context = RunContext()
    with run_log_path.open("w") as run_log:
        runner = nightly_deploy.CommandRunner(run_log)
        try:
            if arguments.command == "rollback":
                result = roll_back(destination, read_answer, context)
            elif arguments.preview:
                result = preview_promotion(job_name, source, destination, staging, run_log, context)
            else:
                result = promote(job_name, source, destination, staging, runner, read_answer, started, context)
            exit_status = 0
        except NightlyError as error:
            result = f"failed: {error.step}: {error.reason}"
            run_log.write(f"\nFAILED at {error.step}: {error.reason}\n")
            if error.detail:
                run_log.write(error.detail + "\n")
            exit_status = 1
        except SystemExit as termination:
            exit_status = 1
            if isinstance(termination.code, int):
                exit_status = termination.code
            result = f"failed: terminated (exit {exit_status})"
            run_log.write(f"\nTERMINATED with exit {exit_status}\n")
        except Exception:
            result = "failed: unexpected error; see the run log"
            run_log.write("\n" + traceback.format_exc())
            exit_status = 1
    line = (f"{started:%Y-%m-%d %H:%M:%S}  {label}  {current_user()}  {short_commit(context.commit)}  {result}"
            f"  (log: {run_log_path})")
    append_log_line(destination, line)
    print(line)
    nightly_deploy.prune_run_logs(staging.run_logs)
    return exit_status


if __name__ == "__main__":
    sys.exit(main())
