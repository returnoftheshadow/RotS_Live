"""Reads what the test server saved under its lib's accounts folder.

The account layer keeps one folder per account, accounts/<letter range>/<email>, holding the
account's record and each linked character's files (account_management.cpp). Each reader takes
the test's lib folder (server.lib_dir) and reads the file as the server last wrote it; nothing
waits for a save.
"""

from __future__ import annotations

import json
from pathlib import Path

from rots_harness.characters import HARNESS_EMAIL

# The harness account's folder in the lib, as rotstool writes it for the account layer
# (account_file_path_from_email, account_management.cpp).
HARNESS_ACCOUNT_FOLDER = f"accounts/F-J/{HARNESS_EMAIL}"
# The harness account's record in that folder.
HARNESS_ACCOUNT_FILE = f"{HARNESS_ACCOUNT_FOLDER}/account.json"
# The kinds of file account storage keeps for each linked character, as account_file's kind
# (account_management.cpp).
CHARACTER_FILE_KINDS = ("character", "objects", "exploits")
# The wear position a saved object records for the head (WEAR_HEAD, src/structs.h).
HEAD_WEAR_POSITION = 6
# The wear position a saved object records when it is carried rather than worn (MAX_WEAR,
# src/structs.h).
CARRIED_WEAR_POSITION = 22


def stored_account(lib_dir: Path) -> dict:
    """Return the harness account's record, HARNESS_ACCOUNT_FILE, parsed.

    FileNotFoundError passes through when the record does not exist, and json.JSONDecodeError when
    it is not JSON.
    """
    record_text = (lib_dir / HARNESS_ACCOUNT_FILE).read_text(encoding="utf-8")
    return json.loads(record_text)


def account_file(lib_dir: Path, character_name: str, kind: str) -> Path:
    """Return the path of the character's <lowercased name>.<kind>.json.

    kind is one of CHARACTER_FILE_KINDS. Looks in every account folder and asserts exactly one
    such file exists.
    """
    file_name = f"{character_name.lower()}.{kind}.json"
    matches = (lib_dir / "accounts").glob(f"*/*/{file_name}")
    paths = sorted(matches)
    assert len(paths) == 1, f"expected one {file_name} under accounts: {paths}"
    return paths[0]


def saved_wear_positions(lib_dir: Path, character_name: str) -> dict[int, int]:
    """Return each object vnum in the character's saved objects file, with its saved wear position.

    The file is found as account_file finds it. An object saved twice keeps its last position.
    json.JSONDecodeError passes through when the file is not JSON, and KeyError when it lacks the
    objects list or an entry lacks item_number or wear_pos.
    """
    objects_path = account_file(lib_dir, character_name, "objects")
    saved_text = objects_path.read_text(encoding="utf-8")
    saved = json.loads(saved_text)
    return {entry["item_number"]: entry["wear_pos"] for entry in saved["objects"]}
