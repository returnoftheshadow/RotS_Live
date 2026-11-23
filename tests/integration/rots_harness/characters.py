"""Test characters as the rotstool fixture spec states them, and the definitions scenarios share.

rotstool validates every field with the server's own rules (names, races, the creation-point
budget, skill keys, maximums); this module checks only what it needs to build the spec.
"""

from __future__ import annotations

import dataclasses
import json
from dataclasses import dataclass, field
from typing import Optional

from rots_harness import world

# The implementor level, the highest the game has.
LEVEL_IMPLEMENTOR = 100
# The harness account's email address, which every character set uses unless it names its own.
HARNESS_EMAIL = "harness@example.com"
# The harness account's password, which every character set uses unless it names its own.
HARNESS_PASSWORD = "Harness1x"


@dataclass(frozen=True)
class ProfessionPoints:
    """A custom split of creation points across the four professions."""

    # Creation points spent on the mage profession.
    mage: int
    # Creation points spent on the mystic profession.
    mystic: int
    # Creation points spent on the ranger profession.
    ranger: int
    # Creation points spent on the warrior profession.
    warrior: int

    def to_spec(self) -> dict[str, int]:
        """Return the split as the fixture spec's points object."""
        return {"mage": self.mage, "mystic": self.mystic, "ranger": self.ranger, "warrior": self.warrior}


@dataclass(frozen=True)
class StoredValues:
    """Values a character is stored with.

    They are named and limited like the wizset fields that set them in a running game.
    """

    # Coins carried, 0 to 100000000.
    gold: Optional[int] = None
    # Spirit points.
    spirit: Optional[int] = None
    # Alignment, -1000 to 1000.
    alignment: Optional[int] = None
    # The specialization's name as the game spells it, such as "fire" or "wild fighting".
    specialization: Optional[str] = None
    # Hunger, 0 to 24, or -1 for off.
    hunger: Optional[int] = None
    # Thirst, 0 to 24, or -1 for off.
    thirst: Optional[int] = None
    # Drunkenness, 0 to 24, or -1 for off.
    drunk: Optional[int] = None
    # Base strength, 0 to 40.
    str: Optional[int] = None
    # Base intelligence, 0 to 40.
    int: Optional[int] = None
    # Base will, 0 to 40.
    wil: Optional[int] = None
    # Base dexterity, 0 to 40.
    dex: Optional[int] = None
    # Base constitution, 0 to 40.
    con: Optional[int] = None
    # Base learning, 0 to 40.
    lea: Optional[int] = None
    # Base stamina (maximum mana), 1 to 5000.
    maxstamina: Optional[int] = None

    def to_spec(self) -> dict:
        """Return the values the spec's set object carries, without the unset ones."""
        return {name: value for name, value in dataclasses.asdict(self).items() if value is not None}


@dataclass(frozen=True)
class Character:
    """One test character, with either a standard class name or a custom profession split."""

    # The character's name, which logs it in and names it in the game.
    name: str
    # The race as the fixture spec names it, such as "human".
    race: str
    # The sex as the fixture spec names it, "male" or "female".
    sex: str
    # The character's level.
    level: int
    # The vnum of the room the character enters the game in.
    load_room: int
    # A standard class name, such as "warrior"; None when points gives a custom split.
    class_name: Optional[str] = None
    # A custom profession split; None when class_name names a standard class.
    points: Optional[ProfessionPoints] = None
    # Each skill key with its practices, in the order the spec lists them.
    skills: tuple[tuple[str, int], ...] = field(default=())
    # Hit points the character is stored with; None keeps rotstool's value.
    hit: Optional[int] = None
    # Stamina (mana) the character is stored with; None keeps rotstool's value.
    mana: Optional[int] = None
    # Movement points the character is stored with; None keeps rotstool's value.
    move: Optional[int] = None
    # Stored values the spec's set object carries beside hit, mana and move.
    stored: StoredValues = StoredValues()

    def __post_init__(self) -> None:
        if (self.class_name is None) == (self.points is None):
            raise ValueError(f"{self.name}: give exactly one of class_name and points")

    def to_spec(self) -> dict:
        """Return the character's entry in the fixture spec's characters array."""
        spec: dict = {"name": self.name, "race": self.race, "sex": self.sex}
        if self.class_name is not None:
            spec["class"] = self.class_name
        else:
            spec["points"] = self.points.to_spec()
        spec["level"] = self.level
        spec["load_room"] = self.load_room
        if self.skills:
            spec["skills"] = {skill_key: practices for skill_key, practices in self.skills}
        set_values = {
            name: value
            for name, value in (("hit", self.hit), ("mana", self.mana), ("move", self.move))
            if value is not None
        }
        set_values.update(self.stored.to_spec())
        if set_values:
            spec["set"] = set_values
        return spec


def immortal(name: str, level: int) -> Character:
    """Return an immortal test character: a human male warrior who loads in ROOM_IMMORTAL_START.

    level should be an immortal's; it is not checked.
    """
    return Character(
        name=name, race="human", sex="male", level=level, load_room=world.ROOM_IMMORTAL_START, class_name="warrior"
    )


# The implementor most scenarios set up and inspect their tests with.
IMPLEMENTOR = immortal("Testimm", LEVEL_IMPLEMENTOR)


def custom_mage(
    name: str = "Testmage",
    level: int = 30,
    points: ProfessionPoints = ProfessionPoints(mage=70, mystic=40, ranger=20, warrior=20),
    load_room: int = world.ROOM_ARENA_CENTRE,
) -> Character:
    """Return a human female mage whose split matches no standard class."""
    return Character(name=name, race="human", sex="female", level=level, load_room=load_room, points=points)


@dataclass(frozen=True)
class CharacterSet:
    """The harness account and one test's characters, in the order its marker lists them."""

    # The test's characters, in the order its marker lists them.
    characters: tuple[Character, ...]
    # The account's email address.
    email: str = HARNESS_EMAIL
    # The account's password.
    password: str = HARNESS_PASSWORD

    def __post_init__(self) -> None:
        if not self.characters:
            raise ValueError("a character set needs at least one character")

    def to_spec(self) -> dict:
        """Return the fixture spec: the account and every character's entry, in set order."""
        return {
            "account": {"email": self.email, "password": self.password},
            "characters": [character.to_spec() for character in self.characters],
        }

    def spec_json(self) -> str:
        """Return the fixture spec as rotstool reads it.

        Equal sets give identical text, which keys the template cache.
        """
        return json.dumps(self.to_spec(), indent=2, sort_keys=True) + "\n"

    def character(self, name: str) -> Character:
        """Return the character with this name, ignoring case; raise ValueError when absent."""
        wanted = name.lower()
        for character in self.characters:
            if character.name.lower() == wanted:
                return character
        raise ValueError(
            f"{name} is not in this test's characters: {', '.join(character.name for character in self.characters)}"
        )
