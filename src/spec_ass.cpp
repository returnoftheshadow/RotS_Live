/* ************************************************************************
 *   File: spec_assign.c                                 Part of CircleMUD *
 *  Usage: Functions to assign function pointers to objs/mobs/rooms        *
 *                                                                         *
 *  All rights reserved.  See license.doc for complete information.        *
 *                                                                         *
 *  Copyright (C) 1993 by the Trustees of the Johns Hopkins University     *
 *  CircleMUD is based on DikuMUD, Copyright (C) 1990, 1991.               *
 ************************************************************************ */

#include <stdio.h>

#include "comm.h"
#include "db.h"
#include "interpre.h"
#include "mob_progs/banker.h"
#include "mob_progs/shopkeeper.h"
#include "platdef.h"
#include "structs.h"
#include "utils.h"

extern struct room_data world;
extern int top_of_world;
extern struct index_data* mob_index;
extern struct index_data* obj_index;

struct obj_data generic_water;
struct obj_data generic_poison;

void boot_ferries();
SPECIAL(postmaster);
SPECIAL(receptionist);
SPECIAL(guild);
SPECIAL(puff);
SPECIAL(doorgod);
SPECIAL(snake);
SPECIAL(thief);
SPECIAL(magic_user);
SPECIAL(intelligent);
SPECIAL(gatekeeper);
SPECIAL(gatekeeper_no_knock);
SPECIAL(gatekeeper2);
SPECIAL(mob_cleric);
SPECIAL(mob_magic_user);
SPECIAL(mob_warrior);
SPECIAL(mob_jig);
SPECIAL(block_exit_north);
SPECIAL(block_exit_east);
SPECIAL(block_exit_south);
SPECIAL(block_exit_west);
SPECIAL(block_exit_up);
SPECIAL(block_exit_down);
SPECIAL(resetter);
SPECIAL(mob_ranger);
SPECIAL(mob_ranger_new);
SPECIAL(react_trap);
SPECIAL(ar_tarthalon); // Special programs for dark lake mobiles
SPECIAL(ghoul); //
SPECIAL(vampire_huntress); // Do not use elsewhere
SPECIAL(thuringwethil); //
SPECIAL(vampire_doorkeep); // Or BAD THINGS will probably happen!
SPECIAL(vampire_killer); //
SPECIAL(healing_plant);

SPECIAL(ferry_boat);
SPECIAL(ferry_captain);
SPECIAL(dragon);
SPECIAL(mob_magic_user_spec);

SPECIAL(gen_board);

SPECIAL(pet_shop);
SPECIAL(kit_room);
SPECIAL(room_temple);

SPECIAL(vortex_elevator); // Vortex elevating player on mob-death
SPECIAL(wolf_summoner);
SPECIAL(reciter);
SPECIAL(herald);
SPECIAL(drake_one);
/*  SPECIAL declarations for objects */
SPECIAL(obj_willpower);

/* assign special procedures to mobiles */
void assign_mobiles(void)
{
    ASSIGNMOB(1105, receptionist);
    ASSIGNMOB(1109, guild);
    ASSIGNMOB(1118, postmaster);
    ASSIGNMOB(1501, receptionist);
    ASSIGNMOB(1503, guild);
    ASSIGNMOB(1509, guild);
    ASSIGNMOB(1510, guild);
    ASSIGNMOB(1511, guild);
    ASSIGNMOB(1541, guild);
    ASSIGNMOB(1515, ferry_captain);

    ASSIGNMOB(4601, guild);
    ASSIGNMOB(4608, guild);
    ASSIGNMOB(4609, guild);
    ASSIGNMOB(4612, receptionist);
    ASSIGNMOB(5701, guild);
    ASSIGNMOB(6000, guild);
    ASSIGNMOB(6003, receptionist);
    ASSIGNMOB(6603, receptionist);
    ASSIGNMOB(6604, receptionist); // haradrim innkeeper
    ASSIGNMOB(9903, guild);

    ASSIGNMOB(10002, guild); // librarian -- languages
    ASSIGNMOB(10003, guild); // mage guild
    ASSIGNMOB(10005, receptionist);
    ASSIGNMOB(10013, receptionist);
    ASSIGNMOB(10017, receptionist);
    ASSIGNMOB(10021, guild); // mystic guild
    ASSIGNMOB(10025, receptionist);
    ASSIGNMOB(10042, guild); // warrior guild
    ASSIGNMOB(10054, guild); // Aroden -- ranger guild

    ASSIGNMOB(13510, receptionist); // Fiddlers' Inn

    ASSIGNMOB(13600, guild); // Magus
    ASSIGNMOB(13605, receptionist); // rent magus

    ASSIGNMOB(13703, guild); // mystic gm
    ASSIGNMOB(13704, guild); // warrior gm
    ASSIGNMOB(13705, guild); // ranger gm
    ASSIGNMOB(13709, receptionist); // innkeeper

    ASSIGNMOB(14403, receptionist);
    ASSIGNMOB(14409, guild); // mystic guild
    ASSIGNMOB(14415, guild); // mage guild
    ASSIGNMOB(14422, guild); // ranger guild
    ASSIGNMOB(14423, guild); // warrior guild

    ASSIGNMOB(2503, guild); // honorable thief -- lockpick guild
    ASSIGNMOB(14100, guild); // hermit -- guardian guild
    ASSIGNMOB(8500, guild); // elven enchanter -- enchant guild
    ASSIGNMOB(3888, guild); // orc prisoner -- enchant/guardian guild

    // dale receptionists
    ASSIGNMOB(15600, receptionist); // New Dale innkeper
    ASSIGNMOB(16607, receptionist); // New Dale innkeper

    // elven halls guildmasters
    ASSIGNMOB(16899, guild); //  Elven hall ranger
    ASSIGNMOB(16898, guild); // Elven hall warrior
    ASSIGNMOB(16802, guild); // Elven hall mage

    // elven halls receptionist
    ASSIGNMOB(16896, receptionist); // Elven hall receptionist

    // lonely mountain receptionists
    ASSIGNMOB(3017, receptionist); // LM receptionist 1
    ASSIGNMOB(3018, receptionist); // LM receptionist 2
    ASSIGNMOB(3019, receptionist); // LM receptionist 3
    ASSIGNMOB(3020, receptionist); // LM receptionist 4

    // New DG innkeepers
    ASSIGNMOB(27531, receptionist); // Kurn the keeper (all dark races)
    ASSIGNMOB(27530, receptionist); // Tsarkon the restless (orc and uruks)
    ASSIGNMOB(27502, receptionist); // Gurm the Slab Warden (trolls)
    ASSIGNMOB(27602, receptionist); // Strom, the Keeper (uruks)
    ASSIGNMOB(27716, receptionist); // A Human Bed-Warden (harads)
    ASSIGNMOB(27725, receptionist); // Gurakhnir (orcs)

    // New DG guildmasters
    ASSIGNMOB(27532, guild); // old uruk ranger (orcs and uruks)
    ASSIGNMOB(27533, guild); // sturdy uruk warrior (orcs and uruks)
    ASSIGNMOB(27505, guild); // huge war-troll (trolls)
    ASSIGNMOB(27601, guild); // unnamed orcish warrior (orcs)
    ASSIGNMOB(27702, guild); // slithe, veteran ranger (all races cept trolls)
    ASSIGNMOB(27701, guild); // logarn, veteran warrior (all races cept trolls)
    ASSIGNMOB(27703, guild); // feeble old sage (all dark races cept trolls)
    ASSIGNMOB(27704, guild); // cowled magician (all dark races cept trolls)
    ASSIGNMOB(27705, guild); // sorceror (all dark races cept trolls)
    ASSIGNMOB(27708, guild); // dark acolyte (all dark races cept trolls)
    ASSIGNMOB(27707, guild); // witch doctor (all dark races cept trolls)
    ASSIGNMOB(27706, guild); // dark spiritualist (all dark races cept trolls)
    ASSIGNMOB(28416, guild); // glhrush, the uruk ranger (uruks)
    ASSIGNMOB(28418, guild); // surka, the uruk ranger (uruks?)

    // goblin gate receptionist
    ASSIGNMOB(32531, receptionist);

    // goblin gate guildmasters
    ASSIGNMOB(32200, guild); // glass-eyed shaman
    ASSIGNMOB(27724, guild); // gorbush the scarred
    ASSIGNMOB(32527, guild); // gurokh, orcish captain
    ASSIGNMOB(32530, guild); // dhamgul, orcish ranger
    ASSIGNMOB(27730, guild); // goblin shaman
    ASSIGNMOB(32529, guild); // maelor, orcish illusionist
    ASSIGNMOB(18833, guild); // Lhuth mob in vinyanost tombs
    ASSIGNMOB(10213, guild); // Karvok
    ASSIGNMOB(4647, guild); // Ungorod Swashbuckler

    ASSIGNMOB(28473, receptionist); // temporary innkeeper in uruk mage tower
    ASSIGNMOB(21343, receptionist); // tawarost receptionist

    ASSIGNMOB(28640, receptionist); // glob, tentmaster
    ASSIGNMOB(21344, receptionist); /*tawarost*/
    // ASSIGNMOB(1101, drake_one);
    ASSIGNMOB(2041, guild); // ALT RotS Puke
    ASSIGNMOB(2043, guild); // ALT RotS Uruk

    ASSIGNMOB(10310, guild); // Beorning guildmaster
    ASSIGNMOB(32816, guild); // Olog-Hai guildmaster
    ASSIGNMOB(6605, guild);

    ASSIGNMOB(2012, dragon);
    ASSIGNMOB(10312, receptionist); // Beorning Rent

    ASSIGNMOB(32800, receptionist); // Olog-Hai Rent
}

/* assign special procedures to objects */
void assign_objects(void)
{
    boot_ferries();

    ASSIGNOBJ(1104, gen_board); /* social board */
    ASSIGNOBJ(1105, gen_board); /* freeze board */
    ASSIGNOBJ(1106, gen_board); /* immortal board */
    ASSIGNOBJ(1107, gen_board); /* mortal board */
    ASSIGNOBJ(1108, gen_board); /* mortal board */
    ASSIGNOBJ(1109, gen_board); /* mortal board */
    ASSIGNOBJ(1110, gen_board); /* mortal board */
    ASSIGNOBJ(1111, gen_board); /* mortal board */
    ASSIGNOBJ(1112, gen_board); /* mortal board */
    ASSIGNOBJ(1113, gen_board); /* mortal board */
    ASSIGNOBJ(1114, gen_board); /* mortal board */
    ASSIGNOBJ(1116, gen_board); /* mortal board */
    ASSIGNOBJ(1117, gen_board); /* mortal board */
    ASSIGNOBJ(1118, gen_board); /* mortal board */
    ASSIGNOBJ(1119, gen_board); /* mortal board */
    ASSIGNOBJ(1127, gen_board); /* mortal board */
    ASSIGNOBJ(1128, gen_board); /* mortal board */
    ASSIGNOBJ(1129, gen_board); /* immortal board */
    ASSIGNOBJ(1130, gen_board); /* mortal board */
    ASSIGNOBJ(1131, gen_board); /* immortal board */
    ASSIGNOBJ(1132, gen_board); /* mortal board */
    ASSIGNOBJ(1133, gen_board); /* mortal board */
    ASSIGNOBJ(1134, gen_board); /* immortal board */
    ASSIGNOBJ(1135, gen_board); /* immortal board */
    ASSIGNOBJ(1121, ferry_boat);

    generic_water.obj_flags.type_flag = ITEM_FOUNTAIN;
    generic_poison.obj_flags.type_flag = ITEM_FOUNTAIN;

    generic_water.obj_flags.value[0] = 100;
    generic_poison.obj_flags.value[0] = 100;
    generic_water.obj_flags.value[1] = 100;
    generic_poison.obj_flags.value[1] = 100;

    generic_water.name = "water";
    generic_poison.name = "water";

    generic_water.obj_flags.value[2] = LIQ_WATER;
    generic_poison.obj_flags.value[2] = LIQ_WATER;

    generic_water.obj_flags.value[3] = 0;
    generic_poison.obj_flags.value[3] = 1;
}

/* assign special procedures to rooms */
void assign_rooms(void)
{
    SPECIAL(pet_shops);

    ASSIGNROOM(1160, kit_room);
    ASSIGNROOM(1170, kit_room);
    ASSIGNROOM(1574, room_temple);
    ASSIGNROOM(1122, pet_shops);
}

char* spec_pro_message[] = {
    "", /* 0 */
    "",
    "",
    "",
    "",
    "",
    "",
    "$n is dancing a merry jig!",
    "$n blocks the way north!",
    "$n blocks the way east!",
    "$n blocks the way south!", // 10
    "$n blocks the way west!",
    "$n blocks the way up!",
    "$n blocks the way down!",
    "",
    "",
    "",
    "",
    "",
    "",
    "", // 20
    "",
    "",
    "",
    "$n is exuding a dizzying fragrance.",
    "",
    "",
    "",
    "",
    "",
    "", // 30
    "",
    "",
    "", // 33 barter vendor
    "" // 34 banker
};

void* virt_program_number(int number)
{
    switch (number) {
    case 0:
        return 0;
    case 1:
        return (void*)snake;
    case 2:
        return (void*)gatekeeper;
    case 3:
        return (void*)mob_cleric;
    case 4:
        return (void*)mob_magic_user;
    case 5:
        return (void*)mob_warrior;
    case 6:
        return (void*)gatekeeper2;
    case 7:
        return (void*)mob_jig;
    case 8:
        return (void*)block_exit_north;
    case 9:
        return (void*)block_exit_east;
    case 10:
        return (void*)block_exit_south;
    case 11:
        return (void*)block_exit_west;
    case 12:
        return (void*)block_exit_up;
    case 13:
        return (void*)block_exit_down;
    case 14:
        return (void*)resetter;
    case 15:
        return (void*)mob_ranger;
    case 16:
        return (void*)react_trap;
    case 17:
        return (void*)gatekeeper_no_knock;
    case 18:
        return (void*)ar_tarthalon;
    case 19:
        return (void*)ghoul;
    case 20:
        return (void*)vampire_huntress;
    case 21:
        return (void*)thuringwethil;
    case 22:
        return (void*)vampire_doorkeep;
    case 23:
        return (void*)vampire_killer;
    case 24:
        return (void*)healing_plant;
    case 25:
        return (void*)vortex_elevator;
    case 26:
        return (void*)wolf_summoner;
    case 27:
        return (void*)reciter;
    case 28:
        return (void*)herald;
    case 29:
        return (void*)postmaster;
    case 30:
        return (void*)dragon;
    case 31:
        return (void*)mob_magic_user_spec;
    case 32:
        return (void*)mob_ranger_new;
    case 33:
        return (void*)barter_vendor;
    case 34:
        return (void*)banker;
    default:
        log("Virt_assign: unknown prog_number for special mob.");
        return 0;
        break;
    }
}

special_func_ptr get_special_function(int number)
{
    switch (number) {
    case 0:
        return 0;
    case 1:
        return &snake;
    case 2:
        return &gatekeeper;
    case 3:
        return &mob_cleric;
    case 4:
        return &mob_magic_user;
    case 5:
        return &mob_warrior;
    case 6:
        return &gatekeeper2;
    case 7:
        return &mob_jig;
    case 8:
        return &block_exit_north;
    case 9:
        return &block_exit_east;
    case 10:
        return &block_exit_south;
    case 11:
        return &block_exit_west;
    case 12:
        return &block_exit_up;
    case 13:
        return &block_exit_down;
    case 14:
        return &resetter;
    case 15:
        return &mob_ranger;
    case 16:
        return &react_trap;
    case 17:
        return &gatekeeper_no_knock;
    case 18:
        return &ar_tarthalon;
    case 19:
        return &ghoul;
    case 20:
        return &vampire_huntress;
    case 21:
        return &thuringwethil;
    case 22:
        return &vampire_doorkeep;
    case 23:
        return &vampire_killer;
    case 24:
        return &healing_plant;
    case 25:
        return &vortex_elevator;
    case 26:
        return &wolf_summoner;
    case 27:
        return &reciter;
    case 28:
        return &herald;
    case 29:
        return &postmaster;
    case 30:
        return &dragon;
    case 31:
        return &mob_magic_user_spec;
    case 32:
        return &mob_ranger_new;
    case 33:
        return &barter_vendor;
    case 34:
        return &banker;
    default:
        log("Virt_assign: unknown prog_number for special mob.");
        return 0;
        break;
    }
}

void* virt_obj_program_number(int number)
{
    switch (number) {
    case 1:
        return (void*)obj_willpower;

    default:
        log("Virt_assign: unknown prog_number for special object.");
        return 0;
        break;
    }
}

void virt_assignmob(struct char_data* mob)
{
    void* tmpptr;
    SPECIAL(*tmpfunc);

    if (mob_index[mob->nr].func)
        return;

    if (IS_SET(mob->specials2.act, MOB_SPEC)) {
        tmpptr = virt_program_number(mob->specials.store_prog_number);
        tmpfunc = (SPECIAL(*))tmpptr;
        ASSIGNREALMOB(mob, tmpfunc);
    }
}

void virt_assignobj(struct obj_data* obj)
{
    void* tmpptr;
    SPECIAL(*tmpfunc);

    if (obj_index[obj->item_number].func)
        return;

    tmpptr = virt_obj_program_number(obj->obj_flags.prog_number);
    tmpfunc = (SPECIAL(*))tmpptr;
    ASSIGNREALOBJ(obj, tmpfunc);

    return;
}
