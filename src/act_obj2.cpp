/* ************************************************************************
 *   File: act.obj2.c                                    Part of CircleMUD *
 *  Usage: eating/drinking and wearing/removing equipment                  *
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

#include "comm.h"
#include "db.h"
#include "handler.h"
#include "interpre.h"
#include "limits.h"
#include "script.h"
#include "spells.h"
#include "structs.h"
#include "utils.h"

/* extern variables */
extern struct room_data world;
extern char* drinks[];
extern int drink_aff[][3];
ACMD(do_twohand);

void weight_change_object(struct obj_data* obj, int weight)
{
    struct obj_data* tmp_obj;
    struct char_data* tmp_ch;

    if (obj->in_room != NOWHERE) {
        GET_OBJ_WEIGHT(obj) += weight;
    } else if ((tmp_ch = obj->carried_by)) {
        obj_from_char(obj);
        GET_OBJ_WEIGHT(obj) += weight;
        obj_to_char(obj, tmp_ch);
    } else if ((tmp_obj = obj->in_obj)) {
        obj_from_obj(obj);
        GET_OBJ_WEIGHT(obj) += weight;
        obj_to_obj(obj, tmp_obj);
    } else {
        log("SYSERR: Unknown attempt to subtract weight from an object.");
    }
}

void name_from_drinkcon(struct obj_data* obj)
{
    int i;
    char* new_name;
    extern struct obj_data* obj_proto;

    for (i = 0; (*((obj->name) + i) != ' ') && (*((obj->name) + i) != '\0'); i++)
        ;

    if (*((obj->name) + i) == ' ') {
        new_name = str_dup((obj->name) + i + 1);
        if (obj->item_number < 0 || obj->name != obj_proto[obj->item_number].name)
            RELEASE(obj->name);
        obj->name = new_name;
    }
}

void name_to_drinkcon(struct obj_data* obj, int type)
{
    char* new_name;
    extern struct obj_data* obj_proto;
    extern char* drinknames[];

    CREATE(new_name, char, strlen(obj->name) + strlen(drinknames[type]) + 2);
    sprintf(new_name, "%s %s", drinknames[type], obj->name);
    if (obj->item_number < 0 || obj->name != obj_proto[obj->item_number].name)
        RELEASE(obj->name);
    obj->name = new_name;
}

extern struct obj_data generic_water;
extern struct obj_data generic_poison;
ACMD(do_drink)
{
    struct obj_data* temp;
    struct affected_type af;
    int amount, weight;
    int on_ground = 0;

    one_argument(argument, arg);

    if (!*arg) {
        send_to_char("Drink from what?\n\r", ch);
        return;
    }

    temp = 0;

    if (!strcmp(arg, "water") && IS_SET(world[ch->in_room].room_flags, DRINK_WATER | DRINK_POISON)) {
        if (IS_SET(world[ch->in_room].room_flags, DRINK_WATER))
            temp = &generic_water;
        if (IS_SET(world[ch->in_room].room_flags, DRINK_POISON))
            temp = &generic_poison;
        if (!temp) {
            act("You can't find it!", FALSE, ch, 0, 0, TO_CHAR);
            return;
        }
    } else {

        if (!(temp = get_obj_in_list_vis(ch, arg, ch->carrying, 9999))) {
            if (!(temp = get_obj_in_list_vis(ch, arg, world[ch->in_room].contents, 9999))) {
                act("You can't find it!", FALSE, ch, 0, 0, TO_CHAR);
                return;
            } else
                on_ground = 1;
        }
    }

    {
        if ((GET_ITEM_TYPE(temp) != ITEM_DRINKCON) && (GET_ITEM_TYPE(temp) != ITEM_FOUNTAIN)) {
            send_to_char("You can't drink from that!\n\r", ch);
            return;
        }

        if (on_ground && (GET_ITEM_TYPE(temp) == ITEM_DRINKCON)) {
            send_to_char("You have to be holding that to drink from it.\n\r", ch);
            return;
        }
    }
    if ((GET_COND(ch, DRUNK) > 10) && (GET_COND(ch, THIRST) > 0)) {
        /* The pig is drunk */
        send_to_char("You can't seem to get close enough to your mouth.\n\r", ch);
        act("$n tries to drink but misses $s mouth!", TRUE, ch, 0, 0, TO_ROOM);
        return;
    }

    if (/*(GET_COND(ch, FULL) > 20) && */ (GET_COND(ch, THIRST) > 23)) {
        send_to_char("Your stomach can't contain anymore!\n\r", ch);
        return;
    }

    if (!temp->obj_flags.value[1]) {
        send_to_char("It's empty.\n\r", ch);
        return;
    }

    if (subcmd == SCMD_DRINK) {
        if (temp->obj_flags.type_flag != ITEM_FOUNTAIN) {
            sprintf(buf, "$n drinks %s from $p.", drinks[temp->obj_flags.value[2]]);
            act(buf, TRUE, ch, temp, 0, TO_ROOM);
        }

        sprintf(buf, "You drink the %s.\n\r", drinks[temp->obj_flags.value[2]]);
        send_to_char(buf, ch);

        if (drink_aff[temp->obj_flags.value[2]][DRUNK] > 0)
            amount = (25 - GET_COND(ch, THIRST)) / drink_aff[temp->obj_flags.value[2]][DRUNK];
        else
            amount = number(3, 10);

    } else {
        act("$n sips from the $o.", TRUE, ch, temp, 0, TO_ROOM);
        sprintf(buf, "It tastes like %s.\n\r", drinks[temp->obj_flags.value[2]]);
        send_to_char(buf, ch);
        amount = 1;
    }

    amount = MIN(amount, temp->obj_flags.value[1]);

    /* You can't subtract more than the object weighs */
    weight = MIN(amount, temp->obj_flags.weight);
    if (temp != &generic_water && temp != &generic_poison)
        weight_change_object(temp, -weight); /* Subtract amount */

    gain_condition(ch, DRUNK,
        (int)((int)drink_aff[temp->obj_flags.value[2]][DRUNK] * amount) / 4);

    gain_condition(ch, FULL,
        (int)((int)drink_aff[temp->obj_flags.value[2]][FULL] * amount) / 4);

    gain_condition(ch, THIRST,
        (int)((int)drink_aff[temp->obj_flags.value[2]][THIRST] * amount) / 4);

    if (GET_COND(ch, DRUNK) > 10)
        send_to_char("You feel drunk.\n\r", ch);

    if (GET_COND(ch, THIRST) > 20)
        send_to_char("You don't feel thirsty any more.\n\r", ch);

    if (GET_COND(ch, FULL) > 20)
        send_to_char("You are full.\n\r", ch);

    if (temp->obj_flags.value[3]) { /* The shit was poisoned ! */
        send_to_char("Oops, it tasted rather strange!\n\r", ch);
        act("$n chokes and utters some strange sounds.", TRUE, ch, 0, 0, TO_ROOM);

        af.type = SPELL_POISON;
        af.duration = amount;
        af.modifier = 0;
        af.location = APPLY_NONE;
        af.bitvector = AFF_POISON;
        affect_join(ch, &af, FALSE, FALSE);
        // TASK-021 port: a poisoned drink has NO poisoner -- whoever poisoned
        // the container is long gone and was never recorded -- so this
        // clears the record rather than leaving whatever an earlier
        // poisoning wrote. A death by this poison credits nobody, which is
        // the honest answer.
        record_poison_origin(ch, nullptr);
    }

    call_trigger(ON_DRINK, temp, ch, 0);

    /* empty the container, and no longer poison. */
    if (temp != &generic_water && temp != &generic_poison)
        temp->obj_flags.value[1] -= amount;
    if (!temp->obj_flags.value[1]) { /* The last bit */
        temp->obj_flags.value[2] = 0;
        temp->obj_flags.value[3] = 0;
        name_from_drinkcon(temp);
    }
    return;
}

ACMD(do_eat)
{
    struct obj_data* food;
    struct affected_type af;
    int amount;

    one_argument(argument, arg);

    if (!*arg) {
        send_to_char("Eat what?\n\r", ch);
        return;
    }

    if (!(food = get_obj_in_list_vis(ch, arg, ch->carrying, 9999)))
        if (!(food = get_obj_in_list_vis(ch, arg, world[ch->in_room].contents, 9999))) {
            send_to_char("You don't seem to have any.\n\r", ch);
            return;
        }

    if (subcmd == SCMD_TASTE && ((GET_ITEM_TYPE(food) == ITEM_DRINKCON) || (GET_ITEM_TYPE(food) == ITEM_FOUNTAIN))) {
        do_drink(ch, argument, wtl, 0, SCMD_SIP);
        return;
    }

    if ((GET_ITEM_TYPE(food) != ITEM_FOOD) && (GET_LEVEL(ch) < LEVEL_GOD)) {
        send_to_char("You can't eat this.\n\r", ch);
        return;
    }

    if (GET_COND(ch, FULL) > 20) { /* Stomach full */
        act("You are too full to eat more.", FALSE, ch, 0, 0, TO_CHAR);
        return;
    }

    if (subcmd == SCMD_EAT) {
        act("You eat $p.", FALSE, ch, food, 0, TO_CHAR);
        act("$n eats $p.", TRUE, ch, food, 0, TO_ROOM);
    } else {
        act("You nibble a little bit of $p.", FALSE, ch, food, 0, TO_CHAR);
        act("$n tastes a little bit of $p.", TRUE, ch, food, 0, TO_ROOM);
    }

    amount = (subcmd == SCMD_EAT ? food->obj_flags.value[0] : 1);

    gain_condition(ch, FULL, amount);

    if (GET_COND(ch, FULL) > 20)
        act("You are full.", FALSE, ch, 0, 0, TO_CHAR);

    if (food->obj_flags.value[3] && (GET_LEVEL(ch) < LEVEL_IMMORT)) {
        /* The shit was poisoned ! */
        send_to_char("Oops, that tasted rather strange!\n\r", ch);
        act("$n coughs and utters some strange sounds.", FALSE, ch, 0, 0, TO_ROOM);

        af.type = SPELL_POISON;
        af.duration = amount * 2;
        af.modifier = 0;
        af.location = APPLY_NONE;
        af.bitvector = AFF_POISON;
        affect_join(ch, &af, FALSE, FALSE);
        // TASK-021 port: poisoned food, like poisoned drink above -- no
        // poisoner to record, so the record is cleared instead of left
        // stale.
        record_poison_origin(ch, nullptr);
    }

    call_trigger(ON_EAT, food, ch, 0);

    if (subcmd == SCMD_EAT)
        extract_obj(food);
    else {
        if (!(--food->obj_flags.value[0])) {
            send_to_char("There's nothing left now.\n\r", ch);
            extract_obj(food);
        }
    }
}

extern struct obj_data generic_water;
extern struct obj_data generic_poison;

ACMD(do_pour)
{
    char arg1[MAX_INPUT_LENGTH];
    char arg2[MAX_INPUT_LENGTH];
    struct obj_data* from_obj = 0;
    struct obj_data* to_obj = 0;
    int amount, gener = 0;

    if (IS_SHADOW(ch)) {
        send_to_char("You cannot do this when inhabiting the shadow world.\n\r", ch);
        return;
    }

    argument_interpreter(argument, arg1, arg2);

    if (subcmd == SCMD_POUR) {
        if (!*arg1) /* No arguments */ {
            act("What do you want to pour from?", FALSE, ch, 0, 0, TO_CHAR);
            return;
        }

        if (IS_SET(world[ch->in_room].room_flags, DRINK_WATER | DRINK_POISON) && !strcmp(arg1, "water")) {
            if (IS_SET(world[ch->in_room].room_flags, DRINK_WATER))
                from_obj = &generic_water;
            else
                from_obj = &generic_poison;
            gener = 1;
        } else {
            gener = 0;
            if (!(from_obj = get_obj_in_list_vis(ch, arg1, world[ch->in_room].contents, 9999))) {
                if (!(from_obj = get_obj_in_list_vis(ch, arg1, ch->carrying, 9999))) {
                    act("You can't find it!", FALSE, ch, 0, 0, TO_CHAR);
                    return;
                }
            }
        }
        if (from_obj->obj_flags.type_flag != ITEM_DRINKCON && from_obj->obj_flags.type_flag != ITEM_FOUNTAIN) {
            act("You can't pour from that!", FALSE, ch, 0, 0, TO_CHAR);
            return;
        }
    }

    /*   if (subcmd == SCMD_FILL) {
      if (!*arg1) { // no arguments
         send_to_char("What do you want to fill?  And what are you filling it from?\n\r", ch);
         return;
      }
      if (!(to_obj = get_obj_in_list_vis(ch, arg1, ch->carrying,9999))) {
         send_to_char("You can't find it!", ch);
         return;
      }

      if (GET_ITEM_TYPE(to_obj) != ITEM_DRINKCON) {
         act("You can't fill $p!", FALSE, ch, to_obj, 0, TO_CHAR);
         return;
      }

      if (!*arg2) { // no 2nd argument
         act("What do you want to fill $p from?", FALSE, ch, to_obj, 0, TO_CHAR);
         return;
      }


      if (!(from_obj = get_obj_in_list_vis(ch, arg2, world[ch->in_room].contents,9999))) {
         sprintf(buf, "There doesn't seem to be any '%s' here.\n\r", arg2);
         send_to_char(buf, ch);
         return;
      }

      if (GET_ITEM_TYPE(from_obj) != ITEM_FOUNTAIN) {
         act("You can't fill something from $p.", FALSE, ch, from_obj, 0, TO_CHAR);
         return;
      }
   }
   */
    if (from_obj->obj_flags.value[1] == 0) {
        act("The $p is empty.", FALSE, ch, from_obj, 0, TO_CHAR);
        return;
    }

    if (subcmd == SCMD_POUR) /* pour */ {
        if (!*arg2) {
            act("Where do you want it?  Out or in what?", FALSE, ch, 0, 0, TO_CHAR);
            return;
        }

        if (!str_cmp(arg2, "out")) {
            if (from_obj->obj_flags.type_flag == ITEM_FOUNTAIN) {
                send_to_char("You can't empty this.\n\r", ch);
                return;
            }
            act("$n empties $p.", TRUE, ch, from_obj, 0, TO_ROOM);
            act("You empty $p.", FALSE, ch, from_obj, 0, TO_CHAR);

            weight_change_object(from_obj, -from_obj->obj_flags.value[1]); /* Empty */

            from_obj->obj_flags.value[1] = 0;
            from_obj->obj_flags.value[2] = 0;
            from_obj->obj_flags.value[3] = 0;
            name_from_drinkcon(from_obj);

            return;
        }

        if (!(to_obj = get_obj_in_list_vis(ch, arg2, ch->carrying, 9999))) {
            act("You can't find it!", FALSE, ch, 0, 0, TO_CHAR);
            return;
        }

        if ((to_obj->obj_flags.type_flag != ITEM_DRINKCON) && (to_obj->obj_flags.type_flag != ITEM_FOUNTAIN)) {
            act("You can't pour anything into that.", FALSE, ch, 0, 0, TO_CHAR);
            return;
        }
    }

    if (to_obj == from_obj) {
        act("A most unproductive effort.", FALSE, ch, 0, 0, TO_CHAR);
        return;
    }

    if ((to_obj->obj_flags.value[1] != 0) && (to_obj->obj_flags.value[2] != from_obj->obj_flags.value[2])) {
        act("There is already another liquid in it!", FALSE, ch, 0, 0, TO_CHAR);
        return;
    }

    if (!(to_obj->obj_flags.value[1] < to_obj->obj_flags.value[0])) {
        act("There is no room for more.", FALSE, ch, 0, 0, TO_CHAR);
        return;
    }

    if (subcmd == SCMD_POUR) {
        sprintf(buf, "You pour some %s into the %s.\n\r",
            drinks[from_obj->obj_flags.value[2]], arg2);
        send_to_char(buf, ch);
        sprintf(buf, "$n pours some %s into a %s.", drinks[from_obj->obj_flags.value[2]], arg2);
        act(buf, TRUE, ch, 0, 0, TO_ROOM);
    }

    if (subcmd == SCMD_FILL) {
        act("You gently fill $p from $P.", FALSE, ch, to_obj, from_obj, TO_CHAR);
        act("$n gently fills $p from $P.", TRUE, ch, to_obj, from_obj, TO_ROOM);
    }

    /* New alias */
    if (to_obj->obj_flags.value[1] == 0)
        name_to_drinkcon(to_obj, from_obj->obj_flags.value[2]);

    /* First same type liq. */
    to_obj->obj_flags.value[2] = from_obj->obj_flags.value[2];

    /* Then how much to pour */
    if (!gener)
        from_obj->obj_flags.value[1] -= (amount = (to_obj->obj_flags.value[0] - to_obj->obj_flags.value[1]));
    else
        amount = to_obj->obj_flags.value[0] - to_obj->obj_flags.value[1];

    to_obj->obj_flags.value[1] = to_obj->obj_flags.value[0];

    if (!gener)
        if (from_obj->obj_flags.value[1] < 0) { /* There was too little */
            to_obj->obj_flags.value[1] += from_obj->obj_flags.value[1];
            amount += from_obj->obj_flags.value[1];
            from_obj->obj_flags.value[1] = 0;
            from_obj->obj_flags.value[2] = 0;
            from_obj->obj_flags.value[3] = 0;
            name_from_drinkcon(from_obj);
        }

    /* Then the poison boogie */
    to_obj->obj_flags.value[3] = (to_obj->obj_flags.value[3] || from_obj->obj_flags.value[3]);

    /* And the weight boogie */
    if (!gener)
        weight_change_object(from_obj, -amount);
    weight_change_object(to_obj, amount); /* Add weight */

    return;
}

void wear_message(struct char_data* ch, struct obj_data* obj, int where)
{
    char* wear_messages[][2] = {
        { "$n lights $p and holds it.",
            "You light $p and hold it." },

        { "$n slides $p on to $s right ring finger.",
            "You slide $p on to your right ring finger." },

        { "$n slides $p on to $s left ring finger.",
            "You slide $p on to your left ring finger." },

        { "$n wears $p around $s neck.",
            "You wear $p around your neck." },

        { "$n wears $p around $s neck.",
            "You wear $p around your neck." },

        {
            "$n wears $p on $s body.",
            "You wear $p on your body.",
        },

        { "$n wears $p on $s head.",
            "You wear $p on your head." },

        { "$n puts $p on $s legs.",
            "You put $p on your legs." },

        { "$n wears $p on $s feet.",
            "You wear $p on your feet." },

        { "$n puts $p on $s hands.",
            "You put $p on your hands." },

        { "$n wears $p on $s arms.",
            "You wear $p on your arms." },

        { "$n straps $p around $s arm as a shield.",
            "You start to use $p as a shield." },

        { "$n wears $p about $s body.",
            "You wear $p around your body." },

        { "$n wears $p around $s waist.",
            "You wear $p around your waist." },

        { "$n puts $p on around $s right wrist.",
            "You put $p on around your right wrist." },

        { "$n puts $p on around $s left wrist.",
            "You put $p on around your left wrist." },

        { "$n wields $p.",
            "You wield $p." },

        { "$n grabs $p.",
            "You grab $p." },

        { "$n wears $p around $s back.",
            "You wear $p around your back." },

        { "$n fastens $p on $s belt.",
            "You fasten $p on your belt." },

        { "$n fastens $p on $s belt.",
            "You fasten $p on your belt." },

        { "$n fastens $p on $s belt.",
            "You fasten $p on your belt." }
    };

    char* beorn_wear_messages[][2] = {
        { "$n lights $p and holds it.",
            "You light $p and hold it." },

        { "$n slides $p on to $s right paw.",
            "You slide $p on to your right paw." },

        { "$n slides $p on to $s left paw.",
            "You slide $p on to your left paw." },

        { "$n wears $p around $s neck.",
            "You wear $p around your neck." },

        { "$n wears $p around $s neck.",
            "You wear $p around your neck." },

        {
            "$n wears $p on $s body.",
            "You wear $p on your body.",
        },

        { "$n wears $p on $s head.",
            "You wear $p on your head." },

        { "$n puts $p on $s hindlegs.",
            "You put $p on your hindlegs." },

        { "$n wears $p on $s hindfeet.",
            "You wear $p on your hindfeet." },

        { "$n puts $p on $s claws.",
            "You put $p on your claws." },

        { "$n wears $p on $s forelegs.",
            "You wear $p on your forelegs." },

        { "$n straps $p around $s arm as a shield.",
            "You start to use $p as a shield." },

        { "$n wears $p about $s body.",
            "You wear $p around your body." },

        { "$n wears $p around $s waist.",
            "You wear $p around your waist." },

        { "$n puts $p on around $s right wrist.",
            "You put $p on around your right wrist." },

        { "$n puts $p on around $s left wrist.",
            "You put $p on around your left wrist." },

        { "$n wields $p.",
            "You wield $p." },

        { "$n grabs $p.",
            "You grab $p." },

        { "$n wears $p around $s back.",
            "You wear $p around your back." },

        { "$n fastens $p on $s belt.",
            "You fasten $p on your belt." },

        { "$n fastens $p on $s belt.",
            "You fasten $p on your belt." },

        { "$n fastens $p on $s belt.",
            "You fasten $p on your belt." }
    };
    if (GET_RACE(ch) == RACE_BEORNING) {
        act(beorn_wear_messages[where][0], TRUE, ch, obj, 0, TO_ROOM);
        act(beorn_wear_messages[where][1], FALSE, ch, obj, 0, TO_CHAR);
    } else {
        act(wear_messages[where][0], TRUE, ch, obj, 0, TO_ROOM);
        act(wear_messages[where][1], FALSE, ch, obj, 0, TO_CHAR);
    }
}

bool ologhai_item_restriction(char_data* character, obj_data* item)
{
    // Only Olog-Hais here
    if (GET_RACE(character) != RACE_OLOGHAI) {
        return false;
    }

    // Olog-Hais cannot wield small weapons, big hands and all.
    if (CAN_WEAR(item, ITEM_WIELD) && (item && item->get_bulk() <= 3 && item->get_weight() <= LIGHT_WEAPON_WEIGHT_CUTOFF)) {
        send_to_char("Your massive hands are unable to grasp the tiny weapon.\n\r", character);
        return true;
    }

    return false;
}

bool beorning_item_restriction(char_data* character, obj_data* item)
{
    if (CAN_WEAR(item, ITEM_WEAR_SHIELD)) {
        send_to_char("You cannot wear a shield.\n\r", character);
        return false;
    }

    if (CAN_WEAR(item, ITEM_WEAR_BODY)) {
        send_to_char("You cannot wear anything around your body.\n\r", character);
        return false;
    }

    if (CAN_WEAR(item, ITEM_WEAR_LEGS)) {
        send_to_char("You cannot wear anything around your hindlegs.\n\r", character);
        return false;
    }

    if (CAN_WEAR(item, ITEM_WEAR_ARMS)) {
        send_to_char("You cannot wear anything around your forelegs.\n\r", character);
        return false;
    }

    if (CAN_WEAR(item, ITEM_HOLD)) {
        send_to_char("You cannot hold anything.\n\r", character);
        return false;
    }

    if (CAN_WEAR(item, ITEM_WEAR_HANDS)) {
        send_to_char("You cannot wear anything on your claws.\n\r", character);
        return false;
    }

    if (CAN_WEAR(item, ITEM_WIELD)) {
        send_to_char("Sorry, bears can't wield weapons.\n\r", character);
        return false;
    }

    if (CAN_WEAR(item, ITEM_WEAR_FEET)) {
        send_to_char("You cannot wear anything on your hindfeet.\n\r", character);
        return false;
    }

    if (armor_absorb(item) > 0) {
        if (CAN_WEAR(item, ITEM_WEAR_BODY) || CAN_WEAR(item, ITEM_WEAR_HEAD) || CAN_WEAR(item, ITEM_WEAR_LEGS) || CAN_WEAR(item, ITEM_WEAR_ARMS)) {
            send_to_char("You cannot wear armor.\n\r", character);
            return false;
        }
    }

    return true;
}
void perform_remove(struct char_data* ch, int pos);

void perform_wear(char_data* character, obj_data* item, int item_slot, bool wearall = false)
{
    static int item_slots[] = {
        ITEM_TAKE, ITEM_WEAR_FINGER, ITEM_WEAR_FINGER, ITEM_WEAR_NECK,
        ITEM_WEAR_NECK, ITEM_WEAR_BODY, ITEM_WEAR_HEAD, ITEM_WEAR_LEGS,
        ITEM_WEAR_FEET, ITEM_WEAR_HANDS, ITEM_WEAR_ARMS, ITEM_WEAR_SHIELD,
        ITEM_WEAR_ABOUT, ITEM_WEAR_WAISTE, ITEM_WEAR_WRIST, ITEM_WEAR_WRIST,
        ITEM_WIELD, ITEM_TAKE, ITEM_WEAR_BACK, ITEM_WEAR_BELT, ITEM_WEAR_BELT,
        ITEM_WEAR_BELT
    };

    static const char* already_wearing_messages[] = {
        "You're already using a light.\n\r",
        "YOU SHOULD NEVER SEE THIS MESSAGE.  PLEASE REPORT.\n\r",
        "You're already wearing something on both of your ring fingers.\n\r",
        "YOU SHOULD NEVER SEE THIS MESSAGE.  PLEASE REPORT.\n\r",
        "You can't wear anything else around your neck.\n\r",
        "You're already wearing something on your body.\n\r",
        "You're already wearing something on your head.\n\r",
        "You're already wearing something on your legs.\n\r",
        "You're already wearing something on your feet.\n\r",
        "You're already wearing something on your hands.\n\r",
        "You're already wearing something on your arms.\n\r",
        "You're already using a shield.\n\r",
        "You're already wearing something about your body.\n\r",
        "You already have something around your waist.\n\r",
        "YOU SHOULD NEVER SEE THIS MESSAGE.  PLEASE REPORT.\n\r",
        "You're already wearing something around both of your wrists.\n\r",
        "You're already wielding a weapon.\n\r",
        "You're already holding something.\n\r",
        "You're already wearing something on your back.\n\r",
        "YOU SHOULD NEVER SEE THIS MESSAGE.  PLEASE REPORT.\n\r",
        "YOU SHOULD NEVER SEE THIS MESSAGE.  PLEASE REPORT.\n\r",
        "There is no more room on your belt.\n\r"
    };

    static const char* beorn_already_wearing_message[] = {
        "You're already using a light.\n\r",
        "YOU SHOULD NEVER SEE THIS MESSAGE.  PLEASE REPORT.\n\r",
        "You're already wearing something on both of your paws.\n\r",
        "YOU SHOULD NEVER SEE THIS MESSAGE.  PLEASE REPORT.\n\r",
        "You can't wear anything else around your neck.\n\r",
        "You're already wearing something on your body.\n\r",
        "You're already wearing something on your head.\n\r",
        "You're already wearing something on your hindlegs.\n\r",
        "You're already wearing something on your hindfeet.\n\r",
        "You're already wearing something on your claws.\n\r",
        "You're already wearing something on your forelegs.\n\r",
        "You're already using a shield.\n\r",
        "You're already wearing something about your body.\n\r",
        "You already have something around your waist.\n\r",
        "YOU SHOULD NEVER SEE THIS MESSAGE.  PLEASE REPORT.\n\r",
        "You're already wearing something around both of your wrists.\n\r",
        "You're already wielding a weapon.\n\r",
        "You're already holding something.\n\r",
        "You're already wearing something on your back.\n\r",
        "YOU SHOULD NEVER SEE THIS MESSAGE.  PLEASE REPORT.\n\r",
        "YOU SHOULD NEVER SEE THIS MESSAGE.  PLEASE REPORT.\n\r",
        "There is no more room on your belt.\n\r"
    };

    /*---------- Beorning item restriction here ----------*/
    if (GET_RACE(character) == RACE_BEORNING) {
        if (!beorning_item_restriction(character, item)) {
            return;
        }
    }

    if (ologhai_item_restriction(character, item)) {
        return;
    }

    /* see if this is shield, and weapon is in two hands */
    if ((GET_ITEM_TYPE(item) == ITEM_SHIELD) && IS_TWOHANDED(character) && character->equipment[WIELD]) {
        send_to_char("You cannot wear a shield while wielding a weapon in two hands\n\r", character);
        return;
    }

    /* first, make sure that the wear position is valid. */
    if (!CAN_WEAR(item, item_slots[item_slot])) {
        act("You can't wear $p there.", FALSE, character, item, 0, TO_CHAR);
        return;
    }

    /* if this is a belt item, make sure the person is wearing a belt */
    if ((item_slot == WEAR_BELT_1) && (!character->equipment[WEAR_WAISTE])) {
        send_to_char("You are not wearing a belt to put that on.\n\r", character);
        return;
    }

    if (!call_trigger(ON_WEAR, item, character, 0))
        return;

    /* for neck, finger, and wrist, try pos 2 if pos 1 is already full */
    if ((item_slot == WEAR_FINGER_R) || (item_slot == WEAR_NECK_1) || (item_slot == WEAR_WRIST_R))
        if (character->equipment[item_slot])
            item_slot++;

    /* for belt, try positions 2 and 3 if pos 1 is already full */
    if (item_slot == WEAR_BELT_1)
        if (character->equipment[item_slot])
            item_slot++;
    if (item_slot == WEAR_BELT_2)
        if (character->equipment[item_slot])
            item_slot++;

    if (character->equipment[item_slot]) {
        if (wearall == true) {
            if (GET_RACE(character) == RACE_BEORNING) {
                send_to_char(beorn_already_wearing_message[item_slot], character);
            } else {
                send_to_char(already_wearing_messages[item_slot], character);
            }
            return;
        }
        if (IS_CARRYING_N(character) >= CAN_CARRY_N(character)) {
            send_to_char("Your hands are already full!\n\r", character);
            return;
        } else {
            perform_remove(character, item_slot);
        }
    }

    obj_from_char(item);
    wear_message(character, item, item_slot);
    equip_char(character, item, item_slot);
}

int find_eq_pos(struct char_data* ch, struct obj_data* obj, char* arg)
{
    int where = -1;

    static char* keywords[] = {
        "!RESERVED!",
        "finger",
        "!RESERVED!",
        "neck",
        "!RESERVED!",
        "body",
        "head",
        "legs",
        "feet",
        "hands",
        "arms",
        "sheild",
        "about",
        "waist",
        "wrist",
        "!RESERVED!",
        "!RESERVED!",
        "!RESERVED!",
        "back",
        "!RESERVED!",
        "!RESERVED!",
        "belt",
        "\n"
    };

    if (!arg || !*arg) {
        if (CAN_WEAR(obj, ITEM_WEAR_FINGER))
            where = WEAR_FINGER_R;
        if (CAN_WEAR(obj, ITEM_WEAR_NECK))
            where = WEAR_NECK_1;
        if (CAN_WEAR(obj, ITEM_WEAR_BODY))
            where = WEAR_BODY;
        if (CAN_WEAR(obj, ITEM_WEAR_HEAD))
            where = WEAR_HEAD;
        if (CAN_WEAR(obj, ITEM_WEAR_LEGS))
            where = WEAR_LEGS;
        if (CAN_WEAR(obj, ITEM_WEAR_FEET))
            where = WEAR_FEET;
        if (CAN_WEAR(obj, ITEM_WEAR_HANDS))
            where = WEAR_HANDS;
        if (CAN_WEAR(obj, ITEM_WEAR_ARMS))
            where = WEAR_ARMS;
        if (CAN_WEAR(obj, ITEM_WEAR_SHIELD))
            where = WEAR_SHIELD;
        if (CAN_WEAR(obj, ITEM_WEAR_ABOUT))
            where = WEAR_ABOUT;
        if (CAN_WEAR(obj, ITEM_WEAR_WAISTE))
            where = WEAR_WAISTE;
        if (CAN_WEAR(obj, ITEM_WEAR_WRIST))
            where = WEAR_WRIST_R;
        if (CAN_WEAR(obj, ITEM_WEAR_BACK))
            where = WEAR_BACK;
        if (CAN_WEAR(obj, ITEM_WEAR_BELT))
            where = WEAR_BELT_1;
    } else {
        if ((where = search_block(arg, keywords, FALSE)) < 0) {
            sprintf(buf, "'%s'?  What part of your body is THAT?\n\r", arg);
            send_to_char(buf, ch);
        }
    }

    return where;
}

ACMD(do_wear)
{
    char arg1[MAX_INPUT_LENGTH];
    char arg2[MAX_INPUT_LENGTH];
    struct obj_data *obj, *next_obj;
    int where, dotmode, items_worn = 0;

    argument_interpreter(argument, arg1, arg2);

    if (!*arg1) {
        send_to_char("Wear what?\n\r", ch);
        return;
    }

    if (IS_NPC(ch) && MOB_FLAGGED(ch, MOB_PET) && !(MOB_FLAGGED(ch, MOB_ORC_FRIEND))) {
        send_to_char("Sorry, tamed mobiles can't wear anything.\n\r", ch);
        return;
    }
    /*- Quick fix to prevent pets from being overpowered */

    dotmode = find_all_dots(arg1);

    if (*arg2 && (dotmode != FIND_INDIV)) {
        send_to_char("You can't specify the same body location for more than one item!\n\r", ch);
        return;
    }

    if (dotmode == FIND_ALL) {
        for (obj = ch->carrying; obj; obj = next_obj) {
            next_obj = obj->next_content;

            where = find_eq_pos(ch, obj, 0);
            if ((where < 0) && CAN_WEAR(obj, ITEM_WIELD))
                where = WIELD;

            if (where >= 0) {
                items_worn++;
                perform_wear(ch, obj, where, true);
            }
        }
        if (!items_worn)
            send_to_char("You don't seem to have anything wearable.\n\r", ch);
    } else if (dotmode == FIND_ALLDOT) {
        if (!*arg1) {
            send_to_char("Wear all of what?\n\r", ch);
            return;
        }
        if (!(obj = get_obj_in_list(arg1, ch->carrying))) {
            sprintf(buf, "You don't seem to have any %ss.\n\r", arg1);
            send_to_char(buf, ch);
        } else
            while (obj) {
                next_obj = get_obj_in_list_vis(ch, arg1, obj->next_content, 9999);
                if ((where = find_eq_pos(ch, obj, 0)) >= 0)
                    perform_wear(ch, obj, where, false);
                else
                    act("You can't wear $p.", FALSE, ch, obj, 0, TO_CHAR);
                obj = next_obj;
            }
    } else {
        if (!(obj = get_obj_in_list_vis(ch, arg1, ch->carrying, 9999))) {
            sprintf(buf, "You don't seem to have %s %s.\n\r", AN(arg1), arg1);
            send_to_char(buf, ch);
        } else {
            if ((where = find_eq_pos(ch, obj, arg2)) >= 0)
                perform_wear(ch, obj, where);
            else if (!*arg2)
                act("You can't wear $p.", FALSE, ch, obj, 0, TO_CHAR);
        }
    }
}

ACMD(do_wield)
{
    struct obj_data* obj;

    one_argument(argument, arg);

    if (IS_NPC(ch) && MOB_FLAGGED(ch, MOB_PET) && !(MOB_FLAGGED(ch, MOB_ORC_FRIEND))) {
        send_to_char("Sorry, tamed mobiles can't wear anything.\n\r", ch);
        return;
    }

    if (GET_RACE(ch) == RACE_BEORNING) {
        send_to_char("Sorry, bears can't wield weapons.\n\r", ch);
        return;
    }

    if (!*arg) {
        send_to_char("Wield what?\n\r", ch);
    } else if (!(obj = get_obj_in_list(arg, ch->carrying))) {
        sprintf(buf, "You don't seem to have %s %s.\n\r", AN(arg), arg);
        send_to_char(buf, ch);
    } else {
        if (!CAN_WEAR(obj, ITEM_WIELD)) {
            char_data* message_receiver = ch;
            if (IS_NPC(ch) && MOB_FLAGGED(ch, MOB_ORC_FRIEND) && ch->master) {
                message_receiver = ch->master;
            }
            send_to_char("That item cannot be wielded.\n\r", message_receiver);
        } else if (GET_OBJ_WEIGHT(obj) > 150 * GET_STR(ch)) {
            if (IS_NPC(ch) && MOB_FLAGGED(ch, MOB_ORC_FRIEND) && ch->master) {
                send_to_char("Your puny follower is far too weak to wield that.\r\n", ch->master);
            } else {
                send_to_char("It's way too heavy for you to use.\n\r", ch);
            }
        } else {
            REMOVE_BIT(ch->specials.affected_by, AFF_TWOHANDED);
            perform_wear(ch, obj, WIELD);
            if (obj->obj_flags.value[2] > 4 && !ch->equipment[WEAR_SHIELD]) {
                do_twohand(ch, 0, 0, 0, SCMD_TWOHANDED);
            }
        }
    }
}

ACMD(do_grab)
{
    struct obj_data* obj;

    if (IS_NPC(ch) && MOB_FLAGGED(ch, MOB_PET) && !(MOB_FLAGGED(ch, MOB_ORC_FRIEND))) {
        send_to_char("Sorry, tamed mobiles can't hold anything.\n\r", ch);
        return;
    }

    one_argument(argument, arg);

    if (!*arg)
        send_to_char("Hold what?\n\r", ch);
    else if (!(obj = get_obj_in_list(arg, ch->carrying))) {
        sprintf(buf, "You don't seem to have %s %s.\n\r", AN(arg), arg);
        send_to_char(buf, ch);
    } else {
        if ((GET_ITEM_TYPE(obj) == ITEM_LIGHT) && (obj->obj_flags.value[2] != 0) && (!ch->equipment[WEAR_LIGHT])) {
            perform_wear(ch, obj, WEAR_LIGHT);
            //         if (obj->obj_flags.value[2])
            //	 world[ch->in_room].light++;
        } else
            perform_wear(ch, obj, HOLD);
    }
}

void perform_remove(struct char_data* ch, int pos)
{
    struct obj_data* obj;

    if (!(obj = ch->equipment[pos])) {
        log("Error in perform_remove: bad pos passed.");
        return;
    }

    if (IS_CARRYING_N(ch) >= CAN_CARRY_N(ch))
        act("$p: you can't carry that many items!", FALSE, ch, obj, 0, TO_CHAR);
    else {
        obj_to_char(unequip_char(ch, pos), ch);

        act("You stop using $p.", FALSE, ch, obj, 0, TO_CHAR);
        act("$n stops using $p.", TRUE, ch, obj, 0, TO_ROOM);

        // If this is a "belt" (waist item) being removed:
        // Check for items on the belt.  If there are any, they are removed as
        // well.  If there's no room for the removed items, they fall to the
        // ground.

        if (pos == WEAR_WAISTE) {
            if (ch->equipment[WEAR_BELT_1]) {
                obj = ch->equipment[WEAR_BELT_1];

                if (IS_CARRYING_N(ch) >= CAN_CARRY_N(ch)) {
                    obj_to_room(unequip_char(ch, WEAR_BELT_1), ch->in_room);

                    act("You can't carry that much; $p falls to the ground.", FALSE, ch, obj, 0, TO_CHAR);
                    act("$p falls to the ground.", TRUE, ch, obj, 0, TO_ROOM);
                } else {
                    obj_to_char(unequip_char(ch, WEAR_BELT_1), ch);

                    act("You stop using $p.", FALSE, ch, obj, 0, TO_CHAR);
                    act("$n stops using $p.", TRUE, ch, obj, 0, TO_ROOM);
                }
            }

            if (ch->equipment[WEAR_BELT_2]) {
                obj = ch->equipment[WEAR_BELT_2];

                if (IS_CARRYING_N(ch) >= CAN_CARRY_N(ch)) {
                    obj_to_room(unequip_char(ch, WEAR_BELT_2), ch->in_room);

                    act("You can't carry that much; $p falls to the ground.", FALSE, ch, obj, 0, TO_CHAR);
                    act("$p falls to the ground.", TRUE, ch, obj, 0, TO_ROOM);
                } else {
                    obj_to_char(unequip_char(ch, WEAR_BELT_2), ch);

                    act("You stop using $p.", FALSE, ch, obj, 0, TO_CHAR);
                    act("$n stops using $p.", TRUE, ch, obj, 0, TO_ROOM);
                }
            }

            if (ch->equipment[WEAR_BELT_3]) {
                obj = ch->equipment[WEAR_BELT_3];

                if (IS_CARRYING_N(ch) >= CAN_CARRY_N(ch)) {
                    obj_to_room(unequip_char(ch, WEAR_BELT_3), ch->in_room);

                    act("You can't carry that much; $p falls to the ground.", FALSE, ch, obj, 0, TO_CHAR);
                    act("$p falls to the ground.", TRUE, ch, obj, 0, TO_ROOM);
                } else {
                    obj_to_char(unequip_char(ch, WEAR_BELT_3), ch);

                    act("You stop using $p.", FALSE, ch, obj, 0, TO_CHAR);
                    act("$n stops using $p.", TRUE, ch, obj, 0, TO_ROOM);
                }
            }
        }

        //      if (obj->obj_flags.type_flag == ITEM_LIGHT)
        //	 if (obj->obj_flags.value[2] && obj->obj_flags.value[3])
        //	    world[ch->in_room].light--;
    }
}

ACMD(do_remove)
{
    int i, dotmode, found;

    one_argument(argument, arg);

    if (!*arg) {
        send_to_char("Remove what?\n\r", ch);
        return;
    }

    dotmode = find_all_dots(arg);

    if (dotmode == FIND_ALL) {
        found = 0;
        for (i = 0; i < MAX_WEAR; i++)
            if (ch->equipment[i]) {
                //	   printf("going to perform remove\n");
                perform_remove(ch, i);
                found = 1;
            }
        if (!found)
            send_to_char("You're not using anything.\n\r", ch);
    } else if (dotmode == FIND_ALLDOT) {
        if (!*arg)
            send_to_char("Remove all of what?\n\r", ch);
        else {
            found = 0;
            for (i = 0; i < MAX_WEAR; i++)
                if (ch->equipment[i] && CAN_SEE_OBJ(ch, ch->equipment[i]) && isname(arg, ch->equipment[i]->name, 0)) {
                    perform_remove(ch, i);
                    found = 1;
                }
            if (!found) {
                sprintf(buf, "You don't seem to be using any %ss.\n\r", arg);
                send_to_char(buf, ch);
            }
        }
    } else {
        for (i = 0; i < MAX_WEAR; i++)
            if (ch->equipment[i])
                if (isname(arg, ch->equipment[i]->name, 0))
                    break;
        if (i < MAX_WEAR)
            perform_remove(ch, i);
        else {
            sprintf(buf, "You don't seem to be using %s %s.\n\r", AN(arg), arg);
            send_to_char(buf, ch);
        }
    }
}

ACMD(do_light)
{
    int i;
    obj_data* torch;

    if (ch->in_room == NOWHERE)
        return;

    one_argument(argument, arg);

    torch = 0;

    if (!*arg)
        torch = ch->equipment[WEAR_LIGHT];
    else {
        torch = get_obj_in_list_vis(ch, arg, world[ch->in_room].contents, 9999);
        if (!torch)
            for (i = 0; i < MAX_WEAR; i++)
                if (ch->equipment[i] && CAN_SEE_OBJ(ch, ch->equipment[i]) && isname(arg, ch->equipment[i]->name, 0)) {
                    torch = ch->equipment[i];
                    break;
                }
    }
    if (!torch) {
        send_to_char("What do you want to light?\n\r", ch);
        return;
    }
    if ((torch->obj_flags.type_flag != ITEM_LIGHT) || (torch->obj_flags.value[2] == 0)) {
        send_to_char("You can not light this.\n\r", ch);
        return;
    }

    if (torch->obj_flags.value[3] != 0) {
        send_to_char("It is lit already.\n\r", ch);
        return;
    }
    torch->obj_flags.value[3] = 1;
    world[ch->in_room].light++;

    act("You light $p.", FALSE, ch, torch, 0, TO_CHAR);
    act("$n lights $p.", FALSE, ch, torch, 0, TO_ROOM);
}
ACMD(do_blowout)
{
    int i;
    obj_data* torch;

    if (ch->in_room == NOWHERE)
        return;

    one_argument(argument, arg);

    torch = 0;

    if (!*arg)
        torch = ch->equipment[WEAR_LIGHT];
    else {
        for (i = 0; i < MAX_WEAR; i++)
            if (ch->equipment[i] && CAN_SEE_OBJ(ch, ch->equipment[i]) && isname(arg, ch->equipment[i]->name, 0)) {
                torch = ch->equipment[i];
                break;
            }
        if (!torch)
            torch = get_obj_in_list_vis(ch, arg, world[ch->in_room].contents, 9999);
    }

    if (!torch) {
        send_to_char("What do you want to light?\n\r", ch);
        return;
    }
    if ((torch->obj_flags.type_flag != ITEM_LIGHT) || (torch->obj_flags.value[2] < 0) || (torch->obj_flags.value[3] < 0)) {
        send_to_char("You can not blow this out.\n\r", ch);
        return;
    }

    if (torch->obj_flags.value[3] == 0) {
        send_to_char("It is out already.\n\r", ch);
        return;
    }
    torch->obj_flags.value[3] = 0;
    world[ch->in_room].light--;

    act("You blow $p out.", FALSE, ch, torch, 0, TO_CHAR);
    act("$n blows $p out.", FALSE, ch, torch, 0, TO_ROOM);
}
