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
| One group | `make integration PYTHON=.venv/bin/python GROUP=poison` |
| Several groups | `make integration PYTHON=.venv/bin/python GROUP="boot or poison"` |
| One test, by part of its name | `.venv/bin/python -m pytest tests/integration/scenarios -q -m scenario -k tainted_bread` |
| One test, by node id | `.venv/bin/python -m pytest -q "tests/integration/scenarios/test_boot.py::test_an_implementor_logs_in_to_immortal_start"` |

`make integration` runs `$(PYTHON) -m pytest tests/integration/scenarios -q -m "scenario and
(<GROUP>)"`. Run directly, pytest finds the same `tests/integration/pytest.ini` and rootdir and
reads the same `ROTS_IT_*` variables, so the direct commands behave like `make integration`.
`-k` is not checked: a name that matches nothing runs nothing and prints only `N deselected`.
`-m` is checked: an unregistered name in it is a usage error listing the registered names
(`pytest_configure` in `conftest.py`, `rots_harness/groups.py`).

Run one harness session per checkout at a time: sessions in a checkout share the template
cache under `build/integration/libs/`.

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
  for run in 1 2 3; do ROTS_IT_LAUNCHER=local make integration PYTHON=/tmp/harness-venv/bin/python GROUP=<marker> || exit 1; done'
```

The container runs as the host user, so the run directories it writes stay the host user's.
The command exits 1 at the first failing run. `ulimit -c 0` matters: a core dump of a crashing
process under emulation can hang the container.

### Bite check

1. Break the behaviour with an uncommitted edit to the file in `src/` that implements it.
2. Rebuild `ageland` and `ageland_harness` (`cmake --build build --target ageland
   ageland_harness`; on a Mac through `docker compose run --rm -T rots bash -lc '...'` under a
   lock taken by hand).
3. Run the pin's group once. It must fail on the pin's own assertion; a `SessionTimeout`, a
   setup error or a crash means the break or the pin needs another look.
4. Undo the edit with `git restore <files>`, rebuild, and run the group again: it must pass.
5. Check `git status` shows no change in `src/` before you commit anything. Never commit the
   break.

### Environment variables

`validate_environment()` in `conftest.py` checks `ROTS_IT_LAUNCHER`, `ROTS_IT_SEED`,
`ROTS_IT_LOCK_WAIT`, `ROTS_IT_BINARY` and `ROTS_IT_HOUR_TIMEOUT` before any test; a bad value is
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
| `load_room` | a room vnum; not checked against the world |
| `skills` | `(("skill_key", practices), ...)`: the key is the skill name in lower case with other characters as `_` (`magic_missile`); practices 1 to 255 are practice sessions, not a percentage |
| `hit`, `mana`, `move` | current points, 1 to the levelled maximum; above it the whole write fails |

Shared definitions: `IMPLEMENTOR` (`Testimm`, human male warrior, level 100, starts in 1101) and
`custom_mage()` (`Testmage`, human female, level 30, points 70/40/20/20, starts in 1131). A
test's characters all share one account, in marker order. The game allows one character online
per account unless the account owns one above level 91, so a test with two characters online
declares `IMPLEMENTOR`; without it the second login fails with `You are already connected as`.

### What rotstool cannot set, and the in-game command that does

The implementor sets these after logging in. `wizset` (`do_wizset`, `act_wiz.cpp`) matches its
field by prefix, takes the first match, clamps the value, and replies `<Name>'s <field> set to
<value>.` with the clamped value: assert that reply.

| Need | Command (as the implementor) | Notes |
| --- | --- | --- |
| Hunger, thirst | `wizset player <name> hunger 10` | 0 to 24, or `off` |
| Gold | `wizset player <name> gold 100` | |
| Abilities | `wizset player <name> str 18` (`int`, `will`, `dex`, `con`, `lea`), then `restore <name>` | 0 to 40. Sets the constant, and the base follows; the current value, which formulas read, does not change until `restore`. See "Abilities" below |
| The `saving_throw` field | `wizset player <name> saving_throw 5` | -100 to 100. Sets `specials2.saving_throw` (`GET_SAVE`, `utils.h`), the value gear and affects change. `saves_spell` (`spell_pa.cpp`) and spell damage reduction (`get_victim_saving_throw`, `mage.cpp`) read it, and MSDP reports it; `new_saves_spell` and `get_character_saving_throw` (`spell_pa.cpp`) do not, using the mage level and current intelligence instead. Check which save function the behaviour calls before relying on it |
| Hit points | `wizset player <name> hit 50`; `maxhit` sets the constant behind the maximum | `restore <name>` refills hit points, mana and moves to the maximum, so set them after any `restore` |
| Items | `load obj <vnum>` (into the implementor's inventory), then `give <keyword> <name>` in the same room | `<name>` is matched as below, "Names, sides and lookups" |
| Mobs | `load mob <vnum>` (into the implementor's room) | |
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
- rotstool cannot set abilities. They are rolled from the seeded random stream when rotstool
  makes each character, in the order the `characters` marker lists them (`profs.cpp`), so a
  different `ROTS_IT_SEED`, or a reordered marker, gives different rolled abilities. Read them
  with `stat`, or set them as above.

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

`tests/integration/pytest.ini` runs under `--strict-markers`:

| Marker | Effect |
| --- | --- |
| `scenario` | boots its own server through the `server` fixture; `make integration` always selects it |
| `characters(*characters)` | the test's characters; a test using `server` without it fails |
| `schedule(name)` | runs `bin/ageland_harness --schedule <name>`; game time advances only through `advance_hours` |
| every other line under `markers =` | a group, with no behaviour of its own |

A new group is one line `<name>: <description>` under `markers =` (the name must start the
line), plus `pytest.mark.<name>` in the scenario's `pytestmark`. `rots_harness/groups.py` and
the Makefile need no change.

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
for each of `wld mob obj zon shp scr mdl` into the lib; the real `lib/world` is not used. Adding
a category means editing `WORLD_CATEGORIES`.

### Zones

| Zone | File | Name | Map (x, y) | Rooms | Resets |
| --- | --- | --- | --- | --- | --- |
| 11 | `zon/11.zon` | Harness arena | (0, 0) | 1101-1199 | target orc (mob 1130) to 1132; harness snake (mob 1131) to 1134 |
| 12 | `zon/12.zon` | Harness distant zone | (3, 4) | 1201-1299 | none |

### Rooms

Every room in the test world, decoded from its header line (`<zone> <flags> <sector> <level>`;
every room's level is 0). Sector 0 is `SECT_INSIDE`. No room has the `INDOORS` flag, so
`OUTSIDE()` is true in all of them. Re-read the header in `wld/11.wld` or `wld/12.wld` after
anyone changes the world.

| Vnum | Name | Flags | Sector | Exits |
| --- | --- | --- | --- | --- |
| 1101 | Immortal Start (`world.ROOM_IMMORTAL_START`) | 0 (none) | 0 | none |
| 1102 | Immortal Idle | 0 (none) | 0 | none |
| 1110 | Frozen Room | 0 (none) | 0 | none |
| 1129 | Olog-hai Start | 0 (none) | 0 | none |
| 1130 | Arena West (`world.ROOM_ARENA_WEST`) | 0 (none) | 0 | east 1131, down 1136 |
| 1131 | Arena Centre (`world.ROOM_ARENA_CENTRE`) | 0 (none) | 0 | north 1133, east 1132, south 1134, west 1130 |
| 1132 | Arena East | 0 (none) | 0 | west 1131 |
| 1133 | Dark Cell (`world.ROOM_DARK_CELL`) | 1 (`DARK`) | 0 | south 1131 |
| 1134 | Corridor One | 0 (none) | 0 | north 1131, south 1135 |
| 1135 | Corridor Two | 0 (none) | 0 | north 1134 |
| 1136 | Crevice Floor | 0 (none) | 0 | up 1130 |
| 1151 | Retirement Home | 0 (none) | 0 | none |
| 1152 | Lost and Found | 0 (none) | 0 | none; where a character with a bad load room arrives |
| 1160 | Human Start | 0 (none) | 0 | none |
| 1170 | Wood-elf Start | 0 (none) | 0 | none |
| 1184 | Beorning Start | 0 (none) | 0 | none |
| 1190 | Uruk Idle | 0 (none) | 0 | none |
| 1191 | Orc Idle | 0 (none) | 0 | none |
| 1201 | Distant Cell (`world.ROOM_DISTANT_CELL`, zone 12) | 0 (none) | 0 | none |

1133 is the only room with a flag. `IS_DARK` is true there unless something in it gives light,
so a character there sees only with infravision, holylight, or moonvision while the moon is up
(1133 counts as outside, and moonlight follows the uncontrolled game hour).

### Mobs and objects

| Mob | Keywords | What it is |
| --- | --- | --- |
| 1130 | `target orc` | a plain melee target; zone 11 resets one into 1132 |
| 1131 | `snake harness` | its bite poisons; act flags `3` include `MOB_SPEC`; zone 11 resets one into 1134 |
| 1132 | `bystander orc` | involved in nothing, there to be splashed |
| 1133 | `brute orc` | hits very hard |
| 1134 | `pet orc` | carries the `MOB_PET` flag (act flags `2097154`) |
| 1135 | `target orc hardy` | a target that outlives several ticks of damage |

Five of these answer to the keyword `orc`, as do two of the guildmaster copies below (4601 and
32200); this matters for name lookups whenever one is loaded ("Names, sides and lookups"
above). `mob/guildmasters.mob` copies six real guildmasters at their real vnums (1503, 2043,
4601, 10003, 13600, 32200) so `spec_ass.cpp` binds the guild special to them.

| Object | Keyword | What it is |
| --- | --- | --- |
| 1130 | `token` | harness token; its vnum is one `spec_ass.cpp` assigns a board to |
| 1136 | `bag` | leather bag, a container |
| 1137 | `cap` | leather cap, head armour |
| 1138 | `amulet` | sickly amulet, worn on the neck; its `A 28 11` line (`APPLY_BITVECTOR`, bit 11) sets `AFF_POISON` on the wearer with no poison affect behind it |
| 1139 | `bread` | tainted bread: food, fill 2, poisoned |

There are no shops (`shp/index` is `$` alone); `scr/11.scr` and `mdl/11.mdl` are stubs.

### File formats

Each category directory has an `index` listing its files one per line, ending with a line `$`
(`index_boot`, `db.cpp`). A file not in its `index` is not loaded.

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
  `~`, its keyword `~`, then `<exit flags> <key> <to room> <width>` (`setup_dir`).
- `S` ends the room; the file ends with `#99999` and `$~`.
- Rooms must ascend and sit inside a zone's range: a room outside every zone stops the boot
  (`Room <n> is outside of any zone.`) without a `SYSERR`.

A zone (`load_zones`, `zone.cpp`): `#<n>`, name `~`, description `~`, map `~`, an owner list
ending in `0`, `<symbol> <x> <y> <level>`, the top room, the lifespan, the reset mode, then reset
rows ending with `S`. An `M` row is `M <if> <mob> <room> <max in world> <percent> <difficulty>
<max existing> <trophy> <comment>`.

Mobs (`load_mobiles`) and objects (`load_objects`) in `db.cpp`: copy an existing entry and read
the loader before changing a field you do not know. Vnums must ascend across the files of the
index as well as within each, because `real_mobile` and `real_object` binary-search.

### Adding to the world

- A room: append it to `wld/11.wld` before `#99999`, between 1101 and 1199 in vnum order, or
  add `wld/<n>.wld` with a `zon/<n>.zon` and list both in their `index` files. Add a constant
  to `rots_harness/world.py` when a scenario names it.
- A mob: append to `mob/11.mob` in vnum order. A special needs the `MOB_SPEC` act flag as well
  as the program number. Place it with a zone reset row or `load mob`.
- An object: append to `obj/11.obj`; avoid vnums `spec_ass.cpp` passes to `ASSIGNOBJ`.
- Boot after every change: the only check is `test_boot.py` and every other scenario booting,
  failing on any `SYSERR`, crash or missing login prompt.

## Scenario territory and harness territory

| Scenario territory | Harness territory (extending-the-harness.md) |
| --- | --- |
| `tests/integration/scenarios/test_<group>.py` | `tests/integration/conftest.py` |
| `tests/integration/world/` | `tests/integration/rots_harness/` other than `world.py` |
| room constants in `rots_harness/world.py` | `tests/integration/unit/` |
| a marker line in `tests/integration/pytest.ini` | `src/harness/`, `src/tools/rotstool/` |

A helper used by one scenario stays in its scenario file, as `hit_points` does in
`test_poison.py`.
