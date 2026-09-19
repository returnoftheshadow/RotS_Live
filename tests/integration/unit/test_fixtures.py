from __future__ import annotations

import json
from pathlib import Path

import pytest

from rots_harness import fixtures

TEMPLATE_PATH = Path(__file__).resolve().parents[1] / "fixtures" / "character.template.json"


def test_bucket_follows_the_servers_alphabet_split() -> None:
    assert fixtures.account_bucket_for_name("harness@example.com") == "F-J"
    assert fixtures.account_bucket_for_name("Alpha") == "A-E"
    assert fixtures.account_bucket_for_name("zed") == "U-Z"
    assert fixtures.account_bucket_for_name("9lives") == "ZZZ"


def test_template_guard_rejects_an_unknown_schema(tmp_path: Path) -> None:
    bad = tmp_path / "template.json"
    bad.write_text(json.dumps({"schema_version": 2}), encoding="utf-8")
    with pytest.raises(ValueError, match="schema_version"):
        fixtures.load_character_template(bad)


def test_write_account_lists_every_roster_character_with_links(tmp_path: Path) -> None:
    account_path = fixtures.write_account(tmp_path, fixtures.STANDARD_ROSTER)

    assert account_path == tmp_path / "accounts" / "F-J" / "harness@example.com" / "account.json"
    data = json.loads(account_path.read_text(encoding="utf-8"))
    assert data["normalized_email"] == fixtures.HARNESS_EMAIL
    assert data["email_verified"] is True
    assert data["password_hash"].startswith("$6$harnesssalt$")
    assert data["characters"] == [spec.name.lower() for spec in fixtures.STANDARD_ROSTER]
    first_link = data["character_links"][0]
    assert first_link == {
        "character_name": "harnessimp",
        "character_path": "harnessimp.character.json",
        "object_path": "harnessimp.objects.json",
        "exploits_path": "harnessimp.exploits.json",
    }


def test_write_character_substitutes_the_spec_and_keeps_every_section(tmp_path: Path) -> None:
    template = fixtures.load_character_template(TEMPLATE_PATH)
    mage = next(spec for spec in fixtures.STANDARD_ROSTER if spec.name == "Harnessmage")

    character_path = fixtures.write_character(tmp_path, mage, template)

    data = json.loads(character_path.read_text(encoding="utf-8"))
    assert character_path.name == "harnessmage.character.json"
    assert data["character_name"] == "Harnessmage"
    assert data["identity"]["idnum"] == mage.idnum
    assert data["identity"]["race"] == mage.race
    assert data["progression"]["level"] == mage.level
    assert data["progression"]["mini_level"] == mage.level * 100
    assert data["professions"]["mage"]["level"] == 30
    assert data["skills"]["blaze"] == 100
    assert data["state"]["load_room"] == mage.load_room
    assert data["abilities"]["temporary"]["hit"] == mage.hit
    assert "prompt" in data["flags"]["preferences"]
    for section in ("identity", "progression", "abilities", "points", "professions", "flags", "conditions", "timers", "perception", "state", "talks", "skills", "affects"):
        assert section in data, section
    objects_path = character_path.parent / "harnessmage.objects.json"
    assert objects_path.exists()
    # write_default_account_object_file (src/account_management_assets.cpp) is the
    # production reference for a brand-new character's objects file: a default
    # ObjectSaveData (src/objects_json.h) with rentcode forced to RENT_CRASH. The
    # loader (src/objects_json.cpp deserialize_objects_from_json) requires every one
    # of these top-level keys, so the brief's placeholder {"version": 1, "objects": []}
    # would fail to load; match the real shape instead.
    assert json.loads(objects_path.read_text(encoding="utf-8")) == {
        "version": 1,
        "rent": {
            "time": 0,
            "rentcode": 1,
            "net_cost_per_hour": 0,
            "gold": 0,
            "nitems": 0,
            "spare0": 0,
            "spare1": 0,
            "spare2": 0,
            "spare3": 0,
            "spare4": 0,
            "spare5": 0,
            "spare6": 0,
            "spare7": 0,
        },
        "objects": [],
        "board_points": [0] * 22,
        "aliases": [],
        "followers": [],
    }
    assert json.loads((character_path.parent / "harnessmage.exploits.json").read_text(encoding="utf-8")) == {"version": 1, "records": []}
