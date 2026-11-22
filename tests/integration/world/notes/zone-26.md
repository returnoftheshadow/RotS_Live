# Zone 26: Harness dressing hall

The zone for the `zone_dressing` and `script_limits` groups: a hall where the zone's reset dresses
a squire through `M`, `E`, `K`, `G` and `P` rows, and two rooms of their own for mobs whose speech
scripts answer each other. Files: `wld/26.wld`, `mob/26.mob`, `obj/26.obj`, `scr/26.scr`,
`zon/26.zon`. Map square (15, 21); vnums 2600 to 2699 (top 2699).

`python -m rots_harness.world_catalogue --zone 26` (from `tests/integration`) lists every fixture
here with its script, triggers and resets. These notes hold what the numbers do not show.

## Rooms

Every room is sector 0 (`SECT_INSIDE`), level 0, without flags and without exits; scenarios reach
them with `goto` or a character's `load_room`.

| Vnum | Name | Constant | Notes |
| --- | --- | --- | --- |
| 2601 | Dressing Hall | `ROOM_DRESSING_HALL` (in `test_zone_dressing.py`) | the squire's reset room |
| 2602 | Echo Room | `ROOM_ECHO` (in `test_script_limits.py`) | for the echo and the mirror, placed by the scenario's `world_resets` |
| 2603 | Chanting Room | `ROOM_CHANTING` (in `test_script_limits.py`) | for three chanters, placed the same way |

- 2602 and 2603 break the world's rule of one speaking hear-script mob per room on purpose, and
  are rooms of their own so that nothing else is in reach of what is said there. Their mobs
  answer each other until the script limits stop them (`MAX_SCRIPT_NESTING` and
  `MAX_SCRIPT_RUNS_PER_EVENT`, `script.h`), which log a `SYSERR` per event and limit. Never
  place these mobs, or speak, in a room a scenario of another group uses.

## Resets

The rows dress the squire in this order; every one but the `M` row has if-flag 2, so it runs
only after the squire loaded.

| Row | What it does |
| --- | --- |
| `M 128 2600 2601 ...` | loads the squire into the Dressing Hall, at most one alive. If-flag 128 runs it only while a player is in zone 26, so the boot never loads him and no other scenario sees the reset's `SYSERR`; `zreset 26` loads him once the implementor stands in the hall |
| `E 2 2610 6 ...` | the iron sallet, worn on his head (slot 6) |
| `E 2 2611 6 ...` | the spare bascinet, for the head slot the sallet already fills: no bascinet is made, and `SYSERR: Char is already equipped: a dressed squire, a spare bascinet` is logged (`reset_zone`, `zone.cpp`) |
| `P 2 -1 2612 -1 ...` | a grey flint into the last object loaded, which the refused row left empty, so no flint is made |
| `K 2 2613 2614 2615 ...` | gives the sabatons, the tabard and the whetstone, then makes him `wear all`: the room reads him put on the tabard and the sabatons; the whetstone stays carried |
| `G 2 2616 ...` | gives the leather satchel |
| `P 2 -1 2617 -1 ...` | an acorn into the last object loaded, the satchel |

A scenario that resets zone 26 must name that `SYSERR` with `@pytest.mark.expect_syserr`.

## Mobs

All are level 5 with 60 hit points, `SENTINEL`, male, with intelligence 10 (`do_say`,
`act_comm.cpp`, refuses a mob below 6) and language 0, so every listener reads their words
exactly.

| Mob | Keywords | What it is |
| --- | --- | --- |
| 2600 | `squire dressed` | the squire the reset dresses; no script |
| 2601 | `echo` | runs script 2600 |
| 2602 | `mirror` | runs script 2600; with the echo, two mobs that answer each other |
| 2603 | `chanter` | runs script 2600; three of them answer each other with branching runs |

## Objects

`obj/26.obj`, each loaded only by the squire's reset rows. The vnums start at 2610 so that none is
also a mob's, since the implementor's `where <vnum>` lists both.

| Vnum | Keywords | Notes |
| --- | --- | --- |
| 2610 | `sallet iron` | armour worn on the head (`TAKE`, `HEAD`) |
| 2611 | `bascinet spare` | the same, for the refused `E` row |
| 2612 | `flint grey` | `ITEM_OTHER`, for the `P` row after the refused one |
| 2613 | `sabatons steel` | armour worn on the feet |
| 2614 | `tabard blue` | armour worn about the body |
| 2615 | `whetstone` | `ITEM_OTHER`, which nothing wears |
| 2616 | `satchel leather` | a container, capacity 3000 |
| 2617 | `acorn` | `ITEM_OTHER`, for the satchel |

## Scripts

`scr/26.scr` holds script 2600, `Harness echo`: on `ON_HEAR_SAY` it says `Hi.` (`DO_SAY` as ch1,
the mob itself), whoever spoke and whatever was said.
