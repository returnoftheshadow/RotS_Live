# Zone 22: Harness order hall

The zone for the `orders` and `corpse_gear` groups: one hall where a leader orders its charmed
followers, the followers themselves, two script mobs that remove someone as an order runs, and a
mob that any blow or poison tick kills. Files:
`wld/22.wld`, `mob/22.mob`, `scr/22.scr`, `zon/22.zon`. Map square (18, 15); vnums 2200 to 2299
(top 2299); no resets, so scenarios `load mob` what they need.

`python -m rots_harness.world_catalogue --zone 22` (from `tests/integration`) lists every fixture
here with its script and triggers. These notes hold what the numbers do not show.

## Rooms

| Vnum | Name | Constant | Notes |
| --- | --- | --- | --- |
| 2201 | Order Hall | `ROOM_ORDER_HALL` (in `rots_harness/world.py`) | sector 0 (`SECT_INSIDE`), level 0, no flags (so not `PEACEROOM`) and no exits; the leader's and the mage's load room |

## Mobs

All are level 5 with 60 hit points unless the table says otherwise, `SENTINEL`, male, with
intelligence 10 (`do_say`, `act_comm.cpp`, refuses a mob below 6) and language 0, so every
listener reads their words exactly. `mob/22.mob` sorts between `mob/guildmaster-uruk.mob` (2043)
and `mob/guildmasters.mob` (3888 on) so that mob vnums ascend (`shared.md`).

| Mob | Keywords | What it is |
| --- | --- | --- |
| 2200 | `warden` | the order warden: script 2200 removes from the game (`EXTRACT_CHAR`) any level-7 speaker he hears; he never speaks, so he may share a room with a mob that does |
| 2201 | `captain` | the lackey captain: script 2201 removes himself from the game when he hears speech containing `farewell`; he never speaks |
| 2202 | `ash lackey` | charmed from the start: his affect bits are `AFF_CHARM` (32), so whoever he follows can order him (`do_order`, `act_offe.cpp`); no script |
| 2203 | `elm lackey` | the same as the ash lackey |
| 2204 | `yew lackey` | the same, but level 7, the level the order warden removes |
| 2205 | `taster frail` | the frail taster, level 1: one hit point and constitution 2, so poison's 5 damage leaves him at -4, past the -1 at which `update_pos` (`fight.cpp`) makes a mob dead, and any player's blow kills him; no affects, resistances or script |

- The lackeys' charm stands in for a tame or a charm spell: `do_order` reads only the
  `AFF_CHARM` bit. `stop_follower` (`handler.cpp`) clears that bit, so a lackey that stops
  following cannot be ordered again until it is loaded afresh.
- A charmed follower that stops following reads its leader as a jerk: the leader reads
  `<follower> hates your guts!` and the rest of the room `<follower> realizes that <leader> is a
  jerk!` (`stop_follower`).

## Scripts

`scr/22.scr` holds one script per script mob, numbered like the mob. Script 2200 compares the
speaker's level (`CH2_LEVEL`) with 7 before removing; script 2201 tests the speech with
`IF_STR_CONTAINS`, whose text is upper case because the script upper-cases the speech first.
