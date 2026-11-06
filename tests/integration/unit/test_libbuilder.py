from __future__ import annotations

import hashlib
import json
from datetime import datetime, timezone
from pathlib import Path
from typing import Callable, Optional

import pytest

from rots_harness import libbuilder
from rots_harness.characters import IMPLEMENTOR, CharacterSet, custom_mage
from rots_harness.launcher import ToolResult
from rots_harness.world_resets import MobReset


def make_repo(root: Path) -> Path:
    """A minimal repo tree with the files the builder copies."""
    (root / "lib" / "text").mkdir(parents=True)
    (root / "lib" / "text" / "motd").write_text("motd\n", encoding="utf-8")
    (root / "lib" / "misc").mkdir()
    for file_name in libbuilder.TRACKED_MISC_FILES:
        (root / "lib" / "misc" / file_name).write_text(file_name, encoding="utf-8")
    (root / "lib" / "misc" / "pklist").write_text("live data", encoding="utf-8")
    world = root / "tests" / "integration" / "world"
    for category in libbuilder.WORLD_CATEGORIES:
        (world / category).mkdir(parents=True)
        (world / category / "index").write_text("$\n", encoding="utf-8")
    return world


class FakeToolRunner:
    def __init__(self, exit_status: int = 0, output: str = "") -> None:
        self.calls: list[tuple[str, list[str], Path]] = []
        self._result = ToolResult(exit_status, output)

    def run_to_completion(self, program_relative: str, arguments: list[str], working_dir: Path, timeout: float) -> ToolResult:
        self.calls.append((program_relative, arguments, working_dir))
        (working_dir / "lib" / "accounts" / "F-J" / "harness@example.com").mkdir(parents=True, exist_ok=True)
        return self._result


def test_the_template_has_the_copied_data_the_world_and_the_save_directories(tmp_path: Path) -> None:
    world = make_repo(tmp_path)
    runner = FakeToolRunner()
    lib = libbuilder.LibTemplateBuilder(tmp_path, world, runner).build(tmp_path / "t1", CharacterSet((IMPLEMENTOR,)), seed=7)
    assert (lib / "text" / "motd").is_file()
    # Literal names: the git-tracked lib/misc files and the server's save-file buckets.
    assert sorted(path.name for path in (lib / "misc").iterdir()) == ["badsites", "crimelist", "messages", "mudlle", "mudlle.old", "socials", "wizlist"]
    for directory in ("players", "accounts", "account_characters", "plrobjs", "exploits"):
        assert sorted(path.name for path in (lib / directory).iterdir()) == ["A-E", "F-J", "K-O", "P-T", "U-Z", "ZZZ"]
    assert (lib / "boards").is_dir()
    assert sorted(path.name for path in (lib / "world").iterdir()) == sorted(libbuilder.WORLD_CATEGORIES)


def test_rotstool_runs_in_the_template_directory_with_the_seed_and_the_spec(tmp_path: Path) -> None:
    world = make_repo(tmp_path)
    runner = FakeToolRunner()
    character_set = CharacterSet((IMPLEMENTOR,))
    libbuilder.LibTemplateBuilder(tmp_path, world, runner).build(tmp_path / "t1", character_set, seed=7)
    assert runner.calls == [("bin/rotstool", ["fixtures", "write", "--lib", "lib", "--random-seed", "7", libbuilder.SPEC_FILE_NAME], tmp_path / "t1")]
    assert (tmp_path / "t1" / libbuilder.SPEC_FILE_NAME).read_text(encoding="utf-8") == character_set.spec_json()


def test_a_rotstool_refusal_raises_with_its_message_and_status(tmp_path: Path) -> None:
    world = make_repo(tmp_path)
    runner = FakeToolRunner(exit_status=2, output="rotstool fixtures write: characters[0]: 'name': too short\n")
    with pytest.raises(libbuilder.LibBuildError, match=r"(?s)exited with status 2.*too short"):
        libbuilder.LibTemplateBuilder(tmp_path, world, runner).build(tmp_path / "t1", CharacterSet((IMPLEMENTOR,)), seed=7)


@pytest.mark.parametrize("hour, expected", [(0, 12), (10, 22), (12, 0), (23, 11)])
def test_the_reboot_hour_is_twelve_hours_away(hour: int, expected: int) -> None:
    assert libbuilder.reboot_hour_away_from(datetime(2026, 10, 11, hour, 30, tzinfo=timezone.utc)) == expected


def make_provider(tmp_path: Path, runner: FakeToolRunner, hour: int = 22) -> libbuilder.RunLibProvider:
    world = make_repo(tmp_path)
    builder = libbuilder.LibTemplateBuilder(tmp_path, world, runner)
    return libbuilder.RunLibProvider(builder, tmp_path / "libs", seed=7, clock=lambda: datetime(2026, 10, 11, hour, 0, tzinfo=timezone.utc))


def test_each_character_set_is_built_once_and_copied_per_run(tmp_path: Path) -> None:
    runner = FakeToolRunner()
    provider = make_provider(tmp_path, runner)
    first = provider.lib_for(CharacterSet((IMPLEMENTOR,)), tmp_path / "runs" / "a")
    second = provider.lib_for(CharacterSet((IMPLEMENTOR,)), tmp_path / "runs" / "b")
    assert len(runner.calls) == 1
    assert first == tmp_path / "runs" / "a" / "lib" and second == tmp_path / "runs" / "b" / "lib"
    assert (second / "accounts" / "F-J" / "harness@example.com").is_dir()


def test_a_different_character_set_gets_its_own_template(tmp_path: Path) -> None:
    runner = FakeToolRunner()
    provider = make_provider(tmp_path, runner)
    provider.lib_for(CharacterSet((IMPLEMENTOR,)), tmp_path / "runs" / "a")
    provider.lib_for(CharacterSet((IMPLEMENTOR, custom_mage())), tmp_path / "runs" / "b")
    assert len(runner.calls) == 2
    assert provider.cache_key(CharacterSet((IMPLEMENTOR,))) != provider.cache_key(CharacterSet((IMPLEMENTOR, custom_mage())))


def test_each_run_gets_boot_options_with_the_reboot_hour_and_the_template_does_not(tmp_path: Path) -> None:
    provider = make_provider(tmp_path, FakeToolRunner(), hour=22)
    character_set = CharacterSet((IMPLEMENTOR,))
    lib = provider.lib_for(character_set, tmp_path / "runs" / "a")
    assert json.loads((lib / libbuilder.BOOT_OPTIONS_RELATIVE).read_text(encoding="utf-8")) == {"daily_reboot_hour_utc": 10}
    template_lib = tmp_path / "libs" / provider.cache_key(character_set) / "lib"
    assert not (template_lib / libbuilder.BOOT_OPTIONS_RELATIVE).exists()


def test_a_failed_build_caches_nothing(tmp_path: Path) -> None:
    runner = FakeToolRunner(exit_status=2, output="refused")
    provider = make_provider(tmp_path, runner)
    character_set = CharacterSet((IMPLEMENTOR,))
    for _attempt in range(2):
        with pytest.raises(libbuilder.LibBuildError):
            provider.lib_for(character_set, tmp_path / "runs" / "a")
    assert len(runner.calls) == 2
    assert not (tmp_path / "libs" / provider.cache_key(character_set)).exists()


def test_each_seed_gets_its_own_template(tmp_path: Path) -> None:
    world = make_repo(tmp_path)
    runner = FakeToolRunner()
    builder = libbuilder.LibTemplateBuilder(tmp_path, world, runner)
    for seed in (7, 8):
        provider = libbuilder.RunLibProvider(builder, tmp_path / "libs", seed=seed)
        provider.lib_for(CharacterSet((IMPLEMENTOR,)), tmp_path / "runs" / str(seed))
    assert len(runner.calls) == 2
    assert len(list((tmp_path / "libs").iterdir())) == 2, "a template built for one seed must not be reused for another"


def make_world_with_one_zone(world: Path) -> None:
    """Writes zone 11 with room 1131 and mob 1130 into the minimal repo's world."""
    (world / "zon" / "11.zon").write_text("#11\nZone~\n~\n~\n0\n? 0 0 5\n1199\n999\n2\nS\n", encoding="latin-1")
    (world / "zon" / "index").write_text("11.zon\n$\n", encoding="latin-1")
    (world / "wld" / "11.wld").write_text("#1131\nroom~\n#99999\n", encoding="latin-1")
    (world / "wld" / "index").write_text("11.wld\n$\n", encoding="latin-1")
    (world / "mob" / "11.mob").write_text("#1130\nmob~\n#99999\n", encoding="latin-1")
    (world / "mob" / "index").write_text("11.mob\n$\n", encoding="latin-1")


def test_the_template_world_holds_the_resets_rows_and_the_committed_world_does_not(tmp_path: Path) -> None:
    world = make_repo(tmp_path)
    make_world_with_one_zone(world)
    lib = libbuilder.LibTemplateBuilder(tmp_path, world, FakeToolRunner()).build(tmp_path / "t1", CharacterSet((IMPLEMENTOR,)), seed=7, resets=(MobReset(1130, 1131),))
    assert "M 0 1130 1131 0 100 100 1 1" in (lib / "world" / "zon" / "11.zon").read_text(encoding="latin-1")
    assert "M 0" not in (world / "zon" / "11.zon").read_text(encoding="latin-1")


def test_a_reset_naming_an_unknown_room_fails_the_build_before_rotstool_runs(tmp_path: Path) -> None:
    world = make_repo(tmp_path)
    make_world_with_one_zone(world)
    runner = FakeToolRunner()
    with pytest.raises(libbuilder.LibBuildError, match="no room 1132"):
        libbuilder.LibTemplateBuilder(tmp_path, world, runner).build(tmp_path / "t1", CharacterSet((IMPLEMENTOR,)), seed=7, resets=(MobReset(1130, 1132),))
    assert runner.calls == []


def test_resets_get_their_own_template_and_no_resets_keep_the_spec_only_key(tmp_path: Path) -> None:
    runner = FakeToolRunner()
    provider = make_provider(tmp_path, runner)
    make_world_with_one_zone(tmp_path / "tests" / "integration" / "world")
    character_set = CharacterSet((IMPLEMENTOR,))
    spec_only_key = hashlib.sha256(f"7\n{character_set.spec_json()}".encode("utf-8")).hexdigest()[:16]
    assert provider.cache_key(character_set) == spec_only_key
    assert provider.cache_key(character_set, (MobReset(1130, 1131),)) != spec_only_key
    provider.lib_for(character_set, tmp_path / "runs" / "a")
    provider.lib_for(character_set, tmp_path / "runs" / "b", (MobReset(1130, 1131),))
    assert len(runner.calls) == 2


class InterleavingToolRunner(FakeToolRunner):
    """A tool runner that runs another provider's lib_for in the middle of its own first rotstool call."""

    def __init__(self, interleaved: Callable[[], None]) -> None:
        super().__init__()
        # Runs once, during the first rotstool call, as another worker's build would.
        self._interleaved: Optional[Callable[[], None]] = interleaved

    def run_to_completion(self, program_relative: str, arguments: list[str], working_dir: Path, timeout: float) -> ToolResult:
        result = super().run_to_completion(program_relative, arguments, working_dir, timeout)
        interleaved, self._interleaved = self._interleaved, None
        if interleaved is not None:
            interleaved()
        return result


def test_providers_sharing_a_session_folder_each_build_at_most_once_and_both_get_a_usable_lib(tmp_path: Path) -> None:
    world = make_repo(tmp_path)
    session_dir = tmp_path / "libs" / "session1"
    character_set = CharacterSet((IMPLEMENTOR,))
    other_runner = FakeToolRunner()
    other_provider = libbuilder.RunLibProvider(libbuilder.LibTemplateBuilder(tmp_path, world, other_runner), session_dir, seed=7)
    template_dir = session_dir / other_provider.cache_key(character_set)
    seen_while_building: list[bool] = []
    other_libs: list[Path] = []

    def other_worker_builds_first() -> None:
        seen_while_building.append(template_dir.exists())
        other_libs.append(other_provider.lib_for(character_set, tmp_path / "runs" / "other"))

    first_runner = InterleavingToolRunner(other_worker_builds_first)
    first_provider = libbuilder.RunLibProvider(libbuilder.LibTemplateBuilder(tmp_path, world, first_runner), session_dir, seed=7)
    first_lib = first_provider.lib_for(character_set, tmp_path / "runs" / "first")

    assert seen_while_building == [False], "a template must not appear under its final name before it is complete"
    assert len(first_runner.calls) == 1 and len(other_runner.calls) == 1
    for lib in (first_lib, other_libs[0]):
        assert (lib / "accounts" / "F-J" / "harness@example.com").is_dir()
    assert [path.name for path in session_dir.iterdir()] == [template_dir.name], "the losing build's temporary folder must be removed"


def test_a_completed_template_is_reused_by_another_provider_without_building(tmp_path: Path) -> None:
    world = make_repo(tmp_path)
    session_dir = tmp_path / "libs" / "session1"
    character_set = CharacterSet((IMPLEMENTOR,))
    first_runner = FakeToolRunner()
    libbuilder.RunLibProvider(libbuilder.LibTemplateBuilder(tmp_path, world, first_runner), session_dir, seed=7).lib_for(character_set, tmp_path / "runs" / "a")
    second_runner = FakeToolRunner()
    second_provider = libbuilder.RunLibProvider(libbuilder.LibTemplateBuilder(tmp_path, world, second_runner), session_dir, seed=7)

    lib = second_provider.lib_for(character_set, tmp_path / "runs" / "b")

    assert second_runner.calls == []
    assert (lib / "accounts" / "F-J" / "harness@example.com").is_dir()


def test_a_failed_build_leaves_no_temporary_folder(tmp_path: Path) -> None:
    provider = make_provider(tmp_path, FakeToolRunner(exit_status=2, output="refused"))
    with pytest.raises(libbuilder.LibBuildError):
        provider.lib_for(CharacterSet((IMPLEMENTOR,)), tmp_path / "runs" / "a")
    assert list((tmp_path / "libs").glob("*")) == []


def test_each_session_has_its_own_template_folder(tmp_path: Path) -> None:
    assert libbuilder.session_templates_dir(tmp_path / "libs", "session1") == tmp_path / "libs" / "session1"


def test_discarding_a_session_removes_only_its_folder(tmp_path: Path) -> None:
    templates_root = tmp_path / "libs"
    for session_id in ("session1", "session2"):
        (libbuilder.session_templates_dir(templates_root, session_id) / "key" / "lib").mkdir(parents=True)

    libbuilder.discard_session_templates(templates_root, "session1")

    assert [path.name for path in templates_root.iterdir()] == ["session2"]
    assert (templates_root / "session2" / "key" / "lib").is_dir()


def test_discarding_a_session_that_built_nothing_is_not_an_error(tmp_path: Path) -> None:
    libbuilder.discard_session_templates(tmp_path / "libs", "session1")
    assert not (tmp_path / "libs").exists()
