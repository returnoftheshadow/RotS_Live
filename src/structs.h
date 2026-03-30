/* ************************************************************************
 *   File: structs.h                                     Part of CircleMUD *
 *  Usage: header file for central structures and contstants               *
 *                                                                         *
 *  All rights reserved.  See license.doc for complete information.        *
 *                                                                         *
 *  Copyright (C) 1993 by the Trustees of the Johns Hopkins University     *
 *  CircleMUD is based on DikuMUD, Copyright (C) 1990, 1991.               *
 ************************************************************************ */

#ifndef STRUCTS_H
#define STRUCTS_H

#include <sys/types.h>

#include <stdio.h>

#include "color.h" /* For MAX_COLOR_FIELDS */
#include "platdef.h" /* For sh_int, ush_int, byte, etc. */

#include "protocol.h"
#include <algorithm>
#include <assert.h>
#include <map>
#include <set>
#include <string>
#include <vector>

#define MAX_ALIAS (30 + GET_LEVEL(ch) * 2)
const int constexpr MAX_SPIRITS = 90000;

const int constexpr ENE_TO_HIT = 1200;
const int constexpr BAREHANDED_DAMAGE = 2;
const int constexpr PRACS_PER_LEVEL = 3;
const int constexpr LEA_PRAC_FACTOR = 5;
const int constexpr LIGHT_WEAPON_WEIGHT_CUTOFF = 235;

const int constexpr PLAYER_RACE_MAX = 100;

const int constexpr INVALID_GUARDIAN = -1;
const int constexpr AGGRESSIVE_GUARDIAN = 0;
const int constexpr DEFENSIVE_GUARDIAN = 1;
const int constexpr MYSTIC_GUARDIAN = 2;

const int constexpr LEVEL_IMPL = 100;
const int constexpr LEVEL_GRGOD = 97;
const int constexpr LEVEL_AREAGOD = 95;
const int constexpr LEVEL_PERMIMM = 94;
const int constexpr LEVEL_GOD = 93;
const int constexpr LEVEL_IMMORT = 91;
const int constexpr LEVEL_MINIMM = LEVEL_IMMORT; /* The lowest immortal level */
const int constexpr LEVEL_MAX = 30;

const int constexpr LEVEL_FREEZE = LEVEL_PERMIMM;

const int constexpr NUM_OF_DIRS = 6;
const int constexpr PULSE_ZONE = 12;
const int constexpr PULSE_MOBILE = 24;
const int constexpr PULSE_VIOLENCE = 12;
const int constexpr PULSE_FAST_UPDATE = 12;
const int constexpr PULSE_MENTAL_FIGHT = 8;

const int constexpr MAX_CHARACTERS = 64000;
const int constexpr MAX_PCCHARACTERS = 32000;
const int constexpr SMALL_BUFSIZE = 512;
const int constexpr LARGE_BUFSIZE = 16384;
const int constexpr MAX_STRING_LENGTH = 8192;
const int constexpr MAX_INPUT_LENGTH = 255;
const int constexpr MAX_MESSAGES = 255;
const int constexpr MAX_ITEMS = 153;
const int constexpr MAX_RACES = 32;
const int constexpr MAX_BODYTYPES = 16;
const int constexpr MAX_BODYPARTS = 11;
const int constexpr MAX_RACE_NAME_LENGTH = 14;
const int constexpr MIN_NAME_LENGTH = 3;
const int constexpr MAX_NAME_LENGTH = 12;
const int constexpr MAX_PWD_LENGTH = 10; /* Used in char_file_u *DO*NOT*CHANGE* */
const int constexpr MAX_ACCOUNT_PASSWORD_LENGTH = 255;
const int constexpr HOST_LEN = 30; /* Used in char_file_u *DO*NOT*CHANGE* */
const int constexpr MAX_MAXBOARD = 22; /* the max number of boards ever -  */
/*   used in objsave                */

#define MAX_ZONES 500

#define BODYTYPE_ANIMAL 2

#define MESS_ATTACKER 1
#define MESS_VICTIM 2
#define MESS_ROOM 3

const int constexpr SECS_PER_REAL_MIN = 60;
const int constexpr SECS_PER_REAL_HOUR = (60 * SECS_PER_REAL_MIN);
const int constexpr SECS_PER_REAL_DAY = (24 * SECS_PER_REAL_HOUR);
const int constexpr SECS_PER_REAL_YEAR = (365 * SECS_PER_REAL_DAY);

const int constexpr SECS_PER_MUD_HOUR = 60;
const int constexpr TICS_PER_SECOND = 4;
const int constexpr FAST_UPDATE_RATE = SECS_PER_MUD_HOUR * TICS_PER_SECOND / PULSE_FAST_UPDATE;

const int constexpr SECS_PER_MUD_DAY = (24 * SECS_PER_MUD_HOUR);
const int constexpr SECS_PER_MUD_MONTH = (30 * SECS_PER_MUD_DAY);
const int constexpr SECS_PER_MUD_YEAR = (12 * SECS_PER_MUD_MONTH);

#define COPP_IN_GOLD 1000
#define COPP_IN_SILV 100

#define SMALL_WORLD_RADIUS 10

#define WORLD_SIZE_X 50
#define WORLD_SIZE_Y 26
#define WORLD_AREA ((WORLD_SIZE_X + 4) * (WORLD_SIZE_Y + 2))
/* The following defs are for obj_data  */

/* For 'type_flag' */

#define ITEM_LIGHT 1
#define ITEM_SCROLL 2
#define ITEM_WAND 3
#define ITEM_STAFF 4
#define ITEM_WEAPON 5
#define ITEM_FIREWEAPON 6
#define ITEM_MISSILE 7
#define ITEM_TREASURE 8
#define ITEM_ARMOR 9
#define ITEM_POTION 10
#define ITEM_WORN 11
#define ITEM_OTHER 12
#define ITEM_TRASH 13
#define ITEM_TRAP 14
#define ITEM_CONTAINER 15
#define ITEM_NOTE 16
#define ITEM_DRINKCON 17
#define ITEM_KEY 18
#define ITEM_FOOD 19
#define ITEM_MONEY 20
#define ITEM_PEN 21
#define ITEM_BOAT 22
#define ITEM_FOUNTAIN 23
#define ITEM_SHIELD 24
#define ITEM_LEVER 25

/* Bitvector for item materials (used in shop.cc) */

// #define MATERIAL_USUAL_STUFF	(1 << 0)
#define MATERIAL_CLOTH (1 << 0)
#define MATERIAL_LEATHER (1 << 1)
#define MATERIAL_CHAIN (1 << 2)
#define MATERIAL_METAL (1 << 3)
#define MATERIAL_WOOD (1 << 4)
#define MATERIAL_STONE (1 << 5)
#define MATERIAL_CRYSTAL (1 << 6)
#define MATERIAL_GOLD (1 << 7)
#define MATERIAL_SILVER (1 << 8)
#define MATERIAL_MITHRIL (1 << 9)
#define MATERIAL_FUR (1 << 10)
#define MATERIAL_GLASS (1 << 11)

/* Bitvector For 'wear_flags' */

#define ITEM_TAKE 1
#define ITEM_WEAR_FINGER 2
#define ITEM_WEAR_NECK 4
#define ITEM_WEAR_BODY 8
#define ITEM_WEAR_HEAD 16
#define ITEM_WEAR_LEGS 32
#define ITEM_WEAR_FEET 64
#define ITEM_WEAR_HANDS 128
#define ITEM_WEAR_ARMS 256
#define ITEM_WEAR_SHIELD 512
#define ITEM_WEAR_ABOUT 1024
#define ITEM_WEAR_WAISTE 2048
#define ITEM_WEAR_WRIST 4096
#define ITEM_WIELD 8192
#define ITEM_HOLD 16384
#define ITEM_THROW 32768
#define ITEM_WEAR_BACK 65536
#define ITEM_WEAR_BELT 131072
/* UNUSED, CHECKS ONLY FOR ITEM_LIGHT #define ITEM_LIGHT_SOURCE  65536 */

/* Bitvector for 'extra_flags' */

#define ITEM_GLOW (1 << 0)
#define ITEM_HUM (1 << 1)
#define ITEM_DARK (1 << 2)
#define ITEM_BREAKABLE (1 << 3) /* was ITEM_LOCK - now used to indicate a breakable item */
#define ITEM_EVIL (1 << 4)
#define ITEM_INVISIBLE (1 << 5)
#define ITEM_MAGIC (1 << 6)
#define ITEM_NODROP (1 << 7)
#define ITEM_BROKEN (1 << 8) /* was ITEM_BLESS - now used to break keys etc */
#define ITEM_ANTI_GOOD (1 << 9) /* not usable by good people    */
#define ITEM_ANTI_EVIL (1 << 10) /* not usable by evil people    */
#define ITEM_ANTI_NEUTRAL (1 << 11) /* not usable by neutral people */
#define ITEM_NORENT (1 << 12) /* not allowed to rent the item */
#define ITEM_NOINVIS (1 << 14) /* not allowed to cast invis on */
#define ITEM_WILLPOWER (1 << 15) /* indicates a weapon which can damage a wraith */
#define ITEM_IMM (1 << 16)
#define ITEM_HUMAN (1 << 17)
#define ITEM_DWARF (1 << 18)
#define ITEM_WOODELF (1 << 19)
#define ITEM_HOBBIT (1 << 20)
#define ITEM_BEORNING (1 << 21)
#define ITEM_URUK (1 << 22)
#define ITEM_ORC (1 << 23)
#define ITEM_MOBORC (1 << 24)
#define ITEM_MAGUS (1 << 25)
#define ITEM_OLOGHAI (1 << 26)
#define ITEM_HARADRIM (1 << 27)
#define ITEM_STAY_ZONE (1 << 28)

/* Some different kind of liquids */
#define LIQ_WATER 0
#define LIQ_BEER 1
#define LIQ_WINE 2
#define LIQ_ALE 3
#define LIQ_DARKALE 4
#define LIQ_WHISKY 5
#define LIQ_LEMONADE 6
#define LIQ_FIREBRT 7
#define LIQ_LOCALSPC 8
#define LIQ_SLIME 9
#define LIQ_MILK 10
#define LIQ_TEA 11
#define LIQ_COFFE 12
#define LIQ_BLOOD 13
#define LIQ_SALTWATER 14
#define LIQ_CLEARWATER 15

/* for containers  - value[1] */

#define CONT_CLOSEABLE 1
#define CONT_PICKPROOF 2
#define CONT_CLOSED 4
#define CONT_LOCKED 8

typedef std::vector<struct char_data*> char_vector;
typedef char_vector::iterator char_iter;
typedef char_vector::const_iterator const_char_iter;

typedef std::set<struct char_data*> char_set;
typedef char_set::iterator char_set_iter;
typedef char_set::const_iterator const_char_set_iter;

struct combat_result_struct {
    combat_result_struct()
        : wants_to_flee(false)
        , will_die(false) {};
    combat_result_struct(bool wimpy, bool dead)
        : wants_to_flee(wimpy)
        , will_die(dead) {};

    bool wants_to_flee;
    bool will_die;
};

namespace game_types {
enum weapon_type {
    WT_UNUSED_1,
    WT_UNUSED_2,
    WT_WHIPPING,
    WT_SLASHING,
    WT_SLASHING_TWO,
    WT_FLAILING,
    WT_BLUDGEONING,
    WT_BLUDGEONING_TWO,
    WT_CLEAVING,
    WT_CLEAVING_TWO,
    WT_STABBING,
    WT_PIERCING,
    WT_SMITING,
    WT_BOW,
    WT_CROSSBOW,
    WT_COUNT,
};

const char* get_weapon_name(weapon_type type);
} // namespace game_types

#ifdef CONSTANTSMARK
int global_release_flag = 1;
#else
extern int global_release_flag;
#endif

struct char_data;
struct obj_data;
struct room_data;

/* possible targets for commands */
#define TAR_IGNORE (1 << 0)
#define TAR_CHAR_ROOM (1 << 1)
#define TAR_CHAR_WORLD (1 << 2)
#define TAR_SELF (1 << 3)
#define TAR_FIGHT_VICT (1 << 4)
#define TAR_SELF_ONLY (1 << 5)
#define TAR_SELF_NONO (1 << 6) /*Only a check, use with ei. TAR_CHAR_ROOM */
#define TAR_OBJ_INV (1 << 7)
#define TAR_OBJ_ROOM (1 << 8)
#define TAR_OBJ_WORLD (1 << 9)
#define TAR_OBJ_EQUIP (1 << 10)
#define TAR_NONE_OK (1 << 11)
#define TAR_DIR_NAME (1 << 12)
#define TAR_DIR_WAY (1 << 13)
#define TAR_DIRECTION (TAR_DIR_NAME | TAR_DIR_WAY)
#define TAR_TEXT (1 << 14)
#define TAR_TEXT_ALL (1 << 15)
#define TAR_ALL (1 << 16)
#define TAR_GOLD (1 << 17)
#define TAR_IN (1 << 18)
#define TAR_VALUE (1 << 19)
#define TAR_DARK_OK (1 << 20)
#define TAR_IN_OTHER (1 << 21)

/* dark_okay means, darkness is not counted against visibility */
/* tar_in_other is for "get meat sack", "steal sack wolf"      */
/* both are additional flags, to work with other target options*/

/* values for target_data... what is in the union, really */

#define TARGET_NONE 0
#define TARGET_CHAR 1
#define TARGET_OBJ 2
#define TARGET_ROOM 3
#define TARGET_TEXT 4
#define TARGET_GOLD 5
#define TARGET_DIR 6
#define TARGET_IN 7
#define TARGET_ALL 8
#define TARGET_VALUE 9
#define TARGET_OTHER 10
#define TARGET_IGNORE 11

#define GET_TARGET_TEXT(t1) ((t1)->ptr.text->text)

struct target_data {
    signed char type;
    union {
        struct char_data* ch;
        struct obj_data* obj;
        struct room_data* room;
        struct txt_block* text;
        void* other;
    } ptr;
    sh_int ch_num; /* abs_number, if the target is a character, or just some
                   digit data*/
    int choice; /* what kind of target is this   */
    void cleanup(); /* cleans the target data, releases the text if nec. */
    void operator=(target_data t2);
    int operator==(target_data t2);

    target_data()
    {
        type = TARGET_NONE;
        ptr.other = 0;
        ch_num = 0;
        choice = 0;
    }
};

struct extra_descr_data {
    char* keyword; /* Keyword in look/examine          */
    char* description; /* What to see                      */
    struct extra_descr_data* next; /* Next in list                     */
};

#define MAX_OBJ_AFFECT 2 /* Used in OBJ_FILE_ELEM *DO*NOT*CHANGE* */
#define OBJ_NOTIMER -700 /*changed*/

struct waiting_type {
    int wait_value; /* number of ticks left to wait */
    int cmd; /* command to be performed after delay -
                                 0  = none,
                                 -1 = call special for NPC, none for PC */
    int subcmd; /* subcmd it is, probably as a chain flag
                                 for specials for NPCs */
    int priority; /* priority it is. 0 is the lowest */

    int flg; /* also for whatever needed, flags mostly */
    struct target_data targ1;
    struct target_data targ2;
    struct char_data* next;
};

#define WEAPON_POISON_DUR 60

struct obj_flag_data {
public:
    int get_ob_coef() const { return value[0]; }
    int get_parry_coef() const { return value[1]; }
    int get_bulk() const { return value[2]; }
    game_types::weapon_type get_weapon_type() const { return game_types::weapon_type(value[3]); }
    int get_level() const { return level; }
    int get_weight() const { return std::max(weight, 1); }

    int get_base_damage_reduction() const { return value[1]; }

    bool is_cloth() const { return material == 1; }
    bool is_leather() const { return material == 2; }
    bool is_chain() const { return material == 3; }
    bool is_metal() const { return material == 4; }

    // Returns true if the object can be equipped.
    bool is_wearable() const;

    // Poison information
    bool is_weapon_poisoned() const { return poisoned; }
    int get_poison_duration() const { return poisondata[0]; }
    int get_poison_strength() const { return poisondata[1]; }
    int get_poison_multipler() const { return poisondata[2]; }

public:
    int value[5]; /* Values of the item (see list) */ /*changed*/
    byte type_flag; /* Type of item                     */
    int wear_flags; /* Where you can wear it            */
    int extra_flags; /* If it hums,glows etc             */
    int weight; /* Weigt what else                  */
    int cost; /* Value when sold (gp.)            */
    sh_int cost_per_day; /* Cost to keep pr. real day        */
    int timer; /* Timer for object                 */
    long bitvector; /* To set chars bits                */
    ubyte level; /* level of an item (not to correspond to character's*/
    ubyte rarity; /* rarity of an item */
    signed char material; /* what is it made of               */
    sh_int butcher_item; /* virtual item to butcher, 0 for none, -1 for butchered */
    int prog_number; /* for special objects... */
    int script_number; /* identifies the script which is triggered under certain conditions */
    struct info_script* script_info; /* Pointer to char_script (protos.h) 0 if no script */
    bool poisoned;
    int poisondata[5];
};

/* Wraps the object_flag_data and exposes values that are used for weapons. */
struct weapon_flag_data {
    weapon_flag_data(obj_flag_data* data)
        : object_flag_data(data)
    {
        assert(data);
        assert(data->type_flag == ITEM_WEAPON);
    }

    int get_ob_coef() const { return object_flag_data->value[0]; }
    int get_parry_coef() const { return object_flag_data->value[1]; }
    int get_bulk() const { return object_flag_data->value[2]; }
    game_types::weapon_type get_weapon_type() const
    {
        return game_types::weapon_type(object_flag_data->value[3]);
    }

private:
    obj_flag_data* object_flag_data;
};

/* Used in OBJ_FILE_ELEM *DO*NOT*CHANGE* */
struct obj_affected_type {
    byte location; /* Which ability to change (APPLY_XXX) */
    int modifier; /* How much it changes by              */
};

/* ======================== Structure for object ========================= */
struct obj_data {
public:
    int get_ob_coef() const { return obj_flags.get_ob_coef(); }
    int get_parry_coef() const { return obj_flags.get_parry_coef(); }
    int get_bulk() const { return obj_flags.get_bulk(); }
    game_types::weapon_type get_weapon_type() const { return obj_flags.get_weapon_type(); }
    int get_level() const { return obj_flags.get_level(); }
    int get_weight() const { return obj_flags.get_weight(); }

    int get_base_damage_reduction() const { return obj_flags.get_base_damage_reduction(); }

    const char_data* get_owner() const { return carried_by; }

    // Returns true if the object can be equipped.
    bool is_wearable() const { return obj_flags.is_wearable(); }

    // Returns true if the object is a quiver.
    bool is_quiver() const;

    // Returns true if the weapon is a ranged weapon.
    bool is_ranged_weapon() const;

public:
    int item_number; /* Where in data-base               */
    int in_room; /* In what room -1 when conta/carr  */

    struct obj_flag_data obj_flags; /* Object information               */
    struct obj_affected_type affected[MAX_OBJ_AFFECT]; /* Which abilities in PC to change  */

    char* name; /* Title of object :get etc.        */
    char* description; /* When in room                     */
    char* short_description; /* when worn/carry/in cont.         */
    char* action_description; /* What to write when used          */
    struct extra_descr_data* ex_description; /* extra descriptions     */
    struct char_data* carried_by; /* Carried by :NULL in room/conta   */

    int owner;
    struct obj_data* in_obj; /* In what object NULL when none    */
    struct obj_data* contains; /* Contains objects                 */

    struct obj_data* next_content; /* For 'contains' lists             */
    struct obj_data* next; /* For the object list              */
    int touched; /* Has a PC touched this object?    */
    int loaded_by; /* idnum of immortal who loaded the object (else 0) */
};

/* ======================================================================= */
/* The following defs are for room_data  */

#define NOWHERE -1 /* nil reference for room-database    */

/* Bitvector For 'room_flags' */

#define DARK (1 << 0)
#define DEATH (1 << 1)
#define NO_MOB (1 << 2)
#define INDOORS (1 << 3)
#define NORIDE (1 << 4)
#define PERMAFFECT (1 << 5)
#define SHADOWY (1 << 6)
#define NO_MAGIC (1 << 7)
#define TUNNEL (1 << 8)
#define PRIVATE (1 << 9)
#define GODROOM (1 << 10)
#define BFS_MARK (1 << 11)
#define DRINK_WATER (1 << 12)
#define DRINK_POISON (1 << 13)
#define SECURITYROOM (1 << 14)
#define PEACEROOM (1 << 15)
#define NO_TELEPORT (1 << 16)
#define HIDE_VNUM (1 << 17)

#define BFS_ERROR -1
#define BFS_ALREADY_THERE -2
#define BFS_NO_PATH -3

/* For 'dir_option' */

#define NORTH 0
#define EAST 1
#define SOUTH 2
#define WEST 3
#define UP 4
#define DOWN 5

#define EX_ISDOOR 1
#define EX_CLOSED 2
#define EX_LOCKED 4
#define EX_NOFLEE 8
#define EX_RSLOCKED 16
#define EX_PICKPROOF 32
#define EX_DOORISHEAVY 64 /*changed*/
#define EX_NOBREAK 128 /*changed*/
#define EX_NO_LOOK 256 /*changed*/
#define EX_ISHIDDEN 512 /* for hidden exit*/
#define EX_ISBROKEN 1024
#define EX_NORIDE 2048
#define EX_NOBLINK 4096
#define EX_LEVER 8192
#define EX_NOWALK 16384

/* For 'Sector types' */

#define SECT_INSIDE 0
#define SECT_CITY 1
#define SECT_FIELD 2
#define SECT_FOREST 3
#define SECT_HILLS 4
#define SECT_MOUNTAIN 5
#define SECT_WATER_SWIM 6
#define SECT_WATER_NOSWIM 7
#define SECT_UNDERWATER 8
#define SECT_ROAD 9
#define SECT_CRACK 10
#define SECT_DENSE_FOREST 11
#define SECT_SWAMP 12

struct room_direction_data {
    char* general_description; /* When look DIR.                  */

    char* keyword; /* for open/close                  */

    ubyte exit_width; /* 1-6, default should be 4 */
    int exit_info; /* Exit info    changed from sh_int                   */
    int key; /* Key's number (-1 for no key)    */
    int to_room; /* Where direction leeds (NOWHERE) */
};

/* ========================= Structure for room ========================== */
#define EXTENSION_START 90000
#define EXTENSION_SIZE 50
#define EXTENSION_ROOM_HEAD 100000

const int NUM_OF_BLOOD_TRAILS = 3;
#define NUM_OF_TRACKS 10
struct room_data;

struct room_track_data {
    sh_int char_number; // race number for players, virt_number for mobs
    byte data; // data/8 = time of the track, data & 8 = direction
    sh_int condition; // Effective condition of the tracks in hours
    // Weather makes tracks age faster/slower etc

    room_track_data()
    {
        char_number = 0;
        data = 0;
        condition = 0;
    }
};

struct room_bleed_data {
    sh_int char_number;
    byte data;
    sh_int condition;

    room_bleed_data()
    {
        char_number = 0;
        data = 0;
        condition = 0;
    }
};

struct room_data_extension {
    room_data* extension_world;
    //  int extension_length;  use EXTENSION_SIZE instead
    room_data_extension* extension_next;

    room_data_extension();
    ~room_data_extension();
};

struct room_data {
    static room_data* BASE_WORLD;
    static int BASE_LENGTH;
    static int TOTAL_LENGTH;
    static room_data_extension* BASE_EXTENSION;

    int number; /* Rooms number                       */
    int zone; /* Room zone (for resetting)          */
    byte level;
    int sector_type; /* sector type (move/hide)*/ /*changed*/
    char* name; /* Rooms name 'You are ...'           */
    char* description; /* Shown when entered                 */
    struct extra_descr_data* ex_description; /* for examine/look       */
    struct room_direction_data* dir_option[NUM_OF_DIRS]; /* Directions */
    struct room_track_data room_track[NUM_OF_TRACKS]; /* track info.. */
    long room_flags; /* DEATH,DARK ... etc                 */
    int alignment; /*changed*/
    byte light; /* Number of lightsources in room     */

    byte bfs_dir;
    room_data* bfs_next; /*instead ot bfs_queue structure, for hunting*/

    int (*funct)(struct char_data*, struct char_data*, int, char*, int, waiting_type*);
    /* special procedure, check SPECIAL in interpre.h      */
    struct obj_data* contents; /* List of items in room              */
    struct char_data* people; /* List of NPC / PC in room           */

    struct affected_type* affected; /* room affects */

    struct room_bleed_data bleed_track[NUM_OF_BLOOD_TRAILS];

    room_data();

    room_data& operator[](int i);

    int create_room(int zone); /* active constructor, returns real number  -
                                                           use this one to add rooms */
    void create_bulk(int amount); /* initial world constructor, the first alloc*/
    void delete_room(); /* active destructor - use it when removing rooms */
    void create_exit(int dir, int room, char connect = 1);
};

/* ======================================================================== */

/* The following defs and structures are related to char_data   */

/* For 'equipment' */

#define WEAR_LIGHT 0
#define WEAR_FINGER_R 1
#define WEAR_FINGER_L 2
#define WEAR_NECK_1 3
#define WEAR_NECK_2 4
#define WEAR_BODY 5
#define WEAR_HEAD 6
#define WEAR_LEGS 7
#define WEAR_FEET 8
#define WEAR_HANDS 9
#define WEAR_ARMS 10
#define WEAR_SHIELD 11
#define WEAR_ABOUT 12
#define WEAR_WAISTE 13
#define WEAR_WRIST_R 14
#define WEAR_WRIST_L 15
#define WIELD 16
#define HOLD 17
#define WEAR_BACK 18
#define WEAR_BELT_1 19
#define WEAR_BELT_2 20
#define WEAR_BELT_3 21
#define WIELD_TWOHANDED 22

/* For 'char_payer_data' */

#define MAX_TOUNGE 3 /* Used in CHAR_FILE_U *DO*NOT*CHANGE* */
#define MAX_SKILLS 256 /* Used in CHAR_FILE_U *DO*NOT*CHANGE* */
#define MAX_WEAR 22
#define MAX_AFFECT 32 /* Used in CHAR_FILE_U *DO*NOT*CHANGE* */

/* Predifined  conditions */
#define DRUNK 0
#define FULL 1
#define THIRST 2

/* Bitvector for 'affected_by' */
#define AFF_DETECT_HIDDEN (1 << 0)
#define AFF_INFRARED (1 << 1)
#define AFF_SNEAK (1 << 2)
#define AFF_HIDE (1 << 3)
#define AFF_DETECT_MAGIC (1 << 4)
#define AFF_CHARM (1 << 5)
#define AFF_CURSE (1 << 6)
#define AFF_SANCTUARY (1 << 7)
#define AFF_TWOHANDED (1 << 8)
#define AFF_INVISIBLE (1 << 9)
#define AFF_MOONVISION (1 << 10)
#define AFF_POISON (1 << 11)
#define AFF_SHIELD (1 << 12)
#define AFF_BREATHE \
    (1 << 13) // was paralysis - now indicates the ch can breathe whatever the room conditions
#define AFF_UNUSED (1 << 14) // unused affect
#define AFF_CONFUSE (1 << 15)
#define AFF_SLEEP (1 << 16)
#define AFF_BASH (1 << 17)
#define AFF_FLYING (1 << 18)
#define AFF_DETECT_INVISIBLE (1 << 19)
#define AFF_FEAR (1 << 20)
#define AFF_BLIND (1 << 21)
#define AFF_FOLLOW (1 << 22)
#define AFF_SWIM (1 << 23)
#define AFF_HUNT (1 << 24)
#define AFF_EVASION (1 << 25)
#define AFF_WAITING (1 << 26)
#define AFF_WAITWHEEL (1 << 27)
#define AFF_CONCENTRATION (1 << 29)
#define AFF_HAZE (1 << 30)
#define AFF_HALLUCINATE (1 << 31)

/* modifiers to char's abilities */

#define APPLY_NONE 0
#define APPLY_STR 1
#define APPLY_DEX 2
#define APPLY_INT 3
#define APPLY_WILL 4
#define APPLY_CON 5
#define APPLY_LEA 6
#define APPLY_PROF 7
#define APPLY_LEVEL 8
#define APPLY_AGE 9
#define APPLY_CHAR_WEIGHT 10
#define APPLY_CHAR_HEIGHT 11
#define APPLY_MANA 12
#define APPLY_HIT 13
#define APPLY_MOVE 14
#define APPLY_GOLD 15
#define APPLY_EXP 16
#define APPLY_DODGE 17
#define APPLY_OB 18
#define APPLY_DAMROLL 19
#define APPLY_SAVING_SPELL 20
#define APPLY_WILLPOWER 21
#define APPLY_REGEN 22
#define APPLY_VISION 23
#define APPLY_SPEED 24
#define APPLY_PERCEPTION 25
#define APPLY_ARMOR 26
#define APPLY_SPELL 27
#define APPLY_BITVECTOR 28
#define APPLY_MANA_REGEN 29
#define APPLY_RESIST 30
#define APPLY_VULN 31
#define APPLY_MAUL 32
#define APPLY_BEND 33

#define APPLY_PK_MAGE 34
#define APPLY_PK_MYSTIC 35
#define APPLY_PK_RANGER 36
#define APPLY_PK_WARRIOR 37

#define APPLY_SPELL_PEN 38
#define APPLY_SPELL_POW 39

#define ROOMAFF_SPELL 1
#define ROOMAFF_EXIT 2

/* 'prof' for PC's */
#define MAX_PROFS 4
#define PROF_GENERAL 0
#define PROF_MAGIC_USER 1
#define PROF_MAGE 1
#define PROF_CLERIC 2
#define PROF_THIEF 3
#define PROF_RANGER 3
#define PROF_WARRIOR 4

#define DEFAULT_PROFS 10

#define LANG_BASIC 0
#define LANG_ANIMAL 121
#define LANG_HUMAN 122
#define LANG_ORC 123

struct prof_type {
    char letter;
    sh_int Class_points[5];
};

namespace game_types {
enum player_specs {
    PS_None,
    PS_Fire,
    PS_Cold,
    PS_Regeneration,
    PS_Protection,
    PS_Animals,
    PS_Stealth,
    PS_WildFighting,
    PS_Teleportation,
    PS_Illusion,
    PS_Lightning,
    PS_Guardian,
    PS_HeavyFighting,
    PS_LightFighting,
    PS_Defender,
    PS_Archery,
    PS_Darkness,
    PS_Arcane,
    PS_WeaponMaster,
    PS_BattleMage,
    PS_Count,
};
}

#define PLRSPEC_NONE 0
#define PLRSPEC_FIRE 1
#define PLRSPEC_COLD 2
#define PLRSPEC_REGN 3
#define PLRSPEC_PROT 4
#define PLRSPEC_PETS 5
#define PLRSPEC_STLH 6
#define PLRSPEC_WILD 7
#define PLRSPEC_TELE 8
#define PLRSPEC_ILLU 9
#define PLRSPEC_LGHT 10
#define PLRSPEC_GRDN 11
#define PLRSPEC_HFGT 12
#define PLRSPEC_LFGT 13
#define PLRSPEC_DFND 14
#define PLRSPEC_ARCH 15
#define PLRSPEC_DARK 16
#define PLRSPEC_ARCANE 17
#define PLRSPEC_WMSR 18
#define PLRSPEC_BTLEMS 19

/* Races for PCS */
const int constexpr RACE_GOD = 0;
const int constexpr RACE_HUMAN = 1;
const int constexpr RACE_DWARF = 2;
const int constexpr RACE_WOOD = 3;
const int constexpr RACE_HOBBIT = 4;
const int constexpr RACE_HIGH = 5;
const int constexpr RACE_BEORNING = 6;
const int constexpr RACE_URUK = 11;
const int constexpr RACE_ORC = 13;
const int constexpr RACE_MAGUS = 15;
const int constexpr RACE_OLOGHAI = 17;
const int constexpr RACE_HARADRIM = 18;

/* Races used for NPCs */
#define RACE_EASTERLING 14
#define RACE_HARAD 12
#define RACE_UNDEAD 16
#define RACE_TROLL 20

#define localtime(x) localtime((time_t*)x)
#ifndef CONSTANTSMARK
extern char* pc_races[];
extern char* pc_race_types[];
extern char* pc_race_keywords[];
extern char* pc_star_types[];
extern char* pc_named_star_types[];
#endif

/* sex */
#define SEX_NEUTRAL 0
#define SEX_MALE 1
#define SEX_FEMALE 2

/* positions */
#define POSITION_DEAD 0
#define POSITION_SHAPING 1
#define POSITION_INCAP 2
#define POSITION_STUNNED 3
#define POSITION_SLEEPING 4
#define POSITION_RESTING 5
#define POSITION_SITTING 6
#define POSITION_FIGHTING 7
#define POSITION_STANDING 8

/* for mobile actions: specials.act */
#define MOB_VOID 0 /* for special uses only */
#define MOB_SPEC (1 << 0) /* spec-proc to be called if exist   */
#define MOB_SENTINEL (1 << 1) /* this mobile not to be moved       */
#define MOB_SCAVENGER (1 << 2) /* pick up stuff lying around        */
#define MOB_ISNPC (1 << 3) /* This bit is set for use with IS_NPC()*/
#define MOB_NOBASH (1 << 4) /* Set if a thief should NOT be killed  */
#define MOB_AGGRESSIVE (1 << 5) /* Set if automatic attack on NPC's     */
#define MOB_STAY_ZONE (1 << 6) /* MOB Must stay inside its own zone    */
#define MOB_WIMPY (1 << 7) /* MOB Will flee when injured, and if   */
/* aggressive only attack sleeping players*/
#define MOB_STAY_TYPE (1 << 8) /* MOB will move in rooms of the same type*/
#define MOB_MOUNT (1 << 9) /* MOB can be ridden	*/
#define MOB_CAN_SWIM \
    (1 << 10) /* MOB doesn't need a boat to swim. If GATEKEEPER, he wont' respond to knock/say */
#define MOB_MEMORY (1 << 11) /* remember attackers if struck first */
#define MOB_HELPER (1 << 12) /* attack chars attacking a PC in room */
#define MOB_AGGRESSIVE_EVIL (1 << 13)
#define MOB_AGGRESSIVE_NEUTRAL (1 << 14)
#define MOB_AGGRESSIVE_GOOD (1 << 15)
#define MOB_BODYGUARD (1 << 16) /* rescues his master, if any */
#define MOB_SHADOW (1 << 17) /* a ghost, a spirit, all that stuff */
#define MOB_SWITCHING (1 << 18) /* is stupid - won't switch opponents */
#define MOB_NORECALC (1 << 19)
#define MOB_FAST (1 << 20) /* will mob_act when somebody enters... */
#define MOB_PET (1 << 21) /* pet of a player */
#define MOB_HUNTER (1 << 22) /* memory + hunts his enemies         */
#define MOB_ORC_FRIEND (1 << 23) /* can be recruited by common orcs */
#define MOB_RACE_GUARD (1 << 24) /* will not allow players of different race into the room */
#define MOB_ASSISTANT (1 << 25) /* will assist his master if his master is in combat */
#define MOB_GUARDIAN (1 << 26) /* flag for guardian mobs */

/* For players : specials.act */
#define PLR_IS_NCHANGED (1 << 1)
#define PLR_FROZEN (1 << 2)
#define PLR_DONTSET (1 << 3) /* Dont EVER set (ISNPC bit) */
#define PLR_WRITING (1 << 4)
#define PLR_MAILING (1 << 5)
#define PLR_CRASH (1 << 6)
#define PLR_SITEOK (1 << 7)
#define PLR_NOSHOUT (1 << 8)
#define PLR_NOTITLE (1 << 9)
#define PLR_DELETED (1 << 10)
#define PLR_LOADROOM (1 << 11)
#define PLR_NOWIZLIST (1 << 12)
#define PLR_NODELETE (1 << 13)
#define PLR_INVSTART (1 << 14)
#define PLR_RETIRED (1 << 15)
#define PLR_SHAPING (1 << 16)
#define PLR_WR_FINISH (1 << 17)
#define PLR_ISSHADOW (1 << 18)
#define PLR_ISAFK (1 << 19)
#define PLR_INCOGNITO (1 << 20)
#define PLR_WAS_KITTED (1 << 21)

/* for players: preference bits */
#define PRF_BRIEF (1 << 0)
#define PRF_COMPACT (1 << 1)
#define PRF_NARRATE (1 << 2)
#define PRF_NOTELL (1 << 3)
#define PRF_MENTAL (1 << 4)
#define PRF_SWIM (1 << 5)
#define PRF_NOTHING2 (1 << 6)
#define PRF_PROMPT (1 << 7)
#define PRF_DISPTEXT (1 << 8)
#define PRF_NOHASSLE (1 << 9)
#define PRF_SUMMONABLE (1 << 11)
#define PRF_ECHO (1 << 12)
#define PRF_HOLYLIGHT (1 << 13)
#define PRF_COLOR (1 << 14)
#define PRF_SING (1 << 15)
#define PRF_WIZ (1 << 16)
#define PRF_LOG1 (1 << 17)
#define PRF_LOG2 (1 << 18)
#define PRF_LOG3 (1 << 19)
#define PRF_CHAT (1 << 20)
#define PRF_ROOMFLAGS (1 << 22)
#define PRF_SPAM (1 << 23)
#define PRF_MSDP (1 << 24) // TODO: Replace this is PRF_MSDP
#define PRF_WRAP (1 << 25)
#define PRF_LATIN1 (1 << 26)
#define PRF_SPINNER (1 << 27)
#define PRF_INV_SORT1 (1 << 28)
#define PRF_INV_SORT2 (1 << 29)
#define PRF_ADVANCED_VIEW (1 << 30)
#define PRF_ADVANCED_PROMPT (1 << 31)

struct memory_rec {
    struct char_data* enemy;
    int enemy_number;
    long id;
    struct memory_rec* next;
    struct memory_rec* next_on_mob;
};

/* This structure is purely intended to be an easy way to transfer */
/* and return information about time (real or mudwise).            */
struct time_info_data {
    byte hours, day, month, moon;
    sh_int year;
};

/* These data contain information about a players time data */
struct time_data {
    time_t birth; /* This represents the characters age                */
    time_t logon; /* Time of the last logon (used to calculate played) */
    int played; /* This is the total accumulated time played in secs */
};

struct char_player_data {
    char* name; /* PC / NPC s name (kill ...  )		*/
    char* short_descr; /* for 'actions'                        	*/
    char* long_descr; /* for 'look'.. Only here for testing   	*/
    char* description; /* Extra descriptions                   	*/
    char* title; /* PC / NPC s title                     	*/
    char* death_cry; /* NPC - message to give in death        */
    char* death_cry2; /* NPC - message to other rooms         */
    int corpse_num; /* what object to use as a corpse        */
    int race; /* PC /NPC's race                       	*/
    byte sex; /* PC / NPC s sex                       	*/
    byte bodytype; /* PC / NPC hit locations                */
    byte prof; /* PC s or NPC s prof                 	*/
    int level; /* PC / NPC s level                     	*/
    byte language; /* the lang he's presently speaking      */
    int hometown; /* PC s Hometown (zone)                 	*/
    byte talks[MAX_TOUNGE]; /* PC s Tounges 0 for NPC           	*/
    struct time_data time; /* PC s AGE in days                 	*/
    int weight; /* PC / NPC s weight                    	*/
    int height; /* PC / NPC s height                    	*/
    int ranking; /* PC / NPC s ranking in fame war */
};

/* Used in CHAR_FILE_U;
 * Changes may require serialization updates in loading code if we want to persist new data
 */
struct char_ability_data {
    signed char str;
    signed char lea;
    signed char intel;
    signed char wil;
    signed char dex;
    signed char con;
    //   ubyte spare1;      /* spare one, if we want to add one */
    //   ubyte spare2;      /* spare one, if we want to add one */
    //   ubyte spare3;      /* spare one, if we want to add one */
    //   ubyte spare4;      /* spare one, if we want to add one */
    /* in case of pwipe, we want some more spares here... */
    //   sh_int spirit;
    int hit;
    sh_int mana;
    sh_int move;
};

/* Used in CHAR_FILE_U;
 * Changes may require serialization updates in loading code if we want to persist new data
 */
struct char_point_data {

    ubyte bodypart_hit[MAX_BODYPARTS]; /* hit points of individual body parts */
    int gold; /* Money carried                           */
    int exp; /* The experience of the player            */
    int spirit; /* well, the spirit */
    int mana_regen = 0; /* bonus mana regen from gear/spells/etc.  can be negative */
    int health_regen = 0; /* bonus health regen from spells etc. */
    int move_regen = 0; /* bonus move regen from spells etc. */

    sh_int OB; /* OB in normal tactics   */
    sh_int damage; /*  damage in normal tactics */
    sh_int ENE_regen; /* Rate at which energy for hitting is regened*/
    sh_int parry; /* parry in normal tactics */
    sh_int dodge; /* dodge. all in this file are tactics independent.*/
    sh_int encumb; /* how encumbered a player is, affects casting and
                          dex skills */
    sh_int willpower; /* strength in mental fights */
    sh_int spell_pen;
    sh_int spell_power;
    sh_int get_spell_power() const { return spell_power; };
    void set_spell_power(sh_int bonus) { spell_power += bonus; };
    sh_int get_spell_pen() const { return spell_pen; };
};

/* char_special_data's fields are fields which are needed while the game
   is running, but are not stored in the playerfile.  In other words,
   a struct of type char_special_data appears in char_data but NOT
   char_file_u.  If you want to add a piece of data which is automatically
   saved and loaded with the playerfile, add it to char_special2_data.
*/

struct char_special_data {
    struct char_data* fighting; /* Opponent                             */
    struct char_data* hunting; /* Hunting person..                     */

    long affected_by; /* Bitvector for spells/skills affected by */
    sh_int resistance; /* bitvector for resistances */
    sh_int vulnerability; /* bitvector for vulnerabilities */

    sh_int position; /* Standing or ...                         */
    sh_int default_pos; /* Default position for NPC                */

    int carry_weight; /* Carried weight                          */
    sh_int worn_weight; /* Worn weight :)                          */
    sh_int encumb_weight; /* weight for skill encumberance            */

    byte carry_items; /* Number of items carried                 */
    int timer; /* Timer for update                        */
    int was_in_room; /* storage of location for linkdead people */

    int ENERGY; /* current energy */
    sh_int current_parry; /*parry currently affected by 'parry split' */

    signed char last_direction; /* The last direction the monster went     */
    int attack_type; /* The Attack Type Bitvector for NPC's     */

    struct alias_list* alias; /* aliases, 0 for mobs */

    char* poofIn; /* Description on arrival of a god.	       */
    /* also a stack pointer for special mobs   */
    char* poofOut; /* Description upon a god's exit.	       */
    /* also a list pointer for special mobs    */
    int invis_level; /* level of invisibility		       */
    /* also a stack pointer for special mobs   */

    union {
        struct char_data* reply_ptr;
        int* prog_number; /* also a call list pointer for special mobs */
    } union1;

    int store_prog_number; /* in database, stores prog_numbers for mobiles,*/
    /* in the game, can be used for PCs as for   */
    /* mobiles with SPECIAL flag set */
    struct info_script* script_info; /* Pointer to char_script (protos.h) */
    /* 0 if no script */
    int script_number; /* vnum of script */

    union {
        int reply_number;
        int* prog_point; /* and the call point list for that*/
    } union2;

    struct memory_rec* memory; /* List of attackers to remember */
    sh_int current_bodypart; /* The number of current bodypart */

    ubyte tactics; /* combat tactics of a person */
    /* also, program pointer in call list for npc */

    int prompt_number; /* which prompt to use if PRF_DISPTEXT is set */
    /* for players, and difficulty_coof for mobs */

    int prompt_value; /* value to be inserted into text prompt */
    /* or zon_table index for NPC */
    int homezone; /* zone where it was loaded */
    int load_line; /* the line in zone that loaded the mob */

    byte trophy_line; /* for mobs, 0-4 in each zone */

    int board_point[MAX_MAXBOARD]; /* pointers on the current messages */

    int null_speed; /*UPDATE* For temporary use, should be removed later*/
    int str_speed; /*UPDATE* For temporary use, should be removed later*/

    int butcher_item; /* virtual item to load when buther corpse, 0 if none*/

    int was_ambushed;
    int attacked_level; /* the highest level of the attackers, NPC only */
    byte hide_value; /* how good you are hidden, if at all */

    int mental_delay;
    int trap_number; /* used to determine #s in trap */
    char* recite_lines; /* For reciters, how far read? */
    ubyte shooting; /* shooting speed for archery spec*/
    ubyte casting; /*  casting speed for spell casters*/
};

/* defines for hide_flags */
#define HIDING_WELL 0x01
#define HIDING_SNUCK_IN 0x04

/* Used in CHAR_FILE_U;
 * Changes may require serialization updates in loading code if we want to persist new data
 */
struct char_special2_data {
    long idnum; /* player's idnum			*/
    int load_room; /* Which room to place char in		*/
    int spells_to_learn; /* How many can you learn yet this level*/
    int alignment; /* +-1000 for alignments          */
    long act; /* act flag for NPC's; player flag for PC's */
    long pref; /* preference flags for PC's,
                                                racial agg/non-agg NPCs		*/
    int wimp_level; /* Below this # of hit points, flee! */
    byte freeze_level; /* Level of god who froze char, if any	*/
    int bad_pws; /* number of bad password attemps	*/
    /* also a call mask for special mobiles */
    int saving_throw; /* saving throw for new mobiles */
    int rawPerception; /* a raw value without any overrides applied. can be outside of the 0 to 100
                          range */
    int perception; /* perception changes between 0 and 100 */
    int conditions[3]; /* Drunk full etc.			*/

    int mini_level;
    int max_mini_level;
    int morale; /* flag to account for good/evil zones, and such */
    int owner;

    ubyte rerolls; /* Number of rerolls that has happened */
    int leg_encumb; /* how encumbered is char for movement/dodging? */
    int rp_flag; /* Special flag for PC, racial behaviour for - */
    /* NPC (bitvector) */
    int retiredon; /* time of retirement */
    int hide_flags; /* flag set for hide info */
    long will_teach;
};

enum source_type {
    SOURCE_PLAYER,
    SOURCE_MOB,
    SOURCE_ROOM,
    SOURCE_ITEM,
    SOURCE_OTHER,
    SOURCE_INVALID,
};

struct affection_source {
    source_type type;
    int source_id;
    void* source;
};

/* Used in CHAR_FILE_U Change with the utmost care */
struct affected_type {
    sh_int type; /* The type of spell that caused this      */

    int duration; /* For how long its effects will last      */
    char time_phase; /* when exactly in the tick it was cast  */
    /*  is set in affect_to_char, room */

    sh_int modifier; /* This is added to apropriate ability     */
    sh_int location; /* Tells which ability to change(APPLY_XXX)*/
    long bitvector; /* Tells which bits to set (AFF_XXX)       */
    sh_int counter;

    struct affected_type* next;
};

struct follow_type {
    int fol_number; /* abs_number of the follower, for safety */
    struct char_data* follower;
    struct follow_type* next;
};

struct mount_data_type {
    struct char_data* mount;
    int mount_number;

    struct char_data* rider;
    int rider_number;

    struct char_data* next_rider;
    int next_rider_number;
};

struct alias_list {
    char keyword[20];
    char* command;
    struct alias_list* next;
};

struct char_prof_data {
    int prof_coof[MAX_PROFS + 1]; /* 100 would mean 100% in that class */
    int prof_level[MAX_PROFS + 1];
    long prof_exp[MAX_PROFS + 1];
    int specializations[5];

    long color_mask;
    char colors[16];
    int specialization;
};

struct specialization_info {
    virtual ~specialization_info() { }

    virtual std::string to_string(char_data& character) const = 0;
};

struct elemental_spec_data : public specialization_info {
    elemental_spec_data()
        : exposed_target(NULL)
        , spell_id(0)
    {
    }

    void reset()
    {
        exposed_target = NULL;
        spell_id = 0;
    }

    /* Target (if any) that the character has 'exposed' to their element. */
    char_data* exposed_target;
    int spell_id;

    virtual std::string to_string(char_data& character) const;

protected:
    void report_exposed_data(std::ostringstream& message_writer) const;
};

struct cold_spec_data : public elemental_spec_data {
public:
    cold_spec_data()
        : total_chill_ray_count(0)
        , successful_chill_ray_count(0)
        , failed_chill_ray_count(0)
        , total_chill_ray_damage(0)
        , total_cone_of_cold_count(0)
        , successful_cone_of_cold_count(0)
        , failed_cone_of_cold_count(0)
        , total_cone_of_cold_damage(0)
        , total_energy_sapped(0)
    {
    }

    void on_chill_ray_success(int damage);
    void on_chill_ray_fail(int damage);

    void on_cone_of_cold_success(int damage);
    void on_cone_of_cold_failed(int damage);

    void on_chill_applied(int chill_amount);

    int get_chill_ray_count() const { return total_chill_ray_count; }
    int get_successful_chills() const { return successful_chill_ray_count; }
    int get_saved_chills() const { return failed_chill_ray_count; }
    double get_chill_success_percentage() const
    {
        return double(successful_chill_ray_count) / double(total_chill_ray_count);
    }

    int get_cone_count() const { return total_cone_of_cold_count; }
    int get_successful_cones() const { return successful_cone_of_cold_count; }
    int get_saved_cones() const { return failed_cone_of_cold_count; }
    double get_cone_success_percentage() const
    {
        return double(successful_cone_of_cold_count) / double(total_cone_of_cold_count);
    }

    long get_total_energy_sapped() const { return total_energy_sapped; }

    virtual std::string to_string(char_data& character) const;

private:
    /* Total number of chill rays cast. */
    int total_chill_ray_count;

    /* Successful chill ray casts. */
    int successful_chill_ray_count;

    /* Failed chill ray casts. */
    int failed_chill_ray_count;

    /* Total damage dealt by chill ray. */
    int total_chill_ray_damage;

    /* Total number of cone of cold casts. */
    int total_cone_of_cold_count;

    /* Successful cone of cold casts. */
    int successful_cone_of_cold_count;

    /* Failed cone of cold casts. */
    int failed_cone_of_cold_count;

    /* Total damage from cone of cold. */
    int total_cone_of_cold_damage;

    /* Total energy sapped from the target. */
    long total_energy_sapped;
};

struct fire_spec_data : public elemental_spec_data {
    virtual std::string to_string(char_data& character) const;
};

struct lightning_spec_data : public elemental_spec_data {
    virtual std::string to_string(char_data& character) const;
};

struct darkness_spec_data : public elemental_spec_data {
    virtual std::string to_string(char_data& character) const;
};

struct arcane_spec_data : public elemental_spec_data {
    virtual std::string to_string(char_data& character) const;
};

struct defender_data : public specialization_info {
    defender_data()
        : last_block_time(0)
        , next_block_available(0)
        , is_blocking(false)
        , blocked_damage(0) {};

    /* The last time the player used the "block" command. */
    time_t last_block_time;

    /* Time the player can use the "block" command again. */
    time_t next_block_available;

    /* True if the player is currently "blocking". */
    bool is_blocking;

    void add_blocked_damage(int damage) { blocked_damage += damage; }
    unsigned int get_total_blocked_damage() { return blocked_damage; }

    virtual std::string to_string(char_data& character) const;

private:
    /* Total damage blocked by defender spec this session. */
    unsigned int blocked_damage;
};

struct battle_mage_spec_data : public specialization_info {
    virtual std::string to_string(char_data& character) const;
};

struct wild_fighting_data : public specialization_info {
    wild_fighting_data()
        : is_frenzying(false)
        , rush_forward_damage(0) {};

    /* True if the character is currently in a frenzy. */
    bool is_frenzying;

    /* Gets the energy regeneration multiplier for the character. */
    double get_eneregy_regen_multiplier(int current_hp, int max_hp);

    /* Gets the damage multiplier for the character. */
    double get_damage_multiplier(int current_hp, int max_hp);

    void add_rush_damage(int damage) { rush_forward_damage += damage; }
    unsigned int get_total_rush_damage() { return rush_forward_damage; }

    virtual std::string to_string(char_data& character) const;

private:
    /* Total extra damage added by rushing forward wildly. */
    unsigned int rush_forward_damage;
};

struct light_fighting_data : public specialization_info {
    light_fighting_data()
        : light_fighting_extra_hits(0) {};

    /* Weapon that is currently being used in the off-hand. */
    obj_data* off_hand_weapon;

    /* Current energy of the off-hand.  */
    sh_int off_hand_energy;

    /* Rate at which energy for hitting is regenerated for the off-hand. */
    sh_int off_hand_energy_regen;

    void add_light_fighting_proc() { ++light_fighting_extra_hits; }
    unsigned int get_total_light_fighting_procs() { return light_fighting_extra_hits; }

    virtual std::string to_string(char_data& character) const;

private:
    /* Total extra damage added by light fighting. */
    unsigned int light_fighting_extra_hits;
};

struct heavy_fighting_data : public specialization_info {
    heavy_fighting_data()
        : heavy_fighting_damage(0) {};

    void add_heavy_fighting_damage(int damage) { heavy_fighting_damage += damage; }
    unsigned int get_total_heavy_fighting_damage() { return heavy_fighting_damage; }

    virtual std::string to_string(char_data& character) const;

private:
    /* Total extra damage added by heavy fighting. */
    unsigned int heavy_fighting_damage;
};

/* ========== Structure for storing specialization information ============= */
struct specialization_data {
    specialization_data()
        : current_spec_info(NULL)
        , current_spec(game_types::PS_None) {};

    ~specialization_data() { reset(); }

    void reset();

    void set(char_data& character);

    specialization_info* current_spec_info;

    bool is_mage_spec() const
    {
        if (current_spec == game_types::PS_None)
            return false;

        return current_spec == game_types::PS_Darkness || current_spec == game_types::PS_Arcane || current_spec == game_types::PS_Fire || current_spec == game_types::PS_Cold || current_spec == game_types::PS_Lightning || current_spec == game_types::PS_BattleMage;
    }

    elemental_spec_data* get_mage_spec() const
    {
        if (is_mage_spec()) {
            return static_cast<elemental_spec_data*>(current_spec_info);
        }

        return NULL;
    }

    game_types::player_specs get_current_spec() const { return current_spec; }
    std::string to_string(char_data& character) const;

private:
    game_types::player_specs current_spec;
};

/* ========== Structure used for storing skill data ============= */
struct player_skill_data {
    byte skills[MAX_SKILLS];
    byte knowledge[MAX_SKILLS];
};

/* ========== Structures used for tracking player damage ============= */
class damage_details {
public:
    damage_details()
        : total_damage(0)
        , instance_count(0)
        , largest_damage(0)
    {
    }

    virtual ~damage_details() { }

    void add_damage(int damage)
    {
        total_damage += damage;
        instance_count++;
        largest_damage = std::max(largest_damage, damage);
    }

    float get_average_damage() const
    {
        return static_cast<float>(total_damage) / static_cast<float>(instance_count);
    }

    int get_total_damage() const { return total_damage; }
    int get_instance_count() const { return instance_count; }
    int get_largest_damage() const { return largest_damage; }

    virtual void reset()
    {
        total_damage = 0;
        instance_count = 0;
        largest_damage = 0;
    }

private:
    int total_damage;
    int instance_count;
    int largest_damage;
};

class player_damage_details {
public:
    player_damage_details()
        : damage_map()
        , elapsed_combat_seconds(0) {};

    void add_damage(int skill_id, int damage) { damage_map[skill_id].add_damage(damage); }
    void tick(float delta) { elapsed_combat_seconds += delta; }
    void reset()
    {
        damage_map.clear();
        elapsed_combat_seconds = 0;
    }

    std::string get_damage_report(const char_data* character) const;

private:
    std::map<int, damage_details> damage_map;
    float elapsed_combat_seconds;
};

class timed_damage_details : public damage_details {
public:
    timed_damage_details()
        : damage_details()
        , elapsed_combat_seconds(0)
    {
    }
    virtual ~timed_damage_details() { }

    float get_combat_time() const { return elapsed_combat_seconds; }
    float get_dps() const
    {
        return static_cast<float>(get_total_damage()) / std::max(elapsed_combat_seconds, 0.5f);
    }
    void tick(float delta) { elapsed_combat_seconds += delta; }

    virtual void reset()
    {
        damage_details::reset();
        elapsed_combat_seconds = 0;
    }

private:
    float elapsed_combat_seconds;
};

class group_damaga_data {
public:
    group_damaga_data()
        : damage_map() {};

    void add_damage(struct char_data* character, int damage)
    {
        damage_map[character].add_damage(damage);
    }
    void track_time(struct char_data* character, float elapsed_seconds)
    {
        damage_map[character].tick(elapsed_seconds);
    }
    void remove(struct char_data* character) { damage_map.erase(character); }
    void reset() { damage_map.clear(); }

    std::string get_damage_report() const;

private:
    std::map<struct char_data*, timed_damage_details> damage_map;
};

/* ================== Structure for grouping ===================== */
class group_data {
public:
    /* Groups cannot exist without a leader. */
    group_data(struct char_data* in_leader)
        : leader(in_leader)
        , pc_count(0)
    {
        add_member(in_leader);
    };

    void add_member(struct char_data* member);
    bool remove_member(struct char_data* member);

    void track_damage(struct char_data* character, int damage)
    {
        damage_report.add_damage(character, damage);
    }
    void track_combat_time(struct char_data* character, float elapsed_seconds);
    void reset_damage();
    std::string get_damage_report() const { return damage_report.get_damage_report(); }

    struct char_data* get_leader() const { return leader; }
    bool is_member(struct char_data* character) const;
    bool is_leader(struct char_data* character) const { return character == leader; }
    int get_pc_count() const { return pc_count; }

    void get_pcs_in_room(char_vector& pc_vec, int room_number) const;
    size_t size() const { return members.size(); }
    char_iter begin() { return members.begin(); }
    char_iter end() { return members.end(); }
    const_char_iter begin() const { return members.begin(); }
    const_char_iter end() const { return members.end(); }
    struct char_data* at(size_t index) { return members.at(index); }
    bool contains(const char_data* character)
    {
        return std::find(members.begin(), members.end(), character) != members.end();
    }

private:
    struct char_data* leader;
    char_vector members;

    group_damaga_data damage_report;
    int pc_count;
};

/* ================== Structure for player/non-player ===================== */
struct char_data {
public:
    int get_cur_str() const { return tmpabilities.str; }
    int get_cur_int() const { return tmpabilities.intel; }
    int get_cur_wil() const { return tmpabilities.wil; }
    int get_cur_dex() const { return tmpabilities.dex; }
    int get_cur_con() const { return tmpabilities.con; }
    int get_cur_lea() const { return tmpabilities.lea; }

    int get_max_str() const { return abilities.str; }
    int get_max_int() const { return abilities.intel; }
    int get_max_wil() const { return abilities.wil; }
    int get_max_dex() const { return abilities.dex; }
    int get_max_con() const { return abilities.con; }
    int get_max_lea() const { return abilities.lea; }

    int get_level() const { return player.level; }
    bool is_legend() const { return player.level >= LEVEL_MAX; }
    // Returns the level of the player, or the 'maximum' level, whichever is lower.
    int get_capped_level() const { return std::min(get_level(), LEVEL_MAX); }

    /* Returns the leader of the character's group, if the character is in a group. */
    char_data* get_group_leader() const { return group ? group->get_leader() : NULL; }

    int get_spent_practice_count() const;
    int get_max_practice_count() const;
    void update_available_practice_sessions();

    // Resets all known skills and practice sessions for a character.
    void reset_skills();

    // returns true if the affected pointer is valid
    bool is_affected() const;

    int get_dodge() const { return points.dodge; }

public:
    int abs_number; /* bit number in the control array */
    int player_index; /* Index in player table */
    int nr; /* monster nr (pos in file)      */
    int in_room; /* Location                      */

    struct char_player_data player; /* Normal data                   */
    struct char_ability_data abilities; /* Max. Abilities                 */
    struct char_ability_data tmpabilities; /* Current abilities    */
    struct char_ability_data constabilities; /* Rolled abilities */
    struct char_point_data points; /* Points                        */
    struct char_special_data specials; /* Special playing constants      */
    struct char_special2_data specials2; /* Additional special constants  */
    struct char_prof_data* profs; /* prof cooficients */
    specialization_data extra_specialization_data; /* extra data used by some specializations */
    player_damage_details damage_details; /* structure for storing damage data */
    byte* skills; /* dynam. alloc. array of pracs spent                                         on
                     skills */
    byte* knowledge; /* array of knowledge, computed from
                                                                                        pracs spent
                        at logon */
    struct affected_type* affected; /* affected by what spells       */
    struct obj_data* equipment[MAX_WEAR]; /* Equipment array               */

    struct obj_data* carrying; /* Head of list                  */
    struct descriptor_data* desc; /* NULL for mobiles              */

    struct char_data* next_in_room; /* For room->people - list         */
    struct char_data* next; /* For either monster or ppl-list  */
    struct char_data* next_fighting; /* For fighting list               */
    struct char_data* next_fast_update; /* For fast-update list            */

    struct follow_type* followers; /* List of chars followers       */
    struct char_data* master; /* Who is char following?        */
    int master_number;

    struct mount_data_type mount_data;
    group_data* group; /* The group that the character belongs to.  Can be null. */

    void* temp; /* pointer to any special structures if need be   */
    struct waiting_type delay;
    struct char_data* next_die; /* next to die in the death_waiting_list :(  */
    int classpoints; /* Only used for character creation in interpre.cc new_player_select.  Move
                        variable elsewhere? */
    int interrupt_count = 0; /* Meant to store times interupted so that npc mages know to stop casting in battle */
    int interrupt_time = 0; /* Meant to be a countdown timer to remove 1 from interrupt_count */

    bool spec_busy;
};

struct race_bodypart_data {
    char* parts[MAX_BODYPARTS];
    sh_int percent[MAX_BODYPARTS];
    sh_int bodyparts;
    ubyte armor_location[MAX_BODYPARTS];
};

/* ======================================================================== */
/* ====================== Weather structures and defines ================== */
/* ======================================================================== */
/* How much light is in the land ? */
#define SUN_DARK 0
#define SUN_RISE 1
#define SUN_LIGHT 2
#define SUN_SET 3

/* And how is the sky ? */
#define SKY_CLOUDLESS 0
#define SKY_CLOUDY 1
#define SKY_RAINING 2
#define SKY_LIGHTNING 3
#define SKY_SNOWING 4
#define SKY_BLIZZARD 5

/* And the moon's condition? eight phases total (see weather.cc) */
#define MOON_NEW 0
#define MOON_QUART1 1
#define MOON_HALF1 2
#define MOON_QUART2 3
#define MOON_FULL 4
#define MOON_QUART3 5
#define MOON_HALF2 6
#define MOON_QUART4 7

/* What season is it? */
#define SEASON_SPRING 0
#define SEASON_SUMMER 1
#define SEASON_AUTUMN 2
#define SEASON_WINTER 3

struct weather_data {
    int pressure; /* How is the pressure (Mb)? */
    int change; /* How fast and in what way does it change? */
    int sky[13]; /* How is the sky? cloudy, sunny, etc for each sector_type*/
    int sunlight; /* And how much sun. (day/night etc) */
    int moonlight;
    int moonphase;
    int temperature[13]; /* Temperature in each sector */
    int snow[13]; /* Is there snow on the ground? */
};

/* ***********************************************************************
   File element for player file.  Changes here require changes in
   save/load code in the database.
*************************************************************************/
struct char_file_u {
    byte sex;
    byte prof;
    byte race;
    byte bodytype;
    byte level;
    byte language;
    int player_index; /* Index in player table */
    time_t birth; /* Time of birth of character     */
    int played; /* Number of secs played in total */

    int weight;
    int height;

    char title[80];
    sh_int hometown;
    char description[512];
    byte talks[MAX_TOUNGE];

    struct char_ability_data tmpabilities;
    struct char_ability_data constabilities;

    struct char_point_data points;

    byte skills[MAX_SKILLS];

    struct affected_type affected[MAX_AFFECT];

    struct char_special2_data specials2;
    struct char_prof_data profs;

    time_t last_logon; /* Time (in secs) of last logon */
    char host[HOST_LEN + 1]; /* host of last logon */

    /* char data */
    char name[MAX_NAME_LENGTH + 1];
    char pwd[MAX_PWD_LENGTH + 1];
};

/* *************************************************************
 * The follower structure is used in objsave for saving followers
 * Changing it will result in the loss of followers and eq      *
 ***************************************************************/
struct follower_file_elem {
    int fol_vnum;
    int mount_vnum;
    int wimpy;
    int exp;
    int flag_config;
    int spare1;
    int spare2;
};

/* ************************************************************************
 *  file element for object file. BEWARE: Changing it will ruin rent files *
 ************************************************************************ */

constexpr const sh_int SENTINEL_ITEM_ID_VALUE = -17;
constexpr const sh_int DEPRECATED_ID_VALUE = -255;
struct obj_file_elem {

    sh_int item_number_deprecated; // this used to be the ID number, but it wasn't big enough.

    sh_int value[5];
    int extra_flags;
    int weight;
    int timer;
    long bitvector;
    struct obj_affected_type affected[MAX_OBJ_AFFECT];
    sh_int wear_pos;
    int loaded_by; // idnum of immortal who loaded object.  0 if loaded by a zone command etc.
    int item_number; // this used to be spare2, but we needed more item numbers.
};

#define RENT_UNDEF 0
#define RENT_CRASH 1
#define RENT_RENTED 2
#define RENT_CAMP 3
#define RENT_FORCED 4
#define RENT_TIMEDOUT 5
#define RENT_QUIT 6

/* header block for rent files */
struct rent_info {
    int time;
    int rentcode;
    int net_cost_per_hour;
    int gold;
    int nitems;
    sh_int spare0;
    sh_int spare1;
    sh_int spare2;
    int spare3;
    int spare4;
    int spare5;
    int spare6;
    int spare7;
};

/* ***********************************************************
 *  The following structures are related to descriptor_data   *
 *********************************************************** */

struct txt_block {
    char* text;
    struct txt_block* next;
};

struct txt_q {
    struct txt_block* head;
    struct txt_block* tail;
};

/* modes of connectedness */

#define CON_PLYNG 0
#define CON_NME 1
#define CON_NMECNF 2
#define CON_PWDNRM 3
#define CON_PWDGET 4
#define CON_PWDCNF 5
#define CON_QSEX 6
#define CON_RMOTD 7
#define CON_SLCT 8
#define CON_EXDSCR 9
#define CON_QPROF 10
#define CON_LDEAD 11
#define CON_PWDNQO 12
#define CON_PWDNEW 13
#define CON_PWDNCNF 14
#define CON_CLOSE 15
#define CON_DELCNF1 16
#define CON_DELCNF2 17
#define CON_QRACE 18
#define CON_QOWN 19
#define CON_QOWN2 20
#define CON_CREATE 21
#define CON_CREATE2 22
#define CON_LINKLS 23
#define CON_LATIN 24
#define CON_COLOR 25
#define CON_ACCTPWD 26
#define CON_ACCTSLCT 27
#define CON_ACCTLINKPWD 28
#define CON_ACCTNEWCNF 29
#define CON_ACCTNEWPWD 30
#define CON_ACCTNEWPWDCNF 31
#define CON_ACCTMENU 32
#define CON_ACCTLINKNAME 33
#define CON_ACCTRESETOLD 34
#define CON_ACCTRESETNEW 35
#define CON_ACCTRESETCNF 36
#define CON_ACCTNEWCHAR 37
#define CON_ACCTLEGPWD 38
#define CON_ACCTVERIFY 39
#define CON_ACCTDELCNF1 40

/* modes for flags */
#define DFLAG_IS_SPAMMING 1

struct snoop_data {
    struct char_data* snooping; /* Who is this char snooping		*/
    struct char_data* snoop_by; /* And who is snooping this char	*/
};

#define BLOCK_STR_LEN                     \
    512 /* how much to allocate initially \
           for string_add messages */

struct descriptor_data {
    SocketType descriptor; /* file descriptor for socket	*/
    char* name; /* ptr to name for mail system		*/
    char host[50]; /* hostname				*/
    uint32_t proxy_peer_address; /* pending proxy peer address */
    byte proxy_peer_bytes_read; /* bytes read for pending proxy header */
    bool waiting_for_proxy_header; /* descriptor is waiting for proxy header completion */
    char pwd[MAX_PWD_LENGTH + 1]; /* password			*/
    char account_name[MAX_INPUT_LENGTH]; /* authenticated account login */
    char account_email[MAX_INPUT_LENGTH]; /* authenticated account email */
    char account_password[MAX_ACCOUNT_PASSWORD_LENGTH + 1]; /* transient account password */
    char account_character_name[MAX_INPUT_LENGTH]; /* pending account character action */
    int bad_pws; /* number of bad pw attemps this login	*/
    int pos; /* position in player-file		*/
    int connected; /* mode of 'connectedness'		*/
    //   int	wait;			/* wait for how many loops    	*/
    int desc_num; /* unique num assigned to desc		*/
    long login_time; /* when the person connected		*/
    char* showstr_head; /* for paging through texts		*/
    char* showstr_point; /*		-			*/
    char** str; /* for the modify-str system		*/
    unsigned int max_str; /*  allocated length of *str		*/
    unsigned int len_str; /* present length of *str               */
    unsigned int cur_str; /* current pointer position in *str     */
    int prompt_mode; /* control of prompt-printing		*/
    char buf[MAX_STRING_LENGTH]; /* buffer for raw input			*/
    char last_input[MAX_INPUT_LENGTH]; /* the last input			*/
    char small_outbuf[SMALL_BUFSIZE]; /* standard output bufer		*/
    char* output; /* ptr to the current output buffer	*/
    int bufptr; /* ptr to end of current output		*/
    int bufspace; /* space left in the output buffer	*/
    unsigned char dflags; /* flags for this descriptor            */
    time_t last_input_time; /* time(0) of last_input               */
    struct txt_block* large_outbuf; /* ptr to large buffer, if we need it */
    struct txt_q input; /* q of unprocessed input		*/
    struct char_data* character; /* linked to char			*/
    struct char_data* original; /* original char if switched		*/
    struct snoop_data snoop; /* to snoop people			*/
    struct descriptor_data* next; /* link to next descriptor		*/
    protocol_t* pProtocol;
};

struct msg_type {
    char* attacker_msg; /* message to attacker */
    char* victim_msg; /* message to victim   */
    char* room_msg; /* message to room     */
};

struct message_type {
    struct msg_type die_msg; /* messages when death			*/
    struct msg_type miss_msg; /* messages when miss			*/
    struct msg_type hit_msg; /* messages when hit			*/
    struct msg_type sanctuary_msg; /* messages when hit on sanctuary	*/
    struct msg_type self_msg; /* messages when hit on god		*/
    struct message_type* next; /* to next messages of this kind.	*/
};

struct message_list {
    int a_type; /* Attack type				*/
    int number_of_attacks; /* How many attack messages to chose from. */
    struct message_type* msg; /* List of messages.			*/
};

struct prompt_type {
    char* message;
    int value;
};

struct universal_list {
    int type;
    int number; /* abs_number for ch, whatever else for obj, */
    /* room number optional for rooms*/
    union {
        char_data* ch;
        room_data* room;
        obj_data* obj;
    } ptr;

    universal_list* next;
};

class group_roll {
public:
    char* character_name;
    int roll;

    group_roll(char_data* character_name, int roll)
        : character_name(character_name->player.name)
        , roll(roll)
    {
    }
};

#endif /* STRUCTS_H */
