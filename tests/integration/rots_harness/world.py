"""The test world's rooms, objects and mobs, and room names, that two or more scenario files, or
the harness itself, name (tests/integration/world). One that only one scenario file names is a
constant in that file, as is a vnum a file names for another role, such as the trader's currency."""

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
# Room 1137, the DEATH room in zone 11 east of the approach, with no exits: whoever walks in dies
# (do_move, act_move.cpp, which calls raw_kill outside any fight).
ROOM_GALLOWS_PIT = 1137
# Room 1160, Human Start in zone 11, where a dead human mortal's spirit wakes (r_mortal_start_room,
# raw_kill in fight.cpp).
ROOM_HUMAN_START = 1160
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
# Room 2201, zone 22's Order Hall: lit, with no exits, no PEACEROOM flag and nothing placed in it
# at boot.
ROOM_ORDER_HALL = 2201

# The name look and goto show for ROOM_ARENA_CENTRE (wld/11.wld).
ROOM_ARENA_CENTRE_NAME = "Arena Centre"
# The name look shows for ROOM_HUMAN_START (wld/11.wld).
ROOM_HUMAN_START_NAME = "Human Start"

# Object 1136 in obj/11.obj, the leather bag: a plain container, with no wear slot and not wieldable.
OBJECT_LEATHER_BAG = 1136
# Object 1137 in obj/11.obj, the leather cap: head armour, which zone 13 stocks in both store rooms.
OBJECT_LEATHER_CAP = 1137
# Object 1138 in obj/11.obj, the sickly amulet: worn on the neck, its APPLY_BITVECTOR affect sets
# AFF_POISON on the wearer with no poison affect behind it.
OBJECT_SICKLY_AMULET = 1138
# Object 1139 in obj/11.obj, the tainted bread: food that fills 2 and poisons anyone below
# immortal level who eats it.
OBJECT_TAINTED_BREAD = 1139
# Object 1140 in obj/11.obj, the plain steel sword: a slashing weapon (value[3] 3) with no affects,
# and the base of the test world's skin family.
OBJECT_PLAIN_STEEL_SWORD = 1140
# Object 1185 in obj/11.obj, the stone girdle: worn on the waist, leadership +3, with the extra
# flags WILLPOWER, DWARF and STAY_ZONE.
OBJECT_STONE_GIRDLE = 1185
# Object 1186 in obj/11.obj, the chain shirt: body armour whose second value, 5, gives its wearer
# a non-zero absorb (get_percent_absorb, act_info.cpp).
OBJECT_CHAIN_SHIRT = 1186
# Object 1308 in obj/13.obj, the copper bead: carried, never worn, and the traders' price currency.
OBJECT_COPPER_BEAD = 1308
# Object 1601 in obj/16.obj, the plain sword: a weapon without the MAGIC flag or affects of its
# own. ROOM_INN is the room with the same number.
OBJECT_PLAIN_SWORD = 1601
# Object 1602 in obj/16.obj, the dwarven stone: carried, with the extra flags DWARF and STAY_ZONE.
OBJECT_DWARVEN_STONE = 1602
# Object 1603 in obj/16.obj, the leather helm: head armour without affects; no zone resets one.
OBJECT_LEATHER_HELM = 1603
# Object 1609 in obj/16.obj, the strange relic: every named extra flag but INVISIBLE and NODROP,
# and the wear flags TAKE and THROW.
OBJECT_STRANGE_RELIC = 1609
# Object 2400 in obj/24.obj, the flask: an empty drink container holding 20.
OBJECT_FLASK = 2400

# Mob 1105 in mob/11.mob, the harness innkeeper: spec_ass.cpp binds the receptionist special to its
# vnum, and zone 16 resets it into ROOM_INN, where it rents rooms to mortals.
MOB_INNKEEPER = 1105
# Mob 1132 in mob/11.mob, the bystander orc: male, involved in nothing, with no maximum stamina
# and 50 moves (its `0 0 50 1` line, load_mobiles in db.cpp).
MOB_BYSTANDER_ORC = 1132
# Mob 1170 in mob/11.mob, the shining angel: the resetter special (spec_ass.cpp), which resets a
# player's practice when told "pracreset".
MOB_SHINING_ANGEL = 1170
# Mob 1454 in mob/14.mob, the straw scarecrow: level 1, with 20 hit points and constitution 10;
# perception 0 and bodytype 1, not an animal.
MOB_STRAW_SCARECROW = 1454
# Mob 1503 in mob/15.mob, the mysterious traveller: a copy of the real mage guildmaster at its
# real vnum.
MOB_MYSTERIOUS_TRAVELLER = 1503
# Mob 1722 in mob/17.mob, the porter: no script and no special, for an implementor to force.
MOB_PORTER = 1722
