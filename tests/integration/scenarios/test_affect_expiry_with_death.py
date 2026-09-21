"""manual-test-plan.md item 11 ("Affect expiry is crash-safe under mid-tick mutation") and spec
B2's last catalogue row: a victim whose own person affect is at duration 1 -- about to expire --
dies to a blaze room tick inside the SAME `affect_update()` call, after another occupant has
already quit.

`affect_update()` (src/limits.cpp:1658-1696) snapshots `affected_list` before walking it
(1607-1656's banner explains why: a body run mid-walk, such as a room tick's kill, can free or
recycle list storage a live walk would still be holding a pointer into). Each `TARGET_CHAR`
entry is then re-validated by identity before it is touched (1679-1691): the character the entry
named must still resolve from its `abs_number`, be the SAME pointer the entry captured, and carry
the SAME `registration_serial` -- otherwise the entry is dropped as stale instead of dereferenced.

Reaching that specific branch needs the room's `TARGET_ROOM` entry to be walked BEFORE
Harnvictim's own `TARGET_CHAR` entry in the SAME pass, so the room tick's kill has already
invalidated the person entry by the time the walk reaches it. `pool_to_list()`
(src/utility.cpp) always prepends -- the affected_list head is whichever affect was inserted
most recently, and the walk goes head-to-tail -- so this file earns Harnvictim its own SPELL_ANGER
BEFORE Harnmage ever casts blaze: the room's node lands on top of Harnvictim's already-existing
one and is walked first. Every later retry reuses that SAME affected_type node rather than
letting it expire and be removed (on_attacked_character's `existing_affect->duration = duration`
path resets an EXISTING affect's duration in place without touching the list at all), so the
relative order -- room entry before Harnvictim's person entry -- holds for every decisive tick
this file fires, not only the first. Casting blaze first and only then giving Harnvictim its
affect (this file's original approach) gets the order backwards: Harnvictim's freshly-created
node would always land ABOVE the room's fixed, already-inserted one, so the room tick would
always be walked AFTER the person entry -- by which point that entry has already been processed
safely, while Harnvictim was still alive, and the identity check's stale-entry branch is never
actually reached. No crash is still the pass condition (the crash monitor fails the test on any
sanitizer report or SYSERR); this ordering is what makes sure a crash, if the guard were removed,
would actually have something to trip over.

Harnmage's own quit happens well before the decisive tick, and needs no anger-clearing loop of
its own: it casts blaze into an EMPTY room (Harnvictim is moved out first, matching
test_blaze_after_quit.py's own idiom) so nothing engages it and it earns no SPELL_ANGER, then
quits immediately -- do_quit (act_othe.cpp) has nothing left to refuse it for. (The "another
occupant with expiring affects" framing in this task's brief is explicitly only an example,
"e.g."; an earlier version of this file had Harnmage's own SPELL_ANGER age down before quitting,
but engaging Harnvictim for that also let it occasionally land a hit back during the brief
mutual combat, creating an extra, unpredictable person-affect node with no bearing on what this
scenario actually needs to pin.)

do_quit's extract_char(ch, -1) pulls Harnmage off `character_list` synchronously, but its
char_data stays registered in char_by_abs_number()'s own table
(characters_by_abs_number[]/char_exists(), handler.cpp) until comm.cpp's own select() loop
notices the closed socket and calls close_socket() -> free_char() (db.cpp) -- the room tick's
credited-killer resolve (caster_snapshot::resolve()) only returns nullptr for Harnmage once THAT
has happened, not the instant the quit command itself returns. A kept run without an explicit
pause here showed the decisive tick still crediting the not-yet-freed Harnmage (an EXPLOIT_DEATH
record naming it); this file waits out that gap deliberately (see the test body) rather than
assuming a departed player's body is gone as soon as the quit confirmation text arrives. "The
server is single-threaded, so 'quits in the same tick' is realised as the ordering above, not a
race" (spec B2): the quit is simply the last thing to happen before this file moves Harnvictim
back into the blazing room and starts the decisive tick loop, not a literal zero-gap adjacency.

Attacking a non-player target earns Harnvictim only duration 2 (on_attacked_character's
`is_long_anger` is false for an NPC victim), not the level-scaled ~30 a mage-cast spell would
carry (see blaze_support.py's module docstring on why a caster-level duration is impractical to
count down); one forced tick brings that to exactly duration 1, which is the state this file
confirms via `stat harnvictim`'s "SPL: (Nhr) anger" line (duration is displayed as duration+1,
act_wiz.cpp's do_stat_character) before firing the decisive tick. Landing the room roll on that
exact tick is not guaranteed (limits.cpp's `affect_update_room`: a 1-in-13 chance, or 1-in-3 for
a fast spell like blaze, per occupant per call -- roughly 38% per attempt), so this retries a
bounded number of times, waiting for the SAME node to read back a fresh duration 2 (not just
"any anger present" -- a kept run showed a re-attack's first swing miss
(`"A target orc dodges Harnvictim's attack."`), leaving the previous attempt's stale "(  1hr)"
reading to be misread as fresh) each time. Every attempt costs exactly two forced ticks
(countdown, decisive), so the whole retry budget stays far under blaze's own nominal duration
(mage.cpp's spell_blaze, `af.duration = level`, ~33-34 for this roster's Harnmage --
get_mage_caster_level(), mage.cpp, template Int 18) -- checked explicitly each attempt anyway,
so a burnt-out room affect reports itself clearly instead of looking like an ordinary miss.
"""

from __future__ import annotations

import re
import time

import pytest

from blaze_support import BLAZE_CAST, floor_hit, room_still_burning
from poison_support import DEATH_MARKER
from rots_harness import fixtures, records
from rots_harness.session import GameSession

pytestmark = pytest.mark.scenario

# act_wiz.cpp do_stat_character: "SPL: (%3dhr) %s" with duration+1; consts.cpp names SPELL_ANGER
# "anger". No modifier/bitvector suffix appears for it (both are 0), so this is the whole line.
ANGER_LINE = re.compile(r"SPL:\s*\(\s*(\d+)hr\)\s*anger", re.IGNORECASE)
FRESH_ANGER_HOURS = 3  # duration 2 (on_attacked_character, non-player target), displayed +1


def _anger_hours(stat_text: str) -> int | None:
    match = ANGER_LINE.search(stat_text)
    return int(match.group(1)) if match is not None else None


def _wait_for_fresh_anger(imp: GameSession, timeout: float = 10.0) -> None:
    """Polls `stat harnvictim` until its anger reads EXACTLY `FRESH_ANGER_HOURS` -- the value
    on_attacked_character sets (or resets an EXISTING affect to, in place) the instant
    Harnvictim's attack actually lands. This scenario reuses the SAME affected_type node for
    Harnvictim's anger across every retry (module docstring: the node must never be allowed to
    fully expire and be removed, or a fresh one would land in the wrong list position), so "any
    anger present" is not enough to trust -- it could be that SAME node's stale reading from the
    previous attempt's decisive tick, still sitting at duration 0 until this attack's hit resets
    it.
    """
    deadline = time.monotonic() + timeout
    last_text = ""
    while True:
        last_text = imp.command("stat harnvictim").text
        if _anger_hours(last_text) == FRESH_ANGER_HOURS:
            return
        if time.monotonic() >= deadline:
            pytest.fail(f"harnvictim's anger never read back a fresh duration of 2 ({FRESH_ANGER_HOURS}hr) within {timeout}s: {last_text}")
        imp.drain(0.5)


def _earn_or_refresh_victims_anger(imp: GameSession, victim: GameSession) -> None:
    """One melee round against a throwaway orc earns (or, on a retry, refreshes -- module
    docstring) Harnvictim's own SPELL_ANGER at duration 2. This must never run in the blazing
    Arena Centre: a mob loaded straight into the room is itself a blaze target from the moment
    it loads, and an early kept run showed the room's own tick killing a freshly-loaded orc
    before Harnvictim's `kill` command could land a single hit on it. The orc is purged the
    instant the affect is confirmed, so it cannot keep fighting back, die on its own, or leave
    Harnvictim engaged with anybody -- a still-engaged victim would let a later death credit
    that opponent instead of nobody (fight.cpp's `engaged_opponent` fallback, see the sibling
    blaze scenarios).
    """
    imp.command("load mob 1130")
    victim.command("kill target")
    _wait_for_fresh_anger(imp)
    imp.command("purge target")
    imp.command("restore harnvictim")


def _kill_at_duration_one(harness, imp: GameSession, victim: GameSession, attempts: int = 10) -> bool:
    """Harnvictim already carries its own SPELL_ANGER, inserted into affected_list BEFORE
    blaze's room entry (module docstring), and already stands in the blazing room. Each attempt
    forces one tick to bring that affect to duration 1 (confirmed via `stat`), then fires the
    decisive tick with Harnvictim's hit floored so the blaze room roll is lethal if it lands. A
    miss just means the affect is now expired (duration 0, not yet removed) without a death in
    that call -- not the condition this test needs -- so Harnvictim is moved to the (unburning)
    Arena West to refresh the SAME anger node before the next attempt, then moved back.
    """
    for _attempt in range(attempts):
        if not room_still_burning(imp):
            pytest.fail(f"blaze burned out after {_attempt} retry attempt(s), before this test could land the decisive tick")

        harness.affects()  # countdown: duration 2 -> 1
        hours = _anger_hours(imp.command("stat harnvictim").text)
        if hours == 2:
            floor_hit(imp, "harnvictim")
            # The decisive call: affect_update() (limits.cpp:1658-1696) walks the room's blaze
            # entry -- inserted after Harnvictim's own, so it is processed FIRST (module
            # docstring) -- killing Harnvictim, and only then reaches the now-stale entry for
            # Harnvictim's own duration-1 anger, which the identity check (1679-1691) must skip
            # rather than dereference.
            harness.affects()
            if DEATH_MARKER in victim.drain(0.5):
                return True
            imp.command("restore harnvictim")

        imp.command(f"goto {fixtures.ROOM_ARENA_WEST}")
        imp.command("transfer harnvictim")
        victim.expect_room("Arena West")
        _earn_or_refresh_victims_anger(imp, victim)

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

    # do_quit moves Harnmage to the account menu with its char_data still registered; the
    # server's own socket-close detection (module docstring) needs a moment of real time to
    # finish freeing it before the room tick's credited-killer resolve can correctly return
    # nobody. A kept run without this pause showed the decisive tick still crediting the
    # not-yet-freed Harnmage.
    imp.drain(3.0)

    imp.command("transfer harnvictim")
    victim.expect_room("Arena Centre")
    imp.command("restore harnvictim")

    assert _kill_at_duration_one(harness, imp, victim), (
        "harnvictim should die to a blaze room tick while its own person affect is at duration 1, "
        "inside the same affect_update() call, within the retry budget"
    )
    victim.expect_room("Wood-elf Start")

    # A quit frees the body (module docstring): the room tick's credited-killer resolve cannot
    # find Harnmage, and Harnvictim was not otherwise engaged (the orc was purged before this) or
    # poisoned, so fight.cpp's die() has no contributor to name at all -- kill_contributors()
    # (combat_list walk, resolve_poisoner(), and the credited killer, all empty/null here) comes
    # back empty, and add_exploit_record()'s per-contributor loop then writes nothing. A kept run
    # confirmed this precisely: harnvictim.exploits.json reads back {"version": 1, "records": []}.
    victim_records = records.read_exploits(server.lib_dir, "Harnvictim")
    assert victim_records == [], (
        f"a departed, unengaged caster's kill must manufacture no exploit record at all: {victim_records}"
    )
