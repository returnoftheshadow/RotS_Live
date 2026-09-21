"""A fireball's splash engages a bystander mob with the caster alone -- not the fighter already
engaged with the primary target -- and neither death that follows (the primary target's, the
bystander's) manufactures a player-facing kill-credit record. spell_fireball (mage.cpp
1914-1996) delivers its primary hit to the cast's victim, then walks every other room occupant
(1959-1991) and rolls each one independently at target_number 0.2 (0.8 once that occupant is
already fighting the caster), delivering a hit as SPELL_FIREBALL2 through apply_spell_damage --
whose damage()/damage_credited() bookkeeping is what puts a splashed bystander into combat with
the caster, not with whoever the primary victim was fighting. No gtest can drive this: it needs
the server's own splash-roll RNG against a live room of loaded mobs and two real player sessions,
read back through the transcripts and on-disk records the running server actually emits. Kill
credit for a mob death is observable only through group_gain()'s share line (test_kill_credit.py's
module docstring), and die() returns through raw_kill() for an NPC before any add_exploit_record
call (gotchas.md, "No exploit record is written for a mob's death"), so both deaths below are
checked the same way: a share line on the credited player's transcript, and an empty exploits
list for both Harncaller and Harnfighter throughout.

Mob 1130 (target orc, keyword "target") cannot be kept alive across repeated fireball casts the
way the blaze scenarios floor a target's hit: its fixed hit-point ceiling (30, 11.mob's "30 30"
hp_current/hp_max line, loaded verbatim into mob_proto[i].abilities.hit with no level-based
scaling, db.cpp load_mobiles ~1739-1740) sits below fireball's own damage floor -- the primary
hit's raw damage is never less than 30 (mage.cpp:1919's unconditional "30 +" term), and
scale_spell_damage (mage.cpp:131-141) scales it up, not down, against a plain NPC victim with no
player-level saving-throw bonus. wizset's own current-hit clamp (act_wiz.cpp's `case 7`,
`RANGE(-9, vict->abilities.hit)`) cannot lift a mob's ceiling past that fixed prototype value
either way. Confirmed empirically in a kept run: a freshly loaded, full-health target orc died to
the very first fireball cast against it ("You grin as your fireball burns a target orc to
death..."), before any splash question was even in play. So the loop below reloads (purge, then
`load mob 1130`) a fresh target before every attempt instead of trying to floor its hit; only the
two bystander orcs (mob 1132, 60 max hit each) need re-flooring, since splash's reduced damage
(fireball_damage divided by 5 before a bystander is itself engaged, by 3 after, mage.cpp:1973-1977)
stays comfortably under that ceiling regardless of the value wizset is asked for -- SAFE_HIT is
set far above it purely so the clamp, not the requested value, is what decides the outcome.

Addressing the two loaded bystanders individually needs the "N.keyword" numbered form:
get_char_room_vis (handler.cpp ~2180-2204, and get_char_vis's own room-first check at
~2217-2226) parses that prefix and searches only the room's own occupant list before any
global fallback. Confirmed empirically in the same kept run: `wizset 1.bystander hit 1` set
only one of the two loaded copies (its own next `stat 1.bystander` read back hit 1 of 60),
while `stat 2.bystander` read back the untouched 60 of 60 -- see the task report for the
transcript excerpt. Because imp, caller and fighter never leave Arena West (room 1130) for this
whole test, every room-scoped lookup here also stays clear of zone 11's own periodic respawn of
mob 1130 into Arena East (11.zon's "M 0 1130 1132" reset command); mob 1132 carries no such
reset entry, so the two bystanders are never duplicated by the zone itself.

Once a splash lands, the engaged caster keeps swinging at the bystander every violence pulse
like any other combatant (ordinary combat continuance, not a fireball-specific mechanic); left
alone, that melee reliably kills the low-hit bystander before the fighter's own deliberate kill
at the end, crediting the caster instead of the fighter -- confirmed empirically in a kept run
("Harncaller barely hits a bystander orc's body.  A bystander orc is dead!" landing one pulse
ahead of the fighter's own queued `kill`). `combat_support.neutralize_melee` floors the caster's
OB/damage once splash is confirmed so the fighter's kill is what actually lands.

Bound: two bystanders independently at 0.2 each give a single cast at least a
1 - 0.8**2 = 0.36 chance of landing a splash on one of them; SPLASH_ATTEMPTS casts leave an
all-miss chance of 0.64**15, about 0.12%, reported as a failed precondition rather than a bare
loop timeout. This is a plain Bernoulli-confidence bound, not a resource one -- there is no
affect duration or hit-point budget the loop itself could exhaust, since the target is reloaded
fresh and the bystanders are re-floored every attempt.
"""

from __future__ import annotations

import pytest

from blaze_support import LETHAL_HIT, floor_hit
from combat_support import neutralize_melee, stat_replies, wait_for_engagement
from rots_harness import fixtures, records
from rots_harness.session import GameSession

pytestmark = pytest.mark.scenario

# lib/misc/messages, attack type 96 (the primary hit) -- caster-side lines only:
PRIMARY_DEATH = "You grin as your fireball burns"  # :255, the kill case (mob 1130's low ceiling makes this common)
PRIMARY_HIT = "You smirk as your fireball explodes in the face of"  # :261, a non-lethal hit
PRIMARY_MISS = "Your fireball burns out before it reaches"  # :258 -- same wording as the splash-saved line at :482
# lib/misc/messages, attack type 201 (the splash) -- caster-side hit line only, :485; its
# miss (:482) is the same text as PRIMARY_MISS above, and its death line (:479) is never
# needed since the bystanders' fixed ceiling keeps a splash hit from being lethal.
SPLASH_HIT = "The heat of your fireball burns"
RESOLUTION_MARKERS = (PRIMARY_DEATH, PRIMARY_HIT, PRIMARY_MISS, SPLASH_HIT)

SPLASH_ATTEMPTS = 15
SAFE_HIT = 400  # clamped down to each mob's own ceiling by wizset's RANGE(-9, abilities.hit); see module docstring
BYSTANDERS = ("1.bystander", "2.bystander")
ORC_DEATH_MARKER = "A target orc is dead"
SHARE_MARKER = "You receive your share of experience"


def _fighting_line(imp: GameSession, target: str) -> str:
    replies = stat_replies(imp, target, lambda text: "Fighting:" in text)
    return next(line for line in replies[-1].splitlines() if "Fighting:" in line)


def _reload_target(imp: GameSession) -> None:
    """Replaces whatever is left of mob 1130 in Arena West with a fresh, full-health copy --
    see the module docstring for why a floored/reset current hit cannot keep it alive across
    the splash-detection loop the way it can for the bystanders.
    """
    imp.command("purge target")
    imp.command("load mob 1130")


def test_splash_engages_the_bystander_with_the_caster_and_manufactures_no_credit(server, imp, caller, fighter) -> None:
    imp.command(f"goto {fixtures.ROOM_ARENA_WEST}")
    for name in ("harncaller", "harnfighter"):
        imp.command(f"transfer {name}")
        imp.command(f"restore {name}")
    caller.expect_room("Arena West")
    fighter.expect_room("Arena West")

    imp.command("load mob 1132")
    imp.command("load mob 1132")
    _reload_target(imp)

    splashed_bystander: str | None = None
    for _attempt in range(SPLASH_ATTEMPTS):
        imp.command("restore harncaller")  # keeps mana from running out over repeated casts
        for name in BYSTANDERS:
            imp.command(f"wizset {name} hit {SAFE_HIT}")

        caller.send_line("cast 'fireball' target")
        reply = caller.expect(RESOLUTION_MARKERS, timeout=12.0)
        if SPLASH_HIT in reply:
            splashed_bystander = next(name for name in BYSTANDERS if "harncaller" in _fighting_line(imp, name).lower())
            break
        _reload_target(imp)
    assert splashed_bystander is not None, f"no splash landed in {SPLASH_ATTEMPTS} casts (all-miss chance ~0.12%)"
    neutralize_melee(imp, "harncaller")  # stop it outracing the fighter's kill below; see docstring

    _reload_target(imp)
    fighter.send_line("kill target")  # send_line + expect below, not command(): see the same race noted further down
    wait_for_engagement(imp, "target", "Harnfighter")
    floor_hit(imp, "target", LETHAL_HIT)
    fighter.expect([ORC_DEATH_MARKER], timeout=20.0)

    for name in ("Harncaller", "Harnfighter"):
        assert records.read_exploits(server.lib_dir, name) == [], name
    still_fighting = "harncaller" in _fighting_line(imp, splashed_bystander).lower()
    assert still_fighting, "splash engagement must outlive the target's unrelated death"

    imp.command(f"wizset {splashed_bystander} hit 1")
    # send_line + expect, not command(): whether the attack's own acknowledgement and the
    # killing swing land in the same violence pulse or different ones is not fixed (seen both
    # in kept runs), and command() only waits for one prompt -- it can swallow the share line
    # itself, leaving a follow-up expect() nothing to find.
    fighter.send_line(f"kill {splashed_bystander}")
    share = fighter.expect([SHARE_MARKER], timeout=20.0)
    assert SHARE_MARKER in share

    for name in ("Harncaller", "Harnfighter"):
        assert records.read_exploits(server.lib_dir, name) == [], name
