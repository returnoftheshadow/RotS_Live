"""A character file written before "mist of baazunga" became "mists of burzum" still logs in.

A skill is saved under the slug of its name (skill_key_for_index, character_json.cpp), and the
reader refuses the whole file on an unknown skill key, so the rename would lock out every
character saved with the old key. The reader maps the old key through kLegacySkillKeys; the next
save writes the new one. The gtests in character_json_tests.cpp pin the mapping itself; this
pins it through a real login and quit.
"""
from __future__ import annotations

import json

import pytest

from rots_harness import fixtures, records
from rots_harness.session import GameSession

pytestmark = pytest.mark.scenario

LEARNER = "Harnnovice"
LEGACY_KEY = "mist_of_baazunga"
CURRENT_KEY = "mists_of_burzum"
PRACTICES = 5


def test_a_character_saved_under_the_old_mist_key_logs_in_and_resaves_under_the_new_one(server) -> None:
    # The server reads a character's file at login, so rewriting it before logging in stands in
    # for a file saved by an older build.
    path = fixtures.account_directory(server.lib_dir) / f"{LEARNER.lower()}.character.json"
    data = json.loads(path.read_text(encoding="utf-8"))
    data["skills"] = {LEGACY_KEY: PRACTICES}
    path.write_text(json.dumps(data, indent=2), encoding="utf-8")

    learner = GameSession(server.handle, server.spec(LEARNER), server.character_number(LEARNER), server.run_dir)
    learner.login()
    learner.quit()

    skills = records.read_character(server.lib_dir, LEARNER)["skills"]
    assert skills.get(CURRENT_KEY) == PRACTICES, skills
    assert LEGACY_KEY not in skills, skills
