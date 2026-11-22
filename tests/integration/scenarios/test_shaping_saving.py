"""Saving from the five editors: a failed backup, /save's rewrite and a plain /add.

A save that cannot back the file up keeps the edits and changes nothing, /save rewrites the record
and keeps the old file, and a plain /add saves a record that is already in the file.
"""

from __future__ import annotations

import re
import shutil
from pathlib import Path
from typing import NamedTuple

import pytest

from rots_harness import shaping, world
from rots_harness.characters import IMPLEMENTOR, LEVEL_IMPLEMENTOR, immortal
from rots_harness.immortal_commands import CREATED_START, goto
from rots_harness.replies import reply_to
from rots_harness.session import GameSession
from rots_harness.text_lines import raw_lines

pytestmark = [pytest.mark.scenario, pytest.mark.shaping, pytest.mark.shaping_saving]

# A second implementor, who reads the live game from the signpost room while the first one shapes.
# Implementors enter the game in the immortal start room whatever their load room, so the
# scenario sends it to the signpost room.
WATCHER = immortal("Testwatch", LEVEL_IMPLEMENTOR)

# The two lines every editor's /done prints when its save fails (extra_coms_* in shape*.cpp).
NOT_SAVED_LINES = ["Not saved - still shaping. Fix the problem and /done again,", "or /free to discard."]
# What the mob editor's save prints when it cannot open the backup copy (replace_proto), worded
# unlike the other editors' shaping.NO_BACKUP_FILE_START.
MOB_BACKUP_FAILURE_REPLY = "ERROR: Could not open backup file"
# What an editor command gets once its editor has been released (shape_center, shapemob.cpp).
NOT_SHAPING_REPLY = "Use 'shape <mob|obj|room|script> <number>' to start shaping."


class Watch(NamedTuple):
    """One line the watcher types to read the live game, and the outcomes its reply ends at."""

    # The line the watcher types.
    line: str
    # The texts, any of which ends the reply.
    markers: tuple[str, ...]


class EditorCase(NamedTuple):
    """An editor opened on a test-world record, one text field edited, and where the edit shows."""

    # The line that starts shaping the record.
    open_line: str
    # The editor's prompt while no field is being answered (prompt_text in consts.cpp).
    prompt: str
    # What /free prints, and /done once it has saved.
    release_marker: str
    # The world file /save rewrites, relative to the lib (SHAPE_*_DIR, protos.h).
    file_relative: Path
    # Where /save copies the file first (SHAPE_*_BACKDIR).
    backup_dir_relative: Path
    # The field number of a one-line text field.
    edit_line: str
    # The field's text in the test world.
    old_text: str
    # What the scenario types into it.
    new_text: str
    # The editor command that shows the field.
    list_line: str
    # How that listing starts the field's line, before its text.
    shown_prefix: str
    # What the save prints without the backup directory.
    backup_failure: str
    # What the watcher types to see the live record, as Watch steps.
    watch: tuple[Watch, ...]
    # What precedes the field's text in the watcher's reply.
    live_prefix: str


# The zone editor on ZONE_NUMBER, editing the zone's name.
ZONE_CASE = EditorCase(
    open_line=f"shape zone {shaping.ZONE_NUMBER}",
    prompt=shaping.ZONE_PROMPT,
    release_marker=shaping.ZONE_RELEASED_REPLY,
    file_relative=shaping.ZONE_FILE_RELATIVE,
    backup_dir_relative=Path("world") / "zon" / "oldzons",
    edit_line="/20",
    old_text="Harness arena",
    new_text="Renamed arena",
    list_line="/51",
    shown_prefix=f"Zone #{shaping.ZONE_NUMBER}: ",
    backup_failure=shaping.NO_BACKUP_FILE_START,
    watch=(Watch(f"zreset {shaping.ZONE_NUMBER}", (": Harness arena.", ": Renamed arena.")),),
    live_prefix=": ",
)
# The room editor on the signpost room, editing the room's name.
ROOM_CASE = EditorCase(
    open_line=f"shape room {world.ROOM_SIGNPOST}",
    prompt=shaping.SIGNPOST_ROOM_PROMPT,
    release_marker=shaping.ROOM_RELEASED_REPLY,
    file_relative=Path("world") / "wld" / "11.wld",
    backup_dir_relative=Path("world") / "wld" / "oldroms",
    edit_line="/1",
    old_text="Signpost Room",
    new_text="Renamed Signpost",
    list_line="/50",
    shown_prefix="(1) name         :",
    backup_failure=shaping.NO_BACKUP_FILE_START,
    watch=(Watch("look", ("Exits are:",)),),
    live_prefix="",
)
# The mobile editor on SHAPED_RECORD, editing its reference description.
MOB_CASE = EditorCase(
    open_line=f"shape mob {shaping.SHAPED_RECORD}",
    prompt=shaping.MOBILE_PROMPT,
    release_marker=shaping.MOBILE_RELEASED_REPLY,
    file_relative=Path("world") / "mob" / "11.mob",
    backup_dir_relative=Path("world") / "mob" / "oldmobs",
    edit_line="/2",
    old_text="a target orc",
    new_text="a renamed orc",
    list_line="/50",
    shown_prefix="(2) reference description  :",
    backup_failure=MOB_BACKUP_FAILURE_REPLY,
    watch=(Watch(f"load mob {shaping.SHAPED_RECORD}", (CREATED_START,)),),
    live_prefix=f"{CREATED_START} ",
)
# The object editor on SHAPED_RECORD, editing its reference description.
OBJECT_CASE = EditorCase(
    open_line=f"shape object {shaping.SHAPED_RECORD}",
    prompt=shaping.OBJECT_PROMPT,
    release_marker=shaping.OBJECT_RELEASED_REPLY,
    file_relative=Path("world") / "obj" / "11.obj",
    backup_dir_relative=Path("world") / "obj" / "oldobjs",
    edit_line="/2",
    old_text=world.OBJECT_HARNESS_TOKEN_SHORT_DESCRIPTION,
    new_text="a renamed token",
    list_line="/50",
    shown_prefix="(2) reference description :",
    backup_failure=shaping.NO_BACKUP_FILE_START,
    watch=(Watch(f"load obj {shaping.SHAPED_RECORD}", (CREATED_START,)),),
    live_prefix=f"{CREATED_START} ",
)
# The script editor on SHAPED_SCRIPT, editing the script's name.
SCRIPT_CASE = EditorCase(
    open_line=f"shape script {shaping.SHAPED_SCRIPT}",
    prompt=shaping.SCRIPT_PROMPT,
    release_marker=shaping.SCRIPT_RELEASED_REPLY,
    file_relative=shaping.SCRIPT_FILE_RELATIVE,
    backup_dir_relative=Path("world") / "scr" / "oldscrs",
    edit_line="/20",
    old_text="harness stub",
    new_text="renamed stub",
    list_line="/51",
    shown_prefix=f"Script #{shaping.SHAPED_SCRIPT}: ",
    backup_failure=shaping.NO_BACKUP_FILE_START,
    # The script editor copies the script the game runs, so opening it shows the live name.
    watch=(
        Watch(f"shape script {shaping.SHAPED_SCRIPT}", (shaping.SCRIPT_PROMPT,)),
        Watch("/51", (f"Script #{shaping.SHAPED_SCRIPT}:",)),
        Watch("/free", (shaping.SCRIPT_RELEASED_REPLY,)),
    ),
    live_prefix=f"Script #{shaping.SHAPED_SCRIPT}: ",
)
# Every editor's case.
EVERY_EDITOR = [ZONE_CASE, ROOM_CASE, MOB_CASE, OBJECT_CASE, SCRIPT_CASE]
# The editors whose /save prints a record of its own: every one but the zone editor, which
# saves one file per zone.
RECORD_EDITORS = [ROOM_CASE, MOB_CASE, OBJECT_CASE, SCRIPT_CASE]


def case_id(case: EditorCase) -> str:
    """Return the editor's name, from its open line, as the test id."""
    open_words = case.open_line.split()
    return open_words[1]


def open_editor(implementor: GameSession, case: EditorCase) -> None:
    """Start shaping the case's record."""
    outcomes = [case.prompt, shaping.COULD_NOT_OPEN_FILE_REPLY]
    opened = reply_to(implementor, case.open_line, outcomes, settle=shaping.EDITOR_SETTLE_SECONDS)
    assert case.prompt in opened, opened


def edit_text_field(implementor: GameSession, case: EditorCase) -> None:
    """Type the case's new text into its one-line field."""
    prompt = reply_to(
        implementor, case.edit_line, ["Enter line", case.prompt], settle=shaping.EDITOR_SETTLE_SECONDS
    )
    assert "Enter line" in prompt, prompt
    answered = reply_to(implementor, case.new_text, [case.prompt], settle=shaping.EDITOR_SETTLE_SECONDS)
    assert case.prompt in answered, answered


def read_live(watcher: GameSession, case: EditorCase) -> str:
    """Return what the watcher reads of the case's record in the live game."""
    replies = [
        reply_to(watcher, step.line, list(step.markers), settle=shaping.EDITOR_SETTLE_SECONDS) for step in case.watch
    ]
    return "".join(replies)


def file_text(path: Path) -> str:
    """Return a world file's text; editors end lines with "\\n\\r", which read_text would double."""
    file_bytes = path.read_bytes()
    return file_bytes.decode("latin-1")


def record(text: str, vnum: int) -> str:
    """Return the record #vnum in a world file's text, up to the next record's '#'."""
    found = re.search(rf"#{vnum}(?!\d)(.*?)(?=#\d)", text, re.S)
    assert found, text
    return found.group(0)


@pytest.mark.parametrize("case", EVERY_EDITOR, ids=[case_id(case) for case in EVERY_EDITOR])
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, WATCHER)
def test_a_done_that_cannot_back_up_the_file_keeps_shaping_and_changes_nothing(server, case: EditorCase) -> None:
    """Pins a /done that cannot back up the file: it keeps shaping and changes nothing.

    When /done cannot open the backup copy it says so, keeps the editor open with the edit, and
    leaves the world file and the live game as they were; a /done once the backup directory is back
    saves, implements and releases. The scenario removes the run's backup directory after login,
    then makes it again.
    """
    implementor = server.login(IMPLEMENTOR.name)
    watcher = server.login(WATCHER.name)
    goto(watcher, world.ROOM_SIGNPOST, ROOM_CASE.old_text)
    backup_dir = server.lib_dir / case.backup_dir_relative
    world_file = server.lib_dir / case.file_relative
    shutil.rmtree(backup_dir)
    booted_bytes = world_file.read_bytes()

    open_editor(implementor, case)
    edit_text_field(implementor, case)
    failed_outcomes = [NOT_SAVED_LINES[1], case.release_marker]
    failed = reply_to(implementor, "/done", failed_outcomes, settle=shaping.EDITOR_SETTLE_SECONDS)
    shown_outcomes = [case.prompt, NOT_SHAPING_REPLY]
    shown = reply_to(implementor, case.list_line, shown_outcomes, settle=shaping.EDITOR_SETTLE_SECONDS)
    live_after_failure = read_live(watcher, case)
    bytes_after_failure = world_file.read_bytes()
    backup_dir.mkdir()
    done_outcomes = [case.release_marker, NOT_SAVED_LINES[1], NOT_SHAPING_REPLY]
    done = reply_to(implementor, "/done", done_outcomes, settle=shaping.EDITOR_SETTLE_SECONDS)
    live_after_done = read_live(watcher, case)

    failed_lines = raw_lines(failed)
    assert case.backup_failure in failed_lines, failed
    assert failed_lines[failed_lines.index(case.backup_failure) + 1:][:2] == NOT_SAVED_LINES, failed
    assert case.release_marker not in failed, failed
    assert case.shown_prefix + case.new_text in raw_lines(shown), shown
    assert bytes_after_failure == booted_bytes, f"{world_file} changed after the failed /done"
    assert case.live_prefix + case.old_text in live_after_failure, live_after_failure
    assert case.release_marker in raw_lines(done), done
    assert NOT_SAVED_LINES[0] not in done, done
    assert case.live_prefix + case.new_text in live_after_done, live_after_done


# The vnum of each record editor's record, for finding it in the file.
RECORD_VNUMS = {
    ROOM_CASE: world.ROOM_SIGNPOST,
    MOB_CASE: shaping.SHAPED_RECORD,
    OBJECT_CASE: shaping.SHAPED_RECORD,
    SCRIPT_CASE: shaping.SHAPED_SCRIPT,
}
# What /save prints for the record; only the room editor says anything (replace_room).
SAVE_REPLIES: dict[EditorCase, str] = {ROOM_CASE: f"Saved as room #{world.ROOM_SIGNPOST}"}


@pytest.mark.parametrize("case", RECORD_EDITORS, ids=[case_id(case) for case in RECORD_EDITORS])
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR)
def test_save_rewrites_the_record_and_keeps_the_old_file_as_the_backup(server, case: EditorCase) -> None:
    """Pins the editors' /save: the world file's record holds the edit, the backup the old record.

    The backup directories are the ones the test world provides.
    """
    implementor = server.login(IMPLEMENTOR.name)
    vnum = RECORD_VNUMS[case]
    save_reply: str | None = SAVE_REPLIES.get(case)

    open_editor(implementor, case)
    edit_text_field(implementor, case)
    saved = reply_to(implementor, "/save", [shaping.EDITOR_COMMAND_PART], settle=shaping.EDITOR_SETTLE_SECONDS)
    # The game runs one line at a time, so once the listing answers, /save has finished writing.
    synced = reply_to(implementor, case.list_line, [case.prompt], settle=shaping.EDITOR_SETTLE_SECONDS)
    shaping.release_editor(implementor, case.release_marker, settle=shaping.EDITOR_SETTLE_SECONDS)

    replies = saved + synced
    assert "could not" not in replies.lower(), replies
    if save_reply:
        assert save_reply in raw_lines(replies), replies

    saved_text = file_text(server.lib_dir / case.file_relative)
    saved_record = record(saved_text, vnum)
    backup_file = server.lib_dir / case.backup_dir_relative / case.file_relative.name
    backup_text = file_text(backup_file)
    backup_record = record(backup_text, vnum)
    assert f"{case.new_text}~" in saved_record and f"{case.old_text}~" not in saved_record, saved_record
    assert f"{case.old_text}~" in backup_record, backup_record


# What a plain /add says for a record already in its file, before saving it (append_proto,
# append_object, append_script).
ADD_REPLIES = {
    MOB_CASE: "Mobile was already added to database. Saving.",
    OBJECT_CASE: "Object already added to database. Saving.",
    SCRIPT_CASE: "Script was already added to database. Saving.",
}
# The editors whose /add saves a record already in its file.
ADD_EDITORS = list(ADD_REPLIES)


@pytest.mark.parametrize("case", ADD_EDITORS, ids=[case_id(case) for case in ADD_EDITORS])
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR)
def test_a_plain_add_of_a_record_already_in_the_file_saves_it(server, case: EditorCase) -> None:
    """Pins a plain /add of a record already in its file: it says so and saves it in place.

    /add with no file name does not add a second copy under a new vnum.
    """
    implementor = server.login(IMPLEMENTOR.name)
    vnum = RECORD_VNUMS[case]

    open_editor(implementor, case)
    edit_text_field(implementor, case)
    added = reply_to(implementor, "/add", [ADD_REPLIES[case], case.prompt], settle=shaping.EDITOR_SETTLE_SECONDS)
    reply_to(implementor, case.list_line, [case.prompt], settle=shaping.EDITOR_SETTLE_SECONDS)
    shaping.release_editor(implementor, case.release_marker, settle=shaping.EDITOR_SETTLE_SECONDS)

    assert ADD_REPLIES[case] in raw_lines(added), added
    saved_text = file_text(server.lib_dir / case.file_relative)
    assert f"{case.new_text}~" in record(saved_text, vnum), saved_text
    assert saved_text.count(f"{case.new_text}~") == 1, saved_text


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR)
def test_add_in_the_zone_editor_says_there_is_nothing_to_add_to(server) -> None:
    """Pins /add in the zone editor: it says a zone file holds one zone; the file is unchanged."""
    implementor = server.login(IMPLEMENTOR.name)
    zone_file = server.lib_dir / ZONE_CASE.file_relative
    booted_bytes = zone_file.read_bytes()

    open_editor(implementor, ZONE_CASE)
    added = reply_to(implementor, "/add", [ZONE_CASE.prompt], settle=shaping.EDITOR_SETTLE_SECONDS)
    shaping.release_editor(implementor, ZONE_CASE.release_marker, settle=shaping.EDITOR_SETTLE_SECONDS)

    assert "A zone file holds one zone, so there is nothing to add it to." in raw_lines(added), added
    assert zone_file.read_bytes() == booted_bytes, f"{zone_file} changed after /add"
