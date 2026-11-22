"""Keyring pins: a keyring on a belt hook is a container, and lock and unlock find keys on it.

The old rules around it (held bags, plain get, rent) stay as they were. The keyring pins pin the
behaviour ITEM_KEYRING gives. The held-bag and plain-get pins are legacy pins on behaviour the
keyring must not change; each becomes a strict xfail if that behaviour is ever changed on purpose.

No checked step draws a random number: put, get, look, wear, lock and unlock read no roll, and
the harness key is not breakable, so check_break_key (act_move.cpp) returns before acting. The
affect-hours schedule keeps zone resets and mob activity from running at all. The one roll is
the innkeeper's random social on a non-rent command (gen_receptionist, objsave.cpp), so the rent
test sends nothing to the innkeeper's room after "rent".
"""

from __future__ import annotations

from collections.abc import Sequence

import pytest

from rots_harness import world
from rots_harness.characters import IMPLEMENTOR, Character
from rots_harness.game_text import PUT_FORMAT
from rots_harness.immortal_commands import GIVEN_START, goto, load_mob, load_object
from rots_harness.session import GameSession

pytestmark = [pytest.mark.scenario, pytest.mark.keyring]

# Room 1901, the Key Hall in zone 19, west of a door locked with key 1900 (the iron harness key).
ROOM_KEY_HALL = 1901

# A level-20 human warrior in the Key Hall, west of the locked door: a mortal on the
# implementor's side, so give and goto match its name.
MORTAL = Character(
    name="Testkeys",
    race="human",
    sex="male",
    level=20,
    load_room=ROOM_KEY_HALL,
    class_name="warrior",
)

# The keyword the implementor goes to and gives things to the mortal by.
MORTAL_KEYWORD = MORTAL.name.lower()

# Object 1900 in obj/19.obj, the iron harness key, which opens the Key Hall door.
KEY_VNUM = 1900
# KEY_VNUM's short description.
KEY_SHORT_DESCRIPTION = "an iron harness key"
# The objects the belt-keyring tests hand the mortal: the belt, the keyring and the key.
BELT_KEYRING_AND_KEY_VNUMS = (world.OBJECT_LEATHER_BELT, world.OBJECT_BRASS_KEYRING, KEY_VNUM)
# The keywords that give those objects to the mortal, in the same order.
BELT_KEYRING_AND_KEY_KEYWORDS = ("belt", "keyring", "key")

# What wearing the belt answers: the waist message (do_wear, act_obj2.cpp).
WEAR_BELT_REPLY = "You wear a leather belt around your waist."
# What wearing the keyring answers: the belt-hook message.
WEAR_KEYRING_REPLY = "You fasten a brass keyring on your belt."
# What holding the keyring answers (do_grab, act_obj2.cpp: "You grab $p.").
HOLD_KEYRING_REPLY = "You grab a brass keyring."
# What putting the key on the keyring answers (perform_put, act_obj1.cpp, keyring branch).
PUT_KEY_ON_KEYRING_REPLY = f"You put {KEY_SHORT_DESCRIPTION} on {world.OBJECT_BRASS_KEYRING_SHORT_DESCRIPTION}."
# What getting the key from the keyring answers (perform_get_from_container, act_obj1.cpp).
GET_KEY_FROM_KEYRING_REPLY = f"You get {KEY_SHORT_DESCRIPTION} from {world.OBJECT_BRASS_KEYRING_SHORT_DESCRIPTION}."
# What getting the key from the bag answers.
GET_KEY_FROM_BAG_REPLY = f"You get {KEY_SHORT_DESCRIPTION} from {world.OBJECT_LEATHER_BAG_SHORT_DESCRIPTION}."
# What unlocking the door answers (do_unlock, act_move.cpp, door branch).
UNLOCK_REPLY = "*click*"
# What locking the door answers (do_lock, act_move.cpp, door branch).
LOCK_REPLY = "*Click*"
# What lock and unlock answer when the mortal has no key they search.
NO_KEY_REPLY = "You do not have the proper key for that."
# The end of the innkeeper's tell refusing the key (Crash_report_unrentables, gen_receptionist).
RENT_REFUSED_END = f"tells you, 'You cannot store {KEY_SHORT_DESCRIPTION}.'"
# The end of what the innkeeper answers once it has stored the renter's things.
RENT_TAKEN_END = "stores your belongings and helps you into your private chamber."


def hand_over(implementor: GameSession, vnums: Sequence[int], keywords: Sequence[str]) -> None:
    """Bring the implementor to the mortal, create each object and give each by keyword."""
    goto(implementor, MORTAL_KEYWORD, "Key Hall")
    for vnum in vnums:
        load_object(implementor, vnum)

    for keyword in keywords:
        given = implementor.command(f"give {keyword} {MORTAL_KEYWORD}")
        assert given.contains(GIVEN_START), given.text


def wear_belt_keyring(mortal: GameSession) -> None:
    """Have the mortal wear the belt, then hang the keyring on it."""
    belt = mortal.command("wear belt")
    assert belt.contains(WEAR_BELT_REPLY), belt.text
    ring = mortal.command("wear keyring")
    assert ring.contains(WEAR_KEYRING_REPLY), ring.text


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, MORTAL)
def test_a_belt_keyring_takes_shows_and_gives_back_a_key_by_a_short_name(server) -> None:
    implementor = server.login(IMPLEMENTOR.name)
    mortal = server.login(MORTAL.name)
    hand_over(implementor, BELT_KEYRING_AND_KEY_VNUMS, BELT_KEYRING_AND_KEY_KEYWORDS)
    wear_belt_keyring(mortal)

    # "keyr" is a 3-4 letter start of the keyword "keyring" (isname with full 0, belt_keyring).
    put = mortal.command("put key keyr")
    assert put.contains(PUT_KEY_ON_KEYRING_REPLY), put.text
    looked = mortal.command("look in keyr")
    assert looked.contains("(worn)") and looked.contains(KEY_SHORT_DESCRIPTION), looked.text
    got = mortal.command("get key keyr")
    assert got.contains(GET_KEY_FROM_KEYRING_REPLY), got.text
    carried = mortal.command("inventory")
    assert carried.contains(KEY_SHORT_DESCRIPTION), carried.text


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, MORTAL)
def test_a_key_on_a_belt_keyring_unlocks_and_locks_a_door_but_one_in_a_bag_does_not(server) -> None:
    implementor = server.login(IMPLEMENTOR.name)
    mortal = server.login(MORTAL.name)
    hand_over(
        implementor,
        (world.OBJECT_LEATHER_BELT, world.OBJECT_BRASS_KEYRING, world.OBJECT_LEATHER_BAG, KEY_VNUM),
        ("belt", "keyring", "bag", "key"),
    )
    wear_belt_keyring(mortal)

    # Control: a key inside a carried bag is not searched (has_key looks only at loose keys,
    # the held item and keyrings).
    bagged = mortal.command("put key bag")
    put_in_bag_reply = PUT_FORMAT.format(KEY_SHORT_DESCRIPTION, world.OBJECT_LEATHER_BAG_SHORT_DESCRIPTION)
    assert bagged.contains(put_in_bag_reply), bagged.text
    refused = mortal.command("unlock door")
    assert refused.contains(NO_KEY_REPLY), refused.text

    moved = mortal.command("get key bag")
    assert moved.contains(GET_KEY_FROM_BAG_REPLY), moved.text
    ringed = mortal.command("put key keyr")
    assert ringed.contains(PUT_KEY_ON_KEYRING_REPLY), ringed.text
    unlocked = mortal.command("unlock door")
    assert unlocked.contains(UNLOCK_REPLY), unlocked.text
    locked = mortal.command("lock door")
    assert locked.contains(LOCK_REPLY), locked.text


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, MORTAL)
def test_a_key_on_a_held_keyring_unlocks_a_door(server) -> None:
    implementor = server.login(IMPLEMENTOR.name)
    mortal = server.login(MORTAL.name)
    hand_over(implementor, (world.OBJECT_BRASS_KEYRING, KEY_VNUM), ("keyring", "key"))

    put = mortal.command("put key keyring")
    assert put.contains(PUT_KEY_ON_KEYRING_REPLY), put.text
    held = mortal.command("hold keyring")
    assert held.contains(HOLD_KEYRING_REPLY), held.text
    unlocked = mortal.command("unlock door")
    assert unlocked.contains(UNLOCK_REPLY), unlocked.text


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, MORTAL)
def test_only_keys_go_on_a_keyring(server) -> None:
    implementor = server.login(IMPLEMENTOR.name)
    mortal = server.login(MORTAL.name)
    hand_over(implementor, (world.OBJECT_BRASS_KEYRING, world.OBJECT_TAINTED_BREAD), ("keyring", "bread"))

    refused = mortal.command("put bread keyring")
    assert refused.contains("Only keys can go on a keyring."), refused.text
    carried = mortal.command("inventory")
    assert carried.contains("a piece of tainted bread"), carried.text


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, MORTAL)
def test_plain_get_does_not_reach_into_a_belt_keyring(server) -> None:
    """Legacy pin: "get <x>" with no container searches only the room (do_get, act_obj1.cpp)."""
    implementor = server.login(IMPLEMENTOR.name)
    mortal = server.login(MORTAL.name)
    hand_over(implementor, BELT_KEYRING_AND_KEY_VNUMS, BELT_KEYRING_AND_KEY_KEYWORDS)
    wear_belt_keyring(mortal)
    put = mortal.command("put key keyr")
    assert put.contains(PUT_KEY_ON_KEYRING_REPLY), put.text

    got = mortal.command("get key")
    assert got.contains("You don't see a key here."), got.text
    looked = mortal.command("look in keyr")
    assert looked.contains(KEY_SHORT_DESCRIPTION), looked.text


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, MORTAL)
def test_put_does_not_reach_a_held_bag(server) -> None:
    """Legacy pin: a held bag is not a put target.

    Players can keep food out of reach of eating triggers by holding it. Only a belt-worn keyring
    has the worn lookup.
    """
    implementor = server.login(IMPLEMENTOR.name)
    mortal = server.login(MORTAL.name)
    hand_over(implementor, (world.OBJECT_LEATHER_BAG, KEY_VNUM), ("bag", "key"))

    held = mortal.command("hold bag")
    assert held.contains(world.OBJECT_LEATHER_BAG_SHORT_DESCRIPTION), held.text
    refused = mortal.command("put key bag")
    assert refused.contains("You don't see a bag here."), refused.text


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, MORTAL)
def test_rent_is_refused_while_a_key_is_on_a_belt_keyring(server) -> None:
    """Pins that a key on a worn keyring cannot be rented.

    Keys cannot be rented (Crash_is_unrentable, objsave.cpp); the innkeeper finds one on a worn
    keyring through the equipment and its contents (Crash_report_unrentables).
    """
    implementor = server.login(IMPLEMENTOR.name)
    mortal = server.login(MORTAL.name)
    hand_over(implementor, BELT_KEYRING_AND_KEY_VNUMS, BELT_KEYRING_AND_KEY_KEYWORDS)
    wear_belt_keyring(mortal)
    put = mortal.command("put key keyr")
    assert put.contains(PUT_KEY_ON_KEYRING_REPLY), put.text
    load_mob(implementor, world.MOB_INNKEEPER)

    # Both outcomes are listed, so a rent that goes through fails on the assertion at once
    # (gen_receptionist, objsave.cpp) instead of waiting for a prompt the menu never sends.
    mortal.send_line("rent")
    reply = mortal.expect([RENT_REFUSED_END, RENT_TAKEN_END])
    assert RENT_REFUSED_END in reply, reply


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, MORTAL)
def test_examine_a_belt_keyring_shows_its_keys(server) -> None:
    """Pins that examine's description and contents steps find the same belt keyring.

    do_examine (act_info.cpp) hands the object over.
    """
    implementor = server.login(IMPLEMENTOR.name)
    mortal = server.login(MORTAL.name)
    hand_over(implementor, BELT_KEYRING_AND_KEY_VNUMS, BELT_KEYRING_AND_KEY_KEYWORDS)
    wear_belt_keyring(mortal)
    put = mortal.command("put key keyring")
    assert put.contains(PUT_KEY_ON_KEYRING_REPLY), put.text

    examined = mortal.command("examine keyring")
    assert examined.contains("When you look inside, you see:"), examined.text
    assert examined.contains("(worn)") and examined.contains(KEY_SHORT_DESCRIPTION), examined.text


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, MORTAL)
def test_a_held_keyring_shows_its_keys_but_put_and_get_do_not_reach_it(server) -> None:
    """Pins that a held keyring is like a held bag.

    look in finds it through worn gear ("(used)"); put and get search only the inventory and the
    room.
    """
    implementor = server.login(IMPLEMENTOR.name)
    mortal = server.login(MORTAL.name)
    hand_over(implementor, (world.OBJECT_BRASS_KEYRING, KEY_VNUM, KEY_VNUM), ("keyring", "key", "key"))
    put = mortal.command("put key keyring")
    assert put.contains(PUT_KEY_ON_KEYRING_REPLY), put.text
    held = mortal.command("hold keyring")
    assert held.contains(HOLD_KEYRING_REPLY), held.text

    looked = mortal.command("look in keyring")
    assert looked.contains("(used)") and looked.contains(KEY_SHORT_DESCRIPTION), looked.text
    refused_put = mortal.command("put key keyring")
    assert refused_put.contains("You don't see a keyring here."), refused_put.text
    refused_get = mortal.command("get key keyring")
    assert refused_get.contains("You don't have a keyring."), refused_get.text
    carried = mortal.command("inventory")
    assert carried.contains(KEY_SHORT_DESCRIPTION), carried.text
