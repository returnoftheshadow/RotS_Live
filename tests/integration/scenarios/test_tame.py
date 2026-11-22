"""Tame and whistle: the zone slot a tame frees, tame's refusals, and the whistle's flee and recall.

The last pin covers the gear a tame keeps across its owner's rent.
"""

from __future__ import annotations

import dataclasses
import re

import pytest

from rots_harness import world
from rots_harness.characters import IMPLEMENTOR, Character
from rots_harness.game_text import INNKEEPER_TELLS_START, PUT_FORMAT, RENTED_REPLY
from rots_harness.immortal_commands import GIVEN_FORMAT, give_new_object, load_mob, load_object, where_lines
from rots_harness.replies import reply_to, settled_expect
from rots_harness.session import CHARACTER_MENU_PROMPT, GameSession
from rots_harness.stat_reading import NO_SUCH_MOB_REPLY, affect_bits, read_stat
from rots_harness.text_lines import capitalised, first_number

pytestmark = [pytest.mark.scenario, pytest.mark.tame]

# Room 1802, the pen gate east of the boar pen in zone 18, with a one-way exit east to the
# whistle hub.
ROOM_PEN_GATE = 1802
# The name look shows for ROOM_PEN_GATE.
ROOM_PEN_GATE_NAME = "Pen Gate"
# Room 1803, zone 18's whistle hub: its six exits lead to the spokes 1804 to 1809, each a dead
# end back to the hub, and it has no exit back to the pen gate.
ROOM_WHISTLE_HUB = 1803
# The name look shows for ROOM_WHISTLE_HUB.
ROOM_WHISTLE_HUB_NAME = "Whistle Hub"
# Room 1804, the whistle hub's north spoke, the first of its six spokes.
ROOM_NORTH_SPOKE = 1804
# Room 1805, the whistle hub's east spoke.
ROOM_EAST_SPOKE = 1805
# Room 1809, the whistle hub's lower spoke, the last of its six spokes.
ROOM_LOWER_SPOKE = 1809

# A level-30 human ranger in the boar pen who knows tame and whistle fully: 100 practice
# sessions put each knowledge at the cap (recalc_skills, spec_pro.cpp).
TAMER = Character(
    name="Testranger",
    race="human",
    sex="male",
    level=30,
    load_room=world.ROOM_BOAR_PEN,
    class_name="ranger",
    skills=(("tame", 100), ("whistle", 100)),
)
# The same ranger starting at the pen gate, away from the pen boar, for tame's refusals.
TAMER_AT_GATE = dataclasses.replace(TAMER, load_room=ROOM_PEN_GATE)
# A level-30 human ranger at the pen gate who has never practised tame.
NOVICE = Character(
    name="Testnovice",
    race="human",
    sex="male",
    level=30,
    load_room=ROOM_PEN_GATE,
    class_name="ranger",
)

# The pen boar's keyword (mob 1450, which zone 18 resets into the pen with max existing 1).
BOAR_KEYWORD = "boar"
# The pen boar's short description, as `where` prints it.
BOAR_SHORT_DESCRIPTION = "a pen boar"
# The pen boar's vnum, which `load mob` takes.
BOAR_VNUM = 1450
# A level-14 grizzly bear (mob 1451), one level past the tamer's cap.
BEAR_VNUM = 1451
# A level-12 grey wolf (mob 1452), within the tamer's cap only while it has no other tame.
WOLF_VNUM = 1452
# A watchful owl (mob 1453): an animal with perception 10.
OWL_VNUM = 1453
# A target orc (mob 1130, 30 hit points), which the boar is set on in the pen gate.
TARGET_ORC_VNUM = 1130
# A hardy target orc (mob 1135, 900 hit points), which the tame fights in the whistle hub.
HARDY_ORC_VNUM = 1135
# The test world's zone 18, the boar pen.
BOAR_ZONE = 18
# The end of the implementor's reply to `zreset 18` (do_zreset, act_wiz.cpp).
ZONE_RESET_END = ": Harness boar pen."
# The whistle hub's six spokes, each one exit from the hub.
HUB_SPOKES = range(ROOM_NORTH_SPOKE, ROOM_LOWER_SPOKE + 1)

# What everyone in the room but the boar reads when the tame succeeds (do_tame, ranger.cpp).
TAMED_REPLY = "A pen boar seems tamed."
# The end of the same line for any animal, so a tame that should have been refused is seen.
ANY_TAMED_END = " seems tamed."
# What the tamer reads when the tame roll fails (do_tame). The boar's answering attack (do_hit,
# act_offe.cpp) is refused in the pen, a PEACEROOM, so the boar stays calm for the next attempt.
FAILED_TAME_REPLY = "You fail to tame your target."
# do_tame's refusal of a target that is not an animal, or a perceptive one; do_tame checks the
# refusals the scenarios pin in the order they appear here.
ANIMALS_ONLY_REPLY = "You can only tame animals."
# do_tame's refusal of a fighting target.
ENRAGED_REPLY = "Your target is too enraged!"
# do_tame's refusal of a target below fighting position.
NEEDS_STANDING_REPLY = "Your target needs to be standing."
# do_tame's refusal of a target that has a master.
ALREADY_FOLLOWING_REPLY = "Your target is already following someone."
# do_tame's refusal of a target above the level cap.
TOO_POWERFUL_REPLY = "Your target is too powerful for you to tame."
# do_tame's answer to taming yourself.
SELF_TAME_REPLY = "Aren't we funny today..."
# do_tame's refusal of a tamer without tame knowledge; do_tame sends it with a space after its line
# end, which this leaves out.
UNSKILLED_REPLY = "Learn how to tame properly first!"
# Every reply do_tame can give, in the order it checks them.
TAME_REPLIES = [
    "You are too insubstantial to do that.",
    "You are too exhausted.",
    "Tame who?",
    "Your victim is no longer among us.",
    "You are too airy for that.",
    ANIMALS_ONLY_REPLY,
    ENRAGED_REPLY,
    NEEDS_STANDING_REPLY,
    ALREADY_FOLLOWING_REPLY,
    TOO_POWERFUL_REPLY,
    SELF_TAME_REPLY,
    UNSKILLED_REPLY,
    "You are too busy to do that.",
    "You target is not here any longer.",
    "Sorry, following in circles is not allowed.",
    ANY_TAMED_END,
    FAILED_TAME_REPLY,
]
# The most tame attempts made before the test fails. Nothing limits the attempts: a failed one
# costs no moves (only a success takes 60) and the PEACEROOM refuses the boar's answering attack,
# so the bound is a count. At 8 failures in 101 per attempt (see the margin comment in
# check_tame_chance), 30 attempts all fail about once in 10^33 runs.
MAX_TAME_ATTEMPTS = 30
# Seconds to wait for a tame's outcome. The delay is 60 beats * target level * 2 / (ranger level
# 30 + 1) passes: 3 for the boar, 54 (13.5 seconds) for the level-14 bear, the most a scenario
# can meet if a refusal stops refusing; 20 leaves room for a loaded host or emulated i386.
TAME_TIMEOUT_SECONDS = 20.0

# What the whistler reads when the whistle carries (do_whistle, ranger.cpp).
WHISTLED_REPLY = "You whistle powerfully."
# What the whistler reads when the knowledge roll fails (do_whistle).
WEAK_WHISTLE_REPLY = "You whistle, but can barely hear yourself."
# What the whistler reads as a shadow (do_whistle).
SHADOW_WHISTLE_REPLY = "You need to breathe to whistle!"
# What a character elsewhere in the whistler's zone reads (do_whistle).
DISTANT_WHISTLE = "You hear a powerful whistle nearby."
# What the room reads when the fighting tame tries to flee (do_flee, act_offe.cpp).
BOAR_PANICS = "A pen boar panics, and attempts to flee!"
# What the room reads when it gets away.
BOAR_FLEES = "A pen boar flees head over heels!"
# What the whistler in the hub reads when its tame hunts its way back from the north spoke.
BOAR_RETURNS = "A pen boar enters from the north."
# What everyone in the room but the boar and its target reads when the boar attacks the hardy orc
# (do_hit).
BOAR_ATTACKS_HARDY_ORC = "A pen boar attacks a hardy target orc!"
# The same when the boar attacks the target orc.
BOAR_ATTACKS_TARGET_ORC = "A pen boar attacks a target orc!"
# The other replies the tamer can read to `order` (do_order, act_offe.cpp).
ORDER_REFUSALS = ["That person isn't here.", "has an indifferent look.", "Order who to do what?"]
# Seconds to wait for the whistle's outcome: whistle's 6 beats are 6 passes, a second and a half.
WHISTLE_TIMEOUT_SECONDS = 8.0

# The tamer's ranger level, which tame's level check and success chance read.
RANGER_LEVEL_PATTERN = re.compile(r"Class levels: Mag:\d+ Cle:\d+ Ran:(\d+) ")
# A practice line for tame at full knowledge (do_practice and how_good, spec_pro.cpp): how_good
# says Mastered only above 99, and knowledge stops at 100.
TAME_MASTERED_PATTERN = re.compile(r"^\s*tame\s+\(Mastered\)", re.MULTILINE)
# Any practice line for tame (do_practice lists only skills with knowledge above 0).
TAME_PRACTICE_PATTERN = re.compile(r"^\s*tame\s", re.MULTILINE)
# The tame affect row in `stat mob`: duration -1 shows as 0 hours, and it sets CHARM.
TAME_AFFECT_PATTERN = re.compile(r"^\s*SPL: \(\s*0hr\) tame\s+sets\s+CHARM\.", re.MULTILINE)
# The whistle's recall affect row in `stat mob` (SPELL_ACTIVITY, which sets HUNT).
ACTIVITY_AFFECT_PATTERN = re.compile(r"^\s*SPL: \(\s*\d+hr\) activity\s.*sets\s+HUNT\.", re.MULTILINE)
# One line of the immortal `where <keyword>` list for the boar (perform_immort_where,
# act_info.cpp): `%3d. %-25s - [%5d] %s`, the room's vnum in brackets.
BOAR_WHERE_LINE_PATTERN = re.compile(
    r"^\s*\d+\. " + re.escape(BOAR_SHORT_DESCRIPTION) + r"\s+- \[\s*(\d+)\]", re.MULTILINE
)


def boar_rooms(implementor: GameSession) -> list[int]:
    """Return the room vnum of every pen boar in the world, from the implementor's `where boar`."""
    reply = implementor.command(f"where {BOAR_KEYWORD}")
    rooms = BOAR_WHERE_LINE_PATTERN.findall(reply.text)
    return sorted(int(room) for room in rooms)


def reset_boar_zone(implementor: GameSession) -> None:
    """Reset zone 18 at once through the implementor's `zreset`."""
    reply = implementor.command(f"zreset {BOAR_ZONE}")
    assert reply.contains(ZONE_RESET_END), reply.text


def stat_mob(implementor: GameSession, keyword: str) -> str:
    """Return the implementor's whole `stat mob <keyword>` reply, whose lines can end in `]`."""
    return read_stat(implementor, f"mob {keyword}", other_outcomes=[NO_SUCH_MOB_REPLY])


def goto_anywhere(immortal: GameSession, room: int) -> None:
    """Move an immortal to room and check the command was accepted."""
    reply = immortal.command(f"goto {room}")
    assert not reply.contains("No room exists with that number.") and not reply.contains("Huh?!"), reply.text


def ranger_level(implementor: GameSession, character: Character) -> int:
    """Return the character's ranger level from the implementor's `stat player`."""
    stat_text = read_stat(implementor, f"player {character.name.lower()}")
    return first_number(RANGER_LEVEL_PATTERN, stat_text)


def check_tame_inputs(implementor: GameSession, tamer: GameSession, character: Character) -> None:
    """Assert the ranger level 30 and the mastered tame every tame margin below rests on."""
    level = ranger_level(implementor, character)
    assert level == 30, level
    practice = tamer.command("practice")
    assert TAME_MASTERED_PATTERN.search(practice.text), practice.text


def check_tame_chance(implementor: GameSession, tamer: GameSession, character: Character) -> None:
    """Assert the numbers behind tame's success roll on the pen boar, which is not roll-proof."""
    check_tame_inputs(implementor, tamer, character)
    # Tame's success roll on the pen boar (tame knowledge 100, Mastered in `practice`):
    #   tmp_abil = (int)(GET_SKILL / 110.0 * 95 + Ran / 36.0 * 9)          (do_tame, ranger.cpp)
    #            = (int)(100 / 110.0 * 95    + 30 / 36.0 * 9)              (Ran:30)
    #              (no anger affect, so no 20% cut)
    #            = (int)(86.36 + 7.5) = 93
    #   tamed    = tmp_abil > number(0, 100): 93 of 101 draws succeed, NOT roll-proof, so a
    #              failure is retried. The level check passes: (int)(30 / 3.0 + 100 / 30.0)
    #              - boar level 1 - no followers = 13 - 1 - 0 = 12, not below 0.
    tame_ability = int(100 / 110 * 95 + 30 / 36 * 9)
    assert tame_ability == 93, tame_ability


def tame_boar(tamer: GameSession) -> str:
    """Tame the boar in the tamer's room and return what the tamer read on success.

    The success roll is the one draw on the path, and a failure only gates the attempt, so the
    attempt is repeated after FAILED_TAME_REPLY, at most MAX_TAME_ATTEMPTS times.
    """
    for _attempt in range(MAX_TAME_ATTEMPTS):
        reply = reply_to(tamer, f"tame {BOAR_KEYWORD}", TAME_REPLIES, timeout=TAME_TIMEOUT_SECONDS)
        if FAILED_TAME_REPLY not in reply:
            assert TAMED_REPLY in reply, reply
            return reply
    pytest.fail(f"the tame failed {MAX_TAME_ATTEMPTS} times running")


def tame_and_walk_to_hub(implementor: GameSession, tamer: GameSession) -> None:
    """Tame the pen boar and walk the tamer east twice to the whistle hub, the boar following."""
    check_tame_chance(implementor, tamer, TAMER)
    tame_boar(tamer)
    at_gate = tamer.command("east")
    assert at_gate.room_name() == ROOM_PEN_GATE_NAME, at_gate.text
    at_hub = tamer.command("east")
    assert at_hub.room_name() == ROOM_WHISTLE_HUB_NAME, at_hub.text
    rooms = boar_rooms(implementor)
    assert rooms == [ROOM_WHISTLE_HUB], rooms


def whistle(tamer: GameSession) -> str:
    """Whistle and return what the whistler read once the delay ended."""
    outcomes = [WHISTLED_REPLY, WEAK_WHISTLE_REPLY, SHADOW_WHISTLE_REPLY]
    return reply_to(tamer, "whistle", outcomes, timeout=WHISTLE_TIMEOUT_SECONDS)


def set_tame_on_hardy_orc(implementor: GameSession, tamer: GameSession) -> None:
    """Load the hardy target orc into the whistle hub and order the tame to attack it.

    do_hit (act_offe.cpp) runs one hit() for the boar, and every way out of hit() that the
    boar can take sets both sides fighting: a dodge or parry falls through to set_fighting, and
    a hit calls damage(), which sets them. The boar's damage cannot floor the orc's 900 hit
    points, and the affect-hours schedule runs no combat rounds after it.
    """
    goto_anywhere(implementor, ROOM_WHISTLE_HUB)
    load_mob(implementor, HARDY_ORC_VNUM)
    ordered = reply_to(tamer, "order boar kill target", [BOAR_ATTACKS_HARDY_ORC, *ORDER_REFUSALS])
    assert BOAR_ATTACKS_HARDY_ORC in ordered, ordered
    fighting = stat_mob(implementor, BOAR_KEYWORD)
    assert "Pos: Fighting, Fighting: a hardy target orc" in fighting, fighting


@pytest.mark.parametrize(
    "removal_command, removal_reply", [("purge", "Ok."), ("slay", "You chop")], ids=["purged", "slain"]
)
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, TAMER)
def test_a_tamed_animal_frees_its_zone_slot_once_so_resets_keep_one_wild_copy(
    server, removal_command: str, removal_reply: str
) -> None:
    """Fix pin (#314): a tamed animal that is purged or slain does not free its zone slot again.

    #314 made do_tame (ranger.cpp) clear the tame's load line after releasing its slot; before
    it, extract_char (handler.cpp) released the same slot again when the tame left the world,
    dropping the reset row's existing count below the wild copy it still had, so the next reset
    loaded a second wild boar. The removal is either `purge`, which extracts the tame at once, or
    `slay`, which kills it (raw_kill, fight.cpp). Zone 18 resets one boar with max existing 1;
    zreset runs inside the command, so the affect-hours schedule's lack of zone updates does not
    matter and keeps every other draw away. The tame's success roll is the one draw that can
    change the outcome, a gating roll that tame_boar repeats.
    """
    implementor = server.login(IMPLEMENTOR.name)
    tamer = server.login(TAMER.name)

    rooms = boar_rooms(implementor)
    assert rooms == [world.ROOM_BOAR_PEN], rooms
    check_tame_chance(implementor, tamer, TAMER)
    tame_boar(tamer)

    tame_stat = stat_mob(implementor, BOAR_KEYWORD)
    assert f"Master is: {TAMER.name}," in tame_stat, tame_stat
    assert "CHARM" in affect_bits(tame_stat), tame_stat
    assert TAME_AFFECT_PATTERN.search(tame_stat), tame_stat

    # The tame follows its tamer out of the pen, so where tells it from the wild copy by room.
    at_gate = tamer.command("east")
    assert at_gate.room_name() == ROOM_PEN_GATE_NAME, at_gate.text
    rooms = boar_rooms(implementor)
    assert rooms == [ROOM_PEN_GATE], rooms

    reset_boar_zone(implementor)
    rooms = boar_rooms(implementor)
    assert rooms == [world.ROOM_BOAR_PEN, ROOM_PEN_GATE], rooms

    goto_anywhere(implementor, ROOM_PEN_GATE)
    removed = implementor.command(f"{removal_command} {BOAR_KEYWORD}")
    assert removed.contains(removal_reply), removed.text
    rooms = boar_rooms(implementor)
    assert rooms == [world.ROOM_BOAR_PEN], rooms

    reset_boar_zone(implementor)
    reset_boar_zone(implementor)
    rooms = boar_rooms(implementor)
    assert rooms == [world.ROOM_BOAR_PEN], rooms


# Each case: the mobs the implementor loads at the pen gate, the command it then gives with the
# reply it reads, the keyword the tamer tames, and the refusal the tamer reads.
REFUSAL_CASES = [
    pytest.param((), None, None, IMPLEMENTOR.name.lower(), ANIMALS_ONLY_REPLY, id="a_player"),
    pytest.param(
        (world.MOB_STRAW_SCARECROW,), None, None, "scarecrow", ANIMALS_ONLY_REPLY, id="a_creature_that_is_not_an_animal"
    ),
    pytest.param((OWL_VNUM,), None, None, "owl", ANIMALS_ONLY_REPLY, id="an_animal_with_perception"),
    pytest.param(
        (BOAR_VNUM, TARGET_ORC_VNUM), "force boar kill orc", BOAR_ATTACKS_TARGET_ORC, BOAR_KEYWORD, ENRAGED_REPLY,
        id="a_fighting_animal",
    ),
    pytest.param(
        (BOAR_VNUM,),
        "force boar sleep",
        "A pen boar lies down and falls asleep.",
        BOAR_KEYWORD,
        NEEDS_STANDING_REPLY,
        id="a_sleeping_animal",
    ),
    pytest.param(
        (BOAR_VNUM,),
        "force boar follow testimm",
        "A pen boar starts following you.",
        BOAR_KEYWORD,
        ALREADY_FOLLOWING_REPLY,
        id="an_animal_following_someone",
    ),
    pytest.param((BEAR_VNUM,), None, None, "bear", TOO_POWERFUL_REPLY, id="an_animal_above_the_level_cap"),
]


@pytest.mark.parametrize("mob_vnums, setup_command, setup_reply, target, refusal", REFUSAL_CASES)
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, TAMER_AT_GATE)
def test_tame_refuses_a_target_it_cannot_tame_before_any_roll(
    server,
    mob_vnums: tuple[int, ...],
    setup_command: str | None,
    setup_reply: str | None,
    target: str,
    refusal: str,
) -> None:
    """Pins that tame refuses players, non-animals and unfit animals at once, before any roll.

    The unfit animals are perceptive, fighting, lying, following or too strong ones. do_tame
    (ranger.cpp) checks the target in this order, before its delay and its one roll: perception, NPC
    and animal bodytype (one check; the owl fails only its perception term and the scarecrow only
    its bodytype term), then fighting, then a position below fighting, then a master, then the level
    cap, which counts the ranger's other tames (here there are none). Each animal case fails only
    its own check: the boar copies fight, sleep or follow only as set up, and the bear is a standing
    animal with perception 0 and no master. The affect-hours schedule keeps every other draw away; a
    boar set on the orc draws in hit() but ends up fighting whatever the draws (see
    set_tame_on_hardy_orc).
    """
    implementor = server.login(IMPLEMENTOR.name)
    tamer = server.login(TAMER_AT_GATE.name)

    goto_anywhere(implementor, ROOM_PEN_GATE)
    for vnum in mob_vnums:
        load_mob(implementor, vnum)

    if setup_command is not None:
        setup_text = reply_to(implementor, setup_command, [setup_reply, "No-one by that name here..."])
        assert setup_reply in setup_text, setup_text

    check_tame_inputs(implementor, tamer, TAMER_AT_GATE)
    # The level cap, for the bear (level 14) and the others alike (do_tame, ranger.cpp):
    #   levels_over_required = (int)(Ran / 3.0 + tame_skill / 30.0) - GET_LEVEL(victim)
    #                          - get_followers_level(ch)
    #   tame_skill = GET_SKILL(tame) + GET_RAW_SKILL(animals) / 2 = 100 + 0 / 2 = 100
    #                (Mastered in `practice`; no animals practice)
    #            = (int)(30 / 3.0 + 100 / 30.0) - 14 - 0                   (Ran:30, no tames)
    #              (no calm affect on the bear, no pets specialization: no +1s)
    #            = 13 - 14 - 0 = -1, below 0: refused. A boar (level 1) leaves 12.
    levels_over_required = int(30 / 3 + 100 / 30) - 14 - 0
    assert levels_over_required == -1, levels_over_required

    reply = reply_to(tamer, f"tame {target}", TAME_REPLIES, timeout=TAME_TIMEOUT_SECONDS)
    assert refusal in reply, reply


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, NOVICE)
def test_tame_refuses_a_ranger_who_has_not_learned_it(server) -> None:
    """Pins that a ranger without tame knowledge is told to learn it, before any roll.

    do_tame (ranger.cpp) checks GET_SKILL(tame) after the target checks and before the delay.
    No roll is on the path.
    """
    implementor = server.login(IMPLEMENTOR.name)
    novice = server.login(NOVICE.name)

    goto_anywhere(implementor, ROOM_PEN_GATE)
    load_mob(implementor, BOAR_VNUM)
    level = ranger_level(implementor, NOVICE)
    assert level == 30, level
    practice = novice.command("practice")
    assert not TAME_PRACTICE_PATTERN.search(practice.text), practice.text
    # The level cap, which comes first (do_tame, ranger.cpp):
    #   levels_over_required = (int)(Ran / 3.0 + tame_skill / 30.0) - GET_LEVEL(victim)
    #                          - get_followers_level(ch)
    #            = (int)(30 / 3.0 + 0 / 30.0) - 1 - 0     (Ran:30; tame not in `practice`)
    #            = 10 - 1 - 0 = 9, not below 0: the knowledge check is reached.
    levels_over_required = int(30 / 3 + 0 / 30) - 1 - 0
    assert levels_over_required == 9, levels_over_required

    reply = reply_to(novice, f"tame {BOAR_KEYWORD}", TAME_REPLIES, timeout=TAME_TIMEOUT_SECONDS)
    assert UNSKILLED_REPLY in reply, reply


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, TAMER)
def test_a_tames_level_counts_against_the_rangers_next_tame(server) -> None:
    """Fix pin (#218): a ranger's tame counts at least two levels against its next tame.

    #218 made do_tame (ranger.cpp) subtract get_followers_level (utility.cpp), the sum of
    max(2, level) over the ranger's charmed followers, from the level cap. With the level-1 boar
    tamed, the level-12 wolf is refused; without it, or with the boar counted at its own level,
    the wolf would be within the cap. The tame's success roll is the one draw, a gating roll
    that tame_boar repeats; the refusal comes before any roll.
    """
    implementor = server.login(IMPLEMENTOR.name)
    tamer = server.login(TAMER.name)

    check_tame_chance(implementor, tamer, TAMER)
    tame_boar(tamer)
    # The tame took 60 of the tamer's moves, and do_tame refuses a tamer below 60 first.
    restored = implementor.command(f"restore {TAMER.name.lower()}")
    assert restored.contains("Done."), restored.text
    goto_anywhere(implementor, world.ROOM_BOAR_PEN)
    load_mob(implementor, WOLF_VNUM)

    # The level cap for the wolf, with the boar tamed (do_tame, ranger.cpp):
    #   levels_over_required = (int)(Ran / 3.0 + tame_skill / 30.0) - GET_LEVEL(victim)
    #                          - get_followers_level(ch)
    #            = (int)(30 / 3.0 + 100 / 30.0) - 12 - max(2, 1)
    #              (Ran:30, tame Mastered; the wolf is level 12; the boar, level 1, counts 2)
    #            = 13 - 12 - 2 = -1, below 0: refused. Without the boar: 13 - 12 - 0 = 1.
    levels_over_required = int(30 / 3 + 100 / 30) - 12 - max(2, 1)
    assert levels_over_required == -1, levels_over_required

    reply = reply_to(tamer, "tame wolf", TAME_REPLIES, timeout=TAME_TIMEOUT_SECONDS)
    assert TOO_POWERFUL_REPLY in reply, reply


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, TAMER)
def test_whistle_makes_a_fighting_tame_flee_its_fight(server) -> None:
    """Fix pin (#273): a whistle makes the whistler's fighting tame flee its fight.

    #273 made do_whistle (ranger.cpp) call do_flee (act_offe.cpp) for a tame that is fighting.
    The whistle cannot fail at knowledge 100: it fails only when 100 < number(0, 99). do_flee
    draws an exit with number(0, 5), and every exit of the whistle hub leads to a spoke, so its
    first draw always finds one; check_simple_move then lets the boar go, since the order's
    two-pass delay is over by the end of the whistle's six. The boar's hit draws too but cannot
    change the outcome (set_tame_on_hardy_orc); the tame's success roll is the one draw that
    can, a gating roll that tame_boar repeats.
    """
    implementor = server.login(IMPLEMENTOR.name)
    tamer = server.login(TAMER.name)

    tame_and_walk_to_hub(implementor, tamer)
    set_tame_on_hardy_orc(implementor, tamer)

    whistled = whistle(tamer)
    assert WHISTLED_REPLY in whistled, whistled
    assert BOAR_PANICS in whistled, whistled
    assert BOAR_FLEES in whistled, whistled

    rooms = boar_rooms(implementor)
    assert len(rooms) == 1 and rooms[0] in HUB_SPOKES, rooms
    fled = stat_mob(implementor, BOAR_KEYWORD)
    assert "Fighting: Nobody" in fled, fled


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, TAMER)
def test_whistle_recalls_the_whistlers_tame_from_elsewhere_in_the_zone(server) -> None:
    """Pins that a whistle sends the whistler's tame back to it, and leaves a wild animal.

    do_whistle (ranger.cpp) gives each pet of the whistler's in its zone the activity affect
    (duration 5, setting HUNT) and remembers the whistler; a wild boar, no pet of the
    whistler's, gets nothing. The whistle cannot fail at knowledge 100 (100 < number(0, 99)
    never holds). activity is a fast affect, so affect_update_person (limits.cpp) gives the boar
    one mobile turn on each of the first five fast passes of an advanced hour; each turn
    hunts one step toward the whistler (one_mobile_activity, mobact.cpp) unless
    number(0, 100) > 0 fails, once in 101, and one step is enough. All five fail together about
    once in 10^10 runs, so a whistle is repeated, at most three times. The tame's success roll
    is the one other draw, a gating roll that tame_boar repeats.
    """
    implementor = server.login(IMPLEMENTOR.name)
    tamer = server.login(TAMER.name)

    tame_and_walk_to_hub(implementor, tamer)
    # A tame will not walk away from its master (do_move, act_move.cpp), so the implementor
    # carries it to the north spoke.
    goto_anywhere(implementor, ROOM_NORTH_SPOKE)
    transferred = implementor.command(f"transfer {BOAR_KEYWORD}")
    assert transferred.contains("A pen boar arrives from a puff of smoke."), transferred.text
    goto_anywhere(implementor, ROOM_EAST_SPOKE)
    load_mob(implementor, BOAR_VNUM)
    rooms = boar_rooms(implementor)
    assert rooms == [ROOM_NORTH_SPOKE, ROOM_EAST_SPOKE], rooms

    whistled = whistle(tamer)
    assert WHISTLED_REPLY in whistled, whistled
    heard = settled_expect(implementor, [DISTANT_WHISTLE])
    assert DISTANT_WHISTLE in heard, heard

    wild = stat_mob(implementor, BOAR_KEYWORD)
    assert "Master is: <none>," in wild, wild
    assert not ACTIVITY_AFFECT_PATTERN.search(wild), wild
    assert "HUNT" not in affect_bits(wild), wild

    goto_anywhere(implementor, ROOM_NORTH_SPOKE)
    recalled = stat_mob(implementor, BOAR_KEYWORD)
    assert f"Master is: {TAMER.name}," in recalled, recalled
    assert ACTIVITY_AFFECT_PATTERN.search(recalled), recalled
    assert "HUNT" in affect_bits(recalled), recalled

    hours_text = ""
    for _whistle in range(3):
        hours_text += server.text_after_hours(tamer)
        rooms = boar_rooms(implementor)
        if rooms == [ROOM_WHISTLE_HUB, ROOM_EAST_SPOKE]:
            break

        whistled = whistle(tamer)
        assert WHISTLED_REPLY in whistled, whistled

    rooms = boar_rooms(implementor)
    assert rooms == [ROOM_WHISTLE_HUB, ROOM_EAST_SPOKE], rooms
    assert BOAR_RETURNS in hours_text, hours_text


# The helm the tame is dressed in (obj/16.obj): the vnum, a keyword and the short description, as
# for each object below. No zone resets any of them, so `where` by vnum finds every copy in the
# world.
TAME_HELM = (world.OBJECT_LEATHER_HELM, "helm", world.OBJECT_LEATHER_HELM_SHORT_DESCRIPTION)
# The bag the tame is handed (obj/11.obj).
TAME_BAG = (world.OBJECT_LEATHER_BAG, "bag", world.OBJECT_LEATHER_BAG_SHORT_DESCRIPTION)
# The bead put in the tame's bag (obj/13.obj).
TAME_BEAD = (world.OBJECT_COPPER_BEAD, "bead", world.OBJECT_COPPER_BEAD_SHORT_DESCRIPTION)
# A grey pebble (world.OBJECT_GREY_PEBBLE) for the tamer to carry: the innkeeper refuses a renter
# who carries nothing (Crash_offer_rent, objsave.cpp).
PEBBLE = (world.OBJECT_GREY_PEBBLE, "pebble", "a grey pebble")


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, TAMER)
def test_a_tame_comes_back_after_its_owners_rent_wearing_its_gear_but_not_carrying_any(server) -> None:
    """Legacy pin: renting saves a tame with what it wears, and destroys what it carries.

    Crash_rentsave (objsave.cpp) saves each follower in the renter's room with its worn gear
    only (Crash_follower_save), then takes the followers out of the world (extract_followers):
    the worn gear and the carried gear are destroyed with them, none left on the floor. The
    login restores the tame wearing a new copy of its helm (Crash_follower_load). The boar is
    dressed before it is tamed, because a tame cannot wear anything (do_wear, act_obj2.cpp).
    The innkeeper is loaded last and only `rent` is typed after it, so its random social
    (gen_receptionist) never draws before the rent. The tame's success roll is the one draw that
    can change the outcome, a gating roll that tame_boar repeats.
    """
    implementor = server.login(IMPLEMENTOR.name)
    tamer = server.login(TAMER.name)
    goto_anywhere(implementor, world.ROOM_BOAR_PEN)
    for item in (TAME_HELM, TAME_BAG, TAME_BEAD):
        load_object(implementor, item[0], item[2])

    put = implementor.command("put bead bag")
    put_reply = PUT_FORMAT.format(TAME_BEAD[2], TAME_BAG[2])
    assert put.contains(put_reply), put.text
    for item in (TAME_HELM, TAME_BAG):
        given = implementor.command(f"give {item[1]} {BOAR_KEYWORD}")
        assert given.contains(GIVEN_FORMAT.format(item[2], BOAR_SHORT_DESCRIPTION)), given.text

    dressed = implementor.command(f"force {BOAR_KEYWORD} wear helm")
    assert dressed.contains(f"{capitalised(BOAR_SHORT_DESCRIPTION)} wears {TAME_HELM[2]} on"), dressed.text
    check_tame_chance(implementor, tamer, TAMER)
    tame_boar(tamer)
    for item in (TAME_HELM, TAME_BAG, TAME_BEAD):
        assert len(where_lines(implementor, item[0], item[2])) == 1, item

    give_new_object(
        implementor, PEBBLE[0], PEBBLE[1], TAMER.name.lower(), shown_recipient=TAMER.name, short_description=PEBBLE[2]
    )
    load_mob(implementor, world.MOB_INNKEEPER)

    rented = reply_to(tamer, "rent", [CHARACTER_MENU_PROMPT, INNKEEPER_TELLS_START])
    assert RENTED_REPLY in rented, rented
    assert CHARACTER_MENU_PROMPT in rented, rented
    tamer.close()
    rooms = boar_rooms(implementor)
    assert rooms == [], rooms
    for item in (TAME_HELM, TAME_BAG, TAME_BEAD):
        assert where_lines(implementor, item[0], item[2]) == [], item

    floor = implementor.command("look").text
    for long_description in ("A leather helm lies here.", "A leather bag lies here.", "A copper bead lies here."):
        assert long_description not in floor, floor

    server.login(TAMER.name)
    rooms = boar_rooms(implementor)
    assert rooms == [world.ROOM_BOAR_PEN], rooms
    restored = stat_mob(implementor, BOAR_KEYWORD)
    assert f"Master is: {TAMER.name}," in restored, restored
    helms = where_lines(implementor, TAME_HELM[0], TAME_HELM[2])
    assert len(helms) == 1 and f"Carried by {BOAR_SHORT_DESCRIPTION}" in helms[0], helms
    bags = where_lines(implementor, TAME_BAG[0], TAME_BAG[2])
    assert bags == [], bags
    beads = where_lines(implementor, TAME_BEAD[0], TAME_BEAD[2])
    assert beads == [], beads
