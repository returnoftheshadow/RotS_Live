
/* File: script.h           Return of the Shadow script header file

   These defines are used in shaping and in executing scripts.  Each script_data object has an
   int command_type field which refers to the defines below.  The char_has_script function looks
   for the defines below to see if a character/object/room has a certain trigger.

   These defines are also used during execution of a script since execution cannot run from
   one trigger to the next in a script
*/

#ifndef SCRIPT_H
#define SCRIPT_H

#include "structs.h" /* For the char_data structure */

// defines: 1-9: flow control, 10-29: triggers, 29- script commands

inline constexpr int SCRIPT_BEGIN = 1; //  Marks the beginning of a block
inline constexpr int SCRIPT_END = 2; //  End block
inline constexpr int SCRIPT_ABORT = 3; //  Abort execution of the script (returns TRUE - program continues as normal)
inline constexpr int SCRIPT_IF_INT_EQUAL = 4; //  Tests to see whether or not two integers are equal
inline constexpr int SCRIPT_END_ELSE_BEGIN = 5; //	Ends a block.  If previous block was being executed then the next
//     block is skipped and vice versa.
inline constexpr int SCRIPT_RETURN_FALSE = 6; //  Halts execution of the script and returns false
//    Program acts on false depending on context eg:
//    a trigger on_die returning false will stop the character dying
//     nb, ABORT returns true - program continues as normal
inline constexpr int SCRIPT_IF_INT_LESS = 7; //  Test to see whether one integer is less than another
inline constexpr int SCRIPT_IF_IS_NPC = 8; //	Test if a character is a player or mobile
inline constexpr int SCRIPT_IF_STR_EQUAL = 9; //  Test if two strings are equal (regardless of case)
inline constexpr int SCRIPT_IF_STR_CONTAINS = 10; //  Test if a string contains another (regardless of case)

inline constexpr int ON_ENTER = 11; //  Signal the beginning of triggers in a script linked list
inline constexpr int ON_BEFORE_ENTER = 12; //  Trigger - if the return value is FALSE - character cannot enter
inline constexpr int ON_BEFORE_DIE = 13; // implemented??
inline constexpr int ON_DIE = 14; //  Trigger - if the return value is FALSE - character does not die
inline constexpr int ON_RECEIVE = 15; //  Trigger
inline constexpr int ON_EXAMINE_OBJECT = 16; //  Object trigger - sends text to character etc.
inline constexpr int ON_HEAR_SAY = 17; //  When a character hears another speak
inline constexpr int ON_DAMAGE = 18; //	When a character damages another.  Returns FALSE if the damage is not to be applied.
inline constexpr int ON_EAT = 19; //  When a character eats an object
inline constexpr int ON_DRINK = 20; //  When a character drinks from an object
inline constexpr int ON_WEAR = 21; //  When a character places an object into their equipment (return false and wear fails)
inline constexpr int ON_PULL = 22; //  When a character pulls a lever (return FALSE - lever is not pulled)
inline constexpr int ON_HEAR_YELL = 23; // When a character hears another yell
inline constexpr int ON_PATH_END = 24; // When a walker mob completes its route (mob_walker.cpp). Return value is ignored.

inline constexpr int SCRIPT_DO_SAY = 30; //  Script command do_say - similar to ACMD
inline constexpr int SCRIPT_ASSIGN_STR = 31; //  Assign the contents of param1 to script variable str1
inline constexpr int SCRIPT_SEND_TO_ROOM = 32; //  Send text to room
inline constexpr int SCRIPT_SEND_TO_ROOM_X = 33; //  Send text to room except param (ch)
inline constexpr int SCRIPT_DO_HIT = 34; //  Hit (param)
inline constexpr int SCRIPT_ASSIGN_INV = 35; //  Assign obj vnum from ch's inventory to obj
inline constexpr int SCRIPT_SEND_TO_CHAR = 36; //  Send text to character
inline constexpr int SCRIPT_DO_FLEE = 37; //  Force a character to flee
inline constexpr int SCRIPT_DO_EMOTE = 38; //  A character's emote
inline constexpr int SCRIPT_LOAD_MOB = 39; //  Load a mobile assign to a chx variable
inline constexpr int SCRIPT_TELEPORT_CHAR = 40; //  Take char_ and followers from_room then put char_to_room
inline constexpr int SCRIPT_DO_YELL = 41; //  Script command to yell
inline constexpr int SCRIPT_DO_FOLLOW = 42; //  One character to follow another
inline constexpr int SCRIPT_EXTRACT_CHAR = 43; //  Removes a character from the game (inventory is left in the room)
inline constexpr int SCRIPT_SET_EXIT_STATE = 44; //  Script interpretation of the function in db.cc
inline constexpr int SCRIPT_RAW_KILL = 45; //  Kills the character/player :(
inline constexpr int SCRIPT_DO_GIVE = 46; //  Makes a character give obx to another
inline constexpr int SCRIPT_LOAD_OBJ = 47; //  Load an object - note this object is not placed in the game
inline constexpr int SCRIPT_OBJ_TO_CHAR = 48; //  Use after load_obj: put object in a character's inventory
inline constexpr int SCRIPT_OBJ_TO_ROOM = 49; //  Use after load_obj: put object in a room's contents
inline constexpr int SCRIPT_CHANGE_EXIT_TO = 50; //  Change the room an exit goes to
inline constexpr int SCRIPT_EXTRACT_OBJ = 51; //	Extract an object from the world (can be from inventory, contents, room etc)
inline constexpr int SCRIPT_ASSIGN_EQ = 52; //	Assign an object from equipment position
inline constexpr int SCRIPT_DO_DROP = 53; //	Make a character drop an object
inline constexpr int SCRIPT_DO_REMOVE = 54; //	Make a character remove an object from equipment
inline constexpr int SCRIPT_DO_WEAR = 55; //	Make a character wear an object (if possible)
inline constexpr int SCRIPT_DO_SOCIAL = 56; //  Make a character perform a social command
inline constexpr int SCRIPT_SET_INT_VALUE = 57; //  Set a value to an integer
inline constexpr int SCRIPT_DO_WAIT = 58; //  For mobiles - put on the waiting_list for a while
inline constexpr int SCRIPT_PAGE_ZONE_MAP = 59; //  Pages a zone map to a character
inline constexpr int SCRIPT_SET_INT_SUM = 60; //  Add two ints together and assign the result
inline constexpr int SCRIPT_SET_INT_MULT = 61; //  As above with multiply
inline constexpr int SCRIPT_SET_INT_DIV = 62; //  As above with division
inline constexpr int SCRIPT_SET_INT_SUB = 63; //  As above with subtraction
inline constexpr int SCRIPT_GAIN_EXP = 64; //  Give/subtract exp to a player
inline constexpr int SCRIPT_TELEPORT_CHAR_X = 65; //  Take char_from_room then put char_to_room (leave followers behind)
inline constexpr int SCRIPT_OBJ_FROM_ROOM = 66; // Remove an object from a room.
inline constexpr int SCRIPT_OBJ_FROM_CHAR = 67; // Remove and object from a character's inventory
inline constexpr int SCRIPT_ASSIGN_ROOM = 68; // Assign an object in a room by vnum to obj pointer
inline constexpr int SCRIPT_SET_INT_RANDOM = 69; // Assign a random number between two values to an integer
inline constexpr int SCRIPT_EQUIP_CHAR = 70; // Load a number of objects to a char and then do wear_all
inline constexpr int SCRIPT_SET_INT_WAR_STATUS = 71; // Set an int to 1 if whities lead fame war, -1 if darkies lead fame war and 0 if the sides are equal
inline constexpr int SCRIPT_IF_INT_GREATER = 72; //  Test to see whether one integer is greater than another
inline constexpr int SCRIPT_IF_INT_TRUE = 73; //  Test to see whether one integer is greater than 0
inline constexpr int SCRIPT_IF_ROOM_SUNLIT = 74; // Check to see if there is sunlight in the room

inline constexpr int SCRIPT_TELEPORT_CHAR_XL = 75; //  Take char_from_room then put chX.room (leave followers behind)
inline constexpr int SCRIPT_IF_INT_FALSE = 76; //  Test to see whether one integer is less than 1
inline constexpr int SCRIPT_LOAD_OBJ_X = 77; //  Load an object from another object - note this object is not placed in the game
inline constexpr int SCRIPT_SECTION = 78; //  A title row for the editor's list: text is "title | comment", does nothing when run
inline constexpr int SCRIPT_RESTYLE_OBJ = 79; //  Change an object, in place, into another look of its family

inline constexpr int SCRIPT_COMMAND_NONE = 99; //  Given to new or unused commands
// 999 - reserved for script loading - do not use

// defines for script_data parameters - variables etc unless the macros in shapescript are to be changed, these
//   parameters -must- lie in the range 100-999

inline constexpr int SCRIPT_PARAM_STR1 = 950;
inline constexpr int SCRIPT_PARAM_STR2 = 951;
inline constexpr int SCRIPT_PARAM_STR3 = 952;

inline constexpr int SCRIPT_PARAM_INT1 = 953;
inline constexpr int SCRIPT_PARAM_INT2 = 954;
inline constexpr int SCRIPT_PARAM_INT3 = 955;

inline constexpr int SCRIPT_PARAM_CH1 = 100;
inline constexpr int SCRIPT_PARAM_CH2 = 200;
inline constexpr int SCRIPT_PARAM_CH3 = 300;

inline constexpr int SCRIPT_PARAM_OB1 = 400;
inline constexpr int SCRIPT_PARAM_OB2 = 500;
inline constexpr int SCRIPT_PARAM_OB3 = 600;

inline constexpr int SCRIPT_PARAM_RM1 = 700;
inline constexpr int SCRIPT_PARAM_RM2 = 800;
inline constexpr int SCRIPT_PARAM_RM3 = 900;

inline constexpr int SCRIPT_PARAM_RM1_NAME = 701;
inline constexpr int SCRIPT_PARAM_RM2_NAME = 702;
inline constexpr int SCRIPT_PARAM_RM3_NAME = 703;

inline constexpr int SCRIPT_PARAM_CH1_NAME = 101;
inline constexpr int SCRIPT_PARAM_CH2_NAME = 201;
inline constexpr int SCRIPT_PARAM_CH3_NAME = 301;

inline constexpr int SCRIPT_PARAM_CH1_ROOM = 102;
inline constexpr int SCRIPT_PARAM_CH2_ROOM = 202;
inline constexpr int SCRIPT_PARAM_CH3_ROOM = 302;

inline constexpr int SCRIPT_PARAM_OB1_NAME = 401;
inline constexpr int SCRIPT_PARAM_OB2_NAME = 501;
inline constexpr int SCRIPT_PARAM_OB3_NAME = 601;

inline constexpr int SCRIPT_PARAM_CH1_LEVEL = 110;
inline constexpr int SCRIPT_PARAM_CH2_LEVEL = 210;
inline constexpr int SCRIPT_PARAM_CH3_LEVEL = 310;

inline constexpr int SCRIPT_PARAM_CH1_HIT = 111;
inline constexpr int SCRIPT_PARAM_CH2_HIT = 211;
inline constexpr int SCRIPT_PARAM_CH3_HIT = 311;

inline constexpr int SCRIPT_PARAM_CH1_RACE = 112;
inline constexpr int SCRIPT_PARAM_CH2_RACE = 212;
inline constexpr int SCRIPT_PARAM_CH3_RACE = 312;

inline constexpr int SCRIPT_PARAM_CH1_EXP = 113;
inline constexpr int SCRIPT_PARAM_CH2_EXP = 213;
inline constexpr int SCRIPT_PARAM_CH3_EXP = 214; /* Why is this like this? */

inline constexpr int SCRIPT_PARAM_CH1_RANK = 114;
inline constexpr int SCRIPT_PARAM_CH2_RANK = 215; /* Should be 214, but CH3_EXP is 214... */
inline constexpr int SCRIPT_PARAM_CH3_RANK = 314;

inline constexpr int SCRIPT_PARAM_OB1_VNUM = 402;
inline constexpr int SCRIPT_PARAM_OB2_VNUM = 502;
inline constexpr int SCRIPT_PARAM_OB3_VNUM = 602;

int find_script_by_number(int number);
int call_trigger(int trigger_type, void* subject, void* subject2, void* subject3);
void note_object_freed(obj_data* object);
void note_char_freed(char_data* character);

/* For a loop that sets off a trigger on each of <room>'s people: the one to go on to once the
 * trigger on <current> has run. <following> is current's next_in_room from before the trigger,
 * and <current_freed> says whether current was freed meanwhile (a freed_watch tells). While
 * current exists, the loop goes on from it as it always has, to its next_in_room (wherever a
 * script has moved it). Once it is freed, the loop goes on from <following> if that is still in
 * the room, and else ends (null). */
char_data* next_person_after_trigger(const room_data& room, char_data* current,
    bool current_freed, char_data* following);
/* The same for a loop over <room>'s contents. */
obj_data* next_content_after_trigger(const room_data& room, obj_data* current,
    bool current_freed, obj_data* following);
/* True if <object> is on the next_content list that starts with <first>: a room's contents, an
 * inventory or a container's contents. <object> is compared by address and never read, so it may
 * be one that has been freed: the answer is then false, unless an object made since at the same
 * address is on the list. */
bool is_on_content_list(const obj_data* first, const obj_data* object);

void continue_char_script(char_data* ch);

/* Report script lines whose vnums name nothing -- informational only. */
void check_script_vnums(int script_index, struct char_data* to);
bool is_restyle_target_param(int param); /* int1-3 or obN.vnum */
void check_script_table(void);
bool report_script_negative_room(void);

/* A script message is not a printf format: the first %s becomes `arg`, %% a
 * percent sign, anything else is copied as is (script.cpp). */
void script_format_text(char* out, size_t outsz, const char* text, const char* arg);

#endif /* SCRIPT_H */
