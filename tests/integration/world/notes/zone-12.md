# Zone 12: Harness distant zone

A second zone far from the arena on the world map, for summon's zone-distance scenario: its map
square (3, 4) is five squares from zone 11's (0, 0). Files: `wld/12.wld`, `zon/12.zon`. Vnums
1200 to 1299 (top 1299); no resets.

`python -m rots_harness.world_catalogue --zone 12` (from `tests/integration`) lists it.

## Rooms

| Vnum | Name | Constant | Notes |
| --- | --- | --- | --- |
| 1201 | Distant Cell | `ROOM_DISTANT_CELL` in `test_summon.py` | the only room of zone 12; sector 0, no flags, no exits. The wanderer (mob 1723, `zone-17.md`) teleports himself here |
