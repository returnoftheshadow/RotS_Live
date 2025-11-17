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
#include "spells.h"
#include "structs.h"
#include "utils.h"

extern struct room_data world;
extern struct skill_data skills[];

extern char num_of_sector_types;

int simple_edit(struct char_data* ch, char** str, char* arg);
int shape_standup(struct char_data* ch, int pos);
struct affected_type* get_from_affected_type_pool();
void put_to_affected_type_pool(struct affected_type*);

void list_room(struct char_data* ch, struct room_data* mob); /* forward declaration */
void list_help_room(struct char_data* ch);

int convert_exit_flag(int tmp, int mode)
{
    /* mode 0 means formal code to real flag, mode 1 - real flag to formal */
    int res;
    // printf("converting %d.\n",tmp);
    res = 0;
    if (!mode) {
        if (tmp >= 100) {
            res = EX_NOFLEE;
            tmp -= 100;
        }
        do {
            if ((tmp >= 10) && (tmp < 20)) {
                res |= EX_NO_LOOK;
                tmp -= 10;
                break;
            }
            if ((tmp >= 20) && (tmp < 30)) {
                res |= EX_DOORISHEAVY;
                tmp -= 20;
                break;
            }
            if ((tmp >= 30) && (tmp < 40)) {
                res |= EX_NO_LOOK | EX_DOORISHEAVY;
                tmp -= 30;
                break;
            }
        } while (0);
        if (tmp == 0)
            res |= 0;
        if (tmp == 1)
            res |= EX_ISDOOR;
        if (tmp == 2)
            res |= EX_ISDOOR | EX_ISHIDDEN;
        if (tmp == 3)
            res |= EX_ISDOOR | EX_PICKPROOF;
        if (tmp == 4)
            res |= EX_ISDOOR | EX_ISHIDDEN | EX_PICKPROOF;
        // printf("mode 0, returning %d\n",res);
        return res;
    } else {
        res = 0;
        if (tmp & EX_NO_LOOK)
            res = 10;
        if (tmp & EX_DOORISHEAVY)
            res = 20;
        if ((tmp & EX_NO_LOOK) && (tmp & EX_DOORISHEAVY))
            res = 30;
        if (tmp & EX_NOFLEE)
            res += 100;
        tmp &= ~(EX_NO_LOOK | EX_DOORISHEAVY | EX_LOCKED | EX_CLOSED);
        switch (tmp) {
        case EX_ISDOOR:
            res += 1;
            break;
        case EX_ISDOOR | EX_ISHIDDEN:
            res += 2;
            break;
        case EX_ISDOOR | EX_PICKPROOF:
            res += 3;
            break;
        case EX_ISDOOR | EX_ISHIDDEN | EX_PICKPROOF:
            res += 4;
            break;

        default:
            break;
        }
        // printf("mode 1, returning %d\n",res);
        return res;
    }
    return 0;
}

int room_chain[50] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 13, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 1
};

int new_room(struct char_data* ch, int number)
{
    int i;
    if (!SHAPE_ROOM(ch)->room) {
        //    SHAPE_ROOM(ch)->room=(struct room_data *)calloc(1,sizeof(struct room_data));
        CREATE1(SHAPE_ROOM(ch)->room, room_data);
        //    bzero((char *)(SHAPE_ROOM(ch)->room),sizeof(struct room_data));
        //    SHAPE_ROOM(ch)->room->name=(char *)calloc(1,1);
        //    SHAPE_ROOM(ch)->room->description=(char *)calloc(1,1);
        CREATE(SHAPE_ROOM(ch)->room->name, char, 1);
        CREATE(SHAPE_ROOM(ch)->room->description, char, 1);

        for (i = 0; i < NUM_OF_DIRS; i++)
            SHAPE_ROOM(ch)
                ->room->dir_option[i]
                = 0;
    } else {
        send_to_char("Your previous shaping is not complete. go away.\n\r", ch);
        return -1;
    }
    SHAPE_ROOM(ch)
        ->room->number
        = number; /*    ????????     to check    */

    SHAPE_ROOM(ch)
        ->room->room_flags
        = 0;
    SHAPE_ROOM(ch)
        ->room->sector_type
        = 0;

    SHAPE_ROOM(ch)
        ->room->funct
        = 0;
    SHAPE_ROOM(ch)
        ->room->contents
        = 0;
    SHAPE_ROOM(ch)
        ->room->people
        = 0;
    SHAPE_ROOM(ch)
        ->room->light
        = 0;
    return number;
}
void write_room(FILE* f, struct room_data* m, int num)
{
    struct extra_descr_data* tmpdescr;
    struct affected_type* tmpaf;
    int i, flg;

    fprintf(f, "#%-d  \n\r", num);
    fprintf(f, "%s~\n\r", m->name);
    fprintf(f, "%s~\n\r", m->description);
    fprintf(f, "%d %ld %d %d\n\r", 0, m->room_flags, m->sector_type, m->level);
    tmpdescr = m->ex_description;
    while (tmpdescr) {
        fprintf(f, "E\n\r%s~\n%s~\n\r", tmpdescr->keyword, tmpdescr->description);
        tmpdescr = tmpdescr->next;
    }
    for (i = 0; i < NUM_OF_DIRS; i++)
        if (m->dir_option[i]) {
            fprintf(f, "D%-d\n\r", i);
            fprintf(f, "%s~\n\r%s~\n\r", m->dir_option[i]->general_description,
                m->dir_option[i]->keyword);

            flg = m->dir_option[i]->exit_info;

            fprintf(f, "%d %d %d %d\n\r", flg, m->dir_option[i]->key, m->dir_option[i]->to_room, m->dir_option[i]->exit_width);
        }

    tmpaf = m->affected;
    while (tmpaf) {
        fprintf(f, "F\n\r%d %d %d %ld\n\r", tmpaf->type, tmpaf->location,
            tmpaf->modifier, tmpaf->bitvector);
        tmpaf = tmpaf->next;
    }
    fprintf(f, "S\n\r");
}

/*
 * This was the previous definition of SUBT that was used
 * when the Windows processing macro was defined.  It's
 * interesting because it releases real->, unless our
 * current SUBST().
 * #define SUBST(x)                              \
 *  if (real->x)                                \
 *    RELEASE(real->x);                         \
 *  CREATE(real->x, char, strlen(curr->x) + 1); \
 *  strcpy(real->x, curr->x);                   \
 */
#define SUBST(x)                                \
    CREATE(real->x, char, strlen(curr->x) + 1); \
    strcpy(real->x, curr->x);

void implement_room(struct char_data* ch)
{
    struct room_data *real, *curr;
    int rnum, i, tmp;
    struct extra_descr_data *tmpdescr, *tmp2descr;
    struct affected_type* tmpaf;

    if (!SHAPE_ROOM(ch)->room) {
        send_to_char("You have nothing to implement\n\r", ch);
        return;
    }
    if (!IS_SET(SHAPE_ROOM(ch)->flags, SHAPE_ROOM_LOADED)) {
        send_to_char("You have nothing to implement\n\r", ch);
        return;
    }
    if (SHAPE_ROOM(ch)->permission == 0) {
        send_to_char("You may not do that in this zone.\n\r", ch);
        return;
    }

    rnum = real_room(SHAPE_ROOM(ch)->room->number);
    if (rnum < 0) {
        send_to_char("This room does not exist (yet). Maybe reboot will help.\n\r", ch);
        return;
    }
    real = &(world[rnum]);
    curr = SHAPE_ROOM(ch)->room;

    SUBST(name);
    SUBST(description);
    real->room_flags = curr->room_flags;

    real->sector_type = curr->sector_type;

    if (real->sector_type >= num_of_sector_types)
        real->sector_type = num_of_sector_types - 1;

    tmpdescr = real->ex_description;
    while (tmpdescr) {
        RELEASE(tmpdescr->keyword);
        RELEASE(tmpdescr->description);
        tmp2descr = tmpdescr->next;
        RELEASE(tmpdescr);
        tmpdescr = tmp2descr;
    }
    real->ex_description = 0;

    while (real->affected)
        affect_remove_room(real, real->affected);

    tmpdescr = curr->ex_description;
    tmp2descr = 0;
    while (tmpdescr) {

        CREATE1(real->ex_description, extra_descr_data);
        real->ex_description->next = tmp2descr;

        CREATE(real->ex_description->keyword, char, strlen(tmpdescr->keyword) + 1);
        strcpy(real->ex_description->keyword, tmpdescr->keyword);

        CREATE(real->ex_description->description, char,
            strlen(tmpdescr->description) + 1);
        strcpy(real->ex_description->description, tmpdescr->description);
        tmpdescr = tmpdescr->next;
        tmp2descr = real->ex_description;
    }

    tmpaf = curr->affected;

    while (tmpaf) {

        affect_to_room(real, tmpaf);
        tmpaf = tmpaf->next;
    };

    for (tmpaf = real->affected; tmpaf; tmpaf = tmpaf->next)
        tmpaf->bitvector |= PERMAFFECT;

    for (i = 0; i < NUM_OF_DIRS; i++) {
        if (curr->dir_option[i]) {
            if (!real->dir_option[i]) {
                // 	real->dir_option[i]=(struct room_direction_data *)calloc(1,sizeof(struct room_direction_data));
                CREATE1(real->dir_option[i], room_direction_data);
            }
            // bzero((char *)(real->dir_option[i]),sizeof(struct room_direction_data));
            if (*(curr->dir_option[i]->general_description)) {
                SUBST(dir_option[i]->general_description);
            } else {
                //	  real->dir_option[i]->general_description=(char *)calloc(1,1);
                CREATE(real->dir_option[i]->general_description, char, 1);
            }
            SUBST(dir_option[i]->keyword);
            real->dir_option[i]->exit_info = curr->dir_option[i]->exit_info;
            real->dir_option[i]->exit_width = curr->dir_option[i]->exit_width;
            real->dir_option[i]->key = curr->dir_option[i]->key;
            tmp = real_room(curr->dir_option[i]->to_room);
            if (tmp == -1)
                tmp = NOWHERE;
            real->dir_option[i]->to_room = tmp;
        } else {
            if (real->dir_option[i]) {
                RELEASE(real->dir_option[i]->keyword);
                RELEASE(real->dir_option[i]->general_description);
                RELEASE(real->dir_option[i]);
                real->dir_option[i] = 0;
            }
        }
    }
    send_to_char("Room implemented.\n\r", ch);
}
#undef SUBST
/****************************************************************/
#define DESCRCHANGE(line, addr)                                       \
    do {                                                              \
        if (!IS_SET(SHAPE_ROOM(ch)->flags, SHAPE_SIMPLE_ACTIVE)) {    \
            sprintf(tmpstr, "You are about to change %s:\n\r", line); \
            send_to_char(tmpstr, ch);                                 \
            SHAPE_ROOM(ch)                                            \
                ->position                                            \
                = shape_standup(ch, POSITION_SHAPING);                \
            ch->specials.prompt_number = 1;                           \
            SET_BIT(SHAPE_ROOM(ch)->flags, SHAPE_SIMPLE_ACTIVE);      \
            str[0] = 0;                                               \
            SHAPE_ROOM(ch)                                            \
                ->tmpstr                                              \
                = str_dup(addr);                                      \
            string_add_init(ch->desc, &(SHAPE_ROOM(ch)->tmpstr));     \
            return;                                                   \
        } else {                                                      \
            if (SHAPE_ROOM(ch)->tmpstr) {                             \
                addr = SHAPE_ROOM(ch)->tmpstr;                        \
                clean_text(addr);                                     \
            }                                                         \
            SHAPE_ROOM(ch)                                            \
                ->tmpstr                                              \
                = 0;                                                  \
            REMOVE_BIT(SHAPE_ROOM(ch)->flags, SHAPE_SIMPLE_ACTIVE);   \
            shape_standup(ch, SHAPE_ROOM(ch)->position);              \
            ch->specials.prompt_number = 4;                           \
            SHAPE_ROOM(ch)                                            \
                ->editflag                                            \
                = 0;                                                  \
            continue;                                                 \
        }                                                             \
    } while (0);

#define LINECHANGE(line, addr)                                                   \
    if (!IS_SET(SHAPE_ROOM(ch)->flags, SHAPE_DIGIT_ACTIVE)) {                    \
        sprintf(tmpstr, "Enter line %s:\n\r[%s]\n\r", line, (addr) ? addr : ""); \
        send_to_char(tmpstr, ch);                                                \
        SHAPE_ROOM(ch)                                                           \
            ->position                                                           \
            = shape_standup(ch, POSITION_SHAPING);                               \
        ch->specials.prompt_number = 2;                                          \
        SET_BIT(SHAPE_ROOM(ch)->flags, SHAPE_DIGIT_ACTIVE);                      \
        return;                                                                  \
    } else {                                                                     \
        str[0] = 0;                                                              \
        if (!sscanf(arg, "%s", str)) {                                           \
            SHAPE_ROOM(ch)                                                       \
                ->editflag                                                       \
                = 0;                                                             \
            shape_standup(ch, SHAPE_ROOM(ch)->position);                         \
            ch->specials.prompt_number = 4;                                      \
            REMOVE_BIT(SHAPE_ROOM(ch)->flags, SHAPE_DIGIT_ACTIVE);               \
            break;                                                               \
        }                                                                        \
    }                                                                            \
    if (str[0] != 0) {                                                           \
        if (!strcmp(str, "%q")) {                                                \
            send_to_char("Empty line set.\n\r", ch);                             \
            arg[0] = 0;                                                          \
        }                                                                        \
        RELEASE(addr);                                                           \
        /*addr=(char *)calloc(strlen(arg)+1,1);*/                                \
        CREATE(addr, char, strlen(arg) + 1);                                     \
        strcpy(addr, arg);                                                       \
        tmp1 = strlen(addr);                                                     \
        for (tmp = 0; tmp < tmp1; tmp++) {                                       \
            if (addr[tmp] == '#')                                                \
                addr[tmp] = '+';                                                 \
            if (addr[tmp] == '~')                                                \
                addr[tmp] = '-';                                                 \
        }                                                                        \
    }                                                                            \
    REMOVE_BIT(SHAPE_ROOM(ch)->flags, SHAPE_DIGIT_ACTIVE);                       \
    shape_standup(ch, SHAPE_ROOM(ch)->position);                                 \
    ch->specials.prompt_number = 4;                                              \
    SHAPE_ROOM(ch)                                                               \
        ->editflag                                                               \
        = 0;

char* exit_convert(int i)
{
    switch (i) {
    case UP:
        return "UP";
    case DOWN:
        return "DOWN";
    case NORTH:
        return "NORTH";
    case EAST:
        return "EAST";
    case SOUTH:
        return "SOUTH";
    case WEST:
        return "WEST";
    default:
        return "VOID";
    }
}
void extra_coms_room(struct char_data* ch, char* arg);

void shape_center_room(struct char_data* ch, char* arg)
{

    char str[1000], tmpstr[1000];
    int tmp, choice, tmp1, tmp2, tmp3;
    struct room_data* mob;
    struct extra_descr_data* tmpdescr;
    struct affected_type* tmpaf;

    // char key;
    // char * tmppt;
    choice = 0;
    tmp = SHAPE_ROOM(ch)->procedure;
    mob = SHAPE_ROOM(ch)->room;
    //  SHAPE_ROOM(ch)->procedure=SHAPE_EDIT;
    if ((tmp != SHAPE_NONE) && (tmp != SHAPE_EDIT)) {
        //    send_to_char("mixed orders. aborted - better restart shaping.\n\r",ch);
        extra_coms_room(ch, arg);
        return;
    }
    if (tmp == SHAPE_NONE) {
        send_to_char("Enter any non-number for list of commands, 99 for list of editor commands:", ch);
        SHAPE_ROOM(ch)
            ->editflag
            = 0;
        REMOVE_BIT(SHAPE_ROOM(ch)->flags, SHAPE_SIMPLE_ACTIVE);
        return;
    }
    do { /* big loop for chains */
        if (SHAPE_ROOM(ch)->editflag == 0) {
            sscanf(arg, "%s", str);
            if ((str[0] >= '0') && (str[0] <= '9')) {
                SHAPE_ROOM(ch)
                    ->editflag
                    = atoi(str); /* now switching to integers.. */
                str[0] = 0;
            } else {
                extra_coms_room(ch, arg);
                return;
            }
        }
        if (!IS_SET(SHAPE_ROOM(ch)->flags, SHAPE_ROOM_LOADED)) {
            send_to_char("You have nothing to shape.\n\r", ch);
            SHAPE_ROOM(ch)
                ->editflag
                = 0;
            return;
        }
        switch (SHAPE_ROOM(ch)->editflag) {
        case 1:
            LINECHANGE("NAME", SHAPE_ROOM(ch)->room->name)
            if (IS_SET(SHAPE_ROOM(ch)->flags, SHAPE_CHAIN))
                SHAPE_ROOM(ch)
                    ->editflag
                    = room_chain[1];
            break;
        case 2:
            DESCRCHANGE("DESCRIPTION", SHAPE_ROOM(ch)->room->description)
            if (IS_SET(SHAPE_ROOM(ch)->flags, SHAPE_CHAIN))
                SHAPE_ROOM(ch)
                    ->editflag
                    = room_chain[2];
            break;
        case 8:
            if (SHAPE_ROOM(ch)->exit_chosen < 0) {
                send_to_char("No exit selected for editing\n\r", ch);
                SHAPE_ROOM(ch)
                    ->editflag
                    = 0;
                break;
            }
            if (!mob->dir_option[SHAPE_ROOM(ch)->exit_chosen]) {
                SHAPE_ROOM(ch)
                    ->editflag
                    = 0;
                break;
            }
            LINECHANGE("EXIT KEYWORD", mob->dir_option[SHAPE_ROOM(ch)->exit_chosen]->keyword);
            if (IS_SET(SHAPE_ROOM(ch)->flags, SHAPE_CHAIN))
                SHAPE_ROOM(ch)
                    ->editflag
                    = room_chain[8];
            break;
        case 9:
            if (SHAPE_ROOM(ch)->exit_chosen < 0) {
                send_to_char("No exit selected for editing\n\r", ch);
                SHAPE_ROOM(ch)
                    ->editflag
                    = 0;
                break;
            }
            if (!mob->dir_option[SHAPE_ROOM(ch)->exit_chosen]) {
                SHAPE_ROOM(ch)
                    ->editflag
                    = 0;
                break;
            }
            DESCRCHANGE("EXIT DESCRIPTION", mob->dir_option[SHAPE_ROOM(ch)->exit_chosen]->general_description);
            if (IS_SET(SHAPE_ROOM(ch)->flags, SHAPE_CHAIN))
                SHAPE_ROOM(ch)
                    ->editflag
                    = room_chain[9];
            break;
        case 13:
            if (!SHAPE_ROOM(ch)->room->ex_description) {
                send_to_char("No extra description exists.\n\r", ch);
                SHAPE_ROOM(ch)
                    ->editflag
                    = 0;
                break;
            }
            LINECHANGE("EXTRA DESCRIPTION KEYWORD", SHAPE_ROOM(ch)->room->ex_description->keyword);
            if (IS_SET(SHAPE_ROOM(ch)->flags, SHAPE_CHAIN))
                SHAPE_ROOM(ch)
                    ->editflag
                    = room_chain[3];
            break;
        case 14:
            if (!SHAPE_ROOM(ch)->room->ex_description) {
                send_to_char("No extra description exists.\n\r", ch);
                SHAPE_ROOM(ch)
                    ->editflag
                    = 0;
                break;
            }
            DESCRCHANGE("ROOM EXTRA DESCRIPTION", SHAPE_ROOM(ch)->room->ex_description->description);
            if (IS_SET(SHAPE_ROOM(ch)->flags, SHAPE_CHAIN))
                SHAPE_ROOM(ch)
                    ->editflag
                    = room_chain[14];
            break;

#define DIGITCHANGE(line, addr)                               \
    if (!IS_SET(SHAPE_ROOM(ch)->flags, SHAPE_DIGIT_ACTIVE)) { \
        sprintf(tmpstr, "enter %s [%d]:\n\r", line, addr);    \
        send_to_char(tmpstr, ch);                             \
        SHAPE_ROOM(ch)                                        \
            ->position                                        \
            = shape_standup(ch, POSITION_SHAPING);            \
        ch->specials.prompt_number = 3;                       \
        SET_BIT(SHAPE_ROOM(ch)->flags, SHAPE_DIGIT_ACTIVE);   \
        return;                                               \
    } else {                                                  \
        tmp = addr;                                           \
        string_to_new_value(arg, &tmp);                       \
        addr = tmp;                                           \
    }                                                         \
    shape_standup(ch, SHAPE_ROOM(ch)->position);              \
    ch->specials.prompt_number = 4;                           \
    REMOVE_BIT(SHAPE_ROOM(ch)->flags, SHAPE_DIGIT_ACTIVE);    \
    SHAPE_ROOM(ch)                                            \
        ->editflag                                            \
        = 0;
#define DIGITCHANGEL(line, addr)                              \
    if (!IS_SET(SHAPE_ROOM(ch)->flags, SHAPE_DIGIT_ACTIVE)) { \
        sprintf(tmpstr, "enter %s [%ld]:\n\r", line, addr);   \
        send_to_char(tmpstr, ch);                             \
        SHAPE_ROOM(ch)                                        \
            ->position                                        \
            = shape_standup(ch, POSITION_SHAPING);            \
        ch->specials.prompt_number = 3;                       \
        SET_BIT(SHAPE_ROOM(ch)->flags, SHAPE_DIGIT_ACTIVE);   \
        return;                                               \
    } else {                                                  \
        tmp = addr;                                           \
        string_to_new_value(arg, &tmp);                       \
        addr = tmp;                                           \
    }                                                         \
    shape_standup(ch, SHAPE_ROOM(ch)->position);              \
    ch->specials.prompt_number = 4;                           \
    REMOVE_BIT(SHAPE_ROOM(ch)->flags, SHAPE_DIGIT_ACTIVE);    \
    SHAPE_ROOM(ch)                                            \
        ->editflag                                            \
        = 0;

        case 3:
            DIGITCHANGEL("ROOM FLAG", mob->room_flags);
            if (IS_SET(SHAPE_ROOM(ch)->flags, SHAPE_CHAIN))
                SHAPE_ROOM(ch)
                    ->editflag
                    = room_chain[3];
            break;
        case 4:
            DIGITCHANGE("SECTOR TYPE", mob->sector_type);
            if (IS_SET(SHAPE_ROOM(ch)->flags, SHAPE_CHAIN))
                SHAPE_ROOM(ch)
                    ->editflag
                    = room_chain[4];
            break;
        /*-----------here go new exit features...----------*/
        case 7:
            if (!IS_SET(SHAPE_ROOM(ch)->flags, SHAPE_DIGIT_ACTIVE)) {
                send_to_char("Enter EXIT TO REMOVE:\n\r", ch);
                SHAPE_ROOM(ch)
                    ->position
                    = shape_standup(ch, POSITION_SHAPING);
                ch->specials.prompt_number = 3;
                SET_BIT(SHAPE_ROOM(ch)->flags, SHAPE_DIGIT_ACTIVE);
                return;
            } else {
                str[0] = 0;
                sscanf(arg, "%s", str);
                tmp = -1;
                if ((str[0] == 'U') || (str[0] == 'u'))
                    tmp = UP;
                if ((str[0] == 'D') || (str[0] == 'd'))
                    tmp = DOWN;
                if ((str[0] == 'N') || (str[0] == 'n'))
                    tmp = NORTH;
                if ((str[0] == 'E') || (str[0] == 'e'))
                    tmp = EAST;
                if ((str[0] == 'S') || (str[0] == 's'))
                    tmp = SOUTH;
                if ((str[0] == 'W') || (str[0] == 'w'))
                    tmp = WEST;
                if (tmp == -1) {
                    send_to_char("a letter required. dropped\n\r", ch);
                    SHAPE_ROOM(ch)
                        ->editflag
                        = 0;
                    shape_standup(ch, SHAPE_ROOM(ch)->position);
                    ch->specials.prompt_number = 4;
                    REMOVE_BIT(SHAPE_ROOM(ch)->flags, SHAPE_DIGIT_ACTIVE);
                    return;
                }
            }
            if (SHAPE_ROOM(ch)->room->dir_option[tmp]) {
                RELEASE(SHAPE_ROOM(ch)->room->dir_option[tmp]);
                SHAPE_ROOM(ch)
                    ->room->dir_option[tmp]
                    = 0;
                sprintf(str, "Exit %s removed.\n\r", exit_convert(tmp));
                SHAPE_ROOM(ch)
                    ->exit_chosen
                    = -1;
            } else {
                sprintf(str, "This exit did not exist already.\n\r");
            }
            send_to_char(str, ch);
            shape_standup(ch, SHAPE_ROOM(ch)->position);
            ch->specials.prompt_number = 4;
            REMOVE_BIT(SHAPE_ROOM(ch)->flags, SHAPE_DIGIT_ACTIVE);
            SHAPE_ROOM(ch)
                ->editflag
                = 0;
            if (IS_SET(SHAPE_ROOM(ch)->flags, SHAPE_CHAIN))
                SHAPE_ROOM(ch)
                    ->editflag
                    = room_chain[7];
            break;
        case 5:
            if (!IS_SET(SHAPE_ROOM(ch)->flags, SHAPE_DIGIT_ACTIVE)) {
                send_to_char("Enter EXIT TO EDIT:\n\r", ch);
                SHAPE_ROOM(ch)
                    ->position
                    = shape_standup(ch, POSITION_SHAPING);
                ch->specials.prompt_number = 2;
                SET_BIT(SHAPE_ROOM(ch)->flags, SHAPE_DIGIT_ACTIVE);
                return;
            } else {
                str[0] = 0;
                sscanf(arg, "%s", str);
                tmp = -1;
                if ((str[0] == 'U') || (str[0] == 'u'))
                    tmp = UP;
                if ((str[0] == 'D') || (str[0] == 'd'))
                    tmp = DOWN;
                if ((str[0] == 'N') || (str[0] == 'n'))
                    tmp = NORTH;
                if ((str[0] == 'E') || (str[0] == 'e'))
                    tmp = EAST;
                if ((str[0] == 'S') || (str[0] == 's'))
                    tmp = SOUTH;
                if ((str[0] == 'W') || (str[0] == 'w'))
                    tmp = WEST;
                if (tmp == -1) {
                    send_to_char("a letter required. dropped\n\r", ch);
                    SHAPE_ROOM(ch)
                        ->editflag
                        = 0;
                    shape_standup(ch, SHAPE_ROOM(ch)->position);
                    ch->specials.prompt_number = 4;
                    REMOVE_BIT(SHAPE_ROOM(ch)->flags, SHAPE_DIGIT_ACTIVE);
                    return;
                }
            }
            SHAPE_ROOM(ch)
                ->exit_chosen
                = tmp;

            shape_standup(ch, SHAPE_ROOM(ch)->position);
            ch->specials.prompt_number = 4;
            REMOVE_BIT(SHAPE_ROOM(ch)->flags, SHAPE_DIGIT_ACTIVE);
            SHAPE_ROOM(ch)
                ->editflag
                = 0;
            if (SHAPE_ROOM(ch)->exit_chosen >= NUM_OF_DIRS)
                SHAPE_ROOM(ch)
                    ->exit_chosen
                    = NUM_OF_DIRS - 1;
            if (SHAPE_ROOM(ch)->exit_chosen < 0)
                SHAPE_ROOM(ch)
                    ->exit_chosen
                    = 0;
            sprintf(str, "You will edit exit %s\n\r", exit_convert(SHAPE_ROOM(ch)->exit_chosen));
            send_to_char(str, ch);
            if (!mob->dir_option[SHAPE_ROOM(ch)->exit_chosen]) {
                // printf("creating exit\n");
                CREATE(mob->dir_option[SHAPE_ROOM(ch)->exit_chosen], struct room_direction_data, 1);
                CREATE(mob->dir_option[SHAPE_ROOM(ch)->exit_chosen]->general_description, char, 1);
                mob->dir_option[SHAPE_ROOM(ch)->exit_chosen]->general_description[0] = 0;
                CREATE(mob->dir_option[SHAPE_ROOM(ch)->exit_chosen]->keyword, char, 1);
                mob->dir_option[SHAPE_ROOM(ch)->exit_chosen]->keyword[0] = 0;
            }
            SHAPE_ROOM(ch)
                ->editflag
                = 0;
            if (IS_SET(SHAPE_ROOM(ch)->flags, SHAPE_CHAIN))
                SHAPE_ROOM(ch)
                    ->editflag
                    = room_chain[5];
            break;

        case 6:
            if (SHAPE_ROOM(ch)->exit_chosen < 0) {
                SHAPE_ROOM(ch)
                    ->editflag
                    = 0;
                send_to_char("No exit selected for editing.\n\r", ch);
                break;
            }
            if (!mob->dir_option[SHAPE_ROOM(ch)->exit_chosen]) {
                SHAPE_ROOM(ch)
                    ->editflag
                    = 0;
                break;
            }
            tmp = SHAPE_ROOM(ch)->room->dir_option[SHAPE_ROOM(ch)->exit_chosen]->exit_info;
            DIGITCHANGE("EXIT FLAG", tmp);
            SHAPE_ROOM(ch)
                ->room->dir_option[SHAPE_ROOM(ch)->exit_chosen]
                ->exit_info
                = tmp;

            //        DIGITCHANGE("EXIT TYPE <0 (no door), 1 (open door), 2 (hidden door), 3 (
            // unpickable door), 4 (hidden unpickable door),\n\r 10-14 the same, but no_look e
            // xit, 20-24 - door_is_heavy,\n\r 30-34 - no_look and is_heavy)",tmp);
            //        SHAPE_ROOM(ch)->room->dir_option[SHAPE_ROOM(ch)->exit_chosen]->exit_info
            // =convert_exit_flag(tmp,0);

            SHAPE_ROOM(ch)
                ->editflag
                = 0;
            if (IS_SET(SHAPE_ROOM(ch)->flags, SHAPE_CHAIN))
                SHAPE_ROOM(ch)
                    ->editflag
                    = room_chain[6];

            break;
        case 10:
            if (SHAPE_ROOM(ch)->exit_chosen < 0) {
                SHAPE_ROOM(ch)
                    ->editflag
                    = 0;
                send_to_char("No exit selected for editing.\n\r", ch);
                break;
            }
            if (!mob->dir_option[SHAPE_ROOM(ch)->exit_chosen]) {
                SHAPE_ROOM(ch)
                    ->editflag
                    = 0;
                break;
            }
            DIGITCHANGE("NUMBER OF KEY", mob->dir_option[SHAPE_ROOM(ch)->exit_chosen]->key);
            if (IS_SET(SHAPE_ROOM(ch)->flags, SHAPE_CHAIN))
                SHAPE_ROOM(ch)
                    ->editflag
                    = room_chain[10];
            break;
        case 11:
            if (SHAPE_ROOM(ch)->exit_chosen < 0) {
                send_to_char("No exit selected for editing.\n\r", ch);
                SHAPE_ROOM(ch)
                    ->editflag
                    = 0;
                break;
            }
            if (!mob->dir_option[SHAPE_ROOM(ch)->exit_chosen]) {
                SHAPE_ROOM(ch)
                    ->editflag
                    = 0;
                break;
            }
            DIGITCHANGE("ROOM FOR EXIT", mob->dir_option[SHAPE_ROOM(ch)->exit_chosen]->to_room);
            if (IS_SET(SHAPE_ROOM(ch)->flags, SHAPE_CHAIN))
                SHAPE_ROOM(ch)
                    ->editflag
                    = room_chain[11];
            break;
        case 12:
            if (SHAPE_ROOM(ch)->exit_chosen < 0) {
                send_to_char("No exit selected for editing.\n\r", ch);
                SHAPE_ROOM(ch)
                    ->editflag
                    = 0;
                break;
            }
            if (!mob->dir_option[SHAPE_ROOM(ch)->exit_chosen]) {
                SHAPE_ROOM(ch)
                    ->editflag
                    = 0;
                break;
            }
            DIGITCHANGE("EXIT WIDTH", mob->dir_option[SHAPE_ROOM(ch)->exit_chosen]->exit_width);
            if (IS_SET(SHAPE_ROOM(ch)->flags, SHAPE_CHAIN))
                SHAPE_ROOM(ch)
                    ->editflag
                    = room_chain[12];
            break;
            /*-----------extra descriptions stuff-------------------*/

        case 15:
            tmpdescr = mob->ex_description;
            CREATE(mob->ex_description, struct extra_descr_data, 1);
            CREATE(mob->ex_description->keyword, char, 1);
            mob->ex_description->keyword[0] = 0;
            CREATE(mob->ex_description->description, char, 1);
            mob->ex_description->description[0] = 0;
            mob->ex_description->next = tmpdescr;
            send_to_char("New extra description added\n\r", ch);
            SHAPE_ROOM(ch)
                ->editflag
                = 0;
            SET_BIT(SHAPE_ROOM(ch)->flags, SHAPE_CHAIN);
            if (IS_SET(SHAPE_ROOM(ch)->flags, SHAPE_CHAIN))
                SHAPE_ROOM(ch)
                    ->editflag
                    = room_chain[15];
            break;

        case 16:
            if (!mob->ex_description) {
                SHAPE_ROOM(ch)
                    ->editflag
                    = 0;
                break;
            }
            if (!mob->ex_description->next) {
                RELEASE(mob->ex_description->keyword);
                RELEASE(mob->ex_description->description);
                RELEASE(SHAPE_ROOM(ch)->room->ex_description);
                SHAPE_ROOM(ch)
                    ->room->ex_description
                    = 0;
                send_to_char("No more extra descriptions in this room\n\r", ch);
                SHAPE_ROOM(ch)
                    ->editflag
                    = 0;
                break;
            } else {
                tmpdescr = mob->ex_description->next;
                RELEASE(mob->ex_description->keyword);
                RELEASE(mob->ex_description->description);
                RELEASE(mob->ex_description);
                mob->ex_description = tmpdescr;
                send_to_char("Extra description removed.\n\r", ch);
                SHAPE_ROOM(ch)
                    ->editflag
                    = 0;
            }
            if (IS_SET(SHAPE_ROOM(ch)->flags, SHAPE_CHAIN))
                SHAPE_ROOM(ch)
                    ->editflag
                    = room_chain[16];
            break;
        case 17:
            DIGITCHANGE("ROOM LEVEL", mob->level);
            if (IS_SET(SHAPE_ROOM(ch)->flags, SHAPE_CHAIN))
                SHAPE_ROOM(ch)
                    ->editflag
                    = room_chain[4];
            break;

        case 18:
            if(!mob->affected){
                send_to_char("No room affections found.\n\r",ch);
                SHAPE_ROOM(ch)
                    ->editflag
                    = 0;
                break;
            }
            if(!IS_SET(SHAPE_ROOM(ch)->flags,SHAPE_DIGIT_ACTIVE)){
                send_to_char("Describe room affection (four numbers, without commas):\n\r",ch);
                SET_BIT(SHAPE_ROOM(ch)->flags,SHAPE_DIGIT_ACTIVE);
                SHAPE_ROOM(ch)->position=shape_standup(ch,POSITION_SHAPING);
                ch->specials.prompt_number=3;
                return;
            }
            else{
                if(4!=sscanf(arg,"%d %d %d %d",
                            &tmp,&tmp1,&tmp2,&tmp3)){
                send_to_char("four numbers required. dropped\n\r",ch);
                REMOVE_BIT(SHAPE_PROTO(ch)->flags,SHAPE_DIGIT_ACTIVE);
                shape_standup(ch,SHAPE_ROOM(ch)->position);
                ch->specials.prompt_number=4;
                SHAPE_ROOM(ch)->editflag=0;
                return;
                }
            }
            mob->affected->type = tmp;
            mob->affected->location = tmp1;
            mob->affected->duration = -1;
            mob->affected->modifier = tmp2;
            mob->affected->bitvector = tmp3;
            REMOVE_BIT(SHAPE_ROOM(ch)->flags,SHAPE_DIGIT_ACTIVE);
            shape_standup(ch,SHAPE_ROOM(ch)->position);
            ch->specials.prompt_number=4;
            SHAPE_ROOM(ch)->editflag=0;
            if(IS_SET(SHAPE_ROOM(ch)->flags,SHAPE_CHAIN)) {
                SHAPE_ROOM(ch)->editflag=room_chain[18];
            }
            break;

        case 19: /* adding new affection */
            tmpaf = get_from_affected_type_pool();
            tmpaf->next = SHAPE_ROOM(ch)->room->affected;
            SHAPE_ROOM(ch)->room->affected = tmpaf;
            send_to_char("A new affection added.\n\r",ch);

            SHAPE_ROOM(ch)->editflag=0;
            SET_BIT(SHAPE_ROOM(ch)->flags, SHAPE_CHAIN);
            if(IS_SET(SHAPE_ROOM(ch)->flags, SHAPE_CHAIN)) {
                SHAPE_ROOM(ch)->editflag=room_chain[19];
            }
            break;

        case 20: /*removing an affection */
            if(!SHAPE_ROOM(ch)->room->affected){
                send_to_char("No affections exist on this room.\n\r",ch);
            }
            else{
                tmpaf = SHAPE_ROOM(ch)->room->affected->next;
                put_to_affected_type_pool(SHAPE_ROOM(ch)->room->affected);
                SHAPE_ROOM(ch)->room->affected = tmpaf;
                send_to_char("Affection removed.\n\r",ch);
            }
            SHAPE_ROOM(ch)->editflag=0;
            SET_BIT(SHAPE_ROOM(ch)->flags, SHAPE_CHAIN);
            if(IS_SET(SHAPE_ROOM(ch)->flags, SHAPE_CHAIN)) {
                SHAPE_ROOM(ch)->editflag=room_chain[20];
            }
            break;

#undef DESCRCHANGE
#undef LINECHANGE
#undef DIGITCHANGE
        case 50:
            list_room(ch, mob);
            SHAPE_ROOM(ch)
                ->editflag
                = 0;
            break;
        default:
            list_help_room(ch);
            SHAPE_ROOM(ch)
                ->editflag
                = 0;
            break;
        }
    } while (SHAPE_ROOM(ch)->editflag);
    return;
}

void list_help_room(struct char_data* ch)
{
    send_to_char("possible fields are:\n\r1 - name;\n\r", ch);
    send_to_char("2 - description;\n\r", ch);
    send_to_char("3 - room flag;\n\r", ch);
    send_to_char("4 - sector type;\n\r", ch);
    send_to_char("       EXITS\n\r", ch);
    send_to_char("5 - select exit;\n\r6 - exit type;\n\r", ch);
    send_to_char("7 - remove exit;\n\r", ch);
    send_to_char("8 - exit keyword;\n\r", ch);
    send_to_char("9 - exit description;\n\r\n\r", ch);
    send_to_char("10 - key number;\n\r11 - room to exit to;\n\r", ch);
    send_to_char("12 - exit width;\n\r", ch);
    send_to_char("     EXTRA DESCRIPTIONS\n\r", ch);
    send_to_char("13 - extra description keyword;\n\r", ch);
    send_to_char("14 - extra description;\n\r", ch);
    send_to_char("15 - add extra description;\n\r", ch);
    send_to_char("16 - remove extra description;\n\r", ch);
    send_to_char("17 - change room level;\n\r", ch);
    send_to_char("18 - change room affection;\n\r", ch);
    send_to_char("19 - add new room affection;\n\r", ch);
    send_to_char("20 - remove the top room affection;\n\r", ch);
    send_to_char("50 - list;\n\r", ch);
    return;
}
/*********--------------------------------*********/
void list_room(struct char_data* ch, struct room_data* mob)
{
    static char str[MAX_STRING_LENGTH]; // st2[50];
    int i, i2;
    long int flg;
    sprintf(str, "(1) name         :%s\n\r", mob->name);
    send_to_char(str, ch);
    sprintf(str, "(2) description  :\n\r");
    send_to_char(str, ch);
    send_to_char(mob->description, ch);
    send_to_char("\n\r", ch);
    sprintf(str, "(3) room flag  :%ld\n\r", mob->room_flags);
    send_to_char(str, ch);
    sprintf(str, "(4) sector type   :%d\n\r", mob->sector_type);
    send_to_char(str, ch);
    sprintf(str, "Existing exits:");
    for (i = 0, i2 = 0; i < NUM_OF_DIRS; i++)
        if (mob->dir_option[i]) {
            sprintf(str + strlen(str), " %s", exit_convert(i));
            i2++;
        }
    if (i2 == 0)
        sprintf(str, "No exits are made from this room\n\r");
    else
        sprintf(str + strlen(str), "\n\r");
    send_to_char(str, ch);
    if (SHAPE_ROOM(ch)->exit_chosen < 0) {
        send_to_char("No exit selected for editing.\n\r", ch);
    } else {
        sprintf(str, "(5) exit selected  :%s\n\r", exit_convert(SHAPE_ROOM(ch)->exit_chosen));
        send_to_char(str, ch);
        flg = mob->dir_option[SHAPE_ROOM(ch)->exit_chosen]->exit_info;
        sprintf(str, "(6) exit type   :%ld\n\r", flg /*convert_exit_flag(flg,1)*/);
        send_to_char(str, ch);
        sprintf(str, "(8) exit keyword   :%s\n\r", mob->dir_option[SHAPE_ROOM(ch)->exit_chosen]->keyword);
        send_to_char(str, ch);
        sprintf(str, "(9) exit description  :\n\r");
        send_to_char(str, ch);
        send_to_char(mob->dir_option[SHAPE_ROOM(ch)->exit_chosen]->general_description, ch);
        send_to_char("\n\r", ch);

        sprintf(str, "(10) key number  :%d\n\r", mob->dir_option[SHAPE_ROOM(ch)->exit_chosen]->key);
        send_to_char(str, ch);
        sprintf(str, "(11) exit to room  :%d\n\r", mob->dir_option[SHAPE_ROOM(ch)->exit_chosen]->to_room);
        send_to_char(str, ch);
        sprintf(str, "(12) exit width  :%d\n\r", mob->dir_option[SHAPE_ROOM(ch)->exit_chosen]->exit_width);
        send_to_char(str, ch);
    }
    if (!mob->ex_description) {
        send_to_char("No extra description exists.\n\r", ch);
    } else {
        sprintf(str, "(13) extra description keyword  :%s\n\r", mob->ex_description->keyword);
        send_to_char(str, ch);
        sprintf(str, "(14) extra description  :\n\r");
        send_to_char(str, ch);
        send_to_char(mob->ex_description->description, ch);
        send_to_char("\n\r", ch);
    }

    sprintf(str, "(17) Room level: %d\n\r", mob->level);

    if (!mob->affected) {
        send_to_char("(18) No affections exist.\n\r", ch);
    } else {
        if (mob->affected->type == ROOMAFF_SPELL)
            sprintf(str, "(18) Affection type (1), (%s)-%d, level %d, flags %ld\n\r",
                ((mob->affected->location >= 0) && (mob->affected->location < MAX_SKILLS)) ? skills[mob->affected->location].name : "unknown",
                mob->affected->location,
                mob->affected->modifier,
                mob->affected->bitvector);
        send_to_char(str, ch);
    }
}
/*********--------------------------------*********/
int find_mob(FILE* f, int n);
int get_text(FILE* f, char** line);
/****************-------------------------------------****************/
/*extern struct room_data *character_list;
 */
int load_room(struct char_data* ch, char* arg)
{
    char format;
    // int i;
    int tmp, tmp2, tmp3, tmp4, number;
    char str[255], fname[80];
    FILE* f;
    struct extra_descr_data* tmpdescr;
    struct affected_type* tmpaf;

    if (2 != sscanf(arg, "%s %d\n\r", str, &number)) {
        send_to_char("Please load room by 'shape room current'\n\r", ch);
        return -1;
    }
    sprintf(fname, "%d", number / 100);
    sprintf(str, SHAPE_ROM_DIR, fname);

    send_to_char(str, ch);
    f = fopen(str, "r+");
    if (f == 0) {
        send_to_char("could not open that file.\n\r", ch);
        return -1;
    }
    strcpy(SHAPE_ROOM(ch)->f_from, str);
    SET_BIT(SHAPE_ROOM(ch)->flags, SHAPE_FILENAME);
    sprintf(SHAPE_ROOM(ch)->f_old, SHAPE_ROM_BACKDIR, fname);

    SHAPE_ROOM(ch)
        ->permission
        = get_permission(number / 100, ch);

    tmp = find_mob(f, number);
    /* fseek(f,tmp,SEEK_SET);
  fscanf(f,"%c",&c);

  fscanf(f,"%d",&tmp);
  */
    if (tmp == -1) {
        send_to_char("no room here.\n\r", ch);
        fclose(f);
        return -1;
    }
    if (tmp > number) {
        new_room(ch, number);
        sprintf(str, " could not find room #%d, created it.\n\r", number);
        send_to_char(str, ch);
    } else {
        sprintf(str, "loading room #%d\n\r", tmp);
        send_to_char(str, ch);
        number = tmp;

        /*  room_number=ch->in_room;*/
        if (!SHAPE_ROOM(ch)->room) {
            //    SHAPE_ROOM(ch)->room=(struct room_data *)calloc(1,sizeof(struct room_data));
            CREATE1(SHAPE_ROOM(ch)->room, room_data);
            // bzero((char *)(SHAPE_ROOM(ch)->room), sizeof(struct room_data));
        }
        SHAPE_ROOM(ch)
            ->room->name
            = 0;
        SHAPE_ROOM(ch)
            ->room->description
            = 0;
        SHAPE_ROOM(ch)
            ->room->ex_description
            = 0;

        SHAPE_ROOM(ch)
            ->room->number
            = number; /*    ????????     to check    */

        get_text(f, &(SHAPE_ROOM(ch)->room->name));
        if (SHAPE_ROOM(ch)->room->name[0] == '$') {
            send_to_char("Commentary found instead of room. Aborted.\n\r", ch);
            RELEASE(SHAPE_ROOM(ch)->room->name);
            RELEASE(SHAPE_ROOM(ch)->room);
            return -1;
        }
        get_text(f, &(SHAPE_ROOM(ch)->room->description));
        fgets(buf, 255, f);
        tmp = tmp2 = tmp3 = tmp4 = 0;
        sscanf(buf, "%d %d %d %d", &tmp, &tmp2, &tmp3, &tmp4); /*????? What is it for, is unclear...*/
        SHAPE_ROOM(ch)
            ->room->room_flags
            = tmp2;
        SHAPE_ROOM(ch)
            ->room->sector_type
            = tmp3;
        SHAPE_ROOM(ch)
            ->room->level
            = tmp4;
        do {
            fscanf(f, "%s ", str);
            format = str[0];
            switch (format) {
            case 'S':
                break;
            case 'E': /* extra description*/
                CREATE(tmpdescr, struct extra_descr_data, 1);
                get_text(f, &(tmpdescr->keyword));
                get_text(f, &(tmpdescr->description));
                tmpdescr->next = SHAPE_ROOM(ch)->room->ex_description;
                SHAPE_ROOM(ch)
                    ->room->ex_description
                    = tmpdescr;
                break;

            case 'F': /* room affect */
                fgets(buf, 255, f);
                sscanf(buf, "%d %d %d %d", &tmp, &tmp2, &tmp3, &tmp4);
                tmpaf = get_from_affected_type_pool();
                tmpaf->type = tmp;
                tmpaf->location = tmp2;
                tmpaf->duration = -1;
                tmpaf->modifier = tmp3;
                tmpaf->bitvector = tmp4;
                tmpaf->next = SHAPE_ROOM(ch)->room->affected;
                SHAPE_ROOM(ch)
                    ->room->affected
                    = tmpaf;
                tmpaf = 0;
                break;

            case 'D':
                tmp = atoi(str + 1);
                CREATE(SHAPE_ROOM(ch)->room->dir_option[tmp], struct room_direction_data, 1);
                get_text(f, &(SHAPE_ROOM(ch)->room->dir_option[tmp]->general_description));
                get_text(f, &(SHAPE_ROOM(ch)->room->dir_option[tmp]->keyword));
                fscanf(f, "%d", &tmp2);
                /*
      switch(tmp2){
      case 1: SHAPE_ROOM(ch)->room->dir_option[tmp]->exit_info=EX_ISDOOR|EX_CLOSED;
        break;
      case 2: SHAPE_ROOM(ch)->room->dir_option[tmp]->exit_info=EX_ISDOOR|EX_CLOSED|EX_LOCKED;
        break;
      case 3: SHAPE_ROOM(ch)->room->dir_option[tmp]->exit_info=EX_ISDOOR|EX_PICKPROOF|EX_CLOSED|EX_LOCKED;
        break;
      case 4: SHAPE_ROOM(ch)->room->dir_option[tmp]->exit_info=EX_ISDOOR|EX_ISHIDDEN|EX_CLOSED;
        break;
      case 5: SHAPE_ROOM(ch)->room->dir_option[tmp]->exit_info=EX_ISDOOR|EX_ISHIDDEN|EX_CLOSED|EX_LOCKED;
        break;
      case 6: SHAPE_ROOM(ch)->room->dir_option[tmp]->exit_info=EX_ISDOOR|EX_PICKPROOF|EX_ISHIDDEN|EX_CLOSED|EX_LOCKED;
        break;
      default: SHAPE_ROOM(ch)->room->dir_option[tmp]->exit_info=0;
      }
      */
                SHAPE_ROOM(ch)
                    ->room->dir_option[tmp]
                    ->exit_info
                    = tmp2;
                fscanf(f, "%d", &tmp2);
                SHAPE_ROOM(ch)
                    ->room->dir_option[tmp]
                    ->key
                    = tmp2;
                fscanf(f, "%d", &tmp2);
                SHAPE_ROOM(ch)
                    ->room->dir_option[tmp]
                    ->to_room
                    = tmp2;
                fscanf(f, "%d", &tmp2);
                SHAPE_ROOM(ch)
                    ->room->dir_option[tmp]
                    ->exit_width
                    = tmp2;
                break;
            }
        } while (format != 'S');
        SHAPE_ROOM(ch)
            ->room->funct
            = 0;
        SHAPE_ROOM(ch)
            ->room->contents
            = 0;
        SHAPE_ROOM(ch)
            ->room->people
            = 0;
        SHAPE_ROOM(ch)
            ->room->light
            = 0;
    }
    SET_BIT(SHAPE_ROOM(ch)->flags, SHAPE_ROOM_LOADED);
    ch->specials.prompt_value = number;
    SHAPE_ROOM(ch)
        ->exit_chosen
        = -1;
    fclose(f);
    return number;
}
/*****************----------------------------------******************/
int append_room(struct char_data* ch, char* arg);
int create_room(struct char_data* ch, char* arg)
{
    int number = 0, tmp; // tmp2,room_number;
    char str[255], fname[80];
    FILE* f;
    //  struct extra_descr_data * tmpdescr;
    if (2 != sscanf(arg, "%s %s\n\r", str, fname)) {
        send_to_char("format: create <zone number>\n\r", ch);
        REMOVE_BIT(SHAPE_ROOM(ch)->flags, SHAPE_ROOM_LOADED);
        return -1;
    }

    tmp = atoi(fname);
    if ((tmp <= 0) || (tmp > MAX_ZONES)) {
        send_to_char("Weird zone number. Aborted.\n\r", ch);
        REMOVE_BIT(SHAPE_ROOM(ch)->flags, SHAPE_ROOM_LOADED);
        return -1;
    }

    tmp = get_permission(tmp, ch);
    if (!tmp) {
        send_to_char("You may not create room here. Aborted.\n\r", ch);
        REMOVE_BIT(SHAPE_ROOM(ch)->flags, SHAPE_ROOM_LOADED);
        return -1;
    }

    sprintf(str, SHAPE_ROM_DIR, fname);

    //  send_to_char(str,ch);
    f = fopen(str, "r+");
    if (f == 0) {
        send_to_char("could not open that file.\n\r", ch);
        REMOVE_BIT(SHAPE_ROOM(ch)->flags, SHAPE_ROOM_LOADED);
        return -1;
    }
    strcpy(SHAPE_ROOM(ch)->f_from, str);
    SET_BIT(SHAPE_ROOM(ch)->flags, SHAPE_FILENAME);
    sprintf(SHAPE_ROOM(ch)->f_old, SHAPE_ROM_BACKDIR, fname);

    tmp = find_mob(f, 99999);
    if (tmp == -1) {
        send_to_char("corrupted zone file? dropped.\n\r", ch);
        fclose(f);
        return -1;
    }

    sprintf(str, "creating room at zone %s\n\r", fname);
    send_to_char(str, ch);
    new_room(ch, number);
    SET_BIT(SHAPE_ROOM(ch)->flags, SHAPE_ROOM_LOADED);
    SHAPE_ROOM(ch)
        ->exit_chosen
        = -1;
    fclose(f);
    SHAPE_ROOM(ch)
        ->room->number
        = append_room(ch, arg);
    sprintf(str, "causing the eternal order to shiver, you created a room #%d\n\r",
        SHAPE_ROOM(ch)->room->number);
    send_to_char(str, ch);
    return number;
}
/*****************----------------------------------******************/
// #define min(a,b) (((a)<(b))? (a):(b))
int replace_room(struct char_data* ch, char* arg)
{
    /* copy f1 to f2, replacing mob #num with new mob */
    char str[255];
    char *f_from, *f_old;
    char c;
    int i, check, num, oldnum;
    FILE* f1;
    FILE* f2;
    /*  if(3!=sscanf(arg,"%s %s %s",str,f_from,f_old)){
    send_to_char("format is <file_from> <file_to>\n\r",ch);
    return -1;
  }
*/
    if (!IS_SET(SHAPE_ROOM(ch)->flags, SHAPE_FILENAME)) {
        send_to_char("How strange... you have no file defined to write to.\n\r",
            ch);
        return -1;
    }
    f_from = SHAPE_ROOM(ch)->f_from;
    f_old = SHAPE_ROOM(ch)->f_old;

    if (!IS_SET(SHAPE_ROOM(ch)->flags, SHAPE_ROOM_LOADED)) {
        send_to_char("you have no mobile to save...\n\r", ch);
        return -1;
    }
    if (SHAPE_ROOM(ch)->permission == 0) {
        send_to_char("You may not do that in this zone.\n\r", ch);
        return -1;
    }
    if (!strcmp(f_from, f_old)) {
        send_to_char("better make source and target files different\n\r", ch);
        return -1;
    }
    num = SHAPE_ROOM(ch)->room->number;
    if (num == -1) {
        send_to_char("you created it afresh, remember? just add it\n\r", ch);
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
        check = fscanf(f1, "%c", &c);
        fprintf(f2, "%c", c);
    } while (check != EOF);
    fclose(f1);
    fclose(f2);

    f2 = fopen(f_from, "w");
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
            check = fscanf(f1, "%c", &c);
            if (c != '#')
                fprintf(f2, "%c", c);
        } while ((c != '#') && (check != EOF));
        fscanf(f1, "%d", &i);
        if (i < num)
            fprintf(f2, "#%-d", i);
        else
            oldnum = i;
    } while ((i < num) && (check != EOF));

    if (check == EOF) {
        sprintf(str, "no room #%d in this file\n\r", num);
        send_to_char(str, ch);
        fclose(f1);
        fclose(f2);
        return -1;
    }
    if (i == num) {
        do {
            i = fscanf(f1, "%c", &c);
        } while ((c != '#') && (i != EOF));
        if (c == '#')
            fscanf(f1, "%d", &oldnum);
    }
    if (!IS_SET(SHAPE_ROOM(ch)->flags, SHAPE_DELETE_ACTIVE)) {
        write_room(f2, SHAPE_ROOM(ch)->room, num);
        sprintf(str, "Saved as room #%d\n\r", num);
        send_to_char(str, ch);
        REMOVE_BIT(SHAPE_ROOM(ch)->flags, SHAPE_DELETE_ACTIVE);
    } else {
        sprintf(str, "Deleted room #%d\n\r", num);
        send_to_char(str, ch);
    }
    fprintf(f2, "#%-d", oldnum);
    for (; i != EOF;) {
        i = fscanf(f1, "%c", &c);
        if (i != EOF)
            fprintf(f2, "%c", c);
    }
    fclose(f1);
    fclose(f2);
    return num;
}
int append_room(struct char_data* ch, char* arg)
{
    /* copy f1 to f2, appending mob #next-in-file with new mob */
    char str[255], fname[80];
    char* f_from;
    char* f_old;
    char c;
    int i, i1, check;
    FILE* f1;
    FILE* f2;
    /*  if(3!=sscanf(arg,"%s %s %s",str,f_from,f_old)){
    send_to_char("format is <file_from> <file_to>\n\r",ch);
    return -1;
  }*/
    if (!IS_SET(SHAPE_ROOM(ch)->flags, SHAPE_FILENAME)) {
        if (2 != sscanf("%s %s", str, fname)) {
            if (!IS_SET(SHAPE_ROOM(ch)->flags, SHAPE_FILENAME)) {
                send_to_char("No file defined to write into. Use 'add <filename>\n\r'",
                    ch);
                return -1;
            }
        } else {
            sprintf(SHAPE_ROOM(ch)->f_from, SHAPE_ROM_DIR, fname);
            sprintf(SHAPE_ROOM(ch)->f_old, SHAPE_ROM_BACKDIR, fname);
            SET_BIT(SHAPE_ROOM(ch)->flags, SHAPE_FILENAME);
        }
    }
    if (SHAPE_ROOM(ch)->permission == 0) {
        send_to_char("You may not do that in this zone.\n\r", ch);
        return -1;
    }

    sprintf(str, "Adding a new room to zone file %s.\n\r", SHAPE_ROOM(ch)->f_from);
    send_to_char(str, ch);
    if (!IS_SET(SHAPE_ROOM(ch)->flags, SHAPE_ROOM_LOADED)) {
        send_to_char("you have no room to add...\n\r", ch);
        return -1;
    }

    f_from = SHAPE_ROOM(ch)->f_from;
    f_old = SHAPE_ROOM(ch)->f_old;

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
        check = fscanf(f1, "%c", &c);
        fprintf(f2, "%c", c);
    } while (check != EOF);
    fclose(f1);
    fclose(f2);

    if (!strcmp(f_from, f_old)) {
        send_to_char("better make source and target files different\n\r", ch);
        return -1;
    }

    f1 = fopen(f_old, "r+");
    if (!f1) {
        send_to_char("could not open backup file\n\r", ch);
        return -1;
    }
    f2 = fopen(f_from, "w+");
    if (!f2) {
        send_to_char("could not open target file\n\r", ch);
        fclose(f1);
        return -1;
    }

    do {

        do {
            check = fscanf(f1, "%c", &c);
            fprintf(f2, "%c", c);
        } while ((c != '#') && (check != EOF));
        i1 = i;
        fscanf(f1, "%d", &i);
        if (i != 99999)
            fprintf(f2, "%-d", i);
    } while ((i != 99999) && (check != EOF));

    if (check == EOF) {
        send_to_char("no final record  in source file\n\r", ch);
    }

    fseek(f2, -1, SEEK_CUR);
    write_room(f2, SHAPE_ROOM(ch)->room, i1 + 1);
    SHAPE_ROOM(ch)
        ->room->number
        = i1 + 1;
    sprintf(str, "The room is added to database. New number is #%d\n\r", i1 + 1);
    send_to_char(str, ch);
    ch->specials.prompt_value = i1 + 1;

    fprintf(f2, "#99999\n\r$~\n\r");
    fclose(f1);
    fclose(f2);
    return i1 + 1;
}
// #define RELEASE(x) if(x) RELEASE(x)
void free_room(struct char_data* ch)
{
    int i;
    struct extra_descr_data *tmpdescr, *tmpdescr2;
    struct affected_type *tmpaf, *tmpaf2;

    if (!SHAPE_ROOM(ch))
        return;
    if (IS_SET(SHAPE_ROOM(ch)->flags, SHAPE_ROOM_LOADED)) {
        for (i = 0; i < NUM_OF_DIRS; i++)
            RELEASE(SHAPE_ROOM(ch)->room->dir_option[i]);

        tmpdescr = SHAPE_ROOM(ch)->room->ex_description;
        while (tmpdescr) {
            tmpdescr2 = tmpdescr->next;
            RELEASE(tmpdescr);
            tmpdescr = tmpdescr2;
        }

        tmpaf = SHAPE_ROOM(ch)->room->affected;
        while (tmpaf) {
            tmpaf2 = tmpaf->next;
            put_to_affected_type_pool(tmpaf);
            tmpaf = tmpaf2;
        }

        SHAPE_ROOM(ch)
            ->room->ex_description
            = 0;
        RELEASE(SHAPE_ROOM(ch)->room->name);
        RELEASE(SHAPE_ROOM(ch)->room->description);
        REMOVE_BIT(SHAPE_ROOM(ch)->flags, SHAPE_ROOM_LOADED);
        RELEASE(SHAPE_ROOM(ch)->room);
        SHAPE_ROOM(ch)
            ->room
            = 0;
        ch->specials.prompt_value = 0;
    }
    //    shape_standup(ch,SHAPE_ROOM(ch)->position);
    REMOVE_BIT(SHAPE_ROOM(ch)->flags, SHAPE_ROOM_LOADED);
    // RELEASE(SHAPE_ROOM(ch));
    // SHAPE_ROOM(ch)=0;
    RELEASE(ch->temp);
    ch->temp = 0;
    REMOVE_BIT(PRF_FLAGS(ch), PRF_DISPTEXT);
    if (GET_POS(ch) <= POSITION_SHAPING)
        GET_POS(ch) = POSITION_STANDING;
}
/****************************** main() ********************************/
void extra_coms_room(struct char_data* ch, char* argument)
{

    /*  extern struct room_data *character_list;*/
    //  extern struct room_data world;
    int comm_key, room_number;
    char str[1000];

    room_number = ch->in_room;
    /*  printf("shape center: flags=%d\n\r",SHAPE_ROOM(ch)->flags);
     */
    if (SHAPE_ROOM(ch)->procedure == SHAPE_EDIT) {

        send_to_char("you invoked some rhymes from shapeless indefinity...\n\r", ch);
        comm_key = SHAPE_NONE;
        str[0] = 0;
        sscanf(argument, "%s", str);
        if (str[0] == 0)
            return;
        do {
            if (!strlen(str))
                strcpy(str, "weird");

            if (!strncmp(str, "create", strlen(str))) {
                comm_key = SHAPE_CREATE;
                break;
            }
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
            if (!strncmp(str, "implement", strlen(str))) {
                comm_key = SHAPE_IMPLEMENT;
                break;
            }
            send_to_char("Possible commands are:\n\r", ch);
            //      send_to_char("create - to build a new room ;\n\r",ch);
            //      send_to_char("load   <room #>;\n\r",ch);
            //      send_to_char("add    <zone #>;\n\r",ch);
            send_to_char("save  - to save changes to the disk database;\n\r", ch);
            send_to_char("delete - to remove the loaded room from the disk database;\n\r", ch);
            send_to_char("implement - applies changes to the game, leaving disk intact;\n\r", ch);
            send_to_char("done - to save your job, implement it and stop shaping.;\n\r", ch);
            send_to_char("free - to stop shaping.;\n\r", ch);

            return;
        } while (0);
        /*    SHAPE_ROOM(ch)->procedure=comm_key;*/
    } else
        comm_key = SHAPE_ROOM(ch)->procedure;
    switch (comm_key) {
    case SHAPE_FREE:
        free_room(ch);
        send_to_char("You released the room and stopped shaping.\n\r", ch);
        break;
    case SHAPE_CREATE:
        if (create_room(ch, argument) < 0)
            free_room(ch);
        break;

    case SHAPE_LOAD:
        if (!IS_SET(SHAPE_ROOM(ch)->flags, SHAPE_ROOM_LOADED)) {
            if (load_room(ch, argument) < 0) {
                free_room(ch);
                break;
            }
        } else
            send_to_char("you already have some place to care about\n\r", ch);
        break;
    case SHAPE_SAVE:
        if (IS_SET(SHAPE_ROOM(ch)->flags, SHAPE_ROOM_LOADED))
            replace_room(ch, argument);
        else
            send_to_char("You have nothing to save.\n\r", ch);
        break;
    case SHAPE_ADD:
        if (IS_SET(SHAPE_ROOM(ch)->flags, SHAPE_ROOM_LOADED))
            append_room(ch, argument);
        else
            send_to_char("You have nothing to add.\n\r", ch);
        break;
    case SHAPE_DELETE:
        if (SHAPE_ROOM(ch)->procedure != SHAPE_DELETE) {
            send_to_char("You are about to remove this room from database.\n\r Are you sure? (type 'yes' to confirm:\n\r", ch);
            SHAPE_ROOM(ch)
                ->procedure
                = SHAPE_DELETE;
            SHAPE_ROOM(ch)
                ->position
                = shape_standup(ch, POSITION_SHAPING);
            break;
        }
        while (*argument && (*argument <= ' '))
            argument++;
        if (!strcmp("yes", argument)) {
            SET_BIT(SHAPE_ROOM(ch)->flags, SHAPE_DELETE_ACTIVE);
            replace_room(ch, argument);
            send_to_char("You still continue to shape it, though - take your chance.\n\r", ch);
        } else
            send_to_char("Deletion cancelled.\n\r", ch);

        REMOVE_BIT(SHAPE_ROOM(ch)->flags, SHAPE_DELETE_ACTIVE);
        SHAPE_ROOM(ch)
            ->procedure
            = SHAPE_EDIT;
        shape_standup(ch, SHAPE_ROOM(ch)->position);
        break;
    case SHAPE_IMPLEMENT:
        implement_room(ch);
        SHAPE_ROOM(ch)
            ->procedure
            = SHAPE_EDIT;
        break;
    case SHAPE_DONE:
        if (IS_SET(SHAPE_ROOM(ch)->flags, SHAPE_ROOM_LOADED)) {
            replace_room(ch, argument);
            implement_room(ch);
        } else
            send_to_char("You have nothing to save.\n\r", ch);
        extra_coms_room(ch, "free");
        break;
    }
    return;
}
