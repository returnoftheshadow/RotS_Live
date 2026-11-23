"""Known bugs in the shaping editors, each pinned as a strict xfail of the behaviour it should have.

A fix shows up as a failure to remove the mark.
"""

from __future__ import annotations

from pathlib import Path
from typing import NamedTuple

import pytest

from rots_harness import world
from rots_harness.characters import IMPLEMENTOR, immortal
from rots_harness.immortal_commands import goto
from rots_harness.replies import reply_to
from rots_harness.session import GameSession
from rots_harness.shaping import (
    COULD_NOT_OPEN_FILE_REPLY,
    EDITOR_COMMAND_PART,
    EDITOR_SETTLE_SECONDS,
    MOBILE_PROMPT,
    MOBILE_RELEASED_REPLY,
    OBJECT_PROMPT,
    OBJECT_RELEASED_REPLY,
    ORDERED_SCRIPT,
    ORDERED_SCRIPT_PROMPT,
    ROOM_IMPLEMENTED_REPLY,
    ROOM_RELEASED_REPLY,
    SCRIPT_FILE_RELATIVE,
    SCRIPT_PROMPT,
    SCRIPT_RELEASED_REPLY,
    SHAPED_RECORD,
    SHAPED_SCRIPT,
    SIGNPOST_ROOM_PROMPT,
    ZONE_FILE_RELATIVE,
    ZONE_IMPLEMENTED_REPLY,
    ZONE_NUMBER,
    ZONE_PROMPT,
    ZONE_RELEASED_REPLY,
)
from rots_harness.stat_reading import OBJECT_STAT_LAST_LINE, read_stat
from rots_harness.text_lines import raw_lines
from test_shaping_saving import NOT_SHAPING_REPLY, file_text, record

pytestmark = [pytest.mark.scenario, pytest.mark.shaping]

# A level-93 immortal: high enough to shape anything (LEVEL_GOD, structs.h), too low to own every
# zone (LEVEL_GRGOD is 97), so an owned zone gives it limited permission.
BUILDER = immortal("Testbuild", 93)
# An idnum no test character has, made the owner of zone 11 so it is no longer open to all.
OUTSIDE_OWNER = 99999
# The start of what the zone editor prints on load (load_zone, shapezon.cpp).
ZONE_LOADED_START = "Zone loaded"
# The signpost room's two extra descriptions, in their file order.
SIGNPOST_KEYWORDS_IN_FILE_ORDER = ["plaque sign", "sign"]
# What `look sign` shows at boot: boot puts the second description first (load_rooms, db.cpp),
# and look takes the first match.
BOOTED_SIGN_LOOK = "The sign reads SECOND."
# The Extra descs line stat room shows at boot, in the order boot builds.
BOOTED_EXTRA_DESCS = "Extra descs: sign plaque sign"
# A refusal in the zone editor ends with this word (the checks in shape_center_zone).
REFUSAL_END = "dropped."


def open_zone_editor(session: GameSession) -> str:
    """Start shaping zone 11 and return what the load printed."""
    outcomes = [ZONE_PROMPT, COULD_NOT_OPEN_FILE_REPLY]
    loaded = reply_to(session, f"shape zone {ZONE_NUMBER}", outcomes, settle=EDITOR_SETTLE_SECONDS)
    assert ZONE_LOADED_START in loaded, loaded
    return loaded


def free_editor(session: GameSession, released: str) -> None:
    """Leave an editor, or find the shaper already out of it, so the session can quit."""
    freed = reply_to(session, "/free", [released, NOT_SHAPING_REPLY], settle=EDITOR_SETTLE_SECONDS)
    assert released in freed or NOT_SHAPING_REPLY in freed, freed


def give_zone_an_outside_owner(implementor: GameSession, *, save: bool) -> None:
    """Make OUTSIDE_OWNER the owner of zone 11, in the game and, if save, in the zone file."""
    open_zone_editor(implementor)
    reply_to(implementor, "/14", ["Enter owner idnum to add:"], settle=EDITOR_SETTLE_SECONDS)
    reply_to(implementor, str(OUTSIDE_OWNER), [ZONE_PROMPT], settle=EDITOR_SETTLE_SECONDS)
    if save:
        reply_to(implementor, "/save", [EDITOR_COMMAND_PART], settle=EDITOR_SETTLE_SECONDS)
    implemented = reply_to(
        implementor, "/implement", [ZONE_IMPLEMENTED_REPLY, "You are not allowed"], settle=EDITOR_SETTLE_SECONDS
    )
    assert ZONE_IMPLEMENTED_REPLY in implemented, implemented
    free_editor(implementor, ZONE_RELEASED_REPLY)


@pytest.mark.shaping_script_editor
@pytest.mark.xfail(
    strict=True,
    raises=AssertionError,
    reason="a builder without permission over the zone cannot /implement a script in it",
)
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, BUILDER)
def test_a_builder_without_permission_cannot_implement_a_script(server) -> None:
    """Bug: the script editor's /implement lets a builder without permission change the game.

    It has no permission check (implement_script and its SHAPE_IMPLEMENT case,
    shapescript.cpp:515-580 and :1223-1225), where its own /save and /add refuse (replace_script
    :432, append_script :317) and every other editor's implement checks.

    Once zone 11 has an owner, a level-93 builder gets limited permission over script 1100, yet
    the name it gives the script reaches the game: the implementor reopening the script, which
    copies the script the game runs, reads the builder's name.
    """
    implementor = server.login(IMPLEMENTOR.name)
    builder = server.login(BUILDER.name)
    give_zone_an_outside_owner(implementor, save=False)

    opened = reply_to(builder, f"shape script {SHAPED_SCRIPT}", [SCRIPT_PROMPT], settle=EDITOR_SETTLE_SECONDS)
    assert "Limited permission only." in opened, opened
    reply_to(builder, "/20", ["Enter line SCRIPT NAME"], settle=EDITOR_SETTLE_SECONDS)
    reply_to(builder, "hacked", [SCRIPT_PROMPT], settle=EDITOR_SETTLE_SECONDS)
    reply_to(builder, "/implement", [EDITOR_COMMAND_PART], settle=EDITOR_SETTLE_SECONDS)
    free_editor(builder, SCRIPT_RELEASED_REPLY)
    reply_to(implementor, f"shape script {SHAPED_SCRIPT}", [SCRIPT_PROMPT], settle=EDITOR_SETTLE_SECONDS)
    shown = reply_to(implementor, "/51", [f"Script #{SHAPED_SCRIPT}:"], settle=EDITOR_SETTLE_SECONDS)
    free_editor(implementor, SCRIPT_RELEASED_REPLY)

    assert f"Script #{SHAPED_SCRIPT}: harness stub" in raw_lines(shown), shown


@pytest.mark.shaping_zone_editor
@pytest.mark.xfail(
    strict=True,
    raises=AssertionError,
    reason="a zone /done refused for want of permission keeps the editor open with the edits",
)
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, BUILDER)
def test_a_zone_done_refused_for_permission_keeps_the_edits(server) -> None:
    """Bug: a zone /done refused for want of permission throws the edits away.

    Zone /done without permission says it may not save, then frees the editor (SHAPE_DONE in
    extra_coms_zone, shapezon.cpp:2619-2621); a failed save keeps the edits (:2613) as in the
    other editors.

    The zone editor reads its owners from the zone file, so the implementor saves the owner.
    """
    implementor = server.login(IMPLEMENTOR.name)
    builder = server.login(BUILDER.name)
    give_zone_an_outside_owner(implementor, save=True)

    loaded = open_zone_editor(builder)
    assert "Limited permission only." in loaded, loaded
    reply_to(builder, "/20", ["Enter line ZONE NAME"], settle=EDITOR_SETTLE_SECONDS)
    reply_to(builder, "Builder arena", [ZONE_PROMPT], settle=EDITOR_SETTLE_SECONDS)
    refused = reply_to(builder, "/done", ["You are not allowed to save this zone."], settle=EDITOR_SETTLE_SECONDS)
    shown = reply_to(builder, "/51", [f"Zone #{ZONE_NUMBER}:", NOT_SHAPING_REPLY], settle=EDITOR_SETTLE_SECONDS)
    free_editor(builder, ZONE_RELEASED_REPLY)

    assert "You are not allowed to save this zone." in raw_lines(refused), refused
    assert f"Zone #{ZONE_NUMBER}: Builder arena" in raw_lines(shown), shown


def extra_description_keywords(room_record: str) -> list[str]:
    """Return the keywords of a room record's extra descriptions, in file order.

    Each is the line after an 'E' line, without its '~'.
    """
    record_lines = raw_lines(room_record)
    return [record_lines[index + 1].rstrip("~") for index, line in enumerate(record_lines) if line == "E"]


@pytest.mark.shaping_room_editor
@pytest.mark.xfail(
    strict=True,
    raises=AssertionError,
    reason="/save of a room with no edit keeps its extra descriptions in file order",
)
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR)
def test_saving_a_room_unedited_keeps_its_extra_descriptions_in_order(server) -> None:
    """Bug: every /save of a room reverses its extra descriptions in the file.

    The room editor reads extra descriptions by putting each in front of the last (load_room,
    shaperom.cpp:1291-1297) and writes them in list order (write_room :184-187). The object editor
    keeps the file order.
    """
    implementor = server.login(IMPLEMENTOR.name)
    room_file = server.lib_dir / "world" / "wld" / "11.wld"
    booted_text = file_text(room_file)
    booted_keywords = extra_description_keywords(record(booted_text, world.ROOM_SIGNPOST))

    reply_to(implementor, f"shape room {world.ROOM_SIGNPOST}", [SIGNPOST_ROOM_PROMPT], settle=EDITOR_SETTLE_SECONDS)
    reply_to(implementor, "/save", [EDITOR_COMMAND_PART], settle=EDITOR_SETTLE_SECONDS)
    # The game runs one line at a time, so once /50 answers, /save has finished writing.
    reply_to(implementor, "/50", ["(17) Room level"], settle=EDITOR_SETTLE_SECONDS)
    free_editor(implementor, ROOM_RELEASED_REPLY)

    assert booted_keywords == SIGNPOST_KEYWORDS_IN_FILE_ORDER, booted_keywords
    saved_text = file_text(room_file)
    saved_keywords = extra_description_keywords(record(saved_text, world.ROOM_SIGNPOST))
    assert saved_keywords == SIGNPOST_KEYWORDS_IN_FILE_ORDER, saved_keywords


def look_and_stat_extra_descriptions(implementor: GameSession) -> tuple[str, str]:
    """Return what `look sign` shows in the signpost room, and stat room's Extra descs line."""
    looked = implementor.command("look sign").text
    stat_text = read_stat(implementor, "room", last_line=OBJECT_STAT_LAST_LINE, settle=EDITOR_SETTLE_SECONDS)
    stat_lines = raw_lines(stat_text)
    extra_descs = next((line for line in stat_lines if line.startswith("Extra descs:")), "")
    return looked, extra_descs.rstrip()


@pytest.mark.shaping_room_editor
@pytest.mark.xfail(
    strict=True,
    raises=AssertionError,
    reason="/implement of a room with no edit leaves what look shows unchanged",
)
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR)
def test_implementing_a_room_unedited_keeps_what_look_shows(server) -> None:
    """Bug: /implement of a room with no edit flips what `look sign` shows.

    /implement copies the editor's extra descriptions by putting each in front of the last
    (implement_room, shaperom.cpp:278-293), the reverse of the editor's list, which is itself the
    reverse of the file; boot builds the reverse of the file (load_rooms, db.cpp:1480-1484). So
    /implement with no edit flips the order, and `look sign`, which takes the first match, shows
    the other description.
    """
    implementor = server.login(IMPLEMENTOR.name)
    goto(implementor, world.ROOM_SIGNPOST)
    booted = look_and_stat_extra_descriptions(implementor)

    reply_to(implementor, f"shape room {world.ROOM_SIGNPOST}", [SIGNPOST_ROOM_PROMPT], settle=EDITOR_SETTLE_SECONDS)
    implemented = reply_to(
        implementor, "/implement", [ROOM_IMPLEMENTED_REPLY, "You may not"], settle=EDITOR_SETTLE_SECONDS
    )
    free_editor(implementor, ROOM_RELEASED_REPLY)
    after = look_and_stat_extra_descriptions(implementor)

    assert BOOTED_SIGN_LOOK in booted[0] and booted[1] == BOOTED_EXTRA_DESCS, booted
    assert ROOM_IMPLEMENTED_REPLY in raw_lines(implemented), implemented
    assert BOOTED_SIGN_LOOK in after[0], after
    assert after[1] == BOOTED_EXTRA_DESCS, after


@pytest.mark.shaping_zone_editor
@pytest.mark.xfail(
    strict=True,
    raises=AssertionError,
    reason="zreset names the zone by its number, as its log line does",
)
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR)
def test_zreset_names_the_zone_by_its_number(server) -> None:
    """Bug: zreset's reply names the zone by its index in the zone table, not its number.

    do_zreset (act_wiz.cpp:2138) prints the index, 0 for the test world's first zone, where its
    log line prints the zone number (:2140).
    """
    implementor = server.login(IMPLEMENTOR.name)

    outcomes = [": Harness arena.", "Invalid zone number."]
    reply = reply_to(implementor, f"zreset {ZONE_NUMBER}", outcomes, settle=EDITOR_SETTLE_SECONDS)

    assert f"Reset zone {ZONE_NUMBER}: Harness arena." in raw_lines(reply), reply


class RecordEditor(NamedTuple):
    """An editor opened on a test-world record, and the record that follows it in the file."""

    # The line that starts shaping the record.
    open_line: str
    # The editor's prompt while no field is being answered.
    prompt: str
    # What /free prints.
    release_marker: str
    # The world file /save rewrites, relative to the lib.
    file_relative: Path
    # How the next record in the file starts.
    next_record: str


# The four record editors, each on a record that another record follows in its file.
RECORD_EDITORS = [
    RecordEditor(
        f"shape room {world.ROOM_SIGNPOST}",
        SIGNPOST_ROOM_PROMPT,
        ROOM_RELEASED_REPLY,
        Path("world") / "wld" / "11.wld",
        "#1184",
    ),
    RecordEditor(
        f"shape mob {SHAPED_RECORD}", MOBILE_PROMPT, MOBILE_RELEASED_REPLY, Path("world") / "mob" / "11.mob", "#1131"
    ),
    RecordEditor(
        f"shape object {SHAPED_RECORD}", OBJECT_PROMPT, OBJECT_RELEASED_REPLY, Path("world") / "obj" / "11.obj", "#1136"
    ),
    RecordEditor(f"shape script {SHAPED_SCRIPT}", SCRIPT_PROMPT, SCRIPT_RELEASED_REPLY, SCRIPT_FILE_RELATIVE, "#1180"),
]


@pytest.mark.shaping_saving
@pytest.mark.parametrize("editor", RECORD_EDITORS, ids=[editor.open_line.split()[1] for editor in RECORD_EDITORS])
@pytest.mark.xfail(
    strict=True,
    raises=AssertionError,
    reason="/save leaves the file after the saved record byte for byte as it was",
)
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR)
def test_saving_a_record_leaves_the_rest_of_the_file_unchanged(server, editor: RecordEditor) -> None:
    """Bug: every record editor's /save adds a copy of the file's last byte.

    Every replace_* backup loop writes the character it read before checking for end of file, so
    the backup, and the file rewritten from it, gain a copy of the last byte on every save
    (replace_room shaperom.cpp:1503-1505, replace_proto shapemob.cpp:2382-2384, replace_object
    shapeobj.cpp:1665-1667, replace_script shapescript.cpp:455-457, and append_script :352-354 the
    same). The zone editor copies with fgets and fputs and is not affected.
    """
    implementor = server.login(IMPLEMENTOR.name)
    world_file = server.lib_dir / editor.file_relative
    booted = world_file.read_bytes()
    next_record = editor.next_record.encode("latin-1")

    reply_to(implementor, editor.open_line, [editor.prompt], settle=EDITOR_SETTLE_SECONDS)
    reply_to(implementor, "/save", [EDITOR_COMMAND_PART], settle=EDITOR_SETTLE_SECONDS)
    # The game runs one line at a time, so once /free answers, /save has finished writing.
    free_editor(implementor, editor.release_marker)

    saved = world_file.read_bytes()
    assert saved[saved.index(next_record):] == booted[booted.index(next_record):], (
        f"{editor.file_relative} changed after the saved record"
    )


@pytest.mark.shaping_script_editor
@pytest.mark.xfail(
    strict=True,
    raises=AssertionError,
    reason="/1 in the script editor labels the rows Prev:, Curr: and Next:",
)
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR)
def test_the_script_editors_1_labels_the_rows_it_shows(server) -> None:
    """Bug: the script editor's /1 shows its three rows unlabelled.

    It builds "Prev: " and "Curr: " into a buffer it never sends (shape_center_script,
    shapescript.cpp:1654-1663), where the zone editor's /1 labels them.
    """
    implementor = server.login(IMPLEMENTOR.name)

    reply_to(implementor, f"shape script {ORDERED_SCRIPT}", [ORDERED_SCRIPT_PROMPT], settle=EDITOR_SETTLE_SECONDS)
    reply_to(implementor, "/8", ["Enter row number"], settle=EDITOR_SETTLE_SECONDS)
    reply_to(implementor, "2", [ORDERED_SCRIPT_PROMPT], settle=EDITOR_SETTLE_SECONDS)
    shown = reply_to(implementor, "/1", ["(third)", "No next command."], settle=EDITOR_SETTLE_SECONDS)
    free_editor(implementor, SCRIPT_RELEASED_REPLY)

    shown_lines = raw_lines(shown)
    assert any(line.startswith("Prev: ") and "(first)" in line for line in shown_lines), shown
    assert any(line.startswith("Curr: ") and "(second)" in line for line in shown_lines), shown
    assert any(line.startswith("Next: ") and "(third)" in line for line in shown_lines), shown


@pytest.mark.shaping_zone_editor
@pytest.mark.xfail(
    strict=True,
    raises=AssertionError,
    reason="a lower-case letter at the zone /2 filter lists that letter's rows",
)
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR)
def test_a_lower_case_zone_filter_letter_lists_its_rows(server) -> None:
    """Bug: a lower-case letter at the zone /2 filter matches no row.

    /2 stores the filter's letter as typed (shape_center_zone, shapezon.cpp:1083), while /3
    upper-cases the letter it sets (:1188, :1198), so `m` matches no row: every row's letter is
    upper case.
    """
    implementor = server.login(IMPLEMENTOR.name)

    open_zone_editor(implementor)
    reply_to(implementor, "/2", ["Enter filter:"], settle=EDITOR_SETTLE_SECONDS)
    reply_to(implementor, "m", [ZONE_PROMPT], settle=EDITOR_SETTLE_SECONDS)
    listing = reply_to(implementor, "/50", ["List range:"], settle=EDITOR_SETTLE_SECONDS)
    free_editor(implementor, ZONE_RELEASED_REPLY)

    listing_lines = raw_lines(listing)
    assert any(line.startswith("  1 M::") for line in listing_lines), listing


@pytest.mark.shaping_zone_editor
@pytest.mark.xfail(
    strict=True,
    raises=AssertionError,
    reason="a $ typed into a zone row's comment is stored once",
)
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR)
def test_a_dollar_typed_into_a_zone_comment_is_stored_once(server) -> None:
    """Bug: a '$' typed into a zone row's comment is stored twice.

    The input reader doubles every typed '$' for act()'s formatting (process_input,
    comm.cpp:1996-1998), and the editors store the line as it arrives, so the comment keeps both.
    """
    implementor = server.login(IMPLEMENTOR.name)

    open_zone_editor(implementor)
    reply_to(implementor, "/5", ["Enter line COMMENT"], settle=EDITOR_SETTLE_SECONDS)
    reply_to(implementor, "costs $5", [ZONE_PROMPT], settle=EDITOR_SETTLE_SECONDS)
    shown = reply_to(implementor, "/1", ["Curr:"], settle=EDITOR_SETTLE_SECONDS)
    free_editor(implementor, ZONE_RELEASED_REPLY)

    shown_raw_lines = raw_lines(shown)
    shown_lines = [line.strip() for line in shown_raw_lines]
    assert "costs $5" in shown_lines, shown


@pytest.mark.shaping_zone_editor
@pytest.mark.xfail(
    strict=True,
    raises=AssertionError,
    reason="a zone D row with a direction above 5 is refused",
)
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR)
def test_a_door_row_with_a_direction_above_5_is_refused(server) -> None:
    """Bug: a zone D row with a direction above 5 is accepted with its state set to 0.

    The row keeps the direction and sets the state to 0 instead (shape_center_zone,
    shapezon.cpp:1294), where a negative direction becomes 0 and the reset-mode prompt refuses
    its out-of-range answers.
    """
    implementor = server.login(IMPLEMENTOR.name)

    open_zone_editor(implementor)
    reply_to(implementor, "/6", ["Next command chosen:", "No next command."], settle=EDITOR_SETTLE_SECONDS)
    reply_to(
        implementor, "/10", ["New command added next to current, and selected."], settle=EDITOR_SETTLE_SECONDS
    )
    reply_to(implementor, "/3", ["Enter command type:"], settle=EDITOR_SETTLE_SECONDS)
    reply_to(implementor, "D", ["Enter 4 numbers:"], settle=EDITOR_SETTLE_SECONDS)
    answered = reply_to(implementor, "0 1131 9 1", ["Enter line COMMENT", ZONE_PROMPT], settle=EDITOR_SETTLE_SECONDS)
    if "Enter line COMMENT" in answered:
        reply_to(implementor, "", [ZONE_PROMPT], settle=EDITOR_SETTLE_SECONDS)
    shown = reply_to(implementor, "/1", ["Curr:"], settle=EDITOR_SETTLE_SECONDS)
    free_editor(implementor, ZONE_RELEASED_REPLY)

    answered_lines = raw_lines(answered)
    assert any(line.endswith(REFUSAL_END) for line in answered_lines), answered
    assert "Exit:X" not in shown, shown


# Each zone number prompt that takes a negative value, the answer, and how /51 shows the field
# unchanged.
NEGATIVE_ZONE_CASES = [("/23", "-5", "Reset time: 999;"), ("/25", "-3", "Level:5")]


@pytest.mark.shaping_zone_editor
@pytest.mark.parametrize(("field_line", "answer", "kept"), NEGATIVE_ZONE_CASES, ids=["lifespan", "level"])
@pytest.mark.xfail(
    strict=True,
    raises=AssertionError,
    reason="the zone lifespan and level prompts refuse a negative number",
)
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR)
def test_a_negative_zone_lifespan_or_level_is_refused(server, field_line: str, answer: str, kept: str) -> None:
    """Bug: the zone lifespan and level prompts store a negative number.

    /23 (reset time) and /25 (level) store it (shape_center_zone, shapezon.cpp:1766 and :1798),
    while /24 (reset mode) refuses a value outside 0-3 (:1780).
    """
    implementor = server.login(IMPLEMENTOR.name)

    open_zone_editor(implementor)
    reply_to(implementor, field_line, ["Enter "], settle=EDITOR_SETTLE_SECONDS)
    answered = reply_to(implementor, answer, [ZONE_PROMPT], settle=EDITOR_SETTLE_SECONDS)
    shown = reply_to(implementor, "/51", ["Owners:"], settle=EDITOR_SETTLE_SECONDS)
    free_editor(implementor, ZONE_RELEASED_REPLY)

    answered_lines = raw_lines(answered)
    assert any(line.endswith(REFUSAL_END) for line in answered_lines), answered
    assert kept in shown, shown


@pytest.mark.shaping_zone_editor
@pytest.mark.xfail(
    strict=True,
    raises=AssertionError,
    reason="zone /save with no edit writes the zone file's words unchanged",
)
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR)
def test_saving_a_zone_unedited_keeps_the_files_words(server) -> None:
    """Bug: zone /save with no edit ends a zone file that the zone ends with two S lines.

    replace_zone reads on to the line after the zone (shapezon.cpp:2420-2423) and writes it after
    the new zone (:2439); when the zone ends the file, as the test world's does, fgets leaves the
    zone's own S line in the buffer. A zone file that ends with a #99999 record, as the real
    world's do, is not affected.
    """
    implementor = server.login(IMPLEMENTOR.name)
    zone_file = server.lib_dir / ZONE_FILE_RELATIVE
    booted_text = zone_file.read_text(encoding="latin-1")
    booted_words = booted_text.split()

    open_zone_editor(implementor)
    reply_to(implementor, "/save", [EDITOR_COMMAND_PART], settle=EDITOR_SETTLE_SECONDS)
    reply_to(implementor, "/1", ["Curr:"], settle=EDITOR_SETTLE_SECONDS)
    free_editor(implementor, ZONE_RELEASED_REPLY)

    saved_text = zone_file.read_text(encoding="latin-1")
    saved_words = saved_text.split()
    assert saved_words == booted_words, f"{zone_file} words changed after /save"
