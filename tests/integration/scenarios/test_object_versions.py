"""Object versions and recorded enchants: enchant weapon records its enchant on the item, and a
version bump refreshes a player's saved copy at the next login, keeping the enchant only while
the new prototype can still take it, and keeping the play state a save holds. Also the test inn's
refusals of items it cannot store."""

from __future__ import annotations

import dataclasses
import re
import time
from pathlib import Path

import pytest

from rots_harness import world
from rots_harness.characters import IMPLEMENTOR, Character, StoredValues
from rots_harness.session import GameSession

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

# The plain sword's virtual number (tests/integration/world/obj/16.obj): a weapon without the
# MAGIC flag or affects of its own, so enchant weapon takes on it (object_takes_enchant).
PLAIN_SWORD_VNUM = 1601
# The keyword the sword answers to, and its short description.
SWORD_KEYWORD = "sword"
SWORD_SHORT_DESCRIPTION = "a plain sword"
# The other fixtures of obj/16.obj these scenarios use, each with its keyword and short description:
# head armour without affects; a BREAKABLE key; the chest it locks (values `100 13 1604`: closed and
# locked, key 1604); a trinket whose rent per day is -2; a token with the NORENT flag; and a
# waterskin holding 10 of its 20 drinks of water.
HELM_VNUM = 1603
HELM_KEYWORD = "helm"
HELM_SHORT_DESCRIPTION = "a leather helm"
KEY_VNUM = 1604
KEY_KEYWORD = "key"
KEY_SHORT_DESCRIPTION = "a brass key"
CHEST_VNUM = 1605
CHEST_KEYWORD = "chest"
CHEST_SHORT_DESCRIPTION = "a small chest"
TRINKET_VNUM = 1606
TRINKET_KEYWORD = "trinket"
TRINKET_SHORT_DESCRIPTION = "a cursed trinket"
TOKEN_VNUM = 1607
TOKEN_KEYWORD = "token"
TOKEN_SHORT_DESCRIPTION = "a pewter token"
WATERSKIN_VNUM = 1608
WATERSKIN_KEYWORD = "waterskin"
WATERSKIN_SHORT_DESCRIPTION = "a waterskin"
# The test world's leather bag (obj/11.obj), a container that can hold the token.
BAG_VNUM = 1136
BAG_KEYWORD = "bag"
BAG_SHORT_DESCRIPTION = "a leather bag"

# Spirit each caster is stored with: enchant weapon costs 50 and a lost concentration 25
# (do_cast, spell_pa.cpp), and nothing restores spirit between requests under affect-hours.
CASTER_SPIRIT = 500
# Alignments a caster is stored with, at the ends of wizset's range and the middle (IS_GOOD at
# 100 or more, IS_EVIL at -100 or less, utils.h).
GOOD_ALIGNMENT = 1000
EVIL_ALIGNMENT = -1000
NEUTRAL_ALIGNMENT = 0
# The affect the builder gives a prototype in the object editor's field 19: location 1 (STR),
# modifier 1. On the sword it means the sword can no longer take an enchant.
STRENGTH_AFFECT_ANSWER = "(1 1)"
# The strength constant the helm's wearer is stored with, so its base strength is known.
WEARER_STRENGTH = 15
# Thirst and drunkenness the owner is stored with before it sips: an immortal's conditions
# otherwise read 255 (advance_level, profs.cpp), and do_drink (act_obj2.cpp) refuses a drinker
# more than 10 drunk, or more than 23 thirsty.
SIPPER_THIRST = 10
SIPPER_DRUNKENNESS = 0
# The renting mortal's gold: rent costs nothing, and the rent's log line shows the gold.
RENTER_GOLD = 0


def caster(character: Character, alignment: int, gold: int | None = None) -> Character:
    """character stored with alignment, enough spirit for the casts and their repeats, and gold when given."""
    return dataclasses.replace(character, stored=StoredValues(alignment=alignment, spirit=CASTER_SPIRIT, gold=gold))


# The mortal mystic as a good, an evil and a neutral caster.
GOOD_MYSTIC = caster(MORTAL_MYSTIC, GOOD_ALIGNMENT)
EVIL_MYSTIC = caster(MORTAL_MYSTIC, EVIL_ALIGNMENT)
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
CAST_DONE_MARKER = "Ok."
# What the caster reads when the concentration roll fails (do_cast): number(0, 100) >= 100 still
# happens once in 101 casts at full knowledge.
LOST_CONCENTRATION_MARKER = "You lost your concentration!"
# What the caster reads when its spirit no longer covers a cast (can_cast_spell, spell_pa.cpp).
NO_ENERGY_MARKER = "You can't summon enough energy to cast the spell."
# What the caster reads when it does not carry the target (report_wrong_target, interpre.cpp).
NO_TARGET_MARKER = "You don't have that."
# What the caster reads when it does not know the spell (can_cast_spell).
UNKNOWN_SPELL_MARKER = "You don't know this spell."
# What enchant weapon prints for a weapon that has an affect but not the MAGIC flag
# (spell_enchant_weapon, mystic.cpp).
TOO_MUCH_MAGIC_MARKER = "There is too much magic in it already."
# What the caster reads when the enchant takes, by the caster's alignment (spell_enchant_weapon).
GLOW_MARKER_FORMAT = "A plain sword glows {}."
GLOW_FRAGMENT = " glows "
# What the owner reads after the room on entering the game, once per refreshed item
# (Crash_tell_object_refresh, objsave.cpp).
UPDATED_MARKER_FORMAT = "Your item, {}, has been updated."
UPDATED_FRAGMENT = "has been updated."
# What the implementor reads when a load, a give or a drop goes through (do_load, act_wiz.cpp;
# give and drop, act_obj1.cpp).
CREATED_MARKER_FORMAT = "You create {}."
GIVEN_MARKER_FORMAT = "You give {} to {}."
DROPPED_MARKER_FORMAT = "You drop {}."
PUT_MARKER_FORMAT = "You put {} in {}."

# The stat obj lines this file pins (do_stat_object, act_wiz.cpp); the last line of the reply is
# the affections line.
STAT_LAST_LINE_MARKER = "Affections:"
NO_SUCH_OBJECT_MARKER = "No such object around."
UNVERSIONED_LINE = "Version: prototype none, this copy none"
BUMPED_BEFORE_LOGIN_LINE = "Version: prototype 1 (on), this copy none"
REFRESHED_LINE = "Version: prototype 1 (on), this copy 1"
TURNED_OFF_UNREFRESHED_LINE = "Version: prototype 1 (off), this copy none"
MADE_WHILE_OFF_LINE = "Version: prototype 1 (off), this copy 1 (made while off)"
NO_ENCHANTS_LINE = "Enchants: none"
ENCHANT_LINE_FORMAT = "Enchants: ob +6 ({})"
ENCHANT_AFFECTIONS_LINE = "Affections: +6 to OB"
STRENGTH_AFFECTIONS_LINE = "Affections: +1 to STR"
# The extra flags line (sprintbit, utility.cpp, puts a space before each flag and a full stop after
# the last; no flags at all print <NONE>). Enchant weapon sets MAGIC, and !EVIL or !GOOD for a good
# or evil caster (apply_object_enchant, utility.cpp).
GOOD_ENCHANT_FLAGS_LINE = "Extra flags   :  MAGIC !EVIL."
EVIL_ENCHANT_FLAGS_LINE = "Extra flags   :  MAGIC !GOOD."
NEUTRAL_ENCHANT_FLAGS_LINE = "Extra flags   :  MAGIC."
NO_FLAGS_LINE = "Extra flags   : <NONE>"
BROKEN_KEY_FLAGS_LINE = "Extra flags   :  BREAKABLE BROKEN."
# The drink container line for the waterskin after one sip.
SIPPED_WATERSKIN_LINE = "Max-contains: 20, Contains: 9, Poisoned: No, Liquid: water"
# Where a copy is (do_stat_object): carried by a character, or inside a container. The loader's
# name comes from player_table, which holds names in lower case.
CARRIED_LOCATION_LINE_FORMAT = "In room: Nowhere, In object: None, Carried by: {}, Loaded by: {}"
CONTAINED_LOCATION_LINE_FORMAT = "In room: Nowhere, In object: {}, Carried by: Nobody, Loaded by: {}"
LOADER_NAME = IMPLEMENTOR.name.lower()

# The object editor's replies (shapeobj.cpp) and its prompt, "Object: <vnum>>" (consts.cpp).
EDITOR_PROMPT_FORMAT = "Object: {}>"
EDITOR_LOADED_MARKER = "Causing the eternal order to snicker, you load an object."
EDITOR_NO_FILE_MARKER = "could not open that file."
EDITOR_NO_OBJECT_MARKER = "no object here."
VERSION_PROMPT = "Enter VERSION: b, blank keeps it [none]:"
VERSION_ON_PROMPT = "Enter VERSION: b or o, blank keeps it [1 (on)]:"
VERSION_BUMPED_MARKER = "Version is now 1 (on)."
VERSION_OFF_MARKER = "Version is now 1 (off)."
VERSION_UNCHANGED_MARKER = "Version unchanged."
AFFECTS_PROMPT = "Current: (0 0) (0 0)"
AFFECTS_DROPPED_MARKER = "No affections were set. Dropped."
IMPLEMENTED_MARKER = "Object implemented"
NOTHING_TO_IMPLEMENT_MARKER = "You have nothing to implement"
NOT_AUTHORIZED_MARKER = "You're not authorized to do this."
RELEASED_MARKER = "You released an object and stopped shaping."
# What /done answers when the world file cannot be written (extra_coms_obj, replace_object).
NOT_SAVED_MARKER = "Not saved - still shaping."
NO_BACKUP_FILE_MARKER = "could not open backup file"
# The world file /done rewrites, under the test's lib: SHAPE_OBJ_DIR (protos.h) for zone 16.
ZONE_OBJECT_FILE = Path("world") / "obj" / "16.obj"
# The sword's record in obj/16.obj after a bump written by /done, without line ends: write_object
# (shapeobj.cpp) puts the version fifth on the level line, after level, rarity, material and script.
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

# What the mortal reads when the implementor teleports it (do_teleport, act_wiz.cpp), and what
# the implementor reads when the name matches no one.
TELEPORTED_MARKER = f"{IMPLEMENTOR.name} has teleported you!"
NO_ONE_MARKER = "No-one by that name around."
# The inn's name and its innkeeper's long description (tests/integration/world: wld/16.wld,
# mob/11.mob), which a character in the inn reads in the room.
INN_ROOM_NAME = "Harness Inn"
INNKEEPER_IN_ROOM_LINE = "The harness innkeeper stands behind the counter, ready to rent you a room."
# What the innkeeper's receptionist special answers (gen_receptionist, Crash_offer_rent and
# Crash_report_unrentables, objsave.cpp): the rent, the refusals the code can reach here, and the
# offer's quote.
RENTED_MARKER = "The harness innkeeper stores your belongings and helps you into your private chamber."
NOTHING_TO_STORE_MARKER = "The harness innkeeper tells you, 'But you are not carrying anything!  Just quit!'"
CANNOT_SEE_RENTER_MARKER = "The harness innkeeper says, 'I don't deal with people I can't see!'"
CANNOT_STORE_MARKER_FORMAT = "The harness innkeeper tells you, 'You cannot store {}.'"
# How every answer the innkeeper tells a mortal starts: the refusals and the quote.
INNKEEPER_TELLS_FRAGMENT = "The harness innkeeper tells you, '"
OFFER_QUOTE_MARKER = "The harness innkeeper tells you, 'It will cost you 6 silver and 88 copper for the first day.'"
OFFER_QUOTE_FRAGMENT = "It will cost you"
# What rent and offer answer with no receptionist in the room (do_rent, objsave.cpp; do_not_here,
# act_othe.cpp).
FIELD_RENT_DISABLED_MARKER = "Field-rent is disabled. You have to go to an inn now."
NOT_HERE_MARKER = "Sorry, but you can't do that here!"
# The character menu's prompt (show_character_menu_impl, interpre.cpp): rent ends the character's
# play there, leaving the connection open.
CHARACTER_MENU_PROMPT = "Make your choice:"

# What the lock and unlock commands answer for the chest (do_lock, do_unlock and check_break_key,
# act_move.cpp), to the actor and to the room.
UNLOCKED_MARKER = "*Click*"
KEY_BREAKS_MARKER = "Unfortunately, a brass key breaks as you use it!"
KEY_BREAKS_OBSERVED_MARKER = f"Unfortunately, a brass key breaks as {OWNER.name} uses it!"
UNLOCK_OBSERVED_MARKER = f"{OWNER.name} unlocks a small chest."
NO_PROPER_KEY_MARKER = "You don't seem to have the proper key."
NOT_LOCKED_MARKER = "Oh.. it wasn't locked, after all."
NOT_CLOSED_MARKER = "Silly - it ain't even closed!"
NOT_A_CONTAINER_MARKER = "That's not a container."
ALREADY_LOCKED_MARKER = "It is locked already."
LOCKED_MARKER = "*Cluck*"
# What wear and remove answer for the helm (act_obj2.cpp).
WORN_MARKER = "You wear a leather helm on your head."
REMOVED_MARKER = "You stop using a leather helm."
# What sip answers (do_drink, act_obj2.cpp): the taste, or the refusals for a drunk or full drinker.
SIP_MARKER = "It tastes like water."
TOO_DRUNK_MARKER = "You can't seem to get close enough to your mouth."
TOO_FULL_MARKER = "Your stomach can't contain anymore!"

# The server log lines these scenarios read. A refresh (Crash_refresh_object, objsave.cpp): format
# with the object's vnum and the owner's idnum, then add the enchant note for an enchanted copy.
REFRESH_LOG_FORMAT = "Object version refresh: obj {}, 0 -> 1, player idnum {}"
ENCHANT_NOTE_FORMAT = ", enchant {}"
REFRESH_LOG_FRAGMENT = "Object version refresh"
# A quit (do_quit, act_othe.cpp), a rent (gen_receptionist) and the return from it (Crash_load),
# each formatted with the character's name; the rent line shows the cost and the gold.
QUIT_LOG_FORMAT = "{} has quit the game."
RENT_LOG_FORMAT = "{} has rented (0/day, 0 tot.)"
UNRENT_LOG_FORMAT = "{} un-renting and entering game."
# The stat player line with the character's idnum (do_stat_character, act_wiz.cpp).
IDNUM_PATTERN = re.compile(r"IDNum: \[\s*(\d+)\]")
# The strength numbers of stat player: current, base and constant.
STRENGTH_PATTERN = re.compile(r"Str:\[(-?\d+)/(-?\d+)/(-?\d+)\]")
# The stat player line just before the affect lines: once it arrives, the drain collects the rest.
STAT_PLAYER_LAST_LINE_MARKER = "VUL:"

# Seconds to wait for a cast's outcome: enchant weapon's 72 beats at mystic level 30 are 36
# (CASTING_TIME, spells.h), four and a half seconds; 15 leaves room for a loaded host.
CAST_TIMEOUT = 15.0
# How much of a login's text a failed assertion quotes.
QUOTED_TEXT_LENGTH = 600
# Seconds to keep reading after an outcome, for text already sent to arrive.
SETTLE_SECONDS = 1.0
# Seconds to wait for a line to reach game.log: the Docker launcher relays the server's stderr.
LOG_TIMEOUT = 15.0
# Seconds between two reads of game.log while waiting for a line.
LOG_POLL_SECONDS = 0.25


def load_object(implementor: GameSession, vnum: int, short_description: str) -> None:
    """Loads an object into the implementor's inventory and asserts the load's reply."""
    created = implementor.command(f"load obj {vnum}")
    assert created.contains(CREATED_MARKER_FORMAT.format(short_description)), created.text


def give_object(implementor: GameSession, keyword: str, short_description: str, receiver: Character) -> None:
    """Hands an object the implementor carries to receiver, who stands in the implementor's room."""
    given = implementor.command(f"give {keyword} {receiver.name.lower()}")
    assert given.contains(GIVEN_MARKER_FORMAT.format(short_description, receiver.name)), given.text


def give_new_object(implementor: GameSession, vnum: int, keyword: str, short_description: str, receiver: Character) -> None:
    """Loads an object and hands it to receiver, who stands in the implementor's room."""
    load_object(implementor, vnum, short_description)
    give_object(implementor, keyword, short_description, receiver)


def give_plain_sword(implementor: GameSession, receiver: Character) -> None:
    """Loads a plain sword and hands it to receiver, who stands in the implementor's room."""
    give_new_object(implementor, PLAIN_SWORD_VNUM, SWORD_KEYWORD, SWORD_SHORT_DESCRIPTION, receiver)


def cast_enchant_weapon(caster: GameSession) -> str:
    """Casts enchant weapon on the sword; returns what the caster read once the spell went off.

    A lost concentration is the one roll that can stop the cast, so the cast is repeated after
    it. Each loss costs spirit and nothing restores it between requests under affect-hours, so
    the repeats end at NO_ENERGY_MARKER at the latest.
    """
    outcomes = [CAST_DONE_MARKER, LOST_CONCENTRATION_MARKER, NO_ENERGY_MARKER, NO_TARGET_MARKER, UNKNOWN_SPELL_MARKER]
    while True:
        caster.send_line(f"cast 'enchant weapon' {SWORD_KEYWORD}")
        reply = caster.expect(outcomes, timeout=CAST_TIMEOUT) + caster.drain(SETTLE_SECONDS)
        if LOST_CONCENTRATION_MARKER not in reply:
            return reply


def stat_object(implementor: GameSession, keyword: str) -> list:
    """The lines of the implementor's stat obj on the first object answering to keyword, stripped.

    stat obj looks in the implementor's inventory, then its room, then the whole world
    (get_obj_vis, handler.cpp), so each caller asserts the location line of the copy it meant.
    """
    implementor.send_line(f"stat obj {keyword}")
    text = implementor.expect([STAT_LAST_LINE_MARKER, NO_SUCH_OBJECT_MARKER]) + implementor.drain(SETTLE_SECONDS)
    assert NO_SUCH_OBJECT_MARKER not in text, text
    return [line.strip() for line in text.splitlines()]


def stat_carried_object(implementor: GameSession, keyword: str, holder: Character) -> list:
    """stat obj on the copy holder carries, the implementor having loaded it; returns the lines."""
    stat_lines = stat_object(implementor, keyword)
    assert CARRIED_LOCATION_LINE_FORMAT.format(holder.name, LOADER_NAME) in stat_lines, stat_lines
    return stat_lines


def stat_sword(implementor: GameSession, holder: Character) -> list:
    """stat obj on the sword holder carries; returns the lines."""
    return stat_carried_object(implementor, SWORD_KEYWORD, holder)


def stat_player(implementor: GameSession, name: str) -> str:
    """The implementor's stat player on the named character."""
    implementor.send_line(f"stat player {name.lower()}")
    return implementor.expect([STAT_PLAYER_LAST_LINE_MARKER]) + implementor.drain(SETTLE_SECONDS)


def player_idnum(implementor: GameSession, name: str) -> int:
    """The character's idnum, from stat player."""
    text = stat_player(implementor, name)
    match = IDNUM_PATTERN.search(text)
    assert match is not None, f"no IDNum in stat player {name}:\n{text}"
    return int(match.group(1))


def strength_base_and_constant(implementor: GameSession, name: str) -> tuple:
    """The character's base and constant strength, from stat player."""
    text = stat_player(implementor, name)
    match = STRENGTH_PATTERN.search(text)
    assert match is not None, f"no Str in stat player {name}:\n{text}"
    return int(match.group(2)), int(match.group(3))


def editor_step(implementor: GameSession, line: str, expected_marker: str, other_outcomes: list) -> str:
    """Sends one line to the object editor and returns what it read, asserting the expected reply."""
    implementor.send_line(line)
    reply = implementor.expect([expected_marker, *other_outcomes]) + implementor.drain(SETTLE_SECONDS)
    assert expected_marker in reply, f"{line!r}:\n{reply}"
    return reply


def open_object_editor(implementor: GameSession, vnum: int) -> None:
    """Starts shaping the prototype from its zone's world file."""
    editor_step(implementor, f"shape object {vnum}", EDITOR_LOADED_MARKER, [EDITOR_NO_FILE_MARKER, EDITOR_NO_OBJECT_MARKER])


def bump_version(implementor: GameSession, vnum: int) -> None:
    """Bumps an unversioned prototype's version to 1 in the open editor (field 22, `b`)."""
    editor_step(implementor, "/22", VERSION_PROMPT, [VERSION_ON_PROMPT])
    editor_step(implementor, "b", VERSION_BUMPED_MARKER, [VERSION_UNCHANGED_MARKER])


def shape_prototype(implementor: GameSession, vnum: int, give_strength_affect: bool = False) -> None:
    """Bumps a prototype's version in the object editor and implements it; optionally gives the
    prototype a strength affect first. Always ends with /free."""
    open_object_editor(implementor, vnum)
    try:
        if give_strength_affect:
            editor_step(implementor, "/19", AFFECTS_PROMPT, [])
            editor_step(implementor, STRENGTH_AFFECT_ANSWER, EDITOR_PROMPT_FORMAT.format(vnum), [AFFECTS_DROPPED_MARKER])
        bump_version(implementor, vnum)
        editor_step(implementor, "/implement", IMPLEMENTED_MARKER, [NOTHING_TO_IMPLEMENT_MARKER, NOT_AUTHORIZED_MARKER])
    finally:
        editor_step(implementor, "/free", RELEASED_MARKER, [])


def quit_and_log_in_again(server, session: GameSession, character: Character) -> GameSession:
    """Quits the character and logs it in again on a new connection; returns the new session."""
    session.quit()
    return server.login(character.name)


def send_to_inn(implementor: GameSession, mortal: GameSession, character: Character) -> None:
    """Teleports the mortal into the inn and asserts it arrived where the innkeeper stands.

    The mortal types nothing in the inn before rent or offer: the receptionist special draws for
    every other command typed there, and on a 0 the innkeeper performs a social (gen_receptionist).
    """
    teleport = implementor.command(f"teleport {character.name.lower()} {world.ROOM_INN}")
    assert not teleport.contains(NO_ONE_MARKER), teleport.text
    arrival = mortal.expect([TELEPORTED_MARKER]) + mortal.drain(SETTLE_SECONDS)
    assert INN_ROOM_NAME in arrival, arrival
    assert INNKEEPER_IN_ROOM_LINE in arrival, arrival


def rent_and_log_in_again(server, mortal: GameSession, character: Character) -> GameSession:
    """Rents at the innkeeper, asserting it stores the mortal's belongings and returns the connection
    to the character menu; closes that connection and logs the character in again on a new one,
    which it returns."""
    mortal.send_line("rent")
    outcomes = [CHARACTER_MENU_PROMPT, INNKEEPER_TELLS_FRAGMENT, CANNOT_SEE_RENTER_MARKER, FIELD_RENT_DISABLED_MARKER]
    reply = mortal.expect(outcomes) + mortal.drain(SETTLE_SECONDS)
    assert RENTED_MARKER in reply, reply
    assert CHARACTER_MENU_PROMPT in reply, reply
    mortal.close()
    return server.login(character.name)


def ask_innkeeper(mortal: GameSession, command: str) -> str:
    """Sends rent or offer in the inn and returns everything the mortal read in answer."""
    mortal.send_line(command)
    outcomes = [CHARACTER_MENU_PROMPT, INNKEEPER_TELLS_FRAGMENT, CANNOT_SEE_RENTER_MARKER, NOT_HERE_MARKER, FIELD_RENT_DISABLED_MARKER]
    return mortal.expect(outcomes) + mortal.drain(SETTLE_SECONDS)


def log_text(log_path: Path) -> str:
    """What game.log holds so far."""
    return log_path.read_text(encoding="latin-1", errors="replace") if log_path.exists() else ""


def wait_for_log_count(log_path: Path, line: str, count: int) -> str:
    """Waits until game.log holds line at least count times and returns the log; fails the test
    naming the line when it does not within LOG_TIMEOUT."""
    deadline = time.monotonic() + LOG_TIMEOUT
    while True:
        text = log_text(log_path)
        if text.count(line) >= count:
            return text
        if time.monotonic() >= deadline:
            refreshes = [log_line for log_line in text.splitlines() if REFRESH_LOG_FRAGMENT in log_line]
            raise AssertionError(f"game.log has {text.count(line)} of {count} {line!r} within {LOG_TIMEOUT}s; refresh lines: {refreshes!r}")
        time.sleep(LOG_POLL_SECONDS)


def wait_for_log_line(log_path: Path, line: str) -> None:
    """Waits until game.log holds line; fails the test naming the line when it never arrives."""
    wait_for_log_count(log_path, line, 1)


@pytest.mark.object_versions_enchant
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR)
def test_stat_shows_no_version_and_no_enchants_on_a_plain_copy(server) -> None:
    """Pins #343's stat lines on an object nobody has versioned or enchanted.

    stat obj shows the prototype's version and the copy's, both `none` while the prototype's
    version is 0, and the recorded enchants, `none` for a plain copy.
    """
    implementor = server.login(IMPLEMENTOR.name)
    load_object(implementor, PLAIN_SWORD_VNUM, SWORD_SHORT_DESCRIPTION)

    stat_lines = stat_sword(implementor, IMPLEMENTOR)
    assert UNVERSIONED_LINE in stat_lines, stat_lines
    assert NO_ENCHANTS_LINE in stat_lines, stat_lines


@pytest.mark.object_versions_enchant
@pytest.mark.schedule("affect-hours")
@pytest.mark.parametrize(
    ("glow_colour", "alignment_name", "flags_line"),
    [
        pytest.param("blue", "good", GOOD_ENCHANT_FLAGS_LINE, marks=pytest.mark.characters(IMPLEMENTOR, GOOD_MYSTIC), id="good"),
        pytest.param("red", "evil", EVIL_ENCHANT_FLAGS_LINE, marks=pytest.mark.characters(IMPLEMENTOR, EVIL_MYSTIC), id="evil"),
        pytest.param("yellow", "neutral", NEUTRAL_ENCHANT_FLAGS_LINE, marks=pytest.mark.characters(IMPLEMENTOR, NEUTRAL_MYSTIC), id="neutral"),
    ],
)
def test_enchant_weapon_records_the_enchant_with_the_casters_alignment(server, glow_colour, alignment_name, flags_line) -> None:
    """Pins #343: a successful enchant weapon is recorded on the item with the caster's alignment.

    The caster reads the glow for its alignment, and stat obj lists the recorded enchant,
    `ob +6` and the alignment, beside the +6 OB affect and the MAGIC flag the spell puts on the
    sword, with !EVIL from a good caster and !GOOD from an evil one.
    The one draw that can stop the cast is the concentration roll in do_cast, which
    cast_enchant_weapon repeats; the affect-hours schedule keeps everything else from drawing.
    """
    implementor = server.login(IMPLEMENTOR.name)
    mystic = server.login(MORTAL_MYSTIC.name)
    give_plain_sword(implementor, MORTAL_MYSTIC)

    reply = cast_enchant_weapon(mystic)
    assert GLOW_MARKER_FORMAT.format(glow_colour) in reply, reply

    stat_lines = stat_sword(implementor, MORTAL_MYSTIC)
    assert ENCHANT_LINE_FORMAT.format(alignment_name) in stat_lines, stat_lines
    assert ENCHANT_AFFECTIONS_LINE in stat_lines, stat_lines
    assert flags_line in stat_lines, stat_lines


@pytest.mark.object_versions_enchant
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, GOOD_MYSTIC)
def test_a_second_enchant_weapon_on_the_same_sword_does_nothing(server) -> None:
    """Pins today's refusal of a second enchant weapon on an enchanted sword: nothing happens.

    The first cast sets the MAGIC flag, and spell_enchant_weapon (mystic.cpp) acts only on a
    weapon without it, so the second cast goes off (`Ok.`) and then prints nothing: no glow,
    and not `There is too much magic in it already.`, which is only for a weapon that has an
    affect without the MAGIC flag. stat obj still lists the one enchant the first cast recorded,
    its one +6 OB and the first cast's flags.
    The one draw that can stop a cast is the concentration roll in do_cast, which
    cast_enchant_weapon repeats; the affect-hours schedule keeps everything else from drawing.
    """
    implementor = server.login(IMPLEMENTOR.name)
    mystic = server.login(MORTAL_MYSTIC.name)
    give_plain_sword(implementor, MORTAL_MYSTIC)

    first_reply = cast_enchant_weapon(mystic)
    assert GLOW_MARKER_FORMAT.format("blue") in first_reply, first_reply

    second_reply = cast_enchant_weapon(mystic)
    assert CAST_DONE_MARKER in second_reply, second_reply
    assert GLOW_FRAGMENT not in second_reply, second_reply
    assert TOO_MUCH_MAGIC_MARKER not in second_reply, second_reply

    stat_lines = stat_sword(implementor, MORTAL_MYSTIC)
    assert ENCHANT_LINE_FORMAT.format("good") in stat_lines, stat_lines
    assert ENCHANT_AFFECTIONS_LINE in stat_lines, stat_lines
    assert GOOD_ENCHANT_FLAGS_LINE in stat_lines, stat_lines


@pytest.mark.object_versions_refresh
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR)
def test_a_bump_saved_with_done_writes_the_version_to_the_world_file(server) -> None:
    """Pins #343: /done saves a bumped version to the zone's world file, fifth on the level line,
    and implements it, so a copy made afterwards is stamped with it.

    The object editor backs the file up to world/obj/oldobjs before rewriting it
    (replace_object, shapeobj.cpp); the test world provides that folder.
    """
    implementor = server.login(IMPLEMENTOR.name)
    open_object_editor(implementor, PLAIN_SWORD_VNUM)
    bump_version(implementor, PLAIN_SWORD_VNUM)
    try:
        editor_step(implementor, "/done", RELEASED_MARKER, [NOT_SAVED_MARKER, NO_BACKUP_FILE_MARKER, NOT_AUTHORIZED_MARKER])
    except AssertionError:
        editor_step(implementor, "/free", RELEASED_MARKER, [])
        raise

    # write_object ends each line with "\n\r".
    world_file_text = (server.lib_dir / ZONE_OBJECT_FILE).read_text(encoding="latin-1").replace("\r", "")
    file_lines = [line.strip() for line in world_file_text.split("\n") if line.strip()]
    assert DONE_SWORD_RECORD[0] in file_lines, world_file_text
    record_start = file_lines.index(DONE_SWORD_RECORD[0])
    assert file_lines[record_start:record_start + len(DONE_SWORD_RECORD)] == DONE_SWORD_RECORD, world_file_text

    load_object(implementor, PLAIN_SWORD_VNUM, SWORD_SHORT_DESCRIPTION)
    stat_lines = stat_sword(implementor, IMPLEMENTOR)
    assert REFRESHED_LINE in stat_lines, stat_lines


@pytest.mark.object_versions_refresh
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, GOOD_OWNER)
def test_a_version_bump_refreshes_the_owners_copy_at_the_next_login_and_keeps_its_enchant(server) -> None:
    """Pins #343: after a version bump, the owner's saved copy is refreshed at its next login and
    keeps its recorded enchant, since the bumped prototype can still take it.

    The owner enchants its sword; the implementor bumps the prototype's version in the object
    editor (field 22, `b`) and implements it. The copy online is not refreshed. The owner quits
    and logs in again: it reads that the sword was updated, stat obj shows the copy stamped with
    the prototype's version, the enchant, its +6 OB and its flags still on it, and the server log
    records the refresh with `enchant kept`.
    The one draw that can stop the cast is the concentration roll in do_cast, which
    cast_enchant_weapon repeats; the affect-hours schedule keeps everything else from drawing.
    """
    implementor = server.login(IMPLEMENTOR.name)
    owner = server.login(OWNER.name)
    give_plain_sword(implementor, OWNER)
    owner_idnum = player_idnum(implementor, OWNER.name)

    reply = cast_enchant_weapon(owner)
    assert GLOW_MARKER_FORMAT.format("blue") in reply, reply
    enchanted_lines = stat_sword(implementor, OWNER)
    assert UNVERSIONED_LINE in enchanted_lines, enchanted_lines
    assert ENCHANT_LINE_FORMAT.format("good") in enchanted_lines, enchanted_lines

    shape_prototype(implementor, PLAIN_SWORD_VNUM)
    online_lines = stat_sword(implementor, OWNER)
    assert BUMPED_BEFORE_LOGIN_LINE in online_lines, online_lines

    owner = quit_and_log_in_again(server, owner, OWNER)
    assert UPDATED_MARKER_FORMAT.format(SWORD_SHORT_DESCRIPTION) in owner.everything, owner.everything[-QUOTED_TEXT_LENGTH:]

    refreshed_lines = stat_sword(implementor, OWNER)
    assert REFRESHED_LINE in refreshed_lines, refreshed_lines
    assert ENCHANT_LINE_FORMAT.format("good") in refreshed_lines, refreshed_lines
    assert ENCHANT_AFFECTIONS_LINE in refreshed_lines, refreshed_lines
    assert GOOD_ENCHANT_FLAGS_LINE in refreshed_lines, refreshed_lines
    refresh_line = REFRESH_LOG_FORMAT.format(PLAIN_SWORD_VNUM, owner_idnum) + ENCHANT_NOTE_FORMAT.format("kept")
    wait_for_log_line(server.run_dir / "game.log", refresh_line)


@pytest.mark.object_versions_refresh
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, OWNER)
def test_a_second_login_after_a_refresh_refreshes_nothing(server) -> None:
    """Pins #343: a refreshed copy is saved with the prototype's version, so only the first login
    after a bump refreshes it.

    The owner's second login reads no update, stat obj still shows the copy at the prototype's
    version, and the server log holds the refresh line once, read after the owner's third quit
    so a second refresh would already be logged.
    """
    implementor = server.login(IMPLEMENTOR.name)
    owner = server.login(OWNER.name)
    give_plain_sword(implementor, OWNER)
    owner_idnum = player_idnum(implementor, OWNER.name)
    shape_prototype(implementor, PLAIN_SWORD_VNUM)
    log_path = server.run_dir / "game.log"
    refresh_line = REFRESH_LOG_FORMAT.format(PLAIN_SWORD_VNUM, owner_idnum)

    owner = quit_and_log_in_again(server, owner, OWNER)
    assert UPDATED_MARKER_FORMAT.format(SWORD_SHORT_DESCRIPTION) in owner.everything, owner.everything[-QUOTED_TEXT_LENGTH:]
    wait_for_log_line(log_path, refresh_line)

    owner = quit_and_log_in_again(server, owner, OWNER)
    second_login_text = owner.everything + owner.drain(SETTLE_SECONDS)
    assert UPDATED_FRAGMENT not in second_login_text, second_login_text[-QUOTED_TEXT_LENGTH:]
    second_login_lines = stat_sword(implementor, OWNER)
    assert REFRESHED_LINE in second_login_lines, second_login_lines

    owner.quit()
    final_log = wait_for_log_count(log_path, QUIT_LOG_FORMAT.format(OWNER.name), 3)
    refresh_lines = [line for line in final_log.splitlines() if REFRESH_LOG_FRAGMENT in line]
    assert len(refresh_lines) == 1, refresh_lines
    assert refresh_lines[0].endswith(refresh_line), refresh_lines


@pytest.mark.object_versions_refresh
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, RENTING_MYSTIC)
def test_a_version_bump_refreshes_a_mortals_rented_copy_when_it_returns_and_keeps_its_enchant(server) -> None:
    """Pins #343 for a mortal: the copy a mortal saves by renting at an inn is refreshed when it
    returns, and keeps its recorded enchant, since the bumped prototype can still take it.

    The mortal enchants its sword; the implementor bumps the prototype's version in the object
    editor and implements it, then teleports the mortal into the test world's inn. The mortal
    rents: the innkeeper stores its belongings, the connection returns to the character menu, and
    the server log records the rent at no cost with the mortal's gold. Logged in again, the mortal
    is back in the inn and reads that the sword was updated; the server log records the return
    from rent; stat obj shows the copy stamped with the prototype's version, the enchant and its
    +6 OB still on it, and the server log records the refresh with `enchant kept`. Rent costs
    nothing (Crash_offer_rent and gen_receptionist, objsave.cpp), so the mortal needs no gold.
    The one draw that can stop the cast is the concentration roll in do_cast, which
    cast_enchant_weapon repeats. The affect-hours schedule keeps the world from drawing; the
    innkeeper's special draws for the login's look in the inn, which decides nothing here.
    """
    implementor = server.login(IMPLEMENTOR.name)
    mystic = server.login(MORTAL_MYSTIC.name)
    give_plain_sword(implementor, MORTAL_MYSTIC)
    mystic_idnum = player_idnum(implementor, MORTAL_MYSTIC.name)
    log_path = server.run_dir / "game.log"

    reply = cast_enchant_weapon(mystic)
    assert GLOW_MARKER_FORMAT.format("blue") in reply, reply

    shape_prototype(implementor, PLAIN_SWORD_VNUM)
    online_lines = stat_sword(implementor, MORTAL_MYSTIC)
    assert BUMPED_BEFORE_LOGIN_LINE in online_lines, online_lines

    send_to_inn(implementor, mystic, MORTAL_MYSTIC)
    mystic = rent_and_log_in_again(server, mystic, MORTAL_MYSTIC)
    returned_text = mystic.everything
    assert UPDATED_MARKER_FORMAT.format(SWORD_SHORT_DESCRIPTION) in returned_text, returned_text[-QUOTED_TEXT_LENGTH:]
    assert INN_ROOM_NAME in returned_text, returned_text[-QUOTED_TEXT_LENGTH:]
    wait_for_log_line(log_path, RENT_LOG_FORMAT.format(MORTAL_MYSTIC.name))
    wait_for_log_line(log_path, UNRENT_LOG_FORMAT.format(MORTAL_MYSTIC.name))

    refreshed_lines = stat_sword(implementor, MORTAL_MYSTIC)
    assert REFRESHED_LINE in refreshed_lines, refreshed_lines
    assert ENCHANT_LINE_FORMAT.format("good") in refreshed_lines, refreshed_lines
    assert ENCHANT_AFFECTIONS_LINE in refreshed_lines, refreshed_lines
    refresh_line = REFRESH_LOG_FORMAT.format(PLAIN_SWORD_VNUM, mystic_idnum) + ENCHANT_NOTE_FORMAT.format("kept")
    wait_for_log_line(log_path, refresh_line)


@pytest.mark.object_versions_rent
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, MORTAL_MYSTIC)
def test_the_test_innkeeper_quotes_rent_for_what_a_mortal_carries(server) -> None:
    """Pins the test world's innkeeper fixture: mob 1105 in the inn has the receptionist special
    (spec_ass.cpp), so a mortal's offer there gets its quote.

    The quote is the day's cost of the level and the carried sword (Crash_offer_rent,
    objsave.cpp). Nothing on this path draws.
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
    assert OFFER_QUOTE_MARKER in reply, reply


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
def test_the_innkeeper_refuses_to_store_an_unrentable_item_and_the_mortal_keeps_it(server, vnum, keyword, short_description) -> None:
    """Pins the inn's refusal of each kind of item Crash_is_unrentable (objsave.cpp) names: a key,
    an item whose rent per day is below -1, and an item with the NORENT flag.

    The innkeeper tells the mortal it cannot store the item, and the rent stops there: no
    storing, no character menu, and the item is still carried by the mortal. Nothing on this
    path draws.
    """
    implementor = server.login(IMPLEMENTOR.name)
    mystic = server.login(MORTAL_MYSTIC.name)
    give_new_object(implementor, vnum, keyword, short_description, MORTAL_MYSTIC)
    send_to_inn(implementor, mystic, MORTAL_MYSTIC)

    reply = ask_innkeeper(mystic, "rent")
    assert CANNOT_STORE_MARKER_FORMAT.format(short_description) in reply, reply
    assert RENTED_MARKER not in reply, reply
    assert CHARACTER_MENU_PROMPT not in reply, reply

    stat_carried_object(implementor, keyword, MORTAL_MYSTIC)


@pytest.mark.object_versions_rent
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, MORTAL_MYSTIC)
def test_the_innkeeper_refuses_an_unrentable_item_inside_a_container(server) -> None:
    """Pins that the inn's refusal reaches into containers (Crash_report_unrentables, objsave.cpp,
    follows each object's contents): a NORENT token inside the mortal's bag stops the rent, and
    the token stays in the bag. Nothing on this path draws.
    """
    implementor = server.login(IMPLEMENTOR.name)
    mystic = server.login(MORTAL_MYSTIC.name)
    load_object(implementor, BAG_VNUM, BAG_SHORT_DESCRIPTION)
    load_object(implementor, TOKEN_VNUM, TOKEN_SHORT_DESCRIPTION)
    put = implementor.command(f"put {TOKEN_KEYWORD} {BAG_KEYWORD}")
    assert put.contains(PUT_MARKER_FORMAT.format(TOKEN_SHORT_DESCRIPTION, BAG_SHORT_DESCRIPTION)), put.text
    give_object(implementor, BAG_KEYWORD, BAG_SHORT_DESCRIPTION, MORTAL_MYSTIC)
    send_to_inn(implementor, mystic, MORTAL_MYSTIC)

    reply = ask_innkeeper(mystic, "rent")
    assert CANNOT_STORE_MARKER_FORMAT.format(TOKEN_SHORT_DESCRIPTION) in reply, reply
    assert RENTED_MARKER not in reply, reply
    assert CHARACTER_MENU_PROMPT not in reply, reply

    token_lines = stat_object(implementor, TOKEN_KEYWORD)
    assert CONTAINED_LOCATION_LINE_FORMAT.format(BAG_SHORT_DESCRIPTION, LOADER_NAME) in token_lines, token_lines


@pytest.mark.object_versions_rent
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, MORTAL_MYSTIC)
def test_an_offer_gives_the_same_refusal_and_no_quote(server) -> None:
    """Pins that offer refuses an unrentable item the way rent does (Crash_offer_rent, objsave.cpp,
    reports the unrentables before quoting): the mortal carrying a key reads that it cannot be
    stored, and no price. Nothing on this path draws.
    """
    implementor = server.login(IMPLEMENTOR.name)
    mystic = server.login(MORTAL_MYSTIC.name)
    give_new_object(implementor, KEY_VNUM, KEY_KEYWORD, KEY_SHORT_DESCRIPTION, MORTAL_MYSTIC)
    send_to_inn(implementor, mystic, MORTAL_MYSTIC)

    reply = ask_innkeeper(mystic, "offer")
    assert CANNOT_STORE_MARKER_FORMAT.format(KEY_SHORT_DESCRIPTION) in reply, reply
    assert OFFER_QUOTE_FRAGMENT not in reply, reply


@pytest.mark.object_versions_rent
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, MORTAL_MYSTIC)
def test_rent_with_nothing_carried_tells_the_mortal_to_quit(server) -> None:
    """Pins the inn's answer to a mortal who carries and wears nothing (Crash_offer_rent,
    objsave.cpp): it is told to quit instead, and stays in the game. Nothing on this path draws.
    """
    implementor = server.login(IMPLEMENTOR.name)
    mystic = server.login(MORTAL_MYSTIC.name)
    send_to_inn(implementor, mystic, MORTAL_MYSTIC)

    reply = ask_innkeeper(mystic, "rent")
    assert NOTHING_TO_STORE_MARKER in reply, reply
    assert RENTED_MARKER not in reply, reply
    assert CHARACTER_MENU_PROMPT not in reply, reply


@pytest.mark.object_versions_refresh
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, OWNER)
def test_a_broken_key_stays_broken_across_a_refresh(server) -> None:
    """Pins #343's promise that a refresh keeps play state: a key that broke stays broken.

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
    assert dropped.contains(DROPPED_MARKER_FORMAT.format(CHEST_SHORT_DESCRIPTION)), dropped.text
    give_new_object(implementor, KEY_VNUM, KEY_KEYWORD, KEY_SHORT_DESCRIPTION, OWNER)
    lock_outcomes = [NO_PROPER_KEY_MARKER, LOCKED_MARKER, ALREADY_LOCKED_MARKER, NOT_A_CONTAINER_MARKER]

    owner.send_line(f"unlock {CHEST_KEYWORD}")
    unlocked = owner.expect([KEY_BREAKS_MARKER, NO_PROPER_KEY_MARKER, NOT_LOCKED_MARKER, NOT_CLOSED_MARKER, NOT_A_CONTAINER_MARKER]) + owner.drain(SETTLE_SECONDS)
    assert UNLOCKED_MARKER in unlocked, unlocked
    assert KEY_BREAKS_MARKER in unlocked, unlocked
    observed = implementor.expect([KEY_BREAKS_OBSERVED_MARKER]) + implementor.drain(SETTLE_SECONDS)
    assert UNLOCK_OBSERVED_MARKER in observed, observed
    owner.send_line(f"lock {CHEST_KEYWORD}")
    refused = owner.expect(lock_outcomes) + owner.drain(SETTLE_SECONDS)
    assert NO_PROPER_KEY_MARKER in refused, refused
    broken_lines = stat_carried_object(implementor, KEY_KEYWORD, OWNER)
    assert BROKEN_KEY_FLAGS_LINE in broken_lines, broken_lines

    shape_prototype(implementor, KEY_VNUM)
    owner = quit_and_log_in_again(server, owner, OWNER)
    assert UPDATED_MARKER_FORMAT.format(KEY_SHORT_DESCRIPTION) in owner.everything, owner.everything[-QUOTED_TEXT_LENGTH:]

    refreshed_lines = stat_carried_object(implementor, KEY_KEYWORD, OWNER)
    assert REFRESHED_LINE in refreshed_lines, refreshed_lines
    assert BROKEN_KEY_FLAGS_LINE in refreshed_lines, refreshed_lines
    owner.send_line(f"lock {CHEST_KEYWORD}")
    refused_after = owner.expect(lock_outcomes) + owner.drain(SETTLE_SECONDS)
    assert NO_PROPER_KEY_MARKER in refused_after, refused_after


@pytest.mark.object_versions_refresh
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, STRONG_OWNER)
def test_a_refreshed_worn_item_gives_its_wearer_the_new_prototypes_affect(server) -> None:
    """Pins #343 for worn gear: a worn copy refreshed at login (Crash_obj2char, objsave.cpp) brings
    the new prototype's affect to its wearer, and taking it off removes exactly that affect.

    The owner wears a helm without affects; the builder gives the helm's prototype STR +1 and
    bumps it. After the owner's relog it reads that the helm was updated and its base strength is
    one above its constant; removing the helm takes exactly that one point off again. Nothing on
    this path draws.
    """
    implementor = server.login(IMPLEMENTOR.name)
    owner = server.login(OWNER.name)
    give_new_object(implementor, HELM_VNUM, HELM_KEYWORD, HELM_SHORT_DESCRIPTION, OWNER)
    worn = owner.command(f"wear {HELM_KEYWORD}")
    assert worn.contains(WORN_MARKER), worn.text
    assert strength_base_and_constant(implementor, OWNER.name) == (WEARER_STRENGTH, WEARER_STRENGTH)

    shape_prototype(implementor, HELM_VNUM, give_strength_affect=True)
    owner = quit_and_log_in_again(server, owner, OWNER)
    assert UPDATED_MARKER_FORMAT.format(HELM_SHORT_DESCRIPTION) in owner.everything, owner.everything[-QUOTED_TEXT_LENGTH:]
    assert strength_base_and_constant(implementor, OWNER.name) == (WEARER_STRENGTH + 1, WEARER_STRENGTH)

    removed = owner.command(f"remove {HELM_KEYWORD}")
    assert removed.contains(REMOVED_MARKER), removed.text
    assert strength_base_and_constant(implementor, OWNER.name) == (WEARER_STRENGTH, WEARER_STRENGTH)


@pytest.mark.object_versions_refresh
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, OWNER)
def test_turning_the_version_off_stamps_new_copies_and_refreshes_no_saved_copy(server) -> None:
    """Pins #343's version off: the editor's `o` keeps the number but turns refreshes off.

    The owner keeps an unversioned sword across a quit while the builder bumps the prototype and
    turns it off in the same edit. At the owner's next login nothing is updated and its copy
    still has no version; a copy loaded now is stamped `made while off`. Nothing on this path
    draws.
    """
    implementor = server.login(IMPLEMENTOR.name)
    owner = server.login(OWNER.name)
    give_plain_sword(implementor, OWNER)

    open_object_editor(implementor, PLAIN_SWORD_VNUM)
    try:
        bump_version(implementor, PLAIN_SWORD_VNUM)
        editor_step(implementor, "/22", VERSION_ON_PROMPT, [VERSION_PROMPT])
        editor_step(implementor, "o", VERSION_OFF_MARKER, [VERSION_UNCHANGED_MARKER])
        editor_step(implementor, "/implement", IMPLEMENTED_MARKER, [NOTHING_TO_IMPLEMENT_MARKER, NOT_AUTHORIZED_MARKER])
    finally:
        editor_step(implementor, "/free", RELEASED_MARKER, [])

    owner = quit_and_log_in_again(server, owner, OWNER)
    login_text = owner.everything + owner.drain(SETTLE_SECONDS)
    assert UPDATED_FRAGMENT not in login_text, login_text[-QUOTED_TEXT_LENGTH:]
    owner_lines = stat_sword(implementor, OWNER)
    assert TURNED_OFF_UNREFRESHED_LINE in owner_lines, owner_lines

    load_object(implementor, PLAIN_SWORD_VNUM, SWORD_SHORT_DESCRIPTION)
    new_copy_lines = stat_sword(implementor, IMPLEMENTOR)
    assert MADE_WHILE_OFF_LINE in new_copy_lines, new_copy_lines


@pytest.mark.object_versions_refresh
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, SIPPING_OWNER)
def test_a_refresh_keeps_what_is_left_in_a_drink_container(server) -> None:
    """Pins #343's promise that a refresh keeps play state: a waterskin keeps what is left in it.

    The owner sips once from the waterskin, leaving 9 of its 10 drinks; after a version bump of
    the waterskin and the owner's relog it reads that the waterskin was updated, and stat obj
    shows the copy at the prototype's version still holding 9. A sip drinks exactly 1 (do_drink,
    act_obj2.cpp); nothing on this path draws.
    """
    implementor = server.login(IMPLEMENTOR.name)
    owner = server.login(OWNER.name)
    give_new_object(implementor, WATERSKIN_VNUM, WATERSKIN_KEYWORD, WATERSKIN_SHORT_DESCRIPTION, OWNER)
    owner.send_line(f"sip {WATERSKIN_KEYWORD}")
    sipped = owner.expect([SIP_MARKER, TOO_DRUNK_MARKER, TOO_FULL_MARKER]) + owner.drain(SETTLE_SECONDS)
    assert SIP_MARKER in sipped, sipped
    sipped_lines = stat_carried_object(implementor, WATERSKIN_KEYWORD, OWNER)
    assert SIPPED_WATERSKIN_LINE in sipped_lines, sipped_lines

    shape_prototype(implementor, WATERSKIN_VNUM)
    owner = quit_and_log_in_again(server, owner, OWNER)
    assert UPDATED_MARKER_FORMAT.format(WATERSKIN_SHORT_DESCRIPTION) in owner.everything, owner.everything[-QUOTED_TEXT_LENGTH:]

    refreshed_lines = stat_carried_object(implementor, WATERSKIN_KEYWORD, OWNER)
    assert REFRESHED_LINE in refreshed_lines, refreshed_lines
    assert SIPPED_WATERSKIN_LINE in refreshed_lines, refreshed_lines


@pytest.mark.object_versions_enchant
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, GOOD_OWNER)
def test_a_version_bump_to_a_prototype_that_cannot_take_the_enchant_drops_it(server) -> None:
    """Pins #343: a refresh drops a recorded enchant the bumped prototype can no longer take.

    The owner enchants its sword; the implementor gives the prototype a strength affect in the
    object editor (field 19), so object_takes_enchant (utility.cpp) refuses it, then bumps the
    version and implements it. After the owner quits and logs in again, the copy carries the
    prototype's affect instead of the enchant's, and the prototype's flags (none) instead of the
    enchant's; stat obj lists no enchant, and the server log records the refresh with
    `enchant dropped`.
    The one draw that can stop the cast is the concentration roll in do_cast, which
    cast_enchant_weapon repeats; the affect-hours schedule keeps everything else from drawing.
    """
    implementor = server.login(IMPLEMENTOR.name)
    owner = server.login(OWNER.name)
    give_plain_sword(implementor, OWNER)
    owner_idnum = player_idnum(implementor, OWNER.name)

    reply = cast_enchant_weapon(owner)
    assert GLOW_MARKER_FORMAT.format("blue") in reply, reply

    shape_prototype(implementor, PLAIN_SWORD_VNUM, give_strength_affect=True)
    owner = quit_and_log_in_again(server, owner, OWNER)
    assert UPDATED_MARKER_FORMAT.format(SWORD_SHORT_DESCRIPTION) in owner.everything, owner.everything[-QUOTED_TEXT_LENGTH:]

    refreshed_lines = stat_sword(implementor, OWNER)
    assert REFRESHED_LINE in refreshed_lines, refreshed_lines
    assert NO_ENCHANTS_LINE in refreshed_lines, refreshed_lines
    assert STRENGTH_AFFECTIONS_LINE in refreshed_lines, refreshed_lines
    assert NO_FLAGS_LINE in refreshed_lines, refreshed_lines
    refresh_line = REFRESH_LOG_FORMAT.format(PLAIN_SWORD_VNUM, owner_idnum) + ENCHANT_NOTE_FORMAT.format("dropped")
    wait_for_log_line(server.run_dir / "game.log", refresh_line)
