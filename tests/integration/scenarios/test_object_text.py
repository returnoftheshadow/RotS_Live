"""What objects made at run time are called, and `stat room`'s object numbers beside a corpse.

The objects are a slain mob's corpse, the same corpse beheaded and its head, a pile of dropped
coins, and a flask filled with ale beside an empty one of the same kind, before and after a quit
and login.
"""

from __future__ import annotations

import dataclasses

import pytest

from rots_harness import world
from rots_harness.characters import IMPLEMENTOR, StoredValues
from rots_harness.game_text import DRINK_EMPTY_REPLY, DROP_FORMAT
from rots_harness.immortal_commands import goto, load_mob, load_object, wizset
from rots_harness.session import GameSession
from rots_harness.stat_reading import read_stat
from rots_harness.text_lines import stripped_lines
from rots_harness.world_resets import ObjectReset

pytestmark = [pytest.mark.scenario, pytest.mark.object_text]

# The dummy yard (tests/integration/world/wld/24.wld), where the implementor works.
ROOM_DUMMY_YARD = 2407
# The name look shows for ROOM_DUMMY_YARD.
ROOM_DUMMY_YARD_NAME = "Dummy Yard"
# The practice dummy's keyword (world.MOB_PRACTICE_DUMMY): level 1, bodytype 1 (humanoid), so its
# corpse has a head (load_scalp, act_obj1.cpp, gives none for bodytype 0); no gold, so its corpse
# holds no coins.
DUMMY_KEYWORD = "dummy"
# The practice dummy's short description.
DUMMY_SHORT_DESCRIPTION = "a practice dummy"
# The keyword of the plain steel sword (world.OBJECT_PLAIN_STEEL_SWORD), a slashing weapon, which
# behead needs.
SWORD_KEYWORD = "sword"
# The cask of ale beside the flasks (world.OBJECT_FLASK, an empty drink container holding 20): a
# fountain of 1000 drinks of ale, liquid 3, placed on the yard's floor. The cask's keywords are
# "cask oak", so "ale" on the floor finds nothing: do_drink looks on the floor when the inventory
# has no match.
CASK_VNUM = 2401
# The cask's short description.
CASK_SHORT_DESCRIPTION = "a cask of ale"

# The corpse's short description (get_corpse_desc, fight.cpp): slay kills through raw_kill with
# attack type 0, which reads "silent"; the rest is the mob's short description.
CORPSE_SHORT_DESCRIPTION = "the silent corpse of a practice dummy"
# The corpse's line on the floor (get_corpse_desc, fight.cpp).
CORPSE_LINE = "The silent corpse of a practice dummy is lying here."
# What behead answers (do_butcher, act_obj1.cpp, SCMD_SCALP; load_scalp): no draw on this path,
# unlike butcher's number(1, 100).
BEHEADED_REPLY = f"You behead {CORPSE_SHORT_DESCRIPTION}."
# The beheaded corpse's line on the floor (load_scalp, act_obj1.cpp).
BEHEADED_CORPSE_LINE = "The silent corpse of a practice dummy is lying here, its head missing."
# The head's short description (load_scalp, act_obj1.cpp).
HEAD_SHORT_DESCRIPTION = "A severed head of a practice dummy"
# The head's line on the floor (load_scalp, act_obj1.cpp).
HEAD_LINE = "A severed head of a practice dummy is lying here."
# What dropping the head answers.
HEAD_DROPPED_REPLY = DROP_FORMAT.format(HEAD_SHORT_DESCRIPTION)
# What behead answers for a corpse already beheaded (do_butcher, act_obj1.cpp).
ALREADY_BEHEADED_REPLY = "It was beheaded already."
# What behead answers without a slashing or cleaving weapon wielded (do_butcher, act_obj1.cpp).
NEED_WEAPON_REPLY = "You need a slashing or cleaving weapon to do that."

# Coins the implementor drops (create_money, handler.cpp).
COINS_DROPPED = 5
# The dropped coins' line on the floor (create_money, handler.cpp).
COINS_LINE = "A small pile of coins is lying here."
# What looking at the dropped coins shows (create_money, handler.cpp).
COINS_LOOKED_AT = f"There are {COINS_DROPPED} copper coins."

# The implementor, stored with drunk and thirst at 0: do_drink refuses a drinker whose drunk is
# above 10 while thirst is above 0 ("You can't seem to get close enough to your mouth."), which is
# how the shared IMPLEMENTOR logs in.
SOBER_IMPLEMENTOR = dataclasses.replace(IMPLEMENTOR, stored=StoredValues(drunk=0, thirst=0))
# What pouring ale into the flask answers (do_pour, act_obj2.cpp). Pouring into an empty flask
# puts the liquid's keyword in front of its name (name_to_drinkcon).
POURED_REPLY = "You pour some ale into the flask."
# What drinking ale answers (do_drink, act_obj2.cpp): ale (drink_aff DRUNK 2) takes
# (25 - 0) / 2 = 12 of the flask's 20 with no draw, so 8 are left.
DRANK_REPLY = "You drink the ale."
# What drink answers when nothing carried or on the floor has the name (do_drink, act_obj2.cpp).
NOT_FOUND_REPLY = "You can't find it!"
# What `look in` prints for an empty drink container (do_look, act_info.cpp).
LOOK_IN_EMPTY = "It is empty."
# The start of `look in`'s line for a full one ("It's <fullness>full of a <colour> liquid.").
LOOK_IN_FULL_START = "It's "

# What `stat room` (do_stat_room, act_wiz.cpp) prints before the floor's objects.
CONTENTS_HEADER = "Contents:"
# The cask as `stat room` lists it, with the number it shows after an object made from a
# prototype.
CASK_WITH_NUMBER = f"{CASK_SHORT_DESCRIPTION} [{CASK_VNUM}]"


def go_to_yard(implementor: GameSession) -> None:
    """Move the implementor to the dummy yard."""
    goto(implementor, ROOM_DUMMY_YARD, ROOM_DUMMY_YARD_NAME)


def slay_a_dummy(implementor: GameSession) -> None:
    """Load a practice dummy beside the implementor and slay it."""
    load_mob(implementor, world.MOB_PRACTICE_DUMMY, DUMMY_SHORT_DESCRIPTION)
    slain = implementor.command(f"slay {DUMMY_KEYWORD}")
    assert slain.contains("You chop"), slain.text


def floor_lines(implementor: GameSession) -> list[str]:
    """Return the lines look shows the implementor in its room."""
    looked = implementor.command("look").text
    return stripped_lines(looked)


def drink(implementor: GameSession, target: str) -> str:
    """Return the outcome line drinking from the target prints.

    The outcome is DRANK_REPLY, NOT_FOUND_REPLY or DRINK_EMPTY_REPLY.
    """
    reply = implementor.command(f"drink {target}").text
    reply_lines = stripped_lines(reply)
    outcomes = [line for line in reply_lines if line in (DRANK_REPLY, NOT_FOUND_REPLY, DRINK_EMPTY_REPLY)]
    assert outcomes, reply
    return outcomes[0]


def is_contents_line(stat_line: str) -> bool:
    """Return whether a `stat room` line is the contents header or names the corpse or the cask."""
    return CONTENTS_HEADER in stat_line or CORPSE_SHORT_DESCRIPTION in stat_line or CASK_SHORT_DESCRIPTION in stat_line


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR)
def test_a_slain_mobs_corpse_and_its_head_read_as_before(server) -> None:
    """Pins the lines of a slain practice dummy's corpse, the corpse beheaded and its head.

    The corpse lies as `The silent corpse of a practice dummy is lying here.`; beheaded with a
    slashing weapon, the corpse's line ends `, its head missing.` and the head is `A severed head
    of a practice dummy`, in the inventory and on the floor. A second behead is refused. Without a
    weapon, behead says what it needs. Nothing draws.
    """
    implementor = server.login(IMPLEMENTOR.name)
    go_to_yard(implementor)
    slay_a_dummy(implementor)

    floor = floor_lines(implementor)
    assert CORPSE_LINE in floor, floor
    unarmed = implementor.command("behead corpse").text
    assert NEED_WEAPON_REPLY in stripped_lines(unarmed), unarmed
    load_object(implementor, world.OBJECT_PLAIN_STEEL_SWORD)
    wielded = implementor.command(f"wield {SWORD_KEYWORD}")
    assert wielded.contains("You wield"), wielded.text
    beheaded = implementor.command("behead corpse").text
    assert BEHEADED_REPLY in stripped_lines(beheaded), beheaded

    after = floor_lines(implementor)
    assert BEHEADED_CORPSE_LINE in after, after
    assert CORPSE_LINE not in after, after
    carried = implementor.command("inventory").text
    assert HEAD_SHORT_DESCRIPTION in stripped_lines(carried), carried
    again = implementor.command("behead corpse").text
    assert ALREADY_BEHEADED_REPLY in stripped_lines(again), again
    dropped = implementor.command("drop head").text
    assert HEAD_DROPPED_REPLY in stripped_lines(dropped), dropped
    floor_after_drop = floor_lines(implementor)
    assert HEAD_LINE in floor_after_drop, floor_after_drop


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR)
def test_dropped_coins_lie_as_a_small_pile_and_look_counts_them(server) -> None:
    """Pins the dropped coins' line and what looking at them shows.

    Five copper coins dropped lie as `A small pile of coins is lying here.`, and looking at them
    reads `There are 5 copper coins.` Nothing draws.
    """
    implementor = server.login(IMPLEMENTOR.name)
    go_to_yard(implementor)
    wizset(implementor, f"player {IMPLEMENTOR.name.lower()}", "gold", COINS_DROPPED)

    dropped = implementor.command(f"drop {COINS_DROPPED} copper").text
    assert "You drop some money." in stripped_lines(dropped), dropped
    floor = floor_lines(implementor)
    assert COINS_LINE in floor, floor
    looked = implementor.command("look coins").text
    assert COINS_LOOKED_AT in stripped_lines(looked), looked


@pytest.mark.schedule("affect-hours")
@pytest.mark.world_resets(ObjectReset(CASK_VNUM, ROOM_DUMMY_YARD))
@pytest.mark.characters(SOBER_IMPLEMENTOR)
def test_a_filled_flask_answers_to_its_liquid_and_its_twin_does_not(server) -> None:
    """Pins that of two flasks of one kind, only the one filled with ale answers to `ale`.

    A flask's new name is its own, not its kind's. After a quit and login the filled flask keeps
    its ale but has its plain name again (Crash_obj2char, objsave.cpp, restores the values but not
    the name), so neither answers to `ale`. Nothing draws.
    """
    implementor = server.login(SOBER_IMPLEMENTOR.name)
    go_to_yard(implementor)
    for _flask in range(2):
        load_object(implementor, world.OBJECT_FLASK, "a flask")

    poured = implementor.command("pour cask flask").text
    assert POURED_REPLY in stripped_lines(poured), poured
    first_drink = drink(implementor, "ale")
    assert first_drink == DRANK_REPLY, first_drink
    second_ale = drink(implementor, "2.ale")
    assert second_ale == NOT_FOUND_REPLY, second_ale
    second_flask = drink(implementor, "2.flask")
    assert second_flask == DRINK_EMPTY_REPLY, second_flask

    again = server.relog(implementor)
    relogged_drink = drink(again, "ale")
    assert relogged_drink == NOT_FOUND_REPLY, relogged_drink
    contents = [again.command(f"look in {ordinal}.flask").text for ordinal in (1, 2)]
    full: list[str] = []
    for text in contents:
        looked_lines = stripped_lines(text)
        if any(line.startswith(LOOK_IN_FULL_START) for line in looked_lines):
            full.append(text)

    empty = [text for text in contents if LOOK_IN_EMPTY in stripped_lines(text)]
    assert (len(full), len(empty)) == (1, 1), contents


@pytest.mark.schedule("affect-hours")
@pytest.mark.world_resets(ObjectReset(CASK_VNUM, ROOM_DUMMY_YARD))
@pytest.mark.characters(IMPLEMENTOR)
def test_stat_room_shows_no_number_after_a_corpse(server) -> None:
    """Fix pin (#534): `stat room` shows a prototype's object with its number, a corpse with none.

    A corpse has no prototype. #534 made the corpse show no number; before it, the number was read
    from before the object table (obj_index[-1]), so some number always followed it. Nothing
    draws.
    """
    implementor = server.login(IMPLEMENTOR.name)
    go_to_yard(implementor)
    slay_a_dummy(implementor)

    stat = read_stat(implementor, "room", last_line=CONTENTS_HEADER)
    stat_lines = stat.splitlines()
    contents = " ".join(line for line in stat_lines if is_contents_line(line))
    assert CASK_WITH_NUMBER in contents, stat
    assert CORPSE_SHORT_DESCRIPTION in contents, stat
    assert f"{CORPSE_SHORT_DESCRIPTION} [" not in contents, stat
