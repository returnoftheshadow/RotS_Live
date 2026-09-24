"""Shared helpers for scenarios that force a mob or player into (or out of) melee, defang a
mob's own attacks so a forced tick or DoT is what lands the kill, or need a `stat` reply that is
not stale broadcast noise racing `command()`'s end-of-prompt check -- used by test_kill_credit.py,
test_fireball_splash.py, test_poison_punishment_player_poison_mob_fight.py,
test_poison_punishment_snake.py, and (via blaze_support.room_stat_replies) the blaze scenarios.
"""

from __future__ import annotations

import time
from typing import TYPE_CHECKING, Callable

import pytest

from poison_support import DEATH_MARKER
from rots_harness import fixtures, records
from rots_harness.session import GameSession, SessionTimeout

if TYPE_CHECKING:
    from conftest import Harness, HarnessServer

BRUTE_ORC_VNUM = 1133  # world/mob/11.mob: "exists to kill a player who stands and fights"
BRUTE_ENERGY_REGEN = 100
# Harnvictim's level whenever a level-30 player must damage it: Big Brother refuses an attacker at
# three or more times the defender's level (big_brother.cpp is_level_range_appropriate); 11 gives 33.
VICTIM_LEVEL = 11
QUIT_BLOCKED = "You may not quit yet."  # act_othe.cpp do_quit, while SPELL_ANGER lingers
QUIT_SUCCEEDED = ("Goodbye", "As you quit")


def wait_for_engagement(imp: GameSession, mob_name: str, victim_name: str, timeout: float = 10.0) -> None:
    """Waits for `stat <mob_name>` to read `Fighting: <victim_name>` instead of trusting one
    reply. `game_loop()`'s "process_commands" step (comm.cpp ~930-972) drains each descriptor's
    queued input once per pulse, then executes one command per descriptor in `descriptor_list`
    order -- a fixed per-pulse iteration order, not send-time order. `imp`'s `stat` is a separate
    descriptor from the one that issued `kill`, so it can be processed before `kill` in that same
    pass, or in the next one, rather than being guaranteed to observe `kill`'s effects already
    committed; this closes that cross-descriptor input-ordering race.
    """
    deadline = time.monotonic() + timeout
    last_text = ""
    while True:
        stat = imp.command(f"stat {mob_name}")
        last_text = stat.text
        if f"fighting: {victim_name.lower()}" in stat.text.lower():
            return
        if time.monotonic() >= deadline:
            pytest.fail(f"{mob_name} never engaged {victim_name} within {timeout}s: {last_text}")
        imp.drain(0.5)


def wait_for_disengagement(imp: GameSession, names: tuple[str, ...], timeout: float = 10.0) -> None:
    """Waits for every name's `stat` to read `Fighting: Nobody`: char_from_room/char_to_room (the
    wizard `transfer`) never touch `specials.fighting`, so disengagement only happens on a later
    violence pulse's room-mismatch check (fight.cpp's `stop_fighting`, ~3084). Closes the race
    between a `transfer` returning and combat_list actually reflecting the move.
    """
    deadline = time.monotonic() + timeout
    pending = list(names)
    last_text = ""
    while pending:
        for name in list(pending):
            stat = imp.command(f"stat {name}")
            last_text = stat.text
            if "Fighting: Nobody" in stat.text:
                pending.remove(name)
        if not pending:
            return
        if time.monotonic() >= deadline:
            pytest.fail(f"{', '.join(pending)} never disengaged from combat within {timeout}s: {last_text}")
        imp.drain(0.5)


# do_wizset's generic NUMBER-field reply (act_wiz.cpp:3113-3115); confirmed rendering
# "Harnfighter's OB set to -20." / "Harnfighter's damage set to -20." against a kept run
# (ROTS_IT_KEEP=1, test_kill_credit.py). Only the field-name substring is matched, mirroring
# test_fireball_splash.py's WIZSET_HIT_REPLY, since the name varies by caller.
WIZSET_OB_REPLY = "'s OB set to"
WIZSET_DAMAGE_REPLY = "'s damage set to"


def neutralize_melee(imp: GameSession, name: str) -> None:
    """Bottoms out `name`'s offense and damage bonuses (act_wiz.cpp wizset fields "OB" and
    "damage", both floored at -20) so its melee rounds land for ~0 damage -- fight.cpp's hit()
    clamps `dam = max(0, dam)` after the OB/roll multipliers, and a negative damage stat wins
    even on the ~1/35 "sure hit" roll that bypasses dodge/parry (that roll only forces OB
    non-negative, never the damage stat). Closes the race between `name`'s own attacks and
    whatever forced tick or DoT a scenario needs to be the one that lands the kill.

    Sent with `send_line` + `expect` on each command's own reply, not `command()`: a fight
    already running in `imp`'s room can end a combat-spam broadcast with a prompt, and
    `command()`'s wait is satisfied by any prompt, not specifically the wizset reply, so it can
    return before the wizset has actually executed server-side (gotchas.md "Timing"; mirrors
    test_fireball_splash.py's `_imp_do`).
    """
    imp.drain(0.1)
    imp.send_line(f"wizset {name} OB -20")
    imp.expect((WIZSET_OB_REPLY,), timeout=8.0)
    imp.drain(0.1)
    imp.send_line(f"wizset {name} damage -20")
    imp.expect((WIZSET_DAMAGE_REPLY,), timeout=8.0)


def stat_replies(imp: GameSession, target: str, is_genuine: Callable[[str], bool], attempts: int = 4) -> list[str]:
    """Collects up to `attempts` `stat <target>` replies, stopping once `is_genuine` accepts one.
    `imp` can itself be standing in a room broadcasting unsolicited text (a burning room tick, a
    death message), which can race `stat`'s own reply and satisfy `command()`'s end-of-prompt
    check before the real reply arrives, leaving stale or concatenated text ahead of it. Closes
    that race by taking the caller's own test for what a genuine reply looks like, rather than
    trusting any single collected reply.
    """
    replies: list[str] = []
    for _attempt in range(attempts):
        text = imp.command(f"stat {target}").text
        replies.append(text)
        if is_genuine(text):
            break
    return replies


def prove_exploit_reader_with_a_brute_death(server: HarnessServer, imp: GameSession, player: GameSession, player_name: str, timeout: float = 30.0) -> None:
    """Positive control for a scenario that asserts some exploit record was NOT written: has
    `player` die to the brute orc's melee in Arena Centre and asserts records.read_exploits()
    sees the EXPLOIT_MOBDEATH record die() writes for it, so an empty read elsewhere in the same
    test means "no record", not "wrong directory". Only the dead player's own file changes: no
    player fights it, so kill_contributors() names nobody. `imp` is left in Arena Centre and the
    brute is purged, so nothing is still fighting when the session fixtures quit.
    """
    imp.command(f"goto {fixtures.ROOM_ARENA_CENTRE}")
    imp.command(f"transfer {player_name}")
    player.expect_room("Arena Centre")
    imp.command(f"load mob {BRUTE_ORC_VNUM}")
    # Every harness mob loads with an energy regen of 0 (11.mob), so it only ever defends; the
    # brute needs a regen to build ENE_TO_HIT and swing back (fight.cpp's violence pulse).
    imp.command(f"wizset brute ENE_regen {BRUTE_ENERGY_REGEN}")
    imp.command(f"wizset {player_name} hit 1")
    player.command("kill brute")
    player.expect([DEATH_MARKER], timeout)
    imp.command("purge brute")

    player_records = records.read_exploits(server.lib_dir, player_name)
    assert any(record.type == records.EXPLOIT_MOBDEATH and "brute" in record.victim_name.lower() for record in player_records), (
        f"positive control: the reader must see the death record the server just wrote for {player_name}: {player_records}"
    )


def quit_once_anger_allows(player: GameSession, harness: Harness, attempts: int = 10) -> None:
    """Quits `player`, first ageing away the SPELL_ANGER that attacking a character leaves on
    the attacker (char_utils_combat.cpp's on_attacked_character) and that makes do_quit refuse.
    SPELL_ANGER is a slow (non-"is_fast") affect: affect_update_person only ages it when the
    current real-time phase matches the phase recorded when it was applied, which is roughly
    once per game hour (~60s), or unconditionally on a forced `harness affects` tick
    (harness_force_affect_phase). Waiting on the real phase match would be impractically slow,
    so each refusal is followed by one forced tick. The forced ticks age every other affect too,
    so call this only once the scenario's own assertions are done or cannot be disturbed.
    """
    for _attempt in range(attempts):
        player.send_line("quit")
        try:
            text = player.expect(QUIT_SUCCEEDED + (QUIT_BLOCKED,), 8.0)
        except SessionTimeout:
            # Close here so the fixture teardown does not quit again and report a second failure.
            player.close()
            raise
        if QUIT_BLOCKED not in text:
            player.close()
            return
        harness.affects()
    pytest.fail(f"{player.character.name}'s SPELL_ANGER never cleared enough to quit in {attempts} forced ticks")
