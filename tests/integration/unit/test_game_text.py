"""Unit tests for rots_harness.game_text: each text is one the game's sources print."""

from __future__ import annotations

from functools import cache
from pathlib import Path

import pytest

from rots_harness import game_text

# The repository root, above tests/integration/unit.
REPOSITORY_ROOT = Path(__file__).resolve().parents[3]
# The game's sources.
SOURCE_DIR = REPOSITORY_ROOT / "src"
# The combat message file the game reads its damage messages from (MESS_FILE, db.h).
MESSAGES_FILE = REPOSITORY_ROOT / "lib" / "misc" / "messages"
# The source files' suffixes.
SOURCE_SUFFIXES = (".cpp", ".h")
# Each text whose sources print part of it at run time, with that part (see the text's own comment).
FILLED_IN_PARTS = {
    "CORPSE_CONTENTS_HEADER": "corpse",
    "RENTED_REPLY": "The harness innkeeper",
    "INNKEEPER_TELLS_START": "The harness innkeeper",
    "FOLLOWER_ACK": "him",
}


def game_texts() -> list[tuple[str, str]]:
    """Every string constant of game_text, by name."""
    bindings = vars(game_text).items()
    return [(name, value) for name, value in bindings if name.isupper() and isinstance(value, str)]


def fixed_pieces(name: str, text: str) -> list[str]:
    """Return the pieces of text the game prints as written.

    A format string splits at each {}, and at the part FILLED_IN_PARTS names for name.
    """
    filled_in = FILLED_IN_PARTS.get(name)
    if filled_in is not None:
        text = text.replace(filled_in, "{}", 1)

    pieces = text.split("{}")
    return [piece for piece in pieces if piece.strip()]


@cache
def source_text() -> str:
    """The game's source files, without its unit tests, and its combat message file, joined."""
    every_path = SOURCE_DIR.rglob("*")
    sorted_paths = sorted(every_path)
    paths = [
        path
        for path in sorted_paths
        if path.suffix in SOURCE_SUFFIXES and "tests" not in path.relative_to(SOURCE_DIR).parts
    ]
    paths.append(MESSAGES_FILE)
    return "\n".join(path.read_text(encoding="latin-1") for path in paths)


# Every text the module holds, by name, for the parametrized check.
GAME_TEXTS = game_texts()


def test_game_text_holds_texts() -> None:
    assert len(GAME_TEXTS) > 10


@pytest.mark.parametrize("name, text", GAME_TEXTS)
def test_each_text_is_printed_by_the_game(name: str, text: str) -> None:
    pieces = fixed_pieces(name, text)
    sources = source_text()
    for piece in pieces:
        assert piece in sources, f"{name}: no {piece!r} in {SOURCE_DIR} or {MESSAGES_FILE}"


def test_a_format_string_splits_at_its_fields() -> None:
    assert fixed_pieces("ANY_FORMAT", "{} has teleported you!") == [" has teleported you!"]
    assert fixed_pieces("CORPSE_CONTENTS_HEADER", "corpse (here) :") == [" (here) :"]
    assert fixed_pieces("FOLLOWER_ACK", "ACK! You could not follow him!") == ["ACK! You could not follow ", "!"]
