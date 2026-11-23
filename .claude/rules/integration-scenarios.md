---
paths:
  - "tests/integration/scenarios/**"
  - "tests/integration/rots_harness/**"
---

# Integration scenarios: use the shared helpers

Before writing a helper or a constant in a scenario file, look it up in
`.agents/skills/rots-integration-scenario/shared-helpers.md`; import it from its `rots_harness`
module when it is there, and move it there when a second scenario file needs it.

The five most common calls:

- `replies.reply_to(session, line, outcomes)` to send a line and read up to one of its outcomes.
- `stat_reading.read_stat(implementor, "player <name>")` to read `stat`.
- `immortal_commands.give_new_object(implementor, vnum, keyword, recipient_keyword)` to load an
  object and give it.
- `immortal_commands.goto(implementor, destination, room_name)` to move the implementor.
- `game_log.wait_for_log_count(server.handle.log_path, line, count)` to wait for a game.log line.

Names: `*_FORMAT` format string, `*_REPLY` line answering the reader's command,
`*_START`/`*_END`/`*_PART` fragment, `*_SECONDS` wait, `*_SHORT_DESCRIPTION`, `ROOM_<X>_NAME`.

Known bugs: `test_<base>_known_bugs.py` imports what it shares from `test_<base>.py`, and
`rots_harness` names from `rots_harness`; an imported scenario file imports no scenario file.
