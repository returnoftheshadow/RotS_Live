---
name: rots-integration-scenario
description: Use when adding, changing or debugging a RotS integration scenario (tests/integration), when a behaviour players see can only be checked on a running server, when pinning today's behaviour before changing it, or when changing the integration harness, its schedules, control requests or ageland_harness options.
---

# Writing a RotS integration scenario

A scenario boots its own server on the test world and its declared characters, logs them in
and checks what they read. Work through the steps in order. Commands, formats and values are in
[harness-reference.md](harness-reference.md); changing the harness is in
[extending-the-harness.md](extending-the-harness.md).

## 1. Is a scenario the right test?

Only when the behaviour needs a running server: commands and their messages, several
characters online, or game time passing. A rule inside one function, on state a fixture can
build, is a GoogleTest test in `src/tests/`.

## 2. Read the behaviour in the code

- Follow the command from the interpreter (`interpre.cpp`) to every outcome, through the
  shared code on the way (casting in `do_cast`, skills, movement, combat).
- Note the order of the checks, each message's exact text and who receives it (`act()`'s
  `TO_CHAR`, `TO_VICT`, `TO_ROOM`), and which character, room or zone each check reads.
- List every random draw on that path: `number()`, `dice()` and whatever calls them. Check each
  roll's bounds: `number(0, 100) >= chance` still fails once in 101 at a chance of 100.
- List every input the behaviour reads and how it is controlled (table below).
- Read the numbers the outcome rests on in game with `stat player <name>`, after `restore`
  when `wizset` changed an ability (reference, "Abilities").

An input the harness cannot control needs a harness extension first, approved by whoever owns
the harness (extending-the-harness.md); never rely on luck.

| Input | Where the code reads it | Controllable today |
| --- | --- | --- |
| Light | `IS_DARK` (`utils.h`): the room's light count, its `DARK` flag, and sunlight in any sector but `SECT_INSIDE` and `SECT_CITY`. `CAN_SEE` (`utility.cpp`): blindness, `AFF_INFRARED`, `PRF_HOLYLIGHT`, and `AFF_MOONVISION` when `OUTSIDE` under moonlight | Yes: room flags in the test world and the implementor's `set holylight on`. The test world has no light object; adding one is a world change (step 6) |
| Outdoors and sector | `OUTSIDE()` (`utils.h`) is true unless the room has the `INDOORS` flag, whatever its sector. `get_sun_level` (`weather.cpp`) is 0 in `SECT_INSIDE`, in a `DARK` room and at night; otherwise sector, sky, snow and `SHADOWY` set it | Yes: the room's flags and sector. Every test-world room is `SECT_INSIDE` without `INDOORS`: `OUTSIDE()` is true, but it is never sunlit |
| Time of day and sunlight | `weather_info.sunlight`, set by `initialize_weather` from the game clock, which `reset_time` (`db.cpp`) takes from the wall clock at boot; `weather_and_time` moves it each game hour | Partly: `advance_hours` moves the clock, but the starting hour, the month and the moon's phase all come from the wall clock at boot. The month sets sunrise and sunset (`sun_events`, `weather.cpp`), so setting the hour alone does not fix them. A known time needs a harness extension |
| Weather, sky (rain, lightning), moonlight | `weather_info`, changed each game hour by `weather_change` (called from `weather_and_time`) with `dice()` draws | No: needs a harness extension. `weather_change` redraws the sky from the pressure every advanced hour, so a sky set alone lasts only until the next hour |
| Race | `other_side` (`handler.cpp`) for sides and name lookups; `PERS` (`utility.cpp`) star names; `race_affect` (`consts.cpp`): infravision for beorning, uruk-hai, orc, uruk-lhuth and olog-hai, moonvision for wood elf and haradrim; `EVIL_RACE` (`utils.h`) | Yes: the character's `race` |
| Power of Arda | `do_power_of_arda` (`limits.cpp`, from `fast_update` and on movement, `EVIL_RACE` only): Haradrim exempt, Olog-hai triple, scaled by the room's `get_sun_level`. `get_power_of_arda` (`utility.cpp`) lowers casting in `do_cast` (`spell_pa.cpp`) and combat values | Race and room, yes; sunlight only partly (above). In the test world as it stands `get_sun_level` is 0, so it never applies |

## 3. Decide the pin

- A **legacy pin** records today's behaviour before a change: written first, against unchanged
  code, and passing there. Its docstring says so, and that it becomes a strict xfail when the
  behaviour changes.
- The commit that changes the behaviour marks it
  `@pytest.mark.xfail(strict=True, raises=AssertionError, reason="<the new behaviour>")`; strict
  makes it fail the run if it ever passes again, and `raises` makes any other error (a dropped
  connection, a missing fixture file, a typo) fail the run instead of counting as the expected
  failure. The harness's timeouts are `AssertionError`s, so a pin can still fail by waiting for
  text that never comes. `unit/test_scenario_markers.py` rejects an xfail without both.
- A **new-behaviour pin** fails before the change and passes after it.
- A crash or memory-safety fix gets no legacy pin.

## 4. Make the outcome deterministic by construction

- Choose levels, abilities and rooms so no roll decides the outcome: drive a value to a clamp,
  or put one side of a comparison out of reach.
- A margin check (asserting from `stat` values that no roll can change the outcome) REQUIRES,
  for every case including the control, the comment the reference's "Margin checks" shows:
  the code's expression evaluated term by term, ending in the value passed.
- Mark the test `@pytest.mark.schedule("affect-hours")`, so only the scenario's own commands
  and the hours it asks for draw.
- A draw that only decides whether the attempt happens and has its own message (a lost
  concentration, a fumbled skill): repeat the attempt on that message, listed in `expect`.
  Bound the loop by what each attempt uses up (mana does not regenerate between requests under
  `affect-hours`) so it ends in an assertion, and name the draw in the docstring.
- A draw that decides the outcome and cannot be pushed out of reach needs a harness extension
  (step 2), not a lucky seed. The seed is never why a test passes; step 9's other seeds prove
  it.

## 5. Declare the characters

Use `@pytest.mark.characters(...)` (fields in the reference). Starting values go in the
character: a mortal's `load_room` (an immortal always starts in 1101) and its `stored` values
(hunger, gold, abilities, spirit, alignment, specialization and more, each limited like its
`wizset` field). Mobs and floor objects the test starts with go in
`@pytest.mark.world_resets(...)`. The implementor sets what changes during the test and what
neither can hold (items, practice sessions, and the `saving_throw` field, which only some save
functions read); assert each reply, such as `hunger set to 10.`. Declare `IMPLEMENTOR` whenever
two characters are online. Across sides a command matches a player's race keyword, never its
name (reference, "Names, sides and lookups").


## 6. Change the world only when it lacks what you need

Find what the test world has with `make world-catalogue PYTHON=.venv/bin/python` (`ZONE=<n>`,
`CATEGORY=mobs`) and the zone notes in `tests/integration/world/notes/`; the rules and gotchas
are in the reference, "The test world".

- A new scenario area adds its own zone: the next zone number (19 today), its own world files,
  vnums from `n * 100` and map square, and its notes file `world/notes/zone-<n>.md`. It appends
  to zone 11's files only for a fixture zone 11's existing scenarios need.
- List each new file in its category's `index` by first vnum, keep vnums ascending, and write
  each fixture's purpose and quirks in its zone's notes.
- Run `make integration-unit` (its world check catches order, index and zone mistakes), then
  boot after every change: a bad field shows only as a `SYSERR` or a server that never reaches
  the login prompt.
- A room or value only one scenario file uses is a constant in that file;
  `rots_harness/world.py` holds only the rooms two or more scenario files name.

The world files and their notes, the scenario files with their constants, the shared room
constants in `rots_harness/world.py` and the group files in `tests/integration/groups/` are
scenario territory; the rest of `tests/integration/` and `src/harness/` is the harness.

## 7. Assert what players see

- Match each message's exact text as that observer receives it: `someone` for an actor it
  cannot see, or a star name across sides (`PERS`, reference "Names, sides and lookups").
- Give `expect` every outcome the code can produce at that step, then assert which arrived, so
  a changed behaviour fails at once with the transcript rather than as a `SessionTimeout`.
- Check what the helpers return in odd states (reference, "Session API").
- Keep each pin to one behaviour. A control case (the same command succeeding once one input
  changes) must avoid the scenario's other quirks, or one break fails two pins.
- Advance time only with `server.advance_hours(n)`; read each hour's text with `expect` and
  `drain` before the next `command()`, which discards unread text.

## 8. Tag the group

Add `tests/integration/groups/<name>.txt` holding one line, what the group pins, and set
`pytestmark = [pytest.mark.scenario, pytest.mark.<name>]`. `GROUP=<name>` then selects it.
When a group passes about 20 tests or covers several behaviours, split it into subgroups:
`groups/<name>/<sub>.txt` registers `<name>_<sub>`, and every test of the group then marks exactly
one subgroup as well (in `pytestmark` for a whole file, or `@pytest.mark.<name>_<sub>` per test).
A new test in a group with subgroups carries one; collection refuses it otherwise. A known-bug
pin joins the subgroup of the behaviour it pins.
In CI, a pull request runs `boot` and the groups of the scenario and group files it changes (only
the subgroups a changed scenario file marks, plus the groups of the scenario files importing it);
a change to the game's code runs a group or subgroup only when the description names it as
`@integration-test-<name>`. A change to the harness, the test world, `src/CMakeLists.txt` or a
`lib/` file the harness copies into every test's lib runs every group, as does every push
(integration README, "Continuous integration").
To run a scenario against an AddressSanitizer build, such as one that proves a memory fix, name
it in the description as `@asan-test-scenario=scenarios/test_<file>.py::<test>`, or its group as
`@asan-test-<name>` (integration README, "Memory checks on request (ASan)").

## 9. Run it repeatedly

- While writing, run one test (reference, "Running"); run `make integration-unit` when the
  harness changed.
- Your scenarios are every test that uses something you added or changed: its body, a
  helper, a fixture, a module-level constant or `Character`, a `parametrize` case, a marker,
  or a world fixture it reads. A change to the harness itself (`rots_harness/`, `conftest.py`)
  follows extending-the-harness.md instead.
- Check that CI will run them: `.venv/bin/python -m pytest --collect-only -q -m "scenario and
  (<group>)" <node ids>` must list every one of your node ids. One it leaves out lacks
  `pytest.mark.scenario` or its group marker, so no `make integration` run would select it,
  yet a run by node id still passes.
- Repeat your scenarios, selected by node id (reference, "Running"). Each scenario boots its
  own server, so repeating the rest of its group adds no evidence about them:
  - three times through the Docker launcher (on Linux, with `ROTS_IT_LAUNCHER=docker`), and
    three times through the local launcher (on a Mac, inside a container). Between runs, the
    starting game hour, month and moon follow the wall clock at boot, commands from different
    connections arrive in any order, asynchronous text can land before or after a prompt, a
    plain server's mobs and combat draw on the wall clock, and the two launchers time a
    server differently, so one pass can be luck;
  - once each with `ROTS_IT_SEED=1` and `ROTS_IT_SEED=987654`, which proves the seed is not
    why they pass (step 4).
- A game hour is one real minute, so back-to-back runs start within a few game hours of each
  other. When an outcome can depend on the time of day (sunlight, the moon, a banker's
  hours), start the runs on each launcher at least 8 minutes apart, so they begin in
  different thirds of the game day.
- The repeats prove a new or changed scenario is stable. Do them when you write or change it,
  not again for each rebase or rebuild of the same scenario.
- Then run every group your scenarios belong to once, `make integration
  PYTHON=.venv/bin/python GROUP=<name>`, which catches a change that breaks a neighbour in the
  same file. A change under `tests/integration/world/`, or to a scenario module that is not a
  `test_*.py`, can break any group: run every scenario once (`make integration
  PYTHON=.venv/bin/python`) instead.
- `make integration` prints its plan first and, without a terminal, stops when the estimate
  reaches 10 minutes, printing a line that starts `ROTS_IT_NEEDS_CONFIRMATION:`. Look for that
  line, not the exit status: `make` exits 2 for it, as for any failure. Before such a run, run
  `make integration-plan` with the same `GROUP`, show your user the plan as a checklist and ask
  which groups to keep, then run with `SKIP_GROUPS=` (or `GROUP=`) and `CONFIRM=yes`. Never pass
  `CONFIRM=yes` for a run over the threshold without your user's answer (reference, "Running").
- Every run must pass.
- Wait out lock waits; `DockerLockHeld` is not a result (reference, "The shared Docker
  lock").
- On a failure, read the kept run directory (reference, "Run directories"); remove only your
  run's, by name.

## 10. Prove the pin bites

For each pin, break the behaviour it pins with an uncommitted edit in `src/`, rebuild, and run
your scenarios. The pin must fail on an assertion about that behaviour, not on a timeout or
setup error. Then undo the edit with `git restore <files>`, rebuild and see it pass. Never
commit the break.

Commit only after the bite checks and the required runs. The commit message's `Tests:` list
(each run and its result) names only what ran; amend it if a later run changes the result.

## Common mistakes

| Mistake | What to do instead |
| --- | --- |
| Working out a threshold or modifier from the formula | Read it with `stat player <name>` and assert the values the outcome rests on |
| Reading only the feature's function, so a roll in shared code (casting, combat) goes unseen | Trace from the interpreter; list every `number()` and `dice()` on the path |
| A margin with a term taken as 0 or neutral ("this term is 0 here") | Write the "Margin checks" comment for every case; each term's value names where the code computes it |
| `wizset ... int 40`, then reading the margin from the old current value | `restore <name>` after `wizset`, then `stat player` |
| Believing a pin tells two behaviours apart | Break each one and watch the pin fail |
| A guessed marker | Run once with `ROTS_IT_KEEP=1`, read the transcript, then find the string in `src/` |
| A fixed `time.sleep` or `drain(n)` as the wait | `expect` the event; `drain` only to settle after `expect` or `advance_hours` |
| `command()` after asynchronous text (hour messages, `The day has now begun.`) | `command()` returns at the first text ending in `>` or `]`; read pending text first, or `send_line` and `expect` the reply |
| A long reply read with `command()` (`stat` has lines ending in `]`) | `send_line`, then `expect` its last line and `drain` |
| Asserting exact hit points after several game hours | Regeneration runs in every hour: check once after the first hour, or assert a range |
| Trusting another connection's command right after a `teleport` | Commands from different connections are not ordered; `expect` the moved character's `has teleported you!` |
| `wizset <name> level` to change profession levels | It sets only the overall level, and `wizset ... prof` does nothing; declare the level and class |
| `wizset <name> room <vnum>` | It matches `roomflag`; set the room with `load_room`, or `teleport` a same-side character |
| Naming an orc, uruk, olog or haradrim player in `give`, `teleport`, `goto` or any targeted command | Across sides a name never matches, and `orc` can pick a mob; use `load_room`, the `player` forms, or `N.<race>` with an asserted reply |
| An immortal as the target of a mortal-only effect | Use a mortal; immortals are spared some effects (food poison, for one) |
| A gating roll left to the seed | Repeat the attempt on its message, bounded by what each attempt uses up |
| A retry loop bounded by a count | Bound it by the mechanism's duration and fail with a message when the precondition is gone |
| Holding your own lock on the checkout's `build/` tree around `make integration` with the Docker launcher | Take none; the harness locks that tree itself and waits for any other lock on it |
| Deleting kept run directories you did not create | Remove only the ones your run kept, by name; the rest are someone's evidence |
