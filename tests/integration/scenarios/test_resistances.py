"""Protection and resistances: a mystic's protection in every sphere, on itself and on others,
and `unprotect`; worn item resistances and vulnerabilities; how protection and items combine,
what a save and a new login keep, and the debug trace of both."""

from __future__ import annotations

import dataclasses
import json
import re

import pytest

from rots_harness import world
from rots_harness.characters import IMPLEMENTOR, Character, StoredValues
from rots_harness.session import GameSession

pytestmark = [pytest.mark.scenario, pytest.mark.resistances]

# The spirit each mystic is stored with: protection costs 5 (skills[] in consts.cpp) and a lost
# concentration 2, so this pays for every cast a test makes and leaves room for losses.
MYSTIC_SPIRIT = 40
# The spirit for the test that casts once in each of five spheres.
SPHERE_LOOP_SPIRIT = 80

# A level-20 human mystic in the arena centre who knows protection: ten practice sessions put
# protection's knowledge at the cap of 100 (learn_diff 1, recalc_skills in spec_pro.cpp).
MYSTIC = Character(
    name="Testmystic",
    race="human",
    sex="female",
    level=20,
    load_room=world.ROOM_ARENA_CENTRE,
    class_name="mystic",
    skills=(("protection", 10),),
    stored=StoredValues(spirit=MYSTIC_SPIRIT),
)
# A level-30 human mystic in the arena centre who knows protection: its mystic level is at the
# profession cap of 30.
SAGE = Character(
    name="Testsage",
    race="human",
    sex="female",
    level=30,
    load_room=world.ROOM_ARENA_CENTRE,
    class_name="mystic",
    skills=(("protection", 10),),
    stored=StoredValues(spirit=MYSTIC_SPIRIT),
)
# The level-20 mystic with the spirit for five casts.
SPHERE_LOOP_MYSTIC = dataclasses.replace(MYSTIC, stored=StoredValues(spirit=SPHERE_LOOP_SPIRIT))
# A level-20 human warrior in the arena centre who wears the fire fixtures and receives the
# mystic's protection; a warrior's mystic level is low.
WEARER = Character(
    name="Testwearer",
    race="human",
    sex="male",
    level=20,
    load_room=world.ROOM_ARENA_CENTRE,
    class_name="warrior",
)

# The possessive pronoun act()'s $s and $S print for each sex (HSHR, utils.h).
POSSESSIVE_PRONOUNS = {"female": "her", "male": "his"}

# The highest mystic level cast_resist_magnitude (mystic.cpp) counts; it adds ten to the level.
RESIST_LEVEL_CAP = 30
RESIST_LEVEL_BONUS = 10

# The fire-resist fixtures (tests/integration/world/obj/14.obj): a head item granting 20% and a
# neck item granting 40%, each through `A 27 <strength * 256 + 161>` (APPLY_SPELL with
# SPELL_RESIST_FIRE, spells.h). The worn markers are what wearing each prints (wear_message,
# act_obj2.cpp).
WEAK_ITEM_VNUM = 1400
WEAK_ITEM_KEYWORD = "circlet"
WEAK_ITEM_SHORT = "a fire circlet"
WEAK_ITEM_WORN_MARKER = "You wear a fire circlet on your head."
WEAK_ITEM_PERCENT = 20
STRONG_ITEM_VNUM = 1401
STRONG_ITEM_KEYWORD = "pendant"
STRONG_ITEM_SHORT = "a fire pendant"
STRONG_ITEM_WORN_MARKER = "You wear a fire pendant around your neck."
STRONG_ITEM_PERCENT = 40
# A finger item whose `A 30 1` (APPLY_RESIST, RESIST_FIRE) sets the fire resistance bit with no
# affect behind it.
FLAG_RESIST_ITEM_VNUM = 1402
FLAG_RESIST_ITEM_KEYWORD = "ring"
FLAG_RESIST_ITEM_WORN_MARKER = "You slide an ember ring on to your right ring finger."
# A finger item whose `A 31 1` (APPLY_VULN) makes the wearer vulnerable to fire.
VULNERABILITY_ITEM_VNUM = 1403
VULNERABILITY_ITEM_KEYWORD = "band"
VULNERABILITY_ITEM_WORN_MARKER = "You slide a tinder band on to your right ring finger."
# The poison amulet in obj/11.obj, whose wearing deals poison damage (equip_char, handler.cpp).
POISON_AMULET_VNUM = 1138
POISON_AMULET_KEYWORD = "amulet"

# The strength affections shows for a resistance bit with no affect behind it, and for any
# vulnerability (the default_percent do_affections passes to sprintbit_resistances, act_info.cpp).
FLAG_RESIST_PERCENT = 33
VULNERABILITY_PERCENT = 50

# What the caster reads when the concentration roll fails (do_cast): number(0, 100) >= 100 still
# happens once in 101 casts at full knowledge.
LOST_CONCENTRATION_MARKER = "You lost your concentration!"
# What the caster reads when its spirit no longer covers a cast (can_cast_spell, spell_pa.cpp).
NO_ENERGY_MARKER = "You can't summon enough energy to cast the spell."
# What the caster reads after paying for a cast, before the spell runs (do_cast).
CAST_PAID_MARKER = "Ok."
# What the protected character reads (spell_protection, mystic.cpp, and do_resist_spell for an
# item); format with the sphere's word.
RESISTANT_MARKER_FORMAT = "You feel resistant to {}!"
RESISTANT_MARKER_START = "You feel resistant to"
DARK_PROTECTION_MARKER = RESISTANT_MARKER_FORMAT.format("dark")
FIRE_RESISTANT_MARKER = RESISTANT_MARKER_FORMAT.format("fire")
# What the caster reads for a protection cast on someone else (spell_protection); format with
# the name and the sphere's word.
GRANT_MARKER_FORMAT = "You grant {} resistance to {}."
GRANT_MARKER_START = "You grant "
# What a caster already protected reads on a second cast (spell_protection).
ALREADY_PROTECTED_MARKER = "You have protection already."
# What the caster reads when the character it names is already protected (spell_protection);
# format with the name and its possessive pronoun.
OTHER_ALREADY_PROTECTED_FORMAT = "{} has {} protection already."
OTHER_ALREADY_PROTECTED_END = "protection already."
# What the caster reads when nobody in the room answers to the name (spell_protection).
NOBODY_MARKER = "Nobody here by that name."
# What a caster reads for a sphere protection does not know (spell_protection); the line breaks
# after "illusion".
WRONG_SPHERE_MARKER = "You can master protection from fire, cold, lightning, physical, illusion\n\ror dark only."
# The start of that reply, which cast_protection waits for so that a changed sphere list fails on
# the assertion rather than as a timeout.
WRONG_SPHERE_START = "You can master protection from"
# What the rest of the room reads while a protection is cast (do_cast and say_spell,
# spell_pa.cpp); format with the caster's name.
MUTTER_ROOM_MARKER_FORMAT = "{} begins quietly muttering some strange, powerful words."
UTTER_ROOM_MARKER_FORMAT = "{} utters a strange command, 'protection'"
# What the character and its room read when it drops its protection (do_unprotect, mystic.cpp);
# format the room's line with the name and the possessive pronoun.
RENOUNCED_MARKER = "You renounce your protection!"
RENOUNCED_ROOM_MARKER_FORMAT = "{} renounces {} protection!"
# What unprotect prints when there is no protection to drop (do_unprotect).
NOTHING_TO_RENOUNCE_MARKER = "You renounce yourself to the world, but nothing happens..."
# What a resting character reads for a command that needs it standing (command_interpreter,
# interpre.cpp; unprotect's minimum position), and what `rest` prints (do_rest, act_move.cpp).
TOO_RELAXED_MARKER = "Nah... You feel too relaxed to do that.."
RESTING_MARKER = "You sit down and rest your tired bones."
# What affections prints for a character with no affect or resistance (do_affections,
# act_info.cpp).
NOT_AFFECTED_MARKER = "You are not affected by anything."
# The headers affections prints above the resistance, vulnerability and affect lines
# (do_affections).
RESISTANT_HEADER = "You are resistant to:"
VULNERABLE_HEADER = "You are vulnerable to:"
AFFECTED_HEADER = "You are affected by:"
# One resistance or vulnerability line in affections (sprintbit_resistances, utility.cpp); format
# with the element and the strength in percent.
RESISTANCE_LINE_FORMAT = "   {} ({}%)"
# The affect lines affections prints (report_affection, act_info.cpp). A cast protection lasts
# twice get_mystic_caster_level (mystic.cpp), the mystic level plus a will share, so at least 20
# hours for every caster here, and 12 hours or more reads "long"; an item's affect never runs
# out (duration -1) and reads "permanent".
PROTECTION_AFFECT_PATTERN = re.compile(r"protection +\(long\)")
ITEM_RESIST_AFFECT_PATTERN = re.compile(r"resist fire +\(permanent\)")
# What the wearer reads on taking an item off (perform_remove, act_obj2.cpp); format with the
# item's short description.
STOP_USING_MARKER_FORMAT = "You stop using {}."
# What a command above the speaker's level prints (command_interpreter, interpre.cpp).
UNRECOGNIZED_MARKER = "Unrecognized command."
# What debug prints as it turns the flag on and off (do_debug, act_wiz.cpp).
DEBUG_ON_MARKER = "Debug flag on."
DEBUG_OFF_MARKER = "Debug flag off."
# The debug lines for the fire pendant: affect_modify's APPLY_SPELL decode (handler.cpp), run on
# wearing and on removing, and install_resist_affect (mystic.cpp), run when the item takes the
# fire slot.
APPLY_SPELL_DEBUG_LINE = f"--APPLY_SPELL: spell 161 level {STRONG_ITEM_PERCENT}"
RESIST_DEBUG_LINE = f"::RESIST::apply type 161 modifier 1 eff_mod {STRONG_ITEM_PERCENT} duration -1"
# The debug line damage() (fight.cpp) prints for the 5 poison damage the amulet deals on
# wearing: SPELL_POISON is attack type 43 and matches no resistance.
POISON_DAMAGE_DEBUG_LINE = "::DAMAGE:: attacktype 43 resist_type 0 check 0 dam 5"
# What the implementor reads when wizset sets the hunger (do_wizset, act_wiz.cpp), which also
# saves the character it changed.
HUNGER_SET_MARKER_FORMAT = "{}'s hunger set to 10."
# Seconds to wait for a cast's outcome: protection's 21 beats take a few seconds (CASTING_TIME,
# spells.h); 15 leaves room for a loaded host or emulated i386.
CAST_TIMEOUT = 15.0
# Seconds to keep reading after an outcome, for text already sent to arrive.
SETTLE_SECONDS = 1.0
# The stat line just before the affect lines (do_stat_character, act_wiz.cpp): once it arrives,
# the drain collects the rest of the reply.
STAT_LAST_LINE_MARKER = "VUL:"
# The room every character in this file starts in, as look names it.
ARENA_CENTRE_NAME = "Arena Centre"

# The stat line with the profession levels (do_stat_character, act_wiz.cpp).
CLERIC_LEVEL_PATTERN = re.compile(r"Class levels: Mag:\d+ Cle:(\d+) ")
# The resistance bits stat prints (do_stat_character): "RES:  FIRE. VUL: ...".
STAT_RESISTANCES_PATTERN = re.compile(r"RES: (.*?) VUL:")
# One affect line in stat (do_stat_character); the name is padded to 21 characters.
STAT_AFFECT_PATTERN = re.compile(r"SPL: \(\s*-?\d+hr\) (\S+(?: \S+)*)\s+[+-]\d+ to ")

# A protection affect as the character file stores it (char_to_store, db.cpp; SPELL_PROTECTION,
# APPLY_RESIST and RESIST_FIRE in spells.h and structs.h).
PROTECTION_AFFECT_TYPE = 89
RESIST_FIRE_AFFECT_TYPE = 161
APPLY_RESIST_LOCATION = 30
RESIST_FIRE_MODIFIER = 1


def possessive(character: Character) -> str:
    """The pronoun act() prints for the character's $s or $S."""
    return POSSESSIVE_PRONOUNS[character.sex]


def stat_player(implementor: GameSession, character: Character) -> str:
    """The whole reply to the implementor's `stat player` for the character."""
    implementor.send_line(f"stat player {character.name.lower()}")
    return implementor.expect([STAT_LAST_LINE_MARKER]) + implementor.drain(SETTLE_SECONDS)


def cleric_level(implementor: GameSession, character: Character) -> int:
    """The mystic profession level stat shows for the character."""
    text = stat_player(implementor, character)
    match = CLERIC_LEVEL_PATTERN.search(text)
    assert match is not None, f"no class levels line for {character.name}:\n{text}"
    return int(match.group(1))


def resist_percent(level: int) -> int:
    """The strength cast_resist_magnitude (mystic.cpp) gives a mystic level."""
    return min(level, RESIST_LEVEL_CAP) + RESIST_LEVEL_BONUS


def cast_protection(caster: GameSession, sphere: str, target: str = "") -> str:
    """Casts protection from sphere on the caster, or on target; returns what the caster read.

    A lost concentration is the one roll that can stop the cast, so the cast is repeated after
    it. Each loss costs spirit, and nothing regenerates spirit between requests under
    affect-hours, which every test here uses, so the repeats end at NO_ENERGY_MARKER at the latest.
    """
    outcomes = [
        RESISTANT_MARKER_START,
        GRANT_MARKER_START,
        ALREADY_PROTECTED_MARKER,
        OTHER_ALREADY_PROTECTED_END,
        NOBODY_MARKER,
        WRONG_SPHERE_START,
        LOST_CONCENTRATION_MARKER,
        NO_ENERGY_MARKER,
    ]
    line = f"cast 'protection' {sphere} {target}".rstrip()
    while True:
        caster.send_line(line)
        reply = caster.expect(outcomes, timeout=CAST_TIMEOUT) + caster.drain(SETTLE_SECONDS)
        if LOST_CONCENTRATION_MARKER not in reply:
            return reply


def affections(session: GameSession) -> str:
    """What the character's affections command prints."""
    return session.command("affections").text


def goto_character(implementor: GameSession, character: Character) -> None:
    """Moves the implementor to the character, who is in the arena centre."""
    arrived = implementor.command(f"goto {character.name.lower()}")
    assert arrived.room_name() == ARENA_CENTRE_NAME, arrived.text


def give_items(implementor: GameSession, recipient: Character, items: tuple[tuple[int, str], ...]) -> None:
    """Loads each (vnum, keyword) item and gives it to the recipient, in the implementor's room."""
    for vnum, keyword in items:
        loaded = implementor.command(f"load obj {vnum}")
        assert loaded.contains("You create"), loaded.text
        given = implementor.command(f"give {keyword} {recipient.name.lower()}")
        assert given.contains("You give"), given.text


def protect_from_dark(server, caster: Character = MYSTIC) -> tuple:
    """Logs in the caster and has it cast protection dark.

    Returns the caster's session and what it read after the cast.
    """
    mystic = server.login(caster.name)
    return mystic, cast_protection(mystic, "dark")


def hand_out_items(server, items: tuple[tuple[int, str], ...]) -> tuple:
    """Logs in the implementor and the wearer, and gives the wearer the (vnum, keyword) items."""
    implementor = server.login(IMPLEMENTOR.name)
    wearer = server.login(WEARER.name)
    goto_character(implementor, WEARER)
    give_items(implementor, WEARER, items)
    return implementor, wearer


def hand_out_fire_items(server) -> tuple:
    """Logs in the implementor and the wearer and gives the wearer both fire-resist items."""
    return hand_out_items(server, ((WEAK_ITEM_VNUM, WEAK_ITEM_KEYWORD), (STRONG_ITEM_VNUM, STRONG_ITEM_KEYWORD)))


def protect_wearer_from_fire(mystic: GameSession) -> str:
    """Has the mystic cast protection fire on the wearer; returns what the mystic read."""
    reply = cast_protection(mystic, "fire", WEARER.name.lower())
    assert GRANT_MARKER_FORMAT.format(WEARER.name, "fire") in reply, reply
    return reply


def resistance_line(element: str, percent: int) -> str:
    """One resistance or vulnerability line as affections prints it."""
    return RESISTANCE_LINE_FORMAT.format(element, percent)


def line_start(element: str) -> str:
    """The start of an element's resistance or vulnerability line, before its strength."""
    return RESISTANCE_LINE_FORMAT.format(element, "").removesuffix("%)")


def fire_lines(shown: str) -> int:
    """How many fire lines affections printed under its resistance header."""
    resistances = shown.split(RESISTANT_HEADER, 1)[1] if RESISTANT_HEADER in shown else ""
    resistances = resistances.split(VULNERABLE_HEADER, 1)[0].split(AFFECTED_HEADER, 1)[0]
    return resistances.count(line_start("fire"))


@pytest.mark.resistances_protection
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, MYSTIC, SAGE)
@pytest.mark.parametrize("caster", [MYSTIC, SAGE], ids=["level-20", "level-30"])
def test_a_mystic_casts_protection_from_dark_and_affections_shows_its_strength(server, caster) -> None:
    """Protection from dark grants a dark resistance worth the mystic level plus ten (PR #342).

    The one draw that can stop the cast is the concentration roll in do_cast, which
    cast_protection repeats; the strength itself comes from the profession level, not from a
    roll (cast_resist_magnitude, mystic.cpp).
    """
    implementor = server.login(IMPLEMENTOR.name)
    mystic, reply = protect_from_dark(server, caster)
    assert DARK_PROTECTION_MARKER in reply, reply

    # Testmystic (level-20 mystic):
    #   effect_modifier = cast_resist_magnitude(get_prof_level(PROF_CLERIC, caster))  (mystic.cpp)
    #                   = (level > 30 ? 30 : level) + 10
    #                   = (20 > 30 ? 30 : 20) + 10        (Class levels: Cle:20)
    #                   = 30
    # Testsage (level-30 mystic):
    #   effect_modifier = (30 > 30 ? 30 : 30) + 10        (Class levels: Cle:30)
    #                   = 40
    level = cleric_level(implementor, caster)
    assert level == caster.level, level
    expected_percent = resist_percent(level)
    assert expected_percent == {MYSTIC.name: 30, SAGE.name: 40}[caster.name]

    shown = affections(mystic)
    assert RESISTANT_HEADER in shown, shown
    assert resistance_line("dark", expected_percent) in shown, shown
    assert PROTECTION_AFFECT_PATTERN.search(shown), shown


@pytest.mark.resistances_protection
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(MYSTIC)
def test_a_mystic_already_protected_is_refused_a_protection_from_another_sphere(server) -> None:
    """A protection cast while one holds is refused whatever its sphere (spell_protection).

    spell_protection checks for a protection before it reads the sphere, so a fire cast over a
    dark protection is refused and dark stays. The one draw that can stop either cast is the
    concentration roll in do_cast, which cast_protection repeats.
    """
    mystic, first_reply = protect_from_dark(server)
    assert DARK_PROTECTION_MARKER in first_reply, first_reply

    second_reply = cast_protection(mystic, "fire")
    assert ALREADY_PROTECTED_MARKER in second_reply, second_reply
    assert FIRE_RESISTANT_MARKER not in second_reply, second_reply

    shown = affections(mystic)
    assert resistance_line("dark", 30) in shown, shown
    assert fire_lines(shown) == 0, shown


@pytest.mark.resistances_protection
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(MYSTIC)
def test_unprotect_frees_the_mystic_for_a_protection_from_another_sphere(server) -> None:
    """After unprotect a protection from another sphere takes, and replaces the first (PR #315).

    The one draw that can stop either cast is the concentration roll in do_cast, which
    cast_protection repeats; unprotect itself draws nothing.
    """
    mystic, first_reply = protect_from_dark(server)
    assert DARK_PROTECTION_MARKER in first_reply, first_reply
    assert mystic.command("unprotect").contains(RENOUNCED_MARKER)

    second_reply = cast_protection(mystic, "fire")
    assert FIRE_RESISTANT_MARKER in second_reply, second_reply

    shown = affections(mystic)
    assert resistance_line("fire", 30) in shown, shown
    assert resistance_line("dark", 30) not in shown, shown


@pytest.mark.resistances_protection
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(MYSTIC)
def test_protection_names_dark_among_its_spheres_when_given_one_it_does_not_know(server) -> None:
    """Protection from an unknown sphere lists the six it knows, dark last (PR #342).

    The one draw that can stop the cast is the concentration roll in do_cast, which
    cast_protection repeats.
    """
    mystic = server.login(MYSTIC.name)

    reply = cast_protection(mystic, "water")
    assert WRONG_SPHERE_MARKER in reply, reply
    assert NOT_AFFECTED_MARKER in affections(mystic)


@pytest.mark.resistances_protection
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(SPHERE_LOOP_MYSTIC)
def test_protection_in_each_other_sphere_grants_that_element_at_the_mystic_strength(server) -> None:
    """Protection from fire, cold, lightning, physical and illusion each grant their element (PR #315).

    Each cast reads its own message and affections shows that element alone at 30% with a long
    protection affect. The spheres are typed as a prefix (`phys`) and in capitals (`ILL`) as
    well as in full: spell_protection matches a lowercased prefix (search_block, interpre.cpp).
    The one draw that can stop a cast is the concentration roll in do_cast, which
    cast_protection repeats; unprotect draws nothing.
    """
    mystic = server.login(SPHERE_LOOP_MYSTIC.name)
    # (what the caster types, the element in the cast message, the element in affections)
    spheres = (
        ("fire", "fire", "fire"),
        ("cold", "cold", "cold"),
        ("lightning", "lightning", "lightning"),
        ("phys", "physical harm", "physical"),
        ("ILL", "illusion", "illusion"),
    )
    shown_elements = [shown_element for _, _, shown_element in spheres] + ["dark"]

    for typed, message_element, shown_element in spheres:
        reply = cast_protection(mystic, typed)
        assert RESISTANT_MARKER_FORMAT.format(message_element) in reply, (typed, reply)

        shown = affections(mystic)
        assert resistance_line(shown_element, 30) in shown, (typed, shown)
        for other in shown_elements:
            if other != shown_element:
                assert line_start(other) not in shown, (typed, shown)
        assert PROTECTION_AFFECT_PATTERN.search(shown), (typed, shown)

        assert mystic.command("unprotect").contains(RENOUNCED_MARKER), typed


@pytest.mark.resistances_protection
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, MYSTIC, WEARER)
def test_protection_cast_on_another_tells_each_observer_and_uses_the_casters_strength(server) -> None:
    """Protection cast on another grants the caster's strength, not the target's (PR #315).

    The caster reads the grant, the target reads its resistance, and a bystander reads only the
    casting. The one draw that can stop the cast is the concentration roll in do_cast, which
    cast_protection repeats; a protection cast takes text, not a character, so do_cast's
    hallucination check does not run.
    """
    implementor = server.login(IMPLEMENTOR.name)
    mystic = server.login(MYSTIC.name)
    wearer = server.login(WEARER.name)
    goto_character(implementor, WEARER)

    reply = protect_wearer_from_fire(mystic)
    assert CAST_PAID_MARKER in reply, reply
    assert FIRE_RESISTANT_MARKER not in reply, reply

    mutter = MUTTER_ROOM_MARKER_FORMAT.format(MYSTIC.name)
    utter = UTTER_ROOM_MARKER_FORMAT.format(MYSTIC.name)
    target_saw = wearer.expect([FIRE_RESISTANT_MARKER]) + wearer.drain(SETTLE_SECONDS)
    assert mutter in target_saw and utter in target_saw, target_saw
    assert GRANT_MARKER_START not in target_saw, target_saw
    bystander_saw = implementor.expect([utter]) + implementor.drain(SETTLE_SECONDS)
    assert mutter in bystander_saw, bystander_saw
    assert RESISTANT_MARKER_START not in bystander_saw, bystander_saw
    assert GRANT_MARKER_START not in bystander_saw, bystander_saw

    # Testwearer, protected by Testmystic:
    #   effect_modifier = cast_resist_magnitude(get_prof_level(PROF_CLERIC, caster))  (mystic.cpp)
    #                   = (20 > 30 ? 30 : 20) + 10        (Testmystic, Class levels: Cle:20)
    #                   = 30
    # The target's own mystic level would give (8 > 30 ? 30 : 8) + 10 = 18 (Testwearer, Cle:8).
    caster_level = cleric_level(implementor, MYSTIC)
    target_level = cleric_level(implementor, WEARER)
    assert (caster_level, target_level) == (20, 8)
    assert (resist_percent(caster_level), resist_percent(target_level)) == (30, 18)

    shown = affections(wearer)
    assert resistance_line("fire", 30) in shown, shown
    assert resistance_line("fire", 18) not in shown, shown
    assert PROTECTION_AFFECT_PATTERN.search(shown), shown


@pytest.mark.resistances_protection
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, MYSTIC, WEARER)
def test_protection_cast_on_another_already_protected_is_refused_with_their_pronoun(server) -> None:
    """A protection aimed at someone already protected is refused, naming them (spell_protection).

    The one draw that can stop either cast is the concentration roll in do_cast, which
    cast_protection repeats.
    """
    implementor = server.login(IMPLEMENTOR.name)
    mystic = server.login(MYSTIC.name)
    server.login(WEARER.name)
    protect_wearer_from_fire(mystic)

    second_reply = cast_protection(mystic, "cold", WEARER.name.lower())
    refusal = OTHER_ALREADY_PROTECTED_FORMAT.format(WEARER.name, possessive(WEARER))
    assert refusal in second_reply, second_reply
    assert GRANT_MARKER_START not in second_reply, second_reply


@pytest.mark.resistances_protection
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(MYSTIC)
def test_protection_cast_on_a_name_nobody_here_answers_to_is_refused(server) -> None:
    """A protection aimed at a name nobody in the room has is refused (spell_protection).

    The one draw that can stop the cast is the concentration roll in do_cast, which
    cast_protection repeats.
    """
    mystic = server.login(MYSTIC.name)

    reply = cast_protection(mystic, "fire", "nobody")
    assert NOBODY_MARKER in reply, reply
    assert NOT_AFFECTED_MARKER in affections(mystic)


@pytest.mark.resistances_protection
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, MYSTIC)
def test_unprotect_drops_protection_and_a_second_unprotect_does_nothing(server) -> None:
    """unprotect drops a protection and tells the room; without one it does nothing (PR #315).

    The one draw that can stop the cast is the concentration roll in do_cast, which
    cast_protection repeats; unprotect itself draws nothing.
    """
    implementor = server.login(IMPLEMENTOR.name)
    mystic, reply = protect_from_dark(server)
    assert DARK_PROTECTION_MARKER in reply, reply
    goto_character(implementor, MYSTIC)

    renounced = mystic.command("unprotect")
    assert renounced.contains(RENOUNCED_MARKER), renounced.text
    room_marker = RENOUNCED_ROOM_MARKER_FORMAT.format(MYSTIC.name, possessive(MYSTIC))
    seen = implementor.expect([room_marker]) + implementor.drain(SETTLE_SECONDS)
    assert room_marker in seen, seen
    assert NOT_AFFECTED_MARKER in affections(mystic)

    again = mystic.command("unprotect")
    assert again.contains(NOTHING_TO_RENOUNCE_MARKER), again.text
    assert not again.contains(RENOUNCED_MARKER), again.text


@pytest.mark.resistances_protection
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(MYSTIC)
def test_a_resting_mystic_cannot_unprotect(server) -> None:
    """unprotect needs its user standing, so a resting mystic keeps its protection (PR #315).

    The one draw that can stop the cast is the concentration roll in do_cast, which
    cast_protection repeats.
    """
    mystic, reply = protect_from_dark(server)
    assert DARK_PROTECTION_MARKER in reply, reply
    assert mystic.command("rest").contains(RESTING_MARKER)

    refused = mystic.command("unprotect")
    assert refused.contains(TOO_RELAXED_MARKER), refused.text
    assert not refused.contains(RENOUNCED_MARKER), refused.text
    assert resistance_line("dark", 30) in affections(mystic)


@pytest.mark.resistances_protection
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(MYSTIC)
def test_a_mystic_keeps_its_protection_and_strength_across_quit_and_login(server) -> None:
    """A protection is saved on quit with its strength and returns on the next login (PR #315).

    The strength comes back from the character file, not the flat 33% of a bare resistance
    bit. The one draw that can stop the cast is the concentration roll in do_cast, which
    cast_protection repeats.
    """
    mystic, reply = protect_from_dark(server)
    assert DARK_PROTECTION_MARKER in reply, reply

    mystic.quit()
    again = server.login(MYSTIC.name)

    shown = affections(again)
    assert resistance_line("dark", 30) in shown, shown
    assert resistance_line("dark", FLAG_RESIST_PERCENT) not in shown, shown
    assert PROTECTION_AFFECT_PATTERN.search(shown), shown


@pytest.mark.resistances_worn
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, MYSTIC, WEARER)
def test_a_save_keeps_the_protection_and_drops_the_worn_items_resistance(server) -> None:
    """A character file keeps a cast protection but not a worn item's resistance (PR #315).

    Wearing the item installs a permanent resist fire affect; char_to_store (db.cpp) skips
    permanent affects because equip_char grants them again at login. wizset saves the
    character it changes (do_wizset, act_wiz.cpp), so it stands in for `save`, which is a god
    command. The one draw that can stop the cast is the concentration roll in do_cast, which
    cast_protection repeats.
    """
    implementor, wearer = hand_out_items(server, ((STRONG_ITEM_VNUM, STRONG_ITEM_KEYWORD),))
    mystic = server.login(MYSTIC.name)
    assert wearer.command(f"wear {STRONG_ITEM_KEYWORD}").contains(FIRE_RESISTANT_MARKER)
    protect_wearer_from_fire(mystic)
    shown = affections(wearer)
    assert ITEM_RESIST_AFFECT_PATTERN.search(shown) and PROTECTION_AFFECT_PATTERN.search(shown), shown

    saved = implementor.command(f"wizset player {WEARER.name.lower()} hunger 10")
    assert saved.contains(HUNGER_SET_MARKER_FORMAT.format(WEARER.name)), saved.text

    character_files = list(server.lib_dir.glob(f"accounts/*/*/{WEARER.name.lower()}.character.json"))
    assert len(character_files) == 1, character_files
    affects = json.loads(character_files[0].read_text(encoding="utf-8"))["affects"]
    protections = [affect for affect in affects if affect["type"] == PROTECTION_AFFECT_TYPE]
    assert len(protections) == 1, affects
    protection = protections[0]
    assert (protection["location"], protection["modifier"], protection["effect_modifier"]) == (
        APPLY_RESIST_LOCATION,
        RESIST_FIRE_MODIFIER,
        30,
    ), protection
    assert not [affect for affect in affects if affect["type"] == RESIST_FIRE_AFFECT_TYPE], affects
    assert not [affect for affect in affects if affect["duration"] < 0], affects


@pytest.mark.resistances_worn
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR)
def test_an_immortal_wearing_a_resist_item_has_it_once_after_quit_and_login(server) -> None:
    """An immortal keeps a worn resist item across quit, and its resistance is granted once (PR #315).

    Login re-equips the item (Crash_load, objsave.cpp), which grants the resistance again; the
    character file must not bring back a second copy. Removing the item then leaves nothing.
    Wearing, quitting and removing draw nothing.
    """
    implementor = server.login(IMPLEMENTOR.name)
    assert implementor.command(f"load obj {STRONG_ITEM_VNUM}").contains("You create")
    assert implementor.command(f"wear {STRONG_ITEM_KEYWORD}").contains(FIRE_RESISTANT_MARKER)

    implementor.quit()
    again = server.login(IMPLEMENTOR.name)

    shown = affections(again)
    assert resistance_line("fire", STRONG_ITEM_PERCENT) in shown, shown
    assert len(ITEM_RESIST_AFFECT_PATTERN.findall(shown)) == 1, shown

    removed = again.command(f"remove {STRONG_ITEM_KEYWORD}")
    assert removed.contains(STOP_USING_MARKER_FORMAT.format(STRONG_ITEM_SHORT)), removed.text
    assert NOT_AFFECTED_MARKER in affections(again)


@pytest.mark.resistances_worn
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, MYSTIC, WEARER)
def test_protection_and_worn_fire_items_coexist_and_the_strongest_applies(server) -> None:
    """A protection and worn fire items hold side by side, and the strongest one shows (PR #315).

    resist_magnitude_for (fight.cpp) takes the largest strength on the list, and dropping one
    source leaves the others. stat's RES and SPL lines are a second view of the same state.
    The one draw that can stop the cast is the concentration roll in do_cast, which
    cast_protection repeats; wearing, removing and unprotect draw nothing.
    """
    implementor, wearer = hand_out_fire_items(server)
    mystic = server.login(MYSTIC.name)
    protect_wearer_from_fire(mystic)

    assert wearer.command(f"wear {WEAK_ITEM_KEYWORD}").contains(WEAK_ITEM_WORN_MARKER)
    shown = affections(wearer)
    assert resistance_line("fire", 30) in shown, shown
    assert fire_lines(shown) == 1, shown
    assert ITEM_RESIST_AFFECT_PATTERN.search(shown) and PROTECTION_AFFECT_PATTERN.search(shown), shown
    stat_text = stat_player(implementor, WEARER)
    resistances = STAT_RESISTANCES_PATTERN.search(stat_text)
    assert resistances is not None and "FIRE" in resistances.group(1), stat_text
    assert sorted(STAT_AFFECT_PATTERN.findall(stat_text)) == ["protection", "resist fire"], stat_text

    assert wearer.command(f"wear {STRONG_ITEM_KEYWORD}").contains(STRONG_ITEM_WORN_MARKER)
    shown = affections(wearer)
    assert resistance_line("fire", STRONG_ITEM_PERCENT) in shown, shown
    assert fire_lines(shown) == 1, shown

    assert wearer.command("unprotect").contains(RENOUNCED_MARKER)
    shown = affections(wearer)
    assert resistance_line("fire", STRONG_ITEM_PERCENT) in shown, shown
    assert not PROTECTION_AFFECT_PATTERN.search(shown), shown

    removed = wearer.command(f"remove {STRONG_ITEM_KEYWORD}")
    assert removed.contains(STOP_USING_MARKER_FORMAT.format(STRONG_ITEM_SHORT)), removed.text
    shown = affections(wearer)
    assert resistance_line("fire", WEAK_ITEM_PERCENT) in shown, shown
    assert fire_lines(shown) == 1, shown


@pytest.mark.resistances_worn
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, WEARER)
@pytest.mark.parametrize("strong_first", [False, True], ids=["weak-first", "strong-first"])
def test_the_strongest_worn_fire_resistance_applies_whatever_the_wear_order(server, strong_first) -> None:
    """Between two worn fire-resist items the stronger holds the resistance (PR #315).

    The weaker item put on beside the stronger one says nothing; the stronger one put on after
    the weaker one takes the slot and announces it. The item's affect is permanent. Wearing
    draws nothing.
    """
    _, wearer = hand_out_fire_items(server)
    weak = (WEAK_ITEM_KEYWORD, WEAK_ITEM_WORN_MARKER)
    strong = (STRONG_ITEM_KEYWORD, STRONG_ITEM_WORN_MARKER)
    first, second = (strong, weak) if strong_first else (weak, strong)

    first_worn = wearer.command(f"wear {first[0]}")
    assert first_worn.contains(first[1]), first_worn.text
    assert first_worn.contains(FIRE_RESISTANT_MARKER), first_worn.text

    second_worn = wearer.command(f"wear {second[0]}")
    assert second_worn.contains(second[1]), second_worn.text
    assert second_worn.contains(FIRE_RESISTANT_MARKER) == (not strong_first), second_worn.text

    shown = affections(wearer)
    assert resistance_line("fire", STRONG_ITEM_PERCENT) in shown, shown
    assert resistance_line("fire", WEAK_ITEM_PERCENT) not in shown, shown
    assert len(ITEM_RESIST_AFFECT_PATTERN.findall(shown)) == 1, shown


@pytest.mark.resistances_worn
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, WEARER)
def test_removing_the_stronger_fire_item_falls_back_quietly_to_the_weaker(server) -> None:
    """Taking off the item holding the resistance hands it to the strongest still worn (PR #315).

    The wearer never lost the resistance, so the removal prints no resistance message, and the
    weaker item's affect is permanent too. Wearing and removing draw nothing.
    """
    _, wearer = hand_out_fire_items(server)
    assert wearer.command(f"wear {WEAK_ITEM_KEYWORD}").contains(FIRE_RESISTANT_MARKER)
    assert wearer.command(f"wear {STRONG_ITEM_KEYWORD}").contains(FIRE_RESISTANT_MARKER)

    removed = wearer.command(f"remove {STRONG_ITEM_KEYWORD}")
    assert removed.contains(STOP_USING_MARKER_FORMAT.format(STRONG_ITEM_SHORT)), removed.text
    assert not removed.contains(FIRE_RESISTANT_MARKER), removed.text

    shown = affections(wearer)
    assert resistance_line("fire", WEAK_ITEM_PERCENT) in shown, shown
    assert resistance_line("fire", STRONG_ITEM_PERCENT) not in shown, shown
    assert ITEM_RESIST_AFFECT_PATTERN.search(shown), shown


@pytest.mark.resistances_worn
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, WEARER)
def test_removing_the_weaker_fire_item_changes_nothing(server) -> None:
    """Taking off a worn item that does not hold the resistance leaves it as it was (PR #315).

    do_resist_spell only clears the slot for the item holding it, so the stronger item keeps
    it and nothing is announced. Wearing and removing draw nothing.
    """
    _, wearer = hand_out_fire_items(server)
    assert wearer.command(f"wear {WEAK_ITEM_KEYWORD}").contains(FIRE_RESISTANT_MARKER)
    assert wearer.command(f"wear {STRONG_ITEM_KEYWORD}").contains(FIRE_RESISTANT_MARKER)

    removed = wearer.command(f"remove {WEAK_ITEM_KEYWORD}")
    assert removed.contains(STOP_USING_MARKER_FORMAT.format(WEAK_ITEM_SHORT)), removed.text
    assert not removed.contains(FIRE_RESISTANT_MARKER), removed.text

    shown = affections(wearer)
    assert resistance_line("fire", STRONG_ITEM_PERCENT) in shown, shown
    assert resistance_line("fire", WEAK_ITEM_PERCENT) not in shown, shown


@pytest.mark.resistances_worn
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, WEARER)
def test_removing_the_only_fire_item_leaves_no_resistance(server) -> None:
    """Taking off the only fire item ends the resistance (PR #315).

    Nothing else grants fire resistance, so nothing takes the slot back. Wearing and removing
    draw nothing.
    """
    _, wearer = hand_out_items(server, ((STRONG_ITEM_VNUM, STRONG_ITEM_KEYWORD),))
    assert wearer.command(f"wear {STRONG_ITEM_KEYWORD}").contains(FIRE_RESISTANT_MARKER)

    removed = wearer.command(f"remove {STRONG_ITEM_KEYWORD}")
    assert removed.contains(STOP_USING_MARKER_FORMAT.format(STRONG_ITEM_SHORT)), removed.text
    assert not removed.contains(FIRE_RESISTANT_MARKER), removed.text

    shown = affections(wearer)
    assert NOT_AFFECTED_MARKER in shown, shown
    assert fire_lines(shown) == 0, shown


@pytest.mark.resistances_worn
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, WEARER)
def test_a_flag_only_fire_resistance_shows_the_flat_strength_and_no_affect_list(server) -> None:
    """A resistance bit with no affect behind it shows the flat 33% and no affect header (PR #315).

    do_affections prints "You are affected by:" only when there is an affect to list.
    Wearing draws nothing.
    """
    _, wearer = hand_out_items(server, ((FLAG_RESIST_ITEM_VNUM, FLAG_RESIST_ITEM_KEYWORD),))
    worn = wearer.command(f"wear {FLAG_RESIST_ITEM_KEYWORD}")
    assert worn.contains(FLAG_RESIST_ITEM_WORN_MARKER), worn.text
    assert not worn.contains(FIRE_RESISTANT_MARKER), worn.text

    shown = affections(wearer)
    assert RESISTANT_HEADER in shown, shown
    assert resistance_line("fire", FLAG_RESIST_PERCENT) in shown, shown
    assert AFFECTED_HEADER not in shown, shown
    assert NOT_AFFECTED_MARKER not in shown, shown


@pytest.mark.resistances_worn
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, WEARER)
def test_a_fire_vulnerability_alone_shows_under_its_own_header(server) -> None:
    """A vulnerability alone shows under "You are vulnerable to:" at 50% (PR #315).

    Wearing draws nothing.
    """
    _, wearer = hand_out_items(server, ((VULNERABILITY_ITEM_VNUM, VULNERABILITY_ITEM_KEYWORD),))
    assert wearer.command(f"wear {VULNERABILITY_ITEM_KEYWORD}").contains(VULNERABILITY_ITEM_WORN_MARKER)

    shown = affections(wearer)
    assert VULNERABLE_HEADER in shown, shown
    assert resistance_line("fire", VULNERABILITY_PERCENT) in shown, shown
    assert RESISTANT_HEADER not in shown, shown
    assert AFFECTED_HEADER not in shown, shown


@pytest.mark.resistances_worn
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, WEARER)
def test_a_fire_resistance_and_a_fire_vulnerability_both_show_with_their_own_strengths(server) -> None:
    """A worn fire resistance and fire vulnerability each show under their header (PR #315).

    The vulnerability line keeps its flat 50% rather than the resistance's 40%. Wearing draws
    nothing.
    """
    _, wearer = hand_out_items(
        server, ((STRONG_ITEM_VNUM, STRONG_ITEM_KEYWORD), (VULNERABILITY_ITEM_VNUM, VULNERABILITY_ITEM_KEYWORD))
    )
    assert wearer.command(f"wear {STRONG_ITEM_KEYWORD}").contains(FIRE_RESISTANT_MARKER)
    assert wearer.command(f"wear {VULNERABILITY_ITEM_KEYWORD}").contains(VULNERABILITY_ITEM_WORN_MARKER)

    shown = affections(wearer)
    resistant_part, _, rest = shown.partition(VULNERABLE_HEADER)
    vulnerable_part = rest.split(AFFECTED_HEADER, 1)[0]
    assert RESISTANT_HEADER in resistant_part, shown
    assert resistance_line("fire", STRONG_ITEM_PERCENT) in resistant_part, shown
    assert resistance_line("fire", VULNERABILITY_PERCENT) in vulnerable_part, shown
    assert resistance_line("fire", STRONG_ITEM_PERCENT) not in vulnerable_part, shown
    assert ITEM_RESIST_AFFECT_PATTERN.search(shown), shown


@pytest.mark.resistances_debug
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, WEARER)
def test_debug_is_an_immortal_command_that_toggles(server) -> None:
    """debug is refused to a mortal and toggles the implementor's flag on and off (do_debug)."""
    implementor = server.login(IMPLEMENTOR.name)
    wearer = server.login(WEARER.name)

    refused = wearer.command("debug")
    assert refused.contains(UNRECOGNIZED_MARKER), refused.text
    assert implementor.command("debug").contains(DEBUG_ON_MARKER)
    assert implementor.command("debug").contains(DEBUG_OFF_MARKER)


@pytest.mark.resistances_debug
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR)
def test_debug_traces_an_item_resistance_once_on_wearing_and_removing(server) -> None:
    """With debug on, wearing a resist item prints its decode and its install once each (PR #315).

    Removing it prints the decode once and installs nothing. Wearing and removing draw nothing.
    """
    implementor = server.login(IMPLEMENTOR.name)
    assert implementor.command("debug").contains(DEBUG_ON_MARKER)
    assert implementor.command(f"load obj {STRONG_ITEM_VNUM}").contains("You create")

    worn = implementor.command(f"wear {STRONG_ITEM_KEYWORD}").text
    assert worn.count(APPLY_SPELL_DEBUG_LINE) == 1, worn
    assert worn.count(RESIST_DEBUG_LINE) == 1, worn
    assert FIRE_RESISTANT_MARKER in worn, worn

    removed = implementor.command(f"remove {STRONG_ITEM_KEYWORD}").text
    assert removed.count(APPLY_SPELL_DEBUG_LINE) == 1, removed
    assert "::RESIST::" not in removed, removed


@pytest.mark.resistances_debug
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR)
def test_debug_traces_the_poison_damage_of_putting_on_the_poison_amulet(server) -> None:
    """With debug on, the poison amulet's damage on wearing prints damage()'s trace (PR #315).

    The trace comes before damage()'s physical-resistance roll is used, and poison is not
    physical, so nothing drawn changes it.
    """
    implementor = server.login(IMPLEMENTOR.name)
    assert implementor.command("debug").contains(DEBUG_ON_MARKER)
    assert implementor.command(f"load obj {POISON_AMULET_VNUM}").contains("You create")

    worn = implementor.command(f"wear {POISON_AMULET_KEYWORD}").text
    assert worn.count(POISON_DAMAGE_DEBUG_LINE) == 1, worn
