# Zone 18: Harness boar pen

The zone for the `tame` group: a peaceful pen whose zone resets one boar, so a tame's zone slot
can be counted, and a hub with six dead-end spokes for the whistle's flee and recall. Files:
`wld/18.wld`, `zon/18.zon`. Map square (9, 9); vnums 1800 to 1809 (top 1809). Reset: the pen boar
(mob 1450) into the pen, 1801, max existing 1. The boar and the other animals tame refuses or
accepts are in `mob/14.mob`, whose notes are in `shared.md`.

`python -m rots_harness.world_catalogue --zone 18` (from `tests/integration`) lists the rooms.
These notes hold what the numbers do not show.

## Rooms

Every room is sector 0 (`SECT_INSIDE`), level 0, and without the `INDOORS` flag.

| Vnum | Name | Constant (in `test_tame.py` unless `rots_harness/world.py` is named) | Notes |
| --- | --- | --- | --- |
| 1801 | Boar Pen | `world.ROOM_BOAR_PEN` | `PEACEROOM`; the zone resets one pen boar (mob 1450) here; east to 1802 |
| 1802 | Pen Gate | `ROOM_PEN_GATE` | east of the pen, with a one-way exit east to the whistle hub; west to 1801 |
| 1803 | Whistle Hub | `ROOM_WHISTLE_HUB` | six exits to the spokes 1804 to 1809, and no exit back to the pen gate |
| 1804 | North Spoke | `ROOM_NORTH_SPOKE` | the first of the hub's six spokes; a dead end back to the hub |
| 1805 | East Spoke | `ROOM_EAST_SPOKE` | a dead end back to the hub |
| 1806 | South Spoke | | a dead end back to the hub |
| 1807 | West Spoke | | a dead end back to the hub |
| 1808 | Upper Spoke | | a dead end back to the hub |
| 1809 | Lower Spoke | `ROOM_LOWER_SPOKE` | the last of the six spokes; a dead end back to the hub |

- 1801 is the zone's only room with a flag. There `PEACEROOM` makes `do_hit` (`act_offe.cpp`)
  refuse every attack, a failed tame's included.
- Every exit of the whistle hub (1803) leads to a spoke, so `do_flee` (`act_offe.cpp`) finds an
  exit on its first draw there, and every spoke comes after the hub in the world, which
  `do_whistle`'s walk over the zone's rooms (`ranger.cpp`) follows.
