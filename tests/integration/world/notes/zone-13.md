# Zone 13: Harness market

Banker and barter vendor fixtures for the `bank` and `vendor` groups. Files: `wld/13.wld`,
`mob/13.mob`, `obj/13.obj`, `zon/13.zon`. Map square (6, 6); vnums 1300 to 1399 (top 1399).

`python -m rots_harness.world_catalogue --zone 13` (from `tests/integration`) lists every fixture
here, with the keepers' options and the zone's resets. These notes hold what the numbers do not
show.

## Rooms

Every room is sector 0 (`SECT_INSIDE`), level 0, and without the `INDOORS` flag.

| Vnum | Name | Constant (in `rots_harness/world.py` unless a scenario file is named) | Notes |
| --- | --- | --- | --- |
| 1301 | Bank Hall | `ROOM_BANK_HALL` in `test_bank.py` | lit; the zone resets the banker (mob 1301) here; east to 1302 |
| 1302 | Trade Hall | `world.ROOM_TRADE_HALL` | lit; the zone resets the trader (mob 1302) here; west to 1301 |
| 1303 | Store Room | `ROOM_STORE_ROOM` in `test_vendor.py` | the trader's stock, with no exits; the zone stocks one leather cap (object 1137) |
| 1304 | Dark Vault Hall | `ROOM_DARK_VAULT_HALL` in `test_bank.py` | `DARK`; the zone resets the dark banker (mob 1304) here |
| 1305 | Morning Bank | `ROOM_MORNING_BANK` in `test_bank.py` | the zone resets the morning banker (mob 1305) here, open hours 0 to 11 |
| 1306 | Evening Bank | `ROOM_EVENING_BANK` in `test_bank.py` | the zone resets the evening banker (mob 1306) here, open hours 12 to 23 |
| 1307 | Odd Trade Hall | `ROOM_ODD_TRADE_HALL` in `test_vendor.py` | the zone resets the odd trader (mob 1307) here |
| 1308 | Odd Store Room | none | the odd trader's stock, with no exits; the zone stocks a leather cap (object 1137), an iron cap (object 1304) and a lead ingot (object 1305) |
| 1309 | Broken Market | `world.ROOM_BROKEN_MARKET` | the zone resets the broken banker (mob 1308) and the broken trader (mob 1309) here, each disabled by its options |

1304 is the zone's only room with a flag. `IS_DARK` is true there unless something gives light,
so a character there sees only with infravision, holylight, or moonvision while the moon is up
(the room counts as outside, and moonlight follows the uncontrolled game hour).

## Mobs

Zone 13's keepers speak no language (the first number of each one's last numeric line is 0;
`load_mobiles`, `db.cpp`), so a keeper's `say` reaches every listener unchanged.

| Mob | Keywords | What it is |
| --- | --- | --- |
| 1301 | `banker` | mob program 34 (banker), option `attacks=no`; the zone resets one into 1301 |
| 1302 | `trader` | mob program 33 (barter vendor), options `store=1303`, `attacks=no`, `price 1137 1308x2`; the zone resets one into 1302 |
| 1304 | `banker dark` | a banker as 1301; the zone resets one into the dark room 1304, where it cannot see a customer without infravision |
| 1305 | `banker morning` | a banker with `hours=0-12` (open hours 0 to 11); the zone resets one into 1305 |
| 1306 | `banker evening` | a banker with `hours=12-0` (open hours 12 to 23); the zone resets one into 1306 |
| 1307 | `trader odd` | a barter vendor with `store=1308`, `list=Step right up!`, `price 1137 1308x2 deduct`, `price 1304 1308x1`, `price 1305 1308x1`, `attacks=no`; the zone resets one into 1307 |
| 1308 | `banker broken` | a banker with `fee=0`, which disables it (a `MOB ERROR` line at boot); the zone resets one into 1309 |
| 1309 | `trader broken` | a barter vendor with no `store=`, which disables it (a `MOB ERROR` line at boot); the zone resets one into 1309 |

There are no `.shp` shops in the test world (`shp/index` is `$` alone); the traders trade through
their mob options instead.

## Objects

| Vnum | Keyword | Notes |
| --- | --- | --- |
| 1304 | `cap` | iron cap, head armour; the odd trader's second stock item |
| 1305 | `ingot` | lead ingot, weight 90000; the odd trader's third stock item |
| 1306 | `key` | brass key, an `ITEM_KEY` |
| 1307 | `ring` | cursed ring, `NODROP` |
| 1308 | `bead` | copper bead, the traders' price currency |
