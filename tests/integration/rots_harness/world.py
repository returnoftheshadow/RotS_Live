"""Virtual numbers of the test world's rooms that two or more scenario files, or the harness itself, name
(tests/integration/world). A room only one scenario file names is a constant in that file."""

from __future__ import annotations

# Room 1101, where implementors arrive.
ROOM_IMMORTAL_START = 1101
# Room 1130, the lit western end of the arena in zone 11.
ROOM_ARENA_WEST = 1130
# Room 1131, the middle of the arena in zone 11.
ROOM_ARENA_CENTRE = 1131
# Room 1132, the eastern end of the arena in zone 11, east of the centre; zone 11 resets the target orc (mob 1130) here.
ROOM_ARENA_EAST = 1132
# Room 1133, the arena's one DARK room, north of the centre in zone 11.
ROOM_DARK_CELL = 1133
# Room 1138, in zone 11, whose one exit leads east into the Gallows Pit.
ROOM_GALLOWS_APPROACH = 1138
# Room 1180, the signpost room in zone 11, whose two extra descriptions both answer to "sign".
ROOM_SIGNPOST = 1180
# Room 1302, the lit trade hall in zone 13, where zone 13 resets the trader (mob 1302).
ROOM_TRADE_HALL = 1302
# Room 1309 in zone 13, where zone 13 resets the broken banker (mob 1308) and the broken trader
# (mob 1309), each disabled by its options.
ROOM_BROKEN_MARKET = 1309
# Room 1601, the inn of zone 16, where the harness innkeeper (mob 1105) rents rooms to mortals.
ROOM_INN = 1601
# Room 1801, zone 18's PEACEROOM boar pen, where the zone resets one pen boar (mob 1450).
ROOM_BOAR_PEN = 1801
