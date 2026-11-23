# Zone 25: Harness trigger hall

The zone for the `script_triggers` group: a hall, a chamber north of it and a closet behind a
hatch, with one mob or object for each script trigger players set off. Files: `wld/25.wld`,
`mob/25.mob`, `obj/25.obj`, `scr/25.scr`, `zon/25.zon`. Map square (21, 21); vnums 2500 to 2599
(top 2599); no resets, so each scenario places what it needs with `world_resets` or `load`.

`python -m rots_harness.world_catalogue --zone 25` (from `tests/integration`) lists every fixture
here with its script and triggers. These notes hold what the numbers do not show.

## Rooms

Every room is sector 0 (`SECT_INSIDE`), level 0, without flags.

| Vnum | Name | Constant (in `test_script_triggers.py`) | Notes |
| --- | --- | --- | --- |
| 2501 | Trigger Hall | `ROOM_TRIGGER_HALL` | every mortal's load room; north to 2502 through an open arch; east to 2503 through a closed hatch (exit flags 3, keyword `hatch`) that the levers open |
| 2502 | Trigger Chamber | `ROOM_TRIGGER_CHAMBER` | south to 2501; where the greeter, the bell, the lantern and the warden wait for someone walking in |
| 2503 | Lever Closet | | west to 2501 through the same hatch |

- The hatch starts closed and nothing resets it, so a hatch a scenario opens stays open for the
  rest of that server's run.
- Zone 25's map x of 21 is east of the river (8), so a good-race killer's share of a kill made
  here gets the east bonus in `exp_with_modifiers` (`fight.cpp`). The mourner's pinned share
  includes it; moving the zone's square changes that number.

## Mobs

Each mob but the mule runs the script of its own number. All are level 5 and constitution 10,
and all but the mule speak language 0, so players read their lines exactly; the sentinel flag
keeps them in place.

| Mob | Keywords | What it is |
| --- | --- | --- |
| 2500 | `greeter` | `ON_ENTER`: says `Welcome, <name>.`, naming whoever walked in (ch2) |
| 2501 | `warden` | `ON_BEFORE_ENTER`: sends anyone below level 50 about to come in `The warden bars your way, <name>.` and returns false; lets the rest in |
| 2502 | `echoer` | `ON_HEAR_YELL` only: says `I hear you, <name>.`. No caller sets off `ON_HEAR_YELL` itself: `trigger_char_hear` (`script.cpp`) runs a mob's yell script after its say script whenever it hears speech, and `yell` runs no script at all |
| 2503 | `stoic` | `ON_DAMAGE`: says `Your blow cannot touch me, <name>.` and returns false; 200 hit points |
| 2504 | `mourner` | `ON_DIE`: says `My watch is ended.` and lets the death go ahead; one hit point and 10000 experience, so a level-10 caster's share is 125 |
| 2505 | `stubborn` | `ON_DIE`: says `Not yet!`, sets his hit points to 30 and returns false; one hit point |
| 2506 | `vanisher` | `ON_HEAR_SAY`: sends the room `The vanisher fades away.` and removes himself. He never speaks, so he can share a room with a speaking listener |
| 2507 | `answerer` | `ON_HEAR_SAY`: says `I heard that.` |
| 2508 | `parrot` | `ON_HEAR_SAY`: says `Let me think about that.`, waits 20 passes (five seconds), then says `You said: <str1>`, the words it heard |
| 2509 | `mule harness` | no script: a `MOB_MOUNT` (act flags 514, with `SENTINEL`) with 50 moves, language 1 |

- Only one mob that speaks when it hears speech may be in a room: `do_say` (`act_comm.cpp`) runs
  every other mob's hear scripts inside the say, so two of them answer each other until the
  nesting limit refuses the runs with a `SYSERR`. The answerer, the parrot and the echoer each
  speak; the vanisher does not.
- A mob whose script removes itself after a `DO_WAIT` logs a `SYSERR` today when it was the last
  waiter (the waiting list's known report), so no script here removes its mob after a wait.

## Objects

Each object's script is its vnum plus 50. ch1 is the character who set it off, ob1 the object.

| Vnum | Keywords | Type | Notes |
| --- | --- | --- | --- |
| 2500 | `bell small` | other, not takeable | `ON_ENTER`: sends `A small bell chimes for <name>.` |
| 2501 | `lantern paper` | other, not takeable | `ON_ENTER`: sends `The paper lantern crumbles to dust.`, then removes itself (`EXTRACT_OBJ ob1`) |
| 2503 | `diadem thorned` | armour, head | `ON_WEAR`: sends `The thorned diadem will not sit on your head.` and returns false |
| 2504 | `coronet silver` | armour, head | `ON_WEAR`: sends `The silver coronet settles on your brow.` |
| 2505 | `lever iron` | lever, not takeable | values 2501 and 1: opens and shuts the hall's east hatch. `ON_PULL`: sends `The iron lever creaks.` |
| 2506 | `ledger` | other | action description `Columns of small figures fill the ledger.`; `ON_EXAMINE_OBJECT`: sends `The pages of the ledger rustle.` |
| 2507 | `cake honey` | food, fills 2 | `ON_EAT`: sends `The honey cake is sweet.` and returns false, which `do_eat` ignores |
| 2508 | `water flask` | drink container, capacity 5, one drink of water | `ON_DRINK`: sends `The water in the flask is cold.` and returns false, which `do_drink` ignores. Emptied, it loses its first keyword (`name_from_drinkcon`, `act_obj2.cpp`), so `flask` still finds it |
| 2509 | `biscuit dry` | food, fills 2 | `ON_EAT`: sends `The dry biscuit crumbles away.`, then removes itself |
| 2510 | `ribbon black` | other | no script: something for the mourner to carry into his corpse |
| 2511 | `crank rusted` | lever, not takeable | the same hatch as the lever. `ON_PULL`: sends `The rusted crank will not move.` and returns false |

- `obj_to_room` puts each new object at the head of a room's contents, while `char_to_room`
  (`handler.cpp`) adds each newcomer at the end of a room's people. A scenario that needs one
  fixture's script to run before another's orders its `world_resets` with that in mind.
