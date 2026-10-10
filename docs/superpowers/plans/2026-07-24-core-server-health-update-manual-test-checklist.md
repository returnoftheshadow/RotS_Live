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

- [x] **Corrected 2026-07-28 — vnum #1140 is not actually attached to any mob.** Read it directly
      (`lib/world/scr/11.scr.txt`, plain text): it genuinely has the risky nested shape (a wood-elf
      check nested inside a broader elf-race check, ending in back-to-back `END`s), but a full scan
      confirmed it's orphaned content — searched every `.zon` file for an `A 12` (assign-script)
      reset command referencing 1140 (zero hits), then ran the immortal `mob2csv` command (dumps
      every one of the then-3009 mob prototypes' `script` field) and grepped for `1140` — zero
      matches. So this specific vnum can't be triggered through normal play. Also confirmed this
      shape is far from a one-off: parsing every `.scr.txt` file for the same "two consecutive
      `END`/`END_ELSE_BEGIN` opcodes" signature found **118** scripts with the shape across the
      whole world, **459** distinct script vnums actually attached to a live mob, and a solid
      overlap between the two sets — plenty of real test candidates exist.
- [x] **Confirmed 2026-07-28 via script #1145 ("Orc Slave Aggro to Whitie Script")** — same nested
      `if/begin...end` shape, live, attached to mob vnum 20335 ("an orc miner", room 17716).
      Triggered via `say free` near it (an `ON_HEAR_SAY` trigger, not `ON_ENTER` — simpler to set
      up than 1140's `ON_ENTER`/elf-race shape since it needed no darkness/locked-door workarounds).
      Full nested sequence fired correctly in order: "an orc miner snarls angrily at the thought of
      being indentured..." (first message inside the nested whitie-logic `BEGIN`), then an
      orcish-garbled "I will not have you as a new master, filth!" (a *later* statement inside that
      same nested block — proves execution didn't skip past it), then the orc attacked and combat
      proceeded completely normally afterward (HP tracked correctly, no corruption, no stuck state,
      no bleed into unrelated script/room content on subsequent `look`/`score`). This is a clean,
      positive, in-order confirmation of the Task 11 fix on a real nested script, not just an
      absence-of-crash check.
- [x] Spot-checked along the way: normal room/mob interaction (movement, combat, dialogue) around
      both test sites showed no regressions in non-nested or already-working script content.

### 3. Flee / windblast double-fire (Task 9 + two fix rounds)

- [x] **Confirmed 2026-07-28** — used the Arena (room 1120, single W exit) as the test room per the
      user's suggestion: opened the west door, loaded mob vnum 5716 ("Blent test", an otherwise-
      unspawned mob whose baked-in script #2398 is a clean `ON_BEFORE_ENTER` test script — random
      roll, prints "You successfully enter the room!" and allows entry, or "You fail to enter the
      room." and blocks it via `RETURN_FALSE`) into the neighboring room (Creation Hall, 1101).
      Set Bashtest's tactics off berserk (`do_flee` refuses while berserk) and issued `flee`
      repeatedly — `do_flee()` picks a random direction each attempt, so only tries that land on the
      Arena's one valid exit (west) actually reach the check. Across 6 attempts, 3 reached room 1101
      and each showed the enter-room message **exactly once** (two "fail", one "success") — never
      duplicated. Directly confirms the `g_skip_next_before_enter_for` fix
      (`src/act_offe.cpp:405`/`src/act_move.cpp:687-690`) for the core case: `do_flee()`'s own
      `check_simple_move()` call fires the trigger once, and the flag correctly suppresses the
      redundant refire inside `do_move()`'s internal `check_simple_move()` call for the *same*
      destination.
- [x] **Confirmed 2026-07-28** — the Arena's single exit can't test this (a haze reroll there just
      fails "no exit," never reaching a second valid room), so used Creation Hall (1101, 5 exits:
      N/E/S/W/U) instead, with the same "Blent test" mob (vnum 5716, script #2398) loaded into all
      5 neighboring rooms so *any* direction — original or haze-rerolled — has an observable
      trigger. Set `AFF_HAZE` directly via `wizset Bashtest affected 1073741824` (confirmed active
      via `stat Bashtest` → `AFF: HAZE.`), then fled repeatedly (haze's reroll is a 25% roll,
      `src/act_move.cpp:693`, and only fires at all when `do_flee()`'s own initial direction-check
      already succeeded — needed ~9-12 such successes per batch to expect one). Caught 3 reroll
      events across 40 attempts. Clearest one (attempt 26): "You successfully enter the room!" /
      "You flee head over heels." / "**You feel dizzy, and move randomly.**" / "**You successfully
      enter the room!**" (a *second*, distinct occurrence) / lands in a *different* room (1140, "A
      Brightly Lit Room") than whatever the original pre-haze direction was. Another (attempt 25)
      showed the same pattern ending in "fail" instead of "success" for the rerolled destination.
      This is the **correct** behavior, not a bug: two different rooms, each getting its trigger
      fired exactly once for its own destination — directly confirms
      `skip_before_enter_direction_intact` (`src/act_move.cpp:702`) correctly withholds the
      suppression flag when haze changes the direction, so the *actual* final destination still
      gets evaluated fresh instead of being silently let through unchecked.
- [ ] **Attempted 2026-07-28, stopped short of a clean confirmation.** Windblast is Haradrim-only
      (`can_harad_use_skill`, `src/ranger.cpp:3477`) — created a level-40 Haradrim ranger
      ("Windtest") to test it. Discovered along the way: **no guild trainer in the entire game has
      the Haradrim race bit set in `will_teach`** (checked all ~60 guild-assigned mobs via `vstat`)
      — a real world-content gap, not a bug, that makes this race untrainable through normal play.
      Worked around it by setting the skill directly in the character's JSON file (`"skills":
      {"wind_blast": 20}`, picked up correctly by `recalc_skills()` on next load).
      The cast itself fires correctly ("Vile black wind eminates from you, slamming into all!"),
      but every target tried fought back once hit (opposing-faction PCs auto-attack on sight per
      the game's faction design, and even a supposedly-inert test mob retaliated once damaged) —
      got Windtest killed twice (once with a level loss) before the random direction-roll ever
      landed on the test room's one valid exit. Per user decision, stopped here rather than keep
      fighting the combat/RNG setup. **Reasoning for treating this as adequately covered anyway:**
      `on_windblast_hit()` (`src/ranger.cpp:3607-3657`) is structurally identical to `do_flee()` —
      same 6-attempt random-direction loop, same `g_skip_next_before_enter_for` flag, same
      `do_move()` call — and that exact mechanism (including the AFF_HAZE edge case) was already
      thoroughly confirmed via the flee tests above. Treat as high-confidence-by-code-identity, not
      independently observed.
- [ ] **Not attempted** — flee while riding a mount (or a ridden mob fleeing). Would need a mount
      set up; deprioritized alongside windblast per the same "sufficiently tested for now" call.

(Note: `ON_ENTER` is a *different*, unrelated trigger — fires once from inside `do_move()` after
actually entering a room. It was never part of the double-fire bug; only `ON_BEFORE_ENTER`,
fired inside `check_simple_move()`, was affected.)

## Medium priority — latency/socket fixes, harder to trigger but worth a pass

### 4. Skill cooldowns (Task 8)
- [x] **Confirmed 2026-07-28 (single-player baseline)** — `defend` (`src/act_offe.cpp:982-1011`) is
      the only non-race-gated skill using `skill_timer`; set Bashtest up (specialized "defending",
      shield equipped, `defend` mastered via a guild trainer) and used it mid-combat. `affections`
      tracked the cooldown cleanly: still active at t=2s and t=6s post-use, cleared by t=10.3s —
      consistent with the real 12s cooldown, no extra stuck tick observed.
- [ ] **Not achieved — the actual bug-triggering shape needs two players' entries interleaved in
      the shared vector, which a single player's own usage structurally cannot produce.**
      `add_skill_timer()` (`src/skill_timer.cpp:12-25`) always pushes a skill-specific entry then
      immediately pushes its own global-cooldown entry (`GLOBAL_COOLDOWN_COUNTER = 2`) right after
      it. The global entry is always at a *later* vector index and always expires first (2s <<
      most skill cooldowns), so erasing it can never cause the erase-skip bug to affect the
      earlier-indexed skill entry — the bug only manifests when an *earlier*-indexed entry expires
      while a *later* one (e.g. a second player's entry, pushed afterward) is still counting down.
      Set up a second character (Windtest) to attempt exactly this cross-player interleaving, but
      hit a chain of incidental setup friction (a JSON skill edit not taking effect because the
      character was still memory-resident from an earlier session; a shield dropping to the floor
      instead of equipping; landing in a different, unlit room than intended) that consumed the
      available time without producing the paired-timing observation. **Stopped here per user
      decision.** Confirmed instead via direct source read: the live code
      (`src/skill_timer.cpp:41-52`) matches the documented fix exactly — `for (int i = 0; i <
      m_skill_timer.size();)` with `++i` only in the `counter > 0` branch, erase-without-increment
      otherwise — so this is high-confidence-by-code-match, not independently observed for the
      actual interleaved-entry case.

### 5. Double-delay / bash-interrupts-cast (Task 10)
- [x] **Confirmed 2026-07-28** — instrumented `WAIT_STATE_BRIEF`/`FULL` (`src/utils.h`) and
      `do_bash` (`src/act_offe.cpp`) with temporary diagnostic logging, then live-tested a level-40
      warrior repeatedly bashing an NPC spellcaster (dark mage, vnum 12600) in an isolated arena
      room (vnum 1120 — see below). Cast something, get bashed mid-cast; confirm the stun applies
      correctly: **24/24 samples**, bash's stun applied cleanly every time it landed mid-cast, no
      corruption observed.
- [ ] **Still open — did not exercise this code path.** Task 10's fix specifically targets the case
      where `complete_delay()` *reentrantly* queues a new delay while force-completing the old one.
      In all 24 samples, the mage's cast priority was 30 vs. bash's 80 — comfortably within the
      pre-existing "clean override" branch that predates this fix and was never buggy. Never
      produced a scenario with the interrupted delay's priority ≥ 80, and this mob's specific
      spells didn't queue a follow-up delay on early completion either way. **Decision (per user):
      pause further synthetic repro — wait for a real player-reported occurrence before resuming.**
      Diagnostic logging left in place (uncommitted, `src/utils.h`/`src/act_offe.cpp`) so a future
      occurrence will already be captured in detail. Full writeup: see the `double_delay_bug`
      memory (auto-memory system) for exact log evidence and reasoning.
      New test infra discovered/built along the way, reusable for future live-combat testing:
      room vnum 1120 ("The Arena" — lit, no weather, isolated, single exit) is the intended
      immortal test-combat room; the default starting room is peaceful (combat no-ops there), and
      the mage's own home room has no ambient light.
- [x] **Re-investigated 2026-07-29, prompted by a concern this fix itself had regressed something.**
      Root-caused: it hadn't. Traced `complete_delay()` (`comm.cpp:2457`) — it zeroes
      `ch->delay.wait_value` as its very first statement, so the reentrant-check added by this
      task's fix (`if (ch->delay.wait_value != 0)` after `complete_delay()`) can only go true on
      genuine reentrancy, never spuriously on the ordinary path. Also confirmed PR #274 (Account
      management) barely touched `utils.h` (two `char *`→`char*` formatting diffs only, nothing near
      the delay macros), ruling out a cross-PR interaction. **999c167 is sound, not a regression.**
- [x] **Separate, real bug found and fixed the same day: bash bypassed battle mage's cast-
      interrupt-resistance entirely, unrelated to the reentrancy fix above.** See "Bonus fix #6"
      below for full detail — root cause, live confirmation, and the fix itself
      (`db1b89f`, not yet pushed).

### 6. Connection behavior (Tasks 3, 4, 5, 6)
- [x] **Confirmed 2026-07-28** — normal play feels the same: scripted login, `score`, movement
      (north/south), `who`, `quit`, all responded correctly with no regressions.
- [x] **Confirmed 2026-07-28 (Task 5, read-loop iteration cap)** — opened a second raw connection
      and flooded it at max rate with no newline terminator (unthrottled `sendall()` loop) while
      timing a normal connection's `score` command latency. Baseline (no flood) avg time-to-first-
      byte 0.443s (n=15); during flood avg 0.449s (n=15) — no measurable difference, zero timeouts
      either side. Server logs stayed clean (no errors/crashes) throughout. This is real load, not
      just a code read — confirms the cap prevents one flooding connection from stalling others.
- [ ] **Attempted, inconclusive (Task 4, EAGAIN-on-write)** — tried to force `write_to_descriptor()`
      into `EWOULDBLOCK` by piling up 300 large-output commands on one connection without ever
      reading the responses. Never got there: hit the pulse-rate command-processing bottleneck
      first (one command consumed per ~250ms tick), not an output-buffer backpressure condition —
      the command backlog took longer to drain than expected, but that's unrelated to Task 4's
      fix. Genuinely needs artificial link throttling (`tc qdisc` on loopback, or a deliberately
      slow-reading client) to trigger for real; didn't want to touch system network config without
      asking first. Still only weak coverage (unchanged-success-path only) as originally noted.
- [x] **Confirmed 2026-07-28 (Task 3, proxy TCP/WebSocket)** — built and ran the proxy
      (`cargo run -p proxy -- --game 127.0.0.1:1024 --listen 0.0.0.0:3791 --websocket
      0.0.0.0:8181`) in front of the game (relaunched with `-x`, see correction below). Tested
      both connection modes end-to-end (full login, MSDP payloads, `score`/`who`/`quit`): plain
      TCP through port 3791 with a raw socket client, and WebSocket through port 8181 with a
      hand-rolled RFC6455 client (no `websockets` package available, no `pip` on this machine).
      Both worked cleanly; server and proxy logs stayed clean (one benign proxy-log line from the
      WS test client closing the raw socket instead of sending a proper Close frame — a test-
      harness artifact, not a real finding). Note: port 8080 (the proxy's WebSocket default) was
      already bound by an unrelated local Docker container — used 8181 instead for this test, not
      a project-related conflict.

      **Correction to this repo's `CLAUDE.md`:** it currently says to run `./bin/ageland -p` when
      a proxy sits in front of the server. That's stale — in the current code, `-p <port>` is just
      an alternate way to specify the listen port (`parse_startup_options`, `src/comm.cpp:275-291`);
      the flag that actually makes the game expect the proxy's 4-byte client-IP header
      (`has_proxy`) is **`-x`** (`src/comm.cpp:292-294`, `:1521`). Using `-p` for this purpose
      doesn't error, it just silently fails to enable proxy-header mode. Worth fixing `CLAUDE.md`.
- [x] **Confirmed 2026-07-28 (Task 6, pre-login idle reap)** — a connection sitting idle at the
      login prompt with zero input got closed by the server after 953.5s (~15.9 min), matching the
      15-minute `PRE_LOGIN_IDLE_TIMEOUT` (checked once per minute, so up to ~1 min of slop past the
      nominal threshold is expected). Confirms `check_pre_login_idle()` fires and reaps correctly.

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

## Bonus fix #5 — `wizset <victim> OB <value>` silently rejected lowercase `ob`

Found while running Task 4 (skill cooldowns) testing on 2026-07-28: `wizset bashtest ob 200`
failed with "Can't set that!" while `wizset bashtest OB 200` worked.

**Root cause:** `do_wizset`'s field-lookup loop (`act_wiz.cpp:2762`) matched the typed field name
against its `fields[]` table using the raw C `strncmp`, which is case-sensitive. Every other field
in the table is lowercase, so this never surfaced — except `"OB"`, the one mixed-case entry. No
match means the loop falls through to the table's terminating sentinel entry, which hits the
`switch (l)` statement's `default` case and prints "Can't set that!" instead of setting anything.
This is the same class of inconsistency as the rest of the file: the two lines just above already
use this codebase's own case-insensitive `str_cmp` for the `file`/`player`/`mob` prefix check.

**Fix:** swapped `strncmp` for `strn_cmp` (`utility.cpp:1027`, already used elsewhere in this same
function), which lowercases both sides via the `LOWER` macro before comparing. This fixes the
comparison generally rather than just special-casing the `"OB"` table entry, so it won't recur if
another mixed-case field name is ever added to the table.

- [x] **Confirmed 2026-07-28** — scratch server (port 1025), `wizset file bashtest ob 77`,
      `OB 55`, and `Ob 33` all succeeded ("Bashtest's OB set to N. Saved in file.") and persisted
      correctly to the character JSON, verified after each call.

## Bonus fix #6 — bash always broke a battle mage's cast, ignoring their tactics/level resistance

Found 2026-07-29 while re-investigating Task 10 after a concern (unfounded, see above) that the
double-delay fix itself had regressed something. The re-investigation surfaced a separate, real,
much older bug in the process.

**Root cause:** `battle_mage_handler::does_spell_get_interrupted()` (`battle_mage_handler.cpp`,
added 2019) is only ever consulted from `damage()` (`fight.cpp:1735`), guarded by
`IS_AFFECTED(victim, AFF_WAITWHEEL) && GET_WAIT_PRIORITY(victim) <= 40`. `do_bash`
(`act_offe.cpp`) calls `WAIT_STATE_FULL(victim, ..., 80, ...)` *before* calling `damage()`.
Priority 80 always beats a cast's priority 30, so `WAIT_STATE_FULL` unconditionally force-completes
the cast via `complete_delay()`, which clears `AFF_WAITWHEEL` as its first statement — by the time
`damage()` runs right after and reaches the battle-mage check, the flag is already gone. Ordinary
weapon hits go straight to `damage()` with `AFF_WAITWHEEL` still intact, so those correctly roll the
resistance chance. Bash was the one attack type that bypassed the roll entirely, regardless of
tactics stance or profession levels.

**Fix:** before calling `WAIT_STATE_FULL`, `do_bash` now checks
`does_spell_get_interrupted()` itself and skips the clobber if the roll says the cast should
survive — mirroring the same silent-skip pattern already used at the melee-hit
(`fight.cpp:1735`) and mental-attack (`clerics.cpp:229,409`) call sites. Bash still lands its own
token damage either way; it just doesn't override a successfully-defended cast.

- [x] **Regression safety confirmed live 2026-07-29** — re-ran the original dark-mage bash repro
      (non-battle-mage victim) against the fix: `victim_resists_cast_interruption` computed `false`
      on every sample including ones caught mid-cast, and the stun applied exactly as before. Zero
      behavior change for the common (non-battle-mage) case.
- [x] **Both branches confirmed live 2026-07-29 with a real battle mage.** Built a fresh test
      character `Battlemage` (Wood Elf, Mage class, `specialize battle` → battle magic, `set tactics
      aggressive`, `chill ray` practiced to 100%) on the shared debug account. Notable side-finding:
      standard-class character creation auto-invests all classpoints into that one profession — a
      single admin `advance Battlemage 40` alone scaled `Class levels: Mag:30`, no manual point
      allocation or JSON editing needed. Ran Bashtest bashing Battlemage while it repeatedly cast
      `chill ray` in the arena (room 1120): of 7 bash landings, 6 caught it mid-cast, and **4 of
      those 6 resisted** — matching the transcript exactly (resisted casts completed normally,
      "Ok. ... chill ray strikes him"; non-resisted ones showed "You could not concentrate
      anymore!", the pre-existing correct interrupted path). ~67% observed resist rate lines up with
      the ~63% predicted from the resistance formula (`base_chance 0.25 + mage_bonus 0.30 +
      tactic_bonus 0.08`) for a 6-sample run.

**Committed** 2026-07-29 as `db1b89f` on local `release-frodo` (`src/act_offe.cpp` only). **Not
pushed** — not yet part of the open PR #276 branch (`core-server-health-update`); per standing
rule, needs an explicit ask before pushing.

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
- **3 more commits exist locally on top of that, not yet pushed (2026-07-29):** two docs commits
  (`2f5c27d`, `75f09ac`) root-causing the double-delay bug, plus the actual fix commit `db1b89f`
  (bonus fix #6 above). Not part of the PR #276 branch yet.
- Standing rule: no further push/PR without an explicit ask each time
