# Zone 153: Harness vampire tower

The rooms of the `vampire_tower` group: the two rooms whose door `vampire_doorkeep`
(`spec_pro.cpp`) keeps, at the fixed vnums that special reads. Files: `wld/153.wld`,
`zon/153.zon`. Map square (18, 18); vnums 15300 to 15399 (top 15399); no resets. The doorwarden
that runs the special is mob 2000 (`zone-20.md`).

`python -m rots_harness.world_catalogue --zone 153` (from `tests/integration`) lists the rooms.
These notes hold what the numbers do not show.

## Rooms

Both rooms are sector 0 (`SECT_INSIDE`), level 0, without flags.

| Vnum | Name | Constant (in `test_vampire_tower.py`) | Notes |
| --- | --- | --- | --- |
| 15345 | Before the Tower Door | `ROOM_BEFORE_THE_DOOR` | south to 15355 through a door (exit flags 1, `EX_ISDOOR`, open at boot; keyword `door`) |
| 15355 | Inside the Tower Door | `ROOM_INSIDE_THE_DOOR` | north to 15345 through the same door, seen from its side (keyword `gate`) |

- The door starts open and no reset changes it, so a closed door stays closed for the rest of
  that server's run.
- `vampire_doorkeep` opens the door only for a player wearing object 7008 as a light. The test
  world has no object 7008, which would need a zone 70 of its own, so the scenarios pin only
  the closing.
- For each player in 15355, `vampire_doorkeep` draws `number(0, 100)` and on 0 shows a wolf's
  charge to the others in 15355. A scenario that reads text in 15355 keeps one player there.
