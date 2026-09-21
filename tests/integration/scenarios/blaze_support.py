"""Shared helpers for the blaze room-affect scenarios (test_blaze_after_quit.py,
test_blaze_after_caster_gone.py, test_kill_credit.py).

Timing model: blaze is a fast room affect (spec B1). Once cast, it ticks from two independent
sources -- the real-time fast block (comm.cpp:1172-1175, `fast_update(); affect_update();`,
every `PULSE_FAST_UPDATE` (12 pulses at `structs.h`'s 4 pulses/sec, ~3s), unconditional
regardless of `harness_mode`) and this harness's own `harness tick` (test_harness.cpp), which
runs the identical `fast_update(); affect_update();` pair on demand. `harness affects()` never
touches it at all: that subcommand only forces the ONE slow *person*-affect phase compare
(limits.cpp), and blaze's own application roll is gated on neither that phase nor
`harness_force_affect_phase`.

Both sources decrement the SAME affect's duration on every call they make, because a fast
`ROOMAFF_SPELL` bypasses the phase-match half of `affect_update_room`'s decrement condition
(limits.cpp ~1534: `(time_phase == tmpaf->time_phase) || (ROOMAFF_SPELL and is_fast)`) -- every
call to `affect_update_room` for a still-active blaze affect spends one unit of its duration,
whether the call came from an explicit `harness tick` or a spontaneous real-time sweep. The two
sources are not synchronized: a `harness tick` round trip that takes longer under a loaded or
contended run leaves more real wall-clock time for the spontaneous 3-second sweep to run in
between, each pass spending duration this test's own tick budget never sees or accounts for. A
fixed tick budget sized against the affect's nominal duration (roughly a caster's mage level,
~30 for this roster's casters -- `get_mage_caster_level()`, mage.cpp) can therefore be outlived
by the affect running out first on a slow enough run, well before the budget itself is spent.

Both sources ALSO run `fast_update()` (limits.cpp:1698-1747), which regenerates every
character's (PC and NPC alike, no `IS_NPC` gate) current hit points toward its real max every
single call -- including every one of this module's own `harness tick` calls. A target's `hit`
floored once before a multi-tick loop starts does not stay floored: observed on a kept run, an
un-refloored `Harnvictim` climbed from 9 to the high 20s over a handful of forced ticks with no
blaze hit ever landing in between, simply from this module's own regen. `wizset <name> maxhit N`
does not fix this -- it writes `constabilities.hit`, a rolled/permanent baseline that only
feeds a PC's `abilities.hit` (the real max) as one small term in a much larger level/class-
driven formula (`recalc_abilities`, profs.cpp) and has no effect at all on an NPC's, whose
`recalc_abilities` call is skipped outright. The only reliable fix is to re-apply
`wizset <name> hit N` (which sets CURRENT hit directly, `act_wiz.cpp`'s `case 7`) at the start
of every loop iteration, so the target enters each tick's roll at exactly the floor regardless
of how much the previous iteration's regen clawed back.

The result: the fix is not a bigger budget (that only spends more of a duration shrinking just
as fast); it is (a) re-flooring the target's hit every iteration so a single successful roll is
always lethal, minimizing how many of the affect's duration units a scenario needs before it can
possibly die, and (b) checking whether the room still carries the affect at all before blaming a
bare timeout, so a genuine duration exhaustion under load reports itself clearly instead of
looking identical to "the roll just never landed."
"""

from __future__ import annotations

import pytest

from rots_harness.session import GameSession

BLAZE_CAST = ("You breathe out fire.",)

# blaze_tick()'s dam = number(8, level) + 10, halved on a save (room_affect_tick.cpp:66-79); the
# roster's mage-capable casters (Harnmage, Harncaller) are level ~30-33
# (get_mage_caster_level(), mage.cpp:33-43), so even the smallest halved roll clears this floor
# -- any ONE successful tick is lethal, which is the whole point: it minimizes how many of the
# affect's duration units (see module docstring) a scenario needs to survive to get one.
LETHAL_HIT = 9


def floor_hit(imp: GameSession, mob_name: str, hit: int = LETHAL_HIT) -> str:
    """Sets `mob_name`'s CURRENT hit points (act_wiz.cpp `wizset ... hit`, `case 7`:
    `tmpabilities.hit = RANGE(-9, abilities.hit)`) so a single blaze burn is fatal. This must be
    re-applied every iteration of a multi-tick loop -- see the module docstring for why a single
    up-front call does not survive `fast_update()`'s own regen, which every `harness tick` also
    runs. Returns the command's transcript text, since it can carry leading broadcast noise a
    caller may need to check (see `tick_until_marker`).
    """
    return imp.command(f"wizset {mob_name} hit {hit}").text


def room_still_burning(imp: GameSession, spell_name: str = "blaze", attempts: int = 4) -> bool:
    """`stat room` (act_wiz.cpp's `do_stat_room` -> `show_room_affection`, mode 1) lists each
    active room affect as `Spell <name>(<location>) level <modifier> <duration>hrs, sets
    <bits>.`; a burnt-out (or never-applied) blaze leaves no such line. `imp` must be standing
    in the room that carries the affect -- every scenario using this module `goto`s there first.

    `imp` stands in the very room the affect is burning, so it is itself a tick target: an
    unsolicited "burning" broadcast can race `stat room`'s own reply and satisfy `command()`'s
    end-of-prompt check before that reply arrives (`read_hp`'s docstring describes the same
    race for `stat <char>`). Retrying until a genuine `do_stat_room` reply (its `Room name:`
    header) is seen avoids reading that race as "the affect is gone."
    """
    marker = f"spell {spell_name}".lower()
    for _attempt in range(attempts):
        text = imp.command("stat room").text.lower()
        if "room name:" in text:
            return marker in text
    pytest.fail(f"stat room never returned a parseable room-affection block in {attempts} attempts")


def read_hp(imp: GameSession, char_name: str, attempts: int = 4) -> int:
    """Retries `stat <char_name>` up to `attempts` times before trusting the parsed HP block.

    `imp` typically stands in the very room a blaze affect is burning, so it is itself a tick
    target: an unsolicited "burning" broadcast (plus the MUD's usual prompt redisplay after it)
    can race a `stat` command's own reply and satisfy `command()`'s end-of-prompt check before
    that reply arrives -- the real reply then surfaces as leading noise ahead of the next
    command and gets silently drained away by it. The real `stat` block is never more than one
    command behind.
    """
    for _attempt in range(attempts):
        points = imp.command(f"stat {char_name}").hit_points()
        if points is not None:
            return points[0]
    pytest.fail(f"stat {char_name} never returned a parseable HP block in {attempts} attempts")


def tick_until_hp_drops(harness, imp: GameSession, char_name: str, before: int, floor_hit_value: int = LETHAL_HIT, budget: int = 12, spell_name: str = "blaze") -> int:
    """Forces `harness tick` up to `budget` times, re-flooring `char_name`'s hit points to
    `floor_hit_value` (`floor_hit`) at the start of every iteration -- see the module docstring
    for why this must happen every time, not just once -- then actively re-reading them
    (`read_hp`, an authoritative poll rather than trusting a broadcast to land in a drain window)
    until they read below `before`.

    `before` and `floor_hit_value` are deliberately separate: comparing against `floor_hit_value`
    itself does not work, because a floor low enough to be lethal in one hit (the whole point of
    `floor_hit_value`) means a successful roll never leaves the character alive with a lower-but-
    still-floor-relative HP to observe -- it kills and respawns them, and a PC's revival HP
    (`raw_kill`, fight.cpp -- a quarter of the real max on a player kill) can land ABOVE a low
    `floor_hit_value` even though a tick very much did land. `before` should be the character's
    true starting HP (read before any flooring), which a revival HP is always below.

    `budget` stays comfortably under the affect's nominal duration (module docstring) so this
    loop is never itself the reason a healthy affect runs out; when HP never drops below `before`
    and `stat room` shows the affect is already gone, that is reported explicitly instead of as a
    bare timeout.
    """
    after = before
    for attempt in range(budget):
        floor_hit(imp, char_name, floor_hit_value)
        harness.tick()
        after = read_hp(imp, char_name)
        if after < before:
            return after
        if not room_still_burning(imp, spell_name):
            pytest.fail(
                f"the {spell_name} room affect expired after {attempt + 1} tick(s) without "
                f"{char_name}'s hit points ever dropping below {before} (still {after}); "
                f"a loaded run let the real-time fast block (comm.cpp, ~3s) spend the affect's "
                f"duration faster than this budget did -- see blaze_support.py's module "
                f"docstring"
            )
    pytest.fail(f"{char_name}'s hit points never dropped below {before} within {budget} harness ticks (room still burning, still {after})")


def tick_until_marker(harness, imp: GameSession, observer: GameSession, marker: str, budget: int = 12, spell_name: str = "blaze", protect: tuple[GameSession, ...] = (), refloor: tuple[str, int] | None = None) -> str:
    """Forces `harness tick` up to `budget` times, watching `observer`'s transcript for
    `marker`.

    `harness.tick()` always drives its command through the `imp` session (conftest.py's
    `Harness` class) and its own `expect()` call already consumes everything up through
    "Harness: hourly tick complete." -- including any room-wide broadcast the tick's damage
    triggered (a lethal tick's "$n is dead!  R.I.P." among them) -- so when `observer is imp`
    this checks `harness.tick()`'s own returned text FIRST; a bare follow-up `imp.drain()` alone
    would see nothing new and silently spend the whole budget. `observer` is a different session
    for a message fight.cpp sends only to that one character ("You are dead!  Sorry...",
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
    appeared and `stat room` shows the affect itself is already gone, that is reported
    explicitly instead of as a bare timeout -- it means the room can no longer produce the
    marker at all, not that this particular call was unlucky.

    When `observer is imp`, EVERY `imp.command()` this loop issues (the `refloor`/`protect`
    commands included, not just `harness.tick()`) is checked for `marker` before moving on.
    `GameSession.command()` drains up to 0.1s of already-pending text before sending its own
    line (`session.py`), so a spontaneous real-time broadcast that lands on `imp`'s socket
    between iterations can surface as leading noise ahead of ANY of those replies, not only
    `harness.tick()`'s -- observed on a kept run where the marker never turned up in 12 ticks
    even though the room affect never expired, most likely because a `protect`/`refloor` call's
    own drain ate it. Checking every reply closes that gap for everything but the narrow 0.1s
    drain window itself.
    """
    def _seen(text: str) -> bool:
        return observer is imp and marker in text

    for attempt in range(budget):
        if refloor is not None:
            refloored = floor_hit(imp, refloor[0], refloor[1])
            if _seen(refloored):
                return refloored
        for session in protect:
            restored = imp.command(f"restore {session.character.name}").text
            if _seen(restored):
                return restored
        tick_text = harness.tick().text
        if _seen(tick_text):
            return tick_text
        drained = observer.drain(1.0)
        if marker in drained:
            return drained
        if not room_still_burning(imp, spell_name):
            pytest.fail(
                f"the {spell_name} room affect expired after {attempt + 1} tick(s) without "
                f"{marker!r} ever appearing; a loaded run let the real-time fast block "
                f"(comm.cpp, ~3s) spend the affect's duration faster than this budget did -- "
                f"see blaze_support.py's module docstring"
            )
    pytest.fail(f"{marker!r} never appeared within {budget} harness ticks (room still burning)")
