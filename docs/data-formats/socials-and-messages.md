# Socials & combat messages

**Source files:** `src/act_soci.cpp` (`boot_social_messages:63`, `fread_action:42`),
`src/fight.cpp` (`load_messages:115`); structs `social_messg` (`db.h:223`),
`message_type`/`msg_type`/`message_list` (`structs.h:1972-1990`)
**Files:** `lib/misc/socials` (`SOCMESS_FILE`), `lib/misc/messages` (`MESS_FILE`)
**Status:** ✅ both formats complete. Act-substitution variable list is partial (see notes).

---

## Socials (`lib/misc/socials`) — `boot_social_messages:63`

Emote-style commands (`smile`, `wave`, …). Each record:
```
<command>
<hide> <min_victim_position> <min_actor_position>
<char_no_arg>
<others_no_arg>
<char_found>
<others_found>
<vict_found>
<not_found>
<char_auto>
<others_auto>
```
Rules:
- **Line 1** = the command word (`fscanf " %s "`).
- **Line 2** = three ints: `hide` (flag), `min_victim_position`, `min_actor_position`
  (POSITION_* codes — e.g. `POSITION_DEAD 0`, `POSITION_RESTING 5`, `POSITION_FIGHTING 7`,
  `POSITION_STANDING 8`; `structs.h:892-900`). If only some are present the actor position
  defaults to `POSITION_RESTING` (`act_soci.cpp:84-86`).
- **Message lines** are read by `fread_action` (`:42`): each is **one whole line** (not
  tilde-terminated). A line consisting of `#` means "no message" → stored as `NULL`.
- **Short-circuit:** if `char_found` (the 6th line) is `#`/NULL, the social has no targeted
  form and the remaining five lines are **omitted** for that record (`:130-132`). So
  no-target socials are exactly: command, position line, and the three no-arg/`#` lines.
- The message roles:
  | Field | Shown when |
  |-------|-----------|
  | `char_no_arg` / `others_no_arg` | actor types the social with no target |
  | `char_found` / `others_found` / `vict_found` | a target was found (to actor / room / victim) |
  | `not_found` | an argument was given but no such target |
  | `char_auto` / `others_auto` | the target is the actor themselves |
- **Termination:** the loop stops when the next command token begins with `-` or at EOF
  (`:76,87-88`). Socials are sorted by command after load (`:144-158`).

---

## Combat messages (`lib/misc/messages`) — `load_messages:115`

Randomized hit/miss/kill flavor text keyed by **attack type**. Each block:
```
M
<attack_type>
<die.attacker_msg>~
<die.victim_msg>~
<die.room_msg>~
<miss.attacker_msg>~
<miss.victim_msg>~
<miss.room_msg>~
<hit.attacker_msg>~
<hit.victim_msg>~
<hit.room_msg>~
<self.attacker_msg>~
<self.victim_msg>~
<self.room_msg>~
```
Rules:
- A block starts with the token `M`, then an integer `attack_type` (weapon/skill type
  index). Reading stops when the next token is not `M` (`:136,199`).
- **Twelve tilde-terminated strings** (via `fread_string`), in four groups of three —
  `die`, `miss`, `hit`, `self` — each group being `{attacker_msg, victim_msg, room_msg}`
  (to the attacker, the victim, and onlookers). The struct also has a `sanctuary_msg`
  group, but it is **not read from the file** (`structs.h:1982`; absent in the loader).
- **Auto-colorization** at load (`:156-198`): `$CH` (hit color) is prepended to
  `die.attacker_msg` and `hit.attacker_msg`; `$CD` (damage color) to `die.victim_msg`,
  `hit.victim_msg`, and `self.victim_msg`. Other strings are uncolored. Files should **not**
  include these codes themselves.
- **Multiple blocks may share an `attack_type`** — they accumulate into a list and one is
  chosen at random per hit (`message_list.number_of_attacks`, `:147`). `MAX_MESSAGES` caps
  the number of distinct types.

---

## Variable substitution (act-style)
Both files use DikuMUD `act()` substitution codes, expanded when the message is shown
(`comm.cpp`). Common ones:
`$n` actor name · `$N` target name · `$e/$E` he-she (actor/target) · `$m/$M` him-her ·
`$s/$S` his-her · `$o/$O` object short-name · `$p` object · `$CH/$CD` color codes.
*(Full code set lives in the `act()` implementation — to be enumerated in a comms doc.)*

## Open questions
- Exact `hide` flag semantics for socials (visibility to blind/can't-see actors).
- The full `act()` substitution table and RotS-specific additions.
- The `attack_type` → weapon/skill enumeration (belongs in the combat catalog).
- Whether `socials` files in the wild use a `$` terminator vs. relying on EOF/`-`.
