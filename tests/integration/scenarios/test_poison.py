"""Poison over game hours, and poisoned gear: tainted bread hurts a mortal once per granted game
hour for its duration, then wears off; the sickly amulet hurts whoever puts it on or takes it
off, and kills a wearer it leaves with no hit points, once."""

from __future__ import annotations

import re

import pytest

from rots_harness import world
from rots_harness.characters import IMPLEMENTOR, Character, StoredValues
from rots_harness.replies import reply_to
from rots_harness.session import GameSession
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
# The tainted bread's virtual number in the test world (tests/integration/world/obj/11.obj).
TAINTED_BREAD_VNUM = 1139
# The bread's fill value is 2 and food poison lasts twice its fill, in game hours.
POISON_HOURS = 4
# Damage the first poison tick does; regeneration cannot raise full hit points before it.
FIRST_TICK_DAMAGE = 5
# A hunger low enough that eating is allowed (above 20 is too full) and high enough not to
# reach 0 in this test.
HUNGER_BEFORE_EATING = 10
# What the mortal reads after eating the bread.
EATEN_POISON_MARKER = "Oops, that tasted rather strange!"
# What the mortal reads on each poison tick.
POISON_TICK_MARKER = "You writhe in pain as poison courses through your veins."
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


def text_after_an_hour(server, session: GameSession, expected_marker: str = "") -> str:
    """Advances one game hour and returns what the session read during it."""
    server.advance_hours(1)
    if expected_marker:
        return session.expect([expected_marker]) + session.drain(SETTLE_SECONDS)
    return session.drain(SETTLE_SECONDS)


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

    set_hunger = implementor.command(f"wizset {mortal_keyword} hunger {HUNGER_BEFORE_EATING}")
    assert set_hunger.contains(f"hunger set to {HUNGER_BEFORE_EATING}."), set_hunger.text
    implementor.command(f"goto {mortal_keyword}")
    assert implementor.command(f"load obj {TAINTED_BREAD_VNUM}").contains("You create")
    assert implementor.command(f"give bread {mortal_keyword}").contains("You give")
    assert mortal.command("eat bread").contains(EATEN_POISON_MARKER)

    first_hour = text_after_an_hour(server, mortal, POISON_TICK_MARKER)
    assert first_hour.count(POISON_TICK_MARKER) == 1, first_hour
    assert hit_points(mortal) == (maximum_hit - FIRST_TICK_DAMAGE, maximum_hit)

    for hour in range(2, POISON_HOURS + 1):
        during_hour = text_after_an_hour(server, mortal, POISON_TICK_MARKER)
        assert during_hour.count(POISON_TICK_MARKER) == 1, f"hour {hour}:\n{during_hour}"

    wearing_off = text_after_an_hour(server, mortal, POISON_WEARS_OFF_MARKER)
    assert POISON_TICK_MARKER not in wearing_off, wearing_off

    after_poison = text_after_an_hour(server, mortal)
    assert POISON_TICK_MARKER not in after_poison, after_poison
    assert POISON_WEARS_OFF_MARKER not in after_poison, after_poison


# The sickly amulet (object 1138, obj/11.obj): worn on the neck, its APPLY_BITVECTOR affect sets
# AFF_POISON, so putting it on or taking it off deals its wearer a poison blow (GEAR_POISON_BLOW;
# deal_gear_poison_blow and unequip_char, handler.cpp).
AMULET_VNUM = 1138
AMULET = "a sickly amulet"
# The blow the amulet deals as it goes on or comes off (handler.cpp).
GEAR_POISON_BLOW = 5
# The name look shows for the arena centre, where the amulet's wearers and the porter stand.
ARENA_CENTRE_NAME = "Arena Centre"
# The porter (mob 1722, mob/17.mob): no script and no special, for the implementor to force.
PORTER_VNUM = 1722
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
WEAR_AMULET_MARKER = f"You wear {AMULET} around your neck."
# What the wearer reads as the amulet comes off (take_off_item, act_obj2.cpp).
REMOVE_AMULET_MARKER = f"You stop using {AMULET}."
# What the wearer reads when the blow leaves it at 0 hit points (damage, fight.cpp).
STUNNED_MARKER = "You're stunned, but will probably regain consciousness again."
# What the wearer reads when the blow kills it outright in damage() instead (damage, fight.cpp).
DEAD_MARKER = "You are dead!  Sorry..."
# What the wearer and the room read as the gear's poison kills it (handler.cpp); format the
# room's line with the wearer as the room sees it.
BODY_FAILED_MARKER = "Your body failed to the magic."
COLLAPSES_FORMAT = "{} suddenly collapses on the ground."
# What a player reads after dying, before it looks at its start room (extract_char, handler.cpp).
NEW_BODY_MARKER = "Your spirit found a new body to wear."
# Where a human mortal's spirit wakes after death (r_mortal_start_room, raw_kill in fight.cpp).
HUMAN_START_ROOM_NAME = "Human Start"
# The start of every corpse's room line (get_corpse_desc, fight.cpp).
CORPSE_LINE_MARKER = "corpse of"
# What `look in corpse` prints before the corpse's contents (do_look, act_info.cpp).
CORPSE_CONTENTS_HEADER = "corpse (here) :"
# The stat lines the stunned-band margin rests on (do_stat_character, act_wiz.cpp).
CURRENT_HIT_PATTERN = re.compile(r"HP :\[(-?\d+)/(\d+)\+")
CURRENT_CONSTITUTION_PATTERN = re.compile(r"Con:\s*\[(-?\d+)/(-?\d+)/(-?\d+)\]")
NO_RESISTANCES_LINE = "RES: <NONE> VUL: <NONE>"
# The stat line just before the affect lines: once it arrives, the drain collects the rest.
STAT_LAST_LINE_MARKER = "VUL:"


def stat_hit_constitution_and_resists_nothing(
    implementor: GameSession, stat_command: str
) -> tuple[int, int, bool]:
    """Current hit points and current constitution from a `stat` reply, and whether it shows
    no resistance or vulnerability."""
    text = reply_to(implementor, stat_command, [STAT_LAST_LINE_MARKER])
    hit = CURRENT_HIT_PATTERN.search(text)
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
    arrived = implementor.command(f"goto {wearer.name.lower()}")
    assert arrived.contains(ARENA_CENTRE_NAME), arrived.text
    created = implementor.command(f"load obj {AMULET_VNUM}")
    assert created.contains(f"You create {AMULET}."), created.text
    given = implementor.command(f"give amulet {wearer.name.lower()}")
    assert given.contains(f"You give {AMULET} to {wearer.name}."), given.text


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
    stat_values = stat_hit_constitution_and_resists_nothing(implementor, f"stat player {LAST_BLOW_WEARER.name.lower()}")
    assert_blow_leaves_zero(*stat_values)

    died = reply_to(wearer, "wear amulet", [NEW_BODY_MARKER])
    expected_order = [WEAR_AMULET_MARKER, STUNNED_MARKER, BODY_FAILED_MARKER, NEW_BODY_MARKER, HUMAN_START_ROOM_NAME]
    assert_in_order(died, expected_order)
    assert DEAD_MARKER not in died, died
    seen = implementor.expect([COLLAPSES_FORMAT.format(LAST_BLOW_WEARER.name)]) + implementor.drain(SETTLE_SECONDS)
    assert seen.count(COLLAPSES_FORMAT.format(LAST_BLOW_WEARER.name)) == 1, seen

    corpses = corpse_lines(implementor)
    assert len(corpses) == 1, corpses
    assert corpse_contents(implementor) == [AMULET]
    assert stat_hit_constitution_and_resists_nothing(implementor, f"stat player {LAST_BLOW_WEARER.name.lower()}")[0] == 1


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
    stat_values = stat_hit_constitution_and_resists_nothing(implementor, f"stat player {TWO_BLOW_WEARER.name.lower()}")
    assert_blow_leaves_zero(*stat_values)

    died = reply_to(wearer, "remove amulet", [REMOVE_AMULET_MARKER])
    expected_order = [STUNNED_MARKER, BODY_FAILED_MARKER, NEW_BODY_MARKER, HUMAN_START_ROOM_NAME, REMOVE_AMULET_MARKER]
    assert_in_order(died, expected_order)
    assert DEAD_MARKER not in died, died
    seen = implementor.expect([COLLAPSES_FORMAT.format(TWO_BLOW_WEARER.name)]) + implementor.drain(SETTLE_SECONDS)
    assert seen.count(COLLAPSES_FORMAT.format(TWO_BLOW_WEARER.name)) == 1, seen

    carried = wearer.command("inventory").text
    assert AMULET in carried, carried
    assert wearer.command("look").room_name() == HUMAN_START_ROOM_NAME
    corpses = corpse_lines(implementor)
    assert len(corpses) == 1, corpses
    assert corpse_contents(implementor) == []
    assert stat_hit_constitution_and_resists_nothing(implementor, f"stat player {TWO_BLOW_WEARER.name.lower()}")[0] == 1


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
    arrived = implementor.command(f"goto {world.ROOM_ARENA_CENTRE}")
    assert arrived.contains(ARENA_CENTRE_NAME), arrived.text
    assert implementor.command(f"load mob {PORTER_VNUM}").contains("You create a porter.")
    created = implementor.command(f"load obj {AMULET_VNUM}")
    assert created.contains(f"You create {AMULET}."), created.text
    given = implementor.command("give amulet porter")
    assert given.contains(f"You give {AMULET} to a porter."), given.text
    set_hit = implementor.command(f"wizset mob porter hit {GEAR_POISON_BLOW}")
    assert set_hit.contains(f"hit set to {GEAR_POISON_BLOW}."), set_hit.text
    hit, constitution, resists_nothing = stat_hit_constitution_and_resists_nothing(implementor, "stat mob porter")
    # The porter's constitution is its own (mob/17.mob), not WEARER_CONSTITUTION: read and assert
    # it, so the stunned band in assert_blow_leaves_zero holds for it too.
    assert constitution >= 4, constitution
    assert_blow_leaves_zero(hit, constitution, resists_nothing)

    forced = implementor.command("force porter wear amulet").text + implementor.drain(SETTLE_SECONDS)
    expected_order = [f"A porter wears {AMULET} around his neck.", COLLAPSES_FORMAT.format("A porter")]
    assert_in_order(forced, expected_order)
    assert "is dead!  R.I.P." not in forced, forced

    corpses = corpse_lines(implementor)
    assert len(corpses) == 1, corpses
    assert corpse_contents(implementor) == [AMULET]
    porters = implementor.command("where porter").text
    assert "Couldn't find any such thing." in porters, porters
