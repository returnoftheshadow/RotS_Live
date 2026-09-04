/* ************************************************************************
 *   File: mobact.c                                      Part of CircleMUD *
 *  Usage: Functions for generating intelligent (?) behavior in mobiles    *
 *                                                                         *
 *  All rights reserved.  See license.doc for complete information.        *
 *                                                                         *
 *  Copyright (C) 1993 by the Trustees of the Johns Hopkins University     *
 *  CircleMUD is based on DikuMUD, Copyright (C) 1990, 1991.               *
 ************************************************************************ */

#include "platdef.h"
#include <stdio.h>
#include <stdlib.h>

#include "char_utils.h"
#include "comm.h"
#include "db.h"
#include "handler.h"
#include "interpre.h"
#include "structs.h"
#include "utils.h"

/* external structs */
extern struct char_data* character_list;
extern struct index_data* mob_index;
extern struct room_data world;
extern int top_of_world;

ACMD(do_move);
ACMD(do_get);
ACMD(do_rescue);
ACMD(do_assist);
ACMD(do_stand);
ACMD(do_hit);
ACMD(do_say);
ACMD(do_flee);
ACMD(do_wear);

SPECIAL(intelligent);

void one_mobile_activity(struct char_data*);
void* virt_program_number(int number);
int find_first_step(int, int);

void enforce_position(struct char_data*, int);

void mobile_activity(void)
{
    struct char_data* ch;
    SPECIAL(*tmpfunc);

    for (ch = character_list; ch; ch = ch->next)
        if (!number(0, 3)) {
            if (IS_NPC(ch))
                one_mobile_activity(ch);
            else {
                tmpfunc = (SPECIAL(*))
                    virt_program_number(ch->specials.store_prog_number);
                if (tmpfunc)
                    tmpfunc(ch, ch, 0, "", SPECIAL_SELF, 0);
            }
        }
}

void one_mobile_activity(char_data* ch)
{
    int door, found, max, tmp, is_passive;
    struct char_data *tmp_ch, *tmpch, *vict;
    struct obj_data *obj, *best_obj, *inside, *next_obj;
    struct waiting_type wtl;
    struct memory_rec* names;
    struct follow_type* tmpfol;
    SPECIAL(*tmpfunc);

    extern int no_specials;

    wtl.cmd = wtl.subcmd = wtl.priority = 0;

    if (!ch)
        return;
    if (!IS_NPC(ch))
        return;

    if ((ch->in_room < 0) || (ch->in_room > top_of_world)) {
        sprintf(buf, "mobile_act called for %s in %d.",
            GET_NAME(ch), ch->in_room);
        mudlog(buf, NRM, LEVEL_IMPL, FALSE);
        return;
    }

    is_passive = 0;
    if (MOB_FLAGGED(ch, MOB_PET) && !utils::is_guardian(*ch) && ch->master && char_exists(ch->master_number)) {
        if (ch->in_room == ch->master->in_room) {
            is_passive = 1;
        }
    }

    // handle interrupts
    if (ch->specials.fighting && ch->interrupt_count > 0 && ch->interrupt_time > 0) {
        ch->interrupt_time = ch->interrupt_time - 1;
        if (ch->interrupt_time == 0) {
            ch->interrupt_count = ch->interrupt_count - 1;
            if (ch->interrupt_count > 0) {
                ch->interrupt_time = 10;
            }
        }
    }

    if (IS_MOB(ch) && ch->delay.wait_value <= 1 && !is_passive) {
        /* Tamed mobs can stay in non-default positions... */
        if (!IS_AFFECTED(ch, AFF_CHARM)) {
            enforce_position(ch, ch->specials.default_pos);
        }

        /* Examine call for special procedure */
        if (IS_SET(ch->specials2.act, MOB_SPEC) && !no_specials) {
            sprintf(buf, "%s - find prog: %d, func:%d", GET_NAME(ch), ch->specials2.act, mob_index[ch->nr].func);
            mudlog_aliased_mob(buf, ch, "progdebug");
            if (!mob_index[ch->nr].func && ch->specials.store_prog_number) {
                tmpfunc = (SPECIAL(*))virt_program_number(ch->specials.store_prog_number);
                if (tmpfunc) {
                    if (tmpfunc(ch, ch, 0, "", SPECIAL_SELF, 0)) {
                        return;
                    }
                }
            } else {
                if (mob_index[ch->nr].func) {
                    if ((*mob_index[ch->nr].func)(ch, ch, 0, "", SPECIAL_SELF, 0)) {
                        return;
                    }
                }
            }

        } else {
            if (ch->specials.union1.prog_number) {
                if (intelligent(ch, ch, 0, "", SPECIAL_SELF, 0)) {
                    return;
                }
            }
        }

        // STOP here if mob is now busy (I believe this occurs when methods above return FALSE when they should be TRUE )
        // BECAUSE: other things are checking subcmd and delay time BUT subcmd gets set to 0 ABOVE!??
        if (ch->delay.wait_value && ch->delay.cmd) {
            return;
        }

        if (!ch->specials.fighting) {
            ch->interrupt_count = 0;
            ch->interrupt_time = 0;
        }

        /* mob - helper */
        const room_data& room = world[ch->in_room];
        if (AWAKE(ch) && IS_SET(ch->specials2.act, MOB_HELPER) && !ch->specials.fighting) {
            for (char_data* ally = room.people; ally; ally = ally->next_in_room) {
                // Don't assist allies that you are aggressive to.
                if (IS_AGGR_TO(ch, ally))
                    continue;

                // Never assist guardians or orc followers.
                if (MOB_FLAGGED(ally, MOB_GUARDIAN) || (MOB_FLAGGED(ally, MOB_ORC_FRIEND) && MOB_FLAGGED(ally, MOB_PET)))
                    continue;

                char_data* enemy = ally->specials.fighting;
                if (enemy && IS_NPC(ally) && CAN_SEE(ch, ally)) {
                    bool assist = false;
                    if (IS_AGGR_TO(ch, enemy)) {
                        // Always assist against targets that you are aggressive to.
                        assist = true;
                    } else {
                        // Assist if the ally has the same alignment as the helper, and the ally
                        // is not aggressive to its enemy.
                        if (GET_ALIGNMENT(ch) * GET_ALIGNMENT(ally) > 0 && !IS_AGGR_TO(ally, enemy)) {
                            assist = true;
                        }
                    }

                    if (assist) {
                        if (GET_INT_BASE(ch) >= 7) {
                            do_say(ch, "I must protect my friend!", 0, 0, 0);
                        }
                        wtl.targ1.type = TARGET_CHAR;
                        wtl.targ1.ptr.ch = ally;
                        wtl.targ1.ch_num = ally->abs_number;
                        wtl.cmd = CMD_ASSIST;
                        do_assist(ch, "", &wtl, 0, 0);
                        break;
                    }
                }
            }
        }

        /* bodyguard - follower */
        if (AWAKE(ch) && IS_SET(ch->specials2.act, MOB_BODYGUARD) && ch->master && (ch->master->in_room == ch->in_room)) {

            if (GET_POS(ch) < POSITION_FIGHTING)
                do_stand(ch, "", 0, 0, 0);

            tmp_ch = (ch->master)->specials.fighting;

            if (tmp_ch && (tmp_ch->specials.fighting == ch->master)) {
                sscanf(ch->master->player.name, "%s", buf);
                //	     printf("trying to rescue %s.\n",buf);
                wtl.targ1.type = TARGET_CHAR;
                wtl.targ1.ptr.ch = ch->master;
                wtl.targ1.ch_num = ch->master->abs_number;
                wtl.cmd = CMD_RESCUE;
                do_rescue(ch, buf, &wtl, 0, 0);
            }
            if (tmp_ch && !(ch->specials.fighting)) {
                sscanf(tmp_ch->player.name, "%s", buf);
                wtl.targ1.type = TARGET_CHAR;
                wtl.targ1.ptr.ch = tmp_ch;
                wtl.targ1.ch_num = tmp_ch->abs_number;
                wtl.cmd = CMD_HIT;
                do_hit(ch, buf, &wtl, 0, 0);
            }
        }

        /* bodyguard - master */
        if (AWAKE(ch) && IS_SET(ch->specials2.act, MOB_BODYGUARD) && ch->followers) {
            if (GET_POS(ch) < POSITION_FIGHTING)
                do_stand(ch, "", 0, 0, 0);

            for (tmpfol = ch->followers; tmpfol; tmpfol = tmpfol->next) {
                tmp_ch = (tmpfol->follower)->specials.fighting;
                if (tmp_ch && (tmp_ch->specials.fighting == tmpfol->follower)) {
                    sscanf(tmpfol->follower->player.name, "%s", buf);
                    wtl.targ1.type = TARGET_CHAR;
                    wtl.targ1.ptr.ch = tmpfol->follower;
                    wtl.targ1.ch_num = tmpfol->follower->abs_number;
                    wtl.cmd = CMD_RESCUE;
                    do_rescue(ch, buf, &wtl, 0, 0);
                }
            }
        }

        /* Assistant mob.  They always assist their master if their master is fighting
         * and they are not. */
        if (AWAKE(ch) && IS_SET(ch->specials2.act, MOB_ASSISTANT) && ch->master) {
            if (ch->master->in_room == ch->in_room && ch->master->specials.fighting && ch->specials.fighting == NULL) {
                if (CAN_SEE(ch, ch->master)) {
                    if (GET_POS(ch) < POSITION_FIGHTING) {
                        do_stand(ch, "", NULL, 0, 0);
                    }

                    wtl.targ1.type = TARGET_CHAR;
                    wtl.targ1.ptr.ch = ch->master;
                    wtl.targ1.ch_num = ch->master->abs_number;
                    wtl.cmd = CMD_ASSIST;
                    do_assist(ch, "", &wtl, 0, 0);
                }
            }
        }

        /* Guardians, special case */
        if (utils::is_guardian(*ch) && ch->master && ch->specials.fighting) {
            if (ch->master->in_room != ch->in_room) {
                do_flee(ch, "", NULL, 0, 0);
            }
        }

        if (ch->specials.fighting && (GET_POS(ch) > POSITION_SITTING) && (IS_SET(ch->specials2.act, MOB_SWITCHING) || IS_SET(ch->specials2.act, MOB_SHADOW))) {
            for (tmpch = world[ch->in_room].people; tmpch;
                 tmpch = tmpch->next_in_room)
                if ((tmpch->specials.fighting == ch) && !number(0, 3) && CAN_SEE(ch, tmpch) && (tmpch != ch->specials.fighting)) {
                    ch->specials.fighting = tmpch;
                    act("$n turns to fight $N!", TRUE, ch, 0, tmpch, TO_ROOM);
                    break;
                }
        }

        if (AWAKE(ch) && !(ch->specials.fighting)) {
            if (IS_SET(ch->specials2.act, MOB_SCAVENGER)) { /* if scavenger */
                if (world[ch->in_room].contents && !number(0, 5)) {
                    for (max = 1, best_obj = 0, obj = world[ch->in_room].contents;
                         obj; obj = obj->next_content) {
                        if (CAN_GET_OBJ(ch, obj)) {
                            if (obj->obj_flags.cost > max) {
                                best_obj = obj;
                                max = obj->obj_flags.cost;
                            } else if (GET_ITEM_TYPE(obj) == ITEM_CONTAINER)
                                for (inside = obj->contains; inside; inside = next_obj) {
                                    next_obj = inside->next_content;
                                    if (inside->obj_flags.wear_flags > 1)
                                        if (IS_CARRYING_N(ch) < CAN_CARRY_N(ch)) {
                                            obj_from_obj(inside);
                                            obj_to_char(inside, ch);
                                            act("$n gets $p from $P.",
                                                TRUE, ch, inside, obj, TO_ROOM);
                                            do_wear(ch, "all", 0, 0, 0);
                                        }
                                }
                        }
                    } /* for */

                    if (best_obj) {
                        obj_from_room(best_obj);
                        obj_to_char(best_obj, ch);
                        act("$n gets $p.", FALSE, ch, best_obj, 0, TO_ROOM);
                    }
                }
            } /* Scavenger */

            if (!IS_SET(ch->specials2.act, MOB_SENTINEL) && (GET_POS(ch) == POSITION_STANDING) && (!ch->master) && ((door = number(0, 45)) < NUM_OF_DIRS) && CAN_GO(ch, door) && !IS_SET(world[EXIT(ch, door)->to_room].room_flags, NO_MOB) && !IS_SET(world[EXIT(ch, door)->to_room].room_flags, DEATH)) {
                if (ch->specials.last_direction == door)
                    ch->specials.last_direction = -1;
                else {
                    /* checking for STAY flags */
                    if ((!IS_SET(ch->specials2.act, MOB_STAY_ZONE) || (world[EXIT(ch, door)->to_room].zone == world[ch->in_room].zone)) && (!IS_SET(ch->specials2.act, MOB_STAY_TYPE) || (world[EXIT(ch, door)->to_room].sector_type == world[ch->in_room].sector_type))) {
                        ch->specials.last_direction = door;
                        do_move(ch, "", 0, ++door, 0);
                    }
                }
            } /* if can go */

            /* Here go Race aggressions */
            if (ch->specials2.pref) {
                for (tmp_ch = world[ch->in_room].people; tmp_ch;
                     tmp_ch = tmp_ch->next_in_room)
                    if ((ch != tmp_ch) && (!IS_SET(ch->specials2.act, MOB_MOUNT)) && IS_AGGR_TO(ch, tmp_ch) && CAN_SEE(ch, tmp_ch)) {
                        sscanf(tmp_ch->player.name, "%s", buf);
                        wtl.targ1.type = TARGET_CHAR;
                        wtl.targ1.ptr.ch = tmp_ch;
                        wtl.targ1.ch_num = tmp_ch->abs_number;
                        wtl.cmd = CMD_HIT;
                        do_hit(ch, buf, &wtl, 0, 0);
                        break;
                    }
                if (tmp_ch)
                    return; // continue;
            }

            /* Standard aggressive mobs */
            if (IS_SET(ch->specials2.act, MOB_AGGRESSIVE) && (!IS_SET(ch->specials2.act, MOB_MOUNT))) {
                found = FALSE;
                vict = 0;
                for (tmp_ch = world[ch->in_room].people; tmp_ch && !found;
                     tmp_ch = tmp_ch->next_in_room) {
                    if (!IS_NPC(tmp_ch) && CAN_SEE(ch, tmp_ch) && !PRF_FLAGGED(tmp_ch, PRF_NOHASSLE)) {
                        if (!IS_SET(ch->specials2.act, MOB_WIMPY) || !AWAKE(tmp_ch)) {
                            if ((IS_SET(ch->specials2.act, MOB_AGGRESSIVE_EVIL) && IS_EVIL(tmp_ch)) || (IS_SET(ch->specials2.act, MOB_AGGRESSIVE_GOOD) && IS_GOOD(tmp_ch)) || (IS_SET(ch->specials2.act, MOB_AGGRESSIVE_NEUTRAL) && IS_NEUTRAL(tmp_ch)) || (!IS_SET(ch->specials2.act, MOB_AGGRESSIVE_EVIL) && !IS_SET(ch->specials2.act, MOB_AGGRESSIVE_NEUTRAL) && !IS_SET(ch->specials2.act, MOB_AGGRESSIVE_GOOD))) {
                                if (MOB_FLAGGED(ch, MOB_SWITCHING)) {
                                    if (!vict)
                                        vict = tmp_ch;
                                    else if (number(0, 1))
                                        vict = tmp_ch;
                                } else {
                                    vict = tmp_ch;
                                    found = TRUE;
                                }
                            }
                        }
                    }
                }
                if (vict) {
                    wtl.targ1.type = TARGET_CHAR;
                    wtl.targ1.ptr.ch = vict;
                    wtl.targ1.ch_num = vict->abs_number;
                    wtl.cmd = CMD_HIT;
                    do_hit(ch, buf, &wtl, 0, 0);
                    vict = 0;
                }
            } /* if aggressive */

            if ((IS_SET(ch->specials2.act, MOB_MEMORY) || IS_SET(ch->specials2.act, MOB_HUNTER) || (IS_AFFECTED(ch, AFF_HUNT))) && ch->specials.memory) {
                /* we assume pets do not hunt by themselves */
                if (MOB_FLAGGED(ch, MOB_PET) && (GET_POS(ch) == POSITION_FIGHTING))
                    do_flee(ch, "", 0, 0, 0);

                /* checking memory */
                if (!IS_SET(ch->specials2.act, MOB_SENTINEL) && (GET_POS(ch) == POSITION_STANDING)) {
                    /* hunting the victim */
                }

                vict = 0;
                for (names = ch->specials.memory; names && !vict;
                     names = names->next_on_mob) {
                    if (names->enemy && char_exists(names->enemy_number) && (names->enemy->in_room == ch->in_room) && CAN_SEE(ch, names->enemy))
                        vict = names->enemy;
                }

                if (vict) {
                    if (ch->master == vict)
                        forget(ch, vict);
                    else {
                        if (GET_INT(ch) <= 6)
                            act("$n snarls and lunges at $N!", FALSE, ch, 0, vict, TO_ROOM);
                        else
                            act("$n grins evilly and attacks $N!", FALSE, ch, 0, vict, TO_ROOM);
                        sscanf(vict->player.name, "%s", buf);
                        wtl.targ1.type = TARGET_CHAR;
                        wtl.targ1.ptr.ch = vict;
                        wtl.targ1.ch_num = vict->abs_number;
                        wtl.cmd = CMD_HIT;
                        do_hit(ch, buf, &wtl, 0, 0);
                    }
                } else if (IS_SET(ch->specials2.act, MOB_HUNTER) || IS_AFFECTED(ch, AFF_HUNT)) {
                    int modifier = 0;

                    if (IS_AFFECTED(ch, AFF_CONFUSE))
                        modifier = get_confuse_modifier(ch);

                    for (names = ch->specials.memory; names && !vict;
                         names = names->next_on_mob) {
                        if (names->enemy && char_exists(names->enemy_number) && (names->enemy->in_room != NOWHERE) && (number(0, 100) > modifier)) // confuse modifier...
                            tmp = find_first_step(ch->in_room, names->enemy->in_room);
                        else
                            tmp = BFS_NO_PATH;
                        if (tmp >= 0) { // found the way, moving there
                            do_move(ch, "", 0, tmp + 1, 0);
                            break;
                        }
                    }
                }
            } /* mob memory */
        }
    } /* If IS_MOB(ch)  */
}

/* Mob Memory Routines */
int memory_rec_counter = 0;
struct memory_rec* memory_rec_pool = 0;
struct memory_rec* memory_rec_active = 0;

struct memory_rec*
get_from_memory_rec_pool()
{
    struct memory_rec* afnew;

    if (memory_rec_pool) {
        afnew = memory_rec_pool;
        memory_rec_pool = afnew->next;
    } else {
        CREATE(afnew, memory_rec, 1);
        memory_rec_counter++;
    }
    afnew->next = memory_rec_active;
    memory_rec_active = afnew;

    return afnew;
}

void put_to_memory_rec_pool(struct memory_rec* oldaf)
{
    struct memory_rec* tmpaf;

    if (oldaf == memory_rec_active)
        memory_rec_active = oldaf->next;
    else {
        for (tmpaf = memory_rec_active; tmpaf->next; tmpaf = tmpaf->next) {
            if (tmpaf->next == oldaf) {
                tmpaf->next = oldaf->next;
                break;
            }
        }
    }
    oldaf->next = memory_rec_pool;
    oldaf->next_on_mob = 0;
    memory_rec_pool = oldaf;
}

/* make ch remember victim */
void remember(struct char_data* ch, struct char_data* victim)
{
    struct memory_rec* tmp;
    unsigned char present = FALSE;

    if (!IS_NPC(ch) || IS_NPC(victim))
        return;

    for (tmp = ch->specials.memory; tmp && !present; tmp = tmp->next_on_mob)
        if (tmp->id == GET_IDNUM(victim)) {
            present = TRUE;
            tmp->enemy = victim;
            tmp->enemy_number = victim->abs_number;
        }

    if (!present) {
        tmp = get_from_memory_rec_pool();
        tmp->next_on_mob = ch->specials.memory;
        tmp->id = GET_IDNUM(victim);
        tmp->enemy = victim;
        tmp->enemy_number = victim->abs_number;
        ch->specials.memory = tmp;
    }
}

/* make ch forget victim */
void forget(struct char_data* ch, struct char_data* victim)
{
    struct memory_rec *curr, *prev = NULL;

    if (!(curr = ch->specials.memory))
        return;

    while (curr && curr->id != GET_IDNUM(victim)) {
        prev = curr;
        curr = curr->next_on_mob;
    }

    if (!curr)
        return; /* person wasn't there at all. */

    if (curr == ch->specials.memory)
        ch->specials.memory = curr->next_on_mob;
    else
        prev->next_on_mob = curr->next_on_mob;

    put_to_memory_rec_pool(curr);
}

/* erase ch's memory */
void clear_memory(struct char_data* ch)
{
    struct memory_rec *curr, *next;

    curr = ch->specials.memory;

    while (curr) {
        next = curr->next_on_mob;
        put_to_memory_rec_pool(curr);
        curr = next;
    }

    ch->specials.memory = NULL;
}

int update_memory_list(struct char_data* victim)
{
    struct memory_rec* tmprec;
    int count;

    count = 0;
    for (tmprec = memory_rec_active; tmprec; tmprec = tmprec->next) {
        if (tmprec->id == GET_IDNUM(victim)) {
            tmprec->enemy = victim;
            tmprec->enemy_number = victim->abs_number;
            count++;
        }
    }
    return count;
}

ACMD(do_sleep);
ACMD(do_rest);
ACMD(do_sit);
ACMD(do_stand);
ACMD(do_wake);

void enforce_position(struct char_data* ch, int new_pos)
{

    if (!IS_NPC(ch))
        return;

    if (GET_POS(ch) == new_pos)
        return;
    if (GET_POS(ch) == POSITION_FIGHTING)
        return;
    if (GET_POS(ch) <= POSITION_STUNNED)
        return;

    if ((GET_POS(ch) <= POSITION_SLEEPING) && (new_pos > POSITION_SLEEPING))
        do_wake(ch, "", 0, 0, 0);

    switch (new_pos) {
    case POSITION_SLEEPING:
        do_sleep(ch, "", 0, 0, 0);
        break;

    case POSITION_RESTING:
        do_rest(ch, "", 0, 0, 0);
        break;

    case POSITION_SITTING:
        do_sit(ch, "", 0, 0, 0);
        break;

    case POSITION_STANDING:
        do_stand(ch, "", 0, 0, 0);
        break;
    }
}
