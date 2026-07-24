/* ************************************************************************
 *   File: utility.c                                     Part of CircleMUD *
 *  Usage: various internal functions of a utility nature                  *
 *                                                                         *
 *  All rights reserved.  See license.doc for complete information.        *
 *                                                                         *
 *  Copyright (C) 1993 by the Trustees of the Johns Hopkins University     *
 *  CircleMUD is based on DikuMUD, Copyright (C) 1990, 1991.               *
 ************************************************************************ */

/**************************************************************************
 * ROTS Documentation                                                      *
 *                                                                         *
 *                                                                         *
 * Universal list                                                          *
 *   A simple linked list structure which can hold pointers to char_data   *
 *   room_data or obj_data.  The number of universal_list structures are   *
 *   counted with universal_list_counter, and the number actively used in  *
 *   lists is held in used_in_universal_list.  Pool_to_list will add a new *
 *   or existing but unused universal_list structure into a linked list.   *
 **************************************************************************/

#include "platdef.h"

#if defined PREDEF_PLATFORM_LINUX
#include <arpa/telnet.h>
#endif

#include <assert.h>
#include <cstring>
#include <ctype.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "color.h"
#include "comm.h"
#include "db.h"
#include "handler.h"
#include "interpre.h"
#include "spells.h"
#include "structs.h"
#include "utils.h"
#include "warrior_spec_handlers.h"

#include "char_utils.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <deque>

extern struct time_data time_info;
extern struct room_data world;
extern int top_of_p_table;
extern struct player_index_element* player_table;
extern struct char_data* character_list;
extern struct char_data* mob_proto;
extern int max_race_str[];
extern struct char_data* waiting_list;
int get_power_of_arda(char_data* ch);

static void check_container_proto(struct obj_data* obj, struct char_data* ch);

/*
 * Adds data to the char_data structure specifying that
 * `ch' has been retired (the PLR_RETIRED bit), sets the
 * time at which `ch' retired (specials1.retiredon), and
 * appends a retirement exploit to `ch's exploits.
 */
void retire(struct char_data* ch)
{
    SET_BIT(PLR_FLAGS(ch), PLR_RETIRED);
    ch->specials2.retiredon = time(0);
    add_exploit_record(EXPLOIT_RETIRED, ch, 0, "");
}

/*
 * Unset `ch's retired flag, add an exploit record showing
 * that `ch' was unretired, and reset `ch's retiredon time
 * to zero.
 */
void unretire(struct char_data* ch)
{
    REMOVE_BIT(PLR_FLAGS(ch), PLR_RETIRED);
    add_exploit_record(EXPLOIT_ACHIEVEMENT, ch, 0, "Unretired");
    ch->specials2.retiredon = 0;
}

/*
 * Return the 7bit ascii value of an 8bit accented character
 * if we do not support the character, return 0
 */
char unaccent(char c)
{
#define B(bottom, c, top) ((c) >= (bottom) && (c) <= (top))
    if (c < 128)
        return c;

    if (B(192, c, 198))
        return 'A';
    if (c == 199)
        return 'C';
    if (B(200, c, 203))
        return 'E';
    if (B(204, c, 207))
        return 'I';
    if (c == 208)
        return 'D';
    if (c == 209)
        return 'N';
    if (B(210, c, 214) || c == 216)
        return 'O';
    if (c == 215)
        return '*';
    if (B(217, c, 220))
        return 'U';
    if (c == 221)
        return 'Y';
    if (c == 222)
        return 'P';
    if (c == 223)
        return 's';
    if (B(224, c, 230))
        return 'a';
    if (c == 231)
        return 'c';
    if (B(232, c, 235))
        return 'e';
    if (B(236, c, 239))
        return 'i';
    if (c == 240)
        return 'd';
    if (c == 241)
        return 'n';
    if (B(242, c, 246) || c == 248)
        return 'o';
    if (c == 247)
        return '/';
    if (B(249, c, 252))
        return 'u';

#undef B
    return 255;
}

inline int
do_squareroot(int i, struct char_data* ch)
{
    if (i / 4 > 170)
        i = 170 * 4;

    return ((4 - i % 4) * square_root[i / 4] + (i % 4) * square_root[i / 4 + 1]);
}

char get_current_time_phase()
{
    extern int pulse;

    return (pulse % (SECS_PER_MUD_HOUR * 4)) / PULSE_FAST_UPDATE;
}

int default_exit_width[] = {
    2, /* #define SECT_INSIDE          0 */
    4, /* #define SECT_CITY            1 */
    6, /* #define SECT_FIELD           2 */
    5, /* #define SECT_FOREST          3 */
    5, /* #define SECT_HILLS           4 */
    5, /* #define SECT_MOUNTAIN        5 */
    5, /* #define SECT_WATER_SWIM      6 */
    5, /* #define SECT_WATER_NOSWIM    7 */
    5, /* #define SECT_UNDERWATER      8 */
    4, /* #define SECT_ROAD            9 */
    3, /* #define SECT_CRACK          10 */
    3, /* #define SECT_DENSE_FOREST   11 */
    5, /* #define SECT_SWAMP          12 */
    0
};

int get_race_weight(struct char_data* ch)
{
    int gender_mod;

    if (GET_SEX(ch) == SEX_FEMALE)
        gender_mod = 8;
    else
        gender_mod = 10;

    switch (GET_RACE(ch)) {
    case RACE_GOD:
        return 100000 * gender_mod / 10;

    case RACE_HUMAN:
        return 17000 * gender_mod / 10;

    case RACE_DWARF:
        return 20000 * gender_mod / 10;

    case RACE_WOOD:
        return 12000 * gender_mod / 10;

    case RACE_HOBBIT:
        return 7000 * gender_mod / 10;

    case RACE_HIGH:
        return 13000 * gender_mod / 10;

    case RACE_URUK:
        return 16000 * gender_mod / 10;

    case RACE_HARAD:
        return 17000 * gender_mod / 10;

    case RACE_ORC:
        return 9000 * gender_mod / 10;

    case RACE_EASTERLING:
        return 17000 * gender_mod / 10;

    case RACE_MAGUS:
        return 16000 * gender_mod / 10;

    case RACE_TROLL:
        return 80000 * gender_mod / 10;

    case RACE_BEORNING:
        return 80000 * gender_mod / 10;

    case RACE_OLOGHAI:
        return 40000 * gender_mod / 10;

    case RACE_HARADRIM:
        return 17000 * gender_mod / 10;

    case RACE_UNDEAD:
        return 5000 * gender_mod / 10;

    default:
        return 15000;
    }

    return 0;
}

int get_race_height(struct char_data* ch)
{
    int gender_mod;

    if (GET_SEX(ch) == SEX_FEMALE)
        gender_mod = 9;
    else
        gender_mod = 10;

    switch (GET_RACE(ch)) {
    case RACE_GOD:
        return 200 * gender_mod / 10;

    case RACE_HUMAN:
        return 180 * gender_mod / 10;

    case RACE_DWARF:
        return 130 * gender_mod / 10;

    case RACE_WOOD:
        return 200 * gender_mod / 10;

    case RACE_HOBBIT:
        return 110 * gender_mod / 10;

    case RACE_HIGH:
        return 210 * gender_mod / 10;

    case RACE_URUK:
        return 170 * gender_mod / 10;

    case RACE_HARAD:
        return 180 * gender_mod / 10;

    case RACE_ORC:
        return 120 * gender_mod / 10;

    case RACE_EASTERLING:
        return 180 * gender_mod / 10;

    case RACE_MAGUS:
        return 170 * gender_mod / 10;

    case RACE_TROLL:
        return 225 * gender_mod / 10;

    case RACE_UNDEAD:
        return 180 * gender_mod / 10;

    case RACE_HARADRIM:
        return 180 * gender_mod / 10;

    case RACE_BEORNING:
        return 225 * gender_mod / 10;

    case RACE_OLOGHAI:
        return 200 * gender_mod / 10;

    default:
        return 200;
    }

    return 0;
}

sh_int
get_race_perception(struct char_data* ch)
{
    switch (GET_RACE(ch)) {
    case RACE_GOD:
        return 0;
    case RACE_HUMAN:
        return 30;
    case RACE_DWARF:
        return 0;
    case RACE_WOOD:
        return 50;
    case RACE_HOBBIT:
        return 30;
    case RACE_HIGH:
        return 100;
    case RACE_URUK:
        return 30;
    case RACE_HARAD:
        return 30;
    case RACE_ORC:
        return 10;
    case RACE_EASTERLING:
        return 30;
    case RACE_MAGUS:
        return 30;
    case RACE_UNDEAD:
        return 60;
    case RACE_TROLL:
        return 30;
    case RACE_HARADRIM:
        return 30;
    case RACE_BEORNING:
        return 30;
    case RACE_OLOGHAI:
        return 30;
    default:
        return 0;
    }
    return 0;
}

sh_int
get_naked_perception(struct char_data* ch)
{
    int tmp;

    if (IS_NPC(ch)) {
        if (MOB_FLAGGED(ch, MOB_SHADOW))
            return 100;
        else
            return GET_PERCEPTION(ch);
    }

    tmp = get_race_perception(ch);
    tmp += GET_PROF_LEVEL(PROF_CLERIC, ch) * 2;

    return tmp;
}

sh_int
get_naked_willpower(struct char_data* ch)
{
    return GET_PROF_LEVEL(PROF_CLERIC, ch) + GET_WILL(ch) - (get_confuse_modifier(ch) / 10);
}

int get_exit_width(struct room_data* room, int dir)
{
    if (!room || (dir < 0) || (dir >= NUM_OF_DIRS))
        return -1;
    if (!room->dir_option[dir])
        return -1;

    if (room->dir_option[dir]->exit_width != 0)
        return room->dir_option[dir]->exit_width;

    return default_exit_width[room->sector_type];
}

int string_to_new_value(char* arg, int* value)
{
    while (*arg && (*arg <= ' '))
        arg++;

    if (!*arg)
        return *value;

    if (isdigit(*arg))
        *value = atoi(arg);
    if (*arg == '+')
        *value += atoi(arg + 1);
    if (*arg == '-')
        *value -= atoi(arg + 1);
    if ((*arg == 'p') || (*arg == 'P'))
        *value |= 1 << atoi(arg + 1);
    if ((*arg == 'm') || (*arg == 'M'))
        *value &= ~(1 << atoi(arg + 1));

    return *value;
}

//============================================================================
int get_bow_weapon_damage(const obj_data& weapon)
{
    const char_data* owner = weapon.get_owner();
    int level_factor = 0;
    if (owner) {
        level_factor = std::min(weapon.get_level(), owner->get_level()) / 3;
    } else {
        level_factor = weapon.get_level() / 3;
    }
    return level_factor + weapon.get_ob_coef() + weapon.get_bulk();
}

//============================================================================
int get_weapon_damage(struct obj_data* obj)
{
    int parry_coef, OB_coef, dam_coef;
    int tmp, bulk, str_speed, null_speed, ene_regen;
    int obj_level, skill_type;
    struct char_data* owner;

    if (GET_ITEM_TYPE(obj) != ITEM_WEAPON) {
        mudlog("Calculating damage for non-weapon!", NRM, LEVEL_IMMORT, TRUE);
        return 1;
    }

    game_types::weapon_type w_type = obj->get_weapon_type();
    if (w_type == game_types::WT_BOW || w_type == game_types::WT_CROSSBOW) {
        return get_bow_weapon_damage(*obj);
    }

    parry_coef = obj->obj_flags.value[1];
    OB_coef = obj->obj_flags.value[0];
    bulk = obj->obj_flags.value[2];
    skill_type = weapon_skill_num(obj->obj_flags.value[3]);
    obj_level = obj->obj_flags.level;
    owner = obj->carried_by;

    /*
     * If the weapon is owned by someone (i.e., we aren't just statting
     * an object), then that person will affect how well the weapon
     * works.  Currently, there are two maluses:
     *   (1) If the wielder's level is a good deal lower than the
     *       object's level, then the level of the object is lowered
     *       to be closer to the wielder's; thus reducing damage.
     *   (2) If the wielder has low weapon skill for the object, then
     *       we reduce the damage.
     */
    if (owner != NULL) {
        /* Case (1) */
        if (obj_level > (GET_LEVEL(owner) * 4 / 3 + 7))
            obj_level -= (obj_level - GET_LEVEL(owner) * 4 / 3 - 7) * 2 / 3;

        /* Case (2): for skill=100, use full obj_level; skill=0, use obj_level/2 */
        obj_level = obj_level * GET_SKILL(owner, skill_type) / 100;
    }

    switch (obj->obj_flags.value[3]) {
    case 2: /* whip */
        parry_coef += 8;
        OB_coef -= 5;
        break;

    case 3:
    case 4:
        parry_coef -= 2;
        break;

    case 6:
    case 7:
        parry_coef += 3;
        break;
    }

    if (parry_coef < -7)
        parry_coef = parry_coef / 3 - 1; /* i.e. parry < -7 */
    else if (parry_coef < 0)
        parry_coef = parry_coef / 2;

    if (parry_coef > 5)
        parry_coef = parry_coef * 2 - 5;

    if (OB_coef < -7)
        OB_coef = OB_coef / 2 - 1;
    else if (OB_coef < 0)
        OB_coef = OB_coef * 2 / 3;

    if (OB_coef > 40)
        OB_coef = 40; /* just against crashes */

    /* dam_coef is about 3000 on avg. low weapon */
    dam_coef = (40 + obj_level - parry_coef) * (50 - OB_coef) * 4 / 3;
    dam_coef = dam_coef * (20 - abs(bulk - 3)) / 20;

    null_speed = 225;
    if (GET_OBJ_WEIGHT(obj) == 0)
        GET_OBJ_WEIGHT(obj) = 1;

    /* all equal damage in 2-hands, with 20 str 20 dex. 100% attack */
    str_speed = 2 * 20 * 2500000 / (GET_OBJ_WEIGHT(obj) * (bulk + 3));

    tmp = (1000000 / (1000000 / str_speed + 1000000 / (null_speed * null_speed)));

    /* ene_regen is about 100 on average */
    ene_regen = do_squareroot(tmp / 100, NULL) / 20;
    dam_coef = dam_coef / ene_regen * 3;

    if (dam_coef > 70)
        dam_coef = 70 + (dam_coef - 70) * 3 / 4;

    if (dam_coef > 90)
        dam_coef = 90 + (dam_coef - 90) * 3 / 4;

    return dam_coef; /* returning damage * 10 */
}

int weight_coof(struct obj_data* obj)
{
    if (CAN_WEAR(obj, ITEM_WEAR_BODY))
        return 3;
    if (CAN_WEAR(obj, ITEM_WEAR_ARMS))
        return 2;
    if (CAN_WEAR(obj, ITEM_WEAR_LEGS))
        return 2;

    return 1;
}

int armor_absorb(struct obj_data* obj)
{
    int absorb, points, encumb_points;

    if (obj->obj_flags.value[0] == -1)
        return 0;

    encumb_points = obj->obj_flags.value[2] * 6 + GET_OBJ_WEIGHT(obj) / weight_coof(obj) / 20;

    points = obj->obj_flags.level + encumb_points;

    /* bonus of 15 abs. at 30 abs., double at 0 */
    if (encumb_points < 30)
        points += encumb_points * (60 - encumb_points) / 90;

    if (obj->obj_flags.value[2]) /* encumb for low encumb */
        points += 3;

    absorb = points - obj->obj_flags.value[1] * 9;
    if (absorb < 0)
        absorb = 0;

    if (absorb > 50)
        absorb = 100 - 2500 / absorb;

    return (absorb);
}

/*
 * get_real_stealth has sort of turned into the default
 * "how stealthy is this person?" function.  It now takes
 * into account specialization and sneak in addition to
 * stealth skill, race, sector type and encumbrance.
 */
int get_real_stealth(struct char_data* ch)
{
    int percent;

    /* This is the only bonus we give for the stealth skill */
    percent = GET_RAW_SKILL(ch, SKILL_STEALTH) / 4;

    /* This is the only place where stealth spec helps hiders */
    if (GET_SPEC(ch) == PLRSPEC_STLH)
        percent += 5;

    /* Now, sneaking helps stealth */
    if (IS_AFFECTED(ch, AFF_SNEAK) && IS_SET(ch->specials2.hide_flags, HIDING_SNUCK_IN)) {
        if (IS_NPC(ch))
            percent += std::max(100, 10 * GET_LEVEL(ch) / 30);
        else
            percent += GET_SKILL(ch, SKILL_SNEAK) / 20;
    }

    switch (world[ch->in_room].sector_type) {
    case SECT_INSIDE:
        percent -= 20;
        break;
    case SECT_CITY:
        percent -= 10;
        break;
    case SECT_FIELD:
        percent += 0;
        break;
    case SECT_FOREST:
        percent += 15;
        break;
    case SECT_HILLS:
        percent += 5;
        break;
    case SECT_MOUNTAIN:
        percent += 0;
        break;
    case SECT_WATER_SWIM:
        percent -= 20;
        break;
    case SECT_WATER_NOSWIM:
        percent -= 20;
        break;
    case SECT_UNDERWATER:
        percent -= 20;
        break;
    case SECT_ROAD:
        percent -= 5;
        break;
    case SECT_CRACK:
        percent -= 10;
        break;
    case SECT_DENSE_FOREST:
        percent += 20;
        break;
    case SECT_SWAMP:
        percent += 5;
        break;
    }

    if (GET_RACE(ch) == RACE_DWARF)
        percent -= 10;

    if (GET_RACE(ch) == RACE_HOBBIT || GET_RACE(ch) == RACE_BEORNING || GET_RACE(ch) == RACE_HARADRIM)
        percent += 5;

    percent -= utils::get_leg_encumbrance(*ch);
    percent -= utils::get_encumbrance(*ch) / 4;

    return (percent);
}

int get_real_OB(char_data* ch)
{
    if (IS_NPC(ch)) {
        int base_npc_ob = (GET_OB(ch) + GET_BAL_STR(ch) + 15 - utils::get_skill_penalty(*ch) + GET_LEVEL(ch) / 2);
        if (IS_AFFECTED(ch, AFF_CONFUSE)) {
            base_npc_ob -= (get_confuse_modifier(ch) * 2 / 3);
        }
        return base_npc_ob;
    }

    int sun_mod = 0;
    int tmpob, tactics, weapon_skill = 0;

    obj_data* weapon = ch->equipment[WIELD];

    int warrior_level = GET_PROF_LEVEL(PROF_WARRIOR, ch);
    int max_warrior_level = GET_MAX_RACE_PROF_LEVEL(PROF_WARRIOR, ch);
    int offense_stat = GET_BAL_STR(ch);

    // Light fighters can use dex and some of their ranger level with light weapons.
    if (utils::get_specialization(*ch) == game_types::PS_LightFighting) {
        if (weapon) {
            int bulk = weapon->get_bulk();
            if (bulk <= 2 || (bulk == 3 && weapon->get_weight() <= LIGHT_WEAPON_WEIGHT_CUTOFF)) {
                offense_stat = std::max(offense_stat, int(ch->tmpabilities.dex));

                int ranger_bonus = GET_PROF_LEVEL(PROF_RANGER, ch) / 3;
                warrior_level += ranger_bonus;
            }
        }
    }

    int ob_bonus = (warrior_level * 3 + 3 * max_warrior_level * GET_LEVELA(ch) / 30) / 2 + offense_stat;

    tmpob = GET_OB(ch);
    tmpob -= utils::get_skill_penalty(*ch);

    player_spec::weapon_master_handler weapon_master(ch);
    tmpob += weapon_master.get_bonus_OB();

    if (!weapon && utils::get_raw_knowledge(*ch, SKILL_NATURAL_ATTACK) == 0) {
        return tmpob + ob_bonus;
    } else if (!weapon && utils::get_raw_knowledge(*ch, SKILL_NATURAL_ATTACK) > 0) {
        weapon_skill = utils::get_raw_knowledge(*ch, SKILL_NATURAL_ATTACK);
        tmpob -= (GET_STR(ch) / 2 - 6);
    } else {
        weapon_skill = utils::get_raw_knowledge(*ch, weapon_skill_num(weapon->get_weapon_type()));

        if (IS_TWOHANDED(ch)) {
            if (weapon->is_ranged_weapon()) {
                tmpob += weapon->obj_flags.value[2] * (200 + GET_RAW_SKILL(ch, SKILL_ARCHERY)) / 100 - 15;
                weapon_skill = (weapon_skill + GET_RAW_SKILL(ch, SKILL_ARCHERY)) / 2;
            } else {
                tmpob += weapon->obj_flags.value[2] * (200 + GET_RAW_KNOWLEDGE(ch, SKILL_TWOHANDED)) / 100 - 15;
                weapon_skill = (weapon_skill + GET_RAW_KNOWLEDGE(ch, SKILL_TWOHANDED)) / 2;
            }
        } else {
            tmpob -= (weapon->obj_flags.value[2] * 2 - 6);
        }
    }

    switch (GET_TACTICS(ch)) {
    case TACTICS_DEFENSIVE:
        tmpob += ob_bonus - ob_bonus / 4 - 8;
        tactics = 4;
        break;
    case TACTICS_CAREFUL:
        tmpob += ob_bonus - ob_bonus / 8 - 4;
        tactics = 6;
        break;
    case TACTICS_NORMAL:
        tmpob += ob_bonus;
        tactics = 8;
        break;
    case TACTICS_AGGRESSIVE:
        tmpob += ob_bonus + ob_bonus / 16 + 2;
        tactics = 10;
        break;
    case TACTICS_BERSERK:
        tmpob += ob_bonus + ob_bonus / 16 + 5 + GET_RAW_SKILL(ch, SKILL_BERSERK) / 8;
        tactics = 10;
        break;
    default:
        tmpob += ob_bonus + GET_BAL_STR(ch);
        break;
    };

    if (IS_AFFECTED(ch, AFF_CONFUSE))
        tmpob -= (get_confuse_modifier(ch) * 2 / 3);

    /* to get the pre-power of arda malus, substitute 10 for sun_mod */
    sun_mod = get_power_of_arda(ch);
    if (sun_mod) {
        if (GET_RACE(ch) == RACE_URUK)
            tmpob = tmpob * 4 / 5 - sun_mod;
        if (GET_RACE(ch) == RACE_ORC)
            tmpob = tmpob * 3 / 4 - sun_mod;
        if (GET_RACE(ch) == RACE_MAGUS)
            tmpob = tmpob * 4 / 5 - sun_mod;
        if (GET_RACE(ch) == RACE_OLOGHAI)
            tmpob = tmpob * 4 / 5 - sun_mod;
    }

    if (!CAN_SEE(ch))
        tmpob -= 10;

    if (!weapon)
        tmpob += weapon_skill * (GET_STR(ch) + 20) * tactics / 1000;
    else
        tmpob += weapon_skill * (weapon->obj_flags.value[2] + 20) * tactics / 1000;

    return tmpob;
}

int get_real_parry(struct char_data* ch)
{
    int sun_mod = 0;

    if (IS_NPC(ch))
        if (IS_AFFECTED(ch, AFF_CONFUSE))
            return (GET_PARRY(ch) + GET_LEVEL(ch) / 2 + 15) - (get_confuse_modifier(ch) * 2 / 3);
        else
            return (GET_PARRY(ch) + GET_LEVEL(ch) / 2 + 15);

    int tmpparry, tmpskill, tactics, bonus, weapon_bonus = 0;
    struct obj_data* weapon;

    tmpparry = GET_PARRY(ch);
    bonus = GET_PROF_LEVEL(PROF_WARRIOR, ch) * 2 + std::min(30, GET_LEVEL(ch)) + GET_BAL_STR(ch);

    player_spec::weapon_master_handler weapon_master(ch);
    tmpparry += weapon_master.get_bonus_PB();

    weapon = ch->equipment[WIELD];
    if (!weapon && utils::get_raw_knowledge(*ch, SKILL_NATURAL_ATTACK) == 0) {
        return tmpparry + bonus / 2;
    } else if (!weapon && utils::get_raw_knowledge(*ch, SKILL_NATURAL_ATTACK) > 0) {
        tmpskill = GET_RAW_SKILL(ch, SKILL_NATURAL_ATTACK);
    } else {
        weapon_bonus = weapon->obj_flags.value[1];

        tmpskill = GET_RAW_KNOWLEDGE(ch, weapon_skill_num(weapon->obj_flags.value[3]));
        if (isname("bow", weapon->name)) {
            tmpskill = GET_RAW_SKILL(ch, SKILL_ARCHERY);
        }

        if (IS_TWOHANDED(ch)) {
            tmpskill = (tmpskill + GET_RAW_KNOWLEDGE(ch, SKILL_TWOHANDED)) / 2;
            if (isname("bow", weapon->name)) {
                tmpskill = (tmpskill + GET_RAW_SKILL(ch, SKILL_ARCHERY)) / 2;
            }
        }
    }

    tmpskill = (tmpskill + 3 * GET_RAW_KNOWLEDGE(ch, SKILL_PARRY)) / 4;
    if (GET_TACTICS(ch) == TACTICS_BERSERK) {
        tmpskill /= 2;
    }

    switch (GET_TACTICS(ch)) {
    case TACTICS_DEFENSIVE:
        tmpparry += bonus / 2 + 3 * bonus / 16;
        tactics = 4;
        break;
    case TACTICS_CAREFUL:
        tmpparry += bonus / 2 + bonus / 8;
        tactics = 6;
        break;
    case TACTICS_NORMAL:
        tmpparry += bonus / 2;
        tactics = 8;
        break;
    case TACTICS_AGGRESSIVE:
        tmpparry += bonus / 2 - bonus / 8;
        tactics = 10;
        break;
    case TACTICS_BERSERK:
        tmpparry += bonus / 2 - bonus / 8;
        tactics = 12;
        break;
    default:
        tmpparry += bonus / 2;
        tactics = 10;
        break;
    };

    tmpparry += tmpskill * (weapon_bonus + 20) * (14 - tactics) / 1000;
    // Parry should now have bigger effect on two-handers:
    if (IS_AFFECTED(ch, AFF_TWOHANDED))
        tmpparry += weapon_bonus / 2;

    if (IS_AFFECTED(ch, AFF_CONFUSE))
        tmpparry -= (get_confuse_modifier(ch) * 2 / 3);

    sun_mod = get_power_of_arda(ch);
    if (sun_mod) {
        if (GET_RACE(ch) == RACE_URUK)
            tmpparry = tmpparry * 9 / 10 - sun_mod;
        if (GET_RACE(ch) == RACE_ORC)
            tmpparry = tmpparry * 8 / 9 - sun_mod;
        if (GET_RACE(ch) == RACE_MAGUS)
            tmpparry = tmpparry * 9 / 10 - sun_mod;
        if (GET_RACE(ch) == RACE_OLOGHAI)
            tmpparry = tmpparry * 9 / 10 - sun_mod;
    }

    if (!CAN_SEE(ch)) {
        tmpparry -= 10;
    }

    return tmpparry;
}

int get_real_dodge(struct char_data* ch)
{
    int sun_mod = 0;

    if (IS_NPC(ch))
        if (IS_AFFECTED(ch, AFF_CONFUSE))
            return (GET_DODGE(ch) + GET_DEX(ch) - 5 + GET_LEVEL(ch) / 2) - (get_confuse_modifier(ch) * 2 / 3);
        else
            return (GET_DODGE(ch) + GET_DEX(ch) - 5 + GET_LEVEL(ch) / 2);

    int dodge = ((GET_SKILL(ch, SKILL_DODGE) + GET_SKILL(ch, SKILL_STEALTH) / 2 + 60) * GET_PROF_LEVEL(PROF_RANGER, ch) / 200 + (GET_SKILL(ch, SKILL_DODGE) + GET_SKILL(ch, SKILL_STEALTH) / 4) / 20);
    dodge -= utils::get_dodge_penalty(*ch);
    dodge += 3;

    if (GET_RACE(ch) == RACE_BEORNING) {
        dodge += 20;
    }

    if (GET_TACTICS(ch) == TACTICS_BERSERK)
        dodge /= 2;

    if (IS_AFFECTED(ch, AFF_CONFUSE))
        dodge -= (get_confuse_modifier(ch) * 2 / 3);

    sun_mod = get_power_of_arda(ch);
    if (sun_mod) {
        if (GET_RACE(ch) == RACE_URUK)
            dodge = dodge * 9 / 10 - sun_mod;
        if (GET_RACE(ch) == RACE_ORC)
            dodge = dodge * 8 / 9 - sun_mod;
        if (GET_RACE(ch) == RACE_MAGUS)
            dodge = dodge * 9 / 10 - sun_mod;
        if (GET_RACE(ch) == RACE_OLOGHAI)
            dodge = dodge * 9 / 10 - sun_mod;
    }

    switch (GET_TACTICS(ch)) {
    case TACTICS_DEFENSIVE:
        return (dodge + GET_DODGE(ch) + 6) + GET_DEX(ch);
    case TACTICS_CAREFUL:
        return (dodge + GET_DODGE(ch) + 4) + GET_DEX(ch);
    case TACTICS_NORMAL:
        return (dodge + GET_DODGE(ch)) + GET_DEX(ch);
    case TACTICS_AGGRESSIVE:
        return (dodge + GET_DODGE(ch) - 4) + GET_DEX(ch);
    case TACTICS_BERSERK:
        return (dodge + GET_DODGE(ch) - 4) + GET_DEX(ch) / 2;
    default:
        return (dodge + GET_DODGE(ch) + GET_DEX(ch));
    };
}

int get_followers_level(char_data* ch) /* summ of levels of mobs/players charmed by ch */
{
    int levels = 0;

    for (char_data* tmpch = character_list; tmpch; tmpch = tmpch->next) {
        if ((tmpch->master == ch) && IS_AFFECTED(tmpch, AFF_CHARM)) {
            if (!utils::is_guardian(*tmpch)) {
                levels += std::max(2, tmpch->get_level());
            }
        }
    }

    return levels;
}

// returns a random number from 0.0 to 1.0
double number()
{
    double roll = std::rand();
    double max = RAND_MAX;
    return roll / max;
}

// returns a random number from 0.0 to max
double number(double max)
{
    return number() * max;
}

// returns a random number in interval [from;to] */
double number_d(double from, double to)
{
    if (from > to) {
        std::swap(from, to);
    }

    return number(to) + from;
}

/* creates a random number in interval [from;to] */
int number(int from, int to)
{
    if (from == to) {
        return from;
    }
    // Ensure that our to/from is in the proper order.
    if (from > to) {
        std::swap(to, from);
    }

    int upper_end = to - from + 1;
    if (upper_end == 0) {
        //       fprintf(stderr, "SYSERR: number(%d, %d)\n",from,to);
        to = from;
    }

    return (std::rand() % upper_end) + from;
}

/* simulates dice roll */
int dice(int number, int size)
{
    int r;
    int sum = 0;

    //   assert(size >= 1);
    if (size < 1) {
        mudlog("Dice rolled with size < 1!", BRF, LEVEL_IMMORT, TRUE);
        return 0;
    }

    for (r = 1; r <= number; r++) {
        sum += (std::rand() % size) + 1;
    }

    return (sum);
}

/* Create a duplicate of a string */
char* str_dup(const char* source)
{
    if (!source)
        return NULL;

    char* new_string;
    int length = std::strlen(source);

    CREATE(new_string, char, ((int)(length / 0x100) + 1) * 0x100);

    for (int i = 0; i < length; i++) {
        new_string[i] = source[i];
    }
    new_string[length] = 0;

    return new_string;
}

/* returns: 0 if equal, 1 if arg1 > arg2, -1 if arg1 < arg2  */
/* scan 'till found different or end of both                 */
int str_cmp(char* arg1, char* arg2)
{
    int chk, i;

    for (i = 0; *(arg1 + i) || *(arg2 + i); i++)
        if ((chk = LOWER(*(arg1 + i)) - LOWER(*(arg2 + i))))
            if (chk < 0)
                return (-1);
            else
                return (1);
    return (0);
}

/* returns: 0 if equal, 1 if arg1 > arg2, -1 if arg1 < arg2  */
/* scan 'till found different, end of both, or n reached     */
int strn_cmp(char* arg1, char* arg2, int n)
{
    int chk, i;

    for (i = 0; (*(arg1 + i) || *(arg2 + i)) && (n > 0); i++, n--)
        if ((chk = LOWER(*(arg1 + i)) - LOWER(*(arg2 + i))))
            if (chk < 0)
                return (-1);
            else
                return (1);

    return (0);
}

/* log a death trap hit */
void log_death_trap(struct char_data* ch)
{
    char buf[150];
    //   extern struct room_data world;

    sprintf(buf, "%s hit death trap #%d (%s)", GET_NAME(ch),
        world[ch->in_room].number, world[ch->in_room].name);
    mudlog(buf, BRF, LEVEL_IMMORT, TRUE);
}

/* writes a string to the log */
void log(const char* str)
{
    // time_t ct(0);
    // char* time_string = asctime(localtime(&ct));

    //*(time_string + std::strlen(time_string) - 1) = '\0';
    // fprintf(stderr, "%-19.19s :: %s\n", time_string, str);

    long ct;
    char* tmstr;
    ct = time(0);
    tmstr = asctime(localtime(&ct));
    *(tmstr + strlen(tmstr) - 1) = '\0';
    fprintf(stderr, "%-19.19s :: %s\n", tmstr, str);
}

void mudlog(char* str, char type, sh_int level, byte file)
{
    char buf[8000];
    extern struct descriptor_data* descriptor_list;
    struct descriptor_data* i;
    char* tmp;
    long ct;
    char tp;

    ct = time(0);
    tmp = asctime(localtime(&ct));
    // time_t ct(0);
    // char* tmp = asctime(localtime(&ct));

    if (file)
        fprintf(stderr, "%d, %-19.19s :: %s\n", type, tmp, str);
    if (level < 0)
        return;

    if (level < LEVEL_AREAGOD)
        level = LEVEL_AREAGOD;

    sprintf(buf, "[ %s ]\n\r", str);

    for (i = descriptor_list; i; i = i->next)
        if (!i->connected && !PLR_FLAGGED(i->character, PLR_WRITING)) {
            tp = ((PRF_FLAGGED(i->character, PRF_LOG1) ? 1 : 0) + (PRF_FLAGGED(i->character, PRF_LOG2) ? 2 : 0) + (PRF_FLAGGED(i->character, PRF_LOG3) ? 4 : 0));

            if ((GET_LEVEL(i->character) >= level) && (tp >= type)) {
                send_to_char(CC_FIX(i->character, CGRN), i->character);
                send_to_char(buf, i->character);
                send_to_char(CC_NORM(i->character), i->character);
            }
        }
    return;
}

void mudlog_debug_mob(char* buf, char_data* ch)
{
    mudlog_aliased_mob(buf, ch, "debug");
}

void mudlog_aliased_mob(char* buf, char_data* ch, char* mob_alias)
{
    if (strstr(ch->player.name, mob_alias)) {
        mudlog(buf, SPL, LEVEL_GOD, FALSE);
    }
}

void vmudlog(char type, char* format, ...)
{
#define BUFSIZE 2048
    char buf[BUFSIZE];
    va_list ap;

    va_start(ap, format);
    vsnprintf(buf, BUFSIZE - 1, format, ap);
    buf[BUFSIZE - 1] = '\0';
    va_end(ap);

    mudlog(buf, type, LEVEL_GOD, TRUE);
}

/*
 * Sprintbit now contains an extra variable (int var) so it can
 * discern when identify is using it.
 */
void sprintbit(long vektor, char* names[], char* result, int var)
{
    long nr;
    int count;

    *result = '\0';
    count = 0;

    if (vektor < 0) {
        strcpy(result, "SPRINTBIT ERROR!");
        return;
    }

    if (vektor == 0) {
        if (var != 0)
            strcpy(result, "has no additional attributes. ");
        else
            strcpy(result, "<NONE>");
        return;
    }

    for (nr = 0; vektor; vektor >>= 1) {
        if (IS_SET(1, vektor) && (vektor != BFS_MARK)) {
            if (*names[nr] != '\n') {
                /*
                 * Where the variable passed in is not 0
                 * then identify is using sprintbit
                 * The block of code contained here is used only
                 * for identify.
                 */
                if (var != 0) {
                    if (var == 2) {
                        if (count == 0)
                            strcat(result, " ");
                        else
                            strcat(result, " and ");
                    } else {
                        if (count == 0)
                            strcat(result, "has the following attributes.\r\n");
                        else
                            strcat(result, ".\r\n");
                    }
                } else /* normal sprintbit resumes here */
                    strcat(result, " ");
                strcat(result, names[nr]);
                count++;
            } else {
                strcat(result, "UNDEFINE ");
            }
        }
        if (*names[nr] != '\r\n')
            nr++;
    }

    if (!*result)
        strcat(result, "NOFLAGS");

    strcat(result, ".");
}

void sprinttype(int type, char* names[], char* result)
{
    int nr;

    for (nr = 0; (*names[nr] != '\n'); nr++)
        ;
    if (type < nr)
        strcpy(result, names[type]);
    else
        strcpy(result, "UNDEFINED");
}

/* Calculate the REAL time passed over the last t2-t1 centuries (secs) */
struct time_info_data real_time_passed(time_t t2, time_t t1)
{
    long secs;
    struct time_info_data now;

    secs = (long)(t2 - t1);

    now.hours = (secs / SECS_PER_REAL_HOUR) % 24; /* 0..23 hours */
    secs -= SECS_PER_REAL_HOUR * now.hours;

    now.day = (secs / SECS_PER_REAL_DAY); /* 0..34 days  */
    secs -= SECS_PER_REAL_DAY * now.day;

    now.month = 0;
    now.year = 0;

    return now;
}

/* Calculate the MUD time passed over the last t2-t1 centuries (secs) */
struct time_info_data mud_time_passed(time_t t2, time_t t1)
{
    long secs;
    struct time_info_data now;

    secs = (long)(t2 - t1);

    now.hours = (secs / SECS_PER_MUD_HOUR) % 24; /* 0..23 hours */
    secs -= SECS_PER_MUD_HOUR * now.hours;

    now.day = (secs / SECS_PER_MUD_DAY) % 30; /* 0..34 days  */
    now.moon = (secs / SECS_PER_MUD_DAY) % 28; /* 0..34 days  */
    secs -= SECS_PER_MUD_DAY * now.day;

    now.month = (secs / SECS_PER_MUD_MONTH) % 12; /* 0..16 months */
    secs -= SECS_PER_MUD_MONTH * now.month;

    now.year = (secs / SECS_PER_MUD_YEAR); /* 0..XX? years */

    return now;
}

struct time_info_data age(struct char_data* ch)
{
    struct time_info_data player_age;

    player_age = mud_time_passed(time(0), ch->player.time.birth);

    player_age.year += 17; /* All players start at 17 */

    return player_age;
}

/*
** Turn off echoing (specific to telnet client)
*/

void echo_off(int sock)
{

    char off_string[] = //"";
        {
            (char)IAC,
            (char)WILL,
            (char)TELOPT_ECHO,
            (char)0,
        };
    (void)write(sock, off_string, sizeof(off_string));
}

/*
** Turn on echoing (specific to telnet client)
*/

void echo_on(int sock)
{
    char off_string[] = //"";
        {
            (char)IAC,
            (char)WONT,
            (char)TELOPT_ECHO,
            (char)TELOPT_NAOFFD,
            (char)TELOPT_NAOCRD,
            (char)0,
        };
    (void)write(sock, off_string, sizeof(off_string));
}

/* This is to work together with CREATE macro, to try fighting
   memory fault crashes.
   */

void* create_pointer = 0;

void* create_function(int elem_size, int elem_num, int line, const char* file)
{

    //  printf("want to allocate size=%d, num=%d\n",elem_size,elem_num);

    if (elem_size * elem_num == 0)
        create_pointer = calloc(1, 1);
    else
        create_pointer = calloc(elem_size, elem_num);

    // if (elem_size * elem_num == 0)
    //   create_pointer = malloc(1);
    // else
    //   create_pointer = malloc(elem_size * elem_num);

    if (!create_pointer) {
        printf("CREATE: could not allocate memory %d size %d elements at line %d, file %s.\n",
            elem_size, elem_num, line, file);
        exit(0);
    }
    //   for(i = 0; i<10; i++) j = random();
    //  printf("create_function, return%p\n",create_pointer);
    return create_pointer;
}

void free_function(void* pnt)
{
    create_pointer = pnt;
    //  printf("free_function, pnt=%p",create_pointer);
    if (create_pointer)
        free(create_pointer);
    //   for(i = 0; i<100; i++) j = random();
    //  printf("  free\n");
    create_pointer = 0;
}
void initialize_buffers()
{
    return;
}

unsigned char encrypt_line_lp[1000];
void encrypt_line(unsigned char* line, int len)
{
    unsigned char k1, k2;
    int tmp;
    /*static*/ unsigned char* lp = encrypt_line_lp;

    for (tmp = 0; tmp < len; tmp++)
        if (line[tmp] > 127)
            line[tmp] -= 128;

    for (tmp = 0; tmp < len - 1; tmp++) {
        k1 = (line[tmp] * 16); // here was *32...
        k2 = (line[tmp + 1] / 8);
        lp[tmp] = (k1 + k2) & 127;
        lp[tmp] += 32;
        //    printf("encoding: '%c' %d %d '%c'%d\n",line[tmp],k1,k2 ,lp[tmp],lp[tmp]);
    }
    k1 = (line[len - 1] * 16);
    k2 = (line[0] / 8);
    // k2 = 0;
    lp[len - 1] = (k1 + k2) & 127;
    lp[len - 1] += 32;
    //  printf("e-encoding: '%c' %d %d '%c'%d\n",line[len-1],k1,k2 ,lp[len-1],lp[len-1]);

    for (tmp = 0; tmp < len; tmp++)
        line[tmp] = lp[tmp];
}

unsigned char decrypt_line_line[1000];
void decrypt_line(unsigned char* lp, int len)
{
    unsigned char k1, k2;
    int tmp;
    /*static*/ unsigned char* line = decrypt_line_line;

    k1 = ((lp[len - 1] - 32) * 8);
    // k1 = 0;
    k2 = ((lp[0] - 32) / 16);
    line[0] = (k1 + k2) & 127;
    //  printf("d-decoding: '%c'%d %d %d '%c'%d\n",lp[0],lp[0],k1,k2 ,line[0],line[0]);
    for (tmp = 1; tmp < len; tmp++) {
        k1 = ((lp[tmp - 1] - 32) * 8);
        k2 = ((lp[tmp] - 32) / 16); // here was /32
        line[tmp] = (k1 + k2) & 127;
        //    printf("decoding: '%c'%d %d %d '%c'%d\n",lp[tmp],lp[tmp],k1,k2 ,line[tmp],line[tmp]);
    }

    for (tmp = 0; tmp < len; tmp++)
        lp[tmp] = line[tmp];
}

char* strcpy_lang(char* str1, char* str2, byte freq, int maxlen)
{
    int i, len;

    len = strlen(str2);
    if (len > maxlen)
        len = maxlen;

    for (i = 0; i < len; i++) {
        if ((number(1, 100) > freq) && isalpha(str2[i])) {
            if (isupper(str2[i]))
                str1[i] = number(65, 90);
            else
                str1[i] = number(97, 122);
        } else
            str1[i] = str2[i];
    }
    str1[i] = 0;

    return str1;
}

void reshuffle(int* arr, int len)
{
    int tmp, tmp2, num;
    int newarr[255];
    char flags[255];

    if (len >= 255) {
        len = 254;
        log("Reshuffle called for more than 255 elements.");
    }

    for (tmp = 0; tmp < len; tmp++)
        flags[tmp] = 1;

    for (tmp = 0; tmp < len; tmp++) {
        num = 1 + number(0, len - tmp - 1);
        for (tmp2 = 0; (tmp2 < len) && num; tmp2++)
            num -= flags[tmp2];
        if (tmp2 >= len + 1) {
            tmp2 = len - 1;
            log("trouble in reshuffle.");
        }
        flags[tmp2 - 1] = 0;
        newarr[tmp] = arr[tmp2 - 1];
    }
    for (tmp = 0; tmp < len; tmp++)
        arr[tmp] = newarr[tmp];
    return;
}

/*
 * Can character see at all?
 */
int CAN_SEE(struct char_data* sub)
{
    if ((sub)->in_room == NOWHERE)
        return 0;
    if (IS_AFFECTED((sub), AFF_BLIND) || PLR_FLAGGED(sub, PLR_WRITING))
        return 0;

    if (IS_SHADOW(sub))
        return 1;

    if (!IS_LIGHT((sub)->in_room) && (!IS_AFFECTED((sub), AFF_INFRARED) && !PRF_FLAGGED((sub), PRF_HOLYLIGHT) && !(OUTSIDE(sub) && IS_AFFECTED((sub), AFF_MOONVISION) && weather_info.moonlight)))
        return 0;

    return 1;
}

/*
 * Can subject see character "obj"?  Returns 0 if sub
 * cannot see obj, and 1 if sub can see obj.  CAN_SEE
 * is called way too many times.  From testing, it's
 * called three times for a kill command.
 */
int CAN_SEE(struct char_data* sub, struct char_data* obj, int light_mode)
{
    int tmp;

    if (!obj)
        return CAN_SEE(sub);
    if ((sub) == (obj))
        return 1;
    if (PLR_FLAGGED(sub, PLR_WRITING))
        return 0;

    /*
     * If you aren't in the same room as it, and it's an NPC,
     * you can't see it, unless it's the guardian angel.
     */
    if (GET_LEVEL(sub) < LEVEL_IMMORT && sub->in_room != obj->in_room && IS_NPC(obj) && strcmp(GET_NAME(obj), "Guardian angel"))
        return 0;

    if (IS_SHADOW(obj) || IS_SHADOW(sub)) {
        if ((sub->specials.fighting == obj) || (obj->specials.fighting == sub))
            return 1;
        if (!(!IS_NPC(sub) && PRF_FLAGGED((sub), PRF_HOLYLIGHT)) && (GET_PERCEPTION(sub) * GET_PERCEPTION(obj) < number(1, 10000)))
            return 0;
    }

    /*
     * Light/physical dependent stuff in here: shadows can
     * see though physical objects (hence see hidden players)
     * and don't worry about light sources.
     */
    if (!(IS_SHADOW(sub))) {
        if (GET_HIDING(obj)) {
            /* mobs have a 10% chance to simply not see people that sneak in */
            if (IS_NPC(sub) && IS_SET(obj->specials2.hide_flags, HIDING_SNUCK_IN) && !number(0, 9) && !IS_NPC(obj)) {
                act("$n glances directly at you, but doesn't seem to notice "
                    "your presence.",
                    FALSE, sub, 0, obj, TO_VICT);
                return 0;
            } else
                tmp = see_hiding(sub);
            if ((tmp < GET_HIDING(obj)) && !(!IS_NPC(sub) && PRF_FLAGGED((sub), PRF_HOLYLIGHT))) {
                return 0;
            }
        }

        if (!light_mode) {
            if (!IS_LIGHT((obj)->in_room) && !IS_AFFECTED((sub), AFF_INFRARED) && !(!IS_NPC(sub) && PRF_FLAGGED((sub), PRF_HOLYLIGHT)) && !(OUTSIDE(sub) && IS_AFFECTED((sub), AFF_MOONVISION) && weather_info.moonlight))
                return 0;
        }
    }
    /* End light/physical dependent stuff */

    /* If obj has an invis level, you can't see it unless you're >= that level */
    if ((GET_LEVEL(sub) < GET_INVIS_LEV(obj)) && !IS_NPC(obj))
        return 0;

    /* Blinded players don't see anything */
    if (IS_AFFECTED((sub), AFF_BLIND))
        return 0;

    /* You can't see invisible objects; unless you're an imm with HOLYLIGHT */
    if (IS_AFFECTED((obj), AFF_INVISIBLE))
        if (!(!IS_NPC(sub) && PRF_FLAGGED((sub), PRF_HOLYLIGHT)))
            return 0;

    /* Ok, noone can see if they are sleeping :) */
    if (GET_POS((sub)) <= POSITION_SLEEPING)
        return 0;

    return 1;
}

/*
 * If a character is affected by the detect hidden spell,
 * can they sense a hidden character?  Returns 1 if sub
 * can see obj, returns 0 otherwise.
 */
int can_sense(char_data* sub, char_data* obj)
{
    if (!IS_AFFECTED((sub), AFF_DETECT_HIDDEN) || GET_PERCEPTION(obj) <= 0)
        return 0;

    if (30 + (GET_PROF_LEVEL(PROF_CLERIC, sub) * 3) > (100 - GET_PERCEPTION(obj) * GET_PERCEPTION(sub) / 100 + GET_HIDING(obj) / 4))
        return 1;
    else
        return 0;
}

int CAN_SEE_OBJ(char_data* sub, obj_data* obj)
{
    if (!sub || !obj)
        return 0;

    if (IS_SHADOW(sub))
        if (IS_SET((obj)->obj_flags.extra_flags, ITEM_MAGIC) || IS_SET((obj)->obj_flags.extra_flags, ITEM_WILLPOWER))
            return 1;
        else
            return 0;

    if ((!IS_SET((obj)->obj_flags.extra_flags, ITEM_INVISIBLE) || IS_AFFECTED((sub), AFF_DETECT_INVISIBLE)) && CAN_SEE(sub))
        return 1;

    return 0;
}

/* moved from utils.h */
int CAN_GO(struct char_data* ch, int door)
{
    if ((EXIT(ch, door) && (EXIT(ch, door)->to_room != NOWHERE)) && (!(IS_SHADOW(ch) ? (IS_SET(EXIT(ch, door)->exit_info, EX_DOORISHEAVY) && IS_SET(EXIT(ch, door)->exit_info, EX_CLOSED)) : IS_SET(EXIT(ch, door)->exit_info, EX_CLOSED)) || IS_SET(EXIT(ch, door)->exit_info, EX_ISBROKEN)))
        return 1;

    return 0;
}

int get_confuse_modifier(struct char_data* ch)
{
    struct affected_type* aff;
    int modifier = 0;

    if (IS_AFFECTED(ch, AFF_CONFUSE))
        for (aff = ch->affected; aff; aff = aff->next)
            if (aff->type == SPELL_CONFUSE)
                modifier = aff->duration * 2 - 10;

    return modifier;
}

int get_power_of_arda(struct char_data* ch)
{
    struct affected_type* aff;
    int sun_mod;

    aff = affected_by_spell(ch, SPELL_ARDA);
    if (aff)
        sun_mod = aff->modifier / 25;
    else
        sun_mod = 0;

    return sun_mod;
}

int has_critical_stat_damage(struct char_data* ch)
{
    if (((GET_STR(ch) * 100) / GET_STR_BASE(ch) < 30) || ((GET_DEX(ch) * 100) / GET_DEX_BASE(ch) < 30) || ((GET_CON(ch) * 100) / GET_CON_BASE(ch) < 30))
        return 1;
    else
        return 0;
}

int can_breathe(struct char_data* ch)
{
    int result;

    result = 1;
    if (world[ch->in_room].sector_type == SECT_UNDERWATER) {
        result = 0;
        if (IS_AFFECTED(ch, AFF_BREATHE) || IS_SHADOW(ch) || (GET_RACE(ch) == RACE_UNDEAD))
            result = 1;
    }
    if (world[ch->in_room].sector_type == SECT_WATER_NOSWIM && !(can_swim(ch)))
        result = 0;

    return result;
}

/*
 * Return the string corresponding to the "nth" number.
 * I.e., if n is 1, then the string is "1st", if n is 2
 * the string is "2nd", and so on.
 *
 * This string is dynamically allocated and must be freed
 * by the caller.
 */
char* nth(int n)
{
    char *s, *r;
    char* first = "st";
    char* second = "nd";
    char* third = "rd";
    char* other = "th";

    /* 11, 12 and 13 don't follow the general rule */
    if (n == 11 || n == 12 || n == 13)
        s = other;
    else {
        switch (n % 10) {
        case 1:
            s = first;
            break;
        case 2:
            s = second;
            break;
        case 3:
            s = third;
            break;
        default:
            s = other;
        }
    }

    asprintf(&r, "%d%s", n, s);

    return r;
}

void day_to_str(struct time_info_data* loc_time_info, char* str)
{
    char* s;
    int day;
    extern char* month_name[];

    day = loc_time_info->day + 1; /* day in [1..35] */

    s = nth(day);

    sprintf(str, "the %s day of %s", s, month_name[(int)loc_time_info->month]);

    free(s);
}

int find_player_in_table(char* name, int idnum)
{
    int i;

    for (i = 0; i <= top_of_p_table; i++)
        if (((idnum < 0) && (!str_cmp((player_table + i)->name, name))) || ((player_table + i)->idnum == idnum))
            break;

    if ((i > top_of_p_table) || (IS_SET((player_table + i)->flags, PLR_DELETED)))
        return -1;

    return i;
}

/*
 * Return a pointer to the character structure associated with
 * `idnum'; this is possible ONLY if a character is playing!
 * find_playing_char will return NULL if no such player was
 * found.
 */
struct char_data*
find_playing_char(int idnum)
{
    struct descriptor_data* d;
    extern struct descriptor_data* descriptor_list;

    for (d = descriptor_list; d; d = d->next)
        if ((d->character && d->connected == CON_PLYNG) && (d->character->specials2.idnum == idnum))
            return d->character;

    return NULL;
}

void set_mental_delay(struct char_data* ch, int value)
{
    if (!GET_MENTAL_DELAY(ch))
        set_fighting(ch, 0);

    GET_MENTAL_DELAY(ch) += value;
}

int universal_list_counter = 0;
int used_in_universal_list = 0;

/*
 * Takes the address of a linked list of universal_list structures
 * and its associated pool list and adds a new item at the beginning.
 * If a free universal_list structure is available in the pool it is
 * removed and returned.  If not, a new structure is created and
 * returned. Counts are kept of the number of universal list
 * structures created and the number in current use.
 */

struct universal_list*
pool_to_list(struct universal_list** list, struct universal_list** head)
{
    struct universal_list* tmplist;

    if (*head) {
        tmplist = *head;
        *head = tmplist->next;
        used_in_universal_list++;
    } else {
        CREATE1(tmplist, universal_list);
        universal_list_counter++;
        used_in_universal_list++;
    }

    tmplist->next = *list;
    *list = tmplist;

    return tmplist;
}

/*
 * Takes a list, its associated pool and a member of the list
 * and removes it from the list and adds it to the head of the
 * pool
 */
void from_list_to_pool(universal_list** list, universal_list** head, universal_list* body)
{
    if (*list == body) {
        *list = body->next;
    } else {
        universal_list* tmplist = NULL;
        for (tmplist = *list; tmplist->next; tmplist = tmplist->next) {
            if (tmplist->next == body) {
                break;
            }
        }

        if (tmplist->next == body) {
            tmplist->next = body->next;
        }
    }

    /* Thus not putting universal lists into a pool, but freeing the memory */
    used_in_universal_list--;
    universal_list_counter++; /* added because we are freeing body */

    free(body);
}

int check_resistances(char_data* victim, int attack_type)
{
    extern skill_data skills[];

    if ((attack_type < MAX_SKILLS) && IS_RESISTANT(victim, skills[attack_type].skill_spec))
        return 1;

    if ((attack_type < MAX_SKILLS) && IS_VULNERABLE(victim, skills[attack_type].skill_spec))
        return -1;

    if ((attack_type >= TYPE_HIT) && (attack_type <= TYPE_CRUSH) || attack_type == SKILL_ARCHERY) {
        if (IS_RESISTANT(victim, PLRSPEC_WILD))
            return 1;

        if (IS_VULNERABLE(victim, PLRSPEC_WILD))
            return -1;
    }

    return 0;
}

/*
 * Compare `obj' to its prototype; return 0 if the object
 * is altered, and 1 if it's the same.  If no prototype is
 * found, we return -1.
 */
int compare_obj_to_proto(struct obj_data* obj)
{
    int diff = 0, i;
    struct obj_data* tmp;
    struct extra_descr_data *edesc, *tmp_edesc;
    extern struct obj_data* obj_proto;
    extern int top_of_objt;
    int generic_scalp = 19;

    if (!obj) // If there's no object, what in tarnation are you trying?
        return -2;

    if (!(obj->item_number) || (obj->item_number < 0) || (obj->item_number > top_of_objt))
        return -2;

    // SPECIAL CASES, such as generic scalps/heads, go here!  Some items DO NOT
    // have prototypes, and will almost certainly cause the MUD to crash if they
    // are seen as valid.
    //
    // Since scalps, for one, have no value other than as ornaments, they can
    // be excluded.
    if (obj->item_number == generic_scalp)
        return 0;

    // Check if there is a prototype.
    tmp = &obj_proto[obj->item_number];

    if (!tmp)
        diff--;
    else {
        // Compare the easy stuff.
        if (str_cmp(obj->name, tmp->name))
            diff++;
        if (str_cmp(obj->description, tmp->description))
            diff++;
        if (str_cmp(obj->short_description, tmp->short_description))
            diff++;
        if (str_cmp(obj->action_description, tmp->action_description))
            diff++;

        // Compare extra descriptions.
        edesc = obj->ex_description;
        tmp_edesc = tmp->ex_description;

        if (edesc && tmp_edesc)
            do {
                if (!((edesc->keyword == tmp_edesc->keyword) && (!str_cmp(edesc->description, tmp_edesc->description))))
                    diff++;
                edesc = edesc->next;
                tmp_edesc = tmp_edesc->next;
            } while (tmp_edesc && edesc);

        // If there is still a description on one object, and not on the other...
        if ((edesc && !tmp_edesc) || (tmp_edesc && !edesc))
            diff++;

        // Compare flags.
        if (obj->obj_flags.value[0] != tmp->obj_flags.value[0])
            diff++;
        // See below for values 1, 2 and 3.
        if (obj->obj_flags.value[4] != tmp->obj_flags.value[4])
            diff++;
        if (obj->obj_flags.type_flag != tmp->obj_flags.type_flag)
            diff++;
        if (obj->obj_flags.wear_flags != tmp->obj_flags.wear_flags)
            diff++;
        if (obj->obj_flags.extra_flags != tmp->obj_flags.extra_flags)
            diff++;
        if (obj->obj_flags.cost != tmp->obj_flags.cost)
            diff++;
        if (obj->obj_flags.cost_per_day != tmp->obj_flags.cost_per_day)
            diff++;
        if (obj->obj_flags.timer != tmp->obj_flags.timer)
            diff++;
        if (obj->obj_flags.bitvector != tmp->obj_flags.bitvector)
            diff++;
        if (obj->obj_flags.level != tmp->obj_flags.level)
            diff++;
        if (obj->obj_flags.rarity != tmp->obj_flags.rarity)
            diff++;
        if (obj->obj_flags.material != tmp->obj_flags.material)
            diff++;
        if (obj->obj_flags.prog_number != tmp->obj_flags.prog_number)
            diff++;

        // Weight is special as this might be a container.
        if ((obj->obj_flags.type_flag != ITEM_CONTAINER) && (obj->obj_flags.type_flag != ITEM_DRINKCON) && (obj->obj_flags.weight != tmp->obj_flags.weight))
            diff++;

        // Value 1 is special for drink containers.
        if ((obj->obj_flags.type_flag != ITEM_DRINKCON) && (obj->obj_flags.value[1] != tmp->obj_flags.value[1]))
            diff++;
        // Values 2 and 3 are special for light sources.
        if (obj->obj_flags.type_flag != ITEM_LIGHT) {
            if (obj->obj_flags.value[2] != tmp->obj_flags.value[2])
                diff++;
            if (obj->obj_flags.value[3] != tmp->obj_flags.value[3])
                diff++;
        }

        // Compare affections.
        for (i = 0; i < MAX_OBJ_AFFECT; i++) {
            if (!((obj->affected[i].location == tmp->affected[i].location) && (obj->affected[i].modifier == tmp->affected[i].modifier)))
                diff++;
        }

        // Special check for enchant.  Enchant shouldn't make an object different.
        // Enchant applies APPLY_OB to an otherwise unaffected object.  It will
        // also apply one of (or both of) anti-good, anti-evil.
        if ((obj->obj_flags.type_flag == ITEM_WEAPON) && (tmp->obj_flags.type_flag == ITEM_WEAPON)) {
            // Any type of enchant.
            if ((obj->affected[0].location == APPLY_OB) && (tmp->affected[0].location != APPLY_OB))
                diff--;

            // First check if the object has ANTI_GOOD, ANTI_EVIL, or MAGIC on it in
            // the prototype.
            int obj_flags = 0;
            int tmp_flags = 0;

            if (IS_OBJ_STAT(obj, ITEM_MAGIC))
                obj_flags += ITEM_MAGIC;
            if (IS_OBJ_STAT(obj, ITEM_ANTI_GOOD))
                obj_flags += ITEM_ANTI_GOOD;
            if (IS_OBJ_STAT(obj, ITEM_ANTI_EVIL))
                obj_flags += ITEM_ANTI_EVIL;

            if (IS_OBJ_STAT(tmp, ITEM_MAGIC))
                tmp_flags += ITEM_MAGIC;
            if (IS_OBJ_STAT(tmp, ITEM_ANTI_GOOD))
                tmp_flags += ITEM_ANTI_GOOD;
            if (IS_OBJ_STAT(tmp, ITEM_ANTI_EVIL))
                tmp_flags += ITEM_ANTI_EVIL;

            // Now check the enchant.
            if ((obj->obj_flags.extra_flags != tmp->obj_flags.extra_flags) && (obj->obj_flags.extra_flags - obj_flags + tmp_flags == tmp->obj_flags.extra_flags))
                diff--;
        }
    }

    return diff;
}

struct obj_data* obj_to_proto(struct obj_data* obj)
{
    // Converts an existing object to a copy of its prototype.
    int i, aff_count = 0;
    struct obj_data* tmp;
    struct extra_descr_data *edesc, *tmp_edesc;
    extern struct obj_data* obj_proto;
    extern struct index_data* obj_index;
    struct obj_data* new_obj;

    CREATE(new_obj, struct obj_data, 1);

    // Get the prototype.
    tmp = &obj_proto[obj->item_number];

    // Copy everything over.
    new_obj->item_number = tmp->item_number;
    new_obj->owner = obj->owner;
    new_obj->carried_by = obj->carried_by;
    new_obj->in_obj = obj->in_obj;
    new_obj->name = str_dup(tmp->name);
    new_obj->description = str_dup(tmp->description);
    new_obj->short_description = str_dup(tmp->short_description);
    new_obj->action_description = str_dup(tmp->action_description);

    // Copy extra descriptions.
    tmp_edesc = tmp->ex_description;

    if (tmp_edesc) {
        CREATE1(new_obj->ex_description, extra_descr_data);
        edesc = new_obj->ex_description;

        do {
            edesc->keyword = str_dup(tmp_edesc->keyword);
            edesc->description = str_dup(tmp_edesc->description);

            if (tmp_edesc->next) {
                tmp_edesc = tmp_edesc->next;
                CREATE1(edesc->next, extra_descr_data);
                edesc = edesc->next;
            }
        } while (tmp_edesc);
    }

    // Copy flags.
    new_obj->obj_flags.value[0] = tmp->obj_flags.value[0];
    new_obj->obj_flags.value[1] = tmp->obj_flags.value[1];
    new_obj->obj_flags.value[2] = tmp->obj_flags.value[2];
    new_obj->obj_flags.value[3] = tmp->obj_flags.value[3];
    new_obj->obj_flags.value[4] = tmp->obj_flags.value[4];
    new_obj->obj_flags.type_flag = tmp->obj_flags.type_flag;
    new_obj->obj_flags.wear_flags = tmp->obj_flags.wear_flags;
    new_obj->obj_flags.extra_flags = tmp->obj_flags.extra_flags;
    new_obj->obj_flags.weight = tmp->obj_flags.weight;
    new_obj->obj_flags.cost = tmp->obj_flags.cost;
    new_obj->obj_flags.cost_per_day = tmp->obj_flags.cost_per_day;
    new_obj->obj_flags.timer = tmp->obj_flags.timer;
    new_obj->obj_flags.bitvector = tmp->obj_flags.bitvector;
    new_obj->obj_flags.level = tmp->obj_flags.level;
    new_obj->obj_flags.rarity = tmp->obj_flags.rarity;
    new_obj->obj_flags.material = tmp->obj_flags.material;
    new_obj->obj_flags.prog_number = tmp->obj_flags.prog_number;

    // If a prototype has any affections, it doesn't matter if the original
    // object is enchanted, because enchant will go away.  So, copy all of
    // the prototype's affections.
    for (i = 0; i < MAX_OBJ_AFFECT; i++) {
        new_obj->affected[i].location = tmp->affected[i].location;
        new_obj->affected[i].modifier = tmp->affected[i].modifier;
        if (tmp->affected[i].location > 0)
            aff_count = 1;
    }

    // Now, if the object was enchanted, and the prototype isn't affected,
    // restore the enchant.
    if (!aff_count) {
        // No affections on the prototype.  Apply away.
        // Only do this if the apply is APPLY_OB!
        if (obj->affected[0].location == APPLY_OB) {
            new_obj->affected[0].location = obj->affected[0].location;
            new_obj->affected[0].modifier = obj->affected[0].modifier;

            int obj_flags = 0;

            if (IS_OBJ_STAT(obj, ITEM_MAGIC))
                obj_flags += ITEM_MAGIC;
            if (IS_OBJ_STAT(obj, ITEM_ANTI_GOOD))
                obj_flags += ITEM_ANTI_GOOD;
            if (IS_OBJ_STAT(obj, ITEM_ANTI_EVIL))
                obj_flags += ITEM_ANTI_EVIL;

            if (IS_OBJ_STAT(new_obj, ITEM_MAGIC))
                obj_flags -= ITEM_MAGIC;
            if (IS_OBJ_STAT(new_obj, ITEM_ANTI_GOOD))
                obj_flags -= ITEM_ANTI_GOOD;
            if (IS_OBJ_STAT(new_obj, ITEM_ANTI_EVIL))
                obj_flags -= ITEM_ANTI_EVIL;

            new_obj->obj_flags.extra_flags += obj_flags;
        }
    }

    // Now move the contents from the old object to the new.
    tmp = obj->contains;

    while (tmp) {
        obj_from_obj(tmp);
        obj_to_obj(tmp, new_obj);
        tmp = obj->contains;
    };

    // The new object must be touched if the old one was touched.
    if (obj->touched == 1) {
        new_obj->touched = 1;
    }

    extract_obj(obj);
    (obj_index[new_obj->item_number].number)++;

    return new_obj;
}

void check_inventory_proto(struct char_data* ch)
{
    int result = 0;
    struct obj_data* tmp;

    for (tmp = ch->carrying; tmp; tmp = tmp->next_content) {
        if (tmp->obj_flags.type_flag == ITEM_CONTAINER)
            check_container_proto(tmp, ch);

        result = compare_obj_to_proto(tmp);
        if (result > 0) {
            char buf[1024];

            sprintf(buf, " - An object in your inventory, %s, was updated.\n\r", tmp->short_description);
            send_to_char(buf, ch);
            obj_to_char(obj_to_proto(tmp), ch);
        } else if (result < 0) {
            char buf[1024];

            sprintf(buf, " - An object in your inventory, %s, has no prototype.  Please notify imps.\n\r",
                tmp->short_description);
        }
    }
}

void check_equipment_proto(struct char_data* ch)
{
    int i, result = 0;
    struct obj_data* tmp;

    for (i = 0; i < MAX_WEAR; i++) {
        tmp = ch->equipment[i];

        if (tmp) {
            if (tmp->obj_flags.type_flag == ITEM_CONTAINER)
                check_container_proto(tmp, ch);

            result = compare_obj_to_proto(tmp);
            if (result > 0) {
                char buf[1024];

                sprintf(buf, " - An object in your equipment, %s, was updated.\n\r", tmp->short_description);
                send_to_char(buf, ch);
                obj_to_char(obj_to_proto(tmp), ch);
            } else if (result < 0) {
                char buf[1024];

                sprintf(buf, " - An object in your inventory, %s, has no prototype.  Please notify imps.\n\r",
                    tmp->short_description);
            }
        }
    }
}

static void check_container_proto(struct obj_data* obj, struct char_data* ch)
{
    int result = 0;
    struct obj_data* tmp;

    for (tmp = obj->contains; tmp; tmp = tmp->next_content) {
        if (tmp->obj_flags.type_flag == ITEM_CONTAINER)
            check_container_proto(tmp, ch);

        result = compare_obj_to_proto(tmp);
        if (result > 0) {
            char buf[1024];

            sprintf(buf, " - An object in %s, %s, was updated.\n\r", obj->short_description, tmp->short_description);
            send_to_char(buf, ch);
            obj_to_char(obj_to_proto(tmp), ch);
        } else if (result < 0) {
            char buf[1024];

            sprintf(buf, " - An object in your inventory, %s, has no prototype.  Please notify imps.\n\r",
                tmp->short_description);
        }
    }
}

/*
 * Show 'target' to 'observer' if 'observer' can see 'target'.
 * Enemies are shown as starred races, colored by 'observer's
 * enemy color preference.  If 'target' is a recruited orc mob,
 * it will show up as an enemy.
 *
 * If 'capitalize' is true, then we attempt to capitalize the
 * final string.  If 'force_visible' is true, then we assume
 * that 'observer' can see 'target' even if they fail CAN_SEE.
 * force_visible is notably used by global communications,
 * where we always want everyone to appear visible.
 *
 * NOTE: It is the responsibility of the -caller- to preserve
 * any color settings in a string where PERS is used!
 */
char* PERS(struct char_data* target, struct char_data* observer,
    int capitalize, int force_visible)
{
    static char name[128];

    if (CAN_SEE(observer, target) || force_visible) {
        if (other_side(observer, target)) {
            snprintf(name, 127, "%s%s%s",
                CC_USE(observer, COLOR_ENMY),
                pc_star_types[GET_RACE(target)],
                CC_NORM(observer));
        } else if (IS_NPC(target) && MOB_FLAGGED(target, MOB_ORC_FRIEND) && MOB_FLAGGED(target, MOB_PET) && other_side(target, observer)) {
            snprintf(name, 127, "%s%s%s",
                CC_USE(observer, COLOR_ENMY),
                pc_star_types[GET_RACE(target)],
                CC_NORM(observer));
        } else
            snprintf(name, 127, "%s", GET_NAME(target));

        name[127] = '\0';
    } else
        sprintf(name, "someone");

    if (capitalize)
        CAP(name);

    return name;
}

int has_alias(char_data* host, char* keyword)
{
    if (strstr(host->player.name, keyword)) {
        return 1;
    } else {
        return 0;
    }
}

int has_program(char_data* host, int num)
{
    if ((int)host->specials.store_prog_number == num) {
        return 1;
    } else {
        return 0;
    }
}
