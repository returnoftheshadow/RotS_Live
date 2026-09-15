/* ************************************************************************
 *   File: act.obj1.c                                    Part of CircleMUD *
 *  Usage: object handling routines -- get/drop and container handling     *
 *                                                                         *
 *  All rights reserved.  See license.doc for complete information.        *
 *                                                                         *
 *  Copyright (C) 1993 by the Trustees of the Johns Hopkins University     *
 *  CircleMUD is based on DikuMUD, Copyright (C) 1990, 1991.               *
 ************************************************************************ */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "platdef.h"

#include "big_brother.h"
#include "comm.h"
#include "db.h"
#include "handler.h"
#include "interpre.h"
#include "script.h"
#include "spells.h"
#include "structs.h"
#include "utils.h"

/* extern variables */
extern struct room_data world;
extern struct player_index_element* player_table;
extern int top_of_p_table;
extern struct char_data* mob_proto;
extern struct obj_data* object_list;
extern struct index_data* obj_index;

void perform_put(struct char_data* ch, struct obj_data* obj,
    struct obj_data* container)
{
    if (GET_OBJ_WEIGHT(container) + GET_OBJ_WEIGHT(obj) > container->obj_flags.value[0])
        act("$p won't fit in $P.", FALSE, ch, obj, container, TO_CHAR);
    else {
        obj_from_char(obj);
        obj_to_obj(obj, container);
        act("You put $p in $P.", FALSE, ch, obj, container, TO_CHAR);
        act("$n puts $p in $P.", TRUE, ch, obj, container, TO_ROOM);

        if ((GET_ITEM_TYPE(obj) != ITEM_FOOD) && (GET_ITEM_TYPE(obj) != ITEM_DRINKCON)) {
            snprintf(buf, MAX_STRING_LENGTH, "OBJ: %s puts %s (%d) in %s (%d)", GET_NAME(ch),
                obj->short_description,
                (obj->item_number >= 0) ? obj_index[obj->item_number].virt : -1,
                container->short_description,
                (container->item_number >= 0) ? obj_index[container->item_number].virt : -1);
            log(buf);
        }
    }
}

/* The following put modes are supported by the code below:

        1) put <object> <container>
        2) put all.<object> <container>
        3) put all <container>

        <container> must be in inventory or on ground.
        all objects to be put into container must be in inventory.
*/

ACMD(do_put)
{
    char arg1[MAX_INPUT_LENGTH];
    char arg2[MAX_INPUT_LENGTH];
    struct obj_data* obj;
    struct obj_data* next_obj;
    struct obj_data* container;
    struct char_data* tmp_char;
    int obj_dotmode, cont_dotmode;

    argument_interpreter(argument, arg1, arg2);
    obj_dotmode = find_all_dots(arg1);
    cont_dotmode = find_all_dots(arg2);

    if (cont_dotmode != FIND_INDIV)
        send_to_char("You can only put things into one container at a time.\n\r", ch);
    else if (!*arg1)
        send_to_char("Put what in what?\n\r", ch);
    else if (!*arg2) {
        sprintf(buf, "What do you want to put %s in?\n\r",
            ((obj_dotmode != FIND_INDIV) ? "them" : "it"));
        send_to_char(buf, ch);
    } else {
        generic_find(arg2, FIND_OBJ_INV | FIND_OBJ_ROOM, ch, &tmp_char, &container);
        if (!container) {
            sprintf(buf, "You don't see a %s here.\n\r", arg2);
            send_to_char(buf, ch);
        } else if (GET_ITEM_TYPE(container) != ITEM_CONTAINER) {
            act("$p is not a container.", FALSE, ch, container, 0, TO_CHAR);
        } else if (IS_SET(container->obj_flags.value[1], CONT_CLOSED)) {
            send_to_char("You'd better open it first!\n\r", ch);
        } else {
            bool is_quiver = isname("quiver", container->name) != 0;
            if (obj_dotmode == FIND_ALL) { /* "put all <container>" case */
                /* check and make sure the guy has something first */
                if (container == ch->carrying && !ch->carrying->next_content) {
                    send_to_char("You don't seem to have anything to put in it.\n\r", ch);
                } else
                    for (obj = ch->carrying; obj; obj = next_obj) {
                        next_obj = obj->next_content;
                        if (GET_ITEM_TYPE(obj) != ITEM_MISSILE && is_quiver) {
                            send_to_char("Only arrows can go into a quiver.\n\r", ch);
                            break;
                        } else if (GET_ITEM_TYPE(obj) == ITEM_MISSILE && is_quiver) {
                            perform_put(ch, obj, container);
                        } else if (obj != container) {
                            perform_put(ch, obj, container);
                        }
                    }
            } else if (obj_dotmode == FIND_ALLDOT) { /* "put all.x <cont>" case */
                if (!(obj = get_obj_in_list_vis(ch, arg1, ch->carrying, 9999))) {
                    sprintf(buf, "You don't seem to have any %ss.\n\r", arg1);
                    send_to_char(buf, ch);
                } else
                    while (obj) {
                        next_obj = get_obj_in_list_vis(ch, arg1, obj->next_content, 9999);
                        if (GET_ITEM_TYPE(obj) != ITEM_MISSILE && is_quiver) {
                            send_to_char("Only arrows can go into a quiver.\n\r", ch);
                            break;
                        } else if (GET_ITEM_TYPE(obj) == ITEM_MISSILE && is_quiver) {
                            perform_put(ch, obj, container);
                        } else if (obj != container) {
                            perform_put(ch, obj, container);
                        }
                        obj = next_obj;
                    }
            } else { /* "put <thing> <container>" case */
                if (!(obj = get_obj_in_list_vis(ch, arg1, ch->carrying, 9999))) {
                    sprintf(buf, "You aren't carrying %s %s.\n\r", AN(arg1), arg1);
                    send_to_char(buf, ch);
                } else if (obj == container) {
                    send_to_char("You attempt to fold it into itself, but fail.\n\r", ch);
                } else if (GET_ITEM_TYPE(obj) != ITEM_MISSILE && is_quiver) {
                    send_to_char("Only arrows can go into a quiver.\n\r", ch);
                } else {
                    perform_put(ch, obj, container);
                }
            }
        }
    }
}

int can_take_obj(char_data* character, obj_data* item)
{
    if (IS_CARRYING_N(character) >= CAN_CARRY_N(character)) {
        sprintf(buf, "%s: You can't carry that many items.\n\r", OBJS(item, character));
        send_to_char(buf, character);
        return 0;
    } else if ((IS_CARRYING_W(character) + GET_OBJ_WEIGHT(item)) > CAN_CARRY_W(character)) {
        sprintf(buf, "%s: You can't carry that much weight.\n\r", OBJS(item, character));
        send_to_char(buf, character);
        return 0;
    } else if (!(CAN_WEAR(item, ITEM_TAKE))) {
        sprintf(buf, "%s: You can't take that!\n\r", OBJS(item, character));
        send_to_char(buf, character);
        return 0;
    }

    return 1;
}

void get_check_money(struct char_data* ch, struct obj_data* obj)
{
    if ((GET_ITEM_TYPE(obj) == ITEM_MONEY) && (obj->obj_flags.value[0] > 0)) {
        obj_from_char(obj);
        if (obj->obj_flags.value[0] > 1) {
            sprintf(buf, "There were %s.\n\r",
                money_message(obj->obj_flags.value[0], 1));
            send_to_char(buf, ch);
        }
        GET_GOLD(ch) += obj->obj_flags.value[0];
        extract_obj(obj);
    }
}

bool is_corpse(obj_data* container)
{
    return container && container->obj_flags.type_flag == ITEM_CONTAINER && container->obj_flags.value[3] == 1;
}

void perform_get_from_container(char_data* character, obj_data* item, obj_data* container, int mode)
{
    if (mode == FIND_OBJ_INV || can_take_obj(character, item)) {
        // Enforce Big Brother loot protection if the container is a corpse.
        if (is_corpse(container)) {
            game_rules::big_brother& bb_instance = game_rules::big_brother::instance();
            if (!bb_instance.on_loot_item(character, container, item)) {
                send_to_char("That corpse has been looted enough.\r\n", character);
                return;
            }
        }

        if (IS_CARRYING_N(character) >= CAN_CARRY_N(character)) {
            sprintf(buf, "%s: You can't hold any more items.\n\r", OBJS(item, character));
        } else {
            obj_from_obj(item);
            obj_to_char(item, character);
            act("You get $p from $P.", FALSE, character, item, container, TO_CHAR);
            act("$n gets $p from $P.", TRUE, character, item, container, TO_ROOM);

            if (item->touched == 1 && ((GET_ITEM_TYPE(item) != ITEM_FOOD) && (GET_ITEM_TYPE(item) != ITEM_DRINKCON))) {
                snprintf(buf, MAX_STRING_LENGTH, "OBJ: %s gets %s (%d) from %s (%d)", GET_NAME(character),
                    item->short_description,
                    (item->item_number >= 0) ? obj_index[item->item_number].virt : -1,
                    container->short_description,
                    (container->item_number >= 0) ? obj_index[container->item_number].virt : -1);
                log(buf);
            }

            if (!IS_NPC(character)) {
                item->touched = 1;
            }

            get_check_money(character, item);
        }
    }
}

void get_from_container(struct char_data* ch, struct obj_data* cont, char* arg, int mode)
{
    struct obj_data *obj, *next_obj;
    int obj_dotmode, found = 0;

    // find_all_dots is destructive on arg, so make a copy of arg and use that.
    char arg_copy[128];
    strcpy(arg_copy, arg);
    obj_dotmode = find_all_dots(arg_copy);

    if (IS_SET(cont->obj_flags.value[1], CONT_CLOSED)) {
        act("The $p is closed.", FALSE, ch, cont, 0, TO_CHAR);
    }

    else if (obj_dotmode == FIND_ALL) {
        /*
         * This if prevents people from spam
         * looting corpses while still allowing
         * for characters to use the get all corpse
         * command while looting their own corpse.
         */
        for (obj = cont->contains; obj; obj = next_obj) {
            next_obj = obj->next_content;
            found = 1;
            perform_get_from_container(ch, obj, cont, mode);
        }
        if (!found) {
            act("$p seems to be empty.", FALSE, ch, cont, 0, TO_CHAR);
        }
    } else if (obj_dotmode == FIND_ALLDOT) {
        if (!*arg_copy) {
            send_to_char("Get all of what?\n\r", ch);
            return;
        }

        obj = get_obj_in_list(arg_copy, cont->contains);
        while (obj) {
            next_obj = get_obj_in_list_vis(ch, arg_copy, obj->next_content, 9999);
            if (CAN_SEE_OBJ(ch, obj)) {
                found = 1;
                perform_get_from_container(ch, obj, cont, mode);
            }
            obj = next_obj;
        }

        if (!found) {
            sprintf(buf, "You can't find any %ss in $p.", arg_copy);
            act(buf, FALSE, ch, cont, 0, TO_CHAR);
        }
    } else {
        if (!(obj = get_obj_in_list(arg_copy, cont->contains))) {
            sprintf(buf, "There doesn't seem to be %s %s in $p.", AN(arg_copy), arg_copy);
            act(buf, FALSE, ch, cont, 0, TO_CHAR);
        } else {
            perform_get_from_container(ch, obj, cont, mode);
        }
    }
}

int perform_get_from_room(char_data* character, obj_data* item)
{
    // Player corpses have negative values on their ID.
    if (is_corpse(item) && item->obj_flags.value[2] < 0) {
        // Corpses can be moved if the player died to a mob, but not if the player
        // died to another player.
        game_rules::big_brother& bb_instance = game_rules::big_brother::instance();
        if (bb_instance.is_corpse_protected(character, item)) {
            send_to_char("You should leave the dead where they lay...", character);
            return 0;
        }
    }

    if (can_take_obj(character, item)) {
        obj_from_room(item);
        obj_to_char(item, character);
        act("You get $p.", FALSE, character, item, 0, TO_CHAR);
        act("$n gets $p.", TRUE, character, item, 0, TO_ROOM);
        if (item->touched == 1 && ((GET_ITEM_TYPE(item) != ITEM_FOOD)) && (GET_ITEM_TYPE(item) != ITEM_DRINKCON)) {
            snprintf(buf, MAX_STRING_LENGTH, "OBJ: %s gets %s (%d) at %s (%d)", GET_NAME(character),
                item->short_description,
                (item->item_number >= 0) ? obj_index[item->item_number].virt : -1,
                world[character->in_room].name, world[character->in_room].number);
            log(buf);
        }
        if (!IS_NPC(character)) {
            item->touched = 1;
        }
        get_check_money(character, item);
        return 1;
    }

    return 0;
}

void get_from_room(struct char_data* ch, char* arg)
{
    struct obj_data *obj, *next_obj;
    int dotmode, found = 0;

    dotmode = find_all_dots(arg);

    if (dotmode == FIND_ALL) {
        for (obj = world[ch->in_room].contents; obj; obj = next_obj) {
            next_obj = obj->next_content;
            if (CAN_SEE_OBJ(ch, obj)) {
                found = 1;
                perform_get_from_room(ch, obj);
            }
        }
        if (!found)
            send_to_char("There doesn't seem to be anything here.\n\r", ch);
    } else if (dotmode == FIND_ALLDOT) {
        if (!*arg) {
            send_to_char("Get all of what?\n\r", ch);
            return;
        }
        if (!(obj = get_obj_in_list_vis(ch, arg, world[ch->in_room].contents, 9999))) {
            sprintf(buf, "You don't see any %ss here.\n\r", arg);
            send_to_char(buf, ch);
        } else
            while (obj) {
                next_obj = get_obj_in_list_vis(ch, arg, obj->next_content, 9999);
                perform_get_from_room(ch, obj);
                obj = next_obj;
            }
    } else {
        if (!(obj = get_obj_in_list_vis(ch, arg, world[ch->in_room].contents, 9999))) {
            sprintf(buf, "You don't see %s %s here.\n\r", AN(arg), arg);
            send_to_char(buf, ch);
        } else
            perform_get_from_room(ch, obj);
    }
}

ACMD(do_get)
{
    char arg1[MAX_INPUT_LENGTH];
    char arg2[MAX_INPUT_LENGTH];

    int cont_dotmode, found = 0, mode;
    struct obj_data *cont, *next_cont;
    struct char_data* tmp_char;

    if (IS_CARRYING_N(ch) >= CAN_CARRY_N(ch))
        send_to_char("Your hands are already full!\n\r", ch);
    else {
        argument_interpreter(argument, arg1, arg2);
        if (!*arg1)
            send_to_char("Get what?\n\r", ch);
        else {
            if (!*arg2)
                get_from_room(ch, arg1);
            else {
                cont_dotmode = find_all_dots(arg2);
                if (cont_dotmode == FIND_ALL) { /* use all in inv. and on ground */
                    for (cont = ch->carrying; cont; cont = cont->next_content)
                        if (GET_ITEM_TYPE(cont) == ITEM_CONTAINER) {
                            found = 1;
                            get_from_container(ch, cont, arg1, FIND_OBJ_INV);
                        }
                    for (cont = world[ch->in_room].contents; cont; cont = cont->next_content)
                        if (CAN_SEE_OBJ(ch, cont) && GET_ITEM_TYPE(cont) == ITEM_CONTAINER) {
                            found = 1;
                            get_from_container(ch, cont, arg1, FIND_OBJ_ROOM);
                        }
                    if (!found)
                        send_to_char("You can't seem to find any containers.\n\r", ch);
                } else if (cont_dotmode == FIND_ALLDOT) {
                    if (!*arg2) {
                        send_to_char("Get from all of what?\n\r", ch);
                        return;
                    }
                    //   cont = get_obj_in_list_vis(ch, arg2, ch->carrying,9999);
                    cont = get_obj_in_list(arg2, ch->carrying);
                    while (cont) {
                        found = 1;
                        next_cont = get_obj_in_list_vis(ch, arg2, cont->next_content, 9999);
                        if (GET_ITEM_TYPE(cont) != ITEM_CONTAINER)
                            act("$p is not a container.", FALSE, ch, cont, 0, TO_CHAR);
                        else
                            get_from_container(ch, cont, arg1, FIND_OBJ_INV);
                        cont = next_cont;
                    }
                    cont = get_obj_in_list_vis(ch, arg2, world[ch->in_room].contents, 9999);
                    while (cont) {
                        found = 1;
                        next_cont = get_obj_in_list_vis(ch, arg2, cont->next_content, 9999);
                        if (GET_ITEM_TYPE(cont) != ITEM_CONTAINER)
                            act("$p is not a container.", FALSE, ch, cont, 0, TO_CHAR);
                        else
                            get_from_container(ch, cont, arg1, FIND_OBJ_ROOM);
                        cont = next_cont;
                    }
                    if (!found) {
                        sprintf(buf, "You can't seem to find any %ss here.\n\r", arg2);
                        send_to_char(buf, ch);
                    }
                } else { /* get <items> <container> (no all or all.x) */
                    mode = generic_find(arg2, FIND_OBJ_INV | FIND_OBJ_ROOM, ch, &tmp_char, &cont);
                    if (!cont) {
                        sprintf(buf, "You don't have %s %s.\n\r", AN(arg2), arg2);
                        send_to_char(buf, ch);
                    } else if (GET_ITEM_TYPE(cont) != ITEM_CONTAINER)
                        act("$p is not a container.", FALSE, ch, cont, 0, TO_CHAR);
                    else
                        get_from_container(ch, cont, arg1, mode);
                }
            }
        }
    }
}

void perform_drop_gold(struct char_data* ch, int amount, sh_int RDR)
{
    struct obj_data* obj;

    if (amount <= 0)
        send_to_char("Heh heh heh.. we are jolly funny today, eh?\n\r", ch);
    else if (GET_GOLD(ch) < amount)
        send_to_char("You don't have that many coins!\n\r", ch);
    else {
        obj = create_money(amount);
        send_to_char("You drop some money.\n\r", ch);
        act("$n drops some money.", FALSE, ch, 0, 0, TO_ROOM);
        obj_to_room(obj, ch->in_room);
        GET_GOLD(ch) -= amount;
        snprintf(buf, MAX_STRING_LENGTH, "OBJ: %s drops %d coins", GET_NAME(ch),
            amount);
        log(buf);
    }
}

int perform_drop(struct char_data* ch, struct obj_data* obj, sh_int RDR)
{
    if (IS_SET(obj->obj_flags.extra_flags, ITEM_NODROP)) {
        sprintf(buf, "You can't drop %s, it must be cursed!\n\r", OBJS(obj, ch));
        send_to_char(buf, ch);
        return 0;
    }

    sprintf(buf, "You drop %s.\n\r", OBJS(obj, ch));
    send_to_char(buf, ch);
    sprintf(buf, "$n drops $p.");
    act(buf, TRUE, ch, obj, 0, TO_ROOM);

    obj_from_char(obj);

    if ((GET_ITEM_TYPE(obj) != ITEM_FOOD) && (GET_ITEM_TYPE(obj) != ITEM_DRINKCON)) {
        snprintf(buf, MAX_STRING_LENGTH, "OBJ: %s drops %s (%d) at %s (%d)", GET_NAME(ch),
            obj->short_description,
            (obj->item_number >= 0) ? obj_index[obj->item_number].virt : -1,
            world[ch->in_room].name, world[ch->in_room].number);
        log(buf);
    }

    obj_to_room(obj, ch->in_room);
    obj->obj_flags.timer = 60;

    return 0;
}

ACMD(do_drop)
{
    struct obj_data *obj, *next_obj;
    sh_int RDR = 0;
    int dotmode, amount = 0;

    argument = one_argument(argument, arg);
    while (*argument && *argument <= ' ')
        argument++;
    if (!*arg) {
        sprintf(buf, "What do you want to drop?\n\r");
        send_to_char(buf, ch);
        return;
    } else if (is_number(arg)) {
        amount = atoi(arg);
        if (!strcmp("gold", argument)) {
            perform_drop_gold(ch, amount * COPP_IN_GOLD, RDR);
            amount = -1;
        } else if (!strcmp("silver", argument)) {
            perform_drop_gold(ch, amount * COPP_IN_SILV, RDR);
            amount = -1;
        } else if (!strcmp("copper", argument)) {
            perform_drop_gold(ch, amount, RDR);
            amount = -1;
        }

    } else {
        dotmode = find_all_dots(arg);

        if (dotmode == FIND_ALL) {
            if (!ch->carrying)
                send_to_char("You don't seem to be carrying anything.\n\r", ch);
            else
                for (obj = ch->carrying; obj; obj = next_obj) {
                    next_obj = obj->next_content;
                    amount += perform_drop(ch, obj, RDR);
                }
        } else if (dotmode == FIND_ALLDOT) {
            if (!*arg) {
                sprintf(buf, "What do you want to drop all of?\n\r");
                send_to_char(buf, ch);
                return;
            }
            if (!(obj = get_obj_in_list(arg, ch->carrying))) {
                sprintf(buf, "You don't seem to have any %ss.\n\r", arg);
                send_to_char(buf, ch);
            }
            while (obj) {
                next_obj = get_obj_in_list(arg, obj->next_content);
                amount += perform_drop(ch, obj, RDR);
                obj = next_obj;
            }
        } else {
            if (!(obj = get_obj_in_list(arg, ch->carrying))) {
                sprintf(buf, "You don't seem to have %s %s.\n\r", AN(arg), arg);
                send_to_char(buf, ch);
            } else
                amount += perform_drop(ch, obj, RDR);
        }
    }
}

void perform_give(struct char_data* ch, struct char_data* vict,
    struct obj_data* obj)
{
    if (IS_SET(obj->obj_flags.extra_flags, ITEM_NODROP)) {
        act("You can't let go of $p!!  Yeech!", FALSE, ch, obj, 0, TO_CHAR);
        return;
    }

    if (IS_CARRYING_N(vict) >= CAN_CARRY_N(vict)) {
        act("$N seems to have $S hands full.", FALSE, ch, 0, vict, TO_CHAR);
        return;
    }

    if (GET_OBJ_WEIGHT(obj) + IS_CARRYING_W(vict) > CAN_CARRY_W(vict)) {
        act("$E can't carry that much weight.", FALSE, ch, 0, vict, TO_CHAR);
        return;
    }

    obj_from_char(obj);
    obj_to_char(obj, vict);
    act("You give $p to $N.", FALSE, ch, obj, vict, TO_CHAR);
    act("$n gives you $p.", FALSE, ch, obj, vict, TO_VICT);
    act("$n gives $p to $N.", TRUE, ch, obj, vict, TO_NOTVICT);

    if ((GET_ITEM_TYPE(obj) != ITEM_FOOD) && (GET_ITEM_TYPE(obj) != ITEM_DRINKCON)) {
        snprintf(buf, MAX_STRING_LENGTH, "OBJ: %s gives %s (%d) to %s", GET_NAME(ch),
            obj->short_description,
            (obj->item_number >= 0) ? obj_index[obj->item_number].virt : -1,
            GET_NAME(vict));
        log(buf);
    }

    call_trigger(ON_RECEIVE, vict, ch, obj);
}

/* utility function for give */
struct char_data* give_find_vict(struct char_data* ch, char* arg1)
{
    struct char_data* vict;
    char arg2[MAX_INPUT_LENGTH];

    strcpy(buf, arg1);
    argument_interpreter(buf, arg1, arg2);
    if (!*arg1) {
        send_to_char("Give what to who?\n\r", ch);
        return 0;
    } else if (!*arg2) {
        send_to_char("To who?\n\r", ch);
        return 0;
    } else if (!(vict = get_char_room_vis(ch, arg2))) {
        send_to_char("No-one by that name here.\n\r", ch);
        return 0;
    } else if (vict == ch) {
        send_to_char("What's the point of that?\n\r", ch);
        return 0;
    } else
        return vict;
}

void perform_give_gold(struct char_data* ch, struct char_data* vict,
    int amount)
{
    if (amount <= 0) {
        send_to_char("Heh heh heh ... we are jolly funny today, eh?\n\r", ch);
        return;
    }

    if (GET_GOLD(ch) < amount) {
        send_to_char("You haven't got that many coins!\n\r", ch);
        return;
    }

    send_to_char("Ok.\n\r", ch);
    sprintf(buf, "%s gives you %s.\n\r", PERS(ch, vict, TRUE, FALSE),
        money_message(amount, 1));
    send_to_char(buf, vict);
    sprintf(buf, "You give %s to %s.\n\r", money_message(amount, 1),
        PERS(vict, ch, FALSE, FALSE));
    send_to_char(buf, ch);
    act("$n gives some money to $N.", TRUE, ch, 0, vict, TO_NOTVICT);

    GET_GOLD(ch) -= amount;
    GET_GOLD(vict) += amount;

    snprintf(buf, MAX_STRING_LENGTH, "OBJ: %s gives %d coins to %s", GET_NAME(ch),
        amount, GET_NAME(vict));
    log(buf);
}

ACMD(do_give)
{
    char arg1[MAX_INPUT_LENGTH];
    char arg2[MAX_INPUT_LENGTH];
    char* original_argument = 0;
    char* temp_arg = 0;
    int amount, dotmode, coinflag;
    struct char_data* vict;
    struct obj_data *obj, *next_obj;

    half_chop(argument, arg1, arg2);

    if (!*arg1)
        send_to_char("Give what to who?\n\r", ch);
    else if (is_number(arg1)) {
        amount = atoi(arg1);
        if (!(vict = give_find_vict(ch, arg2)))
            return;

        coinflag = 1;

        if (!strncmp("silver", arg2, strlen(arg2)))
            amount *= COPP_IN_SILV;
        else if (!strncmp("gold", arg2, strlen(arg2)))
            amount *= COPP_IN_GOLD;
        else if (!strncmp("copper", arg2, strlen(arg2)) && !strncmp("coins", arg2, strlen(arg2)))
            coinflag = 0;

        if (coinflag)
            perform_give_gold(ch, vict, amount);
        else {
            /* code to give multiple items.  anyone want to write it? -je */
            sprintf(arg2, "You don't seem to have a %s.\n\r", arg1);
            send_to_char(arg2, ch);
            return;
        }
    } else {
        original_argument = strdup(argument);
        if (!(vict = give_find_vict(ch, argument)))
            return;
        dotmode = find_all_dots(argument);
        if (dotmode == FIND_ALL) {
            if (!ch->carrying)
                send_to_char("You don't seem to be holding anything.\n\r", ch);
            else
                for (obj = ch->carrying; obj; obj = next_obj) {
                    next_obj = obj->next_content;
                    perform_give(ch, vict, obj);
                }
        } else if (dotmode == FIND_ALLDOT) {
            if (!*argument) {
                send_to_char("All of what?\n\r", ch);
                return;
            }
            if (!(obj = get_obj_in_list_vis(ch, argument, ch->carrying, 9999))) {
                sprintf(buf, "You don't seem to have any %ss.\n\r", argument);
                send_to_char(buf, ch);
            } else
                while (obj && vict) {
                    next_obj = get_obj_in_list_vis(ch, argument, obj->next_content, 9999);
                    perform_give(ch, vict, obj);
                    obj = next_obj;
                    /* This because of poor design - losing orignal arguments
                  in give_find_vict etc.  Needs to be rewritten.
               */
                    if (temp_arg)
                        RELEASE(temp_arg);
                    temp_arg = str_dup(original_argument);
                    vict = give_find_vict(ch, temp_arg); // What if the vict is destroyed by the 1st object?? This line added to check for this...
                }
        } else {
            if (!(obj = get_obj_in_list_vis(ch, argument, ch->carrying, 9999))) {
                sprintf(buf, "You don't seem to have %s %s.\n\r", AN(argument), argument);
                send_to_char(buf, ch);
            } else
                perform_give(ch, vict, obj);
        }
        if (temp_arg)
            RELEASE(temp_arg);
    }
}

int weapon_hit_type(int);
struct obj_data* load_scalp(int);

ACMD(do_butcher)
{
    int trophy_num, chance, no_tool, w_type = 0;
    struct obj_data *obj, *trophy, *tool;

    while (*argument && (*argument <= ' '))
        argument++;

    if (!*argument) {
        obj = get_obj_in_list_vis(ch, "corpse", world[ch->in_room].contents, 9999);
        if (!obj) {
            send_to_char("You see no corpse here.\n\r", ch);
            return;
        }
    } else {
        obj = get_obj_in_list_vis(ch, argument, world[ch->in_room].contents, 9999);
        if (!obj) {
            if (subcmd == SCMD_BUTCHER)
                send_to_char("What do you want to butcher?\n\r", ch);
            if (subcmd == SCMD_SCALP)
                send_to_char("What do you want to behead?\n\r", ch);
            return;
        }
    }
    if ((obj->obj_flags.type_flag != ITEM_CONTAINER) || (obj->obj_flags.value[3] != 1)) {
        if (subcmd == SCMD_BUTCHER)
            send_to_char("You can't butcher this.\n\r", ch);
        if (subcmd == SCMD_SCALP)
            send_to_char("You can't behead this.\n\r", ch);
        return;
    }

    if (subcmd == SCMD_BUTCHER) {
        if (obj->obj_flags.butcher_item == 0) {
            send_to_char("There is nothing of value on it.\n\r", ch);
            return;
        }
        if (obj->obj_flags.butcher_item == -1) {
            send_to_char("It has already been butchered.\n\r", ch);
            return;
        }

        tool = ch->equipment[WIELD];

        if (tool) {
            w_type = weapon_hit_type(tool->obj_flags.value[3]);
            if ((tool->obj_flags.type_flag != ITEM_WEAPON) || ((w_type != TYPE_SLASH) && (w_type != TYPE_PIERCE)))
                tool = 0;
        }
        if (!tool) {
            if (GET_RACE(ch) == RACE_BEORNING) {
                chance = 50 + (GET_PROF_LEVEL(PROF_RANGER, ch) + 2);
            } else {
                chance = 0;
                no_tool = 1;
            }
        } else {
            no_tool = 0;
            chance = 20 - tool->obj_flags.value[2] * 3 + (GET_PROF_LEVEL(PROF_RANGER, ch) + 2) * 20 / (obj->obj_flags.level + 1);

            if (w_type == TYPE_PIERCE) /* bonus for piercers */
                chance += 10;
            if (tool->obj_flags.value[2] == 0) /* bonus for 0 bulk */
                chance += 5;
        }

        trophy_num = real_object(obj->obj_flags.butcher_item);
        if ((trophy_num < 0) || !(trophy = read_object(trophy_num, REAL))) {
            send_to_char("You could not get anything of value from it.\n\r", ch);
            return;
        }

        /* it's easier to butcher food */
        if (!no_tool && trophy->obj_flags.type_flag == ITEM_FOOD)
            chance = 50 + chance / 2;

        if (number(1, 100) > chance) {
            act("You fumble and waste $P.", FALSE, ch, trophy, obj, TO_CHAR);
            act("$n tries to butcher $P but only spoils it.",
                TRUE, ch, trophy, obj, TO_ROOM);
            extract_obj(trophy);
        } else {
            act("You cut $p from $P.", FALSE, ch, trophy, obj, TO_CHAR);
            act("$n cuts $p from $P.", TRUE, ch, trophy, obj, TO_ROOM);
            obj_to_char(trophy, ch);
        }
        obj->obj_flags.butcher_item = -1;
    }

    if (subcmd == SCMD_SCALP) {
        tool = ch->equipment[WIELD];
        if (tool) {
            w_type = weapon_hit_type(tool->obj_flags.value[3]);
            if ((tool->obj_flags.type_flag != ITEM_WEAPON) || ((w_type != TYPE_SLASH) && (w_type != TYPE_BLUDGEON) && (w_type != TYPE_FLAIL) && (w_type != TYPE_CRUSH) && (w_type != TYPE_SMITE) && (w_type != TYPE_CLEAVE)))
                tool = 0;
        }

        if (!obj->obj_flags.value[4]) {
            send_to_char("It was beheaded already.\n\r", ch);
            return;
        }

        if (!tool) {
            send_to_char("You need a slashing or cleaving weapon to do that.\n\r",
                ch);
            return;
        }

        if ((w_type == TYPE_BLUDGEON) || (w_type == TYPE_FLAIL) || (w_type == TYPE_SMITE) || (w_type == TYPE_CRUSH)) {
            act("With a powerful swing of $p, $n destroys the head of "
                "$P completely.",
                TRUE, ch, tool, obj, TO_ROOM);
            act("With a powerful swing of $p, you destroy the head of "
                "$P completely.",
                TRUE, ch, tool, obj, TO_CHAR);
            trophy = 0;
            obj->obj_flags.value[4] = 0;
            obj->description[strlen(obj->description) - 1] = 0;
            sprintf(buf2, "%s, its head a bloody mess.", obj->description);
            RELEASE(obj->description);
            obj->description = str_dup(buf2);
        } else {
            if (obj->obj_flags.value[4] == 0) {
                send_to_char("It was beheaded already.\n\r", ch);
                return;
            }
            trophy = load_scalp(obj->obj_flags.value[4]);
            obj->obj_flags.value[4] = 0;

            if (!trophy) {
                send_to_char("It has no head to get.\n\r", ch);
                return;
            }
            obj->description[strlen(obj->description) - 1] = 0;
            sprintf(buf2, "%s, its head missing.", obj->description);
            RELEASE(obj->description);
            obj->description = str_dup(buf2);
        }

        if (trophy) {
            act("You behead $P.", FALSE, ch, trophy, obj, TO_CHAR);
            act("$n beheads $P.", TRUE, ch, trophy, obj, TO_ROOM);
            obj_to_char(trophy, ch);
        }
    }
}

/* scalping is changed to beheading, but all the names remain as is */

int generic_scalp = 1122;

obj_data* load_scalp(int number)
{
    int tmp, trophy_num, w_type = 0;
    obj_data* scalp;

    if (number == 0) {
        return 0;
    }
    trophy_num = -1;

    if (number > 0) {
        trophy_num = real_mobile(number);
        w_type = 0;
        // real_mobile is -1 when the mob has since been removed from the world; indexing with it
        // read mob_proto[-1]. Such a scalp falls through to the "old skull" below, as a deleted
        // player's head already does, instead of being lost at login.
        if (trophy_num >= 0 && mob_proto[trophy_num].player.bodytype == 0)
            return 0;
    }
    if (number < 0) {
        for (tmp = 0; tmp <= top_of_p_table; tmp++)
            if (player_table[tmp].idnum == -number)
                break;

        if (tmp > top_of_p_table)
            trophy_num = -1;
        else
            trophy_num = tmp;
        w_type = 1;
    }

    CREATE(scalp, struct obj_data, 1);
    // printf("making corpse from %p\n",ch);
    // printf("corpse created, =%p\n",corpse);
    clear_object(scalp);

    scalp->item_number = real_object(generic_scalp);
    scalp->in_room = NOWHERE;

    scalp->obj_flags.type_flag = ITEM_TRASH;
    scalp->obj_flags.wear_flags = ITEM_TAKE | ITEM_WEAR_BELT;
    scalp->obj_flags.extra_flags = 0;

    scalp->obj_flags.weight = 50;
    scalp->obj_flags.cost_per_day = 1;
    scalp->obj_flags.butcher_item = 0;
    scalp->obj_flags.level = 1;
    scalp->obj_flags.timer = -1;
    scalp->obj_flags.value[4] = number;

    if (trophy_num < 0) {
        scalp->description = str_dup("An old, bleached skull is lying here.");
        scalp->short_description = str_dup("An old skull");
        scalp->name = str_dup("head skull");
    } else {
        scalp->name = str_dup("head");
        sprintf(buf2, "A severed head of %s is lying here.",
            (w_type) ? player_table[trophy_num].name : mob_proto[trophy_num].player.short_descr);
        if (w_type)
            buf2[18] = toupper(buf2[18]);
        scalp->description = str_dup(buf2);

        sprintf(buf2, "A severed head of %s",
            (w_type) ? player_table[trophy_num].name : mob_proto[trophy_num].player.short_descr);
        if (w_type)
            buf2[18] = toupper(buf2[18]);
        scalp->short_description = str_dup(buf2);
    }
    scalp->next = object_list;
    object_list = scalp;

    return scalp;
}
