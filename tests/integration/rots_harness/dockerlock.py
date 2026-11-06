"""This pytest session's place in the lock directory shared by every Docker job on the host.

Jobs lock the resources they use, so jobs using different resources run at the same time. A
lock file lists its resources on a `resources:` line; one without that line holds its
checkout's build trees and host port 1024.
"""

from __future__ import annotations

import math
import os
import re
import sys
import time
import uuid
from datetime import datetime, timezone
from pathlib import Path
from typing import Callable, Optional

DEFAULT_LOCK_DIR = Path("/tmp/rots-docker-lock")
LOCK_FILE_PREFIX = "harness-it"
DEFAULT_WAIT_SECONDS = 600.0
POLL_SECONDS = 5.0
# The name _try_acquire gives this harness's lock files; group 1 is the pid of the session that
# wrote it. Only files with this exact shape are ever removed as stale.
HARNESS_LOCK_NAME = re.compile(rf"{re.escape(LOCK_FILE_PREFIX)}-([0-9]+)-[0-9a-f]+\.lock")


class DockerLockHeld(RuntimeError):
    """Another session's lock file holds this session's resource; the message names the holders."""


def _report_to_stderr(message: str) -> None:
    print(message, file=sys.stderr, flush=True)


def build_tree_resource(repo_root: Path) -> str:
    """The lock resource for repo_root's build tree, where the harness reads its programs and keeps its runs."""
    return f"build:{repo_root.name}/build"


def lock_fields(text: str) -> dict[str, str]:
    """The `key: value` lines of a lock file's text; a later line wins over an earlier one with the same key."""
    fields: dict[str, str] = {}
    for line in text.splitlines():
        key, separator, value = line.partition(":")
        if separator:
            fields[key.strip()] = value.strip()
    return fields


def lock_holds(text: str, resource: str, checkout_name: str) -> bool:
    """Whether a lock file with this text holds resource, a build tree of the checkout named checkout_name.

    A lock without resources follows the old convention and holds its checkout's build trees, so
    it holds resource when its repo is that checkout or it names no repo.
    """
    fields = lock_fields(text)
    resources = fields.get("resources", "").split()
    if resources:
        return resource in resources
    repo = fields.get("repo", "")
    return not repo or Path(repo).name == checkout_name


def parse_wait_seconds(text: str) -> float:
    """The lock wait in seconds that text gives; raises ValueError unless it is a finite number of at least 0."""
    seconds = float(text)
    if not math.isfinite(seconds) or seconds < 0:
        raise ValueError(f"{text!r} is not a finite number of seconds of at least 0")
    return seconds


def process_is_running(pid: int) -> bool:
    """Whether a process with this pid exists on this host; True when that cannot be ruled out."""
    try:
        os.kill(pid, 0)
    except ProcessLookupError:
        return False
    except (OSError, OverflowError):
        # PermissionError means the process exists but belongs to someone else. Any other failure,
        # including a pid too large to pass to kill, leaves the lock to a person to judge.
        return True
    return True


class DockerLock:
    """One uniquely named lock file per pytest session on this checkout's build tree, held for the whole session.

    Unique names let two harness sessions on one host see each other, and holding the file for
    the session keeps a build into this tree from replacing the programs between two tests.
    Jobs on other resources (another checkout's tree, a published port, a running game) never
    make it wait.
    """

    def __init__(
        self,
        lock_dir: Path | None,
        repo_root: Path,
        wait_seconds: float = DEFAULT_WAIT_SECONDS,
        poll_seconds: float = POLL_SECONDS,
        clock: Callable[[], float] = time.monotonic,
        sleep: Callable[[float], None] = time.sleep,
        report: Callable[[str], None] = _report_to_stderr,
        process_is_running: Callable[[int], bool] = process_is_running,
    ) -> None:
        """Prepares the lock without touching the directory; a lock_dir of None disables locking."""
        # The shared lock directory, or None when this session runs nothing in Docker.
        self._lock_dir = lock_dir
        # The checkout this session runs from, recorded in the lock file for whoever finds it.
        self._repo_root = repo_root
        # The one resource this session locks: its checkout's build tree.
        self._resource = build_tree_resource(repo_root)
        # How long acquire keeps retrying while another session holds the lock.
        self._wait_seconds = wait_seconds
        # The pause between two attempts while waiting.
        self._poll_seconds = poll_seconds
        # Monotonic seconds that measure the wait; replaced in tests.
        self._clock = clock
        # Pauses between attempts; replaced in tests.
        self._sleep = sleep
        # Tells the person running the tests who holds the lock while acquire waits.
        self._report = report
        # Tells whether the session that wrote a harness lock file is still alive; replaced in tests.
        self._process_is_running = process_is_running
        # This session's own lock file while it is held, otherwise None.
        self._lock_path: Optional[Path] = None
        # Dead harness lock files this session could not remove and has already reported.
        self._unremovable_reported: set[str] = set()

    @property
    def path(self) -> Path | None:
        """This session's lock file while held, otherwise None."""
        return self._lock_path

    def acquire(self, purpose: str) -> None:
        """Takes the lock, waiting up to wait_seconds while another session holds this checkout's build tree.

        Raises DockerLockHeld when the wait runs out, and RuntimeError at once when the lock
        directory is missing.
        """
        if self._lock_dir is None:
            return
        deadline = self._clock() + self._wait_seconds
        last_reported: Optional[str] = None
        while True:
            try:
                self._try_acquire(purpose)
                return
            except DockerLockHeld as held:
                if self._clock() >= deadline:
                    raise
                holders = str(held)
                if holders != last_reported:
                    self._report(f"waiting up to {self._wait_seconds:.0f}s for the Docker lock: {holders}")
                    last_reported = holders
                self._sleep(self._poll_seconds)

    def _try_acquire(self, purpose: str) -> None:
        """Makes one attempt to take the lock; raises DockerLockHeld when another session holds this session's resource."""
        assert self._lock_dir is not None
        if not self._lock_dir.is_dir():
            raise RuntimeError(f"Docker lock directory {self._lock_dir} is missing; create it (see tests/integration/README.md, Shared Docker lock)")
        self._remove_dead_harness_locks()
        holders = self._holders()
        if holders:
            raise DockerLockHeld(f"another session holds {self._resource} ({', '.join(holders)}); see tests/integration/README.md")
        candidate = self._lock_dir / f"{LOCK_FILE_PREFIX}-{os.getpid()}-{uuid.uuid4().hex[:8]}.lock"
        started = datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
        body = (
            f"session: {LOCK_FILE_PREFIX}\n"
            f"pid: {os.getpid()}\n"
            f"repo: {self._repo_root}\n"
            f"resources: {self._resource}\n"
            "service: rots\n"
            f"purpose: {purpose}\n"
            f"started: {started}\n"
            "expected: pytest session\n"
        )
        try:
            # Exclusive creation never overwrites another session's file.
            with candidate.open("x", encoding="utf-8") as handle:
                handle.write(body)
        except FileExistsError as collision:
            raise DockerLockHeld(f"lock name collision on {candidate.name}") from collision
        self._lock_path = candidate
        # Two sessions starting together can both find no holder. Rechecking after
        # this file exists means at least one of them sees the other and backs off, so both may
        # retry but they never both run.
        holders = self._holders(excluding=candidate)
        if holders:
            self.release()
            raise DockerLockHeld(f"another session took {self._resource} at the same time ({', '.join(holders)}); see tests/integration/README.md")

    def _holders(self, excluding: Optional[Path] = None) -> list[str]:
        """The sorted names of the lock files, other than excluding, that hold this session's resource.

        A lock file that cannot be read or decoded counts as a holder; one removed while the
        directory is read does not.
        """
        assert self._lock_dir is not None
        holders = []
        for lock_path in sorted(self._lock_dir.glob("*.lock")):
            if lock_path == excluding:
                continue
            try:
                text = lock_path.read_text(encoding="utf-8")
            except FileNotFoundError:
                continue
            except (OSError, UnicodeDecodeError):
                holders.append(lock_path.name)
                continue
            if lock_holds(text, self._resource, self._repo_root.name):
                holders.append(lock_path.name)
        return holders

    def _remove_dead_harness_locks(self) -> None:
        """Removes every harness lock file whose session's process is no longer running on this host.

        Lock files named any other way belong to other Docker jobs and are left for a person to judge.
        A dead holder's file that cannot be removed (another user's, in a sticky directory) is
        reported once and then waited for like a live holder.
        """
        assert self._lock_dir is not None
        for lock_path in sorted(self._lock_dir.glob("*.lock")):
            name_match = HARNESS_LOCK_NAME.fullmatch(lock_path.name)
            if name_match is None:
                continue
            holder_pid = int(name_match.group(1))
            if self._process_is_running(holder_pid):
                continue
            try:
                lock_path.unlink(missing_ok=True)
            except OSError as failure:
                if lock_path.name not in self._unremovable_reported:
                    self._unremovable_reported.add(lock_path.name)
                    self._report(f"cannot remove stale Docker lock {lock_path.name} (process {holder_pid} is no longer running): {failure}")
                continue
            self._report(f"removed stale Docker lock {lock_path.name}: process {holder_pid} is no longer running")

    def release(self) -> None:
        """Removes this session's lock file if it holds one; does nothing otherwise."""
        if self._lock_path is not None:
            try:
                self._lock_path.unlink()
            except FileNotFoundError:
                pass
        self._lock_path = None
