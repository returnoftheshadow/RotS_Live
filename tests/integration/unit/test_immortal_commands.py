"""Unit tests for rots_harness.immortal_commands, against a fake session answering with canned replies."""

from __future__ import annotations

from pathlib import Path

import pytest

from rots_harness.immortal_commands import (
    CREATED_FORMAT,
    GIVEN_FORMAT,
    HOLYLIGHT_SET_FORMAT,
    SYSLOG_SET_FORMAT,
    WIZSET_FORMAT,
    give_new_object,
    goto,
    load_mob,
    load_object,
    set_holylight,
    set_syslog,
    where_lines,
    wizset,
)
from rots_harness.session import SessionTimeout
from unit.fakes import RecordingSession

# The game's source folder, whose format strings the reply formats come from.
SOURCE_DIR = Path(__file__).resolve().parents[3] / "src"

# A goto reply as the game sends it: the room's name and exits, its description and a prompt.
ARENA_CENTRE_REPLY = "\n\rArena Centre    Exits are: N E S W\n\r   The arena's centre.\n\rS:Surging> "
# A load obj reply for the leather cap.
CAP_CREATED_REPLY = "\n\rYou create a leather cap.\n\rS:Surging> "
# A give reply for the leather cap to Testwearer.
CAP_GIVEN_REPLY = "\n\rYou give a leather cap to Testwearer.\n\rS:Surging> "
# A where reply listing a mob and two objects with vnum 1137, numbered as perform_immort_where
# numbers them, and an unnumbered header.
WHERE_REPLY = "\n\r".join(
    [
        "Objects and mobs:",
        "  1. a leather cap              - [ 1131] Arena Centre",
        "  2. a cap-wearing orc          - [ 1131] Arena Centre",
        "  3. a leather cap              - [ 1132] >> Carried by Testwearer",
        "a leather cap is not on a numbered line",
        "S:Surging> ",
    ]
)


def test_goto_without_a_room_name_sends_goto_and_asserts_nothing() -> None:
    session = RecordingSession()
    reply = goto(session, 1131)
    assert session.commands == ["goto 1131"]
    assert reply.text == ""


def test_goto_with_a_room_name_checks_the_room_name_exactly() -> None:
    session = RecordingSession(command_replies={"goto testwearer": ARENA_CENTRE_REPLY})
    reply = goto(session, "testwearer", "Arena Centre")
    assert reply.text == ARENA_CENTRE_REPLY


def test_goto_fails_naming_the_room_when_the_room_name_differs() -> None:
    session = RecordingSession(command_replies={"goto 1131": ARENA_CENTRE_REPLY})
    with pytest.raises(AssertionError, match="goto 1131 did not reach 'Arena'"):
        goto(session, 1131, "Arena")


def test_goto_not_exact_accepts_the_name_anywhere_in_the_reply() -> None:
    session = RecordingSession(command_replies={"goto 1131": ARENA_CENTRE_REPLY})
    goto(session, 1131, "centre.", exact=False)
    with pytest.raises(AssertionError, match="goto 1131 shows no 'Arena West'"):
        goto(session, 1131, "Arena West", exact=False)


def test_load_object_without_a_short_description_checks_only_you_create() -> None:
    session = RecordingSession(command_replies={"load obj 1137": "\n\rYou create something else.\n\r"})
    load_object(session, 1137)
    assert session.commands == ["load obj 1137"]
    with pytest.raises(AssertionError, match="no 'You create' in the reply to load obj 1138"):
        load_object(session, 1138)


def test_load_object_with_a_short_description_checks_the_whole_reply() -> None:
    session = RecordingSession(command_replies={"load obj 1137": CAP_CREATED_REPLY})
    assert load_object(session, 1137, "a leather cap").text == CAP_CREATED_REPLY
    with pytest.raises(AssertionError, match="no 'You create a leather bag.' in the reply to load obj 1137"):
        load_object(session, 1137, "a leather bag")


def test_load_mob_sends_load_mob_and_checks_as_load_object_does() -> None:
    session = RecordingSession(command_replies={"load mob 1105": "\n\rYou create the harness innkeeper.\n\r"})
    load_mob(session, 1105)
    load_mob(session, 1105, "the harness innkeeper")
    assert session.commands == ["load mob 1105", "load mob 1105"]
    with pytest.raises(AssertionError, match="no 'You create a porter.' in the reply to load mob 1105"):
        load_mob(session, 1105, "a porter")


def test_give_new_object_loads_gives_and_returns_the_give_replys_text() -> None:
    session = RecordingSession(command_replies={"load obj 1137": CAP_CREATED_REPLY, "give cap testwearer": CAP_GIVEN_REPLY})
    given = give_new_object(session, 1137, "cap", "testwearer")
    assert session.commands == ["load obj 1137", "give cap testwearer"]
    assert given == CAP_GIVEN_REPLY


def test_give_new_object_without_options_checks_only_you_create_and_you_give() -> None:
    session = RecordingSession(command_replies={"load obj 1137": "You create x.", "give cap testwearer": "You give x to y."})
    give_new_object(session, 1137, "cap", "testwearer")
    failing = RecordingSession(command_replies={"load obj 1137": "You create x.", "give cap testwearer": "Testwearer refuses."})
    with pytest.raises(AssertionError, match="no 'You give' in the reply to give"):
        give_new_object(failing, 1137, "cap", "testwearer")


def test_give_new_object_fails_on_the_load_before_it_gives() -> None:
    session = RecordingSession(command_replies={"give cap testwearer": CAP_GIVEN_REPLY})
    with pytest.raises(AssertionError, match="no 'You create' in the reply to load obj 1137"):
        give_new_object(session, 1137, "cap", "testwearer")
    assert session.commands == ["load obj 1137"]


def test_give_new_object_with_a_shown_recipient_checks_the_recipient() -> None:
    session = RecordingSession(command_replies={"load obj 1137": "You create x.", "give cap testwearer": "You give x to Testwearer."})
    give_new_object(session, 1137, "cap", "testwearer", shown_recipient="Testwearer")
    with pytest.raises(AssertionError, match="no 'to Testtaker.' in the reply to give"):
        give_new_object(session, 1137, "cap", "testwearer", shown_recipient="Testtaker")


def test_give_new_object_with_a_short_description_checks_both_replies_up_to_the_recipient() -> None:
    session = RecordingSession(command_replies={"load obj 1137": CAP_CREATED_REPLY, "give cap testwearer": CAP_GIVEN_REPLY})
    give_new_object(session, 1137, "cap", "testwearer", short_description="a leather cap")
    wrong_give = RecordingSession(command_replies={"load obj 1137": "You create a leather bag.", "give cap testwearer": "You give a leather cap to Testwearer."})
    with pytest.raises(AssertionError, match="no 'You create a leather bag.'"):
        give_new_object(session, 1137, "cap", "testwearer", short_description="a leather bag")

    with pytest.raises(AssertionError, match="no 'You give a leather bag to' in the reply to give"):
        give_new_object(wrong_give, 1137, "cap", "testwearer", short_description="a leather bag")


def test_give_new_object_with_both_options_checks_the_whole_give_reply() -> None:
    session = RecordingSession(command_replies={"load obj 1137": CAP_CREATED_REPLY, "give cap testwearer": CAP_GIVEN_REPLY})
    give_new_object(session, 1137, "cap", "testwearer", shown_recipient="Testwearer", short_description="a leather cap")
    with pytest.raises(AssertionError, match="no 'You give a leather cap to a porter.' in the reply to give"):
        give_new_object(session, 1137, "cap", "testwearer", shown_recipient="a porter", short_description="a leather cap")


def test_wizset_sends_the_target_field_and_value_and_returns_the_reply() -> None:
    session = RecordingSession(command_replies={"wizset player testpupil lea 40": "Testpupil's lea set to 40.\n\r"})
    assert wizset(session, "player testpupil", "lea", 40) == "Testpupil's lea set to 40.\n\r"
    assert session.commands == ["wizset player testpupil lea 40"]


def test_wizset_with_a_shown_name_checks_the_name_too() -> None:
    session = RecordingSession(command_replies={"wizset player testpupil lea 40": "Testpupil's lea set to 40."})
    wizset(session, "player testpupil", "lea", 40, shown_name="Testpupil")
    with pytest.raises(AssertionError, match="no \"Testother's lea set to 40.\" in the reply to wizset"):
        wizset(session, "player testpupil", "lea", 40, shown_name="Testother")


def test_wizset_without_a_shown_name_fails_on_another_value() -> None:
    session = RecordingSession(command_replies={"wizset mob porter hit 5": "a porter's hit set to 6."})
    with pytest.raises(AssertionError, match="no 'hit set to 5.' in the reply to wizset"):
        wizset(session, "mob porter", "hit", 5)


def test_where_lines_keeps_the_numbered_lines_naming_the_short_description() -> None:
    session = RecordingSession(command_replies={"where 1137": WHERE_REPLY})
    lines = where_lines(session, 1137, "a leather cap")
    assert session.commands == ["where 1137"]
    assert lines == [
        "1. a leather cap              - [ 1131] Arena Centre",
        "3. a leather cap              - [ 1132] >> Carried by Testwearer",
    ]
    assert where_lines(session, 1137, "a leather bag") == []


def test_set_syslog_checks_the_level_it_set() -> None:
    session = RecordingSession(command_replies={"syslog normal": "Your syslog is now normal.\n\r"})
    set_syslog(session, "normal")
    with pytest.raises(AssertionError, match="no 'Your syslog is now off.' in the reply to syslog"):
        set_syslog(session, "off")


def test_set_holylight_checks_the_setting_it_made() -> None:
    session = RecordingSession(command_replies={"set holylight off": "Holylight mode is now off.\n\r"})
    set_holylight(session, "off")
    with pytest.raises(AssertionError, match="no 'Holylight mode is now on.' in the reply to set holylight"):
        set_holylight(session, "on")


def test_a_timeout_passes_through() -> None:
    session = RecordingSession(time_out_commands=True)
    with pytest.raises(SessionTimeout):
        goto(session, 1131, "Arena Centre")

    with pytest.raises(SessionTimeout):
        give_new_object(session, 1137, "cap", "testwearer")


@pytest.mark.parametrize(
    "source_file, format_text, reply_format",
    [
        ("act_wiz.cpp", '"You create $p."', CREATED_FORMAT),
        ("act_wiz.cpp", '"You create $N."', CREATED_FORMAT),
        ("act_obj1.cpp", '"You give $p to $N."', GIVEN_FORMAT),
        ("act_wiz.cpp", '"%s\'s %s set to %d.\\n\\r"', WIZSET_FORMAT),
        ("act_othe.cpp", '"Your syslog is now %s.\\n\\r"', SYSLOG_SET_FORMAT),
        ("act_othe.cpp", '"Holylight mode is now on.\\n\\r"', HOLYLIGHT_SET_FORMAT),
        ("act_othe.cpp", '"Holylight mode is now off.\\n\\r"', HOLYLIGHT_SET_FORMAT),
    ],
)
def test_each_reply_format_matches_the_games_format_string(source_file: str, format_text: str, reply_format: str) -> None:
    source = (SOURCE_DIR / source_file).read_text(encoding="latin-1")
    assert format_text in source, f"no {format_text} in {source_file}"
    fixed_start = reply_format.split("{}")[0]
    assert fixed_start in format_text, f"{reply_format!r} does not start as {format_text}"
