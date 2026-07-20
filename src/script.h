
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

#define SCRIPT_BEGIN 1 //  Marks the beginning of a block
#define SCRIPT_END 2 //  End block
#define SCRIPT_ABORT 3 //  Abort execution of the script (returns TRUE - program continues as normal)
#define SCRIPT_IF_INT_EQUAL 4 //  Tests to see whether or not two integers are equal
#define SCRIPT_END_ELSE_BEGIN 5 //	Ends a block.  If previous block was being executed then the next
//     block is skipped and vice versa.
#define SCRIPT_RETURN_FALSE 6 //  Halts execution of the script and returns false
//    Program acts on false depending on context eg:
//    a trigger on_die returning false will stop the character dying
//     nb, ABORT returns true - program continues as normal
#define SCRIPT_IF_INT_LESS 7 //  Test to see whether one integer is less than another
#define SCRIPT_IF_IS_NPC 8 //	Test if a character is a player or mobile
#define SCRIPT_IF_STR_EQUAL 9 //  Test if two strings are equal (regardless of case)
#define SCRIPT_IF_STR_CONTAINS 10 //  Test if a string contains another (regardless of case)

#define ON_ENTER 11 //  Signal the beginning of triggers in a script linked list
#define ON_BEFORE_ENTER 12 //  Trigger - if the return value is FALSE - character cannot enter
#define ON_BEFORE_DIE 13 // implemented??
#define ON_DIE 14 //  Trigger - if the return value is FALSE - character does not die
#define ON_RECEIVE 15 //  Trigger
#define ON_EXAMINE_OBJECT 16 //  Object trigger - sends text to character etc.
#define ON_HEAR_SAY 17 //  When a character hears another speak
#define ON_DAMAGE 18 //	When a character damages another.  Returns FALSE if the damage is not to be applied.
#define ON_EAT 19 //  When a character eats an object
#define ON_DRINK 20 //  When a character drinks from an object
#define ON_WEAR 21 //  When a character places an object into their equipment (return false and wear fails)
#define ON_PULL 22 //  When a character pulls a lever (return FALSE - lever is not pulled)
#define ON_HEAR_YELL 23 // When a character hears another yell

#define SCRIPT_DO_SAY 30 //  Script command do_say - similar to ACMD
#define SCRIPT_ASSIGN_STR 31 //  Assign the contents of param1 to script variable str1
#define SCRIPT_SEND_TO_ROOM 32 //  Send text to room
#define SCRIPT_SEND_TO_ROOM_X 33 //  Send text to room except param (ch)
#define SCRIPT_DO_HIT 34 //  Hit (param)
#define SCRIPT_ASSIGN_INV 35 //  Assign obj vnum from ch's inventory to obj
#define SCRIPT_SEND_TO_CHAR 36 //  Send text to character
#define SCRIPT_DO_FLEE 37 //  Force a character to flee
#define SCRIPT_DO_EMOTE 38 //  A character's emote
#define SCRIPT_LOAD_MOB 39 //  Load a mobile assign to a chx variable
#define SCRIPT_TELEPORT_CHAR 40 //  Take char_ and followers from_room then put char_to_room
#define SCRIPT_DO_YELL 41 //  Script command to yell
#define SCRIPT_DO_FOLLOW 42 //  One character to follow another
#define SCRIPT_EXTRACT_CHAR 43 //  Removes a character from the game (inventory is left in the room)
#define SCRIPT_SET_EXIT_STATE 44 //  Script interpretation of the function in db.cc
#define SCRIPT_RAW_KILL 45 //  Kills the character/player :(
#define SCRIPT_DO_GIVE 46 //  Makes a character give obx to another
#define SCRIPT_LOAD_OBJ 47 //  Load an object - note this object is not placed in the game
#define SCRIPT_OBJ_TO_CHAR 48 //  Use after load_obj: put object in a character's inventory
#define SCRIPT_OBJ_TO_ROOM 49 //  Use after load_obj: put object in a room's contents
#define SCRIPT_CHANGE_EXIT_TO 50 //  Change the room an exit goes to
#define SCRIPT_EXTRACT_OBJ 51 //	Extract an object from the world (can be from inventory, contents, room etc)
#define SCRIPT_ASSIGN_EQ 52 //	Assign an object from equipment position
#define SCRIPT_DO_DROP 53 //	Make a character drop an object
#define SCRIPT_DO_REMOVE 54 //	Make a character remove an object from equipment
#define SCRIPT_DO_WEAR 55 //	Make a character wear an object (if possible)
#define SCRIPT_DO_SOCIAL 56 //  Make a character perform a social command
#define SCRIPT_SET_INT_VALUE 57 //  Set a value to an integer
#define SCRIPT_DO_WAIT 58 //  For mobiles - put on the waiting_list for a while
#define SCRIPT_PAGE_ZONE_MAP 59 //  Pages a zone map to a character
#define SCRIPT_SET_INT_SUM 60 //  Add two ints together and assign the result
#define SCRIPT_SET_INT_MULT 61 //  As above with multiply
#define SCRIPT_SET_INT_DIV 62 //  As above with division
#define SCRIPT_SET_INT_SUB 63 //  As above with subtraction
#define SCRIPT_GAIN_EXP 64 //  Give/subtract exp to a player
#define SCRIPT_TELEPORT_CHAR_X 65 //  Take char_from_room then put char_to_room (leave followers behind)
#define SCRIPT_OBJ_FROM_ROOM 66 // Remove an object from a room.
#define SCRIPT_OBJ_FROM_CHAR 67 // Remove and object from a character's inventory
#define SCRIPT_ASSIGN_ROOM 68 // Assign an object in a room by vnum to obj pointer
#define SCRIPT_SET_INT_RANDOM 69 // Assign a random number between two values to an integer
#define SCRIPT_EQUIP_CHAR 70 // Load a number of objects to a char and then do wear_all
#define SCRIPT_SET_INT_WAR_STATUS 71 // Set an int to 1 if whities lead fame war, -1 if darkies lead fame war and 0 if the sides are equal
#define SCRIPT_IF_INT_GREATER 72 //  Test to see whether one integer is greater than another
#define SCRIPT_IF_INT_TRUE 73 //  Test to see whether one integer is greater than 0
#define SCRIPT_IF_ROOM_SUNLIT 74 // Check to see if there is sunlight in the room

#define SCRIPT_TELEPORT_CHAR_XL 75 //  Take char_from_room then put chX.room (leave followers behind)
#define SCRIPT_IF_INT_FALSE 76 //  Test to see whether one integer is less than 1
#define SCRIPT_LOAD_OBJ_X 77 //  Load an object from another object - note this object is not placed in the game

#define SCRIPT_COMMAND_NONE 99 //  Given to new or unused commands
// 999 - reserved for script loading - do not use

// defines for script_data parameters - variables etc unless the macros in shapescript are to be changed, these
//   parameters -must- lie in the range 100-999

#define SCRIPT_PARAM_STR1 950
#define SCRIPT_PARAM_STR2 951
#define SCRIPT_PARAM_STR3 952

#define SCRIPT_PARAM_INT1 953
#define SCRIPT_PARAM_INT2 954
#define SCRIPT_PARAM_INT3 955

#define SCRIPT_PARAM_CH1 100
#define SCRIPT_PARAM_CH2 200
#define SCRIPT_PARAM_CH3 300

#define SCRIPT_PARAM_OB1 400
#define SCRIPT_PARAM_OB2 500
#define SCRIPT_PARAM_OB3 600

#define SCRIPT_PARAM_RM1 700
#define SCRIPT_PARAM_RM2 800
#define SCRIPT_PARAM_RM3 900

#define SCRIPT_PARAM_RM1_NAME 701
#define SCRIPT_PARAM_RM2_NAME 702
#define SCRIPT_PARAM_RM3_NAME 703

#define SCRIPT_PARAM_CH1_NAME 101
#define SCRIPT_PARAM_CH2_NAME 201
#define SCRIPT_PARAM_CH3_NAME 301

#define SCRIPT_PARAM_CH1_ROOM 102
#define SCRIPT_PARAM_CH2_ROOM 202
#define SCRIPT_PARAM_CH3_ROOM 302

#define SCRIPT_PARAM_OB1_NAME 401
#define SCRIPT_PARAM_OB2_NAME 501
#define SCRIPT_PARAM_OB3_NAME 601

#define SCRIPT_PARAM_CH1_LEVEL 110
#define SCRIPT_PARAM_CH2_LEVEL 210
#define SCRIPT_PARAM_CH3_LEVEL 310

#define SCRIPT_PARAM_CH1_HIT 111
#define SCRIPT_PARAM_CH2_HIT 211
#define SCRIPT_PARAM_CH3_HIT 311

#define SCRIPT_PARAM_CH1_RACE 112
#define SCRIPT_PARAM_CH2_RACE 212
#define SCRIPT_PARAM_CH3_RACE 312

#define SCRIPT_PARAM_CH1_EXP 113
#define SCRIPT_PARAM_CH2_EXP 213
#define SCRIPT_PARAM_CH3_EXP 214 /* Why is this like this? */

#define SCRIPT_PARAM_CH1_RANK 114
#define SCRIPT_PARAM_CH2_RANK 215 /* Should be 214, but CH3_EXP is 214... */
#define SCRIPT_PARAM_CH3_RANK 314

#define SCRIPT_PARAM_OB1_VNUM 402
#define SCRIPT_PARAM_OB2_VNUM 502
#define SCRIPT_PARAM_OB3_VNUM 602

int find_script_by_number(int number);
int call_trigger(int trigger_type, void* subject, void* subject2, void* subject3);
int trigger_object_wear_event(obj_data* obj, char_data* ch, int wear_slot);
void continue_char_script(char_data* ch);
void js_script_set_legacy_trigger_dispatch_enabled(bool enabled);
bool js_script_legacy_trigger_dispatch_enabled();
void js_script_invalidate_live_registry_generation();
bool js_script_capture_live_registry_generation();
bool js_script_refresh_live_registry_after_publish();

#endif /* SCRIPT_H */
