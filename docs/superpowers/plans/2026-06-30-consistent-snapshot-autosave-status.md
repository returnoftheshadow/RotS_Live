# Consistent-Snapshot Autosave (Phase 2) — Implementation Status

**Branch:** `feature/savebench-port` · **Date:** 2026-06-30 · **Status:** behavioral changes implemented and compiling; the cadence reduction (Task 2.5) is **gated** and the snapshot behavior needs a **running-server validation** (see §3).

This is Phase 2 of `docs/superpowers/plans/2026-06-29-followup-account-perf-and-consistent-autosave.md` (§3). Phase 1 (the account cache + JSON perf) is built/profiled but **switched off** awaiting sign-off — see `docs/superpowers/plans/2026-06-30-perf-work-handoff.md`.

---

## 1. What landed

The periodic autosave is now a **point-in-time snapshot** — one heartbeat pass saves **every** connected player — plus anti-rollback hooks so impactful events persist immediately. Commits:

| Commit | Task(s) | Change |
|---|---|---|
| `5023ddc` | 2.1, 2.3c, 2.4 | `Crash_save_all` de-gated to save every `CON_PLYNG` non-NPC each cadence (no `PLR_CRASH` gate), `notify=0` for all (no "Saving X." spam), skip-and-log a broken descriptor; removed the per-kill 10% XP save in `group_gain`; documented the `CON_LINKLS` exclusion + updated the heartbeat comment. |
| `df537f4` | 2.2 | Persisted `PLR_CRASH` is masked off on load (`apply_character_data_to_store`); `kPlayerFlags` "crash" entry kept so existing files still decode. New `CharacterJson.StripsPersistedPlrCrashFlagOnLoad` test. |
| `02d7a54` | 2.3a, 2.3b | `save_char` after a **confirmed** exploit write (`write_exploits`), gated on success only; direct `save_char` after an angel reroll (`resetter`). |

**Anchors preserved (not changed):** the death save in `raw_kill` (`fight.cpp`) + its `Crash_crashsave`, and the level-up save in `advance_level` (`profs.cpp`, honoring `should_defer_account_backed_birth_persistence`). `save_char`'s signature is unchanged; all hooks rely on its `IS_NPC`/`!ch->desc` guard.

## 2. Verification done

- **Compiles clean** (Docker i386 `ageland_tests` + `ageland`): all six touched files (`objsave.cpp`, `fight.cpp`, `character_json.cpp`, `db.cpp`, `spec_pro.cpp`, `comm.cpp`).
- **62/62 unit tests pass** (`CharacterJson.*` incl. the new PLR_CRASH test, `CrashsaveSchedule.*`, plus the Phase-1 suites as regression); the only red is the **pre-existing** 32-bit baseline `JsonUtils.RejectsIntegersOutsideIntRange`.
- **Line endings preserved:** `objsave.cpp` and `spec_pro.cpp` are CRLF and stayed CRLF (edited via a `\r\n`-preserving script); the LF files stayed LF.

## 3. NOT done / needs a running-server check (intentional)

### 3a. Task 2.5 — cadence reduction is GATED
`autosave_time` is **left at 240s (4-minute cadence)** — unchanged. Lowering it toward the source's 30s is gated on **Phase-1 cache adoption + a re-measure**, because the save-all snapshot runs the per-player object save (and, until the cache is adopted, the 3×-per-save account scan) for *every* connected player each cadence. Size the new cadence as `per-player µs × max connected players` comfortably inside the heartbeat budget (single-threaded, 250 ms/pulse at `TICS_PER_SECOND=4`) using **post-adoption** numbers. At 240s the current per-pass cost is comfortably absorbed, so the snapshot behavior ships safely now; only the tighter cadence waits.

**Cost note (carry into the sizing):** every player now runs `Crash_crashsave` each cadence; `Crash_crashsave`/`idlesave`/`rentsave` still truncate-in-place the legacy `lib/plrobjs` file and read it back before pushing through the account-native atomic writer — a double write + read per player. The Phase-1 cache addresses the *account-read* half, not this *object-write* half; watch it when sizing the cadence.

### 3b. Snapshot behavior must be validated on a running server
The de-gate, anti-rollback hooks, and link-dead exclusion are coupled to live `descriptor_list`/global state and are **not unit-testable**. On a test instance (the local i386 container + `drelibench`), confirm:

- [ ] **Save-all:** every connected player is saved each cadence, not just inventory-dirty ones (e.g. log/inspect `lib/account_characters` mtimes after a cadence).
- [ ] **No spam:** no "Saving X." messages during a routine snapshot (notify=0).
- [ ] **Exploit persists immediately:** trigger an exploit record (PK/death/level/stat) and confirm the character file is rewritten right then (not only at the next cadence).
- [ ] **Reroll persists immediately:** an angel reroll (level-6 resetter) rewrites the character file at reroll time.
- [ ] **Stale PLR_CRASH harmless:** a character that persisted "crash" loads with the bit cleared and behaves normally.
- [ ] **Link-dead excluded:** a `CON_LINKLS` (lost-link) player is not re-saved by the snapshot (it was already flushed on link loss).

Prefer the **test instance** over the live server for these checks.

## 4. Relationship to Phase 1 (perf)

Phase 2's behavioral changes are independent of Phase 1 and ship now. **Only Task 2.5 (cadence) is gated on Phase-1 cache adoption** — do not lower the cadence until the cache is adopted live and re-measured. See the perf handoff (`2026-06-30-perf-work-handoff.md` §5) for the same coupling from the other side.
