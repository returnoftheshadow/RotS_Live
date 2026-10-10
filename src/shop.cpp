/* ************************************************************************
 *   File: shop.c                                        Part of CircleMUD *
 *  Usage: spec-procs and other funcs for shops and shopkeepers            *
 *                                                                         *
 *  All rights reserved.  See license.doc for complete information.        *
 *                                                                         *
 *  Copyright (C) 1993 by the Trustees of the Johns Hopkins University     *
 *  CircleMUD is based on DikuMUD, Copyright (C) 1990, 1991.               *
 ************************************************************************ */

#include "platdef.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "comm.h"
#include "db.h"
#include "handler.h"
#include "interpre.h"
#include "structs.h"
#include "utils.h"

#define MAX_TRADE 5
#define MAX_PROD 5
#define CASH_REGEN 100 // cash regen on shopkeeres per mobile
//  activity period, currently 4/tick.

ACMD(do_tell);
ACMD(do_action);
ACMD(do_emote);
ACMD(do_say);
ACMD(do_look);

extern struct index_data* mob_index;

struct shop_data {
    int producing[MAX_PROD]; /* Which item to produce (virtual)      */
    int profit_buy; /* Factor to multiply cost with.        */
    int profit_sell; /* Factor to multiply cost with.        */
    byte type[MAX_TRADE]; /* Which item to trade.                 */
    char* no_such_item1; /* Message if keeper hasn't got an item */
    char* no_such_item2; /* Message if player hasn't got an item */
    char* missing_cash1; /* Message if keeper hasn't got cash    */
    char* missing_cash2; /* Message if player hasn't got cash    */
    char* do_not_buy; /* If keeper dosn't buy such things. 	*/
    char* message_buy; /* Message when player buys item        */
    char* message_sell; /* Message when player sells item       */
    int temper1; /* How does keeper react if no money    */
    int temper2; /* How does keeper react when attacked  */
    int keeper; /* The mobil who owns the shop (virtual)*/
    int material; /* bitvector: materials the shop will buy. 0 = all */
    int in_room; /* Where is the shop?			*/
    int stock_room; /* where it's goods are kept        */
    int open1, open2; /* When does the shop open?		*/
    int close1, close2; /* When does the shop close?		*/
    int bankAccount; /* Store all gold over 15000    */
    char is_open;
};

extern struct room_data world;
extern struct time_info_data time_info;
extern struct char_data* character_list;

struct shop_data* shop_index;
int number_of_shops = 0;

ACMD(do_unlock);
ACMD(do_open);
ACMD(do_lock);
ACMD(do_close);

void closing_time(struct char_data* keeper)
{
    int tmp, dir;
    char_data* tmpch;
    char_data* next_patron;

    for (dir = 0; dir < NUM_OF_DIRS; dir++) {
        if (EXIT(keeper, dir) && IS_SET(EXIT(keeper, dir)->exit_info, EX_ISDOOR)) {
            if (IS_SET(EXIT(keeper, dir)->exit_info, EX_CLOSED)) {
                do_unlock(keeper, EXIT(keeper, dir)->keyword, 0, 0, 0);
                do_open(keeper, EXIT(keeper, dir)->keyword, 0, 0, 0);
            }
            if (!IS_SET(EXIT(keeper, dir)->exit_info, EX_CLOSED) && (EXIT(keeper, dir)->to_room != NOWHERE))
                break;
        }
    }
    for (tmp = 0; tmp < NUM_OF_DIRS; tmp++)
        if (EXIT(keeper, tmp) && !IS_SET(EXIT(keeper, tmp)->exit_info, EX_ISDOOR))
            break;

    if ((dir == NUM_OF_DIRS) || (tmp != NUM_OF_DIRS)) {
        // No valid exit, or exits without doors
        act("$n says 'Sorry, I have to close now.'", FALSE, keeper, 0, 0, TO_ROOM);
        return;
    }

    act("$n tells you 'I am closing. Please leave now.'",
        FALSE, keeper, 0, 0, TO_ROOM);
    for (tmpch = world[keeper->in_room].people; tmpch; tmpch = next_patron) {
        next_patron = tmpch->next_in_room;
        if (tmpch != keeper) {
            act("$n pushed you out.", TRUE, keeper, 0, tmpch, TO_VICT);
            char_from_room(tmpch);
            char_to_room(tmpch, EXIT(keeper, dir)->to_room);
            act("$n is pushed in.", TRUE, tmpch, 0, 0, TO_ROOM);
            do_look(tmpch, "", 0, 0, 0);
        }
    }
    for (tmp = 0; tmp < NUM_OF_DIRS; tmp++)
        if (EXIT(keeper, tmp) && IS_SET(EXIT(keeper, tmp)->exit_info, EX_ISDOOR)) {
            do_close(keeper, EXIT(keeper, tmp)->keyword, 0, 0, 0);
            do_lock(keeper, EXIT(keeper, tmp)->keyword, 0, 0, 0);
        }
}

void opening_time(struct char_data* keeper)
{
    int tmp;
    for (tmp = 0; tmp < NUM_OF_DIRS; tmp++)
        if (EXIT(keeper, tmp) && IS_SET(EXIT(keeper, tmp)->exit_info, EX_ISDOOR)) {
            do_unlock(keeper, EXIT(keeper, tmp)->keyword, 0, 0, 0);
            do_open(keeper, EXIT(keeper, tmp)->keyword, 0, 0, 0);
        }
    act("$n says 'I am open now.'", FALSE, keeper, 0, 0, TO_ROOM);
}

int is_ok(struct char_data* keeper, struct char_data* ch, int shop_nr)
{
    if (keeper && ch) {
        if (IS_AGGR_TO(keeper, ch)) {
            do_say(keeper, "Go away, I won't deal with you!", 0, 0, 0);
            return FALSE;
        }
    }
    if (ch)
        if (IS_SHADOW(ch)) {
            do_say(keeper, "Ugh!  I'm not serving you!", 0, 0, 0);
            return FALSE;
        }

    if ((ch) && (!RP_RACE_CHECK(keeper, ch))) {
        do_say(keeper, "Sorry, I can't serve you!", 0, 0, 0);
        return FALSE;
    }

    if (!shop_index[shop_nr].is_open)
        if (shop_index[shop_nr].open1 > time_info.hours) {
            if (keeper)
                do_say(keeper, "Come back later!", 0, 17, 0);
            return (FALSE);
        } else if (shop_index[shop_nr].close1 <= time_info.hours)
            if (shop_index[shop_nr].open2 > time_info.hours) {
                if (keeper)
                    do_say(keeper, "Sorry, we have closed, but come back later.", 0, 17, 0);
                return (FALSE);
            } else if (shop_index[shop_nr].close2 <= time_info.hours) {
                if (keeper)
                    do_say(keeper, "Sorry, come back tomorrow.", 0, 17, 0);
                return (FALSE);
            };

    if (!keeper)
        return TRUE;

    if (!(CAN_SEE(keeper, ch))) {
        do_say(keeper, "I don't trade with someone I can't see!", 0, 17, 0);
        return (FALSE);
    };

    return TRUE;

    /*   switch (shop_index[shop_nr].with_who) { (with_who now used for material)
   case 0  :
      return(TRUE);
   case 1  :
      return(TRUE);
   default :
      return(TRUE);
   }; */
}

int trade_with(struct obj_data* item, int shop_nr)
{
    int counter;

    if (item->obj_flags.cost < 1)
        return (FALSE);

    if ((shop_index[shop_nr].material) && !IS_SET(shop_index[shop_nr].material, (1 << (item->obj_flags.material))))
        return (FALSE);

    for (counter = 0; counter < MAX_TRADE; counter++)
        if (shop_index[shop_nr].type[counter] == item->obj_flags.type_flag)
            return (TRUE);
    return (FALSE);
}

int shop_producing(struct obj_data* item, int shop_nr)
{
    int counter;

    if (item->item_number < 0)
        return (FALSE);

    for (counter = 0; counter < MAX_PROD; counter++)
        if (shop_index[shop_nr].producing[counter] == item->item_number)
            return (TRUE);
    return (FALSE);
}

int has_already(struct obj_data* item, int shop_nr)
{
    int temp;
    struct obj_data* tobj;

    if (world[shop_index[shop_nr].stock_room].contents)
        for (temp = 1, tobj = world[shop_index[shop_nr].stock_room].contents;
             tobj; tobj = tobj->next_content, temp++)
            if ((tobj->item_number == item->item_number) && (tobj->obj_flags.level < 20))
                return TRUE;
    return FALSE;
}

void shopping_buy(char* arg, struct char_data* ch,
    struct char_data* keeper, int shop_nr)
{
    char argm[100], buf[MAX_STRING_LENGTH];
    struct obj_data* temp1;
    int tmp, objnum;

    if (!(is_ok(keeper, ch, shop_nr)))
        return;

    one_argument(arg, argm);
    if (!(*argm)) {
        sprintf(buf, "%s what do you want to buy??", GET_NAME(ch));
        do_tell(keeper, buf, 0, 19, 0);
        return;
    };
    for (tmp = 0; (argm[tmp] != 0) && (argm[tmp] >= '0') && (argm[tmp] <= '9'); tmp++)
        ;
    if (argm[tmp] <= ' ')
        objnum = atoi(argm);
    else
        objnum = 9999;
    // printf("argm=%s, objnum=%d\n",argm,objnum);
    if (!(temp1 = get_obj_in_list_vis(ch, argm, world[shop_index[shop_nr].stock_room].contents, objnum))) {
        sprintf(buf, shop_index[shop_nr].no_such_item1, GET_NAME(ch));
        do_tell(keeper, buf, 0, 19, 0);
        return;
    }

    if (temp1->obj_flags.cost <= 0) {
        sprintf(buf, shop_index[shop_nr].no_such_item1, GET_NAME(ch));
        do_tell(keeper, buf, 0, 19, 0);
        extract_obj(temp1);
        return;
    }

    if (GET_GOLD(ch) < (int)(temp1->obj_flags.cost * shop_index[shop_nr].profit_buy / 100) /*&& GET_LEVEL(ch) < LEVEL_GOD*/) {
        sprintf(buf, shop_index[shop_nr].missing_cash2, GET_NAME(ch));
        do_tell(keeper, buf, 0, 19, 0);

        switch (shop_index[shop_nr].temper1) {
        case 0:
            do_action(keeper, GET_NAME(ch), 0, 30, 0);
            return;
        case 1:
            do_emote(keeper, "smokes on his joint.", 0, 36, 0);
            return;
        default:
            return;
        }
    }

    if ((IS_CARRYING_N(ch) + 1 > CAN_CARRY_N(ch))) {
        sprintf(buf, "%s : You can't carry that many items.\n\r", fname(temp1->name));
        send_to_char(buf, ch);
        return;
    }

    if ((IS_CARRYING_W(ch) + temp1->obj_flags.weight) > CAN_CARRY_W(ch)) {
        sprintf(buf, "%s : You can't carry that much weight.\n\r", fname(temp1->name));

        send_to_char(buf, ch);
        return;
    }

    act("$n buys $p.", FALSE, ch, temp1, 0, TO_ROOM);

    sprintf(buf, shop_index[shop_nr].message_buy, GET_NAME(ch), money_message((int)(temp1->obj_flags.cost * shop_index[shop_nr].profit_buy / 100)));
    do_tell(keeper, buf, 0, 19, 0);
    sprintf(buf, "You now have %s.\n\r", temp1->short_description);
    send_to_char(buf, ch);
    GET_GOLD(ch) -= (int)(temp1->obj_flags.cost * shop_index[shop_nr].profit_buy / 100);

    GET_GOLD(keeper) += (int)(temp1->obj_flags.cost * shop_index[shop_nr].profit_buy / 100);

    /* If the shopkeeper has more than 15000 coins, put it in the bank! */
    /* disabled because keepers have so many HP now -je */
    if (GET_GOLD(keeper) > 15000) {
        shop_index[shop_nr].bankAccount += (GET_GOLD(keeper) - 15000);
        GET_GOLD(keeper) = 15000;
    }

    /* Test if producing shop ! */
    if (shop_producing(temp1, shop_nr))
        temp1 = read_object(temp1->item_number, REAL);
    else
        obj_from_room(temp1);

    obj_to_char(temp1, ch);

    return;
}

void shopping_sell(char* arg, struct char_data* ch, struct char_data* keeper, int shop_nr)
{
    char argm[100], buf[MAX_STRING_LENGTH];
    struct obj_data* temp1;

    if (!(is_ok(keeper, ch, shop_nr)))
        return;

    one_argument(arg, argm);

    if (!(*argm)) {
        sprintf(buf, "%s What do you want to sell??", GET_NAME(ch));
        do_tell(keeper, buf, 0, 19, 0);
        return;
    }

    if (!(temp1 = get_obj_in_list_vis(ch, argm, ch->carrying, 9999))) {
        sprintf(buf, shop_index[shop_nr].no_such_item2, GET_NAME(ch));
        do_tell(keeper, buf, 0, 19, 0);
        return;
    }
    if ((IS_OBJ_STAT(temp1, ITEM_ANTI_EVIL) && IS_EVIL(keeper)) || (IS_OBJ_STAT(temp1, ITEM_ANTI_GOOD) && IS_GOOD(keeper))) {
        act("$n shakes his head at you.", TRUE, keeper, 0, ch, TO_VICT);
        act("$n shakes his head at $N.", TRUE, keeper, 0, ch, TO_NOTVICT);
        return;
    }

    if (!(trade_with(temp1, shop_nr)) || (temp1->obj_flags.cost < 1)) {
        sprintf(buf, shop_index[shop_nr].do_not_buy, GET_NAME(ch));
        do_tell(keeper, buf, 0, 19, 0);
        return;
    }

    if ((GET_GOLD(keeper) + shop_index[shop_nr].bankAccount) < (int)(temp1->obj_flags.cost * shop_index[shop_nr].profit_sell / 100)) {
        sprintf(buf, shop_index[shop_nr].missing_cash1, GET_NAME(ch));
        do_tell(keeper, buf, 0, 19, 0);
        return;
    }

    act("$n sells $p.", FALSE, ch, temp1, 0, TO_ROOM);

    sprintf(buf, shop_index[shop_nr].message_sell, GET_NAME(ch), money_message((int)(temp1->obj_flags.cost * shop_index[shop_nr].profit_sell / 100)));
    do_tell(keeper, buf, 0, 19, 0);
    sprintf(buf, "The shopkeeper now has %s.\n\r", temp1->short_description);
    send_to_char(buf, ch);
    GET_GOLD(ch) += (int)(temp1->obj_flags.cost * shop_index[shop_nr].profit_sell / 100);

    /* Get money from the bank, buy the obj, then put money back. */
    GET_GOLD(keeper) += shop_index[shop_nr].bankAccount;
    shop_index[shop_nr].bankAccount = 0;

    GET_GOLD(keeper) -= (int)(temp1->obj_flags.cost * shop_index[shop_nr].profit_sell / 100);

    /* If the shopkeeper has more than 15000 coins, put it in the bank! */
    /* disabled since keepers have so many HP now
        if (GET_GOLD(keeper) > 15000) {
           shop_index[shop_nr].bankAccount += (GET_GOLD(keeper) - 15000);
           GET_GOLD(keeper) = 15000;
        }
*/
    /*   if ((get_obj_in_list(argm, world[shop_index[shop_nr].stock_room].contents)) */
    if (shop_producing(temp1, shop_nr) || (GET_ITEM_TYPE(temp1) == ITEM_TRASH) || (has_already(temp1, shop_nr)))
        extract_obj(temp1);
    else {
        obj_from_char(temp1);
        obj_to_room(temp1, shop_index[shop_nr].stock_room);
    }

    return;
}

void shopping_value(char* arg, struct char_data* ch,
    struct char_data* keeper, int shop_nr)
{
    char argm[100], buf[MAX_STRING_LENGTH];
    struct obj_data* temp1;

    if (!(is_ok(keeper, ch, shop_nr)))
        return;

    one_argument(arg, argm);
    if (!(*argm)) {
        sprintf(buf, "%s What do you want me to valuate??", GET_NAME(ch));
        // printf("buff:%s.\n",buf);
        do_tell(keeper, buf, 0, 19, 0);
        return;
    }

    if (!(temp1 = get_obj_in_list_vis(ch, argm, ch->carrying, 9999))) {
        // printf("no such item\n");
        sprintf(buf, shop_index[shop_nr].no_such_item2, GET_NAME(ch));
        do_tell(keeper, buf, 0, 19, 0);
        return;
    }

    if (!(trade_with(temp1, shop_nr))) {
        // printf("no trade with you\n");
        sprintf(buf, shop_index[shop_nr].do_not_buy, GET_NAME(ch));
        do_tell(keeper, buf, 0, 19, 0);
        return;
    }
    // printf("numbers: cost:%d, profit:%d\n",temp1->obj_flags.cost, shop_index[shop_nr].profit_sell);
    sprintf(buf, "%s I'll give you %s for that!", GET_NAME(ch), money_message((int)(temp1->obj_flags.cost * shop_index[shop_nr].profit_sell / 100)));
    do_tell(keeper, buf, 0, 19, 0);

    return;
}

void shopping_list(char* arg, struct char_data* ch,
    struct char_data* keeper, int shop_nr)
{
    char buf[MAX_STRING_LENGTH], buf2[100], buf3[100];
    struct obj_data* temp1;
    extern char* drinks[];
    int found_obj;
    int count;

    if (!(is_ok(keeper, ch, shop_nr)))
        return;
    while (*arg && (*arg <= ' '))
        arg++;

    strcpy(buf, "You can buy:\n\r");
    found_obj = FALSE;
    if (world[shop_index[shop_nr].stock_room].contents)
        for (count = 1, temp1 = world[shop_index[shop_nr].stock_room].contents;
             temp1; temp1 = temp1->next_content, count++)
            if ((CAN_SEE_OBJ(ch, temp1)) && (temp1->obj_flags.cost > 0)) {

                found_obj = TRUE;
                if (temp1->obj_flags.type_flag != ITEM_DRINKCON)
                    sprintf(buf2, "%d. %s for %s.\n\r",
                        count, (temp1->short_description), money_message((int)((long)temp1->obj_flags.cost * shop_index[shop_nr].profit_buy / 100)));
                else {
                    if (temp1->obj_flags.value[1])
                        sprintf(buf3, "%d. %s of %s", count,
                            (temp1->short_description),
                            drinks[temp1->obj_flags.value[2]]);
                    else
                        sprintf(buf3, "%d. %s", count, (temp1->short_description));
                    sprintf(buf2, "%s for %s.\n\r", buf3, money_message((int)((long)temp1->obj_flags.cost * shop_index[shop_nr].profit_buy / 100), 0));
                }
                if (!*arg || isname(arg, temp1->name)) {
                    CAP(buf2);
                    strcat(buf, buf2);
                }
            };

    if (!found_obj)
        strcat(buf, "Nothing!\n\r");
    send_to_char(buf, ch);
    return;
}

void shopping_kill(char* arg, struct char_data* ch,
    struct char_data* keeper, int shop_nr)
{
    char buf[100];

    switch (shop_index[shop_nr].temper2) {
    case 0:
        sprintf(buf, "%s Don't ever try that again!", GET_NAME(ch));
        do_tell(keeper, buf, 0, 19, 0);
        return;

    case 1:
        sprintf(buf, "%s Scram - midget!", GET_NAME(ch));
        do_tell(keeper, buf, 0, 19, 0);
        return;

    default:
        return;
    }
}

// int	shop_keeper(struct char_data *ch, int cmd, char *arg)
SPECIAL(shop_keeper)
{
    struct char_data* temp_char;
    struct char_data* keeper;
    int shop_nr;
    waiting_type tmpwtl;

    keeper = 0;
    /*
    for (temp_char = world[ch->in_room].people; (!keeper) && (temp_char) ; temp_char = temp_char->next_in_room)
    if (IS_MOB(temp_char))
    if (mob_index[temp_char->nr].func == shop_keeper)
    */
    keeper = temp_char;
    keeper = host;
    for (shop_nr = 0; shop_index[shop_nr].keeper != keeper->nr; shop_nr++)
        ;
    if (callflag == SPECIAL_DEATH) {
        act("With $s last breath, $n bequeathes all $s money to charity.",
            FALSE, host, 0, 0, TO_ROOM);
        GET_GOLD(host) = 0;
    }
    if (callflag == SPECIAL_SELF) {
        GET_GOLD(keeper) += CASH_REGEN;
    }

    if (callflag == SPECIAL_DAMAGE) {
        tmpwtl.targ1.set_character(ch);
        tmpwtl.targ2.ptr.text = get_from_txt_block_pool("Don't even think about it.");
        tmpwtl.targ2.type = TARGET_TEXT;
        tmpwtl.cmd = CMD_TELL;
        do_tell(host, "", &tmpwtl, CMD_TELL, 0);
        tmpwtl.targ1.cleanup();
        tmpwtl.targ2.cleanup();

        return 1;
    }

    if (((shop_index[shop_nr].open1 == time_info.hours) || (shop_index[shop_nr].open2 == time_info.hours)) && (cmd <= 0) && !(shop_index[shop_nr].is_open)) {
        opening_time(keeper);
        shop_index[shop_nr].is_open = 1;
    }
    //     printf("closing, local time:%d, close1=%d, close2=%d,flag=%d\n",
    //	    time_info.hours,shop_index[shop_nr].close1,shop_index[shop_nr].close2,shop_index[shop_nr].is_open);
    if ((((shop_index[shop_nr].close1 == time_info.hours) && ((shop_index[shop_nr].close1 % 24) != (shop_index[shop_nr].open2 % 24)))
            || ((shop_index[shop_nr].close2 == time_info.hours) && ((shop_index[shop_nr].close2 % 24) != (shop_index[shop_nr].open1 % 24))))
        && (cmd <= 0) && (shop_index[shop_nr].is_open)) {
        closing_time(keeper);
        shop_index[shop_nr].is_open = 0;
    }

    if ((cmd == CMD_BUY) && (ch->in_room == real_room(shop_index[shop_nr].in_room))) { /* Buy */

        shopping_buy(arg, ch, keeper, shop_nr);
        return (TRUE);
    }

    if ((cmd == CMD_SELL) && (ch->in_room == real_room(shop_index[shop_nr].in_room))) { /* Sell */

        shopping_sell(arg, ch, keeper, shop_nr);
        return (TRUE);
    }

    if ((cmd == CMD_VALUE) && (ch->in_room == real_room(shop_index[shop_nr].in_room))) { /* value */

        shopping_value(arg, ch, keeper, shop_nr);
        return (TRUE);
    }

    if ((cmd == CMD_LIST) && (ch->in_room == real_room(shop_index[shop_nr].in_room))) { /* List */

        shopping_list(arg, ch, keeper, shop_nr);
        return (TRUE);
    }

    //    if ((cmd == CMD_KILL) || (cmd == CMD_HIT) ||
    //        (cmd == CMD_BASH) || (cmd == CMD_KICK))/* Kill or Hit or Bash or Kick*/{
    //      one_argument(arg, argm);

    //       if (keeper == get_char_room(argm, ch->in_room)) {
    // 	 shopping_kill(arg, ch, keeper, shop_nr);
    // 	 return(TRUE);
    //       }
    //    } else if ((cmd == CMD_CAST) || (cmd == CMD_RECITE) || (cmd == CMD_USE)) {   /* Cast, recite, use */
    //       act("$N tells you 'No magic here - kid!'.", FALSE, ch, 0, keeper, TO_CHAR);
    //       return TRUE;
    //    }

    return (FALSE);
}

void boot_the_shops(FILE* shop_f, char* filename)
{
    char *buf, buf2[150];
    char* tmpbuf;
    int temp, count, nr;

    sprintf(buf2, "beginning of shop file %s", filename);

    for (;;) {
        buf = fread_string(shop_f, buf2);
        for (tmpbuf = buf; (*tmpbuf != 0) && (*tmpbuf < ' '); tmpbuf++)
            ;

        if (*tmpbuf == '#') /* a new shop */ {
            if (!number_of_shops) /* first shop */
                CREATE(shop_index, struct shop_data, 1);
            else if (!(shop_index = (struct shop_data*)realloc(shop_index, (number_of_shops + 1) * sizeof(struct shop_data)))) {
                perror("Error in boot shop\n");
                exit(0);
            }

            sscanf(tmpbuf, "#%d", &nr);
            sprintf(buf2, "shop #%d in shop file %s", nr, filename);

            for (count = 0; count < MAX_PROD; count++) {
                fscanf(shop_f, "%d", &temp);
                if (temp >= 0)
                    shop_index[number_of_shops].producing[count] = real_object(temp);
                else
                    shop_index[number_of_shops].producing[count] = temp;
            }
            fscanf(shop_f, "%d", &shop_index[number_of_shops].profit_buy);
            fscanf(shop_f, "%d", &shop_index[number_of_shops].profit_sell);
            for (count = 0; (count < MAX_TRADE) /*&& (temp > 0)*/; count++) {
                fscanf(shop_f, "%d", &temp);
                shop_index[number_of_shops].type[count] = (byte)temp;
            }
            /*	 for( ; count < MAX_TRADE ; count++ )
            shop_index[number_of_shops].type[count] =  -1;
            */
            shop_index[number_of_shops].no_such_item1 = fread_string(shop_f, buf2);
            shop_index[number_of_shops].no_such_item2 = fread_string(shop_f, buf2);
            shop_index[number_of_shops].do_not_buy = fread_string(shop_f, buf2);
            shop_index[number_of_shops].missing_cash1 = fread_string(shop_f, buf2);
            shop_index[number_of_shops].missing_cash2 = fread_string(shop_f, buf2);
            shop_index[number_of_shops].message_buy = fread_string(shop_f, buf2);
            shop_index[number_of_shops].message_sell = fread_string(shop_f, buf2);
            fscanf(shop_f, "%d", &shop_index[number_of_shops].temper1);
            fscanf(shop_f, "%d", &shop_index[number_of_shops].temper2);
            fscanf(shop_f, "%d", &shop_index[number_of_shops].keeper);

            shop_index[number_of_shops].keeper = real_mobile(shop_index[number_of_shops].keeper);

            fscanf(shop_f, "%d", &shop_index[number_of_shops].material);
            fscanf(shop_f, "%d", &shop_index[number_of_shops].in_room);
            fscanf(shop_f, "%d", &shop_index[number_of_shops].stock_room);
            fscanf(shop_f, "%d", &shop_index[number_of_shops].open1);
            fscanf(shop_f, "%d", &shop_index[number_of_shops].close1);
            fscanf(shop_f, "%d", &shop_index[number_of_shops].open2);
            fscanf(shop_f, "%d", &shop_index[number_of_shops].close2);

            shop_index[number_of_shops].bankAccount = 0;
            shop_index[number_of_shops].is_open = is_ok(0, 0, number_of_shops);
            number_of_shops++;
        } else if (*tmpbuf == '$') /* EOF */
            break;
    }
}

void assign_the_shopkeepers(void)
{
    int temp1, virt1, count;
    struct obj_data* tmp_obj;

    for (temp1 = 0; temp1 < number_of_shops; temp1++) {
        mob_index[shop_index[temp1].keeper].func = shop_keeper;
        virt1 = mob_index[shop_index[temp1].keeper].virt;
        virt1 = real_mobile(virt1);
        shop_index[temp1].stock_room = real_room(shop_index[temp1].stock_room);
        for (count = 0; count < MAX_PROD; count++) {
            if (shop_index[temp1].producing[count] >= 0) {
                tmp_obj = read_object(shop_index[temp1].producing[count], REAL);
                obj_to_room(tmp_obj, shop_index[temp1].stock_room);
            }
        }
    }
}
