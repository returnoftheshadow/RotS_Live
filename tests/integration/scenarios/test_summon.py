"""manual-test-plan.md item 4: summon targets a player by name across rooms even when the
caster is standing in a dark room (`TAR_DARK_OK` in the spell's target mask lets a
name-targeted world spell skip the same-room darkness arm of `CAN_SEE()`, matching `tell`'s
precedent), and item 4 line 25: summoning a link-dead player relocates it without
dereferencing its missing descriptor.
"""

from __future__ import annotations

import pytest

from blaze_support import wait_for_log_line
from rots_harness import fixtures
from rots_harness.session import (
    ACCOUNT_MENU_PROMPT,
    CHARACTER_NUMBER_PROMPT,
    LOGIN_EMAIL_PROMPT,
    LOGIN_PASSWORD_PROMPT,
    GameSession,
)

pytestmark = pytest.mark.scenario

SUMMON_SUCCESS = ("appears in the room.",)  # spell_summon's act(...TO_ROOM/TO_CHAR...) to the caster's room (mage.cpp:876-877)
SUMMONED_MARKER = "summons you!"  # spell_summon's act(...TO_CHAR...) to the victim (mage.cpp:882)
RECONNECT_MARKER = "Reconnecting."  # complete_existing_character_login's linkless-body branch (interpre.cpp ~2780); see _reconnect()'s docstring


def _room_line(vnum: int) -> str:
    """do_stat_character's room field (act_wiz.cpp:761-763): "In room [%5d]\\n\\r" -- the
    vnum, not the room name, right-justified in a 5-wide field.
    """
    return f"In room [{vnum:5d}]"


def _reconnect(server, name: str) -> GameSession:
    """Log `name` back into its own existing body rather than a fresh character.

    `GameSession.login()` waits for `WELC_MESSG` ("Here we go..."), which only fires when
    `complete_existing_character_login` (interpre.cpp:2731) finds no live `character_list`
    entry for the selected idnum and falls through to `show_character_menu_impl` /
    `CON_SLCT` (interpre.cpp ~4267). A link-dead player's character stays in
    `character_list` with its old, closed descriptor (`close_socket`, comm.cpp:2119: the
    `desc = 0` detach is deliberately commented out, so `CON_LINKLS` filtering alone keeps
    it out of the autosave snapshot), so `complete_existing_character_login`'s character-list
    scan (interpre.cpp ~2774) finds it immediately after the character-number prompt and
    reconnects right there -- it never reaches the character menu at all, only printing
    "Reconnecting.\\n\\r". This mirrors `GameSession.login()` up through selecting the
    character number, then waits for the reconnect marker instead of the menu/entry steps.
    """
    session = GameSession(server.handle, server.spec(name), server.character_number(name), server.run_dir)
    session.expect([LOGIN_EMAIL_PROMPT], 15.0)
    session.send_line(fixtures.HARNESS_EMAIL)
    session.expect([LOGIN_PASSWORD_PROMPT])
    session.send_line(fixtures.HARNESS_PASSWORD)
    session.expect([ACCOUNT_MENU_PROMPT])
    session.send_line("2")
    session.expect([CHARACTER_NUMBER_PROMPT])
    session.send_line(str(server.character_number(name)))
    session.expect([RECONNECT_MARKER], 15.0)
    session.command("look")
    return session


def test_summon_by_name_from_the_dark_room(server, imp, caller, victim) -> None:
    # CAN_SEE (utility.cpp:1460) refuses a dark room to anyone without PRF_HOLYLIGHT, level
    # notwithstanding; `stat harnvictim` below resolves the name through get_char_vis, which
    # applies the same check, so without this the god-level `imp` still can't find a victim
    # standing in the Dark Cell.
    imp.command("set holylight on")
    imp.command(f"goto {fixtures.ROOM_DARK_CELL}")
    imp.command("transfer harncaller")
    imp.command("restore harncaller")
    imp.command(f"goto {fixtures.ROOM_CORRIDOR_TWO}")
    imp.command("transfer harnvictim")
    victim.expect_room("Corridor Two")

    # `cast(success_markers=...)` already raises unless SUMMON_SUCCESS[0] (the caster's own
    # arrival line, TO_CHAR) showed up, so no separate assert is needed on the return value here.
    caller.cast("summon", "harnvictim", success_markers=SUMMON_SUCCESS, attempts=10)
    victim.expect([SUMMONED_MARKER], 10.0)  # the victim's own line (TO_CHAR)

    stat = imp.command("stat harnvictim")
    assert _room_line(fixtures.ROOM_DARK_CELL) in stat.text, stat.text


def test_summon_of_a_link_dead_character_relocates_it_without_a_crash(server, imp, caller, victim) -> None:
    imp.command(f"goto {fixtures.ROOM_ARENA_EAST}")
    imp.command("transfer harncaller")
    imp.command("restore harncaller")
    imp.command(f"goto {fixtures.ROOM_CORRIDOR_TWO}")
    imp.command("transfer harnvictim")
    victim.expect_room("Corridor Two")
    victim.drop_link()
    # Waits for close_socket()'s own mudlog line (comm.cpp:2116) instead of a fixed pause, so the
    # server has actually processed the closed socket before the cast targets the linkless body.
    wait_for_log_line(imp, server, "Closing link to: Harnvictim")

    caller.cast("summon", "harnvictim", success_markers=SUMMON_SUCCESS, attempts=10)
    # act()'s TO_CHAR branch only sends when `to->desc` is set (comm.cpp:2517), and do_look
    # bails immediately on `!ch->desc` or `!ch->desc->descriptor` (act_info.cpp:1045-1048);
    # spell_summon's act(...TO_CHAR...) and do_look(victim, "", 0, 0, 0) calls on the linkless
    # victim (mage.cpp:882-883) exercise exactly those guards. The autouse crash-monitor
    # fixture fails this test on any signal or sanitizer report, so no explicit crash
    # assertion is needed here.

    stat = imp.command("stat harnvictim")
    assert _room_line(fixtures.ROOM_ARENA_EAST) in stat.text, stat.text

    reconnected = _reconnect(server, "Harnvictim")
    try:
        reconnected.expect_room("Arena East")
        reconnected.quit()
    finally:
        reconnected.close()
