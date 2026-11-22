"""Gear pins: wear all, remove all and give all, belts and their hooks, and purged gear.

Wear all, remove all and give all visit every item once, in order; a belt takes what hangs on it
off with it; and a purged mob's gear leaves the world with it.

Every test here is a legacy pin on behaviour players see today: each becomes a strict xfail if
that behaviour is ever changed on purpose. No step on these paths draws a random number: wear,
remove, give, get, put, force and purge read no roll, and the affect-hours schedule keeps mob
activity and zone resets from running at all.
"""

from __future__ import annotations

import re
from collections.abc import Sequence

import pytest

from rots_harness import world
from rots_harness.characters import IMPLEMENTOR, Character, StoredValues
from rots_harness.game_text import (
    GET_FORMAT,
    INVENTORY_HEADER,
    PURGED_REPLY,
    PUT_FORMAT,
    STOP_USING_FORMAT,
    WEAR_HEAD_FORMAT,
    WEAR_WAIST_FORMAT,
)
from rots_harness.immortal_commands import GIVEN_FORMAT, give_new_object, goto, load_mob, load_object, where_lines
from rots_harness.replies import SETTLE_SECONDS, settled_expect
from rots_harness.session import GameSession
from rots_harness.stat_reading import LEVEL_PATTERN, read_stat
from rots_harness.text_lines import assert_in_order, first_number, lines_after
from rots_harness.world_resets import ObjectReset

pytestmark = [pytest.mark.scenario, pytest.mark.gear]

# A level-20 human warrior in Arena West who wears, removes and gives the gear: a mortal on the
# implementor's side, so give and goto match its name.
WEARER = Character(
    name="Testwearer",
    race="human",
    sex="male",
    level=20,
    load_room=world.ROOM_ARENA_WEST,
    class_name="warrior",
)
# A second mortal in the same room, who receives what the wearer gives and watches it remove.
TAKER = Character(
    name="Testtaker",
    race="human",
    sex="female",
    level=20,
    load_room=world.ROOM_ARENA_WEST,
    class_name="warrior",
)
# A level-2 human warrior with dexterity 1 in Arena West, whose hands fill after six items.
# Dexterity 1 is the lowest that enters the game where it is stored: calc_load_room
# (objsave.cpp) sends a character with 0 to the bugged-character room, Lost and Found.
CARRIER = Character(
    name="Testcarrier",
    race="human",
    sex="male",
    level=2,
    load_room=world.ROOM_ARENA_WEST,
    class_name="warrior",
    stored=StoredValues(dex=1),
)

# Test-world objects (obj/11.obj, obj/13.obj, obj/16.obj, obj/19.obj): each tuple is the vnum,
# a keyword that names only it among the objects a test hands over, and its short description.
# The cursed ring: worn on a finger; NODROP, so it cannot be given.
CURSED_RING = (world.OBJECT_CURSED_RING, "ring", world.OBJECT_CURSED_RING_SHORT_DESCRIPTION)
# The leather belt: worn about the waist; gives the belt hooks.
BELT = (world.OBJECT_LEATHER_BELT, "belt", world.OBJECT_LEATHER_BELT_SHORT_DESCRIPTION)
# The leather cap: worn on the head.
LEATHER_CAP = (world.OBJECT_LEATHER_CAP, "cap", world.OBJECT_LEATHER_CAP_SHORT_DESCRIPTION)
# The leather bag: a container, with no wear slot and not wieldable.
BAG = (world.OBJECT_LEATHER_BAG, "bag", world.OBJECT_LEATHER_BAG_SHORT_DESCRIPTION)
# The iron cap: worn on the head, so it finds the slot taken.
IRON_CAP = (world.OBJECT_IRON_CAP, "cap", world.OBJECT_IRON_CAP_SHORT_DESCRIPTION)
# The brass keyring: hangs on a belt hook.
KEYRING = (world.OBJECT_BRASS_KEYRING, "keyring", world.OBJECT_BRASS_KEYRING_SHORT_DESCRIPTION)
# The chain shirt: worn on the body.
CHAIN_SHIRT = (world.OBJECT_CHAIN_SHIRT, "shirt", world.OBJECT_CHAIN_SHIRT_SHORT_DESCRIPTION)
# The copper bead: carried, never worn.
COPPER_BEAD = (world.OBJECT_COPPER_BEAD, "bead", world.OBJECT_COPPER_BEAD_SHORT_DESCRIPTION)
# The leather helm: worn on the head; no zone resets one.
LEATHER_HELM = (world.OBJECT_LEATHER_HELM, "helm", world.OBJECT_LEATHER_HELM_SHORT_DESCRIPTION)

# The start every "No-one by that name ..." answer shares, goto's among them (do_goto,
# act_wiz.cpp).
NO_ONE_BY_THAT_NAME_START = "No-one by that name"
# What wear prints for a finger (wear_message, act_obj2.cpp); format with the short description.
WEAR_FINGER_FORMAT = "You slide {} on to your right ring finger."
# What wear prints for a belt hook (wear_message); format with the short description.
WEAR_BELT_HOOK_FORMAT = "You fasten {} on your belt."
# What wear prints for the body (wear_message); format with the short description.
WEAR_BODY_FORMAT = "You wear {} on your body."
# What wear all prints for an item whose head slot is already taken (perform_wear).
HEAD_TAKEN_REPLY = "You're already wearing something on your head."
# What wear all prints when nothing carried can be worn (do_wear).
NOTHING_WEARABLE_REPLY = "You don't seem to have anything wearable."
# What remove all prints when nothing is worn (do_remove).
NOTHING_WORN_REPLY = "You're not using anything."
# What the room reads as the wearer takes an item off (take_off_item); format with the name and
# the short description.
STOPS_USING_FORMAT = "{} stops using {}."
# What the receiver reads for each item given (perform_give, act_obj1.cpp); format with the
# giver's name and the short description.
RECEIVED_FORMAT = "{} gives you {}."
# What the giver reads for an item it cannot let go of (perform_give).
CANNOT_LET_GO_FORMAT = "You can't let go of {}!!  Yeech!"
# The part of what the giver reads when the receiver's hands are full (perform_give).
HANDS_FULL_PART = "seems to have"
# What the wearer reads when a belt item falls as the belt comes off (take_off_item).
FALLS_FORMAT = "You can't carry that much; {} falls to the ground."
# The part every line about an item falling to the floor shares (take_off_item).
FALLS_PART = "falls to the ground"
# What the room reads as the keyring falls (take_off_item; act() capitalises the line).
KEYRING_FALLS = "A brass keyring falls to the ground."
# How look shows the keyring on the floor.
KEYRING_LIES_HERE = "A brass keyring lies here."
# What remove prints when the wearer's hands cannot take the item itself (perform_remove).
CANNOT_CARRY_FORMAT = "{}: you can't carry that many items!"
# What the implementor reads as the porter puts the helm on (wear_message, act_obj2.cpp).
PORTER_WEARS_HELM = f"A porter wears {LEATHER_HELM[2]} on his head."
# How look shows the purged things, had any of them been left behind in the room.
LEFT_BEHIND_LINES = (
    "A leather helm lies here.",
    "A leather bag lies here.",
    "A copper bead lies here.",
    "A porter stands here",
)
# The current dexterity on stat's Dex line, one stat line the full-hands margin rests on
# (do_stat_character, act_wiz.cpp).
CURRENT_DEXTERITY_PATTERN = re.compile(r"Dex:\s*\[(-?\d+)/(-?\d+)/(-?\d+)\]")
# The items carried on stat's Carried line, the other stat line the full-hands margin rests on.
CARRIED_ITEMS_PATTERN = re.compile(r"Carried: weight: -?\d+, items: (\d+);")


def hand_over(implementor: GameSession, receiver: Character, objects: Sequence[tuple[int, str, str]]) -> None:
    """Bring the implementor to the receiver, then create and give it each object in turn.

    One object at a time, so each keyword names the only object the implementor holds, and the
    receiver's inventory lists them last given first (obj_to_char, handler.cpp, puts a new item
    at the head).
    """
    arrived = goto(implementor, receiver.name.lower())
    assert not arrived.contains(NO_ONE_BY_THAT_NAME_START), arrived.text
    for vnum, keyword, short_description in objects:
        give_new_object(
            implementor,
            vnum,
            keyword,
            receiver.name.lower(),
            shown_recipient=receiver.name,
            short_description=short_description,
        )


def inventory_lines(session: GameSession) -> list[str]:
    """Return the object lines of the session's inventory, in the order the game lists them."""
    listed = session.command("inventory").text
    return lines_after(listed, INVENTORY_HEADER)


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, WEARER)
@pytest.mark.world_resets(ObjectReset(CURSED_RING[0], world.ROOM_ARENA_WEST))
def test_wear_all_puts_on_each_carried_item_in_inventory_order_and_passes_over_the_rest(server) -> None:
    """Legacy pin: wear all visits every carried item once, head of the inventory first.

    do_wear's FIND_ALL loop (act_obj2.cpp) walks ch->carrying and calls perform_wear with wearall
    set for each item with a wear slot: the bag has none and is passed over in silence, and the
    iron cap finds the head slot the leather cap took and reads why. The belt comes before the
    keyring in the inventory, so the keyring finds the belt worn. The implementor cannot give
    the NODROP ring, so it lies on the floor at boot and the wearer picks it up last, putting it
    at the head of the inventory.
    """
    implementor = server.login(IMPLEMENTOR.name)
    wearer = server.login(WEARER.name)
    # Given last first, so the inventory lists belt, leather cap, bag, iron cap, keyring, shirt.
    hand_over(implementor, WEARER, (CHAIN_SHIRT, KEYRING, IRON_CAP, BAG, LEATHER_CAP, BELT))
    picked_up = wearer.command(f"get {CURSED_RING[1]}")
    assert picked_up.contains(GET_FORMAT.format(CURSED_RING[2])), picked_up.text
    carried = inventory_lines(wearer)
    assert carried == [
        CURSED_RING[2], BELT[2], LEATHER_CAP[2], BAG[2], IRON_CAP[2], KEYRING[2], CHAIN_SHIRT[2],
    ], carried

    wear_reply = wearer.command("wear all")
    worn = wear_reply.text + wearer.drain(SETTLE_SECONDS)
    assert_in_order(worn, [
        WEAR_FINGER_FORMAT.format(CURSED_RING[2]),
        WEAR_WAIST_FORMAT.format(BELT[2]),
        WEAR_HEAD_FORMAT.format(LEATHER_CAP[2]),
        HEAD_TAKEN_REPLY,
        WEAR_BELT_HOOK_FORMAT.format(KEYRING[2]),
        WEAR_BODY_FORMAT.format(CHAIN_SHIRT[2]),
    ])
    assert NOTHING_WEARABLE_REPLY not in worn, worn
    left_over = inventory_lines(wearer)
    assert left_over == [BAG[2], IRON_CAP[2]], left_over


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, WEARER, TAKER)
@pytest.mark.world_resets(ObjectReset(CURSED_RING[0], world.ROOM_ARENA_WEST))
def test_remove_all_takes_off_each_worn_item_in_slot_order_and_the_keyring_after_its_belt(server) -> None:
    """Legacy pin: remove all takes off everything worn once, by wear slot, with messages.

    do_remove's FIND_ALL loop (act_obj2.cpp) walks the wear slots in order and calls
    perform_remove for each one in use: the finger (slot 1), the body (5), the head (6) and the
    waist (13). Taking off the belt takes the keyring on its hook (slot 19) off with it, so the
    loop finds slot 19 empty by the time it gets there. Each item goes to the head of the
    inventory as it comes off. The taker in the room reads each removal.
    """
    implementor = server.login(IMPLEMENTOR.name)
    wearer = server.login(WEARER.name)
    taker = server.login(TAKER.name)
    hand_over(implementor, WEARER, (CHAIN_SHIRT, KEYRING, LEATHER_CAP, BELT))
    picked_up = wearer.command(f"get {CURSED_RING[1]}")
    assert picked_up.contains(GET_FORMAT.format(CURSED_RING[2])), picked_up.text
    for keyword, worn_line in (
        (CURSED_RING[1], WEAR_FINGER_FORMAT.format(CURSED_RING[2])),
        (BELT[1], WEAR_WAIST_FORMAT.format(BELT[2])),
        (LEATHER_CAP[1], WEAR_HEAD_FORMAT.format(LEATHER_CAP[2])),
        (KEYRING[1], WEAR_BELT_HOOK_FORMAT.format(KEYRING[2])),
        (CHAIN_SHIRT[1], WEAR_BODY_FORMAT.format(CHAIN_SHIRT[2])),
    ):
        worn = wearer.command(f"wear {keyword}")
        assert worn.contains(worn_line), worn.text
    carried = inventory_lines(wearer)
    assert carried == [], carried
    taker.drain(SETTLE_SECONDS)

    remove_reply = wearer.command("remove all")
    removed = remove_reply.text + wearer.drain(SETTLE_SECONDS)
    order = [CURSED_RING, CHAIN_SHIRT, LEATHER_CAP, BELT, KEYRING]
    assert_in_order(removed, [STOP_USING_FORMAT.format(item[2]) for item in order])
    assert NOTHING_WORN_REPLY not in removed, removed
    watched = settled_expect(taker, [STOPS_USING_FORMAT.format(WEARER.name, KEYRING[2])])
    assert_in_order(watched, [STOPS_USING_FORMAT.format(WEARER.name, item[2]) for item in order])
    carried = inventory_lines(wearer)
    assert carried == [item[2] for item in reversed(order)], carried


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, WEARER, TAKER)
@pytest.mark.world_resets(ObjectReset(CURSED_RING[0], world.ROOM_ARENA_WEST))
def test_give_all_hands_over_each_carried_item_in_order_and_keeps_one_it_cannot_let_go(server) -> None:
    """Legacy pin: give all offers every carried item once, head of the inventory first.

    do_give's FIND_ALL loop (act_obj1.cpp) walks ch->carrying and calls perform_give for each
    item. The NODROP ring in the middle is refused with its own line and the loop carries on to
    the items after it. The taker can carry far more than six items, so none is refused for
    full hands.
    """
    implementor = server.login(IMPLEMENTOR.name)
    wearer = server.login(WEARER.name)
    taker = server.login(TAKER.name)
    # The inventory lists keyring, belt, leather cap, ring, bag, iron cap.
    hand_over(implementor, WEARER, (IRON_CAP, BAG))
    picked_up = wearer.command(f"get {CURSED_RING[1]}")
    assert picked_up.contains(GET_FORMAT.format(CURSED_RING[2])), picked_up.text
    hand_over(implementor, WEARER, (LEATHER_CAP, BELT, KEYRING))
    order = [KEYRING, BELT, LEATHER_CAP, CURSED_RING, BAG, IRON_CAP]
    carried = inventory_lines(wearer)
    assert carried == [item[2] for item in order], carried
    taker.drain(SETTLE_SECONDS)

    give_reply = wearer.command(f"give all {TAKER.name.lower()}")
    given = give_reply.text + wearer.drain(SETTLE_SECONDS)
    assert_in_order(given, [
        GIVEN_FORMAT.format(KEYRING[2], TAKER.name),
        GIVEN_FORMAT.format(BELT[2], TAKER.name),
        GIVEN_FORMAT.format(LEATHER_CAP[2], TAKER.name),
        CANNOT_LET_GO_FORMAT.format(CURSED_RING[2]),
        GIVEN_FORMAT.format(BAG[2], TAKER.name),
        GIVEN_FORMAT.format(IRON_CAP[2], TAKER.name),
    ])
    assert HANDS_FULL_PART not in given, given
    received = settled_expect(taker, [RECEIVED_FORMAT.format(WEARER.name, IRON_CAP[2])])
    given_order = [KEYRING, BELT, LEATHER_CAP, BAG, IRON_CAP]
    assert_in_order(received, [RECEIVED_FORMAT.format(WEARER.name, item[2]) for item in given_order])
    kept = inventory_lines(wearer)
    assert kept == [CURSED_RING[2]], kept
    taken = inventory_lines(taker)
    assert taken == [item[2] for item in reversed(given_order)], taken


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, WEARER, TAKER)
def test_remove_belt_takes_the_keyring_on_its_hook_off_after_it(server) -> None:
    """Legacy pin: removing a belt takes the keyring hanging on it off too, into the inventory.

    perform_remove (act_obj2.cpp) takes the belt off, then each belt hook in use: the keyring
    comes off with the same lines as the belt, for the wearer and the room. The wearer's hands
    are nearly empty, so nothing falls (the control for the full-hands pin below).
    """
    implementor = server.login(IMPLEMENTOR.name)
    wearer = server.login(WEARER.name)
    taker = server.login(TAKER.name)
    hand_over(implementor, WEARER, (KEYRING, BELT))
    belt_worn = wearer.command("wear belt")
    assert belt_worn.contains(WEAR_WAIST_FORMAT.format(BELT[2])), belt_worn.text
    keyring_worn = wearer.command("wear keyring")
    assert keyring_worn.contains(WEAR_BELT_HOOK_FORMAT.format(KEYRING[2])), keyring_worn.text
    taker.drain(SETTLE_SECONDS)

    remove_reply = wearer.command("remove belt")
    removed = remove_reply.text + wearer.drain(SETTLE_SECONDS)
    assert_in_order(removed, [STOP_USING_FORMAT.format(BELT[2]), STOP_USING_FORMAT.format(KEYRING[2])])
    assert FALLS_PART not in removed, removed
    watched = settled_expect(taker, [STOPS_USING_FORMAT.format(WEARER.name, KEYRING[2])])
    assert_in_order(watched, [
        STOPS_USING_FORMAT.format(WEARER.name, BELT[2]),
        STOPS_USING_FORMAT.format(WEARER.name, KEYRING[2]),
    ])
    carried = inventory_lines(wearer)
    assert carried == [KEYRING[2], BELT[2]], carried


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, CARRIER, TAKER)
def test_remove_belt_with_one_hand_free_drops_the_keyring_on_its_hook(server) -> None:
    """Legacy pin: a belt that fills the wearer's last free hand lets its keyring fall.

    perform_remove (act_obj2.cpp) refuses the belt only when the hands are already full; once
    the belt is in the inventory it checks again for each belt hook, and a keyring with no hand
    left for it falls to the floor with its own lines, for the wearer and the room.
    """
    implementor = server.login(IMPLEMENTOR.name)
    carrier = server.login(CARRIER.name)
    taker = server.login(TAKER.name)
    hand_over(implementor, CARRIER, (KEYRING, BELT))
    belt_worn = carrier.command("wear belt")
    assert belt_worn.contains(WEAR_WAIST_FORMAT.format(BELT[2])), belt_worn.text
    keyring_worn = carrier.command("wear keyring")
    assert keyring_worn.contains(WEAR_BELT_HOOK_FORMAT.format(KEYRING[2])), keyring_worn.text
    hand_over(implementor, CARRIER, (COPPER_BEAD,) * 5)

    stat_text = read_stat(implementor, f"player {CARRIER.name.lower()}")
    level = first_number(LEVEL_PATTERN, stat_text)
    dexterity = first_number(CURRENT_DEXTERITY_PATTERN, stat_text)
    carried = first_number(CARRIED_ITEMS_PATTERN, stat_text)
    # Margin check, Testcarrier (level 2, current dexterity 1, five beads carried; worn items do
    # not count, equip_char in handler.cpp):
    #   CAN_CARRY_N(ch) = 5 + GET_DEX(ch) / 2 + GET_LEVEL(ch) / 2      (utils.h)
    #                   = 5 + 1 / 2           + 2 / 2                   (Dex:[1/1/1], Lev: [ 2])
    #                   = 5 + 0 + 1 = 6
    #   the belt:    IS_CARRYING_N(ch) >= CAN_CARRY_N(ch)               (perform_remove)
    #              = 5 >= 6 = false: the belt goes to the inventory, which then holds 6 items
    #   the keyring: to_floor = IS_CARRYING_N(ch) >= CAN_CARRY_N(ch)
    #              = 6 >= 6 = true: the keyring falls
    # In the control above (Testwearer, level 20, nothing carried) the keyring's check is
    # 1 >= 5 + GET_DEX / 2 + 10, false for any dexterity.
    assert (level, dexterity, carried) == (2, 1, 5), stat_text
    can_carry = 5 + dexterity // 2 + level // 2
    assert can_carry == 6, stat_text
    assert carried < can_carry, stat_text
    assert carried + 1 >= can_carry, stat_text
    taker.drain(SETTLE_SECONDS)

    remove_reply = carrier.command("remove belt")
    removed = remove_reply.text + carrier.drain(SETTLE_SECONDS)
    assert_in_order(removed, [STOP_USING_FORMAT.format(BELT[2]), FALLS_FORMAT.format(KEYRING[2])])
    assert STOP_USING_FORMAT.format(KEYRING[2]) not in removed, removed
    assert CANNOT_CARRY_FORMAT.format(BELT[2]) not in removed, removed
    watched = settled_expect(taker, [KEYRING_FALLS])
    assert_in_order(watched, [
        STOPS_USING_FORMAT.format(CARRIER.name, BELT[2]),
        KEYRING_FALLS,
    ])
    carried_after = inventory_lines(carrier)
    assert KEYRING[2] not in carried_after, carried_after
    looked = carrier.command("look").text
    assert KEYRING_LIES_HERE in looked, looked


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR)
def test_purge_destroys_what_a_mob_wears_and_carries_and_leaves_nothing_behind(server) -> None:
    """Legacy pin: purging a mob takes its worn and carried gear, and what that holds, with it.

    do_purge (act_wiz.cpp) takes the mob's worn gear off and destroys it, then destroys what it
    carries (a bag destroys its contents with it, extract_obj in handler.cpp), and only then
    removes the mob; extract_char would otherwise drop it all to the floor. The implementor
    forces the porter to wear the helm and hands it a bag with a bead in it; none of the three
    is reset anywhere, so `where` by vnum finds every copy in the world.
    """
    implementor = server.login(IMPLEMENTOR.name)
    goto(implementor, world.ROOM_ARENA_WEST, world.ROOM_ARENA_WEST_NAME, exact=False)
    load_mob(implementor, world.MOB_PORTER, world.MOB_PORTER_SHORT_DESCRIPTION)
    give_new_object(
        implementor,
        LEATHER_HELM[0],
        "helm",
        "porter",
        shown_recipient=world.MOB_PORTER_SHORT_DESCRIPTION,
        short_description=LEATHER_HELM[2],
    )
    dressed = implementor.command("force porter wear helm")
    assert dressed.contains(PORTER_WEARS_HELM), dressed.text
    load_object(implementor, BAG[0], BAG[2])
    load_object(implementor, COPPER_BEAD[0], COPPER_BEAD[2])
    put = implementor.command("put bead bag")
    assert put.contains(PUT_FORMAT.format(COPPER_BEAD[2], BAG[2])), put.text
    given = implementor.command("give bag porter")
    assert given.contains(GIVEN_FORMAT.format(BAG[2], world.MOB_PORTER_SHORT_DESCRIPTION)), given.text
    for item in (LEATHER_HELM, BAG, COPPER_BEAD):
        assert len(where_lines(implementor, item[0], item[2])) == 1, item

    purged = implementor.command("purge porter")
    assert purged.contains(PURGED_REPLY), purged.text
    for item in (LEATHER_HELM, BAG, COPPER_BEAD):
        assert where_lines(implementor, item[0], item[2]) == [], item
    looked = implementor.command("look").text
    for long_description in LEFT_BEHIND_LINES:
        assert long_description not in looked, looked
