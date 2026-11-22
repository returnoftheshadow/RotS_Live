"""The script editor: removing and inserting rows, swapping them, sections and list ranges.

The tests cover removing the first row then inserting before the new first, swapping rows and
saving their new numbers, a section row, and a list range over rows numbered out of order.
"""

from __future__ import annotations

import pytest

from rots_harness.characters import IMPLEMENTOR
from rots_harness.replies import reply_to
from rots_harness.session import GameSession
from rots_harness.shaping import (
    COULD_NOT_OPEN_FILE_REPLY,
    EDITOR_SETTLE_SECONDS,
    ORDERED_SCRIPT,
    SCRIPT_FILE_RELATIVE,
    SCRIPT_RELEASED_REPLY,
    SECTION_COMMENT_PROMPT_START,
    SECTION_TITLE_PROMPT_START,
    release_editor,
)
from rots_harness.text_lines import raw_lines

pytestmark = [pytest.mark.scenario, pytest.mark.shaping, pytest.mark.shaping_script_editor]

# The test world's script #1181, whose rows are numbered 1, 2, 4 and 3 with the texts one, two,
# four and three (tests/integration/world/scr/11.scr); ORDERED_SCRIPT is #1180.
OUT_OF_ORDER_SCRIPT = 1181
# What /9 prints (shape_center_script).
ROW_REMOVED_REPLY = "Command removed."
# What /11 prints (shape_center_script).
ROW_INSERTED_BEFORE_REPLY = "New command added before current, and selected."
# The start of what /13 prints (shape_center_script).
ROWS_SWITCHED_START = "Switched the current and the next commands,"
# How /50 lists a row with no command yet (format_command, shapescript.cpp).
NO_COMMAND_LISTED = "*** No command: script will terminate here ***"
# The footer /50 ends with while no range is set (shape_range_footer, shapemob.cpp).
WHOLE_LIST_FOOTER = "List range: all (/52 to change)."
# The title of the section the scenario adds.
SECTION_TITLE = "Greeting"
# The comment of the section the scenario adds.
SECTION_COMMENT = "what a visitor hears"
# The width of an editor list line (SHAPE_LIST_WIDTH, protos.h).
LIST_WIDTH = 78
# The type name /3 takes for a section.
SECTION_TYPE = "SECTION"


def on_enter_row(number: int, text: str) -> str:
    """Return how /50 lists an ON_ENTER row (format_command, shapescript.cpp)."""
    return f"[{number}] TRIG ON_ENTER        ({text})"


def prompt(script_number: int) -> str:
    """Return the script editor's prompt while no field is answered (prompt_text, consts.cpp)."""
    return f"Script: {script_number}>"


def open_script(implementor: GameSession, script_number: int) -> None:
    """Start shaping a script, with its first row selected."""
    outcomes = [prompt(script_number), COULD_NOT_OPEN_FILE_REPLY]
    opened = reply_to(implementor, f"shape script {script_number}", outcomes, settle=EDITOR_SETTLE_SECONDS)
    assert prompt(script_number) in opened, opened


def listed_rows(listing: str) -> list[str]:
    """Return the row lines of a /50 listing, in order."""
    listing_lines = raw_lines(listing)
    return [line for line in listing_lines if line.startswith("[")]


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR)
def test_a_row_inserted_before_the_first_after_removing_the_first_heads_the_script(server) -> None:
    """Pins /9 on the first row, then /11: the inserted row heads the script.

    /9 makes the next row first, with no row before it, so /11 inserts a new row ahead of it.
    """
    implementor = server.login(IMPLEMENTOR.name)

    open_script(implementor, ORDERED_SCRIPT)
    ordered_prompt = prompt(ORDERED_SCRIPT)
    removed = reply_to(implementor, "/9", [ROW_REMOVED_REPLY, ordered_prompt], settle=EDITOR_SETTLE_SECONDS)
    inserted = reply_to(implementor, "/11", [ROW_INSERTED_BEFORE_REPLY, ordered_prompt], settle=EDITOR_SETTLE_SECONDS)
    listing = reply_to(implementor, "/50", [WHOLE_LIST_FOOTER], settle=EDITOR_SETTLE_SECONDS)
    release_editor(implementor, SCRIPT_RELEASED_REPLY, settle=EDITOR_SETTLE_SECONDS)

    assert ROW_REMOVED_REPLY in raw_lines(removed), removed
    assert ROW_INSERTED_BEFORE_REPLY in raw_lines(inserted), inserted
    assert listed_rows(listing) == [
        f"[1] {NO_COMMAND_LISTED}",
        on_enter_row(2, "second"),
        on_enter_row(3, "third"),
    ], listing


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR)
def test_swapped_rows_are_renumbered_and_saved_in_their_new_order(server) -> None:
    """Pins /13 and /save: swapped rows are renumbered from 1 and saved in their new order.

    /13 swaps the current row with the next; /save writes the rows with their new numbers. /save
    first copies the old file into world/scr/oldscrs, which the test world provides.
    """
    implementor = server.login(IMPLEMENTOR.name)

    open_script(implementor, ORDERED_SCRIPT)
    switch_outcomes = [ROWS_SWITCHED_START, prompt(ORDERED_SCRIPT)]
    switched = reply_to(implementor, "/13", switch_outcomes, settle=EDITOR_SETTLE_SECONDS)
    listing = reply_to(implementor, "/50", [WHOLE_LIST_FOOTER], settle=EDITOR_SETTLE_SECONDS)
    reply_to(implementor, "/save", ["shapeless indefinity"], settle=EDITOR_SETTLE_SECONDS)
    # The game runs one line at a time, so once /51 answers, /save has finished writing.
    reply_to(implementor, "/51", [f"Script #{ORDERED_SCRIPT}:"], settle=EDITOR_SETTLE_SECONDS)
    release_editor(implementor, SCRIPT_RELEASED_REPLY, settle=EDITOR_SETTLE_SECONDS)

    assert ROWS_SWITCHED_START in raw_lines(switched), switched
    assert listed_rows(listing) == [
        on_enter_row(1, "second"),
        on_enter_row(2, "first"),
        on_enter_row(3, "third"),
    ], listing
    saved_bytes = (server.lib_dir / SCRIPT_FILE_RELATIVE).read_bytes()
    saved_text = saved_bytes.decode("latin-1")
    script_start = saved_text.find(f"#{ORDERED_SCRIPT} ")
    script_end = saved_text.find(f"#{OUT_OF_ORDER_SCRIPT} ")
    assert 0 <= script_start < script_end, saved_text
    saved_script = saved_text[script_start:script_end]
    saved_script_lines = saved_script.splitlines()
    assert [line for line in saved_script_lines if line.startswith("11 ")] == [
        "11 1 0 0 0 0 0 0",
        "11 2 0 0 0 0 0 0",
        "11 3 0 0 0 0 0 0",
    ], saved_script
    assert [line.strip() for line in saved_script_lines if line.endswith("~") and line.startswith(" ")] == [
        "second~",
        "first~",
        "third~",
    ], saved_script


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR)
def test_a_section_row_set_with_3_shows_as_a_titled_banner(server) -> None:
    """Pins /3 SECTION: it asks for a title and a comment, and /50 shows the row as a banner.

    The banner is the title centred in a row of '=', the comment underneath, and a closing row of
    '=' (shape_section_show, shapemob.cpp).
    """
    implementor = server.login(IMPLEMENTOR.name)

    open_script(implementor, ORDERED_SCRIPT)
    reply_to(implementor, "/3", ["Enter COMMAND TYPE"], settle=EDITOR_SETTLE_SECONDS)
    ordered_prompt = prompt(ORDERED_SCRIPT)
    title_outcomes = [SECTION_TITLE_PROMPT_START, ordered_prompt]
    title_prompt = reply_to(implementor, SECTION_TYPE, title_outcomes, settle=EDITOR_SETTLE_SECONDS)
    comment_outcomes = [SECTION_COMMENT_PROMPT_START, ordered_prompt]
    comment_prompt = reply_to(implementor, SECTION_TITLE, comment_outcomes, settle=EDITOR_SETTLE_SECONDS)
    reply_to(implementor, SECTION_COMMENT, [prompt(ORDERED_SCRIPT)], settle=EDITOR_SETTLE_SECONDS)
    listing = reply_to(implementor, "/50", [WHOLE_LIST_FOOTER], settle=EDITOR_SETTLE_SECONDS)
    release_editor(implementor, SCRIPT_RELEASED_REPLY, settle=EDITOR_SETTLE_SECONDS)

    assert SECTION_TITLE_PROMPT_START in title_prompt, title_prompt
    assert SECTION_COMMENT_PROMPT_START in comment_prompt, comment_prompt
    label = "[1] "
    banner_middle = f" {SECTION_TITLE} "
    banner_width = LIST_WIDTH - len(label)
    fill = banner_width - len(banner_middle)
    banner = label + "=" * (fill // 2) + banner_middle + "=" * (fill - fill // 2)
    listing_lines = raw_lines(listing)
    assert banner in listing_lines, listing
    first = listing_lines.index(banner)
    assert listing_lines[first + 1].strip() == SECTION_COMMENT, listing
    assert listing_lines[first + 2] == " " * len(label) + "=" * banner_width, listing


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR)
def test_a_list_range_finds_a_row_numbered_out_of_order(server) -> None:
    """Pins /52 3 3 on rows numbered 1, 2, 4, 3: it still lists row 3, after row 4.

    The list checks every row rather than stopping past the end.
    """
    implementor = server.login(IMPLEMENTOR.name)

    open_script(implementor, OUT_OF_ORDER_SCRIPT)
    reply_to(implementor, "/52", ["Enter list range:"], settle=EDITOR_SETTLE_SECONDS)
    range_outcomes = ["List range set:", "Range not changed.", prompt(OUT_OF_ORDER_SCRIPT)]
    range_set = reply_to(implementor, "3 3", range_outcomes, settle=EDITOR_SETTLE_SECONDS)
    listing = reply_to(implementor, "/50", ["List range: 3-3"], settle=EDITOR_SETTLE_SECONDS)
    release_editor(implementor, SCRIPT_RELEASED_REPLY, settle=EDITOR_SETTLE_SECONDS)

    assert "List range set: 3-3." in raw_lines(range_set), range_set
    assert listed_rows(listing) == [on_enter_row(3, "three")], listing
