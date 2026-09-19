# XP and Progression Research Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Document exactly how a Return of the Shadow player gains and loses experience today, and quantify the incentive a level 30 to 90 character has to farm low-level mobs instead of level-appropriate content, as the baseline for a new progression system.

**Architecture:** Four evidence streams (code trace, mob data, loss modelling, player population) feed one findings document in `docs/systems/`. A small Python package under `tools/xp_research/` mirrors the server's XP formulas so the mob corpus can be evaluated at six player tiers, and a GoogleTest file pins the same formulas in C++ so the Python mirror is validated against the real code.

**Tech Stack:** C++ source reading (`src/fight.cpp`, `src/limits.cpp`, `src/act_offe.cpp`, `src/clerics.cpp`, `src/script.cpp`, `src/db.cpp`, `src/zone.cpp`), Python 3 standard library only, GoogleTest via the i386 Docker container.

**Spec:** Approved in conversation on 2026-09-19 (no separate spec file). The approved scope is restated under "Approved scope" below so this plan is self-contained.

## Approved scope

Research only. Answer these questions for player tiers **30 (legend), 40, 50, 60, 75, 90**:

1. How is XP gained from mob kills, player kills, and damage dealt ("hitting XP"), with every modifier?
2. How is XP lost on flee and death, and what other XP loss mechanisms exist?
3. For a representative view of the mob corpus, what does a player at each tier receive per kill and per hit, and how does hitting a level 15 to 20 mob very hard compare with killing level-appropriate mobs?
4. What do flee and death actually cost at each tier, in the same units?
5. Do zone or faction XP bonuses exist, and how large are they?
6. Where does the real player population sit on the level curve?

The findings feed a later proposal (removing hitting XP and flee/death XP loss, adding a new reward vector). That proposal is **out of scope** here; this plan produces the baseline and nothing else.

## Global constraints

- **No production code changes.** The only source additions are new files under `src/tests/` and `tools/xp_research/`. Do not edit `src/limits.cpp`, `src/utils.h`, `src/spec_pro.cpp` or any other existing source; several are CRLF files that a careless edit rewrites wholesale.
- **Never connect to the live server.** All evidence comes from this checkout: `src/`, `lib/world/`, `lib/players/players.csv`, and a small sample of local player files.
- **Player data hygiene.** The findings document contains aggregate counts only. No character names, idnums, or per-character rows leave `lib/players/`. Read player file bodies only for the sample defined in Task 5, and read only the `level` and `exp` lines.
- **Mob XP is per-mob data, not derived from level** (`src/db.cpp:1749` reads it from the file). Extract the whole corpus once (cheap, 3476 records) and sample only for hand-read walkthroughs.
- **Model routing** (personal `model-escalation-gate` policy): Tasks 2, 3, 4 and 5 are bounded delegated work and run on **Sonnet** subagents. Tasks 1 and 6 need judgment about which code is live and what the numbers mean and stay with **Fable** in the main session. The Docker test build in Task 4 is run by the controller, not the subagent.
- **Subagent permissions to preflight before dispatch:** read anywhere in the checkout; write only under `tools/xp_research/` (Tasks 2, 3, 5) or `src/tests/` (Task 4); run `python3` from the checkout root. No git operations, no Docker, no edits outside those folders.
- **Findings document location:** `docs/systems/experience-and-progression.md`, using `docs/_TEMPLATE.md`, indexed from `docs/README.md` in the "Gameplay systems" table. The `backlog/` folder is empty scaffolding with no config; this plan and the findings document are the record of this work.
- **Test gate:** `docker compose run --rm -T rots bash -lc 'cd /rots && cmake --build build --target ageland_tests -j8 && ./bin/tests'`. Compare the failing-test list against the 2026-09-15 baseline (781 tests, 570 passed, 211 pre-existing failures under QEMU), never the raw count.
- **Shared Docker container.** Other Claude sessions work in this repository and in `RotS_Live_Modern` and use the same Docker toolchain. Task 4 Step 3 is the only container use in this plan, and it is a qemu/i386 job (service `rots`), which the shared lock convention treats as exclusive. Before running it, follow `/tmp/rots-docker-lock/README.txt`: list and read every `*.lock` there, start nothing while any lock exists, create `/tmp/rots-docker-lock/<session-unique-name>.lock`, for this session `uaf-port-25.lock` (another session shares this worktree, so never reuse a name derived only from the branch or worktree) containing session name, repo path, service `rots`, purpose, ISO start, expected duration of about 25 minutes), and delete it when the run finishes. Also check `docker ps` for a build already using this checkout's bind-mounted `build/` tree. Never stop, restart, or remove a container this session did not start; a lock older than three hours is messaged about, not deleted.
- **Scratch output.** Generated CSVs and tables are not committed. Every task that writes them uses `SCRATCH=/private/tmp/claude-501/-Users-drelidan-Projects-GitHub-RotS-Live--claude-worktrees-uaf-port/ec85f060-c462-4fb9-b15a-d5659fa21b6d/scratchpad` (this session's scratchpad; a later session substitutes its own) and writes under `$SCRATCH/xp/`.
- **Constants to carry everywhere:** `LEVEL_MAX = 30` (legend threshold), `LEVEL_IMMORT = 91`, `xp_to_level(L) = 1500 * L * L`, `GET_LEVELB(pc) = min(level, 20 + level / 3)`, `gain_exp` clamps each positive event to +7000 and each negative event to -10000.

## Dependency graph

```
Task 1 (trace)  ──┬──> Task 3 (model) ──┐
Task 2 (mobs)   ──┘                     ├──> Task 6 (synthesis)
Task 1 (trace)  ────> Task 4 (pins)  ───┤
Task 5 (players) ───────────────────────┘
```

Tasks 1, 2 and 5 can start together. Task 3 needs 1 and 2. Task 4 needs 1. Task 6 needs everything.

---

### Task 1: Mechanism trace (Fable, main session)

**Files:**
- Create: `tools/xp_research/FORMULAS.md` (the formula table, with `file:line` for every row)
- Read: `src/limits.cpp:90-475`, `src/fight.cpp:1037-1060`, `src/fight.cpp:1285-1400`, `src/fight.cpp:1490-1553`, `src/fight.cpp:1980-1992`, `src/fight.cpp:2085-2100`, `src/act_offe.cpp:375-395`, `src/clerics.cpp:218-230`, `src/clerics.cpp:325-335`, `src/script.cpp:1095-1110`, `src/spec_pro.cpp:470-505`, `src/act_wiz.cpp:1560-1580`, `src/limits.cpp:715-730`, `src/utils.h:315`, `src/utils.h:400-405`

**Interfaces:**
- Produces: `FORMULAS.md` with one row per XP event, columns `id | trigger | who | formula (as code) | modifiers | clamps | source`. The `id` values below are the names Task 3 uses for its Python functions and Task 4 for its test names: `xp_to_level`, `levelb`, `gain_exp_clamp`, `delevel`, `hit_xp_melee`, `hit_xp_mental`, `kill_share`, `exp_with_modifiers`, `flee_loss`, `death_loss`, `script_grant`, `pet_sale`, `wiz_advance`.

- [ ] **Step 1: Enumerate every XP mutation site**

Run from the checkout root:

```bash
grep -n "gain_exp\|gain_exp_regardless\|GET_EXP(.*) *[-+]\?=" src/*.cpp src/*.h | grep -v "^src/tests/"
```

Expected: about 25 lines. Every line must end up either as a row in `FORMULAS.md` or in a short "not an XP vector" list at the bottom of that file (for example `comm.cpp:605` and `act_info.cpp:1756` only display XP). Any site that grants or removes XP and is not already named in the `id` list above gets a new `id`.

- [ ] **Step 2: Record the gain pipeline and delevel rule**

From `src/limits.cpp:410-475`, write rows `gain_exp_clamp` (positive events clamped to 7000, negative to -10000, immortals excluded) and `delevel` (a level is lost while `xp_to_level(level) - 20000 > exp`, each loss also removes practices). Note that `gain_exp_regardless` bypasses both clamps and is what death uses. Note the mini-level advance loop (`temp_int * temp_int * 3 / 20 <= exp`).

- [ ] **Step 3: Record hitting XP**

From `src/fight.cpp:2098` write `hit_xp_melee`:

```
(1 + L_victim) * min(20 + 2 * L_attacker, dam) / (1 + L_attacker)
```

From `src/clerics.cpp:226` write `hit_xp_mental`, identical except `dam` is replaced by `damg * 5`. Record the observation that the `min` saturates at `20 + 2 * L_attacker`, so for a fixed victim level the per-hit reward stops growing with damage once that cap is reached, and that the formula applies the +7000 clamp per hit, not per fight. Record that it fires for every damage event including NPC attackers (the `gain_exp` NPC guard is inside `gain_exp_regardless`).

- [ ] **Step 4: Record kill share**

From `src/fight.cpp:1490-1553` (`group_gain`) write `kill_share`:

```
level_total    = sum(GET_LEVELB(k) for k in killers) + attacked_level(mob)
share          = (mob_exp / 10) * (n + 1) / n / level_total        # NPC victim
share          = (pc_exp / 10) / level_total                       # PC victim
group_bonus    = min(share * levelb / 2, (level_total - attacked_level - levelb) * share / 4)
base           = share * levelb + group_bonus
awarded        = exp_with_modifiers(killer, victim, base)          # then gain_exp clamp
```

Record `attacked_level` semantics: set to the highest `GET_LEVELB` that has hit the mob (`src/fight.cpp:1987`, `src/clerics.cpp:333`), decays by 2 per tick while not fighting (`src/limits.cpp:723-728`). Record the spirit gain alongside as a separate currency, not XP.

- [ ] **Step 5: Record every branch of `exp_with_modifiers`**

From `src/fight.cpp:1334-1390` write `exp_with_modifiers` as an ordered list of transformations:

1. Orc killing `MOB_ORC_FRIEND`: return 0.
2. `base /= max(L_killer + 1, L_victim - 2)`.
3. PC victim: return here.
4. If `L_victim + 6 < L_killer`: `base = 6 * base / (L_killer - L_victim)`.
5. Age curve (mobs above level 5): `age = MOB_AGE_TICKS * 40 / (L_victim + 20)`; if `age < average_mob_life`: `exp = exp * (avg * 60 + age * 40) / (avg * 100)`, else `exp = exp * (140 - 40 * avg / age) / 100`.
6. Flag bonuses on `base`: aggressive +1/5, fast +1/10, switching +1/10, memory +1/20, non-standing default position -1/20, good-on-good `* 2 / 3`, spec-proc mob +1/10.
7. `exp = exp * difficulty / 100` when difficulty is non-zero.
8. East bonus: good-race killer in a zone with `x > 8` gets `+ exp * min(x - 8, 5) * 3 / 100` (max +15 percent).
9. `TEMPORARY` bonus: `exp += 2 * exp / max(1, L_killer - 1)`.

Resolve two facts and record them: the value or derivation of `average_mob_life` (`grep -n "average_mob_life *=" src/*.cpp`), and where `GET_DIFFICULTY` (`specials.prompt_number`, `src/utils.h:403`) is populated for a loaded mob (`grep -n "prompt_number" src/*.cpp`). If difficulty is never loaded from the world files, say so; Task 2 then does not extract it.

- [ ] **Step 6: Record flee and death loss**

From `src/act_offe.cpp:390` write `flee_loss = L_fleeing + L_opponent`, PCs only, through the -10000 clamp.

From `src/fight.cpp:1285-1330` write `death_loss`: `base = -(exp - 3000) / (level + 2)`; a tenth of `base` is always taken; the full `base` is taken additionally when `death_takes_full_mob_xp_loss` is true, which (from `src/fight.cpp:1037-1047`) is: a poison death while engaged with a real mob, or any non-poison death whose killer is a real mob (not a pet, not an orc-friend). Both use `gain_exp_regardless`, so the -10000 clamp does not apply but the delevel rule does.

- [ ] **Step 7: Record the remaining vectors**

`script_grant` from `src/script.cpp:1095-1110`: Mudlle scripts grant a literal amount, scaled above level 30 by `exp * 25 / L` then `6 * exp / (L - 25)`. Run `grep -rl "exp" lib/world/scr | head` and record whether any live script uses the XP opcode; if none do, say so.

`pet_sale` from `src/spec_pro.cpp:470-505`: the pet's own XP is spent as gold at three coins per point. Record as "not a player XP vector".

`wiz_advance` from `src/act_wiz.cpp:1560-1580`: immortal `advance` sets XP directly. Record as administrative.

- [ ] **Step 8: Write the hypotheses the data tasks must test**

At the bottom of `FORMULAS.md`, list the quantitative questions Task 3 answers, so the model is built to answer them rather than to print everything:

1. At each tier, kill XP for a level-matched mob versus a level 15 and a level 20 mob, after the level-gap divisor and the 7000 clamp.
2. At each tier, per-hit XP against a level 15, 20 and level-matched mob at the damage cap, and hits needed to earn one level-matched kill's worth.
3. Death and flee cost at each tier as a fraction of `xp_to_level(L + 1) - xp_to_level(L)` and as "number of level-matched kills".
4. Size of the east bonus and the good-on-good penalty at each tier.
5. How much of the next-level cost the 7000 per-event clamp forces into event count: minimum kill events per level at each tier.

- [ ] **Step 9: Commit**

```bash
git add tools/xp_research/FORMULAS.md
git commit -m "docs: trace every XP gain and loss path for the progression research"
```

---

### Task 2: Mob and zone extraction (Sonnet)

**Files:**
- Create: `tools/xp_research/parse_mobs.py`, `tools/xp_research/parse_zones.py`, `tools/xp_research/test_parse_world.py`
- Read: `src/db.cpp:1690-1835` (mob record read order), `src/zone.cpp:60-85` (zone header read order), `lib/world/mob/100.mob` (worked example)

**Interfaces:**
- Produces: `parse_mobs.parse_mob_file(path) -> list[MobRecord]` and `parse_mobs.parse_all(mob_dir) -> list[MobRecord]`, where `MobRecord` is a dataclass with fields `vnum:int, zone:int, aliases:str, short_descr:str, mob_flags:int, affected_by:int, alignment:int, level:int, ob:int, parry:int, dodge:int, hit:int, max_hit:int, damage:int, ene_regen:int, gold:int, exp:int, position:int, default_pos:int, sex:int, race:int, prog:int, spirit:int` and property methods `is_aggressive`, `is_fast`, `is_switching`, `is_memory`, `is_spec`, `is_orc_friend`, `is_pet` derived from `mob_flags` using the `MOB_*` bit values in `src/structs.h`.
- Produces: `parse_zones.parse_all(zon_dir) -> dict[int, ZoneRecord]` keyed by zone number with fields `number:int, name:str, symbol:str, x:int, y:int, level:int, top:int`, and property `is_east_of_river` (`x > 8`) plus `east_bonus_percent` (`min(x - 8, 5) * 3` when east, else 0).
- Produces (added 2026-09-19 after the trace found that difficulty is a zone-command value, `src/zone.cpp:729`): `ZoneRecord.mob_loads: list[MobLoad]` with `MobLoad(vnum, room, max_existing, load_percent, difficulty)` built from every `M` reset command (`arg1..arg5`), plus `lifespan` and `reset_mode`; a `--mob-loads` CSV mode writing `zone,vnum,room,max_existing,load_percent,difficulty` one row per `M` command to `$SCRATCH/xp/mob_loads.csv`.
- Produces: `python3 tools/xp_research/parse_mobs.py lib/world/mob > <out>/mobs.csv` and `python3 tools/xp_research/parse_zones.py lib/world/zon > <out>/zones.csv`.

- [ ] **Step 1: Confirm the record shape**

Run:

```bash
grep -h -o " [MNS]$" lib/world/mob/*.mob | sort | uniq -c
```

Expected: only `N` (3476 on 2026-09-19). If `M` or `S` appears, add that branch following `src/db.cpp:1725-1745`; otherwise the parser handles `N` only and rejects anything else with a clear error naming the vnum.

- [ ] **Step 2: Write the failing parser test**

`tools/xp_research/test_parse_world.py`, using only `unittest`:

```python
import unittest
from pathlib import Path
from parse_mobs import parse_mob_file
from parse_zones import parse_zone_file

WORLD = Path(__file__).resolve().parents[2] / "lib" / "world"

class ParseMobFile(unittest.TestCase):
    def test_first_record_of_zone_100_matches_the_loader_field_order(self):
        records = parse_mob_file(WORLD / "mob" / "100.mob")
        tailor = records[0]
        self.assertEqual(tailor.vnum, 10000)
        self.assertEqual(tailor.short_descr, "Medrel Inding")
        self.assertEqual(tailor.mob_flags, 526347)
        self.assertEqual(tailor.alignment, 0)
        self.assertEqual((tailor.level, tailor.ob, tailor.parry, tailor.dodge), (8, 32, 16, 8))
        self.assertEqual((tailor.hit, tailor.max_hit), (77, 114))
        self.assertEqual((tailor.damage, tailor.ene_regen), (5, 86))
        self.assertEqual((tailor.gold, tailor.exp), (3000, 3930))
        self.assertEqual((tailor.position, tailor.default_pos, tailor.sex, tailor.race), (8, 8, 1, 1))
        self.assertEqual(tailor.spirit, 0)

    def test_every_world_file_parses_and_every_record_has_a_positive_level(self):
        total = 0
        for path in sorted((WORLD / "mob").glob("*.mob")):
            for record in parse_mob_file(path):
                total += 1
                self.assertGreater(record.level, 0, f"vnum {record.vnum} in {path.name}")
        self.assertGreater(total, 3000)

class ParseZoneFile(unittest.TestCase):
    def test_zone_100_header_exposes_map_coordinates(self):
        zone = parse_zone_file(WORLD / "zon" / "100.zon")
        self.assertEqual(zone.number, 100)
        self.assertIsInstance(zone.x, int)
        self.assertIsInstance(zone.y, int)

if __name__ == "__main__":
    unittest.main()
```

Adjust the zone assertion to the real values of `100.zon` after reading the file, and add one assertion for a zone known to be east of the river once Task 1 has confirmed `x > 8` is the test.

- [ ] **Step 3: Run the test to verify it fails**

Run: `cd tools/xp_research && python3 -m unittest test_parse_world -v`
Expected: FAIL with `ModuleNotFoundError: No module named 'parse_mobs'`.

- [ ] **Step 4: Write the mob parser**

`tools/xp_research/parse_mobs.py`. The server reads strings with `fread_string` (everything up to the next `~`) and then a whitespace-separated integer stream with `fscanf`, so the parser must not rely on line boundaries for the numeric part. Field order for an `N` record, from `src/db.cpp:1700-1835`:

```python
"""Mirror of the mob loader in src/db.cpp (load_mobiles, 'N' records).

Field order is copied from the fscanf sequence; do not reorder without
re-reading db.cpp. Strings end at '~'; numbers are a whitespace stream.
"""
from __future__ import annotations
import csv
import sys
from dataclasses import dataclass, fields
from pathlib import Path

MOB_AGGRESSIVE = 1 << 5     # confirm every bit against src/structs.h before use
MOB_MEMORY = 1 << 7
MOB_SPEC = 1 << 1
MOB_FAST = 1 << 15
MOB_SWITCHING = 1 << 14
MOB_ORC_FRIEND = 1 << 12
MOB_PET = 1 << 21

@dataclass
class MobRecord:
    vnum: int
    zone: int
    aliases: str
    short_descr: str
    mob_flags: int
    affected_by: int
    alignment: int
    level: int
    ob: int
    parry: int
    dodge: int
    hit: int
    max_hit: int
    damage: int
    ene_regen: int
    gold: int
    exp: int
    position: int
    default_pos: int
    sex: int
    race: int
    prog: int
    spirit: int

    def _flag(self, bit: int) -> bool:
        return bool(self.mob_flags & bit)

    @property
    def is_aggressive(self) -> bool: return self._flag(MOB_AGGRESSIVE)
    @property
    def is_memory(self) -> bool: return self._flag(MOB_MEMORY)
    @property
    def is_spec(self) -> bool: return self._flag(MOB_SPEC)
    @property
    def is_fast(self) -> bool: return self._flag(MOB_FAST)
    @property
    def is_switching(self) -> bool: return self._flag(MOB_SWITCHING)
    @property
    def is_orc_friend(self) -> bool: return self._flag(MOB_ORC_FRIEND)
    @property
    def is_pet(self) -> bool: return self._flag(MOB_PET)


class _Cursor:
    """Sequential reader over one file's text with fread_string / fscanf semantics."""

    def __init__(self, text: str):
        self.text = text
        self.pos = 0

    def read_string(self) -> str:
        end = self.text.index("~", self.pos)
        value = self.text[self.pos:end]
        self.pos = end + 1
        return value.strip("\r\n")

    def read_token(self) -> str:
        length = len(self.text)
        while self.pos < length and self.text[self.pos].isspace():
            self.pos += 1
        start = self.pos
        while self.pos < length and not self.text[self.pos].isspace():
            self.pos += 1
        return self.text[start:self.pos]

    def read_ints(self, count: int) -> list[int]:
        return [int(self.read_token()) for _ in range(count)]

    def at_end(self) -> bool:
        return self.pos >= len(self.text)


def parse_mob_file(path: Path) -> list[MobRecord]:
    zone_number = int(path.stem)
    cursor = _Cursor(path.read_text(encoding="latin-1"))
    records: list[MobRecord] = []
    while True:
        marker = cursor.read_token()
        if marker in ("", "$", "$~"):
            return records
        if not marker.startswith("#"):
            raise ValueError(f"{path.name}: expected '#vnum', found {marker!r} near offset {cursor.pos}")
        vnum = int(marker[1:])
        aliases = cursor.read_string()
        short_descr = cursor.read_string()
        cursor.read_string()  # long_descr
        cursor.read_string()  # description
        mob_flags = int(cursor.read_token())
        affected_by, alignment = cursor.read_ints(2)
        letter = cursor.read_token()
        if letter != "N":
            raise ValueError(f"{path.name}: vnum {vnum} uses record type {letter!r}; only 'N' is implemented")
        cursor.read_string()  # death_cry
        cursor.read_string()  # death_cry2
        level, ob, parry, dodge = cursor.read_ints(4)
        hit, max_hit = cursor.read_ints(2)
        damage, ene_regen = cursor.read_ints(2)
        gold, exp, _owner = cursor.read_ints(3)
        position, default_pos, sex, race, _pref = cursor.read_ints(5)
        _weight, _height, prog, _butcher, _corpse, _rp_flag = cursor.read_ints(6)
        _prof, _mana, _move, _bodytype = cursor.read_ints(4)
        _saving_throw = cursor.read_ints(1)[0]
        cursor.read_ints(6)  # str int wil dex con lea
        _language, _perception, _resistance, _vulnerability, _script, spirit, _will_teach = cursor.read_ints(7)
        records.append(MobRecord(vnum, zone_number, aliases, short_descr, mob_flags, affected_by,
                                 alignment, level, ob, parry, dodge, hit, max_hit, damage, ene_regen,
                                 gold, exp, position, default_pos, sex, race, prog, spirit))


def parse_all(mob_dir: Path) -> list[MobRecord]:
    records: list[MobRecord] = []
    for path in sorted(mob_dir.glob("*.mob"), key=lambda p: int(p.stem)):
        records.extend(parse_mob_file(path))
    return records


def main(argv: list[str]) -> int:
    records = parse_all(Path(argv[1]))
    writer = csv.writer(sys.stdout)
    names = [f.name for f in fields(MobRecord)]
    flags = ["is_aggressive", "is_memory", "is_spec", "is_fast", "is_switching", "is_orc_friend", "is_pet"]
    writer.writerow(names + flags)
    for record in records:
        writer.writerow([getattr(record, n) for n in names] + [int(getattr(record, f)) for f in flags])
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
```

Before running, replace every `MOB_*` constant with the value from `src/structs.h` (`grep -n "MOB_AGGRESSIVE\|MOB_MEMORY\|MOB_SPEC\b\|MOB_FAST\|MOB_SWITCHING\|MOB_ORC_FRIEND\|MOB_PET" src/structs.h`). The placeholders above are guesses and will be wrong.

- [ ] **Step 5: Write the zone parser**

`tools/xp_research/parse_zones.py`, following `src/zone.cpp:60-80`: after the `#number` line and the `name~` string come the owner list (integers until a `0`), then one line `symbol x y level`, then `top`, `lifespan`, `reset_mode`. Expose `ZoneRecord(number, name, symbol, x, y, level, top)` with `is_east_of_river` and `east_bonus_percent` as specified in Interfaces, `parse_zone_file(path)`, `parse_all(zon_dir)`, and a `main` that writes CSV to stdout with one row per zone.

- [ ] **Step 6: Run the tests to verify they pass**

Run: `cd tools/xp_research && python3 -m unittest test_parse_world -v`
Expected: PASS, 3 tests.

- [ ] **Step 7: Hand-verify five records against the files**

Pick vnums from five different zones spread across the vnum range (for example the first record of `100.mob`, `150.mob`, `200.mob`, `300.mob`, and the last file in the directory). For each, open the file, and check `level`, `exp`, `max_hit`, and `mob_flags` against the parser's CSV row by eye. Record the five vnums and the result in a comment block at the top of `test_parse_world.py`. If any disagrees, the field order is wrong; fix the parser before continuing.

- [ ] **Step 8: Produce the corpus CSVs and a summary**

```bash
mkdir -p "$SCRATCH/xp"
python3 tools/xp_research/parse_mobs.py lib/world/mob > "$SCRATCH/xp/mobs.csv"
python3 tools/xp_research/parse_zones.py lib/world/zon > "$SCRATCH/xp/zones.csv"
```

Report in the task summary: record count, count per mob-level bucket (1-9, 10-19, 20-29, 30-39, 40-49, 50-59, 60+), the range of `exp` in each bucket, how many zones are east of the river and their bonus percentages, and the ten mobs with the highest `exp / level` ratio (vnum, level, exp only). Do not commit the CSVs.

- [ ] **Step 9: Commit**

```bash
git add tools/xp_research/parse_mobs.py tools/xp_research/parse_zones.py tools/xp_research/test_parse_world.py
git commit -m "tools: offline parsers for mob and zone records mirroring db.cpp and zone.cpp"
```

---

### Task 3: XP model over the corpus (Sonnet; formula module reviewed by Fable)

**Files:**
- Create: `tools/xp_research/xp_formulas.py`, `tools/xp_research/test_xp_formulas.py`, `tools/xp_research/evaluate_corpus.py`
- Read: `tools/xp_research/FORMULAS.md` (Task 1 output), `docs/systems/combat-stat-examples.md:25-40` (damage and speed figures for pacing)

**Interfaces:**
- Consumes: `MobRecord` and `ZoneRecord` from Task 2; the ordered transformation list and `id` names from Task 1.
- Produces: pure functions in `xp_formulas.py`, integer arithmetic throughout (C++ truncating division, use `//` only on non-negative operands and a helper `cdiv(a, b)` that truncates toward zero otherwise):
  - `xp_to_level(level) -> int`
  - `levelb(level) -> int`
  - `gain_exp_clamp(gain) -> int`
  - `hit_xp_melee(attacker_level, victim_level, damage) -> int`
  - `hit_xp_mental(attacker_level, victim_level, damage) -> int`
  - `exp_with_modifiers(killer_level, killer_is_good_race, killer_is_good_align, killer_is_orc, mob: MobRecord, zone_x: int, difficulty: int, age_ticks: int, average_mob_life: int, base_exp: int) -> int` where `zone_x` is the x coordinate of the zone containing the killer's room (use the `M` command's load room, mapped to its zone by `top`), and `difficulty` is the `M` command's value (100 or 0 means unchanged; a mob loaded by several `M` commands is evaluated once per load row)
  - `kill_share(killer_levels: list[int], mob: MobRecord, attacked_level: int) -> list[int]` returning each killer's `base` before `exp_with_modifiers`
  - `solo_kill_xp(killer_level, mob, zone_x, **flags) -> int` composing the two above plus the clamp, with `attacked_level = levelb(killer_level)` and age at `average_mob_life`
  - `flee_loss(fleeing_level, opponent_level) -> int`
  - `death_loss(exp, level, full: bool) -> int`
  - `next_level_cost(level) -> int` = `xp_to_level(level + 1) - xp_to_level(level)`
- Produces: `python3 tools/xp_research/evaluate_corpus.py <mobs.csv> <zones.csv> > <out>/tiers.md` writing the Markdown tables listed in Step 6.

- [ ] **Step 1: Write the failing formula tests**

`tools/xp_research/test_xp_formulas.py`, one test per `id` from Task 1, with expected values computed by hand from `FORMULAS.md` and written into the test with the arithmetic shown in a comment. Minimum set:

```python
import unittest
from xp_formulas import (xp_to_level, levelb, gain_exp_clamp, hit_xp_melee, hit_xp_mental,
                         flee_loss, death_loss, next_level_cost)

class LevelCurve(unittest.TestCase):
    def test_xp_to_level_is_quadratic(self):
        self.assertEqual(xp_to_level(30), 1_350_000)
        self.assertEqual(xp_to_level(90), 12_150_000)

    def test_next_level_cost_at_each_tier(self):
        self.assertEqual(next_level_cost(30), 91_500)
        self.assertEqual(next_level_cost(90), 271_500)

    def test_levelb_caps_at_twenty_plus_a_third(self):
        self.assertEqual(levelb(30), 30)
        self.assertEqual(levelb(60), 40)
        self.assertEqual(levelb(90), 50)

class GainClamp(unittest.TestCase):
    def test_positive_events_cap_at_7000(self):
        self.assertEqual(gain_exp_clamp(250_000), 7000)
    def test_negative_events_cap_at_minus_10000(self):
        self.assertEqual(gain_exp_clamp(-250_000), -10_000)

class HittingXp(unittest.TestCase):
    def test_level_90_hitting_a_level_15_mob_at_the_cap(self):
        # (1+15) * min(20+180, 400) / (1+90) = 16*200/91 = 35
        self.assertEqual(hit_xp_melee(90, 15, 400), 35)
    def test_mental_attack_multiplies_damage_by_five_before_the_cap(self):
        self.assertEqual(hit_xp_mental(90, 15, 40), 35)

class Losses(unittest.TestCase):
    def test_flee_loss_is_the_sum_of_levels(self):
        self.assertEqual(flee_loss(75, 15), 90)
    def test_death_loss_tenth_and_full(self):
        # base = -(exp - 3000) / (level + 2) with C++ truncation toward zero.
        # At level 60 with exp = xp_to_level(60) = 5_400_000: -5_397_000 / 62 = -87_048
        # (Python's // would floor to -87_049; use literals, and cdiv in the implementation).
        exp = xp_to_level(60)
        self.assertEqual(death_loss(exp, 60, full=False), -8_704)          # -87_048 / 10
        self.assertEqual(death_loss(exp, 60, full=True), -8_704 - 87_048)  # tenth plus full

if __name__ == "__main__":
    unittest.main()
```

Add `exp_with_modifiers` and `kill_share` tests with a hand-built `MobRecord` (level 15, exp 3930, no flags, standing) for killer levels 30 and 90, showing the arithmetic in comments. The exact expected numbers come from Task 1's transformation list; if any test's arithmetic cannot be written from `FORMULAS.md` alone, that is a gap in Task 1 and goes back to the controller.

- [ ] **Step 2: Run the tests to verify they fail**

Run: `cd tools/xp_research && python3 -m unittest test_xp_formulas -v`
Expected: FAIL with `ModuleNotFoundError: No module named 'xp_formulas'`.

- [ ] **Step 3: Implement `xp_formulas.py`**

One function per `id`, each with a docstring citing the `file:line` from `FORMULAS.md` and the C++ expression verbatim. Use `cdiv` for any division whose numerator can be negative. No I/O in this module.

- [ ] **Step 4: Run the tests to verify they pass**

Run: `cd tools/xp_research && python3 -m unittest test_xp_formulas -v`
Expected: PASS.

- [ ] **Step 5: Controller review of the formula module**

Controller (Fable) reads `xp_formulas.py` next to `src/fight.cpp:1334-1390` and `src/fight.cpp:1490-1553` and confirms every branch, divisor order, and truncation matches. Any mismatch is fixed in the Python (never in the C++) and gets a regression test.

- [ ] **Step 6: Write `evaluate_corpus.py`**

Reads the two CSVs, drops pets and orc-friends into a separate count, and writes these Markdown tables to stdout, for tiers `[30, 40, 50, 60, 75, 90]`:

1. **Kill XP by mob level bucket and tier**: for each bucket (1-9, 10-19, 20-29, 30-39, 40-49, 50-59, 60+) and tier, the median and 90th percentile `solo_kill_xp` for a good-race, good-aligned killer in a non-east zone against a neutral-or-evil mob, after the clamp. Add one column per tier for the same numbers with the east bonus at +15 percent.
2. **Per-hit XP at the damage cap**: for each tier and victim level in `[15, 20, tier]`, `hit_xp_melee(tier, victim, 10_000)` and the number of such hits equal to the median level-matched kill from table 1.
3. **Hits available per mob**: for each bucket, median `max_hit` and the resulting per-hit-XP-per-mob (`hits_to_kill * hit_xp`) using the damage figure Task 3 reads from `docs/systems/combat-stat-examples.md` (record the figure used at the top of the output). If no per-tier damage figure exists in that document, use the cap `20 + 2 * tier` as the damage and say so; the cap makes per-hit XP independent of damage above it.
4. **Cost of failure**: for each tier, `flee_loss(tier, 15)`, `flee_loss(tier, tier)`, `death_loss` tenth and full at `exp = xp_to_level(tier)`, each also expressed as a percentage of `next_level_cost(tier)` and as a count of median level-matched kills.
5. **Clamp pressure**: for each tier, `next_level_cost(tier) / 7000` rounded up, the minimum number of gain events per level.
6. **Outliers**: the twenty mobs with the highest `solo_kill_xp` at tier 60 (vnum, zone, level, exp, flags), and the twenty highest at tier 30.

Run it and save the output:

```bash
python3 tools/xp_research/evaluate_corpus.py "$SCRATCH/xp/mobs.csv" "$SCRATCH/xp/zones.csv" > "$SCRATCH/xp/tiers.md"
```

- [ ] **Step 7: Commit**

```bash
git add tools/xp_research/xp_formulas.py tools/xp_research/test_xp_formulas.py tools/xp_research/evaluate_corpus.py
git commit -m "tools: Python mirror of the XP formulas and a corpus evaluator at six player tiers"
```

---

### Task 4: C++ pin tests for the live formulas (Sonnet writes; controller builds and runs)

**Files:**
- Create: `src/tests/xp_formula_tests.cpp`
- Read: `src/tests/fight_credit_tests.cpp` (fixture pattern for `group_gain`), `src/tests/damage_tests.cpp` (fixture and `combat_list` teardown pattern), `src/tests/CharPlayerDataBuilder.h`, `src/tests/CMakeLists.txt` or the test source glob in `src/CMakeLists.txt`
- Consumes: expected values from `tools/xp_research/test_xp_formulas.py` (Task 3) so the two suites pin identical numbers.

**Interfaces:**
- Produces: GoogleTest cases named `XpFormula.<Behavior>` that later progression work will change deliberately.

- [ ] **Step 1: Check how tests are registered**

Run: `grep -n "tests/" src/CMakeLists.txt | head`. If sources are globbed, a new file is picked up automatically; if listed, add `tests/xp_formula_tests.cpp` to the list in the same style.

- [ ] **Step 2: Write the tests**

Minimum set, using the builders from `CharPlayerDataBuilder.h` and the stack-character pattern in `fight_credit_tests.cpp`:

```cpp
// Pins the live XP formulas at the six research tiers so the Python mirror in
// tools/xp_research can be validated against the server and so a later
// progression change has to update these numbers deliberately.
#include "../fight.h"
#include "../limits.h"
#include "../structs.h"
#include "../utils.h"
#include "CharPlayerDataBuilder.h"
#include <gtest/gtest.h>

int exp_with_modifiers(char_data* character, char_data* dead_man, int base_exp);

TEST(XpFormula, LevelCostIsQuadratic)
{
    EXPECT_EQ(xp_to_level(30), 1350000) << "legend threshold cost";
    EXPECT_EQ(xp_to_level(90), 12150000) << "top mortal level cost";
}

TEST(XpFormula, GainExpClampsASingleEventToSevenThousand)
{
    // Build a level-60 PC with exp = xp_to_level(60), call gain_exp(ch, 250000),
    // expect GET_EXP == xp_to_level(60) + 7000.
}

TEST(XpFormula, KillModifiersForALevelNinetyKillerOnALevelFifteenMob)
{
    // Build a good-race, good-aligned level-90 PC in a room whose zone has x <= 8,
    // an NPC of level 15 with no flags, default_pos standing, age = average_mob_life,
    // difficulty 0. Call exp_with_modifiers(pc, mob, base) with the same base the Python
    // test uses and EXPECT_EQ the Python expected value. Repeat for killer level 30.
}

TEST(XpFormula, EastOfTheRiverAddsUpToFifteenPercentForGoodRaces)
{
    // Same fixture, zone x = 13 (bonus min(13-8,5)*3 = 15%). Compare against x = 8.
}

TEST(XpFormula, GoodKillingGoodTakesTwoThirds)
{
    // Same fixture, mob alignment made good; expect the 2/3 branch.
}
```

Fill each body following the fixture idioms already in `fight_credit_tests.cpp` (how it places characters in a room, sets `zone_table`, and resets `combat_list` in `TearDown`). The per-hit formula lives inside `damage()` and the flee and death formulas are inline in `do_flee` and `die`; do not test those through the server (`raw_kill` writes player files). Note them as "pinned in Python only" in the file's header comment.

- [ ] **Step 3: Controller builds and runs the suite**

Check `docker ps` first per the shared-container rule in Global constraints, then:

```bash
docker compose run --rm -T rots bash -lc 'cd /rots && cmake --build build --target ageland_tests -j8 && ./bin/tests --gtest_filter="XpFormula.*"'
```

Expected: all `XpFormula.*` tests pass. Then run the full suite once and diff the failing-test list against the 2026-09-15 baseline; the only acceptable difference is zero new failures.

- [ ] **Step 4: Reconcile with the Python mirror**

If a C++ pin disagrees with `test_xp_formulas.py`, the C++ is the truth. Fix `xp_formulas.py`, rerun the Python tests, and add a line to `FORMULAS.md` recording what was misread.

- [ ] **Step 5: Commit**

```bash
git add src/tests/xp_formula_tests.cpp
git commit -m "tests: pin the live kill-XP modifier and level-cost formulas at the research tiers"
```

---

### Task 5: Player population (Sonnet)

**Files:**
- Create: `tools/xp_research/player_levels.py`
- Read: `lib/players/players.csv` (columns `name,level,race`, **no header row**, 4149 rows on 2026-09-19), `docs/data-formats/player-save.md:30-50` (filename metadata)

**Interfaces:**
- Produces: `python3 tools/xp_research/player_levels.py lib/players > <out>/players.md` printing aggregate tables only.

- [ ] **Step 1: Level histogram from the CSV**

From `players.csv`, print counts per level band (1-9, 10-19, 20-29, 30-39, 40-49, 50-59, 60-74, 75-89, 90, 91+) and the counts at or above each tier (30, 40, 50, 60, 75, 90). Treat 91 and above as immortals and report them separately; on 2026-09-19 a raw count shows 93 characters at 90 or above, so most of the "high level" tail is likely staff, and the mortal 60 to 89 band is the number the synthesis needs.

- [ ] **Step 2: Activity weighting from filenames**

For each `lib/players/<bucket>/<name>.<level>.<race>.<idnum>.<log_time>.<flags>` file, read only the filename. Print the same histogram restricted to characters whose `log_time` is within 90, 365, and 730 days of the newest `log_time` in the directory. Do not open the files.

- [ ] **Step 3: XP position within level for a small sample**

Select the ten highest-level mortal characters (level 90 or below) plus three per tier band (30-39, 40-49, 50-59, 60-74, 75-89) from the filename metadata. For each, extract only the `level` and `exp` lines:

```bash
grep -E "^(level|exp) " "<file>"
```

Compute `(exp - xp_to_level(level)) / next_level_cost(level)` and print the distribution as a table of bands (0-25, 25-50, 50-75, 75-100 percent, and "below level floor" if any are inside the 20000 delevel tolerance). Print no names or idnums.

- [ ] **Step 4: Run and save**

```bash
python3 tools/xp_research/player_levels.py lib/players > "$SCRATCH/xp/players.md"
```

- [ ] **Step 5: Commit**

```bash
git add tools/xp_research/player_levels.py
git commit -m "tools: aggregate player level distribution for the progression research"
```

---

### Task 6: Synthesis document (Fable, main session)

**Files:**
- Create: `docs/systems/experience-and-progression.md`
- Modify: `docs/README.md:30-41` (add one row to the Gameplay systems table)
- Modify: `WIP.md` (one dated entry linking this plan and the findings document)
- Read: `tools/xp_research/FORMULAS.md`, `$SCRATCH/xp/tiers.md`, `$SCRATCH/xp/players.md`, `docs/_TEMPLATE.md`

- [ ] **Step 1: Write the document following `docs/_TEMPLATE.md`**

Sections, in template order:

- **Header**: source files with the functions traced; status ✅; an **As of** line with the date and branch `fix/spell-room-affect-uaf-port`.
- **Purpose**: two paragraphs, the second stating that this is the baseline for the progression redesign and that the redesign itself is not described here.
- **Data structures**: `points.exp`, `player.level`, mini-level, `specials.attacked_level`, `GET_LEVELB`, the `gain_exp` clamps, the delevel tolerance.
- **Format / Algorithm**: the formula table from `FORMULAS.md`, then the ordered `exp_with_modifiers` list, then the flee and death rules, then the "other vectors" inventory that answers "are there other XP loss mechanisms" in one sentence per vector.
- **RotS-specific notes**: the east-of-river faction bonus, the `TEMPORARY` low-level bonus and where it vanishes, good-on-good, orc-friend, the fact that hitting XP fires per damage event with its own clamp.
- **Worked example**: tables 1 through 5 from `tiers.md`, each followed by one or two sentences saying what the numbers mean. Then the population tables from `players.md`.
- **Incentive synthesis**: answer the five hypotheses from Task 1 Step 8 directly, one short paragraph each, with the supporting table referenced by number. State plainly whether the data supports "high-level players are rewarded for farming low-level mobs", and by which vector.
- **Open questions**: anything the code left ambiguous (for example, whether any live script grants XP, how `average_mob_life` is tuned in practice), and what only live telemetry could answer.

- [ ] **Step 2: Index and log**

Add to `docs/README.md` after line 40:

```
| [Experience & progression](systems/experience-and-progression.md) | ✅ gain/loss vectors, kill/hit formulas, tier tables, population | `limits.cpp`, `fight.cpp`, `act_offe.cpp`, `clerics.cpp` |
```

Add a dated entry to `WIP.md` with two lines: the plan path and the findings path.

- [ ] **Step 3: Review the document for the comment-brevity rule**

Read it once for repeated facts and cut every second statement of the same number. Check that no character name, idnum, or file path under `lib/players/` appears.

- [ ] **Step 4: Commit**

```bash
git add docs/systems/experience-and-progression.md docs/README.md WIP.md
git commit -m "docs: experience and progression baseline for the XP redesign"
```

---

## Self-review notes (2026-09-19)

- **Scope coverage**: question 1 → Tasks 1, 3, 4; question 2 → Task 1 Steps 6 and 7, Task 3 table 4; question 3 → Task 2, Task 3 tables 1 to 3 and 6; question 4 → Task 3 table 4; question 5 → Task 1 Step 5 item 8, Task 2 zones, Task 3 table 1 east columns, Task 4 east pin; question 6 → Task 5.
- **Known unknowns handed to the executor rather than guessed**: the `MOB_*` bit values, where `GET_DIFFICULTY` is populated, the value of `average_mob_life`, and whether any live Mudlle script grants XP. Each has an explicit lookup step.
- **Name consistency**: the `id` list in Task 1 matches the function names in Task 3 and the `XpFormula.*` test names in Task 4 describe the same behaviours.
