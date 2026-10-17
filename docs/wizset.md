# Wizset Command Reference

`wizset` (also aliased to `wiz`) is the in-game tool for changing player or
mobile state without recompiling. The handler lives in `src/act_wiz.cpp` and
expects staff to explicitly choose a target, a field, and a value. This guide
documents every supported field, its permission checks, and the side effects so
implementors can safely integrate or extend the command.

## Syntax

```
wizset [file|player|mob] <target> <field> <value>
```

- `target` resolves through `get_char_vis` unless `file`, `player`, or `mob`
  changes the lookup.
- `value` is parsed according to the field definition (binary, numeric, or text)
  and often clamped to safe ranges via `RANGE`.
- NPCs may not invoke `wizset`. Most fields additionally require the caller to
  outrank the victim (`LEVEL_GOD + 1` to modify others, plus per-field minimums).

### Target qualifiers

| Qualifier | Description |
|-----------|-------------|
| _(none)_  | Searches visible PCs or NPCs. PC-only fields reject NPC targets. |
| `player`  | Restricts lookup to PCs already in the game. |
| `mob`     | Restricts lookup to NPCs only. |
| `file`    | Loads `<target>` from disk, edits a temporary copy, and writes the results back to the player file. Only certain string fields (e.g., `password`) may be changed while using `file`. |

## Permission levels and field types

- `LEVEL_GOD` < `LEVEL_GRGOD` < `LEVEL_AREAGOD` < `LEVEL_IMPL`. Use the named
  constants when extending the table so intent stays readable.
- `BINARY` fields accept `on/off` or `yes/no`.
- `NUMBER` fields take integer input that is clamped to a hard-coded range.
- `MISC` fields consume strings or keywords; validation is field-specific.
- Fields marked `PC` reject NPC targets. `BOTH` allows either.

## Field reference

### Binary flags

| Field | Targets | Min level | Effect / Notes |
|-------|---------|-----------|----------------|
| `brief` | PC | `LEVEL_GOD` | Toggles `PRF_BRIEF`. |
| `invstart` | PC | `LEVEL_GOD` | Toggles `PLR_INVSTART` (log in invis). |
| `nosummon` | PC | `LEVEL_GRGOD` | Toggles `PRF_SUMMONABLE`; `on` prevents summons. Output is inverted so "`nosummon on`" means “not summonable.” |
| `nohassle` | PC | `LEVEL_GRGOD` (impl to change others) | Toggles `PRF_NOHASSLE` (immune to aggro, full access). |
| `frozen` | PC | `LEVEL_FREEZE` | Toggles `PLR_FROZEN` (cannot act). You cannot freeze yourself. |
| `roomflag` | PC | `LEVEL_GRGOD` | Toggles `PRF_ROOMFLAGS` to see `ROOM_x` bits while standing. |
| `siteok` | PC | `LEVEL_GRGOD` | Toggles `PLR_SITEOK` for host-based login approval. |
| `xxdeleted` | PC | `LEVEL_IMPL` | Sets `PLR_DELETED`. Prefer `delete`/`undelete` flows; this knob exists for emergencies. |
| `nowizlist` | PC | `LEVEL_GOD` | Removes/adds the character to the wizlist. |
| `trash` | PC | `LEVEL_GOD` | Placeholder; no active case in code, so calling it has no effect. |
| `color` | PC | `LEVEL_GOD` | Toggles `PRF_COLOR`. |
| `nodelete` | PC | `LEVEL_GOD` | Toggles `PLR_NODELETE` (protect account from idle cleanup). |

### Numeric fields (general stats, pools, combat tuning)

| Field | Targets | Min level | Range / Input | Effect |
|-------|---------|-----------|---------------|--------|
| `maxhit` | PC/NPC | `LEVEL_GRGOD` | `1..5000` | Sets base hit points (`constabilities.hit`). |
| `maxstamina` | PC/NPC | `LEVEL_GRGOD` | `1..5000` | Sets base stamina/energy (`constabilities.mana`). |
| `maxmove` | PC/NPC | `LEVEL_GRGOD` | `1..5000` | Sets base movement points. |
| `hit` | PC/NPC | `LEVEL_GRGOD` | `-9..current max hit` | Sets current hit points (`tmpabilities.hit`). |
| `stamina` | PC/NPC | `LEVEL_GRGOD` | `0..current max stamina` | Sets current stamina. |
| `move` | PC/NPC | `LEVEL_GRGOD` | `0..current max move` | Sets current movement points. |
| `alignment` | PC/NPC | `LEVEL_GOD` | `-1000..1000` | Adjusts morality alignment. |
| `str`, `lea`, `int`, `will`, `dex`, `con` | PC/NPC | `LEVEL_GRGOD` | `0..40` | Sets permanent stats; recalculates derived bonuses via `affect_total`. |
| `dodge` | PC/NPC | `LEVEL_GRGOD` | `-100..200` | Sets dodge bonus. |
| `parry` | PC/NPC | `LEVEL_GRGOD` | `-20..200` | Sets parry bonus. |
| `OB` | PC/NPC | `LEVEL_GRGOD` | `-20..200` | Offensive Bonus modifier. |
| `damage` | PC/NPC | `LEVEL_GRGOD` | `-20..30` | Flat damage adjustment. |
| `saving_throw` | PC/NPC | `LEVEL_GRGOD` | `-100..100` | Adjusts general saving throw modifier. |
| `specialization` | PC/NPC | `LEVEL_GRGOD` | `-100..100` | Calls `SET_SPEC` with the provided integer (used by weapon/spec systems). |
| `gold` | PC/NPC | `LEVEL_GOD` | `0..100000000` | Sets carried coin. |
| `exp` | PC/NPC | `LEVEL_GRGOD` | `0..50000000` | Raw experience total. |
| `invis` | PC only | `LEVEL_IMPL` (impl or victim) | `0..victim level` | Sets staff invisibility level. Non-impls cannot adjust others. |
| `practices`, `lessons` | PC only | `LEVEL_GRGOD` | `0..1000` | Both map to `SPELLS_TO_LEARN`; use whichever fits your vocabulary. |
| `level` | PC/NPC | `LEVEL_GOD` | `0..max level` | Directly sets the level after `advance_perm` validation (±3 levels from your own unless implementor). |
| `room` | PC/NPC | `LEVEL_IMPL` | room vnum | Instantly moves the target to `<room vnum>`. Stops mounts before teleporting. |
| `idnum` | NPC target only | `LEVEL_IMPL` | integer | Only character ID #1 may retag NPC idnums; useful for data fixes. |
| `race` | PC | `LEVEL_GRGOD` | race id | Sets `player.race`. Use the `RACE_*` constants from `src/structs.h` (e.g., 1=Human, 2=Dwarf, 3=Wood Elf, 4=Hobbit, 5=High Elf, 6=Beorning, 11=Uruk, 13=Orc, 15=Magus, 17=Olog-hai, 18=Haradrim). Follow up with appropriate equipment/language updates manually. |
| `bodytype` | PC/NPC | `LEVEL_GRGOD` | `0..MAX_BODYTYPES-1` | Alters anatomy profile used by combat tables. |
| `height`, `weight` | PC/NPC | `LEVEL_GRGOD` | integers | Physical descriptors used by size calculations. |
| `spirit` | PC/NPC | `LEVEL_GRGOD` | integer | Modifies `points.spirit` (resource for certain skills). |
| `ENE_regen` | PC/NPC | `LEVEL_GRGOD` | `-20..600` | Adjusts stamina/energy regeneration ticks. |
| `affected` | PC/NPC | `LEVEL_GRGOD` | raw bitvector | Replaces `specials.affected_by`. Dangerous: you must supply the full bitmask. |
| `coof_mage`, `coof_cle`, `coof_ran`, `coof_war` | PC | `LEVEL_IMPL` | integer | Sets profession proficiency pools for each class (used by `practice`). |
| `rp_flag` | PC | `LEVEL_AREAGOD` | integer | Custom RP flag in `specials2`. Consumers interpret specific values. |

### Text and enum fields

| Field | Targets | Min level | Accepted values | Notes |
|-------|---------|-----------|-----------------|-------|
| `title` | PC | `LEVEL_GOD` | Free-form string | Reallocates and assigns `GET_TITLE`. Use quotes if spaces should be preserved. |
| `sex` | PC/NPC | `LEVEL_GRGOD` | `male`, `female`, `neutral` | Changes `player.sex`. |
| `drunk`, `hunger`, `thirst` | PC/NPC | `LEVEL_GRGOD` | `off` or `0..24` | `off` sets the condition to -1 (never decays). Numeric sets the raw condition value. |
| `loadroom` | PC | `LEVEL_GRGOD` | `on`, `off`, or room vnum | `on/off` toggles `PLR_LOADROOM`; numeric sets `GET_LOADROOM`. Room must exist. |
| `name` | PC (online only) | `LEVEL_GRGOD` | Valid player name | Calls `rename_char`; cannot be used via `wizset file`. |
| `language` | PC/NPC | `LEVEL_GOD` | `common language` or name of a language skill | Sets spoken language. The string is matched against `skills[language_skills]`. |
| `password` | PC (file mode) | `LEVEL_IMPL` | Any string <= `MAX_PWD_LENGTH` | Only works via `wizset file <player> password <newpass>`. Stores plaintext in memory before hashing on save, so use sparingly. |
| `prof` | PC/NPC | `LEVEL_GRGOD` | _unused_ | The handler contains no `case 39`, so this field currently has no effect. |

## Usage examples

1. Make a player unsummonable and give them a new title while they are online:

   ```
   wizset player Aelwyn nosummon on
   wizset player Aelwyn title Defender of Imladris
   ```

2. Patch an NPC’s combat profile before a raid:

   ```
   wizset mob "orc captain" maxhit 3500
   wizset mob "orc captain" OB 185
   wizset mob "orc captain" damage 25
   ```

3. Fix a player who is offline by editing their file record, then forcing a save:

   ```
   wizset file Corantar password TempPass42
   wizset file Corantar loadroom 3001
   ```

4. Teleport yourself to a build room while marking it as your login location:

   ```
   wizset Self room 12345
   wizset Self loadroom on
   ```

5. Reset someone’s vitals and hunger after a crash:

   ```
   wizset player Ysolde hit 400
   wizset player Ysolde stamina 250
   wizset player Ysolde move 250
   wizset player Ysolde hunger off
   ```

6. Rename a PC to a cleared name that already passed validation:

   ```
   wizset player Cray title
   wizset player Cray name Kreh
   ```

> [!TIP] Every successful `wizset` automatically saves the character (or file)
> after applying the change. When editing a mob or crash-copy, follow up with
> manual logging if you need an audit trail.

## Best practices

- Promote/demote via `advance` when possible; use `wizset level` only for data
  repair with `advance_perm` clearance.
- Avoid `wizset affected` and the raw `coof_*` fields unless you know the bit
  layout; a mistaken mask can corrupt the character.
- Reserve `xxdeleted`, `trash`, and `prof` for debugging until their handlers
  are implemented.
- For offline edits, mirror the world save before running `wizset file *` so you
  can roll back if needed.
