/* ************************************************************************
 *  File: Ranger.cc                                Part of CircleMUD       *
 *  Usage: Handles our ranger coeff skills                                 *
 *  Created 27/04/05                                                       *
 *                                                                         *
 *  All rights reserved.  See license.doc for complete information.        *
 *                                                                         *
 *  Copyright (C) 1993 by the Trustees of the Johns Hopkins University     *
 *  CircleMUD is based on DikuMUD, Copyright (C) 1990, 1991.
 ************************************************************************ */

#include "platdef.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "comm.h"
#include "db.h"
#include "handler.h"
#include "interpre.h"
#include "spells.h"
#include "structs.h"
#include "utils.h"
#include "zone.h" /* For zone_table */

#include "big_brother.h"
#include "char_utils.h"
#include "char_utils_combat.h"

#include <algorithm>
#include <iostream>
#include <sstream>
#include <vector>

typedef char* string;

/*
 * External Variables and structures
 */

extern struct char_data* character_list;
extern struct char_data* waiting_list;
extern struct room_data world;
extern struct skill_data skills[];
extern int find_door(struct char_data* ch, char* type, char* dir);
extern int get_followers_level(struct char_data*);
extern int old_search_block(char*, int, unsigned int, const char**, int);
extern int get_number(char** name);
extern int get_real_stealth(struct char_data*);
extern int rev_dir[];
extern int show_tracks(struct char_data* ch, char* name, int mode);
extern int top_of_world;
extern char* dirs[];
extern void appear(struct char_data* ch);
extern void check_break_prep(struct char_data*);
extern void stop_hiding(struct char_data* ch, char);
extern void update_pos(struct char_data* victim);
int check_simple_move(struct char_data* ch, int cmd, int* move_cost, int mode);
int check_hallucinate(struct char_data* ch, struct char_data* victim);
extern void say_spell(char_data* caster, int spell_index);

namespace Gather {
enum class Item : int {
    Food = 7218,
    Light = 7007,
    Bow = 2700,
    Arrow = 2720,
    Dust = 2100,
    Poison = 4614,
    Antidote = 4615
};
}

ACMD(do_move);
ACMD(do_hit);
ACMD(do_gen_com);

ACMD(do_ride)
{

    follow_type* tmpfol;
    char_data* potential_mount;
    char_data* mountch;

    if (char_exists(ch->mount_data.mount_number) && ch->mount_data.mount) {
        send_to_char("You are riding already.\n\r", ch);
        return;
    }
    /* only people can ride (bodytype 1) */
    if (GET_BODYTYPE(ch) != 1)
        return;

    if (IS_SHADOW(ch)) {
        send_to_char("You cannot ride whilst inhabiting the shadow world.\n\r", ch);
        return;
    }

    while (*argument && (*argument <= ' '))
        argument++;

    mountch = 0;
    if (!*argument) {
        for (tmpfol = ch->followers; tmpfol; tmpfol = tmpfol->next) {
            if (char_exists(tmpfol->fol_number) && tmpfol->follower->in_room == ch->in_room && IS_NPC(tmpfol->follower) && IS_SET(tmpfol->follower->specials2.act, MOB_MOUNT)) {
                break;
            }
        }

        if (tmpfol) {
            mountch = tmpfol->follower;
            stop_follower(mountch, FOLLOW_MOVE);
        } else {
            send_to_char("What do you want to ride?\n\r", ch);
            return;
        }
    } else {

        potential_mount = get_char_room_vis(ch, argument);
        if (!potential_mount) {
            send_to_char("There is nobody by that name.\n\r", ch);
            return;
        }

        if (IS_NPC(potential_mount) && !IS_SET(potential_mount->specials2.act, MOB_MOUNT) || !IS_NPC(potential_mount)) {
            send_to_char("You can not ride this.\n\r", ch);
            return;
        }

        if (IS_AGGR_TO(potential_mount, ch) && !affected_by_spell(potential_mount, SKILL_CALM)) {
            act("$N doesn't want you to ride $M.", FALSE, ch, 0, potential_mount, TO_CHAR);
            return;
        }

        if (potential_mount->mount_data.mount) {
            act("$N is not in a position for you to ride.", FALSE, ch, 0, potential_mount, TO_CHAR);
            return;
        }

        if (potential_mount->master && potential_mount->master != ch) {
            send_to_char("That mount is already following someone else.\r\n", ch);
            return;
        }

        if (affected_by_spell(potential_mount, SKILL_CALM)) {
            if (IS_NPC(ch) && MOB_FLAGGED(ch, MOB_ORC_FRIEND) && ch->master) {
                if (!is_strong_enough_to_tame(ch->master, potential_mount, false)) {
                    send_to_char("Your skill with animals is insufficient to ride that beast.\r\n",
                        ch->master);
                    return;
                }
            } else {
                if (!is_strong_enough_to_tame(ch, potential_mount, false)) {
                    send_to_char("Your skill with animals is insufficient to ride that beast.\r\n",
                        ch);
                    return;
                }
            }
        }

        mountch = potential_mount;
    }

    if (!mountch)
        return;

    if (mountch == ch) {
        send_to_char("You tried to mount yourself, but failed.\n\r", ch);
        return;
    }
    if (IS_RIDING(mountch)) {
        mountch = mountch->mount_data.mount;
    } else {
        stop_follower(mountch, FOLLOW_MOVE);
    }

    /* Mounting a horse now */
    ch->mount_data.mount = mountch;
    ch->mount_data.mount_number = mountch->abs_number;
    ch->mount_data.next_rider = 0;
    ch->mount_data.next_rider_number = -1;

    if (!IS_RIDDEN(mountch)) {
        act("You mount $N and start riding $m.", FALSE, ch, 0, mountch, TO_CHAR);
        act("$n mounts $N and starts riding $m.", FALSE, ch, 0, mountch, TO_ROOM);
        mountch->mount_data.rider = ch;
        mountch->mount_data.rider_number = ch->abs_number;
    } else {
        act("You mount $N.", FALSE, ch, 0, mountch, TO_CHAR);
        act("$n mounts $N.", FALSE, ch, 0, mountch, TO_ROOM);

        for (potential_mount = mountch->mount_data.rider;
             potential_mount->mount_data.next_rider && char_exists(potential_mount->mount_data.next_rider_number);
             potential_mount = potential_mount->mount_data.next_rider)
            ;

        potential_mount->mount_data.next_rider = ch;
        potential_mount->mount_data.next_rider_number = ch->abs_number;
    }
    IS_CARRYING_W(mountch) += GET_WEIGHT(ch) + IS_CARRYING_W(ch);
}

ACMD(do_dismount)
{
    if (ch == NULL) {
        sprintf(buf, "Dismount called without a character.  Exiting.");
        mudlog(buf, NRM, LEVEL_IMMORT, TRUE);
        return;
    }

    if (IS_RIDING(ch) == false) {
        send_to_char("You are not riding anything.\n\r", ch);
    } else {
        char_data* mount = ch->mount_data.mount;
        if (mount != NULL) {
            char_data* were_rider = NULL;
            if (IS_RIDDEN(mount)) {
                were_rider = mount->mount_data.rider;
            } else {
                sprintf(buf, "Screwed mount %s, all be wary!", GET_NAME(mount));
                mudlog(buf, NRM, LEVEL_IMMORT, TRUE);
                were_rider = NULL;
            }

            stop_riding(ch);
            if (IS_NPC(mount) && were_rider == ch && ch->specials.fighting != mount) {
                add_follower(mount, ch, FOLLOW_MOVE);
            }
        }
    }
}

ACMD(do_track)
{
    int count;

    if (subcmd == -1) {
        abort_delay(ch);
        return;
    }
    if (IS_SHADOW(ch)) {
        send_to_char("You can't see the physical world well enough.\n\r", ch);
        return;
    }

    if (subcmd == 0) {
        if (!CAN_SEE(ch)) {
            send_to_char("You can't see to track!", ch);
            return;
        }
        if (wtl && (wtl->targ1.type == TARGET_TEXT))
            argument = wtl->targ1.ptr.text->text;
        if (IS_WATER(ch->in_room)) {
            send_to_char("You can't track in water!\n\r", ch);
            return;
        }
        send_to_char("You start searching the ground for tracks.\n\r", ch);
        act("$n searches the ground for tracks.", TRUE, ch, 0, 0, TO_ROOM);
        WAIT_STATE_FULL(ch, skills[SKILL_TRACK].beats, CMD_TRACK, 1, 30, 0, 0,
            get_from_txt_block_pool(argument), AFF_WAITING | AFF_WAITWHEEL,
            TARGET_TEXT);
        return;
    }
    if (ch->delay.targ1.type == TARGET_TEXT)
        argument = ch->delay.targ1.ptr.text->text;
    count = show_tracks(ch, argument, 1);

    if (ch->delay.targ1.type == TARGET_TEXT)
        ch->delay.targ1.cleanup();

    if (count == 0)
        send_to_char("You found no tracks here.\n\r", ch);
}

/*
 * This is an array of the sector "percentage" bonus values
 * used by gather.
 */
int sector_variables[] = {
    -200, /*Floor*/
    -70, /*City*/
    20, /*Field*/
    20, /*Forest*/
    0, /*Hills*/
    -20, /*Mountains*/
    -200, /*Water*/
    -200, /*No_Swim Water*/
    -200, /*UnderWater*/
    -30, /*Road*/
    -200, /*Crack*/
    20, /*Dense Forest*/
    20 /*Swamp*/
};

/*
 * This function checks the conditions needed to gather
 * with success.
 */
int check_gather_conditions(struct char_data* ch, int percent, int gather_type)
{

    /*
     * The way gather was originally implemented was terrible,
     * because of this i have to but in a second checker for
     * argument here to stop a bug. Nested switch statements
     * using the same variable to switch is a bad idea. . .
     */
    if (gather_type == 0) {
        send_to_char("You can gather food, healing, energy, bows, arrows, or light.\n\r", ch);
        return FALSE;
    }
    if (affected_by_spell(ch, SKILL_GATHER_FOOD) && (gather_type > 2 && gather_type < 5)) {
        send_to_char("You would gain no benefit from this right now.\n\r", ch);
        return FALSE;
    }
    if (gather_type == 1 && (GET_RACE(ch) > 9)) {
        send_to_char("The fruits of Arda are as poison to you.\n\r", ch);
        return FALSE;
    }

    /*
     * Checks our sector conditions (if any)
     */
    switch (world[ch->in_room].sector_type) {
    case SECT_INSIDE:
        send_to_char("You can not gather herbs inside!\n\r", ch);
        return FALSE;
    case SECT_CITY:
        send_to_char("You might have better luck "
                     "outside of city\n\r",
            ch);
        return FALSE;
    case SECT_FIELD:
        send_to_char("Learning how to gather herbs better or,"
                     " perhaps, going to the forest could help.\r\n",
            ch);
        return FALSE;
    case SECT_SWAMP:
        if (gather_type == 2) {
            send_to_char("Trying to gather dry kindle-wood in a swamp is"
                         " an exercise in futility.\r\n",
                ch);
            return FALSE;
        }
        if (GET_RACE(ch) < 9) {
            send_to_char("There are no plants or herbs of any value"
                         " to you here.\r\n",
                ch);
            return FALSE;
        } else
            return TRUE;
    default:
        if (percent <= 10) {
            send_to_char("You are unable to find anything useful here.\r\n", ch);
            return FALSE;
        } else
            return TRUE;
    }

    return TRUE;
}

ACMD(do_gather_food)
{
    struct obj_data* obj;
    struct affected_type af;
    sh_int percent, move_use, GatherType;
    int affects_last, ranger_bonus;

    /*
     * Replaced a series of if/else statements which used to determine
     * what subcommand of gather you were using. Replaced it with
     * a neat little array, and used search_block to get the desired
     * values.
     */
    static char* gather_type[] = { "", "food", "light", "healing", "energy", "bow",
        "arrows", "dust", "poison", "antidote", "\n" };

    if (IS_SHADOW(ch)) {
        send_to_char("You are too insubstantial to do that.\n\r", ch);
        return;
    }
    if (IS_NPC(ch) && (MOB_FLAGGED(ch, MOB_ORC_FRIEND))) {
        send_to_char("Leave that to your leader.\n\r", ch);
        return;
    }

    percent = GET_SKILL(ch, SKILL_GATHER_FOOD);
    /* This is where we get our sector bonus */
    percent += sector_variables[world[ch->in_room].sector_type];

    switch (subcmd) {
    case -1:
        abort_delay(ch);
        return;
    case 0:
        move_use = 25 - percent / 5;
        if (GET_MOVE(ch) < move_use) {
            send_to_char("You are too tired for this right now.\n\r", ch);
            return;
        }
        one_argument(argument, arg);
        GatherType = search_block(arg, gather_type, 0);
        if (GatherType == -1) { /*If we can't find an argument */
            send_to_char("You can gather food, healing, energy, bows, arrows, dust, or light.\n\r",
                ch);
            return;
        }
        if (!check_gather_conditions(ch, percent,
                GatherType)) /*Checks sector and race conditions */
            return;

        /*And we've passed our conditions so we're going on now to try and gather */
        WAIT_STATE_FULL(ch, MIN(25, 40 - GET_SKILL(ch, SKILL_GATHER_FOOD) / 5), CMD_GATHER_FOOD,
            GatherType, 30, 0, 0, 0, AFF_WAITING | AFF_WAITWHEEL, TARGET_NONE);
        GET_MOVE(ch) -= move_use;
        break;
    default:
        if (number(0, 100) > (subcmd < 3 ? percent : percent - 10)) {

            const char* gather_type = nullptr;
            switch (subcmd) {
            case 2:
                gather_type = "a torch";
                break;
            case 5:
                gather_type = "a bow";
                break;
            case 6:
                gather_type = "an arrow";
                break;
            case 7:
                gather_type = "some dust";
                break;
            case 1:
            case 3:
            case 4:
            default:
                gather_type = "some herbs";
                break;
            }

            sprintf(buf, "You tried to gather %s, but could not find anything useful.\n\r",
                gather_type);
            send_to_char(buf, ch);

            return;
        } else {
            /*
             * This portion of the code was relatively untouched
             * and stays in its original format. A few lines concerning
             * objects and darkie races were removed.
             * This is where we load our objects for gather light and food
             * and do our calculations for healing and energy.
             */
            switch (subcmd) {
            case 1:
                if ((obj = read_object(static_cast<int>(Gather::Item::Food), VIRT)) != NULL) {
                    send_to_char("You look around, and manage to find some edible"
                                 " roots and berries.\n\r",
                        ch);
                    obj_to_char(obj, ch);
                } else
                    send_to_char("Minor problem in gather food. Could not create item."
                                 " Please notify imps.\n\r",
                        ch);
                break;
            case 2:
                if ((obj = read_object(static_cast<int>(Gather::Item::Light), VIRT)) != NULL) {
                    send_to_char("You gather some wood and fashion it into a"
                                 " crude torch.\n\r",
                        ch);
                    obj_to_char(obj, ch);
                } else
                    send_to_char("Problem in gather light.  Could not create item."
                                 "Please notify imps.\n\r",
                        ch);
                break;
            case 3:
                GET_HIT(ch) = MIN(GET_HIT(ch) + GET_SKILL(ch, SKILL_GATHER_FOOD) / 3 + GET_PROF_LEVEL(PROF_RANGER, ch),
                    GET_MAX_HIT(ch));
                send_to_char("You look around, and manage to find some healing"
                             " herbs.\n\r",
                    ch);
                break;
            case 4:
                GET_MOVE(ch) = MIN(GET_MOVE(ch) + GET_SKILL(ch, SKILL_GATHER_FOOD) * 2 / 3 + GET_PROF_LEVEL(PROF_RANGER, ch),
                    GET_MAX_MOVE(ch));

                send_to_char("You manage to find some herbs which have given you energy."
                             "\n\r",
                    ch);
                break;
            case 5:
                if ((obj = read_object(static_cast<int>(Gather::Item::Bow), VIRT)) != NULL) {
                    obj_to_char(obj, ch);
                    send_to_char(
                        "You manage to find some branches that you fashion into a bow.\n\r", ch);
                } else {
                    send_to_char(
                        "Problem in gather bow. Could not create item. Please notify imps.\n\r",
                        ch);
                }
                break;
            case 6:
                if ((obj = read_object(static_cast<int>(Gather::Item::Arrow), VIRT)) != NULL) {
                    obj_to_char(obj, ch);
                    send_to_char("You manage to craft an arrow out of twigs near by.\n\r", ch);
                } else {
                    send_to_char(
                        "Problem in gather arrows. Could not create item. Please notify imps.\n\r",
                        ch);
                }
                break;
            case 7:
                if ((obj = read_object(static_cast<int>(Gather::Item::Dust), VIRT)) != NULL) {
                    obj_to_char(obj, ch);
                    send_to_char(
                        "You manage to find some suitable dust for blinding your victim.\n\r", ch);
                } else {
                    send_to_char(
                        "Problem in gather dust. Could not create item. Please notify imps.\n\r",
                        ch);
                }
                break;
            }

            /*
             * The affects of gather herbs now is based on the characters
             * ranger level. Its 25 - ranger level / 2, it was static at
             * 18, so i wanted to keep the values relatively (big relatively)
             * close to that mark.
             */

            ranger_bonus = number(GET_PROF_LEVEL(PROF_RANGER, ch) / 4, GET_PROF_LEVEL(PROF_RANGER, ch) / 3);
            affects_last = 22 - ranger_bonus;
            if (subcmd == 3 || subcmd == 4) {
                af.type = SKILL_GATHER_FOOD;
                af.duration = affects_last;
                af.modifier = 0;
                af.location = APPLY_NONE;
                af.bitvector = 0;
                affect_to_char(ch, &af);
            }
        }
        abort_delay(ch);
    } /*subcommand switch statement*/
}

ACMD(do_pick)
{
    byte percent;
    int door, other_room;
    char type[MAX_INPUT_LENGTH], dir[MAX_INPUT_LENGTH];
    struct room_direction_data* back;
    struct obj_data* obj;
    struct char_data* victim;

    if (IS_SHADOW(ch)) {
        send_to_char("You are too insubstantial to do that.\n\r", ch);
        return;
    }

    if (IS_NPC(ch) && MOB_FLAGGED(ch, MOB_ORC_FRIEND)) {
        send_to_char("Sorry, best leave this to your master.\n\r", ch);
        return;
    }

    argument_interpreter(argument, type, dir);
    percent = number(1, 101);

    if (!*type)
        send_to_char("Pick what?\n\r", ch);
    else if (generic_find(argument, FIND_OBJ_INV | FIND_OBJ_ROOM, ch, &victim, &obj))

        if (obj->obj_flags.type_flag != ITEM_CONTAINER)
            send_to_char("That's not a container.\n\r", ch);
        else if (!IS_SET(obj->obj_flags.value[1], CONT_CLOSED))
            send_to_char("Silly - it isn't even closed!\n\r", ch);
        else if (obj->obj_flags.value[2] < 0)
            send_to_char("Odd - you can't seem to find a keyhole.\n\r", ch);
        else if (!IS_SET(obj->obj_flags.value[1], CONT_LOCKED))
            send_to_char("Oho! This thing is NOT locked!\n\r", ch);
        else if (IS_SET(obj->obj_flags.value[1], CONT_PICKPROOF))
            send_to_char("It resists your attempts at picking it.\n\r", ch);
        else {
            if (percent > GET_SKILL(ch, SKILL_PICK_LOCK)) {
                send_to_char("You failed to pick the lock.\n\r", ch);
                act("$n fumbles with the lock.", TRUE, ch, 0, 0, TO_ROOM);
                return;
            }
            REMOVE_BIT(obj->obj_flags.value[1], CONT_LOCKED);
            send_to_char("*Click*\n\r", ch);
            act("$n fiddles with $p.", FALSE, ch, obj, 0, TO_ROOM);
        }

    else if ((door = find_door(ch, type, dir)) >= 0)
        if (!IS_SET(EXIT(ch, door)->exit_info, EX_ISDOOR))
            send_to_char("That's absurd.\n\r", ch);
        else if (!IS_SET(EXIT(ch, door)->exit_info, EX_CLOSED))
            send_to_char("You realize that the door is already open.\n\r", ch);
        else if (EXIT(ch, door)->key < 0)
            send_to_char("You can't seem to spot any lock to pick.\n\r", ch);
        else if (!IS_SET(EXIT(ch, door)->exit_info, EX_LOCKED))
            send_to_char("Oh.. it wasn't locked at all.\n\r", ch);
        else if (IS_SET(EXIT(ch, door)->exit_info, EX_PICKPROOF))
            send_to_char("You seem to be unable to pick this lock.\n\r", ch);
        else {
            if (percent > GET_SKILL(ch, SKILL_PICK_LOCK)) {
                send_to_char("You failed to pick the lock.\n\r", ch);
                act("$n fumbles with the lock.", TRUE, ch, 0, 0, TO_ROOM);
                return;
            }
            REMOVE_BIT(EXIT(ch, door)->exit_info, EX_LOCKED);
            if (EXIT(ch, door)->keyword)
                act("$n skillfully picks the lock of the $F.", 0, ch, 0, EXIT(ch, door)->keyword,
                    TO_ROOM);
            else
                act("$n picks the lock of the door.", TRUE, ch, 0, 0, TO_ROOM);
            send_to_char("The lock quickly yields to your skills.\n\r", ch);
            /*
             *This piece now unlocks the other side
             * of the door also
             */
            if ((other_room = EXIT(ch, door)->to_room) != NOWHERE)
                if ((back = world[other_room].dir_option[rev_dir[door]]) != NULL)
                    if (back->to_room == ch->in_room)
                        REMOVE_BIT(back->exit_info, EX_LOCKED);
        }
}

void stop_hiding(struct char_data* ch, char mode)
{
    /*
     *if mode is FALSE, then we don't send the "step" message
     */
    if (IS_SET(ch->specials.affected_by, AFF_HIDE) && mode)
        send_to_char("You step out of your cover.\r\n", ch);

    REMOVE_BIT(ch->specials.affected_by, AFF_HIDE);
    REMOVE_BIT(ch->specials2.hide_flags, HIDING_SNUCK_IN);
    GET_HIDING(ch) = 0;
}

ACMD(do_hide)
{
    sh_int hide_skill, hide_chance;
    char first_argument[260];

    one_argument(argument, first_argument);

    /*
     * We can't use an in-function `well' flag for this.  why?  because then
     * we charge the player 5* as many beats for hide, then return.  we're
     * then called again to perform the actual hide, but our well variable
     * has been lost.  making `well' static well also not work, because then
     * if two people hid on the same tick, we could confuse who hid well and
     * who did not
     */

    if (!strcmp(first_argument, "well"))
        SET_BIT(ch->specials2.hide_flags, HIDING_WELL);
    else {
        if (subcmd != 1)
            REMOVE_BIT(ch->specials2.hide_flags, HIDING_WELL);
    }
    if (IS_RIDING(ch)) {
        send_to_char("Hide while riding? Surely you jest.\n\r", ch);
        return;
    }

    if (ch->specials.fighting) {
        send_to_char("You can not hide from your opponent!\n\r", ch);
        return;
    }
    check_break_prep(ch);
    if (!subcmd) {
        if (GET_KNOWLEDGE(ch, SKILL_HIDE) <= 0)
            send_to_char("You cover your eyes with your hands and "
                         "hope you can't be seen.\n\r",
                ch);
        /*
         * The code below determines the beats taken to perform
         * a hide, based on whether or not people have snuck into
         * the room, are looking to hide or hide well. If you've snuck
         * into the room, hiding times will be slightly faster
         */
        else {
            if (IS_SET(ch->specials2.hide_flags, HIDING_SNUCK_IN))
                if (IS_SET(ch->specials2.hide_flags, HIDING_WELL))
                    /* Hiding well after sneaking in -> 7 beats*/
                    WAIT_STATE_BRIEF(ch,
                        skills[SKILL_HIDE].beats * (IS_SET(ch->specials2.hide_flags, HIDING_WELL) * 2) - 1,
                        CMD_HIDE, 1, 30, AFF_WAITING | AFF_WAITWHEEL);
                else
                    /* Hiding after sneaking in -> 3 beats*/
                    WAIT_STATE_BRIEF(ch, skills[SKILL_HIDE].beats - 1, CMD_HIDE, 1, 30,
                        AFF_WAITING | AFF_WAITWHEEL);
            else
                /*Hiding well with no sneak -> 9 beats, with four being default for normal hide*/
                WAIT_STATE_BRIEF(ch,
                    skills[SKILL_HIDE].beats * (IS_SET(ch->specials2.hide_flags, HIDING_WELL) * 2 + 1),
                    CMD_HIDE, 1, 30, AFF_WAITING | AFF_WAITWHEEL);

            if (IS_SET(ch->specials2.hide_flags, HIDING_WELL))
                send_to_char("You carefully choose a place to hide.\n\r", ch);
            else
                send_to_char("You quickly look for somewhere safe to hide.\n\r", ch);
        }
        return;
    }

    if (subcmd != 1) {
        send_to_char("You decide not to hide.\n\r", ch);
        return;
    }

    if (IS_AFFECTED(ch, AFF_HIDE))
        REMOVE_BIT(ch->specials.affected_by, AFF_HIDE);

    hide_skill = hide_prof(ch);
    if (IS_SET(ch->specials2.hide_flags, HIDING_WELL)) {
        send_to_char("You carefully hide yourself.\r\n", ch);
        hide_chance = number(hide_skill * 3 / 4, hide_skill);
        REMOVE_BIT(ch->specials2.hide_flags, HIDING_WELL);
    } else {
        send_to_char("You hide yourself.\r\n", ch);
        hide_chance = number(hide_skill / 2, hide_skill * 3 / 4);
    }
    GET_HIDING(ch) = hide_chance;

    if (hide_chance)
        SET_BIT(ch->specials.affected_by, AFF_HIDE);
}

ACMD(do_sneak)
{
    if (IS_NPC(ch)) {
        send_to_char("Either you sneak or you don't.\r\n", ch);
        return;
    }

    if (!IS_AFFECTED(ch, AFF_SNEAK)) {
        send_to_char("Ok, you'll try to move silently.\r\n", ch);
        SET_BIT(ch->specials.affected_by, AFF_SNEAK);
    } else {
        send_to_char("You stop sneaking.\r\n", ch);
        REMOVE_BIT(ch->specials.affected_by, AFF_SNEAK);
    }
}

/*
 * Return a pointer to the intended victim of an ambush.  If
 * the intended victim is not a VALID victim, then we return
 * NULL.
 */
struct char_data* ambush_get_valid_victim(struct char_data* ch, struct waiting_type* target)
{
    struct char_data* victim;

    if (target->targ1.type == TARGET_TEXT)
        victim = get_char_room_vis(ch, target->targ1.ptr.text->text);
    else if (target->targ1.type == TARGET_CHAR) {
        if (char_exists(target->targ1.ch_num))
            victim = target->targ1.ptr.ch;
    }

    if (victim == nullptr) {
        send_to_char("Ambush who?\r\n", ch);
        return nullptr;
    }

    if ((IS_AFFECTED(ch, AFF_CHARM) && ch->master == victim) || (IS_AFFECTED(victim, AFF_CHARM) && victim->master == ch)) {
        act("$N is just such a good friend, you simply can't hit $M.", FALSE, ch, 0, victim,
            TO_CHAR);
        return nullptr;
    }

    /* Can happen for TARGET_CHAR */
    if (ch->in_room != victim->in_room) {
        send_to_char("Your victim is no longer here.\r\n", ch);
        return nullptr;
    }

    if (victim->specials.fighting) {
        send_to_char("Your target is too alert!\n\r", ch);
        return nullptr;
    }
    if (victim == ch) {
        send_to_char("How can you sneak up on yourself?\n\r", ch);
        return nullptr;
    }
    if (!CAN_SEE(ch, victim)) {
        send_to_char("Ambush who?\r\n", ch);
        return nullptr;
    }

    return victim;
}

/*
 * Decide whether an ambush succeeds or not.  If it does
 * succeed, then the return value is greater than 0.  Also,
 * the return value denotes how WELL the ambush succeeded.
 * Higher return values denote more successful ambushes.
 */
int ambush_calculate_success(struct char_data* ch, struct char_data* victim)
{
    int percent;

    percent = number(-100, 0);
    percent += number(-20, 20);
    percent -= GET_LEVELA(victim);
    percent -= IS_NPC(victim) ? 0 : GET_SKILL(victim, SKILL_AWARENESS) / 2;

    percent += GET_PROF_LEVEL(PROF_RANGER, ch) + 15;
    percent += GET_SKILL(ch, SKILL_AMBUSH) + get_real_stealth(ch);
    if (GET_POSITION(victim) <= POSITION_RESTING) {
        percent += 25 * (POSITION_FIGHTING - GET_POSITION(victim));
    }
    percent -= GET_AMBUSHED(victim);
    percent -= utils::get_encumbrance(*ch);

    return percent;
}

int calculate_ambush_damage_cap(const char_data* attacker)
{
    assert(attacker);

    int ranger_level = utils::get_prof_level(PROF_RANGER, *attacker);
    if (ranger_level <= LEVEL_MAX) {
        return ranger_level * 10;
    } else {
        ranger_level -= LEVEL_MAX;
        return LEVEL_MAX * 10 + ranger_level * 4;
    }
}

/*
 * Calculate ambush damage.  Keep in mind that the modifier can
 * be anywhere from 0 to 200.  On average, for a 30r with stealth
 * against an opponent with no awareness, the modifier should be
 * on the order of 60.
 */
int ambush_calculate_damage(char_data* attacker, char_data* victim, int modifier)
{
    if (modifier <= 0)
        return 0;

    int weapon_bulk = 0;
    int weapon_dmg = 0;

    if (attacker->equipment[WIELD] != NULL) {
        weapon_bulk = attacker->equipment[WIELD]->get_bulk();
        weapon_dmg = get_weapon_damage(attacker->equipment[WIELD]);
    }

    int damage_dealt = 60 + modifier;

    /* Scale by the amount of hp the victim has */
    damage_dealt *= std::min(GET_HIT(victim), GET_PROF_LEVEL(PROF_RANGER, attacker) * 20);

    /* Penalize for gear encumbrance and weapon encumbrance */
    damage_dealt /= 400 + 5 * (utils::get_encumbrance(*attacker) + utils::get_leg_encumbrance(*attacker) + weapon_bulk * weapon_bulk);

    /* Add a small constant amount of damage */
    damage_dealt += GET_PROF_LEVEL(PROF_RANGER, attacker) - GET_LEVELA(victim) + 10;

    /* Apply stealth specialization amplification */
    if (utils::get_specialization(*attacker) == game_types::PS_Stealth) {
        if (IS_NPC(victim))
            damage_dealt = damage_dealt * 3 / 2;
        else
            damage_dealt = damage_dealt * 10 / 8;
    }

    /* Add damage based on weapon */
    damage_dealt += (weapon_dmg * weapon_dmg / 100) * GET_LEVELA(attacker) / 30;

    int soft_damage_cap = calculate_ambush_damage_cap(attacker);
    if (damage_dealt > soft_damage_cap) {
        damage_dealt = soft_damage_cap + ((damage_dealt - soft_damage_cap) / 3);
    }

    if (utils::is_pc(*attacker)) {
        sprintf(buf, "%s ambush damage of %3d.", GET_NAME(attacker), damage_dealt);
        mudlog(buf, NRM, LEVEL_GRGOD, TRUE);
    }

    return damage_dealt;
}

ACMD(do_ambush)
{
    struct char_data* victim;
    int success;
    int dmg;

    if (IS_SHADOW(ch)) {
        send_to_char("Hmm, perhaps you've spent too much time in"
                     " mortal lands.\r\n",
            ch);
        return;
    }

    if (IS_NPC(ch) && MOB_FLAGGED(ch, MOB_ORC_FRIEND)) {
        send_to_char("Leave that to your leader.\r\n", ch);
        return;
    }

    if (IS_SET(world[ch->in_room].room_flags, PEACEROOM)) {
        send_to_char("A peaceful feeling overwhelms you, and you cannot bring"
                     " yourself to attack.\n\r",
            ch);
        return;
    }

    if (!ch->equipment[WIELD]) {
        send_to_char("You must be wielding a weapon to ambush.\r\n", ch);
        return;
    }

    struct obj_data* weapon;
    weapon = ch->equipment[WIELD];
    game_types::weapon_type w_type = weapon->get_weapon_type();

    if ((weapon->obj_flags.value[2] > 2)) {
        if ((GET_RACE(ch) != RACE_HARADRIM) || (weapon_skill_num(w_type) != SKILL_SPEARS)) {
            send_to_char("You need to wield a smaller weapon to surprise your victim.\r\n", ch);
            return;
        }
    }

    if (!GET_SKILL(ch, SKILL_AMBUSH)) {
        send_to_char("Learning how to ambush would help!\r\n", ch);
        return;
    }

    /* Subcmd 0 is the first time ambush is called.  We require a target */
    if (subcmd == 0 && wtl == NULL) {
        send_to_char("Ambush who?\r\n", ch);
        return;
    }

    if (IS_AFFECTED(ch, AFF_SANCTUARY)) {
        appear(ch);
        send_to_char("You cast off your sanctuary!\r\n", ch);
        act("$n renouces $s sanctuary!", FALSE, ch, 0, 0, TO_ROOM);
    }

    game_rules::big_brother& bb_instance = game_rules::big_brother::instance();

    switch (subcmd) {
    case -1:
        abort_delay(ch);
        ch->delay.targ1.cleanup();
        ch->delay.targ2.cleanup();
        ch->delay.cmd = ch->delay.subcmd = 0;
        if (ch->specials.store_prog_number == 16)
            ch->specials.store_prog_number = 0;
        return;
    case 0:
        victim = ambush_get_valid_victim(ch, wtl);
        if (victim == NULL)
            return;

        if (!bb_instance.is_target_valid(ch, victim)) {
            send_to_char("You feel the Gods looking down upon you, and protecting your target.  "
                         "You don't leave your cover.\r\n",
                ch);
            return;
        }

        /* TARGET_CHAR stores the victim, TARGET_TEXT stores the keyword */
        if (wtl->targ1.type == TARGET_CHAR)
            WAIT_STATE_FULL(ch, skills[SKILL_AMBUSH].beats, CMD_AMBUSH, 1, 30, 0,
                GET_ABS_NUM(victim), victim, AFF_WAITING | AFF_WAITWHEEL, TARGET_CHAR);
        else
            WAIT_STATE_FULL(ch, skills[SKILL_AMBUSH].beats, CMD_AMBUSH, 1, 30, 0, 0,
                get_from_txt_block_pool(wtl->targ1.ptr.text->text),
                AFF_WAITING | AFF_WAITWHEEL, TARGET_TEXT);

        return;

    case 1:
        if (wtl == NULL) {
            vmudlog(BRF, "ERROR: ambush callback with no context");
            send_to_char("Error: ambush callback with no callback context.\r\n"
                         "Please report this to an immortal.\r\n",
                ch);
            return;
        }

        victim = ambush_get_valid_victim(ch, wtl);
        if (victim == nullptr)
            return;

        if (!bb_instance.is_target_valid(ch, victim)) {
            send_to_char("You feel the Gods looking down upon you, and protecting your target.  "
                         "You don't leave your cover.\r\n",
                ch);
            return;
        }

        success = ambush_calculate_success(ch, victim);
        if (success > 0) {
            /* Ambushed victims gain awareness and lose energy and parry */
            GET_ENERGY(victim) = GET_ENERGY(victim) / 2 - success * 3;

            SET_CURRENT_PARRY(victim) = 0;

            const int awareness_gain = IS_NPC(victim) ? 50 : 75;

            GET_AMBUSHED(victim) = GET_AMBUSHED(victim) * 3 / 4 + awareness_gain;

            if (!IS_SET(ch->specials2.act, MOB_AGGRESSIVE)) {
                GET_AMBUSHED(victim) += 15;
            }
        }

        dmg = ambush_calculate_damage(ch, victim, success);
        damage(ch, victim, dmg, SKILL_AMBUSH, 0);

        return;

    default:
        abort_delay(ch);
    }
}

/*
 * Check whether target->targ2.ptr.ch is matched by the
 * optional trap target keyword, which is stored in
 * target->targ1.ptr.text->text.
 */
struct char_data* trap_get_valid_victim(struct char_data* ch, struct waiting_type* target)
{
    char* keyword;
    struct char_data* victim;

    if (target->targ1.type == TARGET_NONE)
        victim = target->targ2.ptr.ch;
    else {
        /*
         * Two BIG distinctions: if the keyword begins with a
         * digit, then it's 1.uruk or 2.bear, and thus the keyword
         * is UNIQUE.  There is at most one 1.uruk and one 2.bear
         * in any given room.  If the keyword does not begin with
         * a digit, then the keyword may not be unique; i.e., if
         * the keyword is "uruk" and there are 3 uruks in the room,
         * then this could be any one of them.
         */
        keyword = target->targ1.ptr.text->text;
        if (isdigit(*keyword))
            victim = get_char_room_vis(ch, keyword, 0);
        else if (keyword_matches_char(ch, target->targ2.ptr.ch, keyword))
            victim = target->targ2.ptr.ch;

        /* The victim didn't match the keyword */
        if (victim != target->targ2.ptr.ch)
            return nullptr;
    }

    if (victim == nullptr)
        return nullptr;

    if (!CAN_SEE(ch, victim))
        return nullptr;

    if (victim->specials.fighting) {
        send_to_char("Your target is too alert!\n\r", ch);
        return nullptr;
    }
    if (victim == ch) {
        send_to_char("You attempt to trap yourself.\r\n", ch);
        return nullptr;
    }

    return victim;
}

void trap_cleanup_quiet(struct char_data* ch)
{
    abort_delay(ch);
    ch->delay.targ1.cleanup();
    ch->delay.targ2.cleanup();
    ch->delay.cmd = ch->delay.subcmd = 0;
    if (ch->specials.store_prog_number == 16)
        ch->specials.store_prog_number = 0;
}

bool can_do_trap(char_data& character, int subcmd)
{
    const int max_trap_weapon_bulk = 2;

    if (IS_SHADOW(&character)) {
        send_to_char("Shadows can't trap!\n\r", &character);
        return false;
    }

    if (IS_NPC(&character) && MOB_FLAGGED(&character, MOB_ORC_FRIEND)) {
        send_to_char("Leave that to your leader.\r\n", &character);
        return false;
    }

    if (!GET_SKILL(&character, SKILL_AMBUSH)) {
        send_to_char("You must learn how to ambush to set an effective trap.\r\n", &character);
        return false;
    }

    const obj_data* weapon = character.equipment[WIELD];
    if (!weapon) {
        if (subcmd != 0) {
            if (subcmd == -1)
                send_to_char("You abandon your trap.\r\n", &character);

            if (subcmd > 0)
                send_to_char("Your lack of weapon causes your trap to fail.\r\n", &character);

            trap_cleanup_quiet(&character);
        } else {
            send_to_char("You cannot trap without equipping a weapon.\r\n", &character);
        }
        return false;
    }

    int weapon_bulk = weapon->obj_flags.value[2];
    if (weapon_bulk > max_trap_weapon_bulk) {
        if (subcmd != 0) {
            if (subcmd == -1)
                send_to_char("You abandon your trap.\r\n", &character);

            if (subcmd > 0)
                send_to_char("Your heavy weapon causes your trap to fail.\r\n", &character);

            trap_cleanup_quiet(&character);
        } else {
            send_to_char("You must be using a lighter weapon to set a trap.\r\n", &character);
        }
        return false;
    }

    return true;
}

bool is_valid_subcommand(char_data& character, int sub_command, const waiting_type* wtl)
{
    /* Sanity check ... All subcmds past 0 are callbacks and require a context */
    if (sub_command > 0 && wtl == NULL) {
        vmudlog(BRF, "do_trap: subcmd=%d, but the context is NULL!", sub_command);
        vsend_to_char(&character,
            "ERROR: trap subcommand is %d, but the context is null.\r\n"
            "Please report this message to an immortal.\r\n",
            sub_command);

        return false;
    }

    return true;
}

// drelidan:  Copied ACMD macro here so I could see the arguments.
// void do_trap(struct char_data *ch, char *argument, struct waiting_type * wtl, int cmd, int
// subcmd)
ACMD(do_trap)
{
    static int ignore_recursion = 0;
    struct char_data* victim;
    int dmg;
    int success;

    // Early out if some preconditions aren't met.
    if (!can_do_trap(*ch, subcmd))
        return;

    // Perform a command sanity check.
    if (!is_valid_subcommand(*ch, subcmd, wtl))
        return;

    game_rules::big_brother& bb_instance = game_rules::big_brother::instance();

    /*
     * Subcommand callbacks:
     *  -1   Cancel the current trap.  See the SUPER HACK note in case 2 to
     *       see what the purpose of ignore_recursion is.
     *   0   Command issued by a player or mob.  Has either a text keyword
     *       as an argument or has no argument.
     *   1   Callback: trap setup complete.  We cheat a little and store
     *       important target data on the character's waiting structure.
     *       This may cause problems if some other wait activity happens,
     *       because then the target data for our trap will be cleared.
     *   2   do_trap was called from react_trap in spec_pro.cc.  This means
     *       that someone has entered the room with ch.  If the person who
     *       entered matches ch's target data, then ch will attempt to trap
     *       them.  We delay ch's trap for 1 game tick before letting it go
     *       off.
     *   3   The trap is actually occurring.  Damage and bash the victim if
     *       the trap is successful.  Damage and success percent are heavily
     *       based on ambush success and damage.
     */
    switch (subcmd) {
    case -1:
        /* XXX: SUPER HACK */
        if (ignore_recursion) {
            ignore_recursion = 0;
            return;
        }
        send_to_char("You abandoned your trap.\r\n", ch);
        trap_cleanup_quiet(ch);
        break;

    case 0:
        send_to_char("You begin setting up your trap...\r\n", ch);

        /* If there's a target keyword, then store it */
        if (wtl->targ1.type == TARGET_TEXT) {
            WAIT_STATE_FULL(ch, skills[SKILL_AMBUSH].beats * 2, CMD_TRAP, 1, 30, 0, 0,
                get_from_txt_block_pool(wtl->targ1.ptr.text->text),
                AFF_WAITING | AFF_WAITWHEEL, TARGET_TEXT);
        } else {
            WAIT_STATE_FULL(ch, skills[SKILL_AMBUSH].beats * 2, CMD_TRAP, 1, 30, 0, 0, NULL,
                AFF_WAITING | AFF_WAITWHEEL, TARGET_NONE);
        }
        break;

    case 1:
        send_to_char("You begin to wait patiently for your victim.\r\n", ch);

        /* Use the wait state to store the target data */
        if (wtl->targ1.type == TARGET_TEXT) {
            WAIT_STATE_FULL(ch, -1, CMD_TRAP, 2, 30, 0, 0,
                get_from_txt_block_pool(wtl->targ1.ptr.text->text), 0, TARGET_TEXT);
        } else {
            WAIT_STATE_FULL(ch, -1, CMD_TRAP, 2, 30, 0, 0, NULL, 0, TARGET_NONE);
        }

        /* We use spec_prog 16 for people with trap */
        ch->specials.store_prog_number = 16;
        return;

    case 2:
        victim = trap_get_valid_victim(ch, wtl);
        if (victim == NULL)
            return;

        ch->specials.store_prog_number = 0;

        /*
         * XXX: SUPER HACK.  If we are here, then case 1 has happened already.
         * In case 1, we store trap's target information in the character's
         * delay variable.  However, when we call WAIT_STATE_FULL, it will call
         * complete_delay, which will then call do_trap with subcmd -1 to clear
         * the data we stored there.  Once those targets are deleted, we're
         * screwed.
         *
         * So what we have here is a static variable that we set to 1 when we
         * know this screwball scenario is going to happen.  When ignore_recursion
         * is 1, then case -1 above reacts accordingly and does not clear the
         * target data.
         */
        ignore_recursion = 1;
        if (wtl->targ1.type == TARGET_TEXT) {
            WAIT_STATE_FULL(ch, 1, CMD_TRAP, 3, 40, 0, 0,
                get_from_txt_block_pool(wtl->targ1.ptr.text->text), AFF_WAITING,
                TARGET_TEXT);
        } else {
            WAIT_STATE_FULL(ch, 1, CMD_TRAP, 3, 40, 0, 0, NULL, AFF_WAITING, TARGET_NONE);
        }

        /* WAIT_STATE_FULL clobbers targ2 unconditionally, so we refill it */
        ch->delay.targ2 = wtl->targ2;
        break;

    case 3:
        victim = trap_get_valid_victim(ch, wtl);
        if (victim == NULL) {
            /* Reset the trap.  do_trap subcmd=1 does exactly this. */
            do_trap(ch, "", wtl, CMD_TRAP, 1);
            return;
        }

        /* If player is afk remove flag and protection. Don't afk with trap set... */
        if (IS_SET(PLR_FLAGS(ch), PLR_ISAFK)) {
            REMOVE_BIT(PLR_FLAGS(ch), PLR_ISAFK);
            bb_instance.on_character_returned(ch);
        }

        if (!bb_instance.is_target_valid(ch, victim)) {
            send_to_char("You feel the Gods looking down upon you, and protecting your target.  "
                         "You remain in wait...\r\n",
                ch);

            /* Reset the trap.  do_trap subcmd=1 does exactly this. */
            do_trap(ch, "", wtl, CMD_TRAP, 1);
            return;
        }

        trap_cleanup_quiet(ch); /* Removes the spec prog */
        success = ambush_calculate_success(ch, victim);

        if (success < 0) {
            damage(ch, victim, 0, SKILL_TRAP, 0);
        } else {
            dmg = ambush_calculate_damage(ch, victim, success);
            dmg = dmg >> 1; // Cut the damage in half?  Easier ways to do this.  dmg = dmg >> 1;

            // Set a bash affection on the victim
            WAIT_STATE_FULL(victim, 5, CMD_BASH, 2, 80, 0, 0, 0, AFF_WAITING | AFF_BASH,
                TARGET_IGNORE);

            damage(ch, victim, dmg, SKILL_TRAP, 0);
        }
        break;

    default: {
        abort_delay(ch);
        break;
    }
    }
}

ACMD(do_calm)
{
    int calm_skill;
    struct char_data* victim;
    struct waiting_type tmpwtl;
    struct affected_type af;

    if (IS_SHADOW(ch)) {
        send_to_char("You are too airy for that.\r\n", ch);
        return;
    }

    calm_skill = GET_SKILL(ch, SKILL_CALM) + GET_RAW_SKILL(ch, SKILL_ANIMALS) / 2;

    if (GET_SPEC(ch) == PLRSPEC_PETS)
        calm_skill += 30;
    /*
     * Set up the victim pointer
     * Case -1 Cancelled a calm in progess
     * Case 0 Entered "Calm Target"
     * Case 1 Finished Calm delay
     * Default should never happen
     */
    switch (subcmd) {
    case -1:
        abort_delay(ch);
        return;
    case 0:
        one_argument(argument, arg);
        if (!(victim = get_char_room_vis(ch, arg))) {
            send_to_char("Calm who?\r\n", ch);
            return;
        }
        break;
    case 1:
        if (wtl->targ1.type != TARGET_CHAR || !char_exists(wtl->targ1.ch_num)) {
            send_to_char("Your victim is no longer among us.\r\n", ch);
            return;
        }
        victim = (struct char_data*)wtl->targ1.ptr.ch;
        break;
    default:
        sprintf(buf2, "do_calm: illegal subcommand '%d'.\r\n", subcmd);
        mudlog(buf2, NRM, LEVEL_IMMORT, TRUE);
        return;
    }

    /* Validate victim */
    if (!CAN_SEE(ch, victim)) {
        send_to_char("Calm who?\r\n", ch);
        return;
    } else if (victim == ch) {
        send_to_char("You try to calm yourself.\r\n", ch);
        return;
    } else if (!IS_SET(MOB_FLAGS(victim), MOB_AGGRESSIVE)) {
        send_to_char("Your target is already calm.\r\n", ch);
        return;
    } else if (!IS_NPC(victim) || (GET_BODYTYPE(victim) != BODYTYPE_ANIMAL)) {
        send_to_char("You can only calm animals.\r\n", ch);
        return;
    } else if (GET_POS(victim) == POSITION_FIGHTING) {
        sprintf(buf, "%s is too enraged!\r\n", GET_NAME(victim));
        send_to_char(buf, ch);
        return;
    } else if (GET_POS(victim) < POSITION_FIGHTING) {
        send_to_char("Your target needs to be standing.\r\n", ch);
    }
    switch (subcmd) {
    case 0:
        if (!GET_SKILL(ch, SKILL_CALM)) {
            send_to_char("Learn how to calm properly first!\n\r", ch);
            return;
        }
        /* #define is here for readability only. */

#define CALM_WAIT_BEATS \
    skills[SKILL_CALM].beats * 2 * GET_LEVEL(victim) / (GET_PROF_LEVEL(PROF_RANGER, ch) + calm_skill / 15)

        WAIT_STATE_FULL(ch, CALM_WAIT_BEATS, CMD_CALM, 1, 50, 0, GET_ABS_NUM(victim), victim,
            AFF_WAITING | AFF_WAITWHEEL, TARGET_CHAR);
#undef CALM_WAIT_BEATS

        break;
    case 1:
        if (ch->in_room != victim->in_room) {
            send_to_char("You target is not here any longer.\r\n", ch);
            return;
        }
        if (calm_skill > number(0, 150)) { /* success */
            if (!affected_by_spell(victim, SKILL_CALM)) {
                act("$n seems calmed.", FALSE, victim, 0, 0, TO_ROOM);
                af.type = SKILL_CALM;
                af.duration = -1;
                af.modifier = 0;
                af.location = APPLY_NONE;
                af.bitvector = 0;
                affect_to_char(victim, &af);
            }
            REMOVE_BIT(MOB_FLAGS(victim), MOB_AGGRESSIVE);
        } else {
            send_to_char("You fail to calm your target.\r\n", ch);
            sscanf(ch->player.name, "%s", buf);
            tmpwtl.targ1.type = TARGET_CHAR;
            tmpwtl.targ1.ptr.ch = ch;
            tmpwtl.targ1.ch_num = ch->abs_number;
            tmpwtl.cmd = CMD_HIT;
            do_hit(victim, buf, &tmpwtl, 0, 0);
        }
        break;

    default: /* Shouldn't ever happen */
        sprintf(buf2, "do_calm: illegal subcommand '%d'.\r\n", subcmd);
        mudlog(buf2, NRM, LEVEL_IMMORT, TRUE);
        abort_delay(ch);
        return;
    }
}

bool is_strong_enough_to_tame(char_data* tamer, char_data* animal, bool include_current_followers)
{
    int tame_skill = GET_SKILL(tamer, SKILL_TAME) + GET_RAW_SKILL(tamer, SKILL_ANIMALS) / 2;

    int levels_over_required = (GET_PROF_LEVEL(PROF_RANGER, tamer) / 3 + tame_skill / 30 - GET_LEVEL(animal));
    if (include_current_followers) {
        levels_over_required - get_followers_level(tamer);
    }

    if (affected_by_spell(animal, SKILL_CALM))
        levels_over_required += 1;

    if (GET_SPEC(tamer) == PLRSPEC_PETS)
        levels_over_required += 1;

    return levels_over_required >= 0;
}

ACMD(do_tame)
{
    int tame_skill, levels_over_required, tmp_abil;
    double tmp_skl;
    struct char_data* victim = NULL;
    struct waiting_type tmpwtl;
    struct affected_type af;
    affected_type* existing_affect;

    if (IS_SHADOW(ch)) {
        send_to_char("You are too insubstantial to do that.\r\n", ch);
        return;
    }

    if (IS_NPC(ch) && IS_AFFECTED(ch, AFF_CHARM)) {
        send_to_char("Your superior would not approve of your building"
                     " an army.\n\r",
            ch);
        return;
    }
    tame_skill = GET_SKILL(ch, SKILL_TAME) + GET_RAW_SKILL(ch, SKILL_ANIMALS) / 2;
    one_argument(argument, arg);
    if (GET_MOVE(ch) < 60) {
        send_to_char("You are too exhausted.\r\n", ch);
        return;
    }

    if (subcmd == -1) {
        abort_delay(ch);
        return;
    }

    if (!subcmd)
        if (!(victim = get_char_room_vis(ch, arg))) {
            send_to_char("Tame who?\r\n", ch);
            return;
        }

    if (subcmd == 1) {
        if (wtl->targ1.type != TARGET_CHAR || !char_exists(wtl->targ1.ch_num)) {
            send_to_char("Your victim is no longer among us.\r\n", ch);
            return;
        }
        victim = (struct char_data*)wtl->targ1.ptr.ch;
    }

    double ranger_level = (double)GET_PROF_LEVEL(PROF_RANGER, ch) / 3;
    double divided_tame_skill = (double)tame_skill / 30;
    double skill_total = ranger_level + divided_tame_skill;
    levels_over_required = (int)skill_total - GET_LEVEL(victim) - get_followers_level(ch);

    if (affected_by_spell(victim, SKILL_CALM))
        levels_over_required += 1;

    if (GET_SPEC(ch) == PLRSPEC_PETS)
        levels_over_required += 1;

    if (IS_SHADOW(ch)) {
        send_to_char("You are too airy for that.\r\n", ch);
        return;
    }

    one_argument(argument, arg);

    if (GET_PERCEPTION(victim) || !IS_NPC(victim) || GET_BODYTYPE(victim) != BODYTYPE_ANIMAL) {
        send_to_char("You can only tame animals.\r\n", ch);
        return;
    }
    /*
     * change the above to check for animal flag
     * change 'your target' to the appropriate message (animal's name)
     */
    if (GET_POS(victim) == POSITION_FIGHTING) {
        send_to_char("Your target is too enraged!\r\n", ch);
        return;
    }

    if (GET_POS(victim) < POSITION_FIGHTING) {
        send_to_char("Your target needs to be standing.\r\n", ch);
        return;
    }

    if (victim->master) {
        send_to_char("Your target is already following someone.\r\n", ch);
        return;
    }

    if (levels_over_required < 0) {
        send_to_char("Your target is too powerful for you to tame.\r\n", ch);
        return;
    }

    switch (subcmd) {
    case 0:
        if (victim == ch) {
            send_to_char("Aren't we funny today...\r\n", ch);
            return;
        }

        if (!GET_SKILL(ch, SKILL_TAME)) {
            send_to_char("Learn how to tame properly first!\r\n ", ch);
            return;
        }

        WAIT_STATE_FULL(ch,
            skills[SKILL_TAME].beats * GET_LEVEL(victim) * 2 / (GET_PROF_LEVEL(PROF_RANGER, ch) + 1),
            CMD_TAME, 1, 50, 0, GET_ABS_NUM(victim), victim,
            AFF_WAITING | AFF_WAITWHEEL, TARGET_CHAR);
        return;

    case 1:
        if (GET_POS(ch) == POSITION_FIGHTING) {
            send_to_char("You are too busy to do that.\r\n", ch);
            return;
        }

        if (ch->in_room != victim->in_room) {
            send_to_char("You target is not here any longer.\r\n", ch);
            return;
        }

        tmp_skl = ((double)GET_SKILL(ch, SKILL_TAME) / (double)110);
        tmp_abil = tmp_skl * 95 + (double)GET_PROF_LEVEL(PROF_RANGER, ch) / (double)36 * 9;
        existing_affect = affected_by_spell(ch, SPELL_ANGER);
        if (existing_affect && existing_affect->duration > 2) {
            tmp_abil = tmp_abil - (double)tmp_abil * .2;
        }

        if (tmp_abil > number(0, 100)) {
            if (circle_follow(victim, ch, FOLLOW_MOVE)) {
                send_to_char("Sorry, following in circles is not allowed.\r\n", ch);
                return;
            }

            if (victim->master) {
                stop_follower(victim, FOLLOW_MOVE);
            }

            if (GET_LOADLINE(victim)) {
                zone_table[GET_LOADZONE(victim)].cmd[GET_LOADLINE(victim) - 1].existing--;
                if (zone_table[GET_LOADZONE(victim)].cmd[GET_LOADLINE(victim) - 1].existing < 0) {
                    zone_table[GET_LOADZONE(victim)].cmd[GET_LOADLINE(victim) - 1].existing = 0;
                }
            }

            affect_from_char(victim, SKILL_TAME);
            add_follower(victim, ch, FOLLOW_MOVE);

            act("$n seems tamed.", FALSE, victim, 0, 0, TO_ROOM);
            af.type = SKILL_TAME;
            af.duration = -1;
            af.modifier = 0;
            af.location = APPLY_NONE;
            af.bitvector = AFF_CHARM;

            affect_to_char(victim, &af);

            REMOVE_BIT(MOB_FLAGS(victim), MOB_SPEC);
            victim->specials.store_prog_number = 0;
            REMOVE_BIT(MOB_FLAGS(victim), MOB_AGGRESSIVE);
            REMOVE_BIT(MOB_FLAGS(victim), MOB_STAY_ZONE);
            SET_BIT(MOB_FLAGS(victim), MOB_PET);

            victim->specials2.pref = 0;
            /*
             * Removal of mob aggressions
             * Addition of move bonus
             */
            GET_MOVE(ch) -= 60;
            GET_MOVE(victim) = GET_MAX_MOVE(victim) += 50;
            if (GET_SPEC(ch) == PLRSPEC_PETS) {
                victim->constabilities.str += 2;
                victim->tmpabilities.str += 2;
                victim->abilities.str += 2;
                victim->points.ENE_regen += 40;
                victim->points.damage += 2;
            }
        } else {
            send_to_char("You fail to tame your target.\r\n", ch);
            sscanf(ch->player.name, "%s", buf);
            tmpwtl.targ1.type = TARGET_CHAR;
            tmpwtl.targ1.ptr.ch = ch;
            tmpwtl.targ1.ch_num = ch->abs_number;
            tmpwtl.cmd = CMD_HIT;
            do_hit(victim, buf, &tmpwtl, 0, 0);
        }

    default:
        abort_delay(ch);
        return;
    }
}

ACMD(do_whistle)
{
    int rm_num, zone_num, cur_room_num;
    struct room_data* rm;
    struct char_data* tmpch;

    cur_room_num = ch->in_room;
    zone_num = world[cur_room_num].zone;

    if (IS_SHADOW(ch)) {
        send_to_char("You need to breathe to whistle!\r\n", ch);
        return;
    }

    if (!subcmd) { /* setting a delay */
        send_to_char("You gather your breath.\r\n", ch);
        WAIT_STATE_FULL(ch, skills[SKILL_WHISTLE].beats, CMD_WHISTLE, 1, 50, 0, 0, 0,
            AFF_WAITING | AFF_WAITWHEEL, TARGET_NONE);
        return;
    } else if (subcmd == 1) {
        if ((GET_SKILL(ch, SKILL_WHISTLE) + (GET_RACE(ch) == 13 ? 99 : 0)) < number(0, 99)) {
            send_to_char("You whistle, but can barely hear yourself.\r\n", ch);
            act("$n whistles softly.", FALSE, ch, 0, 0, TO_ROOM);

            return;
        }
        send_to_char("You whistle powerfully.\r\n", ch);
        for (rm_num = 0; rm_num <= top_of_world; rm_num++) {
            rm = &world[rm_num];

            if (rm->zone != zone_num)
                continue;

            ch->in_room = rm_num;

            if (rm_num == cur_room_num)
                act("$n whistles powerfully.", FALSE, ch, 0, 0, TO_ROOM);
            else
                act("You hear a powerful whistle nearby.", FALSE, ch, 0, 0, TO_ROOM);

            for (tmpch = rm->people; tmpch; tmpch = tmpch->next_in_room) {
                if (MOB_FLAGGED(tmpch, MOB_PET) && (tmpch->master == ch)) {
                    /*
                     * this is the guy we need to come to ch
                     * Make him stand up
                     */
                    GET_POS(tmpch) = POSITION_STANDING;
                    update_pos(tmpch);
                    affected_type newaf;
                    newaf.type = SPELL_ACTIVITY;
                    newaf.duration = 5;
                    newaf.modifier = 1;
                    newaf.location = APPLY_SPEED;
                    newaf.bitvector = AFF_HUNT;
                    affect_to_char(tmpch, &newaf);
                    forget(tmpch, ch);
                    remember(tmpch, ch);
                }
            }
        }
        ch->in_room = cur_room_num;
    }
}

ACMD(do_stalk)
{
    int dir;

    if (IS_SHADOW(ch)) {
        send_to_char("You don't leave tracks anyway!.\n\r", ch);
        return;
    }

    if (!wtl || wtl->targ1.type != TARGET_DIR) {
        send_to_char("Improper command target - please notify an immortal.\n\r", ch);
        return;
    }

    dir = wtl->targ1.ch_num;

    if (special(ch, dir + 1, "", SPECIAL_COMMAND, 0))
        return;

    if (GET_KNOWLEDGE(ch, SKILL_STALK) <= 0) {
        do_move(ch, argument, wtl, dir + 1, 0);
        return;
    }

    if (!CAN_GO(ch, dir)) {
        send_to_char("You cannot go that way.\n\r", ch);
        return;
    }

    if (IS_RIDING(ch)) {
        send_to_char("You cannot control tracks of your mount.\n\r", ch);
        return;
    }

    if (world[ch->in_room].sector_type == SECT_WATER_NOSWIM) {
        send_to_char("Water keeps no tracks.\n\r", ch);
        return;
    }

    WAIT_STATE_BRIEF(ch, skills[SKILL_STALK].beats, dir + 1, SCMD_STALK, 30,
        AFF_WAITING | AFF_WAITWHEEL);
    ch->delay.targ1.type = TARGET_DIR;
    ch->delay.targ1.ch_num = dir;

    act("$n looks carefully at the ground.", TRUE, ch, 0, 0, TO_ROOM);
    sprintf(buf, "You look for a discreet way %s.\n\r", dirs[dir]);
    send_to_char(buf, ch);
    return;
}

ACMD(do_cover)
{
    room_data* tmproom;
    int tmp, dir, dt, tr_time;

    if (IS_SHADOW(ch)) {
        send_to_char("You are too insubstantial to do that.\r\n", ch);
        return;
    }

    if (subcmd < 0) {
        send_to_char("You abandoned your track covering.\r\n", ch);
        return;
    }

    if (subcmd == 0) {

        if (GET_KNOWLEDGE(ch, SKILL_STALK) <= 0) {
            send_to_char("You fumble around, trying to tidy up the place.\r\n", ch);
        } else {
            WAIT_STATE_BRIEF(ch, skills[SKILL_STALK].beats, CMD_COVER, 1, 30,
                AFF_WAITING | AFF_WAITWHEEL);
            send_to_char("You start covering up the tracks.\r\n", ch);
        }
        act("$n moves around, making small adjustments.", TRUE, ch, 0, 0, TO_ROOM);
        return;
    }

    if (subcmd != 1) {
        send_to_char("Wrong subcmd in the track covering. Aborting.\r\n", ch);
        return;
    }

    tmproom = &world[ch->in_room];

    for (tmp = 0; tmp < NUM_OF_TRACKS; tmp++) {
        if (tmproom->room_track[tmp].char_number != 0 && GET_KNOWLEDGE(ch, SKILL_STALK) * 2 > number(1, 100)) {
            dt = number(1, 20);
            tr_time = tmproom->room_track[tmp].condition;
            dir = tmproom->room_track[tmp].data & 7;
            if (dt + tr_time >= 24) {
                /* Tracks being removed in this if*/
                tmproom->room_track[tmp].char_number = 0;
                tmproom->room_track[tmp].data = 0;
                tmproom->room_track[tmp].condition = 0;
            } else {
                tmproom->room_track[tmp].condition += dt;
            }
        }
    }

    send_to_char("You sweep over the room, getting rid of the tracks.\r\n", ch);
}

ACMD(do_hunt)
{
    if (IS_SHADOW(ch)) {
        send_to_char("You can't notice details like that.\r\n", ch);
        return;
    }

    if (!IS_AFFECTED(ch, AFF_HUNT)) {
        send_to_char("Ok, you'll try to notice fresh tracks.\r\n", ch);
        SET_BIT(ch->specials.affected_by, AFF_HUNT);
    } else {
        send_to_char("You stop looking for fresh tracks.\r\n", ch);
        REMOVE_BIT(ch->specials.affected_by, AFF_HUNT);
    }
}

/*
 * Used when a character successfully sneaks into a room
 * without anyone noticing.  As the name implies, `snuck_in'
 * assumes that `ch' has snuck into his new room successfully;
 * this, of course, assumes that `ch' is at least sneaking!
 * Note also that the small hide value awarded by snuck_in is
 * a normal hide value, and will vanish should the character
 * perform any revealing action; because of the HIDE_SNUCK_IN
 * bit, stop_hiding needs to unset this bit in addition to
 * removing the GET_HIDING level.  snuck_in also facilitates
 * the small delay associated with using sneak.
 * The purpose is to:
 *   - notify the player that he moved without catching attention,
 *     assuming that there is at least one visible character visible
 *     to `ch' in the room he is entering.
 *   - cause a regular hide (this is so that sneak can actually be
 *     used vs. mobs.)
 *   - set the SNUCK_IN bit in specials2, so that get_real_stealth
 *     calls can determine whether or not we should bonus this
 *     character for his sneaking
 */
void snuck_in(struct char_data* ch)
{
    GET_HIDING(ch) = number(hide_prof(ch) / 3, hide_prof(ch) * 4 / 5);
    if (GET_HIDING(ch) > 0) {
        SET_BIT(ch->specials.affected_by, AFF_HIDE);
    }
    SET_BIT(ch->specials2.hide_flags, HIDING_SNUCK_IN);

    char_data* observer = NULL;
    for (observer = world[ch->in_room].people; observer; observer = observer->next_in_room) {
        if (observer != ch && CAN_SEE(ch, observer)) {
            break;
        }
    }

    if (observer) {
        send_to_char("You managed to enter without anyone noticing.\r\n", ch);
    }

    /*
     * If you're hunting, we don't want sneak to make your hunt delay
     * non-existent, so we add the hunt delay to the sneak delay if
     * you're hunting.
     *
     * For those confused as to why we use the variable `wait': since
     * macro expansion simply replaces the text in the macro with the
     * text we send it as arguments, sending 'ch->delay.wait_value + 2'
     * doesn't actually work.  WAIT_STATE is called, it calls
     * WAIT_STATE_FULL, which will call abort_delay (in the case of
     * hunt), which will set ch->delay.wait_value to 0.  Your wait time
     * will still be using the simple ch->delay.wait_value + 2 syntax
     * when it actually performs the wait_value assignment in
     * WAIT_STATE_FULL, but the value in ch->delay.wait_value will
     * already be 0.
     *
     * Another worthy thing to note is that the WAIT_STATE macros seem
     * to go through a lot of angst to circumvent the buzz-worthy 'double
     * delays'; a double delay is just what is happening here: the hunt
     * delay is still active, and we're throwing another on top of it.
     * I don't really see what the problem with this is, assuming both
     * delays were set with WAIT_STATE (and they are, for this case).
     * Notice that if anyone ever muddles any sort of delay, sneak won't
     * destroy the delay's value, but it'll destroy its other info.. not
     * quite sure what we should do about this.
     */

    // Characters that are stealth spec do not have a sneak delay.
    int wait = ch->delay.wait_value + 2;
    if (GET_PROF_LEVEL(PROF_RANGER, ch) > number(0, 60))
        wait = wait - 1;

    if (utils::get_specialization(*ch) == game_types::PS_Stealth) {
        wait = wait * 0.5;
    }

    WAIT_STATE(ch, wait);
}

/*
 * snuck_out checks to see how many characters in the room (with
 * the exception of `ch' itself) `ch' is leaving are visible to
 * `ch'.  If this number is non-zero, then we send `ch' a message
 * confirming a successful sneak.  Do note that snuck_in does
 * assume that the sneak was performed successfully.
 */
void snuck_out(struct char_data* ch)
{
    struct char_data* t;

    for (t = world[ch->in_room].people; t; t = t->next_in_room)
        if (t != ch && CAN_SEE(ch, t))
            break;
    if (t)
        send_to_char("You seem to have left without raising any alarm.\r\n", ch);
}

/*
 * hide_prof calculates the level of GET_HIDING that `hider'
 * should be able to hide himsef at, using mainly the number
 * generated by get_real_stealth and `hider's hide skill.
 * See get_real_stealth to see what's involved in this.
 *
 * Numbers for non-30 ranger below are absolete
 * hide_chance for a level 30 ranger with 100% hide and 100%
 * stealth in the an average stealth-bonus area operates in the
 * range of [75, 125].  A level 36 ranger hobbit with 100% hide/
 * stealth in the highest stealth area pulls a hide_chance in the
 * range of [135, 180].  In all generality, it's pretty safe to
 * target the [110, 140] range as the "legend hide" range.
 *
 * Note: the low end of the ranges above assume the hider is
 * performing a normal hide; the high end assumes the hider is
 * performing a hide well.
 */
int hide_prof(struct char_data* hider)
{
    int hide_value;
    int get_real_stealth(struct char_data*);

    hide_value = GET_SKILL(hider, SKILL_HIDE) + get_real_stealth(hider) + GET_PROF_LEVEL(PROF_RANGER, hider) - 30;

    return hide_value;
}

/*
 * see_hiding calculates the level of GET_HIDING that
 * `seeker' should be able to see, taking into account:
 *  - the ranger level of `seeker'
 *  - the amount of awareness `seeker' has practiced
 *  - the intelligence of `seeker'
 *  - the specialization of `seeker' (stealth spec gets a bonus)
 *  - the race of `seeker' (elves get a bonus)
 */
int see_hiding(struct char_data* seeker)
{
    int can_see, awareness;

    if (IS_NPC(seeker))
        awareness = std::min(100, 40 + GET_INT(seeker) + GET_LEVEL(seeker));
    else
        awareness = GET_SKILL(seeker, SKILL_AWARENESS) + GET_INT(seeker);

    can_see = awareness * GET_PROF_LEVEL(PROF_RANGER, seeker) / 30;

    if (GET_SPEC(seeker) == PLRSPEC_STLH)
        can_see += 5;

    if (GET_RACE(seeker) == RACE_WOOD)
        can_see += 5;

    return can_see;
}

bool check_archery_accuracy(const char_data& archer, const char_data& victim)
{
    using namespace utils;

    double probability = get_prof_level(PROF_RANGER, archer) * 0.01; // 30% chance at 30r
    probability -= get_skill_penalty(archer) * 0.0001; // minus any skill penalty
    probability -= get_dodge_penalty(archer) * 0.0001; // minus any dodge penalty
    probability *= get_skill(archer, SKILL_ACCURACY) * 0.01; // scaled by skill - 100% gives us the above

    double roll = number();

    return roll < probability;
}

int shoot_calculate_success(const char_data& archer)
{
    int success_chance = 0;
    success_chance += utils::get_skill(archer, SKILL_ARCHERY);
    success_chance += utils::get_skill(archer, SKILL_ACCURACY) / 10;

    return success_chance;
}

int get_hit_location(const char_data& victim)
{
    int hit_location = 0;

    int body_type = victim.player.bodytype;

    const race_bodypart_data& body_data = bodyparts[body_type];
    if (body_data.bodyparts != 0) {
        int roll = number(1, 100);
        while (roll > 0 && hit_location < MAX_BODYPARTS) {
            roll -= body_data.percent[hit_location++];
        }
    }

    if (hit_location > 0)
        --hit_location;

    return hit_location;
}

int apply_armor_to_arrow_damage(char_data& archer, char_data& victim, int damage, int location)
{
    /* Bogus hit location */
    if (location < 0 || location > MAX_WEAR)
        return 0;

    /* If they've got armor, let's let it do its thing */
    obj_data* armor = victim.equipment[location];
    if (armor) {
        const obj_flag_data& obj_flags = armor->obj_flags;
        if (obj_flags.is_chain() || obj_flags.is_metal()) {
            // The target has armor, but we made an accurate shot.
            if (check_archery_accuracy(archer, victim)) {
                act("You manage to find a weakness in $N's armor!", TRUE, &archer, NULL, &victim,
                    TO_CHAR);
                act("$n manages to find a weakness in $N's armor!", TRUE, &archer, NULL, &victim,
                    TO_NOTVICT);
                act("$n notices a weakness in your armor!", TRUE, &archer, NULL, &victim, TO_VICT);
                return damage;
            }

            /* First, remove minimum absorb */
            int damage_reduction = armor->get_base_damage_reduction();

            /* Then apply the armor_absorb factor */
            damage_reduction += (damage * armor_absorb(armor) + 50) / 100;

            if (obj_flags.is_chain()) {
                // Chain is half-effective against shooting.
                damage_reduction = damage_reduction / 2;
            }

            // Reduce damage here, but not below 1.
            damage -= damage_reduction;
            damage = std::max(damage, 1);
        }
    }

    return damage;
}

//============================================================================
// Returns a multiplier to archery damage based on ranger level.
//============================================================================
double get_ranger_level_multiplier(int ranger_level)
{
    const double base_multiplier = 0.8;

    if (ranger_level <= 20)
        return base_multiplier;

    // ranger level > 20
    int num_steps = ranger_level - 20;
    return base_multiplier + num_steps * 0.02; // ranger level 20 is 80% damage, 25 is 90% damage, 30 is base damage
}

/*
 * shoot_calculate_damage
 *
 * Damage should be based on the archer's ranger level, dexterity modifier,
 *  the arrows that they are using, and the bow that they are using.
 *
 * If the archer performs an accurate shot, the target's armor is ignored.
 * --------------------------- Change Log --------------------------------
 * slyon: Jan 24, 2017 - Created function
 * drelidan: Jan 31, 2017 - First pass implementation.  Doesn't take weapon or
 *   arrow type into account.
 * slyon: Feb 3, 2017 - Added arrowto_dam into the equation
 * drelidan: Feburary 7, 2017 - Add weapon damage into the equation.
 */
int shoot_calculate_damage(char_data* archer, char_data* victim, const obj_data* arrow,
    int& hit_location)
{
    using namespace utils;

    int ranger_level = get_prof_level(PROF_RANGER, *archer);
    double ranger_level_factor = (ranger_level * 0.5) * number_d(0.5, 1.0);
    double strength_factor = (archer->get_cur_str() - 10) * 0.5;

    int arrow_todam = arrow->obj_flags.value[1];

    obj_data* bow = archer->equipment[WIELD];
    double weapon_damage = get_weapon_damage(bow);
    double random_cap = (arrow_todam + weapon_damage) * 1.25; // should be between ~4 and 30 at the ABSOLUTE max

    double random_factor_1 = number(random_cap);

    double bow_factor = random_factor_1 + strength_factor;

    double multipler = get_ranger_level_multiplier(ranger_level);
    int damage = int(ranger_level_factor + (bow_factor * multipler));

    int arrow_hit_location = get_hit_location(*victim);

    int body_type = victim->player.bodytype;
    const race_bodypart_data& body_data = bodyparts[body_type];

    // Apply damage reduction.
    int armor_location = body_data.armor_location[arrow_hit_location];
    damage = apply_armor_to_arrow_damage(*archer, *victim, damage, armor_location);

    hit_location = arrow_hit_location;
    return damage;
}

int shoot_calculate_wait(const char_data* archer)
{
    const int base_beats = 12;
    int min_beats = 4;

    int total_beats = base_beats - ((archer->points.ENE_regen / base_beats) - base_beats);
    total_beats = total_beats - (utils::get_prof_level(PROF_RANGER, *archer) / base_beats);

    if (archer->player.race == RACE_WOOD || archer->player.race == RACE_HARADRIM) {
        total_beats = total_beats - 1;
    }

    if (GET_SHOOTING(archer) == SHOOTING_FAST) {
        total_beats = total_beats / 2;
        min_beats = 3;
    } else if (GET_SHOOTING(archer) == SHOOTING_SLOW) {
        total_beats = total_beats * 2;
        min_beats = 8;
    }

    total_beats = std::max(total_beats, min_beats);

    return total_beats;
}

/*
 * This function will determine if an arrow breaks based on the
 * victims armor and percentage on arrows themself.
 * --------------------------- Change Log --------------------------------
 * drelidan: Jan 26, 2017 - Created function
 */
bool does_arrow_break(const char_data* archer, const char_data* victim, obj_data* arrow,
    int hit_location)
{
    if (hit_location < 0 || hit_location > MAX_WEAR) {
        return false;
    }

    obj_data* armor = victim->equipment[hit_location];

    if (armor == nullptr) {
        return false;
    }

    const obj_flag_data& obj_flags = armor->obj_flags;

    if (obj_flags.is_chain() || obj_flags.is_metal()) {
        int break_percentage = arrow->obj_flags.value[3];

        // Haradrims get a bonus with crude arrows for being a primitive race
        if ((break_percentage > 30) && (GET_RACE(archer) == RACE_HARADRIM)) {
            break_percentage >>= 1;
        }

        if (utils::get_specialization(*archer) == (int)game_types::PS_Archery) {
            break_percentage >>= 1;
        }

        const int rolledNumber = number(1, 100);
        if (rolledNumber < break_percentage) {
            extract_obj(arrow);
            return true;
        }
    }
    return false;
}

/*
 * move_arrow_to_victim will take the arrow out of the shooters quiver and
 * tag the arrow with their character_id. After that it will move the arrow
 * into the victims inventory. We tag the arrow so the shooter can type recover
 * after the kill, and it will return all arrows with his character_id on it.
 *
 * This function will also handle the breaking of arrows based on the
 * victims armor and percentage on arrows themselves.
 *
 * Returns true if the arrow was moved to the victim, false if it was destroyed.
 * --------------------------- Change Log --------------------------------
 * slyon: Jan 25, 2017 - Created function
 * drelidan: Jan 26, 2017 - Implemented function logic.
 */
bool move_arrow_to_victim(char_data* archer, char_data* victim, obj_data* arrow)
{
    // Remove object from the character.
    if (arrow->in_obj) {
        obj_from_obj(arrow);
    }
    obj_to_char(arrow, archer); // Move it into his inventory.
    // tag arrow in value slot 2 of the shooter
    if (!IS_NPC(archer)) {
        arrow->obj_flags.value[2] = (int)archer->specials2.idnum;
    } else if (IS_NPC(archer)) {
        arrow->obj_flags.value[2] = archer->abs_number;
    } else {
        return false;
    }

    // Move the arrow to the victim.
    obj_from_char(arrow);
    obj_to_char(arrow, victim);

    return true;
}

/*
 * move_arrow_to_room will take the arrow out of the shooters quiver and
 * tag the arrow with their character_id. After that it will deposit the arrow
 * on the ground. We tag the arrow so the shooter can type recover
 * after the kill and it will return all arrows with his character_id on it.
 *
 * This function will also handle the breaking of arrows based on the
 * victims armor and percentage on arrows themself.
 *
 * Returns true if the arrow was moved to the victim, false if it was destroyed.
 * --------------------------- Change Log --------------------------------
 * drelidan: Feb 07, 2017 - Created function
 */
void move_arrow_to_room(char_data* archer, obj_data* arrow, int room_num)
{
    // Remove object from the character.
    if (arrow->in_obj) {
        obj_from_obj(arrow);
    }

    obj_to_char(arrow, archer); // Move it into his inventory.

    // tag arrow in value slot 2 of the shooter
    arrow->obj_flags.value[2] = utils::is_pc(*archer) ? (int)archer->specials2.idnum : (int)archer->abs_number;

    // Move the arrow to the room.
    obj_from_char(arrow);
    obj_to_room(arrow, room_num);
}

/*
 * can_ch_shoot will check all the prereqs for shooting a bow
 *  -- Does the ch have the correct equipment
 *  -- Does the ch have SKILL_ARCHERY prac'd
 * --------------------------- Change Log --------------------------------
 * slyon: Jan 24, 2017 - Created function
 * slyon: Jan 25, 2017 - Renamed function to better reflect what it's doing
 */
bool can_ch_shoot(char_data* archer)
{
    using namespace utils;

    if (is_shadow(*archer)) {
        send_to_char("Hmm, perhaps you've spent too much time in the "
                     " mortal lands.\r\n",
            archer);
        return false;
    }

    if (is_npc(*archer) && is_mob_flagged(*archer, MOB_ORC_FRIEND)) {
        int dex = archer->get_cur_dex();
        if (dex < 18) {
            char_data* receiver = archer->master ? archer->master : archer;
            send_to_char("Your clumsy follower lacks the dexterity to use a bow.", receiver);
            return false;
        }
    }

    const room_data& room = world[archer->in_room];
    if (is_set(room.room_flags, (long)PEACEROOM)) {
        send_to_char("A peaceful feeling overwhelms you, and you cannot bring"
                     " yourself to attack.\r\n",
            archer);
        return false;
    }

    const obj_data* weapon = archer->equipment[WIELD];
    if (!weapon || !isname("bow", weapon->name)) {
        send_to_char("You must be wielding a bow to shoot.\r\n", archer);
        return false;
    }

    const obj_data* quiver = archer->equipment[WEAR_BACK];
    if (!quiver || !quiver->is_quiver()) {
        if (is_pc(*archer)) {
            send_to_char("You must be wearing a quiver on your back.\r\n", archer);
            return false;
        } else {
            // Ensure that the NPC has an arrow.
            obj_data* arrow = NULL;

            // Must be a follower and shooting from their inventory.  Find the first missile they
            // have.
            for (obj_data* item = archer->carrying; item; item = item->next) {
                if (item->obj_flags.type_flag == ITEM_MISSILE) {
                    arrow = item;
                    break;
                }
            }

            if (arrow == NULL) {
                char_data* receiver = archer->master ? archer->master : archer;
                send_to_char("Your follower is out of arrows.", receiver);
                return false;
            }
        }
    }

    if (is_pc(*archer) && !quiver->contains) {
        send_to_char("Your quiver is empty!  Find some more arrows.\r\n", archer);
        return false;
    }

    if (!is_twohanded(*archer)) {
        send_to_char("You must be wielding your bow with two hands.\r\n", archer);
        return false;
    }

    if (get_skill(*archer, SKILL_ARCHERY) == 0) {
        send_to_char("Learn how to shoot a bow first.\r\n", archer);
        return false;
    }

    return true;
}

/*
 * is_targ_valid will check to see if the target the shoot is a valid
 * one and return true if so and false if not.
 * --------------------------- Change Log --------------------------------
 * slyon: Jan 25, 2017 - Created function
 */
char_data* is_target_valid(char_data* archer, waiting_type* target)
{
    char_data* victim = nullptr;

    if (target->targ1.type == TARGET_TEXT) {
        victim = get_char_room_vis(archer, target->targ1.ptr.text->text);
    } else if (target->targ1.type == TARGET_CHAR) {
        if (char_exists(target->targ1.ch_num)) {
            victim = target->targ1.ptr.ch;
        }
    }

    if (victim == nullptr) {
        if (archer->specials.fighting) {
            victim = archer->specials.fighting;
        } else {
            send_to_char("Shoot who?\r\n", archer);
            return nullptr;
        }
    }

    if (archer->in_room != victim->in_room) {
        send_to_char("Your victim is no longer here.\r\n", archer);
        return nullptr;
    }

    if (archer == victim) {
        send_to_char("But you have so much to live for!\r\n", archer);
        return nullptr;
    }

    if (!CAN_SEE(archer, victim)) {
        send_to_char("Shoot who?\r\n", archer);
        return nullptr;
    }

    return victim;
}

//============================================================================
// Moves the arrow to the room specified.
//============================================================================
void do_move_arrow_to_room(char_data* archer, obj_data* arrow, int room_num)
{
    send_to_char("Your arrow harmlessly flies past your target.\r\n", archer);
    move_arrow_to_room(archer, arrow, room_num);
}

//============================================================================
// Handles an arrow hitting a target - calculating and applying damage, and
// moving the arrow to that target.
//============================================================================
void on_arrow_hit(char_data* archer, char_data* victim, obj_data* arrow)
{
    int hit_location = 0;
    int damage_dealt = shoot_calculate_damage(archer, victim, arrow, hit_location);
    if (!does_arrow_break(archer, victim, arrow, hit_location)) {
        move_arrow_to_victim(archer, victim, arrow);
    }
    if (GET_SHOOTING(archer) == SHOOTING_FAST) {
        damage_dealt = damage_dealt / 2;
    } else if (GET_SHOOTING(archer) == SHOOTING_SLOW) {
        damage_dealt = damage_dealt * 2;
    }
    sprintf(buf, "%s archery damage of %3d to %s.", GET_NAME(archer), damage_dealt,
        GET_NAME(victim));
    mudlog(buf, NRM, LEVEL_GRGOD, TRUE);

    damage(archer, victim, damage_dealt, SKILL_ARCHERY, hit_location);
}

//============================================================================
// Handles the arrow hitting another target.  Currently only considers characters
// in combat with the victim as potential targets.
//============================================================================
void change_arrow_target(char_data* archer, char_data* victim, obj_data* arrow)
{
    const room_data& room = world[archer->in_room];

    // Get the list of people that are in-combat with the victim, and
    // ensure that the archer isn't in the list of potential targets.

    typedef std::vector<char_data*>::iterator iter;

    std::vector<char_data*> potential_targets = utils::get_engaged_characters(victim, room);

    iter archer_iter = std::remove(potential_targets.begin(), potential_targets.end(), archer);
    if (archer_iter != potential_targets.end()) {
        potential_targets.erase(archer_iter);
    }

    iter victim_iter = std::remove(potential_targets.begin(), potential_targets.end(), victim);
    if (victim_iter != potential_targets.end()) {
        potential_targets.erase(victim_iter);
    }

    // If there aren't any targets, have the arrow fall into the room.
    if (potential_targets.empty()) {
        do_move_arrow_to_room(archer, arrow, archer->in_room);
    } else {
        int target_roll = number(0, potential_targets.size() - 1);
        char_data* new_victim = potential_targets.at(target_roll);

        send_to_char("Your shot misses your target and flies into someone else!\r\n", archer);
        on_arrow_hit(archer, new_victim, arrow);
    }
}

//============================================================================
// Gets the room that the arrow will land in.
//============================================================================
int get_arrow_landing_location(const room_data& room)
{
    // The arrow flies into an adjacent room.
    // Build the possible exit list...
    int valid_dirs = 0;
    int exit_indices[NUM_OF_DIRS] = { -1 };
    for (int i = 0; i < NUM_OF_DIRS; ++i) {
        // The room exit exits and goes somewhere.
        room_direction_data* dir = room.dir_option[i];
        if (dir && dir->to_room != NOWHERE) {
            // The exit has a door...
            if (utils::is_set(dir->exit_info, EX_ISDOOR)) {
                // But it's open, so the arrow can fly that way.
                if (!utils::is_set(dir->exit_info, EX_CLOSED)) {
                    exit_indices[valid_dirs++] = i;
                }
            }
            // There's no door - this is a valid location for the arrow to land.
            else {
                exit_indices[valid_dirs++] = i;
            }
        }
    }

    if (valid_dirs == 0) {
        // The arrow has to fall in the room passed in.
        return room.number;
    } else {
        int random_exit = number(0, valid_dirs - 1);
        int exit_index = exit_indices[random_exit];

        // The arrow flies into a nearby room.
        return room.dir_option[exit_index]->to_room;
    }
}

//============================================================================
// Handles an arrow missing the target.
// The arrow may impact into someone else, go into a separate room, or land
// harmlessly on the ground (if it doesn't break).
//============================================================================
void on_arrow_miss(char_data* archer, char_data* victim, obj_data* arrow)
{
    const room_data& room = world[archer->in_room];

    double roll = number();
    roll -= 0.20;
    if (roll <= 0) {
        change_arrow_target(archer, victim, arrow);
        return;
    }

    int arrow_landing_location = archer->in_room;

    roll -= 0.01;
    if (roll <= 0) {
        arrow_landing_location = get_arrow_landing_location(room);
    }
    int hit_location = get_hit_location(*victim);

    // The arrow falls into this or a nearby room.
    send_to_char("Your arrow harmlessly flies past your target.\r\n", archer);
    move_arrow_to_room(archer, arrow, arrow_landing_location);
    damage(archer, victim, 0, SKILL_ARCHERY, hit_location);
}

/*
 * do_shoot will attempt to shoot the victim with a bow or crossbow
 * There is a lot of things going on with this ACMD so just read through it.
 * --------------------------- Change Log --------------------------------
 * slyon: Jan 25, 2017 - Created ACMD
 */
ACMD(do_shoot)
{
    one_argument(argument, arg);

    if (subcmd == -1) {
        send_to_char("You could not concentrate on shooting anymore!\r\n", ch);
        ch->specials.ENERGY = std::min(ch->specials.ENERGY, 0); // reset swing timer after interruption.

        // Clean-up targets.
        wtl->targ1.cleanup();
        wtl->targ2.cleanup();
        return;
    }

    if (!can_ch_shoot(ch))
        return;

    char_data* victim = is_target_valid(ch, wtl);

    game_rules::big_brother& bb_instance = game_rules::big_brother::instance();
    if (!bb_instance.is_target_valid(ch, victim)) {
        send_to_char("You feel the Gods looking down upon you, and protecting your target.  You "
                     "lower your bow.\r\n",
            ch);
        return;
    }

    if (utils::is_affected_by(*ch, AFF_SANCTUARY)) {
        appear(ch);
        send_to_char("You cast off your sanctuary!\r\n", ch);
        act("$n renounces $s sanctuary!", FALSE, ch, 0, 0, TO_ROOM);
    }

    if (!check_hallucinate(ch, victim)) {
        return;
    }

    switch (subcmd) {
    case 0: {
        if (victim == NULL) {
            return;
        }
        send_to_char("You draw back your bow and prepare to fire...\r\n", ch);

        // Only send a message to the room if the character isn't hiding.
        // To onlookers, it will be impossible to tell if the archer is shooting or ambushing.
        if (!utils::is_affected_by(*ch, AFF_HIDE)) {
            if (GET_SEX(ch) == SEX_MALE) {
                act("$n draws back his bow and prepares to fire...\r\n", FALSE, ch, 0, 0, TO_ROOM);
            } else if (GET_SEX(ch) == SEX_FEMALE) {
                act("$n draws back her bow and prepares to fire...\r\n", FALSE, ch, 0, 0, TO_ROOM);
            } else {
                act("$n draws back their bow and prepares to fire...\r\n", FALSE, ch, 0, 0,
                    TO_ROOM);
            }
        }

        // Clean-up targets prior to setting new targets.
        wtl->targ1.cleanup();
        wtl->targ2.cleanup();

        int wait_delay = shoot_calculate_wait(ch);
        SET_CURRENT_PARRY(ch) = 0;
        WAIT_STATE_FULL(ch, wait_delay, CMD_SHOOT, 1, 30, 0, victim->abs_number, victim,
            AFF_WAITING | AFF_WAITWHEEL, TARGET_CHAR);
    } break;
    case 1: {
        if (victim == NULL) {
            return;
        }

        if (!CAN_SEE(ch, victim)) {
            send_to_char("Shoot who?\r\n", ch);
            return;
        }

        if (ch->in_room != victim->in_room) {
            send_to_char("Your target is not here any longer\r\n", ch);
            return;
        }

        // Get the arrow.
        obj_data* arrow = NULL;
        obj_data* quiver = ch->equipment[WEAR_BACK];
        if (quiver) {
            arrow = quiver->contains;
        } else {
            // Must be a follower and shooting from their inventory.  Find the first missile they
            // have.
            for (obj_data* item = ch->carrying; item; item = item->next) {
                if (item->obj_flags.type_flag == ITEM_MISSILE) {
                    arrow = item;
                    break;
                }
            }
        }
        send_to_char("You release your arrow and it goes flying!\r\n", ch);

        byte sex = ch->player.sex;
        if (sex == SEX_MALE) {
            act("$n releases his arrow and it goes flying!\r\n", FALSE, ch, 0, 0, TO_ROOM);
        } else if (sex == SEX_FEMALE) {
            act("$n releases her arrow and it goes flying!\r\n", FALSE, ch, 0, 0, TO_ROOM);
        } else {
            act("$n releases their arrow and it goes flying!\r\n", FALSE, ch, 0, 0, TO_ROOM);
        }

        int roll = number(0, 99);
        int target_number = shoot_calculate_success(*ch);
        if (roll < target_number) {
            on_arrow_hit(ch, victim, arrow);
        } else {
            on_arrow_miss(ch, victim, arrow);
        }

        ch->specials.ENERGY = std::min(ch->specials.ENERGY, 0); // reset swing timer after loosing an arrow.

        // Clean-up targets.
        wtl->targ1.cleanup();
        wtl->targ2.cleanup();
    } break;
    default:
        sprintf(buf2, "do_shoot: illegal subcommand '%d'.\r\n", subcmd);
        mudlog(buf2, NRM, LEVEL_IMMORT, TRUE);
        abort_delay(ch);
        break;
        ;
    }
}

//============================================================================
// Put the arrows recovered into the quiver worn on the characters
// back.
//============================================================================

void put_arrow_quiver(char_data* character, obj_data* arrow, obj_data* quiver)
{
    if (GET_OBJ_WEIGHT(quiver) + GET_OBJ_WEIGHT(arrow) > quiver->obj_flags.value[0]) {
        act("$p won't fit in $P.", FALSE, character, arrow, quiver, TO_CHAR);
    } else {
        obj_from_char(arrow);
        obj_to_obj(arrow, quiver);
    }
}

//============================================================================
// Gets all arrows in the object list that are tagged to the character.  These
// arrows are placed in the 'arrows' vector.
//============================================================================
void get_tagged_arrows(const char_data* character, obj_data* obj_list,
    std::vector<obj_data*>& arrows)
{
    // Iterate through items in the list.
    for (obj_data* item = obj_list; item; item = item->next_content) {
        if (item->obj_flags.type_flag == ITEM_MISSILE) {
            if (!IS_NPC(character)) {
                if (item->obj_flags.value[2] == character->specials2.idnum) {
                    arrows.push_back(item);
                }
            } else if (IS_NPC(character)) {
                if (item->obj_flags.value[2] == character->abs_number) {
                    arrows.push_back(item);
                }
            }
        }
    }
}

//============================================================================
// Gets all arrows in the room that are tagged to the character passed in.
//============================================================================
void get_room_tagged_arrows(const char_data* character, std::vector<obj_data*>& arrows)
{
    const room_data& room = world[character->in_room];
    obj_data* obj_list = room.contents;

    return get_tagged_arrows(character, obj_list, arrows);
}

//============================================================================
// Gets all arrows from the corpses in the room that are tagged to the
// character passed in.
//============================================================================
void get_corpse_tagged_arrows(const char_data* character, std::vector<obj_data*>& arrows)
{
    const room_data& room = world[character->in_room];
    obj_data* obj_list = room.contents;

    // Iterate through items in the list.
    for (obj_data* item = obj_list; item; item = item->next_content) {
        if (strstr(item->name, "corpse") != NULL) {
            get_tagged_arrows(character, item->contains, arrows);
        }
    }
}

//============================================================================
// Recovers arrows that have been tagged by a player from the room that the
// player is in, and any corpses within the room.
//============================================================================
void do_recover(char_data* character, char* argument, waiting_type* wait_list, int command,
    int sub_command)
{
    if (character == NULL)
        return;

    bool has_quiver;
    obj_data* quiver = character->equipment[WEAR_BACK];
    if (!quiver || !quiver->is_quiver()) {
        has_quiver = false;
    } else {
        has_quiver = true;
    }

    // Characters cannot recover arrows if they are blind.
    if (!CAN_SEE(character)) {
        send_to_char("You can't see anything in this darkness!\n\r", character);
        return;
    }

    // Characters cannot recover arrows if they are a shadow.
    if (utils::is_shadow(*character)) {
        send_to_char("Try rejoining the corporal world first...\n\r", character);
        return;
    }

    int max_inventory = utils::get_carry_item_limit(*character);
    int max_carry_weight = utils::get_carry_weight_limit(*character);

    std::vector<obj_data*> arrows_to_get;
    get_room_tagged_arrows(character, arrows_to_get);
    get_corpse_tagged_arrows(character, arrows_to_get);

    if (arrows_to_get.empty()) {
        send_to_char("You have no expended arrows here.\n\r", character);
        return;
    }

    int num_recovered = 0;

    typedef std::vector<obj_data*>::iterator iter;
    for (iter arrow_iter = arrows_to_get.begin(); arrow_iter != arrows_to_get.end(); ++arrow_iter) {
        obj_data* arrow = *arrow_iter;
        if (character->specials.carry_items < max_inventory) {
            if (character->specials.carry_weight + arrow->get_weight() < max_carry_weight) {
                ++num_recovered;
                obj_data* arrow_container = arrow->in_obj;
                if (arrow_container) {
                    obj_from_obj(arrow);
                }
                if (arrow->in_room >= 0) {
                    obj_from_room(arrow);
                }
                obj_to_char(arrow, character);
                if (has_quiver) {
                    put_arrow_quiver(character, arrow, quiver);
                }
            } else {
                send_to_char("You can't carry that much weight!\n\r", character);
                break;
            }
        } else {
            send_to_char("You can't carry that many items!\n\r", character);
            break;
        }
    }

    std::ostringstream message_writer;
    message_writer << "You recover " << num_recovered
                   << (num_recovered > 1 ? " arrows." : " arrow.") << std::endl;
    std::string message = message_writer.str();
    send_to_char(message.c_str(), character);

    act("$n recovers some arrows.\n\r", FALSE, character, NULL, NULL, TO_ROOM);
}

/*=================================================================================
   do_scan:
   This will scan all rooms in a straight line from the player and report back
   all mobs in the rooms. The characters ranger level determines how far they
   can scan. There are two hard stops for the loop one is if the room is dark and
   the player doesn't have infravision and two is if the direction is a door.
   ------------------------------Change Log---------------------------------------
   slyon: Sept 6, 2017 - Documented
==================================================================================*/
void do_scan(char_data* character, char* argument, waiting_type* wait_list, int command,
    int sub_command)
{
    struct char_data* i;
    int is_in, dir, dis, maxdis, found = 0;

    const char* distance[] = {
        "right here",
        "immediately ",
        "nearby ",
        "a ways ",
        "far ",
        "very far ",
        "extremely far ",
        "impossibly far ",
    };

    if (character == NULL)
        return;

    if (!CAN_SEE(character)) {
        send_to_char("You can't see anything in this darkness!\n\r", character);
        return;
    }

    if (utils::is_shadow(*character)) {
        send_to_char("Try rejoining the corporal world first...\n\r", character);
        return;
    }

    if ((GET_MOVE(character) < 3) && (GET_LEVEL(character) < LEVEL_IMMORT)) {
        act("You are too exhausted.", TRUE, character, 0, 0, TO_CHAR);
        return;
    }

    maxdis = (1 + ((GET_PROF_LEVEL(PROF_RANGER, character) / 3))) / 2;
    maxdis = std::max(1, maxdis);
    if (GET_LEVEL(character) >= LEVEL_IMMORT)
        maxdis = 7;

    act("You quickly scan the area and see:", TRUE, character, 0, 0, TO_CHAR);
    act("$n quickly scans the area.", FALSE, character, 0, 0, TO_ROOM);
    if (GET_LEVEL(character) < LEVEL_IMMORT)
        GET_MOVE(character) -= 3;

    is_in = character->in_room;
    for (dir = 0; dir < NUM_OF_DIRS; dir++) {
        character->in_room = is_in;
        for (dis = 0; dis <= maxdis; dis++) {
            if (((dis == 0) && (dir == 0)) || (dis > 0)) {
                for (i = world[character->in_room].people; i; i = i->next_in_room) {
                    if ((!((character == i) && (dis == 0))) && CAN_SEE(character, i)) {
                        if (dis > 0) {
                            sprintf(buf, "%33s: %s%s%s%s",
                                (IS_NPC(i) ? GET_NAME(i) : pc_star_types[i->player.race]),
                                distance[dis],
                                ((dis > 0) && (dir < (NUM_OF_DIRS - 2))) ? "to the " : "",
                                (dis > 0) ? dirs[dir] : "",
                                ((dis > 0) && (dir > (NUM_OF_DIRS - 3))) ? "wards" : "");
                            act(buf, TRUE, character, 0, 0, TO_CHAR);
                        }
                        found++;
                    }
                }
            }
            if (!CAN_GO(character, dir) || (world[character->in_room].dir_option[dir]->to_room == is_in) || (IS_SET(EXIT(character, dir)->exit_info, EX_NO_LOOK)))
                break;
            else
                character->in_room = world[character->in_room].dir_option[dir]->to_room;
        }
    }
    if (found == 0)
        act("Nobody anywhere near you.", TRUE, character, 0, 0, TO_CHAR);
    character->in_room = is_in;
}

/*=================================================================================
   mark_calculate_duration:
   The duration for mark is ranger level - 10 and then ranger_level / 2 *
   sec_per_mud_hour(60) * 4 / pulse_fast_update (12)
   ------------------------------Change Log---------------------------------------
   slyon: Sept 5, 2017 - Created
==================================================================================*/
int mark_calculate_duration(char_data* marker)
{
    int mark_duration = utils::get_prof_level(PROF_RANGER, *marker) - 10;
    mark_duration = mark_duration / 2 * (SECS_PER_MUD_HOUR * 4) / PULSE_FAST_UPDATE;
    return mark_duration;
}

/*=================================================================================
   mark_calculate_damage:
   Here we do a simple damage calculation on mark. It's damage is similar to
   magic missile. The mark ability isn't meant to do that much damage.
   ------------------------------Change Log---------------------------------------
   slyon: Sept 5, 2017 - Created
==================================================================================*/
int mark_calculate_damage(char_data* marker, char_data* victim)
{
    int ranger_level = utils::get_prof_level(PROF_RANGER, *marker);
    int str_factor = marker->tmpabilities.str / 5;
    int dex_factor = marker->tmpabilities.dex / 3;
    if (number(0, dex_factor % 5) > 0) {
        ++dex_factor;
    }

    int damage = ranger_level + str_factor + dex_factor;
    damage = number(1, damage / 6) + 12;
    return damage;
}

/*=================================================================================
   on_mark_hit:
   When a harads mark is successful and hits the target it will apply the mark
   affection and do small damage to them.
   ------------------------------Change Log---------------------------------------
   slyon: Sept 5, 2017 - Created
==================================================================================*/
void on_mark_hit(char_data* marker, char_data* victim)
{
    struct affected_type af;
    int damage_dealt = mark_calculate_damage(marker, victim);
    if (!utils::is_affected_by_spell(*victim, SKILL_MARK)) {
        af.type = SKILL_MARK;
        af.duration = mark_calculate_duration(marker);
        af.modifier = 1;
        af.location = APPLY_NONE;
        af.bitvector = 0;
        affect_join(victim, &af, FALSE, FALSE);
    }

    damage(marker, victim, damage_dealt, SKILL_MARK, 0);
}

/*=================================================================================
   on_mark_miss:
   If the harad archer misses his mark it moves the arrow they used to shoot
   to the room they are in.
   ------------------------------Change Log---------------------------------------
   slyon: Sept 5, 2017 - Created
==================================================================================*/
void on_mark_miss(char_data* marker, char_data* victim)
{
    damage(marker, victim, 0, SKILL_MARK, 0);
}

/*=================================================================================
   mark_calculate_success:
   Here we calculate the success chance of the Haradrim using the mark skill. Current
   forumla is ranger level + (ranger dex / 2) + (mark skill / 2)
   ------------------------------Change Log---------------------------------------
   slyon: Sept 6, 2017 - Created
   slyon: June 12, 2018 - Remove archery and make success based on touch attack
   slyon: April 21, 2020 - Changes success rate to a better formula
==================================================================================*/
int mark_calculate_success(char_data* marker, char_data* victim)
{
    int prob = GET_SKILL(marker, SKILL_MARK);
    prob -= get_real_dodge(victim);
    prob -= get_real_parry(victim) / 2;
    prob += get_real_OB(marker) / 2;
    prob += utils::get_prof_level(PROF_RANGER, *marker) / 2;
    prob += number(1, 100);
    prob -= 120;
    return prob;
}

/*=================================================================================
   can_ch_mark:
   Check to see if the archer is a Haradrim and they have the skill mark.
   ------------------------------Change Log---------------------------------------
   slyon: Sept 6, 2017 - Created
   slyon: June 12, 2018 - Redesigned check
==================================================================================*/
bool can_ch_mark(char_data* ch)
{
    using namespace utils;
    // Check to see if archer is a player haradrim
    if (GET_RACE(ch) != RACE_HARADRIM) {
        send_to_char("Unrecognized command.\r\n", ch);
        return false;
    }

    if (utils::get_skill(*ch, SKILL_MARK) == 0) {
        send_to_char("Learn how to mark first.\r\n", ch);
        return false;
    }

    if (is_shadow(*ch)) {
        send_to_char("Hmm, perhaps you've spent too much time in the mortal lands.\r\n", ch);
        return false;
    }

    if (IS_SET(world[ch->in_room].room_flags, PEACEROOM)) {
        send_to_char(
            "A peaceful feeling overwhelms you, and you cannot bring yourself to attack.\r\n", ch);
        return false;
    }

    if (ch->tmpabilities.mana < skills[SKILL_MARK].min_usesmana) {
        send_to_char("You can't summon enough energy to cast the spell.\n\r", ch);
        return false;
    }

    return true;
}

/*=================================================================================
   is_targ_valid_mark:
   Check to see if the mark target is valid and return NULL if not
   ------------------------------Change Log---------------------------------------
   slyon: June 12, 2018 - Created
==================================================================================*/
char_data* is_targ_valid_mark(char_data* marker, waiting_type* target)
{
    char_data* victim = NULL;

    if (target->targ1.type == TARGET_TEXT) {
        victim = get_char_room_vis(marker, target->targ1.ptr.text->text);
    } else if (target->targ1.type == TARGET_CHAR) {
        if (char_exists(target->targ1.ch_num)) {
            victim = target->targ1.ptr.ch;
        }
    }

    if (victim == NULL) {
        if (marker->specials.fighting) {
            victim = marker->specials.fighting;
        } else {
            send_to_char("Mark who?\r\n", marker);
            return NULL;
        }
    }

    if (marker->in_room != victim->in_room) {
        send_to_char("Your victim is no longer here.\r\n", marker);
        return NULL;
    }

    if (marker == victim) {
        send_to_char("But you have so much to live for!\r\n", marker);
        return NULL;
    }

    if (!CAN_SEE(marker, victim)) {
        send_to_char("Mark who?\r\n", marker);
        return NULL;
    }

    return victim;
}

/*=================================================================================
   mark_calculate_wait:
   Figure out how long the wait wheel should be for the Haradrim
   ------------------------------Change Log---------------------------------------
   slyon: June 12, 2018 - Created
==================================================================================*/
int mark_calculate_wait(const char_data* marker)
{
    const int base_beats = 12;
    const int min_beats = 6;

    int total_beats = base_beats - ((marker->points.ENE_regen / base_beats) - base_beats);
    total_beats = total_beats - (utils::get_prof_level(PROF_RANGER, *marker) / base_beats);
    total_beats = std::max(total_beats, min_beats);

    return total_beats;
}

/*=================================================================================
   do_mark:
   This is a race specific harad ability. It will mark the victim and they
   will leave a blood trail as they enter and leave rooms. It will also
   slow down movement regeneration. When the harad is shooting a marked target they
   will receive a bonus on damage and their success rate.
   Cure self and regeneration will reduce the duration of the mark ability.
   ------------------------------Change Log---------------------------------------
   slyon: Sept 5, 2017 - Created
==================================================================================*/
ACMD(do_mark)
{
    one_argument(argument, arg);

    if (subcmd == -1) {
        send_to_char("You lost your concentration and miss your target!\r\n", ch);
        ch->specials.ENERGY = std::min(ch->specials.ENERGY, 0); // reset swing timer after interruption.

        // Clean-up targets.
        wtl->targ1.cleanup();
        wtl->targ2.cleanup();
        return;
    }
    // TODO: Add can_ch_mark
    if (!can_ch_mark(ch))
        return;

    char_data* victim = is_targ_valid_mark(ch, wtl);

    game_rules::big_brother& bb_instance = game_rules::big_brother::instance();
    if (!bb_instance.is_target_valid(ch, victim)) {
        send_to_char("You feel the Gods looking down upon you, and protecting your target.  You "
                     "lower your bow.\r\n",
            ch);
        return;
    }

    if (utils::is_affected_by(*ch, AFF_SANCTUARY)) {
        appear(ch);
        send_to_char("You cast off your sanctuary!\r\n", ch);
        act("$n renounces $s sanctuary!", FALSE, ch, 0, 0, TO_ROOM);
    }

    switch (subcmd) {
    case 0: {
        if (victim == NULL) {
            return;
        }
        send_to_char("You start to draw powers from the land and gods of old.\r\n", ch);

        if (!utils::is_affected_by(*ch, AFF_HIDE)) {
            act("$n begins quietly muttering some strange, foreign powerful words.\n\r", FALSE, ch,
                0, 0, TO_ROOM);
        }

        wtl->targ1.cleanup();
        wtl->targ2.cleanup();

        int wait_delay = mark_calculate_wait(ch);
        WAIT_STATE_FULL(ch, wait_delay, CMD_MARK, 1, 30, 0, victim->abs_number, victim,
            AFF_WAITING | AFF_WAITWHEEL, TARGET_CHAR);
    } break;
    case 1: {
        if (victim == NULL) {
            return;
        }

        if (!CAN_SEE(ch, victim)) {
            send_to_char("Mark who?\r\n", ch);
            return;
        }

        if (ch->in_room != victim->in_room) {
            send_to_char("Your target is not here any longer.\r\n", ch);
            return;
        }

        // Reduce mana for using the skill.
        ch->tmpabilities.mana -= 20;

        // Did we land our touch attack???
        int target_number = mark_calculate_success(ch, victim);
        say_spell(ch, SKILL_MARK);
        if (target_number > 0) {
            on_mark_hit(ch, victim);
        } else {
            on_mark_miss(ch, victim);
        }

        wtl->targ1.cleanup();
        wtl->targ2.cleanup();
    } break;
    default:
        sprintf(buf2, "do_mark: illegal subcommand '%d'.\r\n", subcmd);
        mudlog(buf2, NRM, LEVEL_IMMORT, TRUE);
        abort_delay(ch);
        break;
        ;
    }
}

bool can_ch_blind(char_data* ch, int mana_cost)
{
    const room_data& room = world[ch->in_room];
    obj_data* dust = NULL;

    if (GET_RACE(ch) != RACE_HARADRIM) {
        send_to_char("Unrecognized Command.\r\n", ch);
        return false;
    }

    if (utils::is_shadow(*ch)) {
        send_to_char("Hmm, perphaps you've spent too much time in the shadow lands.\r\n", ch);
        return false;
    }

    if (utils::is_npc(*ch)) {
        char_data* receiver = ch->master ? ch->master : ch;
        send_to_char("Your follower lacks the knowledge to blind a target.\r\n", receiver);
        return false;
    }

    if (utils::is_set(room.room_flags, (long)PEACEROOM)) {
        send_to_char(
            "A peaceful feeling overwhelms you, and you cannot bring yourself to attack.\r\n", ch);
        return false;
    }

    if (utils::get_skill(*ch, SKILL_BLINDING) == 0) {
        send_to_char("Learn how to blind first.\r\n", ch);
        return false;
    }

    if (GET_MANA(ch) < mana_cost) {
        send_to_char("You can't summon enough energy to cast the spell.\n\r", ch);
        return false;
    }

    if (ch->carrying != NULL) {
        for (obj_data* item = ch->carrying; item; item = item->next_content) {
            if (item->item_number == real_object(static_cast<int>(Gather::Item::Dust))) {
                dust = item;
                break;
            }
        }

        if (dust == NULL) {
            send_to_char("You do not possess the appropriate materials to blind your target.\r\n",
                ch);
            return false;
        }
    }

    return true;
}

char_data* is_targ_blind_valid(char_data* ch, waiting_type* target)
{
    char_data* victim = NULL;

    if (target->targ1.type == TARGET_TEXT) {
        victim = get_char_room_vis(ch, target->targ1.ptr.text->text);
    } else if (target->targ1.type == TARGET_CHAR) {
        if (char_exists(target->targ1.ch_num)) {
            victim = target->targ1.ptr.ch;
        }
    }

    if (victim == NULL) {
        if (ch->specials.fighting) {
            victim = ch->specials.fighting;
        } else {
            send_to_char("Blind who?\r\n", ch);
            return NULL;
        }
    }

    if (ch->in_room != victim->in_room) {
        send_to_char("Your victim is no longer here.\r\n", ch);
        return NULL;
    }

    if (ch == victim) {
        send_to_char("Why would you blind yourself?!\r\n", ch);
        return NULL;
    }

    if (!CAN_SEE(ch, victim)) {
        send_to_char("Blind who?\r\n", ch);
        return NULL;
    }

    return victim;
}

int harad_skill_calculate_save(char_data* ch, char_data* victim, int skill_check)
{
    int random_roll = number(0, 99);

    // Attacker calculations
    int ch_skill = utils::get_skill(*ch, skill_check);
    int ch_ranger_level = utils::get_prof_level(PROF_RANGER, *ch);
    int ch_dex = ch->get_cur_dex() + 10;

    // Victim calculation saves
    int victim_ranger_level = utils::get_prof_level(PROF_RANGER, *victim);
    int victim_dex = victim->get_cur_dex();
    int victim_con = victim->get_cur_con();
    int victim_dodge = get_real_dodge(victim);

    // TODO: Needs to be tweaked / balanced
    int attack_dc = ch_skill + ch_ranger_level + ch_dex + random_roll;
    int victim_save = victim_dex + victim_dodge + victim_ranger_level + (victim_con / 4);

    return attack_dc - victim_save;
}

void on_dust_miss(char_data* ch, char_data* victim, int mana_cost)
{
    byte sex = ch->player.sex;

    GET_MANA(ch) -= (mana_cost >> 1);
    damage(ch, victim, 0, SKILL_BLINDING, 0);
}

void on_dust_hit(char_data* ch, char_data* victim, int mana_cost)
{
    int ranger_bonus = 15; // Setting this as a default for now.
    struct affected_type af;
    int affects_last = 22 - ranger_bonus;

    af.type = SKILL_BLINDING;
    af.duration = affects_last;
    af.modifier = 0;
    af.location = APPLY_NONE;
    af.bitvector = AFF_BLIND | AFF_HAZE;

    GET_MANA(ch) -= mana_cost;

    // Skip blindness if it kills
    if (damage(ch, victim, 1, SKILL_BLINDING, 0) < 1) {
        affect_to_char(victim, &af);
    }
}

/*=================================================================================
  do_blinding:
  This is a race specific harad ability. It will blind the target provided the harad
  has GATHER_DUST in his inventory and the target fails their dex and con save.
  ------------------------------Change Log---------------------------------------
  slyon: Sept 27, 2017 - Created
==================================================================================*/
ACMD(do_blinding)
{
    const int mana_cost = skills[SKILL_BLINDING].min_usesmana;

    one_argument(argument, arg);

    if (subcmd == -1) {
        // FIXME: This triggers on cancel, is this right?
        send_to_char("Your attempt to blind your target have been foiled.\r\n", ch);
        ch->specials.ENERGY = std::min(ch->specials.ENERGY, 0);
        wtl->targ1.cleanup();
        wtl->targ2.cleanup();
        return;
    }

    if (!can_ch_blind(ch, mana_cost)) {
        return;
    }

    char_data* victim = is_targ_blind_valid(ch, wtl);

    if (victim == NULL) {
        return;
    }

    game_rules::big_brother& bb_instance = game_rules::big_brother::instance();
    if (!bb_instance.is_target_valid(ch, victim)) {
        send_to_char("You feel the Gods looking down upon you, and protecting your target.\r\n",
            ch);
        return;
    }

    if (utils::is_affected_by(*ch, AFF_SANCTUARY)) {
        appear(ch);
        send_to_char("You cast off your sanctuary!\r\n", ch);
        act("$n renounces $s sanctuary!", FALSE, ch, 0, 0, TO_ROOM);
    }

    if (affected_by_spell(victim, AFF_BLIND)) {
        act("$N is already blind.\r\n", FALSE, ch, 0, victim, TO_CHAR);
        return;
    }

    switch (subcmd) {
    case 0: {
        if (victim == NULL) {
            return;
        }

        wtl->targ1.cleanup();
        wtl->targ2.cleanup();

        WAIT_STATE_FULL(ch, skills[SKILL_BLINDING].beats, CMD_BLINDING, 1, 30, 0,
            victim->abs_number, victim, AFF_WAITING | AFF_WAITWHEEL, TARGET_CHAR);
    } break;

    case 1: {
        if (victim == NULL) {
            return;
        }

        if (!CAN_SEE(ch, victim)) {
            send_to_char("Blind who?\r\n", ch);
            return;
        }

        if (ch->in_room != victim->in_room) {
            send_to_char("Your victim is no longer here.\r\n", ch);
            return;
        }

        obj_data* dust = NULL;
        for (obj_data* item = ch->carrying; item; item = item->next_content) {
            if (item->item_number == real_object(static_cast<int>(Gather::Item::Dust))) {
                dust = item;
                break;
            }
        }

        if (dust == NULL) {
            send_to_char("You lack the material to use this skill.\r\n", ch);
            return;
        }

        int percent_hit = harad_skill_calculate_save(ch, victim, SKILL_BLINDING);
        say_spell(ch, SKILL_BLINDING);
        if (percent_hit < 0) {
            on_dust_miss(ch, victim, mana_cost);
        } else {
            on_dust_hit(ch, victim, mana_cost);
        }

        extract_obj(dust);

        ch->specials.ENERGY = std::min(ch->specials.ENERGY, 0);
        wtl->targ1.cleanup();
        wtl->targ2.cleanup();
    } break;

    default:
        sprintf(buf2, "do_blinding: illegal subcommand '%d'.\r\n", subcmd);
        mudlog(buf2, NRM, LEVEL_IMMORT, TRUE);
        abort_delay(ch);
        break;
        ;
    }
}

bool can_harad_use_skill(char_data* ch, int mana_cost, int move_cost, int skill)
{
    const room_data& room = world[ch->in_room];
    if (GET_RACE(ch) != RACE_HARADRIM) {
        send_to_char("Unrecognized command.\r\n", ch);
        return false;
    }

    if (utils::is_shadow(*ch)) {
        send_to_char("Hmm, perphaps you've spent too much time in the shadow lands.\r\n", ch);
        return false;
    }

    if (utils::is_npc(*ch)) {
        char_data* receiver = ch->master ? ch->master : ch;
        send_to_char("Your follower lacks the knowledge to bend time.\r\n", receiver);
        return false;
    }

    if (utils::is_set(room.room_flags, (long)PEACEROOM)) {
        send_to_char(
            "A peaceful feeling overwhelms you, and you cannot bring yourself to attack.\r\n", ch);
        return false;
    }

    if (utils::get_skill(*ch, skill) == 0) {
        send_to_char("Learn how to bend time first.\r\n", ch);
        return false;
    }

    if (GET_MANA(ch) < mana_cost) {
        send_to_char("You can't summon enough energy to use this skill.\r\n", ch);
        return false;
    }

    if (GET_MOVE(ch) < move_cost) {
        send_to_char("You are too tired for this right now.\n\r", ch);
        return false;
    }

    return true;
}

bool check_skill_success(char_data* ch, int skill)
{
    int skill_percent = utils::get_skill(*ch, skill);
    int roll = number(1, 99);
    if (roll > skill_percent)
        return false;
    else
        return true;
}

void on_bend_success(char_data* ch, int mana_cost, int move_cost)
{
    GET_MANA(ch) -= mana_cost;
    GET_MOVE(ch) -= move_cost;

    struct affected_type af;

    af.type = SKILL_BEND_TIME;
    af.duration = 15;
    af.modifier = 20;
    af.location = APPLY_BEND;
    af.bitvector = 0;
    affect_to_char(ch, &af);
    send_to_char("Time starts to move slower, and energy from the land fills your body.\r\n", ch);
    act("$n starts to blur in and out of existance.\r\n", FALSE, ch, 0, 0, TO_ROOM);
}

/*=================================================================================
  do_bendtime:
  This is a race specific harad ability. It will allow the Haradrim to double it's
  current energy and increase their temporary ob by 20, for a short duration.
  The cost of this skill is the players max mana and 100 moves.
  ------------------------------Change Log---------------------------------------
  slyon: Oct 04, 2018 - Created
==================================================================================*/
ACMD(do_bendtime)
{
    const int mana_cost = ch->abilities.mana;
    const int move_cost = 100;

    one_argument(argument, arg);

    if (subcmd == -1) {
        send_to_char("You could not concentrate anymore!\r\n", ch);
        wtl->targ1.cleanup();
        wtl->targ2.cleanup();
        ch->specials.ENERGY = std::min(ch->specials.ENERGY, 0);
        return;
    }

    if (!can_harad_use_skill(ch, mana_cost, move_cost, SKILL_BEND_TIME)) {
        return;
    }

    if (affected_by_spell(ch, SKILL_BEND_TIME)) {
        send_to_char("You are already affected by bend time.\r\n", ch);
        return;
    }

    switch (subcmd) {
    case 0: {
        wtl->targ1.cleanup();
        wtl->targ2.cleanup();

        send_to_char("You start to draw powers from the land and gods of old.\r\n", ch);
        act("$n begins quietly muttering some strange, foreign powerful words.\n\r", FALSE, ch, 0,
            0, TO_ROOM);

        WAIT_STATE_FULL(ch, skills[SKILL_BEND_TIME].beats, CMD_BENDTIME, 1, 30, 0, 0, 0,
            AFF_WAITING | AFF_WAITWHEEL, TARGET_NONE);
    } break;
    case 1: {
        say_spell(ch, SKILL_BEND_TIME);
        if (check_skill_success(ch, SKILL_BEND_TIME))
            on_bend_success(ch, mana_cost, move_cost);
        else {
            GET_MANA(ch) -= (mana_cost / 2);
            GET_MOVE(ch) -= (move_cost / 2);
            send_to_char("You lost your concentration!\n\r", ch);
        }
    } break;

    default: {
        sprintf(buf2, "do_bendtime: illegal subcommand '%d'.\r\n", subcmd);
        mudlog(buf2, NRM, LEVEL_IMMORT, TRUE);
        abort_delay(ch);
    } break;
    }
}

void on_windblast_hit(char_data* ch)
{
    int i, attempt, res, die, move_cost = 0;
    struct char_data* tmpch;
    stop_riding(ch);
    if (GET_TACTICS(ch) == TACTICS_BERSERK) {
        send_to_char("You stand your ground against the blast of thunderous force!", ch);
        return;
    }

    if (GET_POS(ch) < POSITION_FIGHTING)

        return;

    for (i = 0; i < 6; i++) {
        attempt = number(0, NUM_OF_DIRS - 1);
        res = CAN_GO(ch, attempt) && !IS_SET(EXIT(ch, attempt)->exit_info, EX_NOFLEE) && !IS_SET(EXIT(ch, attempt)->exit_info, EX_NOWALK);

        if (res) {
            res = !IS_SET(world[EXIT(ch, attempt)->to_room].room_flags, DEATH);
            if (IS_NPC(ch)) {
                res = res && ((!IS_SET(ch->specials2.act, MOB_STAY_ZONE) || (world[EXIT(ch, attempt)->to_room].zone == world[ch->in_room].zone)) && (!IS_SET(ch->specials2.act, MOB_STAY_TYPE) || (world[EXIT(ch, attempt)->to_room].sector_type == world[ch->in_room].sector_type)));
            }
        }

        if (res) {
            die = check_simple_move(ch, attempt, &move_cost, SCMD_FLEE);

            if (!die) {
                if (ch->specials.fighting) {
                    for (tmpch = world[ch->in_room].people; tmpch; tmpch = tmpch->next_in_room) {
                        if (tmpch->specials.fighting == ch) {
                            stop_fighting(tmpch);
                        }
                    }
                    stop_fighting(ch);
                }

                if (IS_AFFECTED(ch, AFF_HUNT)) {
                    REMOVE_BIT(ch->specials.affected_by, AFF_HUNT);
                }

                send_to_char("A wave of thunderous force sweeps you out!", ch);
                act("$n gets sweep out from the wave of thunderous force!", FALSE, ch, 0, 0,
                    TO_ROOM);

                do_move(ch, dirs[attempt], 0, attempt + 1, SCMD_FLEE);
                return;
            }
        }
    }
    send_to_char("A wave of thunderous force sweeps you off your feet!", ch);
    act("$n gets sweep off their feet from a thunderous wave of force!", TRUE, ch, 0, 0, TO_ROOM);

    return;
}

void on_windblast_success(char_data* ch, int mana_cost, int move_cost)
{
    int dam_value, tmp, power_level;
    struct char_data *tmpch, *tmpch_next;
    room_data* cur_room;

    GET_MANA(ch) -= mana_cost;
    GET_MOVE(ch) -= move_cost;

    if (ch->in_room == NOWHERE)
        return;

    power_level = utils::get_prof_level(PROF_RANGER, *ch) / 2;

    cur_room = &world[ch->in_room];

    dam_value = number(1, 30) + power_level;

    game_rules::big_brother& bb_instance = game_rules::big_brother::instance();

    send_to_char("Vile black wind eminates from you, slamming into all!\r\n", ch);

    for (tmpch = world[ch->in_room].people; tmpch; tmpch = tmpch_next) {
        tmpch_next = tmpch->next_in_room;
        if (tmpch != ch) {
            if (!bb_instance.is_target_valid(ch, tmpch)) {
                send_to_char(
                    "You feel the Gods looking down upon you, and protecting your target.\r\n", ch);
                continue;
            }

            int saved = harad_skill_calculate_save(ch, tmpch, 0);
            if (saved < 0) {
                dam_value >> 1;
                damage(ch, tmpch, dam_value, SKILL_WINDBLAST, 0);
            } else {
                on_windblast_hit(tmpch);
                damage(ch, tmpch, dam_value, SKILL_WINDBLAST, 0);
            }
        }
    }
}

/*=================================================================================
  do_windblast:
  This is a race specific harad ability. Each creature in the room must make a
  saving throw. Any creature that fails their saving throw takes damage and are
  pushed out of the room, random directions. On a successful save, they take reduced
  damage.
  ------------------------------Change Log---------------------------------------
  slyon: Feb 17, 2019 - Created
==================================================================================*/
ACMD(do_windblast)
{
    const int mana_cost = skills[SKILL_BLINDING].min_usesmana;
    const int move_cost = 40;

    one_argument(argument, arg);

    if (subcmd == -1) {
        send_to_char("You could not concentrate anymore!\r\n", ch);
        wtl->targ1.cleanup();
        wtl->targ2.cleanup();
        ch->specials.ENERGY = std::min(ch->specials.ENERGY, 0);
        return;
    }

    if (!can_harad_use_skill(ch, mana_cost, move_cost, SKILL_WINDBLAST)) {
        return;
    }

    switch (subcmd) {
    case 0: {
        wtl->targ1.cleanup();
        wtl->targ2.cleanup();

        send_to_char("You start to draw powers from the land and gods of old.\r\n", ch);
        act("$n begins quietly muttering some strange, foreign powerful words.\r\n", FALSE, ch, 0,
            0, TO_ROOM);
        WAIT_STATE_FULL(ch, skills[SKILL_WINDBLAST].beats, CMD_WINDBLAST, 1, 30, 0, 0, 0,
            AFF_WAITING | AFF_WAITWHEEL, TARGET_NONE);
    } break;
    case 1: {
        say_spell(ch, SKILL_WINDBLAST);
        if (check_skill_success(ch, SKILL_WINDBLAST)) {
            on_windblast_success(ch, mana_cost, move_cost);
        } else {
            GET_MANA(ch) -= (mana_cost / 2);
            GET_MOVE(ch) -= (move_cost / 2);
            send_to_char("You lost your concentration!\r\n", ch);
        }
    } break;
    default: {
        sprintf(buf2, "do_windblast: illegal subcommand '%d'.\r\n", subcmd);
        mudlog(buf2, NRM, LEVEL_IMMORT, TRUE);
        abort_delay(ch);
    } break;
    }
}
