from __future__ import annotations

from rots_harness.groups import registered_marker_names, unknown_marker_names

INI_LINES = [
    "scenario: boots its own server through the server fixture",
    "boot: the server boots on a harness lib and characters log in",
    "characters(*characters): the characters the test's server is built with",
]


def test_registered_names_are_read_from_the_ini_lines() -> None:
    assert registered_marker_names(INI_LINES) == ["scenario", "boot", "characters"]


def test_a_known_expression_has_no_unknown_names() -> None:
    assert unknown_marker_names("scenario and boot", ["scenario", "boot"]) == []


def test_a_typo_is_reported() -> None:
    assert unknown_marker_names("scenario and bot", ["scenario", "boot"]) == ["bot"]


def test_operators_and_parentheses_are_not_names() -> None:
    assert unknown_marker_names("(scenario or boot) and not boot", ["scenario", "boot"]) == []


def test_an_empty_expression_has_no_names() -> None:
    assert unknown_marker_names("", ["scenario"]) == []
