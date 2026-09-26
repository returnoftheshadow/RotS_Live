"""`man spell blaze` and `man spell mist of baazunga` return their entries of lib/text/spel_tbl.
The help chapters index every entry's first line as keywords at boot (build_help_index,
modify.cpp) and `man` prefix-matches the chapter name then the keyword (do_help with subcmd 1,
act_info.cpp ~2039); a missing entry answers with the chapter's "no help available" text instead.

Each entry must fit the pager's 22-line page (show_string, modify.cpp): a longer entry stops at
"Press return to continue", the reply lacks the text below the break, and the imp is left inside
the pager for the rest of the session.
"""
from __future__ import annotations

import pytest

pytestmark = pytest.mark.scenario

BLAZE_SYNTAX = "cast 'blaze'"  # the entry's [Syntax] line
BLAZE_DESCRIPTION = 'Casting "Blaze" with no target breathes a cloud of fire'


def test_man_spell_blaze_shows_the_entry(server, imp) -> None:
    reply = imp.command("man spell blaze")
    assert BLAZE_SYNTAX in reply.text, reply.text
    assert BLAZE_DESCRIPTION in reply.text, reply.text


MIST_SYNTAX = "cast 'mist of baazunga'"  # the entry's [Syntax] line
MIST_DESCRIPTION = 'Casting "Mist of Baazunga" breathes out a dark mist that shrouds the room in'


def test_man_spell_mist_of_baazunga_shows_the_entry(server, imp) -> None:
    reply = imp.command("man spell mist of baazunga")
    assert MIST_SYNTAX in reply.text, reply.text
    assert MIST_DESCRIPTION in reply.text, reply.text
