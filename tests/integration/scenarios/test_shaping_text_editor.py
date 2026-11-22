"""The text editor's command list when it opens, and %f in the mob options field.

The mob options field, one setting per line, neither offers nor accepts %f.
"""

from __future__ import annotations

import pytest

from rots_harness import world
from rots_harness.characters import IMPLEMENTOR, Character
from rots_harness.game_text import TEXT_EDITOR_CURSOR, TEXT_EDITOR_PROMPT, TEXT_SO_FAR
from rots_harness.replies import reply_to
from rots_harness.session import GameSession
from rots_harness.shaping import (
    COULD_NOT_OPEN_FILE_REPLY,
    EDITOR_SETTLE_SECONDS,
    MOBILE_PROMPT,
    MOBILE_RELEASED_REPLY,
    SHAPED_RECORD,
    release_editor,
)
from rots_harness.text_lines import raw_lines

pytestmark = [pytest.mark.scenario, pytest.mark.shaping, pytest.mark.shaping_prompts]

# A level-10 human warrior in the arena: a mortal editing its own description.
MORTAL = Character(
    name="Testwriter",
    race="human",
    sex="female",
    level=10,
    load_room=world.ROOM_ARENA_CENTRE,
    class_name="warrior",
)
# The opening lines every text editor shows before the line about %f (string_add_commands,
# modify.cpp).
COMMAND_LIST_HEAD = [
    "Edit the text now. Commands, each at the start of a line:",
    "  %e save and exit           %q abort",
    "  %d delete the last line    %d<+/-n> delete n lines back/forward",
    "  %l cursor to the end       %l<n> cursor after line n",
    "  %r redisplay the text      %s<old>~<new> replace everywhere",
]
# The list's line about %f and %%.
FORMAT_LINE = "  %f format the text         %% a % sign"
# The list's line about %h, for text with no help entry of its own.
GENERAL_HELP_LINE = "  %h more on these commands"
# The mob options field's line in place of FORMAT_LINE.
PERCENT_ONLY_LINE = "  %% a % sign"
# The mob options field's %h line, which offers the field's own help entry first.
FIELD_HELP_LINE = "  %h help for this field, and more on these commands"
# What %f prints in the mob options field (string_add, modify.cpp).
FORMAT_REFUSED_REPLY = "This text is one setting per line, so %f is not allowed here."
# The ways to type format: the command letter in either case, after '%' or '~'.
FORMAT_COMMANDS = ["%f", "%F", "~f"]
# Two settings typed into the options, which a format would join into one line.
OPTION_LINES = ["first setting", "second setting"]
# What %f prints in any other text that is still empty.
NOTHING_TO_FORMAT_REPLY = "What are you trying to format?"
# The start of what %f prints once it has formatted a text.
FORMATTED_START = "Your text is formatted."

# What /simple prints when it leaves simple mode, where field 42 does not exist.
EXTENDED_MODE_REPLY = "You switched to extended editing mode."
# What /42 prints before it opens the text editor on the mob options.
OPTIONS_FIELD_REPLY = "You are about to change OPTIONS, settings for the mob program, one per line:"


def open_mob_options(implementor: GameSession) -> str:
    """Open SHAPED_RECORD's mob options in the text editor and return what the editor printed."""
    outcomes = [MOBILE_PROMPT, COULD_NOT_OPEN_FILE_REPLY, "No such mob"]
    opened = reply_to(implementor, f"shape mob {SHAPED_RECORD}", outcomes, settle=EDITOR_SETTLE_SECONDS)
    assert MOBILE_PROMPT in opened, opened
    extended = reply_to(implementor, "/simple", ["editing mode."], settle=EDITOR_SETTLE_SECONDS)
    assert EXTENDED_MODE_REPLY in extended, extended
    return reply_to(implementor, "/42", [OPTIONS_FIELD_REPLY, MOBILE_PROMPT], settle=EDITOR_SETTLE_SECONDS)


def close_mob_editor(implementor: GameSession) -> None:
    """Abandon the options text and leave the editor, so the session can quit."""
    implementor.send_line("%q")
    release_editor(implementor, MOBILE_RELEASED_REPLY, settle=EDITOR_SETTLE_SECONDS)


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(MORTAL)
def test_a_mortals_description_editor_lists_its_commands_and_q_leaves_it(server) -> None:
    """Pins `set description`'s editor: it lists its commands, %f among them, and %q leaves it.

    After %q the next line is a command again.
    """
    mortal = server.login(MORTAL.name)

    opened = reply_to(mortal, "set description", [GENERAL_HELP_LINE], settle=EDITOR_SETTLE_SECONDS)
    mortal.send_line("%q")
    # Still in the editor, look would only add a line to the text and get the editor's prompt.
    look_outcomes = [world.ROOM_ARENA_CENTRE_NAME, TEXT_EDITOR_PROMPT]
    looked = reply_to(mortal, "look", look_outcomes, settle=EDITOR_SETTLE_SECONDS)

    opened_lines = raw_lines(opened)
    start = opened_lines.index(COMMAND_LIST_HEAD[0]) if COMMAND_LIST_HEAD[0] in opened_lines else -1
    expected = COMMAND_LIST_HEAD + [FORMAT_LINE, GENERAL_HELP_LINE]
    assert start >= 0 and opened_lines[start:start + len(expected)] == expected, opened
    assert world.ROOM_ARENA_CENTRE_NAME in looked, looked


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR)
def test_the_mob_options_editor_does_not_offer_format(server) -> None:
    """Pins the command list for the mob options field (/42): no %f line, only %%."""
    implementor = server.login(IMPLEMENTOR.name)

    opened = open_mob_options(implementor)
    close_mob_editor(implementor)

    opened_lines = raw_lines(opened)
    start = opened_lines.index(COMMAND_LIST_HEAD[0]) if COMMAND_LIST_HEAD[0] in opened_lines else -1
    expected = COMMAND_LIST_HEAD + [PERCENT_ONLY_LINE, FIELD_HELP_LINE]
    assert start >= 0 and opened_lines[start:start + len(expected)] == expected, opened
    assert not any("%f" in line for line in opened_lines), opened


@pytest.mark.parametrize("format_command", FORMAT_COMMANDS)
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR)
def test_the_mob_options_editor_refuses_format(server, format_command: str) -> None:
    """Pins that format in the mob options field is refused, however it is typed.

    Joining the lines would merge the settings; the text keeps one setting per line.
    """
    implementor = server.login(IMPLEMENTOR.name)

    opened = open_mob_options(implementor)
    assert OPTIONS_FIELD_REPLY in opened, opened
    for option_line in OPTION_LINES:
        implementor.send_line(option_line)
    format_outcomes = [FORMAT_REFUSED_REPLY, NOTHING_TO_FORMAT_REPLY, FORMATTED_START]
    formatted = reply_to(implementor, format_command, format_outcomes, settle=EDITOR_SETTLE_SECONDS)
    shown = reply_to(implementor, "%r", [TEXT_EDITOR_CURSOR], settle=EDITOR_SETTLE_SECONDS)
    close_mob_editor(implementor)

    assert FORMAT_REFUSED_REPLY in raw_lines(formatted), formatted
    shown_lines = raw_lines(shown)
    start = shown_lines.index(TEXT_SO_FAR) if TEXT_SO_FAR in shown_lines else -1
    shown_text = [line for line in shown_lines[start + 1:] if line] if start >= 0 else []
    assert shown_text[:3] == OPTION_LINES + [TEXT_EDITOR_CURSOR], shown
