# Extending the harness

Use this when a scenario needs something the harness cannot do today: an input it cannot
control (a known game hour, the weather), a kind of game time `affect-hours` does not run
(combat, mob activity, zone resets), a new request over the control channel, or a new
`ageland_harness` option.

**Whoever owns the harness (the project's maintainers) approves a harness extension before it
is built.** Describe what the scenario needs, why the test world and in-game commands cannot
provide it, and which checklist below it falls under. Then build it with its tests, separately
from the scenario that uses it.

**New C++ code is portable standard C++.** Use the standard library, not POSIX- or Linux-only
APIs: `std::filesystem` with the non-throwing `std::error_code` overloads, `std::chrono` for
time, `<fstream>` for files, `std::thread` for threads. The same applies to new tests and
every helper they use. The harness lives in namespace `harness` under `src/harness/`, and its
production seam is the schedule. Game code changes for a test only through a seam that is also
a legitimate design improvement, called out explicitly (AGENTS.md, "Testing Guidelines").

**Some game sources use CRLF line endings** (`comm.h`, `utils.h` and `handler.cpp` among them):
keep each file's endings when you edit it, or the commit shows a whole-file diff.

Build and run the checks with:

```bash
make test                                  # ageland_tests, which compiles HARNESS_SOURCES
make integration-unit PYTHON=.venv/bin/python
make integration PYTHON=.venv/bin/python   # every scenario, after any harness change
```

`make test` builds and runs 32-bit Linux binaries, so on a Mac run it inside the Docker container
(`docker compose run --rm -T rots bash -lc 'make test'`) under a lock taken by hand
(harness-reference.md, "The shared Docker lock").

## A new schedule

A schedule decides which world updates each game-loop pass runs (`GameTickSchedule::WorldPass`,
`src/game_tick_schedule.h`).

1. `src/harness/<name>_schedule.{h,cpp}`: a class deriving `GameTickSchedule`. `next_pass()` is
   called exactly once per pass. Keep the phase contract the existing tests check: the phase
   stays in range, the hourly update comes only with the fast update at phase 0, and the phase
   advances by one exactly on fast-update passes.
2. `src/harness/<name>_registration.{h,cpp}`, modelled on `affect_hours_registration.cpp`:
   - a name constant;
   - a factory returning `ScheduleSetup{schedule, requests, ready_state}`
     (`src/harness/schedule_setup.h`), registering the schedule's requests and returning an
     empty `ScheduleSetup{}` when a request cannot be registered;
   - `register_<name>_schedule(ScheduleRegistry&)` filling a
     `ScheduleDefinition{name, summary, create_setup}` (`src/harness/schedule_definition.h`).
3. Call the registration in `src/harness/main.cpp` beside `register_affect_hours_schedule`,
   returning `EXIT_FAILURE` when it fails.
4. Add the `.cpp` files to `HARNESS_SOURCES` and the test files to `ROTS_TEST_SOURCES` in
   `src/CMakeLists.txt`.
5. Python: scenarios use `@pytest.mark.schedule("<name>")`. `TimeControl.wait_ready`
   (`tests/integration/rots_harness/timecontrol.py`) expects `ready schedule=<name> hours=0`
   exactly; a schedule with a different ready state needs that check generalised, with its
   tests in `tests/integration/unit/test_timecontrol.py`.
6. Tests to add or update:
   - the schedule's own tests, like `src/tests/harness_affect_hours_schedule_tests.cpp`, using
     `keeps_the_phase_contract` from `src/tests/game_tick_schedule_contract.h`;
   - registration and an end-to-end transcript, like
     `src/tests/harness_affect_hours_transcript_tests.cpp`; its
     `IsTheOnlyScheduleItsRegistrationAdds` checks the registry's contents;
   - `src/tests/harness_schedule_registry_tests.cpp`;
   - `src/tests/harness_options_tests.cpp`, whose unknown-schedule message lists every
     registered name.

## A new control request

A request is a line on the harness binary's stdin; its reply is one line on stdout starting
`harness: `. Requests belong to a schedule, not to the binary. They are answered on the game
thread, inside `HarnessControlledSchedule::next_pass`
(`src/harness/harness_controlled_schedule.cpp`) before the pass's world updates, so a request
may change game state without locking.

1. `src/harness/<name>_request.{h,cpp}`: a class deriving `HarnessRequest`
   (`src/harness/harness_request.h`) with `name()` and `handle(line, arguments)`. `handle`
   returns an `ImmediateReply` or a `DeferredReply` (`src/harness/request_outcome.h`); a body
   starts with `done ` or `error `. A deferred reply refers to the schedule through a
   `std::weak_ptr`.
2. Register it in the schedule's factory with `setup.requests.register_request(...)`, as
   `create_affect_hours_setup` registers `AdvanceHoursRequest`. The registry
   (`src/harness/harness_request_registry.cpp`) refuses a duplicate or null request.
3. The reply grammar (`dispatch_request_line`, `src/harness/request_dispatch.cpp`): one line of
   at most `MAX_REQUEST_LINE_LENGTH` (64) characters, one trailing `\r` tolerated, the first
   word the request's name and the rest its arguments. Errors already defined include
   `error empty request` and `error <line>: unknown request`.
4. Add the files to `HARNESS_SOURCES` and the tests to `ROTS_TEST_SOURCES`.
5. Python: a `TimeControl` method that writes the line and accepts only its own `done ...`
   reply, raising `TimeControlError` on `error ...` (model: `TimeControl.advance_hours`), and a
   `HarnessServer` method in `tests/integration/conftest.py` that calls it with a timeout (model:
   `HarnessServer.advance_hours`). Unit tests in `tests/integration/unit/test_timecontrol.py`
   using the `fake_server_and_control` fixture.
6. C++ tests to mirror: `src/tests/harness_advance_hours_request_tests.cpp`,
   `harness_request_dispatch_tests.cpp`, `harness_request_registry_tests.cpp`,
   `harness_controlled_schedule_tests.cpp` (with `src/tests/fake_harness_request.h`) and the
   transcript tests.

## A new `ageland_harness` option

The harness binary claims its own options and passes every other argument, in order, to the
server's `parse_startup_options` (`src/comm.cpp`).

Parsing a value is not using it. `main` in `src/harness/main.cpp` builds the `ScheduleSetup`
before `run_the_game`, and `boot_db` then sets the game clock and weather from the wall clock
(`reset_time`, `db.cpp`). An option that changes world state, such as a starting hour, needs a
seam that acts after boot, for example on the first pass in `HarnessControlledSchedule`.

- **An option only the harness reads** (`--name value`):
  1. In `harness_option_parser` (`src/harness/harness_options.cpp`), build and register a
     `CommandLineOption` with its own `required` flag and `validate` function. `--schedule` is
     the model for the shape only: it is required, and its validator accepts only registered
     schedule names.
  2. Add its field to `HarnessOptions` (`src/harness/harness_options.h`) and fill it in
     `parse_harness_options`. Usage text follows from the registration (`harness_usage`).
  3. Names start with `--`, may not repeat, and may not be one of `RESERVED_OPTION_NAMES`
     (`src/harness/command_line_parser.h`, today only `--random-seed`). The value is the next
     argument; `--name=value` is refused for a registered name.
- **An option the server itself must understand** (like `--random-seed`) goes to
  `parse_startup_options` instead: add its branch there and its field to `StartupOptions`
  (`src/comm.h`). If the harness parser must leave it alone, add it to `RESERVED_OPTION_NAMES`
  and update `ReservesOnlyTheRandomSeed` in `src/tests/harness_command_line_parser_tests.cpp`.
  Keep the usage strings in `src/main.cpp` and `AGELAND_OPTIONS_SUMMARY`
  (`src/harness/harness_options.cpp`) in step.
- Python: pass the argument in `LocalProcessLauncher.command` and
  `DockerComposeLauncher.server_command` (`tests/integration/rots_harness/launcher.py`). When a
  test chooses it, add the parameter to `ServerLauncher.start` (abstract) and to both concrete
  `LocalProcessLauncher.start` and `DockerComposeLauncher.start`, and pass it from the
  `harness_launcher.start(...)` call in the `server` fixture (`tests/integration/conftest.py`).
- Tests: `src/tests/harness_options_tests.cpp`, `src/tests/harness_command_line_parser_tests.cpp`,
  `src/tests/startup_options_tests.cpp` for a server option, and the launcher command tests in
  `tests/integration/unit/test_launcher.py`
  (`test_local_launcher_builds_the_harness_binary_command_for_a_schedule`,
  `test_docker_launcher_runs_the_harness_binary_for_a_schedule`).

## A new Python module or fixture

1. A module goes in `tests/integration/rots_harness/` (`pythonpath = .` in `pytest.ini` makes
   `from rots_harness import <module>` work). House style: a module docstring,
   `from __future__ import annotations`, a `#` comment above every module constant and every
   instance attribute, descriptive names.
2. Its unit tests go in `tests/integration/unit/test_<module>.py` and need no server
   (`make integration-unit`). Models: `test_groups.py`, `test_characters.py`,
   `test_timecontrol.py` (fake server pipes), `test_libbuilder.py` (fake tool runner),
   `test_session.py` (loopback socket), `test_launcher.py`.
3. A fixture goes in `tests/integration/conftest.py`. `conftest.py` has no unit tests of its
   own; running the scenarios is its test.
4. A new character field changes both sides: `Character.to_spec` in
   `tests/integration/rots_harness/characters.py`, and rotstool's parser
   (`src/tools/rotstool/fixture_spec_parser.cpp`, `FixtureSpec` in `fixture_spec.h`,
   `fixture_character_maker.cpp`) with `src/tests/rotstool_fixture_spec_tests.cpp` and
   `rotstool_fixture_maker_tests.cpp`.
5. Update `tests/integration/README.md` for anything a scenario writer uses: a new marker,
   method, variable or file in the run directory. A marker that configures a test goes under
   `markers` in `pytest.ini`; a group is never a harness change, only a file in
   `tests/integration/groups/` (`rots_harness/groups.py` reads them for `conftest.py` and CI).

## The world check and the catalogue

- `rots_harness/world_files.py` reads the test world's files: `WORLD_CATEGORIES` (the folders
  the lib builder copies), each index's expected order (`index_order`), record vnums and zone
  headers. `rots_harness/world_check.py` holds the checks `unit/test_world_files.py` runs on the
  committed world; a new rule is a function there returning one line per problem, with a
  synthetic-world test that it fails.
- `rots_harness/world_catalogue.py` prints the world (`make world-catalogue`); its readers follow
  the loaders in `db.cpp`, so a new field the loaders read changes the reader too. The flag names
  it prints are copied from `src/structs.h` and `src/script.h` into `rots_harness/world_flags.py`;
  `unit/test_world_catalogue.py` checks them against the headers, so a renamed or renumbered
  constant fails there until the table follows it.
