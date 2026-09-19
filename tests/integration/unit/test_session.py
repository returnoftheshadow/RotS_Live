from __future__ import annotations

from rots_harness import session
from rots_harness.session import Transcript, ends_with_prompt


def test_transcript_parses_the_stat_hit_point_line() -> None:
    transcript = Transcript("Str:[10/10(10)]\nHP :[15/60+3(0)]  Stamina :[0/40+1(0)]  Move :[120/120+2(0)] Spirit:[100/100+0]\n")
    assert transcript.hit_points() == (15, 60)


def test_transcript_reports_no_hit_points_when_the_line_is_absent() -> None:
    assert Transcript("nothing here").hit_points() is None


def test_transcript_room_name_is_the_first_non_empty_line() -> None:
    assert Transcript("\nWood-elf Start\n   A glade where wood elves begin.\n") .room_name() == "Wood-elf Start"


def test_transcript_room_name_drops_the_exits_suffix() -> None:
    transcript = Transcript("\nArena Centre    Exits are: N E S W\nThe centre...\n")
    assert transcript.room_name() == "Arena Centre"


def test_ansi_pattern_strips_colour_codes() -> None:
    assert session.ANSI_PATTERN.sub("", "\x1b[33mArena Centre\x1b[0m") == "Arena Centre"


def test_prompt_detection_accepts_trailing_whitespace_and_both_terminators() -> None:
    assert ends_with_prompt("You breathe out fire.\nHP:Healthy > ")
    assert ends_with_prompt("some output\n]")
    assert not ends_with_prompt("You start to cast a spell...")


def test_character_number_prompt_matches_both_server_wordings() -> None:
    assert session.CHARACTER_NUMBER_PROMPT in "\n\rCharacter number: "
    assert session.CHARACTER_NUMBER_PROMPT in "\n\rCharacter number or name: "
