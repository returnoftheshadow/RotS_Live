"""Summon's pins: the dark-room block, the zone-distance arithmetic, the stay-zone gear a target
leaves behind across zones, and a target who quits while the spell is cast."""

from __future__ import annotations

import re
from pathlib import Path

import pytest

from rots_harness import world
from rots_harness.characters import IMPLEMENTOR, Character, StoredValues
from rots_harness.game_text import QUIT_DROP_REPLY, TELEPORTED_BY_ANYONE
from rots_harness.immortal_commands import give_new_object, set_holylight
from rots_harness.replies import reply_to
from rots_harness.session import GameSession
from rots_harness.stat_reading import MAGE_LEVEL_PATTERN, read_stat

pytestmark = [pytest.mark.scenario, pytest.mark.summon]

# Room 1201, the only room of zone 12, whose map square is (3, 4).
ROOM_DISTANT_CELL = 1201

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
# The short descriptions of the stone girdle (world.OBJECT_STONE_GIRDLE), worn about the waist,
# and the dwarven stone (world.OBJECT_DWARVEN_STONE), carried; both are STAY_ZONE.
GIRDLE = "a stone girdle"
STONE = "a dwarven stone"

# The caster's mage level: a single-class mage's mage level is its overall level, up to 30.
CASTER_MAGE_LEVEL = CASTER.level
# The test world's zone files, which hold each zone's map square.
ZONE_DIRECTORY = Path(__file__).resolve().parents[1] / "world" / "zon"

# What the caster reads when summon's target lookup finds nobody it can see (report_wrong_target,
# interpre.cpp, for a mask with TAR_CHAR_WORLD).
NOBODY_MARKER = "Nobody by that name."
# What the caster reads when the casting delay starts (do_cast, spell_pa.cpp).
CONCENTRATE_MARKER = "You start to concentrate."
# What the caster reads when the concentration roll fails (do_cast): number(0, 100) >= 100 still
# happens once in 101 casts at full knowledge.
LOST_CONCENTRATION_MARKER = "You lost your concentration!"
# What the caster reads when the victim saves or summon refuses the target (spell_summon).
FAILED_MARKER = "You failed."
# What the caster reads when its mana no longer covers a cast (can_cast_spell, spell_pa.cpp).
NO_ENERGY_MARKER = "You can't summon enough energy to cast the spell."
# The start of what the caster reads when do_cast's big_brother check protects the target
# (spell_pa.cpp; a writing or AFK target, big_brother.cpp).
PROTECTED_TARGET_MARKER = "You feel the Gods looking down upon you"
# What the caster reads when the victim has fought another player too recently (spell_summon).
RECENT_PK_MARKER = "Player has engaged in PK too recently to be summoned."
# What the caster reads, in place of the victim's name, when it cannot see the victim in the
# room the victim is leaving: spell_summon's act() runs PERS before the move.
UNSEEN_ARRIVAL_MARKER = "Someone appears in the room."
# What the caster reads when the victim it can see arrives (spell_summon); format with the name.
ARRIVAL_MARKER_FORMAT = "{} appears in the room."
# What the victim reads when it is moved (spell_summon); format with the caster's name.
SUMMONED_MARKER_FORMAT = "{} summons you!"
# What the caster reads when the target left the game while the spell was cast (do_cast,
# spell_pa.cpp, for a TAR_CHAR_WORLD target in no room).
LEFT_THE_WORLD_MARKER = "Your victim has left the world."
# What a summoned character reads for each stay-zone item it carries as it leaves its zone
# (prohibit_item_stay_zone_move, act_move.cpp); format with the short description. A worn one
# comes off with no line.
DROP_FORMAT = "You drop {}."
# What look prints instead of the room for a character who cannot see (do_look, act_info.cpp).
DARK_ROOM_LOOK = "It is pitch black..."
# Seconds to wait for a cast's outcome: summon's 36 beats at mage level 30 are 18 passes, four
# and a half seconds (CASTING_TIME, spells.h); 15 leaves room for a loaded host or emulated i386.
CAST_TIMEOUT = 15.0
# Seconds to keep reading after an outcome, for text already sent to arrive.
SETTLE_SECONDS = 1.0

# The stat lines new_saves_spell's inputs come from (do_stat_character, act_wiz.cpp).
CURRENT_INTELLIGENCE_PATTERN = re.compile(r"Int:\s*\[(-?\d+)/(-?\d+)/(-?\d+)\]")
SPELL_PEN_PATTERN = re.compile(r"Spell_Pen: (-?\d+),")
# A zone file's map line, `<symbol> <x> <y> <level>` (load_zones, zone.cpp).
ZONE_MAP_PATTERN = re.compile(r"^\S (-?\d+) (-?\d+) -?\d+$", re.MULTILINE)


def c_divide(numerator: int, denominator: int) -> int:
    """C's integer division, which truncates toward zero where Python's // floors."""
    return int(numerator / denominator)


def zone_map_square(zone_number: int) -> tuple:
    """The (x, y) map square in the test world's zone file, which spell_summon reads."""
    text = (ZONE_DIRECTORY / f"{zone_number}.zon").read_text()
    match = ZONE_MAP_PATTERN.search(text)
    assert match is not None, f"no map line in zone {zone_number}:\n{text}"
    return int(match.group(1)), int(match.group(2))


def save_inputs(implementor: GameSession, name: str) -> tuple:
    """The mage level, current intelligence and spell penetration stat shows for a character."""
    text = read_stat(implementor, f"player {name.lower()}")
    mage_level = MAGE_LEVEL_PATTERN.search(text)
    intelligence = CURRENT_INTELLIGENCE_PATTERN.search(text)
    spell_pen = SPELL_PEN_PATTERN.search(text)
    assert mage_level and intelligence and spell_pen, f"stat lines missing for {name}:\n{text}"
    return int(mage_level.group(1)), int(intelligence.group(1)), int(spell_pen.group(1))


def cast_summon(caster: GameSession, target_name: str) -> str:
    """Casts summon at target_name (as the game prints it); returns what the caster read after.

    A lost concentration is the one roll left that can stop the cast, so the cast is repeated
    after it. Each loss costs mana, and nothing regenerates mana between requests under
    affect-hours, which every test here uses, so the repeats end at NO_ENERGY_MARKER at the latest.
    """
    outcomes = [
        NOBODY_MARKER,
        FAILED_MARKER,
        LOST_CONCENTRATION_MARKER,
        NO_ENERGY_MARKER,
        PROTECTED_TARGET_MARKER,
        RECENT_PK_MARKER,
        UNSEEN_ARRIVAL_MARKER,
        ARRIVAL_MARKER_FORMAT.format(target_name),
    ]
    while True:
        reply = reply_to(caster, f"cast 'summon' {target_name.lower()}", outcomes, timeout=CAST_TIMEOUT)
        if LOST_CONCENTRATION_MARKER not in reply:
            return reply


def setup_caster_and_victim(implementor: GameSession, victim: Character) -> tuple:
    """Returns the caster's and the victim's save inputs.

    Holylight lets the implementor stat a character standing in the dark cell.
    """
    set_holylight(implementor, "on")
    return save_inputs(implementor, CASTER.name), save_inputs(implementor, victim.name)


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

    assert victim.command("look").room_name() == DARK_ROOM_LOOK
    caster_inputs, victim_inputs = setup_caster_and_victim(implementor, DARK_VICTIM)

    # Dark case, Testlost in the dark cell (1133, DARK, zone 11): no margin applies, because
    # target_from_word (interpre.cpp) finds no target the caster can see and do_cast returns
    # with report_wrong_target before the delay, the concentration roll or new_saves_spell.
    dark_reply = cast_summon(caster, DARK_VICTIM.name)
    assert NOBODY_MARKER in dark_reply, dark_reply
    assert CONCENTRATE_MARKER not in dark_reply, dark_reply
    assert victim.command("look").room_name() == DARK_ROOM_LOOK

    implementor.command(f"teleport {DARK_VICTIM.name.lower()} {world.ROOM_ARENA_WEST}")
    victim.expect([TELEPORTED_BY_ANYONE])
    victim.drain(SETTLE_SECONDS)

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
    assert ((caster_x, caster_y), (victim_x, victim_y)) == ((0, 0), (0, 0))
    assert (distance, save_value, casting_dc) == (4, 3, 28)
    assert 20 + save_value <= casting_dc

    lit_reply = cast_summon(caster, DARK_VICTIM.name)
    assert ARRIVAL_MARKER_FORMAT.format(DARK_VICTIM.name) in lit_reply, lit_reply
    summoned_marker = SUMMONED_MARKER_FORMAT.format(CASTER.name)
    summoned = victim.expect([summoned_marker]) + victim.drain(SETTLE_SECONDS)
    assert "Arena Centre" in summoned, summoned


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

    assert victim.command("look").room_name() == "Distant Cell"
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
    assert ((caster_x, caster_y), (victim_x, victim_y)) == ((0, 0), (3, 4))
    assert (distance, same_zone_distance, save_value, casting_dc) == (-3, 4, -4, 28)
    assert distance < same_zone_distance
    assert 20 + save_value <= casting_dc

    reply = cast_summon(caster, DISTANT_VICTIM.name)
    assert ARRIVAL_MARKER_FORMAT.format(DISTANT_VICTIM.name) in reply, reply
    summoned_marker = SUMMONED_MARKER_FORMAT.format(CASTER.name)
    summoned = victim.expect([summoned_marker]) + victim.drain(SETTLE_SECONDS)
    assert "Arena Centre" in summoned, summoned


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, CASTER, DISTANT_DWARF)
def test_summon_across_zones_leaves_the_targets_stay_zone_gear_in_the_room_it_left(server) -> None:
    """Legacy pin: a target summoned out of its zone leaves its stay-zone gear where it stood.

    spell_summon (mage.cpp) calls prohibit_item_stay_zone_move (act_move.cpp) for a target in
    another zone before its save roll: the worn girdle comes off and lies in the room the
    target leaves, with no line, and the carried stone drops there with "You drop". Both stay
    even though the target is then summoned. The save cannot succeed (the distant-zone margin
    below), and the one draw left, the concentration roll, is repeated by cast_summon; the drop
    happens only in the cast that reaches spell_summon.
    """
    implementor = server.login(IMPLEMENTOR.name)
    caster = server.login(CASTER.name)
    victim = server.login(DISTANT_DWARF.name)
    implementor.command(f"goto {ROOM_DISTANT_CELL}")
    stay_zone_items = ((world.OBJECT_STONE_GIRDLE, "girdle", GIRDLE), (world.OBJECT_DWARVEN_STONE, "stone", STONE))
    for vnum, keyword, short_description in stay_zone_items:
        give_new_object(implementor, vnum, keyword, DISTANT_DWARF.name.lower(), shown_recipient=DISTANT_DWARF.name, short_description=short_description)
    worn = victim.command("wear girdle")
    assert worn.contains(f"You wear {GIRDLE} around your waist."), worn.text
    caster_inputs, victim_inputs = setup_caster_and_victim(implementor, DISTANT_DWARF)

    # Distant case, caster in Arena Centre (zone 11), Testdelver in the Distant Cell (zone 12),
    # as in the distant-zone pin above, a dwarf in place of a human (no race term in the save):
    #   dist       = ((0 - 3) ^ 2) + ((0 - 4) ^ 2) = -1 + -2 = -3        (spell_summon, mage.cpp)
    #   save_value = Mag / 3 + (Int - 8) / 4 + 0 + dist                   (0: not a hobbit)
    #              = 3 / 3   + (0 - 8) / 4   + 0 + -3 = 1 + -2 + 0 + -3 = -4   (Mag:3, Int:[0/0/0])
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
    assert (distance, save_value, casting_dc) == (-3, -4, 28)
    assert 20 + save_value <= casting_dc
    implementor.drain(SETTLE_SECONDS)

    reply = cast_summon(caster, DISTANT_DWARF.name)
    assert ARRIVAL_MARKER_FORMAT.format(DISTANT_DWARF.name) in reply, reply
    summoned = victim.expect([SUMMONED_MARKER_FORMAT.format(CASTER.name)]) + victim.drain(SETTLE_SECONDS)
    assert summoned.count(DROP_FORMAT.format(STONE)) == 1, summoned
    assert DROP_FORMAT.format(GIRDLE) not in summoned, summoned
    assert summoned.index(DROP_FORMAT.format(STONE)) < summoned.index(SUMMONED_MARKER_FORMAT.format(CASTER.name)), summoned
    assert "Arena Centre" in summoned, summoned
    watched = implementor.expect([f"{DISTANT_DWARF.name} drops {STONE}."]) + implementor.drain(SETTLE_SECONDS)
    assert watched.count(f"{DISTANT_DWARF.name} drops {STONE}.") == 1, watched

    equipment = victim.command("equipment").text
    assert GIRDLE not in equipment, equipment
    carried = victim.command("inventory").text
    assert STONE not in carried, carried
    floor = implementor.command("look").text
    assert floor.count("A stone girdle lies here.") == 1, floor
    assert floor.count("A dwarven stone lies here.") == 1, floor


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, CASTER, DISTANT_VICTIM)
def test_summon_tells_the_caster_its_target_left_the_world_when_the_target_quits_during_the_cast(server) -> None:
    """Fix pin (#534): a target who quits while summon is cast is not summoned.

    Quitting takes a mortal out of every room to the character menu (extract_char,
    handler.cpp) but keeps its character, so the cast's target still exists when the casting
    delay ends. do_cast (spell_pa.cpp) checks a TAR_CHAR_WORLD target for a room and stops with
    "Your victim has left the world." #534 made the cast stop there; before it, spell_summon ran
    on a target in no room and pulled it out of the menu. The check comes before the
    concentration roll, so nothing draws. Summon's 36 beats at mage level 30 are about four and a half seconds
    (CAST_TIMEOUT), and the quit is a single pass. The target's connection stays open until
    the cast ends: closing it would free the character.
    """
    server.login(IMPLEMENTOR.name)
    caster = server.login(CASTER.name)
    victim = server.login(DISTANT_VICTIM.name)

    caster.send_line(f"cast 'summon' {DISTANT_VICTIM.name.lower()}")
    started = caster.expect([CONCENTRATE_MARKER, NOBODY_MARKER, NO_ENERGY_MARKER])
    assert CONCENTRATE_MARKER in started, started
    victim.send_line("quit")
    quit_text = victim.expect([QUIT_DROP_REPLY])
    assert QUIT_DROP_REPLY in quit_text, quit_text

    outcomes = [
        LEFT_THE_WORLD_MARKER,
        NOBODY_MARKER,
        FAILED_MARKER,
        LOST_CONCENTRATION_MARKER,
        PROTECTED_TARGET_MARKER,
        UNSEEN_ARRIVAL_MARKER,
        ARRIVAL_MARKER_FORMAT.format(DISTANT_VICTIM.name),
    ]
    reply = caster.expect(outcomes, timeout=CAST_TIMEOUT) + caster.drain(SETTLE_SECONDS)
    assert LEFT_THE_WORLD_MARKER in reply, reply
    assert ARRIVAL_MARKER_FORMAT.format(DISTANT_VICTIM.name) not in reply, reply
    menu = victim.drain(SETTLE_SECONDS)
    assert SUMMONED_MARKER_FORMAT.format(CASTER.name) not in menu, menu
    victim.close()
