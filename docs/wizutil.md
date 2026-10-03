# Wizutil Command Reference

`wizutil` centralizes staff utilities that operate on a single player record.
The implementation lives in `src/act_wiz.cpp` (`wizutil_options[]` and
`ACMD(do_wizutil)`) and exposes the same behaviors that also back the legacy
commands (`freeze`, `thaw`, `notitle`, etc.). Use this document to understand
permissions, side effects, and the expected argument order.

## Syntax

```
wizutil <field> <player> [message]
```

- `<field>` must match one of the entries in the table below. The handler
  already strips aliases like `freeze`, so typing `freeze <player>` directly
  from the command line resolves to the same code path and respects the
  per-field minimum level.
- `<player>` must be an online PC; NPC targets are rejected.
- All successful actions end with a `save_char` to persist changes.
- Callers must outrank the victim (no modifying higher-level staff).

### Option summary

| Field | Subcommand | Min level | Description |
|-------|------------|-----------|-------------|
| `reroll` | `SCMD_REROLL` | `LEVEL_GRGOD` | Re-rolls the target’s base stats (80–93 range) and recalculates practice sessions. |
| `pardon` | `SCMD_PARDON` | `LEVEL_GOD + 1` | Stubbed; early-returned and unused. |
| `freeze` | `SCMD_FREEZE` | `LEVEL_FREEZE` | Sets `PLR_FROZEN` so the target cannot act. Stores who froze them. |
| `notitle` | `SCMD_NOTITLE` | `LEVEL_GOD` | Toggles `PLR_NOTITLE`, preventing self-edited titles. |
| `thaw` | `SCMD_THAW` | `LEVEL_FREEZE` | Removes `PLR_FROZEN`, respecting the original freezer’s level. |
| `unaffect` | `SCMD_UNAFFECT` | `LEVEL_GOD + 1` | Strips every active spell/affect. |
| `retire` | `SCMD_RETIRE` | `LEVEL_GOD + 1` | Marks `PLR_RETIRED` and runs `retire(vict)`. |
| `reactivate` | `SCMD_REACTV` | `LEVEL_GOD + 1` | Clears retirement state via `unretire`. |
| `rehash` | `SCMD_REHASH` | `LEVEL_GOD` | Invokes `do_rehash` to rebuild the affect cache. |
| `noshout` | `SCMD_SQUELCH` | `LEVEL_AREAGOD` | Toggles `PLR_NOSHOUT`, muting/unmuting public channels. |
| `note` | `SCMD_NOTE` | `LEVEL_AREAGOD` | Records an exploit note against the target. Message required. |

## Behavior details

### reroll

- Re-rolls base abilities via `roll_abilities(vict, 80, 93)`.
- Announces to the victim and logs `(GC) <wiz> has rerolled <vict>`.
- Calls `update_available_practice_sessions()` so their skill pool matches the
  new stats. Use when unwedging bad stat lines, not as a reward machine.

### freeze / thaw

- `freeze` cannot target yourself and refuses if the player is already frozen.
- Sets `vict->specials2.freeze_level` to capture your level for later checks.
- `thaw` ensures the player is frozen and the thawing wizard’s level is at least
  what was recorded. This prevents lower-level staff from undoing higher-level
  punishments.
- Both commands broadcast immersive room messages and log `(GC)` entries.

### notitle

- Flips `PLR_NOTITLE`. When `on`, the player cannot change their title via `title`.
- Logger: `(GC) Notitle on/off for <vict> by <wiz>`.

### noshout

- Toggles `PLR_NOSHOUT` and logs to BRF level.
- Effectively silences tells, auctions, and other broadcast channels that check
  the flag. Unlike banishment, the player may still speak in-person.

### unaffect

- Runs `affect_remove` until the victim has no spells/effects.
- Use to clear stacking effects or when a bug leaves someone permanently buffed.
- Prints feedback to both parties.

### retire / reactivate

- `retire` calls `retire(vict)` and sets `PLR_RETIRED`. Once flagged, only
  implementors at `LEVEL_GOD + 1` or higher routing through `reactivate` can
  reverse the status.
- `reactivate` checks the flag before calling `unretire`.
- Both commands broadcast `(GC)` log lines for auditing.

### rehash

- Simply calls `do_rehash` then prints “Ok, rehashed.” to the caller.
- Use after editing affect tables or `world[].affected` entries so live pointers
  match disk state. No victim-specific change occurs.

### note

- Syntax: `wizutil note <player> <message>` or `note <player> <message>`.
- The handler skips the first two tokens and expects a non-empty, alphabetic
  message start; otherwise it prints `usage: wizutil note <player> <message>`.
- Notes are stored via `add_exploit_record(EXPLOIT_NOTE, vict, 0, message)` and
  logged. Ideal for leaving context about investigations or punishments.

### pardon (unused)

- Present for historical reasons but immediately returns before doing anything.
- Prefer the dedicated justice commands (`noshout`, `freeze`, etc.) or modify
  the player file directly if you truly need to lift flags.

## Usage examples

```
wizutil reroll Shelyn
wizutil freeze Rowtag
wizutil thaw Rowtag
wizutil unaffect Zerrin
wizutil retire Oldtimer
wizutil reactivate Oldtimer
wizutil noshout Spammer
wizutil note Spammer Posting offensive chat logs in global.
wizutil rehash dummy
```

> [!NOTE] `wizutil rehash dummy` ignores the “player” value; the handler only
> needs a valid `TARGET_CHAR` pointer. Most staff run the standalone `rehash`
> command instead, but the option exists for parity.

## Best practices

- Always confirm identities via `stat <player>` before freezing, retiring, or
  rerolling so you do not punish the wrong target.
- When writing notes, include timestamps and references to logs/screenshots in
  your message for future investigators.
- The `freeze`/`thaw` pair should be paired with out-of-band communication (e.g.
  email or forum post) so players know what happened and what to fix.
- Because `wizutil` enforces level thresholds, ensure your immortal promotions
  match the duties you expect (e.g., give `LEVEL_FREEZE` only to staff trusted
  with lockdown powers).
