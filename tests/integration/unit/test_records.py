from __future__ import annotations

import json
from pathlib import Path

from rots_harness import fixtures, records


def test_read_exploits_returns_typed_records_in_file_order(tmp_path: Path) -> None:
    directory = fixtures.account_directory(tmp_path)
    directory.mkdir(parents=True)
    (directory / "harnessvictim.exploits.json").write_text(json.dumps({
        "version": 1,
        "records": [
            {"type": 11, "chtime": "now", "victim_id": 0, "victim_name": "", "victim_level": 10, "killer_level": 0, "int_param": 0},
            {"type": 2, "chtime": "now", "victim_id": 9000002, "victim_name": "Harnessmage", "victim_level": 10, "killer_level": 30, "int_param": 0},
        ],
    }), encoding="utf-8")

    result = records.read_exploits(tmp_path, "Harnessvictim")

    assert [record.type for record in result] == [records.EXPLOIT_POISON, records.EXPLOIT_DEATH]
    assert result[1].victim_name == "Harnessmage"
    assert result[1].killer_level == 30


def test_read_exploits_of_a_character_without_a_file_is_empty(tmp_path: Path) -> None:
    assert records.read_exploits(tmp_path, "Nobody") == []
