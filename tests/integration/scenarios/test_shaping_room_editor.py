"""The room editor: room affections, room flags and /implement.

Affections are added, changed and removed and reach the room on /implement, a room's flags survive
/implement, and a flag answer with several flags says where it stopped.
"""

from __future__ import annotations

import pytest

from rots_harness import world
from rots_harness.characters import IMPLEMENTOR, Character
from rots_harness.immortal_commands import goto
from rots_harness.replies import reply_to
from rots_harness.session import GameSession
from rots_harness.shaping import (
    COULD_NOT_OPEN_FILE_REPLY,
    EDITOR_SETTLE_SECONDS,
    IMMORTAL_START_ROOM_PROMPT,
    ROOM_IMPLEMENTED_REPLY,
    ROOM_RELEASED_REPLY,
    release_editor,
)
from rots_harness.stat_reading import OBJECT_STAT_LAST_LINE, read_stat
from rots_harness.text_lines import raw_lines

pytestmark = [pytest.mark.scenario, pytest.mark.shaping, pytest.mark.shaping_room_editor]

# The room the implementor arrives in and shapes; it has no flags, so it boots with no affection.
ROOM_VNUM = world.ROOM_IMMORTAL_START
# What shape room prints once it has read the room (load_room, shaperom.cpp); format with the
# room's number.
ROOM_LOADED_FORMAT = "loading room #{}"
# What shape room prints when it cannot read the room.
ROOM_LOAD_FAILURES = [COULD_NOT_OPEN_FILE_REPLY, "no room here."]
# What /19 prints after adding an affection to the editor's copy of the room.
AFFECTION_ADDED_REPLY = "A new affection added."
# The /18 prompt's line with the first affection's values: a new affection is a spell affection
# (type 1) naming no spell (SPELL_NONE, 127; spells.h).
NEW_AFFECTION_VALUES = "Current: 1 127 0 0"
# What /18 prints when the room has no affection to change.
NO_AFFECTIONS_FOUND_REPLY = "No room affections found."
# The spell the scenario gives the affection: blink (SPELL_BLINK, spells.h), which no room code
# reacts to.
SPELL_NUMBER = 76
# SPELL_NUMBER's name, as /50 and stat room show it.
SPELL_NAME = "blink"
# The level the scenario gives the affection.
SPELL_LEVEL = 30
# The /18 answer: a spell affection of SPELL_NUMBER at SPELL_LEVEL, setting no room flag.
AFFECTION_ANSWER = f"1 {SPELL_NUMBER} {SPELL_LEVEL} 0"
# How /50 shows the changed affection (list_room, shaperom.cpp).
AFFECTION_LISTED = f"(18) Affection type (1), ({SPELL_NAME})-{SPELL_NUMBER}, level {SPELL_LEVEL}, flags 0"
# What /20 prints after removing the first affection.
AFFECTION_REMOVED_REPLY = "Affection removed."
# What /20 prints when the room has no affection left to remove.
NO_AFFECTIONS_EXIST_REPLY = "No affections exist on this room."
# The start of stat room's affection line for the implemented spell, which then names its
# hours and flags (show_room_affection, act_info.cpp).
STAT_AFFECTION_LINE_START = f" Spell {SPELL_NAME}({SPELL_NUMBER}) level {SPELL_LEVEL}, -1hrs"
# Stat room's affection line for a room with none (do_stat_room, act_wiz.cpp).
STAT_NO_AFFECTIONS = "Affections: None."
# The /3 room-flag prompt with the room's flags, 0 (DIGITCHANGEL, shaperom.cpp).
ROOM_FLAGS_PROMPT = "Enter room flags [0]:"
# An answer that sets SHADOWY (bit 6), then stops at a word that is not a flag, so bit 7
# (NO_MAGIC) after it is not set.
MULTI_FLAG_ANSWER = "p6 pause p7"
# What shape_flag_value (shapemob.cpp) prints for MULTI_FLAG_ANSWER.
MULTI_FLAG_STOPPED_REPLY = "Stopped at 'pause': it and the rest were not applied."
# How /50 shows the room's flags afterwards: SHADOWY alone, 1 << 6.
SHADOWY_FLAGS_LISTED = "(3) room flag  :64"
# What look shows a character who cannot see the room (do_look, act_info.cpp).
PITCH_BLACK = "It is pitch black..."
# The dark cell's flags as stat room names them (do_stat_room, act_wiz.cpp).
DARK_FLAGS_LINE = "SpecProc: No, Flags:  DARK."
# The blink affection again, now also setting room flag bit 0, DARK (structs.h).
DARKENING_AFFECTION_ANSWER = f"1 {SPELL_NUMBER} {SPELL_LEVEL} 1"


# What /implement prints in place of ROOM_IMPLEMENTED_REPLY when it refuses (implement_room,
# shaperom.cpp).
IMPLEMENT_REFUSALS = ["You may not do that in this zone.", "You have nothing to implement"]


def mortal_in(room_vnum: int) -> Character:
    """Return a level-10 human warrior starting in room_vnum.

    The warrior is a mortal without infravision, who sees only in a lit room.
    """
    return Character(
        name="Testlooker", race="human", sex="male", level=10, load_room=room_vnum, class_name="warrior"
    )


# The mortal in the dark cell.
MORTAL_IN_DARK_CELL = mortal_in(world.ROOM_DARK_CELL)
# The mortal in the implementor's room.
MORTAL_IN_ROOM = mortal_in(ROOM_VNUM)


def open_room_editor(implementor: GameSession, room_vnum: int = ROOM_VNUM) -> None:
    """Start shaping a room, the implementor's unless another is named."""
    loaded_reply = ROOM_LOADED_FORMAT.format(room_vnum)
    outcomes = [loaded_reply, *ROOM_LOAD_FAILURES]
    loaded = reply_to(implementor, f"shape room {room_vnum}", outcomes, settle=EDITOR_SETTLE_SECONDS)
    assert loaded_reply in loaded, loaded


def look(mortal: GameSession) -> str | None:
    """Return the first line of what the mortal's look shows: the room's name, or PITCH_BLACK."""
    looked = mortal.command("look")
    return looked.room_name()


def stat_room_affections(implementor: GameSession) -> list[str]:
    """Return the lines of stat room from its affection heading on, for the implementor's room."""
    text = read_stat(implementor, "room", last_line=OBJECT_STAT_LAST_LINE, settle=EDITOR_SETTLE_SECONDS)
    stat_lines = raw_lines(text)
    heading = next(index for index, line in enumerate(stat_lines) if line.startswith(OBJECT_STAT_LAST_LINE))
    return stat_lines[heading:]


def add_blink_affection(implementor: GameSession) -> None:
    """Add an affection in the editor, set it to blink at level 30 and check the editor lists it."""
    added_outcomes = [AFFECTION_ADDED_REPLY, IMMORTAL_START_ROOM_PROMPT]
    added = reply_to(implementor, "/19", added_outcomes, settle=EDITOR_SETTLE_SECONDS)
    assert AFFECTION_ADDED_REPLY in added, added
    prompt_outcomes = [NEW_AFFECTION_VALUES, "Current:", NO_AFFECTIONS_FOUND_REPLY]
    prompt = reply_to(implementor, "/18", prompt_outcomes, settle=EDITOR_SETTLE_SECONDS)
    assert NEW_AFFECTION_VALUES in raw_lines(prompt), prompt
    implementor.send_line(AFFECTION_ANSWER)
    listed = reply_to(implementor, "/50", ["(18) "], settle=EDITOR_SETTLE_SECONDS)
    assert AFFECTION_LISTED in raw_lines(listed), listed


def implement_room(implementor: GameSession) -> None:
    """Run /implement and check it reports the room implemented."""
    outcomes = [ROOM_IMPLEMENTED_REPLY, *IMPLEMENT_REFUSALS]
    implemented = reply_to(implementor, "/implement", outcomes, settle=EDITOR_SETTLE_SECONDS)
    assert ROOM_IMPLEMENTED_REPLY in implemented, implemented


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR)
def test_a_room_affection_added_and_set_in_the_editor_shows_in_stat_room(server) -> None:
    """Pins that an affection added and set in the room editor reaches stat room on /implement.

    /19 adds a spell affection naming no spell, /18 sets its spell and level, and /implement puts
    it on the room, where stat room shows it.
    """
    implementor = server.login(IMPLEMENTOR.name)
    booted_affections = stat_room_affections(implementor)
    assert booted_affections[0] == STAT_NO_AFFECTIONS, booted_affections

    open_room_editor(implementor)
    add_blink_affection(implementor)
    implement_room(implementor)
    release_editor(implementor, ROOM_RELEASED_REPLY, settle=EDITOR_SETTLE_SECONDS)

    affections = stat_room_affections(implementor)
    assert affections[0] == "Affections:", affections
    assert affections[1].startswith(STAT_AFFECTION_LINE_START), affections


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR)
def test_a_room_affection_removed_in_the_editor_leaves_the_room_on_implement(server) -> None:
    """Pins that an affection removed in the room editor leaves the room on /implement.

    /20 removes the affection, a second /20 finds none left, and implementing again takes it off
    the room, so stat room shows none.
    """
    implementor = server.login(IMPLEMENTOR.name)

    open_room_editor(implementor)
    add_blink_affection(implementor)
    implement_room(implementor)
    implemented_affections = stat_room_affections(implementor)
    assert implemented_affections[1].startswith(STAT_AFFECTION_LINE_START), implemented_affections
    removal_outcomes = [AFFECTION_REMOVED_REPLY, NO_AFFECTIONS_EXIST_REPLY]
    removed = reply_to(implementor, "/20", removal_outcomes, settle=EDITOR_SETTLE_SECONDS)
    removed_again = reply_to(implementor, "/20", removal_outcomes, settle=EDITOR_SETTLE_SECONDS)
    implement_room(implementor)
    release_editor(implementor, ROOM_RELEASED_REPLY, settle=EDITOR_SETTLE_SECONDS)

    assert AFFECTION_REMOVED_REPLY in raw_lines(removed), removed
    assert NO_AFFECTIONS_EXIST_REPLY in raw_lines(removed_again), removed_again
    affections = stat_room_affections(implementor)
    assert affections[0] == STAT_NO_AFFECTIONS, affections


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR)
def test_a_flag_answer_stops_at_the_first_word_that_is_not_a_flag(server) -> None:
    """Pins that a flag answer stops at the first word that is not a flag, and names it.

    A flag prompt takes several pN/mN answers and applies those before the first word it cannot
    read.
    """
    implementor = server.login(IMPLEMENTOR.name)

    open_room_editor(implementor)
    prompt_outcomes = [ROOM_FLAGS_PROMPT, IMMORTAL_START_ROOM_PROMPT]
    prompt = reply_to(implementor, "/3", prompt_outcomes, settle=EDITOR_SETTLE_SECONDS)
    assert ROOM_FLAGS_PROMPT in raw_lines(prompt), prompt
    answer_outcomes = [MULTI_FLAG_STOPPED_REPLY, IMMORTAL_START_ROOM_PROMPT]
    answered = reply_to(implementor, MULTI_FLAG_ANSWER, answer_outcomes, settle=EDITOR_SETTLE_SECONDS)
    listed = reply_to(implementor, "/50", ["(3) room flag"], settle=EDITOR_SETTLE_SECONDS)
    release_editor(implementor, ROOM_RELEASED_REPLY, settle=EDITOR_SETTLE_SECONDS)

    assert MULTI_FLAG_STOPPED_REPLY in raw_lines(answered), answered
    assert SHADOWY_FLAGS_LISTED in raw_lines(listed), listed


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, MORTAL_IN_DARK_CELL)
def test_implementing_a_dark_room_unchanged_leaves_it_dark(server) -> None:
    """Pins that /implement of the dark cell, unedited, keeps its DARK flag.

    A mortal there still sees nothing and stat room still shows the flag. /implement removes the
    room's affections, whose bits come off its flags, before it sets the flags again
    (implement_room, shaperom.cpp).
    """
    implementor = server.login(IMPLEMENTOR.name)
    mortal = server.login(MORTAL_IN_DARK_CELL.name)
    booted_look = look(mortal)

    open_room_editor(implementor, world.ROOM_DARK_CELL)
    implement_room(implementor)
    release_editor(implementor, ROOM_RELEASED_REPLY, settle=EDITOR_SETTLE_SECONDS)
    implemented_look = look(mortal)
    goto(implementor, world.ROOM_DARK_CELL)
    stat_text = read_stat(implementor, "room", last_line=OBJECT_STAT_LAST_LINE, settle=EDITOR_SETTLE_SECONDS)

    assert booted_look == PITCH_BLACK, booted_look
    assert implemented_look == PITCH_BLACK, implemented_look
    assert DARK_FLAGS_LINE in raw_lines(stat_text), stat_text


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, MORTAL_IN_ROOM)
def test_an_implemented_affections_room_flags_take_effect_and_leave_with_it(server) -> None:
    """Pins that an affection whose flags set DARK darkens the room once implemented.

    A mortal there sees nothing, and removing the affection and implementing again lights the
    room.
    """
    implementor = server.login(IMPLEMENTOR.name)
    mortal = server.login(MORTAL_IN_ROOM.name)
    lit_look = look(mortal)

    open_room_editor(implementor)
    added_outcomes = [AFFECTION_ADDED_REPLY, IMMORTAL_START_ROOM_PROMPT]
    added = reply_to(implementor, "/19", added_outcomes, settle=EDITOR_SETTLE_SECONDS)
    assert AFFECTION_ADDED_REPLY in added, added
    prompt_outcomes = [NEW_AFFECTION_VALUES, "Current:", NO_AFFECTIONS_FOUND_REPLY]
    prompt = reply_to(implementor, "/18", prompt_outcomes, settle=EDITOR_SETTLE_SECONDS)
    assert NEW_AFFECTION_VALUES in raw_lines(prompt), prompt
    reply_to(implementor, DARKENING_AFFECTION_ANSWER, [IMMORTAL_START_ROOM_PROMPT], settle=EDITOR_SETTLE_SECONDS)
    implement_room(implementor)
    darkened_look = look(mortal)
    removal_outcomes = [AFFECTION_REMOVED_REPLY, NO_AFFECTIONS_EXIST_REPLY]
    removed = reply_to(implementor, "/20", removal_outcomes, settle=EDITOR_SETTLE_SECONDS)
    assert AFFECTION_REMOVED_REPLY in raw_lines(removed), removed
    implement_room(implementor)
    release_editor(implementor, ROOM_RELEASED_REPLY, settle=EDITOR_SETTLE_SECONDS)
    relit_look = look(mortal)

    assert lit_look == world.ROOM_IMMORTAL_START_NAME, lit_look
    assert darkened_look == PITCH_BLACK, darkened_look
    assert relit_look == world.ROOM_IMMORTAL_START_NAME, relit_look
