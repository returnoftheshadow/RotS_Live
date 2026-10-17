from __future__ import annotations

import os
import subprocess
import sys
from pathlib import Path

import pytest

from rots_harness import dockerlock


class FakeClock:
    def __init__(self) -> None:
        # Seconds since the test started; advances only when sleep is called.
        self.now = 0.0

    def __call__(self) -> float:
        return self.now

    def sleep(self, seconds: float) -> None:
        self.now += seconds


def test_docker_lock_refuses_while_another_lock_exists(tmp_path: Path) -> None:
    lock_dir = tmp_path / "locks"
    lock_dir.mkdir()
    (lock_dir / "someone-else.lock").write_text("session: other\n", encoding="utf-8")
    lock = dockerlock.DockerLock(lock_dir, repo_root=tmp_path, wait_seconds=0)
    with pytest.raises(dockerlock.DockerLockHeld, match="someone-else.lock"):
        lock.acquire(purpose="unit test")
    assert lock.path is None
    assert sorted(path.name for path in lock_dir.iterdir()) == ["someone-else.lock"]


def test_docker_lock_writes_a_uniquely_named_file_and_removes_it_on_release(tmp_path: Path) -> None:
    lock_dir = tmp_path / "locks"
    lock_dir.mkdir()
    lock = dockerlock.DockerLock(lock_dir, repo_root=tmp_path, wait_seconds=0)
    lock.acquire(purpose="unit test")
    lock_path = lock.path
    assert lock_path is not None and lock_path.is_file()
    assert lock_path.parent == lock_dir
    assert lock_path.name.startswith(f"{dockerlock.LOCK_FILE_PREFIX}-") and lock_path.name.endswith(".lock")
    body_lines = lock_path.read_text(encoding="utf-8").splitlines()
    assert "purpose: unit test" in body_lines
    assert f"repo: {tmp_path}" in body_lines
    assert f"resources: build:{tmp_path.name}/build" in body_lines
    assert "service: rots" in body_lines
    assert "expected: pytest session" in body_lines
    lock.release()
    assert not lock_path.exists()
    assert lock.path is None


def test_second_docker_lock_refuses_while_the_first_is_held(tmp_path: Path) -> None:
    lock_dir = tmp_path / "locks"
    lock_dir.mkdir()
    first = dockerlock.DockerLock(lock_dir, repo_root=tmp_path, wait_seconds=0)
    first.acquire(purpose="first session")
    assert first.path is not None
    second = dockerlock.DockerLock(lock_dir, repo_root=tmp_path, wait_seconds=0)
    with pytest.raises(dockerlock.DockerLockHeld, match=first.path.name):
        second.acquire(purpose="second session")
    assert second.path is None
    first.release()


def test_harness_sessions_in_different_checkouts_hold_the_lock_at_the_same_time(tmp_path: Path) -> None:
    lock_dir = tmp_path / "locks"
    lock_dir.mkdir()
    first = dockerlock.DockerLock(lock_dir, repo_root=tmp_path / "slot-1", wait_seconds=0)
    second = dockerlock.DockerLock(lock_dir, repo_root=tmp_path / "slot-2", wait_seconds=0)
    first.acquire(purpose="first session")
    second.acquire(purpose="second session")
    assert first.path is not None and first.path.exists()
    assert second.path is not None and second.path.exists()
    first.release()
    second.release()


@pytest.mark.parametrize(
    "resources",
    ["build:slot-4/build", "build:slot-1/build-jammy", "port:1024", "game:slot-1", "timing", "build:slot-4/build-jammy port:1024 game:slot-4"],
)
def test_a_lock_on_other_resources_does_not_make_acquire_wait(tmp_path: Path, resources: str) -> None:
    lock_dir = tmp_path / "locks"
    lock_dir.mkdir()
    (lock_dir / "other-job.lock").write_text(f"session: other\nrepo: {tmp_path / 'slot-1'}\nresources: {resources}\n", encoding="utf-8")
    lock = dockerlock.DockerLock(lock_dir, repo_root=tmp_path / "slot-1", wait_seconds=0)
    lock.acquire(purpose="unit test")
    assert lock.path is not None and lock.path.exists()
    lock.release()


@pytest.mark.parametrize("resources", ["build:slot-1/build", "port:1024 build:slot-1/build game:slot-1"])
def test_a_lock_naming_this_build_tree_makes_acquire_wait(tmp_path: Path, resources: str) -> None:
    lock_dir = tmp_path / "locks"
    lock_dir.mkdir()
    (lock_dir / "slot-1-build.lock").write_text(f"session: other\nresources: {resources}\n", encoding="utf-8")
    lock = dockerlock.DockerLock(lock_dir, repo_root=tmp_path / "slot-1", wait_seconds=0)
    with pytest.raises(dockerlock.DockerLockHeld, match="build:slot-1/build.*slot-1-build.lock"):
        lock.acquire(purpose="unit test")
    assert lock.path is None


def test_an_old_style_lock_holds_only_its_own_checkout(tmp_path: Path) -> None:
    lock_dir = tmp_path / "locks"
    lock_dir.mkdir()
    (lock_dir / "old-style.lock").write_text(f"session: other\nrepo: {tmp_path / 'slot-2'}\nresources:\n", encoding="utf-8")
    elsewhere = dockerlock.DockerLock(lock_dir, repo_root=tmp_path / "slot-1", wait_seconds=0)
    elsewhere.acquire(purpose="another checkout")
    assert elsewhere.path is not None
    elsewhere.release()
    same_checkout = dockerlock.DockerLock(lock_dir, repo_root=tmp_path / "slot-2", wait_seconds=0)
    with pytest.raises(dockerlock.DockerLockHeld, match="old-style.lock"):
        same_checkout.acquire(purpose="same checkout")
    assert same_checkout.path is None


def test_an_unreadable_lock_makes_acquire_wait(tmp_path: Path) -> None:
    lock_dir = tmp_path / "locks"
    lock_dir.mkdir()
    (lock_dir / "garbled.lock").write_bytes(b"resources: port:1024\n\xff\xfe\n")
    lock = dockerlock.DockerLock(lock_dir, repo_root=tmp_path / "slot-1", wait_seconds=0)
    with pytest.raises(dockerlock.DockerLockHeld, match="garbled.lock"):
        lock.acquire(purpose="unit test")
    assert lock.path is None


def test_a_lock_removed_while_the_directory_is_read_does_not_make_acquire_wait(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    lock_dir = tmp_path / "locks"
    lock_dir.mkdir()
    vanishing = lock_dir / "finished-job.lock"
    vanishing.write_text("session: other\n", encoding="utf-8")
    original_read_text = Path.read_text

    def remove_then_read(path: Path, *args: object, **kwargs: object) -> str:
        # The other job ends between listing the directory and reading its file.
        if path == vanishing:
            path.unlink(missing_ok=True)
        return original_read_text(path, *args, **kwargs)

    monkeypatch.setattr(Path, "read_text", remove_then_read)
    lock = dockerlock.DockerLock(lock_dir, repo_root=tmp_path / "slot-1", wait_seconds=0)
    lock.acquire(purpose="unit test")
    assert lock.path is not None and lock.path.exists()
    lock.release()


def test_docker_lock_fails_when_the_directory_is_missing(tmp_path: Path) -> None:
    lock = dockerlock.DockerLock(tmp_path / "missing", repo_root=tmp_path, wait_seconds=0)
    with pytest.raises(RuntimeError, match="is missing"):
        lock.acquire(purpose="unit test")
    assert not (tmp_path / "missing").exists()


def test_docker_lock_backs_off_when_another_lock_appears_after_its_own(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    lock_dir = tmp_path / "locks"
    lock_dir.mkdir()
    created_paths: list[Path] = []
    real_open = Path.open

    def open_then_plant_rival(self: Path, mode: str = "r", *args: object, **kwargs: object):
        handle = real_open(self, mode, *args, **kwargs)
        if mode == "x":
            created_paths.append(self)
            (lock_dir / "rival-session.lock").write_text("session: rival\n", encoding="utf-8")
        return handle

    monkeypatch.setattr(Path, "open", open_then_plant_rival)
    lock = dockerlock.DockerLock(lock_dir, repo_root=tmp_path, wait_seconds=0)
    with pytest.raises(dockerlock.DockerLockHeld, match="rival-session.lock"):
        lock.acquire(purpose="unit test")
    assert len(created_paths) == 1
    assert not created_paths[0].exists()
    assert lock.path is None
    assert sorted(path.name for path in lock_dir.iterdir()) == ["rival-session.lock"]


def test_docker_lock_without_a_directory_does_nothing(tmp_path: Path) -> None:
    lock = dockerlock.DockerLock(None, repo_root=tmp_path)
    lock.acquire(purpose="unit test")
    assert lock.path is None
    lock.release()


def test_the_lock_waits_for_the_holder_and_then_takes_it(tmp_path: Path) -> None:
    lock_dir = tmp_path / "locks"
    lock_dir.mkdir()
    holder = lock_dir / "other-session.lock"
    holder.write_text("session: other\n", encoding="utf-8")
    clock = FakeClock()
    reports: list[str] = []

    def sleep_then_release(seconds: float) -> None:
        clock.sleep(seconds)
        if clock.now >= 10:
            holder.unlink(missing_ok=True)

    lock = dockerlock.DockerLock(lock_dir, repo_root=tmp_path, wait_seconds=60, poll_seconds=5, clock=clock, sleep=sleep_then_release, report=reports.append)
    lock.acquire(purpose="unit test")
    assert lock.path is not None and lock.path.exists()
    assert clock.now == 10
    assert len(reports) == 1 and "other-session.lock" in reports[0]
    lock.release()


def test_the_lock_gives_up_after_the_wait_and_names_the_holder(tmp_path: Path) -> None:
    lock_dir = tmp_path / "locks"
    lock_dir.mkdir()
    (lock_dir / "other-session.lock").write_text("session: other\n", encoding="utf-8")
    clock = FakeClock()
    lock = dockerlock.DockerLock(lock_dir, repo_root=tmp_path, wait_seconds=30, poll_seconds=5, clock=clock, sleep=clock.sleep, report=lambda _message: None)
    with pytest.raises(dockerlock.DockerLockHeld, match="other-session.lock"):
        lock.acquire(purpose="unit test")
    assert clock.now >= 30
    assert sorted(path.name for path in lock_dir.iterdir()) == ["other-session.lock"]


def test_a_dead_harness_holder_is_removed_and_the_lock_taken(tmp_path: Path) -> None:
    lock_dir = tmp_path / "locks"
    lock_dir.mkdir()
    dead_holder = lock_dir / "harness-it-424242-0123abcd.lock"
    dead_holder.write_text("session: harness-it\n", encoding="utf-8")
    checked_pids: list[int] = []
    reports: list[str] = []

    def nothing_is_running(pid: int) -> bool:
        checked_pids.append(pid)
        return False

    lock = dockerlock.DockerLock(lock_dir, repo_root=tmp_path, wait_seconds=0, report=reports.append, process_is_running=nothing_is_running)
    lock.acquire(purpose="unit test")
    assert lock.path is not None and lock.path.exists()
    assert not dead_holder.exists()
    assert checked_pids == [424242]
    assert reports == ["removed stale Docker lock harness-it-424242-0123abcd.lock: process 424242 is no longer running"]
    lock.release()


def test_a_live_harness_holder_makes_acquire_wait(tmp_path: Path) -> None:
    lock_dir = tmp_path / "locks"
    lock_dir.mkdir()
    live_holder = lock_dir / "harness-it-424242-0123abcd.lock"
    live_holder.write_text("session: harness-it\n", encoding="utf-8")
    clock = FakeClock()
    reports: list[str] = []
    lock = dockerlock.DockerLock(
        lock_dir, repo_root=tmp_path, wait_seconds=30, poll_seconds=5, clock=clock, sleep=clock.sleep,
        report=reports.append, process_is_running=lambda _pid: True,
    )
    with pytest.raises(dockerlock.DockerLockHeld, match=live_holder.name):
        lock.acquire(purpose="unit test")
    assert clock.now >= 30
    assert live_holder.exists()
    assert len(reports) == 1 and reports[0].startswith("waiting up to 30s for the Docker lock") and live_holder.name in reports[0]


@pytest.mark.parametrize("lock_name", ["claude-session-31337.lock", "slot-4-424242.lock", "harness-it-99.lock.lock", "my-harness-it-424242-0123abcd.lock"])
def test_a_lock_not_named_by_the_harness_is_never_removed(tmp_path: Path, lock_name: str) -> None:
    lock_dir = tmp_path / "locks"
    lock_dir.mkdir()
    (lock_dir / lock_name).write_text("session: other\n", encoding="utf-8")
    checked_pids: list[int] = []

    def nothing_is_running(pid: int) -> bool:
        checked_pids.append(pid)
        return False

    lock = dockerlock.DockerLock(lock_dir, repo_root=tmp_path, wait_seconds=0, report=lambda _message: None, process_is_running=nothing_is_running)
    with pytest.raises(dockerlock.DockerLockHeld, match=lock_name):
        lock.acquire(purpose="unit test")
    assert sorted(path.name for path in lock_dir.iterdir()) == [lock_name]
    assert checked_pids == []


@pytest.mark.parametrize("lock_name", ["harness-it-session.lock", "harness-it-abc-0123abcd.lock", "harness-it--0123abcd.lock", "harness-it-424242.lock"])
def test_a_harness_lock_without_a_parsable_pid_is_left_alone(tmp_path: Path, lock_name: str) -> None:
    lock_dir = tmp_path / "locks"
    lock_dir.mkdir()
    (lock_dir / lock_name).write_text("session: harness-it\n", encoding="utf-8")
    lock = dockerlock.DockerLock(lock_dir, repo_root=tmp_path, wait_seconds=0, report=lambda _message: None, process_is_running=lambda _pid: False)
    with pytest.raises(dockerlock.DockerLockHeld, match=lock_name):
        lock.acquire(purpose="unit test")
    assert sorted(path.name for path in lock_dir.iterdir()) == [lock_name]


def test_a_dead_holder_already_removed_by_someone_else_is_not_an_error(tmp_path: Path) -> None:
    lock_dir = tmp_path / "locks"
    lock_dir.mkdir()
    dead_holder = lock_dir / "harness-it-424242-0123abcd.lock"
    dead_holder.write_text("session: harness-it\n", encoding="utf-8")

    def removed_while_checking(_pid: int) -> bool:
        dead_holder.unlink()
        return False

    lock = dockerlock.DockerLock(lock_dir, repo_root=tmp_path, wait_seconds=0, report=lambda _message: None, process_is_running=removed_while_checking)
    lock.acquire(purpose="unit test")
    assert lock.path is not None and lock.path.exists()
    lock.release()


def test_process_is_running_tells_a_live_process_from_a_reaped_one() -> None:
    finished = subprocess.Popen([sys.executable, "-c", "pass"])
    finished.wait()
    assert dockerlock.process_is_running(os.getpid())
    assert not dockerlock.process_is_running(finished.pid)


def test_process_is_running_counts_a_process_it_may_not_signal_as_running(monkeypatch: pytest.MonkeyPatch) -> None:
    def refuse(_pid: int, _signal: int) -> None:
        raise PermissionError("not permitted")

    monkeypatch.setattr(dockerlock.os, "kill", refuse)
    assert dockerlock.process_is_running(1)


def test_process_is_running_counts_a_pid_too_large_to_signal_as_running() -> None:
    assert dockerlock.process_is_running(10 ** 30)


def test_a_dead_holder_that_cannot_be_removed_is_waited_for_like_a_live_one(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    lock_dir = tmp_path / "locks"
    lock_dir.mkdir()
    undeletable = lock_dir / "harness-it-424242-0123abcd.lock"
    undeletable.write_text("session: harness-it\n", encoding="utf-8")
    original_unlink = Path.unlink

    def refuse_the_dead_holder(path: Path, missing_ok: bool = False) -> None:
        # Another user's file in a sticky directory cannot be removed.
        if path == undeletable:
            raise PermissionError(13, "Permission denied", str(path))
        original_unlink(path, missing_ok=missing_ok)

    monkeypatch.setattr(Path, "unlink", refuse_the_dead_holder)
    clock = FakeClock()
    reports: list[str] = []
    lock = dockerlock.DockerLock(
        lock_dir, repo_root=tmp_path, wait_seconds=30, poll_seconds=5, clock=clock, sleep=clock.sleep,
        report=reports.append, process_is_running=lambda _pid: False,
    )
    with pytest.raises(dockerlock.DockerLockHeld, match=undeletable.name):
        lock.acquire(purpose="unit test")
    assert clock.now >= 30
    assert undeletable.exists()
    assert [report for report in reports if report.startswith("cannot remove")] == [
        f"cannot remove stale Docker lock {undeletable.name} (process 424242 is no longer running): [Errno 13] Permission denied: '{undeletable}'"
    ]
    assert len([report for report in reports if report.startswith("waiting up to")]) == 1


@pytest.mark.parametrize("text, expected", [("0", 0.0), ("2.5", 2.5), ("600", 600.0)])
def test_parse_wait_seconds_accepts_a_finite_number_of_at_least_zero(text: str, expected: float) -> None:
    assert dockerlock.parse_wait_seconds(text) == expected


@pytest.mark.parametrize("text", ["", "ten", "-1", "nan", "inf", "-inf", "infinity"])
def test_parse_wait_seconds_refuses_anything_else(text: str) -> None:
    with pytest.raises(ValueError):
        dockerlock.parse_wait_seconds(text)


def test_a_harness_lock_name_with_text_after_it_is_never_removed(tmp_path: Path) -> None:
    lock_dir = tmp_path / "locks"
    lock_dir.mkdir()
    lock_name = "harness-it-123-abcd.lock.lock"
    (lock_dir / lock_name).write_text("session: other\n", encoding="utf-8")
    lock = dockerlock.DockerLock(lock_dir, repo_root=tmp_path, wait_seconds=0, report=lambda _message: None, process_is_running=lambda _pid: False)
    with pytest.raises(dockerlock.DockerLockHeld, match=lock_name):
        lock.acquire(purpose="unit test")
    assert sorted(path.name for path in lock_dir.iterdir()) == [lock_name]
