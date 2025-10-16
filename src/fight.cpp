/* ************************************************************************
 *   File: fight.c                                       Part of CircleMUD *
 *  Usage: Combat system                                                   *
 *                                                                         *
 *  All rights reserved.  See license.doc for complete information.        *
 *                                                                         *
 *  Copyright (C) 1993 by the Trustees of the Johns Hopkins University     *
 *  CircleMUD is based on DikuMUD, Copyright (C) 1990, 1991.               *
 ************************************************************************ */

#include "platdef.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "color.h"
#include "comm.h"
#include "db.h"
#include "handler.h"
#include "interpre.h"
#include "limits.h"
#include "pkill.h"
#include "script.h"
#include "spells.h"
#include "structs.h"
#include "utils.h"
#include "warrior_spec_handlers.h"
#include "zone.h" /* For zone_table */

#include "big_brother.h"
#include "char_utils.h"
#include "char_utils_combat.h"

#include <ctime>

#define IS_PHYSICAL(_at) \
    ((_at) >= TYPE_HIT && (_at) <= TYPE_CRUSH ? TRUE : FALSE)

/* Structures */
struct char_data* combat_list = 0; /* head of l-list of fighting chars */
struct char_data* combat_next_dude = 0; /* Next dude global trick */

/* External structures */
extern struct room_data world;
extern struct message_list fight_messages[MAX_MESSAGES];
extern struct obj_data* object_list;
extern struct char_data* waiting_list; /* in db.cpp */
extern int immort_start_room;
extern int mortal_start_room[];
extern int r_mortal_start_room[];
extern int r_immort_start_room;
extern struct index_data* mob_index;
extern int armor_absorb(struct obj_data* obj);
extern const int average_mob_life;
extern skill_data skills[];
extern int max_race_str[];
extern bool weapon_willpower_damage(char_data* ch, char_data* victim);
extern void check_break_prep(struct char_data*);
extern const int max_npc_corpse_time, max_pc_corpse_time;
extern char* pc_star_types[];

/* External procedures */
char* fread_string(FILE* fl, char* error);
int check_resistances(char_data* ch, int attacktype);
void stop_hiding(struct char_data*, char);
void break_meditation(char_data* ch);
ACMD(do_flee);
ACMD(do_stand);
ACMD(do_mental);

/* local procedures */
void hit(struct char_data* ch, struct char_data* victim, int type);
void do_parry(struct char_data* ch, struct char_data* victim, int type);
void forget(struct char_data* ch, struct char_data* victim);
void remember(struct char_data* ch, struct char_data* victim);
void complete_delay(struct char_data* ch);
void group_gain(struct char_data* ch, struct char_data* victim);
void do_pass_through(struct char_data* ch, struct char_data* victim, int type);
/* riposte doesn't use int type, but maybe when messages are sorted... */
void do_riposte(struct char_data* ch, struct char_data* victim);
int check_hallucinate(struct char_data* ch, struct char_data* victim);

/* Weapon attack texts */
struct attack_hit_type attack_hit_text[] = {
    { "hit", "hits" },
    { "pound", "pounds" },
    { "pierce", "pierces" },
    { "slash", "slashes" },
    { "stab", "stabs" },
    { "whip", "whips" },
    { "stab", "stabs" },
    { "claw", "claws" },
    { "bite", "bites" },
    { "sting", "stings" },
    { "cleave", "cleaves" },
    { "flail", "flails" },
    { "smite", "smites" },
    { "crush", "crushes" }, /* TYPE_CRUSH    */
    { "crush3", "crushes3" } /* TYPE_CRUSH    */
};

/*
 * Used to remove invisibility; now it removes sanctuary
 */
void appear(struct char_data* ch)
{
    if (affected_by_spell(ch, SPELL_SANCTUARY)) {
        affect_from_char(ch, SPELL_SANCTUARY);
        send_to_char("Your sanctuary is ended!\n\r", ch);
    }
    REMOVE_BIT(ch->specials.affected_by, AFF_SANCTUARY);
}

void load_messages(void)
{

    FILE* f1;
    struct message_type* messages;
    int i, type;
    char chk[100];

    if (!(f1 = fopen(MESS_FILE, "r"))) {
        sprintf(buf2, "Error reading combat message file %s", MESS_FILE);
        perror(buf2);
        exit(0);
    }

    for (i = 0; i < MAX_MESSAGES; i++) {
        fight_messages[i].a_type = 0;
        fight_messages[i].number_of_attacks = 0;
        fight_messages[i].msg = 0;
    }

    fscanf(f1, " %s \n", chk);
    while (*chk == 'M') {
        fscanf(f1, " %d\n", &type);

        for (i = 0; (i < MAX_MESSAGES) && (fight_messages[i].a_type != type) && (fight_messages[i].a_type); i++)
            ;
        if (i >= MAX_MESSAGES) {
            log("SYSERR: Too many combat messages.");
            exit(0);
        }

        CREATE(messages, struct message_type, 1);
        fight_messages[i].number_of_attacks++;
        fight_messages[i].a_type = type;
        messages->next = fight_messages[i].msg;
        fight_messages[i].msg = messages;

        sprintf(buf2, "combat message #%d in file '%s'", i, MESS_FILE);

        /* Messages on kill */
        /* Read and color as hit */
        messages->die_msg.attacker_msg = fread_string(f1, buf2);
        sprintf(buf, "$CH%s", messages->die_msg.attacker_msg);
        free(messages->die_msg.attacker_msg);
        messages->die_msg.attacker_msg = strdup(buf);

        /* Read and color as damage */
        messages->die_msg.victim_msg = fread_string(f1, buf2);
        sprintf(buf, "$CD%s", messages->die_msg.victim_msg);
        free(messages->die_msg.victim_msg);
        messages->die_msg.victim_msg = strdup(buf);

        messages->die_msg.room_msg = fread_string(f1, buf2);

        /* Messages on miss--none have color */
        messages->miss_msg.attacker_msg = fread_string(f1, buf2);
        messages->miss_msg.victim_msg = fread_string(f1, buf2);
        messages->miss_msg.room_msg = fread_string(f1, buf2);

        /* Messages on hit */
        /* Attacker gets hit color */
        messages->hit_msg.attacker_msg = fread_string(f1, buf2);
        sprintf(buf, "$CH%s", messages->hit_msg.attacker_msg);
        free(messages->hit_msg.attacker_msg);
        messages->hit_msg.attacker_msg = strdup(buf);

        /* Victim gets damage color */
        messages->hit_msg.victim_msg = fread_string(f1, buf2);
        sprintf(buf, "$CD%s", messages->hit_msg.victim_msg);
        free(messages->hit_msg.victim_msg);
        messages->hit_msg.victim_msg = strdup(buf);

        messages->hit_msg.room_msg = fread_string(f1, buf2);

        /* Messages when one hits oneself */
        messages->self_msg.attacker_msg = fread_string(f1, buf2);

        /* Victim (which is attacker) gets damage message */
        messages->self_msg.victim_msg = fread_string(f1, buf2);
        sprintf(buf, "$CD%s", messages->self_msg.victim_msg);
        free(messages->self_msg.victim_msg);
        messages->self_msg.victim_msg = strdup(buf);

        messages->self_msg.room_msg = fread_string(f1, buf2);
        fscanf(f1, " %s \n", chk);
    }
    fclose(f1);
}

void update_pos(struct char_data* victim)
{
    if (GET_HIT(victim) > 0) {
        if (victim->specials.fighting)
            GET_POS(victim) = POSITION_FIGHTING;
        else
            GET_POS(victim) = POSITION_STANDING;
    } else if (GET_HIT(victim) > 0)
        GET_POS(victim) = POSITION_STANDING;
    else if (GET_HIT(victim) <= -GET_CON(victim) / 2)
        GET_POS(victim) = POSITION_DEAD;
    else if (GET_HIT(victim) <= -GET_CON(victim) / 4)
        GET_POS(victim) = POSITION_INCAP;
    else
        GET_POS(victim) = POSITION_STUNNED;
}

/* start one char fighting another (yes, it is horrible, I know... )  */
void set_fighting(struct char_data* ch, struct char_data* vict)
{

    struct char_data* tmpch;

    if (ch == vict)
        return;

    if (ch->specials.fighting) {
        if (GET_POS(ch) > POSITION_STUNNED)
            GET_POS(ch) = POSITION_FIGHTING;
        return;
    }

    for (tmpch = combat_list; tmpch; tmpch = tmpch->next_fighting)
        if (tmpch == ch)
            break;

    if (!tmpch) {
        ch->next_fighting = combat_list;
        combat_list = ch;
    }

    ch->specials.fighting = vict;
    if (vict)
        GET_POS(ch) = POSITION_FIGHTING;

    stop_hiding(ch, TRUE);

    if (vict)
        stop_hiding(vict, FALSE);
}

/*
 * Remove a char from the list of fighting chars
 */
void stop_fighting(struct char_data* ch)
{
    struct char_data* tmp;
    struct char_data* oppon;

    oppon = ch->specials.fighting;

    /*
     * Go through the combat list.  If a char is found that is fighting ch,
     * ch is not directly fighting that_char, and ch CAN_SEE that_char,
     * tmp = that_char, otherwise (if no characters are found) tmp = NULL.
     */
    for (tmp = combat_list; tmp; tmp = tmp->next_fighting)
        if (tmp->specials.fighting == ch && tmp != oppon
            && CAN_SEE(ch, tmp))
            break;

    /*
     *If we didn't find a character above, and ch has not regenned energy or
     * has mental delay, and ch is alive (stunned or better), ch is no longer
     * fighting anyone, and we return.
     */
    if (!tmp && GET_ENERGY(ch) < ENE_TO_HIT || GET_MENTAL_DELAY(ch) && GET_POS(ch) > POSITION_STUNNED) {
        ch->specials.fighting = 0;
        GET_POS(ch) = POSITION_STANDING;
        update_pos(ch);
        return;
        /* Do not remove from the list yet */
    }

    /* here remain only cases with Energy >= Ene_to_hit already, or dead ones */
    if (!tmp) {
        if (ch == combat_next_dude)
            combat_next_dude = ch->next_fighting;

        if (combat_list == ch)
            combat_list = ch->next_fighting;
        else {
            for (tmp = combat_list; tmp && (tmp->next_fighting != ch);
                 tmp = tmp->next_fighting)
                ;

            if (!tmp) {
                ch->next_fighting = 0;
                ch->specials.fighting = 0;
                if (GET_POS(ch) == POSITION_FIGHTING)
                    GET_POS(ch) = POSITION_STANDING;
                update_pos(ch);
                return; /* he's not fighting */
            }
            tmp->next_fighting = ch->next_fighting;
        }

        ch->next_fighting = 0;
        ch->specials.fighting = 0;
        if (GET_POS(ch) == POSITION_FIGHTING) {
            GET_POS(ch) = POSITION_STANDING;
            update_pos(ch);
        }
    } else {
        ch->specials.fighting = tmp;
        send_to_char("You turn to face your next enemy.\n\r", ch);
    }
}

void stop_fighting_him(struct char_data* ch)
{

    struct char_data *tmp, *next_fighter;

    for (tmp = combat_list; tmp; tmp = next_fighter) {
        next_fighter = tmp->next_fighting;
        if (tmp->specials.fighting == ch)
            stop_fighting(tmp);
    }
}

/*
 * Checks to see if a hit breaks through a sanctuary
 * spell (or it didn't exist). Returns 0 if sanct remains
 */

int check_sanctuary(char_data* ch, char_data* victim)
{

    affected_type* aff;
    int return_value;

    /* aff->modifier is the victim's original alignment */

    aff = affected_by_spell(victim, SPELL_SANCTUARY);
    if (!aff)
        return 1;

    /* The strength of the spell is weakened with every hit */

    if (GET_ALIGNMENT(ch) < 0)
        aff->modifier = aff->modifier - (number(0, (-1 * GET_ALIGNMENT(ch)) / 2));
    else
        aff->modifier = aff->modifier - (number(0, GET_ALIGNMENT(ch)));

    if (aff->modifier < 0) {
        affect_from_char(victim, SPELL_SANCTUARY);
        send_to_char("Your sanctuary is broken!\n\r\n\r", ch);
        return_value = 1;
        if (ch != victim) {
            act("You break $N's sanctuary!", FALSE, ch, 0, victim, TO_CHAR);
            act("$n breaks $N's sanctuary!", FALSE, ch, 0, victim, TO_ROOM);
        } else
            act("$n's sanctuary is broken!", FALSE, ch, 0, 0, TO_ROOM);
    } else {
        act("You could not break $S sanctity.", FALSE, ch, 0, victim, TO_CHAR);
        act("$n's aura flickers.", FALSE, victim, 0, 0, TO_ROOM);
        if (ch->specials.fighting == victim)
            stop_fighting(ch);
        return_value = 0;
        ;
    }
    return return_value;
}

/*
 * This function gives to us our corpses "death_type"
 * effectively just returning a string.
 */
void get_corpse_desc(struct obj_data* corpse, struct char_data* ch,
    int attacktype)
{
#define BUF_LEN 32

    char condition[BUF_LEN];

    switch (attacktype) {
    case 43:
        strncpy(condition, "rancid", BUF_LEN - 1);
        break;
    case 25:
        strncpy(condition, "cleanly slain", BUF_LEN - 1);
        break;
    case 71:
        strncpy(condition, "slightly charred", BUF_LEN - 1);
        break;
    case 75:
        strncpy(condition, "slightly chilled", BUF_LEN - 1);
        break;
    case 78:
        strncpy(condition, "electrocuted", BUF_LEN - 1);
        break;
    case 81:
        strncpy(condition, "battered", BUF_LEN - 1);
        break;
    case 84:
        strncpy(condition, "darkened", BUF_LEN - 1);
        break;
    case 90:
        strncpy(condition, "burnt", BUF_LEN - 1);
        break;
    case 91:
        strncpy(condition, "badly burnt", BUF_LEN - 1);
        break;
    case 93:
        strncpy(condition, "frozen", BUF_LEN - 1);
        break;
    case 96:
        strncpy(condition, "burnt", BUF_LEN - 1);
        break;
    case 99:
        strncpy(condition, "almost unrecognisable", BUF_LEN - 1);
        break;
    case 100:
        strncpy(condition, "harrowing", BUF_LEN - 1);
        break;
    case 102:
        strncpy(condition, "agonised", BUF_LEN - 1);
        break;
    case 103:
        strncpy(condition, "tormented", BUF_LEN - 1);
        break;
    case 104:
        strncpy(condition, "shocked", BUF_LEN - 1);
        break;
    case 105:
        strncpy(condition, "horribly darkened", BUF_LEN - 1);
        break;
    case 106:
        strncpy(condition, "husk-like", BUF_LEN - 1);
        break;
    case 107:
        strncpy(condition, "tortured", BUF_LEN - 1);
        break;
    case 201:
        strncpy(condition, "badly burnt", BUF_LEN - 1);
        break;
    case 130:
        strncpy(condition, "badly beaten", BUF_LEN - 1);
        break;
    case 131:
        strncpy(condition, "crushed", BUF_LEN - 1);
        break;
    case 132:
        strncpy(condition, "pierced", BUF_LEN - 1);
        break;
    case 133:
        strncpy(condition, "slashed", BUF_LEN - 1);
        break;
    case 134:
        strncpy(condition, "stabbed", BUF_LEN - 1);
        break;
    case 135:
        strncpy(condition, "whipped", BUF_LEN - 1);
        break;
    case 136:
        strncpy(condition, "skewered", BUF_LEN - 1);
        break;
    case 137:
        strncpy(condition, "clawed", BUF_LEN - 1);
        break;
    case 138:
        strncpy(condition, "bitten", BUF_LEN - 1);
        break;
    case 139:
        strncpy(condition, "poisoned", BUF_LEN - 1);
        break;
    case 140:
        strncpy(condition, "hacked", BUF_LEN - 1);
        break;
    case 141:
        strncpy(condition, "disfigured", BUF_LEN - 1);
        break;
    case 142:
        strncpy(condition, "crushed", BUF_LEN - 1);
        break;
    case 143:
        strncpy(condition, "crushed", BUF_LEN - 1);
        break;
    case 98:
        strncpy(condition, "darkened", BUF_LEN - 1);
        break;
    case 55:
        strncpy(condition, "mutilated", BUF_LEN - 1);
        break;
    case 113:
        strncpy(condition, "gnawed", BUF_LEN - 1);
        break;
    case 115:
        strncpy(condition, "mauled", BUF_LEN - 1);
        break;
    case 124:
        strncpy(condition, "festering", BUF_LEN - 1);
        break;
    case 152:
        strncpy(condition, "badly beaten", BUF_LEN - 1);
        break;
    case 154:
        strncpy(condition, "battered", BUF_LEN - 1);
        break;
    case 156:
        strncpy(condition, "cleaved", BUF_LEN - 1);
        break;
    case 157:
        strncpy(condition, "trampled", BUF_LEN - 1);
        break;
    default:
        strncpy(condition, "silent", BUF_LEN - 1);
    }
    if (world[ch->in_room].sector_type == SECT_WATER_NOSWIM || world[ch->in_room].sector_type == SECT_WATER_SWIM || world[ch->in_room].sector_type == SECT_UNDERWATER)
        asprintf(&(corpse->description), "The %s corpse of %s is floating here.",
            condition,
            IS_NPC(ch) ? GET_NAME(ch) : pc_star_types[GET_RACE(ch)]);
    else
        asprintf(&(corpse->description), "The %s corpse of %s is lying here.",
            condition,
            IS_NPC(ch) ? GET_NAME(ch) : pc_star_types[GET_RACE(ch)]);

    asprintf(&(corpse->short_description), "the %s corpse of %s", condition,
        IS_NPC(ch) ? GET_NAME(ch) : pc_star_types[GET_RACE(ch)]);
}

/*
 * This fucntion creates and put money
 * into the corpse. It also prevents a gold
 * duplication loophole.
 * It also sets, after death, the characters
 * gold back to zero.
 * If the option passed in is zero we place the gold
 * in the object "object", if its not zero it all drops
 * on the ground.
 */
void move_gold(struct char_data* ch, struct obj_data* object, int option)
{
    struct obj_data* create_money(int amount);
    struct obj_data* money;

    if (GET_GOLD(ch) > 0) {
        if (option == 0)
            if (IS_NPC(ch) || (!IS_NPC(ch) && ch->desc)) {
                money = create_money(GET_GOLD(ch));
                obj_to_obj(money, object);
            } else {
                money = create_money(GET_GOLD(ch));
                obj_to_room(money, ch->in_room);
            }
    }
    GET_GOLD(ch) = 0;
}

/*
 * This function determines how long the corpse will last
 * before it decays. I've added an extra int value called
 * duration here. This is just for possible future re-use
 * of the function. Might be nice to have varying decay
 * times in certain circumstances.
 */
void corpse_decay_time(char_data* character, obj_data* corpse, int duration)
{
    if (duration > 0) {
        corpse->obj_flags.timer = duration;
    } else {
        if (IS_NPC(character)) {
            if (MOB_FLAGGED(character, MOB_ORC_FRIEND)) {
                corpse->obj_flags.timer = max_npc_corpse_time + 15;
            } else {
                corpse->obj_flags.timer = max_npc_corpse_time;
            }
        } else {
            corpse->obj_flags.timer = max_pc_corpse_time;
        }
    }
}

/*
 * This function gives a 4% random chance of an item disappearing
 * At most 1 item goes if your carrying under 25 items.
 * The first two objects that an NPC carries are immune to the
 * chance of being taken.
 */
void remove_random_item(struct char_data* ch, struct obj_data* corpse)
{
    int chance, i;
    struct obj_data* obj;

    if (!IS_NPC(ch) || IS_AFFECTED(ch, AFF_CHARM) || utils::is_guardian(*ch)) {
        chance = number(0, 24);
        i = 0;
        for (obj = corpse->contains; obj; obj = obj->next_content) {
            i++;

            if (i % 25 == chance) {
                if (!IS_SET(obj->obj_flags.extra_flags, ITEM_NORENT) && GET_ITEM_TYPE(obj) != ITEM_KEY && !IS_ARTIFACT(obj)) {
                    if (GET_ITEM_TYPE(obj) == ITEM_CONTAINER) {
                        i--;
                    } else {
                        extract_obj(obj);
                        break;
                    }
                }
            }
        }
    }
}

void remove_and_drop_object(char_data* character)
{
    obj_data* next_obj = NULL;
    for (obj_data* cur_obj = character->carrying; cur_obj; cur_obj = next_obj) {
        // Do this here since cur_obj->next_content becomes invalid after the item gets
        // moved to the room.
        next_obj = cur_obj->next_content;
        obj_to_room(cur_obj, character->in_room);
    }
    character->carrying = 0;

    for (int gear_index = 0; gear_index < MAX_WEAR; gear_index++) {
        if (character->equipment[gear_index]) {
            obj_data* item = unequip_char(character, gear_index);
            obj_to_room(item, character->in_room);
        }
    }
}

//============================================================================
// Moves an item from its current container to the corpse.
//============================================================================
void move_from_container_to_corpse(obj_data* corpse, obj_data* item)
{
    obj_from_obj(item);
    obj_to_obj(item, corpse);
}

//============================================================================
// Recursively parses the container passed in, moving any wearable or key items
// from the container into the corpse's inventory.
//============================================================================
void parse_container(obj_data* corpse, obj_data* container)
{
    obj_data* next_item = NULL;
    for (obj_data* item = container->contains; item; item = next_item) {
        next_item = item->next_content;
        if (item->is_wearable()) {
            // Move wearable items into base inventory.
            move_from_container_to_corpse(corpse, item);
        } else if (GET_ITEM_TYPE(item) == ITEM_KEY) {
            // Move keys into base inventory.
            move_from_container_to_corpse(corpse, item);
        } else if (GET_ITEM_TYPE(item) == ITEM_CONTAINER) {
            // Recursively parse containers so people can't hide anything.
            parse_container(corpse, item);
        }
    }
}

//============================================================================
// Moves all items that can be worn into corpse object, out of any containers
// that they were in.
//============================================================================
void move_wearables_to_corpse(obj_data* corpse)
{
    for (obj_data* item = corpse->contains; item; item = item->next_content) {
        if (GET_ITEM_TYPE(item) == ITEM_CONTAINER) {
            parse_container(corpse, item);
        }
    }
}

//============================================================================
// Makes a corpse from the character passed in.  Returns a pointer to the corpse.
//============================================================================
obj_data* make_physical_corpse(char_data* character, char_data* killer, int attack_type)
{
    obj_data* corpse = NULL;

    if (character->player.corpse_num) {
        corpse = read_object(character->player.corpse_num, VIRT);
    }

    if (!corpse) {
        CREATE(corpse, struct obj_data, 1);
        clear_object(corpse);
        corpse->next = object_list;
        object_list = corpse;
        corpse->item_number = NOWHERE;
        corpse->in_room = NOWHERE;
        corpse->name = str_dup("corpse");

        /* We call this function to retrieve our corpse "type"*/
        get_corpse_desc(corpse, character, attack_type);
        corpse->obj_flags.type_flag = ITEM_CONTAINER;
        corpse->obj_flags.wear_flags = ITEM_TAKE;
        corpse->obj_flags.value[0] = 0; /* You can't store stuff in a corpse */
        corpse->obj_flags.value[3] = 1; /* corpse identifier */
        corpse->obj_flags.weight = GET_WEIGHT(character) + IS_CARRYING_W(character);
        corpse->obj_flags.cost_per_day = 10000;
        corpse->obj_flags.level = GET_LEVEL(character);
    }

    /*
     * Next two lines are used to identify to who the corpse belongs to
     * In the case of the first, its for the purposes of beheading
     * In the case of the second value, it allows the owner
     * of the corpse to freely use "get all corpse".
     */
    int corpse_id = 0;
    if (IS_NPC(character)) {
        corpse_id = mob_index[character->nr].virt;
    } else {
        corpse_id = -GET_IDNUM(character);
    }

    corpse->obj_flags.value[4] = corpse_id;
    corpse->obj_flags.value[2] = corpse_id;

    corpse->obj_flags.butcher_item = character->specials.butcher_item;
    corpse->contains = character->carrying;
    /* this function sorts out money related matters */
    move_gold(character, corpse, 0);
    /*
     * Sets our corpse decay time
     * The third value passed in an integer value, if
     * this value is set to anything but 0 then that value
     * will become the corpse decay time.
     */
    corpse_decay_time(character, corpse, 0);

    /* removing our victims eq */
    for (int gear_index = 0; gear_index < MAX_WEAR; gear_index++) {
        if (character->equipment[gear_index]) {
            obj_data* item = unequip_char(character, gear_index);
            obj_to_obj(item, corpse);
        }
    }

    // If the character died to another player or poison, move all of their
    // wearable gear into their corpse and out of any containers they own.
    if (attack_type == SPELL_POISON || !IS_NPC(killer)) {
        move_wearables_to_corpse(corpse);
    }

    character->carrying = NULL;
    IS_CARRYING_N(character) = 0;
    IS_CARRYING_W(character) = 0;

    /* putting everything into the corpse */
    for (obj_data* obj = corpse->contains; obj; obj = obj->next_content) {
        obj->in_obj = corpse;
    }

    object_list_new_owner(corpse, NULL);
    // remove_random_item(character, corpse); // To re-enable item decay, remove the comment before this line.

    /* moving the corpse into the room of our players death */
    obj_to_room(corpse, character->in_room);

    return corpse;
}

void spirit_death(char_data* character)
{
    act("$n vanishes into thin air.", TRUE, character, 0, 0, TO_ROOM);
    remove_and_drop_object(character);
    move_gold(character, NULL, 1);
}

obj_data* make_corpse(char_data* character, char_data* killer, int attack_type)
{
    if (!IS_SHADOW(character)) {
        return make_physical_corpse(character, killer, attack_type);
    } else {
        spirit_death(character);
        return NULL;
    }
}

/* When ch kills victim */
void change_alignment(struct char_data* ch, struct char_data* victim)
{
    int align;
    extern int min_race_align[];
    extern int max_race_align[];

    if (ch == victim)
        return; // -has- happened :)

    align = 0; // initialization, if nothing below is true, no gain :)

    if (GET_ALIGNMENT(victim) == 0)
        return; // neutral mobs do not effect alignment

    // Supposedly "good" races
    if (RACE_GOOD(ch)) {

        // Good vs evil: how things should be...
        if (GET_ALIGNMENT(victim) < 0 && GET_ALIGNMENT(ch) >= 0)
            align = -1 * GET_ALIGNMENT(victim) / 25;

        // Evil whities kill evil - recover slowly -- especially of mob non-agg
        if ((GET_ALIGNMENT(victim) < 0) && GET_ALIGNMENT(ch) < 0) {
            if (MOB_FLAGGED(victim, MOB_AGGRESSIVE) || IS_AGGR_TO(victim, ch))
                align = MAX(0, (GET_ALIGNMENT(ch) / 2 - GET_ALIGNMENT(victim)) / 100);
            else
                align = MAX(0, (GET_ALIGNMENT(ch) / 2 - GET_ALIGNMENT(victim)) / 200);
        }
        // whities kill good - tut tut
        if (GET_ALIGNMENT(victim) > 0) {
            if (MOB_FLAGGED(victim, MOB_AGGRESSIVE) || IS_AGGR_TO(victim, ch)) {
                align = 0 - GET_ALIGNMENT(victim) / 10;
            } else {
                align = 0 - GET_ALIGNMENT(victim) / 2;
            }
        }

    } else {

        // "evil" races

        if (GET_ALIGNMENT(victim) < GET_ALIGNMENT(ch))
            align = 1;
        else
            align = 0 - (GET_ALIGNMENT(victim) / 25);
    }

    GET_ALIGNMENT(ch) += align;

    if (GET_ALIGNMENT(ch) < min_race_align[GET_RACE(ch)])
        GET_ALIGNMENT(ch) = min_race_align[GET_RACE(ch)];

    if (GET_ALIGNMENT(ch) > max_race_align[GET_RACE(ch)])
        GET_ALIGNMENT(ch) = max_race_align[GET_RACE(ch)];
}

void death_cry(struct char_data* ch)
{
    int door, was_in;

    /* Check to see if death cry is NULL. */
    if (ch->player.death_cry && strcmp(ch->player.death_cry, "(null)")) {
        strcpy(buf, ch->player.death_cry);
    } else {
        sprintf(buf, "Your blood freezes as you hear %s's death cry.",
            (IS_NPC(ch)) ? "$n" : GET_NAME(ch));
    }

    act(buf, FALSE, ch, 0, 0, TO_ROOM);
    was_in = ch->in_room;

    if (ch->player.death_cry2 && strcmp(ch->player.death_cry2, "(null)")) {
        strcpy(buf, ch->player.death_cry2);
    } else {
        sprintf(buf, "Your blood freezes as you hear %s's death cry.",
            (IS_NPC(ch)) ? "someone" : GET_NAME(ch));
    }
    for (door = 0; door < NUM_OF_DIRS; door++) {
        if (CAN_GO(ch, door)) {
            ch->in_room = world[was_in].dir_option[door]->to_room;
            act(buf, FALSE, ch, 0, 0, TO_ROOM);
            ch->in_room = was_in;
        }
    }
}

void raw_kill(char_data* dead_man, char_data* killer, int attack_type)
{
    waiting_type tmpwtl;

    if ((dead_man->delay.wait_value >= 0) && (dead_man->delay.cmd > 0)) {
        dead_man->delay.subcmd = -1;
        complete_delay(dead_man);
    }
    abort_delay(dead_man);

    if (dead_man->specials.fighting) {
        stop_fighting(dead_man);
    }

    if (special(dead_man, 0, "", SPECIAL_DEATH, &tmpwtl))
        return;

    if (IS_RIDING(dead_man))
        stop_riding(dead_man);

    if (IS_AFFECTED(dead_man, AFF_BASH))
        REMOVE_BIT(dead_man->specials.affected_by, AFF_BASH);

    while (dead_man->affected) {
        if (dead_man->affected->type == SPELL_FAME_WAR)
            remove_fame_war_bonuses(dead_man, dead_man->affected);

        affect_remove(dead_man, dead_man->affected);
    }
    death_cry(dead_man);
    obj_data* corpse = make_corpse(dead_man, killer, attack_type);

    // Let big brother know that the player died.
    game_rules::big_brother& bb_instance = game_rules::big_brother::instance();
    bb_instance.on_character_died(dead_man, killer, corpse);

    if (!IS_NPC(dead_man)) {
        int race = GET_RACE(dead_man);
        save_char(dead_man, r_mortal_start_room[race], 0);
        Crash_crashsave(dead_man);

        // The player was killed by another player (probably).
        // Restore them.
        // TODO(drelidan):  When we can track the origin of status effects, include that
        // here so we can determine if the 'poisoned' kill type was actually from a player.
        bool died_to_player = attack_type == SPELL_POISON || killer != NULL && !IS_NPC(killer);
        char_ability_data& cur_abils = dead_man->tmpabilities;
        char_ability_data& max_abils = dead_man->abilities;
        cur_abils = max_abils;

        if (died_to_player) {
            cur_abils.hit = max_abils.hit / 4;
            cur_abils.mana = 0;
        } else {
            // Set his stats to 2/3 their current value.
            SET_STR(dead_man, GET_STR(dead_man) * 2 / 3);
            GET_INT(dead_man) = GET_INT(dead_man) * 2 / 3;
            GET_WILL(dead_man) = GET_WILL(dead_man) * 2 / 3;
            GET_DEX(dead_man) = GET_DEX(dead_man) * 2 / 3;
            GET_CON(dead_man) = GET_CON(dead_man) * 2 / 3;
            GET_LEA(dead_man) = GET_LEA(dead_man) * 2 / 3;

            // Set hits to 1, moves and mana to 0.
            GET_HIT(dead_man) = 1;
            GET_MANA(dead_man) = 0;
            GET_MOVE(dead_man) = 0;
        }

        if (GET_LEVEL(dead_man) < LEVEL_IMMORT) {
            // Note, these are two different int arrays.
            dead_man->specials2.load_room = mortal_start_room[race];
            extract_char(dead_man, r_mortal_start_room[race]);
        } else {
            dead_man->specials2.load_room = immort_start_room;
            extract_char(dead_man, r_immort_start_room);
        }

        REMOVE_BIT(PLR_FLAGS(dead_man), PLR_WAS_KITTED);
    } else {
        extract_char(dead_man);
    }
}

/*
 * Simple enough function to check whether a character has
 * Death_ward up or not.
 */
int check_death_ward(struct char_data* ch)
{
    if (affected_by_spell(ch, SPELL_DEATH_WARD) != NULL) {
        affect_from_char(ch, SPELL_DEATH_WARD);
        affect_total(ch);

        if (GET_HIT(ch) <= 0 && GET_HIT(ch) + affected_by_spell(ch, SPELL_DEATH_WARD)->modifier > 0) {
            act("$n's whole body flashes with blinding light, "
                "and stirs back to life!",
                FALSE, ch, 0, 0, TO_ROOM);
            send_to_char("Your ward flares up and sinks into your body."
                         "You are alive!\n\r",
                ch);

            if (GET_HIT(ch) < 0) {
                GET_HIT(ch) += affected_by_spell(ch, SPELL_DEATH_WARD)->modifier;
                if (!GET_HIT(ch))
                    GET_HIT(ch) = 1;
            }
            stop_fighting_him(ch);
            stop_fighting(ch);
            update_pos(ch);
            return TRUE;
        } else {
            send_to_char("Your ward sends one short pulse through your veins, "
                         "then fades.\r\n",
                ch);
            return FALSE;
        }
    } else
        return FALSE;
}

void die(char_data* dead_man, char_data* killer, int attack_type)
{
    /* Character doesn't die if call_trigger returns FALSE */
    if (call_trigger(ON_DIE, dead_man, killer, 0) == FALSE) {
        stop_fighting_him(dead_man);
        stop_fighting(dead_man);
        update_pos(dead_man);
        return;
    }

    /* the following piece is moved here, might cause problems... */
    if (killer) {
        // Only grant gains for kills on NPCs and connected players.
        if (IS_NPC(dead_man) || dead_man->desc) {
            group_gain(killer, dead_man);
        }

        if (!IS_NPC(dead_man)) {
            const room_data& death_room = world[dead_man->in_room];
            const char* room_name = death_room.name;
            if (MOB_FLAGGED(killer, MOB_PET)) {
                vmudlog(BRF, "%s killed by %s (%s) at %s", GET_NAME(dead_man), GET_NAME(killer), GET_NAME(killer->master), room_name);
            } else {
                vmudlog(BRF, "%s killed by %s at %s", GET_NAME(dead_man), GET_NAME(killer), room_name);
            }
        }

        /*
         * If a mob kills a player, it no longer has memory of the player.
         *
         * Greeeeaaaat, so if a player dies to one mob with memory, every
         * other mob who remembers the player still has him on memory.  So
         * death isn't a clean start-over.
         */
        if (IS_NPC(killer) && !IS_NPC(dead_man) && IS_SET(killer->specials2.act, MOB_MEMORY)) {
            forget(killer, dead_man);
        }
    }

    /* A mob died.  Who cares. */
    if (IS_NPC(dead_man)) {
        raw_kill(dead_man, killer, attack_type);
        return;
    }

    /* log mobdeaths */
    if (IS_NPC(killer) && !(MOB_FLAGGED(killer, MOB_ORC_FRIEND) || MOB_FLAGGED(killer, MOB_PET))) {
        add_exploit_record(EXPLOIT_MOBDEATH, dead_man, GET_IDNUM(killer), GET_NAME(killer));
    }

    int base_xp_gain = -(dead_man->points.exp - 3000) / (dead_man->player.level + 2);

    /* A player died: DT/poison/incap/etc. death. */
    if (!killer) {
        gain_exp_regardless(dead_man, std::min(0, base_xp_gain / 10));
    } else {
        gain_exp_regardless(dead_man, std::min(0, base_xp_gain / 10));

        // TODO(drelidan):  I am unsure why this early out is here, but figure it out and potentially
        // fix it... 'cause this could have all sorts of problems.
        if (attack_type == SPELL_POISON) {
            add_exploit_record(EXPLOIT_POISON, dead_man, 0, NULL);

            // TODO(drelidan):  Only early-out if the dead man isn't in combat.  Otherwise continue
            // so that proper exploits are given out.
            if (dead_man->specials.fighting == NULL) {
                raw_kill(dead_man, killer, attack_type);
                return;
            }
        }

        // PK records are created regardless of death cause, but then early out if it's
        // all NPCs killing the character.  Heh...
        pkill_create(dead_man);
        add_exploit_record(EXPLOIT_PK, dead_man, 0, NULL); /* pk records to killers */

        /* add death records to dead player */
        /* Fingolfin: Jul 19: since we record mobdeaths earlier */
        if (killer && !IS_NPC(killer)) {
            add_exploit_record(EXPLOIT_DEATH, dead_man, 0, NULL);
        }

        if (IS_NPC(killer)) {
            // Only grant mob_death XP if the player died to a mob that is not controlled
            // by a player.
            if (!MOB_FLAGGED(killer, MOB_ORC_FRIEND) && !MOB_FLAGGED(killer, MOB_PET)) {
                gain_exp_regardless(dead_man, std::min(0, base_xp_gain));
            }
        }
    }

    GET_COND(dead_man, FULL) = 24;
    GET_COND(dead_man, THIRST) = 24;
    GET_COND(dead_man, DRUNK) = 0;

    raw_kill(dead_man, killer, attack_type);
}

int exp_with_modifiers(char_data* character, char_data* dead_man, int base_exp)
{
    int exp, age;

    /* Orcs get no experience for orc-friends */
    if (GET_RACE(character) == RACE_ORC && IS_NPC(dead_man) && MOB_FLAGGED(dead_man, MOB_ORC_FRIEND))
        return 0;

    base_exp /= std::max(GET_LEVEL(character) + 1, GET_LEVEL(dead_man) - 2);

    if (!IS_NPC(dead_man))
        return base_exp;

    if (GET_LEVEL(dead_man) + 6 < GET_LEVEL(character))
        base_exp = 6 * base_exp / (GET_LEVEL(character) - GET_LEVEL(dead_man));

    exp = base_exp;
    age = MOB_AGE_TICKS(dead_man, time(0)) * 40 / (GET_LEVEL(dead_man) + 20);

    if (GET_LEVEL(dead_man) > 5) {
        if (age < average_mob_life)
            exp = exp * (average_mob_life * 60 + age * 40) / (average_mob_life * 100);
        else
            exp = exp * (140 - 40 * average_mob_life / age) / 100;
    }

    if (MOB_FLAGGED(dead_man, MOB_AGGRESSIVE) || IS_AGGR_TO(dead_man, character))
        exp += base_exp / 5;

    if (MOB_FLAGGED(dead_man, MOB_FAST))
        exp += base_exp / 10;

    if (MOB_FLAGGED(dead_man, MOB_SWITCHING))
        exp += base_exp / 10;

    if (MOB_FLAGGED(dead_man, MOB_MEMORY))
        exp += base_exp / 20;

    if (dead_man->specials.default_pos < POSITION_STANDING)
        exp -= base_exp / 20;

    if (IS_GOOD(character) && IS_GOOD(dead_man))
        exp = exp * 2 / 3;

    if (MOB_FLAGGED(dead_man, MOB_SPEC) && dead_man->nr >= 0 && (mob_index[dead_man->nr].func || dead_man->specials.store_prog_number))
        exp += base_exp / 10;

    if (GET_DIFFICULTY(dead_man))
        exp = exp * GET_DIFFICULTY(dead_man) / 100;

    /* east exp bonus:  8 is the river */
    if (RACE_GOOD(character) && zone_table[world[character->in_room].zone].x > 8)
        exp += exp * std::min(zone_table[world[character->in_room].zone].x - 8, 5) * 3 / 100;

    /* TEMPORARY: */
    exp += 2 * exp / std::max(1, GET_LEVEL(character) - 1);

    /* Here we return the experience for killing a mob/player */
    return exp;
}

namespace {
bool master_gets_credit(const char_data* character)
{
    assert(character);
    assert(character->master);

    return (MOB_FLAGGED(character, MOB_ORC_FRIEND)
               || MOB_FLAGGED(character, MOB_PET)
               || MOB_FLAGGED(character, MOB_GUARDIAN))
        && character->in_room == character->master->in_room;
}
}

void group_gain(char_data* killer, char_data* dead_man)
{
    if (killer == nullptr || dead_man == nullptr)
        return;

    if (killer->in_room == NOWHERE)
        return;

    if (killer->in_room != dead_man->in_room)
        return;

    char_vector involved_killers;
    char_set player_killers;

    // This can happen from some effects.
    if (killer != dead_man) {
        // Ensure that the killer is involved.
        involved_killers.push_back(killer);
        if (utils::is_pc(*killer)) {
            player_killers.insert(killer);
        }
    }

    // Ensure that the victim's target is involved as well.
    if (dead_man->specials.fighting != nullptr) {
        involved_killers.push_back(dead_man->specials.fighting);
        if (utils::is_pc(*dead_man->specials.fighting)) {
            player_killers.insert(dead_man->specials.fighting);
        }
    }

    // Collect everyone in the room who is fighting the person that died.
    const room_data& death_room = world[dead_man->in_room];
    for (char_data* character = death_room.people; character; character = character->next_in_room) {
        if (character->specials.fighting == dead_man) {
            involved_killers.push_back(character);
            if (utils::is_pc(*character)) {
                player_killers.insert(character);
            }
        }
    }

    for (auto local_killer : involved_killers) {
        // Iterate over the group of each killer.
        if (local_killer->group) {
            for (auto groupee : *local_killer->group) {
                if (groupee->in_room == dead_man->in_room) {
                    if (utils::is_pc(*groupee)) {
                        player_killers.insert(groupee);
                    }
                }
            }
        }

        if (utils::is_npc(*local_killer)) {
            if (local_killer->master && master_gets_credit(local_killer)) {
                char_data* master = local_killer->master;
                if (utils::is_pc(*master)) {
                    player_killers.insert(master);
                }

                // Master is in a different group than its pet.  Add credit to the master's group too.
                if (master->group && (master->group != local_killer->group)) {
                    for (auto groupee : *master->group) {
                        if (groupee->in_room == dead_man->in_room) {
                            if (utils::is_pc(*groupee)) {
                                player_killers.insert(groupee);
                            }
                        }
                    }
                }
            }
        }
    }

    if (player_killers.empty())
        return;

    int perception_total = 0;
    int level_total = 0;
    for (auto iter = player_killers.begin(); iter != player_killers.end(); ++iter) {
        level_total += GET_LEVELB(*iter);
        perception_total += GET_PERCEPTION(*iter);
    }

    int share = GET_EXP(dead_man) / 10;
    int npc_level_malus = 0;
    int spirit_gain = 0;

    if (IS_NPC(dead_man)) {
        spirit_gain = GET_LEVEL(dead_man) * get_naked_perception(dead_man);
        if (IS_SET(MOB_FLAGS(dead_man), MOB_SHADOW)) {
            spirit_gain *= 3;
        }

        npc_level_malus = dead_man->specials.attacked_level;
        level_total += npc_level_malus;

        int num_killers = (int)player_killers.size();
        share = share * (num_killers + 1) / num_killers;
    } else {
        spirit_gain = utils::get_spirits(dead_man) * 100 / 4;
    }

    share = share / level_total;

    for (auto character : player_killers) {
        if (character->player.level >= LEVEL_IMMORT)
            continue;

        // Do spirit gain.
        if (perception_total > 0 && spirit_gain > 0) {
            int gain = spirit_gain * GET_PERCEPTION(character) / perception_total / 100;
            if (gain > 0 && (GET_ALIGNMENT(character) * GET_ALIGNMENT(dead_man) <= 0 || RACE_EVIL(character))) {
                if (utils::get_spirits(character) >= 99000) {
                    send_to_char("You have capped out on spirits.\n\r", character);
                } else {
                    vsend_to_char(character, "Your spirit increases by %d.\n\r", gain);
                    utils::add_spirits(character, gain);
                }
            }
        }

        int capped_level = GET_LEVELB(character);
        int group_bonus = std::min(share * capped_level / 2, (level_total - npc_level_malus - capped_level) * share / 4);
        int tmp = exp_with_modifiers(character, dead_man, share * capped_level + group_bonus);

        vsend_to_char(character, "You receive your share of experience -- %d points.\r\n", tmp);
        gain_exp(character, tmp);
        change_alignment(character, dead_man);

        // Allow wild fighting to handle this kill.
        player_spec::wild_fighting_handler wild_fighting(character);
        wild_fighting.on_unit_killed(dead_man);

        /* save only 10% of the time to avoid lag in big groups */
        if (number(0, 9) == 0) {
            save_char(character, NOWHERE, 1);
        }
    }
}

char replace_string_buf[500];

char* replace_string(char* str, char* weapon_singular, char* weapon_plural, char* bodypart)
{
    char* buf;
    char* cp;

    buf = replace_string_buf;
    cp = buf;

    for (; *str; str++) {
        if (*str == '#') {
            switch (*(++str)) {
            case 'W':
                for (; *weapon_plural; *(cp++) = *(weapon_plural++))
                    continue;
                break;
            case 'w':
                for (; *weapon_singular; *(cp++) = *(weapon_singular++))
                    continue;
                break;
            case 'b':
                if (bodypart[0]) {
                    *(cp++) = ' ';
                    for (; *bodypart; *(cp++) = *(bodypart++))
                        continue;
                }
                break;
            case 's':
                if (bodypart[0]) {
                    *(cp++) = '\'';
                    *(cp++) = 's';
                }
                break;
            case 'r':
                if (bodypart[0])
                    *(cp++) = 'r';
                break;
            default:
                *(cp++) = '#';
                break;
            }
        } else
            *(cp++) = *str;

        *cp = 0;
    }

    return buf;
}

/* use #w for singular (i.e. "slash") and #W for plural (i.e. "slashes") */
static struct dam_weapon_type {
    char* to_room;
    char* to_char;
    char* to_victim;
} dam_weapons[] = {
    { "$n misses $N#s#b with $s #w.", /* 0: 0     */
        "$CHYou miss $N#s#b with your #w.",
        "$CD$n misses you#r#b with $s #w." },
    { "$n scratches $N#s#b with $s #w.", /* 1: 1  */
        "$CHYou scratch $N#s#b as you #w $M.",
        "$CD$n scratches you#r#b as $e #W you." },
    { "$n barely #W $N#s#b.", /* 2: 2..3 */
        "$CHYou barely #w $N#s#b.",
        "$CD$n barely #W you#r#b." },
    { "$n lightly #W $N#s#b.", /* 3: 4..6  */
        "$CHYou lightly #w $N#s#b.",
        "$CD$n lightly #W you#r#b." },
    { "$n #W $N#s#b.", /* 4: 7..11  */
        "$CHYou #w $N#s#b.",
        "$CD$n #W you#r#b." },
    { "$n #W $N#s#b hard.", /* 5: 12..17  */
        "$CHYou #w $N#s#b hard.",
        "$CD$n #W you#r#b hard." },
    { "$n #W $N#s#b very hard.", /* 6: 18..24  */
        "$CHYou #w $N#s#b very hard.",
        "$CD$n #W you#r#b very hard." },
    { "$n #W $N#s#b extremely hard.", /* 7: 25..33  */
        "$CHYou #w $N#s#b extremely hard.",
        "$CD$n #W you#r#b extremely hard." },
    { "$n deeply wounds $N#s#b with $s #w.", /* 8: 34..60  */
        "$CHYou deeply wound $N#s#b with your #w.",
        "$CD$n deeply wounds you#r#b with $s #w." },
    { "$n severely wounds $N#s#b with $s #w!", /* 9: 61..89 */
        "$CHYou severely wound $N#s#b with your #w!",
        "$CD$n severely wounds you#r#b with $s #w!" },
    { "$n MUTILATES $N#s#b with $s deadly #w!!", /* 90: > 120  */
        "$CHYou MUTILATE $N#s#b with your deadly #w!!",
        "$CD$n MUTILATES you#r#b with $s deadly #w!!" }
};

//============================================================================
// Gets the damage message index for a given damage value.
//============================================================================
int get_damage_message_number(int damage)
{
    int message_number = 0;

    if (damage == 0)
        message_number = 0;
    else if (damage <= 1)
        message_number = 1;
    else if (damage <= 3)
        message_number = 2;
    else if (damage <= 6)
        message_number = 3;
    else if (damage <= 11)
        message_number = 4;
    else if (damage <= 17)
        message_number = 5;
    else if (damage <= 24)
        message_number = 6;
    else if (damage <= 33)
        message_number = 7;
    else if (damage <= 60)
        message_number = 8;
    else if (damage <= 90)
        message_number = 9;
    else
        message_number = 10;

    return message_number;
}

const attack_hit_type& get_hit_text(int w_type)
{
    // That is, instead of using the TYPE_ which starts at 140+,
    // see spells.h for details, we use w_type range from 0 to 13 or so,
    // whatever number of different weapon types we have.
    w_type -= TYPE_HIT; /* Change to base of table with text */

    return attack_hit_text[w_type];
}

void dam_message(int damage, char_data* attacker, char_data* victim, int w_type, char* bodypart)
{
    obj_data* wield = attacker->equipment[WIELD];

    int msgnum = get_damage_message_number(damage);
    const dam_weapon_type& weap = dam_weapons[msgnum];
    const attack_hit_type& text = get_hit_text(w_type);

    /* damage message to onlookers.  see note on replace_string */
    char* buf = replace_string(weap.to_room, text.singular, text.plural, bodypart);
    act(buf, FALSE, attacker, wield, victim, TO_NOTVICT, FALSE);

    /* damage message to attacker */
    buf = replace_string(weap.to_char, text.singular, text.plural, bodypart);
    act(buf, FALSE, attacker, wield, victim, TO_CHAR, FALSE);

    /* damage message to victim */
    buf = replace_string(weap.to_victim, text.singular, text.plural, bodypart);
    act(buf, FALSE, attacker, wield, victim, TO_VICT, FALSE);
}

void generate_damage_message(char_data* attacker, char_data* victim, int damage, int attacktype, int hit_location)
{
    int nr;
    int i, j;
    struct message_type* messages;

    const race_bodypart_data& part = bodyparts[GET_BODYTYPE(victim)];
    char* body_part = part.parts[hit_location];
    if (IS_PHYSICAL(attacktype)) {
        if (!attacker->equipment[WIELD]) {
            if (GET_RACE(attacker) == RACE_BEORNING) {
                dam_message(damage, attacker, victim, TYPE_CLAW, body_part);
            } else {
                dam_message(damage, attacker, victim, TYPE_HIT, body_part);
            }
        } else {
            dam_message(damage, attacker, victim, attacktype, body_part);
        }
    } else if (attacktype == SKILL_ARCHERY) {
        // Add archery damage message here.
        // TODO(drelidan):  There's probably a better way to do this... but laziness!  Woohoo!
        obj_data* wield = attacker->equipment[WIELD];

        int msg_num = get_damage_message_number(damage);
        const dam_weapon_type& weap = dam_weapons[msg_num];
        char* singular = "shot";
        char* plural = "shot";
        if (damage < 34) {
            singular = "shoot";
            plural = "shoots";
        }

        /* damage message to onlookers.  see note on replace_string */
        char* buf = replace_string(weap.to_room, singular, plural, body_part);
        act(buf, FALSE, attacker, wield, victim, TO_NOTVICT, FALSE);

        /* damage message to attacker */
        buf = replace_string(weap.to_char, singular, plural, body_part);
        act(buf, FALSE, attacker, wield, victim, TO_CHAR, FALSE);

        /* damage message to victim */
        buf = replace_string(weap.to_victim, singular, plural, body_part);
        act(buf, FALSE, attacker, wield, victim, TO_VICT, FALSE);

    } else {
        for (i = 0; i < MAX_MESSAGES; i++) {
            if (fight_messages[i].a_type == attacktype) {
                nr = dice(1, fight_messages[i].number_of_attacks);

                for (j = 1, messages = fight_messages[i].msg; j < nr && messages; j++) {
                    messages = messages->next;
                }

                if (!messages) {
                    dam_message(damage, attacker, victim, TYPE_HIT, body_part);
                    vmudlog(BRF, "No fight_message for attacktype %d, i=%d\n", attacktype, i);
                    break;
                }

                if (victim == attacker) {
                    act(messages->self_msg.victim_msg, FALSE, attacker, attacker->equipment[WIELD], victim, TO_CHAR);
                    act(messages->self_msg.room_msg, FALSE, attacker, attacker->equipment[WIELD], victim, TO_NOTVICT);
                } else if (damage != 0) {
                    if (GET_POS(victim) == POSITION_DEAD) {
                        act(messages->die_msg.attacker_msg, FALSE, attacker, attacker->equipment[WIELD], victim, TO_CHAR);
                        act(messages->die_msg.victim_msg, FALSE, attacker, attacker->equipment[WIELD], victim, TO_VICT);
                        act(messages->die_msg.room_msg, FALSE, attacker, attacker->equipment[WIELD], victim, TO_NOTVICT);
                    } else {
                        act(messages->hit_msg.attacker_msg, FALSE, attacker, attacker->equipment[WIELD], victim, TO_CHAR);
                        act(messages->hit_msg.victim_msg, FALSE, attacker, attacker->equipment[WIELD], victim, TO_VICT);
                        act(messages->hit_msg.room_msg, FALSE, attacker, attacker->equipment[WIELD], victim, TO_NOTVICT);
                    }
                } else { /* Dam == 0 */
                    act(messages->miss_msg.attacker_msg, FALSE, attacker, attacker->equipment[WIELD], victim, TO_CHAR);
                    act(messages->miss_msg.victim_msg, FALSE, attacker, attacker->equipment[WIELD], victim, TO_VICT);
                    act(messages->miss_msg.room_msg, FALSE, attacker, attacker->equipment[WIELD], victim, TO_NOTVICT);
                }
            }
        }
    }
}

// TODO:(maga) Remove modifier mod
int maul_damage_reduction(char_data* ch, int damage)
{
    using namespace utils;
    double damage_reduction = 0.10;
    double maul_db = 2.00;
    double dur = 2;

    if (utils::get_specialization(*ch) == game_types::PS_Defender) {
        maul_db = 1.25;
        dur = 6;
    }
    affected_type* maul_reduction = affected_by_spell(ch, SKILL_MAUL);

    if (maul_reduction && maul_reduction->location == APPLY_MAUL) {
        damage_reduction += ((maul_reduction->duration * 10 / 2) / maul_db) * 0.001;
    }

    affected_type* defend_affect = affected_by_spell(ch, SKILL_DEFEND);
    if (defend_affect) {
        dur += 3;
    }

    damage_reduction = (double)damage * damage_reduction;
    damage -= (int)damage_reduction;

    if (maul_reduction && maul_reduction->location == APPLY_MAUL && maul_reduction->duration > 1) {
        int duration = maul_reduction->duration;
        if (!(((int)(damage_reduction / dur) > duration))) {
            maul_reduction->duration -= (int)(damage_reduction / dur);
        }
    }
    return damage = std::max(damage, 1);
}

/*
 * damage now modified to return int - 1 if the victim was
 * killed, 0 if not.
 */
int damage(char_data* attacker, char_data* victim, int dam, int attacktype, int hit_location)
{
    struct affected_type* aff;
    int i, tmp, tmp1;
    struct waiting_type tmpwtl;
    extern void record_spell_damage(struct char_data*, struct char_data*,
        int, int);

    game_rules::big_brother& bb_instance = game_rules::big_brother::instance();

    // Don't allow damage to affect invalid targets.
    if (!bb_instance.is_target_valid(attacker, victim, attacktype)) {
        send_to_char("You feel the Gods looking down upon you, and protecting your target.  Your hand is stayed.\r\n", attacker);
        return 0;
    }

    if (!victim)
        return 0;

    if (!attacker)
        attacker = victim; /* emergency fix */

    if (GET_POS(victim) <= POSITION_DEAD) {
        log("SYSERR: Attempt to damage a corpse.");
        return 0; /* -je, 7/7/92 */
    }

    if (GET_POS(victim) <= POSITION_DEAD) {
        send_to_char("Don't mess with the dead body!\r\n"
                     "Notify imps of this bug!\n\r",
            attacker);
        return 0;
    }

    /*
     * Check hallucination.  Only check this if it's a physical hit type (to
     * avoid checking spells twice).
     */
    if (IS_PHYSICAL(attacktype) && !check_hallucinate(attacker, victim))
        return 0;

    /* Here we are checking to see if the victim is a Beorning because
           they have a natural damage reduction on physical weapons only. Spell
           damage is still at it's full amount. */
    if (GET_RACE(victim) == RACE_BEORNING && dam > 1) {
        dam = maul_damage_reduction(victim, dam);
    }

    affected_type* pkaff = affected_by_spell(victim, SPELL_FAME_WAR);

    if (!IS_NPC(attacker) && pkaff && victim->player.ranking < 4 && victim->player.ranking != PKILL_UNRANKED) {
        dam += dam * ((15 / victim->player.ranking) / 100);
    }

    /* Call special procs on damage */
    if (victim->specials.fighting != attacker) {
        tmpwtl.targ1.ptr.ch = victim;
        tmpwtl.targ1.type = TARGET_CHAR;
        tmpwtl.targ2.ptr.other = 0;
        tmpwtl.targ2.type = TARGET_NONE;
        i = special(attacker, 0, "", SPECIAL_DAMAGE, &tmpwtl);
        if (i) {
            if (attacker->specials.fighting == victim)
                stop_fighting(attacker);
            return 0;
        }
    }

    if (IS_SHADOW(victim)) {
        do_pass_through(attacker, victim, 0);
        return 0;
    }

    if (!check_sanctuary(attacker, victim))
        return 0;

    utils::on_attacked_character(attacker, victim);

    /* If sanctuary wasn't broken, victim is NULL */
    if (!victim) {
        set_fighting(attacker, 0);
        return 0;
    }

    if (victim != attacker) {
        if (GET_POS(attacker) > POSITION_STUNNED) {
            if (!(attacker->specials.fighting))
                set_fighting(attacker, victim);

            if (IS_RIDING(attacker) && (attacker->mount_data.mount == victim))
                stop_riding(attacker);
            if (IS_RIDING(victim) && (victim->mount_data.mount == attacker))
                stop_riding(victim);

            if (IS_NPC(attacker) && IS_NPC(victim) && victim->master && !number(0, 10) && IS_AFFECTED(victim, AFF_CHARM) && (victim->master->in_room == attacker->in_room)) {
                if (attacker->specials.fighting)
                    stop_fighting(attacker);
                hit(attacker, victim->master, TYPE_UNDEFINED);
                return 0;
            }
        }

        if (GET_POS(victim) > POSITION_STUNNED) {
            if (!(victim->specials.fighting))
                set_fighting(victim, attacker);
            if (IS_NPC(victim) && IS_SET(victim->specials2.act, MOB_MEMORY) && !IS_NPC(attacker) && (GET_LEVEL(attacker) < LEVEL_IMMORT))
                remember(victim, attacker);
            GET_POS(victim) = POSITION_FIGHTING;
        }
    }

    if (victim->master == attacker) {
        stop_follower(victim, FOLLOW_MOVE);
    }

    if (victim != attacker) {
        // If a group member was attacked by the leader, remove the attacked member
        // from the group.
        char_data* victim_leader = victim->get_group_leader();
        if (victim_leader == attacker) {
            remove_character_from_group(victim, victim_leader);
        }

        // If you members of the same group attack each other, remove both members from
        // the group.
        if (victim_leader && victim_leader == attacker->get_group_leader()) {
            remove_character_from_group(attacker, victim_leader);
            remove_character_from_group(victim, victim_leader);
        }
    }

    if (IS_AFFECTED(attacker, AFF_SANCTUARY))
        appear(attacker);

    /*
     * XXX: This should really use stop_hiding.  But stop_hiding
     * needs modifications first.
     */
    if (IS_AFFECTED(attacker, AFF_HIDE)) {
        act("$n steps out of $s cover.", TRUE, attacker, 0, 0, TO_ROOM);
        send_to_char("You step out of your cover.\n\r", attacker);
        REMOVE_BIT(attacker->specials.affected_by, AFF_HIDE);
    }
    player_spec::battle_mage_handler battle_mage_handler(victim);
    /* Remove delay if wait_wheel */
    if (dam > 0) {
        if (IS_AFFECTED(victim, AFF_WAITWHEEL) && GET_WAIT_PRIORITY(victim) <= 40)
            if (battle_mage_handler.does_spell_get_interrupted())
                break_spell(victim);
    }

    if (IS_NPC(victim) && victim->specials.attacked_level < GET_LEVELB(attacker))
        victim->specials.attacked_level = GET_LEVELB(attacker);

    /* 33% chance to resist with protection physical*/
    tmp = check_resistances(victim, attacktype);
    if (number(0, 2) == 0 && IS_PHYSICAL(attacktype))
        tmp = 0;

    if (tmp > 0) {
        send_to_char("You resist a lot.\n\r", victim);
        act("$n resists a lot.\n\r",
            TRUE, victim, 0, 0, TO_ROOM);
        dam = dam * 2 / 3;
    }

    if (tmp < 0) {
        send_to_char("You feel it a lot.\n\r", victim);
        dam = dam * 3 / 2;
    }

    if (dam > 200 && attacktype != SKILL_AMBUSH) {
        vmudlog(NRM, "Damage overflow for %s (%dhit).", GET_NAME(attacker), dam);
        dam = std::min(dam, 200);
    }
    dam = std::max(dam, 0);

    /*
     * Seether's shield:
     * If the victim has the shield spell up, we're going to take
     * some of his mana before his hitpoints, if he makes the roll.
     * It will only drain mana to 1 (not 0) and will do at -least-
     * 1 hp of damage; we allow for the hit to take at most all of
     * their mana minus one.
     *   if(affected_by_spell(victim, SPELL_SHIELD) &&
     *      attacktype != SKILL_AMBUSH) {
     *     if(number(1, 101) < (29 + GET_PROF_LEVEL(PROF_MAGE, victim) * 2)) {
     *       int shield_prof, mana_remove = 0;
     *
     *       shield_prof = number(1, GET_PROF_LEVEL(PROF_MAGE, victim)) +
     *                     number(1, GET_MANA(victim) / 3);
     *       mana_remove = MIN(MIN(dam - 1, shield_prof), GET_MANA(victim));
     *       GET_MANA(victim) -= mana_remove;
     *       dam -= mana_remove;
     *     }
     *   }
     *
     *
     * Fingol's shield:
     * We should compare the two - see how they work in the game.  If
     * the victim has shield spell up it absorbs up to 40% of damage
     * remaining by now (after other abs etc) at a cost of mana
     * dependent on mage level.  My simple version: 20 / mage_level
     * cost in mana.
     *
     * Dim's shield:
     *   mana cost = level * 20 / mage_level +
     *               number(0, mage_level) > i * 20 % mage_level ? 1 : 0;
     *
     * 01/01/00: now we check that attack is not bash, else damage will
     * be absorbed and the bash message won't be given.
     */
    if (affected_by_spell(victim, SPELL_SHIELD) && attacktype != SKILL_AMBUSH && attacktype != SKILL_BASH) {
        i = (dam * 2 + 4) / 5;
        aff = affected_by_spell(victim, SPELL_SHIELD);

        /* cost of mana to absorb the hit */
        tmp1 = (i * 15) / GET_PROF_LEVEL(PROF_MAGE, victim);
        if (number(0, GET_PROF_LEVEL(PROF_MAGE, victim)) > i * 15 % GET_PROF_LEVEL(PROF_MAGE, victim))
            tmp1++;

        /* max hits for remaining mana */
        if (tmp1 > GET_MANA(victim)) {
            i = i * GET_MANA(victim) / tmp1;
            GET_MANA(victim) = 0;
        } else {
            GET_MANA(victim) -= tmp1;
        }
        dam -= i;
        if (GET_MANA(victim) == 0) {
            affect_from_char(victim, SPELL_SHIELD);
        } else if (aff->duration > 2) {
            aff->duration = 2;
        }
    }

    if (!call_trigger(ON_DAMAGE, victim, attacker, 0)) {
        update_pos(victim);
        update_pos(attacker);
        return 0;
    }

    /* Spell damage logging */
    record_spell_damage(attacker, victim, attacktype, dam);

    // Create wild fighting data to handle rage effects.
    player_spec::wild_fighting_handler wild_fighting(victim);

    GET_HIT(victim) -= dam;

    // if victim is a berserker, let people know that rage may be entered.
    wild_fighting.update_health(victim->tmpabilities.hit);

    if (utils::is_pc(*attacker) || (attacker->master && utils::is_affected_by(*attacker, AFF_CHARM))) {
        attacker->damage_details.add_damage(attacktype, dam);
    }

    if (attacker->group) {
        attacker->group->track_damage(attacker, dam);
    }

    if (attacker != victim) {
        gain_exp(attacker, (1 + GET_LEVEL(victim)) * std::min(20 + GET_LEVEL(attacker) * 2, dam) / (1 + GET_LEVEL(attacker)));
    }

    /* hack for PC vs. PC fights to skip the incap step */
    if (GET_HIT(victim) < 0 && !IS_NPC(attacker)) {
        GET_HIT(victim) = std::min(GET_HIT(victim), -20);
    }

    update_pos(victim);
    update_pos(attacker);

    generate_damage_message(attacker, victim, dam, attacktype, hit_location);

    if (dam > 0) {
        check_break_prep(victim);
        check_break_prep(attacker);
    }

    /* Use send_to_char -- act() doesn't send message if you are DEAD. */
    switch (GET_POS(victim)) {
    case POSITION_INCAP:
        act("$n is incapacitated and will die, if not aided.",
            TRUE, victim, 0, 0, TO_ROOM);
        send_to_char("You are incapacitated and will slowly die, "
                     "if not aided.\n\r",
            victim);
        break;
    case POSITION_STUNNED:
        act("$n is stunned, but will probably regain consciousness again.",
            TRUE, victim, 0, 0, TO_ROOM);
        send_to_char("You're stunned, but will probably regain "
                     "consciousness again.\n\r",
            victim);
        break;
    case POSITION_DEAD:
        act("$n is dead!  R.I.P.", TRUE, victim, 0, 0, TO_ROOM);
        send_to_char("You are dead!  Sorry...\n\r", victim);
        break;
    default: /* >= POSITION SLEEPING */
        /*
         * This code gets duplicated in hit().  Make sure to check
         * there if you change anything here.
         */
        if (dam > (GET_MAX_HIT(victim) / 5))
            act("That really did HURT!", FALSE, victim, 0, 0, TO_CHAR);

        if (GET_HIT(victim) < (GET_MAX_HIT(victim) / 5)) {
            act("You wish that your wounds would stop BLEEDING so much!",
                FALSE, victim, 0, 0, TO_CHAR);
            if (IS_NPC(victim)) {
                if (IS_SET(victim->specials2.act, MOB_WIMPY))
                    if (GET_POSITION(victim) > POSITION_SLEEPING)
                        do_flee(victim, "", 0, 0, 0);
            }
        }

        if (!IS_NPC(victim) && WIMP_LEVEL(victim) && victim != attacker && GET_HIT(victim) < WIMP_LEVEL(victim))
            if (GET_POSITION(victim) > POSITION_SLEEPING && GET_TACTICS(victim) != TACTICS_BERSERK) {
                send_to_char("You wimp out, and attempt to flee!\n\r",
                    victim);
                do_flee(victim, "", 0, 0, 0);
            }
    }

    if (!IS_NPC(victim) && !(victim->desc && victim->desc->descriptor) && (victim->specials.fighting) && GET_POS(victim) > POSITION_INCAP) {
        do_flee(victim, "", 0, 0, 0);
        victim->specials.was_in_room = victim->in_room;
    }

    if (!AWAKE(victim))
        if (victim->specials.fighting)
            stop_fighting(victim);

    if (GET_POS(victim) == POSITION_DEAD) {
        // Redirect the attacker as the pet's master if the master is in the same room as the pet.
        if (IS_NPC(attacker)) {
            if (attacker->master && (MOB_FLAGGED(attacker, MOB_PET) || MOB_FLAGGED(attacker, MOB_ORC_FRIEND)) && attacker->master->in_room == attacker->in_room) {
                attacker = attacker->master;
            }
        }

        die(victim, attacker, attacktype);
        return 1;
    } else {
        return 0;
    }
}

bool does_victim_save_on_weapon_poison(struct char_data* victim, struct obj_data* weapon)
{
    int multipler = weapon->obj_flags.get_poison_multipler();
    int strength = weapon->obj_flags.get_poison_strength();
    int offense = (((strength * 2) * 8) * multipler) / 100;
    int defense = (GET_CON(victim) * 5) + (GET_WILLPOWER(victim) * 3) + (GET_RACE(victim) == RACE_WOOD ? 30 : 0);
    return number(offense / 3, offense) < number(defense / 2, defense) ? false : true;
}

/*UPDATE* integreate parry message with other messages */
void do_parry(struct char_data* ch, struct char_data* victim, int type)
{
    act("$N deflects $n's attack.",
        FALSE, ch, 0, victim, TO_NOTVICT, TRUE);
    act("$N deflects your attack.",
        FALSE, ch, 0, victim, TO_CHAR, TRUE);
    act("You deflect $n's attack.",
        FALSE, ch, 0, victim, TO_VICT, TRUE);
}

void do_dodge(struct char_data* ch, struct char_data* victim, int type)
{
    act("$N dodges $n's attack.",
        FALSE, ch, 0, victim, TO_NOTVICT, TRUE);
    act("$N dodges your attack.",
        FALSE, ch, 0, victim, TO_CHAR, TRUE);
    act("You dodge $n's attack.",
        FALSE, ch, 0, victim, TO_VICT, TRUE);
}

void do_evade(struct char_data* ch, struct char_data* victim, int type)
{
    act("$N distracts $n into missing $M.",
        FALSE, ch, 0, victim, TO_NOTVICT, TRUE);
    act("$N is not where you attack $M.",
        FALSE, ch, 0, victim, TO_CHAR, TRUE);
    act("You evade $n's attack.",
        FALSE, ch, 0, victim, TO_VICT, TRUE);
}

void do_pass_through(struct char_data* ch, struct char_data* victim, int type)
{
    act("$n's attack passes clean through $N.",
        FALSE, ch, 0, victim, TO_NOTVICT, FALSE);
    act("Your attack passes clean through $N.",
        FALSE, ch, 0, victim, TO_CHAR, FALSE);
    act("$n's attack passes clean through you.",
        FALSE, ch, 0, victim, TO_VICT, FALSE);
}

void do_riposte(struct char_data* ch, struct char_data* victim)
{
    act("$n's riposte catches $N off guard.",
        FALSE, ch, 0, victim, TO_NOTVICT, FALSE);
    act("Your riposte catches $N off guard.",
        FALSE, ch, 0, victim, TO_CHAR, FALSE);
    act("$n's riposte catches you off guard.",
        FALSE, ch, 0, victim, TO_VICT, FALSE);
}

int weapon_hit_type(int weapon_type)
{
    int w_type;
    switch (weapon_type) {
    case 0:
    case 1:
    case 2:
        w_type = TYPE_WHIP; /* whip */
        break;
    case 3:
    case 4:
        w_type = TYPE_SLASH;
        break;
    case 5:
        w_type = TYPE_FLAIL; /* flail */
        break;
    case 6:
        w_type = TYPE_CRUSH;
        break;
    case 7:
        w_type = TYPE_BLUDGEON;
        break;
    case 8:
    case 9:
        w_type = TYPE_CLEAVE;
        break;
    case 10:
        w_type = TYPE_SPEARS;
        break;
    case 11:
        w_type = TYPE_PIERCE;
        break;
    case 12:
        w_type = TYPE_SMITE;
        break;
    case 13:
        w_type = TYPE_WHIP;
    case 14:
        w_type = TYPE_BLUDGEON;
        break;
    default:
        w_type = TYPE_HIT;
        break;
    }
    return w_type;
}

/*
 * 'ch' is hitting 'victim'--does 'ch' find weakness?  Return
 * the correct amount of damage dealt after the check; if 'ch'
 * finds weakness, it will be more, otherwise it will be the
 * same as the amount of damage passed.
 */
int check_find_weakness(struct char_data* ch, struct char_data* victim, int dam)
{
    int prob;

    if (!IS_NPC(ch)) {
        prob = GET_RAW_SKILL(ch, SKILL_EXTRA_DAMAGE) / 3 * GET_PROF_LEVEL(PROF_WARRIOR, ch) / 30;
        if (GET_PROF_LEVEL(PROF_WARRIOR, ch) > 30)
            prob += GET_PROF_LEVEL(PROF_WARRIOR, ch) - 30;

        if (prob > number(0, 99)) {
            dam += dam / 2;

            act("You discover a weakness in $N's defense!",
                TRUE, ch, 0, victim, TO_CHAR);
            act("$n discovers a weakness in $N's defense!",
                TRUE, ch, 0, victim, TO_NOTVICT);
            act("$n discovers a weakness in your defense!",
                TRUE, ch, 0, victim, TO_VICT);
        }
    }

    return dam;
}

/*
 * Basically, if a character is wielding a one-handed weapon
 * with both hands, we cause them to lose energy, and thus slow
 * them down.
 *
 * NOTE: A weapon is considered one handed if it has a bulk of
 * 4 or less.
 */
void check_grip(struct char_data* ch, struct obj_data* wielded)
{
    int bulk;

    if (wielded != NULL)
        bulk = wielded->obj_flags.value[2];
    else
        bulk = 0;

    if (bulk <= 4 && IS_AFFECTED(ch, AFF_TWOHANDED) && wielded)
        if (number(0, 99) < 30 + 5 - bulk) {
            GET_ENERGY(ch) -= 300 + ((5 - bulk) * 100);
            act("You struggle to maintain your grip on $p.",
                FALSE, ch, wielded, NULL, TO_CHAR);
        }
}

/*
 * Hit was parried - can victim riposte?  Should encumbrance
 * be factored in?
 *
 * NOTE: riposte can only happen if an attack is deflected -
 * not dodged or evaded.
 */
int check_riposte(struct char_data* ch, struct char_data* victim)
{
    int dam;
    int prob = 0;
    int can_riposte = 0;
    struct obj_data* wielded;
    wielded = victim->equipment[WIELD];

    if (!wielded || GET_POS(victim) != POSITION_FIGHTING || IS_SET(victim->specials.affected_by, AFF_BASH)) {
        return 0;
    }

    const int ranger_prog = 32;
    if (IS_NPC(victim)) {
        if (has_alias(victim, "riposter") && has_program(victim, ranger_prog)) {
            can_riposte = 1;
            prob = 75;
        }
    } else {
        can_riposte = 1;
        prob = GET_SKILL(victim, SKILL_RIPOSTE);
    }

    if (can_riposte && prob)
        if (wielded->obj_flags.value[2] <= 3) {
            prob += GET_SKILL(victim, SKILL_STEALTH);
            prob += GET_DEX(victim) * 5;
            prob *= GET_PROF_LEVEL(PROF_RANGER, victim);
            prob /= 200;

            if (number(0, 99) <= prob) {
                do_riposte(victim, ch);
                dam = get_weapon_damage(wielded) * std::min(static_cast<int>(GET_DEX(victim)), 20) / number(50, 100);

                if (damage(victim, ch, dam, weapon_hit_type(wielded->obj_flags.value[3]), 1)) {
                    return 1;
                }
            }
        }
    return 0;
}

/*
 * Given a hit location and a damage, calculate how much damage
 * should be done after the victim's armor is factored in.  The
 * modified amount is returned.
 *
 * This used to be (tmp >= 0); this implied that torches
 * could parry. - Tuh
 */
int armor_effect(struct char_data* ch, struct char_data* victim,
    int damage, int location, int w_type)
{
    struct obj_data* armor;

    /* Bogus hit location */
    if (location < 0 || location > MAX_WEAR)
        return 0;

    /* If they've got armor, let's let it do its thing */
    if (victim->equipment[location] != NULL) {
        armor = victim->equipment[location];

        /* First, remove minimum absorb */
        int damage_reduction = armor->obj_flags.value[1];

        player_spec::weapon_master_handler weapon_master(ch);

        int divisor = 100;
        if (w_type == TYPE_SPEARS) {
            /* Spears hit the armor, but then go right through it */
            divisor += 50;

            // Weapon masters have a chance to double their armor punching with spears.
            if (weapon_master.does_spear_proc(victim)) {
                divisor *= 2;
            }
        }

        damage_reduction += ((damage - damage_reduction) * armor_absorb(armor) + 50) / divisor;

        // Weapon masters have a chance to bypass the vast majority of armor with daggers.
        if (weapon_master.ignores_armor(victim)) {
            damage_reduction = armor->obj_flags.value[1];
        }

        /* Heavy fighters get an extra 10% damage absorption. */
        if (utils::get_specialization(*victim) == (int)game_types::PS_HeavyFighting) {
            damage_reduction += damage_reduction / 10;
        }

        damage -= damage_reduction;

        /*
         * Smiting weapons can sometimes crush an opponent's bones
         * under the armor; wearing armor will actually INCREASE
         * the amount of damage taken, since the bodypart cannot
         * recoil under the disfigured metal.  Additionally, one's
         * head can hit the armor from the inside and add extra
         * damage.
         *
         * Based on a real life program on the discovery channel.
         *
         * Alright, now smiting only works on rigid metal armor,
         * not chain or leather.  Unfortunately, it won't work on
         * armor made of mithril, even if the description says the
         * armor is made of mithril plats.  Perhaps mithril should
         * never appear in plates? Or perhaps we should have some
         * other flag to define rigidity? Or perhaps smiting should
         * not exist? :)
         */
        if (w_type == TYPE_SMITE && armor->obj_flags.material == 4) {
            if (number() >= 0.80) {
                send_to_char("Your opponent's bones crunch loudly.\n\r", ch);
                send_to_char("OUCH! You hear a crunching sound and feel "
                             "a sharp pain.\n\r",
                    victim);
                send_to_room_except_two("You hear a crunching sound.\n\r", ch->in_room, ch, victim);

                damage += damage_reduction * 2;
            }
        }
    }

    return damage;
}

//============================================================================
int heavy_fighting_effect(char_data& attacker, int damage)
{
    if (utils::get_specialization(attacker) == (int)game_types::PS_HeavyFighting) {
        obj_data* weapon = attacker.equipment[WIELD];
        if (weapon && weapon->get_bulk() >= 3 && weapon->get_weight() > LIGHT_WEAPON_WEIGHT_CUTOFF) {
            int extra_damage = damage / 20;

            heavy_fighting_data* data = static_cast<heavy_fighting_data*>(attacker.extra_specialization_data.current_spec_info);
            data->add_heavy_fighting_damage(extra_damage);

            return damage + extra_damage;
        }
    }

    return damage;
}

bool is_frenzy_active(char_data& attacker)
{
    return utils::get_race(attacker) == RACE_OLOGHAI && utils::is_affected_by_spell(attacker, SKILL_FRENZY);
}

int frenzy_effect(char_data& attacker, int damage)
{
    if (is_frenzy_active(attacker)) {
        return damage * 1.10;
    }

    return damage;
}

//============================================================================
int wild_fighting_effect(char_data* attacker, int damage)
{
    player_spec::wild_fighting_handler handler(attacker);
    return handler.do_rush(damage);
}

//============================================================================
int defender_effect(char_data* attacker, char_data* victim, int damage)
{
    if (utils::get_specialization(*victim) == game_types::PS_Defender) {
        obj_data* shield = victim->equipment[WEAR_SHIELD];
        if (shield && GET_ITEM_TYPE(shield) == ITEM_SHIELD) {

            static const float reduced_per_block = 0.3f;

            float blocked_damage_percentage = 0.0f;
            if (auto defend_affect = utils::is_affected_by_spell(*victim, SKILL_DEFEND)) {
                int defend_chance = defend_affect->modifier;
                if (number(0, 100) <= defend_chance) {
                    blocked_damage_percentage += reduced_per_block;
                }
            }

            int warrior_level = utils::get_prof_level(PROF_WARRIOR, *victim);
            int ranger_level = utils::get_prof_level(PROF_RANGER, *victim);
            int defender_block_chance = std::max(warrior_level, ranger_level);
            defender_block_chance += std::min(warrior_level, ranger_level) / 2;

            if (number(0, 100) <= defender_block_chance) {
                blocked_damage_percentage += reduced_per_block;
            }

            // Adding the extra float at the end to handle precision issues.
            if (blocked_damage_percentage > reduced_per_block + 0.01f) {
                // critical block
                act("Your shield absorbs most of the impact of $N's attack.", FALSE, victim, nullptr, attacker, TO_CHAR);
                act("$n absorbs most of your attack with $s shield.", FALSE, victim, nullptr, attacker, TO_VICT);
                act("$n absorbs most of the impact of $N's attack with $s shield.", FALSE, victim, 0, attacker, TO_NOTVICT, FALSE);
            } else if (blocked_damage_percentage > 0.01f) {
                // standard block
                act("You block $N's attack, reducing its effectiveness.", FALSE, victim, nullptr, attacker, TO_CHAR);
                act("$n blocks your attack, reducing its effectiveness.", FALSE, victim, nullptr, attacker, TO_VICT);
                act("$n blocks $N's attack.", FALSE, victim, 0, attacker, TO_NOTVICT, FALSE);
            }

            int blocked_damage = int(damage * blocked_damage_percentage);

            defender_data* data = static_cast<defender_data*>(victim->extra_specialization_data.current_spec_info);
            data->add_blocked_damage(blocked_damage);

            return damage - blocked_damage;
        }
    }

    return damage;
}

//============================================================================
int get_evasion_malus(const char_data& attacker, const char_data& victim)
{
    if (!utils::is_affected_by(victim, AFF_EVASION))
        return 0;

    const int BASE_VALUE = 5;

    int defender_bonus = utils::get_prof_level(PROF_CLERIC, victim) / 2;
    int attacker_offset = utils::get_prof_level(PROF_CLERIC, attacker) * (100 - utils::get_perception(attacker)) / 200;

    if (utils::is_npc(attacker)) {
        attacker_offset /= 2;
    }

    int spec_bonus = 0;
    // Protection specialization grants a bit of extra dodge.
    if (utils::get_specialization(victim) == game_types::PS_Protection) {
        spec_bonus += 3;
    }

    // Always return at least the base value.
    if (attacker_offset > defender_bonus)
        return BASE_VALUE + spec_bonus;

    return BASE_VALUE + spec_bonus + defender_bonus - attacker_offset;
}

int natural_attack_dam(struct char_data* attacker)
{
    int dam, level_factor, str_factor, warrior_factor;

    if (utils::get_skill(*attacker, SKILL_NATURAL_ATTACK) == 0)
        return dam = BAREHANDED_DAMAGE * 10;

    level_factor = GET_LEVEL(attacker);
    level_factor = level_factor / 3;
    warrior_factor = utils::get_prof_level(PROF_WARRIOR, *attacker);
    str_factor = GET_STR(attacker);
    dam = level_factor + str_factor + warrior_factor;
    if (utils::get_specialization(*attacker) != game_types::PS_LightFighting && GET_LEVEL(attacker) > 11) {
        if (utils::get_specialization(*attacker) == game_types::PS_WildFighting)
            dam = (int)((double)dam * 0.75);
        else
            dam = (int)((double)dam * 0.50);
    }

    return dam;
}

//============================================================================
void hit(char_data* ch, char_data* victim, int type)
{
    obj_data* wielded = 0; /* weapon that ch wields */
    int w_type; /* weapon type, like TYPE_SLASH */
    int OB;
    int dam = 0, dodge_malus, evasion_malus;
    int location;
    int tmp; /* rolled number stored as the chance to hit, adds OB */
    waiting_type tmpwtl;
    extern race_bodypart_data bodyparts[16];

    if (ch->in_room != victim->in_room) {
        log("SYSERR: NOT SAME ROOM WHEN FIGHTING!");
        return;
    }

    if (GET_POS(ch) < POSITION_FIGHTING)
        return;

    if (ch->equipment[WIELD] && ch->equipment[WIELD]->obj_flags.type_flag == ITEM_WEAPON) {
        wielded = ch->equipment[WIELD];
        w_type = weapon_hit_type(wielded->obj_flags.value[3]);
        dam = get_weapon_damage(wielded);
    } else {
        /* XXX: Can we find some uses of attack_type elsewhere? */
        if (IS_NPC(ch) && ch->specials.attack_type >= TYPE_HIT)
            w_type = ch->specials.attack_type;
        else {
            w_type = TYPE_HIT;
            if (!IS_NPC(ch))
                dam = natural_attack_dam(ch);
        }
    }

    // Don't allow damage to affect invalid targets.
    game_rules::big_brother& bb_instance = game_rules::big_brother::instance();
    if (!bb_instance.is_target_valid(ch, victim, w_type)) {
        send_to_char("You feel the Gods looking down upon you, and protecting your target.  Your hand is stayed.\r\n", ch);
        return;
    }

    player_spec::weapon_master_handler weapon_master(ch);

    /*
     * Calculate hits/misses/damage
     *
     * NOTE: If tmp is 35, then the hit will succeed no matter
     * what.  This is not just because of the 100 OB bonus, but
     * because of the if statements which decide whether a dodge,
     * parry or evade is possible.
     */
    tmp = number(1, 35);
    OB = get_real_OB(ch);
    OB += number(1, 55 + OB / 4);
    OB += tmp;
    OB -= OB / 8;
    OB -= 40;
    if (tmp == 35)
        OB += 100;

    dodge_malus = evasion_malus = 0;
    evasion_malus = get_evasion_malus(*ch, *victim);
    dodge_malus = get_real_dodge(victim) + evasion_malus;
    OB -= dodge_malus;

    // Weapon masters have a chance to ignore a shield's contribution to dodge (and parry, but that's handled later).
    bool ignore_shield = weapon_master.ignores_shields(victim);
    if (ignore_shield) {
        obj_data* shield = victim->equipment[WEAR_SHIELD];
        if (shield != nullptr) {
            OB += shield->obj_flags.value[1];
        }
    }

    if (GET_POS(victim) < POSITION_FIGHTING)
        OB += 10 * (POSITION_FIGHTING - GET_POS(victim));

    /* checking here for the hit legitimacy... i hate this */
    if (GET_ENERGY(ch) >= ENE_TO_HIT) {
        GET_ENERGY(ch) -= ENE_TO_HIT;

        if (IS_SHADOW(victim)) {
            if (!weapon_willpower_damage(ch, victim))
                do_pass_through(ch, victim, 0);
            return;
        }

        if (is_frenzy_active(*ch)) {
            tmp = 35;
        }

        if (OB < 0 && tmp != 35) {
            if (number(0, dodge_malus) < evasion_malus)
                do_evade(ch, victim, w_type);
            else
                do_dodge(ch, victim, w_type);
        } else {
            if (GET_POS(victim) > POSITION_STUNNED) {
                OB -= get_real_parry(victim) * GET_CURRENT_PARRY(victim) / 100;

                if (ignore_shield) {
                    obj_data* shield = victim->equipment[WEAR_SHIELD];
                    if (shield != nullptr) {
                        // scale the shield's parry from the user's current parry
                        OB += shield->obj_flags.value[0] * GET_CURRENT_PARRY(victim) / 100;
                    }
                }
            }

            SET_CURRENT_PARRY(victim) = GET_CURRENT_PARRY(victim) * 2 / 3;

            if (tmp == 35) {
                OB = std::max(OB, 0);
            }

            if (OB < 0) {
                do_parry(ch, victim, w_type);
                if (check_riposte(ch, victim))
                    return;

                check_grip(ch, wielded);
                check_grip(victim, victim->equipment[WIELD]);
            } else {
                /* Generate a hit location based on bodypart structure */
                /*
                 *XXX: Make into a separate function that treats bodyparts
                 * for all body types well.
                 */
                location = 0;
                if (bodyparts[GET_BODYTYPE(victim)].bodyparts) {
                    tmp = number(1, 100);

                    for (; tmp > 0 && location < MAX_BODYPARTS; location++)
                        tmp -= bodyparts[GET_BODYTYPE(victim)].percent[location];
                }

                if (location)
                    location--;
                if (IS_NPC(ch))
                    dam /= 2; /* mobs have weapon damage halved */

                /*
                 * 100 str would double damage, double effect for
                 * 2-handers, last factor is avg. 1.4 with str 13
                 * OB bonus factored in, and mult. by number between
                 * 1 and 2, with  numbers close to 1 more probable
                 */
                dam += GET_DAMAGE(ch) * 10;
                int damage_roll = number(0, 100);

                // Weapon masters with axes have a chance to proc a second damage roll, using the better of the two.
                damage_roll = weapon_master.do_on_damage_rolled(damage_roll, victim);

                /* damage divided again by 10 */
                dam = (dam * (OB + 100) * (10000 + (damage_roll * damage_roll) + (IS_TWOHANDED(ch) ? 2 : 1) * 133 * GET_BAL_STR(ch))) / 13300000;

                // Add in "specialization" damage before armor.
                dam = check_find_weakness(ch, victim, dam);
                dam = wild_fighting_effect(ch, dam);
                dam = heavy_fighting_effect(*ch, dam);
                if (!ignore_shield) {
                    dam = defender_effect(ch, victim, dam);
                }
                dam = frenzy_effect(*ch, dam);
                dam = weapon_master.get_total_damage(dam);

                tmp = bodyparts[GET_BODYTYPE(victim)].armor_location[location];
                dam = armor_effect(ch, victim, dam, tmp, w_type);

                if (GET_POS(victim) < POSITION_FIGHTING)
                    dam += dam / 2;

                dam = std::max(0, dam); /* Not less than 0 damage */

                // Weapon masters with clubs and smiters have a chance to proc additional effects based on damage dealt.
                weapon_master.do_on_damage_dealt(dam, victim);

                damage(ch, victim, dam, w_type, location);

                if (dam > 0) {
                    check_grip(ch, wielded);
                }

                /* so we won't check engagement for hit people */
                return;
            }
        }
    }

    /*
     * Following moved here from damage function, so dodge/parry
     * would engage.  It might be better done by just rewriting
     * dodge to go through damage parser, we'll see.
     *
     * Later: I think it could be marginally better to send this
     * code through the damage function, mainly so that we don't
     * have to worry about duplicated code.  However, we might
     * not have a very elegant way to preclude parries/dodges
     * from calling the SPECIAL_DAMAGE proc in that case.
     *
     * NOTE: Everything beyond this point happens ONLY if 'ch'
     * didn't have enough energy to hit, or 'ch' was parried,
     * dodged or evaded.  If any amount of damage is dealt
     * (whether it be by riposte, a normal hit, etc), this code
     * is not reached, and its functionality is duplicated in
     * damage().  It is for this reason why I think these calls
     * for SPECIAL_DAMAGE procs are highly out of place, and
     * this mess of position updating is also absolutely horrid.
     */
    if (ch->specials.fighting != victim) {
        tmpwtl.targ1.ptr.ch = victim;
        tmpwtl.targ1.type = TARGET_CHAR;
        tmpwtl.targ2.ptr.other = 0;
        tmpwtl.targ2.type = TARGET_NONE;
        tmp = special(ch, 0, "", SPECIAL_DAMAGE, &tmpwtl);
        if (tmp) {
            if (ch->specials.fighting == victim)
                stop_fighting(ch);
            return;
        }
    }

    if (GET_POS(ch) > POSITION_STUNNED && victim != ch)
        if (!ch->specials.fighting || GET_POS(ch) != POSITION_FIGHTING)
            set_fighting(ch, victim);

    if (GET_POS(victim) > POSITION_STUNNED) {
        if (!victim->specials.fighting || GET_POS(victim) != POSITION_FIGHTING)
            set_fighting(victim, ch);

        if (IS_NPC(victim) && IS_SET(victim->specials2.act, MOB_MEMORY) && !IS_NPC(ch) && (GET_LEVEL(ch) < LEVEL_IMMORT))
            remember(victim, ch);

        GET_POS(victim) = POSITION_FIGHTING;
    }

    if (GET_HIT(victim) < GET_MAX_HIT(victim) / 5) {
        act("You wish that your wounds would stop BLEEDING so much!",
            FALSE, victim, 0, 0, TO_CHAR);
        if (IS_NPC(victim))
            if (IS_SET(victim->specials2.act, MOB_WIMPY))
                if (GET_POSITION(victim) > POSITION_SLEEPING)
                    do_flee(victim, "", 0, 0, 0);
    }

    if (!IS_NPC(victim) && WIMP_LEVEL(victim) && victim != ch && GET_HIT(victim) < WIMP_LEVEL(victim)) {
        if (GET_POSITION(victim) > POSITION_SLEEPING) {
            send_to_char("You wimp out, and attempt to flee!\n\r", victim);
            do_flee(victim, "", 0, 0, 0);
        }
    }

    if (!IS_NPC(victim) && !(victim->desc && victim->desc->descriptor) && victim->specials.fighting && GET_POS(victim) > POSITION_INCAP) {
        do_flee(victim, "", 0, 0, 0);
        victim->specials.was_in_room = victim->in_room;
    }
}

bool is_victim_around(const char_data* character)
{
    // The character is no longer fighting anyone.  Can't double-hit.
    if (character->specials.fighting == NULL)
        return false;

    // The character's enemy is no longer in the room (probably wimpied out).  Can't
    // attack an enemy that isn't there.
    if (character->specials.fighting->in_room != character->in_room)
        return false;

    return true;
}

bool can_double_hit(const char_data* character)
{
    assert(character);

    // Only characters with light-fighting can double-hit.
    if (utils::get_specialization(*character) != game_types::PS_LightFighting)
        return false;

    // Characters must be wielding a weapon in one hand to double strike.
    obj_data* weapon = character->equipment[WIELD];
    if (!weapon || utils::is_twohanded(*character))
        return false;

    // Characters must be using a light weapon
    if (weapon && weapon->get_bulk() >= 3 && weapon->get_weight() > LIGHT_WEAPON_WEIGHT_CUTOFF)
        return false;

    return is_victim_around(character);
}

bool does_double_hit_proc(const char_data* character)
{
    // Double-hit has a 20% proc chance.
    return number() >= 0.80;
}

bool can_beorning_swipe(struct char_data* character)
{
    assert(character);

    if (GET_RACE(character) != RACE_BEORNING)
        return false;

    return is_victim_around(character);
}

/*
 * does_beorning_swipe_proc:
 * determines the probability of a swipe attack firing off
 * taking into account:
 * - the char warrior level
 * - the char skill level
 * - the char level
 * NOTE: Max probability should be 31 for a level 90
 * bear with 36w and fully practiced swipe
 */
bool does_beorning_swipe_proc(struct char_data* character)
{
    int warrior_level, skill_level, ch_level;
    double chance;
    warrior_level = utils::get_prof_level(PROF_WARRIOR, *character) / 3;
    skill_level = utils::get_skill(*character, SKILL_SWIPE) / 10;
    ch_level = GET_LEVEL(character) / 10;
    chance = (warrior_level + skill_level + ch_level) / 100.0;
    return number() <= chance;
}

namespace {
timeval last_time;
timeval current_time;

timeval timediff(struct timeval* a, struct timeval* b)
{
    struct timeval rslt, tmp;

    tmp = *a;

    if ((rslt.tv_usec = tmp.tv_usec - b->tv_usec) < 0) {
        rslt.tv_usec += 1000000;
        --(tmp.tv_sec);
    }
    if ((rslt.tv_sec = tmp.tv_sec - b->tv_sec) < 0) {
        rslt.tv_usec = 0;
        rslt.tv_sec = 0;
    }
    return (rslt);
}
}

/*
 * Control all of the fights going on; works on PULSE_VIOLENCE
 */
void perform_violence(int mini_tics)
{
    last_time = current_time;
    gettimeofday(&current_time, NULL);
    timeval time_difference = timediff(&current_time, &last_time);

    float time_delta = time_difference.tv_sec + time_difference.tv_usec / 1000000.0f;
    for (char_data* fighter = combat_list; fighter; fighter = combat_next_dude) {
        fighter->damage_details.tick(time_delta);
        if (fighter->group) {
            fighter->group->track_combat_time(fighter, time_delta);
        }

        combat_next_dude = fighter->next_fighting;

        SET_CURRENT_PARRY(fighter) = std::min(GET_CURRENT_PARRY(fighter) + 3, 100);

        if (GET_MENTAL_DELAY(fighter) && (GET_MENTAL_DELAY(fighter) > -120)) {
            GET_MENTAL_DELAY(fighter)
            --;
        }

        if (GET_MENTAL_DELAY(fighter) > 1)
            continue;

        if (fighter->specials.fighting && (IS_MENTAL(fighter) || (IS_NPC(fighter) && IS_SHADOW(fighter->specials.fighting)))) {
            if ((GET_POS(fighter) >= POSITION_FIGHTING) && !IS_AFFECTED(fighter, AFF_WAITING)) {
                do_mental(fighter, "", 0, 0, 0);
            }
            continue; // have in mind, the ch could die himself
        }

        if (!IS_AFFECTED(fighter, AFF_WAITING) || (GET_WAIT_PRIORITY(fighter) == 29) || (GET_WAIT_PRIORITY(fighter) == 59)) {
            if ((GET_POS(fighter) >= POSITION_FIGHTING) && (fighter->specials.ENERGY <= ENE_TO_HIT)) {
                fighter->specials.ENERGY += utils::get_energy_regen(*fighter);
            } else if (IS_NPC(fighter) && !fighter->delay.wait_value) {
                do_stand(fighter, "", 0, 0, 0);
            }

            if (fighter->specials.ENERGY > ENE_TO_HIT) {
                if (AWAKE(fighter) && fighter->specials.fighting && (fighter->in_room == fighter->specials.fighting->in_room)) {
                    // Note:  Calling hit reduces the character's current energy.
                    sh_int current_energy = fighter->specials.ENERGY;
                    hit(fighter, fighter->specials.fighting, TYPE_UNDEFINED);

                    if (can_double_hit(fighter) && does_double_hit_proc(fighter)) {
                        light_fighting_data* data = static_cast<light_fighting_data*>(fighter->extra_specialization_data.current_spec_info);
                        data->add_light_fighting_proc();

                        char_data* victim = fighter->specials.fighting;
                        act("You find an opening in $N's defenses, and strike again rapidly.", FALSE, fighter, NULL, victim, TO_CHAR);
                        act("$n finds an opening in your defenses, and strikes again rapidly.", FALSE, fighter, NULL, victim, TO_VICT);
                        act("$n finds an opening in $N's defenses, and strikes again rapidly.", FALSE, fighter, 0, victim, TO_NOTVICT, FALSE);

                        fighter->specials.ENERGY = current_energy;
                        hit(fighter, victim, TYPE_UNDEFINED);
                    }

                    else if (can_beorning_swipe(fighter) && does_beorning_swipe_proc(fighter)) {
                        char_data* victim = fighter->specials.fighting;
                        act("You rear back and extend your foreleg swiping at $N!", FALSE, fighter, NULL, victim, TO_CHAR);
                        act("$n rears back and extends $s foreleg swiping at you!", FALSE, fighter, NULL, victim, TO_VICT);
                        act("$n rears back and extends $s foreleg swiping at $N!", FALSE, fighter, 0, victim, TO_NOTVICT, FALSE);

                        fighter->specials.ENERGY = current_energy;
                        hit(fighter, victim, TYPE_UNDEFINED);
                    }

                    else {
                        player_spec::weapon_master_handler weapon_master(fighter);
                        weapon_master.regain_energy(fighter->specials.fighting);
                    }
                } else /* Not in same room */
                {
                    stop_fighting(fighter);
                }
            }
        }
    }
}

#define SWORD_HAND WIELD
#define SHIELD_HAND WEAR_SHIELD

ACMD(do_twohand)
{
    if (!ch->equipment[SWORD_HAND]) {
        send_to_char("You are not wielding anything.\n\r", ch);
        return;
    }

    if (subcmd == SCMD_TWOHANDED) {
        if (IS_TWOHANDED(ch)) {
            send_to_char("You are already wielding your weapon with two hands.\n\r",
                ch);
            return;
        }

        if (ch->equipment[SHIELD_HAND]) {
            send_to_char("You can not wield a weapon in two hands while "
                         "wearing a shield.\n\r",
                ch);
            return;
        }

        if ((ch->equipment[SWORD_HAND])->obj_flags.value[2] <= 4)
            send_to_char("This weapon seems too small to be used two-handed.\n\r",
                ch);

        SET_BIT(ch->specials.affected_by, AFF_TWOHANDED);
        send_to_char("You wield your weapon with both hands.\n\r", ch);
        act("$n takes $s weapon with both hands.", FALSE, ch, 0, 0, TO_ROOM);
        affect_total(ch);
        return;
    }

    if (subcmd == SCMD_ONEHANDED) {
        if (!IS_TWOHANDED(ch)) {
            send_to_char("You are already wielding your weapon with one hand.\n\r",
                ch);
            return;
        }
        REMOVE_BIT(ch->specials.affected_by, AFF_TWOHANDED);
        send_to_char("You wield your weapon with one hand.\n\r", ch);
        act("$n takes $s weapon with one hand.", FALSE, ch, 0, 0, TO_ROOM);
        affect_total(ch);
        return;
    }
}

int check_hallucinate(struct char_data* ch, struct char_data* victim)
{
    struct affected_type* aff;

    /*
     * Check for a hallucinating ch and check whether or not
     * they just hit thin air.  If they did, remove a modifier.
     * If not, remove the effect.  If the modifier is now 0 as
     * a result of removal, remove the effect.
     */
    if (IS_AFFECTED(ch, AFF_HALLUCINATE)) {
        for (aff = ch->affected; aff; aff = aff->next) {
            if (aff->type == SPELL_HALLUCINATE) {
                /* We found the affection, test the modifier */
                if (number(1, 100) > (100 / (aff->modifier + 1))) {
                    /* They hit thin air.  Subtract a number from the modifier */
                    aff->modifier--;

                    act("You hit thin air!", FALSE, ch, 0, victim, TO_CHAR);
                    act("$N pauses in confusion as they hit thin air!",
                        FALSE, victim, 0, ch, TO_CHAR);
                    act("$n pauses in confusion as they hit thin air!",
                        FALSE, ch, 0, victim, TO_NOTVICT);

                    /* If the modifier is now 0, unaffect the character */
                    if (aff->modifier == 0)
                        affect_from_char(ch, SPELL_HALLUCINATE);

                    /* Get out of the damage sequence, because we did miss, in effect */
                    return 0;
                } else {
                    /*
                     * They hit the player.  Unaffect the character and
                     * continue the damage sequence as though nothing
                     * happened.
                     */
                    affect_from_char(ch, SPELL_HALLUCINATE);
                    return 1;
                }
            }
        }
    }

    /* No affection, so return 1 (indicating that ch is not affected) */
    return 1;
}
