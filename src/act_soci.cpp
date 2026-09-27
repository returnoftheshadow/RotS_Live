/* ************************************************************************
 *   File: act.social.c                                  Part of CircleMUD *
 *  Usage: Functions to handle socials                                     *
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
#include "spells.h"
#include "structs.h"
#include "utils.h"

/* extern variables */
extern struct room_data world;
extern struct descriptor_data* descriptor_list;

/* extern functions */
void parse_string(char* input, char* output, struct char_data* ch1,
    struct char_data* ch2, struct char_data* to);
int action(int cmd);
char* fread_action(FILE* fl, int nr);

/* local globals */
int social_list_top = -1;
int social_command_number = -1;
// this social_command_number is a lame connection with
// command_interpreter, to allow command interception etc

struct social_messg* soc_mess_list = 0;

char* fread_action(FILE* fl, int nr)
{
    char buf[MAX_STRING_LENGTH], *rslt;

    fgets(buf, MAX_STRING_LENGTH, fl);
    if (feof(fl)) {
        sprintf(buf, "SYSERR: fread_action - unexpected EOF near action #%d", nr);
        log(buf);
        exit(0);
    }

    if (*buf == '#')
        return (0);
    else {
        *(buf + strlen(buf) - 1) = '\0';
        CREATE(rslt, char, strlen(buf) + 1);
        strcpy(rslt, buf);
        return (rslt);
    }
}

void boot_social_messages(void)
{
    FILE* fl;
    int nr = 0, tmp, hide, min_pos, char_pos, len1, len2;
    struct social_messg tmpsoc;
    char str[MAX_STRING_LENGTH];
    if (!(fl = fopen(SOCMESS_FILE, "r"))) {
        perror("boot_social_messages");
        exit(0);
    }

    tmp = -1;
    for (;;) {
        buf[0] = '-';
        fscanf(fl, " %s ", buf);
        fprintf(stderr, "reading command %s\n", buf);
        //      if (*buf == '-')
        //	 break;
        //      fscanf(fl, " %d ", &hide);
        //      fscanf(fl, " %d \n", &min_pos);
        fgets(str, MAX_STRING_LENGTH, fl);
        min_pos = 0;
        char_pos = POSITION_RESTING;
        sscanf(str, "%d %d %d \n", &hide, &min_pos, &char_pos);
        if (*buf == '-')
            break;
        /* alloc a new cell */
        if (!soc_mess_list) {
            CREATE(soc_mess_list, struct social_messg, 1);
            social_list_top = 0;
        } else {
            RECREATE(soc_mess_list, struct social_messg, (++social_list_top + 1), social_list_top);
            if (!(soc_mess_list)) {
                perror("boot_social_messages. realloc");
                exit(1);
            }
        }
        if (!tmp) {
            if (!social_list_top)
                fprintf(stderr, "Format error near beginning of socials file.\n");
            else
                fprintf(stderr, "Format error in social file near social #%s.\n",
                    soc_mess_list[social_list_top - 1].command);
            exit(1);
        }
        /*
      if (social_list_top && soc_mess_list[social_list_top-1].act_nr > tmp) {
         fprintf(stderr, "Format error or out-of-order social in socials file near #%d.\n",
             soc_mess_list[social_list_top-1].act_nr);
         exit(1);
      }
      */

        /* read the stuff */
        // buf was read above, with the command itself.
        CREATE(soc_mess_list[social_list_top].command, char, strlen(buf) + 1);
        strcpy(soc_mess_list[social_list_top].command, buf);
        //      printf("social loaded, %s\n",buf);
        //      soc_mess_list[social_list_top].act_nr = tmp;
        soc_mess_list[social_list_top].hide = hide;
        soc_mess_list[social_list_top].min_victim_position = min_pos;
        soc_mess_list[social_list_top].min_actor_position = char_pos;

        soc_mess_list[social_list_top].char_no_arg = fread_action(fl, nr);
        soc_mess_list[social_list_top].others_no_arg = fread_action(fl, nr);
        soc_mess_list[social_list_top].char_found = fread_action(fl, nr);

        /* if no char_found, the rest is to be ignored */
        if (!soc_mess_list[social_list_top].char_found)
            continue;

        soc_mess_list[social_list_top].others_found = fread_action(fl, nr);
        soc_mess_list[social_list_top].vict_found = fread_action(fl, nr);
        soc_mess_list[social_list_top].not_found = fread_action(fl, nr);
        soc_mess_list[social_list_top].char_auto = fread_action(fl, nr);
        soc_mess_list[social_list_top].others_auto = fread_action(fl, nr);
    }
    fclose(fl);

    /* sorting socials */

    for (nr = 0; nr < social_list_top; nr++)
        for (tmp = 0; tmp < social_list_top; tmp++) {
            len1 = strlen(soc_mess_list[tmp].command);
            len2 = strlen(soc_mess_list[tmp + 1].command);
            if (len1 > len2)
                len1 = len2;

            if (strncmp(soc_mess_list[tmp].command,
                    soc_mess_list[tmp + 1].command, len1)
                > 0) {
                tmpsoc = soc_mess_list[tmp];
                soc_mess_list[tmp] = soc_mess_list[tmp + 1];
                soc_mess_list[tmp + 1] = tmpsoc;
            }
        }
    //   printf("sorted list of socials:\n");
    //   for(nr = 0; nr < social_list_top; nr++)
    //     printf("%s\n",soc_mess_list[nr].command);
}

int find_action_old(char* arg)
{
    int tmp, space_pos;
    char space_char;

    tmp = strlen(arg);
    for (space_pos = 0; space_pos < tmp; space_pos++)
        if (arg[space_pos] <= ' ')
            break;
    space_char = arg[space_pos];
    arg[space_pos] = 0;

    for (tmp = 0; tmp <= social_list_top; tmp++)
        if (!strncmp(soc_mess_list[tmp].command, arg, strlen(arg)))
            break;

    arg[space_pos] = space_char;

    if (tmp > social_list_top)
        return -1;
    return tmp;
}

int find_action(char* arg)
{
    int bot, top, mid, len;

    bot = 0;
    top = social_list_top;
    len = strlen(arg);

    if (top < 0)
        return -1;

    for (;;) {
        mid = (bot + top) / 2;

        if (strncmp(soc_mess_list[mid].command, arg, len) == 0)
            return mid;
        if (bot >= top)
            return -1;

        if (strncmp(soc_mess_list[mid].command, arg, len) > 0)
            top = --mid;
        else
            bot = ++mid;
    }
    return -1;
}

char action_arg[MAX_INPUT_LENGTH];

int social_parser(char_data* ch, char* argument, waiting_type* wtl)
/*** returns the social info in wtl, and 1/0 if success/fail ***/
{
    char* arg;
    int act_nr, space_pos;
    char space_char;

    strcpy(action_arg, argument);
    arg = action_arg;

    while (*arg && *arg <= ' ')
        arg++;

    for (space_pos = 0; *(arg + space_pos) > ' '; space_pos++)
        ;

    space_char = *(arg + space_pos);
    *(arg + space_pos) = 0;

    if ((act_nr = find_action(arg)) < 0) {
        social_command_number = -1;
        return 0;
    }
    wtl->subcmd = act_nr;

    *(arg + space_pos) = space_char;
    while (*arg && *arg > ' ')
        arg++;

    one_argument(arg, buf);

    wtl->targ1.ptr.ch = get_char_room_vis(ch, buf);

    if (wtl->targ1.ptr.ch) {
        wtl->targ1.type = TARGET_CHAR;
        wtl->targ1.ch_num = wtl->targ1.ptr.ch->abs_number;
    } else if (!*buf)
        wtl->targ1.type = TARGET_NONE;
    else
        wtl->targ1.type = TARGET_OTHER;

    return 1;
}

void report_wrong_position(char_data* ch);

ACMD(do_action)
{
    int act_nr;
    struct social_messg* action;
    struct char_data* vict;
    waiting_type tmpwtl;

    if (wtl && (wtl->cmd == CMD_SOCIAL)) {
        act_nr = wtl->subcmd;

        if ((wtl->targ1.type == TARGET_CHAR) && char_exists(wtl->targ1.ch_num))
            vict = wtl->targ1.ptr.ch;
        else
            vict = 0;
    } else {
        if (!social_parser(ch, argument, &tmpwtl))
            return;
        act_nr = tmpwtl.subcmd;

        if ((tmpwtl.targ1.type == TARGET_CHAR) && char_exists(tmpwtl.targ1.ch_num))
            vict = tmpwtl.targ1.ptr.ch;
        else
            vict = 0;
    }

    social_command_number = act_nr;

    action = &soc_mess_list[act_nr];

    if (GET_POS(ch) < action->min_actor_position) {
        report_wrong_position(ch);
        return;
    }

    if (!vict) {
        send_to_char(action->char_no_arg, ch);
        send_to_char("\n\r", ch);
        act(action->others_no_arg, action->hide, ch, 0, 0, TO_ROOM);
        return;
    }

    if (vict == ch) {
        send_to_char(action->char_auto, ch);
        send_to_char("\n\r", ch);
        act(action->others_auto, action->hide, ch, 0, 0, TO_ROOM);
    } else {
        if (GET_POS(vict) < action->min_victim_position)
            act("$N is not in a proper position for that.", FALSE, ch, 0, vict, TO_CHAR);
        else {
            act(action->char_found, 0, ch, 0, vict, TO_CHAR);
            act(action->others_found, action->hide, ch, 0, vict, TO_NOTVICT);
            act(action->vict_found, action->hide, ch, 0, vict, TO_VICT);
        }
    }
}

ACMD(do_insult)
{
    struct char_data* victim;

    one_argument(argument, arg);

    if (*arg) {
        if (!(victim = get_char_room_vis(ch, arg)))
            send_to_char("Can't hear you!\n\r", ch);
        else {
            if (victim != ch) {
                sprintf(buf, "You insult %s.\n\r", PERS(victim, ch, FALSE, FALSE));
                send_to_char(buf, ch);

                switch (number(0, 2)) {
                case 0:
                    if (GET_SEX(ch) == SEX_MALE) {
                        if (GET_SEX(victim) == SEX_MALE)
                            act("$n accuses you of fighting like a woman!", FALSE,
                                ch, 0, victim, TO_VICT);
                        else
                            act("$n says that women can't fight.",
                                FALSE, ch, 0, victim, TO_VICT);
                    } else { /* Ch == Woman */
                        if (GET_SEX(victim) == SEX_MALE)
                            act("$n accuses you of having the smallest... (brain?)",
                                FALSE, ch, 0, victim, TO_VICT);
                        else
                            act("$n tells you that you'd loose a beauty contest against a troll.",
                                FALSE, ch, 0, victim, TO_VICT);
                    }
                    break;
                case 1:
                    act("$n calls your mother a bitch!", FALSE, ch, 0, victim, TO_VICT);
                    break;
                default:
                    act("$n tells you to get lost!", FALSE, ch, 0, victim, TO_VICT);
                    break;
                } /* end switch */

                act("$n insults $N.", TRUE, ch, 0, victim, TO_NOTVICT);
            } else /* ch == victim */
                send_to_char("You feel insulted.\n\r", ch);
        }
    } else
        send_to_char("I'm sure you don't want to insult *everybody*...\n\r", ch);
}
