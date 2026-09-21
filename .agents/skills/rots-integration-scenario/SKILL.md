---
name: rots-integration-scenario
description: Write an integration scenario for the RotS server when a change can only be verified against a running game (several players, hourly or affect ticks, room affects, link drops, on-disk player records) and a GoogleTest unit test cannot cover it. Use it when feature work touches such behaviour; the executing agent judges whether a new scenario is warranted.
---

# Writing a RotS integration scenario

The harness under `tests/integration/` boots a real `ageland` server on a throwaway `lib/`
seeded with fixture characters, drives it over telnet, and asserts on the transcripts. A
scenario is a pytest function that logs in some of those characters, makes the server do
something, and reads the result back. This skill tells you when a scenario is the right
test, how to write one that does not flake, and how to prove it before you hand it over.

Read `harness-reference.md` (API, roster, rooms, commands) and `gotchas.md` (server
behaviours that have already cost someone a fix round) alongside this file.

## 1. Decide whether a scenario is the right test

Prefer the cheapest test that can fail for the right reason. Walk down this ladder and stop
at the first rung that fits:

1. **GoogleTest unit test** (`src/tests/`). Right whenever the behaviour is a function of
   in-memory state you can build in a fixture: one character, one affect, one room. Most
   server logic is here. `AGENTS.md` asks for this first.
2. **Scenario on the existing roster and world.** Right when the behaviour needs more than
   one connected player, the hourly tick or the affect sweep, a room affect that spans
   ticks, a socket that drops or a character that quits mid-effect, or a record the server
   writes to disk (`*.exploits.json`, the character file, the object file).
3. **Scenario plus roster or world data.** As above, but the existing five characters or the
   arena rooms cannot express the setup: a new race or profession, a mob with a special, a
   room shape (a crevice below, a dark cell). Data changes live under
   `tests/integration/rots_harness/fixtures.py` and `tests/integration/world/`.
4. **Scenario plus a server harness sub-command.** Only when no client command can reach the
   state at all. `harness tick` and `harness affects` (`src/test_harness.cpp`) are the
   existing examples; each is gated behind the `-t` start flag and paired with a gtest.
   This is the one production seam the harness allows. Say so explicitly in your report.

If a change is on rung 1, write the gtest and stop. A scenario costs a server boot per test
(about ten seconds of the CI job's thirty-minute budget) and a review of its timing, so it
must earn its place by pinning something a gtest cannot.

## 2. Work from an invariant, not a feature

State in one sentence what must be true after the scenario runs, and name the server
mechanism that makes it true by file and function. The module docstring carries that
sentence, the mechanism, and the source citation. It is the first thing a reviewer reads and
the thing that tells a future reader whether a failure is a regression or a stale test.

Then pick the observation. Every assertion reads something the server actually emits:

- a line in a character's transcript (`GameSession.command`, `expect`, `drain`);
- a `stat` reply parsed by `Transcript.hit_points`, `abilities`, `room_name`;
- a line in `game.log` (`wait_for_log_line` in `scenarios/blaze_support.py`);
- a record on disk under the run's `lib/` (`rots_harness/records.py`).

Find the exact marker text by running the flow once with `ROTS_IT_KEEP=1` and reading the
kept transcript, then cite where the server prints it (`act()` string, `send_to_char`,
`mudlog`). A marker you guessed will match nothing; a marker that matches anything asserts
nothing.

## 3. The recipe

1. **Create `tests/integration/scenarios/test_<behaviour>.py`** with the module docstring
   from section 2 and `pytestmark = pytest.mark.scenario`. Request only the sessions you use:
   `server`, `imp`, `mage`, `fighter`, `victim`, `caller`, `harness`. Each test gets a fresh
   server; there is no state to clean up between tests.
2. **Stage the setup through the imp.** `goto`, `transfer`, `restore`, `wizset`, `load mob`,
   `purge` are the tools. After every room move, call `expect_room` on the moved session;
   a `look` issued before the move lands will read the old room.
3. **Trigger the behaviour** with the acting session: `cast` (which retries a failed cast
   up to its budget and returns the transcript), `command`, or `send_line` when the reply is
   delayed.
4. **Advance time deliberately.** `harness.tick()` runs the hourly block once;
   `harness.affects()` forces one person-affect pass. Neither stops the real-time fast
   block, which still runs every three seconds and regenerates hit points, so a value you
   floored before a loop must be re-floored on every iteration (see `tick_until_marker`).
5. **Wait on events, never on time.** `expect(markers, timeout)` and the wait helpers in
   `combat_support.py` and `blaze_support.py` return as soon as the event lands and fail with
   the transcript when it does not. A bare `drain(n)` or `time.sleep(n)` is a blind wait;
   the review will ask for the marker it should have been.
6. **Bound every loop by the mechanism.** A retry budget must be smaller than whatever the
   loop consumes: an affect's duration, a character's hit points against regen, a cast's
   mana. State the bound in the docstring. When the precondition vanishes early (the room
   affect burned out, the target died to a stray swing), fail with a message that says so
   instead of letting the loop time out.
7. **Assert ranges where the observation is later than the event.** A `stat` after a death
   sees regen; `poison_support.REGEN_ALLOWANCE` is the shared slack. Exact equality is for
   values the server writes once.
8. **Run the file, then the suite.** `make integration PYTHON=build/integration-venv/bin/python`
   with `-k <name>` first, then the whole suite, against your local server build and the
   launcher your environment is configured with (`harness-reference.md`, "Running"). Delete
   only the run directories your own runs kept.
9. **Read the sanitized CI job as the gate.** The `integration-asan` job runs the suite
   against an AddressSanitizer build; a use-after-free your scenario provokes fails there
   even when it passed locally. Push, then read that job's result before reporting.

## 4. Skeleton

```python
"""<One sentence: what must be true.> <Mechanism: file.cpp function(), what it does.>

<Why a gtest cannot pin it.> <Bound on any loop below and where it comes from.>
"""
from __future__ import annotations

import pytest

from rots_harness import fixtures
from rots_harness.session import GameSession

pytestmark = pytest.mark.scenario

EFFECT_MARKER = "You feel very sick."  # spell_poison, mystic.cpp: victim's own line


def test_<behaviour>_<outcome>(server, imp, mage, victim, harness) -> None:
    imp.command(f"goto {fixtures.ROOM_ARENA_WEST}")
    imp.command("transfer harnvictim")
    victim.expect_room("Arena West")

    mage.cast("poison", "harnvictim", success_markers=EFFECT_MARKER)
    harness.affects()

    stat = imp.command("stat harnvictim")
    assert stat.hit_points() is not None, stat.text
    current, _maximum = stat.hit_points()
    assert current < server.spec("Harnvictim").hit, stat.text
```

## 5. Extending the roster or the world

- **A character**: add a `CharacterSpec` to `STANDARD_ROSTER` in `rots_harness/fixtures.py`
  with the next `idnum`, and a session fixture in `conftest.py` if tests log it in. Race
  matters: `other_side()` puts a magus and a wood elf on opposite sides, which blocks
  `summon` and changes how names render. Add a case to `tests/integration/unit/test_fixtures.py`.
- **A room or mob**: edit `tests/integration/world/wld/11.wld` or `mob/11.mob` (zone 11 is
  the harness zone). Exit directions are `D0` north through `D3` west, `D4` up, `D5` down.
  A mob whose special must fire needs the `MOB_SPEC` act flag as well as the program number.
  Add the expectation to `tests/integration/unit/test_world.py`, which parses these files
  without a server.
- **A harness sub-command**: extend `do_harness` in `src/test_harness.cpp`, add the gtest in
  `src/tests/test_harness_tests.cpp`, and the Python wrapper on `Harness` in `conftest.py`.
  Keep the command a pure trigger of existing server code paths; it must not implement
  game logic of its own.

## 6. Self-review before hand-over

`AGENTS.md` assigns test design an adversarial partner, Bazarat. Ask its questions of your
scenario before anyone else does:

- Which assertion would still pass if the mechanism were deleted? Remove or strengthen it.
- Which wait is a fixed delay rather than an event? Replace it with the marker.
- Which loop's budget exceeds the duration of the thing it depends on?
- Which name in a transcript could belong to a different character or render differently to
  a different observer (see `gotchas.md` on `*an Uruk*`)?
- If the setup fails partway (a cast never lands, a mob does not engage), does the test say
  so, or does it time out in a later step with a misleading message?
- Does the docstring state one fact once, with the source citation, and nothing the code
  already says?

Report the scenario's result with the run command you used, the pass count, and the CI
job's conclusion. If you left a known flake bound or a rung-4 seam, say so in the report.
