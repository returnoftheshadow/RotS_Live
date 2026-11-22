"""Unit tests for rots_harness.saved_files, against account folders written to a temporary lib."""

from __future__ import annotations

import json
from pathlib import Path

import pytest

from rots_harness import world
from rots_harness.characters import HARNESS_EMAIL, Character
from rots_harness.saved_files import (
    CARRIED_WEAR_POSITION,
    HARNESS_ACCOUNT_FILE,
    HARNESS_ACCOUNT_FOLDER,
    HEAD_WEAR_POSITION,
    account_file,
    saved_wear_positions,
    stored_account,
)

# The character whose saved files the tests write.
WEARER = Character(name="Testwearer", race="human", sex="male", level=20, load_room=world.ROOM_ARENA_CENTRE, class_name="warrior")
# Where the repository keeps the game's headers.
SOURCE_DIRECTORY = Path(__file__).resolve().parents[3] / "src"


def write_json(path: Path, record: dict) -> None:
    """Writes record to path as JSON, making its folders."""
    path.parent.mkdir(parents=True, exist_ok=True)
    record_text = json.dumps(record)
    path.write_text(record_text, encoding="utf-8")


def test_the_harness_account_folder_is_the_harness_emails() -> None:
    assert HARNESS_ACCOUNT_FOLDER == f"accounts/F-J/{HARNESS_EMAIL}"
    assert HARNESS_ACCOUNT_FILE == f"accounts/F-J/{HARNESS_EMAIL}/account.json"


def test_stored_account_reads_the_harness_account_record(tmp_path: Path) -> None:
    write_json(tmp_path / HARNESS_ACCOUNT_FILE, {"roster_sort": "level"})
    assert stored_account(tmp_path) == {"roster_sort": "level"}


def test_account_file_finds_the_one_file_in_any_account_folder(tmp_path: Path) -> None:
    saved_path = tmp_path / "accounts" / "K-O" / "other@example.com" / "testwearer.objects.json"
    write_json(saved_path, {"objects": []})
    assert account_file(tmp_path, WEARER, "objects") == saved_path


def test_account_file_fails_naming_the_file_when_there_is_none_or_two(tmp_path: Path) -> None:
    with pytest.raises(AssertionError, match="expected one testwearer.character.json"):
        account_file(tmp_path, WEARER, "character")
    write_json(tmp_path / HARNESS_ACCOUNT_FOLDER / "testwearer.character.json", {})
    write_json(tmp_path / "accounts" / "K-O" / "other@example.com" / "testwearer.character.json", {})
    with pytest.raises(AssertionError, match="expected one testwearer.character.json"):
        account_file(tmp_path, WEARER, "character")


def test_saved_wear_positions_map_each_vnum_to_its_wear_position(tmp_path: Path) -> None:
    objects = [
        {"item_number": world.OBJECT_LEATHER_CAP, "wear_pos": HEAD_WEAR_POSITION},
        {"item_number": world.OBJECT_LEATHER_BAG, "wear_pos": CARRIED_WEAR_POSITION},
    ]
    write_json(tmp_path / HARNESS_ACCOUNT_FOLDER / "testwearer.objects.json", {"objects": objects})
    assert saved_wear_positions(tmp_path, WEARER) == {world.OBJECT_LEATHER_CAP: HEAD_WEAR_POSITION, world.OBJECT_LEATHER_BAG: CARRIED_WEAR_POSITION}


def test_the_wear_positions_are_the_headers() -> None:
    structs_text = (SOURCE_DIRECTORY / "structs.h").read_text(encoding="latin-1")
    assert f"inline constexpr int WEAR_HEAD = {HEAD_WEAR_POSITION};" in structs_text
    assert f"inline constexpr int MAX_WEAR = {CARRIED_WEAR_POSITION};" in structs_text
