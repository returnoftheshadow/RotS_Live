# Zone 19: Harness key hall

The zone for the `keyring` group: a hall and a store room joined by a locked door, and the
keyring fixtures that open it. Files: `wld/19.wld`, `obj/19.obj`, `zon/19.zon`. Map square
(9, 5); vnums 1900 to 1999 (top 1999); no resets, so scenarios `load obj` and `load mob` what
they use.

`python -m rots_harness.world_catalogue --zone 19` (from `tests/integration`) lists every fixture
here. These notes hold what the numbers do not show.

## Rooms

Every room is sector 0 (`SECT_INSIDE`), level 0, and without the `INDOORS` flag.

| Vnum | Name | Constant (in `test_keyring.py`) | Notes |
| --- | --- | --- | --- |
| 1901 | Key Hall | `ROOM_KEY_HALL` | the mortal's load room; east door (keyword `door`, flags 7: door, closed, locked) needs key 1900 |
| 1902 | Key Store | | behind the same door, seen from its west side |

- The door is locked at boot by its exit flags alone; the zone has no `D` reset, so a door a
  scenario unlocks stays unlocked for the rest of that server's run.

## Objects

`obj/19.obj` (the scenarios name each by its own constant):

| Vnum | Keyword | Notes |
| --- | --- | --- |
| 1900 | `key` | iron harness key, `TAKE` and `HOLD`; not `BREAKABLE`, so `check_break_key` (`act_move.cpp`) returns before any roll. Unrentable as a key |
| 1901 | `keyring` | brass keyring (`ITEM_KEYRING`, 26), capacity 500; wear flags `TAKE`, `HOLD` and `BELT` |
| 1902 | `belt` | leather belt, worn about the waist; gives the belt hooks a keyring hangs on |

- The rent pin loads the harness innkeeper (mob 1105, `mob/11.mob`) into the Key Hall instead of
  using the inn (1601), so no command is typed in a room where the innkeeper's random social
  can fire before `rent`.
