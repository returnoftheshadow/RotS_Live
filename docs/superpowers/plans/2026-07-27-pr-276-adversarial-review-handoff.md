# PR #276 Adversarial Review — Agent Handoff

**PR:** https://github.com/returnoftheshadow/RotS_Live/pull/276
**Title:** Core server-health update: MSDP, socket/latency, combat reentrancy, scripting fixes
**Author:** `ahumbert` | **Base:** `release-frodo` | **Head:** `core-server-health-update`
**Review date:** 2026-07-27 (adversarial review by Claude; findings verified against base-branch source, not just the diff)

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

## [FIX] 1. `g_skip_next_before_enter_for` can dangle → silently skips ON_BEFORE_ENTER on a later, unrelated move

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

---

## [FIX] 2. EAGAIN deferral loses the pending leading newline — reintroduces the glued-prompt bug on the exact path built to handle it

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

---

## [ASK AUTHOR] 3. `MSDPSend` → `MSDPFlush` silently adds a REPORT requirement (client-visible protocol change)

**Files:** `src/act_move.cpp` (`msdp_room_update()`, ROOM_EXITS), `src/weather.cpp`
(`another_hour()` WORLD_TIME; deleted `weather_change()` WEATHER push).

**Facts (verified in `src/protocol.cpp`):** `MSDPSend` (line ~1178) pushes to any logged-in
client with `PRF_MSDP` + `bMSDP`, regardless of report status. `MSDPFlush` (line ~1164)
additionally requires `bReport && bDirty`. Therefore:

- `ROOM_EXITS` (per move), `WORLD_TIME` (per mud-hour), and `WEATHER` (now only delivered via the
  `bReport`-gated `MSDPUpdate` sweep) are **no longer sent at all to clients that never issued
  `REPORT <var>`**. Previously these were unsolicited pushes.
- The `bDirty` gate also means moving between two rooms with identical exit strings sends nothing
  (value unchanged) — the PR checklist's "exactly once per move" is really "at most once per move."

**Question for the author:** *Is removing the unsolicited-push behavior intentional? Clients that
negotiate MSDP but rely on pushes without REPORTing (rather than the REPORT/dirty model) will go
silent for these variables. If intentional, the PR description should say so (it currently frames
this purely as dedup), and client authors should be notified. If not intentional, the dedup needs
a different shape (e.g. send-then-clear-dirty without the bReport gate).*

Context in the change's favor: the `ROOM` table itself was already `bReport`-gated via
`MSDPUpdate`, so the raw pushes were the anomaly. This is a defensible cleanup — but it must be a
*decision*, not a side effect.

---

## [ASK AUTHOR] 4. WAIT_STATE reentrancy guard drops the *new* action's delay and shows players a "Possible bug" message on a now-legitimate path

**File:** `src/utils.h`, `WAIT_STATE_BRIEF` and `WAIT_STATE_FULL` macros (PR's version).

**Facts (verified):** `complete_delay()` (`src/comm.cpp:2327`) zeroes `wait_value` first, so the
new `if (ch->delay.wait_value != 0)` recheck correctly detects a reentrantly-queued delay. But
when it trips, the macro `break`s out: the interrupted action's follow-up delay **wins**, and the
incoming action's delay is never applied — e.g. in bash-interrupts-cast, the spell's recovery
delay survives and **the bash stun is silently not applied to the victim**. `SET_BIT(...,
new_flag)` is also skipped. Meanwhile the player still sees
`"Possible bug - double delay. Please notify Imps."` for what the PR's own comment describes as
expected reentrant behavior.

**Questions for the author:**
1. *When a reentrant delay collides with a new action's delay, which should win? The current code
   keeps the reentrant (spell-recovery) delay and drops the new one (bash stun) — is that the
   intended gameplay outcome? For PvP bash specifically, dropping the stun seems wrong.*
2. *Should the player-facing "Possible bug … notify Imps" message be removed/log-only on this
   path, given it's now recognized-legitimate?*

**Minor (fix without asking):** `WAIT_STATE_FULL`'s copy of the guard lacks the explanatory
comment that `WAIT_STATE_BRIEF`'s has — add it.

---

## [ASK AUTHOR] 5. `get_next_command()` still mis-skips nested if/**else** blocks (fix incomplete)

**File:** `src/script.cpp`, `get_next_command()` (~line 759).

**Facts (verified against caller convention, e.g. `script.cpp:1121`):** the double-advance fix is
correct — callers pass the `BEGIN`, the function scans from `BEGIN->next`, and the rewrite no
longer skips the element after a nested block. **But** the recursive skip stops at the *first*
`SCRIPT_END` **or** `SCRIPT_END_ELSE_BEGIN`. For a *nested* construct
`BEGIN … END_ELSE_BEGIN … END` inside a region being skipped, the recursion returns into the
nested else-body; the outer scan then treats the nested `END` as the outer terminator and resumes
execution **mid-block** — executing code that should have been skipped.

This is **pre-existing** (the old code had the same stop condition), not introduced by the PR.

**Question for the author:** *The PR claims to fix skipping past nested if/begin/end blocks — do
any live scripts nest an if-with-else inside a skippable block (e.g. is vnum #1140's shape
if-only or if/else)? If else-nesting occurs in the wild, the recursion needs a "skip both halves"
mode: when the recursive call hits `END_ELSE_BEGIN`, continue scanning to the matching `END`
rather than returning. Should that follow-up land in this PR or a separate one?*

---

## [ASK AUTHOR] 6. Claimed autorun backoff fix is not in the PR

**File:** `.gitignore` (adds `autorun`).

**Facts (verified):** `autorun` is untracked (`git ls-files` confirms), and this PR adds it to
`.gitignore`. The PR description's "5s backoff before autorun restarts the game process" exists
only as an unversioned file on the server — a described production behavior with no source of
truth in the repo.

**Question for the author:** *Should the autorun script be committed (e.g. as `autorun.example`
or under `scripts/`), or should the backoff claim be dropped from the PR description? As-is, the
change can't be reviewed and would be lost if the server is rebuilt.*

---

## [FIX] 7. Committed credentials + email-verification bypass recipe in the test checklist

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

---

## Minor items (fix or note, author query optional)

- **Prompt writes ignore `write_to_descriptor` returns while setting `bare_prompt_pending`**
  (`src/comm.cpp`, "give the people some prompts" block): if a prompt write itself returns `-2`
  (silently dropped), the flag claims a bare prompt was written → spurious leading blank line on
  the next flush. Cosmetic; fix by only setting the flag when the write returns > 0.
- **Proxy `set_nodelay(true)?`** (`proxy/src/main.rs`): the `?` propagates a (rare) nodelay
  failure and kills the connection. The C side logs via `perror` and continues; best-effort
  (log-and-continue) would match.

---

## Verified non-issues — do not re-raise

These were adversarially checked during the review and found sound:

- `last_input_time` is initialized to `login_time` at descriptor creation (`comm.cpp:1470`), so
  `check_pre_login_idle()` cannot insta-reap fresh connections.
- `close_socket(point)` with default `drop_all=TRUE` is safe for character-less descriptors
  during the reap iteration (`next_point` captured first; only the passed descriptor is freed).
- `weather_and_time()` (game_loop line ~1040) runs before `msdp_update()` (~1054), so deleting
  `weather_change()`'s push adds no latency *for REPORTing clients* (see finding 3 for the rest).
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

## Suggested order of work

1. Fix findings 1 and 2 (verified bugs; both narrow-window but each defeats its surrounding fix).
2. Post the author questions for findings 3–6 in one batch; proceed per answers.
3. Scrub finding 7 immediately (docs-only, no behavior risk).
4. Sweep the minor items last.
