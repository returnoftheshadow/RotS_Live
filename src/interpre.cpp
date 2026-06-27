/**************************************************************************
 *   File: interpreter.c                                 Part of CircleMUD *
 *  Usage: parse user commands, search for specials, call ACMD functions   *
 *                                                                         *
 *  All rights reserved.  See license.doc for complete information.        *
 *                                                                         *
 *  Copyright (C) 1993 by the Trustees of the Johns Hopkins University     *
 *  CircleMUD is based on DikuMUD, Copyright (C) 1990, 1991.               *
 **************************************************************************/

#include "platdef.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "color.h"
#include "comm.h"
#include "db.h"
#include "handler.h"
#include "interpre.h"
#include "limits.h"
#include "mail.h"
#include "pkill.h"
#include "profs.h"
#include "protos.h"
#include "spells.h"
#include "structs.h"
#include "utils.h"
#include "mob_csv_extract.h"

#include "big_brother.h"
#include "char_utils.h"

#define COMMANDO(number, min_pos, pointer, min_level,    \
    retired, subcommand, targfl1, targfl2,               \
    special_mask)                                        \
    {                                                    \
        cmd_info[(number)].command_pointer = (pointer);  \
        cmd_info[(number)].minimum_position = (min_pos); \
        cmd_info[(number)].retired_allowed = (retired);  \
        cmd_info[(number)].minimum_level = (min_level);  \
        cmd_info[(number)].subcmd = (subcommand);        \
        cmd_info[(number)].mask |= (special_mask);       \
        cmd_info[(number)].add_target(targfl1, targfl2); \
    }

#define ADD_TARGET(number, targfl1, targfl2) \
    cmd_info[(number)].add_target(targfl1, targfl2)

#define FULL_TARGET (131071 & ~TAR_SELF_NONO & ~TAR_IGNORE)

extern struct prof_type existing_profs[DEFAULT_PROFS];
extern struct player_index_element* player_table;
extern struct char_data* character_list;
extern struct index_data* mob_index;
extern struct index_data* obj_index;
extern struct room_data world;

extern int social_command_number;
extern char* wizlock_default;
extern char* wizlock_msg;
extern int top_of_p_table;
extern char* START_MESSG;
extern char* WELC_MESSG;
extern char* background;
extern int no_specials;
extern int restrict;
extern char* dirs[];
extern char* imotd;
extern char* motd;
extern char* MENU;

struct command_info cmd_info[MAX_CMD_LIST];

int new_player_select(struct descriptor_data*, char*);
void introduce_char(struct descriptor_data*);
void one_mobile_activity(struct char_data*);
int update_memory_list(struct char_data*);
void stop_hiding(struct char_data*, char);
int valid_name(char*);
void report_mail(struct char_data*);
void report_news(struct char_data*);
void init_char(struct char_data*);
void set_title(struct char_data*);
void do_start(struct char_data*);
int create_entry(char*);
int find_action(char*);
int isbanned(char*);
int echo_off(int);
int echo_on(int);

SPECIAL(intelligent);
SPECIAL(gen_board);

ACMD(do_obj2html);
ACMD(do_look);
ACMD(do_move);
ACMD(do_look);
ACMD(do_read);
ACMD(do_say);
ACMD(do_exit);
ACMD(do_snoop);
ACMD(do_insult);
ACMD(do_quit);
ACMD(do_help);
ACMD(do_who);
ACMD(do_emote);
ACMD(do_echo);
ACMD(do_trans);
ACMD(do_stand);
ACMD(do_sit);
ACMD(do_rest);
ACMD(do_sleep);
ACMD(do_wake);
ACMD(do_force);
ACMD(do_get);
ACMD(do_drop);
ACMD(do_score);
ACMD(do_inventory);
ACMD(do_equipment);
ACMD(do_not_here);
ACMD(do_tell);
ACMD(do_wear);
ACMD(do_wield);
ACMD(do_grab);
ACMD(do_remove);
ACMD(do_put);
ACMD(do_shutdown);
ACMD(do_save);
ACMD(do_hit);
ACMD(do_string);
ACMD(do_give);
ACMD(do_stat);
ACMD(do_wizset);
ACMD(do_time);
ACMD(do_weather);
ACMD(do_load);
ACMD(do_vstat);
ACMD(do_purge);
ACMD(do_whisper);
ACMD(do_cast);
ACMD(do_at);
ACMD(do_goto);
ACMD(do_ask);
ACMD(do_drink);
ACMD(do_eat);
ACMD(do_pour);
ACMD(do_order);
ACMD(do_follow);
ACMD(do_refollow);
ACMD(do_rent);
ACMD(do_offer);
ACMD(do_advance);
ACMD(do_close);
ACMD(do_open);
ACMD(do_lock);
ACMD(do_unlock);
ACMD(do_exits);
ACMD(do_enter);
ACMD(do_leave);
ACMD(do_write);
ACMD(do_flee);
ACMD(do_sneak);
ACMD(do_hide);
ACMD(do_ambush);
ACMD(do_trap);
ACMD(do_pick);
ACMD(do_bash);
ACMD(do_rescue);
ACMD(do_kick);
ACMD(do_examine);
ACMD(do_info);
ACMD(do_users);
ACMD(do_where);
ACMD(do_levels);
ACMD(do_consider);
ACMD(do_group);
ACMD(do_restore);
ACMD(do_return);
ACMD(do_switch);
ACMD(do_use);
ACMD(do_credits);
ACMD(do_pracreset);
ACMD(do_poofset);
ACMD(do_teleport);
ACMD(do_gecho);
ACMD(do_wiznet);
ACMD(do_invis);
ACMD(do_wimpy);
ACMD(do_wizlock);
ACMD(do_dc);
ACMD(do_gsay);
ACMD(do_title);
ACMD(do_assist);
ACMD(do_report);
ACMD(do_split);
ACMD(do_toggle);
ACMD(do_vnum);
ACMD(do_action);
ACMD(do_practice);
ACMD(do_uptime);
ACMD(do_commands);
ACMD(do_ban);
ACMD(do_unban);
ACMD(do_date);
ACMD(do_zreset);
ACMD(do_gen_ps);
ACMD(do_gen_tog);
ACMD(do_gen_com);
ACMD(do_wizutil);
ACMD(do_color);
ACMD(do_syslog);
ACMD(do_show);
ACMD(do_savebench);
ACMD(do_ungroup);
ACMD(do_page);
ACMD(do_diagnose);
ACMD(do_reload);
ACMD(do_last);
ACMD(do_track);
ACMD(do_shape);
ACMD(do_zone);
ACMD(do_slay);
ACMD(do_set);
ACMD(do_display);
ACMD(do_alias);
ACMD(do_twohand);
ACMD(do_register);
ACMD(do_whois);
ACMD(do_next);
ACMD(do_knock);
ACMD(do_gather_food);
ACMD(do_map);
ACMD(do_findzone);
ACMD(do_setfree);
ACMD(do_ride);
ACMD(do_dismount);
ACMD(do_lead);
ACMD(do_afk);
ACMD(do_search);
ACMD(do_butcher);
ACMD(do_pray);
ACMD(do_light);
ACMD(do_blowout);
ACMD(do_disengage);
ACMD(do_orc_delay);
ACMD(do_affections);
ACMD(do_lose);
ACMD(do_jig);
ACMD(do_fame);
ACMD(do_rank);
ACMD(do_compare);
ACMD(do_wiztrack);
ACMD(do_mental);
ACMD(do_prepare);
ACMD(do_hunt);
ACMD(do_tame);
ACMD(do_calm);
ACMD(do_concentrate);
ACMD(do_specialize);
ACMD(do_pull);
ACMD(do_fish);
ACMD(do_whistle);
ACMD(do_recruit);
ACMD(do_stalk);
ACMD(do_cover);
ACMD(do_exploits);
ACMD(do_delete);
ACMD(do_small_map);
ACMD(do_board);
ACMD(do_petitio);
ACMD(do_namechange);
ACMD(do_retire);
ACMD(do_top);
ACMD(do_grouproll);
ACMD(do_shoot);
ACMD(do_rend);
ACMD(do_mark);
ACMD(do_blinding);
ACMD(do_bite);
ACMD(do_maul);
ACMD(do_bendtime);
ACMD(do_windblast);
ACMD(do_smash);
ACMD(do_cleave);
ACMD(do_overrun);
ACMD(do_frenzy);
ACMD(do_stomp);
ACMD(do_defend);

void do_recover(char_data* character, char* argument, waiting_type* wait_list, int command, int sub_command);

void do_scan(char_data* character, char* argument, waiting_type* wait_list, int command, int sub_command);

void do_details(char_data* character, char* argument, waiting_type* wait_list, int command, int sub_command);

void do_renounce(char_data* character, char* argument, waiting_type* wait_list, int command, int sub_command);

const char* command[] = {
    "north", /* 1 */
    "east",
    "south",
    "west",
    "up",
    "down",
    "enter",
    "exits",
    "flee",
    "get",
    "drink", /* 11 */
    "eat",
    "wear",
    "wield",
    "look",
    "score",
    "say", /* 17, do not shift */
    "yell", /* shout */
    "tell",
    "inventory",
    "qui", /* 21 */
    "", /* bounce */
    "kill",
    "insult",
    "help",
    "who",
    "emote",
    "echo",
    "stand",
    "sit",
    "rest", /* 31 */
    "sleep",
    "wake",
    "force",
    "transfer",
    "news",
    "equipment",
    "buy",
    "sell",
    "value",
    "list", /* 41 */
    "drop",
    "goto",
    "weather",
    "read",
    "pour",
    "grab",
    "remove",
    "put",
    "shutdow",
    "save", /* 51 */
    "hit",
    "petitio", /* string */
    "give",
    "quit",
    "stat",
    "rank", /* skillset */
    "time",
    "load",
    "purge",
    "shutdown", /* 61 */
    "compare", /* idea */
    "wiztrack", /* typo */
    "sing", /* bug */
    "whisper",
    "cast",
    "at",
    "ask",
    "order",
    "sip",
    "taste", /* 71 */
    "snoop",
    "follow",
    "rent",
    "offer",
    "advance",
    "open",
    "close",
    "lock",
    "unlock",
    "leave", /* 81 */
    "write",
    "hold",
    "hold", /* never used */ /* 84 */
    "sneak",
    "hide",
    "ambush",
    "pick",
    "behead", /* steal */
    "bash",
    "rescue", /* 91 */
    "kick",
    "practice",
    "examine",
    "take",
    "info",
    "'",
    "practise",
    "use",
    "where",
    "levels", /* 101 */
    "wizutil", /* reroll */
    ":",
    "wizset",
    "wizlist",
    "consider",
    "group",
    "restore",
    "return",
    "switch",
    "", /* quaff */ /* 111 */
    "", /* recite */
    "users",
    "", /* immlist */
    "afk", /* noshout */
    "wizhelp",
    "credits",
    "petition", /* compact */
    "pracreset", /* display */
    "poofin",
    "poofout", /* 121 */
    "teleport",
    "gecho",
    "wiznet", /* don't move */
    "search", /* holylight */
    "invis",
    "specialize",
    "set",
    "ungroup",
    "pray", /* notell */
    "wizlock", /* 131 */
    "jig", /* junk */
    "", /* gsay */
    "hunt", /* pardon */
    "disengage", /* murder */
    "alias", /* title */
    ";", /* wiznet */
    "balance",
    "deposit",
    "withdraw",
    "", /* visible */ /* 141 */
    "twohanded", /* quest */
    "concentrate", /* freeze */
    "dc",
    "butcher",
    "assist",
    "split",
    "gtell",
    "reply",
    "onehanded", /* norepeat */
    "knock", /* toggle */ /* 151 */
    "map",
    "check",
    "receive",
    "mail",
    "register", /* holler */
    "vnum",
    "whois", /* fill */
    "uptime",
    "commands",
    "socials", /* 161 */
    "ban",
    "unban",
    "date",
    "zreset",
    "colour",
    "show",
    "next", /* prompt */
    "handbook",
    "policy",
    "", /* 171 */
    "manual", /* nogossip */
    "findzone", /* noauction */
    "setfree", /* nograts */
    "ride", /* roomflags */
    "noshout",
    "dismount", /* nowiz */
    "pull", /* notitle */
    "whistle", /* thaw */
    "swing", /* unaffect */
    "cls", /* 181 */
    "clear",
    "version",
    "chat", /* gossip */
    "narrate", /* auction */
    "affections", /* grats */
    "", /* donate */
    "page",
    "report",
    "diagnose",
    "reload", /* 191 */
    "syslog",
    "last",
    "slowns",
    "track",
    "whoami",
    "vstat",
    "beep",
    "lose",
    "shape",
    "zone", /* 201 */
    "slay",
    "gather",
    "will",
    "prepare",
    "light",
    "blowout",
    "fame",
    "trash", /* reserved for orc delay */
    "tame",
    "calm", /* 211 */
    "fish",
    "recruit",
    "refollow",
    "stalk",
    "cover",
    "lead",
    "exploits",
    "trophy",
    "trap", /* "trap",   */
    "", /* 221 */
    "",
    "",
    "obj2html",
    "delete",
    "world",
    "top",
    "groll",
    "shoot",
    "recover", // 230
    "retrieve",
    "scan",
    "details",
    "bite",
    "rend", // 235
    "maul",
    "mark",
    "blind",
    "bend",
    "windblast", // 240
    "smash",
    "frenzy",
    "stomp",
    "cleave",
    "overrun", // 245
    "defend",
    "renounce",
    "mob2csv",
    "savebench",
    "\n"
};

/* CEND: search for me when you're looking for the end of the cmd list! :) */

/* these words are effectively ignored */
char* fill[] = {
    "in",
    "from",
    "with",
    "the",
    "on",
    "at",
    "to",
    "\n"
};

int search_block(char* arg, char** list, char exact)
{
    register int i, l;

    /* Make into lower case, and get length of string */
    for (l = 0; *(arg + l); l++)
        *(arg + l) = LOWER(*(arg + l));

    if (exact) {
        for (i = 0; **(list + i) != '\n'; i++)
            if (!strcmp(arg, *(list + i)))
                return i;
    } else {
        if (!l)
            l = 1; /* Avoid "" to match the first available string */
        for (i = 0; **(list + i) != '\n'; i++)
            if (!strncmp(arg, *(list + i), l))
                return i;
    }

    return -1;
}

int old_search_block(char* argument, int begin, unsigned int length, const char** list, int mode)
{
    unsigned int guess, found, search;

    /* If the word contain 0 letters, then a match is already found */
    found = (length < 1);

    guess = 0;

    /* Search for a match */
    if (mode)
        while (!found && *(list[guess]) != '\n') {
            found = (length == strlen(list[guess]));
            for (search = 0; (search < length) && found; search++)
                found = (*(argument + begin + search) == *(list[guess] + search));
            guess++;
        }
    else {
        while (!found && *(list[guess]) != '\n') {
            found = 1;
            for (search = 0; (search < length) && found; search++)
                found = (*(argument + begin + search) == *(list[guess] + search));
            guess++;
        }
    }

    if (found)
        return guess;
    else
        return -1;
}

void report_wrong_position(struct char_data* ch)
{
    switch (GET_POS(ch)) {
    case POSITION_DEAD:
        send_to_char("Lie still; you are DEAD!!! :-( \n\r", ch);
        break;

    case POSITION_INCAP:
        send_to_char("You are in a pretty bad shape, unable to do anything!\n\r", ch);
        break;

    case POSITION_STUNNED:
        send_to_char("All you can do right now is think about the stars!\n\r", ch);
        break;

    case POSITION_SLEEPING:
        send_to_char("In your dreams, or what?\n\r", ch);
        break;

    case POSITION_RESTING:
        send_to_char("Nah... You feel too relaxed to do that..\n\r", ch);
        break;

    case POSITION_SITTING:
        send_to_char("Maybe you should get on your feet first?\n\r", ch);
        break;

    case POSITION_FIGHTING:
        send_to_char("No way!  You're fighting for your life!\n\r", ch);
        break;
    }
}

void target_data::cleanup()
{
    if (type == TARGET_TEXT)
        put_to_txt_block_pool(ptr.text);
    ptr.other = 0;
    type = TARGET_NONE;
    ch_num = 0;
}

void target_data::operator=(target_data t2)
{
    cleanup();
    if (t2.type == TARGET_TEXT) {
        ptr.text = get_from_txt_block_pool();
        strcpy(ptr.text->text, t2.ptr.text->text);
    } else
        ptr.other = t2.ptr.other;

    type = t2.type;
    ch_num = t2.ch_num;
    choice = t2.choice;
}

int target_data::operator==(target_data t2)
{
    if ((type == t2.type) && (ptr.other == t2.ptr.other) && (ch_num == t2.ch_num))
        return 1;

    return 0;
}

/*
 * The procedure takes the mask of ==possible== arguments,
 * assumes that they are not satisfied, and suggests
 * the correct usage to ch.
 */
void report_wrong_target(struct char_data* ch, int mask, char has_arg)
{
    if (IS_SET(mask, TAR_TEXT_ALL)) {
        send_to_char("Strange. Please report to gods what you just did (1).\n\r",
            ch);
        return;
    }

    /* first, some argument exists. */
    if (has_arg) {
        if (IS_SET(mask, TAR_TEXT)) {
            send_to_char("Strange. You need a better argument for this.\n\r", ch);
            return;
        }

        if (IS_SET(mask, TAR_ALL)) {
            send_to_char("You need to do this to \"all\"\n\r", ch);
            return;
        }
        /* recognizing gold - lame, but who cares */

        if (IS_SET(mask, TAR_CHAR_WORLD)) {
            send_to_char("Nobody by that name.\n\r", ch);
            return;
        }

        if (IS_SET(mask, TAR_CHAR_ROOM)) {
            send_to_char("Nobody here by that name.\n\r", ch);
            return;
        }

        if (IS_SET(mask, TAR_FIGHT_VICT)) {
            send_to_char("You need to fight somebody for this.\n\r", ch);
            return;
        }

        if (IS_SET(mask, TAR_OBJ_ROOM)) {
            send_to_char("Nothing here by that name.\n\r", ch);
            return;
        }

        if (IS_SET(mask, TAR_OBJ_INV)) {
            send_to_char("You don't have that.\n\r", ch);
            return;
        }

        if (IS_SET(mask, TAR_OBJ_EQUIP)) {
            send_to_char("You are not wearing that.\n\r", ch);
            return;
        }

        if (IS_SET(mask, TAR_OBJ_WORLD)) {
            send_to_char("There is nothing by that name.\n\r", ch);
            return;
        }

        if (IS_SET(mask, TAR_GOLD)) {
            send_to_char("You can do that with money only.\n\r", ch);
            return;
        }

        if (IS_SET(mask, TAR_DIR_NAME)) {
            send_to_char("Nothing here by that name.\n\r", ch);
            return;
        }

        if (IS_SET(mask, TAR_DIR_WAY)) {
            send_to_char("What direction is that?\n\r", ch);
            return;
        }

        if (IS_SET(mask, TAR_SELF_ONLY)) {
            send_to_char("You can do it to yourself only.\n\r", ch);
            return;
        }

        if (IS_SET(mask, TAR_IGNORE)) {
            send_to_char("Strange. Please report to gods what you just did (2).\n\r",
                ch);
            return;
        }
    } else { /* no argument */
        if (IS_SET(mask, TAR_NONE_OK)) {
            send_to_char("Strange. Please report to gods what you just did (3).\n\r", ch);
            return;
        }
        if (IS_SET(mask, TAR_TEXT)) {
            send_to_char("You need some argument here.\n\r", ch);
            return;
        }
        if (IS_SET(mask, TAR_FIGHT_VICT)) {
            send_to_char("Your victim is not here!\n\r", ch);
            return;
        }
        if (IS_SET(mask, TAR_SELF) || IS_SET(mask, TAR_SELF_ONLY)) {
            send_to_char("You can do that to self only.\n\r", ch);
            return;
        }
        if (IS_SET(mask, TAR_IGNORE)) {
            send_to_char("You need no argument here.\n\r", ch);
            return;
        }
    }
    send_to_char("You can not do it this way.\n\r", ch);
    return;
}

int target_check_one(struct char_data* ch, int mask, struct target_data* t1)
/*
 * mask is the mask of possible targets,
 * t1 carries the target to check,
 * the proc returns the target status, or 0 if wrong target
 */
{
    int tmp;

    if (t1->type == TARGET_OTHER || t1->type == TARGET_VALUE || t1->type == TARGET_IGNORE)
        return 1;

    if (IS_SET(mask, TAR_IGNORE))
        return TAR_IGNORE;

    if (t1->type == TARGET_NONE) {
        if (IS_SET(mask, TAR_NONE_OK))
            return TAR_NONE_OK;
        if (IS_SET(mask, TAR_IGNORE))
            return TAR_IGNORE;
        return 0;
    }

    if (t1->type == TARGET_CHAR) {
        if (!char_exists(t1->ch_num))
            return 0;
        if (IS_SET(mask, TAR_SELF_NONO) && (t1->ptr.ch == ch))
            return 0;

        if (IS_SET(mask, TAR_SELF_ONLY) && (t1->ptr.ch == ch))
            return TAR_SELF_ONLY;
        if (IS_SET(mask, TAR_SELF) && (t1->ptr.ch == ch))
            return TAR_SELF;

        if (IS_SET(mask, TAR_FIGHT_VICT) && (t1->ptr.ch == ch->specials.fighting)
            && (t1->ptr.ch->in_room == ch->in_room))
            return TAR_FIGHT_VICT;

        if (!CAN_SEE(ch, t1->ptr.ch, (IS_SET(mask, TAR_DARK_OK)) ? 1 : 0) && (t1->ptr.ch != ch))
            return 0;

        if (IS_SET(mask, TAR_CHAR_ROOM) && (t1->ptr.ch->in_room == ch->in_room))
            return TAR_CHAR_ROOM;

        if (IS_SET(mask, TAR_CHAR_WORLD))
            return TAR_CHAR_WORLD;

        return 0;
    }

    if (t1->type == TARGET_OBJ) {
        if (!t1->ptr.obj)
            return 0;

        if (IS_SET(mask, TAR_OBJ_ROOM) && (t1->ptr.obj->in_room == ch->in_room))
            return TAR_OBJ_ROOM;
        if (IS_SET(mask, TAR_OBJ_EQUIP)) {
            for (tmp = 0; tmp < MAX_WEAR; tmp++)
                if (ch->equipment[tmp] == t1->ptr.obj)
                    return TAR_OBJ_EQUIP;
        }
        if (IS_SET(mask, TAR_OBJ_INV) && (t1->ptr.obj->carried_by == ch))
            return TAR_OBJ_INV;

        if (IS_SET(mask, TAR_OBJ_WORLD))
            return TAR_OBJ_WORLD;

        return 0;
    }
    if (t1->type == TARGET_TEXT) {
        if (!t1->ptr.text)
            return 0;

        if (IS_SET(mask, TAR_TEXT))
            return TAR_TEXT;
        if (IS_SET(mask, TAR_TEXT_ALL))
            return TAR_TEXT_ALL;

        return 0;
    }
    if ((t1->type == TARGET_OTHER) || (t1->type == TARGET_DIR) || (t1->type == TARGET_ALL) || (t1->type == TARGET_GOLD) || (t1->type == TARGET_VALUE))
        /* these are not checked */
        return mask;

    return mask;
}

char* target_from_word(struct char_data* ch, char* argument, int mask, struct target_data* t1)
/*
 * This one tries to take a target from argument string.
 * Possible targets are determined from the mask argument.
 * Returns the target in t1 and the remaining string as return value
 */
{
    int tmp, arg_i, tmpvalue;
    char word[MAX_INPUT_LENGTH];
    struct char_data* tmpch;
    struct obj_data* tmpobj;

    /***************************************************
    Okay, here we parse the argument line for two targets, if they are
    there. Priority is, from the highest.
    For no argument:
    TAR_NONE_OK, TARGET_IGNORE
    TAR_FIGHT_VICT
    TAR_SELF, TARGET_SELF_ONLY

    For some argument:
    TAR_TEXT_ALL - sends the whole line as a text argument, as for narrate.
    TAR_TEXT
    TARGET_ALL
    TAR_GOLD
    TAR_CHAR_ROOM
    TAR_CHAR_WORLD
    TAR_OBJ_ROOM
    TAR_OBJ_INV
    TAR_OBJ_EQUIP
    TAR_OBJ_WORLD
    TAR_DIRECTION (NAME, then WAY)
    TAR_VALUE
    **************************************************/

    arg_i = 0;
    while (argument[arg_i] && (argument[arg_i] <= ' '))
        arg_i++;
    t1->ptr.other = 0;
    t1->type = TARGET_NONE;
    t1->choice = TAR_IGNORE;
    t1->ch_num = 0;

    if (IS_SET(mask, TAR_TEXT_ALL)) {
        t1->ptr.text = get_from_txt_block_pool();
        strcpy(GET_TARGET_TEXT(t1), argument + arg_i);
        t1->type = TARGET_TEXT;
        t1->choice = TAR_TEXT_ALL;
        return argument + strlen(argument);
    }

    if (IS_SET(mask, TAR_IGNORE))
        return argument + arg_i;

    if (!argument[arg_i]) {
        if (IS_SET(mask, TAR_NONE_OK))
            return argument + arg_i;
        else if (IS_SET(mask, TAR_FIGHT_VICT) && (ch->specials.fighting)) {
            t1->ptr.ch = ch->specials.fighting;
            t1->ch_num = ch->specials.fighting->abs_number;
            t1->type = TARGET_CHAR;
            t1->choice = TAR_FIGHT_VICT;
            return argument + arg_i;
        } else if (IS_SET(mask, TAR_SELF) || IS_SET(mask, TAR_SELF_ONLY)) {
            t1->ptr.ch = ch;
            t1->ch_num = ch->abs_number;
            t1->type = TARGET_CHAR;
            t1->choice = TAR_SELF;
            return argument + arg_i;
        }
        if (IS_SET(mask, TAR_IGNORE))
            return argument;

        return 0;
    }

    /* some argument exists. parsing the first argument. */
    tmpvalue = 0;
    tmp = arg_i;
    while (isdigit(argument[arg_i])) {
        tmpvalue = tmpvalue * 10 + argument[arg_i] - (int)('0');
        arg_i++;
    }
    if (argument[arg_i] == '.') {
        arg_i = tmp;
        tmpvalue = 0;
    }

    if (argument[arg_i] == '\'') {
        for (tmp = 0, arg_i++; argument[arg_i] && (argument[arg_i] != '\'');
             tmp++, arg_i++)
            word[tmp] = argument[arg_i];
        word[tmp] = 0;

        if (argument[arg_i] == '\'')
            arg_i++;
    } else {
        for (tmp = 0; argument[arg_i] && (argument[arg_i] > ' ');
             tmp++, arg_i++)
            word[tmp] = argument[arg_i];
        word[tmp] = 0;
    }

    if (IS_SET(mask, TAR_TEXT)) {
        t1->ptr.text = get_from_txt_block_pool();
        strcpy(GET_TARGET_TEXT(t1), word);
        t1->type = TARGET_TEXT;
        t1->choice = TAR_TEXT;
        return argument + arg_i;
    }
    if (IS_SET(mask, TAR_ALL) && !strcmp(word, "all")) {
        t1->type = TARGET_ALL;
        t1->ch_num = 0;
        t1->choice = TAR_ALL;
        return argument + arg_i;
    }
    /* recognizing gold. lame */
    if (IS_SET(mask, TAR_GOLD) && tmpvalue && !strcmp(word, "gold")) {
        t1->type = TARGET_GOLD;
        t1->ch_num = tmpvalue * COPP_IN_GOLD;
        t1->choice = TAR_GOLD;
        return argument + arg_i;
    }
    if (IS_SET(mask, TAR_GOLD) && tmpvalue && !strcmp(word, "silver")) {
        t1->type = TARGET_GOLD;
        t1->ch_num = tmpvalue * COPP_IN_SILV;
        t1->choice = TAR_GOLD;
        return argument + arg_i;
    }
    if (IS_SET(mask, TAR_GOLD) && tmpvalue && !strcmp(word, "copper")) {
        t1->type = TARGET_GOLD;
        t1->ch_num = tmpvalue;
        t1->choice = TAR_GOLD;
        return argument + arg_i;
    }
    if (IS_SET(mask, TAR_CHAR_ROOM)) {
        tmpch = get_char_room_vis(ch, word, (IS_SET(mask, TAR_DARK_OK)) ? 1 : 0);
        if (tmpch) {
            t1->ptr.ch = tmpch;
            t1->ch_num = tmpch->abs_number;
            t1->type = TARGET_CHAR;
            t1->choice = TAR_CHAR_ROOM;
            return argument + arg_i;
        }
    }
    if (IS_SET(mask, TAR_CHAR_WORLD)) {
        tmpch = get_char_vis(ch, word, (IS_SET(mask, TAR_DARK_OK)) ? 1 : 0);
        if (tmpch) {
            t1->ptr.ch = tmpch;
            t1->ch_num = tmpch->abs_number;
            t1->choice = TAR_CHAR_WORLD;
            t1->type = TARGET_CHAR;
            return argument + arg_i;
        }
    }
    if (IS_SET(mask, TAR_OBJ_ROOM)) {
        tmpobj = get_obj_in_list(word, world[ch->in_room].contents);
        if (tmpobj) {
            t1->ptr.obj = tmpobj;
            t1->ch_num = 0;
            t1->type = TARGET_OBJ;
            t1->choice = TAR_OBJ_ROOM;
            return argument + arg_i;
        }
    }
    if (IS_SET(mask, TAR_OBJ_INV)) {
        tmpobj = get_obj_in_list(word, ch->carrying);
        if (tmpobj) {
            t1->ptr.obj = tmpobj;
            t1->ch_num = 0;
            t1->type = TARGET_OBJ;
            t1->choice = TAR_OBJ_INV;
            return argument + arg_i;
        }
    }
    if (IS_SET(mask, TAR_OBJ_EQUIP)) {
        for (tmp = 0; tmp < MAX_WEAR; tmp++)
            if (ch->equipment[tmp] && isname(word, ch->equipment[tmp]->name)) {
                t1->ptr.obj = ch->equipment[tmp];
                t1->ch_num = 0;
                t1->type = TARGET_OBJ;
                t1->choice = TAR_OBJ_EQUIP;
                return argument + arg_i;
            }
    }
    if (IS_SET(mask, TAR_OBJ_WORLD)) {
        tmpobj = get_obj(word);
        if (tmpobj) {
            t1->ptr.obj = tmpobj;
            t1->ch_num = 0;
            t1->type = TARGET_OBJ;
            t1->choice = TAR_OBJ_WORLD;
            return argument + arg_i;
        }
    }
    if (IS_SET(mask, TAR_DIR_NAME)) {
        for (tmp = 0; tmp < NUM_OF_DIRS; tmp++)
            if (EXIT(ch, tmp) && !str_cmp(EXIT(ch, tmp)->keyword, word)) {
                t1->ch_num = tmp;
                t1->type = TARGET_DIR;
                t1->choice = TAR_DIR_NAME;
                return argument + arg_i;
            }
    }
    if (IS_SET(mask, TAR_DIR_WAY)) {
        for (tmp = 0; tmp < NUM_OF_DIRS; tmp++)
            if (!strncmp(dirs[tmp], word, strlen(word))) {
                t1->ch_num = tmp;
                t1->type = TARGET_DIR;
                t1->choice = TAR_DIR_WAY;
                return argument + arg_i;
            }
    }
    if (IS_SET(mask, TAR_VALUE) && (isdigit(*word) || (*word == '-'))) {
        tmp = atoi(word);
        t1->ch_num = tmp;
        t1->type = TARGET_VALUE;
        t1->choice = TAR_VALUE;
        return argument + arg_i;
    }

    /* wrong target */
    return 0;
}

int target_check(struct char_data* ch, int cmd, struct target_data* t1,
    struct target_data* t2)
{
    struct command_info* this_command;
    int tmp, tc, res, last_tc, check, last_check;

    this_command = &cmd_info[cmd];
    last_check = last_tc = check = res = 0;

    for (tc = 0; tc < 32; tc++) {
        if (cmd_info[cmd].target_mask[tc]) {
            check = target_check_one(ch, 1 << tc, t1);

            if (check) {
                res = target_check_one(ch, cmd_info[cmd].target_mask[tc], t2) != 0;
                last_check = check;
                last_tc = tc;
            } else
                res = 0;

            if (res)
                return 1;
        }
    }

    if (!last_check) { /* couldn't parse the first argument */
        for (tc = 0, tmp = 0; tc < 32; tc++)
            if (cmd_info[cmd].target_mask[tc])
                tmp |= 1 << tc;

        if (t1->type != TARGET_OTHER)
            tmp = t1->choice;

        report_wrong_target(ch, tmp, (t1->type == TARGET_NONE) ? 0 : 1);
    } else {
        if (t2->type != TARGET_OTHER)
            tmp = t2->choice;
        else
            tmp = cmd_info[cmd].target_mask[last_tc];

        report_wrong_target(ch, tmp, (t2->type == TARGET_NONE) ? 0 : 1);
    }

    return 0;
}

int target_parser(struct char_data* ch, int cmd, char* argument,
    struct target_data* t1, struct target_data* t2)
{
    struct command_info* this_command;
    int tmp, tc, res, last_tc = 0, target_modif;
    char *newarg, *last_newarg;

    this_command = &cmd_info[cmd];

    t1->ptr.other = 0;
    t1->type = TARGET_NONE;
    t1->ch_num = 0;
    t2->ptr.other = 0;
    t2->type = TARGET_NONE;
    t2->ch_num = 0;

    /** Selecting TARGET1  **/
    target_modif = 0;
    newarg = last_newarg = NULL;

    if (cmd_info[cmd].target_mask[20])
        target_modif = TAR_DARK_OK;
    if (cmd_info[cmd].target_mask[21])
        target_modif |= TAR_IN_OTHER;
    for (tc = 0; tc < 32; tc++)
        if (cmd_info[cmd].target_mask[tc]) {
            newarg = target_from_word(ch, argument, (1 << tc) | target_modif, t1);

            if (newarg) {
                res = (target_from_word(ch, newarg, cmd_info[cmd].target_mask[tc], t2) != 0);
                last_newarg = newarg;
                last_tc = tc;
            } else
                res = 0;

            if (res)
                return 1;
        }
    if (!last_newarg) { /* could not parse the first argument */
        while (*argument && (*argument <= ' '))
            argument++;
        for (tc = 0, tmp = 0; tc < 32; tc++)
            if (cmd_info[cmd].target_mask[tc])
                tmp |= (1 << tc);
        report_wrong_target(ch, tmp, (*argument) ? 1 : 0);
    } else
        report_wrong_target(ch, cmd_info[cmd].target_mask[last_tc],
            (*last_newarg) ? 1 : 0);
    return 0;
}

void replace_aliases(struct char_data* ch, char* line)
{
    char new_line[MAX_INPUT_LENGTH * 2];
    int begin, tmp;
    struct alias_list* list;

    for (begin = 0; (*(line + begin) == ' '); begin++)
        ;
    new_line[0] = 0;

    if (ch->specials.alias) {
        for (list = ch->specials.alias; list; list = list->next) {
            tmp = strlen(list->keyword);
            if (!strncmp(line + begin, list->keyword, tmp) && *(line + begin + tmp) <= ' ')
                break;
        }
        if (list) {
            strcpy(new_line, list->command);
            begin += strlen(list->keyword);
        }
    }

    strcat(new_line, line + begin);
    new_line[MAX_INPUT_LENGTH - 1] = 0;
    strcpy(line, new_line);
}

void command_interpreter(struct char_data* ch, char* argument_chr,
    struct waiting_type* argument_wtl)
{
    int look_at, cmd, subcmd, begin, mode, may_not_perform;
    char argument[MAX_INPUT_LENGTH];
    char local_buf[MAX_INPUT_LENGTH];
    char* argument_raw = "";
    struct waiting_type *argument_info, interp_argument_info;
    extern int no_specials;

    bzero((char*)&interp_argument_info, sizeof(waiting_type));
    look_at = begin = mode = subcmd = 0;

    /* should only happen if someone other than ch causes this function call */
    if (ch->delay.wait_value > 0) {
        send_to_char("You are busy with something else.\n\r", ch);
        return;
    }

    if (argument_wtl) {
        argument_info = argument_wtl;
        argument_raw = "";
        mode = 1;
    } else {
        argument_raw = argument_chr;
        argument_info = &interp_argument_info;
        mode = 0;
        /* are they AFK? remove the flag */
        if (IS_SET(PLR_FLAGS(ch), PLR_ISAFK)) {
            REMOVE_BIT(PLR_FLAGS(ch), PLR_ISAFK);
            send_to_char("You return to your keyboard.\n\r", ch);

            // Let Big Brother know that the character is back.
            game_rules::big_brother& bb_instance = game_rules::big_brother::instance();
            bb_instance.on_character_returned(ch);
        }
    }

    /* there's nothing special going on with this guy */
    if (!mode) {
        bzero(argument, MAX_INPUT_LENGTH);

        /* Find first non blank */
        for (begin = 0; (*(argument_raw + begin) == ' '); begin++)
            ;
        strcpy(argument, argument_raw + begin);
        begin = 0;

        /* Use do_shape level */
        if ((*(argument + begin) == '/') && (GET_LEVEL(ch) >= cmd_info[CMD_SHAPE].minimum_level) && !(RETIRED(ch))) {
            shape_center(ch, argument + begin + 1);
            return;
        }

        /*
         * Already shaping.  Don't check for retirement.  If they're in here
         * somehow they have to be able to get out.
         */
        if ((ch->specials.position == POSITION_SHAPING) && (GET_LEVEL(ch) >= cmd_info[CMD_SHAPE].minimum_level)) { /* do_shape */
            shape_center(ch, argument_raw);
            return;
        }

        /* special checks required for shortcut tokenized commands */
        if (*(argument + begin) == '\'') {
            cmd = CMD_SAY;
            *(argument + begin) = ' ';
            look_at = 0;
        } else if (*(argument + begin) == ';') {
            cmd = CMD_WIZNET;
            *(argument + begin) = ' ';
            look_at = 0;
        } else if (*(argument + begin) == ',') {
            cmd = CMD_EMOTE;
            *(argument + begin) = ' ';
            look_at = 0;
        }
        /* they entered a blank line */
        else if (*(argument + begin) == 0)
            return;
        else {
            /* get lower case letters and length */
            for (look_at = 0; *(argument + begin + look_at) > ' '; look_at++)
                *(argument + begin + look_at) = LOWER(*(argument + begin + look_at));

            cmd = old_search_block(argument, begin, look_at, command, 0);
        }
    } else {
        /* if you're hazed, you have a 10% chance of forgetting targ1 */
        if (IS_AFFECTED(ch, AFF_HAZE))
            if (!number(0, 9)) {
                argument_info->targ1.cleanup();
                argument_info->targ2.cleanup();
            }

        cmd = argument_info->cmd;
        subcmd = argument_info->subcmd;
        if (!target_check(ch, cmd, &argument_info->targ1, &argument_info->targ2))
            return;
    }

    /* lacking minimum level requirement */
    if (cmd > 0 && GET_LEVEL(ch) < cmd_info[cmd].minimum_level)
        cmd = -1;

    /* retired players can't use this command */
    if (cmd > 0 && !cmd_info[cmd].retired_allowed && RETIRED(ch))
        cmd = -1;

    if (cmd <= 0) {
        one_argument(argument + begin, local_buf);
        subcmd = find_action(local_buf);
        if (subcmd >= 0)
            cmd = CMD_SOCIAL;
        else
            cmd = -1;
    }
    if (cmd <= 0) {
        send_to_char("Unrecognized command.\n\r", ch);
        return;
    }

    /*
     * unhide hiders; commands with CMD_MASK_NO_UNHIDE shouldn't
     * unhide people.  If someone snuck into a room (and therefore
     * has the small hide value and SNUCK_IN hide_flag), they
     * should not be unhidden if they're:
     *  - still hidden from their initial entrance
     *  - they're using the hide command
     * Additionally, since practi{c,s}e can be used to either list
     * information (a case that shouldn't unhide you) or cause your
     * character to do very noticeable, physical activity (a case
     * that should unhide you), we check for it specially.
     */
    if (cmd_info[cmd].minimum_position > POSITION_SLEEPING)
        if (!IS_SET(cmd_info[cmd].mask, CMD_MASK_NO_UNHIDE) && !(cmd == CMD_HIDE && IS_SET(ch->specials2.hide_flags, HIDING_SNUCK_IN)) && !((cmd == CMD_PRACTICE || cmd == CMD_PRACTISE) && !*(argument + begin + look_at)))
            stop_hiding(ch, TRUE);

    /* is ch frozen? imps can't be frozen.. */
    if (!IS_NPC(ch) && PLR_FLAGGED(ch, PLR_FROZEN) && GET_LEVEL(ch) < LEVEL_IMPL) {
        send_to_char("You try, but are restrained and cannot move...\n\r", ch);
        return;
    }

    /* now we execute the function */
    if (cmd > 0 && (cmd_info[cmd].command_pointer)) {
        /* are we in the right position? */
        if (GET_POS(ch) < cmd_info[cmd].minimum_position)
            report_wrong_position(ch);
        else {
            may_not_perform = 0;
            if (!mode) { /* Parse the target into ch->delay */
                argument_info->wait_value = 0;
                argument_info->cmd = cmd;
                argument_info->subcmd = (cmd == CMD_SOCIAL) ? subcmd : cmd_info[cmd].subcmd;
                may_not_perform = target_parser(ch, cmd, argument + begin + look_at,
                    &argument_info->targ1,
                    &argument_info->targ2);
                may_not_perform = !may_not_perform;
            }
            if (!no_specials && !may_not_perform) {
                may_not_perform |= special(ch, cmd, argument + begin + look_at,
                    SPECIAL_COMMAND, argument_info, NOWHERE);
                if (cmd != CMD_SELL) /* TEMPORARY, to keep CMD_SELL from crashing */
                    may_not_perform |= special(ch, cmd, argument + begin + look_at,
                        SPECIAL_TARGET, argument_info, NOWHERE);
                else /* also temp */
                    may_not_perform = 1;
            }

            /*
             * if the command has a target of TAR_CHAR_ROOM, a
             * move penalty applies.  this is to keep people from
             * spamming things like 'kill uruk' or 'pat uruk'
             */

            if (!may_not_perform) {
                /* execute the command */
                ((*cmd_info[cmd].command_pointer)(ch, argument + begin + look_at, argument_info, cmd,
                    mode ? subcmd : cmd_info[cmd].subcmd));
            }
        }
        if (!mode) {
            argument_info->targ1.cleanup();
            argument_info->targ2.cleanup();
        }
        return;
    }

    if (cmd > 0 && (cmd_info[cmd].command_pointer == 0))
        send_to_char("Sorry, but that command has yet to be implemented...\n\r", ch);
    else
        send_to_char("Huh?!?\n\r", ch);
}

void argument_interpreter(char* argument, char* first_arg, char* second_arg)
{
    int look_at, found, begin;

    found = begin = 0;

    do {
        /* Find first non blank */
        for (; *(argument + begin) == ' '; begin++)
            ;

        /* Find length of first word */
        for (look_at = 0; *(argument + begin + look_at) > ' '; look_at++)
            /* Make all letters lower case, AND copy them to first_arg */
            *(first_arg + look_at) = LOWER(*(argument + begin + look_at));

        *(first_arg + look_at) = '\0';
        begin += look_at;
    } while (fill_word(first_arg));

    do {
        /* Find first non blank */
        for (; *(argument + begin) == ' '; begin++)
            ;

        /* Find length of first word */
        for (look_at = 0; *(argument + begin + look_at) > ' '; look_at++)
            /* Make all letters lower case, AND copy them to second_arg */
            *(second_arg + look_at) = LOWER(*(argument + begin + look_at));

        *(second_arg + look_at) = '\0';
        begin += look_at;
    } while (fill_word(second_arg));
}

int is_number(char* str)
{
    int look_at;

    if (*str == '\0')
        return (0);

    for (look_at = 0; *(str + look_at) != '\0'; look_at++)
        if ((*(str + look_at) < '0') || (*(str + look_at) > '9'))
            return 0;

    return 1;
}

char* one_argument(char* argument, char* first_arg)
/*
 * find the first sub-argument of a string, return pointer to first char
 * in primary argument, following the sub-arg
 */
{
    int found, begin, look_at;

    found = begin = 0;

    do {
        /* Find first non blank */
        for (; isspace(*(argument + begin)); begin++)
            ;

        /* Find length of first word */
        /* Make all letters lower case, AND copy them to first_arg */
        if (*(argument + begin) == '\'') {
            for (look_at = 0; *(argument + begin + look_at) && (*(argument + begin + look_at) != '\''); look_at++)
                *(first_arg + look_at) = LOWER(*(argument + begin + look_at));

            if (*(argument + begin + look_at) == '\'')
                look_at++;
        } else {
            for (look_at = 0; *(argument + begin + look_at) > ' '; look_at++)
                *(first_arg + look_at) = LOWER(*(argument + begin + look_at));
        }

        *(first_arg + look_at) = '\0';
        begin += look_at;
    } while (fill_word(first_arg));

    return (argument + begin);
}

int fill_word(char* argument)
{
    return (search_block(argument, fill, TRUE) >= 0);
}

int is_abbrev(char* arg1, char* arg2)
/* determine if a given string is an abbreviation of another string */
{
    if (!*arg1)
        return (0);

    for (; *arg1; arg1++, arg2++)
        if (LOWER(*arg1) != LOWER(*arg2))
            return (0);

    return (1);
}

/* return first 'word' plus trailing substring of input string */
void half_chop(char* string, char* arg1, char* arg2)
{
    for (; isspace(*string); string++)
        ;

    if (*string == '\'') {
        for (string++; (*string != '\'') && *string; string++, arg1++)
            *arg1 = *string;

        if (*string == '\'')
            string++;
    } else
        for (; !isspace(*string) && *string; string++, arg1++)
            *arg1 = *string;

    *arg1 = '\0';

    for (; isspace(*string); string++)
        ;

    for (; (*arg2 = *string); string++, arg2++)
        ;
}

void* virt_program_number(int);

int activate_char_special(char_data* character, char_data* victim, int cmd, char* argument,
    int callflag, waiting_type* wait_data, int in_room)
{
    special_func tmp_func;

    if (IS_MOB(character)) {
        tmp_func = mob_index[character->nr].func;
        if (tmp_func && (IS_SET(character->specials2.act, MOB_SPEC) && !no_specials)) {
            if (tmp_func(character, victim, cmd, argument, callflag, wait_data)) {
                return 1;
            }
        } else if (character->specials.store_prog_number) {
            tmp_func = (special_func)virt_program_number(character->specials.store_prog_number);
            if (tmp_func && tmp_func(character, victim, cmd, argument, callflag, wait_data)) {
                return 1;
            }
        } else if (character->specials.union1.prog_number) {
            if (intelligent(character, victim, cmd, argument, callflag, wait_data)) {
                return 1;
            }
        }
    } else if (!IS_NPC(character) && character->specials.store_prog_number) {
        tmp_func = (special_func)virt_program_number(character->specials.store_prog_number);
        if (tmp_func && tmp_func(character, victim, cmd, argument, callflag, wait_data)) {
            return 1;
        }
    }

    return 0;
}

void* virt_obj_program_number(int);

int activate_obj_special(struct obj_data* host, struct char_data* ch, int cmd,
    char* arg, int callflag, struct waiting_type* wtl)
{
    SPECIAL(*tmpfunc);

    if ((void*)(obj_index[host->item_number].func)) {
        if ((*obj_index[host->item_number].func)((struct char_data*)(host), ch, cmd, arg, callflag, wtl))
            return 1;
    } else if (host->obj_flags.prog_number) {
        tmpfunc = (SPECIAL(*))virt_obj_program_number(host->obj_flags.prog_number);
        if (tmpfunc((char_data*)(host), ch, cmd, arg, callflag, wtl))
            return 1;
    }
    return 0;
}

int special(struct char_data* ch, int cmd, char* arg, int callflag,
    struct waiting_type* wtl, int in_room)
{
    register struct obj_data* i;
    register struct char_data* k;
    int j, remote_mode;
    struct char_data* tmpch;

    if (in_room != ch->in_room)
        remote_mode = 1;
    else
        remote_mode = 0;

    if (in_room == NOWHERE) {
        in_room = ch->in_room;
        remote_mode = 0;
    }

    if (in_room == NOWHERE)
        return FALSE;

    if (callflag == SPECIAL_DEATH) {
        /* in this case, specials are called on ch itself */
        if (activate_char_special(ch, ch, cmd, arg, callflag, wtl, in_room))
            return 1;
        return 0;
    }

    if ((callflag == SPECIAL_TARGET) || (callflag == SPECIAL_DAMAGE)) { /* instead of room, checking targets */
        if (!wtl)
            return 0;

        switch (wtl->targ1.type) {
        case TARGET_ROOM:
            if (!wtl->targ1.ptr.room)
                break;
            if ((void*)(wtl->targ1.ptr.room->funct))
                if ((*wtl->targ1.ptr.room->funct)(ch, 0, cmd, arg, callflag, 0))
                    return 1;
            break;

        case TARGET_OBJ:
            if (wtl->targ1.ptr.obj->item_number < 0)
                break;
            if ((void*)(obj_index[wtl->targ1.ptr.obj->item_number].func))
                if ((*obj_index[wtl->targ1.ptr.obj->item_number].func)(wtl->targ1.ptr.ch, ch, cmd, arg, callflag, wtl))
                    return 1;
            break;

        case TARGET_CHAR:
            tmpch = wtl->targ1.ptr.ch;
            if (!char_exists(wtl->targ1.ch_num))
                break;
            if (activate_char_special(tmpch, ch, cmd, arg, callflag, wtl, in_room))
                return 1;
            break;
        }

        switch (wtl->targ2.type) {
        case TARGET_ROOM:
            if (!wtl->targ2.ptr.room)
                break;
            if ((void*)(wtl->targ2.ptr.room->funct))
                if ((*wtl->targ2.ptr.room->funct)(ch, 0, cmd, arg, callflag, 0))
                    return 1;
            break;

        case TARGET_OBJ:
            if (wtl->targ2.ptr.obj->item_number < 0)
                break;
            if ((void*)(obj_index[wtl->targ2.ptr.obj->item_number].func))
                if ((*obj_index[wtl->targ2.ptr.obj->item_number].func)(wtl->targ2.ptr.ch, ch, cmd, arg, callflag, wtl))
                    return 1;
            break;

        case TARGET_CHAR:
            tmpch = wtl->targ2.ptr.ch;
            if (!char_exists(wtl->targ2.ch_num))
                break;
            if (activate_char_special(tmpch, ch, cmd, arg, callflag, wtl, in_room))
                return 1;
            break;
        }
        return 0;
    }

    /* special in room? */
    if ((void*)(world[in_room].funct))
        if ((*world[in_room].funct)(ch, ch, cmd, arg, callflag, 0))
            return (1);

    if (!remote_mode) {
        /* special in equipment list? */
        for (j = 0; j <= (MAX_WEAR - 1); j++)
            if (ch->equipment[j] && ch->equipment[j]->item_number >= 0)
                if (activate_obj_special(ch->equipment[j], ch, cmd, arg, callflag, wtl))
                    return (1);

        /* special in inventory? */
        for (i = ch->carrying; i; i = i->next_content)
            if (i->item_number >= 0)
                if (activate_obj_special(i, ch, cmd, arg, callflag, wtl))
                    return 1;
    }

    /* special in mobile present? */
    for (k = world[in_room].people; k; k = k->next_in_room)
        if (activate_char_special(k, ch, cmd, arg, callflag, wtl, in_room))
            return 1;

    /* special in object present? */
    for (i = world[in_room].contents; i; i = i->next_content)
        if (i->item_number >= 0)
            if (obj_index[i->item_number].func)
                if ((*obj_index[i->item_number].func)((struct char_data*)(i), ch,
                        cmd, arg, callflag, wtl))
                    return 1;

    return 0;
}

void assign_command_pointers(void)
{
    int position;

    for (position = 0; position < MAX_CMD_LIST; position++)
        cmd_info[position].command_pointer = 0;

    COMMANDO(1, POSITION_STANDING, do_move, 0, TRUE, 0,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(2, POSITION_STANDING, do_move, 0, TRUE, 0,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(3, POSITION_STANDING, do_move, 0, TRUE, 0,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(4, POSITION_STANDING, do_move, 0, TRUE, 0,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(5, POSITION_STANDING, do_move, 0, TRUE, 0,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(6, POSITION_STANDING, do_move, 0, TRUE, 0,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(7, POSITION_STANDING, do_enter, 0, TRUE, 0,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(8, POSITION_RESTING, do_exits, 0, TRUE, 0,
        FULL_TARGET, FULL_TARGET, CMD_MASK_NO_UNHIDE);
    COMMANDO(9, POSITION_FIGHTING, do_flee, 0, TRUE, 0,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(10, POSITION_RESTING, do_get, 0, TRUE, 0,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(11, POSITION_RESTING, do_drink, 0, TRUE, SCMD_DRINK,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(12, POSITION_RESTING, do_eat, 0, TRUE, SCMD_EAT,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(13, POSITION_RESTING, do_wear, 0, TRUE, 0,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(14, POSITION_RESTING, do_wield, 0, TRUE, 0,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(15, POSITION_RESTING, do_look, 0, TRUE, 0,
        FULL_TARGET, FULL_TARGET, CMD_MASK_NO_UNHIDE);
    COMMANDO(16, POSITION_DEAD, do_score, 0, TRUE, 0,
        FULL_TARGET, FULL_TARGET, CMD_MASK_NO_UNHIDE);
    COMMANDO(17, POSITION_RESTING, do_say, 0, TRUE, 0,
        TAR_TEXT_ALL, FULL_TARGET, 1);
    COMMANDO(18, POSITION_RESTING, do_gen_com, 0, TRUE, SCMD_YELL,
        TAR_TEXT_ALL, FULL_TARGET, 0);
    COMMANDO(19, POSITION_RESTING, do_tell, 0, TRUE, SCMD_TELL,
        TAR_CHAR_WORLD | TAR_DARK_OK, TAR_TEXT_ALL, 0);
    COMMANDO(20, POSITION_DEAD, do_inventory, 0, TRUE, 0,
        FULL_TARGET, FULL_TARGET, CMD_MASK_NO_UNHIDE);
    COMMANDO(21, POSITION_DEAD, do_quit, 0, TRUE, 0,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(22, POSITION_DEAD, do_action, 0, TRUE, 0,
        TAR_CHAR_ROOM | TAR_NONE_OK, FULL_TARGET, 0);
    COMMANDO(23, POSITION_FIGHTING, do_hit, 0, TRUE, SCMD_HIT,
        FULL_TARGET, FULL_TARGET, CMD_MASK_MOVE_PENALTY);
    COMMANDO(24, POSITION_RESTING, do_insult, 0, TRUE, 0,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(25, POSITION_DEAD, do_help, 0, TRUE, 0,
        FULL_TARGET, FULL_TARGET, CMD_MASK_NO_UNHIDE);
    COMMANDO(26, POSITION_DEAD, do_who, 0, TRUE, 0,
        FULL_TARGET, FULL_TARGET, CMD_MASK_NO_UNHIDE);
    COMMANDO(27, POSITION_DEAD, do_emote, 1, TRUE, 0,
        TAR_TEXT_ALL, FULL_TARGET, 0);
    COMMANDO(28, POSITION_SLEEPING, do_echo, LEVEL_AREAGOD, TRUE, 0,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(29, POSITION_SLEEPING, do_stand, 0, TRUE, 0,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(30, POSITION_RESTING, do_sit, 0, TRUE, 0,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(31, POSITION_RESTING, do_rest, 0, TRUE, 0,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(32, POSITION_SLEEPING, do_sleep, 0, TRUE, 0,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(33, POSITION_SLEEPING, do_wake, 0, TRUE, 0,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(34, POSITION_SLEEPING, do_force, LEVEL_GOD + 1, FALSE, 0,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(35, POSITION_SLEEPING, do_trans, LEVEL_GOD + 1, FALSE, 0,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(36, POSITION_SLEEPING, do_board, 0, TRUE, SCMD_NEWS,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(37, POSITION_SLEEPING, do_equipment, 0, TRUE, 0,
        FULL_TARGET, FULL_TARGET, CMD_MASK_NO_UNHIDE);
    COMMANDO(38, POSITION_STANDING, do_not_here, 0, TRUE, 0,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(39, POSITION_STANDING, do_not_here, 0, TRUE, 0,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(40, POSITION_STANDING, do_not_here, 0, TRUE, 0,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(41, POSITION_STANDING, do_not_here, 0, TRUE, 0,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(42, POSITION_RESTING, do_drop, 0, TRUE, 0,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(43, POSITION_SLEEPING, do_goto, LEVEL_IMMORT, FALSE, 0,
        TAR_TEXT_ALL, FULL_TARGET, 0);
    COMMANDO(44, POSITION_RESTING, do_weather, 0, TRUE, 0,
        FULL_TARGET, FULL_TARGET, CMD_MASK_NO_UNHIDE);
    COMMANDO(45, POSITION_SLEEPING, do_read, 0, TRUE, 0,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(46, POSITION_STANDING, do_pour, 0, TRUE, SCMD_POUR,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(47, POSITION_RESTING, do_grab, 0, TRUE, 0,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(48, POSITION_RESTING, do_remove, 0, TRUE, 0,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(49, POSITION_RESTING, do_put, 0, TRUE, 0,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(50, POSITION_DEAD, do_shutdown, LEVEL_GRGOD, FALSE, 0,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(51, POSITION_SLEEPING, do_save, LEVEL_GOD, FALSE, 0,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(52, POSITION_FIGHTING, do_hit, 0, TRUE, SCMD_HIT,
        FULL_TARGET, FULL_TARGET, CMD_MASK_MOVE_PENALTY);
    COMMANDO(53, POSITION_SLEEPING, do_petitio, LEVEL_GRGOD, TRUE, 0,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(54, POSITION_RESTING, do_give, 0, TRUE, 0,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(55, POSITION_DEAD, do_quit, 0, TRUE, SCMD_QUIT,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(56, POSITION_DEAD, do_stat, 0, TRUE, 0,
        TAR_TEXT | TAR_NONE_OK, TAR_IGNORE, CMD_MASK_NO_UNHIDE);
    COMMANDO(57, POSITION_SLEEPING, do_rank, 0, TRUE, 0,
        FULL_TARGET, FULL_TARGET, CMD_MASK_NO_UNHIDE);
    COMMANDO(58, POSITION_DEAD, do_time, 0, TRUE, 0,
        FULL_TARGET, FULL_TARGET, CMD_MASK_NO_UNHIDE);
    COMMANDO(59, POSITION_DEAD, do_load, LEVEL_GOD, FALSE, 0,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(60, POSITION_DEAD, do_purge, LEVEL_GOD, FALSE, 0,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(61, POSITION_DEAD, do_shutdown, LEVEL_GRGOD - 1, FALSE, SCMD_SHUTDOWN,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(62, POSITION_STANDING, do_compare, 0, TRUE, 0,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(63, POSITION_STANDING, do_wiztrack, LEVEL_GOD, FALSE, 0,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(64, POSITION_DEAD, do_gen_com, 0, TRUE, SCMD_SING,
        TAR_TEXT_ALL, FULL_TARGET, 0);
    COMMANDO(65, POSITION_RESTING, do_whisper, 0, TRUE, 0,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(66, POSITION_FIGHTING, do_cast, 1, TRUE, 0,
        TAR_TEXT, FULL_TARGET, CMD_MASK_STAMINA_PENALTY);
    COMMANDO(67, POSITION_DEAD, do_at, LEVEL_GOD + 1, FALSE, 0,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(68, POSITION_RESTING, do_ask, 0, TRUE, 0,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(69, POSITION_RESTING, do_order, 1, TRUE, 0,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(70, POSITION_RESTING, do_drink, 0, TRUE, SCMD_SIP,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(71, POSITION_RESTING, do_eat, 0, TRUE, SCMD_TASTE,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(72, POSITION_DEAD, do_snoop, LEVEL_AREAGOD, FALSE, 0,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(73, POSITION_RESTING, do_follow, 0, TRUE, 0,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(74, POSITION_STANDING, do_rent, 1, TRUE, 0,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(75, POSITION_STANDING, do_not_here, 1, TRUE, 0,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(76, POSITION_DEAD, do_advance, LEVEL_GOD + 2, FALSE, 0,
        FULL_TARGET, FULL_TARGET, 0);

    COMMANDO(77, POSITION_SITTING, do_open, 0, TRUE, 0,
        TAR_OBJ_ROOM | TAR_OBJ_INV | TAR_OBJ_EQUIP, TAR_IGNORE, 0);
    ADD_TARGET(77, TAR_DIR_NAME, TAR_DIR_WAY | TAR_NONE_OK);

    COMMANDO(78, POSITION_SITTING, do_close, 0, TRUE, 0,
        TAR_OBJ_ROOM | TAR_OBJ_INV | TAR_OBJ_EQUIP, TAR_IGNORE, 0);
    ADD_TARGET(78, TAR_DIR_NAME, TAR_DIR_WAY | TAR_NONE_OK);

    COMMANDO(79, POSITION_SITTING, do_lock, 0, TRUE, 0,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(80, POSITION_SITTING, do_unlock, 0, TRUE, 0,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(81, POSITION_STANDING, do_leave, 0, TRUE, 0,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(82, POSITION_RESTING, do_write, 1, TRUE, 0,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(83, POSITION_RESTING, do_grab, 1, TRUE, 0,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(84, POSITION_FIGHTING, do_flee, 1, TRUE, 0,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(85, POSITION_STANDING, do_sneak, 1, TRUE, 0,
        FULL_TARGET, FULL_TARGET, CMD_MASK_NO_UNHIDE);
    COMMANDO(86, POSITION_STANDING, do_hide, 1, TRUE, 0,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(87, POSITION_STANDING, do_ambush, 1, TRUE, 0,
        TAR_CHAR_ROOM | TAR_TEXT | TAR_NONE_OK, TAR_IGNORE,
        CMD_MASK_NO_UNHIDE);
    COMMANDO(88, POSITION_STANDING, do_pick, 1, TRUE, 0,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(89, POSITION_STANDING, do_butcher, 0, TRUE, SCMD_SCALP,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(90, POSITION_DEAD, do_bash, 1, TRUE, 0,
        TAR_NONE_OK | TAR_CHAR_ROOM | TAR_FIGHT_VICT | TAR_DIR_NAME,
        TAR_IGNORE, CMD_MASK_MOVE_PENALTY);
    COMMANDO(91, POSITION_FIGHTING, do_rescue, 1, TRUE, 0,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(92, POSITION_FIGHTING, do_kick, 1, TRUE, SCMD_KICK,
        TAR_FIGHT_VICT | TAR_CHAR_ROOM, TAR_IGNORE, CMD_MASK_MOVE_PENALTY);
    COMMANDO(93, POSITION_RESTING, do_practice, 1, TRUE, 0,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(94, POSITION_RESTING, do_examine, 0, TRUE, 0,
        FULL_TARGET, FULL_TARGET, CMD_MASK_NO_UNHIDE);
    COMMANDO(95, POSITION_RESTING, do_get, 0, TRUE, 0,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(96, POSITION_DEAD, do_info, 0, TRUE, 0,
        FULL_TARGET, FULL_TARGET, CMD_MASK_NO_UNHIDE);
    COMMANDO(97, POSITION_RESTING, do_say, 0, TRUE, 0,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(98, POSITION_RESTING, do_practice, 1, TRUE, 0,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(99, POSITION_STANDING, do_use, 1, TRUE, 0,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(100, POSITION_DEAD, do_where, 1, TRUE, 0,
        FULL_TARGET, FULL_TARGET, CMD_MASK_NO_UNHIDE);
    COMMANDO(101, POSITION_DEAD, do_levels, 0, TRUE, 0,
        FULL_TARGET, FULL_TARGET, CMD_MASK_NO_UNHIDE);
    COMMANDO(102, POSITION_DEAD, do_wizutil, LEVEL_GOD, FALSE, 0,
        TAR_TEXT, TAR_CHAR_WORLD | TAR_SELF, 0);
    COMMANDO(103, POSITION_SLEEPING, do_emote, 1, TRUE, 0,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(104, POSITION_DEAD, do_wizset, LEVEL_GOD, FALSE, 0,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(105, POSITION_DEAD, do_gen_ps, 0, TRUE, SCMD_WIZLIST,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(106, POSITION_RESTING, do_consider, 0, TRUE, 0,
        FULL_TARGET, FULL_TARGET, CMD_MASK_NO_UNHIDE);
    COMMANDO(107, POSITION_SLEEPING, do_group, 1, TRUE, 0,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(108, POSITION_DEAD, do_restore, LEVEL_GOD + 1, TRUE, 0,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(109, POSITION_DEAD, do_return, 0, TRUE, 0,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(110, POSITION_DEAD, do_switch, LEVEL_GRGOD, FALSE, 0,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(113, POSITION_DEAD, do_users, LEVEL_AREAGOD, TRUE, 0,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(114, POSITION_DEAD, do_gen_ps, 0, TRUE, SCMD_IMMLIST,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(115, POSITION_DEAD, do_afk, 0, TRUE, 0,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(116, POSITION_SLEEPING, do_commands, LEVEL_IMMORT, TRUE, SCMD_WIZHELP,
        FULL_TARGET, FULL_TARGET, CMD_MASK_NO_UNHIDE);
    COMMANDO(117, POSITION_DEAD, do_gen_ps, 0, TRUE, SCMD_CREDITS,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(118, POSITION_DEAD, do_wiznet, 0, TRUE, 1,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(119, POSITION_DEAD, do_pracreset, LEVEL_GRGOD, FALSE, 0,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(120, POSITION_DEAD, do_poofset, LEVEL_GOD, TRUE, SCMD_POOFIN,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(121, POSITION_DEAD, do_poofset, LEVEL_GOD, TRUE, SCMD_POOFOUT,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(122, POSITION_DEAD, do_teleport, LEVEL_GOD, FALSE, 0,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(123, POSITION_DEAD, do_gecho, LEVEL_GRGOD, TRUE, 0,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(124, POSITION_DEAD, do_wiznet, LEVEL_IMMORT, TRUE, 0,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(125, POSITION_STANDING, do_search, 0, TRUE, 0,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(126, POSITION_DEAD, do_invis, LEVEL_IMMORT + 1, TRUE, 0,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(127, POSITION_DEAD, do_specialize, 12, TRUE, 0,
        TAR_TEXT | TAR_NONE_OK, TAR_IGNORE, CMD_MASK_NO_UNHIDE);
    COMMANDO(128, POSITION_DEAD, do_set, 0, TRUE, 0,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(129, POSITION_DEAD, do_ungroup, 0, TRUE, 0,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(130, POSITION_FIGHTING, do_pray, 0, TRUE, 0,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(131, POSITION_DEAD, do_wizlock, LEVEL_GRGOD, FALSE, 0,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(132, POSITION_STANDING, do_jig, 0, TRUE, 0,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(133, POSITION_STANDING, do_not_here, 0, TRUE, 0,
        FULL_TARGET, FULL_TARGET, 0); /* was block, can now be reused */
    COMMANDO(134, POSITION_DEAD, do_hunt, 0, TRUE, 0,
        TAR_IGNORE, TAR_IGNORE, 0);
    COMMANDO(135, POSITION_FIGHTING, do_disengage, 0, TRUE, 0,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(136, POSITION_DEAD, do_alias, 0, TRUE, 0,
        FULL_TARGET, FULL_TARGET, CMD_MASK_NO_UNHIDE);
    COMMANDO(137, POSITION_DEAD, do_wiznet, LEVEL_IMMORT, TRUE, 0,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(138, POSITION_STANDING, do_not_here, 1, TRUE, 0,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(139, POSITION_STANDING, do_not_here, 1, TRUE, 0,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(140, POSITION_STANDING, do_not_here, 1, TRUE, 0,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(141, POSITION_RESTING, do_not_here, 1, TRUE, 0,
        FULL_TARGET, FULL_TARGET, 0); /* was visible - now can be reused */
    COMMANDO(142, POSITION_DEAD, do_twohand, 0, TRUE, SCMD_TWOHANDED,
        FULL_TARGET, FULL_TARGET, CMD_MASK_NO_UNHIDE);
    COMMANDO(143, POSITION_RESTING, do_concentrate, 1, TRUE, 0,
        TAR_IGNORE, TAR_IGNORE, 0);
    COMMANDO(144, POSITION_DEAD, do_dc, LEVEL_GRGOD, FALSE, 0,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(145, POSITION_STANDING, do_butcher, 1, TRUE, SCMD_BUTCHER,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(146, POSITION_FIGHTING, do_assist, 1, TRUE, 0,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(147, POSITION_SITTING, do_split, 1, TRUE, 0,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(148, POSITION_SLEEPING, do_gsay, 0, TRUE, 0,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(149, POSITION_RESTING, do_tell, 0, TRUE, SCMD_REPLY,
        TAR_TEXT_ALL, FULL_TARGET, 0);
    COMMANDO(150, POSITION_DEAD, do_twohand, 0, TRUE, SCMD_ONEHANDED,
        FULL_TARGET, FULL_TARGET, CMD_MASK_NO_UNHIDE);
    COMMANDO(151, POSITION_RESTING, do_knock, 0, TRUE, 0,
        TAR_DIR_NAME | TAR_NONE_OK, TAR_DIR_WAY | TAR_NONE_OK, 0);
    COMMANDO(152, POSITION_DEAD, do_small_map, 0, TRUE, 0,
        FULL_TARGET, FULL_TARGET, CMD_MASK_NO_UNHIDE);
    COMMANDO(153, POSITION_STANDING, do_not_here, 1, TRUE, 0,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(154, POSITION_STANDING, do_not_here, 1, TRUE, 0,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(155, POSITION_SLEEPING, do_not_here, 1, TRUE, 0,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(156, POSITION_RESTING, do_register, LEVEL_IMMORT, FALSE, 0,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(157, POSITION_DEAD, do_vnum, LEVEL_IMMORT, FALSE, 0,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(158, POSITION_DEAD, do_whois, 0, TRUE, 0,
        FULL_TARGET, FULL_TARGET, CMD_MASK_NO_UNHIDE);
    COMMANDO(159, POSITION_DEAD, do_uptime, LEVEL_IMMORT, TRUE, 0,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(160, POSITION_DEAD, do_commands, 0, TRUE, SCMD_COMMANDS,
        FULL_TARGET, FULL_TARGET, CMD_MASK_NO_UNHIDE);
    COMMANDO(161, POSITION_DEAD, do_commands, 0, TRUE, SCMD_SOCIALS,
        FULL_TARGET, FULL_TARGET, CMD_MASK_NO_UNHIDE);
    COMMANDO(162, POSITION_DEAD, do_ban, LEVEL_AREAGOD, FALSE, 0,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(163, POSITION_DEAD, do_unban, LEVEL_AREAGOD, FALSE, 0,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(164, POSITION_DEAD, do_date, 0, TRUE, 0,
        FULL_TARGET, FULL_TARGET, CMD_MASK_NO_UNHIDE);
    COMMANDO(165, POSITION_DEAD, do_zreset, LEVEL_GOD, FALSE, 0,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(166, POSITION_DEAD, do_color, 0, TRUE, 0,
        FULL_TARGET, FULL_TARGET, CMD_MASK_NO_UNHIDE);
    COMMANDO(167, POSITION_DEAD, do_show, LEVEL_IMMORT, FALSE, 0,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(168, POSITION_DEAD, do_board, 0, TRUE, 0,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(169, POSITION_DEAD, do_gen_ps, LEVEL_IMMORT, TRUE, SCMD_HANDBOOK,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(170, POSITION_DEAD, do_gen_ps, 0, TRUE, SCMD_POLICIES,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(171, POSITION_DEAD, do_board, 0, TRUE, 0,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(172, POSITION_DEAD, do_help, 0, TRUE, 1,
        FULL_TARGET, FULL_TARGET, CMD_MASK_NO_UNHIDE);
    COMMANDO(173, POSITION_DEAD, do_findzone, LEVEL_IMMORT, FALSE, 0,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(174, POSITION_DEAD, do_setfree, LEVEL_IMPL, FALSE, 0,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(175, POSITION_STANDING, do_ride, 0, TRUE, 0,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(176, POSITION_DEAD, do_wizutil, LEVEL_PERMIMM, FALSE, SCMD_SQUELCH,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(177, POSITION_FIGHTING, do_dismount, 0, TRUE, 0,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(178, POSITION_RESTING, do_pull, 0, TRUE, 0,
        TAR_OBJ_ROOM, TAR_IGNORE, 0);
    COMMANDO(179, POSITION_DEAD, do_whistle, 0, TRUE, 0,
        TAR_IGNORE, TAR_IGNORE, 0);
    COMMANDO(180, POSITION_DEAD, do_kick, 0, TRUE, SCMD_SWING,
        TAR_CHAR_ROOM | TAR_FIGHT_VICT, TAR_IGNORE, CMD_MASK_MOVE_PENALTY);
    COMMANDO(181, POSITION_DEAD, do_gen_ps, 0, TRUE, SCMD_CLEAR,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(182, POSITION_DEAD, do_gen_ps, 0, TRUE, SCMD_CLEAR,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(183, POSITION_DEAD, do_gen_ps, 0, TRUE, SCMD_VERSION,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(184, POSITION_SLEEPING, do_gen_com, 0, TRUE, SCMD_CHAT,
        TAR_TEXT_ALL, FULL_TARGET, 0);
    COMMANDO(185, POSITION_SLEEPING, do_gen_com, 0, TRUE, SCMD_NARRATE,
        TAR_TEXT_ALL, FULL_TARGET, 0);
    COMMANDO(186, POSITION_DEAD, do_affections, 0, TRUE, 0,
        FULL_TARGET, FULL_TARGET, CMD_MASK_NO_UNHIDE);
    COMMANDO(188, POSITION_DEAD, do_page, LEVEL_GOD, TRUE, SCMD_PAGE,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(189, POSITION_RESTING, do_report, 0, TRUE, 0,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(190, POSITION_RESTING, do_diagnose, 0, TRUE, 0,
        FULL_TARGET, FULL_TARGET, CMD_MASK_NO_UNHIDE);
    COMMANDO(191, POSITION_DEAD, do_reload, LEVEL_GRGOD, TRUE, 0,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(192, POSITION_DEAD, do_syslog, LEVEL_AREAGOD, TRUE, 0,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(193, POSITION_DEAD, do_last, LEVEL_AREAGOD, TRUE, 0,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(195, POSITION_STANDING, do_track, 0, TRUE, 0,
        TAR_TEXT | TAR_NONE_OK, TAR_IGNORE, 0);
    COMMANDO(196, POSITION_DEAD, do_gen_ps, 0, TRUE, SCMD_WHOAMI,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(197, POSITION_DEAD, do_vstat, LEVEL_GOD + 1, FALSE, 0,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(198, POSITION_RESTING, do_page, 0, TRUE, SCMD_BEEP,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(199, POSITION_RESTING, do_lose, 0, TRUE, 0,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(200, POSITION_SHAPING, do_shape, LEVEL_IMMORT, FALSE, 0,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(201, POSITION_DEAD, do_zone, LEVEL_GOD, FALSE, 0,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(202, POSITION_FIGHTING, do_slay, LEVEL_GOD, FALSE, 0,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(203, POSITION_STANDING, do_gather_food, 0, TRUE, 0,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(204, POSITION_RESTING, do_mental, 0, TRUE, 0,
        TAR_CHAR_ROOM | TAR_FIGHT_VICT, FULL_TARGET, CMD_MASK_MOVE_PENALTY);
    COMMANDO(205, POSITION_STANDING, do_prepare, 0, TRUE, 0,
        TAR_TEXT, FULL_TARGET, 0);
    COMMANDO(206, POSITION_RESTING, do_light, 0, TRUE, 0,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(207, POSITION_RESTING, do_blowout, 0, TRUE, 0,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(208, POSITION_DEAD, do_fame, 0, TRUE, 0,
        FULL_TARGET, FULL_TARGET, CMD_MASK_NO_UNHIDE);
    COMMANDO(209, POSITION_DEAD, do_orc_delay, 0, TRUE, 0,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(210, POSITION_STANDING, do_tame, 1, TRUE, 0,
        TAR_CHAR_ROOM, FULL_TARGET, 0);
    COMMANDO(211, POSITION_STANDING, do_calm, 1, TRUE, 0,
        TAR_CHAR_ROOM, FULL_TARGET, 0);
    COMMANDO(212, POSITION_STANDING, do_fish, 0, TRUE, 0,
        TAR_IGNORE, TAR_IGNORE, 0);
    COMMANDO(213, POSITION_STANDING, do_recruit, 0, TRUE, 0,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(214, POSITION_STANDING, do_refollow, 0, TRUE, 0,
        TAR_IGNORE, TAR_IGNORE, 0);
    COMMANDO(215, POSITION_STANDING, do_stalk, 0, TRUE, 0,
        TAR_DIR_WAY, TAR_IGNORE, 0);
    COMMANDO(216, POSITION_STANDING, do_cover, 0, TRUE, 0,
        TAR_IGNORE, TAR_IGNORE, 0);
    COMMANDO(217, POSITION_FIGHTING, do_lead, 0, TRUE, 0,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(218, POSITION_DEAD, do_exploits, 0, TRUE, 0,
        TAR_IGNORE, TAR_IGNORE, 0);
    COMMANDO(219, POSITION_DEAD, do_exploits, 0, TRUE, 0,
        TAR_IGNORE, TAR_IGNORE, 0);
    COMMANDO(220, POSITION_STANDING, do_trap, 1, TRUE, 0,
        TAR_TEXT | TAR_NONE_OK, TAR_CHAR_ROOM | TAR_NONE_OK,
        CMD_MASK_NO_UNHIDE);
    COMMANDO(224, POSITION_DEAD, do_obj2html, LEVEL_GRGOD, FALSE, 0,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(225, POSITION_DEAD, do_delete, LEVEL_GRGOD, FALSE, 0,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(226, POSITION_DEAD, do_map, 0, TRUE, 0,
        FULL_TARGET, FULL_TARGET, CMD_MASK_NO_UNHIDE);
    COMMANDO(227, POSITION_DEAD, do_top, 0, TRUE, SCMD_TOP,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(228, POSITION_RESTING, do_grouproll, 0, TRUE, 0,
        TAR_IGNORE, TAR_IGNORE, 0);
    COMMANDO(229, POSITION_FIGHTING, do_shoot, 0, TRUE, 0,
        TAR_CHAR_ROOM | TAR_FIGHT_VICT, TAR_IGNORE, CMD_MASK_NO_UNHIDE);
    COMMANDO(230, POSITION_STANDING, do_recover, 0, TRUE, 0,
        FULL_TARGET, TAR_IGNORE, 0);
    COMMANDO(231, POSITION_STANDING, do_recover, 0, TRUE, 0,
        FULL_TARGET, TAR_IGNORE, 0);
    COMMANDO(232, POSITION_STANDING, do_scan, 0, TRUE, 0,
        FULL_TARGET, TAR_IGNORE, 0);
    COMMANDO(233, POSITION_DEAD, do_details, 0, TRUE, 0,
        TAR_TEXT | TAR_NONE_OK, TAR_IGNORE, CMD_MASK_NO_UNHIDE);
    COMMANDO(234, POSITION_FIGHTING, do_bite, 1, TRUE, 0,
        TAR_FIGHT_VICT | TAR_CHAR_ROOM, TAR_IGNORE, CMD_MASK_MOVE_PENALTY);
    COMMANDO(235, POSITION_FIGHTING, do_rend, 1, TRUE, SCMD_REND,
        TAR_FIGHT_VICT | TAR_CHAR_ROOM, TAR_IGNORE, CMD_MASK_MOVE_PENALTY);
    COMMANDO(236, POSITION_FIGHTING, do_maul, 1, TRUE, 0,
        TAR_FIGHT_VICT | TAR_CHAR_ROOM, TAR_IGNORE, CMD_MASK_MOVE_PENALTY);
    COMMANDO(237, POSITION_FIGHTING, do_mark, 0, TRUE, 0,
        TAR_CHAR_ROOM | TAR_FIGHT_VICT, TAR_IGNORE, CMD_MASK_NO_UNHIDE);
    COMMANDO(238, POSITION_FIGHTING, do_blinding, 0, TRUE, 0,
        TAR_CHAR_ROOM | TAR_FIGHT_VICT, TAR_IGNORE, CMD_MASK_NO_UNHIDE);
    COMMANDO(239, POSITION_FIGHTING, do_bendtime, 0, TRUE, 0,
        TAR_IGNORE, TAR_IGNORE, 0);
    COMMANDO(240, POSITION_FIGHTING, do_windblast, 0, TRUE, 0,
        TAR_IGNORE, TAR_IGNORE, 0);
    COMMANDO(241, POSITION_FIGHTING, do_smash, 0, TRUE, 0,
        TAR_FIGHT_VICT | TAR_CHAR_ROOM, TAR_IGNORE, CMD_MASK_MOVE_PENALTY);
    COMMANDO(242, POSITION_FIGHTING, do_frenzy, 0, TRUE, 0,
        TAR_IGNORE, TAR_IGNORE, 0);
    COMMANDO(243, POSITION_FIGHTING, do_stomp, 0, TRUE, 0,
        TAR_NONE_OK, TAR_IGNORE, CMD_MASK_MOVE_PENALTY);
    COMMANDO(244, POSITION_FIGHTING, do_cleave, 0, TRUE, 0,
        TAR_NONE_OK, TAR_IGNORE, CMD_MASK_MOVE_PENALTY);
    COMMANDO(245, POSITION_STANDING, do_overrun, 0, TRUE, 0,
        TAR_DIR_WAY, TAR_IGNORE, CMD_MASK_MOVE_PENALTY);
    COMMANDO(CMD_DEFEND, POSITION_FIGHTING, do_defend, 0, TRUE, 0,
        TAR_NONE_OK, TAR_IGNORE, 0);
    COMMANDO(247, POSITION_STANDING, do_renounce, 0, TRUE, 0,
        FULL_TARGET, TAR_IGNORE, 0);
    COMMANDO(248, POSITION_DEAD, do_mob_csv_extract, LEVEL_IMPL, FALSE, 0,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(249, POSITION_DEAD, do_savebench, LEVEL_IMPL, FALSE, 0, FULL_TARGET, FULL_TARGET,
        0);
}

/* *************************************************************************
 *  Stuff for controlling the non-playing sockets (get name, pwd etc)       *
 ************************************************************************* */

int find_name(char* name)
/* locate entry in p_table with entry->name == name. -1 mrks failed search */
{
    int i;

    for (i = 0; i <= top_of_p_table; i++)
        if (!str_cmp((player_table + i)->name, name))
            return i;

    return -1;
}

int _parse_name(char* arg, char* name)
{
    int i;

    /* skip whitespaces */
    for (; isspace(*arg); arg++)
        ;

    for (i = 0; (*name = *arg); arg++, i++, name++)
        if (!isalpha(*arg) || i > 15)
            return 1;

    if (!i)
        return 1;
    else
        name[i] = 0;

    return 0;
}

void nanny(struct descriptor_data* d, char* arg)
/* deal with newcomers and other non-playing sockets */
{
    char buf[1000];
    int player_i, load_result;
    char tmp_name[20];
    struct char_file_u tmp_store;
    struct char_data* tmp_ch;
    int i, tmp;
    long s;

    extern long secs_to_unretire(struct char_data*);
    int load_char(char* name, struct char_file_u* char_element);
    void load_character(struct char_data * ch); // new function in objsave

    switch (STATE(d)) {
    case CON_NME: /* wait for input of name */
        if (!d->character) {
            CREATE(d->character, struct char_data, 1);
            clear_char(d->character, MOB_VOID);
            register_pc_char(d->character);
            d->character->desc = d;
            SET_BIT(PRF_FLAGS(d->character), PRF_LATIN1);
        }

        for (; isspace(*arg); arg++)
            continue;

        /* blank input = we disconnect you */
        if (!*arg)
            close_socket(d);

        if ((_parse_name(arg, tmp_name))) {
            SEND_TO_Q("Invalid name, please try another.\n\r", d);
            SEND_TO_Q("Name: ", d);
            return;
        }

        if ((player_i = load_char(tmp_name, &tmp_store)) > -1) {
            d->pos = player_i;
            store_to_char(&tmp_store, d->character);

            if (isbanned(d->host) > 0 && !PLR_FLAGGED(d->character, PLR_SITEOK)) {
                SEND_TO_Q("Sorry, your site has been banned.\r\n", d);
                close_socket(d);
                log("(Siteban)");
                return;
            }

            if (PLR_FLAGGED(d->character, PLR_DELETED)) {
                free_char(d->character);
                CREATE(d->character, struct char_data, 1);
                clear_char(d->character, MOB_VOID);
                register_pc_char(d->character);
                d->character->desc = d;
                CREATE(d->character->player.name, char, strlen(tmp_name) + 1);
                CAP(tmp_name);
                strcpy(d->character->player.name, tmp_name);
                sprintf(buf, "\n\rNot found in the player database.\n\r"
                             "A new character will be created if you enter 'Y'.\n\r"
                             "Is %s a suitable name for roleplay in Middle-earth (Y/N)? ",
                    tmp_name);
                SEND_TO_Q(buf, d);
                STATE(d) = CON_NMECNF;
            } else {
                strncpy(d->pwd, tmp_store.pwd, MAX_PWD_LENGTH);
                d->pwd[MAX_PWD_LENGTH] = 0;

                /* undo it just in case they are set */
                REMOVE_BIT(PLR_FLAGS(d->character), PLR_WRITING | PLR_MAILING);

                SEND_TO_Q("Password: ", d);
                echo_off(d->descriptor);

                STATE(d) = CON_PWDNRM;
            }
        } else {
            /* player unknown gotta make a new */
            d->pos = -1;

            CREATE(d->character->player.name, char, strlen(tmp_name) + 1);
            CAP(tmp_name);
            strcpy(d->character->player.name, tmp_name);
            sprintf(buf, "\n\rNot found in the player database.\n\r"
                         "A new character will be created if you enter 'Y'.\n\r"
                         "Is %s a suitable name for roleplay in Middle-earth (Y/N)? ",
                tmp_name);
            SEND_TO_Q(buf, d);
            STATE(d) = CON_NMECNF;
        }
        break;
    case CON_NMECNF: /* wait for conf. of new name */
        for (; isspace(*arg); arg++)
            continue;

        if (is_abbrev(arg, "yes")) {
            /* they're banned */
            if (isbanned(d->host) >= BAN_NEW) {
                vmudlog(NRM, "Request for new char %s denied from [%s] (siteban)",
                    GET_NAME(d->character), d->host);
                SEND_TO_Q("Sorry, new characters not allowed from your site!\n\r", d);
                STATE(d) = CON_CLOSE;
                return;
            }

            /* we're wizlocked */
            if (restrict) {
                SEND_TO_Q("Sorry, new players can't be created at the moment.\r\n",
                    d);
                SEND_TO_Q(wizlock_msg, d);
                SEND_TO_Q("\n\r", d);
                vmudlog(NRM, "Request for new char %s denied from %s (wizlock)",
                    GET_NAME(d->character), d->host);
                STATE(d) = CON_CLOSE;
                return;
            }

            /* is joker trying to pull a dipshit name? */
            if (fill_word(strcpy(buf, GET_NAME(d->character))) || !valid_name(GET_NAME(d->character))) {
                SEND_TO_Q("Illegal name, please try another: ", d);
                RELEASE(GET_NAME(d->character));
                d->character->player.name = 0;
                STATE(d) = CON_NME;
                break;
            }

            SEND_TO_Q("New character.\n\r", d);
            sprintf(buf, "Please enter a password for %s: ",
                GET_NAME(d->character));
            SEND_TO_Q(buf, d);
            echo_off(d->descriptor);
            STATE(d) = CON_PWDGET;

            vmudlog(BRF, "%s [%s] has connected (new character).",
                GET_NAME(d->character), d->host);
        } else if (is_abbrev(arg, "no")) {
            SEND_TO_Q("\n\rOk, what name would you like to use, then? ", d);
            RELEASE(GET_NAME(d->character));
            d->character->player.name = 0;
            STATE(d) = CON_NME;
        } else
            SEND_TO_Q("Please type yes or no: ", d);
        break;
    case CON_PWDNRM: /* get pwd for known player	*/
        /* turn echo back on */
        echo_on(d->descriptor);

        for (; isspace(*arg); arg++)
            continue;

        if (!*arg)
            close_socket(d);
        else {
            if (strncmp(CRYPT(arg, d->pwd), d->pwd, MAX_PWD_LENGTH)) {
                vmudlog(BRF, "Bad PW: %s [%s]",
                    GET_NAME(d->character), d->host);
                d->character->specials2.bad_pws++;
                save_char(d->character, d->character->specials2.load_room, 0);

                if (++(d->bad_pws) >= 5) { /* 5 strikes and you're out. */
                    SEND_TO_Q("Wrong password... disconnecting.\n\r", d);
                    STATE(d) = CON_CLOSE;
                } else {
                    SEND_TO_Q("Wrong password.\n\rPassword: ", d);
                    echo_off(d->descriptor);
                }
                return;
            }

            load_result = d->character->specials2.bad_pws;
            d->character->specials2.bad_pws = 0;
            save_char(d->character, d->character->specials2.load_room, 0);

            if (isbanned(d->host) == BAN_SELECT && !PLR_FLAGGED(d->character, PLR_SITEOK)) {
                SEND_TO_Q("Sorry, this character has not been "
                          "cleared for login from your site!\n\r",
                    d);
                STATE(d) = CON_CLOSE;
                vmudlog(NRM, "Connection attempt for %s denied from %s",
                    GET_NAME(d->character), d->host);
                return;
            }

            if (GET_LEVEL(d->character) < restrict) {
                SEND_TO_Q("The game is temporarily restricted.\r\n"
                          "Try again later.",
                    d);
                SEND_TO_Q(wizlock_msg, d);
                SEND_TO_Q("\n\r", d);
                STATE(d) = CON_CLOSE;
                vmudlog(NRM, "Request for login denied for %s [%s] (wizlock)",
                    GET_NAME(d->character), d->host);
                return;
            }

            /* first, check for switched characters */
            for (tmp_ch = character_list; tmp_ch; tmp_ch = tmp_ch->next)
                if (IS_NPC(tmp_ch) && tmp_ch->desc && tmp_ch->desc->original && GET_IDNUM(tmp_ch->desc->original) == GET_IDNUM(d->character)) {
                    SEND_TO_Q("Disconnecting.", tmp_ch->desc);
                    free_char(d->character);
                    d->character = tmp_ch->desc->original;
                    d->character->desc = d;
                    tmp_ch->desc->character = 0;
                    tmp_ch->desc->original = 0;
                    STATE(tmp_ch->desc) = CON_CLOSE;
                    d->character->specials.timer = 0;
                    SEND_TO_Q("Reconnecting to unswitched char.", d);
                    REMOVE_BIT(PLR_FLAGS(d->character), PLR_MAILING | PLR_WRITING);
                    STATE(d) = CON_PLYNG;
                    vmudlog(NRM, "%s [%s] has reconnected.",
                        GET_NAME(d->character), d->host);
                    return;
                }

            /* now check for linkless and usurpable */
            for (tmp_ch = character_list; tmp_ch; tmp_ch = tmp_ch->next)
                if (!IS_NPC(tmp_ch) && GET_IDNUM(d->character) == GET_IDNUM(tmp_ch)) {
                    if (!tmp_ch->desc || (!tmp_ch->desc->descriptor)) {
                        SEND_TO_Q("Reconnecting.\n\r", d);
                        act("$n has reconnected.", TRUE, tmp_ch, 0, 0, TO_ROOM);
                        vmudlog(NRM, "%s [%s] has reconnected.",
                            GET_NAME(d->character), d->host);

                        if (tmp_ch->desc) {
                            tmp_ch->desc->character = 0;
                            close_socket(tmp_ch->desc, TRUE);
                        }
                    } else {
                        vmudlog(NRM, "%s [%s] has re-logged in; disconnecting old socket.",
                            GET_NAME(tmp_ch), d->host);
                        SEND_TO_Q("This body has been usurped!\n\r", tmp_ch->desc);
                        STATE(tmp_ch->desc) = CON_CLOSE;
                        tmp_ch->desc->character = 0;
                        tmp_ch->desc = 0;
                        SEND_TO_Q("You take over your own body, already in use!\n\r", d);
                        act("$n has reconnected.\n\r"
                            "$n's body has been taken over by a new spirit!",
                            TRUE, tmp_ch, 0, 0, TO_ROOM);
                    }

                    free_char(d->character);
                    tmp_ch->desc = d;
                    d->character = tmp_ch;
                    tmp_ch->specials.timer = 0;
                    REMOVE_BIT(PLR_FLAGS(d->character), PLR_MAILING | PLR_WRITING);
                    STATE(d) = CON_PLYNG;
                    d->pProtocol = ProtocolCreate();
                    ProtocolNegotiate(d);
                    extern void msdp_room_update(char_data* ch);
                    msdp_room_update(d->character);
                    return;
                }

            vmudlog(BRF, "%s [%s] has connected.",
                GET_NAME(d->character), d->host);

            if (GET_LEVEL(d->character) >= LEVEL_IMMORT)
                SEND_TO_Q(imotd, d);
            else
                SEND_TO_Q(motd, d);

            if (load_result) {
                sprintf(buf, "\n\r\n\r"
                             "%s%d LOGIN FAILURE%s SINCE LAST SUCCESSFUL LOGIN.%s\n\r",
                    CC_FIX(d->character, CRED), load_result,
                    (load_result > 1) ? "S" : "", CC_NORM(d->character));
                SEND_TO_Q(buf, d);
            }

            SEND_TO_Q(MENU, d);
            STATE(d) = CON_SLCT;
        }
        break;
    case CON_PWDGET: /* get pwd for new player */
        for (; isspace(*arg); arg++)
            continue;

        if (!*arg || strlen(arg) > MAX_PWD_LENGTH || strlen(arg) < 3 || !str_cmp(arg, GET_NAME(d->character))) {
            SEND_TO_Q("\n\rIllegal password.\n\r", d);
            SEND_TO_Q("Password: ", d);
            return;
        }

        strncpy(d->pwd, CRYPT(arg, d->character->player.name), MAX_PWD_LENGTH);
        *(d->pwd + MAX_PWD_LENGTH) = '\0';

        SEND_TO_Q("Please retype your password: ", d);
        STATE(d) = CON_PWDCNF;
        break;
    case CON_PWDCNF: /* get confirmation of new pwd */
        for (; isspace(*arg); arg++)
            continue;

        if (strncmp(CRYPT(arg, d->pwd), d->pwd, MAX_PWD_LENGTH)) {
            SEND_TO_Q("\n\rPasswords don't match... start over.\n\r", d);
            SEND_TO_Q("Password: ", d);
            STATE(d) = CON_PWDGET;
            return;
        }

        /* turn echo back on */
        echo_on(d->descriptor);

        SEND_TO_Q("What is your sex (M/F)? ", d);
        STATE(d) = CON_QSEX;
        break;
    case CON_QSEX: /* query sex of new user */
        for (; isspace(*arg); arg++)
            continue;

        if (is_abbrev(arg, "male"))
            d->character->player.sex = SEX_MALE;
        else if (is_abbrev(arg, "female"))
            d->character->player.sex = SEX_FEMALE;
        else {
            SEND_TO_Q("\r\nThat's not a sex.\n\r"
                      "What is your sex? ",
                d);
            return;
        }

        SEND_TO_Q("\r\n"
                  "Please choose a race:\r\n"
                  "\r\n"
                  "One of RotS' major features is the ongoing war between the\r\n"
                  "good and evil races of Middle-earth.  Players on opposing\r\n"
                  "sides of the war will not be able to see each other in the\r\n"
                  "'who' listings or communicate over global or private\r\n"
                  "channels: players of opposing races are sworn enemies and\r\n"
                  "will attempt to kill each other on sight.\r\n"
                  "\r\n"
                  "If you are a new player, it is highly recommended that you\r\n"
                  "begin as a member of the free peoples of Middle-earth.\r\n"
                  "\r\n"
                  "  Free peoples           Evil Races      \n\r"
                  "  ------------           ----------------\n\r"
                  "  [H]uman                * [U]ruk-hai Orc\n\r"
                  "  [D]warf                # [C]ommon Orc  \n\r"
                  "  [W]ood Elf             # Uruk-[L]huth  \n\r"
                  "  Ho[B]bit               # [O]log-Hai    \n\r"
                  "* Beor[N]ing             # Harad[R]im    \n\r"
                  "\n\r"
                  "Races marked with a * are hard to play.\n\r"
                  "Races marked with a # are very hard to play.\n\r"
                  "\r\n"
                  "Type '? <race>' for help.\r\n"
                  "\r\n"
                  "Race: ",
            d);
        STATE(d) = CON_QRACE;
        break;
    case CON_QRACE:
        for (; isspace(*arg); arg++)
            continue;

        *arg = LOWER(*arg);

        switch (*arg) {
        case 'h':
            GET_RACE(d->character) = RACE_HUMAN;
            break;
        case 'd':
            GET_RACE(d->character) = RACE_DWARF;
            break;
        case 'w':
            GET_RACE(d->character) = RACE_WOOD;
            break;
        case 'b':
            GET_RACE(d->character) = RACE_HOBBIT;
            break;
        case 'u':
            GET_RACE(d->character) = RACE_URUK;
            break;
        case 'c':
            GET_RACE(d->character) = RACE_ORC;
            break;
        case 'l':
            GET_RACE(d->character) = RACE_MAGUS;
            break;
        case 'n':
            GET_RACE(d->character) = RACE_BEORNING;
            break;
        case 'o':
            GET_RACE(d->character) = RACE_OLOGHAI;
            break;
        case 'r':
            GET_RACE(d->character) = RACE_HARADRIM;
            break;
        case '?':
            do_help(d->character, arg + 1, 0, 0, 0);
            SEND_TO_Q("\n\rRace: ", d);
            STATE(d) = CON_QRACE;
            return;
        default:
            SEND_TO_Q("\r\nThat's not a race.\r\n"
                      "Race: ",
                d);
            return;
        }

        /* end race */
        SEND_TO_Q("\n\r"
                  "Please choose a class:\n\r"
                  "\r\n"
                  "It is possible to create your own, custom class; however,\r\n"
                  "we do not recommend this for new players, and even seasoned\r\n"
                  "players should take caution when designing their own class.\r\n"
                  "\r\n"
                  "If you wish to create a custom class, enter 'o'. Otherwise,\r\n"
                  "we invite you to choose amongst the following standard\r\n"
                  "classes:\r\n"
                  "\r\n"
                  "  Mys[T]ic     [R]anger     [W]arrior     [M]age\r\n"
                  "  Co[N]jurer   W[I]zard     [H]ealer      [S]washbuckler\r\n"
                  "  [B]arbarian  [A]dventurer\r\n"
                  "\r\n"
                  "Enter '? <class>' for help on a class\r\n"
                  "\r\n"
                  "Class: ",
            d);
        STATE(d) = CON_QPROF;
        break;
    case CON_QPROF:
        for (; isspace(*arg); arg++)
            continue;

        *arg = LOWER(*arg);

        if (*arg != 'm' && *arg != 't' && *arg != 'w' && *arg != 'r' && *arg != 'n' && *arg != 'i' && *arg != 'h' && *arg != 's' && *arg != 'b' && *arg != 'a' && *arg != 'o' && *arg != '?') {
            SEND_TO_Q("\n\rThat's not a class\n\rClass: ", d);
            return;
        }

        if (*arg == '?') {
            do_help(d->character, arg + 1, 0, 0, 0);
            SEND_TO_Q("\n\rClass: ", d);
            STATE(d) = CON_QPROF;
            return;
        }

        /* chose custom class */
        if (*arg == 'o') {
            SEND_TO_Q("\n\r"
                      "You are given 150 points to work with, and you can split\r\n"
                      "them however you choose between the four classes.  But be\r\n"
                      "warned: the more points you spend on any class, the less\r\n"
                      "each following point will benefit you.\r\n"
                      "\r\n"
                      "Enter 'l' to list your choices or '=' to finalize your\r\n"
                      "class selection\r\n\r\n"
                      "Your choice: ",
                d);
            STATE(d) = CON_CREATE;
            return;
        }

        for (tmp = 0; tmp < DEFAULT_PROFS; tmp++)
            if (*arg == existing_profs[tmp].letter)
                for (i = 0; i < 5; i++)
                    GET_PROF_POINTS(i, d->character) = existing_profs[tmp].Class_points[i];
        SEND_TO_Q("\r\n"
                  "On RotS, you are allowed to create your own colour scheme.\r\n"
                  "However, many new players find this burdensome, and prefer\r\n"
                  "a quick way to enable colours; thus, we have made available\r\n"
                  "a default set of colours to satisfy those players who rather\r\n"
                  "dive into the game than muck around in the RotS manual pages.\r\n"
                  "\r\n"
                  "Please note that you may disable ANSI colours at any time by\r\n"
                  "typing 'colour off'; furthermore, we encourage you to read\r\n"
                  "our manual entry on how to define your own, personalized set\r\n"
                  "of colours via the 'help colour' or 'manual general colour'\r\n"
                  "commands.\r\n"
                  "\r\n"
                  "Do you wish to enable the default colour set (Y/N)? ",
            d);
        STATE(d) = CON_COLOR;
        break;
    case CON_COLOR:
        if (is_abbrev(arg, "yes")) {
            set_colors_default(d->character);
            SEND_TO_Q("Ok, you have enabled the default colour set.\r\n", d);
        } else if (!is_abbrev(arg, "no")) {
            SEND_TO_Q("\r\nThat's not an option.\r\n"
                      "Please type yes or no: ",
                d);
            break;
        }

        sprintf(buf, "\r\n"
                     "We allow players to use the latin-1 character set to better\r\n"
                     "represent the alphabet common in Tolkien's works.  You may\r\n"
                     "want to enable latin-1 encoding here if your terminal passes\r\n"
                     "the test below--but remember that you can change your latin-1\r\n"
                     "preferences at any time during normal game play via the\r\n"
                     "'set latin-1' command.\r\n"
                     "\r\nDo you see an 'a' with "
                     "a pair of dots above it: %c (Y/N)? ",
            228);
        SEND_TO_Q(buf, d);
        STATE(d) = CON_LATIN;
        break;
    case CON_LATIN:
        if (is_abbrev(arg, "no")) {
            REMOVE_BIT(PRF_FLAGS(d->character), PRF_LATIN1);
            SEND_TO_Q("Ok, you will not use the latin-1 character set.\r\n", d);
        } else if (!is_abbrev(arg, "yes")) {
            SEND_TO_Q("\r\nThat's not an option.\r\n"
                      "Please type yes or no: ",
                d);
            break;
        } else
            SEND_TO_Q("\r\nOk, you will use the latin-1 character set.\r\n", d);

        /* Give them an autowimpy of 10 */
        WIMP_LEVEL(d->character) = 10;
        introduce_char(d);
        SEND_TO_Q(MENU, d);
        STATE(d) = CON_SLCT;
        vmudlog(NRM, "%s [%s] new player.",
            GET_NAME(d->character), d->host);
        break;
    case CON_QOWN:
        break;
    case CON_QOWN2:
        break;
    case CON_RMOTD: /* read CR after printing motd	*/
        SEND_TO_Q(MENU, d);
        STATE(d) = CON_SLCT;
        break;
    case CON_SLCT: /* get selection from main menu */
        for (; isspace(*arg); arg++)
            continue;

        switch (*arg) {
        case '0':
            close_socket(d);
            break;
        case '1':
            // new
            for (tmp_ch = character_list; tmp_ch; tmp_ch = tmp_ch->next) {
                if (!IS_NPC(tmp_ch) && GET_IDNUM(d->character) == GET_IDNUM(tmp_ch)) {
                    if (!tmp_ch->desc || (!tmp_ch->desc->descriptor)) {
                        SEND_TO_Q("Reconnecting.\n\r", d);
                        act("$n has reconnected.", TRUE, tmp_ch, 0, 0, TO_ROOM);
                        vmudlog(NRM, "%s [%s] has reconnected.", GET_NAME(d->character), d->host);

                        if (tmp_ch->desc) {
                            tmp_ch->desc->character = 0;
                            close_socket(tmp_ch->desc, TRUE);
                        }
                    } else {
                        vmudlog(NRM, "%s [%s] has re-logged in; disconnecting old socket.", GET_NAME(tmp_ch), d->host);
                        SEND_TO_Q("This body has been usurped!\n\r", tmp_ch->desc);
                        STATE(tmp_ch->desc) = CON_CLOSE;
                        tmp_ch->desc->character = 0;
                        tmp_ch->desc = 0;
                        SEND_TO_Q("You take over your own body, already in use!\n\r", d);
                        act("$n has reconnected.\n\r$n's body has been taken over by a new spirit!", TRUE, tmp_ch, 0, 0, TO_ROOM);
                    }

                    free_char(d->character);
                    tmp_ch->desc = d;
                    d->character = tmp_ch;
                    tmp_ch->specials.timer = 0;
                    REMOVE_BIT(PLR_FLAGS(d->character), PLR_MAILING | PLR_WRITING);
                    STATE(d) = CON_PLYNG;
                    return;
                }
            }

            // endnew
            reset_char(d->character);
            load_character(d->character); // new function in objsave
            save_char(d->character, d->character->in_room, 0);
            STATE(d) = CON_PLYNG;
            report_news(d->character);
            report_mail(d->character);
            send_to_char(WELC_MESSG, d->character);

            
            send_to_char("\n\r", d->character);

            /* if level 0, start out the new character */
            if (!GET_LEVEL(d->character)) {
                do_start(d->character);
                send_to_char(START_MESSG, d->character);
            }

            /* set character's energy to full on log-in */
            d->character->specials.ENERGY = ENE_TO_HIT;

            /* ensure character has correct practice sessions available on log-in */
            d->character->update_available_practice_sessions();

            /* update msdp room information */
            d->pProtocol = ProtocolCreate();
            ProtocolNegotiate(d);
            extern void msdp_room_update(char_data* ch);
            msdp_room_update(d->character);

            do_look(d->character, "", 0, 0, 0);

            /* report how long they must wait until unretire */
            if (IS_SET(PLR_FLAGS(d->character), PLR_RETIRED)) {
                if ((s = secs_to_unretire(d->character)) > 0) {
                    sprintf(buf, "You may unretire in ");
                    if ((tmp = (int)(s / 86400)))
                        sprintf(buf + strlen(buf), "%d day%s.\r\n", tmp,
                            tmp == 1 ? "" : "s");
                    else if ((tmp = (int)(s / 3600)))
                        sprintf(buf + strlen(buf), "%d hour%s.\r\n", tmp,
                            tmp == 1 ? "" : "s");
                    else if ((tmp = (int)(s / 60)))
                        sprintf(buf + strlen(buf), "%d minute%s.\r\n", tmp,
                            tmp == 1 ? "" : "s");
                    else
                        sprintf(buf + strlen(buf), "%ld second%s.\r\n", s,
                            s == 1 ? "" : "s");
                } else {
                    sprintf(buf, "You may unretire now.\r\nType leave to leave the retirement home.\r\n");
                }

                send_to_char(buf, d->character);
            }
            d->prompt_mode = 1;
            REMOVE_BIT(PRF_FLAGS(d->character), PRF_DISPTEXT);
            d->character->temp = 0;
            update_memory_list(d->character);
            break;
        case '3':
            SEND_TO_Q(background, d);
            SEND_TO_Q("\n\r\n\r*** PRESS RETURN:", d);
            STATE(d) = CON_RMOTD;
            break;
        case '4':
            SEND_TO_Q("Enter your old password: ", d);
            echo_off(d->descriptor);
            STATE(d) = CON_PWDNQO;
            break;
        case '5':
            if (GET_LEVEL(d->character) >= LEVEL_GOD) {
                SEND_TO_Q("\n\rYou are immortal. Forget it.\n\r", d);
                break;
            } else if (GET_LEVEL(d->character) > 20) {
                SEND_TO_Q("\n\rPlease ask an Arata or higher to delete you.\n\r", d);
                break;
            }
            SEND_TO_Q("\n\rEnter your password for verification: ", d);
            echo_off(d->descriptor);
            STATE(d) = CON_DELCNF1;
            break;
        case '6':
            draw_coofs(buf, d->character);
            SEND_TO_Q(buf, d);
            SEND_TO_Q("   PRESS RETURN:\n\r", d);
            STATE(d) = CON_RMOTD;
            break;
        default:
            SEND_TO_Q("\n\rThat's not a menu choice!\n\r", d);
            SEND_TO_Q(MENU, d);
            break;
        }
        break;
    case CON_PWDNQO:
        for (; isspace(*arg); arg++)
            continue;

        if (strncmp(CRYPT(arg, d->pwd), d->pwd, MAX_PWD_LENGTH)) {
            SEND_TO_Q("\n\rIncorrect password.\n\r", d);
            SEND_TO_Q(MENU, d);
            STATE(d) = CON_SLCT;
            echo_on(d->descriptor);
            return;
        } else {
            SEND_TO_Q("\n\rEnter a new password: ", d);
            STATE(d) = CON_PWDNEW;
            return;
        }
        break;
    case CON_PWDNEW:
        for (; isspace(*arg); arg++)
            continue;

        if (!*arg || strlen(arg) > MAX_PWD_LENGTH || strlen(arg) < 3 || !str_cmp(arg, GET_NAME(d->character))) {
            SEND_TO_Q("\n\rIllegal password.\n\r", d);
            SEND_TO_Q("Password: ", d);
            return;
        }

        strncpy(d->pwd, CRYPT(arg, d->character->player.name), MAX_PWD_LENGTH);
        *(d->pwd + MAX_PWD_LENGTH) = '\0';
        SEND_TO_Q("\n\rPlease retype password: ", d);
        STATE(d) = CON_PWDNCNF;
        break;
    case CON_PWDNCNF:
        for (; isspace(*arg); arg++)
            continue;

        if (strncmp(CRYPT(arg, d->pwd), d->pwd, MAX_PWD_LENGTH)) {
            SEND_TO_Q("\n\rPasswords don't match; start over.\n\r", d);
            SEND_TO_Q("Password: ", d);
            STATE(d) = CON_PWDNEW;
            return;
        }

        SEND_TO_Q("\n\rDone.\r\n"
                  "You must enter the game to make the change final.\n\r",
            d);
        SEND_TO_Q(MENU, d);
        echo_on(d->descriptor);
        STATE(d) = CON_SLCT;
        break;
    case CON_DELCNF1:
        echo_on(d->descriptor);
        for (; isspace(*arg); arg++)
            continue;

        if (strncmp(CRYPT(arg, d->pwd), d->pwd, MAX_PWD_LENGTH)) {
            SEND_TO_Q("\n\rIncorrect password.\n\r", d);
            SEND_TO_Q(MENU, d);
            STATE(d) = CON_SLCT;
        } else {
            SEND_TO_Q("\n\rYOU ARE ABOUT TO DELETE THIS CHARACTER PERMANENTLY.\n\r"
                      "ARE YOU ABSOLUTELY SURE?\n\r\n\r"
                      "Please type \"yes\" to confirm: ",
                d);
            STATE(d) = CON_DELCNF2;
        }
        break;
    case CON_DELCNF2:
        if (!strcmp(arg, "yes") || !strcmp(arg, "YES")) {
            if (PLR_FLAGGED(d->character, PLR_FROZEN)) {
                SEND_TO_Q("You try to kill yourself, but the ice stops you.\n\r", d);
                SEND_TO_Q("Character not deleted.\n\r\n\r", d);
                STATE(d) = CON_CLOSE;
                return;
            }

            SET_BIT(PLR_FLAGS(d->character), PLR_DELETED);

            pkill_unref_character(d->character);

            save_char(d->character, NOWHERE, 0);
            Crash_delete_file(GET_NAME(d->character));
            delete_exploits_file(GET_NAME(d->character));
            delete_character_file(d->character);
            sprintf(buf, "Character '%s' deleted!\n\rGoodbye.\n\r",
                GET_NAME(d->character));
            SEND_TO_Q(buf, d);
            vmudlog(NRM, "%s (lev %d) has self-deleted.",
                GET_NAME(d->character), GET_LEVEL(d->character));
            STATE(d) = CON_CLOSE;
            return;
        } else {
            SEND_TO_Q("Character not deleted.\n\r\n\r", d);
            SEND_TO_Q(MENU, d);
            STATE(d) = CON_SLCT;
        }
        break;
    case CON_CREATE:
    case CON_CREATE2:
        STATE(d) = new_player_select(d, arg);
        break;
    case CON_CLOSE:
        close_socket(d);
        break;
    default:
        log("SYSERR: Nanny: illegal state of con'ness");
        abort();
        break;
    }
}

int new_player_select(struct descriptor_data* d, char* arg)
/*
 * returns CON_CREATE to continue creation, CON_SLCT to end
 */
{
    int tmp, tmp2, i, classpoints;

    if (STATE(d) == CON_CREATE) {
        tmp2 = 0;
        if ((*arg == 'l') && (*(arg + 1) == '\0')) {
            SEND_TO_Q("\r\n"
                      "To become more proficient in a class, enter the number of\r\n"
                      "points you wish to allocate to a class at the choice\r\n"
                      "prompt.  At the following prompt, enter a class letter.\r\n"
                      "Use a negative number of points to become less proficient\r\n"
                      "in a class.\r\n"
                      "\r\n"
                      "Type '*' to see your current selection; enter '=' when\n\r"
                      "you have finished building your class.  If you wish, you\r\n"
                      "can allocate the point selections of any standard class\r\n"
                      "by entering a class letter from the standard class\r\n"
                      "selection dialogue.\r\n"
                      "\r\n"
                      "Your choice: ",
                d);
            return CON_CREATE;
        }

        if (*(arg + 1) == '\0')
            for (tmp = 0; tmp < DEFAULT_PROFS; tmp++)
                if (*arg == existing_profs[tmp].letter) {
                    for (i = 0; i < 5; i++)
                        GET_PROF_POINTS(i, d->character) = existing_profs[tmp].Class_points[i];
                    draw_coofs(buf, d->character);
                    SEND_TO_Q("Ok, your abilities are now as follows:\n\r", d);
                    SEND_TO_Q(buf, d);
                    sprintf(buf, "Points remaining: %d\n\r",
                        150 - points_used(d->character));
                    SEND_TO_Q(buf, d);
                    SEND_TO_Q("Your choice: ", d);
                    return CON_CREATE;
                }

        if (isdigit(*arg) || (*arg == '-' && isdigit(*(arg + 1)))) {
            d->character->classpoints = atoi(arg);
            SEND_TO_Q("Which class do you wish to add this number to (m/t/r/w)? ",
                d);
            return CON_CREATE2;
        }

        if (*arg == '=') {
            if (points_used(d->character) <= 150) {
                /* Must sync with message in CON_COLOR above */
                SEND_TO_Q("\r\n"
                          "On RotS, you are allowed to create your own colour scheme.\r\n"
                          "However, many new players find this burdensome, and prefer\r\n"
                          "a quick way to enable colours; thus, we have made available\r\n"
                          "a default set of colours to satisfy those players who rather\r\n"
                          "dive into the game than muck around in the RotS manual pages.\r\n"
                          "\r\n"
                          "Please note that you may disable ANSI colours at any time by\r\n"
                          "typing 'colour off'; furthermore, we encourage you to read\r\n"
                          "our manual entry on how to define your own, personalized set\r\n"
                          "of colours via the 'help colour' or 'manual general colour'\r\n"
                          "commands.\r\n"
                          "\r\n"
                          "Do you wish to enable the default colour set (Y/N)? ",
                    d);

                return CON_COLOR;
            } else {
                SEND_TO_Q("You've allocated more than 150 creation points.\r\n"
                          "Please revise your decisions.\r\n"
                          "Your choice: ",
                    d);
                return CON_CREATE;
            }
        }

        if (*arg == '*') {
            SEND_TO_Q("Your current abilities are:\n\r", d);
            draw_coofs(buf, d->character);
            SEND_TO_Q(buf, d);
            sprintf(buf, "Points remaining: %d\n\r", 150 - points_used(d->character));
            SEND_TO_Q(buf, d);
            SEND_TO_Q("\n\rYour Choice: ", d);
            return CON_CREATE;
        }
        SEND_TO_Q("Unrecognized command\n\rYour choice: ", d);
    } else if (STATE(d) == CON_CREATE2) {
        *arg = LOWER(*arg);
        classpoints = d->character->classpoints;

        switch (*arg) {
        case 'm':
            GET_PROF_POINTS(PROF_MAGE, d->character) = MAX(0, MIN(classpoints + GET_PROF_POINTS(PROF_MAGE, d->character), 165));
            break;

        case 't':
            GET_PROF_POINTS(PROF_CLERIC, d->character) = MAX(0, MIN(classpoints + GET_PROF_POINTS(PROF_CLERIC, d->character), 165));
            break;

        case 'r':
            GET_PROF_POINTS(PROF_RANGER, d->character) = MAX(0, MIN(classpoints + GET_PROF_POINTS(PROF_RANGER, d->character), 165));
            break;

        case 'w':
            GET_PROF_POINTS(PROF_WARRIOR, d->character) = MAX(0, MIN(classpoints + GET_PROF_POINTS(PROF_WARRIOR, d->character), 165));
            break;

        default:
            sprintf(buf, "Invalid ability: %c\n\r", *arg);
            SEND_TO_Q(buf, d);
            break;
        }
        draw_coofs(buf, d->character);
        SEND_TO_Q(buf, d);
        sprintf(buf, "Points remaining: %d\n\r", 150 - points_used(d->character));
        SEND_TO_Q(buf, d);
        SEND_TO_Q("Ok.\n\rYour choice: ", d);
    } else {
        log("Illegal call to new_player_select.");
        return CON_SLCT;
    }
    return CON_CREATE;
}

void introduce_char(struct descriptor_data* d)
{
    FILE* fp;
    init_char(d->character);

    if (d->pos < 0)
        d->pos = create_entry(GET_NAME(d->character));
    sprintf(buf, "Char created: %s, idnum = %ld", GET_NAME(d->character),
        GET_IDNUM(d->character));
    log(buf);

    SET_SHOOTING(d->character, SHOOTING_NORMAL);
    utils::set_specialization(*d->character, game_types::PS_None);
    utils::set_casting(*d->character, CASTING_NORMAL);

    if ((fp = Crash_get_file_by_name(GET_NAME(d->character), "wb")))
        fclose(fp);
    save_char(d->character, NOWHERE, 0);

    SEND_TO_Q(motd, d);
}
