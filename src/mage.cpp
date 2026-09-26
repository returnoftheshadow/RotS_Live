/* ************************************************************************
 *   File: mage.c                                        Part of CircleMUD *
 *  Usage: actual effects of mage only spells                              *
 *                                                                         *
 *  All rights reserved.  See license.doc for complete information.        *
 *                                                                         *
 *  Copyright (C) 1993 by the Trustees of the Johns Hopkins University     *
 *  CircleMUD is based on DikuMUD, Copyright (C) 1990, 1991.               *
 ************************************************************************ */

#include "char_utils.h"
#include "comm.h"
#include "db.h"
#include "handler.h"
#include "interpre.h"
#include "platdef.h"
#include "spells.h"
#include "structs.h"
#include "utils.h"
#include "warrior_spec_handlers.h"
#include "zone.h" /* For zone_table */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern struct room_data world;

#define RACE_SOME_ORC(caster) ((GET_RACE(caster) == RACE_URUK || GET_RACE(caster) == RACE_ORC || GET_RACE(caster) == RACE_MAGUS))

int apply_spell_damage(char_data* caster, char_data* victim, int damage_dealt, int spell_number, int hit_location);
bool new_saves_spell(const char_data* caster, const char_data* victim, int save_bonus);

int get_mage_caster_level(const char_data* caster)
{
    int mage_level = utils::get_prof_level(PROF_MAGE, *caster);

    // Factor in intel values not divisible by 5.
    int intel_factor = caster->tmpabilities.intel / 5;
    if (number(0, intel_factor % 5) > 0) {
        ++intel_factor;
    }

    return mage_level + intel_factor;
}

int get_magic_power(const char_data* caster)
{
    player_spec::battle_mage_handler battle_mage_handler(caster);
    int caster_level = get_mage_caster_level(caster);
    int level_modifier = GET_MAX_RACE_PROF_LEVEL(PROF_MAGE, caster) * GET_LEVELA(caster) / 30;
    caster_level += battle_mage_handler.get_bonus_spell_power(caster->points.spell_power);

    // Factor in intel values not divisible by 5.
    int intel_factor = caster->tmpabilities.intel / 5;
    if (number(0, intel_factor % 5) > 0) {
        ++intel_factor;
    }

    return caster_level + level_modifier + intel_factor;
}

bool should_apply_spell_penetration(const char_data* caster)
{
    bool apply_spell_pen = utils::is_pc(*caster);
    if (apply_spell_pen == false) {
        if (utils::is_mob_flagged(*caster, MOB_ORC_FRIEND) && utils::is_affected_by(*caster, AFF_CHARM)) {
            apply_spell_pen = caster->master && utils::is_pc(*(caster->master));
        }
    }
    return apply_spell_pen;
}

double get_spell_pen_value(const char_data* caster)
{
    int mage_level = utils::get_prof_level(PROF_MAGE, *caster);
    if (utils::is_npc(*caster) && utils::is_affected_by(*caster, AFF_CHARM) && caster->master) {
        mage_level += utils::get_prof_level(PROF_MAGE, *(caster->master)) / 3;
    }

    return mage_level / 5.0;
}

double get_victim_saving_throw(const char_data* caster, const char_data* victim)
{
    double saving_throw = victim->specials2.saving_throw; // this value comes from gear and/or spells.

    if (should_apply_spell_penetration(caster)) {
        double spell_pen = get_spell_pen_value(caster);
        saving_throw = saving_throw - spell_pen;
        if (utils::is_pc(*victim)) {
            // Players that are a higher level than their attacker's mage level
            // get some innate damage reduction.  Players at a lower level take
            // extra damage.
            saving_throw += utils::get_level_a(*victim) / 5.0;
        }
    }

    return saving_throw;
}

int apply_spell_damage(char_data* caster, char_data* victim, int damage_dealt, int spell_number, int hit_location)
{
    double saving_throw = get_victim_saving_throw(caster, victim);

    double damage_multiplier = 1.0;
    if (saving_throw > 0) {
        damage_multiplier = 20.0 / (20.0 + saving_throw);
    } else if (saving_throw < 0) {
        damage_multiplier = 2.0 - (20.0 / (20.0 - saving_throw));
    }

    damage_dealt = int(damage_dealt * damage_multiplier);

    return damage(caster, victim, damage_dealt, spell_number, hit_location);
}

bool different_zone(int was_in, int to_room)
{
    return world[was_in].zone != world[to_room].zone;
}

/*
 * external structures
 */

extern struct obj_data* obj_proto;
extern struct obj_data* object_list;
extern struct char_data* character_list;
extern void prohibit_item_stay_zone_move(char_data* ch, int was_in);
extern int rev_dir[];
extern int top_of_world;
extern char* dirs[];
extern char* refer_dirs[];
extern char* room_bits[];
extern char* sector_types[];

/*
 * external functions
 */
void list_char_to_char(struct char_data* list, struct char_data* caster, int mode);
// void do_stat_object(struct char_data *caster, struct obj_data *j);
void stop_hiding(struct char_data* caster, char);
ACMD(do_look);
void do_identify_object(struct char_data*, struct obj_data*);

char saves_spell(char_data* victim, sh_int caster_level, int bonus);
bool new_saves_spell(const char_data* caster, const char_data* victim, int save_bonus);

/*
 *Spells are listed below have been split into five categories.
 * - Non Offensive spells.
 * - Teleportation spells.
 * - Offensive spells.
 * - Uruk Lhuth spells.
 * - Specilization spells.
 *Each group of spells is seperated by a double commented striaght line, with the group
 * name and which spells are withing the code segement.
 *Each single Spell function is seperated by a _SINGLE_ commented line, under which the
 * spells general details will commented.
 *I'm quite aware at the fact that this is probably not needed, but i think this is a
 * good practice, as it makes code easily readable.
 * Khronos 26/03/05
 */

/*-----------------------------------------------------------------------------------------------------------*/
/*-----------------------------------------------------------------------------------------------------------*/
/*
 * Non Offensive Spells
 * Spells in this segement in order
 * - Create Light
 * - Locate Living (plus the locate life coordinates function)
 * - Cure Self
 * - Detect Evil
 * - Reveal Life
 * - Shield
 * - Flash
 * - Vitalize Self
 * - Summon
 * - Identify
 */

/*-----------------------------------------------------------------------------------------------------------*/
/*
 * Spell Create light
 * Basically loads object 7006 (a brilliant orb of light) to the caster
 */

ASPELL(spell_create_light)
{
    struct obj_data* tmpobj;
    int object_vnum = 7006;

    if (!caster)
        return;

    object_vnum = real_object(object_vnum);
    tmpobj = read_object(object_vnum, REAL);

    if (!tmpobj) {
        send_to_char("Spell Error - Notify Imms.\r\n", caster);
        return;
    }

    obj_to_char(tmpobj, caster);
    act("$p materializes in your hands.", FALSE, caster, tmpobj, 0, TO_CHAR);
    return;
}

/*----------------------------------------------------------------------------------------------------------*/
/*
 *These functions  provide the coordinates for the locate life spell
 */
struct loclife_coord {
    int number; // room number in world[]
    signed char n; //
    signed char e; // north/east/up coordinates
    signed char u; //
};

int loclife_add_rooms(loclife_coord room, loclife_coord* roomlist,
    int* roomnum, int room_not)
{
    int tmp, dmp, count;
    int dirarray[NUM_OF_DIRS];
    struct room_direction_data* exptr;

    for (tmp = 0; tmp < NUM_OF_DIRS; tmp++)
        dirarray[tmp] = tmp;

    reshuffle(dirarray, NUM_OF_DIRS);
    count = 0;

    for (dmp = 0; dmp < NUM_OF_DIRS; dmp++) {
        exptr = world[room.number].dir_option[dirarray[dmp]];

        if (exptr) {
            if (!(IS_SET(exptr->exit_info, EX_CLOSED) && IS_SET(exptr->exit_info, EX_DOORISHEAVY)) && (exptr->to_room != NOWHERE) && (exptr->to_room != room_not)) {
                for (tmp = 0; tmp < *roomnum; tmp++)
                    if (roomlist[tmp].number == exptr->to_room)
                        break;

                if ((tmp == *roomnum) && (*roomnum < 254)) {
                    roomlist[*roomnum].number = exptr->to_room;
                    roomlist[*roomnum].n = room.n;
                    roomlist[*roomnum].e = room.e;
                    roomlist[*roomnum].u = room.u;
                    switch (dirarray[dmp]) {
                    case 0: /* north */
                        roomlist[*roomnum].n++;
                        break;
                    case 1: /* east  */
                        roomlist[*roomnum].e++;
                        break;
                    case 2: /* south */
                        roomlist[*roomnum].n--;
                        break;
                    case 3: /* west  */
                        roomlist[*roomnum].e--;
                        break;
                    case 4: /* up    */
                        roomlist[*roomnum].u++;
                        break;
                    case 5: /* down  */
                        roomlist[*roomnum].u--;
                        break;
                    };
                    (*roomnum)++;
                    count++;
                }
            }
        }
    }
    return count;
}
const char* loclife_dirnames[216] = {
    /*
     *Lines - up/down here - farthest down - farthest up
     *columns - east/west
     * groups - north/south
     * (n/s, e/w, u/d)
     */
    "here",
    "down",
    "down",
    "up",
    "up",
    "west",
    "west and down",
    "west and down",
    "west and up",
    "up and west and up",
    "west",
    "west and down",
    "west and down",
    "west and up",
    "up and west and up",
    "east",
    "east and down",
    "east and down",
    "east and up",
    "up and east and up",
    "east",
    "east and down",
    "east and down",
    "east and up",
    "up and east and up",

    "south",
    "south and down",
    "south and down",
    "south and up",
    "up and south",
    "southwest",
    "southwest and down",
    "southwest and down",
    "southwest and up",
    "up and southwest",
    "west-southwest",
    "west-southwest and down",
    "west-southwest and down",
    "west-southwest and up",
    "up and west-southwest",
    "southeast",
    "southeast and down",
    "southeast and down",
    "southeast and up",
    "up and southeast",
    "east-southeast",
    "east-southeast and down",
    "east-southeast and down",
    "east-southeast and up",
    "up and east-southeast",

    "south",
    "south and down",
    "south and down",
    "south and up",
    "up and south",
    "south-southwest",
    "south-southwest and down",
    "south-southwest and down",
    "south-southwest and up",
    "up and south-southwest",
    "southwest",
    "southwest and down",
    "southwest and down",
    "southwest and up",
    "up and southwest",
    "south-southeast",
    "south-southeast and down",
    "south-southeast and down",
    "south-southeast and up",
    "up and south-southeast",
    "southeast",
    "southeast and down",
    "southeast and down",
    "southeast and up",
    "up and southeast",

    "north",
    "north and down",
    "north and down",
    "north and up",
    "up and north",
    "northwest",
    "northwest and down",
    "northwest and down",
    "northwest and up",
    "up and northwest",
    "west-northwest",
    "west-northwest and down",
    "west-northwest and down",
    "west-northwest and up",
    "up and west-northwest",
    "northeast",
    "northeast and down",
    "northeast and down",
    "northeast and up",
    "up and northeast",
    "east-northeast",
    "east-northeast and down",
    "east-northeast and down",
    "east-northeast and up",
    "up and east-northeast",

    "north",
    "north and down",
    "north and down",
    "north and up",
    "up and north and up",
    "north-northwest",
    "north-northwest and down",
    "north-northwest and down",
    "north-northwest and up",
    "up and north-northwest",
    "northwest",
    "northwest and down",
    "northwest and down",
    "northwest and up",
    "up and northwest",
    "north-northeast",
    "anorth-northeast nd down",
    "north-northeast and down",
    "north-northeast and up",
    "up and north-northeast",
    "northeast",
    "northeast and down",
    "northeast and down",
    "northeast and up",
    "up and northeast",

};
char* loclife_dir_convert(loclife_coord rm)
{
    signed char nr, er, ur;

    nr = abs(rm.n);
    er = abs(rm.e);
    ur = abs(rm.u);

    if (nr > 2)
        nr = 2;
    if (er > 2)
        er = 2;
    if (ur > 2)
        ur = 2;

    if (rm.n > 0)
        nr += 2;
    if (rm.e > 0)
        er += 2;
    if (rm.u > 0)
        ur += 2;

    return (char*)loclife_dirnames[(nr * 5 + er) * 5 + ur];
}

/*--------------------------------------------------------------------------------------------------------*/

/*
 * Spell locate living
 * Uses the above functions to search for mobs within the
 * casters immediate area, then displays the result
 */
ASPELL(spell_locate_living)
{
    struct char_data* mobs;
    int isscanned[255];
    int tmp, tmp2, roomnum, num, notscanned, bigcount, roomrange, mobrange;
    char buf[255];
    loclife_coord roomlist[255];
    loclife_coord new_room;

    if (caster->in_room == NOWHERE)
        return;

    for (tmp = 0; tmp < 255; tmp++)
        roomlist[tmp].number = roomlist[tmp].n = roomlist[tmp].e = roomlist[tmp].u = isscanned[tmp] = 0;
    roomnum = 0;

    int level = get_mage_caster_level(caster);
    roomrange = 5 + level / 3;
    mobrange = 2 + level / 3;

    new_room.number = caster->in_room;
    new_room.n = new_room.e = new_room.u = 0;
    notscanned = loclife_add_rooms(new_room, roomlist, &roomnum,
        caster->in_room);

    bigcount = 0;
    while ((notscanned > 0) && (bigcount < 255) && (roomnum < roomrange)) {
        num = number(0, notscanned - 1);
        for (tmp = 0, tmp2 = 0; (tmp2 < num) || (isscanned[tmp]); tmp++)
            if (!isscanned[tmp])
                tmp2++;

        isscanned[tmp] = 1;
        notscanned += -1 + loclife_add_rooms(roomlist[tmp], roomlist, &roomnum, caster->in_room);
        bigcount++;
    }

    int caster_level = get_mage_caster_level(caster);
    bigcount = 0;
    for (tmp = 0; (tmp < roomnum) && (bigcount < mobrange); tmp++) {
        mobs = world[roomlist[tmp].number].people;
        while (mobs && (bigcount < mobrange)) {
            if (!new_saves_spell(caster, mobs, 0)) {
                sprintf(buf, "%s at %s to the %s.\n\r",
                    (IS_NPC(mobs) ? GET_NAME(mobs) : pc_star_types[mobs->player.race]),
                    world[roomlist[tmp].number].name,
                    loclife_dir_convert(roomlist[tmp]));
                send_to_char(buf, caster);
            }
            bigcount++;
            mobs = mobs->next_in_room;
        }
    }
    if (bigcount)
        send_to_char("You could not further concentrate.\n\r", caster);
    else
        send_to_char("The area seems to be empty.\n\r", caster);
}

/*----------------------------------------------------------------------------------------------------------*/
/*
 * Self explanatory this one
 * Simply adds hit points to your current hit points
 */

ASPELL(spell_cure_self)
{
    affected_type* af;
    af = affected_by_spell(caster, SKILL_MARK);

    if (caster->tmpabilities.hit >= caster->abilities.hit) {
        if (!af) {
            send_to_char("You are already healthy.\n\r", caster);
            return;
        } else {
            af->duration = std::max(af->duration - 30, 0);
            send_to_char("The festering wound on your side starts to close up.\n\r", caster);
            return;
        }
    }

    if (af) {
        af->duration = std::max(af->duration - 20, 0);
    }

    int caster_level = get_mage_caster_level(caster);
    int health_restored = caster_level / 2 + 10;
    if (utils::get_specialization(*caster) == game_types::PS_Regeneration) {
        health_restored += 5;
    }

    caster->tmpabilities.hit = std::min(caster->abilities.hit, caster->tmpabilities.hit + health_restored);
    send_to_char("You feel better.\n\r", caster);
}

/*----------------------------------------------------------------------------------------------------------*/

/*
 * Gets the zone allignment, the same as our mage sense ability
 * Do we need this as a spell, Does anyone even use it?
 */

ASPELL(spell_detect_evil)
{
    if (report_zone_power(caster) == -1)
        send_to_char("Evil power is very strong in this place.\n\r", caster);
    else
        send_to_char("You cannot detect the presence of evil here.\n\r", caster);
}

/*----------------------------------------------------------------------------------------------------------*/
/*
 * Saving a reveal spell:
 * Saving a reveal spell is a tricky deal, since we're dealing
 * with so many factors.  These are:
 *   - the hider's spell save: half of the hider's spell save
 *     is effective when determining a save against a reveal
 *     spell; this is accomplished by adding a negative bonus
 *     to the saves_spell function.
 *   - the hider's hide level: for every x points of hide level
 *     the hider is bonused by one spell save.  This x is
 *     dependent on the spell being cast; word of sight has a
 *     slightly higher x, thus making it harder to save.
 *   - the LEVELA/GET_PROF_MAGE difference: since the LEVELA /
 *     GET_PROF_MAGE difference is so extreme (in order to give
 *     offensive mid-ranged mages a much harder time), we have
 *     to give these mid-mages a bit of a bonus for reveal,
 *     otherwise, it'd be absolutely useless at lower levels,
 *     and this is not the intent of the LEVELA/GPM difference.
 *     We achieve this difference in the 'negative bonus' way.
 *
 * Additionally, when a reveal spell saves, it has a decent
 * chance to lower the hider's GET_HIDING value (hide level);
 * this chance is slightly higher for word of sight than it is
 * for reveal life.
 */

ASPELL(spell_reveal_life)
{
    struct char_data* hider;
    int found, hider_bonus;

    if (caster->in_room == NOWHERE)
        return;

    act("Suddenly, a flash of intense light floods your surroundings.", TRUE, caster, 0, 0, TO_ROOM);
    send_to_char("A surge of light reveals to you every corner of the room.\n\r", caster);

    int level = get_mage_caster_level(caster);

    for (hider = world[caster->in_room].people, found = 0; hider;
         hider = hider->next_in_room) {
        if (hider != caster) {
            if (GET_HIDING(hider) > 0) {
                hider_bonus = GET_HIDING(hider) / 25;
                hider_bonus += number(0, GET_HIDING(hider)) % 25 ? 1 : 0;
                hider_bonus -= (30 - level) / 3;
                if (!new_saves_spell(caster, hider, hider_bonus)) {
                    stop_hiding(hider, FALSE);
                    send_to_char("You've been discovered!\r\n", hider);
                    found = 1;
                } else if (!number(0, GET_HIDING(hider) / 35)) {
                    send_to_char("The intense glow fades, and you remain hidden; "
                                 "yet you feel strangely insecure.\r\n",
                        hider);
                    GET_HIDING(hider) -= GET_PROF_LEVEL(PROF_MAGE, caster) / 3;
                } else
                    send_to_char("The bright light fades, and you seem to have "
                                 "remained undiscovered.\r\n",
                        hider);
            }
            /* not hiding? no contest, we found you */
            else
                found = 1;
        }
    }

    if (!found)
        send_to_char("The place seems empty.\n\r", caster);
    else
        list_char_to_char(world[caster->in_room].people, caster, 0);
}

/*----------------------------------------------------------------------------------------------------------*/
/* Spell Shield
 * Gives the caster a magical shield that absorbs damage
 * which is currently hugely disproportioned to the actual
 * ammount of mana lose, making this spell as it stands very
 * overpowered. IT NEEDS TO BE CHANGED
 * Khronos 27/03/05
 */

ASPELL(spell_shield)
{
    if (!victim)
        victim = caster;
    if (affected_by_spell(victim, SPELL_SHIELD)) {
        send_to_char("You are already protected by a magical shield.\n\r", caster);
        return;
    }

    int level = get_mage_caster_level(caster);
    if (utils::get_specialization(*caster) == game_types::PS_Protection) {
        level += 5;
    }

    affected_type af;
    af.type = SPELL_SHIELD;
    af.duration = level + 5;
    af.modifier = level * 5; /* % of HP */
    af.bitvector = AFF_SHIELD;
    af.location = APPLY_NONE;

    affect_to_char(victim, &af);
    send_to_char("You surround yourself with a magical shield.\n\r", victim);
}

/*----------------------------------------------------------------------------------------------------------*/

/*
 * Flash Spell Whitie Only
 * Disengages the caster from their victim
 * Gives darkie races POA when cast in the same room as them
 * Should we prehaps give Darkies a similar ability?
 */

ASPELL(spell_flash)
{
    char_data* tmpch;
    affected_type* tmpaf;
    affected_type newaf;
    int afflevel, maxlevel;

    if (caster->in_room < 0)
        return;

    int caster_level = get_mage_caster_level(caster);

    for (tmpch = world[caster->in_room].people; tmpch; tmpch = tmpch->next_in_room) {
        if (tmpch != caster)
            send_to_char("A blinding flash of light makes you dizzy.\n\r", tmpch);
        if (tmpch->specials.fighting)
            GET_ENERGY(tmpch) -= 400;
        if (tmpch->specials.fighting == caster)
            GET_ENERGY(tmpch) -= 400;
        if (!new_saves_spell(caster, tmpch, 0)) {
            //  6-11-01 Errent attempts to make flash give power of arda to darkies - look out!
            if (RACE_EVIL(tmpch) && GET_RACE(tmpch) != RACE_HARADRIM) {
                afflevel = 50;
                maxlevel = afflevel * 25;
                tmpaf = affected_by_spell(tmpch, SPELL_ARDA);
                if (tmpaf) {
                    if (tmpaf->modifier > maxlevel) {
                        tmpaf->modifier = std::max(tmpaf->modifier - afflevel, 30);
                    } else {
                        tmpaf->modifier = std::min(tmpaf->modifier + afflevel, 400);
                    }

                    tmpaf->duration = 100; // to keep the affection going

                } else {
                    newaf.type = SPELL_ARDA;
                    newaf.duration = 400; // immaterial this figure
                    newaf.modifier = afflevel;
                    newaf.location = APPLY_NONE;
                    newaf.bitvector = 0;
                    affect_to_char(tmpch, &newaf);
                    send_to_char("The power of Arda weakens your body.\n\r", tmpch);
                }
            }

            if (tmpch->specials.fighting) {
                stop_fighting(tmpch);
            }
            if ((tmpch != caster) && (tmpch->delay.wait_value > 0) && (GET_WAIT_PRIORITY(tmpch) < 40)) {
                break_spell(tmpch);
            }
            if (tmpch->specials.fighting)
                GET_ENERGY(tmpch) -= 400;
        }
    }
    send_to_char("You produce a bright flash of light.\n\r", caster);
}

/*----------------------------------------------------------------------------------------------------------*/

/*
 * Spell vitalize self
 * Gives the caster back a number of moves based
 * on the casters mage level
 */
ASPELL(spell_vitalize_self)
{
    if (caster->tmpabilities.move >= caster->abilities.move) {
        send_to_char("You are already rested.\n\r", caster);
        return;
    }

    int caster_level = get_mage_caster_level(caster);
    int moves_restored = 2 * caster_level;
    if (utils::get_specialization(*caster) == game_types::PS_Regeneration) {
        moves_restored += 10;
    }

    caster->tmpabilities.move = std::min(caster->abilities.move, (sh_int)(caster->tmpabilities.move + moves_restored));
    send_to_char("You feel refreshed.\n\r", caster);
    return;
}

/*----------------------------------------------------------------------------------------------------------*/
/*
 * Summon spell
 * Transfers a character from where they are to
 * the casters room.
 * We don't use this spell anymore should it be completely
 * removed?
 */

ASPELL(spell_summon)
{
    int ch_x, ch_y, v_x, v_y, dist;
    int was_in, to_room;

    if (!victim)
        return;
    if (caster->in_room == NOWHERE)
        return;

    if (IS_NPC(victim)) {
        send_to_char("No such player in the world.\n\r", caster);
        return;
    }

    if (victim == caster) {
        send_to_char("You are already here.\n\r", caster);
        return;
    }

    if (victim->in_room == caster->in_room) {
        act("$E is already here.\n\r", FALSE, caster, 0, victim, TO_CHAR);
        return;
    }

    if ((GET_POS(victim) == POSITION_FIGHTING) || (IS_SET(world[caster->in_room].room_flags, NO_TELEPORT)) || (PRF_FLAGGED(victim, PRF_SUMMONABLE)) || (GET_LEVEL(victim) >= LEVEL_IMMORT)) {
        send_to_char("You failed.\n\r", caster);
        return;
    }

    affected_type* angerAffect = affected_by_spell(victim, SPELL_ANGER);
    if (angerAffect && angerAffect->duration > 2) {
        send_to_char("Player has engaged in PK too recently to be summoned.", caster);
        return;
    }

    was_in = victim->in_room;
    to_room = caster->in_room;

    if (different_zone(was_in, to_room)) {
        prohibit_item_stay_zone_move(victim, was_in);
    }

    ch_x = zone_table[world[caster->in_room].zone].x;
    ch_y = zone_table[world[caster->in_room].zone].y;
    v_x = zone_table[world[victim->in_room].zone].x;
    v_y = zone_table[world[victim->in_room].zone].y;
    dist = ((ch_x - v_x) ^ 2) + ((ch_y - v_y) ^ 2);

    int save_bonus = dist;
    /* Make high level mobs harder to summon */
    if (victim->player.level > 10 && IS_NPC(victim)) {
        save_bonus += victim->player.level / 3;
    }

    if (!new_saves_spell(caster, victim, save_bonus) && !other_side(caster, victim)) {
        act("$N appears in the room.", TRUE, caster, 0, victim, TO_ROOM);
        act("$N appears in the room.", TRUE, caster, 0, victim, TO_CHAR);
        if (IS_RIDING(victim))
            stop_riding(victim);
        char_from_room(victim);
        char_to_room(victim, caster->in_room);
        act("$N summons you!", FALSE, victim, 0, caster, TO_CHAR);
        do_look(victim, "", 0, 0, 0);
        extern void msdp_room_update(char_data * ch);

        msdp_room_update(victim);
    } else
        send_to_char("You failed.\n\r", caster);
}

/*----------------------------------------------------------------------------------------------------------*/

/*
 * Spell Identify
 * Allows mortals to use the vstat command!
 * Gives details on objects
 */

ASPELL(spell_identify)
{
    struct affected_type af;

    if (!obj) {
        send_to_char("You should cast this on objects only.\n\r", caster);
        return;
    }

    do_identify_object(caster, obj);
    int level = get_mage_caster_level(caster);
    GET_MOVE(caster) = GET_MOVE(caster) - level;
}

/*----------------------------------------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------------------------------------*/
/*----------------------------------------------------------------------------------------------------------*/

/* Teleportation Spells
 *Spells in this segement in order
 * - Blink (and blink related funcations)
 * - Relocate
 * - Beacon
 * On the note of Teleportation spells they are very
 * UnTolkienish, even moreso than our other spells
 * should we perhaps give thought to removing them from the
 * game completely, with the possible exception of blink
 * Khronos 27/03/05
 */

/*----------------------------------------------------------------------------------------------------------*/

int random_exit(int room)
{
    // selects a random exit from the room (real number 'room')
    // and returns the real number of the new room.

    int tmp, num, res;
    sh_int romfl;
    int ex_rooms[NUM_OF_DIRS];

    if ((room < 0) || (room > top_of_world))
        return NOWHERE;

    for (tmp = 0, num = 0; tmp < NUM_OF_DIRS; tmp++)
        ex_rooms[tmp] = 0;

    for (tmp = 0, num = 0; tmp < NUM_OF_DIRS; tmp++)
        if (world[room].dir_option[tmp])
            if ((world[room].dir_option[tmp]->to_room != NOWHERE) && ((!IS_SET(world[room].dir_option[tmp]->exit_info, EX_DOORISHEAVY)) || (!IS_SET(world[room].dir_option[tmp]->exit_info, EX_CLOSED))) && !IS_SET(world[room].dir_option[tmp]->exit_info, EX_NOBLINK)) {

                romfl = world[world[room].dir_option[tmp]->to_room].room_flags;

                if (!IS_SET(romfl, DEATH) && !IS_SET(romfl, NO_MAGIC) && !IS_SET(romfl, SECURITYROOM)) {
                    ex_rooms[num] = world[room].dir_option[tmp]->to_room;
                    num++;
                }
            }
    if (num == 0)
        return room;

    res = number(0, num - 1);

    return ex_rooms[res];
}

ASPELL(spell_blink)
{
    int room;
    int tmp;
    int fail;
    int dist;
    int was_in = 0;

    if (!caster)
        return;

    if (!victim)
        victim = caster;

    was_in = victim->in_room;
    room = victim->in_room;
    fail = 0;

    if (GET_SPEC(caster) == PLRSPEC_TELE)
        dist = 5;
    else
        dist = 3;

    for (tmp = 0; tmp < dist; tmp++) {
        if (room == NOWHERE) {
            fail = 1;
            break;
        }
        room = random_exit(room);
    }
    if (room == NOWHERE)
        fail = 1;

    if (IS_SET(world[room].room_flags, NO_TELEPORT)) {
        // Oops, this is a NO_TELEPORT room, we fail.
        fail = 1;
    }

    if (fail) {
        send_to_char("The world spins around, but nothing happens.\n\r", victim);
    } else {
        stop_riding(victim);
        if (caster != victim)
            send_to_char("You relocated your victim.\n\r", caster);
        if (different_zone(was_in, room)) {
            prohibit_item_stay_zone_move(caster, was_in);
        }
        send_to_char("The world spinned around you, and changed.\n\r", victim);
        act("$n disappeared in a flash of light.\n\r", TRUE, victim, 0, 0, TO_ROOM);
        char_from_room(victim);
        char_to_room(victim, room);
        do_look(victim, "", 0, 15, 0);
        extern void msdp_room_update(char_data * ch);

        msdp_room_update(victim);
        act("$n appeared in a flash of light.\n\r", TRUE, victim, 0, 0, TO_ROOM);
    }
}

bool is_teleportation_room_valid(room_data* room)
{
    // Don't teleport the caster into a room with people
    if (room->people)
        return false;

    // Don't kill the caster with a death room
    if (IS_SET(room->room_flags, DEATH))
        return false;

    // Don't let the caster go into rooms they shouldn't be in
    if (IS_SET(room->room_flags, SECURITYROOM))
        return false;

    if (IS_SET(room->room_flags, NO_TELEPORT))
        return false;

    if (IS_SET(room->room_flags, GODROOM))
        return false;

    // Everything looks good, let them go
    return true;
}

/*----------------------------------------------------------------------------------------------------------*/
ASPELL(spell_relocate)
{
    int zon_start, zon_target, zon_try, dist, was_in;
    int x, y, del_x, del_y, tmp, tmp2, num, tmpnum;
    struct room_data* room;
    struct affected_type af;

    if (caster->in_room == NOWHERE)
        return;

    zon_start = world[caster->in_room].zone;
    was_in = caster->in_room;
    x = zone_table[zon_start].x;
    y = zone_table[zon_start].y;
    del_x = del_y = 0;

    /* The base range of relocate is 4 zones */
    dist = 4;

    /*
     * If the player has Anger, remove 2 zones from the range,
     * otherwise if the player's specialization is teleportation,
     * add a zone.
     */
    if (affected_by_spell(caster, SPELL_ANGER))
        dist -= 2;
    else if (GET_SPEC(caster) == PLRSPEC_TELE)
        dist += 1;

    switch (digit) {
    case 0:
        del_y = -dist;
        break;

    case 1:
        del_x = dist;
        break;

    case 2:
        del_y = dist;
        break;

    case 3:
        if (GET_RACE(caster) == RACE_URUK && (x >= 8 && x <= 10)) {
            send_to_char("The light of Valar blinds your eyes, and "
                         "prevents you from going west.\n\r",
                caster);
            return;
        }
        del_x = -dist;
        break;

    case 4:
        send_to_char("There is but the sky up there.\n\r", caster);
        return;

    case 5:
        send_to_char("There is but the ground down there.\n\r", caster);
        return;

    default:
        send_to_char("Unrecognized direction, please report.\n\r", caster);
        return;
    }

    zon_target = -1;
    zon_try = 0;
    num = 0;
    while (zon_target < 0) {
        while ((zon_target < 0) && (del_x || del_y)) {
            for (; zon_try <= top_of_zone_table; zon_try++)
                if ((zone_table[zon_try].x == (x + del_x)) && (zone_table[zon_try].y == (y + del_y)))
                    break;

            if ((zon_try > top_of_zone_table) && (num == 0)) {
                del_x /= 2;
                del_y /= 2;
                zon_try = 0;
            } else
                zon_target = zon_try;
        }

        if ((zon_try >= top_of_zone_table) || !(del_x || del_y))
            break;

        for (tmp = 0; tmp <= top_of_world; tmp++) {
            room = &world[tmp];
            if ((room->zone == zon_target) && is_teleportation_room_valid(room)) {
                for (tmp2 = 0; tmp2 < NUM_OF_DIRS; tmp2++)
                    if (room->dir_option[tmp2])
                        break;
                if (tmp2 < NUM_OF_DIRS)
                    num++;
            }
        }
        if ((num == 0) || !(del_x || del_y)) {
            zon_target = -1;
            del_x /= 2;
            del_y /= 2;
        }
    }
    if ((zon_target < 0) || (num == 0)) {
        send_to_char("There is no suitable land in that direction.\n\r", caster);
        return;
    }
    num = number(1, num);

    zon_target = -1;
    zon_try = 0;
    tmpnum = 0;
    while (zon_target < 0) {
        while ((zon_target < 0) && (del_x || del_y)) {
            for (; zon_try <= top_of_zone_table; zon_try++)
                if ((zone_table[zon_try].x == (x + del_x)) && (zone_table[zon_try].y == (y + del_y)))
                    break;

            if (zon_try <= top_of_zone_table)
                zon_target = zon_try;
            else {
                send_to_char("Something went wrong in your spell.\n\r", caster);
                return;
            }
        }
        for (tmp = 0; tmp <= top_of_world; tmp++) {
            room = &world[tmp];
            if ((room->zone == zon_target) && is_teleportation_room_valid(room)) {
                for (tmp2 = 0; tmp2 < NUM_OF_DIRS; tmp2++)
                    if (room->dir_option[tmp2])
                        break;
                if (tmp2 < NUM_OF_DIRS)
                    tmpnum++;
                if (tmpnum == num)
                    break;
            }
        }
        if (tmpnum == 0)
            zon_target = -1;
        if (tmpnum == num)
            break;
    }

    if (tmp <= top_of_world) {
        if (different_zone(was_in, tmp)) {
            prohibit_item_stay_zone_move(caster, was_in);
        }
        stop_riding(caster);
        act("$n screams a shrill wail of pain, and suddenly disappears.\n\r",
            FALSE, caster, 0, 0, TO_ROOM);
        char_from_room(caster);
        char_to_room(caster, tmp);
        act("$n appears in an explosion of soft white light.\n\r",
            FALSE, caster, 0, 0, TO_ROOM);
        send_to_char("Pain fills your body, and your vision blurs. "
                     "You now stand elsewhere.\n\r",
            caster);
        extern void msdp_room_update(char_data * ch);

        msdp_room_update(caster);

        /* Apply confuse and haze */
        if (!affected_by_spell(caster, SPELL_CONFUSE)) {
            af.type = SPELL_CONFUSE;
            af.duration = 40; /* level 30 confuse */
            af.modifier = 1; /* modifier doesn't matter */
            af.location = APPLY_NONE;
            af.bitvector = AFF_CONFUSE;

            affect_to_char(caster, &af);
            act("Strange thoughts stream through your mind, making it "
                "hard to concentrate.",
                TRUE, caster, 0, 0, TO_CHAR);
            act("$n appears to be confused!", FALSE, caster, 0, 0, TO_ROOM);
        }

        if (!affected_by_spell(caster, SPELL_HAZE)) {
            af.type = SPELL_HAZE;
            af.duration = 1; /* 1 tick */
            af.modifier = 1; /* modifier doesn't matter */
            af.location = APPLY_NONE;
            af.bitvector = AFF_HAZE;

            affect_to_char(caster, &af);
            act("You feel dizzy as your surroundings seem to blur and twist.\n\r",
                TRUE, caster, 0, 0, TO_CHAR);
            act("$n staggers, overcome by dizziness!", FALSE, caster, 0, 0, TO_ROOM);
        }
        do_look(caster, "", 0, 0, 0);
    }
}

/*----------------------------------------------------------------------------------------------------------*/

ASPELL(spell_beacon)
{

    affected_type newaf;
    int mode;
    waiting_type* wtl = &caster->delay;
    mode = 0;

    if (wtl->targ2.type == TARGET_TEXT) {

        if (!str_cmp(wtl->targ2.ptr.text->text, "set"))
            mode = 1;

        else if (!str_cmp(wtl->targ2.ptr.text->text, "return"))
            mode = 2;

        else if (!str_cmp(wtl->targ2.ptr.text->text, "release"))
            mode = 3;

    } else if (wtl->targ2.type == TARGET_NONE) {
        mode = 2;
    }

    if (!mode) {
        send_to_char("You can only 'set' your beacon, 'return' to it or 'release' it.\n\r", caster);
        return;
    }

    if (mode == 1) { // Setting the beacone

        if (IS_SET(world[caster->in_room].room_flags, NO_TELEPORT)) {
            send_to_char("You cannot seem to set your beacon here.\n\r", caster);
            return;
        }

        if (affected_by_spell(caster, SPELL_BEACON)) {
            send_to_char("You reset your beacon here.\n\r", caster);
            affect_from_char(caster, SPELL_BEACON);
        } else {
            send_to_char("You set your beacon here.\n\r", caster);
        }

        int level = get_mage_caster_level(caster);
        newaf.type = SPELL_BEACON;
        newaf.duration = level;
        newaf.modifier = caster->in_room;
        newaf.location = 0;
        newaf.bitvector = 0;

        affect_to_char(caster, &newaf);

        return;
    }

    if (mode == 2) { // Returning..

        affected_type* oldaf;

        if (!(oldaf = affected_by_spell(caster, SPELL_BEACON))) {

            send_to_char("You have no beacons set.\n\r", caster);
            return;
        }

        if (oldaf->modifier > top_of_world) {

            send_to_char("Your beacon has been corruped, you cannot return to it.\n\r", caster);

            affect_from_char(caster, SPELL_BEACON);
            return;
        } else {
            room_data* to_room = &world[oldaf->modifier];
            zone_data* old_zone = &zone_table[world[caster->in_room].zone];
            zone_data* new_zone = &zone_table[to_room->zone];

            int distance = (old_zone->x - new_zone->x) * (old_zone->x - new_zone->x) + (old_zone->y - new_zone->y) * (old_zone->y - new_zone->y);

            if (distance > 25) {

                send_to_char("Your beacon is too far to be of use.\n\r", caster);
                return; // not removing the beacon.
            }

            act("$n disappears in bright spectral halo.", FALSE, caster, 0, 0, TO_ROOM);
            char_from_room(caster);
            char_to_room(caster, oldaf->modifier);
            act("$n appears in bright spectral halo.", FALSE, caster, 0, 0, TO_ROOM);

            send_to_char("You return to your beacon!\n\r", caster);
            do_look(caster, "", 0, 0, 0);

            affect_from_char(caster, SPELL_BEACON);
            return;
        }
    }
    if (mode == 3) {
        if (affected_by_spell(caster, SPELL_BEACON)) {
            send_to_char("You release your beacon.\n\r", caster);
            affect_from_char(caster, SPELL_BEACON);
        } else {
            send_to_char("You have no beacons to release.\n\r", caster);
        }
    }
}
/*---------------------------------------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------------------------------------*/
/*----------------------------------------------------------------------------------------------------------*/
/* Offensive spells listed in order off
 * - Magic Missile
 * - Chill Ray
 * - Lightning Bolt
 * - Dark Bolt
 * - Fire Bolt
 * - Cone of Cold
 * - Earthquake
 * - Lightning Strike
 * - Fireball
 */

// Gets the save bonus provided by character specialization.
// Higher numbers increase the targets chance of saving.
int get_save_bonus(const char_data& caster, const char_data& victim, game_types::player_specs primary_spec, game_types::player_specs opposing_spec)
{
    int save_bonus = 0;
    game_types::player_specs caster_spec = utils::get_specialization(caster);
    game_types::player_specs victim_spec = utils::get_specialization(victim);
    game_types::player_specs arcane_spec = game_types::player_specs::PS_Arcane;

    if (caster_spec == primary_spec || caster_spec == arcane_spec) {
        save_bonus -= 2;
    } else if (caster_spec == opposing_spec) {
        save_bonus += 2;
    }

    if (victim_spec == primary_spec) {
        save_bonus += 2;
    } else if (victim_spec == opposing_spec || victim_spec == arcane_spec) {
        save_bonus -= 2;
    }

    return save_bonus;
}

/*----------------------------------------------------------------------------------------------------------*/
/*
 * Spell Magic Missile
 * Our most basic Offensive spell
 * Does small amounts of phys damage
 */

ASPELL(spell_magic_missile)
{
    int mag_power = get_magic_power(caster);
    int dam = 12 + number(1, mag_power / 6);

    bool saved = new_saves_spell(caster, victim, 0);
    if (saved) {
        act("$N ignores most of the impact.", FALSE, caster, 0, victim, TO_CHAR);
        act("You ignore most of the impact.", FALSE, caster, 0, victim, TO_VICT);
        dam = dam >> 1;
    }

    apply_spell_damage(caster, victim, dam, SPELL_MAGIC_MISSILE, 0);
}

void apply_chilled_effect(char_data* caster, char_data* victim)
{
    int energy_lost = victim->specials.ENERGY / 2 + utils::get_energy_regen(*victim) * 4;
    victim->specials.ENERGY -= energy_lost;

    if (utils::get_specialization(*caster) == game_types::PS_Cold) {
        cold_spec_data* data = static_cast<cold_spec_data*>(caster->extra_specialization_data.current_spec_info);
        data->on_chill_applied(energy_lost);
    }
}

/*----------------------------------------------------------------------------------------------------------*/
/*
 * Spell Chill Ray
 * This spell is supposed to do medium ammounts of damage
 * but is currently our MOST effective spells vs everything
 * its "freezeing" affect means people can spam chill ray smobs
 * and never get hit. Plus i've seen it do 48 damage a cast
 * This spell needs to be addresses ASAP AND CHANGED
 * Khronos 27/03/05
 */
ASPELL(spell_chill_ray)
{
    int mag_power = get_magic_power(caster);
    int dam = 20 + number(1, mag_power) / 2;

    int save_bonus = get_save_bonus(*caster, *victim, game_types::PS_Cold, game_types::PS_Fire);

    // Cold spec makes chill ray much harder to resist.
    bool is_cold_spec = utils::get_specialization(*caster) == game_types::PS_Cold;
    if (is_cold_spec) {
        save_bonus -= 4;
    }

    bool saved = new_saves_spell(caster, victim, save_bonus);
    if (!saved) {
        apply_chilled_effect(caster, victim);
        if (is_cold_spec) {
            cold_spec_data* data = static_cast<cold_spec_data*>(caster->extra_specialization_data.current_spec_info);
            data->on_chill_ray_success(dam);
        }
    } else {
        act("$N shrugs off the cold, withstanding most of the chill.", FALSE, caster, 0, victim, TO_CHAR);
        act("You shrug off the cold, withstanding the brunt of the chill.", FALSE, caster, 0, victim, TO_VICT);
        dam >>= 1;
        if (is_cold_spec) {
            cold_spec_data* data = static_cast<cold_spec_data*>(caster->extra_specialization_data.current_spec_info);
            data->on_chill_ray_fail(dam);
        }
    }

    apply_spell_damage(caster, victim, dam, SPELL_CHILL_RAY, 0);
}

/*----------------------------------------------------------------------------------------------------------*/
/*
 * Spell Lightning bolt
 * Very effective spell if a little under used
 * Its fast and does reasonable damage, if we tweak chill ray
 * i can see this spell becomming more popular, also if lightning
 * spec was introduced this could become a prefered spell
 * On a side note if we do introduce spec lightning perhaps if a
 * player was speced in lightning there would be no lose of spell
 * affectiveness if they were indoors
 */

ASPELL(spell_lightning_bolt)
{
    int dam = 25 + number(0, get_magic_power(caster)) / 2;

    if (OUTSIDE(caster) || (utils::get_specialization(*caster) == game_types::PS_Lightning)) {
        dam += 4 + number(0, get_magic_power(caster)) / 4;
    } else {
        send_to_char("Your lightning is weaker inside, as you can not call on nature's full force here.\n\r", caster);
    }

    // Lightning spec deals +10% damage on lightning bolt.
    if (utils::get_specialization(*caster) == game_types::PS_Lightning) {
        dam += dam / 10;
    }

    int save_bonus = get_save_bonus(*caster, *victim, game_types::PS_Lightning, game_types::PS_Darkness);
    bool saved = new_saves_spell(caster, victim, save_bonus);
    if (saved) {
        act("$N dodges off to the side, avoiding part of the lightning!", FALSE, caster, 0, victim, TO_CHAR);
        act("You dodge to the side, avoiding part of the lightning!", FALSE, caster, 0, victim, TO_VICT);
        dam >>= 1;
    }

    apply_spell_damage(caster, victim, dam, SPELL_LIGHTNING_BOLT, 0);
}

/*----------------------------------------------------------------------------------------------------------*/
/*
 * Spell Dark bolt
 * Our Darkie races spell of choice
 * A good spell well fast to cast and relatively low on mana
 * It affected by sunlight which weakens its damage
 */

ASPELL(spell_dark_bolt)
{
    int dam = 25 + number(0, get_magic_power(caster)) / 2;
    if (!SUN_PENALTY(caster)) {
        dam += 4 + number(0, get_magic_power(caster)) / 4;
    } else {
        send_to_char("Your spell is weakened by the intensity of light.\n\r", caster);
    }

    // Dark spec deals an extra 10% damage with dark bolt.
    if (utils::get_specialization(*caster) == game_types::PS_Darkness) {
        dam += dam / 10;
    }

    int save_bonus = get_save_bonus(*caster, *victim, game_types::PS_Darkness, game_types::PS_Lightning);
    bool saved = new_saves_spell(caster, victim, save_bonus);
    if (saved) {
        act("$N seems unfazed by the darkness.", FALSE, caster, 0, victim, TO_CHAR);
        act("You are unfazed by the darkness.", FALSE, caster, 0, victim, TO_VICT);
        dam >>= 1;
    }

    apply_spell_damage(caster, victim, dam, SPELL_DARK_BOLT, 0);
}

/*----------------------------------------------------------------------------------------------------------*/
/*
 * spell Firebolt
 * powerful mid level spell for whities only
 * very random spell damage
 */

ASPELL(spell_firebolt)
{
    int dam = number(1, 65) + number(1, get_magic_power(caster)) / 4 + number(1, get_magic_power(caster)) / 4 + number(1, get_magic_power(caster)) / 8 + number(1, get_magic_power(caster)) / 8 + number(1, get_magic_power(caster)) / 16 + number(1, get_magic_power(caster)) / 16;

    // Fire spec mages get a minimum damage value on firebolt.
    if (utils::get_specialization(*caster) == game_types::PS_Fire) {
        dam = std::max(dam, get_mage_caster_level(caster));
    }

    int save_bonus = get_save_bonus(*caster, *victim, game_types::PS_Fire, game_types::PS_Cold);
    bool saved = new_saves_spell(caster, victim, save_bonus);
    if (saved) {
        act("$N dodges off to the side, avoiding part of the bolt!", FALSE, caster, 0, victim, TO_CHAR);
        act("You dodge to the side, avoiding part of the bolt!", FALSE, caster, 0, victim, TO_VICT);
        dam >>= 1;
    }

    apply_spell_damage(caster, victim, dam, SPELL_FIREBOLT, 0);
}

/*----------------------------------------------------------------------------------------------------------*/
/*
 * Spell cone of cold
 * Powerful upper end cold spell
 * not as popular as chill ray as it can't perma bash chars
 * Should we perhaps remove the directional stuff on this spell
 * Will we ever use it??
 * Khronos 27/03/05
 */

ASPELL(spell_cone_of_cold)
{
    int dam = 25 + number(1, get_magic_power(caster)) / 2 + get_magic_power(caster) / 4;

    if (victim) {
        bool is_cold_spec = utils::get_specialization(*caster) == game_types::PS_Cold;

        int save_bonus = get_save_bonus(*caster, *victim, game_types::PS_Cold, game_types::PS_Fire);
        bool saved = new_saves_spell(caster, victim, save_bonus);
        if (saved) {
            act("$N shrugs off the cold, withstanding most of the chill.", FALSE, caster, 0, victim, TO_CHAR);
            act("You shrug off the cold, withstanding the brunt of the chill.", FALSE, caster, 0, victim, TO_VICT);
            dam = dam * 2 / 3;
            if (is_cold_spec) {
                cold_spec_data* data = static_cast<cold_spec_data*>(caster->extra_specialization_data.current_spec_info);
                data->on_cone_of_cold_failed(dam);
            }
        } else if (is_cold_spec) {
            // Cold spec mages apply the chilled effect on Cone of Cold.
            apply_chilled_effect(caster, victim);
            cold_spec_data* data = static_cast<cold_spec_data*>(caster->extra_specialization_data.current_spec_info);
            data->on_cone_of_cold_success(dam);
        }

        apply_spell_damage(caster, victim, dam, SPELL_CONE_OF_COLD, 0);
        return;
    }

    /* Directional part */
    if ((digit < 0) || (digit >= NUM_OF_DIRS))
        return;

    if (!EXIT(caster, digit) || (EXIT(caster, digit)->to_room == NOWHERE)) {
        send_to_char("There is nothing in that direction.\n\r", caster);
        return;
    }

    char buf1[255], buf2[255];
    if (IS_SET(EXIT(caster, digit)->exit_info, EX_CLOSED)) {
        if (IS_SET(EXIT(caster, digit)->exit_info, EX_ISHIDDEN)) {
            send_to_char("There is nothing in that direction.\n\r", caster);
            return;
        } else {
            sprintf(buf1, "Your cone of cold hit %s, to no real effect.\n\r",
                EXIT(caster, digit)->keyword);
            send_to_char(buf1, caster);
            return;
        }
    }
    if (IS_SET(EXIT(caster, digit)->exit_info, EX_NO_LOOK)) {
        send_to_char("Your cone of cold faded before reaching its' target.\n\r",
            caster);
        return;
    }

    sprintf(buf1, "You send a cone of cold to %s.\n\r", refer_dirs[digit]);
    send_to_char(buf1, caster);

    sprintf(buf1, "A sudden wave of cold strikes you, coming from %s.\n\r",
        refer_dirs[rev_dir[digit]]);
    sprintf(buf2, "You feel a sudden wave of cold coming from %s.\n\r",
        refer_dirs[rev_dir[digit]]);

    struct char_data* tmpch;
    for (tmpch = world[EXIT(caster, digit)->to_room].people;
         tmpch;
         tmpch = tmpch->next_in_room) {
        int save_bonus = get_save_bonus(*caster, *tmpch, game_types::PS_Fire, game_types::PS_Cold);
        bool saved = new_saves_spell(caster, tmpch, save_bonus);
        if (saved) {
            int tmp = GET_HIT(tmpch);
            if (tmp < dam / 2)
                tmp = 1;
            else
                tmp = tmp - dam / 2;

            GET_HIT(tmpch) = tmp;
            send_to_char(buf1, tmpch);
        } else {
            send_to_char(buf2, tmpch);
        }
    }
}

/*----------------------------------------------------------------------------------------------------------*/
/*
 * Spell earthquake
 * Powerful spell that deals damage to every character in the room
 * has the ability to demolish Orc's quite quickly, perhaps orcs should
 * have a specific save vs the spell
 * Khronos 27/03/05
 */

ASPELL(spell_earthquake)
{
    int dam_value, crack_chance, tmp;
    struct char_data *tmpch, *tmpch_next;
    room_data* cur_room;
    int crack;

    if (!caster)
        return;
    if (caster->in_room == NOWHERE)
        return;
    cur_room = &world[caster->in_room];

    int level = get_mage_caster_level(caster);
    crack_chance = 1;

    if ((cur_room->sector_type == SECT_CITY) || (cur_room->sector_type == SECT_CRACK) || (cur_room->sector_type == SECT_WATER_SWIM) || (cur_room->sector_type == SECT_WATER_NOSWIM) || (cur_room->sector_type == SECT_UNDERWATER))
        crack_chance = 0;

    if (IS_SET(cur_room->room_flags, INDOORS))
        crack_chance = 0;
    if (cur_room->dir_option[DOWN] && IS_SET(cur_room->dir_option[DOWN]->exit_info, EX_CLOSED | EX_DOORISHEAVY))
        crack_chance = 0;

    if (cur_room->dir_option[DOWN] && !cur_room->dir_option[DOWN]->exit_info)
        crack_chance = 1;
    else if (number(0, 100) > level)
        crack_chance = 0;

    /* Here goes normal earthquake */

    dam_value = number(1, 30) + level;

    if (crack_chance)
        dam_value /= 2;

    for (tmpch = world[caster->in_room].people; tmpch; tmpch = tmpch_next) {
        tmpch_next = tmpch->next_in_room;
        if (tmpch != caster) {
            bool saved = new_saves_spell(caster, tmpch, 0);
            if (saved) {
                act("$N withstands the vibrating earth.", FALSE, caster, 0, tmpch, TO_CHAR);
                act("You withstand the tremors shaking your body.", FALSE, caster, 0, tmpch, TO_VICT);
                apply_spell_damage(caster, tmpch, dam_value / 2, SPELL_EARTHQUAKE, 0);
            } else {
                apply_spell_damage(caster, tmpch, dam_value, SPELL_EARTHQUAKE, 0);
            }
        }
        //  return;
    }

    if (crack_chance) {
        if (cur_room->dir_option[DOWN] && (cur_room->dir_option[DOWN]->to_room != NOWHERE)) {
            crack = cur_room->dir_option[DOWN]->to_room;
            if (crack != NOWHERE) {
                if (cur_room->dir_option[DOWN]->exit_info) {
                    act("The way down crashes open!", FALSE, caster, 0, 0, TO_ROOM);
                    send_to_char("The way down crashes open!\n\r", caster);
                    cur_room->dir_option[DOWN]->exit_info = 0;
                    if (world[crack].dir_option[UP] && (world[crack].dir_option[UP]->to_room == caster->in_room) && world[crack].dir_option[UP]->exit_info) {
                        tmp = caster->in_room;
                        caster->in_room = crack;
                        act("The way up crashes open!", FALSE, caster, 0, 0, TO_ROOM);
                        world[crack].dir_option[UP]->exit_info = 0;
                        caster->in_room = tmp;
                    }
                }
            }
        }
        /* no room there, so create one */
        else {
            crack = world.create_room(world[caster->in_room].zone);
            world[caster->in_room].create_exit(DOWN, crack);

            RELEASE(world[crack].name);
            world[crack].name = str_dup("Deep Crevice");
            RELEASE(world[crack].description);
            world[crack].description = str_dup(
                "   The crevice is deep, dark and looks unsafe. The walls of fresh broken\n\r"
                "rock are uneven and still crumbling. Some powerful disaster must have \n\r"
                "torn the ground here recently.\n\r");
            world[crack].sector_type = SECT_CRACK;
            world[crack].room_flags = cur_room->room_flags;
            act("The ground is cracked under your feet!", FALSE, caster, 0, 0, TO_ROOM);
            send_to_char("Your spell cracks the ground open!\n\r", caster);
        }

        /* deal out the damage */
        for (tmpch = cur_room->people; tmpch; tmpch = tmpch_next) {
            bool saved = new_saves_spell(caster, tmpch, tmpch->tmpabilities.dex / 4);
            tmpch_next = tmpch->next_in_room;
            if (!saved && (tmpch != caster) || (!number(0, 1))) {
                act("$n loses balance and falls down!", TRUE, tmpch, 0, 0, TO_ROOM);
                send_to_char("The earthquake throws you down!\n\r", tmpch);
                stop_riding(tmpch);
                char_from_room(tmpch);
                char_to_room(tmpch, crack);
                act("$n falls in.", TRUE, tmpch, 0, 0, TO_ROOM);
                tmpch->specials.position = POSITION_SITTING;

                if (new_saves_spell(caster, tmpch, 0)) {
                    act("$N manages to land on his feet!", FALSE, caster, 0, tmpch, TO_CHAR);
                    act("You manage to land on your feet!", FALSE, caster, 0, tmpch, TO_VICT);
                    apply_spell_damage(caster, tmpch, dam_value, SPELL_EARTHQUAKE, 0);
                } else {
                    apply_spell_damage(caster, tmpch, dam_value * 2, SPELL_EARTHQUAKE, 0);
                }
            }
        }
    }
}

/*----------------------------------------------------------------------------------------------------------*/
/*
 * Spell lightning strike
 * Very powerful spell capable of doing huge damage
 * Only works when the weather is stormy
 * Perhaps in future if we imp a lighhning spec a character
 * speced in lightning won't have to wait on weather to cast
 * this spell (but have the damage reduced slightly)
 * Khronos 27/03/05
 */

ASPELL(spell_lightning_strike)
{
    int dam = 40 + number(0, get_magic_power(caster)) + number(0, get_magic_power(caster)) / 2;

    if (!OUTSIDE(caster)) {
        send_to_char("You can not call lightning inside!\n\r", caster);
        return;
    }

    if (weather_info.sky[world[caster->in_room].sector_type] != SKY_LIGHTNING) {
        if (utils::get_specialization(*caster) == (int)game_types::PS_Lightning) {
            send_to_char("You manage to create some lightning, but the effect is reduced.\n\r", caster);
            dam = dam * 4 / 5;
        } else {
            send_to_char("The weather is not appropriate for this spell.\n\r", caster);
            return;
        }
    }

    int save_bonus = get_save_bonus(*caster, *victim, game_types::PS_Lightning, game_types::PS_Darkness);
    bool saved = new_saves_spell(caster, victim, save_bonus);
    if (saved) {
        act("$N dodges off to the side, avoiding part of the lightning!", FALSE, caster, 0, victim, TO_CHAR);
        act("You dodge to the side, avoiding part of the lightning!", FALSE, caster, 0, victim, TO_VICT);
        dam = dam * 2 / 3;
    }

    apply_spell_damage(caster, victim, dam, SPELL_LIGHTNING_STRIKE, 0);
}

/*----------------------------------------------------------------------------------------------------------*/
/*
 * Spell searing darkness
 * Very powerful spell capable of doing huge damage
 * Uruk-Hai only spell that does fire and darkness as damage.
 * Maga 11/21/2016
 */

ASPELL(spell_searing_darkness)
{
    game_types::player_specs caster_spec = utils::get_specialization(*caster);

    int darkness_damage = 15 + number(0, get_magic_power(caster)) / 2;

    if (!SUN_PENALTY(caster)) {
        darkness_damage += 5 + number(0, get_magic_power(caster) / 4);
    } else {
        send_to_char("Your spell is weakened by the intensity of light.\n\r", caster);
    }

    // Dark spec adds an additional 10% darkness damage.
    if (caster_spec == game_types::PS_Darkness) {
        darkness_damage += darkness_damage / 10;
    }

    int fire_damage = 15 + number(0, get_magic_power(caster)) / 2;

    // Fire spec adds an additional 50% fire damage.
    if (caster_spec == game_types::PS_Fire) {
        fire_damage += fire_damage / 2;
    }

    int save_bonus = get_save_bonus(*caster, *victim, game_types::PS_Fire, game_types::PS_Cold);
    bool saves_fire = new_saves_spell(caster, victim, save_bonus);
    if (saves_fire) {
        act("$N avoids most of the fire, but is still consumed by the darkness.", FALSE, caster, 0, victim, TO_CHAR);
        act("You avoid most of the fire, but the darkness consumes you.", FALSE, caster, 0, victim, TO_VICT);
        fire_damage = fire_damage * 1 / 3;
    }

    int damage_dealt = fire_damage + darkness_damage;

    apply_spell_damage(caster, victim, damage_dealt, SPELL_SEARING_DARKNESS, 0);
}

/*----------------------------------------------------------------------------------------------------------*/
/*
 * Spell Fireball
 * OUr high end damage spell
 * does big damage at the risk of hitting others in the room
 */

bool is_friendly_taget(const char_data* caster, const char_data* victim)
{
    if (victim == caster)
        return true;

    if (victim->master)
        return is_friendly_taget(caster, victim->master);

    return !other_side(caster, victim);
}

ASPELL(spell_fireball)
{
    if (caster->in_room == NOWHERE)
        return;

    int fireball_damage = 30 + number(1, get_magic_power(caster)) / 2 + number(1, get_magic_power(caster)) / 2 + number(1, get_magic_power(caster)) / 2;

    if (RACE_SOME_ORC(caster))
        fireball_damage -= 5;
    if (RACE_SOME_ORC(caster) && !number(0, 9)) {
        send_to_char("You fail to control the fire you invoke!\n\r", caster);
        victim = caster;
        fireball_damage = fireball_damage / 3;
    }

    bool is_fire_spec = utils::get_specialization(*caster) == game_types::PS_Fire;

    int save_bonus = get_save_bonus(*caster, *victim, game_types::PS_Fire, game_types::PS_Cold);
    bool saved = new_saves_spell(caster, victim, save_bonus);
    if (saved) {
        act("$N dodges off to the side, avoiding part of the blast!", FALSE, caster, 0, victim, TO_CHAR);
        act("You dodge to the side, avoiding part of the blast!", FALSE, caster, 0, victim, TO_VICT);
        apply_spell_damage(caster, victim, fireball_damage * 2 / 3, SPELL_FIREBALL, 0);
    } else {
        apply_spell_damage(caster, victim, fireball_damage, SPELL_FIREBALL, 0);
    }

    char_data* next_character = nullptr;
    for (char_data* potential_victim = world[caster->in_room].people; potential_victim; potential_victim = next_character) {
        next_character = potential_victim->next_in_room;
        if (potential_victim == caster || potential_victim == victim)
            continue;

        /* Fire specialization mages won't hit friendly targets. */
        if (is_fire_spec && is_friendly_taget(caster, victim))
            continue;

        double random_roll = number();
        // target_number and damage are higher if the victim is fighting with the caster
        double target_number = 0.2;
        int damage_divisor = 5;
        if (potential_victim->specials.fighting == caster || caster->specials.fighting == potential_victim) {
            target_number = 0.8;
            damage_divisor = 3;
        }

        if (random_roll <= target_number) {
            int splash_damage = fireball_damage / damage_divisor;
            int splash_save_bonus = get_save_bonus(*caster, *potential_victim, game_types::PS_Fire, game_types::PS_Cold);
            bool splash_saved = new_saves_spell(caster, potential_victim, splash_save_bonus);
            if (splash_saved) {
                act("$N dodges off to the side, avoiding part of the blast!", FALSE, caster, 0, potential_victim, TO_CHAR);
                act("You dodge to the side, avoiding part of the blast!", FALSE, caster, 0, potential_victim, TO_VICT);
                splash_damage = splash_damage >> 1;
            }

            apply_spell_damage(caster, potential_victim, splash_damage, SPELL_FIREBALL2, 0);
        }
    }
}

/*----------------------------------------------------------------------------------------------------------*/
/*----------------------------------------------------------------------------------------------------------*/
/* Uruk Lhuth spells in order of
 * - Word of Pain
 * - Leach
 * - Word of Sight
 * - Word of Shock
 * - Black Arrow
 * - Word of Agony
 * - Shout of Pain
 * - Spear of Darkness
 */

/*----------------------------------------------------------------------------------------------------------*/

/*
 * `word of pain' is analogous to magic missile
 */
ASPELL(spell_word_of_pain)
{
    int dam = 12 + number(1, get_magic_power(caster) / 6);

    bool saved = new_saves_spell(caster, victim, 0);
    if (saved) {
        act("$N ignores some of your phantom words.", FALSE, caster, 0, victim, TO_CHAR);
        act("You realize, almost too late, that the words are false.", FALSE, caster, 0, victim, TO_VICT);
        dam = dam >> 1;
    }

    apply_spell_damage(caster, victim, dam, SPELL_WORD_OF_PAIN, 0);
}

/*----------------------------------------------------------------------------------------------------------*/
/*
 * leach: replaces chill ray; a bit less damage, but half of
 * the damage done goes to the caster.  There is also a small
 * drain of random (albeit small) number of moves.
 */
ASPELL(spell_leach)
{
    int mag_power = get_magic_power(caster);
    int dam = 18 + number(1, mag_power / 4);

    bool saved = new_saves_spell(caster, victim, 0);
    if (saved) {
        act("$N fights off the leeching energy.", FALSE, caster, 0, victim, TO_CHAR);
        act("You fight off the leeching energy.", FALSE, caster, 0, victim, TO_VICT);
        dam >>= 1;
    } else {
        int moves = std::min((int)GET_MOVE(victim), number(0, 5));
        GET_MOVE(victim) += -moves;
        GET_MOVE(caster) = std::min((int)GET_MAX_MOVE(caster), GET_MOVE(caster) + moves);
        GET_HIT(caster) = std::min(GET_MAX_HIT(caster), GET_HIT(caster) + dam / 2);
        send_to_char("Your life's ichor is drained!\n\r", victim);
    }

    apply_spell_damage(caster, victim, dam, SPELL_LEACH, 0);
}

/*----------------------------------------------------------------------------------------------------------*/

/*
 * For a detailed discussion on the workings of reveal-
 * like spells, see the comment heading the reveal_life
 * spell.  For brevity, we leave all other reveal spells
 * completely undiscussed within their respective
 * function definitions.
 */
ASPELL(spell_word_of_sight)
{
    struct char_data* hider;
    int found, hider_bonus;

    if (caster->in_room == NOWHERE)
        return;

    act("A presence seeks the area, searching for souls.", TRUE, caster, 0, 0, TO_ROOM);
    send_to_char("Your mind probes the area seeking other souls.\n\r",
        caster);

    int level = get_mage_caster_level(caster);

    for (hider = world[caster->in_room].people, found = 0; hider;
         hider = hider->next_in_room) {
        if (hider != caster) {
            if (GET_HIDING(hider) > 0) {
                hider_bonus = GET_HIDING(hider) / 30;
                hider_bonus += number(0, GET_HIDING(hider) % 30) ? 1 : 0;
                hider_bonus -= (30 - level) / 3;
                if (!new_saves_spell(caster, hider, hider_bonus)) {
                    send_to_char("You've been discovered!\r\n", hider);
                    stop_hiding(hider, FALSE);
                    found = 1;
                } else if (!number(0, GET_HIDING(hider) / 40)) {
                    send_to_char("The presence of the soul grows intense, and you "
                                 "feel less confident of your cover.\r\n",
                        hider);
                    GET_HIDING(hider) -= GET_PROF_LEVEL(PROF_MAGE, caster) / 3;
                } else
                    send_to_char("The power of the soul recesses, and you feel "
                                 "your cover is uncompromised.\r\n",
                        hider);
            } else
                found = 1;
        }
    }

    if (!found)
        send_to_char("The place seems empty.\n\r", caster);
    else
        list_char_to_char(world[caster->in_room].people, caster, 0);
}

/*----------------------------------------------------------------------------------------------------------*/
/*
 * word of shock is analogous to flash
 */
ASPELL(spell_word_of_shock)
{
    struct char_data* tmpch;

    if (caster->in_room < 0)
        return;

    int caster_level = get_mage_caster_level(caster);

    for (tmpch = world[caster->in_room].people; tmpch; tmpch = tmpch->next_in_room) {
        if (tmpch != caster)
            send_to_char("An assault on your mind leaves you reeling.\n\r", tmpch);
        if (tmpch->specials.fighting)
            GET_ENERGY(tmpch) -= 400;
        if (tmpch->specials.fighting == caster)
            GET_ENERGY(tmpch) -= 400;
        if (!saves_spell(tmpch, caster_level, 0)) {
            if (tmpch->specials.fighting)
                stop_fighting(tmpch);
            if ((tmpch != caster) && (tmpch->delay.wait_value > 0) && (GET_WAIT_PRIORITY(tmpch) < 40))
                break_spell(tmpch);
            if (tmpch->specials.fighting)
                GET_ENERGY(tmpch) -= 400;
        }
    }
    send_to_char("You utter a word of power.\n\r", caster);
}

/*----------------------------------------------------------------------------------------------------------*/

/*
 * black arrow replaces firebolt; though it deals a smaller
 * amount of damage, it has a chance to poison its victim.
 */
ASPELL(spell_black_arrow)
{
    int dam = 13 + number(1, get_magic_power(caster)) / 2 + number(1, get_magic_power(caster)) / 2;

    if (!SUN_PENALTY(caster) || utils::get_specialization(*caster) == game_types::PS_Darkness) {
        dam += number(0, get_magic_power(caster) / 6) + 2;
    } else {
        send_to_char("Your spell is weakened by the intensity of light.\n\r", caster);
    }

    const int level = get_mage_caster_level(caster);
    const int min_poison_dam = 5;

    int save_bonus = get_save_bonus(*caster, *victim, game_types::PS_Darkness, game_types::PS_Lightning);
    bool saved = new_saves_spell(caster, victim, save_bonus);
    if (saved) {
        act("$N seems to resist the effects of your black arrow.", FALSE, caster, 0, victim, TO_CHAR);
        act("You resist the dark energies of the black arrow.", FALSE, caster, 0, victim, TO_VICT);
        dam >>= 1;
    } else if (number(1, 50) < level && GET_HIT(victim) > min_poison_dam) {
        // TODO(drelidan):  Should this conditional poison apply after damage is applied?
        affected_type af;
        af.type = SPELL_POISON;
        af.duration = level + 1;
        af.modifier = -2;
        af.location = APPLY_STR;
        af.bitvector = AFF_POISON;
        affect_join(victim, &af, FALSE, FALSE);

        send_to_char("The vile magic poisons you!\n\r", victim);
        apply_spell_damage(caster, victim, min_poison_dam, SPELL_POISON, 0);
    }

    apply_spell_damage(caster, victim, dam, SPELL_BLACK_ARROW, 0);
}

/*----------------------------------------------------------------------------------------------------------*/
/*
 * word_of_agony is analogous to cone, and has a chill
 * ray type effect on energy
 */
ASPELL(spell_word_of_agony)
{
    int dam = 20 + number(1, get_magic_power(caster)) / 2 + number(1, get_magic_power(caster)) / 2;

    bool saved = new_saves_spell(caster, victim, -2);
    if (saved) {
        act("$N seems to resist some of the agony.", FALSE, caster, 0, victim, TO_CHAR);
        act("Your mind resists some of the agony.", FALSE, caster, 0, victim, TO_VICT);
        apply_spell_damage(caster, victim, dam * 2 / 3, SPELL_WORD_OF_AGONY, 0);
    } else {
        apply_chilled_effect(caster, victim);
        apply_spell_damage(caster, victim, dam, SPELL_WORD_OF_AGONY, 0);
    }
}

/*----------------------------------------------------------------------------------------------------------*/
/*
 * shout of pain is analogous to earthquke, obviously
 * leaving no crack
 */
ASPELL(spell_shout_of_pain)
{
    if (!caster || caster->in_room == NOWHERE)
        return;

    int dam_value = number(1, 50) + get_magic_power(caster) / 2;

    char_data* tmpch_next = NULL;
    for (char_data* tmpch = world[caster->in_room].people; tmpch; tmpch = tmpch_next) {
        tmpch_next = tmpch->next_in_room;
        if (tmpch != caster) {
            bool saved = new_saves_spell(caster, tmpch, 0);
            if (saved) {
                act("$N seems to resist some of the agony.", FALSE, caster, 0, tmpch, TO_CHAR);
                act("Your mind resists some of the agony.", FALSE, caster, 0, tmpch, TO_VICT);
                apply_spell_damage(caster, tmpch, dam_value / 2, SPELL_SHOUT_OF_PAIN, 0);
            } else {
                apply_spell_damage(caster, tmpch, dam_value, SPELL_SHOUT_OF_PAIN, 0);
            }
        }
    }
}

/*----------------------------------------------------------------------------------------------------------*/

/*
 * spear of darkness; damage like fireball, damage malused
 * in sun in the style of dark bolt.  Additionally, spear
 * is not saveable.
 */
ASPELL(spell_spear_of_darkness)
{
    int dam = 0;
    if (SUN_PENALTY(caster)) {
        send_to_char("Your spell is weakened by the intensity of light.\n\r", caster);
    } else {
        dam = 30 + number(8, get_magic_power(caster)) / 2;
    }
    dam += number(8, get_magic_power(caster)) / 2 + number(8, get_magic_power(caster)) / 2 + number(0, get_magic_power(caster)) / 5;

    // Dark spec deals an extra 5% damage with spear of darkness.
    if (utils::get_specialization(*caster) == game_types::PS_Darkness) {
        dam += dam / 20;
    }

    // Just run through new_saves_spell for logging.
    bool saved = new_saves_spell(caster, victim, -20);
    damage(caster, victim, dam, SPELL_SPEAR_OF_DARKNESS, 0);
}

/*----------------------------------------------------------------------------------------------------------*/
/*----------------------------------------------------------------------------------------------------------*/
/* Spec Specific spells list below in order of
 * - Blaze
 * - Freeze
 * - Mist of bazunga
 */
/*----------------------------------------------------------------------------------------------------------*/

/*
 * Spell blaze
 * powerful spell that sets the room the spell is casted in
 * on fire, literally.
 * Are we going to imp this every?
 * Khronos 27/03/05
 */

ASPELL(spell_blaze)
{
    struct affected_type af;
    struct affected_type* oldaf;
    struct char_data* tmpch;
    struct char_data* tmpch_next;
    int dam;

    int level = get_mage_caster_level(caster);
    bool is_fire_spec = utils::get_specialization(*caster) == game_types::PS_Fire;

    if (!victim && !obj) { /* there was no target, hit the room */
        if (!caster)
            return;

        act("$n breathes out a cloud of fire!", TRUE, caster, 0, 0, TO_ROOM);
        send_to_char("You breathe out fire.\n\r", caster);

        /* Damage everyone in the room */
        for (tmpch = world[caster->in_room].people; tmpch; tmpch = tmpch_next) {
            tmpch_next = tmpch->next_in_room;

            // friends don't burn friends, at first...
            if (is_friendly_taget(caster, tmpch)) {
                continue;
            }
            dam = number(1, 30) + get_magic_power(caster) / 2; /* same as earthquake */

            int save_bonus = get_save_bonus(*caster, *tmpch, game_types::PS_Fire, game_types::PS_Cold);
            bool saved = new_saves_spell(caster, tmpch, save_bonus + tmpch == caster ? 3 : 0);

            if (saved) {
                dam = dam >> 1;
            }

            apply_spell_damage(caster ? caster : tmpch, tmpch, dam, SPELL_BLAZE, 0);
        }

        /*
         * Add the new affection.  Keep in mind that the af.modifier is the
         * mage level that will be used to calculate damage in subsequent
         * calls.
         */
        af.type = ROOMAFF_SPELL;
        af.duration = level;
        af.modifier = level;
        af.location = SPELL_BLAZE;
        af.bitvector = 0;
        if ((oldaf = room_affected_by_spell(&world[caster->in_room], SPELL_BLAZE))) {
            if (oldaf->duration < af.duration)
                oldaf->duration = af.duration;
            if (oldaf->modifier < af.modifier)
                oldaf->modifier = af.modifier;
        } else
            affect_to_room(&world[caster->in_room], &af);

        act("The area suddenly bursts into a roaring firestorm!",
            FALSE, caster, 0, 0, TO_ROOM);
        send_to_char("The area suddenly bursts into a roaring firestorm!\n\r", caster);
    }
    /*
     * We were called by room_affect_update or there was an actual
     * victim specified
     */
    else if (victim) {
        int save_bonus = get_save_bonus(*caster, *victim, game_types::PS_Fire, game_types::PS_Cold);
        bool saved = new_saves_spell(caster, victim, save_bonus);

        dam = number(8, level) + 10;
        if (saved) {
            dam >>= 1;
        }

        if (caster != victim) {
            act("$n breathes fire on you!", TRUE, caster, 0, victim, TO_VICT);
        }
        apply_spell_damage(caster ? caster : victim, victim, dam, SPELL_BLAZE, 0);
    }
}

/*----------------------------------------------------------------------------------------------------------*/

/*
 * Bear in mind that `targ2' is the target_info structure that
 * contains our door.  Also realize that we don't really do any
 * exit validation at this point in the game; that's all been
 * done in (first) interpre.cc when the command was originally
 * entered, and then done again in spell_pa.cc's do_cast.
 *
 * TODO: now we just need to make a new 'is frozen' affection
 * for exits, add behavior all over the code for that flag
 * (mainly in stuff like do_open, do_close, maybe pick, and, of
 * course, the shaping interface [including manuals]).  We also
 * need a flag like NO_FREEZE for exits so that builders can
 * designate certain doors as unfreezable.
 */

/*
 *I think personally this spell is a bad idea,
 * i envisage alot of trap kills from it
 * Khronos 27/03/05
 */

ASPELL(spell_freeze)

{
    if (IS_SET(EXIT(caster, digit)->exit_info, EX_CLOSED)) {
        /* Did they use "east", "west", etc. to target this direction? */
        if (caster->delay.targ2.choice == TAR_DIR_WAY)
            sprintf(buf, "A thick sheet of hardened frost forms %s%s%s.\r\n",
                digit != UP && digit != DOWN ? "to " : "", refer_dirs[digit],
                digit != UP && digit != DOWN ? "" : " you");
        /* Or did they actually know the name of the door? */
        else if (caster->delay.targ2.choice == TAR_DIR_NAME)
            sprintf(buf, "The %s is frozen shut.\r\n", EXIT(caster, digit)->keyword);
        send_to_room(buf, caster->in_room);
        return;
    } else
        send_to_char("You must first have a closed exit to cast upon.\r\n", caster);
}

/*----------------------------------------------------------------------------------------------------------*/

/*
 * uruk mage spell: mist of baazunga
 * The mist of baazunga causes the room it is casted in to
 * be affected become dark for a short period of time.
 * Additionally, every room directly connected to the misted
 * room is affected.  The only other place where the mists
 * are dealt with is in affect_update_room in limits.cc:
 * that code maintains the mist, and causes it to float
 * about from room to room.
 *
 * TODO: mist should actually cause rooms to be dark.  We
 * should also generalize the affection "branching" code so
 * that we can also have things like blaze branch from room
 * to room.
 */
ASPELL(spell_mist_of_baazunga)
{
    struct affected_type af, af2;
    struct affected_type* oldaf;
    struct room_data* room;
    int modifier, direction, mod, roomnum;

    if (!caster)
        return;

    room = &world[caster->in_room];
    if ((oldaf = room_affected_by_spell(room, SPELL_MIST_OF_BAAZUNGA)))
        modifier = oldaf->modifier;
    else {
        if (IS_SET(room->room_flags, SHADOWY))
            modifier = 1;
        else
            modifier = 0;
    }

    int level = get_mage_caster_level(caster);
    af.type = ROOMAFF_SPELL;
    af.duration = level / 5;
    af.modifier = modifier;
    af.location = SPELL_MIST_OF_BAAZUNGA;
    af.bitvector = 0;

    /* Apply the full spell to main room */
    if ((oldaf = room_affected_by_spell(&world[caster->in_room],
             SPELL_MIST_OF_BAAZUNGA))) {
        if (oldaf->duration < af.duration)
            oldaf->duration = af.duration;
        /*
         * This has been commented out for a pretty long time;
         * why exactly don't we want to output a message if the
         * caster is renewing the mists?
         *
         * act("$n breathes out dark mists.", TRUE, caster, 0, 0, TO_ROOM);
         * send_to_char("You breathe out dark mists.\n\r", caster);
         */
    } else {
        affect_to_room(&world[caster->in_room], &af);
        act("$n breathes out dark mists.", TRUE, caster, 0, 0, TO_ROOM);
        send_to_char("You breathe out dark mists.\n\r", caster);
    }

    /* Apply a smaller spell to the joined rooms */
    for (direction = 0; direction < NUM_OF_DIRS; direction++) {
        if (room->dir_option[direction]) {
            if (room->dir_option[direction]->to_room != NOWHERE) {
                roomnum = room->dir_option[direction]->to_room;

                if ((oldaf = room_affected_by_spell(&world[roomnum],
                         SPELL_MIST_OF_BAAZUNGA)))
                    mod = oldaf->modifier;
                else if (IS_SET(world[roomnum].room_flags, SHADOWY))
                    mod = 1;
                else
                    mod = 0;

                af2.type = ROOMAFF_SPELL;
                af2.duration = level / 6;
                af2.modifier = mod;
                af2.location = SPELL_MIST_OF_BAAZUNGA;
                af2.bitvector = 0;

                if ((oldaf = room_affected_by_spell(&world[roomnum],
                         SPELL_MIST_OF_BAAZUNGA))) {
                    if (oldaf->duration < af.duration)
                        oldaf->duration = af.duration;
                } else
                    affect_to_room(&world[roomnum], &af2);
            }
        }
    }

    return;
}

const char* get_expose_spell_message(int spell_id)
{
    switch (spell_id) {
    case SPELL_SPEAR_OF_DARKNESS:
        return "Your target is now exposed to 'Spear of Darkness'.\r\n";
        break;
    case SPELL_SEARING_DARKNESS:
        return "Your target is now exposed to 'Searing Darkness'.\r\n";
        break;
    case SPELL_DARK_BOLT:
        return "Your target is now exposed to 'Dark Bolt'.\r\n";
        break;
    case SPELL_FIREBALL:
        return "Your target is now exposed to 'Fireball'.\r\n";
        break;
    case SPELL_FIREBOLT:
        return "Your target is now exposed to 'Firebolt'.\r\n";
        break;
    case SPELL_CONE_OF_COLD:
        return "Your target is now exposed to 'Cone of Cold'.\r\n";
        break;
    case SPELL_CHILL_RAY:
        return "Your target is now exposed to 'Chill Ray'.\r\n";
        break;
    case SPELL_LIGHTNING_STRIKE:
        return "Your target is now exposed to 'Lightning Strike'.\r\n";
        break;
    case SPELL_LIGHTNING_BOLT:
        return "Your target is now exposed to 'Lightning Bolt'.\r\n";
        break;
    default:
        return "Unknown spell.\r\n";
    }
}

void spell_expose_elements(char_data* caster, char* arg, int type, char_data* victim, obj_data* obj, int digit, int is_object)
{
    const int max_valid_specs = 6;
    if (!victim || !caster)
        return;

    if (utils::is_pc(*victim)) {
        send_to_char("This spell cannot target players.\r\n", caster);
        return;
    }

    game_types::player_specs valid_specs[max_valid_specs] = { game_types::PS_Arcane,
        game_types::PS_Cold, game_types::PS_Darkness, game_types::PS_Fire,
        game_types::PS_Lightning, game_types::PS_BattleMage };

    bool valid_spec = false;
    game_types::player_specs caster_spec = utils::get_specialization(*caster);
    for (int i = 0; i < max_valid_specs; ++i) {
        if (caster_spec == valid_specs[i]) {
            valid_spec = true;
        }
    }

    if (!valid_spec) {
        send_to_char("You are not of an appropriate specialization to cast this spell.", caster);
        return;
    }

    // Player is of the appropriate spec - cast the data.
    elemental_spec_data* spec_data = caster->extra_specialization_data.get_mage_spec();
    spec_data->exposed_target = victim;

    const room_data& current_room = world[caster->in_room];
    int weather_type = weather_info.sky[current_room.sector_type];

    switch (caster_spec) {
    case game_types::PS_BattleMage: {
        if (utils::is_evil_race(*caster)) {
            spec_data->spell_id = SPELL_DARK_BOLT;
        } else {
            spec_data->spell_id = SPELL_FIREBOLT;
        }
    } break;
    case game_types::PS_Arcane: {
        if (utils::is_evil_race(*caster)) {
            if (caster->player.race == RACE_MAGUS) {
                if (GET_SKILL(caster, SPELL_SPEAR_OF_DARKNESS) > 50) {
                    spec_data->spell_id = SPELL_SPEAR_OF_DARKNESS;
                } else {
                    spec_data->spell_id = SPELL_DARK_BOLT;
                }
            } else {
                if (GET_SKILL(caster, SPELL_SEARING_DARKNESS) > 50) {
                    spec_data->spell_id = SPELL_SEARING_DARKNESS;
                } else {
                    spec_data->spell_id = SPELL_DARK_BOLT;
                }
            }
        } else if (GET_SKILL(caster, SPELL_LIGHTNING_STRIKE) > 50 && weather_type == SKY_LIGHTNING && OUTSIDE(caster)) {
            spec_data->spell_id = SPELL_LIGHTNING_STRIKE;
        } else if (GET_SKILL(caster, SPELL_CONE_OF_COLD) > 50) {
            spec_data->spell_id = SPELL_CONE_OF_COLD;
        } else if (GET_SKILL(caster, SPELL_LIGHTNING_BOLT) > 50 && OUTSIDE(caster)) {
            spec_data->spell_id = SPELL_LIGHTNING_BOLT;
        } else if (GET_SKILL(caster, SPELL_FIREBOLT) > 50) {
            spec_data->spell_id = SPELL_FIREBOLT;
        } else {
            spec_data->spell_id = SPELL_CHILL_RAY;
        }
    } break;
    case game_types::PS_Cold: {
        if (GET_SKILL(caster, SPELL_CONE_OF_COLD) > 50) {
            spec_data->spell_id = SPELL_CONE_OF_COLD;
        } else {
            spec_data->spell_id = SPELL_CHILL_RAY;
        }
    } break;
    case game_types::PS_Darkness: {
        if (caster->player.race == RACE_MAGUS) {
            if (GET_SKILL(caster, SPELL_SPEAR_OF_DARKNESS) > 50) {
                spec_data->spell_id = SPELL_SPEAR_OF_DARKNESS;
            } else {
                spec_data->spell_id = SPELL_DARK_BOLT;
            }
        } else {
            if (GET_SKILL(caster, SPELL_SEARING_DARKNESS) > 50) {
                spec_data->spell_id = SPELL_SEARING_DARKNESS;
            } else {
                spec_data->spell_id = SPELL_DARK_BOLT;
            }
        }
    } break;
    case game_types::PS_Fire: {
        if (utils::is_race_good(*caster)) {
            if (GET_SKILL(caster, SPELL_FIREBALL) > 50) {
                spec_data->spell_id = SPELL_FIREBALL;
            } else {
                spec_data->spell_id = SPELL_FIREBOLT;
            }
        } else {
            if (GET_SKILL(caster, SPELL_SEARING_DARKNESS) > 50) {
                spec_data->spell_id = SPELL_SEARING_DARKNESS;
            } else {
                spec_data->spell_id = SPELL_DARK_BOLT;
            }
        }
    } break;
    case game_types::PS_Lightning: {
        if (OUTSIDE(caster) && GET_SKILL(caster, SPELL_LIGHTNING_STRIKE) > 50) {
            spec_data->spell_id = SPELL_LIGHTNING_STRIKE;
        } else {
            spec_data->spell_id = SPELL_LIGHTNING_BOLT;
        }
    } break;
    default:
        break;
    }

    send_to_char(get_expose_spell_message(spec_data->spell_id), caster);
}
