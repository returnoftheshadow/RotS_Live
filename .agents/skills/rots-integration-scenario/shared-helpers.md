# Shared scenario helpers

Look here before writing a helper or a constant in a scenario file. Every name below is in
`tests/integration/rots_harness/` and is imported from the module named, for example
`from rots_harness.replies import reply_to`. `unit/test_scenario_shared_helpers.py` fails when a
scenario file defines a function named like a shared one, or a string or vnum equal to a
shared constant.

## Need → call

### Sending a line and reading the reply

| Need | Call |
| --- | --- |
| Send a line and read up to one of several outcomes, then settle | `replies.reply_to(session, line, outcomes, settle=, timeout=)` |
| Read up to an outcome without sending (text already on its way) | `replies.settled_expect(session, outcomes, settle=, timeout=)` |
| What an observer saw of an actor's earlier commands | `replies.text_seen_before_a_say(actor, actor_name, observer)` |
| The default settle after an outcome | `replies.SETTLE_SECONDS` (1.0) |
| A one-line command whose reply ends in a prompt | `GameSession.command(text)` returns a `Transcript` (`session.py`) |
| A login prompt or menu text | `session.LOGIN_PASSWORD_PROMPT`, `session.ACCOUNT_MENU_PROMPT`, `session.CHARACTER_MENU_PROMPT` |

### Lines and numbers in a reply

| Need | Call |
| --- | --- |
| Lines split at newlines, blank lines kept | `text_lines.raw_lines(text)` |
| Non-blank lines, stripped | `text_lines.stripped_lines(text)` |
| A `Transcript`'s text without carriage returns | `text_lines.reply_text(reply)` |
| Assert lines appear consecutively, in order | `text_lines.assert_lines(reply, lines)` |
| Assert lines each appear once, in order (not consecutive) | `text_lines.assert_in_order(text, lines)` |
| The lines after a header (`look in corpse`, a list) | `text_lines.lines_after(text, header, until, drop_prompts=)` |
| A pattern's first group as a number | `text_lines.first_number(pattern, text)` |
| A short description as act() starts a line | `text_lines.capitalised(text)` |

### Stat

| Need | Call |
| --- | --- |
| `stat player <name>`, `stat mob <kw>`, `stat obj <kw>` | `stat_reading.read_stat(implementor, target, last_line, other_outcomes, settle=)` |
| The last line to wait for | `stat_reading.CHARACTER_STAT_LAST_LINE` (default), `stat_reading.OBJECT_STAT_LAST_LINE` |
| `stat mob` finding no mob | `stat_reading.NO_SUCH_MOB_REPLY` as an `other_outcomes` entry |
| The AFF line's names | `stat_reading.affect_bits(stat_text)` |
| A stat value | `stat_reading.HIT_POINTS_PATTERN`, `HIT_POINTS_BEFORE_GAIN_PATTERN`, `HIT_POINTS_UNSIGNED_MAXIMUM_PATTERN`, `CONSTITUTION_PATTERN`, `STRENGTH_PATTERN`, `LEARNING_PATTERN`, `LEVEL_PATTERN`, `PRACTICE_SESSIONS_PATTERN`, `MAGE_LEVEL_PATTERN`, `SAVING_THROW_PATTERN`, `AFFECT_BITS_PATTERN`, `AFFECT_BITS_AT_LINE_START_PATTERN`, `ANGER_AFFECT_PATTERN`, read with `text_lines.first_number` |

### Implementor set-up commands

Each sends through `GameSession.command` and asserts only what its keyword arguments ask for:

| Need | Call | What it checks |
| --- | --- | --- |
| Go to a room or a character | `immortal_commands.goto(implementor, destination, room_name, exact=)` | nothing without `room_name`; with it, `room_name()` equals it, or with `exact=False` the reply contains it |
| Load an object or a mob | `immortal_commands.load_object(implementor, vnum, short_description)`, `immortal_commands.load_mob(...)` | "You create"; with `short_description`, `CREATED_FORMAT` in full |
| Load an object and give it | `immortal_commands.give_new_object(implementor, vnum, keyword, recipient_keyword, shown_recipient=, short_description=)` | the load as above; "You give", plus "to <shown_recipient>." and/or the object's name, `GIVEN_FORMAT` in full with both |
| Set a number field | `immortal_commands.wizset(implementor, target, field, value, shown_name=)` | "<field> set to <value>."; with `shown_name`, "<shown_name>'s ..." |
| An object's or mob's `where` lines | `immortal_commands.where_lines(implementor, vnum, short_description)` | nothing |
| Syslog level, holylight | `immortal_commands.set_syslog(implementor, level)`, `immortal_commands.set_holylight(implementor, setting)` | their reply |
| Their reply texts | `immortal_commands.CREATED_FORMAT`, `GIVEN_FORMAT`, `CREATED_START`, `GIVEN_START`, `WIZSET_FORMAT`, `SYSLOG_SET_FORMAT`, `HOLYLIGHT_SET_FORMAT`, `WHERE_ROW_PATTERN` | |

### The characters and the server

| Need | Call |
| --- | --- |
| The implementor every scenario sets up with | `characters.IMPLEMENTOR`, level `characters.LEVEL_IMPLEMENTOR` |
| Another immortal | `characters.immortal(name, level)` |
| A mage with a custom profession split | `characters.custom_mage(...)` |
| The harness account's email and password | `characters.HARNESS_EMAIL`, `characters.HARNESS_PASSWORD`; its email prompt is `launcher.LOGIN_EMAIL_PROMPT` |
| Log a character in | `server.login(name)` (`HarnessServer`) |
| Quit and log the same character in again | `session = server.relog(session)` |
| Run game hours (needs `@pytest.mark.schedule`) | `server.advance_hours(hours)` |
| Run hours and read what arrived | `server.text_after_hours(session, hours, expected_marker, settle=)` |

### game.log

| Need | Call |
| --- | --- |
| Wait until a line is logged n times | `game_log.wait_for_log_count(server.handle.log_path, line, count, exact=, timeout=, poll=, describe=)` |
| Count a mudlog message once it is logged, without asserting | `game_log.count_logged(log_path, message, expected_count, timeout=, poll=)` |
| Read now, without waiting | `game_log.read_game_log`, `game_log.count_in_log`, `game_log.log_lines_holding`, `game_log.logged_messages` |
| The default wait and poll | `game_log.LOG_TIMEOUT_SECONDS`, `game_log.LOG_POLL_SECONDS` |

### Casting

| Need | Call |
| --- | --- |
| Cast until past the concentration roll | `casting.cast_until_it_lands(caster, line, outcomes, timeout=, settle=)` |
| Its texts and wait | `casting.LOST_CONCENTRATION_REPLY`, `casting.NO_ENERGY_REPLY`, `casting.CAST_TIMEOUT_SECONDS` |
| The other refusals a caller passes it | `game_text.UNKNOWN_SPELL_REPLY`, `game_text.TARGET_NOT_CARRIED_REPLY` |

### Accounts and saved files

| Need | Call |
| --- | --- |
| A connection at the email prompt | `with accounts.account_connection(server.handle, server.run_dir, label) as connection:` |
| A logged-in connection the fixture does not quit | `with accounts.playing_connection(server.handle, server.run_dir, character) as connection:` |
| Whether the server closes a connection | `accounts.closes_within(connection, seconds)` |
| `account show`'s character list | `accounts.account_show_characters(implementor)` |
| Account-menu texts | `accounts.ACCOUNT_MENU_NEW_CHARACTER`, `NEW_CHARACTER_NAME_PROMPT`, `INVALID_CREDENTIALS_REPLY`, `SHORT_PASSWORD_REPLY`, `ACCOUNT_SHOW_CHARACTERS_START`, `UNREADABLE_ROW_PART` |
| The harness account's record | `saved_files.stored_account(server.lib_dir)`, `saved_files.HARNESS_ACCOUNT_FILE`, `saved_files.HARNESS_ACCOUNT_FOLDER` |
| A character's saved file | `saved_files.account_file(lib_dir, name, kind)`, kinds `saved_files.CHARACTER_FILE_KINDS` |
| Saved wear positions | `saved_files.saved_wear_positions(lib_dir, name)`, `saved_files.HEAD_WEAR_POSITION`, `saved_files.CARRIED_WEAR_POSITION` |

### Shaping editors

| Need | Call |
| --- | --- |
| Close an editor with /free | `shaping.release_editor(session, released_reply, settle=)` |
| The settle every shaping and text-editor file passes | `shaping.EDITOR_SETTLE_SECONDS` (0.5) |
| The records edited | `shaping.SHAPED_RECORD`, `SHAPED_SCRIPT`, `ORDERED_SCRIPT`, `ZONE_NUMBER`, `ZONE_FILE_RELATIVE`, `SCRIPT_FILE_RELATIVE` |
| Each editor's prompt | `shaping.MOBILE_PROMPT`, `OBJECT_PROMPT`, `IMMORTAL_START_ROOM_PROMPT`, `SIGNPOST_ROOM_PROMPT`, `ZONE_PROMPT`, `SCRIPT_PROMPT`, `ORDERED_SCRIPT_PROMPT` |
| Each editor's /free line | `shaping.MOBILE_RELEASED_REPLY`, `OBJECT_RELEASED_REPLY`, `ROOM_RELEASED_REPLY`, `ZONE_RELEASED_REPLY`, `SCRIPT_RELEASED_REPLY` |
| Other editor texts | `shaping.COULD_NOT_OPEN_FILE_REPLY`, `NO_BACKUP_FILE_START`, `ROOM_IMPLEMENTED_REPLY`, `ZONE_IMPLEMENTED_REPLY`, `EDITOR_COMMAND_PART`, `SECTION_TITLE_PROMPT_START`, `SECTION_COMMENT_PROMPT_START` |

### Fixed game text (`game_text`)

| Need | Constant |
| --- | --- |
| Immortal commands' replies | `FORCED_REPLY`, `PURGED_REPLY`, `NO_ONE_AROUND_REPLY`, `TELEPORTED_FORMAT`, `TELEPORTED_END` |
| Interpreter and shop refusals | `UNRECOGNIZED_COMMAND_REPLY`, `NOT_HERE_REPLY`, `TARGET_ABSENT_REPLY` |
| Fighting, dying, following, ordering | `FIGHT_ONGOING_REPLY`, `IS_DEAD_FORMAT`, `DEATH_CRY_FORMAT`, `NEW_BODY_MESSAGE`, `NOW_FOLLOWING_FORMAT`, `NOW_FOLLOWS_FORMAT`, `FOLLOWED_FORMAT`, `FOLLOWER_ACK`, `ORDER_DONE_REPLY`, `NOBODY_WILLING_REPLY`, `CONCENTRATE_REPLY` |
| Objects | `GET_FORMAT`, `GET_FROM_START_FORMAT`, `DROP_FORMAT`, `PUT_FORMAT`, `WEAR_HEAD_FORMAT`, `WEAR_WAIST_FORMAT`, `STOP_USING_FORMAT`, `INVENTORY_HEADER`, `CORPSE_CONTENTS_HEADER`, `QUIT_DROP_REPLY` |
| Food, drink, poison, positions | `EATEN_POISON_REPLY`, `DRINK_EMPTY_REPLY`, `POISON_TICK`, `SIT_DOWN_REPLY`, `STAND_UP_REPLY` |
| Where, rent, overflow | `NOTHING_FOUND_REPLY`, `RENTED_REPLY`, `INNKEEPER_TELLS_START`, `OUTPUT_OVERFLOW` |
| The text editor | `TEXT_EDITOR_PROMPT`, `TEXT_SO_FAR`, `TEXT_EDITOR_CURSOR` |

### The test world (`world`)

| Need | Constants |
| --- | --- |
| Rooms | `ROOM_IMMORTAL_START`, `ROOM_ARENA_WEST`, `ROOM_ARENA_CENTRE`, `ROOM_ARENA_EAST`, `ROOM_DARK_CELL`, `ROOM_GALLOWS_APPROACH`, `ROOM_GALLOWS_PIT`, `ROOM_HUMAN_START`, `ROOM_SIGNPOST`, `ROOM_TRADE_HALL`, `ROOM_BROKEN_MARKET`, `ROOM_INN`, `ROOM_BOAR_PEN`, `ROOM_ORDER_HALL` |
| Room names | `ROOM_<X>_NAME`, such as `ROOM_ARENA_CENTRE_NAME` |
| Objects | `OBJECT_<X>`, such as `OBJECT_LEATHER_CAP`, `OBJECT_HARNESS_TOKEN`, `OBJECT_GREY_SASH` |
| Mobs | `MOB_INNKEEPER`, `MOB_BYSTANDER_ORC`, `MOB_HARDY_TARGET_ORC`, `MOB_SHINING_ANGEL`, `MOB_STRAW_SCARECROW`, `MOB_MYSTERIOUS_TRAVELLER`, `MOB_PORTER`, `MOB_PRACTICE_DUMMY` |
| Short descriptions and keywords | `<vnum constant>_SHORT_DESCRIPTION`, such as `OBJECT_LEATHER_CAP_SHORT_DESCRIPTION`, `MOB_PORTER_SHORT_DESCRIPTION`; `OBJECT_STRANGE_RELIC_KEYWORD` |

## Settle

A file whose replies settle 0.5 seconds (the shaping and text-editor files) passes
`settle=EDITOR_SETTLE_SECONDS` to every shared helper it calls. Every other file takes each
helper's default, `replies.SETTLE_SECONDS`, and imports that name rather than defining its own.

## Where a new shared name goes

A helper or constant one scenario file uses stays in that file. When a second scenario file
needs it, move it to the module whose docstring covers it and import it in both:

| What it is | Module |
| --- | --- |
| Waiting for and reading a reply | `replies` |
| Splitting text into lines or numbers | `text_lines` |
| Reading `stat`, or a stat-line pattern | `stat_reading` |
| Fixed text the game prints, from the same game function, that no helper waits for | `game_text` |
| A room, object or mob, its name or short description | `world` (and its row in `unit/test_world.py`) |
| An implementor's set-up or inspect command | `immortal_commands` |
| Reading game.log | `game_log` |
| Casting | `casting` |
| Account connections and account-menu text | `accounts` |
| Files the server saved | `saved_files` |
| Shaping-editor facts | `shaping` |
| A test character two files declare | `characters` |

Each new function gets a unit test in `unit/test_<module>.py`. An edit under `rots_harness/`
runs every CI group, where an edit to one scenario file runs only its groups: batch shared
additions into one change.

## Names

`*_FORMAT` is a format string; `*_REPLY` a fixed line answering the reader's own command;
`*_START`, `*_END` and `*_PART` a fragment matched at a line's start, end or middle; any other
fixed line is named for what it is (`POISON_TICK`). Lists of texts are plural nouns
(`CAST_OUTCOMES`). Waits end in `_SECONDS`, short descriptions in `_SHORT_DESCRIPTION`, room
names are `ROOM_<X>_NAME`. No `*_MARKER`, `*_FRAGMENT` or `*_TEXT`.

## Known-bugs files

- A `test_<base>_known_bugs.py` imports what it shares with `test_<base>.py` from that file:
  `from test_<base> import ...`.
- A scenario file another one imports imports no scenario file. CI follows importers one level
  only, so a chain hides the top file's groups (`unit/test_ci_groups.py` checks it).
- A name from `rots_harness` is imported from `rots_harness`, never through the base file.
