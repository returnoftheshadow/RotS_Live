# XP gain and loss: every path in the live server

Traced 2026-09-19 on branch `fix/spell-room-affect-uaf-port` (HEAD ec0f7d5). Line numbers refer to that
commit. The `id` column is the name Task 3 gives the Python mirror and Task 4 gives the C++ pin.

Constants: `LEVEL_MAX = 30` (legend), `LEVEL_IMMORT = 91`, `LEVEL_IMPL = 100` (`src/structs.h:45-52`);
`average_mob_life = 40` mud hours (`src/config.cpp:33`); `SECS_PER_MUD_HOUR` drives `MOB_AGE_TICKS`
(`src/utils.h:677`).

## Event table

| id | trigger | who | formula (as code) | modifiers | clamps | source |
| --- | --- | --- | --- | --- | --- | --- |
| `xp_to_level` | level threshold | PC | `lvl * lvl * 1500` | none | none | `src/limits.cpp:90` |
| `levelb` | effective level for kill share and `attacked_level` | PC | `min(level, LEVEL_MAX * 2 / 3 + level / 3)` = `min(level, 20 + level / 3)`; NPCs use raw level | none | none | `src/utils.h:315` |
| `gain_exp_clamp` | every gain routed through `gain_exp` | PC below 91 | positive: `min(7000, gain)` only while `level < 90`; negative: `max(-10000, gain)` while `level < 91` | none | 7000 per event up, 10000 per event down | `src/limits.cpp:410-426` |
| `delevel` | any negative `gain_exp_regardless` | PC | `while xp_to_level(level) - 20000 > exp: level -= 1; mini_level = 100 * level; practices -= PRACS_PER_LEVEL + lea_base / LEA_PRAC_FACTOR` | 20000-point tolerance below the floor | exp floored at 0 | `src/limits.cpp:456-472` |
| `mini_level` | any positive `gain_exp_regardless` | PC | advance while `m * m * 3 / 20 <= exp` (mini level `m`); each advance may add 1 max HP at 2 percent, then **raises the character level** when `xp_to_level(level + 1) <= exp` (`level += 1; advance_level`), and advances profession levels by coefficient | none | none | `src/limits.cpp:92-118, 444-453` |
| `hit_xp_melee` | every damage event that passes `damage_credited`'s guards, any attack type routed through `damage()` (melee, weapon skills, spells) | attacker != victim; NPC attackers are dropped inside `gain_exp_regardless` | `(1 + L_victim) * min(20 + 2 * L_attacker, dam) / (1 + L_attacker)` | none | `gain_exp_clamp` per hit | `src/fight.cpp:2097-2099` (inside `damage_credited`, `:1836`; `damage()` at `:2208` forwards to it) |
| `hit_xp_mental` | a successful mental attack (`do_mental`; also fired for NPCs fighting shadows, `src/fight.cpp:3037-3039`, where the NPC guard drops it) | attacker; NPCs dropped inside `gain_exp_regardless` | same as `hit_xp_melee` with `dam` replaced by `damg * 5` | none | `gain_exp_clamp` per attack | `src/clerics.cpp:226` (in `do_mental`, `:89`) |
| `kill_share` | `group_gain` after a death | every PC in the death room who is fighting the victim, is the victim's target, is the credited killer, is in such a fighter's group, or is the master of an orc-friend, pet or guardian that is fighting; immortals skip | see "kill share" below | `attacked_level` malus, group split | via `exp_with_modifiers` then `gain_exp_clamp` | `src/fight.cpp:1407-1553` |
| `exp_with_modifiers` | applied to each killer's `base` from `kill_share` | PC | ordered list below | level gap, mob age, mob flags, alignment, difficulty, east bonus, low-level bonus | none of its own | `src/fight.cpp:1334-1390` |
| `flee_loss` | a successful flee while fighting | PC only | `-(L_fleeing + L_opponent)` | none | `gain_exp_clamp` (never reached: max 180) | `src/act_offe.cpp:388-390` |
| `death_loss` | `die()` for a PC | PC | `base = -(exp - 3000) / (level + 2)`; always `min(0, base / 10)`; additionally `min(0, base)` when `death_takes_full_mob_xp_loss` | poison classification, killer type | none (uses `gain_exp_regardless`); `delevel` applies | `src/fight.cpp:1293-1325`, `:1037-1047` |
| `script_grant` | Mudlle `GAIN_EXP` opcode (64) | PC target only | literal `exp` from a script int; above level 30: `exp = exp * 25 / L; exp = 6 * exp / (L - 25)` | level scaling | `gain_exp_clamp` | `src/script.cpp:1092-1106`, `src/script.h:82` |
| `pet_sale` | selling a pet back at the pet shop | PC gold, pet exp | price paid `3 * GET_EXP(pet)`; pet exp set to 0 | none | none | `src/spec_pro.cpp:479-502` |
| `wiz_advance` | immortal `advance` | target PC | up: `gain_exp_regardless(xp_to_level(level + adv) - exp)`; down: `exp = xp_to_level(newlevel)` | none | none | `src/act_wiz.cpp:1560-1578` |
| `new_character` | `do_start` | PC | `exp = 1500`, then `gain_exp_regardless(1500)`; first player ever gets `xp_to_level(LEVEL_IMPL)` | none | none | `src/limits.cpp:920, 943, 972` |

### Kill share (`group_gain`, `src/fight.cpp:1490-1541`)

```
level_total     = sum(levelb(k) for k in player_killers) + attacked_level          # NPC victim adds its malus
share           = victim_exp / 10
NPC victim:       share = share * (n + 1) / n                                       # n = number of PC killers
share           = share / level_total
group_bonus     = min(share * levelb(k) / 2, (level_total - attacked_level - levelb(k)) * share / 4)
base(k)         = share * levelb(k) + group_bonus
awarded(k)      = gain_exp(exp_with_modifiers(k, victim, base(k)))
```

- `attacked_level` is the highest `levelb` that has damaged the mob (`src/fight.cpp:1987-1988`,
  `src/clerics.cpp:332-333`); it decays by 2 per tick while the mob is not fighting and is zeroed at 1
  (`src/limits.cpp:723-728`). A solo killer therefore has `level_total = 2 * levelb`.
- Solo kill: the second `min` operand is 0, so `group_bonus = 0`.
- Spirits are awarded in the same loop (`level * naked_perception`, tripled for shadows) as a separate
  currency; they are not XP and are not traced further.
- PC victim: `share = victim_exp / 10 / level_total`, no `(n + 1) / n` factor, and `exp_with_modifiers`
  returns after step 2 below.

### `exp_with_modifiers` (`src/fight.cpp:1334-1390`), applied in this order

1. Orc killer and `MOB_ORC_FRIEND` victim: return 0.
2. `base /= max(L_killer + 1, L_victim - 2)`.
3. PC victim: return `base` here (nothing below applies to player kills).
4. If `L_victim + 6 < L_killer`: `base = 6 * base / (L_killer - L_victim)`.
5. Age (victim level above 5), with `avg = average_mob_life`: `age = MOB_AGE_TICKS * 40 / (L_victim + 20)`;
   if `age < avg`: `exp = exp * (avg * 60 + age * 40) / (avg * 100)` (60 to 100 percent), else
   `exp = exp * (140 - 40 * avg / age) / 100` (100 to 140 percent asymptotically). Mobs loaded at boot
   get a random age in `[0, 80]` mud hours (`src/db.cpp:1610-1613`); repop mobs start at 0.
6. Flag bonuses, each on the post-step-4 `base`: `MOB_AGGRESSIVE` or aggressive-to-killer `+base/5`,
   `MOB_FAST +base/10`, `MOB_SWITCHING +base/10`, `MOB_MEMORY +base/20`, default position below standing
   `-base/20`; good killer and good victim `exp = exp * 2 / 3`; `MOB_SPEC` with a live proc or program
   `+base/10`.
7. Difficulty: if `GET_DIFFICULTY(victim) != 0`: `exp = exp * difficulty / 100`. The value comes from the
   zone reset command that spawned the mob (`M` command `arg5`, `src/zone.cpp:729`; or `A 2 value`,
   `src/zone.cpp:653`), not from the mob file. World files use 100 as the default; anything else is
   deliberate tuning. The same vnum can carry different values at different spawn points.
8. East bonus: `RACE_GOOD(killer)` and `zone_table[world[killer->in_room].zone].x > 8`:
   `exp += exp * min(x - 8, 5) * 3 / 100` (3 to 15 percent). Keyed on the killer's room zone. The zone
   header line is `symbol x y level` (`src/zone.cpp:76-80`); Lake-town (zone 100) has `x = 24`.
9. Comment `TEMPORARY`: `exp += 2 * exp / max(1, L_killer - 1)`: +200 percent at level 2, +6.9 percent
   at 30, +3.4 percent at 60, +2.2 percent at 90.

### Death loss classification (`src/fight.cpp:1037-1047`, `classify_pc_death` `:1010`)

- Poison death while engaged with a real mob (not pet, not orc-friend): full loss.
- Poison death not engaged with a real mob: tenth only.
- Any other death: full loss when the killer is a real mob; tenth only when the killer is a player, a pet,
  an orc-friend, or nobody.
- Death also refills hunger and thirst and clears drunkenness; no other XP effect.

### Live script usage of `GAIN_EXP`

Compiled script lines start with the opcode: 54 `GAIN_EXP` operations exist in `lib/world/scr`
(11.scr: 10, 14.scr: 8, 22.scr: 13, 23.scr: 2, 27.scr: 3, 80.scr: 12, 275.scr: 6). The amount is a script
integer variable set by earlier operations, so the grant size needs a per-script read; the level-30
scaling in `script.cpp` applies to all of them.

## Sites that are not XP vectors

- `src/limits.h:33-34`, `src/act_offe.cpp:338`, `src/act_wiz.cpp:1495`: declarations of `gain_exp` / `gain_exp_regardless`.
- `src/limits.cpp:406, 430`: comment text; `src/limits.cpp:434`: the `gain_exp_regardless` signature.

- `src/comm.cpp:605`, `src/act_info.cpp:1756, 1868, 2664`, `src/act_wiz.cpp:803`: display only.
- `src/db.cpp:1749`: mob prototype `exp` loaded from the mob file (the value `kill_share` divides by 10).
- `src/script.cpp:470-480`: exposes `GET_EXP` as a script-readable integer.
- `src/spec_pro.cpp:479-502`: pet resale (listed above as `pet_sale`; the player's XP is untouched).

## Answers to "are there other XP loss mechanisms?"

Only three paths remove XP from a player: `flee_loss`, `death_loss`, and a script or immortal command
passing a negative amount. Nothing else calls `gain_exp` or `gain_exp_regardless` with a negative value,
and no code writes `points.exp` downward except `wiz_advance` demotion.

## Hypotheses for Task 3 to quantify

1. Kill XP at each tier for a level-matched mob versus level 15 and 20 mobs, after steps 2 and 4 and the
   7000 clamp.
2. Per-hit XP at each tier against level 15, 20 and level-matched victims at the damage cap
   `20 + 2 * L_attacker`, and the number of such hits equal to one level-matched kill.
3. Death (tenth and full) and flee cost at each tier as a fraction of `xp_to_level(L + 1) - xp_to_level(L)`
   and as a count of level-matched kills.
4. East bonus and good-on-good penalty at each tier.
5. Minimum gain events per level forced by the 7000 clamp: `ceil(next_level_cost / 7000)`.
6. Whether the `(n + 1) / n` group factor and `group_bonus` make a duo or trio out-earn a solo per member.
