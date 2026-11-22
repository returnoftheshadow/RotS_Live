"""The text editor end to end: a description, a letter, and the editor's replace and limit.

A description typed with `set description` is stored as typed and read by whoever looks; a letter
written at a post office is paid for, kept and handed over; and the editor's replace and redisplay
and a letter's limit are pinned as players read them.
"""

from __future__ import annotations

import re
from collections.abc import Sequence

import pytest

from rots_harness.characters import IMPLEMENTOR, Character, StoredValues
from rots_harness.game_text import TEXT_EDITOR_CURSOR, TEXT_EDITOR_PROMPT, TEXT_SO_FAR
from rots_harness.replies import reply_to
from rots_harness.session import GameSession
from rots_harness.shaping import EDITOR_SETTLE_SECONDS
from rots_harness.stat_reading import read_stat
from rots_harness.text_lines import first_number, raw_lines, stripped_lines
from rots_harness.world_resets import MobReset

pytestmark = [pytest.mark.scenario, pytest.mark.text_editor]

# The post office (tests/integration/world/wld/24.wld), where every character here starts.
ROOM_POST_OFFICE = 2403
# The name look shows for ROOM_POST_OFFICE.
ROOM_POST_OFFICE_NAME = "Post Office"
# The postmaster (mob/24.mob): MOB_SPEC and program 29, the postmaster special (mail.cpp).
POSTMASTER_VNUM = 2401

# What a letter's stamp costs, in copper (STAMP_PRICE, mail.h).
STAMP_PRICE_COPPER = 200
# The writer's coins, in copper, before the stamp.
COINS_BEFORE_STAMP = 500
# The writer's coins, in copper, after the stamp.
COINS_AFTER_STAMP = COINS_BEFORE_STAMP - STAMP_PRICE_COPPER
# The coins line of the implementor's `stat player` (do_stat_character, act_wiz.cpp: "Coins:
# [%9d]").
COINS_PATTERN = re.compile(r"Coins: \[\s*(\d+)\]")

# A level-10 human warrior who writes: her own description, or a letter. Level 10 is the least
# that may send mail (MIN_MAIL_LEVEL, mail.h); her COINS_BEFORE_STAMP pay the stamp.
WRITER = Character(
    name="Testwriter",
    race="human",
    sex="female",
    level=10,
    load_room=ROOM_POST_OFFICE,
    class_name="warrior",
    stored=StoredValues(gold=COINS_BEFORE_STAMP),
)
# A level-10 human warrior who looks at the writer and receives her letters. Good-aligned, so a
# letter comes from the Postal Service of Gondor rather than the Subversive Messaging System
# (read_delete, mail.cpp).
READER = Character(
    name="Testreader",
    race="human",
    sex="male",
    level=10,
    load_room=ROOM_POST_OFFICE,
    class_name="warrior",
    stored=StoredValues(alignment=500),
)

# The first line of the editor's command list (string_add_commands, modify.cpp).
EDITOR_OPENED = "Edit the text now. Commands, each at the start of a line:"
# What %s prints first: the count, with "occurence" given an "s" for any count but 0.
REPLACED_FORMAT = "Replaced {} occurence{}."
# What %s prints before the text.
TEXT_IS = "Your text is:"
# What %s prints for a line with no '~'.
REPLACE_USAGE = "Use %s<old>~<new>."
# What %s prints for a line with nothing between %s and '~'.
NO_PATTERN = "No pattern to replace."
# What the room reads once a writer saves (string_add_finish).
FINISHED_WRITING = f"{WRITER.name} finished writing."

# The three lines of the description the writer types.
DESCRIPTION_LINES = [
    "A tall woman in a grey cloak.",
    "Her boots are caked in old mud.",
    "She leans on a walking staff.",
]
# The word the replace looks for in the description.
REPLACE_OLD = "grey"
# The word the replace puts in its place.
REPLACE_NEW = "green"

# The postmaster as act() starts its lines to the sender and the reader (postmaster_send_mail,
# postmaster_check_mail, postmaster_receive_mail, mail.cpp), each "<postmaster> tells you, '...'".
POSTMASTER = "A postmaster"
# What the postmaster tells a sender as it takes the stamp.
STAMP_TAKEN = f"{POSTMASTER} tells you, 'I'll take 2 silver coins for the stamp.'"
# What the postmaster tells a sender as the editor opens.
WRITE_NOW = f"{POSTMASTER} tells you, 'Write your message, use %e when done.'"
# What the postmaster tells a sender whose recipient no player has.
NOT_REGISTERED = f"{POSTMASTER} tells you, 'No one by that name is registered here!'"
# What the postmaster tells a reader who has mail.
MAIL_WAITING = f"{POSTMASTER} tells you, 'You have mail waiting.'"
# What the postmaster tells a reader who has none.
NO_MAIL_WAITING = f"{POSTMASTER} tells you, 'Sorry, you don't have any mail waiting.'"
# What a reader reads as it receives a letter.
LETTER_GIVEN = f"{POSTMASTER} gives you a letter."
# What the sender reads once the letter is saved (string_add, modify.cpp).
MESSAGE_SENT = "Message sent!"
# The letter's object (postmaster_receive_mail) as the inventory lists it.
LETTER_SHORT_DESCRIPTION = "a letter"
# What reading a note prints before its text (show_obj_to_char, act_info.cpp).
WRITTEN_UPON = "There is something written upon it:"
# The letter's header for a good-aligned reader (read_delete).
GONDOR_HEADER = "--- Postal Service of Gondor ---"
# The start of the header's date line, which follows the wall clock, so only its start is matched.
DATE_LINE_START = "Date: "
# The header's recipient line.
TO_LINE = f"To: {READER.name}"
# The header's sender line.
FROM_LINE = f"From: {WRITER.name}"
# The letter's two lines.
LETTER_LINES = ["The gate is closed tonight.", "Meet me at the inn instead."]
# A name the post office refuses because nobody has it.
UNKNOWN_NAME = "nobody"
# The reader's name with a digit, which parse_name (interpre.cpp) refuses.
NAME_WITH_DIGIT = f"{READER.name.lower()}1"

# A letter's limit (MAX_MAIL_SIZE, mail.h).
LETTER_LIMIT = 4000
# What the editor prints when a line would pass the limit (edit_fits_limit, modify.cpp).
LIMIT_REFUSED = f"That would take the text past its limit of {LETTER_LIMIT} characters, so it was not changed."
# The length of each long line: each takes 202 with its line end.
LONG_LINE_LENGTH = 200
# How many long lines fit: the first 19 make 3838 and the 20th would make 4040, past the limit.
LONG_LINES_THAT_FIT = 19

# Seconds to wait for the editor's and the postmaster's replies.
REPLY_TIMEOUT_SECONDS = 8.0


def type_lines(session: GameSession, lines: Sequence[str]) -> None:
    """Type each line into the open editor, waiting for its prompt."""
    for line in lines:
        reply_to(session, line, [TEXT_EDITOR_PROMPT], settle=EDITOR_SETTLE_SECONDS, timeout=REPLY_TIMEOUT_SECONDS)


def text_after(text: str, header: str) -> list[str]:
    """Return the lines after the header line, up to the cursor mark or the editor prompt."""
    all_lines = raw_lines(text)
    assert header in all_lines, text
    following = []
    header_index = all_lines.index(header)
    for line in all_lines[header_index + 1:]:
        if line == TEXT_EDITOR_CURSOR or line.startswith(TEXT_EDITOR_PROMPT.strip()):
            break
        following.append(line)

    return following


def coins_of(implementor: GameSession, name: str) -> int:
    """Return the coins, in copper, the implementor's `stat player` shows the character carrying."""
    reply = read_stat(implementor, f"player {name}", settle=EDITOR_SETTLE_SECONDS)
    return first_number(COINS_PATTERN, reply)


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, WRITER, READER)
def test_a_description_typed_in_the_editor_is_stored_and_read_as_typed(server) -> None:
    """Pins that a description typed in the editor is stored, and read exactly as typed.

    `set description` opens the editor; three lines typed, %r shows them with the cursor after
    the last, and %e saves them. The room reads that the writer finished, and whoever looks at her
    reads the three lines exactly as typed. Nothing draws.
    """
    writer = server.login(WRITER.name)
    reader = server.login(READER.name)

    opened = reply_to(
        writer, "set description", [EDITOR_OPENED], settle=EDITOR_SETTLE_SECONDS, timeout=REPLY_TIMEOUT_SECONDS
    )
    assert EDITOR_OPENED in raw_lines(opened), opened
    type_lines(writer, DESCRIPTION_LINES)
    shown = reply_to(writer, "%r", [TEXT_EDITOR_CURSOR], settle=EDITOR_SETTLE_SECONDS, timeout=REPLY_TIMEOUT_SECONDS)
    assert text_after(shown, TEXT_SO_FAR) == DESCRIPTION_LINES, shown
    reader.drain(EDITOR_SETTLE_SECONDS)
    writer.send_line("%e")
    finished = reader.expect([FINISHED_WRITING])
    assert FINISHED_WRITING in stripped_lines(finished), finished

    looked = reader.command(f"look {WRITER.name.lower()}").text
    looked_lines = stripped_lines(looked)
    first = looked_lines.index(DESCRIPTION_LINES[0]) if DESCRIPTION_LINES[0] in looked_lines else -1
    assert first >= 0 and looked_lines[first:first + len(DESCRIPTION_LINES)] == DESCRIPTION_LINES, looked


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(WRITER)
def test_replace_in_the_editor_counts_and_shows_the_text(server) -> None:
    """Pins that %s<old>~<new> replaces every match and prints the count and the replaced text.

    A pattern that matches nothing prints a count of 0 and the text unchanged. Only the reply is
    pinned here: the text the replace leaves is the fix pin below. Nothing draws.
    """
    writer = server.login(WRITER.name)

    reply_to(writer, "set description", [EDITOR_OPENED], settle=EDITOR_SETTLE_SECONDS, timeout=REPLY_TIMEOUT_SECONDS)
    type_lines(writer, DESCRIPTION_LINES)
    replace_outcomes = [TEXT_IS, REPLACE_USAGE, NO_PATTERN]
    replace_line = f"%s{REPLACE_OLD}~{REPLACE_NEW}"
    replaced = reply_to(
        writer, replace_line, replace_outcomes, settle=EDITOR_SETTLE_SECONDS, timeout=REPLY_TIMEOUT_SECONDS
    )
    unmatched = reply_to(
        writer, "%sviolet~purple", replace_outcomes, settle=EDITOR_SETTLE_SECONDS, timeout=REPLY_TIMEOUT_SECONDS
    )
    writer.send_line("%q")
    looked = reply_to(
        writer,
        "look",
        [ROOM_POST_OFFICE_NAME, TEXT_EDITOR_PROMPT],
        settle=EDITOR_SETTLE_SECONDS,
        timeout=REPLY_TIMEOUT_SECONDS,
    )

    expected_text = [line.replace(REPLACE_OLD, REPLACE_NEW) for line in DESCRIPTION_LINES]
    assert REPLACED_FORMAT.format(1, "s") in raw_lines(replaced), replaced
    assert text_after(replaced, TEXT_IS)[:len(expected_text)] == expected_text, replaced
    assert REPLACED_FORMAT.format(0, "") in raw_lines(unmatched), unmatched
    assert text_after(unmatched, TEXT_IS)[:len(expected_text)] == expected_text, unmatched
    assert ROOM_POST_OFFICE_NAME in looked, looked


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(WRITER)
def test_replace_adds_no_blank_line_to_the_text(server) -> None:
    """Fix pin (#537): after a replace, %r shows the replaced lines, then the cursor, no blank line.

    #537 made %s add no line; before it, every %s line also added an empty line at the cursor.
    Nothing draws.
    """
    writer = server.login(WRITER.name)

    reply_to(writer, "set description", [EDITOR_OPENED], settle=EDITOR_SETTLE_SECONDS, timeout=REPLY_TIMEOUT_SECONDS)
    type_lines(writer, DESCRIPTION_LINES)
    reply_to(
        writer,
        f"%s{REPLACE_OLD}~{REPLACE_NEW}",
        [TEXT_IS, REPLACE_USAGE, NO_PATTERN],
        settle=EDITOR_SETTLE_SECONDS,
        timeout=REPLY_TIMEOUT_SECONDS,
    )
    shown = reply_to(writer, "%r", [TEXT_EDITOR_CURSOR], settle=EDITOR_SETTLE_SECONDS, timeout=REPLY_TIMEOUT_SECONDS)
    writer.send_line("%q")

    expected_text = [line.replace(REPLACE_OLD, REPLACE_NEW) for line in DESCRIPTION_LINES]
    assert text_after(shown, TEXT_SO_FAR) == expected_text, shown


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(WRITER)
def test_replace_without_a_tilde_says_how_and_changes_nothing(server) -> None:
    """Fix pin (#537): %s with no '~' prints its usage and leaves the text as it was.

    #537 made such a line print the usage; before it, %s read the rest of the line as the pattern
    and an empty replacement, deleting every match. Nothing draws.
    """
    writer = server.login(WRITER.name)

    reply_to(writer, "set description", [EDITOR_OPENED], settle=EDITOR_SETTLE_SECONDS, timeout=REPLY_TIMEOUT_SECONDS)
    type_lines(writer, DESCRIPTION_LINES)
    refused = reply_to(
        writer,
        f"%s{REPLACE_OLD}",
        [REPLACE_USAGE, TEXT_IS, NO_PATTERN],
        settle=EDITOR_SETTLE_SECONDS,
        timeout=REPLY_TIMEOUT_SECONDS,
    )
    shown = reply_to(writer, "%r", [TEXT_EDITOR_CURSOR], settle=EDITOR_SETTLE_SECONDS, timeout=REPLY_TIMEOUT_SECONDS)
    writer.send_line("%q")

    assert REPLACE_USAGE in raw_lines(refused), refused
    assert text_after(shown, TEXT_SO_FAR) == DESCRIPTION_LINES, shown


@pytest.mark.schedule("affect-hours")
@pytest.mark.world_resets(MobReset(POSTMASTER_VNUM, ROOM_POST_OFFICE))
@pytest.mark.characters(IMPLEMENTOR, WRITER, READER)
def test_a_letter_is_paid_for_kept_and_handed_to_its_reader(server) -> None:
    """Pins that a letter written at a post office is paid for, kept and handed to its reader.

    At a post office, `mail <name>` takes the stamp and opens the editor; %e sends the letter.
    The reader then has mail waiting, receives a letter and reads it with the postal header and
    the two lines as written; afterwards no mail is waiting. Nothing draws.
    """
    implementor = server.login(IMPLEMENTOR.name)
    writer = server.login(WRITER.name)
    reader = server.login(READER.name)
    coins = coins_of(implementor, WRITER.name)
    assert coins == COINS_BEFORE_STAMP, coins

    opened = reply_to(
        writer,
        f"mail {READER.name.lower()}",
        [WRITE_NOW, NOT_REGISTERED],
        settle=EDITOR_SETTLE_SECONDS,
        timeout=REPLY_TIMEOUT_SECONDS,
    )
    assert STAMP_TAKEN in stripped_lines(opened), opened
    assert WRITE_NOW in stripped_lines(opened), opened
    type_lines(writer, LETTER_LINES)
    sent = reply_to(writer, "%e", [MESSAGE_SENT], settle=EDITOR_SETTLE_SECONDS, timeout=REPLY_TIMEOUT_SECONDS)
    assert MESSAGE_SENT in stripped_lines(sent), sent
    coins = coins_of(implementor, WRITER.name)
    assert coins == COINS_AFTER_STAMP, coins

    checked = reader.command("check").text
    assert MAIL_WAITING in stripped_lines(checked), checked
    received = reader.command("receive").text
    assert LETTER_GIVEN in stripped_lines(received), received
    carried = reader.command("inventory").text
    assert LETTER_SHORT_DESCRIPTION in stripped_lines(carried), carried
    letter = reply_to(reader, "read letter", [LETTER_LINES[-1]], settle=EDITOR_SETTLE_SECONDS)
    letter_lines = stripped_lines(letter)
    start = letter_lines.index(WRITTEN_UPON) if WRITTEN_UPON in letter_lines else -1
    assert start >= 0, letter
    body = letter_lines[start + 1:start + 6]
    assert body[0] == GONDOR_HEADER, letter
    assert body[1].startswith(DATE_LINE_START), letter
    assert body[2:] == [TO_LINE, FROM_LINE, LETTER_LINES[0]], letter
    assert letter_lines[start + 6] == LETTER_LINES[1], letter
    checked_again = reader.command("check").text
    assert NO_MAIL_WAITING in stripped_lines(checked_again), checked_again


@pytest.mark.parametrize("recipient", [UNKNOWN_NAME, NAME_WITH_DIGIT], ids=["unknown", "with-digit"])
@pytest.mark.schedule("affect-hours")
@pytest.mark.world_resets(MobReset(POSTMASTER_VNUM, ROOM_POST_OFFICE))
@pytest.mark.characters(IMPLEMENTOR, WRITER, READER)
def test_mail_to_a_name_nobody_has_is_refused_and_takes_no_stamp(server, recipient: str) -> None:
    """Pins that `mail` to a name nobody has is refused before the stamp is taken.

    A name no player has, or a player's name with a digit added, is refused with "No one by that
    name is registered here!", and no editor opens. parse_name (interpre.cpp) refuses the name
    with a digit. Nothing draws.
    """
    implementor = server.login(IMPLEMENTOR.name)
    writer = server.login(WRITER.name)
    server.login(READER.name)

    refused = reply_to(
        writer,
        f"mail {recipient}",
        [NOT_REGISTERED, WRITE_NOW],
        settle=EDITOR_SETTLE_SECONDS,
        timeout=REPLY_TIMEOUT_SECONDS,
    )
    looked = reply_to(
        writer,
        "look",
        [ROOM_POST_OFFICE_NAME, TEXT_EDITOR_PROMPT],
        settle=EDITOR_SETTLE_SECONDS,
        timeout=REPLY_TIMEOUT_SECONDS,
    )

    assert NOT_REGISTERED in stripped_lines(refused), refused
    assert WRITE_NOW not in refused, refused
    assert ROOM_POST_OFFICE_NAME in looked, looked
    coins = coins_of(implementor, WRITER.name)
    assert coins == COINS_BEFORE_STAMP, coins


@pytest.mark.schedule("affect-hours")
@pytest.mark.world_resets(MobReset(POSTMASTER_VNUM, ROOM_POST_OFFICE))
@pytest.mark.characters(IMPLEMENTOR, WRITER, READER)
def test_a_letter_line_that_would_pass_4000_characters_is_refused(server) -> None:
    """Fix pin (#537): a letter is held to 4000 characters.

    Nineteen lines of 200 characters fit; the twentieth is refused with the limit, and the text
    stays as it was. #537 made the editor keep the limit; before it, the limit was lost once the
    first line was typed, and every line was taken. Nothing draws.
    """
    writer = server.login(WRITER.name)
    server.login(READER.name)

    reply_to(
        writer,
        f"mail {READER.name.lower()}",
        [WRITE_NOW, NOT_REGISTERED],
        settle=EDITOR_SETTLE_SECONDS,
        timeout=REPLY_TIMEOUT_SECONDS,
    )
    long_lines = [f"{index:02d}" + "x" * (LONG_LINE_LENGTH - 2) for index in range(LONG_LINES_THAT_FIT + 1)]
    fitting_outcomes = [TEXT_EDITOR_PROMPT, LIMIT_REFUSED]
    fitting_replies = [
        reply_to(writer, line, fitting_outcomes, settle=EDITOR_SETTLE_SECONDS, timeout=REPLY_TIMEOUT_SECONDS)
        for line in long_lines[:-1]
    ]
    refused = reply_to(
        writer,
        long_lines[-1],
        [LIMIT_REFUSED, TEXT_EDITOR_PROMPT],
        settle=EDITOR_SETTLE_SECONDS,
        timeout=REPLY_TIMEOUT_SECONDS,
    )
    shown = reply_to(writer, "%r", [TEXT_EDITOR_CURSOR], settle=EDITOR_SETTLE_SECONDS, timeout=REPLY_TIMEOUT_SECONDS)
    writer.send_line("%q")

    assert all(LIMIT_REFUSED not in reply for reply in fitting_replies), fitting_replies
    assert LIMIT_REFUSED in raw_lines(refused), refused
    assert text_after(shown, TEXT_SO_FAR) == long_lines[:-1], shown
