# Zone 23: Harness stables

The zone for the riding pins of the `followers` group: a stable yard, a paddock east of it and a
sinkhole east of the paddock, and a cob that can carry a rider. Files: `wld/23.wld`,
`mob/23.mob`, `zon/23.zon`. Map square (21, 15); vnums 2300 to 2399 (top 2399); no resets, so
scenarios place the cob with their `world_resets` marker.

`python -m rots_harness.world_catalogue --zone 23` (from `tests/integration`) lists every fixture
here. These notes hold what the numbers do not show.

## Rooms

Every room is sector 0 (`SECT_INSIDE`), level 0, and without the `INDOORS` or `NORIDE` flag, so a
mount may enter each one.

| Vnum | Name | Constant (in `test_followers.py`) | Notes |
| --- | --- | --- | --- |
| 2301 | Stable Yard | `ROOM_STABLE_YARD` | east to 2302 |
| 2302 | Paddock | `ROOM_PADDOCK` | west to 2301, east to 2303 |
| 2303 | Sinkhole | `ROOM_SINKHOLE` | `DEATH`, no exits: whoever enters dies there (`do_move` and `perform_move_mount`, `act_move.cpp`), a rider and its mount alike |

## Mobs

| Vnum | Keywords | Notes |
| --- | --- | --- |
| 2301 | `cob stable` | a stable cob: the harness pony (1161, `zone-11.md`) with 100 moves (the third number of its `0 0 100 1` line, `load_mobiles` in `db.cpp`), so it can take the steps a rider asks. Act flags `514` (`SENTINEL`, `MOB_MOUNT`). A mob's maximum moves come from its file alone: `wizset mob ... maxmove` changes a constant that only a player's maximum is rebuilt from (`recalc_abilities`, `profs.cpp`), so the pony cannot be given moves in game |
