/* ************************************************************************
 *   File: handler.c                                     Part of CircleMUD *
 *  Usage: internal funcs: moving and finding chars/objs                   *
 *                                                                         *
 *  All rights reserved.  See license.doc for complete information.        *
 *                                                                         *
 *  Copyright (C) 1993 by the Trustees of the Johns Hopkins University     *
 *  CircleMUD is based on DikuMUD, Copyright (C) 1990, 1991.               *
 ************************************************************************ */

/**************************************************************************
 *  ROTS Documentation                                                     *
 *                                                                         *
 *  Handling Affections                                                    *
 *    An affection should be applied to a character using affect_to_char   *
 *    with pointers to the character and the new affection.                *
 *    To remove an affection use affect_from_char sending a pointer to the *
 *    character and the skill number (from spells_pa.cc).                  *
 *    To remove an unknown affection from a character use affect_remove    *
 *    after checking that the pointer you are sending to the function is   *
 *    present in ch->affected.                                             *
 *                                                                         *
 *  affected_type_pool                                                     *
 *    This is a linked list of unused affections which can be allocated to *
 *    characters as and when they are needed.  Once the affection is       *
 *    removed from a character it returns to the pool until it is needed.  *
 *    If the pool becomes empty then a call to get_from_affected_type_pool *
 *    will allocate resources for a new affection. Rooms also use this     *
 *    pool since their affection handling should be almost identical to    *
 *    characters.                                                          *
 **************************************************************************/

#include "platdef.h"
#include <assert.h>
#include <ctype.h>
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
#include "zone.h" /* For zone_table */

#include "base_utils.h"
#include "char_utils.h"
#include <iostream>
#include <sstream>
#include <string>

/* external vars */
extern struct room_data world;
extern struct obj_data* object_list;
extern struct char_data* character_list;
extern struct index_data* mob_index;
extern struct index_data* obj_index;
extern struct descriptor_data* descriptor_list;
extern struct char_data* fast_update_list;
extern char* MENU;
extern int top_of_world;
extern struct skill_data skills[];
extern sh_int encumb_table[MAX_WEAR];
extern sh_int leg_encumb_table[MAX_WEAR];
extern long race_affect[];
extern int max_race_str[];

/* external functions */
void free_char(struct char_data*);
void stop_fighting(struct char_data*);
void remove_follower(struct char_data*);
void clear_memory(struct char_data*);
void show_character_menu(struct descriptor_data* d);

ACMD(do_save);
ACMD(do_return);

#define MAX_MAUL_DODGE 50

char char_control_array[MAX_CHARACTERS / 8 + 1];
long last_control_set = -1;

int dummy_affected_var = 17;
universal_list* affected_list = 0;
universal_list* affected_list_pool = 0;

affected_type* affected_type_pool = 0;
int affected_type_counter = 0;

struct affected_type* get_from_affected_type_pool();
void put_to_affected_type_pool(struct affected_type*);

follow_type* follow_type_pool = 0;
int follow_type_counter = 0;

struct follow_type* get_from_follow_type_pool();
void put_to_follow_type_pool(struct follow_type*);

char fname_nameholder[100];
char* fname(char* namelist)
{
    //   char	holder[30];
    register char* point;

    for (point = fname_nameholder; isalpha(*namelist); namelist++, point++)
        *point = *namelist;

    *point = '\0';

    return (fname_nameholder);
}

int char_power(int lev)
{
    if (lev >= LEVEL_IMMORT)
        return 0;

    return MIN((lev + 2), 16 + lev / 2) * MIN(lev + 2, 32);
}

/*
 * Decide if `character' and `other' are on the same side of the race
 * war.  Return 0 if they are, return 1 if they aren't.
 */
int other_side(const char_data* character, const char_data* other)
{
    if (IS_NPC(other) && !IS_AFFECTED(other, AFF_CHARM))
        return 0;
    if (IS_NPC(character) && !IS_AFFECTED(character, AFF_CHARM))
        return 0;
    if ((GET_RACE(character) == RACE_GOD) || (GET_RACE(other) == RACE_GOD))
        return 0;
    if (RACE_EAST(other) && !(RACE_EAST(character)))
        return 1;
    if (!(RACE_EAST(other)) && RACE_EAST(character))
        return 1;
    if (RACE_MAGI(other) && !(RACE_MAGI(character)))
        return 1;
    if (!(RACE_MAGI(other)) && RACE_MAGI(character))
        return 1;
    if (RACE_EVIL(other) && RACE_GOOD(character))
        return 1;
    if (RACE_GOOD(other) && RACE_EVIL(character))
        return 1;

    return 0;
}

int other_side_num(int ch_race, int i_race)
{
    if ((ch_race == RACE_GOD) || (i_race == RACE_GOD))
        return 0;
    if ((ch_race <= RACE_BEORNING) && (i_race <= RACE_BEORNING))
        return 0;

    if ((ch_race >= RACE_URUK) && (ch_race != RACE_MAGUS) && (ch_race != RACE_EASTERLING) && (ch_race != RACE_HARADRIM) && (i_race >= RACE_URUK) && (i_race != RACE_MAGUS) && (i_race != RACE_EASTERLING) && (i_race != RACE_HARADRIM))
        return 0;

    if (((ch_race == RACE_MAGUS) || (ch_race == RACE_HARADRIM)) && ((i_race == RACE_MAGUS) || (i_race == RACE_HARADRIM)))
        return 0;

    if (ch_race == i_race)
        return 0;

    return 1;
}

void recount_light_room(int room)
{
    struct char_data* tmpch;
    struct obj_data* tmpobj;
    int count, tmp;

    if ((room < 0) || (room >= top_of_world))
        return;

    count = 0;
    for (tmpch = world[room].people; tmpch; tmpch = tmpch->next_in_room)
        for (tmp = 0; tmp < MAX_WEAR; tmp++)
            if (tmpch->equipment[tmp])
                if (tmpch->equipment[tmp]->obj_flags.type_flag == ITEM_LIGHT)
                    if ((tmpch->equipment[tmp]->obj_flags.value[2] != 0) && (tmpch->equipment[tmp]->obj_flags.value[3] != 0))
                        count++;

    for (tmpobj = world[room].contents; tmpobj; tmpobj = tmpobj->next_content)
        if ((tmpobj->obj_flags.value[2] != 0) && (tmpobj->obj_flags.value[3] != 0))
            count++;

    world[room].light = count;
}

int isname(const char* str, const char* namelist, char full)
{
    register char *curname, *curstr;
    int tmp;

    while (*str && (*str <= ' '))
        str++;

    tmp = strlen(str);
    if (!tmp)
        return 0;

    if ((tmp < 3) || (tmp > 4))
        full = 1;

    if (!namelist)
        return 0;

    curname = (char*)namelist;
    for (;;) {
        for (curstr = (char*)str;; curstr++, curname++) {
            if (!*curstr && (!full || !isalpha(*curname)))
                return 1;

            if (!*curname)
                return 0;

            if (!*curstr || *curname == ' ')
                break;

            if (LOWER(*curstr) != LOWER(*curname))
                break;
        } /* skip to next name */

        for (; isalpha(*curname); curname++)
            ;

        if (!*curname)
            return 0;

        /* first char of new name */
        for (; *curname && (!isalpha(*curname) || (*curname == ' ')); curname++)
            ;
    }
}

void affect_modify_room(struct room_data* room, byte loc, int mod,
    long bitv, char add)
{
    bitv = bitv & (~PERMAFFECT);

    if (add == AFFECT_MODIFY_SET)
        SET_BIT(room->room_flags, bitv);
    else if (add == AFFECT_MODIFY_REMOVE) {
        REMOVE_BIT(room->room_flags, bitv);
        mod = -mod;
    }
}

void affect_modify(struct char_data* ch, byte loc, int mod, long bitv, char add, sh_int counter)
{
    int maxabil, tmp, tmp2;

    if (add == AFFECT_MODIFY_SET) {
        SET_BIT(ch->specials.affected_by, bitv);
        if (utils::is_set(bitv, long(AFF_CHARM))) {
            ch->damage_details.reset();
        }
    } else if (add == AFFECT_MODIFY_REMOVE) {
        REMOVE_BIT(ch->specials.affected_by, bitv);
        if (utils::is_set(bitv, long(AFF_CHARM))) {
            ch->damage_details.reset();
        }

        mod = -mod;
    }
    ch->specials.affected_by |= race_affect[GET_RACE(ch)];

    maxabil = (IS_NPC(ch) ? 25 : 25);

    if (add == AFFECT_MODIFY_TIME) {
        return; /* so, usual affects are not modified in this call */
    }

    switch (loc) {
    case APPLY_NONE:
        break;

    case APPLY_STR:
        SET_STR_BASE(ch, GET_STR_BASE(ch) + mod);
        SET_STR(ch, GET_STR(ch) + mod);
        break;

    case APPLY_LEA:
        GET_LEA_BASE(ch) += mod;
        GET_LEA(ch) += mod;
        break;

    case APPLY_DEX:
        GET_DEX_BASE(ch) += mod;
        GET_DEX(ch) += mod;
        break;

    case APPLY_INT:
        GET_INT_BASE(ch) += mod;
        GET_INT(ch) += mod;
        break;

    case APPLY_WILL:
        GET_WILL_BASE(ch) += mod;
        GET_WILL(ch) += mod;
        break;

    case APPLY_CON:
        GET_CON_BASE(ch) += mod;
        GET_CON(ch) += mod;
        break;

    case APPLY_PROF:
        /* ??? GET_PROF(ch) += mod; */
        break;

    case APPLY_LEVEL:
        /* ??? GET_LEVEL(ch) += mod; */
        break;

    case APPLY_AGE:
        ch->player.time.birth -= (mod * SECS_PER_MUD_YEAR);
        break;

    case APPLY_CHAR_WEIGHT:
        GET_WEIGHT(ch) += mod;
        break;

    case APPLY_CHAR_HEIGHT:
        GET_HEIGHT(ch) += mod;
        break;

    case APPLY_MANA:
        GET_MAX_MANA(ch) += mod;
        if (GET_MANA(ch) >= GET_MAX_MANA(ch) - mod)
            GET_MANA(ch) += mod;

        break;

    case APPLY_WILLPOWER:
        GET_WILLPOWER(ch) += mod;
        break;

    case APPLY_HIT:
        GET_MAX_HIT(ch) += mod;
        if (GET_HIT(ch) >= GET_MAX_HIT(ch) - mod)
            GET_HIT(ch) += mod;

        break;

    case APPLY_MOVE:
        GET_MAX_MOVE(ch) += mod;
        if (GET_MOVE(ch) >= GET_MAX_MOVE(ch) - mod)
            GET_MOVE(ch) += mod;
        break;

    case APPLY_GOLD:
        break;

    case APPLY_EXP:
        break;

    case APPLY_DODGE:
        SET_DODGE(ch) += mod;
        break;

    case APPLY_OB:
        SET_OB(ch) += mod;
        break;

    case APPLY_SPELL_PEN:
        ch->points.spell_pen += mod;
        break;

    case APPLY_SPELL_POW:
        ch->points.spell_power += mod;
        break;

    case APPLY_DAMROLL:
        GET_DAMAGE(ch) += mod;
        break;

    case APPLY_SAVING_SPELL:
        GET_SAVE(ch) += mod;
        break;

    case APPLY_VISION:
        if (add) {
            if (mod > 0)
                SET_BIT(ch->specials.affected_by, AFF_INFRARED);
            if (mod < 0)
                SET_BIT(ch->specials.affected_by, AFF_BLIND);
        } else {
            if (mod > 0)
                REMOVE_BIT(ch->specials.affected_by, AFF_BLIND);
            if (mod < 0)
                REMOVE_BIT(ch->specials.affected_by, AFF_INFRARED);
        }

    case APPLY_REGEN:
        break;

    case APPLY_SPEED:
        GET_ENE_REGEN(ch) += mod;
        break;

    case APPLY_BEND: {
        GET_ENE_REGEN(ch) += (GET_ENE_REGEN(ch) / 2);
        SET_OB(ch) += mod;
    } break;

    case APPLY_ARMOR:
        //     mod = (2*mod*GET_PERCEPTION(ch))/100;
        //     SET_DODGE(ch) += mod;
        break;
    case APPLY_MAUL:
        if (!add) {
            SET_DODGE(ch) += std::min((counter * 5), MAX_MAUL_DODGE);
        }

        if (add) {
            SET_DODGE(ch) += -(std::min((counter * 5), MAX_MAUL_DODGE));
        }
        break;

    case APPLY_PERCEPTION:
        // Lego: Since we loop through every gear slot, we need to track the underlying perception value rather than update the final perception value
        //       because each subsequent iteration would add/subtract the overridden minimum percep.
        //       Then we override the underlying value with our minimum perception logic and expose that to the rest of the game.
        ch->specials2.rawPerception += mod;

        if (affected_by_spell(ch, SPELL_INSIGHT)) {
            int minimumRacePerception = utils::get_minimum_insight_perception(*ch);

            ch->specials2.perception = std::max(ch->specials2.rawPerception, minimumRacePerception);
        } else {
            ch->specials2.perception = ch->specials2.rawPerception;
        }
        break;

    case APPLY_SPELL:
        if (!add)
            mod = -mod;
        tmp = mod & 255; // spell number, in skills[] table
        tmp2 = mod / 256; // spell level
        if (!tmp2)
            tmp2 = GET_LEVEL(ch);
        if (tmp >= 128)
            break;

        if (!skills[tmp].spell_pointer)
            break;

        if (add)
            skills[tmp].spell_pointer(ch, "", SPELL_TYPE_SPELL, ch, 0, 0, 1);
        else
            skills[tmp].spell_pointer(ch, "", SPELL_TYPE_ANTI, ch, 0, 0, 1);
        break;

    case APPLY_BITVECTOR:
        if (add) {
            if ((mod < 0) || (mod > 31))
                mod = 0;
            SET_BIT(ch->specials.affected_by, 1 << mod);
        } else {
            mod = -mod;
            if ((mod < 0) || (mod > 31))
                mod = 0;
            REMOVE_BIT(ch->specials.affected_by, 1 << mod);
        }
        break;

    case APPLY_MANA_REGEN:
        ch->points.mana_regen += mod;
        break;

    case APPLY_RESIST:
        if (mod >= 0)
            GET_RESISTANCES(ch) |= (1 << mod);
        else
            GET_RESISTANCES(ch) &= ~(1 << mod);
        break;

    case APPLY_VULN:
        if (mod >= 0)
            GET_VULNERABILITIES(ch) |= (1 << mod);
        else
            GET_VULNERABILITIES(ch) &= ~(1 << mod);
        break;

    default:
        log("SYSERR: Unknown apply adjust attempt (handler.c, affect_modify).");
        break;
    } /* switch */
}

/* This updates a character by subtracting everything he is affected by */
/* restoring original abilities, and then affecting all again     ?????      */

void affect_total_room(struct room_data* room, int mode)
{
}

void affect_naked(char_data* ch)
{
    // sets some intrinsic parameters
    // assumes that the char is naked and has no affections.

    // SET_PERCEPTION(ch, get_naked_perception(ch));
    int nakedPerception = get_naked_perception(ch);
    ch->specials2.rawPerception = ch->specials2.perception = nakedPerception;
    GET_WILLPOWER(ch) = get_naked_willpower(ch);
    ch->specials.affected_by |= race_affect[GET_RACE(ch)];

    if (!IS_NPC(ch)) {
        GET_RESISTANCES(ch) = 0;
        GET_VULNERABILITIES(ch) = 0;
    }
}

void apply_gear_affects(char_data* character, const obj_data* item, int modify_flag)
{
    for (int count = 0; count < MAX_OBJ_AFFECT; ++count) {

        const obj_affected_type& obj_affect = item->affected[count];
        if (obj_affect.location == APPLY_SPELL)
            continue;

        affect_modify(character, obj_affect.location, obj_affect.modifier, item->obj_flags.bitvector, modify_flag, 0);
    }
}

void apply_gear_affects(char_data* character, int modify_flag)
{
    for (int item_index = 0; item_index < MAX_WEAR; ++item_index) {
        const obj_data* item = character->equipment[item_index];
        if (item == nullptr)
            continue;

        if (item_index == HOLD && !CAN_WEAR(item, ITEM_HOLD))
            continue;

        apply_gear_affects(character, item, modify_flag);
    }
}

void modify_affects(char_data* character, int modify_flag)
{
    int count = 0;
    affected_type* af = character->affected;
    while (count < MAX_AFFECT && af != nullptr) {
        affect_modify(character, af->location, af->modifier, af->bitvector, modify_flag, af->counter);
        ++count;
        af = af->next;
    }
}

void affect_total(struct char_data* ch, int mode)
{
    if (mode & AFFECT_TOTAL_REMOVE) {
        apply_gear_affects(ch, AFFECT_MODIFY_REMOVE);
        modify_affects(ch, AFFECT_MODIFY_REMOVE);

        recalc_abilities(ch);
        affect_naked(ch);
    }

    if (mode & AFFECT_TOTAL_SET) {
        apply_gear_affects(ch, AFFECT_MODIFY_SET);
        modify_affects(ch, AFFECT_MODIFY_SET);
    }

    if (mode & AFFECT_TOTAL_TIME) {
        apply_gear_affects(ch, AFFECT_MODIFY_TIME);
        modify_affects(ch, AFFECT_MODIFY_TIME);
    }
    /* Make certain values are between 0..100, not < 0 and not > 100! */

    signed char max_value = 100;
    signed char min_dex_str = 1;
    signed char min_others = 0;

    ch->abilities.dex = std::max(min_dex_str, std::min(ch->abilities.dex, max_value));
    ch->abilities.intel = std::max(min_others, std::min(ch->abilities.intel, max_value));
    ch->abilities.wil = std::max(min_others, std::min(ch->abilities.wil, max_value));
    ch->abilities.con = std::max(min_others, std::min(ch->abilities.con, max_value));
    ch->abilities.str = std::max(min_dex_str, std::min(ch->abilities.str, max_value));
    ch->abilities.lea = std::max(min_others, std::min(ch->abilities.lea, max_value));
}

/*  If there is a structure of affected_type in the affected_type_pool list then
        it is removed, if not then one is CREATEd.  A pointer to an available affected_type
        structure is returned to be applied to a character or room. */

struct affected_type* get_from_affected_type_pool()
{
    struct affected_type* afnew;

    if (affected_type_pool) {
        afnew = affected_type_pool;
        affected_type_pool = afnew->next;

        bzero(afnew, sizeof(affected_type));
    } else {
        CREATE(afnew, struct affected_type, 1);
        affected_type_counter++;
    }
    return afnew;
}

/* Puts a struct affected_type into the head of the pool.
 ** Replaced with free at the moment to aid bughunting. */

void put_to_affected_type_pool(struct affected_type* oldaf)
{

    free(oldaf);
    //  oldaf->next = affected_type_pool;
    //  affected_type_pool = oldaf;
}

/* Insert an affect_type in a char_data structure
   Automatically sets apropriate bits and applys

   1.  Checks to see if the character is on the affected list.  If not they are added
   2.  Allocates memory for the new affection (also inserting it into affected_list)
   3.  Copies the parameters of the affection to the structure in the affected_list
   4.  Adds it to the ch->affected list
   5.  Calls affect_modify and affect_total to update the characters stats/abilities  */

void affect_to_char(struct char_data* ch, struct affected_type* af)
{
    struct affected_type* affected_alloc;
    universal_list* tmplist;
    char mybuf[255];

    if (!ch)
        return;

    // 1
    if (!ch->affected) {
        tmplist = pool_to_list(&affected_list, &affected_list_pool);
        tmplist->ptr.ch = ch;
        tmplist->number = ch->abs_number;
        tmplist->type = TARGET_CHAR;

        sprintf(mybuf, "Char to aff_list: %s\n\r", GET_NAME(ch));
    }

    // 2
    affected_alloc = get_from_affected_type_pool();

    // 3
    *affected_alloc = *af;

    // 4
    affected_alloc->next = ch->affected;
    ch->affected = affected_alloc;

    affected_alloc->time_phase = get_current_time_phase();

    // 5
    affect_modify(ch, af->location, af->modifier, af->bitvector,
        AFFECT_MODIFY_SET, af->counter);
    affect_total(ch);
}

/* Standard mud call to put an affected structure to a room.  The room is added to
   the list of affected rooms if necessary, and its values are updated.  Similar to
   affect_to_char */

void affect_to_room(struct room_data* room, struct affected_type* af)
{
    struct affected_type* affected_alloc;
    struct affected_type* tmpaf;
    universal_list* tmplist;
    char perms_only;

    perms_only = 1;
    for (tmpaf = room->affected; tmpaf; tmpaf = tmpaf->next)
        if (!IS_SET(tmpaf->bitvector, PERMAFFECT))
            perms_only = 0;

    if (perms_only) {
        tmplist = pool_to_list(&affected_list, &affected_list_pool);
        tmplist->ptr.room = room;
        tmplist->number = room->number;
        tmplist->type = TARGET_ROOM;
    }

    affected_alloc = get_from_affected_type_pool();

    *affected_alloc = *af;
    affected_alloc->time_phase = get_current_time_phase();

    affected_alloc->next = room->affected;
    room->affected = affected_alloc;

    affect_modify_room(room, af->location, af->modifier, af->bitvector,
        AFFECT_MODIFY_SET);
    affect_total_room(room);
}

/* Remove an affected_type structure from a char (called when duration
   reaches zero). Pointer *af must never be NIL! Frees mem and calls
   affect_location_apply
                                               */
void affect_remove(struct char_data* ch, struct affected_type* af)
{
    struct affected_type* hjp;
    universal_list *tmplist, *tmplist2;
    int tmp;

    //   assert(ch->affected);
    // Looks as though the following line is "just in case", but where did af come from in this case?
    if (!ch->affected)
        return;

    affect_modify(ch, af->location, af->modifier, af->bitvector,
        AFFECT_MODIFY_REMOVE, af->counter);

    /* remove structure *af from linked list */
    if (ch->affected == af) {
        /* remove head of list */
        ch->affected = af->next;
    } else {
        for (hjp = ch->affected, tmp = 0;
             (hjp->next) && (hjp->next != af) && (tmp < MAX_AFFECT);
             hjp = hjp->next, tmp++) {
        }
        if (hjp->next != af) {
            log("SYSERR: FATAL : Could not locate affected_type in ch->affected. (handler.c, affect_remove)");
            //	 exit(1);
            return;
        }
        hjp->next = af->next; /* skip the af element */
    }

    //   RELEASE(af);
    put_to_affected_type_pool(af);

    if (!ch->affected && affected_list) {
        for (tmplist = affected_list; tmplist; tmplist = tmplist2) {
            tmplist2 = tmplist->next;
            if ((tmplist->type == TARGET_CHAR) && (tmplist->ptr.ch == ch))
                from_list_to_pool(&affected_list, &affected_list_pool, tmplist);
        }
    }

    affect_total(ch);
}

void affect_remove_notify(struct char_data* ch, struct affected_type* af)
{
    extern char* spell_wear_off_msg[];

    if (*spell_wear_off_msg[af->type] && !PLR_FLAGGED(ch, PLR_WRITING))
        vsend_to_char(ch, "%s\n", spell_wear_off_msg[af->type]);

    affect_remove(ch, af);
}

/* Removes an affection from a room */

void affect_remove_room(struct room_data* room, struct affected_type* af)
{
    struct affected_type *hjp, *tmpaf;
    universal_list *tmplist, *tmplist2;
    int tmp, perms_only;

    //   assert(ch->affected);
    if (!room->affected)
        return;

    affect_modify_room(room, af->location, af->modifier, af->bitvector,
        AFFECT_MODIFY_REMOVE);

    /* remove structure *af from linked list */
    if (room->affected == af) {
        /* remove head of list */
        room->affected = af->next;
    } else {
        for (hjp = room->affected, tmp = 0;
             (hjp->next) && (hjp->next != af) && (tmp < MAX_AFFECT);
             hjp = hjp->next, tmp++) {
        }
        if (hjp->next != af) {
            log("SYSERR: FATAL : Could not locate affected_type in room->affected. (handler.c, affect_remove_room)");
            //	 exit(1);
            return;
        }
        hjp->next = af->next; /* skip the af element */
    }

    //   RELEASE(af);
    put_to_affected_type_pool(af);

    perms_only = 1;
    for (tmpaf = room->affected; tmpaf; tmpaf = tmpaf->next)
        if (!IS_SET(tmpaf->bitvector, PERMAFFECT))
            perms_only = 0;

    if (perms_only && affected_list) {
        for (tmplist = affected_list; tmplist; tmplist = tmplist2) {
            tmplist2 = tmplist->next;
            if ((tmplist->type == TARGET_ROOM) && (tmplist->ptr.room == room))
                from_list_to_pool(&affected_list, &affected_list_pool, tmplist);
        }
    }

    affect_total_room(room);
}

/* Returns 1 if a character is found in the affected_list.  0 if not */

int in_affected_list(struct char_data* ch)
{
    universal_list* tmplist;
    int found;

    found = 0;
    for (tmplist = affected_list; tmplist; tmplist = tmplist->next) {
        if (tmplist->ptr.ch == ch)
            found = 1;
    }
    return found;
}

/*
 * Same as affect_from_char below, except also sends a notification
 * to the character that the spell has faded.
 */
void affect_from_char_notify(struct char_data* ch, byte skill)
{
    extern char* spell_wear_off_msg[];

    if (*spell_wear_off_msg[skill] && !PLR_FLAGGED(ch, PLR_WRITING))
        vsend_to_char(ch, "%s\n", spell_wear_off_msg[skill]);

    affect_from_char(ch, skill);
}

/* Call affect_remove with every spell of spelltype "skill"
   Standard mud call to remove an affection of known type from a character.  */

void affect_from_char(struct char_data* ch, byte skill)
{
    struct affected_type *hjp, *t;
    int tmp;

    for (hjp = ch->affected, tmp = 0; hjp && (tmp < MAX_AFFECT);
         hjp = t, tmp++) {
        t = hjp->next;
        if (hjp->type == skill)
            affect_remove(ch, hjp);
    }
}

/* Return if a char is affected by a spell (SPELL_XXX), NULL indicates not affected.
   start_affect is not used anywhere in the mud...*/
affected_type* affected_by_spell(const char_data* ch, byte skill, affected_type* start_affect)
{
    if (!start_affect)
        start_affect = ch->affected;

    int count = 0;
    for (affected_type* status_affect = start_affect; status_affect && (count < MAX_AFFECT); status_affect = status_affect->next, count++) {
        if (status_affect->type == skill) {
            return status_affect;
        }
    }

    return NULL;
}

/* Return a pointer to an affection if the room is affected by the spell.
   Otherwise return null. */
affected_type* room_affected_by_spell(const room_data* room, int spell)
{
    for (affected_type* status_effect = room->affected; status_effect; status_effect = status_effect->next) {
        if (status_effect->type == ROOMAFF_SPELL && status_effect->location == spell) {
            return status_effect;
        }
    }

    return NULL;
}

/* Similar to affect_to_char, affect_join is a general mud function to add an
   affection to a character.  If the character already has an affection of that
   type the values of the new affection are added.  Used for poison.  Average
   duration and average modifier are not implemented for some reason.*/

void affect_join(struct char_data* ch, struct affected_type* af,
    char avg_dur, char avg_mod)
{
    struct affected_type* hjp;
    char found = FALSE;

    for (hjp = ch->affected; !found && hjp; hjp = hjp->next) {
        if (hjp->type == af->type) {

            if (af->duration < hjp->duration)
                af->duration += hjp->duration;

            //	 if (avg_dur)
            //	    af->duration /= 2;

            if (((af->modifier >= 0) && (af->modifier < hjp->modifier)) || ((af->modifier >= 0) && (af->modifier < hjp->modifier)))
                af->modifier += hjp->modifier;

            //	 if (avg_mod)
            //	    af->modifier /= 2;

            affect_remove(ch, hjp);
            affect_to_char(ch, af);
            found = TRUE;
        }
    }
    if (!found)
        affect_to_char(ch, af);
}

//***************** follow_type procedures ********************************

struct follow_type* get_from_follow_type_pool()
{
    struct follow_type* folnew;

    if (follow_type_pool) {
        folnew = follow_type_pool;
        follow_type_pool = folnew->next;
    } else {
        CREATE(folnew, struct follow_type, 1);
        follow_type_counter++;
    }
    return folnew;
}

void put_to_follow_type_pool(struct follow_type* oldfol)
{
    oldfol->next = follow_type_pool;
    follow_type_pool = oldfol;
}

/* Do NOT call this before having checked if a circle of followers */
/* will arise. CH will follow leader                               */
void add_follower(char_data* follower, char_data* leader, int mode)
{
    if (!leader) {
        printf("add_follower called without leader for %s\n", GET_NAME(follower));
        return;
    }

    if (mode == FOLLOW_MOVE) {
        if (follower->master) {
            stop_follower(follower, FOLLOW_MOVE);
        }
        follower->master = leader;
    }

    follow_type* k = get_from_follow_type_pool();

    k->follower = follower;
    k->fol_number = follower->abs_number;
    if (mode == FOLLOW_MOVE) {
        k->next = leader->followers;
    }

    if (mode == FOLLOW_MOVE) {
        leader->followers = k;
        act("You now follow $N.", FALSE, follower, 0, leader, TO_CHAR);
        act("$n starts following you.", TRUE, follower, 0, leader, TO_VICT);
        act("$n now follows $N.", TRUE, follower, 0, leader, TO_NOTVICT);
    }
}

/* Called when stop following persons, or stopping charm */
/* This will NOT do if a character quits/dies!!          */

void put_to_memory_rec_pool(struct memory_rec* oldaf);

void stop_follower(struct char_data* ch, int mode)
{
    struct follow_type *j, *k;

    if (mode == FOLLOW_MOVE) {
        if (!ch->master)
            return;

        if ((GET_SPEC(ch->master) == PLRSPEC_PETS) && (IS_AFFECTED(ch, AFF_CHARM))) {
            ch->constabilities.str -= 2;
            ch->tmpabilities.str -= 2;
            ch->abilities.str -= 2;
            ch->points.ENE_regen -= 40;
            ch->points.damage -= 2;
        }

        forget(ch, ch->master); // in case we were "hunting" him

        if (IS_AFFECTED(ch, AFF_CHARM)) {
            act("You realize that $N is a jerk!", FALSE, ch, 0, ch->master, TO_CHAR);
            act("$n realizes that $N is a jerk!", FALSE, ch, 0, ch->master, TO_NOTVICT);
            act("$n hates your guts!", FALSE, ch, 0, ch->master, TO_VICT);
            if (affected_by_spell(ch, SKILL_TAME)) {
                affect_from_char(ch, SKILL_TAME);
                GET_MAX_MOVE(ch) -= 50; // move bonus for being tamed
            }
            if (affected_by_spell(ch, SKILL_RECRUIT)) {
                affect_from_char(ch, SKILL_RECRUIT);
            }
            REMOVE_BIT(ch->specials.affected_by, AFF_CHARM);
            ch->damage_details.reset();
        } else {
            act("You stop following $N.", FALSE, ch, 0, ch->master, TO_CHAR);
            if (ch->in_room == ch->master->in_room) {
                act("$n stops following $N.", FALSE, ch, 0, ch->master, TO_NOTVICT);
            }
            act("$n stops following you.", FALSE, ch, 0, ch->master, TO_VICT);
        }

        if (ch->master->followers->follower == ch) { /* Head of follower-list? */
            k = ch->master->followers;
            ch->master->followers = k->next;
            put_to_follow_type_pool(k);
        } else { /* locate follower who is not head of list */
            for (k = ch->master->followers; k->next->follower != ch; k = k->next)
                ;

            j = k->next;
            k->next = j->next;
            put_to_follow_type_pool(j);
        }

        ch->master = 0;
        if (affected_by_spell(ch, SKILL_TAME))
            affect_from_char(ch, SKILL_TAME);

        REMOVE_BIT(ch->specials.affected_by, AFF_CHARM);

        // Recursive call to rid ourselves of our group if we were a pet.
        if (IS_NPC(ch) && MOB_FLAGGED(ch, MOB_PET)) {
            REMOVE_BIT(MOB_FLAGS(ch), MOB_PET);
            stop_follower(ch, FOLLOW_GROUP);
        }
    }

    else if (mode == FOLLOW_REFOL) {
        if (!ch->master)
            return;

        act("You stop following $N.", FALSE, ch, 0, ch->master, TO_CHAR);
        if (ch->in_room == ch->master->in_room)
            act("$n stops following $N.", FALSE, ch, 0, ch->master, TO_NOTVICT);
        act("$n stops following you.", FALSE, ch, 0, ch->master, TO_VICT);

        if (ch->master->followers->follower == ch) { /* Head of follower-list? */
            k = ch->master->followers;
            ch->master->followers = k->next;
            put_to_follow_type_pool(k);
        } else { /* locate follower who is not head of list */
            for (k = ch->master->followers; k->next->follower != ch; k = k->next)
                ;

            j = k->next;
            k->next = j->next;
            put_to_follow_type_pool(j);
        }

        ch->master = 0;
    }
}

/* Check if making CH follow VICTIM will create an illegal */
/* Follow "Loop/circle"                                    */
char circle_follow(struct char_data* ch, struct char_data* victim, int mode)
{
    for (char_data* character = victim; character; character = character->master) {
        if (character == ch) {
            return (TRUE);
        }
    }

    return (FALSE);
}

/* Called when a character that follows/is followed dies */
void die_follower(char_data* character)
{
    struct follow_type *j, *k;

    if (character->master) {
        stop_follower(character, FOLLOW_MOVE);
    }

    for (k = character->followers; k; k = j) {
        j = k->next;
        stop_follower(k->follower, FOLLOW_MOVE);
    }

    group_data* group = character->group;
    if (group) {
        if (group->is_leader(character)) {
            // Do disband.
            size_t index = group->size() - 1;
            while (index >= 1) {
                remove_character_from_group(group->at(index), character);
                --index;
            }
        } else {
            // Remove the character from the group.
            remove_character_from_group(character, group->get_leader());
        }
    }
}

//**************************************************************************

/* move a player out of a room */
void char_from_room(struct char_data* ch)
{
    struct char_data* i;
    int tmp;
    if (ch->in_room == NOWHERE) {
        //      log("SYSERR: NOWHERE extracting char from room (handler.c, char_from_room)");
        //      exit(1);
        return; // he's already nowehre
    }

    for (tmp = 0; tmp < MAX_WEAR; tmp++)
        if (ch->equipment[tmp])
            if (ch->equipment[tmp]->obj_flags.type_flag == ITEM_LIGHT)
                if (ch->equipment[tmp]->obj_flags.value[2] && (ch->equipment[tmp]->obj_flags.value[3])) /* Light is ON */
                    world[ch->in_room].light--;

    if (ch == world[ch->in_room].people) /* head of list */
        world[ch->in_room].people = ch->next_in_room;

    else /* locate the previous element */ {
        for (i = world[ch->in_room].people;
             i && (i->next_in_room != ch); i = i->next_in_room)
            ;

        if (!i)
            return;

        i->next_in_room = ch->next_in_room;
    }

    tmp = char_power(GET_LEVEL(ch));

    if (!IS_NPC(ch)) {
        //     zone_table[world[ch->in_room].zone].nature_power -= tmp;
        if (RACE_GOOD(ch))
            zone_table[world[ch->in_room].zone].white_power -= tmp;
        else if (RACE_EVIL(ch))
            zone_table[world[ch->in_room].zone].dark_power -= tmp;
    }

    ch->in_room = NOWHERE;
    ch->next_in_room = 0;
    for (tmp = 0; ch->specials.fighting && (tmp < 100); tmp++) {
        if (ch->specials.fighting->specials.fighting == ch)
            stop_fighting(ch->specials.fighting);
        stop_fighting(ch);
    }
    if (tmp == 100) {
        sprintf(buf, "Char_from_room: could not stop fighting for %s.\n",
            GET_NAME(ch));
        mudlog(buf, NRM, LEVEL_GOD, TRUE);
    }
}

/* place a character in a room */
void char_to_room(struct char_data* ch, int room)
{
    struct char_data* tmpch;
    int tmp;

    /* append ch to the room's list */
    if (!world[room].people)
        world[room].people = ch;
    else {
        for (tmpch = world[room].people; tmpch->next_in_room; tmpch = tmpch->next_in_room)
            ;
        tmpch->next_in_room = ch;
    }
    ch->next_in_room = 0;
    ch->in_room = room;

    /* do they have a light? */
    for (tmp = 0; tmp < MAX_WEAR; tmp++)
        if (ch->equipment[tmp])
            if (ch->equipment[tmp]->obj_flags.type_flag == ITEM_LIGHT)
                if (ch->equipment[tmp]->obj_flags.value[2] && (ch->equipment[tmp]->obj_flags.value[3])) /* Light is ON */
                    world[room].light++;

    tmp = char_power(GET_LEVEL(ch));

    /* increase the goodness/evilness of this room's zone */
    if (!IS_NPC(ch)) {
        if (RACE_GOOD(ch))
            zone_table[world[room].zone].white_power += tmp;
        else if (RACE_EVIL(ch))
            zone_table[world[room].zone].dark_power += tmp;
    }
}

/* give an object to a char   */
void obj_to_char(struct obj_data* object, struct char_data* ch)
{
    object->next_content = ch->carrying;
    ch->carrying = object;
    object->carried_by = ch;
    object->in_room = NOWHERE;
    object->obj_flags.timer = -1;
    IS_CARRYING_W(ch) += GET_OBJ_WEIGHT(object);
    if (IS_RIDING(object->carried_by))
        IS_CARRYING_W(object->carried_by->mount_data.mount) += GET_OBJ_WEIGHT(object);
    IS_CARRYING_N(ch)
    ++;

    /* set flag for crash-save system */
    if (!IS_NPC(ch))
        SET_BIT(PLR_FLAGS(ch), PLR_CRASH);
}

/* take an object from a char */
void obj_from_char(struct obj_data* object)
{
    struct obj_data* tmp;
    int i;

    if (object->carried_by->carrying == object) { /* head of list */
        object->carried_by->carrying = object->next_content;
        IS_CARRYING_N(object->carried_by)
        --;
    } else {
        for (tmp = object->carried_by->carrying;
             tmp && (tmp->next_content != object);
             tmp = tmp->next_content)
            ; /* locate previous */
        if (tmp) {
            tmp->next_content = object->next_content;
            IS_CARRYING_N(object->carried_by)
            --;
        } else {
            for (i = 0; i < MAX_WEAR; i++)
                if (object->carried_by->equipment[i] == object)
                    break;
            if (i < MAX_WEAR)
                unequip_char(object->carried_by, i);
        }
    }

    /* set flag for crash-save system */
    if (!IS_NPC(object->carried_by))
        SET_BIT(PLR_FLAGS(object->carried_by), PLR_CRASH);

    if (IS_RIDING(object->carried_by))
        IS_CARRYING_W(object->carried_by->mount_data.mount) -= GET_OBJ_WEIGHT(object);

    IS_CARRYING_W(object->carried_by) -= GET_OBJ_WEIGHT(object);
    object->carried_by = 0;
    object->next_content = 0;
    object->in_room = NOWHERE;

    if (IS_OBJ_STAT(object, ITEM_WILLPOWER))
        REMOVE_BIT(object->obj_flags.extra_flags, ITEM_WILLPOWER);
    if (object->obj_flags.prog_number == 1)
        object->obj_flags.prog_number = 0;
}

void equip_char(char_data* character, obj_data* item, int item_slot)
{
    int was_poisoned = IS_AFFECTED(character, AFF_POISON);

    assert(item_slot >= 0 && item_slot < MAX_WEAR);

    if (character->equipment[item_slot]) {
        sprintf(buf, "SYSERR: Char is already equipped: %s, %s", GET_NAME(character),
            item->short_description);
        log(buf);
        return;
    }

    if (item->in_room != NOWHERE) {
        log("SYSERR: EQUIP: Obj is in_room when equip.");
        return;
    }

    if ((IS_OBJ_STAT(item, ITEM_ANTI_EVIL) && IS_EVIL(character)) || (IS_OBJ_STAT(item, ITEM_ANTI_GOOD) && IS_GOOD(character)) || (IS_OBJ_STAT(item, ITEM_ANTI_NEUTRAL) && IS_NEUTRAL(character))) {
        if (character->in_room != NOWHERE) {

            act("You are zapped by $p and instantly drop it.", FALSE, character, item, 0, TO_CHAR);
            act("$n is zapped by $p and instantly drops it.", FALSE, character, item, 0, TO_ROOM);
            obj_to_char(item, character); /* changed to drop in inventory instead of ground */
            return;
        } else
            log("SYSERR: ch->in_room = NOWHERE when equipping char.");
    }

    if ((IS_OBJ_STAT(item, ITEM_HARADRIM) && GET_RACE(character) != RACE_HARADRIM) || (IS_OBJ_STAT(item, ITEM_HUMAN) && GET_RACE(character) != RACE_HUMAN) || (IS_OBJ_STAT(item, ITEM_DWARF) && GET_RACE(character) != RACE_DWARF) || (IS_OBJ_STAT(item, ITEM_WOODELF) && GET_RACE(character) != RACE_WOOD) || (IS_OBJ_STAT(item, ITEM_HOBBIT) && GET_RACE(character) != RACE_HOBBIT) || (IS_OBJ_STAT(item, ITEM_BEORNING) && GET_RACE(character) != RACE_BEORNING) || (IS_OBJ_STAT(item, ITEM_URUK) && GET_RACE(character) != RACE_URUK) || (IS_OBJ_STAT(item, ITEM_ORC) && GET_RACE(character) != RACE_ORC) || (IS_OBJ_STAT(item, ITEM_MAGUS) && GET_RACE(character) != RACE_MAGUS) || (IS_OBJ_STAT(item, ITEM_OLOGHAI) && GET_RACE(character) != RACE_OLOGHAI)) {
        if (character->in_room != NOWHERE) {

            act("You are zapped by $p and instantly drop it.", FALSE, character, item, 0, TO_CHAR);
            act("$n is zapped by $p and instantly drops it.", FALSE, character, item, 0, TO_ROOM);
            obj_to_char(item, character); /* changed to drop in inventory instead of ground */
            return;
        } else
            log("SYSERR: ch->in_room = NOWHERE when equipping char.");
    }

    character->equipment[item_slot] = item;
    item->carried_by = character;
    item->obj_flags.timer = -1;

    // Encumb and weight update:
    character->points.encumb += item->obj_flags.value[2] * encumb_table[item_slot];
    character->specials2.leg_encumb += item->obj_flags.value[2] * leg_encumb_table[item_slot];
    if (encumb_table[item_slot])
        GET_ENCUMB_WEIGHT(character) += GET_OBJ_WEIGHT(item) * encumb_table[item_slot];
    else
        GET_ENCUMB_WEIGHT(character) += GET_OBJ_WEIGHT(item) / 2;
    GET_WORN_WEIGHT(character) += GET_OBJ_WEIGHT(item);

    IS_CARRYING_W(character) += GET_OBJ_WEIGHT(item);

    if ((item_slot == HOLD) && !CAN_WEAR(item, ITEM_HOLD))
        return;

    if (GET_ITEM_TYPE(item) == ITEM_ARMOR)
        SET_DODGE(character) += item->obj_flags.value[3];

    else if (GET_ITEM_TYPE(item) == ITEM_WEAPON) {
        SET_OB(character) += item->obj_flags.value[0];
        SET_PARRY(character) += item->obj_flags.value[1];

        if (GET_OBJ_WEIGHT(item) > (GET_BAL_STR(character) * 50) && !IS_TWOHANDED(character))
            send_to_char("This weapon seems too heavy for one hand.\n\r", character);
        else if (GET_OBJ_WEIGHT(item) > (GET_BAL_STR(character) * 100))
            send_to_char("This weapon seems too heavy for you!\n\r", character);

    } else if (GET_ITEM_TYPE(item) == ITEM_SHIELD) {
        SET_DODGE(character) += item->obj_flags.value[0];
        SET_PARRY(character) += item->obj_flags.value[1];
    } else if (GET_ITEM_TYPE(item) == ITEM_LIGHT) {
        if ((character->in_room != NOWHERE) && (item->obj_flags.value[2] != 0)) {
            if (item->obj_flags.value[3] == 0)
                item->obj_flags.value[3] = 1;
            world[character->in_room].light++;
        }
    }

    for (int j = 0; j < MAX_OBJ_AFFECT; j++)
        affect_modify(character, item->affected[j].location,
            item->affected[j].modifier,
            item->obj_flags.bitvector, AFFECT_MODIFY_SET, 0);

    affect_total(character);

    // Special case for poisoned objects.  The wearer should get poison damage
    // when wearing/removing something poisoned.
    if (was_poisoned == 0 && IS_AFFECTED(character, AFF_POISON)) {
        extern void raw_kill(struct char_data * character, char_data * killer, int attacktype);

        damage(character, character, 5, SPELL_POISON, 0);

        if (GET_HIT(character) <= 0) {
            act("$n suddenly collapses on the ground.",
                TRUE, character, 0, 0, TO_ROOM);
            send_to_char("Your body failed to the magic.\n\r", character);
            raw_kill(character, NULL, 0);
        }
    }
}

struct obj_data* unequip_char(struct char_data* ch, int pos)
{
    int j;
    int was_poisoned = 0;
    struct obj_data* obj;

    was_poisoned = IS_AFFECTED(ch, AFF_POISON);

    assert(pos >= 0 && pos < MAX_WEAR);

    if (!ch->equipment[pos]) {
        mudlog("unequip_char called for zero object.", NRM, 0, 0);
        log("unequip_char called for zero object.");
        return 0;
    }

    obj = ch->equipment[pos];

    ch->equipment[pos] = 0;

    ch->points.encumb -= obj->obj_flags.value[2] * encumb_table[pos];
    ch->specials2.leg_encumb -= obj->obj_flags.value[2] * leg_encumb_table[pos];
    if (encumb_table[pos])
        GET_ENCUMB_WEIGHT(ch) -= GET_OBJ_WEIGHT(obj) * encumb_table[pos];
    else
        GET_ENCUMB_WEIGHT(ch) -= GET_OBJ_WEIGHT(obj) / 2;
    GET_WORN_WEIGHT(ch) -= GET_OBJ_WEIGHT(obj);

    IS_CARRYING_W(ch) -= GET_OBJ_WEIGHT(obj);

    if ((pos == HOLD) && !CAN_WEAR(obj, ITEM_HOLD))
        return obj;

    if (GET_ITEM_TYPE(obj) == ITEM_ARMOR) {
        SET_DODGE(ch) -= obj->obj_flags.value[3];

    } else if (GET_ITEM_TYPE(obj) == ITEM_WEAPON) {
        SET_OB(ch) -= obj->obj_flags.value[0];
        SET_PARRY(ch) -= obj->obj_flags.value[1];

    } else if (GET_ITEM_TYPE(obj) == ITEM_SHIELD) {
        SET_DODGE(ch) -= obj->obj_flags.value[0];
        SET_PARRY(ch) -= obj->obj_flags.value[1];

    } else if (GET_ITEM_TYPE(obj) == ITEM_LIGHT) {
        if ((ch->in_room != NOWHERE) && (obj->obj_flags.value[2] != 0) && (obj->obj_flags.value[3] != 0)) {
            if (obj->obj_flags.value[3] > 0)
                obj->obj_flags.value[3] = 0;
            world[ch->in_room].light--;
        }
    }

    for (j = 0; j < MAX_OBJ_AFFECT; j++)
        affect_modify(ch, obj->affected[j].location,
            obj->affected[j].modifier,
            obj->obj_flags.bitvector, AFFECT_MODIFY_REMOVE, 0);

    affect_total(ch);

    // Special case for poisoned objects.  The wearer should get poison damage
    // when wearing/removing something poisoned.
    if (was_poisoned != 0 && !IS_AFFECTED(ch, AFF_POISON)) {
        extern void raw_kill(struct char_data * ch, char_data * killer, int attacktype);

        damage(ch, ch, 5, SPELL_POISON, 0);

        if (GET_HIT(ch) <= 0) {
            act("$n suddenly collapses on the ground.",
                TRUE, ch, 0, 0, TO_ROOM);
            send_to_char("Your body failed to the magic.\n\r", ch);
            raw_kill(ch, NULL, 0);
        }
    }

    return (obj);
}

int get_number(char** name)
{

    int i;
    char* ppos;
    char number[MAX_INPUT_LENGTH] = "";

    if ((ppos = strchr(*name, '.'))) {
        *ppos++ = '\0';
        strcpy(number, *name);
        strcpy(*name, ppos);

        for (i = 0; *(number + i); i++)
            if (!isdigit(*(number + i)))
                return (0);

        return (atoi(number));
    }

    return (1);
}

/* Search a given list for an object, and return a pointer to that object */
struct obj_data* get_obj_in_list(char* name, struct obj_data* list)
{
    struct obj_data* i;
    int j, number;
    char tmpname[MAX_INPUT_LENGTH];
    char* tmp;

    strcpy(tmpname, name);
    tmp = tmpname;
    if (!(number = get_number(&tmp)))
        return (0);

    for (i = list, j = 1; i && (j <= number); i = i->next_content)
        if (isname(tmp, i->name, 0)) {
            if (j == number)
                return (i);
            j++;
        }

    return (0);
}

/* Search a given list for an object number, and return a ptr to that obj */
struct obj_data* get_obj_in_list_num(int num, struct obj_data* list)
{
    struct obj_data* i;

    for (i = list; i; i = i->next_content)
        if (i->item_number == num)
            return (i);

    return (0);
}

/* Search a given list for a specified vnum, and return a ptr to that obj */
struct obj_data* get_obj_in_list_vnum(int vnum, struct obj_data* list)
{
    struct obj_data* i;

    if (vnum == 0)
        return 0;

    for (i = list; i; i = i->next_content)
        if (((i->item_number >= 0) ? obj_index[i->item_number].virt : 0) == vnum)
            return (i);

    return (0);
}

/* Search a given list for an object number - including containers */
struct obj_data* get_obj_in_list_num_containers(int num, struct obj_data* list)
{

    struct obj_data* i = 0;

    if (!list)
        return 0;

    if (list->contains)
        i = get_obj_in_list_num_containers(num, list->contains);
    if (!i)
        return get_obj_in_list_num(num, list);
    else
        return i;
}

int count_obj_in_list(int num, struct obj_data* list)
{
    struct obj_data* i;
    int n;

    for (n = 0, i = list; i; i = i->next_content)
        if (!num || (i->item_number == num))
            n++;

    return n;
}

/*search the entire world for an object, and return a pointer  */
struct obj_data* get_obj(char* name)
{
    struct obj_data* i;
    int j, number;
    char tmpname[MAX_INPUT_LENGTH];
    char* tmp;

    strcpy(tmpname, name);
    tmp = tmpname;
    if (!(number = get_number(&tmp)))
        return (0);

    for (i = object_list, j = 1; i && (j <= number); i = i->next)
        if (isname(tmp, i->name)) {
            if (j == number)
                return (i);
            j++;
        }

    return (0);
}

/*search the entire world for an object number, and return a pointer  */
struct obj_data* get_obj_num(int nr)
{
    struct obj_data* i;

    for (i = object_list; i; i = i->next)
        if (i->item_number == nr)
            return (i);

    return (0);
}

/* search a room for a char, and return a pointer if found..  */
struct char_data* get_char_room(char* name, int room)
{
    struct char_data* i;
    int j, number;
    char tmpname[MAX_INPUT_LENGTH];
    char* tmp;

    strcpy(tmpname, name);
    tmp = tmpname;
    if (!(number = get_number(&tmp)))
        return (0);

    for (i = world[room].people, j = 1; i && (j <= number); i = i->next_in_room)
        if (isname(tmp, i->player.name)) {
            if (j == number)
                return (i);
            j++;
        }

    return (0);
}

/* search all over the world for a char, and return a pointer if found */
struct char_data* get_char(char* name)
{
    struct char_data* i;
    int j, number;
    char tmpname[MAX_INPUT_LENGTH];
    char* tmp;

    strcpy(tmpname, name);
    tmp = tmpname;
    if (!(number = get_number(&tmp)))
        return (0);

    for (i = character_list, j = 1; i && (j <= number); i = i->next)
        if (isname(tmp, i->player.name)) {
            if (j == number)
                return (i);
            j++;
        }

    return (0);
}

/* search all over the world for a char num, and return a pointer if found */
struct char_data* get_char_num(int nr)
{
    struct char_data* i;

    for (i = character_list; i; i = i->next)
        if (i->nr == nr)
            return (i);

    return (0);
}

/* put an object in a room */
void obj_to_room(struct obj_data* object, int room)
{
    int tmp;
    obj_data* tmpobj;

    if (!object)
        return;

    for (tmpobj = world[room].contents; tmpobj; tmpobj = tmpobj->next_content)
        if (tmpobj == object) {
            sprintf(buf, "obj_to_room: double call for room %d, object %s\n", world[room].number, object->short_description);
            mudlog(buf, NRM, LEVEL_IMPL, TRUE);
            return;
        }
    object->next_content = world[room].contents;
    world[room].contents = object;

    if (GET_ITEM_TYPE(object) == ITEM_LIGHT) {
        if (object->obj_flags.value[2] && object->obj_flags.value[3]) {
            world[room].light++;
        }
    }
    for (tmp = 0, tmpobj = world[room].contents; tmpobj && (tmp < 1000);
         tmpobj = tmpobj->next_content, tmp++)
        ;
    if (tmp >= 1000) {
        mudlog("obj_to_room: infinite loop in room contents.",
            NRM, LEVEL_GOD, TRUE);
        world[room].contents = object;
        object->next_content = 0;
    }
    object->in_room = room;
    object->carried_by = 0;
    //   printf("obj_to_room %d, %p, descr:%s\n",world[room].number,object,object->description);
}

/* Take an object from a room */
void obj_from_room(struct obj_data* object)
{
    struct obj_data* i;

    /* remove object from room */

    if (!object)
        return;

    if (object == world[object->in_room].contents) /* head of list */
        world[object->in_room].contents = object->next_content;

    else /* locate previous element in list */ {
        for (i = world[object->in_room].contents; i && (i->next_content != object); i = i->next_content)
            ;

        i->next_content = object->next_content;
    }

    if (GET_ITEM_TYPE(object) == ITEM_LIGHT) {
        if (object->obj_flags.value[2] && object->obj_flags.value[3]) {
            world[object->in_room].light--;
            if (object->obj_flags.value[3] > 0)
                object->obj_flags.value[3] = 0;
        }
    }
    object->in_room = NOWHERE;
    object->next_content = 0;
}

/* put an object in an object (quaint)  */
void obj_to_obj(obj_data* item, obj_data* container, char change_weight)
{
    if (!item || !container)
        return;

    item->next_content = container->contains;
    container->contains = item;
    item->in_obj = container;

    if (change_weight) {
        obj_data* tmp_obj = NULL;
        for (tmp_obj = item->in_obj; tmp_obj->in_obj; tmp_obj = tmp_obj->in_obj) {
            GET_OBJ_WEIGHT(tmp_obj) += GET_OBJ_WEIGHT(item);
        }

        /* top level object.  Subtract weight from inventory if necessary. */
        GET_OBJ_WEIGHT(tmp_obj) += GET_OBJ_WEIGHT(item);
        if (tmp_obj->carried_by) {
            IS_CARRYING_W(tmp_obj->carried_by) += GET_OBJ_WEIGHT(item);
        }
    }
}

/* remove an object from an object */
void obj_from_obj(obj_data* item)
{
    if (!item)
        return;

    if (item->in_obj) {
        obj_data* tmp;
        obj_data* obj_from = item->in_obj;
        if (item == obj_from->contains) /* head of list */
        {
            obj_from->contains = item->next_content;
        } else {
            for (tmp = obj_from->contains; tmp && (tmp->next_content != item); tmp = tmp->next_content)
                ; /* locate previous */

            if (!tmp) {
                perror("SYSERR: Fatal error in object structures.");
                abort();
            }

            tmp->next_content = item->next_content;
        }

        /* Subtract weight from containers container */
        for (tmp = item->in_obj; tmp->in_obj; tmp = tmp->in_obj) {
            GET_OBJ_WEIGHT(tmp) -= GET_OBJ_WEIGHT(item);
        }

        /* Subtract weight from char that carries the object */
        GET_OBJ_WEIGHT(tmp) -= GET_OBJ_WEIGHT(item);
        if (tmp->carried_by) {
            IS_CARRYING_W(tmp->carried_by) -= GET_OBJ_WEIGHT(item);
        }

        item->in_obj = 0;
        item->next_content = 0;
    } else {
        perror("SYSERR: Trying to object from object when in no object.");
        abort();
    }
}

/* Set all carried_by to point to new owner */
void object_list_new_owner(struct obj_data* list, struct char_data* ch)
{
    if (list) {
        object_list_new_owner(list->contains, ch);
        object_list_new_owner(list->next_content, ch);
        list->carried_by = ch;
    }
}

/* Extract an object from the world */
void extract_obj(struct obj_data* obj)
{
    struct obj_data *temp1, *temp2;

    if (obj->in_room != NOWHERE)
        obj_from_room(obj);
    else if (obj->carried_by)
        obj_from_char(obj);
    else if (obj->in_obj) {
        temp1 = obj->in_obj;
        if (temp1->contains == obj) /* head of list */
            temp1->contains = obj->next_content;
        else {
            for (temp2 = temp1->contains;
                 temp2 && (temp2->next_content != obj);
                 temp2 = temp2->next_content)
                ;

            if (temp2) {
                temp2->next_content = obj->next_content;
            }
        }
    }

    for (; obj->contains; extract_obj(obj->contains))
        ;
    /* leaves nothing ! */

    if (object_list == obj) /* head of list */
        object_list = obj->next;
    else {
        for (temp1 = object_list;
             temp1 && (temp1->next != obj);
             temp1 = temp1->next)
            ;

        if (temp1)
            temp1->next = obj->next;
    }

    if (obj->item_number >= 0)
        (obj_index[obj->item_number].number)--;
    // printf("extracting object %s in room %d\n",obj->name, obj->in_room);
    free_obj(obj);
}

void update_object(struct obj_data* obj, int use)
{

    if (obj->obj_flags.timer > 0)
        obj->obj_flags.timer -= use;
    if (obj->contains)
        update_object(obj->contains, use);
    if (obj->next_content)
        update_object(obj->next_content, use);
}

void update_char_objects(struct char_data* ch)
{

    int i;

    //    for (tmp = 0; tmp < MAX_WEAR; tmp++)
    //    if (ch->equipment[tmp])
    //      if (ch->equipment[tmp]->obj_flags.type_flag == ITEM_LIGHT){
    //        if (ch->equipment[tmp]->obj_flags.value[2] > 0){
    // 	    (ch->equipment[tmp]->obj_flags.value[2])--;
    // 	 if(ch->equipment[tmp]->obj_flags.value[2] == 0){
    // 	   send_to_char("Your light went out.\n\r",ch);
    // 	   recount_light_room(ch->in_room);
    // 	 }
    // 	 else if((ch->equipment[tmp]->obj_flags.value[2] < 3) &&
    // 		 (ch->equipment[tmp]->obj_flags.value[2] > 0))
    // 	   send_to_char("Your light is fading.\n\r",ch);
    //        }
    //      }
    for (i = 0; i < MAX_WEAR; i++)
        if (ch->equipment[i])
            update_object(ch->equipment[i], 2);

    if (ch->carrying)
        update_object(ch->carrying, 1);
}

/* Extract a ch completely from the world, and leave his stuff behind */

ACMD(do_look);
void stop_fighting_him(struct char_data* ch);

void extract_char(struct char_data* ch)
{
    return extract_char(ch, -1);
}

void extract_char(struct char_data* ch, int new_room)
{
    struct obj_data* i;
    struct char_data *k, *k2, *next_char;
    struct descriptor_data* t_desc;
    int l, was_in;

    extern struct char_data* combat_list;
    extern struct char_data* waiting_list;

    if (!IS_NPC(ch) && !ch->desc) {
        for (t_desc = descriptor_list; t_desc; t_desc = t_desc->next)
            if (t_desc->original == ch)
                do_return(t_desc->character, "", 0, 0, 0);
    }

    if (ch->followers || ch->master || ch->group)
        die_follower(ch);

    GET_ENERGY(ch) = 1201;
    GET_MENTAL_DELAY(ch) = 0;
    stop_fighting_him(ch);
    stop_fighting(ch);
    stop_riding(ch);
    abort_delay(ch);

    if (ch->desc) {
        /* Forget snooping */
        if (ch->desc->snoop.snooping)
            ch->desc->snoop.snooping->desc->snoop.snoop_by = 0;

        if (ch->desc->snoop.snoop_by) {
            send_to_char("Your victim is no longer among us.\n\r",
                ch->desc->snoop.snoop_by);
            ch->desc->snoop.snoop_by->desc->snoop.snooping = 0;
        }
        ch->desc->snoop.snooping = ch->desc->snoop.snoop_by = 0;
    }

    if (ch->carrying) {
        /* transfer ch's objects to room */
        if (ch->in_room != NOWHERE) {
            if (world[ch->in_room].contents) /* room nonempty */ {
                /* locate tail of room-contents */
                for (i = world[ch->in_room].contents; i->next_content;
                     i = i->next_content)
                    ;

                /* append ch's stuff to room-contents */
                i->next_content = ch->carrying;
            } else
                world[ch->in_room].contents = ch->carrying;

            /* connect the stuff to the room */
            for (i = ch->carrying; i; i = i->next_content) {
                i->carried_by = 0;
                i->in_room = ch->in_room;
            }
            ch->carrying = 0;
        } else {
            struct obj_data* j;
            for (i = ch->carrying; i; i = j) {
                j = i->next_content;
                extract_obj(i);
            }
        }
    }

    //    while (ch->affected)
    //       affect_remove(ch, ch->affected);

    for (k = combat_list; k; k = next_char) {
        next_char = k->next_fighting;
        if (k->specials.fighting == ch)
            stop_fighting(k);
    }
    for (k2 = 0, k = waiting_list; k; k = k->delay.next) {
        if (k == ch)
            break;
        k2 = k;
    }
    if (!k2)
        waiting_list = ch->delay.next;
    else
        k2->delay.next = ch->delay.next;
    /* Must remove from room before removing the equipment! */
    if (ch->in_room != NOWHERE) {
        was_in = ch->in_room;
        ch->specials2.load_room = world[ch->in_room].number;
        char_from_room(ch);

        /* clear equipment_list */
        for (l = 0; l < MAX_WEAR; l++)
            if (ch->equipment[l])
                obj_to_room(unequip_char(ch, l), was_in);

        recount_light_room(was_in);
    } else {
        was_in = NOWHERE;
        for (l = 0; l < MAX_WEAR; l++)
            if (ch->equipment[l])
                extract_obj(unequip_char(ch, l));
    }
    for (l = 0; l < MAX_WEAR; l++)
        ch->equipment[l] = 0;

    if (IS_NPC(ch) || !(ch->desc) || (!ch->desc->descriptor) || (new_room < 0)) {
        /* pull the char from the list */

        if (ch == character_list)
            character_list = ch->next;
        else {
            for (k = character_list; (k) && (k->next != ch); k = k->next)
                ;
            if (k)
                k->next = ch->next;
            else {
                char log_buf[1024];
                sprintf(log_buf, "SYSERR: Trying to remove %s from character_list. (handler.c, extract_char)%c", GET_NAME(ch), '\0');
                log(log_buf);
                abort();
            }
        }
    }

    if (ch->desc) {
        if (ch->desc->original) {
            do_return(ch, "", 0, 0, 0);
        } else
            save_char(ch, (new_room < 0) ? ((was_in == NOWHERE) ? -1 : world[was_in].number) : new_room, 0);
    }

    if (IS_NPC(ch)) {
        if (ch->nr > -1) { /* if mobile */
            mob_index[ch->nr].number--;
            if (mob_index[ch->nr].number < 0)
                mob_index[ch->nr].number = 0;
        }
        if (GET_LOADLINE(ch)) {
            zone_table[GET_LOADZONE(ch)].cmd[GET_LOADLINE(ch) - 1].existing--;
            if (zone_table[GET_LOADZONE(ch)].cmd[GET_LOADLINE(ch) - 1].existing < 0)
                zone_table[GET_LOADZONE(ch)].cmd[GET_LOADLINE(ch) - 1].existing = 0;
        }

        clear_memory(ch); /* Only NPC's can have memory */
        remove_char_exists(ch->abs_number);
        free_char(ch);
    } else if (ch->desc) {
        if (!ch->desc->descriptor) {
            close_socket(ch->desc);
        } else if (new_room >= 0) {
            while (ch->affected)
                affect_remove(ch, ch->affected);
            char_to_room(ch, new_room);
            ch->specials.was_in_room = NOWHERE;
            send_to_char("Your spirit found a new body to wear.\n\r", ch);
            SET_POS(ch) = POSITION_RESTING;
            utils::set_spirits(ch, utils::get_spirits(ch) / 2);
            do_look(ch, "", 0, 0, 0);
        } else {
            ch->desc->connected = CON_SLCT;
            show_character_menu(ch->desc);
        }
    } else {
        while (ch->affected)
            affect_remove(ch, ch->affected); // Extracted characters' affections were not being removed
        remove_char_exists(ch->abs_number);
        free_char(ch);
    }
}

/* ***********************************************************************
   Here follows high-level versions of some earlier routines, ie functions
   which incorporate the actual player-data.
   *********************************************************************** */

int keyword_matches_char(struct char_data* ch, struct char_data* vict, char* keyword)
{
    int check;

    if (other_side(ch, vict)) {
        check = isname(keyword, pc_race_keywords[GET_RACE(vict)]);
    } else
        check = isname(keyword, vict->player.name);

    return check;
}

struct char_data* get_char_room_vis(struct char_data* ch, char* name, int dark_ok)
{
    struct char_data* i;
    int j, number, check;
    char tmpname[MAX_INPUT_LENGTH];
    char* tmp;

    strcpy(tmpname, name);
    tmp = tmpname;
    if (!(number = get_number(&tmp)))
        return (0);

    j = 1;
    for (i = world[ch->in_room].people; i && (j <= number); i = i->next_in_room) {

        check = keyword_matches_char(ch, i, tmp);
        if (check)
            if (CAN_SEE(ch, i, dark_ok)) {
                if (j == number)
                    return (i);
                j++;
            }
    }
    return (0);
}

struct char_data* get_player_vis(struct char_data* ch, char* name)
{
    struct char_data* i;

    for (i = character_list; i; i = i->next)
        if (!IS_NPC(i) && !str_cmp(i->player.name, name) && CAN_SEE(ch, i))
            return i;

    return 0;
}

struct char_data* get_char_vis(struct char_data* ch, char* name, int dark_ok)
{
    struct char_data* i;
    int j, number, check;
    char tmpname[MAX_INPUT_LENGTH];
    char* tmp;

    /* check location */
    if ((i = get_char_room_vis(ch, name)))
        return (i);

    strcpy(tmpname, name);
    tmp = tmpname;
    if (!(number = get_number(&tmp)))
        return (0);

    for (i = character_list, j = 1; i && (j <= number); i = i->next) {
        if (other_side(ch, i))
            check = isname(tmp, pc_race_keywords[i->player.race]);
        else
            check = isname(tmp, i->player.name);

        if (check)
            if (CAN_SEE(ch, i, dark_ok)) {
                if (j == number)
                    return (i);
                j++;
            }
    }

    return (0);
}

struct obj_data* get_obj_in_list_vis(struct char_data* ch, char* name,
    struct obj_data* list, int num)
{
    struct obj_data* i;
    int j, number;
    char tmpname[MAX_INPUT_LENGTH];
    char* tmp;

    if (num < 9999) {
        for (i = list, j = 1; i && (j < num); i = i->next_content, j++)
            ;
        return (i);
    }

    strcpy(tmpname, name);
    tmp = tmpname;
    if (!(number = get_number(&tmp)))
        return (0);

    for (i = list, j = 1; i && (j <= number); i = i->next_content)
        if (isname(tmp, i->name, 0))
            if (CAN_SEE_OBJ(ch, i)) {
                if (j == number)
                    return (i);
                j++;
            }
    return (0);
}

/*search the entire world for an object, and return a pointer  */
struct obj_data* get_obj_vis(struct char_data* ch, char* name)
{
    struct obj_data* i;
    int j, number;
    char tmpname[MAX_INPUT_LENGTH];
    char* tmp;

    /* scan items carried */
    if ((i = get_obj_in_list_vis(ch, name, ch->carrying, 9999)))
        return (i);

    /* scan room */
    if ((i = get_obj_in_list_vis(ch, name, world[ch->in_room].contents, 9999)))
        return (i);

    strcpy(tmpname, name);
    tmp = tmpname;
    if (!(number = get_number(&tmp)))
        return (0);

    /* ok.. no luck yet. scan the entire obj list   */
    for (i = object_list, j = 1; i && (j <= number); i = i->next)
        if (isname(tmp, i->name, 0))
            if (CAN_SEE_OBJ(ch, i)) {
                if (j == number)
                    return (i);
                j++;
            }
    return (0);
}

struct obj_data* get_object_in_equip_vis(struct char_data* ch,
    char* arg, struct obj_data* equipment[], int* j)
{

    for ((*j) = 0; (*j) < MAX_WEAR; (*j)++)
        if (equipment[(*j)])
            if (CAN_SEE_OBJ(ch, equipment[(*j)]))
                if (isname(arg, equipment[(*j)]->name))
                    return (equipment[(*j)]);

    return (0);
}

struct obj_data* create_money(int amount)
{
    char buf[200];

    struct obj_data* obj;
    struct extra_descr_data* new_descr;

    if (amount <= 0) {
        log("SYSERR: Try to create negative or 0 money.");
        exit(1);
    }

    CREATE(obj, struct obj_data, 1);
    CREATE(new_descr, struct extra_descr_data, 1);
    clear_object(obj);
    if (amount == 1) {
        obj->name = str_dup("coin money copper");
        obj->short_description = str_dup("a coin");
        obj->description = str_dup("One miserable copper coin is lying here.");
        new_descr->keyword = str_dup("coin gold");
        new_descr->description = str_dup("It's just one miserable little copper coin.");
    } else {
        obj->name = str_dup("coins money gold");
        if (amount <= 100) {
            obj->short_description = str_dup("a small pile of coins");
            obj->description = str_dup("A small pile of coins is lying here.");
        } else if (amount <= 1000) {
            obj->short_description = str_dup("a pile of coins");
            obj->description = str_dup("A pile of coins is lying here.");
        } else if (amount <= 25000) {
            obj->short_description = str_dup("a large heap of coins");
            obj->description = str_dup("A large heap of coins is lying here.");
        } else if (amount <= 500000) {
            obj->short_description = str_dup("a huge mound of coins");
            obj->description = str_dup("A huge mound of coins is lying here.");
        } else {
            obj->short_description = str_dup("an enormous mountain of coins");
            obj->description = str_dup("An enormous mountain of money is lying here.");
        }

        new_descr->keyword = str_dup("coins money gold");
        if (amount < COPP_IN_SILV) {
            sprintf(buf, "There are %d copper coins.", amount);
            new_descr->description = str_dup(buf);
        } else if (amount < COPP_IN_GOLD) {
            sprintf(buf, "There are about %d silver coins.", (amount / COPP_IN_SILV));
            new_descr->description = str_dup(buf);
        } else if (amount < 10 * COPP_IN_GOLD) {
            sprintf(buf, "It looks to be about %d gold coins.", (amount / COPP_IN_GOLD));
            new_descr->description = str_dup(buf);
        } else if (amount < 100 * COPP_IN_GOLD) {
            sprintf(buf, "You guess there are, maybe, %d gold coins.",
                10 * ((amount / 10 / COPP_IN_GOLD)));
            new_descr->description = str_dup(buf);
        } else
            new_descr->description = str_dup("There is a lot of gold.");
    }

    new_descr->next = 0;
    obj->ex_description = new_descr;

    obj->obj_flags.type_flag = ITEM_MONEY;
    obj->obj_flags.wear_flags = ITEM_TAKE;
    obj->obj_flags.value[0] = amount;
    obj->obj_flags.cost = amount;
    obj->item_number = -1;

    obj->next = object_list;
    object_list = obj;

    return (obj);
}

/* Generic Find, designed to find any object/character                    */
/* Calling :                                                              */
/*  *arg     is the sting containing the string to be searched for.       */
/*           This string doesn't have to be a single word, the routine    */
/*           extracts the next word itself.                               */
/*  bitv..   All those bits that you want to "search through".            */
/*           Bit found will be result of the function                     */
/*  *ch      This is the person that is trying to "find"                  */
/*  **tar_ch Will be NULL if no character was found, otherwise points     */
/* **tar_obj Will be NULL if no object was found, otherwise points        */
/*                                                                        */
/* The routine returns a pointer to the next word in *arg (just like the  */
/* one_argument routine).                                                 */

int generic_find(char* arg, int bitvector, struct char_data* ch,
    struct char_data** tar_ch, struct obj_data** tar_obj)
{
    char* ignore[] = {
        "the",
        "in",
        "on",
        "at",
        "\n"
    };

    int i, namelen = 0;
    char name[256];
    char found, tmpfound;

    found = FALSE;

    /* Eliminate spaces and "ignore" words */
    while (*arg && !found) {

        for (; *arg == ' '; arg++)
            ;

        for (i = 0; (name[i] = *(arg + i)) && (name[i] != ' '); i++)
            ;
        name[i] = 0;
        namelen = i;
        arg += i;
        if (search_block(name, ignore, TRUE) > -1)
            found = TRUE;
    }

    if (!name[0])
        return (0);

    *tar_ch = 0;
    *tar_obj = 0;

    if (IS_SET(bitvector, FIND_CHAR_ROOM)) { /* Find person in room */
        if ((*tar_ch = get_char_room_vis(ch, name))) {
            return (FIND_CHAR_ROOM);
        }
    }

    if (IS_SET(bitvector, FIND_CHAR_WORLD)) {
        if ((*tar_ch = get_char_vis(ch, name))) {
            return (FIND_CHAR_WORLD);
        }
    }

    if (IS_SET(bitvector, FIND_OBJ_EQUIP)) {
        for (found = FALSE, i = 0; i < MAX_WEAR && !found; i++) {
            if (namelen > 2)
                tmpfound = (ch->equipment[i] && strn_cmp(name, ch->equipment[i]->name, namelen) == 0);
            else
                tmpfound = (ch->equipment[i] && str_cmp(name, ch->equipment[i]->name) == 0);
            if (tmpfound) {
                *tar_obj = ch->equipment[i];
                found = TRUE;
            }
        }
        if (found) {
            return (FIND_OBJ_EQUIP);
        }
    }

    if (IS_SET(bitvector, FIND_OBJ_INV)) {
        //   if ((*tar_obj = get_obj_in_list_vis(ch, name, ch->carrying,9999))) {
        if ((*tar_obj = get_obj_in_list(name, ch->carrying))) {
            return (FIND_OBJ_INV);
        }
    }

    if (IS_SET(bitvector, FIND_OBJ_ROOM)) {
        if ((*tar_obj = get_obj_in_list_vis(ch, name, world[ch->in_room].contents, 9999))) {
            return (FIND_OBJ_ROOM);
        }
    }

    if (IS_SET(bitvector, FIND_OBJ_WORLD)) {
        if ((*tar_obj = get_obj_vis(ch, name))) {
            return (FIND_OBJ_WORLD);
        }
    }

    return (0);
}

/* a function to scan for "all" or "all.x" */
int find_all_dots(char* arg)
{
    if (!strcmp(arg, "all"))
        return FIND_ALL;
    else if (!strncmp(arg, "all.", 4)) {
        strcpy(arg, arg + 4);
        return FIND_ALLDOT;
    } else
        return FIND_INDIV;
}

char* money_message(int sum, int mode)
{
    static char moneystr[100];
    int g, s, c;

    *moneystr = 0;

    if (sum < 0) {
        sprintf(moneystr, "%d copper coins", sum);
        return moneystr;
    }

    g = sum / COPP_IN_GOLD;
    c = sum % COPP_IN_GOLD;
    s = c / COPP_IN_SILV;
    c = c % COPP_IN_SILV;

    if (g)
        sprintf(moneystr, "%d gold", g);
    if (g && c && s)
        strcat(moneystr, ", ");
    if (!c && s && g)
        strcat(moneystr, " and ");
    if (s)
        sprintf(moneystr + strlen(moneystr), "%d silver", s);
    if ((g || s) && c)
        strcat(moneystr, " and ");
    if (c || (!sum))
        sprintf(moneystr + strlen(moneystr), "%d copper", c);

    if (mode)
        sprintf(moneystr + strlen(moneystr), " coin%s",
            ((g == 1) && (s == 1)) || c == 1 ? "" : "s");

    return moneystr;
}

int char_exists(int num)
{
    return (char_control_array[num / 8] & (1 << (num % 8)));
}
void set_char_exists(int num)
{
    char_control_array[num / 8] |= (1 << (num % 8));
}
void remove_char_exists(int num)
{
    char_control_array[num / 8] &= ~(1 << (num % 8));
}
int register_npc_char(struct char_data* mob)
{
    int i, flag;

    if (!mob) {
        log("register_char: zero char passed.");
        return -1;
    }
    flag = 0;
    for (i = last_control_set + 1; i < MAX_CHARACTERS; i++)
        if (!char_exists(i))
            break;
    if (i == MAX_CHARACTERS) {
        flag = 1;
        for (i = 0; i <= last_control_set; i++)
            if (!char_exists(i))
                break;
    }
    if (flag && (i > last_control_set)) {
        log("register_char: MUD IS OVERFLOWED.");
        exit(0);
    }
    set_char_exists(i);
    mob->abs_number = i;
    last_control_set = i;

    return i;
}

int register_pc_char(struct char_data* ch)
{

    return register_npc_char(ch);
}

int can_swim(struct char_data* ch)
{

    struct obj_data* tmpobj;
    int tmp;

    if (IS_SHADOW(ch))
        return TRUE;

    if (!IS_NPC(ch) && !PRF_FLAGGED(ch, PRF_SWIM))
        return FALSE;

    if (IS_NPC(ch))
        if (IS_SET(ch->specials2.act, MOB_CAN_SWIM) || IS_AFFECTED(ch, AFF_FLYING) || IS_SHADOW(ch))
            return TRUE;

    if (IS_AFFECTED(ch, AFF_SWIM))
        return TRUE;

    if (!IS_NPC(ch) && GET_SKILL(ch, SKILL_SWIM) > 0)
        return TRUE;

    for (tmpobj = ch->carrying; tmpobj; tmpobj = tmpobj->next_content)
        if (tmpobj->obj_flags.type_flag == ITEM_BOAT)
            return TRUE;

    for (tmp = 0; tmp < MAX_WEAR; tmp++)
        if (ch->equipment[tmp])
            if ((ch->equipment[tmp])->obj_flags.type_flag == ITEM_BOAT)
                return TRUE;

    return FALSE;
}

void stop_riding(struct char_data* ch)
{
    struct char_data *tmpch, *mount;

    while (IS_RIDDEN(ch))
        stop_riding(ch->mount_data.rider);

    tmpch = 0;

    if (!IS_RIDING(ch))
        return;

    if (char_exists(ch->mount_data.mount_number)) {
        mount = ch->mount_data.mount;
        act("You stop riding $N.", FALSE, ch, 0, mount, TO_CHAR);
        act("$n stops riding $N.", FALSE, ch, 0, mount, TO_NOTVICT);
        act("$n stops riding you.", FALSE, ch, 0, mount, TO_VICT);

        if ((mount)->mount_data.rider == ch) {
            tmpch = ch;
            (mount)->mount_data.rider = ch->mount_data.next_rider;
            (mount)->mount_data.rider_number = ch->mount_data.next_rider_number;
        } else {
            for (tmpch = (mount)->mount_data.rider;
                 tmpch->mount_data.next_rider;
                 tmpch = tmpch->mount_data.next_rider) {
                if (tmpch->mount_data.next_rider == ch) {
                    tmpch->mount_data.next_rider = ch->mount_data.next_rider;
                    tmpch->mount_data.next_rider_number = ch->mount_data.next_rider_number;
                    break;
                }
            }
        }
        if (tmpch)
            IS_CARRYING_W(mount) -= GET_WEIGHT(ch) + IS_CARRYING_W(ch);
    }
    ch->mount_data.mount = 0;
    ch->mount_data.next_rider = 0;
    return;
}

void stop_riding_all(char_data* mount)
{
    char_data* tmpch;

    for (tmpch = character_list; tmpch; tmpch = tmpch->next)
        if (tmpch->mount_data.mount == mount)
            stop_riding(tmpch);
}

void recalc_zone_power()
{
    int tmp;
    char_data* tmpch;

    for (tmp = 0; tmp <= top_of_zone_table; tmp++) {
        // nature_power is set from the zone files.
        zone_table[tmp].white_power = 0;
        zone_table[tmp].dark_power = 0;
        zone_table[tmp].magi_power = 0;
    }

    for (tmpch = character_list; tmpch; tmpch = tmpch->next)
        if (!IS_NPC(tmpch) && (tmpch->in_room != NOWHERE)) {
            tmp = char_power(GET_LEVEL(tmpch));
            if (RACE_GOOD(tmpch))
                zone_table[world[tmpch->in_room].zone].white_power += tmp;
            else if (RACE_EVIL(tmpch))
                zone_table[world[tmpch->in_room].zone].dark_power += tmp;
            else if (RACE_MAGI(tmpch))
                zone_table[world[tmpch->in_room].zone].magi_power += tmp;
        }
}

/*
 * Indicates what side of the race war has the most influence
 * over this zone; returns -1 for dominating evil, 1 for domi-
 * nating good, 0 for neither.
 */
int report_zone_power(struct char_data* ch)
{
    struct zone_data* z;

    z = &zone_table[world[ch->in_room].zone];

    if (RACE_GOOD(ch)) {
        if (((z->dark_power > char_power(z->level) * 3 / 2) && (z->dark_power > z->white_power * 3 / 2)))
            return -1;
        if (((z->magi_power > char_power(z->level) * 3 / 2) && (z->magi_power > z->white_power * 3 / 2)))
            return -1;
    }

    else if (RACE_EVIL(ch)) {
        if (((z->white_power > char_power(z->level) * 4) && (z->white_power > z->dark_power * 4)))
            return 1;
        if (((z->magi_power > char_power(z->level) * 4) && (z->magi_power > z->dark_power * 4)))
            return -1;
    }

    else if (RACE_MAGI(ch)) {
        if (((z->white_power > char_power(z->level) * 4) && (z->white_power > z->magi_power * 4)))
            return 1;
        if (((z->dark_power > char_power(z->level) * 4) && (z->dark_power > z->magi_power * 4)))
            return -1;
    }

    return 0;
}
