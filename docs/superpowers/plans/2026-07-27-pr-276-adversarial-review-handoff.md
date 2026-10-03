# PR #276 Adversarial Review — Agent Handoff

**PR:** https://github.com/returnoftheshadow/RotS_Live/pull/276
**Title:** Core server-health update: MSDP, socket/latency, combat reentrancy, scripting fixes
**Author:** `ahumbert` | **Base:** `release-frodo` | **Head:** `core-server-health-update`
**Review date:** 2026-07-27 (adversarial review by Claude; findings verified against base-branch source, not just the diff)

## Status as of 2026-07-27 (end of day)

- **Fixed and pushed to the PR** (`508bec4`, `a837cd8`, `3c865a3`): findings 1, 2, and both minor
  items.
- **Resolved, not a code fix:** finding 6 (dropped the unbacked claim from the PR description
  directly). Finding 7 (checklist scrubbed, commit `eca5e51`, not yet pushed).
- **Resolved as a non-issue**, confirmed by a live test, not just re-reading source: finding 3 —
  see its updated section below. The original finding was wrong about the practical impact.
- **Resolved 2026-07-29:** finding 4, deferred by author decision (see its updated section — both
  questions reduce to the same single-slot-delay redesign question, which is out of scope for
  this PR). A related-but-distinct bug found the same day (battle mage bash bypass) was fixed and
  pushed (`db1b89f`); the minor comment item was also fixed and pushed (`474831d`).
- **Resolved 2026-07-29:** finding 5, marked out of scope for this PR (see its updated section —
  fixing it risks changing behavior for live scripts, which isn't a fit for a PR that's now only
  taking safe corrections, not new intended changes).

**All findings resolved. Nothing left open in this review.**

## Instructions for the agent taking this over

1. **Do not assume intent.** Findings marked **[ASK AUTHOR]** involve a deliberate-looking design
   choice — query the PR author (via PR comment, or however the user directs) before changing
   anything. Findings marked **[FIX]** are verified defects with a concrete failure path; propose
   or apply the fix per the user's direction.
2. All file/line references below are against the **base branch** (`release-frodo` at `f26d314`)
   unless the reference is to a hunk the PR adds — those are described by function name so they
   survive line drift. Check out the PR head branch before editing.
3. Re-verify each finding against the head branch before acting — the author may have pushed
   updates since this review.
4. The PR's core fixes were verified sound during this review (see "Verified non-issues" at the
   bottom). Do not re-litigate those; focus on the items below.

---

## [FIXED — `508bec4`] 1. `g_skip_next_before_enter_for` can dangle → silently skips ON_BEFORE_ENTER on a later, unrelated move

**Files:** `src/act_move.cpp` (new global + `check_simple_move()` + `do_move()`), set by
`src/act_offe.cpp` `do_flee()` and `src/ranger.cpp` `on_windblast_hit()`.

**Mechanism:** `do_move()` re-arms the global immediately before each
`check_simple_move(ch, cmd, ...)` call site, but the flag is only *consumed* at the trigger check
inside `check_simple_move()`. There are five early returns **before** that consume point
(base `act_move.cpp:155-172`):

- `special()` intercept (mode is `SCMD_FLEE`, not `SCMD_MOVING`, so specials run here),
- `GET_POS(ch) < POSITION_FIGHTING` or `PLR_FLAGGED(ch, PLR_WRITING)`,
- missing `dir_option` / `to_room < 0`,
- `ch->delay.wait_value > 0`,
- `!room_to`.

If any fires, the armed flag survives `do_move()`. On the character's **next** movement command,
`do_move()`'s entry logic consumes it, and because `cmd == requested_cmd_before_haze - 1` is
trivially true when AFF_HAZE doesn't re-roll, the flag is re-armed and **ON_BEFORE_ENTER is
silently skipped for an unrelated move** — the exact bug class this PR set out to fix. Guard-room
triggers could be walked past once.

**This is reachable:** `special()` runs *twice* per flee (once in `do_flee()`'s validation
`check_simple_move()`, once in `do_move()`'s internal call) — a stateful or randomized special
proc can pass the first call and intercept the second. The validation call's own trigger can also
set a delay (`wait_value > 0`) or change position before the internal call runs.

**Suggested fix:** consume the flag unconditionally as the *first statement* of
`check_simple_move()` (capture into a local `bool`, branch on the local at the trigger site).
That makes a dangle structurally impossible regardless of early returns. This also removes the
need for the re-arm-per-call-site dance in `do_move()` — the entry capture there can pass intent
some other way, or the global can simply be consumed once in `check_simple_move()` with
`do_move()`'s haze check clearing it instead of re-arming.

**Secondary note (mention to author, no action required):** only `call_trigger` is deduped;
`special()` still fires twice per flee. Pre-existing, but adjacent to this PR's theme.

**Resolution:** fixed exactly as suggested — the flag is now consumed unconditionally as the
first statement of `check_simple_move()`, captured into a local `bool skip_before_enter`. A
dangle across the five early returns is now structurally impossible. Build-verified
(`scripts/rots-docker.sh compile`); not separately dynamic-tested (a gdb-based attempt to call
`check_simple_move()` directly hit unrelated tooling friction resolving this codebase's C++
debug-info symbols — see commit message for detail — abandoned in favor of the static trace,
which is solid for a change this mechanically simple). Committed as `508bec4`, pushed to the PR.

---

## [FIXED — `a837cd8`] 2. EAGAIN deferral loses the pending leading newline — reintroduces the glued-prompt bug on the exact path built to handle it

**File:** `src/comm.cpp`, `process_output()` (PR's version).

**Mechanism:** the PR's `process_output()` consumes `bare_prompt_pending` (checks **and clears**
it) at the top, placing the `"\n\r"` break only in the local output buffer. If
`write_to_descriptor()` then returns `-2` (EAGAIN, nothing written), the new code correctly
leaves `t->output` intact and returns 0 — but the flag is already false. The retry next pulse
rebuilds output **without** the leading break, so the still-dangling prompt from the prior pulse
gets glued to the next line. EAGAIN occurs precisely under the bursty send-buffer pressure where
the prompt race matters most.

**Suggested fix:** in the `-2` branch, restore the flag before returning:
`if (i_shift == 2) t->bare_prompt_pending = true;` — or restructure so the flag is only cleared
after a successful write.

**Resolution:** fixed exactly as suggested. Dynamically confirmed: 15 rapid-fire `look` commands
50ms apart against a scratch server, byte-captured, zero glued-prompt occurrences — the original
prompt-race fix (`8ae32c9`) is intact after this change and the write-success-gating change below
it. Committed as `a837cd8`, pushed to the PR.

---

## [RESOLVED — not an issue, confirmed by live test] 3. `MSDPSend` → `MSDPFlush` silently adds a REPORT requirement (client-visible protocol change)

**Files:** `src/act_move.cpp` (`msdp_room_update()`, ROOM_EXITS), `src/weather.cpp`
(`another_hour()` WORLD_TIME; deleted `weather_change()` WEATHER push).

**Facts (verified in `src/protocol.cpp`):** `MSDPSend` (line ~1178) pushes to any logged-in
client with `PRF_MSDP` + `bMSDP`, regardless of report status. `MSDPFlush` (line ~1164)
additionally requires `bReport && bDirty`. On paper that means `ROOM_EXITS`/`WORLD_TIME`/`WEATHER`
should now require a client to have sent `REPORT <var>` first, where they used to be unsolicited.

**Why this doesn't actually happen — the finding was wrong about practical impact:**
`bReport` defaults to `true` for *every* variable on *every* connection
(`protocol.cpp:323`, `pProtocol->pVariables[i]->bReport = true;` at protocol struct setup) — a
separate, pre-existing, already-catalogued bug (`docs/msdp-audit.md` finding #2: "`bReport`
defaults to `true`" — makes `REPORT` itself a no-op, since everything's already "reported" from
connect). `bMSDP` also defaults `true` (this PR's own finding 6 territory — `SEND SERVER_ID`).
So `MSDPFlush`'s `bReport &&` gate is trivially satisfied for effectively every client today; it
isn't gating anything in practice.

**Confirmed live, not just re-derived from source:** connected a scratch-server test client that
never sent `IAC DO MSDP` and never sent a single `REPORT`, logged in, and moved rooms. `ROOM_EXITS`
and the full `ROOM` table arrived unsolicited on login and on the very first move anyway — byte
capture, not inference.

**Conclusion:** no client goes silent from this change on this codebase, today. What survives from
the original finding is much smaller and not a bug: the `bDirty` gate means moving between two
rooms with identical exit strings sends nothing (value unchanged) — the PR checklist's "exactly
once per move" is really "at most once per move," which is correct dedup behavior.

The underlying `bReport`-defaults-true bug is real, pre-existing, not touched by this PR, and
already tracked at low priority in `docs/msdp-audit.md`. Out of scope here unless the author wants
to pull it in.

---

## [DEFERRED, per author, 2026-07-29] 4. WAIT_STATE reentrancy guard drops the *new* action's delay and shows players a "Possible bug" message on a now-legitimate path

**File:** `src/utils.h`, `WAIT_STATE_BRIEF` and `WAIT_STATE_FULL` macros (PR's version).

**Facts (verified):** `complete_delay()` (`src/comm.cpp:2327`) zeroes `wait_value` first, so the
new `if (ch->delay.wait_value != 0)` recheck correctly detects a reentrantly-queued delay. But
when it trips, the macro `break`s out: the interrupted action's follow-up delay **wins**, and the
incoming action's delay is never applied — e.g. in bash-interrupts-cast, the spell's recovery
delay survives and **the bash stun is silently not applied to the victim**. `SET_BIT(...,
new_flag)` is also skipped. Meanwhile the player still sees
`"Possible bug - double delay. Please notify Imps."` for what the PR's own comment describes as
expected reentrant behavior.

**Resolution (2026-07-29):** both questions reduce to the same underlying issue — `ch->delay` is a
single slot, so there's no answer to "which delay should win" that isn't a workaround for that.
Picking a winner (or changing the message) is exactly the judgment call that requires the real fix
(a proper delay queue or explicit interrupt/cancel semantics), which is a bigger redesign than this
PR's scope. **Decision: defer both questions along with the redesign itself** — see the
`double_delay_bug` memory (auto-memory system, 2026-07-29 entry) for the full reasoning and the
logged redesign idea. Current behavior (reentrant delay wins, message unchanged) stays as-is.

A related-but-distinct bug *was* found and fixed the same day: bash bypassed battle mage's
cast-interrupt-resistance roll entirely (unrelated to the reentrancy question above — it's a
different code path, the priority-based force-complete itself, not the reentrant-collision guard).
See the test checklist's "Bonus fix #6" for detail. Committed `db1b89f`, pushed.

**Minor (fix without asking), done:** `WAIT_STATE_FULL`'s copy of the guard lacked the explanatory
comment that `WAIT_STATE_BRIEF`'s has — added. Committed `474831d`, pushed.

---

## [OUT OF SCOPE FOR THIS PR, per author, 2026-07-29] 5. `get_next_command()` still mis-skips nested if/**else** blocks (fix incomplete)

**File:** `src/script.cpp`, `get_next_command()` (~line 759).

**Facts (verified against caller convention, e.g. `script.cpp:1121`):** the double-advance fix is
correct — callers pass the `BEGIN`, the function scans from `BEGIN->next`, and the rewrite no
longer skips the element after a nested block. **But** the recursive skip stops at the *first*
`SCRIPT_END` **or** `SCRIPT_END_ELSE_BEGIN`. For a *nested* construct
`BEGIN … END_ELSE_BEGIN … END` inside a region being skipped, the recursion returns into the
nested else-body; the outer scan then treats the nested `END` as the outer terminator and resumes
execution **mid-block** — executing code that should have been skipped.

This is **pre-existing** (the old code had the same stop condition), not introduced by the PR.

**Resolution (2026-07-29): out of scope for this PR.** Per the author: this PR already implements
its intended changes, and is now only being corrected for what's needed to safely deploy — a
behavioral change to the skip-recursion (the "skip both halves" fix suggested above) risks changing
execution for any live script that happens to hit this shape, which is exactly the kind of new risk
this PR shouldn't be taking on at this stage. Leave the pre-existing limitation as-is; if it's ever
picked up, do it as its own separate, deliberately-scoped change (with its own testing pass against
live scripts), not folded into this PR.

---

## [RESOLVED — dropped from PR description] 6. Claimed autorun backoff fix is not in the PR

**File:** `.gitignore` (adds `autorun`).

**Facts (verified):** `autorun` is untracked (`git ls-files` confirms), and this PR adds it to
`.gitignore`. The PR description's "5s backoff before autorun restarts the game process" exists
only as an unversioned file on the server — a described production behavior with no source of
truth in the repo.

**Question for the author:** *Should the autorun script be committed (e.g. as `autorun.example`
or under `scripts/`), or should the backoff claim be dropped from the PR description? As-is, the
change can't be reviewed and would be lost if the server is rebuilt.*

**Resolution:** the version that briefly existed in git (`74fe5a4`) was untracked again the same
day (`c9a8dfb`, "isn't compatible with the live deploy environments"), so committing it wasn't the
right call. Instead the "5s backoff before autorun restarts the game process" bullet was removed
from the live PR #276 description directly (confirmed via `gh pr view 276`, the line is gone).
No code change.

---

## [FIXED — `eca5e51`, not yet pushed] 7. Committed credentials + email-verification bypass recipe in the test checklist

**File:** `docs/superpowers/plans/2026-07-24-core-server-health-update-manual-test-checklist.md`
(new file in this PR).

The checklist commits:
- a debug account's email and **password** (`clauded3bugbot@example.com` / `TestPass123!`) — the
  account also apparently exists under `lib/accounts/` on real servers;
- step-by-step instructions for flipping `email_verified` in account JSON to bypass email
  verification;
- a hardcoded session-scoped `/tmp/claude-1000/...` path from another user's machine as a
  reference.

**Action:** scrub the credentials and the bypass recipe from the committed doc (move to a private
location if the author wants to keep them); replace the tmp path reference with the durable
login-sequence description that's already in the doc. Confirm with the author/user whether the
`Debugbot` account should also be removed or have its password rotated on any shared server.

**Correction on severity, confirmed with the author:** the "apparently exists on real servers" /
"rotate the password" framing above doesn't hold up — `lib/accounts/` is fully gitignored (`git
check-ignore -v` confirms), so the real account file with its real password hash was never
committed at all; only this plaintext string, documenting a throwaway account created purely for a
local scratch-server instance (port 1025), was. No filesystem access to the account store means
the bypass recipe grants nothing either. The only real issue was the stale tmp path (doc hygiene,
not a leak). **Resolution:** replaced the tmp path with a pointer back to the durable login
sequence, and added a framing note ahead of the credential block explaining this is disposable
local-only test infrastructure so a future review doesn't re-flag it. Committed as `eca5e51`
(bundled with the review-doc relocation), not yet pushed.

---

## Minor items (fix or note, author query optional) — [FIXED, pushed]

- **Prompt writes ignore `write_to_descriptor` returns while setting `bare_prompt_pending`**
  (`src/comm.cpp`, "give the people some prompts" block): if a prompt write itself returns `-2`
  (silently dropped), the flag claims a bare prompt was written → spurious leading blank line on
  the next flush. Cosmetic; fix by only setting the flag when the write returns > 0.
  **Correction during implementation:** `write_to_descriptor()`'s actual success value at this
  call site is `0`, not `> 0` as suggested — `> 0` never happens for this function and would have
  silently disabled the flag entirely. Fixed with the correct condition (`== 0`). Committed as
  `a837cd8`, pushed.
- **Proxy `set_nodelay(true)?`** (`proxy/src/main.rs`): the `?` propagates a (rare) nodelay
  failure and kills the connection. The C side logs via `perror` and continues; best-effort
  (log-and-continue) would match. Fixed at all three call sites (`GameAddr::connect()`,
  `handle_tcp()`, `handle_ws()` — the review only named one, the other two had the same pattern).
  `cargo build -p proxy` and `cargo test -p proxy` pass. Committed as `3c865a3`, pushed.

---

## Verified non-issues — do not re-raise

These were adversarially checked during the review and found sound:

- `last_input_time` is initialized to `login_time` at descriptor creation (`comm.cpp:1470`), so
  `check_pre_login_idle()` cannot insta-reap fresh connections.
- `close_socket(point)` with default `drop_all=TRUE` is safe for character-less descriptors
  during the reap iteration (`next_point` captured first; only the passed descriptor is freed).
- `weather_and_time()` (game_loop line ~1040) runs before `msdp_update()` (~1054), so deleting
  `weather_change()`'s push adds no latency for any client (see finding 3, now resolved: the
  `bReport` gate isn't actually restricting anyone today).
- `MSDPSendList` sanitization is safe: the space→`MSDP_VAL` conversion happens after `sprintf`,
  and 0x20 passes `MSDPSanitizeValue` untouched; sanitized length is used for the buffer-size
  check, so no truncation mismatch. `MSDPSanitizeValue(NULL)` is null-safe.
- `process_output()` return contract (1 success / 0 EAGAIN-deferral / -1 fatal) is handled
  consistently at all three call sites (`comm.cpp:155` treats 0 as non-fatal; buffered greeting
  flushes later — correct).
- `skill_timer::update_skill_timer()` erase-without-increment fix is correct.
- `msdp_room_update()` guard inversion is correct, and switching `world[]` indexing from
  `ch->desc->character->in_room` to `ch->in_room` is right (matters for switched immortals).
- Sanitize insertions in `MSDPSendPair`/`MSDPSendList` occur before the `RequiredBuffer`
  computation, so size checks see the (possibly longer) escaped value.
- Sockets are `O_NONBLOCK` (`comm.cpp:1409`), so the EAGAIN paths are reachable as designed.

## Remaining work

None. All findings resolved as of 2026-07-29:

- Finding 4 deferred by author decision (single-slot-delay redesign question, out of scope) — no
  further action expected unless that redesign itself gets picked up later.
- Finding 5 marked out of scope for this PR — a behavioral fix here risks changing execution for
  live scripts, which doesn't fit a PR that's now only taking safe corrections, not new intended
  changes. Pick up separately, with its own testing pass, if it's ever done.

All commits referenced in this doc, including `eca5e51`, have been pushed to the PR.
