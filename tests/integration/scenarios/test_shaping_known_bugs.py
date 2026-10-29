"""Known bugs in the shaping editors, each pinned as a strict xfail of the behaviour it should
have, so a fix shows up as a failure to remove the mark."""

from __future__ import annotations

import re
from pathlib import Path
from typing import NamedTuple

import pytest

from rots_harness import world
from rots_harness.characters import IMPLEMENTOR, Character
from rots_harness.session import GameSession

pytestmark = [pytest.mark.scenario, pytest.mark.shaping]

# A level-93 immortal: high enough to shape anything (LEVEL_GOD, structs.h), too low to own every
# zone (LEVEL_GRGOD is 97), so an owned zone gives it limited permission.
BUILDER = Character(name="Testbuild", race="human", sex="male", level=93, load_room=world.ROOM_IMMORTAL_START, class_name="warrior")
# An idnum no test character has, made the owner of zone 11 so it is no longer open to all.
OUTSIDE_OWNER = 99999
# The zone the editors work in, and its file relative to the lib.
ZONE_NUMBER = 11
ZONE_FILE_RELATIVE = Path("world") / "zon" / f"{ZONE_NUMBER}.zon"
# The editors' prompts while no field is being answered (prompt_text in consts.cpp).
ZONE_PROMPT = f"Zone: {ZONE_NUMBER}>"
SIGNPOST_PROMPT = f"Shaping: {world.ROOM_SIGNPOST}>"
# What /free prints in the zone, room and script editors, and what a / line gets once the
# shaper is no longer shaping (shape_center, shapemob.cpp).
ZONE_RELEASED = "You released your job and stop shaping."
ROOM_RELEASED = "You released the room and stopped shaping."
SCRIPT_RELEASED = "You released the script and stopped shaping."
NOT_SHAPING = "Use 'shape <mob|obj|room|script> <number>' to start shaping."
# What the zone editor prints on load and on implement (load_zone, implement_zone).
ZONE_LOADED_MARKER = "Zone loaded"
ZONE_IMPLEMENTED = "The zone was implemented."
# What every editor command but a field number prints first (extra_coms_* in shape*.cpp).
EDITOR_COMMAND_MARKER = "shapeless indefinity"
# The signpost room's two extra descriptions, in their file order, and what `look sign` shows
# at boot: boot puts the second one first (load_rooms, db.cpp), and look takes the first match.
SIGNPOST_KEYWORDS_IN_FILE_ORDER = ["plaque sign", "sign"]
BOOTED_SIGN_TEXT = "The sign reads SECOND."
BOOTED_EXTRA_DESCS = "Extra descs: sign plaque sign"
# A refusal in the zone editor ends with this word (the checks in shape_center_zone).
REFUSAL_END = "dropped."
# Seconds to keep reading after a marker, for the rest of the reply.
SETTLE_SECONDS = 0.5


def lines_of(text: str) -> list:
    """The text's lines without the carriage returns the game sends with each line end."""
    return [line.strip("\r") for line in text.split("\n")]


def reply_to(session: GameSession, line: str, outcomes: list) -> str:
    """Sends a line and returns what came back, up to and just after the first outcome.

    Editor prompts end in ':' or ']' mid-field, so command() cannot be used inside an editor.
    """
    session.send_line(line)
    return session.expect(outcomes) + session.drain(SETTLE_SECONDS)


def open_zone_editor(session: GameSession) -> str:
    """Starts shaping zone 11; returns what the load printed."""
    loaded = reply_to(session, f"shape zone {ZONE_NUMBER}", [ZONE_PROMPT, "could not open that file."])
    assert ZONE_LOADED_MARKER in loaded, loaded
    return loaded


def free_editor(session: GameSession, released: str) -> None:
    """Leaves an editor, or finds the shaper already out of it, so the session can quit."""
    freed = reply_to(session, "/free", [released, NOT_SHAPING])
    assert released in freed or NOT_SHAPING in freed, freed


def give_zone_an_outside_owner(implementor: GameSession, save: bool) -> None:
    """Makes OUTSIDE_OWNER the owner of zone 11, in the game and, if save, in the zone file."""
    open_zone_editor(implementor)
    reply_to(implementor, "/14", ["Enter owner idnum to add:"])
    reply_to(implementor, str(OUTSIDE_OWNER), [ZONE_PROMPT])
    if save:
        reply_to(implementor, "/save", [EDITOR_COMMAND_MARKER])
    implemented = reply_to(implementor, "/implement", [ZONE_IMPLEMENTED, "You are not allowed"])
    assert ZONE_IMPLEMENTED in implemented, implemented
    free_editor(implementor, ZONE_RELEASED)


def file_text(path: Path) -> str:
    """A world file's text; the editors end lines with "\\n\\r", which read_text would double."""
    return path.read_bytes().decode("latin-1")


@pytest.mark.shaping_script_editor
@pytest.mark.xfail(strict=True, raises=AssertionError, reason="a builder without permission over the zone cannot /implement a script in it")
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, BUILDER)
def test_a_builder_without_permission_cannot_implement_a_script(server) -> None:
    """B1: the script editor's /implement has no permission check (implement_script and its
    SHAPE_IMPLEMENT case, shapescript.cpp:515-580 and :1223-1225), where its own /save and /add
    refuse (replace_script :432, append_script :317) and every other editor's implement checks.

    Once zone 11 has an owner, a level-93 builder gets limited permission over script 1100, yet
    the name it gives the script reaches the game: the implementor reopening the script, which
    copies the script the game runs, reads the builder's name.
    """
    implementor = server.login(IMPLEMENTOR.name)
    builder = server.login(BUILDER.name)
    give_zone_an_outside_owner(implementor, save=False)

    opened = reply_to(builder, "shape script 1100", ["Script: 1100>"])
    assert "Limited permission only." in opened, opened
    reply_to(builder, "/20", ["Enter line SCRIPT NAME"])
    reply_to(builder, "hacked", ["Script: 1100>"])
    reply_to(builder, "/implement", [EDITOR_COMMAND_MARKER])
    free_editor(builder, SCRIPT_RELEASED)
    reply_to(implementor, "shape script 1100", ["Script: 1100>"])
    shown = reply_to(implementor, "/51", ["Script #1100:"])
    free_editor(implementor, SCRIPT_RELEASED)

    assert "Script #1100: harness stub" in lines_of(shown), shown


@pytest.mark.shaping_zone_editor
@pytest.mark.xfail(strict=True, raises=AssertionError, reason="a zone /done refused for want of permission keeps the editor open with the edits")
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, BUILDER)
def test_a_zone_done_refused_for_permission_keeps_the_edits(server) -> None:
    """B2: zone /done without permission says it may not save, then frees the editor, throwing
    the edits away (SHAPE_DONE in extra_coms_zone, shapezon.cpp:2619-2621); a failed save keeps
    them (:2613) as in the other editors.

    The zone editor reads its owners from the zone file, so the implementor saves the owner.
    """
    implementor = server.login(IMPLEMENTOR.name)
    builder = server.login(BUILDER.name)
    give_zone_an_outside_owner(implementor, save=True)

    loaded = open_zone_editor(builder)
    assert "Limited permission only." in loaded, loaded
    reply_to(builder, "/20", ["Enter line ZONE NAME"])
    reply_to(builder, "Builder arena", [ZONE_PROMPT])
    refused = reply_to(builder, "/done", ["You are not allowed to save this zone."])
    shown = reply_to(builder, "/51", ["Zone #11:", NOT_SHAPING])
    free_editor(builder, ZONE_RELEASED)

    assert "You are not allowed to save this zone." in lines_of(refused), refused
    assert "Zone #11: Builder arena" in lines_of(shown), shown


def extra_description_keywords(room_record: str) -> list:
    """The keywords of a room record's extra descriptions, in file order: the line after each
    'E' line, without its '~'."""
    record_lines = lines_of(room_record)
    return [record_lines[index + 1].rstrip("~") for index, line in enumerate(record_lines) if line == "E"]


def room_record(text: str, vnum: int) -> str:
    """The room #vnum in a world file's text, up to the next record's '#'."""
    found = re.search(rf"#{vnum}(?!\d)(.*?)(?=#\d)", text, re.S)
    assert found, text
    return found.group(0)


@pytest.mark.shaping_room_editor
@pytest.mark.xfail(strict=True, raises=AssertionError, reason="/save of a room with no edit keeps its extra descriptions in file order")
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR)
def test_saving_a_room_unedited_keeps_its_extra_descriptions_in_order(server) -> None:
    """B3: the room editor reads extra descriptions by putting each in front of the last
    (load_room, shaperom.cpp:1291-1297) and writes them in list order (write_room :184-187), so
    every /save reverses them in the file. #318 fixed the same for objects only.
    """
    implementor = server.login(IMPLEMENTOR.name)
    room_file = server.lib_dir / "world" / "wld" / "11.wld"
    booted_keywords = extra_description_keywords(room_record(file_text(room_file), world.ROOM_SIGNPOST))

    reply_to(implementor, f"shape room {world.ROOM_SIGNPOST}", [SIGNPOST_PROMPT])
    reply_to(implementor, "/save", [EDITOR_COMMAND_MARKER])
    # The game runs one line at a time, so once /50 answers, /save has finished writing.
    reply_to(implementor, "/50", ["(17) Room level"])
    free_editor(implementor, ROOM_RELEASED)

    assert booted_keywords == SIGNPOST_KEYWORDS_IN_FILE_ORDER
    saved_keywords = extra_description_keywords(room_record(file_text(room_file), world.ROOM_SIGNPOST))
    assert saved_keywords == SIGNPOST_KEYWORDS_IN_FILE_ORDER, saved_keywords


def look_and_stat_extra_descriptions(implementor: GameSession) -> tuple:
    """What `look sign` shows in the signpost room, and stat room's Extra descs line."""
    looked = implementor.command("look sign").text
    implementor.send_line("stat room")
    stat_text = implementor.expect(["Affections:"]) + implementor.drain(SETTLE_SECONDS)
    extra_descs = next((line for line in lines_of(stat_text) if line.startswith("Extra descs:")), "")
    return looked, extra_descs.rstrip()


@pytest.mark.shaping_room_editor
@pytest.mark.xfail(strict=True, raises=AssertionError, reason="/implement of a room with no edit leaves what look shows unchanged")
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR)
def test_implementing_a_room_unedited_keeps_what_look_shows(server) -> None:
    """B3: /implement copies the editor's extra descriptions by putting each in front of the
    last (implement_room, shaperom.cpp:278-293), the reverse of the editor's list, which is
    itself the reverse of the file; boot builds the reverse of the file (load_rooms,
    db.cpp:1480-1484). So /implement with no edit flips the order, and `look sign`, which takes
    the first match, shows the other description.
    """
    implementor = server.login(IMPLEMENTOR.name)
    implementor.command(f"goto {world.ROOM_SIGNPOST}")
    booted = look_and_stat_extra_descriptions(implementor)

    reply_to(implementor, f"shape room {world.ROOM_SIGNPOST}", [SIGNPOST_PROMPT])
    implemented = reply_to(implementor, "/implement", ["Room implemented.", "You may not"])
    free_editor(implementor, ROOM_RELEASED)
    after = look_and_stat_extra_descriptions(implementor)

    assert BOOTED_SIGN_TEXT in booted[0] and booted[1] == BOOTED_EXTRA_DESCS, booted
    assert "Room implemented." in lines_of(implemented), implemented
    assert BOOTED_SIGN_TEXT in after[0], after
    assert after[1] == BOOTED_EXTRA_DESCS, after


@pytest.mark.shaping_zone_editor
@pytest.mark.xfail(strict=True, raises=AssertionError, reason="zreset names the zone by its number, as its log line does")
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR)
def test_zreset_names_the_zone_by_its_number(server) -> None:
    """B6: zreset's reply prints the zone's index in the zone table (do_zreset, act_wiz.cpp:2138),
    0 for the test world's first zone, where its log line prints the zone number (:2140)."""
    implementor = server.login(IMPLEMENTOR.name)

    implementor.send_line(f"zreset {ZONE_NUMBER}")
    reply = implementor.expect([": Harness arena.", "Invalid zone number."]) + implementor.drain(SETTLE_SECONDS)

    assert f"Reset zone {ZONE_NUMBER}: Harness arena." in lines_of(reply), reply


class RecordEditor(NamedTuple):
    """An editor opened on a test-world record, and the record that follows it in the file."""

    open_line: str  # starts shaping the record
    prompt: str  # the editor's prompt while no field is being answered
    release_marker: str  # what /free prints
    file_relative: Path  # the world file /save rewrites, relative to the lib
    next_record: str  # how the next record in the file starts


RECORD_EDITORS = [
    RecordEditor(f"shape room {world.ROOM_SIGNPOST}", SIGNPOST_PROMPT, ROOM_RELEASED, Path("world") / "wld" / "11.wld", "#1184"),
    RecordEditor("shape mob 1130", "Mobile: 1130>", "You released the mobile and stopped shaping.", Path("world") / "mob" / "11.mob", "#1131"),
    RecordEditor("shape object 1130", "Object: 1130>", "You released an object and stopped shaping.", Path("world") / "obj" / "11.obj", "#1136"),
    RecordEditor("shape script 1100", "Script: 1100>", SCRIPT_RELEASED, Path("world") / "scr" / "11.scr", "#1180"),
]


@pytest.mark.shaping_saving
@pytest.mark.parametrize("editor", RECORD_EDITORS, ids=[editor.open_line.split()[1] for editor in RECORD_EDITORS])
@pytest.mark.xfail(strict=True, raises=AssertionError, reason="/save leaves the file after the saved record byte for byte as it was")
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR)
def test_saving_a_record_leaves_the_rest_of_the_file_unchanged(server, editor: RecordEditor) -> None:
    """B7: every replace_* backup loop writes the character it read before checking for end of
    file, so the backup, and the file rewritten from it, gain a copy of the last byte on every
    save (replace_room shaperom.cpp:1503-1505, replace_proto shapemob.cpp:2382-2384,
    replace_object shapeobj.cpp:1665-1667, replace_script shapescript.cpp:455-457, and append_script
    :352-354 the same). The zone
    editor copies with fgets and fputs and is not affected.
    """
    implementor = server.login(IMPLEMENTOR.name)
    world_file = server.lib_dir / editor.file_relative
    booted = world_file.read_bytes()
    next_record = editor.next_record.encode("latin-1")

    reply_to(implementor, editor.open_line, [editor.prompt])
    reply_to(implementor, "/save", [EDITOR_COMMAND_MARKER])
    # The game runs one line at a time, so once /free answers, /save has finished writing.
    free_editor(implementor, editor.release_marker)

    saved = world_file.read_bytes()
    assert saved[saved.index(next_record):] == booted[booted.index(next_record):]


@pytest.mark.shaping_script_editor
@pytest.mark.xfail(strict=True, raises=AssertionError, reason="/1 in the script editor labels the rows Prev:, Curr: and Next:")
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR)
def test_the_script_editors_1_labels_the_rows_it_shows(server) -> None:
    """B9: the script editor's /1 builds "Prev: " and "Curr: " into a buffer it never sends
    (shape_center_script, shapescript.cpp:1654-1663), so the three rows come unlabelled, where the
    zone editor's /1 labels them."""
    implementor = server.login(IMPLEMENTOR.name)

    reply_to(implementor, "shape script 1180", ["Script: 1180>"])
    reply_to(implementor, "/8", ["Enter row number"])
    reply_to(implementor, "2", ["Script: 1180>"])
    shown = reply_to(implementor, "/1", ["(third)", "No next command."])
    free_editor(implementor, SCRIPT_RELEASED)

    shown_lines = lines_of(shown)
    assert any(line.startswith("Prev: ") and "(first)" in line for line in shown_lines), shown
    assert any(line.startswith("Curr: ") and "(second)" in line for line in shown_lines), shown
    assert any(line.startswith("Next: ") and "(third)" in line for line in shown_lines), shown


@pytest.mark.shaping_zone_editor
@pytest.mark.xfail(strict=True, raises=AssertionError, reason="a lower-case letter at the zone /2 filter lists that letter's rows")
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR)
def test_a_lower_case_zone_filter_letter_lists_its_rows(server) -> None:
    """B12: /2 stores the filter's letter as typed (shape_center_zone, shapezon.cpp:1083), while
    /3 upper-cases the letter it sets (:1188, :1198), so `m` matches no row: every row's letter
    is upper case."""
    implementor = server.login(IMPLEMENTOR.name)

    open_zone_editor(implementor)
    reply_to(implementor, "/2", ["Enter filter:"])
    reply_to(implementor, "m", [ZONE_PROMPT])
    listing = reply_to(implementor, "/50", ["List range:"])
    free_editor(implementor, ZONE_RELEASED)

    assert any(line.startswith("  1 M::") for line in lines_of(listing)), listing


@pytest.mark.shaping_zone_editor
@pytest.mark.xfail(strict=True, raises=AssertionError, reason="a $ typed into a zone row's comment is stored once")
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR)
def test_a_dollar_typed_into_a_zone_comment_is_stored_once(server) -> None:
    """B5: the input reader doubles every typed '$' for act()'s formatting (process_input,
    comm.cpp:1996-1998), and the editors store the line as it arrives, so the comment keeps both.
    """
    implementor = server.login(IMPLEMENTOR.name)

    open_zone_editor(implementor)
    reply_to(implementor, "/5", ["Enter line COMMENT"])
    reply_to(implementor, "costs $5", [ZONE_PROMPT])
    shown = reply_to(implementor, "/1", ["Curr:"])
    free_editor(implementor, ZONE_RELEASED)

    shown_lines = [line.strip() for line in lines_of(shown)]
    assert "costs $5" in shown_lines, shown


@pytest.mark.shaping_zone_editor
@pytest.mark.xfail(strict=True, raises=AssertionError, reason="a zone D row with a direction above 5 is refused")
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR)
def test_a_door_row_with_a_direction_above_5_is_refused(server) -> None:
    """B10: a D row's numbers with a direction above 5 keep the direction and set the state to
    0 instead (shape_center_zone, shapezon.cpp:1294), where a negative direction becomes 0 and
    the reset-mode prompt refuses its out-of-range answers."""
    implementor = server.login(IMPLEMENTOR.name)

    open_zone_editor(implementor)
    reply_to(implementor, "/6", ["Next command chosen:", "No next command."])
    reply_to(implementor, "/10", ["New command added next to current, and selected."])
    reply_to(implementor, "/3", ["Enter command type:"])
    reply_to(implementor, "D", ["Enter 4 numbers:"])
    answered = reply_to(implementor, "0 1131 9 1", ["Enter line COMMENT", ZONE_PROMPT])
    if "Enter line COMMENT" in answered:
        reply_to(implementor, "", [ZONE_PROMPT])
    shown = reply_to(implementor, "/1", ["Curr:"])
    free_editor(implementor, ZONE_RELEASED)

    assert any(line.endswith(REFUSAL_END) for line in lines_of(answered)), answered
    assert "Exit:X" not in shown, shown


# Each zone number prompt that takes a negative value, the answer, and how /51 shows the field
# unchanged.
NEGATIVE_ZONE_CASES = [("/23", "-5", "Reset time: 999;"), ("/25", "-3", "Level:5")]


@pytest.mark.shaping_zone_editor
@pytest.mark.parametrize(("field_line", "answer", "kept"), NEGATIVE_ZONE_CASES, ids=["lifespan", "level"])
@pytest.mark.xfail(strict=True, raises=AssertionError, reason="the zone lifespan and level prompts refuse a negative number")
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR)
def test_a_negative_zone_lifespan_or_level_is_refused(server, field_line: str, answer: str, kept: str) -> None:
    """B11: /23 (reset time) and /25 (level) store a negative number (shape_center_zone,
    shapezon.cpp:1766 and :1798), while /24 (reset mode) refuses a value outside 0-3 (:1780)."""
    implementor = server.login(IMPLEMENTOR.name)

    open_zone_editor(implementor)
    reply_to(implementor, field_line, ["Enter "])
    answered = reply_to(implementor, answer, [ZONE_PROMPT])
    shown = reply_to(implementor, "/51", ["Owners:"])
    free_editor(implementor, ZONE_RELEASED)

    assert any(line.endswith(REFUSAL_END) for line in lines_of(answered)), answered
    assert kept in shown, shown


@pytest.mark.shaping_zone_editor
@pytest.mark.xfail(strict=True, raises=AssertionError, reason="zone /save with no edit writes the zone file's words unchanged")
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR)
def test_saving_a_zone_unedited_keeps_the_files_words(server) -> None:
    """B16: replace_zone reads on to the line after the zone (shapezon.cpp:2420-2423) and writes it
    after the new zone (:2439); when the zone ends the file, as the test world's does, fgets
    leaves the zone's own S line in the buffer, so the file ends with two S lines. A zone file that
    ends with a #99999 record, as the real world's do, is not affected.
    """
    implementor = server.login(IMPLEMENTOR.name)
    zone_file = server.lib_dir / ZONE_FILE_RELATIVE
    booted_words = zone_file.read_text(encoding="latin-1").split()

    open_zone_editor(implementor)
    reply_to(implementor, "/save", [EDITOR_COMMAND_MARKER])
    reply_to(implementor, "/1", ["Curr:"])
    free_editor(implementor, ZONE_RELEASED)

    assert zone_file.read_text(encoding="latin-1").split() == booted_words
