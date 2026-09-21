"""Account-native character fixtures written straight into a run's lib directory.

Seeding JSON before boot skips the login-flow account creation (email verification,
and the QEMU crash in that path) and gives every character explicit spell knowledge.
"""

from __future__ import annotations

import copy
import json
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Sequence

HARNESS_EMAIL = "harness@example.com"
HARNESS_ACCOUNT_NAME = "harness"
HARNESS_PASSWORD = "Harness1x"
# openssl passwd -6 -salt harnesssalt Harness1x ; the server verifies with crypt(3).
HARNESS_PASSWORD_HASH = "$6$harnesssalt$Zg5ujyEGeH6uCYrL2SwKp4kILHQ1c4vjwbBieWFGPwiir7Cse6SaAzA1okLWkqYwrzW9fWT2gduXOoTT7cSUY/"
HARNESS_PASSWORD_SALT = "harnesssalt"
EXPECTED_SCHEMA_VERSION = 1

RACE_GOD = 0  # other_side() treats God as neither side, so a God sees and targets everyone by name
RACE_HUMAN = 1
RACE_WOOD_ELF = 3
RACE_MAGUS = 15  # Uruk-Lhuth: evil side, so it may attack the elf victim

ROOM_IMMORTAL_START = 1101
ROOM_ARENA_WEST = 1130
ROOM_ARENA_CENTRE = 1131
ROOM_ARENA_EAST = 1132
ROOM_DARK_CELL = 1133
ROOM_CORRIDOR_ONE = 1134
ROOM_CORRIDOR_TWO = 1135
ROOM_CREVICE_FLOOR = 1136
ROOM_DISTANT_CELL = 1201  # zone 12 at map (4,2): squared distance 20 from zone 11 (test_world.py)
ROOM_WOOD_ELF_START = 1170

# src/structs.h: RENT_CRASH and MAX_MAXBOARD. write_default_account_object_file
# (src/account_management_assets.cpp) forces rentcode to RENT_CRASH on a fresh
# character's objects file; MAX_MAXBOARD sizes the fixed board_points array.
RENT_CRASH = 1
OBJECTS_BOARD_COUNT = 22


@dataclass(frozen=True)
class CharacterSpec:
    name: str
    race: int
    level: int
    load_room: int
    professions: dict[str, int] = field(default_factory=dict)
    skills: dict[str, int] = field(default_factory=dict)
    hit: int = 100
    mana: int = 100
    move: int = 100
    idnum: int = 0


MAGE_SKILLS = {
    "blaze": 100,
    "poison": 100,
    "mist_of_baazunga": 100,
    "haze": 100,
    "summon": 100,
    "earthquake": 100,
}

STANDARD_ROSTER: tuple[CharacterSpec, ...] = (
    CharacterSpec("Harnimp", RACE_GOD, 100, ROOM_IMMORTAL_START, {"mage": 30, "mystic": 30, "ranger": 30, "warrior": 30}, {}, 1000, 1000, 1000, 9000001),
    CharacterSpec("Harnmage", RACE_MAGUS, 30, ROOM_ARENA_CENTRE, {"mage": 30, "mystic": 30}, MAGE_SKILLS, 200, 600, 200, 9000002),
    CharacterSpec("Harnfighter", RACE_HUMAN, 20, ROOM_ARENA_CENTRE, {"warrior": 20}, {}, 200, 50, 200, 9000003),
    CharacterSpec("Harnvictim", RACE_WOOD_ELF, 10, ROOM_ARENA_CENTRE, {"ranger": 10}, {}, 60, 40, 120, 9000004),
    # Human, not magus: other_side() puts a magus on the opposite side from the wood-elf victim,
    # and spell_summon fails across sides. The summon and relogin scenarios use this caster,
    # and the splash scenario needs a human caster with fireball.
    CharacterSpec("Harncaller", RACE_HUMAN, 30, ROOM_ARENA_CENTRE, {"mage": 30}, {"summon": 100, "blaze": 100, "fireball": 100}, 200, 600, 200, 9000005),
)


def account_bucket_for_name(name: str) -> str:
    normalized = name.strip().lower()
    if not normalized:
        return "ZZZ"
    first = normalized[0]
    for letters, bucket in (("abcde", "A-E"), ("fghij", "F-J"), ("klmno", "K-O"), ("pqrst", "P-T"), ("uvwxyz", "U-Z")):
        if first in letters:
            return bucket
    return "ZZZ"


def account_directory(lib_root: Path, email: str = HARNESS_EMAIL) -> Path:
    return lib_root / "accounts" / account_bucket_for_name(email) / email


def load_character_template(path: Path) -> dict:
    template = json.loads(path.read_text(encoding="utf-8"))
    version = template.get("schema_version")
    if version != EXPECTED_SCHEMA_VERSION:
        raise ValueError(f"character template schema_version {version!r} != {EXPECTED_SCHEMA_VERSION}; recapture the template from a current save")
    return template


def _character_link(name: str) -> dict[str, str]:
    lowered = name.lower()
    return {
        "character_name": lowered,
        "character_path": f"{lowered}.character.json",
        "object_path": f"{lowered}.objects.json",
        "exploits_path": f"{lowered}.exploits.json",
    }


def _empty_object_save_document() -> dict:
    """The shape write_default_account_object_file gives a brand-new character:
    a default ObjectSaveData (src/objects_json.h) with rentcode forced to
    RENT_CRASH. deserialize_objects_from_json (src/objects_json.cpp) requires
    every one of these top-level keys, so a bare {"version": 1, "objects": []}
    fails to load.
    """
    return {
        "version": 1,
        "rent": {
            "time": 0,
            "rentcode": RENT_CRASH,
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
        "board_points": [0] * OBJECTS_BOARD_COUNT,
        "aliases": [],
        "followers": [],
    }


def write_account(lib_root: Path, roster: Sequence[CharacterSpec]) -> Path:
    directory = account_directory(lib_root)
    directory.mkdir(parents=True, exist_ok=True)
    now = int(time.time())
    account = {
        "version": 1,
        "account_name": HARNESS_ACCOUNT_NAME,
        "normalized_email": HARNESS_EMAIL,
        "password_hash": HARNESS_PASSWORD_HASH,
        "password_salt": HARNESS_PASSWORD_SALT,
        "characters": [spec.name.lower() for spec in roster],
        "character_links": [_character_link(spec.name) for spec in roster],
        "email_verified": True,
        "email_verified_by": "harness-fixture",
        "email_verified_at": now,
        "verification_code_hash": "",
        "verification_code_sent_at": 0,
        "verification_code_expires_at": 0,
        "verification_attempt_count": 0,
        "verification_last_attempt_at": 0,
        "blocked": False,
        "block_reason": "",
        "blocked_by": "",
        "blocked_at": 0,
        "created_at": now,
        "updated_at": now,
        "password_reset_at": 0,
        "password_reset_by": "",
        "failed_login_count": 0,
        "failed_login_last_at": 0,
        "failed_login_last_host": "",
        "password_reset_code_hash": "",
        "password_reset_code_sent_at": 0,
        "password_reset_code_expires_at": 0,
        "password_reset_attempt_count": 0,
    }
    account_path = directory / "account.json"
    account_path.write_text(json.dumps(account, indent=2) + "\n", encoding="utf-8")
    return account_path


def write_character(lib_root: Path, spec: CharacterSpec, template: dict) -> Path:
    directory = account_directory(lib_root)
    directory.mkdir(parents=True, exist_ok=True)
    data = copy.deepcopy(template)
    now = int(time.time())

    data["character_name"] = spec.name
    data["title"] = "the Harness Fixture"
    data["identity"]["idnum"] = spec.idnum
    data["identity"]["race"] = spec.race
    data["progression"]["level"] = spec.level
    data["progression"]["mini_level"] = spec.level * 100
    data["progression"]["max_mini_level"] = spec.level * 100
    for pool in ("temporary", "rolled"):
        data["abilities"][pool]["hit"] = spec.hit
        data["abilities"][pool]["mana"] = spec.mana
        data["abilities"][pool]["move"] = spec.move
    for profession in data["professions"]:
        level = spec.professions.get(profession, 0)
        data["professions"][profession] = {"level": level, "points": level, "coeff": level, "experience": 0}
    data["skills"] = dict(spec.skills)
    data["state"]["load_room"] = spec.load_room
    data["state"]["wimp_level"] = 0  # a fleeing victim leaves the room and survives what the scenario expects to kill it
    data["timers"] = {"birth": now, "last_logon": now, "played_seconds": 0, "retired_on": 0}
    preferences = data["flags"].setdefault("preferences", [])
    if "prompt" not in preferences:
        preferences.append("prompt")

    lowered = spec.name.lower()
    character_path = directory / f"{lowered}.character.json"
    character_path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
    (directory / f"{lowered}.objects.json").write_text(json.dumps(_empty_object_save_document(), indent=2) + "\n", encoding="utf-8")
    (directory / f"{lowered}.exploits.json").write_text(json.dumps({"version": 1, "records": []}) + "\n", encoding="utf-8")
    return character_path
