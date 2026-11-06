"""Practice-session counts: the formula a login and a level change apply, and pracreset from the
angel and the immortal command."""

from __future__ import annotations

import dataclasses
import re

import pytest

from rots_harness import world
from rots_harness.characters import IMPLEMENTOR, Character, StoredValues
from rots_harness.session import GameSession
from rots_harness.world_resets import MobReset

pytestmark = [pytest.mark.scenario, pytest.mark.practice, pytest.mark.practice_sessions]

# The blink sessions the pupil has spent; three put blink at 84% (recalc_skills, spec_pro.cpp).
BLINK_SESSIONS_SPENT = 3
# A level-30 human mage in the arena centre with those blink sessions spent.
PUPIL = Character(
    name="Testreset",
    race="human",
    sex="female",
    level=30,
    load_room=world.ROOM_ARENA_CENTRE,
    class_name="mage",
    skills=(("blink", BLINK_SESSIONS_SPENT),),
)

# The traveller (mob/15.mob), a guildmaster who teaches blink up to 100; a test that needs it has
# the world place it in the arena centre.
TRAVELLER_VNUM = 1503
TRAVELLER_LIST_HEADER = "Hello, Testreset! I can teach you these spells:"
# The shining angel (mob/11.mob): MOB_SPEC with program 14, the resetter (spec_ass.cpp).
ANGEL_VNUM = 1170
# A mob that is not a player and has no special (mob/11.mob), for the command's mob refusal.
BYSTANDER_VNUM = 1132

# The learning the pupil is stored with for a known session count: wizset's top, above any
# rolled learning.
LEARNING = 40
# The specialization the pupil has before a reset, and its PLRSPEC_FIRE number (structs.h).
FIRE_SPECIALIZATION_NAME = "fire"
FIRE_SPECIALIZATION = 1
# The pupil stored with LEARNING, so its sessions are known from its first login.
LEARNED_PUPIL = dataclasses.replace(PUPIL, stored=StoredValues(lea=LEARNING))
# The learned pupil, also specialized in fire.
FIRE_PUPIL = dataclasses.replace(PUPIL, stored=StoredValues(lea=LEARNING, specialization=FIRE_SPECIALIZATION_NAME))

# The first line of the plain practice list (do_practice, spec_pro.cpp); it has no full stop.
PLAIN_SESSIONS_PATTERN = re.compile(r"You have (-?\d+) practice sessions left\r?\n")
# A plain practice list row: the name, then how well it is known in parentheses (how_good).
PLAIN_ROW_PATTERN_FORMAT = r"^\r?{}\s+\(([A-Za-z ]+)\)"
# A guildmaster list row: name, knowledge, sessions spent (SPECIAL(guild), spec_pro.cpp).
GUILD_ROW_PATTERN_FORMAT = r"^\r?{}\s+(\d+)%\s+(\d+)\s+Taught to: "
# The guildmaster list's session count.
GUILD_SESSIONS_PATTERN = re.compile(r"You have (\d+) practice sessions left\.")
# Values in `stat player` (do_stat_character, act_wiz.cpp): learning as current/base/constant,
# the level, the specialization number and the practice sessions.
LEARNING_PATTERN = re.compile(r"Lea:\[(-?\d+)/(-?\d+)/(-?\d+)\]")
LEVEL_PATTERN = re.compile(r"Lev: \[\s*(\d+)\]")
SPECIALIZATION_PATTERN = re.compile(r"Spec:\((-?\d+)\)")
STAT_SESSIONS_PATTERN = re.compile(r"\(pracs left:\[(-?\d+)\]\)")

# What the pupil reads on a reset (do_pracreset, spec_pro.cpp); format with the resetter's name
# as act() starts a line with it.
RESET_MARKER_FORMAT = "{} waves his hand over your brow and you feel yourself a total rookie."
RESET_DONE_MARKER = "Your practices have been reset"
# What the implementor reads on a reset.
RESET_REPORT_MARKER = "You reset his/her learning abilities."
# The angel's tells (SPECIAL(resetter), spec_pro.cpp).
ANGEL_ANSWER_MARKER = "A shining angel tells you 'You may ask me for a pracreset and rerolls.'"
ONLY_LEVEL_SIX_MARKER = "A shining angel tells you 'Only level 6 characters may reroll statistics.'"
# What the pupil reads when the tell finds nobody it can see (do_tell, act_comm.cpp).
NOBODY_MARKER = "Nobody by that name."
# The pupil's own tell to the angel, the last line of every answer: SPECIAL(resetter) runs before
# do_tell (act_comm.cpp) and lets it go on.
TOLD_ANGEL_FORMAT = "You tell a shining angel '{}'"
# The immortal command's refusals (do_pracreset).
NO_SUCH_PERSON_MARKER = "No such person."
CANNOT_LEARN_MARKER = "This person cannot learn anything, don't bother yourself."
# Seconds to keep reading after an outcome, for text already sent to arrive.
SETTLE_SECONDS = 1.0


def stat_player(implementor: GameSession, name: str) -> str:
    """The implementor's `stat player` of the character, read to its last line."""
    implementor.send_line(f"stat player {name.lower()}")
    return implementor.expect(["VUL:"]) + implementor.drain(SETTLE_SECONDS)


def stat_value(pattern: re.Pattern[str], text: str) -> tuple[int, ...]:
    """The numbers pattern captures in a `stat player` text."""
    match = pattern.search(text)
    assert match is not None, f"no {pattern.pattern} in:\n{text}"
    return tuple(int(group) for group in match.groups())


def plain_list(pupil: GameSession) -> str:
    """The plain practice list, read away from any guildmaster."""
    reply = pupil.command("practice")
    assert PLAIN_SESSIONS_PATTERN.search(reply.text) is not None, reply.text
    return reply.text


def plain_sessions(listed: str) -> int:
    """The practice sessions the plain practice list reports."""
    return int(PLAIN_SESSIONS_PATTERN.search(listed).group(1))


def plain_row(listed: str, skill: str) -> str | None:
    """How well the plain practice list says a skill is known, or None when it is not listed."""
    row = re.search(PLAIN_ROW_PATTERN_FORMAT.format(re.escape(skill)), listed, re.MULTILINE)
    return None if row is None else row.group(1)


def tell_angel(pupil: GameSession, text: str) -> str:
    """Sends `tell angel <text>` and returns everything the pupil read, its own tell included."""
    pupil.send_line(f"tell angel {text}")
    return pupil.expect([TOLD_ANGEL_FORMAT.format(text), NOBODY_MARKER]) + pupil.drain(SETTLE_SECONDS)


def relog(server, pupil: GameSession) -> GameSession:
    """Quits the pupil's session and logs the character in again; returns the new session."""
    pupil.quit()
    return server.login(PUPIL.name)


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, FIRE_PUPIL)
@pytest.mark.world_resets(MobReset(ANGEL_VNUM, world.ROOM_ARENA_CENTRE))
def test_telling_the_angel_pracreset_resets_knowledge_sessions_and_specialization(server) -> None:
    """`tell angel pracreset` empties the skills, refills the sessions, clears the specialization.

    Long-standing: SPECIAL(resetter) calls do_pracreset (spec_pro.cpp). The pupil reads the angel's two reset
    lines before its own tell. Blink leaves the plain list, the race's language comes back with
    recalc_skills, and the sessions are the full count from the current learning. Nothing on the
    way draws a random number.
    """
    implementor = server.login(IMPLEMENTOR.name)
    pupil = server.login(PUPIL.name)
    before = stat_player(implementor, PUPIL.name)
    assert stat_value(SPECIALIZATION_PATTERN, before) == (FIRE_SPECIALIZATION,)
    level = stat_value(LEVEL_PATTERN, before)[0]
    current_learning = stat_value(LEARNING_PATTERN, before)[0]
    assert (level, current_learning) == (30, LEARNING)
    assert plain_row(plain_list(pupil), "blink") is not None

    reply = tell_angel(pupil, "pracreset")
    assert RESET_MARKER_FORMAT.format("A shining angel") in reply, reply
    reset_at = reply.index(RESET_MARKER_FORMAT.format("A shining angel"))
    told_at = reply.index(TOLD_ANGEL_FORMAT.format("pracreset"))
    assert reset_at < reply.index(RESET_DONE_MARKER) < told_at, reply

    listed = plain_list(pupil)
    assert plain_row(listed, "blink") is None, listed
    assert plain_row(listed, "human language") == "Mastered", listed
    # do_pracreset:
    #   spells_to_learn = GET_LEVEL * PRACS_PER_LEVEL + GET_LEVEL * GET_LEA / LEA_PRAC_FACTOR + 10
    #                   = 30 * 3 + 30 * 40 / 5 + 10      (Lev: [30]; Lea current 40, as stored)
    #                   = 90 + 240 + 10 = 340
    assert plain_sessions(listed) == 340, listed
    after = stat_player(implementor, PUPIL.name)
    assert stat_value(SPECIALIZATION_PATTERN, after) == (0,), after


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(PUPIL)
@pytest.mark.world_resets(MobReset(ANGEL_VNUM, world.ROOM_ARENA_CENTRE))
@pytest.mark.parametrize(
    ("told", "answer"),
    [("hello", ANGEL_ANSWER_MARKER), ("reroll", ONLY_LEVEL_SIX_MARKER)],
    ids=["anything-else", "reroll-above-level-6"],
)
def test_the_angel_answers_anything_but_pracreset_with_a_tell_and_resets_nothing(
    server, told: str, answer: str
) -> None:
    """The angel tells a pupil what it offers, or that only level 6 may reroll (SPECIAL(resetter)).

    Long-standing. The level-30 pupil keeps blink and its session count. Nothing draws a random
    number.
    """
    pupil = server.login(PUPIL.name)
    before = plain_list(pupil)

    reply = tell_angel(pupil, told)
    assert answer in reply, reply
    assert reply.index(answer) < reply.index(TOLD_ANGEL_FORMAT.format(told)), reply
    assert RESET_DONE_MARKER not in reply, reply
    after = plain_list(pupil)
    assert plain_row(before, "blink") is not None, before
    assert plain_row(after, "blink") == plain_row(before, "blink"), after
    assert plain_sessions(after) == plain_sessions(before), after


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR)
@pytest.mark.world_resets(MobReset(BYSTANDER_VNUM, world.ROOM_ARENA_CENTRE))
@pytest.mark.parametrize(
    ("argument", "refusal"),
    [("nobody", NO_SUCH_PERSON_MARKER), ("bystander", CANNOT_LEARN_MARKER)],
    ids=["nobody-by-that-name", "a-mob"],
)
def test_the_pracreset_command_refuses_a_name_that_is_not_a_player(
    server, argument: str, refusal: str
) -> None:
    """`pracreset` with a name nobody has, or a mob's name, is refused (do_pracreset; long-standing)."""
    implementor = server.login(IMPLEMENTOR.name)

    reply = implementor.command(f"pracreset {argument}")
    assert reply.contains(refusal), reply.text
    assert not reply.contains(RESET_REPORT_MARKER), reply.text


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, LEARNED_PUPIL)
def test_the_pracreset_command_resets_the_named_player(server) -> None:
    """`pracreset <player>` empties every skill and refills the sessions (do_pracreset; long-standing).

    The implementor reads the report, the pupil the two reset lines naming the implementor.
    Nothing on the way draws a random number.
    """
    implementor = server.login(IMPLEMENTOR.name)
    pupil = server.login(PUPIL.name)

    reply = implementor.command(f"pracreset {PUPIL.name.lower()}")
    assert reply.contains(RESET_REPORT_MARKER), reply.text
    told = pupil.expect([RESET_DONE_MARKER]) + pupil.drain(SETTLE_SECONDS)
    assert RESET_MARKER_FORMAT.format(IMPLEMENTOR.name) in told, told
    reset_at = told.index(RESET_MARKER_FORMAT.format(IMPLEMENTOR.name))
    assert reset_at < told.index(RESET_DONE_MARKER), told

    listed = plain_list(pupil)
    assert plain_row(listed, "blink") is None, listed
    # do_pracreset: 30 * 3 + 30 * 40 / 5 + 10 = 90 + 240 + 10 = 340 (Lev: [30]; Lea current 40).
    assert plain_sessions(listed) == 340, listed


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, PUPIL)
def test_learning_set_by_an_immortal_changes_the_sessions_only_at_the_next_login(server) -> None:
    """`wizset ... lea` leaves the session count alone; the next login recounts it by the formula.

    Long-standing: do_wizset (act_wiz.cpp) only sets the constant; the login recount (interpre.cpp) applies
    get_max_practice_count. Nothing on the way draws a random number.
    """
    implementor = server.login(IMPLEMENTOR.name)
    pupil = server.login(PUPIL.name)
    before = plain_sessions(plain_list(pupil))

    reply = implementor.command(f"wizset player {PUPIL.name.lower()} lea {LEARNING}")
    assert reply.contains(f"{PUPIL.name}'s lea set to {LEARNING}."), reply.text
    learning = stat_value(LEARNING_PATTERN, stat_player(implementor, PUPIL.name))
    assert learning[1:] == (LEARNING, LEARNING)
    assert plain_sessions(plain_list(pupil)) == before

    pupil = relog(server, pupil)
    after = stat_player(implementor, PUPIL.name)
    assert stat_value(LEVEL_PATTERN, after) == (30,)
    assert stat_value(LEARNING_PATTERN, after)[1] == LEARNING
    # get_max_practice_count - get_spent_practice_count (char_utils.cpp):
    #   level * PRACS_PER_LEVEL + level * abilities.lea / LEA_PRAC_FACTOR + 10 - spent
    #   = 30 * 3 + 30 * 40 / 5 + 10 - 3        (Lev: [30]; Lea base 40; 3 blink sessions spent)
    #   = 90 + 240 + 10 - 3 = 337
    # The rolled learning is below 40, so the count before the relog was lower.
    assert before < 337
    assert plain_sessions(plain_list(pupil)) == 337


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(PUPIL)
@pytest.mark.world_resets(MobReset(TRAVELLER_VNUM, world.ROOM_ARENA_CENTRE))
def test_a_relog_leaves_knowledge_spent_and_remaining_sessions_unchanged(server) -> None:
    """A relog after practising keeps the knowledge, sessions spent and sessions left unchanged.

    Long-standing: the login recount (interpre.cpp) and recalc_skills give back what the
    traveller listed. A
    fourth blink session takes blink to 96%, below the traveller's limit of 100, so no cap is
    involved. Practice draws no random number.
    """
    pupil = server.login(PUPIL.name)
    pupil.send_line("practice blink")
    pupil.expect(["You practice it for a while."])
    before = pupil.command("practice")
    assert before.contains(TRAVELLER_LIST_HEADER), before.text
    blink_before = re.search(GUILD_ROW_PATTERN_FORMAT.format("blink"), before.text, re.MULTILINE)
    assert blink_before is not None and blink_before.groups() == ("96", "4"), before.text

    pupil = relog(server, pupil)
    after = pupil.command("practice")
    assert after.contains(TRAVELLER_LIST_HEADER), after.text
    blink_after = re.search(GUILD_ROW_PATTERN_FORMAT.format("blink"), after.text, re.MULTILINE)
    assert blink_after is not None and blink_after.groups() == ("96", "4"), after.text
    sessions_before = GUILD_SESSIONS_PATTERN.search(before.text)
    sessions_after = GUILD_SESSIONS_PATTERN.search(after.text)
    assert sessions_before is not None and sessions_after is not None, after.text
    assert sessions_after.group(1) == sessions_before.group(1), after.text


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, LEARNED_PUPIL)
def test_advancing_down_and_back_up_moves_the_sessions_by_the_formula(server) -> None:
    """`advance` to a lower level takes each level's sessions off; advancing back recounts them.

    Long-standing: do_advance (act_wiz.cpp) lowers the sessions by adv * PRACS_PER_LEVEL +
    adv * base learning / LEA_PRAC_FACTOR; each level gained runs advance_level (profs.cpp),
    whose last step is the recount. The pupil was level 30 before, so the way back up rolls no
    hit points, rerolls no abilities and raises no stat (those need a mini-level above its
    highest, mml in `stat`). Learning 40 makes each level's share, 40 / 5, a whole number, so
    the subtraction going down and the recount agree here; for learning that is not a multiple
    of 5 they differ by the dropped fractions.
    """
    implementor = server.login(IMPLEMENTOR.name)
    server.login(PUPIL.name)
    # Recount at login: 30 * 3 + 30 * 40 / 5 + 10 - 3 = 90 + 240 + 10 - 3 = 337
    # (Lev: [30]; Lea base 40; 3 blink sessions spent).
    assert stat_value(STAT_SESSIONS_PATTERN, stat_player(implementor, PUPIL.name)) == (337,)

    reply = implementor.command(f"advance {PUPIL.name.lower()} 20")
    assert reply.contains("Ok."), reply.text
    lowered = stat_player(implementor, PUPIL.name)
    assert stat_value(LEVEL_PATTERN, lowered) == (20,)
    assert stat_value(LEARNING_PATTERN, lowered)[1] == LEARNING
    # do_advance, adv = 20 - 30 = -10:
    #   spells_to_learn += adv * PRACS_PER_LEVEL + (adv * GET_LEA_BASE) / LEA_PRAC_FACTOR
    #                    = -10 * 3 + (-10 * 40) / 5 = -30 - 80 = -110, so 337 - 110 = 227
    assert stat_value(STAT_SESSIONS_PATTERN, lowered) == (227,)

    reply = implementor.command(f"advance {PUPIL.name.lower()} 30")
    assert reply.contains("Ok."), reply.text
    raised = stat_player(implementor, PUPIL.name)
    assert stat_value(LEVEL_PATTERN, raised) == (30,)
    assert stat_value(LEARNING_PATTERN, raised)[1] == LEARNING
    # advance_level's recount at level 30: 30 * 3 + 30 * 40 / 5 + 10 - 3 = 337.
    assert stat_value(STAT_SESSIONS_PATTERN, raised) == (337,)
