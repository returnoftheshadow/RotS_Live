# Resistances and vulnerabilities with magnitudes — design

Date: 2026-09-16
Branch: `feat/resistances` (off `release-frodo` @ `ffca361`)
Status: approved design, ready for an implementation plan

## Goal

Make resistances a complete system: every element a spell can deal damage with has a matching
resistance and vulnerability, and both carry a **magnitude** instead of the blanket ⅓ reduction.
Players can cast the resist spells; items can grant them; both persist correctly across logout,
relog and a cold boot.

This lands as a single PR. Stages below are how the work is sequenced inside it, not partial
shipments.

## Source material

The work exists as two uncommitted working trees from 2025–26, now preserved as commits on their
original base `bfee1a4` ("New Mage Update 2", 2025-10-14), which is a direct ancestor of
`release-frodo`, 226 commits back:

- **`import/resist-live-ah`** (`7442126`) — from `~/u/games/RotS_Live_ah`. **Authoritative.**
  16 files, +773/−224.
- **`import/resist-b4-cleanup`** (`2c163b9`) — from `~/u/games/zzz_resists_working_b4_cleanup`.
  **Reference only**, for intent. Its design used `AFF_RESIST_*` bits 32–35 on a 32-bit `long`,
  which cannot work; it was superseded.

Working rule: the branch is a work in progress from several attempts. Comments and constants in it
are hypotheses, not specification. Behaviour is the judge.

Method: hand-port onto `release-frodo` in stages (not a 3-way merge), then diff the result against
a trial merge of `import/resist-live-ah` as a completeness checklist.

## Background: what already existed before this work

- `specials.resistance` / `specials.vulnerability`, both bitvectors, tested by
  `IS_RESISTANT(ch, group)` / `IS_VULNERABLE(ch, group)` as `1 << group` (`utils.h:693-697`).
- `check_resistances(victim, attacktype)` returning `+1` / `-1` / `0`, indexed through
  `skills[attacktype].skill_spec`.
- `resistance_name[]` / `vulnerability_name[]` in `consts.cpp`.
- Flat effects in `damage()`: resist `dam * 2 / 3`, vulnerable `dam * 3 / 2`, plus a 1-in-3 chance
  that a **physical** resistance does not apply at all.
- Mobs: resistance and vulnerability bitvectors in the `.mob` record; OLC fields 36 and 37.
- Objects: `APPLY_RESIST` (30) / `APPLY_VULN` (31) affect slots, and `APPLY_SPELL` (27), whose
  modifier packs `level * 256 + spellnum`.
- `spell_evasion` already implements item-replaces-spell slot ownership. Item **6393** (shiny
  sleeves, `A 27 3882` = evasion at level 15) is the live control case.

## The core problem the branch solves

The spell's element and the resistance index were treated as the same number. They are not:
`resistance_name[]` / `vulnerability_name[]` are the tables `IS_RESISTANT` actually indexes, and
`PLRSPEC_*` drifted away from them (`PLRSPEC_DARK` is 16, the dark resistance slot is 12). The fix
is an explicit resist id stored per skill, rather than reusing the spec id.

## Section 1 — Data model and persistence

**Identity.** `RESIST_*` (0–13) is the canonical resist id, matching the name tables.
`skill_data` gains a 13th field, `char resist`, set explicitly on every `skills[]` row.
`PLRSPEC_*` is left alone.

**Magnitude.** `affected_type.effect_modifier`, an `int` percentage. 0 means "no magnitude",
40 means 40% reduction.

**Persistence.** Characters save two ways today:

- **Account-native → JSON** (`save_char` → `write_account_character_file` →
  `serialize_character_to_json`). This is the live path; legacy files are erased at first login.
- **Legacy versioned text** (`save_player` → `write_player_text`) — **load-only in practice**, for
  a character's first login before migration.

Both funnel through `char_to_store` into `char_file_u`, and **nothing writes `char_file_u` as raw
bytes any more**. That is what makes widening `affected_type` safe now: the struct never reaches
disk, so no field can be displaced. This is the failure that stopped the original work — affects
loaded but everything behind them shifted, losing hunger — and it cannot recur.

Work:

1. `char_to_store` / `store_to_char` zero and carry the new field.
2. **Text path: nothing to do.** A legacy file predates the field, so it cannot carry one, and
   accounts are live on production with no route back to the legacy format. The parser already
   does `memset(char_element, 0, sizeof(struct char_file_u))` before parsing (`db.cpp:2415`), so a
   migrating character arrives with `effect_modifier` at 0 for free — no garbage, no code change.
   *(The `live-ah` change that extended this format with a 7th field is deliberately not ported:
   the format is no longer written. Recorded so the omission is a decision, not a miss.)*
3. **JSON path**: `AffectData` gains `effect_modifier`; writer, reader, and absent-key-reads-as-0
   for every character already on disk. Whether that is additive at the current schema version or
   warrants a bump is settled during implementation.

## Section 2 — Plumbing: how a magnitude reaches the affect, and who owns the slot

**Where item magnitudes come from.** The `APPLY_SPELL` modifier, `level * 256 + spellnum`. The
level half **is** the percentage, used literally — an item encoding 30 grants 30% regardless of the
wearer's level. This is already how the test bench is authored:

| vnum | affect | decodes to |
|---|---|---|
| 2067 | `A 27 7844` | level 30, spell 164 — resist illusion |
| 2069 | `A 27 7841` | level 30, spell 161 — resist fire |
| 2071 | `A 27 7841` | level 30, resist fire |
| 2073 | `A 27 7842` | level 30, resist cold |
| 2075 | `A 27 7843` | level 30, resist light |

`obj_flags.level` is **not** the source. `equip_char` currently passes it into `affect_modify`,
where only the debug printf reads it; 6393 would give 20 from the object and 15 from the spell.

**Transport.** An object's affect slots are `{location, modifier}` only, inside `obj_file_elem`
which is marked `*DO*NOT*CHANGE*`, so no new field can live on the item. And the spell is invoked
as `spell_pointer(ch, "", type, ch, 0, 0, 1)` with `obj = 0` — it is never told which object
applied it, so it cannot read anything off the item.

Therefore the magnitude crosses through the existing module-scope variable (`eff_mod`), kept but
made disciplined: set immediately before the call, cleared on every exit path, read only when
`is_object`, and documented as the one deliberate piece of implicitness.

Alternatives rejected: reusing the `digit` parameter (it is the *direction* argument for spells
like cone of cold, so an item applying a directional spell would fire it at random), and adding an
eighth `ASPELL` parameter (correct, but rewrites every spell signature in the game and five
dispatch sites — too wide for this PR).

**Slot ownership — the `spell_evasion` rule.** One affect per resist type. On `is_object` or
`SPELL_TYPE_ANTI`, strip the existing affect of that type, then add. The current
`if (current_effect) return;` refusal goes away.

Consequences, accepted deliberately: wearing a resist item replaces a cast one even if weaker;
removing the item clears the slot and the cast version does not return; the player manages this.
The reason is recovery — a permanent affect cannot be re-derived when a temporary one expires
without rescanning all worn gear on every expiry, so item-over-spell is the ordering that never
leaves a hole.

**Duration.** Keep `(is_object) ? -1 : level * 2`; drop the unconditional `-1` that overwrites it.
Item resists are permanent, cast resists expire.

`removeable_spell_affection` is carried across unused. It counts affects of a type and returns one
only if unique; it cannot answer "which object owns this", and the evasion rule makes it
unnecessary.

## Section 3 — The spells

One shared `do_resist_spell(resist_type, modifier, caster, victim, type, is_object, name)` with six
wrappers at spell numbers 161–166: fire, cold, light(ning), illusion, physical, dark. `skills[]`
rows exist at those indices (`PROF_CLERIC`, `PLRSPEC_PROT`, `RESIST_NONE` — correct, they deal no
damage).

Cast magnitude stays as built: `min(GET_LEVEL(caster), 30) + 10`, so a 40% ceiling on a cast.
Item magnitude is the literal encoded value.

`spell_protection` keeps its conversion — `RESIST_*` modifiers, `effect_modifier` set, and the new
illusion sphere — plus a fix to its sphere table, where a missing comma glues `"illusion"` to the
`"\n"` terminator.

**Learnability is a real gap.** A spell is castable once it has a `skills[]` row, but learnable only
where some guildmaster has `knowledge[spell] > 0`. Counting the tables: the "ALL SKILLS" guildmaster
has 162 entries, so it teaches **161 (resist fire) only**; every other guildmaster's array stops at
129–149, so none of the six is teachable in the world. This is why fire was the one that worked, and
why testing went through items and imm-set skills.

## Section 4 — The damage path

Today `check_resistances` tests a *bit* and `get_resisted_damage` then hunts for an *affect*, with
nothing tying the two to the same element. One rule replaces both:

```
element = resist type of the attack       /* skills[attacktype].resist, keeping the
                                             TYPE_HIT..TYPE_CRUSH / archery -> RESIST_PHYS fallback */

if victim is vulnerable to element:
        dam = dam * 3 / 2                 /* flat, no roll, unchanged */

else if victim is resistant to element:
        mod = magnitude for that element   /* first affect with location == APPLY_RESIST
                                              and modifier == element -> effect_modifier */
        if mod > 0:  dam -= round(dam * mod / 100)
        else:        legacy: physical keeps its 1-in-3 cancel, then dam = dam * 2 / 3
```

Looking the magnitude up **by element rather than by spell** fixes the wrong-element match for free
(a fire resist currently reduces cold damage), unifies `SPELL_RESIST_*` and `SPELL_PROTECTION` —
both write `location = APPLY_RESIST, modifier = RESIST_*` — and dissolves the physical special case.

A bit with no affect behind it is a mob or object flag resistance with no magnitude; it keeps the
old behaviour exactly. A percentage resist applies on **every** hit — the roll survives only on the
flag path, because rolling twice would make a 40% resist behave like 27% and the number on a
player's gear would stop meaning anything.

`is_resistant` stops being assigned as a by-value parameter.

## Section 5 — Display and diagnostics

`do_affections` gains "You are resistant to:" and "You are vulnerable to:" above the affect list,
rendered by `sprintbit_affections` from the name tables (strips the `V-` prefix, lowercases).
`live-ah`'s `buf[0] = '\0'` fix comes with it — an unrelated real bug where the command printed
leftover buffer content.

**Strength is shown**, for now, so it can be seen and discussed:

- affect-backed resistance → `fire (30%)` from `effect_modifier`
- bit-only resistance (mob field, or `A 30` item — only 6518 and 6531 in the whole world) →
  the legacy default, `fire (33%)`
- vulnerabilities are bit-only everywhere → their default, 50%

The physical flag roll is not reflected in the displayed number; deliberately ignored for now.

**Diagnostics are kept**, all of them: `char_data::debug_flag`, the `debug` command
(`LEVEL_GRGOD`, non-persistent), `debug_flag_msg` writing to the imm's own screen, and
`mudlog_debug_mob_or_player` writing to the mudlog for a mob named `debug`. Every call site in the
affect, equip and damage paths comes across. Pruning them is the author's later call.

**Removed:** the unconditional `mudlog` instrumentation in `db.cpp` and `nanny`
(`CALL--> load_char`, `store_to_char`, `Save player`, `saveP::conditions2`), which fires for every
character on every save and login and belonged to the hunger-bug hunt that JSON persistence answers.
Including `sprintf(buf, "... %lu", st->specials2)`, which passes a struct to `printf`.

## Section 6 — Verification

Per stage, before the next begins:

1. **Data model / persistence** — gtest round-trip in `character_json_tests.cpp` (affect with a
   modifier survives; JSON without the key loads as 0 with all other fields intact). Live: set a
   resist affect, quit, relog, `affections` unchanged; reboot, relog, unchanged.
2. **Plumbing** — wear 2069, `affections` shows `fire (30%)`; remove, gone. Cast then wear: item
   wins. Regression control: **6393 behaves identically before and after** the change.
3. **Spells** — cast each of the six (imm-set knowledge): message, `level * 2` expiry, item version
   permanent, `unprotect` clears a protection.
4. **Damage** — arena 1120, test weapon 2068 driving script 2393, `debug` on. Specifically: fire
   resist does not reduce cold damage; a flag-resistant mob still takes ⅓ less with its physical
   roll; vulnerability still adds 50%.

Throughout: `scripts/rots-docker.sh test` against a baseline taken on the clean tree first, and one
temporary build with `-Wall -Wextra -Werror` (not committed — the Makefile change stays out of the
PR) to catch the class of defect the `%lu` line belongs to.

At the end: diff against a trial merge of `import/resist-live-ah` and walk through anything present
there and absent here.

## Deferred — not in this PR

- Balance numbers on the six `skills[]` rows: minimum level (currently 0 for all six), mana (5),
  beats (21).
- `learn_diff` / `learn_type` are transposed on fire, cold and lightning (`1, 10` where the other
  three read `10, 1`). Harmless today, since only `LEARN_SPEC` is tested.
- Which guildmasters teach which resist spells. Today: "ALL SKILLS" teaches fire only.
- Elements beyond the six. Only fire, cold, lightning and dark have damaging spells; illusion
  resistance matters through `do_mental`'s save rather than through damage; arcane has no damaging
  spell at all.
- `SPELL_FIREBALL2` (fireball's splash) and `SPELL_DRAGONSBREATH` have no `skills[]` rows, so they
  are not fire-resistible while fireball's direct hit is.
- `SKILL_DEFEND` is 131 and `TYPE_BLUDGEON` is 131; out of scope by decision — defend is not
  something that gets resisted.
- Re-authoring 6518 and 6531 as `APPLY_SPELL` items so their resistances carry a magnitude
  (world data, not code).
- `APPLY_RESIST_FIRE..DARK` (40–44) in `structs.h` are defined and unreferenced; carried, not used.

## Risks

- **`eff_mod` remains a module-scope side channel.** Bounded to one call and cleared on every path,
  but it is the one implicit thing in the design; a nested item-applied cast would be its failure
  mode.
- **Slot ownership is last-writer-wins**, so a player can lower their own resistance by equipping a
  weaker item. Accepted, and it matches `spell_evasion`.
- **Existing account characters carry no modifier.** An absent JSON key reads as 0, which under the
  section 4 rule falls back to the legacy flat behaviour rather than granting nothing.
