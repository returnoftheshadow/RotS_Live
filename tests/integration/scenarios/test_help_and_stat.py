"""Pins on help, color, set, stat and wizset: the help entries #292 and #298 fixed, the colour
slots and their replies, every preference stat names, wizset's field matching, stat obj's flag
names, and controls for the bugs found beside them."""

from __future__ import annotations

import re
from pathlib import Path

import pytest

from rots_harness import world
from rots_harness.characters import IMPLEMENTOR, Character
from rots_harness.session import GameSession

pytestmark = [pytest.mark.scenario, pytest.mark.help_and_stat]

# A level-12 human warrior in the arena centre: the player who reads help, sets its preferences
# and colours, and whose stat the implementor reads.
PLAYER = Character(
    name="Testreader",
    race="human",
    sex="female",
    level=12,
    load_room=world.ROOM_ARENA_CENTRE,
    class_name="warrior",
)

# Every topic that reaches the colour entry: its four keywords, an upper-case one and a prefix.
COLOUR_HELP_TOPICS = ("color", "colour", "colors", "colours", "COLOR", "col")
# The colour entry's keyword line in lib/text/help_tbl, which do_help (act_info.cpp) prints first.
COLOUR_ENTRY_KEYWORD_LINE = "COLOR COLOUR COLORS COLOURS"
# The end of the slot list, naming the mob slot (#292).
MOB_SLOT_LIST_MARKER = "enemy description group magic weather mob"
# The sentence explaining the mob slot (#292).
MOB_SLOT_SENTENCE_MARKER = "'mob' colours the"
# The hex-format sentence #298 keeps on one line: a help line starting with '#' ends the entry.
HEX_FORMAT_MARKER = "Hex values must look like #RRGGBB."
# The two lines of the entry's closing sentence about true colour.
TRUE_COLOUR_TAIL_MARKERS = (
    "If your terminal does not fully support true colour, these colours may",
    "not display exactly as chosen.",
)
# The ANSI entry's keyword line and the sentence sending the reader on to HELP COLOUR (#292).
ANSI_ENTRY_KEYWORD_LINE = "ANSI"
ANSI_POINTER_MARKER = "This page covers the classic ANSI colours only."
# The SET entry's keyword line and the end of its See-also list, which no longer names AUTOEXIT.
SET_ENTRY_KEYWORD_LINE = "SET"
SET_SEE_ALSO_TAIL = "NOSUMMON, ECHO, INCOGNITO, SWIM, LATIN1"
# The option #292 dropped from SET's See-also list; no entry and no set option carry its name.
AUTOEXIT_NAME = "AUTOEXIT"
NO_AUTOEXIT_ENTRY = "There is no entry for 'autoexit' in the general chapter."
# The uruk-hai entry's keyword line and its last line, and the uruk-lhuth entry's.
URUK_HAI_KEYWORD_LINE = "URUK-HAI URUK"
URUK_HAI_LAST_LINE = "bonus to the bash skill."
URUK_LHUTH_KEYWORD_LINE = 'LHUTH "URUK-LHUTH"'
URUK_LHUTH_LAST_LINE = "Uruk-lhuth. See: MAN SPELL LHUTH"
# What the pager prints after each full page (game loop prompts, comm.cpp).
PAGER_PROMPT = "*** Press return to continue, q to quit ***"
# Lines on each page the pager shows (show_string, modify.cpp).
PAGE_LINES = 22
# The start of what do_help answers when no keyword matches.
NO_ENTRY_MARKER = "There is no entry for"

# The stat line just before the affect lines (do_stat_character, act_wiz.cpp): once it arrives,
# the drain collects the rest of the reply.
STAT_LAST_LINE_MARKER = "VUL:"
# The stat lines with the player flags, the preference flags and the affect bits
# (do_stat_character): names separated by spaces, then the period sprintbit adds.
PLAYER_FLAG_LINE_PATTERN = re.compile(r"PLR: ([^\r\n]*)")
PREFERENCE_LINE_PATTERN = re.compile(r"PRF: ([^\r\n]*)")
AFFECT_LINE_PATTERN = re.compile(r"AFF: ([^\r\n]*)")
# The stat line with the coins (do_stat_character).
COINS_PATTERN = re.compile(r"Coins: \[\s*(-?\d+)\]")
# The absorb figure in info (do_info, act_info.cpp).
INFO_ABSORB_PATTERN = re.compile(r"Your armour absorbs about (-?\d+)% damage")
# What sprintbit (utility.cpp) printed for a flag word with bit 31 set before #332.
SPRINTBIT_ERROR_MARKER = "SPRINTBIT ERROR"
# preference_bits (consts.cpp): one name per PRF_* bit, in bit order.
PREFERENCE_NAMES_IN_BIT_ORDER = (
    "BRIEF", "COMPACT", "NARR", "!TELL", "MENTAL", "SWIM", "NOTH2", "PRMPT", "D_TEXT", "!HASS",
    "!UNUS10", "SUMN", "ECHO", "LIGHT", "COLOR", "SING", "WIZ", "LOG1", "LOG2", "LOG3", "CHAT",
    "!UNUS21", "RMFLG", "SPAM", "MSDP", "WRAP", "LATIN1", "SPINNER", "SORT1", "SORT2", "ADVVIEW",
    "ADVPRMPT",
)
# The preferences a new character starts with (the default flags in db.cpp), in bit order.
DEFAULT_PREFERENCE_NAMES = ["NARR", "PRMPT", "ECHO", "SING", "WIZ", "CHAT", "SPAM", "SPINNER"]
# Names preference_bits gave bits 10, 21 and 28 to 31 before #332.
RETIRED_PREFERENCE_NAMES = ("!AUC", "AUTOEX", "!GTZ", "QUEST", "!UNUS1", "!UNUS2", "!UNUS3")
# The name stat gives PRF_ADVANCED_PROMPT, bit 31 of the preference flags.
ADVANCED_PROMPT_FLAG_NAME = "ADVPRMPT"
# Each preference a mortal turns on and off with `set`: its keyword (change_comm, act_othe.cpp),
# its stat name and the replies tog_messages (act_othe.cpp) gives for on and off. Advanced
# prompt is last because it changes the prompt.
PREFERENCE_TOGGLES = (
    ("brief", "BRIEF", "Brief mode now on.", "Brief mode now off."),
    ("compact", "COMPACT", "Compact mode now on.", "Compact mode now off."),
    ("narrate", "NARR", "You can now hear narrates.", "You are now deaf to narrates."),
    ("notell", "!TELL", "You are now deaf to tells.", "You can now hear tells."),
    ("mental", "MENTAL", "AutoMental mode is now on.", "AutoMental mode is now off."),
    ("swim", "SWIM", "You will now attempt to swim if needed.", "You will no longer attempt to swim."),
    ("prompt", "PRMPT", "Prompt is now on.", "Prompt is now off."),
    ("nosummon", "SUMN", "You are now safe from summoning by other players.",
     "You may now be summoned by other players."),
    ("echo", "ECHO", "You will now have your communication repeated.",
     "You will no longer have your communication repeated."),
    ("sing", "SING", "You can now hear singing.", "You are now deaf to singing."),
    ("chat", "CHAT", "You can now hear chat.", "You are now deaf to chat."),
    ("spam", "SPAM", "Spam mode now on.", "Spam mode now off."),
    ("msdp", "MSDP", "Msdp mode is now on.", "Msdp mode is now off."),
    ("latin1", "LATIN1", "You will now use the latin-1 character set.",
     "You will no longer use the latin-1 character set."),
    ("spinner", "SPINNER", "You will now see a spinner during skill and spell delays.",
     "You will no longer see a spinner during skill and spell delays."),
    ("advancedview", "ADVVIEW", "Advanced view is now on.", "Advanced view is now off."),
    ("advancedprompt", "ADVPRMPT", "Advanced prompt is now on.", "Advanced prompt is now off."),
)
# The two preferences whose off reply is a status text today ("... is off.", tog_messages)
# rather than "... is now off.", and those two texts; the matrix accepts either.
ADVANCED_KEYWORDS = ("advancedview", "advancedprompt")
ADVANCED_OFF_REPLIES_TODAY = ("Advanced view is off.", "Advanced prompt is off.")
# Line wrapping's replies. Wrapping is an account setting that also reaches the implementor, and
# it hard-wraps every line at the screen width, so it is never on while stat is read.
WRAP_ON_REPLY = "Line wrapping is now on."
WRAP_OFF_REPLY = "Line wrapping is now off."
# What do_gen_tog answers an argument that is not on, off or toggle, and do_set an unknown keyword.
TOGGLE_OPTIONS_MARKER = "Options are on | off | toggle."
SET_ARGUMENTS_MARKER = "Possible arguments are:"
# The `set` line for the chat channel (do_toggle, act_info.cpp).
CHAT_CHANNEL_PATTERN = re.compile(r"Chat Channel: (ON|OFF)")
CHAT_OFF_REPLY = "You are now deaf to chat."
INCOGNITO_ON_REPLY = "Incognito mode is now on."
# The name stat gives PLR_INCOGNITO (player_bits, consts.cpp).
INCOGNITO_FLAG_NAME = "INCOG"

# What wizset answers a field name that matches no row (do_wizset, act_wiz.cpp).
UNKNOWN_FIELD_MARKER = "Can't set that!"
# AFF_DETECT_MAGIC (bit 4) as the value wizset's affected field takes, and its name in
# affected_bits (consts.cpp).
DETECT_MAGIC_BIT_VALUE = 16
DETECT_MAGIC_NAME = "DET-MAGIC"

# The harness token (object 1130), a board: spec_ass.cpp assigns gen_board to its vnum.
BOARD_TOKEN_VNUM = 1130
# What the colour commands answer with colours off, and what the mail board answers a letter
# with no addressee and one to nobody (boards.cpp).
COLOURS_OFF_REPLY = "Your colours are turned off."
MAIL_NO_ADDRESSEE_REPLY = "Send the letter to whom?"
MAIL_NO_SUCH_PERSON_REPLY = "No such person."
# do_color's replies (color.cpp); a reply to a set slot starts "You colour " or "You set ".
COLOURS_ON_REPLY = "Colours turned on."
COLOURS_LIST_HEADER = "Your colours are:"
DEFAULT_COLOURS_REPLY = "Ok, you'll use the default colour set."
COLOUR_ARGUMENTS_HEADER = "Possible arguments are:"
COLOUR_SLOT_SET_PREFIX = "You colour "
COLOUR_FOREGROUND_SET_PREFIX = "You set "
UNKNOWN_COLOUR_HEADER = "Possible colours are:"
COLOUR_USAGE_HEADER = "Usage:"
COLOUR_ARGUMENTS_LINE = (
    " narrate chat yell tell say roomname hit damage character object enemy description group"
    " magic weather mob off on default"
)
# Each colour slot in show order with the colour set_colors_default (color.cpp) gives it.
DEFAULT_SLOT_COLOURS = (
    ("narrate", "yellow"), ("chat", "magenta"), ("yell", "red"), ("tell", "green"),
    ("say", "cyan"), ("roomname", "yellow"), ("hit", "cyan"), ("damage", "red"),
    ("character", "green"), ("object", "cyan"), ("enemy", "bright white"),
    ("description", "green"), ("group", "green"), ("magic", "bright magenta"),
    ("weather", "bright cyan"), ("mob", "green"),
)

# The start of the colour list's last row, the mob slot's.
LAST_SLOT_ROW_START = f"{'mob':>11}: fg "

# The test world's stone girdle (object 1185): extra flags WILLPOWER, DWARF and STAY_ZONE, wear
# flags TAKE and WAIST, and one affect of +3 on location 6, APPLY_LEA (structs.h).
GIRDLE_VNUM = 1185
GIRDLE_WEAR_LINE = "Can be worn on:  TAKE WAIST."
GIRDLE_EXTRA_FLAGS_LINE = "Extra flags   :  WILLPOWER DWARF STAY_ZONE."
OBJECT_AFFECT_LINE_PATTERN = re.compile(r"Affections:([^\r\n]*)")
GIRDLE_AFFECT_PREFIX = " +3 to "
# The test world's chain shirt (object 1186): body armour whose value[1] gives its wearer a
# non-zero absorb (get_percent_absorb, act_info.cpp).
CHAIN_SHIRT_VNUM = 1186
# The name of the player's room, world.ROOM_ARENA_CENTRE, as goto shows it.
ARENA_CENTRE_NAME = "Arena Centre"
# Seconds to keep reading after a reply, for text already sent to arrive.
SETTLE_SECONDS = 1.0


def help_entry_lines(lib_dir: Path, keyword_line: str) -> list:
    """The help entry as do_help sends it: from its keyword line up to the next line starting with '#'."""
    help_lines = (lib_dir / "text" / "help_tbl").read_text().splitlines()
    first = help_lines.index(keyword_line)
    after = next(number for number in range(first + 1, len(help_lines)) if help_lines[number].startswith("#"))
    return help_lines[first:after]


def read_help_entry(session: GameSession, topic: str, entry_lines: list) -> str:
    """Asks for help on topic and pages through the reply; returns all of it.

    The reply ends at the entry's last non-blank line (or at the no-entry answer); every page
    before it ends at the pager prompt and is followed by a Return. The entry's own length
    bounds the pages.
    """
    last_line = next(line for line in reversed(entry_lines) if line.strip())
    page_limit = len(entry_lines) // PAGE_LINES + 1
    session.send_line(f"help {topic}")
    text = ""
    for page_number in range(1, page_limit + 2):
        page = session.expect([PAGER_PROMPT, last_line, NO_ENTRY_MARKER])
        text += page
        if PAGER_PROMPT not in page:
            return text + session.drain(SETTLE_SECONDS)
        session.send_line("")
    raise AssertionError(f"help {topic} ran past {page_number} pages:\n{text}")


def read_uruk_help(session: GameSession, topic: str) -> str:
    """Asks for help on topic, which can answer with either uruk entry, and returns the reply."""
    session.send_line(f"help {topic}")
    return session.expect([URUK_HAI_LAST_LINE, URUK_LHUTH_LAST_LINE, NO_ENTRY_MARKER]) + session.drain(SETTLE_SECONDS)


def read_stat(implementor: GameSession, name: str) -> str:
    """The implementor's `stat player <name>`, read up to its last line."""
    implementor.send_line(f"stat player {name.lower()}")
    return implementor.expect([STAT_LAST_LINE_MARKER]) + implementor.drain(SETTLE_SECONDS)


def flag_names(stat: str, pattern: re.Pattern) -> list:
    """The flag names on one stat line, in the order stat prints them; empty for <NONE>."""
    line = pattern.search(stat)
    assert line is not None, stat
    names = line.group(1).rstrip(".").split()
    return [] if names == ["<NONE>"] else names


def in_bit_order(names: set) -> list:
    """The given preference names in the order of their bits."""
    return [name for name in PREFERENCE_NAMES_IN_BIT_ORDER if name in names]


def set_preference(session: GameSession, keyword: str, state: str, replies: tuple) -> str:
    """Sends `set <keyword> <state>` and returns its reply once one of replies, or a refusal, arrives."""
    session.send_line(f"set {keyword} {state}")
    return session.expect(list(replies) + [TOGGLE_OPTIONS_MARKER, SET_ARGUMENTS_MARKER])


def set_display(session: GameSession, pattern: re.Pattern) -> str:
    """The value `set` shows on the line pattern matches."""
    display = session.command("set")
    value = pattern.search(display.text)
    assert value is not None, display.text
    return value.group(1)


def wizset(implementor: GameSession, field_and_value: str) -> str:
    """Sends `wizset player testreader <field_and_value>` and returns the reply."""
    return implementor.command(f"wizset player {PLAYER.name.lower()} {field_and_value}").text


def join_player(implementor: GameSession) -> None:
    """The implementor goes to the player in the arena centre."""
    arrived = implementor.command(f"goto {PLAYER.name.lower()}")
    assert arrived.room_name() == ARENA_CENTRE_NAME, arrived.text


def put_board_beside_player(implementor: GameSession) -> None:
    """The implementor joins the player and drops a harness token, a board, in its room."""
    join_player(implementor)
    created = implementor.command(f"load obj {BOARD_TOKEN_VNUM}")
    assert created.contains("You create a harness token."), created.text
    dropped = implementor.command("drop token")
    assert dropped.contains("You drop a harness token."), dropped.text


def colour_command(session: GameSession, command: str, replies: list) -> str:
    """Sends a colour command and returns its reply once one of replies arrives."""
    session.send_line(command)
    return session.expect(replies) + session.drain(SETTLE_SECONDS)


def slot_row(slot: str, colour: str) -> str:
    """One row of the colour list (show_color_slot_summary, color.cpp) for an ANSI foreground."""
    return f"{slot:>11}: fg ansi {colour} bg default"


def read_object_stat(implementor: GameSession, keyword: str) -> str:
    """The implementor's `stat obj <keyword>`, read up to its last line, the affect list."""
    implementor.send_line(f"stat obj {keyword}")
    return implementor.expect(["Affections:"]) + implementor.drain(SETTLE_SECONDS)


def armour_the_player(implementor: GameSession, player: GameSession) -> int:
    """Gives the player the chain shirt to wear and returns the absorb its own info shows."""
    join_player(implementor)
    created = implementor.command(f"load obj {CHAIN_SHIRT_VNUM}")
    assert created.contains("You create a chain shirt."), created.text
    given = implementor.command(f"give shirt {PLAYER.name.lower()}")
    assert given.contains("You give a chain shirt to"), given.text
    worn = player.command("wear shirt")
    assert worn.contains("You wear a chain shirt on your body."), worn.text
    info = player.command("info")
    absorb = INFO_ABSORB_PATTERN.search(info.text)
    assert absorb is not None, info.text
    return int(absorb.group(1))


@pytest.mark.characters(IMPLEMENTOR, PLAYER)
def test_every_spelling_of_help_color_shows_the_whole_colour_entry(server) -> None:
    """Pins #292 and #298: every keyword, an upper-case one and a prefix page through the whole entry.

    do_help (act_info.cpp) finds the topic by a case-insensitive prefix in the sorted keyword
    index and sends the entry's lines up to the next line starting with '#'. #292 made the four
    spellings this entry's keywords and added the mob slot; #298 kept "#RRGGBB" off the start of
    a line, where it ended the entry before its closing sentence about true colour.
    """
    player = server.login(PLAYER.name)
    entry_lines = help_entry_lines(server.lib_dir, COLOUR_ENTRY_KEYWORD_LINE)

    for topic in COLOUR_HELP_TOPICS:
        entry = read_help_entry(player, topic, entry_lines)
        assert NO_ENTRY_MARKER not in entry, entry
        assert COLOUR_ENTRY_KEYWORD_LINE in entry, entry
        assert MOB_SLOT_LIST_MARKER in entry, entry
        assert MOB_SLOT_SENTENCE_MARKER in entry, entry
        assert HEX_FORMAT_MARKER in entry, entry
        for tail_marker in TRUE_COLOUR_TAIL_MARKERS:
            assert tail_marker in entry, entry


@pytest.mark.characters(IMPLEMENTOR, PLAYER)
def test_help_ansi_sends_the_reader_on_to_help_colour(server) -> None:
    """Pins #292: the ANSI entry ends by saying it covers the classic colours and naming HELP COLOUR."""
    player = server.login(PLAYER.name)

    entry = read_help_entry(player, "ansi", help_entry_lines(server.lib_dir, ANSI_ENTRY_KEYWORD_LINE))
    assert NO_ENTRY_MARKER not in entry, entry
    assert ANSI_ENTRY_KEYWORD_LINE in [line.strip() for line in entry.splitlines()], entry
    assert ANSI_POINTER_MARKER in entry, entry


@pytest.mark.characters(IMPLEMENTOR, PLAYER)
def test_help_set_no_longer_points_to_autoexit(server) -> None:
    """Pins #292: SET's See-also list ends without AUTOEXIT, an option and entry that no longer exist."""
    player = server.login(PLAYER.name)

    entry = read_help_entry(player, "set", help_entry_lines(server.lib_dir, SET_ENTRY_KEYWORD_LINE))
    assert NO_ENTRY_MARKER not in entry, entry
    assert SET_SEE_ALSO_TAIL in entry, entry
    assert AUTOEXIT_NAME not in entry, entry

    autoexit = player.command("help autoexit")
    assert autoexit.contains(NO_AUTOEXIT_ENTRY), autoexit.text


@pytest.mark.characters(IMPLEMENTOR, PLAYER)
def test_help_uruk_hai_shows_the_uruk_hai_entry(server) -> None:
    """Pins the control for the uruk bug (`help uruk` answers with uruk-lhuth): the full keyword reaches uruk-hai."""
    player = server.login(PLAYER.name)

    entry = read_uruk_help(player, "uruk-hai")
    assert URUK_HAI_KEYWORD_LINE in entry, entry
    assert URUK_LHUTH_KEYWORD_LINE not in entry, entry


@pytest.mark.characters(IMPLEMENTOR, PLAYER)
def test_stat_names_the_advanced_prompt_preference(server) -> None:
    """Pins #332: turning the advanced prompt on adds exactly ADVPRMPT to stat's PRF line.

    sprintbit (utility.cpp) read a flag word with bit 31 set as negative and printed
    "SPRINTBIT ERROR!" in place of every flag; #332 reads it as unsigned 32 bits and renamed the
    retired preference names, and preference_bits (consts.cpp) names bit 31 ADVPRMPT. The line
    starts as the default preferences a new character gets (db.cpp).
    """
    implementor = server.login(IMPLEMENTOR.name)
    player = server.login(PLAYER.name)

    before = flag_names(read_stat(implementor, PLAYER.name), PREFERENCE_LINE_PATTERN)
    assert before == DEFAULT_PREFERENCE_NAMES, before

    turned_on = set_preference(player, "advancedprompt", "on", ("Advanced prompt is now on.",))
    assert "Advanced prompt is now on." in turned_on, turned_on

    stat = read_stat(implementor, PLAYER.name)
    assert SPRINTBIT_ERROR_MARKER not in stat, stat
    after = flag_names(stat, PREFERENCE_LINE_PATTERN)
    assert set(after) - set(before) == {ADVANCED_PROMPT_FLAG_NAME}, after
    assert set(before) - set(after) == set(), after
    for retired_name in RETIRED_PREFERENCE_NAMES:
        assert retired_name not in after, after


@pytest.mark.characters(IMPLEMENTOR, PLAYER)
def test_every_preference_set_turns_on_and_off_shows_in_stat_in_bit_order(server) -> None:
    """Pins #332: stat's PRF line names every preference set changes, in bit order, on and off.

    Each `set <preference> on|off` replies with its tog_messages line (do_gen_tog, act_othe.cpp).
    With all of them on, the PRF line is the defaults plus every one of them; with all off, only
    WIZ, which a mortal cannot change, is left. The account preferences reach the implementor too
    (ppc_propagate_from), so it reads stat with send_line and expect, never relying on a prompt.
    """
    implementor = server.login(IMPLEMENTOR.name)
    player = server.login(PLAYER.name)

    for keyword, _, on_reply, _ in PREFERENCE_TOGGLES:
        reply = set_preference(player, keyword, "on", (on_reply,))
        assert on_reply in reply, reply
    all_on = flag_names(read_stat(implementor, PLAYER.name), PREFERENCE_LINE_PATTERN)
    toggled_names = {name for _, name, _, _ in PREFERENCE_TOGGLES}
    assert all_on == in_bit_order(set(DEFAULT_PREFERENCE_NAMES) | toggled_names), all_on

    wrap_on = set_preference(player, "wrap", "on", (WRAP_ON_REPLY,))
    assert WRAP_ON_REPLY in wrap_on, wrap_on
    wrap_off = set_preference(player, "wrap", "off", (WRAP_OFF_REPLY,))
    assert WRAP_OFF_REPLY in wrap_off, wrap_off

    for keyword, _, _, off_reply in PREFERENCE_TOGGLES:
        reply = set_preference(player, keyword, "off", (off_reply,) + ADVANCED_OFF_REPLIES_TODAY)
        if keyword not in ADVANCED_KEYWORDS:
            assert off_reply in reply, reply
    all_off = flag_names(read_stat(implementor, PLAYER.name), PREFERENCE_LINE_PATTERN)
    assert all_off == in_bit_order(set(DEFAULT_PREFERENCE_NAMES) - toggled_names), all_off


@pytest.mark.characters(IMPLEMENTOR, PLAYER)
def test_set_shows_the_chat_channel_and_stat_shows_incognito(server) -> None:
    """Pins the controls for the incognito bugs (set reads Incognito Mode from the chat bit): chat off and INCOG.

    `set chat off` clears PRF_CHAT and `set incognito on` sets PLR_INCOGNITO (do_gen_tog,
    act_othe.cpp); `set` (do_toggle, act_info.cpp) shows the first, stat's PLR line the second.
    """
    implementor = server.login(IMPLEMENTOR.name)
    player = server.login(PLAYER.name)

    chat_off = set_preference(player, "chat", "off", (CHAT_OFF_REPLY,))
    assert CHAT_OFF_REPLY in chat_off, chat_off
    assert set_display(player, CHAT_CHANNEL_PATTERN) == "OFF"

    incognito_on = set_preference(player, "incognito", "on", (INCOGNITO_ON_REPLY,))
    assert INCOGNITO_ON_REPLY in incognito_on, incognito_on
    assert INCOGNITO_FLAG_NAME in flag_names(read_stat(implementor, PLAYER.name), PLAYER_FLAG_LINE_PATTERN)


@pytest.mark.characters(IMPLEMENTOR, PLAYER)
def test_wizset_matches_field_names_in_any_case(server) -> None:
    """Pins #276: wizset finds its field whatever the case the immortal types it in.

    do_wizset (act_wiz.cpp) matches the typed field against its table by prefix; #276 made the
    match case-insensitive, so GOLD and GoLd find the "gold" row and ob finds the "OB" row. The
    reply names the row as the table spells it, and stat's coins line shows the value set. A
    field matching no row reaches the table's end and is refused. No draw is on the path.
    """
    implementor = server.login(IMPLEMENTOR.name)
    server.login(PLAYER.name)

    gold = wizset(implementor, "GOLD 100")
    assert UNKNOWN_FIELD_MARKER not in gold, gold
    assert f"{PLAYER.name}'s gold set to 100." in gold, gold
    coins = COINS_PATTERN.search(read_stat(implementor, PLAYER.name))
    assert coins is not None and int(coins.group(1)) == 100

    mixed_case = wizset(implementor, "GoLd 7")
    assert f"{PLAYER.name}'s gold set to 7." in mixed_case, mixed_case
    coins = COINS_PATTERN.search(read_stat(implementor, PLAYER.name))
    assert coins is not None and int(coins.group(1)) == 7

    offence = wizset(implementor, "ob 5")
    assert UNKNOWN_FIELD_MARKER not in offence, offence
    assert f"{PLAYER.name}'s OB set to 5." in offence, offence

    unknown = wizset(implementor, "ZZZ 5")
    assert UNKNOWN_FIELD_MARKER in unknown, unknown


@pytest.mark.characters(IMPLEMENTOR, PLAYER)
def test_stat_shows_an_affect_bit_on_one_aff_line(server) -> None:
    """Pins the control for the hallucinate bug (HALLU breaks the AFF line): an affect bit is named on one AFF line.

    wizset's affected field sets the affect bits unclamped (do_wizset, act_wiz.cpp); stat's AFF
    line names them through sprintbit (utility.cpp) and ends with its period.
    """
    implementor = server.login(IMPLEMENTOR.name)
    server.login(PLAYER.name)

    reply = wizset(implementor, f"affected {DETECT_MAGIC_BIT_VALUE}")
    assert f"{PLAYER.name}'s affected set to {DETECT_MAGIC_BIT_VALUE}." in reply, reply
    affect_line = AFFECT_LINE_PATTERN.search(read_stat(implementor, PLAYER.name))
    assert affect_line is not None
    assert affect_line.group(1) == f" {DETECT_MAGIC_NAME}.", affect_line.group(0)


@pytest.mark.characters(IMPLEMENTOR, PLAYER)
def test_colour_works_beside_a_board(server) -> None:
    """Pins the control for the board bug (a board takes `color` as mail): `colour` works with a board in the room.

    colour is command 166, which no board special takes (gen_board, boards.cpp), so its replies
    are do_color's own (color.cpp). The player starts with colours off.
    """
    implementor = server.login(IMPLEMENTOR.name)
    player = server.login(PLAYER.name)
    put_board_beside_player(implementor)

    listed = colour_command(player, "colour", [COLOURS_OFF_REPLY, COLOURS_LIST_HEADER, MAIL_NO_ADDRESSEE_REPLY])
    assert COLOURS_OFF_REPLY in listed, listed
    slot_set = colour_command(player, "colour mob red", ["You colour mob red.", MAIL_NO_SUCH_PERSON_REPLY])
    assert "You colour mob red." in slot_set, slot_set


@pytest.mark.characters(IMPLEMENTOR, PLAYER)
def test_color_default_gives_every_slot_its_default_colour(server) -> None:
    """Pins #290 and #292: `color default` turns colours on and lists all 16 slots, mob in green.

    A new character starts with colours off. set_colors_default (color.cpp) turns them on and
    gives each slot its colour, mob included; the list shows each slot as
    "%11s: fg %s bg %s" (show_color_slot_summary).
    """
    server.login(IMPLEMENTOR.name)
    player = server.login(PLAYER.name)

    before = colour_command(player, "colour", [COLOURS_OFF_REPLY, COLOURS_LIST_HEADER])
    assert COLOURS_OFF_REPLY in before, before
    default = colour_command(player, "color default", [DEFAULT_COLOURS_REPLY])
    assert DEFAULT_COLOURS_REPLY in default, default

    listed = colour_command(player, "colour", [LAST_SLOT_ROW_START, COLOURS_OFF_REPLY])
    assert COLOURS_LIST_HEADER in listed, listed
    for slot, colour in DEFAULT_SLOT_COLOURS:
        assert slot_row(slot, colour) in listed, listed


@pytest.mark.characters(IMPLEMENTOR, PLAYER)
def test_color_matches_slot_names_by_prefix_and_lists_them_for_an_unknown_one(server) -> None:
    """Pins #290 and #292: slot prefixes, the fg ansi form, and the argument list naming the mob slot.

    do_color (color.cpp) takes the first slot in table order the typed name starts, so "m" is
    magic, which comes before mob, and "mo" is mob. `fg ansi` sets the foreground with its own
    reply. A name matching no slot lists every slot and the on, off and default arguments.
    """
    server.login(IMPLEMENTOR.name)
    player = server.login(PLAYER.name)

    turned_on = colour_command(player, "color on", [COLOURS_ON_REPLY])
    assert COLOURS_ON_REPLY in turned_on, turned_on
    slot_replies = [COLOUR_SLOT_SET_PREFIX, COLOUR_FOREGROUND_SET_PREFIX, UNKNOWN_COLOUR_HEADER, COLOUR_USAGE_HEADER, COLOUR_ARGUMENTS_HEADER]
    foreground = colour_command(player, "color mob fg ansi blue", slot_replies)
    assert "You set mob foreground to blue." in foreground, foreground
    magic = colour_command(player, "color m bright magenta", slot_replies)
    assert "You colour magic bright magenta." in magic, magic
    mob = colour_command(player, "color mo red", slot_replies)
    assert "You colour mob red." in mob, mob

    listed = colour_command(player, "colour", [LAST_SLOT_ROW_START, COLOURS_OFF_REPLY])
    assert slot_row("magic", "bright magenta") in listed, listed
    assert slot_row("mob", "red") in listed, listed

    unknown = colour_command(player, "color zzz", slot_replies)
    assert COLOUR_ARGUMENTS_HEADER in unknown, unknown
    assert COLOUR_ARGUMENTS_LINE in [line.strip("\r") for line in unknown.split("\n")], unknown


@pytest.mark.characters(IMPLEMENTOR, PLAYER)
def test_stat_obj_names_wear_and_extra_flags(server) -> None:
    """Pins #332: stat obj spells the waist wear flag WAIST and names the girdle's race and STAY_ZONE flags.

    do_stat_object (act_wiz.cpp) prints both flag words through sprintbit (utility.cpp) with
    wear_bits and extra_bits (consts.cpp). #332 renamed WAISTE to WAIST and made sprintbit stop
    at each table's "\n" terminator; the affect list gives the modifier, then a label from
    apply_types.
    """
    implementor = server.login(IMPLEMENTOR.name)
    server.login(PLAYER.name)

    created = implementor.command(f"load obj {GIRDLE_VNUM}")
    assert created.contains("You create a stone girdle."), created.text
    stat = read_object_stat(implementor, "girdle")
    assert GIRDLE_WEAR_LINE in stat, stat
    assert GIRDLE_EXTRA_FLAGS_LINE in stat, stat
    affect_line = OBJECT_AFFECT_LINE_PATTERN.search(stat)
    assert affect_line is not None and affect_line.group(1).startswith(GIRDLE_AFFECT_PREFIX), stat


@pytest.mark.characters(IMPLEMENTOR, PLAYER)
def test_info_shows_the_absorb_of_worn_armour(server) -> None:
    """Pins the control for the absorb bug (stat shows the viewer's absorb): the armoured player's info shows one.

    get_percent_absorb (act_info.cpp) sums the armour worn on each body part, with no draw;
    the unarmoured implementor's own info shows 0.
    """
    implementor = server.login(IMPLEMENTOR.name)
    player = server.login(PLAYER.name)

    assert armour_the_player(implementor, player) > 0
    own_info = implementor.command("info")
    own_absorb = INFO_ABSORB_PATTERN.search(own_info.text)
    assert own_absorb is not None and int(own_absorb.group(1)) == 0, own_info.text
