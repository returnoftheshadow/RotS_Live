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

#define STATE(d) ((d)->connected)

#define MAX_CMD_LIST 350

#define ACMD(c) \
    void(c)(struct char_data * ch, char* argument, struct waiting_type* wtl, int cmd, int subcmd)

// #define CRYPT(a,b) ((char *) crypt((a),(b)))

#define CRYPT(a, b) ((char*)(a))

typedef int (*special_func)(char_data* host, char_data* character, int cmd, char* argument,
    int callflag, waiting_type* wait_data);
#define SPECIAL(cname)                                                                           \
    int(cname)(struct char_data * host, struct char_data * ch, int cmd, char* arg, int callflag, \
        waiting_type* wtl)

#define SPECIAL_NONE 0
#define SPECIAL_COMMAND 1
#define SPECIAL_SELF 2
#define SPECIAL_ENTER 4
#define SPECIAL_DELAY 8
#define SPECIAL_TARGET 16
#define SPECIAL_DAMAGE 32
#define SPECIAL_DEATH 64

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
int old_search_block(char* argument, int begin, unsigned int length, const char** list, int mode);
char lower(char c);
void argument_interpreter(char* argument, char* first_arg, char* second_arg);
int special(struct char_data* ch, int cmd, char* arg, int callflag, waiting_type* wtl,
    int in_room = NOWHERE);
int activate_char_special(char_data* k, char_data* ch, int cmd, char* arg, int callflag,
    waiting_type* wtl, int in_room);

char* one_argument(char* argument, char* first_arg);
int fill_word(char* argument);
void half_chop(char* string, char* arg1, char* arg2);
void nanny(struct descriptor_data* d, char* arg);
int is_abbrev(char* arg1, char* arg2);
int is_number(char* str);

void virt_assignmob(struct char_data*);
void virt_assignobj(struct obj_data* obj);

struct command_info {
    void (*command_pointer)(struct char_data* ch, char* argument, struct waiting_type*, int cmd,
        int subcmd);
    byte minimum_position;
    sh_int minimum_level;
    char retired_allowed;
    sh_int sort_pos;
    int subcmd;
    byte is_social;

/* mask bits */
#define CMD_MASK_MOVE_PENALTY 0x01 /* command has random move penalty */
#define CMD_MASK_STAMINA_PENALTY 0x02 /* command has random stamina penalty */
#define CMD_MASK_NO_UNHIDE 0x04 /* command doesn't unhide players */
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

#define SCMD_INFO 100
#define SCMD_HANDBOOK 101
#define SCMD_CREDITS 102
#define SCMD_WIZLIST 104
#define SCMD_POLICIES 105
#define SCMD_VERSION 106
#define SCMD_IMMLIST 107
#define SCMD_CLEAR 108
#define SCMD_WHOAMI 109
#define SCMD_TOP 110

#define SCMD_TOG_BASE 201
#define SCMD_NOSUMMON 201
#define SCMD_NOHASSLE 202
#define SCMD_BRIEF 203
#define SCMD_COMPACT 204
#define SCMD_NOTELL 205
// #define SCMD_NARRATE	207
// #define SCMD_CHAT	208
#define SCMD_WIZ 210
#define SCMD_SPAM 211
#define SCMD_ROOMFLAGS 212
#define SCMD_ECHO 213
#define SCMD_HOLYLIGHT 214
#define SCMD_SLOWNS 215
#define SCMD_WRAP 216
#define SCMD_MSDP 217
#define SCMD_DESCRIP 218
#define SCMD_INCOGNITO 219
#define SCMD_TIME 220
#define SCMD_MENTAL 221
#define SCMD_SETPROMPT 222
#define SCMD_SWIM 223
#define SCMD_LATIN1 224
#define SCMD_SPINNER 225
#define SCMD_ADVANCED_VIEW 226
#define SCMD_ADVANCED_PROMPT 227

#define SCMD_PARDON 301
#define SCMD_NOTITLE 302
#define SCMD_SQUELCH 303
#define SCMD_FREEZE 304
#define SCMD_THAW 305
#define SCMD_UNAFFECT 306
#define SCMD_REROLL 307
#define SCMD_RETIRE 308
#define SCMD_REACTV 309
#define SCMD_REHASH 310
#define SCMD_RENAME 312
#define SCMD_NOTE 313

#define SCMD_BOARD 400
#define SCMD_NEWS 401
#define SCMD_MAIL 402

#define SCMD_NARRATE 0
#define SCMD_CHAT 1
#define SCMD_YELL 2
#define SCMD_SING 3

#define SCMD_TELL 0
#define SCMD_REPLY 1

#define SCMD_SHUTDOWN 1
#define SCMD_QUIT 1

#define SCMD_COMMANDS 0
#define SCMD_SOCIALS 1
#define SCMD_WIZHELP 2

#define SCMD_GECHO 0
#define SCMD_QECHO 1

#define SCMD_POUR 0
#define SCMD_FILL 1

#define SCMD_POOFIN 0
#define SCMD_POOFOUT 1

#define SCMD_HIT 0
#define SCMD_MURDER 1
#define SCMD_WILL 2

#define SCMD_EAT 0
#define SCMD_TASTE 1
#define SCMD_DRINK 2
#define SCMD_SIP 3

#define SCMD_NONHANDED 0
#define SCMD_TWOHANDED 1
#define SCMD_ONEHANDED 2

#define SCMD_PAGE 0
#define SCMD_BEEP 1

#define SCMD_BUTCHER 0
#define SCMD_SCALP 1

#define SCMD_MOVING 0
#define SCMD_FOLLOW 1
#define SCMD_MOUNT 2
#define SCMD_FLEE 3
#define SCMD_CARRIED 4
#define SCMD_STALK 5

#define SCMD_APPLY_POISON 0
#define SCMD_APPLY_ANTIDOTE 1

#define SCMD_LOOK_NORM 0
#define SCMD_LOOK_EXAM 1
#define SCMD_LOOK_BRIEF 2

#define OUTDOORS_LIGHT 0
#define OUTDOORS_WIND 1

#define PROMPT_HIT 1
#define PROMPT_MANA 2
#define PROMPT_MOVE 4
#define PROMPT_STAT 8
#define PROMPT_MAUL 16
#define PROMPT_ARROWS 32
#define PROMPT_ADVANCED 64
#define PROMPT_ALL (PROMPT_HIT | PROMPT_MANA | PROMPT_MOVE)

#define SCMD_KICK 1
#define SCMD_SWING 2

#define SCMD_BITE 1
#define SCMD_REND 2
#define SCMD_MAUL 3

#define CMD_ENTER 7
#define CMD_LOOK 15
#define CMD_SAY 17
#define CMD_YELL 18
#define CMD_TELL 19
#define CMD_SOCIAL 22
#define CMD_KILL 23
#define CMD_EMOTE 27
#define CMD_READ 45
#define CMD_REMOVE 48
#define CMD_QUIT 55
#define CMD_OPEN 77
#define CMD_CLOSE 78
#define CMD_BASH 90
#define CMD_RESCUE 91
#define CMD_KICK 92
#define CMD_HIT 52
#define CMD_PRACRESET 119
#define CMD_WIZNET 124
#define CMD_SEARCH 125
#define CMD_PRAY 130
#define CMD_JIG 132
#define CMD_BLOCK 133
#define CMD_CONCENTRATE 143
#define CMD_ASSIST 146
#define CMD_KNOCK 151
#define CMD_WHISTLE 179
#define CMD_SWING 180
#define CMD_AFFECTIONS 186
#define CMD_TRACK 195
#define CMD_SHAPE 200
#define CMD_GATHER_FOOD 203
#define CMD_PREPARE 205
#define CMD_FAME 208
#define CMD_ORC_DELAY 209
#define CMD_STALK 215
#define CMD_COVER 216
#define CMD_SHOOT 229
#define CMD_MARK 237
#define CMD_BLINDING 238
#define CMD_BENDTIME 239
#define CMD_WINDBLAST 240
#define CMD_SMASH 241
#define CMD_FRENZY 242
#define CMD_STOMP 243
#define CMD_CLEAVE 244
#define CMD_OVERRUN 245
#define CMD_DEFEND 246

#define CMD_HIDE 86
#define CMD_AMBUSH 87
#define CMD_WRITE 82
#define CMD_EXAMINE 94
#define CMD_NEXT 168
#define CMD_SEND 171
#define CMD_BUY 38
#define CMD_SELL 39
#define CMD_VALUE 40
#define CMD_LIST 41
#define CMD_CAST 66
#define CMD_RECITE 112
#define CMD_USE 99
#define CMD_PRACTICE 93
#define CMD_PRACTISE 98
#define CMD_RENT 74
#define CMD_OFFER 75
#define CMD_TAME 210
#define CMD_CALM 211
#define CMD_FISH 212
#define CMD_TRAP 220

#define CMD_ASK 68

#define CMD_SCRIPT 9999 // for waitin_list returns

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
