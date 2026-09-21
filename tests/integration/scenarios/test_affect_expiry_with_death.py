"""manual-test-plan.md item 11 ("Affect expiry is crash-safe under mid-tick mutation") and spec
B2's last catalogue row: Harnvictim's own person affect is at duration 1 -- about to expire --
when a blaze room tick kills it inside the SAME `affect_update()` call, after Harnmage has
already quit.

`affect_update()` (limits.cpp:1658-1696) snapshots `affected_list` before walking it head-to-tail
(the snapshot-walk banner is at 1607-1656); each `TARGET_CHAR` entry is re-validated by identity
before use (1679-1691), and a now-stale one (its character dead, extracted, or freed since the
snapshot) is dropped instead of dereferenced. `pool_to_list()` (utility.cpp) always prepends, so
the walk visits the MOST recently inserted affect first. Reaching the identity check's stale
branch needs the room's `TARGET_ROOM` entry (blaze) walked before Harnvictim's own `TARGET_CHAR`
entry, so the room tick's kill has already invalidated the person entry by the time the walk
reaches it -- so this file gives Harnvictim its own SPELL_ANGER BEFORE Harnmage ever casts blaze
(the room's node then lands above it) and, on every retry, refreshes that SAME affected_type node
in place (on_attacked_character's `existing_affect->duration = duration` path, never removing and
recreating it) rather than letting a fresh one land in the wrong position. No crash is the pass
condition (the crash monitor fails on any sanitizer report or SYSERR); this ordering is what
gives the guard something to actually skip.

Harnmage casts blaze into an empty room (Harnvictim steps out first) so nothing engages it and it
earns no SPELL_ANGER, then quits immediately. do_quit's extract_char(ch, -1) pulls it off
`character_list` synchronously, but its char_data stays resolvable via `char_by_abs_number()`
until comm.cpp's own I/O loop notices the closed socket and `close_socket()` calls `free_char()`
(db.cpp) -- only then does the room tick's credited-killer resolve correctly return nobody. This
file waits for `close_socket()`'s own "Losing player: Harnmage" mudlog line (game.log) rather
than assuming a fixed pause is long enough.
"""

from __future__ import annotations

import re
import time

import pytest

from blaze_support import BLAZE_CAST, floor_hit, room_still_burning, wait_for_log_line
from poison_support import DEATH_MARKER
from rots_harness import fixtures, records
from rots_harness.session import GameSession

pytestmark = pytest.mark.scenario

# act_wiz.cpp do_stat_character: "SPL: (%3dhr) %s" with duration+1; consts.cpp names SPELL_ANGER
# "anger". No modifier/bitvector suffix appears for it (both are 0), so this is the whole line.
ANGER_LINE = re.compile(r"SPL:\s*\(\s*(\d+)hr\)\s*anger", re.IGNORECASE)
FRESH_ANGER_HOURS = 3  # duration 2 (on_attacked_character, non-player target), displayed +1
STALE_ANGER_HOURS = 1  # duration 0: decremented by a failed decisive tick, not yet removed


def _anger_hours(stat_text: str) -> int | None:
    match = ANGER_LINE.search(stat_text)
    return int(match.group(1)) if match is not None else None


def _stat_victim_replies(imp: GameSession, attempts: int = 4) -> list[str]:
    """Collects up to `attempts` `stat harnvictim` replies, stopping once a genuine
    do_stat_character reply (its "IDNum:" field) is seen -- mirrors blaze_support.py's
    `room_stat_replies()` and for the same reason: an unsolicited broadcast, or a still-pending
    earlier reply, racing `command()`'s end-of-prompt check can leave stale or concatenated text
    ahead of the real reply, so only the LAST collected reply is trusted as genuine.
    """
    replies: list[str] = []
    for _attempt in range(attempts):
        text = imp.command("stat harnvictim").text
        replies.append(text)
        if "idnum:" in text.lower():
            break
    return replies


def _victims_anger_hours(imp: GameSession) -> int | None:
    """Harnvictim's current anger duration (displayed hours; None if absent), read from the
    guaranteed-genuine last reply of `_stat_victim_replies()`.
    """
    replies = _stat_victim_replies(imp)
    if "idnum:" not in replies[-1].lower():
        pytest.fail(f"stat harnvictim never returned a parseable reply in {len(replies)} attempts")
    return _anger_hours(replies[-1])


def _wait_for_fresh_anger(imp: GameSession, timeout: float = 10.0) -> None:
    """Polls until Harnvictim's anger reads back EXACTLY `FRESH_ANGER_HOURS` -- the value
    on_attacked_character sets, or resets an existing affect to in place, the instant
    Harnvictim's attack actually lands. "Any anger present" is not enough to trust: it could be
    the SAME node's stale reading from a previous attempt, still sitting at `STALE_ANGER_HOURS`
    until this attack's hit resets it.
    """
    deadline = time.monotonic() + timeout
    last_hours: int | None = None
    while True:
        last_hours = _victims_anger_hours(imp)
        if last_hours == FRESH_ANGER_HOURS:
            return
        if time.monotonic() >= deadline:
            pytest.fail(f"harnvictim's anger never read back a fresh duration of 2 ({FRESH_ANGER_HOURS}hr) within {timeout}s; last reading: {last_hours!r}hr")
        imp.drain(0.5)


def _earn_or_refresh_victims_anger(imp: GameSession, victim: GameSession, *, require_existing_node: bool = False) -> None:
    """One melee round against a throwaway orc earns (or refreshes, in place -- module
    docstring) Harnvictim's own SPELL_ANGER at duration 2. Must never run in the blazing Arena
    Centre: a mob loaded straight into the room is itself a blaze target from the moment it
    loads and can die to the room's own tick before `kill target` ever lands. The orc is purged
    the instant the affect is confirmed, so it cannot keep fighting, die on its own, or leave
    Harnvictim engaged with anybody (a still-engaged victim would let a later death credit that
    opponent instead of nobody -- fight.cpp's `engaged_opponent` fallback).

    `require_existing_node=True` (every retry) first requires the affect to still read
    `STALE_ANGER_HOURS`: a spontaneous phase-matched tick (affect_update_person, limits.cpp,
    roughly once per 60s of real time) could otherwise have removed the node in the interval
    since the last decisive tick, and refreshing it here would then create a brand new node
    ABOVE the room's blaze entry instead of below -- the inverted ordering that defeats the
    whole point of this scenario (module docstring).
    """
    if require_existing_node:
        hours = _victims_anger_hours(imp)
        if hours != STALE_ANGER_HOURS:
            pytest.fail(
                f"harnvictim's anger node should still read duration 0 (displayed "
                f"{STALE_ANGER_HOURS}hr) before a retry refresh, got {hours!r}hr -- a "
                f"spontaneous phase-matched tick likely removed it; refreshing now would "
                f"recreate it above the room's blaze entry, inverting the ordering this test "
                f"depends on"
            )
    imp.command("load mob 1130")
    victim.command("kill target")
    _wait_for_fresh_anger(imp)
    imp.command("purge target")
    imp.command("restore harnvictim")


def _kill_at_duration_one(harness, imp: GameSession, victim: GameSession, attempts: int = 10) -> bool:
    """Harnvictim already carries its own SPELL_ANGER, inserted before blaze's room entry
    (module docstring), and already stands in the blazing room. Each attempt forces one tick to
    bring that affect to duration 1 (confirmed via `stat`), then fires the decisive tick with
    Harnvictim's hit floored so the blaze room roll is lethal if it lands. A miss just leaves the
    affect expired (duration 0, not yet removed) without a death -- not what this test needs --
    so Harnvictim is moved to the unburning Arena West to refresh the SAME node before retrying.
    A burnt-out room affect is checked for explicitly, so it reports itself instead of looking
    like an ordinary miss.
    """
    for _attempt in range(attempts):
        if not room_still_burning(imp):
            pytest.fail(f"blaze burned out after {_attempt} retry attempt(s), before this test could land the decisive tick")

        harness.affects()  # countdown: duration 2 -> 1
        if _victims_anger_hours(imp) == 2:
            floor_hit(imp, "harnvictim")
            # The decisive call: affect_update() (limits.cpp:1658-1696) walks the room's blaze
            # entry -- inserted after Harnvictim's own, so it is processed FIRST (module
            # docstring) -- killing Harnvictim, and only then reaches the now-stale entry for
            # Harnvictim's own duration-1 anger, which the identity check (1679-1691) must skip
            # rather than dereference.
            harness.affects()
            if DEATH_MARKER in victim.drain(1.0):
                return True
            imp.command("restore harnvictim")

        imp.command(f"goto {fixtures.ROOM_ARENA_WEST}")
        imp.command("transfer harnvictim")
        victim.expect_room("Arena West")
        _earn_or_refresh_victims_anger(imp, victim, require_existing_node=True)

        imp.command(f"goto {fixtures.ROOM_ARENA_CENTRE}")
        imp.command("transfer harnvictim")
        victim.expect_room("Arena Centre")
    return False


def test_death_and_expiry_in_one_affect_update_after_a_quit(server, imp, mage, victim, harness) -> None:
    imp.command(f"goto {fixtures.ROOM_ARENA_CENTRE}")
    imp.command("transfer harnvictim")
    imp.command("restore harnvictim")

    # Harnvictim earns its own SPELL_ANGER here, in a room that does not carry blaze yet -- this
    # is what makes affected_list insert the room's node ABOVE this one once Harnmage casts,
    # rather than the other way around (module docstring).
    _earn_or_refresh_victims_anger(imp, victim)

    # Harnvictim steps out before Harnmage casts, so the on-cast burst engages nobody and
    # Harnmage earns no SPELL_ANGER of its own -- its quit right below needs no clearing loop.
    imp.command(f"goto {fixtures.ROOM_ARENA_WEST}")
    imp.command("transfer harnvictim")
    victim.expect_room("Arena West")

    imp.command(f"goto {fixtures.ROOM_ARENA_CENTRE}")
    imp.command("transfer harnmage")
    imp.command("restore harnmage")
    mage.cast("blaze", success_markers=BLAZE_CAST)
    mage.quit()

    # do_quit moves Harnmage to the account menu with its char_data still registered; only
    # close_socket()'s own mudlog line (module docstring) confirms free_char() has actually run
    # and the room tick's credited-killer resolve can correctly return nobody.
    wait_for_log_line(imp, server, "Losing player: Harnmage")

    imp.command("transfer harnvictim")
    victim.expect_room("Arena Centre")
    imp.command("restore harnvictim")

    assert _kill_at_duration_one(harness, imp, victim), (
        "harnvictim should die to a blaze room tick while its own person affect is at duration 1, "
        "inside the same affect_update() call, within the retry budget"
    )
    victim.expect_room("Wood-elf Start")

    # A quit frees the body: the room tick's credited-killer resolve cannot find Harnmage, and
    # Harnvictim was not otherwise engaged (the orc was purged before this) or poisoned, so
    # fight.cpp's die() has no contributor to name at all -- kill_contributors() comes back
    # empty and add_exploit_record()'s per-contributor loop never runs. read_exploits() returns
    # [] both for a file that exists with zero records and one that was never created; a kept
    # run confirmed harnvictim.exploits.json exists here and reads back the unmodified
    # account-creation stub {"version": 1, "records": []}, not the latter.
    victim_records = records.read_exploits(server.lib_dir, "Harnvictim")
    assert victim_records == [], (
        f"a departed, unengaged caster's kill must manufacture no exploit record at all: {victim_records}"
    )
