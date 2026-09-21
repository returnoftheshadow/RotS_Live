"""A fireball's splash engages a bystander mob with the caster alone -- not the fighter, who
stays engaged with the primary target throughout -- and neither death that follows (the primary
target's, the bystander's) manufactures a player-facing kill-credit record. spell_fireball
(mage.cpp 1914-1996) delivers its primary hit to the cast's victim, then walks every other room
occupant (1959-1991) and rolls each one independently at target_number 0.2 (0.8 once that
occupant is already fighting the caster), delivering a hit as SPELL_FIREBALL2 through
apply_spell_damage -- whose damage()/damage_credited() bookkeeping is what puts a splashed
bystander into combat with the caster. Harnfighter is itself an eligible splash roll (the loop
only skips the caster and the primary victim); its own engagement guard
(fight.cpp:1938-1940, `if (!(victim->specials.fighting)) set_fighting(victim, attacker)`) looks
like it should protect an already-engaged Harnfighter from being redirected, but the primary hit
runs first (mage.cpp:1955-1957) and, per the mob-1130 finding below, reliably kills the target
before the splash loop (1959-1991) even starts -- stop_fighting_him() (handler.cpp, called from
extract_char/die's path) clears Harnfighter's own specials.fighting the instant its opponent dies,
so the guard sees "not fighting" and lets a fighter-only splash pair it with the caster instead, in
full mutual PvP. Confirmed empirically (task report): left that way, Harnfighter can never
re-issue `kill target` on a later attempt (do_hit's "You already have a fight to worry about.",
act_offe.cpp:136, since it is still fighting someone else); `_free_fighter_if_stuck` recovers it
every attempt before the next `kill target` (see its own docstring). No gtest can drive this: it
needs the server's own splash-roll RNG against a live room of loaded mobs and two real player
sessions, read back through the transcripts and on-disk records the running server actually
emits. Kill credit for a
mob death is observable only through group_gain()'s share line (test_kill_credit.py's module
docstring), and die() returns through raw_kill() for an NPC before any add_exploit_record call
(gotchas.md, "No exploit record is written for a mob's death"), so both deaths below are checked
the same way: a share line on the credited player's transcript, and an empty exploits list for
both Harncaller and Harnfighter throughout.

Mob 1130 (target orc, keyword "target") cannot be kept alive across repeated fireball casts the
way the blaze scenarios floor a target's hit: its fixed hit-point ceiling (30, 11.mob's "30 30"
hp_current/hp_max line, loaded verbatim into mob_proto[i].abilities.hit with no level-based
scaling, db.cpp load_mobiles ~1739-1740) sits below fireball's own damage floor -- the primary
hit's raw damage is never less than 30 before a save (mage.cpp:1919's unconditional "30 +" term),
and scale_spell_damage (mage.cpp:131-141) scales it up, not down, against a plain NPC victim with
no player-level saving-throw bonus; wizset's own current-hit clamp (act_wiz.cpp's `case 7`,
`RANGE(-9, vict->abilities.hit)`) cannot lift a mob's ceiling past that fixed prototype value
either way, so no `wizset` value can keep a fresh target alive against it. Confirmed empirically
in a kept run (task report). So the loop below reloads (purge, then `load mob 1130`) a fresh
target before every attempt instead of trying to floor its hit, and re-engages the fighter with
each fresh copy before every cast; the target is never asserted still fighting Harnfighter after
a cast, since the primary hit usually kills it within that same cast. Only the two bystander orcs
(mob 1132, 60 max hit each) need re-flooring, since splash's reduced damage (fireball_damage
divided by 5 before a bystander is itself engaged, by 3 after, mage.cpp:1973-1977) stays
comfortably under that ceiling regardless of the value wizset is asked for -- SAFE_HIT is set far
above it purely so the clamp, not the requested value, decides the outcome (the one fact this
docstring states about that clamp).

Addressing the two loaded bystanders individually needs the "N.keyword" numbered form:
get_char_room_vis (handler.cpp ~2180-2204, and get_char_vis's own room-first check at
~2217-2226) parses that prefix and searches only the room's own occupant list before any global
fallback; `do_hit` (act_offe.cpp:82, the `kill` command) resolves victims the same way, so the
fighter's kills below address a specific loaded copy too. Confirmed empirically in a kept run
(task report). Because imp, caller and fighter never leave Arena West (room 1130) for this whole
test, every room-scoped lookup here also stays clear of zone 11's own periodic respawn of mob
1130 into Arena East (11.zon's "M 0 1130 1132" reset command); mob 1132 carries no such reset
entry, so the two bystanders are never duplicated by the zone itself.

Once a splash lands, the engaged caster keeps swinging at the bystander every violence pulse like
any other combatant (ordinary combat continuance, not a fireball-specific mechanic); left alone,
that melee reliably kills the low-hit bystander before the fighter's own deliberate kill at the
end, crediting the caster instead of the fighter. Confirmed empirically in a kept run (task
report). `combat_support.neutralize_melee` floors the caster's OB/damage once splash is confirmed
so the fighter's kill is what actually lands. The two `fighter.expect([SHARE_MARKER], ...)` calls
below are liveness checks that a share line arrived, not proof of who specifically was credited:
per gotchas.md the share line reaches "anyone still fighting the mob" at the death instant, so
Harncaller (still engaged with the target through the fireball that kills it, and again with the
bystander it is itself fighting by the second kill) receives one too; `expect()` already
guarantees the marker is present in what it returns, so no separate `in` assertion follows either
call.

Bound: two bystanders independently at 0.2 each give a single cast at least a
1 - 0.8**2 = 0.36 chance of landing a splash on one of them; SPLASH_ATTEMPTS casts leave an
all-miss chance of 0.64**15, about 0.12%, reported as a failed precondition rather than a bare
loop timeout. This is a plain Bernoulli-confidence bound, not a resource one -- there is no
affect duration or hit-point budget the loop itself could exhaust, since the target is reloaded
fresh and the bystanders are re-floored every attempt. A cast that splashes only Harnfighter (not
a bystander) does not break the loop -- see `splashed_bystander`'s selection below -- so it spends
an attempt without counting toward this bound's "at least one splash" event; the bound is
therefore conservative, not violated, by that case.
"""

from __future__ import annotations

import pytest

from blaze_support import LETHAL_HIT, floor_hit
from combat_support import neutralize_melee, stat_replies, wait_for_disengagement, wait_for_engagement
from rots_harness import fixtures, records
from rots_harness.session import GameSession

pytestmark = pytest.mark.scenario

# lib/misc/messages, attack type 96 (the primary hit) -- caster-side lines only:
PRIMARY_DEATH = "You grin as your fireball burns"  # :255, the kill case (mob 1130's low ceiling makes this common)
PRIMARY_HIT = "You smirk as your fireball explodes in the face of"  # :261, a non-lethal hit
PRIMARY_MISS = "Your fireball burns out before it reaches"  # :258
# lib/misc/messages, attack type 201 (the splash) -- caster-side hit line only, :485. Its own
# miss line (:482) is the same wording as PRIMARY_MISS above; a successful save instead prints a
# separate, hard-coded "$N dodges off to the side, avoiding part of the blast!" line
# (mage.cpp:1984-1985), not a messages-file pair at all. Its death line (:479) is never needed
# since the bystanders' fixed ceiling keeps a splash hit from being lethal.
SPLASH_HIT = "The heat of your fireball burns"
RESOLUTION_MARKERS = (PRIMARY_DEATH, PRIMARY_HIT, PRIMARY_MISS, SPLASH_HIT)

SPLASH_ATTEMPTS = 15
SAFE_HIT = 400  # clamped down to each mob's own ceiling by wizset's RANGE(-9, abilities.hit); see module docstring
BYSTANDERS = ("1.bystander", "2.bystander")
ORC_DEATH_MARKER = "A target orc is dead"
SHARE_MARKER = "You receive your share of experience"

# Reply markers for imp commands issued once a fight may already be running in imp's own room
# (from the point a splash first lands onward): see gotchas.md's Timing entry (CI run 35655894942,
# build/integration/4ba60d7e87b2). Each is sent with `_imp_do` below instead of `command()`, whose
# bare "any prompt" check a stray combat broadcast can satisfy before the command itself has
# actually executed server-side.
RESTORE_DONE = "Done."  # do_restore, act_wiz.cpp:1617
PURGE_REPLIES = ("Ok.", "I don't know anyone or anything by that name.")  # do_purge: purged / nothing to purge
LOAD_TARGET_CREATED = "You create a target orc."  # do_load, act_wiz.cpp:1326
WIZSET_HIT_REPLY = "'s hit set to"  # do_wizset's generic NUMBER-field reply, act_wiz.cpp:3111-3113 -- the
# clamped value varies per mob/command, so only this substring is stable
WIZSET_ROOM_REPLY = "'s room set to"  # do_wizset field 36, act_wiz.cpp:2954-2963 (also a NUMBER field)


def _imp_do(imp: GameSession, command: str, markers: tuple[str, ...]) -> None:
    """send_line + expect on `command`'s own reply marker, not `command()`. See the module-level
    comment above `RESTORE_DONE` for why: `command()` only waits for the next prompt, and any
    prompt will do, so it can return before `command` has actually executed.
    """
    imp.send_line(command)
    imp.expect(markers, timeout=8.0)


def _fighting_line(imp: GameSession, target: str) -> str:
    replies = stat_replies(imp, target, lambda text: "Fighting:" in text)
    line = next((line for line in replies[-1].splitlines() if "Fighting:" in line), None)
    if line is None:
        pytest.fail(f"no 'Fighting:' line in stat reply for {target!r}: {replies[-1]!r}")
    return line


def _reload_target(imp: GameSession) -> None:
    """Replaces whatever is left of mob 1130 in Arena West with a fresh, full-health copy --
    see the module docstring for why a floored/reset current hit cannot keep it alive across
    the splash-detection loop the way it can for the bystanders.
    """
    _imp_do(imp, "purge target", PURGE_REPLIES)
    _imp_do(imp, "load mob 1130", (LOAD_TARGET_CREATED,))


def _free_fighter_if_stuck(imp: GameSession) -> None:
    """Recovers Harnfighter from a fighter-only splash (module docstring) by moving it to a
    different room and back. char_from_room/char_to_room (the `wizset ... room` teleport used
    here) never touch specials.fighting; disengagement only happens on a later violence pulse's
    room-mismatch check (fight.cpp ~3044, the same mechanism combat_support.wait_for_disengagement
    documents for `transfer`). Harnimp's level (100) meets the `room` field's LEVEL_IMPL
    requirement, so this does not need to physically relocate imp itself the way `transfer` would.
    A no-op when Harnfighter is not stuck.
    """
    if "fighting: nobody" in _fighting_line(imp, "harnfighter").lower():
        return
    _imp_do(imp, f"wizset harnfighter room {fixtures.ROOM_CORRIDOR_ONE}", (WIZSET_ROOM_REPLY,))
    wait_for_disengagement(imp, ("harncaller", "harnfighter"))
    _imp_do(imp, f"wizset harnfighter room {fixtures.ROOM_ARENA_WEST}", (WIZSET_ROOM_REPLY,))


def test_splash_engages_the_bystander_with_the_caster_and_manufactures_no_credit(server, imp, caller, fighter) -> None:
    imp.command(f"goto {fixtures.ROOM_ARENA_WEST}")
    for name in ("harncaller", "harnfighter"):
        imp.command(f"transfer {name}")
        imp.command(f"restore {name}")
    caller.expect_room("Arena West")
    fighter.expect_room("Arena West")

    imp.command("load mob 1132")
    imp.command("load mob 1132")
    imp.command("load mob 1130")  # first load, no purge yet -- _reload_target is for later attempts only

    splashed_bystander: str | None = None
    for _attempt in range(SPLASH_ATTEMPTS):
        _imp_do(imp, "restore harncaller", (RESTORE_DONE,))  # keeps mana up, heals off any fighter-splash PvP
        _imp_do(imp, "restore harnfighter", (RESTORE_DONE,))
        for name in BYSTANDERS:
            _imp_do(imp, f"wizset {name} hit {SAFE_HIT}", (WIZSET_HIT_REPLY,))
        _free_fighter_if_stuck(imp)

        fighter.command("kill target")
        wait_for_engagement(imp, "target", "Harnfighter")

        caller.send_line("cast 'fireball' target")
        reply = caller.expect(RESOLUTION_MARKERS, timeout=12.0)
        if SPLASH_HIT in reply:
            splashed_bystander = next(
                (name for name in BYSTANDERS if "harncaller" in _fighting_line(imp, name).lower()), None
            )
            if splashed_bystander is not None:
                break  # a fighter-only splash (no bystander fighting the caster) spends the attempt instead
        _reload_target(imp)
    assert splashed_bystander is not None, f"no splash landed in {SPLASH_ATTEMPTS} casts (all-miss chance ~0.12%)"

    other_bystander = next(name for name in BYSTANDERS if name != splashed_bystander)
    splashed_line = _fighting_line(imp, splashed_bystander)
    assert "harncaller" in splashed_line.lower() and "harnfighter" not in splashed_line.lower(), splashed_line
    other_line = _fighting_line(imp, other_bystander)
    assert "fighting: nobody" in other_line.lower(), other_line

    neutralize_melee(imp, "harncaller")  # stop it outracing the fighter's kill below; see docstring
    _free_fighter_if_stuck(imp)  # the same breaking cast could have also splashed Harnfighter itself

    _reload_target(imp)
    fighter.send_line("kill target")  # send_line + expect below, not command(): see the same race noted further down
    wait_for_engagement(imp, "target", "Harnfighter")
    floor_hit(imp, "target", LETHAL_HIT)
    fighter.expect([ORC_DEATH_MARKER], timeout=20.0)
    fighter.expect([SHARE_MARKER], timeout=20.0)

    for name in ("Harncaller", "Harnfighter"):
        assert records.read_exploits(server.lib_dir, name) == [], name
    still_fighting = "harncaller" in _fighting_line(imp, splashed_bystander).lower()
    assert still_fighting, "splash engagement must outlive the target's unrelated death"

    _imp_do(imp, f"wizset {splashed_bystander} hit 1", (WIZSET_HIT_REPLY,))
    # send_line + expect, not command(): whether the attack's own acknowledgement and the
    # killing swing land in the same violence pulse or different ones is not fixed (seen both
    # in kept runs), and command() only waits for one prompt -- it can swallow the share line
    # itself, leaving a follow-up expect() nothing to find.
    fighter.send_line(f"kill {splashed_bystander}")
    fighter.expect([SHARE_MARKER], timeout=20.0)

    for name in ("Harncaller", "Harnfighter"):
        assert records.read_exploits(server.lib_dir, name) == [], name
