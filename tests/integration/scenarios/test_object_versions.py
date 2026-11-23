"""Object versions and recorded enchants, and the test inn's refusals of items it cannot store.

Enchant weapon records its enchant on the item, and a version bump refreshes a player's saved copy
at the next login, keeping the enchant only while the new prototype can still take it, and keeping
the play state a save holds.
"""

from __future__ import annotations

import dataclasses
import re
from collections.abc import Sequence
from pathlib import Path

import pytest

from rots_harness import world
from rots_harness.casting import NO_ENERGY_REPLY, cast_until_it_lands
from rots_harness.characters import IMPLEMENTOR, Character, StoredValues
from rots_harness.game_log import wait_for_log_count
from rots_harness.game_text import (
    DROP_FORMAT,
    INNKEEPER_TELLS_START,
    NOT_HERE_REPLY,
    NO_ONE_AROUND_REPLY,
    PUT_FORMAT,
    RENTED_REPLY,
    STOP_USING_FORMAT,
    TARGET_NOT_CARRIED_REPLY,
    TELEPORTED_FORMAT,
    UNKNOWN_SPELL_REPLY,
    WEAR_HEAD_FORMAT,
)
from rots_harness.immortal_commands import GIVEN_FORMAT, give_new_object, load_object
from rots_harness.replies import SETTLE_SECONDS, reply_to, settled_expect
from rots_harness.session import CHARACTER_MENU_PROMPT, GameSession
from rots_harness.shaping import COULD_NOT_OPEN_FILE_REPLY, NO_BACKUP_FILE_START, OBJECT_RELEASED_REPLY
from rots_harness.stat_reading import OBJECT_STAT_LAST_LINE, STRENGTH_PATTERN, read_stat
from rots_harness.text_lines import first_number

pytestmark = [pytest.mark.scenario, pytest.mark.object_versions]

# A level-30 human mystic in the implementors' room who knows enchant weapon fully: a mortal
# caster, and the mortal who rents at the inn. Enchant weapon is a level-20 mystic spell (skills[]
# in consts.cpp).
MORTAL_MYSTIC = Character(
    name="Testmystic",
    race="human",
    sex="female",
    level=30,
    load_room=world.ROOM_IMMORTAL_START,
    class_name="mystic",
    skills=(("enchant_weapon", 100),),
)
# A level-91 mystic who owns the enchanted weapon across a quit. It is an immortal because only
# an immortal's quit saves what it carries (do_quit, act_othe.cpp): a mortal's possessions drop
# to the ground, and a mortal keeps them only by renting at the inn.
OWNER = Character(
    name="Testowner",
    race="human",
    sex="male",
    level=91,
    load_room=world.ROOM_IMMORTAL_START,
    class_name="mystic",
    skills=(("enchant_weapon", 100),),
)

# The keyword of the plain sword (world.OBJECT_PLAIN_SWORD): a weapon without the MAGIC flag or
# affects of its own, so enchant weapon takes on it (object_takes_enchant).
SWORD_KEYWORD = "sword"
# The keyword of the leather helm (world.OBJECT_LEATHER_HELM), one of the fixtures of obj/16.obj
# these scenarios use.
HELM_KEYWORD = "helm"
# Obj 1604 in obj/16.obj, a BREAKABLE key.
KEY_VNUM = 1604
# The keyword the key answers to.
KEY_KEYWORD = "key"
# The key's short description.
KEY_SHORT_DESCRIPTION = "a brass key"
# Obj 1605 in obj/16.obj, the chest the key locks (values `100 13 1604`: closed and locked, key
# 1604).
CHEST_VNUM = 1605
# The keyword the chest answers to.
CHEST_KEYWORD = "chest"
# The chest's short description.
CHEST_SHORT_DESCRIPTION = "a small chest"
# Obj 1606 in obj/16.obj, a trinket whose rent per day is -2.
TRINKET_VNUM = 1606
# The keyword the trinket answers to.
TRINKET_KEYWORD = "trinket"
# The trinket's short description.
TRINKET_SHORT_DESCRIPTION = "a cursed trinket"
# Obj 1607 in obj/16.obj, a token with the NORENT flag.
TOKEN_VNUM = 1607
# The keyword the token answers to.
TOKEN_KEYWORD = "token"
# The token's short description.
TOKEN_SHORT_DESCRIPTION = "a pewter token"
# Obj 1608 in obj/16.obj, a waterskin holding 10 of its 20 drinks of water, weighing 30.
WATERSKIN_VNUM = 1608
# The keyword the waterskin answers to.
WATERSKIN_KEYWORD = "waterskin"
# The waterskin's short description.
WATERSKIN_SHORT_DESCRIPTION = "a waterskin"
# The keyword of the leather bag (world.OBJECT_LEATHER_BAG), a container that can hold the token.
BAG_KEYWORD = "bag"

# Spirit each caster is stored with: enchant weapon costs 50 and a lost concentration 25
# (do_cast, spell_pa.cpp), and nothing restores spirit between requests under affect-hours.
CASTER_SPIRIT = 500
# A good caster's alignment, the top of wizset's range (IS_GOOD at 100 or more, utils.h).
GOOD_ALIGNMENT = 1000
# An evil caster's alignment, the bottom of wizset's range (IS_EVIL at -100 or less, utils.h).
EVIL_ALIGNMENT = -1000
# A neutral caster's alignment, the middle of wizset's range.
NEUTRAL_ALIGNMENT = 0
# The affect the builder gives a prototype in the object editor's field 19: location 1 (STR),
# modifier 1. On the sword it means the sword can no longer take an enchant.
STRENGTH_AFFECT_ANSWER = "(1 1)"
# The strength constant the helm's wearer is stored with, so its base strength is known.
WEARER_STRENGTH = 15
# Thirst the owner is stored with before it sips: an immortal's conditions otherwise read 255
# (advance_level, profs.cpp), and do_drink (act_obj2.cpp) refuses a drinker more than 23 thirsty.
SIPPER_THIRST = 10
# Drunkenness the owner is stored with before it sips; do_drink refuses a drinker more than 10
# drunk.
SIPPER_DRUNKENNESS = 0
# The renting mortal's gold: rent costs nothing, and the rent's log line shows the gold.
RENTER_GOLD = 0


def caster(character: Character, alignment: int, gold: int | None = None) -> Character:
    """Return character stored with alignment, spirit for the casts and their repeats, and gold."""
    stored = StoredValues(alignment=alignment, spirit=CASTER_SPIRIT, gold=gold)
    return dataclasses.replace(character, stored=stored)


# The mortal mystic as a good caster.
GOOD_MYSTIC = caster(MORTAL_MYSTIC, GOOD_ALIGNMENT)
# The mortal mystic as an evil caster.
EVIL_MYSTIC = caster(MORTAL_MYSTIC, EVIL_ALIGNMENT)
# The mortal mystic as a neutral caster.
NEUTRAL_MYSTIC = caster(MORTAL_MYSTIC, NEUTRAL_ALIGNMENT)
# The good mortal caster who rents at the inn.
RENTING_MYSTIC = caster(MORTAL_MYSTIC, GOOD_ALIGNMENT, RENTER_GOLD)
# The owner as a good caster.
GOOD_OWNER = caster(OWNER, GOOD_ALIGNMENT)
# The owner who wears the helm, with a known strength constant.
STRONG_OWNER = dataclasses.replace(OWNER, stored=StoredValues(str=WEARER_STRENGTH))
# The owner who sips from the waterskin.
SIPPING_OWNER = dataclasses.replace(OWNER, stored=StoredValues(drunk=SIPPER_DRUNKENNESS, thirst=SIPPER_THIRST))

# What the caster reads when the spell goes off (do_cast).
CAST_DONE_REPLY = "Ok."
# What enchant weapon prints for a weapon that has an affect but not the MAGIC flag
# (spell_enchant_weapon, mystic.cpp).
TOO_MUCH_MAGIC_REPLY = "There is too much magic in it already."
# What the caster reads when the enchant takes, by the caster's alignment (spell_enchant_weapon);
# format with the glow's colour.
GLOW_FORMAT = "A plain sword glows {}."
# GLOW_FORMAT's fixed middle, for a check that does not name the colour.
GLOW_PART = " glows "
# What the owner reads after the room on entering the game, once per refreshed item
# (Crash_tell_object_refresh, objsave.cpp); format with the item's short description.
UPDATED_FORMAT = "Your item, {}, has been updated."
# UPDATED_FORMAT's fixed ending, for a check that does not name the item.
UPDATED_END = "has been updated."

# What stat obj answers when no object answers to the keyword (do_stat_object, act_wiz.cpp).
NO_SUCH_OBJECT_REPLY = "No such object around."
# stat obj's version line for a copy whose prototype has no version.
UNVERSIONED_LINE = "Version: prototype none, this copy none"
# stat obj's version line for a copy made before its prototype's bump, not yet refreshed.
BUMPED_BEFORE_LOGIN_LINE = "Version: prototype 1 (on), this copy none"
# stat obj's version line for a copy at its prototype's version.
REFRESHED_LINE = "Version: prototype 1 (on), this copy 1"
# stat obj's version line for an unrefreshed copy whose prototype's version is off.
TURNED_OFF_UNREFRESHED_LINE = "Version: prototype 1 (off), this copy none"
# stat obj's version line for a copy made while its prototype's version is off.
MADE_WHILE_OFF_LINE = "Version: prototype 1 (off), this copy 1 (made while off)"
# stat obj's enchants line for a copy with no recorded enchant.
NO_ENCHANTS_LINE = "Enchants: none"
# stat obj's enchants line for an enchanted copy; format with the caster's alignment name.
ENCHANT_LINE_FORMAT = "Enchants: ob +6 ({})"
# stat obj's affections line for an enchanted copy.
ENCHANT_AFFECTIONS_LINE = "Affections: +6 to OB"
# stat obj's affections line for a copy of a prototype given STR +1.
STRENGTH_AFFECTIONS_LINE = "Affections: +1 to STR"
# The extra flags line of a good caster's enchanted copy (sprintbit, utility.cpp, puts a space
# before each flag and a full stop after the last). Enchant weapon sets MAGIC, and !EVIL or !GOOD
# for a good or evil caster (apply_object_enchant, utility.cpp).
GOOD_ENCHANT_FLAGS_LINE = "Extra flags   :  MAGIC !EVIL."
# The extra flags line of an evil caster's enchanted copy.
EVIL_ENCHANT_FLAGS_LINE = "Extra flags   :  MAGIC !GOOD."
# The extra flags line of a neutral caster's enchanted copy.
NEUTRAL_ENCHANT_FLAGS_LINE = "Extra flags   :  MAGIC."
# The extra flags line of a copy with no flags at all.
NO_FLAGS_LINE = "Extra flags   : <NONE>"
# The extra flags line of the key once it has broken.
BROKEN_KEY_FLAGS_LINE = "Extra flags   :  BREAKABLE BROKEN."
# The drink container line for the waterskin after one sip.
SIPPED_WATERSKIN_LINE = "Max-contains: 20, Contains: 9, Poisoned: No, Liquid: water"
# Where a copy carried by a character is (do_stat_object); format with the carrier's and the
# loader's names.
CARRIED_LOCATION_LINE_FORMAT = "In room: Nowhere, In object: None, Carried by: {}, Loaded by: {}"
# Where a copy inside a container is; format with the container's short description and the
# loader's name.
CONTAINED_LOCATION_LINE_FORMAT = "In room: Nowhere, In object: {}, Carried by: Nobody, Loaded by: {}"
# The loader's name as stat obj shows it: from player_table, which holds names in lower case.
LOADER_NAME = IMPLEMENTOR.name.lower()

# The object editor's prompt, "Object: <vnum>>" (consts.cpp); format with the vnum.
EDITOR_PROMPT_FORMAT = "Object: {}>"
# What the object editor prints once it has loaded the prototype (shapeobj.cpp).
EDITOR_LOADED_REPLY = "Causing the eternal order to snicker, you load an object."
# What the object editor prints when no prototype has the number.
EDITOR_NO_OBJECT_REPLY = "no object here."
# Field 22's prompt for an unversioned prototype.
VERSION_PROMPT = "Enter VERSION: b, blank keeps it [none]:"
# Field 22's prompt for a prototype at version 1 with refreshes on.
VERSION_ON_PROMPT = "Enter VERSION: b or o, blank keeps it [1 (on)]:"
# What field 22 answers to `b`.
VERSION_BUMPED_REPLY = "Version is now 1 (on)."
# What field 22 answers to `o`.
VERSION_OFF_REPLY = "Version is now 1 (off)."
# What field 22 answers when the version stays as it was.
VERSION_UNCHANGED_REPLY = "Version unchanged."
# Field 19's prompt for a prototype without affects.
AFFECTS_PROMPT = "Current: (0 0) (0 0)"
# What field 19 answers when the answer sets no affection.
AFFECTS_DROPPED_REPLY = "No affections were set. Dropped."
# What /implement prints once the prototype is live.
IMPLEMENTED_REPLY = "Object implemented"
# What /implement prints when nothing is open to implement.
NOTHING_TO_IMPLEMENT_REPLY = "You have nothing to implement"
# What /implement and /done print to a shaper without permission.
NOT_AUTHORIZED_REPLY = "You're not authorized to do this."
# What /done answers when the world file cannot be written (extra_coms_obj, replace_object).
NOT_SAVED_REPLY = "Not saved - still shaping."
# The world file /done rewrites, under the test's lib: SHAPE_OBJ_DIR (protos.h) for zone 16.
ZONE_OBJECT_FILE = Path("world") / "obj" / "16.obj"
# The sword's record in obj/16.obj after a bump written by /done, without line ends: write_object
# (shapeobj.cpp) puts the version fifth on the level line, after level, rarity, material and
# script.
DONE_SWORD_RECORD = [
    "#1601",
    "sword plain~",
    "a plain sword~",
    "A plain sword lies here.~",
    "~",
    "5 0 8193",
    "5 2 2 3 0",
    "130 2000 -1",
    "1 0 4 0 1",
]

# What the mortal reads when the implementor teleports it.
TELEPORTED_BY_IMPLEMENTOR = TELEPORTED_FORMAT.format(IMPLEMENTOR.name)
# The inn's name (tests/integration/world/wld/16.wld), which a character in the inn reads.
ROOM_INN_NAME = "Harness Inn"
# The innkeeper's long description (tests/integration/world/mob/11.mob), which a character in the
# inn reads in the room.
INNKEEPER_IN_ROOM_LINE = "The harness innkeeper stands behind the counter, ready to rent you a room."
# What the innkeeper's receptionist special answers a renter with nothing to store
# (Crash_offer_rent, objsave.cpp). The rent itself is game_text.RENTED_REPLY.
NOTHING_TO_STORE_REPLY = "The harness innkeeper tells you, 'But you are not carrying anything!  Just quit!'"
# What the receptionist special answers a renter it cannot see (gen_receptionist, objsave.cpp).
CANNOT_SEE_RENTER_REPLY = "The harness innkeeper says, 'I don't deal with people I can't see!'"
# What the receptionist special answers for an item it cannot store (Crash_report_unrentables,
# objsave.cpp); format with the item's short description.
CANNOT_STORE_FORMAT = "The harness innkeeper tells you, 'You cannot store {}.'"
# The offer's quote for the mystic carrying the sword (Crash_offer_rent, objsave.cpp).
OFFER_QUOTE_REPLY = "The harness innkeeper tells you, 'It will cost you 6 silver and 88 copper for the first day.'"
# The middle of any offer's quote, for a check that names no price.
OFFER_QUOTE_PART = "It will cost you"
# What rent answers with no receptionist in the room (do_rent, objsave.cpp); offer answers
# NOT_HERE_REPLY.
FIELD_RENT_DISABLED_REPLY = "Field-rent is disabled. You have to go to an inn now."

# What unlock answers the actor for the chest (do_unlock, act_move.cpp).
UNLOCKED_REPLY = "*Click*"
# What the actor reads as the key breaks (check_break_key, act_move.cpp).
KEY_BREAKS_REPLY = "Unfortunately, a brass key breaks as you use it!"
# What the room reads as the owner's key breaks (check_break_key).
KEY_BREAKS_OBSERVED = f"Unfortunately, a brass key breaks as {OWNER.name} uses it!"
# What the room reads as the owner unlocks the chest (do_unlock).
UNLOCK_OBSERVED = f"{OWNER.name} unlocks a small chest."
# What lock and unlock answer without the chest's key (do_lock, do_unlock).
NO_PROPER_KEY_REPLY = "You don't seem to have the proper key."
# What unlock answers for a chest already unlocked (do_unlock).
NOT_LOCKED_REPLY = "Oh.. it wasn't locked, after all."
# What lock and unlock answer for a chest that is open.
NOT_CLOSED_REPLY = "Silly - it ain't even closed!"
# What lock and unlock answer for an object that is not a container.
NOT_A_CONTAINER_REPLY = "That's not a container."
# What lock answers for a chest already locked (do_lock).
ALREADY_LOCKED_REPLY = "It is locked already."
# What lock answers the actor once the chest is locked (do_lock).
LOCKED_REPLY = "*Cluck*"
# What wear answers for the helm (wear_message, act_obj2.cpp).
WORN_REPLY = WEAR_HEAD_FORMAT.format(world.OBJECT_LEATHER_HELM_SHORT_DESCRIPTION)
# What remove answers for the helm (take_off_item, act_obj2.cpp).
REMOVED_REPLY = STOP_USING_FORMAT.format(world.OBJECT_LEATHER_HELM_SHORT_DESCRIPTION)
# What sip answers with the taste of water (do_drink, act_obj2.cpp).
SIP_REPLY = "It tastes like water."
# What sip answers a drinker too drunk to drink (do_drink).
TOO_DRUNK_REPLY = "You can't seem to get close enough to your mouth."
# What sip answers a drinker too full to drink (do_drink).
TOO_FULL_REPLY = "Your stomach can't contain anymore!"

# The server log line of a refresh (Crash_refresh_object, objsave.cpp): format with the object's
# vnum and the owner's idnum; an enchanted copy's line goes on with ENCHANT_NOTE_FORMAT.
REFRESH_LOG_FORMAT = "Object version refresh: obj {}, 0 -> 1, player idnum {}"
# The note a refresh of an enchanted copy adds; format with "kept" or "dropped".
ENCHANT_NOTE_FORMAT = ", enchant {}"
# The part of every refresh log line, for counting them.
REFRESH_LOG_PART = "Object version refresh"
# The server log line of a quit (do_quit, act_othe.cpp); format with the character's name.
QUIT_LOG_FORMAT = "{} has quit the game."
# The server log line of a rent (gen_receptionist), with the cost and the gold; format with the
# character's name.
RENT_LOG_FORMAT = "{} has rented (0/day, 0 tot.)"
# The server log line of the return from rent (Crash_load); format with the character's name.
UNRENT_LOG_FORMAT = "{} un-renting and entering game."
# The stat player line with the character's idnum (do_stat_character, act_wiz.cpp).
IDNUM_PATTERN = re.compile(r"IDNum: \[\s*(\d+)\]")

# How much of a login's text a failed assertion quotes.
QUOTED_TEXT_LENGTH = 600
# Seconds to wait for a line to reach game.log: the Docker launcher relays the server's stderr.
LOG_TIMEOUT_SECONDS = 15.0
# Seconds between two reads of game.log while waiting for a line.
LOG_POLL_SECONDS = 0.25


def give_object(implementor: GameSession, keyword: str, short_description: str, receiver: Character) -> None:
    """Hand an object the implementor carries to receiver, who stands in the implementor's room."""
    given = implementor.command(f"give {keyword} {receiver.name.lower()}")
    assert given.contains(GIVEN_FORMAT.format(short_description, receiver.name)), given.text


def hand_new_object(
    implementor: GameSession, vnum: int, keyword: str, short_description: str, receiver: Character
) -> None:
    """Load an object and hand it to receiver in the implementor's room, checking both replies."""
    give_new_object(
        implementor,
        vnum,
        keyword,
        receiver.name.lower(),
        shown_recipient=receiver.name,
        short_description=short_description,
    )


def give_plain_sword(implementor: GameSession, receiver: Character) -> None:
    """Load a plain sword and hand it to receiver, who stands in the implementor's room."""
    hand_new_object(
        implementor, world.OBJECT_PLAIN_SWORD, SWORD_KEYWORD, world.OBJECT_PLAIN_SWORD_SHORT_DESCRIPTION, receiver
    )


def cast_enchant_weapon(caster: GameSession) -> str:
    """Cast enchant weapon on the sword and return what the caster read once the spell went off.

    The cast is repeated past a lost concentration (cast_until_it_lands).
    """
    outcomes = [CAST_DONE_REPLY, NO_ENERGY_REPLY, TARGET_NOT_CARRIED_REPLY, UNKNOWN_SPELL_REPLY]
    return cast_until_it_lands(caster, f"cast 'enchant weapon' {SWORD_KEYWORD}", outcomes)


def stat_object(implementor: GameSession, keyword: str) -> list[str]:
    """Return the stripped lines of stat obj on the first object answering to keyword.

    stat obj looks in the implementor's inventory, then its room, then the whole world
    (get_obj_vis, handler.cpp), so each caller asserts the location line of the copy it meant.
    """
    text = read_stat(
        implementor, f"obj {keyword}", last_line=OBJECT_STAT_LAST_LINE, other_outcomes=[NO_SUCH_OBJECT_REPLY]
    )
    assert NO_SUCH_OBJECT_REPLY not in text, text
    text_lines = text.splitlines()
    return [line.strip() for line in text_lines]


def stat_carried_object(implementor: GameSession, keyword: str, holder: Character) -> list[str]:
    """Run stat obj on the copy holder carries, which the implementor loaded; return the lines."""
    stat_lines = stat_object(implementor, keyword)
    assert CARRIED_LOCATION_LINE_FORMAT.format(holder.name, LOADER_NAME) in stat_lines, stat_lines
    return stat_lines


def stat_sword(implementor: GameSession, holder: Character) -> list[str]:
    """Run stat obj on the sword holder carries and return the lines."""
    return stat_carried_object(implementor, SWORD_KEYWORD, holder)


def player_idnum(implementor: GameSession, name: str) -> int:
    """Return the character's idnum, from stat player."""
    text = read_stat(implementor, f"player {name.lower()}")
    return first_number(IDNUM_PATTERN, text)


def strength_base_and_constant(implementor: GameSession, name: str) -> tuple[int, int]:
    """Return the character's base and constant strength, from stat player."""
    text = read_stat(implementor, f"player {name.lower()}")
    match = STRENGTH_PATTERN.search(text)
    assert match is not None, f"no Str in stat player {name}:\n{text}"
    base = int(match.group(2))
    constant = int(match.group(3))
    return base, constant


def editor_step(implementor: GameSession, line: str, expected_reply: str, other_outcomes: Sequence[str]) -> str:
    """Send one line to the object editor and return what it read, asserting the expected reply."""
    reply = reply_to(implementor, line, [expected_reply, *other_outcomes])
    assert expected_reply in reply, f"{line!r}:\n{reply}"
    return reply


def open_object_editor(implementor: GameSession, vnum: int) -> None:
    """Start shaping the prototype from its zone's world file."""
    other_outcomes = [COULD_NOT_OPEN_FILE_REPLY, EDITOR_NO_OBJECT_REPLY]
    editor_step(implementor, f"shape object {vnum}", EDITOR_LOADED_REPLY, other_outcomes)


def bump_version(implementor: GameSession) -> None:
    """Bump an unversioned prototype's version to 1 in the open editor (field 22, `b`)."""
    editor_step(implementor, "/22", VERSION_PROMPT, [VERSION_ON_PROMPT])
    editor_step(implementor, "b", VERSION_BUMPED_REPLY, [VERSION_UNCHANGED_REPLY])


def shape_prototype(implementor: GameSession, vnum: int, *, give_strength_affect: bool = False) -> None:
    """Bump a prototype's version in the object editor and implement it, always ending with /free.

    With give_strength_affect, the prototype gets a strength affect first.
    """
    open_object_editor(implementor, vnum)
    try:
        if give_strength_affect:
            editor_step(implementor, "/19", AFFECTS_PROMPT, [])
            editor_prompt = EDITOR_PROMPT_FORMAT.format(vnum)
            editor_step(implementor, STRENGTH_AFFECT_ANSWER, editor_prompt, [AFFECTS_DROPPED_REPLY])

        bump_version(implementor)
        editor_step(implementor, "/implement", IMPLEMENTED_REPLY, [NOTHING_TO_IMPLEMENT_REPLY, NOT_AUTHORIZED_REPLY])
    finally:
        editor_step(implementor, "/free", OBJECT_RELEASED_REPLY, [])


def send_to_inn(implementor: GameSession, mortal: GameSession, character: Character) -> None:
    """Teleport the mortal into the inn and assert it arrived where the innkeeper stands.

    The mortal types nothing in the inn before rent or offer: the receptionist special draws for
    every other command typed there, and on a 0 the innkeeper performs a social (gen_receptionist).
    """
    teleport = implementor.command(f"teleport {character.name.lower()} {world.ROOM_INN}")
    assert not teleport.contains(NO_ONE_AROUND_REPLY), teleport.text
    arrival = settled_expect(mortal, [TELEPORTED_BY_IMPLEMENTOR])
    assert ROOM_INN_NAME in arrival, arrival
    assert INNKEEPER_IN_ROOM_LINE in arrival, arrival


def rent_and_log_in_again(server, mortal: GameSession, character: Character) -> GameSession:
    """Rent at the innkeeper, then log the character in again on a new connection and return it.

    Asserts the innkeeper stores the mortal's belongings and returns the connection to the
    character menu, and closes that connection.
    """
    outcomes = [CHARACTER_MENU_PROMPT, INNKEEPER_TELLS_START, CANNOT_SEE_RENTER_REPLY, FIELD_RENT_DISABLED_REPLY]
    reply = reply_to(mortal, "rent", outcomes)
    assert RENTED_REPLY in reply, reply
    assert CHARACTER_MENU_PROMPT in reply, reply
    mortal.close()
    return server.login(character.name)


def ask_innkeeper(mortal: GameSession, command: str) -> str:
    """Send rent or offer in the inn and return everything the mortal read in answer."""
    outcomes = [
        CHARACTER_MENU_PROMPT,
        INNKEEPER_TELLS_START,
        CANNOT_SEE_RENTER_REPLY,
        NOT_HERE_REPLY,
        FIELD_RENT_DISABLED_REPLY,
    ]
    return reply_to(mortal, command, outcomes)


def describe_refresh_lines(log_text: str) -> str:
    """Return the refresh lines log_text holds, for a failed wait's message."""
    log_lines = log_text.splitlines()
    refreshes = [log_line for log_line in log_lines if REFRESH_LOG_PART in log_line]
    return f"refresh lines: {refreshes!r}"


@pytest.mark.object_versions_enchant
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR)
def test_stat_shows_no_version_and_no_enchants_on_a_plain_copy(server) -> None:
    """Pins the stat lines on an object nobody has versioned or enchanted.

    stat obj shows the prototype's version and the copy's, both `none` while the prototype's
    version is 0, and the recorded enchants, `none` for a plain copy.
    """
    implementor = server.login(IMPLEMENTOR.name)
    load_object(implementor, world.OBJECT_PLAIN_SWORD, world.OBJECT_PLAIN_SWORD_SHORT_DESCRIPTION)

    stat_lines = stat_sword(implementor, IMPLEMENTOR)
    assert UNVERSIONED_LINE in stat_lines, stat_lines
    assert NO_ENCHANTS_LINE in stat_lines, stat_lines


@pytest.mark.object_versions_enchant
@pytest.mark.schedule("affect-hours")
@pytest.mark.parametrize(
    ("glow_colour", "alignment_name", "flags_line"),
    [
        pytest.param(
            "blue", "good", GOOD_ENCHANT_FLAGS_LINE, marks=pytest.mark.characters(IMPLEMENTOR, GOOD_MYSTIC), id="good"
        ),
        pytest.param(
            "red", "evil", EVIL_ENCHANT_FLAGS_LINE, marks=pytest.mark.characters(IMPLEMENTOR, EVIL_MYSTIC), id="evil"
        ),
        pytest.param(
            "yellow",
            "neutral",
            NEUTRAL_ENCHANT_FLAGS_LINE,
            marks=pytest.mark.characters(IMPLEMENTOR, NEUTRAL_MYSTIC),
            id="neutral",
        ),
    ],
)
def test_enchant_weapon_records_the_enchant_with_the_casters_alignment(
    server, glow_colour: str, alignment_name: str, flags_line: str
) -> None:
    """Pins that a successful enchant weapon is recorded on the item with the caster's alignment.

    The caster reads the glow for its alignment, and stat obj lists the recorded enchant, `ob +6`
    and the alignment, beside the +6 OB affect and the MAGIC flag the spell puts on the sword, with
    !EVIL from a good caster and !GOOD from an evil one. The one draw that can stop the cast is the
    concentration roll in do_cast, which cast_enchant_weapon repeats; the affect-hours schedule
    keeps everything else from drawing.
    """
    implementor = server.login(IMPLEMENTOR.name)
    mystic = server.login(MORTAL_MYSTIC.name)
    give_plain_sword(implementor, MORTAL_MYSTIC)

    reply = cast_enchant_weapon(mystic)
    assert GLOW_FORMAT.format(glow_colour) in reply, reply

    stat_lines = stat_sword(implementor, MORTAL_MYSTIC)
    assert ENCHANT_LINE_FORMAT.format(alignment_name) in stat_lines, stat_lines
    assert ENCHANT_AFFECTIONS_LINE in stat_lines, stat_lines
    assert flags_line in stat_lines, stat_lines


@pytest.mark.object_versions_enchant
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, GOOD_MYSTIC)
def test_a_second_enchant_weapon_on_the_same_sword_does_nothing(server) -> None:
    """Pins today's refusal of a second enchant weapon on an enchanted sword: nothing happens.

    The first cast sets the MAGIC flag, and spell_enchant_weapon (mystic.cpp) acts only on a weapon
    without it, so the second cast goes off (`Ok.`) and then prints nothing: no glow, and not `There
    is too much magic in it already.`, which is only for a weapon that has an affect without the
    MAGIC flag. stat obj still lists the one enchant the first cast recorded, its one +6 OB and the
    first cast's flags. The one draw that can stop a cast is the concentration roll in do_cast,
    which cast_enchant_weapon repeats; the affect-hours schedule keeps everything else from drawing.
    """
    implementor = server.login(IMPLEMENTOR.name)
    mystic = server.login(MORTAL_MYSTIC.name)
    give_plain_sword(implementor, MORTAL_MYSTIC)

    first_reply = cast_enchant_weapon(mystic)
    assert GLOW_FORMAT.format("blue") in first_reply, first_reply

    second_reply = cast_enchant_weapon(mystic)
    assert CAST_DONE_REPLY in second_reply, second_reply
    assert GLOW_PART not in second_reply, second_reply
    assert TOO_MUCH_MAGIC_REPLY not in second_reply, second_reply

    stat_lines = stat_sword(implementor, MORTAL_MYSTIC)
    assert ENCHANT_LINE_FORMAT.format("good") in stat_lines, stat_lines
    assert ENCHANT_AFFECTIONS_LINE in stat_lines, stat_lines
    assert GOOD_ENCHANT_FLAGS_LINE in stat_lines, stat_lines


@pytest.mark.object_versions_refresh
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR)
def test_a_bump_saved_with_done_writes_the_version_to_the_world_file(server) -> None:
    """Pins /done of a bump: the version is saved fifth on the level line, and implemented.

    A copy made afterwards is stamped with it. The object editor backs the file up to
    world/obj/oldobjs before rewriting it (replace_object, shapeobj.cpp); the test world provides
    that folder.
    """
    implementor = server.login(IMPLEMENTOR.name)
    open_object_editor(implementor, world.OBJECT_PLAIN_SWORD)
    bump_version(implementor)
    try:
        done_outcomes = [NOT_SAVED_REPLY, NO_BACKUP_FILE_START, NOT_AUTHORIZED_REPLY]
        editor_step(implementor, "/done", OBJECT_RELEASED_REPLY, done_outcomes)
    except AssertionError:
        editor_step(implementor, "/free", OBJECT_RELEASED_REPLY, [])
        raise

    # write_object ends each line with "\n\r".
    world_file_raw = (server.lib_dir / ZONE_OBJECT_FILE).read_text(encoding="latin-1")
    world_file_text = world_file_raw.replace("\r", "")
    world_file_lines = world_file_text.split("\n")
    file_lines = [line.strip() for line in world_file_lines if line.strip()]
    assert DONE_SWORD_RECORD[0] in file_lines, world_file_text
    record_start = file_lines.index(DONE_SWORD_RECORD[0])
    assert file_lines[record_start:record_start + len(DONE_SWORD_RECORD)] == DONE_SWORD_RECORD, world_file_text

    load_object(implementor, world.OBJECT_PLAIN_SWORD, world.OBJECT_PLAIN_SWORD_SHORT_DESCRIPTION)
    stat_lines = stat_sword(implementor, IMPLEMENTOR)
    assert REFRESHED_LINE in stat_lines, stat_lines


@pytest.mark.object_versions_refresh
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, GOOD_OWNER)
def test_a_version_bump_refreshes_the_owners_copy_at_the_next_login_and_keeps_its_enchant(server) -> None:
    """Pins that a bump refreshes the owner's saved copy at its next login, keeping its enchant.

    The bumped prototype can still take the enchant. The owner enchants its sword; the implementor
    bumps the prototype's version in the object editor (field 22, `b`) and implements it. The copy
    online is not refreshed. The owner quits and logs in again: it reads that the sword was updated,
    stat obj shows the copy stamped with the prototype's version, the enchant, its +6 OB and its
    flags still on it, and the server log records the refresh with `enchant kept`. The one draw that
    can stop the cast is the concentration roll in do_cast, which cast_enchant_weapon repeats; the
    affect-hours schedule keeps everything else from drawing.
    """
    implementor = server.login(IMPLEMENTOR.name)
    owner = server.login(OWNER.name)
    give_plain_sword(implementor, OWNER)
    owner_idnum = player_idnum(implementor, OWNER.name)

    reply = cast_enchant_weapon(owner)
    assert GLOW_FORMAT.format("blue") in reply, reply
    enchanted_lines = stat_sword(implementor, OWNER)
    assert UNVERSIONED_LINE in enchanted_lines, enchanted_lines
    assert ENCHANT_LINE_FORMAT.format("good") in enchanted_lines, enchanted_lines

    shape_prototype(implementor, world.OBJECT_PLAIN_SWORD)
    online_lines = stat_sword(implementor, OWNER)
    assert BUMPED_BEFORE_LOGIN_LINE in online_lines, online_lines

    owner = server.relog(owner)
    updated = UPDATED_FORMAT.format(world.OBJECT_PLAIN_SWORD_SHORT_DESCRIPTION)
    assert updated in owner.everything, owner.everything[-QUOTED_TEXT_LENGTH:]

    refreshed_lines = stat_sword(implementor, OWNER)
    assert REFRESHED_LINE in refreshed_lines, refreshed_lines
    assert ENCHANT_LINE_FORMAT.format("good") in refreshed_lines, refreshed_lines
    assert ENCHANT_AFFECTIONS_LINE in refreshed_lines, refreshed_lines
    assert GOOD_ENCHANT_FLAGS_LINE in refreshed_lines, refreshed_lines
    refresh_line = REFRESH_LOG_FORMAT.format(world.OBJECT_PLAIN_SWORD, owner_idnum)
    refresh_line += ENCHANT_NOTE_FORMAT.format("kept")
    wait_for_log_count(
        server.handle.log_path,
        refresh_line,
        1,
        timeout=LOG_TIMEOUT_SECONDS,
        poll=LOG_POLL_SECONDS,
        describe=describe_refresh_lines,
    )


@pytest.mark.object_versions_refresh
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, OWNER)
def test_a_second_login_after_a_refresh_refreshes_nothing(server) -> None:
    """Pins that only the first login after a bump refreshes a copy.

    A refreshed copy is saved with the prototype's version. The owner's second login reads no
    update, stat obj still shows the copy at the prototype's version, and the server log holds the
    refresh line once, read after the owner's third quit so a second refresh would already be
    logged.
    """
    implementor = server.login(IMPLEMENTOR.name)
    owner = server.login(OWNER.name)
    give_plain_sword(implementor, OWNER)
    owner_idnum = player_idnum(implementor, OWNER.name)
    shape_prototype(implementor, world.OBJECT_PLAIN_SWORD)
    log_path = server.handle.log_path
    refresh_line = REFRESH_LOG_FORMAT.format(world.OBJECT_PLAIN_SWORD, owner_idnum)

    owner = server.relog(owner)
    updated = UPDATED_FORMAT.format(world.OBJECT_PLAIN_SWORD_SHORT_DESCRIPTION)
    assert updated in owner.everything, owner.everything[-QUOTED_TEXT_LENGTH:]
    wait_for_log_count(
        log_path,
        refresh_line,
        1,
        timeout=LOG_TIMEOUT_SECONDS,
        poll=LOG_POLL_SECONDS,
        describe=describe_refresh_lines,
    )

    owner = server.relog(owner)
    second_login_text = owner.everything
    second_login_text += owner.drain(SETTLE_SECONDS)
    assert UPDATED_END not in second_login_text, second_login_text[-QUOTED_TEXT_LENGTH:]
    second_login_lines = stat_sword(implementor, OWNER)
    assert REFRESHED_LINE in second_login_lines, second_login_lines

    owner.quit()
    final_log = wait_for_log_count(
        log_path,
        QUIT_LOG_FORMAT.format(OWNER.name),
        3,
        timeout=LOG_TIMEOUT_SECONDS,
        poll=LOG_POLL_SECONDS,
        describe=describe_refresh_lines,
    )
    final_log_lines = final_log.splitlines()
    refresh_lines = [line for line in final_log_lines if REFRESH_LOG_PART in line]
    assert len(refresh_lines) == 1, refresh_lines
    assert refresh_lines[0].endswith(refresh_line), refresh_lines


@pytest.mark.object_versions_refresh
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, RENTING_MYSTIC)
def test_a_version_bump_refreshes_a_mortals_rented_copy_when_it_returns_and_keeps_its_enchant(server) -> None:
    """Pins that a bump refreshes a mortal's rented copy when it returns, keeping its enchant.

    The bumped prototype can still take the enchant. The mortal enchants its sword; the implementor
    bumps the prototype's version in the object editor and implements it, then teleports the mortal
    into the test world's inn. The mortal rents: the innkeeper stores its belongings, the connection
    returns to the character menu, and the server log records the rent at no cost with the mortal's
    gold. Logged in again, the mortal is back in the inn and reads that the sword was updated; the
    server log records the return from rent; stat obj shows the copy stamped with the prototype's
    version, the enchant and its +6 OB still on it, and the server log records the refresh with
    `enchant kept`. Rent costs nothing (Crash_offer_rent and gen_receptionist, objsave.cpp), so the
    mortal needs no gold. The one draw that can stop the cast is the concentration roll in do_cast,
    which cast_enchant_weapon repeats. The affect-hours schedule keeps the world from drawing; the
    innkeeper's special draws for the login's look in the inn, which decides nothing here.
    """
    implementor = server.login(IMPLEMENTOR.name)
    mystic = server.login(MORTAL_MYSTIC.name)
    give_plain_sword(implementor, MORTAL_MYSTIC)
    mystic_idnum = player_idnum(implementor, MORTAL_MYSTIC.name)
    log_path = server.handle.log_path

    reply = cast_enchant_weapon(mystic)
    assert GLOW_FORMAT.format("blue") in reply, reply

    shape_prototype(implementor, world.OBJECT_PLAIN_SWORD)
    online_lines = stat_sword(implementor, MORTAL_MYSTIC)
    assert BUMPED_BEFORE_LOGIN_LINE in online_lines, online_lines

    send_to_inn(implementor, mystic, MORTAL_MYSTIC)
    mystic = rent_and_log_in_again(server, mystic, MORTAL_MYSTIC)
    returned_text = mystic.everything
    updated = UPDATED_FORMAT.format(world.OBJECT_PLAIN_SWORD_SHORT_DESCRIPTION)
    assert updated in returned_text, returned_text[-QUOTED_TEXT_LENGTH:]
    assert ROOM_INN_NAME in returned_text, returned_text[-QUOTED_TEXT_LENGTH:]
    wait_for_log_count(
        log_path,
        RENT_LOG_FORMAT.format(MORTAL_MYSTIC.name),
        1,
        timeout=LOG_TIMEOUT_SECONDS,
        poll=LOG_POLL_SECONDS,
        describe=describe_refresh_lines,
    )
    wait_for_log_count(
        log_path,
        UNRENT_LOG_FORMAT.format(MORTAL_MYSTIC.name),
        1,
        timeout=LOG_TIMEOUT_SECONDS,
        poll=LOG_POLL_SECONDS,
        describe=describe_refresh_lines,
    )

    refreshed_lines = stat_sword(implementor, MORTAL_MYSTIC)
    assert REFRESHED_LINE in refreshed_lines, refreshed_lines
    assert ENCHANT_LINE_FORMAT.format("good") in refreshed_lines, refreshed_lines
    assert ENCHANT_AFFECTIONS_LINE in refreshed_lines, refreshed_lines
    refresh_line = REFRESH_LOG_FORMAT.format(world.OBJECT_PLAIN_SWORD, mystic_idnum)
    refresh_line += ENCHANT_NOTE_FORMAT.format("kept")
    wait_for_log_count(
        log_path,
        refresh_line,
        1,
        timeout=LOG_TIMEOUT_SECONDS,
        poll=LOG_POLL_SECONDS,
        describe=describe_refresh_lines,
    )


@pytest.mark.object_versions_rent
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, MORTAL_MYSTIC)
def test_the_test_innkeeper_quotes_rent_for_what_a_mortal_carries(server) -> None:
    """Pins the test world's innkeeper fixture: a mortal's offer in the inn gets its quote.

    Mob 1105 in the inn has the receptionist special (spec_ass.cpp). The quote is the day's cost of
    the level and the carried sword (Crash_offer_rent, objsave.cpp). Nothing on this path draws.
    """
    implementor = server.login(IMPLEMENTOR.name)
    mystic = server.login(MORTAL_MYSTIC.name)
    give_plain_sword(implementor, MORTAL_MYSTIC)
    send_to_inn(implementor, mystic, MORTAL_MYSTIC)

    # The quote, with factor RENT_FACTOR (1) and RENT_HALFTIME 300 (objsave.cpp):
    #   totalcost = level * factor + (cost_per_day(sword) >> 1) * factor
    #             = 30 * 1         + ((2000 / 100) / 8 >> 1) * 1
    #               (Testmystic's level; the sword's cost 2000, rent -1 and level 1, obj/16.obj)
    #             = 30 + (2 >> 1) = 31
    #   shown     = totalcost * RENT_HALFTIME * 24 / (RENT_HALFTIME + 24)
    #             = 31 * 300 * 24 / 324 = 688 copper, "6 silver and 88 copper" (money_message)
    reply = ask_innkeeper(mystic, "offer")
    assert OFFER_QUOTE_REPLY in reply, reply


@pytest.mark.object_versions_rent
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, MORTAL_MYSTIC)
@pytest.mark.parametrize(
    ("vnum", "keyword", "short_description"),
    [
        (KEY_VNUM, KEY_KEYWORD, KEY_SHORT_DESCRIPTION),
        (TRINKET_VNUM, TRINKET_KEYWORD, TRINKET_SHORT_DESCRIPTION),
        (TOKEN_VNUM, TOKEN_KEYWORD, TOKEN_SHORT_DESCRIPTION),
    ],
    ids=["key", "rent-below-minus-one", "norent-flag"],
)
def test_the_innkeeper_refuses_to_store_an_unrentable_item_and_the_mortal_keeps_it(
    server, vnum: int, keyword: str, short_description: str
) -> None:
    """Pins the inn's refusal of each kind of item Crash_is_unrentable (objsave.cpp) names.

    The kinds are a key, an item whose rent per day is below -1, and an item with the NORENT flag.
    The innkeeper tells the mortal it cannot store the item, and the rent stops there: no storing,
    no character menu, and the item is still carried by the mortal. Nothing on this path draws.
    """
    implementor = server.login(IMPLEMENTOR.name)
    mystic = server.login(MORTAL_MYSTIC.name)
    hand_new_object(implementor, vnum, keyword, short_description, MORTAL_MYSTIC)
    send_to_inn(implementor, mystic, MORTAL_MYSTIC)

    reply = ask_innkeeper(mystic, "rent")
    assert CANNOT_STORE_FORMAT.format(short_description) in reply, reply
    assert RENTED_REPLY not in reply, reply
    assert CHARACTER_MENU_PROMPT not in reply, reply

    stat_carried_object(implementor, keyword, MORTAL_MYSTIC)


@pytest.mark.object_versions_rent
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, MORTAL_MYSTIC)
def test_the_innkeeper_refuses_an_unrentable_item_inside_a_container(server) -> None:
    """Pins that the inn's refusal reaches into containers: a NORENT token in a bag stops the rent.

    Crash_report_unrentables (objsave.cpp) follows each object's contents. The token stays in the
    mortal's bag. Nothing on this path draws.
    """
    implementor = server.login(IMPLEMENTOR.name)
    mystic = server.login(MORTAL_MYSTIC.name)
    load_object(implementor, world.OBJECT_LEATHER_BAG, world.OBJECT_LEATHER_BAG_SHORT_DESCRIPTION)
    load_object(implementor, TOKEN_VNUM, TOKEN_SHORT_DESCRIPTION)
    put = implementor.command(f"put {TOKEN_KEYWORD} {BAG_KEYWORD}")
    put_reply = PUT_FORMAT.format(TOKEN_SHORT_DESCRIPTION, world.OBJECT_LEATHER_BAG_SHORT_DESCRIPTION)
    assert put.contains(put_reply), put.text
    give_object(implementor, BAG_KEYWORD, world.OBJECT_LEATHER_BAG_SHORT_DESCRIPTION, MORTAL_MYSTIC)
    send_to_inn(implementor, mystic, MORTAL_MYSTIC)

    reply = ask_innkeeper(mystic, "rent")
    assert CANNOT_STORE_FORMAT.format(TOKEN_SHORT_DESCRIPTION) in reply, reply
    assert RENTED_REPLY not in reply, reply
    assert CHARACTER_MENU_PROMPT not in reply, reply

    token_lines = stat_object(implementor, TOKEN_KEYWORD)
    location_line = CONTAINED_LOCATION_LINE_FORMAT.format(world.OBJECT_LEATHER_BAG_SHORT_DESCRIPTION, LOADER_NAME)
    assert location_line in token_lines, token_lines


@pytest.mark.object_versions_rent
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, MORTAL_MYSTIC)
def test_an_offer_gives_the_same_refusal_and_no_quote(server) -> None:
    """Pins that offer refuses an unrentable item the way rent does, with no quote.

    Crash_offer_rent (objsave.cpp) reports the unrentables before quoting: the mortal carrying a
    key reads that it cannot be stored, and no price. Nothing on this path draws.
    """
    implementor = server.login(IMPLEMENTOR.name)
    mystic = server.login(MORTAL_MYSTIC.name)
    hand_new_object(implementor, KEY_VNUM, KEY_KEYWORD, KEY_SHORT_DESCRIPTION, MORTAL_MYSTIC)
    send_to_inn(implementor, mystic, MORTAL_MYSTIC)

    reply = ask_innkeeper(mystic, "offer")
    assert CANNOT_STORE_FORMAT.format(KEY_SHORT_DESCRIPTION) in reply, reply
    assert OFFER_QUOTE_PART not in reply, reply


@pytest.mark.object_versions_rent
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, MORTAL_MYSTIC)
def test_rent_with_nothing_carried_tells_the_mortal_to_quit(server) -> None:
    """Pins the inn's answer to a mortal who carries and wears nothing: it is told to quit instead.

    The mortal stays in the game (Crash_offer_rent, objsave.cpp). Nothing on this path draws.
    """
    implementor = server.login(IMPLEMENTOR.name)
    mystic = server.login(MORTAL_MYSTIC.name)
    send_to_inn(implementor, mystic, MORTAL_MYSTIC)

    reply = ask_innkeeper(mystic, "rent")
    assert NOTHING_TO_STORE_REPLY in reply, reply
    assert RENTED_REPLY not in reply, reply
    assert CHARACTER_MENU_PROMPT not in reply, reply


@pytest.mark.object_versions_refresh
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, OWNER)
def test_a_broken_key_stays_broken_across_a_refresh(server) -> None:
    """Pins that a refresh keeps play state: a key that broke stays broken.

    The owner unlocks the chest on the floor with a BREAKABLE key; the key breaks (check_break_key,
    act_move.cpp), which the implementor sees too, and the broken key no longer locks the chest.
    After a version bump of the key and the owner's relog, the key is refreshed and still BROKEN
    (refresh_object_to_prototype_version, utility.cpp, keeps that one flag) and still cannot lock
    the chest. Nothing on this path draws.
    """
    implementor = server.login(IMPLEMENTOR.name)
    owner = server.login(OWNER.name)
    load_object(implementor, CHEST_VNUM, CHEST_SHORT_DESCRIPTION)
    dropped = implementor.command(f"drop {CHEST_KEYWORD}")
    assert dropped.contains(DROP_FORMAT.format(CHEST_SHORT_DESCRIPTION)), dropped.text
    hand_new_object(implementor, KEY_VNUM, KEY_KEYWORD, KEY_SHORT_DESCRIPTION, OWNER)
    lock_outcomes = [NO_PROPER_KEY_REPLY, LOCKED_REPLY, ALREADY_LOCKED_REPLY, NOT_A_CONTAINER_REPLY]

    unlock_outcomes = [KEY_BREAKS_REPLY, NO_PROPER_KEY_REPLY, NOT_LOCKED_REPLY, NOT_CLOSED_REPLY, NOT_A_CONTAINER_REPLY]
    unlocked = reply_to(owner, f"unlock {CHEST_KEYWORD}", unlock_outcomes)
    assert UNLOCKED_REPLY in unlocked, unlocked
    assert KEY_BREAKS_REPLY in unlocked, unlocked
    observed = settled_expect(implementor, [KEY_BREAKS_OBSERVED])
    assert UNLOCK_OBSERVED in observed, observed
    refused = reply_to(owner, f"lock {CHEST_KEYWORD}", lock_outcomes)
    assert NO_PROPER_KEY_REPLY in refused, refused
    broken_lines = stat_carried_object(implementor, KEY_KEYWORD, OWNER)
    assert BROKEN_KEY_FLAGS_LINE in broken_lines, broken_lines

    shape_prototype(implementor, KEY_VNUM)
    owner = server.relog(owner)
    updated = UPDATED_FORMAT.format(KEY_SHORT_DESCRIPTION)
    assert updated in owner.everything, owner.everything[-QUOTED_TEXT_LENGTH:]

    refreshed_lines = stat_carried_object(implementor, KEY_KEYWORD, OWNER)
    assert REFRESHED_LINE in refreshed_lines, refreshed_lines
    assert BROKEN_KEY_FLAGS_LINE in refreshed_lines, refreshed_lines
    refused_after = reply_to(owner, f"lock {CHEST_KEYWORD}", lock_outcomes)
    assert NO_PROPER_KEY_REPLY in refused_after, refused_after


@pytest.mark.object_versions_refresh
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, STRONG_OWNER)
def test_a_refreshed_worn_item_gives_its_wearer_the_new_prototypes_affect(server) -> None:
    """Pins that a worn copy refreshed at login gives its wearer the new prototype's affect.

    The refresh is Crash_obj2char's (objsave.cpp), and taking the copy off removes exactly that
    affect. The owner wears a helm without affects; the builder gives the helm's prototype STR +1
    and bumps it. After the owner's relog it reads that the helm was updated and its base strength
    is one above its constant; removing the helm takes exactly that one point off again. Nothing on
    this path draws.
    """
    implementor = server.login(IMPLEMENTOR.name)
    owner = server.login(OWNER.name)
    hand_new_object(
        implementor, world.OBJECT_LEATHER_HELM, HELM_KEYWORD, world.OBJECT_LEATHER_HELM_SHORT_DESCRIPTION, OWNER
    )
    worn = owner.command(f"wear {HELM_KEYWORD}")
    assert worn.contains(WORN_REPLY), worn.text
    worn_strength = strength_base_and_constant(implementor, OWNER.name)
    assert worn_strength == (WEARER_STRENGTH, WEARER_STRENGTH), worn_strength

    shape_prototype(implementor, world.OBJECT_LEATHER_HELM, give_strength_affect=True)
    owner = server.relog(owner)
    updated = UPDATED_FORMAT.format(world.OBJECT_LEATHER_HELM_SHORT_DESCRIPTION)
    assert updated in owner.everything, owner.everything[-QUOTED_TEXT_LENGTH:]
    refreshed_strength = strength_base_and_constant(implementor, OWNER.name)
    assert refreshed_strength == (WEARER_STRENGTH + 1, WEARER_STRENGTH), refreshed_strength

    removed = owner.command(f"remove {HELM_KEYWORD}")
    assert removed.contains(REMOVED_REPLY), removed.text
    removed_strength = strength_base_and_constant(implementor, OWNER.name)
    assert removed_strength == (WEARER_STRENGTH, WEARER_STRENGTH), removed_strength


@pytest.mark.object_versions_refresh
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, OWNER)
def test_turning_the_version_off_stamps_new_copies_and_refreshes_no_saved_copy(server) -> None:
    """Pins version off: the editor's `o` keeps the number but turns refreshes off.

    The owner keeps an unversioned sword across a quit while the builder bumps the prototype and
    turns it off in the same edit. At the owner's next login nothing is updated and its copy
    still has no version; a copy loaded now is stamped `made while off`. Nothing on this path
    draws.
    """
    implementor = server.login(IMPLEMENTOR.name)
    owner = server.login(OWNER.name)
    give_plain_sword(implementor, OWNER)

    open_object_editor(implementor, world.OBJECT_PLAIN_SWORD)
    try:
        bump_version(implementor)
        editor_step(implementor, "/22", VERSION_ON_PROMPT, [VERSION_PROMPT])
        editor_step(implementor, "o", VERSION_OFF_REPLY, [VERSION_UNCHANGED_REPLY])
        editor_step(implementor, "/implement", IMPLEMENTED_REPLY, [NOTHING_TO_IMPLEMENT_REPLY, NOT_AUTHORIZED_REPLY])
    finally:
        editor_step(implementor, "/free", OBJECT_RELEASED_REPLY, [])

    owner = server.relog(owner)
    login_text = owner.everything
    login_text += owner.drain(SETTLE_SECONDS)
    assert UPDATED_END not in login_text, login_text[-QUOTED_TEXT_LENGTH:]
    owner_lines = stat_sword(implementor, OWNER)
    assert TURNED_OFF_UNREFRESHED_LINE in owner_lines, owner_lines

    load_object(implementor, world.OBJECT_PLAIN_SWORD, world.OBJECT_PLAIN_SWORD_SHORT_DESCRIPTION)
    new_copy_lines = stat_sword(implementor, IMPLEMENTOR)
    assert MADE_WHILE_OFF_LINE in new_copy_lines, new_copy_lines


@pytest.mark.object_versions_refresh
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, SIPPING_OWNER)
def test_a_refresh_keeps_what_is_left_in_a_drink_container(server) -> None:
    """Pins that a refresh keeps play state: a waterskin keeps what is left in it.

    The owner sips once from the waterskin, leaving 9 of its 10 drinks; after a version bump of
    the waterskin and the owner's relog it reads that the waterskin was updated, and stat obj
    shows the copy at the prototype's version still holding 9. A sip drinks exactly 1 (do_drink,
    act_obj2.cpp); nothing on this path draws.
    """
    implementor = server.login(IMPLEMENTOR.name)
    owner = server.login(OWNER.name)
    hand_new_object(implementor, WATERSKIN_VNUM, WATERSKIN_KEYWORD, WATERSKIN_SHORT_DESCRIPTION, OWNER)
    sipped = reply_to(owner, f"sip {WATERSKIN_KEYWORD}", [SIP_REPLY, TOO_DRUNK_REPLY, TOO_FULL_REPLY])
    assert SIP_REPLY in sipped, sipped
    sipped_lines = stat_carried_object(implementor, WATERSKIN_KEYWORD, OWNER)
    assert SIPPED_WATERSKIN_LINE in sipped_lines, sipped_lines

    shape_prototype(implementor, WATERSKIN_VNUM)
    owner = server.relog(owner)
    updated = UPDATED_FORMAT.format(WATERSKIN_SHORT_DESCRIPTION)
    assert updated in owner.everything, owner.everything[-QUOTED_TEXT_LENGTH:]

    refreshed_lines = stat_carried_object(implementor, WATERSKIN_KEYWORD, OWNER)
    assert REFRESHED_LINE in refreshed_lines, refreshed_lines
    assert SIPPED_WATERSKIN_LINE in refreshed_lines, refreshed_lines


@pytest.mark.object_versions_enchant
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, GOOD_OWNER)
def test_a_version_bump_to_a_prototype_that_cannot_take_the_enchant_drops_it(server) -> None:
    """Pins that a refresh drops a recorded enchant the bumped prototype can no longer take.

    The owner enchants its sword; the implementor gives the prototype a strength affect in the
    object editor (field 19), so object_takes_enchant (utility.cpp) refuses it, then bumps the
    version and implements it. After the owner quits and logs in again, the copy carries the
    prototype's affect instead of the enchant's, and the prototype's flags (none) instead of the
    enchant's; stat obj lists no enchant, and the server log records the refresh with `enchant
    dropped`. The one draw that can stop the cast is the concentration roll in do_cast, which
    cast_enchant_weapon repeats; the affect-hours schedule keeps everything else from drawing.
    """
    implementor = server.login(IMPLEMENTOR.name)
    owner = server.login(OWNER.name)
    give_plain_sword(implementor, OWNER)
    owner_idnum = player_idnum(implementor, OWNER.name)

    reply = cast_enchant_weapon(owner)
    assert GLOW_FORMAT.format("blue") in reply, reply

    shape_prototype(implementor, world.OBJECT_PLAIN_SWORD, give_strength_affect=True)
    owner = server.relog(owner)
    updated = UPDATED_FORMAT.format(world.OBJECT_PLAIN_SWORD_SHORT_DESCRIPTION)
    assert updated in owner.everything, owner.everything[-QUOTED_TEXT_LENGTH:]

    refreshed_lines = stat_sword(implementor, OWNER)
    assert REFRESHED_LINE in refreshed_lines, refreshed_lines
    assert NO_ENCHANTS_LINE in refreshed_lines, refreshed_lines
    assert STRENGTH_AFFECTIONS_LINE in refreshed_lines, refreshed_lines
    assert NO_FLAGS_LINE in refreshed_lines, refreshed_lines
    refresh_line = REFRESH_LOG_FORMAT.format(world.OBJECT_PLAIN_SWORD, owner_idnum)
    refresh_line += ENCHANT_NOTE_FORMAT.format("dropped")
    wait_for_log_count(
        server.handle.log_path,
        refresh_line,
        1,
        timeout=LOG_TIMEOUT_SECONDS,
        poll=LOG_POLL_SECONDS,
        describe=describe_refresh_lines,
    )
