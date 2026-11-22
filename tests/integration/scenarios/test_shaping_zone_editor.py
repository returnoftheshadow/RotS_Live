"""The zone editor: /implement's reports, a reset's run-time report, and the editor's commands.

/implement reports the rows that name nothing and keeps the zone's living mobs counted, a reset
reports a row that fails at run time, and the other pins cover the editor's prompts, list range,
section rows and save.
"""

from __future__ import annotations

from pathlib import Path

import pytest

from rots_harness import world
from rots_harness.characters import IMPLEMENTOR
from rots_harness.immortal_commands import WHERE_ROW_PATTERN, goto, set_syslog
from rots_harness.replies import reply_to
from rots_harness.session import GameSession
from rots_harness.shaping import (
    COULD_NOT_OPEN_FILE_REPLY,
    EDITOR_SETTLE_SECONDS,
    SECTION_COMMENT_PROMPT_START,
    SECTION_TITLE_PROMPT_START,
    ZONE_FILE_RELATIVE,
    ZONE_IMPLEMENTED_REPLY,
    ZONE_NUMBER,
    ZONE_PROMPT,
    ZONE_RELEASED_REPLY,
    release_editor,
)
from rots_harness.text_lines import raw_lines

pytestmark = [pytest.mark.scenario, pytest.mark.shaping, pytest.mark.shaping_zone_editor]

# A vnum no mob or object of the test world has.
MISSING_VNUM = 99999
# Where /save copies the zone file (ZONE_FILE_RELATIVE, SHAPE_ZON_DIR in protos.h) before it
# rewrites it (SHAPE_ZON_BACKDIR).
ZONE_BACKUP_RELATIVE = Path("world") / "zon" / "oldzons" / f"{ZONE_NUMBER}.zon"
# The test world's copy of that file, which every run's lib starts from.
WORLD_ZONE_PATH = Path(__file__).resolve().parents[1] / ZONE_FILE_RELATIVE
# WORLD_ZONE_PATH's text.
WORLD_ZONE_TEXT = WORLD_ZONE_PATH.read_text(encoding="latin-1")
# The title of the section row the section scenarios add.
SECTION_TITLE = "Harness guards"
# The comment of that section row.
SECTION_COMMENT = "who waits where"
# The width of an editor list line (SHAPE_LIST_WIDTH, protos.h).
LIST_WIDTH = 78
# How much further in than its banner a section's comment starts in the zone list (show_command,
# shapezon.cpp).
SECTION_COMMENT_INDENT = 2

# What load_zone prints when the zone file holds no such zone; a missing file is
# COULD_NOT_OPEN_FILE_REPLY.
ZONE_MISSING_REPLY = "no zone here."
# What /6 prints when it moves to the next row.
NEXT_ROW_REPLY = "Next command chosen:"
# What /10 prints when it inserts a row.
ROW_INSERTED_REPLY = "New command added next to current, and selected."
# The /3 prompt's last line (shape_center_zone, zone_param_prompt).
COMMAND_TYPE_PROMPT = "Enter command type:"
# The /4 prompt's last line for a new row's eight numbers.
EIGHT_NUMBERS_PROMPT = "Enter 8 numbers:"
# The comment prompt's first words.
COMMENT_PROMPT = "Enter line COMMENT"
# What /3 prints for a letter it does not know.
UNKNOWN_COMMAND_TYPE_REPLY = "Unrecognized command."
# The start of the summary /implement ends with, with or without disabled rows (implement_zone).
IMPLEMENTED_START = "The zone was implemented"
# The summary after one row was disabled.
IMPLEMENTED_WITH_ONE_DISABLED = (
    "The zone was implemented, but 1 command(s) were DISABLED because of the error(s) above."
    "  Fix the vnum(s) and implement again."
)
# What /implement prints instead when the builder may not implement the zone.
NOT_ALLOWED_TO_IMPLEMENT_REPLY = "You are not allowed to implement this zone."
# What /implement prints instead when the zone is not in the world.
NO_SUCH_ZONE_REPLY = "It seems there is no such zone in the world"
# A K row that kits the last mob with only the missing object; -1 marks the unused slots.
KIT_ROW_NUMBERS = f"0 {MISSING_VNUM} -1 -1 -1 -1 -1 -1"
# An M row loading the missing mob into the arena centre at full chance and normal xp.
MOB_ROW_NUMBERS = f"0 {MISSING_VNUM} {world.ROOM_ARENA_CENTRE} 0 100 100 0 0"
# What zreset's reply ends with for zone 11 (do_zreset, act_wiz.cpp).
ZONE_RESET_END = ": Harness arena."
# What /52 is read up to: its prompt, or the editor prompt.
RANGE_PROMPT_OUTCOMES = ["Enter list range:", ZONE_PROMPT]
# The /52 prompt's line while no range is set (shape_range_set, shapemob.cpp).
RANGE_PROMPT_LINE = "Enter list range: start [end] (0 = all, blank keeps) [all]:"
# What an answer to /52 that sets or keeps the range is read up to.
RANGE_SET_OUTCOMES = ["List range set:", "Range not changed.", ZONE_PROMPT]
# The keyword of the target orc, one of the two mobs zone 11 loads.
ORC_KEYWORD = "target"
# The keyword of the snake, the other one.
SNAKE_KEYWORD = "snake"
# Room 1134, Corridor One in zone 11, where zone 11 loads the snake.
ROOM_CORRIDOR_ONE = 1134


def zone_reset_rows(zone_text: str) -> list[str]:
    """Return the zone's reset rows, up to the S that ends them (load_zone, shapezon.cpp).

    They are the lines after its name, description and map (each ending in '~'), its owner list,
    its map symbol line, its top room, lifespan and reset mode.
    """
    lines = zone_text.splitlines()
    last_header_text = [index for index, line in enumerate(lines) if line.endswith("~")][2]
    return lines[last_header_text + 6:lines.index("S")]


def zone_record_tokens(zone_text: str) -> list[str]:
    """Return the zone record's words, from its number to the S that ends its rows.

    Words rather than lines, since /save rewrites the spacing of the header lines (write_zone).
    """
    tokens = zone_text.split()
    return tokens[:tokens.index("S") + 1]


# The zone's rows as the test world has them.
ZONE_ROWS = zone_reset_rows(WORLD_ZONE_TEXT)
# The number of a new row, which lands after the last.
NEW_ROW_NUMBER = len(ZONE_ROWS) + 1
# What the builder reads once shape zone has read the zone's rows (load_zone, shapezon.cpp).
ZONE_LOADED_REPLY = f"Zone loaded, {len(ZONE_ROWS)} commands found."
# What a reset logs when a new K row runs without its object (report_zone_cmd_failure,
# zone.cpp), in mudlog's brackets.
RUNTIME_KIT_ERROR = f"[ ZONE ERROR: zone #{ZONE_NUMBER}, command {NEW_ROW_NUMBER} (K): object not loaded ]"


def zone_error(command_letter: str, kind: str, *, disabled: bool) -> str:
    """Return the ZONE ERROR line renum_zone_one sends for the new row (zone_renum_report)."""
    suffix = " - command disabled" if disabled else ""
    return (
        f"ZONE ERROR: zone #{ZONE_NUMBER}, command {NEW_ROW_NUMBER} ({command_letter}): "
        f"{kind} vnum {MISSING_VNUM} not found{suffix}"
    )


def open_zone_editor(implementor: GameSession) -> None:
    """Start shaping zone 11, with row 1 selected."""
    loaded = reply_to(
        implementor,
        f"shape zone {ZONE_NUMBER}",
        [ZONE_LOADED_REPLY, COULD_NOT_OPEN_FILE_REPLY, ZONE_MISSING_REPLY],
        settle=EDITOR_SETTLE_SECONDS,
    )
    assert ZONE_LOADED_REPLY in loaded, loaded


def insert_row_after_the_last(implementor: GameSession, command_letter: str) -> str:
    """Insert a row after the zone's last row, give it command_letter, and return the reply.

    The reply ends at the row's next prompt: its numbers, or a section's title.
    """
    for _row in ZONE_ROWS[1:]:
        moved = reply_to(implementor, "/6", [NEXT_ROW_REPLY, "No next command."], settle=EDITOR_SETTLE_SECONDS)
        assert NEXT_ROW_REPLY in moved, moved

    inserted = reply_to(implementor, "/10", [ROW_INSERTED_REPLY], settle=EDITOR_SETTLE_SECONDS)
    assert ROW_INSERTED_REPLY in inserted, inserted
    typed = reply_to(implementor, "/3", [COMMAND_TYPE_PROMPT], settle=EDITOR_SETTLE_SECONDS)
    assert COMMAND_TYPE_PROMPT in typed, typed
    return reply_to(
        implementor,
        command_letter,
        [EIGHT_NUMBERS_PROMPT, SECTION_TITLE_PROMPT_START, UNKNOWN_COMMAND_TYPE_REPLY],
        settle=EDITOR_SETTLE_SECONDS,
    )


def enter_row_numbers_and_comment(implementor: GameSession, numbers: str, comment: str) -> None:
    """Answer the new row's number and comment prompts, then check the row is selected."""
    numbered = reply_to(implementor, numbers, [COMMENT_PROMPT, ZONE_PROMPT], settle=EDITOR_SETTLE_SECONDS)
    assert COMMENT_PROMPT in numbered, numbered
    implementor.send_line(comment)
    shown = reply_to(implementor, "/1", ["Curr:", "No current command."], settle=EDITOR_SETTLE_SECONDS)
    assert f"Curr:   {NEW_ROW_NUMBER} " in shown, shown


def implement(implementor: GameSession) -> str:
    """Run /implement and return its report."""
    return reply_to(
        implementor,
        "/implement",
        [IMPLEMENTED_START, NOT_ALLOWED_TO_IMPLEMENT_REPLY, NO_SUCH_ZONE_REPLY],
        settle=EDITOR_SETTLE_SECONDS,
    )


def add_section_row(implementor: GameSession) -> None:
    """Insert a row after the last as a section with SECTION_TITLE and SECTION_COMMENT."""
    title_prompt = insert_row_after_the_last(implementor, "=")
    assert SECTION_TITLE_PROMPT_START in title_prompt, title_prompt
    comment_prompt = reply_to(
        implementor, SECTION_TITLE, [SECTION_COMMENT_PROMPT_START, ZONE_PROMPT], settle=EDITOR_SETTLE_SECONDS
    )
    assert SECTION_COMMENT_PROMPT_START in comment_prompt, comment_prompt
    implementor.send_line(SECTION_COMMENT)
    shown = reply_to(implementor, "/1", ["Curr:", "No current command."], settle=EDITOR_SETTLE_SECONDS)
    assert f"Curr:   {NEW_ROW_NUMBER} " in shown and SECTION_TITLE in shown, shown


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR)
def test_implementing_a_kit_row_naming_a_missing_object_tells_the_builder(server) -> None:
    """Pins that /implement tells the builder which row names an object that does not exist.

    A K row is never disabled for a missing object, so the summary stays the clean one. The
    builder's syslog is off (rotstool's default), so the line comes straight to the builder
    rather than through the log (zone_renum_send, zone.cpp).
    """
    implementor = server.login(IMPLEMENTOR.name)
    syslog = implementor.command("syslog")
    assert syslog.contains("Your syslog is currently off."), syslog.text

    open_zone_editor(implementor)
    numbers_prompt = insert_row_after_the_last(implementor, "K")
    assert EIGHT_NUMBERS_PROMPT in numbers_prompt, numbers_prompt
    enter_row_numbers_and_comment(implementor, KIT_ROW_NUMBERS, "kit with a missing object")
    report = implement(implementor)
    release_editor(implementor, ZONE_RELEASED_REPLY, settle=EDITOR_SETTLE_SECONDS)

    report_lines = raw_lines(report)
    assert zone_error("K", "object", disabled=False) in report_lines, report
    assert ZONE_IMPLEMENTED_REPLY in report_lines, report


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR)
def test_implementing_a_mob_row_naming_a_missing_mob_disables_it_and_says_so(server) -> None:
    """Pins that an M row naming a missing mob is disabled, says so, and the summary counts it."""
    implementor = server.login(IMPLEMENTOR.name)

    open_zone_editor(implementor)
    numbers_prompt = insert_row_after_the_last(implementor, "M")
    assert EIGHT_NUMBERS_PROMPT in numbers_prompt, numbers_prompt
    enter_row_numbers_and_comment(implementor, MOB_ROW_NUMBERS, "a mob that does not exist")
    report = implement(implementor)
    release_editor(implementor, ZONE_RELEASED_REPLY, settle=EDITOR_SETTLE_SECONDS)

    report_lines = raw_lines(report)
    assert zone_error("M", "mobile", disabled=True) in report_lines, report
    assert IMPLEMENTED_WITH_ONE_DISABLED in report_lines, report


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR)
def test_a_builder_whose_syslog_shows_zone_errors_reads_each_one_once(server) -> None:
    """Pins that with syslog at normal the ZONE ERROR line reaches the builder once, by the log.

    It comes in mudlog's brackets and is not sent a second time (mudlog_reaches, utility.cpp).
    """
    implementor = server.login(IMPLEMENTOR.name)
    set_syslog(implementor, "normal")

    open_zone_editor(implementor)
    numbers_prompt = insert_row_after_the_last(implementor, "K")
    assert EIGHT_NUMBERS_PROMPT in numbers_prompt, numbers_prompt
    enter_row_numbers_and_comment(implementor, KIT_ROW_NUMBERS, "kit with a missing object")
    report = implement(implementor)
    release_editor(implementor, ZONE_RELEASED_REPLY, settle=EDITOR_SETTLE_SECONDS)

    error_line = zone_error("K", "object", disabled=False)
    assert report.count(error_line) == 1, report
    assert f"[ {error_line} ]" in raw_lines(report), report


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR)
def test_an_m_rows_number_prompt_says_an_xp_percent_of_100_is_normal(server) -> None:
    """Pins that the /4 prompt of an M row explains its xp% field, and shows the row's values."""
    implementor = server.login(IMPLEMENTOR.name)

    open_zone_editor(implementor)
    prompt = reply_to(implementor, "/4", ["Enter 8 numbers [", ZONE_PROMPT], settle=EDITOR_SETTLE_SECONDS)
    kept = reply_to(implementor, "", ["Values kept.", ZONE_PROMPT], settle=EDITOR_SETTLE_SECONDS)
    assert "Values kept." in kept, kept
    implementor.send_line("")
    release_editor(implementor, ZONE_RELEASED_REPLY, settle=EDITOR_SETTLE_SECONDS)

    prompt_lines = raw_lines(prompt)
    xp_help = "  max_world, max_alive_line: 0 = no limit.  xp%: 100 = normal (0 also normal)"
    assert xp_help in prompt_lines, prompt
    row_fields = ZONE_ROWS[0].split()
    first_row_numbers = " ".join(row_fields[1:9])
    assert f"Enter 8 numbers [{first_row_numbers}]:" in prompt_lines, prompt


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR)
def test_a_list_range_limits_the_zone_list_and_its_footer_says_so(server) -> None:
    """Pins that /52 sets where the /50 list starts, and the list's last line names the range."""
    implementor = server.login(IMPLEMENTOR.name)

    open_zone_editor(implementor)
    range_prompt = reply_to(implementor, "/52", RANGE_PROMPT_OUTCOMES, settle=EDITOR_SETTLE_SECONDS)
    assert RANGE_PROMPT_LINE in raw_lines(range_prompt), range_prompt
    range_set = reply_to(implementor, "2", RANGE_SET_OUTCOMES, settle=EDITOR_SETTLE_SECONDS)
    assert "List range set: from 2." in raw_lines(range_set), range_set
    listing = reply_to(implementor, "/50", [ZONE_PROMPT], settle=EDITOR_SETTLE_SECONDS)
    release_editor(implementor, ZONE_RELEASED_REPLY, settle=EDITOR_SETTLE_SECONDS)

    listing_lines = raw_lines(listing)
    assert "List range: from 2 (/52 to change)." in listing_lines, listing
    assert not any(line.startswith("  1 M::") for line in listing_lines), listing
    assert any(line.startswith("  2 M:: Ld_flg(0) Mob:1131 toRom:1134") for line in listing_lines), listing


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR)
def test_a_list_range_with_an_end_stops_the_zone_list_there(server) -> None:
    """Pins that /52 with a start and an end lists only the rows between them, the footer both."""
    implementor = server.login(IMPLEMENTOR.name)

    open_zone_editor(implementor)
    reply_to(implementor, "/52", RANGE_PROMPT_OUTCOMES, settle=EDITOR_SETTLE_SECONDS)
    range_set = reply_to(implementor, "1 1", RANGE_SET_OUTCOMES, settle=EDITOR_SETTLE_SECONDS)
    listing = reply_to(implementor, "/50", [ZONE_PROMPT], settle=EDITOR_SETTLE_SECONDS)
    release_editor(implementor, ZONE_RELEASED_REPLY, settle=EDITOR_SETTLE_SECONDS)

    assert "List range set: 1-1." in raw_lines(range_set), range_set
    listing_lines = raw_lines(listing)
    assert "List range: 1-1 (/52 to change)." in listing_lines, listing
    assert any(line.startswith("  1 M::") for line in listing_lines), listing
    assert not any(line.startswith("  2 M::") for line in listing_lines), listing


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR)
def test_a_list_range_of_0_clears_it(server) -> None:
    """Pins that 0 alone at the /52 prompt clears the range, so /50 lists every row again."""
    implementor = server.login(IMPLEMENTOR.name)

    open_zone_editor(implementor)
    reply_to(implementor, "/52", RANGE_PROMPT_OUTCOMES, settle=EDITOR_SETTLE_SECONDS)
    reply_to(implementor, "2", RANGE_SET_OUTCOMES, settle=EDITOR_SETTLE_SECONDS)
    reply_to(implementor, "/52", RANGE_PROMPT_OUTCOMES, settle=EDITOR_SETTLE_SECONDS)
    cleared = reply_to(
        implementor, "0", ["List range cleared.", "Range not changed.", ZONE_PROMPT], settle=EDITOR_SETTLE_SECONDS
    )
    listing = reply_to(implementor, "/50", [ZONE_PROMPT], settle=EDITOR_SETTLE_SECONDS)
    release_editor(implementor, ZONE_RELEASED_REPLY, settle=EDITOR_SETTLE_SECONDS)

    assert "List range cleared." in raw_lines(cleared), cleared
    listing_lines = raw_lines(listing)
    assert "List range: all (/52 to change)." in listing_lines, listing
    assert any(line.startswith("  1 M::") for line in listing_lines), listing


# Each answer /52 refuses, and what it says (shape_range_set, shapemob.cpp).
RANGE_REFUSALS = [
    ("1 2 3", "Enter at most two numbers. Range not changed."),
    ("2x", "Numbers only. Range not changed."),
    ("0 3", "Use numbers 1 or more, or 0 alone to clear. Range not changed."),
    ("3 2", "End must not be below start. Range not changed."),
]


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR)
def test_a_list_range_answer_it_cannot_use_is_refused_and_changes_nothing(server) -> None:
    """Pins that /52 refuses an answer it cannot use, saying why, and changes nothing.

    It refuses more than two numbers, a word, a number below 1 beside another, and an end below
    the start; the next /52 prompt still shows the whole list as the range.
    """
    implementor = server.login(IMPLEMENTOR.name)

    open_zone_editor(implementor)
    replies = []
    for answer, refusal in RANGE_REFUSALS:
        prompt = reply_to(implementor, "/52", RANGE_PROMPT_OUTCOMES, settle=EDITOR_SETTLE_SECONDS)
        assert RANGE_PROMPT_LINE in raw_lines(prompt), prompt
        refusal_reply = reply_to(
            implementor, answer, ["Range not changed.", "List range", ZONE_PROMPT], settle=EDITOR_SETTLE_SECONDS
        )
        replies.append((refusal, refusal_reply))

    last_prompt = reply_to(implementor, "/52", RANGE_PROMPT_OUTCOMES, settle=EDITOR_SETTLE_SECONDS)
    reply_to(implementor, "", [ZONE_PROMPT], settle=EDITOR_SETTLE_SECONDS)
    release_editor(implementor, ZONE_RELEASED_REPLY, settle=EDITOR_SETTLE_SECONDS)

    for refusal, reply in replies:
        assert refusal in raw_lines(reply), reply

    assert RANGE_PROMPT_LINE in raw_lines(last_prompt), last_prompt


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR)
def test_a_section_row_shows_as_a_titled_banner_in_the_zone_list(server) -> None:
    """Pins that the /50 list shows a section row as a titled banner, its comment and a closing row.

    The title is centred in a row of '=', the comment is underneath, and a closing row of '='
    follows (shape_section_show, shapemob.cpp). After the row's label, the banner and the closing
    row fill the rest of the list's width.
    """
    implementor = server.login(IMPLEMENTOR.name)

    open_zone_editor(implementor)
    add_section_row(implementor)
    listing = reply_to(implementor, "/50", [ZONE_PROMPT], settle=EDITOR_SETTLE_SECONDS)
    release_editor(implementor, ZONE_RELEASED_REPLY, settle=EDITOR_SETTLE_SECONDS)

    label = f"{NEW_ROW_NUMBER:3d} "
    banner_width = LIST_WIDTH - len(label)
    banner_middle = f" {SECTION_TITLE} "
    fill = banner_width - len(banner_middle)
    expected_rows = [
        label + "=" * (fill // 2) + banner_middle + "=" * (fill - fill // 2),
        " " * (len(label) + SECTION_COMMENT_INDENT) + SECTION_COMMENT,
        " " * len(label) + "=" * banner_width,
    ]
    listing_lines = raw_lines(listing)
    first = listing_lines.index(expected_rows[0]) if expected_rows[0] in listing_lines else -1
    assert first >= 0 and listing_lines[first:first + 3] == expected_rows, listing


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR)
def test_saving_a_zone_writes_its_section_row_to_the_zone_file(server) -> None:
    """Pins that /save writes a section row as "= <title> | <comment>" in its place among the rows.

    The rest of the zone is written as it was. /save first copies the old file, unchanged, into
    world/zon/oldzons, which the test world provides.
    """
    implementor = server.login(IMPLEMENTOR.name)
    zone_file = server.lib_dir / ZONE_FILE_RELATIVE
    pre_save_text = zone_file.read_text(encoding="latin-1")
    section_row = f"= {SECTION_TITLE} | {SECTION_COMMENT}"

    open_zone_editor(implementor)
    add_section_row(implementor)
    save_reply = reply_to(implementor, "/save", ["shapeless indefinity"], settle=EDITOR_SETTLE_SECONDS)
    # The game runs one line at a time, so once /1 answers, /save has finished writing.
    synced = reply_to(implementor, "/1", ["Curr:"], settle=EDITOR_SETTLE_SECONDS)
    release_editor(implementor, ZONE_RELEASED_REPLY, settle=EDITOR_SETTLE_SECONDS)

    assert "could not" not in save_reply + synced, save_reply + synced
    backup_file = server.lib_dir / ZONE_BACKUP_RELATIVE
    backup_text = backup_file.read_text(encoding="latin-1")
    assert backup_text == pre_save_text, backup_text
    saved_text = zone_file.read_text(encoding="latin-1")
    saved_lines = saved_text.splitlines()
    saved_rows = [line for line in saved_lines if line[:1] in ("M", "=")]
    assert saved_rows == ZONE_ROWS + [section_row], saved_text
    without_section = "\n".join(line for line in saved_lines if line != section_row)
    assert zone_record_tokens(without_section) == zone_record_tokens(pre_save_text), saved_text


def where_rows(implementor: GameSession, keyword: str) -> list[str]:
    """Return the numbered rows `where <keyword>` lists, one per mob or object answering to keyword.

    perform_immort_where (act_info.cpp) lists them.
    """
    reply = implementor.command(f"where {keyword}")
    reply_lines = raw_lines(reply.text)
    return [line for line in reply_lines if WHERE_ROW_PATTERN.match(line)]


def reset_zone(implementor: GameSession) -> str:
    """Run zreset on zone 11 and return everything up to its reply, which names the zone."""
    return reply_to(
        implementor, f"zreset {ZONE_NUMBER}", [ZONE_RESET_END, "Invalid zone number."], settle=EDITOR_SETTLE_SECONDS
    )


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR)
def test_implementing_a_zone_keeps_its_living_mobs_counted_against_their_rows(server) -> None:
    """Pins that /implement counts each living mob the zone loaded against its M row again.

    So the next reset does not load a second one past the row's limit of one. Each row loads at
    100%, so no roll decides a load. The control is the reset before /implement, which already finds
    each row at its limit.
    """
    implementor = server.login(IMPLEMENTOR.name)
    booted_orcs = where_rows(implementor, ORC_KEYWORD)
    booted_snakes = where_rows(implementor, SNAKE_KEYWORD)
    booted = (booted_orcs, booted_snakes)
    reset_zone(implementor)
    orc_rows = where_rows(implementor, ORC_KEYWORD)
    snake_rows = where_rows(implementor, SNAKE_KEYWORD)
    before_implement = (len(orc_rows), len(snake_rows))

    open_zone_editor(implementor)
    report = implement(implementor)
    release_editor(implementor, ZONE_RELEASED_REPLY, settle=EDITOR_SETTLE_SECONDS)
    reset_zone(implementor)
    orc_rows = where_rows(implementor, ORC_KEYWORD)
    snake_rows = where_rows(implementor, SNAKE_KEYWORD)
    after_implement = (len(orc_rows), len(snake_rows))

    assert [len(rows) for rows in booted] == [1, 1], booted
    assert before_implement == (1, 1), before_implement
    assert ZONE_IMPLEMENTED_REPLY in raw_lines(report), report
    assert after_implement == (1, 1), after_implement


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR)
def test_a_reset_reports_a_kit_row_whose_object_is_missing_each_time_it_runs(server) -> None:
    """Pins that each reset that runs an implemented K row naming a missing object reports it.

    Each reset that loads the row's mob says, through the log, that the row's object was not
    loaded. The K row follows the snake's row, so it runs only when that row loads a snake: with the
    snake alive (the control) the reset says nothing about it; once the snake is purged the
    reset loads one and reports the row. The row is implemented, never saved.
    """
    implementor = server.login(IMPLEMENTOR.name)
    set_syslog(implementor, "normal")

    open_zone_editor(implementor)
    numbers_prompt = insert_row_after_the_last(implementor, "K")
    assert EIGHT_NUMBERS_PROMPT in numbers_prompt, numbers_prompt
    enter_row_numbers_and_comment(implementor, KIT_ROW_NUMBERS, "kit the snake with a missing object")
    implement(implementor)
    release_editor(implementor, ZONE_RELEASED_REPLY, settle=EDITOR_SETTLE_SECONDS)
    with_snake_alive = reset_zone(implementor)
    goto(implementor, ROOM_CORRIDOR_ONE)
    purged = implementor.command(f"purge {SNAKE_KEYWORD}")
    after_purge = reset_zone(implementor)

    assert RUNTIME_KIT_ERROR not in with_snake_alive, with_snake_alive
    assert where_rows(implementor, SNAKE_KEYWORD), purged.text + after_purge
    assert RUNTIME_KIT_ERROR in raw_lines(after_purge), after_purge
