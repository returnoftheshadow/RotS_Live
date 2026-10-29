"""The script editor: removing the first row then inserting before the new first, swapping rows
and saving their new numbers, a section row, and a list range over rows numbered out of order."""

from __future__ import annotations

from pathlib import Path

import pytest

from rots_harness.characters import IMPLEMENTOR
from rots_harness.session import GameSession

pytestmark = [pytest.mark.scenario, pytest.mark.shaping, pytest.mark.shaping_script_editor]

# The test world's scripts: #1180 has three ON_ENTER rows numbered 1, 2 and 3 with the texts
# first, second and third; #1181 has rows numbered 1, 2, 4 and 3 with the texts one, two, four
# and three (tests/integration/world/scr/11.scr).
ORDERED_SCRIPT = 1180
OUT_OF_ORDER_SCRIPT = 1181
# The script file /save rewrites, relative to the lib (SHAPE_SCRIPT_DIR, protos.h).
SCRIPT_FILE_RELATIVE = Path("world") / "scr" / "11.scr"
# What /free prints (extra_coms_script).
RELEASED_MARKER = "You released the script and stopped shaping."
# What /9, /11 and /13 print (shape_center_script).
ROW_REMOVED = "Command removed."
ROW_INSERTED_BEFORE = "New command added before current, and selected."
ROWS_SWITCHED = "Switched the current and the next commands,"
# How /50 lists an ON_ENTER row and a row with no command yet (format_command, shapescript.cpp).
NO_COMMAND_TEXT = "*** No command: script will terminate here ***"
# The footer /50 ends with while no range is set (shape_range_footer, shapemob.cpp).
WHOLE_LIST_FOOTER = "List range: all (/52 to change)."
# The section the scenario adds, and the width of an editor list line (SHAPE_LIST_WIDTH, protos.h).
SECTION_TITLE = "Greeting"
SECTION_COMMENT = "what a visitor hears"
LIST_WIDTH = 78
# The type name /3 takes for a section, and its title and comment prompts' first words
# (shape_section_prompt, shapemob.cpp).
SECTION_TYPE = "SECTION"
SECTION_TITLE_PROMPT = "Enter section TITLE"
SECTION_COMMENT_PROMPT = "Enter section COMMENT"
# Seconds to keep reading after a marker, for the rest of the reply.
SETTLE_SECONDS = 0.5


def on_enter_row(number: int, text: str) -> str:
    """How /50 lists an ON_ENTER row."""
    return f"[{number}] TRIG ON_ENTER        ({text})"


def lines_of(text: str) -> list:
    """The text's lines without the carriage returns the game sends with each line end."""
    return [line.strip("\r") for line in text.split("\n")]


def editor_step(implementor: GameSession, line: str, outcomes: list) -> str:
    """Sends one editor line and returns what came back, up to and just after the first outcome.

    Editor prompts end in ':' or ']' mid-field, so command() cannot be used inside the editor.
    """
    implementor.send_line(line)
    return implementor.expect(outcomes) + implementor.drain(SETTLE_SECONDS)


def prompt(script_number: int) -> str:
    """The script editor's prompt while no field is being answered (prompt_text in consts.cpp)."""
    return f"Script: {script_number}>"


def open_script(implementor: GameSession, script_number: int) -> None:
    """Starts shaping a script, with its first row selected."""
    opened = editor_step(implementor, f"shape script {script_number}", [prompt(script_number), "could not open that file."])
    assert prompt(script_number) in opened, opened


def close_script(implementor: GameSession) -> None:
    """Leaves the editor, so the session can quit when the test ends."""
    released = editor_step(implementor, "/free", [RELEASED_MARKER])
    assert RELEASED_MARKER in released, released


def listed_rows(listing: str) -> list:
    """The row lines of a /50 listing, in order."""
    return [line for line in lines_of(listing) if line.startswith("[")]


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR)
def test_a_row_inserted_before_the_first_after_removing_the_first_heads_the_script(server) -> None:
    """Pins #368: /9 on the first row makes the next row first, with no row before it, so /11
    then inserts a new row ahead of it at the head of the script."""
    implementor = server.login(IMPLEMENTOR.name)

    open_script(implementor, ORDERED_SCRIPT)
    removed = editor_step(implementor, "/9", [ROW_REMOVED, prompt(ORDERED_SCRIPT)])
    inserted = editor_step(implementor, "/11", [ROW_INSERTED_BEFORE, prompt(ORDERED_SCRIPT)])
    listing = editor_step(implementor, "/50", [WHOLE_LIST_FOOTER])
    close_script(implementor)

    assert ROW_REMOVED in lines_of(removed), removed
    assert ROW_INSERTED_BEFORE in lines_of(inserted), inserted
    assert listed_rows(listing) == [
        f"[1] {NO_COMMAND_TEXT}",
        on_enter_row(2, "second"),
        on_enter_row(3, "third"),
    ], listing


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR)
def test_swapped_rows_are_renumbered_and_saved_in_their_new_order(server) -> None:
    """Pins #368: /13 swaps the current row with the next and numbers the rows again from 1, and
    /save writes the rows in their new order with their new numbers.

    /save first copies the old file into world/scr/oldscrs, which the test world provides.
    """
    implementor = server.login(IMPLEMENTOR.name)

    open_script(implementor, ORDERED_SCRIPT)
    switched = editor_step(implementor, "/13", [ROWS_SWITCHED, prompt(ORDERED_SCRIPT)])
    listing = editor_step(implementor, "/50", [WHOLE_LIST_FOOTER])
    editor_step(implementor, "/save", ["shapeless indefinity"])
    # The game runs one line at a time, so once /51 answers, /save has finished writing.
    editor_step(implementor, "/51", [f"Script #{ORDERED_SCRIPT}:"])
    close_script(implementor)

    assert ROWS_SWITCHED in lines_of(switched), switched
    assert listed_rows(listing) == [
        on_enter_row(1, "second"),
        on_enter_row(2, "first"),
        on_enter_row(3, "third"),
    ], listing
    saved_text = (server.lib_dir / SCRIPT_FILE_RELATIVE).read_bytes().decode("latin-1")
    script_start = saved_text.find(f"#{ORDERED_SCRIPT} ")
    script_end = saved_text.find(f"#{OUT_OF_ORDER_SCRIPT} ")
    assert 0 <= script_start < script_end, saved_text
    saved_script = saved_text[script_start:script_end]
    assert [line for line in saved_script.splitlines() if line.startswith("11 ")] == [
        "11 1 0 0 0 0 0 0",
        "11 2 0 0 0 0 0 0",
        "11 3 0 0 0 0 0 0",
    ], saved_script
    assert [line.strip() for line in saved_script.splitlines() if line.endswith("~") and line.startswith(" ")] == [
        "second~",
        "first~",
        "third~",
    ], saved_script


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR)
def test_a_section_row_set_with_3_shows_as_a_titled_banner(server) -> None:
    """Pins #368: /3 SECTION asks for a title and a comment instead of parameters, and /50 shows
    the row as its title centred in a row of '=', its comment underneath, and a closing row of
    '=' (shape_section_show, shapemob.cpp)."""
    implementor = server.login(IMPLEMENTOR.name)

    open_script(implementor, ORDERED_SCRIPT)
    editor_step(implementor, "/3", ["Enter COMMAND TYPE"])
    title_prompt = editor_step(implementor, SECTION_TYPE, [SECTION_TITLE_PROMPT, prompt(ORDERED_SCRIPT)])
    comment_prompt = editor_step(implementor, SECTION_TITLE, [SECTION_COMMENT_PROMPT, prompt(ORDERED_SCRIPT)])
    editor_step(implementor, SECTION_COMMENT, [prompt(ORDERED_SCRIPT)])
    listing = editor_step(implementor, "/50", [WHOLE_LIST_FOOTER])
    close_script(implementor)

    assert SECTION_TITLE_PROMPT in title_prompt, title_prompt
    assert SECTION_COMMENT_PROMPT in comment_prompt, comment_prompt
    label = "[1] "
    banner_middle = f" {SECTION_TITLE} "
    banner_width = LIST_WIDTH - len(label)
    fill = banner_width - len(banner_middle)
    banner = label + "=" * (fill // 2) + banner_middle + "=" * (fill - fill // 2)
    listing_lines = lines_of(listing)
    assert banner in listing_lines, listing
    first = listing_lines.index(banner)
    assert listing_lines[first + 1].strip() == SECTION_COMMENT, listing
    assert listing_lines[first + 2] == " " * len(label) + "=" * banner_width, listing


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR)
def test_a_list_range_finds_a_row_numbered_out_of_order(server) -> None:
    """Pins #320: /52 3 3 on a script whose rows are numbered 1, 2, 4, 3 still lists row 3, which
    comes after row 4: the list checks every row rather than stopping past the end."""
    implementor = server.login(IMPLEMENTOR.name)

    open_script(implementor, OUT_OF_ORDER_SCRIPT)
    editor_step(implementor, "/52", ["Enter list range:"])
    range_set = editor_step(implementor, "3 3", ["List range set:", "Range not changed.", prompt(OUT_OF_ORDER_SCRIPT)])
    listing = editor_step(implementor, "/50", ["List range: 3-3"])
    close_script(implementor)

    assert "List range set: 3-3." in lines_of(range_set), range_set
    assert listed_rows(listing) == [on_enter_row(3, "three")], listing
