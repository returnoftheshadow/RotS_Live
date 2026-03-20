/* ************************************************************************
 *   File: ban.c                                         Part of CircleMUD *
 *  Usage: banning/unbanning/checking sites and player names               *
 *                                                                         *
 *  All rights reserved.  See license.doc for complete information.        *
 *                                                                         *
 *  Copyright (C) 1993 by the Trustees of the Johns Hopkins University     *
 *  CircleMUD is based on DikuMUD, Copyright (C) 1990, 1991.               *
 ************************************************************************ */

#include "platdef.h"
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "comm.h"
#include "db.h"
#include "handler.h"
#include "interpre.h"
#include "structs.h"
#include "utils.h"

struct ban_list_element* ban_list = 0;
extern char* ban_types[];

void load_banned(void)
{
    FILE* fl;
    int i, date;
    char site_name[BANNED_SITE_LENGTH + 1], ban_type[100];
    char name[MAX_NAME_LENGTH + 1];
    struct ban_list_element* next_node;

    ban_list = 0;

    if (!(fl = fopen(BAN_FILE, "r"))) {
        perror("Unable to open banfile");
        return;
    }

    while (fscanf(fl, " %s %s %d %s ", ban_type, site_name, &date, name) == 4) {
        CREATE(next_node, struct ban_list_element, 1);
        strncpy(next_node->site, site_name, BANNED_SITE_LENGTH);
        next_node->site[BANNED_SITE_LENGTH] = '\0';
        strncpy(next_node->name, name, MAX_NAME_LENGTH);
        next_node->name[MAX_NAME_LENGTH] = '\0';
        next_node->date = date;

        for (i = BAN_NOT; i <= BAN_ALL; i++)
            if (!strcmp(ban_type, ban_types[i]))
                next_node->type = i;

        next_node->next = ban_list;
        ban_list = next_node;
    }

    fclose(fl);
}

int isbanned(char* hostname)
{
    int i;
    struct ban_list_element* banned_node;
    char* nextchar;

    if (!hostname || !*hostname)
        return (0);

    i = 0;
    for (nextchar = hostname; *nextchar; nextchar++)
        *nextchar = tolower(*nextchar);

    for (banned_node = ban_list; banned_node; banned_node = banned_node->next)
        if (strstr(hostname, banned_node->site)) /* if hostname is a substring */
            i = MAX(i, banned_node->type);

    return i;
}

void _write_one_node(FILE* fp, struct ban_list_element* node)
{
    if (node) {
        _write_one_node(fp, node->next);
        fprintf(fp, "%s %s %ld %s\n", ban_types[node->type],
            node->site, node->date, node->name);
    }
}

void write_ban_list(void)
{
    FILE* fl;

    if (!(fl = fopen(BAN_FILE, "w"))) {
        perror("write_ban_list");
        return;
    }

    _write_one_node(fl, ban_list); /* recursively write from end to start */
    fclose(fl);
    return;
}

ACMD(do_ban)
{
    char flag[80], site[80], format[50], *nextchar, *timestr;
    int i;
    struct ban_list_element* ban_node;

    if (IS_NPC(ch)) {
        send_to_char("You Beast!!\n\r", ch);
        return;
    }

    strcpy(buf, "");
    if (!*argument) {
        if (!ban_list) {
            send_to_char("No sites are banned.\n\r", ch);
            return;
        }
        strcpy(format, "%-39.39s  %-8.8s  %-10.10s  %-16.16s\n\r");
        sprintf(buf, format,
            "Banned Site Name",
            "Ban Type",
            "Banned On",
            "Banned By");
        send_to_char(buf, ch);
        sprintf(buf, format,
            "----------------------------------------",
            "----------------------------------------",
            "----------------------------------------",
            "----------------------------------------");
        send_to_char(buf, ch);

        for (ban_node = ban_list; ban_node; ban_node = ban_node->next) {
            if (ban_node->date) {
                timestr = asctime(localtime(&(ban_node->date)));
                *(timestr + 10) = 0;
                strcpy(site, timestr);
            } else
                strcpy(site, "Unknown");
            sprintf(buf, format, ban_node->site, ban_types[ban_node->type], site,
                ban_node->name);
            send_to_char(buf, ch);
        }
        return;
    }

    half_chop(argument, flag, site);
    if (!*site || !*flag) {
        send_to_char("Usage: ban {all | select | new} site_name\n\r", ch);
        return;
    }

    if (!(!str_cmp(flag, "select") || !str_cmp(flag, "all") || !str_cmp(flag, "new"))) {
        send_to_char("Flag must be ALL, SELECT, or NEW.\n\r", ch);
        return;
    }

    for (ban_node = ban_list; ban_node; ban_node = ban_node->next) {
        if (!str_cmp(ban_node->site, site)) {
            send_to_char(
                "That site has already been banned -- unban it to change the ban type.\n\r", ch);
            return;
        }
    }

    CREATE(ban_node, struct ban_list_element, 1);
    strncpy(ban_node->site, site, BANNED_SITE_LENGTH);
    for (nextchar = ban_node->site; *nextchar; nextchar++)
        *nextchar = tolower(*nextchar);
    ban_node->site[BANNED_SITE_LENGTH] = '\0';
    strncpy(ban_node->name, GET_NAME(ch), MAX_NAME_LENGTH);
    ban_node->name[MAX_NAME_LENGTH] = '\0';
    ban_node->date = time(0);

    for (i = BAN_NEW; i <= BAN_ALL; i++)
        if (!str_cmp(flag, ban_types[i]))
            ban_node->type = i;

    ban_node->next = ban_list;
    ban_list = ban_node;

    sprintf(buf, "%s has banned %s for %s players.", GET_NAME(ch), site,
        ban_types[ban_node->type]);
    mudlog(buf, NRM, (sh_int)MAX(LEVEL_GOD, GET_INVIS_LEV(ch)), TRUE);
    send_to_char("Site banned.\n\r", ch);
    write_ban_list();
}

ACMD(do_unban)
{
    char site[80];
    struct ban_list_element *ban_node, *prev_node;
    int found = 0;

    if (IS_NPC(ch)) {
        send_to_char("You are not godly enough for that!\n\r", ch);
    }

    one_argument(argument, site);
    if (!*site) {
        send_to_char("A site to unban might help.\n\r", ch);
        return;
    }

    ban_node = ban_list;
    while (ban_node && !found) {
        if (!str_cmp(ban_node->site, site))
            found = 1;
        else
            ban_node = ban_node->next;
    }

    if (!found) {
        send_to_char("That site is not currently banned.\n\r", ch);
        return;
    }

    /* first element in list */
    if (ban_node == ban_list)
        ban_list = ban_list->next;
    else {
        for (prev_node = ban_list; prev_node->next != ban_node;
             prev_node = prev_node->next)
            ;

        prev_node->next = ban_node->next;
    }

    send_to_char("Site unbanned.\n\r", ch);
    sprintf(buf, "%s removed the %s-player ban on %s.",
        GET_NAME(ch), ban_types[ban_node->type], ban_node->site);
    mudlog(buf, NRM, (sh_int)MAX(LEVEL_GOD, GET_INVIS_LEV(ch)), TRUE);

    RELEASE(ban_node);
    write_ban_list();
}

/**************************************************************************
 *  Code to check for invalid names (i.e., profanity, etc.)		  *
 *  Written by Sharon P. Goza						  *
 **************************************************************************/

char** invalid_list = NULL;
int num_invalid = 0;

extern struct char_data* mob_proto;
extern struct obj_data* obj_proto;
extern int top_of_mobt;
extern int top_of_objt;

void read_invalid_list(void);
void clear_invalid_list(void);

int valid_name(char* newname)
{
    int i;
    char* cptr;
    char tempname[MAX_NAME_LENGTH];

    /*
     * Read in the list.  This should be done every time so that we don't have
     * to reboot to add someone to the list.  -Alkar
     */
    read_invalid_list();

    /* IF THE LIST COULDN'T BE READ IN FOR A REASON, RETURN VALID */
    if (!invalid_list)
        return 1;

    /*
     * If the length of the name is below the minimum length, return invalid.
     * special check for "all" so that people can still be called 'Alleth', etc
     */
    if (strlen(newname) < MIN_NAME_LENGTH || strlen(newname) > MAX_NAME_LENGTH) {
        clear_invalid_list();
        return 0;
    }

    if (str_cmp(newname, "all") == 0) {
        sprintf(buf, "Invalid name '%s' (matched '%s')",
            newname, "all");
        mudlog(buf, NRM, LEVEL_GOD, TRUE);
        clear_invalid_list();
        return 0;
    }

    /* CHANGE THE INPUT STRING TO BE LOWERCASE SO WE ONLY HAVE TO COMPARE ONCE */
    for (i = 0, cptr = newname; *cptr && i < MAX_NAME_LENGTH;)
        tempname[i++] = tolower(*cptr++);
    tempname[i] = 0;

    /*
     * Loop through the invalid list and see if the string occurs in
     * the desired name
     */
    for (i = 0; i < num_invalid; i++)
        /* If invalid_list[i] is a null string, don't compare it */
        if (*invalid_list[i])
            if (strstr(tempname, invalid_list[i])) {
                sprintf(buf, "Invalid name '%s' (matched '%s')",
                    newname, invalid_list[i]);
                mudlog(buf, NRM, LEVEL_GOD, TRUE);
                clear_invalid_list();
                return 0;
            }

    for (i = 0; i < top_of_mobt; i++)
        if (isname(newname, mob_proto[i].player.name))
            break;
    if (i < top_of_mobt) {
        clear_invalid_list();
        return 0;
    }

    for (i = 0; i < top_of_objt; i++)
        if (isname(newname, obj_proto[i].name))
            break;
    if (i < top_of_objt) {
        clear_invalid_list();
        return 0;
    }

    clear_invalid_list();
    return 1;
}

void read_invalid_list(void)
{
    FILE* fp;
    int i = 0;
    char dummy[80];

    if (!(fp = fopen(XNAME_FILE, "r"))) {
        perror("Unable to open invalid name file");
        return;
    }

    /* count how many records */
    for (num_invalid = 0; !feof(fp); ++num_invalid)
        fscanf(fp, "%s", dummy);

    /* Alright, back to the beginning of the file */
    errno = 0;
    rewind(fp);
    if (errno) {
        perror("rewind");
        return;
    }

    /* Create the list and fill each spot */
    CREATE(invalid_list, char*, num_invalid);
    for (i = 0; !feof(fp); ++i) {
        CREATE(invalid_list[i], char, MAX_NAME_LENGTH);

        /*
         * When fscanf fails during conversion, the contents
         * of invalid_list[i] will be undefined.  So, we make
         * the string NULL terminated so that later uses of
         * the string won't crash and burn.
         */
        if (fscanf(fp, "%s", invalid_list[i]) == EOF) {
            *invalid_list[i] = 0;
            break;
        }
    }

    fclose(fp);
}

void clear_invalid_list(void)
{
    int i;

    /* no list to clear? */
    if (!invalid_list)
        return;

    for (i = 0; i < num_invalid; ++i)
        RELEASE(invalid_list[i]);
    RELEASE(invalid_list);
}
