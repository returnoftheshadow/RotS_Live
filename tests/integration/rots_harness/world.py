"""The test world's rooms, objects and mobs, and their names, that scenarios share.

A fixture is here when two or more scenario files, or the harness itself, name it. The world is
tests/integration/world.

A room's name is ROOM_<X>_NAME, a short description is OBJECT_<X>_SHORT_DESCRIPTION or
MOB_<X>_SHORT_DESCRIPTION, and an object's keyword is OBJECT_<X>_KEYWORD, each beside the vnum
constant it belongs to.
"""

from __future__ import annotations

# Room 1101, where implementors arrive.
ROOM_IMMORTAL_START = 1101
# Room 1130, the lit western end of the arena in zone 11.
ROOM_ARENA_WEST = 1130
# Room 1131, the middle of the arena in zone 11.
ROOM_ARENA_CENTRE = 1131
# Room 1132, the eastern end of the arena in zone 11, east of the centre; zone 11 resets the target
# orc (mob 1130) here.
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

# The name look and goto show for ROOM_IMMORTAL_START (wld/11.wld).
ROOM_IMMORTAL_START_NAME = "Immortal Start"
# The name look and goto show for ROOM_ARENA_WEST (wld/11.wld).
ROOM_ARENA_WEST_NAME = "Arena West"
# The name look and goto show for ROOM_ARENA_CENTRE (wld/11.wld).
ROOM_ARENA_CENTRE_NAME = "Arena Centre"
# The name look and goto show for ROOM_ARENA_EAST (wld/11.wld).
ROOM_ARENA_EAST_NAME = "Arena East"
# The name look shows for ROOM_HUMAN_START (wld/11.wld).
ROOM_HUMAN_START_NAME = "Human Start"
# The name look and goto show for ROOM_TRADE_HALL (wld/13.wld).
ROOM_TRADE_HALL_NAME = "Trade Hall"
# The name look and goto show for ROOM_ORDER_HALL (wld/22.wld).
ROOM_ORDER_HALL_NAME = "Order Hall"

# Object 1130 in obj/11.obj, the harness token: a board, since spec_ass.cpp assigns gen_board to
# its vnum. Mob 1130 and room 1130 share its number.
OBJECT_HARNESS_TOKEN = 1130
# OBJECT_HARNESS_TOKEN's short description.
OBJECT_HARNESS_TOKEN_SHORT_DESCRIPTION = "a harness token"
# Object 1136 in obj/11.obj, the leather bag: a plain container, with no wear slot and not
# wieldable.
OBJECT_LEATHER_BAG = 1136
# OBJECT_LEATHER_BAG's short description.
OBJECT_LEATHER_BAG_SHORT_DESCRIPTION = "a leather bag"
# Object 1137 in obj/11.obj, the leather cap: head armour, which zone 13 stocks in both store rooms.
OBJECT_LEATHER_CAP = 1137
# OBJECT_LEATHER_CAP's short description.
OBJECT_LEATHER_CAP_SHORT_DESCRIPTION = "a leather cap"
# Object 1138 in obj/11.obj, the sickly amulet: worn on the neck, its APPLY_BITVECTOR affect sets
# AFF_POISON on the wearer with no poison affect behind it.
OBJECT_SICKLY_AMULET = 1138
# OBJECT_SICKLY_AMULET's short description.
OBJECT_SICKLY_AMULET_SHORT_DESCRIPTION = "a sickly amulet"
# Object 1139 in obj/11.obj, the tainted bread: food that fills 2 and poisons anyone below
# immortal level who eats it.
OBJECT_TAINTED_BREAD = 1139
# OBJECT_TAINTED_BREAD's short description.
OBJECT_TAINTED_BREAD_SHORT_DESCRIPTION = "a piece of tainted bread"
# Object 1140 in obj/11.obj, the plain steel sword: a slashing weapon (value[3] 3) with no affects,
# and the base of the test world's skin family.
OBJECT_PLAIN_STEEL_SWORD = 1140
# Object 1185 in obj/11.obj, the stone girdle: worn on the waist, leadership +3, with the extra
# flags WILLPOWER, DWARF and STAY_ZONE.
OBJECT_STONE_GIRDLE = 1185
# OBJECT_STONE_GIRDLE's short description.
OBJECT_STONE_GIRDLE_SHORT_DESCRIPTION = "a stone girdle"
# Object 1186 in obj/11.obj, the chain shirt: body armour whose second value, 5, gives its wearer
# a non-zero absorb (get_percent_absorb, act_info.cpp).
OBJECT_CHAIN_SHIRT = 1186
# OBJECT_CHAIN_SHIRT's short description.
OBJECT_CHAIN_SHIRT_SHORT_DESCRIPTION = "a chain shirt"
# Object 1308 in obj/13.obj, the copper bead: carried, never worn, and the traders' price currency.
OBJECT_COPPER_BEAD = 1308
# OBJECT_COPPER_BEAD's short description.
OBJECT_COPPER_BEAD_SHORT_DESCRIPTION = "a copper bead"
# Object 1304 in obj/13.obj, the iron cap: head armour.
OBJECT_IRON_CAP = 1304
# OBJECT_IRON_CAP's short description.
OBJECT_IRON_CAP_SHORT_DESCRIPTION = "an iron cap"
# Object 1307 in obj/13.obj, the cursed ring: worn on a finger, with the NODROP flag, so it cannot
# be given away or dropped.
OBJECT_CURSED_RING = 1307
# OBJECT_CURSED_RING's short description.
OBJECT_CURSED_RING_SHORT_DESCRIPTION = "a cursed ring"
# Object 1601 in obj/16.obj, the plain sword: a weapon without the MAGIC flag or affects of its
# own. ROOM_INN is the room with the same number.
OBJECT_PLAIN_SWORD = 1601
# OBJECT_PLAIN_SWORD's short description.
OBJECT_PLAIN_SWORD_SHORT_DESCRIPTION = "a plain sword"
# Object 1602 in obj/16.obj, the dwarven stone: carried, with the extra flags DWARF and STAY_ZONE.
OBJECT_DWARVEN_STONE = 1602
# OBJECT_DWARVEN_STONE's short description.
OBJECT_DWARVEN_STONE_SHORT_DESCRIPTION = "a dwarven stone"
# Object 1603 in obj/16.obj, the leather helm: head armour without affects; no zone resets one.
OBJECT_LEATHER_HELM = 1603
# OBJECT_LEATHER_HELM's short description.
OBJECT_LEATHER_HELM_SHORT_DESCRIPTION = "a leather helm"
# Object 1609 in obj/16.obj, the strange relic: every named extra flag but INVISIBLE and NODROP,
# and the wear flags TAKE and THROW.
OBJECT_STRANGE_RELIC = 1609
# The keyword OBJECT_STRANGE_RELIC answers to.
OBJECT_STRANGE_RELIC_KEYWORD = "relic"
# OBJECT_STRANGE_RELIC's short description.
OBJECT_STRANGE_RELIC_SHORT_DESCRIPTION = "a strange relic"
# Object 1701 in obj/17.obj, the grey pebble: carried, never worn, the token script mobs are given.
OBJECT_GREY_PEBBLE = 1701
# Object 1901 in obj/19.obj, the brass keyring (ITEM_KEYRING): hangs on a belt hook, or is held.
OBJECT_BRASS_KEYRING = 1901
# OBJECT_BRASS_KEYRING's short description.
OBJECT_BRASS_KEYRING_SHORT_DESCRIPTION = "a brass keyring"
# Object 1902 in obj/19.obj, the leather belt: worn about the waist, which gives the belt hooks.
OBJECT_LEATHER_BELT = 1902
# OBJECT_LEATHER_BELT's short description.
OBJECT_LEATHER_BELT_SHORT_DESCRIPTION = "a leather belt"
# Object 2100 in obj/21.obj, the draining grey sash: worn about the body, maximum hit points -72.
OBJECT_GREY_SASH = 2100
# OBJECT_GREY_SASH's short description.
OBJECT_GREY_SASH_SHORT_DESCRIPTION = "a draining grey sash"
# Object 2400 in obj/24.obj, the flask: an empty drink container holding 20.
OBJECT_FLASK = 2400

# Mob 1105 in mob/11.mob, the harness innkeeper: spec_ass.cpp binds the receptionist special to its
# vnum, and zone 16 resets it into ROOM_INN.
MOB_INNKEEPER = 1105
# Mob 1132 in mob/11.mob, the bystander orc: male, involved in nothing, with no maximum stamina
# and 50 moves (its `0 0 50 1` line, load_mobiles in db.cpp).
MOB_BYSTANDER_ORC = 1132
# MOB_BYSTANDER_ORC's short description.
MOB_BYSTANDER_ORC_SHORT_DESCRIPTION = "a bystander orc"
# Mob 1135 in mob/11.mob, the hardy target orc: 900 hit points, so a fight with it outlasts the
# blows a scenario lands.
MOB_HARDY_TARGET_ORC = 1135
# MOB_HARDY_TARGET_ORC's short description.
MOB_HARDY_TARGET_ORC_SHORT_DESCRIPTION = "a hardy target orc"
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
# MOB_PORTER's short description.
MOB_PORTER_SHORT_DESCRIPTION = "a porter"
# Mob 2404 in mob/24.mob, the practice dummy: level 1, which a test slays or targets.
MOB_PRACTICE_DUMMY = 2404
# MOB_PRACTICE_DUMMY's short description.
MOB_PRACTICE_DUMMY_SHORT_DESCRIPTION = "a practice dummy"
