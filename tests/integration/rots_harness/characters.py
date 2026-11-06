"""Test characters as the rotstool fixture spec states them, and the shared definitions scenarios use.

rotstool validates every field with the server's own rules (names, races, the creation-point
budget, skill keys, maximums); this module checks only what it needs to build the spec.
"""

from __future__ import annotations

import dataclasses
import json
from dataclasses import dataclass, field
from typing import Optional

from rots_harness import world

LEVEL_IMPLEMENTOR = 100
HARNESS_EMAIL = "harness@example.com"
HARNESS_PASSWORD = "Harness1x"


@dataclass(frozen=True)
class ProfessionPoints:
    """A custom split of creation points across the four professions."""

    mage: int
    mystic: int
    ranger: int
    warrior: int

    def to_spec(self) -> dict[str, int]:
        """The split as the fixture spec's points object."""
        return {"mage": self.mage, "mystic": self.mystic, "ranger": self.ranger, "warrior": self.warrior}


@dataclass(frozen=True)
class StoredValues:
    """Values a character is stored with, named and limited like the wizset fields that set them in a running game."""

    # Coins carried, 0 to 100000000.
    gold: Optional[int] = None
    # Spirit points.
    spirit: Optional[int] = None
    # Alignment, -1000 to 1000.
    alignment: Optional[int] = None
    # The specialization's name as the game spells it, such as "fire" or "wild fighting".
    specialization: Optional[str] = None
    # Hunger, thirst and drunkenness, 0 to 24, or -1 for off.
    hunger: Optional[int] = None
    thirst: Optional[int] = None
    drunk: Optional[int] = None
    # Base abilities, 0 to 40.
    str: Optional[int] = None
    int: Optional[int] = None
    wil: Optional[int] = None
    dex: Optional[int] = None
    con: Optional[int] = None
    lea: Optional[int] = None
    # Base stamina (maximum mana), 1 to 5000.
    maxstamina: Optional[int] = None

    def to_spec(self) -> dict:
        """The values the spec's set object carries, without the unset ones."""
        return {name: value for name, value in dataclasses.asdict(self).items() if value is not None}


@dataclass(frozen=True)
class Character:
    """One test character, with either a standard class name or a custom profession split."""

    name: str
    race: str
    sex: str
    level: int
    load_room: int
    class_name: Optional[str] = None
    points: Optional[ProfessionPoints] = None
    skills: tuple[tuple[str, int], ...] = field(default=())
    hit: Optional[int] = None
    mana: Optional[int] = None
    move: Optional[int] = None
    # Stored values the spec's set object carries beside hit, mana and move.
    stored: StoredValues = StoredValues()

    def __post_init__(self) -> None:
        if (self.class_name is None) == (self.points is None):
            raise ValueError(f"{self.name}: give exactly one of class_name and points")

    def to_spec(self) -> dict:
        """The character's entry in the fixture spec's characters array."""
        spec: dict = {"name": self.name, "race": self.race, "sex": self.sex}
        if self.class_name is not None:
            spec["class"] = self.class_name
        else:
            spec["points"] = self.points.to_spec()
        spec["level"] = self.level
        spec["load_room"] = self.load_room
        if self.skills:
            spec["skills"] = {skill_key: practices for skill_key, practices in self.skills}
        set_values = {name: value for name, value in (("hit", self.hit), ("mana", self.mana), ("move", self.move)) if value is not None}
        set_values.update(self.stored.to_spec())
        if set_values:
            spec["set"] = set_values
        return spec


IMPLEMENTOR = Character(name="Testimm", race="human", sex="male", level=LEVEL_IMPLEMENTOR, load_room=world.ROOM_IMMORTAL_START, class_name="warrior")


def custom_mage(
    name: str = "Testmage",
    level: int = 30,
    points: ProfessionPoints = ProfessionPoints(mage=70, mystic=40, ranger=20, warrior=20),
    load_room: int = world.ROOM_ARENA_CENTRE,
) -> Character:
    """A human female mage whose split matches no standard class."""
    return Character(name=name, race="human", sex="female", level=level, load_room=load_room, points=points)


@dataclass(frozen=True)
class CharacterSet:
    """The harness account and one test's characters, in the order its marker lists them."""

    characters: tuple[Character, ...]
    email: str = HARNESS_EMAIL
    password: str = HARNESS_PASSWORD

    def __post_init__(self) -> None:
        if not self.characters:
            raise ValueError("a character set needs at least one character")

    def to_spec(self) -> dict:
        """The fixture spec: the account and every character's entry, in set order."""
        return {"account": {"email": self.email, "password": self.password}, "characters": [character.to_spec() for character in self.characters]}

    def spec_json(self) -> str:
        """The fixture spec as rotstool reads it; equal sets give identical text, which keys the template cache."""
        return json.dumps(self.to_spec(), indent=2, sort_keys=True) + "\n"

    def character(self, name: str) -> Character:
        """The character with this name, ignoring case; raises ValueError when absent."""
        wanted = name.lower()
        for character in self.characters:
            if character.name.lower() == wanted:
                return character
        raise ValueError(f"{name} is not in this test's characters: {', '.join(character.name for character in self.characters)}")
