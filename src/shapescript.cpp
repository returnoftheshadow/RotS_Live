/**********************************************************************
 *         Return of the Shadow Shape Scripting Functions              *
 *                                                                     *
 * How shaping scripts works:                                          *
 *   At boot a block of memory is created which holds the header for   *
 *   all scripts called script_table (script_head * number_of_scripts).*
 *   The individual components are struct script_head and are the      *
 *   headers for linked lists of script commands (script_data).        *
 *   When shaping, a temporary script_head and script_data commands are*
 *   created and edited.  When shaping is /implemented these temporary *
 *   structures are copied into the script_table - only if the script  *
 *   existed at reboot;  if not, they can only be /saved and will      *
 *   appear in the script_table when the mud next reboots.             *
 *                                                                     *
 * To add a new command or trigger:                                    *
 *   Add a #define in script.h for the new command                     *
 *   Add the text entry for it in get_command (shapescript.cc)         *
 *   Add the display properties to show_command (shapescript.cc)       *
 *   Add entry handling in shape_center_script  (shapescript.cc)       *
 *   Add the command to run_script (script.cc)                         *
 *   Easy huh?                                                         *
 *                                                                     *
 * Script file format:                                                 *
 *   #<script number> Title of script                                  *
 *   Long description                                                  *
 *   of script.  Multi-line ending in an empty line containing:        *
 *   ~                                                                 *
 *   <comand_type> <command_number> <params 0..5>                      *
 *   Text for command ending in~                                       *
 *   ...                                                               *
 *   999 0 0 0 0 0 0 0        <- indicates last command (no text line) *
 *   #<script number> Title of script                                  *
 *   ... etc                                                           *
 *   999 0 0 0 0 0 0 0                                                 *
 *   #99999                                                            *
 *   $~                                                                *
 **********************************************************************/

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
#include "script.h"
#include "structs.h"
#include "utils.h"

// External declarations
extern struct room_data world;
extern struct script_head* script_table;
int shape_standup(struct char_data* ch, int pos);
int get_text(FILE* f, char** line);
int get_command(char* command);
void shape_disabled(struct char_data* ch, const char* prefix, const char* typed);

// Local declarations
int replace_script(struct char_data* ch, char* arg);
int get_parameter(char* param);
char* get_param_text(int param);

/* Free the temporary structures associated with shaping a script.
   If the script should be implemented/saved, then this should
   already have happened */

void free_script(struct char_data* ch)
{

    if (!SHAPE_SCRIPT(ch))
        return;
    if (IS_SET(SHAPE_SCRIPT(ch)->flags, SHAPE_SCRIPT_LOADED)) {
        // free memory here...

        RELEASE(SHAPE_SCRIPT(ch)->name);
        RELEASE(SHAPE_SCRIPT(ch)->description);

        SHAPE_SCRIPT(ch)
            ->script
            = 0;
        REMOVE_BIT(SHAPE_SCRIPT(ch)->flags, SHAPE_SCRIPT_LOADED);
        ch->specials.prompt_value = 0;
    }
    ch->specials.position = SHAPE_SCRIPT(ch)->position;
    REMOVE_BIT(PRF_FLAGS(ch), PRF_DISPTEXT);
    if (ch->temp)
        RELEASE(ch->temp);
    if (GET_POS(ch) <= POSITION_SHAPING)
        GET_POS(ch) = POSITION_STANDING;
}

/* Starting with * root free all script_data commands in a linked list */

void free_script_list(script_data* root)
{
    script_data* tmpscript;
    script_data* tmpscript2;

    for (tmpscript = root; tmpscript;) {
        tmpscript2 = tmpscript;
        tmpscript = tmpscript2->next;
        RELEASE(tmpscript2->text);
        RELEASE(tmpscript2);
    }
}

void new_script(struct char_data* ch)
{

    // Should CREATE1 a script and initialise all variables
}

int load_script(struct char_data* ch, char* arg)
{
    int number, index, i, tmp;
    char str[255], fname[80];
    FILE* f;
    script_data* tmpscript = 0;
    script_data* newscript = 0;
    script_data* lastcmd = 0;

    // In other shape(x) files this would load the mob/zone/room/object from file.
    // Seems unnecessary (actually we should change to this method), so in shape scripting we copy the existing in-memory script.
    // If this causes problems we'll have to load from file at this point too.

    if (2 != sscanf(arg, "%s %d", str, &number)) {
        send_to_char("Choose a script by 'shape script <script number>'\n\r", ch);
        return -1;
    }

    // Just checking the script file exists... (not loading commands from file)

    sprintf(fname, "%d", number / 100);
    sprintf(str, SHAPE_SCRIPT_DIR, fname);

    send_to_char(str, ch);
    f = fopen(str, "r+");
    if (f == 0) {
        send_to_char(" could not open that file.\n\r", ch);
        return -1;
    }
    fclose(f);

    strcpy(SHAPE_SCRIPT(ch)->f_from, str);
    SET_BIT(SHAPE_SCRIPT(ch)->flags, SHAPE_FILENAME);
    sprintf(SHAPE_SCRIPT(ch)->f_old, SHAPE_SCRIPT_BACKDIR, fname);

    // Now to find the index of the script - if we can't then we create an empty script

    if ((index = find_script_by_number(number)) == -1) {
        sprintf(str, " could not find script #%d, created it.\n\r", number);
        send_to_char(str, ch);
        SHAPE_SCRIPT(ch)
            ->name
            = 0;
        SHAPE_SCRIPT(ch)
            ->description
            = 0;
        SHAPE_SCRIPT(ch)
            ->number
            = number;
        SHAPE_SCRIPT(ch)
            ->index_pos
            = -1;
        CREATE1(newscript, script_data);
        newscript->room = 0;
        newscript->number = 1;
        newscript->next = 0;
        newscript->prev = 0;
        newscript->text = 0;
        newscript->command_type = SCRIPT_COMMAND_NONE;
        for (tmp = 0; tmp < 6; tmp++)
            newscript->param[tmp] = 0;
        SHAPE_SCRIPT(ch)
            ->root
            = newscript;
        SHAPE_SCRIPT(ch)
            ->script
            = newscript;
    } else {

        SHAPE_SCRIPT(ch)
            ->index_pos
            = index;

        // Assign memory and copy over the text elements of the header

        CREATE(SHAPE_SCRIPT(ch)->name, char, strlen(script_table[index].name) + 1);
        CREATE(SHAPE_SCRIPT(ch)->description, char, strlen(script_table[index].description) + 1);
        strcpy(SHAPE_SCRIPT(ch)->name, script_table[index].name);
        strcpy(SHAPE_SCRIPT(ch)->description, script_table[index].description);
        SHAPE_SCRIPT(ch)
            ->number
            = script_table[index].number;
        SHAPE_SCRIPT(ch)
            ->script
            = 0;
        SHAPE_SCRIPT(ch)
            ->root
            = 0;
        if (script_table[index].script) {
            for (tmpscript = script_table[index].script; tmpscript; tmpscript = tmpscript->next) {
                CREATE1(newscript, script_data);
                newscript->room = tmpscript->room;
                newscript->number = tmpscript->number;
                newscript->command_type = tmpscript->command_type;
                if (tmpscript->text) {
                    CREATE(newscript->text, char, strlen(tmpscript->text) + 1);
                    strcpy(newscript->text, tmpscript->text);
                }
                for (i = 0; i < 6; i++)
                    newscript->param[i] = tmpscript->param[i];
                newscript->next = 0;
                if (lastcmd) {
                    newscript->prev = lastcmd;
                    lastcmd->next = newscript;
                } else {
                    newscript->prev = 0;
                    SHAPE_SCRIPT(ch)
                        ->script
                        = newscript;
                    SHAPE_SCRIPT(ch)
                        ->root
                        = newscript;
                }
                lastcmd = newscript;
            }
        } else {
            CREATE1(newscript, script_data);
            newscript->room = 0;
            newscript->number = 1;
            newscript->next = 0;
            newscript->prev = 0;
            newscript->text = 0;
            for (tmp = 0; tmp < 6; tmp++)
                newscript->param[tmp] = 0;
            newscript->command_type = SCRIPT_COMMAND_NONE;
            SHAPE_SCRIPT(ch)
                ->root
                = newscript;
            SHAPE_SCRIPT(ch)
                ->script
                = newscript;
        }
    }
    ch->specials.prompt_value = number;
    SET_BIT(SHAPE_SCRIPT(ch)->flags, SHAPE_SCRIPT_LOADED);
    SHAPE_SCRIPT(ch)
        ->permission
        = get_permission(number / 100, ch);
    return number;
}

void write_script(FILE* f, struct char_data* ch)
{

    script_data* tmpscript = 0;

    fprintf(f, "#%-d ", SHAPE_SCRIPT(ch)->number);
    if (SHAPE_SCRIPT(ch)->name)
        fprintf(f, "%s~\n", SHAPE_SCRIPT(ch)->name);
    else
        fprintf(f, "~\n");
    if (SHAPE_SCRIPT(ch)->description)
        fprintf(f, "%s~", SHAPE_SCRIPT(ch)->description);
    else
        fprintf(f, "~");
    fprintf(f, "\n");

    tmpscript = SHAPE_SCRIPT(ch)->root;

    while (tmpscript) {

        fprintf(f, "%d %d %d %d %d %d %d %d\n",
            tmpscript->command_type,
            tmpscript->number,
            tmpscript->param[0],
            tmpscript->param[1],
            tmpscript->param[2],
            tmpscript->param[3],
            tmpscript->param[4],
            tmpscript->param[5]);
        if (tmpscript->text)
            fprintf(f, " %s~\n", tmpscript->text);
        else
            fprintf(f, "~\n");

        tmpscript = tmpscript->next;
    }
    fprintf(f, "999 0 0 0 0 0 0\n");
}

int append_script(struct char_data* ch, char* arg)
{
    FILE* f1;
    FILE* f2;
    char *f_from, *f_old;
    int check, i, i1;
    char c;
    char str[255], fname[80];

    if (SHAPE_SCRIPT(ch)->permission == 0) {
        send_to_char("You're not authorised in this zone to do this.\n\r", ch);
        return -1;
    }
    if (SHAPE_SCRIPT(ch)->number != -1) {
        send_to_char("Script was already added to database. Saving.\n\r", ch);
        replace_script(ch, arg);
        return -1;
    }
    /* The old 'add <filename>' read passed its sscanf arguments the wrong way
     * round (undefined behaviour); the file is the one the script loaded from. */
    if (!IS_SET(SHAPE_SCRIPT(ch)->flags, SHAPE_FILENAME)) {
        send_to_char("No file defined to write into.\n\r", ch);
        return -1;
    }
    if (!IS_SET(SHAPE_SCRIPT(ch)->flags, SHAPE_SCRIPT_LOADED)) {
        send_to_char("you have no script to save...\n\r", ch);
        return -1;
    }

    f_from = SHAPE_SCRIPT(ch)->f_from;
    f_old = SHAPE_SCRIPT(ch)->f_old;

    //  Backup original file:
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
    if (!IS_SET(SHAPE_SCRIPT(ch)->flags, SHAPE_FILENAME))
        i = atoi(fname) * 100 + 1;
    else {
        for (c = 0; (f_from[c] < '0' || f_from[c] > '9') && f_from[c]; c++)
            ;
        sscanf(f_from + c, "%d", &i);
        i = i * 100;
    }
    do {
        do {
            check = fscanf(f1, "%c", &c);
            fprintf(f2, "%c", c);
        } while ((c != '#') && (check != EOF));
        i1 = i;
        fscanf(f1, "%d", &i);
        if (i != 99999)
            fprintf(f2, "%-d\n\r", i);
    } while ((i != 99999) && (check != EOF));

    if (check == EOF) {
        send_to_char("no final record in source file\n\r", ch);
    }
    fseek(f2, -1, SEEK_CUR);
    write_script(f2, ch);
    sprintf(str, "Script added to database as #%d.\n\r", i1 + 1);
    send_to_char(str, ch);
    SHAPE_SCRIPT(ch)
        ->number
        = i1 + 1;
    ch->specials.prompt_value = i1 + 1;
    fprintf(f2, "#99999\n\r");
    fclose(f1);
    fclose(f2);
    return i1;
}

// Replaces the current in-file script with this one - if the script does not exist append_script is called

int replace_script(struct char_data* ch, char* arg)
{
    FILE* f1;
    FILE* f2;
    char *f_from, *f_old;
    int num, check, i, oldnum;
    char c;
    char str[255];

    if (!IS_SET(SHAPE_SCRIPT(ch)->flags, SHAPE_FILENAME)) {
        send_to_char("How strange... you have no file defined to write to.\n\r", ch);
        return -1;
    }

    f_from = SHAPE_SCRIPT(ch)->f_from;
    f_old = SHAPE_SCRIPT(ch)->f_old;

    if (!IS_SET(SHAPE_SCRIPT(ch)->flags, SHAPE_SCRIPT_LOADED)) {
        send_to_char("you have no mobile to save...\n\r", ch);
        return -1;
    }
    if (!strcmp(f_from, f_old)) {
        send_to_char("better make source and target files different\n\r", ch);
        return -1;
    }
    if (SHAPE_SCRIPT(ch)->permission == 0) {
        send_to_char("You're not authorized in this zone to do this.\n\r", ch);
        return -1;
    }
    num = SHAPE_SCRIPT(ch)->number;
    if (SHAPE_SCRIPT(ch)->number == -1) {
        send_to_char("you created it afresh, remember? Adding it.\n\r", ch);
        append_script(ch, arg);
        return -1;
    }

    //  Backup original file:
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
        if (check == EOF)
            break;
        fscanf(f1, "%d", &i);
        if (i < num)
            fprintf(f2, "#%-d", i);
        else
            oldnum = i;
    } while ((i < num) && (check != EOF));
    if (check == EOF) {
        sprintf(str, "no script #%d in this file\n\r", num);
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
    if (!IS_SET(SHAPE_SCRIPT(ch)->flags, SHAPE_DELETE_ACTIVE)) {
        write_script(f2, ch);
        REMOVE_BIT(SHAPE_SCRIPT(ch)->flags, SHAPE_DELETE_ACTIVE);
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

void implement_script(struct char_data* ch)
{
    script_data* tmpscript;
    script_data* newscript;
    script_data* last_command;
    int i;

    // copy the script into memory

    // cannot be done if script does not exist:

    if (SHAPE_SCRIPT(ch)->index_pos == -1) {
        send_to_char("Cannot implement yet - maybe reboot will help... (imp script)\n\r", ch);
        return;
    }

    if (SHAPE_SCRIPT(ch)->name) {
        RELEASE(script_table[SHAPE_SCRIPT(ch)->index_pos].name);
        CREATE(script_table[SHAPE_SCRIPT(ch)->index_pos].name, char, strlen(SHAPE_SCRIPT(ch)->name) + 1);
        strcpy(script_table[SHAPE_SCRIPT(ch)->index_pos].name, SHAPE_SCRIPT(ch)->name);
    }
    if (SHAPE_SCRIPT(ch)->description) {
        RELEASE(script_table[SHAPE_SCRIPT(ch)->index_pos].description);
        CREATE(script_table[SHAPE_SCRIPT(ch)->index_pos].description, char,
            strlen(SHAPE_SCRIPT(ch)->description) + 1);
        strcpy(script_table[SHAPE_SCRIPT(ch)->index_pos].description, SHAPE_SCRIPT(ch)->description);
    }

    // Clear old script commands
    free_script_list(script_table[SHAPE_SCRIPT(ch)->index_pos].script);
    script_table[SHAPE_SCRIPT(ch)->index_pos].script = 0;

    // Create and copy new ones
    last_command = 0;
    for (tmpscript = SHAPE_SCRIPT(ch)->root; tmpscript; tmpscript = tmpscript->next) {
        CREATE1(newscript, script_data);
        newscript->room = tmpscript->number;
        newscript->number = tmpscript->number;
        newscript->command_type = tmpscript->command_type;
        for (i = 0; i < 6; i++)
            newscript->param[i] = tmpscript->param[i];
        if (tmpscript->text) {
            CREATE(newscript->text, char, strlen(tmpscript->text) + 1);
            strcpy(newscript->text, tmpscript->text);
        }
        newscript->next = 0;
        if (!last_command) {
            newscript->prev = 0;
            script_table[SHAPE_SCRIPT(ch)->index_pos].script = newscript;
        } else {
            newscript->prev = last_command;
            last_command->next = newscript;
        }
        last_command = newscript;
    }

    /* Tell the builder about any vnum in the script that names nothing. */
    check_script_vnums(SHAPE_SCRIPT(ch)->index_pos, ch);
}

void show_command(char_data* ch, script_data* script)
{

    switch (script->command_type) {

    case ON_BEFORE_ENTER:
        sprintf(buf, "[%d] TRIG ON_BEFORE_ENTER (%s)\n\r", script->number, script->text);
        break;

    case ON_DAMAGE:
        sprintf(buf, "[%d] TRIG ON_DAMAGE       (%s)\n\r", script->number, script->text);
        break;

    case ON_DIE:
        sprintf(buf, "[%d] TRIG ON_DIE          (%s)\n\r", script->number, script->text);
        break;

    case ON_EAT:
        sprintf(buf, "[%d] TRIG ON_EAT          (%s)\n\r", script->number, script->text);
        break;

    case ON_ENTER:
        sprintf(buf, "[%d] TRIG ON_ENTER        (%s)\n\r", script->number, script->text);
        break;

    case ON_EXAMINE_OBJECT:
        sprintf(buf, "[%d] TRIG ON_EXAMINE_OBJECT (%s)\n\r", script->number, script->text);
        break;

    case ON_DRINK:
        sprintf(buf, "[%d] TRIG ON_DRINK        (%s)\n\r", script->number, script->text);
        break;

    case ON_HEAR_SAY:
        sprintf(buf, "[%d] TRIG ON_HEAR_SAY     (%s)\n\r", script->number, script->text);
        break;

    case ON_HEAR_YELL:
        sprintf(buf, "[%d] TRIG ON_HEAR_YELL    (%s)\n\r", script->number, script->text);
        break;

    case ON_PULL:
        sprintf(buf, "[%d] TRIG ON_PULL         (%s)\n\r", script->number, script->text);
        break;

    case ON_RECEIVE:
        sprintf(buf, "[%d] TRIG ON_RECEIVE      (%s)\n\r", script->number, script->text);
        break;

    case ON_WEAR:
        sprintf(buf, "[%d] TRIG ON_WEAR         (%s)\n\r", script->number, script->text);
        break;

    case SCRIPT_ABORT:
        sprintf(buf, "[%d] ABORT EXECUTION      (%s)\n\r", script->number, script->text);
        break;

    case SCRIPT_ASSIGN_EQ:
        sprintf(buf, "[%d] SYS ASSIGN_EQ        character: %s, object: %s, position: %d, int true/false: %s\n\r", script->number,
            get_param_text(script->param[0]), get_param_text(script->param[1]), script->param[2], get_param_text(script->param[3]));
        break;

    case SCRIPT_ASSIGN_INV:
        sprintf(buf, "[%d] SYS ASSIGN_INV       vnum: %d assign to: %s, inv of: %s, int result: %s\n\r",
            script->number, script->param[0], get_param_text(script->param[1]), get_param_text(script->param[2]), get_param_text(script->param[3]));
        break;

    case SCRIPT_ASSIGN_ROOM:
        sprintf(buf, "[%d] SYS ASSIGN_ROOM      vnum: %d assign to: %s, room: %s, int result: %s\n\r",
            script->number, script->param[0], get_param_text(script->param[1]), get_param_text(script->param[2]), get_param_text(script->param[3]));
        break;

    case SCRIPT_ASSIGN_STR:
        sprintf(buf, "[%d] SYS ASSIGN_STR:      %s  to: %s\n\r",
            script->number, script->text, get_param_text(script->param[0]));
        break;

    case SCRIPT_BEGIN:
        sprintf(buf, "[%d] BEGIN                (%s)\n\r", script->number, script->text);
        break;

    case SCRIPT_CHANGE_EXIT_TO:
        sprintf(buf, "[%d] SYS CHANGE_EXIT_TO   room: %s, direction: %d, room to: %d (%s)\n\r", script->number,
            get_param_text(script->param[0]), script->param[1], script->param[2], script->text);
        break;

    case SCRIPT_COMMAND_NONE:
        sprintf(buf, "[%d] *** No command: script will terminate here ***\n\r", script->number);
        break;

    case SCRIPT_DO_DROP:
        sprintf(buf, "[%d] ACT DO_DROP          character: %s, object: %s (%s)\n\r", script->number,
            get_param_text(script->param[0]), get_param_text(script->param[1]), script->text);
        break;

    case SCRIPT_DO_EMOTE:
        sprintf(buf, "[%d] ACT DO_EMOTE         %s %s\n\r", script->number,
            get_param_text(script->param[0]), script->text);
        break;

    case SCRIPT_DO_FLEE:
        sprintf(buf, "[%d] ACT DO_FLEE          character: %s (%s)\n\r", script->number,
            get_param_text(script->param[0]), script->text);
        break;

    case SCRIPT_DO_FOLLOW:
        sprintf(buf, "[%d] ACT DO_FOLLOW        character: %s to follow: %s (%s)\n\r", script->number,
            get_param_text(script->param[0]), get_param_text(script->param[1]), script->text);
        break;

    case SCRIPT_DO_GIVE:
        sprintf(buf, "[%d] ACT  DO_GIVE         from: %s, to: %s, object: %s (%s)\n\r", script->number,
            get_param_text(script->param[0]), get_param_text(script->param[1]), get_param_text(script->param[2]), script->text);
        break;

    case SCRIPT_DO_HIT:
        sprintf(buf, "[%d] ACT  DO_HIT          (%s, %s)\n\r", script->number,
            get_param_text(script->param[0]), get_param_text(script->param[1]));
        break;

    case SCRIPT_DO_REMOVE:
        sprintf(buf, "[%d] ACT DO_REMOVE        character: %s, position: %d (%s)\n\r", script->number,
            get_param_text(script->param[0]), script->param[1], script->text);
        break;

    case SCRIPT_DO_SAY:
        sprintf(buf, "[%d] ACT DO_SAY           "
                     "%s"
                     " (%s)(%s)\n\r",
            script->number, script->text,
            get_param_text(script->param[0]), get_param_text(script->param[1]));
        break;

    case SCRIPT_DO_SOCIAL:
        sprintf(buf, "[%d] ACT DO_SOCIAL        social: %s, character: %s, to char (optional): %s.\n\r",
            script->number, script->text, get_param_text(script->param[0]), get_param_text(script->param[1]));
        break;

    case SCRIPT_DO_WAIT:
        sprintf(buf, "[%d] SYS DO_WAIT          wait for:%d (%s)\n\r", script->number,
            script->param[0], script->text);
        break;

    case SCRIPT_DO_WEAR:
        sprintf(buf, "[%d] ACT DO_WEAR          character: %s, object: %s (%s)\n\r", script->number,
            get_param_text(script->param[0]), get_param_text(script->param[1]), script->text);
        break;

    case SCRIPT_DO_YELL:
        sprintf(buf, "[%d] ACT DO_YELL          "
                     "%s"
                     " %s (%s)\n\r",
            script->number, script->text,
            get_param_text(script->param[0]), get_param_text(script->param[1]));
        break;

    case SCRIPT_END:
        sprintf(buf, "[%d] END                  (%s)\n\r", script->number, script->text);
        break;

    case SCRIPT_END_ELSE_BEGIN:
        sprintf(buf, "[%d] END_ELSE_BEGIN       (%s)\n\r", script->number, script->text);
        break;

    case SCRIPT_EQUIP_CHAR:
        sprintf(buf, "[%d] SYS EQUIP_CHAR       Equip: %s with: %d %d %d %d %d\n\r", script->number,
            get_param_text(script->param[0]), script->param[1], script->param[2], script->param[3],
            script->param[4], script->param[5]);
        break;

    case SCRIPT_EXTRACT_CHAR:
        sprintf(buf, "[%d] SYS EXTRACT_CHAR     Extract: %s (%s)\n\r", script->number,
            get_param_text(script->param[0]), script->text);
        break;

    case SCRIPT_EXTRACT_OBJ:
        sprintf(buf, "[%d] SYS EXTRACT_OBJ      Extract: %s (%s)\n\r", script->number,
            get_param_text(script->param[0]), script->text);
        break;

    case SCRIPT_GAIN_EXP:
        sprintf(buf, "[%d] SYS GAIN_EXP         Give %s, %s experience (%s)\n\r", script->number,
            get_param_text(script->param[0]), get_param_text(script->param[1]), script->text);
        break;

    case SCRIPT_IF_INT_EQUAL:
        sprintf(buf, "[%d] IF_INT_EQUAL:        compare: %s with %s (%s)\n\r", script->number,
            get_param_text(script->param[0]), get_param_text(script->param[1]), script->text);
        break;

    case SCRIPT_IF_INT_LESS:
        sprintf(buf, "[%d] IF_INT_LESS:         is %s less than %s? (%s)\n\r", script->number,
            get_param_text(script->param[0]), get_param_text(script->param[1]), script->text);
        break;

    case SCRIPT_IF_INT_GREATER:
        sprintf(buf, "[%d] IF_INT_GREATER:      is %s greater than %s? (%s)\n\r", script->number,
            get_param_text(script->param[0]), get_param_text(script->param[1]), script->text);
        break;

    case SCRIPT_IF_INT_TRUE:
        sprintf(buf, "[%d] IF_INT_TRUE:         is %s true? (%s)\n\r", script->number,
            get_param_text(script->param[0]), script->text);
        break;

    case SCRIPT_IF_INT_FALSE:
        sprintf(buf, "[%d] IF_INT_FALSE:        is %s false? (%s)\n\r", script->number,
            get_param_text(script->param[0]), script->text);
        break;

    case SCRIPT_IF_IS_NPC:
        sprintf(buf, "[%d] IF_IS_NPC:           is %s a mobile? (%s)\n\r", script->number,
            get_param_text(script->param[0]), script->text);
        break;

    case SCRIPT_IF_ROOM_SUNLIT:
        sprintf(buf, "[%d] IF_ROOM_SUNLIT:      does %s have sun? (%s)\n\r", script->number,
            get_param_text(script->param[0]), script->text);
        break;

    case SCRIPT_IF_STR_CONTAINS:
        sprintf(buf, "[%d] IF_STR_CONTAINS:     does %s contain "
                     "%s"
                     "?\n\r",
            script->number,
            get_param_text(script->param[0]), script->text);
        break;

    case SCRIPT_IF_STR_EQUAL:
        sprintf(buf, "[%d] IF_STR_EQUAL:        is %s the same as "
                     "%s"
                     "?\n\r",
            script->number,
            get_param_text(script->param[0]), script->text);
        break;

    case SCRIPT_LOAD_MOB:
        sprintf(buf, "[%d] SYS LOAD_MOB         vnum: %d, char variable: %s (%s)\n\r", script->number,
            script->param[0], get_param_text(script->param[1]), script->text);
        break;

    case SCRIPT_LOAD_OBJ:
        sprintf(buf, "[%d] SYS LOAD_OBJ         vnum: %d, obj variable: %s (%s)\n\r", script->number,
            script->param[0], get_param_text(script->param[1]), script->text);
        break;

    case SCRIPT_LOAD_OBJ_X:
        sprintf(buf, "[%d] SYS LOAD_OBJ_X       copies ob1 into: %s (%s)\n\r", script->number,
            get_param_text(script->param[1]), script->text);
        break;

    case SCRIPT_OBJ_FROM_CHAR:
        sprintf(buf, "[%d] SYS OBJ_FROM_CHAR    object: %s, character: %s (%s)\n\r", script->number,
            get_param_text(script->param[0]), get_param_text(script->param[1]), script->text);
        break;

    case SCRIPT_OBJ_FROM_ROOM:
        sprintf(buf, "[%d] SYS OBJ_FROM_ROOM    object: %s, room: %s (%s)\n\r", script->number,
            get_param_text(script->param[0]), get_param_text(script->param[1]), script->text);
        break;

    case SCRIPT_OBJ_TO_CHAR:
        sprintf(buf, "[%d] SYS OBJ_TO_CHAR      object: %s, character: %s (%s)\n\r", script->number,
            get_param_text(script->param[0]), get_param_text(script->param[1]), script->text);
        break;

    case SCRIPT_OBJ_TO_ROOM:
        sprintf(buf, "[%d] SYS OBJ_TO_ROOM      object: %s, room: %s (%s)\n\r", script->number,
            get_param_text(script->param[0]), get_param_text(script->param[1]), script->text);
        break;

    case SCRIPT_PAGE_ZONE_MAP:
        sprintf(buf, "[%d] MSG PAGE_ZONE_MAP    zone: %d, player: %s (%s)\n\r", script->number,
            script->param[1], get_param_text(script->param[0]), script->text);
        break;

    case SCRIPT_RAW_KILL:
        sprintf(buf, "[%d] SYS RAW_KILL         character/player: %s (%s)\n\r", script->number,
            get_param_text(script->param[0]), script->text);
        break;

    case SCRIPT_RETURN_FALSE:
        sprintf(buf, "[%d] SYS RETURN_FALSE     %s\n\r", script->number, script->text);
        break;

    case SCRIPT_SEND_TO_CHAR:
        sprintf(buf, "[%d] MSG SEND_TO_CHAR     "
                     "%s"
                     " to: %s (%s)\n\r",
            script->number, script->text,
            get_param_text(script->param[0]), get_param_text(script->param[1]));
        break;

    case SCRIPT_SEND_TO_ROOM:
        sprintf(buf, "[%d] MSG SEND_TO_ROOM     "
                     "%s"
                     " (%s)(%s)\n\r",
            script->number, script->text,
            get_param_text(script->param[0]), get_param_text(script->param[1]));
        break;

    case SCRIPT_SEND_TO_ROOM_X:
        sprintf(buf, "[%d] MSG SEND_TO_ROOM_X   "
                     "%s"
                     " (%s, %s)\n\r",
            script->number, script->text,
            get_param_text(script->param[0]), get_param_text(script->param[1]));
        break;

    case SCRIPT_SET_EXIT_STATE:
        sprintf(buf, "[%d] SYS SET_EXIT_STATE   room: %s, direction: %d, state: %d\n\r", script->number,
            get_param_text(script->param[2]), script->param[0], script->param[1]);
        break;

    case SCRIPT_SET_INT_DIV:
        sprintf(buf, "[%d] SYS SET_INT_DIV      %s = %s divided by %s (%s)\n\r", script->number,
            get_param_text(script->param[0]), get_param_text(script->param[1]),
            get_param_text(script->param[2]), script->text);
        break;

    case SCRIPT_SET_INT_MULT:
        sprintf(buf, "[%d] SYS SET_INT_MULT     %s = %s multiplied by %s (%s)\n\r", script->number,
            get_param_text(script->param[0]), get_param_text(script->param[1]),
            get_param_text(script->param[2]), script->text);
        break;

    case SCRIPT_SET_INT_RANDOM:
        sprintf(buf, "[%d] SYS SET_INT_RANDOM   %s = random number between %s and %s (%s)\n\r", script->number,
            get_param_text(script->param[0]), get_param_text(script->param[1]),
            get_param_text(script->param[2]), script->text);
        break;

    case SCRIPT_SET_INT_SUB:
        sprintf(buf, "[%d] SYS SET_INT_SUB      %s = %s minus %s (%s)\n\r", script->number,
            get_param_text(script->param[0]), get_param_text(script->param[1]),
            get_param_text(script->param[2]), script->text);
        break;

    case SCRIPT_SET_INT_SUM:
        sprintf(buf, "[%d] SYS SET_INT_SUM      %s = %s plus %s (%s)\n\r", script->number,
            get_param_text(script->param[0]), get_param_text(script->param[1]),
            get_param_text(script->param[2]), script->text);
        break;

    case SCRIPT_SET_INT_WAR_STATUS:
        sprintf(buf, "[%d] SYS SET_INT_WAR_STATUS integer: %s (%s)\r\n",
            script->number, get_param_text(script->param[0]), script->text);
        break;
    case SCRIPT_SET_INT_VALUE:
        sprintf(buf, "[%d] SYS SET_INT_VALUE    integer: %s, value: %d (%s)\n\r", script->number,
            get_param_text(script->param[1]), script->param[0], script->text);
        break;

    case SCRIPT_TELEPORT_CHAR:
        sprintf(buf, "[%d] SYS TELEPORT_CHAR    to room: %d, character: %s (%s)\n\r", script->number,
            script->param[0], get_param_text(script->param[1]), script->text);
        break;

    case SCRIPT_TELEPORT_CHAR_X:
        sprintf(buf, "[%d] SYS TELEPORT_CHAR_X  to room: %d, character: %s (%s)\n\r", script->number,
            script->param[0], get_param_text(script->param[1]), script->text);
        break;

    case SCRIPT_TELEPORT_CHAR_XL:
        sprintf(buf, "[%d] SYS TELEPORT_CHAR_XL room of: %s, character: %s (%s)\n\r", script->number,
            get_param_text(script->param[0]), get_param_text(script->param[1]), script->text);
        break;

    default:
        sprintf(buf, "[%d] ERROR: unknown command type\n\r", script->number);
    } // switch
    send_to_char(buf, ch);
}

// /50 - list all commands in a script

void list_script(struct char_data* ch)
{
    script_data* tmpscript;

    tmpscript = SHAPE_SCRIPT(ch)->root;

    if (tmpscript)
        while (tmpscript) {
            show_command(ch, tmpscript);
            tmpscript = tmpscript->next;
        }
    else
        send_to_char("No commands in this script yet.\n\r", ch);
}

// NB Keeping much the same command structure as for shaping zones.

void list_help_script(struct char_data* ch)
{

    send_to_char("possible fields are:\n\r", ch);
    send_to_char("1 - show current command;\n\r", ch);
    send_to_char("3 - change current command;\n\r", ch);
    send_to_char("4 - change parameters of the current command;\n\r", ch);
    send_to_char("5 - set comment on current command;\n\r", ch);
    send_to_char("/3 asks /4 next, and /4 asks /5 for most commands.\n\r", ch);
    send_to_char("6 - select next command;\n\r", ch);
    send_to_char("7 - select previous command;\n\r", ch);
    send_to_char("8 - select a command by number;\n\r", ch);
    send_to_char("9 - remove current command;\n\r", ch);
    send_to_char("10 - insert new command after the current one;\n\r", ch);
    send_to_char("11 - insert new command before the current one;\n\r", ch);
    send_to_char("13 - switch the current and the next commands;\n\r", ch);
    send_to_char("\n\r", ch);

    send_to_char("20 - change script name\n\r", ch);
    send_to_char("21 - change script description\n\r", ch);
    send_to_char("50 - list;\n\r", ch);
    send_to_char("51 - show script name and description;\n\r", ch);

    return;
}

// Renumber commands starting with 1

void renum_commands(struct script_data* script)
{
    int num;
    struct script_data* tmpscript;

    num = 0;
    tmpscript = script;
    while (tmpscript) {
        num++;
        tmpscript->number = num;
        tmpscript = tmpscript->next;
    }
}

void check_script_syntax(struct char_data* ch)
{

    // add checks for: unterminated scripts, untriggered sections, unconditional use of conditional commands
}

void extra_coms_script(struct char_data* ch, char* argument)
{

    char str[255], str2[50];
    int room_number, comm_key, zonnum;

    room_number = ch->in_room;

    if (SHAPE_SCRIPT(ch)->procedure == SHAPE_EDIT) {

        send_to_char("you invoked some rhymes from shapeless indefinity...\n\r", ch);
        comm_key = SHAPE_NONE;
        str[0] = 0;
        str2[0] = 0;
        sscanf(argument, "%s %s", str, str2);
        if (str[0] == 0)
            return;
        do {
            if (!strlen(str))
                strcpy(str, "weird");
            if (!strncmp(str, "free", strlen(str))) {
                comm_key = SHAPE_FREE;
                break;
            }
            if (!strncmp(str, "new", strlen(str))) {
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
            if (!strncmp(str, "done", strlen(str))) {
                comm_key = SHAPE_DONE;
                break;
            }
            if (!strncmp(str, "delete", strlen(str))) {
                comm_key = SHAPE_DELETE;
                break;
            }
            if (!strncmp(str, "simple", strlen(str))) {
                comm_key = SHAPE_MODE;
                break;
            }
            if (!strncmp(str, "implement", strlen(str))) {
                comm_key = SHAPE_IMPLEMENT;
                break;
            }
            if (!strncmp(str, "recalculate", strlen(str))) {
                comm_key = SHAPE_RECALCULATE;
                break;
            }
            send_to_char("Possible commands are:\n\r", ch);
            //      send_to_char("load   <script number #>;\n\r",ch);
            //      send_to_char("add    <zone #>;\n\r",ch);
            send_to_char("save - to save changes to the disk database;\n\r", ch);
            send_to_char("implement - applies changes to the game, leaving disk intact;\n\r", ch);
            send_to_char("done - to save your job, implement it and stop shaping;\n\r", ch);
            send_to_char("free - to stop shaping;\n\r", ch);
            return;
        } while (0);
    } else
        comm_key = SHAPE_SCRIPT(ch)->procedure;
    switch (comm_key) {
    case SHAPE_FREE:
        free_script(ch);
        send_to_char("You released the script and stopped shaping.\n\r", ch);
        break;

    case SHAPE_CREATE:
        /* World files are made outside the game; new picked last + 1. */
        shape_disabled(ch, "/", argument);
        break;
        if (str2[0] == 0) {
            send_to_char("Choose zone of script by 'new <zone_number>'.\n\r", ch);
            free_script(ch);
            break;
        }
        zonnum = atoi(str2);
        if (zonnum <= 0 || zonnum >= MAX_ZONES) {
            send_to_char("Weird script number. Aborted.\n\r", ch);
            free_script(ch);
            break;
        }
        SHAPE_SCRIPT(ch)
            ->permission
            = get_permission(zonnum, ch);
        sprintf(SHAPE_SCRIPT(ch)->f_from, SHAPE_SCRIPT_DIR, str2);
        sprintf(SHAPE_SCRIPT(ch)->f_old, SHAPE_SCRIPT_BACKDIR, str2);
        SET_BIT(SHAPE_SCRIPT(ch)->flags, SHAPE_FILENAME);
        new_script(ch);
        SET_BIT(SHAPE_SCRIPT(ch)->flags, SHAPE_SCRIPT_LOADED);
        SHAPE_SCRIPT(ch)
            ->procedure
            = SHAPE_EDIT;
        SHAPE_SCRIPT(ch)
            ->editflag
            = 49;
        send_to_char("OK. You created a new script. Do '/save' to assign a number to your script\n\r", ch);
        shape_center_script(ch, "");
        break;

    case SHAPE_LOAD:
        if (!IS_SET(SHAPE_SCRIPT(ch)->flags, SHAPE_SCRIPT_LOADED)) {
            if (load_script(ch, argument) < 0) {
                free_script(ch);
            }
        } else
            send_to_char("you already are working on something\n\r", ch);
        break;

    case SHAPE_SAVE:
        replace_script(ch, argument);
        break;

    case SHAPE_ADD:
        /* 'add <file>' could write into any file. */
        if (str2[0]) {
            shape_disabled(ch, "/", argument);
            break;
        }
        append_script(ch, argument);
        break;

    case SHAPE_DELETE:
        /* Rewrote the zone file to drop the script. */
        if (SHAPE_SCRIPT(ch)->procedure != SHAPE_DELETE) {
            shape_disabled(ch, "/", argument);
            break;
        }
        if (SHAPE_SCRIPT(ch)->procedure != SHAPE_DELETE) {
            send_to_char("You are about to remove this script from database.\n\r Are you sure? (type 'yes' to confirm:\n\r", ch);
            SHAPE_SCRIPT(ch)
                ->procedure
                = SHAPE_DELETE;
            SHAPE_SCRIPT(ch)
                ->position
                = ch->specials.position;
            ch->specials.position = POSITION_SHAPING;
            break;
        }
        while (*argument && (*argument <= ' '))
            argument++;
        if (!strcmp("yes", argument)) {
            SET_BIT(SHAPE_SCRIPT(ch)->flags, SHAPE_DELETE_ACTIVE);
            replace_script(ch, argument);
            send_to_char("You still continue to shape it, /free to exit.\n\r", ch);
        } else
            send_to_char("Deletion cancelled.\n\r", ch);
        REMOVE_BIT(SHAPE_SCRIPT(ch)->flags, SHAPE_DELETE_ACTIVE);
        SHAPE_SCRIPT(ch)
            ->procedure
            = SHAPE_EDIT;
        ch->specials.position = SHAPE_SCRIPT(ch)->position;
        break;

    case SHAPE_IMPLEMENT:
        implement_script(ch);
        SHAPE_SCRIPT(ch)
            ->procedure
            = SHAPE_EDIT;
        break;

    case SHAPE_DONE:
        /* A failed save must not throw the edits away. */
        if (replace_script(ch, argument) < 0) {
            send_to_char("Not saved - still shaping. Fix the problem and /done again,\n\r"
                         "or /free to discard.\n\r",
                ch);
            break;
        }
        implement_script(ch);
        extra_coms_script(ch, "free");
        break;
    }
    return;
}

#define SCRIPTDESCRCHANGE(line, addr)                                 \
    do {                                                              \
        if (!IS_SET(SHAPE_SCRIPT(ch)->flags, SHAPE_SIMPLE_ACTIVE)) {  \
            sprintf(tmpstr, "You are about to change %s:\n\r", line); \
            send_to_char(tmpstr, ch);                                 \
            SHAPE_SCRIPT(ch)                                          \
                ->position                                            \
                = shape_standup(ch, POSITION_SHAPING);                \
            ch->specials.prompt_number = 1;                           \
            SET_BIT(SHAPE_SCRIPT(ch)->flags, SHAPE_SIMPLE_ACTIVE);    \
            str[0] = 0;                                               \
            SHAPE_SCRIPT(ch)                                          \
                ->tmpstr                                              \
                = str_dup(addr);                                      \
            string_add_init(ch->desc, &(SHAPE_SCRIPT(ch)->tmpstr));   \
            return;                                                   \
        } else {                                                      \
            if (SHAPE_SCRIPT(ch)->tmpstr) {                           \
                addr = SHAPE_SCRIPT(ch)->tmpstr;                      \
                clean_text(addr);                                     \
            }                                                         \
            SHAPE_SCRIPT(ch)                                          \
                ->tmpstr                                              \
                = 0;                                                  \
            REMOVE_BIT(SHAPE_SCRIPT(ch)->flags, SHAPE_SIMPLE_ACTIVE); \
            shape_standup(ch, SHAPE_SCRIPT(ch)->position);            \
            ch->specials.prompt_number = 9;                           \
            SHAPE_SCRIPT(ch)                                          \
                ->editflag                                            \
                = 0;                                                  \
            continue;                                                 \
        }                                                             \
    } while (0);

#define SCRIPTLINECHANGE(line, addr)                                                        \
    do {                                                                                    \
        if (!IS_SET(SHAPE_SCRIPT(ch)->flags, SHAPE_DIGIT_ACTIVE)) {                         \
            sprintf(tmpstr, "Enter line %s:\n\r[%s]\n\r", line, (addr) ? (char*)addr : ""); \
            send_to_char(tmpstr, ch);                                                       \
            SHAPE_SCRIPT(ch)                                                                \
                ->position                                                                  \
                = shape_standup(ch, POSITION_SHAPING);                                      \
            ch->specials.prompt_number = 2;                                                 \
            SET_BIT(SHAPE_SCRIPT(ch)->flags, SHAPE_DIGIT_ACTIVE);                           \
            return;                                                                         \
        } else {                                                                            \
            str[0] = 0;                                                                     \
            if (!sscanf(arg, "%s", str)) {                                                  \
                SHAPE_SCRIPT(ch)                                                            \
                    ->editflag                                                              \
                    = 0;                                                                    \
                shape_standup(ch, SHAPE_SCRIPT(ch)->position);                              \
                ch->specials.prompt_number = 9;                                             \
                REMOVE_BIT(SHAPE_SCRIPT(ch)->flags, SHAPE_DIGIT_ACTIVE);                    \
                break;                                                                      \
            }                                                                               \
        }                                                                                   \
        if (str[0] != 0) {                                                                  \
            if (!strcmp(str, "%q")) {                                                       \
                send_to_char("Empty line set.\n\r", ch);                                    \
                arg[0] = 0;                                                                 \
            }                                                                               \
            RELEASE(addr);                                                                  \
            CREATE(addr, char, strlen(arg) + 1);                                            \
            strcpy(addr, arg);                                                              \
            itmp[1] = strlen(addr);                                                         \
            for (itmp[0] = 0; itmp[0] < itmp[1]; itmp[0]++) {                               \
                if (addr[itmp[0]] == '#')                                                   \
                    addr[itmp[0]] = '+';                                                    \
                if (addr[itmp[0]] == '~')                                                   \
                    addr[itmp[0]] = '-';                                                    \
            }                                                                               \
        }                                                                                   \
        REMOVE_BIT(SHAPE_SCRIPT(ch)->flags, SHAPE_DIGIT_ACTIVE);                            \
        shape_standup(ch, SHAPE_SCRIPT(ch)->position);                                      \
        ch->specials.prompt_number = 9;                                                     \
        SHAPE_SCRIPT(ch)                                                                    \
            ->editflag                                                                      \
            = 0;                                                                            \
    } while (0);

#define SCRIPTREALDIGCHANGE(line, addr)                                  \
    do {                                                                 \
        if (!IS_SET(SHAPE_SCRIPT(ch)->flags, SHAPE_DIGIT_ACTIVE)) {      \
            sprintf(tmpstr, "Enter %s [%d]:\n\r", line, addr);           \
            send_to_char(tmpstr, ch);                                    \
            SHAPE_SCRIPT(ch)                                             \
                ->position                                               \
                = shape_standup(ch, POSITION_SHAPING);                   \
            ch->specials.prompt_number = 3;                              \
            SET_BIT(SHAPE_SCRIPT(ch)->flags, SHAPE_DIGIT_ACTIVE);        \
            return;                                                      \
        } else {                                                         \
            for (tmp1 = 0; arg[tmp1] && arg[tmp1] <= ' '; tmp1++)        \
                ;                                                        \
            if (!arg[tmp1])                                              \
                tmp1 = addr;                                             \
            else if (!sscanf(arg, "%d", &tmp1)) {                        \
                send_to_char("a number required. dropped\n\r", ch);      \
                SHAPE_SCRIPT(ch)                                         \
                    ->editflag                                           \
                    = 0;                                                 \
                shape_standup(ch, SHAPE_SCRIPT(ch)->position);           \
                ch->specials.prompt_number = 9;                          \
                REMOVE_BIT(SHAPE_SCRIPT(ch)->flags, SHAPE_DIGIT_ACTIVE); \
                return;                                                  \
            }                                                            \
        }                                                                \
        addr = tmp1;                                                     \
        shape_standup(ch, SHAPE_SCRIPT(ch)->position);                   \
        ch->specials.prompt_number = 9;                                  \
        REMOVE_BIT(SHAPE_SCRIPT(ch)->flags, SHAPE_DIGIT_ACTIVE);         \
        SHAPE_SCRIPT(ch)                                                 \
            ->editflag                                                   \
            = 0;                                                         \
    } while (0);

/* Command types offered at /3, grouped as the prompt lists them. */
static const char* script_type_groups[][2] = {
    { "Triggers:", "ON_BEFORE_ENTER ON_DAMAGE ON_DIE ON_DRINK ON_EAT ON_ENTER\n\r"
                   "  ON_EXAMINE_OBJECT ON_HEAR_SAY ON_HEAR_YELL ON_PULL ON_RECEIVE ON_WEAR" },
    { "Flow:    ", "BEGIN END END_ELSE_BEGIN ABORT RETURN_FALSE DO_WAIT" },
    { "Tests:   ", "IF_INT_EQUAL IF_INT_LESS IF_INT_GREATER IF_INT_TRUE IF_INT_FALSE\n\r"
                   "  IF_IS_NPC IF_ROOM_SUNLIT IF_STR_CONTAINS IF_STR_EQUAL" },
    { "Values:  ", "ASSIGN_EQ ASSIGN_INV ASSIGN_ROOM ASSIGN_STR SET_INT_VALUE\n\r"
                   "  SET_INT_SUM SET_INT_SUB SET_INT_MULT SET_INT_DIV SET_INT_RANDOM\n\r"
                   "  SET_INT_WAR_STATUS GAIN_EXP" },
    { "Actions: ", "DO_SAY DO_YELL DO_EMOTE DO_SOCIAL DO_DROP DO_GIVE DO_WEAR DO_REMOVE\n\r"
                   "  DO_HIT DO_FLEE DO_FOLLOW" },
    { "Create:  ", "LOAD_MOB LOAD_OBJ LOAD_OBJ_X EQUIP_CHAR EXTRACT_CHAR EXTRACT_OBJ\n\r"
                   "  RAW_KILL" },
    { "Move:    ", "OBJ_FROM_CHAR OBJ_TO_CHAR OBJ_FROM_ROOM OBJ_TO_ROOM TELEPORT_CHAR\n\r"
                   "  TELEPORT_CHAR_X TELEPORT_CHAR_XL CHANGE_EXIT_TO SET_EXIT_STATE" },
    { "Messages:", "SEND_TO_CHAR SEND_TO_ROOM SEND_TO_ROOM_X PAGE_ZONE_MAP" },
};

/* The name of a command type, found by asking get_command about each name
 * in the /3 list; "?" if it is not one of them. */
static const char* script_type_name(int type)
{
    static char word[40];
    unsigned int g;
    const char *p, *w;

    for (g = 0; g < sizeof(script_type_groups) / sizeof(script_type_groups[0]); g++)
        for (p = script_type_groups[g][1]; *p;) {
            while (*p && *p <= ' ')
                p++;
            for (w = p; *p > ' '; p++)
                ;
            if (p > w && p - w < (int)sizeof(word)) {
                memcpy(word, w, p - w);
                word[p - w] = 0;
                if (get_command(word) == type)
                    return word;
            }
        }
    return "?";
}

/* What /5 edits: the message for the SEND_TO commands, the text typed at /4
 * for the commands that have one, a note for everything else. */
static const char* script_text_label(int type)
{
    switch (type) {
    case SCRIPT_SEND_TO_CHAR:
    case SCRIPT_SEND_TO_ROOM:
    case SCRIPT_SEND_TO_ROOM_X:
        return "MESSAGE (%s = the text value, %% = a % sign) (blank = keep)";
    case SCRIPT_DO_SAY:
    case SCRIPT_DO_YELL:
    case SCRIPT_DO_EMOTE:
    case SCRIPT_DO_SOCIAL:
    case SCRIPT_ASSIGN_STR:
    case SCRIPT_IF_STR_CONTAINS:
    case SCRIPT_IF_STR_EQUAL:
        return "TEXT (same as the /4 text) (blank = keep, %q = empty)";
    default:
        return "COMMENT, shown in the list only (blank = keep, %q = empty)";
    }
}

static bool script_text_is_message(int type)
{
    return type == SCRIPT_SEND_TO_CHAR || type == SCRIPT_SEND_TO_ROOM || type == SCRIPT_SEND_TO_ROOM_X;
}

/* Parameter prompts.  kinds has one letter per value, stored from
 * param[first]: 'v' a variable (ch1, ob2, int1, ch1.room...), 's' a writable
 * text variable (str1-3, obN.name), 'n' a typed number (vnum, slot, pulses). */
static void script_show_params(struct char_data* ch, const char* label, int first, const char* kinds)
{
    char buf[1200], *p;
    char* name;
    int i, v;

    p = buf + sprintf(buf, "Enter %s\n\rCurrent:", label);
    for (i = 0; kinds[i]; i++) {
        v = SHAPE_SCRIPT(ch)->script->param[first + i];
        name = (kinds[i] != 'n') ? get_param_text(v) : 0;
        if (name)
            p += sprintf(p, " %s", name);
        else
            p += sprintf(p, " %d", v);
    }
    strcpy(p, "   (blank = keep)\n\r");
    send_to_char(buf, ch);
}

static bool script_is_number_word(const char* w)
{
    if (*w == '-' || *w == '+')
        w++;
    if (!*w)
        return false;
    for (; *w; w++)
        if (!isdigit(*w))
            return false;
    return true;
}

/* Reads a parameter answer.  Returns 1 if the values were stored, 0 for a
 * blank answer (values kept), -1 if it was refused (values kept). */
static int script_read_params(struct char_data* ch, char* arg, int first, const char* kinds)
{
    char word[50], lookup[50], buf[120];
    int vals[6], n, i, v, len;
    char *p, *w;

    n = strlen(kinds);
    for (p = arg; *p && *p <= ' '; p++)
        ;
    if (!*p)
        return 0;
    for (i = 0; i < n && *p; i++) {
        for (w = p; *p > ' '; p++)
            ;
        len = p - w;
        if (len > (int)sizeof(word) - 1)
            len = sizeof(word) - 1;
        memcpy(word, w, len);
        word[len] = 0;
        strcpy(lookup, word); /* get_parameter uppercases what it is given */
        v = get_parameter(lookup);
        if (kinds[i] == 'n') {
            if (!script_is_number_word(word)) {
                sprintf(buf, "Not a number: %s - dropped.\n\r", word);
                send_to_char(buf, ch);
                return -1;
            }
            v = atoi(word);
        } else if (!v) {
            if (!script_is_number_word(word)) {
                sprintf(buf, "Unknown value: %s - dropped.\n\r", word);
                send_to_char(buf, ch);
                return -1;
            }
            v = atoi(word);
        }
        if (kinds[i] == 's' && v != SCRIPT_PARAM_STR1 && v != SCRIPT_PARAM_STR2 && v != SCRIPT_PARAM_STR3
            && v != SCRIPT_PARAM_OB1_NAME && v != SCRIPT_PARAM_OB2_NAME && v != SCRIPT_PARAM_OB3_NAME) {
            /* Anything else crashed the row when it ran. */
            send_to_char("Must be str1-3 or obN.name. dropped.\n\r", ch);
            return -1;
        }
        vals[i] = v;
        while (*p && *p <= ' ')
            p++;
    }
    for (; i < n; i++)
        vals[i] = 0;
    for (i = 0; i < n; i++)
        SHAPE_SCRIPT(ch)->script->param[first + i] = vals[i];
    return 1;
}

/* A parameter prompt: shows the label and current values, then on the
 * answer stores them.  A refused answer ends any /3 /4 /5 chain. */
#define SCRIPTPARAMS(line, first, kinds)                                            \
    if (!IS_SET(SHAPE_SCRIPT(ch)->flags, SHAPE_DIGIT_ACTIVE)) {                     \
        SHAPE_SCRIPT(ch)->position = shape_standup(ch, POSITION_SHAPING);           \
        ch->specials.prompt_number = strspn(kinds, "n") == strlen(kinds) ? 3 : 2;   \
        script_show_params(ch, line, first, kinds);                                 \
        SET_BIT(SHAPE_SCRIPT(ch)->flags, SHAPE_DIGIT_ACTIVE);                       \
        return;                                                                     \
    } else {                                                                        \
        SHAPE_SCRIPT(ch)->position = shape_standup(ch, SHAPE_SCRIPT(ch)->position); \
        ch->specials.prompt_number = 9;                                             \
        REMOVE_BIT(SHAPE_SCRIPT(ch)->flags, SHAPE_DIGIT_ACTIVE);                    \
        SHAPE_SCRIPT(ch)->editflag = 0;                                             \
        if (script_read_params(ch, arg, first, kinds) < 0)                          \
            return;                                                                 \
    }

void shape_center_script(struct char_data* ch, char* arg)
{
    char str[MAX_STRING_LENGTH * 2];
    struct script_data* script;
    char tmpstr[1000];
    int tmp, itmp[8], tmp1, tmp2, i;
    char st1[50];
    char* ptr;
    script_data* tmpscript;

    script = SHAPE_SCRIPT(ch)->script;
    tmp = SHAPE_SCRIPT(ch)->procedure;
    if ((tmp != SHAPE_NONE) && (tmp != SHAPE_EDIT)) {
        send_to_char("mixed orders. aborted - better restart shaping.\n\r", ch);
        extra_coms_script(ch, arg);
        return;
    }
    if (tmp == SHAPE_NONE) {
        send_to_char("Enter any non-number for list of commands, 99 for list of editor commands:", ch);
        SHAPE_SCRIPT(ch)
            ->editflag
            = 0;
        REMOVE_BIT(SHAPE_SCRIPT(ch)->flags, SHAPE_SIMPLE_ACTIVE);
        return;
    }

    if (SHAPE_SCRIPT(ch)->editflag == 0) {
        sscanf(arg, "%s", str);
        if ((str[0] >= '0') && (str[0] <= '9')) {
            SHAPE_SCRIPT(ch)
                ->editflag
                = atoi(str);
            str[0] = 0;
            if (SHAPE_SCRIPT(ch)->editflag == 0) {
                list_help_script(ch);
                return;
            }
        } else {
            extra_coms_script(ch, arg);
            return;
        }
    }
    if (!IS_SET(SHAPE_SCRIPT(ch)->flags, SHAPE_SCRIPT_LOADED)) {
        send_to_char("You have no script to edit.\n\r", ch);
        SHAPE_SCRIPT(ch)
            ->editflag
            = 0;
        return;
    }
    if (IS_SET(SHAPE_SCRIPT(ch)->flags, SHAPE_CURRFLAG))
        SHAPE_SCRIPT(ch)
            ->cur_room
            = world[ch->in_room].number;

    while (SHAPE_SCRIPT(ch)->editflag) // big loop

        switch (SHAPE_SCRIPT(ch)->editflag) {

        case 1: // case 1: show current command
            if (SHAPE_SCRIPT(ch)->script) {
                if (SHAPE_SCRIPT(ch)->script->prev) {
                    sprintf(str, "Prev: ");
                    show_command(ch, SHAPE_SCRIPT(ch)->script->prev);
                } else
                    send_to_char("No previous command.\n\r", ch);

                sprintf(str, "Curr: ");
                show_command(ch, SHAPE_SCRIPT(ch)->script);

                if (SHAPE_SCRIPT(ch)->script->next) {
                    sprintf(str, "Next: ");
                    show_command(ch, SHAPE_SCRIPT(ch)->script->next);
                } else
                    send_to_char("No next command.\n\r", ch);
            } else
                send_to_char("No current command.\n\r", ch);
            SHAPE_SCRIPT(ch)
                ->editflag
                = 0;
            break;

        case 2: // case 2: Set Mask  needed?  probably
            SHAPE_SCRIPT(ch)
                ->editflag
                = 0;
            break;

        case 3: // case 3: Set Command
            if (!IS_SET(SHAPE_SCRIPT(ch)->flags, SHAPE_DIGIT_ACTIVE)) {
                for (i = 0; i < (int)(sizeof(script_type_groups) / sizeof(script_type_groups[0])); i++) {
                    sprintf(str, "%s %s\n\r", script_type_groups[i][0], script_type_groups[i][1]);
                    send_to_char(str, ch);
                }
                sprintf(str, "Enter COMMAND TYPE, full name e.g. DO_SAY [%s]:\n\r",
                    script_type_name(SHAPE_SCRIPT(ch)->script->command_type));
                send_to_char(str, ch);
                SET_BIT(SHAPE_SCRIPT(ch)->flags, SHAPE_DIGIT_ACTIVE);
                ch->specials.prompt_number = 2;
                SHAPE_SCRIPT(ch)
                    ->position
                    = shape_standup(ch, POSITION_SHAPING);
                return;
            } else {
                ch->specials.prompt_number = 7;
                shape_standup(ch, SHAPE_SCRIPT(ch)->position);
                /* Blank keeps the type; an unknown name used to store type 0,
                 * which stops the script when it runs. */
                if (sscanf(arg, "%49s", st1) != 1) {
                    REMOVE_BIT(SHAPE_SCRIPT(ch)->flags, SHAPE_DIGIT_ACTIVE);
                    SHAPE_SCRIPT(ch)->editflag = 0;
                    return;
                }
                for (ptr = st1; *ptr; ptr++)
                    *ptr = toupper(*ptr);
                if (!get_command(st1)) {
                    send_to_char("Unknown command type. dropped.\n\r", ch);
                    REMOVE_BIT(SHAPE_SCRIPT(ch)->flags, SHAPE_DIGIT_ACTIVE);
                    SHAPE_SCRIPT(ch)->editflag = 0;
                    return;
                }
            }

            SHAPE_SCRIPT(ch)
                ->script->command_type
                = get_command(st1);
            SHAPE_SCRIPT(ch)
                ->editflag
                = 4;

            // if error - editflag = 0;

            REMOVE_BIT(SHAPE_SCRIPT(ch)->flags, SHAPE_DIGIT_ACTIVE);

            break;

        case 4: // case 4: get if_flag and parameters etc

            switch (SHAPE_SCRIPT(ch)->script->command_type) {

            case ON_BEFORE_ENTER:
                SHAPE_SCRIPT(ch)
                    ->editflag
                    = 5;
                break;

            case ON_ENTER:
                SHAPE_SCRIPT(ch)
                    ->editflag
                    = 5;
                break;

            case ON_EXAMINE_OBJECT:
                SHAPE_SCRIPT(ch)
                    ->editflag
                    = 5;
                break;

            case ON_DIE:
                SHAPE_SCRIPT(ch)
                    ->editflag
                    = 5;
                break;

            case ON_DRINK:
                SHAPE_SCRIPT(ch)
                    ->editflag
                    = 5;
                break;

            case ON_EAT:
                SHAPE_SCRIPT(ch)
                    ->editflag
                    = 5;
                break;

            case ON_PULL:
                SHAPE_SCRIPT(ch)
                    ->editflag
                    = 5;
                break;

            case ON_RECEIVE:
                SHAPE_SCRIPT(ch)
                    ->editflag
                    = 5;
                break;

            case ON_WEAR:
                SHAPE_SCRIPT(ch)
                    ->editflag
                    = 5;
                break;

            case SCRIPT_ABORT:
                SHAPE_SCRIPT(ch)
                    ->editflag
                    = 5;
                break;

            case SCRIPT_ASSIGN_EQ:
                SCRIPTPARAMS("ASSIGN_EQ: character object-var slot found  e.g. ch1 ob1 16 int1\n\r"
                             "  0 light, 1-2 fingers, 3-4 neck, 5 body, 6 head, 7 legs, 8 feet, 9 hands,\n\r"
                             "  10 arms, 11 shield, 12 about, 13 waist, 14-15 wrists, 16 wield, 17 hold,\n\r"
                             "  18 back, 19-21 belt"
                             "\n\r"
                             "  (found = int1-3: 1 if worn there, 0 if not)",
                    0, "vvnv");
                SHAPE_SCRIPT(ch)
                    ->editflag
                    = 5;
                break;

            case SCRIPT_ASSIGN_INV:
                SCRIPTPARAMS("ASSIGN_INV: obj-vnum object-var character count  e.g. 5400 ob1 ch1 int1\n\r"
                             "  (count = int1-3: how many they carry)",
                    0, "nvvv");
                SHAPE_SCRIPT(ch)
                    ->editflag
                    = 0;
                break;

            case SCRIPT_ASSIGN_ROOM:
                SCRIPTPARAMS("ASSIGN_ROOM: obj-vnum object-var room count  e.g. 5400 ob1 ch1.room int1\n\r"
                             "  (count = int1-3: how many lie there)",
                    0, "nvvv");
                SHAPE_SCRIPT(ch)
                    ->editflag
                    = 0;
                break;

            case SCRIPT_ASSIGN_STR:
                SCRIPTPARAMS("ASSIGN_STR: str1-3, or obN.name to rename the object  e.g. str1", 0, "s");
                SHAPE_SCRIPT(ch)
                    ->editflag
                    = 49;
                break;

            case SCRIPT_BEGIN:
                SHAPE_SCRIPT(ch)
                    ->editflag
                    = 5;
                break;

            case SCRIPT_CHANGE_EXIT_TO:
                SCRIPTPARAMS("CHANGE_EXIT_TO: room direction destination-vnum  e.g. ch1.room 0 1120\n\r"
                             "  (0 n, 1 e, 2 s, 3 w, 4 u, 5 d; the exit must already exist)",
                    0, "vnn");
                SHAPE_SCRIPT(ch)
                    ->editflag
                    = 5;
                break;

            case SCRIPT_DO_DROP:
                SCRIPTPARAMS("DO_DROP: character object  e.g. ch1 ob1 (only if they carry it)", 0, "vv");
                SHAPE_SCRIPT(ch)
                    ->editflag
                    = 5;
                break;

            case SCRIPT_DO_EMOTE:
                SCRIPTPARAMS("DO_EMOTE: character  e.g. ch1", 0, "v");
                SHAPE_SCRIPT(ch)
                    ->editflag
                    = 42;
                break;

            case SCRIPT_DO_FLEE:
                SCRIPTPARAMS("DO_FLEE: character  e.g. ch1", 0, "v");
                SHAPE_SCRIPT(ch)
                    ->editflag
                    = 5;
                break;

            case SCRIPT_DO_FOLLOW:
                SCRIPTPARAMS("DO_FOLLOW: follower leader  e.g. ch2 ch1", 0, "vv");
                SHAPE_SCRIPT(ch)
                    ->editflag
                    = 5;
                break;

            case SCRIPT_DO_GIVE:
                SCRIPTPARAMS("DO_GIVE: giver receiver object  e.g. ch1 ch2 ob1\n\r"
                             "  (giver must carry it; the object variable is cleared after)",
                    0, "vvv");
                SHAPE_SCRIPT(ch)
                    ->editflag
                    = 5;
                break;

            case SCRIPT_DO_HIT:
                SCRIPTPARAMS("DO_HIT: attacker victim  e.g. ch1 ch2", 0, "vv");
                SHAPE_SCRIPT(ch)
                    ->editflag
                    = 5;
                break;

            case SCRIPT_DO_REMOVE:
                SCRIPTPARAMS("DO_REMOVE: character slot  e.g. ch1 16\n\r"
                             "  0 light, 1-2 fingers, 3-4 neck, 5 body, 6 head, 7 legs, 8 feet, 9 hands,\n\r"
                             "  10 arms, 11 shield, 12 about, 13 waist, 14-15 wrists, 16 wield, 17 hold,\n\r"
                             "  18 back, 19-21 belt",
                    0, "vn");
                SHAPE_SCRIPT(ch)
                    ->editflag
                    = 5;
                break;

            case SCRIPT_DO_SAY:
                SCRIPTLINECHANGE("TEXT to say (%s = the text value) (blank = keep)", SHAPE_SCRIPT(ch)->script->text);
                SHAPE_SCRIPT(ch)
                    ->editflag
                    = 41;
                break;

            case SCRIPT_DO_SOCIAL:
                SCRIPTPARAMS("DO_SOCIAL: character target(optional, same room)  e.g. ch1 ch2", 0, "vv");
                SHAPE_SCRIPT(ch)
                    ->editflag
                    = 44;
                break;

            case SCRIPT_DO_WAIT:
                SCRIPTPARAMS("DO_WAIT: pulses to wait (4 = 1 second)\n\r"
                             "  (ch1 waits. In mob scripts the script then goes on with only ch1 set;\n\r"
                             "   in object scripts the rest of the script does not run.)",
                    0, "n");
                SHAPE_SCRIPT(ch)
                    ->editflag
                    = 5;
                break;

            case SCRIPT_DO_WEAR:
                SCRIPTPARAMS("DO_WEAR: character object  e.g. ch1 ob1 (only if they carry it)", 0, "vv");
                SHAPE_SCRIPT(ch)
                    ->editflag
                    = 5;
                break;

            case SCRIPT_DO_YELL:
                SCRIPTLINECHANGE("TEXT to yell (%s = the text value) (blank = keep)", SHAPE_SCRIPT(ch)->script->text);
                SHAPE_SCRIPT(ch)
                    ->editflag
                    = 41;
                break;

            case SCRIPT_END:
                SHAPE_SCRIPT(ch)
                    ->editflag
                    = 5;
                break;

            case SCRIPT_END_ELSE_BEGIN:
                SHAPE_SCRIPT(ch)
                    ->editflag
                    = 5;
                break;

            case SCRIPT_EQUIP_CHAR:
                SCRIPTPARAMS("EQUIP_CHAR: up to 5 object vnums (0 = none)  e.g. 5400 5401 0 0 0", 1, "nnnnn");
                SHAPE_SCRIPT(ch)
                    ->editflag
                    = 48;
                break;

            case SCRIPT_EXTRACT_CHAR:
                SCRIPTPARAMS("EXTRACT_CHAR: mob to remove  e.g. ch2 (players are not removed)", 0, "v");
                SHAPE_SCRIPT(ch)
                    ->editflag
                    = 5;
                break;

            case SCRIPT_EXTRACT_OBJ:
                SCRIPTPARAMS("EXTRACT_OBJ: object to remove  e.g. ob1", 0, "v");
                SHAPE_SCRIPT(ch)
                    ->editflag
                    = 5;
                break;

            case SCRIPT_GAIN_EXP:
                SCRIPTPARAMS("GAIN_EXP: character amount  e.g. ch1 int1\n\r"
                             "  (variables only - int1-3, chN.level/.hit/.race/.exp/.rank, obN.vnum;\n\r"
                             "   the result must be int1-3 or chN.hit)",
                    0, "vv");
                SHAPE_SCRIPT(ch)
                    ->editflag
                    = 5;
                break;

            case SCRIPT_IF_INT_EQUAL:
                SCRIPTPARAMS("IF_INT_EQUAL: left right (is left = right?)  e.g. int1 ch1.level\n\r"
                             "  (variables only - int1-3, chN.level/.hit/.race/.exp/.rank, obN.vnum;\n\r"
                             "   the result must be int1-3 or chN.hit)"
                             "\n\r"
                             "  (a typed number stops the script)\n\r"
                             "  (true: the next row runs; false: it is skipped, or its BEGIN...END block)",
                    0, "vv");
                SHAPE_SCRIPT(ch)
                    ->editflag
                    = 5;
                break;

            case SCRIPT_IF_INT_LESS:
                SCRIPTPARAMS("IF_INT_LESS: left right (is left < right?)  e.g. int1 ch1.level\n\r"
                             "  (variables only - int1-3, chN.level/.hit/.race/.exp/.rank, obN.vnum;\n\r"
                             "   the result must be int1-3 or chN.hit)"
                             "\n\r"
                             "  (a typed number stops the script)\n\r"
                             "  (true: the next row runs; false: it is skipped, or its BEGIN...END block)",
                    0, "vv");
                SHAPE_SCRIPT(ch)
                    ->editflag
                    = 5;
                break;

            case SCRIPT_IF_INT_GREATER:
                SCRIPTPARAMS("IF_INT_GREATER: left right (is left > right?)  e.g. int1 ch1.level\n\r"
                             "  (variables only - int1-3, chN.level/.hit/.race/.exp/.rank, obN.vnum;\n\r"
                             "   the result must be int1-3 or chN.hit)"
                             "\n\r"
                             "  (a typed number stops the script)\n\r"
                             "  (true: the next row runs; false: it is skipped, or its BEGIN...END block)",
                    0, "vv");
                SHAPE_SCRIPT(ch)
                    ->editflag
                    = 5;
                break;

            case SCRIPT_IF_INT_TRUE:
                SCRIPTPARAMS("IF_INT_TRUE: variable (true = above 0)  e.g. int1\n\r"
                             "  (variables only - int1-3, chN.level/.hit/.race/.exp/.rank, obN.vnum;\n\r"
                             "   the result must be int1-3 or chN.hit)"
                             "\n\r"
                             "  (a typed number stops the script)\n\r"
                             "  (true: the next row runs; false: it is skipped, or its BEGIN...END block)",
                    0, "v");
                SHAPE_SCRIPT(ch)
                    ->editflag
                    = 5;
                break;

            case SCRIPT_IF_INT_FALSE:
                SCRIPTPARAMS("IF_INT_FALSE: variable (false = 0 or less)  e.g. int1\n\r"
                             "  (variables only - int1-3, chN.level/.hit/.race/.exp/.rank, obN.vnum;\n\r"
                             "   the result must be int1-3 or chN.hit)"
                             "\n\r"
                             "  (a typed number stops the script)\n\r"
                             "  (true: the next row runs; false: it is skipped, or its BEGIN...END block)",
                    0, "v");
                SHAPE_SCRIPT(ch)
                    ->editflag
                    = 5;
                break;

            case SCRIPT_IF_IS_NPC:
                SCRIPTPARAMS("IF_IS_NPC: character  e.g. ch2\n\r"
                             "  (true: the next row runs; false: it is skipped, or its BEGIN...END block)",
                    0, "v");
                SHAPE_SCRIPT(ch)
                    ->editflag
                    = 5;
                break;

            case SCRIPT_IF_ROOM_SUNLIT:
                SCRIPTPARAMS("IF_ROOM_SUNLIT: room  e.g. ch1.room\n\r"
                             "  (true: the next row runs; false: it is skipped, or its BEGIN...END block)",
                    0, "v");
                SHAPE_SCRIPT(ch)
                    ->editflag
                    = 5;
                break;

            case SCRIPT_IF_STR_CONTAINS:
                SCRIPTPARAMS("IF_STR_CONTAINS: text variable  e.g. str1 ch2.name\n\r"
                             "  (true: the next row runs; false: it is skipped, or its BEGIN...END block)",
                    0, "v");
                SHAPE_SCRIPT(ch)
                    ->editflag
                    = 47;
                break;

            case SCRIPT_IF_STR_EQUAL:
                SCRIPTPARAMS("IF_STR_EQUAL: text variable  e.g. str1\n\r"
                             "  (true: the next row runs; false: it is skipped, or its BEGIN...END block)",
                    0, "v");
                SHAPE_SCRIPT(ch)
                    ->editflag
                    = 47;
                break;

            case SCRIPT_LOAD_MOB:
                SCRIPTPARAMS("LOAD_MOB: mob-vnum character-var  e.g. 5400 ch2\n\r"
                             "  (it is nowhere until a TELEPORT_CHAR row places it)",
                    0, "nv");
                SHAPE_SCRIPT(ch)
                    ->editflag
                    = 5;
                break;

            case SCRIPT_LOAD_OBJ:
                SCRIPTPARAMS("LOAD_OBJ: obj-vnum object-var  e.g. 5400 ob1 (placed nowhere)", 0, "nv");
                SHAPE_SCRIPT(ch)
                    ->editflag
                    = 5;
                break;

            case SCRIPT_LOAD_OBJ_X:
                SCRIPTPARAMS("LOAD_OBJ_X: (unused) object-var  e.g. 1 ob2 (always copies ob1)", 0, "nv");
                SHAPE_SCRIPT(ch)
                    ->editflag
                    = 5;
                break;

            case SCRIPT_OBJ_FROM_CHAR:
                SCRIPTPARAMS("OBJ_FROM_CHAR: object character  e.g. ob1 ch1 (object is then nowhere)", 0, "vv");
                SHAPE_SCRIPT(ch)
                    ->editflag
                    = 5;
                break;

            case SCRIPT_OBJ_FROM_ROOM:
                SCRIPTPARAMS("OBJ_FROM_ROOM: object room  e.g. ob1 ch1.room", 0, "vv");
                SHAPE_SCRIPT(ch)
                    ->editflag
                    = 5;
                break;

            case SCRIPT_OBJ_TO_CHAR:
                SCRIPTPARAMS("OBJ_TO_CHAR: object character  e.g. ob1 ch1 (object must be nowhere)", 0, "vv");
                SHAPE_SCRIPT(ch)
                    ->editflag
                    = 5;
                break;

            case SCRIPT_OBJ_TO_ROOM:
                SCRIPTPARAMS("OBJ_TO_ROOM: object room  e.g. ob1 ch1.room", 0, "vv");
                SHAPE_SCRIPT(ch)
                    ->editflag
                    = 5;
                break;

            case SCRIPT_PAGE_ZONE_MAP:
                SCRIPTPARAMS("PAGE_ZONE_MAP: zone number (room vnum / 100)  e.g. 11", 1, "n");
                SHAPE_SCRIPT(ch)
                    ->editflag
                    = 46;
                break;

            case SCRIPT_RAW_KILL:
                SCRIPTPARAMS("RAW_KILL: character to kill  e.g. ch2", 0, "v");
                SHAPE_SCRIPT(ch)
                    ->editflag
                    = 5;
                break;

            case SCRIPT_RETURN_FALSE:
                SHAPE_SCRIPT(ch)
                    ->editflag
                    = 5;
                break;

            case SCRIPT_SEND_TO_CHAR:
                SCRIPTPARAMS("SEND_TO_CHAR: character text-value(optional)  e.g. ch1 ch2.name\n\r"
                             "  (the message itself is asked for next)",
                    0, "vv");
                SHAPE_SCRIPT(ch)
                    ->editflag
                    = 5;
                break;

            case SCRIPT_SEND_TO_ROOM:
                SCRIPTPARAMS("SEND_TO_ROOM: room text-value(optional)  e.g. ch1.room ch1.name\n\r"
                             "  (the message itself is asked for next)",
                    0, "vv");
                SHAPE_SCRIPT(ch)
                    ->editflag
                    = 5;
                break;

            case SCRIPT_SEND_TO_ROOM_X:
                SCRIPTPARAMS("SEND_TO_ROOM_X: room hidden-from text-value(optional)\n\r"
                             "  e.g. ch1.room ch1 ch1.name (the message itself is asked for next)",
                    0, "vvv");
                SHAPE_SCRIPT(ch)
                    ->editflag
                    = 5;
                break;

            case SCRIPT_SET_EXIT_STATE:
                SCRIPTPARAMS("SET_EXIT_STATE: room  e.g. ch1.room", 2, "v");
                SHAPE_SCRIPT(ch)
                    ->editflag
                    = 43;
                break;

            case SCRIPT_SET_INT_DIV:
                SCRIPTPARAMS("SET_INT_DIV: result first second (result = first / second)\n\r"
                             "  (variables only - int1-3, chN.level/.hit/.race/.exp/.rank, obN.vnum;\n\r"
                             "   the result must be int1-3 or chN.hit)",
                    0, "vvv");
                SHAPE_SCRIPT(ch)
                    ->editflag
                    = 5;
                break;

            case SCRIPT_SET_INT_MULT:
                SCRIPTPARAMS("SET_INT_MULT: result first second (result = first x second)\n\r"
                             "  (variables only - int1-3, chN.level/.hit/.race/.exp/.rank, obN.vnum;\n\r"
                             "   the result must be int1-3 or chN.hit)",
                    0, "vvv");
                SHAPE_SCRIPT(ch)
                    ->editflag
                    = 5;
                break;

            case SCRIPT_SET_INT_RANDOM:
                SCRIPTPARAMS("SET_INT_RANDOM: result low high (a random number from low to high)\n\r"
                             "  (variables only - int1-3, chN.level/.hit/.race/.exp/.rank, obN.vnum;\n\r"
                             "   the result must be int1-3 or chN.hit)",
                    0, "vvv");
                SHAPE_SCRIPT(ch)
                    ->editflag
                    = 5;
                break;

            case SCRIPT_SET_INT_SUB:
                SCRIPTPARAMS("SET_INT_SUB: result first second (result = first - second)\n\r"
                             "  (variables only - int1-3, chN.level/.hit/.race/.exp/.rank, obN.vnum;\n\r"
                             "   the result must be int1-3 or chN.hit)",
                    0, "vvv");
                SHAPE_SCRIPT(ch)
                    ->editflag
                    = 5;
                break;

            case SCRIPT_SET_INT_SUM:
                SCRIPTPARAMS("SET_INT_SUM: result first second  e.g. int1 int2 ch1.level\n\r"
                             "  (variables only - int1-3, chN.level/.hit/.race/.exp/.rank, obN.vnum;\n\r"
                             "   the result must be int1-3 or chN.hit)",
                    0, "vvv");
                SHAPE_SCRIPT(ch)
                    ->editflag
                    = 5;
                break;

            case SCRIPT_SET_INT_WAR_STATUS:
                SCRIPTPARAMS("SET_INT_WAR_STATUS: variable (1 = good side leads, -1 = evil, 0 = tied)\n\r"
                             "  (variables only - int1-3, chN.level/.hit/.race/.exp/.rank, obN.vnum;\n\r"
                             "   the result must be int1-3 or chN.hit)",
                    0, "v");
                SHAPE_SCRIPT(ch)
                    ->editflag
                    = 5;
                break;

            case SCRIPT_SET_INT_VALUE:
                SCRIPTPARAMS("SET_INT_VALUE: variable to set (int1-3 or chN.hit)  e.g. int1", 1, "v");
                SHAPE_SCRIPT(ch)
                    ->editflag
                    = 45;
                break;

            case SCRIPT_TELEPORT_CHAR:
                SCRIPTPARAMS("TELEPORT_CHAR: room-vnum character  e.g. 1120 ch1\n\r"
                             "  (room is a typed number; also moves NPC followers in the room)",
                    0, "nv");
                SHAPE_SCRIPT(ch)
                    ->editflag
                    = 5;
                break;

            case SCRIPT_TELEPORT_CHAR_X:
                SCRIPTPARAMS("TELEPORT_CHAR_X: room-vnum character  e.g. 1120 ch1 (a typed number)", 0, "nv");
                SHAPE_SCRIPT(ch)
                    ->editflag
                    = 5;
                break;

            case SCRIPT_TELEPORT_CHAR_XL:
                SCRIPTPARAMS("TELEPORT_CHAR_XL: room character  e.g. ch2.room ch1", 0, "vv");
                SHAPE_SCRIPT(ch)
                    ->editflag
                    = 5;
                break;

            default:
                SHAPE_SCRIPT(ch)
                    ->editflag
                    = 0;

            } // nested switch in 4 - get input for commands etc...

            break;

        case 41:
            SCRIPTPARAMS(SHAPE_SCRIPT(ch)->script->command_type == SCRIPT_DO_YELL
                    ? "DO_YELL: speaker text-value(optional)  e.g. ch1 ch2.name"
                    : "DO_SAY: speaker text-value(optional)  e.g. ch1 ch2.name",
                0, "vv");
            SHAPE_SCRIPT(ch)
                ->editflag
                = 0;
            break;

        case 42:
            SCRIPTLINECHANGE("EMOTE text (blank = keep)", SHAPE_SCRIPT(ch)->script->text);
            SHAPE_SCRIPT(ch)
                ->editflag
                = 0;
            break;

        case 43:
            SCRIPTPARAMS("SET_EXIT_STATE: direction state  e.g. 0 2\n\r"
                         "  (0 n 1 e 2 s 3 w 4 u 5 d; state 0 open, 1 closed, 2 locked)",
                0, "nn");
            SHAPE_SCRIPT(ch)
                ->editflag
                = 5;
            break;

        case 44:
            SCRIPTLINECHANGE("SOCIAL name, e.g. nod (blank = keep)", SHAPE_SCRIPT(ch)->script->text);
            SHAPE_SCRIPT(ch)
                ->editflag
                = 0;
            break;

        case 45:
            SCRIPTPARAMS("SET_INT_VALUE: the number to set it to  e.g. 10", 0, "n");
            SHAPE_SCRIPT(ch)
                ->editflag
                = 5;
            break;

        case 46:
            SCRIPTPARAMS("PAGE_ZONE_MAP: character to show it to  e.g. ch1", 0, "v");
            SHAPE_SCRIPT(ch)
                ->editflag
                = 5;
            break;

        case 47:
            SCRIPTLINECHANGE(SHAPE_SCRIPT(ch)->script->command_type == SCRIPT_IF_STR_EQUAL
                    ? "TEXT to compare with, any case (blank = keep)"
                    : "TEXT to look for, in CAPITALS (blank = keep)",
                SHAPE_SCRIPT(ch)->script->text);
            break;

        case 48:
            SCRIPTPARAMS("EQUIP_CHAR: character to equip  e.g. ch1", 0, "v");
            SHAPE_SCRIPT(ch)
                ->editflag
                = 0;
            break;

        case 49:
            SCRIPTLINECHANGE("TEXT to store (blank = keep)", SHAPE_SCRIPT(ch)->script->text);
            SHAPE_SCRIPT(ch)
                ->editflag
                = 0;
            break;
        case 5: // case 5: set comment
            /* An empty message sends nothing. */
            if (IS_SET(SHAPE_SCRIPT(ch)->flags, SHAPE_DIGIT_ACTIVE)
                && script_text_is_message(SHAPE_SCRIPT(ch)->script->command_type)
                && sscanf(arg, "%s", str) == 1 && !strcmp(str, "%q")) {
                send_to_char("The message can't be empty.\n\r", ch);
                arg[0] = 0;
            }
            SCRIPTLINECHANGE(script_text_label(SHAPE_SCRIPT(ch)->script->command_type), SHAPE_SCRIPT(ch)->script->text);
            break;

        case 6: // case 6: Choose next
            script = SHAPE_SCRIPT(ch)->script->next;
            while (script) {
                if (!SHAPE_SCRIPT(ch)->cur_room || (SHAPE_SCRIPT(ch)->cur_room == script->room))
                    break;
                script = script->next;
            }
            if (script) {
                SHAPE_SCRIPT(ch)
                    ->script
                    = script;
                send_to_char("Next command chosen:\n\r", ch);
                show_command(ch, SHAPE_SCRIPT(ch)->script);
            } else
                send_to_char("No next command.\n\r", ch);
            SHAPE_SCRIPT(ch)
                ->editflag
                = 0;
            break;

        case 7: // case 7: Choose previous command
            script = SHAPE_SCRIPT(ch)->script->prev;
            while (script) {
                if (!SHAPE_SCRIPT(ch)->cur_room || (SHAPE_SCRIPT(ch)->cur_room == script->room))
                    break;
                script = script->prev;
            }
            if (script) {
                SHAPE_SCRIPT(ch)
                    ->script
                    = script;
                send_to_char("Previous command chosen:\n\r", ch);
                show_command(ch, SHAPE_SCRIPT(ch)->script);
            } else
                send_to_char("No previous command.\n\r", ch);
            SHAPE_SCRIPT(ch)
                ->editflag
                = 0;
            break;

        case 8:
            /* itmp[2], not tmp1: the macro uses tmp1 while it reads, so a
             * blank answer used to give 0 ("Wrong command number"). */
            itmp[2] = SHAPE_SCRIPT(ch)->script->number;
            SCRIPTREALDIGCHANGE("row number", itmp[2]);
            tmp1 = itmp[2];
            for (script = SHAPE_SCRIPT(ch)->root; script; script = script->next)
                if (script->number == tmp1)
                    break;
            if (!script)
                send_to_char("Wrong command number.\n\r", ch);
            else {
                SHAPE_SCRIPT(ch)
                    ->script
                    = script;
                if (SHAPE_SCRIPT(ch)->cur_room && (SHAPE_SCRIPT(ch)->cur_room != script->room)) {
                    SHAPE_SCRIPT(ch)
                        ->cur_room
                        = script->room;
                    send_to_char("The 'current room' number changed.\n\r", ch);
                }
            }
            break;

        case 9: // Case 9: remove command
            SHAPE_SCRIPT(ch)
                ->editflag
                = 0;

            if (SHAPE_SCRIPT(ch)->root == SHAPE_SCRIPT(ch)->script && SHAPE_SCRIPT(ch)->script->next == 0) {
                send_to_char("You cannot delete the only command in the script!\n\r", ch);
                break;
            }

            if (SHAPE_SCRIPT(ch)->script->text)
                RELEASE(SHAPE_SCRIPT(ch)->script->text);
            if (SHAPE_SCRIPT(ch)->root == SHAPE_SCRIPT(ch)->script) { // ie the first command
                tmpscript = SHAPE_SCRIPT(ch)->script->next;
                SHAPE_SCRIPT(ch)
                    ->root
                    = SHAPE_SCRIPT(ch)->script->next;
                RELEASE(SHAPE_SCRIPT(ch)->script);
                SHAPE_SCRIPT(ch)
                    ->script
                    = tmpscript;
            } else {
                SHAPE_SCRIPT(ch)
                    ->script->prev->next
                    = SHAPE_SCRIPT(ch)->script->next;
                if (SHAPE_SCRIPT(ch)->script->next)
                    SHAPE_SCRIPT(ch)
                        ->script->next->prev
                        = SHAPE_SCRIPT(ch)->script->prev;
                else
                    SHAPE_SCRIPT(ch)
                        ->script->next
                        = 0;
                tmpscript = SHAPE_SCRIPT(ch)->script->prev;
                RELEASE(SHAPE_SCRIPT(ch)->script);
                SHAPE_SCRIPT(ch)
                    ->script
                    = tmpscript;
            }
            send_to_char("Command removed.\n\r", ch);
            renum_commands(SHAPE_SCRIPT(ch)->root);

            break;

        case 10: // Case 10: insert new command after current
            CREATE1(script, script_data);
            script->prev = SHAPE_SCRIPT(ch)->script;
            if (SHAPE_SCRIPT(ch)->script->next)
                (SHAPE_SCRIPT(ch)->script->next)->prev = script;
            script->next = SHAPE_SCRIPT(ch)->script->next;
            SHAPE_SCRIPT(ch)
                ->script->next
                = script;
            CREATE(script->text, char, 1);
            script->text[0] = 0;
            script->command_type = SCRIPT_COMMAND_NONE;
            SHAPE_SCRIPT(ch)
                ->script
                = SHAPE_SCRIPT(ch)->script->next;
            renum_commands(SHAPE_SCRIPT(ch)->root);
            send_to_char("New command added next to current, and selected.\n\r", ch);
            SHAPE_SCRIPT(ch)
                ->editflag
                = 0;
            break;

        case 11: // Case 11: insert new command before current
            CREATE1(script, script_data);
            bzero((char*)(script), sizeof(struct script_data));
            script->next = SHAPE_SCRIPT(ch)->script;
            script->prev = SHAPE_SCRIPT(ch)->script->prev;
            if (SHAPE_SCRIPT(ch)->script->prev)
                (SHAPE_SCRIPT(ch)->script->prev)->next = script;
            else {
                SHAPE_SCRIPT(ch)
                    ->root
                    = script;
                script->prev = 0;
            }
            SHAPE_SCRIPT(ch)
                ->script->prev
                = script;
            CREATE(script->text, char, 1);
            script->text[0] = 0;
            SHAPE_SCRIPT(ch)
                ->script
                = SHAPE_SCRIPT(ch)->script->prev;
            renum_commands(SHAPE_SCRIPT(ch)->root);
            send_to_char("New command added before current, and selected.\n\r", ch);
            SHAPE_SCRIPT(ch)
                ->editflag
                = 0;
            break;

        case 12: // Case 12: change current room number
            /* Left over from the zone editor: script rows have no room. */
            shape_disabled(ch, "/", arg);
            SHAPE_SCRIPT(ch)->editflag = 0;
            break;
            SCRIPTREALDIGCHANGE("'CURRENT ROOM' number", SHAPE_SCRIPT(ch)->cur_room);
            if (IS_SET(SHAPE_SCRIPT(ch)->flags, SHAPE_CURRFLAG)) {
                REMOVE_BIT(SHAPE_SCRIPT(ch)->flags, SHAPE_CURRFLAG);
                send_to_char("The auto 'current room' mode removed.\n\r", ch);
            }
            break;

        case 13: // Case 13: switch commands
            if (!SHAPE_SCRIPT(ch)->script->next) {
                send_to_char("There is no next command, not switched.\n\r", ch);
            } else {
                script = SHAPE_SCRIPT(ch)->script->next;
                if (script->next)
                    script->next->prev = SHAPE_SCRIPT(ch)->script;
                SHAPE_SCRIPT(ch)
                    ->script->next
                    = script->next;
                if (SHAPE_SCRIPT(ch)->script->prev)
                    SHAPE_SCRIPT(ch)
                        ->script->prev->next
                        = script;
                else
                    SHAPE_SCRIPT(ch)
                        ->root
                        = script;
                script->prev = SHAPE_SCRIPT(ch)->script->prev;
                script->next = SHAPE_SCRIPT(ch)->script;
                SHAPE_SCRIPT(ch)
                    ->script->prev
                    = script;
                SHAPE_SCRIPT(ch)
                    ->script
                    = script;
                send_to_char("Switched the current and the next commands,\n\rthe next command selected.\n\r", ch);
            }
            SHAPE_SCRIPT(ch)
                ->editflag
                = 0;
            break;

        case 14: // case 14: syntax check
            check_script_syntax(ch);
            SHAPE_SCRIPT(ch)
                ->editflag
                = 0;
            break;

        case 20: // case 20: change script name
            if (IS_SET(SHAPE_SCRIPT(ch)->flags, SHAPE_DIGIT_ACTIVE) && sscanf(arg, "%s", str) == 1
                && !strcmp(str, "%q")) {
                send_to_char("The script name can't be empty.\n\r", ch);
                arg[0] = 0;
            }
            SCRIPTLINECHANGE("SCRIPT NAME, a one-line title (blank = keep)", SHAPE_SCRIPT(ch)->name);
            break;

        case 21: //  case 21: change script description
            SCRIPTDESCRCHANGE("SCRIPT DESCRIPTION", SHAPE_SCRIPT(ch)->description);
            break;

        case 50: // case 50: list commands
            list_script(ch);
            SHAPE_SCRIPT(ch)
                ->editflag
                = 0;
            break;

        case 51: // case 51: print name and description
            sprintf(str, "Script #%d: %s\n\r",
                SHAPE_SCRIPT(ch)->number, SHAPE_SCRIPT(ch)->name);
            send_to_char(str, ch);
            send_to_char(SHAPE_SCRIPT(ch)->description, ch);
            SHAPE_SCRIPT(ch)
                ->editflag
                = 0;
            break;

        default:
            list_help_script(ch);
            SHAPE_SCRIPT(ch)
                ->editflag
                = 0;
            break;

        } // End long switch
    return;
}

int get_command(char* command)
{

    switch (*command) {

    case 'A':
        if (!strcmp(command, "ABORT"))
            return SCRIPT_ABORT;
        if (!strcmp(command, "ASSIGN_EQ"))
            return SCRIPT_ASSIGN_EQ;
        if (!strcmp(command, "ASSIGN_INV"))
            return SCRIPT_ASSIGN_INV;
        if (!strcmp(command, "ASSIGN_ROOM"))
            return SCRIPT_ASSIGN_ROOM;
        if (!strcmp(command, "ASSIGN_STR"))
            return SCRIPT_ASSIGN_STR;
        return 0;

    case 'B':
        if (!strcmp(command, "BEGIN"))
            return SCRIPT_BEGIN;
        return 0;

    case 'C':
        if (!strcmp(command, "CHANGE_EXIT_TO"))
            return SCRIPT_CHANGE_EXIT_TO;
        return 0;

    case 'D':
        if (!strcmp(command, "DO_DROP"))
            return SCRIPT_DO_DROP;
        if (!strcmp(command, "DO_EMOTE"))
            return SCRIPT_DO_EMOTE;
        if (!strcmp(command, "DO_FLEE"))
            return SCRIPT_DO_FLEE;
        if (!strcmp(command, "DO_FOLLOW"))
            return SCRIPT_DO_FOLLOW;
        if (!strcmp(command, "DO_GIVE"))
            return SCRIPT_DO_GIVE;
        if (!strcmp(command, "DO_HIT"))
            return SCRIPT_DO_HIT;
        if (!strcmp(command, "DO_REMOVE"))
            return SCRIPT_DO_REMOVE;
        if (!strcmp(command, "DO_SAY"))
            return SCRIPT_DO_SAY;
        if (!strcmp(command, "DO_SOCIAL"))
            return SCRIPT_DO_SOCIAL;
        if (!strcmp(command, "DO_WAIT"))
            return SCRIPT_DO_WAIT;
        if (!strcmp(command, "DO_WEAR"))
            return SCRIPT_DO_WEAR;
        if (!strcmp(command, "DO_YELL"))
            return SCRIPT_DO_YELL;
        return 0;

    case 'E':
        if (!strcmp(command, "END"))
            return SCRIPT_END;
        if (!strcmp(command, "END_ELSE_BEGIN"))
            return SCRIPT_END_ELSE_BEGIN;
        if (!strcmp(command, "EQUIP_CHAR"))
            return SCRIPT_EQUIP_CHAR;
        if (!strcmp(command, "EXTRACT_CHAR"))
            return SCRIPT_EXTRACT_CHAR;
        if (!strcmp(command, "EXTRACT_OBJ"))
            return SCRIPT_EXTRACT_OBJ;
        return 0;

    case 'G':
        if (!strcmp(command, "GAIN_EXP"))
            return SCRIPT_GAIN_EXP;
        return 0;

    case 'I':
        if (!strcmp(command, "IF_INT_EQUAL"))
            return SCRIPT_IF_INT_EQUAL;
        if (!strcmp(command, "IF_INT_LESS"))
            return SCRIPT_IF_INT_LESS;
        if (!strcmp(command, "IF_INT_GREATER"))
            return SCRIPT_IF_INT_GREATER;
        if (!strcmp(command, "IF_INT_TRUE"))
            return SCRIPT_IF_INT_TRUE;
        if (!strcmp(command, "IF_INT_FALSE"))
            return SCRIPT_IF_INT_FALSE;
        if (!strcmp(command, "IF_IS_NPC"))
            return SCRIPT_IF_IS_NPC;
        if (!strcmp(command, "IF_ROOM_SUNLIT"))
            return SCRIPT_IF_ROOM_SUNLIT;
        if (!strcmp(command, "IF_STR_CONTAINS"))
            return SCRIPT_IF_STR_CONTAINS;
        if (!strcmp(command, "IF_STR_EQUAL"))
            return SCRIPT_IF_STR_EQUAL;
        return 0;

    case 'L':
        if (!strcmp(command, "LOAD_MOB"))
            return SCRIPT_LOAD_MOB;
        if (!strcmp(command, "LOAD_OBJ"))
            return SCRIPT_LOAD_OBJ;
        if (!strcmp(command, "LOAD_OBJ_X"))
            return SCRIPT_LOAD_OBJ_X;
        return 0;

    case 'O':
        if (!strcmp(command, "OBJ_FROM_CHAR"))
            return SCRIPT_OBJ_FROM_CHAR;
        if (!strcmp(command, "OBJ_FROM_ROOM"))
            return SCRIPT_OBJ_FROM_ROOM;
        if (!strcmp(command, "OBJ_TO_CHAR"))
            return SCRIPT_OBJ_TO_CHAR;
        if (!strcmp(command, "OBJ_TO_ROOM"))
            return SCRIPT_OBJ_TO_ROOM;
        if (!strcmp(command, "ON_BEFORE_ENTER"))
            return ON_BEFORE_ENTER;
        if (!strcmp(command, "ON_DAMAGE"))
            return ON_DAMAGE;
        if (!strcmp(command, "ON_DIE"))
            return ON_DIE;
        if (!strcmp(command, "ON_DRINK"))
            return ON_DRINK;
        if (!strcmp(command, "ON_EAT"))
            return ON_EAT;
        if (!strcmp(command, "ON_ENTER"))
            return ON_ENTER;
        if (!strcmp(command, "ON_EXAMINE_OBJECT"))
            return ON_EXAMINE_OBJECT;
        if (!strcmp(command, "ON_HEAR_SAY"))
            return ON_HEAR_SAY;
        if (!strcmp(command, "ON_PULL"))
            return ON_PULL;
        if (!strcmp(command, "ON_RECEIVE"))
            return ON_RECEIVE;
        if (!strcmp(command, "ON_WEAR"))
            return ON_WEAR;
        if (!strcmp(command, "ON_HEAR_YELL"))
            return ON_HEAR_YELL;
        return 0;

    case 'P':
        if (!strcmp(command, "PAGE_ZONE_MAP"))
            return SCRIPT_PAGE_ZONE_MAP;
        return 0;

    case 'R':
        if (!strcmp(command, "RAW_KILL"))
            return SCRIPT_RAW_KILL;
        if (!strcmp(command, "RETURN_FALSE"))
            return SCRIPT_RETURN_FALSE;
        return 0;

    case 'S':
        if (!strcmp(command, "SEND_TO_CHAR"))
            return SCRIPT_SEND_TO_CHAR;
        if (!strcmp(command, "SEND_TO_ROOM"))
            return SCRIPT_SEND_TO_ROOM;
        if (!strcmp(command, "SEND_TO_ROOM_X"))
            return SCRIPT_SEND_TO_ROOM_X;
        if (!strcmp(command, "SET_INT_SUM"))
            return SCRIPT_SET_INT_SUM;
        if (!strcmp(command, "SET_INT_MULT"))
            return SCRIPT_SET_INT_MULT;
        if (!strcmp(command, "SET_INT_DIV"))
            return SCRIPT_SET_INT_DIV;
        if (!strcmp(command, "SET_INT_RANDOM"))
            return SCRIPT_SET_INT_RANDOM;
        if (!strcmp(command, "SET_INT_SUB"))
            return SCRIPT_SET_INT_SUB;
        if (!strcmp(command, "SET_INT_VALUE"))
            return SCRIPT_SET_INT_VALUE;
        if (!strcmp(command, "SET_INT_WAR_STATUS"))
            return SCRIPT_SET_INT_WAR_STATUS;
        if (!strcmp(command, "SET_EXIT_STATE"))
            return SCRIPT_SET_EXIT_STATE;
        return 0;

    case 'T':
        if (!strcmp(command, "TELEPORT_CHAR"))
            return SCRIPT_TELEPORT_CHAR;
        if (!strcmp(command, "TELEPORT_CHAR_X"))
            return SCRIPT_TELEPORT_CHAR_X;
        if (!strcmp(command, "TELEPORT_CHAR_XL"))
            return SCRIPT_TELEPORT_CHAR_XL;
        return 0;

    default:
        // log("Unknown command type: get_command");
        return 0;

    } // switch
}

int get_parameter(char* param)
{
    char* ptr;

    for (ptr = param; *ptr; ptr++)
        *ptr = toupper(*ptr);

    switch (*param) {

    case 'I':
        if (!strcmp(param, "INT1"))
            return SCRIPT_PARAM_INT1;
        if (!strcmp(param, "INT2"))
            return SCRIPT_PARAM_INT2;
        if (!strcmp(param, "INT3"))
            return SCRIPT_PARAM_INT3;

    case 'S':
        if (!strcmp(param, "STR1"))
            return SCRIPT_PARAM_STR1;
        if (!strcmp(param, "STR2"))
            return SCRIPT_PARAM_STR2;
        if (!strcmp(param, "STR3"))
            return SCRIPT_PARAM_STR3;

    case 'R':
        if (!strcmp(param, "RM1"))
            return SCRIPT_PARAM_RM1;
        if (!strcmp(param, "RM2"))
            return SCRIPT_PARAM_RM3;
        if (!strcmp(param, "RM3"))
            return SCRIPT_PARAM_RM3;

        if (!strcmp(param, "RM1.NAME"))
            return SCRIPT_PARAM_RM1_NAME;
        if (!strcmp(param, "RM2.NAME"))
            return SCRIPT_PARAM_RM2_NAME;
        if (!strcmp(param, "RM3.NAME"))
            return SCRIPT_PARAM_RM3_NAME;

    case 'O':
        if (!strcmp(param, "OB1"))
            return SCRIPT_PARAM_OB1;
        if (!strcmp(param, "OB2"))
            return SCRIPT_PARAM_OB2;
        if (!strcmp(param, "OB3"))
            return SCRIPT_PARAM_OB3;

        if (!strcmp(param, "OB1.NAME"))
            return SCRIPT_PARAM_OB1_NAME;
        if (!strcmp(param, "OB2.NAME"))
            return SCRIPT_PARAM_OB2_NAME;
        if (!strcmp(param, "OB3.NAME"))
            return SCRIPT_PARAM_OB3_NAME;

        if (!strcmp(param, "OB1.VNUM"))
            return SCRIPT_PARAM_OB1_VNUM;
        if (!strcmp(param, "OB2.VNUM"))
            return SCRIPT_PARAM_OB2_VNUM;
        if (!strcmp(param, "OB3.VNUM"))
            return SCRIPT_PARAM_OB3_VNUM;

    case 'C':

        if (!strcmp(param, "CH1"))
            return SCRIPT_PARAM_CH1;
        if (!strcmp(param, "CH2"))
            return SCRIPT_PARAM_CH2;
        if (!strcmp(param, "CH3"))
            return SCRIPT_PARAM_CH3;

        if (!strcmp(param, "CH1.EXP"))
            return SCRIPT_PARAM_CH1_EXP;
        if (!strcmp(param, "CH2.EXP"))
            return SCRIPT_PARAM_CH2_EXP;
        if (!strcmp(param, "CH3.EXP"))
            return SCRIPT_PARAM_CH3_EXP;

        if (!strcmp(param, "CH1.HIT"))
            return SCRIPT_PARAM_CH1_HIT;
        if (!strcmp(param, "CH2.HIT"))
            return SCRIPT_PARAM_CH2_HIT;
        if (!strcmp(param, "CH3.HIT"))
            return SCRIPT_PARAM_CH3_HIT;

        if (!strcmp(param, "CH1.LEVEL"))
            return SCRIPT_PARAM_CH1_LEVEL;
        if (!strcmp(param, "CH2.LEVEL"))
            return SCRIPT_PARAM_CH2_LEVEL;
        if (!strcmp(param, "CH3.LEVEL"))
            return SCRIPT_PARAM_CH3_LEVEL;

        if (!strcmp(param, "CH1.NAME"))
            return SCRIPT_PARAM_CH1_NAME;
        if (!strcmp(param, "CH2.NAME"))
            return SCRIPT_PARAM_CH2_NAME;
        if (!strcmp(param, "CH3.NAME"))
            return SCRIPT_PARAM_CH3_NAME;

        if (!strcmp(param, "CH1.RACE"))
            return SCRIPT_PARAM_CH1_RACE;
        if (!strcmp(param, "CH2.RACE"))
            return SCRIPT_PARAM_CH2_RACE;
        if (!strcmp(param, "CH3.RACE"))
            return SCRIPT_PARAM_CH3_RACE;
        if (!strcmp(param, "CH1.ROOM"))
            return SCRIPT_PARAM_CH1_ROOM;
        if (!strcmp(param, "CH2.ROOM"))
            return SCRIPT_PARAM_CH2_ROOM;
        if (!strcmp(param, "CH3.ROOM"))
            return SCRIPT_PARAM_CH3_ROOM;

        if (!strcmp(param, "CH1.RANK"))
            return SCRIPT_PARAM_CH1_RANK;
        if (!strcmp(param, "CH2.RANK"))
            return SCRIPT_PARAM_CH2_RANK;
        if (!strcmp(param, "CH3.RANK"))
            return SCRIPT_PARAM_CH3_RANK;

    default:
        return 0;
    } // switch * param
}

char* get_param_text(int param)
{
    switch (param) {
    case SCRIPT_PARAM_STR1:
        return "str1";
    case SCRIPT_PARAM_STR2:
        return "str2";
    case SCRIPT_PARAM_STR3:
        return "str3";
    case SCRIPT_PARAM_INT1:
        return "int1";
    case SCRIPT_PARAM_INT2:
        return "int2";
    case SCRIPT_PARAM_INT3:
        return "int3";
    case SCRIPT_PARAM_CH1:
        return "ch1";
    case SCRIPT_PARAM_CH2:
        return "ch2";
    case SCRIPT_PARAM_CH3:
        return "ch3";
    case SCRIPT_PARAM_OB1:
        return "ob1";
    case SCRIPT_PARAM_OB2:
        return "ob2";
    case SCRIPT_PARAM_OB3:
        return "ob3";
    case SCRIPT_PARAM_RM1:
        return "rm1";
    case SCRIPT_PARAM_RM2:
        return "rm2";
    case SCRIPT_PARAM_RM3:
        return "rm3";

    case SCRIPT_PARAM_OB1_NAME:
        return "ob1.name";
    case SCRIPT_PARAM_OB2_NAME:
        return "ob2.name";
    case SCRIPT_PARAM_OB3_NAME:
        return "ob3.name";

    case SCRIPT_PARAM_OB1_VNUM:
        return "ob1.vnum";
    case SCRIPT_PARAM_OB2_VNUM:
        return "ob2.vnum";
    case SCRIPT_PARAM_OB3_VNUM:
        return "ob3.vnum";

    case SCRIPT_PARAM_CH1_NAME:
        return "ch1.name";
    case SCRIPT_PARAM_CH2_NAME:
        return "ch2.name";
    case SCRIPT_PARAM_CH3_NAME:
        return "ch3.name";
    case SCRIPT_PARAM_CH1_LEVEL:
        return "ch1.level";
    case SCRIPT_PARAM_CH2_LEVEL:
        return "ch2.level";
    case SCRIPT_PARAM_CH3_LEVEL:
        return "ch3.level";
    case SCRIPT_PARAM_CH1_HIT:
        return "ch1.hit";
    case SCRIPT_PARAM_CH2_HIT:
        return "ch2.hit";
    case SCRIPT_PARAM_CH3_HIT:
        return "ch3.hit";
    case SCRIPT_PARAM_CH1_RACE:
        return "ch1.race";
    case SCRIPT_PARAM_CH2_RACE:
        return "ch2.race";
    case SCRIPT_PARAM_CH3_RACE:
        return "ch3.race";
    case SCRIPT_PARAM_CH1_RANK:
        return "ch1.rank";
    case SCRIPT_PARAM_CH2_RANK:
        return "ch2.rank";
    case SCRIPT_PARAM_CH3_RANK:
        return "ch3.rank";
    case SCRIPT_PARAM_CH1_ROOM:
        return "ch1.room";
    case SCRIPT_PARAM_CH2_ROOM:
        return "ch2.room";
    case SCRIPT_PARAM_CH3_ROOM:
        return "ch3.room";
    case SCRIPT_PARAM_CH1_EXP:
        return "ch1.exp";
    case SCRIPT_PARAM_CH2_EXP:
        return "ch2.exp";
    case SCRIPT_PARAM_CH3_EXP:
        return "ch3.exp";

    case SCRIPT_PARAM_RM1_NAME:
        return "rm1.name";
    case SCRIPT_PARAM_RM2_NAME:
        return "rm2.name";
    case SCRIPT_PARAM_RM3_NAME:
        return "rm3.name";
    default:
        return 0;
    }
}
