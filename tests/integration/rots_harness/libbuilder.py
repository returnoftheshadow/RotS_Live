"""Libs for harness runs: a template per character set, built once per session, and a copy per test."""

from __future__ import annotations

import hashlib
import json
import shutil
from datetime import datetime, timezone
from pathlib import Path
from typing import Callable, Protocol

from rots_harness.characters import CharacterSet
from rots_harness.launcher import ToolResult

BUCKETS = ("A-E", "F-J", "K-O", "P-T", "U-Z", "ZZZ")
BUCKETED_DIRECTORIES = ("players", "accounts", "account_characters", "plrobjs", "exploits")
# The lib/misc files git tracks; the rest of a developer's lib/misc is live data that must not reach a run.
TRACKED_MISC_FILES = ("badsites", "crimelist", "messages", "mudlle", "mudlle.old", "socials", "wizlist")
WORLD_CATEGORIES = ("wld", "mob", "obj", "zon", "shp", "scr", "mdl")
SPEC_FILE_NAME = "fixture-spec.json"
ROTSTOOL_RELATIVE = "bin/rotstool"
BOOT_OPTIONS_RELATIVE = "misc/game_boot_options.json"
HOURS_PER_DAY = 24
REBOOT_HOUR_OFFSET = 12


class LibBuildError(RuntimeError):
    """rotstool could not write a character set's account and characters."""


class ToolRunner(Protocol):
    """Runs a repo program to completion where the i386 binaries can run."""

    def run_to_completion(self, program_relative: str, arguments: list[str], working_dir: Path, timeout: float) -> ToolResult:
        """Runs program_relative with arguments in working_dir and returns its exit status and output."""
        ...


def reboot_hour_away_from(now: datetime) -> int:
    """The UTC hour furthest from now, so a run never meets the daily reboot or its warnings."""
    return (now.astimezone(timezone.utc).hour + REBOOT_HOUR_OFFSET) % HOURS_PER_DAY


class LibTemplateBuilder:
    """Builds the template lib for one character set: copied data, the test world and rotstool's characters."""

    def __init__(self, repo_root: Path, world_dir: Path, tool_runner: ToolRunner, timeout: float = 600.0) -> None:
        self._repo_root = repo_root  # source of lib/text and lib/misc
        self._world_dir = world_dir  # the committed test world
        self._tool_runner = tool_runner  # runs rotstool where the i386 binaries can run
        self._timeout = timeout  # seconds rotstool may take; generous for QEMU

    def build(self, template_dir: Path, character_set: CharacterSet, seed: int) -> Path:
        """Builds template_dir/lib from scratch and returns it; raises LibBuildError when rotstool fails."""
        if template_dir.exists():
            shutil.rmtree(template_dir)
        lib_dir = template_dir / "lib"
        lib_dir.mkdir(parents=True)
        shutil.copytree(self._repo_root / "lib" / "text", lib_dir / "text")
        (lib_dir / "misc").mkdir()
        for file_name in TRACKED_MISC_FILES:
            shutil.copy2(self._repo_root / "lib" / "misc" / file_name, lib_dir / "misc" / file_name)
        for parent in BUCKETED_DIRECTORIES:
            for bucket in BUCKETS:
                (lib_dir / parent / bucket).mkdir(parents=True)
        (lib_dir / "boards").mkdir()
        for category in WORLD_CATEGORIES:
            shutil.copytree(self._world_dir / category, lib_dir / "world" / category)
        (template_dir / SPEC_FILE_NAME).write_text(character_set.spec_json(), encoding="utf-8")
        arguments = ["fixtures", "write", "--lib", "lib", "--random-seed", str(seed), SPEC_FILE_NAME]
        result = self._tool_runner.run_to_completion(ROTSTOOL_RELATIVE, arguments, template_dir, self._timeout)
        if result.exit_status != 0:
            raise LibBuildError(f"rotstool exited with status {result.exit_status}:\n{result.output}")
        return lib_dir


class RunLibProvider:
    """Gives each test its own lib for a character set, building each template once per session."""

    def __init__(self, builder: LibTemplateBuilder, cache_root: Path, seed: int, clock: Callable[[], datetime] = lambda: datetime.now(timezone.utc)) -> None:
        self._builder = builder  # builds a template on first use
        self._cache_root = cache_root  # one subdirectory per character set
        self._seed = seed  # passed to rotstool; part of the cache key
        self._clock = clock  # current time, for the reboot hour
        self._built: set[str] = set()  # cache keys whose template finished building this session

    def cache_key(self, character_set: CharacterSet) -> str:
        """A short name for the set's template directory, unique per seed and character spec."""
        digest = hashlib.sha256(f"{self._seed}\n{character_set.spec_json()}".encode("utf-8"))
        return digest.hexdigest()[:16]

    def lib_for(self, character_set: CharacterSet, run_dir: Path) -> Path:
        """Copies the set's template into run_dir/lib, writes the run's boot options, and returns the lib."""
        key = self.cache_key(character_set)
        template_dir = self._cache_root / key
        if key not in self._built:
            try:
                self._builder.build(template_dir, character_set, self._seed)
            except BaseException:
                shutil.rmtree(template_dir, ignore_errors=True)
                raise
            self._built.add(key)
        lib_dir = run_dir / "lib"
        shutil.copytree(template_dir / "lib", lib_dir)
        boot_options = {"daily_reboot_hour_utc": reboot_hour_away_from(self._clock())}
        (lib_dir / BOOT_OPTIONS_RELATIVE).write_text(json.dumps(boot_options, indent=2) + "\n", encoding="utf-8")
        return lib_dir

    def discard_templates(self) -> None:
        """Deletes every template and forgets they were built."""
        shutil.rmtree(self._cache_root, ignore_errors=True)
        self._built.clear()
