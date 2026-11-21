# Zone 20: Harness tower guards

The mobs of the `vampire_tower` group, whose rooms are zone 153's (`zone-153.md`). Files:
`mob/20.mob`, `zon/20.zon`. Map square (15, 15); vnums 2000 to 2099 (top 2099); no rooms and no
resets, so scenarios place the mobs with `world_resets` or `load mob`.

`python -m rots_harness.world_catalogue --zone 20` (from `tests/integration`) lists the mobs.
These notes hold what the numbers do not show.

- The mobs are here and not in a `mob/153.mob`: mob vnums ascend from file to file in index
  order, and `mob/guildmasters.mob` already runs from 2043 to 32200, so no file can hold 15300
  to 15399. A zone-20 mob still fits before `guildmasters.mob`.
- `spec_ass.cpp` binds 2012, 2041 and 2043 to specials by vnum; a new mob here avoids them.
- The catalogue's zone 20 also lists the uruk guildmaster, 2043, which lives in
  `mob/guildmasters.mob` (`shared.md`) but falls in zone 20's vnums.

## Mobs

| Mob | Keywords | What it is |
| --- | --- | --- |
| 2000 | `doorwarden` | act flags `SPEC` and `SENTINEL`, program 22: `vampire_doorkeep` (`spec_pro.cpp`). It ignores the command it is offered and reads rooms 15345 and 15355 and object 7008 by their fixed vnums, so it runs on every command typed in its room and when anyone walks in. The scenarios place it in 15345 |
