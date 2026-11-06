# Zone 17: Harness gatehouse

The zone for the `scripts` group: a yard and a passage joined by a door, the script-running mobs
and their scripts. Files: `wld/17.wld`, `mob/17.mob`, `obj/17.obj`, `scr/17.scr`, `zon/17.zon`.
Map square (2, 8); vnums 1700 to 1799 (top 1799); no resets, so scenarios `load mob` what they
need.

`python -m rots_harness.world_catalogue --zone 17` (from `tests/integration`) lists every fixture
here with its script and triggers. These notes hold what the numbers do not show.

## Rooms

Both rooms are sector 0 (`SECT_INSIDE`), level 0, without flags.

| Vnum | Name | Constant (in `test_scripts.py`) | Notes |
| --- | --- | --- | --- |
| 1701 | Gatehouse Yard | `ROOM_GATEHOUSE_YARD` | north to 1702 through a closed door (exit flags 3, `EX_ISDOOR` and `EX_CLOSED`; keyword `door`) |
| 1702 | Beyond the Gate | `ROOM_BEYOND_THE_GATE` | south to 1701 through the same door, seen from its side (keyword `gate`) |

The door between 1701 and 1702 is the test world's only door; it starts closed and no zone reset
changes it.

## Mobs

The 1700s mobs speak no language (language 0), so every listener reads their lines exactly. All
have constitution 10, 60 hit points and level 5. Only one mob that speaks when it hears speech may
be in a room: `do_say` (`act_comm.cpp`) runs every other mob's `ON_HEAR_SAY` script inside the
say, so two of them answer each other until the server's stack overflows.

| Mob | Keywords | What it is |
| --- | --- | --- |
| 1700 | `smith plain` | runs script 1700 when given an object (`ON_RECEIVE`): an IF on the star token (object 1700) whose block holds a nested guard IF with an empty else branch, then says `A star token! Here is your reward.`; otherwise `That is no star token.`; always ends with `Who is next?` |
| 1701 | `smith labelled` | runs script 1701: script 1700 with `SECTION` rows between the IF and its BEGIN, before the nested IF, and on the nested empty else branch |
| 1702 | `foreman weary` | runs script 1702 when anyone in the room speaks (`ON_HEAR_SAY`): says `Goodbye, then.`, waits four passes (`DO_WAIT`), says `My shift is over at last.` and ends |
| 1703 | `gatekeeper keeper` | given anything, opens the door north of his room (`SET_EXIT_STATE` state 0); hearing speech containing `lock`, closes and locks it (state 2) |
| 1704 | `surveyor` | given anything, points his room's up exit at 1101 (`CHANGE_EXIT_TO`; a room without that exit is reported), then says `Still here.` |
| 1705 | `courier` | given anything, runs a `TELEPORT_CHAR` that moves nobody, then `TELEPORT_CHAR_XL` to rm1, which is unset (reported), then says `Still here.` |
| 1706 | `valet` | given anything, runs `ASSIGN_EQ` on slot 22 (reported, at boot too), then says `Still here.` |
| 1707 | `dresser` | given anything, runs `DO_REMOVE` on slot 22 (reported, at boot too), then says `Still here.` |
| 1708 | `appraiser` | given anything, loads a leather cap, runs `ASSIGN_EQ` on unset ch3's head slot, and says `Phantom.` if it found an object or `Nothing.` if not, then `Still here.` |
| 1709 | `mumbler` | given anything, says `Still here.`, then skips a false IF whose nested block never reaches an END (`Never said.` inside it) |
| 1710 | `renamer` | hearing speech, runs `ASSIGN_STR` to ch1.name (refused and logged), then says `Still here.` |
| 1711 | `smiler` | hearing speech, runs `DO_SOCIAL smile` by unset ch3, then says `Still here.` |
| 1712 | `fitter` | hearing speech, runs `DO_WEAR` of ob1, which a hear trigger leaves unset, then says `Still here.` |
| 1713 | `copier` | hearing speech, runs `LOAD_OBJ_X`, which copies the unset ob1, then says `Still here.` |
| 1714 | `hatter` | given an object, wears it (`DO_WEAR`) |
| 1715 | `scribe` | given an object, copies it (`LOAD_OBJ_X`), takes the copy and gives it to the giver |
| 1716 | `hale wrestler` | given anything, writes 5000000 to his own hit points |
| 1717 | `dazed wrestler` | given anything, writes 0 to his own hit points; hearing speech, writes 50 |
| 1718 | `doomed wrestler` | given anything, writes -5000000 to his own hit points |
| 1719 | `accountant` | given anything, sets int1 to 99, divides 1000000 by 0 into int1 (`SET_INT_DIV`), and says `Quotient zero.` or `Quotient not zero.` |
| 1721 | `foreman patient` | script 1721: script 1702 with a wait of 400 passes (100 seconds) |
| 1722 | `porter` | no script, for an implementor to `force` |
| 1723 | `wanderer` | hearing speech, teleports himself to 1201 (`TELEPORT_CHAR_X`; zone 12's Distant Cell) |
| 1724 | `listener` | hearing speech, says `I heard that.` |
| 1730 | `crier` | hearing speech, says `First.`, waits four passes, says `Second.` and waits four passes again as the script's last row |
| 1731 | `clerk first` | given anything, skips a false IF whose block ends with a nested IF / BEGIN / ... / END (`Nested body.`), then has an else branch saying `Outer else.` and a last row saying `After.` |
| 1732 | `clerk second` | given anything, skips a false IF whose block holds a nested IF / BEGIN / ... / END_ELSE_BEGIN / ... / END (`Nested then.`, `Nested else.`) and a row saying `Outer tail.`, then has an else branch saying `Outer else.` and a last row saying `After.` |
| 1733 | `copyist careless` | script 1715 with 0 in `LOAD_OBJ_X`'s first parameter |

## Objects

| Vnum | Keyword | Notes |
| --- | --- | --- |
| 1700 | `token` | the star token the smiths' scripts look for |
| 1701 | `pebble` | a grey pebble, anything that is not the star token |

## Scripts

`scr/17.scr` holds scripts 1700 to 1702 and, from 1703 on, one script per mob numbered like the
mob, which the 1700s mobs run. Script 1720 belongs to no mob and never runs: each of its lines
names something missing (mob, object and room vnum 1799, which is left undefined, slot 22 and -1,
direction 6 and 7), so the boot check writes twelve `SCRIPT ERROR: script #1720, ...` lines to
every test's `game.log`, and the valet's and dresser's scripts add one line each.
