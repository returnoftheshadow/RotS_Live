/* ************************************************************************
 *   File: interpreter.h                                 Part of CircleMUD *
 *  Usage: header file: public procs, macro defs, subcommand defines       *
 *                                                                         *
 *  All rights reserved.  See license.doc for complete information.        *
 *                                                                         *
 *  Copyright (C) 1993 by the Trustees of the Johns Hopkins University     *
 *  CircleMUD is based on DikuMUD, Copyright (C) 1990, 1991.               *
 ************************************************************************ */

#ifndef INTERPRE_H
#define INTERPRE_H

#include "platdef.h" /* For sh_int, ush_int, byte, etc. */
#include "structs.h" /* For the NOWHERE macro */

#include <string>

namespace account {
struct AccountData;
}

#define STATE(d) ((d)->connected)

inline constexpr int MAX_CMD_LIST = 350;

#define ACMD(c) \
    void(c)(struct char_data * ch, char* argument, struct waiting_type* wtl, int cmd, int subcmd)

// #define CRYPT(a,b) ((char *) crypt((a),(b)))

#define CRYPT(a, b) ((char*)(a))

typedef int (*special_func)(char_data* host, char_data* character, int cmd, char* argument,
    int callflag, waiting_type* wait_data);
#define SPECIAL(cname)                                                                           \
    int(cname)(struct char_data * host, struct char_data * ch, int cmd, char* arg, int callflag, \
        waiting_type* wtl)

inline constexpr int SPECIAL_NONE = 0;
inline constexpr int SPECIAL_COMMAND = 1;
inline constexpr int SPECIAL_SELF = 2;
inline constexpr int SPECIAL_ENTER = 4;
inline constexpr int SPECIAL_DELAY = 8;
inline constexpr int SPECIAL_TARGET = 16;
inline constexpr int SPECIAL_DAMAGE = 32;
inline constexpr int SPECIAL_DEATH = 64;

#define ASSIGNMOB(mob, fname)                         \
    {                                                 \
        if (real_mobile(mob) >= 0)                    \
            mob_index[real_mobile(mob)].func = fname; \
    }

#define ASSIGNREALMOB(mob, fname)            \
    {                                        \
        if (mob->nr >= 0)                    \
            mob_index[mob->nr].func = fname; \
    }

#define ASSIGNREALOBJ(obj, fname)                     \
    {                                                 \
        if (obj->item_number >= 0)                    \
            obj_index[obj->item_number].func = fname; \
    }

#define ASSIGNOBJ(obj, fname)                         \
    {                                                 \
        if (real_object(obj) >= 0)                    \
            obj_index[real_object(obj)].func = fname; \
    }

#define ASSIGNROOM(room, fname)                   \
    {                                             \
        if (real_room(room) >= 0)                 \
            world[real_room(room)].funct = fname; \
    }

void command_interpreter(struct char_data* ch, char* arg_chr, struct waiting_type* arg_wtl = 0);
int search_block(char* arg, char** list, char exact);
int old_search_block(const char* argument, int begin, unsigned int length, const char** list, int mode);
char lower(char c);
void argument_interpreter(const char* argument, char* first_arg, char* second_arg);
/* Offers <cmd> by <ch> to the special procedures that <callflag> asks: for SPECIAL_DEATH ch's
 * own; for SPECIAL_TARGET and SPECIAL_DAMAGE those of wtl's targets; otherwise <in_room>'s
 * (ch's room for NOWHERE), ch's equipment and inventory when that is ch's room, and those of
 * the people and objects there. Returns 1 as soon as a procedure returns nonzero, and also once
 * a procedure has freed ch, which the caller must then not touch; otherwise 0. */
int special(struct char_data* ch, int cmd, char* arg, int callflag, waiting_type* wtl,
    int in_room = NOWHERE);
int activate_char_special(char_data* k, char_data* ch, int cmd, char* arg, int callflag,
    waiting_type* wtl, int in_room);

char* one_argument(char* argument, char* first_arg);
int fill_word(char* argument);
void half_chop(const char* string, char* arg1, char* arg2);
void nanny(struct descriptor_data* d, char* arg);
int is_abbrev(const char* arg1, const char* arg2);
int is_number(const char* str);

void virt_assignmob(const struct char_data*);
void virt_assignobj(const struct obj_data* obj);
bool grant_account_character_selection_unlock(
    const account::AccountData& account_data, std::string* error_message);
bool account_has_restricting_active_linked_session(const account::AccountData& account_data);

/* command_info::mask bits */
inline constexpr int CMD_MASK_MOVE_PENALTY = 0x01; /* command has random move penalty */
inline constexpr int CMD_MASK_STAMINA_PENALTY = 0x02; /* command has random stamina penalty */
inline constexpr int CMD_MASK_NO_UNHIDE = 0x04; /* command doesn't unhide players */

struct command_info {
    void (*command_pointer)(struct char_data* ch, char* argument, struct waiting_type*, int cmd,
        int subcmd);
    byte minimum_position;
    sh_int minimum_level;
    char retired_allowed;
    sh_int sort_pos;
    int subcmd;
    byte is_social;

    unsigned char mask;

    int target_mask[32];
    void add_target(int mask_in, int value)
    {
        for (int index = 0; index < 32; index++) {
            int bit_position_as_int = 1 << index;
            if (bit_position_as_int & mask_in) {
                target_mask[index] |= value;
            }
        }
    }

    /* This is never used anywhere */
    bool valid_target(int mask_in, int value)
    {
        for (int index = 0; index < 32; index++) {
            int bit_position_as_int = 1 << index;
            if (bit_position_as_int & mask_in) {
                return (target_mask[index] & value) != 0;
            }
        }

        return false;
    }
};

inline constexpr int SCMD_INFO = 100;
inline constexpr int SCMD_HANDBOOK = 101;
inline constexpr int SCMD_CREDITS = 102;
inline constexpr int SCMD_WIZLIST = 104;
inline constexpr int SCMD_POLICIES = 105;
inline constexpr int SCMD_VERSION = 106;
inline constexpr int SCMD_IMMLIST = 107;
inline constexpr int SCMD_CLEAR = 108;
inline constexpr int SCMD_WHOAMI = 109;
inline constexpr int SCMD_TOP = 110;

inline constexpr int SCMD_TOG_BASE = 201;
inline constexpr int SCMD_NOSUMMON = 201;
inline constexpr int SCMD_NOHASSLE = 202;
inline constexpr int SCMD_BRIEF = 203;
inline constexpr int SCMD_COMPACT = 204;
inline constexpr int SCMD_NOTELL = 205;
// #define SCMD_NARRATE	207
// #define SCMD_CHAT	208
inline constexpr int SCMD_WIZ = 210;
inline constexpr int SCMD_SPAM = 211;
inline constexpr int SCMD_ROOMFLAGS = 212;
inline constexpr int SCMD_ECHO = 213;
inline constexpr int SCMD_HOLYLIGHT = 214;
inline constexpr int SCMD_SLOWNS = 215;
inline constexpr int SCMD_WRAP = 216;
inline constexpr int SCMD_MSDP = 217;
inline constexpr int SCMD_DESCRIP = 218;
inline constexpr int SCMD_INCOGNITO = 219;
inline constexpr int SCMD_TIME = 220;
inline constexpr int SCMD_MENTAL = 221;
inline constexpr int SCMD_SETPROMPT = 222;
inline constexpr int SCMD_SWIM = 223;
inline constexpr int SCMD_LATIN1 = 224;
inline constexpr int SCMD_SPINNER = 225;
inline constexpr int SCMD_ADVANCED_VIEW = 226;
inline constexpr int SCMD_ADVANCED_PROMPT = 227;

inline constexpr int SCMD_PARDON = 301;
inline constexpr int SCMD_NOTITLE = 302;
inline constexpr int SCMD_SQUELCH = 303;
inline constexpr int SCMD_FREEZE = 304;
inline constexpr int SCMD_THAW = 305;
inline constexpr int SCMD_UNAFFECT = 306;
inline constexpr int SCMD_REROLL = 307;
inline constexpr int SCMD_RETIRE = 308;
inline constexpr int SCMD_REACTV = 309;
inline constexpr int SCMD_REHASH = 310;
inline constexpr int SCMD_RENAME = 312;
inline constexpr int SCMD_NOTE = 313;

inline constexpr int SCMD_BOARD = 400;
inline constexpr int SCMD_NEWS = 401;
inline constexpr int SCMD_MAIL = 402;

inline constexpr int SCMD_NARRATE = 0;
inline constexpr int SCMD_CHAT = 1;
inline constexpr int SCMD_YELL = 2;
inline constexpr int SCMD_SING = 3;

inline constexpr int SCMD_TELL = 0;
inline constexpr int SCMD_REPLY = 1;

inline constexpr int SCMD_SHUTDOWN = 1;
inline constexpr int SCMD_QUIT = 1;

inline constexpr int SCMD_COMMANDS = 0;
inline constexpr int SCMD_SOCIALS = 1;
inline constexpr int SCMD_WIZHELP = 2;

inline constexpr int SCMD_GECHO = 0;
inline constexpr int SCMD_QECHO = 1;

inline constexpr int SCMD_POUR = 0;
inline constexpr int SCMD_FILL = 1;

inline constexpr int SCMD_POOFIN = 0;
inline constexpr int SCMD_POOFOUT = 1;

inline constexpr int SCMD_HIT = 0;
inline constexpr int SCMD_MURDER = 1;
inline constexpr int SCMD_WILL = 2;

inline constexpr int SCMD_EAT = 0;
inline constexpr int SCMD_TASTE = 1;
inline constexpr int SCMD_DRINK = 2;
inline constexpr int SCMD_SIP = 3;

inline constexpr int SCMD_NONHANDED = 0;
inline constexpr int SCMD_TWOHANDED = 1;
inline constexpr int SCMD_ONEHANDED = 2;

inline constexpr int SCMD_PAGE = 0;
inline constexpr int SCMD_BEEP = 1;

inline constexpr int SCMD_BUTCHER = 0;
inline constexpr int SCMD_SCALP = 1;

inline constexpr int SCMD_MOVING = 0;
inline constexpr int SCMD_FOLLOW = 1;
inline constexpr int SCMD_MOUNT = 2;
inline constexpr int SCMD_FLEE = 3;
inline constexpr int SCMD_CARRIED = 4;
inline constexpr int SCMD_STALK = 5;

inline constexpr int SCMD_APPLY_POISON = 0;
inline constexpr int SCMD_APPLY_ANTIDOTE = 1;

inline constexpr int SCMD_LOOK_NORM = 0;
inline constexpr int SCMD_LOOK_EXAM = 1;
inline constexpr int SCMD_LOOK_BRIEF = 2;

inline constexpr int OUTDOORS_LIGHT = 0;
inline constexpr int OUTDOORS_WIND = 1;

inline constexpr int PROMPT_HIT = 1;
inline constexpr int PROMPT_MANA = 2;
inline constexpr int PROMPT_MOVE = 4;
inline constexpr int PROMPT_STAT = 8;
inline constexpr int PROMPT_MAUL = 16;
inline constexpr int PROMPT_ARROWS = 32;
inline constexpr int PROMPT_ADVANCED = 64;
inline constexpr int PROMPT_ALL = (PROMPT_HIT | PROMPT_MANA | PROMPT_MOVE);

inline constexpr int SCMD_KICK = 1;
inline constexpr int SCMD_SWING = 2;

inline constexpr int SCMD_BITE = 1;
inline constexpr int SCMD_REND = 2;
inline constexpr int SCMD_MAUL = 3;

inline constexpr int CMD_ENTER = 7;
inline constexpr int CMD_LOOK = 15;
inline constexpr int CMD_SAY = 17;
inline constexpr int CMD_YELL = 18;
inline constexpr int CMD_TELL = 19;
inline constexpr int CMD_SOCIAL = 22;
inline constexpr int CMD_KILL = 23;
inline constexpr int CMD_EMOTE = 27;
inline constexpr int CMD_READ = 45;
inline constexpr int CMD_REMOVE = 48;
inline constexpr int CMD_QUIT = 55;
inline constexpr int CMD_OPEN = 77;
inline constexpr int CMD_CLOSE = 78;
inline constexpr int CMD_BASH = 90;
inline constexpr int CMD_RESCUE = 91;
inline constexpr int CMD_KICK = 92;
inline constexpr int CMD_HIT = 52;
inline constexpr int CMD_PRACRESET = 119;
inline constexpr int CMD_WIZNET = 124;
inline constexpr int CMD_SEARCH = 125;
inline constexpr int CMD_PRAY = 130;
inline constexpr int CMD_JIG = 132;
inline constexpr int CMD_BLOCK = 133;
inline constexpr int CMD_CONCENTRATE = 143;
inline constexpr int CMD_ASSIST = 146;
inline constexpr int CMD_KNOCK = 151;
inline constexpr int CMD_WHISTLE = 179;
inline constexpr int CMD_SWING = 180;
inline constexpr int CMD_AFFECTIONS = 186;
inline constexpr int CMD_TRACK = 195;
inline constexpr int CMD_SHAPE = 200;
inline constexpr int CMD_GATHER_FOOD = 203;
inline constexpr int CMD_PREPARE = 205;
inline constexpr int CMD_FAME = 208;
inline constexpr int CMD_ORC_DELAY = 209;
inline constexpr int CMD_STALK = 215;
inline constexpr int CMD_COVER = 216;
inline constexpr int CMD_SHOOT = 229;
inline constexpr int CMD_MARK = 237;
inline constexpr int CMD_BLINDING = 238;
inline constexpr int CMD_BENDTIME = 239;
inline constexpr int CMD_WINDBLAST = 240;
inline constexpr int CMD_SMASH = 241;
inline constexpr int CMD_FRENZY = 242;
inline constexpr int CMD_STOMP = 243;
inline constexpr int CMD_CLEAVE = 244;
inline constexpr int CMD_OVERRUN = 245;
inline constexpr int CMD_DEFEND = 246;

inline constexpr int CMD_HIDE = 86;
inline constexpr int CMD_AMBUSH = 87;
inline constexpr int CMD_WRITE = 82;
inline constexpr int CMD_EXAMINE = 94;
inline constexpr int CMD_NEXT = 168;
inline constexpr int CMD_SEND = 171;
inline constexpr int CMD_BUY = 38;
inline constexpr int CMD_SELL = 39;
inline constexpr int CMD_VALUE = 40;
inline constexpr int CMD_LIST = 41;
inline constexpr int CMD_GIVE = 54;
inline constexpr int CMD_BALANCE = 138;
inline constexpr int CMD_DEPOSIT = 139;
inline constexpr int CMD_WITHDRAW = 140;
inline constexpr int CMD_CAST = 66;
inline constexpr int CMD_RECITE = 112;
inline constexpr int CMD_USE = 99;
inline constexpr int CMD_PRACTICE = 93;
inline constexpr int CMD_PRACTISE = 98;
inline constexpr int CMD_RENT = 74;
inline constexpr int CMD_OFFER = 75;
inline constexpr int CMD_TAME = 210;
inline constexpr int CMD_CALM = 211;
inline constexpr int CMD_FISH = 212;
inline constexpr int CMD_TRAP = 220;

inline constexpr int CMD_ASK = 68;

inline constexpr int CMD_SCRIPT = 9999; // for waitin_list returns

/* definitions for special mobiles, ferry */

struct ferry_boat_type {
    int length;
    int from_room[100];
    int to_room[100];
};

struct ferry_captain_type {
    int num_of_ferry; // virt. number of the ferry_boat.
    int length; // route length
    int ferry_route[100]; // where to put the ferry object
    int cabin_route[100]; // where to put passengers
    int stop_time[100]; // in quarter-ticks (?) 0 = no stopping
    int timer;
    int marker;
    char leave_to_outside[255]; // $n stands for the captain,
    char leave_to_inside[255]; // $o - for the ferry
    char arrive_to_outside[255];
    char arrive_to_inside[255];
    char move_out_outside[255];
    char move_out_inside[255];
    char move_in_outside[255];
    char move_in_inside[255];
};

#endif /* INTERPRE_H */
