"""Pins on help, color, set, stat and wizset, and controls for the bugs found beside them.

The colour, ANSI, SET and uruk help entries, the colour slots and their replies, every preference
stat names, wizset's field matching and stat obj's flag names.
"""

from __future__ import annotations

import re
from collections.abc import Sequence, Set
from pathlib import Path

import pytest

from rots_harness import world
from rots_harness.characters import IMPLEMENTOR, Character
from rots_harness.game_text import DROP_FORMAT
from rots_harness.immortal_commands import give_new_object, goto, load_object
from rots_harness.replies import SETTLE_SECONDS, reply_to
from rots_harness.session import GameSession
from rots_harness.stat_reading import OBJECT_STAT_LAST_LINE, read_stat
from rots_harness.text_lines import first_number, raw_lines, stripped_lines

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
# The keyword the implementor names the player by.
PLAYER_KEYWORD = PLAYER.name.lower()

# Every topic that reaches the colour entry: its four keywords, an upper-case one and a prefix.
COLOUR_HELP_TOPICS = ("color", "colour", "colors", "colours", "COLOR", "col")
# The colour entry's keyword line in lib/text/help_tbl, which do_help (act_info.cpp) prints first.
COLOUR_ENTRY_KEYWORD_LINE = "COLOR COLOUR COLORS COLOURS"
# The end of the slot list, naming the mob slot.
MOB_SLOT_LIST_END = "enemy description group magic weather mob"
# The start of the sentence explaining the mob slot.
MOB_SLOT_SENTENCE_START = "'mob' colours the"
# The hex-format sentence, which must stay inside the entry: a help line starting with '#' ends
# it.
HEX_FORMAT_SENTENCE = "Hex values must look like #RRGGBB."
# The two lines of the entry's closing sentence about true colour.
TRUE_COLOUR_TAIL_LINES = (
    "If your terminal does not fully support true colour, these colours may",
    "not display exactly as chosen.",
)
# The ANSI entry's keyword line.
ANSI_ENTRY_KEYWORD_LINE = "ANSI"
# The ANSI entry's sentence sending the reader on to HELP COLOUR.
ANSI_POINTER_SENTENCE = "This page covers the classic ANSI colours only."
# The SET entry's keyword line.
SET_ENTRY_KEYWORD_LINE = "SET"
# The end of the SET entry's See-also list.
SET_SEE_ALSO_TAIL = "NOSUMMON, ECHO, INCOGNITO, SWIM, LATIN1"
# A name SET's See-also list and the help index must not carry; no set option has it either.
AUTOEXIT_NAME = "AUTOEXIT"
# What `help autoexit` answers (do_help, act_info.cpp).
NO_AUTOEXIT_ENTRY = "There is no entry for 'autoexit' in the general chapter."
# The uruk-hai entry's keyword line.
URUK_HAI_KEYWORD_LINE = "URUK-HAI URUK"
# The uruk-hai entry's last line.
URUK_HAI_LAST_LINE = "bonus to the bash skill."
# The uruk-lhuth entry's keyword line.
URUK_LHUTH_KEYWORD_LINE = 'LHUTH "URUK-LHUTH"'
# The uruk-lhuth entry's last line.
URUK_LHUTH_LAST_LINE = "Uruk-lhuth. See: MAN SPELL LHUTH"
# What the pager prints after each full page (game loop prompts, comm.cpp).
PAGER_PROMPT = "*** Press return to continue, q to quit ***"
# Lines on each page the pager shows (show_string, modify.cpp).
PAGE_LINES = 22
# The start of what do_help answers when no keyword matches.
NO_ENTRY_START = "There is no entry for"

# The stat line with the player flags (do_stat_character, act_wiz.cpp): names separated by
# spaces, then the period sprintbit adds.
PLAYER_FLAG_LINE_PATTERN = re.compile(r"PLR: ([^\r\n]*)")
# The stat line with the preference flags, in the same form.
PREFERENCE_LINE_PATTERN = re.compile(r"PRF: ([^\r\n]*)")
# The stat line with the affect bits, in the same form.
AFFECT_LINE_PATTERN = re.compile(r"AFF: ([^\r\n]*)")
# The stat line with the coins (do_stat_character).
COINS_PATTERN = re.compile(r"Coins: \[\s*(-?\d+)\]")
# The absorb figure in info (do_info, act_info.cpp).
INFO_ABSORB_PATTERN = re.compile(r"Your armour absorbs about (-?\d+)% damage")
# The text sprintbit (utility.cpp) prints when it cannot name a flag word.
SPRINTBIT_ERROR_PART = "SPRINTBIT ERROR"
# preference_bits (consts.cpp): one name per PRF_* bit, in bit order.
PREFERENCE_NAMES_IN_BIT_ORDER = (
    "BRIEF", "COMPACT", "NARR", "!TELL", "MENTAL", "SWIM", "NOTH2", "PRMPT", "D_TEXT", "!HASS",
    "!UNUS10", "SUMN", "ECHO", "LIGHT", "COLOR", "SING", "WIZ", "LOG1", "LOG2", "LOG3", "CHAT",
    "!UNUS21", "RMFLG", "SPAM", "MSDP", "WRAP", "LATIN1", "SPINNER", "SORT1", "SORT2", "ADVVIEW",
    "ADVPRMPT",
)
# The preferences a new character starts with (the default flags in db.cpp), in bit order.
DEFAULT_PREFERENCE_NAMES = ["NARR", "PRMPT", "ECHO", "SING", "WIZ", "CHAT", "SPAM", "SPINNER"]
# Names stat must not show for bits 10, 21 and 28 to 31.
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
# rather than "... is now off."; the matrix accepts either.
ADVANCED_KEYWORDS = ("advancedview", "advancedprompt")
# Those two status texts.
ADVANCED_OFF_REPLIES_TODAY = ("Advanced view is off.", "Advanced prompt is off.")
# What turning line wrapping on answers. Wrapping is an account setting that also reaches the
# implementor, and it hard-wraps every line at the screen width, so it is never on while stat is
# read.
WRAP_ON_REPLY = "Line wrapping is now on."
# What turning line wrapping off answers.
WRAP_OFF_REPLY = "Line wrapping is now off."
# What do_gen_tog answers an argument that is not on, off or toggle.
TOGGLE_OPTIONS_REPLY = "Options are on | off | toggle."
# What do_set answers an unknown keyword.
SET_ARGUMENTS_REPLY = "Possible arguments are:"
# The `set` line for the chat channel (do_toggle, act_info.cpp).
CHAT_CHANNEL_PATTERN = re.compile(r"Chat Channel: (ON|OFF)")
# What `set chat off` answers (tog_messages, act_othe.cpp).
CHAT_OFF_REPLY = "You are now deaf to chat."
# What `set incognito on` answers.
INCOGNITO_ON_REPLY = "Incognito mode is now on."
# The name stat gives PLR_INCOGNITO (player_bits, consts.cpp).
INCOGNITO_FLAG_NAME = "INCOG"

# What wizset answers a field name that matches no row (do_wizset, act_wiz.cpp).
UNKNOWN_FIELD_REPLY = "Can't set that!"
# AFF_DETECT_MAGIC (bit 4) as the value wizset's affected field takes.
DETECT_MAGIC_BIT_VALUE = 16
# AFF_DETECT_MAGIC's name in affected_bits (consts.cpp).
DETECT_MAGIC_NAME = "DET-MAGIC"

# What the colour commands answer with colours off (color.cpp).
COLOURS_OFF_REPLY = "Your colours are turned off."
# What the mail board answers a letter with no addressee (boards.cpp).
MAIL_NO_ADDRESSEE_REPLY = "Send the letter to whom?"
# What the mail board answers a letter to nobody.
MAIL_NO_SUCH_PERSON_REPLY = "No such person."
# What `color on` answers (do_color, color.cpp).
COLOURS_ON_REPLY = "Colours turned on."
# The header of do_color's colour list.
COLOURS_LIST_HEADER = "Your colours are:"
# What `color default` answers.
DEFAULT_COLOURS_REPLY = "Ok, you'll use the default colour set."
# The header of the argument list do_color gives a name matching no slot.
COLOUR_ARGUMENTS_HEADER = "Possible arguments are:"
# The start of do_color's reply to a set slot.
COLOUR_SLOT_SET_START = "You colour "
# The start of do_color's reply to a set foreground.
COLOUR_FOREGROUND_SET_START = "You set "
# The header of the colour list do_color gives an unknown colour.
UNKNOWN_COLOUR_HEADER = "Possible colours are:"
# The header of do_color's usage text.
COLOUR_USAGE_HEADER = "Usage:"
# What do_color answers once it sets the mob slot to red.
MOB_SLOT_SET_REPLY = "You colour mob red."
# The argument list's line naming every slot and the on, off and default arguments.
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

# The stone girdle's (world.OBJECT_STONE_GIRDLE) wear flags in stat obj: TAKE and WAIST.
GIRDLE_WEAR_LINE = "Can be worn on:  TAKE WAIST."
# The stone girdle's extra flags in stat obj: WILLPOWER, DWARF and STAY_ZONE.
GIRDLE_EXTRA_FLAGS_LINE = "Extra flags   :  WILLPOWER DWARF STAY_ZONE."
# The affect list on stat obj's last line.
OBJECT_AFFECT_LINE_PATTERN = re.compile(r"Affections:([^\r\n]*)")
# The start of the stone girdle's one affect, +3 on location 6, APPLY_LEA (structs.h).
GIRDLE_AFFECT_START = " +3 to "
# What the player reads as it puts on the chain shirt (wear_message, act_obj2.cpp).
CHAIN_SHIRT_WORN_REPLY = "You wear a chain shirt on your body."


def help_entry_lines(lib_dir: Path, keyword_line: str) -> list[str]:
    """Return the help entry as do_help sends it.

    The entry runs from its keyword line up to the next line starting with '#'.
    """
    help_text = (lib_dir / "text" / "help_tbl").read_text(encoding="latin-1")
    help_lines = help_text.splitlines()
    first = help_lines.index(keyword_line)
    after = next(
        number for number in range(first + 1, len(help_lines)) if help_lines[number].startswith("#")
    )
    return help_lines[first:after]


def read_help_entry(session: GameSession, topic: str, entry_lines: Sequence[str]) -> str:
    """Ask for help on topic, page through the reply and return all of it.

    The reply ends at the entry's last non-blank line (or at the no-entry answer); every page
    before it ends at the pager prompt and is followed by a Return. The entry's own length
    bounds the pages.
    """
    last_line = next(line for line in reversed(entry_lines) if line.strip())
    page_limit = len(entry_lines) // PAGE_LINES + 1
    session.send_line(f"help {topic}")
    text = ""
    for page_number in range(1, page_limit + 2):
        page = session.expect([PAGER_PROMPT, last_line, NO_ENTRY_START])
        text += page
        if PAGER_PROMPT not in page:
            return text + session.drain(SETTLE_SECONDS)

        session.send_line("")

    raise AssertionError(f"help {topic} ran past {page_number} pages:\n{text}")


def read_uruk_help(session: GameSession, topic: str) -> str:
    """Ask for help on topic, which can answer with either uruk entry, and return the reply."""
    outcomes = [URUK_HAI_LAST_LINE, URUK_LHUTH_LAST_LINE, NO_ENTRY_START]
    return reply_to(session, f"help {topic}", outcomes)


def flag_names(stat: str, pattern: re.Pattern[str]) -> list[str]:
    """Return the flag names on one stat line, in the order stat prints them; empty for <NONE>."""
    line = pattern.search(stat)
    assert line is not None, stat
    flags_text = line.group(1)
    without_period = flags_text.rstrip(".")
    names = without_period.split()
    return [] if names == ["<NONE>"] else names


def in_bit_order(names: Set[str]) -> list[str]:
    """Return the given preference names in the order of their bits."""
    return [name for name in PREFERENCE_NAMES_IN_BIT_ORDER if name in names]


def set_preference(session: GameSession, keyword: str, state: str, replies: Sequence[str]) -> str:
    """Send `set <keyword> <state>`; return the reply once one of replies or a refusal arrives."""
    outcomes = [*replies, TOGGLE_OPTIONS_REPLY, SET_ARGUMENTS_REPLY]
    return reply_to(session, f"set {keyword} {state}", outcomes, settle=0)


def set_display(session: GameSession, pattern: re.Pattern[str]) -> str:
    """Return the value `set` shows on the line pattern matches."""
    display = session.command("set")
    value = pattern.search(display.text)
    assert value is not None, display.text
    return value.group(1)


def send_wizset(implementor: GameSession, field_and_value: str) -> str:
    """Send `wizset player testreader <field_and_value>` and return the reply's text."""
    reply = implementor.command(f"wizset player {PLAYER_KEYWORD} {field_and_value}")
    return reply.text


def join_player(implementor: GameSession) -> None:
    """Bring the implementor to the player in the arena centre."""
    goto(implementor, PLAYER_KEYWORD, world.ROOM_ARENA_CENTRE_NAME)


def put_board_beside_player(implementor: GameSession) -> None:
    """Bring the implementor to the player and have it drop a harness token, a board, there."""
    join_player(implementor)
    load_object(implementor, world.OBJECT_HARNESS_TOKEN, world.OBJECT_HARNESS_TOKEN_SHORT_DESCRIPTION)
    dropped = implementor.command("drop token")
    assert dropped.contains(DROP_FORMAT.format(world.OBJECT_HARNESS_TOKEN_SHORT_DESCRIPTION)), dropped.text


def colour_command(session: GameSession, command: str, replies: Sequence[str]) -> str:
    """Send a colour command and return its reply once one of replies arrives."""
    return reply_to(session, command, replies)


def slot_row(slot: str, colour: str) -> str:
    """Return one colour list row (show_color_slot_summary, color.cpp) for an ANSI foreground."""
    return f"{slot:>11}: fg ansi {colour} bg default"


def armour_the_player(implementor: GameSession, player: GameSession) -> int:
    """Give the player the chain shirt to wear and return the absorb its own info shows."""
    join_player(implementor)
    give_new_object(
        implementor,
        world.OBJECT_CHAIN_SHIRT,
        "shirt",
        PLAYER_KEYWORD,
        short_description=world.OBJECT_CHAIN_SHIRT_SHORT_DESCRIPTION,
    )
    worn = player.command("wear shirt")
    assert worn.contains(CHAIN_SHIRT_WORN_REPLY), worn.text
    info = player.command("info")
    return first_number(INFO_ABSORB_PATTERN, info.text)


@pytest.mark.characters(IMPLEMENTOR, PLAYER)
def test_every_spelling_of_help_color_shows_the_whole_colour_entry(server) -> None:
    """Pins that every keyword, an upper-case one and a prefix page through the whole colour entry.

    do_help (act_info.cpp) finds the topic by a case-insensitive prefix in the sorted keyword
    index and sends the entry's lines up to the next line starting with '#'. The four spellings
    are this entry's keywords and the entry names the mob slot; "#RRGGBB" stays off the start of a
    line, where it would end the entry before its closing sentence about true colour.
    """
    player = server.login(PLAYER.name)
    entry_lines = help_entry_lines(server.lib_dir, COLOUR_ENTRY_KEYWORD_LINE)

    for topic in COLOUR_HELP_TOPICS:
        entry = read_help_entry(player, topic, entry_lines)
        assert NO_ENTRY_START not in entry, entry
        assert COLOUR_ENTRY_KEYWORD_LINE in entry, entry
        assert MOB_SLOT_LIST_END in entry, entry
        assert MOB_SLOT_SENTENCE_START in entry, entry
        assert HEX_FORMAT_SENTENCE in entry, entry
        for tail_line in TRUE_COLOUR_TAIL_LINES:
            assert tail_line in entry, entry


@pytest.mark.characters(IMPLEMENTOR, PLAYER)
def test_help_ansi_sends_the_reader_on_to_help_colour(server) -> None:
    """Pins that the ANSI entry ends by saying it covers the classic colours, naming HELP COLOUR."""
    player = server.login(PLAYER.name)

    entry_lines = help_entry_lines(server.lib_dir, ANSI_ENTRY_KEYWORD_LINE)
    entry = read_help_entry(player, "ansi", entry_lines)
    assert NO_ENTRY_START not in entry, entry
    assert ANSI_ENTRY_KEYWORD_LINE in stripped_lines(entry), entry
    assert ANSI_POINTER_SENTENCE in entry, entry


@pytest.mark.characters(IMPLEMENTOR, PLAYER)
def test_help_set_no_longer_points_to_autoexit(server) -> None:
    """Pins that SET's See-also list ends without AUTOEXIT, which names no option or entry."""
    player = server.login(PLAYER.name)

    entry_lines = help_entry_lines(server.lib_dir, SET_ENTRY_KEYWORD_LINE)
    entry = read_help_entry(player, "set", entry_lines)
    assert NO_ENTRY_START not in entry, entry
    assert SET_SEE_ALSO_TAIL in entry, entry
    assert AUTOEXIT_NAME not in entry, entry

    autoexit = player.command("help autoexit")
    assert autoexit.contains(NO_AUTOEXIT_ENTRY), autoexit.text


@pytest.mark.characters(IMPLEMENTOR, PLAYER)
def test_help_uruk_hai_shows_the_uruk_hai_entry(server) -> None:
    """Pins the control for the uruk bug: the full keyword `uruk-hai` reaches the uruk-hai entry.

    The bug is that `help uruk` answers with uruk-lhuth.
    """
    player = server.login(PLAYER.name)

    entry = read_uruk_help(player, "uruk-hai")
    assert URUK_HAI_KEYWORD_LINE in entry, entry
    assert URUK_LHUTH_KEYWORD_LINE not in entry, entry


@pytest.mark.characters(IMPLEMENTOR, PLAYER)
def test_stat_names_the_advanced_prompt_preference(server) -> None:
    """Pins that turning the advanced prompt on adds exactly ADVPRMPT to stat's PRF line.

    sprintbit (utility.cpp) reads a flag word as unsigned 32 bits, so bit 31 does not make it
    print "SPRINTBIT ERROR!" in place of every flag, and preference_bits (consts.cpp) names bit 31
    ADVPRMPT and carries none of the retired names. The line starts as the default preferences a
    new character gets (db.cpp).
    """
    implementor = server.login(IMPLEMENTOR.name)
    player = server.login(PLAYER.name)

    before_stat = read_stat(implementor, f"player {PLAYER_KEYWORD}")
    before = flag_names(before_stat, PREFERENCE_LINE_PATTERN)
    assert before == DEFAULT_PREFERENCE_NAMES, before

    turned_on = set_preference(player, "advancedprompt", "on", ("Advanced prompt is now on.",))
    assert "Advanced prompt is now on." in turned_on, turned_on

    stat = read_stat(implementor, f"player {PLAYER_KEYWORD}")
    assert SPRINTBIT_ERROR_PART not in stat, stat
    after = flag_names(stat, PREFERENCE_LINE_PATTERN)
    assert set(after) - set(before) == {ADVANCED_PROMPT_FLAG_NAME}, after
    assert set(before) - set(after) == set(), after
    for retired_name in RETIRED_PREFERENCE_NAMES:
        assert retired_name not in after, after


@pytest.mark.characters(IMPLEMENTOR, PLAYER)
def test_every_preference_set_turns_on_and_off_shows_in_stat_in_bit_order(server) -> None:
    """Pins that stat's PRF line names every preference set changes, in bit order, on and off.

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

    all_on_stat = read_stat(implementor, f"player {PLAYER_KEYWORD}")
    all_on = flag_names(all_on_stat, PREFERENCE_LINE_PATTERN)
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

    all_off_stat = read_stat(implementor, f"player {PLAYER_KEYWORD}")
    all_off = flag_names(all_off_stat, PREFERENCE_LINE_PATTERN)
    assert all_off == in_bit_order(set(DEFAULT_PREFERENCE_NAMES) - toggled_names), all_off


@pytest.mark.characters(IMPLEMENTOR, PLAYER)
def test_set_shows_the_chat_channel_and_stat_shows_incognito(server) -> None:
    """Pins the controls for the incognito bugs: `set` shows chat off, and stat shows INCOG.

    The bugs are that set reads Incognito Mode from the chat bit. `set chat off` clears PRF_CHAT and
    `set incognito on` sets PLR_INCOGNITO (do_gen_tog, act_othe.cpp); `set` (do_toggle,
    act_info.cpp) shows the first, stat's PLR line the second.
    """
    implementor = server.login(IMPLEMENTOR.name)
    player = server.login(PLAYER.name)

    chat_off = set_preference(player, "chat", "off", (CHAT_OFF_REPLY,))
    assert CHAT_OFF_REPLY in chat_off, chat_off
    chat_channel = set_display(player, CHAT_CHANNEL_PATTERN)
    assert chat_channel == "OFF", chat_channel

    incognito_on = set_preference(player, "incognito", "on", (INCOGNITO_ON_REPLY,))
    assert INCOGNITO_ON_REPLY in incognito_on, incognito_on
    stat = read_stat(implementor, f"player {PLAYER_KEYWORD}")
    player_flags = flag_names(stat, PLAYER_FLAG_LINE_PATTERN)
    assert INCOGNITO_FLAG_NAME in player_flags, stat


@pytest.mark.characters(IMPLEMENTOR, PLAYER)
def test_wizset_matches_field_names_in_any_case(server) -> None:
    """Pins that wizset finds its field whatever the case the immortal types it in.

    do_wizset (act_wiz.cpp) matches the typed field against its table by prefix, whatever the
    case, so GOLD and GoLd find the "gold" row and ob finds the "OB" row. The reply names the row
    as the table spells it, and stat's coins line shows the value set. A field matching no row
    reaches the table's end and is refused. No draw is on the path.
    """
    implementor = server.login(IMPLEMENTOR.name)
    server.login(PLAYER.name)

    gold = send_wizset(implementor, "GOLD 100")
    assert UNKNOWN_FIELD_REPLY not in gold, gold
    assert f"{PLAYER.name}'s gold set to 100." in gold, gold
    stat = read_stat(implementor, f"player {PLAYER_KEYWORD}")
    assert first_number(COINS_PATTERN, stat) == 100, stat

    mixed_case = send_wizset(implementor, "GoLd 7")
    assert f"{PLAYER.name}'s gold set to 7." in mixed_case, mixed_case
    stat = read_stat(implementor, f"player {PLAYER_KEYWORD}")
    assert first_number(COINS_PATTERN, stat) == 7, stat

    offence = send_wizset(implementor, "ob 5")
    assert UNKNOWN_FIELD_REPLY not in offence, offence
    assert f"{PLAYER.name}'s OB set to 5." in offence, offence

    unknown = send_wizset(implementor, "ZZZ 5")
    assert UNKNOWN_FIELD_REPLY in unknown, unknown


@pytest.mark.characters(IMPLEMENTOR, PLAYER)
def test_stat_shows_an_affect_bit_on_one_aff_line(server) -> None:
    """Pins the control for the hallucinate bug: an affect bit is named on one AFF line.

    The bug is that HALLU breaks the AFF line. wizset's affected field sets the affect bits
    unclamped (do_wizset, act_wiz.cpp); stat's AFF line names them through sprintbit (utility.cpp)
    and ends with its period.
    """
    implementor = server.login(IMPLEMENTOR.name)
    server.login(PLAYER.name)

    reply = send_wizset(implementor, f"affected {DETECT_MAGIC_BIT_VALUE}")
    assert f"{PLAYER.name}'s affected set to {DETECT_MAGIC_BIT_VALUE}." in reply, reply
    stat = read_stat(implementor, f"player {PLAYER_KEYWORD}")
    affect_line = AFFECT_LINE_PATTERN.search(stat)
    assert affect_line is not None, stat
    assert affect_line.group(1) == f" {DETECT_MAGIC_NAME}.", affect_line.group(0)


@pytest.mark.characters(IMPLEMENTOR, PLAYER)
def test_colour_works_beside_a_board(server) -> None:
    """Pins the control for the board bug: `colour` works with a board in the room.

    The bug is that a board takes `color` as mail. colour is command 166, which no board special
    takes (gen_board, boards.cpp), so its replies are do_color's own (color.cpp). The player starts
    with colours off.
    """
    implementor = server.login(IMPLEMENTOR.name)
    player = server.login(PLAYER.name)
    put_board_beside_player(implementor)

    listed = colour_command(player, "colour", [COLOURS_OFF_REPLY, COLOURS_LIST_HEADER, MAIL_NO_ADDRESSEE_REPLY])
    assert COLOURS_OFF_REPLY in listed, listed
    slot_set = colour_command(player, "colour mob red", [MOB_SLOT_SET_REPLY, MAIL_NO_SUCH_PERSON_REPLY])
    assert MOB_SLOT_SET_REPLY in slot_set, slot_set


@pytest.mark.characters(IMPLEMENTOR, PLAYER)
def test_color_default_gives_every_slot_its_default_colour(server) -> None:
    """Pins that `color default` turns colours on and lists all 16 slots, mob in green.

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
    """Pins color's slot prefixes, the fg ansi form, and the argument list naming the mob slot.

    do_color (color.cpp) takes the first slot in table order the typed name starts, so "m" is
    magic, which comes before mob, and "mo" is mob. `fg ansi` sets the foreground with its own
    reply. A name matching no slot lists every slot and the on, off and default arguments.
    """
    server.login(IMPLEMENTOR.name)
    player = server.login(PLAYER.name)

    turned_on = colour_command(player, "color on", [COLOURS_ON_REPLY])
    assert COLOURS_ON_REPLY in turned_on, turned_on
    slot_replies = [
        COLOUR_SLOT_SET_START,
        COLOUR_FOREGROUND_SET_START,
        UNKNOWN_COLOUR_HEADER,
        COLOUR_USAGE_HEADER,
        COLOUR_ARGUMENTS_HEADER,
    ]
    foreground = colour_command(player, "color mob fg ansi blue", slot_replies)
    assert "You set mob foreground to blue." in foreground, foreground
    magic = colour_command(player, "color m bright magenta", slot_replies)
    assert "You colour magic bright magenta." in magic, magic
    mob = colour_command(player, "color mo red", slot_replies)
    assert MOB_SLOT_SET_REPLY in mob, mob

    listed = colour_command(player, "colour", [LAST_SLOT_ROW_START, COLOURS_OFF_REPLY])
    assert slot_row("magic", "bright magenta") in listed, listed
    assert slot_row("mob", "red") in listed, listed

    unknown = colour_command(player, "color zzz", slot_replies)
    assert COLOUR_ARGUMENTS_HEADER in unknown, unknown
    assert COLOUR_ARGUMENTS_LINE in raw_lines(unknown), unknown


@pytest.mark.characters(IMPLEMENTOR, PLAYER)
def test_stat_obj_names_wear_and_extra_flags(server) -> None:
    """Pins that stat obj spells WAIST and names the girdle's race and STAY_ZONE flags.

    do_stat_object (act_wiz.cpp) prints both flag words through sprintbit (utility.cpp) with
    wear_bits and extra_bits (consts.cpp), and sprintbit stops at each table's "\n" terminator;
    the affect list gives the modifier, then a label from apply_types.
    """
    implementor = server.login(IMPLEMENTOR.name)
    server.login(PLAYER.name)

    load_object(implementor, world.OBJECT_STONE_GIRDLE, world.OBJECT_STONE_GIRDLE_SHORT_DESCRIPTION)
    stat = read_stat(implementor, "obj girdle", last_line=OBJECT_STAT_LAST_LINE)
    assert GIRDLE_WEAR_LINE in stat, stat
    assert GIRDLE_EXTRA_FLAGS_LINE in stat, stat
    affect_line = OBJECT_AFFECT_LINE_PATTERN.search(stat)
    assert affect_line is not None and affect_line.group(1).startswith(GIRDLE_AFFECT_START), stat


@pytest.mark.characters(IMPLEMENTOR, PLAYER)
def test_info_shows_the_absorb_of_worn_armour(server) -> None:
    """Pins the control for the absorb bug: the armoured player's info shows a non-zero absorb.

    The bug is that stat shows the viewer's absorb. get_percent_absorb (act_info.cpp) sums the
    armour worn on each body part, with no draw; the unarmoured implementor's own info shows 0.
    """
    implementor = server.login(IMPLEMENTOR.name)
    player = server.login(PLAYER.name)

    player_absorb = armour_the_player(implementor, player)
    assert player_absorb > 0, player_absorb
    own_info = implementor.command("info")
    assert first_number(INFO_ABSORB_PATTERN, own_info.text) == 0, own_info.text
