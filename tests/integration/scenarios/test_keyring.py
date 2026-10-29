"""Keyring pins: a keyring worn on a belt hook works as a container, lock and unlock find keys
on a keyring, and the old rules around it (held bags, plain get, rent) stay as they were.

The keyring pins are new-behaviour pins: before ITEM_KEYRING existed they could not pass. The
held-bag and plain-get pins are legacy pins on behaviour the keyring must not change; each
becomes a strict xfail if that behaviour is ever changed on purpose.

No checked step draws a random number: put, get, look, wear, lock and unlock read no roll, and
the harness key is not breakable, so check_break_key (act_move.cpp) returns before acting. The
affect-hours schedule keeps zone resets and mob activity from running at all. The one roll is
the innkeeper's random social on a non-rent command (gen_receptionist, objsave.cpp), so the rent
test sends nothing to the innkeeper's room after "rent".
"""

from __future__ import annotations

import pytest

from rots_harness.characters import IMPLEMENTOR, Character
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

# Test-world objects: keyring fixtures in obj/19.obj, bag and bread in obj/11.obj, and the
# innkeeper in mob/11.mob.
KEY_VNUM = 1900  # an iron harness key; opens the Key Hall door
KEYRING_VNUM = 1901  # a brass keyring (ITEM_KEYRING), wearable on a belt hook or held
BELT_VNUM = 1902  # a leather belt, worn about the waist; gives the belt hooks
BAG_VNUM = 1136  # a leather bag, a plain container
BREAD_VNUM = 1139  # tainted bread, something that is not a key
INNKEEPER_VNUM = 1105  # the receptionist special is bound to mob 1105 (spec_ass.cpp)

KEY = "an iron harness key"
KEYRING = "a brass keyring"
BAG = "a leather bag"

# do_wear (act_obj2.cpp): the waist message, then the belt-hook message.
WEAR_BELT_MARKER = "You wear a leather belt around your waist."
WEAR_KEYRING_MARKER = "You fasten a brass keyring on your belt."
# do_grab (act_obj2.cpp): "You grab $p."
HOLD_KEYRING_MARKER = "You grab a brass keyring."
# do_unlock / do_lock (act_move.cpp), door branch.
UNLOCK_MARKER = "*click*"
LOCK_MARKER = "*Click*"
NO_KEY_MARKER = "You do not have the proper key for that."
# What the mortal reads from the innkeeper (Crash_report_unrentables, gen_receptionist).
RENT_REFUSED = f"tells you, 'You cannot store {KEY}.'"
RENT_TAKEN = "stores your belongings and helps you into your private chamber."


def hand_over(implementor: GameSession, vnums: tuple[int, ...], keywords: tuple[str, ...]) -> None:
    """The implementor goes to the mortal, creates each object and gives it to the mortal."""
    assert implementor.command(f"goto {MORTAL.name.lower()}").room_name() == "Key Hall"
    for vnum in vnums:
        assert implementor.command(f"load obj {vnum}").contains("You create"), vnum
    for keyword in keywords:
        given = implementor.command(f"give {keyword} {MORTAL.name.lower()}")
        assert given.contains("You give"), given.text


def wear_belt_keyring(mortal: GameSession) -> None:
    """The mortal wears the belt, then hangs the keyring on it."""
    belt = mortal.command("wear belt")
    assert belt.contains(WEAR_BELT_MARKER), belt.text
    ring = mortal.command("wear keyring")
    assert ring.contains(WEAR_KEYRING_MARKER), ring.text


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, MORTAL)
def test_a_belt_keyring_takes_shows_and_gives_back_a_key_by_a_short_name(server) -> None:
    implementor = server.login(IMPLEMENTOR.name)
    mortal = server.login(MORTAL.name)
    hand_over(implementor, (BELT_VNUM, KEYRING_VNUM, KEY_VNUM), ("belt", "keyring", "key"))
    wear_belt_keyring(mortal)

    # "keyr" is a 3-4 letter start of the keyword "keyring" (isname with full 0, belt_keyring).
    put = mortal.command("put key keyr")
    assert put.contains(f"You put {KEY} on {KEYRING}."), put.text
    looked = mortal.command("look in keyr")
    assert looked.contains("(worn)") and looked.contains(KEY), looked.text
    got = mortal.command("get key keyr")
    assert got.contains(f"You get {KEY} from {KEYRING}."), got.text
    carried = mortal.command("inventory")
    assert carried.contains(KEY), carried.text


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, MORTAL)
def test_a_key_on_a_belt_keyring_unlocks_and_locks_a_door_but_one_in_a_bag_does_not(server) -> None:
    implementor = server.login(IMPLEMENTOR.name)
    mortal = server.login(MORTAL.name)
    hand_over(
        implementor,
        (BELT_VNUM, KEYRING_VNUM, BAG_VNUM, KEY_VNUM),
        ("belt", "keyring", "bag", "key"),
    )
    wear_belt_keyring(mortal)

    # Control: a key inside a carried bag is not searched (has_key looks only at loose keys,
    # the held item and keyrings).
    bagged = mortal.command("put key bag")
    assert bagged.contains(f"You put {KEY} in {BAG}."), bagged.text
    refused = mortal.command("unlock door")
    assert refused.contains(NO_KEY_MARKER), refused.text

    moved = mortal.command("get key bag")
    assert moved.contains(f"You get {KEY} from {BAG}."), moved.text
    ringed = mortal.command("put key keyr")
    assert ringed.contains(f"You put {KEY} on {KEYRING}."), ringed.text
    unlocked = mortal.command("unlock door")
    assert unlocked.contains(UNLOCK_MARKER), unlocked.text
    locked = mortal.command("lock door")
    assert locked.contains(LOCK_MARKER), locked.text


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, MORTAL)
def test_a_key_on_a_held_keyring_unlocks_a_door(server) -> None:
    implementor = server.login(IMPLEMENTOR.name)
    mortal = server.login(MORTAL.name)
    hand_over(implementor, (KEYRING_VNUM, KEY_VNUM), ("keyring", "key"))

    put = mortal.command("put key keyring")
    assert put.contains(f"You put {KEY} on {KEYRING}."), put.text
    held = mortal.command("hold keyring")
    assert held.contains(HOLD_KEYRING_MARKER), held.text
    unlocked = mortal.command("unlock door")
    assert unlocked.contains(UNLOCK_MARKER), unlocked.text


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, MORTAL)
def test_only_keys_go_on_a_keyring(server) -> None:
    implementor = server.login(IMPLEMENTOR.name)
    mortal = server.login(MORTAL.name)
    hand_over(implementor, (KEYRING_VNUM, BREAD_VNUM), ("keyring", "bread"))

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
    hand_over(implementor, (BELT_VNUM, KEYRING_VNUM, KEY_VNUM), ("belt", "keyring", "key"))
    wear_belt_keyring(mortal)
    put = mortal.command("put key keyr")
    assert put.contains(f"You put {KEY} on {KEYRING}."), put.text

    got = mortal.command("get key")
    assert got.contains("You don't see a key here."), got.text
    looked = mortal.command("look in keyr")
    assert looked.contains(KEY), looked.text


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, MORTAL)
def test_put_does_not_reach_a_held_bag(server) -> None:
    """Legacy pin: a held bag is not a put target, so players can keep food out of reach of
    eating triggers by holding it. Only belt-worn keyrings got the new worn lookup."""
    implementor = server.login(IMPLEMENTOR.name)
    mortal = server.login(MORTAL.name)
    hand_over(implementor, (BAG_VNUM, KEY_VNUM), ("bag", "key"))

    held = mortal.command("hold bag")
    assert held.contains(BAG), held.text
    refused = mortal.command("put key bag")
    assert refused.contains("You don't see a bag here."), refused.text


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, MORTAL)
def test_rent_is_refused_while_a_key_is_on_a_belt_keyring(server) -> None:
    """Keys cannot be rented (Crash_is_unrentable, objsave.cpp); the innkeeper finds one on a
    worn keyring through the equipment and its contents (Crash_report_unrentables)."""
    implementor = server.login(IMPLEMENTOR.name)
    mortal = server.login(MORTAL.name)
    hand_over(implementor, (BELT_VNUM, KEYRING_VNUM, KEY_VNUM), ("belt", "keyring", "key"))
    wear_belt_keyring(mortal)
    put = mortal.command("put key keyr")
    assert put.contains(f"You put {KEY} on {KEYRING}."), put.text
    assert implementor.command(f"load mob {INNKEEPER_VNUM}").contains("You create"), "no innkeeper"

    # Both outcomes are listed, so a rent that goes through fails on the assertion at once
    # (gen_receptionist, objsave.cpp) instead of waiting for a prompt the menu never sends.
    mortal.send_line("rent")
    reply = mortal.expect([RENT_REFUSED, RENT_TAKEN])
    assert RENT_REFUSED in reply, reply


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, MORTAL)
def test_examine_a_belt_keyring_shows_its_keys(server) -> None:
    """Examine's description and contents steps find the same belt keyring (do_examine hands
    the object over, act_info.cpp)."""
    implementor = server.login(IMPLEMENTOR.name)
    mortal = server.login(MORTAL.name)
    hand_over(implementor, (BELT_VNUM, KEYRING_VNUM, KEY_VNUM), ("belt", "keyring", "key"))
    wear_belt_keyring(mortal)
    put = mortal.command("put key keyring")
    assert put.contains(f"You put {KEY} on {KEYRING}."), put.text

    examined = mortal.command("examine keyring")
    assert examined.contains("When you look inside, you see:"), examined.text
    assert examined.contains("(worn)") and examined.contains(KEY), examined.text


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, MORTAL)
def test_a_held_keyring_shows_its_keys_but_put_and_get_do_not_reach_it(server) -> None:
    """A held keyring is like a held bag: look in finds it through worn gear ("(used)"), put
    and get search only the inventory and the room."""
    implementor = server.login(IMPLEMENTOR.name)
    mortal = server.login(MORTAL.name)
    hand_over(implementor, (KEYRING_VNUM, KEY_VNUM, KEY_VNUM), ("keyring", "key", "key"))
    put = mortal.command("put key keyring")
    assert put.contains(f"You put {KEY} on {KEYRING}."), put.text
    held = mortal.command("hold keyring")
    assert held.contains(HOLD_KEYRING_MARKER), held.text

    looked = mortal.command("look in keyring")
    assert looked.contains("(used)") and looked.contains(KEY), looked.text
    refused_put = mortal.command("put key keyring")
    assert refused_put.contains("You don't see a keyring here."), refused_put.text
    refused_get = mortal.command("get key keyring")
    assert refused_get.contains("You don't have a keyring."), refused_get.text
    carried = mortal.command("inventory")
    assert carried.contains(KEY), carried.text
