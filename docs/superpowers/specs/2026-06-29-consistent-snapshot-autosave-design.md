# Consistent-Snapshot Autosave — Design

**Status:** Design (pressure-tested by a 5-lens adversarial review on 2026-06-29; awaiting user sign-off before planning)

**Goal:** Make the periodic autosave a *point-in-time consistent snapshot* so that when the server crashes during PvP or a group boss fight ("smob"), every participant is restored to the **same moment** — same room, HP, XP, stats — instead of being scattered across different save-times and locations. Secondary goals: a faster, configurable cadence, and unified crash-proofing of impactful ("exploit-recorded") events so a player cannot deliberately crash the game to undo them.

**Tech stack:** C++17, single-threaded DikuMUD/CircleMUD-derived MUD (32-bit, Linux). Build is Docker-only.

---

## Global Constraints

- **New code must be portable, cross-compilable standard C++** (`std::filesystem` with non-throwing `std::error_code`, `std::chrono`, `<fstream>`) — not POSIX-only APIs. (Project rule; new functions only.)
- **Preserve CRLF** line endings in files that already use them (`objsave.cpp`, `act_wiz.cpp`, `db.h` are CRLF; `db.cpp`, `comm.cpp`, `fight.cpp`, `limits.cpp`, `profs.cpp`, `spec_pro.cpp`, tests are LF — verify per file before editing).
- **clang-format is disabled** in this repo (`.claude/.no-autoformat`); match surrounding style by hand.
- The per-player atomic save primitives already exist and are gtested: `save_char` → `save_player` → `finalize_player_file_rename` (char file) and `Crash_crashsave` → `finalize_save_file` (object file). This design **reuses** them; it does not reimplement atomic file writes.

---

## Background — current behavior

| Mechanism | Location | Behavior today |
|---|---|---|
| Periodic autosave trigger | `comm.cpp:842-848` | Outer gate `if (!(pulse % (60*4)))` evaluates **once per real minute**; inner `++mins_since_crashsave >= autosave_time` (minutes) runs `Crash_save_all()`. Effectively every 4 minutes. |
| `Crash_save_all()` | `objsave.cpp:1687` | Loops `descriptor_list`; for each `CON_PLYNG` non-NPC with the **`PLR_CRASH` dirty bit** set, does `Crash_crashsave` (objects) + `save_char` (char; `notify=1` for mortals, `0` for imms) + clears the bit — **independently per player**. |
| `PLR_CRASH` dirty bit | set only at `handler.cpp:1224` (`obj_to_char`), `handler.cpp:1257` (`obj_from_char`) | Set **only on object gain/loss**. Never set on movement/room change, HP/damage, healing/regen, position, or affects. |
| Kill-XP save | `fight.cpp:1304-1307` | After a kill awards XP, `if (number(0,9)==0) save_char(...)` — *"save only 10% of the time to avoid lag in big groups."* |
| Level-up save | `advance_level` `profs.cpp:429` | Immediate `save_char` at end of a full level-up (after the `roll_abilities` stat reroll at :418). |
| Death save | `raw_kill` `fight.cpp:915-916` | Immediate `save_char` + `Crash_crashsave` of the dead player. |
| Exploit records | `add_exploit_record` `db.cpp:3689` → `write_exploits` `db.cpp:3591` | PC-mortal-only (guards `IS_NPC`/immortal at :3697). Writes a record to the **killer** for `EXPLOIT_PK`, to the **char themselves** for `DEATH/LEVEL/STAT/BIRTH/MOBDEATH/POISON/REGEN_DEATH/ACHIEVEMENT/RETIRED/NOTE`. `write_exploits` is called **only** from `add_exploit_record`. |
| `Emergency_save` | `objsave.cpp:1704` | Best-effort per-player save of all connected players; invoked from signal handlers incl. `SIGSEGV/SIGBUS` (`signals.cpp:121`). |

---

## The core insight (why the obvious approach fails)

The intuitive design — "make the periodic batch atomic and route XP through the `PLR_CRASH` dirty bit" — **structurally cannot deliver the stated goal.** Inclusion in the snapshot is gated on `PLR_CRASH`, which is set *only* on object gain/loss. But **location (`load_room`) and current HP — exactly what the user reports as desynced — change without ever setting the bit.** A tank or healer who, last interval, only moved and took damage but looted nothing and gained no kill-XP is **not dirty**, is skipped by the snapshot, and on recovery sits at their *previous* save's room/HP while teammates who happened to be dirty are at the fresh snapshot. The dirty-gated snapshot would be internally consistent but would leave precisely the "different people at different locations" problem intact.

**Resolution: save *all* connected players each cycle.** Because the MUD is single-threaded, one pass over `descriptor_list` inside a single heartbeat tick is a genuine atomic point-in-time capture (no game logic runs mid-loop). This is both simpler (it deletes the dirty-bit machinery and the XP-routing change) and the only thing that actually achieves consistency.

---

## Design

### 1. Snapshot = every connected player, every 30s (configurable)

- **`Crash_save_all()` drops the `PLR_CRASH` gate** and saves **every** `CON_PLYNG` non-NPC player, reusing the existing per-player atomic saves (`save_char` + `Crash_crashsave`).
  - Because players are saved **serially** (single-threaded loop) and each one fully commits before the next begins, the existing shared `players/temp` char scratch is safe **as-is** — **no per-player temp and no `save_player` split are required.** (This is the simplification that avoids the two-phase machinery, the `ch->desc` NULL-deref hazard of a new serialize helper, and the bucket-scan temp-reaping pitfall — all of which only arise if we hand-roll a batch serializer instead of reusing `save_char`.)
- **Failure policy: skip + log, never whole-batch abort.** If one player's save fails (e.g. a missing bucket dir, disk error), log it loudly *with the player's name* and continue saving the rest. The failed player simply re-converges next cycle. Rationale: under save-all, a strict all-or-none abort would let a single broken player stall **every** player's snapshot indefinitely — re-introducing server-wide staleness, the opposite of the goal. (This intentionally relaxes the originally-requested strict "all-or-none"; the consistency benefit of strict mode is marginal because the snapshot is point-in-time regardless, and the cost is severe.)
- **Silence the per-cycle notification.** `Crash_save_all` currently passes `notify=1` for mortals, which sends "Saving `<name>`." At a 30s cadence over every player that is constant spam. **Use `notify=0` for the periodic batch path.**
- **Cadence is the heartbeat gate, not just a config value.** The outer gate at `comm.cpp:842` only evaluates once per minute, so reinterpreting `autosave_time` as seconds without touching it would produce **no** sub-minute saves. Change the gate to a sub-minute pulse boundary (e.g. a `PULSE_CRASHSAVE`-style modulus; `30s = 30 * TICS_PER_SECOND = 120` pulses) and drive it from a **seconds-based config value** (default 30), clamped/rounded to the gate granularity.
  - *Configurability scope:* a single seconds-based config constant read at boot (edit + restart), consistent with every other value in `config.cpp`. A live runtime tuning command is **out of scope** (scope creep) unless explicitly requested later.

### 2. Remove the desyncing kill-XP save

- **Delete** the 10% `save_char` at `fight.cpp:1305-1306`. Do **not** replace it with a dirty-bit mark — under save-all the next ≤30s snapshot captures the new XP for everyone anyway. (The originally-proposed "mark dirty on XP" change is therefore dropped as unnecessary.)
- `PLR_CRASH` is left in place (still set on object change, still cleared by `Crash_crashsave` each cycle) but **no longer gates the snapshot**. It becomes effectively vestigial for autosave; fully removing it is a separate, optional cleanup and is out of scope here.

### 3. Anti-rollback: save on exploit-recording (generalized)

The unifying rule: **whenever a new exploit record is written for a character, immediately persist that character.** Because `write_exploits(ch, …)` is the single sink for all exploit records and is always called with the character the record belongs to, the hook is one place:

- **Add `save_char(ch, NOWHERE, 0)` at the end of `write_exploits`** (after the record is written). This automatically crash-proofs: PK trophies (saves the killer), deaths, mob/poison/regen deaths, full level-ups (`EXPLOIT_LEVEL`), birth, achievements, retire/unretire, and immortal notes — saving the correct character in each case (killer for `EXPLOIT_PK`, the char themselves otherwise). `write_exploits`/`add_exploit_record` are already PC-mortal-gated, and `save_char` independently guards `IS_NPC`/`!ch->desc`, so the hook is safe and cheap (exploits are infrequent, not a hot path).
  - **Note on `+1` stat gains (corrected per whole-branch review):** `check_stat_increase` records `EXPLOIT_STAT` *before* applying each increment, and is only ever reached via `advance_level` — so a `+1` stat gain is persisted by `advance_level`'s own trailing `save_char` (the anchor), **not** by the exploit-hook capturing the increment (the hook would save the pre-increment state). Outcome is correct, but the protection is load-bearing on that anchor: a future caller of `check_stat_increase` without a trailing save would leave a stat gain rollback-able for up to one cadence. *Optional hardening (out of the original plan's scope):* move the `add_exploit_record(EXPLOIT_STAT…)` calls in `check_stat_increase` to *after* each increment so the hook itself captures the gain.
- **Angel reroll** (`spec_pro.cpp:2691`): records **no** exploit, so the `write_exploits` hook won't catch it — yet it calls the *same* `roll_abilities` stat reroll that a level-up makes crash-proof, and a player can crash to redo a bad roll and refund a capped attempt. **Add a direct `save_char(ch, NOWHERE, 0)` immediately after the `rerolls` increment** — with **no** exploit record (rerolls are too frequent to log without noise; per user decision). This closes the crash-to-reroll exploit on its own, independent of the exploit hook. It is the one impactful event protected by a direct save rather than via the exploit-recording hook.
- The existing explicit `save_char` calls in `raw_kill` (death) and `advance_level` (level-up) become partially redundant with the hook but are **kept** as reliable anchors (cheap atomic saves; removing them is unnecessary risk). The dead player's own save must stay in `raw_kill` regardless, because `EXPLOIT_DEATH` is recorded for the victim but `EXPLOIT_PK` (the trophy) is recorded for the killer — the hook covers both, and `raw_kill` remains the guaranteed death persistence.
- **`do_pracreset`: no save** — confirmed not exploitable (per user).

### 4. `Emergency_save` is unchanged

Leave it best-effort and per-player. It runs from `SIGSEGV/SIGBUS` signal handlers where the heap may already be corrupt; adding `std::vector`/`std::filesystem` batch machinery there is async-signal-unsafe and risks a secondary fault that loses *more* players. Point-in-time consistency is the periodic snapshot's job; the emergency path's job is to salvage as many players as possible.

---

## Edge cases & decisions

| Scenario | Handling |
|---|---|
| A player's save fails mid-cycle (missing bucket dir, disk error) | Skip + log loudly with the player name; continue; player re-converges next cycle. |
| One of a player's two files commits, the other fails (char rename OK, object rename fails — different buckets/dirs) | Accepted residual: that one player is internally split for one cycle (char at T, objects at T−1). Log the partial failure; the player re-converges next cycle. Rare (rename failure, not a crash); same risk class as the accepted crash-mid-rename window. |
| Link-dead participant (`CON_LINKLS`) | **Deliberately not snapshotted** — the `CON_PLYNG` filter in `Crash_save_all` excludes them. Note (corrected): RotS does *not* null `ch->desc` on link-loss — the `d->character->desc = 0;` detach in `close_socket` (comm.cpp:1689) is commented out, and the descriptor lingers in `descriptor_list` marked `CON_LINKLS` — so `save_char` would actually work for them. Including them would therefore be a one-line filter change (accept `CON_LINKLS`), **not** the `character_list`-iteration / `ch->desc`-decoupling rewrite an earlier draft assumed. It is left out by choice: a link-dead character was already saved at link-loss, and its death and idle-out paths save it directly, so it does not need the periodic snapshot. |
| Configured cadence finer than the gate granularity | Clamp/round the configured seconds up to a multiple of the gate's pulse boundary. |
| "Same moment" definition | Means same **room + HP + XP + stats** — combat posture/position is not persisted today and is **out of scope**. |
| Legitimate mid-fight level-up / death | Their immediate (exploit-hook + anchor) save commits them at the level-up / respawn moment, so they intentionally do **not** match the group's pre-event location. This is desired anti-exploit behavior, not the desync being fixed. |
| Orphan temps | Save-all reuses the existing per-player save paths (object temp `…/<name>.obj.tmp`, shared `players/temp`), whose finalize already consumes them; no new temp scheme is introduced, so no new orphan class. |
| Mini-level `+1` HP roll (`advance_mini_level`) | Now persisted automatically by the next snapshot (under the old dirty design it was silently never saved — a latent bug save-all removes). No immediate save. |

---

## Testing

Reuse the existing gtest harness (`src/tests/`). The atomic file primitives are already covered; new coverage targets the new behavior:

1. **Snapshot includes everyone:** `Crash_save_all` saves all connected non-NPC players regardless of `PLR_CRASH` (not just dirty ones).
2. **Skip-on-failure:** a simulated single-player save failure leaves that player unsaved + logged while the others are saved (no whole-batch abort).
3. **No notify in batch path:** the periodic save path passes `notify=0`.
4. **Exploit→save hook:** writing any exploit record for a character triggers a `save_char` of that character (and of the killer for `EXPLOIT_PK`).
5. **Cadence gate:** the heartbeat gate fires at the configured sub-minute interval (logic-level test of the pulse math / clamping).

Where a behavior needs a live `char_data`/`descriptor` (the batch loop), prefer a thin seam that can be unit-tested over a full integration harness; otherwise validate on the dev server (`dev-coding4810`) with the runtime log watcher, as with the prior save work.

---

## Out of scope

- Live runtime cadence-tuning admin command (config + restart only).
- Including link-dead (`CON_LINKLS`) players in the snapshot — a deliberate exclusion. (It would be a one-line filter change, since `ch->desc` is retained on link-loss, but link-dead players are already covered by their link-loss save plus the death/idle-out saves, so they are intentionally left out of the periodic snapshot.)
- Persisting combat posture/position.
- Fully removing the now-vestigial `PLR_CRASH` bit.
- Converting `Emergency_save` / the shutdown loop to the snapshot path.
- `do_pracreset` save (not exploitable).

---

## Summary of code touch-points (for planning)

| Change | File / area |
|---|---|
| Save-all + skip-on-failure + `notify=0` | `Crash_save_all` `objsave.cpp:1687-1702` |
| Sub-minute configurable gate | `comm.cpp:842-848`; `autosave_time` `config.cpp:40`; pulse consts `structs.h` |
| Delete 10% XP save | `fight.cpp:1304-1307` |
| Exploit→save hook | end of `write_exploits` `db.cpp:3591` |
| Angel-reroll direct `save_char` (no exploit record) | `spec_pro.cpp:~2691` |
| (unchanged, by design) | `Emergency_save` `objsave.cpp:1704`; `raw_kill` save `fight.cpp:915`; `advance_level` save `profs.cpp:429` |
