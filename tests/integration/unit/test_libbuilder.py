from __future__ import annotations

import json
from pathlib import Path

from rots_harness import fixtures
from rots_harness.libbuilder import RunLibBuilder

INTEGRATION_ROOT = Path(__file__).resolve().parents[1]
REPO_ROOT = INTEGRATION_ROOT.parents[1]


def build_into(tmp_path: Path):
    builder = RunLibBuilder(REPO_ROOT, INTEGRATION_ROOT / "world", INTEGRATION_ROOT / "fixtures" / "character.template.json")
    return builder.build(tmp_path / "run", fixtures.STANDARD_ROSTER)


def test_build_copies_tracked_text_and_misc_and_the_test_world(tmp_path: Path) -> None:
    built = build_into(tmp_path)

    assert built.lib_dir == tmp_path / "run" / "lib"
    assert (built.lib_dir / "text" / "motd").is_file()
    assert (built.lib_dir / "misc" / "messages").is_file()
    assert (built.lib_dir / "misc" / "socials").is_file()
    assert not (built.lib_dir / "misc" / "pklist").exists(), "git-ignored developer data must not leak in"
    assert (built.lib_dir / "world" / "wld" / "11.wld").is_file()
    assert (built.lib_dir / "world" / "shp" / "index").read_text(encoding="latin-1").strip() == "$"


def test_build_creates_every_runtime_directory_the_server_expects(tmp_path: Path) -> None:
    built = build_into(tmp_path)
    for parent in ("players", "accounts", "account_characters", "plrobjs", "exploits"):
        for bucket in ("A-E", "F-J", "K-O", "P-T", "U-Z", "ZZZ"):
            assert (built.lib_dir / parent / bucket).is_dir(), f"{parent}/{bucket}"
    assert (built.lib_dir / "boards").is_dir()


def test_build_seeds_the_roster(tmp_path: Path) -> None:
    built = build_into(tmp_path)
    account = json.loads((fixtures.account_directory(built.lib_dir) / "account.json").read_text(encoding="utf-8"))
    assert account["characters"] == ["harnimp", "harnmage", "harnfighter", "harnvictim", "harncaller", "harnpupil", "harnnovice"]
    assert (fixtures.account_directory(built.lib_dir) / "harnvictim.character.json").is_file()
    assert built.roster == fixtures.STANDARD_ROSTER
