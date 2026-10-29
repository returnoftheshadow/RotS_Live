"""Known practice bugs, each pinned as a strict xfail of the behaviour the game should have: the
session counts pracreset, advance and the angel's reroll leave, a bare pracreset, the spoken
language a recount resets, a guildmaster's refusal text and gear bonuses a session drops."""

from __future__ import annotations

import dataclasses
import re

import pytest

from rots_harness import world
from rots_harness.characters import IMPLEMENTOR, Character, StoredValues
from rots_harness.session import GameSession
from rots_harness.world_resets import MobReset

pytestmark = [pytest.mark.scenario, pytest.mark.practice]

# The blink sessions the reset pupil has spent; three put blink at 84% (recalc_skills, spec_pro.cpp).
BLINK_SESSIONS_SPENT = 3
# A level-30 human mage in the arena centre with those blink sessions spent.
RESET_PUPIL = Character(
    name="Testreset",
    race="human",
    sex="female",
    level=30,
    load_room=world.ROOM_ARENA_CENTRE,
    class_name="mage",
    skills=(("blink", BLINK_SESSIONS_SPENT),),
)
# A level-30 human mage in the arena centre who has practiced nothing.
PUPIL = Character(
    name="Testpupil",
    race="human",
    sex="female",
    level=30,
    load_room=world.ROOM_ARENA_CENTRE,
    class_name="mage",
)
# A level-6 human mage: the only level the angel rerolls.
REROLL_PUPIL = Character(
    name="Testroller",
    race="human",
    sex="male",
    level=6,
    load_room=world.ROOM_ARENA_CENTRE,
    class_name="mage",
)

# The traveller (mob/guildmasters.mob), a guildmaster who teaches blink up to 100 to humans.
TRAVELLER_VNUM = 1503
TRAVELLER_CREATED_MARKER = "You create a mysterious traveller."
# The scarred trainer (mob/guildmasters.mob), program 9, the URUK WARRIOR row, whose reject
# message is "$n snarls 'Get out!'"; it teaches only uruk-hai (will_teach 2048).
TRAINER_VNUM = 4608
TRAINER_LINE_START = "A scarred trainer"
# The shining angel (mob/11.mob): MOB_SPEC with program 14, the resetter (spec_ass.cpp).
ANGEL_VNUM = 1170
# The iron circlet (obj/11.obj): head armour whose one affect is APPLY_STR +2.
CIRCLET_VNUM = 1171
CIRCLET_CREATED_MARKER = "You create an iron circlet."
CIRCLET_STRENGTH_BONUS = 2

# The learning the implementor gives the pupil for a known session count: wizset's top, above
# any rolled learning.
LEARNING = 40
# Learning that is not a multiple of LEA_PRAC_FACTOR (5), so a level's share has a fraction.
FRACTIONAL_LEARNING = 37
# The reset pupil stored with FRACTIONAL_LEARNING.
FRACTIONAL_PUPIL = dataclasses.replace(RESET_PUPIL, stored=StoredValues(lea=FRACTIONAL_LEARNING))
# The practice sessions the implementor gives a pupil before practicing.
STARTING_SESSIONS = 20

# The first line of the plain practice list (do_practice, spec_pro.cpp); it has no full stop.
PLAIN_SESSIONS_PATTERN = re.compile(r"You have (-?\d+) practice sessions left\r?\n")
# Values in `stat player` (do_stat_character, act_wiz.cpp): learning and strength as
# current/base/constant, the level and the practice sessions.
LEARNING_PATTERN = re.compile(r"Lea:\[(-?\d+)/(-?\d+)/(-?\d+)\]")
STRENGTH_PATTERN = re.compile(r"Str:\[(-?\d+)/(-?\d+)/(-?\d+)\]")
LEVEL_PATTERN = re.compile(r"Lev: \[\s*(\d+)\]")
STAT_SESSIONS_PATTERN = re.compile(r"\(pracs left:\[(-?\d+)\]\)")

# What the pupil reads on a reset (do_pracreset, spec_pro.cpp).
RESET_DONE_MARKER = "Your practices have been reset"
# What the implementor reads on a reset.
RESET_REPORT_MARKER = "You reset his/her learning abilities."
# The immortal command's refusal without a name (do_pracreset).
WHOM_MARKER = "Whom do you want to reset?"
# What the pupil reads when the tell finds nobody it can see (do_tell, act_comm.cpp).
NOBODY_MARKER = "Nobody by that name."
# The pupil's own tell to the angel, the last line of every answer (SPECIAL(resetter) runs first).
TOLD_ANGEL_FORMAT = "You tell a shining angel '{}'"
# The reroll count after each angel reroll (SPECIAL(resetter)).
REROLLS_LEFT_PATTERN = re.compile(r"You have (\d+) reroll attempts left\.")
# The angel's refusal once the rerolls are used up.
NO_REROLLS_MARKER = "Sorry, you have exceeded the amount of times you can reroll statistics."
# The pupil's own learning after a reroll, as current/base (do_stat, act_info.cpp).
OWN_LEARNING_PATTERN = re.compile(r"Lea:\s*(\d+)/\s*(\d+)\.")

# What `set language <name>` and a bare `set language` reply (do_language, act_othe.cpp).
CHOSEN_LANGUAGE_FORMAT = "You will now use {}."
SPOKEN_LANGUAGE_PATTERN = re.compile(r"You are using ([a-z ]+)\.")
# What the pupil reads on the first session of a practice command at the traveller.
PRACTICED_MARKER = "You practice it for a while."
# What a guildmaster says to a race outside its will_teach mask (SPECIAL(guild), do_say), and
# the scarred trainer's own reject message, the URUK WARRIOR row's reject_message.
GO_AWAY_SPEECH = "says 'Go away, I won't teach you anything!'"
TRAINER_REJECT_MARKER = f"{TRAINER_LINE_START} snarls 'Get out!'"
# Seconds to keep reading after an outcome, for text already sent to arrive.
SETTLE_SECONDS = 1.0


def give_sessions(implementor: GameSession, name: str, sessions: int) -> None:
    """Sets the character's practice sessions to exactly sessions."""
    reply = implementor.command(f"wizset player {name.lower()} practices {sessions}")
    assert reply.contains(f"{name}'s practices set to {sessions}."), reply.text


def stat_player(implementor: GameSession, name: str) -> str:
    """The implementor's `stat player` of the character, read to its last line."""
    implementor.send_line(f"stat player {name.lower()}")
    return implementor.expect(["VUL:"]) + implementor.drain(SETTLE_SECONDS)


def stat_value(pattern: re.Pattern[str], text: str) -> tuple[int, ...]:
    """The numbers pattern captures in a `stat player` text."""
    match = pattern.search(text)
    assert match is not None, f"no {pattern.pattern} in:\n{text}"
    return tuple(int(group) for group in match.groups())


def plain_sessions(pupil: GameSession) -> int:
    """The practice sessions the plain practice list reports, read away from any guildmaster."""
    reply = pupil.command("practice")
    sessions = PLAIN_SESSIONS_PATTERN.search(reply.text)
    assert sessions is not None, reply.text
    return int(sessions.group(1))


def load_mob(implementor: GameSession, vnum: int, created_marker: str) -> None:
    """Moves the implementor to the arena centre and loads the mob there."""
    implementor.command(f"goto {world.ROOM_ARENA_CENTRE}")
    reply = implementor.command(f"load mob {vnum}")
    assert reply.contains(created_marker), reply.text


def tell_angel(pupil: GameSession, text: str) -> str:
    """Sends `tell angel <text>` and returns everything the pupil read, its own tell included."""
    pupil.send_line(f"tell angel {text}")
    return pupil.expect([TOLD_ANGEL_FORMAT.format(text), NOBODY_MARKER]) + pupil.drain(SETTLE_SECONDS)


def spoken_language(pupil: GameSession) -> str:
    """The language a bare `set language` says the character is using."""
    reply = pupil.command("set language")
    spoken = SPOKEN_LANGUAGE_PATTERN.search(reply.text)
    assert spoken is not None, reply.text
    return spoken.group(1)


def practice_once(pupil: GameSession, skill: str) -> str:
    """Sends `practice <skill>` at the traveller and returns everything the pupil read."""
    pupil.send_line(f"practice {skill}")
    return pupil.expect([PRACTICED_MARKER, GO_AWAY_SPEECH, TRAINER_REJECT_MARKER]) + pupil.drain(
        SETTLE_SECONDS
    )


@pytest.mark.practice_sessions
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, RESET_PUPIL)
@pytest.mark.xfail(
    strict=True,
    raises=AssertionError,
    reason="pracreset should leave the session count the next login computes, from base learning",
)
def test_pracreset_leaves_the_session_count_the_next_login_computes(server) -> None:
    """After a pracreset, the sessions should be what the next login's recount gives.

    The bug: do_pracreset (spec_pro.cpp:545) counts from the current learning, GET_LEA, while the
    login recount, get_max_practice_count (char_utils.cpp:1433), reads the base. The implementor
    raises the pupil's constant learning to 40 without a restore: the base follows, the current
    value stays at the rolled one, below the base, as a death or a drained learning leaves it.
    Nothing on the way draws a random number.
    """
    implementor = server.login(IMPLEMENTOR.name)
    pupil = server.login(RESET_PUPIL.name)
    reply = implementor.command(f"wizset player {RESET_PUPIL.name.lower()} lea {LEARNING}")
    assert reply.contains(f"{RESET_PUPIL.name}'s lea set to {LEARNING}."), reply.text
    before = stat_player(implementor, RESET_PUPIL.name)
    current_learning, base_learning, constant_learning = stat_value(LEARNING_PATTERN, before)
    assert (base_learning, constant_learning) == (LEARNING, LEARNING)
    assert current_learning < LEARNING
    assert stat_value(LEVEL_PATTERN, before) == (30,)

    reply = implementor.command(f"pracreset {RESET_PUPIL.name.lower()}")
    assert reply.contains(RESET_REPORT_MARKER), reply.text
    pupil.expect([RESET_DONE_MARKER])
    # get_max_practice_count - get_spent_practice_count, as the next login computes it:
    #   level * PRACS_PER_LEVEL + level * abilities.lea / LEA_PRAC_FACTOR + 10 - spent
    #   = 30 * 3 + 30 * 40 / 5 + 10 - 0        (Lev: [30]; Lea base 40; nothing spent since)
    #   = 90 + 240 + 10 = 340
    # pracreset today gives 30 * 3 + 30 * current / 5 + 10, from the rolled current learning.
    assert plain_sessions(pupil) == 340

    pupil.quit()
    pupil = server.login(RESET_PUPIL.name)
    assert plain_sessions(pupil) == 340


@pytest.mark.practice_sessions
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR)
@pytest.mark.xfail(
    strict=True,
    raises=AssertionError,
    reason="a bare pracreset should ask whom to reset and reset nobody",
)
def test_the_pracreset_command_asks_whom_without_a_name(server) -> None:
    """A bare `pracreset` should ask whom to reset and reset nobody.

    The bug: the command's target mask, FULL_TARGET (interpre.cpp:2012, COMMANDO 119), includes
    TAR_SELF, so target_parser fills the first target with the immortal before do_pracreset's
    empty-argument check (spec_pro.cpp:525-526) runs, and the immortal's own skills are reset.
    """
    implementor = server.login(IMPLEMENTOR.name)

    reply = implementor.command("pracreset")
    assert not reply.contains(RESET_REPORT_MARKER), reply.text
    assert reply.contains(WHOM_MARKER), reply.text


@pytest.mark.practice_sessions
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, FRACTIONAL_PUPIL)
@pytest.mark.xfail(
    strict=True,
    raises=AssertionError,
    reason="advancing down should leave the session count the login recount gives at the new level",
)
def test_advancing_down_leaves_the_count_the_login_recount_gives(server) -> None:
    """`advance` down three levels at learning 37 should leave the recount's 287, not 288.

    The bug: do_advance (act_wiz.cpp:1620) subtracts adv * PRACS_PER_LEVEL +
    (adv * base learning) / LEA_PRAC_FACTOR, whose integer division drops a different fraction
    from the recount's level * learning / LEA_PRAC_FACTOR (get_max_practice_count,
    char_utils.cpp:1433), so the count is off until the next login. The exp-loss level drop
    (limits.cpp:469) subtracts per level the same way. Nothing on the way draws a random number.
    """
    implementor = server.login(IMPLEMENTOR.name)
    server.login(RESET_PUPIL.name)
    # Recount at login: 30 * 3 + 30 * 37 / 5 + 10 - 3 = 90 + 222 + 10 - 3 = 319
    # (Lev: [30]; Lea base 37; 3 blink sessions spent).
    assert stat_value(STAT_SESSIONS_PATTERN, stat_player(implementor, RESET_PUPIL.name)) == (319,)

    reply = implementor.command(f"advance {RESET_PUPIL.name.lower()} 27")
    assert reply.contains("Ok."), reply.text
    lowered = stat_player(implementor, RESET_PUPIL.name)
    assert stat_value(LEVEL_PATTERN, lowered) == (27,)
    assert stat_value(LEARNING_PATTERN, lowered)[1] == FRACTIONAL_LEARNING
    # The recount at level 27: 27 * 3 + 27 * 37 / 5 + 10 - 3 = 81 + 199 + 10 - 3 = 287.
    # do_advance today, adv = -3: 319 + (-3 * 3) + (-3 * 37) / 5 = 319 - 9 - 22 = 288.
    assert stat_value(STAT_SESSIONS_PATTERN, lowered) == (287,)


@pytest.mark.practice_sessions
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, PUPIL)
@pytest.mark.xfail(
    strict=True,
    raises=AssertionError,
    reason="a chosen spoken language should survive a relog and a practice session",
)
@pytest.mark.parametrize("recount", ["relog", "practice-session"])
def test_a_chosen_spoken_language_survives_the_next_skill_recount(server, recount: str) -> None:
    """`set language orcish` should last; a relog or a practice session puts it back to human.

    The bug: recalc_skills (spec_pro.cpp:198) sets player.language from the race every time it
    runs. store_to_char loads the saved language (db.cpp:2835) and then calls recalc_skills
    (db.cpp:2887) at login, and handle_pracs calls it on every session (spec_pro.cpp:332), so
    the choice do_language (act_othe.cpp:1454) accepted is lost. Nothing draws a random number.
    """
    implementor = server.login(IMPLEMENTOR.name)
    pupil = server.login(PUPIL.name)
    reply = pupil.command("set language orcish")
    assert reply.contains(CHOSEN_LANGUAGE_FORMAT.format("orcish language")), reply.text
    assert spoken_language(pupil) == "orcish language"

    if recount == "relog":
        pupil.quit()
        pupil = server.login(PUPIL.name)
    else:
        give_sessions(implementor, PUPIL.name, STARTING_SESSIONS)
        load_mob(implementor, TRAVELLER_VNUM, TRAVELLER_CREATED_MARKER)
        assert PRACTICED_MARKER in practice_once(pupil, "blink")
    assert spoken_language(pupil) == "orcish language"


@pytest.mark.practice_sessions
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, REROLL_PUPIL)
@pytest.mark.world_resets(MobReset(ANGEL_VNUM, world.ROOM_ARENA_CENTRE))
@pytest.mark.xfail(
    strict=True,
    raises=AssertionError,
    reason="the angel's reroll should recount the practice sessions from the new learning",
)
def test_the_angels_reroll_recounts_the_sessions_from_the_new_learning(server) -> None:
    """After `tell angel reroll` changes learning, the sessions should follow the new learning.

    The bug: SPECIAL(resetter) (spec_pro.cpp:3120) calls roll_abilities alone, while the
    immortal's reroll (act_wiz.cpp:2308) and a level gained (profs.cpp:435) both end with
    update_available_practice_sessions, so the count keeps the old learning until the next
    login. The pupil rerolls until its base learning changes: each reroll's abilities come from
    the seeded stream, so the loop ends on the reroll that changes it, bounded by the reroll
    attempts the angel counts down. Each reroll saves the character.
    """
    implementor = server.login(IMPLEMENTOR.name)
    pupil = server.login(REROLL_PUPIL.name)
    before = stat_player(implementor, REROLL_PUPIL.name)
    assert stat_value(LEVEL_PATTERN, before) == (6,)
    original_learning = stat_value(LEARNING_PATTERN, before)[1]
    # The login recount: 6 * 3 + 6 * learning / 5 + 10 - 0 (Lev: [6]; nothing spent).
    assert plain_sessions(pupil) == 6 * 3 + 6 * original_learning // 5 + 10

    rerolled_learning = original_learning
    while rerolled_learning == original_learning:
        reply = tell_angel(pupil, "reroll")
        assert NO_REROLLS_MARKER not in reply, f"the rerolls ran out with learning unchanged:\n{reply}"
        rerolls_left = REROLLS_LEFT_PATTERN.search(reply)
        own_learning = OWN_LEARNING_PATTERN.search(reply)
        assert rerolls_left is not None and own_learning is not None, reply
        rerolled_learning = int(own_learning.group(2))
        if rerolled_learning == original_learning:
            assert int(rerolls_left.group(1)) > 0, f"the rerolls ran out with learning unchanged:\n{reply}"
    assert stat_value(LEARNING_PATTERN, stat_player(implementor, REROLL_PUPIL.name))[1] == rerolled_learning

    # update_available_practice_sessions, as the immortal's reroll applies it:
    #   6 * PRACS_PER_LEVEL + 6 * rerolled base learning / LEA_PRAC_FACTOR + 10 - 0 spent
    # 6 * learning / 5 is strictly increasing in learning, so any change moves the count.
    assert plain_sessions(pupil) == 6 * 3 + 6 * rerolled_learning // 5 + 10


@pytest.mark.practice_limits
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, PUPIL)
@pytest.mark.world_resets(MobReset(TRAINER_VNUM, world.ROOM_ARENA_CENTRE))
@pytest.mark.xfail(
    strict=True,
    raises=AssertionError,
    reason="a race the guildmaster will not teach should read the guildmaster's own reject message",
)
def test_a_race_the_guildmaster_will_not_teach_reads_its_own_reject_message(server) -> None:
    """The scarred trainer should snarl its row's 'Get out!', not the hard-coded go-away line.

    The bug: skill_teach_data's reject_message (spells.h:381) is never read; SPECIAL(guild)
    (spec_pro.cpp:365-367) always says "Go away, I won't teach you anything!". The trainer is a
    copy at 4608 with program 9 (URUK WARRIOR, reject message "$n snarls 'Get out!'") that
    teaches only uruk-hai and speaks human language, so a human pupil understands its speech.
    """
    implementor = server.login(IMPLEMENTOR.name)
    pupil = server.login(PUPIL.name)
    give_sessions(implementor, PUPIL.name, STARTING_SESSIONS)

    reply = practice_once(pupil, "slashing")
    assert PRACTICED_MARKER not in reply, reply
    assert f"{TRAINER_LINE_START} {GO_AWAY_SPEECH}" not in reply, reply
    assert TRAINER_REJECT_MARKER in reply, reply


@pytest.mark.practice_abilities
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, PUPIL)
@pytest.mark.world_resets(MobReset(TRAVELLER_VNUM, world.ROOM_ARENA_CENTRE))
@pytest.mark.xfail(
    strict=True,
    raises=AssertionError,
    reason="a practice session should keep the strength worn gear adds to base strength",
)
def test_a_practice_session_keeps_the_strength_worn_gear_adds(server) -> None:
    """Base strength with a +2 circlet worn should stay constant + 2 after a practice session.

    The bug: handle_pracs (spec_pro.cpp:333) calls recalc_abilities, which sets every base
    ability back to the constant (profs.cpp:731-736), without affect_total (handler.cpp:599),
    which removes and re-applies gear and affects around it; so the session drops the circlet's
    +2 from base strength until the next affect_total. Practice draws no random number.
    """
    implementor = server.login(IMPLEMENTOR.name)
    pupil = server.login(PUPIL.name)
    implementor.command(f"goto {world.ROOM_ARENA_CENTRE}")
    reply = implementor.command(f"load obj {CIRCLET_VNUM}")
    assert reply.contains(CIRCLET_CREATED_MARKER), reply.text
    reply = implementor.command(f"give circlet {PUPIL.name.lower()}")
    assert reply.contains("You give"), reply.text
    reply = pupil.command("wear circlet")
    assert reply.contains("You wear an iron circlet on your head."), reply.text
    worn = stat_value(STRENGTH_PATTERN, stat_player(implementor, PUPIL.name))
    assert worn[1] == worn[2] + CIRCLET_STRENGTH_BONUS, worn
    give_sessions(implementor, PUPIL.name, STARTING_SESSIONS)

    assert PRACTICED_MARKER in practice_once(pupil, "blink")
    practised = stat_value(STRENGTH_PATTERN, stat_player(implementor, PUPIL.name))
    assert practised[1] == practised[2] + CIRCLET_STRENGTH_BONUS, practised
