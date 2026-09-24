"""Reads a character's on-disk JSON records and the binary player-kill list for assertions."""

from __future__ import annotations

import json
import struct
from dataclasses import dataclass
from pathlib import Path

from rots_harness import fixtures

EXPLOIT_PK = 1
EXPLOIT_DEATH = 2
EXPLOIT_MOBDEATH = 6
EXPLOIT_POISON = 11

# The 32-bit PKILL struct (pkill.h): three ints, two level bytes, two padding bytes, two ints.
PKILL_FORMAT = struct.Struct("<iiiBBxxii")


@dataclass(frozen=True)
class ExploitRecord:
    type: int
    victim_name: str
    victim_level: int
    killer_level: int
    int_param: int


def read_exploits(lib_dir: Path, name: str) -> list[ExploitRecord]:
    path = fixtures.account_directory(lib_dir) / f"{name.lower()}.exploits.json"
    if not path.exists():
        return []
    data = json.loads(path.read_text(encoding="utf-8"))
    return [
        ExploitRecord(int(entry["type"]), str(entry.get("victim_name", "")), int(entry.get("victim_level", 0)), int(entry.get("killer_level", 0)), int(entry.get("int_param", 0)))
        for entry in data.get("records", [])
    ]


def read_character(lib_dir: Path, name: str) -> dict:
    path = fixtures.account_directory(lib_dir) / f"{name.lower()}.character.json"
    return json.loads(path.read_text(encoding="utf-8"))


@dataclass(frozen=True)
class PkillRecord:
    """One PKILL entry; killer_id and victim_id are idnums (pkill_update_file, pkill.cpp)."""

    kill_time: int
    killer_id: int
    victim_id: int
    killer_level: int
    victim_level: int
    killer_points: int
    victim_points: int


def read_pkills(lib_dir: Path) -> list[PkillRecord]:
    """Every record in lib/misc/pklist in file order; [] when the server never wrote one."""
    path = lib_dir / "misc" / "pklist"
    if not path.exists():
        return []
    data = path.read_bytes()
    if len(data) % PKILL_FORMAT.size != 0:
        raise ValueError(f"{path}: length {len(data)} is not a whole number of {PKILL_FORMAT.size}-byte PKILL records")
    return [PkillRecord(*fields) for fields in PKILL_FORMAT.iter_unpack(data)]
