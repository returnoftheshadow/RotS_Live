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
    if (2 != sscanf("%s %s", str, fname)) {
        if (!IS_SET(SHAPE_SCRIPT(ch)->flags, SHAPE_FILENAME)) {
            send_to_char("No file defined to write into. Use 'add <filename>\n\r'", ch);
            return -1;
        }
    } else {
        sprintf(SHAPE_SCRIPT(ch)->f_from, SHAPE_SCRIPT_DIR, fname);
        sprintf(SHAPE_SCRIPT(ch)->f_old, SHAPE_SCRIPT_BACKDIR, fname);
        SET_BIT(SHAPE_SCRIPT(ch)->flags, SHAPE_FILENAME);
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
        sprintf(buf, "[%d] SYS LOAD_OBJ_X       object: %s, obj variable: %s (%s)\n\r", script->number,
            get_param_text(script->param[1]), get_param_text(script->param[1]), script->text);
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
    send_to_char("2 - set mask for list of commands;\n\r", ch); // maybe??
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
    send_to_char("13 - switch the current and the next commands;\n\r", ch);
    send_to_char("14 - perform syntax check\n\r\n\r", ch);

    send_to_char("20 - change script name\n\r", ch);
    send_to_char("21 - change script description\n\r", ch);
    send_to_char("50 - list;\n\r", ch);

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
            send_to_char("new <script_number> - to create a new script;\n\r", ch);
            //      send_to_char("load   <script number #>;\n\r",ch);
            //      send_to_char("add    <zone #>;\n\r",ch);
            send_to_char("save  [script #]- to save changes to the disk database;\n\r", ch);
            send_to_char("delete - to remove the loaded script from the disk database;\n\r", ch);
            send_to_char("implement - applies changes to the game, leaving disk prointact;\n\r", ch);
            send_to_char("edit  - edit it is;\n\r", ch);
            send_to_char("done - to save your job, implement it and stop shaping.;\n\r", ch);
            send_to_char("free - to stop shaping.;\n\r", ch);
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
        append_script(ch, argument);
        break;

    case SHAPE_DELETE:
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
        replace_script(ch, argument);
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

#define SCRIPTDIGITCHANGE(line, num)                                       \
    if (!IS_SET(SHAPE_SCRIPT(ch)->flags, SHAPE_DIGIT_ACTIVE)) {            \
        SHAPE_SCRIPT(ch)                                                   \
            ->position                                                     \
            = shape_standup(ch, POSITION_SHAPING);                         \
        ch->specials.prompt_number = 2;                                    \
        send_to_char(line, ch);                                            \
        SET_BIT(SHAPE_SCRIPT(ch)->flags, SHAPE_DIGIT_ACTIVE);              \
        return;                                                            \
    } else {                                                               \
        tmp2 = 0;                                                          \
        for (i = 0; i < num; i++) {                                        \
            while ((arg[tmp2] < '0') && (arg[tmp2] != '-') && (arg[tmp2])) \
                tmp2++;                                                    \
            SHAPE_SCRIPT(ch)                                               \
                ->script->param[i]                                         \
                = atoi(arg + tmp2);                                        \
            while ((arg[tmp2] > ' ') && (arg[tmp2]))                       \
                tmp2++;                                                    \
        }                                                                  \
    }                                                                      \
    shape_standup(ch, SHAPE_SCRIPT(ch)->position);                         \
    ch->specials.prompt_number = 9;                                        \
    REMOVE_BIT(SHAPE_SCRIPT(ch)->flags, SHAPE_DIGIT_ACTIVE);               \
    SHAPE_SCRIPT(ch)                                                       \
        ->editflag                                                         \
        = 0;

#define SCRIPTPARAMCHANGE(line, num)                             \
    if (!IS_SET(SHAPE_SCRIPT(ch)->flags, SHAPE_DIGIT_ACTIVE)) {  \
        SHAPE_SCRIPT(ch)                                         \
            ->position                                           \
            = shape_standup(ch, POSITION_SHAPING);               \
        ch->specials.prompt_number = 2;                          \
        send_to_char(line, ch);                                  \
        SET_BIT(SHAPE_SCRIPT(ch)->flags, SHAPE_DIGIT_ACTIVE);    \
        return;                                                  \
    } else {                                                     \
        tmp1 = 0;                                                \
        tmp2 = 0;                                                \
        memset(input[0], 0, 50);                                 \
        memset(input[1], 0, 50);                                 \
        memset(input[2], 0, 50);                                 \
        memset(input[3], 0, 50);                                 \
        for (i = 0; i < num; i++) {                              \
            while ((arg[tmp2] != ' ') && (arg[tmp2]))            \
                tmp2++;                                          \
            memcpy(input[i], arg + tmp1, tmp2 - tmp1);           \
            tmp1 = tmp2 + 1;                                     \
            while ((arg[tmp2] == ' ') && (arg[tmp2]))            \
                tmp2++;                                          \
            if (!(tmp2 < (int)strlen(arg)))                      \
                break;                                           \
        }                                                        \
        for (i = 0; i < num; i++)                                \
            if (get_parameter(input[i]))                         \
                SHAPE_SCRIPT(ch)                                 \
                    ->script->param[i]                           \
                    = get_parameter(input[i]);                   \
            else                                                 \
                SHAPE_SCRIPT(ch)                                 \
                    ->script->param[i]                           \
                    = atoi(input[i]);                            \
        SHAPE_SCRIPT(ch)                                         \
            ->position                                           \
            = shape_standup(ch, SHAPE_SCRIPT(ch)->position);     \
        ch->specials.prompt_number = 9;                          \
        REMOVE_BIT(SHAPE_SCRIPT(ch)->flags, SHAPE_DIGIT_ACTIVE); \
        SHAPE_SCRIPT(ch)                                         \
            ->editflag                                           \
            = 0;                                                 \
    }

void shape_center_script(struct char_data* ch, char* arg)
{
    char str[MAX_STRING_LENGTH * 2];
    struct script_data* script;
    char tmpstr[1000];
    int tmp, itmp[8], tmp1, tmp2, i;
    char st1[50];
    char* ptr;
    char input[3][50];
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
                sprintf(str, "ENTER COMMAND TYPE <>:\n\r");
                send_to_char(str, ch);
                SET_BIT(SHAPE_SCRIPT(ch)->flags, SHAPE_DIGIT_ACTIVE);
                ch->specials.prompt_number = 2;
                SHAPE_SCRIPT(ch)
                    ->position
                    = shape_standup(ch, POSITION_SHAPING);
                return;
            } else {
                sscanf(arg, "%s", st1);
                ch->specials.prompt_number = 7;
                shape_standup(ch, SHAPE_SCRIPT(ch)->position);
                if (tmp == 0) {
                    send_to_char("Nothing entered. dropped.\n\r", ch);
                    SHAPE_SCRIPT(ch)
                        ->editflag
                        = 0;
                    return;
                }
            }
            for (ptr = st1; *ptr; ptr++)
                *ptr = toupper(*ptr);

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
                SCRIPTPARAMCHANGE("Enter character, object variable, position, and true/false integer (eg ch1 ob2 3 int1)", 4);
                SHAPE_SCRIPT(ch)
                    ->editflag
                    = 5;
                break;

            case SCRIPT_ASSIGN_INV:
                SCRIPTPARAMCHANGE("Enter vnum of object, the assignment variable (ob1),\n\r the inventory (ch1) and true/false integer (int1)", 4);
                SHAPE_SCRIPT(ch)
                    ->editflag
                    = 0;
                break;

            case SCRIPT_ASSIGN_ROOM:
                SCRIPTPARAMCHANGE("Enter vnum of object, the assignment variable (ob1),\n\r the room (rm1) and true/false integer (int1)", 4);
                SHAPE_SCRIPT(ch)
                    ->editflag
                    = 0;
                break;

            case SCRIPT_ASSIGN_STR:
                SCRIPTPARAMCHANGE("Enter the string to assign to (usually str1, str2 or str3): ", 1);
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
                SCRIPTPARAMCHANGE("Enter room, exit direction (0-5) and the room the exit is to lead to:", 3);
                SHAPE_SCRIPT(ch)
                    ->editflag
                    = 5;
                break;

            case SCRIPT_DO_DROP:
                SCRIPTPARAMCHANGE("Enter character and the object they are to drop (eg ch1 ob2)", 2);
                SHAPE_SCRIPT(ch)
                    ->editflag
                    = 5;
                break;

            case SCRIPT_DO_EMOTE:
                SCRIPTPARAMCHANGE("Enter character to emote", 1);
                SHAPE_SCRIPT(ch)
                    ->editflag
                    = 42;
                break;

            case SCRIPT_DO_FLEE:
                SCRIPTPARAMCHANGE("Enter character to flee", 1);
                SHAPE_SCRIPT(ch)
                    ->editflag
                    = 5;
                break;

            case SCRIPT_DO_FOLLOW:
                SCRIPTPARAMCHANGE("Enter character who is to follow and the character they are to follow:", 2);
                SHAPE_SCRIPT(ch)
                    ->editflag
                    = 5;
                break;

            case SCRIPT_DO_GIVE:
                SCRIPTPARAMCHANGE("Enter: character to give, character to receive and object (eg ch1 ch2 ob1)", 3);
                SHAPE_SCRIPT(ch)
                    ->editflag
                    = 5;
                break;

            case SCRIPT_DO_HIT:
                SCRIPTPARAMCHANGE("Enter attacker (ch1) and victim (ch2)", 2);
                SHAPE_SCRIPT(ch)
                    ->editflag
                    = 5;
                break;

            case SCRIPT_DO_REMOVE:
                SCRIPTPARAMCHANGE("Enter character and equipment position (eg ch1 16):", 2);
                SHAPE_SCRIPT(ch)
                    ->editflag
                    = 5;
                break;

            case SCRIPT_DO_SAY:
                SCRIPTLINECHANGE("Enter text to say. Include %s to insert a text parameter", SHAPE_SCRIPT(ch)->script->text);
                SHAPE_SCRIPT(ch)
                    ->editflag
                    = 41;
                break;

            case SCRIPT_DO_SOCIAL:
                SCRIPTPARAMCHANGE("Enter the character to perform the social, and their subject (eg ch1 ch2)", 2);
                SHAPE_SCRIPT(ch)
                    ->editflag
                    = 44;
                break;

            case SCRIPT_DO_WAIT:
                SCRIPTDIGITCHANGE("Enter the length of time to wait (4 = 1 second):", 1);
                SHAPE_SCRIPT(ch)
                    ->editflag
                    = 5;
                break;

            case SCRIPT_DO_WEAR:
                SCRIPTPARAMCHANGE("Enter character and object to wear (eg ch1, ob2):", 2);
                SHAPE_SCRIPT(ch)
                    ->editflag
                    = 5;
                break;

            case SCRIPT_DO_YELL:
                SCRIPTLINECHANGE("Enter text to say.  Include %s to insert a text field.", SHAPE_SCRIPT(ch)->script->text);
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
                SCRIPTDIGITCHANGE("Enter up to 5 vnums of objects with which to equip a character:", 5);
                SHAPE_SCRIPT(ch)
                    ->script->param[5]
                    = SHAPE_SCRIPT(ch)->script->param[0];
                SHAPE_SCRIPT(ch)
                    ->editflag
                    = 48;
                break;

            case SCRIPT_EXTRACT_CHAR:
                SCRIPTPARAMCHANGE("Enter character (can only be mobiles) to extract (eg ch1) - be careful!", 1);
                SHAPE_SCRIPT(ch)
                    ->editflag
                    = 5;
                break;

            case SCRIPT_EXTRACT_OBJ:
                SCRIPTPARAMCHANGE("Enter object to extract (eg ob1) - be careful!", 1);
                SHAPE_SCRIPT(ch)
                    ->editflag
                    = 5;
                break;

            case SCRIPT_GAIN_EXP:
                SCRIPTPARAMCHANGE("Enter character to gain/lose exp (eg ch1) and the integer gain/loss (eg int1)", 2);
                SHAPE_SCRIPT(ch)
                    ->editflag
                    = 5;
                break;

            case SCRIPT_IF_INT_EQUAL:
                SCRIPTPARAMCHANGE("Enter integers to compare (eg int1 ch1.level)", 2);
                SHAPE_SCRIPT(ch)
                    ->editflag
                    = 5;
                break;

            case SCRIPT_IF_INT_LESS:
                SCRIPTPARAMCHANGE("Enter integers to compare (eg int1 ch1.level)", 2);
                SHAPE_SCRIPT(ch)
                    ->editflag
                    = 5;
                break;

            case SCRIPT_IF_INT_GREATER:
                SCRIPTPARAMCHANGE("Enter integers to compare (eg int1 ch1.level)", 2);
                SHAPE_SCRIPT(ch)
                    ->editflag
                    = 5;
                break;

            case SCRIPT_IF_INT_TRUE:
                SCRIPTPARAMCHANGE("Enter integer to test (eg int1 ch1.level)", 2);
                SHAPE_SCRIPT(ch)
                    ->editflag
                    = 5;
                break;

            case SCRIPT_IF_INT_FALSE:
                SCRIPTPARAMCHANGE("Enter integer to test (eg int1 ch1.level)", 2);
                SHAPE_SCRIPT(ch)
                    ->editflag
                    = 5;
                break;

            case SCRIPT_IF_IS_NPC:
                SCRIPTPARAMCHANGE("Enter character to check if they are a mobile (eg ch1)", 1);
                SHAPE_SCRIPT(ch)
                    ->editflag
                    = 5;
                break;

            case SCRIPT_IF_ROOM_SUNLIT:
                SCRIPTPARAMCHANGE("Enter room to check if the sun is out (eg ch1.room)", 1);
                SHAPE_SCRIPT(ch)
                    ->editflag
                    = 5;
                break;

            case SCRIPT_IF_STR_CONTAINS:
                SCRIPTPARAMCHANGE("Enter main string variable which you want to check (eg ch1.name, str1)", 1);
                SHAPE_SCRIPT(ch)
                    ->editflag
                    = 47;
                break;

            case SCRIPT_IF_STR_EQUAL:
                SCRIPTPARAMCHANGE("Enter the main string variable which you want to check (eg ch1.name, str1)", 1);
                SHAPE_SCRIPT(ch)
                    ->editflag
                    = 47;
                break;

            case SCRIPT_LOAD_MOB:
                SCRIPTPARAMCHANGE("Enter vnum of mobile and character variable to assign to (eg ch2)", 2);
                SHAPE_SCRIPT(ch)
                    ->editflag
                    = 5;
                break;

            case SCRIPT_LOAD_OBJ:
                SCRIPTPARAMCHANGE("Enter vnum of object and object variable to assign to (eg 5400 ob1)", 2);
                SHAPE_SCRIPT(ch)
                    ->editflag
                    = 5;
                break;

            case SCRIPT_LOAD_OBJ_X:
                SCRIPTPARAMCHANGE("Enter object to copy and object variable to assign to (eg ob1.vnum ob2)", 2);
                SHAPE_SCRIPT(ch)
                    ->editflag
                    = 5;
                break;

            case SCRIPT_OBJ_FROM_CHAR:
                SCRIPTPARAMCHANGE("Enter object and character (eg ob1 ch1)", 2);
                SHAPE_SCRIPT(ch)
                    ->editflag
                    = 5;
                break;

            case SCRIPT_OBJ_FROM_ROOM:
                SCRIPTPARAMCHANGE("Enter object and room (eg ob1 rm1)", 2);
                SHAPE_SCRIPT(ch)
                    ->editflag
                    = 5;
                break;

            case SCRIPT_OBJ_TO_CHAR:
                SCRIPTPARAMCHANGE("Enter object and character (eg ob1 ch1)", 2);
                SHAPE_SCRIPT(ch)
                    ->editflag
                    = 5;
                break;

            case SCRIPT_OBJ_TO_ROOM:
                SCRIPTPARAMCHANGE("Enter object and room (eg ob1 rm1)", 2);
                SHAPE_SCRIPT(ch)
                    ->editflag
                    = 5;
                break;

            case SCRIPT_PAGE_ZONE_MAP:
                SCRIPTDIGITCHANGE("Enter zone number of the map you want to page:", 1);
                SHAPE_SCRIPT(ch)
                    ->script->param[1]
                    = SHAPE_SCRIPT(ch)->script->param[0];
                SHAPE_SCRIPT(ch)
                    ->editflag
                    = 46;
                break;

            case SCRIPT_RAW_KILL:
                SCRIPTPARAMCHANGE("Enter character/player to be killed (eg ch1):", 1);
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
                SCRIPTPARAMCHANGE("Enter character to send text to (eg ch1) (and optional text field)", 2);
                SHAPE_SCRIPT(ch)
                    ->editflag
                    = 5;
                break;

            case SCRIPT_SEND_TO_ROOM:
                SCRIPTPARAMCHANGE("Enter room to send text to (eg rm1) and optional text field (eg ch1.name)", 2);
                SHAPE_SCRIPT(ch)
                    ->editflag
                    = 5;
                break;

            case SCRIPT_SEND_TO_ROOM_X:
                SCRIPTPARAMCHANGE("Enter room to send text to (eg rm1), character not to see it (eg ch1) and optional text field", 3);
                SHAPE_SCRIPT(ch)
                    ->editflag
                    = 5;
                break;

            case SCRIPT_SET_EXIT_STATE:
                SCRIPTPARAMCHANGE("Enter room (eg rm1):", 1);
                SHAPE_SCRIPT(ch)
                    ->script->param[2]
                    = SHAPE_SCRIPT(ch)->script->param[0];
                SHAPE_SCRIPT(ch)
                    ->editflag
                    = 43;
                break;

            case SCRIPT_SET_INT_DIV:
                SCRIPTPARAMCHANGE("Enter the integer to assign to, the integer, and the integer it is to be divided by", 3);
                SHAPE_SCRIPT(ch)
                    ->editflag
                    = 5;
                break;

            case SCRIPT_SET_INT_MULT:
                SCRIPTPARAMCHANGE("Enter the integer to assign to and the two integers to multiply", 3);
                SHAPE_SCRIPT(ch)
                    ->editflag
                    = 5;
                break;

            case SCRIPT_SET_INT_RANDOM:
                SCRIPTPARAMCHANGE("Enter the integer to assign to, the lower integer and then the higher", 3);
                SHAPE_SCRIPT(ch)
                    ->editflag
                    = 5;
                break;

            case SCRIPT_SET_INT_SUB:
                SCRIPTPARAMCHANGE("Enter the integer to assign to, the first integer and the integer to subtract", 3);
                SHAPE_SCRIPT(ch)
                    ->editflag
                    = 5;
                break;

            case SCRIPT_SET_INT_SUM:
                SCRIPTPARAMCHANGE("Enter the integer to assign to and the two integers to add together", 3);
                SHAPE_SCRIPT(ch)
                    ->editflag
                    = 5;
                break;

            case SCRIPT_SET_INT_WAR_STATUS:
                SCRIPTPARAMCHANGE("Enter integer to set to the war status: (Should almost always be int1, int2 or int3):", 1);
                SHAPE_SCRIPT(ch)
                    ->editflag
                    = 5;
                break;

            case SCRIPT_SET_INT_VALUE:
                SCRIPTPARAMCHANGE("Enter integer parameter (eg, int1, ch1.hit - be careful!:", 1);
                SHAPE_SCRIPT(ch)
                    ->script->param[1]
                    = SHAPE_SCRIPT(ch)->script->param[0];
                SHAPE_SCRIPT(ch)
                    ->editflag
                    = 45;
                break;

            case SCRIPT_TELEPORT_CHAR:
                SCRIPTPARAMCHANGE("Enter room to teleport the character (and followers) to and the character:", 2);
                SHAPE_SCRIPT(ch)
                    ->editflag
                    = 5;
                break;

            case SCRIPT_TELEPORT_CHAR_X:
                SCRIPTPARAMCHANGE("Enter room num to teleport to and the character:", 2);
                SHAPE_SCRIPT(ch)
                    ->editflag
                    = 5;
                break;

            case SCRIPT_TELEPORT_CHAR_XL:
                SCRIPTPARAMCHANGE("Enter chX.room (ch1.room) to teleport to and the character:", 2);
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
            SCRIPTPARAMCHANGE("Enter: who to speak/yell (ch1) and optional parameter to insert (ch1.name)", 2);
            SHAPE_SCRIPT(ch)
                ->editflag
                = 0;
            break;

        case 42:
            SCRIPTLINECHANGE("Enter the text to emote", SHAPE_SCRIPT(ch)->script->text);
            SHAPE_SCRIPT(ch)
                ->editflag
                = 0;
            break;

        case 43:
            SCRIPTDIGITCHANGE("Enter exit direction (0-5) and door state (0-2)", 2);
            SHAPE_SCRIPT(ch)
                ->editflag
                = 5;
            break;

        case 44:
            SCRIPTLINECHANGE("Enter the social (text) to perform (eg, nod):", SHAPE_SCRIPT(ch)->script->text);
            SHAPE_SCRIPT(ch)
                ->editflag
                = 0;
            break;

        case 45:
            SCRIPTDIGITCHANGE("Enter integer value:", 1);
            SHAPE_SCRIPT(ch)
                ->editflag
                = 5;
            break;

        case 46:
            SCRIPTPARAMCHANGE("Enter the character which is to receive the map:", 1);
            SHAPE_SCRIPT(ch)
                ->editflag
                = 5;
            break;

        case 47:
            SCRIPTLINECHANGE("Enter the text you want to check for (in capitals)", SHAPE_SCRIPT(ch)->script->text);
            break;

        case 48:
            SCRIPTPARAMCHANGE("Enter the character to is to receive the equipment:", 1);
            SHAPE_SCRIPT(ch)
                ->editflag
                = 0;
            break;

        case 49:
            SCRIPTLINECHANGE("Enter the text you wish to store: ",
                SHAPE_SCRIPT(ch)->script->text);
            SHAPE_SCRIPT(ch)
                ->editflag
                = 0;
            break;
        case 5: // case 5: set comment
            SCRIPTLINECHANGE("COMMENT", SHAPE_SCRIPT(ch)->script->text);
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
            tmp1 = SHAPE_SCRIPT(ch)->script->number;
            SCRIPTREALDIGCHANGE("New command number:", tmp1);
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
            SCRIPTLINECHANGE("SCRIPT NAME", SHAPE_SCRIPT(ch)->name);
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
