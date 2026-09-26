/* ************************************************************************
 *   File: modify.c                                      Part of CircleMUD *
 *  Usage: Run-time modification of game variables                         *
 *                                                                         *
 *  All rights reserved.  See license.doc for complete information.        *
 *                                                                         *
 *  Copyright (C) 1993 by the Trustees of the Johns Hopkins University     *
 *  CircleMUD is based on DikuMUD, Copyright (C) 1990, 1991.               *
 ************************************************************************ */

#include "platdef.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "comm.h"
#include "db.h"
#include "handler.h"
#include "interpre.h"
#include "mail.h"
#include "protos.h"
#include "structs.h"
#include "utils.h"

#define TP_MOB 0
#define TP_OBJ 1
#define TP_ERROR 2

void show_string(struct descriptor_data* d, char* input);

const char* string_fields[] = {
    "name",
    "short",
    "long",
    "description",
    "title",
    "delete-description",
    "\n"
};

/* maximum length for text field x+1 */
unsigned int length[] = {
    15,
    60,
    256,
    240,
    60
};

char* skill_fields[] = {
    "learned",
    "affected",
    "duration",
    "recognize",
    "\n"
};

int max_value[] = {
    255,
    255,
    10000,
    1
};

extern sh_int screen_width;
/* ************************************************************************
 *  modification of malloc'ed strings                                      *
 ************************************************************************ */

int word_length(char* str)
{
    int tmp = 0;
    while (str[tmp] && (str[tmp] > ' '))
        tmp++;
    return tmp;
}
int space_length(char* str)
{
    int tmp = 0;
    while (str[tmp] && (str[tmp] <= ' '))
        tmp++;
    return tmp;
}

char* format_string(char* str)
{
    /* the string should be without '\n\r' already */
    int len, strscan, bufscan, tmp, linecount;
    int spacelen, wordlen;

    len = strlen(str);
    bzero(buf, MAX_STRING_LENGTH);

    linecount = 3;
    strcpy(buf, "   ");
    bufscan = 3; // paragraph indentation
    for (strscan = 0; (strscan < len) && (str[strscan] <= ' '); strscan++)
        ;

    while (strscan < len) {
        spacelen = space_length(str + strscan);
        wordlen = word_length(str + strscan + spacelen);

        if (linecount + spacelen + wordlen < screen_width) {
            for (tmp = 0; tmp < spacelen; tmp++)
                buf[bufscan++] = ' ';
            strscan += spacelen;
            linecount += spacelen;
        } else {
            buf[bufscan++] = '\n';
            buf[bufscan++] = '\r';
            linecount = 0;
            strscan += spacelen;
        }
        for (tmp = 0; tmp < wordlen; tmp++)
            buf[bufscan++] = str[strscan++];

        linecount += wordlen;
    }

    buf[bufscan++] = '\n';
    buf[bufscan++] = '\r';
    buf[bufscan++] = 0;

    return str_dup(buf);
}

int replace_pattern(descriptor_data* d, char* pattern, char* new_pattern)
{
    unsigned int len, pat_len, new_len, count, tmp, bufscan;

    if (!pattern || !*pattern || !new_pattern)
        return 0;

    bzero(buf, MAX_STRING_LENGTH);

    bufscan = 0;
    len = strlen(*d->str);
    pat_len = strlen(pattern);
    new_len = strlen(new_pattern);
    count = 0;

    for (tmp = 0; tmp < len; tmp++) {
        if (!strncmp(*d->str + tmp, pattern, pat_len)) {
            tmp += pat_len - 1;
            strncpy(buf + bufscan, new_pattern, new_len);
            bufscan += new_len;
            if (tmp < d->cur_str)
                d->cur_str = d->cur_str + new_len - pat_len;

            count++;
        } else {
            buf[bufscan] = *(*d->str + tmp);
            bufscan++;
        }
    }
    buf[bufscan] = 0;

    if (!count)
        return 0;

    d->len_str = bufscan;

    if (bufscan < d->max_str) {
        strcpy(*d->str, buf);
        *(*d->str + bufscan) = 0;
    } else {
        RELEASE(*d->str);
        CREATE(*d->str, char, bufscan + BLOCK_STR_LEN);
        strcpy(*d->str, buf);
        *(*d->str + bufscan) = 0;
    }
    return count;
}

void string_add_init(struct descriptor_data* d, char** str)
{
    char* tmpstr;
    int tmp;

    if (!d->character) {
        printf("no char in init!\n");
        return;
    }

    send_to_char("Edit the text now, type %e to save and exit, %q to abort,\n\r"
                 "%f to format, %h for help.\n\r",
        d->character);
    if (str)
        if (*str) {
            send_to_char("Your text so far:\n\r", d->character);
            send_to_char(*str, d->character);
        }

    d->str = str;
    if (!d->str) {
        printf("creating d->str\n");
        CREATE1(d->str, char*);
    }

    if (*d->str) {
        if (*str) {
            tmp = strlen(*d->str);
            CREATE(tmpstr, char, BLOCK_STR_LEN * (1 + tmp / BLOCK_STR_LEN));
            strncpy(tmpstr, *d->str, tmp);
            *d->str = tmpstr;
            tmpstr = 0;
            d->max_str = BLOCK_STR_LEN * (1 + tmp / BLOCK_STR_LEN);
            d->len_str = tmp;
        }
    } else {
        CREATE(*d->str, char, BLOCK_STR_LEN);
        d->max_str = BLOCK_STR_LEN;
        d->len_str = 2;
        (*d->str)[0] = '\n';
        (*d->str)[1] = '\r';
    }
    d->cur_str = d->len_str;
    SET_BIT(PLR_FLAGS(d->character), PLR_WRITING);
}

void string_add_finish(struct descriptor_data* d)
/*** Our writer has decided to stop writing. ***/
{
    if (!d->str)
        printf("no text!\n");
    else if (!(*d->str))
        d->str = 0;
    else if (strlen(*d->str) >= MAX_STRING_LENGTH)
        *(*d->str + MAX_STRING_LENGTH - 1) = 0;

    d->str = 0;
    if (!d->connected && d->character && !IS_NPC(d->character)) {
        REMOVE_BIT(PLR_FLAGS(d->character), PLR_WRITING);
        act("$n finished writing.", TRUE, d->character, 0, 0, TO_ROOM);
    }
}

void string_add(struct descriptor_data* d, char* str)
/*** Add some user input to the current string: d->str ***/
{
    char* scan;
    char *tmpstr, *tmpstr2;
    int terminator = 0;
    int tmp, tmp2, tmpmark, crmark;

    tmpmark = d->cur_str;
    crmark = 1;

    /* determine if this is the terminal string, and truncate if so */
    for (scan = str; *scan && *scan <= ' '; scan++) {
        if (*scan == '$') {
            memmove(scan, scan + 1, strlen(scan + 1));
        }
    }

    if (*scan == '%' || *scan == '~') {
        switch (tolower(*(scan + 1))) {

        case 'q':
            *str = 0;
            scan = str - 1;
            d->len_str = 0;

            if (d->str)
                RELEASE(*d->str);
            d->max_str = 0;
            terminator = -1;
            break;
        case 'e':
            terminator = 1;
            *scan = 0;
            *(scan + 1) = 0;
            crmark = 0;
            break;
        case 'd':
            if (!*d->str)
                break;

            if (!isdigit(*(scan + 2)) && (*(scan + 2) != '-'))
                tmp2 = -1;
            else
                tmp2 = atoi(scan + 2);

            if (tmp2 < 0) {
                tmp = d->cur_str - 1;
                for (; (tmp2 < 0) && (tmp > 0); tmp2++) {
                    for (; tmp >= 0 && *(*d->str + tmp) < ' '; tmp--)
                        continue;
                    for (; tmp >= 0 && *(*d->str + tmp) >= ' '; tmp--)
                        continue;
                }
                tmpmark = tmp + 1;
            } else {
                tmp = d->cur_str;
                for (; (tmp2 > 0) && (tmp < (int)d->len_str); tmp2--) {
                    for (; tmp < (int)d->len_str && *(*d->str + tmp) >= ' '; tmp++)
                        continue;
                    for (; tmp < (int)d->len_str && *(*d->str + tmp) < ' '; tmp++)
                        continue;
                }
                tmpmark = d->cur_str;
                d->cur_str = tmp;
            }
            *str = 0;
            *(scan + 1) = 0;
            crmark = 0;

            if (!d->character)
                break;
            send_to_char("Line removed.\n\r", d->character);
            break;
        case 'l':
            if (!isdigit(*(scan + 2)) && *(scan + 2) != '-')
                tmp2 = 999;
            else
                tmp2 = atoi(scan + 2);

            for (tmp = 0; (tmp2 > 0) && (tmp < (int)d->len_str); tmp2--) {
                for (; tmp < (int)d->len_str && *(*d->str + tmp) >= ' '; tmp++)
                    continue;
                for (; tmp < (int)d->len_str && *(*d->str + tmp) < ' '; tmp++)
                    continue;
            }

            d->cur_str = tmp;
            tmpmark = tmp;
            crmark = 0;
            *str = 0;
            *(scan + 1) = 0;
            if (!IS_SET(PRF_FLAGS(d->character), PRF_SPAM))
                break;
        case 'r':
            if ((int)d->len_str == 0) {
                send_to_char("Display what?\n\r", d->character);
                *str = 0;
                break;
            }
            send_to_char("Your text so far:\n\r", d->character);
            tmp = *(*d->str + d->cur_str);
            *(*d->str + d->cur_str) = 0;
            send_to_char(*d->str, d->character);
            send_to_char(">>\n\r", d->character);
            *(*d->str + d->cur_str) = tmp;
            *(*d->str + d->len_str) = 0;
            send_to_char(*d->str + d->cur_str, d->character);
            *str = 0;
            *(scan + 1) = 0;
            crmark = 0;
            break;
        case 's':
            for (tmpstr2 = scan + 2; *tmpstr2 && (*tmpstr2 != '~'); tmpstr2++)
                continue;

            tmpstr = scan + 2;
            if (tmpstr == tmpstr2)
                send_to_char("No pattern to replace.\n\r", d->character);
            else {
                *tmpstr2 = 0;
                tmp = replace_pattern(d, tmpstr, tmpstr2 + 1);
                sprintf(buf, "Replaced %d occurence%s.\n\rYour text is:\n\r", tmp,
                    !tmp ? "" : "s");
                send_to_char(buf, d->character);
                send_to_char(*d->str, d->character);
                tmpmark = d->cur_str;
            }
            *str = 0;
            break;
        case 'f':
            if ((int)d->len_str == 0) {
                send_to_char("What are you trying to format?\n\r", d->character);
                *str = 0;
                break;
            }
            for (tmp = 0; tmp < (int)d->len_str; tmp++)
                if (*(*d->str + tmp) == '\n')
                    *(*d->str + tmp) = ' ';
                else if (*(*d->str + tmp) < ' ')
                    memmove(*d->str + tmp, *d->str + tmp + 1, d->len_str - tmp);

            *(*d->str + d->len_str) = 0;

            tmpstr = format_string(*d->str);
            RELEASE(*d->str);
            *d->str = tmpstr;
            tmpmark = d->len_str = d->cur_str = d->max_str = strlen(tmpstr);
            crmark = 0;
            *str = 0;
            send_to_char("Your text is formatted.\n\r", d->character);
            send_to_char(*d->str, d->character);
            break;
        case '%':
            strcpy(scan + 1, scan + 2);
            break;
        case '~':
            strcpy(scan + 1, scan + 2);
            break;
        case 'h':
            if (!d->character)
                break;

            send_to_char("The commands work only if at the line start.\n\r"
                         "The following commands exist:\n\r"
                         "%e   - to finish the text;\n\r"
                         "%q   - to cancel the text;\n\r"
                         "%d   - to remove the previous line;\n\r"
                         "%d<+/-num> - to remove several lines back or forward;\n\r"
                         "%l   - to set the cursor at the end of the text;\n\r"
                         "%l<num> - to set the cursor after the line <num>;\n\r"
                         "%r   - to redisplay the text;\n\r"
                         "%s<old_string>~<new_string> - to replace all substrings;\n\r"
                         "%f   - to reformat the whole text;\n\r"
                         "%%   - to insert % sign;\n\r"
                         "%h   - to see this help.\n\r",
                d->character);
            return;
        case 0:
            break;
        default:
            break;
        }
    }

    if (terminator >= 0) {
        if (!(*d->str)) {
            CREATE(*d->str, char, strlen(str) + 3);
            d->max_str = strlen(str) + 3;
            strcpy(*d->str, str);
            strcat(*d->str, "\n\r");
            d->len_str = strlen(str) + 2;
            d->cur_str = d->len_str;
        } else {
            if (strlen(str) + d->len_str + 12 > d->max_str) {
                tmp = strlen(str) + d->len_str + 2;
                tmp = (int)(tmp / BLOCK_STR_LEN + 1) * BLOCK_STR_LEN;
                CREATE(tmpstr, char, tmp);
                d->max_str = tmp;
                memcpy(tmpstr, *d->str, tmp - 2);
                RELEASE(*d->str);
                *d->str = tmpstr;
                tmpstr = 0;
            }
            tmp = strlen(str) + 2 * crmark;

            memmove(*d->str + tmpmark + tmp, *d->str + d->cur_str,
                d->len_str - d->cur_str);
            memcpy(*d->str + tmpmark, str, tmp - 2 * crmark);

            if (crmark)
                memcpy(*d->str + tmpmark + tmp - 2, "\n\r", 2);
            d->len_str += tmp - d->cur_str + tmpmark;
            d->cur_str = tmpmark + tmp;
            *(*d->str + d->len_str) = 0;
        }
    }

    if (terminator) {
        if (!d->connected && (PLR_FLAGGED(d->character, PLR_MAILING))) {
            store_mail(d->name, d->character->player.name, *d->str);
            if (*d->str)
                SEND_TO_Q("Message sent!\n\r", d);
            else
                SEND_TO_Q("Message aborted!\n\r", d);
            if (*d->str)
                RELEASE(*d->str);
            RELEASE(d->str);
            RELEASE(d->name);
            d->name = 0;
            if (!IS_NPC(d->character))
                REMOVE_BIT(PLR_FLAGS(d->character), PLR_MAILING | PLR_WRITING);
        }
        string_add_finish(d);
    }
}

/* interpret an argument for do_string */
void quad_arg(char* arg, int* type, char* name, int* field, char* string)
{
    char buf[MAX_STRING_LENGTH];

    /* determine type */
    arg = one_argument(arg, buf);
    if (is_abbrev(buf, "char"))
        *type = TP_MOB;
    else if (is_abbrev(buf, "obj"))
        *type = TP_OBJ;
    else {
        *type = TP_ERROR;
        return;
    }

    /* find name */
    arg = one_argument(arg, name);

    /* field name and number */
    arg = one_argument(arg, buf);
    if (!(*field = old_search_block(buf, 0, strlen(buf), string_fields, 0)))
        return;

    /* string */
    for (; isspace(*arg); arg++)
        ;
    for (; (*string = *arg); arg++, string++)
        ;

    return;
}

/* modification of malloc'ed strings in chars/objects */
ACMD(do_string)
{
    char name[MAX_STRING_LENGTH], string[MAX_STRING_LENGTH];
    int field, type;
    struct char_data* mob;
    struct obj_data* obj;
    struct extra_descr_data *ed, *tmp;

    if (IS_NPC(ch))
        return;

    quad_arg(arg, &type, name, &field, string);

    if (type == TP_ERROR) {
        send_to_char("Usage: string ('obj'|'char') <name> <field> [<string>]", ch);
        return;
    }

    if (!field) {
        send_to_char("No field by that name. Try 'help string'.\n\r", ch);
        return;
    }

    if (type == TP_MOB) {
        /* locate the beast */
        if (!(mob = get_char_vis(ch, name))) {
            send_to_char("I don't know anyone by that name...\n\r", ch);
            return;
        }

        if (IS_NPC(mob)) {
            send_to_char("Sorry, string is disabled for mobs.\n\r", ch);
            return;
        }

        switch (field) {
        case 1:
            if (!IS_NPC(mob) && GET_LEVEL(ch) < LEVEL_IMPL) {
                send_to_char("You can't change that field for players.", ch);
                return;
            }
            ch->desc->str = &(mob->player.name);
            if (!IS_NPC(mob))
                send_to_char("WARNING: You have changed the name of a player.\n\r", ch);
            break;
        case 2:
            if (!IS_NPC(mob)) {
                send_to_char("That field is for monsters only.\n\r", ch);
                return;
            }
            ch->desc->str = &mob->player.short_descr;
            break;
        case 3:
            if (!IS_NPC(mob)) {
                send_to_char("That field is for monsters only.\n\r", ch);
                return;
            }
            ch->desc->str = &mob->player.long_descr;
            break;
        case 4:
            ch->desc->str = &mob->player.description;
            break;
        case 5:
            if (IS_NPC(mob)) {
                send_to_char("Monsters have no titles.\n\r", ch);
                return;
            }
            ch->desc->str = &mob->player.title;
            break;
        default:
            send_to_char("That field is undefined for monsters.\n\r", ch);
            return;
            break;
        }
    } else /* type == TP_OBJ */ {
        send_to_char("Sorry, string has been disabled for objects.\n\r", ch);
        return;

        /* locate the object */
        if (!(obj = get_obj_vis(ch, name))) {
            send_to_char("Can't find such a thing here..\n\r", ch);
            return;
        }

        switch (field) {
        case 1:
            ch->desc->str = &obj->name;
            break;
        case 2:
            ch->desc->str = &obj->short_description;
            break;
        case 3:
            ch->desc->str = &obj->description;
            break;
        case 4:
            if (!*string) {
                send_to_char("You have to supply a keyword.\n\r", ch);
                return;
            }
            /* try to locate extra description */
            for (ed = obj->ex_description;; ed = ed->next)
                if (!ed) /* the field was not found. create a new one. */ {
                    CREATE(ed, struct extra_descr_data, 1);
                    ed->next = obj->ex_description;
                    obj->ex_description = ed;
                    CREATE(ed->keyword, char, strlen(string) + 1);
                    strcpy(ed->keyword, string);
                    ed->description = 0;
                    ch->desc->str = &ed->description;
                    send_to_char("New field.\n\r", ch);
                    break;
                } else if (!str_cmp(ed->keyword, string)) /* the field exists */ {
                    RELEASE(ed->description);
                    ed->description = 0;
                    ch->desc->str = &ed->description;
                    send_to_char("Modifying description.\n\r", ch);
                    break;
                }
            ch->desc->max_str = MAX_STRING_LENGTH;
            return; /* the stndrd (see below) procedure does not apply here */
            break;
        case 6:
            if (!*string) {
                send_to_char("You must supply a field name.\n\r", ch);
                return;
            }
            /* try to locate field */
            for (ed = obj->ex_description;; ed = ed->next)
                if (!ed) {
                    send_to_char("No field with that keyword.\n\r", ch);
                    return;
                } else if (!str_cmp(ed->keyword, string)) {
                    RELEASE(ed->keyword);
                    RELEASE(ed->description);

                    /* delete the entry in the desr list */
                    if (ed == obj->ex_description)
                        obj->ex_description = ed->next;
                    else {
                        for (tmp = obj->ex_description; tmp->next != ed;
                             tmp = tmp->next)
                            ;
                        tmp->next = ed->next;
                    }
                    RELEASE(ed);

                    send_to_char("Field deleted.\n\r", ch);
                    return;
                }
            break;
        default:
            send_to_char(
                "That field is undefined for objects.\n\r", ch);
            return;
            break;
        }
    }

    RELEASE(*ch->desc->str);

    if (*string) /* there was a string in the argument array */ {
        if (strlen(string) > length[field - 1]) {
            send_to_char("String too long - truncated.\n\r", ch);
            *(string + length[field - 1]) = '\0';
        }
        CREATE(*ch->desc->str, char, strlen(string) + 1);
        strcpy(*ch->desc->str, string);
        ch->desc->str = 0;
        send_to_char("Ok.\n\r", ch);
    } else /* there was no string. enter string mode */ {
        send_to_char("Enter string. terminate with '@'.\n\r", ch);
        *ch->desc->str = 0;
        ch->desc->max_str = length[field - 1];
    }
}

/* db stuff *********************************************** */

/* One_Word is like one_argument, execpt that words in quotes "" are */
/* regarded as ONE word                                              */

char* one_word(char* argument, char* first_arg)
{
    int found, begin, look_at;
    found = begin = 0;

    do {
        for (; isspace(*(argument + begin)); begin++)
            ;

        if (*(argument + begin) == '\"') { /* is it a quote */
            begin++;

            for (look_at = 0; (*(argument + begin + look_at) >= ' ') && (*(argument + begin + look_at) != '\"'); look_at++)
                *(first_arg + look_at) = LOWER(*(argument + begin + look_at));

            if (*(argument + begin + look_at) == '\"')
                begin++;

        } else
            for (look_at = 0; *(argument + begin + look_at) > ' '; look_at++)
                *(first_arg + look_at) = LOWER(*(argument + begin + look_at));

        *(first_arg + look_at) = '\0';
        begin += look_at;
    } while (fill_word(first_arg));

    return (argument + begin);
}

struct help_index_element*
build_help_index(FILE* fl, int* num, struct help_index_element** listpt)
{
    int nr = -1, issorted, i;
    struct help_index_element *list = *listpt, mem;
    struct help_index_element* list2;
    char buf[81], tmp[81], *scan;
    long pos;

    for (;;) {
        pos = ftell(fl);
        fgets(buf, 81, fl);
        *(buf + strlen(buf) - 1) = '\0';
        scan = buf;
        for (;;) {
            /* extract the keywords */
            scan = one_word(scan, tmp);

            if (!*tmp)
                break;

            if (!list) {
                CREATE(list, struct help_index_element, 10);
                nr = 0;
            } else {
                CREATE(list2, struct help_index_element, ++nr + 1);
                memmove(list2, list, nr * sizeof(struct help_index_element));
                RELEASE(list);
                list = list2;
            }
            list[nr].pos = pos;
            CREATE(list[nr].keyword, char, strlen(tmp) + 1);
            strcpy(list[nr].keyword, tmp);
        }
        /* skip the text */
        do
            fgets(buf, 81, fl);
        while (*buf != '#');

        if (*(buf + 1) == '~')
            break;
    }
    /* we might as well sort the stuff */
    do {
        issorted = 1;
        for (i = 0; i < nr; i++) {
            if (str_cmp(list[i].keyword, list[i + 1].keyword) > 0) {
                mem = list[i];
                list[i] = list[i + 1];
                list[i + 1] = mem;
                issorted = 0;
            }
        }
    } while (!issorted);

    *num = nr;
    *listpt = list;
    return (list);
}

void page_string(struct descriptor_data* d, char* str, int keep_internal)
{
    if (!d)
        return;

    if (keep_internal) {
        CREATE(d->showstr_head, char, strlen(str) + 1);
        strcpy(d->showstr_head, str);
        d->showstr_point = d->showstr_head;
    } else
        d->showstr_point = str;

    show_string(d, "");
}

void show_string(struct descriptor_data* d, char* input)
{
    char buffer[MAX_STRING_LENGTH], buf[MAX_INPUT_LENGTH];
    register char *scan, *chk;
    int lines = 0;

    one_argument(input, buf);

    if (*buf) {
        RELEASE(d->showstr_head);
        d->showstr_point = 0;
        return;
    }

    /* show a chunk */
    for (scan = buffer;; scan++, d->showstr_point++)
        if ((*scan = *d->showstr_point) == '\n')
            lines++;
        else if (!*scan || (lines >= 22)) {
            if (*scan == '\r')
                scan++;

            *scan = '\0';
            SEND_TO_Q(buffer, d);

            /* see if this is the end (or near the end) of the string */
            for (chk = d->showstr_point; isspace(*chk); chk++)
                ;

            if (!*chk) {
                RELEASE(d->showstr_head);
                d->showstr_point = 0;
            }

            return;
        }
}
