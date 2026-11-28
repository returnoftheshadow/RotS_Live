"""Practice at a guildmaster: what a practice command spends, refuses and lists.

Counted and `all` practice, its refusals, the sessions-spent column, two guildmasters with
different limits, the sessions a skill takes by ranger coefficient, the dark-side guildmasters and
race-adjusted coefficients, weapon mastery, a guildmaster that is only posing, a negative session
count, and the plain list's casting costs.
"""

from __future__ import annotations

import dataclasses
import re
from dataclasses import dataclass

import pytest

from rots_harness import world
from rots_harness.characters import IMPLEMENTOR, Character, ProfessionPoints, StoredValues
from rots_harness.game_text import PURGED_REPLY, SIT_DOWN_REPLY, STAND_UP_REPLY
from rots_harness.immortal_commands import goto, load_mob, wizset
from rots_harness.replies import reply_to, settled_expect
from rots_harness.session import GameSession
from rots_harness.stat_reading import LEARNING_PATTERN, LEVEL_PATTERN, PRACTICE_SESSIONS_PATTERN, read_stat
from rots_harness.text_lines import capitalised, first_number
from rots_harness.world_resets import MobReset

pytestmark = [pytest.mark.scenario, pytest.mark.practice]


@dataclass(frozen=True)
class Guildmaster:
    """A guildmaster copy (mob/15.mob or mob/guildmasters.mob) and the lines a pupil reads."""

    # The copy's vnum: one spec_ass.cpp binds the guild special to.
    vnum: int
    # The keyword `purge` names it by.
    keyword: str
    # Its short description, as `load mob` and act() print it mid-line.
    short_description: str
    # Its list's first line (guildmasters[], consts.cpp); format with the pupil's name.
    list_header_format: str

    @property
    def line_start(self) -> str:
        """Return the short description as act() starts a line with it, capitalised."""
        return capitalised(self.short_description)


# Program 7, the TRAVELLER (MAGE) row of guildmasters[] (consts.cpp): teaches blink up to 100 and
# cure self up to 80, fireball and cone of cold, but not magic missile. It teaches the races in
# its will_teach mask, 63: god, human, dwarf, wood elf, hobbit and high elf, not the beorning.
TRAVELLER = Guildmaster(
    world.MOB_MYSTERIOUS_TRAVELLER, "traveller", "a mysterious traveller", "Hello, {}! I can teach you these spells:"
)
# Scorther, program 16, the LAKETOWN MAGE row: teaches blink and cure self up to 60, magic
# missile and identify, but not fireball. Same will_teach mask.
SCORTHER = Guildmaster(
    10003, "scorther", "Scorther", "Hello, {}! I can only teach you these spells:"
)
# Aroden, program 28, the LAKETOWN RANGER (wanderer) row: teaches awareness, travelling and
# stalking up to 100. Same will_teach mask.
ARODEN = Guildmaster(10054, "aroden", "Aroden", "Hello, {}! I can only teach you these skills:")
# The young uruk mage, program 12, the URUK MAGE row: teaches magic missile up to 100 to uruk-hai,
# the only race in its will_teach mask, 2048.
URUK_MAGE_TEACHER = Guildmaster(4601, "young", "the young uruk mage", "You can try to learn these spells:")
# The glass-eyed shaman, program 47: teaches magic missile up to 100 and chill ray up to 20 to
# orcs (will_teach 8192), with its own practice and limit lines.
SHAMAN = Guildmaster(
    32200,
    "shaman",
    "a glass-eyed shaman",
    "As the shaman turns his head, his glass eye wobbles furiously, 'What do you want?'",
)
# The Magus, program 30, the MAGUS row: teaches blink up to 100 to uruk-lhuth (will_teach 32768).
MAGUS = Guildmaster(13600, "magus", "Magus", "You can try to learn these spells:")
# The uruk guildmaster, program 58: teaches the weapon skills and weapon mastery up to 100 to
# uruk-hai and orcs (will_teach 10240).
URUK_TRAINER = Guildmaster(2043, "master", "an uruk guildmaster", "You can train in these skills:")
# The wise mystic copy, program 61: the last row of guildmasters[] (guildmaster_number, consts.cpp).
LAST_ROW_TEACHER = Guildmaster(1509, "mystic", "a wise mystic", "You can train in these skills:")
# A copy with program 0, which names no row of guildmasters[]: it has no list.
NO_ROW_TEACHER = Guildmaster(3888, "prisoner", "a starved prisoner", "")
# A copy with program 62, past the last row: it has no list either.
PAST_LAST_ROW_TEACHER = Guildmaster(8500, "enchanter", "an elven enchanter", "")

# A level-30 human mage in the arena centre who has practiced nothing.
PUPIL = Character(
    name="Testpupil",
    race="human",
    sex="female",
    level=30,
    load_room=world.ROOM_ARENA_CENTRE,
    class_name="mage",
)
# The sessions the listed pupil has already spent on blink.
BLINK_SESSIONS_SPENT = 12
# The same kind of pupil with those blink sessions spent, for the sessions-spent column.
LISTED_PUPIL = Character(
    name="Testlisted",
    race="human",
    sex="female",
    level=30,
    load_room=world.ROOM_ARENA_CENTRE,
    class_name="mage",
    skills=(("blink", BLINK_SESSIONS_SPENT),),
)
# The same kind of pupil with five blink sessions spent, which put blink's knowledge at 101
# (recalc_skills, spec_pro.cpp, for a mage-coefficient 1000 pupil: t = 1000 - 5 * 200 = 0, and
# (10000 - 0) / 99 = 101).
PERFECT_PUPIL = Character(
    name="Testperfect",
    race="human",
    sex="female",
    level=30,
    load_room=world.ROOM_ARENA_CENTRE,
    class_name="mage",
    skills=(("blink", 5),),
)
# A level-8 human mage: mage level 8, below fireball's level of 21 (skills[], consts.cpp).
NOVICE_PUPIL = Character(
    name="Testnovice",
    race="human",
    sex="female",
    level=8,
    load_room=world.ROOM_ARENA_CENTRE,
    class_name="mage",
)
# A level-30 beorning mage, a race outside the traveller's will_teach mask. Three sessions of
# human language master it (recalc_skills: a general skill at coefficient 1000 and difficulty 5
# passes 100 on the third), so the traveller's speech, in human language, reaches the beorning
# unscrambled.
BEORNING_PUPIL = Character(
    name="Testbear",
    race="beorning",
    sex="male",
    level=30,
    load_room=world.ROOM_ARENA_CENTRE,
    class_name="mage",
    skills=(("human_language", 3),),
)
# Level-30 humans whose ranger coefficients differ. This one, of the ranger class, has 100 ranger
# points (coefficient square_root[100] = 1000).
RANGER_PUPIL = Character(
    name="Testranger",
    race="human",
    sex="male",
    level=30,
    load_room=world.ROOM_ARENA_CENTRE,
    class_name="ranger",
)
# The wizard class has 9 ranger points (coefficient 300).
WIZARD_PUPIL = Character(
    name="Testwizard",
    race="human",
    sex="male",
    level=30,
    load_room=world.ROOM_ARENA_CENTRE,
    class_name="wizard",
)
# This custom split has 7 ranger points (coefficient 264).
LOW_RANGER_PUPIL = Character(
    name="Testlowran",
    race="human",
    sex="male",
    level=30,
    load_room=world.ROOM_ARENA_CENTRE,
    points=ProfessionPoints(mage=100, mystic=43, ranger=7, warrior=0),
)
# The sessions that end practice in one skill: practice_refusal refuses once more than 50 are spent.
SESSION_CAP = 51
# The low-ranger build with the cap already spent on travelling, which leaves it at 97%.
CAPPED_PUPIL = Character(
    name="Testcapped",
    race="human",
    sex="male",
    level=30,
    load_room=world.ROOM_ARENA_CENTRE,
    points=ProfessionPoints(mage=100, mystic=43, ranger=7, warrior=0),
    skills=(("travelling", SESSION_CAP),),
)

# Level-30 mages of the dark races, for the race-adjusted mage coefficient (GET_PROF_COOF,
# utils.h) and the dark-side guildmasters: this one an uruk-hai.
URUK_PUPIL = Character(
    name="Testuruk",
    race="uruk-hai",
    sex="male",
    level=30,
    load_room=world.ROOM_ARENA_CENTRE,
    class_name="mage",
)
# The orc mage.
ORC_PUPIL = Character(
    name="Testorc",
    race="orc",
    sex="male",
    level=30,
    load_room=world.ROOM_ARENA_CENTRE,
    class_name="mage",
)
# The uruk-lhuth mage.
LHUTH_PUPIL = Character(
    name="Testlhuth",
    race="uruk-lhuth",
    sex="male",
    level=30,
    load_room=world.ROOM_ARENA_CENTRE,
    class_name="mage",
)
# The weapon mastery sessions the uruk warrior has spent.
MASTERY_SESSIONS_SPENT = 8
# A level-30 uruk-hai warrior with those weapon mastery sessions spent and nothing else.
MASTERY_PUPIL = Character(
    name="Testmastery",
    race="uruk-hai",
    sex="male",
    level=30,
    load_room=world.ROOM_ARENA_CENTRE,
    class_name="warrior",
    skills=(("weapon_mastery", MASTERY_SESSIONS_SPENT),),
)
# A level-30 human mage who has spent 153 sessions, three skills at the session cap, with a
# learning of 0, so its login recount is below what it has spent.
OVERSPENT_PUPIL = Character(
    name="Testspent",
    race="human",
    sex="female",
    level=30,
    load_room=world.ROOM_ARENA_CENTRE,
    class_name="mage",
    skills=(("magic_missile", 51), ("blink", 51), ("cure_self", 51)),
    stored=StoredValues(lea=0),
)

# The last of the two lines a pupil reads on a pracreset (do_pracreset, spec_pro.cpp).
PRACRESET_DONE = "Your practices have been reset"

# The practice sessions the implementor gives a pupil before practicing.
STARTING_SESSIONS = 20
# Enough sessions that a practice command never runs out of them.
PLENTY_OF_SESSIONS = 200
# The specialization stalking needs: PLRSPEC_STLH (structs.h), its skills[] skill_spec.
STEALTH_SPECIALIZATION = 6

# The guildmaster list's session count (SPECIAL(guild), spec_pro.cpp).
GUILD_SESSIONS_PATTERN = re.compile(r"You have (-?\d+) practice sessions left\.")
# A guildmaster list row: name, knowledge, sessions spent, and what the guildmaster teaches it
# to, as how_good's word. Rows end in "\n\r", so each starts after a carriage return.
GUILD_ROW_PATTERN_FORMAT = r"^\r?{}\s+(\d+)%\s+(\d+)\s+Taught to:\s+\(([A-Za-z ]+)\)"
# The part of the plain practice list's first line, away from a guildmaster (do_practice,
# spec_pro.cpp), that names the sessions.
PLAIN_SESSIONS_PART = "practice sessions left"
# That first line's session count; the line has no full stop.
PLAIN_SESSIONS_PATTERN = re.compile(r"You have (-?\d+) practice sessions left\r?\n")
# A plain practice list row: the name, then how well it is known in parentheses (how_good).
PLAIN_ROW_PATTERN_FORMAT = r"^\r?{}\s+\(([A-Za-z ]+)\)"
# A whole plain-list row of a mage spell: the name, how well it is known, then the casting time
# and stamina, and nothing more.
PLAIN_SPELL_ROW_PATTERN_FORMAT = r"^\r?{}\s+\(([A-Za-z ]+)\)\s+\(\s*\d+ time,\s+\d+ stamina\)$"
# The professions' coefficients in `stat player` (do_stat_character, act_wiz.cpp).
COEFFICIENTS_PATTERN = re.compile(r"Class coofs : Mag:(\d+) Cle:(\d+) Ran:(\d+) War:(\d+)")
# The professions' levels in `stat player`.
CLASS_LEVELS_PATTERN = re.compile(r"Class levels: Mag:(\d+) Cle:(\d+) Ran:(\d+) War:(\d+)")

# What the pupil reads on the first session of a practice command (guildmasters[]).
PRACTICED_REPLY = "You practice it for a while."
# What the others in the room read on that first session at the traveller; format with the
# pupil's name.
TEACHES_FORMAT = f"{TRAVELLER.line_start} teaches {{}} for a while."
# What the pupil reads after more than one session (SPECIAL(guild)): the count and the skill.
SESSIONS_SPENT_PATTERN = re.compile(r"You spend (\d+) sessions practicing ([a-z ]+)\.")
# What the pupil reads when the guildmaster teaches the skill no better (guildmasters[]).
LIMIT_REPLY = "The guildmaster bows and tells you 'Sorry, I can not teach you better.'"
# What the pupil reads when the skill is already at 100 (guildmasters[]).
PERFECT_REPLY = "You're already perfect in this."
# What the pupil reads after 51 sessions in one skill (practice_refusal, spec_pro.cpp).
TOO_LONG_REPLY = "You studied this for too long. Enough for you."
# What the pupil reads when its sessions run out (practice_refusal).
NO_SESSIONS_REPLY = "You have no practice sessions left now."
# SPECIAL(guild)'s refusal of a zero count, given before any session is spent.
HOW_MANY_REPLY = "Practice how many times?"
# Its refusal of a count with no skill.
WHICH_SKILL_REPLY = "Practice which skill?"
# Its refusal of a skill no table has.
NO_SUCH_SKILL_REPLY = "There is no such skill as you seek."
# The part, after the guildmaster's name, of its refusal of a skill it does not teach.
UNKNOWN_TO_GUILDMASTER_PART = "tells you 'I know nothing about it.'"
# Its refusal of a specialization skill to a pupil without the specialization.
NOT_DEDICATED_REPLY = "You are not dedicated enough for this."
# Its refusal of a skill above the pupil's level in the profession.
NOT_EXPERIENCED_REPLY = "You are not experienced enough for this."
# What the guildmaster says to a race outside its will_teach mask (SPECIAL(guild), do_say).
GO_AWAY_SPEECH = "says 'Go away, I won't teach you anything!'"
# What it says to a shadow.
SHADOW_SPEECH = "says 'Ugh!  I'm not teaching YOU!'"
# What it says to a race outside its rp_flag mask.
RACE_RULE_SPEECH = "says 'Sorry, I can't teach you.'"
# What a guildmaster whose program names no row of guildmasters[] tells the pupil.
POSING_REPLY = "Alas, I am just posing for guildmaster! Come back later."
# The glass-eyed shaman's own first-session line (its row of guildmasters[]).
SHAMAN_PRACTICED_REPLY = "A glass-eyed shaman explains it to you."
# The glass-eyed shaman's own limit line.
SHAMAN_LIMIT_REPLY = "A glass-eyed shaman tells you 'There isn't more for you to know.'"

# Every line a practice command at a guildmaster can end with: the outcomes of SPECIAL(guild),
# and the plain list's first line, for a command no guildmaster takes.
PRACTICE_OUTCOMES = [
    PRACTICED_REPLY,
    LIMIT_REPLY,
    PERFECT_REPLY,
    TOO_LONG_REPLY,
    NO_SESSIONS_REPLY,
    HOW_MANY_REPLY,
    WHICH_SKILL_REPLY,
    NO_SUCH_SKILL_REPLY,
    UNKNOWN_TO_GUILDMASTER_PART,
    NOT_DEDICATED_REPLY,
    NOT_EXPERIENCED_REPLY,
    GO_AWAY_SPEECH,
    SHADOW_SPEECH,
    RACE_RULE_SPEECH,
    POSING_REPLY,
    SHAMAN_PRACTICED_REPLY,
    SHAMAN_LIMIT_REPLY,
    PLAIN_SESSIONS_PART,
]


def give_sessions(implementor: GameSession, name: str, sessions: int) -> None:
    """Set the character's practice sessions to exactly sessions."""
    wizset(implementor, f"player {name.lower()}", "practices", sessions, shown_name=name)


def guildmaster_reset(guildmaster: Guildmaster = TRAVELLER) -> MobReset:
    """Return the world reset that places the guildmaster in the arena centre at boot."""
    return MobReset(guildmaster.vnum, world.ROOM_ARENA_CENTRE)


def bring_guildmaster(implementor: GameSession, guildmaster: Guildmaster = TRAVELLER) -> None:
    """Bring the implementor to the arena centre and load the guildmaster there."""
    goto(implementor, world.ROOM_ARENA_CENTRE)
    load_mob(implementor, guildmaster.vnum, guildmaster.short_description)


def dismiss_guildmaster(implementor: GameSession, guildmaster: Guildmaster) -> None:
    """Purge the guildmaster from the implementor's room, so the next one loaded answers alone."""
    reply = implementor.command(f"purge {guildmaster.keyword}")
    assert reply.contains(PURGED_REPLY), reply.text


def guild_list(pupil: GameSession, name: str, guildmaster: Guildmaster = TRAVELLER) -> str:
    """Return the guildmaster's list as the pupil reads it after a bare `practice`."""
    reply = pupil.command("practice")
    assert reply.contains(guildmaster.list_header_format.format(name)), reply.text
    return reply.text


def guild_row(listed: str, skill: str) -> tuple[int, int, str]:
    """Return a skill's knowledge, sessions spent and taught-to word in a guildmaster's list."""
    escaped_skill = re.escape(skill)
    pattern = GUILD_ROW_PATTERN_FORMAT.format(escaped_skill)
    row = re.search(pattern, listed, re.MULTILINE)
    assert row is not None, f"no {skill} row in the guildmaster's list:\n{listed}"
    knowledge = int(row.group(1))
    sessions_spent = int(row.group(2))
    return knowledge, sessions_spent, row.group(3)


def listed_sessions(listed: str) -> int:
    """Return the practice sessions a guildmaster's list reports; asserts it reports them."""
    return first_number(GUILD_SESSIONS_PATTERN, listed)


def sessions_left(pupil: GameSession, name: str, guildmaster: Guildmaster = TRAVELLER) -> int:
    """Return the practice sessions the guildmaster's list reports for the pupil."""
    listed = guild_list(pupil, name, guildmaster)
    return listed_sessions(listed)


def practice(pupil: GameSession, arguments: str, command: str = "practice") -> str:
    """Send `<command> <arguments>` at a guildmaster and return everything the pupil read."""
    return reply_to(pupil, f"{command} {arguments}", PRACTICE_OUTCOMES)


def has_guild_row(listed: str, skill: str) -> bool:
    """Return whether a guildmaster's list has a row for the skill."""
    escaped_skill = re.escape(skill)
    pattern = GUILD_ROW_PATTERN_FORMAT.format(escaped_skill)
    return re.search(pattern, listed, re.MULTILINE) is not None


def ranger_coefficient_and_level(implementor: GameSession, name: str) -> tuple[int, int]:
    """Return the character's ranger coefficient and ranger level, from `stat player`."""
    text = read_stat(implementor, f"player {name.lower()}")
    coefficients = COEFFICIENTS_PATTERN.search(text)
    levels = CLASS_LEVELS_PATTERN.search(text)
    assert coefficients is not None and levels is not None, text
    coefficient = int(coefficients.group(3))
    level = int(levels.group(3))
    return coefficient, level


def mage_coefficient_and_level(implementor: GameSession, name: str) -> tuple[int, int]:
    """Return the character's mage coefficient and mage level, from `stat player`."""
    text = read_stat(implementor, f"player {name.lower()}")
    coefficients = COEFFICIENTS_PATTERN.search(text)
    levels = CLASS_LEVELS_PATTERN.search(text)
    assert coefficients is not None and levels is not None, text
    coefficient = int(coefficients.group(1))
    level = int(levels.group(1))
    return coefficient, level


@pytest.mark.practice_limits
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, PUPIL)
@pytest.mark.world_resets(guildmaster_reset())
def test_practice_all_stops_at_the_guildmasters_limit_and_spends_nothing_more(server) -> None:
    """Pins that `practice <skill> all` spends sessions only up to the guildmaster's limit.

    The traveller teaches cure self up to 80. With a mage coefficient of 1000, cure self's
    knowledge after N sessions is 36, 64, 84 (recalc_skills, spec_pro.cpp), so the third session
    passes 80 and is cut back to it. Practicing it `all` shows the practice lines once, the three
    sessions spent, then the limit line, and the session count drops by exactly three; asking
    again spends nothing and gives the limit line alone. Practice draws no random number.
    """
    implementor = server.login(IMPLEMENTOR.name)
    pupil = server.login(PUPIL.name)
    give_sessions(implementor, PUPIL.name, STARTING_SESSIONS)
    goto(implementor, world.ROOM_ARENA_CENTRE)
    left = sessions_left(pupil, PUPIL.name)
    assert left == STARTING_SESSIONS, left

    first_try = practice(pupil, "cure self all")
    assert first_try.count(PRACTICED_REPLY) == 1, first_try
    spent = SESSIONS_SPENT_PATTERN.search(first_try)
    assert spent is not None, first_try
    assert (int(spent.group(1)), spent.group(2)) == (3, "cure self"), first_try
    limit_at = first_try.index(LIMIT_REPLY)
    assert first_try.index(PRACTICED_REPLY) < spent.start() < limit_at, first_try
    teaching = TEACHES_FORMAT.format(PUPIL.name)
    teaching_seen = settled_expect(implementor, [teaching])
    assert teaching_seen.count(teaching) == 1, teaching_seen
    listed = guild_list(pupil, PUPIL.name)
    assert guild_row(listed, "cure self") == (80, 3, "good"), listed
    assert listed_sessions(listed) == STARTING_SESSIONS - 3, listed

    second_try = practice(pupil, "cure self all")
    assert LIMIT_REPLY in second_try, second_try
    assert PRACTICED_REPLY not in second_try, second_try
    assert SESSIONS_SPENT_PATTERN.search(second_try) is None, second_try
    left = sessions_left(pupil, PUPIL.name)
    assert left == STARTING_SESSIONS - 3, left


@pytest.mark.practice_limits
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, PUPIL)
@pytest.mark.world_resets(guildmaster_reset())
@pytest.mark.parametrize("arguments", ["blink 3", "3 blink"], ids=["skill-first", "count-first"])
@pytest.mark.parametrize("command", ["practice", "practise"])
def test_a_counted_practice_spends_exactly_that_many_sessions_in_either_word_order(
    server, arguments: str, command: str
) -> None:
    """Pins that `practice blink 3` and `practice 3 blink` each spend exactly three sessions.

    Each order starts from a pupil who has never practiced blink: three sessions leave it below
    the traveller's limit of 100, which this pupil passes on the fifth. The `practise` spelling,
    its own command (CMD_PRACTISE), behaves the same. Practice draws no random number.
    """
    implementor = server.login(IMPLEMENTOR.name)
    pupil = server.login(PUPIL.name)
    give_sessions(implementor, PUPIL.name, STARTING_SESSIONS)

    reply = practice(pupil, arguments, command)
    assert reply.count(PRACTICED_REPLY) == 1, reply
    spent = SESSIONS_SPENT_PATTERN.search(reply)
    assert spent is not None, reply
    assert (int(spent.group(1)), spent.group(2)) == (3, "blink"), reply
    assert LIMIT_REPLY not in reply, reply
    left = sessions_left(pupil, PUPIL.name)
    assert left == STARTING_SESSIONS - 3, left


@pytest.mark.practice_limits
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, PUPIL)
@pytest.mark.world_resets(guildmaster_reset())
def test_running_out_of_sessions_part_way_stops_with_the_no_sessions_line(server) -> None:
    """Pins that with two sessions, `practice blink 5` spends both, then says none are left.

    Practice draws no random number.
    """
    implementor = server.login(IMPLEMENTOR.name)
    pupil = server.login(PUPIL.name)
    give_sessions(implementor, PUPIL.name, 2)

    reply = practice(pupil, "blink 5")
    assert reply.count(PRACTICED_REPLY) == 1, reply
    spent = SESSIONS_SPENT_PATTERN.search(reply)
    assert spent is not None, reply
    assert (int(spent.group(1)), spent.group(2)) == (2, "blink"), reply
    assert spent.start() < reply.index(NO_SESSIONS_REPLY), reply
    left = sessions_left(pupil, PUPIL.name)
    assert left == 0, left


@pytest.mark.practice_limits
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, PUPIL)
@pytest.mark.world_resets(guildmaster_reset())
@pytest.mark.parametrize(
    ("arguments", "refusal"),
    [
        ("blink 0", HOW_MANY_REPLY),
        ("5", WHICH_SKILL_REPLY),
        ("zzz 3", NO_SUCH_SKILL_REPLY),
    ],
    ids=["zero-count", "no-skill", "unknown-skill"],
)
def test_a_practice_the_guildmaster_cannot_carry_out_is_refused_and_spends_nothing(
    server, arguments: str, refusal: str
) -> None:
    """Pins that a zero count, a count alone and an unknown skill each get their refusal.

    Nothing is practiced and no session is spent.
    """
    implementor = server.login(IMPLEMENTOR.name)
    pupil = server.login(PUPIL.name)
    give_sessions(implementor, PUPIL.name, STARTING_SESSIONS)

    reply = practice(pupil, arguments)
    assert refusal in reply, reply
    assert PRACTICED_REPLY not in reply, reply
    left = sessions_left(pupil, PUPIL.name)
    assert left == STARTING_SESSIONS, left


@pytest.mark.practice_lists
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, LISTED_PUPIL)
def test_the_guildmaster_list_shows_sessions_spent_and_the_plain_list_does_not(server) -> None:
    """Pins that the guildmaster's list has a sessions-spent column and plain `practice` has none.

    Away from a guildmaster, `practice` lists blink with how well it is known and no count; at
    the traveller, the blink row shows the 12 sessions the pupil has spent on it.
    """
    implementor = server.login(IMPLEMENTOR.name)
    pupil = server.login(LISTED_PUPIL.name)

    plain = pupil.command("practice")
    assert plain.contains(PLAIN_SESSIONS_PART), plain.text
    assert not plain.contains(TRAVELLER.list_header_format.format(LISTED_PUPIL.name)), plain.text
    blink_row_pattern = PLAIN_ROW_PATTERN_FORMAT.format("blink")
    assert re.search(blink_row_pattern, plain.text, re.MULTILINE), plain.text
    blink_spell_row_pattern = PLAIN_SPELL_ROW_PATTERN_FORMAT.format("blink")
    plain_row = re.search(blink_spell_row_pattern, plain.text, re.MULTILINE)
    assert plain_row is not None, plain.text

    bring_guildmaster(implementor)
    listed = guild_list(pupil, LISTED_PUPIL.name)
    assert guild_row(listed, "blink")[1] == BLINK_SESSIONS_SPENT, listed


@pytest.mark.practice_limits
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, PUPIL)
def test_a_skill_stopped_at_one_guildmasters_limit_is_finished_at_a_higher_one(server) -> None:
    """Pins that blink practised to Scorther's limit of 60 goes on to 100 at the traveller.

    With a mage coefficient of 1000, blink's knowledge after N sessions is 36, 64, 84, 96, 101
    (recalc_skills, spec_pro.cpp). At Scorther the second session passes 60, is cut back to it,
    and the limit line ends the command; the traveller's list then shows the same 60% with its
    own higher limit, and three more sessions reach 100 (the fifth passes it and is cut back, so
    the limit line ends this command too). Only one guildmaster is in the room at a time.
    Practice draws no random number.
    """
    implementor = server.login(IMPLEMENTOR.name)
    pupil = server.login(PUPIL.name)
    give_sessions(implementor, PUPIL.name, STARTING_SESSIONS)
    bring_guildmaster(implementor, SCORTHER)

    at_scorther = practice(pupil, "blink all")
    spent = SESSIONS_SPENT_PATTERN.search(at_scorther)
    assert spent is not None, at_scorther
    assert (int(spent.group(1)), spent.group(2)) == (2, "blink"), at_scorther
    assert LIMIT_REPLY in at_scorther, at_scorther
    assert spent.start() < at_scorther.index(LIMIT_REPLY), at_scorther
    scorther_list = guild_list(pupil, PUPIL.name, SCORTHER)
    assert guild_row(scorther_list, "blink") == (60, 2, "fair"), scorther_list

    dismiss_guildmaster(implementor, SCORTHER)
    bring_guildmaster(implementor, TRAVELLER)
    traveller_list = guild_list(pupil, PUPIL.name, TRAVELLER)
    assert guild_row(traveller_list, "blink") == (60, 2, "Mastered"), traveller_list

    at_traveller = practice(pupil, "blink all")
    spent = SESSIONS_SPENT_PATTERN.search(at_traveller)
    assert spent is not None, at_traveller
    assert (int(spent.group(1)), spent.group(2)) == (3, "blink"), at_traveller
    assert LIMIT_REPLY in at_traveller, at_traveller
    assert spent.start() < at_traveller.index(LIMIT_REPLY), at_traveller
    finished_list = guild_list(pupil, PUPIL.name, TRAVELLER)
    assert guild_row(finished_list, "blink") == (100, 5, "Mastered"), finished_list
    assert listed_sessions(finished_list) == STARTING_SESSIONS - 5, finished_list


@pytest.mark.practice_limits
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, PUPIL)
def test_a_lower_limit_guildmaster_refuses_a_skill_already_past_it_and_spends_nothing(
    server,
) -> None:
    """Pins that cure self practised to the traveller's 80 is refused at once by Scorther (60).

    Three sessions take cure self to 84, cut back to the traveller's 80 (recalc_skills gives the
    same 36, 64, 84 as blink at a mage coefficient of 1000). Scorther's limit of 60 is below it,
    so his only answer is the limit line and the session count stays put. Practice draws no
    random number.
    """
    implementor = server.login(IMPLEMENTOR.name)
    pupil = server.login(PUPIL.name)
    give_sessions(implementor, PUPIL.name, STARTING_SESSIONS)
    bring_guildmaster(implementor, TRAVELLER)
    at_traveller = practice(pupil, "cure self all")
    spent = SESSIONS_SPENT_PATTERN.search(at_traveller)
    assert spent is not None, at_traveller
    assert (int(spent.group(1)), spent.group(2)) == (3, "cure self"), at_traveller
    traveller_list = guild_list(pupil, PUPIL.name, TRAVELLER)
    assert guild_row(traveller_list, "cure self") == (80, 3, "good"), traveller_list

    dismiss_guildmaster(implementor, TRAVELLER)
    bring_guildmaster(implementor, SCORTHER)
    at_scorther = practice(pupil, "cure self")
    assert LIMIT_REPLY in at_scorther, at_scorther
    assert PRACTICED_REPLY not in at_scorther, at_scorther
    scorther_list = guild_list(pupil, PUPIL.name, SCORTHER)
    assert guild_row(scorther_list, "cure self") == (80, 3, "fair"), scorther_list
    assert listed_sessions(scorther_list) == STARTING_SESSIONS - 3, scorther_list


@pytest.mark.practice_limits
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, PUPIL)
@pytest.mark.parametrize(
    ("refusing", "teaching", "skill"),
    [(TRAVELLER, SCORTHER, "magic missile"), (SCORTHER, TRAVELLER, "fireball")],
    ids=["magic-missile-only-at-scorther", "fireball-only-at-the-traveller"],
)
def test_a_spell_one_guildmaster_does_not_teach_is_refused_there_and_taught_by_the_other(
    server, refusing: Guildmaster, teaching: Guildmaster, skill: str
) -> None:
    """Pins that a spell one guildmaster's row has at 0 is refused there and taught by the other.

    A guildmaster whose row has 0 for a spell refuses it; one with a limit teaches it. The
    refusal spends nothing; the other guildmaster's one session spends one. The level-30
    pupil's mage level, 30, covers fireball's level of 21. Practice draws no random number.
    """
    implementor = server.login(IMPLEMENTOR.name)
    pupil = server.login(PUPIL.name)
    give_sessions(implementor, PUPIL.name, STARTING_SESSIONS)
    bring_guildmaster(implementor, refusing)

    refused = practice(pupil, skill)
    assert f"{refusing.line_start} {UNKNOWN_TO_GUILDMASTER_PART}" in refused, refused
    assert PRACTICED_REPLY not in refused, refused
    left = sessions_left(pupil, PUPIL.name, refusing)
    assert left == STARTING_SESSIONS, left

    dismiss_guildmaster(implementor, refusing)
    bring_guildmaster(implementor, teaching)
    taught = practice(pupil, skill)
    assert PRACTICED_REPLY in taught, taught
    assert UNKNOWN_TO_GUILDMASTER_PART not in taught, taught
    teaching_list = guild_list(pupil, PUPIL.name, teaching)
    assert guild_row(teaching_list, skill)[1] == 1, teaching_list
    assert listed_sessions(teaching_list) == STARTING_SESSIONS - 1, teaching_list


@pytest.mark.practice_limits
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, BEORNING_PUPIL)
def test_a_race_outside_the_guildmasters_mask_is_sent_away(server) -> None:
    """Pins that the traveller tells a beorning to go away and teaches it nothing.

    WILL_TEACH (utils.h) tests the pupil's race bit, 1 << 6 for a beorning, against the mask 63.
    The refusal is the guildmaster's speech (do_say), which the pupil reads as
    `A mysterious traveller says '...'`; the session count in `stat player` stays put.
    """
    implementor = server.login(IMPLEMENTOR.name)
    pupil = server.login(BEORNING_PUPIL.name)
    plain = pupil.command("practice")
    language_pattern = PLAIN_ROW_PATTERN_FORMAT.format("human language")
    language_row = re.search(language_pattern, plain.text, re.MULTILINE)
    assert language_row is not None and language_row.group(1) == "Mastered", plain.text
    give_sessions(implementor, BEORNING_PUPIL.name, STARTING_SESSIONS)
    bring_guildmaster(implementor)

    reply = practice(pupil, "blink")
    assert f"{TRAVELLER.line_start} {GO_AWAY_SPEECH}" in reply, reply
    assert PRACTICED_REPLY not in reply, reply
    stat_text = read_stat(implementor, f"player {BEORNING_PUPIL.name.lower()}")
    assert first_number(PRACTICE_SESSIONS_PATTERN, stat_text) == STARTING_SESSIONS, stat_text


@pytest.mark.practice_limits
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, NOVICE_PUPIL)
@pytest.mark.world_resets(guildmaster_reset())
def test_a_spell_above_the_pupils_mage_level_is_not_taught(server) -> None:
    """Pins that a level-8 mage is not experienced enough for fireball (level 21).

    The traveller refuses it. The check compares skills[].level with the pupil's mage level, read
    here from `stat player`. Nothing is practiced and no session is spent.
    """
    implementor = server.login(IMPLEMENTOR.name)
    pupil = server.login(NOVICE_PUPIL.name)
    stat_text = read_stat(implementor, f"player {NOVICE_PUPIL.name.lower()}")
    assert first_number(CLASS_LEVELS_PATTERN, stat_text) == 8, stat_text
    give_sessions(implementor, NOVICE_PUPIL.name, STARTING_SESSIONS)

    reply = practice(pupil, "fireball")
    assert NOT_EXPERIENCED_REPLY in reply, reply
    assert PRACTICED_REPLY not in reply, reply
    left = sessions_left(pupil, NOVICE_PUPIL.name)
    assert left == STARTING_SESSIONS, left


@pytest.mark.practice_limits
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, RANGER_PUPIL)
@pytest.mark.world_resets(guildmaster_reset(ARODEN))
def test_a_specialization_skill_is_taught_only_to_a_pupil_with_that_specialization(
    server,
) -> None:
    """Pins that Aroden refuses stalking without the stealth specialization, then teaches it.

    Stalking's learn_type has LEARN_SPEC (skills[], consts.cpp), so SPECIAL(guild) compares the
    pupil's specialization with its skill_spec, stealth, both in its list, which leaves stalking
    out until the specialization is set, and before a session. The refusal spends nothing; after
    `wizset ... specialization 6` the list shows stalking and the same request spends one
    session. Practice draws no random number.
    """
    implementor = server.login(IMPLEMENTOR.name)
    pupil = server.login(RANGER_PUPIL.name)
    give_sessions(implementor, RANGER_PUPIL.name, STARTING_SESSIONS)

    unspecialized_list = guild_list(pupil, RANGER_PUPIL.name, ARODEN)
    assert not has_guild_row(unspecialized_list, "stalking"), unspecialized_list
    assert has_guild_row(unspecialized_list, "awareness"), unspecialized_list
    refused = practice(pupil, "stalking")
    assert NOT_DEDICATED_REPLY in refused, refused
    assert PRACTICED_REPLY not in refused, refused
    left = sessions_left(pupil, RANGER_PUPIL.name, ARODEN)
    assert left == STARTING_SESSIONS, left

    name = RANGER_PUPIL.name
    wizset(implementor, f"player {name.lower()}", "specialization", STEALTH_SPECIALIZATION, shown_name=name)
    specialized_list = guild_list(pupil, RANGER_PUPIL.name, ARODEN)
    assert guild_row(specialized_list, "stalking") == (0, 0, "Mastered"), specialized_list
    taught = practice(pupil, "stalking")
    assert PRACTICED_REPLY in taught, taught
    assert NOT_DEDICATED_REPLY not in taught, taught
    left = sessions_left(pupil, RANGER_PUPIL.name, ARODEN)
    assert left == STARTING_SESSIONS - 1, left


@pytest.mark.practice_limits
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, CAPPED_PUPIL)
@pytest.mark.world_resets(guildmaster_reset(ARODEN))
def test_a_skill_with_51_sessions_spent_is_refused_at_once(server) -> None:
    """Pins that Aroden refuses travelling to a pupil who has spent 51 sessions on it, at 97%.

    practice_refusal's check is `skills > 50`; the knowledge is below Aroden's limit of 100, so
    the session cap is the only refusal that applies. Nothing is practiced and no session is
    spent.
    """
    implementor = server.login(IMPLEMENTOR.name)
    pupil = server.login(CAPPED_PUPIL.name)
    give_sessions(implementor, CAPPED_PUPIL.name, STARTING_SESSIONS)
    listed = guild_list(pupil, CAPPED_PUPIL.name, ARODEN)
    assert guild_row(listed, "travelling") == (97, SESSION_CAP, "Mastered"), listed

    reply = practice(pupil, "travelling")
    assert TOO_LONG_REPLY in reply, reply
    assert PRACTICED_REPLY not in reply, reply
    left = sessions_left(pupil, CAPPED_PUPIL.name, ARODEN)
    assert left == STARTING_SESSIONS, left


@pytest.mark.practice_limits
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, PERFECT_PUPIL)
@pytest.mark.world_resets(guildmaster_reset())
def test_a_skill_already_at_100_is_refused_as_perfect(server) -> None:
    """Pins that the traveller refuses blink to a pupil whose five sessions put it at 101%.

    practice_refusal checks `knowledge >= 100` first. Nothing is practiced and no session is
    spent.
    """
    implementor = server.login(IMPLEMENTOR.name)
    pupil = server.login(PERFECT_PUPIL.name)
    give_sessions(implementor, PERFECT_PUPIL.name, STARTING_SESSIONS)

    reply = practice(pupil, "blink")
    assert PERFECT_REPLY in reply, reply
    assert PRACTICED_REPLY not in reply, reply
    left = sessions_left(pupil, PERFECT_PUPIL.name)
    assert left == STARTING_SESSIONS, left


# The knowledge a session brings (recalc_skills, spec_pro.cpp) rests on the sessions spent N, the
# skill's level L and difficulty D (skills[], consts.cpp), and the pupil's ranger coefficient C
# (`stat player`'s "Class coofs" Ran), never its overall level. For a ranger skill with L < 20:
#   tmps      = C - L * (1000 - C) / 30
#   tmps      = MIN(1000, tmps * (80 + L) / 100 + 200 - L * 10)
#   t         = 1000 - (N * 20 * tmps * 10) / (D * 100)
#   knowledge = (10000 - t * t / 100) / 99, or 100 when t < 0
# The command stops at the first check practice_refusal fails: knowledge >= 100 gives the
# perfect line, more than 50 sessions the too-long line.
#
# Awareness, L = 1, D = 20:
#   Testranger, C = 1000: tmps = 1000 - 1 * 0 / 30 = 1000
#                         MIN(1000, 1000 * 81 / 100 + 200 - 10) = 810 + 190 = 1000
#     N = 8: t = 1000 - 8 * 20 * 1000 * 10 / 2000 = 1000 - 800 = 200; (10000 - 400) / 99 = 96
#     N = 9: t = 1000 - 900 = 100; (10000 - 100) / 99 = 100, so 9 sessions, then the perfect line.
#   Testwizard, C = 300: tmps = 300 - 1 * 700 / 30 = 300 - 23 = 277
#                        MIN(1000, 277 * 81 / 100 + 200 - 10) = 224 + 190 = 414
#     N = 21: t = 1000 - 21 * 20 * 414 * 10 / 2000 = 1000 - 869 = 131; (10000 - 171) / 99 = 99
#     N = 22: t = 1000 - 22 * 20 * 414 * 10 / 2000 = 1000 - 910 = 90; (10000 - 81) / 99 = 100,
#     so 22 sessions, then the perfect line.
# Travelling, L = 6, D = 30:
#   Testranger, C = 1000: tmps = 1000 - 6 * 0 / 30 = 1000
#                         MIN(1000, 1000 * 86 / 100 + 200 - 60) = 860 + 140 = 1000
#     N = 13: t = 1000 - 13 * 20 * 1000 * 10 / 3000 = 1000 - 866 = 134; (10000 - 179) / 99 = 99
#     N = 14: t = 1000 - 933 = 67; (10000 - 44) / 99 = 100, so 14 sessions, then the perfect line.
#   Testlowran, C = 264: tmps = 264 - 6 * 736 / 30 = 264 - 147 = 117
#                        MIN(1000, 117 * 86 / 100 + 200 - 60) = 100 + 140 = 240
#     N = 51: t = 1000 - 51 * 20 * 240 * 10 / 3000 = 1000 - 816 = 184; (10000 - 338) / 99 = 97,
#     so the cap of 51 sessions ends it at 97% with the too-long line.
# Awareness cannot show the cap for a human: a ranger level of 1 needs a coefficient of at least
# 34 (advance_mini_level, limits.cpp), which square_root[] makes 100 or more, and C = 100 masters
# awareness in 37 sessions (tmps = 246, t = 1000 - 37 * 20 * 246 * 10 / 2000 = 90).


@pytest.mark.practice_coefficients
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, RANGER_PUPIL, WIZARD_PUPIL)
@pytest.mark.world_resets(guildmaster_reset(ARODEN))
@pytest.mark.parametrize(
    ("pupil_character", "coefficient", "sessions"),
    [(RANGER_PUPIL, 1000, 9), (WIZARD_PUPIL, 300, 22)],
    ids=["ranger", "wizard"],
)
def test_awareness_takes_fewer_sessions_to_master_the_higher_the_ranger_coefficient(
    server, pupil_character: Character, coefficient: int, sessions: int
) -> None:
    """Pins that awareness takes 9 sessions to master at ranger coefficient 1000 and 22 at 300.

    The counts are worked out in the comment above. Practice draws no random number.
    """
    implementor = server.login(IMPLEMENTOR.name)
    pupil = server.login(pupil_character.name)
    coefficient_and_level = ranger_coefficient_and_level(implementor, pupil_character.name)
    assert coefficient_and_level[0] == coefficient, coefficient_and_level
    give_sessions(implementor, pupil_character.name, PLENTY_OF_SESSIONS)

    reply = practice(pupil, "awareness all")
    spent = SESSIONS_SPENT_PATTERN.search(reply)
    assert spent is not None, reply
    assert (int(spent.group(1)), spent.group(2)) == (sessions, "awareness"), reply
    assert PERFECT_REPLY in reply, reply
    assert spent.start() < reply.index(PERFECT_REPLY), reply
    listed = guild_list(pupil, pupil_character.name, ARODEN)
    assert guild_row(listed, "awareness") == (100, sessions, "Mastered"), listed


@pytest.mark.practice_coefficients
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, RANGER_PUPIL, LOW_RANGER_PUPIL)
@pytest.mark.world_resets(guildmaster_reset(ARODEN))
@pytest.mark.parametrize(
    ("pupil_character", "coefficient", "sessions", "final_line", "knowledge"),
    [
        (RANGER_PUPIL, 1000, 14, PERFECT_REPLY, 100),
        (LOW_RANGER_PUPIL, 264, SESSION_CAP, TOO_LONG_REPLY, 97),
    ],
    ids=["ranger-masters-it", "low-ranger-reaches-the-cap"],
)
def test_a_low_ranger_coefficient_reaches_the_session_cap_before_mastering_travelling(
    server, pupil_character: Character, coefficient: int, sessions: int, final_line: str, knowledge: int
) -> None:
    """Pins that a ranger masters travelling in 14 sessions and a 7-point ranger stops at the cap.

    The 7-point ranger stops at 97%. Travelling (level 6) needs a ranger level of 6, which both
    pupils have. The counts and knowledge are worked out in the comment above. Practice draws no
    random number.
    """
    implementor = server.login(IMPLEMENTOR.name)
    pupil = server.login(pupil_character.name)
    pupil_coefficient, ranger_level = ranger_coefficient_and_level(implementor, pupil_character.name)
    assert pupil_coefficient == coefficient, pupil_coefficient
    assert ranger_level >= 6, ranger_level
    give_sessions(implementor, pupil_character.name, PLENTY_OF_SESSIONS)

    reply = practice(pupil, "travelling all")
    spent = SESSIONS_SPENT_PATTERN.search(reply)
    assert spent is not None, reply
    assert (int(spent.group(1)), spent.group(2)) == (sessions, "travelling"), reply
    assert final_line in reply, reply
    assert spent.start() < reply.index(final_line), reply
    listed = guild_list(pupil, pupil_character.name, ARODEN)
    assert guild_row(listed, "travelling") == (knowledge, sessions, "Mastered"), listed
    assert listed_sessions(listed) == PLENTY_OF_SESSIONS - sessions, listed


# One session of magic missile (L = 0, D = 10, N = 1) rests only on the pupil's mage coefficient
# C (recalc_skills, spec_pro.cpp), which GET_PROF_COOF (utils.h) adjusts by race from
# square_root[100] = 1000 for a mage: an uruk-hai loses GET_URUK_MAGE_PENALTY, 100; an orc gets
# (1000 * 2 + 2) / 3 = 667.
#   tmps      = C - L * (1000 - C) / 30 = C - 0 = C
#   tmps      = MIN(1000, tmps * (80 + L) / 100 + 200 - L * 10) = MIN(1000, C * 80 / 100 + 200)
#   t         = 1000 - (N * 20 * tmps * 10) / (D * 100) = 1000 - 200 * tmps / 1000
#   knowledge = (10000 - t * t / 100) / 99
# Testpupil, human, C = 1000: tmps = 800 + 200 = 1000; t = 1000 - 200 = 800;
#   (10000 - 640000 / 100) / 99 = 3600 / 99 = 36
# Testuruk, uruk-hai, C = 900: tmps = 720 + 200 = 920; t = 1000 - 184 = 816;
#   (10000 - 665856 / 100) / 99 = (10000 - 6658) / 99 = 33
# Testorc, orc, C = 667: tmps = 533 + 200 = 733; t = 1000 - 146 = 854;
#   (10000 - 729316 / 100) / 99 = (10000 - 7293) / 99 = 27


@pytest.mark.practice_coefficients
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, PUPIL, URUK_PUPIL, ORC_PUPIL)
@pytest.mark.parametrize(
    ("pupil_character", "guildmaster", "coefficient", "mage_level", "knowledge", "practiced_line"),
    [
        (PUPIL, SCORTHER, 1000, 30, 36, PRACTICED_REPLY),
        (URUK_PUPIL, URUK_MAGE_TEACHER, 900, 27, 33, PRACTICED_REPLY),
        (ORC_PUPIL, SHAMAN, 667, 20, 27, SHAMAN_PRACTICED_REPLY),
    ],
    ids=["human-at-scorther", "uruk-hai-at-the-young-uruk-mage", "orc-at-the-glass-eyed-shaman"],
)
def test_one_session_of_magic_missile_teaches_each_race_by_its_adjusted_mage_coefficient(
    server,
    pupil_character: Character,
    guildmaster: Guildmaster,
    coefficient: int,
    mage_level: int,
    knowledge: int,
    practiced_line: str,
) -> None:
    """Pins that one magic missile session gives a human 36%, an uruk-hai 33% and an orc 27%.

    Each pupil practises at a guildmaster of its own side: Scorther, the young uruk mage (4601)
    and the glass-eyed shaman (32200). The knowledge is worked out in the comment above, from the
    coefficient `stat player` shows; the lower coefficients also give lower mage levels (27 and
    20), above magic missile's level of 0. Only the pupil's own lines are asserted: the
    implementor reads a dark-side pupil by a star name. Practice draws no random number.
    """
    implementor = server.login(IMPLEMENTOR.name)
    pupil = server.login(pupil_character.name)
    stat_values = mage_coefficient_and_level(implementor, pupil_character.name)
    assert stat_values == (coefficient, mage_level), stat_values
    give_sessions(implementor, pupil_character.name, STARTING_SESSIONS)
    bring_guildmaster(implementor, guildmaster)

    reply = practice(pupil, "magic missile")
    assert practiced_line in reply, reply
    assert SESSIONS_SPENT_PATTERN.search(reply) is None, reply
    listed = guild_list(pupil, pupil_character.name, guildmaster)
    assert guild_row(listed, "magic missile") == (knowledge, 1, "Mastered"), listed
    assert listed_sessions(listed) == STARTING_SESSIONS - 1, listed


@pytest.mark.practice_lists
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, ORC_PUPIL)
@pytest.mark.world_resets(guildmaster_reset(SHAMAN))
def test_the_glass_eyed_shaman_teaches_and_stops_with_its_own_lines(server) -> None:
    """Pins that the shaman's row has its own practice and limit lines, which an orc pupil reads.

    Chill ray (L = 4, D = 10), which the shaman teaches up to 20, at the orc's mage coefficient
    of 667 and mage level of 20, above chill ray's level (recalc_skills, spec_pro.cpp):
      tmps = 667 - 4 * (1000 - 667) / 30 = 667 - 44 = 623
      tmps = MIN(1000, 623 * 84 / 100 + 200 - 40) = 523 + 160 = 683
      t    = 1000 - (1 * 20 * 683 * 10) / (10 * 100) = 1000 - 136 = 864
      knowledge = (10000 - 746496 / 100) / 99 = (10000 - 7464) / 99 = 25
    so the first session passes 20, is cut back to it, and the shaman's limit line ends the
    command. Practice draws no random number.
    """
    implementor = server.login(IMPLEMENTOR.name)
    pupil = server.login(ORC_PUPIL.name)
    coefficient_and_level = mage_coefficient_and_level(implementor, ORC_PUPIL.name)
    assert coefficient_and_level == (667, 20), coefficient_and_level
    give_sessions(implementor, ORC_PUPIL.name, STARTING_SESSIONS)

    reply = practice(pupil, "chill ray all")
    assert SHAMAN_PRACTICED_REPLY in reply, reply
    assert SHAMAN_LIMIT_REPLY in reply, reply
    assert reply.index(SHAMAN_PRACTICED_REPLY) < reply.index(SHAMAN_LIMIT_REPLY), reply
    assert PRACTICED_REPLY not in reply and LIMIT_REPLY not in reply, reply
    assert SESSIONS_SPENT_PATTERN.search(reply) is None, reply
    listed = guild_list(pupil, ORC_PUPIL.name, SHAMAN)
    assert guild_row(listed, "chill ray") == (20, 1, "bad"), listed
    assert listed_sessions(listed) == STARTING_SESSIONS - 1, listed


@pytest.mark.practice_coefficients
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, LHUTH_PUPIL)
@pytest.mark.world_resets(guildmaster_reset(MAGUS))
def test_an_uruk_lhuth_knows_blink_at_10_percent_unpractised_and_again_after_pracreset(
    server,
) -> None:
    """Pins that an uruk-lhuth knows blink at 10% unpractised, and again after a pracreset.

    recalc_skills (spec_pro.cpp) sets blink to 10 for a RACE_MAGUS player whose
    blink is 0, and do_pracreset ends with recalc_skills. The Magus (13600) lists blink at 10%
    with 0 sessions; one session takes it to 36% at the pupil's mage coefficient of 1000 (the
    same 36 as a human's first blink session); `tell angel pracreset` brings back 10% and 0.
    Nothing on the way draws a random number.
    """
    implementor = server.login(IMPLEMENTOR.name)
    pupil = server.login(LHUTH_PUPIL.name)
    coefficient_and_level = mage_coefficient_and_level(implementor, LHUTH_PUPIL.name)
    assert coefficient_and_level == (1000, 30), coefficient_and_level
    give_sessions(implementor, LHUTH_PUPIL.name, STARTING_SESSIONS)
    goto(implementor, world.ROOM_ARENA_CENTRE)
    unpractised = guild_list(pupil, LHUTH_PUPIL.name, MAGUS)
    assert guild_row(unpractised, "blink") == (10, 0, "Mastered"), unpractised

    reply = practice(pupil, "blink")
    assert PRACTICED_REPLY in reply, reply
    practised = guild_list(pupil, LHUTH_PUPIL.name, MAGUS)
    assert guild_row(practised, "blink") == (36, 1, "Mastered"), practised

    load_mob(implementor, world.MOB_SHINING_ANGEL, "a shining angel")
    reset = reply_to(pupil, "tell angel pracreset", [PRACRESET_DONE])
    assert PRACRESET_DONE in reset, reset
    after_reset = guild_list(pupil, LHUTH_PUPIL.name, MAGUS)
    assert guild_row(after_reset, "blink") == (10, 0, "Mastered"), after_reset


# Weapon mastery adds 3/8 of its sessions to each weapon skill but natural attacks and swimming
# (recalc_skills, spec_pro.cpp: pracs_used += skills[10] * 20 * 3 / 8; its comment says three
# mastery sessions count as one weapon session, which the arithmetic does not give: three make
# 22, not 20). Weapon skills are PROF_WARRIOR, so no coefficient enters:
#   pracs_used = (N * 20 + M * 20 * 3 / 8) * 10000, with N = 0 and M = 8: 60 * 10000 = 600000
#   t          = 1000 - pracs_used / (D * 100)
#   knowledge  = (10000 - t * t / 100) / 99
# Slashing, whips/flails, piercing, spears, axes, D = 30: t = 1000 - 200 = 800; 3600 / 99 = 36
# Concussion, D = 15: t = 1000 - 400 = 600; (10000 - 3600) / 99 = 64
# Two-handed, D = 40: t = 1000 - 150 = 850; (10000 - 7225) / 99 = 28
# Weapon mastery itself, D = 80, N = 8: pracs_used = 160 * 10000; t = 1000 - 200 = 800; 36
MASTERY_KNOWLEDGE = [
    ("slashing", 36, 0),
    ("concussion", 64, 0),
    ("whips/flails", 36, 0),
    ("piercing", 36, 0),
    ("spears", 36, 0),
    ("axes", 36, 0),
    ("two-handed", 28, 0),
    ("weapon mastery", 36, MASTERY_SESSIONS_SPENT),
]


@pytest.mark.practice_coefficients
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(MASTERY_PUPIL)
@pytest.mark.world_resets(guildmaster_reset(URUK_TRAINER))
def test_weapon_mastery_sessions_raise_every_weapon_skill_without_sessions_of_its_own(
    server,
) -> None:
    """Pins that eight weapon mastery sessions show each weapon skill at its mastery share.

    The uruk guildmaster (2043) lists the uruk-hai warrior's weapon skills at the knowledge the
    comment above works out, with 0 sessions spent on each. Nothing draws a random number.
    """
    pupil = server.login(MASTERY_PUPIL.name)

    listed = guild_list(pupil, MASTERY_PUPIL.name, URUK_TRAINER)
    for skill, knowledge, sessions_spent in MASTERY_KNOWLEDGE:
        assert guild_row(listed, skill)[:2] == (knowledge, sessions_spent), (skill, listed)


@pytest.mark.practice_lists
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, PUPIL)
@pytest.mark.parametrize(
    "guildmaster",
    [NO_ROW_TEACHER, PAST_LAST_ROW_TEACHER],
    ids=["program-0", "program-62"],
)
def test_a_guildmaster_whose_program_names_no_row_is_only_posing(
    server, guildmaster: Guildmaster
) -> None:
    """Pins that a guild copy with program 0 or 62 is only posing, and one with program 61 lists.

    SPECIAL(guild) (spec_pro.cpp) takes the row as the program number less one and refuses one
    outside guildmasters[]'s 61 rows (guildmaster_number, consts.cpp) with the posing line, both
    for the list and for a practice request. The control, the wise mystic copy with program 61,
    lists its row. Nothing draws a random number.
    """
    implementor = server.login(IMPLEMENTOR.name)
    pupil = server.login(PUPIL.name)
    give_sessions(implementor, PUPIL.name, STARTING_SESSIONS)
    bring_guildmaster(implementor, guildmaster)

    posing = reply_to(pupil, "practice", [POSING_REPLY, PLAIN_SESSIONS_PART])
    assert POSING_REPLY in posing, posing
    assert GUILD_SESSIONS_PATTERN.search(posing) is None, posing
    requested = practice(pupil, "archery")
    assert POSING_REPLY in requested, requested
    assert PRACTICED_REPLY not in requested, requested

    dismiss_guildmaster(implementor, guildmaster)
    bring_guildmaster(implementor, LAST_ROW_TEACHER)
    listed = guild_list(pupil, PUPIL.name, LAST_ROW_TEACHER)
    assert POSING_REPLY not in listed, listed
    assert listed_sessions(listed) == STARTING_SESSIONS, listed


@pytest.mark.practice_sessions
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, OVERSPENT_PUPIL)
def test_more_sessions_spent_than_the_formula_gives_leaves_a_negative_count(server) -> None:
    """Pins that spending more sessions than the login recount gives leaves a negative count.

    update_available_practice_sessions (char_utils.cpp) sets the count to the formula less the
    sessions spent, with no floor. The pupil has spent 153; with learning set to 0 the recount at
    the next login is below that. An unpractised skill is then refused with the no-sessions line,
    and the count stays where it was. Nothing on the way draws a random number.
    """
    implementor = server.login(IMPLEMENTOR.name)
    pupil = server.login(OVERSPENT_PUPIL.name)
    name = OVERSPENT_PUPIL.name
    after_relog = read_stat(implementor, f"player {name.lower()}")
    assert first_number(LEVEL_PATTERN, after_relog) == 30, after_relog
    learning = LEARNING_PATTERN.search(after_relog)
    assert learning is not None and int(learning.group(2)) == 0, after_relog
    # get_max_practice_count - get_spent_practice_count (char_utils.cpp):
    #   level * PRACS_PER_LEVEL + level * abilities.lea / LEA_PRAC_FACTOR + 10 - spent
    #   = 30 * 3 + 30 * 0 / 5 + 10 - 153     (level 30; Lea base 0, read above; 3 * 51 spent)
    #   = 90 + 0 + 10 - 153 = -53
    plain = pupil.command("practice")
    assert first_number(PLAIN_SESSIONS_PATTERN, plain.text) == -53, plain.text

    bring_guildmaster(implementor)
    reply = practice(pupil, "fireball")
    assert NO_SESSIONS_REPLY in reply, reply
    assert PRACTICED_REPLY not in reply, reply
    left = sessions_left(pupil, name)
    assert left == -53, left


@pytest.mark.practice_lists
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, PUPIL)
@pytest.mark.world_resets(guildmaster_reset())
def test_a_sitting_pupil_gets_the_plain_list_and_practises_nothing(server) -> None:
    """Pins that a sitting pupil's `practice blink` at a guildmaster gives the plain list.

    SPECIAL(guild) (spec_pro.cpp) lets the command go on below POSITION_STANDING, and the command
    itself is allowed from resting, so do_practice answers with the plain list, which ignores the
    skill named. No session is spent.
    """
    implementor = server.login(IMPLEMENTOR.name)
    pupil = server.login(PUPIL.name)
    give_sessions(implementor, PUPIL.name, STARTING_SESSIONS)
    sat = pupil.command("sit")
    assert sat.contains(SIT_DOWN_REPLY), pupil.everything

    reply = reply_to(pupil, "practice blink", PRACTICE_OUTCOMES)
    assert first_number(PLAIN_SESSIONS_PATTERN, reply) == STARTING_SESSIONS, reply
    assert TRAVELLER.list_header_format.format(PUPIL.name) not in reply, reply
    assert PRACTICED_REPLY not in reply, reply

    stood = pupil.command("stand")
    assert stood.contains(STAND_UP_REPLY), pupil.everything
    left = sessions_left(pupil, PUPIL.name)
    assert left == STARTING_SESSIONS, left


@pytest.mark.practice_lists
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, PUPIL)
@pytest.mark.world_resets(guildmaster_reset())
def test_a_skill_abbreviation_names_the_first_skill_in_table_order(server) -> None:
    """Pins that `practice bl` asks the traveller for block exit, not blink.

    find_practice_skill (spec_pro.cpp) takes the first skill whose name starts with the words,
    and block exit (17) comes before blink (76) in skills[] (consts.cpp). The traveller does not
    teach block exit, so the answer is its refusal and no session is spent.
    """
    implementor = server.login(IMPLEMENTOR.name)
    pupil = server.login(PUPIL.name)
    give_sessions(implementor, PUPIL.name, STARTING_SESSIONS)

    reply = practice(pupil, "bl")
    assert f"{TRAVELLER.line_start} {UNKNOWN_TO_GUILDMASTER_PART}" in reply, reply
    assert PRACTICED_REPLY not in reply, reply
    listed = guild_list(pupil, PUPIL.name)
    assert guild_row(listed, "blink")[:2] == (0, 0), listed
    assert listed_sessions(listed) == STARTING_SESSIONS, listed


# The listed pupil specialized in arcane, the only specialization that may set a casting speed.
ARCANE_PUPIL = dataclasses.replace(LISTED_PUPIL, stored=StoredValues(specialization="arcane"))
# The plain list's cost for blink (L = 5, beats 13, min_usesmana 5; skills[], consts.cpp) at the
# listed pupil's mage level of 30, from do_practice (spec_pro.cpp), which do_cast (spell_pa.cpp)
# matches for every mage spell but expose elements:
#   time    = CASTING_TIME = beats * 30 / (30 + mage level) = 13 * 30 / 60 = 6
#   stamina = USE_MANA = MAX(5, 120 / (3 + MAX(-1, 30 - 5))) = MAX(5, 120 / 28) = MAX(5, 4) = 5
#   fast: time MAX(1, (6 + 1) / 2) = 3,       stamina 5 * 3 / 2 = 7
#   slow: time MAX(1, (6 * 3 + 1) / 2) = 9,   stamina (5 * 1 + 1) / 2 = 3


@pytest.mark.practice_lists
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, ARCANE_PUPIL)
@pytest.mark.parametrize(
    ("speed", "casting_time", "stamina"),
    [("normal", 6, 5), ("fast", 3, 7), ("slow", 9, 3)],
    ids=["normal", "fast", "slow"],
)
def test_the_plain_list_shows_the_casting_cost_of_the_chosen_casting_speed(
    server, speed: str, casting_time: int, stamina: int
) -> None:
    """Pins that an arcane mage's plain list shows blink's time and stamina for its casting speed.

    `set casting <speed>` (do_casting, act_othe.cpp) needs the arcane specialization; the costs
    are worked out in the comment above. Nothing draws a random number.
    """
    implementor = server.login(IMPLEMENTOR.name)
    pupil = server.login(LISTED_PUPIL.name)
    name = LISTED_PUPIL.name
    coefficient_and_level = mage_coefficient_and_level(implementor, name)
    assert coefficient_and_level[1] == 30, coefficient_and_level
    reply = pupil.command(f"set casting {speed}")
    assert reply.contains(f"You are now using {speed} casting."), reply.text

    plain = pupil.command("practice")
    blink_spell_row_pattern = PLAIN_SPELL_ROW_PATTERN_FORMAT.format("blink")
    plain_row = re.search(blink_spell_row_pattern, plain.text, re.MULTILINE)
    assert plain_row is not None, plain.text
    row_text = plain_row.group(0)
    costs = re.search(r"\(\s*(\d+) time,\s+(\d+) stamina\)", row_text)
    assert costs is not None, plain.text
    assert (int(costs.group(1)), int(costs.group(2))) == (casting_time, stamina), plain.text
