"""What objects made at run time are called: a slain mob's corpse, the same corpse beheaded and its
head, a pile of dropped coins, and a flask filled with ale beside an empty one of the same kind,
before and after a quit and login; and `stat room`'s object numbers beside a corpse."""

from __future__ import annotations

import dataclasses

import pytest

from rots_harness import world
from rots_harness.characters import IMPLEMENTOR, StoredValues
from rots_harness.session import GameSession
from rots_harness.stat_reading import read_stat
from rots_harness.text_lines import stripped_lines
from rots_harness.world_resets import ObjectReset

pytestmark = [pytest.mark.scenario, pytest.mark.object_text]

# The dummy yard (tests/integration/world/wld/24.wld), where the implementor works, and what look
# names it.
ROOM_DUMMY_YARD = 2407
DUMMY_YARD_NAME = "Dummy Yard"
# The practice dummy (mob/24.mob): level 1, bodytype 1 (humanoid), so its corpse has a head
# (load_scalp, act_obj1.cpp, gives none for bodytype 0); no gold, so its corpse holds no coins.
DUMMY_VNUM = 2404
DUMMY_KEYWORD = "dummy"
DUMMY_CREATED = "You create a practice dummy."
# The keyword of the plain steel sword (world.OBJECT_PLAIN_STEEL_SWORD), a slashing weapon, which
# behead needs.
SWORD_KEYWORD = "sword"
# The flask (world.OBJECT_FLASK, an empty drink container holding 20) and the cask of ale (a
# fountain of 1000 drinks of ale, liquid 3, placed on the yard's floor), with the cask's vnum as
# stat room shows it. The cask's keywords are "cask oak", so "ale" on the floor finds nothing:
# do_drink looks on the floor when the inventory has no match.
CASK_VNUM = 2401
CASK_SHORT = "a cask of ale"

# The corpse's lines (get_corpse_desc, fight.cpp): slay kills through raw_kill with attack type
# 0, which reads "silent"; the short description is the mob's.
CORPSE_SHORT = "the silent corpse of a practice dummy"
CORPSE_LINE = "The silent corpse of a practice dummy is lying here."
# What behead prints and leaves (do_butcher, act_obj1.cpp, SCMD_SCALP; load_scalp): no draw on
# this path, unlike butcher's number(1, 100).
BEHEADED_MARKER = f"You behead {CORPSE_SHORT}."
BEHEADED_CORPSE_LINE = "The silent corpse of a practice dummy is lying here, its head missing."
HEAD_SHORT = "A severed head of a practice dummy"
HEAD_LINE = "A severed head of a practice dummy is lying here."
HEAD_DROPPED = f"You drop {HEAD_SHORT}."
ALREADY_BEHEADED = "It was beheaded already."
NEED_WEAPON = "You need a slashing or cleaving weapon to do that."

# Coins the implementor drops, and their lines (create_money, handler.cpp).
COINS_DROPPED = 5
COINS_LINE = "A small pile of coins is lying here."
COINS_LOOKED_AT = f"There are {COINS_DROPPED} copper coins."

# The implementor, stored with drunk and thirst at 0: do_drink refuses a drinker whose drunk is
# above 10 while thirst is above 0 ("You can't seem to get close enough to your mouth."), which is
# how the shared IMPLEMENTOR logs in.
SOBER_IMPLEMENTOR = dataclasses.replace(IMPLEMENTOR, stored=StoredValues(drunk=0, thirst=0))
# What pouring and drinking print (do_pour, do_drink, act_obj2.cpp). Pouring into an empty flask
# puts the liquid's keyword in front of its name (name_to_drinkcon); drinking ale (drink_aff
# DRUNK 2) takes (25 - 0) / 2 = 12 of the flask's 20 with no draw, so 8 are left.
POURED = "You pour some ale into the flask."
DRANK = "You drink the ale."
NOT_FOUND = "You can't find it!"
EMPTY = "It's empty."
# What `look in` prints for a drink container (do_look, act_info.cpp): an empty one, and the
# start of a full one's line ("It's <fullness>full of a <colour> liquid.").
LOOK_IN_EMPTY = "It is empty."
LOOK_IN_FULL_START = "It's "

# What `stat room` (do_stat_room, act_wiz.cpp) prints before the floor's objects, and the
# number it shows after an object made from a prototype.
CONTENTS_HEADER = "Contents:"
CASK_WITH_NUMBER = f"{CASK_SHORT} [{CASK_VNUM}]"


def go_to_yard(implementor: GameSession) -> None:
    """Moves the implementor to the dummy yard."""
    went = implementor.command(f"goto {ROOM_DUMMY_YARD}")
    assert went.room_name() == DUMMY_YARD_NAME, went.text


def slay_a_dummy(implementor: GameSession) -> None:
    """Loads a practice dummy beside the implementor and slays it."""
    created = implementor.command(f"load mob {DUMMY_VNUM}")
    assert created.contains(DUMMY_CREATED), created.text
    slain = implementor.command(f"slay {DUMMY_KEYWORD}")
    assert slain.contains("You chop"), slain.text


def floor_lines(implementor: GameSession) -> list:
    """The lines look shows the implementor in its room."""
    looked = implementor.command("look").text
    return stripped_lines(looked)


def drink(implementor: GameSession, target: str) -> str:
    """The outcome line drinking from the target prints: DRANK, NOT_FOUND or EMPTY."""
    reply = implementor.command(f"drink {target}").text
    outcomes = [line for line in stripped_lines(reply) if line in (DRANK, NOT_FOUND, EMPTY)]
    assert outcomes, reply
    return outcomes[0]


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR)
def test_a_slain_mobs_corpse_and_its_head_read_as_before(server) -> None:
    """A slain practice dummy leaves `The silent corpse of a practice dummy is lying here.`;
    beheaded with a slashing weapon, the corpse's line ends `, its head missing.` and the head is
    `A severed head of a practice dummy`, in the inventory and on the floor. A second behead is
    refused. Without a weapon, behead says what it needs. Nothing draws."""
    implementor = server.login(IMPLEMENTOR.name)
    go_to_yard(implementor)
    slay_a_dummy(implementor)

    assert CORPSE_LINE in floor_lines(implementor)
    unarmed = implementor.command("behead corpse").text
    assert NEED_WEAPON in stripped_lines(unarmed), unarmed
    assert implementor.command(f"load obj {world.OBJECT_PLAIN_STEEL_SWORD}").contains("You create")
    assert implementor.command(f"wield {SWORD_KEYWORD}").contains("You wield")
    beheaded = implementor.command("behead corpse").text
    assert BEHEADED_MARKER in stripped_lines(beheaded), beheaded

    after = floor_lines(implementor)
    assert BEHEADED_CORPSE_LINE in after, after
    assert CORPSE_LINE not in after, after
    carried = implementor.command("inventory").text
    assert HEAD_SHORT in stripped_lines(carried), carried
    again = implementor.command("behead corpse").text
    assert ALREADY_BEHEADED in stripped_lines(again), again
    dropped = implementor.command("drop head").text
    assert HEAD_DROPPED in stripped_lines(dropped), dropped
    assert HEAD_LINE in floor_lines(implementor)


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR)
def test_dropped_coins_lie_as_a_small_pile_and_look_counts_them(server) -> None:
    """Five copper coins dropped lie as `A small pile of coins is lying here.`, and looking at
    them reads `There are 5 copper coins.` Nothing draws."""
    implementor = server.login(IMPLEMENTOR.name)
    go_to_yard(implementor)
    given = implementor.command(f"wizset player {IMPLEMENTOR.name.lower()} gold {COINS_DROPPED}")
    assert given.contains(f"gold set to {COINS_DROPPED}."), given.text

    dropped = implementor.command(f"drop {COINS_DROPPED} copper").text
    assert "You drop some money." in stripped_lines(dropped), dropped
    assert COINS_LINE in floor_lines(implementor)
    looked = implementor.command("look coins").text
    assert COINS_LOOKED_AT in stripped_lines(looked), looked


@pytest.mark.schedule("affect-hours")
@pytest.mark.world_resets(ObjectReset(CASK_VNUM, ROOM_DUMMY_YARD))
@pytest.mark.characters(SOBER_IMPLEMENTOR)
def test_a_filled_flask_answers_to_its_liquid_and_its_twin_does_not(server) -> None:
    """Of two flasks of one kind, the one filled with ale answers to `ale` and the other does not:
    a flask's new name is its own, not its kind's. After a quit and login the filled flask keeps
    its ale but has its plain name again (Crash_obj2char, objsave.cpp, restores the values but not
    the name), so neither answers to `ale`. Nothing draws."""
    implementor = server.login(SOBER_IMPLEMENTOR.name)
    go_to_yard(implementor)
    for _flask in range(2):
        assert implementor.command(f"load obj {world.OBJECT_FLASK}").contains("You create a flask.")

    poured = implementor.command("pour cask flask").text
    assert POURED in stripped_lines(poured), poured
    assert drink(implementor, "ale") == DRANK
    assert drink(implementor, "2.ale") == NOT_FOUND
    assert drink(implementor, "2.flask") == EMPTY

    implementor.quit()
    again = server.login(SOBER_IMPLEMENTOR.name)
    assert drink(again, "ale") == NOT_FOUND
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
    """Fix pin (#534): `stat room` shows an object made from a prototype with its number and a
    corpse, which has no prototype, with none. #534 made the corpse show no number; before it,
    the number was read from before the object table (obj_index[-1]), so some number always
    followed it. Nothing draws."""
    implementor = server.login(IMPLEMENTOR.name)
    go_to_yard(implementor)
    slay_a_dummy(implementor)

    stat = read_stat(implementor, "room", last_line=CONTENTS_HEADER)
    contents = " ".join(line for line in stat.splitlines() if CONTENTS_HEADER in line or CORPSE_SHORT in line or CASK_SHORT in line)
    assert CASK_WITH_NUMBER in contents, stat
    assert CORPSE_SHORT in contents, stat
    assert f"{CORPSE_SHORT} [" not in contents, stat
