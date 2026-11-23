"""Known practice bugs, each pinned as a strict xfail of the behaviour the game should have.

The session counts pracreset, advance and the angel's reroll leave, a bare pracreset, the spoken
language a recount resets, a guildmaster's refusal text and gear bonuses a session drops.
"""

from __future__ import annotations

import dataclasses
import re

import pytest

from rots_harness import world
from rots_harness.characters import IMPLEMENTOR, Character, StoredValues
from rots_harness.immortal_commands import GIVEN_START, goto, load_mob, load_object, wizset
from rots_harness.replies import reply_to
from rots_harness.session import GameSession
from rots_harness.stat_reading import (
    LEARNING_PATTERN,
    LEVEL_PATTERN,
    PRACTICE_SESSIONS_PATTERN,
    STRENGTH_PATTERN,
    read_stat,
)
from rots_harness.text_lines import first_number
from rots_harness.world_resets import MobReset
from test_practice import GO_AWAY_SPEECH, PRACTICED_REPLY, PUPIL, STARTING_SESSIONS, TRAVELLER, give_sessions
from test_practice_sessions import (
    LEARNING,
    PLAIN_SESSIONS_PATTERN,
    RESET_DONE,
    RESET_REPORT_REPLY,
    stat_value,
    tell_angel,
)
from test_practice_sessions import PUPIL as RESET_PUPIL
from test_practice_sessions import PUPIL_KEYWORD as RESET_PUPIL_KEYWORD

pytestmark = [pytest.mark.scenario, pytest.mark.practice]

# The keyword the implementor names the practice pupil by.
PUPIL_KEYWORD = PUPIL.name.lower()
# A level-6 human mage: the only level the angel rerolls.
REROLL_PUPIL = Character(
    name="Testroller",
    race="human",
    sex="male",
    level=6,
    load_room=world.ROOM_ARENA_CENTRE,
    class_name="mage",
)

# The scarred trainer (mob/guildmasters.mob), program 9, the URUK WARRIOR row, whose reject
# message is "$n snarls 'Get out!'"; it teaches only uruk-hai (will_teach 2048).
TRAINER_VNUM = 4608
# The trainer's short description as act() starts a line with it.
TRAINER_LINE_START = "A scarred trainer"
# The iron circlet (obj/11.obj): head armour whose one affect is APPLY_STR +2.
CIRCLET_VNUM = 1171
# CIRCLET_VNUM's short description.
CIRCLET_SHORT_DESCRIPTION = "an iron circlet"
# What wearing the circlet answers (wear_message, act_obj2.cpp).
CIRCLET_WORN_REPLY = "You wear an iron circlet on your head."
# The strength the circlet's affect adds.
CIRCLET_STRENGTH_BONUS = 2

# Learning that is not a multiple of LEA_PRAC_FACTOR (5), so a level's share has a fraction.
FRACTIONAL_LEARNING = 37
# The reset pupil stored with FRACTIONAL_LEARNING.
FRACTIONAL_PUPIL = dataclasses.replace(RESET_PUPIL, stored=StoredValues(lea=FRACTIONAL_LEARNING))

# The immortal command's refusal without a name (do_pracreset).
WHOM_REPLY = "Whom do you want to reset?"
# The reroll count after each angel reroll (SPECIAL(resetter)).
REROLLS_LEFT_PATTERN = re.compile(r"You have (\d+) reroll attempts left\.")
# The angel's refusal once the rerolls are used up.
NO_REROLLS_LEFT = "Sorry, you have exceeded the amount of times you can reroll statistics."
# The pupil's own learning after a reroll, as current/base (do_stat, act_info.cpp).
OWN_LEARNING_PATTERN = re.compile(r"Lea:\s*(\d+)/\s*(\d+)\.")

# What `set language <name>` replies (do_language, act_othe.cpp); format with the language.
CHOSEN_LANGUAGE_FORMAT = "You will now use {}."
# What a bare `set language` replies.
SPOKEN_LANGUAGE_PATTERN = re.compile(r"You are using ([a-z ]+)\.")
# The scarred trainer's own reject message, the URUK WARRIOR row's reject_message (SPECIAL(guild)).
TRAINER_REJECTION = f"{TRAINER_LINE_START} snarls 'Get out!'"


def practice_list_sessions(pupil: GameSession) -> int:
    """Return the sessions the plain practice list reports, read away from any guildmaster."""
    reply = pupil.command("practice")
    return first_number(PLAIN_SESSIONS_PATTERN, reply.text)


def bring_mob(implementor: GameSession, vnum: int, short_description: str) -> None:
    """Bring the implementor to the arena centre and load the mob there, checking load names it."""
    goto(implementor, world.ROOM_ARENA_CENTRE)
    load_mob(implementor, vnum, short_description)


def spoken_language(pupil: GameSession) -> str:
    """Return the language a bare `set language` says the character is using."""
    reply = pupil.command("set language")
    spoken = SPOKEN_LANGUAGE_PATTERN.search(reply.text)
    assert spoken is not None, reply.text
    return spoken.group(1)


def practice_once(pupil: GameSession, skill: str) -> str:
    """Send `practice <skill>` at the traveller and return everything the pupil read."""
    outcomes = [PRACTICED_REPLY, GO_AWAY_SPEECH, TRAINER_REJECTION]
    return reply_to(pupil, f"practice {skill}", outcomes)


@pytest.mark.practice_sessions
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, RESET_PUPIL)
@pytest.mark.xfail(
    strict=True,
    raises=AssertionError,
    reason="pracreset should leave the session count the next login computes, from base learning",
)
def test_pracreset_leaves_the_session_count_the_next_login_computes(server) -> None:
    """Bug: pracreset counts the sessions from the current learning, not the base.

    After a pracreset, the sessions should be what the next login's recount gives. do_pracreset
    (spec_pro.cpp:545) counts from the current learning, GET_LEA, while the login recount,
    get_max_practice_count (char_utils.cpp:1433), reads the base. The implementor raises the pupil's
    constant learning to 40 without a restore: the base follows, the current value stays at the
    rolled one, below the base, as a death or a drained learning leaves it. Nothing on the way draws
    a random number.
    """
    implementor = server.login(IMPLEMENTOR.name)
    pupil = server.login(RESET_PUPIL.name)
    wizset(implementor, f"player {RESET_PUPIL_KEYWORD}", "lea", LEARNING, shown_name=RESET_PUPIL.name)
    before = read_stat(implementor, f"player {RESET_PUPIL_KEYWORD}")
    current_learning, base_learning, constant_learning = stat_value(LEARNING_PATTERN, before)
    assert (base_learning, constant_learning) == (LEARNING, LEARNING), before
    assert current_learning < LEARNING, before
    assert stat_value(LEVEL_PATTERN, before) == (30,), before

    reply = implementor.command(f"pracreset {RESET_PUPIL_KEYWORD}")
    assert reply.contains(RESET_REPORT_REPLY), reply.text
    pupil.expect([RESET_DONE])
    # get_max_practice_count - get_spent_practice_count, as the next login computes it:
    #   level * PRACS_PER_LEVEL + level * abilities.lea / LEA_PRAC_FACTOR + 10 - spent
    #   = 30 * 3 + 30 * 40 / 5 + 10 - 0        (Lev: [30]; Lea base 40; nothing spent since)
    #   = 90 + 240 + 10 = 340
    # pracreset today gives 30 * 3 + 30 * current / 5 + 10, from the rolled current learning.
    after_reset = practice_list_sessions(pupil)
    assert after_reset == 340, after_reset

    pupil = server.relog(pupil)
    after_relog = practice_list_sessions(pupil)
    assert after_relog == 340, after_relog


@pytest.mark.practice_sessions
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR)
@pytest.mark.xfail(
    strict=True,
    raises=AssertionError,
    reason="a bare pracreset should ask whom to reset and reset nobody",
)
def test_the_pracreset_command_asks_whom_without_a_name(server) -> None:
    """Bug: a bare `pracreset` resets the immortal's own skills.

    It should ask whom to reset and reset nobody. The command's target mask, FULL_TARGET
    (interpre.cpp:2012, COMMANDO 119), includes TAR_SELF, so target_parser fills the first target
    with the immortal before do_pracreset's empty-argument check (spec_pro.cpp:525-526) runs, and
    the immortal's own skills are reset.
    """
    implementor = server.login(IMPLEMENTOR.name)

    reply = implementor.command("pracreset")
    assert not reply.contains(RESET_REPORT_REPLY), reply.text
    assert reply.contains(WHOM_REPLY), reply.text


@pytest.mark.practice_sessions
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, FRACTIONAL_PUPIL)
@pytest.mark.xfail(
    strict=True,
    raises=AssertionError,
    reason="advancing down should leave the session count the login recount gives at the new level",
)
def test_advancing_down_leaves_the_count_the_login_recount_gives(server) -> None:
    """Bug: `advance` down three levels at learning 37 leaves 288, not the recount's 287.

    It should leave what the login recount gives at the new level. do_advance (act_wiz.cpp:1620)
    subtracts adv * PRACS_PER_LEVEL + (adv * base learning) / LEA_PRAC_FACTOR, whose integer
    division drops a different fraction from the recount's level * learning / LEA_PRAC_FACTOR
    (get_max_practice_count, char_utils.cpp:1433), so the count is off until the next login. The
    exp-loss level drop (limits.cpp:469) subtracts per level the same way. Nothing on the way draws
    a random number.
    """
    implementor = server.login(IMPLEMENTOR.name)
    server.login(RESET_PUPIL.name)
    # Recount at login: 30 * 3 + 30 * 37 / 5 + 10 - 3 = 90 + 222 + 10 - 3 = 319
    # (Lev: [30]; Lea base 37; 3 blink sessions spent).
    at_login = read_stat(implementor, f"player {RESET_PUPIL_KEYWORD}")
    assert stat_value(PRACTICE_SESSIONS_PATTERN, at_login) == (319,), at_login

    reply = implementor.command(f"advance {RESET_PUPIL_KEYWORD} 27")
    assert reply.contains("Ok."), reply.text
    lowered = read_stat(implementor, f"player {RESET_PUPIL_KEYWORD}")
    assert stat_value(LEVEL_PATTERN, lowered) == (27,), lowered
    assert stat_value(LEARNING_PATTERN, lowered)[1] == FRACTIONAL_LEARNING, lowered
    # The recount at level 27: 27 * 3 + 27 * 37 / 5 + 10 - 3 = 81 + 199 + 10 - 3 = 287.
    # do_advance today, adv = -3: 319 + (-3 * 3) + (-3 * 37) / 5 = 319 - 9 - 22 = 288.
    assert stat_value(PRACTICE_SESSIONS_PATTERN, lowered) == (287,), lowered


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
    """Bug: a relog or a practice session puts a chosen spoken language back to human.

    `set language orcish` should last. recalc_skills (spec_pro.cpp:198) sets player.language from
    the race every time it runs. store_to_char loads the saved language (db.cpp:2835) and then calls
    recalc_skills (db.cpp:2887) at login, and handle_pracs calls it on every session
    (spec_pro.cpp:332), so the choice do_language (act_othe.cpp:1454) accepted is lost. Nothing
    draws a random number.
    """
    implementor = server.login(IMPLEMENTOR.name)
    pupil = server.login(PUPIL.name)
    reply = pupil.command("set language orcish")
    assert reply.contains(CHOSEN_LANGUAGE_FORMAT.format("orcish language")), reply.text
    chosen = spoken_language(pupil)
    assert chosen == "orcish language", chosen

    if recount == "relog":
        pupil = server.relog(pupil)
    else:
        give_sessions(implementor, PUPIL.name, STARTING_SESSIONS)
        bring_mob(implementor, TRAVELLER.vnum, TRAVELLER.short_description)
        practised = practice_once(pupil, "blink")
        assert PRACTICED_REPLY in practised, practised

    after_recount = spoken_language(pupil)
    assert after_recount == "orcish language", after_recount


@pytest.mark.practice_sessions
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, REROLL_PUPIL)
@pytest.mark.world_resets(MobReset(world.MOB_SHINING_ANGEL, world.ROOM_ARENA_CENTRE))
@pytest.mark.xfail(
    strict=True,
    raises=AssertionError,
    reason="the angel's reroll should recount the practice sessions from the new learning",
)
def test_the_angels_reroll_recounts_the_sessions_from_the_new_learning(server) -> None:
    """Bug: the angel's reroll changes learning but leaves the session count of the old learning.

    After `tell angel reroll` changes learning, the sessions should follow it. SPECIAL(resetter)
    (spec_pro.cpp:3120) calls roll_abilities alone, while the immortal's reroll (act_wiz.cpp:2308)
    and a level gained (profs.cpp:435) both end with update_available_practice_sessions, so the
    count keeps the old learning until the next login. The pupil rerolls until its base learning
    changes: each reroll's abilities come from the seeded stream, so the loop ends on the reroll
    that changes it, bounded by the reroll attempts the angel counts down. Each reroll saves the
    character.
    """
    implementor = server.login(IMPLEMENTOR.name)
    pupil = server.login(REROLL_PUPIL.name)
    before = read_stat(implementor, f"player {REROLL_PUPIL.name.lower()}")
    assert stat_value(LEVEL_PATTERN, before) == (6,), before
    original_learning = stat_value(LEARNING_PATTERN, before)[1]
    # The login recount: 6 * 3 + 6 * learning / 5 + 10 - 0 (Lev: [6]; nothing spent).
    at_login = practice_list_sessions(pupil)
    assert at_login == 6 * 3 + 6 * original_learning // 5 + 10, at_login

    rerolled_learning = original_learning
    while rerolled_learning == original_learning:
        reply = tell_angel(pupil, "reroll")
        assert NO_REROLLS_LEFT not in reply, f"the rerolls ran out with learning unchanged:\n{reply}"
        rerolls_left = REROLLS_LEFT_PATTERN.search(reply)
        own_learning = OWN_LEARNING_PATTERN.search(reply)
        assert rerolls_left is not None and own_learning is not None, reply
        rerolled_learning = int(own_learning.group(2))
        if rerolled_learning == original_learning:
            assert int(rerolls_left.group(1)) > 0, f"the rerolls ran out with learning unchanged:\n{reply}"

    rerolled_stat = read_stat(implementor, f"player {REROLL_PUPIL.name.lower()}")
    assert stat_value(LEARNING_PATTERN, rerolled_stat)[1] == rerolled_learning, rerolled_stat

    # update_available_practice_sessions, as the immortal's reroll applies it:
    #   6 * PRACS_PER_LEVEL + 6 * rerolled base learning / LEA_PRAC_FACTOR + 10 - 0 spent
    # 6 * learning / 5 is strictly increasing in learning, so any change moves the count.
    after_reroll = practice_list_sessions(pupil)
    assert after_reroll == 6 * 3 + 6 * rerolled_learning // 5 + 10, after_reroll


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
    """Bug: the scarred trainer says the hard-coded go-away line, not its row's 'Get out!'.

    It should snarl its own reject message. skill_teach_data's reject_message (spells.h:381) is
    never read; SPECIAL(guild) (spec_pro.cpp:365-367) always says "Go away, I won't teach you
    anything!". The trainer is a copy at 4608 with program 9 (URUK WARRIOR, reject message "$n
    snarls 'Get out!'") that teaches only uruk-hai and speaks human language, so a human pupil
    understands its speech.
    """
    implementor = server.login(IMPLEMENTOR.name)
    pupil = server.login(PUPIL.name)
    give_sessions(implementor, PUPIL.name, STARTING_SESSIONS)

    reply = practice_once(pupil, "slashing")
    assert PRACTICED_REPLY not in reply, reply
    assert f"{TRAINER_LINE_START} {GO_AWAY_SPEECH}" not in reply, reply
    assert TRAINER_REJECTION in reply, reply


@pytest.mark.practice_abilities
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, PUPIL)
@pytest.mark.world_resets(MobReset(world.MOB_MYSTERIOUS_TRAVELLER, world.ROOM_ARENA_CENTRE))
@pytest.mark.xfail(
    strict=True,
    raises=AssertionError,
    reason="a practice session should keep the strength worn gear adds to base strength",
)
def test_a_practice_session_keeps_the_strength_worn_gear_adds(server) -> None:
    """Bug: a practice session drops the +2 a worn circlet adds to base strength.

    Base strength with the circlet worn should stay constant + 2 after the session. handle_pracs
    (spec_pro.cpp:333) calls recalc_abilities, which sets every base ability back to the constant
    (profs.cpp:731-736), without affect_total (handler.cpp:599), which removes and re-applies gear
    and affects around it; so the session drops the circlet's +2 from base strength until the next
    affect_total. Practice draws no random number.
    """
    implementor = server.login(IMPLEMENTOR.name)
    pupil = server.login(PUPIL.name)
    goto(implementor, world.ROOM_ARENA_CENTRE)
    load_object(implementor, CIRCLET_VNUM, CIRCLET_SHORT_DESCRIPTION)
    reply = implementor.command(f"give circlet {PUPIL_KEYWORD}")
    assert reply.contains(GIVEN_START), reply.text
    reply = pupil.command("wear circlet")
    assert reply.contains(CIRCLET_WORN_REPLY), reply.text
    worn_stat = read_stat(implementor, f"player {PUPIL_KEYWORD}")
    worn = stat_value(STRENGTH_PATTERN, worn_stat)
    assert worn[1] == worn[2] + CIRCLET_STRENGTH_BONUS, worn
    give_sessions(implementor, PUPIL.name, STARTING_SESSIONS)

    practised_reply = practice_once(pupil, "blink")
    assert PRACTICED_REPLY in practised_reply, practised_reply
    practised_stat = read_stat(implementor, f"player {PUPIL_KEYWORD}")
    practised = stat_value(STRENGTH_PATTERN, practised_stat)
    assert practised[1] == practised[2] + CIRCLET_STRENGTH_BONUS, practised
