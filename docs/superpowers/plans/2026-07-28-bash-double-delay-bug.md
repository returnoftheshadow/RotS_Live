# The "Bash Double-Delay" Bug — Detailed Description

**Status:** Open — needs the author's design-intent decision (this is PR #276 review Finding 4,
`docs/superpowers/plans/2026-07-27-pr-276-adversarial-review-handoff.md`). Not a "does it crash"
bug; it's a silent-drop / misleading-message bug with an unresolved "which action should win"
question underneath it.

**Origin:** long-standing player reports of a "double delay" glitch, believed tied to bashing (or
otherwise interrupting) a target that's mid-cast or otherwise busy. Investigated during the
core-server-health latency audit.

## The mechanism

`ch->delay` is a **single slot** (not a queue) holding one pending delayed action plus a
`priority`. `WAIT_STATE_BRIEF` / `WAIT_STATE_FULL` (`src/utils.h:482-609`) are the only way
anything gets written into that slot, and they resolve a collision — a new action arriving while
`ch->delay.wait_value != 0` — purely by comparing the new action's priority against the pending
one's:

```
if (ch->delay.wait_value != 0) {
    if (prir >= ch->delay.priority) {
        ch->delay.subcmd = -1;
        complete_delay(ch);              // <-- forces the OLD action to run NOW, synchronously
        if (ch->delay.wait_value != 0) { // <-- did completing it queue ANOTHER delay?
            "Possible bug - double delay. Please notify Imps."
            break;                        // <-- new action's effect is discarded entirely
        }
        abort_delay(ch);
        // falls through to apply the NEW delay normally
    } else {
        "Possible bug - double delay. Please notify Imps."
        break;                            // <-- new action silently dropped, old delay untouched
    }
}
// ... write the new delay into ch->delay ...
```

`complete_delay()` (`src/comm.cpp:2453`) zeroes `wait_value`, then **re-dispatches the original
delayed command inline**, synchronously, via `command_interpreter(ch, "", &(ch->delay))` (or
`continue_char_script()` / a mob special-proc callback for `CMD_SCRIPT` / NPC delays) — i.e. it
runs the old queued command's actual handler *right there*, reentrantly, before the new action's
own `WAIT_STATE_*` call has finished writing its own delay into `ch->delay`.

Concretely, for the classic "bash a casting target" scenario:

- Spell casting queues its delay at **priority 30** (`src/spell_pa.cpp:750`, `:1117`).
- Bash's stun, applied directly to the victim, queues at **priority 80** (`src/act_offe.cpp:575`,
  `:675`; `src/ranger.cpp:1310`; `src/olog_hai.cpp:48`).

So bashing a casting target always takes the `prir >= ch->delay.priority` branch (80 ≥ 30):
`complete_delay()` force-fires the spell early/out-of-turn (instead of the spell being cleanly
interrupted), and *then* the code checks whether that force-completion itself queued a new delay.

## Two structurally different sub-cases — this is the part that's easy to conflate

The priority check only decides **whether an override is attempted at all**. Once it is, there are
two genuinely different outcomes depending on what happens *inside* `complete_delay()`:

### A. Clean override (confirmed working, 24/24 live samples)

If the interrupted command's handler, when force-run by `complete_delay()`, does **not** itself
call `WAIT_STATE_*` again — i.e. it's a one-shot effect with no follow-up delay — then
`ch->delay.wait_value` is still `0` after `complete_delay()` returns. Execution falls through to
`abort_delay()` and then applies the new action's delay (bash's stun) normally. This is the
everyday, working case.

### B. Reentrant collision (never exercised, this is Finding 4)

If the interrupted command's handler **does** itself call `WAIT_STATE_BRIEF`/`WAIT_STATE_FULL`
again as part of finishing — e.g. a spell that auto-queues a post-cast recovery/cooldown delay, or
a multi-stage skill that queues its own next stage — then `ch->delay.wait_value` is non-zero again
when the outer macro rechecks it. The code interprets this as a bug, logs/prints "Possible bug -
double delay. Please notify Imps.", and `break`s out **without ever applying the new action's
delay or its `SET_BIT` flag**. Concretely: **the bash stun is silently never applied to the
victim**, while the mage's own reentrant follow-up delay is left in place, untouched — and the
player sees a message implying something went wrong, even though this is (arguably) legitimate
reentrant behavior, not a bug.

### C. New action outright rejected (the `else` branch, unambiguous)

If the new action's priority is *lower* than the pending delay's, the new action is silently
dropped, the pending delay is untouched, and the same "Possible bug" message is shown. This branch
is not really in question — a lower-priority action losing to a higher-priority pending one is
expected — but it uses the same misleading message as case B.

**The key point:** priority (80 vs 30) only determines whether branch A or C is taken. Whether you
land in A's clean path or A's nested reentrant path (B) depends entirely on whether completing the
*interrupted* action has a side effect of queuing a *new* delay for itself — a property of that
specific skill/spell's implementation, unrelated to the interrupting action's priority.

## What was actually tested (2026-07-28) vs. what wasn't

Live-tested via a level-40 warrior (`Bashtest`) repeatedly bashing an NPC dark mage (vnum 12600,
casting) in an isolated test room (vnum 1120, "The Arena" — see
[[rots_test_arena_room_1120]]/[[rots_test_char_bash_tester_profile]] in auto-memory). Instrumented
`WAIT_STATE_BRIEF`/`WAIT_STATE_FULL` (`src/utils.h`) and `do_bash` (`src/act_offe.cpp`) with
temporary `DBLDELAY-DIAG` log lines capturing old/new cmd, subcmd, priority, and wait_value on
every collision.

**Result: 24/24 samples, across 4 different spell subcommands** (`cmd=66` subcmd 71/75/78,
`cmd=90` subcmd 2), all landed in branch **A's clean path**: every sampled delay was priority 30
(matching the casting citations above) against bash's 80, `complete_delay()` fired the spell
cleanly, no reentrant queue, the stun applied correctly every time. Zero "reentrant queue during
complete_delay" warnings, zero low-priority "double delay" warnings.

**This confirms branch A's clean path works. It does not exercise branch B (Finding 4's actual
concern) or branch C:**
- **Branch B** needs a skill/spell whose own completion routine reentrantly calls
  `WAIT_STATE_BRIEF`/`FULL` when force-completed early. This mage's spells don't appear to. Whether
  *any* live skill/spell does this hasn't been established — it would require either a source
  audit of every `WAIT_STATE_*` call site to see which ones sit inside a completion/effect routine
  that could be reached via `complete_delay()`'s `command_interpreter()` re-dispatch, or catching a
  real occurrence via the diagnostic logging described below.
- **Branch C** needs the *victim's* existing delay to have priority ≥ 80 (bash's priority) — never
  encountered, since every sampled delay was priority 30.

## Current state of the fix/investigation

- **No code fix applied yet** — this needs a design decision first (see below), not more
  investigation.
- **Diagnostic logging is in place but uncommitted** (`src/utils.h`, `src/act_offe.cpp`,
  `DBLDELAY-DIAG` lines) — deliberately left in so that if/when a real branch-B or branch-C
  occurrence happens on a live server, the logs will already capture old/new cmd, subcmd, priority,
  and wait_value in enough detail to diagnose it without needing to reproduce it synthetically
  first.
- **Decision (2026-07-28, per user):** pause further synthetic reproduction attempts (e.g. hunting
  for a player-castable spell with a reentrant follow-up, or engineering a priority ≥ 80 collision)
  in favor of waiting for a real player-reported occurrence, now that the logging exists to catch
  one.

## Open question for the author (unchanged from Finding 4)

1. **When branch B fires, which delay should win?** Current behavior: the interrupted action's own
   reentrant follow-up wins, the interrupting action's delay/effect is dropped entirely. For
   PvP bash specifically, silently dropping the stun seems like the wrong outcome — but this is a
   gameplay/design call, not something the code alone can answer.
2. **Should the player-facing "Possible bug - double delay. Please notify Imps." message be
   removed or made log-only on the branch-B path**, given it's arguably legitimate reentrant
   behavior rather than an actual bug? (Branch C's use of the same message is less contentious —
   that one really is just "your lower-priority action lost.")

A real fix, once the intended behavior is decided, likely needs either: replacing the single
`ch->delay` slot with an actual queue (bigger change), or adding explicit
interrupt/cancel-and-replace semantics for branch B instead of the current "silently keep the old,
discard the new" resolution.

## Reference

- PR #276 review Finding 4:
  `docs/superpowers/plans/2026-07-27-pr-276-adversarial-review-handoff.md` (lines ~139-161)
- Manual test checklist, Task 10 section:
  `docs/superpowers/plans/2026-07-24-core-server-health-update-manual-test-checklist.md`
- Auto-memory tracking entry: `double_delay_bug` (exact log evidence, test methodology)
