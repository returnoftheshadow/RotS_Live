# Harness reference

The facts a scenario writer needs, cited by file and function. Paths are relative to the
repository root; `tests/integration/` is the harness root. When this file and the code disagree,
the code wins: fix this file in the same change.

## Running

Build the three programs once (32-bit Linux binaries; on a Mac, inside the container, as a
Docker job under a lock you take by hand, as "The shared Docker lock" below describes):

```bash
make configure && cmake --build build --target ageland ageland_harness rotstool
docker compose run --rm -T rots bash -lc 'make configure && cmake --build build --target ageland ageland_harness rotstool'
```

`make build` builds only `ageland`. A test with `@pytest.mark.schedule(...)` also needs
`bin/ageland_harness`; a missing program stops the run with the build command for its launcher
(`ServerLauncher.require_program` in `rots_harness/launcher.py`).

Create the virtual environment once: `python3 -m venv .venv && .venv/bin/pip install -r
tests/integration/requirements.txt`.

| What | Command (from the repository root) |
| --- | --- |
| The harness's own tests, no server | `make integration-unit PYTHON=.venv/bin/python` |
| Every scenario | `make integration PYTHON=.venv/bin/python` |
| Every scenario, on 8 parallel workers | `make integration PYTHON=.venv/bin/python JOBS=8` |
| One group | `make integration PYTHON=.venv/bin/python GROUP=poison` |
| One subgroup of a large group | `make integration PYTHON=.venv/bin/python GROUP=shaping_zone_editor` |
| Several groups | `make integration PYTHON=.venv/bin/python GROUP="boot or poison"` |
| Only show the plan: each group's tests and estimated minutes | `make integration-plan PYTHON=.venv/bin/python GROUP="boot or poison"` |
| Every scenario but some groups, without asking | `make integration PYTHON=.venv/bin/python SKIP_GROUPS="shaping practice" CONFIRM=yes` |
| One test, by part of its name; `-k` also matches marker names, so a fragment such as `saving` or `sessions` pulls in whole subgroups: use the node id or a longer fragment | `.venv/bin/python -m pytest tests/integration/scenarios -q -m scenario -k tainted_bread` |
| One test, by node id | `.venv/bin/python -m pytest -q -m scenario "tests/integration/scenarios/test_boot.py::test_an_implementor_logs_in_to_immortal_start"` |
| Several tests, by node id | `.venv/bin/python -m pytest -q -m scenario "tests/integration/scenarios/test_bank.py::test_a_banker_refuses_a_gift" "tests/integration/scenarios/test_bank.py::test_a_banker_refuses_an_attack_and_no_fight_starts"` |
| Every test in a file | `.venv/bin/python -m pytest -q -m scenario tests/integration/scenarios/test_bank.py` |
| Whether CI's group filter selects your tests | `.venv/bin/python -m pytest --collect-only -q -m "scenario and (bank)" <node ids>` |
| The groups your changes reach: CI's rules (`rots_harness/ci_groups.py`, which already run every group for `src/CMakeLists.txt`, `lib/text/` or a copied `lib/misc` file) on changes since `BASE` (default `origin/release-frodo-next`), uncommitted and untracked files, plus the local-only rule: every group for a changed `src/**/*.h` outside `src/harness/` and `src/tests/` (`rots_harness/changed_groups.py`) | `make integration-changed PYTHON=.venv/bin/python` |
| Only print that choice and its reasons | `PYTHONPATH=tests/integration .venv/bin/python -m rots_harness.changed_groups --list-only` |
| Build the three programs (inside the container on a Mac, under the build lock) | `make integration-build`; `docker compose run --rm -T rots bash -lc 'make integration-build'` |

`make integration` runs `$(PYTHON) -m pytest tests/integration/scenarios -q -m "scenario and
(<GROUP>)"`, adding `and not (<skipped groups>)` for `SKIP_GROUPS` and unticked groups and `-n
<JOBS>` when `JOBS` is above 1, and writes each test's result and time to
`build/integration/results.xml` and lists the 25 slowest.

Before that it shows its plan (`rots_harness/run_plan.py`): each selected group with the tests
it will run (this shard's, under `ROTS_IT_SHARD`) and minutes estimated from
`build/integration/durations.json`, which runs update with the times of the scenarios that ran
to the end (15 s for a test without a time). At a terminal the groups are checkboxes to untick,
and no answer starts the run after 5 minutes. Without a terminal, an estimate of
`ROTS_IT_CONFIRM_MINUTES` (default 10) or more, without `CONFIRM=yes`, prints a line starting
`ROTS_IT_NEEDS_CONFIRMATION:` and stops. Detect that line: the run plan's own status is 10, but
`make` exits 2 for it as for every failure. An agent then runs `make integration-plan`, shows its
user the plan as a checklist, asks which groups to keep, and reruns with `SKIP_GROUPS=` (or
`GROUP=`) and `CONFIRM=yes`; it never passes `CONFIRM=yes` for such a run without that answer. A
cancelled prompt exits 11, a bad `SKIP_GROUPS`, `CONFIRM`, `ROTS_IT_CONFIRM_MINUTES` or
`ROTS_IT_SHARD` exits 2, and `CI=true` skips the plan and question but still applies
`SKIP_GROUPS`. After a run that left groups out, a `Repeat:` line gives the command that repeats
it. Run directly, pytest finds the same
`tests/integration/pytest.ini` and rootdir and reads the same `ROTS_IT_*` variables as `make
integration`, but only `make integration` adds the group filter. A node id runs its test even
when the test lacks its group marker, so step 9 checks membership with `--collect-only`. With
`-m scenario`, a lone node id whose test lacks `scenario` collects nothing and exits 5; among
several, it only adds to `N deselected`.
`-k` is not checked: a name that matches nothing runs nothing and prints only `N deselected`.
A node id that names nothing is an error (`ERROR: not found`), so node ids are the safer way to
pick the tests step 9 repeats. `--collect-only -q` prints node ids relative to
`tests/integration` (`scenarios/test_bank.py::...`): put `tests/integration/` in front of each
to run it from the repository root. A parametrized test's node id includes its case in
brackets; quote it in the shell. Unset `ROTS_IT_SHARD` for these runs: a shard can leave a run
nothing to run, and that run still passes.
`-m` is checked: an unregistered name in it is a usage error listing the registered names
(`pytest_configure` in `conftest.py`, `rots_harness/groups.py`).

Use `JOBS=8` for a run of many scenarios, such as the full suite or a large group: each
scenario still gets its own server, and scenarios mostly wait on an idle server, so the run
takes a fraction of the serial time. Each run has its own template folder,
`build/integration/libs/<session id>/`, so two runs in one checkout do not disturb each other
(with the Docker launcher the second waits for the first's lock); the README's "Running"
section has the details.

### Launchers

| Launcher | Chosen by | How it runs the programs |
| --- | --- | --- |
| `docker` | default off Linux, or `ROTS_IT_LAUNCHER=docker` | `docker compose run --rm -T --name rots-it-<hex>` with this checkout's `rots` service, each server on a random loopback port. Locks this checkout's build tree itself for the whole session (`rots_harness/dockerlock.py`); runs alongside jobs on other resources |
| `local` | default on Linux, or `ROTS_IT_LAUNCHER=local` | runs `bin/` directly; needs the i386 runtime (`libc6:i386`). Takes no lock |

### The shared Docker lock

Docker jobs on one host share a lock directory, `/tmp/rots-docker-lock` by default
(`ROTS_IT_DOCKER_LOCK_DIR`), and run at the same time. Each job locks only the resources it
uses: `build:<checkout>/<tree>` (a build into that tree, or programs run from it),
`port:<n>` (a published host port), `game:<checkout>` (a game on that checkout's own `lib/`) and
the optional `timing`. `<checkout>` is the checkout's folder name. The integration README's
"Shared Docker lock" section has the table and the lock-file format.

- **The harness's own lock.** With the Docker launcher the harness takes
  `harness-it-<pid>-<id>.lock` on `build:<checkout>/build` for the whole pytest session
  (`DockerLock`, `rots_harness/dockerlock.py`). Builds and games in other checkouts, and harness
  runs in other checkouts, run alongside it. It waits only for a lock file that holds the same
  tree: a build into it, another harness session in the same checkout, an old-style lock (no
  `resources:` line) for the same checkout, or a lock file it cannot read.
- **When the wait runs out**, the run stops with `DockerLockHeld` naming the lock files that
  held the tree. That is not a test result: rerun with a longer wait, such as
  `ROTS_IT_LOCK_WAIT=3600` (seconds; default 600). A required run is waited out, never skipped.
- **Do not hold a lock on this checkout's `build/` tree** around `make integration` with the
  Docker launcher: the harness waits for it, then fails.
- **A Docker job outside the harness** (a build in the container, `make test`, or the local
  launcher in a container below) takes a lock by hand on the resources it uses; each of these
  uses `build:<checkout>/build`. Create the directory if it is missing
  (`mkdir -p /tmp/rots-docker-lock`), wait until no other lock file names one of those
  resources, create a lock file with a session-unique name (for example
  `<your name>-<purpose>-<random hex>.lock`) in the README's format, run the job, and delete
  that file when the job ends, however it ends.

### The local launcher on a Mac

The local launcher needs a Linux host that runs i386 binaries, so on a Mac it runs inside a
throwaway container of the compose service's image (Ubuntu 22.04, which has the 32-bit runtime
and Python). Take a lock by hand first (above) and delete it afterwards. From the
repository root, with `ROTS_UID` and `ROTS_GID` exported
(`export ROTS_UID=$(id -u) ROTS_GID=$(id -g)`):

```bash
docker compose -f docker-compose.yml run --rm -T rots bash -lc '
  ulimit -c 0
  python3 -m venv /tmp/harness-venv && /tmp/harness-venv/bin/pip install -q -r tests/integration/requirements.txt || exit 1
  for run in 1 2 3; do ROTS_IT_LAUNCHER=local /tmp/harness-venv/bin/python -m pytest -q -m scenario <node ids> || exit 1; done'
```

Put the node ids of the scenarios step 9 repeats in place of `<node ids>`, each in double
quotes: the whole script is inside single quotes, so a single quote would end it.

The container runs as the host user, so the run directories it writes stay the host user's.
The command exits 1 at the first failing run. `ulimit -c 0` matters: a core dump of a crashing
process under emulation can hang the container.

### Bite check

1. Break the behaviour with an uncommitted edit to the file in `src/` that implements it.
2. Rebuild `ageland` and `ageland_harness` (`cmake --build build --target ageland
   ageland_harness`; on a Mac through `docker compose run --rm -T rots bash -lc '...'` under a
   lock taken by hand).
3. Run your scenarios (skill, step 9) once, by node id. The pin must fail on its own
   assertion; a `SessionTimeout`, a setup error or a crash means the break or the pin needs
   another look.
4. Undo the edit with `git restore <files>`, rebuild, and run them again: they must pass.
5. Check `git status` shows no change in `src/` before you commit anything. Never commit the
   break.

### Environment variables

`validate_environment()` in `conftest.py` checks `ROTS_IT_LAUNCHER`, `ROTS_IT_SEED`,
`ROTS_IT_LOCK_WAIT`, `ROTS_IT_BINARY`, `ROTS_IT_HOUR_TIMEOUT` and `ROTS_IT_SHARD` before any test; a bad value is
a usage error naming the variable. `ROTS_IT_KEEP` (only `1` counts) and
`ROTS_IT_DOCKER_LOCK_DIR` are not checked.

| Variable | Default | Meaning |
| --- | --- | --- |
| `ROTS_IT_LAUNCHER` | `local` on Linux, `docker` elsewhere | `local` or `docker` |
| `ROTS_IT_BINARY` | `bin/ageland` | the server for tests without a schedule, relative to the repository root |
| `ROTS_IT_SEED` | `20261011` | the random seed for rotstool and every server (`--random-seed`) |
| `ROTS_IT_KEEP` | unset | `1` keeps every run directory and template lib |
| `ROTS_IT_LOCK_WAIT` | `600` | seconds the Docker launcher waits for the lock |
| `ROTS_IT_DOCKER_LOCK_DIR` | `/tmp/rots-docker-lock` | the shared lock directory |
| `ROTS_IT_HOUR_TIMEOUT` | `10.0` | seconds `advance_hours` allows per game hour; the wait is `hours * timeout + 10` |
| `ROTS_IT_SHARD` | unset | `k/N`: keeps the k-th of N shares of the selected tests, balanced by the times in `tests/integration/shard_durations.json` (`rots_harness/sharding.py`); CI shards only a run of every group, `INTEGRATION_SHARDS` ways. An empty share passes, with or without `JOBS` workers |
| `ROTS_IT_ALLOW_STALE` | unset | `1` runs a program older than its sources; not checked by `validate_environment()` |
| `ROTS_IT_CONFIRM_MINUTES` | `10` | the estimate from which `make integration` without a terminal needs `CONFIRM=yes`; checked by `rots_harness/run_plan.py`, not by `validate_environment()` |

The local launcher passes the server a clean environment, forwarding only `ASAN_OPTIONS`,
`LSAN_OPTIONS`, `UBSAN_OPTIONS` and `ASAN_SYMBOLIZER_PATH`.

### Run directories

Each test gets `build/integration/runs/<12 hex>/`:

| File | Contents |
| --- | --- |
| `test-id.txt` | the test's node id |
| `lib/` | the test's own copy of the template lib |
| `game.log` | the server's stderr, and its stdout too when the test has no schedule |
| `stdout.log` | a schedule server's ordinary stdout lines; `harness: ` replies are not written here |
| `<lowercase name>.txt` | one transcript per character, appended on every read, colour codes removed |

A passing test's directory is deleted. It is kept when `ROTS_IT_KEEP=1`, when any test in the
session has failed, when the server's checks fail, when the run is interrupted, or when setup
failed after the lib was copied. Kept directories are listed at the end of the run under "kept
run directories". Template libs live in `build/integration/libs/<key>/` and are deleted at the
end of the session unless `ROTS_IT_KEEP=1`.

Everything under `build/` is git-ignored build output (`.gitignore`), so kept directories never
reach a commit. Remove only the directories your own run kept, by the names the run listed, or
leave them; other kept directories are someone else's evidence.

### What fails a test besides its assertions

`CrashMonitor` (`rots_harness/crashmonitor.py`) reads `game.log` and the exit status: a
sanitizer marker, `Error: signal`, any `SYSERR` line (none is allowed today), the server exiting
during the test, and after the stop a non-zero status or a status 0 without the
`Received SIGHUP, SIGINT, or SIGTERM` line. A session that does not answer `quit` also fails the
test.

A program that is missing, or older than the newest `.cpp` or `.h` file it is built from, stops
each test that runs it at setup with the build command (`ServerLauncher.require_program` and
`newest_source` in `rots_harness/launcher.py`). `src/tests/` never counts, `src/harness/` counts
only for `ageland_harness`, `src/tools/rotstool/` only for `rotstool` and `src/main.cpp` only
for `ageland`. `ROTS_IT_ALLOW_STALE=1` skips the age check.

## Characters

### Declaring them

`Character` (`rots_harness/characters.py`) is a frozen dataclass; `to_spec()` turns it into
rotstool's fixture spec, and `bin/rotstool fixtures write` validates it with the server's own
rules (`src/tools/rotstool/fixture_spec_parser.cpp`). A refusal fails the test with rotstool's
message.

| Field | Accepts |
| --- | --- |
| `name` | 3 to 12 ASCII letters, unique in the test; stored with a capital first letter |
| `race` | `human`, `dwarf`, `wood elf`, `hobbit`, `uruk-hai`, `orc`, `uruk-lhuth`, `beorning`, `olog-hai`, `haradrim` |
| `sex` | `male`, `female` |
| `level` | 1 to 100, reached through the game's own levelling, so abilities, hit points, mana and moves are those of a real character of that level |
| `class_name` or `points` | exactly one. `class_name`: `mystic`, `ranger`, `warrior`, `mage`, `conjurer`, `wizard`, `healer`, `swashbuckler`, `barbarian`, `adventurer`. `points`: `ProfessionPoints(mage, mystic, ranger, warrior)`, each 0 to 150 and 150 in total at most |
| `load_room` | a room vnum; not checked against the world. A character above level 90 starts in Immortal Start (1101) whatever it says: `advance_level` (`profs.cpp`) makes an immortal's race god, and `calc_load_room` (`objsave.cpp`) sends race god to 1101. An immortal needed elsewhere moves there with `goto` |
| `skills` | `(("skill_key", practices), ...)`: the key is the skill name in lower case with other characters as `_` (`magic_missile`); practices 1 to 255 are practice sessions, not a percentage |
| `hit`, `mana`, `move` | current points, 1 to the levelled maximum; above it the whole write fails |
| `stored` | `StoredValues(...)`: values the character is stored with, named and limited like the `wizset` fields that set them in game (table below) |

`StoredValues` fields, each optional; rotstool refuses a value outside its range:

| Field | Range | What it sets |
| --- | --- | --- |
| `gold` | 0 to 100000000 | coins carried, in copper |
| `spirit` | any | spirit points |
| `alignment` | -1000 to 1000 | alignment |
| `specialization` | a name from `specialize_name` (`consts.cpp`), such as `"fire"` or `"wild fighting"` | the specialization |
| `hunger`, `thirst`, `drunk` | 0 to 24, or -1 for off | the conditions |
| `str`, `int`, `wil`, `dex`, `con`, `lea` | 0 to 40 | the ability's constant, base and current value alike, as `wizset` then `restore` leave them |
| `maxstamina` | 1 to 5000 | the constant behind maximum mana; current mana keeps its levelled value unless `mana` sets it |

A stored value is in the character file before boot, so the first login reads it the way a
player's own file is read: a stored `lea` already gives the login recount of practice sessions.
Practice sessions themselves are not stored values, because that recount replaces them.

Shared definitions: `IMPLEMENTOR` (`Testimm`, human male warrior, level 100, starts in 1101) and
`custom_mage()` (`Testmage`, human female, level 30, points 70/40/20/20, starts in 1131). A test
that needs a variant declares one with `dataclasses.replace`, such as
`dataclasses.replace(MAGE, stored=StoredValues(gold=500))`. A
test's characters all share one account, in marker order. The game allows one character online
per account unless the account owns one above level 91, so a test with two characters online
declares `IMPLEMENTOR`; without it the second login fails with `You are already connected as`.

### What changes in game, and the command that does it

Set what a test starts with in the character (`stored`, a mortal's `load_room`) or the world
(`world_resets`, "Markers and groups") before boot. The implementor sets the rest, and anything
that changes during the test, after logging in. `wizset` (`do_wizset`, `act_wiz.cpp`) matches its
field by prefix, takes the first match, clamps the value, and replies `<Name>'s <field> set to
<value>.` with the clamped value: assert that reply.

| Need | Command (as the implementor) | Notes |
| --- | --- | --- |
| Hunger, thirst | `wizset player <name> hunger 10` | 0 to 24, or `off`. At the start: `stored` |
| Gold | `wizset player <name> gold 100` | At the start: `stored` |
| Abilities | `wizset player <name> str 18` (`int`, `will`, `dex`, `con`, `lea`), then `restore <name>` | 0 to 40. Sets the constant, and the base follows; the current value, which formulas read, does not change until `restore`. See "Abilities" below. At the start: `stored` |
| Practice sessions | `wizset player <name> practices 20` | Login recounts them from level, learning and the sessions spent, so they cannot be stored |
| The `saving_throw` field | `wizset player <name> saving_throw 5` | -100 to 100. Sets `specials2.saving_throw` (`GET_SAVE`, `utils.h`), the value gear and affects change. `saves_spell` (`spell_pa.cpp`) and spell damage reduction (`get_victim_saving_throw`, `mage.cpp`) read it, and MSDP reports it; `new_saves_spell` and `get_character_saving_throw` (`spell_pa.cpp`) do not, using the mage level and current intelligence instead. Check which save function the behaviour calls before relying on it |
| Hit points | `wizset player <name> hit 50`; `maxhit` sets the constant behind the maximum | `restore <name>` refills hit points, mana and moves to the maximum, so set them after any `restore` |
| Items | `load obj <vnum>` (into the implementor's inventory), then `give <keyword> <name>` in the same room | `<name>` is matched as below, "Names, sides and lookups" |
| Mobs | `load mob <vnum>` (into the implementor's room) | At the start: `world_resets` |
| Position in the world | the character's `load_room`; `teleport <name> <vnum>`, `transfer <name>`, `goto <vnum>` | `load_room` works for every race; `teleport` and `transfer` match `<name>` as below |
| Profession levels | none | `wizset ... level` sets only the overall level and `wizset ... prof` does nothing; choose `level` and `class_name` or `points` (see "Profession levels" below) |
| Affects, flags | cast the spell, or wear an object that sets it | |

### Reading a character in game

The implementor's `stat player <name>` (`do_stat` in `act_info.cpp`, then `do_wizstat` and
`do_stat_character` in `act_wiz.cpp`) prints, among other lines:

```text
Class levels: Mag:6 Cle:8 Ran:10 War:20,  mini-level: ...
Spec:(0) ..., Lev: [20], XP: [...], Align: [...]
Str:[cur/base/const] Int:[...] Wil:[...] Dex:[...] Con: [...] Lea:[...]
HP :[cur/max+gain(bonus)]  Stamina :[...]  Move :[...] ...
Encumbrance n, Leg_encu n, Perception n, Willpower n,
 OB: ..., parry: ..., dodge: ..., c_parry n,  Saving throws n,
Hunger: n, Thirst: n, Drunk: n, Att.Level: n
AFF: <affect bits>
RES: ... VUL: ...
SPL: (  4hr) <affect name> ...
```

`Cle` is the mystic level. `Saving throws n` is the same `specials2.saving_throw` field
`wizset ... saving_throw` sets, not the result of every save calculation (see the wizset table).
The `SPL:` lines (one per affect) come last, after `VUL:`. Several
lines end in `]`, which `command()` takes for a prompt, so read the reply with
`send_line(f"stat player {name}")`, then `expect(["VUL:"])` and `drain(1.0)`. Parse with, for
example, `re.search(r"Saving throws (-?\d+),", text)`,
`re.search(r"Class levels: Mag:(\d+) Cle:(\d+) Ran:(\d+) War:(\d+)", text)`, and for an
ability's current, base and constant `re.search(r"Int:\s*\[(-?\d+)/(-?\d+)/(-?\d+)\]", text)`
(the same with `Str`, `Wil`, `Dex`, `Con`, `Lea`; `\s*` covers the space the format string puts
after `Con:`).

`stat player` finds the character by exact name whichever side it is on, but only if the
implementor can see it: for a character in a dark room, send `set holylight on` first (reply
`Holylight mode is now on.`). `stat` with no argument shows the speaker's own abilities, and
`stat mob <keyword>` shows a mob.

### Abilities

Each ability in `stat` is `Int:[current/base/constant]` (`do_stat_character`, `act_wiz.cpp`):

| Number | Field | What sets it |
| --- | --- | --- |
| current | `tmpabilities` (`GET_INT`, `utils.h`) | follows base: each game hour it rises by `number(1, tmp)`, never past base, with `tmp` 1 awake and 2 asleep (`point_update`, `limits.cpp`), or it jumps to base on `restore` |
| base | `abilities` (`GET_INT_BASE`) | the constant plus gear and affect modifiers, rebuilt by `affect_total` (`handler.cpp`) through `recalc_abilities` (`profs.cpp`) |
| constant | `constabilities` | rolled when the character is made, saved as its permanent stats |

- Formulas read the **current** value (`GET_INT`, `GET_WILL`, ...): saving throws in
  `spell_pa.cpp`, spell power, mana regeneration and maximum mana among them.
- `wizset player <name> int 40` sets the constant (0 to 40) and calls `affect_total`: the base
  follows, the current value does not. `restore <name>` then sets current to base at once.
  `restore` also refills hit points, mana and moves, so set those afterwards.
- The scenario's order is therefore `wizset`, `restore`, then `stat player` to read the
  current value the formula will use.
- An ability the character's `stored` values leave unset is rolled from the seeded random
  stream when rotstool makes the character, in the order the `characters` marker lists them
  (`profs.cpp`), so a different `ROTS_IT_SEED`, or a reordered marker, gives different rolled
  abilities. Read them with `stat`, store them, or set them as above. A stored ability is the
  constant, base and current value alike.

### Profession levels

A character's profession levels come from its class (or `points`) and its level alone, not
from the seed (`creation_points.cpp`; `advance_mini_level` in `limits.cpp` calls
`advance_level_prof`, `profs.cpp`; each profession is capped at `LEVEL_MAX`, 30). The
professions besides the main one are not zero:

| Character | Mag | Mys (`Cle`) | Ran | War |
| --- | --- | --- | --- | --- |
| warrior, level 20 | 6 | 8 | 10 | 20 |
| mage, level 20 | 20 | 10 | 8 | 6 |

Read them from `stat player <name>`'s `Class levels:` line rather than assuming them.

### Margin checks

A margin check asserts, from values read with `stat player`, that no roll can change a case's
outcome. It is only as good as each term it uses, so **every case, the control included,
carries a comment beside the call that evaluates the code's own expression for that case, term
by term, as the code writes it (operators and integer division included), ending in the value
passed.** Each term names where its value comes from: a `stat` line, or the function that
computes it for this case. A term is 0 only when the code computes 0 for this case, and the
comment says where. Writing the expression out is what catches a term assumed to be neutral.

For an invented check, where the code reads
`chance = 2 * GET_LEVEL(ch) + GET_DEX(ch) / 3 - armour_penalty(ch) * 5;` and the action fails
when `number(1, 100) > chance`:

```python
# Control case, Testcontrol (level 45, wearing the leather cap):
#   chance = 2 * GET_LEVEL(ch) + GET_DEX(ch) / 3 - armour_penalty(ch) * 5
#          = 2 * 45            + 18 / 3          - 1 * 5
#            (Lev: [45])         (Dex:[18/18/18])  (armour_penalty: 1 for the worn cap)
#          = 90 + 6 - 5 = 91, and number(1, 100) can exceed 91: NOT roll-proof.
# read_level_and_current_dexterity is the scenario's own helper over `stat player`.
level, dexterity = read_level_and_current_dexterity(implementor, "testcontrol")
assert (level, dexterity) == (45, 18)
assert 2 * level + dexterity // 3 - 1 * 5 == 91
```

Here the comment shows the case is not roll-proof, so the scenario must change an input (for
example a higher level) until the evaluated value reaches 100, and repeat the comment for the
changed case. Assert the values read from `stat` that the comment relies on, so a changed
input fails the test rather than silently moving the margin.

### Names, sides and lookups

Most commands find a character by `keyword_matches_char` (`handler.cpp`), through
`get_char_room_vis` (the speaker's room) or `get_char_vis` (the speaker's room first, then the
whole world). Command targets parsed by the interpreter (`TAR_CHAR_ROOM`, `TAR_CHAR_WORLD` in
`interpre.cpp`) go the same way, so the rule holds for every player's targeted commands, not
only the implementor's:

- When the speaker and the target are players on opposite sides (`other_side`), the target is
  matched by its race keyword (`pc_race_keywords`, `consts.cpp`), never by its name.
- `other_side` puts the free peoples (human, dwarf, wood elf, hobbit, beorning) against
  uruk-hai, orc and olog-hai, and puts uruk-lhuth and haradrim against everyone else. A mob
  (unless charmed) or a `RACE_GOD` character is never on the other side.
- The race keywords of the offered races: `human`, `dwarf`, `elf` (wood elf), `hobbit`, `bear`
  (beorning), `uruk` (uruk-hai and uruk-lhuth), `orc`, `olog` (olog-hai), `haradrim`.
- A mob is always matched by its own keywords, so `orc` also matches the test world's orc mobs.
- `N.<keyword>` picks the Nth match (`get_number`). `get_char_vis` first looks for it in the
  speaker's room, then counts again from 1 over the whole world.

`IMPLEMENTOR` is human, so for an uruk-hai, orc, olog-hai, uruk-lhuth or haradrim character
`goto <name>`, `teleport <name>`, `transfer <name>`, `give <item> <name>`, `stat <name>` and
`wizset <name>` all fail with the name, and `orc` may pick a mob. Instead:

- put the character where the scenario needs it through its `load_room`;
- hand it an object by `drop`ping the object in its room and having it `get` the object;
- use the exact-name forms where they exist: `stat player <name>` and `wizset player <name>`
  (`get_player_vis`), and `restore <name>` (`get_char`, which matches names and ignores sides);
- otherwise use `N.<race keyword>` in a room where you know what else answers to it, and assert
  the reply names the right character;
- or declare a same-side character as the other actor.

All of these lookups also need the speaker to see the target (`CAN_SEE`); `restore` does not.

What an observer reads for a character is `PERS` (`utility.cpp`): the name, a star name such as
`*an Orc*` across sides, or `someone` when the observer cannot see it. `act()` (`comm.cpp`) with
`hide_invisible` set sends nothing to an observer who cannot see the actor.

## Session API

`server` (the fixture in `conftest.py`) is a `HarnessServer`:

| Member | Use |
| --- | --- |
| `login(name)` | logs the character in and returns its `GameSession`; it is quit after the test |
| `advance_hours(n)` | runs `n` game hours on a schedule server and returns when the server acknowledges them; fails the test without `@pytest.mark.schedule` |
| `run_dir`, `lib_dir` | the run directory and the test's lib |

`GameSession` (`rots_harness/session.py`):

| Method | Behaviour |
| --- | --- |
| `command(text, timeout=8.0)` | drains unread text for 0.1 s and discards it, sends `text`, returns a `Transcript` of everything up to the first read whose text ends in `>` or `]` |
| `expect(markers, timeout=8.0)` | returns all unread text once any marker appears in it |
| `drain(timeout=0.5)` | reads for `timeout` seconds and returns the unread text |
| `send_line(line)` | sends a line without waiting |
| `everything` | all text received so far |

`Transcript` has `.text`, `.contains(marker)` and `.room_name()`, the first non-empty line up to
two spaces. A timeout raises `SessionTimeout` (an `AssertionError`) quoting the last 1500
characters. A refused login raises `LoginRefused` at once with the game's words.

What helpers return in odd states (`do_look`, `act_info.cpp`): a character who cannot see gets
`It is pitch black...` instead of the room, so `room_name()` returns `It is pitch black...`;
a sleeping one gets `You can't see anything, you're sleeping!`; a blind one gets
`You can't see a damned thing, you're blinded!`.

One example, abridged from `scenarios/test_poison.py`:

```python
pytestmark = [pytest.mark.scenario, pytest.mark.poison]

# What the mortal reads on each poison tick.
POISON_TICK_MARKER = "You writhe in pain as poison courses through your veins."
# A level-20 human warrior in the arena: a mortal, since immortals are not poisoned by food.
MORTAL = Character(name="Testeater", race="human", sex="male", level=20,
                   load_room=world.ROOM_ARENA_CENTRE, class_name="warrior")


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, MORTAL)
def test_tainted_bread_poisons_a_mortal_once_per_game_hour_then_wears_off(server) -> None:
    implementor = server.login(IMPLEMENTOR.name)
    mortal = server.login(MORTAL.name)
    set_hunger = implementor.command("wizset testeater hunger 10")
    assert set_hunger.contains("hunger set to 10."), set_hunger.text
    implementor.command("goto testeater")
    assert implementor.command("load obj 1139").contains("You create")
    assert implementor.command("give bread testeater").contains("You give")
    assert mortal.command("eat bread").contains("Oops, that tasted rather strange!")
    server.advance_hours(1)
    first_hour = mortal.expect([POISON_TICK_MARKER]) + mortal.drain(1.0)
    assert first_hour.count(POISON_TICK_MARKER) == 1, first_hour
```

### Timing rules

- Commands from different connections are not ordered by when they were sent: the game loop
  (`comm.cpp`) takes one command per connection per pass, in connection order. Wait for one
  character's reply before relying on its effect from another connection.
- Text can arrive between commands: affect messages each hour, and in any room without
  `INDOORS` or `DARK` (most test-world rooms) the sun and moon lines `send_to_outdoor` sends
  (`weather.cpp`): `The day has now begun.`, `Night falls over the land.`,
  `The <phase> moon shows in the sky.`, `The <phase> moon goes off the sky.`. In an outdoor room
  of any sector but inside (sector 0, which every test-world room has today), the weather
  lines `weather_change` sends each hour arrive too (`send_to_sector`, `comm.cpp`). Read it
  all with `expect`/`drain` before the next `command()`.
- A command with a delay (`WAIT_STATE_FULL` or `WAIT_STATE_BRIEF`, `utils.h`; casting and
  `search` use them) sets `AFF_WAITING`: the game takes no further command from that
  character until the delay ends, and the outcome can arrive after the reply `command()`
  returns. `expect` the outcome.
- When the delay ends, `command_interpreter` (`interpre.cpp`) checks the command's target again
  (`target_check`, `target_check_one`), so a target test such as `CAN_SEE` can gate a
  behaviour twice: once when the command is typed and once when it completes. A bite check or
  a fix must open both.
- After `advance_hours` returns, the server has run the passes; `drain(1.0)` (or `expect` on
  the hour's marker, then `drain`) collects text already sent.
- Regeneration (`fast_update`) runs inside every advanced hour, and the hourly update runs
  before it in the same pass: assert hit points once, after the first hour, or as a range.

## Markers and groups

`tests/integration/pytest.ini` runs under `--strict-markers` and registers the markers that are
not groups:

| Marker | Effect |
| --- | --- |
| `scenario` | boots its own server through the `server` fixture; `make integration` always selects it |
| `characters(*characters)` | the test's characters; a test using `server` without it fails |
| `schedule(name)` | runs `bin/ageland_harness --schedule <name>`; game time advances only through `advance_hours` |
| `world_resets(*resets)` | `MobReset(mob_vnum, room_vnum)` and `ObjectReset(object_vnum, room_vnum)` (`rots_harness/world_resets.py`): the test's lib adds one reset row per value to the zone that owns the room, so boot places each mob in its room and each object on its floor before the first login. A vnum the world lacks fails the lib build, naming it. A server on the wall clock resets a zone again when its lifespan ends, which brings back a placed mob the test has purged or killed, so a test that relies on a mob being gone loads it with `load mob` instead |
| a group, one per `tests/integration/groups/<name>.txt` | no behaviour of its own |
| a subgroup `<name>_<sub>`, one per `tests/integration/groups/<name>/<sub>.txt` | no behaviour of its own; a test marking it also marks `<name>` |

A new group is a new file `tests/integration/groups/<name>.txt` holding its description on one
line, plus `pytest.mark.<name>` in the scenario's `pytestmark`. `conftest.py` registers a marker
per file (`register_group_markers`, reading through `rots_harness/groups.py`), and CI's group
choice reads the same folder. A name must be a marker name (letters, digits and underscores). A
unit test (`unit/test_groups.py`) fails when a scenario marks a group with no file or a file names
a group no scenario marks. Neither `pytest.ini` nor the Makefile changes.

A subgroup is a file `tests/integration/groups/<name>/<sub>.txt` beside `<name>.txt`, registering
`<name>_<sub>`; subgroups go one level deep. Each test of a group with subgroups marks the group
and exactly one subgroup, and each test marking a subgroup marks its group:
`SubgroupMarkingCheck` in `conftest.py` stops collection otherwise, before `-m` or the shard
deselects anything (`groups.subgroup_marking_problems`). CI and `make integration-changed` run
only the subgroups a changed scenario file marks, plus the groups of the scenario files that
import it; a changed `<name>.txt` or `@integration-test-<name>` still runs the whole group. The
run plan shows a group's subgroups under a header.

## Time control

Without a schedule the server runs on the wall clock: mob activity, combat, zone resets and the
hourly update all happen in real time.

With `@pytest.mark.schedule("affect-hours")`, the only schedule today, the harness binary
starts and writes `harness: ready schedule=affect-hours hours=0` to stdout; the fixture waits
for it. World updates then run only when granted. `advance_hours(n)` writes `advance-hours n`
(1 to 1000) to the server's stdin; the reply is `harness: done advance-hours n hours=<total>` or
`harness: error <reason>`, which raises `TimeControlError`.

| An advanced hour runs | It never runs |
| --- | --- |
| 19 fast-update passes, then one pass with the hourly update (`weather_and_time`, `point_update`, `stat_update`) and the fast update (`fast_update`, `affect_update`) | zone resets, mob activity, combat (`perform_violence`), skill timers |

So a scenario needing combat, wandering mobs or resets has no schedule today: it uses a plain
server, or a new schedule (extending-the-harness.md).

### Randomness

Every draw goes through one `std::rand()` stream (`number()` and `dice()`, `utility.cpp`),
seeded from `--random-seed` (`ROTS_IT_SEED`). On a plain server mob activity and combat draw on
the wall clock, so the seed does not make a run repeat. Under `affect-hours`, between requests
only the scenario's commands draw, and each hour draws in the same order (`weather_change`
among others), so the same commands give the same sequence.

## The test world

`LibTemplateBuilder.build` (`rots_harness/libbuilder.py`) copies `tests/integration/world/<cat>`
for each of `wld mob obj zon shp scr mdl` into the lib; the real `lib/world` is not used. Adding a
category means editing `WORLD_CATEGORIES` (`rots_harness/world_files.py`). The world's `notes/`
folder is documentation; the harness never copies it.

This section holds the rules for using and changing the world, not a list of its fixtures. The
fixtures are found as below, so a branch that adds an area edits no shared list here.

### Finding a fixture

- **The catalogue** prints every zone, room, mob, object and script from the world files, with
  the flag numbers decoded: `make world-catalogue PYTHON=.venv/bin/python` from the repository
  root, or `python -m rots_harness.world_catalogue` from `tests/integration`. `ZONE=<n>`
  (`--zone`) keeps one zone and the fixtures among its own vnums; `CATEGORY=` (`--category`)
  `zones`, `rooms`, `mobs`, `objects` or `scripts` keeps one table. It shows each zone's map
  square, vnums and resets; each room's flags, sector and exits (with a door's flags and
  keyword); each mob's keywords, level, act flags, special program, script and options; each
  object's type, extra and wear flags and affects; each script's triggers.
- **The notes**, `world/notes/zone-<n>.md`, one per zone: what the zone is for, and what each
  fixture does that its numbers do not show (its behaviour, its quirks, why a value is what it
  is, which scenarios rely on it). Fixtures outside every zone's own vnums (`mob/14.mob`,
  `mob/15.mob`, `mob/guildmasters.mob`, `obj/14.obj`) are in `world/notes/shared.md`.
- **The world files** for anything else, decoded as "File formats" below.
- **Room constants**: `rots_harness/world.py` holds the rooms that two or more scenario files, or
  the harness itself, name. A room only one scenario file names is a constant in that file.

### Rules and gotchas

- **Vnums ascend across each category**, within each file and from file to file in index order,
  with no repeats: `real_room`, `real_mobile` and `real_object` binary-search, so an entry out of
  order is silently never found.
- **Each index lists exactly its folder's world files, by first vnum** (so zone files by zone
  number), then `$`; a file not in its `index` is not loaded. The index files stay committed
  because something outside the harness boots on this world: `scripts/nightly_deploy.py` copies
  it into `lib/world` for the account smoke flow (`tools/account_smoke.py`).
- **A zone owns its own vnums**: zone `n` owns `n * 100` to `n * 100 + 99`, up to its top line.
  The shaping editors open the file named after `vnum / 100` (`<n>.wld`, `<n>.obj`, ...) and
  check the builder's permission on zone `vnum / 100` (`load_object`, `shapeobj.cpp`;
  `get_permission`, `shapemob.cpp`), so a file `<n>.*` holds only zone `n`'s vnums. `load_rooms`
  (`db.cpp`) gives each room to the first zone in the index whose top is not below it; a room
  above every zone's top stops the boot (`Room <n> is outside of any zone.`) without a `SYSERR`.
- **Each zone has a map square no other zone uses** (its `<symbol> <x> <y> <level>` line):
  relocate and the world map find a zone by its square.
- **Each zone has its notes file**, `world/notes/zone-<n>.md`.
- **The world check** (`unit/test_world_files.py`, run by `make integration-unit` and first in
  CI's "Choose the integration groups" job) fails on each of these: an index out of step with
  its folder (it prints the listing it expects), vnums out of order or repeated, a file without
  its closing `#99999`, a shared map square, a vnum outside its zone or its file's block, a zone
  without notes, and a merge conflict marker in the world or the group files. Run it after every
  world change, then boot.
- **Boot after every change**: the check catches layout and order, but only the server's loaders
  catch a field they misread. The boot check is `test_boot.py`, and every other scenario booting,
  failing on any `SYSERR`, crash or missing login prompt.
- **Rooms and light**: every room so far is `SECT_INSIDE` without `INDOORS`, so `OUTSIDE()` is
  true and no room is ever sunlit (SKILL.md, "Read the behaviour in the code"). A `DARK` room is
  dark unless something in it gives light; the test world has no light object.
- **One speaking hear-script mob per room**: `do_say` (`act_comm.cpp`) runs every other mob's
  `ON_HEAR_SAY` script inside the say, so two mobs that speak when they hear speech answer each
  other until the server's stack overflows.
- **A mob's language decides what players read**: language 0 reaches every listener exactly;
  language 1, the animal tongue, is garbled for players by `say_to_char` (`act_comm.cpp`). The
  first number of a mob's last numeric line sets it (`load_mobiles`, `db.cpp`); give 0 to a mob
  whose lines a scenario reads.
- **Keywords are shared**: several mobs answer to `orc`, and several to `harness` (the zone 11
  notes list them). Give a new fixture keywords no other fixture uses, or a lookup can pick the
  wrong one ("Names, sides and lookups" above).
- **Specials are bound by vnum**: `spec_ass.cpp` binds specials to particular mob and object
  vnums (the receptionist, the guild masters, boards). A mob gets its special only with the
  `MOB_SPEC` act flag as well; a mob's program number is a special's row only with that flag.
  Avoid object vnums `spec_ass.cpp` passes to `ASSIGNOBJ` unless the scenario wants that special.
- **The inn's random social**: every command typed in the inn (room 1601) other than `rent` and
  `offer` draws `number(0, 30)`, and on 0 the innkeeper performs a random social, so text can
  arrive there unasked. Teleport the mortal in just before `rent` or `offer`, and rent rather
  than `quit`, which drops everything a mortal carries (`world/notes/zone-16.md`).
- **No `yell` in zone 16**: a yell anywhere in zone 16 outside the inn logs
  `SYSERR: Fubar'd receptionist.`, which fails the test (`do_gen_com`, `act_comm.cpp`, calls every
  room special in the zone). Keep rooms where a scenario yells out of zone 16.
- **Expected boot reports**: the boot check of scripts writes `SCRIPT ERROR: script #1720, ...`
  lines, and one each for the valet's and dresser's scripts, to every test's `game.log`
  (`world/notes/zone-17.md`). They are not `SYSERR` lines and fail nothing.
- **Backup folders**: the shaping editors' `/save` copies the file it rewrites into a backup
  folder first and fails without it (`SHAPE_*_BACKDIR`, `protos.h`; the object editor's `/save`
  and `/done` in `replace_object`, `shapeobj.cpp`, for example). The world provides all five,
  each kept in git by an empty `.gitkeep`: `wld/oldroms`, `mob/oldmobs`, `obj/oldobjs`,
  `zon/oldzons` and `scr/oldscrs` (`obj/oldobjs` also holds a README). They are not world files
  and no index lists them.
- **Empty categories**: there are no `.shp` shops (`shp/index` is `$` alone), and `mdl/11.mdl` is
  a stub.
- **`*.obj` is ignored by git** (`.gitignore`): a new `obj/<n>.obj` file needs `git add -f`.

### File formats

Each category directory has an `index` listing its files one per line, ending with a line `$`
(`index_boot`, `db.cpp`).

A room (`load_rooms`, `db.cpp`):

```text
#1131
Arena Centre~
The centre of the harness arena. ...
~
11 0 0 0
D0
~
~
0 0 <to room> 5
S
```

- The header line is `<zone> <flags> <sector> <level>`. Flags are bits from `structs.h`,
  among them `DARK` 1, `DEATH` 2, `NO_MOB` 4, `INDOORS` 8, `NORIDE` 16, `SHADOWY` 64,
  `NO_MAGIC` 128, `TUNNEL` 256, `PRIVATE` 512, `GODROOM` 1024, `PEACEROOM` 32768, `NO_TELEPORT` 65536. Sectors:
  0 inside, 1 city, 2 field, 3 forest, 4 hills, 5 mountain, 6 water (swim), 7 water (no swim),
  8 underwater, 9 road, 10 crack, 11 dense forest, 12 swamp.
- An exit is `D<direction>` (0 north, 1 east, 2 south, 3 west, 4 up, 5 down), its description
  `~`, its keyword `~`, then `<exit flags> <key> <to room> <width>` (`setup_dir`). Exit flags
  include `EX_ISDOOR` 1, `EX_CLOSED` 2 and `EX_LOCKED` 4.
- `E` starts an extra description: its keywords `~`, then its text `~`. `S` ends the room; the
  file ends with `#99999` and `$~`.

A zone (`load_zones`, `zone.cpp`): `#<n>`, name `~`, description `~`, map `~`, an owner list
ending in `0`, `<symbol> <x> <y> <level>`, the top room, the lifespan, the reset mode, then reset
rows ending with `S`. An `M` row is `M <if> <mob> <room> <max in world> <percent> <difficulty>
<max existing> <trophy> <comment>`; an `O` row is `O <if> <object> <room> ...`.

Mobs (`load_mobiles`) and objects (`load_objects`) in `db.cpp`: copy an existing entry and read
the loader before changing a field you do not know. Every room, mob, object, script and mudlle
file ends with a `#99999` record, which `count_hash_records` (`db.cpp`) counts on.

### Adding to the world

- **A new scenario area gets its own zone**: the next zone number above the highest (19 today;
  14 and 15 are taken, since `mob/14.mob`, `mob/15.mob` and `obj/14.obj` already hold fixtures
  numbered 1400 to 1599; when another open branch already claims the number, take the next). It
  has its own files (`wld/<n>.wld` and `zon/<n>.zon`, plus `mob/<n>.mob`,
  `obj/<n>.obj` and `scr/<n>.scr` as needed), its own vnums from `n * 100`, a top line of at most
  `n * 100 + 99` and above every earlier zone's, a map square no other zone uses, and its own notes file
  `world/notes/zone-<n>.md`. List each new file in its category's `index` at its first-vnum place.
  With its own group file and its constants in its scenario file, the area then edits no file
  another area's branch edits, apart from the checked indexes.
- **Append to zone 11's files** (`wld/11.wld` before `#99999`, `mob/11.mob`, `obj/11.obj`) only
  for a fixture zone 11's existing scenarios need, keeping its vnums between 1101 and 1199.
- A mob: a special needs the `MOB_SPEC` act flag as well as the program number. Place it with a
  zone reset row, a test's `world_resets` marker or `load mob`.
- An object the object editor must load goes in its zone's file.
- Record each new fixture's purpose and quirks in its zone's notes file, and name a room in
  `rots_harness/world.py` only when a second scenario file needs it.

## Scenario territory and harness territory

| Scenario territory | Harness territory (extending-the-harness.md) |
| --- | --- |
| `tests/integration/scenarios/test_<group>.py` | `tests/integration/conftest.py` |
| `tests/integration/world/`, its notes files included | `tests/integration/rots_harness/` other than `world.py` |
| room constants shared by two scenario files, in `rots_harness/world.py` | `tests/integration/unit/` |
| a group or subgroup file in `tests/integration/groups/` | `src/harness/`, `src/tools/rotstool/` |

A helper or constant used by one scenario file stays in that file, as `hit_points` does in
`test_poison.py` and `ROOM_BANK_HALL` in `test_bank.py`.
