"""The editors' number prompts refuse values out of their range and keep the field, the mob
editor's -N answers set or subtract as each field says, and the name prompts refuse %q."""

from __future__ import annotations

from typing import NamedTuple

import pytest

from rots_harness.characters import IMPLEMENTOR
from rots_harness.session import GameSession

pytestmark = [pytest.mark.scenario, pytest.mark.shaping, pytest.mark.shaping_prompts]


class Refusal(NamedTuple):
    """One answer a prompt refuses, what it says, and how the editor's list shows the field after."""

    field_line: str  # the field number typed at the editor prompt
    answer: str  # what the scenario answers
    refusal: str  # what the editor prints for that answer
    listed: str  # the field's line in the editor's list afterwards, unchanged


class Editor(NamedTuple):
    """An editor opened on a test-world record."""

    open_lines: tuple  # what starts shaping the record and gets it ready for the fields
    prompt: str  # the editor's prompt while no field is being answered (prompt_text in consts.cpp)
    list_line: str  # the editor command that lists the record
    release_marker: str  # what /free prints


# Mob 1130 in extended mode, where the fields have their extended numbers (list_help, shapemob.cpp).
MOB_EDITOR = Editor(
    ("shape mob 1130", "/simple"), "Mobile: 1130>", "/50", "You released the mobile and stopped shaping."
)
OBJECT_EDITOR = Editor(("shape object 1130",), "Object: 1130>", "/50", "You released an object and stopped shaping.")
ROOM_EDITOR = Editor(("shape room 1101",), "Shaping: 1101>", "/50", "You released the room and stopped shaping.")
ZONE_EDITOR = Editor(("shape zone 11",), "Zone: 11>", "/51", "You released your job and stop shaping.")
SCRIPT_EDITOR = Editor(("shape script 1100",), "Script: 1100>", "/51", "You released the script and stopped shaping.")

# Each editor's range refusals (the checks beside each message in shape_center_proto,
# shape_center_obj, shape_center_room and shape_center_zone).
REFUSAL_CASES = [
    (
        MOB_EDITOR,
        [
            Refusal("/19", "21", "Race must be 0-20. dropped.", "(19) race         :13"),
            Refusal("/16", "7", "Position must be 4, 5, 6 or 8. dropped.", "(16) position     :8"),
            Refusal("/26", "16", "Body type must be 0-15. dropped.", "(26) bodytype     :1"),
        ],
    ),
    (
        OBJECT_EDITOR,
        [
            Refusal("/9", "27", "Item type must be 1-26. dropped.", "(9) type flag    :12"),
            Refusal("/16", "256", "Level must be 0-255. dropped.", "(16) level        :0"),
            Refusal("/18", "14", "Material must be 0-13. dropped.", "(18) material     :0 (Usual stuff)"),
        ],
    ),
    (
        ROOM_EDITOR,
        [
            Refusal("/4", "13", "Sector type must be 0-12. dropped.", "(4) sector type   :0"),
            Refusal("/17", "256", "Room level must be 0-255. dropped.", "(17) Room level: 0"),
        ],
    ),
    (
        ZONE_EDITOR,
        [
            Refusal("/24", "7", "Reset mode must be 0-3. dropped.", "Reset time: 999; Reset mode: 2"),
            Refusal("/14", "0", "Nothing entered. dropped.", "Owners: All"),
        ],
    ),
]

# Each name prompt that refuses %q, what it says, and how the list shows the name after
# (shape_center_proto and shape_center_obj field 1, shape_center_script field 20).
EMPTY_NAME_CASES = [
    (MOB_EDITOR, Refusal("/1", "%q", "Aliases can't be empty.", "(1) alias(es)         :target orc")),
    (OBJECT_EDITOR, Refusal("/1", "%q", "Aliases can't be empty.", "(1) alias(es)    :harness token")),
    (SCRIPT_EDITOR, Refusal("/20", "%q", "The script name can't be empty.", "Script #1100: harness stub")),
]

# How mob 1130's list shows its alignment and damage after the -N answers: alignment -500 set to
# -300, damage 2 less 1.
NEGATIVE_ALIGNMENT_LISTED = "(7) alignment    :-300"
SUBTRACTED_DAMAGE_LISTED = "(11) damage       :1"
# The last character of every field's prompt (Dgt] or Lin], prompt_text in consts.cpp).
FIELD_PROMPT_END = "]"
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


def open_editor(implementor: GameSession, editor: Editor) -> None:
    """Starts shaping the editor's record and gets it ready for the fields."""
    for line in editor.open_lines:
        opened = reply_to(implementor, line, [editor.prompt, "could not open that file."])
        assert editor.prompt in opened, opened


def close_editor(implementor: GameSession, editor: Editor) -> None:
    """Leaves the editor, so the session can quit when the test ends."""
    released = reply_to(implementor, "/free", [editor.release_marker])
    assert editor.release_marker in released, released


def answer_field(implementor: GameSession, editor: Editor, field_line: str, answer: str) -> str:
    """Opens a field's prompt, answers it, and returns the reply up to the editor prompt."""
    prompt = reply_to(implementor, field_line, [FIELD_PROMPT_END, editor.prompt])
    assert FIELD_PROMPT_END in prompt, prompt
    return reply_to(implementor, answer, [editor.prompt])


def editor_id(editor: Editor) -> str:
    """The editor's name, from its open line, as the test id."""
    return editor.open_lines[0].split()[1]


@pytest.mark.parametrize(
    ("editor", "refusals"), REFUSAL_CASES, ids=[editor_id(editor) for editor, refusals in REFUSAL_CASES]
)
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR)
def test_a_number_out_of_its_fields_range_is_refused_and_the_field_kept(server, editor: Editor, refusals: list) -> None:
    """Pins #318: each of these number prompts refuses a value outside its range, says what the
    range is, and leaves the field as it was."""
    implementor = server.login(IMPLEMENTOR.name)

    open_editor(implementor, editor)
    replies = [answer_field(implementor, editor, refusal.field_line, refusal.answer) for refusal in refusals]
    listing = reply_to(implementor, editor.list_line, [editor.prompt])
    close_editor(implementor, editor)

    listing_lines = lines_of(listing)
    for refusal, reply in zip(refusals, replies):
        assert refusal.refusal in lines_of(reply), reply
        assert refusal.listed in listing_lines, listing


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR)
def test_a_minus_answer_sets_alignment_negative_but_subtracts_from_damage(server) -> None:
    """Pins #318: the alignment prompt, whose values run negative, takes -300 as the value; the
    damage prompt takes -1 as one less (string_to_negative_value and string_to_new_value,
    utility.cpp)."""
    implementor = server.login(IMPLEMENTOR.name)

    open_editor(implementor, MOB_EDITOR)
    answer_field(implementor, MOB_EDITOR, "/7", "-300")
    answer_field(implementor, MOB_EDITOR, "/11", "-1")
    listing = reply_to(implementor, MOB_EDITOR.list_line, [MOB_EDITOR.prompt])
    close_editor(implementor, MOB_EDITOR)

    listing_lines = lines_of(listing)
    assert NEGATIVE_ALIGNMENT_LISTED in listing_lines, listing
    assert SUBTRACTED_DAMAGE_LISTED in listing_lines, listing


@pytest.mark.parametrize(
    ("editor", "refusal"), EMPTY_NAME_CASES, ids=[editor_id(editor) for editor, refusal in EMPTY_NAME_CASES]
)
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR)
def test_q_at_a_name_prompt_is_refused_and_the_name_kept(server, editor: Editor, refusal: Refusal) -> None:
    """Pins #318: %q, which empties most text fields, is refused where an empty name would leave
    the record unusable, and the name stays."""
    implementor = server.login(IMPLEMENTOR.name)

    open_editor(implementor, editor)
    reply = answer_field(implementor, editor, refusal.field_line, refusal.answer)
    listing = reply_to(implementor, editor.list_line, [editor.prompt])
    close_editor(implementor, editor)

    assert refusal.refusal in lines_of(reply), reply
    assert refusal.listed in lines_of(listing), listing
