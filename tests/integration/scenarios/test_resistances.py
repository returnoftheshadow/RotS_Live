"""Protection and resistances: a mystic's protection, worn item resistances, and how they combine.

The protection pins cover every sphere, on the mystic itself and on others, and `unprotect`; the
item pins cover worn resistances and vulnerabilities. Others pin how protection and items
combine, what a save and a new login keep, and the debug trace of both.
"""

from __future__ import annotations

import dataclasses
import json
import re
from collections.abc import Sequence

import pytest

from rots_harness import world
from rots_harness.casting import NO_ENERGY_REPLY, cast_until_it_lands
from rots_harness.characters import IMPLEMENTOR, Character, StoredValues
from rots_harness.game_text import STOP_USING_FORMAT, UNRECOGNIZED_COMMAND_REPLY
from rots_harness.immortal_commands import give_new_object, goto, load_object, wizset
from rots_harness.replies import settled_expect
from rots_harness.saved_files import account_file
from rots_harness.session import GameSession
from rots_harness.stat_reading import read_stat
from rots_harness.text_lines import first_number

pytestmark = [pytest.mark.scenario, pytest.mark.resistances]

# The cleric level on stat's profession levels line (do_stat_character, act_wiz.cpp).
CLERIC_LEVEL_PATTERN = re.compile(r"Class levels: Mag:\d+ Cle:(\d+) ")
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

# The highest mystic level cast_resist_magnitude (mystic.cpp) counts.
RESIST_LEVEL_CAP = 30
# What cast_resist_magnitude adds to the mystic level it counts.
RESIST_LEVEL_BONUS = 10

# The weak fire-resist fixture (tests/integration/world/obj/14.obj): a head item granting 20%
# through `A 27 <strength * 256 + 161>` (APPLY_SPELL with SPELL_RESIST_FIRE, spells.h).
WEAK_ITEM_VNUM = 1400
# The keyword the weak item answers to.
WEAK_ITEM_KEYWORD = "circlet"
# The weak item's short description.
WEAK_ITEM_SHORT_DESCRIPTION = "a fire circlet"
# What wearing the weak item prints (wear_message, act_obj2.cpp).
WEAK_ITEM_WORN_REPLY = "You wear a fire circlet on your head."
# The fire resistance the weak item grants, in percent.
WEAK_ITEM_PERCENT = 20
# The strong fire-resist fixture (obj/14.obj): a neck item granting 40% the same way.
STRONG_ITEM_VNUM = 1401
# The keyword the strong item answers to.
STRONG_ITEM_KEYWORD = "pendant"
# The strong item's short description.
STRONG_ITEM_SHORT_DESCRIPTION = "a fire pendant"
# What wearing the strong item prints (wear_message).
STRONG_ITEM_WORN_REPLY = "You wear a fire pendant around your neck."
# The fire resistance the strong item grants, in percent.
STRONG_ITEM_PERCENT = 40
# A finger item whose `A 30 1` (APPLY_RESIST, RESIST_FIRE) sets the fire resistance bit with no
# affect behind it.
FLAG_RESIST_ITEM_VNUM = 1402
# The keyword the flag-only item answers to.
FLAG_RESIST_ITEM_KEYWORD = "ring"
# What wearing the flag-only item prints (wear_message).
FLAG_RESIST_ITEM_WORN_REPLY = "You slide an ember ring on to your right ring finger."
# A finger item whose `A 31 1` (APPLY_VULN) makes the wearer vulnerable to fire.
VULNERABILITY_ITEM_VNUM = 1403
# The keyword the vulnerability item answers to.
VULNERABILITY_ITEM_KEYWORD = "band"
# What wearing the vulnerability item prints (wear_message).
VULNERABILITY_ITEM_WORN_REPLY = "You slide a tinder band on to your right ring finger."
# The keyword of the sickly amulet (world.OBJECT_SICKLY_AMULET), whose wearing deals poison damage
# (equip_char, handler.cpp).
POISON_AMULET_KEYWORD = "amulet"

# The strength affections shows for a resistance bit with no affect behind it (the
# default_percent do_affections passes to sprintbit_resistances, act_info.cpp).
FLAG_RESIST_PERCENT = 33
# The strength affections shows for any vulnerability (the same default_percent).
VULNERABILITY_PERCENT = 50

# What the caster reads after paying for a cast, before the spell runs (do_cast).
CAST_PAID_REPLY = "Ok."
# What the protected character reads (spell_protection, mystic.cpp, and do_resist_spell for an
# item); format with the sphere's word.
RESISTANT_FORMAT = "You feel resistant to {}!"
# RESISTANT_FORMAT's start, whatever the sphere.
RESISTANT_START = "You feel resistant to"
# What a protection from dark tells the protected character.
DARK_RESISTANT = RESISTANT_FORMAT.format("dark")
# What a protection from fire, or a fire-resist item taking the fire slot, tells the character.
FIRE_RESISTANT = RESISTANT_FORMAT.format("fire")
# What the caster reads for a protection cast on someone else (spell_protection); format with
# the name and the sphere's word.
GRANT_FORMAT = "You grant {} resistance to {}."
# GRANT_FORMAT's start, whoever the target.
GRANT_START = "You grant "
# What a caster already protected reads on a second cast (spell_protection).
ALREADY_PROTECTED_REPLY = "You have protection already."
# What the caster reads when the character it names is already protected (spell_protection);
# format with the name and its possessive pronoun.
OTHER_ALREADY_PROTECTED_FORMAT = "{} has {} protection already."
# OTHER_ALREADY_PROTECTED_FORMAT's end, whoever the target.
OTHER_ALREADY_PROTECTED_END = "protection already."
# What the caster reads when nobody in the room answers to the name (spell_protection).
NOBODY_HERE_REPLY = "Nobody here by that name."
# What a caster reads for a sphere protection does not know (spell_protection); the line breaks
# after "illusion".
WRONG_SPHERE_REPLY = "You can master protection from fire, cold, lightning, physical, illusion\n\ror dark only."
# The start of that reply, which cast_protection waits for so that a changed sphere list fails on
# the assertion rather than as a timeout.
WRONG_SPHERE_START = "You can master protection from"
# What the rest of the room reads as a protection's casting begins (do_cast, spell_pa.cpp); format
# with the caster's name.
MUTTER_ROOM_FORMAT = "{} begins quietly muttering some strange, powerful words."
# What the rest of the room reads as the protection is cast (say_spell, spell_pa.cpp); format with
# the caster's name.
UTTER_ROOM_FORMAT = "{} utters a strange command, 'protection'"
# What the character reads when it drops its protection (do_unprotect, mystic.cpp).
RENOUNCED_REPLY = "You renounce your protection!"
# What its room reads then; format with the name and the possessive pronoun.
RENOUNCED_ROOM_FORMAT = "{} renounces {} protection!"
# What unprotect prints when there is no protection to drop (do_unprotect).
NOTHING_TO_RENOUNCE_REPLY = "You renounce yourself to the world, but nothing happens..."
# What a resting character reads for a command that needs it standing (command_interpreter,
# interpre.cpp; unprotect's minimum position).
TOO_RELAXED_REPLY = "Nah... You feel too relaxed to do that.."
# What `rest` prints (do_rest, act_move.cpp).
RESTING_REPLY = "You sit down and rest your tired bones."
# What affections prints for a character with no affect or resistance (do_affections,
# act_info.cpp).
NOT_AFFECTED_REPLY = "You are not affected by anything."
# The header affections prints above the resistance lines (do_affections).
RESISTANT_HEADER = "You are resistant to:"
# The header affections prints above the vulnerability lines.
VULNERABLE_HEADER = "You are vulnerable to:"
# The header affections prints above the affect lines.
AFFECTED_HEADER = "You are affected by:"
# One resistance or vulnerability line in affections (sprintbit_resistances, utility.cpp); format
# with the element and the strength in percent.
RESISTANCE_LINE_FORMAT = "   {} ({}%)"
# A cast protection's affect line in affections (report_affection, act_info.cpp). It lasts twice
# get_mystic_caster_level (mystic.cpp), the mystic level plus a will share, so at least 20 hours
# for every caster here, and 12 hours or more reads "long".
PROTECTION_AFFECT_PATTERN = re.compile(r"protection +\(long\)")
# A worn item's resist fire affect line: it never runs out (duration -1) and reads "permanent".
ITEM_RESIST_AFFECT_PATTERN = re.compile(r"resist fire +\(permanent\)")
# What debug prints as it turns the flag on (do_debug, act_wiz.cpp).
DEBUG_ON_REPLY = "Debug flag on."
# What debug prints as it turns the flag off.
DEBUG_OFF_REPLY = "Debug flag off."
# The fire pendant's debug line from affect_modify's APPLY_SPELL decode (handler.cpp), run on
# wearing and on removing.
APPLY_SPELL_DEBUG_LINE = f"--APPLY_SPELL: spell 161 level {STRONG_ITEM_PERCENT}"
# The fire pendant's debug line from install_resist_affect (mystic.cpp), run when the item takes
# the fire slot.
RESIST_DEBUG_LINE = f"::RESIST::apply type 161 modifier 1 eff_mod {STRONG_ITEM_PERCENT} duration -1"
# The debug line damage() (fight.cpp) prints for the 5 poison damage the amulet deals on
# wearing: SPELL_POISON is attack type 43 and matches no resistance.
POISON_DAMAGE_DEBUG_LINE = "::DAMAGE:: attacktype 43 resist_type 0 check 0 dam 5"

# The resistance bits stat prints (do_stat_character, act_wiz.cpp): "RES:  FIRE. VUL: ...".
STAT_RESISTANCES_PATTERN = re.compile(r"RES: (.*?) VUL:")
# One affect line in stat (do_stat_character); the name is padded to 21 characters.
STAT_AFFECT_PATTERN = re.compile(r"SPL: \(\s*-?\d+hr\) (\S+(?: \S+)*)\s+[+-]\d+ to ")

# A protection affect's type as the character file stores it (char_to_store, db.cpp;
# SPELL_PROTECTION in spells.h).
PROTECTION_AFFECT_TYPE = 89
# A resist fire affect's type in the character file (SPELL_RESIST_FIRE, spells.h).
RESIST_FIRE_AFFECT_TYPE = 161
# A protection affect's location in the character file (APPLY_RESIST, structs.h).
APPLY_RESIST_LOCATION = 30
# A fire protection affect's modifier in the character file (RESIST_FIRE, structs.h).
RESIST_FIRE_MODIFIER = 1


def possessive(character: Character) -> str:
    """Return the pronoun act() prints for the character's $s or $S."""
    return POSSESSIVE_PRONOUNS[character.sex]


def cleric_level(implementor: GameSession, character: Character) -> int:
    """Return the mystic profession level stat shows for the character."""
    text = read_stat(implementor, f"player {character.name.lower()}")
    return first_number(CLERIC_LEVEL_PATTERN, text)


def resist_percent(level: int) -> int:
    """Return the strength cast_resist_magnitude (mystic.cpp) gives a mystic level."""
    return min(level, RESIST_LEVEL_CAP) + RESIST_LEVEL_BONUS


def cast_protection(caster: GameSession, sphere: str, target: str = "") -> str:
    """Cast protection from sphere on the caster, or on target, and return what the caster read.

    Repeats the cast past a lost concentration (cast_until_it_lands).
    """
    outcomes = [
        RESISTANT_START,
        GRANT_START,
        ALREADY_PROTECTED_REPLY,
        OTHER_ALREADY_PROTECTED_END,
        NOBODY_HERE_REPLY,
        WRONG_SPHERE_START,
        NO_ENERGY_REPLY,
    ]
    line = f"cast 'protection' {sphere} {target}".rstrip()
    return cast_until_it_lands(caster, line, outcomes)


def affections(session: GameSession) -> str:
    """Return what the character's affections command prints."""
    return session.command("affections").text


def goto_character(implementor: GameSession, character: Character) -> None:
    """Move the implementor to the character, who is in the arena centre."""
    goto(implementor, character.name.lower(), world.ROOM_ARENA_CENTRE_NAME)


def give_items(implementor: GameSession, recipient: Character, items: Sequence[tuple[int, str]]) -> None:
    """Load each (vnum, keyword) item and give it to the recipient, in the implementor's room."""
    for vnum, keyword in items:
        give_new_object(implementor, vnum, keyword, recipient.name.lower())


def protect_from_dark(server, caster: Character = MYSTIC) -> tuple[GameSession, str]:
    """Log in the caster, have it cast protection dark, and return its session and what it read."""
    mystic = server.login(caster.name)
    return mystic, cast_protection(mystic, "dark")


def hand_out_items(server, items: Sequence[tuple[int, str]]) -> tuple[GameSession, GameSession]:
    """Log in the implementor and the wearer, give the wearer the (vnum, keyword) items.

    Return both sessions, the implementor's first.
    """
    implementor = server.login(IMPLEMENTOR.name)
    wearer = server.login(WEARER.name)
    goto_character(implementor, WEARER)
    give_items(implementor, WEARER, items)
    return implementor, wearer


def hand_out_fire_items(server) -> tuple[GameSession, GameSession]:
    """Log in the implementor and the wearer, and give the wearer both fire-resist items.

    Return both sessions, the implementor's first.
    """
    return hand_out_items(server, ((WEAK_ITEM_VNUM, WEAK_ITEM_KEYWORD), (STRONG_ITEM_VNUM, STRONG_ITEM_KEYWORD)))


def protect_wearer_from_fire(mystic: GameSession) -> str:
    """Have the mystic cast protection fire on the wearer, and return what the mystic read."""
    reply = cast_protection(mystic, "fire", WEARER.name.lower())
    assert GRANT_FORMAT.format(WEARER.name, "fire") in reply, reply
    return reply


def resistance_line(element: str, percent: int) -> str:
    """Return one resistance or vulnerability line as affections prints it."""
    return RESISTANCE_LINE_FORMAT.format(element, percent)


def line_start(element: str) -> str:
    """Return the start of an element's resistance or vulnerability line, before its strength."""
    line = RESISTANCE_LINE_FORMAT.format(element, "")
    return line.removesuffix("%)")


def fire_lines(shown: str) -> int:
    """Return how many fire lines affections printed under its resistance header."""
    resistances = shown.split(RESISTANT_HEADER, 1)[1] if RESISTANT_HEADER in shown else ""
    resistances = resistances.split(VULNERABLE_HEADER, 1)[0]
    resistances = resistances.split(AFFECTED_HEADER, 1)[0]
    fire_start = line_start("fire")
    return resistances.count(fire_start)


@pytest.mark.resistances_protection
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, MYSTIC, SAGE)
@pytest.mark.parametrize("caster", [MYSTIC, SAGE], ids=["level-20", "level-30"])
def test_a_mystic_casts_protection_from_dark_and_affections_shows_its_strength(server, caster: Character) -> None:
    """Pins that protection from dark grants a dark resistance worth the mystic level plus ten.

    The one draw that can stop the cast is the concentration roll in do_cast, which
    cast_protection repeats; the strength itself comes from the profession level, not from a
    roll (cast_resist_magnitude, mystic.cpp).
    """
    implementor = server.login(IMPLEMENTOR.name)
    mystic, reply = protect_from_dark(server, caster)
    assert DARK_RESISTANT in reply, reply

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
    assert expected_percent == {MYSTIC.name: 30, SAGE.name: 40}[caster.name], expected_percent

    shown = affections(mystic)
    assert RESISTANT_HEADER in shown, shown
    assert resistance_line("dark", expected_percent) in shown, shown
    assert PROTECTION_AFFECT_PATTERN.search(shown), shown


@pytest.mark.resistances_protection
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(MYSTIC)
def test_a_mystic_already_protected_is_refused_a_protection_from_another_sphere(server) -> None:
    """Pins that a protection cast while one holds is refused whatever its sphere.

    spell_protection checks for a protection before it reads the sphere, so a fire cast over a
    dark protection is refused and dark stays. The one draw that can stop either cast is the
    concentration roll in do_cast, which cast_protection repeats.
    """
    mystic, first_reply = protect_from_dark(server)
    assert DARK_RESISTANT in first_reply, first_reply

    second_reply = cast_protection(mystic, "fire")
    assert ALREADY_PROTECTED_REPLY in second_reply, second_reply
    assert FIRE_RESISTANT not in second_reply, second_reply

    shown = affections(mystic)
    assert resistance_line("dark", 30) in shown, shown
    assert fire_lines(shown) == 0, shown


@pytest.mark.resistances_protection
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(MYSTIC)
def test_unprotect_frees_the_mystic_for_a_protection_from_another_sphere(server) -> None:
    """Pins that after unprotect a protection from another sphere takes, and replaces the first.

    The one draw that can stop either cast is the concentration roll in do_cast, which
    cast_protection repeats; unprotect itself draws nothing.
    """
    mystic, first_reply = protect_from_dark(server)
    assert DARK_RESISTANT in first_reply, first_reply
    renounced = mystic.command("unprotect")
    assert renounced.contains(RENOUNCED_REPLY), renounced.text

    second_reply = cast_protection(mystic, "fire")
    assert FIRE_RESISTANT in second_reply, second_reply

    shown = affections(mystic)
    assert resistance_line("fire", 30) in shown, shown
    assert resistance_line("dark", 30) not in shown, shown


@pytest.mark.resistances_protection
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(MYSTIC)
def test_protection_names_dark_among_its_spheres_when_given_one_it_does_not_know(server) -> None:
    """Pins that protection from an unknown sphere lists the six it knows, dark last.

    The one draw that can stop the cast is the concentration roll in do_cast, which
    cast_protection repeats.
    """
    mystic = server.login(MYSTIC.name)

    reply = cast_protection(mystic, "water")
    assert WRONG_SPHERE_REPLY in reply, reply
    shown = affections(mystic)
    assert NOT_AFFECTED_REPLY in shown, shown


@pytest.mark.resistances_protection
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(SPHERE_LOOP_MYSTIC)
def test_protection_in_each_other_sphere_grants_that_element_at_the_mystic_strength(server) -> None:
    """Pins that protection from fire, cold, lightning, physical and illusion grant their element.

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
        assert RESISTANT_FORMAT.format(message_element) in reply, (typed, reply)

        shown = affections(mystic)
        assert resistance_line(shown_element, 30) in shown, (typed, shown)
        for other in shown_elements:
            if other != shown_element:
                assert line_start(other) not in shown, (typed, shown)
        assert PROTECTION_AFFECT_PATTERN.search(shown), (typed, shown)

        assert mystic.command("unprotect").contains(RENOUNCED_REPLY), typed


@pytest.mark.resistances_protection
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, MYSTIC, WEARER)
def test_protection_cast_on_another_tells_each_observer_and_uses_the_casters_strength(server) -> None:
    """Pins that protection cast on another grants the caster's strength, not the target's.

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
    assert CAST_PAID_REPLY in reply, reply
    assert FIRE_RESISTANT not in reply, reply

    mutter = MUTTER_ROOM_FORMAT.format(MYSTIC.name)
    utter = UTTER_ROOM_FORMAT.format(MYSTIC.name)
    target_saw = settled_expect(wearer, [FIRE_RESISTANT])
    assert mutter in target_saw and utter in target_saw, target_saw
    assert GRANT_START not in target_saw, target_saw
    bystander_saw = settled_expect(implementor, [utter])
    assert mutter in bystander_saw, bystander_saw
    assert RESISTANT_START not in bystander_saw, bystander_saw
    assert GRANT_START not in bystander_saw, bystander_saw

    # Testwearer, protected by Testmystic:
    #   effect_modifier = cast_resist_magnitude(get_prof_level(PROF_CLERIC, caster))  (mystic.cpp)
    #                   = (20 > 30 ? 30 : 20) + 10        (Testmystic, Class levels: Cle:20)
    #                   = 30
    # The target's own mystic level would give (8 > 30 ? 30 : 8) + 10 = 18 (Testwearer, Cle:8).
    caster_level = cleric_level(implementor, MYSTIC)
    target_level = cleric_level(implementor, WEARER)
    assert (caster_level, target_level) == (20, 8), (caster_level, target_level)
    percents = (resist_percent(caster_level), resist_percent(target_level))
    assert percents == (30, 18), percents

    shown = affections(wearer)
    assert resistance_line("fire", 30) in shown, shown
    assert resistance_line("fire", 18) not in shown, shown
    assert PROTECTION_AFFECT_PATTERN.search(shown), shown


@pytest.mark.resistances_protection
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, MYSTIC, WEARER)
def test_protection_cast_on_another_already_protected_is_refused_with_their_pronoun(server) -> None:
    """Pins that a protection aimed at someone already protected is refused, naming them.

    spell_protection refuses it. The one draw that can stop either cast is the concentration roll in
    do_cast, which cast_protection repeats.
    """
    implementor = server.login(IMPLEMENTOR.name)
    mystic = server.login(MYSTIC.name)
    server.login(WEARER.name)
    protect_wearer_from_fire(mystic)

    second_reply = cast_protection(mystic, "cold", WEARER.name.lower())
    refusal = OTHER_ALREADY_PROTECTED_FORMAT.format(WEARER.name, possessive(WEARER))
    assert refusal in second_reply, second_reply
    assert GRANT_START not in second_reply, second_reply


@pytest.mark.resistances_protection
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(MYSTIC)
def test_protection_cast_on_a_name_nobody_here_answers_to_is_refused(server) -> None:
    """Pins that a protection aimed at a name nobody in the room has is refused (spell_protection).

    The one draw that can stop the cast is the concentration roll in do_cast, which
    cast_protection repeats.
    """
    mystic = server.login(MYSTIC.name)

    reply = cast_protection(mystic, "fire", "nobody")
    assert NOBODY_HERE_REPLY in reply, reply
    shown = affections(mystic)
    assert NOT_AFFECTED_REPLY in shown, shown


@pytest.mark.resistances_protection
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, MYSTIC)
def test_unprotect_drops_protection_and_a_second_unprotect_does_nothing(server) -> None:
    """Pins that unprotect drops a protection and tells the room; without one it does nothing.

    The one draw that can stop the cast is the concentration roll in do_cast, which
    cast_protection repeats; unprotect itself draws nothing.
    """
    implementor = server.login(IMPLEMENTOR.name)
    mystic, reply = protect_from_dark(server)
    assert DARK_RESISTANT in reply, reply
    goto_character(implementor, MYSTIC)

    renounced = mystic.command("unprotect")
    assert renounced.contains(RENOUNCED_REPLY), renounced.text
    renounced_room = RENOUNCED_ROOM_FORMAT.format(MYSTIC.name, possessive(MYSTIC))
    seen = settled_expect(implementor, [renounced_room])
    assert renounced_room in seen, seen
    shown = affections(mystic)
    assert NOT_AFFECTED_REPLY in shown, shown

    again = mystic.command("unprotect")
    assert again.contains(NOTHING_TO_RENOUNCE_REPLY), again.text
    assert not again.contains(RENOUNCED_REPLY), again.text


@pytest.mark.resistances_protection
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(MYSTIC)
def test_a_resting_mystic_cannot_unprotect(server) -> None:
    """Pins that unprotect needs its user standing, so a resting mystic keeps its protection.

    The one draw that can stop the cast is the concentration roll in do_cast, which
    cast_protection repeats.
    """
    mystic, reply = protect_from_dark(server)
    assert DARK_RESISTANT in reply, reply
    rested = mystic.command("rest")
    assert rested.contains(RESTING_REPLY), rested.text

    refused = mystic.command("unprotect")
    assert refused.contains(TOO_RELAXED_REPLY), refused.text
    assert not refused.contains(RENOUNCED_REPLY), refused.text
    shown = affections(mystic)
    assert resistance_line("dark", 30) in shown, shown


@pytest.mark.resistances_protection
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(MYSTIC)
def test_a_mystic_keeps_its_protection_and_strength_across_quit_and_login(server) -> None:
    """Pins that a protection is saved on quit with its strength and returns on the next login.

    The strength comes back from the character file, not the flat 33% of a bare resistance
    bit. The one draw that can stop the cast is the concentration roll in do_cast, which
    cast_protection repeats.
    """
    mystic, reply = protect_from_dark(server)
    assert DARK_RESISTANT in reply, reply

    again = server.relog(mystic)

    shown = affections(again)
    assert resistance_line("dark", 30) in shown, shown
    assert resistance_line("dark", FLAG_RESIST_PERCENT) not in shown, shown
    assert PROTECTION_AFFECT_PATTERN.search(shown), shown


@pytest.mark.resistances_worn
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, MYSTIC, WEARER)
def test_a_save_keeps_the_protection_and_drops_the_worn_items_resistance(server) -> None:
    """Pins that a character file keeps a cast protection but not a worn item's resistance.

    Wearing the item installs a permanent resist fire affect; char_to_store (db.cpp) skips
    permanent affects because equip_char grants them again at login. wizset saves the
    character it changes (do_wizset, act_wiz.cpp), so it stands in for `save`, which is a god
    command. The one draw that can stop the cast is the concentration roll in do_cast, which
    cast_protection repeats.
    """
    implementor, wearer = hand_out_items(server, ((STRONG_ITEM_VNUM, STRONG_ITEM_KEYWORD),))
    mystic = server.login(MYSTIC.name)
    worn = wearer.command(f"wear {STRONG_ITEM_KEYWORD}")
    assert worn.contains(FIRE_RESISTANT), worn.text
    protect_wearer_from_fire(mystic)
    shown = affections(wearer)
    assert ITEM_RESIST_AFFECT_PATTERN.search(shown) and PROTECTION_AFFECT_PATTERN.search(shown), shown

    wizset(implementor, f"player {WEARER.name.lower()}", "hunger", 10, shown_name=WEARER.name)

    character_file = account_file(server.lib_dir, WEARER.name, "character")
    saved_text = character_file.read_text(encoding="utf-8")
    saved = json.loads(saved_text)
    affects = saved["affects"]
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
    """Pins that an immortal keeps a worn resist item across quit, its resistance granted once.

    Login re-equips the item (Crash_load, objsave.cpp), which grants the resistance again; the
    character file must not bring back a second copy. Removing the item then leaves nothing.
    Wearing, quitting and removing draw nothing.
    """
    implementor = server.login(IMPLEMENTOR.name)
    load_object(implementor, STRONG_ITEM_VNUM)
    worn = implementor.command(f"wear {STRONG_ITEM_KEYWORD}")
    assert worn.contains(FIRE_RESISTANT), worn.text

    again = server.relog(implementor)

    shown = affections(again)
    assert resistance_line("fire", STRONG_ITEM_PERCENT) in shown, shown
    assert len(ITEM_RESIST_AFFECT_PATTERN.findall(shown)) == 1, shown

    removed = again.command(f"remove {STRONG_ITEM_KEYWORD}")
    assert removed.contains(STOP_USING_FORMAT.format(STRONG_ITEM_SHORT_DESCRIPTION)), removed.text
    shown = affections(again)
    assert NOT_AFFECTED_REPLY in shown, shown


@pytest.mark.resistances_worn
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, MYSTIC, WEARER)
def test_protection_and_worn_fire_items_coexist_and_the_strongest_applies(server) -> None:
    """Pins that a protection and worn fire items hold side by side, and the strongest one shows.

    resist_magnitude_for (fight.cpp) takes the largest strength on the list, and dropping one
    source leaves the others. stat's RES and SPL lines are a second view of the same state.
    The one draw that can stop the cast is the concentration roll in do_cast, which
    cast_protection repeats; wearing, removing and unprotect draw nothing.
    """
    implementor, wearer = hand_out_fire_items(server)
    mystic = server.login(MYSTIC.name)
    protect_wearer_from_fire(mystic)

    worn = wearer.command(f"wear {WEAK_ITEM_KEYWORD}")

    assert worn.contains(WEAK_ITEM_WORN_REPLY), worn.text
    shown = affections(wearer)
    assert resistance_line("fire", 30) in shown, shown
    assert fire_lines(shown) == 1, shown
    assert ITEM_RESIST_AFFECT_PATTERN.search(shown) and PROTECTION_AFFECT_PATTERN.search(shown), shown
    stat_text = read_stat(implementor, f"player {WEARER.name.lower()}")
    resistances = STAT_RESISTANCES_PATTERN.search(stat_text)
    assert resistances is not None and "FIRE" in resistances.group(1), stat_text
    assert sorted(STAT_AFFECT_PATTERN.findall(stat_text)) == ["protection", "resist fire"], stat_text

    worn = wearer.command(f"wear {STRONG_ITEM_KEYWORD}")

    assert worn.contains(STRONG_ITEM_WORN_REPLY), worn.text
    shown = affections(wearer)
    assert resistance_line("fire", STRONG_ITEM_PERCENT) in shown, shown
    assert fire_lines(shown) == 1, shown

    renounced = wearer.command("unprotect")

    assert renounced.contains(RENOUNCED_REPLY), renounced.text
    shown = affections(wearer)
    assert resistance_line("fire", STRONG_ITEM_PERCENT) in shown, shown
    assert not PROTECTION_AFFECT_PATTERN.search(shown), shown

    removed = wearer.command(f"remove {STRONG_ITEM_KEYWORD}")
    assert removed.contains(STOP_USING_FORMAT.format(STRONG_ITEM_SHORT_DESCRIPTION)), removed.text
    shown = affections(wearer)
    assert resistance_line("fire", WEAK_ITEM_PERCENT) in shown, shown
    assert fire_lines(shown) == 1, shown


@pytest.mark.resistances_worn
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, WEARER)
@pytest.mark.parametrize("strong_first", [False, True], ids=["weak-first", "strong-first"])
def test_the_strongest_worn_fire_resistance_applies_whatever_the_wear_order(server, strong_first: bool) -> None:
    """Pins that between two worn fire-resist items the stronger holds the resistance.

    The weaker item put on beside the stronger one says nothing; the stronger one put on after
    the weaker one takes the slot and announces it. The item's affect is permanent. Wearing
    draws nothing.
    """
    _, wearer = hand_out_fire_items(server)
    weak = (WEAK_ITEM_KEYWORD, WEAK_ITEM_WORN_REPLY)
    strong = (STRONG_ITEM_KEYWORD, STRONG_ITEM_WORN_REPLY)
    first, second = (strong, weak) if strong_first else (weak, strong)

    first_worn = wearer.command(f"wear {first[0]}")
    assert first_worn.contains(first[1]), first_worn.text
    assert first_worn.contains(FIRE_RESISTANT), first_worn.text

    second_worn = wearer.command(f"wear {second[0]}")
    assert second_worn.contains(second[1]), second_worn.text
    assert second_worn.contains(FIRE_RESISTANT) == (not strong_first), second_worn.text

    shown = affections(wearer)
    assert resistance_line("fire", STRONG_ITEM_PERCENT) in shown, shown
    assert resistance_line("fire", WEAK_ITEM_PERCENT) not in shown, shown
    assert len(ITEM_RESIST_AFFECT_PATTERN.findall(shown)) == 1, shown


@pytest.mark.resistances_worn
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, WEARER)
def test_removing_the_stronger_fire_item_falls_back_quietly_to_the_weaker(server) -> None:
    """Pins that taking off the item holding the resistance hands it to the strongest still worn.

    The wearer never lost the resistance, so the removal prints no resistance message, and the
    weaker item's affect is permanent too. Wearing and removing draw nothing.
    """
    _, wearer = hand_out_fire_items(server)
    worn = wearer.command(f"wear {WEAK_ITEM_KEYWORD}")
    assert worn.contains(FIRE_RESISTANT), worn.text
    worn = wearer.command(f"wear {STRONG_ITEM_KEYWORD}")
    assert worn.contains(FIRE_RESISTANT), worn.text

    removed = wearer.command(f"remove {STRONG_ITEM_KEYWORD}")
    assert removed.contains(STOP_USING_FORMAT.format(STRONG_ITEM_SHORT_DESCRIPTION)), removed.text
    assert not removed.contains(FIRE_RESISTANT), removed.text

    shown = affections(wearer)
    assert resistance_line("fire", WEAK_ITEM_PERCENT) in shown, shown
    assert resistance_line("fire", STRONG_ITEM_PERCENT) not in shown, shown
    assert ITEM_RESIST_AFFECT_PATTERN.search(shown), shown


@pytest.mark.resistances_worn
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, WEARER)
def test_removing_the_weaker_fire_item_changes_nothing(server) -> None:
    """Pins that taking off a worn item that does not hold the resistance leaves it as it was.

    do_resist_spell only clears the slot for the item holding it, so the stronger item keeps
    it and nothing is announced. Wearing and removing draw nothing.
    """
    _, wearer = hand_out_fire_items(server)
    worn = wearer.command(f"wear {WEAK_ITEM_KEYWORD}")
    assert worn.contains(FIRE_RESISTANT), worn.text
    worn = wearer.command(f"wear {STRONG_ITEM_KEYWORD}")
    assert worn.contains(FIRE_RESISTANT), worn.text

    removed = wearer.command(f"remove {WEAK_ITEM_KEYWORD}")
    assert removed.contains(STOP_USING_FORMAT.format(WEAK_ITEM_SHORT_DESCRIPTION)), removed.text
    assert not removed.contains(FIRE_RESISTANT), removed.text

    shown = affections(wearer)
    assert resistance_line("fire", STRONG_ITEM_PERCENT) in shown, shown
    assert resistance_line("fire", WEAK_ITEM_PERCENT) not in shown, shown


@pytest.mark.resistances_worn
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, WEARER)
def test_removing_the_only_fire_item_leaves_no_resistance(server) -> None:
    """Pins that taking off the only fire item ends the resistance.

    Nothing else grants fire resistance, so nothing takes the slot back. Wearing and removing
    draw nothing.
    """
    _, wearer = hand_out_items(server, ((STRONG_ITEM_VNUM, STRONG_ITEM_KEYWORD),))
    worn = wearer.command(f"wear {STRONG_ITEM_KEYWORD}")
    assert worn.contains(FIRE_RESISTANT), worn.text

    removed = wearer.command(f"remove {STRONG_ITEM_KEYWORD}")
    assert removed.contains(STOP_USING_FORMAT.format(STRONG_ITEM_SHORT_DESCRIPTION)), removed.text
    assert not removed.contains(FIRE_RESISTANT), removed.text

    shown = affections(wearer)
    assert NOT_AFFECTED_REPLY in shown, shown
    assert fire_lines(shown) == 0, shown


@pytest.mark.resistances_worn
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, WEARER)
def test_a_flag_only_fire_resistance_shows_the_flat_strength_and_no_affect_list(server) -> None:
    """Pins that a resistance bit with no affect behind it shows the flat 33% and no affect header.

    do_affections prints "You are affected by:" only when there is an affect to list.
    Wearing draws nothing.
    """
    _, wearer = hand_out_items(server, ((FLAG_RESIST_ITEM_VNUM, FLAG_RESIST_ITEM_KEYWORD),))
    worn = wearer.command(f"wear {FLAG_RESIST_ITEM_KEYWORD}")
    assert worn.contains(FLAG_RESIST_ITEM_WORN_REPLY), worn.text
    assert not worn.contains(FIRE_RESISTANT), worn.text

    shown = affections(wearer)
    assert RESISTANT_HEADER in shown, shown
    assert resistance_line("fire", FLAG_RESIST_PERCENT) in shown, shown
    assert AFFECTED_HEADER not in shown, shown
    assert NOT_AFFECTED_REPLY not in shown, shown


@pytest.mark.resistances_worn
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, WEARER)
def test_a_fire_vulnerability_alone_shows_under_its_own_header(server) -> None:
    """Pins that a vulnerability alone shows under "You are vulnerable to:" at 50%.

    Wearing draws nothing.
    """
    _, wearer = hand_out_items(server, ((VULNERABILITY_ITEM_VNUM, VULNERABILITY_ITEM_KEYWORD),))
    worn = wearer.command(f"wear {VULNERABILITY_ITEM_KEYWORD}")
    assert worn.contains(VULNERABILITY_ITEM_WORN_REPLY), worn.text

    shown = affections(wearer)
    assert VULNERABLE_HEADER in shown, shown
    assert resistance_line("fire", VULNERABILITY_PERCENT) in shown, shown
    assert RESISTANT_HEADER not in shown, shown
    assert AFFECTED_HEADER not in shown, shown


@pytest.mark.resistances_worn
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, WEARER)
def test_a_fire_resistance_and_a_fire_vulnerability_both_show_with_their_own_strengths(server) -> None:
    """Pins that a worn fire resistance and fire vulnerability each show under their header.

    The vulnerability line keeps its flat 50% rather than the resistance's 40%. Wearing draws
    nothing.
    """
    _, wearer = hand_out_items(
        server, ((STRONG_ITEM_VNUM, STRONG_ITEM_KEYWORD), (VULNERABILITY_ITEM_VNUM, VULNERABILITY_ITEM_KEYWORD))
    )
    worn = wearer.command(f"wear {STRONG_ITEM_KEYWORD}")
    assert worn.contains(FIRE_RESISTANT), worn.text
    worn = wearer.command(f"wear {VULNERABILITY_ITEM_KEYWORD}")
    assert worn.contains(VULNERABILITY_ITEM_WORN_REPLY), worn.text

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
    """Pins that debug is refused to a mortal and toggles the implementor's flag (do_debug)."""
    implementor = server.login(IMPLEMENTOR.name)
    wearer = server.login(WEARER.name)

    refused = wearer.command("debug")
    assert refused.contains(UNRECOGNIZED_COMMAND_REPLY), refused.text
    toggled = implementor.command("debug")
    assert toggled.contains(DEBUG_ON_REPLY), toggled.text
    toggled = implementor.command("debug")
    assert toggled.contains(DEBUG_OFF_REPLY), toggled.text


@pytest.mark.resistances_debug
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR)
def test_debug_traces_an_item_resistance_once_on_wearing_and_removing(server) -> None:
    """Pins that with debug on, wearing a resist item prints its decode and its install once each.

    Removing it prints the decode once and installs nothing. Wearing and removing draw nothing.
    """
    implementor = server.login(IMPLEMENTOR.name)
    toggled = implementor.command("debug")
    assert toggled.contains(DEBUG_ON_REPLY), toggled.text
    load_object(implementor, STRONG_ITEM_VNUM)

    worn = implementor.command(f"wear {STRONG_ITEM_KEYWORD}").text
    assert worn.count(APPLY_SPELL_DEBUG_LINE) == 1, worn
    assert worn.count(RESIST_DEBUG_LINE) == 1, worn
    assert FIRE_RESISTANT in worn, worn

    removed = implementor.command(f"remove {STRONG_ITEM_KEYWORD}").text
    assert removed.count(APPLY_SPELL_DEBUG_LINE) == 1, removed
    assert "::RESIST::" not in removed, removed


@pytest.mark.resistances_debug
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR)
def test_debug_traces_the_poison_damage_of_putting_on_the_poison_amulet(server) -> None:
    """Pins that with debug on, the poison amulet's damage on wearing prints damage()'s trace.

    The trace comes before damage()'s physical-resistance roll is used, and poison is not
    physical, so nothing drawn changes it.
    """
    implementor = server.login(IMPLEMENTOR.name)
    toggled = implementor.command("debug")
    assert toggled.contains(DEBUG_ON_REPLY), toggled.text
    load_object(implementor, world.OBJECT_SICKLY_AMULET)

    worn = implementor.command(f"wear {POISON_AMULET_KEYWORD}").text
    assert worn.count(POISON_DAMAGE_DEBUG_LINE) == 1, worn
