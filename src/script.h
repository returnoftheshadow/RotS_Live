
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
/* How many scripts can run inside one another (a script setting off a trigger whose script sets
 * off another...). A trigger set off past that runs no script, as though there were none, and
 * logs a SYSERR naming the script and its subject. */
inline constexpr int MAX_SCRIPT_NESTING = 32;
/* Runs the scripts a game event triggers. Returns 1 if the event goes ahead as usual and 0 if a
 * script ended with RETURN_FALSE (what that stops is listed with each ON_ trigger above).
 * An object's own ON_DRINK, ON_EAT, ON_EXAMINE_OBJECT, ON_PULL and ON_WEAR scripts go through
 * call_object_trigger instead: given one of those, call_trigger runs nothing, logs an error and
 * returns 1.
 *
 * A script can free characters and objects (EXTRACT_CHAR, EXTRACT_OBJ, a death it causes),
 * including the subjects it was given, so a caller that uses one afterwards keeps a freed_watch
 * (freed_watch.h) on it across the call. call_trigger itself touches nothing freed:
 *   - ON_BEFORE_ENTER, ON_ENTER: once the one entering (subject2) is freed, the room's remaining
 *     mobs and objects do not react, and 0 is returned.
 *   - ON_DAMAGE: once the victim (subject) or the attacker (subject2) is freed, the weapon's
 *     script does not run, and 0 is returned: no damage is to be dealt.
 *   - ON_HEAR_SAY, ON_HEAR_YELL: once the mob or the speaker is freed, the mob's yell script does
 *     not run. */
int call_trigger(int trigger_type, void* subject, void* subject2, void* subject3);

/* How an object's own trigger script ended, for the command that fired it. */
enum class object_trigger_result {
    proceed, /* there is no such script, or it ended without RETURN_FALSE */
    refused, /* it ended with RETURN_FALSE; the command decides what that stops */
    object_extracted, /* the object was freed while the script ran, whatever the script returned */
};

/* Runs <object>'s own ON_DRINK, ON_EAT, ON_EXAMINE_OBJECT, ON_PULL or ON_WEAR script, with the
 * object as ob1 and <ch> (which may be null) as ch1.
 *
 * After object_extracted the caller must not read, write or free the object again: it is gone,
 * whether the script extracted it itself or something the script set off did (a container
 * extracted with it, another script). A null <object> is logged and returns object_extracted,
 * as there is nothing to touch; any other trigger_type runs nothing, is logged and returns
 * proceed. */
[[nodiscard]] object_trigger_result call_object_trigger(int trigger_type, obj_data* object,
    char_data* ch);

/* Tells the game that <object> is about to be freed: every freed_watch on it reports freed (so a
 * call_object_trigger on it reports object_extracted), every running script's ob1-ob3 naming it
 * is emptied, and its script state is released, at once or, while a script is running on that
 * state, when the script ends. free_obj calls it for every object it frees. */
void note_object_freed(obj_data* object);
/* The same for <character>, whose running scripts' ch1-ch3 naming it are emptied. free_char calls
 * it for every character it frees. */
void note_char_freed(char_data* character);

/* For a loop that sets off a trigger on each of <room>'s people: the one to go on to once the
 * trigger on <current> has run. <following> is current's next_in_room from before the trigger,
 * and <current_freed> says whether current was freed meanwhile (a freed_watch tells). While
 * current is still in the room, the loop goes on from it as usual. Otherwise it goes on from
 * <following> if that is still in the room, and else it ends (null): a script that moved or
 * removed both has left nothing to go on from. */
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

/* Resumes the script <ch> paused with DO_WAIT, with ch1 set to ch and the other character, object
 * and room variables empty; its texts and ints carry on. */
void continue_char_script(char_data* ch);
/* For complete_delay's forced completion of <ch>'s DO_WAIT pause (another delay replacing it): the
 * paused script is not run there, inside whoever forced it, but kept to resume once the replacing
 * delay ends (resume_deferred_char_script). Nothing happens if ch has no paused script. */
void defer_char_script(char_data* ch);
/* Resumes <ch>'s deferred script, if it has one; the script can free ch. For process_waiting_list,
 * once the delay that replaced the pause has ended and ch waits for nothing. Returns whether a
 * script was resumed. */
bool resume_deferred_char_script(char_data* ch);
/* Discards <ch>'s deferred script, if it has one, and tells the builders so, giving <reason>
 * (raw_kill: the character died). */
void drop_deferred_char_script(char_data* ch, const char* reason);

/* Report script lines whose vnums name nothing -- informational only. */
void check_script_vnums(int script_index, struct char_data* to);
bool is_restyle_target_param(int param); /* int1-3 or obN.vnum */
void check_script_table(void);
bool report_script_negative_room(void);

/* A script message is not a printf format: the first %s becomes `arg`, %% a
 * percent sign, anything else is copied as is (script.cpp). */
void script_format_text(char* out, size_t outsz, const char* text, const char* arg);

#endif /* SCRIPT_H */
