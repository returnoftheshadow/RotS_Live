"""Known bugs found while pinning object versions: each test pins the correct behaviour and is a
strict xfail until the bug is fixed, when it starts failing as an unexpected pass."""

from __future__ import annotations

import dataclasses

import pytest

from rots_harness import world
from rots_harness.characters import IMPLEMENTOR, Character, StoredValues
from rots_harness.session import GameSession

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

# The fixtures these pins use (tests/integration/world/obj/16.obj), each with its keyword and
# short description: the plain sword; a waterskin holding 10 of its 20 drinks, weighing 30; and
# a relic whose extra flags are every bit from 0 to 28 but 5 (INVISIBLE), 7 (NODROP) and 13
# (the retired !DONATE), among them 15, WILLPOWER.
PLAIN_SWORD_VNUM = 1601
SWORD_KEYWORD = "sword"
SWORD_SHORT_DESCRIPTION = "a plain sword"
WATERSKIN_VNUM = 1608
WATERSKIN_KEYWORD = "waterskin"
WATERSKIN_SHORT_DESCRIPTION = "a waterskin"
RELIC_VNUM = 1609
RELIC_KEYWORD = "relic"
RELIC_SHORT_DESCRIPTION = "a strange relic"
# The waterskin's weight after one sip: the prototype's 30, less the 1 drink the sip took
# (do_drink, act_obj2.cpp, takes the drink's amount off the weight).
SIPPED_WATERSKIN_WEIGHT_LINE_START = "Weight: 29,"
SIPPED_WATERSKIN_CONTENTS_FRAGMENT = "Contains: 9,"
# Thirst and drunkenness the owner is stored with before it sips: an immortal's conditions
# otherwise read 255 (advance_level, profs.cpp), and do_drink refuses a drinker more than 10
# drunk, or more than 23 thirsty.
SIPPER_THIRST = 10
SIPPER_DRUNKENNESS = 0
# The mortal's gold for the offer: none at all.
OFFER_GOLD = 0
# The owner who sips from the waterskin.
SIPPING_OWNER = dataclasses.replace(OWNER, stored=StoredValues(drunk=SIPPER_DRUNKENNESS, thirst=SIPPER_THIRST))
# The mortal mystic with no gold.
PENNILESS_MYSTIC = dataclasses.replace(MORTAL_MYSTIC, stored=StoredValues(gold=OFFER_GOLD))

# What the implementor reads when a load, a give or a drop goes through (do_load, act_wiz.cpp;
# give and drop, act_obj1.cpp).
CREATED_MARKER_FORMAT = "You create {}."
GIVEN_MARKER_FORMAT = "You give {} to {}."
DROPPED_MARKER_FORMAT = "You drop {}."
# What the implementor reads when the purge goes through (do_purge, act_wiz.cpp), and its inventory
# header (do_inventory, act_info.cpp).
PURGED_MARKER = "Ok."
PURGE_REFUSED_MARKER = "Fuuuuuuuuu!"
NO_ONE_TO_PURGE_MARKER = "I don't know anyone or anything by that name."
INVENTORY_HEADER = "You are carrying:"
# What sip answers (do_drink, act_obj2.cpp): the taste, or the refusals for a drunk or full drinker.
SIP_MARKER = "It tastes like water."
TOO_DRUNK_MARKER = "You can't seem to get close enough to your mouth."
TOO_FULL_MARKER = "Your stomach can't contain anymore!"
# The extra flag stat obj lists for bit 15 (extra_bits[], consts.cpp).
WILLPOWER_FLAG = " WILLPOWER "
EXTRA_FLAGS_LINE_START = "Extra flags   :"

# The stat obj reply's last line, and its answer when nothing matches (do_stat_object, do_wizstat).
STAT_LAST_LINE_MARKER = "Affections:"
NO_SUCH_OBJECT_MARKER = "No such object around."
# The teleport reply the mortal reads (do_teleport, act_wiz.cpp), the implementor's reply when the
# name matches no one, and the inn the mortal lands in.
TELEPORTED_MARKER = f"{IMPLEMENTOR.name} has teleported you!"
NO_ONE_MARKER = "No-one by that name around."
INNKEEPER_IN_ROOM_LINE = "The harness innkeeper stands behind the counter, ready to rent you a room."
# The offer's answers (Crash_offer_rent, objsave.cpp): the quote for the carried sword, the
# lifetime line, the refusal for too little gold, and the refusals the code can reach here.
OFFER_QUOTE_MARKER = "The harness innkeeper tells you, 'It will cost you 6 silver and 88 copper for the first day.'"
LIFETIME_OF_RENT_MARKER = "The harness innkeeper tells you, 'You have enough gold for a lifetime of rent.'"
NOT_ENOUGH_MONEY_MARKER = "The harness innkeeper tells you, 'You haven't enough money to rent.'"
NOTHING_TO_STORE_MARKER = "The harness innkeeper tells you, 'But you are not carrying anything!  Just quit!'"
CANNOT_SEE_RENTER_MARKER = "The harness innkeeper says, 'I don't deal with people I can't see!'"
NOT_HERE_MARKER = "Sorry, but you can't do that here!"
# Seconds to keep reading after an outcome, for text already sent to arrive.
SETTLE_SECONDS = 1.0


def give_new_object(implementor: GameSession, vnum: int, keyword: str, short_description: str, receiver: Character) -> None:
    """Loads an object and hands it to receiver, who stands in the implementor's room."""
    created = implementor.command(f"load obj {vnum}")
    assert created.contains(CREATED_MARKER_FORMAT.format(short_description)), created.text
    given = implementor.command(f"give {keyword} {receiver.name.lower()}")
    assert given.contains(GIVEN_MARKER_FORMAT.format(short_description, receiver.name)), given.text


def stat_object(implementor: GameSession, keyword: str) -> list:
    """The lines of the implementor's stat obj on the first object answering to keyword, stripped."""
    implementor.send_line(f"stat obj {keyword}")
    text = implementor.expect([STAT_LAST_LINE_MARKER, NO_SUCH_OBJECT_MARKER]) + implementor.drain(SETTLE_SECONDS)
    assert NO_SUCH_OBJECT_MARKER not in text, text
    return [line.strip() for line in text.splitlines()]


def extra_flags_line(stat_lines: list) -> str:
    """The extra flags line of a stat obj reply, with a space after it so the last flag matches."""
    for line in stat_lines:
        if line.startswith(EXTRA_FLAGS_LINE_START):
            return line.rstrip(".") + " "
    raise AssertionError(f"no extra flags line in {stat_lines!r}")


@pytest.mark.object_versions_handling
@pytest.mark.xfail(strict=True, raises=AssertionError, reason="purge <player> should leave the purging immortal's inventory alone; today it destroys it")
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, MORTAL_MYSTIC)
def test_purging_a_player_leaves_the_purgers_inventory_alone(server) -> None:
    """Pins the correct purge of a player: the immortal who purges keeps what it carries.

    Bug: do_purge (act_wiz.cpp:1469-1470) calls Crash_idlesave(ch) with ch the purger, not the
    victim. Crash_idlesave (objsave.cpp:1466-1515) writes the purger's file and ends with
    Crash_extract_objs(ch->carrying), destroying the purger's inventory; the victim is never
    saved. Here the implementor carries a sword, purges the mortal, and should still carry it.
    Nothing on this path draws.
    """
    implementor = server.login(IMPLEMENTOR.name)
    mystic = server.login(MORTAL_MYSTIC.name)
    created = implementor.command(f"load obj {PLAIN_SWORD_VNUM}")
    assert created.contains(CREATED_MARKER_FORMAT.format(SWORD_SHORT_DESCRIPTION)), created.text

    purged = implementor.command(f"purge {MORTAL_MYSTIC.name.lower()}")
    assert purged.contains(PURGED_MARKER), purged.text
    assert not purged.contains(PURGE_REFUSED_MARKER), purged.text
    assert not purged.contains(NO_ONE_TO_PURGE_MARKER), purged.text
    # The purge closes the mortal's connection from the server's side.
    mystic.close()

    inventory = implementor.command("inventory")
    assert inventory.contains(INVENTORY_HEADER), inventory.text
    assert inventory.contains(SWORD_SHORT_DESCRIPTION), inventory.text


@pytest.mark.object_versions_refresh
@pytest.mark.xfail(strict=True, raises=AssertionError, reason="a drink container's saved weight should come back at login; today it is reset to the prototype's while its contents are kept")
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, SIPPING_OWNER)
def test_a_sipped_drink_containers_weight_is_kept_across_a_login(server) -> None:
    """Pins the correct weight of a part-drunk waterskin after a login: what it weighed when saved.

    Bug: a sip takes the drink off the waterskin's weight (act_obj2.cpp:167-175), and the save
    stores that weight (Crash_obj2store, objsave.cpp:963), but Crash_obj2char
    (objsave.cpp:473-525) restores a drink container's values and not its weight, so the copy
    comes back at the prototype's weight while holding less. Here the waterskin weighs 29 after
    one sip and should weigh 29 after the owner's relog; today it weighs 30 again. A sip drinks
    exactly 1; nothing on this path draws.
    """
    implementor = server.login(IMPLEMENTOR.name)
    owner = server.login(OWNER.name)
    give_new_object(implementor, WATERSKIN_VNUM, WATERSKIN_KEYWORD, WATERSKIN_SHORT_DESCRIPTION, OWNER)
    owner.send_line(f"sip {WATERSKIN_KEYWORD}")
    sipped = owner.expect([SIP_MARKER, TOO_DRUNK_MARKER, TOO_FULL_MARKER]) + owner.drain(SETTLE_SECONDS)
    assert SIP_MARKER in sipped, sipped
    sipped_lines = stat_object(implementor, WATERSKIN_KEYWORD)
    assert any(line.startswith(SIPPED_WATERSKIN_WEIGHT_LINE_START) for line in sipped_lines), sipped_lines

    owner.quit()
    owner = server.login(OWNER.name)

    returned_lines = stat_object(implementor, WATERSKIN_KEYWORD)
    assert any(SIPPED_WATERSKIN_CONTENTS_FRAGMENT in line for line in returned_lines), returned_lines
    assert any(line.startswith(SIPPED_WATERSKIN_WEIGHT_LINE_START) for line in returned_lines), returned_lines


@pytest.mark.object_versions_handling
@pytest.mark.xfail(strict=True, raises=AssertionError, reason="an object built with WILLPOWER should keep it when it leaves a character; today any move out of an inventory clears it, as if it came from attune")
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR)
def test_a_builder_set_willpower_flag_survives_a_drop(server) -> None:
    """Pins that a prototype's WILLPOWER flag stays on its copy when the copy is dropped.

    Bug: obj_from_char (handler.cpp:1348-1349) clears WILLPOWER on every object leaving a
    character's inventory, to end attune's temporary flag (spell_attune, mystic.cpp:1336, which
    also sets prog_number 1). An object whose prototype has WILLPOWER, such as the live spectral
    rapier (obj 33601), loses it on its first drop, give or put. Here the relic has WILLPOWER when
    loaded and should still have it on the floor. Nothing on this path draws.
    """
    implementor = server.login(IMPLEMENTOR.name)
    created = implementor.command(f"load obj {RELIC_VNUM}")
    assert created.contains(CREATED_MARKER_FORMAT.format(RELIC_SHORT_DESCRIPTION)), created.text
    loaded_flags = extra_flags_line(stat_object(implementor, RELIC_KEYWORD))
    assert WILLPOWER_FLAG in loaded_flags, loaded_flags

    dropped = implementor.command(f"drop {RELIC_KEYWORD}")
    assert dropped.contains(DROPPED_MARKER_FORMAT.format(RELIC_SHORT_DESCRIPTION)), dropped.text
    dropped_flags = extra_flags_line(stat_object(implementor, RELIC_KEYWORD))
    assert WILLPOWER_FLAG in dropped_flags, dropped_flags


@pytest.mark.object_versions_rent
@pytest.mark.xfail(strict=True, raises=AssertionError, reason="an offer to a mortal with no gold should not both quote a price and promise a lifetime of rent")
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, PENNILESS_MYSTIC)
def test_an_offer_to_a_mortal_without_gold_does_not_promise_a_lifetime_of_rent(server) -> None:
    """Pins that the inn's offer agrees with itself: a mortal with no gold who is quoted 6 silver
    and 88 copper a day is not told its gold lasts a lifetime.

    Bug: Crash_offer_rent (objsave.cpp:1638-1647) quotes the day's cost, then sets
    the cost to 0 before comparing it with the gold, so every offer ends with the lifetime line,
    whatever the gold; the rent itself is never charged (gen_receptionist). Either fix (no quote
    while rent is free, or a real charge that this mortal cannot pay) passes this pin. Nothing on
    this path draws.
    """
    implementor = server.login(IMPLEMENTOR.name)
    mystic = server.login(MORTAL_MYSTIC.name)
    give_new_object(implementor, PLAIN_SWORD_VNUM, SWORD_KEYWORD, SWORD_SHORT_DESCRIPTION, MORTAL_MYSTIC)
    teleport = implementor.command(f"teleport {MORTAL_MYSTIC.name.lower()} {world.ROOM_INN}")
    assert not teleport.contains(NO_ONE_MARKER), teleport.text
    arrival = mystic.expect([TELEPORTED_MARKER]) + mystic.drain(SETTLE_SECONDS)
    assert INNKEEPER_IN_ROOM_LINE in arrival, arrival

    mystic.send_line("offer")
    outcomes = [LIFETIME_OF_RENT_MARKER, NOT_ENOUGH_MONEY_MARKER, OFFER_QUOTE_MARKER, NOTHING_TO_STORE_MARKER, CANNOT_SEE_RENTER_MARKER, NOT_HERE_MARKER]
    reply = mystic.expect(outcomes) + mystic.drain(SETTLE_SECONDS)
    assert not (OFFER_QUOTE_MARKER in reply and LIFETIME_OF_RENT_MARKER in reply), reply
