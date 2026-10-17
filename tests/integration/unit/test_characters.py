from __future__ import annotations

import json

import pytest

from rots_harness import world
from rots_harness.characters import (
    HARNESS_EMAIL,
    HARNESS_PASSWORD,
    IMPLEMENTOR,
    LEVEL_IMPLEMENTOR,
    Character,
    CharacterSet,
    ProfessionPoints,
    custom_mage,
)


def test_a_standard_class_character_becomes_the_spec_rotstool_reads() -> None:
    character = Character(name="Testwar", race="dwarf", sex="male", level=12, load_room=1101, class_name="warrior")
    assert character.to_spec() == {"name": "Testwar", "race": "dwarf", "sex": "male", "class": "warrior", "level": 12, "load_room": 1101}


def test_a_custom_split_writes_points_instead_of_a_class() -> None:
    character = Character(name="Testcust", race="human", sex="female", level=5, load_room=1131, points=ProfessionPoints(mage=70, mystic=40, ranger=20, warrior=20))
    assert character.to_spec()["points"] == {"mage": 70, "mystic": 40, "ranger": 20, "warrior": 20}
    assert "class" not in character.to_spec()


def test_skills_and_set_values_are_written_only_when_given() -> None:
    character = Character(name="Testwiz", race="human", sex="male", level=30, load_room=1101, class_name="wizard", skills=(("magic_missile", 5),), hit=300)
    spec = character.to_spec()
    assert spec["skills"] == {"magic_missile": 5}
    assert spec["set"] == {"hit": 300}
    assert "skills" not in IMPLEMENTOR.to_spec() and "set" not in IMPLEMENTOR.to_spec()


@pytest.mark.parametrize("class_name, points", [(None, None), ("mage", ProfessionPoints(70, 40, 20, 20))])
def test_a_character_needs_exactly_one_of_class_and_points(class_name: str | None, points: ProfessionPoints | None) -> None:
    with pytest.raises(ValueError, match="exactly one of class_name and points"):
        Character(name="Testbad", race="human", sex="male", level=1, load_room=1101, class_name=class_name, points=points)


def test_the_implementor_is_a_level_100_warrior_in_immortal_start() -> None:
    assert IMPLEMENTOR.name == "Testimm"
    assert IMPLEMENTOR.level == LEVEL_IMPLEMENTOR == 100
    assert IMPLEMENTOR.load_room == world.ROOM_IMMORTAL_START
    assert IMPLEMENTOR.class_name == "warrior"


def test_custom_mage_defaults_and_overrides() -> None:
    default = custom_mage()
    assert (default.name, default.level, default.load_room) == ("Testmage", 30, world.ROOM_ARENA_CENTRE)
    assert default.points == ProfessionPoints(mage=70, mystic=40, ranger=20, warrior=20)
    assert custom_mage(name="Testmagetwo", level=12).level == 12


def test_a_character_set_writes_the_account_and_characters_in_order() -> None:
    character_set = CharacterSet((IMPLEMENTOR, custom_mage()))
    spec = json.loads(character_set.spec_json())
    assert spec["account"] == {"email": HARNESS_EMAIL, "password": HARNESS_PASSWORD}
    assert [entry["name"] for entry in spec["characters"]] == ["Testimm", "Testmage"]


def test_spec_json_is_stable_for_equal_sets() -> None:
    assert CharacterSet((IMPLEMENTOR,)).spec_json() == CharacterSet((IMPLEMENTOR,)).spec_json()
    assert CharacterSet((IMPLEMENTOR,)).spec_json() != CharacterSet((IMPLEMENTOR, custom_mage())).spec_json()


def test_character_looks_names_up_ignoring_case() -> None:
    character_set = CharacterSet((IMPLEMENTOR, custom_mage()))
    assert character_set.character("testimm") is IMPLEMENTOR
    assert character_set.character("TESTMAGE").name == "Testmage"


def test_an_unknown_name_is_refused() -> None:
    with pytest.raises(ValueError, match="Nobody is not in this test's characters"):
        CharacterSet((IMPLEMENTOR,)).character("Nobody")


def test_an_empty_character_set_is_refused() -> None:
    with pytest.raises(ValueError, match="at least one character"):
        CharacterSet(())
