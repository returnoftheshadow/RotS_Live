"""Strict xfails of the correct behaviour for bugs found beside the help_and_stat pins: help's
prefix search, a board swallowing `color`, the AFF line, set's incognito display, two toggle
replies, an object affect label and stat's absorb. Each passes, and so fails the run, once fixed."""

from __future__ import annotations

import re

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

# The uruk-hai entry's keyword line and its last line, and the uruk-lhuth entry's (lib/text/help_tbl).
URUK_HAI_KEYWORD_LINE = "URUK-HAI URUK"
URUK_HAI_LAST_LINE = "bonus to the bash skill."
URUK_LHUTH_KEYWORD_LINE = 'LHUTH "URUK-LHUTH"'
URUK_LHUTH_LAST_LINE = "Uruk-lhuth. See: MAN SPELL LHUTH"
# The start of what do_help answers when no keyword matches.
NO_ENTRY_MARKER = "There is no entry for"

# The stat line just before the affect lines (do_stat_character, act_wiz.cpp): once it arrives,
# the drain collects the rest of the reply.
STAT_LAST_LINE_MARKER = "VUL:"
# The absorb figure on stat's OB line and in info (do_info, act_info.cpp).
STAT_ABSORB_PATTERN = re.compile(r"absorb (-?\d+)")
INFO_ABSORB_PATTERN = re.compile(r"Your armour absorbs about (-?\d+)% damage")

# The advanced view and advanced prompt toggles: keyword, on reply, the off reply every other
# preference's pattern gives, and the status text tog_messages (act_othe.cpp) gives instead.
ADVANCED_TOGGLES = (
    ("advancedview", "Advanced view is now on.", "Advanced view is now off.", "Advanced view is off."),
    ("advancedprompt", "Advanced prompt is now on.", "Advanced prompt is now off.", "Advanced prompt is off."),
)
# What do_gen_tog answers an argument that is not on, off or toggle, and do_set an unknown keyword.
TOGGLE_OPTIONS_MARKER = "Options are on | off | toggle."
SET_ARGUMENTS_MARKER = "Possible arguments are:"
# The `set` line for incognito (do_toggle, act_info.cpp); ONOFF pads ON to three characters.
INCOGNITO_MODE_PATTERN = re.compile(r"Incognito Mode: (ON|OFF)")
CHAT_OFF_REPLY = "You are now deaf to chat."
INCOGNITO_ON_REPLY = "Incognito mode is now on."

# AFF_HALLUCINATE (bit 31) as the signed value wizset's affected field takes, and its name in
# affected_bits (consts.cpp).
HALLUCINATE_BIT_VALUE = -2147483648
HALLUCINATE_NAME = "HALLU"

# The harness token (object 1130), a board: spec_ass.cpp assigns gen_board to its vnum.
BOARD_TOKEN_VNUM = 1130
# What the colour commands answer with colours off and do_color's list header (color.cpp), and
# what the mail board answers a letter with no addressee and one to nobody (boards.cpp).
COLOURS_OFF_REPLY = "Your colours are turned off."
COLOURS_LIST_HEADER = "Your colours are:"
MAIL_NO_ADDRESSEE_REPLY = "Send the letter to whom?"
MAIL_NO_SUCH_PERSON_REPLY = "No such person."
MOB_SLOT_SET_REPLY = "You colour mob red."

# The test world's stone girdle (object 1185), with one affect of +3 on location 6, APPLY_LEA.
GIRDLE_VNUM = 1185
OBJECT_AFFECT_LINE_PATTERN = re.compile(r"Affections:([^\r\n]*)")
# The label apply_types (consts.cpp) gives location 6 today.
STALE_LEARNING_LABEL = "SEX"
# The test world's chain shirt (object 1186): body armour that gives its wearer a non-zero absorb.
CHAIN_SHIRT_VNUM = 1186
# The name of the player's room, world.ROOM_ARENA_CENTRE, as goto shows it.
ARENA_CENTRE_NAME = "Arena Centre"
# Seconds to keep reading after a reply, for text already sent to arrive.
SETTLE_SECONDS = 1.0


def read_uruk_help(session: GameSession, topic: str) -> str:
    """Asks for help on topic, which can answer with either uruk entry, and returns the reply."""
    session.send_line(f"help {topic}")
    return session.expect([URUK_HAI_LAST_LINE, URUK_LHUTH_LAST_LINE, NO_ENTRY_MARKER]) + session.drain(SETTLE_SECONDS)


def read_stat(implementor: GameSession, name: str) -> str:
    """The implementor's `stat player <name>`, read up to its last line."""
    implementor.send_line(f"stat player {name.lower()}")
    return implementor.expect([STAT_LAST_LINE_MARKER]) + implementor.drain(SETTLE_SECONDS)


def set_preference(session: GameSession, keyword: str, state: str, replies: tuple) -> str:
    """Sends `set <keyword> <state>` and returns its reply once one of replies, or a refusal, arrives."""
    session.send_line(f"set {keyword} {state}")
    return session.expect(list(replies) + [TOGGLE_OPTIONS_MARKER, SET_ARGUMENTS_MARKER])


def incognito_display(session: GameSession) -> str:
    """The value `set` shows for Incognito Mode."""
    display = session.command("set")
    value = INCOGNITO_MODE_PATTERN.search(display.text)
    assert value is not None, display.text
    return value.group(1)


def join_player(implementor: GameSession) -> None:
    """The implementor goes to the player in the arena centre."""
    arrived = implementor.command(f"goto {PLAYER.name.lower()}")
    assert arrived.room_name() == ARENA_CENTRE_NAME, arrived.text


def colour_command(session: GameSession, command: str, replies: list) -> str:
    """Sends a colour command and returns its reply once one of replies arrives."""
    session.send_line(command)
    return session.expect(replies) + session.drain(SETTLE_SECONDS)


def read_object_stat(implementor: GameSession, keyword: str) -> str:
    """The implementor's `stat obj <keyword>`, read up to its last line, the affect list."""
    implementor.send_line(f"stat obj {keyword}")
    return implementor.expect(["Affections:"]) + implementor.drain(SETTLE_SECONDS)


@pytest.mark.xfail(strict=True, raises=AssertionError, reason="help uruk should show the URUK-HAI URUK entry, whose keywords include URUK exactly")
@pytest.mark.characters(IMPLEMENTOR, PLAYER)
def test_help_uruk_shows_the_uruk_hai_entry(server) -> None:
    """Bug: `help uruk` shows the uruk-lhuth entry, though the uruk-hai entry has URUK as an exact keyword.

    do_help (act_info.cpp:2083) binary-searches the sorted keyword index and stops at the first
    entry whose keyword starts with the topic. URUK, URUK-HAI and URUK-LHUTH all start with
    "uruk", and the search lands on URUK-LHUTH (lib/text/help_tbl:296) rather than the URUK-HAI
    URUK entry (help_tbl:278). The control, `help uruk-hai`, is in test_help_and_stat.py.
    """
    player = server.login(PLAYER.name)

    entry = read_uruk_help(player, "uruk")
    assert URUK_HAI_KEYWORD_LINE in entry, entry
    assert URUK_LHUTH_KEYWORD_LINE not in entry, entry


@pytest.mark.xfail(strict=True, raises=AssertionError, reason="color should list and set colours with a board in the room, as colour does")
@pytest.mark.characters(IMPLEMENTOR, PLAYER)
def test_color_works_beside_a_board(server) -> None:
    """Bug: with a board in the room, `color` is taken as a mail letter.

    CMD_SEND (interpre.h:329) is 171, the index of "color" in the command table
    (interpre.cpp:491). Object specials in the room run before the command (interpre.cpp:1758),
    and gen_board (boards.cpp:221, :284) turns CMD_SEND into a mail letter: `color` asks "Send
    the letter to whom?" (boards.cpp:999) and `color mob red` answers "No such person.". The
    control, `colour` beside the same board, is in test_help_and_stat.py.
    """
    implementor = server.login(IMPLEMENTOR.name)
    player = server.login(PLAYER.name)
    join_player(implementor)
    created = implementor.command(f"load obj {BOARD_TOKEN_VNUM}")
    assert created.contains("You create a harness token."), created.text
    dropped = implementor.command("drop token")
    assert dropped.contains("You drop a harness token."), dropped.text

    listed = colour_command(player, "color", [COLOURS_OFF_REPLY, COLOURS_LIST_HEADER, MAIL_NO_ADDRESSEE_REPLY])
    assert COLOURS_OFF_REPLY in listed, listed
    slot_set = colour_command(player, "color mob red", [MOB_SLOT_SET_REPLY, MAIL_NO_SUCH_PERSON_REPLY])
    assert MOB_SLOT_SET_REPLY in slot_set, slot_set


@pytest.mark.xfail(strict=True, raises=AssertionError, reason="stat should name AFF_HALLUCINATE as HALLU. on the AFF line itself")
@pytest.mark.characters(IMPLEMENTOR, PLAYER)
def test_stat_names_hallucinate_on_one_aff_line(server) -> None:
    """Bug: stat prints a line break between HALLU and the AFF line's closing period.

    In affected_bits (consts.cpp:2041-2042) "HALLU" and "\\n" have no comma between them, so C
    joins them into "HALLU\\n": sprintbit (utility.cpp) prints the name, a line break and then
    the period, and the array loses its "\\n" terminator. The control, a bit 4 set the same way,
    is in test_help_and_stat.py.
    """
    implementor = server.login(IMPLEMENTOR.name)
    server.login(PLAYER.name)

    reply = implementor.command(f"wizset player {PLAYER.name.lower()} affected {HALLUCINATE_BIT_VALUE}")
    assert reply.contains(f"{PLAYER.name}'s affected set to {HALLUCINATE_BIT_VALUE}."), reply.text
    stat = read_stat(implementor, PLAYER.name)
    assert re.search(rf"AFF:[^\r\n]*{HALLUCINATE_NAME}\.", stat), stat


@pytest.mark.xfail(strict=True, raises=AssertionError, reason="after set chat off, set should still show Incognito Mode: OFF")
@pytest.mark.characters(IMPLEMENTOR, PLAYER)
def test_turning_chat_off_leaves_incognito_off_in_set(server) -> None:
    """Bug: `set chat off` makes `set` show Incognito Mode: ON.

    do_toggle (act_info.cpp:2897) prints ONOFF(!PRF_FLAGGED(ch, PLR_INCOGNITO)): it reads the
    preference word with a player-flag constant, PLR_INCOGNITO (structs.h:1000) is PRF_CHAT's bit
    (structs.h:1023), and it is negated. The controls (Chat Channel: OFF, stat's INCOG) are in
    test_help_and_stat.py.
    """
    server.login(IMPLEMENTOR.name)
    player = server.login(PLAYER.name)

    chat_off = set_preference(player, "chat", "off", (CHAT_OFF_REPLY,))
    assert CHAT_OFF_REPLY in chat_off, chat_off
    assert incognito_display(player) == "OFF"


@pytest.mark.xfail(strict=True, raises=AssertionError, reason="after set incognito on, set should show Incognito Mode: ON")
@pytest.mark.characters(IMPLEMENTOR, PLAYER)
def test_turning_incognito_on_shows_it_on_in_set(server) -> None:
    """Bug: `set incognito on` sets PLR_INCOGNITO, but `set` still shows Incognito Mode: OFF.

    do_toggle (act_info.cpp:2897) shows the negated chat bit instead of PLR_INCOGNITO, so a
    player with chat on sees OFF; stat's PLR line shows INCOG (control in test_help_and_stat.py).
    """
    server.login(IMPLEMENTOR.name)
    player = server.login(PLAYER.name)

    incognito_on = set_preference(player, "incognito", "on", (INCOGNITO_ON_REPLY,))
    assert INCOGNITO_ON_REPLY in incognito_on, incognito_on
    assert incognito_display(player) == "ON"


@pytest.mark.xfail(strict=True, raises=AssertionError, reason="turning advancedview or advancedprompt off should say \"... is now off.\" like every other preference")
@pytest.mark.characters(IMPLEMENTOR, PLAYER)
def test_turning_the_advanced_preferences_off_says_they_are_now_off(server) -> None:
    """Bug: `set advancedview off` and `set advancedprompt off` reply with the status text.

    tog_messages (act_othe.cpp:974, :976) holds, per preference, the replies for on, off, is-on
    and is-off; for these two the off reply repeats the is-off text ("Advanced view is off.")
    where every other preference says "now".
    """
    server.login(IMPLEMENTOR.name)
    player = server.login(PLAYER.name)

    for keyword, on_reply, off_reply, status_text in ADVANCED_TOGGLES:
        turned_on = set_preference(player, keyword, "on", (on_reply,))
        assert on_reply in turned_on, turned_on
        turned_off = set_preference(player, keyword, "off", (off_reply, status_text))
        assert off_reply in turned_off, turned_off


@pytest.mark.xfail(strict=True, raises=AssertionError, reason="stat obj should not label a location-6 (APPLY_LEA, learning) affect SEX; LEA is the likely label")
@pytest.mark.characters(IMPLEMENTOR, PLAYER)
def test_stat_obj_does_not_label_a_learning_affect_sex(server) -> None:
    """Bug: stat obj calls the girdle's learning affect "+3 to SEX".

    apply_types (consts.cpp:2046-2047) keeps the old WIS, SEX and CLASS labels at 4, 6 and 7,
    while structs.h:790-793 numbers APPLY_WILL, APPLY_LEA and APPLY_PROF there, and APPLY_LEA
    changes learning (affect_modify, handler.cpp:303). Only "not SEX" is asserted: the new label
    is the owner's call. The control (the girdle's flags and +3) is in test_help_and_stat.py.
    """
    implementor = server.login(IMPLEMENTOR.name)
    server.login(PLAYER.name)

    created = implementor.command(f"load obj {GIRDLE_VNUM}")
    assert created.contains("You create a stone girdle."), created.text
    affect_line = OBJECT_AFFECT_LINE_PATTERN.search(read_object_stat(implementor, "girdle"))
    assert affect_line is not None
    assert STALE_LEARNING_LABEL not in affect_line.group(1).split(), affect_line.group(0)


@pytest.mark.xfail(strict=True, raises=AssertionError, reason="stat player should show the statted player's own absorb, the figure its info shows")
@pytest.mark.characters(IMPLEMENTOR, PLAYER)
def test_stat_shows_the_statted_characters_absorb(server) -> None:
    """Bug: stat player shows the viewer's absorb, so an unarmoured immortal sees 0 for an armoured player.

    do_stat_character (act_wiz.cpp:881) passes ch, the immortal reading stat, to
    get_percent_absorb, where every other figure on the line reads k, the character statted.
    The control (info shows the armoured player's non-zero absorb) is in test_help_and_stat.py.
    """
    implementor = server.login(IMPLEMENTOR.name)
    player = server.login(PLAYER.name)
    join_player(implementor)
    created = implementor.command(f"load obj {CHAIN_SHIRT_VNUM}")
    assert created.contains("You create a chain shirt."), created.text
    given = implementor.command(f"give shirt {PLAYER.name.lower()}")
    assert given.contains("You give a chain shirt to"), given.text
    worn = player.command("wear shirt")
    assert worn.contains("You wear a chain shirt on your body."), worn.text
    info = player.command("info")
    player_absorb = INFO_ABSORB_PATTERN.search(info.text)
    assert player_absorb is not None, info.text

    stat_absorb = STAT_ABSORB_PATTERN.search(read_stat(implementor, PLAYER.name))
    assert stat_absorb is not None
    assert int(stat_absorb.group(1)) == int(player_absorb.group(1))
