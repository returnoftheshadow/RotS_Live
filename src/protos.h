/****************************************************************************/
/*     File protos.h                                   Part of CircleMUD    */
/*     Usage: descriptions for protos.c, for creating mobiles               */
/*     About copyrights - no idea                                           */
/****************************************************************************/

#ifndef PROTOS_H
#define PROTOS_H

#include <stdio.h>

#include "db.h" /* For the reset_com structure */
#include "platdef.h" /* For sh_int, ush_int, byte, etc. */

#define SHAPE_PROTOS 1
#define SHAPE_OBJECTS 2
#define SHAPE_ROOMS 3
#define SHAPE_ZONES 4
#define SHAPE_MUDLLES 5
#define SHAPE_RECALC_ALL 6
#define SHAPE_MASTER_MOBILE 7
#define SHAPE_MASTER_OBJECT 8
#define SHAPE_SCRIPTS 9

#define SHAPE_NONE 0
#define SHAPE_CREATE 1
#define SHAPE_EDIT 2
#define SHAPE_LOAD 3
#define SHAPE_SAVE 4
#define SHAPE_ADD 5
#define SHAPE_STOP 6
#define SHAPE_FREE 7
#define SHAPE_DELETE 8
#define SHAPE_SIMPLE_EDIT 9
#define SHAPE_IMPLEMENT 10
#define SHAPE_MODE 11
#define SHAPE_RECALCULATE 12
#define SHAPE_DONE 13
#define SHAPE_CURRENT 14

#define SHAPE_PROTO_LOADED 1
#define SHAPE_OBJECT_LOADED 1
#define SHAPE_ROOM_LOADED 1
#define SHAPE_ZONE_LOADED 1
#define SHAPE_MUDLLE_LOADED 1
#define SHAPE_SCRIPT_LOADED 1
#define SHAPE_SIMPLE_ACTIVE 2
#define SHAPE_DIGIT_ACTIVE 4
#define SHAPE_DELETE_ACTIVE 8
#define SHAPE_FILENAME 16
#define SHAPE_SIMPLEMODE 32
#define SHAPE_CHAIN 64
#define SHAPE_CURRFLAG 128

#define SHAPE_PROTO(ch) ((struct shape_proto*)((ch)->temp))
#define SHAPE_OBJECT(ch) ((struct shape_object*)((ch)->temp))
#define SHAPE_ROOM(ch) ((struct shape_room*)((ch)->temp))
#define SHAPE_ZONE(ch) ((struct shape_zone*)((ch)->temp))
#define SHAPE_MUDLLE(ch) ((struct shape_mudlle*)((ch)->temp))
#define SHAPE_SCRIPT(ch) ((struct shape_script*)((ch)->temp))

#define SHAPE_MOB_DIR "world/mob/%s.mob"
#define SHAPE_MOB_BACKDIR "world/mob/oldmobs/%s.mob"

#define SHAPE_OBJ_DIR "world/obj/%s.obj"
#define SHAPE_OBJ_BACKDIR "world/obj/oldobjs/%s.obj"

#define SHAPE_ROM_DIR "world/wld/%s.wld"
#define SHAPE_ROM_BACKDIR "world/wld/oldroms/%s.wld"

#define SHAPE_ZON_DIR "world/zon/%s.zon"
#define SHAPE_ZON_BACKDIR "world/zon/oldzons/%s.zon"

#define SHAPE_MDL_DIR "world/mdl/%s.mdl"
#define SHAPE_MDL_BACKDIR "world/mdl/oldmdls/%s.mdl"

#define SHAPE_SCRIPT_DIR "world/scr/%s.scr"
#define SHAPE_SCRIPT_BACKDIR "world/scr/oldscrs/%s.scr"

struct script_data; //  Declaration.  Definition follows below.

struct shape_proto {
    sh_int act; /* descriptor for what kind of struct is this */
    char mode; /* 'N', 'M', or whatthehell is this mob */
    struct char_data* proto; /* the proto to shape, to allocate here */
    sh_int procedure; /* which procedure is active now        */
    sh_int shift; /* for editor, how much was typed already */
    char editflag; /* for edit, which case is invoked      */
    sh_int flags; /* and flags they are                   */
    char f_from[80]; /* file descriptors for loading         */
    char f_old[80]; /* and saving mobiles                   */
    char* tmpstr;
    sh_int position;
    int number;
    int permission;
};
struct shape_object {
    sh_int act; /* descriptor for what kind of struct is this */
    struct obj_data* object; /* the proto to shape, to allocate here */
    sh_int procedure; /* which procedure is active now        */
    sh_int shift; /* for editor, how much was typed already */
    char editflag; /* for edit, which case is invoked      */
    sh_int flags; /* and flags they are                   */
    char f_from[80]; /* file descriptors for loading         */
    char f_old[80]; /* and saving mobiles                   */
    int basenum; /* number of object in database, or -1  */
    char* tmpstr;
    sh_int position;
    int number;
    int permission;
};

struct shape_room {
    sh_int act; /* descriptor for what kind of struct is this */
    struct room_data* room; /* the proto to shape, to allocate here */
    sh_int procedure; /* which procedure is active now        */
    sh_int shift; /* for editor, how much was typed already */
    char editflag; /* for edit, which case is invoked      */
    sh_int flags; /* and flags they are                   */
    char f_from[80]; /* file descriptors for loading         */
    char f_old[80]; /* and saving mobiles                   */
    sh_int exit_chosen; /* number of exit chosen for editing   */
    char* tmpstr;
    sh_int position;
    sh_int permission;
};
struct zone_tree {
    struct reset_com comm;
    int room;
    int number;
    struct zone_tree* next;
    struct zone_tree* prev;
    char* comment;
    sh_int permission;
};

struct shape_zone {
    sh_int act; /* descriptor for what kind of struct is this */
    struct zone_tree* root; /* the root of the loaded command record*/
    sh_int procedure; /* which procedure is active now        */
    sh_int shift; /* for editor, how much was typed already */
    char editflag; /* for edit, which case is invoked      */
    sh_int flags; /* and flags they are                   */
    char f_from[80]; /* file descriptors for loading         */
    char f_old[80]; /* and saving mobiles                   */
    struct zone_tree* curr; /* currently reviewed command   */
    char* zone_name;
    char* zone_descr;
    char* zone_map;
    int zone_number;
    int top;
    int lifespan;
    int reset_mode;
    struct owner_list* root_owner;
    int x, y, level;
    char symbol;
    struct reset_com mask; /* for masked listing */
    int cur_room; /* for 'in this room only' mode */
    int list_start; /* /52 list range, command numbers; 0 = all */
    int list_end; /* 0 = to the end */
    sh_int position;
    sh_int permission;
    char* tmpstr;
};

struct info_script { //  The structure which each char/obj/zon will have if it has a script.
    int index; //  Index number in the script_table of this script
    script_data* next_command; //  Command next to be executed.
    int str_dynamic[3]; // Set dynamic[x] to 1 if script allocated str[x]
    char* str[3]; //  General text field for holding variable text information, eg a character's name
    char_data* ch[3]; //  Variables
    obj_data* ob[3]; //  Variables
    int ints[3]; //  Variables
    room_data* rm[3]; //  Variables
};

struct script_head { //  The header structure for a linked list of scripts - forms the index for scripts.
    int number; //  Real script number.
    char* name; //  Name of the script
    int virt_num; //  Number in the index.
    char* description; //  Description of what the script does.  ** Saved to script file **
    int* host; //  Whether the script is for char, obj or room _data - the structure calling the script
    struct script_data* script; //  The first command in the script
};

struct script_data {
    int room; //  Room in which the command is executed (if needed)
    int number; //  Number of the command in the script.
    struct script_data* next; //  Next command in the script.
    struct script_data* prev; //  Previous command in the script
    int command_type; //  See script.h - type of command
    char* text; //  General text field - eg for do_say, send_to_room etc (also for comments)
    int param[6]; //  Parameters for command (if needed)  Refers to a char_script variable
};

struct shape_script {
    sh_int act; //  Type of thing being shaped - must be 1st field in structure
    int index_pos; //  Position in the script_table (-1 if not present)
    struct script_data* script; //  Current command in the script
    struct script_data* root; //  1st command in the script;
    sh_int position; //  Position of immortal shaping (standing, sitting etc)
    sh_int procedure;
    sh_int permission; //  Zone permission
    char f_from[80]; //  File
    char f_old[80]; //  Old file
    sh_int flags;
    int cur_room; //  Current room (if set)
    int list_start; //  /52 list range, command numbers; 0 = all
    int list_end; //  0 = to the end
    char editflag; //  Current shaping (internal) command
    char* name; //  Name of script
    char* description; //  Long description
    int number; //  vnum - ie zone + number in zone
    char* tmpstr; //  Like it says... a pointer to a temporary string
};

struct shape_mudlle {
    sh_int act;
    char* txt;
    int prog_num;
    int real_num;
    sh_int permission;
    char f_from[80]; /* file descriptors for loading         */
    char f_old[80]; /* and saving mobiles                   */
};

void shape_center_proto(struct char_data* ch, char* argument);
void shape_center_obj(struct char_data* ch, char* argument);
void shape_center_room(struct char_data* ch, char* argument);
void shape_center_zone(struct char_data* ch, char* argument);
void shape_center_mudlle(struct char_data* ch, char* argument);
void shape_center_script(struct char_data* ch, char* argument);
void shape_center(struct char_data* ch, char* argument);
int get_permission(int zonnum, struct char_data* ch, int mode = 0);
/* mode != 0 denies permission to "unlocked" zones */
void clean_text(char*); /* removes ~ and # from the string */
ACMD(do_shape);

/* /52 list range, shared by the zone and script editors (shapemob.cpp). */
void shape_range_prompt(struct char_data* ch, int start, int end);
void shape_range_set(struct char_data* ch, const char* arg, int* start, int* end);
bool shape_range_includes(int start, int end, int number);
void shape_range_footer(int start, int end, char* out);
int shape_list_begin(struct char_data* ch);
bool shape_list_fits(struct char_data* ch, const char* line, const char* footer, int* wrapped);
void shape_list_finish(struct char_data* ch, const char* footer, bool cut);

#endif /* PROTOS_H */
