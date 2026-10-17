from __future__ import annotations

import json
from datetime import datetime, timezone
from pathlib import Path

import pytest

from rots_harness import libbuilder
from rots_harness.characters import IMPLEMENTOR, CharacterSet, custom_mage
from rots_harness.launcher import ToolResult


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


def test_discard_templates_removes_the_cache(tmp_path: Path) -> None:
    provider = make_provider(tmp_path, FakeToolRunner())
    provider.lib_for(CharacterSet((IMPLEMENTOR,)), tmp_path / "runs" / "a")
    provider.discard_templates()
    assert not (tmp_path / "libs").exists()


def test_each_seed_gets_its_own_template(tmp_path: Path) -> None:
    world = make_repo(tmp_path)
    runner = FakeToolRunner()
    builder = libbuilder.LibTemplateBuilder(tmp_path, world, runner)
    for seed in (7, 8):
        provider = libbuilder.RunLibProvider(builder, tmp_path / "libs", seed=seed)
        provider.lib_for(CharacterSet((IMPLEMENTOR,)), tmp_path / "runs" / str(seed))
    assert len(runner.calls) == 2
    assert len(list((tmp_path / "libs").iterdir())) == 2, "a template built for one seed must not be reused for another"
