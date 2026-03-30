/* ************************************************************************
 *   File: limits.c                                      Part of CircleMUD *
 *  Usage: limits & gain funcs for HMV, exp, hunger/thirst, idle time      *
 *                                                                         *
 *  All rights reserved.  See license.doc for complete information.        *
 *                                                                         *
 *  Copyright (C) 1993 by the Trustees of the Johns Hopkins University     *
 *  CircleMUD is based on DikuMUD, Copyright (C) 1990, 1991.               *
 ************************************************************************ */

#include "limits.h"
#include "comm.h"
#include "db.h"
#include "handler.h"
#include "interpre.h"
#include "pkill.h"
#include "platdef.h"
#include "profs.h"
#include "spells.h"
#include "structs.h"
#include "utils.h"
#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "big_brother.h"
#include "char_utils.h"
#include <algorithm>
#include <cmath>

extern char* pc_race_types[];

#define READ_TITLE(ch) pc_race_types[GET_RACE(ch)]

extern struct char_data* character_list;
extern struct obj_data* object_list;
extern struct room_data world;
extern int top_of_world;
extern struct time_info_data time_info;
extern struct skill_data skills[];
extern char* spell_wear_off_msg[];
extern struct char_data* fast_update_list;
extern int circle_shutdown;
extern struct char_data* death_waiting_list;
extern int r_mortal_start_room[];

void recalc_skills(struct char_data* ch);

extern void raw_kill(char_data* ch, char_data* killer, int attacktype);
void one_mobile_activity(char_data* ch);

#define MIN_RANK 1
#define MAX_RANK 10

ACMD(do_flee);
char saves_spell(struct char_data* ch, sh_int level, int bonus);
void stop_riding(struct char_data* ch);
char char_perception_check(struct char_data* ch);
int get_sun_level(int room);

/* When age < 15 return the value p0 */
/* When age in 15..29 calculate the line between p1 & p2 */
/* When age in 30..44 calculate the line between p2 & p3 */
/* When age in 45..59 calculate the line between p3 & p4 */
/* When age in 60..79 calculate the line between p4 & p5 */
/* When age >= 80 return the value p6 */
int graf(int age, int p0, int p1, int p2, int p3, int p4, int p5, int p6)
{

    if (age < 15)
        return (p0); /* < 15   */
    else if (age <= 29)
        return (int)(p1 + (((age - 15) * (p2 - p1)) / 15)); /* 15..29 */
    else if (age <= 44)
        return (int)(p2 + (((age - 30) * (p3 - p2)) / 15)); /* 30..44 */
    else if (age <= 59)
        return (int)(p3 + (((age - 45) * (p4 - p3)) / 15)); /* 45..59 */
    else if (age <= 79)
        return (int)(p4 + (((age - 60) * (p5 - p4)) / 20)); /* 60..79 */
    else
        return (p6); /* >= 80 */
}

int xp_to_level(int lvl) { return lvl * lvl * 1500; }

inline void advance_mini_level(struct char_data* ch)
{
    int i, n;
    i = ++GET_MINI_LEVEL(ch);
    /* add on average 2 HP/level */
    if (GET_MAX_MINI_LEVEL(ch) < GET_MINI_LEVEL(ch)) {
        if (number() >= 0.98) {
            ch->constabilities.hit++;
        }
    }

    if ((xp_to_level(GET_LEVEL(ch) + 1) <= GET_EXP(ch))) {
        GET_LEVEL(ch)
        ++;
        advance_level(ch);
    }

    for (n = 1; n <= MAX_PROFS; n++) {
        if ((i)*GET_PROF_COOF(n, ch) >= 100000 * (GET_PROF_LEVEL(n, ch) + 1)) {
            if (GET_PROF_COOF(n, ch) * LEVEL_MAX >= 1000 * (GET_PROF_LEVEL(n, ch) + 1)) {
                advance_level_prof(n, ch);
            }
        }
    }

    GET_MAX_MINI_LEVEL(ch) = std::max(GET_MAX_MINI_LEVEL(ch), GET_MINI_LEVEL(ch));
}

double adjust_regen_for_level(int character_level, double regen_amount)
{
    // No extra regen for characters over level 10.
    if (character_level > 10)
        return regen_amount;

    const double flat_multiplier = 2.0;
    const double level_penalty = 0.1;

    double adjusted_amount = regen_amount * (flat_multiplier - (character_level * level_penalty));
    return adjusted_amount;
}

float get_bonus_mana_gain(const char_data* character) { return character->points.mana_regen; }

/* manapoint gain pr. game hour */
float mana_gain(const char_data* character)
{
    using namespace utils;

    float gain(character->player.level);
    if (is_pc(*character)) {
        const char_ability_data& abils = character->tmpabilities;
        gain = 8.0 + abils.intel / 2.0 + abils.wil / 5.0;
        gain = gain + get_prof_level(PROF_MAGE, *character) / 5.0 + get_prof_level(PROF_CLERIC, *character) / 5.0;

        switch (character->specials.position) {
        case POSITION_SLEEPING:
            gain *= 2;
            break;
        case POSITION_RESTING:
            gain *= 1.5;
            break;
        case POSITION_SITTING:
            gain *= 1.25;
            break;
        }
    } else {
        // NPCs that aren't in combat regenerate mana 50% faster to compensate for increased player
        // health/mana regen.
        if (character->specials.fighting == NULL) {
            gain *= 1.5;
        }
    }

    if (is_affected_by(*character, AFF_POISON)) {
        gain *= 0.25;
    }

    if (get_condition(*character, FULL) == 0 || get_condition(*character, THIRST) == 0) {
        gain *= 0.25;
    }

    gain = adjust_regen_for_level(character->player.level, gain);

    // add bonus regen after all modifiers
    gain += get_bonus_mana_gain(character);

    return gain;
}

float get_bonus_hit_gain(const char_data* character)
{
    float perception_modifier = utils::get_perception(*character) / 100.0f;

    // Early out if we have no perception.
    if (perception_modifier <= 0.0f)
        return character->points.health_regen;

    // Iterate through affect list for regeneration spells.
    float bonus_gain = 0.0f;
    for (affected_type* affect = character->affected; affect != nullptr; affect = affect->next) {
        if (affect->type == SPELL_CURING) {
            bonus_gain += affect->modifier;
        }

        if (affect->type == SPELL_RESTLESSNESS) {
            bonus_gain -= affect->modifier;
        }

        if (affect->type == SPELL_REGENERATION) {
            bonus_gain += affect->duration * 6 / (float)FAST_UPDATE_RATE;
        }
    }

    return (bonus_gain * perception_modifier) + character->points.health_regen;
}

/* Hitpoint gain pr. game hour */
float hit_gain(const char_data* character)
{
    using namespace utils;

    float gain(character->player.level);

    if (is_pc(*character)) {
        gain = 8.0 + character->tmpabilities.con / 2.0 + get_prof_level(PROF_WARRIOR, *character) / 3.0 + get_prof_level(PROF_RANGER, *character) / 5.0;

        switch (character->specials.position) {
        case POSITION_SLEEPING:
            gain *= 1.5;
            break;
        case POSITION_RESTING:
            gain *= 1.25;
            break;
        case POSITION_SITTING:
            gain *= 1.125;
            break;
        }
    } else {
        // NPCs that aren't in combat regenerate health 50% faster to compensate for increased
        // player health/mana regen.
        if (character->specials.fighting == NULL) {
            gain *= 1.5;
        }
    }

    bool is_poisoned = is_affected_by(*character, AFF_POISON);

    if (is_poisoned) {
        gain *= 0.25;
    }

    if (get_condition(*character, FULL) == 0 || get_condition(*character, THIRST) == 0) {
        gain *= 0.25;
    }

    if (GET_RACE(character) == RACE_OLOGHAI && !is_poisoned) {
        gain *= 1.5;
    }

    gain = adjust_regen_for_level(character->player.level, gain);

    // add bonus health regen after all modifiers
    gain += get_bonus_hit_gain(character);

    return gain;
}

float get_bonus_move_gain(const char_data* character)
{
    float perception_modifier = utils::get_perception(*character) / 100.0f;

    // Early out if we have no perception.
    if (perception_modifier <= 0.0f)
        return character->points.move_regen;

    // Iterate through affect list for regeneration spells.
    float bonus_gain = 0.0f;
    for (affected_type* affect = character->affected; affect != nullptr; affect = affect->next) {
        if (affect->type == SPELL_CURING) {
            bonus_gain -= affect->modifier;
        } else if (affect->type == SPELL_RESTLESSNESS) {
            bonus_gain += affect->modifier;
        } else if (affect->type == SPELL_VITALITY) {
            bonus_gain += affect->duration * 6 / (float)FAST_UPDATE_RATE;
        }
    }

    return (bonus_gain * perception_modifier) + character->points.move_regen;
}

float move_gain(const char_data* character)
/* move gain pr. game hour */
{
    using namespace utils;

    const char_ability_data& stats = character->tmpabilities;
    if (is_npc(*character)) {
        if (is_mob_flagged(*character, MOB_MOUNT)) {
            return 26 + (character->player.level * 2) + stats.con + stats.dex + get_bonus_move_gain(character);
        }
    }

    float gain = 7.0f;

    // Animals get move regen bonus
    if (GET_BODYTYPE(character) == 2) {
        gain += character->player.level + 10;
    }

    /* Prof/Level calculations */
    gain += (stats.con + stats.dex) / 2.0;

    /* Expertise for Rangers and Travelling Skill */
    float expertise_modifier = (get_prof_level(PROF_RANGER, *character) / 6.0) + (get_raw_knowledge(*character, SKILL_TRAVELLING) / 40);
    gain += expertise_modifier;

    /* Skill/Spell calculations */

    /* Position calculations    */
    switch (character->specials.position) {
    case POSITION_SLEEPING:
        gain *= 1.75;
        break;
    case POSITION_RESTING:
        gain *= 1.5;
        break;
    case POSITION_SITTING:
        gain *= 1.125;
        break;
    }

    if (is_npc(*character)) {
        // Tames get double move regen (have to be animals).
        if (affected_by_spell(const_cast<char_data*>(character), SKILL_TAME)) {
            gain *= 2.0;
            char_data* master = character->master;
            if (master && get_specialization(*master) == PLRSPEC_PETS) {
                // If the tamer is animals spec, double move regen again.
                gain *= 2.0;
            }
        }
        gain += get_bonus_move_gain(character);
        return gain - 5;
    } else {
        int race = character->player.race;
        bool is_poisoned = is_affected_by(*character, AFF_POISON);

        if (race == RACE_WOOD || race == RACE_HIGH) {
            gain += 5.0;
        }

        if (race == RACE_BEORNING) {
            gain *= 1.50;
        }

        if (race == RACE_OLOGHAI && !is_poisoned) {
            gain *= 1.50;
        }

        if (is_poisoned) {
            gain *= 0.25;
        }

        if (affected_by_spell(character, SKILL_MARK)) {
            gain *= 0.25;
        }

        if (get_condition(*character, FULL) == 0 || get_condition(*character, THIRST) == 0) {
            gain *= 0.25;
        }

        // Add flat regen after all modifiers
        gain += get_bonus_move_gain(character);

        return gain;
    }
}

void set_title(char_data* character)
{
    if (GET_TITLE(character))
        RELEASE(GET_TITLE(character));
    CREATE(GET_TITLE(character), char, strlen(READ_TITLE(character)) + 5);

    sprintf(GET_TITLE(character), "the %s", READ_TITLE(character));
    *(GET_TITLE(character) + 4) = toupper(*(GET_TITLE(character) + 4));
}

void check_autowiz(struct char_data* ch)
{
    char buf[100];
    extern int min_wizlist_lev;

    if (GET_LEVEL(ch) >= LEVEL_IMMORT) {
        sprintf(buf, "nice ../bin/autowiz %d %s %d %s %d &", min_wizlist_lev, WIZLIST_FILE,
            LEVEL_IMMORT, IMMLIST_FILE, getpid());
        mudlog("Initiating autowiz.", NRM, LEVEL_IMMORT, FALSE);
        system(buf);
    }
}

/* EXP for a level, is level^2 * 1500 */
/* EXP for a given min_level is xp_to_level(mini_level)/10000 */

/*
 * Gain experience with sanity checking.
 *
 * Add experience to a character.  You can't gain more than 7000
 * experience from any single gain and you cannot lose more than
 * 10,000 experience from any single loss.  Furthermore, characters
 * can only gain experience if they're between levels 1 and
 * LEVEL_IMMORT - 1.
 *
 * NOTE: I can't think of any time when this function is used with
 * a negative gain.  Those cases always use gain_exp_regardless,
 * because I've never seen this 10,000 exp loss minimum come into
 * play.
 */
void gain_exp(struct char_data* ch, int gain)
{
    if (GET_LEVEL(ch) < 0)
        return;

    /* Don't let LEVEL_IMMORT - 1 chars gain exp, otherwise they'll be imms */
    if (gain > 0 && GET_LEVEL(ch) < LEVEL_IMMORT - 1) {
        gain = MIN(7000, gain);
        gain_exp_regardless(ch, gain);
    }

    /* Imms shouldn't lose experience */
    if (gain < 0 && GET_LEVEL(ch) < LEVEL_IMMORT) {
        gain = MAX(-10000, gain); /* Never lose more than 10000 */
        gain_exp_regardless(ch, gain);
    }
}

/*
 * Add exp to a character regardless of how large or small the
 * experience gain is.  For example, gain_exp_regardless is used
 * when creating new implementors or when a player dies from a
 * mob death.
 */
void gain_exp_regardless(char_data* character, int gain)
{
    /* NPCs aren't allowed to gain experience */
    if (IS_NPC(character))
        return;

    int temp_int = GET_MINI_LEVEL(character);
    bool is_altered = false;

    if (gain > 0) {
        GET_EXP(character) += gain;

        for (temp_int = GET_MINI_LEVEL(character);
             temp_int * temp_int * 3 / 20 <= GET_EXP(character); temp_int++) {
            if (temp_int > GET_MINI_LEVEL(character)) {
                advance_mini_level(character);
                is_altered = true;
                GET_MINI_LEVEL(character) = temp_int;
            }
        }
    }

    if (gain < 0) {
        GET_EXP(character) += gain;
        is_altered = true;

        /* Each loop loses one level, with tolerance of 20,000 exp */
        while (xp_to_level(GET_LEVEL(character)) - 20000 > GET_EXP(character)) {
            GET_LEVEL(character) = GET_LEVEL(character) - 1;
            GET_MINI_LEVEL(character) = 100 * GET_LEVEL(character);
            SPELLS_TO_LEARN(character) -= PRACS_PER_LEVEL + GET_LEA_BASE(character) / LEA_PRAC_FACTOR;

            send_to_char("You lose a level!\n\r", character);
        }
    }

    /* Sanity check: don't let exp go negative */
    if (GET_EXP(character) < 0)
        GET_EXP(character) = 0;

    /* If we did anything, recalc hp, mana, moves and other points */
    if (is_altered) {
        int hp_lack = GET_MAX_HIT(character) - GET_HIT(character);
        affect_total(character);
        GET_HIT(character) = std::max(GET_HIT(character), GET_MAX_HIT(character) - hp_lack);
    }
}

void gain_condition(struct char_data* ch, int condition, int value)
{
    if (GET_COND(ch, condition) == -1) /* No change */
        return;

    if (IS_SHADOW(ch))
        return;

    char intoxicated = (GET_COND(ch, DRUNK) > 0);

    GET_COND(ch, condition) += value;

    GET_COND(ch, condition) = std::max(0, GET_COND(ch, condition));
    GET_COND(ch, condition) = std::min(24, GET_COND(ch, condition));

    if (GET_COND(ch, condition) || PLR_FLAGGED(ch, PLR_WRITING))
        return;

    switch (condition) {
    case FULL:
        send_to_char("You are hungry.\n\r", ch);
        return;
    case THIRST:
        send_to_char("You are thirsty.\n\r", ch);
        return;
    case DRUNK:
        if (intoxicated)
            send_to_char("You are now sober.\n\r", ch);
        return;
    default:
        break;
    }
}

void Crash_extract_objs(obj_data*);

//============================================================================
// Returns 1 if the char was extracted, 0 otherwise
//============================================================================
int check_idling(char_data* character)
{
    extern int r_mortal_idle_room[];

    // Gods get their own checks, and are never auto-disconnected.
    if ((GET_LEVEL(character) >= LEVEL_GOD) && (character->desc) && (character->desc->descriptor)) {
        (character->specials.timer)++;
        if (character->specials.timer > 15) {
            SET_BIT(PLR_FLAGS(character), PLR_ISAFK);
        }
        return 0;
    }

    if (character->specials.timer++ >= 3) {
        if (!character->specials.fighting) {
            bool was_afk = IS_SET(PLR_FLAGS(character), PLR_ISAFK);

            // Mark the character as AFK and give them AFK protection after 3 minute.
            SET_BIT(PLR_FLAGS(character), PLR_ISAFK);

            game_rules::big_brother& bb_instance = game_rules::big_brother::instance();
            bb_instance.on_character_afked(character);
            if (!was_afk) {
                send_to_char("You have been idle, and are now flagged as AFK.\r\n", character);
            }
        }
    }
    if (character->specials.timer > 8) {
        // Pull a character into the void after 8 minutes.
        if (character->specials.was_in_room == NOWHERE && character->in_room != NOWHERE) {
            character->specials.was_in_room = character->in_room;
            if (character->specials.fighting) {
                stop_fighting(character->specials.fighting);
                stop_fighting(character);
            }
            if (IS_RIDING(character)) {
                stop_riding(character);
            }
            act("$n disappears into the void.", TRUE, character, 0, 0, TO_ROOM);
            send_to_char("You have been idle, and are pulled into a void.\r\n", character);
            save_char(character, NOWHERE, 0);
            Crash_crashsave(character);
            char_from_room(character);
            char_to_room(character, r_mortal_idle_room[GET_RACE(character)]);
        }
    }
    // Disconnect a player after being idle for 28 minutes.
    if (character->specials.timer > 28) {
        if (character && !IS_NPC(character) && (character->specials.was_in_room != NOWHERE)) {
            if (character->in_room != NOWHERE) {
                char_from_room(character);
            }
            char_to_room(character, character->specials.was_in_room);
            save_char(character, world[character->specials.was_in_room].number, 0);
            character->specials.was_in_room = NOWHERE;
        }
        Crash_idlesave(character);
        sprintf(buf, "%s force-rented and extracted (idle).", GET_NAME(character));
        mudlog(buf, NRM, LEVEL_GOD, TRUE);

        for (int j = 0; j < MAX_WEAR; j++) {
            if (character->equipment[j]) {
                Crash_extract_objs(character->equipment[j]);
                character->equipment[j] = NULL;
            }
        }
        Crash_extract_objs(character->carrying);

        if (character->desc && character->desc->descriptor) {
            close_socket(character->desc);
            character->desc = NULL; // was commented out, now put back in by Fingolfin Jan 9
        }

        extract_char(character);
        return 1;
    }

    return 0;
}

/* Update both PC's & NPC's and objects*/
void recount_light_room(int room);
void update_room_tracks();
void update_bleed_tracks();
extern int LOOT_DECAY_TIME;

void point_update(void)
{
    int recalc_flag = 0, full_update, feeling, mytime, tmp;
    void update_char_objects(struct char_data * ch); /* handler.c */
    void extract_obj(struct obj_data * obj); /* handler.c */
    struct char_data *i, *next_dude;
    struct obj_data *j, *next_thing, *jj, *next_thing2;
    struct affected_type* hjp;

    /* characters */
    recalc_zone_power();
    update_room_tracks();
    update_bleed_tracks();

    mytime = time(0);

    if (((mytime / 3600) % 24 == 9) && ((mytime / 60) % 60 == 30)) {
        send_to_all("ROUTINE REBOOT IN 30 MINUTES.\n\r");
    }
    if (((mytime / 3600) % 24 == 9) && ((mytime / 60) % 60 == 55)) {
        send_to_all("ROUTINE REBOOT IN 5 MINUTES.\n\r");
    }
    if (((mytime / 3600) % 24 == 9) && ((mytime / 60) % 60 == 56)) {
        send_to_all("ROUTINE REBOOT IN 4 MINUTES.\n\r");
    }
    if (((mytime / 3600) % 24 == 9) && ((mytime / 60) % 60 == 59)) {
        send_to_all("ROUTINE REBOOT IN 1 MINUTE.\n\r");
    }
    if (((mytime / 3600) % 24 == 10) && (((mytime / 60) % 60 == 0) || ((mytime / 60) % 60 == 1))) {
        send_to_all("ROUTINE REBOOT NOW.\n\r");
        circle_shutdown = 1;
    }

    for (i = character_list; i; i = next_dude) {
        next_dude = i->next;

        affect_total(i, AFFECT_TOTAL_TIME);
        feeling = report_zone_power(i);
        // hint/stench messages no longer automatic
        if ((GET_LEVEL(i) < LEVEL_IMMORT) && (GET_POS(i) > POSITION_SLEEPING) && !IS_SET(PLR_FLAGS(i), PLR_WRITING)) {
            if ((feeling < 0) && !(number(0, 3))) {
                send_to_char("You catch a waft of evil stench in the air.\n\r", i);
            }
            if ((feeling > 0) & !(number(0, 4))) {
                send_to_char("The air brings a hint of goodness' presence to you.\n\r", i);
            }
        }

        if (!IS_NPC(i)) {
            update_char_objects(i);

            //  Time messages removed.
            //  if(PRF_FLAGGED(i, PRF_TIME) && (GET_POS(i) >= POSITION_SLEEPING))
            //    send_to_char("You feel the time passing by...\n\r",i);
            if (check_idling(i))
                continue;
        }

        full_update = -1;
        if ((hjp = affected_by_spell(i, SPELL_SLOW_DIGESTION)) != 0) {
            if (hjp->modifier > number(0, 49))
                full_update = 0;
        }

        if ((!IS_NPC(i) && !PLR_FLAGGED(i, PLR_RETIRED)) || IS_NPC(i)) {
            gain_condition(i, FULL, full_update);
            gain_condition(i, THIRST, full_update);
        }
        gain_condition(i, DRUNK, -1);

        if (GET_POS(i) == POSITION_STUNNED)
            update_pos(i); // Added by Fingolfin 30th December 2001

        if (GET_POS(i) >= POSITION_STUNNED) {
            if (GET_AMBUSHED(i))
                if (IS_NPC(i))
                    GET_AMBUSHED(i) -= GET_AMBUSHED(i) / (GET_LEVELA(i) + 5) + 1;
                else
                    GET_AMBUSHED(i) -= GET_AMBUSHED(i) / 10 + 1;

            recalc_flag = 0;

            if (GET_POS(i) == POSITION_SLEEPING)
                tmp = 2;
            else
                tmp = 1;

            if (GET_STR(i) != GET_STR_BASE(i)) {
                SET_STR(i, MIN(GET_STR(i) + number(1, tmp), GET_STR_BASE(i)));
                recalc_flag++;
            }
            if (GET_INT(i) != GET_INT_BASE(i)) {
                GET_INT(i) = MIN(GET_INT(i) + number(1, tmp), GET_INT_BASE(i));
                recalc_flag++;
            }
            if (GET_WILL(i) != GET_WILL_BASE(i)) {
                GET_WILL(i) = MIN(GET_WILL(i) + number(1, tmp), GET_WILL_BASE(i));
                recalc_flag++;
            }
            if (GET_DEX(i) != GET_DEX_BASE(i)) {
                GET_DEX(i) = MIN(GET_DEX(i) + number(1, tmp), GET_DEX_BASE(i));
                recalc_flag++;
            }
            if (GET_CON(i) != GET_CON_BASE(i)) {
                GET_CON(i) = MIN(GET_CON(i) + number(1, tmp), GET_CON_BASE(i));
                recalc_flag++;
            }
            if (GET_LEA(i) != GET_LEA_BASE(i)) {
                GET_LEA(i) = MIN(GET_LEA(i) + number(1, tmp), GET_LEA_BASE(i));
                recalc_flag++;
            }
            if (recalc_flag) {
                //	recalc_abilities(i);
                affect_total(i);
            }

            if (IS_NPC(i) && i->specials.attacked_level) {

                if (i->specials.attacked_level <= 1)
                    i->specials.attacked_level = 0;
                else if (GET_POS(i) != POSITION_FIGHTING)
                    i->specials.attacked_level -= 2;
            }

            if (!affected_by_spell(i, SPELL_POISON) && IS_AFFECTED(i, AFF_POISON))
                damage(i, i, 5, SPELL_POISON, 0);
            //        if (GET_POS(i) == POSITION_STUNNED)
            //  	update_pos(i);

        } else if (GET_POS(i) == POSITION_INCAP)
            damage(i, i, 3, TYPE_SUFFERING, 0);
    } /* for */

    /* objects */
    for (j = object_list; j; j = next_thing) {
        next_thing = j->next; /* Next in object list */

        //      /* If this is a corpse */
        // if ( (GET_ITEM_TYPE(j) == ITEM_CONTAINER) && (j->obj_flags.value[3]) ) {
        if (j->obj_flags.timer >= 0) {
            /* timer count down */
            if (j->obj_flags.timer > 0)
                j->obj_flags.timer--;

            if (j->obj_flags.timer == 0) {
                if (j->carried_by) {
                    act("$p decays in your hands.", FALSE, j->carried_by, j, 0, TO_CHAR);
                } else if ((j->in_room != NOWHERE) && (world[j->in_room].people)) {
                    act("$p decays into dust.", TRUE, world[j->in_room].people, j, 0, TO_ROOM);
                    act("$p decays into dust.", TRUE, world[j->in_room].people, j, 0, TO_CHAR);
                }

                if (GET_ITEM_TYPE(j) == ITEM_CONTAINER) {
                    // If this is a corpse, let big brother know that it is decaying.
                    if (j->obj_flags.value[3] == 1) {
                        game_rules::big_brother& bb_instance = game_rules::big_brother::instance();
                        bb_instance.on_corpse_decayed(j);
                    }

                    for (jj = j->contains; jj; jj = next_thing2) {
                        next_thing2 = jj->next_content; /* Next in inventory */
                        obj_from_obj(jj);

                        if (j->in_obj) {
                            obj_to_obj(jj, j->in_obj);
                        } else if (j->carried_by) {
                            obj_to_room(jj, j->carried_by->in_room);
                        } else if (j->in_room != NOWHERE) {
                            obj_to_room(jj, j->in_room);
                        } else {
                            log("SYSERR: OBJ DECAYED IN NOWHERE (limits.c)!!!");
                        }

                        jj->obj_flags.timer = LOOT_DECAY_TIME;
                    }
                }

                extract_obj(j);
                continue;
            }
        }

        if (GET_ITEM_TYPE(j) == ITEM_FOUNTAIN) {
            /* supposedly this will refill fountains at zone resets. */
            //      printf("resetting fountain %s\n",j->name);
            j->obj_flags.value[1] = j->obj_flags.value[0];
        } else if (GET_ITEM_TYPE(j) == ITEM_LIGHT) {
            if ((j->obj_flags.value[2] > 0) && (j->obj_flags.value[3] > 0) && !(IS_NPC(j->carried_by)))
                j->obj_flags.value[2]--;
            if ((j->obj_flags.value[2] == 0) && (j->obj_flags.value[3] > 0)) {
                // the torch went out messages
                j->obj_flags.value[3] = 0;

                if (j->carried_by) {
                    act("Your $o went out.", FALSE, j->carried_by, j, 0, TO_CHAR);
                    act("$n's $o went out.", TRUE, j->carried_by, j, 0, TO_ROOM);
                    recount_light_room(j->carried_by->in_room);
                } else if (j->in_room != NOWHERE) {
                    sprintf(buf, "%s here went out.\n\r", j->short_description);
                    send_to_room(buf, j->in_room);
                    recount_light_room(j->in_room);
                }
                extract_obj(j);
            } else if ((j->obj_flags.value[2] < 3) && (j->obj_flags.value[2] >= 0) && (j->obj_flags.value[3] > 0)) {
                // the torch flickers messages

                if (j->carried_by) {
                    act("Your $o flickers weakly.", FALSE, j->carried_by, j, 0, TO_CHAR);
                    act("$n's $o flickers weakly.", TRUE, j->carried_by, j, 0, TO_ROOM);
                    //	  recount_light_room(j->carried_by->in_room);
                } else if (j->in_room != NOWHERE) {
                    sprintf(buf, "%s here flickers weakly.\n\r", j->short_description);
                    send_to_room(buf, j->in_room);
                    //	  recount_light_room(j->in_room);
                }
            }
        } else if ((GET_ITEM_TYPE(j) == ITEM_CONTAINER) && (!j->obj_flags.value[3]) && (!j->carried_by) && (j->in_room != NOWHERE)) {
            /* the object is not a corpse, and is on the ground... */
            /* restoring its closed/locked state */
            if (!(j->obj_flags.value[1] & CONT_CLOSED) && (j->obj_flags.value[4] & CONT_CLOSED))
                act("$o closes quietly.", TRUE, 0, j, 0, TO_ROOM);
            if ((j->obj_flags.value[1] & CONT_CLOSED) && !(j->obj_flags.value[4] & CONT_CLOSED))
                act("$o opens quietly.", TRUE, 0, j, 0, TO_ROOM);
            if (!(j->obj_flags.value[1] & CONT_LOCKED) && (j->obj_flags.value[4] & CONT_LOCKED))
                act("You hear a soft click.", FALSE, 0, j, 0, TO_ROOM);
            j->obj_flags.value[1] = j->obj_flags.value[4];
        }
    }
}

void update_room_tracks()
{
    room_data* tmproom;
    int roomnum, tmp;

    for (roomnum = 0; roomnum <= top_of_world; roomnum++) {
        tmproom = &world[roomnum];

        for (tmp = 0; tmp < NUM_OF_TRACKS; tmp++)
            if (tmproom->room_track[tmp].data / 8 == time_info.hours) {
                tmproom->room_track[tmp].char_number = 0;
                tmproom->room_track[tmp].data = 0;
                tmproom->room_track[tmp].condition = 0;
            }
    }
}

void update_bleed_tracks()
{
    room_data* tmproom;
    int roomnum, tmp;

    for (roomnum = 0; roomnum <= top_of_world; roomnum++) {
        tmproom = &world[roomnum];

        for (tmp = 0; tmp < NUM_OF_BLOOD_TRAILS; tmp++)
            if (tmproom->bleed_track[tmp].data / 8 == time_info.hours) {
                tmproom->bleed_track[tmp].char_number = 0;
                tmproom->bleed_track[tmp].data = 0;
                tmproom->bleed_track[tmp].condition = 0;
            }
    }
}

/*
 * Function called when a character chooses "1" from the main
 * menu and the character's level is 0.
 */
void do_start(struct char_data* ch)
{
    int tmp;
    extern int top_of_p_table; /* From db.cc */

    GET_MINI_LEVEL(ch) = 99;
    GET_LEVEL(ch) = 0;
    GET_EXP(ch) = 1500;

    if (GET_RACE(ch) == RACE_BEORNING) {
        GET_BODYTYPE(ch) = 15;
    } else {
        GET_BODYTYPE(ch) = 1;
    }
    set_title(ch);

    roll_abilities(ch, 80, 85);

    ch->constabilities.move = 80;
    ch->constabilities.mana = 40;
    ch->points.spirit = 9;
    ch->constabilities.hit = 10; /* These are BASE numbers   */
    SPELLS_TO_LEARN(ch) = 10;

    if (!number(0, 1))
        ch->constabilities.hit++;

    GET_GOLD(ch) = number(200, 300);

    advance_mini_level(ch);
    gain_exp_regardless(ch, 1500);

    for (tmp = 0; tmp < MAX_SKILLS; tmp++)
        ch->skills[tmp] = 0;

    recalc_skills(ch);
    recalc_abilities(ch);
    GET_REROLLS(ch) = 0;

    GET_HIT(ch) = GET_MAX_HIT(ch);
    GET_MANA(ch) = GET_MAX_MANA(ch);
    GET_MOVE(ch) = GET_MAX_MOVE(ch);

    GET_COND(ch, THIRST) = 48;
    GET_COND(ch, FULL) = 48;
    GET_COND(ch, DRUNK) = 0;

    ch->player.time.played = 0;
    ch->player.time.logon = time(0);

    /*
     * If no player files exist, the top of the p_table is -1.  We
     * call create_entry before we call do_start, and create_entry
     * will create a new cell in the ptab and increment the top by
     * 1.  Hence if the top is 0, there are no players other than
     * this one and we want it to be the imp.
     */
    if (top_of_p_table == 0) {
        log("Player table was empty: creating new implementor.\r\n");
        gain_exp_regardless(ch, xp_to_level(LEVEL_IMPL));
    }
}

void finalize_new_character_start_state(struct char_data* ch)
{
    if (ch == nullptr || GET_LEVEL(ch) != 0)
        return;

    do_start(ch);
    affect_total(ch);

    const int race = GET_RACE(ch);
    if (race < 0 || race >= MAX_RACES)
        return;

    const int start_room_rnum = r_mortal_start_room[race];
    if (start_room_rnum < 0 || start_room_rnum > top_of_world)
        return;

    ch->specials2.load_room = world[start_room_rnum].number;
}

// Simply checks that a character can breathe in the room they are in.
// If not, they are affected by asphyxiation, the effects of which are dealt with elsewhere.

void check_breathing(char_data* ch)
{
    affected_type newaf;
    affected_type* tmpaf;

    tmpaf = affected_by_spell(ch, SPELL_ASPHYXIATION);
    if (!(can_breathe(ch))) {
        if (tmpaf) {
            tmpaf->modifier += 1;
        } else {
            newaf.type = SPELL_ASPHYXIATION;
            newaf.duration = 100;
            newaf.modifier = 1;
            newaf.location = APPLY_NONE;
            newaf.bitvector = 0;
            affect_to_char(ch, &newaf);
            send_to_char("Panic!  You cannot breathe!!\n\r", ch);
        }
    } else {
        if (tmpaf) {
            if (tmpaf->modifier > 1)
                tmpaf->modifier -= 1;

            else {
                affect_from_char(ch, SPELL_ASPHYXIATION);
                send_to_char("Your breathing has returned to normal.\n\r", ch);
            }
        }
    }
}

bool is_rank_valid(int ranking)
{
    return (ranking != PKILL_UNRANKED) && (ranking >= MIN_RANK) && (ranking <= MAX_RANK);
}

void set_player_moves(struct char_data* ch, int mod, bool mode)
{
    if (!mode)
        mod = -mod;

    ch->constabilities.move += mod;
    ch->abilities.move += mod;
    ch->tmpabilities.move += mod;
}

void set_player_ob(struct char_data* ch, int mod, bool mode)
{
    if (!mode)
        mod = -mod;

    SET_OB(ch) += mod;
}

void set_player_hit(struct char_data* ch, int mod, bool mode)
{
    if (mode == false)
        mod = -mod;

    ch->constabilities.hit += mod;
    ch->abilities.hit += mod;
    ch->tmpabilities.hit += mod;
}

void set_player_con(struct char_data* ch, int mod, bool mode)
{
    if (mode == false)
        mod = -mod;

    ch->constabilities.con += mod;
    ch->abilities.con += mod;
}

void set_player_mana(struct char_data* ch, int mod, bool mode)
{
    if (mode == false)
        mod = -mod;

    ch->constabilities.mana += mod;
    ch->abilities.mana += mod;
    ch->tmpabilities.mana += mod;
}

void set_player_damage(struct char_data* ch, int mod, bool mode)
{
    if (!mode)
        mod = -mod;

    if (!mode && ch->points.damage == 0)
        return;

    ch->points.damage += mod;
}

void set_player_spell_pen(struct char_data* ch, int mod, bool mode)
{
    if (!mode)
        mod = -mod;
    ch->points.spell_pen += mod;
}

void assign_pk_mage_bonus(struct char_data* ch, int tier, bool mode)
{
    switch (tier) {
    case 1:
        set_player_mana(ch, 15, mode);
        set_player_con(ch, 2, mode);
        set_player_spell_pen(ch, 3, mode);
        break;
    case 2:
        set_player_con(ch, 1, mode);
        set_player_mana(ch, 10, mode);
        set_player_spell_pen(ch, 2, mode);
        break;
    case 3:
        set_player_con(ch, 1, mode);
        set_player_mana(ch, 5, mode);
        set_player_spell_pen(ch, 1, mode);
        break;
    case 4:
        set_player_mana(ch, 3, mode);
        break;
    default:
        break;
    }
}

void assign_pk_mystic_bonus(struct char_data* ch, int tier, bool mode)
{
    switch (tier) {
    case 1:
        set_player_con(ch, 2, mode);
        break;
    case 2:
        set_player_con(ch, 2, mode);
        break;
    case 3:
        set_player_con(ch, 1, mode);
        break;
    case 4:
    default:
        break;
    }
}

void assign_pk_ranger_bonus(struct char_data* ch, int tier, bool mode)
{
    switch (tier) {
    case 1:
        set_player_con(ch, 2, mode);
        set_player_moves(ch, 20, mode);
        set_player_ob(ch, 10, mode);
        break;
    case 2:
        set_player_con(ch, 1, mode);
        set_player_moves(ch, 15, mode);
        set_player_ob(ch, 7, mode);
        break;
    case 3:
        set_player_con(ch, 1, mode);
        set_player_moves(ch, 10, mode);
        set_player_ob(ch, 5, mode);
        break;
    case 4:
        set_player_moves(ch, 10, mode);
        set_player_ob(ch, 3, mode);
        break;
        ;
    default:
        break;
    }
}

void assign_pk_warrior_bonus(struct char_data* ch, int tier, bool mode)
{
    switch (tier) {
    case 1:
        set_player_con(ch, 2, mode);
        set_player_hit(ch, 15, mode);
        set_player_ob(ch, 10, mode);
        break;
    case 2:
        set_player_con(ch, 1, mode);
        set_player_hit(ch, 10, mode);
        set_player_ob(ch, 7, mode);
        break;
    case 3:
        set_player_con(ch, 1, mode);
        set_player_hit(ch, 5, mode);
        set_player_ob(ch, 5, mode);
        break;
    case 4:
        set_player_ob(ch, 3, mode);
        set_player_hit(ch, 5, mode);
        break;
    default:
        break;
    }
}

void assign_pk_bonuses(struct char_data* ch, int coeff, int tier, bool mode)
{
    switch (coeff) {
    case PROF_MAGE:
        assign_pk_mage_bonus(ch, tier, mode);
        break;
    case PROF_CLERIC:
        assign_pk_mystic_bonus(ch, tier, mode);
        break;
    case PROF_RANGER:
        assign_pk_ranger_bonus(ch, tier, mode);
        break;
    case PROF_WARRIOR:
        assign_pk_warrior_bonus(ch, tier, mode);
        break;
    default:
        break;
    }
}

void remove_fame_war_bonuses(struct char_data* ch, struct affected_type* pkaff)
{
    int coeff = utils::get_highest_coeffs(*ch);
    affected_type* aff = affected_by_spell(ch, SPELL_FAME_WAR);
    assign_pk_bonuses(ch, coeff, aff->modifier, false);
    recalc_abilities(ch);
}

void do_fame_war_bonuses(struct char_data* ch)
{
    if (IS_NPC(ch))
        return;

    int ranking = pkill_get_rank_by_character(ch, true) + 1;
    affected_type* pkaff = affected_by_spell(ch, SPELL_FAME_WAR);

    if (!is_rank_valid(ranking) && !pkaff) {
        ch->player.ranking = ranking;
        return; // If the player doesn't have ranking don't give them bonuses
    }

    int coeff = utils::get_highest_coeffs(*ch);
    int tier = utils::get_ranking_tier(ranking);

    if (!is_rank_valid(ranking) && pkaff) // player has dropped bonuses
    {
        remove_fame_war_bonuses(ch, pkaff);
        affect_remove(ch, pkaff);
    }

    if ((ranking == ch->player.ranking && pkaff) // ranking hasn't changed
        || ((ranking != ch->player.ranking && pkaff) && (pkaff->modifier == tier))) // ranking has but tier hasn't
    {
        pkaff->duration = 100;
    }

    if (pkaff && pkaff->modifier != tier) {
        assign_pk_bonuses(ch, coeff, pkaff->modifier, false);
        assign_pk_bonuses(ch, coeff, tier, true);
        if (tier < pkaff->modifier)
            send_to_char("The power of war increases in your body!\n\r", ch);
        else
            send_to_char("The power of war decreases in your body!\n\r", ch);
        pkaff->modifier = tier;
    }

    if (!pkaff) {
        affected_type newpkaf;
        assign_pk_bonuses(ch, coeff, tier, true);
        newpkaf.type = SPELL_FAME_WAR;
        newpkaf.duration = 100;
        newpkaf.location = APPLY_NONE;
        newpkaf.modifier = tier;
        newpkaf.bitvector = 0;
        newpkaf.counter = coeff;
        affect_to_char(ch, &newpkaf);
        send_to_char("The power of war empowers your body!\n\r", ch);
    }

    ch->player.ranking = ranking;
}

void do_power_of_arda(char_data* ch)
{
    affected_type* tmpaf;
    affected_type newaf;
    int level, maxlevel;

    if (GET_RACE(ch) == RACE_HARADRIM) {
        return;
    }

    level = get_sun_level(ch->in_room);
    maxlevel = level * 25; // so if you are in a shady room, the penalty can never be too high
    tmpaf = affected_by_spell(ch, SPELL_ARDA);
    if (level) {
        if (utils::get_race(*ch) == RACE_OLOGHAI) {
            level *= 3;
        }
        if (tmpaf) {
            if (tmpaf->modifier > maxlevel)
                tmpaf->modifier = MAX(tmpaf->modifier - level,
                    30); // if you are in a shady room, the affection can never disappear
            else
                tmpaf->modifier = MIN(tmpaf->modifier + level, 400);
            tmpaf->duration = 100; // to keep the affection going
        } else {
            newaf.type = SPELL_ARDA;
            newaf.duration = 100; // immaterial this figure
            newaf.modifier = level;
            newaf.location = APPLY_NONE;
            newaf.bitvector = 0;
            affect_to_char(ch, &newaf);
            send_to_char("The power of Arda weakens your body.\n\r", ch);
        }
    } else {
        if (tmpaf) {
            if (GET_POS(ch) < POSITION_RESTING)
                level = 20;
            else if (GET_POS(ch) < POSITION_STANDING)
                level = 15;
            else
                level = 10;
            tmpaf->modifier -= level;
            if (tmpaf->modifier <= 0)
                tmpaf->duration = 0;
        }
    }
}

void affect_update_person(struct char_data* i, int mode)
{
    affected_type* otheraf;
    // mode != 0 for fast_update

    static struct affected_type *af, *next_af_dude;
    int tmp, freq, val, time_phase;

    if (i->desc && i->desc->connected && (i->desc->connected != CON_LINKLS))
        return; // PC is not in 'playing' mode

    time_phase = get_current_time_phase();

    for (af = i->affected; af; af = next_af_dude) {
        next_af_dude = af->next;
        if (skills[af->type].is_fast || (!mode && (time_phase == af->time_phase))) {
            if ((af->duration >= 1) || (af->duration < 0)) {
                if (af->duration >= 1)
                    af->duration--;

                switch (af->type) {
                case SPELL_POISON:
                    otheraf = affected_by_spell(i, SPELL_RESIST_POISON);
                    if (otheraf) {
                        af->duration = std::max(af->duration - otheraf->modifier, 0);
                        otheraf->duration = af->duration;
                    }

                    /* If poison is fatal, damage returns non-zero */
                    if (damage(i, i, 5, SPELL_POISON, 0))
                        return;
                    break;
                case SPELL_CURING:
                case SPELL_RESTLESSNESS:
                    // handled by hit_gain and move_gain now.
                    break;
                case SPELL_FEAR:
                    do_flee(i, "", 0, 0, 0);
                    if (saves_spell(i, af->modifier -= 2, 0))
                        af->duration = 0;
                    break;
                case SPELL_VITALITY:
                    // handled by move_gain now.
                    break;
                case SPELL_REGENERATION:
                    // handled by hit_gain now.
                    break;
                case SPELL_ACTIVITY:
                    if (IS_NPC(i))
                        one_mobile_activity(i);
                case SPELL_CONFUSE:
                    if (IS_AFFECTED(i, AFF_CONCENTRATION) && (af->duration >= 10))
                        af->duration -= 3;
                    break;

                case SPELL_ASPHYXIATION:
                    int zone, under_water_zone, death_modifier;
                    zone = world[i->in_room].zone;
                    under_water_zone = 262;
                    death_modifier = zone == under_water_zone ? 80 : 40;

                    if (!(can_breathe(i))) {
                        if (af->modifier > death_modifier) {
                            send_to_char("You gasp one final time as your body loses its battle "
                                         "for life...\n\r",
                                i);
                            act("$n gasps one last time... and dies.", TRUE, i, nullptr, nullptr,
                                TO_ROOM);
                            raw_kill(i, nullptr, SPELL_ASPHYXIATION);
                            return;
                        }
                        if (af->modifier > 20) {
                            if (damage(i, i, (af->modifier / 5), SPELL_ASPHYXIATION, 0))
                                return;
                            GET_MOVE(i) = MAX(GET_MOVE(i) - 4, 10);
                        }
                    }
                    break;
                case SKILL_MARK:
                    /* If the player is affected by regeneration reduce the duration of mark. */
                    otheraf = affected_by_spell(i, SPELL_REGENERATION);
                    if (otheraf) {
                        /* If the target is at full health remove the duration faster. */
                        if (i->tmpabilities.hit >= i->abilities.hit) {
                            af->duration = std::max(af->duration - 4, 0);
                        } else {
                            af->duration = std::max(af->duration - 2, 0);
                        }
                    }
                    break;
                default:
                    break;
                }
                if (GET_POS(i) == POSITION_STUNNED)
                    update_pos(i);
            } else {
                if (af->type > 0 && af->type < MAX_SKILLS)
                    /* It must be a spell */
                    if (!af->next || af->next->type != af->type || af->next->duration > 0)
                        affect_remove_notify(i, af);
                    else
                        affect_remove(i, af);

                if (af->type == SPELL_ANGER)
                    i->specials.attacked_level = 0;
            }
        }
    }
}

ASPELL(spell_poison);

void affect_update_room(struct room_data* room)
{
    struct affected_type *tmpaf, *next_tmpaf, *checkaf;
    struct affected_type newaf;
    struct char_data *tmpch, *next_tmpch;
    int time_phase, tmp, direction, mod, roomnum, movechance;
    extern char* dirs[];

    time_phase = get_current_time_phase();

    movechance = number(1, 100); // to be used to check if mists move.

    for (tmpaf = room->affected; tmpaf; tmpaf = next_tmpaf) {
        next_tmpaf = tmpaf->next;

        switch (tmpaf->type) {
        case ROOMAFF_SPELL:
            if ((tmpaf->location >= 0) && (tmpaf->location < MAX_SKILLS) && skills[tmpaf->location].spell_pointer) {
                if (tmpaf->location == SPELL_MIST_OF_BAAZUNGA)
                    if (!IS_SET(room->room_flags, SHADOWY))
                        SET_BIT(room->room_flags, SHADOWY);

                if (1 /*skills[tmpaf->location].targets  & TAR_CHAR_ROOM*/) {
                    for (tmpch = room->people; tmpch; tmpch = next_tmpch) {
                        next_tmpch = tmpch->next_in_room;

                        /* 1 in 13 chance that a room spell won't do anything */
                        if (!(tmp = number(0, 12)) || (skills[tmpaf->location].is_fast && !number(0, 2))) {
                            (skills[tmpaf->location].spell_pointer)(tmpch, "", SPELL_TYPE_SPELL,
                                tmpch, 0, 0, 0);
                        }
                    }
                } else {
                    sprintf(buf2, "Attempt to cast spell %d in room %d", tmpaf->location,
                        room->number);
                    mudlog(buf2, NRM, LEVEL_GOD, FALSE);
                }
            }
        }

        if (((time_phase == tmpaf->time_phase) || ((tmpaf->type == ROOMAFF_SPELL) && skills[tmpaf->location].is_fast)) && (tmpaf->duration > 0))
            tmpaf->duration--;
        if (tmpaf->location == SPELL_MIST_OF_BAAZUNGA && tmpaf->duration > 0 && time_phase == tmpaf->time_phase) {
            /* 70% chance of mist thinking about moving */
            sprintf(buf, "check mist movement");
            mudlog(buf, NRM, LEVEL_GOD, FALSE);
            if (movechance < 75) {
                direction = number(0, NUM_OF_DIRS - 1);
                /* Decide if the random direction is legal, if so, move the mist */
                if (!(room->dir_option[direction])) {
                    sprintf(buf, "no option for movement");
                    mudlog(buf, NRM, LEVEL_GOD, FALSE);
                } else if (room->dir_option[direction]->to_room != NOWHERE) {
                    roomnum = room->dir_option[direction]->to_room;
                    if (!(room_affected_by_spell(&world[roomnum], SPELL_MIST_OF_BAAZUNGA))) {
                        if (tmpaf->modifier != 1)
                            REMOVE_BIT(room->room_flags, SHADOWY);
                        if ((checkaf = room_affected_by_spell(&world[roomnum], SPELL_MIST_OF_BAAZUNGA))) {
                            mod = checkaf->modifier;
                            sprintf(buf, "WARNING LOMAN: Mist already in move to room");
                            mudlog(buf, NRM, LEVEL_GOD, FALSE);
                        } else if (IS_SET(world[roomnum].room_flags, SHADOWY))
                            mod = 1;
                        else
                            mod = 0;

                        newaf.type = ROOMAFF_SPELL;
                        newaf.duration = tmpaf->duration;
                        newaf.modifier = mod;
                        newaf.location = SPELL_MIST_OF_BAAZUNGA;
                        newaf.bitvector = 0;

                        sprintf(buf, "The mists drift %s.\n\r", dirs[direction]);
                        send_to_room(buf, room->number);

                        affect_to_room(&world[roomnum], &newaf);
                        affect_remove_room(room, tmpaf);
                    }
                }
            }
        }

        if (tmpaf)
            if (tmpaf->duration == 0) {
                if (tmpaf->location == SPELL_MIST_OF_BAAZUNGA && tmpaf->modifier != 1)
                    REMOVE_BIT(room->room_flags, SHADOWY);
                affect_remove_room(room, tmpaf);
            }
    }
}

extern universal_list* affected_list;

extern universal_list* affected_list_pool;

void affect_update()
{
    universal_list *tmplist, *tmplist2, *tmplist3;
    ;
    char mybuf[1000];

    tmplist3 = 0;
    for (tmplist = affected_list; tmplist; tmplist = tmplist2) {
        tmplist2 = tmplist->next;

        if (tmplist->type == TARGET_CHAR) {

            if (char_exists(tmplist->number) && tmplist->ptr.ch && tmplist->ptr.ch->affected) {
                affect_update_person(tmplist->ptr.ch, 0);
            } else {
                if (char_exists(tmplist->number))
                    sprintf(mybuf, "Getting %s off the affected_list.", GET_NAME(tmplist->ptr.ch));
                else
                    strcpy(mybuf, "Getting Unknown char off the affected_list.");
                mudlog(mybuf, CMP, LEVEL_GRGOD, TRUE);
                from_list_to_pool(&affected_list, &affected_list_pool, tmplist);
            }
        } else if (tmplist->type == TARGET_ROOM) {
            affect_update_room(tmplist->ptr.room);
        }
        tmplist3 = tmplist; /* prev item */
    }
}

void fast_update()
{
    int freq = FAST_UPDATE_RATE;
    for (char_data* character = character_list; character != nullptr; character = character->next) {

        // Note:  Regen values can be negative, so we can't test if a character is below max as an
        // optimization.

        float health_regen_base = hit_gain(character) / freq;
        int hitregen = int(health_regen_base);
        if (number() < (std::abs(health_regen_base) - std::abs(std::trunc(health_regen_base)))) {
            hitregen += hitregen >= 0 ? 1 : -1;
        }

        float move_regen_base = move_gain(character) / freq;
        int moveregen = int(move_regen_base);
        if (number() < (std::abs(move_regen_base) - std::abs(std::trunc(move_regen_base)))) {
            moveregen += moveregen >= 0 ? 1 : -1;
        }

        float mana_regen_base = mana_gain(character) / freq;
        int manaregen = int(mana_regen_base);
        if (number() < (std::abs(mana_regen_base) - std::abs(std::trunc(mana_regen_base)))) {
            manaregen += manaregen >= 0 ? 1 : -1;
        }

        // Characters can die to negative regen values (think restlessness)
        GET_HIT(character) = std::min(GET_HIT(character) + hitregen, GET_MAX_HIT(character));
        if (GET_HIT(character) < 0 && hitregen < 0) {
            act("$n suddenly collapses on the ground.", TRUE, character, 0, 0, TO_ROOM);
            send_to_char("Your body failed to the magic.\n\r", character);
            raw_kill(character, NULL, TYPE_UNDEFINED);
            add_exploit_record(EXPLOIT_REGEN_DEATH, character, 0, NULL);
            return;
        }

        GET_MANA(character) = std::min(GET_MANA(character) + manaregen, (int)GET_MAX_MANA(character));
        GET_MOVE(character) = std::min(GET_MOVE(character) + moveregen, (int)GET_MAX_MOVE(character));

        // Make sure characters moves don't drop below zero.
        GET_MOVE(character) = std::max((int)GET_MOVE(character), 0);

        if (EVIL_RACE(character)) {
            do_power_of_arda(character);
        }

        check_breathing(character);
        do_fame_war_bonuses(character);
    }
}
