"""Names for the numbers in world files' headers, copied from the game's headers (src/structs.h,
src/script.h); unit/test_world_catalogue.py checks every name and value against those headers.

Each table pairs the header's constant name with its value. Flag tables hold bit masks; the others
hold plain numbers. The catalogue shows a name without the table's prefix.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Optional


@dataclass(frozen=True)
class NameTable:
    """Constants of one kind from a game header, and the prefix the catalogue drops from their names."""

    # The header the constants come from, relative to src/.
    header: str
    # The prefix every name carries in the header and the catalogue leaves out; empty for none.
    prefix: str
    # (header name, value) pairs; a flag table's values are single bits.
    entries: tuple[tuple[str, int], ...]

    def name_of(self, value: int) -> str:
        """The short name for a plain number, or the number itself when the table has none."""
        name = self.short_name(value)
        return str(value) if name is None else name

    def flag_names(self, flags: int) -> list[str]:
        """The short names of the bits set in flags, in bit order; an unnamed bit is "bit <n>"."""
        names = []
        bit_index = 0
        while flags >> bit_index:
            if flags & (1 << bit_index):
                name = self.short_name(1 << bit_index)
                names.append(f"bit {bit_index}" if name is None else name)
            bit_index += 1
        return names

    def short_name(self, value: int) -> Optional[str]:
        """The name of the entry holding value, without the prefix; None when no entry does."""
        for name, entry_value in self.entries:
            if entry_value == value:
                return name[len(self.prefix):]
        return None


def bits(*names: str) -> tuple[tuple[str, int], ...]:
    """Pairs each name with the bit of its position: the first is bit 0. An empty name skips a bit."""
    return tuple((name, 1 << index) for index, name in enumerate(names) if name)


def numbered(first: int, *names: str) -> tuple[tuple[str, int], ...]:
    """Pairs each name with consecutive numbers starting at first."""
    return tuple((name, first + index) for index, name in enumerate(names))


# Room flags, the second number of a room's header ('room_flags', structs.h).
ROOM_FLAGS = NameTable("structs.h", "", bits(
    "DARK", "DEATH", "NO_MOB", "INDOORS", "NORIDE", "PERMAFFECT", "SHADOWY", "NO_MAGIC", "TUNNEL",
    "PRIVATE", "GODROOM", "BFS_MARK", "DRINK_WATER", "DRINK_POISON", "SECURITYROOM", "PEACEROOM",
    "NO_TELEPORT", "HIDE_VNUM",
))
# Sector types, the third number of a room's header.
SECTORS = NameTable("structs.h", "SECT_", numbered(
    0, "SECT_INSIDE", "SECT_CITY", "SECT_FIELD", "SECT_FOREST", "SECT_HILLS", "SECT_MOUNTAIN",
    "SECT_WATER_SWIM", "SECT_WATER_NOSWIM", "SECT_UNDERWATER", "SECT_ROAD", "SECT_CRACK",
    "SECT_DENSE_FOREST", "SECT_SWAMP",
))
# Exit directions, the number after an exit's D.
DIRECTIONS = NameTable("structs.h", "", numbered(0, "NORTH", "EAST", "SOUTH", "WEST", "UP", "DOWN"))
# Exit flags, the first number of an exit's last line.
EXIT_FLAGS = NameTable("structs.h", "EX_", bits(
    "EX_ISDOOR", "EX_CLOSED", "EX_LOCKED", "EX_NOFLEE", "EX_RSLOCKED", "EX_PICKPROOF",
    "EX_DOORISHEAVY", "EX_NOBREAK", "EX_NO_LOOK", "EX_ISHIDDEN", "EX_ISBROKEN", "EX_NORIDE",
    "EX_NOBLINK", "EX_LEVER", "EX_NOWALK",
))
# Mob act flags, the number after a mob's strings (specials.act).
MOB_FLAGS = NameTable("structs.h", "MOB_", bits(
    "MOB_SPEC", "MOB_SENTINEL", "MOB_SCAVENGER", "MOB_ISNPC", "MOB_NOBASH", "MOB_AGGRESSIVE",
    "MOB_STAY_ZONE", "MOB_WIMPY", "MOB_STAY_TYPE", "MOB_MOUNT", "MOB_CAN_SWIM", "MOB_MEMORY",
    "MOB_HELPER", "MOB_AGGRESSIVE_EVIL", "MOB_AGGRESSIVE_NEUTRAL", "MOB_AGGRESSIVE_GOOD",
    "MOB_BODYGUARD", "MOB_SHADOW", "MOB_SWITCHING", "MOB_NORECALC", "MOB_FAST", "MOB_PET",
    "MOB_HUNTER", "MOB_ORC_FRIEND", "MOB_RACE_GUARD", "MOB_ASSISTANT", "MOB_GUARDIAN",
))
# Object types, the first number of an object's numeric lines ('type_flag').
ITEM_TYPES = NameTable("structs.h", "ITEM_", numbered(
    1, "ITEM_LIGHT", "ITEM_SCROLL", "ITEM_WAND", "ITEM_STAFF", "ITEM_WEAPON", "ITEM_FIREWEAPON",
    "ITEM_MISSILE", "ITEM_TREASURE", "ITEM_ARMOR", "ITEM_POTION", "ITEM_WORN", "ITEM_OTHER",
    "ITEM_TRASH", "ITEM_TRAP", "ITEM_CONTAINER", "ITEM_NOTE", "ITEM_DRINKCON", "ITEM_KEY",
    "ITEM_FOOD", "ITEM_MONEY", "ITEM_PEN", "ITEM_BOAT", "ITEM_FOUNTAIN", "ITEM_SHIELD", "ITEM_LEVER",
    "ITEM_KEYRING",
))
# Object extra flags, the second number ('extra_flags'); bit 13 has no name.
ITEM_EXTRA_FLAGS = NameTable("structs.h", "ITEM_", bits(
    "ITEM_GLOW", "ITEM_HUM", "ITEM_DARK", "ITEM_BREAKABLE", "ITEM_EVIL", "ITEM_INVISIBLE",
    "ITEM_MAGIC", "ITEM_NODROP", "ITEM_BROKEN", "ITEM_ANTI_GOOD", "ITEM_ANTI_EVIL",
    "ITEM_ANTI_NEUTRAL", "ITEM_NORENT", "", "ITEM_NOINVIS", "ITEM_WILLPOWER", "ITEM_IMM",
    "ITEM_HUMAN", "ITEM_DWARF", "ITEM_WOODELF", "ITEM_HOBBIT", "ITEM_BEORNING", "ITEM_URUK",
    "ITEM_ORC", "ITEM_MOBORC", "ITEM_MAGUS", "ITEM_OLOGHAI", "ITEM_HARADRIM", "ITEM_STAY_ZONE",
))
# Object wear flags, the third number ('wear_flags').
ITEM_WEAR_FLAGS = NameTable("structs.h", "ITEM_", bits(
    "ITEM_TAKE", "ITEM_WEAR_FINGER", "ITEM_WEAR_NECK", "ITEM_WEAR_BODY", "ITEM_WEAR_HEAD",
    "ITEM_WEAR_LEGS", "ITEM_WEAR_FEET", "ITEM_WEAR_HANDS", "ITEM_WEAR_ARMS", "ITEM_WEAR_SHIELD",
    "ITEM_WEAR_ABOUT", "ITEM_WEAR_WAISTE", "ITEM_WEAR_WRIST", "ITEM_WIELD", "ITEM_HOLD",
    "ITEM_THROW", "ITEM_WEAR_BACK", "ITEM_WEAR_BELT",
))
# Where an object's A line applies its modifier.
APPLY_LOCATIONS = NameTable("structs.h", "APPLY_", numbered(
    0, "APPLY_NONE", "APPLY_STR", "APPLY_DEX", "APPLY_INT", "APPLY_WILL", "APPLY_CON", "APPLY_LEA",
    "APPLY_PROF", "APPLY_LEVEL", "APPLY_AGE", "APPLY_CHAR_WEIGHT", "APPLY_CHAR_HEIGHT", "APPLY_MANA",
    "APPLY_HIT", "APPLY_MOVE", "APPLY_GOLD", "APPLY_EXP", "APPLY_DODGE", "APPLY_OB", "APPLY_DAMROLL",
    "APPLY_SAVING_SPELL", "APPLY_WILLPOWER", "APPLY_REGEN", "APPLY_VISION", "APPLY_SPEED",
    "APPLY_PERCEPTION", "APPLY_ARMOR", "APPLY_SPELL", "APPLY_BITVECTOR", "APPLY_MANA_REGEN",
    "APPLY_RESIST", "APPLY_VULN", "APPLY_MAUL", "APPLY_BEND", "APPLY_PK_MAGE", "APPLY_PK_MYSTIC",
    "APPLY_PK_RANGER", "APPLY_PK_WARRIOR", "APPLY_SPELL_PEN", "APPLY_SPELL_POW",
))
# The location whose modifier is the index of the AFF_ bit the object sets (affect_modify, handler.cpp).
APPLY_BITVECTOR = 28
# Character affect bits, named for an APPLY_BITVECTOR line; bit 28 has no name.
AFFECT_FLAGS = NameTable("structs.h", "AFF_", bits(
    "AFF_DETECT_HIDDEN", "AFF_INFRARED", "AFF_SNEAK", "AFF_HIDE", "AFF_DETECT_MAGIC", "AFF_CHARM",
    "AFF_CURSE", "AFF_SANCTUARY", "AFF_TWOHANDED", "AFF_INVISIBLE", "AFF_MOONVISION", "AFF_POISON",
    "AFF_SHIELD", "AFF_BREATHE", "AFF_UNUSED", "AFF_CONFUSE", "AFF_SLEEP", "AFF_BASH", "AFF_FLYING",
    "AFF_DETECT_INVISIBLE", "AFF_FEAR", "AFF_BLIND", "AFF_FOLLOW", "AFF_SWIM", "AFF_HUNT",
    "AFF_EVASION", "AFF_WAITING", "AFF_WAITWHEEL", "", "AFF_CONCENTRATION", "AFF_HAZE",
    "AFF_HALLUCINATE",
))
# Script triggers, the command types that start a script's handling of an event (script.h).
SCRIPT_TRIGGERS = NameTable("script.h", "", numbered(
    11, "ON_ENTER", "ON_BEFORE_ENTER", "ON_BEFORE_DIE", "ON_DIE", "ON_RECEIVE", "ON_EXAMINE_OBJECT",
    "ON_HEAR_SAY", "ON_DAMAGE", "ON_EAT", "ON_DRINK", "ON_WEAR", "ON_PULL", "ON_HEAR_YELL",
    "ON_PATH_END",
))
# Every table, for the test that checks them against the headers.
ALL_TABLES = (
    ROOM_FLAGS, SECTORS, DIRECTIONS, EXIT_FLAGS, MOB_FLAGS, ITEM_TYPES, ITEM_EXTRA_FLAGS,
    ITEM_WEAR_FLAGS, APPLY_LOCATIONS, AFFECT_FLAGS, SCRIPT_TRIGGERS,
)
