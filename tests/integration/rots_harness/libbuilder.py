"""Harness run libs: a template per character set, built once per session, and a copy per test."""

from __future__ import annotations

import hashlib
import json
import shutil
import uuid
from datetime import datetime, timezone
from pathlib import Path
from typing import Callable, Protocol, Sequence

from rots_harness.characters import CharacterSet
from rots_harness.launcher import ToolResult
from rots_harness.world_files import WORLD_CATEGORIES
from rots_harness.world_resets import WorldReset, WorldResetError, resets_key_text, write_world_resets

# The letter buckets the game files players and accounts under.
BUCKETS = ("A-E", "F-J", "K-O", "P-T", "U-Z", "ZZZ")
# The lib folders that hold one subfolder per letter bucket.
BUCKETED_DIRECTORIES = ("players", "accounts", "account_characters", "plrobjs", "exploits")
# The lib/misc files git tracks; the rest of a developer's lib/misc is live data that must not reach
# a run.
TRACKED_MISC_FILES = ("badsites", "crimelist", "messages", "mudlle", "mudlle.old", "socials", "wizlist")
# The name of the fixture spec rotstool reads, in the template folder.
SPEC_FILE_NAME = "fixture-spec.json"
# The rotstool program, relative to the repository root.
ROTSTOOL_RELATIVE = "bin/rotstool"
# The boot options file, relative to a lib, that sets the run's daily reboot hour.
BOOT_OPTIONS_RELATIVE = "misc/game_boot_options.json"
# The hours in a day, which the reboot hour wraps round.
HOURS_PER_DAY = 24
# Hours between the current hour and the reboot hour a run is given: half a day away.
REBOOT_HOUR_OFFSET = 12
# Starts the name of the folder a template is built in before it is renamed to its cache key;
# the leading dot keeps it apart from the keys, which are hexadecimal digits.
BUILDING_PREFIX = ".building-"


class LibBuildError(RuntimeError):
    """rotstool could not write a character set's account and characters, or a reset is wrong.

    A world reset is wrong when it names a vnum the world lacks.
    """


class ToolRunner(Protocol):
    """Runs a repo program to completion where the i386 binaries can run."""

    def run_to_completion(
        self, program_relative: str, arguments: list[str], working_dir: Path, timeout: float
    ) -> ToolResult:
        """Run program_relative with arguments in working_dir; return its exit status and output."""
        ...


def session_templates_dir(templates_root: Path, session_id: str) -> Path:
    """Return the folder holding one session's templates, under templates_root.

    It holds one subfolder per character set.
    """
    return templates_root / session_id


def discard_session_templates(templates_root: Path, session_id: str) -> None:
    """Delete the session's template folder and everything in it.

    Other sessions' folders are left alone.

    Does nothing when the folder does not exist; never raises.
    """
    shutil.rmtree(session_templates_dir(templates_root, session_id), ignore_errors=True)


def reboot_hour_away_from(now: datetime) -> int:
    """Return the UTC hour furthest from now.

    A run then never meets the daily reboot or its warnings.
    """
    return (now.astimezone(timezone.utc).hour + REBOOT_HOUR_OFFSET) % HOURS_PER_DAY


class LibTemplateBuilder:
    """Builds the template lib for one character set and world resets.

    The lib holds copied data, the test world with the resets' rows, and rotstool's characters.
    """

    def __init__(self, repo_root: Path, world_dir: Path, tool_runner: ToolRunner, timeout: float = 600.0) -> None:
        # The source of lib/text and lib/misc.
        self._repo_root = repo_root
        # The committed test world.
        self._world_dir = world_dir
        # Runs rotstool where the i386 binaries can run.
        self._tool_runner = tool_runner
        # Seconds rotstool may take; generous for QEMU.
        self._timeout = timeout

    def build(
        self, template_dir: Path, character_set: CharacterSet, seed: int, resets: Sequence[WorldReset] = ()
    ) -> Path:
        """Build template_dir/lib from scratch and return it.

        Raise LibBuildError when a reset names a vnum the world lacks or rotstool fails.
        """
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
        try:
            write_world_resets(lib_dir / "world", resets)
        except WorldResetError as unknown_vnum:
            raise LibBuildError(f"world resets: {unknown_vnum}") from None
        (template_dir / SPEC_FILE_NAME).write_text(character_set.spec_json(), encoding="utf-8")
        arguments = ["fixtures", "write", "--lib", "lib", "--random-seed", str(seed), SPEC_FILE_NAME]
        result = self._tool_runner.run_to_completion(ROTSTOOL_RELATIVE, arguments, template_dir, self._timeout)
        if result.exit_status != 0:
            raise LibBuildError(f"rotstool exited with status {result.exit_status}:\n{result.output}")
        return lib_dir


class RunLibProvider:
    """Gives each test its own lib for a character set.

    Each template is built at most once per session folder.

    Every process of a session (each pytest-xdist worker) has its own provider on the same
    session folder. A template appears there under its cache key only once it is complete, so
    providers share templates safely; two providers that miss the same template at the same time
    both build it, and the copy renamed first is the one both use.
    """

    def __init__(
        self,
        builder: LibTemplateBuilder,
        session_dir: Path,
        seed: int,
        clock: Callable[[], datetime] = lambda: datetime.now(timezone.utc),
    ) -> None:
        # Builds a template a test needs that the session folder lacks.
        self._builder = builder
        # This session's templates, one subfolder per cache key.
        self._session_dir = session_dir
        # Passed to rotstool; part of the cache key.
        self._seed = seed
        # The current time, for the reboot hour.
        self._clock = clock

    def cache_key(self, character_set: CharacterSet, resets: Sequence[WorldReset] = ()) -> str:
        """Return a short name for the template directory.

        The name is unique per seed, character spec and world resets.

        Without resets the key text is the seed and spec alone, so those templates keep their keys.
        """
        digest = hashlib.sha256(f"{self._seed}\n{character_set.spec_json()}{resets_key_text(resets)}".encode("utf-8"))
        return digest.hexdigest()[:16]

    def lib_for(self, character_set: CharacterSet, run_dir: Path, resets: Sequence[WorldReset] = ()) -> Path:
        """Copy the template for the set and resets into run_dir/lib and return the lib.

        Write the run's boot options there too.

        Builds the template first when the session folder has no complete one; raises
        LibBuildError when a reset names a vnum the world lacks or rotstool fails, leaving
        nothing of the failed build behind.
        """
        template_dir = self._session_dir / self.cache_key(character_set, resets)
        if not template_dir.is_dir():
            self._build(template_dir, character_set, resets)
        lib_dir = run_dir / "lib"
        shutil.copytree(template_dir / "lib", lib_dir)
        boot_options = {"daily_reboot_hour_utc": reboot_hour_away_from(self._clock())}
        (lib_dir / BOOT_OPTIONS_RELATIVE).write_text(json.dumps(boot_options, indent=2) + "\n", encoding="utf-8")
        return lib_dir

    def _build(self, template_dir: Path, character_set: CharacterSet, resets: Sequence[WorldReset]) -> None:
        """Build the template in a temporary folder beside template_dir, then rename it.

        The temporary folder is renamed to template_dir.

        When another provider renamed its build to template_dir first, keeps that one and deletes
        this build.
        """
        building_dir = template_dir.with_name(f"{BUILDING_PREFIX}{template_dir.name}-{uuid.uuid4().hex[:8]}")
        try:
            self._builder.build(building_dir, character_set, self._seed, resets)
            try:
                building_dir.rename(template_dir)
            except OSError:
                # A rename onto a folder that exists and is not empty fails on every platform;
                # only a complete template is ever renamed into place, so it can be used.
                if not template_dir.is_dir():
                    raise
        finally:
            shutil.rmtree(building_dir, ignore_errors=True)
