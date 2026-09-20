"""Idle/force rent through `purge <player>` (spec A2): the object file the server writes on that
path must round-trip into the account-native JSON copy without a SYSERR."""

from __future__ import annotations

import json
from pathlib import Path

import pytest

from rots_harness import fixtures

pytestmark = pytest.mark.scenario


def objects_json_path(lib_dir: Path, name: str) -> Path:
    # Account-native character files live under accounts/<bucket>/<email>/, not
    # account_characters/ (that bucketed tree exists on disk but nothing writes into
    # it); verified by listing a kept run's lib directory.
    matches = list(lib_dir.glob(f"accounts/*/*/{name.lower()}.objects.json"))
    assert len(matches) == 1, f"expected one objects file for {name}: {matches}"
    return matches[0]


def test_purging_a_player_refreshes_the_account_native_object_file(server, imp, victim, harness) -> None:
    imp.command(f"goto {fixtures.ROOM_ARENA_CENTRE}")
    imp.command("transfer harnvictim")
    before = objects_json_path(server.lib_dir, "Harnvictim").read_text()

    # do_purge closes the victim's socket directly (act_wiz.cpp), so the victim's own
    # session never sees text; the room-broadcast link-loss message lands on the imp,
    # who is standing in the room and isn't excluded from it the way the purger and the
    # purge target are excluded from the "$n disintegrates $N" message.
    purge_result = imp.command("purge harnvictim")
    assert purge_result.contains("Harnvictim has lost his link."), purge_result.text

    after_path = objects_json_path(server.lib_dir, "Harnvictim")
    after = json.loads(after_path.read_text())
    assert after_path.read_text() != before, "the refresh must rewrite the objects file"
    assert "rent" in after, after
    # The crash monitor (server fixture teardown) fails this test on any SYSERR, including the
    # truncation the old idle save produced.
