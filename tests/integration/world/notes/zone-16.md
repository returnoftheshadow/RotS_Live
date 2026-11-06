# Zone 16: Harness object workshop

The zone for the object fixtures of the `object_versions` and `identify` groups, and for the inn
where mortals rent. Files: `wld/16.wld`, `obj/16.obj`, `zon/16.zon`. Map square (6, 2); vnums
1600 to 1699 (top 1699). Reset: the harness innkeeper (mob 1105, filed in `mob/11.mob`) into the
inn, 1601.

The zone exists for the objects in `obj/16.obj`: the object editor loads `shape object <vnum>`
from `world/obj/<vnum / 100>.obj`, and `/implement` is refused for a vnum outside every zone
(`load_object`, `shapeobj.cpp`; `get_permission`, `shapemob.cpp`).

`python -m rots_harness.world_catalogue --zone 16` (from `tests/integration`) lists every fixture
here. These notes hold what the numbers do not show.

## Rooms

| Vnum | Name | Constant | Notes |
| --- | --- | --- | --- |
| 1601 | Harness Inn | `world.ROOM_INN` | sector 0, no flags, no exits; the harness innkeeper (mob 1105) stands here and rents rooms to mortals |

## Objects

`obj/16.obj` (the scenarios name each by its own constant):

| Vnum | Keyword | Notes |
| --- | --- | --- |
| 1601 | `sword` | plain sword: a weapon without the `MAGIC` flag or affects of its own, so enchant weapon takes on it (`object_takes_enchant`) |
| 1602 | `stone` | dwarven stone, extra flags `DWARF` and `STAY_ZONE` |
| 1603 | `helm` | leather helm, head armour without affects |
| 1604 | `key` | brass key, `BREAKABLE`; it locks the chest. Unrentable as a key |
| 1605 | `chest` | small chest, values `100 13 1604`: closed and locked, key 1604 |
| 1606 | `trinket` | cursed trinket whose rent per day is -2, so it is unrentable |
| 1607 | `token` | pewter token with the `NORENT` flag, so it is unrentable |
| 1608 | `waterskin` | a waterskin holding 10 of its 20 drinks of water |
| 1609 | `relic` | strange relic with every named extra flag but `INVISIBLE` and `NODROP`, and wear flags `TAKE` and `THROW` |

## The innkeeper

Mob 1105 (`innkeeper harness`) is a human whose vnum `spec_ass.cpp` assigns the receptionist
special (act flags `3` include `MOB_SPEC`). A mortal's `quit` drops everything it carries
(`do_quit`, `act_othe.cpp`); renting at the inn is how a mortal keeps its items across a login. The
innkeeper's special is `gen_receptionist` (`objsave.cpp`), which answers `rent` and `offer` typed
in its room:

- Rent costs nothing: `Crash_offer_rent` and `gen_receptionist` set the cost to 0, so a mortal
  needs no gold. `offer` still quotes a price for the first day, from the level and what the
  character carries, then says `You have enough gold for a lifetime of rent.` whatever the gold.
  A character carrying nothing gets `But you are not carrying anything!  Just quit!`.
- Before quoting or storing, `rent` and `offer` refuse each item the character carries or wears,
  containers' contents included, that `Crash_is_unrentable` (`objsave.cpp`) names: a key, an item
  with `NORENT`, or one whose rent per day is below -1 (fixtures 1604, 1607 and 1606). Each gets
  `The harness innkeeper tells you, 'You cannot store <item>.'` and the rent goes no further.
- `rent` replies `The harness innkeeper stores your belongings and helps you into your private
  chamber.`, saves what the character carries and wears (`Crash_rentsave`), and returns the
  connection to the character menu (`Make your choice:`) with the socket still open. Close that
  session with `close()`, then `server.login(name)`: the mortal returns to the inn with its
  items (`calc_load_room`). `rent_and_log_in_again` in `scenarios/test_object_versions.py` does
  this.
- Every other command typed in the inn draws `number(0, 30)`, and on 0 the innkeeper performs a
  random social, so text can arrive there unasked. Bring the mortal in with the implementor's
  `teleport <name> 1601` (it reads `<Implementor> has teleported you!` and the room) just before
  `rent` or `offer`.
- A `yell` anywhere in zone 16 outside the inn would log `SYSERR: Fubar'd receptionist.`, which
  fails the test: `do_gen_com` (`act_comm.cpp`) calls the specials of every room in the zone, and
  `gen_receptionist` looks for the innkeeper in the yeller's room.
