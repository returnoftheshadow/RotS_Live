"""Summon's pins: dark rooms, zone distance, stay-zone gear and a target who quits mid-cast.

The pins cover the dark-room block, the zone-distance arithmetic, the stay-zone gear a target
leaves behind across zones, and a target who quits while the spell is cast.
"""

from __future__ import annotations

import re
from pathlib import Path

import pytest

from rots_harness import world
from rots_harness.casting import CAST_TIMEOUT_SECONDS, LOST_CONCENTRATION_REPLY, NO_ENERGY_REPLY, cast_until_it_lands
from rots_harness.characters import IMPLEMENTOR, Character, StoredValues
from rots_harness.game_text import CONCENTRATE_REPLY, QUIT_DROP_REPLY, TELEPORTED_END
from rots_harness.immortal_commands import give_new_object, goto, set_holylight
from rots_harness.replies import SETTLE_SECONDS, reply_to, settled_expect
from rots_harness.session import GameSession
from rots_harness.stat_reading import MAGE_LEVEL_PATTERN, read_stat
from rots_harness.text_lines import first_number

pytestmark = [pytest.mark.scenario, pytest.mark.summon]

# Room 1201, the only room of zone 12, whose map square is (3, 4).
ROOM_DISTANT_CELL = 1201
# The name look shows for ROOM_DISTANT_CELL (wld/12.wld).
ROOM_DISTANT_CELL_NAME = "Distant Cell"

# The intelligence the caster is stored with: the top of wizset's range.
CASTER_INTELLIGENCE = 40
# The intelligence each victim is stored with: the bottom of wizset's range.
VICTIM_INTELLIGENCE = 0

# A level-30 human mage in the arena centre who knows summon fully: 100 practice sessions put
# its knowledge at the cap of 100 (recalc_skills, spec_pro.cpp).
CASTER = Character(
    name="Testsummoner",
    race="human",
    sex="female",
    level=30,
    load_room=world.ROOM_ARENA_CENTRE,
    class_name="mage",
    skills=(("summon", 100),),
    stored=StoredValues(int=CASTER_INTELLIGENCE),
)
# A level-12 human warrior who starts in the dark cell: a mortal on the caster's side.
DARK_VICTIM = Character(
    name="Testlost",
    race="human",
    sex="male",
    level=12,
    load_room=world.ROOM_DARK_CELL,
    class_name="warrior",
    stored=StoredValues(int=VICTIM_INTELLIGENCE),
)
# A level-12 human warrior who starts in zone 12, five map squares from the caster's zone.
DISTANT_VICTIM = Character(
    name="Testfar",
    race="human",
    sex="male",
    level=12,
    load_room=ROOM_DISTANT_CELL,
    class_name="warrior",
    stored=StoredValues(int=VICTIM_INTELLIGENCE),
)
# A level-12 dwarf warrior who starts in zone 12: the stone girdle and the dwarven stone are
# dwarves' gear (their DWARF flag zaps anyone else who puts one on, equip_char in handler.cpp).
DISTANT_DWARF = Character(
    name="Testdelver",
    race="dwarf",
    sex="male",
    level=12,
    load_room=ROOM_DISTANT_CELL,
    class_name="warrior",
    stored=StoredValues(int=VICTIM_INTELLIGENCE),
)

# The caster's mage level: a single-class mage's mage level is its overall level, up to 30.
CASTER_MAGE_LEVEL = CASTER.level
# The test world's zone files, which hold each zone's map square.
ZONE_DIRECTORY = Path(__file__).resolve().parents[1] / "world" / "zon"

# What the caster reads when summon's target lookup finds nobody it can see (report_wrong_target,
# interpre.cpp, for a mask with TAR_CHAR_WORLD).
NOBODY_BY_THAT_NAME_REPLY = "Nobody by that name."
# What the caster reads when the victim saves or summon refuses the target (spell_summon).
FAILED_REPLY = "You failed."
# The start of what the caster reads when do_cast's big_brother check protects the target
# (spell_pa.cpp; a writing or AFK target, big_brother.cpp).
PROTECTED_TARGET_START = "You feel the Gods looking down upon you"
# What the caster reads when the victim has fought another player too recently (spell_summon).
RECENT_PK_REPLY = "Player has engaged in PK too recently to be summoned."
# What the caster reads, in place of the victim's name, when it cannot see the victim in the
# room the victim is leaving: spell_summon's act() runs PERS before the move.
UNSEEN_ARRIVAL = "Someone appears in the room."
# What the caster reads when the victim it can see arrives (spell_summon); format with the name.
ARRIVAL_FORMAT = "{} appears in the room."
# What the victim reads when it is moved (spell_summon); format with the caster's name.
SUMMONED_FORMAT = "{} summons you!"
# What the caster reads when the target left the game while the spell was cast (do_cast,
# spell_pa.cpp, for a TAR_CHAR_WORLD target in no room).
LEFT_THE_WORLD_REPLY = "Your victim has left the world."
# What a summoned character reads for each stay-zone item it carries as it leaves its zone
# (prohibit_item_stay_zone_move, act_move.cpp); format with the short description. A worn one
# comes off with no line.
STAY_ZONE_DROP_FORMAT = "You drop {}."
# What the room left behind reads as the dwarf's stone drops (prohibit_item_stay_zone_move).
DWARF_DROPS_STONE = f"{DISTANT_DWARF.name} drops {world.OBJECT_DWARVEN_STONE_SHORT_DESCRIPTION}."
# What wearing the girdle answers (wear_message, act_obj2.cpp).
GIRDLE_WORN_REPLY = f"You wear {world.OBJECT_STONE_GIRDLE_SHORT_DESCRIPTION} around your waist."
# How look shows the girdle on the floor.
GIRDLE_LIES_HERE = "A stone girdle lies here."
# How look shows the stone on the floor.
STONE_LIES_HERE = "A dwarven stone lies here."
# What look prints instead of the room for a character who cannot see (do_look, act_info.cpp).
DARK_ROOM_LOOK = "It is pitch black..."

# The current intelligence on stat's Int line, one input of new_saves_spell (do_stat_character,
# act_wiz.cpp).
CURRENT_INTELLIGENCE_PATTERN = re.compile(r"Int:\s*\[(-?\d+)/(-?\d+)/(-?\d+)\]")
# The spell penetration on stat's Spell_Pen line, another input of new_saves_spell.
SPELL_PEN_PATTERN = re.compile(r"Spell_Pen: (-?\d+),")
# A zone file's map line, `<symbol> <x> <y> <level>` (load_zones, zone.cpp).
ZONE_MAP_PATTERN = re.compile(r"^\S (-?\d+) (-?\d+) -?\d+$", re.MULTILINE)


def c_divide(numerator: int, denominator: int) -> int:
    """Return C's integer division, which truncates toward zero where Python's // floors."""
    return int(numerator / denominator)


def zone_map_square(zone_number: int) -> tuple[int, int]:
    """Return the (x, y) map square in the test world's zone file, which spell_summon reads."""
    zone_path = ZONE_DIRECTORY / f"{zone_number}.zon"
    text = zone_path.read_text(encoding="latin-1")
    match = ZONE_MAP_PATTERN.search(text)
    assert match is not None, f"no map line in zone {zone_number}:\n{text}"
    x_text = match.group(1)
    y_text = match.group(2)
    return int(x_text), int(y_text)


def save_inputs(implementor: GameSession, name: str) -> tuple[int, int, int]:
    """Return the mage level, current intelligence and spell penetration stat shows for name."""
    text = read_stat(implementor, f"player {name.lower()}")
    mage_level = first_number(MAGE_LEVEL_PATTERN, text)
    intelligence = first_number(CURRENT_INTELLIGENCE_PATTERN, text)
    spell_pen = first_number(SPELL_PEN_PATTERN, text)
    return mage_level, intelligence, spell_pen


def cast_summon(caster: GameSession, target_name: str) -> str:
    """Cast summon at target_name (as the game prints it) and return what the caster read after.

    A lost concentration is the one roll left that can stop the cast, so the cast is repeated
    after it. Each loss costs mana, and nothing regenerates mana between requests under
    affect-hours, which every test here uses, so the repeats end at NO_ENERGY_REPLY at the latest.
    """
    outcomes = [
        NOBODY_BY_THAT_NAME_REPLY,
        FAILED_REPLY,
        NO_ENERGY_REPLY,
        PROTECTED_TARGET_START,
        RECENT_PK_REPLY,
        UNSEEN_ARRIVAL,
        ARRIVAL_FORMAT.format(target_name),
    ]
    return cast_until_it_lands(caster, f"cast 'summon' {target_name.lower()}", outcomes)


def setup_caster_and_victim(
    implementor: GameSession, victim: Character
) -> tuple[tuple[int, int, int], tuple[int, int, int]]:
    """Return the caster's and the victim's save inputs.

    Holylight lets the implementor stat a character standing in the dark cell.
    """
    set_holylight(implementor, "on")
    caster_inputs = save_inputs(implementor, CASTER.name)
    victim_inputs = save_inputs(implementor, victim.name)
    return caster_inputs, victim_inputs


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, CASTER, DARK_VICTIM)
def test_a_mage_cannot_summon_a_player_standing_in_a_dark_room(server) -> None:
    """Legacy pin: summon cannot reach a player in a DARK room, though it reaches a lit one.

    Records today's behaviour. Summon's target mask (6, skills[] in consts.cpp) lacks
    TAR_DARK_OK, so the caster's world lookup (get_char_vis) needs CAN_SEE, and a human caster
    without infravision or holylight cannot see into an unlit DARK room. The cast stops at the
    lookup, before any roll. The interpreter's target_check_one (interpre.cpp) repeats the same
    sight test when the casting delay ends, so reaching dark rooms needs both checks changed.
    The control moves the same victim to a lit room of the same zone.
    The one draw left that can change the outcome is the concentration roll in do_cast, which
    cast_summon repeats; the affect-hours schedule keeps everything else from drawing. When
    summon is changed to reach dark rooms, this test becomes a strict xfail.
    """
    implementor = server.login(IMPLEMENTOR.name)
    caster = server.login(CASTER.name)
    victim = server.login(DARK_VICTIM.name)

    looked = victim.command("look")
    assert looked.room_name() == DARK_ROOM_LOOK, looked.text
    caster_inputs, victim_inputs = setup_caster_and_victim(implementor, DARK_VICTIM)

    # Dark case, Testlost in the dark cell (1133, DARK, zone 11): no margin applies, because
    # target_from_word (interpre.cpp) finds no target the caster can see and do_cast returns
    # with report_wrong_target before the delay, the concentration roll or new_saves_spell.
    dark_reply = cast_summon(caster, DARK_VICTIM.name)
    assert NOBODY_BY_THAT_NAME_REPLY in dark_reply, dark_reply
    assert CONCENTRATE_REPLY not in dark_reply, dark_reply
    still_dark = victim.command("look")
    assert still_dark.room_name() == DARK_ROOM_LOOK, still_dark.text

    implementor.command(f"teleport {DARK_VICTIM.name.lower()} {world.ROOM_ARENA_WEST}")
    settled_expect(victim, [TELEPORTED_END])

    # Control case, Testlost in Arena West (1130, lit, zone 11), caster in Arena Centre (zone 11):
    #   dist       = ((ch_x - v_x) ^ 2) + ((ch_y - v_y) ^ 2)        (spell_summon, mage.cpp)
    #              = ((0 - 0) ^ 2) + ((0 - 0) ^ 2)                  (zone 11's map square (0, 0))
    #              = 2 + 2 = 4                                      (^ is exclusive or: 0 ^ 2 = 2)
    #   save_bonus = dist = 4                (the NPC level bonus does not apply to a player)
    #   save_value = get_character_saving_throw(victim) + save_bonus    (spell_pa.cpp)
    #              = Mag / 3 + (Int - 8) / 4 + 0 + save_bonus           (0: not a hobbit)
    #              = 3 / 3   + (0 - 8) / 4   + 0 + 4                    (Mag:3, Int:[0/0/0])
    #              = 1 + -2 + 0 + 4 = 3
    #   casting_dc = 10 + Mag / 3 + (Int - 8) / 4 + Spell_Pen           (get_saving_throw_dc)
    #              = 10 + 30 / 3  + (40 - 8) / 4  + 0
    #                (Mag:30, Int:[40/40/40], Spell_Pen: 0)
    #              = 10 + 10 + 8 + 0 = 28
    #   saved      = roll + save_value > casting_dc, roll = number(1, 20)  (new_saves_spell)
    #              = at most 20 + 3 = 23, never above 28: never saves. A save_bonus of 0
    #                (squared distances) gives at most 19, so this case holds whatever the
    #                arithmetic.
    assert caster_inputs == (CASTER_MAGE_LEVEL, CASTER_INTELLIGENCE, 0), caster_inputs
    assert victim_inputs == (3, VICTIM_INTELLIGENCE, 0), victim_inputs
    caster_mage_level, caster_intelligence, caster_spell_pen = caster_inputs
    victim_mage_level, victim_intelligence = victim_inputs[:2]
    caster_x, caster_y = zone_map_square(11)
    victim_x, victim_y = zone_map_square(11)
    distance = ((caster_x - victim_x) ^ 2) + ((caster_y - victim_y) ^ 2)
    save_value = c_divide(victim_mage_level, 3) + c_divide(victim_intelligence - 8, 4) + 0 + distance
    casting_dc = 10 + c_divide(caster_mage_level, 3) + c_divide(caster_intelligence - 8, 4) + caster_spell_pen
    squares = ((caster_x, caster_y), (victim_x, victim_y))
    assert squares == ((0, 0), (0, 0)), squares
    margin = (distance, save_value, casting_dc)
    assert margin == (4, 3, 28), margin
    assert 20 + save_value <= casting_dc, margin

    lit_reply = cast_summon(caster, DARK_VICTIM.name)
    assert ARRIVAL_FORMAT.format(DARK_VICTIM.name) in lit_reply, lit_reply
    summoned = settled_expect(victim, [SUMMONED_FORMAT.format(CASTER.name)])
    assert world.ROOM_ARENA_CENTRE_NAME in summoned, summoned


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, CASTER, DISTANT_VICTIM)
def test_summon_reaches_a_zone_five_map_squares_away_as_if_it_were_nearer_than_the_casters_own(server) -> None:
    """Legacy pin: summon's zone distance uses `^`, C's exclusive or, where a square was meant.

    Records today's behaviour. spell_summon (mage.cpp) adds the zones' map distance to the
    victim's save as `((ch_x - v_x) ^ 2) + ((ch_y - v_y) ^ 2)`. From zone 11 at (0, 0) to zone 12
    at (3, 4), five squares apart, that is -3, below the +4 a victim in the caster's own zone
    gets, so a weak victim there never saves; squared, it would always save.
    The one draw left that can change the outcome is the concentration roll in do_cast, which
    cast_summon repeats; the affect-hours schedule keeps everything else from drawing. When the
    distance arithmetic changes, this test becomes a strict xfail.
    """
    implementor = server.login(IMPLEMENTOR.name)
    caster = server.login(CASTER.name)
    victim = server.login(DISTANT_VICTIM.name)

    looked = victim.command("look")
    assert looked.room_name() == ROOM_DISTANT_CELL_NAME, looked.text
    caster_inputs, victim_inputs = setup_caster_and_victim(implementor, DISTANT_VICTIM)

    # Distant case, caster in Arena Centre (zone 11), Testfar in the Distant Cell (zone 12):
    #   dist       = ((ch_x - v_x) ^ 2) + ((ch_y - v_y) ^ 2)        (spell_summon, mage.cpp)
    #              = ((0 - 3) ^ 2) + ((0 - 4) ^ 2)                  (zone 11 (0, 0), zone 12 (3, 4))
    #              = (-3 ^ 2) + (-4 ^ 2) = -1 + -2 = -3
    #                (^ is exclusive or: it flips bit 1 of each difference)
    #   save_bonus = dist = -3               (the NPC level bonus does not apply to a player)
    #   save_value = get_character_saving_throw(victim) + save_bonus    (spell_pa.cpp)
    #              = Mag / 3 + (Int - 8) / 4 + 0 + save_bonus           (0: not a hobbit)
    #              = 3 / 3   + (0 - 8) / 4   + 0 + -3                   (Mag:3, Int:[0/0/0])
    #              = 1 + -2 + 0 + -3 = -4
    #   casting_dc = 10 + Mag / 3 + (Int - 8) / 4 + Spell_Pen           (get_saving_throw_dc)
    #              = 10 + 30 / 3  + (40 - 8) / 4  + 0
    #                (Mag:30, Int:[40/40/40], Spell_Pen: 0)
    #              = 10 + 10 + 8 + 0 = 28
    #   saved      = roll + save_value > casting_dc, roll = number(1, 20)  (new_saves_spell)
    #              = at most 20 + -4 = 16, never above 28: never saves; -3 is inside (-20, 20),
    #                so new_saves_spell returns that result rather than a forced one.
    # Squared, dist would be (-3) * (-3) + (-4) * (-4) = 9 + 16 = 25, at or above
    # new_saves_spell's forced save at 20: the victim would always save.
    # In the caster's own zone, dist is ((0 - 0) ^ 2) + ((0 - 0) ^ 2) = 2 + 2 = 4, above -3.
    assert caster_inputs == (CASTER_MAGE_LEVEL, CASTER_INTELLIGENCE, 0), caster_inputs
    assert victim_inputs == (3, VICTIM_INTELLIGENCE, 0), victim_inputs
    caster_mage_level, caster_intelligence, caster_spell_pen = caster_inputs
    victim_mage_level, victim_intelligence = victim_inputs[:2]
    caster_x, caster_y = zone_map_square(11)
    victim_x, victim_y = zone_map_square(12)
    distance = ((caster_x - victim_x) ^ 2) + ((caster_y - victim_y) ^ 2)
    same_zone_distance = ((caster_x - caster_x) ^ 2) + ((caster_y - caster_y) ^ 2)
    save_value = c_divide(victim_mage_level, 3) + c_divide(victim_intelligence - 8, 4) + 0 + distance
    casting_dc = 10 + c_divide(caster_mage_level, 3) + c_divide(caster_intelligence - 8, 4) + caster_spell_pen
    squares = ((caster_x, caster_y), (victim_x, victim_y))
    assert squares == ((0, 0), (3, 4)), squares
    margin = (distance, same_zone_distance, save_value, casting_dc)
    assert margin == (-3, 4, -4, 28), margin
    assert distance < same_zone_distance, margin
    assert 20 + save_value <= casting_dc, margin

    reply = cast_summon(caster, DISTANT_VICTIM.name)
    assert ARRIVAL_FORMAT.format(DISTANT_VICTIM.name) in reply, reply
    summoned = settled_expect(victim, [SUMMONED_FORMAT.format(CASTER.name)])
    assert world.ROOM_ARENA_CENTRE_NAME in summoned, summoned


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, CASTER, DISTANT_DWARF)
def test_summon_across_zones_leaves_the_targets_stay_zone_gear_in_the_room_it_left(server) -> None:
    """Legacy pin: a target summoned out of its zone leaves its stay-zone gear where it stood.

    spell_summon (mage.cpp) calls prohibit_item_stay_zone_move (act_move.cpp) for a target in
    another zone before its save roll: the worn girdle comes off and lies in the room the
    target leaves, with no line, and the carried stone drops there with "You drop". Both stay
    even though the target is then summoned. The save cannot succeed (the distant-zone margin
    below), and the one draw left, the concentration roll, is repeated by cast_summon; the drop
    happens only in the cast that reaches spell_summon. Records today's behaviour. When what a
    summoned target leaves behind changes, this test becomes a strict xfail.
    """
    implementor = server.login(IMPLEMENTOR.name)
    caster = server.login(CASTER.name)
    victim = server.login(DISTANT_DWARF.name)
    goto(implementor, ROOM_DISTANT_CELL)
    stay_zone_items = (
        (world.OBJECT_STONE_GIRDLE, "girdle", world.OBJECT_STONE_GIRDLE_SHORT_DESCRIPTION),
        (world.OBJECT_DWARVEN_STONE, "stone", world.OBJECT_DWARVEN_STONE_SHORT_DESCRIPTION),
    )
    for vnum, keyword, short_description in stay_zone_items:
        give_new_object(
            implementor,
            vnum,
            keyword,
            DISTANT_DWARF.name.lower(),
            shown_recipient=DISTANT_DWARF.name,
            short_description=short_description,
        )
    worn = victim.command("wear girdle")
    assert worn.contains(GIRDLE_WORN_REPLY), worn.text
    caster_inputs, victim_inputs = setup_caster_and_victim(implementor, DISTANT_DWARF)

    # Distant case, caster in Arena Centre (zone 11), Testdelver in the Distant Cell (zone 12),
    # as in the distant-zone pin above, a dwarf in place of a human (no race term in the save):
    #   dist       = ((0 - 3) ^ 2) + ((0 - 4) ^ 2) = -1 + -2 = -3        (spell_summon, mage.cpp)
    #   save_value = Mag / 3 + (Int - 8) / 4 + 0 + dist                   (0: not a hobbit)
    #              = 3 / 3   + (0 - 8) / 4   + 0 + -3                     (Mag:3, Int:[0/0/0])
    #              = 1 + -2 + 0 + -3 = -4
    #   casting_dc = 10 + 30 / 3 + (40 - 8) / 4 + 0 = 28                  (Mag:30, Int:[40/40/40])
    #   saved      = number(1, 20) + -4 > 28: at most 16, never.
    assert caster_inputs == (CASTER_MAGE_LEVEL, CASTER_INTELLIGENCE, 0), caster_inputs
    assert victim_inputs == (3, VICTIM_INTELLIGENCE, 0), victim_inputs
    caster_mage_level, caster_intelligence, caster_spell_pen = caster_inputs
    victim_mage_level, victim_intelligence = victim_inputs[:2]
    caster_x, caster_y = zone_map_square(11)
    victim_x, victim_y = zone_map_square(12)
    distance = ((caster_x - victim_x) ^ 2) + ((caster_y - victim_y) ^ 2)
    save_value = c_divide(victim_mage_level, 3) + c_divide(victim_intelligence - 8, 4) + 0 + distance
    casting_dc = 10 + c_divide(caster_mage_level, 3) + c_divide(caster_intelligence - 8, 4) + caster_spell_pen
    margin = (distance, save_value, casting_dc)
    assert margin == (-3, -4, 28), margin
    assert 20 + save_value <= casting_dc, margin
    implementor.drain(SETTLE_SECONDS)

    reply = cast_summon(caster, DISTANT_DWARF.name)
    assert ARRIVAL_FORMAT.format(DISTANT_DWARF.name) in reply, reply
    summoned_line = SUMMONED_FORMAT.format(CASTER.name)
    summoned = settled_expect(victim, [summoned_line])
    stone_dropped = STAY_ZONE_DROP_FORMAT.format(world.OBJECT_DWARVEN_STONE_SHORT_DESCRIPTION)
    assert summoned.count(stone_dropped) == 1, summoned
    assert STAY_ZONE_DROP_FORMAT.format(world.OBJECT_STONE_GIRDLE_SHORT_DESCRIPTION) not in summoned, summoned
    assert summoned.index(stone_dropped) < summoned.index(summoned_line), summoned
    assert world.ROOM_ARENA_CENTRE_NAME in summoned, summoned
    watched = settled_expect(implementor, [DWARF_DROPS_STONE])
    assert watched.count(DWARF_DROPS_STONE) == 1, watched

    equipment = victim.command("equipment").text
    assert world.OBJECT_STONE_GIRDLE_SHORT_DESCRIPTION not in equipment, equipment
    carried = victim.command("inventory").text
    assert world.OBJECT_DWARVEN_STONE_SHORT_DESCRIPTION not in carried, carried
    floor = implementor.command("look").text
    assert floor.count(GIRDLE_LIES_HERE) == 1, floor
    assert floor.count(STONE_LIES_HERE) == 1, floor


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, CASTER, DISTANT_VICTIM)
def test_summon_tells_the_caster_its_target_left_the_world_when_the_target_quits_during_the_cast(server) -> None:
    """Fix pin (#534): a target who quits while summon is cast is not summoned.

    Quitting takes a mortal out of every room to the character menu (extract_char,
    handler.cpp) but keeps its character, so the cast's target still exists when the casting
    delay ends. do_cast (spell_pa.cpp) checks a TAR_CHAR_WORLD target for a room and stops with
    "Your victim has left the world." #534 made the cast stop there; before it, spell_summon ran
    on a target in no room and pulled it out of the menu. The check comes before the
    concentration roll, so nothing draws. Summon's 36 beats at mage level 30 are about four and a
    half seconds (CAST_TIMEOUT_SECONDS), and the quit is a single pass. The target's connection
    stays open until the cast ends: closing it would free the character.
    """
    server.login(IMPLEMENTOR.name)
    caster = server.login(CASTER.name)
    victim = server.login(DISTANT_VICTIM.name)

    cast_line = f"cast 'summon' {DISTANT_VICTIM.name.lower()}"
    started = reply_to(caster, cast_line, [CONCENTRATE_REPLY, NOBODY_BY_THAT_NAME_REPLY, NO_ENERGY_REPLY], settle=0)
    assert CONCENTRATE_REPLY in started, started
    quit_text = reply_to(victim, "quit", [QUIT_DROP_REPLY], settle=0)
    assert QUIT_DROP_REPLY in quit_text, quit_text

    outcomes = [
        LEFT_THE_WORLD_REPLY,
        NOBODY_BY_THAT_NAME_REPLY,
        FAILED_REPLY,
        LOST_CONCENTRATION_REPLY,
        PROTECTED_TARGET_START,
        UNSEEN_ARRIVAL,
        ARRIVAL_FORMAT.format(DISTANT_VICTIM.name),
    ]
    reply = settled_expect(caster, outcomes, timeout=CAST_TIMEOUT_SECONDS)
    assert LEFT_THE_WORLD_REPLY in reply, reply
    assert ARRIVAL_FORMAT.format(DISTANT_VICTIM.name) not in reply, reply
    menu = victim.drain(SETTLE_SECONDS)
    assert SUMMONED_FORMAT.format(CASTER.name) not in menu, menu
    victim.close()
