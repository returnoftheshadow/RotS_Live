# Harness reference

Signatures and values as of the slice 2 suite. When this file and the code disagree, the
code wins; fix the file in the same change.

## Running

The harness boots whatever server build you point it at, through a launcher chosen by the
environment. Any way of building and hosting the game locally works as long as one of the
launchers can start it; add a launcher (below) if none does.

```sh
python3 -m venv build/integration-venv                      # once
build/integration-venv/bin/pip install pytest               # once
make integration-unit PYTHON=build/integration-venv/bin/python   # harness self-tests, no server
make integration      PYTHON=build/integration-venv/bin/python   # the scenario suite
build/integration-venv/bin/python -m pytest tests/integration -q -k <name>   # one scenario
```

| Variable | Meaning | Default |
| --- | --- | --- |
| `ROTS_IT_LAUNCHER` | `local` runs the binary as a child process; `docker` runs it through the repo's compose service | `local` on Linux, `docker` elsewhere |
| `ROTS_IT_BINARY` | server binary, relative to the repo root | `bin/ageland` |
| `ROTS_IT_SEED` | RNG seed passed to the server | `20260919` |
| `ROTS_IT_KEEP=1` | keep the run directory after a passing run | unset |
| `ROTS_IT_DOCKER_LOCK_DIR` | lock directory for the docker launcher | `/tmp/rots-docker-lock` |

Run output lands in `build/integration/<run-id>/`: the seeded `lib/`, the server's
`game.log`, and one transcript per logged-in character. The directory is kept when a test
fails, when the crash monitor finds a report, when the server fails to start, or when
`ROTS_IT_KEEP=1` is set. Delete only the directories your own runs left behind; other
people's kept runs are their evidence.

**Adding a launcher.** `rots_harness/launcher.py` defines `ServerLauncher` with `start(run_dir,
lib_dir, port, seed) -> ServerHandle` and `stop(handle)`. `LocalProcessLauncher` and
`DockerComposeLauncher` are the two implementations; `choose_launcher()` in `conftest.py`
maps `ROTS_IT_LAUNCHER` to one. A new hosting mechanism is a third subclass plus a branch
there and a case in `tests/integration/unit/test_launcher.py`. The server must be started
with `-t` (harness mode) and the seed; copy the argument list from `LocalProcessLauncher.command`.

**Docker launcher notes** (only if that is what you use): the launcher takes a lock file in
the lock directory and refuses to start while another `*.lock` is present, so do not create
your own lock there for a harness run. If you run the server or test binary in the
container by hand, prefix with `ulimit -c 0`; a core dump under emulation hangs the
container. The container has no sanitizer runtime; CI's `integration-asan` job is the
sanitized gate.

## Fixtures (`tests/integration/conftest.py`)

| Fixture | Type | Notes |
| --- | --- | --- |
| `server` | `HarnessServer` | fresh server per test; `.lib_dir` is the run's `lib/`; `.character_number(name)`, `.spec(name)` |
| `imp`, `mage`, `fighter`, `victim`, `caller`, `pupil`, `novice` | `GameSession` | logged in on request; quit at teardown |
| `harness` | `Harness` | `.tick()` runs the hourly block once; `.affects()` forces one person-affect pass |
| `fail_on_server_crash` | autouse | fails the test on any signal or sanitizer report in `game.log` |

`launcher.server_binary_is_sanitized(binary)` tells a scenario whether the server binary is an
AddressSanitizer build (it carries `__asan_init`), whichever launcher starts it, e.g. for an
xfail only ASan can trigger.

## Roster (`rots_harness/fixtures.py`, `STANDARD_ROSTER`)

| Name | Race | Level | Professions | Skills | Hit / Mana / Move | Starts in |
| --- | --- | --- | --- | --- | --- | --- |
| Harnimp | god | 100 | all four at 30 | none | 1000 / 1000 / 1000 | 1101 Immortal Start |
| Harnmage | magus | 30 | mage 120, mystic 30 | blaze, poison, mist, haze, summon, earthquake, remove poison, resist poison | 200 / 600 / 200 | 1131 Arena Centre |
| Harnfighter | human | 20 | warrior 20 | none | 200 / 50 / 200 | 1131 |
| Harnvictim | wood elf | 10 | ranger 10 | none | 60 / 40 / 120 | 1131 |
| Harncaller | human | 30 | mage 30 | summon, blaze | 200 / 600 / 200 | 1131 |
| Harnpupil | human | 27 | mage 27 | none | 200 / 600 / 200 | 1131 |
| Harnnovice | human | 17 | mage 17 | none | 200 / 600 / 200 | 1131 |

All share one account (`harness@example.com`) and constitution 11
(`tests/integration/fixtures/character.template.json`). Harncaller exists because a magus
and a wood elf are on opposite sides, which `summon` refuses.

## Rooms (`rots_harness/fixtures.py`, zone 11 in `tests/integration/world/`)

| Constant | Vnum | Name | Notes |
| --- | --- | --- | --- |
| `ROOM_IMMORTAL_START` | 1101 | Immortal Start | imp's start room |
| `ROOM_ARENA_WEST` | 1130 | Arena West | exit down to 1136 (earthquake crevice) |
| `ROOM_ARENA_CENTRE` | 1131 | Arena Centre | roster start room |
| `ROOM_ARENA_EAST` | 1132 | Arena East | |
| `ROOM_DARK_CELL` | 1133 | dark room for summon-by-name |
| `ROOM_CORRIDOR_ONE` / `_TWO` | 1134 / 1135 | two rooms away from the arena for flee cases |
| `ROOM_CREVICE_FLOOR` | 1136 | below 1130 | |
| `ROOM_WOOD_ELF_START` | 1170 | | |

Harness mobs: `1130` target orc (plain melee target), `1131` snake with the poison special
(`MOB_SPEC` set). Load with `load mob <vnum>`; remove with `purge <keyword>`.

`mob/guildmasters.mob` holds copies of six real guildmasters at their real vnums (1503, 2043,
4601, 10003, 13600, 32200), so `spec_ass.cpp` binds `guild` to them; only their teaching fields
match the real mobs. `real_mobile()` binary-searches, so mob vnums must ascend across the
index's files as well as within each.

Harness objects: `1130` harness token, `1136` leather bag (keyword `bag`, an open
container that holds the cap), `1137` leather cap (keyword `cap`, head armour), `1138` sickly
amulet (keyword `amulet`, worn on the neck; its `A 28 11` line sets AFF_POISON on the wearer with
no poison affect behind it, `poison_support.wear_the_sickly_amulet`). Load with
`load obj <vnum>`, which puts the object in the loader's inventory. A new harness object must
avoid the vnums `spec_ass.cpp` passes to `ASSIGNOBJ`. The token sits on one of them, a
`gen_board`, and is harmless only because no scenario carries or looks at it.

## `GameSession` (`rots_harness/session.py`)

| Method | Returns | Behaviour |
| --- | --- | --- |
| `command(text, timeout=8.0)` | `Transcript` | sends a line, waits for the prompt, returns everything printed |
| `send_line(text)` | none | sends without waiting; pair with `expect` |
| `expect(markers, timeout=8.0)` | `str` | returns as soon as any marker appears; raises `SessionTimeout` with the text otherwise |
| `drain(timeout=0.5)` | `str` | reads whatever arrives within the interval; a blind wait, use only to discard |
| `expect_room(room_name, timeout=10.0)` | `Transcript` | repeats `look` until the room name shows |
| `cast(spell, target=None, success_markers=(), attempts=6, timeout=12.0)` | `Transcript` | retries a failed cast; raises when no attempt matches a success marker |
| `quit()` | none | `quit` and close; refused by the server while `SPELL_ANGER` lingers |
| `drop_link()` | none | closes the socket without quitting (link-dead) |
| `everything()` | `str` | the whole transcript so far |

`Transcript`: `.text`, `.contains(marker)`, `.hit_points() -> (current, max) | None` and
`.experience() -> int | None` (the live `XP:` value) from a `stat` reply,
`.abilities() -> dict | None`, `.room_name() -> str | None` from a `look`.
`.perception_and_willpower() -> (perception, willpower) | None` reads a `stat` reply's `Perception %d, Willpower %d,` line.

## Records (`rots_harness/records.py`)

`read_exploits(lib_dir, name) -> list[ExploitRecord]` reads `<name>.exploits.json`;
`read_character(lib_dir, name) -> dict` reads the character file. Both read the run's
`lib/`, so call them after the server has saved (a quit, a death, or `save`).

`read_pkills(lib_dir) -> list[PkillRecord]` reads the binary `lib/misc/pklist`: one 24-byte
`PKILL` record (`pkill.h`) per credited killer, with idnums in `killer_id`/`victim_id`.
`pkill_create()` appends them when a player is killed by other players, so no save is
needed. An empty or missing file reads as no records (`boot_pkills()` recreates it empty).

## Support modules (`tests/integration/scenarios/`)

| Module | Provides |
| --- | --- |
| `poison_support.py` | `POISON_LANDED`, `POISON_EXTENDED` (an equal poison extended the running one), `POISON_BLOCKED` (a weaker poison was refused), `DEATH_MARKER`, `REGEN_ALLOWANCE`, `death_tick_budget(hit)`, `poison_until_it_lands(caster, victim, word)`, `affect_ticks_until_death(harness, victim, budget)`; remove and resist poison markers (`REMOVE_POISON_CURED`, `REMOVE_POISON_ROOM`, `RESIST_POISON_STARTED`, `RESIST_POISON_CASTER`, `RESIST_POISON_ALREADY`, `RESIST_POISON_TARGET_UNPOISONED`, `RESIST_POISON_SELF_UNPOISONED`) and `CAST_COMPLETED`; `wear_the_sickly_amulet(imp, wearer, name)`; `stat` parsers `spell_affects(text, name)` (duration and modifier of each `SPL:` line) and `affect_flags(text)` (the `AFF:` names) |
| `blaze_support.py` | `BLAZE_CAST`, `LETHAL_HIT`, `floor_hit(imp, name)`, `room_still_burning(imp)`, `tick_until_marker(harness, imp, observer, marker, budget, protect=, refloor=)`, `wait_for_log_line(imp, server, needle)` |
| `combat_support.py` | `wait_for_engagement(imp, mob, victim)`, `wait_for_disengagement(imp, names)`, `move_out_of_the_fight(imp, mover, mover_name, opponent_name)`, `neutralize_melee(imp, name)`, `stat_replies(imp, target, is_genuine)`, `read_affect_listing(imp, name)` (a whole `stat` reply, affect lines included) |

Put a helper in one of these when a second scenario needs it; a helper used once stays in
its scenario file.

## Server side (`src/test_harness.cpp`)

`-t` starts the server in harness mode: the hourly block no longer runs on the wall clock,
idle timeouts are disabled, and `do_harness` is registered. `harness tick` runs the hourly
block once and prints `Harness: hourly tick complete.`; `harness affects` sets
`harness_force_affect_phase` around one `affect_update()` and prints
`Harness: affect tick complete.`. The real-time fast block (regen, room sweeps every three
seconds) runs in harness mode exactly as in production.
