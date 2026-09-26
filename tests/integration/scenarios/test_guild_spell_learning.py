"""A guildmaster teaches mist of baazunga and blaze only to a player of a race it will teach,
with the spell's specialization and a mage level at or above the spell's level.

SPECIAL(guild) (spec_pro.cpp) refuses a race outside the mob's will_teach mask, then, for a named
spell, refuses a LEARN_SPEC spell to a player of another specialization and any spell above the
player's mage level; the list it prints on a bare `practice` hides the spells failing either of
the last two checks. The teach values are guildmasters[] (consts.cpp), selected by the mob-file
program number, and the gates are skills[]'s level, learn_type and skill_spec. A gtest cannot
pin this together with the test world's copies of the real mobs (world/mob/guildmasters.mob),
whose program numbers and will_teach masks decide which table and which races apply.

Knowledge is read from the guildmaster's own list and from the `skills` (practice sessions spent
on each skill) of the character file the learner's quit writes, not from the practice reply.
"""
from __future__ import annotations

import re

import pytest

from rots_harness import fixtures, records
from rots_harness.session import GameSession

pytestmark = pytest.mark.scenario

MIST = "mist of baazunga"
BLAZE = "blaze"
MIST_KEY = "mist_of_baazunga"  # skill_key_for_index() slug (character_json.cpp)
BLAZE_KEY = "blaze"

PLRSPEC_FIRE = 1  # structs.h; blaze's skill_spec
PLRSPEC_DARK = 16  # structs.h; mist of baazunga's skill_spec

RACE_URUK_HAI = 11
RACE_ORC = 13

TRAVELLER = 1503  # guildmasters[] table 7, will_teach 63
URUK_GUILDMASTER = 2043  # table 58, will_teach Orc | Uruk-hai
URUK_MAGE = 4601  # table 12, will_teach Uruk-hai
SCORTHER = 10003  # table 16, will_teach 63
MAGUS = 13600  # table 30, will_teach Uruk-Lhuth

NOT_DEDICATED = "You are not dedicated enough for this."  # SPECIAL(guild), spec_pro.cpp
NOT_EXPERIENCED = "You are not experienced enough for this."  # SPECIAL(guild), spec_pro.cpp
WONT_TEACH = "Go away, I won't teach you anything!"  # SPECIAL(guild)'s do_say on a WILL_TEACH miss

PRACTICE_SESSIONS = 10
LEARNER_MAGE_COEFFICIENT = 100  # wizset coof_mage: a realistic coefficient for a level-27 mage
LESSON_ROOM = fixtures.ROOM_CORRIDOR_TWO
LESSON_ROOM_NAME = "Corridor Two"


def _stage_lesson(imp: GameSession, learner: GameSession, learner_name: str, trainer_vnum: int, race: int, specialization: int) -> None:
    """Load the trainer into the lesson room and bring the learner there with the given race and
    specialization and enough practice sessions."""
    imp.command(f"goto {LESSON_ROOM}")
    loaded = imp.command(f"load mob {trainer_vnum}")
    assert loaded.contains("You create"), f"mob {trainer_vnum} did not load:\n{loaded.text}"
    # The fixture's mage coefficient of 27 gives an Orc or Uruk-hai a negative starting knowledge
    # (recalc_skills), which the unsigned knowledge byte wraps; a real level-27 mage's coefficient
    # is far higher, so give the learner one that yields true percentages. Its mage level is unchanged.
    staged = (("race", race), ("specialization", specialization), ("practices", PRACTICE_SESSIONS), ("coof_mage", LEARNER_MAGE_COEFFICIENT))
    for field, value in staged:
        reply = imp.command(f"wizset {learner_name} {field} {value}")
        assert reply.contains(f"{field} set to {value}"), reply.text
    imp.command(f"transfer {learner_name}")
    learner.expect_room(LESSON_ROOM_NAME)


def _listed_knowledge(listing: str, spell: str) -> int | None:
    """The learner's knowledge of `spell` from the guild list (`%-25s %3d%%     Taught to:`,
    SPECIAL(guild)), or None when the list leaves the spell out."""
    match = re.search(rf"^\r?{re.escape(spell)}\s+(\d+)%\s+Taught to:", listing, flags=re.MULTILINE)
    return None if match is None else int(match.group(1))


def _saved_practices(server, learner: GameSession, learner_name: str, skill_key: str) -> int:
    """Practice sessions spent on `skill_key`, from the character file the learner's quit writes.
    The session is closed afterwards, so the fixture's teardown skips its own quit."""
    learner.quit()
    return int(records.read_character(server.lib_dir, learner_name)["skills"].get(skill_key, 0))


@pytest.mark.parametrize(
    ("trainer_vnum", "race", "spell", "skill_key", "specialization"),
    [
        pytest.param(MAGUS, fixtures.RACE_MAGUS, MIST, MIST_KEY, PLRSPEC_DARK, id="mist-magus"),
        pytest.param(URUK_MAGE, RACE_URUK_HAI, MIST, MIST_KEY, PLRSPEC_DARK, id="mist-young-uruk-mage"),
        pytest.param(URUK_GUILDMASTER, RACE_ORC, MIST, MIST_KEY, PLRSPEC_DARK, id="mist-uruk-guildmaster"),
        pytest.param(TRAVELLER, fixtures.RACE_HUMAN, BLAZE, BLAZE_KEY, PLRSPEC_FIRE, id="blaze-traveller"),
        pytest.param(SCORTHER, fixtures.RACE_HUMAN, BLAZE, BLAZE_KEY, PLRSPEC_FIRE, id="blaze-scorther"),
        pytest.param(URUK_MAGE, RACE_URUK_HAI, BLAZE, BLAZE_KEY, PLRSPEC_FIRE, id="blaze-young-uruk-mage"),
    ],
)
def test_a_qualified_learner_is_taught_the_spell(server, imp, pupil, trainer_vnum, race, spell, skill_key, specialization) -> None:
    """Pins that each trainer lists the spell to a learner of an allowed race, the spell's
    specialization and mage level 27 (at mist's level, above blaze's), and that practising it
    spends one session and raises the learner's knowledge."""
    _stage_lesson(imp, pupil, "harnpupil", trainer_vnum, race, specialization)

    before = pupil.command("practice")
    assert _listed_knowledge(before.text, spell) == 0, f"{spell} missing from mob {trainer_vnum}'s list:\n{before.text}"

    pupil.command(f"practice {spell}")

    after = pupil.command("practice")
    knowledge = _listed_knowledge(after.text, spell)
    assert knowledge is not None and knowledge > 0, f"{spell} knowledge did not rise:\n{after.text}"
    assert _saved_practices(server, pupil, "Harnpupil", skill_key) == 1


@pytest.mark.parametrize(
    ("trainer_vnum", "race", "spell", "skill_key", "other_specialization"),
    [
        pytest.param(MAGUS, fixtures.RACE_MAGUS, MIST, MIST_KEY, PLRSPEC_FIRE, id="mist-magus"),
        pytest.param(TRAVELLER, fixtures.RACE_HUMAN, BLAZE, BLAZE_KEY, PLRSPEC_DARK, id="blaze-traveller"),
    ],
)
def test_another_specialization_is_not_taught_the_spell(server, imp, pupil, trainer_vnum, race, spell, skill_key, other_specialization) -> None:
    """Pins the LEARN_SPEC gate: the qualified learner with another specialization does not see
    the spell listed, is refused as not dedicated, and its knowledge stays at zero."""
    _stage_lesson(imp, pupil, "harnpupil", trainer_vnum, race, other_specialization)

    listing = pupil.command("practice")
    assert _listed_knowledge(listing.text, spell) is None, listing.text

    refusal = pupil.command(f"practice {spell}")
    assert refusal.contains(NOT_DEDICATED), refusal.text
    assert _saved_practices(server, pupil, "Harnpupil", skill_key) == 0


@pytest.mark.parametrize(
    ("trainer_vnum", "race", "spell", "skill_key", "specialization"),
    [
        pytest.param(MAGUS, fixtures.RACE_MAGUS, MIST, MIST_KEY, PLRSPEC_DARK, id="mist-magus"),
        pytest.param(TRAVELLER, fixtures.RACE_HUMAN, BLAZE, BLAZE_KEY, PLRSPEC_FIRE, id="blaze-traveller"),
    ],
)
def test_a_low_mage_level_is_not_taught_the_spell(server, imp, novice, trainer_vnum, race, spell, skill_key, specialization) -> None:
    """Pins the level gate: a learner of the right race and specialization at mage level 17
    (below mist's 27 and blaze's 18) does not see the spell listed, is refused as not
    experienced, and its knowledge stays at zero."""
    _stage_lesson(imp, novice, "harnnovice", trainer_vnum, race, specialization)

    listing = novice.command("practice")
    assert _listed_knowledge(listing.text, spell) is None, listing.text

    refusal = novice.command(f"practice {spell}")
    assert refusal.contains(NOT_EXPERIENCED), refusal.text
    assert _saved_practices(server, novice, "Harnnovice", skill_key) == 0


def test_the_magus_turns_a_human_away(server, imp, pupil) -> None:
    """Pins the will_teach race gate: the Magus teaches Uruk-Lhuth only, so a Human dark mage
    of mist's level is sent away and learns nothing."""
    _stage_lesson(imp, pupil, "harnpupil", MAGUS, fixtures.RACE_HUMAN, PLRSPEC_DARK)

    refusal = pupil.command(f"practice {MIST}")
    assert refusal.contains(WONT_TEACH), refusal.text
    assert _saved_practices(server, pupil, "Harnpupil", MIST_KEY) == 0
