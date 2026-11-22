"""Known bugs found while pinning object versions, each a strict xfail of the correct behaviour.

Each test starts failing as an unexpected pass once its bug is fixed.
"""

from __future__ import annotations

import dataclasses
from collections.abc import Sequence

import pytest

from rots_harness import world
from rots_harness.characters import IMPLEMENTOR, Character, StoredValues
from rots_harness.game_text import DROP_FORMAT, INVENTORY_HEADER, NOT_HERE_REPLY, NO_ONE_AROUND_REPLY, PURGED_REPLY
from rots_harness.immortal_commands import load_object
from rots_harness.replies import reply_to, settled_expect
from test_object_versions import (
    CANNOT_SEE_RENTER_REPLY,
    INNKEEPER_IN_ROOM_LINE,
    NOTHING_TO_STORE_REPLY,
    OFFER_QUOTE_REPLY,
    SIP_REPLY,
    SIPPER_DRUNKENNESS,
    SIPPER_THIRST,
    TELEPORTED_BY_IMPLEMENTOR,
    TOO_DRUNK_REPLY,
    TOO_FULL_REPLY,
    WATERSKIN_KEYWORD,
    WATERSKIN_SHORT_DESCRIPTION,
    WATERSKIN_VNUM,
    give_plain_sword,
    hand_new_object,
    stat_object,
)

pytestmark = [pytest.mark.scenario, pytest.mark.object_versions]

# A level-30 human mystic in the implementors' room: the mortal who is purged, and who asks the
# inn for an offer.
MORTAL_MYSTIC = Character(
    name="Testmystic",
    race="human",
    sex="female",
    level=30,
    load_room=world.ROOM_IMMORTAL_START,
    class_name="mystic",
)
# A level-91 immortal who keeps what it carries across a quit (do_quit, act_othe.cpp saves an
# immortal's items; a mortal's drop to the ground).
OWNER = Character(
    name="Testowner",
    race="human",
    sex="male",
    level=91,
    load_room=world.ROOM_IMMORTAL_START,
    class_name="mystic",
)

# The strange relic (world.OBJECT_STRANGE_RELIC) these pins drop has every extra flag from bit 0 to
# 28 but 5 (INVISIBLE), 7 (NODROP) and 13 (the retired !DONATE), among them 15, WILLPOWER.

# The start of the waterskin's (WATERSKIN_VNUM) weight line after one sip: the prototype's weight,
# less the 1 drink the sip took (do_drink, act_obj2.cpp, takes the drink's amount off the weight).
SIPPED_WATERSKIN_WEIGHT_START = "Weight: 29,"
# The waterskin's contents after one sip, in the middle of its drink container line.
SIPPED_WATERSKIN_CONTENTS_PART = "Contains: 9,"
# The mortal's gold for the offer: none at all.
OFFER_GOLD = 0
# The owner who sips from the waterskin.
SIPPING_OWNER = dataclasses.replace(OWNER, stored=StoredValues(drunk=SIPPER_DRUNKENNESS, thirst=SIPPER_THIRST))
# The mortal mystic with no gold.
PENNILESS_MYSTIC = dataclasses.replace(MORTAL_MYSTIC, stored=StoredValues(gold=OFFER_GOLD))

# What purge answers when it refuses (do_purge, act_wiz.cpp).
PURGE_REFUSED_REPLY = "Fuuuuuuuuu!"
# What purge answers when nothing in the room has the name (do_purge, act_wiz.cpp).
NO_ONE_TO_PURGE_REPLY = "I don't know anyone or anything by that name."
# The extra flag stat obj lists for bit 15 (extra_bits[], consts.cpp).
WILLPOWER_FLAG = " WILLPOWER "
# The start of stat obj's extra flags line (do_stat_object, act_wiz.cpp).
EXTRA_FLAGS_LINE_START = "Extra flags   :"

# The offer's answer for a renter with gold for a lifetime of rent (Crash_offer_rent, objsave.cpp).
LIFETIME_OF_RENT_REPLY = "The harness innkeeper tells you, 'You have enough gold for a lifetime of rent.'"
# The offer's answer for a renter with too little gold (Crash_offer_rent, objsave.cpp).
NOT_ENOUGH_MONEY_REPLY = "The harness innkeeper tells you, 'You haven't enough money to rent.'"


def extra_flags_line(stat_lines: Sequence[str]) -> str:
    """Return a stat obj reply's extra flags line, plus a space so the last flag matches too."""
    for line in stat_lines:
        if line.startswith(EXTRA_FLAGS_LINE_START):
            return line.rstrip(".") + " "
    raise AssertionError(f"no extra flags line in {stat_lines!r}")


@pytest.mark.object_versions_handling
@pytest.mark.xfail(
    strict=True,
    raises=AssertionError,
    reason="purge <player> should leave the purging immortal's inventory alone; today it destroys it",
)
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, MORTAL_MYSTIC)
def test_purging_a_player_leaves_the_purgers_inventory_alone(server) -> None:
    """Bug: purging a player destroys the purging immortal's inventory.

    The immortal who purges should keep what it carries. do_purge (act_wiz.cpp:1469-1470) calls
    Crash_idlesave(ch) with ch the purger, not the victim. Crash_idlesave (objsave.cpp:1466-1515)
    writes the purger's file and ends with Crash_extract_objs(ch->carrying), destroying the purger's
    inventory; the victim is never saved. Here the implementor carries a sword, purges the mortal,
    and should still carry it. Nothing on this path draws.
    """
    implementor = server.login(IMPLEMENTOR.name)
    mystic = server.login(MORTAL_MYSTIC.name)
    load_object(implementor, world.OBJECT_PLAIN_SWORD, world.OBJECT_PLAIN_SWORD_SHORT_DESCRIPTION)

    purged = implementor.command(f"purge {MORTAL_MYSTIC.name.lower()}")
    assert purged.contains(PURGED_REPLY), purged.text
    assert not purged.contains(PURGE_REFUSED_REPLY), purged.text
    assert not purged.contains(NO_ONE_TO_PURGE_REPLY), purged.text
    # The purge closes the mortal's connection from the server's side.
    mystic.close()

    inventory = implementor.command("inventory")
    assert inventory.contains(INVENTORY_HEADER), inventory.text
    assert inventory.contains(world.OBJECT_PLAIN_SWORD_SHORT_DESCRIPTION), inventory.text


@pytest.mark.object_versions_refresh
@pytest.mark.xfail(
    strict=True,
    raises=AssertionError,
    reason="a drink container's saved weight should come back at login; today it is reset to the prototype's "
    "while its contents are kept",
)
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, SIPPING_OWNER)
def test_a_sipped_drink_containers_weight_is_kept_across_a_login(server) -> None:
    """Bug: a part-drunk waterskin comes back at the prototype's weight after a login.

    It should weigh what it weighed when saved. A sip takes the drink off the waterskin's weight
    (act_obj2.cpp:167-175), and the save stores that weight (Crash_obj2store, objsave.cpp:963), but
    Crash_obj2char (objsave.cpp:473-525) restores a drink container's values and not its weight, so
    the copy comes back at the prototype's weight while holding less. Here the waterskin weighs 29
    after one sip and should weigh 29 after the owner's relog; today it weighs 30 again. A sip
    drinks exactly 1; nothing on this path draws.
    """
    implementor = server.login(IMPLEMENTOR.name)
    owner = server.login(OWNER.name)
    hand_new_object(implementor, WATERSKIN_VNUM, WATERSKIN_KEYWORD, WATERSKIN_SHORT_DESCRIPTION, OWNER)
    sipped = reply_to(owner, f"sip {WATERSKIN_KEYWORD}", [SIP_REPLY, TOO_DRUNK_REPLY, TOO_FULL_REPLY])
    assert SIP_REPLY in sipped, sipped
    sipped_lines = stat_object(implementor, WATERSKIN_KEYWORD)
    assert any(line.startswith(SIPPED_WATERSKIN_WEIGHT_START) for line in sipped_lines), sipped_lines

    owner = server.relog(owner)

    returned_lines = stat_object(implementor, WATERSKIN_KEYWORD)
    assert any(SIPPED_WATERSKIN_CONTENTS_PART in line for line in returned_lines), returned_lines
    assert any(line.startswith(SIPPED_WATERSKIN_WEIGHT_START) for line in returned_lines), returned_lines


@pytest.mark.object_versions_handling
@pytest.mark.xfail(
    strict=True,
    raises=AssertionError,
    reason="an object built with WILLPOWER should keep it when it leaves a character; today any move out of an "
    "inventory clears it, as if it came from attune",
)
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR)
def test_a_builder_set_willpower_flag_survives_a_drop(server) -> None:
    """Bug: a prototype's WILLPOWER flag is cleared from its copy when the copy is dropped.

    The flag should stay on the copy. obj_from_char (handler.cpp:1348-1349) clears WILLPOWER on
    every object leaving a character's inventory, to end attune's temporary flag (spell_attune,
    mystic.cpp:1336, which also sets prog_number 1). An object whose prototype has WILLPOWER, such
    as the live spectral rapier (obj 33601), loses it on its first drop, give or put. Here the relic
    has WILLPOWER when loaded and should still have it on the floor. Nothing on this path draws.
    """
    implementor = server.login(IMPLEMENTOR.name)
    load_object(implementor, world.OBJECT_STRANGE_RELIC, world.OBJECT_STRANGE_RELIC_SHORT_DESCRIPTION)
    loaded_lines = stat_object(implementor, world.OBJECT_STRANGE_RELIC_KEYWORD)
    loaded_flags = extra_flags_line(loaded_lines)
    assert WILLPOWER_FLAG in loaded_flags, loaded_flags

    dropped = implementor.command(f"drop {world.OBJECT_STRANGE_RELIC_KEYWORD}")
    assert dropped.contains(DROP_FORMAT.format(world.OBJECT_STRANGE_RELIC_SHORT_DESCRIPTION)), dropped.text
    dropped_lines = stat_object(implementor, world.OBJECT_STRANGE_RELIC_KEYWORD)
    dropped_flags = extra_flags_line(dropped_lines)
    assert WILLPOWER_FLAG in dropped_flags, dropped_flags


@pytest.mark.object_versions_rent
@pytest.mark.xfail(
    strict=True,
    raises=AssertionError,
    reason="an offer to a mortal with no gold should not both quote a price and promise a lifetime of rent",
)
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, PENNILESS_MYSTIC)
def test_an_offer_to_a_mortal_without_gold_does_not_promise_a_lifetime_of_rent(server) -> None:
    """Bug: the inn's offer to a mortal with no gold quotes a price and promises a lifetime of rent.

    The offer should agree with itself: a mortal with no gold who is quoted 6 silver and 88 copper a
    day should not be told its gold lasts a lifetime. Crash_offer_rent (objsave.cpp:1638-1647)
    quotes the day's cost, then sets the cost to 0 before comparing it with the gold, so every offer
    ends with the lifetime line, whatever the gold; the rent itself is never charged
    (gen_receptionist). Either fix (no quote while rent is free, or a real charge that this mortal
    cannot pay) passes this pin. Nothing on this path draws.
    """
    implementor = server.login(IMPLEMENTOR.name)
    mystic = server.login(MORTAL_MYSTIC.name)
    give_plain_sword(implementor, MORTAL_MYSTIC)
    teleport = implementor.command(f"teleport {MORTAL_MYSTIC.name.lower()} {world.ROOM_INN}")
    assert not teleport.contains(NO_ONE_AROUND_REPLY), teleport.text
    arrival = settled_expect(mystic, [TELEPORTED_BY_IMPLEMENTOR])
    assert INNKEEPER_IN_ROOM_LINE in arrival, arrival

    outcomes = [
        LIFETIME_OF_RENT_REPLY,
        NOT_ENOUGH_MONEY_REPLY,
        OFFER_QUOTE_REPLY,
        NOTHING_TO_STORE_REPLY,
        CANNOT_SEE_RENTER_REPLY,
        NOT_HERE_REPLY,
    ]
    reply = reply_to(mystic, "offer", outcomes)
    assert not (OFFER_QUOTE_REPLY in reply and LIFETIME_OF_RENT_REPLY in reply), reply
