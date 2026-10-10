#include "platdef.h"
#include <algorithm>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "comm.h"
#include "db.h"
#include "handler.h"
#include "interpre.h"
#include "limits.h"
#include "mudlle.h"
#include "protos.h"
#include "structs.h"
#include "utils.h"

ACMD(do_move);
ACMD(do_say);
ACMD(do_act);
ACMD(do_cast);
int find_first_step(int src, int target);

extern struct room_data world;
extern char** mobile_program;
extern int* mobile_program_zone;
extern int num_of_programs;
extern struct char_data* waiting_list;
extern struct char_data* character_list;
extern int top_of_world;

char mudlle_dummy[] = "Overflowed  buffer overflow.";

extern char* dirs[];

char* mudalloc(char* str);
void muddealloc(char* str);
void TO_STACK(struct char_data* host, long x);
long STACK_VALUE(struct char_data* host);
long FROM_STACK(struct char_data* host);
void DOWN_STACK(struct char_data* host);
void UP_STACK(struct char_data* host);
void int_itemtostring(char_data* host);

void TO_LIST(struct char_data* host, target_data* newtarg);
void TO_LIST(struct char_data* host, void* entr, int type);
void REMOVE_LIST(struct char_data* host);
void UP_LIST(struct char_data* host);
void DOWN_LIST(struct char_data* host);
int CHECK_LIST(char_data* host, char val);

void TEXT_LIST(char_data* host, char* targ);
int compare_list(char_data* host, target_data* it1, target_data* it2);

#define PRE_COMMAND             \
    tmp_mask = CALL_MASK(host); \
    CALL_MASK(host) = 0;
#define POST_COMMAND CALL_MASK(host) = tmp_mask;

void service_commands(struct char_data* host, char* arg, int cmd,
    int callflag, struct waiting_type* wtl)
{
    int tmp, tmp2, value;
    char tmp_mask;

    switch (*arg) {
    case 'u': /* duplicate stack item */
        TO_STACK(host, STACK_VALUE(host));
        break;

    case 'd': /* delete stack item */
        FROM_STACK(host);
        break;

    case 'x': /* excahnge two lowest stack items */
        tmp = FROM_STACK(host);
        tmp2 = FROM_STACK(host);
        TO_STACK(host, tmp);
        TO_STACK(host, tmp2);
        break;

    case 'f':
        tmp = FROM_STACK(host) - 1;
        for (tmp2 = 0; tmp2 < tmp; tmp2++)
            UP_STACK(host);
        break;

    case 'b':
        tmp = FROM_STACK(host) - 1;
        for (tmp2 = 0; tmp2 < tmp; tmp2++)
            DOWN_STACK(host);
        break;

    case 'r':
        SPECIAL_STACKPOINT(host) = 0;
        memset(SPECIAL_STACK(host), 0, sizeof(long) * SPECIAL_STACKLEN);
        break;

    case 'U':
        if (SPECIAL_LIST_TYPE(host) == TARGET_TEXT)
            TO_LIST(host, get_from_txt_block_pool(SPECIAL_LIST(host).ptr.text->text),
                SPECIAL_LIST_TYPE(host));
        else if (SPECIAL_LIST_TYPE(host) != SPECIAL_MARK)
            TO_LIST(host, SPECIAL_LIST(host).ptr.other, SPECIAL_LIST_TYPE(host));
        break;

    case 'D': /* remove an entrie from the list */
        REMOVE_LIST(host);
        break;

    case 'F': /* move up in list */
        tmp = FROM_STACK(host) - 1;
        for (tmp2 = 0; tmp2 < tmp; tmp2++)
            UP_LIST(host);
        break;

    case 'B': /* move down the list */
        tmp = FROM_STACK(host) - 1;
        for (tmp2 = 0; tmp2 < tmp; tmp2++)
            DOWN_LIST(host);
        break;

    case 'X':
        if (SPECIAL_LIST_NEXT(host) < 0) {
            PRE_COMMAND;
            do_say(host, "Not enough elements in the list to switch. :(", 0, 0, 0);
            POST_COMMAND;
            break;
        }
        tmp = SPECIAL_LIST_NEXT(host);
        tmp2 = SPECIAL_LIST_HEAD(host);

        SPECIAL_LIST_NEXT(host) = SPECIAL_LIST_AREA(host)->next[tmp];
        SPECIAL_LIST_HEAD(host) = tmp;
        SPECIAL_LIST_NEXT(host) = tmp2;
        break;

    case 'N': /* get the next (global) item */
        tmp = SPECIAL_LIST_TYPE(host);
        if (tmp == TARGET_CHAR)
            TO_LIST(host, SPECIAL_LIST(host).ptr.ch->next, TARGET_CHAR);
        else if (tmp == TARGET_OBJ)
            TO_LIST(host, SPECIAL_LIST(host).ptr.obj->next, TARGET_OBJ);
        else if (tmp == TARGET_ROOM) {
            tmp = real_room(SPECIAL_LIST(host).ptr.room->number);
            if (tmp < top_of_world)
                TO_LIST(host, &world[tmp + 1], TARGET_ROOM);
            else
                TO_LIST(host, 0, SPECIAL_NULL);
        } else
            TO_LIST(host, 0, SPECIAL_NULL);
        break;

    case 'n': /* get the next (local) char */
        tmp = SPECIAL_LIST_TYPE(host);
        if (tmp == TARGET_CHAR)
            TO_LIST(host, SPECIAL_LIST(host).ptr.ch->next_in_room, TARGET_CHAR);
        else if (tmp == TARGET_OBJ)
            TO_LIST(host, SPECIAL_LIST(host).ptr.obj->next_content, TARGET_OBJ);
        else if (tmp == TARGET_ROOM) {
            tmp2 = SPECIAL_LIST(host).ptr.room->zone;
            for (tmp = real_room(SPECIAL_LIST(host).ptr.room->number) + 1;
                 (tmp <= top_of_world) && world[tmp].zone != tmp2;
                 tmp++)
                ;
            if (tmp <= top_of_world)
                TO_LIST(host, &world[tmp], TARGET_ROOM);
            else
                TO_LIST(host, 0, SPECIAL_NULL);
        } else
            TO_LIST(host, 0, SPECIAL_NULL);
        break;

    case 'C':
        value = 0;
        for (tmp = SPECIAL_LIST_NEXT(host), tmp2 = 1;
             (SPECIAL_LIST_AREA(host)->next[tmp] >= 0) && (tmp2 < SPECIAL_STACKLEN - 1);
             tmp = SPECIAL_LIST_AREA(host)->next[tmp], tmp2++) {
            if (compare_list(host,
                    SPECIAL_LIST_AREA(host)->field + (SPECIAL_LIST_HEAD(host)),
                    SPECIAL_LIST_AREA(host)->field + tmp)) {
                value = 1;
                break;
            }
        }
        if (value) {
            TO_STACK(host, tmp2);
            REMOVE_LIST(host);
        } else
            TO_STACK(host, 0);
        break;
    }
}

void int_tostack(struct char_data* host, char* arg, int cmd,
    int callflag, struct waiting_type* wtl)
{
    int value, tmp, tmp2;
    char_data* tmpch;
    obj_data* tmpobj;

    value = 0;

    switch (*arg) {
    case 'C':
        if (wtl)
            value = wtl->cmd;
        else
            value = cmd;
        break;

    case 'c':
        if (wtl)
            value = wtl->subcmd;
        else
            value = 0;
        break;

    case 'I':
        value = callflag;
        break;

    case 'h':
        if (SPECIAL_LIST_TYPE(host) == TARGET_CHAR)
            if (SPECIAL_LIST(host).ptr.ch)
                value = SPECIAL_LIST(host).ptr.ch->tmpabilities.hit;
        break;

    case 'H':
        if (SPECIAL_LIST_TYPE(host) == TARGET_CHAR)
            if (SPECIAL_LIST(host).ptr.other)
                value = SPECIAL_LIST(host).ptr.ch->abilities.hit;
        break;

    case 'm':
        if (SPECIAL_LIST_TYPE(host) == TARGET_CHAR)
            if (SPECIAL_LIST(host).ptr.other)
                value = SPECIAL_LIST(host).ptr.ch->tmpabilities.mana;
        break;

    case 'M':
        if (SPECIAL_LIST_TYPE(host) == TARGET_CHAR)
            if (SPECIAL_LIST(host).ptr.other)
                value = SPECIAL_LIST(host).ptr.ch->abilities.mana;
        break;

    case 'v':
        if (SPECIAL_LIST_TYPE(host) == TARGET_CHAR)
            if (SPECIAL_LIST(host).ptr.other)
                value = SPECIAL_LIST(host).ptr.ch->tmpabilities.move;
        break;

    case 'V':
        if (SPECIAL_LIST_TYPE(host) == TARGET_CHAR)
            if (SPECIAL_LIST(host).ptr.other)
                value = SPECIAL_LIST(host).ptr.ch->abilities.move;
        break;

    case 'l':
        if (SPECIAL_LIST_TYPE(host) == TARGET_CHAR)
            if (SPECIAL_LIST(host).ptr.other)
                value = SPECIAL_LIST(host).ptr.ch->player.level;
        break;

    case 'r':
        tmpch = 0;
        tmpobj = 0;
        if (SPECIAL_LIST_TYPE(host) == TARGET_ROOM) {
            value = real_room(SPECIAL_LIST(host).ptr.room->number);
            break;
        }
        if (SPECIAL_LIST_TYPE(host) == TARGET_CHAR)
            tmpch = SPECIAL_LIST(host).ptr.ch;
        if (SPECIAL_LIST_TYPE(host) == TARGET_OBJ) {
            for (tmpobj = SPECIAL_LIST(host).ptr.obj; tmpobj->in_obj;
                 tmpobj = tmpobj->in_obj)
                ;
            if (tmpobj->carried_by)
                tmpch = tmpobj->carried_by;
        }
        if (tmpch)
            value = tmpch->in_room;
        else if (tmpobj)
            value = tmpobj->in_room;
        break;

    case 'R':
        tmp = FROM_STACK(host);
        value = real_room(tmp);
        break;

    case 'P':
        tmp = FROM_STACK(host);
        value = find_first_step(host->in_room, tmp) + 1;
        break;

    case '=':
        value = 0;
        if (SPECIAL_LIST_HEAD(host) < 0)
            value = 0;
        else if (SPECIAL_LIST_NEXT(host) < 0)
            value = 0;
        else
            value = compare_list(host, SPECIAL_LIST_AREA(host)->field + (SPECIAL_LIST_HEAD(host)),
                SPECIAL_LIST_AREA(host)->field + (SPECIAL_LIST_NEXT(host)));
        TO_STACK(host, value);
        REMOVE_LIST(host);
        REMOVE_LIST(host);
        break;

    case 'N':
        value = FROM_STACK(host);
        if (value >= 0)
            value = number(0, value);
        else
            value = 0;
        break;

    case 'T':
        value = SPECIAL_LIST_TYPE(host);
        break;

    case 'S':
        for (tmp = 2, tmp2 = SPECIAL_LIST_NEXT(host); ((tmp < 100) && !(SPECIAL_LIST(host) == SPECIAL_LIST_AREA(host)->field[tmp2]));
             tmp++, tmp2 = SPECIAL_LIST_AREA(host)->next[tmp2])
            ;

        if ((tmp >= 100) || (SPECIAL_LIST_HEAD(host) == tmp2))
            value = 0;
        else {
            REMOVE_LIST(host);
            value = tmp;
        }
        break;

    default:
        value = 0;
        // SPECIAL_STACK(host)[SPECIAL_STACKPOINT(host)]=0;
        break;
    }
    TO_STACK(host, value);
}

void int_tolist(struct char_data* host, struct char_data* ch, char* cmdline,
    char* arg, int cmd, int callflag, struct waiting_type* wtl)
{
    struct char_data* tmpch;
    int tmpvar, tmp;
    char str[MAX_STRING_LENGTH];
    char str2[MAX_STRING_LENGTH];

    tmpch = host;

    // printf("f commandd key:%c\n",tmp);
    switch (*arg) {
    case 's': /* self */
        TO_LIST(host, host, TARGET_CHAR);
        break;

    case 'a':
        //     tmptxt = cmdline;
        //     while(*tmptxt && (*tmptxt <= ' ')) tmptxt++;
        //     TO_LIST(host,get_from_txt_block_pool(tmptxt),TARGET_TEXT);
        if (wtl)
            TO_LIST(host, &wtl->targ1);
        else if (arg)
            TO_LIST(host, get_from_txt_block_pool(cmdline), TARGET_TEXT);
        else
            TO_LIST(host, 0, TARGET_NONE);
        break;

    case 'A':
        if (wtl)
            TO_LIST(host, &wtl->targ2);
        else
            TO_LIST(host, 0, TARGET_NONE);

        break;

    case 'i':
        tmpvar = FROM_STACK(host);
        sprintf(str, "%d%c", tmpvar, 0);
        //    printf("f-i command, goind to list:%s.\n",str);
        TO_LIST(host, get_from_txt_block_pool(str), TARGET_TEXT);
        break;

    case '+': // concatenating two lowest items in the list.
        TEXT_LIST(host, str2);
        REMOVE_LIST(host);
        TEXT_LIST(host, str);
        REMOVE_LIST(host);
        strcat(str, str2);
        TO_LIST(host, get_from_txt_block_pool(str), TARGET_TEXT);
        break;

    case '/':
        if (SPECIAL_LIST_TYPE(host) == TARGET_TEXT)
            strcpy(str, SPECIAL_LIST(host).ptr.text->text);
        else
            TEXT_LIST(host, str);
        REMOVE_LIST(host);
        for (tmpvar = 0; str[tmpvar] && str[tmpvar] <= ' '; tmpvar++)
            ;
        for (tmp = 0; str[tmpvar] && str[tmpvar] > ' '; tmpvar++, tmp++)
            str2[tmp] = str[tmpvar];
        str2[tmp] = 0;
        TO_LIST(host, get_from_txt_block_pool(str + tmpvar), TARGET_TEXT);
        TO_LIST(host, get_from_txt_block_pool(str2), TARGET_TEXT);
        break;

    case 'r': // a room the host is in
        //    TO_LIST(host, world+host->in_room, TARGET_ROOM);
        if (host->in_room >= 0) {
            TO_LIST(host, &world[host->in_room], TARGET_ROOM);
            TO_STACK(host, 1);
        } else {
            TO_LIST(host, 0, SPECIAL_NULL);
            TO_STACK(host, 0);
        }
        break;

    case 'R': // the room by number taken from stack
        tmpvar = FROM_STACK(host);
        if ((tmpvar >= 0) && (tmpvar <= top_of_world)) {
            TO_LIST(host, &world[tmpvar], TARGET_ROOM);
            TO_STACK(host, 1);
        } else {
            TO_LIST(host, 0, SPECIAL_NULL);
            TO_STACK(host, 0);
        }
        break;

    case 'h':
        if (ch) {
            TO_LIST(host, ch, TARGET_CHAR);
            TO_STACK(host, 1);
        } else {
            TO_LIST(host, 0, SPECIAL_NULL);
            TO_STACK(host, 0);
        }
        break;

    case 'c': /* first char in room, not self*/
        tmpch = world[host->in_room].people;
        while ((tmpch == host) || !CAN_SEE(host, tmpch)) {
            if (tmpch->next_in_room)
                tmpch = tmpch->next_in_room;
            else
                tmpch = 0;
        }
        if (tmpch) {
            TO_LIST(host, tmpch, TARGET_CHAR);
            TO_STACK(host, 1);
        } else {
            TO_LIST(host, 0, SPECIAL_NULL);
            TO_STACK(host, 0);
        }
        break;

    case 'C': /* first char in room, not self, not NPC*/
        tmpch = world[host->in_room].people;
        while (tmpch && ((tmpch == host) || !CAN_SEE(host, tmpch) || IS_NPC(tmpch))) {
            if (tmpch->next_in_room)
                tmpch = tmpch->next_in_room;
            else
                tmpch = 0;
        }
        if (tmpch) {
            TO_LIST(host, tmpch, TARGET_CHAR);
            TO_STACK(host, 1);
        } else {
            TO_LIST(host, 0, SPECIAL_NULL);
            TO_STACK(host, 0);
        }
        break;

    case '0':
        TO_LIST(host, 0, SPECIAL_NULL);
        break;

    case 'f':
        if ((SPECIAL_LIST_TYPE(host) != TARGET_CHAR) || (!SPECIAL_LIST(host).ptr.ch->specials.fighting)) {
            REMOVE_LIST(host);
            TO_LIST(host, 0, SPECIAL_NULL);
            TO_STACK(host, 0);
        } else {
            tmpch = SPECIAL_LIST(host).ptr.ch->specials.fighting;
            REMOVE_LIST(host);
            TO_LIST(host, tmpch, TARGET_CHAR);
            TO_STACK(host, 1);
        }
        break;

    default:
        sprintf(buf, "Wrong to-list command '%c', sorry.\n\r", *arg);
        do_say(host, buf, 0, 0, 0);
        break;
    }
    //      printf("going to-stack it :)\n");
    //  if(tmpchar) TO_STACK(host,1);
    //  else TO_STACK(host,0);
    //     if (tmpchar)  printf("got to list %s\n",GET_NAME(tmpchar));
    //     else printf("got to list:0.\n");
}

void service_get_old(struct char_data* host, struct char_data* ch, char* cmdline,
    char* arg, int cmd, int callflag, struct waiting_type* wtl)
{
    switch (*arg) {
    case 'I': /* sets call mask */
        TO_STACK(host, CALL_MASK(host));
        break;

    case 'S':
        if (ch)
            TO_LIST(host, ch, TARGET_CHAR);
        else
            TO_LIST(host, 0, SPECIAL_NULL);
        break;

    case 's':
        TO_LIST(host, host, TARGET_CHAR);
        break;

    case 'O':
        TO_LIST(host, 0, SPECIAL_NULL);
        break;
    }
}

void service_set_old(struct char_data* host, struct char_data* ch, char* cmdline,
    char* arg, int cmd, int callflag, struct waiting_type* wtl)
{
    int tmpvar;

    switch (*arg) {
    case 'I': /* sets call mask */
        tmpvar = FROM_STACK(host);
        CALL_MASK(host) = tmpvar;
        break;
    }
}

/*
 * The function called for a question mark directive.
 * Forces the mobile to dump its stack and list - via
 * the `say' command.
 */
void question_proc(struct char_data* host)
{
    int tmp;
    char tmp_mask;
    char tmpstr[255];
    struct target_data* tmplist;

    sprintf(tmpstr, "My stack is:[");
    for (tmp = SPECIAL_STACKPOINT(host) - 1; tmp >= 0; tmp--)
        sprintf(tmpstr + strlen(tmpstr), " %ld", SPECIAL_STACK(host)[tmp]);
    sprintf(tmpstr + strlen(tmpstr), "]");
    PRE_COMMAND;
    do_say(host, tmpstr, 0, 0, 0);

    tmp = SPECIAL_LIST_HEAD(host);
    do_say(host, "My list is:", 0, 0, 0);
    while (tmp >= 0) {
        tmpstr[0] = 0;
        tmplist = SPECIAL_LIST_AREA(host)->field + tmp;
        switch (SPECIAL_LIST_AREA(host)->field[tmp].type) {
        case SPECIAL_MARK:
            sprintf(tmpstr, "mark_record");
            break;

        case SPECIAL_NULL:
            //	    sprintf(tmpstr,"Null :%ld",tmplist.str);
            sprintf(tmpstr, "null_record");
            break;

        case TARGET_TEXT:
            sprintf(tmpstr, "Str :%s", tmplist->ptr.text->text);
            break;

        case TARGET_OBJ:
            sprintf(tmpstr, "Obj :%s", tmplist->ptr.obj->name);
            break;

        case TARGET_CHAR:
            sprintf(tmpstr, "Char:%s", GET_NAME(tmplist->ptr.ch));
            break;

        case TARGET_ROOM:
            sprintf(tmpstr, "Room:%s", tmplist->ptr.room->name);
            break;
        }
        //	  printf("'?': tmpstr=%s, next=%ld\n",tmpstr,tmplist->next);
        do_say(host, tmpstr, 0, 0, 0);
        tmp = SPECIAL_LIST_AREA(host)->next[tmp];
        //	  printf("new tmplist=%ld\n",tmplist);
    }
    do_say(host, "End of list", 0, 0, 0);
    POST_COMMAND;
    //	printf("'?' passed\n");
}

/*
 * A script may write directly into a character's hit points via
 * the 'h' case below, bypassing update_pos()/normal death processing
 * entirely.  Clamp to a range far beyond any real character's hit
 * points but well inside int range (mirrors script.cpp's
 * MIN/MAX_SCRIPT_HIT_VALUE for the SET_INT_VALUE opcode).
 */
static const int MIN_MUDLLE_HIT_VALUE = -1000000;
static const int MAX_MUDLLE_HIT_VALUE = 1000000;

/*
 * Sets parameters of the item in list, taking
 * them from the stack.
 */
void int_fromstack(struct char_data* host, char* arg, int cmd,
    int callflag, struct waiting_type* wtl)
{
    int val;

    val = FROM_STACK(host);

    switch (*arg) {
    case 'I': /* sets call mask */
        CALL_MASK(host) = val;
        break;

    case 'h':
        if ((SPECIAL_LIST_TYPE(host) == TARGET_CHAR) && (SPECIAL_LIST(host).ptr.ch)) {
            char_data* target = SPECIAL_LIST(host).ptr.ch;
            val = std::max(MIN_MUDLLE_HIT_VALUE, std::min(val, MAX_MUDLLE_HIT_VALUE));
            target->tmpabilities.hit = val;
            /* Only resync when the write pushes hit non-positive, or
             * when the character was already sitting at DEAD/INCAP/
             * STUNNED from an earlier script write and needs releasing
             * now that they've been healed - not on every positive
             * write, since update_pos() forces STANDING/FIGHTING
             * whenever hit > 0 (would wrongly stand up a resting/
             * sleeping character). Without the second condition a
             * character healed from a prior scripted DEAD/INCAP/
             * STUNNED would stay stuck at that stale position - a
             * soft-lock until the next point_update() sweep, since the
             * command dispatcher requires a higher position for nearly
             * everything. POSITION_SHAPING is deliberately excluded -
             * it's the OLC "shape" editor state (interpre.cpp),
             * unrelated to dying. */
            bool already_dying = GET_POS(target) == POSITION_DEAD ||
                GET_POS(target) == POSITION_INCAP ||
                GET_POS(target) == POSITION_STUNNED;
            if (val <= 0 || already_dying)
                update_pos(target);
        }
        break;

    case 'm':
        if ((SPECIAL_LIST_TYPE(host) == TARGET_CHAR) && (SPECIAL_LIST(host).ptr.ch))
            SPECIAL_LIST(host)
                .ptr.ch->tmpabilities.mana
                = val;
        break;
    case 'v':
        if ((SPECIAL_LIST_TYPE(host) == TARGET_CHAR) && (SPECIAL_LIST(host).ptr.ch))
            SPECIAL_LIST(host)
                .ptr.ch->tmpabilities.move
                = val;
        break;
    }

    return;
}

#define CHECK_ARG_LETTER(c)       \
    {                             \
        if ((c) == '.')           \
            break;                \
        if ((c) == ',')           \
            return FALSE;         \
        if ((c) == ';')           \
            return TRUE;          \
        if ((c) == 0) {           \
            PROG_POINT(host) = 0; \
            return FALSE;         \
        }                         \
    }

SPECIAL(intelligent)
{
    char* cmd_line;
    char key;
    int tmp, cmd_leng, cmd_count;
    long tmpvar, tmpvar2;
    sh_int tmp_mask;
    char tmpstr[255];
    struct waiting_type tmpwtl;

    /*
     * first, we need to make sure that the callflag
     * that called this function is appropriate for this
     * mobile.  If the callflag isn't in his CALL_MASK,
     * we return; however, no CALL_MASK can keep us from
     * running if SPECIAL_DELAY is the callflag
     */
    if (!IS_SET(CALL_MASK(host), callflag) && (callflag != SPECIAL_DELAY))
        return FALSE;

    cmd_line = SPECIAL_PROGRAM(host);
    cmd_leng = strlen(cmd_line);
    cmd_count = 0;

    for (tmp = 0; tmp < SPECIAL_STACKLEN; tmp++)
        if ((SPECIAL_LIST_AREA(host)->field[tmp].type == TARGET_CHAR) && !char_exists(SPECIAL_LIST_REFS(host))) {
            SPECIAL_LIST_AREA(host)
                ->field[tmp]
                .type
                = SPECIAL_NULL;
            SPECIAL_LIST_AREA(host)
                ->field[tmp]
                .ptr.other
                = 0;
        }

    CHECK_LIST(host, SPECIAL_LIST_HEAD(host));
    //  printf("ch=%ld host=%ld flag=%d\n",(long)ch,(long)host,callflag);

    key = *(cmd_line + PROG_POINT(host));
    while ((key != 0) && (key != ',') && (key != ';') && (cmd_count < 100)) {
        //  printf("mouse avant, cmd=%d.'%c'\n",PROG_POINT(host),key);
        //  printf("key is %c\n",key);

        cmd_count++;
        if ((key >= '0') && (key <= '9')) {
            tmpvar = 0;
            while ((key >= '0') && (key <= '9')) {
                PROG_POINT(host)
                ++;
                tmpvar = tmpvar * 10 + key - '0';
                key = *(cmd_line + PROG_POINT(host));
            }
            TO_STACK(host, tmpvar);
            //  printf("digit found %d\n",tmpvar);
        }
        //  printf("command is '%c', addr=%d, depth=%d\n",
        //  key, PROG_POINT(host), host->specials.tactics);

        switch (key) {
        case '?':
            question_proc(host);
            break;

        case '.': /* empty command */
            break;

        case '`':
            PROG_POINT(host)
            ++;
            key = *(cmd_line + PROG_POINT(host));
            tmp = 0;
            while ((key != '`') && (key != 0) && (tmp < 255)) {
                tmpstr[tmp] = key;
                PROG_POINT(host)
                ++;
                key = *(cmd_line + PROG_POINT(host));
                tmp++;
            }
            switch (key) {
            case '`':
                tmpstr[tmp] = 0;
                TO_LIST(host, get_from_txt_block_pool(tmpstr), TARGET_TEXT);
                break;

            case 0:
                PROG_POINT(host) = 0;
                return FALSE;

            default:
                PRE_COMMAND;
                do_say(host, "My string is too long.", 0, 0, 0);
                POST_COMMAND;
                PROG_POINT(host) = 0;
                return FALSE;
            }
            break;

        case ',':
            PROG_POINT(host)
            ++;
            return FALSE;

        case ';':
            PROG_POINT(host)
            ++;
            return TRUE;

        case 'R': /* reset and return FALSE */
        case 'Q':
            for (tmp = 0; tmp < SPECIAL_STACKLEN; tmp++) {
                if (SPECIAL_LIST_AREA(host)->field[tmp].type == TARGET_TEXT)
                    put_to_txt_block_pool(SPECIAL_LIST_AREA(host)->field[tmp].ptr.text);

                SPECIAL_LIST_AREA(host)
                    ->field[tmp]
                    .ptr.other
                    = 0;
                SPECIAL_LIST_AREA(host)
                    ->next[tmp]
                    = -1;
                SPECIAL_LIST_AREA(host)
                    ->field[tmp]
                    .type
                    = SPECIAL_VOID;
                SPECIAL_STACK(host)
                [tmp] = 0;
            }
            SPECIAL_LIST_HEAD(host) = 0;
            SPECIAL_LIST_AREA(host)
                ->field[0]
                .type
                = SPECIAL_MARK;
            SPECIAL_LIST_AREA(host)
                ->next[0]
                = -1;
            SPECIAL_STACKPOINT(host) = 0;
            host->specials.tactics = 0;
            CALL_MASK(host) = 255;
            PROG_POINT(host) = 0;
            if (key == 'Q')
                return FALSE;
            else
                return TRUE;

        case 'm':
        case 'c':
        case 'C':
            tmpvar = FROM_STACK(host);
            tmpvar2 = FROM_STACK(host);
            tmpwtl.cmd = tmpvar2;
            tmpwtl.subcmd = tmpvar;
            tmpwtl.targ1.type = TARGET_NONE;
            tmpwtl.targ2.type = TARGET_NONE;
            if (key == 'c') {
                tmpwtl.targ1 = SPECIAL_LIST(host);
                REMOVE_LIST(host);
            }
            if (key == 'C') {
                tmpwtl.targ1 = SPECIAL_LIST(host);
                REMOVE_LIST(host);
                tmpwtl.targ2 = SPECIAL_LIST(host);
                REMOVE_LIST(host);
            }
            PRE_COMMAND;
            //      do_move(host,dirs[tmpvar], 0, tmpvar+1, 0);
            command_interpreter(host, "", &tmpwtl);
            POST_COMMAND;
            tmpwtl.targ1.cleanup();
            tmpwtl.targ2.cleanup();
            break;
        case 'd':
            tmpvar = FROM_STACK(host);
            (PROG_POINT(host))++;
            WAIT_STATE_FULL(host, tmpvar, -1, 0, 0, 0, 0, 0, 0, TARGET_NONE);
            return FALSE;
            //     case 'S':                            /* set some parameter */
            //       (PROG_POINT(host))++;
            //       tmp=*(cmd_line + PROG_POINT(host));
            //       if(tmp == '_') return FALSE;
            //       if(tmp == 0) { PROG_POINT(host)=0; return FALSE;}
            // //      printf("to_list command:'%c', type=%d\n",tmp,SPECIAL_LIST_TYPE(host));
            //       service_set(host,ch, arg, cmd_line + PROG_POINT(host), cmd,
            // 		  callflag, wtl);
            //       break;
            //     case 'G':                            /* get some parameter */
            //       (PROG_POINT(host))++;
            //       tmp=*(cmd_line + PROG_POINT(host));
            //       if(tmp == '_') return FALSE;
            //       if(tmp == 0) { PROG_POINT(host)=0; return FALSE;}
            // //      printf("to_list command:'%c', type=%d\n",tmp,SPECIAL_LIST_TYPE(host));
            //       service_get(host,ch, arg, cmd_line + PROG_POINT(host), cmd,
            // 		  callflag, wtl);
            //       break;
            //     case 'U':
            //       if((SPECIAL_LIST_TYPE(host) == SPECIAL_STR) ||
            // 	 (SPECIAL_LIST_TYPE(host) == SPECIAL_SST)){
            // 	PRE_COMMAND;
            // 	command_interpreter(host,SPECIAL_LIST(host).str);
            // 	POST_COMMAND;
            // 	REMOVE_LIST(host);
            // 	REMOVE_LIST(host);
            // 	FROM_STACK(host);
            // 	FROM_STACK(host);
            // 	break;
            //       }
            //       if(SPECIAL_LIST_TYPE(host) == TARGET_CHAR){
            // 	tmpwtl.flg = 1;
            // 	tmpwtl.targ1.type = TARGET_CHAR;
            // 	tmpwtl.ptr = SPECIAL_LIST(host).chr;
            // 	tmpwtl.num = ((char_data *)tmpwtl.ptr)->abs_number;
            // 	tmpwtl.dig = FROM_STACK(host);
            // 	tmpwtl.cmd = FROM_STACK(host);
            // 	tmpwtl.subcmd = 0;
            // 	REMOVE_LIST(host);
            // 	PRE_COMMAND;
            // 	command_interpreter(host,"", &tmpwtl);
            // 	POST_COMMAND;
            // 	REMOVE_LIST(host);
            // 	break;
            //       }
            //       REMOVE_LIST(host);
            //       REMOVE_LIST(host);
            //       break;
            //     case 'D':
            //       if((SPECIAL_LIST_TYPE(host) == SPECIAL_STR) ||
            // 	 (SPECIAL_LIST_TYPE(host) == SPECIAL_SST)){
            // 	PRE_COMMAND;
            // 	command_interpreter(host,SPECIAL_LIST(host).str);
            // 	POST_COMMAND;
            // 	REMOVE_LIST(host);
            // 	FROM_STACK(host);
            // 	break;
            //       }
            //       if(SPECIAL_LIST_TYPE(host) == TARGET_CHAR){
            // 	tmpwtl.character = host;
            // 	tmpwtl.flg = 1;
            // 	tmpwtl.ptr = SPECIAL_LIST(host).chr;
            // 	tmpwtl.num = ((char_data *)tmpwtl.ptr)->abs_number;
            // 	tmpwtl.cmd = FROM_STACK(host);
            // 	tmpwtl.subcmd = 0;
            // 	REMOVE_LIST(host);
            // 	tmpwtl.ptr = 0;
            // 	PRE_COMMAND;
            // 	command_interpreter(host,"", &tmpwtl);
            // 	POST_COMMAND;
            // 	REMOVE_LIST(host);
            // 	break;
            //       }
            //       REMOVE_LIST(host);
            break;
        case 'Z': /* formerly 'S' */
            if (SPECIAL_LIST_HEAD(host) != SPECIAL_MARK)
                int_itemtostring(host);
            break;
        case 'S':
            (PROG_POINT(host))++;
            tmp = *(cmd_line + PROG_POINT(host));
            CHECK_ARG_LETTER(tmp);

            service_commands(host, cmd_line + PROG_POINT(host), cmd,
                callflag, wtl);
            break;
        case 'v':
            (PROG_POINT(host))++;
            tmp = *(cmd_line + PROG_POINT(host));
            CHECK_ARG_LETTER(tmp);

            //      printf("to_stack command:'%c', type=%d\n",tmp,SPECIAL_LIST_TYPE(host));
            int_tostack(host, cmd_line + PROG_POINT(host), cmd,
                callflag, wtl);
            break;
        case 'V':
            (PROG_POINT(host))++;
            tmp = *(cmd_line + PROG_POINT(host));
            CHECK_ARG_LETTER(tmp);
            //      printf("from_stack cmd:'%c', type=%d\n",tmp,SPECIAL_LIST_TYPE(host));
            int_fromstack(host, cmd_line + PROG_POINT(host), cmd,
                callflag, wtl);
            break;
        case 's': /* say the string from the list */
            if (SPECIAL_LIST_TYPE(host) == TARGET_TEXT) {
                PRE_COMMAND;
                do_say(host, SPECIAL_LIST(host).ptr.text->text, 0, 0, 0);
                POST_COMMAND;
            } else {
                PRE_COMMAND;
                do_say(host, "What can I say if there is nothing to say?..", 0, 0, 0);
                POST_COMMAND;
            }
            REMOVE_LIST(host);
            break;
        case 'E': /* echo to char/room */
            if (SPECIAL_LIST_TYPE(host) == TARGET_TEXT) {
                strcpy(buf, SPECIAL_LIST(host).ptr.text->text);
                strcat(buf, "\n\r");
                REMOVE_LIST(host);
            } else
                strcpy(buf, "You feel something is afoot here.\n\r");

            if (SPECIAL_LIST_TYPE(host) == TARGET_CHAR)
                send_to_char(buf, SPECIAL_LIST(host).ptr.ch);
            else if (SPECIAL_LIST_TYPE(host) == TARGET_ROOM)
                send_to_room(buf, real_room(SPECIAL_LIST(host).ptr.room->number));
            REMOVE_LIST(host);
            break;

        case 'f': /* get item to list */
            (PROG_POINT(host))++;
            tmp = *(cmd_line + PROG_POINT(host));
            CHECK_ARG_LETTER(tmp);
            //      printf("to_list command:'%c', type=%d\n",tmp,SPECIAL_LIST_TYPE(host));
            int_tolist(host, ch, arg, cmd_line + PROG_POINT(host), cmd,
                callflag, wtl);
            break;

        case 'l':
        case 'L':
            for (tmp = SPECIAL_LIST_NEXT(host); tmp >= 0;
                 tmp = SPECIAL_LIST_AREA(host)->next[tmp])
                if ((SPECIAL_LIST_TYPE(host) == SPECIAL_LIST_AREA(host)->field[tmp].type) && (SPECIAL_LIST(host).ptr.other == SPECIAL_LIST_AREA(host)->field[tmp].ptr.other))
                    break;
            if (tmp)
                TO_STACK(host, 1);
            else
                TO_STACK(host, 0);
            // printf("soft check passed:%d\n",(tmplist2)?1:0);
            if (key == 'c')
                break;
            if (!tmp)
                break;
            REMOVE_LIST(host);
            while (SPECIAL_LIST_HEAD(host) != tmp)
                UP_LIST(host);
            // printf("hard check passed\n");
            break;

        case '+': /* sum, (stack)+(stack-1) */
            if (SPECIAL_STACKPOINT(host) > 1) {
                tmpvar = FROM_STACK(host);
                tmpvar2 = FROM_STACK(host);
                TO_STACK(host, tmpvar + tmpvar2);
            }
            break;

        case '-': /* subtract, (stack-1)-(stack) */
            if (SPECIAL_STACKPOINT(host) > 1) {
                tmpvar = FROM_STACK(host);
                tmpvar2 = FROM_STACK(host);
                TO_STACK(host, tmpvar2 - tmpvar);
            } else
                SPECIAL_STACK(host)
            [0] = -SPECIAL_STACK(host)[0];
            break;

        case '*': /* multiply, (stack-1)*(stack) */
            if (SPECIAL_STACKPOINT(host) > 1) {
                tmpvar = FROM_STACK(host);
                tmpvar2 = FROM_STACK(host);
                TO_STACK(host, tmpvar2 * tmpvar);
            } else
                SPECIAL_STACK(host)
            [0] = 0;
            break;

        case '/': /* divide, (stack-1)/(stack) */
            if (SPECIAL_STACKPOINT(host) > 1) {
                tmpvar = FROM_STACK(host);
                tmpvar2 = FROM_STACK(host);
                if (tmpvar == 0) {
                    PRE_COMMAND;
                    do_say(host, "I tried to divide by zero. Zero put in stack.",
                        0, 0, 0);
                    POST_COMMAND;
                    TO_STACK(host, 0);
                } else
                    TO_STACK(host, tmpvar2 / tmpvar);
            } else
                SPECIAL_STACK(host)
            [0] = 0;
            break;

        case '<': /* compare, (stack-1) < (stack) */
            if (SPECIAL_STACKPOINT(host) > 1) {
                tmpvar = FROM_STACK(host);
                tmpvar2 = FROM_STACK(host);
                TO_STACK(host, tmpvar2 < tmpvar);
            } else
                SPECIAL_STACK(host)
            [0] = (0 < SPECIAL_STACK(host)[0]);
            break;

        case '>': /* compare, (stack-1) > (stack) */
            if (SPECIAL_STACKPOINT(host) > 1) {
                tmpvar = FROM_STACK(host);
                tmpvar2 = FROM_STACK(host);
                TO_STACK(host, tmpvar2 > tmpvar);
            } else
                SPECIAL_STACK(host)
            [0] = (0 > SPECIAL_STACK(host)[0]);
            break;

        case '=': /* compare, (stack-1) == (stack) */
            if (SPECIAL_STACKPOINT(host) > 1) {
                tmpvar = FROM_STACK(host);
                tmpvar2 = FROM_STACK(host);
                TO_STACK(host, tmpvar2 == tmpvar);
            } else
                SPECIAL_STACK(host)
            [0] = (0 == SPECIAL_STACK(host)[0]);
            break;

        case '&': /* logical &&, (stack-1) & (stack) */
            if (SPECIAL_STACKPOINT(host) > 1) {
                tmpvar = FROM_STACK(host);
                tmpvar2 = FROM_STACK(host);
                TO_STACK(host, tmpvar2 & tmpvar);
            } else
                SPECIAL_STACK(host)
            [0] = (0 && SPECIAL_STACK(host)[0]);
            break;

        case '|': /* logical !!, (stack-1) || (stack) */
            if (SPECIAL_STACKPOINT(host) > 1) {
                tmpvar = FROM_STACK(host);
                tmpvar2 = FROM_STACK(host);
                TO_STACK(host, tmpvar2 | tmpvar);
            } else
                SPECIAL_STACK(host)
            [0] = (0 || SPECIAL_STACK(host)[0]);
            break;

        case '!':
            if (SPECIAL_STACKPOINT(host) > 0) {
                tmpvar = FROM_STACK(host);
                TO_STACK(host, !tmpvar);
            } else
                TO_STACK(host, 1);
            break;

        case 'K':
            tmpvar = FROM_STACK(host);
            if (tmpvar >= num_of_programs)
                break;
            if (host->specials.tactics >= SPECIAL_CALLLIST - 1)
                break;
            //  prog_point is automatic.,.
            host->specials.tactics++;
            PROG_NUMBER(ch) = tmpvar;
            PROG_POINT(host) = -1;
            cmd_line = SPECIAL_PROGRAM(host);
            cmd_leng = strlen(cmd_line);
            cmd_count = 0;
            break;

        case 'r':
            if (host->specials.tactics == 0)
                return FALSE;
            host->specials.tactics--;
            cmd_line = SPECIAL_PROGRAM(host);
            cmd_leng = strlen(cmd_line);
            cmd_count = 0;
            break;

        case 'g': /* unconditional goto */
            tmpvar = FROM_STACK(host);
            PROG_POINT(host) = tmpvar - 1;
            if (PROG_POINT(host) >= cmd_leng)
                PROG_POINT(host) = 0;
            break;

        case 'i':
            if (SPECIAL_STACKPOINT(host) > 1) {
                tmpvar = FROM_STACK(host);
                tmpvar2 = FROM_STACK(host);
            } else {
                tmpvar = FROM_STACK(host);
                tmpvar2 = 0;
            }
            // printf("'i' command, arg=%d, addr=%d\n",tmpvar2,tmpvar);
            if (tmpvar2)
                PROG_POINT(host) = tmpvar - 1;
            if (PROG_POINT(host) >= cmd_leng)
                PROG_POINT(host) = 0;
            break;
            //     case 'W':                       /* temporary command (?), cast spell */
            //       if(SPECIAL_LIST_TYPE(host)!=SPECIAL_STR){
            // 	   PRE_COMMAND;
            // 	   do_say(host,"I can't cast what i am urged to.\n\r",0,0,0);
            // 	   POST_COMMAND;
            // 	   break;
            //       }
            //       if(SPECIAL_LIST_NEXT(host) >= 0 ) {
            // 	   if(SPECIAL_LIST_AREA(host)->type[SPECIAL_LIST_NEXT(host)]
            // 	      == TARGET_CHAR)
            // 	     sprintf(tmpstr,"'%s' %s",SPECIAL_LIST(host).str,
            // 		     GET_NAME(SPECIAL_LIST_AREA(host)->field[SPECIAL_LIST_NEXT(host)].chr));
            // 	   else if(SPECIAL_LIST_AREA(host)->type[SPECIAL_LIST_NEXT(host)]
            // 	           == TARGET_OBJ)
            // 	  sprintf(tmpstr,"'%s' %s",SPECIAL_LIST(host).str,
            // 		  SPECIAL_LIST_AREA(host)->field[SPECIAL_LIST_NEXT(host)].obj->name);
            // 	else if(SPECIAL_LIST_AREA(host)->type[SPECIAL_LIST_NEXT(host)]
            // 	   ==SPECIAL_STR)
            // 	  sprintf(tmpstr,"'%s' %s",SPECIAL_LIST_AREA(host)->field[SPECIAL_LIST_NEXT(host)].str,
            // 		  SPECIAL_LIST_AREA(host)->field[SPECIAL_LIST_NEXT(host)].str);
            // 	REMOVE_LIST(host);
            // 	REMOVE_LIST(host);
            //       }
            //       else{
            // 	sprintf(tmpstr,"'%s'",SPECIAL_LIST(host).str);
            // 	REMOVE_LIST(host);
            //       }
            //       PRE_COMMAND;
            //       do_cast(host,tmpstr,0,0,0);
            //       POST_COMMAND;
            //       break;

        default:
            PRE_COMMAND;
            sprintf(buf2, "I can't understand my command (%c), alas :(", key);
            do_say(host, buf2, 0, 0, 0);
            POST_COMMAND;
            break;
        } /* End of the main switch */

        (PROG_POINT(host))++;
        key = *(cmd_line + PROG_POINT(host));
    }

    if (!key)
        PROG_POINT(host) = 0;
    else
        PROG_POINT(host)
    ++;
    /*printf("continuing.\n");*/
    if (key == ',')
        return FALSE;
    if (key == ';')
        return TRUE;

    return FALSE;
}

/*
 * Returns a pointer to the new "converted"
 * line, whatever that means.
 */
char* mudlle_converter(char* source)
{
    int i, tmp, tmp2, len, len2, markn, intext;
    char* newl;
    static int mark_adr[1000];
    static long mark_nam[1000];

    len = strlen(source);
    len2 = 0;
    markn = 0;
    intext = 0;
    for (i = 0; i < len; i++)
        if ((source[i] > ' ') || intext)
            switch (source[i]) {
            case '@': /* new mark */
                if (!intext) {
                    mark_nam[markn] = 0;
                    for (tmp = 1; tmp < 4; tmp++)
                        mark_nam[markn] = mark_nam[markn] * 128 + source[i + tmp];
                    mark_adr[markn] = len2;
                    markn++;
                    i += 3;
                } else
                    len2++;
                break;

            case 'M':
                if (!intext) {
                    i += 3;
                    len2 += 4;
                } else
                    len2++;
                break;

            case '$':
                if (!intext) {
                    i += 5;
                    len2 += 6;
                } else
                    len2++;
                break;

            case '`':
                intext = ~intext;
                len2++;
                break;

            case '\\':
                if (!intext)
                    while ((i < len) && (source[i] != '\n'))
                        i++;
                else
                    len2++;
                break;

            default:
                len2++;
            }

    CREATE(newl, char, len2 + 1);
    if (len2 == 0)
        return newl;

    len2 = 0;
    intext = 0;
    for (i = 0; i < len; i++)
        if ((source[i] > ' ') || intext)
            switch (source[i]) {
            case '@':
                if (!intext)
                    i += 3;
                else
                    newl[len2++] = source[i];
                break;

            case 'M':
                if (!intext) {
                    for (tmp2 = 0, tmp = 1; tmp < 4; tmp++)
                        tmp2 = tmp2 * 128 + source[i + tmp];

                    for (tmp = 0; tmp < markn; tmp++)
                        if (mark_nam[tmp] == tmp2)
                            break;

                    if (tmp == markn) {
                        RELEASE(newl);
                        CREATE(newl, char, 1000);
                        sprintf(newl, "Mark not found:");
                        strncat(newl, source + i, 5);
                        strcat(newl, "\n\r");
                        return newl;
                    }
                    sprintf(newl + len2, "%4.4d", mark_adr[tmp]);
                    len2 += 4;
                    i += 3;
                } else
                    newl[len2++] = source[i];
                break;

            case '$':
                if (!intext) {
                    strncpy(buf, source + i + 1, 5);
                    buf[5] = 0;
                    tmp2 = atoi(buf);
                    for (tmp = 0; tmp < num_of_programs; tmp++)
                        if (mobile_program_zone[tmp] == tmp2)
                            break;
                    if (tmp == num_of_programs)
                        tmp = 99999;
                    sprintf(newl + len2, "%6.6d", tmp);
                    len2 += 6;
                    i += 5;
                } else
                    newl[len2++] = source[i];
                break;

            case '`':
                intext = ~intext;
                newl[len2++] = source[i];
                break;

            case '\\':
                if (!intext)
                    while ((i < len) && (source[i] != '\n'))
                        i++;
                else
                    newl[len2++] = source[i];
                break;

            default:
                newl[len2++] = source[i];
                break;
            }
    return newl;
}
