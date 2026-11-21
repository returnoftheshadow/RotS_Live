# Zone 21: Harness sick ward

The zone for the `idle_removal`, `affect_ticks` and `wands` groups: an idle hall where a
link-dead player is left to be idle-removed, a sick ward where poison ticks, a wand room, and
the gear and wands those scenarios use. Files: `wld/21.wld`, `obj/21.obj`, `zon/21.zon`. Map
square (15, 18); vnums 2100 to 2199 (top 2199); no resets, so scenarios `load obj` and
`load mob` what they use.

The zone has no mob file; the poison-tick scenario uses zone 14's straw scarecrow (1454).

`python -m rots_harness.world_catalogue --zone 21` (from `tests/integration`) lists every fixture
here. These notes hold what the numbers do not show.

## Rooms

Every room is sector 0 (`SECT_INSIDE`), level 0, without the `INDOORS` flag, and without exits.

| Vnum | Name | Constant | Notes |
| --- | --- | --- | --- |
| 2100 | Idle Hall | `ROOM_IDLE_HALL` in `test_idle_removal.py` | the idler's load room; idle removal brings the idler back here from the void (1102) before its gear comes off |
| 2101 | Sick Ward | `ROOM_SICK_WARD` in `test_affect_ticks.py` | where poison ticks |
| 2102 | Wand Room | `ROOM_WAND_ROOM` in `test_wands.py` | where wands and a staff are used |

## Objects

`obj/21.obj` (the scenarios name each by its own constant):

| Vnum | Keyword | Notes |
| --- | --- | --- |
| 2100 | `sash` | draining grey sash, worn about the body (slot 12), `A 13 -72`: maximum hit points -72. A level-10 human warrior with constitution 0 has 74, so the sash leaves 2; the straw scarecrow's 20 become -52 |
| 2101 | `wand` | birch wand of insight (`ITEM_WAND`), 2 of 2 charges of spell 50, insight; `TAKE` and `HOLD` |
| 2102 | `wand` | oak wand of enchanting, 1 of 1 charge of spell 60, enchant weapon |
| 2103 | `wand` | ivory wand of sanctuary, 1 of 1 charge of spell 56, sanctuary |
| 2104 | `staff` | ash staff of insight (`ITEM_STAFF`), 0 of 1 charges of spell 50 |

- The sash drives its wearer's maximum hit points to a clamp: `fast_update` (`limits.cpp`)
  keeps current hit points at or below the maximum on every pass, so no regeneration can lift
  them above it. Its `APPLY_HIT` (`affect_modify`, `handler.cpp`) lowers the current hit points
  with the maximum only when the wearer is at full hit points; otherwise the next pass does it.
- The wands and the staff answer to `wand` and `staff`; use their second keywords (`birch`,
  `oak`, `ivory`, `ash`) when a character holds more than one.
- `do_use` (`act_othe.cpp`) draws nothing: a wand or staff with a charge left casts its spell,
  one without says `The wand seems powerless.` or `The staff seems powerless.`
- The staff has no charge on purpose. Tapped with one, it would cast insight at nobody, which
  logs a `SYSERR` (the spell refuses a missing victim) that fails any scenario.
