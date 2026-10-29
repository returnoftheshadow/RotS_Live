"""Mob scripts run as written: a false IF skips its whole block, nested blocks included, section
rows change nothing, a script that has finished stays finished when its mob is killed, a line
that would crash is reported and skipped, script writes to hit points are clamped and keep the
position in step, a north door opens and locks, and the boot check reports every line that names
something missing."""

from __future__ import annotations

import re
import time
from dataclasses import dataclass
from typing import Optional

import pytest

from rots_harness import world
from rots_harness.characters import IMPLEMENTOR, Character
from rots_harness.session import GameSession
from rots_harness.world_resets import MobReset

pytestmark = [pytest.mark.scenario, pytest.mark.scripts]

# Room 1701, zone 17's yard, whose north exit is a door (keyword door) to 1702.
ROOM_GATEHOUSE_YARD = 1701
# Room 1702, north of the yard, whose south exit is the same door seen from its side (keyword gate).
ROOM_BEYOND_THE_GATE = 1702


@dataclass(frozen=True)
class ScriptMob:
    """A test-world mob that runs a script (tests/integration/world/mob/17.mob, scr/17.scr)."""

    # The mob's virtual number; from 1703 on, its script has the same number.
    vnum: int
    # The keyword that finds this mob and no other in the room.
    keyword: str
    # The mob's short description.
    short_description: str


def placed(*mobs: ScriptMob) -> pytest.MarkDecorator:
    """The world_resets marker that places each mob in the implementors' room at boot, in order."""
    return pytest.mark.world_resets(*(MobReset(mob.vnum, world.ROOM_IMMORTAL_START) for mob in mobs))


# Script 1700 (tests/integration/world/scr/17.scr): Brali's shape (#2709), an outer IF on the
# star token whose block holds a nested guard IF / BEGIN / ... / END_ELSE_BEGIN / END with an
# empty else branch, then the reward; the outer else branch and a final line follow.
PLAIN_SMITH = ScriptMob(vnum=1700, keyword="plain", short_description="a plain smith")
# Script 1701: script 1700 with SECTION rows between the outer IF and its BEGIN, before the
# nested IF, and on the nested block's empty else branch.
LABELLED_SMITH = ScriptMob(vnum=1701, keyword="labelled", short_description="a labelled smith")
SMITHS = [
    pytest.param(PLAIN_SMITH, id="plain", marks=placed(PLAIN_SMITH)),
    pytest.param(LABELLED_SMITH, id="labelled", marks=placed(LABELLED_SMITH)),
]

# The star token, a grey pebble (tests/integration/world/obj/17.obj) and the leather cap
# (obj/11.obj), a piece of head armour.
STAR_TOKEN_VNUM = 1700
PEBBLE_VNUM = 1701
LEATHER_CAP_VNUM = 1137
# The weary foreman (mob 1702), whose script 1702 says a line when anyone speaks, waits four
# passes (DO_WAIT), says a last line and ends.
WEARY_FOREMAN = ScriptMob(vnum=1702, keyword="weary", short_description="a weary foreman")
# The patient foreman (mob 1721): script 1702 with a wait of 400 passes, 100 seconds.
PATIENT_FOREMAN = ScriptMob(vnum=1721, keyword="patient", short_description="a patient foreman")
# A mob without a script, for an implementor to force.
PORTER = ScriptMob(vnum=1722, keyword="porter", short_description="a porter")

# What the smiths say (DO_SAY rows of scripts 1700 and 1701), in script order: the nested
# guard's body, which only a mob giver reaches; the reward inside the star token's block; the
# outer else branch; and the last row, which a player's give reaches.
GUARD_LINE = "I do not trade with your kind."
REWARD_LINE = "A star token! Here is your reward."
NOT_A_TOKEN_LINE = "That is no star token."
FINAL_LINE = "Who is next?"
# What the foremen say before and after their wait (scripts 1702 and 1721).
FAREWELL_LINE = "Goodbye, then."
LAST_LINE = "My shift is over at last."
# What the implementor reads when it slays a mob (do_slay, act_offe.cpp), before raw_kill runs.
SLAY_MARKER = "You chop him to pieces!  Ah!  The blood!"
# The stat line that holds a character's delay (do_stat_character, act_wiz.cpp).
DELAY_LINE_PATTERN = re.compile(r"Delay command:(-?\d+) delay_value:(-?\d+)")
# The command number a script's wait leaves as the delay command (CMD_SCRIPT, interpre.h).
SCRIPT_DELAY_COMMAND = 9999
# The stat lines with a character's current hit points, maximum and hourly gain, its position,
# and its constitution's current value.
HIT_POINTS_PATTERN = re.compile(r"HP :\[(-?\d+)/(-?\d+)\+(-?\d+)\(")
POSITION_PATTERN = re.compile(r"Pos: ([A-Za-z ]+), Fighting:")
CONSTITUTION_PATTERN = re.compile(r"Con:\s*\[(-?\d+)/")
# The stat line after the delay line's block; once it arrives the drain collects the rest.
STAT_LAST_LINE_MARKER = "VUL:"
# Seconds to wait for a foreman's last line: four passes are one second (OPT_USEC, comm.cpp);
# the rest leaves room for a loaded host. Also the wait for a line to reach game.log.
WAIT_TIMEOUT = 10.0
# Seconds to keep reading after an outcome, for text already sent to arrive.
SETTLE_SECONDS = 1.0
# Seconds between two reads of game.log while waiting for a line.
LOG_POLL_SECONDS = 0.1

# What every mob of scripts 1704 to 1713 says last.
STILL_HERE_LINE = "Still here."


def said(short_description: str, line: str) -> str:
    """What the implementor reads when a mob says a line (say_to_char, act_comm.cpp).

    act() capitalises the first letter of the line it sends.
    """
    return f"{short_description[0].upper()}{short_description[1:]} says '{line}'"


def death_cry(short_description: str) -> str:
    """What a character in the room reads from raw_kill's death_cry (fight.cpp)."""
    return f"Your blood freezes as you hear {short_description}'s death cry."


def load_mob(implementor: GameSession, mob: ScriptMob) -> None:
    """Loads a mob into the implementor's room and checks the game's reply."""
    reply = implementor.command(f"load mob {mob.vnum}")
    assert reply.contains(f"You create {mob.short_description}."), reply.text


def give_new_object(implementor: GameSession, vnum: int, keyword: str, mob: ScriptMob) -> str:
    """Loads an object and gives it to the mob; returns the give's reply.

    The ON_RECEIVE trigger runs inside the give (perform_give, act_obj1.cpp), so everything the
    mob's script says arrives in the reply, before its prompt.
    """
    assert implementor.command(f"load obj {vnum}").contains("You create")
    reply = implementor.command(f"give {keyword} {mob.keyword}")
    assert reply.contains("You give"), reply.text
    return reply.text


def read_mob_stat(implementor: GameSession, mob: ScriptMob) -> str:
    """The implementor's stat of the mob; several of its lines end in ], so command() would stop early."""
    implementor.send_line(f"stat mob {mob.keyword}")
    return implementor.expect([STAT_LAST_LINE_MARKER]) + implementor.drain(SETTLE_SECONDS)


def stat_value(pattern: re.Pattern, stat: str) -> str:
    """The first group of the pattern in the stat text."""
    match = pattern.search(stat)
    assert match is not None, stat
    return match.group(1)


def log_messages(server) -> list:
    """The messages mudlog wrote to game.log, each line being '<type>, <time> :: <message>' (utility.cpp)."""
    text = (server.run_dir / "game.log").read_text(encoding="utf-8", errors="replace")
    return [line.split(" :: ", 1)[1] for line in text.splitlines() if " :: " in line]


def count_logged(server, message: str, expected_count: int) -> int:
    """How many times game.log holds the message, once it holds it expected_count times or WAIT_TIMEOUT passes.

    The server writes its log before it sends the reply that caused it; the Docker launcher
    relays the log through compose, so the line can reach the file a little later.
    """
    deadline = time.monotonic() + WAIT_TIMEOUT
    while log_messages(server).count(message) < expected_count and time.monotonic() < deadline:
        time.sleep(LOG_POLL_SECONDS)
    return log_messages(server).count(message)


def finish_the_foremans_script(implementor: GameSession) -> None:
    """Starts the weary foreman's script by speaking, and waits for the script's last line.

    The first line runs inside the say (do_say, act_comm.cpp). The last runs when the foreman's
    wait ends on the game loop's waiting list (comm.cpp), which runs every pass under the
    affect-hours schedule too.
    """
    spoken = implementor.command("say Your shift is done.")
    assert said(WEARY_FOREMAN.short_description, FAREWELL_LINE) in spoken.text, spoken.text
    last_line_said = said(WEARY_FOREMAN.short_description, LAST_LINE)
    finished = implementor.expect([last_line_said], timeout=WAIT_TIMEOUT)
    finished += implementor.drain(SETTLE_SECONDS)
    assert finished.count(last_line_said) == 1, finished


def start_the_patient_foremans_wait(implementor: GameSession) -> None:
    """Starts the patient foreman's script, which then waits for 100 seconds."""
    spoken = implementor.command("say Your shift is done.")
    assert said(PATIENT_FOREMAN.short_description, FAREWELL_LINE) in spoken.text, spoken.text


@pytest.mark.scripts_control_flow
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR)
@pytest.mark.parametrize("smith", SMITHS)
def test_a_false_if_skips_its_whole_block_with_the_nested_block_inside(server, smith: ScriptMob) -> None:
    """A false outer IF skips its block, nested IF/ELSE included, and runs its else branch.

    Pins #305: get_next_command (script.cpp) skips a nested IF / BEGIN / ... /
    END_ELSE_BEGIN / END with an empty else branch as a whole, so giving a smith anything but
    the star token runs the outer else branch and never the reward (Brali's script #2709 has
    this shape). The labelled smith pins #368 on the same path: section rows between the IF
    and its BEGIN and on the nested empty else branch leave the skip where it is. Nothing here
    draws a random number.
    """
    implementor = server.login(IMPLEMENTOR.name)

    reply = give_new_object(implementor, PEBBLE_VNUM, "pebble", smith)
    assert said(smith.short_description, FINAL_LINE) in reply, reply
    assert said(smith.short_description, NOT_A_TOKEN_LINE) in reply, reply
    assert REWARD_LINE not in reply, reply
    assert GUARD_LINE not in reply, reply


@pytest.mark.scripts_control_flow
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR)
@pytest.mark.parametrize("smith", SMITHS)
def test_a_true_if_runs_its_block_past_the_skipped_nested_guard(server, smith: ScriptMob) -> None:
    """A true outer IF runs its block: the false nested guard is skipped and the reward runs.

    Pins the other side of #305: inside the star token's block the nested guard is false for a
    player giver, its block is skipped, and the script goes on to the reward, then past the
    outer else branch to the last row. The labelled smith pins #368 here too: a section row on
    the path that runs, including the one labelling the empty else branch the guard's skip
    lands on, does nothing. Nothing here draws a random number.
    """
    implementor = server.login(IMPLEMENTOR.name)

    reply = give_new_object(implementor, STAR_TOKEN_VNUM, "token", smith)
    reward = said(smith.short_description, REWARD_LINE)
    final = said(smith.short_description, FINAL_LINE)
    assert reward in reply and final in reply, reply
    assert reply.index(reward) < reply.index(final), reply
    assert NOT_A_TOKEN_LINE not in reply, reply
    assert GUARD_LINE not in reply, reply


@pytest.mark.scripts_control_flow
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR)
@placed(PLAIN_SMITH, PORTER)
def test_a_true_nested_guard_halts_the_script_for_a_mob_giver(server) -> None:
    """A smith given the star token by a mob runs the nested guard's body and stops there.

    The other branch of script 1700's nested guard: IF_IS_NPC on the giver is true for a mob,
    so the smith says the guard line and RETURN_FALSE ends the script before the reward and
    the last row. perform_give (act_obj1.cpp) ignores the trigger's return value, so the give
    itself still happens. A pebble never reaches the guard, which sits inside the star token's
    block. Nothing here draws a random number.
    """
    implementor = server.login(IMPLEMENTOR.name)
    give_new_object(implementor, STAR_TOKEN_VNUM, "token", PORTER)

    reply = implementor.command(f"force {PORTER.keyword} give token {PLAIN_SMITH.keyword}").text
    assert "A porter gives a star token to a plain smith." in reply, reply
    assert said(PLAIN_SMITH.short_description, GUARD_LINE) in reply, reply
    assert REWARD_LINE not in reply, reply
    assert FINAL_LINE not in reply, reply


@pytest.mark.scripts_delays
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR)
@placed(WEARY_FOREMAN)
def test_a_finished_script_leaves_no_script_delay_on_its_mob(server) -> None:
    """Once a waiting script has run to its end, the mob's delay command is cleared.

    Pins #270's continue_char_script (script.cpp), which clears delay.cmd before the script
    goes on after a DO_WAIT. Left set, the script's command (CMD_SCRIPT, 9999, interpre.h)
    stays on the mob with no wait to count down. Nothing here draws a random number.
    """
    implementor = server.login(IMPLEMENTOR.name)
    finish_the_foremans_script(implementor)

    stat = read_mob_stat(implementor, WEARY_FOREMAN)
    delay = DELAY_LINE_PATTERN.search(stat)
    assert delay is not None, stat
    assert (int(delay.group(1)), int(delay.group(2))) == (0, 0), stat


@pytest.mark.scripts_delays
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR)
@placed(PATIENT_FOREMAN)
def test_a_waiting_script_shows_its_delay_on_its_mob(server) -> None:
    """While a script waits, stat shows the script's delay command and the passes left.

    The positive control for the finished-script pin above: during the patient foreman's
    100-second wait, DO_WAIT's WAIT_STATE_BRIEF (utils.h) has set the delay command to
    CMD_SCRIPT and the wait value counts down from 400. Nothing here draws a random number.
    """
    implementor = server.login(IMPLEMENTOR.name)
    start_the_patient_foremans_wait(implementor)

    stat = read_mob_stat(implementor, PATIENT_FOREMAN)
    delay = DELAY_LINE_PATTERN.search(stat)
    assert delay is not None, stat
    assert int(delay.group(1)) == SCRIPT_DELAY_COMMAND, stat
    assert int(delay.group(2)) > 0, stat


@pytest.mark.scripts_delays
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR)
@placed(WEARY_FOREMAN)
def test_slaying_a_mob_whose_script_finished_does_not_repeat_its_last_line(server) -> None:
    """Killing a mob whose script has finished does not run the script's last line again.

    Pins #270: raw_kill (fight.cpp) completes a pending delay only while one is still counting
    down, and the finished script left none. A repeated line would arrive between the slay
    message and the death cry. Either of #270's two changes prevents it alone, so the pin fails
    only with both undone. slay calls raw_kill directly (do_slay, act_offe.cpp), with no roll.
    """
    implementor = server.login(IMPLEMENTOR.name)
    finish_the_foremans_script(implementor)

    reply = implementor.command(f"slay {WEARY_FOREMAN.keyword}")
    assert reply.contains(SLAY_MARKER), reply.text
    assert reply.contains(death_cry(WEARY_FOREMAN.short_description)), reply.text
    assert LAST_LINE not in reply.text, reply.text


@pytest.mark.scripts_delays
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR)
@placed(PATIENT_FOREMAN)
def test_slaying_a_mob_mid_wait_runs_the_rest_of_its_script_before_it_dies(server) -> None:
    """Killing a mob whose script is still waiting runs the rest of the script as it dies.

    The positive control for the slay pin above: raw_kill (fight.cpp) completes the pending
    delay, so continue_char_script says the patient foreman's last line between the slay
    message and the death cry. slay calls raw_kill directly (do_slay, act_offe.cpp), with no
    roll.
    """
    implementor = server.login(IMPLEMENTOR.name)
    start_the_patient_foremans_wait(implementor)

    reply = implementor.command(f"slay {PATIENT_FOREMAN.keyword}").text
    last_line = said(PATIENT_FOREMAN.short_description, LAST_LINE)
    cry = death_cry(PATIENT_FOREMAN.short_description)
    assert SLAY_MARKER in reply and last_line in reply and cry in reply, reply
    assert reply.index(SLAY_MARKER) < reply.index(last_line) < reply.index(cry), reply


@dataclass(frozen=True)
class ReportedLine:
    """A mob whose ON_RECEIVE script reaches a line that would crash; the game reports and skips it."""

    # The mob, whose script ends by saying STILL_HERE_LINE.
    mob: ScriptMob
    # The report mudlog writes when the line runs (script.cpp).
    report: str
    # How many times the boot check (check_script_table) has already written the same report.
    boot_reports: int


# Script 1704: CHANGE_EXIT_TO on the up exit of room 1101, which has no exits
# (report_script_bad_exit). Script 1705: TELEPORT_CHAR_XL to rm1, which is unset, after a line
# that leaves tmpint at room 1101's real number (report_script_no_room). Scripts 1706 and 1707:
# ASSIGN_EQ and DO_REMOVE on slot 22, outside 0 to MAX_WEAR - 1 (report_script_range), which the
# boot check also reports.
SURVEYOR_LINE = ReportedLine(
    ScriptMob(vnum=1704, keyword="surveyor", short_description="a surveyor"),
    "SCRIPT ERROR: script #1704, line 2 (change exit to): room 1101 has no exit 4",
    0,
)
# The mobs whose scripts reach the reported lines below, each running the script of its own number.
COURIER = ScriptMob(vnum=1705, keyword="courier", short_description="a courier")
VALET = ScriptMob(vnum=1706, keyword="valet", short_description="a valet")
DRESSER = ScriptMob(vnum=1707, keyword="dresser", short_description="a dresser")
REPORTED_LINES = [
    pytest.param(SURVEYOR_LINE, id="change-exit-to-a-missing-exit", marks=placed(SURVEYOR_LINE.mob)),
    pytest.param(
        ReportedLine(
            COURIER,
            "SCRIPT ERROR: script #1705, line 3 (teleport xl): room not found",
            0,
        ),
        marks=placed(COURIER), id="teleport-xl-to-an-unset-room",
    ),
    pytest.param(
        ReportedLine(
            VALET,
            "SCRIPT ERROR: script #1706, line 2 (assign eq): slot 22 is not 0-21",
            1,
        ),
        marks=placed(VALET), id="assign-eq-slot-22",
    ),
    pytest.param(
        ReportedLine(
            DRESSER,
            "SCRIPT ERROR: script #1707, line 2 (do remove): slot 22 is not 0-21",
            1,
        ),
        marks=placed(DRESSER), id="do-remove-slot-22",
    ),
]
# A level-20 human warrior in the implementor's room, who must not read script reports.
ROOM_WATCHER = Character(
    name="Testwatcher",
    race="human",
    sex="male",
    level=20,
    load_room=world.ROOM_IMMORTAL_START,
    class_name="warrior",
)


@pytest.mark.scripts_reports
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR)
@pytest.mark.parametrize("case", REPORTED_LINES)
def test_a_script_line_that_would_crash_is_reported_and_skipped(server, case: ReportedLine) -> None:
    """A script line that used to crash the server writes one report each time it runs, and the script goes on.

    Pins #312's crash guards in run_script (script.cpp): each case's line is skipped with a
    SCRIPT ERROR report in game.log and the script's last line still runs, on two gives in a
    row. Nothing here draws a random number.
    """
    implementor = server.login(IMPLEMENTOR.name)
    still_here = said(case.mob.short_description, STILL_HERE_LINE)

    for gives in (1, 2):
        reply = give_new_object(implementor, PEBBLE_VNUM, "pebble", case.mob)
        assert still_here in reply, reply
        assert count_logged(server, case.report, case.boot_reports + gives) == case.boot_reports + gives


@pytest.mark.scripts_reports
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, ROOM_WATCHER)
def test_a_script_report_reaches_an_immortal_with_syslog_on_and_no_mortal(server) -> None:
    """A runtime script report reaches an immortal whose syslog is on, before the script's next line, and no mortal.

    Pins how #312's reports reach people: mudlog (utility.cpp) sends "[ <report> ]" to a
    character of LEVEL_AREAGOD or above whose syslog level is at least the report's (NRM), and
    to nobody else in the game. Nothing here draws a random number.
    """
    implementor = server.login(IMPLEMENTOR.name)
    watcher = server.login(ROOM_WATCHER.name)
    syslog = implementor.command("syslog normal")
    assert syslog.contains("Your syslog is now normal."), syslog.text
    load_mob(implementor, SURVEYOR_LINE.mob)
    assert implementor.command(f"load obj {PEBBLE_VNUM}").contains("You create")
    watcher.drain(SETTLE_SECONDS)

    # The report line ends in "]", which command() would take for the prompt.
    implementor.send_line(f"give pebble {SURVEYOR_LINE.mob.keyword}")
    still_here = said(SURVEYOR_LINE.mob.short_description, STILL_HERE_LINE)
    reply = implementor.expect([still_here]) + implementor.drain(SETTLE_SECONDS)
    report_line = f"[ {SURVEYOR_LINE.report} ]"
    assert report_line in reply, reply
    assert reply.index(report_line) < reply.index(still_here), reply

    watched = watcher.expect([still_here]) + watcher.drain(SETTLE_SECONDS)
    assert "SCRIPT ERROR" not in watched, watched


# The appraiser (mob 1708), whose script ASSIGN_EQs from a character its trigger leaves unset.
APPRAISER = ScriptMob(vnum=1708, keyword="appraiser", short_description="an appraiser")


@pytest.mark.scripts_reports
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR)
@placed(APPRAISER)
def test_assign_eq_on_an_unset_character_finds_no_object(server) -> None:
    """ASSIGN_EQ on an unset character finds nothing, even after another line looked up an object.

    Pins #312's ASSIGN_EQ (run_script, script.cpp), which clears its object before reading the
    slot: the appraiser loads a cap, then reads the head slot of ch3, which is unset. Keeping
    the cap would set int1 and say "Phantom."; finding nothing says "Nothing.". Nothing here
    draws a random number.
    """
    implementor = server.login(IMPLEMENTOR.name)

    reply = give_new_object(implementor, PEBBLE_VNUM, "pebble", APPRAISER)
    assert said(APPRAISER.short_description, "Nothing.") in reply, reply
    assert "Phantom." not in reply, reply
    assert said(APPRAISER.short_description, STILL_HERE_LINE) in reply, reply


# The mumbler (mob 1709), whose script has a false IF whose nested block never ends.
MUMBLER = ScriptMob(vnum=1709, keyword="mumbler", short_description="a mumbler")


@pytest.mark.scripts_control_flow
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR)
@placed(MUMBLER)
def test_skipping_a_false_if_whose_nested_block_never_ends_ends_the_script(server) -> None:
    """A false IF whose nested block never reaches an END ends the script instead of crashing it.

    Pins #368's guard in get_next_command (script.cpp): when the nested block runs off the end
    of the script, the scan stops there rather than stepping past the end. The mumbler speaks
    first, since nothing can follow an unterminated block; a second give shows the server and
    the mob still answer. Nothing here draws a random number.
    """
    implementor = server.login(IMPLEMENTOR.name)

    for give_number in (1, 2):
        reply = give_new_object(implementor, PEBBLE_VNUM, "pebble", MUMBLER)
        assert reply.count(said(MUMBLER.short_description, STILL_HERE_LINE)) == 1, (give_number, reply)
        assert "Never said." not in reply, (give_number, reply)


@dataclass(frozen=True)
class UnsetVariableLine:
    """A mob whose ON_HEAR_SAY script runs one line on a variable a hear trigger leaves unset."""

    # The mob, whose script then says STILL_HERE_LINE.
    mob: ScriptMob
    # What game.log records for the line, if anything.
    log_message: Optional[str]


# Script 1710: ASSIGN_STR to ch1.name, which get_text_param_writable refuses. Script 1711:
# DO_SOCIAL performed by ch3, which is unset. Script 1712: DO_WEAR of ob1, which a hear trigger
# leaves unset. Script 1713: LOAD_OBJ_X, which copies ob1.
# The mobs whose scripts run the unset-variable lines below, each running the script of its own number.
RENAMER = ScriptMob(vnum=1710, keyword="renamer", short_description="a renamer")
SMILER = ScriptMob(vnum=1711, keyword="smiler", short_description="a smiler")
FITTER = ScriptMob(vnum=1712, keyword="fitter", short_description="a fitter")
COPIER = ScriptMob(vnum=1713, keyword="copier", short_description="a copier")
UNSET_VARIABLE_LINES = [
    pytest.param(
        UnsetVariableLine(
            RENAMER,
            "Script #1710 attempted to write to string variable 101.",
        ),
        marks=placed(RENAMER), id="assign-str-to-ch1-name",
    ),
    pytest.param(
        UnsetVariableLine(SMILER, None),
        marks=placed(SMILER), id="do-social-by-unset-ch3",
    ),
    pytest.param(
        UnsetVariableLine(FITTER, None),
        marks=placed(FITTER), id="do-wear-of-unset-ob1",
    ),
    pytest.param(
        UnsetVariableLine(COPIER, None),
        marks=placed(COPIER), id="load-obj-x-of-unset-ob1",
    ),
]


@pytest.mark.scripts_reports
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR)
@pytest.mark.parametrize("case", UNSET_VARIABLE_LINES)
def test_a_script_line_on_an_unset_variable_is_skipped(server, case: UnsetVariableLine) -> None:
    """A script line whose variable is unset, or cannot be written, does nothing and the script goes on.

    Pins #318's guards in run_script (script.cpp), each of which used to crash the server. Only
    the write to ch1.name is logged. Only one talking ON_HEAR_SAY mob is ever in the room: two
    would answer each other without end. Nothing here draws a random number.
    """
    implementor = server.login(IMPLEMENTOR.name)

    reply = implementor.command("say Hello there.").text
    assert reply.count(said(case.mob.short_description, STILL_HERE_LINE)) == 1, reply
    if case.log_message is not None:
        assert count_logged(server, case.log_message, 1) == 1


# The hatter (mob 1714), whose script wears an object it carries.
HATTER = ScriptMob(vnum=1714, keyword="hatter", short_description="a hatter")


@pytest.mark.scripts_reports
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR)
@placed(HATTER)
def test_do_wear_wears_an_object_the_mob_carries(server) -> None:
    """DO_WEAR on set variables wears the object: the control for the unset-object case above.

    The hatter's ON_RECEIVE script wears the leather cap it was given (perform_wear,
    act_obj2.cpp). Nothing here draws a random number.
    """
    implementor = server.login(IMPLEMENTOR.name)

    reply = give_new_object(implementor, LEATHER_CAP_VNUM, "cap", HATTER)
    assert "A hatter wears a leather cap on his head." in reply, reply


# The scribe (mob 1715), whose script copies the object it is given with LOAD_OBJ_X.
SCRIBE = ScriptMob(vnum=1715, keyword="scribe", short_description="a scribe")


@pytest.mark.scripts_reports
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR)
@placed(SCRIBE)
def test_load_obj_x_copies_the_given_object(server) -> None:
    """LOAD_OBJ_X with ob1 set copies it: the control for the unset-object case above.

    The scribe's ON_RECEIVE script copies the leather cap it was given into ob2, takes the copy
    and gives it to the giver (DO_GIVE, perform_give in act_obj1.cpp). Nothing here draws a
    random number.
    """
    implementor = server.login(IMPLEMENTOR.name)

    reply = give_new_object(implementor, LEATHER_CAP_VNUM, "cap", SCRIBE)
    assert "A scribe gives you a leather cap." in reply, reply


# The limit a script's write to hit points is clamped to (MAX_SCRIPT_HIT_VALUE and
# MIN_SCRIPT_HIT_VALUE, script.cpp).
SCRIPT_HIT_LIMIT = 1000000
# Every 1700s mob's constitution (mob/17.mob): the update_pos margins below rest on it.
MOB_CONSTITUTION = 10
# The fast updates in one game hour (FAST_UPDATE_RATE, structs.h).
FAST_UPDATES_PER_HOUR = 20


# The hale wrestler (mob 1716), whose script writes hit points above its maximum.
HALE = ScriptMob(vnum=1716, keyword="hale", short_description="a hale wrestler")


@pytest.mark.scripts_actions
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR)
@placed(HALE)
def test_a_script_hit_write_above_the_limit_is_clamped_and_reads_healthy(server) -> None:
    """A script writing 5000000 to its mob's hit points stores 1000000, which look reports as healthy.

    Pins #272: set_int_value (script.cpp) clamps a write to chN.hit, and report_char_health
    (act_info.cpp) stops its scan at the last of health_diagnose's eight entries (consts.cpp)
    instead of running past it on a percentage far above 1000. Nothing here draws a random
    number.
    """
    implementor = server.login(IMPLEMENTOR.name)
    give_new_object(implementor, PEBBLE_VNUM, "pebble", HALE)

    stat = read_mob_stat(implementor, HALE)
    assert int(stat_value(HIT_POINTS_PATTERN, stat)) == SCRIPT_HIT_LIMIT, stat
    looked = implementor.command(f"look {HALE.keyword}").text
    assert "A hale wrestler looks healthy." in looked, looked


# The dazed wrestler (mob 1717), whose script writes zero hit points, then more.
DAZED = ScriptMob(vnum=1717, keyword="dazed", short_description="a dazed wrestler")


@pytest.mark.scripts_actions
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR)
@placed(DAZED)
def test_a_script_hit_write_of_zero_stuns_and_a_later_write_stands_the_mob_up(server) -> None:
    """A script writing 0 to its mob's hit points stuns it; writing 50 afterwards stands it up.

    Pins #272's position resync in set_int_value (script.cpp): a write that leaves hit points at
    or below 0 calls update_pos (fight.cpp), and so does a later write while the mob is still
    stunned, incapacitated or dead. Nothing here draws a random number.
    """
    implementor = server.login(IMPLEMENTOR.name)
    give_new_object(implementor, PEBBLE_VNUM, "pebble", DAZED)

    stat = read_mob_stat(implementor, DAZED)
    # update_pos (fight.cpp) for the dazed wrestler, with hit 0 and Con from stat:
    #   GET_HIT > 0                  : 0 > 0                      is false
    #   GET_HIT <= -GET_CON / 2      : 0 <= -10 / 2 = -5          is false
    #   GET_HIT <= -GET_CON / 4      : 0 <= -10 / 4 = -2          is false
    #   so POSITION_STUNNED.
    #   (HP :[0/...], Con: [10/...])
    assert int(stat_value(CONSTITUTION_PATTERN, stat)) == MOB_CONSTITUTION, stat
    assert int(stat_value(HIT_POINTS_PATTERN, stat)) == 0, stat
    assert stat_value(POSITION_PATTERN, stat) == "Stunned", stat
    looked = implementor.command("look").text
    assert "A dazed wrestler is lying here, stunned." in looked, looked

    implementor.command("say Wake up.")
    stat = read_mob_stat(implementor, DAZED)
    assert int(stat_value(HIT_POINTS_PATTERN, stat)) == 50, stat
    assert stat_value(POSITION_PATTERN, stat) == "Standing", stat


# The doomed wrestler (mob 1718), whose script writes hit points far below zero.
DOOMED = ScriptMob(vnum=1718, keyword="doomed", short_description="a doomed wrestler")


@pytest.mark.scripts_actions
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR)
@placed(DOOMED)
def test_a_script_hit_write_far_below_zero_leaves_the_mob_dead_until_the_hourly_sweep(server) -> None:
    """A script writing -5000000 to its mob's hit points leaves it dead, and the next game hour kills it.

    Pins #272: the write is clamped to -1000000 and update_pos sets POSITION_DEAD; point_update
    (limits.cpp) then finds the dead mob in its hourly sweep and calls die(), whose death cry
    the implementor hears. Nothing here draws a random number that decides the outcome; the
    hour's regeneration is bounded below.
    """
    implementor = server.login(IMPLEMENTOR.name)
    give_new_object(implementor, PEBBLE_VNUM, "pebble", DOOMED)

    stat = read_mob_stat(implementor, DOOMED)
    hit_match = HIT_POINTS_PATTERN.search(stat)
    assert hit_match is not None, stat
    hit, hourly_gain = int(hit_match.group(1)), int(hit_match.group(3))
    # update_pos (fight.cpp) for the doomed wrestler, with hit -1000000 and Con from stat:
    #   GET_HIT > 0                  : -1000000 > 0               is false
    #   GET_HIT <= -GET_CON / 2      : -1000000 <= -10 / 2 = -5   is true
    #   so POSITION_DEAD.
    # point_update's sweep calls update_pos again after the hour's FAST_UPDATE_RATE (20,
    # structs.h) fast updates, each adding int(gain / 20) plus at most 1 (fast_update,
    # limits.cpp), with the hourly gain from stat (HP :[-1000000/60+11(0)]):
    #   -1000000 + 20 * (int(11 / 20) + 1) = -1000000 + 20 = -999980 <= -10 / 2 = -5,
    #   still POSITION_DEAD.
    assert int(stat_value(CONSTITUTION_PATTERN, stat)) == MOB_CONSTITUTION, stat
    assert hit == -SCRIPT_HIT_LIMIT, stat
    assert hit + FAST_UPDATES_PER_HOUR * (hourly_gain // FAST_UPDATES_PER_HOUR + 1) <= -MOB_CONSTITUTION // 2, stat
    assert stat_value(POSITION_PATTERN, stat) == "Dead", stat
    looked = implementor.command("look").text
    assert "A doomed wrestler is lying here, dead." in looked, looked

    # advance_hours returns once the hour has run; the drain collects what the hour sent.
    server.advance_hours(1)
    hour = implementor.drain(SETTLE_SECONDS)
    assert hour.count(death_cry(DOOMED.short_description)) == 1, hour
    looked = implementor.command("look").text
    assert "A doomed wrestler" not in looked, looked


# The accountant (mob 1719), whose script divides by zero.
ACCOUNTANT = ScriptMob(vnum=1719, keyword="accountant", short_description="an accountant")


@pytest.mark.scripts_reports
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR)
@placed(ACCOUNTANT)
def test_a_script_division_by_zero_gives_zero_and_is_logged(server) -> None:
    """SET_INT_DIV by zero stores 0 and logs the attempt, instead of drawing a random number.

    Pins #272's int_binary_op (script.cpp): dividing by 0 returns 0 rather than falling through
    into SET_INT_RANDOM. The accountant's int1 held 99 before the division, so a skipped write
    shows too; a fall-through would draw number(0, 1000000), 0 once in 1000001 draws.
    """
    implementor = server.login(IMPLEMENTOR.name)

    reply = give_new_object(implementor, PEBBLE_VNUM, "pebble", ACCOUNTANT)
    assert said(ACCOUNTANT.short_description, "Quotient zero.") in reply, reply
    assert "Quotient not zero." not in reply, reply
    assert count_logged(server, "Script #1719 tried to divide by 0.", 1) == 1


# The gatekeeper (mob 1703), whose script opens the door north of his room when given anything
# and closes and locks it when he hears "lock".
GATEKEEPER = ScriptMob(vnum=1703, keyword="gatekeeper", short_description="a gatekeeper")
# A level-20 human warrior on the far side of the gatekeeper's door.
GATE_WATCHER = Character(
    name="Testgateward",
    race="human",
    sex="male",
    level=20,
    load_room=ROOM_BEYOND_THE_GATE,
    class_name="warrior",
)


@pytest.mark.scripts_actions
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, GATE_WATCHER)
def test_a_script_opens_and_locks_a_north_door_from_both_sides(server) -> None:
    """SET_EXIT_STATE works on a north door, and changes the door's other side with it.

    Pins #312: run_script's SET_EXIT_STATE (script.cpp) checks the direction's range instead
    of testing it for non-zero, which made north (0) impossible. set_exit_state (db.cpp) tells
    each side in its own words: the yard's exit is keyword "door", the far side's "gate".
    Nothing here draws a random number.
    """
    implementor = server.login(IMPLEMENTOR.name)
    watcher = server.login(GATE_WATCHER.name)
    assert implementor.command(f"goto {ROOM_GATEHOUSE_YARD}").contains("Gatehouse Yard")
    load_mob(implementor, GATEKEEPER)
    watcher.drain(SETTLE_SECONDS)

    opened = give_new_object(implementor, PEBBLE_VNUM, "pebble", GATEKEEPER)
    assert "Door opens quietly." in opened, opened
    watched = watcher.expect(["Gate opens quietly."]) + watcher.drain(SETTLE_SECONDS)
    assert watched.count("Gate opens quietly.") == 1, watched
    assert implementor.command("north").contains("Beyond the Gate")
    assert implementor.command("south").contains("Gatehouse Yard")
    watcher.drain(SETTLE_SECONDS)

    locked = implementor.command("say lock").text
    lock_sound = "You hear a sound of a lock snapping shut."
    assert "The door closes quietly." in locked and lock_sound in locked, locked
    watched = watcher.expect([lock_sound]) + watcher.drain(SETTLE_SECONDS)
    assert "The gate closes quietly." in watched and lock_sound in watched, watched
    blocked = implementor.command("north")
    assert blocked.contains("The door seems to be closed."), blocked.text
    blocked = watcher.command("south")
    assert blocked.contains("The gate seems to be closed."), blocked.text


# What the boot check (check_script_table, script.cpp) writes for script 1720, which no mob
# uses: one report per line, each naming the line by the number stored with it. The last line
# is stored as 40 although it is the script's thirteenth row.
BOOT_REPORTS = [
    "SCRIPT ERROR: script #1720, line 2 (load mob): mobile vnum 1799 not found",
    "SCRIPT ERROR: script #1720, line 3 (load obj): object vnum 1799 not found",
    "SCRIPT ERROR: script #1720, line 4 (assign inv): object vnum 1799 not found",
    "SCRIPT ERROR: script #1720, line 5 (assign room): object vnum 1799 not found",
    "SCRIPT ERROR: script #1720, line 6 (teleport): room vnum 1799 not found",
    "SCRIPT ERROR: script #1720, line 7 (teleport x): room vnum 1799 not found",
    "SCRIPT ERROR: script #1720, line 8 (change exit to): room vnum 1799 not found",
    "SCRIPT ERROR: script #1720, line 9 (equip char): object vnum 1799 not found",
    "SCRIPT ERROR: script #1720, line 10 (assign eq): slot 22 is not 0-21",
    "SCRIPT ERROR: script #1720, line 11 (do remove): slot -1 is not 0-21",
    "SCRIPT ERROR: script #1720, line 12 (set exit state): direction 6 is not 0-5",
    "SCRIPT ERROR: script #1720, line 40 (change exit to): direction 7 is not 0-5",
]


@pytest.mark.scripts_reports
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR)
def test_the_boot_check_reports_every_script_line_naming_something_missing(server) -> None:
    """At boot, each script line naming a missing vnum, slot or direction is reported once, by its stored line number.

    Pins #312's check_script_table (script.cpp), which runs once rooms, mobs and objects are
    loaded (boot_db, db.cpp). Script 1720 never runs, so every report comes from the boot
    check. Nothing here draws a random number.
    """
    server.login(IMPLEMENTOR.name)

    for report in BOOT_REPORTS:
        assert count_logged(server, report, 1) == 1, report
    script_reports = [message for message in log_messages(server) if message.startswith("SCRIPT ERROR: script #1720,")]
    assert script_reports == BOOT_REPORTS, script_reports


# The wanderer (mob 1723), whose script teleports himself to room 1201 when he hears speech,
# and the listener (mob 1724), whose script answers it.
WANDERER = ScriptMob(vnum=1723, keyword="wanderer", short_description="a wanderer")
LISTENER = ScriptMob(vnum=1724, keyword="listener", short_description="a listener")
LISTENER_LINE = "I heard that."


@pytest.mark.scripts_actions
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR)
@placed(LISTENER, WANDERER)
def test_a_listener_ahead_of_a_mob_that_leaves_on_speech_still_answers(server) -> None:
    """Every mob in the room hears a say, when the one that leaves comes after the listener.

    do_say (act_comm.cpp) runs each mob's ON_HEAR_SAY trigger in room order. Loaded first, the
    listener hears the implementor and answers; the wanderer, which hears that answer too,
    then leaves for room 1201. Nothing here draws a random number.
    """
    implementor = server.login(IMPLEMENTOR.name)

    reply = implementor.command("say Anyone there?").text
    assert reply.count(said(LISTENER.short_description, LISTENER_LINE)) == 1, reply
    looked = implementor.command("look").text
    assert "A listener stands here" in looked and "A wanderer" not in looked, looked
