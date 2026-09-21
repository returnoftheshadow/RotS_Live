"""Shared helpers for scenarios that force a mob or player into (or out of) melee, defang a
mob's own attacks so a forced tick or DoT is what lands the kill, or need a `stat` reply that is
not stale broadcast noise racing `command()`'s end-of-prompt check -- used by test_kill_credit.py,
test_poison_punishment_player_poison_mob_fight.py, test_poison_punishment_snake.py, and (via
blaze_support.room_stat_replies) the blaze scenarios.
"""

from __future__ import annotations

import time
from typing import Callable

import pytest

from rots_harness.session import GameSession


def wait_for_engagement(imp: GameSession, mob_name: str, victim_name: str, timeout: float = 10.0) -> None:
    """Waits for `stat <mob_name>` to read `Fighting: <victim_name>` instead of trusting one
    reply. `game_loop()`'s "process_commands" step (comm.cpp ~903-965) drains each descriptor's
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


def neutralize_melee(imp: GameSession, name: str) -> None:
    """Bottoms out `name`'s offense and damage bonuses (act_wiz.cpp wizset fields "OB" and
    "damage", both floored at -20) so its melee rounds land for ~0 damage -- fight.cpp's hit()
    clamps `dam = max(0, dam)` after the OB/roll multipliers, and a negative damage stat wins
    even on the ~1/35 "sure hit" roll that bypasses dodge/parry (that roll only forces OB
    non-negative, never the damage stat). Closes the race between `name`'s own attacks and
    whatever forced tick or DoT a scenario needs to be the one that lands the kill.
    """
    imp.command(f"wizset {name} OB -20")
    imp.command(f"wizset {name} damage -20")


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
