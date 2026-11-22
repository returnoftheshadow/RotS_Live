"""Idle removal of a link-dead player: the gear that poisons as it comes off, survived or fatal."""

from __future__ import annotations

import re

import pytest

from rots_harness import world
from rots_harness.characters import IMPLEMENTOR, Character, StoredValues
from rots_harness.game_log import count_in_log
from rots_harness.game_text import DEATH_CRY_FORMAT, FORCED_REPLY, IS_DEAD_FORMAT, POISON_TICK, WEAR_HEAD_FORMAT
from rots_harness.immortal_commands import give_new_object, goto
from rots_harness.saved_files import CARRIED_WEAR_POSITION, HEAD_WEAR_POSITION, saved_wear_positions
from rots_harness.session import GameSession
from rots_harness.stat_reading import AFFECT_BITS_PATTERN, HIT_POINTS_PATTERN, read_stat
from rots_harness.text_lines import first_number

pytestmark = [pytest.mark.scenario, pytest.mark.idle_removal]

# Room 2100 in zone 21, the idler's load room, where idle removal brings the idler back.
ROOM_IDLE_HALL = 2100
# How look names the idle hall.
ROOM_IDLE_HALL_NAME = "Idle Hall"
# Room 1102, the human idle room (mortal_idle_room, consts.cpp), where the void pull puts the
# idler.
ROOM_MORTAL_IDLE = 1102
# A level-10 human warrior with constitution 0, so its maximum hit points come from its level and
# class alone (recalc_abilities, profs.cpp), whatever the seed rolled, and update_pos
# (fight.cpp) counts it dead at 0 hit points or fewer. No hunger or thirst, so neither changes
# its regeneration during the idle hours.
IDLER = Character(
    name="Testidler",
    race="human",
    sex="male",
    level=10,
    load_room=ROOM_IDLE_HALL,
    class_name="warrior",
    stored=StoredValues(con=0, hunger=-1, thirst=-1),
)
# The idler's maximum hit points: 10 + level 10 + the class term, its constitution adding nothing.
IDLER_MAXIMUM_HIT = 74

# The maximum hit points the sash takes away.
SASH_HIT_PENALTY = 72
# The idler's maximum hit points while wearing the sash.
SASHED_MAXIMUM_HIT = IDLER_MAXIMUM_HIT - SASH_HIT_PENALTY
# The wear position a saved object records for the neck (WEAR_NECK_1, src/structs.h).
NECK_WEAR_POSITION = 3

# check_idling (limits.cpp) removes an idler in the hourly update that takes its idle timer past
# 28; the timer counts the hours since the idler last sent anything.
HOURS_BEFORE_REMOVAL = 28
# The poison blow gear deals as it goes on or comes off (unequip_char, handler.cpp).
GEAR_POISON_DAMAGE = 5

# What the room reads when the idler's connection closes (close_socket, comm.cpp).
IDLER_LOST_LINK = f"{IDLER.name} has lost his link."
# What the room reads when check_idling pulls the idler into the void, after 8 idle hours.
IDLER_DISAPPEARS = f"{IDLER.name} disappears into the void."
# What the room reads when the amulet's poison blow hurts the idler (lib/misc/messages).
IDLER_WRITHES = f"{IDLER.name} writhes in pain as the poison takes hold."
# What the room reads when the idler dies.
IDLER_IS_DEAD = IS_DEAD_FORMAT.format(IDLER.name)
# What the room reads of the dying idler's cry.
IDLER_DEATH_CRY = DEATH_CRY_FORMAT.format(IDLER.name)
# How look shows a human's corpse after a poison death (get_corpse_desc, fight.cpp).
HUMAN_CORPSE = "The rancid corpse of *a Human* is lying here."
# The part of any line that shows a corpse.
CORPSE_PART = "corpse"
# What the idler reads at its first command after logging back in: the AFK flag the idle hours
# set was saved with it.
BACK_FROM_AFK_REPLY = "You return to your keyboard."
# What stat player answers for a character not in the game (do_stat, act_info.cpp).
NO_SUCH_PLAYER_REPLY = "No such player around."
# What equipment answers when nothing is worn (show_equipment_to_char, act_info.cpp).
NOTHING_WORN_REPLY = "Nothing."
# What the server logs when check_idling removes the idler.
IDLE_REMOVAL_LOG = f"{IDLER.name} force-rented and extracted (idle)."
# What the server logs when the idler dies of its own gear's poison (die, fight.cpp).
DEATH_LOG = f"{IDLER.name} killed by {IDLER.name} at {ROOM_IDLE_HALL_NAME}"
# The part of every death the server logs.
KILLED_BY_PART = "killed by"

# The stat line with the room vnum (do_stat_character, act_wiz.cpp).
STAT_ROOM_PATTERN = re.compile(r"In room \[\s*(\d+)\]")
# The stat line with the idle timer, in game hours.
STAT_TIMER_PATTERN = re.compile(r"Idle Timer \(in tics\) \[(\d+)\]")
# The score line with current and maximum hit points.
SCORE_HIT_POINTS_PATTERN = re.compile(r"You have (-?\d+)/(\d+) hit,")


def hand_object(implementor: GameSession, vnum: int, keyword: str) -> None:
    """Load an object and give it to the idler in the room, checking the reply names the idler."""
    give_new_object(implementor, vnum, keyword, IDLER.name.lower(), shown_recipient=IDLER.name)


def idler_stat(implementor: GameSession) -> tuple[int, int, int, int, str]:
    """Return the idler's room vnum, current and maximum hit points, idle timer and affect bits."""
    text = read_stat(implementor, f"player {IDLER.name.lower()}")
    room = first_number(STAT_ROOM_PATTERN, text)
    timer = first_number(STAT_TIMER_PATTERN, text)
    hit = HIT_POINTS_PATTERN.search(text)
    affects = AFFECT_BITS_PATTERN.search(text)
    assert hit and affects, f"stat lines missing for {IDLER.name}:\n{text}"
    current_hit_text = hit.group(1)
    maximum_hit_text = hit.group(2)
    affects_text = affects.group(1)
    return room, int(current_hit_text), int(maximum_hit_text), timer, affects_text.strip()


def force_idler(implementor: GameSession, command: str) -> None:
    """Have the link-dead idler run command, leaving its idle timer as it was.

    The command is sent from the implementor's connection, not the idler's.
    """
    forced = implementor.command(f"force {IDLER.name.lower()} {command}")
    assert forced.contains(FORCED_REPLY), forced.text


def idle_the_idler_for_28_hours(server, implementor: GameSession, idler: GameSession, *, with_sash: bool) -> None:
    """Gear the idler, close its connection and advance 28 game hours, one short of its removal.

    The idler is given the cap to wear, and the bag and the amulet (and the sash when with_sash)
    to carry, and its connection is closed without quitting. Checks what the implementor, waiting
    in the idle hall, reads: the lost link, then, once in the 28 hours, the void pull; and that the
    idler is then in the void, link-dead, with an idle timer of 28 and full hit points.
    """
    goto(implementor, ROOM_IDLE_HALL, ROOM_IDLE_HALL_NAME)
    hand_object(implementor, world.OBJECT_LEATHER_CAP, "cap")
    hand_object(implementor, world.OBJECT_LEATHER_BAG, "bag")
    hand_object(implementor, world.OBJECT_SICKLY_AMULET, "amulet")
    if with_sash:
        hand_object(implementor, world.OBJECT_GREY_SASH, "sash")
    worn = idler.command("wear cap")
    assert worn.contains(WEAR_HEAD_FORMAT.format(world.OBJECT_LEATHER_CAP_SHORT_DESCRIPTION)), worn.text

    idler.close()
    lost_link = implementor.expect([IDLER_LOST_LINK])
    assert lost_link.count(IDLER_LOST_LINK) == 1, lost_link

    idle_hours = server.text_after_hours(implementor, HOURS_BEFORE_REMOVAL)
    assert idle_hours.count(IDLER_DISAPPEARS) == 1, idle_hours
    assert IDLER_WRITHES not in idle_hours, idle_hours
    idle_stat = idler_stat(implementor)
    expected_stat = (ROOM_MORTAL_IDLE, IDLER_MAXIMUM_HIT, IDLER_MAXIMUM_HIT, HOURS_BEFORE_REMOVAL, "<NONE>")
    assert idle_stat == expected_stat, idle_stat


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, IDLER)
def test_an_idler_whose_gear_poisons_them_as_it_comes_off_is_removed_and_returns_with_it(server) -> None:
    """Pins the idle removal of a link-dead player whom the gear's poison does not kill.

    The idler wears a cap, carries a bag and the sickly amulet, and closes its connection without
    quitting. After 28 idle hours (pulled into the void after 8) it is made to put the amulet on,
    whose poison blow takes 5 hit points, and in the next hour check_idling removes it: it is
    brought back to the idle hall, its gear is saved to its objects file, and the amulet's poison
    blow as it comes off is all the hall reads. The server logs the removal, and the idler logs
    back in, still flagged AFK, to the hall wearing the amulet and cap and carrying the bag, the
    amulet's blow greeting it again as it goes back on. Nothing on this path draws a number that
    changes the outcome: regeneration only raises hit points, so it cannot bring the blow nearer a
    death.
    """
    implementor = server.login(IMPLEMENTOR.name)
    idler = server.login(IDLER.name)
    idle_the_idler_for_28_hours(server, implementor, idler, with_sash=False)

    force_idler(implementor, "wear amulet")
    room, hit, maximum_hit, timer, affects = idler_stat(implementor)
    # Control case, Testidler poisoned by the amulet going on, before the removal hour:
    #   hit at the removal blow = hit + regeneration over the hour's passes    (fast_update)
    #                           >= 69 + 0                    (HP :[69/74], hit_gain is positive)
    #   after the blow          = 69 - GEAR_POISON_DAMAGE    (unequip_char's damage(ch, ch, 5))
    #                           >= 69 - 5 = 64, above 0: the idler survives whatever
    #                              regeneration adds.
    stat_values = (room, hit, maximum_hit, timer, affects)
    assert stat_values == (
        ROOM_MORTAL_IDLE, IDLER_MAXIMUM_HIT - GEAR_POISON_DAMAGE, IDLER_MAXIMUM_HIT, HOURS_BEFORE_REMOVAL, "POISON.",
    ), stat_values
    assert hit - GEAR_POISON_DAMAGE == 64, stat_values

    removal_hour = server.text_after_hours(implementor)
    assert removal_hour.count(IDLER_WRITHES) == 1, removal_hour
    assert IDLER_IS_DEAD not in removal_hour, removal_hour
    hall = implementor.command("look")
    assert IDLER.name not in hall.text and CORPSE_PART not in hall.text, hall.text
    gone = implementor.command(f"stat player {IDLER.name.lower()}")
    assert gone.contains(NO_SUCH_PLAYER_REPLY), gone.text
    removals = count_in_log(server.handle.log_path, IDLE_REMOVAL_LOG)
    assert removals == 1, f"{IDLE_REMOVAL_LOG!r} logged {removals} times"
    deaths = count_in_log(server.handle.log_path, KILLED_BY_PART)
    assert deaths == 0, f"{KILLED_BY_PART!r} logged {deaths} times"
    saved_positions = saved_wear_positions(server.lib_dir, IDLER.name)
    assert saved_positions == {
        world.OBJECT_SICKLY_AMULET: NECK_WEAR_POSITION,
        world.OBJECT_LEATHER_CAP: HEAD_WEAR_POSITION,
        world.OBJECT_LEATHER_BAG: CARRIED_WEAR_POSITION,
    }, saved_positions

    returned = server.login(IDLER.name)
    assert returned.everything.count(POISON_TICK) == 1, returned.everything
    looked = returned.command("look")
    assert looked.contains(BACK_FROM_AFK_REPLY), looked.text
    looked_again = returned.command("look")
    assert looked_again.room_name() == ROOM_IDLE_HALL_NAME, looked_again.text
    equipment = returned.command("equipment")
    assert equipment.contains(world.OBJECT_SICKLY_AMULET_SHORT_DESCRIPTION), equipment.text
    assert equipment.contains(world.OBJECT_LEATHER_CAP_SHORT_DESCRIPTION), equipment.text
    inventory = returned.command("inventory")
    assert inventory.contains(world.OBJECT_LEATHER_BAG_SHORT_DESCRIPTION), inventory.text


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, IDLER)
def test_an_idler_whose_gear_kills_them_as_it_comes_off_dies_once_and_the_server_stays_up(server) -> None:
    """Fix pin (#561): idle removal stops once the gear coming off kills a link-dead idler.

    As the survived removal, with the draining sash put on after the amulet, which holds the
    idler at 2 hit points. In the removal hour the amulet comes off first (neck, slot 3, before
    the sash's slot 12), and its poison blow kills the idler. The hall reads the blow, the death
    and the death cry, and keeps the corpse with the cap and sash the idler still wore; the server
    logs the removal and the death once each and stays up; and the idler logs back in at the
    human start with nothing, at a quarter of its hit points. #561 made check_idling stop with the
    freed idler; before it, check_idling went on and extracted it a second time, a crash or a
    heap-use-after-free. Nothing on this path draws a number that changes the outcome.
    """
    implementor = server.login(IMPLEMENTOR.name)
    idler = server.login(IDLER.name)
    idle_the_idler_for_28_hours(server, implementor, idler, with_sash=True)

    force_idler(implementor, "wear amulet")
    force_idler(implementor, "wear sash")
    room, hit, maximum_hit, timer, affects = idler_stat(implementor)
    # Fatal case, Testidler poisoned and sashed, before the removal hour:
    #   hit at the removal blow = min(hit + regeneration, maximum)            (fast_update)
    #                           <= 2                                          (HP :[2/2])
    #   after the blow          = 2 - GEAR_POISON_DAMAGE = -3, below 0, and the blow's attacker,
    #                             the idler, is a player, so damage (fight.cpp) sets hit to
    #                             min(-3, -20) = -20
    #   update_pos              : -20 <= -Con / 2 = -0 / 2 = 0                (Con: [0/0/0])
    #                           = POSITION_DEAD, and damage calls die.
    stat_values = (room, hit, maximum_hit, timer, affects)
    assert stat_values == (
        ROOM_MORTAL_IDLE, SASHED_MAXIMUM_HIT, SASHED_MAXIMUM_HIT, HOURS_BEFORE_REMOVAL, "POISON.",
    ), stat_values
    assert SASHED_MAXIMUM_HIT - GEAR_POISON_DAMAGE == -3, stat_values

    removal_hour = server.text_after_hours(implementor)
    assert removal_hour.count(IDLER_WRITHES) == 1, removal_hour
    assert removal_hour.count(IDLER_IS_DEAD) == 1, removal_hour
    assert removal_hour.count(IDLER_DEATH_CRY) == 1, removal_hour
    hall = implementor.command("look")
    assert hall.contains(HUMAN_CORPSE), hall.text
    corpse = implementor.command("look in corpse")
    assert corpse.contains(world.OBJECT_LEATHER_CAP_SHORT_DESCRIPTION), corpse.text
    assert corpse.contains(world.OBJECT_GREY_SASH_SHORT_DESCRIPTION), corpse.text
    assert world.OBJECT_SICKLY_AMULET_SHORT_DESCRIPTION not in corpse.text, corpse.text
    assert world.OBJECT_LEATHER_BAG_SHORT_DESCRIPTION not in corpse.text, corpse.text
    gone = implementor.command(f"stat player {IDLER.name.lower()}")
    assert gone.contains(NO_SUCH_PLAYER_REPLY), gone.text
    removals = count_in_log(server.handle.log_path, IDLE_REMOVAL_LOG)
    assert removals == 1, f"{IDLE_REMOVAL_LOG!r} logged {removals} times"
    deaths = count_in_log(server.handle.log_path, DEATH_LOG)
    assert deaths == 1, f"{DEATH_LOG!r} logged {deaths} times"
    saved_positions = saved_wear_positions(server.lib_dir, IDLER.name)
    assert saved_positions == {}, saved_positions

    returned = server.login(IDLER.name)
    looked = returned.command("look")
    assert looked.contains(BACK_FROM_AFK_REPLY), looked.text
    looked_again = returned.command("look")
    assert looked_again.room_name() == world.ROOM_HUMAN_START_NAME, looked_again.text
    equipment = returned.command("equipment")
    assert equipment.contains(NOTHING_WORN_REPLY), equipment.text
    score = returned.command("score")
    hit_points = SCORE_HIT_POINTS_PATTERN.search(score.text)
    assert hit_points is not None, score.text
    current_hit = int(hit_points.group(1))
    maximum_hit_after = int(hit_points.group(2))
    # raw_kill (fight.cpp) counts a poison death as a death to a player: a quarter of the maximum.
    assert (current_hit, maximum_hit_after) == (IDLER_MAXIMUM_HIT // 4, IDLER_MAXIMUM_HIT), score.text
