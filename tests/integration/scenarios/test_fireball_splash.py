"""A fireball's splash engages a bystander mob with the caster and with nobody else: the splashed
bystander fights Harncaller, never the melee partner that is fighting the primary target, and no
player record is manufactured for or against either player. spell_fireball (mage.cpp:1914-1996)
delivers the primary hit to the cast's own victim, then walks every other occupant of the room
(1959-1991) and rolls each one independently at target_number 0.2 (0.8 once that occupant already
fights the caster), delivering SPELL_FIREBALL2 through apply_spell_damage (mage.cpp:1989); it is
damage_credited's set_fighting pair (fight.cpp:1923 and :1940) that puts a splashed bystander into
combat with the caster. No gtest can pin this: it needs the server's own splash roll against a
live room of loaded mobs, two real player sessions, and the records the running server writes to
disk.

The melee partner is Harnvictim (level 10), not Harnfighter (level 20), because Big Brother makes
a level-10 partner an impossible splash victim and so removes the caster-versus-partner PvP branch
this scenario used to have to recover from. damage_credited consults
big_brother::is_target_valid before anything else (fight.cpp:1847-1849) and refuses the hit with
"...protecting your target.  Your hand is stayed." without reaching either set_fighting call;
is_target_valid rejects a PC victim whose is_level_range_appropriate fails
(big_brother.cpp:315-316), and that check fails when attacker_level >= defender_level * 3
(big_brother.cpp:381-394) -- exactly Harncaller's 30 against Harnvictim's 10, both read through
get_level_legend_cap, which is get_level_a and caps a PC at LEVEL_MAX 30 (char_utils.cpp:307-318).
SPELL_FIREBALL2 is in the harmful-skill set (big_brother.cpp:74), so the skill-aware overload of
is_target_valid rejects it too rather than letting it through as potentially helpful. Harnfighter's
20 sits inside the permitted band (30 < 60), which is why it was a legal splash victim. Harnimp,
standing in the room to run the staging, is spared by the same guard's earlier god branch
(big_brother.cpp:303), so the two bystander orcs are the only occupants a splash can engage at
all -- and a "protecting your target" line on the caster's transcript does not by itself say
which of the two protected players it spared.

Every cast kills the primary target, so the partner's own share line is this scenario's per-cast
credit observation. Mob 1130 (target orc, keyword "target") has a fixed 30/30 hit-point ceiling
(11.mob, loaded verbatim into mob_proto[i].abilities.hit with no level scaling, db.cpp
load_mobiles ~1739-1740) that wizset cannot raise (SAFE_HIT below states the clamp); the primary
hit's raw damage is never less than 30 before a save (mage.cpp:1919's unconditional "30 +" term)
and scale_spell_damage (mage.cpp:131-141) scales it up, not down, against a plain NPC with no
player-level saving-throw bonus; and the orc cannot save, since new_saves_spell (spell_pa.cpp:245-266)
needs number(1, 20) plus its own save value of 1 (get_character_saving_throw, spell_pa.cpp:203-220:
an NPC's mage "profession level" is its own level 5, two thirds of it, a third of that; intel 10
adds nothing) to exceed Harncaller's DC of 22 (get_saving_throw_dc, spell_pa.cpp:227-233: 10 plus a
third of mage level 30 plus a quarter of intel 18 above 8, with no specialization bonus) -- 21 at
the very best roll. A cast that nonetheless leaves the target standing is reported as a failed
precondition rather than left to time out on the partner's missing share line.

Addressing the two loaded bystanders individually needs the "N.keyword" numbered form:
get_char_room_vis (handler.cpp ~2180-2204, and get_char_vis's own room-first check at ~2217-2226)
parses that prefix and searches only the room's own occupant list before any global fallback.
Confirmed empirically in a kept run (task report). Because imp, caller and partner never leave
Arena West (room 1130) for this whole test, every room-scoped lookup here also stays clear of zone
11's own periodic respawn of mob 1130 into Arena East (11.zon's "M 0 1130 1132" reset command);
mob 1132 carries no such reset entry, so the two bystanders are never duplicated by the zone
itself. Only the bystanders are re-floored between attempts, since splash's reduced damage
(fireball_damage divided by 5 before a bystander is itself engaged, by 3 after, mage.cpp:1972-1977)
stays comfortably under their 60-point ceiling; the target is reloaded instead, because no wizset
value can keep a fresh copy of it alive against the paragraph above.

A splash is detected by reading both bystanders' own stat lines after every resolved cast, not
from the caster's transcript order: expect() returns as soon as any one marker is seen, so it can
return on the primary line before a same-cast splash line has even arrived on the socket.

Bound: two bystanders rolling independently at 0.2 each give a single cast at least a
1 - 0.8**2 = 0.36 chance of landing a splash on one of them, so SPLASH_ATTEMPTS casts leave an
all-miss chance of 0.64**15, about 0.12%, reported as a failed precondition rather than a bare
loop timeout. This is a plain Bernoulli-confidence bound, not a resource one: the target is
reloaded fresh, the bystanders are re-floored and both players restored on every attempt, so
nothing the loop consumes can run out first. The harness pins the server's RNG seed
(conftest.py's DEFAULT_SEED, applied by test_harness.cpp's seed_random_from_environment), so
which cast splashes is reproducible only up to how many number() calls the real-time combat
pulses have consumed by then; the bound is what keeps the loop honest when that ordering shifts,
as it does between seeds.

The two records assertions are what "manufactures no credit" means on disk. No exploit record is
written for a mob's death at all -- die() returns through raw_kill() for an NPC before any
add_exploit_record call (gotchas.md) -- so a record appearing for either player would have to have
been manufactured by the splash's own bookkeeping. Both bystanders are purged before the test
returns, so that nothing is still swinging at the caster while the session fixtures tear the
sessions down; the SPELL_ANGER those swings left behind can still refuse a `quit` (gotchas.md),
which the fixtures already handle.
"""

from __future__ import annotations

import pytest

from combat_support import stat_replies, wait_for_engagement
from rots_harness import fixtures, records
from rots_harness.session import GameSession

pytestmark = pytest.mark.scenario

# lib/misc/messages, attack type 96 (the primary hit) -- caster-side lines only:
PRIMARY_DEATH = "You grin as your fireball burns"  # :255, the kill case, and the only one this test expects
PRIMARY_HIT = "You smirk as your fireball explodes in the face of"  # :261, a non-lethal hit
PRIMARY_MISS = "Your fireball burns out before it reaches"  # :258
# lib/misc/messages, attack type 201 (the splash) -- caster-side hit line only, :485. Its own miss
# line (:482) is the same wording as PRIMARY_MISS above; a successful save instead prints a
# separate, hard-coded "$N dodges off to the side, avoiding part of the blast!" line
# (mage.cpp:1984-1985), not a messages-file pair at all. Its death line (:479) is never needed,
# since the bystanders' fixed ceiling keeps a splash hit from being lethal.
SPLASH_HIT = "The heat of your fireball burns"
# PRIMARY_HIT and PRIMARY_MISS are here so a cast that did not kill still ends the wait promptly
# and fails on the precondition assertion below rather than on this timeout.
RESOLUTION_MARKERS = (PRIMARY_DEATH, PRIMARY_HIT, PRIMARY_MISS, SPLASH_HIT)

MELEE_PARTNER = "Harnvictim"
SPLASH_ATTEMPTS = 15
# Any value at or above a mob's own hp ceiling clamps down to that ceiling, never up: wizset's
# `case 7` does `tmpabilities.hit = RANGE(-9, vict->abilities.hit)` (act_wiz.cpp:2826-2827; RANGE
# itself at :2586), so the mob's own ceiling decides the outcome here, not the requested value.
SAFE_HIT = 400
BYSTANDERS = ("1.bystander", "2.bystander")
SHARE_MARKER = "You receive your share of experience"

# Reply markers for imp commands issued once a fight may already be running in imp's own room
# (from the partner's first `kill target` onward): see gotchas.md's Timing entry (CI run
# 35655894942, build/integration/4ba60d7e87b2). Each is sent with `_imp_do` below instead of
# `command()`, whose bare "any prompt" check a stray combat broadcast can satisfy before the
# command itself has actually executed server-side.
RESTORE_DONE = "Done."  # do_restore, act_wiz.cpp:1617
PURGE_REPLIES = ("Ok.", "I don't know anyone or anything by that name.")  # do_purge, act_wiz.cpp:1460 and :1464
LOAD_TARGET_CREATED = "You create a target orc."  # do_load's TO_CHAR line, act_wiz.cpp:1326
WIZSET_HIT_REPLY = "'s hit set to"  # do_wizset's generic NUMBER-field reply, act_wiz.cpp:3113-3115 -- the
# clamped value varies per mob and command, so only this substring is stable


def _imp_do(imp: GameSession, command: str, markers: tuple[str, ...]) -> None:
    """send_line + expect on `command`'s own reply marker, not `command()`. See the module-level
    comment above `RESTORE_DONE` for why: `command()` only waits for the next prompt, and any
    prompt will do, so it can return before `command` has actually executed. The leading
    `drain(0.1)` matches `command()`'s own (session.py) so a marker already sitting unconsumed
    from an earlier broadcast cannot satisfy `expect` before this command's own reply arrives.
    """
    imp.drain(0.1)
    imp.send_line(command)
    imp.expect(markers, timeout=8.0)


def _fighting_line(imp: GameSession, target: str) -> str:
    replies = stat_replies(imp, target, lambda text: "Fighting:" in text)
    line = next((line for line in replies[-1].splitlines() if "Fighting:" in line), None)
    if line is None:
        pytest.fail(f"no 'Fighting:' line in stat reply for {target!r}: {replies[-1]!r}")
    return line


def _reload_target(imp: GameSession) -> None:
    """Replaces whatever is left of mob 1130 in Arena West with a fresh, full-health copy -- see
    the module docstring for why a floored current hit cannot keep it alive across the
    splash-detection loop the way it can for the bystanders. The purge normally finds nothing,
    since the cast that just resolved killed the previous copy.
    """
    _imp_do(imp, "purge target", PURGE_REPLIES)
    _imp_do(imp, "load mob 1130", (LOAD_TARGET_CREATED,))


def test_splash_engages_the_bystander_with_the_caster_and_manufactures_no_credit(server, imp, caller, victim) -> None:
    imp.command(f"goto {fixtures.ROOM_ARENA_WEST}")
    for name in ("harncaller", MELEE_PARTNER):
        imp.command(f"transfer {name}")
        imp.command(f"restore {name}")
    caller.expect_room("Arena West")
    victim.expect_room("Arena West")

    imp.command("load mob 1132")
    imp.command("load mob 1132")
    imp.command("load mob 1130")  # first load, no purge yet -- _reload_target is for later attempts only

    splashed_bystander: str | None = None
    for _attempt in range(SPLASH_ATTEMPTS):
        _imp_do(imp, "restore harncaller", (RESTORE_DONE,))  # keeps the caster's mana up for the next cast
        _imp_do(imp, f"restore {MELEE_PARTNER}", (RESTORE_DONE,))  # heals off the target orc's melee
        for name in BYSTANDERS:
            _imp_do(imp, f"wizset {name} hit {SAFE_HIT}", (WIZSET_HIT_REPLY,))

        victim.command("kill target")
        wait_for_engagement(imp, "target", MELEE_PARTNER)

        caller.send_line("cast 'fireball' target")
        resolved = caller.expect(RESOLUTION_MARKERS, timeout=12.0)
        assert PRIMARY_DEATH in resolved, f"the primary hit left the target orc standing:\n{resolved[-800:]}"
        # The partner was engaged with the target and present when the fireball killed it, so
        # group_gain sends it a share line. Consumed on every attempt, so that no earlier
        # attempt's line can be what a later expect() finds.
        victim.expect([SHARE_MARKER], timeout=20.0)

        splashed_bystander = next(
            (name for name in BYSTANDERS if "harncaller" in _fighting_line(imp, name).lower()), None
        )
        if splashed_bystander is not None:
            break
        _reload_target(imp)
    assert splashed_bystander is not None, f"no splash landed in {SPLASH_ATTEMPTS} casts (all-miss chance ~0.12%)"

    partner_key = MELEE_PARTNER.lower()
    splashed_line = _fighting_line(imp, splashed_bystander)
    assert "harncaller" in splashed_line.lower() and partner_key not in splashed_line.lower(), splashed_line
    other_bystander = next(name for name in BYSTANDERS if name != splashed_bystander)
    # Not "Fighting: Nobody": both bystanders roll independently at 0.2, so the same cast can
    # splash both (~11% of the casts that break the loop). The invariant is that a splash never
    # pairs a bystander with the melee partner, not that only one bystander is ever engaged.
    other_line = _fighting_line(imp, other_bystander)
    assert partner_key not in other_line.lower(), other_line
    partner_line = _fighting_line(imp, MELEE_PARTNER)
    assert "harncaller" not in partner_line.lower(), partner_line

    for name in ("Harncaller", MELEE_PARTNER):
        assert records.read_exploits(server.lib_dir, name) == [], name

    # Ends the caster's fight (see the module docstring's last paragraph). Purged by the bare
    # keyword, not the numbered form: once the first copy is gone the survivor is "1.bystander"
    # again, so a second `purge 2.bystander` would find nothing and leave the caster in combat.
    for _bystander in BYSTANDERS:
        _imp_do(imp, "purge bystander", PURGE_REPLIES)
