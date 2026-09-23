"""Shared helpers for the blaze room-affect scenarios (test_blaze_after_quit.py,
test_blaze_after_caster_gone.py, test_kill_credit.py).

Timing model: blaze is a fast room affect (spec B1). `affect_update()` (limits.cpp:1658-1693)
walks ONE shared list of `TARGET_CHAR` and `TARGET_ROOM` entries, calling `affect_update_person`
for the former and `affect_update_room` for the latter, so every one of its callers reaches a
room affect the same way. Three things call it: the real-time fast block
(comm.cpp:1172-1175, `fast_update(); affect_update();`, every `PULSE_FAST_UPDATE` (~3s),
unconditional regardless of `harness_mode`), this harness's `harness tick` (test_harness.cpp,
the same pair on demand), and `harness affects()` (test_harness.cpp, `affect_update()` alone
with `harness_force_affect_phase` set). `harness affects()` is therefore NOT a no-op for blaze:
it still rolls occupants and spends duration through the exact same `affect_update_room` path
`tick` uses. What it does not do is force blaze's own roll -- `harness_force_affect_phase` is
read only inside `affect_update_person` (limits.cpp ~1373), never in the room path, since it
exists for slow PERSON affects and blaze is a fast room one -- or run `fast_update()`'s regen.
This module still drives every scenario with `harness tick`, matching the sibling scenarios'
existing convention, not because `affects()` would skip blaze; it would not.

`affect_update_room`'s duration decrement (limits.cpp ~1534: `(time_phase == tmpaf->time_phase)
|| (ROOMAFF_SPELL and is_fast)`) bypasses the phase-match half of its own condition for a fast
`ROOMAFF_SPELL`, so every call to it -- from `tick`, from `affects()`, or from a spontaneous
real-time sweep -- spends one unit of the SAME affect's duration. The three call sites are not
synchronized: a `harness tick` round trip that takes longer under a loaded or contended run
leaves more real wall-clock time for the spontaneous 3-second sweep to run in between, spending
duration this test's own tick budget never sees or accounts for. A fixed tick budget sized
against the affect's nominal duration (roughly a caster's mage level -- `get_mage_caster_level()`,
mage.cpp; ~30-33 for Harncaller, 120+ for Harnmage's raised fixture level, `rots_harness/fixtures.py`)
can therefore be outlived by the affect running out first on a slow enough run, well before the
budget itself is spent.

Whichever call sites run `fast_update()` (`tick` and the spontaneous sweep; not `affects()`)
also regenerate every character's (PC and NPC alike, no `IS_NPC` gate) current hit points toward
its real max on every single call -- including every one of this module's own `harness tick`
calls. A target's `hit` floored once before a multi-tick loop starts does not stay floored:
observed on a kept run, an un-refloored `Harnvictim` climbed from 9 into the high 20s over a
handful of forced ticks with no blaze hit ever landing in between, purely from this module's own
regen. `wizset <name> maxhit N` does not fix this -- it raises a PC's MAX hit (`constabilities.hit`
feeds `abilities.hit` through `recalc_abilities`, profs.cpp:756), never the current value, and
has no effect at all on an NPC's, whose `recalc_abilities` call is skipped outright. The only
reliable fix is to re-apply `wizset <name> hit N` (sets CURRENT hit directly, `act_wiz.cpp`'s `case 7`)
at the start of every loop iteration, so the target enters each roll at exactly the floor
regardless of how much the previous iteration's regen clawed back.

`LETHAL_HIT` (9) makes almost any single successful roll fatal, not literally every one: death
needs `hit <= -CON/2` (`update_pos`, fight.cpp ~215; this roster's CON is 11, so death needs
`hit <= -5`), and blaze's minimum possible damage -- the smallest raw roll
(`number(8, level) + 10`, minimum 18) halved by a save -- is exactly 9, which only brings a
floor-9 target to 0 (STUNNED: `-CON/4 < hit <= 0`), not dead. That rare worst case is why the
loop re-floors BEFORE each tick rather than after: a 0-hit survivor gets bumped back up to the
lethal-range floor for its NEXT roll, rather than being left to die of attrition, or of nothing
at all, alone.

The fix, in total: not a bigger budget (that only spends more of a duration shrinking just as
fast), but (a) re-flooring the target's hit every iteration so almost every successful roll is
lethal and the rare non-lethal one still gets a full-strength retry, minimizing how many of the
affect's duration units a scenario needs before it can possibly die, and (b) checking whether
the room still carries the affect at all -- scanning every reply that check makes for the death
marker too, since any one of them can just as easily race a broadcast -- before blaming a bare
timeout, so a genuine duration exhaustion under load reports itself clearly instead of looking
identical to "the roll just never landed."
"""

from __future__ import annotations

import re
import time

import pytest

import combat_support
from rots_harness.session import GameSession

# do_stat_room's own header (act_wiz.cpp:431): "Room name: %s%s%s\n\r" (color codes around the
# name are already stripped from the transcript by the session's ANSI sanitizer).
ROOM_NAME_LINE = re.compile(r"room name:\s*(.+)", re.IGNORECASE)

BLAZE_CAST = ("You breathe out fire.",)

# blaze_tick()'s dam = number(8, level) + 10, halved on a save (room_affect_tick.cpp:66-79); the
# roster's mage-capable casters (get_mage_caster_level(), mage.cpp:33-43) are level ~30-33 for
# Harncaller, 120+ for Harnmage's raised fixture level (rots_harness/fixtures.py) -- either way
# `number(8, level)`'s minimum stays 8, so the halved-on-save floor stays 9 regardless of caster.
# See the module docstring for why this is "almost always lethal in one hit," not an absolute
# guarantee.
LETHAL_HIT = 9


def floor_hit(imp: GameSession, mob_name: str, hit: int = LETHAL_HIT) -> str:
    """Sets `mob_name`'s CURRENT hit points (act_wiz.cpp `wizset ... hit`, `case 7`:
    `tmpabilities.hit = RANGE(-9, abilities.hit)`) so a single blaze burn is (almost always)
    fatal. This must be re-applied every iteration of a multi-tick loop -- see the module
    docstring for why a single up-front call does not survive `fast_update()`'s own regen, which
    every `harness tick` also runs. Returns the command's transcript text, since it can carry
    leading broadcast noise a caller may need to check (see `tick_until_marker`).
    """
    return imp.command(f"wizset {mob_name} hit {hit}").text


def room_stat_replies(imp: GameSession, attempts: int = 4) -> list[str]:
    """`combat_support.stat_replies()` for `stat room`, genuine on `do_stat_room`'s `Room name:`
    header. A caller watching for a marker (`tick_until_marker`) must scan every returned reply,
    not just trust the final one -- the room `imp` is standing in can broadcast unsolicited text
    ahead of ANY of them, not only the last.
    """
    return combat_support.stat_replies(imp, "room", lambda text: "room name:" in text.lower(), attempts)


def room_still_burning(imp: GameSession, expected_room: str, spell_name: str = "blaze") -> bool:
    """True when the last of `room_stat_replies()`'s replies (act_wiz.cpp's `do_stat_room` ->
    `show_room_affection`, mode 1) lists an active `spell_name` room affect (`Spell
    <name>(<location>) level <modifier> <duration>hrs, sets <bits>.`); a burnt-out (or
    never-applied) blaze leaves no such line. `imp` must be standing in the room that carries
    the affect -- every scenario using this module `goto`s there first.

    `expected_room` guards the case where that stopped being true without the caller's
    knowledge: a room-affect tick can kill `imp` itself, which auto-respawns to Immortal Start
    (RACE_GOD), and a `stat room` reply from there would misreport a healthy affect as "burned
    out" rather than naming the real cause. Compare the reply's own "Room name:" line against
    `expected_room` and fail loudly on a mismatch instead.
    """
    replies = room_stat_replies(imp)
    last = replies[-1]
    if "room name:" not in last.lower():
        pytest.fail(f"stat room never returned a parseable room-affection block in {len(replies)} attempts")
    match = ROOM_NAME_LINE.search(last)
    room_name = match.group(1).strip() if match else ""
    if room_name.lower() != expected_room.lower():
        pytest.fail(
            f"imp is standing in {room_name!r}, not {expected_room!r} -- it likely died (a "
            f"room-affect tick can kill imp itself) and respawned elsewhere; stat room reply:\n{last}"
        )
    return f"spell {spell_name}".lower() in last.lower()


def tick_until_marker(harness, imp: GameSession, observer: GameSession, marker: str, budget: int = 12, spell_name: str = "blaze", protect: tuple[GameSession, ...] = (), refloor: tuple[str, int] | None = None) -> str:
    """Forces `harness tick` up to `budget` times, watching `observer`'s transcript for
    `marker`.

    `harness.tick()` always drives its command through the `imp` session (conftest.py's
    `Harness` class), so a room-wide broadcast such as a lethal tick's "$n is dead!  R.I.P."
    reaches `imp`. `observer` is a different session for a
    message fight.cpp sends only to that one character ("You are dead!  Sorry...",
    `damage_credited`'s `POSITION_DEAD` arm) -- that text never reaches `imp`'s socket at all,
    so only draining `observer` can find it.

    `refloor`, when given as `(mob_name, hit)`, re-applies `floor_hit(imp, mob_name, hit)` at
    the start of every iteration -- the module docstring explains why a one-shot floor set
    before the loop does not survive this loop's own regen-driving `harness tick` calls.

    `protect`, when given, is a list of PC sessions to `restore` (act_wiz.cpp `do_restore`, sets
    current abilities back to their max) at the start of every iteration, before that
    iteration's tick can burn them -- the room affect damages every occupant each pass it rolls
    on (room_affect_tick.cpp), PCs included, so a scenario that needs its own participants alive
    afterward must not simply let a multi-tick loop run over them.

    `budget` stays comfortably under the affect's nominal duration (module docstring) so this
    loop is never itself the reason a healthy affect runs out. When the marker still hasn't
    appeared and `room_stat_replies()`'s last reply shows the affect itself is already gone,
    that is reported explicitly instead of as a bare timeout -- it means the room can no longer
    produce the marker at all, not that this particular call was unlucky.

    The marker is searched for in `observer`'s whole transcript since this call began, not in
    individual replies: `GameSession.command()` and `harness.tick()` drain up to 0.1s of
    already-pending text before sending and discard it (`session.py`, `conftest.py`), so a
    real-time broadcast landing in that window never appears in any reply.
    """
    observed_from = len(observer.everything)

    def _observed() -> str | None:
        seen = observer.everything[observed_from:]
        return seen if marker in seen else None

    for attempt in range(budget):
        if refloor is not None:
            floor_hit(imp, refloor[0], refloor[1])
            if (seen := _observed()) is not None:
                return seen
        for session in protect:
            imp.command(f"restore {session.character.name}")
            if (seen := _observed()) is not None:
                return seen
        harness.tick()
        if observer is not imp:
            observer.drain(1.0)
        if (seen := _observed()) is not None:
            return seen
        replies = room_stat_replies(imp)
        if (seen := _observed()) is not None:
            return seen
        if "room name:" not in replies[-1].lower():
            pytest.fail(f"stat room never returned a parseable room-affection block in {len(replies)} attempts")
        if f"spell {spell_name}".lower() not in replies[-1].lower():
            pytest.fail(
                f"the {spell_name} room affect expired after {attempt + 1} tick(s) without "
                f"{marker!r} ever appearing; a loaded run let the real-time fast block "
                f"(comm.cpp, ~3s) spend the affect's duration faster than this budget did -- "
                f"see blaze_support.py's module docstring"
            )
    pytest.fail(f"{marker!r} never appeared within {budget} harness ticks (room still burning)")


def wait_for_log_line(imp: GameSession, server, needle: str, timeout: float = 10.0) -> None:
    """Polls `server.handle.log_path` (game.log, the launcher's redirect of `mudlog()`'s
    `fprintf`-to-stderr file-logging arm, utility.cpp) for a line containing `needle`, instead of
    assuming a fixed amount of real time is enough for a server-side event `mudlog()` itself
    records -- e.g. `close_socket()`'s "Losing player: <name> [<host>]." or "Closing link to:
    <name> [<host>]." arms (comm.cpp), confirming a character's socket has actually closed.
    """
    deadline = time.monotonic() + timeout
    text = ""
    while True:
        if server.handle.log_path.exists():
            text = server.handle.log_path.read_text(encoding="latin-1", errors="replace")
            if needle in text:
                return
        if time.monotonic() >= deadline:
            pytest.fail(f"{needle!r} never appeared in {server.handle.log_path} within {timeout}s; log tail:\n{text[-2000:]}")
        imp.drain(0.3)
