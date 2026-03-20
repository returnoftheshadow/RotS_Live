/** ************************************************************************
 *   File: spec_procs.c                                  Part of CircleMUD *
 *  Usage: implementation of special procedures for mobiles/objects/rooms  *
 *                                                                         *
 *  All rights reserved.  See license.doc for complete information.        *
 *                                                                         *
 *  Copyright (C) 1993 by the Trustees of the Johns Hopkins University     *
 *  CircleMUD is based on DikuMUD, Copyright (C) 1990, 1991.               *
 ************************************************************************ */

#include "platdef.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

#include "comm.h"
#include "db.h"
#include "handler.h"
#include "interpre.h"
#include "limits.h"
#include "profs.h"
#include "spells.h"
#include "structs.h"
#include "utils.h"

#include "char_utils.h"

/*   external vars  */
extern int get_number(char** name);
extern int num_of_ferries;
extern int num_of_captains;
extern char guildmaster_number;
extern char* dirs[];
extern void raw_kill(char_data* ch, char_data* killer, int attacktype);
extern int mark_calculate_wait(const char_data* ch);
extern int shoot_calculate_wait(const char_data* archer);
extern byte language_number;
extern byte language_skills[];
extern struct char_data* character_list;
extern struct char_data* combat_list;
extern struct char_data* waiting_list; /* in db.cpp */
extern struct command_info cmd_info[];
extern struct descriptor_data* descriptor_list;
extern struct index_data* mob_index;
extern struct index_data* obj_index;
extern struct obj_data* object_list;
extern struct room_data world;
extern struct skill_data skills[MAX_SKILLS];
extern struct time_info_data time_info;
extern skill_teach_data guildmasters[MAX_SKILLS];
extern ferry_boat_type ferry_boat_data[];
extern ferry_captain_type ferry_captain_data[];
int find_first_step(int, int);

ACMD(do_ambush);
ACMD(do_hide);
ACMD(do_say);
ACMD(do_open);
ACMD(do_close);
ACMD(do_lock);
ACMD(do_unlock);
ACMD(do_look);
ACMD(do_stand);
ACMD(do_wake);
ACMD(do_move);
ACMD(do_drop);
ACMD(do_hit);
ACMD(do_cast);
ACMD(do_gen_com);
ACMD(do_tell);
ACMD(do_pull);
ACMD(do_wear);
ACMD(do_kick);
ACMD(do_stat);

struct social_type {
    char* cmd;
    int next_line;
};

char* how_good(int percent)
{

    static char buf[256];

    if (percent == 0)
        strcpy(buf, " (not learned)");
    else if (percent <= 10)
        strcpy(buf, " (awful)");
    else if (percent <= 20)
        strcpy(buf, " (bad)");
    else if (percent <= 40)
        strcpy(buf, " (poor)");
    else if (percent <= 55)
        strcpy(buf, " (average)");
    else if (percent <= 70)
        strcpy(buf, " (fair)");
    else if (percent <= 80)
        strcpy(buf, " (good)");
    else if (percent <= 85)
        strcpy(buf, " (very good)");
    else if (percent <= 99)
        strcpy(buf, " (Superb)");
    else
        strcpy(buf, " (Mastered)");

    return (buf);
}

/*
 * Procedures for skills, guilds
 * difficulty: pracs for perfection * 10.
 * pracs_used: pracs used * 100000 with adjustments
 */

void recalc_skills(struct char_data* ch)
{

    int skill_no, pracs_used, tmps, tmp, difficulty, skill_level;
    if (!ch->knowledge || !ch->skills)
        return;

    for (skill_no = 1; skill_no < MAX_SKILLS; skill_no++) {
        pracs_used = ch->skills[skill_no] * 20;

        if (!skills[skill_no].learn_diff)
            skills[skill_no].learn_diff = 10;
        skill_level = skills[skill_no].level;

        /* 3 pracs in weapon mastery is same as 1 prac in all weapons */
        if ((skill_no >= 0) && (skill_no < 10) && (skill_no != 8) && (skill_no != 7))
            pracs_used += ch->skills[10] * 20 * 3 / 8;

        difficulty = skills[skill_no].learn_diff;

        /* pracs /= coof. - (lvl/30)*(1-coof) (where coof 0 to 1) */
        if (skills[skill_no].type != PROF_WARRIOR) {
            tmps = GET_PROF_COOF((int)skills[skill_no].type, ch) - skill_level * (1000 - GET_PROF_COOF((int)skills[skill_no].type, ch)) / 30;

            if (skill_level < 20)
                tmps = tmps * (80 + skill_level) / 100 + 200 - skills[(int)skill_no].level * 10;

            tmps = MIN(1000, tmps);

            pracs_used = pracs_used * tmps * 10;
        } else
            pracs_used = pracs_used * 10000;

        // pracs used * 100000 with adjustment
        tmps = 1000 - pracs_used / (difficulty * 100);
        ch->knowledge[skill_no] = (10000 - tmps * tmps / 100) / 99;
        if (tmps < 0)
            ch->knowledge[skill_no] = 100;
    }

    switch (GET_RACE(ch)) {
    case RACE_GOD:
        tmp = LANG_BASIC;
        break;
    case RACE_HUMAN:
    case RACE_DWARF:
    case RACE_WOOD:
    case RACE_HOBBIT:
    case RACE_HIGH:
        tmp = LANG_HUMAN;
        break;
    case RACE_BEORNING:
        tmp = LANG_ANIMAL;
        break;
    case RACE_URUK:
    case RACE_HARAD:
    case RACE_ORC:
    case RACE_HARADRIM:
    case RACE_OLOGHAI:
    case RACE_MAGUS:
        tmp = LANG_ORC;
        break;
    case RACE_EASTERLING:
        tmp = LANG_BASIC;
        break;
    default:
        tmp = LANG_BASIC;
        break;
    }

    // Set the spoken language.
    ch->player.language = tmp;

    if (tmp != LANG_BASIC)
        SET_KNOWLEDGE(ch, tmp, 100);

    if (GET_RACE(ch) == RACE_GOD)
        for (tmp = 0; tmp < language_number; tmp++)
            SET_KNOWLEDGE(ch, language_skills[tmp], 100);

    if (!IS_NPC(ch) && (GET_RACE(ch) == RACE_MAGUS) && (GET_RAW_KNOWLEDGE(ch, SPELL_BLINK) == 0))
        SET_KNOWLEDGE(ch, SPELL_BLINK, 10);

    if (!IS_NPC(ch) && (GET_RACE(ch) == RACE_BEORNING) && (GET_RAW_KNOWLEDGE(ch, SKILL_NATURAL_ATTACK) == 0)) {
        SET_KNOWLEDGE(ch, SKILL_NATURAL_ATTACK, 10);
    }
}

SPECIAL(guild)
{

    int tmp, prog, len, request, level;
    char str[255];

    if (IS_NPC(ch) || ((cmd != CMD_PRACTICE) && (cmd != CMD_PRACTISE)))
        return (FALSE);

    if (GET_POS(ch) < POSITION_STANDING)
        return FALSE;

    if (!ch->skills || !ch->knowledge) {
        send_to_char("You are dumb as a mob. Go away.\n\r", ch);
        return TRUE;
    }

    if (!WILL_TEACH(host, ch)) {
        do_say(host, "Go away, I won't teach you anything!", 0, 0, 0);
        return TRUE;
    }

    if (IS_SHADOW(ch)) {
        do_say(host, "Ugh!  I'm not teaching YOU!", 0, 0, 0);
        return TRUE;
    }

    if (!RP_RACE_CHECK(host, ch)) {
        do_say(host, "Sorry, I can't teach you.", 0, 0, 0);
        return TRUE;
    }

    prog = host->specials.store_prog_number - 1;
    if ((prog < 0) || (prog >= guildmaster_number)) {
        send_to_char("Alas, I am just posing for guildmaster! Come back later.\n\r", ch);
        return TRUE;
    }
    for (; *arg == ' '; arg++)
        ;
    if (!*arg) {
        act(guildmasters[prog].list_message, FALSE, host, 0, ch, TO_VICT);
        sprintf(str, "You have %d practice sessions left.\n\r", ch->specials2.spells_to_learn);
        send_to_char(str, ch);
        for (tmp = 0; tmp < MAX_SKILLS; tmp++) {
            if (guildmasters[prog].knowledge[tmp] > 0) {
                level = GET_PROF_LEVEL((int)skills[(int)tmp].type, ch);

                if ((skills[tmp].level <= level) && (!IS_SET(skills[tmp].learn_type, LEARN_SPEC) || (GET_SPEC(ch) == skills[tmp].skill_spec))) {

                    sprintf(str, "%-25s %3d%%     Taught to: %-12s\n\r", skills[tmp].name,
                        ch->knowledge[tmp], how_good(guildmasters[prog].knowledge[tmp]));
                    send_to_char(str, ch);
                }
            }
        }
    } else {
        len = strlen(arg);
        for (request = 0; request < MAX_SKILLS; request++)
            if (!strncmp(arg, skills[request].name, len))
                break;

        if (request == MAX_SKILLS) {
            act("There is no such skill as you seek.", FALSE, host, 0, ch, TO_VICT);
            return TRUE;
        }

        if (guildmasters[prog].knowledge[request] <= 0) {
            act("$n tells you 'I know nothing about it.'", FALSE, host, 0, ch, TO_VICT);
            return TRUE;
        }
        if (ch->knowledge[request] >= 100) {
            act(guildmasters[prog].learned_message, FALSE, host, 0, ch, TO_VICT);
            return TRUE;
        }
        if (guildmasters[prog].knowledge[request] <= ch->knowledge[request]) {
            act(guildmasters[prog].limit_message, FALSE, host, 0, ch, TO_VICT);
            return TRUE;
        }
        if (ch->skills[request] > 50) {
            act("You studied this for too long. Enough for you.\n\r", FALSE, host, 0, ch, TO_VICT);
            return TRUE;
        }
        if (ch->specials2.spells_to_learn <= 0) {
            act("You have no practice sessions left now.\n\r", FALSE, host, 0, ch, TO_VICT);
            return TRUE;
        }

        if (IS_SET(skills[request].learn_type, LEARN_SPEC) && !(GET_SPEC(ch) == skills[request].skill_spec)) {
            send_to_char("You are not dedicated enough for this.\n\r", ch);
            return TRUE;
        }

        level = GET_PROF_LEVEL((int)skills[request].type, ch);

        if (skills[request].level > level) {
            send_to_char("You are not experienced enough for this.\n\r", ch);
            return TRUE;
        }
        ch->skills[request]++;
        ch->specials2.spells_to_learn--;
        recalc_skills(ch);
        recalc_abilities(ch);

        act(guildmasters[prog].practice_message, FALSE, host, 0, ch, TO_NOTVICT);
        act(guildmasters[prog].practice_msg_to_char, FALSE, host, 0, ch, TO_VICT);

        if (ch->knowledge[request] > guildmasters[prog].knowledge[request]) {
            ch->knowledge[request] = guildmasters[prog].knowledge[request];
            act(guildmasters[prog].limit_message, FALSE, host, 0, ch, TO_VICT);
            return TRUE;
        }
    }
    return TRUE;
}

ACMD(do_practice)
{
    int tmp;
    char str[255], str2[30];

    sprintf(str, "You have %d practice sessions left\n\r", ch->specials2.spells_to_learn);
    send_to_char(str, ch);
    if (!ch->skills || !ch->knowledge) {
        send_to_char("But you don't have skill memory anyway.\n\r", ch);
        return;
    }
    for (tmp = 0; tmp < MAX_SKILLS; tmp++)
        if (GET_RAW_SKILL(ch, tmp) > 0) {
            str2[0] = 0;
            if (skills[tmp].type == PROF_MAGE) {
                auto casting_time = CASTING_TIME(ch, tmp);
                auto casting = GET_CASTING(ch);
                auto mana_cost = USE_MANA(ch, tmp);

                if (casting == CASTING_FAST) {
                    casting_time = std::max(1, (casting_time + 1) / 2);
                    mana_cost = mana_cost * 3 / 2;
                } else if (casting == CASTING_SLOW) {
                    casting_time = std::max(1, (casting_time * 3 + 1) / 2);
                    mana_cost = (mana_cost * 1 + 1) / 2;
                }

                sprintf(str2, "(%3d time,   %3d stamina)", casting_time, mana_cost);
            } else if (skills[tmp].type == PROF_CLERIC) {
                sprintf(str2, "(%3d time, %3d spirit)", CASTING_TIME(ch, tmp), USE_SPIRIT(ch, tmp));
            } else if (tmp == SKILL_BEND_TIME) {
                sprintf(str2, "(%3d time,   %3d stamina)", skills[tmp].beats, ch->abilities.mana);
            } else if (tmp == SKILL_MARK) {
                sprintf(str2, "(%3d time,   %3d stamina)", mark_calculate_wait(ch),
                    skills[tmp].min_usesmana);
            } else if (tmp == SKILL_ARCHERY) {
                sprintf(str2, "(%3d time)", shoot_calculate_wait(ch));
            } else if (skills[tmp].type == PROF_RANGER && skills[tmp].min_usesmana > 10) {
                sprintf(str2, "(%3d time,   %3d stamina)", skills[tmp].beats,
                    skills[tmp].min_usesmana);
            } else if (CASTING_TIME(ch, tmp)) {
                sprintf(str2, "(%3d time)", skills[tmp].beats);
            } else {
                strcpy(str2, "");
            }

            sprintf(str, "%-25s %-12s %s\n\r", skills[tmp].name, how_good(ch->knowledge[tmp]),
                str2);
            send_to_char(str, ch);
        }
}

ACMD(do_pracreset)
{
    int tmp;
    char* arg;
    struct char_data* vict;

    if (wtl && (wtl->targ1.type == TARGET_CHAR) && char_exists(wtl->targ1.ch_num))
        vict = wtl->targ1.ptr.ch;
    else {
        if (!*argument) {
            send_to_char("Whom do you want to reset?\n\r", ch);
            return;
        }
        for (arg = argument; (*arg <= ' ') && (*arg != 0); arg++)
            ;

        vict = get_char_vis(ch, arg);
    }
    if (!vict) {
        send_to_char("No such person.\n\r", ch);
        return;
    }
    if (IS_NPC(vict) || !vict->knowledge || !vict->skills) {
        send_to_char("This person cannot learn anything, don't bother yourself.\n\r", ch);
        return;
    }
    for (tmp = 0; tmp < MAX_SKILLS; tmp++) {
        vict->knowledge[tmp] = 0;
        vict->skills[tmp] = 0;
        vict->specials2.spells_to_learn = GET_LEVEL(vict) * PRACS_PER_LEVEL + GET_LEVEL(vict) * GET_LEA(vict) / LEA_PRAC_FACTOR + 10;
    }

    utils::set_specialization(*vict, game_types::PS_None);

    SET_SHOOTING(vict, SHOOTING_NORMAL);
    utils::set_casting(*vict, CASTING_NORMAL);

    recalc_skills(vict);
    send_to_char("You reset his/her learning abilities.\n\r", ch);
    act("$n waves $s hand over your brow and you feel yourself a total rookie.\n\r"
        "Your practices have been reset",
        FALSE, ch, 0, vict, TO_VICT);
}

SPECIAL(pet_shops)
{
    char buf[MAX_STRING_LENGTH], pet_name[256];
    int pet_room;
    struct char_data* pet;

    pet_room = ch->in_room + 1;

    if (cmd == CMD_LIST) { /* List */
        send_to_char("Available pets are:\n\r", ch);
        for (pet = world[pet_room].people; pet; pet = pet->next_in_room) {
            sprintf(buf, "%-20s - %s\n\r", pet->player.short_descr,
                money_message(3 * GET_EXP(pet)));
            send_to_char(buf, ch);
        }
        return (TRUE);
    } else if (cmd == CMD_BUY) { /* Buy */

        arg = one_argument(arg, buf);
        arg = one_argument(arg, pet_name);
        /* Pet_Name is for later use when I feel like it */

        if (!(pet = get_char_room(buf, pet_room))) {
            send_to_char("There is no such pet!\n\r", ch);
            return (TRUE);
        }

        if (GET_GOLD(ch) < (GET_EXP(pet) * 3)) {
            send_to_char("You don't have enough gold!\n\r", ch);
            return (TRUE);
        }

        GET_GOLD(ch) -= GET_EXP(pet) * 3;

        pet = read_mobile(pet->nr, REAL);
        GET_EXP(pet) = 0;
        SET_BIT(MOB_FLAGS(pet), MOB_PET);

        char_to_room(pet, ch->in_room);
        add_follower(pet, ch, FOLLOW_MOVE);

        /* Be certain that pet's can't get/carry/use/weild/wear items */
        IS_CARRYING_W(pet) = 1000;
        IS_CARRYING_N(pet) = 100;

        send_to_char("May you enjoy your pet.\n\r", ch);
        act("$n buys $N as a pet.", FALSE, ch, 0, pet, TO_ROOM);

        return (TRUE);
    }

    return (FALSE);
}

/*
 * Any room with the kit_room spec prog will allow players who
 * use 'ask' to receive a kit.  The kit_eq array holds all items
 * that belong to every kit possible.  Players will always receive
 * a subset of kit_eq.  Each item in kit_eq is checked, and if the
 * character requesting the eq meets the following three require-
 * ments, that piece of eq is given to the player:
 *   - The player's level is less than or equal to the level value
 *     set in the kit_eq structure.  Note that darkies have a malus
 *     when it comes to determining their level--we treat them as
 *     being a higher level than they actually are, so that they
 *     get less eq as they level faster than whities get less eq.
 *   - The player's race, shifted into a bitvector, is set in the
 *     'race' bitvector associated with the item.
 *   - The player's primary class (the highest coef, or in W-R-T-M
 *     order if a tie must be broken), shifted into a bitvector, is
 *     in the 'class' bitvector associated with the item.
 */
#define KIT_LOWBIE 10
#define KIT_MIDBIE 20
#define KIT_LEGEND 30
#define KIT_ALWAYS LEVEL_IMPL

#define KIT_HUM (1 << RACE_HUMAN)
#define KIT_DWA (1 << RACE_DWARF)
#define KIT_WELF (1 << RACE_WOOD)
#define KIT_HOB (1 << RACE_HOBBIT)
#define KIT_HIGH (1 << RACE_HIGH)
#define KIT_URUK (1 << RACE_URUK)
#define KIT_HARAD (1 << RACE_HARAD)
#define KIT_ORC (1 << RACE_ORC)
#define KIT_EAST (1 << RACE_EASTERLING)
#define KIT_LHUTH (1 << RACE_MAGUS)

#define KIT_WHITIE (KIT_WELF | KIT_HUM | KIT_HOB | KIT_DWA | KIT_HIGH)
#define KIT_DARKIE (KIT_URUK | KIT_ORC)
#define KIT_THIRD (KIT_LHUTH)
#define KIT_RACEALL (KIT_WHITIE | KIT_DARKIE | KIT_THIRD)

#define KIT_WARRIOR (1 << PROF_WARRIOR)
#define KIT_RANGER (1 << PROF_RANGER)
#define KIT_MYSTIC (1 << PROF_CLERIC)
#define KIT_MAGE (1 << PROF_MAGE)
#define KIT_CLASSALL (KIT_WARRIOR | KIT_RANGER | KIT_MYSTIC | KIT_MAGE)

/* Whether a character gets absorb eq or not */
#define KIT_ARMOR (KIT_WARRIOR)
#define KIT_NOARMOR (KIT_RANGER | KIT_MYSTIC | KIT_MAGE)

struct kit_item {
    int vnum; /* The virtual number of the object to be in the kit */
    int number; /* How many of this item should be loaded */
    int level; /* The highest level that this object will load for */
    int race; /* A bitvector of races this will load for */
    int coef; /* A bitvector of classes this will load for */
};

struct kit_item kit_eq[] = {
    { 6500, 1, KIT_LOWBIE, KIT_RACEALL, KIT_CLASSALL }, /* small wooden shield */
    { 6023, 1, KIT_MIDBIE, KIT_RACEALL, KIT_CLASSALL }, /* leather belt */

    { 7001, 2, KIT_ALWAYS, KIT_WHITIE, KIT_CLASSALL }, /* torches */
    { 7138, 1, KIT_LEGEND, KIT_WHITIE, KIT_CLASSALL }, /* water skin */
    { 7700, 1, KIT_LOWBIE, KIT_WHITIE, KIT_CLASSALL }, /* large sack */
    { 7240, 2, KIT_LEGEND, KIT_WHITIE, KIT_CLASSALL }, /* cram */
    { 7635, 1, KIT_LOWBIE, KIT_WHITIE, KIT_CLASSALL }, /* MB map */
    { 7622, 1, KIT_LOWBIE, KIT_WHITIE, KIT_CLASSALL }, /* LT map */
    { 7602, 1, KIT_LOWBIE, KIT_WHITIE, KIT_CLASSALL }, /* Vinyanost Map */
    { 6309, 1, KIT_LOWBIE, KIT_WHITIE, KIT_CLASSALL }, /* wool cloak */
    { 6013, 1, KIT_LOWBIE, KIT_WHITIE, KIT_CLASSALL }, /* leather boots */
    { 6009, 1, KIT_LOWBIE, KIT_WHITIE, KIT_ARMOR }, /* leather jerkin */
    { 6017, 1, KIT_LOWBIE, KIT_WHITIE, KIT_ARMOR }, /* thick leather pants */
    { 6016, 1, KIT_LOWBIE, KIT_WHITIE, KIT_ARMOR }, /* thick leather helm */
    { 6304, 1, KIT_LOWBIE, KIT_WHITIE, KIT_NOARMOR }, /* white cotton hood */
    { 6300, 1, KIT_LOWBIE, KIT_WHITIE, KIT_NOARMOR }, /* white cotton shirt */
    { 6301, 1, KIT_LOWBIE, KIT_WHITIE, KIT_NOARMOR }, /* white cotton pants */
    { 5401, 1, KIT_LOWBIE, KIT_WHITIE, KIT_CLASSALL }, /* dagger (to butcher) */

    /* Dwarves get axe regardless */
    { 5208, 1, KIT_LOWBIE, KIT_DWA, KIT_CLASSALL },
    /* Hobbits get shortsword regardless */
    { 5008, 1, KIT_LOWBIE, KIT_HOB, KIT_CLASSALL },
    /* Warrior humans and elves get broadsword */
    { 5003, 1, KIT_LOWBIE, KIT_WHITIE & ~(KIT_DWA | KIT_HOB), KIT_WARRIOR },
    /* Non-warrior humans and elves get longsword */
    { 5002, 1, KIT_LOWBIE, KIT_WHITIE & ~(KIT_DWA | KIT_HOB), KIT_CLASSALL & ~KIT_WARRIOR },

    { 7139, 1, KIT_LEGEND, KIT_DARKIE, KIT_CLASSALL }, /* leather flask */
    { 7703, 1, KIT_LOWBIE, KIT_DARKIE, KIT_CLASSALL }, /* leather bag */
    { 7206, 4, KIT_LEGEND, KIT_DARKIE, KIT_CLASSALL }, /* dried strip of flesh */
    { 6333, 1, KIT_LOWBIE, KIT_DARKIE, KIT_CLASSALL }, /* black robe */
    { 6035, 1, KIT_LOWBIE, KIT_DARKIE, KIT_CLASSALL }, /* ironshod boots */
    { 6123, 1, KIT_LOWBIE, KIT_DARKIE, KIT_CLASSALL }, /* rusted chain shirt */
    { 6124, 1, KIT_LOWBIE, KIT_DARKIE, KIT_CLASSALL }, /* rusted chain legging */
    { 6125, 1, KIT_LOWBIE, KIT_DARKIE, KIT_CLASSALL }, /* rusted chain coif */

    /* Rusted iron hammer for warrior darkies */
    { 5304, 1, KIT_LOWBIE, KIT_DARKIE, KIT_WARRIOR },
    /* Bone scimitar for any other class */
    { 5011, 1, KIT_LOWBIE, KIT_DARKIE, KIT_CLASSALL & ~KIT_WARRIOR }
};

int kit_item_num = sizeof(kit_eq) / sizeof(kit_item);

SPECIAL(kit_room)
{
    int i, j, num_items;
    int rnum;
    int level;
    int coef;
    struct obj_data* o;

    if (cmd != CMD_ASK || callflag != SPECIAL_COMMAND)
        return FALSE;

    if (PLR_FLAGGED(ch, PLR_WAS_KITTED)) {
        act("Sorry, you've already received a set of equipment.", FALSE, ch, NULL, NULL, TO_CHAR);
        return TRUE;
    }

    coef = 0;
    for (i = 1; i <= MAX_PROFS; ++i)
        if (GET_PROF_POINTS(i, ch) >= GET_PROF_POINTS(coef, ch))
            coef = i;

    coef = 1 << coef; /* bitvectorize it */

    level = GET_LEVEL(ch);
    if (IS_SET(1 << GET_RACE(ch), KIT_DARKIE))
        level += 3;
    if (IS_SET(1 << GET_RACE(ch), KIT_THIRD))
        level += 3;
    level = MIN(level, LEVEL_IMPL);

    num_items = 0;
    for (i = 0; i < kit_item_num; ++i) {
        if (level <= kit_eq[i].level && IS_SET(1 << GET_RACE(ch), kit_eq[i].race) && IS_SET(coef, kit_eq[i].coef)) {
            rnum = real_object(kit_eq[i].vnum);
            for (j = 0; j < kit_eq[i].number; ++j) {
                o = read_object(rnum, REAL);
                obj_to_char(o, ch);
                o->touched = 1; /* Should it be 'touched'? */
                ++num_items;
            }
        }
    }

    SET_BIT(PLR_FLAGS(ch), PLR_WAS_KITTED);
    if (!num_items)
        act("You don't need any free equipment!", FALSE, ch, NULL, NULL, TO_CHAR);
    else if (level <= KIT_LOWBIE)
        act("You receive a set of basic equipment.", FALSE, ch, NULL, NULL, TO_CHAR);
    else if (level > KIT_LOWBIE && level <= KIT_MIDBIE)
        act("You receive some essentials to help in your recovery.", FALSE, ch, NULL, NULL,
            TO_CHAR);
    else if (level > KIT_MIDBIE && level <= KIT_LEGEND)
        act("You receive a few useful items.", FALSE, ch, NULL, NULL, TO_CHAR);
    else if (level > KIT_LEGEND && level <= KIT_ALWAYS)
        act("You receive only the bare necessities.", FALSE, ch, NULL, NULL, TO_CHAR);
    else
        act("Impossible case in kit_room.  Please notify an immortal.", FALSE, ch, NULL, NULL,
            TO_CHAR);

    return TRUE;
}

/*
 * Special procedures for objects
 */
SPECIAL(obj_willpower)
{
    obj_data* obj;
    char_data* holder;

    obj = (obj_data*)(host);

    if (!(obj))
        return 0;
    holder = obj->carried_by;
    if (!(holder)) {
        obj->obj_flags.prog_number = 0;
        return 0;
    }
    if (holder->equipment[WIELD] != obj) {
        obj->obj_flags.prog_number = 0;
        return 0;
    }
    return 0;
}

/*
 * Proc's for mobs begin here.
 */

SPECIAL(snake)
{
    if (cmd)
        return FALSE;

    if (callflag != SPECIAL_SELF)
        return FALSE;

    if (GET_POS(ch) != POSITION_FIGHTING)
        return FALSE;

    if (host->specials.fighting && (host->specials.fighting->in_room == host->in_room) && (number(0, 42 - GET_LEVEL(host)) < std::min(1 + GET_LEVEL(host) / 4, 4))) {
        act("$n bites $N!", 1, host, 0, host->specials.fighting, TO_NOTVICT);
        act("$n bites you!", 1, host, 0, host->specials.fighting, TO_VICT);
        spell_poison(host, "", SPELL_TYPE_SPELL, host->specials.fighting, 0, 0, 0);
        return TRUE;
    }
    return FALSE;
}

/*
 * This function is not highly commented, except for its
 * differences from gatekeeper2.  If you'd like commentary,
 * please see gatekeeper2.
 *
 * Subcommands are as follows:
 *   - 0: Called
 *   - 1: Opening the gate
 *   - 2: Closing the gate
 */

SPECIAL(gatekeeper)
{
    int doordir;
    char* msg;

    if (callflag != SPECIAL_NONE && callflag != SPECIAL_SELF && callflag != SPECIAL_COMMAND && callflag != SPECIAL_DELAY)
        return FALSE;

    if (GET_POS(host) < POSITION_SITTING)
        return FALSE;

    /* Character from the wtl? */
    if (wtl && wtl->targ1.type == TARGET_CHAR && char_exists(wtl->targ1.ch_num))
        ch = wtl->targ1.ptr.ch;

    if (callflag != SPECIAL_DELAY || ch == NULL) {
        /*
         * This if block is the only part of this code that should
         * execute if callflag=SPECIAL_SELF
         */
        if (!cmd || ch == NULL) {
            /* Leave gates open during the day */
            if (weather_info.sunlight >= SUN_RISE && weather_info.sunlight < SUN_SET)
                for (doordir = 0; doordir < NUM_OF_DIRS; doordir++)
                    if (EXIT(host, doordir))
                        if (IS_SET(EXIT(host, doordir)->exit_info, EX_ISDOOR) && EXIT(host, doordir)->keyword != NULL) {
                            do_unlock(host, EXIT(host, doordir)->keyword, 0, 0, 0);
                            do_open(host, EXIT(host, doordir)->keyword, 0, 0, 0);
                        }

            /* Keep them closed at night */
            if (weather_info.sunlight < SUN_RISE || weather_info.sunlight >= SUN_SET)
                for (doordir = 0; doordir < NUM_OF_DIRS; doordir++)
                    if (EXIT(host, doordir))
                        if (IS_SET(EXIT(host, doordir)->exit_info, EX_ISDOOR) && EXIT(host, doordir)->keyword != NULL) {
                            do_close(host, EXIT(host, doordir)->keyword, 0, 0, 0);
                            do_lock(host, EXIT(host, doordir)->keyword, 0, 0, 0);
                        }
        }

        if (cmd != CMD_SAY && cmd != CMD_KNOCK)
            return FALSE;

        if (!CAN_SEE(host, ch)) {
            do_gen_com(host, "Who is out there?", 0, 0, SCMD_YELL);
            return FALSE;
        }

        if (cmd == CMD_KNOCK) {
            host->delay.targ2.type = TARGET_OTHER;
            if (wtl)
                host->delay.targ2.ch_num = wtl->targ1.ch_num;
            else
                host->delay.targ2.ch_num = ch->delay.targ1.ch_num;
        } else {
            while (*arg && *arg <= ' ')
                arg++;

            if (!*arg || strncmp(arg, "open", 4)) {
                if (number(1, 10) == 5)
                    do_say(host, "Stop loitering here, I have a job to do.", 0, 0, 0);
                return FALSE;
            }

            while (*arg && *arg > ' ')
                arg++;
            while (*arg && *arg <= ' ')
                arg++;

            /* Get the direction of the door */
            for (doordir = 0; doordir < NUM_OF_DIRS; doordir++)
                if (EXIT(host, doordir))
                    if (IS_SET(EXIT(host, doordir)->exit_info, EX_ISDOOR) && (!EXIT(host, doordir)->keyword || (!*arg || !strcmp(arg, EXIT(host, doordir)->keyword))))
                        break;

            if (doordir == NUM_OF_DIRS) {
                do_say(host, "There is nothing such to open.", 0, 0, 0);
                return FALSE;
            }

            host->delay.targ2.type = TARGET_OTHER;
            host->delay.targ2.ch_num = doordir;
        }

        /* Wait between 1 and 3 ticks. */
        WAIT_STATE_FULL(host, number(2, 4), -1, (cmd == CMD_KNOCK) ? 1 : 2, 30, 0, ch->abs_number,
            ch, 0, TARGET_CHAR);

        return FALSE;
    }

    if (!host->delay.cmd || !host->delay.subcmd) {
        do_say(host, "Ugh, I forgot what I'm doing.", 0, 0, 0);
        host->delay.cmd = host->delay.subcmd = 0;
        return FALSE;
    }

    if (host->delay.targ2.ch_num == -1) {
        do_say(host, "There is nothing like that here.", 0, 0, 0);
        host->delay.cmd = host->delay.subcmd = 0;
        return FALSE;
    }

    if (host->delay.targ2.ch_num == -2) {
        do_say(host, "Ah, there is no door for me to open.", 0, 0, 0);
        host->delay.cmd = host->delay.subcmd = 0;
        return FALSE;
    }

    if (!char_exists(host->delay.targ1.ch_num))
        return FALSE;
    ch = host->delay.targ1.ptr.ch;
    doordir = host->delay.targ2.ch_num;

    if (ch != NULL && IS_AGGR_TO(host, ch)) {
        asprintf(&msg, "To arms! The enemy is at %s!", world[ch->in_room].name);
        do_gen_com(host, msg, 0, 0, SCMD_YELL);
        free(msg);
        host->delay.cmd = host->delay.subcmd = 0;
        return FALSE;
    }

    if (ch != NULL && !RP_RACE_CHECK(host, ch))
        return FALSE;

    // Gatekeeper with swim flag will not open gates at request:
    if (MOB_FLAGGED(host, MOB_CAN_SWIM)) {
        host->delay.cmd = host->delay.subcmd = 0;
        return FALSE;
    }

    /* Can these two acts be consolidated? */
    if ((host->delay.subcmd == 1) && (ch->in_room == host->in_room)) {
        act("$n shifts uncertainly and peers at $N.", FALSE, host, 0, ch, TO_NOTVICT);
        act("$n shifts uncertainly and peers at you.", FALSE, host, 0, ch, TO_VICT);
        host->delay.cmd = host->delay.subcmd = 0;
        return FALSE;
    }

    if (host->delay.subcmd == 2) {
        if (CAN_GO(host, doordir)) {
            do_say(host, "Ah, the door is open. You are free to go.", 0, 0, 0);
            host->delay.cmd = host->delay.subcmd = 0;
            return FALSE;
        }
    }

    if (host->delay.subcmd == 1 || host->delay.subcmd == 2) {
        do_unlock(host, EXIT(host, doordir)->keyword, 0, 0, 0);
        do_open(host, EXIT(host, doordir)->keyword, 0, 0, 0);
        WAIT_STATE_FULL(host, 15, -1, 3, 30, 0, ch->abs_number, ch, 0, TARGET_CHAR);
        return FALSE;
    }

    if (host->delay.subcmd == 3) {
        do_close(host, EXIT(host, doordir)->keyword, 0, 0, 0);
        do_lock(host, EXIT(host, doordir)->keyword, 0, 0, 0);
        host->delay.cmd = host->delay.subcmd = 0;
        return FALSE;
    }

    host->delay.cmd = host->delay.subcmd = 0;

    return FALSE;
}

/*
 * Gatekeeper_no_knock is a basic copy of gatekeeper that
 * doesn't react to people. This proc can and should be re-
 * written at a later date.
 *
 * Does not react to subcommands; simply keeps a gate either:
 *   - open, if it's daytime,
 *   - closed, if it's night time.
 *
 */
SPECIAL(gatekeeper_no_knock)
{
    int doordir;

    if (GET_POS(host) < POSITION_SITTING)
        return FALSE;

    if (wtl && wtl->targ1.type == TARGET_CHAR && char_exists(wtl->targ1.ch_num))
        ch = wtl->targ1.ptr.ch;

    if (callflag != SPECIAL_DELAY || ch == NULL) {
        if (!cmd || ch == NULL) {
            if (weather_info.sunlight >= SUN_RISE && weather_info.sunlight < SUN_SET)
                for (doordir = 0; doordir < NUM_OF_DIRS; doordir++)
                    if (EXIT(host, doordir))
                        if (IS_SET(EXIT(host, doordir)->exit_info, EX_ISDOOR) && EXIT(host, doordir)->keyword != NULL) {
                            do_unlock(host, EXIT(host, doordir)->keyword, 0, 0, 0);
                            do_open(host, EXIT(host, doordir)->keyword, 0, 0, 0);
                        }

            if (weather_info.sunlight < SUN_RISE || weather_info.sunlight >= SUN_SET)
                for (doordir = 0; doordir < NUM_OF_DIRS; doordir++)
                    if (EXIT(host, doordir))
                        if (IS_SET(EXIT(host, doordir)->exit_info, EX_ISDOOR) && EXIT(host, doordir)->keyword != NULL) {
                            do_close(host, EXIT(host, doordir)->keyword, 0, 0, 0);
                            do_lock(host, EXIT(host, doordir)->keyword, 0, 0, 0);
                        }
        }

        return FALSE;
    }

    host->delay.cmd = host->delay.subcmd = 0;

    return FALSE;
}

/*
 * The "paranoid" gatekeeper: he doesn't leave the gates open
 * during the day.  Rather, they're always closed.
 *
 * We react to these callflags for these reasons:
 *   - NONE: do not ask me why, but the people who coded knock
 *     decided that it should generate a SPECIAL_NONE; not a
 *     SPECIAL_COMMAND.  This seems like a bad idea to me--we
 *     should probably change it some day.
 *   - SELF: for the paranoid gatekeeper, the self callflag
 *     is only useful to cause the gatekeeper to close and
 *     lock a gate that has been left open.
 *   - COMMAND: this is the most obviously required callflag.
 *     The command callflag is sent when characters perform a
 *     command, and thus facilitate saying "open" and knocking.
 *   - DELAY: We use delays throughout the gatekeeper specproc
 *     to cause both opening and closing of doors to happen
 *     after small delays from when an open/knock command is
 *     issued.
 *
 * Looks like the only difference between this gatekeeper
 * and the other is that this one doesn't open its doors
 * at sunrise and close them at sunset.  Why the _hell_ do
 * we have completely different functions for this minor
 * alteration in functionality?
 *
 * Subcommands:
 *  - 0: On start
 *  - 1: Going to open the door
 *  - 2: Going to close the door
 */
SPECIAL(gatekeeper2)
{
    char* msg;
    int doordir;

    if (callflag != SPECIAL_NONE && callflag != SPECIAL_SELF && callflag != SPECIAL_COMMAND && callflag != SPECIAL_DELAY)
        return FALSE;

    if (GET_POS(host) < POSITION_SITTING)
        return FALSE;

    if (wtl && wtl->targ1.type == TARGET_CHAR && char_exists(wtl->targ1.ch_num))
        ch = wtl->targ1.ptr.ch;

    if (callflag != SPECIAL_DELAY || ch == NULL) {
        /* Are any of the doors open? This is the only use of SPECIAL_SELF */
        if (!cmd || ch == NULL) {
            for (doordir = 0; doordir < NUM_OF_DIRS; doordir++)
                if (EXIT(host, doordir))
                    if (IS_SET(EXIT(host, doordir)->exit_info, EX_ISDOOR) && EXIT(host, doordir)->keyword != NULL) {
                        do_close(host, EXIT(host, doordir)->keyword, 0, 0, 0);
                        do_lock(host, EXIT(host, doordir)->keyword, 0, 0, 0);
                    }
        }

        /* If command isn't say or knock, we aren't interested */
        if (cmd != CMD_SAY && cmd != CMD_KNOCK)
            return FALSE;

        /* Don't trust someone you can't see */
        if (!CAN_SEE(host, ch)) {
            do_gen_com(host, "Who is out there?", 0, 0, SCMD_YELL);

            return FALSE;
        }

        /* If they knocked, target should be resolved already */
        if (cmd == CMD_KNOCK) {
            host->delay.targ2.type = TARGET_OTHER;
            if (wtl)
                host->delay.targ2.ch_num = wtl->targ1.ch_num;
            else
                host->delay.targ2.ch_num = ch->delay.targ1.ch_num;
        } else {
            /* They used 'say'.  Check for 'open <doorname>' */
            while (*arg && *arg <= ' ')
                arg++;

            if (!*arg || strncmp(arg, "open", 4)) {
                if (number(1, 10) == 5)
                    do_say(host, "Stop loitering here, I have a job to do.", 0, 0, 0);
                return FALSE;
            }

            while (*arg && *arg > ' ')
                arg++;
            while (*arg && *arg <= ' ')
                arg++;

            for (doordir = 0; doordir < NUM_OF_DIRS; doordir++)
                if (EXIT(host, doordir))
                    if (IS_SET(EXIT(host, doordir)->exit_info, EX_ISDOOR) && (!EXIT(host, doordir)->keyword || (!*arg || !strcmp(arg, EXIT(host, doordir)->keyword))))
                        break;

            if (doordir == NUM_OF_DIRS) {
                do_say(host, "There is nothing such to open.", 0, 0, 0);
                return FALSE;
            }

            host->delay.targ2.type = TARGET_OTHER;
            host->delay.targ2.ch_num = doordir;
        }

        /*
         *Note that due to the implementation of wait states, imm-
         * ediately after this function returns, it will decrement
         * the gatekeeper's wait state.  So using a wait state of 1
         * will result in having no delay, a wait state of 2 will
         * result in a 2 tick delay, and so on.
         */
        WAIT_STATE_FULL(host, number(2, 4), -1, cmd == CMD_KNOCK ? 1 : 2, 30, 0, ch->abs_number, ch,
            0, TARGET_CHAR);
        return FALSE;
    }

    /*
     * From this point on, we're completing our -own- delay, so
     * it's safe to modify delay values.  In everything above,
     * modifying a delay value could cause the dreaded gatekeeper
     * bash bug, because you could overwrite a delay created by
     * bash (or anything else, for that matter)
     */
    if (!host->delay.cmd || !host->delay.subcmd) {
        do_say(host, "Ugh, I forgot what I'm doing.", 0, 0, 0);
        host->delay.cmd = host->delay.subcmd = 0;
        return FALSE;
    }

    if (host->delay.targ2.ch_num == -1) {
        do_say(host, "There is nothing like that here.", 0, 0, 0);
        host->delay.cmd = host->delay.subcmd = 0;
        return FALSE;
    }

    if (host->delay.targ2.ch_num == -2) {
        do_say(host, "Ah, there is no door for me to open.", 0, 0, 0);
        host->delay.cmd = host->delay.subcmd = 0;
        return FALSE;
    }

    if (!char_exists(host->delay.targ1.ch_num))
        return FALSE;
    ch = host->delay.targ1.ptr.ch;
    doordir = host->delay.targ2.ch_num;

    if (ch && IS_AGGR_TO(host, ch)) {
        asprintf(&msg, "To arms! The enemy is at %s!", world[ch->in_room].name);
        do_gen_com(host, msg, 0, 0, SCMD_YELL);
        free(msg);
        host->delay.cmd = host->delay.subcmd = 0;
        return FALSE;
    }

    if (ch != NULL && !RP_RACE_CHECK(host, ch))
        return FALSE;

    if (host->delay.subcmd == 1 && ch->in_room == host->in_room) {
        act("$n shifts uncertainly and peers at $N.", FALSE, host, 0, ch, TO_NOTVICT);
        act("$n shifts uncertainly and peers at you.", FALSE, host, 0, ch, TO_VICT);
        host->delay.cmd = host->delay.subcmd = 0;
        return FALSE;
    }

    if (host->delay.subcmd == 2) {
        if (CAN_GO(host, doordir)) {
            do_say(host, "Ah, the door is open. You are free to go.", 0, 0, 0);
            host->delay.cmd = host->delay.subcmd = 0;
            return FALSE;
        }
    }

    if (host->delay.subcmd == 1 || host->delay.subcmd == 2) {
        do_unlock(host, EXIT(host, doordir)->keyword, 0, 0, 0);
        do_open(host, EXIT(host, doordir)->keyword, 0, 0, 0);
        WAIT_STATE_FULL(host, 15, -1, 3, 30, 0, ch->abs_number, ch, 0, TARGET_CHAR);
        return FALSE;
    }

    if (host->delay.subcmd == 3) {
        do_close(host, EXIT(host, doordir)->keyword, 0, 0, 0);
        do_lock(host, EXIT(host, doordir)->keyword, 0, 0, 0);
        host->delay.cmd = host->delay.subcmd = 0;
        return FALSE;
    }

    host->delay.cmd = host->delay.subcmd = 0;

    return FALSE;
}

SPECIAL(room_temple)
{
    if (callflag != SPECIAL_COMMAND || !host)
        return FALSE;

    if (cmd != CMD_CONCENTRATE)
        return FALSE;

    if (GET_POS(host) != POSITION_STANDING)
        return FALSE;

    if (utils::get_spirits(host) < 100) {
        utils::set_spirits(host, 100);
        send_to_char("You feel inspired!\n\r", host);
    }

    return FALSE;
}

/* The Ferry */

void boot_ferries()
{
    int tmp, num;
    for (num = 0; num < num_of_ferries; num++)
        for (tmp = 0; tmp < ferry_boat_data[num].length; tmp++) {
            ferry_boat_data[num].from_room[tmp] = real_room(ferry_boat_data[num].from_room[tmp]);
            ferry_boat_data[num].to_room[tmp] = real_room(ferry_boat_data[num].to_room[tmp]);
        }

    for (num = 0; num < num_of_captains; num++) {
        for (tmp = 0; tmp < ferry_captain_data[num].length; tmp++) {
            ferry_captain_data[num].ferry_route[tmp] = real_room(ferry_captain_data[num].ferry_route[tmp]);
            ferry_captain_data[num].cabin_route[tmp] = real_room(ferry_captain_data[num].cabin_route[tmp]);
        }
        ferry_captain_data[num].num_of_ferry = real_object(ferry_captain_data[num].num_of_ferry);
    }
}

void _recursive_move(struct char_data* ch, struct obj_data* hostobj)
{
    struct follow_type* tmpfol;
    int num, tmp, was_in;

    if (!ch || !hostobj)
        return;
    if (ch->in_room != hostobj->in_room)
        return;

    was_in = ch->in_room;
    num = hostobj->obj_flags.prog_number;

    if ((num < 0) || (num >= num_of_ferries))
        num = 0;
    for (tmp = 0; tmp < ferry_boat_data[num].length; tmp++)
        if (ch->in_room == ferry_boat_data[num].from_room[tmp])
            break;

    if ((tmp == ferry_boat_data[num].length) || (ferry_boat_data[num].to_room[tmp] < 0)) {
        send_to_char("You cannot enter it here.\n\r", ch);
        return;
    }
    if (IS_RIDING(ch)) {
        send_to_char("You can not enter it riding.\n\r", ch);
        return;
    }

    act("You entered $o.", FALSE, ch, hostobj, 0, TO_CHAR);
    act("$n enters $o.", TRUE, ch, hostobj, 0, TO_ROOM);
    char_from_room(ch);
    char_to_room(ch, ferry_boat_data[num].to_room[tmp]);
    do_look(ch, "", 0, 0, 0);
    act("$n comes aboard.", TRUE, ch, 0, 0, TO_ROOM);
    for (tmpfol = ch->followers; tmpfol; tmpfol = tmpfol->next)
        if (tmpfol->follower->in_room == was_in) {
            if (IS_RIDING(tmpfol->follower)) {
                send_to_char("You can not enter it riding.\n\r", tmpfol->follower);
                act("ACK! $N could not follow you in.", FALSE, ch, 0, tmpfol->follower, TO_CHAR);
            } else
                _recursive_move(tmpfol->follower, hostobj);
        }
}

SPECIAL(ferry_boat)
{
    struct obj_data* hostobj = (struct obj_data*)host;
    int tmp, num, was_in;

    if ((cmd != CMD_ENTER) || !ch)
        return FALSE;

    if (ch->in_room != hostobj->in_room)
        return FALSE;

    was_in = ch->in_room;
    while (*arg && (*arg <= ' '))
        arg++;
    if (!*arg)
        return FALSE;

    if (!isname(arg, hostobj->name))
        return FALSE;

    num = hostobj->obj_flags.prog_number;
    if ((num < 0) || (num >= num_of_ferries))
        num = 0;

    for (tmp = 0; tmp < ferry_boat_data[num].length; tmp++)
        if (ch->in_room == ferry_boat_data[num].from_room[tmp])
            break;

    _recursive_move(ch, hostobj);
    return TRUE;
}

SPECIAL(ferry_captain)
{
    struct obj_data *tmpobj, *ferryobj;
    struct char_data* tmpch;
    int tmp, num, new_room, old_room;

    num = host->specials.store_prog_number;
    if ((num < 0) || (num >= num_of_captains))
        num = 0;

    if (host->in_room == NOWHERE)
        return FALSE;

    if (GET_POS(host) < POSITION_FIGHTING) {
        do_wake(host, "", 0, 0, 0);
        do_stand(host, "", 0, 0, 0);
    }
    if (GET_POS(host) < POSITION_STANDING)
        return FALSE;

    tmp = ferry_captain_data[num].ferry_route[ferry_captain_data[num].marker];
    for (ferryobj = world[tmp].contents; ferryobj; ferryobj = ferryobj->next_content) {
        if (ferryobj->item_number == ferry_captain_data[num].num_of_ferry)
            break;
    }
    if (!ferryobj)
        for (ferryobj = object_list; ferryobj; ferryobj = ferryobj->next) {
            if (ferryobj->item_number == ferry_captain_data[num].num_of_ferry)
                break;
        }

    if (!ferryobj) {
        if (!number(0, 5))
            do_say(host, "I wonder where my ship is?", 0, 0, 0);
        return FALSE;
    }

    if (cmd != 0)
        return FALSE;

    if (ferry_captain_data[num].timer > 0) {
        ferry_captain_data[num].timer--;
        return FALSE;
    }

    old_room = ferry_captain_data[num].cabin_route[ferry_captain_data[num].marker];
    if (old_room != host->in_room) {
        char_from_room(host);
        char_to_room(host, old_room);
        act("$n steps out from the shadows.", TRUE, host, 0, 0, TO_ROOM);
    }

    if ((ferry_captain_data[num].timer == 0) && (ferry_captain_data[num].stop_time[ferry_captain_data[num].marker] > 0)) {
        tmp = host->in_room;
        host->in_room = ferryobj->in_room;
        act(ferry_captain_data[num].leave_to_outside, FALSE, host, ferryobj, 0, TO_ROOM);
        host->in_room = tmp;
        act(ferry_captain_data[num].leave_to_inside, FALSE, host, 0, 0, TO_ROOM);
    }

    ferry_captain_data[num].marker++;
    if (ferry_captain_data[num].marker >= ferry_captain_data[num].length)
        ferry_captain_data[num].marker = 0;

    act(ferry_captain_data[num].move_out_inside, FALSE, host, ferryobj, 0, TO_ROOM);

    tmp = host->in_room;
    host->in_room = ferryobj->in_room;
    act(ferry_captain_data[num].move_out_outside, FALSE, host, ferryobj, 0, TO_ROOM);
    host->in_room = tmp;

    obj_from_room(ferryobj);

    tmp = ferry_captain_data[num].ferry_route[ferry_captain_data[num].marker];
    if (tmp != NOWHERE)
        obj_to_room(ferryobj, tmp);

    new_room = ferry_captain_data[num].cabin_route[ferry_captain_data[num].marker];

    if (new_room != old_room) {
        /* nobody should be in that room, but just in case they are...*/
        for (tmpch = world[old_room].people; tmpch; tmpch = tmpch->next_in_room) {
            if (!tmpch->next_in_room)
                break;
        }
        if (tmpch)
            tmpch->next_in_room = world[new_room].people;
        else
            world[old_room].people = world[new_room].people;
        for (tmpobj = world[old_room].contents; tmpobj; tmpobj = tmpobj->next_content)
            if (!tmpobj->next_content)
                break;
        if (tmpobj)
            tmpobj->next_content = world[new_room].contents;
        else
            world[old_room].contents = world[new_room].contents;

        world[new_room].people = world[old_room].people;
        world[old_room].people = 0;
        world[new_room].contents = world[old_room].contents;
        world[old_room].contents = 0;

        for (tmpch = world[new_room].people; tmpch; tmpch = tmpch->next_in_room) {
            tmpch->in_room = new_room;
        }
        for (tmpobj = world[new_room].contents; tmpobj; tmpobj = tmpobj->next_content)
            tmpobj->in_room = new_room;
    }

    ferry_captain_data[num].timer = ferry_captain_data[num].stop_time[ferry_captain_data[num].marker];

    if (ferry_captain_data[num].timer == 0) {
        act(ferry_captain_data[num].move_in_inside, TRUE, host, ferryobj, 0, TO_ROOM);
        tmp = host->in_room;
        host->in_room = ferryobj->in_room;
        act(ferry_captain_data[num].move_in_outside, FALSE, host, ferryobj, 0, TO_ROOM);
        host->in_room = tmp;
    } else {
        act(ferry_captain_data[num].arrive_to_inside, FALSE, host, ferryobj, 0, TO_ROOM);

        tmp = host->in_room;
        host->in_room = ferryobj->in_room;
        act(ferry_captain_data[num].arrive_to_outside, FALSE, host, ferryobj, 0, TO_ROOM);
        host->in_room = tmp;
    }
    return TRUE;
}

/*
 * This is a temporary array housing an index of spell lists
 * I am currently working on making each mob hold an individual
 * and unique set of spells for each mob, so once that
 * gets imped i'll remove all stuff relating to these.
 */

int spell_list[][4] = {
    { SPELL_MAGIC_MISSILE, SPELL_MAGIC_MISSILE, SPELL_WORD_OF_PAIN, SPELL_CURSE }, /*level 5*/
    { SPELL_CHILL_RAY, SPELL_CHILL_RAY, SPELL_LEACH, SPELL_PRAGMATISM }, /*level 5*/
    { SPELL_LIGHTNING_BOLT, SPELL_DARK_BOLT, SPELL_DARK_BOLT, SPELL_CURSE }, /*level 15*/
    { SPELL_LIGHTNING_BOLT, SPELL_DARK_BOLT, SPELL_BLACK_ARROW, SPELL_TERROR }, /*level 20*/
    { SPELL_FIREBOLT, SPELL_DARK_BOLT, SPELL_WORD_OF_AGONY, SPELL_POISON }, /*level 25*/
    { SPELL_CONE_OF_COLD, SPELL_CONE_OF_COLD, SPELL_BLACK_ARROW, SPELL_POISON }, /*level 30*/
    { SPELL_FIREBOLT, SPELL_CHILL_RAY, SPELL_BLACK_ARROW, SPELL_CURSE }, /*level 35*/
    { SPELL_FIREBALL, SPELL_CONE_OF_COLD, SPELL_SPEAR_OF_DARKNESS, SPELL_TERROR }, /*level 40*/
    { SPELL_CONE_OF_COLD, SPELL_DARK_BOLT, SPELL_DARK_BOLT, SPELL_CURSE }, /*level 45*/
    { SPELL_FIREBALL, SPELL_SEARING_DARKNESS, SPELL_SPEAR_OF_DARKNESS, SPELL_CONFUSE }, /*level 50*/

};

namespace {
int choose_mystic_spell(char_data* caster, char_data* target)
{
    if (caster == target) {
        /*
         * When this mob_type is poisoned it will cast remove
         * and when below 1/3 of its hit points will start
         * casting regeneration spells.
         */
        if (affected_by_spell(caster, SPELL_POISON)) {
            return SPELL_REMOVE_POISON;
        } else if (caster->tmpabilities.hit < caster->abilities.hit / 3) {
            if (!affected_by_spell(caster, SPELL_REGENERATION)) {
                return SPELL_REGENERATION;
            } else if (!affected_by_spell(caster, SPELL_CURING)) {
                return SPELL_CURING;
            }
        }

        return 0;
    }

    // Level cap the spells that the caster will use.
    int max_spell_list_index = (caster->player.level / 5) - 1;
    max_spell_list_index = std::min(max_spell_list_index, 9);
    max_spell_list_index = std::max(max_spell_list_index, 0);

    /*
     * If the mob is engaged its going to cast
     * its mystic spells from the list.
     * I'm using a random number one to ten here on a
     * temporary basis until i've written the code
     * that allows for individual mob spell lists.
     */
    int spell_list_index = number(0, max_spell_list_index);
    return spell_list[spell_list_index][3];
}

/*
 * Little function that runs a series
 * of checks to give the mob in question the
 * Correct set of spells to cast from.
 */
int get_mob_spell_type(const char_data* caster)
{
    if (GET_RACE(caster) == RACE_MAGUS)
        return 2;
    else if (GET_RACE(caster) == RACE_URUK || GET_RACE(caster) == RACE_ORC)
        return 1;
    else
        return 0;
}

int choose_mage_spell(const char_data* caster, const char_data* target)
{
    // Heal up if low
    if (caster == target) {
        if (caster->tmpabilities.hit < caster->abilities.hit / 3) {
            return SPELL_CURE_SELF;
        } else {
            return 0;
        }
    }

    // Allow wimpy mobs to peace out.
    if (IS_SET(MOB_FLAGS(caster), MOB_WIMPY) && (caster->tmpabilities.hit < caster->abilities.hit / 4)) {
        return SPELL_BLINK;
    }

    // 50% chance to not cast at all.
    if (number() < 0.5f) {
        return 0;
    }

    /*
     * Since we have a list of 10 spells to choose from, and
     * since some of our in-game mobs are over 50 in level
     * any mob over level 50 will have its random number
     * divided by eight rather than 5 to ensure a spell
     * is picked from the array spell_list.
     * This will very soon be replaced with each mob having
     * a bitvector list of spells that each can cast from.
     */
    int spell_list_index = number(0, GET_PROF_LEVEL(PROF_MAGIC_USER, caster));
    if (spell_list_index > 50)
        spell_list_index = spell_list_index / 8;
    else
        spell_list_index = spell_list_index / 5;

    int spell_var = get_mob_spell_type(caster);
    return spell_list[spell_list_index][spell_var];
}

bool is_engaged_with_victim(const char_data* character, const char_data* victim)
{
    if (victim == nullptr || character == nullptr) {
        return false;
    }

    // Targets are directly engaged.
    if (character->specials.fighting == victim || victim->specials.fighting == character) {
        return true;
    }

    // Don't check NPC grouping to prevent abuse cases like leading and grouping tons of mounts
    // in so that they eat spells for you.
    if (utils::is_npc(*character)) {
        return false;
    }

    // Targets are engaged through character's group.
    if (character->group != nullptr) {
        const group_data& group = *character->group;
        for (const char_data* group_member : group) {
            if (group_member->specials.fighting == victim || victim->specials.fighting == group_member) {
                return true;
            }
        }
    }

    return false;
}

char_data* choose_caster_target(char_data* caster)
{
    // Choose an initial target.  If the caster isn't fighting anyone, it will be himself.
    // Otherwise, it will be someone that is in combat with him.
    if (caster->specials.fighting == nullptr) {
        return caster;
    }

    // NPC casters will preferentially target characters that are fighting them or
    // group members of those characters.
    int valid_count = 0;
    char_data* target = caster->specials.fighting; // default target is the character the caster is engaged with
    for (char_data* character = world[caster->in_room].people; character != nullptr;
         character = character->next_in_room) {

        if (!is_engaged_with_victim(character, caster)) {
            continue;
        }

        float selection_chance = 1.0f / ++valid_count;
        if (number() > selection_chance) {
            continue;
        }

        target = character;
    }

    return target;
}
} // namespace

SPECIAL(mob_cleric)
{
    if (callflag != SPECIAL_SELF || host->in_room == NOWHERE)
        return FALSE;

    if (host->delay.wait_value != 0 && host->delay.subcmd != 0) {
        return FALSE;
    }

    char_data* target = choose_caster_target(host);
    if (target == nullptr)
        return FALSE;

    // No good mystic spell to cast.
    int spell_number = choose_mystic_spell(host, target);
    if (spell_number == 0)
        return FALSE;

    host->points.spirit = 100;

    waiting_type wtl_base;
    wtl_base.cmd = CMD_CAST;
    wtl_base.subcmd = 0;
    wtl_base.targ1.ch_num = spell_number;
    wtl_base.targ1.type = TARGET_OTHER;
    wtl_base.targ2.ptr.ch = target;
    wtl_base.targ2.ch_num = target->abs_number;
    wtl_base.targ2.type = TARGET_CHAR;
    wtl_base.targ2.choice = TAR_CHAR_ROOM;
    wtl_base.flg = 1;
    do_cast(host, "", &wtl_base, CMD_CAST, 0);
    return FALSE;
}

SPECIAL(mob_magic_user)
{
    if (callflag != SPECIAL_SELF || host->in_room == NOWHERE)
        return FALSE;

    if (host->delay.wait_value && host->delay.subcmd) {
        return FALSE;
    }

    char_data* target = choose_caster_target(host);
    if (target == nullptr)
        return FALSE;

    int spell_number = choose_mage_spell(host, target);
    if (spell_number == 0)
        return FALSE;

    waiting_type wtl_base;
    wtl_base.cmd = CMD_CAST;
    wtl_base.subcmd = 0;
    wtl_base.targ1.ch_num = spell_number;
    wtl_base.targ1.type = TARGET_OTHER;
    wtl_base.targ2.ptr.ch = target;
    wtl_base.targ2.ch_num = target->abs_number;
    wtl_base.targ2.type = TARGET_CHAR;
    wtl_base.targ2.choice = TAR_CHAR_ROOM;
    wtl_base.flg = 1;
    do_cast(host, "", &wtl_base, CMD_CAST, 0);
    return FALSE;
}

/*  New Mage Below  */
const int fire_mage = 0;
const int lightning_mage = 1;
const int cold_mage = 2;
const int dark_mage = 3;
const int lhuth_mage = 4;
const int default_mage = 5;

const int mage_types = 6;
const char* mage_aliases[] = { "fmage", "lmage", "cmage", "dmage", "lumage", "defaultm" };

const double full_pct = 0.76;
const double three_quarter_pct = 0.51;
const double half_pct = 0.26;
const double quarter_pct = 0.0;

const int hp_brackets = 4;
const double percents[] = { full_pct, three_quarter_pct, half_pct, quarter_pct };

const int indvidual_spells_length = 4;

int new_spell_list[mage_types][hp_brackets][indvidual_spells_length] = {
    { // fire mage
        { SPELL_FIREBOLT },
        { SPELL_FIREBOLT },
        { SPELL_CHILL_RAY, SPELL_FIREBOLT },
        { SPELL_CHILL_RAY, SPELL_MAGIC_MISSILE } },
    { // lightning mage
        { SPELL_LIGHTNING_BOLT },
        { SPELL_LIGHTNING_BOLT },
        { SPELL_CHILL_RAY, SPELL_LIGHTNING_BOLT },
        { SPELL_CHILL_RAY, SPELL_MAGIC_MISSILE } },
    { // cold mage
        { SPELL_CHILL_RAY },
        { SPELL_CHILL_RAY, SPELL_LIGHTNING_BOLT },
        { SPELL_CHILL_RAY },
        { SPELL_CHILL_RAY, SPELL_MAGIC_MISSILE } },
    { // dark mage
        { SPELL_DARK_BOLT },
        { SPELL_DARK_BOLT },
        { SPELL_CHILL_RAY, SPELL_DARK_BOLT },
        { SPELL_CHILL_RAY, SPELL_MAGIC_MISSILE } },
    { // lhuth mage
        { SPELL_WORD_OF_AGONY, SPELL_BLACK_ARROW },
        { SPELL_WORD_OF_AGONY, SPELL_BLACK_ARROW },
        { SPELL_DARK_BOLT, SPELL_BLACK_ARROW },
        { SPELL_LEACH, SPELL_DARK_BOLT } },
    { // default mage
        { SPELL_FIREBOLT, SPELL_LIGHTNING_BOLT },
        { SPELL_FIREBOLT, SPELL_LIGHTNING_BOLT },
        { SPELL_CHILL_RAY, SPELL_LIGHTNING_BOLT },
        { SPELL_CHILL_RAY, SPELL_MAGIC_MISSILE } }
};

int pick_a_spell(int* spell_list, char_data* host)
{
    if (spell_list[0] > 0) {
        if (has_alias(host, "spells")) {
            sprintf(buf, "----------");
            mudlog_aliased_mob(buf, host, "spells");
            for (int i = 1; i <= spell_list[0]; i++) {
                sprintf(buf, "Spell: %d", spell_list[i]);
                mudlog_aliased_mob(buf, host, "spells");
            }
        }
        int chance = number(1, spell_list[0]);
        return spell_list[chance];
    } else {
        sprintf(buf, "--- NO MAGE KEYWORDS FOUND ---");
        mudlog_aliased_mob(buf, host, "spells");
        return 0;
    }
}

// add spell and avoid dupes
void add_spell_to_list(int* spell_list, int spell, char_data* host)
{
    int found = 0;
    for (int i = 1; i <= spell_list[0]; i++) {
        if ((int)spell_list[i] == (int)spell) {
            found += 1;
            break;
        }
    }
    if (found == 0) {
        sprintf(buf, "ADDING: %d", spell);
        mudlog_aliased_mob(buf, host, "spells");
        spell_list[0] += 1;
        spell_list[spell_list[0]] = spell;
    }
}

// fetch spells from hp bracket
void get_spells(int* spell_list, int mage_type, int health, char_data* host)
{
    sprintf(buf, "MType: %d,    Spell_Tier: %d, Pct: %.2f", mage_type, health, percents[health]);
    mudlog_aliased_mob(buf, host, "spells");
    for (int i = 0; i < indvidual_spells_length; i++) {
        if (new_spell_list[mage_type][health][i] != 0) {
            add_spell_to_list(spell_list, new_spell_list[mage_type][health][i], host);
        }
    }
}

// check if correct level and add spell
void add_leveled_spell_to_list(int* spell_list, int spell, int mage_type, int cur_mage_type,
    char* keyword, char_data* host, int min_level)
{
    if (GET_LEVEL(host) >= min_level && mage_type == cur_mage_type && has_alias(host, keyword)) {
        sprintf(buf, "MType: %d    (HL_SPELL)", mage_type);
        mudlog_aliased_mob(buf, host, "spells");
        add_spell_to_list(spell_list, spell, host);
    }
}

// lookup spells by mage type and hp thresholds
void get_combat_spells(char_data* host, int* spell_list, double current_health_pct,
    double current_mana_pct)
{
    for (int mage_type = 0; mage_type < mage_types; mage_type++) {
        char* keyword = (char*)mage_aliases[mage_type];
        if (has_alias(host, keyword)) {
            int current_tier = hp_brackets;
            for (int i = 0; i < hp_brackets; i++) {
                if (current_health_pct >= percents[i]) {
                    current_tier = i;
                    break;
                }
            }
            get_spells(spell_list, mage_type, current_tier, host);

            // restricted spells
            add_leveled_spell_to_list(spell_list, SPELL_FIREBALL, fire_mage, mage_type, keyword,
                host, 40);
            add_leveled_spell_to_list(spell_list, SPELL_CONE_OF_COLD, cold_mage, mage_type, keyword,
                host, 30);
            add_leveled_spell_to_list(spell_list, SPELL_SEARING_DARKNESS, dark_mage, mage_type,
                keyword, host, 40);
            add_leveled_spell_to_list(spell_list, SPELL_EARTHQUAKE, default_mage, mage_type,
                keyword, host, 30);
            if (OUTSIDE(host) && weather_info.sky[world[host->in_room].sector_type] == SKY_LIGHTNING) {
                add_leveled_spell_to_list(spell_list, SPELL_LIGHTNING_STRIKE, lightning_mage,
                    mage_type, keyword, host, 35);
            }
            if (!SUN_PENALTY(host)) {
                add_leveled_spell_to_list(spell_list, SPELL_SPEAR_OF_DARKNESS, lhuth_mage,
                    mage_type, keyword, host, 40);
            }
        }
    }
}

// NOTE: firebolt (maybe a dark spell was too), is returning FALSE?
//       - was seeing scenarios where casting when already casting, but no longer an issue?
// note: also see spec_pro_message
SPECIAL(mob_magic_user_spec)
{
    // was there a reason mob_magic_user checks delay.subcmd & wait_value, when subcmd can be 0??
    // if (host->delay.wait_value && host->delay.subcmd) {
    const int bonus_to_cast = 20;
    int chance_to_cast = GET_LEVEL(host) + bonus_to_cast;
    if ((host->delay.wait_value && host->delay.cmd) || callflag != SPECIAL_SELF || host->in_room == NOWHERE || (number(1 - 100) > chance_to_cast)) {
        return FALSE;
    }

    struct char_data *tmpch, *tmpch_next;
    char_data* target;
    int spell_list[20]; // the first index is the count of spells
    int tgt = 0;
    int spell_number = 0;
    double current_health_pct = (double)GET_HIT(host) / (double)GET_MAX_HIT(host);
    double current_mana_pct = (double)GET_MANA(host) / (double)GET_MAX_MANA(host);

    // conj: prioritize heal powers in non-combat
    if (!host->specials.fighting && has_alias(host, "conj")) {
        // handle regen
        if (!utils::is_affected_by_spell(*host, SPELL_REGENERATION)) {
            target = host;
            tgt = TARGET_CHAR;
            spell_number = SPELL_REGENERATION;
            utils::set_spirits(host, 100);
        }

        // handle curing sat
        if (!utils::is_affected_by_spell(*host, SPELL_CURING) && spell_number == 0) {
            target = host;
            tgt = TARGET_CHAR;
            spell_number = SPELL_CURING;
            utils::set_spirits(host, 100);
        }
    }

    // handle other non-combat
    if (!host->specials.fighting && spell_number == 0) {
        // handle cure self
        if ((current_health_pct <= .9 && !utils::is_affected_by_spell(*host, SPELL_SHIELD)) || (current_health_pct <= .9 && utils::is_affected_by_spell(*host, SPELL_SHIELD) && current_mana_pct >= .5)) {
            target = host;
            tgt = TARGET_CHAR;
            spell_number = SPELL_CURE_SELF;
        }
    }

    // handle switch tactics (when mob is max interrupted)
    if (host->specials.fighting && host->interrupt_count == 3 && spell_number == 0) {
        // shield tact: we use a "super flash" to have an attempt to cast shield
        if (has_alias(host, "shield")) {
            if (!utils::is_affected_by_spell(*host, SPELL_SHIELD) && GET_MANA(host) > 12) {
                if (number(1, 100) > 50) {
                    for (tmpch = world[host->in_room].people; tmpch; tmpch = tmpch->next_in_room) {
                        if (tmpch->specials.fighting == host) {
                            // send_to_char("A blinding flash of light makes you dizzy.\n\r\n",
                            // tmpch); no message?? futile if p has react trig?
                            stop_fighting(tmpch);
                        }
                    }
                    stop_fighting(host);
                    target = host;
                    tgt = TARGET_CHAR;
                    spell_number = SPELL_SHIELD;
                }
            }
        }
    }

    // handle terror
    if (has_alias(host, "terror") && host->specials.fighting && spell_number == 0 && (current_health_pct <= quarter_pct)) {
        if (number(1, 100) > 80) {
            target = host;
            tgt = TARGET_OTHER;
            spell_number = SPELL_TERROR;
            utils::set_spirits(host, 100);
        }
    }

    // Combat Focus
    if (target != host && spell_number == 0) {
        spell_list[0] = 0;
        target = choose_caster_target(host);
        if (target == nullptr) {
            return FALSE;
        }
        if (host->specials.fighting && spell_number == 0) {
            tgt = TARGET_OTHER;
            if (has_alias(host, "conj")) {
                if (number(1, 100) > 75) {
                    host->points.spirit = 100;
                    if (!utils::is_affected_by_spell(*target, SPELL_CONFUSE)) {
                        spell_number = SPELL_CONFUSE;
                    }
                    if (!has_alias(host, "lumage") && !utils::is_affected_by_spell(*target, SPELL_POISON) && spell_number == 0) {
                        spell_number = SPELL_POISON;
                    }
                }
            }
            // Normal combat casting
            if (spell_number == 0 && host->interrupt_count < 3) {
                get_combat_spells(host, spell_list, current_health_pct, current_mana_pct);
                spell_number = pick_a_spell(spell_list, host);
            }
        }
    }

    if (spell_number < 1) {
        return FALSE;
    }

    sprintf(buf, "PROG::MAGE    -> Tgt: %s, Spell: %d, TgtType: %d, InterruptCnt: %d, Hit%: %.2f",
        GET_NAME(target), spell_number, tgt, host->interrupt_count, current_health_pct);
    mudlog_debug_mob(buf, host);

    waiting_type wtl_base;
    wtl_base.cmd = CMD_CAST;
    wtl_base.subcmd = 0;
    wtl_base.targ1.ch_num = spell_number;
    wtl_base.targ1.type = tgt;
    wtl_base.targ2.ptr.ch = target;
    wtl_base.targ2.ch_num = target->abs_number;
    wtl_base.targ2.type = TARGET_CHAR;
    wtl_base.targ2.choice = TAR_CHAR_ROOM;
    wtl_base.flg = 1;
    do_cast(host, "", &wtl_base, CMD_CAST, 0);
    return TRUE;
}

int warrior_abilities[] = {
    CMD_KICK,
    CMD_BASH,
    0,
    0,
    CMD_KICK,
    0,
    0,
    CMD_BASH,
    0,
};

SPECIAL(mob_warrior)
{
    waiting_type wtl_base;
    int com_num, num, tmp, kick_var;
    char_data* tmpch;
    char_data* tar_ch;
    char arg1[] = "";
    char* argptr = arg1;

    if ((callflag != SPECIAL_SELF) || (host->in_room == NOWHERE))
        return FALSE;

    for (num = 0, tmpch = world[ch->in_room].people; tmpch; tmpch = tmpch->next_in_room)
        if (!IS_NPC(tmpch))
            num++;

    if (!num)
        return FALSE;
    if (GET_POS(host) < POSITION_FIGHTING)
        return FALSE;
    if (!host->specials.fighting)
        return FALSE;

    else {
        tar_ch = host->specials.fighting;
        tmp = number(0, GET_PROF_LEVEL(PROF_WARRIOR, host)) % 9;
        com_num = warrior_abilities[tmp];
        kick_var = number(1, 2);
    }
    /*
     * Warrior mobs now kick and swing.
     * The kick_var int deceides which one
     * it will perform at any time, its totally
     * random.
     */
    wtl_base.cmd = com_num;
    if (com_num == CMD_KICK)
        wtl_base.subcmd = kick_var;
    else
        wtl_base.subcmd = 0;
    wtl_base.targ1.ptr.ch = tar_ch;
    wtl_base.targ1.ch_num = tar_ch->abs_number;
    wtl_base.targ1.type = TARGET_CHAR;
    wtl_base.flg = 1;
    command_interpreter(host, argptr, &wtl_base);
    return FALSE;
}

SPECIAL(mob_ranger)
{
    /*
     * Ranger mobs were coded to kick, but coded incorrectly
     * I removed the kick as it wasn't a random Kick, rather
     * the mob kept constantly kicking while engaged in
     * Combat
     */

    waiting_type tmpwtl;
    char_data* tmpch;
    room_data* tmproom;
    int tmp, tmp2, mintime, mintmp;

    bzero((char*)&tmpwtl, sizeof(waiting_type));

    if (host->in_room == NOWHERE)
        return 0;
    if ((callflag != SPECIAL_SELF) && (callflag != SPECIAL_ENTER))
        return 0;

    if (GET_POS(host) < POSITION_FIGHTING)
        update_pos(host);
    tmpch = 0;
    if ((callflag == SPECIAL_ENTER) && ch && IS_AGGR_TO(host, ch))
        tmpch = ch;
    else if (callflag == SPECIAL_SELF) {
        for (tmpch = world[host->in_room].people; tmpch; tmpch = tmpch->next_in_room)
            if ((tmpch != host) && CAN_SEE(host, tmpch) && IS_AGGR_TO(host, tmpch))
                break;
    }
    /* Ambush */
    if (tmpch) {
        tmpwtl.cmd = CMD_AMBUSH;
        tmpwtl.subcmd = 0;
        tmpwtl.targ1.type = TARGET_CHAR;
        tmpwtl.targ1.ptr.ch = tmpch;
        tmpwtl.targ1.ch_num = tmpch->abs_number;
        do_ambush(host, "", &tmpwtl, CMD_AMBUSH, 0);
        tmpwtl.targ1.cleanup();
        return 1;
    }

    if (((GET_POS(host) == POSITION_STANDING) && !GET_HIDING(host) || IS_SET(ch->specials2.hide_flags, HIDING_SNUCK_IN)) && ch->delay.wait_value == 0)
        do_hide(host, "", 0, 0, 0);

    tmproom = &world[host->in_room];
    mintime = 999;
    mintmp = NUM_OF_TRACKS;
    for (tmp = 0; tmp < NUM_OF_TRACKS; tmp++) {
        tmp2 = (24 + time_info.hours - tmproom->room_track[tmp].data / 8) % 24;
        if ((tmproom->room_track[tmp].char_number < 0) && (host->specials2.pref & (1 << -tmproom->room_track[tmp].char_number)) && tmp2 < mintime) {
            mintime = tmp2;
            mintmp = tmp;
        }
    }
    if (mintmp < NUM_OF_TRACKS) {
        tmpwtl.cmd = (tmproom->room_track[mintmp].data & 7) + 1;
        tmpwtl.subcmd = 0;
        // below is bugged! Within do_move() it calls for this proc again right after it moves the
        // char, skipping the ON_ENTER call for each in-between room,
        //   likely doesnt leave tracks in the middle rooms either, also it moves rather instantly
        //   since its looping in a single one_mobile_activity call
        do_move(host, "", &tmpwtl, (tmproom->room_track[mintmp].data & 7) + 1, 0);
        tmpwtl.targ1.cleanup();
        return 0;
    }
    if (GET_POS(host) == POSITION_STANDING) {
        tmpwtl.cmd = number(1, NUM_OF_DIRS);
        tmpwtl.subcmd = 0;
        if (CAN_GO(host, tmpwtl.cmd - 1))
            do_move(host, "", &tmpwtl, tmpwtl.cmd, 0);
        tmpwtl.targ1.cleanup();
    }
    return 0; // Because this isn't a 1: If the mob moves above, when it leaves this prog it can
              // move again from continuing one_mobile_activity (also double delays)
}

/* Created for the new ranger prog */
bool see_hidden(char_data* host, char_data* tmpch)
{
    int hide, vuln_st, my_hide, orig_hide, see_hide;

    orig_hide = see_hiding(host);
    see_hide = orig_hide;
    // COMMENT OUT 3 LINES BELOW to use ONLY THE STANDARD see_hiding() calc
    if (GET_LEVEL(host) >= 40) {
        see_hide = (GET_LEVEL(host) * 2) + number(-7, 20) + number(-7, 20) + number(-7, 20);
    }
    vuln_st = IS_VULNERABLE(host, PLRSPEC_STLH);
    if (vuln_st > 0) {
        see_hide = see_hide - (see_hide * .5);
    }
    hide = 0;
    hide = GET_HIDING(tmpch);
    my_hide = GET_HIDING(host);
    return (!(hide > 0) || (hide > 0 && (see_hide > hide)));
}

// note: we dont check if fighting here, because its also used for ambush
// TODO: factor in god nohassle
bool should_attack(char_data* host, char_data* tmpch)
{
    int is_aggressive = IS_SET(host->specials2.act, MOB_AGGRESSIVE);
    return (tmpch && tmpch != host && CAN_SEE(host, tmpch) && see_hidden(host, tmpch) && see_hidden(host, tmpch) && !PRF_FLAGGED(tmpch, PRF_NOHASSLE) && (IS_AGGR_TO(host, tmpch) || (is_aggressive && !IS_NPC(tmpch)) || host->specials.fighting && (GET_POS(host) > POSITION_SITTING)));
}

void do_spec_ambush(char_data* host, char_data* tmpch)
{
    waiting_type tmpwtl;
    bzero((char*)&tmpwtl, sizeof(waiting_type));
    tmpwtl.cmd = CMD_AMBUSH;
    tmpwtl.subcmd = 0;
    tmpwtl.targ1.type = TARGET_CHAR;
    tmpwtl.targ1.ptr.ch = tmpch;
    tmpwtl.targ1.ch_num = tmpch->abs_number;
    do_ambush(host, "", &tmpwtl, CMD_AMBUSH, 0);
    tmpwtl.targ1.cleanup();
    host->spec_busy = false;
}

void do_spec_hit(char_data* host, char_data* tmpch)
{
    waiting_type tmpwtl;
    bzero((char*)&tmpwtl, sizeof(waiting_type));
    if (GET_INT(host) <= 6) {
        act("$n snarls and lunges at $N!", FALSE, host, 0, tmpch, TO_ROOM);
    } else {
        act("$n grins evilly and attacks $N!", FALSE, host, 0, tmpch, TO_ROOM);
    }
    sscanf(tmpch->player.name, "%s", buf);
    tmpwtl.targ1.type = TARGET_CHAR;
    tmpwtl.targ1.ptr.ch = tmpch;
    tmpwtl.targ1.ch_num = tmpch->abs_number;
    tmpwtl.cmd = CMD_HIT;
    do_hit(host, buf, &tmpwtl, 0, 0);
    host->spec_busy = false;
}

bool prog_do_hunter(char_data* host, int is_wimpy)
{
    return ((IS_SET(host->specials2.act, MOB_MEMORY) || IS_SET(host->specials2.act, MOB_HUNTER) || (IS_AFFECTED(host, AFF_HUNT))) && (GET_POS(host) == POSITION_STANDING) && !is_wimpy && !(MOB_FLAGGED(host, MOB_PET)) && host->specials.memory && !host->specials.fighting);
}

// Works with HUNTER and MEMORY flags, handled below
// Opt: alias keyword "stab" makes it favor ambushing
//    : alias keyword "hunt" makes it hunt tracks
SPECIAL(mob_ranger_new)
{
    waiting_type tmpwtl;
    char_data* tmpch;
    room_data* tmproom;
    struct memory_rec* names;
    struct char_data* vict;
    int tmp, tmp2, mintime, mintmp, dir;
    int is_wimpy = 0;

    long racial_aggr = host->specials2.pref;
    int is_aggressive = IS_SET(host->specials2.act, MOB_AGGRESSIVE);

    bzero((char*)&tmpwtl, sizeof(waiting_type));

    if (host->in_room == NOWHERE)
        return 0;
    if ((callflag != SPECIAL_SELF) && (callflag != SPECIAL_ENTER))
        return 0;

    // this allows do_move, by blocking do_move()'s internal call to SPECIAL PROG again
    ch->spec_busy = true;

    if (GET_POS(host) < POSITION_FIGHTING) {
        update_pos(host);
    }

    const int wimpy_health_limit = GET_MAX_HIT(host) / 5;
    if (GET_HIT(host) <= wimpy_health_limit) {
        sprintf(buf, "WIMPY --> health: %d, wimpy: %d", GET_HIT(host), wimpy_health_limit);
        mudlog_debug_mob(buf, host);
        is_wimpy = 1;
    }

    tmpch = 0;
    const bool is_engaged = host->specials.fighting != nullptr;
    const bool is_not_engaged = !is_engaged;
    if (is_not_engaged) {
        // hide here for pop or after fighting and didnt move
        if (((GET_POS(host) == POSITION_STANDING) && !GET_HIDING(host) || IS_SET(ch->specials2.hide_flags, HIDING_SNUCK_IN)) && host->delay.wait_value == 0) {
            do_hide(host, "", 0, 0, 0);
        }

        if ((callflag == SPECIAL_ENTER) && ch && !is_wimpy && !PRF_FLAGGED(ch, PRF_NOHASSLE) && (IS_AGGR_TO(host, ch) || IS_SET(ch->specials2.act, MOB_AGGRESSIVE))) {
            tmpch = ch;
        } else if (callflag == SPECIAL_SELF && !is_wimpy) {
            // should this pick a random target, instead of first? or if MOB_SWITCHING??
            for (tmpch = world[host->in_room].people; tmpch; tmpch = tmpch->next_in_room) {
                if (should_attack(host, tmpch)) {
                    break;
                }
            }
        }
    }

    /* Ambush */
    if (tmpch && tmpch != host && strstr(ch->player.name, "stab") && is_not_engaged && !tmpch->specials.fighting && !is_wimpy) {
        do_spec_ambush(host, tmpch);
        return 1;
    }
    // else attack
    if (tmpch && tmpch != host && is_not_engaged && !is_wimpy) {
        do_spec_hit(host, tmpch);
        return 1;
    }

    // MOB_SWITCHING
    if (IS_SET(host->specials2.act, MOB_SWITCHING) && host->specials.fighting && !is_wimpy) {
        for (tmpch = world[ch->in_room].people; tmpch; tmpch = tmpch->next_in_room) {
            // also tgt someone invis ??
            if (tmpch->specials.fighting == host && !number(0, 3) && should_attack(host, tmpch) && tmpch != host->specials.fighting) {
                host->specials.fighting = tmpch;
                act("$n turns to fight $N!", TRUE, host, 0, tmpch, TO_ROOM);
                break;
            }
        }
    }

    // MEMORY / HUNTER
    if (prog_do_hunter(host, is_wimpy)) {
        vict = 0;
        for (names = ch->specials.memory; names && !vict; names = names->next_on_mob) {
            if (names->enemy && char_exists(names->enemy_number) && (names->enemy->in_room == ch->in_room) && CAN_SEE(ch, names->enemy)) {
                vict = names->enemy;
            }
        }
        if (vict) {
            if (ch->master == vict) {
                forget(ch, vict);
                // below needed: these attacks only occurr if mob_memory and not otherwise aggr to
            } else {
                if (should_attack(host, tmpch) && strstr(ch->player.name, "stab") && is_not_engaged && !vict->specials.fighting) {
                    do_spec_ambush(host, tmpch);
                    return 1;
                }
                if (should_attack(host, tmpch) && is_not_engaged) {
                    do_spec_hit(host, tmpch);
                    return 1;
                }
            }
        }
        if (IS_SET(ch->specials2.act, MOB_HUNTER) || IS_AFFECTED(ch, AFF_HUNT)) {
            int modifier = 0;
            if (IS_AFFECTED(ch, AFF_CONFUSE)) {
                modifier = get_confuse_modifier(ch);
            }

            for (names = ch->specials.memory; names && !vict; names = names->next_on_mob) {
                if (names->enemy && char_exists(names->enemy_number) && (names->enemy->in_room != NOWHERE) && (number(0, 100) > modifier)) {
                    tmp = find_first_step(ch->in_room, names->enemy->in_room);
                } else {
                    tmp = BFS_NO_PATH;
                }
                if (tmp >= 0) {
                    do_move(ch, "", 0, tmp + 1, 0);
                    do_hide(host, "", 0, 0, 0);
                    host->spec_busy = false;
                    return 1;
                }
            }
        }
    } /* mob memory/hunter */

    tmproom = &world[host->in_room];
    mintime = 999;
    mintmp = NUM_OF_TRACKS;
    if (ch->delay.wait_value == 0 && is_not_engaged) {
        // TRACKING
        if (strstr(ch->player.name, "hunt") && !is_wimpy) {
            for (tmp = 0; tmp < NUM_OF_TRACKS; tmp++) {
                tmp2 = (24 + time_info.hours - tmproom->room_track[tmp].data / 8) % 24;
                dir = (tmproom->room_track[tmp].data & 7); // the direction
                room_data exit_room;

                if (CAN_GO(host, dir)) {
                    exit_room = world[EXIT(host, dir)->to_room];
                }

                if ((tmproom->room_track[tmp].char_number < 0) && ((racial_aggr & (1 << -tmproom->room_track[tmp].char_number)) || (is_aggressive && (1 << -tmproom->room_track[tmp].char_number) <= PLAYER_RACE_MAX)) && tmp2 < mintime && !IS_SET(exit_room.room_flags, NO_MOB)) {
                    // NOTE: these get set on every true pass, so it would be the last matching
                    // track, unless sorting was done on the random order of tracks
                    mintime = tmp2;
                    mintmp = tmp;
                }
            }

            if (mintmp < NUM_OF_TRACKS) {
                tmpwtl.cmd = (tmproom->room_track[mintmp].data & 7) + 1;
                tmpwtl.subcmd = 0;
                do_move(host, "", &tmpwtl, tmpwtl.cmd, 0);
                if (((GET_POS(host) == POSITION_STANDING) && !GET_HIDING(host) || IS_SET(ch->specials2.hide_flags, HIDING_SNUCK_IN)) && host->delay.wait_value == 0) {
                    do_hide(host, "", 0, 0, 0);
                }
                tmpwtl.targ1.cleanup();
                ch->spec_busy = false;
                return 1;
            }
        }

        // default: try move and hide again
        if (!IS_SET(host->specials2.act, MOB_SENTINEL) && GET_POS(host) == POSITION_STANDING) {
            tmpwtl.cmd = number(1, NUM_OF_DIRS);
            tmpwtl.subcmd = 0;
            if (CAN_GO(host, tmpwtl.cmd - 1)) {
                do_move(host, "", &tmpwtl, tmpwtl.cmd, tmpwtl.subcmd);
                if (((GET_POS(host) == POSITION_STANDING) && !GET_HIDING(host) || IS_SET(ch->specials2.hide_flags, HIDING_SNUCK_IN)) && host->delay.wait_value == 0) {
                    do_hide(host, "", 0, 0, 0);
                }
                tmpwtl.targ1.cleanup();
                return 1;
            }
            tmpwtl.targ1.cleanup();
        }
    }

    ch->spec_busy = false;
    return 1; //-- we are cutting off the rest of one_mobile_activity to ensure that the prog has a
              // consistant flow
}

/*
 * Removed a large switch statment and replaced it with this
 * array for sending dancing signals to players
 * and to the room.
 */

char* dance_description[][2] = {
    { "jumps merrily up and down, dancing happily.", "You happily jump up and down" },
    { "steps in a little circle, clapping $s hands together",
        "You step in a little circle, clapping your hands together" },
    { "advances a little forward, then retreats, humming the tune.",
        "You advance a little forward, then retreat, humming the tune" },
    { "red in face, speeds up the temp, tapping out $s rhythm.",
        "You speed up the temp, tapping out the rhythm" },
    { "waves happily, inviting you to join $s dance",
        "You look around, hoping to find a dance partner" },
    { "is tired and breathes heavily, but continues $s dance.",
        "You wipe your sweaty face, tired, but dancing is so much fun" }
};

SPECIAL(mob_jig)
{
    int dance_desc;

    if ((callflag == SPECIAL_COMMAND) && (ch == host) && ((cmd < 0) || (cmd_info[cmd].minimum_position > POSITION_RESTING))) {

        if (cmd == CMD_JIG)
            return FALSE;

        host->specials.store_prog_number = 0;
        act("$n interrupts $s dance.", TRUE, host, 0, 0, TO_ROOM);
        send_to_char("You interrupt your dance.\n\r", host);
        return FALSE;
    }

    if (GET_POS(host) < POSITION_STANDING) {
        host->specials.store_prog_number = 0;
        return FALSE;
    }

    if ((callflag != SPECIAL_SELF) || (host->in_room == NOWHERE))
        return FALSE;

    dance_desc = number(0, 5);
    sprintf(buf, "$n %s.\r\n", dance_description[dance_desc][0]);
    sprintf(buf1, "%s.\r\n", dance_description[dance_desc][1]);
    act(buf, TRUE, host, 0, 0, TO_ROOM);
    send_to_char(buf1, ch);

    return FALSE;
}

int get_exit_width(room_data* room, int exit);

/*
 * Well in answer to Vakaurs' prayers i've removed
 * our six block_exit functions and replaced them
 * with one.
 */

#define BLOCK_CHANCE(host, ch, width) \
    (((GET_SKILL(host, SKILL_BLOCK) + 110) / (1 + width) / (CAN_SEE(host, ch) ? 1 : 2) / (GET_POS(host) < POSITION_STANDING ? 3 : 1) - 3 * width) * GET_WEIGHT(host) / (GET_WEIGHT(ch) + 10 * GET_DEX(ch)))

/*
SPECIAL(block_exit) {

  int width, chance;

  if(callflag == SPECIAL_COMMAND && ch == host &&
     cmd < 0 || cmd_info[cmd].minimum_position > POSITION_RESTING) {
    if(cmd == CMD_BLOCK)
      return FALSE;
    if(!CAN_GO(ch, 0))
      return FALSE;
    ch->specials.store_prog_number = 0;
    act("$n stopped blocking the way north.",TRUE, ch, 0, 0, TO_ROOM);
    send_to_char("You stopped blocking the way north.\n\r",ch);
    return FALSE;
  }

  if((callflag != SPECIAL_COMMAND) || (cmd != host->specials2.rp_flag + 1))
    return FALSE;

  if(ch->in_room == NOWHERE) {
    ch->specials.store_prog_number = 0;
    return FALSE;
  }

  width = get_exit_width(&world[ch->in_room], host->specials2.rp_flag);
  if(width <= 0){
    ch->specials.store_prog_number = 0;
    return FALSE;
  }

  chance = BLOCK_CHANCE(host, ch, width);

  if(number(1,100) < chance){
    act("$n tried to slip north past $N but could not.", TRUE, ch, 0, host, TO_NOTVICT);
    act("$n tried to move north past you, but failed.", FALSE, ch, 0, host, TO_VICT);
    act("$N blocks your way north.", FALSE, ch, 0, host, TO_CHAR);
    return TRUE;
  }
  else{
    act("$n slips north past $N.", TRUE, ch, 0, host, TO_NOTVICT);
    act("$n slips north past you.", FALSE, ch, 0, host, TO_VICT);
    act("You slip north past $N.", FALSE, ch, 0, host, TO_CHAR);
    return FALSE;
  }
}
*/

SPECIAL(block_exit_north)
{
    int width, chance;

    if ((callflag == SPECIAL_COMMAND) && (ch == host) && ((cmd < 0) || (cmd_info[cmd].minimum_position > POSITION_RESTING))) {

        if (cmd == CMD_BLOCK)
            return FALSE;
        if (!CAN_GO(ch, 0))
            return FALSE;

        ch->specials.store_prog_number = 0;
        act("$n stopped blocking the way north.", TRUE, ch, 0, 0, TO_ROOM);
        send_to_char("You stopped blocking the way north.\n\r", ch);
        return FALSE;
    }

    if ((callflag != SPECIAL_COMMAND) || (cmd != 1))
        return FALSE;

    if (ch->in_room == NOWHERE) {
        ch->specials.store_prog_number = 0;
        return FALSE;
    }

    width = get_exit_width(&world[ch->in_room], 0);
    if (width <= 0) {
        ch->specials.store_prog_number = 0;
        return FALSE;
    }

    chance = BLOCK_CHANCE(host, ch, width);

    if (number(1, 100) < chance) {
        act("$n tried to slip north past $N but could not.", TRUE, ch, 0, host, TO_NOTVICT);
        act("$n tried to move north past you, but failed.", FALSE, ch, 0, host, TO_VICT);
        act("$N blocks your way north.", FALSE, ch, 0, host, TO_CHAR);
        return TRUE;
    } else {
        act("$n slips north past $N.", TRUE, ch, 0, host, TO_NOTVICT);
        act("$n slips north past you.", FALSE, ch, 0, host, TO_VICT);
        act("You slip north past $N.", FALSE, ch, 0, host, TO_CHAR);
        return FALSE;
    }
}

SPECIAL(block_exit_east)
{
    int width, chance;

    if ((callflag == SPECIAL_COMMAND) && (ch == host) && ((cmd < 0) || (cmd_info[cmd].minimum_position > POSITION_RESTING))) {

        if (cmd == CMD_BLOCK)
            return FALSE;
        if (!CAN_GO(ch, 0))
            return FALSE;

        ch->specials.store_prog_number = 0;
        act("$n stopped blocking the way east.", TRUE, ch, 0, 0, TO_ROOM);
        send_to_char("You stopped blocking the way east.\n\r", ch);
        return FALSE;
    }

    if ((callflag != SPECIAL_COMMAND) || (cmd != 2))
        return FALSE;

    if (ch->in_room == NOWHERE) {
        ch->specials.store_prog_number = 0;
        return FALSE;
    }

    width = get_exit_width(&world[ch->in_room], 1);
    if (width <= 0) {
        ch->specials.store_prog_number = 0;
        return FALSE;
    }

    chance = BLOCK_CHANCE(host, ch, width);

    if (number(1, 100) < chance) {
        act("$n tried to slip east past $N but could not.", TRUE, ch, 0, host, TO_NOTVICT);
        act("$n tried to move east past you, but failed.", FALSE, ch, 0, host, TO_VICT);
        act("$N blocks your way east.", FALSE, ch, 0, host, TO_CHAR);
        return TRUE;
    } else {
        act("$n slips east past $N.", TRUE, ch, 0, host, TO_NOTVICT);
        act("$n slips east past you.", FALSE, ch, 0, host, TO_VICT);
        act("You slip east past $N.", FALSE, ch, 0, host, TO_CHAR);
        return FALSE;
    }
}
SPECIAL(block_exit_south)
{
    int width, chance;

    if ((callflag == SPECIAL_COMMAND) && (ch == host) && ((cmd < 0) || (cmd_info[cmd].minimum_position > POSITION_RESTING))) {

        if (cmd == CMD_BLOCK)
            return FALSE;
        if (!CAN_GO(ch, 0))
            return FALSE;

        ch->specials.store_prog_number = 0;
        act("$n stopped blocking the way south.", TRUE, ch, 0, 0, TO_ROOM);
        send_to_char("You stopped blocking the way south.\n\r", ch);
        return FALSE;
    }

    if ((callflag != SPECIAL_COMMAND) || (cmd != 3))
        return FALSE;

    if (ch->in_room == NOWHERE) {
        ch->specials.store_prog_number = 0;
        return FALSE;
    }

    width = get_exit_width(&world[ch->in_room], 2);
    if (width <= 0) {
        ch->specials.store_prog_number = 0;
        return FALSE;
    }

    chance = BLOCK_CHANCE(host, ch, width);

    if (number(1, 100) < chance) {
        act("$n tried to slip south past $N but could not.", TRUE, ch, 0, host, TO_NOTVICT);
        act("$n tried to move south past you, but failed.", FALSE, ch, 0, host, TO_VICT);
        act("$N blocks your way south.", FALSE, ch, 0, host, TO_CHAR);
        return TRUE;
    } else {
        act("$n slips south past $N.", TRUE, ch, 0, host, TO_NOTVICT);
        act("$n slips south past you.", FALSE, ch, 0, host, TO_VICT);
        act("You slip south past $N.", FALSE, ch, 0, host, TO_CHAR);
        return FALSE;
    }
}
SPECIAL(block_exit_west)
{
    int width, chance;

    if ((callflag == SPECIAL_COMMAND) && (ch == host) && ((cmd < 0) || (cmd_info[cmd].minimum_position > POSITION_RESTING))) {

        if (cmd == CMD_BLOCK)
            return FALSE;
        if (!CAN_GO(ch, 0))
            return FALSE;

        ch->specials.store_prog_number = 0;
        act("$n stopped blocking the way west.", TRUE, ch, 0, 0, TO_ROOM);
        send_to_char("You stopped blocking the way west.\n\r", ch);
        return FALSE;
    }

    if ((callflag != SPECIAL_COMMAND) || (cmd != 4))
        return FALSE;

    if (ch->in_room == NOWHERE) {
        ch->specials.store_prog_number = 0;
        return FALSE;
    }

    width = get_exit_width(&world[ch->in_room], 3);
    if (width <= 0) {
        ch->specials.store_prog_number = 0;
        return FALSE;
    }

    chance = BLOCK_CHANCE(host, ch, width);

    if (number(1, 100) < chance) {
        act("$n tried to slip west past $N but could not.", TRUE, ch, 0, host, TO_NOTVICT);
        act("$n tried to move west past you, but failed.", FALSE, ch, 0, host, TO_VICT);
        act("$N blocks your way west.", FALSE, ch, 0, host, TO_CHAR);
        return TRUE;
    } else {
        act("$n slips west past $N.", TRUE, ch, 0, host, TO_NOTVICT);
        act("$n slips west past you.", FALSE, ch, 0, host, TO_VICT);
        act("You slip west past $N.", FALSE, ch, 0, host, TO_CHAR);
        return FALSE;
    }
}
SPECIAL(block_exit_up)
{
    int width, chance;

    if ((callflag == SPECIAL_COMMAND) && (ch == host) && ((cmd < 0) || (cmd_info[cmd].minimum_position > POSITION_RESTING))) {

        if (cmd == CMD_BLOCK)
            return FALSE;
        if (!CAN_GO(ch, 0))
            return FALSE;

        ch->specials.store_prog_number = 0;
        act("$n stopped blocking the way up.", TRUE, ch, 0, 0, TO_ROOM);
        send_to_char("You stopped blocking the way up.\n\r", ch);
        return FALSE;
    }

    if ((callflag != SPECIAL_COMMAND) || (cmd != 5))
        return FALSE;

    if (ch->in_room == NOWHERE) {
        ch->specials.store_prog_number = 0;
        return FALSE;
    }

    width = get_exit_width(&world[ch->in_room], 4);
    if (width <= 0) {
        ch->specials.store_prog_number = 0;
        return FALSE;
    }

    chance = BLOCK_CHANCE(host, ch, width);

    if (number(1, 100) < chance) {
        act("$n tried to slip up past $N but could not.", TRUE, ch, 0, host, TO_NOTVICT);
        act("$n tried to move up past you, but failed.", FALSE, ch, 0, host, TO_VICT);
        act("$N blocks your way up.", FALSE, ch, 0, host, TO_CHAR);
        return TRUE;
    } else {
        act("$n slips up past $N.", TRUE, ch, 0, host, TO_NOTVICT);
        act("$n slips up past you.", FALSE, ch, 0, host, TO_VICT);
        act("You slip up past $N.", FALSE, ch, 0, host, TO_CHAR);
        return FALSE;
    }
}
SPECIAL(block_exit_down)
{
    int width, chance;

    if ((callflag == SPECIAL_COMMAND) && (ch == host) && ((cmd < 0) || (cmd_info[cmd].minimum_position > POSITION_RESTING))) {

        if (cmd == CMD_BLOCK)
            return FALSE;
        if (!CAN_GO(ch, 0))
            return FALSE;

        ch->specials.store_prog_number = 0;
        act("$n stopped blocking the way down.", TRUE, ch, 0, 0, TO_ROOM);
        send_to_char("You stopped blocking the way down.\n\r", ch);
        return FALSE;
    }

    if ((callflag != SPECIAL_COMMAND) || (cmd != 6))
        return FALSE;

    if (ch->in_room == NOWHERE) {
        ch->specials.store_prog_number = 0;
        return FALSE;
    }

    width = get_exit_width(&world[ch->in_room], 5);
    if (width <= 0) {
        ch->specials.store_prog_number = 0;
        return FALSE;
    }

    chance = BLOCK_CHANCE(host, ch, width);

    if (number(1, 100) < chance) {
        act("$n tried to slip down past $N but could not.", TRUE, ch, 0, host, TO_NOTVICT);
        act("$n tried to move down past you, but failed.", FALSE, ch, 0, host, TO_VICT);
        act("$N blocks your way down.", FALSE, ch, 0, host, TO_CHAR);
        return TRUE;
    } else {
        act("$n slips down past $N.", TRUE, ch, 0, host, TO_NOTVICT);
        act("$n slips down past you.", FALSE, ch, 0, host, TO_VICT);
        act("You slip down past $N.", FALSE, ch, 0, host, TO_CHAR);
        return FALSE;
    }
}

SPECIAL(resetter)
{
    struct follow_type* tmpfol;
    struct waiting_type wtltmp;
    int reroll_count;

    if (callflag != SPECIAL_TARGET || !wtl)
        return 0;

    if (wtl->targ1.ptr.ch != host || wtl->targ2.type != TARGET_TEXT)
        return 0;

    if (!str_cmp(wtl->targ2.ptr.text->text, "pracreset")) {
        wtltmp.targ1.type = TARGET_CHAR;
        wtltmp.targ1.ptr.ch = ch;
        wtltmp.targ1.ch_num = ch->abs_number;
        wtltmp.cmd = CMD_PRACRESET;
        wtltmp.subcmd = 0;

        for (tmpfol = ch->followers; tmpfol; tmpfol = ch->followers)
            stop_follower(tmpfol->follower, FOLLOW_MOVE);
        do_pracreset(host, "", &wtltmp, CMD_PRACRESET, 0);

        return 0;

    } else if (!str_cmp(wtl->targ2.ptr.text->text, "reroll")) {
        wtltmp.targ1.type = TARGET_CHAR;
        wtltmp.targ1.ptr.ch = ch;
        wtltmp.targ1.ch_num = ch->abs_number;
        if (GET_LEVEL(ch) == 6) {
            if (ch->specials2.rerolls >= 41) {
                wtltmp.targ2.type = TARGET_TEXT;
                wtltmp.targ2.ptr.text = get_from_txt_block_pool(
                    "Sorry, you have exceeded the amount of times you can reroll statistics.");
                wtltmp.cmd = CMD_TELL;
                wtltmp.subcmd = 0;
                do_tell(host, "", &wtltmp, CMD_TELL, 0);
                wtltmp.targ2.cleanup();
                return 0;
            }
            send_to_char("Rerolling statistics...\n\r", ch);
            roll_abilities(ch, 80, 93);
            ch->specials2.rerolls += 1;
            reroll_count = 41 - ch->specials2.rerolls;
            sprintf(buf, "You have %d reroll attempts left.\n\r", reroll_count);
            send_to_char(buf, ch);
            do_stat(ch, "", 0, 0, 0);
            return 0;
        } else {
            wtltmp.targ2.type = TARGET_TEXT;
            wtltmp.targ2.ptr.text = get_from_txt_block_pool("Only level 6 characters may reroll statistics.");
            wtltmp.cmd = 0;
            do_tell(host, "", &wtltmp, CMD_TELL, 0);
            wtltmp.targ2.cleanup();
            return 0;
        }
    } else {
        wtltmp.targ1.type = TARGET_CHAR;
        wtltmp.targ1.ptr.ch = ch;
        wtltmp.targ1.ch_num = ch->abs_number;
        wtltmp.targ2.type = TARGET_TEXT;
        wtltmp.targ2.ptr.text = get_from_txt_block_pool("You may ask me for a pracreset and rerolls.");
        wtltmp.cmd = CMD_TELL;
        wtltmp.subcmd = 0;
        do_tell(host, "", &wtltmp, CMD_TELL, 0);
        wtltmp.targ2.cleanup();
        return 0;
    }

    return 0;
}

/*
 * host      The player who set the trap.
 * ch        The player who triggered the special.
 * cmd       If ch entered host's room, then cmd is the direction
 *           ch entered from.  Otherwise, host typed in a command
 *           and cmd is its command index in cmd_info.
 * arg       If ch entered host's room, then arg is the null string.
 *           If host typed a command, it's the argument to host's
 *           command.
 * callflag  The callflag that triggered this script.  We only care
 *           about SPECIAL_ENTER and SPECIAL_commandXXX.
 * wtl       If ch entered host's room an triggered the trap, then
 *           this is NULL.  Otherwise, it's the wtl for a command
 *           entered by host.
 */
SPECIAL(react_trap)
{
    struct waiting_type w;
    ACMD(do_trap);
    int target_check(struct char_data*, int, struct target_data*, struct target_data*);

    if (callflag != SPECIAL_COMMAND && callflag != SPECIAL_ENTER)
        return FALSE;

    /* See if they abandon their trap */
    if (callflag == SPECIAL_COMMAND) {
        if (cmd_info[cmd].minimum_position > POSITION_SITTING && ch == host && cmd != CMD_TRAP)
            do_trap(host, "", NULL, CMD_TRAP, -1);
        return FALSE;
    }

    /* Make a copy of host->delay and add ch as targ2 */
    w = host->delay;
    w.targ2.type = TARGET_CHAR;
    w.targ2.ch_num = GET_ABS_NUM(ch); /* Not necessary */
    w.targ2.ptr.ch = ch;

    /*
     * XXX: What to do here: if I call command_interpreter, I
     * get some proper processing on 'w' to make sure the args
     * are valid.  But then command_interpreter will call this
     * again with ch = host = the trapper and cmd = 220 (TRAP).
     * Then I have to make the trap command not break the set
     * trap.  Can this be dealt with in do_trap()?
     */
    target_check(host, CMD_TRAP, &w.targ1, &w.targ2);
    do_trap(host, "", &w, CMD_TRAP, w.subcmd);

    return FALSE;
}

SPECIAL(ar_tarthalon)
{ // Ar-Tarthalon summons help when fighting
    char_data* victim; // in the form of 2 more wights from nearby
    waiting_type tmpwtl;

    if (GET_POS(host) != POSITION_FIGHTING)
        return FALSE;
    if (number(0, 1))
        return FALSE;
    if (number(0, 1)) {
        victim = get_char("balkazor");
        if (victim)
            if (world[victim->in_room].number != 8483)
                do_say(host, "Balkazor my sorcerer, I command you to aid me!", 0, 0, 0);
            else
                switch (number(0, 6)) {
                case 4:
                    do_say(host, "Turn these intruders to dust, my faithful wizard!", 0, 0, 0);
                    break;
                case 5:
                    do_say(host, "Balkazor, burn these infidels into eternity!", 0, 0, 0);
                    break;
                }
    } else {
        victim = get_char("karahaz");
        if (victim)
            if (world[victim->in_room].number != 8483)
                do_say(host, "Karahaz!  Master swordsman, Come!  Rid my tomb of these intruders!",
                    0, 0, 0);
            else
                switch (number(0, 6)) {
                case 4:
                    do_say(host, "Karahaz, may these mere mortals feel the power of your blade!", 0,
                        0, 0);
                    break;
                case 5:
                    do_say(host, "I require your service once again Karahaz - do not fail me.", 0,
                        0, 0);
                    break;
                }
    }
    if (!victim)
        return 0;
    if (GET_POS(victim) == POSITION_STANDING) {
        switch (world[victim->in_room].number) {
        case 8484:
            tmpwtl.cmd = 3;
            do_unlock(victim, "northdoor", 0, 0, 0);
            do_open(victim, "northdoor", 0, 0, 0);
            break;
        case 8485:
            tmpwtl.cmd = 1;
            do_unlock(victim, "southdoor", 0, 0, 0);
            do_open(victim, "southdoor", 0, 0, 0);
            break;
        case 8482:
            tmpwtl.cmd = 2;
            break;
        case 8481:
            tmpwtl.cmd = 2;
            break;
        case 8480:
            tmpwtl.cmd = 3;
            break;
        case 8486:
            tmpwtl.cmd = 5;
            break;
        case 8437:
            tmpwtl.cmd = 6;
            break;
        case 15286:
            tmpwtl.cmd = 4;
            break;
        case 15285:
            tmpwtl.cmd = 4;
            break;
        case 15284:
            tmpwtl.cmd = 4;
            break;
        default:
            tmpwtl.cmd = 0;
        }
        tmpwtl.subcmd = 0;
        if (CAN_GO(victim, tmpwtl.cmd - 1))
            do_move(victim, "", &tmpwtl, tmpwtl.cmd, 0);
        tmpwtl.targ1.cleanup();
    }

    return 0;
}

/*
 * In true horror tradition, more and more ghouls appear
 * until the character is forced to flee
 * If the spawned Ghouls are not fighting they
 * return to the ground whence they came
 */
SPECIAL(ghoul)
{
    struct char_data* mob;
    int tmpno, num_followers = 0;
    struct follow_type *j, *k;
    obj_data* obj;

    if (GET_POS(host) != POSITION_FIGHTING) {
        if (host->followers && !number(0, 10)) {
            mob = host->followers->follower;
            if (!mob->followers) {
                act("$n seems to fade away into the ground.", TRUE, host, 0, 0, TO_ROOM);
                for (tmpno = 0; tmpno < MAX_WEAR; tmpno++)
                    if (mob->equipment[tmpno])
                        extract_obj(unequip_char(mob, tmpno));
                for (tmpno = 0; (tmpno < 1000) && host->carrying; tmpno++) {
                    obj = mob->carrying;
                    obj_from_char(mob->carrying);
                    extract_obj(obj);
                }
                extract_char(mob);
            }
        }
        return 0;
    }
    if (number(0, 30))
        return 0;
    for (k = host->followers; k; k = j) {
        j = k->next;
        num_followers++;
    }
    if (num_followers < 3) {
        act("Decaying hands claw their way out of the ground as $n emerges.", TRUE, host, 0, 0,
            TO_ROOM);
        mob = read_mobile(real_mobile(15300), REAL);
        char_to_room(mob, host->in_room);
        add_follower(mob, host, FOLLOW_MOVE);
    }
    return 0;
}

SPECIAL(swarm) { return 0; }

SPECIAL(dragon)
{
    struct char_data *tmpch, *tmpch_next;
    int dam_value, mob_level;

    if (GET_POS(host) != POSITION_FIGHTING) {
        return 0;
    }

    if (number(0, 4)) {
        return 0;
    }

    mob_level = GET_LEVEL(host) / 2;

    dam_value = dice(mob_level, 6);

    for (tmpch = world[host->in_room].people; tmpch; tmpch = tmpch_next) {
        tmpch_next = tmpch->next_in_room;
        if (tmpch != host) {
            damage(host, tmpch, dam_value, SPELL_DRAGONSBREATH, 0);
        }
    }
    return 0;
}

SPECIAL(vampire_huntress)
{ // Thuringwethil - in bat form she hunts through her zone
    struct char_data *victim, *mob; // quite quickly.  If she comes across a PC she will either
    waiting_type tmpwtl; // continue on her way, attack, or abduct the victim taking
    int to_room, tmpno; // them back to the dungeons of her tower.
    struct affected_type af;
    obj_data* obj;
    room_data* room;
    // If she is too badly hurt in a fight, she will flee back to
    // her tower and change into human form and regenerate.  Once
    if (host->in_room == NOWHERE) // healed she will once again assume bat form and hunt her
        return 0; // zone.  NB she returns to the safety of her tower during the
    if ((GET_POS(host) != POSITION_FIGHTING) && (!IS_SET(host->specials.affected_by, AFF_BASH))) { // day.
        switch (world[host->in_room].number) {
        case 15375:
            to_room = 15374;
            break;
        case 15374:
            to_room = 15373;
            break;
        case 15373:
            to_room = 15363;
            break;
        case 15363:
            to_room = 15362;
            break;
        case 15362:
            to_room = 15352;
            break;
        case 15352:
            to_room = 15351;
            break;
        case 15351:
            to_room = 15350;
            break;
        case 15350:
            to_room = 15342;
            break;
        case 15342:
            to_room = 15343;
            break;
        case 15343:
            to_room = 15333;
            break;
        case 15333:
            to_room = 15332;
            break;
        case 15332:
            to_room = 15331;
            break;
        case 15331:
            to_room = 15321;
            break;
        case 15321:
            to_room = 15311;
            break;
        case 15311:
            to_room = 15310;
            break;
        case 15310:
            to_room = 15334;
            break;
        case 15334:
            to_room = 15335;
            break;
        case 15335:
            to_room = 15336;
            break;
        case 15336:
            to_room = 15337;
            break;
        case 15337:
            to_room = 15344;
            break;
        case 15344:
            to_room = 15345;
            break;
        case 15345:
            to_room = 15346;
            break;
        case 15346:
            to_room = 15347;
            break;
        case 15347:
            to_room = 15349;
            break;
        case 15349:
            to_room = 15359;
            break;
        case 15359:
            to_room = 15369;
            break;
        case 15369:
            to_room = 15368;
            break;
        case 15368:
            to_room = 15367;
            break;
        case 15367:
            to_room = 15377;
            break;
        case 15377:
            to_room = 15376;
            break;
        case 15376:
            to_room = 15375;
            break;
        default:
            to_room = 15375;
            break;
        }
        if (weather_info.sunlight == SUN_LIGHT)
            to_room = 0;
        if (weather_info.sunlight == SUN_RISE)
            to_room = 15379;
        if (!to_room)
            return 0;
        act("$n flies away through the trees.", FALSE, host, 0, 0, TO_ROOM);
        char_from_room(host);
        char_to_room(host, real_room(to_room));
        act("$n enters, her massive wings darkening the forest.", FALSE, host, 0, 0, TO_ROOM);
        tmpno = number(0, 2);
        if (tmpno) { // 33% she will fly off, 33% attack, 33% kidnap
            victim = 0;
            for (victim = world[host->in_room].people; victim; victim = victim->next_in_room)
                if ((victim != host) && CAN_SEE(host, victim) && !IS_NPC(victim))
                    break;
            if (victim && GET_LEVEL(victim) < LEVEL_GOD) {
                if (tmpno == 1) {
                    if (IS_RIDING(victim))
                        stop_riding(victim);
                    act("$n swoops down suddenly and you find yourself carried away!!", FALSE, host,
                        0, victim, TO_VICT);
                    act("$n is carried away by $N!!", FALSE, victim, 0, host, TO_ROOM);
                    char_from_room(victim);
                    if (victim->player.race > 5)
                        char_to_room(victim, real_room(15399));
                    else
                        char_to_room(victim, real_room(15398));
                    char_from_room(host);
                    char_to_room(host, real_room(15379));
                    send_to_char("You are carried at great speed through the trees.\n\r\n", victim);
                    send_to_char("Branches and trees all flash past, and you are powerless to "
                                 "prevent it.\n\r\n",
                        victim);
                    act("$n bites you!", FALSE, host, 0, victim, TO_VICT);
                    af.type = SPELL_POISON; // replace with more powerful poison when coded
                    af.duration = 24;
                    af.modifier = -4;
                    af.location = APPLY_STR;
                    af.bitvector = AFF_POISON;
                    affect_join(victim, &af, FALSE, FALSE);
                    send_to_char("You feel extremely sick.\n\r", victim);
                    send_to_char(
                        "For a moment the pain is too great and you lose consciousness...\n\r\n\n",
                        victim);
                    GET_HIT(victim) = GET_HIT(victim) * 4 / 5;
                    send_to_char(
                        "You come to lying on a stone floor, bruised but still alive!.\n\r\n",
                        victim);
                    act("$n seems to fall through the roof to land painfully on the floor.", FALSE,
                        victim, 0, 0, TO_ROOM);
                    GET_POS(victim) = POSITION_RESTING;
                    room = &world[victim->in_room];
                    SET_BIT(room->dir_option[0]->exit_info, EX_CLOSED);
                    SET_BIT(room->dir_option[0]->exit_info, EX_LOCKED);
                    send_to_room("The iron door slams shut.\n\r\n", real_room(room->number));
                    room = &world[room->dir_option[0]->to_room];
                    SET_BIT(room->dir_option[2]->exit_info, EX_CLOSED);
                    SET_BIT(room->dir_option[2]->exit_info, EX_LOCKED);
                    send_to_room("The iron door slams shut.\n\r\n", real_room(room->number));
                    do_look(victim, "", wtl, 0, 0);
                    WAIT_STATE_FULL(host, 200, 0, 0, 59, 0, 0, 0, AFF_WAITING, TARGET_NONE);
                } else if (tmpno == 2 && GET_LEVEL(victim) < LEVEL_GOD)
                    hit(host, victim, TYPE_UNDEFINED);
            }
        }
    }
    if (GET_POS(host) == POSITION_FIGHTING && GET_HIT(host) < GET_MAX_HIT(host) / 5) {
        stop_fighting(host);
        act("$n flaps her great wings, and flies away.", FALSE, host, 0, 0, TO_ROOM);
        char_from_room(host);
        char_to_room(host, real_room(15379));
        for (tmpno = 0; tmpno < MAX_WEAR; tmpno++)
            if (host->equipment[tmpno])
                extract_obj(unequip_char(host, tmpno));
        for (tmpno = 0; (tmpno < 1000) && host->carrying; tmpno++) {
            obj = host->carrying;
            obj_from_char(host->carrying);
            extract_obj(obj);
        }
        mob = read_mobile(real_mobile(15302), REAL);
        GET_HIT(mob) = GET_HIT(host) * 3;
        extract_char(host); // Gives a [room_data..] error presumably when it returns - strange...
        char_to_room(mob, real_room(15379));
        if ((obj = read_object(6338, VIRT)))
            obj_to_char(obj, mob);
        if ((obj = read_object(5410, VIRT)))
            obj_to_char(obj, mob);
        if ((obj = read_object(6631, VIRT)))
            obj_to_char(obj, mob);
        if ((obj = read_object(6510, VIRT)))
            obj_to_char(obj, mob);
        do_wear(mob, "all", 0, 0, 0);
        tmpwtl.subcmd = 0;
        tmpwtl.cmd = 6;
        if (CAN_GO(mob, tmpwtl.cmd - 1))
            do_move(mob, "", &tmpwtl, tmpwtl.cmd, 0);
        tmpwtl.targ1.cleanup();
        host = mob; // Maybe this will avoid the error, pointing to the new mob - nope :-)
        return 1;
    }
    return 0;
}

/*
 * Thuringwethil in human form. Will regenerate
 * and hits very fast. If she heals completely she
 * will change back into a bat and patrol the area
 */
SPECIAL(thuringwethil)
{
    struct char_data *victim, *tmpch;
    obj_data* obj;
    int tmpno;
    waiting_type tmpwtl;

    for (obj = world[host->in_room].contents; obj; obj = obj->next_content)
        if (obj->item_number == real_object(7640))
            break;

    if (GET_HIT(host) <= 500) {
        do_say(host, "Curse you all, I will remember this outrage!\n\r", 0, 0, 0);
        if (obj)
            act("$n hides her face from the glowing plate!", FALSE, host, 0, 0, TO_ROOM);
        for (tmpch = combat_list; tmpch; tmpch = tmpch->next_fighting)
            if ((tmpch->specials.fighting == host) && (!(IS_NPC(tmpch))))
                add_exploit_record(EXPLOIT_ACHIEVEMENT, tmpch, 0, "Defeated the Pale Vampire");
        stop_fighting(host);
        act("$n seems to disappear, her clothing falls to the ground.", FALSE, host, 0, 0, TO_ROOM);
        extract_char(host);
        return 1;
    }

    /* Slow her regen down if the plate is in the room.*/

    if ((obj) && number(0, 2))
        return 0;

    GET_HIT(host) += 10;
    GET_HIT(host) = MIN(GET_HIT(host), GET_MAX_HIT(host));
    if (affected_by_spell(host, SPELL_POISON))
        affect_from_char(host, SPELL_POISON);
    if (affected_by_spell(host, SPELL_CONFUSE))
        affect_from_char(host, SPELL_CONFUSE);
    if (GET_POS(host) != POSITION_FIGHTING && GET_POS(host) != POSITION_RESTING && GET_HIT(host) == GET_MAX_HIT(host)) {
        act("$n melts away into the shadows.", FALSE, host, 0, 0, TO_ROOM);
        char_from_room(host);
        char_to_room(host, real_room(15379));
        for (tmpno = 0; tmpno < MAX_WEAR; tmpno++)
            if (host->equipment[tmpno])
                extract_obj(unequip_char(host, tmpno));
        for (tmpno = 0; (tmpno < 1000) && host->carrying; tmpno++) {
            obj = host->carrying;
            obj_from_char(host->carrying);
            extract_obj(obj);
        }
        extract_char(host); // strange... extract works here with no room_data errors
        victim = read_mobile(real_mobile(15301), REAL);
        char_to_room(victim, real_room(15379));
        /*    tmpwtl.cmd = 6;
          tmpwtl.subcmd = 0;
          if(CAN_GO(victim, tmpwtl.cmd -1))
          do_move(victim,"", &tmpwtl, tmpwtl.cmd, 0);
          tmpwtl.targ1.cleanup();  Changed - Fingolfin, Dec 2001  */
        host = victim; // trying to avoid room_data errors when we return
        return 1;
    }
    return 0;
}

SPECIAL(vampire_doorkeep)
{
    struct char_data* victim;
    room_data *room, *room2;
    int close_it;

    close_it = 1;
    room = &world[real_room(15345)];
    room2 = &world[real_room(15355)];
    for (victim = room->people; victim; victim = victim->next_in_room)
        if ((victim != host) && CAN_SEE(host, victim) && !IS_NPC(victim))
            if (victim->equipment[WEAR_LIGHT])
                if (victim->equipment[WEAR_LIGHT]->item_number == real_object(7008))
                    close_it = 0;
    for (victim = room2->people; victim; victim = victim->next_in_room)
        if ((victim != host) && CAN_SEE(host, victim) && !IS_NPC(victim)) {
            if (victim->equipment[WEAR_LIGHT])
                if (victim->equipment[WEAR_LIGHT]->item_number == real_object(7008))
                    close_it = 0;
            if (!number(0, 100) && GET_POS(victim) != POSITION_FIGHTING)
                act("A huge wolf suddenly appears, charges at $n and vanishes!", FALSE, victim, 0,
                    0, TO_ROOM);
        }
    if (close_it)
        if (!IS_SET(room->dir_option[2]->exit_info, EX_CLOSED)) {
            SET_BIT(room->dir_option[2]->exit_info, EX_CLOSED);
            sprintf(buf, "The %s blurs for a second... then closes.\n\r",
                room->dir_option[2]->keyword);
            send_to_room(buf, real_room(15345));
            SET_BIT(room2->dir_option[0]->exit_info, EX_CLOSED);
            sprintf(buf, "The %s blurs for a second... then closes.\n\r",
                room2->dir_option[0]->keyword);
            send_to_room(buf, real_room(15355));
        }
    if (!close_it)
        if (IS_SET(room->dir_option[2]->exit_info, EX_CLOSED)) {
            REMOVE_BIT(room->dir_option[2]->exit_info, EX_CLOSED);
            sprintf(buf, "The %s blurs for a second... then opens..\n\r",
                room->dir_option[2]->keyword);
            send_to_room(buf, real_room(15345));
            REMOVE_BIT(room2->dir_option[0]->exit_info, EX_CLOSED);
            sprintf(buf, "The %s blurs for a second... then opens.\n\r",
                room2->dir_option[0]->keyword);
            send_to_room(buf, real_room(15355));
        }
    return 0;
}

SPECIAL(vampire_killer)
{
    struct char_data *victim, *victim2;
    room_data* room = 0;
    int which_room;
    waiting_type tmpwtl;

    for (victim = world[host->in_room].people; victim; victim = victim->next_in_room)
        if ((victim != host) && CAN_SEE(host, victim) && !IS_NPC(victim))
            break;
    if (victim && victim != host) {
        if (number(0, 50)) // Give them a chance to get away...
            return 0;
        act("$n leans over and bites $N on the neck!!", TRUE, host, 0, victim, TO_NOTVICT);
        act("$n leans over and bites you on the neck!!", FALSE, host, 0, victim, TO_VICT);
        send_to_char(
            "\n\rYou feel a moment of intense pain... as your numb body slumps to the floor.\n\r\n",
            victim);
        send_to_char("You are dead, sorry...\n\r", victim);
        sprintf(buf, "%s killed by %s at %s", GET_NAME(victim), GET_NAME(host),
            world[victim->in_room].name);
        mudlog(buf, BRF, LEVEL_GOD, TRUE);
        add_exploit_record(EXPLOIT_MOBDEATH, victim, GET_IDNUM(host), GET_NAME(host));
        raw_kill(victim, host, 0);
        return 0;
    }

    which_room = 0;
    tmpwtl.cmd = 0;
    for (victim = world[real_room(15399)].people; victim; victim = victim->next_in_room)
        if ((victim != host) && CAN_SEE(host, victim) && !IS_NPC(victim))
            break;
    if (victim)
        which_room = 1;
    for (victim2 = world[real_room(15398)].people; victim2; victim2 = victim2->next_in_room)
        if ((victim2 != host) && CAN_SEE(host, victim2) && !IS_NPC(victim2))
            break;
    if (victim2)
        which_room += 2;
    if (!which_room) {
        switch (world[host->in_room].number) { // If nothing to bite, get out of the cell
        case 15399:
            tmpwtl.cmd = 1;
            break;
        case 15398:
            tmpwtl.cmd = 1;
            break;
        default: {
            if (number(0, 1))
                tmpwtl.cmd = 2;
            else
                tmpwtl.cmd = 4;
            if (number(0, 15)) // So he doesn't run around down there
                return 0;
        }
        }
        tmpwtl.subcmd = 0;
        if (tmpwtl.cmd == 1) {
            if (IS_SET(room->dir_option[0]->exit_info, EX_CLOSED)) {
                do_unlock(host, "irondoor", 0, 0, 0);
                do_open(host, "irondoor", 0, 0, 0);
            }
        }
        if (CAN_GO(host, tmpwtl.cmd - 1))
            do_move(host, "", &tmpwtl, tmpwtl.cmd, 0);
        tmpwtl.targ1.cleanup();
        return 0;
    } else {
        if (number(0, 10))
            return 0; // Slow him down a bit
        if (which_room == 3)
            which_room = number(1, 2); // If both whities and darkies are there 50% which
        switch (world[host->in_room].number) { // one
        case 15395:
            tmpwtl.cmd = 2;
            break;
        case 15300:
            tmpwtl.cmd = 3;
            break;
        case 15302:
            tmpwtl.cmd = 2;
            break;
        case 15354:
            tmpwtl.cmd = 5;
            break;
        case 15396: {
            if (which_room == 1)
                tmpwtl.cmd = 2;
            else {
                room = &world[real_room(15396)];
                if (IS_SET(room->dir_option[2]->exit_info, EX_CLOSED)) {
                    do_unlock(host, "irondoor", 0, 0, 0);
                    do_open(host, "irondoor", 0, 0, 0);
                } else
                    tmpwtl.cmd = 3;
            }
            break;
        }
        case 15397: {
            if (which_room == 2)
                tmpwtl.cmd = 4;
            else {
                room = &world[real_room(15397)];
                if (IS_SET(room->dir_option[2]->exit_info, EX_CLOSED)) {
                    do_unlock(host, "irondoor", 0, 0, 0);
                    do_open(host, "irondoor", 0, 0, 0);
                } else
                    tmpwtl.cmd = 3;
            }
            break;
        }
        default:
            tmpwtl.cmd = 0;
        }
        tmpwtl.subcmd = 0;
        if (CAN_GO(host, tmpwtl.cmd - 1))
            do_move(host, "", &tmpwtl, tmpwtl.cmd, 0);
        tmpwtl.targ1.cleanup();
    }
    return 0;
}

SPECIAL(healing_plant)
{
    if (callflag != SPECIAL_SELF)
        return 0;

    int level = std::max(1, GET_LEVEL(host) / 2);

    for (char_data* character = world[host->in_room].people; character != NULL;
         character = character->next_in_room) {
        if (IS_GOOD(character) && host != character) {
            GET_HIT(character) = std::min(GET_MAX_HIT(character), GET_HIT(character) + number(1, level));
        }
    }

    return 0;
}

SPECIAL(vortex_elevator)
{ // For now, this little func serves the sole and only purpose ...
    struct char_data* tmpchar; // of allowing me (Aggrippa) to play with it and learn the mechanics.
    for (tmpchar = world[host->in_room].people; tmpchar;
         tmpchar = tmpchar->next_in_room) // for all players present in the room ...
        if (host != tmpchar) { // For all mobs in room, except vortex ...
            act("$n makes $N very dizzy!", TRUE, host, 0, tmpchar,
                TO_NOTVICT); // Tell everybody that 'tmpchar' is dizzy
            if (!IS_NPC(tmpchar))
                act("$n makes you very dizy!", FALSE, host, 0, tmpchar,
                    TO_VICT); // and tell it tmpchar , unless ofcourse he's a mob
        }
    return 0;
}

SPECIAL(wolf_summoner)
{
#define FOLLOWER GET_VNUM(host) + 1
#define MAX_FOLLOWER (GET_LEVEL(host) / 3)
#define CHANCE 2
    struct char_data* wolf;
    struct follow_type* f;
    int num_followers = 0;
    if ((GET_POS(host) == POSITION_FIGHTING) && (number(1, 10) > CHANCE)) {

        for (f = host->followers; f; f = f->next, num_followers++)
            ;

        // Don't load more than MAX_FOLLOWER helpers
        if (num_followers >= MAX_FOLLOWER)
            return 0;

        act("$n rises his head high and howls loudly into the caverns.", TRUE, host, 0, 0, TO_ROOM);
        wolf = read_mobile(real_mobile(FOLLOWER), REAL);
        if (!wolf) {
            mudlog("wolf_summoner: follower does not exist!", BRF, LEVEL_GOD, TRUE);
            return 0;
        };
        if (!wolf->player.death_cry2) {
            mudlog("wolf_summoner: follower has no death cry ", BRF, LEVEL_GOD, TRUE);
            return 0;
        }
        act(wolf->player.death_cry2, TRUE, host, 0, 0, TO_ROOM);

        // This is really a question I have not answered yet: can i free(death_cry2) or not? (yes!)

        RELEASE(wolf->player.death_cry2);
        wolf->player.death_cry2 = NULL;
        char_to_room(wolf, host->in_room);
        add_follower(wolf, host, FOLLOW_MOVE);
        // make wolf a follower
        if (host->specials.fighting) // always check you have a valid pointer.  Also
                                     // host->next_fighting points to the next character on the
                                     // fighting_list - not someone fighting them
            hit(wolf, host->specials.fighting, TYPE_UNDEFINED);
        // make wolf assist
    }
    return 0;
}

SPECIAL(reciter)
{ // reciting mob
    struct obj_data* o;
    int quitting = 0;
    char* e;
    char* c = host->specials.recite_lines;
    if (GET_HIT(ch) < 1) {
        quitting = 1;
        goto remove_scroll;
    } // Hm, sbdy just has spoken his last line...
    if ((GET_POS(host) == POSITION_FIGHTING))
        return 0;
    if (number(0, 100) < 50)
        return 0;
    if (c) { // singing already...
        e = strchr(c, '\n');
        if (e) {
            *e = '\0';
            if (*c == '+')
                act((char*)(c + 1), TRUE, host, 0, 0, TO_ROOM);
            else
                do_say(host, c, 0, 0, 0);
            *e = '\n';
            host->specials.recite_lines = e + 2; // it appears, asif each line is terminated by a two character combination
        } else
            host->specials.recite_lines = NULL;
        return 0;
    }
// Not singing
remove_scroll:
    for (o = host->carrying; ((o) && (o->name) && (strcmp(o->name, "textscroll"))); o = o->next)
        ; // find a scroll
    if (o) { // found the scroll?
        if (!o->ex_description) { // scroll has no extended description
            mudlog("reciter: missing poem's description", BRF, LEVEL_GOD, TRUE);
            return 0;
        }
        if (!o->ex_description->description) { // again, no extended description
            mudlog("reciter: empty description", BRF, LEVEL_GOD, TRUE);
            return 0;
        }
        if (quitting) {
            obj_from_char(o);
            extract_obj(o);
            mudlog("reciter: done extracting", BRF, LEVEL_GOD, TRUE);
            return 0;
        }
        host->specials.recite_lines = o->ex_description->description;
    } else
        mudlog("reciter: missing poem", BRF, LEVEL_GOD, TRUE);
    return 0;
}

#define HERALD_LEN 1024

SPECIAL(herald)
{
    char hbuf[HERALD_LEN];
    struct memory_rec *mr, *pr;

    if (GET_HIT(host) < 1) { /* herald dead? */
        host->player.death_cry2 = NULL;
        return 0;
    }

    if (!ch)
        return 0;
    if ((ch == host) || (IS_NPC(ch)))
        return 0;
    if (host->specials.position != POSITION_STANDING)
        return 0;

    /*
     * we'll abuse host->specials.memory to keep
     * track of people entering and leaving the room
     * ok, let's see... did we announce 'host' yet?
     */
    pr = mr = host->specials.memory;
    while (mr) {
        if (mr->enemy != ch) {
            pr = mr;
            mr = mr->next_on_mob;
        } else
            return 0;
    }

    /* ch is new in the hall... */
    remember(host, ch);
    if (!host->player.death_cry2) {
        mudlog("herald: missing death_cry2", BRF, LEVEL_GOD, TRUE);
        return 0;
    }
    snprintf(hbuf, HERALD_LEN - 1, host->player.death_cry2, ch->player.name, ch->player.title);
    do_say(host, hbuf, 0, 0, 0);

    return 0;
}
