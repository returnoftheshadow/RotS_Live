/* ************************************************************************
 *   File: act.movement.c                                Part of CircleMUD *
 *  Usage: movement commands, door handling, & sleep/rest/etc state        *
 *                                                                         *
 *  All rights reserved.  See license.doc for complete information.        *
 *                                                                         *
 *  Copyright (C) 1993 by the Trustees of the Johns Hopkins University     *
 *  CircleMUD is based on DikuMUD, Copyright (C) 1990, 1991.               *
 ************************************************************************ */

#include <stdio.h>
#include <string.h>

#include "char_utils.h"
#include "comm.h"
#include "db.h"
#include "handler.h"
#include "interpre.h"
#include "protocol.h"
#include "script.h"
#include "spells.h"
#include "structs.h"
#include "utils.h"

#include <assert.h>

typedef char* string;

extern struct room_data world;
extern struct char_data* character_list;
extern struct descriptor_data* descriptor_list;
extern struct index_data* obj_index;
extern int rev_dir[];
extern char* dirs[];
extern char* refer_dirs[];
extern int movement_loss[];
extern struct time_info_data time_info;
extern struct skill_data skills[];
extern int get_real_stealth(struct char_data* ch);

extern void raw_kill(char_data* ch, char_data* killer, int attacktype);

ACMD(do_look);
ACMD(do_open);
ACMD(do_close);
ACMD(do_dismount);
/* external functs */
void death_cry(struct char_data* ch);
extern struct char_data* waiting_list;
void stop_hiding(struct char_data* ch, char);
void do_power_of_arda(char_data* ch);

ACMD(do_look);

bool should_double_strength(char_data* character)
{
    char_data* master = NULL;
    if (utils::is_pc(*character)) {
        master = character;
    } else {
        if (MOB_FLAGGED(character, MOB_MOUNT)) {
            master = character->mount_data.rider;
        }
    }

    if (master) {
        return utils::get_specialization(*master) == game_types::PS_HeavyFighting;
    }

    return false;
}

int get_room_move_penalty(const char_data* character, int room_sector)
{
    int room_move_penalty = movement_loss[room_sector];

    if (utils::get_specialization(*character) == game_types::PS_Stealth && !character->mount_data.mount) {
        room_move_penalty = std::max(1, room_move_penalty / 2);
    }

    return room_move_penalty;
}

int room_move_cost(char_data* character, room_data* new_room)
{
    assert(character);
    assert(new_room);

    int character_strength = character->tmpabilities.str;
    if (should_double_strength(character)) {
        character_strength = character_strength * 2;
    }

    int move_cost = std::max(20 + IS_CARRYING_W(character) / character_strength / 10,
        70 + IS_CARRYING_W(character) / character_strength / 20);

    // Now can carry str*6 pounds without penalty, penalty becomes heavy at str*10.
    if (MOB_FLAGGED(character, MOB_MOUNT)) {
        move_cost = IS_CARRYING_W(character) / GET_STR(character) / 20; // Mounts have less str penalty
    }

    if (move_cost < 100) {
        if (MOB_FLAGGED(character, MOB_MOUNT)) {
            move_cost = std::max(75, 50 + move_cost / 2);
        } else {
            move_cost = 100;
        }
    }

    move_cost += utils::get_leg_encumbrance(*character) * 2;
    int room_sector = new_room->sector_type;
    int room_move_penalty = get_room_move_penalty(character, room_sector);

    if (!MOB_FLAGGED(character, MOB_MOUNT)) {
        move_cost = room_move_penalty * (move_cost + number(0, 99)) / 2;
    } else {
        move_cost = room_move_penalty * (move_cost + number(0, 99)) * 2 / 5;
    }

    if (IS_RIDING(character)) {
        move_cost = (move_cost / (120 + GET_RAW_KNOWLEDGE(character, SKILL_RIDE) * 2 + GET_RAW_KNOWLEDGE(character, SKILL_ANIMALS) / 2)) * 5 / 4;
    } else {
        move_cost = move_cost / 100;
    }

    // Rooms always cost at least one to move.
    return std::max(move_cost, 1);
}

//***********************************************************************
int check_simple_move(struct char_data* ch, int cmd, int* mv_cost, int mode)
/* Assumes,
     1. That there is no master and no followers.
     2. That the direction exists.

     Returns :

     0 : success
     1 : intercepted by specials, or something like that
     2 : need a boat
     3 : not enough move points
     4 : mount exhausted // obsolete
     5 : mount would not move // obsolete
     6 : has no control over mount
     7 : riders can't go indoors
     8 : race guard mob is in the room

     Also returns movement cost in mv_cost
  */
{
    int need_movement;
    struct char_data* tmpch;
    struct room_data *room_to, *room_from;

    if (mode != SCMD_MOVING)
        /* check for special routines (north = 1) */
        if (special(ch, cmd + 1, "", SPECIAL_COMMAND, 0))
            return 1;
    if ((GET_POS(ch) < POSITION_FIGHTING) || (PLR_FLAGGED(ch, PLR_WRITING)))
        return 1;

    room_from = &world[ch->in_room];

    if (!room_from->dir_option[cmd])
        return 1;
    if (room_from->dir_option[cmd]->to_room < 0)
        return 1;
    if (ch->delay.wait_value > 0)
        return 1;

    room_to = &world[room_from->dir_option[cmd]->to_room];
    if (!room_to)
        return 1;

    if (call_trigger(ON_BEFORE_ENTER, room_to, ch, 0) == FALSE)
        return 1; //  Trigger doesn't allow them to enter the new room
    if (IS_SET(EXIT(ch, cmd)->exit_info, EX_NOWALK))
        return 8;

    // look out for the same need_movement in do_simple_move and others...
    need_movement = (room_move_cost(ch, room_from) + room_move_cost(ch, room_to)) / 2;

    /// if bloodied, +2 mvs to walk
    if ((GET_HIT(ch) < GET_MAX_HIT(ch) / 4) || has_critical_stat_damage(ch))
        need_movement += 2;

    // If "sundelayed", harder to move. now, affects with power of Arda
    if (EVIL_RACE(ch))
        do_power_of_arda(ch);

    if (IS_SHADOW(ch))
        need_movement = 0;

    if (room_from->sector_type == SECT_WATER_NOSWIM || room_to->sector_type == SECT_WATER_NOSWIM) {
        /*
         *    room_from->sector_type == SECT_WATER_SWIM ||
         *    room_to->sector_type == SECT_WATER_SWIM) {
         */
        if (!can_swim(ch) && !IS_RIDING(ch)) {
            // can swim on mounts
            return 2;
        } else {
            int boat = 0;
            struct obj_data* tmpobj;
            int tmp;

            for (tmpobj = ch->carrying; tmpobj && !boat; tmpobj = tmpobj->next_content) {
                if (tmpobj->obj_flags.type_flag == ITEM_BOAT)
                    boat = 1;
            }
            for (tmp = 0; tmp < MAX_WEAR && !boat; tmp++) {
                if (ch->equipment[tmp])
                    if ((ch->equipment[tmp])->obj_flags.type_flag == ITEM_BOAT)
                        boat = 1;
            }

            if (!boat) {
                int m;

                /*
                 * m will be in the range [0, 6].  Since the sector movement
                 * cost for SECT_WATER_SWIM and SECT_WATER_NOSWIM are 8 and 10,
                 * this means that a 36r with mastered swim can swim a NOSWIM
                 * room with 1 movement cost.
                 */
                m = GET_PROF_LEVEL(PROF_RANGER, ch) + GET_SKILL(ch, SKILL_SWIM);
                m /= 20;

                need_movement = MAX(1, need_movement - m);
            }
        }
    }

    *mv_cost = need_movement;

    if (IS_NPC(ch)) { // checking mob limitations on movement.
        if ((mode != SCMD_FOLLOW) && (mode != SCMD_MOUNT) && IS_SET(room_to->room_flags, NO_MOB) && !IS_AFFECTED(ch, AFF_CHARM))
            return 5;

        if ((mode != SCMD_FOLLOW) && MOB_FLAGGED(ch, MOB_STAY_ZONE) && !IS_AFFECTED(ch, AFF_CHARM) && (room_from->zone != room_to->zone))
            return 5;
        if ((mode != SCMD_FOLLOW) && MOB_FLAGGED(ch, MOB_STAY_TYPE) && !IS_AFFECTED(ch, AFF_CHARM) && (room_from->sector_type != room_to->sector_type))
            return 5;
    }

    if (GET_MOVE(ch) < need_movement)
        return 3;

    // okay, ch can move now - checking his mount.
    if (IS_RIDING(ch)) {
        if (!char_exists(ch->mount_data.mount_number)) {
            ch->mount_data.mount = 0;
            ch->mount_data.next_rider = 0;
        }
        if (IS_SET(EXIT(ch, cmd)->exit_info, EX_NORIDE))
            return 7;
        if (IS_SET(world[world[ch->in_room].dir_option[cmd]->to_room].room_flags, INDOORS))
            return 7;
        if (IS_SET(world[world[ch->in_room].dir_option[cmd]->to_room].room_flags, NORIDE))
            return 7;
        else if ((mode != SCMD_CARRIED) && ((ch->mount_data.mount)->mount_data.rider != ch))
            return 6;
    }

    // Checking for race_guard mobs in the room the ch wants to move to

    for (tmpch = world[world[ch->in_room].dir_option[cmd]->to_room].people; tmpch;
         tmpch = tmpch->next_in_room)
        if (IS_NPC(tmpch) && MOB_FLAGGED(tmpch, MOB_RACE_GUARD))
            if ((GET_RACE(ch) != GET_RACE(tmpch)) && !IS_NPC(ch))
                return 8;

    return 0;
}

/*=================================================================================
   set_blood_trail:
   Here we are setting the blood trail for the harad ability in rooms.
   ------------------------------Change Log---------------------------------------
   slyon: Sept 8, 2017 - Created
==================================================================================*/
void set_blood_trail(struct char_data* ch, int dir)
{
    int tmp;
    if ((utils::is_npc(*ch) || (utils::get_race(*ch) != RACE_GOD))) {
        tmp = number(0, NUM_OF_BLOOD_TRAILS - 1);
        if (utils::is_npc(*ch)) {
            world[ch->in_room].bleed_track[tmp].char_number = ch->nr;
        } else {
            world[ch->in_room].bleed_track[tmp].char_number = -GET_RACE(ch);
        }

        world[ch->in_room].bleed_track[tmp].data = time_info.hours * 8 + dir;
        world[ch->in_room].bleed_track[tmp].condition = 0;
    }
}

/*
 * moves the mount and everybody riding it..
 * performs special() on everybody except the primary rider
 * or the mount itself, depending on who is moving.
 * Returns 1 on success, 0 otherwise
 *
 * assumes the move is legal, e.g. the door is open.
 */
int perform_move_mount(struct char_data* ch, int dir)
{
    char_data *tmpch, *tmpch2, *tmpvict;
    int was_in, new_room, num2, is_death, move_cost, tmp, should_show;
    char buff[1000];
    char buff2[1000];
    void show_mount_to_char(struct char_data*, struct char_data*, char*, char*, int);

    if (!EXIT(ch, dir) || !ch->mount_data.rider)
        return 0;

    if (!char_exists(ch->mount_data.rider_number)) {
        ch->mount_data.rider = 0;
        return 0;
    }

    new_room = EXIT(ch, dir)->to_room;
    was_in = ch->in_room;

    is_death = IS_SET(world[new_room].room_flags, DEATH);

    /* supposedly, the primary rider has already passed special() */
    special(ch->mount_data.rider, dir + 1, "", SPECIAL_COMMAND, 0);

    for (tmpch = ch->mount_data.rider->mount_data.next_rider,
        num2 = ch->mount_data.rider->mount_data.next_rider_number;
         char_exists(num2) && tmpch;
         num2 = ch->mount_data.next_rider_number, tmpch = tmpch->mount_data.next_rider) {

        if (tmpch->in_room != ch->in_room)
            stop_riding(tmpch);

        if ((tmp = check_simple_move(tmpch, dir, &move_cost, SCMD_CARRIED))) {
            if (tmp == 3)
                send_to_char("You are too exhausted!\n\r", ch);
            stop_riding(tmpch);
        } else {
            // if bloodied harder to move -Z
            if (GET_HIT(ch) < GET_MAX_HIT(ch) / 4)
                move_cost += 2;

            // If "sundelayed", harder to move. - power of Arda
            if (EVIL_RACE(ch))
                do_power_of_arda(ch);
            // if (GET_RACE(ch) == RACE_ORC) move_cost += number(2,3);
            // else move_cost += number(1,2);

            GET_MOVE(tmpch) -= move_cost;
        }
    }
    if (tmpch) {
        /* something happened.. dismount all and abort */
        stop_riding_all(ch);
        return 0;
    }

    /* now forming and sending the "leave" message */

    sprintf(buff, " leaving %s, riding on ", dirs[dir]);
    sprintf(buff2, " leaving %s, riding on ", dirs[dir]);

    for (tmpvict = world[was_in].people; tmpvict; tmpvict = tmpvict->next_in_room) {

        if ((tmpvict == ch) || (tmpvict->mount_data.mount == ch))
            continue;
        if (GET_POS(tmpvict) > POSITION_SLEEPING)
            show_mount_to_char(ch, tmpvict, buff, buff2, FALSE);
    }

    /* moving all people involved */
    for (tmpch = ch->mount_data.rider; tmpch; tmpch = tmpch2) {
        tmpch2 = tmpch->mount_data.next_rider;

        if (tmpch != ch->mount_data.rider) {

            sprintf(buff, "You are carried %s by %s.\n\r", dirs[dir],
                PERS(ch, tmpch, FALSE, FALSE));
            send_to_char(buff, tmpch);
        }
        char_from_room(tmpch);
        char_to_room(tmpch, new_room);
        if (is_death)
            raw_kill(tmpch, NULL, 0);
    }
    /* Setting tracks in room */
    if ((IS_NPC(ch) || (GET_RACE(ch) != RACE_GOD)) && !(IS_AFFECTED(ch, AFF_FLYING))) {
        tmp = number(0, NUM_OF_TRACKS - 1);
        if (IS_NPC(ch))
            world[ch->in_room].room_track[tmp].char_number = ch->nr;
        else
            world[ch->in_room].room_track[tmp].char_number = -GET_RACE(ch);

        world[ch->in_room].room_track[tmp].data = time_info.hours * 8 + dir;
        world[ch->in_room].room_track[tmp].condition = 0;
    }

    if (utils::is_affected_by_spell(*ch, SKILL_MARK)) {
        set_blood_trail(ch, dir);
    }

    stop_hiding(ch, TRUE);
    char_from_room(ch);
    char_to_room(ch, new_room);

    if (is_death) {
        raw_kill(ch, NULL, 0);
        return 0;
    }
    do_look(ch, "", 0, 0, SCMD_LOOK_BRIEF);

    for (tmpch = ch->mount_data.rider; tmpch; tmpch = tmpch->mount_data.next_rider) {
        do_look(tmpch, "", 0, CMD_LOOK, SCMD_LOOK_BRIEF);
    }
    /* now forming and sending the "enter" message */

    sprintf(buff, " entering from %s, riding on ", refer_dirs[rev_dir[dir]]);
    sprintf(buff2, " entering from %s, riding on ", refer_dirs[rev_dir[dir]]);

    for (tmpvict = world[new_room].people; tmpvict; tmpvict = tmpvict->next_in_room) {

        should_show = 1;

        if ((tmpvict == ch) || (tmpvict->mount_data.mount == ch))
            should_show = 0;
        if (ch->mount_data.rider && !PRF_FLAGGED(tmpvict, PRF_SPAM)) {
            if ((ch->mount_data.rider->master == tmpvict) || (tmpvict->master && (ch->mount_data.rider->master == tmpvict->master)))
                should_show = 0;
        }
        if (GET_POS(tmpvict) <= POSITION_SLEEPING)
            should_show = 0;

        if (should_show)
            show_mount_to_char(ch, tmpvict, buff, buff2, FALSE);
    }

    for (tmpch = ch->mount_data.rider; tmpch; tmpch = tmpch2) {
        tmpch2 = tmpch->mount_data.next_rider;

        special(tmpch, rev_dir[dir] + 1, "", SPECIAL_ENTER, 0);

        call_trigger(ON_ENTER, (void*)&world[tmpch->in_room], (void*)tmpch, 0);
    }
    if (special(ch, rev_dir[dir] + 1, "", SPECIAL_ENTER, 0))
        return 0;

    call_trigger(ON_ENTER, (void*)&world[ch->in_room], (void*)ch, 0);

    return 1;
}

void parse_container_for_stay_zone(char_data* ch, obj_data* container, const int room)
{
    obj_data* next_item = nullptr;
    for (obj_data* item = container->contains; item; item = next_item) {
        next_item = item->next_content;

        if (GET_ITEM_TYPE(item) == ITEM_CONTAINER) {
            parse_container_for_stay_zone(ch, item, room);
        }

        if (IS_OBJ_STAT(item, ITEM_STAY_ZONE)) {
            sprintf(buf, "You drop %s.\n\r", OBJS(item, ch));
            send_to_char(buf, ch);
            sprintf(buf, "$n drops $p.");
            act(buf, TRUE, ch, item, 0, TO_ROOM);
            obj_from_obj(item);
            obj_to_room(item, room);
        }
    }
};

void prohibit_item_stay_zone_move(char_data* ch, int room)
{
    // Check for gear that character is wearing.
    for (int gear_index = 0; gear_index < MAX_WEAR; gear_index++) {
        obj_data* item = ch->equipment[gear_index];
        if (item == nullptr) {
            continue;
        }

        if (IS_OBJ_STAT(ch->equipment[gear_index], ITEM_STAY_ZONE)) {
            obj_data* item = unequip_char(ch, gear_index);
            obj_to_room(item, room);
        }
    }

    // Check inventory of character.
    obj_data* next_item = nullptr;
    for (obj_data* item = ch->carrying; item; item = next_item) {
        next_item = item->next_content;
        if (GET_ITEM_TYPE(item) == ITEM_CONTAINER) {
            parse_container_for_stay_zone(ch, item, room);
        }

        if (IS_OBJ_STAT(item, ITEM_STAY_ZONE)) {
            sprintf(buf, "You drop %s.\n\r", OBJS(item, ch));
            send_to_char(buf, ch);
            sprintf(buf, "$n drops $p.");
            act(buf, TRUE, ch, item, 0, TO_ROOM);
            obj_from_char(item);
            obj_to_room(item, room);
        }
    }
}

/*
 * Reduces the movement cost of rooms based on character race and sector type.
 */
int racial_movement_reduction(const int room_type, const int race, const int movement_cost)
{
    // No love for third side or Olog-Hais
    if (race == RACE_HARADRIM || race == RACE_MAGUS || race == RACE_OLOGHAI) {
        return movement_cost;
    }

    // Dwarves be in mountains
    if (room_type == SECT_MOUNTAIN && race == RACE_DWARF) {
        return movement_cost / 2;
    }

    // Forests for Elves and Bears of course
    if ((room_type == SECT_DENSE_FOREST || room_type == SECT_FOREST) && (race == RACE_WOOD || race == RACE_BEORNING)) {
        return movement_cost / 2;
    }

    // Orcs and Uruk-Hais are swamp rats
    if (room_type == SECT_SWAMP && (race == RACE_URUK || race == RACE_ORC)) {
        return movement_cost / 2;
    }

    // Hobbits love them holes in hills
    if (room_type == SECT_HILLS && race == RACE_HOBBIT) {
        return movement_cost / 2;
    }

    // Humans riding their horses in the fields.
    if (room_type == SECT_FIELD && race == RACE_HUMAN) {
        return movement_cost / 2;
    }

    return movement_cost;
}

bool is_exit_valid(const room_direction_data room_direction)
{
    auto to_room = room_direction.to_room;
    auto exit_info = room_direction.exit_info;

    if (!to_room || to_room == NOWHERE) {
        return false;
    }

    if (exit_info == NOWHERE) {
        return false;
    }

    if (IS_SET(exit_info, EX_ISBROKEN)) {
        return false;
    }

    if (IS_SET(exit_info, EX_NOWALK)) {
        return false;
    }

    if (IS_SET(exit_info, EX_ISHIDDEN)) {
        return false;
    }

    return true;
}

void msdp_room_update(char_data* ch)
{
    if (utils::is_npc(*ch)) {
        return;
    }

    if (!ch->desc->pProtocol) {
        return;
    }

    if (ch->in_room >= 0) {
        return;
    }

    MSDPSetString(ch->desc, eMSDP_ROOM_NAME, world[ch->desc->character->in_room].name);
    MSDPSetNumber(ch->desc, eMSDP_ROOM_VNUM, world[ch->desc->character->in_room].number);

    std::string msdp_room = {};
    msdp_room += (char)MSDP_VAR;
    msdp_room += "VNUM";
    msdp_room += (char)MSDP_VAL;
    msdp_room += std::to_string(world[ch->in_room].number);
    msdp_room += (char)MSDP_VAR;
    msdp_room += "NAME";
    msdp_room += (char)MSDP_VAL;
    msdp_room += world[ch->in_room].name;
    msdp_room += (char)MSDP_VAR;
    msdp_room += "EXITS";
    msdp_room += (char)MSDP_VAL;
    msdp_room += (char)MSDP_ARRAY_OPEN;
    std::string exits_names = {};
    const std::string direction[NUM_OF_DIRS] = { "n", "e", "s", "w", "u", "d" };

    for (int exits = 0; exits < NUM_OF_DIRS; exits++) {
        if (ch->in_room == NOWHERE) {
            break;
        }

        if (world[ch->in_room].dir_option[exits] == nullptr) {
            continue;
        }

        const room_direction_data room_direction = *world[ch->in_room].dir_option[exits];

        if (is_exit_valid(room_direction)) {
            msdp_room += (char)MSDP_VAL;
            msdp_room += std::to_string(world[room_direction.to_room].number);
            exits_names += (char)MSDP_VAL;
            exits_names += direction[exits];
        }
    }
    msdp_room += (char)MSDP_ARRAY_CLOSE;
    msdp_room += (char)MSDP_VAR;
    msdp_room += "TERRAIN";
    msdp_room += (char)MSDP_VAL;

    extern char* sector_types[];
    msdp_room += sector_types[world[ch->in_room].sector_type];

    // Room exits need to be sent first before anything else
    MSDPSetArray(ch->desc, eMSDP_ROOM_EXITS, exits_names.c_str());
    MSDPSend(ch->desc, eMSDP_ROOM_EXITS);
    MSDPSetTable(ch->desc, eMSDP_ROOM, msdp_room.c_str());

    MSDPUpdate(ch->desc);
}

ACMD(do_move)
/* do_move is under construction to account for riding... !!!!  */
{
    int was_in, res_flag, to_room, tmp, need_move, tmp_move;
    char is_death, is_fol;
    struct follow_type *k, *next_dude;
    struct char_data* tmpvict;
    follow_type fol_people;
    waiting_type tmpwtl;
    int mounts;

    if (IS_AFFECTED(ch, AFF_HAZE) && number(1, 4) == 1) {
        send_to_char("You feel dizzy, and move randomly.\n\r", ch);
        cmd = number(1, NUM_OF_DIRS);
    }
    --cmd;

    if ((ch->delay.wait_value > 0) && (ch->delay.priority <= 30)) {
        send_to_char("You could not concentrate anymore.\n\r", ch);
        abort_delay(ch);
    }

    if ((is_fol = (ch->followers != 0)))
        fol_people = *ch->followers;

    if (IS_RIDDEN(ch)) {
        perform_move_mount(ch, cmd);
        return;
    }

    if (!world[ch->in_room].dir_option[cmd]) {
        send_to_char("You cannot go that way.\n\r", ch);
        return;
    } else if (world[ch->in_room].dir_option[cmd]->to_room == NOWHERE) {
        send_to_char("You cannot go that way.\n\r", ch);
        return;
    } else { /* Direction is possible */
        if (IS_NPC(ch) && (subcmd == SCMD_MOVING) && IS_SET(EXIT(ch, cmd)->exit_info, EX_ISDOOR | EX_CLOSED) && !IS_SET(EXIT(ch, cmd)->exit_info, EX_ISHIDDEN | EX_LOCKED)) {
            tmpwtl.cmd = CMD_OPEN;
            tmpwtl.targ1.type = TARGET_DIR;
            tmpwtl.targ1.ch_num = cmd;
            tmpwtl.targ2.type = TARGET_NONE;
            do_open(ch, "", &tmpwtl, CMD_OPEN, 0);
        }

        if (!CAN_GO(ch, cmd)) {
            if (IS_SET(EXIT(ch, cmd)->exit_info, EX_ISHIDDEN) && !PRF_FLAGGED(ch, PRF_HOLYLIGHT)) {
                send_to_char("You cannot go that way.\n\r", ch);
                return;
            } else if (EXIT(ch, cmd)->keyword) {
                if (IS_SHADOW(ch))
                    sprintf(buf2, "You cannot pass through the %s.\n\r",
                        fname(EXIT(ch, cmd)->keyword));
                else
                    sprintf(buf2, "The %s seems to be closed.\n\r", fname(EXIT(ch, cmd)->keyword));
                send_to_char(buf2, ch);
                return;
            } else {
                send_to_char("It seems to be closed.\n\r", ch);
                return;
            }
        } else if (EXIT(ch, cmd)->to_room == NOWHERE) {
            send_to_char("You cannot go that way.\n\r", ch);
            return;
        }
        if (IS_AFFECTED(ch, AFF_CHARM) && (ch->master) && (ch->in_room == ch->master->in_room) && (subcmd != SCMD_FOLLOW && subcmd != SCMD_FLEE)) {
            send_to_char("The thought of leaving your master makes you weep.\n\r", ch);
            act("$n bursts into tears.", FALSE, ch, 0, 0, TO_ROOM);
            return;
        }

        // Exit does exist, trying to move there

        to_room = EXIT(ch, cmd)->to_room;
        is_death = IS_SET(world[to_room].room_flags, DEATH);
        was_in = ch->in_room;

        bool different_zone = world[was_in].zone != world[to_room].zone;

        if (!IS_RIDING(ch)) {
            res_flag = check_simple_move(ch, cmd, &need_move, subcmd);

            if (subcmd == SCMD_FOLLOW) {
                if (res_flag != 0) {
                    act("ACK! $n could not follow, you lost $m!", FALSE, ch, 0, ch->master,
                        TO_VICT);
                    act("ACK! You could not follow $M!", FALSE, ch, 0, ch->master, TO_CHAR);
                } else
                    act("You follow $N.\n\r", FALSE, ch, 0, ch->master, TO_CHAR);
            }

            switch (res_flag) {
            case 0:
                break;
            case 1:
                return;
            case 2:
                send_to_char("You need to swim better or find a boat to go there.\n\r", ch);
                return;
            case 3:
                send_to_char("You are too exhausted to move.\n\r", ch);
                return;
            case 4:
                return;
            case 5:
                return;
            case 6:
                send_to_char("You do not control your mount.\n\r", ch);
                return;
            case 7:
                send_to_char("Can not go there mounted.\n\r", ch);
                return;
            case 8:
                send_to_char("You are prevented from entering there.\n\r", ch);
                return;
            case 9:
                send_to_char("You cannot go that way.\n\r", ch);
                return;
            }

            // At this point, check for common orc "followers" to move before their
            // master does, just for group randomness.

            if (is_fol) {
                for (k = &fol_people; k; k = next_dude) {
                    next_dude = k->next;
                    if ((was_in == k->follower->in_room) && (GET_POS(k->follower) >= POSITION_STANDING) && (IS_NPC(k->follower) && MOB_FLAGGED(k->follower, MOB_ORC_FRIEND) && MOB_FLAGGED(k->follower, MOB_PET)) && (number(1, 100) > 50)) {
                        // act("$n moves ahead of you.", FALSE, k->follower, 0, ch, TO_VICT);
                        bzero((char*)&tmpwtl, sizeof(waiting_type));
                        tmpwtl.cmd = cmd + 1;
                        tmpwtl.subcmd = SCMD_FOLLOW;
                        command_interpreter(k->follower, argument, &tmpwtl);
                    }
                }
            }

            if (!IS_AFFECTED(ch, AFF_SNEAK) || (subcmd == SCMD_FLEE) || number(0, 125) > GET_SKILL(ch, SKILL_SNEAK) + get_real_stealth(ch)) {
                sprintf(buf2, " leaves %s.", dirs[cmd]);
                for (tmpvict = world[ch->in_room].people; tmpvict;
                     tmpvict = tmpvict->next_in_room) {
                    if ((ch == tmpvict) || !CAN_SEE(tmpvict, ch) || ((ch->master == tmpvict) && IS_NPC(ch) && MOB_FLAGGED(ch, MOB_ORC_FRIEND)))
                        continue;
                    show_char_to_char(ch, tmpvict, 0, buf2);
                }
            } else if (IS_AFFECTED(ch, AFF_SNEAK)) {
                snuck_out(ch);
                need_move *= 1.50;
            }

            const auto room_type = world[ch->in_room].sector_type;
            const auto race = ch->player.race;

            need_move = racial_movement_reduction(room_type, race, need_move);

            // Here setting his tracks...

            if ((IS_NPC(ch) || (GET_RACE(ch) != RACE_GOD)) && !IS_SHADOW(ch) && !IS_AFFECTED(ch, AFF_FLYING)) { // &&
                //!(world[ch->in_room].sector_type == SECT_WATER_NOSWIM) &&
                //!(world[ch->in_room].sector_type == SECT_WATER_SWIM) ) now hunt will work in water

                if ((subcmd == SCMD_STALK) && (GET_KNOWLEDGE(ch, SKILL_STALK) > number(0, 119))) {

                    send_to_char("You have found sure foothold.\n\r", ch);
                    tmp = -1;
                } else
                    tmp = number(0, NUM_OF_TRACKS - 1);

                if (tmp >= 0) {
                    if (IS_NPC(ch))
                        world[ch->in_room].room_track[tmp].char_number = ch->nr;
                    else
                        world[ch->in_room].room_track[tmp].char_number = -GET_RACE(ch);

                    world[ch->in_room].room_track[tmp].data = time_info.hours * 8 + cmd;
                    world[ch->in_room].room_track[tmp].condition = 0;
                }
            }

            if (utils::is_affected_by_spell(*ch, SKILL_MARK)) {
                set_blood_trail(ch, cmd);
            }

            // Check for item_stay_zone in players inventory and equipment.
            if (different_zone) {
                prohibit_item_stay_zone_move(ch, was_in);
            }

            char_from_room(ch);
            char_to_room(ch, to_room);
            do_look(ch, "\0", 0, 0, 0);
            GET_MOVE(ch) -= need_move;
            if (!IS_AFFECTED(ch, AFF_SNEAK) || (subcmd == SCMD_FLEE) || number(0, 100) > GET_SKILL(ch, SKILL_SNEAK) + get_real_stealth(ch) - 25) {
                sprintf(buf2, " enters from %s.", refer_dirs[rev_dir[cmd]]);
                for (tmpvict = world[ch->in_room].people; tmpvict;
                     tmpvict = tmpvict->next_in_room) {
                    if ((tmpvict == ch) || !CAN_SEE(tmpvict, ch))
                        continue;
                    if (!PRF_FLAGGED(ch, PRF_SPAM) && (subcmd == SCMD_FOLLOW) && ch->master && ((tmpvict->master == ch->master) || (tmpvict == ch->master)))
                        continue;
                    show_char_to_char(ch, tmpvict, 0, buf2);
                }
            } else if (IS_AFFECTED(ch, AFF_SNEAK))
                snuck_in(ch);

            if (!ch->spec_busy) {
                special(ch, rev_dir[cmd] + 1, "", SPECIAL_ENTER, 0);
            }

            call_trigger(ON_ENTER, (void*)&world[ch->in_room], (void*)ch, 0);

            if (is_death)
                raw_kill(ch, NULL, 0);
        } else { // riding...
            if ((ch->mount_data.mount)->mount_data.rider != ch) {
                send_to_char("You do not control your mount.\n\r", ch);
                return;
            }
            res_flag = check_simple_move(ch, cmd, &need_move, subcmd);

            if (subcmd == SCMD_FOLLOW) {
                if (res_flag != 0) {
                    act("ACK! $n could not follow, you lost $m!", TRUE, ch, 0, ch->master, TO_VICT);
                    act("ACK! You could not follow $M!", TRUE, ch, 0, ch->master, TO_CHAR);
                } else
                    act("You follow $N.\n\r", FALSE, ch, 0, ch->master, TO_CHAR);
            }
            switch (res_flag) {
            case 0:
                break;
            case 1:
                return;
            case 2:
                send_to_char("Your mount cannot swim.\n\r", ch);
                return;
            case 3:
                send_to_char("You are too exhausted to ride.\n\r", ch);
                return;
            case 4:
                send_to_char("Your mount is too exhausted to move.\n\r", ch);
                return;
            case 5:
                send_to_char("Your mount would not go there.\n\r", ch);
                return;
            case 6:
                send_to_char("You do not control your mount.\n\r", ch);
                return;
            case 7:
                send_to_char("Can not go there mounted.\n\r", ch);
                return;
            case 8:
                send_to_char("You cannot go that way.\n\r", ch);
                return;
            }
            // GET_MOVE(ch) -= need_move;       // This belongs below.
            res_flag = check_simple_move(ch->mount_data.mount, cmd, &tmp_move, SCMD_MOUNT);

            if (subcmd == SCMD_FOLLOW) {
                if (res_flag != 0) {
                    act("ACK! $n could not follow riding, you lost $m!", TRUE, ch, 0, ch->master,
                        TO_VICT);
                    act("ACK! You could not follow $M riding!", TRUE, ch, 0, ch->master, TO_CHAR);
                }
                // 	else
                // 	  act("You follow $N.\n\r", FALSE, ch, 0, ch->master, TO_CHAR);
            }
            switch (res_flag) {
            case 0:
                break;
            case 1:
                return;
            case 2:
                send_to_char("Your mount cannot swim.\n\r", ch);
                return;
            case 3:
                send_to_char("Your mount is too exhausted to move.\n\r", ch);
                return;
            case 5:
                send_to_char("Your mount would not go there.\n\r", ch);
                return;
            case 6:
                send_to_char("Your mount does not control its mount (\?\?).\n\r", ch);
                return;
            case 7:
                send_to_char("Can not go there mounted.\n\r", ch);
                return;
            case 8:
                send_to_char("You cannot go that way.\n\r", ch);
                return;
            }
            GET_MOVE(ch) -= need_move;

            // At this point, check for common orc "followers" to move before their
            // master does, just for group randomness.

            if (is_fol) {
                for (k = &fol_people; k; k = next_dude) {
                    next_dude = k->next;
                    if ((was_in == k->follower->in_room) && (GET_POS(k->follower) >= POSITION_STANDING) && (IS_NPC(k->follower) && MOB_FLAGGED(k->follower, MOB_ORC_FRIEND) && MOB_FLAGGED(k->follower, MOB_PET)) && (number(1, 100) > 50)) {
                        // act("$n moves ahead of you.", FALSE, k->follower, 0, ch, TO_VICT);
                        bzero((char*)&tmpwtl, sizeof(waiting_type));
                        tmpwtl.cmd = cmd + 1;
                        tmpwtl.subcmd = SCMD_FOLLOW;
                        command_interpreter(k->follower, argument, &tmpwtl);
                    }
                }
            }

            GET_MOVE(ch->mount_data.mount) -= tmp_move;

            sprintf(buf2, "$N has forced you %s.\n\r", dirs[cmd]);
            act(buf2, FALSE, ch->mount_data.mount, 0, ch, TO_CHAR);

            res_flag = perform_move_mount(ch->mount_data.mount, cmd);
        }

        msdp_room_update(ch);

        mounts = 0;
        if (IS_RIDING(ch))
            mounts++;
        if (is_fol) { /* If success move followers */
            for (k = &fol_people; k; k = next_dude) {
                next_dude = k->next;
                if ((was_in == k->follower->in_room) && (GET_POS(k->follower) >= POSITION_STANDING)) {
                    //	  act("You follow $N.\n\r", FALSE, k->follower, 0, ch, TO_CHAR);

                    bzero((char*)&tmpwtl, sizeof(waiting_type));
                    tmpwtl.cmd = cmd + 1;
                    tmpwtl.subcmd = SCMD_FOLLOW;
                    //	  do_move(k->follower, argument, &tmpwtl, cmd + 1, SCMD_FOLLOW);
                    // Can not lead too many mounts:
                    if (IS_NPC(k->follower) && (MOB_FLAGGED(k->follower, MOB_MOUNT))) {
                        mounts++;
                        if (mounts <= 2 || number(1, 20) != 20)
                            command_interpreter(k->follower, argument, &tmpwtl);
                        else
                            send_to_char("One of your mounts has fallen behind!\r\n", ch);
                    } else
                        command_interpreter(k->follower, argument, &tmpwtl);
                }
            }
        }
    }
}

int find_door(struct char_data* ch, char* type, char* dir)
{
    int door;
    char* dirs[] = { "north", "east", "south", "west", "up", "down", "\n" };

    if (*dir) /* a direction was specified */ {
        if ((door = search_block(dir, dirs, FALSE)) == -1) /* Partial Match */ {
            send_to_char("That's not a direction.\n\r", ch);
            return (-1);
        }

        if (EXIT(ch, door) && (EXIT(ch, door)->to_room != NOWHERE))
            if (EXIT(ch, door)->keyword)
                if (isname(type, EXIT(ch, door)->keyword))
                    return (door);
                else {
                    sprintf(buf2, "I see no %s there.\n\r", type);
                    send_to_char(buf2, ch);
                    return (-1);
                }
            else
                return (door);
        else {
            send_to_char("There is no passage in that direction.\n\r", ch);
            return (-1);
        }
    } else /* try to locate the keyword */ {
        for (door = 0; door < NUM_OF_DIRS; door++)
            if (EXIT(ch, door) && (EXIT(ch, door)->to_room != NOWHERE))
                if (EXIT(ch, door)->keyword)
                    if (isname(type, EXIT(ch, door)->keyword))
                        return (door);

        sprintf(buf2, "I see no %s here.\n\r", type);
        send_to_char(buf2, ch);
        return (-1);
    }
}

ACMD(do_open)
{
    int door, other_room, d1, d2;
    char type[MAX_INPUT_LENGTH], dir[MAX_INPUT_LENGTH];
    struct room_direction_data* back;
    struct obj_data* obj;
    struct char_data* victim;

    if (IS_SHADOW(ch)) {
        send_to_char("You are too insubstantial to do that.\n\r", ch);
        return;
    }

    half_chop(argument, buf1, buf2);

    back = 0;
    obj = 0;
    door = -1;

    if ((wtl && (wtl->targ1.type == TARGET_OBJ)) && !(*buf2)) {
        obj = wtl->targ1.ptr.obj;
    } else if (wtl && (wtl->targ1.type == TARGET_DIR)) {
        d1 = wtl->targ1.ch_num;
        if (!EXIT(ch, d1)) {
            send_to_char("Can't open anything in that direction.\n\r", ch);
            return;
        }
        if (wtl->targ2.type == TARGET_DIR) {
            d2 = wtl->targ2.ch_num;
            if (!EXIT(ch, d2)) {
                send_to_char("Can't open anything in that direction.\n\r", ch);
                return;
            }
            sscanf(EXIT(ch, d1)->keyword, "%s", type);
            if (str_cmp(EXIT(ch, d2)->keyword, type)) {
                sprintf(buf, "No %s in that direction.\n\r", type);
                send_to_char(buf, ch);
                return;
            }
            door = d2;
        } else
            door = d1;
    } else {

        if (wtl && (wtl->targ1.type == TARGET_TEXT)) {
            argument = wtl->targ1.ptr.text->text;
        }

        argument_interpreter(argument, type, dir);

        if (!*type)
            send_to_char("Open what?\n\r", ch);
        else if (generic_find(argument, FIND_OBJ_INV | FIND_OBJ_ROOM, ch, &victim, &obj)) {

            // this is an object
        } else
            door = find_door(ch, type, dir);
    }

    /* perhaps it is a door */

    if (door >= 0) {
        if (IS_SET(EXIT(ch, door)->exit_info, EX_ISBROKEN))
            send_to_char("It is broken.\n\r", ch);
        else if (!IS_SET(EXIT(ch, door)->exit_info, EX_ISDOOR))
            send_to_char("That's impossible, I'm afraid.\n\r", ch);
        else if (!IS_SET(EXIT(ch, door)->exit_info, EX_CLOSED))
            send_to_char("It's already open!\n\r", ch);
        else if (IS_SET(EXIT(ch, door)->exit_info, EX_LOCKED))
            send_to_char("It seems to be locked.\n\r", ch);
        else if (IS_SET(EXIT(ch, door)->exit_info, EX_LEVER))
            send_to_char("It will not budge.\n\r", ch);

        else {
            REMOVE_BIT(EXIT(ch, door)->exit_info, EX_CLOSED);
            if (EXIT(ch, door)->keyword)
                act("$n opens the $F.", FALSE, ch, 0, EXIT(ch, door)->keyword, TO_ROOM);
            else
                act("$n opens the door.", FALSE, ch, 0, 0, TO_ROOM);
            send_to_char("Ok.\n\r", ch);
            /* now for opening the OTHER side of the door! */
            if ((other_room = EXIT(ch, door)->to_room) != NOWHERE)
                if ((back = world[other_room].dir_option[rev_dir[door]]))
                    if (back->to_room == ch->in_room) {
                        REMOVE_BIT(back->exit_info, EX_CLOSED);
                        if (back->keyword) {
                            sprintf(buf, "The %s is opened from the other side.\n\r",
                                fname(back->keyword));
                            send_to_room(buf, EXIT(ch, door)->to_room);
                        } else
                            send_to_room("The door is opened from the other side.\n\r",
                                EXIT(ch, door)->to_room);
                    }
        }
    }
    if (obj) {
        if (obj->obj_flags.type_flag != ITEM_CONTAINER)
            send_to_char("That's not a container.\n\r", ch);
        else if (!IS_SET(obj->obj_flags.value[1], CONT_CLOSED))
            send_to_char("But it's already open!\n\r", ch);
        else if (!IS_SET(obj->obj_flags.value[1], CONT_CLOSEABLE))
            send_to_char("You can't do that.\n\r", ch);
        else if (IS_SET(obj->obj_flags.value[1], CONT_LOCKED))
            send_to_char("It seems to be locked.\n\r", ch);
        else {
            REMOVE_BIT(obj->obj_flags.value[1], CONT_CLOSED);
            send_to_char("Ok.\n\r", ch);
            act("$n opens $p.", FALSE, ch, obj, 0, TO_ROOM);
        }
    }
}

ACMD(do_close)
{
    int door, other_room, d1, d2;
    char type[MAX_INPUT_LENGTH], dir[MAX_INPUT_LENGTH];
    struct room_direction_data* back;
    struct obj_data* obj;
    struct char_data* victim;

    if (IS_SHADOW(ch)) {
        send_to_char("You are too insubstantial to do that.\n\r", ch);
        return;
    }

    back = 0;
    obj = 0;
    door = -1;

    half_chop(argument, buf1, buf2);

    if ((wtl && (wtl->targ1.type == TARGET_OBJ)) && !(*buf2)) {
        obj = wtl->targ1.ptr.obj;
    }

    else if (wtl && (wtl->targ1.type == TARGET_DIR)) {
        d1 = wtl->targ1.ch_num;
        if (!EXIT(ch, d1)) {
            send_to_char("Can't close anything in that direction.\n\r", ch);
            return;
        }
        if (wtl->targ2.type == TARGET_DIR) {
            d2 = wtl->targ2.ch_num;
            if (!EXIT(ch, d2)) {
                send_to_char("Can't close anything in that direction.\n\r", ch);
                return;
            }
            sscanf(EXIT(ch, d1)->keyword, "%s", type);
            if (str_cmp(EXIT(ch, d2)->keyword, type)) {
                sprintf(buf, "No %s in that direction.\n\r", type);
                send_to_char(buf, ch);
                return;
            }
            door = d2;
        } else
            door = d1;
    } else {

        if (wtl && (wtl->targ1.type == TARGET_TEXT)) {
            argument = wtl->targ1.ptr.text->text;
        }

        argument_interpreter(argument, type, dir);

        if (!*type)
            send_to_char("Close what?\n\r", ch);
        else if (generic_find(argument, FIND_OBJ_INV | FIND_OBJ_ROOM, ch, &victim, &obj)) {

            /* this is an object */
        } else
            door = find_door(ch, type, dir);
    }
    /* Or a door */

    if (door >= 0) {
        if (IS_SET(EXIT(ch, door)->exit_info, EX_ISBROKEN))
            send_to_char("It is broken.\n\r", ch);
        else if (!IS_SET(EXIT(ch, door)->exit_info, EX_ISDOOR))
            send_to_char("There is nothing to close.\n\r", ch);
        else if (IS_SET(EXIT(ch, door)->exit_info, EX_CLOSED))
            send_to_char("It's already closed!\n\r", ch);
        else if (IS_SET(EXIT(ch, door)->exit_info, EX_LEVER))
            send_to_char("It will not budge.\n\r", ch);

        else {
            SET_BIT(EXIT(ch, door)->exit_info, EX_CLOSED);
            if (EXIT(ch, door)->keyword)
                act("$n closes the $F.", 0, ch, 0, EXIT(ch, door)->keyword, TO_ROOM);
            else
                act("$n closes the door.", FALSE, ch, 0, 0, TO_ROOM);
            send_to_char("Ok.\n\r", ch);
            /* now for closing the other side, too */
            if ((other_room = EXIT(ch, door)->to_room) != NOWHERE)
                if ((back = world[other_room].dir_option[rev_dir[door]]))
                    if ((back->to_room == ch->in_room) && IS_SET(back->exit_info, EX_ISDOOR)) {
                        SET_BIT(back->exit_info, EX_CLOSED);
                        if (back->keyword) {
                            sprintf(buf, "The %s closes quietly.\n\r", back->keyword);
                            send_to_room(buf, EXIT(ch, door)->to_room);
                        } else
                            send_to_room("The door closes quietly.\n\r", EXIT(ch, door)->to_room);
                    }
        }
    }
    if (obj) {
        if (obj->obj_flags.type_flag != ITEM_CONTAINER)
            send_to_char("That's not a container.\n\r", ch);
        else if (IS_SET(obj->obj_flags.value[1], CONT_CLOSED))
            send_to_char("But it's already closed!\n\r", ch);
        else if (!IS_SET(obj->obj_flags.value[1], CONT_CLOSEABLE))
            send_to_char("That's impossible.\n\r", ch);
        else {
            SET_BIT(obj->obj_flags.value[1], CONT_CLOSED);
            send_to_char("Ok.\n\r", ch);
            act("$n closes $p.", FALSE, ch, obj, 0, TO_ROOM);
        }
    }
}

bool is_key(obj_data* item) { return item->obj_flags.type_flag == ITEM_KEY; }

/* returns NULL if the character does not have the key (or it is broken),
                   a pointer to the key if the character does have the key  */
obj_data* has_key(char_data* character, int key)
{
    for (obj_data* item = character->carrying; item; item = item->next_content)
        if (obj_index[item->item_number].virt == key)
            if (!(IS_SET(item->obj_flags.extra_flags, ITEM_BROKEN)))
                if (is_key(item))
                    return (item);

    if (character->equipment[HOLD])
        if (obj_index[character->equipment[HOLD]->item_number].virt == key)
            if (!(IS_SET(character->equipment[HOLD]->obj_flags.extra_flags, ITEM_BROKEN)))
                if (is_key(character->equipment[HOLD]))
                    return (character->equipment[HOLD]);

    return NULL;
}

void check_break_key(struct obj_data* obj, struct char_data* ch)
{
    if (!(IS_SET(obj->obj_flags.extra_flags, ITEM_BREAKABLE)) || IS_SET(obj->obj_flags.extra_flags, ITEM_BROKEN))
        return;
    act("Unfortunately, $p breaks as $n uses it!", FALSE, ch, obj, 0, TO_ROOM);
    act("Unfortunately, $p breaks as you use it!", FALSE, ch, obj, 0, TO_CHAR);
    SET_BIT(obj->obj_flags.extra_flags, ITEM_BROKEN);
}

ACMD(do_lock)
{
    int door, other_room;
    char type[MAX_INPUT_LENGTH], dir[MAX_INPUT_LENGTH];
    struct room_direction_data* back;
    struct obj_data* obj;
    struct char_data* victim;

    if (IS_SHADOW(ch)) {
        send_to_char("You are too insubstantial to do that.\n\r", ch);
        return;
    }

    argument_interpreter(argument, type, dir);

    if (!*type)
        send_to_char("Lock what?\n\r", ch);
    else if (generic_find(argument, FIND_OBJ_INV | FIND_OBJ_ROOM, ch, &victim, &obj))

        /* this is an object */

        if (obj->obj_flags.type_flag != ITEM_CONTAINER)
            send_to_char("That's not a container.\n\r", ch);
        else if (!IS_SET(obj->obj_flags.value[1], CONT_CLOSED))
            send_to_char("Maybe you should close it first...\n\r", ch);
        else if (obj->obj_flags.value[2] < 0)
            send_to_char("That thing can't be locked.\n\r", ch);
        else if (!has_key(ch, obj->obj_flags.value[2]))
            send_to_char("You don't seem to have the proper key.\n\r", ch);
        else if (IS_SET(obj->obj_flags.value[1], CONT_LOCKED))
            send_to_char("It is locked already.\n\r", ch);
        else {
            SET_BIT(obj->obj_flags.value[1], CONT_LOCKED);
            send_to_char("*Cluck*\n\r", ch);
            act("$n locks $p - 'cluck', it says.", FALSE, ch, obj, 0, TO_ROOM);
        }
    else if ((door = find_door(ch, type, dir)) >= 0)

        /* a door, perhaps */

        if (IS_SET(EXIT(ch, door)->exit_info, EX_ISBROKEN))
            send_to_char("It is broken.\n\r", ch);
        else if (!IS_SET(EXIT(ch, door)->exit_info, EX_ISDOOR))
            send_to_char("That's absurd.\n\r", ch);
        else if (!IS_SET(EXIT(ch, door)->exit_info, EX_CLOSED))
            send_to_char("You have to close it first, I'm afraid.\n\r", ch);
        else if (EXIT(ch, door)->key < 0)
            send_to_char("There does not seem to be any keyholes.\n\r", ch);
        else if (!has_key(ch, EXIT(ch, door)->key))
            send_to_char("You don't have the proper key.\n\r", ch);
        else if (IS_SET(EXIT(ch, door)->exit_info, EX_LOCKED))
            send_to_char("It's already locked!\n\r", ch);
        else {
            SET_BIT(EXIT(ch, door)->exit_info, EX_LOCKED);
            if (EXIT(ch, door)->keyword)
                act("$n locks the $F.", 0, ch, 0, EXIT(ch, door)->keyword, TO_ROOM);
            else
                act("$n locks the door.", FALSE, ch, 0, 0, TO_ROOM);
            send_to_char("*Click*\n\r", ch);
            /* now for locking the other side, too */
            if ((other_room = EXIT(ch, door)->to_room) != NOWHERE)
                if ((back = world[other_room].dir_option[rev_dir[door]]))
                    if ((back->to_room == ch->in_room) && IS_SET(back->exit_info, EX_ISDOOR))
                        SET_BIT(back->exit_info, EX_LOCKED);
        }
}

ACMD(do_unlock)
{
    int door, other_room;
    char type[MAX_INPUT_LENGTH], dir[MAX_INPUT_LENGTH];
    struct room_direction_data* back;
    struct obj_data* obj;
    struct char_data* victim;

    if (IS_SHADOW(ch)) {
        send_to_char("You are too insubstantial to do that.\n\r", ch);
        return;
    }

    argument_interpreter(argument, type, dir);

    if (!*type)
        send_to_char("Unlock what?\n\r", ch);
    else if (generic_find(argument, FIND_OBJ_INV | FIND_OBJ_ROOM, ch, &victim, &obj))

        /* this is an object */

        if (obj->obj_flags.type_flag != ITEM_CONTAINER)
            send_to_char("That's not a container.\n\r", ch);
        else if (!IS_SET(obj->obj_flags.value[1], CONT_CLOSED))
            send_to_char("Silly - it ain't even closed!\n\r", ch);
        else if (obj->obj_flags.value[2] < 0)
            send_to_char("Odd - you can't seem to find a keyhole.\n\r", ch);
        else if (!has_key(ch, obj->obj_flags.value[2]))
            send_to_char("You don't seem to have the proper key.\n\r", ch);
        else if (!IS_SET(obj->obj_flags.value[1], CONT_LOCKED))
            send_to_char("Oh.. it wasn't locked, after all.\n\r", ch);
        else {
            REMOVE_BIT(obj->obj_flags.value[1], CONT_LOCKED);
            send_to_char("*Click*\n\r", ch);
            act("$n unlocks $p.", FALSE, ch, obj, 0, TO_ROOM);
            check_break_key(has_key(ch, obj->obj_flags.value[2]), ch);
        }
    else if ((door = find_door(ch, type, dir)) >= 0)

        /* it is a door */

        if (IS_SET(EXIT(ch, door)->exit_info, EX_ISBROKEN))
            send_to_char("It is broken.\n\r", ch);
        else if (!IS_SET(EXIT(ch, door)->exit_info, EX_ISDOOR))
            send_to_char("That's absurd.\n\r", ch);
        else if (!IS_SET(EXIT(ch, door)->exit_info, EX_CLOSED))
            send_to_char("Heck.. it ain't even closed!\n\r", ch);
        else if (EXIT(ch, door)->key < 0)
            send_to_char("You can't seem to spot any keyholes.\n\r", ch);
        else if (!has_key(ch, EXIT(ch, door)->key))
            send_to_char("You do not have the proper key for that.\n\r", ch);
        else if (!IS_SET(EXIT(ch, door)->exit_info, EX_LOCKED))
            send_to_char("It's already unlocked, it seems.\n\r", ch);
        else {
            REMOVE_BIT(EXIT(ch, door)->exit_info, EX_LOCKED);
            if (EXIT(ch, door)->keyword)
                act("$n unlocks the $F.", 0, ch, 0, EXIT(ch, door)->keyword, TO_ROOM);
            else
                act("$n unlocks the door.", FALSE, ch, 0, 0, TO_ROOM);
            send_to_char("*click*\n\r", ch);
            check_break_key(has_key(ch, EXIT(ch, door)->key), ch);
            /* now for unlocking the other side, too */
            if ((other_room = EXIT(ch, door)->to_room) != NOWHERE)
                if ((back = world[other_room].dir_option[rev_dir[door]]))
                    if (back->to_room == ch->in_room)
                        REMOVE_BIT(back->exit_info, EX_LOCKED);
        }
}

ACMD(do_move);
ACMD(do_enter)
{
    int door;

    //   ACMD(do_move);

    one_argument(argument, buf);

    if (*buf) /* an argument was supplied, search for door keyword */ {
        for (door = 0; door < NUM_OF_DIRS; door++)
            if (EXIT(ch, door))
                if (EXIT(ch, door)->keyword)
                    if (!str_cmp(EXIT(ch, door)->keyword, buf)) {
                        do_move(ch, "", wtl, ++door, 0);
                        return;
                    }
        sprintf(buf2, "There is no %s here.\n\r", buf);
        send_to_char(buf2, ch);
    } else if (IS_SET(world[ch->in_room].room_flags, INDOORS))
        send_to_char("You are already indoors.\n\r", ch);
    else {
        /* try to locate an entrance */
        for (door = 0; door < NUM_OF_DIRS; door++)
            if (EXIT(ch, door))
                if (EXIT(ch, door)->to_room != NOWHERE)
                    if (!IS_SET(EXIT(ch, door)->exit_info, EX_CLOSED) && IS_SET(world[EXIT(ch, door)->to_room].room_flags, INDOORS)) {
                        do_move(ch, "", wtl, ++door, 0);
                        return;
                    }
        send_to_char("You can't seem to find anything to enter.\n\r", ch);
    }
}

ACMD(do_leave)
{
    int door;

    extern long secs_to_unretire(struct char_data*);
    extern int r_mortal_start_room[];

    /* retired characters can 'leave' the retirement home */
    if (IS_SET(PLR_FLAGS(ch), PLR_RETIRED)) {
        if (secs_to_unretire(ch) <= 0) {
            send_to_char("You leave the retirement home, hungry for new adventures.\r\n", ch);
            act("A wisp of slate grey smoke settles around $n.\r\n"
                "The smoke recesses to the bar, and $n is nowhere to be found.",
                FALSE, ch, 0, 0, TO_ROOM);
            unretire(ch);
            char_from_room(ch);
            char_to_room(ch, r_mortal_start_room[GET_RACE(ch)]);
            do_look(ch, "", 0, 0, 0);
        } else
            send_to_char("You cannot leave the retirement home yet.\r\n", ch);

        return;
    }

    if (!IS_SET(world[ch->in_room].room_flags, INDOORS))
        send_to_char("You are outside.. where do you want to go?\n\r", ch);
    else {
        for (door = 0; door < NUM_OF_DIRS; door++)
            if (EXIT(ch, door))
                if (EXIT(ch, door)->to_room != NOWHERE)
                    if (!IS_SET(EXIT(ch, door)->exit_info, EX_CLOSED) && !IS_SET(world[EXIT(ch, door)->to_room].room_flags, INDOORS)) {
                        do_move(ch, "", wtl, ++door, 0);
                        return;
                    }
        send_to_char("I see no obvious exits to the outside.\n\r", ch);
    }
}

ACMD(do_stand)
{
    switch (GET_POS(ch)) {
    case POSITION_STANDING:
        act("You are already standing.", FALSE, ch, 0, 0, TO_CHAR);
        break;
    case POSITION_SITTING:
        act("You stand up.", FALSE, ch, 0, 0, TO_CHAR);
        act("$n clambers to $s feet.", TRUE, ch, 0, 0, TO_ROOM);
        if (ch->specials.fighting)
            GET_POS(ch) = POSITION_FIGHTING;
        else
            GET_POS(ch) = POSITION_STANDING;
        break;
    case POSITION_RESTING:
        act("You stop resting, and stand up.", FALSE, ch, 0, 0, TO_CHAR);
        act("$n stops resting, and clambers on $s feet.", TRUE, ch, 0, 0, TO_ROOM);
        if (ch->specials.fighting)
            GET_POS(ch) = POSITION_FIGHTING;
        else
            GET_POS(ch) = POSITION_STANDING;
        break;
    case POSITION_SLEEPING:
        //      act("You have to wake up first!", FALSE, ch, 0, 0, TO_CHAR);
        if (ch->specials.fighting)
            GET_POS(ch) = POSITION_FIGHTING;
        else
            GET_POS(ch) = POSITION_STANDING;
        act("You wake, and stand up.", FALSE, ch, 0, 0, TO_CHAR);
        act("$n awakes, and clambers on $s feet.", TRUE, ch, 0, 0, TO_ROOM);
        break;
    case POSITION_FIGHTING:
        act("Do you not consider fighting as standing?", FALSE, ch, 0, 0, TO_CHAR);
        break;
    case POSITION_STUNNED:
        return;
    case POSITION_INCAP:
        return;
    case POSITION_DEAD:
        return;
    default:
        act("You stop floating around, and put your feet on the ground.", FALSE, ch, 0, 0, TO_CHAR);
        act("$n stops floating around, and puts $s feet on the ground.", TRUE, ch, 0, 0, TO_ROOM);
        break;
    }
}

ACMD(do_sit)
{
    switch (GET_POS(ch)) {
    case POSITION_STANDING:
        act("You sit down.", FALSE, ch, 0, 0, TO_CHAR);
        act("$n sits down.", FALSE, ch, 0, 0, TO_ROOM);
        GET_POS(ch) = POSITION_SITTING;
        break;
    case POSITION_SITTING:
        send_to_char("You're sitting already.\n\r", ch);
        break;

    case POSITION_RESTING:
        act("You stop resting, and sit up.", FALSE, ch, 0, 0, TO_CHAR);
        act("$n stops resting.", TRUE, ch, 0, 0, TO_ROOM);
        GET_POS(ch) = POSITION_SITTING;
        break;
    case POSITION_SLEEPING:
        act("You have to wake up first.", FALSE, ch, 0, 0, TO_CHAR);
        break;
    case POSITION_FIGHTING:
        act("Sit down while fighting? are you MAD?", FALSE, ch, 0, 0, TO_CHAR);
        break;
    default:
        act("You stop floating around, and sit down.", FALSE, ch, 0, 0, TO_CHAR);
        act("$n stops floating around, and sits down.", TRUE, ch, 0, 0, TO_ROOM);
        GET_POS(ch) = POSITION_SITTING;
        break;
    }
}

ACMD(do_rest)
{
    switch (GET_POS(ch)) {
    case POSITION_STANDING:
        act("You sit down and rest your tired bones.", FALSE, ch, 0, 0, TO_CHAR);
        act("$n sits down and rests.", TRUE, ch, 0, 0, TO_ROOM);
        GET_POS(ch) = POSITION_RESTING;
        break;
    case POSITION_SITTING:
        act("You rest your tired bones.", FALSE, ch, 0, 0, TO_CHAR);
        act("$n rests.", TRUE, ch, 0, 0, TO_ROOM);
        GET_POS(ch) = POSITION_RESTING;
        break;
    case POSITION_RESTING:
        act("You are already resting.", FALSE, ch, 0, 0, TO_CHAR);
        break;
    case POSITION_SLEEPING:
        act("You have to wake up first.", FALSE, ch, 0, 0, TO_CHAR);
        break;
    case POSITION_FIGHTING:
        act("Rest while fighting?  Are you MAD?", FALSE, ch, 0, 0, TO_CHAR);
        break;
    default:
        act("You stop floating around, and stop to rest your tired bones.", FALSE, ch, 0, 0,
            TO_CHAR);
        act("$n stops floating around, and rests.", FALSE, ch, 0, 0, TO_ROOM);
        GET_POS(ch) = POSITION_SITTING;
        break;
    }
}

ACMD(do_sleep)
{

    if (IS_RIDING(ch))
        do_dismount(ch, "", 0, 0, 0);

    switch (GET_POS(ch)) {
    case POSITION_STANDING:
    case POSITION_SITTING:
    case POSITION_RESTING:
        send_to_char("You go to sleep.\n\r", ch);
        act("$n lies down and falls asleep.", TRUE, ch, 0, 0, TO_ROOM);
        GET_POS(ch) = POSITION_SLEEPING;
        break;
    case POSITION_SLEEPING:
        send_to_char("You are already sound asleep.\n\r", ch);
        break;
    case POSITION_FIGHTING:
        send_to_char("Sleep while fighting?  Are you MAD?\n\r", ch);
        break;
    default:
        act("You stop floating around, and lie down to sleep.", FALSE, ch, 0, 0, TO_CHAR);
        act("$n stops floating around, and lie down to sleep.", TRUE, ch, 0, 0, TO_ROOM);
        GET_POS(ch) = POSITION_SLEEPING;
        break;
    }
}

ACMD(do_wake)
{
    struct char_data* tmp_char;

    one_argument(argument, arg);
    if (*arg) {
        if (GET_POS(ch) == POSITION_SLEEPING) {
            act("You can't wake people up if you are asleep yourself!", FALSE, ch, 0, 0, TO_CHAR);
        } else {
            tmp_char = get_char_room_vis(ch, arg);
            if (tmp_char) {
                if (tmp_char == ch) {
                    act("If you want to wake yourself up, just type 'wake'", FALSE, ch, 0, 0,
                        TO_CHAR);
                } else {
                    if (GET_POS(tmp_char) == POSITION_SLEEPING) {
                        //  if (IS_AFFECTED(tmp_char, AFF_SLEEP)) {
                        //     act("You can not wake $M up!", FALSE, ch, 0, tmp_char, TO_CHAR);
                        //  } else {
                        act("You wake $M up.", FALSE, ch, 0, tmp_char, TO_CHAR);
                        GET_POS(tmp_char) = POSITION_SITTING;
                        act("You are awakened by $n.", FALSE, ch, 0, tmp_char, TO_VICT);

                    } else {
                        act("$N is already awake.", FALSE, ch, 0, tmp_char, TO_CHAR);
                    }
                }
            } else {
                send_to_char("You do not see that person here.\n\r", ch);
            }
        }
    } else {
        //   if (IS_AFFECTED(ch, AFF_SLEEP)) {
        //	 send_to_char("You can't wake up!\n\r", ch);
        //   } else {
        if (GET_POS(ch) > POSITION_SLEEPING)
            send_to_char("You are already awake...\n\r", ch);
        else {
            send_to_char("You wake, and sit up.\n\r", ch);
            act("$n awakens.", TRUE, ch, 0, 0, TO_ROOM);
            GET_POS(ch) = POSITION_SITTING;
        }
    }
}

ACMD(do_lose)
{
    follow_type* tmpfol;
    char_data* tmpch;

    one_argument(argument, buf);

    if (!*buf) {
        send_to_char("Whom do you want to lose?\n\r", ch);
        return;
    }
    if (!str_cmp(buf, "all"))
        for (tmpfol = ch->followers; tmpfol; tmpfol = ch->followers)
            stop_follower(tmpfol->follower, FOLLOW_MOVE);
    else {
        tmpch = get_char_vis(ch, buf);
        if (!tmpch || other_side(ch, tmpch)) {
            send_to_char("Nobody by that name.\n\r", ch);
            return;
        }
        if (tmpch->master == ch)
            stop_follower(tmpch, FOLLOW_MOVE);
        else {
            sprintf(buf, "But, %s is not following you!\n\r", HSSH(tmpch));
            send_to_char(buf, ch);
        }
    }
}

ACMD(do_follow)
{
    if (ch == NULL)
        return;

    char_data* leader = NULL;

    one_argument(argument, buf);

    if (*buf) {
        if (!str_cmp(buf, "self")) {
            leader = ch;
        } else {
            leader = get_char_room_vis(ch, buf);
        }
    } else {
        send_to_char("Whom do you wish to follow?\n\r", ch);
        return;
    }

    if (leader == NULL) {
        send_to_char("I see no person by that name here!\n\r", ch);
        return;
    }

    if (other_side(ch, leader) || (IS_NPC(leader) && MOB_FLAGGED(leader, MOB_MOUNT) && IS_AGGR_TO(leader, ch))) {
        sprintf(buf, "It doesn't want you to follow it.\n\r");
        send_to_char(buf, ch);
        return;
    }

    if (IS_SHADOW(ch)) {
        send_to_char("You cannot follow anything whilst being a shadow.\n\r", ch);
        return;
    }

    if (ch->master == leader) {
        sprintf(buf, "You are already following %s.\n\r", HMHR(leader));
        send_to_char(buf, ch);
        return;
    }

    if (ch->master && (MOB_FLAGGED(ch, MOB_ORC_FRIEND) || MOB_FLAGGED(ch, MOB_PET))) {
        act("But you only feel like following $N!", FALSE, ch, 0, ch->master, TO_CHAR);
        act("$n only feels like following you!", FALSE, ch, 0, ch->master, TO_VICT);
    } else { /* Not Charmed follow person */
        if (leader == ch) {
            if (!ch->master) {
                send_to_char("You are already following yourself.\n\r", ch);
                return;
            }
            stop_follower(ch, FOLLOW_MOVE);
        } else {
            if (circle_follow(ch, leader, FOLLOW_MOVE)) {
                act("Sorry, but following in loops is not allowed.", FALSE, ch, 0, 0, TO_CHAR);
                return;
            }

            if (ch->master) {
                stop_follower(ch, FOLLOW_MOVE);
            }

            add_follower(ch, leader, FOLLOW_MOVE);
        }
    }
}

ACMD(do_refollow)
{
    if (ch->master == NULL) {
        send_to_char("But, you aren't following anyone!\n\r", ch);
        return;
    }

    char_data* leader = ch->master;

    stop_follower(ch, FOLLOW_REFOL);
    add_follower(ch, leader, FOLLOW_MOVE);
}

ACMD(do_lead)
{ // Added by Loman.
    char_data* potential_mount;
    char_data* mount;

    while (*argument && (*argument <= ' '))
        argument++;
    mount = 0;

    if (!*argument) { // default mount
        do_dismount(ch, "", 0, 0, 0);
    } else {
        potential_mount = get_char_room_vis(ch, argument);
        if (!potential_mount) {
            send_to_char("There is nobody by that name.\n\r", ch);
            return;
        }
        if (IS_NPC(potential_mount) && !IS_SET(potential_mount->specials2.act, MOB_MOUNT) || !IS_NPC(potential_mount)) {
            send_to_char("You can not lead this.\n\r", ch);
            return;
        }
        if (IS_AGGR_TO(potential_mount, ch)) {
            act("$N doesn't want you to lead $M.", FALSE, ch, 0, potential_mount, TO_CHAR);
            return;
        }
        if (potential_mount->mount_data.mount) {
            act("$N is not in a position for you to lead $M.", FALSE, ch, 0, potential_mount,
                TO_CHAR);
            return;
        }
        mount = potential_mount;
    }

    if (!mount)
        return;

    if (mount == ch) {
        send_to_char("You tried to lead yourself, but failed.\n\r", ch);
        return;
    }

    if (IS_RIDING(ch) && ch->mount_data.mount == mount) {
        do_dismount(ch, "", 0, 0, 0);
        return;
    }

    if (GET_POSITION(ch) == POSITION_FIGHTING) {
        send_to_char("You can't do this while fighting.\n\r", ch);
        return;
    }

    if (mount->master) {
        if (mount->master == ch)
            act("$N is already following you.", FALSE, ch, 0, mount, TO_CHAR);
        else
            act("$N is already following someone.", FALSE, ch, 0, mount, TO_CHAR);
        return;
    }

    if (IS_RIDDEN(mount)) {
        act("$N is already being ridden!", FALSE, ch, 0, mount, TO_CHAR);
        return;
    }

    if (affected_by_spell(mount, SKILL_CALM)) {
        if (!is_strong_enough_to_tame(ch, mount, false)) {
            send_to_char("Your skill with animals is insufficient to lead that beast.\r\n", ch);
            return;
        }
    }

    act("You grab $N and start leading $m.", FALSE, ch, 0, mount, TO_CHAR);
    act("$n grabs $N and starts leading $m.", FALSE, ch, 0, mount, TO_ROOM);
    add_follower(mount, ch, FOLLOW_MOVE);
}

ACMD(do_pull)
{

    obj_data* obj;
    int room_num, exit_num, next_room_num;
    int would_open; // 1 if open, 0 is close.
    room_data* room;
    room_data* next_room;

    if (IS_SHADOW(ch)) {
        send_to_char("You are too insubstantial to do that.\n\r", ch);
        return;
    }

    if (!wtl || (wtl->targ1.type != TARGET_OBJ)) {
        send_to_char("You can't pull this!\n\r", ch);
        return;
    }

    obj = wtl->targ1.ptr.obj;

    if (obj->in_room != ch->in_room) {
        send_to_char("It is not here.\n\r", ch);
        return;
    }

    if (!call_trigger(ON_PULL, obj, ch, 0))
        return;

    if (obj->obj_flags.type_flag != ITEM_LEVER) {
        if (CAN_WEAR(obj, ITEM_TAKE)) {
            act("$n drags $P around, for all to see.", TRUE, ch, 0, obj, TO_ROOM);
            act("You drag $P around, showing it off.", TRUE, ch, 0, obj, TO_CHAR);
        } else {
            act("$n tries to pull at $P, but cannot shift it.", TRUE, ch, 0, obj, TO_ROOM);
            act("You try to pull at $P, but it would not budge.", TRUE, ch, 0, obj, TO_CHAR);
        }
        return;
    }
    room_num = real_room(obj->obj_flags.value[0]);
    exit_num = obj->obj_flags.value[1];
    if (exit_num >= NUM_OF_DIRS)
        exit_num = -1;

    if (room_num >= 0)
        room = &world[room_num];
    else
        room = 0;

    if (!room || (exit_num < 0) || !room->dir_option[exit_num] || !room->dir_option[exit_num]->keyword) {
        act("$P seems to be broken.", FALSE, ch, 0, obj, TO_CHAR);
        return;
    }
    act("You pull at $P.", FALSE, ch, 0, obj, TO_CHAR);
    act("$n pulls at $P.", FALSE, ch, 0, obj, TO_ROOM);

    if (IS_SET(room->dir_option[exit_num]->exit_info, EX_CLOSED)) {
        REMOVE_BIT(room->dir_option[exit_num]->exit_info, EX_CLOSED);
        sprintf(buf, "The %s opens slowly.\n\r", room->dir_option[exit_num]->keyword);
        send_to_room(buf, room_num);

        would_open = 1;

        if (ch->in_room != room_num) {
            send_to_room("You hear low rumbling in the distance.\n\r", ch->in_room);
        }
    } else {
        SET_BIT(room->dir_option[exit_num]->exit_info, EX_CLOSED);
        sprintf(buf, "The %s closes slowly.\n\r", room->dir_option[exit_num]->keyword);
        send_to_room(buf, room_num);

        would_open = 0;

        if (ch->in_room != room_num) {
            send_to_room("You hear low rumbling in the distance.\n\r", ch->in_room);
        }
    }

    //*** now opening the other side of the door

    next_room_num = room->dir_option[exit_num]->to_room;

    if (next_room_num >= 0)
        next_room = &world[next_room_num];
    else
        next_room = 0;

    exit_num = rev_dir[exit_num];

    if (!next_room || (exit_num < 0) || !next_room->dir_option[exit_num] || !next_room->dir_option[exit_num]->keyword) {

        return;
    }

    if (next_room->dir_option[exit_num]->to_room != room_num)
        return;

    if (IS_SET(next_room->dir_option[exit_num]->exit_info, EX_CLOSED) || would_open) {
        REMOVE_BIT(next_room->dir_option[exit_num]->exit_info, EX_CLOSED);
        sprintf(buf, "The %s opens slowly.\n\r", next_room->dir_option[exit_num]->keyword);
        send_to_room(buf, next_room_num);

    } else {
        SET_BIT(next_room->dir_option[exit_num]->exit_info, EX_CLOSED);
        sprintf(buf, "The %s closes slowly.\n\r", next_room->dir_option[exit_num]->keyword);
        send_to_room(buf, next_room_num);
    }
}
