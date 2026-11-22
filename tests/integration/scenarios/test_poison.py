"""Poison over game hours, and poisoned gear: tainted bread hurts a mortal once per granted game
hour for its duration, then wears off; the sickly amulet hurts whoever puts it on or takes it
off, and kills a wearer it leaves with no hit points, once."""

from __future__ import annotations

import re

import pytest

from rots_harness import world
from rots_harness.characters import IMPLEMENTOR, Character, StoredValues
from rots_harness.game_text import (
    CORPSE_CONTENTS_HEADER,
    EATEN_POISON_REPLY,
    NEW_BODY_MESSAGE,
    NOTHING_FOUND_REPLY,
    POISON_TICK,
    STOP_USING_FORMAT,
)
from rots_harness.immortal_commands import give_new_object, goto, load_mob, wizset
from rots_harness.replies import reply_to
from rots_harness.session import GameSession
from rots_harness.stat_reading import HIT_POINTS_UNSIGNED_MAXIMUM_PATTERN, read_stat
from rots_harness.text_lines import assert_in_order, lines_after

pytestmark = [pytest.mark.scenario, pytest.mark.poison]

# A level-20 human warrior in the arena: a mortal, since immortals are not poisoned by food.
MORTAL = Character(
    name="Testeater",
    race="human",
    sex="male",
    level=20,
    load_room=world.ROOM_ARENA_CENTRE,
    class_name="warrior",
)
# The tainted bread's (world.OBJECT_TAINTED_BREAD) fill value is 2 and food poison lasts twice its
# fill, in game hours.
POISON_HOURS = 4
# Damage the first poison tick does; regeneration cannot raise full hit points before it.
FIRST_TICK_DAMAGE = 5
# A hunger low enough that eating is allowed (above 20 is too full) and high enough not to
# reach 0 in this test.
HUNGER_BEFORE_EATING = 10
# What the mortal reads when the poison wears off.
POISON_WEARS_OFF_MARKER = "You feel less sick."
# Seconds to keep reading after an acknowledgement, for text already sent to arrive.
SETTLE_SECONDS = 1.0
# The score line with current and maximum hit points.
HIT_POINTS_PATTERN = re.compile(r"You have (-?\d+)/(\d+) hit,")


def hit_points(session: GameSession) -> tuple:
    """The character's current and maximum hit points, read from score."""
    score = session.command("score").text
    match = HIT_POINTS_PATTERN.search(score)
    assert match is not None, f"no hit-point line in score:\n{score}"
    return int(match.group(1)), int(match.group(2))


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, MORTAL)
def test_tainted_bread_poisons_a_mortal_once_per_game_hour_then_wears_off(server) -> None:
    implementor = server.login(IMPLEMENTOR.name)
    mortal = server.login(MORTAL.name)
    mortal_keyword = MORTAL.name.lower()

    current_hit, maximum_hit = hit_points(mortal)
    assert current_hit == maximum_hit, (
        f"expected full hit points before eating, got {current_hit}/{maximum_hit}"
    )

    wizset(implementor, mortal_keyword, "hunger", HUNGER_BEFORE_EATING)
    implementor.command(f"goto {mortal_keyword}")
    give_new_object(implementor, world.OBJECT_TAINTED_BREAD, "bread", mortal_keyword)
    assert mortal.command("eat bread").contains(EATEN_POISON_REPLY)

    first_hour = server.text_after_hours(mortal, expected_marker=POISON_TICK)
    assert first_hour.count(POISON_TICK) == 1, first_hour
    assert hit_points(mortal) == (maximum_hit - FIRST_TICK_DAMAGE, maximum_hit)

    for hour in range(2, POISON_HOURS + 1):
        during_hour = server.text_after_hours(mortal, expected_marker=POISON_TICK)
        assert during_hour.count(POISON_TICK) == 1, f"hour {hour}:\n{during_hour}"

    wearing_off = server.text_after_hours(mortal, expected_marker=POISON_WEARS_OFF_MARKER)
    assert POISON_TICK not in wearing_off, wearing_off

    after_poison = server.text_after_hours(mortal)
    assert POISON_TICK not in after_poison, after_poison
    assert POISON_WEARS_OFF_MARKER not in after_poison, after_poison


# The blow the amulet deals as it goes on or comes off (handler.cpp).
GEAR_POISON_BLOW = 5
# The constitution the amulet's wearers are stored with, so the stunned band below is known.
WEARER_CONSTITUTION = 15
# A level-20 human warrior in the arena centre with exactly the amulet's blow left in hit points.
# The amulet's wearers carry no coins, so a corpse holds only what the test gives.
LAST_BLOW_WEARER = Character(
    name="Testsickly",
    race="human",
    sex="male",
    level=20,
    load_room=world.ROOM_ARENA_CENTRE,
    class_name="warrior",
    hit=GEAR_POISON_BLOW,
    stored=StoredValues(con=WEARER_CONSTITUTION, gold=0),
)
# The same warrior with two blows left: the amulet going on leaves one, and taking it off the last.
TWO_BLOW_WEARER = Character(
    name="Testailing",
    race="human",
    sex="male",
    level=20,
    load_room=world.ROOM_ARENA_CENTRE,
    class_name="warrior",
    hit=2 * GEAR_POISON_BLOW,
    stored=StoredValues(con=WEARER_CONSTITUTION, gold=0),
)
# What the wearer reads as the amulet goes on (wear_message, act_obj2.cpp).
WEAR_AMULET_MARKER = f"You wear {world.OBJECT_SICKLY_AMULET_SHORT_DESCRIPTION} around your neck."
# What the wearer reads as the amulet comes off.
REMOVE_AMULET_MARKER = STOP_USING_FORMAT.format(world.OBJECT_SICKLY_AMULET_SHORT_DESCRIPTION)
# What the wearer reads when the blow leaves it at 0 hit points (damage, fight.cpp).
STUNNED_MARKER = "You're stunned, but will probably regain consciousness again."
# What the wearer reads when the blow kills it outright in damage() instead (damage, fight.cpp).
DEAD_MARKER = "You are dead!  Sorry..."
# What the wearer and the room read as the gear's poison kills it (handler.cpp); format the
# room's line with the wearer as the room sees it.
BODY_FAILED_MARKER = "Your body failed to the magic."
COLLAPSES_FORMAT = "{} suddenly collapses on the ground."
# The start of every corpse's room line (get_corpse_desc, fight.cpp).
CORPSE_LINE_MARKER = "corpse of"
# The stat lines the stunned-band margin rests on (do_stat_character, act_wiz.cpp).
CURRENT_CONSTITUTION_PATTERN = re.compile(r"Con:\s*\[(-?\d+)/(-?\d+)/(-?\d+)\]")
NO_RESISTANCES_LINE = "RES: <NONE> VUL: <NONE>"


def stat_hit_constitution_and_resists_nothing(
    implementor: GameSession, target: str
) -> tuple[int, int, bool]:
    """Current hit points and current constitution from the reply to `stat <target>`, and whether
    it shows no resistance or vulnerability."""
    text = read_stat(implementor, target)
    hit = HIT_POINTS_UNSIGNED_MAXIMUM_PATTERN.search(text)
    constitution = CURRENT_CONSTITUTION_PATTERN.search(text)
    assert hit and constitution, text
    return int(hit.group(1)), int(constitution.group(1)), NO_RESISTANCES_LINE in text


def assert_blow_leaves_zero(hit_before: int, constitution: int, resists_nothing: bool) -> None:
    """Asserts the amulet's blow leaves exactly 0 hit points and a stunned, not dead, position.

    Margin check (damage and update_pos, fight.cpp), for a wearer at GEAR_POISON_BLOW hit
    points with constitution WEARER_CONSTITUTION (the porter's is the same band, see its test):
      dam        = 5, unchanged: check_resistances gives 0 (RES: <NONE> VUL: <NONE>), and no
                   shield, beorning or fame-war term applies to a plain human or mob
      GET_HIT    = 5 - 5 = 0; the player-attacker hack (`GET_HIT < 0` sets -20) needs below 0
      update_pos = 0 > 0 no; 0 <= -GET_CON / 2 = -15 / 2 = -7 no; 0 <= -GET_CON / 4 = -3 no:
                   STUNNED, so damage() kills nobody and deal_gear_poison_blow's `GET_HIT <= 0`
                   collapse kills the wearer. The blow draws number(0, 2) for physical
                   resistance, which a poison blow never uses.
    """
    assert resists_nothing
    hit_after = hit_before - GEAR_POISON_BLOW
    assert hit_after == 0
    assert not hit_after <= -(constitution // 2)
    assert not hit_after <= -(constitution // 4)


def corpse_lines(session: GameSession) -> list:
    """The corpse lines of the session's `look`."""
    looked = session.command("look").text
    lines = looked.splitlines()
    return [line.strip() for line in lines if CORPSE_LINE_MARKER in line]


def corpse_contents(session: GameSession) -> list:
    """The lines `look in corpse` lists after its header, up to the prompt."""
    looked = session.command("look in corpse").text
    return lines_after(looked, CORPSE_CONTENTS_HEADER)


def hand_amulet_to(implementor: GameSession, wearer: Character) -> None:
    """The implementor goes to the wearer, creates the amulet and gives it."""
    goto(implementor, wearer.name.lower(), world.ROOM_ARENA_CENTRE_NAME, exact=False)
    give_new_object(implementor, world.OBJECT_SICKLY_AMULET, "amulet", wearer.name.lower(), shown_recipient=wearer.name, short_description=world.OBJECT_SICKLY_AMULET_SHORT_DESCRIPTION)


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, LAST_BLOW_WEARER)
def test_a_poisoned_amulet_put_on_with_the_last_hit_points_kills_its_wearer_once(server) -> None:
    """Fix pin (#539): putting on the amulet with exactly its blow left kills the wearer once.

    equip_char's blow (deal_gear_poison_blow, handler.cpp) leaves the wearer at 0 hit points,
    stunned, and the collapse kills it (raw_kill, fight.cpp). raw_kill marks the wearer dead
    before the corpse takes its gear, and unequip_char spares the dead. #539 made the wearer die
    once; before it, the corpse taking the amulet off dealt a second blow to the dying wearer,
    who died again with a second corpse and the death line of damage(). The wearer wakes in its
    start room with 1 hit point (a death with no killer and no poison attack type, raw_kill), and
    the one corpse holds the amulet. No roll decides any of this (assert_blow_leaves_zero).
    """
    implementor = server.login(IMPLEMENTOR.name)
    wearer = server.login(LAST_BLOW_WEARER.name)
    hand_amulet_to(implementor, LAST_BLOW_WEARER)
    stat_values = stat_hit_constitution_and_resists_nothing(implementor, f"player {LAST_BLOW_WEARER.name.lower()}")
    assert_blow_leaves_zero(*stat_values)

    died = reply_to(wearer, "wear amulet", [NEW_BODY_MESSAGE])
    expected_order = [WEAR_AMULET_MARKER, STUNNED_MARKER, BODY_FAILED_MARKER, NEW_BODY_MESSAGE, world.ROOM_HUMAN_START_NAME]
    assert_in_order(died, expected_order)
    assert DEAD_MARKER not in died, died
    seen = implementor.expect([COLLAPSES_FORMAT.format(LAST_BLOW_WEARER.name)]) + implementor.drain(SETTLE_SECONDS)
    assert seen.count(COLLAPSES_FORMAT.format(LAST_BLOW_WEARER.name)) == 1, seen

    corpses = corpse_lines(implementor)
    assert len(corpses) == 1, corpses
    assert corpse_contents(implementor) == [world.OBJECT_SICKLY_AMULET_SHORT_DESCRIPTION]
    assert stat_hit_constitution_and_resists_nothing(implementor, f"player {LAST_BLOW_WEARER.name.lower()}")[0] == 1


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, TWO_BLOW_WEARER)
def test_a_poisoned_amulet_taken_off_with_the_last_hit_points_kills_its_wearer_who_keeps_it(server) -> None:
    """Legacy pin: taking off the amulet with exactly its blow left kills the wearer, who wakes
    in its start room holding the amulet.

    The amulet going on costs one blow, leaving one. Taking it off deals the last
    (unequip_char, handler.cpp) before the remove command has put it anywhere: the collapse
    kills the wearer (raw_kill, fight.cpp), whose corpse takes nothing of the amulet, and only
    then does the remove put it in the inventory of the wearer, now in its start room, and say
    so. The wearer wakes with 1 hit point. No roll decides any of this (assert_blow_leaves_zero).
    """
    implementor = server.login(IMPLEMENTOR.name)
    wearer = server.login(TWO_BLOW_WEARER.name)
    hand_amulet_to(implementor, TWO_BLOW_WEARER)
    worn = wearer.command("wear amulet")
    assert worn.contains(WEAR_AMULET_MARKER), worn.text
    assert BODY_FAILED_MARKER not in worn.text, worn.text
    stat_values = stat_hit_constitution_and_resists_nothing(implementor, f"player {TWO_BLOW_WEARER.name.lower()}")
    assert_blow_leaves_zero(*stat_values)

    died = reply_to(wearer, "remove amulet", [REMOVE_AMULET_MARKER])
    expected_order = [STUNNED_MARKER, BODY_FAILED_MARKER, NEW_BODY_MESSAGE, world.ROOM_HUMAN_START_NAME, REMOVE_AMULET_MARKER]
    assert_in_order(died, expected_order)
    assert DEAD_MARKER not in died, died
    seen = implementor.expect([COLLAPSES_FORMAT.format(TWO_BLOW_WEARER.name)]) + implementor.drain(SETTLE_SECONDS)
    assert seen.count(COLLAPSES_FORMAT.format(TWO_BLOW_WEARER.name)) == 1, seen

    carried = wearer.command("inventory").text
    assert world.OBJECT_SICKLY_AMULET_SHORT_DESCRIPTION in carried, carried
    assert wearer.command("look").room_name() == world.ROOM_HUMAN_START_NAME
    corpses = corpse_lines(implementor)
    assert len(corpses) == 1, corpses
    assert corpse_contents(implementor) == []
    assert stat_hit_constitution_and_resists_nothing(implementor, f"player {TWO_BLOW_WEARER.name.lower()}")[0] == 1


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR)
def test_a_mob_made_to_put_on_a_poisoned_amulet_with_the_last_hit_points_dies_once(server) -> None:
    """Fix pin (#539, #547): a mob the amulet kills as it goes on dies once, and its corpse holds it.

    The porter, forced to wear the amulet at exactly its blow, is stunned at 0 hit points and the
    collapse kills it (deal_gear_poison_blow, handler.cpp; raw_kill frees a mob). The one corpse
    holds the amulet and the server stays up to show it. #539 made the mob die once; before it,
    the corpse taking the amulet off dealt a second blow that killed the mob again inside the
    first death, freeing it while the first death still used it. No roll decides any of this
    (assert_blow_leaves_zero, with the porter's own constitution read from stat).
    """
    implementor = server.login(IMPLEMENTOR.name)
    goto(implementor, world.ROOM_ARENA_CENTRE, world.ROOM_ARENA_CENTRE_NAME, exact=False)
    load_mob(implementor, world.MOB_PORTER, "a porter")
    give_new_object(implementor, world.OBJECT_SICKLY_AMULET, "amulet", "porter", shown_recipient="a porter", short_description=world.OBJECT_SICKLY_AMULET_SHORT_DESCRIPTION)
    wizset(implementor, "mob porter", "hit", GEAR_POISON_BLOW)
    hit, constitution, resists_nothing = stat_hit_constitution_and_resists_nothing(implementor, "mob porter")
    # The porter's constitution is its own (mob/17.mob), not WEARER_CONSTITUTION: read and assert
    # it, so the stunned band in assert_blow_leaves_zero holds for it too.
    assert constitution >= 4, constitution
    assert_blow_leaves_zero(hit, constitution, resists_nothing)

    forced = implementor.command("force porter wear amulet").text + implementor.drain(SETTLE_SECONDS)
    expected_order = [f"A porter wears {world.OBJECT_SICKLY_AMULET_SHORT_DESCRIPTION} around his neck.", COLLAPSES_FORMAT.format("A porter")]
    assert_in_order(forced, expected_order)
    assert "is dead!  R.I.P." not in forced, forced

    corpses = corpse_lines(implementor)
    assert len(corpses) == 1, corpses
    assert corpse_contents(implementor) == [world.OBJECT_SICKLY_AMULET_SHORT_DESCRIPTION]
    porters = implementor.command("where porter").text
    assert NOTHING_FOUND_REPLY in porters, porters
