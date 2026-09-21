#include "platdef.h"
#include <ctype.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "comm.h"
#include "db.h"
#include "handler.h"
#include "interpre.h"
#include "protos.h"
#include "structs.h"
#include "utils.h"
#include "zone.h"

extern struct obj_data* obj_proto;
extern struct room_data world;

int shape_standup(struct char_data* ch, int pos);
int convert_exit_flag(int tmp, int mode); /* in shaperom.cpp */
// void symbol_to_map(int x, int y, int symb);
void draw_map();

int shapezone_lastroom;

int command_room(struct reset_com* com)
{
    // returns the number of a room the command is performed in.
    int rom;

    switch (com->command) {
    case 'M':
    case 'O':
        rom = com->arg2;
        break;
    case 'G':
    case 'E':
    case 'K':
    case 'A':
        rom = shapezone_lastroom;
        break;
    case 'D':
    case 'R':
        rom = com->arg1;
        break;
    case 'P':
        if (com->arg1 >= 0)
            rom = com->arg1;
        else
            rom = shapezone_lastroom;
        break;
    case 'L':
        if (com->arg2 >= 0)
            rom = com->arg2;
        else
            rom = shapezone_lastroom;
        break;
    default:
        rom = 0;
        break;
    }
    shapezone_lastroom = rom;
    return rom;
}
void set_command_room(struct reset_com* com, int rom)
{
    // sets the number of a room the command is performed in.

    switch (com->command) {
    case 'M':
    case 'O':
    case 'L':
        com->arg2 = rom;
        break;
    case 'G':
    case 'E':
    case 'K':
    case 'A':
        shapezone_lastroom = rom;
        break;
    case 'D':
    case 'R':
    case 'P':
        com->arg1 = rom;
        break;
    default:
        break;
    }
    shapezone_lastroom = rom;
    return;
}

void renum_rooms(struct zone_tree* com)
{
    int num;
    struct zone_tree* tmpzon;

    num = 0;
    tmpzon = com;
    while (tmpzon) {
        num++;
        tmpzon->number = num;
        tmpzon->room = command_room(&(tmpzon->comm));
        tmpzon = tmpzon->next;
    }
}

void show_command(char* str, struct zone_tree* zon)
{
    struct reset_com* com;
    char tmpc;

    com = &(zon->comm);

    switch (com->command) {

    case 'A': /* Additional command, affects last loaded mobile */

        sprintf(str, "%3d A::Type:%d, Arg1:%d, Arg2:%d\n\r      %s\n\r",
            zon->number, com->arg1, com->arg2, com->arg3, zon->comment);

        break;
    case 'M': /* Load mobile */

        sprintf(str, "%3d M:: Ld_flg(%d) Mob:%d toRom:%d MxExst:%d Prb:%d Diff:%d MxLine:%d Tro:%d\n\r      %s\n\r",
            zon->number, com->if_flag, com->arg1, com->arg2, com->arg3,
            com->arg4, com->arg5, com->arg6, com->arg7, zon->comment);

        break;

    case 'N': /* Load mobile, random */

        sprintf(str, "%3d N:: Ld_flg(%d) Type:%d toRom:%d LvlGen:%d Prb:%d Dif:%d MxLine:%d Tro:%d\n\r      %s\n\r",
            zon->number, com->if_flag, com->arg1, com->arg2, com->arg3,
            com->arg4, com->arg5, com->arg6, com->arg7, zon->comment);

        break;

    case 'O': /* Load object */

        sprintf(str, "%3d O:: Load_flg(%d) Obj:%d to Room:%d, MxExst:%d Prb:%d MxInRom:%d\n\r     %s\n\r",
            zon->number, com->if_flag, com->arg1, com->arg2, com->arg3,
            com->arg4, com->arg5, zon->comment);

        break;

        //   case 'L':   /* Load object, random */

        //     sprintf(str,"%3d L:: Ld_flg(%d) Type:%d toRom:%d, MxToRom:%d Prb:%d LvlGen:%d Good:%d\n\r     %s\n\r",
        // 	    zon->number,com->if_flag, com->arg1,com->arg2,com->arg3,
        // 	    com->arg4,com->arg5,com->arg6,zon->comment);

        //     break;

    case 'G': /* give object to the last 'M' loaded mob */

        sprintf(str, "%3d G:: Load_flg(%d) ObjN:%d NULL(?):%d, MxExst:%d Prb:%d MxOnMob:%d MxObjMob:%d\n\r     %s\n\r",
            zon->number, com->if_flag, com->arg1, com->arg2, com->arg3,
            com->arg4, com->arg5, com->arg6, zon->comment);

        break;

    case 'H': /* give object to the last 'M' loaded mob, random */

        sprintf(str, "%3d H:: Load_flg(%d) Type:%d Prb:%d, MaxOnMob:%d MxObjMob:%d LvlGen:%d Good:%d\n\r     %s\n\r",
            zon->number, com->if_flag, com->arg1, com->arg2, com->arg3,
            com->arg4, com->arg5, com->arg6, zon->comment);

        break;

    case 'E': /* Object to equipment list of the last 'M' loaded mob */

        sprintf(str, "%3d E:: Load_flg(%d) ObjN:%d EqPos:%d, MaxExst:%d Prb:%d MaxOnMob:%d MxObjMob:%d\n\r     %s\n\r",
            zon->number, com->if_flag, com->arg1, com->arg2, com->arg3,
            com->arg4, com->arg5, com->arg6, zon->comment);

        break;

    case 'Q': /* Object to equip list of the last 'M' loaded mob, random */

        sprintf(str, "%3d Q:: Load_flg(%d) Type:%d EqPos:%d, Prb:%d MaxOnMob:%d, MxObjMob:%d LvlGen:%d Good:%d\n\r     %s\n\r",
            zon->number, com->if_flag, com->arg1, com->arg2, com->arg3,
            com->arg4, com->arg5, com->arg6, com->arg7, zon->comment);

        break;

    case 'P': /* Put object to object */

        sprintf(str, "%3d P:: Load_flg(%d) Room:%d Obj:%d toObj:%d, MaxExst:%d Prb:%d MaxInObj:%d\n\r     %s\n\r",
            zon->number, com->if_flag, com->arg1, com->arg2, com->arg3,
            com->arg4, com->arg5, com->arg6, zon->comment);

        break;

    case 'K':

        sprintf(str, "%3d K:: Load_flg(%d) Objects:%d, %d, %d, %d, %d, %d, %d\n\r     %s\n\r",
            zon->number, com->if_flag, com->arg1, com->arg2, com->arg3,
            com->arg4, com->arg5, com->arg6, com->arg7, zon->comment);

        break;

    case 'D': /* set state of door */
        switch (com->arg2) {
        case NORTH:
            tmpc = 'N';
            break;
        case EAST:
            tmpc = 'E';
            break;
        case SOUTH:
            tmpc = 'S';
            break;
        case WEST:
            tmpc = 'W';
            break;
        case UP:
            tmpc = 'U';
            break;
        case DOWN:
            tmpc = 'D';
            break;
        default:
            tmpc = 'X';
        }
        sprintf(str, "%3d D:: Load_flg(%d) Room:%d Exit:%c, State:%d\n\r    %s\n\r",
            zon->number, com->if_flag, com->arg1, tmpc, com->arg3, zon->comment);
        break;

    case 'R': /* remove object from room */

        sprintf(str, "%3d R:: Load_flg(%d) Room:%d, Obj:%d\n\r     %s\n\r",
            zon->number, com->if_flag, com->arg1, com->arg2, zon->comment);

        break;

    case 'L': /* set last_mob or last_obj */
        sprintf(str, "%3d L:: Load_flg(%d) Mode:%d Room:%d Mob/Obj:%d Num:%d\n\r     %s\n\r",
            zon->number, com->if_flag, com->arg1, com->arg2, com->arg3,
            com->arg4, zon->comment);

        break;

    default:

        sprintf(str, "%3d Unrecognized Command (if %d) on %d by %d to %d when %d with %d, (%d %d) \n\r    %s.\n\r",
            zon->number, com->if_flag, com->arg1, com->arg2, com->arg3,
            com->arg4, com->arg5, com->arg6, com->arg7, zon->comment);

        break;
    }
}

void list_zone(struct char_data* ch); /* forward declaration */

void list_help_zone(struct char_data* ch);

void write_command(FILE* f, struct zone_tree* zon)
{

    //  int j;

    struct reset_com* com;

    char comm_char;
    com = &(zon->comm);

    comm_char = com->command;
    if (comm_char <= ' ')
        comm_char = '.';
    fprintf(f, "%c %d %d %d %d %d %d", comm_char,

        com->if_flag, com->arg1, com->arg2, com->arg3, com->arg4, com->arg5);

    switch (com->command) {

    case 'M':
    case 'N':
    case 'H':
    case 'E':
    case 'Q':
    case 'K':
        fprintf(f, " %d %d", com->arg6, com->arg7);
        break;
    case 'P':
        fprintf(f, " %d", com->arg6);
        break;
    }

    if (zon->comment)
        fprintf(f, " %s", zon->comment);

    fprintf(f, "\n");
}

void write_zone(FILE* f, struct char_data* ch)
{

    struct zone_tree* tmpcomm;

    struct owner_list* owner;

    fprintf(f, "#%-d ", SHAPE_ZONE(ch)->zone_number);

    if (SHAPE_ZONE(ch)->zone_name)

        fprintf(f, "%s~\n", SHAPE_ZONE(ch)->zone_name);

    else
        fprintf(f, "~\n");

    if (SHAPE_ZONE(ch)->zone_descr)

        fprintf(f, "%s~\n", SHAPE_ZONE(ch)->zone_descr);

    else
        fprintf(f, "~\n");

    if (SHAPE_ZONE(ch)->zone_map)

        fprintf(f, "%s~\n", SHAPE_ZONE(ch)->zone_map);

    else
        fprintf(f, "~\n");

    owner = SHAPE_ZONE(ch)->root_owner;

    while (owner->owner) {

        fprintf(f, "%d ", owner->owner);

        owner = owner->next;

        if (!owner) {
            printf("no next owner\n");
            break;
        }
    }

    fprintf(f, "0\n");

    fprintf(f, "%c %d %d %d \n", SHAPE_ZONE(ch)->symbol,
        SHAPE_ZONE(ch)->x, SHAPE_ZONE(ch)->y, SHAPE_ZONE(ch)->level);

    fprintf(f, "%d \n%d \n%d\n", SHAPE_ZONE(ch)->top, SHAPE_ZONE(ch)->lifespan,

        SHAPE_ZONE(ch)->reset_mode);

    tmpcomm = SHAPE_ZONE(ch)->root;

    while (tmpcomm) {

        write_command(f, tmpcomm);

        tmpcomm = tmpcomm->next;
    }

    fprintf(f, "S\n");
}

// #define SUBST(x)   real->x=(char *)calloc(strlen(curr->x)+1,1);  strcpy(real->x,curr->x);

#define SUBST(x)                                \
    CREATE(real->x, char, strlen(curr->x) + 1); \
    strcpy(real->x, curr->x);

void implement_zone(struct char_data* ch)
{

    int adr, count;

    struct zone_tree* tmpzon;

    struct owner_list *tmpowner, *tmpowner2;
    if (!IS_SET(SHAPE_ZONE(ch)->flags, SHAPE_ZONE_LOADED)) {

        send_to_char("You have no zone loaded to implement.\n\r", ch);

        SHAPE_ZONE(ch)
            ->procedure
            = SHAPE_NONE;

        return;
    }

    adr = 0;

    while ((adr < MAX_ZONES) && (zone_table[adr].number != SHAPE_ZONE(ch)->zone_number)) {

        adr++;
    }

    if (adr == MAX_ZONES) {

        send_to_char("It seems there is no such zone in the world as you want to implement.\n\r", ch);

        SHAPE_ZONE(ch)
            ->procedure
            = SHAPE_EDIT;

        return;
    }

    if (SHAPE_ZONE(ch)->zone_name) {
        RELEASE(zone_table[adr].name);
        //    zone_table[adr].name=(char *)calloc(strlen(SHAPE_ZONE(ch)->zone_name)+1,1);
        CREATE(zone_table[adr].name, char, strlen(SHAPE_ZONE(ch)->zone_name) + 1);
        strcpy(zone_table[adr].name, SHAPE_ZONE(ch)->zone_name);
    }
    if (SHAPE_ZONE(ch)->zone_descr) {
        RELEASE(zone_table[adr].description);
        //    zone_table[adr].description=(char *)calloc(strlen(SHAPE_ZONE(ch)->zone_descr)+1,1);
        CREATE(zone_table[adr].description, char,
            strlen(SHAPE_ZONE(ch)->zone_descr) + 1);
        strcpy(zone_table[adr].description, SHAPE_ZONE(ch)->zone_descr);
    }
    if (SHAPE_ZONE(ch)->zone_map) {
        RELEASE(zone_table[adr].map);
        //    zone_table[adr].map=(char *)calloc(strlen(SHAPE_ZONE(ch)->zone_map)+1,1);
        CREATE(zone_table[adr].map, char, strlen(SHAPE_ZONE(ch)->zone_map) + 1);
        strcpy(zone_table[adr].map, SHAPE_ZONE(ch)->zone_map);
    }
    tmpowner = zone_table[adr].owners;
    count = 0;
    tmpowner = zone_table[adr].owners;
    while (tmpowner) {
        tmpowner2 = tmpowner->next;
        RELEASE(tmpowner);
        tmpowner = tmpowner2;
    }
    tmpowner = SHAPE_ZONE(ch)->root_owner;
    //  zone_table[adr].owners=(struct owner_list *)calloc(1,sizeof(struct owner_list));
    CREATE1(zone_table[adr].owners, owner_list);
    tmpowner2 = zone_table[adr].owners;
    tmpowner2->owner = tmpowner->owner;
    tmpowner = tmpowner->next;
    while (tmpowner) {
        //    tmpowner2->next=(struct owner_list *)calloc(1,sizeof(struct owner_list));
        CREATE1(tmpowner2->next, owner_list);
        tmpowner2 = tmpowner2->next;
        tmpowner2->owner = tmpowner->owner;
        tmpowner = tmpowner->next;
    }

    //  symbol_to_map(zone_table[adr].x,zone_table[adr].y, ' ');

    zone_table[adr].level = SHAPE_ZONE(ch)->level;

    if ((zone_table[adr].symbol != SHAPE_ZONE(ch)->symbol) || (zone_table[adr].x != SHAPE_ZONE(ch)->x) || (zone_table[adr].y != SHAPE_ZONE(ch)->y)) {
        zone_table[adr].x = SHAPE_ZONE(ch)->x;
        zone_table[adr].y = SHAPE_ZONE(ch)->y;
        zone_table[adr].symbol = SHAPE_ZONE(ch)->symbol;
        draw_map();
    }

    zone_table[adr].reset_mode = SHAPE_ZONE(ch)->reset_mode;
    zone_table[adr].lifespan = SHAPE_ZONE(ch)->lifespan;

    //  symbol_to_map(zone_table[adr].x,zone_table[adr].y, zone_table[adr].symbol);

    tmpzon = SHAPE_ZONE(ch)->root;

    count = 0;

    for (; tmpzon; tmpzon = tmpzon->next)
        count++;

    RELEASE(zone_table[adr].cmd);

    //  zone_table[adr].cmd=(struct reset_com *)calloc(count+1,sizeof(struct reset_com));
    CREATE(zone_table[adr].cmd, reset_com, count + 1);

    // printf("counted %d zone commands.\n",count);
    tmpzon = SHAPE_ZONE(ch)->root;

    count = 0;

    for (; tmpzon; tmpzon = tmpzon->next, count++) {

        zone_table[adr].cmd[count].command = tmpzon->comm.command;

        zone_table[adr].cmd[count].if_flag = tmpzon->comm.if_flag;

        zone_table[adr].cmd[count].arg1 = tmpzon->comm.arg1;

        zone_table[adr].cmd[count].arg2 = tmpzon->comm.arg2;

        zone_table[adr].cmd[count].arg3 = tmpzon->comm.arg3;

        zone_table[adr].cmd[count].arg4 = tmpzon->comm.arg4;

        zone_table[adr].cmd[count].arg5 = tmpzon->comm.arg5;

        zone_table[adr].cmd[count].arg6 = tmpzon->comm.arg6;

        zone_table[adr].cmd[count].arg7 = tmpzon->comm.arg7;

        zone_table[adr].cmd[count].existing = 0;
    }

    zone_table[adr].cmd[count].command = 'S';
    renum_zone_one(adr, ch); /* report any bad vnums straight to the builder */

    if (zone_table[adr].cmds_disabled > 0) {
        sprintf(buf, "The zone was implemented, but %d command(s) were DISABLED "
                     "because of the error(s) above.  Fix the vnum(s) and implement again.\n\r",
            zone_table[adr].cmds_disabled);
        send_to_char(buf, ch);
    } else {
        send_to_char("The zone was implemented.\n\r", ch);
    }
}

#undef SUBST

/****************************************************************/

#define DESCRCHANGE(line, addr)                                       \
    do {                                                              \
        if (!IS_SET(SHAPE_ZONE(ch)->flags, SHAPE_SIMPLE_ACTIVE)) {    \
            sprintf(tmpstr, "You are about to change %s:\n\r", line); \
            send_to_char(tmpstr, ch);                                 \
            SHAPE_ZONE(ch)                                            \
                ->position                                            \
                = shape_standup(ch, POSITION_SHAPING);                \
            ch->specials.prompt_number = 1;                           \
            SET_BIT(SHAPE_ZONE(ch)->flags, SHAPE_SIMPLE_ACTIVE);      \
            str[0] = 0;                                               \
            SHAPE_ZONE(ch)                                            \
                ->tmpstr                                              \
                = str_dup(addr);                                      \
            string_add_init(ch->desc, &(SHAPE_ZONE(ch)->tmpstr));     \
            return;                                                   \
        } else {                                                      \
            if (SHAPE_ZONE(ch)->tmpstr) {                             \
                addr = SHAPE_ZONE(ch)->tmpstr;                        \
                clean_text(addr);                                     \
            }                                                         \
            SHAPE_ZONE(ch)                                            \
                ->tmpstr                                              \
                = 0;                                                  \
            REMOVE_BIT(SHAPE_ZONE(ch)->flags, SHAPE_SIMPLE_ACTIVE);   \
            shape_standup(ch, SHAPE_ZONE(ch)->position);              \
            ch->specials.prompt_number = 7;                           \
            SHAPE_ZONE(ch)                                            \
                ->editflag                                            \
                = 0;                                                  \
            continue;                                                 \
        }                                                             \
    } while (0);
#define LINECHANGE(line, addr)                                                              \
    do {                                                                                    \
        if (!IS_SET(SHAPE_ZONE(ch)->flags, SHAPE_DIGIT_ACTIVE)) {                           \
            sprintf(tmpstr, "Enter line %s:\n\r[%s]\n\r", line, (addr) ? (char*)addr : ""); \
            send_to_char(tmpstr, ch);                                                       \
            SHAPE_ZONE(ch)                                                                  \
                ->position                                                                  \
                = shape_standup(ch, POSITION_SHAPING);                                      \
            ch->specials.prompt_number = 2;                                                 \
            SET_BIT(SHAPE_ZONE(ch)->flags, SHAPE_DIGIT_ACTIVE);                             \
            return;                                                                         \
        } else {                                                                            \
            str[0] = 0;                                                                     \
            if (!sscanf(arg, "%s", str)) {                                                  \
                SHAPE_ZONE(ch)                                                              \
                    ->editflag                                                              \
                    = 0;                                                                    \
                shape_standup(ch, SHAPE_ZONE(ch)->position);                                \
                ch->specials.prompt_number = 7;                                             \
                REMOVE_BIT(SHAPE_ZONE(ch)->flags, SHAPE_DIGIT_ACTIVE);                      \
                break;                                                                      \
            }                                                                               \
        }                                                                                   \
        if (str[0] != 0) {                                                                  \
            if (!strcmp(str, "%q")) {                                                       \
                send_to_char("Empty line set.\n\r", ch);                                    \
                arg[0] = 0;                                                                 \
            }                                                                               \
            RELEASE(addr);                                                                  \
            /*addr=(char *)calloc(strlen(arg)+1,1);*/                                       \
            CREATE(addr, char, strlen(arg) + 1);                                            \
            strcpy(addr, arg);                                                              \
            tmp[1] = strlen(addr);                                                          \
            for (tmp[0] = 0; tmp[0] < tmp[1]; tmp[0]++) {                                   \
                if (addr[tmp[0]] == '#')                                                    \
                    addr[tmp[0]] = '+';                                                     \
                if (addr[tmp[0]] == '~')                                                    \
                    addr[tmp[0]] = '-';                                                     \
            }                                                                               \
        }                                                                                   \
        REMOVE_BIT(SHAPE_ZONE(ch)->flags, SHAPE_DIGIT_ACTIVE);                              \
        shape_standup(ch, SHAPE_ZONE(ch)->position);                                        \
        ch->specials.prompt_number = 7;                                                     \
        SHAPE_ZONE(ch)                                                                      \
            ->editflag                                                                      \
            = 0;                                                                            \
    } while (0);

#define DIGITCHANGE(line, num)                                             \
    if (!IS_SET(SHAPE_ZONE(ch)->flags, SHAPE_DIGIT_ACTIVE)) {              \
        SHAPE_ZONE(ch)                                                     \
            ->position                                                     \
            = shape_standup(ch, POSITION_SHAPING);                         \
        ch->specials.prompt_number = 2;                                    \
        send_to_char(line, ch);                                            \
        SET_BIT(SHAPE_ZONE(ch)->flags, SHAPE_DIGIT_ACTIVE);                \
        return;                                                            \
    } else {                                                               \
        tmp2 = 0;                                                          \
        for (i = 0; i < num; i++) {                                        \
            while ((arg[tmp2] < '0') && (arg[tmp2] != '-') && (arg[tmp2])) \
                tmp2++;                                                    \
            tmp[i] = atoi(arg + tmp2);                                     \
            while ((arg[tmp2] > ' ') && (arg[tmp2]))                       \
                tmp2++;                                                    \
        }                                                                  \
    }                                                                      \
    shape_standup(ch, SHAPE_ZONE(ch)->position);                           \
    ch->specials.prompt_number = 7;                                        \
    REMOVE_BIT(SHAPE_ZONE(ch)->flags, SHAPE_DIGIT_ACTIVE);                 \
    SHAPE_ZONE(ch)                                                         \
        ->editflag                                                         \
        = 0;

#define REALDIGCHANGE(line, addr)                                      \
    do {                                                               \
        if (!IS_SET(SHAPE_ZONE(ch)->flags, SHAPE_DIGIT_ACTIVE)) {      \
            sprintf(tmpstr, "Enter %s [%d]:\n\r", line, addr);         \
            send_to_char(tmpstr, ch);                                  \
            SHAPE_ZONE(ch)                                             \
                ->position                                             \
                = shape_standup(ch, POSITION_SHAPING);                 \
            ch->specials.prompt_number = 3;                            \
            SET_BIT(SHAPE_ZONE(ch)->flags, SHAPE_DIGIT_ACTIVE);        \
            return;                                                    \
        } else {                                                       \
            for (tmp1 = 0; arg[tmp1] && arg[tmp1] <= ' '; tmp1++)      \
                ;                                                      \
            if (!arg[tmp1])                                            \
                tmp1 = addr;                                           \
            else if (!sscanf(arg, "%d", &tmp1)) {                      \
                send_to_char("a number required. dropped\n\r", ch);    \
                SHAPE_ZONE(ch)                                         \
                    ->editflag                                         \
                    = 0;                                               \
                shape_standup(ch, SHAPE_ZONE(ch)->position);           \
                ch->specials.prompt_number = 7;                        \
                REMOVE_BIT(SHAPE_ZONE(ch)->flags, SHAPE_DIGIT_ACTIVE); \
                return;                                                \
            }                                                          \
        }                                                              \
        addr = tmp1;                                                   \
        shape_standup(ch, SHAPE_ZONE(ch)->position);                   \
        ch->specials.prompt_number = 7;                                \
        REMOVE_BIT(SHAPE_ZONE(ch)->flags, SHAPE_DIGIT_ACTIVE);         \
        SHAPE_ZONE(ch)                                                 \
            ->editflag                                                 \
            = 0;                                                       \
    } while (0);

void extra_coms_zone(struct char_data* ch, char* argument);

void shape_center_zone(struct char_data* ch, char* arg)
{

    static char str[MAX_STRING_LENGTH * 2];
    char st1[50], st2[50], st3[50], st4[50], st5[50], st6[50], st7[50], st8[50], st9[50], tmpstr[255];
    int i, tmp1, tmp2, tmp[8], choice;
    struct zone_tree* current;
    struct zone_tree* tmpzon;
    struct owner_list *tmpowner, *tmpowner2;
    //  char key;

    choice = 0;

    current = SHAPE_ZONE(ch)->curr;
    tmp1 = SHAPE_ZONE(ch)->procedure;
    SHAPE_ZONE(ch)
        ->procedure
        = SHAPE_EDIT;
    if ((tmp1 != SHAPE_NONE) && (tmp1 != SHAPE_EDIT)) {
        // send_to_char("mixed orders. aborted - better restart shaping.\n\r",ch);
        extra_coms_zone(ch, arg);
        return;
    }
    if (tmp1 == SHAPE_NONE) {
        send_to_char("Enter any non-number for list of commands, 99 for list of editor commands:", ch);
        SHAPE_ZONE(ch)
            ->editflag
            = 0;
        REMOVE_BIT(SHAPE_ZONE(ch)->flags, SHAPE_SIMPLE_ACTIVE);
        return;
    }

    if (SHAPE_ZONE(ch)->editflag == 0) {
        sscanf(arg, "%s", str);
        if ((str[0] >= '0') && (str[0] <= '9')) {
            SHAPE_ZONE(ch)
                ->editflag
                = atoi(str);
            str[0] = 0;
            if (SHAPE_ZONE(ch)->editflag == 0) {
                list_help_zone(ch);
                return;
            }
        } else {
            extra_coms_zone(ch, arg);
            return;
        }
    }
    if (!IS_SET(SHAPE_ZONE(ch)->flags, SHAPE_ZONE_LOADED)) {
        send_to_char("You have no zone to edit.\n\r", ch);
        SHAPE_ZONE(ch)
            ->editflag
            = 0;
        return;
    }
    if (IS_SET(SHAPE_ZONE(ch)->flags, SHAPE_CURRFLAG))
        SHAPE_ZONE(ch)
            ->cur_room
            = world[ch->in_room].number;

    while (SHAPE_ZONE(ch)->editflag)

        switch (SHAPE_ZONE(ch)->editflag) {

        case 1: /* show current  */

            if (current) {
                if (current->prev) {
                    sprintf(str, "Prev: ");
                    show_command(str + strlen(str), current->prev);
                    send_to_char(str, ch);
                } else
                    send_to_char("No previous command.\n\r", ch);

                sprintf(str, "Curr: ");
                show_command(str + strlen(str), current);
                send_to_char(str, ch);

                if (current->next) {
                    sprintf(str, "Next: ");
                    show_command(str + strlen(str), current->next);
                    send_to_char(str, ch);
                } else
                    send_to_char("No next command.\n\r", ch);
            } else
                send_to_char("No current command.\n\r", ch);
            SHAPE_ZONE(ch)
                ->editflag
                = 0;
            break;

        case 2: /*Set mask*/

            if (!IS_SET(SHAPE_ZONE(ch)->flags, SHAPE_DIGIT_ACTIVE)) {
                sprintf(str, "ENTER MASK, use '*' to ignore parameter :\n\r");
                send_to_char(str, ch);
                SET_BIT(SHAPE_ZONE(ch)->flags, SHAPE_DIGIT_ACTIVE);
                ch->specials.prompt_number = 2;
                SHAPE_ZONE(ch)
                    ->position
                    = shape_standup(ch, POSITION_SHAPING);
                return;
            } else {

                st1[0] = st2[0] = st3[0] = st4[0] = st5[0] = st6[0] = st7[0] = st8[0] = st9[0] = 0;
                tmp1 = sscanf(arg, "%s %s %s %s %s %s %s %s %s", st1, st2, st3, st4, st5, st6, st7, st8, st9);
                ch->specials.prompt_number = 7;
                shape_standup(ch, SHAPE_ZONE(ch)->position);
                if (tmp1 == 0) {
                    send_to_char("Nothing entered. dropped.\n\r", ch);
                    SHAPE_ZONE(ch)
                        ->editflag
                        = 0;
                    return;
                }
                SHAPE_ZONE(ch)
                    ->mask.if_flag
                    = -1;
                SHAPE_ZONE(ch)
                    ->mask.arg1
                    = -1;
                SHAPE_ZONE(ch)
                    ->mask.arg2
                    = -1;
                SHAPE_ZONE(ch)
                    ->mask.arg3
                    = -1;
                SHAPE_ZONE(ch)
                    ->mask.arg4
                    = -1;
                SHAPE_ZONE(ch)
                    ->mask.arg5
                    = -1;
                SHAPE_ZONE(ch)
                    ->mask.arg6
                    = -1;
                SHAPE_ZONE(ch)
                    ->mask.arg7
                    = -1;

                if (st1[0])
                    SHAPE_ZONE(ch)
                        ->mask.command
                        = st1[0];
                if ((tmp1 > 1) && (st2[0] != '*') && (st2[0]))
                    SHAPE_ZONE(ch)
                        ->mask.if_flag
                        = atoi(st2);
                if ((tmp1 > 2) && (st3[0] != '*') && (st3[0]))
                    SHAPE_ZONE(ch)
                        ->mask.arg1
                        = atoi(st3);
                if ((tmp1 > 3) && (st4[0] != '*') && (st4[0]))
                    SHAPE_ZONE(ch)
                        ->mask.arg2
                        = atoi(st4);
                if ((tmp1 > 4) && (st5[0] != '*') && (st5[0]))
                    SHAPE_ZONE(ch)
                        ->mask.arg3
                        = atoi(st5);
                if ((tmp1 > 5) && (st6[0] != '*') && (st6[0]))
                    SHAPE_ZONE(ch)
                        ->mask.arg4
                        = atoi(st6);
                if ((tmp1 > 6) && (st7[0] != '*') && (st7[0]))
                    SHAPE_ZONE(ch)
                        ->mask.arg5
                        = atoi(st7);
                if ((tmp1 > 7) && (st8[0] != '*') && (st8[0]))
                    SHAPE_ZONE(ch)
                        ->mask.arg6
                        = atoi(st8);
                if ((tmp1 > 8) && (st9[0] != '*') && (st9[0]))
                    SHAPE_ZONE(ch)
                        ->mask.arg7
                        = atoi(st9);
            }

            REMOVE_BIT(SHAPE_ZONE(ch)->flags, SHAPE_DIGIT_ACTIVE);
            SHAPE_ZONE(ch)
                ->editflag
                = 0;
            break;

        case 3: /*Set command*/

            if (!IS_SET(SHAPE_ZONE(ch)->flags, SHAPE_DIGIT_ACTIVE)) {
                sprintf(str, "ENTER COMMAND TYPE <MOGEPDKAL>:\n\r");
                send_to_char(str, ch);
                SET_BIT(SHAPE_ZONE(ch)->flags, SHAPE_DIGIT_ACTIVE);
                ch->specials.prompt_number = 2;
                SHAPE_ZONE(ch)
                    ->position
                    = shape_standup(ch, POSITION_SHAPING);
                return;
            } else {
                sscanf(arg, "%s", st1);
                ch->specials.prompt_number = 7;
                shape_standup(ch, SHAPE_ZONE(ch)->position);
                if (tmp == 0) {
                    send_to_char("Nothing entered. dropped.\n\r", ch);
                    SHAPE_ZONE(ch)
                        ->editflag
                        = 0;
                    return;
                }
                switch (st1[0]) {
                case 'M':
                case 'N':
                case 'O':
                case 'G':
                case 'H':
                case 'E':
                case 'Q':
                case 'P':
                case 'D':
                case 'L':
                case 'A':
                case 'K':
                case 'm':
                case 'n':
                case 'o':
                case 'g':
                case 'h':
                case 'e':
                case 'q':
                case 'p':
                case 'd':
                case 'l':
                case 'a':
                case 'k':
                    SHAPE_ZONE(ch)
                        ->curr->comm.command
                        = toupper(st1[0]);
                    SHAPE_ZONE(ch)
                        ->editflag
                        = 4;
                    break;
                default:
                    send_to_char("Unrecognized command.\n\r", ch);
                    SHAPE_ZONE(ch)
                        ->editflag
                        = 0;
                    break;
                }
            }
            REMOVE_BIT(SHAPE_ZONE(ch)->flags, SHAPE_DIGIT_ACTIVE);
            break;

        case 4:
            tmp[0] = tmp[1] = tmp[2] = tmp[3] = tmp[4] = tmp[5] = tmp[6] = tmp[7] = 0;
            switch (SHAPE_ZONE(ch)->curr->comm.command) {
            case 'A':
                DIGITCHANGE("Enter type and 2 parameters (no if_flag):\n\r", 3);
                SHAPE_ZONE(ch)
                    ->editflag
                    = 5;
                tmp[3] = tmp[2];
                tmp[2] = tmp[1];
                tmp[1] = tmp[0];
                tmp[0] = 1;
                break;
            case 'O':
            case 'G':
                DIGITCHANGE("Enter if_flag and 5 parameters:\n\r", 6);
                SHAPE_ZONE(ch)
                    ->editflag
                    = 5;
                break;
            case 'P':
                DIGITCHANGE("Enter if_flag and 6 parameters:\n\r", 7);
                SHAPE_ZONE(ch)
                    ->editflag
                    = 5;
                break;
            case 'L':
                DIGITCHANGE("Enter if_flag, mode  and 3 parameters:\n\r", 7);
                SHAPE_ZONE(ch)
                    ->editflag
                    = 5;
                break;
            case 'K':
                DIGITCHANGE("Enter if_flag and up to 7 objects:\n\r", 8);
                SHAPE_ZONE(ch)
                    ->editflag
                    = 5;
                break;
            case 'D':
                DIGITCHANGE("Enter <if_flag> <room> <dir> <state(0-2)>:\n\r", 4);
                /*tmp[3]=convert_exit_flag(tmp[3],0);*/
                if (tmp[3] > 2)
                    tmp[3] = 2;
                if (tmp[2] > 5)
                    tmp[3] = 0;
                if (tmp[2] < 0)
                    tmp[2] = 0;
                SHAPE_ZONE(ch)
                    ->editflag
                    = 5;
                break;
            case 'M':
            case 'N':
            case 'H':
            case 'E':
            case 'Q':
                DIGITCHANGE("Enter if_flag and 7 parameters:\n\r", 8);
                SHAPE_ZONE(ch)
                    ->editflag
                    = 5;
                break;
            default:
                send_to_char("Unrecognized command type. Dropped.\n\r", ch);
                SHAPE_ZONE(ch)
                    ->editflag
                    = 0;
                break;
            }
            if (SHAPE_ZONE(ch)->editflag) {
                SHAPE_ZONE(ch)
                    ->curr->comm.if_flag
                    = tmp[0];
                SHAPE_ZONE(ch)
                    ->curr->comm.arg1
                    = tmp[1];
                SHAPE_ZONE(ch)
                    ->curr->comm.arg2
                    = tmp[2];
                SHAPE_ZONE(ch)
                    ->curr->comm.arg3
                    = tmp[3];
                SHAPE_ZONE(ch)
                    ->curr->comm.arg4
                    = tmp[4];
                SHAPE_ZONE(ch)
                    ->curr->comm.arg5
                    = tmp[5];
                SHAPE_ZONE(ch)
                    ->curr->comm.arg6
                    = tmp[6];
                SHAPE_ZONE(ch)
                    ->curr->comm.arg7
                    = tmp[7];
                SHAPE_ZONE(ch)
                    ->editflag
                    = 5;
            }

            break;

        case 5: /* Set comment */
            /*    if(!IS_SET(SHAPE_ZONE(ch)->flags,SHAPE_DIGIT_ACTIVE)){
      sprintf(tmpstr,"ENTER one-line COMMENT:\n\r");
      send_to_char(tmpstr,ch);
      SET_BIT(SHAPE_ZONE(ch)->flags,SHAPE_DIGIT_ACTIVE);
      ch->specials.prompt_number=2;
      SHAPE_ZONE(ch)->position=shape_standup(ch,POSITION_SHAPING);
      return;
    }
    else{
      if(!arg){
        send_to_char("string required. dropped\n\r",ch);
        SHAPE_ZONE(ch)->editflag=0;
        ch->specials.prompt_number=7;
        shape_standup(ch,SHAPE_ZONE(ch)->position);
        return;
      }
      RELEASE(SHAPE_ZONE(ch)->curr->comment);
      //      SHAPE_ZONE(ch)->curr->comment=(char *)calloc(strlen(arg)+1,1);
      CREATE( SHAPE_ZONE(ch)->curr->comment, char, strlen(arg) + 1);
      strcpy(SHAPE_ZONE(ch)->curr->comment,arg);
    }
    REMOVE_BIT(SHAPE_ZONE(ch)->flags,SHAPE_DIGIT_ACTIVE);
    SHAPE_ZONE(ch)->editflag=0;
    ch->specials.prompt_number=7;
    shape_standup(ch,SHAPE_ZONE(ch)->position);
    break;
    */
            LINECHANGE("COMMENT", SHAPE_ZONE(ch)->curr->comment);
            break;
        case 6: /* Choose next */
            tmpzon = SHAPE_ZONE(ch)->curr->next;
            while (tmpzon) {
                if (!SHAPE_ZONE(ch)->cur_room || (SHAPE_ZONE(ch)->cur_room == tmpzon->room))
                    break;
                tmpzon = tmpzon->next;
            }
            if (tmpzon) {
                SHAPE_ZONE(ch)
                    ->curr
                    = tmpzon;
                send_to_char("Next command chosen:\n\r", ch);
                show_command(str, SHAPE_ZONE(ch)->curr);
                send_to_char(str, ch);
            } else
                send_to_char("No next command.\n\r", ch);
            SHAPE_ZONE(ch)
                ->editflag
                = 0;
            break;
        case 7: /* Choose prev */

            tmpzon = SHAPE_ZONE(ch)->curr->prev;
            while (tmpzon) {
                if (!SHAPE_ZONE(ch)->cur_room || (SHAPE_ZONE(ch)->cur_room == tmpzon->room))
                    break;
                tmpzon = tmpzon->prev;
            }
            if (tmpzon) {
                SHAPE_ZONE(ch)
                    ->curr
                    = tmpzon;
                send_to_char("Previous command chosen:\n\r", ch);
                show_command(str, SHAPE_ZONE(ch)->curr);
                send_to_char(str, ch);
            } else
                send_to_char("No previous command.\n\r", ch);
            SHAPE_ZONE(ch)
                ->editflag
                = 0;
            break;
        case 8:
            REALDIGCHANGE("New command number:", tmp1);
            for (tmpzon = SHAPE_ZONE(ch)->root; tmpzon; tmpzon = tmpzon->next)
                if (tmpzon->number == tmp1)
                    break;
            if (!tmpzon)
                send_to_char("Wrong command number.\n\r", ch);
            else {
                SHAPE_ZONE(ch)
                    ->curr
                    = tmpzon;
                if (SHAPE_ZONE(ch)->cur_room && (SHAPE_ZONE(ch)->cur_room != tmpzon->room)) {
                    SHAPE_ZONE(ch)
                        ->cur_room
                        = tmpzon->room;
                    send_to_char("The 'current room' number changed.\n\r", ch);
                }
            }
            break;

        case 9: /* remove command */
            if (!IS_SET(SHAPE_ZONE(ch)->flags, SHAPE_DIGIT_ACTIVE)) {
                sprintf(str, "REMOVE current command?<yn>:\n\r");
                send_to_char(str, ch);
                SET_BIT(SHAPE_ZONE(ch)->flags, SHAPE_DIGIT_ACTIVE);
                ch->specials.prompt_number = 2;
                SHAPE_ZONE(ch)
                    ->position
                    = shape_standup(ch, POSITION_SHAPING);
                return;
            } else {
                st1[0] = 0;
                sscanf(arg, "%s", st1);
                ch->specials.prompt_number = 7;
                shape_standup(ch, SHAPE_ZONE(ch)->position);
                REMOVE_BIT(SHAPE_ZONE(ch)->flags, SHAPE_DIGIT_ACTIVE);
                if ((st1[0] != 'y') && (st1[0] != 'Y')) {
                    send_to_char("Dropped.\n\r", ch);
                    SHAPE_ZONE(ch)
                        ->editflag
                        = 0;
                    break;
                }
            }
            tmpzon = SHAPE_ZONE(ch)->curr->prev;
            if (tmpzon) {
                tmpzon->next = SHAPE_ZONE(ch)->curr->next;
                if (tmpzon->next)
                    (tmpzon->next)->prev = tmpzon;
            } else {
                if (!SHAPE_ZONE(ch)->curr->next) {
                    send_to_char("Can't remove the only command, sorry.\n\r", ch);
                    SHAPE_ZONE(ch)
                        ->editflag
                        = 0;
                    break;
                    ;
                }
                SHAPE_ZONE(ch)
                    ->root
                    = SHAPE_ZONE(ch)->curr->next;
                (SHAPE_ZONE(ch)->root)->prev = 0;
                tmpzon = SHAPE_ZONE(ch)->root;
            }
            RELEASE(SHAPE_ZONE(ch)->curr->comment);
            RELEASE(SHAPE_ZONE(ch)->curr);
            SHAPE_ZONE(ch)
                ->curr
                = tmpzon;
            send_to_char("Command removed.\n\r", ch);
            renum_rooms(SHAPE_ZONE(ch)->root);
            SHAPE_ZONE(ch)
                ->editflag
                = 0;
            break;

        case 10: /* insert after */
            CREATE1(tmpzon, zone_tree);
            tmpzon->prev = SHAPE_ZONE(ch)->curr;
            if (SHAPE_ZONE(ch)->curr->next)
                (SHAPE_ZONE(ch)->curr->next)->prev = tmpzon;
            tmpzon->next = SHAPE_ZONE(ch)->curr->next;
            SHAPE_ZONE(ch)
                ->curr->next
                = tmpzon;
            CREATE(tmpzon->comment, char, 1);
            tmpzon->comment[0] = 0;
            SHAPE_ZONE(ch)
                ->curr
                = SHAPE_ZONE(ch)->curr->next;
            renum_rooms(SHAPE_ZONE(ch)->root);
            send_to_char("New command added next to current, and selected.\n\r", ch);
            SHAPE_ZONE(ch)
                ->editflag
                = 0;
            break;

        case 11: /* insert before */
            CREATE1(tmpzon, zone_tree);
            bzero((char*)(tmpzon), sizeof(struct zone_tree));
            tmpzon->next = SHAPE_ZONE(ch)->curr;
            tmpzon->prev = SHAPE_ZONE(ch)->curr->prev;
            if (SHAPE_ZONE(ch)->curr->prev)
                (SHAPE_ZONE(ch)->curr->prev)->next = tmpzon;
            else {
                SHAPE_ZONE(ch)
                    ->root
                    = tmpzon;
                tmpzon->prev = 0;
            }
            SHAPE_ZONE(ch)
                ->curr->prev
                = tmpzon;
            CREATE(tmpzon->comment, char, 1);
            tmpzon->comment[0] = 0;
            SHAPE_ZONE(ch)
                ->curr
                = SHAPE_ZONE(ch)->curr->prev;
            renum_rooms(SHAPE_ZONE(ch)->root);
            send_to_char("New command added before current, and selected.\n\r", ch);
            SHAPE_ZONE(ch)
                ->editflag
                = 0;
            break;

        case 12:
            REALDIGCHANGE("'CURRENT ROOM' number", SHAPE_ZONE(ch)->cur_room);
            if (IS_SET(SHAPE_ZONE(ch)->flags, SHAPE_CURRFLAG)) {
                REMOVE_BIT(SHAPE_ZONE(ch)->flags, SHAPE_CURRFLAG);
                send_to_char("The auto 'current room' mode removed.\n\r", ch);
            }
            break;
        case 13:
            if (!SHAPE_ZONE(ch)->curr->next) {
                send_to_char("There is no next command, not switched.\n\r", ch);
            } else {
                tmpzon = SHAPE_ZONE(ch)->curr->next;

                if (tmpzon->next)
                    tmpzon->next->prev = SHAPE_ZONE(ch)->curr;
                SHAPE_ZONE(ch)
                    ->curr->next
                    = tmpzon->next;

                if (SHAPE_ZONE(ch)->curr->prev)
                    SHAPE_ZONE(ch)
                        ->curr->prev->next
                        = tmpzon;
                else
                    SHAPE_ZONE(ch)
                        ->root
                        = tmpzon;
                tmpzon->prev = SHAPE_ZONE(ch)->curr->prev;

                tmpzon->next = SHAPE_ZONE(ch)->curr;
                SHAPE_ZONE(ch)
                    ->curr->prev
                    = tmpzon;
                SHAPE_ZONE(ch)
                    ->curr
                    = tmpzon;

                send_to_char("Switched the current and the next commands,\n\rthe next command selected.\n\r", ch);
            }
            SHAPE_ZONE(ch)
                ->editflag
                = 0;
            break;
        case 14: /* add owner */
            if (ch->player.level < LEVEL_AREAGOD) {
                send_to_char("You are not godly anough for this.\n\r", ch);
                SHAPE_ZONE(ch)
                    ->editflag
                    = 0;
                break;
            }
            if (!get_permission(SHAPE_ZONE(ch)->zone_number, ch, 1)) {
                send_to_char("You may change owners of this zone.\n\r", ch);
                SHAPE_ZONE(ch)
                    ->editflag
                    = 0;
                break;
            }
            DIGITCHANGE("Enter new zone owner:\n\r", 1);
            //    tmpowner=(struct owner_list *)calloc(1,sizeof(struct owner_list));
            CREATE1(tmpowner, owner_list);
            tmpowner->next = SHAPE_ZONE(ch)->root_owner;
            SHAPE_ZONE(ch)
                ->root_owner
                = tmpowner;
            tmpowner->owner = tmp[0];
            break;
        case 15: /* remove owner */
            if (ch->player.level < LEVEL_AREAGOD) {
                send_to_char("You are not godly anough for this.\n\r", ch);
                SHAPE_ZONE(ch)
                    ->editflag
                    = 0;
                break;
            }
            if (!get_permission(SHAPE_ZONE(ch)->zone_number, ch, 1)) {
                send_to_char("You may change owners of this zone.\n\r", ch);
                SHAPE_ZONE(ch)
                    ->editflag
                    = 0;
                break;
            }
            if (SHAPE_ZONE(ch)->root_owner->owner == 0) {
                send_to_char("This is common access zone, can't remove owner.\n\r", ch);
                SHAPE_ZONE(ch)
                    ->editflag
                    = 0;

                break;
            }
            DIGITCHANGE("Enter the zone owner to remove:\n\r", 1);
            tmpowner2 = tmpowner = SHAPE_ZONE(ch)->root_owner;
            printf("removing the owner %d.\n", tmp[0]);
            while ((tmpowner->owner != tmp[0]) && (tmpowner->owner != 0)) {
                tmpowner2 = tmpowner;
                tmpowner = tmpowner->next;
            }
            if (tmpowner->owner != tmp[0]) {
                send_to_char("There is no such owner.\n\r", ch);
                SHAPE_ZONE(ch)
                    ->editflag
                    = 0;

                break;
            }
            tmpowner2->next = tmpowner->next;
            if (tmpowner == SHAPE_ZONE(ch)->root_owner)
                SHAPE_ZONE(ch)
                    ->root_owner
                    = tmpowner->next;
            RELEASE(tmpowner);
            SHAPE_ZONE(ch)
                ->editflag
                = 0;

            break;
        case 16:
            if (ch->player.level < LEVEL_GRGOD) {
                send_to_char("You are not godly anough for this.\n\r", ch);
                SHAPE_ZONE(ch)
                    ->editflag
                    = 0;
                break;
            }
            DIGITCHANGE("Enter new zone coordinates, x y :\n\r", 2);
            SHAPE_ZONE(ch)
                ->x
                = tmp[0];
            SHAPE_ZONE(ch)
                ->y
                = tmp[1];
            SHAPE_ZONE(ch)
                ->editflag
                = 0;
            break;
        case 17:
            if (ch->player.level < LEVEL_GRGOD) {
                send_to_char("You are not godly anough for this.\n\r", ch);
                SHAPE_ZONE(ch)
                    ->editflag
                    = 0;
                break;
            }
            if (!IS_SET(SHAPE_ZONE(ch)->flags, SHAPE_DIGIT_ACTIVE)) {
                sprintf(tmpstr, "Enter zone map symbol:[%c]\n\r",
                    SHAPE_ZONE(ch)->symbol);
                send_to_char(tmpstr, ch);
                SHAPE_ZONE(ch)
                    ->position
                    = shape_standup(ch, POSITION_SHAPING);
                ch->specials.prompt_number = 2;
                SET_BIT(SHAPE_ZONE(ch)->flags, SHAPE_DIGIT_ACTIVE);
                return;
            } else {
                if (arg[0] >= ' ')
                    SHAPE_ZONE(ch)
                        ->symbol
                        = arg[0];
                SHAPE_ZONE(ch)
                    ->editflag
                    = 0;
                shape_standup(ch, SHAPE_ZONE(ch)->position);
                ch->specials.prompt_number = 7;
                REMOVE_BIT(SHAPE_ZONE(ch)->flags, SHAPE_DIGIT_ACTIVE);
                SHAPE_ZONE(ch)
                    ->editflag
                    = 0;
            }
            break;
        case 20:
            LINECHANGE("ZONE NAME", SHAPE_ZONE(ch)->zone_name);
            break;
        case 21:
            DESCRCHANGE("ZONE DESCRIPTION", SHAPE_ZONE(ch)->zone_descr);
            break;
        case 22:
            DESCRCHANGE("ZONE MAP", SHAPE_ZONE(ch)->zone_map);
            break;
        case 23:
            tmp[0] = SHAPE_ZONE(ch)->lifespan;
            DIGITCHANGE("Enter RESET TIME:", 1);
            SHAPE_ZONE(ch)
                ->editflag
                = 5;
            SHAPE_ZONE(ch)
                ->lifespan
                = tmp[0];
            SHAPE_ZONE(ch)
                ->editflag
                = 0;
            break;
        case 24:
            tmp[0] = SHAPE_ZONE(ch)->reset_mode;
            DIGITCHANGE("Enter RESET MODE (0-3):", 1);
            SHAPE_ZONE(ch)
                ->editflag
                = 5;
            SHAPE_ZONE(ch)
                ->reset_mode
                = tmp[0];
            SHAPE_ZONE(ch)
                ->editflag
                = 0;
            break;
        case 25:
            tmp[0] = SHAPE_ZONE(ch)->level;
            DIGITCHANGE("Enter ZONE_LEVEL:", 1);
            SHAPE_ZONE(ch)
                ->editflag
                = 5;
            SHAPE_ZONE(ch)
                ->level
                = tmp[0];
            SHAPE_ZONE(ch)
                ->editflag
                = 0;
            break;
        case 50:

            list_zone(ch);

            SHAPE_ZONE(ch)
                ->editflag
                = 0;

            break;

        case 51:
            sprintf(str, "Zone #%d: %s\n\rReset time: %d; Reset mode: %d\n\r",
                SHAPE_ZONE(ch)->zone_number, SHAPE_ZONE(ch)->zone_name,
                SHAPE_ZONE(ch)->lifespan, SHAPE_ZONE(ch)->reset_mode);
            send_to_char(str, ch);
            sprintf(str, "Symbol:'%c', Coords - x:%d y:%d, Level:%d\n\r",
                SHAPE_ZONE(ch)->symbol, SHAPE_ZONE(ch)->x,
                SHAPE_ZONE(ch)->y, SHAPE_ZONE(ch)->level);
            send_to_char(str, ch);

            sprintf(str, "Owners: ");
            tmpowner = SHAPE_ZONE(ch)->root_owner;
            if (tmpowner->owner == 0)
                strcat(str, "All");
            else
                while (tmpowner->owner != 0) {
                    sprintf(tmpstr, " %d", tmpowner->owner);
                    strcat(str, tmpstr);
                    tmpowner = tmpowner->next;
                }
            strcat(str, "\n\r\n\r");
            send_to_char(str, ch);
            send_to_char(SHAPE_ZONE(ch)->zone_descr, ch);
            send_to_char("\n\r-----------------------------------------------\n\r", ch);
            send_to_char(SHAPE_ZONE(ch)->zone_map, ch);
            SHAPE_ZONE(ch)
                ->editflag
                = 0;

            break;
        default:

            list_help_zone(ch);

            SHAPE_ZONE(ch)
                ->editflag
                = 0;

            break;
        }

    return;
}

#undef DESCRCHANGE
#undef LINECHANGE
#undef DIGITCHANGE

void list_help_zone(struct char_data* ch)
{

    send_to_char("possible fields are:\n\r", ch);
    send_to_char("1 - show current command;\n\r", ch);
    send_to_char("2 - set mask for list of commands;\n\r", ch);
    send_to_char("3 - change current command;\n\r", ch);
    send_to_char("4 - change parameters of the current command;\n\r", ch);
    send_to_char("5 - set comment on current command;\n\r", ch);
    send_to_char("/3 invokes /4 and /5 as well, /4 invokes /5.\n\r", ch);
    send_to_char("6 - select next command;\n\r", ch);
    send_to_char("7 - select previoust command;\n\r", ch);
    send_to_char("8 - select a command by number;\n\r", ch);
    send_to_char("9 - remove current command;\n\r", ch);
    send_to_char("10 - insert new command after the current one;\n\r", ch);
    send_to_char("11 - insert new command before the current one;\n\r", ch);
    send_to_char("12 - define the 'current room' option (0 to ignore);\n\r", ch);
    send_to_char("13 - switch the current and the next commands;\n\r\n\r", ch);

    if (ch->player.level >= LEVEL_AREAGOD) {
        send_to_char("14 - add new owner;\n\r", ch);
        send_to_char("15 - remove an owner;\n\r\n\r", ch);
    }
    if (ch->player.level > LEVEL_GRGOD) {
        send_to_char("16 - set zone coordinates;\n\r", ch);
        send_to_char("17 - set zone map symbol;\n\r", ch);
    }

    send_to_char("20 - change zone name;\n\r", ch);
    send_to_char("21 - change zone description;\n\r", ch);
    send_to_char("22 - change zone map;\n\r", ch);
    send_to_char("23 - change zone reset time;\n\r", ch);
    send_to_char("24 - change zone reset mode;\n\r", ch);
    send_to_char("25 - change zone level;\n\r", ch);

    send_to_char("50 - list;\n\r", ch);

    send_to_char("51 - show zone name, description, map.\n\r", ch);
    return;
}

/*********--------------------------------*********/

void list_zone(struct char_data* ch)
{

    static char str[MAX_STRING_LENGTH];

    int check;
    struct zone_tree* zon;
    struct reset_com* mask = &(SHAPE_ZONE(ch)->mask);

    sprintf(str, "Mask is: %c ", mask->command);
    if (mask->if_flag != -1)
        sprintf(str + strlen(str), "%d ", mask->if_flag);
    else
        sprintf(str + strlen(str), "* ");

    if (mask->arg1 != -1)
        sprintf(str + strlen(str), "%d ", mask->arg1);
    else
        sprintf(str + strlen(str), "* ");
    if (mask->arg2 != -1)
        sprintf(str + strlen(str), "%d ", mask->arg2);
    else
        sprintf(str + strlen(str), "* ");
    if (mask->arg3 != -1)
        sprintf(str + strlen(str), "%d ", mask->arg3);
    else
        sprintf(str + strlen(str), "* ");
    if (mask->arg4 != -1)
        sprintf(str + strlen(str), "%d ", mask->arg4);
    else
        sprintf(str + strlen(str), "* ");
    if (mask->arg5 != -1)
        sprintf(str + strlen(str), "%d ", mask->arg5);
    else
        sprintf(str + strlen(str), "* ");
    if (mask->arg6 != -1)
        sprintf(str + strlen(str), "%d ", mask->arg6);
    else
        sprintf(str + strlen(str), "* ");
    if (mask->arg7 != -1)
        sprintf(str + strlen(str), "%d\n\r", mask->arg7);
    else
        sprintf(str + strlen(str), "*\n\r");
    send_to_char(str, ch);
    str[0] = 0;
    zon = SHAPE_ZONE(ch)->root;
    // printf("cur_room = %d\n",SHAPE_ZONE(ch)->cur_room);
    while (zon) {
        check = 1;
        if (SHAPE_ZONE(ch)->cur_room) {
            check &= (SHAPE_ZONE(ch)->cur_room == zon->room);
            //	 printf("check=%d, comm_room=%d\n",check,zon->room);
        }
        if (mask->command != '*')
            check &= (mask->command == zon->comm.command);
        if (mask->if_flag != -1)
            check &= (mask->if_flag == zon->comm.if_flag);
        if (mask->arg1 != -1)
            check &= (mask->arg1 == zon->comm.arg1);
        if (mask->arg2 != -1)
            check &= (mask->arg2 == zon->comm.arg2);
        if (mask->arg3 != -1)
            check &= (mask->arg3 == zon->comm.arg3);
        if (mask->arg4 != -1)
            check &= (mask->arg4 == zon->comm.arg4);
        if (mask->arg5 != -1)
            check &= (mask->arg5 == zon->comm.arg5);
        switch (zon->comm.command) {
        case 'M':
        case 'N':
        case 'H':
        case 'E':
        case 'Q':
            if (mask->arg6 != -1)
                check &= (mask->arg6 == zon->comm.arg6);
            if (mask->arg7 != -1)
                check &= (mask->arg7 == zon->comm.arg7);
            break;
        }
        if (check) {
            show_command(str, zon);
            send_to_char(str, ch);
        }
        zon = zon->next;
    }
}
/*********--------------------------------*********/

int find_zone(FILE* f, int n)
{

    int check, i;

    char c;

    do {

        do {

            check = fscanf(f, "%c", &c);

        } while ((c != '#') && (check != EOF));

        //    res=ftell(f)-1;

        fscanf(f, "%d", &i);

    } while ((i != n) && (check != EOF));

    if (check == EOF)
        return -1;

    else
        return i;
}

int get_text(FILE* f, char** line); /* exist in protos.c */

/****************-------------------------------------****************/

// extern struct room_data world;

int load_zone(struct char_data* ch, char* arg)
{

    int tmp, num, number, comm_num;
    int tmp1, tmp2, tmp3, tmp4, tmp5, tmp6;
    char str[255], fname[80];
    FILE* f;
    struct zone_tree *zon, *prv;
    struct owner_list* owner;

    if (2 != sscanf(arg, "%s %d\n", str, &number)) {
        send_to_char("Choose a zone by 'shape zone <zone_number>'\n\r", ch);
        return -1;
    }
    sprintf(fname, "%d", number);
    sprintf(str, SHAPE_ZON_DIR, fname);
    send_to_char(str, ch);
    f = fopen(str, "r+");
    if (f == 0) {
        send_to_char("could not open that file.\n\r", ch);
        return -1;
    }
    strcpy(SHAPE_ZONE(ch)->f_from, str);
    SET_BIT(SHAPE_ZONE(ch)->flags, SHAPE_FILENAME);

    sprintf(SHAPE_ZONE(ch)->f_old, SHAPE_ZON_BACKDIR, fname);

    tmp = find_zone(f, number);
    if (tmp == -1) {
        send_to_char("no zone here.\n\r", ch);
        fclose(f);
        return -1;
    }

    CREATE1(SHAPE_ZONE(ch)->root, zone_tree);
    SHAPE_ZONE(ch)
        ->root->prev
        = 0;
    get_text(f, &(SHAPE_ZONE(ch)->zone_name));
    get_text(f, &(SHAPE_ZONE(ch)->zone_descr));
    get_text(f, &(SHAPE_ZONE(ch)->zone_map));
    sprintf(str, " loading zone #%d %s\n\r", tmp, SHAPE_ZONE(ch)->zone_name);
    send_to_char(str, ch);

    CREATE1(SHAPE_ZONE(ch)->root_owner, owner_list);
    SHAPE_ZONE(ch)
        ->permission
        = 0;
    owner = SHAPE_ZONE(ch)->root_owner;
    do {
        fscanf(f, "%d", &tmp);
        owner->owner = tmp;
        if (tmp == ch->specials2.idnum)
            SHAPE_ZONE(ch)
                ->permission
                = 1;
        if (tmp) {
            CREATE1(owner->next, owner_list);
            owner = owner->next;
        }
    } while (tmp);

    owner->next = 0;
    if (SHAPE_ZONE(ch)->root_owner->owner == 0 || GET_LEVEL(ch) >= LEVEL_GRGOD)
        SHAPE_ZONE(ch)
            ->permission
            = 1;
    if (!SHAPE_ZONE(ch)->permission)
        send_to_char("You have no authority over this zone. Limited permission only.\n\r", ch);

    fgets(str, 255, f);
    do {
        fread(&(SHAPE_ZONE(ch)->symbol), 1, 1, f);
    } while (SHAPE_ZONE(ch)->symbol <= ' ');
    fgets(str, 255, f);
    SHAPE_ZONE(ch)
        ->level
        = 0;
    sscanf(str, "%d %d %d", &(SHAPE_ZONE(ch)->x), &(SHAPE_ZONE(ch)->y),
        &(SHAPE_ZONE(ch)->level));

    fscanf(f, "%d %d %d", &(SHAPE_ZONE(ch)->top), &(SHAPE_ZONE(ch)->lifespan),
        &(SHAPE_ZONE(ch)->reset_mode));

    SHAPE_ZONE(ch)
        ->zone_number
        = number;
    zon = SHAPE_ZONE(ch)->root;
    SHAPE_ZONE(ch)
        ->curr
        = zon;
    prv = 0;
    num = 0;

    comm_num = 0;
    while (1) {
        fscanf(f, "%s", str);
        if (str[0] == 'S')
            break;
        if (str[0] == '*') {
            fgets(str, 254, f);
        } else {
            num++;
            zon->comm.command = str[0];
            CREATE1(zon->next, zone_tree);
            zon->next->next = 0;
            bzero((char*)(zon->next), sizeof(struct zone_tree));
            fscanf(f, "%d %d %d %d %d %d", &tmp1, &tmp2, &tmp3, &tmp4, &tmp5, &tmp6);
            zon->comm.if_flag = tmp1;
            zon->comm.arg1 = tmp2;
            zon->comm.arg2 = tmp3;
            zon->comm.arg3 = tmp4;
            zon->comm.arg4 = tmp5;
            zon->comm.arg5 = tmp6;
            switch (str[0]) {
            case 'M':
            case 'N':
            case 'H':
            case 'E':
            case 'Q':
            case 'K':
                fscanf(f, "%d %d", &tmp1, &tmp2);
                zon->comm.arg6 = tmp1;
                zon->comm.arg7 = tmp2;
                break;
            case 'P':
                fscanf(f, "%d", &tmp1);
                zon->comm.arg6 = tmp1;
                break;
            default:
                zon->comm.arg6 = zon->comm.arg7 = 0;
                break;
            }
            fgets(str, 254, f);
            for (tmp = 0; str[tmp] && str[tmp] <= ' '; tmp++)
                ;
            str[strlen(str) - 1] = 0;
            if ((int)strlen(str) + 1 <= tmp)
                tmp = strlen(str);
            CREATE(zon->comment, char, strlen(str) + 1 - tmp);
            strcpy(zon->comment, str + tmp);
            zon->prev = prv;
            if (prv)
                prv->next = zon;
            prv = zon;
            zon->room = command_room(&zon->comm);
            zon = zon->next;
            comm_num++;
            zon->number = comm_num;
        }
    }
    if (num != 0) {
        RELEASE(zon);
        if (prv)
            prv->next = 0;
        sprintf(str, "Zone loaded, %d commands found.\n\r", num);
        send_to_char(str, ch);
    } else {
        send_to_char("Zone loaded, 0 commands found, fake command inserted.\n\r",
            ch);
        SHAPE_ZONE(ch)
            ->root->next
            = 0;
        //     SHAPE_ZONE(ch)->root->comment=(char *)calloc(1,1);
        CREATE(SHAPE_ZONE(ch)->root->comment, char, 1);
    }
    ch->specials.prompt_value = number;
    SET_BIT(SHAPE_ZONE(ch)->flags, SHAPE_ZONE_LOADED);
    fclose(f);
    return number;
}

/*****************----------------------------------******************/

int replace_zone(struct char_data* ch, char* arg)
{

    /* this procedure is used for deleting objects, too */

    char str[255];
    char mybuf[MAX_STRING_LENGTH];
    char *f_from, *f_old, *check;
    char c;
    int i, num;
    FILE* f1;
    FILE* f2;

    /*  if(3!=sscanf(arg,"%s %s %s",str,f_from,f_old)){

    send_to_char("format is <file_from> <file_to>\n\r",ch);

    return -1;

  }

*/

    num = SHAPE_ZONE(ch)->zone_number;

    if (!IS_SET(SHAPE_ZONE(ch)->flags, SHAPE_FILENAME)) {

        send_to_char("How strange... you have no file defined to write to.\n\r",

            ch);

        return -1;
    }

    f_from = SHAPE_ZONE(ch)->f_from;

    f_old = SHAPE_ZONE(ch)->f_old;

    if (!IS_SET(SHAPE_ZONE(ch)->flags, SHAPE_ZONE_LOADED)) {

        send_to_char("you have no zone to save...\n\r", ch);

        return -1;
    }

    if (!strcmp(f_from, f_old)) {

        send_to_char("better make source and target files different\n\r", ch);

        return -1;
    }

    /* Here goes file backuping... */

    f1 = fopen(f_from, "r+");

    if (!f1) {
        send_to_char("could not open source file\n\r", ch);
        return -1;
    }

    f2 = fopen(f_old, "w+");

    if (!f2) {
        send_to_char("could not open backup file\n\r", ch);

        fclose(f1);
        return -1;
    }

    do {

        check = fgets(mybuf, MAX_STRING_LENGTH, f1);
        if (check)
            fputs(mybuf, f2);

    } while (check);

    fclose(f1);
    fclose(f2);

    f2 = fopen(f_from, "w+");

    if (!f2) {
        send_to_char("could not open source file-2\n\r", ch);
        return -1;
    }

    f1 = fopen(f_old, "r");

    if (!f1) {
        send_to_char("could not open backup file-2\n\r", ch);

        fclose(f2);
        return -1;
    }

    do {

        do {

            check = fgets(mybuf, MAX_STRING_LENGTH, f1);
            c = mybuf[0];
            if (check && (c != '#'))
                fputs(mybuf, f2);
        } while ((c != '#') && (check));

        sscanf(mybuf + 1, "%d", &i);

        //    if(i!=num)fprintf(f2,"%-d",i);

    } while ((i != num) && (check));

    if (!check) {
        sprintf(str, "no zone #%d in this file\n", num);

        send_to_char(str, ch);

        fclose(f1);
        fclose(f2);

        return -1;
    }

    do {

        check = fgets(mybuf, MAX_STRING_LENGTH, f1);
        c = mybuf[0];
    } while ((c != '#') && check);

    /*  if(c!='#'){printf("strange file... aborted\n"); return -1;}

*/

    //  fseek(f2,ftell(f2)-1,SEEK_SET);

    if (!IS_SET(SHAPE_ZONE(ch)->flags, SHAPE_DELETE_ACTIVE)) {

        write_zone(f2, ch);

        REMOVE_BIT(SHAPE_ZONE(ch)->flags, SHAPE_DELETE_ACTIVE);
    }

    //  fprintf(f2,"#");
    fputs(mybuf, f2);

    for (; i != EOF;) {
        i = fscanf(f1, "%c", &c);
        if (i != EOF)
            fprintf(f2, "%c", c);
    }

    fclose(f1);
    fclose(f2);

    return num;
}
void free_zone(struct char_data* ch)
{
    struct zone_tree *tmp, *tmp2;
    struct owner_list *owner, *owner2;
    if (IS_SET(SHAPE_ZONE(ch)->flags, SHAPE_ZONE_LOADED)) {
        tmp = SHAPE_ZONE(ch)->root;
        while (tmp) {
            RELEASE(tmp->comment);
            tmp2 = tmp->next;
            RELEASE(tmp);
            tmp = tmp2;
        }
        SHAPE_ZONE(ch)
            ->root
            = 0;
        SHAPE_ZONE(ch)
            ->curr
            = 0;
        RELEASE(SHAPE_ZONE(ch)->zone_name);
    }
    RELEASE(SHAPE_ZONE(ch)->tmpstr);
    owner = SHAPE_ZONE(ch)->root_owner;
    while (owner) {
        owner2 = owner->next;
        RELEASE(owner);
        owner = owner2;
    }
    // RELEASE(SHAPE_ZONE(ch));
    RELEASE(ch->temp);
    REMOVE_BIT(PRF_FLAGS(ch), PRF_DISPTEXT);
    if (GET_POS(ch) <= POSITION_SHAPING)
        GET_POS(ch) = POSITION_STANDING;
}

/****************************** main() ********************************/

void extra_coms_zone(struct char_data* ch, char* argument)
{

    //  extern struct room_data world;

    int comm_key, room_number; //,i;

    char str[255], str2[255];

    room_number = ch->in_room;

    /*  printf("shape center: flags=%d\n",SHAPE_ZONE(ch)->flags);

*/

    if (SHAPE_ZONE(ch)->procedure == SHAPE_EDIT) {

        send_to_char("you invoked some rhymes from shapeless indefinity...\n\r", ch);

        str[0] = 0;
        sscanf(argument, "%s %s", str, str2);
        do {
            if (!strlen(str))
                strcpy(str, "weird");
            if (!strncmp(str, "create", strlen(str))) {
                comm_key = SHAPE_CREATE;
                break;
            }
            /*   no zone creation possible :) */
            if (!strncmp(str, "load", strlen(str))) {
                comm_key = SHAPE_LOAD;
                break;
            }
            if (!strncmp(str, "save", strlen(str))) {
                comm_key = SHAPE_SAVE;
                break;
            }
            if (!strncmp(str, "add", strlen(str))) {
                comm_key = SHAPE_ADD;
                break;
            }
            if (!strncmp(str, "free", strlen(str))) {
                comm_key = SHAPE_FREE;
                break;
            }
            if (!strncmp(str, "done", strlen(str))) {
                comm_key = SHAPE_DONE;
                break;
            }
            if (!strncmp(str, "delete", strlen(str))) {
                comm_key = SHAPE_DELETE;
                break;
            }
            if (!strncmp(str, "current", strlen(str))) {
                comm_key = SHAPE_CURRENT;
                break;
            }
            if (!strncmp(str, "implement", strlen(str))) {
                comm_key = SHAPE_IMPLEMENT;
                break;
            }

            send_to_char("Possible commands are:\n\r", ch);
            send_to_char("load <zone_number>;\n\r", ch);
            send_to_char("save; \n\r", ch);
            send_to_char("add; \n\r", ch);
            send_to_char("implement; \n\r", ch);
            send_to_char("current - to see the listing for your room only;\n\r", ch);
            send_to_char("done - to save your job, implement it and stop shaping.;\n\r", ch);
            send_to_char("free - to stop shaping zone.\n\r", ch);
            send_to_char("\n\r", ch);
            send_to_char("\n\r", ch);
            return;
        } while (0);

        /*    SHAPE_ZONE(ch)->procedure=comm_key;*/
    } else
        comm_key = SHAPE_ZONE(ch)->procedure;
    switch (comm_key) {
    case SHAPE_CREATE:
        send_to_char("Zone cannot be created that simple.\n\r If you want a new zone, talk to the Implementors.\n\r", ch);
        break;
    case SHAPE_FREE:
        free_zone(ch);
        send_to_char("You released your job and stop shaping.\n\r", ch);
        break;
    case SHAPE_LOAD:
        if (!IS_SET(SHAPE_ZONE(ch)->flags, SHAPE_ZONE_LOADED)) {
            if (load_zone(ch, argument) < 0) {
                free_zone(ch);
                break;
            }
        } else
            send_to_char("you already have something to care about\n\r", ch);
        SHAPE_ZONE(ch)
            ->procedure
            = SHAPE_EDIT;
        break;
    case SHAPE_SAVE:
        if (SHAPE_ZONE(ch)->permission || GET_LEVEL(ch) >= LEVEL_GRGOD)
            replace_zone(ch, argument);
        else
            send_to_char("You are not allowed to save this zone.\n\r", ch);
        SHAPE_ZONE(ch)
            ->procedure
            = SHAPE_EDIT;
        break;
    case SHAPE_IMPLEMENT:
        if (SHAPE_ZONE(ch)->permission || GET_LEVEL(ch) >= LEVEL_GRGOD)
            implement_zone(ch);
        else
            send_to_char("You are not allowed to implement this zone.\n\r", ch);
        SHAPE_ZONE(ch)
            ->procedure
            = SHAPE_EDIT;
        break;
    case SHAPE_DONE:
        if (SHAPE_ZONE(ch)->permission || GET_LEVEL(ch) >= LEVEL_GRGOD) {
            replace_zone(ch, argument);
            implement_zone(ch);
        } else
            send_to_char("You are not allowed to save this zone.\n\r", ch);
        extra_coms_zone(ch, "free");
        break;
    case SHAPE_CURRENT:
        if (IS_SET(SHAPE_ZONE(ch)->flags, SHAPE_CURRFLAG)) {
            REMOVE_BIT(SHAPE_ZONE(ch)->flags, SHAPE_CURRFLAG);
            SHAPE_ZONE(ch)
                ->cur_room
                = 0;
            send_to_char("You will now see the whole zone list.\n\r", ch);
        } else {
            SET_BIT(SHAPE_ZONE(ch)->flags, SHAPE_CURRFLAG);
            SHAPE_ZONE(ch)
                ->cur_room
                = world[ch->in_room].number;
            send_to_char("You will now see the listing of your room only.\n\r", ch);
        }
    }
    return;
}
