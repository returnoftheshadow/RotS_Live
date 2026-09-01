Here's the in-game verification plan, grounded in what the branch actually changed. All of this happens on **4810 after a reboot** — the new binary is deployed but the running process is still the old image, so step zero is an implementor `shutdown reboot` there, then confirm the process start time is newer than the deploy (Aug 31 21:30).

While testing, keep a second terminal tailing the 4810 syslog/log dir — the autorun loop wraps crashes in a gdb backtrace, so any UAF regression shows up there even if the game just restarts silently.

## The scenarios, mapped to each fix

**1. Room-affect tick after the caster is gone (the core UAF — blaze/mist/haze):**
- Cast blaze (or mist/haze) in a room with a weak NPC, then have the caster **die** before the next tick (second character or a hostile mob does it). Pre-fix this was the crash/corruption path. Expected: the tick still fires, damage uses the caster's *cast-time* stats, and if it kills the NPC, the kill is recorded to the original caster — check `exploits` on the victim.
- Repeat with the caster **quitting/renting** instead of dying.
- Repeat with the caster dying and a **different character logging in immediately** — the identity registry exists precisely so the tick can't attribute to whoever now occupies that memory. You can't force memory reuse deterministically, but any misattribution here is an instant fail.

**2. Poison attribution:**
- Poison a victim, then log the poisoner out (and separately: kill the poisoner). Let the victim later die *of the poison tick*. Expected: no crash, an `EXPLOIT_POISON` record on the victim, and kill credit to the original poisoner where still resolvable — with a safe/anonymous record when they're gone, not garbage or a wrong name.
- Control case: poisoner stays online → normal credit and XP.

**3. Kill credit separation (`damage_credited` / die-records-participation):**
- Group fight where the killing blow comes from someone who wasn't the mob's current fight target (e.g., mob is tanking A, B's spell lands the kill). Expected: the kill is recorded and XP shares go to participants — pre-fix, kills could go unrecorded when the killer wasn't the engaged opponent.
- Splash-damage bystander: fireball a fight where an uninvolved NPC catches splash. The bystander being dragged into combat must **not** manufacture player-kill credit for or against it later.
- Remote-credit XP split: while two characters fight a victim, a third drops blaze (or poison) and leaves the room; the tick lands the kill. Expected: the two engaged characters each see "You receive your share of experience", the remote caster gets no XP but appears in the death records. Repeat with the caster standing in the room, unengaged — they join the XP split.

**4. Summon (two changes):**
- In a **dark room**, `cast summon <name>` — targeting by name now works there (`TAR_DARK_OK`); pre-fix it couldn't find the target.
- Test summon at increasing zone distances (same zone, adjacent, far). The distance penalty was accidentally XOR-ing the zone distance; it now squares it — so expect a *sensible monotonic falloff*. This is a genuine behavior change: success rates at distance will differ from old live, and players may notice.
- Also try summoning a linkdead player — that path was pinned by tests and should behave, not crash.

**5. Earthquake ordering:** cast quake in a crowded room where the caster also falls. Everyone else falls **before** the caster's own fall (watch the message order) — the caster's fall no longer interrupts processing the other occupants.

**6. Mass affect expiry (background check, not a targeted test):** during your session let buffs/affects expire while characters die or quit in the same tick window — `affect_update` now walks a snapshot, so no crash is the pass condition.

## Expected behavior changes, in detail

These are intended changes, not regressions. Each one states what the old code did, what the
new code does, and a concrete example of the difference a tester will see.

### 1. Room-affect ticks now use the caster's stats, not the victim's

**Before:** when blaze/mist/haze/poison ticked on a room occupant, the code *re-cast the spell
with the occupant standing in as its own caster*. Every formula input — caster level, save DC,
spell penetration — came from the victim, not from whoever cast the affect.

**After:** each `(room, spell)` affect records a `caster_snapshot` at cast time (level, prof
levels, int/wil, spell power/pen, tactics, specialization, race). Ticks compute damage and
saves from that snapshot.

**Example:** a level-30 mage drops blaze in a room; a level-5 character walks in. Old code: the
tick's save DC and damage scaled off the *level-5 victim's own* stats — weak characters faced
weak blazes, strong characters faced strong ones, regardless of who cast it. New code: the tick
hits with level-30-mage numbers for everyone. Low-level characters will find high-level casters'
room affects noticeably more dangerous than before; a powerful character walking through a
novice's blaze will find it weaker.

**Corollary — frozen at cast time:** if the caster levels, re-specs, or changes gear while the
affect burns, ticks keep using the cast-time values. Re-casting is the only way to refresh them.

### 2. Lethal room-affect ticks now credit the caster (before: nobody)

**Before:** a room-affect tick that killed someone credited *nobody at all* — no XP, no exploit
attribution to the caster.

**After:** the recorded caster gets the kill credit; `exploits` on the victim names them. If the
caster is gone (dead, quit, extracted), the kill safely credits nobody — never a wrong
character, even if a new login now occupies the old caster's memory (identity is validated
through an abs-number registry, not the raw pointer).

**Example:** cast blaze, walk two rooms away, and let the tick finish a wounded orc. Old:
orc dies uncredited. New: your character appears as the killer in the orc's death record, and
you get the kill even though you never engaged it.

**XP on remote-credited kills:** credit and XP are separate. The remote caster gets the kill
*record* but never XP — presence in the death room is still required for a share. Anyone
engaged with the victim in the death room (and their same-room groupmates) splits the XP
exactly as before; a caster standing *in* the room when the tick kills (even unengaged) joins
the split. Verify both sides: the engaged fighter sees "You receive your share of experience",
the remote caster sees nothing but appears in the death records.

### 3. A ticking room affect no longer drags its caster into combat

The *engaged* attacker for a tick is the occupant itself; only the *credit* points at the
caster. **Example:** your blaze ticks a mob that your group-mate is tanking, or even ticks a
group-mate — you stay resting/regen-ing; you are not yanked into fighting state, and the room
affect can never start a caster-vs-groupmate fight.

### 4. Poison deaths are attributed to the poisoner (before: to the victim, or nothing)

**Before:** the poison tick in `point_update` damaged the victim *as their own attacker* — a
poison death was effectively self-inflicted for credit purposes. Worse, `die()` had an early
return for `SPELL_POISON && !fighting`: a victim who died of poison while *not in combat*
generated **no kill records at all** — a poisoner could kill with zero paper trail by waiting.

**After:** `resolve_poisoner()` reads the recorded poison origin, and the contributor list
(fighters + resolved poisoner + primary killer, deduped) drives all record-building — the
"was the victim in combat?" question is gone.

**Example (the one PK players will notice):** poison an enemy player, then leave the zone. They
die of the poison alone, out of combat, minutes later. Old: no PK record, no attribution. New:
a kill record exists and names the poisoner — even though the poisoner was rooms away and never
in the victim's fight. If the poisoner has since logged off or died, the record degrades safely
(poison-death record without a live killer) instead of naming someone wrong. XP follows the
same room rule as blaze above: the remote poisoner gets the record but no XP, while anyone
fighting the victim in the death room still splits the XP normally.

### 5. PK weight/opponent records now agree with each other and include remote contributors

**Before:** the three PK record walks (`pkill_weight`, `pkill_opponents`, the pkill-table
update) each independently re-derived "who was involved" from the victim's live combat list —
which structurally *cannot see* a poisoner standing rooms away or a room-affect caster. The
three walks could disagree.

**After:** `die()` builds one deduped `kill_contributor_list` and threads it through all three,
so weight, opponent count, and records always describe the same set of people.

**Example:** two players melee a victim while a third's poison lands the killing tick. Old: PK
weight and opponent records counted only the two in `combat_list`. New: all three appear, and
the kill's weight is split across all three. Expect PK ledger entries to name more participants
than the old code did — that's the fix working, not inflation.

### 6. Kill-credit fallback never invents a killer

`damage()` was split into engagement vs. credit (`damage_credited()`). When a death has no
explicit credited killer, credit falls back to whoever the victim was fighting at that moment —
and if the victim was fighting *nobody*, no killer is invented. **Example:** a mob dies to fall
damage or an environmental tick while idle: the death records show no killer rather than
whichever character happened to be nearby.

### 7. Summon: the save-distance math is fixed (this changes success rates)

**Before:** the victim's save bonus used `dist = ((ch_x - v_x) ^ 2) + ((ch_y - v_y) ^ 2)` — but
`^` is bitwise XOR in C++, not exponentiation. A **same-zone** summon (delta 0,0) computed
`(0^2)+(0^2) = 2+2 = 4`: a phantom +4 save bonus that let victims resist even a same-zone
summon more easily than intended. Long-range deltas produced
arithmetically meaningless values: delta (3,4) gave `(3^2)+(4^2) = 1+6 = 7` where true squaring
gives `9+16 = 25`.

**After:** real squared Euclidean distance: `(dx*dx) + (dy*dy)`.

**Example:** summoning a willing friend standing in the *same zone* now carries **no** distance
save bonus (was +4) — same-zone summons land more reliably than on old live. Summoning across
the map now gives the victim a genuinely large, proportionate save bonus (25 vs the old 7 in
the delta-(3,4) example) — long-range summons of unwilling targets fail more often. Players
will perceive summon as "better up close, worse cross-map."

### 8. Summon works in dark rooms (new capability)

`summon` gained `TAR_DARK_OK` in its target mask (matching `tell`'s precedent). **Before:**
`cast summon <name>` at a target standing in an unlit room refused to find them. **After:** the
name resolves and the summon proceeds normally — darkness at the victim's end no longer blocks
targeting. Everything else about the spell's checks is unchanged.

### 9. Earthquake: the caster's own fall happens last (message order changes)

**Before:** the caster could fall into the crevice mid-loop; if that fall was lethal, the rest
of the occupant loop ran against a freed caster (crash class). **After:** every other
occupant's fall resolves first; the caster's own fall is the spell's final act. The landing
saves are still rolled at the original points in the loop, so **RNG outcomes are identical** —
only the order of the fall messages changes: testers will see the caster's fall reported last
where it may previously have appeared mid-sequence.

### 10. Drifting mist no longer corrupts memory (latent pre-existing bug, fixed in passing)

The mist-move arm freed the room-affect entry and then read it through a stale pointer on
*every* mist that drifted between rooms. The new code snapshots the recorded caster before the
removal and carries it to the destination room. **Observable change:** a mist that drifts
retains its original caster's stats and credit in the new room (and the server stops rolling
dice on freed memory every time weather moves a mist).

### 11. Affect expiry is crash-safe under mid-tick mutation

`affect_update` now walks a snapshot of the affected list and validates each entry by identity
before touching it. No gameplay-visible change is expected — this is purely "many affects
expiring while characters die/quit in the same pulse no longer risks a crash." Any behavioral
difference observed here (an affect skipped or double-ticked) is a bug, not an intended change.
