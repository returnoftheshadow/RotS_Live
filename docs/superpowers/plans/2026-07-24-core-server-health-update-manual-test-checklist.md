# Core Server-Health Update — Manual Test Checklist

Companion to `2026-07-24-core-server-health-update.md`. All 11 tasks (+1 task-level fix round +1
final-review fix wave, 14 commits total) are merged into local `release-frodo`. Nothing has been
manually tested yet as of this checklist's creation — this tracks that testing pass.

Ordered roughly by priority (highest-risk / most-likely-to-show-a-regression first).

## High priority — new/changed behavior with real risk

### 1. MSDP ROOM table going live for the first time (Tasks 1, 2, final-review fix #4)

Note: general MSDP (`ROOM_NAME`, `ROOM_VNUM`, `HEALTH`, etc. via the per-pulse `msdp_update()`
sweep) has been working fine for a while already — that part was already fixed upstream before
this bundle started. What's actually new here is specifically the fuller **ROOM table**
(`ROOM.EXITS`, `ROOM.TERRAIN`, table-form `NAME`/`VNUM`) from `msdp_room_update()`, which had an
inverted guard and was a no-op until this fix.

- [x] Connect with an MSDP-capable client (Mudlet — `display(msdp.ROOM)`) — **confirmed working
      2026-07-24**, `msdp.ROOM` populates correctly (was a no-op before this fix)
- [x] Walk through several rooms with real exits and confirm `ROOM.EXITS`/`ROOM.TERRAIN` update on
      *every* move — **confirmed via a full protocol-monitoring pass 2026-07-24** (see below), not
      just once
- [x] **Verified 2026-07-24 via a debugger, not natural gameplay** — see correction below, this
      line's earlier wording ("link-dead") was itself inaccurate.
      Correction: ordinary disconnect does **not** null `ch->desc` in this codebase —
      `close_socket()` (`comm.cpp:2035`) has `d->character->desc = 0` commented out on purpose (the
      comment explains the autosave snapshot instead relies on the `CON_PLYNG` state filter), so a
      link-dead character keeps a valid `desc` pointing at its now-`CON_LINKLS` descriptor. The only
      genuine null-desc, non-NPC case in the codebase is `do_switch` (`act_wiz.cpp:1265`,
      `ch->desc = 0` on the immortal's own original body while they possess a mob) — but that body
      is LEVEL_GRGOD, and `spell_summon` explicitly refuses immortal-level victims
      (`mage.cpp` ~line 787), so there's no clean natural-gameplay path combining both conditions
      to test through normal play. Verified directly instead: launched the scratch server as gdb's
      *own child* (`gdb --args ./bin/ageland 1025`, sidesteps the ptrace_scope restriction that
      blocks attaching to an already-running process without root), set a conditional breakpoint on
      `msdp_room_update` (`ch->desc != 0`, to skip an incidental NPC self-update that also hits this
      function with a naturally-null desc — harmless, since NPCs return earlier on the `is_npc()`
      check), stopped at the real login-triggered call for the connected Debugbot character,
      manually zeroed `ch->desc`, called `msdp_room_update(ch)` directly from gdb with that exact
      guarded condition (non-NPC + null desc), confirmed it returned cleanly with no crash, restored
      the real `desc` pointer, and resumed — the connection kept working normally afterward.
- [x] `LIST SENDABLE_VARIABLES`, `LIST COMMANDS`, `SEND SERVER_ID` — **confirmed 2026-07-24**, all
      well-formed; `SEND SERVER_ID` initially came back empty (separate bug, found + fixed, see
      "Bonus fix #2" below), now returns `"Return of the Shadow"` correctly
- [x] Confirmed: most MSDP variables are read-only/server-push. A small configurable subset
      (`CLIENT_VERSION`, `ANSI_COLORS`, `XTERM_256_COLORS`, `UTF_8`) can be set by the client and
      the server actually uses them (color-code formatting decisions) — this is pre-existing
      behavior, not something this bundle touched, just noted for context.
- [x] **Full protocol monitoring pass 2026-07-24** — wrote a real telnet/MSDP protocol validator
      (not just a byte dump): negotiates every option the server offers (TTYPE, NAWS, CHARSET,
      MSDP, MSSP, MXP, ECHO), then structurally validates every MSDP subnegotiation observed —
      `VAR`/`VAL` pairing and `TABLE_OPEN`/`CLOSE`/`ARRAY_OPEN`/`CLOSE` nesting balance — across
      login, `LIST`/`SEND`/`REPORT`/`UNREPORT`, walking 3 rooms, and the periodic sweep. **Zero
      structural findings** — every payload was well-formed. Important gotcha rediscovered: the
      `PRF_MSDP` player preference defaults off per-character (separate from connection-level MSDP
      negotiation) — nothing (not even `ROOM`) is sent until `set msdp on` is issued in-game; this
      is confirmed correct/intentional design, not a bug.

### 2. Scripts — nested if/begin/end (Task 11)

- [ ] Visit vnum **#1140** (`lib/world/scr/11.scr.txt`) — implementer found this has the exact
      nested shape the double-advance bug affected. Watch for script content that used to get
      silently skipped.
- [ ] Spot-check a couple of other scripted rooms/mobs you know well, confirm nothing regressed

### 3. Flee / windblast double-fire (Task 9 + two fix rounds)

- [ ] Flee into a room with a real `ON_BEFORE_ENTER` trigger (message/counter/damage side effect)
      — should fire **once**, not twice
- [ ] Same test **while affected by AFF_HAZE** (dizzy-move effect) — this was the specific gap the
      final review caught: haze re-rolling the flee direction should let the *actual* destination's
      trigger fire normally, not get wrongly suppressed
- [ ] If a ranger with windblast is available, blast someone into a triggered room too
- [ ] Flee while riding a mount (or have a ridden mob flee) — confirm no crash, normal single-fire

(Note: `ON_ENTER` is a *different*, unrelated trigger — fires once from inside `do_move()` after
actually entering a room. It was never part of the double-fire bug; only `ON_BEFORE_ENTER`,
fired inside `check_simple_move()`, was affected.)

## Medium priority — latency/socket fixes, harder to trigger but worth a pass

### 4. Skill cooldowns (Task 8)
- [ ] Use two skills with different cooldown lengths back-to-back; confirm neither takes one tick
      longer than expected to come off cooldown

### 5. Double-delay / bash-interrupts-cast (Task 10)
- [ ] Cast something, get bashed mid-cast; confirm the stun applies correctly
- [ ] If the interrupted spell would normally queue its own follow-up recovery delay, confirm it
      isn't silently dropped — watch the log for a new "double delay (reentrant queue...)" message

### 6. Connection behavior (Tasks 3, 4, 5, 6)
- [ ] Normal play feels the same — login, movement, combat spam, a big `who`/`score` output
- [ ] Idle at the name/password prompt (don't log in); confirm the connection eventually gets
      reaped rather than hanging forever (Task 6 — real timeout is 15 min, slow to verify)
- [ ] If testing through the Rust proxy, connect via both plain TCP and WebSocket

## Bonus fix — prompt racing ahead of buffered game text (found during manual testing, not part of original 11-task bundle)

**Three mechanisms found, all fixed, in this order:**

1. `select()` reports the socket not currently writable (`FD_ISSET` false) — pre-existing, years
   old, unrelated to this bundle. `prompt_mode`, set by the command-processing block, wasn't
   cleared when the flush was skipped this way.
2. The EAGAIN-deferral path added by this bundle's Task 4 fix — before Task 4, hitting this
   condition disconnected the player instead of racing, so this specific path is new (but the
   underlying flaw below is not).
3. **The actual root cause (confirmed by direct reproduction, not just static analysis):**
   `process_output()` used `prompt_mode` for two unrelated purposes at once — "print a prompt
   after this flush" (set by command-processing) and "was there a still-unbroken bare prompt from
   a previous pulse, requiring a leading newline before new content" (read by `process_output()`).
   Processing a *new* command sets `prompt_mode = 1` for the first purpose *before*
   `process_output()` checks it for the second, masking a real dangling prompt from a prior pulse.
   This needs no EAGAIN, no full send buffer — reproduces on a perfectly healthy connection from
   ordinary successive commands, which is why it's been happening for years and worsens under
   rapid typing (more pulses hitting the same collision).

**Fix:** added a new, separate `bool bare_prompt_pending` field (`structs.h`, next to
`prompt_mode`) that's set `true` only when a bare prompt is actually written (the three
`write_to_descriptor()` prompt sites in `comm.cpp`'s "give the people some prompts" block), and
consumed (checked + cleared) by `process_output()` to decide the leading-newline break —
completely decoupled from `prompt_mode` now. Plus the two narrower fixes for items 1 and 2 above
(clearing `prompt_mode` when a flush is skipped/deferred).

- [x] **Confirmed via direct reproduction 2026-07-24** — wrote a Python test client, logged into a
      dedicated debug account/character, captured raw bytes before and after the fix while firing
      10 rapid `look`/`score`/`inventory` commands 50ms apart (see account details below).
      Before: `...to read it.\n\r>\x1b[33mThe Great Crossroads...` (prompt glued to next line, no
      break). After: `...to read it.\n\r>\n\r\x1b[33mThe Great Crossroads...` (proper break) — every
      single occurrence across the whole capture, verified on a scratch server instance (port
      1025) running the new binary, without touching the live session on port 1024.
- [ ] **Still needed:** restart the live/dev server (port 1024) to pick up the new binary, then
      confirm the same rapid-command test looks right from a real client (Mudlet etc.), and watch
      whether previously-flaky trigger/pattern-matching issues clear up.
- [ ] Confirm normal play still looks/feels identical when NOT under load (prompt still appears
      immediately after your own command's output, same as always)

**Debug account/character (for reuse in future testing):**

This is throwaway local test infrastructure, not a real credential: a disposable account/character
on a scratch server instance (port 1025) that only exists on this machine, spun up specifically so
manual testing didn't have to touch the dev live session on port 1024. `lib/accounts/` is
gitignored, so the account itself was never committed — only this login recipe is. Recreate it
(or one like it) on any machine by booting a scratch instance and creating a fresh account/
character; there's nothing here that needs rotating or protecting.

- Email: `clauded3bugbot@example.com`, password: `TestPass123!`, character: `Debugbot` (Human
  Warrior, male). Account JSON at `lib/accounts/A-E/clauded3bugbot@example.com/account.json` —
  **email_verified gets reset to false by the server on next login if the account cache is stale**
  (observed: editing the JSON while a server process already has it cached in memory gets
  silently overwritten back to false on that process's next login attempt) — if login fails on
  "verification pending," re-edit `email_verified` to `true` in that file and reconnect fresh
  (works reliably against a server process that hasn't touched this account yet this run).
- Login sequence: `Account email:` → email → `Account password:` → password → account menu
  (`2` = play a linked character) → character list (`1` = pick first/only character) → character
  welcome menu (`1` = enter the game).
- A scratch Python client scripts this same login sequence against the scratch server for
  automated before/after byte captures — rebuild it from the login sequence above rather than
  relying on a tmp-dir path, since scratch/session tmp directories don't survive between sessions.

**Committed** 2026-07-24 as `8ae32c9` on `release-frodo` (`src/comm.cpp`, `src/structs.h`), after
the user confirmed the fix against their live session. Still not pushed anywhere.

## Bonus fix #2 — `SEND SERVER_ID` returned an empty string (found during the protocol-monitoring pass)

**Root cause:** `SERVER_ID`'s value is announced once via a raw `MSDPSendPair()` call inside
`PerformHandshake()`'s `TELOPT_MSDP`/`DO` branch (`src/protocol.cpp`), but that call never
populates the variable's own backing storage — only `MSDPSetString()`/`MSDPSetNumber()` do that.
So a later client-issued `SEND SERVER_ID` (which reads that storage via `MSDPSend()`) got an empty
string. Deeper wrinkle found while fixing: `bMSDP` (unlike every sibling protocol flag —
`bMSSP`/`bATCP`/`bMSP`/`bMXP`/`bMCCP`, all `false` by default) defaults to `true` at connection
creation, so the `if (!pProtocol->bMSDP)` "first negotiation only" guard around that whole
announcement block is dead in practice on a normal `DO`/`WILL` exchange — an initial attempt to
just add `MSDPSetString()` inside that guard silently did nothing, since the guard itself never
fires. **Also confirmed `MSDPSend()` requires a logged-in `character` with `PRF_MSDP` set** —
important, since this announcement fires during telnet negotiation, *before* login, so replacing
the raw send with `MSDPSend()` would have silently broken the initial announcement entirely.

**Fix:** moved the `MSDPSetString(apDescriptor, eMSDP_SERVER_ID, MUD_NAME)` call outside/above the
dead `!bMSDP` guard so it runs unconditionally on every `DO MSDP`, while leaving the existing
`MSDPSendPair()` announcement (inside the guard, unchanged) as the immediate wire send. The
sibling ATCP-branch call site (also touched, `bATCP` correctly defaults `false` there) already had
this fix applied correctly since that guard genuinely does fire on first negotiation.

- [x] **Confirmed via the protocol monitor 2026-07-24** — `SEND SERVER_ID` now correctly returns
      `"Return of the Shadow"`, both via an explicit `SEND` command and via the natural per-pulse
      sweep once `set msdp on` is active. Build-verified on a scratch server instance.

**Committed** 2026-07-24 as `04afc58` on `release-frodo` (`src/protocol.cpp`). Still not pushed.

## Bonus fix #3 — MSDP update frequency/logic audit (found via a deep-dive after the protocol-monitoring pass)

Audited when/why the server sends MSDP updates (`msdp_update()` in comm.cpp,
`msdp_room_update()` in act_move.cpp, `weather_and_time()`/`weather_change()`
in weather.cpp). Found 5 issues; user deferred #5, approved #1-#4.

1. **ROOM_EXITS double-sent per move** — `msdp_room_update()` sent it
   immediately via a raw `MSDPSend()` (doesn't clear dirty), then the
   trailing `MSDPUpdate()` sweep sent it again since it was still dirty.
   Fixed: `MSDPSend` → `MSDPFlush`.
2. **WORLD_TIME double-sent per mud-hour tick** — same root cause as #1, in
   `weather.cpp`'s `another_hour()`. Fixed the same way.
3. **WEATHER redundant computation + double-send** — `weather_change()` had
   its own immediate push doing the exact same computation `msdp_update()`
   already does every single pulse (250ms) with proper dirty-tracking.
   Fixed by deleting the redundant push entirely (not just patching the
   send) — `weather_and_time()` runs earlier in the same pulse as
   `msdp_update()`, so no latency is added.
4. **Latent OOB crash risk in the deleted WEATHER push** — it read
   `world[desc->character->in_room].sector_type` with no bounds check on
   `in_room`, unlike `msdp_update()`'s own guarded version. Resolved as a
   side effect of deleting the block in #3 (no code path left that touches
   `world[]` without the guard).
5. **`msdp_update()` runs unthrottled every pulse (250ms) for every
   connected player** — **deferred**, user wants to investigate/confer with
   others before deciding whether to change the cadence. Not a bug fix,
   more a performance/design tuning decision.

- [x] **Verified on the scratch server (port 1025) 2026-07-24**: wrote
      `verify_dedup_fixes.py` (in the session scratchpad), walked 6 moves —
      standalone `ROOM_EXITS` var occurred exactly once per move
      (`[1,1,1,1,1,1]`, not 2); idled 65s across one mud-hour tick —
      `WORLD_TIME` sent exactly once; zero MSDP structural (VAR/VAL/
      TABLE/ARRAY) regressions. WEATHER didn't happen to change during the
      window (probabilistic per-tick), so no live before/after duplicate
      comparison for that one — confirmed via code review instead (the
      fix is a deletion, not a patch, so there's no remaining code path to
      race). First pass of the test script had a counting bug (conflated
      the standalone `ROOM_EXITS` var with the nested, unrelated `EXITS`
      field inside the `ROOM` table) — caught and fixed before trusting
      the result.

**Committed** 2026-07-24 as `b2f05e1` on `release-frodo` (`src/act_move.cpp`,
`src/weather.cpp`). Not pushed.

## Bonus fix #4 (possible) — long-standing "switch then return crashes" reports

User recalled a long-standing live-server problem: after an immortal uses `switch` to possess an
NPC body, using `return` to go back has reportedly caused crashes, to the point people have been
disconnecting/reconnecting instead of trusting `return`. Asked to test whether this is still a
problem.

- [x] **Tested directly 2026-07-24** on the scratch server (port 1025), as a temporarily-promoted
      implementor (Debugbot's level bumped to 100 for the test, `debugbot.character.json`, restored
      to 1 afterward from a saved backup): `load mob 2307` (a rat), `switch rat` → `Ok.`, `look`,
      `return` → `You return to your original body.`, `look`/`score` both worked normally. Repeated
      the whole cycle a second time. Confirmed the server was still accepting brand-new connections
      afterward. **No crash, either time.**
- **Interesting finding along the way**: while switched into the rat, `look` showed `Debugbot the
  Human (linkless) is standing here` — the game tags your abandoned original body with the same
  "(linkless)" marker normally used for a dropped connection. This lines up exactly with
  `do_switch`'s code (`act_wiz.cpp:1265`, `ch->desc = 0` on the original body while possessing the
  mob) — the same null-desc, non-NPC condition class that the `msdp_room_update()` guard added in
  this bundle's final-review fix (commit `5708b22`) protects against. **Possible relation, not
  confirmed as the exact historical cause**: if something used to call `msdp_room_update()` directly
  on that "linkless" original body while it sat desc-null (e.g. a spell targeting it from another
  player), that would have crashed before this fix existed. This bundle may have already fixed the
  reported switch/return crash as a side effect, without anyone having connected the two at the
  time. Not verified against the pre-fix code (would require reverting the guard and deliberately
  reproducing the crash) — user declined that extra step; treat this as a plausible, not certain,
  explanation.

**No code change for this entry** — it documents a live-testing result and a hypothesis about an
already-merged fix, not a new fix.

## Low priority — basically unverifiable without special setup

### 7. autorun backoff (Task 7)
- [ ] Only testable by actually crashing the server or scripting it separately; likely skip unless
      deliberately crash-testing

---

## Reference

- Full plan with exact diffs per task: `2026-07-24-core-server-health-update.md`
- Branch: merged into local `release-frodo` (18 commits, `f26d314..3027ab3`)
- **Pushed to `ahumbert/RotS_Live_ah:core-server-health-update` and opened as
  [PR #276](https://github.com/returnoftheshadow/RotS_Live/pull/276) against
  `returnoftheshadow/RotS_Live:release-frodo` on 2026-07-25.**
- Standing rule: no further push/PR without an explicit ask each time
