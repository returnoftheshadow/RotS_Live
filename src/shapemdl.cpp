/*
 * src/shapemdl.cc
 *
 * Provide the in-game interface to ASIMA mudlle shaping
 */

#include "platdef.h"
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

extern struct room_data world;
extern char** mobile_program;
extern int* mobile_program_zone;
extern int num_of_programs;

char* mudlle_converter(char* source);

int load_mudlle(char_data* ch, char* arg)
{
    FILE* fp;
    int number, num, isnew;
    char str[MAX_STRING_LENGTH];
    char* tmpstr;
    char fname[100];

    if (!SHAPE_MUDLLE(ch)) {
        send_to_char("You are not shaping anything.\n\r", ch);
        return -1;
    }
    if (SHAPE_MUDLLE(ch)->prog_num > 0) {
        send_to_char("You are already shaping a program.\n\r", ch);
        return -1;
    }

    while (*arg && *arg <= ' ')
        arg++;

    number = 0;
    number = atoi(arg);
    if (!number) {
        send_to_char("Usage: /load [number].\n\r", ch);
        return -1;
    }
    sprintf(fname, "%d", number / 100);
    sprintf(str, SHAPE_MDL_DIR, fname);

    if (!(fp = fopen(str, "rb"))) {
        send_to_char("Could not open programs file.\n\r", ch);
        return -1;
    }
    strcpy(SHAPE_MUDLLE(ch)->f_from, str);
    sprintf(SHAPE_MUDLLE(ch)->f_old, SHAPE_MDL_BACKDIR, fname);

    num = 0;
    do {
        if (!fgets(str, 1000, fp)) {
            send_to_char("Wrong program file format.\n\r", ch);
            return -1;
        }
        tmpstr = str;
        while (*tmpstr && (*tmpstr < ' '))
            tmpstr++;
        if (*tmpstr == '#') {
            num = atoi(str + 1);
            if ((num == number) || (num == 99999))
                break;
        }
    } while (1);

    if (num == 99999) {
        CREATE(SHAPE_MUDLLE(ch)->txt, char, 1);
        send_to_char("A new program.\n\r", ch);
        SHAPE_MUDLLE(ch)
            ->real_num
            = 0;
        isnew = 1;
    } else {
        isnew = 0;
        bzero(str, MAX_STRING_LENGTH);
        do {
            tmpstr = str + strlen(str);
            fgets(tmpstr, MAX_STRING_LENGTH - strlen(str), fp);
            while (*tmpstr && (*tmpstr < ' '))
                tmpstr++;
        } while (*tmpstr != '#');
        *tmpstr = 0;

        CREATE(SHAPE_MUDLLE(ch)->txt, char, strlen(str) + 1);
        strcpy(SHAPE_MUDLLE(ch)->txt, str);

        SHAPE_MUDLLE(ch)
            ->real_num
            = real_program(number);

        if (!isnew) {
            sprintf(str, "You uploaded the special program #%d.\n\r", number);
            send_to_char(str, ch);
        } else {
            sprintf(str, "Could not find a program #%d, created it.\n\r", number);
            send_to_char(str, ch);
        }
    }

    SHAPE_MUDLLE(ch)
        ->permission
        = get_permission(number / 100, ch);
    if (!SHAPE_MUDLLE(ch)->permission)
        send_to_char("You have only limited permission for this zone.\n\r", ch);

    SHAPE_MUDLLE(ch)
        ->prog_num
        = num;
    ch->specials.prompt_value = num;

    return num;
}

int save_mudlle(struct char_data* ch)
{
    FILE *fp, *ofp;
    int tmp, num, saved;
    char str[MAX_STRING_LENGTH];

    if (!SHAPE_MUDLLE(ch)) {
        send_to_char("You are not shaping anything.\n\r", ch);
        return -1;
    }
    if (SHAPE_MUDLLE(ch)->prog_num < 0) {
        send_to_char("You have nothing to save.\n\r", ch);
        return -1;
    }
    if (!SHAPE_MUDLLE(ch)->permission) {
        send_to_char("You have no access to this zone, may not save.\n\r", ch);
        return -1;
    }
    str[0] = 0;
    sprintf(str, "%s %s %s", COPY_COMMAND,
        SHAPE_MUDLLE(ch)->f_from, SHAPE_MUDLLE(ch)->f_old);
    fputs(str, stderr);
    system(str);
    fp = fopen(SHAPE_MUDLLE(ch)->f_from, "wb+");
    ofp = fopen(SHAPE_MUDLLE(ch)->f_old, "rb");

    num = -1;
    saved = 0;
    /* fgets as the loop test: a feof() test ran one extra pass at the end
     * and wrote the last line again on every save. */
    while (fgets(str, MAX_STRING_LENGTH, ofp)) {
        fputs(str, stderr);
        for (tmp = 0; (tmp < MAX_STRING_LENGTH) && (str[tmp] <= ' '); tmp++)
            ;

        if (str[tmp] == '#') {
            num = atoi(str + tmp + 1);
            if ((num >= SHAPE_MUDLLE(ch)->prog_num) && !saved) {
                clean_text(SHAPE_MUDLLE(ch)->txt);
                sprintf(str, "#%-d\n", SHAPE_MUDLLE(ch)->prog_num);
                fwrite(str, 1, strlen(str), fp);
                fwrite(SHAPE_MUDLLE(ch)->txt, 1, strlen(SHAPE_MUDLLE(ch)->txt), fp);
                saved = 1;
            }
        }
        if (num != SHAPE_MUDLLE(ch)->prog_num)
            fwrite(str, 1, strlen(str), fp);
    }
    if (saved)
        send_to_char("Saved succesfully.\n\r", ch);
    else
        send_to_char("Could not save. Sorry.\n\r", ch);

    fclose(fp);
    fclose(ofp);
    return SHAPE_MUDLLE(ch)->prog_num;
}

void implement_mudlle(struct char_data* ch)
{
    if (!SHAPE_MUDLLE(ch)) {
        send_to_char("You are not shaping anything.\n\r", ch);
        return;
    }
    if (SHAPE_MUDLLE(ch)->prog_num < 0) {
        send_to_char("You have nothing to implement.\n\r", ch);
        return;
    }
    if (!SHAPE_MUDLLE(ch)->permission) {
        send_to_char("You have no access to this zone, may not implement.\n\r", ch);
        return;
    }
    if (SHAPE_MUDLLE(ch)->real_num < 0) {
        send_to_char("This is a new program; implementation requires a reboot.\n\r", ch);
        return;
    }
    RELEASE(mobile_program[SHAPE_MUDLLE(ch)->real_num]);
    mobile_program[SHAPE_MUDLLE(ch)->real_num] = mudlle_converter(SHAPE_MUDLLE(ch)->txt);

    send_to_char("Program implemented.\n\r", ch);
}

void show_mudlle(struct char_data* ch)
{
    char str[500];

    if (!SHAPE_MUDLLE(ch))
        return;
    if ((!SHAPE_MUDLLE(ch)->txt) || (!SHAPE_MUDLLE(ch)->prog_num < 0)) {
        send_to_char("You have no program to shape.\n\r", ch);
        return;
    }

    sprintf(str, "Program #%d (real #%d).\n\rProgram text:\n\r",
        SHAPE_MUDLLE(ch)->prog_num, SHAPE_MUDLLE(ch)->real_num);
    send_to_char(str, ch);
    send_to_char(SHAPE_MUDLLE(ch)->txt, ch);
    send_to_char("\n\r", ch);

    return;
}

void free_mudlle(struct char_data* ch)
{
    RELEASE(SHAPE_MUDLLE(ch)->txt);
    RELEASE(ch->temp);
    ch->specials.prompt_value = -1;
    ch->specials.prompt_number = 0;
    REMOVE_BIT(PRF_FLAGS(ch), PRF_DISPTEXT);
    return;
}

void edit_mudlle(struct char_data* ch)
{
    if (!ch->desc)
        return;
    string_add_init(ch->desc, &(SHAPE_MUDLLE(ch)->txt));
    return;
}

void shape_center_mudlle(struct char_data* ch, char* argument)
{
    char str[255], str2[255];
    int len;

    str[0] = str2[0] = 0;
    if (!sscanf(argument, "%s %s", str, str2))
        return;
    len = strlen(str);

    if (!strncmp(str, "load", len)) {
        load_mudlle(ch, str2);
        return;
    } else if (!strncmp(str, "show", len)) {
        show_mudlle(ch);
        return;
    } else if (!strncmp(str, "free", len)) {
        free_mudlle(ch);
        return;
    } else if (!strncmp(str, "save", len)) {
        save_mudlle(ch);
        return;
    } else if (!strncmp(str, "implement", len)) {
        implement_mudlle(ch);
        return;
    } else if (!strncmp(str, "edit", len)) {
        edit_mudlle(ch);
        return;
    } else if (!strncmp(str, "done", len)) {
        implement_mudlle(ch);
        save_mudlle(ch);
        free_mudlle(ch);
        return;
    }
}
