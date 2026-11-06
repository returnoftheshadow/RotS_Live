"""The room editor: affections are added, changed and removed and reach the room on /implement,
a room's flags survive /implement, and a flag answer with several flags says where it stopped."""

from __future__ import annotations

import pytest

from rots_harness import world
from rots_harness.characters import IMPLEMENTOR, Character
from rots_harness.session import GameSession

pytestmark = [pytest.mark.scenario, pytest.mark.shaping, pytest.mark.shaping_room_editor]

# The room the implementor arrives in and shapes; it has no flags, so it boots with no affection.
ROOM_VNUM = world.ROOM_IMMORTAL_START
# The room editor's prompt while no field is being answered (prompt_text in consts.cpp).
ROOM_PROMPT = f"Shaping: {ROOM_VNUM}>"
# What shape room prints once it has read the room, and when it cannot (load_room, shaperom.cpp).
ROOM_LOADED_MARKER = f"loading room #{ROOM_VNUM}"
ROOM_LOAD_FAILURES = ["could not open that file.", "no room here."]
# What /free prints (extra_coms_room).
ROOM_RELEASED_MARKER = "You released the room and stopped shaping."
# What /19 prints after adding an affection to the editor's copy of the room.
AFFECTION_ADDED = "A new affection added."
# The /18 prompt's line with the first affection's values: a new affection is a spell affection
# (type 1) naming no spell (SPELL_NONE, 127; spells.h).
NEW_AFFECTION_VALUES = "Current: 1 127 0 0"
# What /18 prints when the room has no affection to change.
NO_AFFECTIONS_FOUND = "No room affections found."
# The spell, level and flags the scenario gives the affection: blink (SPELL_BLINK, spells.h),
# which no room code reacts to, at level 30, setting no room flag.
SPELL_NUMBER = 76
SPELL_NAME = "blink"
SPELL_LEVEL = 30
AFFECTION_ANSWER = f"1 {SPELL_NUMBER} {SPELL_LEVEL} 0"
# How /50 shows the changed affection (list_room, shaperom.cpp).
AFFECTION_LISTED = f"(18) Affection type (1), ({SPELL_NAME})-{SPELL_NUMBER}, level {SPELL_LEVEL}, flags 0"
# What /implement prints once the room in the world matches the editor's copy (implement_room).
ROOM_IMPLEMENTED = "Room implemented."
# What /20 prints after removing the first affection, and when there is none to remove.
AFFECTION_REMOVED = "Affection removed."
NO_AFFECTIONS_EXIST = "No affections exist on this room."
# The start of stat room's affection line for the implemented spell, which then names its
# hours and flags (show_room_affection, act_info.cpp); and its line for a room with none
# (do_stat_room, act_wiz.cpp).
STAT_AFFECTION_LINE_START = f" Spell {SPELL_NAME}({SPELL_NUMBER}) level {SPELL_LEVEL}, -1hrs"
STAT_NO_AFFECTIONS = "Affections: None."
# The /3 room-flag prompt with the room's flags, 0 (DIGITCHANGEL, shaperom.cpp).
ROOM_FLAGS_PROMPT = "Enter room flags [0]:"
# An answer that sets SHADOWY (bit 6), then stops at a word that is not a flag, so bit 7
# (NO_MAGIC) after it is not set.
MULTI_FLAG_ANSWER = "p6 pause p7"
# What shape_flag_value (shapemob.cpp) prints for MULTI_FLAG_ANSWER.
MULTI_FLAG_STOPPED = "Stopped at 'pause': it and the rest were not applied."
# How /50 shows the room's flags afterwards: SHADOWY alone, 1 << 6.
SHADOWY_FLAGS_LISTED = "(3) room flag  :64"
# What look shows a character who cannot see the room (do_look, act_info.cpp).
PITCH_BLACK = "It is pitch black..."
# The dark cell's name, and its flags as stat room names them (do_stat_room, act_wiz.cpp).
DARK_CELL_NAME = "Dark Cell"
DARK_FLAGS_LINE = "SpecProc: No, Flags:  DARK."
# The blink affection again, now also setting room flag bit 0, DARK (structs.h).
DARKENING_AFFECTION_ANSWER = f"1 {SPELL_NUMBER} {SPELL_LEVEL} 1"
# Seconds to keep reading after a marker, for the rest of the reply.
SETTLE_SECONDS = 0.5


def mortal_in(room_vnum: int) -> Character:
    """A level-10 human warrior starting in room_vnum: a mortal without infravision, who sees
    only in a lit room."""
    return Character(name="Testlooker", race="human", sex="male", level=10, load_room=room_vnum, class_name="warrior")


# The mortal in the dark cell, and the mortal in the implementor's room.
MORTAL_IN_DARK_CELL = mortal_in(world.ROOM_DARK_CELL)
MORTAL_IN_ROOM = mortal_in(ROOM_VNUM)


def lines_of(text: str) -> list:
    """The text's lines without the carriage returns the game sends with each line end."""
    return [line.strip("\r") for line in text.split("\n")]


def editor_step(implementor: GameSession, line: str, outcomes: list) -> str:
    """Sends one editor line and returns what came back, up to and just after the first outcome.

    Editor prompts end in ':' or ']' mid-field, so command() cannot be used inside the editor.
    """
    implementor.send_line(line)
    return implementor.expect(outcomes) + implementor.drain(SETTLE_SECONDS)


def open_room_editor(implementor: GameSession, room_vnum: int = ROOM_VNUM) -> None:
    """Starts shaping a room, the implementor's unless another is named."""
    marker = f"loading room #{room_vnum}"
    loaded = editor_step(implementor, f"shape room {room_vnum}", [marker, *ROOM_LOAD_FAILURES])
    assert marker in loaded, loaded


def close_room_editor(implementor: GameSession) -> None:
    """Leaves the editor, so the session can quit when the test ends."""
    released = editor_step(implementor, "/free", [ROOM_RELEASED_MARKER])
    assert ROOM_RELEASED_MARKER in released, released


def look(mortal: GameSession) -> str:
    """The first line of what the mortal's look shows: the room's name, or PITCH_BLACK."""
    return mortal.command("look").room_name()


def stat_room_affections(implementor: GameSession) -> list:
    """The lines of stat room from its affection heading on, for the implementor's room.

    stat room has lines ending in ']', so it is read with send_line, expect and drain.
    """
    implementor.send_line("stat room")
    text = implementor.expect(["Affections:"]) + implementor.drain(SETTLE_SECONDS)
    stat_lines = lines_of(text)
    heading = next(index for index, line in enumerate(stat_lines) if line.startswith("Affections:"))
    return stat_lines[heading:]


def add_blink_affection(implementor: GameSession) -> None:
    """Adds an affection in the editor, sets it to blink at level 30, and checks the editor's
    list shows it."""
    added = editor_step(implementor, "/19", [AFFECTION_ADDED, ROOM_PROMPT])
    assert AFFECTION_ADDED in added, added
    prompt = editor_step(implementor, "/18", [NEW_AFFECTION_VALUES, "Current:", NO_AFFECTIONS_FOUND])
    assert NEW_AFFECTION_VALUES in lines_of(prompt), prompt
    implementor.send_line(AFFECTION_ANSWER)
    listed = editor_step(implementor, "/50", ["(18) "])
    assert AFFECTION_LISTED in lines_of(listed), listed


def implement_room(implementor: GameSession) -> None:
    """Runs /implement and checks it reports the room implemented."""
    implemented = editor_step(implementor, "/implement", [ROOM_IMPLEMENTED, "You may not do that in this zone.", "You have nothing to implement"])
    assert ROOM_IMPLEMENTED in implemented, implemented


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR)
def test_a_room_affection_added_and_set_in_the_editor_shows_in_stat_room(server) -> None:
    """Pins #318: /19 adds a spell affection naming no spell, /18 sets its spell and level, and
    /implement puts it on the room, where stat room shows it."""
    implementor = server.login(IMPLEMENTOR.name)
    booted_affections = stat_room_affections(implementor)
    assert booted_affections[0] == STAT_NO_AFFECTIONS, booted_affections

    open_room_editor(implementor)
    add_blink_affection(implementor)
    implement_room(implementor)
    close_room_editor(implementor)

    affections = stat_room_affections(implementor)
    assert affections[0] == "Affections:", affections
    assert affections[1].startswith(STAT_AFFECTION_LINE_START), affections


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR)
def test_a_room_affection_removed_in_the_editor_leaves_the_room_on_implement(server) -> None:
    """Pins #318: /20 removes the affection, a second /20 finds none left, and implementing
    again takes it off the room, so stat room shows none."""
    implementor = server.login(IMPLEMENTOR.name)

    open_room_editor(implementor)
    add_blink_affection(implementor)
    implement_room(implementor)
    implemented_affections = stat_room_affections(implementor)
    assert implemented_affections[1].startswith(STAT_AFFECTION_LINE_START), implemented_affections
    removed = editor_step(implementor, "/20", [AFFECTION_REMOVED, NO_AFFECTIONS_EXIST])
    removed_again = editor_step(implementor, "/20", [AFFECTION_REMOVED, NO_AFFECTIONS_EXIST])
    implement_room(implementor)
    close_room_editor(implementor)

    assert AFFECTION_REMOVED in lines_of(removed), removed
    assert NO_AFFECTIONS_EXIST in lines_of(removed_again), removed_again
    affections = stat_room_affections(implementor)
    assert affections[0] == STAT_NO_AFFECTIONS, affections


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR)
def test_a_flag_answer_stops_at_the_first_word_that_is_not_a_flag(server) -> None:
    """Pins #341: a flag prompt takes several pN/mN answers, applies those before the first word
    it cannot read, and names that word."""
    implementor = server.login(IMPLEMENTOR.name)

    open_room_editor(implementor)
    prompt = editor_step(implementor, "/3", [ROOM_FLAGS_PROMPT, ROOM_PROMPT])
    assert ROOM_FLAGS_PROMPT in lines_of(prompt), prompt
    answered = editor_step(implementor, MULTI_FLAG_ANSWER, [MULTI_FLAG_STOPPED, ROOM_PROMPT])
    listed = editor_step(implementor, "/50", ["(3) room flag"])
    close_room_editor(implementor)

    assert MULTI_FLAG_STOPPED in lines_of(answered), answered
    assert SHADOWY_FLAGS_LISTED in lines_of(listed), listed


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, MORTAL_IN_DARK_CELL)
def test_implementing_a_dark_room_unchanged_leaves_it_dark(server) -> None:
    """Pins #318: /implement of the dark cell, unedited, keeps its DARK flag, so a mortal there
    still sees nothing and stat room still shows the flag.

    /implement removes the room's affections, whose bits come off its flags, before it sets the
    flags again (implement_room, shaperom.cpp).
    """
    implementor = server.login(IMPLEMENTOR.name)
    mortal = server.login(MORTAL_IN_DARK_CELL.name)
    booted_look = look(mortal)

    open_room_editor(implementor, world.ROOM_DARK_CELL)
    implement_room(implementor)
    close_room_editor(implementor)
    implemented_look = look(mortal)
    implementor.command(f"goto {world.ROOM_DARK_CELL}")
    implementor.send_line("stat room")
    stat_text = implementor.expect(["Affections:"]) + implementor.drain(SETTLE_SECONDS)

    assert booted_look == PITCH_BLACK
    assert implemented_look == PITCH_BLACK
    assert DARK_FLAGS_LINE in lines_of(stat_text), stat_text


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, MORTAL_IN_ROOM)
def test_an_implemented_affections_room_flags_take_effect_and_leave_with_it(server) -> None:
    """Pins #318: an affection whose flags set DARK darkens the room once implemented, so a
    mortal there sees nothing, and removing it and implementing again lights the room."""
    implementor = server.login(IMPLEMENTOR.name)
    mortal = server.login(MORTAL_IN_ROOM.name)
    lit_look = look(mortal)

    open_room_editor(implementor)
    added = editor_step(implementor, "/19", [AFFECTION_ADDED, ROOM_PROMPT])
    assert AFFECTION_ADDED in added, added
    prompt = editor_step(implementor, "/18", [NEW_AFFECTION_VALUES, "Current:", NO_AFFECTIONS_FOUND])
    assert NEW_AFFECTION_VALUES in lines_of(prompt), prompt
    editor_step(implementor, DARKENING_AFFECTION_ANSWER, [ROOM_PROMPT])
    implement_room(implementor)
    darkened_look = look(mortal)
    removed = editor_step(implementor, "/20", [AFFECTION_REMOVED, NO_AFFECTIONS_EXIST])
    assert AFFECTION_REMOVED in lines_of(removed), removed
    implement_room(implementor)
    close_room_editor(implementor)
    relit_look = look(mortal)

    assert lit_look == "Immortal Start"
    assert darkened_look == PITCH_BLACK
    assert relit_look == "Immortal Start"
