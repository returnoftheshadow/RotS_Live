# Research tools

Small read-only Python 3 tools that answer questions about the game from its source and world files:
which guildmaster teaches what, how commands are registered, where actions set a wait state, which checks
a command handler makes, how fast practice raises a skill, and how mobs are spread across levels.

They use only the standard library. Each one takes `--repo DIR`, which defaults to the repository that
contains `tools/research/`, so from the repository root no option is needed. Tools that write files take
`--out DIR`; tools that print take `--out FILE` to write the report to a file instead. Every tool has
`--help`.

Run the tests with:

```
python3 tools/research/research_tests.py
```

They use small inline fixtures and do not read the repository.

## The tools

### skill_data.py: skills, guilds and guildmasters as JSON

Reads `skills[]` and `guildmasters[]` from `src/consts.cpp`, the `SKILL_`/`SPELL_`/`LANG_` defines from
`src/spells.h` and `src/structs.h`, the `ASSIGNMOB(<vnum>, guild)` lines in `src/spec_ass.cpp`, and each
guildmaster's mob record, zone loads and load-room names from `lib/world/`.

```
python3 tools/research/skill_data.py --out /tmp/research --summary
skills rows: 167; guild rows: 61; guildmaster vnums: 60
```

Writes `skill_data.json` (keys `skills`, `guilds`, `guildmasters`, `mob_spec_shift`) and, with
`--summary`, `skill_data.txt`, for example:

```
  1 slashing               | warrior   0 |   0   0 16             |  30   1 | nothing        | SKILL_SLASH,SPELL_TYPE_POTION | consts.cpp:399
2503 | an honorable thief | guild 23 PICK LOCK (23) | Human | spec=True | teaches [] | rp any | 2584 Store Room [Deep Mirkwood]
```

### class_packs.py: one data pack per class

For warrior, ranger, mystic, mage and general, writes `pack_<class>.md` and `pack_<class>.json`: each
skill's row and every trainer of it, with the guild's cap, the mob, its race, the races it teaches, where it
loads and the problems that stop it teaching. Reads the repository, or `--data skill_data.json`.

```
python3 tools/research/class_packs.py --out /tmp/research
warrior 30
ranger 27
mystic 41
mage 36
general 3
```

A pack line:

```
- 100%: the tall, strong guard (mob 1510, Human), guild 2 'VINYANOST WARRIOR (2)', teaches ['God', 'Human', 'Dwarf', 'Wood Elf', 'Hobbit', 'High Elf'], loads in room 1520 'A Training Yard' (Vinyanost, 15.zon)
```

### trainer_tables.py: the "Where to train" table for one class

```
python3 tools/research/trainer_tables.py warrior
| Trainer | Where | Teaches | Weapon skills | Other warrior skills (cap %) |
...
UNUSABLE: Guildmaster (mob 1109): never loaded by a zone reset
```

Warrior rows summarise the ten weapon skills as "N of 10, up to C".

### command_table.py: command registrations

Every `COMMANDO(...)` in `assign_command_pointers()` (`src/interpre.cpp`) with its number, its name from
`command[]`, position, handler, level, retired flag, subcommand, both target masks and mask; tallies by
position, level, retired flag and mask; the numbers with no registration; and where `sort_commands()`
stops. `--summary-only` leaves out the table.

```
python3 tools/research/command_table.py --summary-only
COMMANDO registrations: 247
...
== unregistered numbers (name in command[])
 111  ''
 194  'slowns'
...
sort_commands() stops after 110: commands 111 and above are not in its sorted list
```

### wait_calls.py: wait-state calls

Every `WAIT_STATE`, `WAIT_STATE_BRIEF` and `WAIT_STATE_FULL` call in `src/*.cpp` with its cycle, command,
subcommand, priority and flags. `WAIT_STATE`'s implied values come from its definition in `src/utils.h`.
`--by-priority` groups the calls.

```
python3 tools/research/wait_calls.py
src/act_info.cpp:1499 WAIT_STATE cycle=4 cmd=0 sub=0 prio=50 (implicit) flags=AFF_WAITING
src/act_info.cpp:3381 WAIT_STATE_FULL cycle=10 cmd=CMD_SEARCH sub=2 prio=30 flags=AFF_WAITING | AFF_WAITWHEEL
...
56 calls
```

### handler_gates.py: checks inside command handlers

For the named handlers (`bash` or `do_bash`), or every `ACMD(do_...)` handler when none is named, prints
the handler's line range and the lines that match a gate pattern: skill, race, mana, move, spirit,
specialization, level, riding, peace-room, shadow and target checks. `--pattern REGEX` replaces the pattern.

```
python3 tools/research/handler_gates.py hide
== do_hide src/ranger.cpp:646-735
   ...
```

### practice_curve.py: knowledge after N practice sessions

Follows `recalc_skills()` (`src/spec_pro.cpp:120-157`). For a warrior skill give `--difficulty`
(learn_diff). For another profession also give `--coefficient` (the character's `GET_PROF_COOF`, 0-1000,
after its race adjustment; general skills use 1000) and `--level`. `--weapon-skill --mastery-sessions M`
adds M practices of weapon mastery, which count for skills 1-6 and 9. `--skill N` takes the difficulty,
level, profession and weapon rule from `skills[N]`.

```
python3 tools/research/practice_curve.py --difficulty 10 --sessions 6
sessions  knowledge
       1         36
       2         64
       3         84
       4         96
       5        101  above 100
       6        100
```

### mob_census.py: mobs by a numeric field

Counts the world's mobs by one of the 40 record fields (`--field`, level by default) and, with `--value`,
lists the vnums that have that value.

```
python3 tools/research/mob_census.py --top 3
3723 mobs; 3723 with a level; 53 distinct values
 count  level
   994  0
   231  20
   206  10
```

## Rules the tools follow

- A guild's number is its position in `guildmasters[]`, counted from 1. A guildmaster mob's program field
  selects its guild.
- A guild array shorter than `MAX_SKILLS` teaches the missing skills to 0.
- A trainer is unusable when its mob is missing, lacks the SPEC flag (act bit 0), has a program number
  outside the guild table, has a `will_teach` race mask of 0, or is never loaded by a zone `M` line.
- A load's zone is the zone of its room, taken from the `.wld` file that holds the room. A zone's reset file can
  load a mob into another zone's room; pack lines and trainer tables then also name the zone that loads it.
- A mob record that stops before its 40th integer has `will_teach` 0, as the loader leaves it
  (`src/db.cpp:2016-2033`).
- `skills[151]` has an empty name and type `PROF_GENERAL` but is the ranger trap skill; packs file it under
  ranger as "trap (row 151 has an empty name)" and keep its source type. `stomp` and `recruit` are general skills listed under warrior; internal rows (asphyxiation, Power
  of Arda, activity, rage, anger, Fame War, nothing, trash and empty names) are left out.
- Files are read as latin-1 with Python's universal newlines, so the `\n\r` line endings of world files read
  as blank-line pairs. World files are read in zone-number order.

## Known limits

- The C parsing is pattern-based, not a C parser. It relies on the formatting the sources have now: guild
  entries open with a four-space-indented `{`, a function body ends at the first line that is exactly `}`,
  and macros are not expanded (a `WAIT_STATE` inside another macro's body is not found).
- C comments are skipped everywhere except where a label is read from them (guild labels and
  `ASSIGNMOB` comments). `#if 0` blocks are not skipped.
- Only `src/*.cpp` is searched by wait_calls.py and handler_gates.py; `src/wait_functions.cpp` is left out
  of wait_calls.py.
- practice_curve.py does not compute `GET_PROF_COOF` from a character (its race adjustment and square-root
  table); give the coefficient directly. It does not model 32-bit overflow.
- A mob record that stops early leaves the fields after its last integer unset, apart from `will_teach`;
  mob_census.py does not count those mobs for the missing fields.
