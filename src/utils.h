/* ************************************************************************
 *   File: utils.h                                       Part of CircleMUD *
 *  Usage: header file: utility macros and prototypes of utility funcs     *
 *                                                                         *
 *  All rights reserved.  See license.doc for complete information.        *
 *                                                                         *
 *  Copyright (C) 1993 by the Trustees of the Johns Hopkins University     *
 *  CircleMUD is based on DikuMUD, Copyright (C) 1990, 1991.               *
 ************************************************************************ */

#ifndef UTILS_H
#define UTILS_H

#include "platdef.h" /* For byte, sh_int, ush_int, etc. */
#include "structs.h" /* For time_info_data */

extern struct weather_data weather_info;
extern sh_int square_root[];
extern char get_current_time_phase(); // returns the portion number of the tick
extern int armor_absorb(struct obj_data* obj);
extern int get_weapon_damage(struct obj_data* obj);

/* public functions */
void recalc_abilities(struct char_data*);

// returns a random number from 0.0 to 1.0
double number();

// returns a random number from 0.0 to max
double number(double max);

// returns a random number in interval [from;to] */
double number_d(double from, double to);

struct time_info_data mud_time_passed(time_t, time_t);
char* PERS(struct char_data*, struct char_data*, int, int);
void retire(struct char_data*);
void unretire(struct char_data*);
struct char_data* find_playing_char(int);
void snuck_out(struct char_data*);
void snuck_in(struct char_data*);
int hide_prof(struct char_data*);
int see_hiding(struct char_data*);
char unaccent(char);
char* str_dup(const char* source);
int str_cmp(char* arg1, char* arg2);
int strn_cmp(char* arg1, char* arg2, int n);
void log(const char* str);
void mudlog(char* str, char type, sh_int level, byte file);
void mudlog_debug_mob(char* buf, char_data* ch);
void mudlog_aliased_mob(char* buf, char_data* ch, char* mob_alias);
void vmudlog(char type, char* format, ...);
void log_death_trap(struct char_data* ch);
int number(int from, int to);
int dice(int number, int size);
void sprintbit(long vektor, char* names[], char* result, int var);
void sprinttype(int type, char* names[], char* result);
int get_real_OB(struct char_data* ch);
int get_real_dodge(struct char_data* ch);
int get_real_parry(struct char_data* ch);
int CHAR_WEARS_OBJ(struct char_data* ch, struct obj_data* obj);
char get_colornum(char_data* ch, int col);
void set_colornum(char_data* ch, int col, int value);

void string_add_init(struct descriptor_data*, char**);
void string_add_finish(struct descriptor_data*);
void string_add(struct descriptor_data*, char*);
int string_to_new_value(char* arg, int* value);
char* nth(int);
void day_to_str(time_info_data* loc_time_info, char* str);
int find_player_in_table(char* name, int idnum);

char* strcpy_lang(char* targ, char* src, byte freq, int maxlen);
void reshuffle(int* arr, int len);

void* create_function(int elem_size, int elem_num, int line, const char* file);
void free_function(void* pnt);

int get_total_fame(char_data* ch);

int get_confuse_modifier(char_data* ch);
int compare_obj_to_proto(obj_data* obj);
struct obj_data* obj_to_proto(obj_data* obj);
void check_inventory_proto(char_data* ch);
void check_equipment_proto(char_data* ch);
sh_int get_race_perception(char_data* ch);
int get_power_of_arda(char_data* ch);
int has_critical_stat_damage(char_data* ch);
int can_breathe(char_data* ch);

struct time_info_data age(struct char_data* ch);

void track_specialized_mage(char_data* mage);
void untrack_specialized_mage(char_data* mage);

int has_alias(char_data* host, char* keyword);
int has_program(char_data* host, int num);

/* defines for fseek */
#ifndef SEEK_SET
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#endif

/* define for tacticss */

#define TACTICS_DEFENSIVE 1
#define TACTICS_CAREFUL 2
#define TACTICS_NORMAL 3
#define TACTICS_AGGRESSIVE 4
#define TACTICS_BERSERK 5

/* defines for shooting speeds*/
#define SHOOTING_SLOW 1
#define SHOOTING_NORMAL 2
#define SHOOTING_FAST 3

/* defines for casting speeds*/
#define CASTING_SLOW 1
#define CASTING_NORMAL 2
#define CASTING_FAST 3

/* defines for mudlog() */

#define OFF 0
#define BRF 1
#define NRM 2
#define SPL 3
#define CMP 4

#define TRUE 1

#define FALSE 0

#define YESNO(a) ((a) ? "YES" : "NO")
#define ONOFF(a) ((a) ? "ON" : "OFF")
#define YESNO_INV(a) ((a) ? "NO" : "YES")
#define ONOFF_INV(a) ((a) ? "OFF" : "ON")

#define LOWER(c) (((c) >= 'A' && (c) <= 'Z') ? ((c) + ('a' - 'A')) : (c))

#define UPPER(c) (((c) >= 'a' && (c) <= 'z') ? ((c) + ('A' - 'a')) : (c))

/* Functions in utility.c
 #define MAX(a,b) (((a) > (b)) ? (a) : (b))
 #define MIN(a,b) (((a) < (b)) ? (a) : (b)) */

#define ISNEWL(ch) ((ch) == '\n' || (ch) == '\r')

#define IF_STR(st) ((st) ? (st) : "\0")

#define CAP(st) (*(st) = UPPER(*(st)))

/* #define CREATE(result, type, number)  do {\
        if (!((result) = (type *) calloc ((number), sizeof(type))))\
                { perror("malloc failure"); abort(); } } while(0) */

// second thought taken, let's try another variant first

#define CREATE(result, type, number)                                                   \
    do {                                                                               \
        result = (type*)create_function(sizeof(type) + 1, number, __LINE__, __FILE__); \
    } while (0)

/* #define CREATE(result, type, number) do{\
       result = (type *)create_function( sizeof(type), number, __LINE__, __FILE__);\
       result = (type *)create_pointer; }while(0) */

#define CREATE1(result, type) CREATE(result, type, 1)
#define RELEASE(pointer)                      \
    do {                                      \
        if ((pointer) && global_release_flag) \
            free_function(pointer);           \
        (pointer) = 0;                        \
    } while (0)

#define RECREATE(result, type, number, numb2)            \
    do {                                                 \
        void* tmpp = 0;                                  \
        CREATE(tmpp, type, number);                      \
        if (numb2 > 0)                                   \
            memmove(tmpp, result, sizeof(type) * numb2); \
        RELEASE(result);                                 \
        result = (type*)tmpp;                            \
    } while (0)
/* #define CREATE(result, type, number) do{\
          if((number) == 0){\
            log("CREATE: Wanted to allocate zero elements");\
            exit(0);\
          }\
          (result) = new type[number];\
          bzero((char *) (result), ((number)*sizeof(type))); }while(0)

#define CREATE1(result, type) do{(result) = new(type);\
                              bzero((char *)(result), sizeof(type));}while(0)

#define RELEASE(pointer) do{\
         if (pointer) delete(pointer);\
          pointer = 0;  } while(0)\

*/

#define IS_SET(flag, bit) ((flag) & (bit))
#define SET_BIT(var, bit) ((var) |= (bit))
#define REMOVE_BIT(var, bit) ((var) &= ~(bit))
#define TOGGLE_BIT(var, bit) ((var) = (var) ^ (bit))

#define IS_NPC(ch) ((ch) && IS_SET((ch)->specials2.act, MOB_ISNPC))
#define IS_MOB(ch) (IS_NPC(ch) && ((ch)->nr > -1))

#define RETIRED(ch) (IS_SET(PLR_FLAGS((ch)), PLR_RETIRED))

#define MOB_FLAGS(ch) ((ch)->specials2.act)
#define PLR_FLAGS(ch) ((ch)->specials2.act)
#define PRF_FLAGS(ch) ((ch)->specials2.pref)

#define MOB_FLAGGED(ch, flag) (IS_NPC(ch) && IS_SET(MOB_FLAGS(ch), (flag)))
#define PLR_FLAGGED(ch, flag) (!IS_NPC(ch) && IS_SET(PLR_FLAGS(ch), (flag)))
#define PRF_FLAGGED(ch, flag) (IS_SET(PRF_FLAGS(ch), (flag)))

#define PLR_TOG_CHK(ch, flag) ((TOGGLE_BIT(PLR_FLAGS(ch), (flag))) & (flag))
#define PRF_TOG_CHK(ch, flag) ((TOGGLE_BIT(PRF_FLAGS(ch), (flag))) & (flag))

#define PLR_MODE(ch) ((ch)->specials2.mode) /* for flags on spam and autoex (and such) */
#define PLR_MODE_ON(ch, flag) (PLR_MODE(ch) & (flag))
#define PLR_MODE_TOG(ch, flag) (PLR_MODE(ch) ^= (flag))

#define SWITCH(a, b) \
    {                \
        (a) ^= (b);  \
        (b) ^= (a);  \
        (a) ^= (b);  \
    }

#define IS_AFFECTED(ch, skill) (IS_SET((ch)->specials.affected_by, (skill)))

#define IS_DARK(room) (                                                                                                                                                                          \
    !world[room].light && (IS_SET(world[room].room_flags, DARK) || ((world[room].sector_type != SECT_INSIDE && world[room].sector_type != SECT_CITY) && (/*weather_info.sunlight == SUN_SET ||*/ \
                                                                        weather_info.sunlight == SUN_DARK))))

#define IS_LIGHT(room) (!IS_DARK(room))

#define IS_SUNLIT(room) ((weather_info.sunlight == SUN_LIGHT || weather_info.sunlight == SUN_RISE) && IS_LIGHT(room))

#define GET_INVIS_LEV(ch) ((ch)->specials.invis_level)

#define WIMP_LEVEL(ch) ((ch)->specials2.wimp_level)

#define HSHR(ch) ((ch)->player.sex ? (((ch)->player.sex == 1) ? "his" : "her") : "its")

#define HSSH(ch) ((ch)->player.sex ? (((ch)->player.sex == 1) ? "he" : "she") : "it")

#define HMHR(ch) ((ch)->player.sex ? (((ch)->player.sex == 1) ? "him" : "her") : "it")

#define AN(string) (strchr("aeiouAEIOU", *string) ? "an" : "a")

#define ANA(obj) (strchr("aeiouyAEIOUY", *(obj)->name) ? "An" : "A")

#define SANA(obj) (strchr("aeiouyAEIOUY", *(obj)->name) ? "an" : "a")

/* Appropriately resolve "an" or "a" for a number, `num' */
#define NANA(num) (num == 8 ? "an" : num == 11 ? "an" \
        : num == 18                            ? "an" \
        : (num >= 80 && num < 89)              ? "an" \
        : (num >= 800 && num < 900)            ? "an" \
        : (num >= 8000 && num < 9000)          ? "an" \
                                               : "a")

#define GET_TACTICS(ch) ((IS_NPC(ch)) ? 0 : (ch)->specials.tactics)
#define SET_TACTICS(ch, value)              \
    do {                                    \
        if (!IS_NPC(ch))                    \
            (ch)->specials.tactics = value; \
    } while (0)

#define GET_SHOOTING(ch) ((IS_NPC(ch)) ? 0 : (ch)->specials.shooting)
#define SET_SHOOTING(ch, value)              \
    do {                                     \
        if (!IS_NPC(ch))                     \
            (ch)->specials.shooting = value; \
    } while (0)

#define GET_CASTING(ch) ((IS_NPC(ch)) ? 0 : (ch)->specials.casting)
#define SET_CASTING(ch, value)              \
    do {                                    \
        if (!IS_NPC(ch))                    \
            (ch)->specials.casting = value; \
    } while (0)

#define GET_POS(ch) ((ch)->specials.position)
#define SET_POS(ch) ((ch)->specials.position)

#define GET_ENERGY(ch) ((ch)->specials.ENERGY)
#define SET_ENERGY(ch) ((ch)->specials.ENERGY)

#define GET_CURRENT_PARRY(ch) ((ch)->specials.current_parry)
#define SET_CURRENT_PARRY(ch) ((ch)->specials.current_parry)

#define GET_COND(ch, i) ((ch)->specials2.conditions[(i)])
#define SET_COND(ch, i) ((ch)->specials2.conditions[(i)])

#define GET_LOADROOM(ch) ((ch)->specials2.load_room)

#define GET_VNUM(ch) (mob_index[ch->nr].virt)
#define GET_INDEX(ch) (IS_NPC(ch) ? -1 : (ch)->player_index)

#define GET_NAME(ch) (IS_NPC(ch) ? (ch)->player.short_descr : (ch)->player.name)

#define GET_TITLE(ch) ((ch)->player.title)

#define GET_LEVEL(ch) ((ch)->player.level)

#define GET_LEVELA(ch) (IS_NPC(ch) ? GET_LEVEL(ch) : std::min(GET_LEVEL(ch), LEVEL_MAX))
#define GET_LEVELB(ch) (IS_NPC(ch) ? GET_LEVEL(ch) : std::min(GET_LEVEL(ch), LEVEL_MAX * 2 / 3 + GET_LEVEL(ch) / 3))

#define GET_PROF_LEVEL(prof, ch) (((prof == PROF_GENERAL) || IS_NPC(ch)) ? GET_LEVEL(ch) : (ch)->profs->prof_level[prof])

#define GET_MAX_RACE_PROF_LEVEL(prof, ch) ((GET_RACE(ch) == RACE_ORC) ? 20 : (GET_RACE(ch) == RACE_URUK) ? (prof == PROF_MAGE) ? 27 : 30 \
                                                                                                         : 30)

#define SET_PROF_LEVEL(prof, ch, val)            \
    do {                                         \
        if (prof != PROF_GENERAL)                \
            (ch)->profs->prof_level[prof] = val; \
    } while (0)

#define GET_URUK_MAGE_PENALTY(prof, ch) ((GET_RACE(ch) == RACE_URUK) ? (prof == PROF_MAGE) ? 100 : 0 : 0)

// (a + b-1) / b will make sure that we round up, not down.
#define GET_PROF_COOF(prof, ch) ((prof == PROF_GENERAL) ? 1000 : (GET_RACE((ch)) == RACE_ORC) ? (square_root[(ch)->profs->prof_coof[(prof)]] * 2 + 2) / 3                         \
        : (GET_RACE((ch)) == RACE_URUK)                                                       ? square_root[(ch)->profs->prof_coof[(prof)]] - GET_URUK_MAGE_PENALTY((prof), (ch)) \
                                                                                              : square_root[(ch)->profs->prof_coof[(prof)]])

#define GET_PROF_POINTS(prof, ch) ((ch)->profs->prof_coof[(int)(prof)])

#define GET_MINI_LEVEL(ch) ((ch)->specials2.mini_level)
#define SET_MINI_LEVEL(ch) ((ch)->specials2.mini_level)

#define GET_MAX_MINI_LEVEL(ch) ((ch)->specials2.max_mini_level)
#define SET_MAX_MINI_LEVEL(ch) ((ch)->specials2.max_mini_level)

#define GET_PROF(ch) ((ch)->player.prof)

#define GET_RACE(ch) ((ch)->player.race)

#define GET_BODYTYPE(ch) ((ch)->player.bodytype)

#define GET_HOME(ch) ((ch)->player.hometown)

#define GET_AGE(ch) (age(ch).year)

#define GET_STR(ch) (((ch)->tmpabilities.str) ? (ch)->tmpabilities.str : -1)
#define GET_STR_BASE(ch) (((ch)->abilities.str) ? (ch)->abilities.str : -1)
#define SET_STR(ch, val) (ch)->tmpabilities.str = (val)
#define SET_STR_BASE(ch, val) (ch)->abilities.str = (val)
// GET_BAL_STR takes account of the unbalancing effects of over-high str
#define GET_BAL_STR(ch) (GET_STR(ch) <= max_race_str[GET_RACE(ch)] ? GET_STR(ch) : (GET_STR(ch) - max_race_str[GET_RACE(ch)]) / 2 + max_race_str[GET_RACE(ch)])

#define GET_LEA(ch) ((ch)->tmpabilities.lea)
#define GET_LEA_BASE(ch) ((ch)->abilities.lea)

#define GET_DEX(ch) ((ch)->tmpabilities.dex)
#define GET_DEX_BASE(ch) ((ch)->abilities.dex)

#define GET_INT(ch) ((ch)->tmpabilities.intel)
#define GET_INT_BASE(ch) ((ch)->abilities.intel)

#define GET_WILL(ch) ((ch)->tmpabilities.wil)
#define GET_WILL_BASE(ch) ((ch)->abilities.wil)

#define GET_CON(ch) ((ch)->tmpabilities.con)
#define GET_CON_BASE(ch) ((ch)->abilities.con)
#define SET_CON_BASE(ch, val) (ch)->abilities.con = (val)
#define SET_CON(ch, val) (ch)->tmpabilities.str = (val)

#define GET_SAVE(ch) ((ch)->specials2.saving_throw)

#define GET_ALIAS(ch) ((ch)->specials.alias)
/* The hit_limit, move_limit, and mana_limit functions are gone.  See
   limits.c for details.
*/
extern struct race_bodypart_data bodyparts[MAX_BODYTYPES];

#define GET_BODYPART(ch, num) ((bodyparts[(ch)->player.race].parts[(num)]))
#define GET_CURRPART(ch) ((bodyparts[(ch)->player.race].parts[((ch)->specials.current_bodypart)]))

#define GET_HIT(ch) ((ch)->tmpabilities.hit)
#define GET_MAX_HIT(ch) ((ch)->abilities.hit)

#define GET_MOVE(ch) ((ch)->tmpabilities.move)
#define GET_MAX_MOVE(ch) ((ch)->abilities.move)

#define GET_MANA(ch) ((ch)->tmpabilities.mana)
#define GET_MAX_MANA(ch) ((ch)->abilities.mana)

#define GET_SPIRIT(ch) ((ch)->points.spirit)

#define GET_GOLD(ch) ((ch)->points.gold)

#define GET_EXP(ch) ((ch)->points.exp)
#define SET_EXP(ch) ((ch)->points.exp)

#define GET_DIFFICULTY(ch) ((ch)->specials.prompt_number)

#define GET_HEIGHT(ch) ((ch)->player.height)

#define GET_WEIGHT(ch) ((ch)->player.weight)

#define GET_SEX(ch) ((ch)->player.sex)

#define GET_DODGE(ch) ((ch)->points.dodge)
#define SET_DODGE(ch) ((ch)->points.dodge)

#define GET_PARRY(ch) ((ch)->points.parry)
#define SET_PARRY(ch) ((ch)->points.parry)

// If:  the adjacent room is not DARK, not INDOORS, and the exit is open or broken:
// and if the adjacent room ain't shadowy either (loman)
#define IS_SUNLIT_EXIT(cur_room, adj_room, door) \
    ((weather_info.sunlight == SUN_LIGHT) && (!IS_SET(world[adj_room].room_flags, DARK)) && (!IS_SET(world[adj_room].room_flags, SHADOWY)) && (!IS_SET(world[adj_room].room_flags, INDOORS)) && ((!IS_SET(world[cur_room].dir_option[door]->exit_info, EX_CLOSED) || (IS_SET(world[cur_room].dir_option[door]->exit_info, EX_ISBROKEN)))))

#define IS_SHADOWY_EXIT(cur_room, adj_room, door) \
    ((IS_SET(world[adj_room].room_flags, SHADOWY)) && (!IS_SET(world[cur_room].dir_option[door]->exit_info, EX_CLOSED)))

#define SUN_PENALTY(ch) (((GET_RACE(ch) == RACE_URUK) || (GET_RACE(ch) == RACE_OLOGHAI) || (GET_RACE(ch) == RACE_ORC) || (GET_RACE(ch) == RACE_MAGUS)) && OUTSIDE(ch) && (weather_info.sunlight == SUN_LIGHT) && (!IS_SET(world[ch->in_room].room_flags, DARK)) && (!IS_SET(world[ch->in_room].room_flags, SHADOWY)))
#define EVIL_RACE(ch) ((GET_RACE(ch) == RACE_URUK) || (GET_RACE(ch) == RACE_ORC) || (GET_RACE(ch) == RACE_MAGUS) || (GET_RACE(ch) == RACE_OLOGHAI) || (GET_RACE(ch) == RACE_HARADRIM))

#define GET_OB(ch) ((ch)->points.OB)
#define SET_OB(ch) ((ch)->points.OB)

#define GET_SPELL_PEN(ch) ((ch)->points.spell_pen)
#define SET_SPELL_PEN(ch) ((ch)->points.spell_pen)

#define GET_SPELL_POWER(ch) ((ch)->points.spell_power)
#define SET_SPELL_POWER(ch) ((ch)->points.spell_power)

#define GET_WILLPOWER(ch) ((ch)->points.willpower)

#define GET_ENE_REGEN(ch) ((ch)->points.ENE_regen)
#define SET_ENE_REGEN(ch) ((ch)->points.ENE_regen)

#define GET_DAMAGE(ch) ((ch)->points.damage)
#define SET_DAMAGE(ch) ((ch)->points.damage)

#define GET_AMBUSHED(ch) ((ch)->specials.was_ambushed)
#define GET_LOADLINE(ch) ((ch)->specials.load_line)

#define GET_LOADZONE(ch) ((ch)->specials.homezone)

#define SPELLS_TO_LEARN(ch) ((ch)->specials2.spells_to_learn)

#define GET_IDNUM(ch) (IS_NPC(ch) ? -1 : (ch)->specials2.idnum)

#define AWAKE(ch) (GET_POS(ch) > POSITION_SLEEPING)

#define GET_RAW_SKILL(ch, i) ((ch)->knowledge ? (GET_BODYTYPE(ch) == 2) ? 0 : (((ch)->knowledge)[i]) : 80)
#define GET_SKILL(ch, i) (IS_AFFECTED(ch, AFF_CONFUSE) ? ((((ch)->knowledge ? (GET_BODYTYPE((ch)) == 2) ? 0 : (((ch)->knowledge)[i]) : 80)) - get_confuse_modifier((ch))) : ((ch)->knowledge ? (GET_BODYTYPE(ch) == 2) ? 0 : (((ch)->knowledge)[i]) : 80))

//*- temporarily prevented pets from having skills.. will need to address it later in a proper way
#define SET_SKILL(ch, i, pct)      \
    {                              \
        if ((ch)->skills)          \
            (ch)->skills[i] = pct; \
    }

#define GET_RAW_KNOWLEDGE(ch, i) ((ch)->knowledge ? (((ch)->knowledge)[i]) : 80)
#define GET_KNOWLEDGE(ch, i) (IS_AFFECTED(ch, AFF_CONFUSE) ? ((((ch)->knowledge ? (((ch)->knowledge)[i]) : 80)) - get_confuse_modifier((ch))) : ((ch)->knowledge ? (((ch)->knowledge)[i]) : 80))

#define SET_KNOWLEDGE(ch, i, pct)     \
    {                                 \
        if ((ch)->knowledge)          \
            (ch)->knowledge[i] = pct; \
    }

#define GET_ABS_NUM(ch) ((ch)->abs_number)

#define GET_POSITION(ch) ((ch)->specials.position)

#define CALL_MASK(ch) ((ch)->specials2.bad_pws)

#define WAIT_STATE_BRIEF(ch, cycle, commd, subcommd, prir, new_flag)                    \
    do {                                                                                \
        char_data* tmpch;                                                               \
        if (ch->delay.wait_value != 0) {                                                \
            if (prir >= ch->delay.priority) {                                           \
                ch->delay.subcmd = -1;                                                  \
                complete_delay(ch);                                                     \
                abort_delay(ch);                                                        \
            } else {                                                                    \
                send_to_char("Possible bug - double delay. Please notify Imps.\n", ch); \
                log("double delay?\n");                                                 \
                break;                                                                  \
            }                                                                           \
        }                                                                               \
        ch->delay.wait_value = cycle;                                                   \
        ch->delay.cmd = commd;                                                          \
        ch->delay.targ1.type = ch->delay.targ2.type = TARGET_IGNORE;                    \
        ch->delay.subcmd = subcommd;                                                    \
        /*ch->delay.num = ch->abs_number;*/                                             \
        ch->delay.priority = prir;                                                      \
        SET_BIT(ch->specials.affected_by, new_flag);                                    \
        tmpch = waiting_list;                                                           \
        if (tmpch == ch)                                                                \
            tmpch = waiting_list = ch->delay.next;                                      \
        if (!tmpch)                                                                     \
            waiting_list = ch;                                                          \
        else {                                                                          \
            while (tmpch->delay.next) {                                                 \
                if (tmpch->delay.next == ch)                                            \
                    tmpch->delay.next = ch->delay.next;                                 \
                else                                                                    \
                    tmpch = tmpch->delay.next;                                          \
            }                                                                           \
            if (tmpch != ch)                                                            \
                tmpch->delay.next = ch;                                                 \
        }                                                                               \
        ch->delay.next = 0;                                                             \
    } while (0)

#define WAIT_STATE_FULL(ch, cycle, commd, subcommd, prir, flag, dgt, argument, new_flag, data_type) \
    do {                                                                                            \
        struct char_data* tmpch;                                                                    \
        if (ch->delay.wait_value != 0) {                                                            \
            if (prir >= ch->delay.priority) {                                                       \
                ch->delay.subcmd = -1;                                                              \
                complete_delay(ch);                                                                 \
                abort_delay(ch);                                                                    \
            } else {                                                                                \
                send_to_char("Possible bug - double delay. Please notify Imps.\n", ch);             \
                printf("double delay?\n");                                                          \
                break;                                                                              \
            }                                                                                       \
        }                                                                                           \
        ch->delay.wait_value = cycle;                                                               \
        ch->delay.cmd = commd;                                                                      \
        ch->delay.subcmd = subcommd;                                                                \
        ch->delay.targ1.ptr.other = argument;                                                       \
        /*ch->delay.num = ch->abs_number;*/                                                         \
        ch->delay.priority = prir;                                                                  \
        ch->delay.flg = flag;                                                                       \
        ch->delay.targ1.ch_num = dgt;                                                               \
        ch->delay.targ1.type = data_type;                                                           \
        ch->delay.targ2.type = TARGET_IGNORE;                                                       \
        SET_BIT(ch->specials.affected_by, new_flag);                                                \
        tmpch = waiting_list;                                                                       \
        if (tmpch == ch)                                                                            \
            tmpch = waiting_list = ch->delay.next;                                                  \
        if (!tmpch)                                                                                 \
            waiting_list = ch;                                                                      \
        else {                                                                                      \
            while (tmpch->delay.next) {                                                             \
                if (tmpch->delay.next == ch)                                                        \
                    tmpch->delay.next = ch->delay.next;                                             \
                else                                                                                \
                    tmpch = tmpch->delay.next;                                                      \
            }                                                                                       \
            if (tmpch != ch)                                                                        \
                tmpch->delay.next = ch;                                                             \
        }                                                                                           \
        ch->delay.next = 0;                                                                         \
    } while (0)

#define WAIT_STATE(ch, cycle)                                                      \
    do {                                                                           \
        WAIT_STATE_FULL(ch, cycle, 0, 0, 50, 0, 0, 0, AFF_WAITING, TARGET_IGNORE); \
    } while (0)

#define CHECK_WAIT(ch) ((ch) ? ((ch)->wait > 1) : 0)

#define GET_WAIT_PRIORITY(ch) ((ch)->delay.priority)

int CAN_GO(char_data* ch, int door);
int CAN_SEE(char_data* sub);
int CAN_SEE(char_data* sub, char_data* obj, int light_mode = 0);
int can_sense(char_data* sub, char_data* obj);
int CAN_SEE_OBJ(char_data* sub, obj_data* obj);

/* Object And Carry related macros */

#define IS_ARTIFACT(obj) 0

#define GET_ITEM_TYPE(obj) ((obj)->obj_flags.type_flag)

#define CAN_WEAR(obj, part) (IS_SET((obj)->obj_flags.wear_flags, part))

#define GET_OBJ_WEIGHT(obj) ((obj)->obj_flags.weight)

#define CAN_CARRY_W(ch) (2000 + 1000 * GET_STR(ch))

#define CAN_CARRY_N(ch) (5 + GET_DEX(ch) / 2 + GET_LEVEL(ch) / 2)

#define IS_CARRYING_W(ch) ((ch)->specials.carry_weight)

#define GET_WORN_WEIGHT(ch) ((ch)->specials.worn_weight)

#define GET_ENCUMB_WEIGHT(ch) ((ch)->specials.encumb_weight)

#define IS_CARRYING_N(ch) ((ch)->specials.carry_items)

#define IS_TWOHANDED(ch) ((ch)->specials.affected_by & AFF_TWOHANDED)

#define IS_SHADOW(ch) ((IS_NPC(ch) && IS_SET(MOB_FLAGS(ch), MOB_SHADOW)) || (!IS_NPC(ch) && IS_SET(PLR_FLAGS(ch), PLR_ISSHADOW)))

#define CAN_CARRY_OBJ(ch, obj) \
    (((IS_CARRYING_W(ch) + GET_OBJ_WEIGHT(obj)) <= CAN_CARRY_W(ch)) && ((IS_CARRYING_N(ch) + 1) <= CAN_CARRY_N(ch)))

#define CAN_GET_OBJ(ch, obj) \
    (CAN_WEAR((obj), ITEM_TAKE) && CAN_CARRY_OBJ((ch), (obj)) && CAN_SEE_OBJ((ch), (obj)))

#define IS_OBJ_STAT(obj, stat) (IS_SET((obj)->obj_flags.extra_flags, stat))

#define RACE_GOOD(ch) ((GET_RACE(ch) > 0) ? (GET_RACE(ch) < 10) ? 1 : 0 : 0)
#define RACE_EVIL(ch) ((GET_RACE(ch) > 10) ? 1 : 0)
#define RACE_EAST(ch) ((GET_RACE(ch) == 14) ? 1 : 0)
#define RACE_MAGI(ch) ((GET_RACE(ch) == 15) || (GET_RACE(ch) == 18) ? 1 : 0)

#define GET_REROLLS(ch) ((ch)->specials2.rerolls)

#define OBJS(obj, vict) (CAN_SEE_OBJ((vict), (obj)) ? (obj)->short_description : "something")

#define OBJN(obj, vict) (CAN_SEE_OBJ((vict), (obj)) ? fname((obj)->name) : "something")

#define OUTSIDE(ch) (!IS_SET(world[(ch)->in_room].room_flags, INDOORS))

#define EXIT(ch, door) (world[(ch)->in_room].dir_option[door])

/* #define CAN_GO(ch, door) (EXIT(ch,door)  &&  (EXIT(ch,door)->to_room != NOWHERE) \
                          && (!IS_SET(EXIT(ch, door)->exit_info, EX_CLOSED) || IS_SET(EXIT(ch, door)->exit_info, EX_ISBROKEN)) )
*/

#define GET_ALIGNMENT(ch) ((ch)->specials2.alignment)
#define GET_LAWFULNESS(ch) ((ch)->specials2.lawfulness)
#define IS_GOOD(ch) (GET_ALIGNMENT(ch) >= 100)
#define IS_EVIL(ch) (GET_ALIGNMENT(ch) <= -100)
#define IS_NEUTRAL(ch) (!IS_GOOD(ch) && !IS_EVIL(ch))

#define IS_AGGR_TO(ch, vict) \
    (IS_NPC(ch) && (((ch)->specials2.pref & (1 << GET_RACE(vict))) || ((other_side(ch, vict) && !IS_NPC(vict)) && GET_RACE(ch) != 0)))

#define WILL_TEACH(ch, vict) \
    (IS_NPC(ch) && (((ch)->specials2.will_teach & (1 << GET_RACE(vict))) || ((other_side(ch, vict) && !IS_NPC(vict)) && GET_RACE(ch) != 0)))

#define RP_RACE_CHECK(ch, vict) (IS_NPC(ch) && (!(ch)->specials2.rp_flag) || ((ch)->specials2.rp_flag & (1 << GET_RACE(vict))))

#define GET_RP_FLAG(ch) ((ch)->specials2.rp_flag)

#define IS_RIDING(ch) (((ch)->mount_data.mount) && char_exists((ch)->mount_data.mount_number))
#define IS_RIDDEN(ch) (((ch)->mount_data.rider) && char_exists((ch)->mount_data.rider_number))

#define PROF_ABBR(ch) (IS_NPC(ch) ? "--" : prof_abbrevs[(int)GET_PROF(ch)])
#define RACE_ABBR(ch) (IS_NPC(ch) ? "--" : race_abbrevs[(int)GET_RACE(ch)])

#define MOB_AGE_TICKS(ch, tm) (((tm) - (ch)->player.time.logon) / SECS_PER_MUD_HOUR)

universal_list* pool_to_list(universal_list** list, universal_list** pool);
void from_list_to_pool(universal_list** list, universal_list** head,
    universal_list* body);

int get_race_weight(char_data* ch);
int get_race_height(char_data* ch);
sh_int get_race_perception(char_data* ch);
sh_int get_naked_perception(char_data* ch);
sh_int get_naked_willpower(char_data* ch);

// int get_mental_delay(char_data * ch); /* if mental is <= 1, can do it again. */
void set_mental_delay(char_data* ch, int value);

#define GET_MENTAL_DELAY(ch) ((ch)->specials.mental_delay)

#define GET_PERCEPTION(ch) (IS_SHADOW(ch)    \
        ? 100                                \
        : ((ch)->specials2.perception == -1) \
        ? get_race_perception((ch))          \
        : std::min(100, std::max(0, (ch)->specials2.perception)))

#define IS_MENTAL(ch) (IS_SHADOW(ch) || (!IS_NPC(ch) && PRF_FLAGGED(ch, PRF_MENTAL)))

#define GET_HIDING(ch) ((ch)->specials.hide_value)

#define GET_SPEC(ch) ((IS_NPC(ch) || !(ch)->profs) ? 0 : (ch)->profs->specialization)
#define SET_SPEC(ch, value)                        \
    do {                                           \
        if (!IS_NPC(ch) && (ch)->profs)            \
            (ch)->profs->specialization = (value); \
    } while (0)

#define GET_RESISTANCES(ch) ((ch)->specials.resistance)
#define GET_VULNERABILITIES(ch) ((ch)->specials.vulnerability)

#define IS_RESISTANT(ch, attackgroup) (GET_RESISTANCES(ch) & (1 << (attackgroup)))
#define IS_VULNERABLE(ch, attackgroup) (GET_VULNERABILITIES(ch) & (1 << (attackgroup)))

#ifdef SUNPROCESSING
void bzero(char*, int);
#endif
#define CLEAR(x) bzero((char*)(x), sizeof(x))

void show_char_to_char(struct char_data* i, struct char_data* ch, int mode,
    char* pos_line = 0);

#define IS_WATER(room) ((world[(room)].sector_type == SECT_WATER_SWIM) || (world[(room)].sector_type == SECT_WATER_NOSWIM) || (world[(room)].sector_type == SECT_UNDERWATER))

/* Returns the guardian type.  Returns GUARDIAN_INVALID if the mob is not a guardian. */
int get_guardian_type(int race_number, const char_data* in_guardian_mob);

/* Gets the skills array.  It has MAX_SKILLS entries. */
const struct skill_data* get_skill_array();

/* Gets the attack hit type for a given index. */
const struct attack_hit_type& get_hit_text(int w_type);

void add_character_to_group(struct char_data* character, struct char_data* group_leader);
void remove_character_from_group(struct char_data* character, struct char_data* group_leader);

namespace string_func {
bool equals(const char* a, const char* b);
bool is_null_or_empty(const char* a);
bool contains(const char* a, const char* b);
}

#endif /* UTILS_H */
