"""Disabled shaping commands each refuse, quoting what was typed; shape master_mobile and
master_object without a number show their usage; a builder needs level 93 for full permission over
a zone open to all; and the mob editor's /recalculate needs six letters."""

from __future__ import annotations

from typing import NamedTuple

import pytest

from rots_harness import world
from rots_harness.characters import IMPLEMENTOR, Character
from rots_harness.session import GameSession

pytestmark = [pytest.mark.scenario, pytest.mark.shaping, pytest.mark.shaping_commands]


class Editor(NamedTuple):
    """An editor opened on a test-world record, and what it prints as it opens and closes."""

    open_line: str  # starts shaping the record
    prompt: str  # the prompt while editing (prompt_text in consts.cpp), first shown once the record loads
    release_marker: str  # what /free prints


# The mob, object, room and script editors on the test world's first record of each kind.
MOB_EDITOR = Editor("shape mob 1130", "Mobile: 1130>", "You released the mobile and stopped shaping.")
OBJECT_EDITOR = Editor("shape object 1130", "Object: 1130>", "You released an object and stopped shaping.")
ROOM_EDITOR = Editor("shape room 1101", "Shaping: 1101>", "You released the room and stopped shaping.")
SCRIPT_EDITOR = Editor("shape script 1100", "Script: 1100>", "You released the script and stopped shaping.")

# What a loader prints when the record or its file is missing (load_proto, load_object,
# load_room and load_script in the shape*.cpp files).
LOAD_FAILURE_MARKERS = ["could not open that file.", "No such mob or file corrupted.", "no object here.", "no room here."]
# Seconds to keep reading after a marker, for the prompt that follows it.
SETTLE_SECONDS = 0.5


def refusal(typed: str) -> str:
    """What shape_disabled (shapemob.cpp) prints for a command typed as typed."""
    return f'"{typed}" has been disabled due to a bug.'


def lines_of(text: str) -> list:
    """The text's lines without the carriage returns the game sends with each line end."""
    return [line.strip("\r") for line in text.split("\n")]


def reply_to(session: GameSession, line: str, outcomes: list) -> str:
    """Sends a line and returns what came back, up to and just after the first outcome."""
    session.send_line(line)
    return session.expect(outcomes) + session.drain(SETTLE_SECONDS)


# The disabled commands typed at the game prompt; the reason for each is beside its
# shape_disabled call (do_shape, shapemob.cpp).
COMMAND_LINE_CASES = ["shape mob new", "shape object new", "shape room new", "shape script new", "shape recalc_mobile"]

# The disabled commands typed inside an editor; the reason for each is beside its shape_disabled
# call in that editor's extra_coms function (the script editor's /12 is in shape_center_script).
# Only the room editor refuses a bare /add; the others refuse /add with a file name.
EDITOR_CASES = [
    (MOB_EDITOR, "/new"),
    (MOB_EDITOR, "/delete"),
    (MOB_EDITOR, "/add x"),
    (OBJECT_EDITOR, "/new"),
    (OBJECT_EDITOR, "/delete"),
    (OBJECT_EDITOR, "/add x"),
    (ROOM_EDITOR, "/new"),
    (ROOM_EDITOR, "/add"),
    (ROOM_EDITOR, "/delete"),
    (SCRIPT_EDITOR, "/new"),
    (SCRIPT_EDITOR, "/delete"),
    (SCRIPT_EDITOR, "/add x"),
    (SCRIPT_EDITOR, "/12"),
]


@pytest.mark.parametrize("typed", COMMAND_LINE_CASES)
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR)
def test_a_disabled_shape_command_refuses_with_what_was_typed(server, typed: str) -> None:
    """Pins #318: a disabled `shape` command starts no editor and says it is disabled, quoting
    the command as typed."""
    implementor = server.login(IMPLEMENTOR.name)

    reply = implementor.command(typed)

    assert refusal(typed) in lines_of(reply.text), reply.text
    assert "You start shaping" not in reply.text, reply.text


@pytest.mark.parametrize(
    ("editor", "typed"),
    EDITOR_CASES,
    ids=[f"{editor.open_line.split()[1]}-{typed}" for editor, typed in EDITOR_CASES],
)
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR)
def test_a_disabled_editor_command_refuses_with_what_was_typed(server, editor: Editor, typed: str) -> None:
    """Pins #318: inside an editor, a disabled command says it is disabled, quoting the command
    as typed, and the editor stays open on its record."""
    implementor = server.login(IMPLEMENTOR.name)

    opened = reply_to(implementor, editor.open_line, [editor.prompt, *LOAD_FAILURE_MARKERS])
    assert editor.prompt in opened, opened
    reply = reply_to(implementor, typed, [editor.prompt, editor.release_marker])
    released = reply_to(implementor, "/free", [editor.release_marker])
    assert editor.release_marker in released, released

    assert refusal(typed) in lines_of(reply), reply
    assert editor.prompt in reply, reply


# The two master commands, with the word each uses in its replies (do_shape, shapemob.cpp).
MASTER_CASES = [("master_mobile", "Mobile"), ("master_object", "Object")]


@pytest.mark.parametrize(("command", "kind"), MASTER_CASES, ids=[command for command, kind in MASTER_CASES])
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR)
def test_shape_master_without_a_number_shows_its_usage(server, command: str, kind: str) -> None:
    """Pins #318: `shape master_mobile` or `shape master_object` with no number names the current
    master, shows the usage line and leaves the master as it was."""
    implementor = server.login(IMPLEMENTOR.name)

    reply = implementor.command(f"shape {command}")

    reply_lines = lines_of(reply.text)
    assert f"Usage: shape {command} <idnum>" in reply_lines, reply.text
    assert any(line.startswith(f"{kind} master is player #") for line in reply_lines), reply.text
    assert f"{kind} permission set to player" not in reply.text, reply.text


def immortal(name: str, level: int) -> Character:
    """An immortal human warrior of the given level, entering at the immortal start."""
    return Character(name=name, race="human", sex="male", level=level, load_room=world.ROOM_IMMORTAL_START, class_name="warrior")


# Immortals just below and at the level that gets full permission over a zone without owners
# (LEVEL_GOD, structs.h).
LEVEL_92_BUILDER = immortal("Testlower", 92)
LEVEL_93_BUILDER = immortal("Testgod", 93)
# What get_permission (shapemob.cpp) prints when it gives limited permission.
LIMITED_PERMISSION = "You have no permission to this zone. Limited permission only."


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(LEVEL_92_BUILDER, LEVEL_93_BUILDER)
def test_a_zone_open_to_all_gives_full_permission_from_level_93(server) -> None:
    """Pins get_permission as #318 left it, no longer writing into another editor's state: in a
    zone without owners, a level-92 builder shaping a room is told it has limited permission, and
    a level-93 builder is not."""
    lower = server.login(LEVEL_92_BUILDER.name)
    god = server.login(LEVEL_93_BUILDER.name)

    lower_opened = reply_to(lower, ROOM_EDITOR.open_line, [ROOM_EDITOR.prompt, *LOAD_FAILURE_MARKERS])
    god_opened = reply_to(god, ROOM_EDITOR.open_line, [ROOM_EDITOR.prompt, *LOAD_FAILURE_MARKERS])
    for builder in (lower, god):
        released = reply_to(builder, "/free", [ROOM_EDITOR.release_marker])
        assert ROOM_EDITOR.release_marker in released, released

    assert ROOM_EDITOR.prompt in lower_opened and ROOM_EDITOR.prompt in god_opened, lower_opened + god_opened
    # The room loader prints the file's name with no line end, so the notice follows it on its line.
    assert LIMITED_PERMISSION in lower_opened, lower_opened
    assert LIMITED_PERMISSION not in god_opened, god_opened


# What /recalculate prints, and what the editor prints for a command it does not know
# (extra_coms_proto, shapemob.cpp).
RECALCULATED = "You set parameters of your mobile to standard."
UNKNOWN_EDITOR_COMMAND = "Possible commands are:"
# Mob 1130's fields before and after /recalculate, as the extended /50 list shows them: level 5
# gives OB 5 * 4, parry 5 * 2, dodge 5, damage 1 + (5 + 1) / 2 and xp 900 + 600 * 5 * 5
# (recalculate_mob, shapemob.cpp).
BOOTED_FIELDS = ["(9) OB:10,   parry:0, dodge:0", "(11) damage       :2", "(14) exp          :5000"]
RECALCULATED_FIELDS = ["(9) OB:20,   parry:10, dodge:5", "(11) damage       :4", "(14) exp          :15900"]


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR)
def test_recalculate_needs_six_letters_and_sets_a_mobs_numbers_from_its_level(server) -> None:
    """Pins #318: the mob editor takes /recalculate from its first six letters, so a shorter
    /rec is an unknown command that changes nothing, and /recalc sets the mob's combat numbers
    from its level."""
    implementor = server.login(IMPLEMENTOR.name)

    opened = reply_to(implementor, MOB_EDITOR.open_line, [MOB_EDITOR.prompt, *LOAD_FAILURE_MARKERS])
    assert MOB_EDITOR.prompt in opened, opened
    extended = reply_to(implementor, "/simple", ["editing mode."])
    assert "You switched to extended editing mode." in extended, extended
    short = reply_to(implementor, "/rec", [UNKNOWN_EDITOR_COMMAND, RECALCULATED])
    after_short = reply_to(implementor, "/50", ["(42) options"])
    recalculated = reply_to(implementor, "/recalc", [RECALCULATED, UNKNOWN_EDITOR_COMMAND])
    after_recalc = reply_to(implementor, "/50", ["(42) options"])
    released = reply_to(implementor, "/free", [MOB_EDITOR.release_marker])
    assert MOB_EDITOR.release_marker in released, released

    assert UNKNOWN_EDITOR_COMMAND in lines_of(short), short
    assert all(field in lines_of(after_short) for field in BOOTED_FIELDS), after_short
    assert RECALCULATED in lines_of(recalculated), recalculated
    assert all(field in lines_of(after_recalc) for field in RECALCULATED_FIELDS), after_recalc
