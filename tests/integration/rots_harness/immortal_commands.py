"""Commands an implementor sends to set up or inspect a test; set-up commands check their reply.

Each helper sends its commands through GameSession.command and asserts only what its
docstring names; an optional argument makes a check stronger, so a caller passes only
the arguments for the check it means to make. Failed checks raise AssertionError; transport
errors such as SessionTimeout pass through unchanged. A name ends in _FORMAT for a format
string, filled in with str.format, as in game_text.
"""

from __future__ import annotations

import re

from rots_harness.session import GameSession, Transcript

# What load answers once it has made the mob or object (do_load, act_wiz.cpp); format with its
# short description.
CREATED_FORMAT = "You create {}."
# What give answers once the receiver holds the object (perform_give, act_obj1.cpp); format with
# the object's short description and the receiver's name or short description.
GIVEN_FORMAT = "You give {} to {}."
# The start of CREATED_FORMAT, for a check that does not name what was made.
CREATED_START = "You create"
# The start of GIVEN_FORMAT, for a check that does not name the object.
GIVEN_START = "You give"
# What wizset answers once it has set a number field (do_wizset, act_wiz.cpp), after the
# character's name and "'s "; format with the field as typed and the value.
WIZSET_FORMAT = "{} set to {}."
# What syslog answers as it changes the implementor's log level (do_syslog, act_othe.cpp);
# format with the level, such as "off" or "normal".
SYSLOG_SET_FORMAT = "Your syslog is now {}."
# What `set holylight on` and `set holylight off` answer (tog_messages, act_othe.cpp); format
# with "on" or "off".
HOLYLIGHT_SET_FORMAT = "Holylight mode is now {}."
# The start of a numbered line of an implementor's `where` (perform_immort_where, act_info.cpp).
WHERE_ROW_PATTERN = re.compile(r"^\s*\d+\. ")


def goto(
    implementor: GameSession, destination: int | str, room_name: str | None = None, *, exact: bool = True
) -> Transcript:
    """Send `goto <destination>`, a room vnum or a character's keyword, and return the reply.

    With room_name, asserts the room arrived at: the reply's room_name() equals room_name, or,
    when exact is False, the reply's text contains room_name anywhere. Without it, asserts
    nothing. Any other check on the reply is left to the caller.
    """
    reply = implementor.command(f"goto {destination}")
    if room_name is None:
        return reply

    if exact:
        assert reply.room_name() == room_name, f"goto {destination} did not reach {room_name!r}:\n{reply.text}"
    else:
        assert reply.contains(room_name), f"goto {destination} shows no {room_name!r}:\n{reply.text}"

    return reply


def load_object(implementor: GameSession, vnum: int, short_description: str | None = None) -> Transcript:
    """Send `load obj <vnum>` and return the reply.

    Asserts the reply holds CREATED_FORMAT filled with short_description, or only "You create"
    without it.
    """
    return _load(implementor, "obj", vnum, short_description)


def load_mob(implementor: GameSession, vnum: int, short_description: str | None = None) -> Transcript:
    """Send `load mob <vnum>` and return the reply.

    Asserts the reply holds CREATED_FORMAT filled with short_description, or only "You create"
    without it.
    """
    return _load(implementor, "mob", vnum, short_description)


def give_new_object(
    implementor: GameSession,
    vnum: int,
    keyword: str,
    recipient_keyword: str,
    *,
    shown_recipient: str | None = None,
    short_description: str | None = None,
) -> str:
    """Load object vnum, give it by keyword to recipient_keyword and return the give reply's text.

    The load reply is checked as load_object checks it. The give reply always holds "You give";
    with shown_recipient (the name the reply shows, such as "Testwearer" or "a porter") it also
    holds "to <shown_recipient>."; with short_description it holds "You give <short_description>
    to", and with both, GIVEN_FORMAT in full.
    """
    _load(implementor, "obj", vnum, short_description)
    given = implementor.command(f"give {keyword} {recipient_keyword}")
    if short_description is not None and shown_recipient is not None:
        expected = GIVEN_FORMAT.format(short_description, shown_recipient)
        assert given.contains(expected), f"no {expected!r} in the reply to give:\n{given.text}"
    elif short_description is not None:
        expected = f"{GIVEN_START} {short_description} to"
        assert given.contains(expected), f"no {expected!r} in the reply to give:\n{given.text}"
    else:
        assert given.contains(GIVEN_START), f"no {GIVEN_START!r} in the reply to give:\n{given.text}"
        if shown_recipient is not None:
            expected = f"to {shown_recipient}."
            assert given.contains(expected), f"no {expected!r} in the reply to give:\n{given.text}"

    return given.text


def wizset(
    implementor: GameSession, target: str, field: str, value: int | str, *, shown_name: str | None = None
) -> str:
    """Send `wizset <target> <field> <value>` and return the reply's text.

    target is such as "player testpupil".

    Asserts the reply holds "<shown_name>'s <field> set to <value>.", or only "<field> set to
    <value>." without shown_name. field must be a number field, which wizset answers with
    WIZSET_FORMAT.
    """
    reply = implementor.command(f"wizset {target} {field} {value}")
    expected = WIZSET_FORMAT.format(field, value)
    if shown_name is not None:
        expected = f"{shown_name}'s {expected}"

    assert reply.contains(expected), f"no {expected!r} in the reply to wizset:\n{reply.text}"
    return reply.text


def where_lines(implementor: GameSession, vnum: int, short_description: str) -> list[str]:
    """Return the numbered lines of the implementor's `where <vnum>` naming short_description.

    The lines are stripped.

    `where` by number lists the mobs with that vnum as well as the objects (perform_immort_where,
    act_info.cpp), so only the lines naming short_description count. Asserts nothing; an empty
    list means none.
    """
    reply = implementor.command(f"where {vnum}").text
    lines = reply.splitlines()
    return [line.strip() for line in lines if WHERE_ROW_PATTERN.match(line) and short_description in line]


def set_syslog(implementor: GameSession, level: str) -> Transcript:
    """Send `syslog <level>` and return the reply.

    Assert it holds SYSLOG_SET_FORMAT filled with level.
    """
    reply = implementor.command(f"syslog {level}")
    expected = SYSLOG_SET_FORMAT.format(level)
    assert reply.contains(expected), f"no {expected!r} in the reply to syslog:\n{reply.text}"
    return reply


def set_holylight(implementor: GameSession, setting: str) -> Transcript:
    """Send `set holylight <setting>`, "on" or "off", and return the reply.

    Assert it holds HOLYLIGHT_SET_FORMAT filled with setting.
    """
    reply = implementor.command(f"set holylight {setting}")
    expected = HOLYLIGHT_SET_FORMAT.format(setting)
    assert reply.contains(expected), f"no {expected!r} in the reply to set holylight:\n{reply.text}"
    return reply


def _load(implementor: GameSession, kind: str, vnum: int, short_description: str | None) -> Transcript:
    """Send `load <kind> <vnum>` and return the reply, checked as load_object documents."""
    created = implementor.command(f"load {kind} {vnum}")
    if short_description is None:
        assert created.contains(
            CREATED_START
        ), f"no {CREATED_START!r} in the reply to load {kind} {vnum}:\n{created.text}"
    else:
        expected = CREATED_FORMAT.format(short_description)
        assert created.contains(expected), f"no {expected!r} in the reply to load {kind} {vnum}:\n{created.text}"

    return created
