"""Assembles one run's lib directory: tracked data, the test world, and the fixture roster."""

from __future__ import annotations

import shutil
from dataclasses import dataclass
from pathlib import Path
from typing import Sequence

from rots_harness import fixtures

BUCKETS = ("A-E", "F-J", "K-O", "P-T", "U-Z", "ZZZ")
BUCKETED_DIRECTORIES = ("players", "accounts", "account_characters", "plrobjs", "exploits")
# The lib/misc files git tracks (see `git ls-files lib/misc`); everything else in a
# developer's lib/misc is live data and must not leak into a run.
TRACKED_MISC_FILES = ("badsites", "crimelist", "messages", "mudlle", "mudlle.old", "socials", "wizlist")
WORLD_CATEGORIES = ("wld", "mob", "obj", "zon", "shp", "scr", "mdl")


@dataclass(frozen=True)
class BuiltLib:
    lib_dir: Path
    roster: tuple[fixtures.CharacterSpec, ...]


class TestLibBuilder:
    def __init__(self, repo_root: Path, world_dir: Path, template_path: Path) -> None:
        self._repo_root = repo_root
        self._world_dir = world_dir
        self._template_path = template_path

    def build(self, run_dir: Path, roster: Sequence[fixtures.CharacterSpec]) -> BuiltLib:
        lib_dir = run_dir / "lib"
        if lib_dir.exists():
            shutil.rmtree(lib_dir)
        lib_dir.mkdir(parents=True)

        shutil.copytree(self._repo_root / "lib" / "text", lib_dir / "text")
        misc_dir = lib_dir / "misc"
        misc_dir.mkdir()
        for file_name in TRACKED_MISC_FILES:
            shutil.copy2(self._repo_root / "lib" / "misc" / file_name, misc_dir / file_name)

        for parent in BUCKETED_DIRECTORIES:
            for bucket in BUCKETS:
                (lib_dir / parent / bucket).mkdir(parents=True)
        (lib_dir / "boards").mkdir()

        world_dir = lib_dir / "world"
        world_dir.mkdir()
        for category in WORLD_CATEGORIES:
            shutil.copytree(self._world_dir / category, world_dir / category)

        template = fixtures.load_character_template(self._template_path)
        fixtures.write_account(lib_dir, roster)
        for spec in roster:
            fixtures.write_character(lib_dir, spec, template)

        return BuiltLib(lib_dir=lib_dir, roster=tuple(roster))
