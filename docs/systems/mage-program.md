# Mage program (program 31)

Program 31, `mob_magic_user_spec`, makes a mob a caster: in a fight it casts at its foes from
spell lists picked by its mage types and its health; out of a fight it heals itself. Code:
`src/spec_pro.cpp:1641-1950`; its settings live in the mob options and are read by
`src/mob_progs/mage_prog.cpp`. Builder help: `MAN SHAPE MAGE` (`lib/text/shap_tbl`). Design
notes: issue #243 and PRs #217, #234, #244, #248.

The old caster programs are program 3 (`mob_cleric`, `src/spec_pro.cpp:1576`) and program 4
(`mob_magic_user`, `:1610`). They pick spells their own way; all three share the target picker
(`choose_caster_target`, `:1540`).

## When it runs

Only on the mob's own turn (`SPECIAL_SELF`, from `one_mobile_activity`, `src/mobact.cpp`), and
not while a command is pending (`delay.wait_value` with `delay.cmd`). The cast roll comes first:
the mob goes on only if `number(1, 100)` is at most its level + 20 (always from level 80). A
stunned (or worse) mob does nothing, and neither does one still waiting (`AFF_WAITING` with no
command, as after an attack, a rescue or a sneak step): `do_cast` would cast at once, with no
casting time and no chance to interrupt, so it casts on a later turn.

It returns 1 ("turn handled") only when a cast really started (`delay.cmd == CMD_CAST` with
`AFF_WAITING`). A turn with no spell to cast, or a cast `do_cast` refused on the spot (no mana, a
peace room, the wrong position), returns 0, and the standard mob turn runs on: aggression,
helping, wandering, and the interrupt reset after a fight.

## Settings (`/42`)

One per line, `key=yes|no`, all optional, default no. Parsed once per prototype at boot and on
`/implement`, and kept by mob rnum (`mage_config_for`); running mobs pick up a change at
`/implement` or the next boot. The settings follow the program a mob runs (its type's function,
else its own number), so copies already loaded follow a switch to program 31 at `/implement`. When `/implement` leaves
the type with no program (`/29 0`, or the spec flag cleared), copies already loaded keep running
program 31 until they reset, and read the type's settings still. A mob without the spec flag (or
any mob on a `-s` boot) gets no settings. `/save` only checks the
lines and reports problems; it checks a mob just switched to program 31 by `/29` before
`/implement` too (`mob_program_candidate`, `src/mob_options.cpp`). General settings (walkers)
are passed over.

The six types pick the combat spell lists; several types add their lists together (no spell
twice). The old alias keywords (`fmage`, `lmage`, `cmage`, `dmage`, `lumage`, `defaultm`,
`conj`, `shield`, `terror`) are no longer read.

| Setting | Old keyword | 76%+ health | 51-75% | 26-50% | below 26% | From a level |
|---|---|---|---|---|---|---|
| `fire=yes` | `fmage` | firebolt | firebolt | chill ray, firebolt | chill ray, magic missile | fireball (40) |
| `lightning=yes` | `lmage` | lightning bolt | lightning bolt | chill ray, lightning bolt | chill ray, magic missile | lightning strike (35), outdoors with `SKY_LIGHTNING` |
| `cold=yes` | `cmage` | chill ray | chill ray, lightning bolt | chill ray | chill ray, magic missile | cone of cold (30) |
| `dark=yes` | `dmage` | dark bolt | dark bolt | chill ray, dark bolt | chill ray, magic missile | searing darkness (40) |
| `lhuth=yes` | `lumage` | word of agony, black arrow | word of agony, black arrow | dark bolt, black arrow | leach, dark bolt | spear of darkness (40), not under `SUN_PENALTY` |
| `mixed=yes` | `defaultm` | firebolt, lightning bolt | firebolt, lightning bolt | chill ray, lightning bolt | chill ray, magic missile | earthquake (30) |

Below 0 hit points, or with a maximum of 0, the lowest band applies. Level spells join the list
whatever the health.

| Setting | Old keyword | Effect |
|---|---|---|
| `conjurer=yes` | `conj` | Out of a fight: regeneration if not on it, else curing if not on it. In a fight (below the interrupt limit), a 1 in 4 chance of confuse on the one it fights if that one is not confused (confuse can only target the one fought, `TAR_FIGHT_VICT`), else poison on the picked target if not poisoned (never with `lhuth=yes`). |
| `shield=yes` | `shield` | At the interrupt limit, with no shield, the mana shield costs it (`USE_MANA`; a low-level mob needs more) and a 50% roll: everyone fighting it stops, it stops, and it casts shield. No message, on purpose (issue #243: a client trigger would make it pointless). |
| `terror=yes` | `terror` | In a fight below 26% health, a 20% chance of terror (hits everyone else in the room). Not stopped by the interrupt limit. |

Problems are reported in the house style on `/save` and `/implement` (and logged at boot):
`unknown setting - line ignored`, `duplicate <key> - line ignored` (the first good line stands),
`bad <key> - line ignored` (a later line with a good value still counts). With /29 set to 31 but
the program not running (SPEC off, or a special on the vnum), one line says why and the lines are
checked as if it ran (`report_blocked_program`, see `ranger-program.md`). `/implement` also warns,
to the builder only, when no type is set:
`MOB WARNING: mobile #<vnum>: no mage type set, no type spells` (conjurer, shield and terror still
work without a type).

A mob with `MOB_SHADOW` (WRAITH in the builder's flag list, bit 17) can cast only the shadow
spells (`can_cast_spell`, `src/spell_pa.cpp`), so none of these; the help says not to give it
program 31. With `shield=yes` its break-off would stop the fight and the shield then be refused
(left as is: a building mistake).

The keywords `debug` and `spells` in the mob's name list still turn on immortal log lines
(`mudlog_debug_mob`, `mudlog_aliased_mob`); they change no behaviour.

## The turn

In order; the first that gives a spell is cast:

1. Out of a fight, `conjurer=yes`: regeneration, then curing.
2. Out of a fight, at 90% health or less: cure self (with shield up, only at half mana or more).
3. In a fight at the interrupt limit, `shield=yes`: the shield break-off.
4. In a fight below 26%, `terror=yes`: terror.
5. In a fight below the interrupt limit: `conjurer=yes` confuse or poison, else a spell from the
   type lists, at a target from the picker.

The interrupt count. Each interrupted cast adds one (`do_cast`, `src/spell_pa.cpp:596`), up to 3.
In a fight, one comes off every 10 of the mob's turns (`src/mobact.cpp:194`); a turn out of a
fight that reaches the standard turn clears it. At 3 the mob casts nothing at its foes (steps 3
and 4 still apply) and fights with blows.

The target picker (`choose_caster_target`). The one the mob is fighting is always a valid
target, as in melee (blows land on an unseen opponent too). Anyone else engaged with it (fighting
it, or a player whose group member fights it, or is fought by it) is a valid target only if the
mob can see them (`CAN_SEE`: hidden, invisible, dark). One valid target is chosen at random.

## Live mobs

On 2026-10-15 two live mobs ran program 31: 18000 "the young uruk mage" (its keyword `dgmage`
was a typo for `dmage`, fixed on the test server) and 26223 "a horrid forgotten wight" (`dmage
lumage conj defaultm`). 16899 Brilithor is set to 31, but `spec_ass.cpp` assigns it `guild`,
which wins. Their settings move to `/42` with this change; deploy the code before the world
files.

## Tests

`src/tests/mage_prog_tests.cpp`: the option parser and warnings, the cast roll, the health
bands, level spells, weather and sun gates, conjurer, lhuth, the interrupt limit, the shield
break-off, refused casts, and the target picker for programs 31 and 4.
