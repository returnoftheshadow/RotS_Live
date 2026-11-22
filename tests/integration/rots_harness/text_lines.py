"""Reads lines, numbers and their order out of text the game sent."""

from __future__ import annotations

import re
from collections.abc import Sequence

from rots_harness.session import Transcript


def raw_lines(text: str) -> list[str]:
    """Return text's lines split at each newline, each without the carriage return sent with it.

    Blank lines are kept, and other whitespace is left alone, so a caller can check spacing.
    """
    split_lines = text.split("\n")
    return [line.strip("\r") for line in split_lines]


def stripped_lines(text: str) -> list[str]:
    """Return text's non-blank lines, each stripped of surrounding whitespace."""
    all_lines = text.splitlines()
    return [line.strip() for line in all_lines if line.strip()]


def reply_text(reply: Transcript) -> str:
    """Return the reply's text with the carriage returns of the game's line ends removed."""
    return reply.text.replace("\r", "")


def assert_lines(reply: Transcript, lines: Sequence[str]) -> None:
    """Assert the reply holds lines consecutively and in order.

    The check runs once its carriage returns are removed.
    """
    block = "\n".join(lines)
    assert block in reply_text(reply), f"expected:\n{block}\nin:\n{reply.text}"


def lines_after(text: str, header: str, until: str | None = None, *, drop_prompts: bool = True) -> list[str]:
    """Return the stripped, non-blank lines after the line holding header's first occurrence.

    The rest of the header's own line is dropped. With until, the lines stop before the first
    later line holding it, which is dropped too. drop_prompts drops lines ending in ">" (a game
    prompt). Fails when header, or a given until after it, is missing; returns an empty list when
    nothing follows.
    """
    assert header in text, f"no {header!r} in:\n{text}"
    after_header = text.split(header, 1)[1]
    # The first of these lines is the rest of the header's own line.
    following = after_header.splitlines()[1:]
    if until is not None:
        until_indexes = [index for index, line in enumerate(following) if until in line]
        assert until_indexes, f"no {until!r} after {header!r} in:\n{text}"
        following = following[:until_indexes[0]]

    kept = [line.strip() for line in following if line.strip()]
    if drop_prompts:
        kept = [line for line in kept if not line.endswith(">")]

    return kept


def assert_in_order(text: str, lines: Sequence[str]) -> None:
    """Assert each of lines appears exactly once in text, each starting after the one before.

    Two lines found at the same place (one a prefix of the other) fail as out of order.
    """
    position = -1
    for line in lines:
        assert text.count(line) == 1, f"expected {line!r} once:\n{text}"
        found = text.index(line)
        assert found > position, f"{line!r} out of order:\n{text}"
        position = found


def first_number(pattern: re.Pattern, text: str) -> int:
    """Return pattern's first group in its first match in text, as a number.

    Fails naming the pattern when it does not match.
    """
    match = pattern.search(text)
    assert match is not None, f"no {pattern.pattern!r} in:\n{text}"
    number_text = match.group(1)
    return int(number_text)


def capitalised(text: str) -> str:
    """Return text with its first letter upper case and the rest unchanged, as act() starts a line.

    Unlike str.capitalize, later letters are not lowered. text must not be empty.
    """
    return text[0].upper() + text[1:]
