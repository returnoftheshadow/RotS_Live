"""Reads what the test server saved under its lib's accounts folder.

The account layer keeps one folder per account, accounts/<letter range>/<email>, holding the
account's record and each linked character's files (account_management.cpp). Each reader takes
the test's lib folder (server.lib_dir) and reads the file as the server last wrote it; nothing
waits for a save.
"""

from __future__ import annotations

import json
from pathlib import Path

from rots_harness.characters import HARNESS_EMAIL, Character

# The harness account's folder in the lib, as rotstool writes it for the account layer
# (account_file_path_from_email, account_management.cpp).
HARNESS_ACCOUNT_FOLDER = f"accounts/F-J/{HARNESS_EMAIL}"
# The harness account's record in that folder.
HARNESS_ACCOUNT_FILE = f"{HARNESS_ACCOUNT_FOLDER}/account.json"
# The wear position a saved object records for the head (WEAR_HEAD, src/structs.h).
HEAD_WEAR_POSITION = 6
# The wear position a saved object records when it is carried rather than worn (MAX_WEAR,
# src/structs.h).
CARRIED_WEAR_POSITION = 22


def stored_account(lib_dir: Path) -> dict:
    """The harness account's record, HARNESS_ACCOUNT_FILE, as the server last wrote it."""
    record_text = (lib_dir / HARNESS_ACCOUNT_FILE).read_text(encoding="utf-8")
    return json.loads(record_text)


def account_file(lib_dir: Path, character: Character, kind: str) -> Path:
    """The path of character's <name>.<kind>.json, kind such as "character" or "objects".

    Looks in every account folder and asserts exactly one such file exists.
    """
    file_name = f"{character.name.lower()}.{kind}.json"
    matches = (lib_dir / "accounts").glob(f"*/*/{file_name}")
    paths = sorted(matches)
    assert len(paths) == 1, f"expected one {file_name} under accounts: {paths}"
    return paths[0]


def saved_wear_positions(lib_dir: Path, character: Character) -> dict[int, int]:
    """Each object vnum in character's saved objects file, with the wear position it was saved at.

    The file is found as account_file finds it. An object saved twice keeps its last position.
    """
    objects_path = account_file(lib_dir, character, "objects")
    saved_text = objects_path.read_text(encoding="utf-8")
    saved = json.loads(saved_text)
    return {entry["item_number"]: entry["wear_pos"] for entry in saved["objects"]}
