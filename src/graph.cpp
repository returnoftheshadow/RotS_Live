/* ************************************************************************
 *   File: graph.c                                       Part of CircleMUD *
 *  Usage: various graph algorithms                                        *
 *                                                                         *
 *  All rights reserved.  See license.doc for complete information.        *
 *                                                                         *
 *  Copyright (C) 1993 by the Trustees of the Johns Hopkins University     *
 *  CircleMUD is based on DikuMUD, Copyright (C) 1990, 1991.               *
 ************************************************************************ */

#define TRACK_THROUGH_DOORS

/*
 * You can define or not define TRACK_THOUGH_DOORS, above, depending on
 * whether or not you want track to find paths which lead through closed
 * or hidden doors.
 */

#include "platdef.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "comm.h"
#include "db.h"
#include "handler.h"
#include "interpre.h"
#include "spells.h"
#include "structs.h"
#include "utils.h"

/* Externals */
extern int top_of_world;
extern char* dirs[];
extern struct room_data world;
extern struct time_info_data time_info;
extern struct char_data* mob_proto; /* prototypes for mobs*/
extern struct char_data* waiting_list;
extern struct char_data* character_list;
extern struct skill_data skills[];

ACMD(do_say);
ACMD(do_move);

struct bfs_queue_struct {
    int room;
    char dir;
    struct bfs_queue_struct* next;
};

static struct room_data *queue_head = 0, *queue_tail = 0;

/* Utility macros */
#define MARK(room) (SET_BIT((room)->room_flags, BFS_MARK))
#define UNMARK(room) (REMOVE_BIT((room)->room_flags, BFS_MARK))
#define IS_MARKED(room) (IS_SET((room)->room_flags, BFS_MARK | NO_MOB | DEATH))
#define TOROOM(x, y) ((x)->dir_option[(y)]->to_room)

#ifdef TRACK_THROUGH_DOORS
#define IS_CLOSED(x, y) (IS_SET((x)->dir_option[(y)]->exit_info, EX_LOCKED | EX_ISHIDDEN) && (IS_SET((x)->dir_option[(y)]->exit_info, EX_CLOSED) || !IS_SET((x)->dir_option[(y)]->exit_info, EX_ISDOOR)))
#else
#define IS_CLOSED(x, y) (IS_SET((x)->dir_option[(y)]->exit_info, EX_CLOSED))
#endif

#define VALID_EDGE(x, y) ((x)->dir_option[(y)] && (TOROOM(x, y) != NOWHERE) && (!IS_CLOSED(x, y)) && (!IS_MARKED(&world[TOROOM(x, y)])))

void bfs_enqueue(room_data* room, char dir)
{

    room_data* curr;
    curr = room;
    curr->bfs_next = 0;
    curr->bfs_dir = dir;
    if (queue_tail) {
        queue_tail->bfs_next = curr;
        queue_tail = curr;
    } else
        queue_head = queue_tail = curr;
}

void bfs_dequeue(void)
{

    room_data* curr;
    curr = queue_head;
    if (!(queue_head = queue_head->bfs_next))
        queue_tail = 0;
}

void bfs_clear_queue(void)
{
    while (queue_head)
        bfs_dequeue();
}

/*
 * find_first_step: given a source room and a target room, find the first
 * step on the shortest path from the source to the target.
 * Intended usage: in mobile_activity, give a mob a dir to go if they're
 * tracking another mob or a PC.  Or, a 'track' skill for PCs.
 */

int find_first_step(int src, int target)
{

    int curr_room, curr_dir;
    room_data *src_room, *target_room;

    if (src < 0 || src > top_of_world || target < 0 || target > top_of_world) {
        log("Illegal value passed to find_first_step (graph.c)");
        return BFS_ERROR;
    }

    if (src == target)
        return BFS_ALREADY_THERE;

    /* clear marks first */
    for (curr_room = 0; curr_room <= top_of_world; curr_room++)
        UNMARK(&world[curr_room]);

    src_room = &world[src];
    target_room = &world[target];
    MARK(src_room);

    /*
     * first, enqueue the first steps, saving which
     * direction we're going.
     */
    for (curr_dir = 0; curr_dir < NUM_OF_DIRS; curr_dir++)
        if (VALID_EDGE(src_room, curr_dir)) {
            MARK(&world[(int)TOROOM(src_room, curr_dir)]);
            bfs_enqueue(&world[(int)TOROOM(src_room, curr_dir)], curr_dir);
        }

    /* now, do the classic BFS. */
    while (queue_head) {
        if (queue_head == target_room) {
            curr_dir = queue_head->bfs_dir;
            bfs_clear_queue();
            return curr_dir;
        } else {
            for (curr_dir = 0; curr_dir < NUM_OF_DIRS; curr_dir++)
                if (VALID_EDGE(queue_head, curr_dir)) {
                    MARK(&world[TOROOM(queue_head, curr_dir)]);
                    bfs_enqueue(&world[TOROOM(queue_head, curr_dir)],
                        queue_head->bfs_dir);
                }
            bfs_dequeue();
        }
    }
    return BFS_NO_PATH;
}

/*
 *  Functions and Commands which use the above fns
 */

ACMD(do_wiztrack)
{

    struct char_data* vict;
    int dir;

    one_argument(argument, arg);
    if (!*arg) {
        send_to_char("Whom are you trying to track?\r\n", ch);
        return;
    }

    if (!(vict = get_char_vis(ch, arg))) {
        send_to_char("No-one around by that name.\r\n", ch);
        return;
    }

    dir = find_first_step(ch->in_room, vict->in_room);
    switch (dir) {
    case BFS_ERROR:
        send_to_char("Hmm.. something seems to be wrong.\r\n", ch);
        break;
    case BFS_ALREADY_THERE:
        send_to_char("You're already in the same room!!\r\n", ch);
        break;
    case BFS_NO_PATH:
        snprintf(buf, sizeof(buf), "You can't sense a trail to %s from here.\r\n",
            HMHR(vict));
        send_to_char(buf, ch);
        break;
    default:

        /*
         * if you want to make this into a skill instead of a command,
         * the next few lines make it give you a random direction if you
         * fail the random skill roll.
         */

#ifdef TRACK_IS_SKILL
    {
        int num;
        num = number(0, 101);
        if (SKILL(ch, (SKILL_TRACK + 10) * 100 / 10) < num)
            do {
                dir = number(0, NUM_OF_DIRS - 1);
            } while (!CAN_GO(ch, dir));
    }
#endif
        snprintf(buf, sizeof(buf), "You sense a trail %s from here!\n\r", dirs[dir]);
        send_to_char(buf, ch);
        break;
    }
}

void hunt_victim(struct char_data* ch)
{

    struct char_data* tmp;
    int dir;
    byte found;

    if (!ch || !ch->specials.hunting)
        return;
    /* make sure the char still exists */
    for (found = 0, tmp = character_list; tmp && !found; tmp = tmp->next)
        if (ch->specials.hunting == tmp)
            found = 1;

    if (!found) {
        do_say(ch, "Damn!  My prey is gone!!", 0, 0, 0);
        ch->specials.hunting = 0;
        return;
    }

    dir = find_first_step(ch->in_room, ch->specials.hunting->in_room);
    if (dir < 0) {
        snprintf(buf, sizeof(buf), "Damn!  Lost %s!", HMHR(ch->specials.hunting));
        do_say(ch, buf, 0, 0, 0);
        ch->specials.hunting = 0;
        return;
    } else {
        do_move(ch, "", 0, dir + 1, 0);
        if (ch->in_room == ch->specials.hunting->in_room)
            hit(ch, ch->specials.hunting, TYPE_UNDEFINED);
        return;
    }
}

const char* track_desc(int track_age)
{
    switch (track_age) {
    case 0:
        return "fresh";
    case 1:
        return "very clear";
    case 2:
        return "clear";
    case 3:
    case 4:
        return "quite clear";
    case 5:
    case 6:
    case 7:
        return "distinctive";
    case 8:
    case 9:
    case 10:
        return "weathering";
    case 11:
    case 12:
    case 13:
    case 14:
    case 15:
        return "weathered";
    case 16:
    case 17:
    case 18:
    case 19:
    case 20:
        return "very poor";
    case 21:
    case 22:
    case 23:
    case 24:
        return "barely recognisable";
    default:
        return "unclear";
    }
}

const char* water_track_desc(int track_age)
{
    switch (track_age) {
    case 0:
        return "very";
    case 1:
        return "quite";
    case 2:
        return "a little";
    default:
        return "hardly";
    }
}

int show_tracks(char_data* ch, char* name, int mode)
{
    /*
     * returns the number of tracks shown
     * mode: 1 - full mode, all tracks and easier to find
     *       2 - fast mode, fresh tracks only and harder to notice
     */

    int tmp, count, ch_num, chance_factor, tr_time, shall_show;
    room_data* ch_room;
    if (ch->in_room == NOWHERE)
        return 0;
    ch_room = &world[ch->in_room];
    chance_factor = 0;

    if (ch_room->sector_type == SECT_CITY)
        chance_factor = -100;
    if (GET_LEVEL(ch) >= LEVEL_IMMORT)
        chance_factor = 200;

    if (mode == 1)
        chance_factor += 20;
    if (mode == 2)
        chance_factor -= 20;

    buf[0] = 0;
    for (tmp = 0, count = 0; tmp < NUM_OF_TRACKS; tmp++) {
        ch_num = ch_room->room_track[tmp].char_number;
        tr_time = ch_room->room_track[tmp].condition;
        shall_show = (ch_num != 0) && ((mode == 1) || (tr_time < 3)) && (GET_SKILL(ch, SKILL_TRACK) + chance_factor - tr_time * 2 > number(0, 99));
        if (shall_show && name && *name) {
            if (ch_num > 0)
                shall_show = isname(name, mob_proto[ch_num].player.name, 1);
            else
                shall_show = !str_cmp(name, pc_race_keywords[-ch_num]);
        }
        if (shall_show) {
            count++;
            if (IS_WATER(ch->in_room))
                std::snprintf(buf, sizeof(buf), "The water looks %s disturbed to the %s.\n\r",
                    water_track_desc(tr_time), dirs[ch_room->room_track[tmp].data & 7]);
            else {
                std::size_t length = strlen(buf);
                std::snprintf(buf + length, sizeof(buf) - length,
                    "The tracks of %s lead %s.  Their condition is %s\n\r",
                    (ch_num >= 0) ? mob_proto[ch_num].player.short_descr : pc_star_types[-ch_num], dirs[ch_room->room_track[tmp].data & 7], track_desc(tr_time));
            }
        }
    }
    if (count != 0) {
        if (mode == 1)
            send_to_char("You find some tracks of what went past here.\r\n", ch);
        send_to_char(buf, ch);
    }
    return count;
}

int show_blood_trail(struct char_data* ch, char* name, int mode)
{
    int tmp, count, chance_factor, ch_num, tr_time, shall_show;
    room_data* ch_room;
    if (ch->in_room == NOWHERE) {
        return 0;
    }

    ch_room = &world[ch->in_room];
    chance_factor = 0;

    if (ch_room->sector_type == SECT_CITY) {
        chance_factor = -100;
    }

    if (mode == 1) {
        chance_factor += 20;
    }

    if (mode == 2) {
        chance_factor -= 20;
    }

    buf[0] = 0;

    for (tmp = 0, count = 0; tmp < NUM_OF_BLOOD_TRAILS; tmp++) {
        ch_num = ch_room->bleed_track[tmp].char_number;
        tr_time = ch_room->bleed_track[tmp].condition;
        shall_show = (ch_num != 0) && ((mode == 1) || (tr_time < 3)) && (GET_SKILL(ch, SKILL_MARK) + chance_factor - tr_time * 2 > number(0, 99));

        if (shall_show && name && *name) {
            if (ch_num > 0) {
                shall_show = isname(name, mob_proto[ch_num].player.name, 1);
            } else {
                shall_show = !str_cmp(name, pc_race_keywords[-ch_num]);
            }
        }

        if (shall_show) {
            count++;
            if (IS_WATER(ch->in_room)) {
                std::snprintf(buf, sizeof(buf), "The water looks %s disturbed to the %s.\n\r",
                    water_track_desc(tr_time), dirs[ch_room->bleed_track[tmp].data & 7]);
            } else {
                std::size_t length = strlen(buf);
                std::snprintf(buf + length, sizeof(buf) - length,
                    "A blood trail leading %s is %s.\n\r", dirs[ch_room->bleed_track[tmp].data & 7],
                    track_desc(tr_time));
            }
        }
    }

    if (count != 0) {
        if (mode == 1) {
            send_to_char("You find some blood trails here.\r\n", ch);
        }
        send_to_char(buf, ch);
    }
    return count;
}
