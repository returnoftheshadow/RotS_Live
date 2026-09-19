"""Reads a character's on-disk JSON records for assertions."""

from __future__ import annotations

import json
from dataclasses import dataclass
from pathlib import Path

from rots_harness import fixtures

EXPLOIT_PK = 1
EXPLOIT_DEATH = 2
EXPLOIT_MOBDEATH = 6
EXPLOIT_POISON = 11


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
