/**************************************************************************
 *   File: act.comm.c                                    Part of CircleMUD *
 *  Usage: Player-level communication commands                             *
 *                                                                         *
 *  All rights reserved.  See license.doc for complete information.        *
 *                                                                         *
 *  Copyright (C) 1993 by the Trustees of the Johns Hopkins University     *
 *  CircleMUD is based on DikuMUD, Copyright (C) 1990, 1991.               *
 **************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "big_brother.h"
#include "char_utils.h"
#include "color.h"
#include "comm.h"
#include "db.h"
#include "handler.h"
#include "interpre.h"
#include "script.h"
#include "spells.h"
#include "structs.h"
#include "utils.h"

extern struct room_data world;
extern struct descriptor_data* descriptor_list;
extern int top_of_world;

ACMD(do_petitio)
{
    send_to_char("You must type 'petition' - no less - to petition.\r\n", ch);
}

void say_to_char(struct char_data* speaker, struct char_data* aud,
    const char* color, const char* action,
    const char* end, char* text, int force_visible)
{
    char speech[600];
    int freq, l;

    l = speaker->player.language;

    if (l) {
        if (IS_NPC(speaker))
            freq = 100;
        else
            freq = GET_RAW_KNOWLEDGE(speaker, l);

        if (!IS_NPC(aud))
            freq = freq * GET_RAW_KNOWLEDGE(aud, l) / 100;
    } else
        freq = 100;

    sprintf(speech, "%s%s %ss '",
        color,
        force_visible ? "$K" : "$N",
        action);

    if (strlen(speech) > 100) {
        mudlog("SYSERR: say_to_char header overflow",
            NRM, LEVEL_IMMORT, FALSE);
        speech[99] = 0;
    }

    if (freq >= 100)
        strcpy(speech + strlen(speech), text);
    else
        strcpy_lang(speech + strlen(speech), text, freq, 499);

    if (strlen(text) > 499)
        strcpy(speech + strlen(speech), text + 499);

    strcat(speech, "'");
    strcat(speech, end);
    act(speech, FALSE, aud, 0, speaker, TO_CHAR);
}

ACMD(do_say)
{
    int i;
    struct char_data* s;
    char* talk_line;

    if (IS_NPC(ch) && (GET_INT(ch) < 6)) {
        send_to_char("You are too stupid to talk.\n\r", ch);
        return;
    }

    if (!wtl || (wtl->targ1.type != TARGET_TEXT)) {
        for (i = 0; *(argument + i) == ' '; i++)
            continue;
        talk_line = argument + i;
    } else
        talk_line = wtl->targ1.ptr.text->text;

    if (!*talk_line)
        send_to_char("Yes, but WHAT do you want to say?\n\r", ch);
    else {
        for (s = world[ch->in_room].people; s; s = s->next_in_room)
            if (s->desc && s->desc->descriptor && s != ch && GET_POS(s) > POSITION_SLEEPING && !PLR_FLAGGED(s, PLR_WRITING))
                say_to_char(ch, s, "$CS", "say", "", talk_line, FALSE);

        if (PRF_FLAGGED(ch, PRF_ECHO)) {
            sprintf(buf, "%sYou say '%s'%s",
                CC_USE(ch, COLOR_SAY), talk_line, CC_NORM(ch));
            act(buf, FALSE, ch, 0, 0, TO_CHAR);
        } else
            send_to_char("Ok.\n\r", ch);

        for (s = world[ch->in_room].people; s; s = s->next_in_room)
            if (IS_NPC(s) && (s != ch))
                call_trigger(ON_HEAR_SAY, s, ch, talk_line);
    }
}

void convert_string(const char* str, int hide_invisible, struct char_data* ch,
    struct obj_data* obj, void* vict_obj,
    struct char_data* to, const char* buf);
extern char act_buffer[MAX_STRING_LENGTH];

ACMD(do_gsay)
{
    int message_index = 0;
    for (message_index = 0; *(argument + message_index) == ' '; message_index++)
        ;

    group_data* group = ch->group;
    if (group == NULL) {
        send_to_char("But you are not the member of a group!\n\r", ch);
        return;
    }

    if (!*(argument + message_index)) {
        send_to_char("Yes, but WHAT do you want to group-say?\n\r", ch);
    } else {
        // Send the message to the group.
        sprintf(buf, "$CG%s group-says '%s'\n\r", GET_NAME(ch), argument + message_index);
        for (char_iter iter = group->begin(); iter != group->end(); ++iter) {
            char_data* member = *iter;
            if (member != ch) {
                act(buf, FALSE, ch, NULL, member, TO_VICT);
                // send_to_char(buf, member);
            }
        }

        // Alert the player that their message has been sent.
        if (PRF_FLAGGED(ch, PRF_ECHO)) {
            sprintf(buf, "$CGYou group-say '%s'\n\r", argument + message_index);
            char* str = act_buffer;
            // Had to mimic act() but can't use it because of position check and
            // send_to_char won't do coloring.
            convert_string(buf, FALSE, ch, NULL, NULL, ch, str);
            SEND_TO_Q(str, ch->desc);
        } else {
            send_to_char("Ok.\n\r", ch);
        }

        // Other people in the room hear this as well.
        sprintf(buf, "$CS$n says '%s'\n\r", argument + message_index);
        for (char_data* bystander = world[ch->in_room].people; bystander; bystander = bystander->next_in_room) {
            if (group != bystander->group && utils::is_pc(*bystander)) {
                act(buf, FALSE, ch, 0, bystander, TO_VICT);
            }
        }
    }
}

ACMD(do_tell)
{
    struct char_data* vict;
    char* tmpstr;
    char* talk_line;

    if (IS_NPC(ch) && MOB_FLAGGED(ch, MOB_PET) && !(MOB_FLAGGED(ch, MOB_ORC_FRIEND))) {
        send_to_char("Sorry, tamed mobiles can't do that.\n\r", ch);
        return;
    }

    if (IS_SHADOW(ch)) {
        send_to_char("Sorry, you cannot tell in shadow form.\n\r", ch);
        return;
    }

    //   printf("do_tell, arg=%s.\n",argument);
    vict = 0;
    if (subcmd == SCMD_REPLY) {
        if (IS_NPC(ch)) {
            send_to_char("Mobiles need not reply.\n\r", ch);
            return;
        }
        if (!ch->specials.union1.reply_ptr || !char_exists(ch->specials.union2.reply_number))
            vict = 0;
        else
            vict = ch->specials.union1.reply_ptr;
        if (vict && vict->desc && vict->desc->connected)
            vict = 0;

        if (!wtl || (wtl->targ1.type != TARGET_TEXT)) {
            for (talk_line = argument; *talk_line && (*talk_line <= ' ');
                 talk_line++)
                ;
        } else
            talk_line = wtl->targ1.ptr.text->text;

        if (!*talk_line) {
            //       send_to_char("What do you want to reply with?\n\r",ch);
            if (!vict)
                send_to_char("You don't have anybody to reply to.\n\r", ch);
            else
                act("You would reply to $N.", FALSE, ch, 0, vict, TO_CHAR);

            return;
        }
        strcpy(buf2, talk_line);
    } else if (subcmd == SCMD_TELL) {

        if (!wtl || (wtl->targ1.type != TARGET_CHAR) || (wtl->targ2.type != TARGET_TEXT)) {

            half_chop(argument, buf, buf2);
            if (!*buf || !*buf2) {
                send_to_char("Who do you wish to tell what??\n\r", ch);
                return;
            }
            //     vict = get_char_vis(ch, buf);
            vict = get_char(buf);
        } else {
            vict = wtl->targ1.ptr.ch;
            tmpstr = wtl->targ2.ptr.text->text;
            strcpy(buf2, tmpstr);
        }
    } else
        return;

    if (!vict)
        return;

    if (other_side(ch, vict) || (!CAN_SEE(ch, vict, 1)) && (subcmd != SCMD_REPLY))
        send_to_char("Nobody by that name.\n\r", ch);
    else if (ch == vict)
        send_to_char("You try to tell yourself something.\n\r", ch);
    else if (!IS_NPC(ch) && PRF_FLAGGED(ch, PRF_NOTELL))
        send_to_char("You can't tell other people while you have notell on.\n\r", ch);
    else if (!IS_NPC(vict) && !vict->desc->descriptor) /* linkless */
        act("$E's linkless at the moment.", FALSE, ch, 0, vict, TO_CHAR);
    else if (PLR_FLAGGED(vict, PLR_WRITING))
        act("$E's writing a message right now, try again later.",
            FALSE, ch, 0, vict, TO_CHAR);
    else if ((GET_POS(vict) <= POSITION_SLEEPING) || (!IS_NPC(vict) && PRF_FLAGGED(vict, PRF_NOTELL)))
        act("$E can't hear you.", FALSE, ch, 0, vict, TO_CHAR);
    else {
        if (PLR_FLAGGED(vict, PLR_ISAFK))
            act("$E is away from keyboard.", FALSE, ch, 0, vict, TO_CHAR);

        sprintf(buf, "$CT$n tells you '%s'", buf2);
        act(buf, FALSE, ch, NULL, vict, TO_VICT);

        if (PRF_FLAGGED(ch, PRF_ECHO)) {
            sprintf(buf, "$CTYou tell $N '%s'", buf2);
            act(buf, FALSE, ch, 0, vict, TO_CHAR);
        } else
            send_to_char("Ok.\n\r", ch);

        if (!IS_NPC(vict) && !IS_NPC(ch)) {
            vict->specials.union1.reply_ptr = ch;
            vict->specials.union2.reply_number = ch->abs_number;
        }
    }
}

ACMD(do_whisper)
{
    struct char_data* vict;

    half_chop(argument, buf, buf2);

    if (!*buf || !*buf2)
        send_to_char("Who do you want to whisper to.. and what??\n\r", ch);
    else if (!(vict = get_char_room_vis(ch, buf)))
        send_to_char("No-one by that name here..\n\r", ch);
    else if (vict == ch) {
        act("$n whispers quietly to $mself.", FALSE, ch, 0, 0, TO_ROOM);
        send_to_char("You can't seem to get your mouth close enough to your ear...\n\r", ch);
    } else {
        sprintf(buf, "$n whispers to you, '%s'", buf2);
        act(buf, FALSE, ch, 0, vict, TO_VICT);
        send_to_char("Ok.\n\r", ch);
        act("$n whispers something to $N.", FALSE, ch, 0, vict, TO_NOTVICT);
    }
}

ACMD(do_ask)
{
    struct char_data* vict;

    half_chop(argument, buf, buf2);

    if (!*buf || !*buf2)
        send_to_char("Who do you want to ask something.. and what??\n\r", ch);
    else if (!(vict = get_char_room_vis(ch, buf)))
        send_to_char("No-one by that name here..\n\r", ch);
    else if (vict == ch) {
        act("$n quietly asks $mself a question.", FALSE, ch, 0, 0, TO_ROOM);
        send_to_char("You think about it for a while...\n\r", ch);
    } else {
        sprintf(buf, "$n asks you '%s'", buf2);
        act(buf, FALSE, ch, 0, vict, TO_VICT);
        send_to_char("Ok.\n\r", ch);
        act("$n asks $N a question.", FALSE, ch, 0, vict, TO_NOTVICT);
    }
}

#define MAX_NOTE_LENGTH 1000 /* arbitrary */

ACMD(do_write)
{
    struct obj_data *paper = 0, *pen = 0;
    char *papername, *penname;

    papername = buf1;
    penname = buf2;

    argument_interpreter(argument, papername, penname);

    if (!ch->desc)
        return;

    if (!*papername) /* nothing was delivered */ {
        send_to_char("Write?  With what?  ON what?  What are you trying to do?!?\n\r", ch);
        return;
    }
    if (*penname) /* there were two arguments */ {
        if (!(paper = get_obj_in_list_vis(ch, papername, ch->carrying, 9999))) {
            sprintf(buf, "You have no %s.\n\r", papername);
            send_to_char(buf, ch);
            return;
        }
        if (!(pen = get_obj_in_list_vis(ch, penname, ch->carrying, 9999))) {
            sprintf(buf, "You have no %s.\n\r", papername);
            send_to_char(buf, ch);
            return;
        }
    } else /* there was one arg.let's see what we can find */ {
        if (!(paper = get_obj_in_list_vis(ch, papername, ch->carrying, 9999))) {
            sprintf(buf, "There is no %s in your inventory.\n\r", papername);
            send_to_char(buf, ch);
            return;
        }
        if (paper->obj_flags.type_flag == ITEM_PEN) /* oops, a pen.. */ {
            pen = paper;
            paper = 0;
        } else if (paper->obj_flags.type_flag != ITEM_NOTE) {
            send_to_char("That thing has nothing to do with writing.\n\r", ch);
            return;
        }

        /* one object was found. Now for the other one. */
        if (!ch->equipment[HOLD]) {
            sprintf(buf, "You can't write with a %s alone.\n\r", papername);
            send_to_char(buf, ch);
            return;
        }
        if (!CAN_SEE_OBJ(ch, ch->equipment[HOLD])) {
            send_to_char("The stuff in your hand is invisible!  Yeech!!\n\r", ch);
            return;
        }

        if (pen)
            paper = ch->equipment[HOLD];
        else
            pen = ch->equipment[HOLD];
    }

    /* ok.. now let's see what kind of stuff we've found */
    if (pen->obj_flags.type_flag != ITEM_PEN) {
        act("$p is no good for writing with.", FALSE, ch, pen, 0, TO_CHAR);
    } else if (paper->obj_flags.type_flag != ITEM_NOTE) {
        act("You can't write on $p.", FALSE, ch, paper, 0, TO_CHAR);
    } else if (paper->action_description)
        send_to_char("There's something written on it already.\n\r", ch);
    else {
        /* we can write - hooray! */
        send_to_char("Ok.. go ahead and write.. end the note with a @.\n\r", ch);
        act("$n begins to jot down a note.", TRUE, ch, 0, 0, TO_ROOM);
        ch->desc->str = &paper->action_description;
        ch->desc->max_str = MAX_NOTE_LENGTH;
    }
}

ACMD(do_page)
{
    struct descriptor_data* d;
    struct char_data* vict;

    if (IS_NPC(ch)) {
        send_to_char("Monsters can't page or beep... go away.\n\r", ch);
        return;
    }

    if (!*argument) {
        if (subcmd == SCMD_PAGE)
            send_to_char("Whom do you wish to page?\n\r", ch);
        else
            send_to_char("Whom do you wish to beep?\n\r", ch);
        return;
    }
    half_chop(argument, buf, buf2);
    if (!str_cmp(buf, "all") && (subcmd == SCMD_PAGE)) {
        if (GET_LEVEL(ch) > LEVEL_GOD) {
            sprintf(buf, "\007\007*%s* %s\n\r", GET_NAME(ch), buf2);
            for (d = descriptor_list; d; d = d->next)
                if (!d->connected)
                    SEND_TO_Q(buf, d);
        } else
            send_to_char("You will never be godly enough to do that!\n\r", ch);
        return;
    }

    if ((vict = get_char_vis(ch, buf)) && !(!IS_NPC(vict) && other_side(ch, vict))) {
        if ((subcmd == SCMD_BEEP) && (GET_POS(vict) < POSITION_RESTING)) {
            act("$E can't hear you.", FALSE, ch, 0, vict, TO_CHAR);
            return;
        }
        sprintf(buf, "\007\007*%s* %s%s%s\n\r",
            GET_NAME(ch),
            (subcmd == SCMD_PAGE) ? "pages, '" : "",
            (subcmd == SCMD_PAGE) ? buf2 : "beeps you!",
            (subcmd == SCMD_PAGE) ? "'" : "");

        send_to_char(buf, vict);
        if (!PRF_FLAGGED(ch, PRF_ECHO))
            send_to_char("Ok.\n\r", ch);
        else {
            sprintf(buf, "You %s %s%s%s%s\n\r",
                (subcmd == SCMD_PAGE) ? "page" : "beep",
                GET_NAME(vict),
                (subcmd == SCMD_PAGE) ? " with '" : "",
                (subcmd == SCMD_PAGE) ? buf2 : "",
                (subcmd == SCMD_PAGE) ? "'" : ".");
            send_to_char(buf, ch);
        }
        return;
    } else
        send_to_char("There is no such person in the game!\n\r", ch);
}

/**********************************************************************
 * generalized communication func, originally by Fred C. Merkel (Torg) *
 *********************************************************************/

int channels[] = {
    PRF_NARRATE,
    PRF_CHAT,
    -1,
    PRF_SING
};

char* com_msgs[][3] = {
    { "You tried to narrate but could not make a sound\n\r",
        "narrate",
        "You aren't even on the channel!\n\r" },
    { "You tried to chat, but could not attract anybody's attention\n\r",
        "chat",
        "You aren't even on the channel!\n\r" },
    { "You tried to yell, but fell short of breath\n\r",
        "yell",
        "You aren't even on the channel!\n\r" },
    { "You tried to sing, but could not catch a tune\n\r",
        "sing",
        "You aren't even on the channel!\n\r" }
};

char* com_msgs_col[] = {
    "$CN",
    "$CC",
    "$CY",
    "$CC" /* Sing is the same color as chat */
};

ACMD(do_gen_com)
{
    struct descriptor_data* i;
    char* color;
    int imm_to_race, imm_side;
    struct room_data* tmproom;
    int myzone, tmp;

    void stop_hiding(struct char_data*, char);

    static char* imm_side_message[] = {
        "",
        " to the Light.",
        " to the Dark.",
        " to the Uruk-Lhuth.",
        " to All of Arda."
    };

    if (PLR_FLAGGED(ch, PLR_NOSHOUT)) {
        send_to_char(com_msgs[subcmd][0], ch);
        return;
    }

    if (IS_NPC(ch) && MOB_FLAGGED(ch, MOB_PET)) {
        sprintf(buf1, "Sorry, tamed mobiles and orc followers can't %s.\n\r",
            com_msgs[subcmd][1]);
        send_to_char(buf1, ch);
        return;
    }

    if (IS_NPC(ch) && (GET_INT(ch) < 6)) {
        send_to_char("You are too stupid to talk.\n\r", ch);
        return;
    }

    if (GET_POS(ch) < POSITION_RESTING) {
        send_to_char("You better be awake to do that.\n\r", ch);
        return;
    }

    if (IS_SHADOW(ch)) {
        send_to_char("You are unable to interact in this way "
                     "with the physical world.\n\r",
            ch);
        return;
    }

    if (GET_INVIS_LEV(ch) > 0 && GET_LEVEL(ch) < LEVEL_AREAGOD) {
        send_to_char("You cannot do this when invisible.\n\r", ch);
        return;
    }

    stop_hiding(ch, TRUE);

    if (!IS_NPC(ch))
        if (channels[subcmd] > 0 && !PRF_FLAGGED(ch, channels[subcmd])) {
            send_to_char(com_msgs[subcmd][2], ch);
            return;
        }

    if (wtl && wtl->targ1.type == TARGET_TEXT)
        argument = wtl->targ1.ptr.text->text;

    for (; *argument == ' '; argument++)
        continue;
    if (!(*argument)) {
        sprintf(buf1, "Yes, %s, fine, %s we must, but WHAT???\n\r",
            com_msgs[subcmd][1], com_msgs[subcmd][1]);
        send_to_char(buf1, ch);
        return;
    }

    if (!other_side_num(GET_RACE(ch), RACE_HUMAN))
        imm_side = 1;
    else if (!other_side_num(GET_RACE(ch), RACE_URUK))
        imm_side = 2;
    else if (!other_side_num(GET_RACE(ch), RACE_MAGUS))
        imm_side = 3;

    if (GET_LEVEL(ch) >= LEVEL_IMMORT) {
        imm_to_race = RACE_HUMAN;
        imm_side = 1;
        if (*argument == '-') {
            switch (UPPER(*(argument + 1))) {
            case 'W':
            case 'L':
                argument += 2;
                imm_to_race = RACE_HUMAN;
                imm_side = 1;
                break;
            case 'D':
                argument += 2;
                imm_to_race = RACE_URUK;
                imm_side = 2;
                break;
            case 'M':
                argument += 2;
                imm_to_race = RACE_MAGUS;
                imm_side = 3;
                break;
            case 'A':
                argument += 2;
                imm_to_race = RACE_GOD;
                imm_side = 4;
                break;
            default:
                break;
            };
        }
    }

    while (*argument && *argument <= ' ')
        argument++;

    color = com_msgs_col[subcmd];

    if (!PRF_FLAGGED(ch, PRF_ECHO))
        send_to_char("Ok.\n\r", ch);
    else {
        sprintf(buf, "%sYou %s '%s'%s",
            color,
            com_msgs[subcmd][1],
            argument,
            subcmd != SCMD_YELL && GET_LEVEL(ch) >= LEVEL_IMMORT ? imm_side_message[imm_side] : "");
        act(buf, FALSE, ch, NULL, NULL, TO_CHAR);
    }

    for (i = descriptor_list; i; i = i->next) {
        if (!i->connected && i != ch->desc && PRF_FLAGGED(i->character, channels[subcmd]) && !PLR_FLAGGED(i->character, PLR_WRITING)) {
            if (GET_POS(i->character) < POSITION_RESTING || IS_SHADOW(i->character))
                continue;

            switch (subcmd) {
            case SCMD_YELL: /* Go to all in zone */
                if (world[ch->in_room].zone != world[i->character->in_room].zone)
                    continue;
                say_to_char(ch, i->character, color,
                    com_msgs[subcmd][1], "", argument, FALSE);
                break;
            case SCMD_NARRATE:
            case SCMD_CHAT:
            case SCMD_SING:
                if (GET_LEVEL(ch) >= LEVEL_IMMORT) {
                    if (other_side_num(GET_RACE(i->character), imm_to_race))
                        continue;
                } else if (other_side(i->character, ch))
                    continue;

                say_to_char(ch, i->character, color,
                    com_msgs[subcmd][1],
                    GET_LEVEL(i->character) >= LEVEL_IMMORT ? imm_side_message[imm_side] : "",
                    argument, TRUE);
                break;
            default:
                vmudlog(NRM, "ERROR: Unrecognized subcmd '%d' in gen_com",
                    subcmd);
            }
        }
    }

    /* XXX: This REALLY needs to be replaced with something more efficient */
    if (subcmd == SCMD_YELL) {
        // triggering specials in other rooms now...
        myzone = world[ch->in_room].zone;
        for (tmp = 0; tmp <= top_of_world; tmp++) {
            tmproom = &world[tmp];
            if (tmproom->zone == myzone)
                special(ch, CMD_YELL, argument, SPECIAL_COMMAND, wtl, tmp);
        }
    }
}

ACMD(do_alias)
{
    char arg[MAX_INPUT_LENGTH];
    char *arg1, *arg2;
    struct alias_list *list, *list2;
    int tmp, count;

    if (IS_NPC(ch)) {
        send_to_char("Mobiles do not use aliases. go away.\n\r", ch);
        return;
    }

    arg1 = one_argument(argument, arg);

    if (!*arg) {
        /* alias list */
        list = ch->specials.alias;
        if (!list) {
            send_to_char("You have no aliases defined.\n\r", ch);
            return;
        }
        send_to_char("You have the following aliases defined:\n\r", ch);
        for (count = 0; list; list = list->next, count++) {
            sprintf(buf, "%-20s: %s\n\r", list->keyword, list->command);
            // printf("list found: %s\n",buf);
            send_to_char(buf, ch);
        }
        //    sprintf(buf,"You have %d of max %d aliases.\n\r",count,MAX_ALIAS);
        //  send_to_char(buf,ch);
        return;
    }
    count = 0;
    for (list = ch->specials.alias, list2 = 0; list; list2 = list, list = list->next) {
        //    printf("list->keyword=%s\n",list->keyword);
        if (!strcmp(arg, list->keyword))
            break;
        count++;
    }
    tmp = 0;
    for (arg2 = arg1; !(tmp = ((*arg2) > ' ')) && *arg2; arg2++)
        ;

    if (!tmp) {
        /* remove alias */
        if (!list) {
            send_to_char("You have no such alias.\n\r", ch);
            return;
        }
        sprintf(buf, "You removed the alias '%s'.\n\r", list->keyword);
        send_to_char(buf, ch);

        if (!list2)
            GET_ALIAS(ch) = list->next;
        else
            list2->next = list->next;

        RELEASE(list->command);
        list->keyword[0] = 0;

        RELEASE(list);

        return;
    }

    /* add/replace  alias */

    if (!list) {
        /* add alias */
        //    printf("adding alias\n");
        //    list2=(struct alias_list *)calloc(1,sizeof(struct alias_list));
        if (count >= MAX_ALIAS) {
            send_to_char("You reached the limit on alias number already.\n\r", ch);
            return;
        }

        CREATE1(list2, alias_list);
    } else {
        //    printf("replacing alias\n");
        list2 = list;
    }
    strncpy(list2->keyword, arg, 20);
    list2->keyword[strlen(arg)] = 0;

    RELEASE(list2->command);
    //  list2->command=(char *)calloc(strlen(arg2)+1,1);
    CREATE(list2->command, char, strlen(arg2) + 1);
    strcpy(list2->command, arg2);

    if (!list) {
        list2->next = ch->specials.alias;
        ch->specials.alias = list2;
        sprintf(buf, "You added the alias '%s'.\n\r", list2->keyword);
        send_to_char(buf, ch);
    } else {
        sprintf(buf, "You replaced the alias '%s'.\n\r", list2->keyword);
        send_to_char(buf, ch);
    }

    return;
}

ACMD(do_afk)
{
    if (IS_NPC(ch)) {
        send_to_char("Mobiles have no keyboard!\n\r", ch);
        return;
    }

    SET_BIT(PLR_FLAGS(ch), PLR_ISAFK);

    act("$n goes away from keyboard.", TRUE, ch, 0, 0, TO_ROOM);
    send_to_char("You go away from keyboard.\n\r", ch);

    if (!affected_by_spell(ch, SPELL_ANGER)) {
        game_rules::big_brother& bb_instance = game_rules::big_brother::instance();
        bb_instance.on_character_afked(ch);
    } else {
        send_to_char("You are too angry to be granted the protection of the Gods... wait a few minutes.\n\r", ch);
    }
}

ACMD(do_pray)
{
    char_data* tar_ch;

    for (; *argument && (*argument <= ' '); argument++)
        ;

    if (!*argument) {
        send_to_char("You raise your prayers to the sky.\n\r", ch);
        act("$n prays to the sky.", FALSE, ch, 0, 0, TO_ROOM);
        return;
    }

    tar_ch = get_char_vis(ch, argument);

    if (utils::is_npc(*ch) && ch->master) {
        if (tar_ch && other_side(ch->master, tar_ch)) {
            send_to_char("There is no such person to hear you.\n\r", ch);
            return;
        }
    }

    if (!tar_ch || (tar_ch && other_side(ch, tar_ch))) {
        send_to_char("There is no such person to hear you.\n\r", ch);
        return;
    }

    switch (GET_SEX(tar_ch)) {
    case SEX_MALE:
        act("You pray to $N, hoping for his good will.", FALSE, ch, 0, tar_ch, TO_CHAR);
        break;
    case SEX_FEMALE:
        act("You pray to $N, hoping for her good will.", FALSE, ch, 0, tar_ch, TO_CHAR);
        break;
    case SEX_NEUTRAL:
        act("You pray to $N, hoping for it's good will.", FALSE, ch, 0, tar_ch, TO_CHAR);
    };
    act("$n prays to $N, hoping for $S good will.",
        FALSE, ch, 0, tar_ch, TO_NOTVICT);
    act("$n prays to you, hoping for your good will.",
        FALSE, ch, 0, tar_ch, TO_VICT);
}
