/* ************************************************************************
 *   File: db.h                                          Part of CircleMUD *
 *  Usage: header file for database handling                               *
 *                                                                         *
 *  All rights reserved.  See license.doc for complete information.        *
 *                                                                         *
 *  Copyright (C) 1993 by the Trustees of the Johns Hopkins University     *
 *  CircleMUD is based on DikuMUD, Copyright (C) 1990, 1991.               *
 ************************************************************************ */

#ifndef DB_H
#define DB_H

#include <stdio.h> /* For the FILE structure */

#include "interpre.h" /* For the SPECIAL macro */
#include "platdef.h"  /* For sh_int, ush_int, byte, etc. */
#include "structs.h"  /* For MAX_NAME_LENGTH and MAX_STRING_LENGTH */

/* arbitrary constants used by index_boot() (must be unique) */
#define DB_BOOT_WLD 0
#define DB_BOOT_MOB 1
#define DB_BOOT_OBJ 2
#define DB_BOOT_ZON 3
#define DB_BOOT_SHP 4
#define DB_BOOT_MDL 6
#define DB_BOOT_SCR 7

/* names of various files and directories */
#define INDEX_FILE "index"      /* index of world files		*/
#define MINDEX_FILE "index.min" /* ... and for mini-mud-mode	*/
#define NEWINDEX_FILE "index.new"
#define WLD_PREFIX "world/wld" /* room definitions		*/
#define MOB_PREFIX "world/mob" /* monster prototypes		*/
#define OBJ_PREFIX "world/obj" /* object prototypes		*/
#define ZON_PREFIX "world/zon" /* zon defs & command tables	*/
#define SHP_PREFIX "world/shp" /* shop definitions		*/
#define MDL_PREFIX "world/mdl" /* mudlle(asima) programs     	*/
#define SCR_PREFIX "world/scr" /* scripts */

#define CREDITS_FILE "text/credits"    /* for the 'credits' command	*/
#define NEWS_FILE "text/news"          /* for the 'news' command	*/
#define MOTD_FILE "text/motd"          /* messages of the day / mortal	*/
#define IMOTD_FILE "text/imotd"        /* messages of the day / immort	*/
#define IDEA_FILE "text/ideas"         /* for the 'idea'-command	*/
#define TYPO_FILE "text/typos"         /*         'typo'		*/
#define BUG_FILE "text/bugs"           /*         'bug'		*/
#define HELP_KWRD_FILE "text/help_tbl" /* for HELP <keywrd>	       */
#define HELP_PAGE_FILE "text/help"     /* for HELP <CR>		*/
#define INFO_FILE "text/info"          /* for INFO			*/
#define WIZLIST_FILE "text/wizlist"    /* for WIZLIST			*/
#define IMMLIST_FILE "text/immlist"    /* for IMMLIST			*/
#define BACKGROUND_FILE "text/backgr"  /* for the background story	*/
#define POLICIES_FILE "text/policies"  /* player policies/rules	*/
#define HANDBOOK_FILE "text/handbook"  /* handbook for new immorts	*/
#define SPELL_FILE "text/spel_tbl"     /* for MAN SPELL		*/
#define POWER_FILE "text/pray_tbl"     /* for MAN POWER		*/
#define ASIMA_FILE "text/mudl_tbl"     /* for MAN ASIMA		*/
#define SKILLS_FILE "text/skil_tbl"    /* for MAN SKILL		*/
#define SHAPE_FILE "text/shap_tbl"     /* for MAN SHAPE		*/
#define MSDP_FILE "text/msdp_tbl"      /* for MAN MSDP */

#define LASTDEATH_FILE "../log/lastdeath" /* the last words of the dead mud */

#define PLAYER_FILE "misc/players"       /* the player database		*/
#define MESS_FILE "misc/messages"        /* damage message		*/
#define SOCMESS_FILE "misc/socials"      /* messgs for social acts	*/
#define MAIL_FILE "misc/plrmail"         /* for the mudmail system	*/
#define BAN_FILE "misc/badsites"         /* for the siteban system	*/
#define XNAME_FILE "misc/xnames"         /* invalid name substrings	*/
#define MUDLLE_FILE "misc/mudlle"        /* mudlle programs              */
#define MUDLLE_OLDFILE "misc/mudlle.old" /* backup from shaping        */
#define PKILL_FILE "misc/pklist"         /*the list of player killings   */
#define CRIME_FILE "misc/crimelist"      /*the list of player crimes	*/

// exploit types
#define EXPLOIT_PK 1
#define EXPLOIT_DEATH 2
#define EXPLOIT_LEVEL 3
#define EXPLOIT_BIRTH 4
#define EXPLOIT_STAT 5
#define EXPLOIT_MOBDEATH 6
#define EXPLOIT_RETIRED 8
#define EXPLOIT_ACHIEVEMENT 9
#define EXPLOIT_NOTE 10
#define EXPLOIT_POISON 11
#define EXPLOIT_REGEN_DEATH 12

#define SAVE_VERSION 1

// Players who load in these rooms [x][...] will be moved to [...][y]
#define MAX_MAZE_RENT_MAPPINGS 6

static const int mortal_maze_room[MAX_MAZE_RENT_MAPPINGS][2] = {
    {23500, 10700}, {23600, 10700}, {23700, 10700},
    {23100, 6330},  {23200, 6330},  {23300, 6330}};

/* public procedures in db.c */
void boot_db(void);
void char_to_store(struct char_data *, struct char_file_u *);
void store_to_char(struct char_file_u *, struct char_data *);
int load_char(char *, struct char_file_u *);
void save_char(struct char_data *, int, int);
/* Player-file finalize helpers. See
 * docs/superpowers/specs/2026-06-27-savebench-finalize-ab-test-design.md.
 * Both move a fully-written scratch file into its versioned destination; they
 * MUST produce byte-identical results. legacy uses system("rm")/system("cp");
 * rename uses a portable readdir/unlink glob then rename(). */
// Serialize one PC to scratch_path (the fopen + char_to_store + fprintf block).
void write_player_text(struct char_data *ch, int load_room, const char *scratch_path);
bool finalize_player_file_legacy(const char *scratch_path, const char *base_path,
                                 const char *versioned_path);
bool finalize_player_file_rename(const char *scratch_path, const char *dir_path,
                                 const char *base_name, const char *versioned_path);
int create_entry(char *);
void init_char(struct char_data *);
void clear_char(struct char_data *, int);
void clear_object(struct obj_data *);
void reset_char(struct char_data *);
void free_char(struct char_data *);
void free_obj(struct obj_data *);
int real_room(int);
int real_program(int);
char *fread_string(FILE *, char *);
int real_object(int);
int real_mobile(int);
int vnum_mobile(char *, struct char_data *);
int vnum_object(char *, struct char_data *);
void record_crime(struct char_data *, struct char_data *, int, int);
void add_crime(int, int, int, int, int);
void forget_crimes(struct char_data *, int);
void add_exploit_record(int, struct char_data *, int, char *);
int delete_exploits_file(char *);
void delete_character_file(struct char_data *);
void move_char_deleted(int);
int get_char_directory(char *, char *);
int load_player(char *, struct char_file_u *);

// Implemented in consts.cc

/* Returns an array of size MAX_WEAR that contains item encumbrance multipliers.
 */
sh_int *get_encumb_table();

/* Returns an array of size MAX_WEAR that contains item leg encumbrance
 * multipliers. */
sh_int *get_leg_encumb_table();

#define REAL 0
#define VIRT 1

struct obj_data *read_object(int, int);
struct char_data *read_mobile(int, int);

struct crime_record_type {
  int crime_time;
  sh_int criminal;
  sh_int victim;
  int crime;
  sh_int witness;
  sh_int witness_type;
};

/* structure for the reset commands */
struct reset_com {
  char command; /* current command */
  int if_flag;  /* if TRUE: exe only if preceding exe'd */
  int arg1;
  int arg2; /* Arguments to the command */
  int arg3;
  int arg4;
  int arg5;
  int arg6;
  int arg7;
  int existing;
  /*
   *  Commands:
   *  'M': Read a mobile
   *  'O': Read an object
   *  'G': Give obj to mob
   *  'P': Put obj in obj
   *  'G': Obj to char
   *  'E': Obj to char equip
   *  'D': Set state of door
   */
};

/* element in monster and object index-tables */
struct index_data {
  int virt;       /* virt number of this mob/obj */
  int number;     /* number of existing units of this mob/obj	*/
  SPECIAL(*func); /* special procedure for this mob/obj */
};

struct player_index_element {
  char *name;
  sh_int level;
  sh_int race;
  int idnum;
  time_t log_time;
  long flags;
  int warpoints;
  int rank;
  int totalrank;
  char ch_file[80]; /* for speed in locating the file to load */
};

struct help_index_element {
  char *keyword;
  long pos;
};

struct help_index_summary {
  char *keyword;
  char *descr;
  char *filename;
  FILE *file;
  int top_of_helpt;
  char imm_only;
  struct help_index_element *index;
};

/* exploits */
struct exploit_record {
  int type;              /* type of record */
  char chtime[30];       /* str date of death */
  sh_int shintVictimID;  /* idnum of victim */
  char chVictimName[30]; /* in case char has been deleted */
  int iVictimLevel;      /* at time of kill */
  int iKillerLevel;      /* at time of kill */
  int iIntParam;         /* reserved */
};

struct social_messg {
  int hide;
  int min_victim_position; /* Position of victim */
  int min_actor_position;  /* Position of victim */
  char *command;
  char *char_no_arg; /* No argument was supplied */
  char *others_no_arg;
  char *char_found; /* if NULL, read no further, ignore args */
  char *others_found;
  char *vict_found;
  char *not_found; /* An argument was there, but no victim was found */
  char *char_auto; /* The victim turned out to be the character */
  char *others_auto;
};

/* don't change these */
#define BAN_NOT 0
#define BAN_NEW 1
#define BAN_SELECT 2
#define BAN_ALL 3

#define BANNED_SITE_LENGTH 50
struct ban_list_element {
  char name[MAX_NAME_LENGTH + 1];
  char site[BANNED_SITE_LENGTH + 1];
  int type;
  long date;
  struct ban_list_element *next;
};

extern char buf[MAX_STRING_LENGTH];
extern char buf1[MAX_STRING_LENGTH];
extern char buf2[MAX_STRING_LENGTH];
extern char arg[MAX_STRING_LENGTH];

#endif /* DB_H */
