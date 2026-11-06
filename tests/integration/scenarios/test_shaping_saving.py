"""Saving from the five editors: a save that cannot back the file up keeps the edits and changes
nothing, /save rewrites the record and keeps the old file, and a plain /add saves a record that is
already in the file."""

from __future__ import annotations

import re
import shutil
from pathlib import Path
from typing import NamedTuple, Optional

import pytest

from rots_harness import world
from rots_harness.characters import IMPLEMENTOR, Character
from rots_harness.session import GameSession

pytestmark = [pytest.mark.scenario, pytest.mark.shaping, pytest.mark.shaping_saving]

# A second implementor, who reads the live game from the signpost room while the first one shapes.
# Implementors enter the game in the immortal start room whatever their load room, so the
# scenario sends it to the signpost room.
WATCHER = Character(
    name="Testwatch",
    race="human",
    sex="male",
    level=100,
    load_room=world.ROOM_IMMORTAL_START,
    class_name="warrior",
)

# The two lines every editor's /done prints when its save fails (extra_coms_* in shape*.cpp).
NOT_SAVED_LINES = ["Not saved - still shaping. Fix the problem and /done again,", "or /free to discard."]
# What a save prints when it cannot open the backup copy (replace_zone, replace_room,
# replace_object and replace_script); the mob editor's replace_proto words it its own way.
BACKUP_FAILURE = "could not open backup file"
MOB_BACKUP_FAILURE = "ERROR: Could not open backup file"
# What an editor command gets once its editor has been released (shape_center, shapemob.cpp).
NOT_SHAPING = "Use 'shape <mob|obj|room|script> <number>' to start shaping."
# The line every editor command but a field number prints first (extra_coms_* in shape*.cpp).
EDITOR_COMMAND_MARKER = "shapeless indefinity"
# Seconds to keep reading after a marker, for the rest of the reply.
SETTLE_SECONDS = 0.5


class Watch(NamedTuple):
    """One line the watcher types to read the live game, and the markers its reply ends at."""

    line: str
    markers: tuple


class EditorCase(NamedTuple):
    """An editor opened on a test-world record, one text field edited, and where the edit shows."""

    open_line: str  # starts shaping the record
    prompt: str  # the editor's prompt while no field is being answered (prompt_text in consts.cpp)
    release_marker: str  # what /free prints, and /done once it has saved
    file_relative: Path  # the world file /save rewrites, relative to the lib (SHAPE_*_DIR, protos.h)
    backup_dir_relative: Path  # where /save copies the file first (SHAPE_*_BACKDIR)
    edit_line: str  # the field number of a one-line text field
    old_text: str  # the field's text in the test world
    new_text: str  # what the scenario types into it
    list_line: str  # the editor command that shows the field
    shown_prefix: str  # how that listing starts the field's line, before its text
    backup_failure: str  # what the save prints without the backup directory
    watch: tuple  # what the watcher types to see the live record, as Watch steps
    live_prefix: str  # what precedes the field's text in the watcher's reply


ZONE_CASE = EditorCase(
    open_line="shape zone 11",
    prompt="Zone: 11>",
    release_marker="You released your job and stop shaping.",
    file_relative=Path("world") / "zon" / "11.zon",
    backup_dir_relative=Path("world") / "zon" / "oldzons",
    edit_line="/20",
    old_text="Harness arena",
    new_text="Renamed arena",
    list_line="/51",
    shown_prefix="Zone #11: ",
    backup_failure=BACKUP_FAILURE,
    watch=(Watch("zreset 11", (": Harness arena.", ": Renamed arena.")),),
    live_prefix=": ",
)
ROOM_CASE = EditorCase(
    open_line=f"shape room {world.ROOM_SIGNPOST}",
    prompt=f"Shaping: {world.ROOM_SIGNPOST}>",
    release_marker="You released the room and stopped shaping.",
    file_relative=Path("world") / "wld" / "11.wld",
    backup_dir_relative=Path("world") / "wld" / "oldroms",
    edit_line="/1",
    old_text="Signpost Room",
    new_text="Renamed Signpost",
    list_line="/50",
    shown_prefix="(1) name         :",
    backup_failure=BACKUP_FAILURE,
    watch=(Watch("look", ("Exits are:",)),),
    live_prefix="",
)
MOB_CASE = EditorCase(
    open_line="shape mob 1130",
    prompt="Mobile: 1130>",
    release_marker="You released the mobile and stopped shaping.",
    file_relative=Path("world") / "mob" / "11.mob",
    backup_dir_relative=Path("world") / "mob" / "oldmobs",
    edit_line="/2",
    old_text="a target orc",
    new_text="a renamed orc",
    list_line="/50",
    shown_prefix="(2) reference description  :",
    backup_failure=MOB_BACKUP_FAILURE,
    watch=(Watch("load mob 1130", ("You create",)),),
    live_prefix="You create ",
)
OBJECT_CASE = EditorCase(
    open_line="shape object 1130",
    prompt="Object: 1130>",
    release_marker="You released an object and stopped shaping.",
    file_relative=Path("world") / "obj" / "11.obj",
    backup_dir_relative=Path("world") / "obj" / "oldobjs",
    edit_line="/2",
    old_text="a harness token",
    new_text="a renamed token",
    list_line="/50",
    shown_prefix="(2) reference description :",
    backup_failure=BACKUP_FAILURE,
    watch=(Watch("load obj 1130", ("You create",)),),
    live_prefix="You create ",
)
SCRIPT_CASE = EditorCase(
    open_line="shape script 1100",
    prompt="Script: 1100>",
    release_marker="You released the script and stopped shaping.",
    file_relative=Path("world") / "scr" / "11.scr",
    backup_dir_relative=Path("world") / "scr" / "oldscrs",
    edit_line="/20",
    old_text="harness stub",
    new_text="renamed stub",
    list_line="/51",
    shown_prefix="Script #1100: ",
    backup_failure=BACKUP_FAILURE,
    # The script editor copies the script the game runs, so opening it shows the live name.
    watch=(
        Watch("shape script 1100", ("Script: 1100>",)),
        Watch("/51", ("Script #1100:",)),
        Watch("/free", ("You released the script and stopped shaping.",)),
    ),
    live_prefix="Script #1100: ",
)
EVERY_EDITOR = [ZONE_CASE, ROOM_CASE, MOB_CASE, OBJECT_CASE, SCRIPT_CASE]
# The editors whose /save prints a record of its own: every one but the zone editor, which
# saves one file per zone.
RECORD_EDITORS = [ROOM_CASE, MOB_CASE, OBJECT_CASE, SCRIPT_CASE]


def case_id(case: EditorCase) -> str:
    """The editor's name, from its open line, as the test id."""
    return case.open_line.split()[1]


def lines_of(text: str) -> list:
    """The text's lines without the carriage returns the game sends with each line end."""
    return [line.strip("\r") for line in text.split("\n")]


def reply_to(session: GameSession, line: str, outcomes: list) -> str:
    """Sends a line and returns what came back, up to and just after the first outcome.

    Editor prompts end in ':' or ']' mid-field, so command() cannot be used inside an editor.
    """
    session.send_line(line)
    return session.expect(outcomes) + session.drain(SETTLE_SECONDS)


def open_editor(implementor: GameSession, case: EditorCase) -> None:
    """Starts shaping the case's record."""
    opened = reply_to(implementor, case.open_line, [case.prompt, "could not open that file."])
    assert case.prompt in opened, opened


def edit_text_field(implementor: GameSession, case: EditorCase) -> None:
    """Types the case's new text into its one-line field."""
    prompt = reply_to(implementor, case.edit_line, ["Enter line", case.prompt])
    assert "Enter line" in prompt, prompt
    answered = reply_to(implementor, case.new_text, [case.prompt])
    assert case.prompt in answered, answered


def read_live(watcher: GameSession, case: EditorCase) -> str:
    """What the watcher reads of the case's record in the live game."""
    replies = [reply_to(watcher, step.line, list(step.markers)) for step in case.watch]
    return "".join(replies)


def file_text(path: Path) -> str:
    """A world file's text; the editors end lines with "\\n\\r", which read_text would double."""
    return path.read_bytes().decode("latin-1")


def record(text: str, vnum: int) -> str:
    """The record #vnum in a world file's text, up to the next record's '#'."""
    found = re.search(rf"#{vnum}(?!\d)(.*?)(?=#\d)", text, re.S)
    assert found, text
    return found.group(0)


@pytest.mark.parametrize("case", EVERY_EDITOR, ids=[case_id(case) for case in EVERY_EDITOR])
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, WATCHER)
def test_a_done_that_cannot_back_up_the_file_keeps_shaping_and_changes_nothing(server, case: EditorCase) -> None:
    """Pins #318: when /done cannot open the backup copy it says so, keeps the editor open with
    the edit, leaves the world file and the live game as they were, and a /done once the backup
    directory is back saves, implements and releases.

    The scenario removes the run's backup directory after login, then makes it again.
    """
    implementor = server.login(IMPLEMENTOR.name)
    watcher = server.login(WATCHER.name)
    went = watcher.command(f"goto {world.ROOM_SIGNPOST}")
    assert went.room_name() == ROOM_CASE.old_text, went.text
    backup_dir = server.lib_dir / case.backup_dir_relative
    world_file = server.lib_dir / case.file_relative
    shutil.rmtree(backup_dir)
    booted_bytes = world_file.read_bytes()

    open_editor(implementor, case)
    edit_text_field(implementor, case)
    failed = reply_to(implementor, "/done", [NOT_SAVED_LINES[1], case.release_marker])
    shown = reply_to(implementor, case.list_line, [case.prompt, NOT_SHAPING])
    live_after_failure = read_live(watcher, case)
    bytes_after_failure = world_file.read_bytes()
    backup_dir.mkdir()
    done = reply_to(implementor, "/done", [case.release_marker, NOT_SAVED_LINES[1], NOT_SHAPING])
    live_after_done = read_live(watcher, case)

    failed_lines = lines_of(failed)
    assert case.backup_failure in failed_lines, failed
    assert failed_lines[failed_lines.index(case.backup_failure) + 1:][:2] == NOT_SAVED_LINES, failed
    assert case.release_marker not in failed, failed
    assert case.shown_prefix + case.new_text in lines_of(shown), shown
    assert bytes_after_failure == booted_bytes
    assert case.live_prefix + case.old_text in live_after_failure, live_after_failure
    assert case.release_marker in lines_of(done), done
    assert NOT_SAVED_LINES[0] not in done, done
    assert case.live_prefix + case.new_text in live_after_done, live_after_done


# The vnum of each record editor's record, for finding it in the file.
RECORD_VNUMS = {ROOM_CASE: world.ROOM_SIGNPOST, MOB_CASE: 1130, OBJECT_CASE: 1130, SCRIPT_CASE: 1100}
# What /save prints for the record; only the room editor says anything (replace_room).
SAVE_REPLIES: dict = {ROOM_CASE: f"Saved as room #{world.ROOM_SIGNPOST}"}


@pytest.mark.parametrize("case", RECORD_EDITORS, ids=[case_id(case) for case in RECORD_EDITORS])
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR)
def test_save_rewrites_the_record_and_keeps_the_old_file_as_the_backup(server, case: EditorCase) -> None:
    """Pins the editors' /save with the backup directories the test world provides: the record
    in the world file holds the edit, and the backup copy holds the record as it was."""
    implementor = server.login(IMPLEMENTOR.name)
    vnum = RECORD_VNUMS[case]
    save_reply: Optional[str] = SAVE_REPLIES.get(case)

    open_editor(implementor, case)
    edit_text_field(implementor, case)
    saved = reply_to(implementor, "/save", [EDITOR_COMMAND_MARKER])
    # The game runs one line at a time, so once the listing answers, /save has finished writing.
    synced = reply_to(implementor, case.list_line, [case.prompt])
    released = reply_to(implementor, "/free", [case.release_marker])
    assert case.release_marker in released, released

    replies = saved + synced
    assert "could not" not in replies.lower(), replies
    if save_reply:
        assert save_reply in lines_of(replies), replies
    saved_record = record(file_text(server.lib_dir / case.file_relative), vnum)
    backup_file = server.lib_dir / case.backup_dir_relative / case.file_relative.name
    backup_record = record(file_text(backup_file), vnum)
    assert f"{case.new_text}~" in saved_record and f"{case.old_text}~" not in saved_record, saved_record
    assert f"{case.old_text}~" in backup_record, backup_record


# What a plain /add says for a record already in its file, before saving it (append_proto,
# append_object, append_script).
ADD_REPLIES = {
    MOB_CASE: "Mobile was already added to database. Saving.",
    OBJECT_CASE: "Object already added to database. Saving.",
    SCRIPT_CASE: "Script was already added to database. Saving.",
}
ADD_EDITORS = list(ADD_REPLIES)


@pytest.mark.parametrize("case", ADD_EDITORS, ids=[case_id(case) for case in ADD_EDITORS])
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR)
def test_a_plain_add_of_a_record_already_in_the_file_saves_it(server, case: EditorCase) -> None:
    """Pins #318: /add with no file name, on a record the file already holds, says so and saves
    the record in place rather than adding a second copy under a new vnum."""
    implementor = server.login(IMPLEMENTOR.name)
    vnum = RECORD_VNUMS[case]

    open_editor(implementor, case)
    edit_text_field(implementor, case)
    added = reply_to(implementor, "/add", [ADD_REPLIES[case], case.prompt])
    reply_to(implementor, case.list_line, [case.prompt])
    released = reply_to(implementor, "/free", [case.release_marker])
    assert case.release_marker in released, released

    assert ADD_REPLIES[case] in lines_of(added), added
    saved_text = file_text(server.lib_dir / case.file_relative)
    assert f"{case.new_text}~" in record(saved_text, vnum), saved_text
    assert saved_text.count(f"{case.new_text}~") == 1, saved_text


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR)
def test_add_in_the_zone_editor_says_there_is_nothing_to_add_to(server) -> None:
    """Pins #318: /add in the zone editor says a zone file holds one zone, and leaves the file
    as it was."""
    implementor = server.login(IMPLEMENTOR.name)
    zone_file = server.lib_dir / ZONE_CASE.file_relative
    booted_bytes = zone_file.read_bytes()

    open_editor(implementor, ZONE_CASE)
    added = reply_to(implementor, "/add", [ZONE_CASE.prompt])
    released = reply_to(implementor, "/free", [ZONE_CASE.release_marker])
    assert ZONE_CASE.release_marker in released, released

    assert "A zone file holds one zone, so there is nothing to add it to." in lines_of(added), added
    assert zone_file.read_bytes() == booted_bytes
