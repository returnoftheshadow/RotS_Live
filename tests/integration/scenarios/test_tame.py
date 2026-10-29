"""Tame and whistle: the zone slot a tame frees, tame's refusals, and the whistle's flee and recall."""

from __future__ import annotations

import dataclasses
import re

import pytest

from rots_harness import world
from rots_harness.characters import IMPLEMENTOR, Character
from rots_harness.session import GameSession

pytestmark = [pytest.mark.scenario, pytest.mark.tame]

# Room 1802, the pen gate east of the boar pen in zone 18, with a one-way exit east to the
# whistle hub.
ROOM_PEN_GATE = 1802
# Room 1803, zone 18's whistle hub: its six exits lead to the spokes 1804 to 1809, each a dead
# end back to the hub, and it has no exit back to the pen gate.
ROOM_WHISTLE_HUB = 1803
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
BOAR_NAME = "a pen boar"
# The pen boar's vnum, which `load mob` takes.
BOAR_VNUM = 1450
# A level-14 grizzly bear (mob 1451), one level past the tamer's cap.
BEAR_VNUM = 1451
# A level-12 grey wolf (mob 1452), within the tamer's cap only while it has no other tame.
WOLF_VNUM = 1452
# A watchful owl (mob 1453): an animal with perception 10.
OWL_VNUM = 1453
# A straw scarecrow (mob 1454): perception 0, but bodytype 1, not an animal.
SCARECROW_VNUM = 1454
# A target orc (mob 1130, 30 hit points), which the boar is set on in the pen gate.
TARGET_ORC_VNUM = 1130
# A hardy target orc (mob 1135, 900 hit points), which the tame fights in the whistle hub.
HARDY_ORC_VNUM = 1135
# The test world's zone 18, the boar pen.
BOAR_ZONE = 18
# The end of the implementor's reply to `zreset 18` (do_zreset, act_wiz.cpp).
ZONE_RESET_MARKER = ": Harness boar pen."
# The whistle hub's six spokes, each one exit from the hub.
HUB_SPOKES = range(ROOM_NORTH_SPOKE, ROOM_LOWER_SPOKE + 1)

# What everyone in the room but the boar reads when the tame succeeds (do_tame, ranger.cpp).
TAMED_MARKER = "A pen boar seems tamed."
# The end of the same line for any animal, so a tame that should have been refused is seen.
ANY_TAMED_MARKER = " seems tamed."
# What the tamer reads when the tame roll fails (do_tame). The boar's answering attack (do_hit,
# act_offe.cpp) is refused in the pen, a PEACEROOM, so the boar stays calm for the next attempt.
FAILED_TAME_MARKER = "You fail to tame your target."
# do_tame's refusals that the scenarios pin, in the order it checks them.
ANIMALS_ONLY_MARKER = "You can only tame animals."
ENRAGED_MARKER = "Your target is too enraged!"
NEEDS_STANDING_MARKER = "Your target needs to be standing."
ALREADY_FOLLOWING_MARKER = "Your target is already following someone."
TOO_POWERFUL_MARKER = "Your target is too powerful for you to tame."
SELF_TAME_MARKER = "Aren't we funny today..."
# do_tame sends this one with a space after its line end; the marker leaves the space out.
UNSKILLED_MARKER = "Learn how to tame properly first!"
# Every reply do_tame can give, in the order it checks them.
TAME_REPLY_MARKERS = [
    "You are too insubstantial to do that.",
    "You are too exhausted.",
    "Tame who?",
    "Your victim is no longer among us.",
    "You are too airy for that.",
    ANIMALS_ONLY_MARKER,
    ENRAGED_MARKER,
    NEEDS_STANDING_MARKER,
    ALREADY_FOLLOWING_MARKER,
    TOO_POWERFUL_MARKER,
    SELF_TAME_MARKER,
    UNSKILLED_MARKER,
    "You are too busy to do that.",
    "You target is not here any longer.",
    "Sorry, following in circles is not allowed.",
    ANY_TAMED_MARKER,
    FAILED_TAME_MARKER,
]
# The most tame attempts made before the test fails. Nothing limits the attempts: a failed one
# costs no moves (only a success takes 60) and the PEACEROOM refuses the boar's answering attack,
# so the bound is a count. At 8 failures in 101 per attempt (see the margin comment in
# check_tame_chance), 30 attempts all fail about once in 10^33 runs.
MAX_TAME_ATTEMPTS = 30
# Seconds to wait for a tame's outcome. The delay is 60 beats * target level * 2 / (ranger level
# 30 + 1) passes: 3 for the boar, 54 (13.5 seconds) for the level-14 bear, the most a scenario
# can meet if a refusal stops refusing; 20 leaves room for a loaded host or emulated i386.
TAME_TIMEOUT = 20.0
# Seconds to keep reading after an outcome, for text already sent to arrive.
SETTLE_SECONDS = 1.0

# What the whistler reads when the whistle carries (do_whistle, ranger.cpp).
WHISTLED_MARKER = "You whistle powerfully."
# What the whistler reads when the knowledge roll fails (do_whistle).
WEAK_WHISTLE_MARKER = "You whistle, but can barely hear yourself."
# What the whistler reads as a shadow (do_whistle).
SHADOW_WHISTLE_MARKER = "You need to breathe to whistle!"
# What a character elsewhere in the whistler's zone reads (do_whistle).
DISTANT_WHISTLE_MARKER = "You hear a powerful whistle nearby."
# What the room reads when a fighting tame tries to flee, then when it gets away (do_flee,
# act_offe.cpp).
PANICKED_MARKER = "A pen boar panics, and attempts to flee!"
FLED_MARKER = "A pen boar flees head over heels!"
# What the whistler in the hub reads when its tame hunts its way back from the north spoke.
RETURNED_MARKER = "A pen boar enters from the north."
# What everyone in the room but the boar and its target reads when the boar attacks (do_hit).
BOAR_ATTACKS_HARDY_ORC_MARKER = "A pen boar attacks a hardy target orc!"
BOAR_ATTACKS_TARGET_ORC_MARKER = "A pen boar attacks a target orc!"
# The other replies the tamer can read to `order` (do_order, act_offe.cpp).
ORDER_REFUSAL_MARKERS = ["That person isn't here.", "has an indifferent look.", "Order who to do what?"]
# Seconds to wait for the whistle's outcome: whistle's 6 beats are 6 passes, a second and a half.
WHISTLE_TIMEOUT = 8.0

# The stat line just before the affect lines (do_stat_character, act_wiz.cpp).
STAT_LAST_LINE_MARKER = "VUL:"
# What `stat mob` prints when no mob matches (do_wizstat, act_wiz.cpp).
NO_SUCH_MOB_MARKER = "No such mobile around."
# The tamer's ranger level, which tame's level check and success chance read.
RANGER_LEVEL_PATTERN = re.compile(r"Class levels: Mag:\d+ Cle:\d+ Ran:(\d+) ")
# A practice line for tame at full knowledge (do_practice and how_good, spec_pro.cpp): how_good
# says Mastered only above 99, and knowledge stops at 100.
TAME_MASTERED_PATTERN = re.compile(r"^\s*tame\s+\(Mastered\)", re.MULTILINE)
# Any practice line for tame (do_practice lists only skills with knowledge above 0).
TAME_PRACTICE_PATTERN = re.compile(r"^\s*tame\s", re.MULTILINE)
# A mob's affect bits in `stat mob`: sprintbit (utility.cpp) puts a space before each name and a
# full stop after the last.
AFFECT_BITS_PATTERN = re.compile(r"^\s*AFF: (.*)$", re.MULTILINE)
# The tame affect row in `stat mob`: duration -1 shows as 0 hours, and it sets CHARM.
TAME_AFFECT_PATTERN = re.compile(r"^\s*SPL: \(\s*0hr\) tame\s+sets\s+CHARM\.", re.MULTILINE)
# The whistle's recall affect row in `stat mob` (SPELL_ACTIVITY, which sets HUNT).
ACTIVITY_AFFECT_PATTERN = re.compile(r"^\s*SPL: \(\s*\d+hr\) activity\s.*sets\s+HUNT\.", re.MULTILINE)
# One line of the immortal `where <keyword>` list for the boar (perform_immort_where,
# act_info.cpp): `%3d. %-25s - [%5d] %s`, the room's vnum in brackets.
BOAR_WHERE_LINE_PATTERN = re.compile(r"^\s*\d+\. " + re.escape(BOAR_NAME) + r"\s+- \[\s*(\d+)\]", re.MULTILINE)


def boar_rooms(implementor: GameSession) -> list:
    """The room vnum of every pen boar in the world, from the implementor's `where boar`."""
    reply = implementor.command(f"where {BOAR_KEYWORD}")
    return sorted(int(room) for room in BOAR_WHERE_LINE_PATTERN.findall(reply.text))


def reset_boar_zone(implementor: GameSession) -> None:
    """Resets zone 18 at once through the implementor's `zreset`."""
    reply = implementor.command(f"zreset {BOAR_ZONE}")
    assert reply.contains(ZONE_RESET_MARKER), reply.text


def stat_mob(implementor: GameSession, keyword: str) -> str:
    """The implementor's whole `stat mob <keyword>` reply, whose lines can end in `]`."""
    implementor.send_line(f"stat mob {keyword}")
    return implementor.expect([STAT_LAST_LINE_MARKER, NO_SUCH_MOB_MARKER]) + implementor.drain(SETTLE_SECONDS)


def affect_bits(stat_text: str) -> list:
    """The affect bit names on a `stat` reply's AFF line."""
    match = AFFECT_BITS_PATTERN.search(stat_text)
    assert match is not None, stat_text
    return [name.rstrip(".") for name in match.group(1).split()]


def load_mob(implementor: GameSession, vnum: int) -> None:
    """Loads one copy of mob vnum into the implementor's room."""
    reply = implementor.command(f"load mob {vnum}")
    assert reply.contains("You create"), reply.text


def goto(immortal: GameSession, room: int) -> None:
    """Moves an immortal to room and checks the command was accepted."""
    reply = immortal.command(f"goto {room}")
    assert not reply.contains("No room exists with that number.") and not reply.contains("Huh?!"), reply.text


def ranger_level(implementor: GameSession, character: Character) -> int:
    """The character's ranger level from the implementor's `stat player`."""
    implementor.send_line(f"stat player {character.name.lower()}")
    stat_text = implementor.expect([STAT_LAST_LINE_MARKER]) + implementor.drain(SETTLE_SECONDS)
    match = RANGER_LEVEL_PATTERN.search(stat_text)
    assert match is not None, stat_text
    return int(match.group(1))


def check_tame_inputs(implementor: GameSession, tamer: GameSession, character: Character) -> None:
    """Asserts the ranger level 30 and the mastered tame every tame margin below rests on."""
    assert ranger_level(implementor, character) == 30
    practice = tamer.command("practice")
    assert TAME_MASTERED_PATTERN.search(practice.text), practice.text


def check_tame_chance(implementor: GameSession, tamer: GameSession, character: Character) -> None:
    """Asserts the numbers behind tame's success roll on the pen boar, which is not roll-proof."""
    check_tame_inputs(implementor, tamer, character)
    # Tame's success roll on the pen boar (tame knowledge 100, Mastered in `practice`):
    #   tmp_abil = (int)(GET_SKILL / 110.0 * 95 + Ran / 36.0 * 9)          (do_tame, ranger.cpp)
    #            = (int)(100 / 110.0 * 95    + 30 / 36.0 * 9)              (Ran:30)
    #              (no anger affect, so no 20% cut)
    #            = (int)(86.36 + 7.5) = 93
    #   tamed    = tmp_abil > number(0, 100): 93 of 101 draws succeed, NOT roll-proof, so a
    #              failure is retried. The level check passes: (int)(30 / 3.0 + 100 / 30.0)
    #              - boar level 1 - no followers = 13 - 1 - 0 = 12, not below 0.
    assert int(100 / 110 * 95 + 30 / 36 * 9) == 93


def tame_boar(tamer: GameSession) -> str:
    """Tames the boar in the tamer's room; returns what the tamer read on success.

    The success roll is the one draw on the path, and a failure only gates the attempt, so the
    attempt is repeated after FAILED_TAME_MARKER, at most MAX_TAME_ATTEMPTS times.
    """
    for _attempt in range(MAX_TAME_ATTEMPTS):
        tamer.send_line(f"tame {BOAR_KEYWORD}")
        reply = tamer.expect(TAME_REPLY_MARKERS, timeout=TAME_TIMEOUT) + tamer.drain(SETTLE_SECONDS)
        if FAILED_TAME_MARKER not in reply:
            assert TAMED_MARKER in reply, reply
            return reply
    pytest.fail(f"the tame failed {MAX_TAME_ATTEMPTS} times running")


def tame_and_walk_to_hub(implementor: GameSession, tamer: GameSession) -> None:
    """Tames the pen boar and walks the tamer east twice to the whistle hub, the boar following."""
    check_tame_chance(implementor, tamer, TAMER)
    tame_boar(tamer)
    assert tamer.command("east").room_name() == "Pen Gate"
    assert tamer.command("east").room_name() == "Whistle Hub"
    assert boar_rooms(implementor) == [ROOM_WHISTLE_HUB]


def whistle(tamer: GameSession) -> str:
    """Whistles and returns what the whistler read once the delay ended."""
    tamer.send_line("whistle")
    outcomes = [WHISTLED_MARKER, WEAK_WHISTLE_MARKER, SHADOW_WHISTLE_MARKER]
    return tamer.expect(outcomes, timeout=WHISTLE_TIMEOUT) + tamer.drain(SETTLE_SECONDS)


def set_tame_on_hardy_orc(implementor: GameSession, tamer: GameSession) -> None:
    """Loads the hardy target orc into the whistle hub and orders the tame to attack it.

    do_hit (act_offe.cpp) runs one hit() for the boar, and every way out of hit() that the
    boar can take sets both sides fighting: a dodge or parry falls through to set_fighting, and
    a hit calls damage(), which sets them. The boar's damage cannot floor the orc's 900 hit
    points, and the affect-hours schedule runs no combat rounds after it.
    """
    goto(implementor, ROOM_WHISTLE_HUB)
    load_mob(implementor, HARDY_ORC_VNUM)
    tamer.send_line("order boar kill target")
    ordered = tamer.expect([BOAR_ATTACKS_HARDY_ORC_MARKER, *ORDER_REFUSAL_MARKERS]) + tamer.drain(SETTLE_SECONDS)
    assert BOAR_ATTACKS_HARDY_ORC_MARKER in ordered, ordered
    fighting = stat_mob(implementor, BOAR_KEYWORD)
    assert "Pos: Fighting, Fighting: a hardy target orc" in fighting, fighting


@pytest.mark.parametrize("removal_command, removal_reply", [("purge", "Ok."), ("slay", "You chop")], ids=["purged", "slain"])
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, TAMER)
def test_a_tamed_animal_frees_its_zone_slot_once_so_resets_keep_one_wild_copy(server, removal_command, removal_reply) -> None:
    """New-behaviour pin: a tamed animal that is purged or slain does not free its zone slot again.

    #314 made do_tame (ranger.cpp) clear the tame's load line after releasing its slot, so
    extract_char (handler.cpp) no longer releases the same slot again when the tame leaves the
    world, whether `purge` extracts it at once or `slay` kills it (raw_kill, fight.cpp). Before
    it, the removal dropped the reset row's existing count below the wild copy it still had, and
    the next reset loaded a second wild boar. Zone 18 resets one boar with max existing 1;
    zreset runs inside the command, so the affect-hours schedule's lack of zone updates does not
    matter and keeps every other draw away. The tame's success roll is the one draw that can
    change the outcome, a gating roll that tame_boar repeats.
    """
    implementor = server.login(IMPLEMENTOR.name)
    tamer = server.login(TAMER.name)

    assert boar_rooms(implementor) == [world.ROOM_BOAR_PEN]
    check_tame_chance(implementor, tamer, TAMER)
    tame_boar(tamer)

    tame_stat = stat_mob(implementor, BOAR_KEYWORD)
    assert f"Master is: {TAMER.name}," in tame_stat, tame_stat
    assert "CHARM" in affect_bits(tame_stat), tame_stat
    assert TAME_AFFECT_PATTERN.search(tame_stat), tame_stat

    # The tame follows its tamer out of the pen, so where tells it from the wild copy by room.
    assert tamer.command("east").room_name() == "Pen Gate"
    assert boar_rooms(implementor) == [ROOM_PEN_GATE]

    reset_boar_zone(implementor)
    assert boar_rooms(implementor) == [world.ROOM_BOAR_PEN, ROOM_PEN_GATE]

    goto(implementor, ROOM_PEN_GATE)
    removed = implementor.command(f"{removal_command} {BOAR_KEYWORD}")
    assert removed.contains(removal_reply), removed.text
    assert boar_rooms(implementor) == [world.ROOM_BOAR_PEN]

    reset_boar_zone(implementor)
    reset_boar_zone(implementor)
    assert boar_rooms(implementor) == [world.ROOM_BOAR_PEN]


# Each case: the mobs the implementor loads at the pen gate, the command it then gives with the
# reply it reads, the keyword the tamer tames, and the refusal the tamer reads.
REFUSAL_CASES = [
    pytest.param((), None, None, IMPLEMENTOR.name.lower(), ANIMALS_ONLY_MARKER, id="a_player"),
    pytest.param((SCARECROW_VNUM,), None, None, "scarecrow", ANIMALS_ONLY_MARKER, id="a_creature_that_is_not_an_animal"),
    pytest.param((OWL_VNUM,), None, None, "owl", ANIMALS_ONLY_MARKER, id="an_animal_with_perception"),
    pytest.param(
        (BOAR_VNUM, TARGET_ORC_VNUM), "force boar kill orc", BOAR_ATTACKS_TARGET_ORC_MARKER, BOAR_KEYWORD, ENRAGED_MARKER,
        id="a_fighting_animal",
    ),
    pytest.param(
        (BOAR_VNUM,), "force boar sleep", "A pen boar lies down and falls asleep.", BOAR_KEYWORD, NEEDS_STANDING_MARKER,
        id="a_sleeping_animal",
    ),
    pytest.param(
        (BOAR_VNUM,), "force boar follow testimm", "A pen boar starts following you.", BOAR_KEYWORD, ALREADY_FOLLOWING_MARKER,
        id="an_animal_following_someone",
    ),
    pytest.param((BEAR_VNUM,), None, None, "bear", TOO_POWERFUL_MARKER, id="an_animal_above_the_level_cap"),
]


@pytest.mark.parametrize("mob_vnums, setup_command, setup_reply, target, refusal", REFUSAL_CASES)
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, TAMER_AT_GATE)
def test_tame_refuses_a_target_it_cannot_tame_before_any_roll(server, mob_vnums, setup_command, setup_reply, target, refusal) -> None:
    """New-behaviour pin: tame refuses players, non-animals, perceptive, fighting, lying, following
    and too-strong animals at once.

    do_tame (ranger.cpp) checks the target in this order, before its delay and its one roll:
    perception, NPC and animal bodytype (one check; the owl fails only its perception term and
    the scarecrow only its bodytype term), then fighting, then a position below fighting, then a
    master, then the level cap, which #218 made count the ranger's other tames (here there are
    none). Each animal case fails only its own check: the boar copies fight, sleep or follow
    only as set up, and the bear is a standing animal with perception 0 and no master. The
    affect-hours schedule keeps every other draw away; a boar set on the orc draws in hit() but
    ends up fighting whatever the draws (see set_tame_on_hardy_orc).
    """
    implementor = server.login(IMPLEMENTOR.name)
    tamer = server.login(TAMER_AT_GATE.name)

    goto(implementor, ROOM_PEN_GATE)
    for vnum in mob_vnums:
        load_mob(implementor, vnum)
    if setup_command is not None:
        implementor.send_line(setup_command)
        setup_text = implementor.expect([setup_reply, "No-one by that name here..."]) + implementor.drain(SETTLE_SECONDS)
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
    assert int(30 / 3 + 100 / 30) - 14 - 0 == -1

    tamer.send_line(f"tame {target}")
    reply = tamer.expect(TAME_REPLY_MARKERS, timeout=TAME_TIMEOUT) + tamer.drain(SETTLE_SECONDS)
    assert refusal in reply, reply


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, NOVICE)
def test_tame_refuses_a_ranger_who_has_not_learned_it(server) -> None:
    """New-behaviour pin: a ranger without tame knowledge is told to learn it, before any roll.

    do_tame (ranger.cpp) checks GET_SKILL(tame) after the target checks and before the delay.
    No roll is on the path.
    """
    implementor = server.login(IMPLEMENTOR.name)
    novice = server.login(NOVICE.name)

    goto(implementor, ROOM_PEN_GATE)
    load_mob(implementor, BOAR_VNUM)
    assert ranger_level(implementor, NOVICE) == 30
    practice = novice.command("practice")
    assert not TAME_PRACTICE_PATTERN.search(practice.text), practice.text
    # The level cap, which comes first (do_tame, ranger.cpp):
    #   levels_over_required = (int)(Ran / 3.0 + tame_skill / 30.0) - GET_LEVEL(victim)
    #                          - get_followers_level(ch)
    #            = (int)(30 / 3.0 + 0 / 30.0) - 1 - 0     (Ran:30; tame not in `practice`)
    #            = 10 - 1 - 0 = 9, not below 0: the knowledge check is reached.
    assert int(30 / 3 + 0 / 30) - 1 - 0 == 9

    novice.send_line(f"tame {BOAR_KEYWORD}")
    reply = novice.expect(TAME_REPLY_MARKERS, timeout=TAME_TIMEOUT) + novice.drain(SETTLE_SECONDS)
    assert UNSKILLED_MARKER in reply, reply


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, TAMER)
def test_a_tames_level_counts_against_the_rangers_next_tame(server) -> None:
    """New-behaviour pin: a ranger's tame counts at least two levels against its next tame.

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
    goto(implementor, world.ROOM_BOAR_PEN)
    load_mob(implementor, WOLF_VNUM)

    # The level cap for the wolf, with the boar tamed (do_tame, ranger.cpp):
    #   levels_over_required = (int)(Ran / 3.0 + tame_skill / 30.0) - GET_LEVEL(victim)
    #                          - get_followers_level(ch)
    #            = (int)(30 / 3.0 + 100 / 30.0) - 12 - max(2, 1)
    #              (Ran:30, tame Mastered; the wolf is level 12; the boar, level 1, counts 2)
    #            = 13 - 12 - 2 = -1, below 0: refused. Without the boar: 13 - 12 - 0 = 1.
    assert int(30 / 3 + 100 / 30) - 12 - max(2, 1) == -1

    tamer.send_line("tame wolf")
    reply = tamer.expect(TAME_REPLY_MARKERS, timeout=TAME_TIMEOUT) + tamer.drain(SETTLE_SECONDS)
    assert TOO_POWERFUL_MARKER in reply, reply


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, TAMER)
def test_whistle_makes_a_fighting_tame_flee_its_fight(server) -> None:
    """New-behaviour pin: a whistle makes the whistler's fighting tame flee its fight.

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
    assert WHISTLED_MARKER in whistled, whistled
    assert PANICKED_MARKER in whistled, whistled
    assert FLED_MARKER in whistled, whistled

    rooms = boar_rooms(implementor)
    assert len(rooms) == 1 and rooms[0] in HUB_SPOKES, rooms
    fled = stat_mob(implementor, BOAR_KEYWORD)
    assert "Fighting: Nobody" in fled, fled


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, TAMER)
def test_whistle_recalls_the_whistlers_tame_from_elsewhere_in_the_zone(server) -> None:
    """New-behaviour pin: a whistle sends the whistler's tame back to it, and leaves a wild animal.

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
    goto(implementor, ROOM_NORTH_SPOKE)
    transferred = implementor.command(f"transfer {BOAR_KEYWORD}")
    assert transferred.contains("A pen boar arrives from a puff of smoke."), transferred.text
    goto(implementor, ROOM_EAST_SPOKE)
    load_mob(implementor, BOAR_VNUM)
    assert boar_rooms(implementor) == [ROOM_NORTH_SPOKE, ROOM_EAST_SPOKE]

    whistled = whistle(tamer)
    assert WHISTLED_MARKER in whistled, whistled
    heard = implementor.expect([DISTANT_WHISTLE_MARKER]) + implementor.drain(SETTLE_SECONDS)
    assert DISTANT_WHISTLE_MARKER in heard, heard

    wild = stat_mob(implementor, BOAR_KEYWORD)
    assert "Master is: <none>," in wild, wild
    assert not ACTIVITY_AFFECT_PATTERN.search(wild), wild
    assert "HUNT" not in affect_bits(wild), wild

    goto(implementor, ROOM_NORTH_SPOKE)
    recalled = stat_mob(implementor, BOAR_KEYWORD)
    assert f"Master is: {TAMER.name}," in recalled, recalled
    assert ACTIVITY_AFFECT_PATTERN.search(recalled), recalled
    assert "HUNT" in affect_bits(recalled), recalled

    hours_text = ""
    for _whistle in range(3):
        server.advance_hours(1)
        hours_text += tamer.drain(SETTLE_SECONDS)
        if boar_rooms(implementor) == [ROOM_WHISTLE_HUB, ROOM_EAST_SPOKE]:
            break
        assert WHISTLED_MARKER in whistle(tamer)
    assert boar_rooms(implementor) == [ROOM_WHISTLE_HUB, ROOM_EAST_SPOKE]
    assert RETURNED_MARKER in hours_text, hours_text
