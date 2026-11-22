"""Idle removal of a link-dead player: the gear that poisons as it comes off, survived or fatal."""

from __future__ import annotations

import json
import re
from pathlib import Path

import pytest

from rots_harness import world
from rots_harness.characters import IMPLEMENTOR, Character, StoredValues
from rots_harness.game_text import DEATH_CRY_FORMAT, FORCED_REPLY, IS_DEAD_FORMAT, POISON_TICK
from rots_harness.session import GameSession
from rots_harness.stat_reading import AFFECT_BITS_PATTERN, HIT_POINTS_PATTERN, read_stat

pytestmark = [pytest.mark.scenario, pytest.mark.idle_removal]

# Room 2100 in zone 21, the idler's load room, where idle removal brings the idler back.
ROOM_IDLE_HALL = 2100
# How look names the idle hall.
IDLE_HALL_NAME = "Idle Hall"
# Room 1102, the human idle room (mortal_idle_room, consts.cpp), where the void pull puts the idler.
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

# Zone 21's draining grey sash, worn about the body: maximum hit points -72.
SASH_VNUM = 2100
# The maximum hit points the sash takes away.
SASH_HIT_PENALTY = 72
# The idler's maximum hit points while wearing the sash.
SASHED_MAXIMUM_HIT = IDLER_MAXIMUM_HIT - SASH_HIT_PENALTY
# The wear position a saved object records for the neck (WEAR_NECK_1, src/structs.h).
NECK_WEAR_POSITION = 3
# The wear position a saved object records for the head (WEAR_HEAD).
HEAD_WEAR_POSITION = 6
# The wear position a saved object records when it is carried rather than worn (MAX_WEAR).
CARRIED_WEAR_POSITION = 22

# check_idling (limits.cpp) removes an idler in the hourly update that takes its idle timer past
# 28; the timer counts the hours since the idler last sent anything.
HOURS_BEFORE_REMOVAL = 28
# The poison blow gear deals as it goes on or comes off (unequip_char, handler.cpp).
GEAR_POISON_DAMAGE = 5

# What the room reads when the idler's connection closes (close_socket, comm.cpp).
LOST_LINK_MARKER = "Testidler has lost his link."
# What the room reads when check_idling pulls the idler into the void, after 8 idle hours.
VOID_MARKER = "Testidler disappears into the void."
# What the room reads when the amulet's poison blow hurts the idler (lib/misc/messages).
WRITHES_MARKER = "Testidler writhes in pain as the poison takes hold."
# What the room reads when the idler dies.
DEAD_MARKER = IS_DEAD_FORMAT.format("Testidler")
# What the room reads of the dying idler's cry.
DEATH_CRY_MARKER = DEATH_CRY_FORMAT.format("Testidler")
# How look shows a human's corpse after a poison death (get_corpse_desc, fight.cpp).
CORPSE_MARKER = "The rancid corpse of *a Human* is lying here."
# What the idler reads at its first command after logging back in: the AFK flag the idle hours
# set was saved with it.
BACK_FROM_AFK_MARKER = "You return to your keyboard."
# What stat player answers for a character no longer in the game (do_stat, act_info.cpp).
NO_SUCH_PLAYER_MARKER = "No such player around."
# What the server logs when check_idling removes the idler.
IDLE_REMOVAL_LOG = "Testidler force-rented and extracted (idle)."
# What the server logs when the idler dies of its own gear's poison (die, fight.cpp).
DEATH_LOG = "Testidler killed by Testidler at Idle Hall"
# Seconds to keep reading after an acknowledgement, for text already sent to arrive.
SETTLE_SECONDS = 1.0

# The stat line with the room vnum (do_stat_character, act_wiz.cpp).
STAT_ROOM_PATTERN = re.compile(r"In room \[\s*(\d+)\]")
# The stat line with the idle timer, in game hours.
STAT_TIMER_PATTERN = re.compile(r"Idle Timer \(in tics\) \[(\d+)\]")
# The score line with current and maximum hit points.
SCORE_HIT_PATTERN = re.compile(r"You have (-?\d+)/(\d+) hit,")


def hand_object(implementor: GameSession, vnum: int, keyword: str) -> None:
    """Loads an object into the implementor's inventory and gives it to the idler, in the same room."""
    created = implementor.command(f"load obj {vnum}")
    assert created.contains("You create"), created.text
    given = implementor.command(f"give {keyword} {IDLER.name.lower()}")
    assert given.contains(f"to {IDLER.name}."), given.text


def idler_stat(implementor: GameSession) -> tuple:
    """The idler's room vnum, current and maximum hit points, idle timer and affect bits, from stat."""
    text = read_stat(implementor, f"player {IDLER.name.lower()}")
    room = STAT_ROOM_PATTERN.search(text)
    hit = HIT_POINTS_PATTERN.search(text)
    timer = STAT_TIMER_PATTERN.search(text)
    affects = AFFECT_BITS_PATTERN.search(text)
    assert room and hit and timer and affects, f"stat lines missing for {IDLER.name}:\n{text}"
    return int(room.group(1)), int(hit.group(1)), int(hit.group(2)), int(timer.group(1)), affects.group(1).strip()


def force_idler(implementor: GameSession, command: str) -> None:
    """Has the link-dead idler run command, which sends nothing from the idler's connection and so
    leaves its idle timer as it was."""
    forced = implementor.command(f"force {IDLER.name.lower()} {command}")
    assert forced.contains(FORCED_REPLY), forced.text


def saved_wear_positions(lib_dir: Path) -> dict[int, int]:
    """Each object vnum in the idler's saved objects file, with the wear position it was saved at."""
    paths = sorted((lib_dir / "accounts").glob(f"*/*/{IDLER.name.lower()}.objects.json"))
    assert len(paths) == 1, paths
    saved = json.loads(paths[0].read_text(encoding="utf-8"))
    return {entry["item_number"]: entry["wear_pos"] for entry in saved["objects"]}


def server_log_count(server, line: str) -> int:
    """How many times the server's log, game.log in the run directory, holds line."""
    log_text = (server.run_dir / "game.log").read_text(encoding="latin-1")
    return log_text.count(line)


def idle_the_idler_for_28_hours(server, implementor: GameSession, idler: GameSession, *, with_sash: bool) -> None:
    """Gives the idler the cap to wear, the bag and the amulet (and the sash when with_sash) to
    carry, closes its connection without quitting, and advances 28 game hours, one short of its
    removal.

    Checks what the implementor, waiting in the idle hall, reads: the lost link, then, once in the
    28 hours, the void pull; and that the idler is then in the void, link-dead, with an idle timer
    of 28 and full hit points.
    """
    arrived = implementor.command(f"goto {ROOM_IDLE_HALL}")
    assert arrived.room_name() == IDLE_HALL_NAME, arrived.text
    hand_object(implementor, world.OBJECT_LEATHER_CAP, "cap")
    hand_object(implementor, world.OBJECT_LEATHER_BAG, "bag")
    hand_object(implementor, world.OBJECT_SICKLY_AMULET, "amulet")
    if with_sash:
        hand_object(implementor, SASH_VNUM, "sash")
    worn = idler.command("wear cap")
    assert worn.contains("You wear a leather cap on your head."), worn.text

    idler.close()
    lost_link = implementor.expect([LOST_LINK_MARKER])
    assert lost_link.count(LOST_LINK_MARKER) == 1, lost_link

    server.advance_hours(HOURS_BEFORE_REMOVAL)
    idle_hours = implementor.drain(SETTLE_SECONDS)
    assert idle_hours.count(VOID_MARKER) == 1, idle_hours
    assert WRITHES_MARKER not in idle_hours, idle_hours
    assert idler_stat(implementor) == (ROOM_MORTAL_IDLE, IDLER_MAXIMUM_HIT, IDLER_MAXIMUM_HIT, HOURS_BEFORE_REMOVAL, "<NONE>")


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
    #                           >= 69 + 0                       (HP :[69/74], hit_gain is positive)
    #   after the blow          = 69 - GEAR_POISON_DAMAGE       (unequip_char's damage(ch, ch, 5))
    #                           >= 69 - 5 = 64, above 0: the idler survives whatever regeneration adds.
    assert (room, hit, maximum_hit, timer, affects) == (
        ROOM_MORTAL_IDLE, IDLER_MAXIMUM_HIT - GEAR_POISON_DAMAGE, IDLER_MAXIMUM_HIT, HOURS_BEFORE_REMOVAL, "POISON.",
    )
    assert hit - GEAR_POISON_DAMAGE == 64

    server.advance_hours(1)
    removal_hour = implementor.drain(SETTLE_SECONDS)
    assert removal_hour.count(WRITHES_MARKER) == 1, removal_hour
    assert DEAD_MARKER not in removal_hour, removal_hour
    hall = implementor.command("look")
    assert IDLER.name not in hall.text and "corpse" not in hall.text, hall.text
    gone = implementor.command(f"stat player {IDLER.name.lower()}")
    assert gone.contains(NO_SUCH_PLAYER_MARKER), gone.text
    assert server_log_count(server, IDLE_REMOVAL_LOG) == 1
    assert server_log_count(server, "killed by") == 0
    assert saved_wear_positions(server.lib_dir) == {
        world.OBJECT_SICKLY_AMULET: NECK_WEAR_POSITION,
        world.OBJECT_LEATHER_CAP: HEAD_WEAR_POSITION,
        world.OBJECT_LEATHER_BAG: CARRIED_WEAR_POSITION,
    }

    returned = server.login(IDLER.name)
    assert returned.everything.count(POISON_TICK) == 1, returned.everything
    assert returned.command("look").contains(BACK_FROM_AFK_MARKER)
    assert returned.command("look").room_name() == IDLE_HALL_NAME
    equipment = returned.command("equipment")
    assert equipment.contains("a sickly amulet") and equipment.contains("a leather cap"), equipment.text
    inventory = returned.command("inventory")
    assert inventory.contains("a leather bag"), inventory.text


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, IDLER)
def test_an_idler_whose_gear_kills_them_as_it_comes_off_dies_once_and_the_server_stays_up(server) -> None:
    """New-behaviour pin: idle removal stops once the gear coming off kills a link-dead idler.

    As the survived removal, with the draining sash put on after the amulet, which holds the
    idler at 2 hit points. In the removal hour the amulet comes off first (neck, slot 3, before
    the sash's slot 12), and its poison blow kills the idler. The hall reads the blow, the death
    and the death cry, and keeps the corpse with the cap and sash the idler still wore; the server
    logs the removal and the death once each and stays up; and the idler logs back in at the
    human start with nothing, at a quarter of its hit points. Before #561, check_idling went on
    with the freed idler and extracted it a second time: a crash or a heap-use-after-free.
    Nothing on this path draws a number that changes the outcome.
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
    assert (room, hit, maximum_hit, timer, affects) == (
        ROOM_MORTAL_IDLE, SASHED_MAXIMUM_HIT, SASHED_MAXIMUM_HIT, HOURS_BEFORE_REMOVAL, "POISON.",
    )
    assert SASHED_MAXIMUM_HIT - GEAR_POISON_DAMAGE == -3

    server.advance_hours(1)
    removal_hour = implementor.drain(SETTLE_SECONDS)
    assert removal_hour.count(WRITHES_MARKER) == 1, removal_hour
    assert removal_hour.count(DEAD_MARKER) == 1, removal_hour
    assert removal_hour.count(DEATH_CRY_MARKER) == 1, removal_hour
    hall = implementor.command("look")
    assert hall.contains(CORPSE_MARKER), hall.text
    corpse = implementor.command("look in corpse")
    assert corpse.contains("a leather cap") and corpse.contains("a draining grey sash"), corpse.text
    assert "a sickly amulet" not in corpse.text and "a leather bag" not in corpse.text, corpse.text
    gone = implementor.command(f"stat player {IDLER.name.lower()}")
    assert gone.contains(NO_SUCH_PLAYER_MARKER), gone.text
    assert server_log_count(server, IDLE_REMOVAL_LOG) == 1
    assert server_log_count(server, DEATH_LOG) == 1
    assert saved_wear_positions(server.lib_dir) == {}

    returned = server.login(IDLER.name)
    assert returned.command("look").contains(BACK_FROM_AFK_MARKER)
    assert returned.command("look").room_name() == world.ROOM_HUMAN_START_NAME
    equipment = returned.command("equipment")
    assert equipment.contains("Nothing."), equipment.text
    score = returned.command("score")
    hit_points = SCORE_HIT_PATTERN.search(score.text)
    assert hit_points is not None, score.text
    # raw_kill (fight.cpp) counts a poison death as a death to a player: a quarter of the maximum.
    assert (int(hit_points.group(1)), int(hit_points.group(2))) == (IDLER_MAXIMUM_HIT // 4, IDLER_MAXIMUM_HIT)
