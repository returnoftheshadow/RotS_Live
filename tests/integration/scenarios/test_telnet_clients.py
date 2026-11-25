"""Telnet clients: each harness client profile negotiating with the real server.

Each profile is answered as the server's protocol code says it should be: the options offered,
the terminal type cycle, the character set, MSDP, MSSP, the MXP acknowledgement and the marker
that ends each prompt.
"""

from __future__ import annotations

import dataclasses
from collections.abc import Iterable

import pytest

from rots_harness import world
from rots_harness.characters import custom_mage
from rots_harness.game_text import TEXT_EDITOR_PROMPT
from rots_harness.launcher import LOGIN_EMAIL_PROMPT
from rots_harness.replies import reply_to
from rots_harness.session import CSI_PATTERN, GameSession
from rots_harness.telnet import (
    DO,
    DONT,
    OPTION_ATCP,
    OPTION_CHARSET,
    OPTION_ECHO,
    OPTION_MSDP,
    OPTION_MSP,
    OPTION_MSSP,
    OPTION_MXP,
    OPTION_NAWS,
    OPTION_TTYPE,
    WILL,
    WONT,
    TelnetCommand,
    TelnetEvent,
    TelnetNegotiation,
    TelnetSubnegotiation,
    TelnetText,
    describe_event,
)
from rots_harness.telnet_payloads import (
    CHARSET_REJECTED,
    MSDP_VAL,
    MSDP_VAR,
    TTYPE_SEND,
    decode_charset_request,
    decode_msdp,
    decode_mssp,
)
from rots_harness.telnet_profile import (
    FULL_CLIENT,
    MSDP_CLIENT,
    REFUSING_CLIENT,
    TelnetClientProfile,
)
from rots_harness.telnet_record import TelnetDirection

pytestmark = [pytest.mark.scenario, pytest.mark.telnet]

# A mage with a custom profession split, logging in to the arena.
MAGE = custom_mage(level=30, load_room=world.ROOM_ARENA_CENTRE)

# What the server sends a new connection before the email prompt: a request for the terminal
# type.
CONNECT_OFFER = TelnetNegotiation(DO, OPTION_TTYPE)

# The server's first eight offers, in order: DO TTYPE at connect, then the seven it makes once the
# client answers DO TTYPE either way.
OFFERS_IN_ORDER = [
    TelnetNegotiation(DO, OPTION_TTYPE),
    TelnetNegotiation(DO, OPTION_NAWS),
    TelnetNegotiation(DO, OPTION_CHARSET),
    TelnetNegotiation(WILL, OPTION_MSDP),
    TelnetNegotiation(WILL, OPTION_MSSP),
    TelnetNegotiation(DO, OPTION_ATCP),
    TelnetNegotiation(WILL, OPTION_MSP),
    TelnetNegotiation(DO, OPTION_MXP),
]

# The commands that offer or request an option; the server's offers use only these.
OFFER_COMMANDS = (WILL, DO)

# The commands that refuse an option.
REFUSAL_COMMANDS = (WONT, DONT)

# SB TTYPE SEND: the server asking for the next terminal type.
TTYPE_SEND_PAYLOAD = bytes([TTYPE_SEND])

# How many times the server asks MSDP_CLIENT for a terminal type: it stops once a reply repeats
# the one before.
MSDP_CLIENT_TTYPE_REQUESTS = 4

# MSDP_CLIENT's TTYPE IS replies, one per request; the last name repeats.
MSDP_CLIENT_TTYPE_REPLIES = [
    b"\x00ROTS-HARNESS",
    b"\x00XTERM-256COLOR",
    b"\x00MTTS 9",
    b"\x00MTTS 9",
]

# MSDP_CLIENT's CHARSET reply: ACCEPTED, then the one character set the server offers.
MSDP_CLIENT_CHARSET_REPLIES = [b"\x02ISO-8859-1"]

# The variable an MSDP LIST COMMANDS reply sets.
MSDP_COMMANDS_VARIABLE = "COMMANDS"

# The start of the MSDP frame that sets COMMANDS; selecting by it leaves other frames undecoded.
COMMANDS_FRAME_START = (
    bytes([MSDP_VAR]) + MSDP_COMMANDS_VARIABLE.encode("ascii") + bytes([MSDP_VAL])
)

# The server's answer to MSDP LIST COMMANDS.
MSDP_COMMANDS_REPLY = {MSDP_COMMANDS_VARIABLE: ["LIST", "REPORT", "RESET", "SEND", "UNREPORT"]}

# The command that turns the msdp preference on; the server sends in-game MSDP frames only with it.
MSDP_ON_COMMAND = "set msdp on"

# The game's reply when the msdp preference turns on.
MSDP_ON_REPLY = "Msdp mode is now on."

# The variable carrying the vnum of the character's room.
ROOM_VNUM_VARIABLE = "ROOM_VNUM"

# The start of the MSDP frame that sets ROOM_VNUM; the server sends one variable per frame.
ROOM_VNUM_FRAME_START = (
    bytes([MSDP_VAR]) + ROOM_VNUM_VARIABLE.encode("ascii") + bytes([MSDP_VAL])
)

# The MSSP status the server sends once a client accepts MSSP.
SERVER_STATUS = {
    "NAME": ["Return of the Shadow"],
    "PLAYERS": ["0"],
    "UPTIME": ["0"],
    "CRAWL DELAY": ["-1"],
}

# The control sequence the server sends as text after its empty SB MXP, opening a secure MXP
# channel.
MXP_SECURE_CHANNEL = "\x1b[7z"

# The telnet option for End of Record, which lets a server mark the end of a prompt.
OPTION_EOR = 25

# The telnet option for Suppress Go-Ahead; a client sends DO SGA to stop GA after prompts.
OPTION_SGA = 3

# The go-ahead command byte, which follows IAC to end a prompt for a client without EOR.
COMMAND_GA = 249

# The end-of-record command byte, which follows IAC to end a prompt for a client that agreed to
# EOR.
COMMAND_EOR = 239

# The options TINTIN_CLIENT accepts when the server offers them. TinTin++ answers the server's
# WILL EOR with DO EOR without a script.
TINTIN_CLIENT_OPTIONS = frozenset(
    (
        OPTION_TTYPE,
        OPTION_ECHO,
        OPTION_NAWS,
        OPTION_CHARSET,
        OPTION_MSDP,
        OPTION_MSSP,
        OPTION_EOR,
    )
)

# TinTin++ 2.02.61 running a script that handles MSDP, as client mappers do; without one it
# refuses MSDP and leaves MSSP unanswered. Its window size stands in for the real terminal's.
TINTIN_CLIENT = TelnetClientProfile(
    name="tintin",
    answers_negotiation=True,
    accepted_options=TINTIN_CLIENT_OPTIONS,
    terminal_types=("TINTIN++", "xterm-256color", "MTTS 2831"),
    window_size=(120, 40),
    charsets=("UTF-8",),
)

# How many times the server asks TINTIN_CLIENT for a terminal type: once for each of its three
# names, and once more for the repeat.
TINTIN_CLIENT_TTYPE_REQUESTS = 4

# The client variables MSDP SEND reports for TINTIN_CLIENT's terminal types. The first terminal
# type becomes CLIENT_ID, and MTTS 2831 holds the ANSI (1), UTF-8 (4) and 256-colour (8) bits.
# CLIENT_VERSION keeps its default: the MTTS number goes only to the variable's number, and the
# variable is sent as text.
TINTIN_CLIENT_VARIABLES = {
    "CLIENT_ID": "TINTIN++",
    "CLIENT_VERSION": "Unknown",
    "ANSI_COLORS": "1",
    "XTERM_256_COLORS": "1",
    "UTF_8": "1",
}

# The character sets the server's CHARSET request offers.
SERVER_CHARSETS = ["ISO-8859-1"]

# TINTIN_CLIENT's CHARSET reply: REJECTED, since the server offers no UTF-8.
TINTIN_CLIENT_CHARSET_REPLIES = [bytes([CHARSET_REJECTED])]

# The commands that mark the end of a prompt.
PROMPT_MARKER_COMMANDS = (COMMAND_GA, COMMAND_EOR)

# A protocol client that refuses EOR and, once it asks for suppress-go-ahead, agrees when the
# server answers WILL SGA rather than refusing it.
GO_AHEAD_SUPPRESSING_CLIENT = dataclasses.replace(
    MSDP_CLIENT,
    name="go-ahead-suppressing",
    accepted_options=MSDP_CLIENT.accepted_options | {OPTION_SGA},
)

# The command that opens the text editor on the character's own description.
TEXT_EDITOR_OPEN_COMMAND = "set description"

# The text editor's command that abandons the text and leaves the editor (string_add, modify.cpp).
TEXT_EDITOR_ABORT_COMMAND = "%q"

# The last characters of a prompt: the game prompt's ">", the shaping prompt's "]" and the text
# editor's "] " once its space is stripped.
PROMPT_ENDINGS = (">", "]")

# A command whose reply ends at the in-game prompt.
LOOK_COMMAND = "look"

# The variable an MSDP LIST REPORTABLE_VARIABLES reply sets.
MSDP_REPORTABLE_VARIABLE = "REPORTABLE_VARIABLES"

# The start of the MSDP frame that sets REPORTABLE_VARIABLES.
REPORTABLE_FRAME_START = (
    bytes([MSDP_VAR]) + MSDP_REPORTABLE_VARIABLE.encode("ascii") + bytes([MSDP_VAL])
)

# The leading names of the server's reportable-variables list.
LEADING_REPORTABLE_VARIABLES = ["", "CHARACTER_NAME"]


def events_in_direction(game_session: GameSession, direction: TelnetDirection) -> list[TelnetEvent]:
    """Return the session's telnet events in one direction, text included, oldest first."""
    return [
        recorded.event for recorded in game_session.telnet_events if recorded.direction is direction
    ]


def non_text_received_before_email_prompt(game_session: GameSession) -> list[TelnetEvent]:
    """Return the received events other than text that came before the email prompt was complete.

    The prompt is found in the text accumulated across text events, since a read can split it.
    Fails the test when the prompt never arrived.
    """
    prompt = LOGIN_EMAIL_PROMPT.encode("latin-1")
    received_text = bytearray()
    non_text: list[TelnetEvent] = []
    received = events_in_direction(game_session, TelnetDirection.RECEIVED)
    for event in received:
        if isinstance(event, TelnetText):
            received_text.extend(event.data)
            if prompt in received_text:
                return non_text
        else:
            non_text.append(event)

    pytest.fail(
        f"{game_session.character_name}: no {LOGIN_EMAIL_PROMPT!r} among the received text events"
    )


def subnegotiation_payloads(events: Iterable[TelnetEvent], option: int) -> list[bytes]:
    """Return the payloads of the subnegotiations for option, in order."""
    return [
        event.payload
        for event in events
        if isinstance(event, TelnetSubnegotiation) and event.option == option
    ]


def wire_listing(game_session: GameSession) -> str:
    """Return every telnet event other than text, one line each with its direction.

    The listing is an assertion message.
    """
    return "\n".join(
        f"{recorded.direction.value:<8} {describe_event(recorded.event)}"
        for recorded in game_session.telnet_events
        if not isinstance(recorded.event, TelnetText)
    )


def sets_msdp_commands(payload: bytes) -> bool:
    """Return whether an MSDP frame sets the COMMANDS variable."""
    return payload.startswith(COMMANDS_FRAME_START)


def sets_room_vnum(payload: bytes) -> bool:
    """Return whether an MSDP frame sets the ROOM_VNUM variable."""
    return payload.startswith(ROOM_VNUM_FRAME_START)


def sets_reportable_variables(payload: bytes) -> bool:
    """Return whether an MSDP frame sets the REPORTABLE_VARIABLES variable."""
    return payload.startswith(REPORTABLE_FRAME_START)


def frame_start(variable: str) -> bytes:
    """Return the bytes an MSDP frame setting variable starts with."""
    return bytes([MSDP_VAR]) + variable.encode("ascii") + bytes([MSDP_VAL])


def wait_for_ttype_cycle(game_session: GameSession, requests: int) -> None:
    """Wait until the server has asked for a terminal type requests times.

    Fails the test when a request does not arrive.
    """
    for _ in range(requests):
        game_session.expect_subnegotiation(OPTION_TTYPE, matching=is_ttype_send)


def received_prompt_markers(game_session: GameSession) -> list[tuple[str, int]]:
    """Return each received GA or EOR with the text received just before it, oldest first.

    The text has its colour codes and trailing spaces removed. A marker whose previous event is
    not text pairs with "".
    """
    markers: list[tuple[str, int]] = []
    previous_text = ""
    received = events_in_direction(game_session, TelnetDirection.RECEIVED)
    for event in received:
        if isinstance(event, TelnetText):
            previous_text = event.data.decode("latin-1")
        elif isinstance(event, TelnetCommand) and event.command in PROMPT_MARKER_COMMANDS:
            colourless_text = CSI_PATTERN.sub("", previous_text)
            cleaned = colourless_text.rstrip()
            markers.append((cleaned, event.command))
            previous_text = ""
        else:
            previous_text = ""
    return markers


def follows_a_prompt(marker: tuple[str, int]) -> bool:
    """Return whether a marker from received_prompt_markers comes right after a prompt's end."""
    text, _ = marker
    return text.endswith(PROMPT_ENDINGS)


def log_in_and_look(game_session: GameSession) -> None:
    """Log in, send look, read its reply up to the prompt, and read whatever follows the prompt."""
    game_session.login()
    reply = game_session.command(LOOK_COMMAND)
    assert reply.text, wire_listing(game_session)

    # A marker can arrive after the prompt text, in a later read.
    game_session.drain()


def is_ttype_send(payload: bytes) -> bool:
    """Return whether a TTYPE subnegotiation asks for the next terminal type."""
    return payload == TTYPE_SEND_PAYLOAD


def is_empty(payload: bytes) -> bool:
    """Return whether a subnegotiation carries no payload."""
    return payload == b""


@pytest.mark.characters(MAGE)
def test_a_silent_client_is_offered_only_ttype(server) -> None:
    """Pins the server offering a client that never answers only DO TTYPE before the email prompt.

    The client answers no negotiation, so the server's other offers, which follow an answer to
    DO TTYPE, never come, and everything the client sends through login is text.
    """
    mage = server.connect(MAGE.name)
    mage.login()
    # The password prompt's WILL ECHO comes after the email prompt and is not an offer.
    before_prompt = non_text_received_before_email_prompt(mage)
    assert before_prompt == [CONNECT_OFFER], wire_listing(mage)

    sent = events_in_direction(mage, TelnetDirection.SENT)
    assert all(isinstance(event, TelnetText) for event in sent), wire_listing(mage)


@pytest.mark.characters(MAGE)
def test_a_refusing_client_is_offered_every_other_option_and_refuses_each(server) -> None:
    """Pins the server's eight offers, in order, to a client that refuses every option.

    Once the client answers DO TTYPE, even with a refusal, the server offers NAWS, CHARSET, MSDP,
    MSSP, ATCP, MSP and MXP; every negotiation the client sends is a WONT or a DONT.
    """
    mage = server.connect(MAGE.name, client=REFUSING_CLIENT)
    mage.expect_negotiation(DO, OPTION_MXP)
    mage.login()
    received = events_in_direction(mage, TelnetDirection.RECEIVED)
    offers = [
        event
        for event in received
        if isinstance(event, TelnetNegotiation) and event.command in OFFER_COMMANDS
    ]
    # Later offers are the server's WILL MXP after our WONT MXP and its DO TTYPE at game entry.
    assert offers[: len(OFFERS_IN_ORDER)] == OFFERS_IN_ORDER, wire_listing(mage)

    sent = events_in_direction(mage, TelnetDirection.SENT)
    sent_negotiations = [event for event in sent if isinstance(event, TelnetNegotiation)]
    assert sent_negotiations, wire_listing(mage)
    assert all(event.command in REFUSAL_COMMANDS for event in sent_negotiations), wire_listing(mage)


@pytest.mark.characters(MAGE)
def test_an_msdp_client_completes_the_ttype_cycle_and_accepts_iso_8859_1(server) -> None:
    """Pins the server's TTYPE cycle and CHARSET offer with a client that accepts both.

    The server asks for a terminal type four times, stopping once a reply repeats the one before,
    and the client accepts ISO-8859-1, the one character set the server offers.
    """
    mage = server.connect(MAGE.name, client=MSDP_CLIENT)
    mage.expect_subnegotiation(OPTION_CHARSET)
    # Wait for every request: the entry-token login can finish before the cycle does.
    for _ in range(MSDP_CLIENT_TTYPE_REQUESTS):
        mage.expect_subnegotiation(OPTION_TTYPE, matching=is_ttype_send)

    mage.login()
    received = events_in_direction(mage, TelnetDirection.RECEIVED)
    sent = events_in_direction(mage, TelnetDirection.SENT)
    received_ttype_payloads = subnegotiation_payloads(received, OPTION_TTYPE)
    ttype_requests = [
        payload for payload in received_ttype_payloads if payload == TTYPE_SEND_PAYLOAD
    ]
    assert len(ttype_requests) == MSDP_CLIENT_TTYPE_REQUESTS, wire_listing(mage)

    sent_ttype_payloads = subnegotiation_payloads(sent, OPTION_TTYPE)
    assert sent_ttype_payloads == MSDP_CLIENT_TTYPE_REPLIES, wire_listing(mage)

    sent_charset_payloads = subnegotiation_payloads(sent, OPTION_CHARSET)
    assert sent_charset_payloads == MSDP_CLIENT_CHARSET_REPLIES, wire_listing(mage)


@pytest.mark.characters(MAGE)
def test_an_msdp_client_lists_commands_at_the_login_prompt(server) -> None:
    """Pins the server's answer to MSDP LIST COMMANDS sent before the client logs in."""
    mage = server.connect(MAGE.name, client=MSDP_CLIENT)
    mage.expect_subnegotiation(OPTION_CHARSET)
    mage.send_msdp("LIST", "COMMANDS")
    reply = mage.expect_subnegotiation(OPTION_MSDP, matching=sets_msdp_commands)
    variables = decode_msdp(reply.event.payload)
    assert variables == MSDP_COMMANDS_REPLY, wire_listing(mage)


@pytest.mark.characters(MAGE)
# Only the account-menu login reaches the password prompt that sends WILL ECHO.
@pytest.mark.real_login
def test_an_msdp_client_hides_the_password_and_gets_its_room_vnum_in_game(server) -> None:
    """Pins WILL ECHO at the password prompt and, with msdp on, an MSDP frame of the room's vnum."""
    mage = server.login(MAGE.name, client=MSDP_CLIENT)
    # The password prompt's WILL ECHO; the wait fails the test when it never arrived.
    mage.expect_negotiation(WILL, OPTION_ECHO)
    reply = mage.command(MSDP_ON_COMMAND)
    assert MSDP_ON_REPLY in reply.text, reply.text

    room_frame = mage.expect_subnegotiation(OPTION_MSDP, matching=sets_room_vnum)
    room_variables = decode_msdp(room_frame.event.payload)
    room_vnum = room_variables[ROOM_VNUM_VARIABLE]
    assert room_vnum == str(world.ROOM_ARENA_CENTRE), wire_listing(mage)


@pytest.mark.characters(MAGE)
def test_a_full_client_receives_mssp_and_the_mxp_acknowledgement(server) -> None:
    """Pins the MSSP status and the MXP acknowledgement a client accepting every option receives.

    The acknowledgement is an empty SB MXP followed, as text, by the sequence opening a secure MXP
    channel.
    """
    mage = server.connect(MAGE.name, client=FULL_CLIENT)
    status = mage.expect_subnegotiation(OPTION_MSSP)
    fields = decode_mssp(status.event.payload)
    assert fields == SERVER_STATUS, wire_listing(mage)

    # The empty SB MXP that acknowledges WILL MXP; the wait fails the test when it never arrived.
    mage.expect_subnegotiation(OPTION_MXP, matching=is_empty)
    mage.login()
    assert MXP_SECURE_CHANNEL in mage.everything_raw, wire_listing(mage)


@pytest.mark.characters(MAGE)
def test_a_tintin_client_is_identified_from_its_terminal_types(server) -> None:
    """Legacy pin: the client variables the server takes from TinTin++'s terminal types.

    CLIENT_VERSION stays Unknown. The server answers MSDP SEND only for a character in game with
    msdp on. This becomes a strict xfail when the server reads more of MTTS.
    """
    mage = server.connect(MAGE.name, client=TINTIN_CLIENT)
    wait_for_ttype_cycle(mage, TINTIN_CLIENT_TTYPE_REQUESTS)
    mage.login()
    reply = mage.command(MSDP_ON_COMMAND)
    assert MSDP_ON_REPLY in reply.text, reply.text

    reported: dict[str, object] = {}
    for variable in TINTIN_CLIENT_VARIABLES:
        mage.send_msdp("SEND", variable)
        start = frame_start(variable)
        frame = mage.expect_subnegotiation(
            OPTION_MSDP, matching=lambda payload, start=start: payload.startswith(start)
        )
        decoded = decode_msdp(frame.event.payload)
        reported.update(decoded)

    assert reported == TINTIN_CLIENT_VARIABLES, wire_listing(mage)


@pytest.mark.characters(MAGE)
def test_a_tintin_client_rejects_the_servers_only_character_set(server) -> None:
    """Legacy pin: the server offers only ISO-8859-1, and TinTin++ rejects it.

    This becomes a strict xfail when the server offers UTF-8.
    """
    mage = server.connect(MAGE.name, client=TINTIN_CLIENT)
    request = mage.expect_subnegotiation(OPTION_CHARSET)
    charsets = decode_charset_request(request.event.payload)
    assert charsets == SERVER_CHARSETS, wire_listing(mage)

    mage.login()
    sent = events_in_direction(mage, TelnetDirection.SENT)
    sent_charset_payloads = subnegotiation_payloads(sent, OPTION_CHARSET)
    assert sent_charset_payloads == TINTIN_CLIENT_CHARSET_REPLIES, wire_listing(mage)


@pytest.mark.characters(MAGE)
@pytest.mark.xfail(
    strict=True,
    raises=AssertionError,
    reason="the server offers EOR and ends each prompt with EOR or GA",
)
def test_a_tintin_client_gets_no_end_of_prompt_marker(server) -> None:
    """Legacy pin: the server neither offers EOR nor ends any prompt with GA or EOR.

    Checked through login and the prompt after an in-game command. This becomes a strict xfail
    when the server marks its prompts.
    """
    mage = server.connect(MAGE.name, client=TINTIN_CLIENT)
    mage.login()
    reply = mage.command(LOOK_COMMAND)
    assert reply.text, wire_listing(mage)

    received = events_in_direction(mage, TelnetDirection.RECEIVED)
    eor_offers = [
        event
        for event in received
        if isinstance(event, TelnetNegotiation) and event.option == OPTION_EOR
    ]
    assert not eor_offers, wire_listing(mage)

    prompt_markers = [
        event
        for event in received
        if isinstance(event, TelnetCommand) and event.command in PROMPT_MARKER_COMMANDS
    ]
    assert not prompt_markers, wire_listing(mage)


@pytest.mark.characters(MAGE)
def test_a_tintin_clients_reportable_variables_list_starts_with_an_empty_name(server) -> None:
    """Legacy pin: MSDP LIST REPORTABLE_VARIABLES answers an array whose first name is empty.

    This becomes a strict xfail when the list holds only real names.
    """
    mage = server.connect(MAGE.name, client=TINTIN_CLIENT)
    mage.expect_subnegotiation(OPTION_CHARSET)
    mage.send_msdp("LIST", MSDP_REPORTABLE_VARIABLE)
    reply = mage.expect_subnegotiation(OPTION_MSDP, matching=sets_reportable_variables)
    variables = decode_msdp(reply.event.payload)
    names = variables[MSDP_REPORTABLE_VARIABLE]
    leading_names = names[: len(LEADING_REPORTABLE_VARIABLES)]
    assert leading_names == LEADING_REPORTABLE_VARIABLES, wire_listing(mage)


@pytest.mark.characters(MAGE)
def test_a_client_that_agrees_to_eor_gets_eor_after_each_prompt(server) -> None:
    """Pins IAC EOR, offered and agreed, after every prompt a TinTin++ client receives."""
    mage = server.connect(MAGE.name, client=TINTIN_CLIENT)
    mage.expect_negotiation(WILL, OPTION_EOR)
    log_in_and_look(mage)
    markers = received_prompt_markers(mage)
    assert markers, wire_listing(mage)
    assert all(command == COMMAND_EOR for _, command in markers), wire_listing(mage)
    assert all(follows_a_prompt(marker) for marker in markers), wire_listing(mage)


@pytest.mark.characters(MAGE)
def test_a_client_that_refuses_eor_gets_ga_after_each_prompt(server) -> None:
    """Pins IAC GA after every prompt a client that refused EOR receives."""
    mage = server.connect(MAGE.name, client=MSDP_CLIENT)
    log_in_and_look(mage)
    markers = received_prompt_markers(mage)
    assert markers, wire_listing(mage)
    assert all(command == COMMAND_GA for _, command in markers), wire_listing(mage)
    assert all(follows_a_prompt(marker) for marker in markers), wire_listing(mage)


@pytest.mark.characters(MAGE)
def test_a_silent_client_gets_ga_after_each_prompt(server) -> None:
    """Pins IAC GA after every prompt a client that never answers negotiation receives."""
    mage = server.connect(MAGE.name)
    log_in_and_look(mage)
    markers = received_prompt_markers(mage)
    assert markers, wire_listing(mage)
    assert all(command == COMMAND_GA for _, command in markers), wire_listing(mage)
    assert all(follows_a_prompt(marker) for marker in markers), wire_listing(mage)


@pytest.mark.characters(MAGE)
def test_a_client_that_suppresses_go_ahead_gets_no_prompt_marker(server) -> None:
    """Pins WILL SGA in answer to DO SGA, and no GA or EOR after any prompt from then on."""
    mage = server.connect(MAGE.name, client=GO_AHEAD_SUPPRESSING_CLIENT)
    mage.send_negotiation(DO, OPTION_SGA)
    mage.expect_negotiation(WILL, OPTION_SGA)
    log_in_and_look(mage)
    assert not received_prompt_markers(mage), wire_listing(mage)


@pytest.mark.characters(MAGE)
def test_a_client_that_turns_eor_off_gets_ga_again(server) -> None:
    """Pins WONT EOR in answer to DONT EOR, and IAC GA after the prompts that follow."""
    mage = server.connect(MAGE.name, client=TINTIN_CLIENT)
    log_in_and_look(mage)
    mage.send_negotiation(DONT, OPTION_EOR)
    mage.expect_negotiation(WONT, OPTION_EOR)
    reply = mage.command(LOOK_COMMAND)
    assert reply.text, wire_listing(mage)

    mage.drain()
    markers = received_prompt_markers(mage)
    assert markers, wire_listing(mage)
    assert markers[-1][1] == COMMAND_GA, wire_listing(mage)
    assert follows_a_prompt(markers[-1]), wire_listing(mage)


@pytest.mark.characters(MAGE)
def test_a_client_that_stops_suppressing_go_ahead_gets_ga_again(server) -> None:
    """Pins WONT SGA in answer to DONT SGA, and IAC GA after the prompts that follow."""
    mage = server.connect(MAGE.name, client=GO_AHEAD_SUPPRESSING_CLIENT)
    mage.send_negotiation(DO, OPTION_SGA)
    mage.expect_negotiation(WILL, OPTION_SGA)
    log_in_and_look(mage)
    mage.send_negotiation(DONT, OPTION_SGA)
    mage.expect_negotiation(WONT, OPTION_SGA)
    reply = mage.command(LOOK_COMMAND)
    assert reply.text, wire_listing(mage)

    mage.drain()
    markers = received_prompt_markers(mage)
    assert markers, wire_listing(mage)
    assert markers[-1][1] == COMMAND_GA, wire_listing(mage)
    assert follows_a_prompt(markers[-1]), wire_listing(mage)


@pytest.mark.characters(MAGE)
def test_the_text_editor_prompt_is_followed_by_ga(server) -> None:
    """Pins IAC GA after the text editor's prompt, which `set description` opens."""
    mage = server.connect(MAGE.name, client=MSDP_CLIENT)
    mage.login()
    reply_to(mage, TEXT_EDITOR_OPEN_COMMAND, [TEXT_EDITOR_PROMPT])
    editor_prompt = TEXT_EDITOR_PROMPT.rstrip()
    markers = received_prompt_markers(mage)
    editor_markers = [marker for marker in markers if marker[0].endswith(editor_prompt)]
    # Leave the editor first, so the test ends at the game prompt whatever the assertions find.
    mage.command(TEXT_EDITOR_ABORT_COMMAND)
    assert editor_markers, wire_listing(mage)
    assert all(command == COMMAND_GA for _, command in editor_markers), wire_listing(mage)
