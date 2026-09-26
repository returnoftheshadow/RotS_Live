#include "platdef.h"
#include <ctype.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "comm.h"
#include "db.h"
#include "interpre.h"
#include "protos.h"
#include "structs.h"
#include "utils.h"
#include "zone.h"

#include <limits>

extern struct room_data world;
extern struct char_data* character_list;
extern struct char_data* mob_proto; /* prototypes for mobs    */
extern struct index_data* mob_index;
extern byte language_number;
extern byte language_skills[];
extern int top_of_mobt;
extern int mobile_master_idnum;
extern int object_master_idnum;

void free_proto(struct char_data* ch);
ACMD(do_shutdown);

void virt_assignmob(struct char_data* mob);
void* virt_program_number(int number);

/* A command turned off because it did harm; says exactly what was typed so
 * it is clear which one, and easy to find if it is ever turned back on. */
void shape_disabled(struct char_data* ch, const char* prefix, const char* typed)
{
    char buf[300];
    int len;

    while (*typed && *typed <= ' ')
        typed++;
    len = strlen(typed);
    while (len > 0 && typed[len - 1] <= ' ')
        len--;
    snprintf(buf, sizeof(buf), "\"%s%.*s\" has been disabled due to a bug.\n\r", prefix, len, typed);
    send_to_char(buf, ch);
}

/* The /52 list range of the zone and script editors: /50 shows only the
 * commands numbered start..end. start 0 = the whole list, end 0 = to the end. */
static void shape_range_text(int start, int end, char* out)
{
    if (start <= 0)
        strcpy(out, "all");
    else if (end <= 0)
        sprintf(out, "from %d", start);
    else
        sprintf(out, "%d-%d", start, end);
}

void shape_range_prompt(struct char_data* ch, int start, int end)
{
    char range[40], line[120];

    shape_range_text(start, end, range);
    sprintf(line, "Enter list range: start [end] (0 = all, blank keeps) [%s]:\n\r", range);
    send_to_char(line, ch);
}

/* Reads "start [end]" typed at the /52 prompt. Blank keeps the range, 0 alone
 * clears it; anything else wrong leaves it as it was. */
void shape_range_set(struct char_data* ch, const char* arg, int* start, int* end)
{
    const long INT_LIMIT = std::numeric_limits<int>::max();
    long num[2];
    int count = 0;
    char* stop;
    char range[40], line[80];

    for (;;) {
        while (*arg && isspace((unsigned char)*arg))
            arg++;
        if (!*arg)
            break;
        if (count == 2) {
            send_to_char("Enter at most two numbers. Range not changed.\n\r", ch);
            return;
        }
        num[count] = strtol(arg, &stop, 10);
        if (stop == arg || (*stop && !isspace((unsigned char)*stop))) {
            send_to_char("Numbers only. Range not changed.\n\r", ch);
            return;
        }
        count++;
        arg = stop;
    }

    if (count == 0)
        return;
    if (count == 1 && num[0] == 0) {
        *start = 0;
        *end = 0;
        send_to_char("List range cleared.\n\r", ch);
        return;
    }
    if (num[0] < 1 || (count == 2 && num[1] < 1) || num[0] > INT_LIMIT
        || (count == 2 && num[1] > INT_LIMIT)) {
        send_to_char("Use numbers 1 or more, or 0 alone to clear. Range not changed.\n\r", ch);
        return;
    }
    if (count == 2 && num[1] < num[0]) {
        send_to_char("End must not be below start. Range not changed.\n\r", ch);
        return;
    }

    *start = num[0];
    *end = (count == 2) ? num[1] : 0;
    shape_range_text(*start, *end, range);
    sprintf(line, "List range set: %s.\n\r", range);
    send_to_char(line, ch);
}

bool shape_range_includes(int start, int end, int number)
{
    return (start <= 0 || number >= start) && (end <= 0 || number <= end);
}

/* The last line of a /50 list; out needs room for 80 characters. */
void shape_range_footer(int start, int end, char* out)
{
    char range[40];

    shape_range_text(start, end, range);
    sprintf(out, "List range: %s (/52 to change).\n\r", range);
}

/* What line wrap will add to the output already waiting to be sent: the
 * starting value of the running total that shape_list_fits keeps. */
int shape_list_begin(struct char_data* ch)
{
    if (!ch->desc || ch->desc->bufptr < 0 || !ch->desc->character
        || !PRF_FLAGGED(ch->desc->character, PRF_WRAP))
        return 0;
    return wrap_added_length(ch->desc->output);
}

/* True if line and then the footer still fit in this pulse's output. A list
 * stops at the first line that does not, so the footer is not lost unless
 * the output was already full before the list began.
 * wrapped carries, from line to line, what line wrap adds to the output already
 * queued; start it with shape_list_begin(). */
bool shape_list_fits(struct char_data* ch, const char* line, const char* footer, int* wrapped)
{
    struct char_data* reader;
    int line_added = 0, footer_added = 0;

    if (!ch->desc)
        return true;
    reader = ch->desc->character;
    if (reader && PRF_FLAGGED(reader, PRF_WRAP)) {
        line_added = wrap_added_length(line);
        footer_added = wrap_added_length(footer);
    }
    if ((int)(strlen(line) + strlen(footer)) + *wrapped + line_added + footer_added
        > output_space_left(ch->desc))
        return false;
    *wrapped += line_added;
    return true;
}

/* Ends a /50 list with the footer. cut = the list stopped early for lack of
 * room: the output then overflows as it would have, showing "**OVERFLOW**". */
void shape_list_finish(struct char_data* ch, const char* footer, bool cut)
{
    send_to_char(footer, ch);
    if (cut && ch->desc)
        output_mark_overflow(ch->desc);
}

int proto_chain[51] = {
    0, 2, 3, 4, 8, 0, 0, 0, 50, 10,
    29, 12, 13, 14, 15, 16, 17, 18, 19, 20,
    21, 22, 23, 24, 25, 26, 27, 28, 9, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 23, 1, 48
};

void recalculate_mob(struct char_data* ch)
{
    struct char_data* mob;
    int level;
    if (!SHAPE_PROTO(ch)) {
        send_to_char("Shaping alarmed, orders mixed. Notify implementors.\n\r", ch);
        return;
    }
    mob = SHAPE_PROTO(ch)->proto;
    if (!mob) {
        send_to_char("No mob to recalculate. No good :( \n\r", ch);
        return;
    }
    level = mob->player.level;
    /* Here goes calculation of the simple mob's parameters... */

    //  mob->specials2.alignment=0;
    mob->abilities.mana = 20 + level;
    mob->abilities.move = 50 + 2 * level;
    mob->tmpabilities.hit = level * level / 3 + 7 * level;
    mob->abilities.hit = level * level / 2 + 9 * level + 10;
    mob->points.OB = level * 4;
    mob->points.parry = level * 2;
    mob->points.dodge = level;
    mob->points.damage = 1 + (level + 1) / 2;
    mob->points.ENE_regen = 70 + level * 2;
    mob->points.exp = 900 + 600 * level * level;
    mob->abilities.move = 50 + level * 2;
    mob->specials2.saving_throw = level / 4;
    //  mob->specials.prog_number=0;
    mob->abilities.str = 7 + level / 2;
    mob->abilities.intel = 7 + level / 2;
    mob->abilities.wil = 7 + level / 2;
    mob->abilities.dex = 7 + level / 2;
    mob->abilities.con = 7 + level / 2;
    mob->abilities.lea = 7 + level / 2;
    mob->specials.position = POSITION_STANDING;
    mob->specials.default_pos = POSITION_STANDING;
    /* player.language here is the editor's index (0 common, 1 animal,
     * 2 human, 3 orc - see language_skills[]), not the skill number. */
    switch (mob->player.race) {
    case RACE_HUMAN:
    case RACE_DWARF:
    case RACE_WOOD:
    case RACE_HOBBIT:
    case RACE_HIGH:
        mob->player.language = 2;
        break;
    case RACE_BEORNING:
        mob->player.language = 1;
        break;
    case RACE_URUK:
    case RACE_HARAD:
    case RACE_ORC:
    case RACE_MAGUS:
        mob->player.language = 3;
        break;
    case RACE_EASTERLING:
        mob->player.language = 0;
        break;
    default:
        mob->player.language = 1;
        break;
    }
    if (level == 0) {
        mob->points.OB = -15;
        mob->points.parry = 0;
        mob->points.dodge = 0;
        mob->points.damage = 1;
        mob->points.exp = 100;
        mob->tmpabilities.hit = 3;
    }
}

int command_simple_convert(int key)
{
    /* Converts simple_mode commands to normal */
    switch (key) {
    case 0:
        return 0;
    case 1:
        return 1;
    case 2:
        return 2;
    case 3:
        return 3;
    case 4:
        return 4;
    case 5:
        return 5;
    case 6:
        return 6;
    //  case 7: return 7;
    case 7:
        return 8;
    case 8:
        return 18;
    case 9:
        return 19;
    case 10:
        return 26;
    case 11:
        return 20;
    case 12:
        return 31;
    case 13:
        return 40;
    case 49:
        return 49;
    case 50:
        return 50;
    default:
        return 0;
    }
}
void clean_text(char* str)
{
    char* s;
    byte startline;

    startline = 1;
    for (s = str; *s; s++) {
        if (startline && (*s == '#'))
            *s = '+';
        if (*s == '~')
            *s = '-';
        if (startline && (*s > ' '))
            startline = 0;
    }
}
int get_permission(int zonnum, struct char_data* ch, int mode)
{
    int num, perm;
    struct owner_list* tmpowner;
    for (num = 0; (num <= top_of_zone_table) && (zone_table[num].number != zonnum); num++)
        ;
    if (num > top_of_zone_table) {
        send_to_char("Warning: this item is outside any zone. Limited permission only.\n\r", ch);
        perm = 0;
    } else {
        tmpowner = zone_table[num].owners;
        /* Callers store the result in their own editor's struct; ch->temp
         * is not always a shape_proto, so it is not written here. */
        if (tmpowner->owner == 0) {
            perm = (mode) ? 0 : 1;
            if (GET_LEVEL(ch) < LEVEL_GOD)
                perm = 0;

        } else {
            perm = 0;
            for (; tmpowner->owner != 0; tmpowner = tmpowner->next)
                if (tmpowner->owner == ch->specials2.idnum)
                    perm = 1;
        }
        if (ch->player.level >= LEVEL_GRGOD)
            perm = 1;

        if (perm == 0)
            send_to_char(" You have no permission to this zone. Limited permission only.\n\r", ch);
    }

    return perm;
}

int shape_standup(struct char_data* ch, int pos)
{
    int tmp;

    tmp = ch->specials.position;

    if (pos != POSITION_SHAPING)
        act("$n stops humming and opens $s eyes.", FALSE, ch, 0, 0, TO_ROOM);
    else
        act("$n sits down and starts chanting softly.", FALSE, ch, 0, 0, TO_ROOM);

    ch->specials.position = pos;

    return tmp;
}

void list_proto(struct char_data* ch, struct char_data* mob); /* forward declaration */
void list_help(struct char_data* ch);
void list_simple_proto(struct char_data* ch, struct char_data* mob); /* forward declaration */
void list_simple_help(struct char_data* ch);

void new_mob(struct char_data* ch)
{
    CREATE1(SHAPE_PROTO(ch)->proto, char_data);
    SHAPE_PROTO(ch)
        ->proto->player.time.birth
        = time(0);
    SHAPE_PROTO(ch)
        ->proto->player.time.played
        = 0;
    SHAPE_PROTO(ch)
        ->proto->player.time.logon
        = time(0);
    SHAPE_PROTO(ch)
        ->proto->specials2.act
        = MOB_ISNPC;
    SHAPE_PROTO(ch)
        ->proto->nr
        = -1;
    CREATE(SHAPE_PROTO(ch)->proto->player.name, char, 6);
    strcpy(SHAPE_PROTO(ch)->proto->player.name, "golem");
    CREATE(SHAPE_PROTO(ch)->proto->player.short_descr, char, 6);
    strcpy(SHAPE_PROTO(ch)->proto->player.short_descr, "golem");
    CREATE(SHAPE_PROTO(ch)->proto->player.long_descr, char, 6);
    strcpy(SHAPE_PROTO(ch)->proto->player.long_descr, "golem");
    CREATE(SHAPE_PROTO(ch)->proto->player.description, char, 8);
    strcpy(SHAPE_PROTO(ch)->proto->player.description, "golem\n\r");
    SHAPE_PROTO(ch)
        ->proto->in_room
        = ch->in_room;
    SHAPE_PROTO(ch)
        ->proto->specials2.pref
        = 0;
    SHAPE_PROTO(ch)
        ->proto->player.weight
        = 5000;
    SHAPE_PROTO(ch)
        ->proto->player.height
        = 150;
    SHAPE_PROTO(ch)
        ->proto->player.prof
        = 0;
    SHAPE_PROTO(ch)
        ->proto->points.gold
        = 0;
    SHAPE_PROTO(ch)
        ->proto->specials.position
        = POSITION_STANDING;
    SHAPE_PROTO(ch)
        ->proto->specials.default_pos
        = POSITION_STANDING;
    SHAPE_PROTO(ch)
        ->proto->specials.butcher_item
        = 0;
    SHAPE_PROTO(ch)
        ->proto->specials2.perception
        = -1;
    SHAPE_PROTO(ch)
        ->proto->specials2.will_teach
        = 0;
    SHAPE_PROTO(ch)
        ->proto->player.corpse_num
        = 0;
    SHAPE_PROTO(ch)
        ->proto->specials.resistance
        = 0;
    SHAPE_PROTO(ch)
        ->proto->specials.vulnerability
        = 0;
    SHAPE_PROTO(ch)
        ->proto->player.death_cry
        = NULL;
    SHAPE_PROTO(ch)
        ->proto->player.death_cry2
        = NULL;
}

/*
 * NOTE: mob is always written in 'M' mode.
 */
void write_proto(FILE* f, struct char_data* m, int num)
{
    fprintf(f, "#%-d\n", num);
    fprintf(f, "%s~\n", m->player.name);
    fprintf(f, "%s~\n", m->player.short_descr);
    fprintf(f, "%s~\n", m->player.long_descr);
    fprintf(f, "%s~\n", m->player.description);
    fprintf(f, "%ld %ld %d N\n",
        m->specials2.act,
        m->specials.affected_by,
        m->specials2.alignment);
    fprintf(f, "%s~\n", m->player.death_cry);
    fprintf(f, "%s~\n", m->player.death_cry2);
    fprintf(f, "%d %d %d %d ",
        m->player.level,
        m->points.OB,
        m->points.parry,
        m->points.dodge);
    fprintf(f, "%d %d %d %d\n",
        m->tmpabilities.hit,
        m->abilities.hit,
        m->points.damage,
        m->points.ENE_regen);
    fprintf(f, "%d %d %d\n",
        m->points.gold,
        m->points.exp,
        17);
    fprintf(f, "%d %d %d %d %ld\n",
        m->specials.position,
        m->specials.default_pos,
        m->player.sex,
        m->player.race,
        m->specials2.pref);
    fprintf(f, "%d %d %d %d %d %d\n",
        m->player.weight,
        m->player.height,
        m->specials.store_prog_number,
        m->specials.butcher_item,
        m->player.corpse_num,
        m->specials2.rp_flag);
    fprintf(f, "%d %d %d %d \n",
        m->player.prof,
        m->abilities.mana,
        m->abilities.move,
        m->player.bodytype);
    fprintf(f, "%d %d %d %d %d %d %d\n",
        m->specials2.saving_throw,
        m->abilities.str,
        m->abilities.intel,
        m->abilities.wil,
        m->abilities.dex,
        m->abilities.con,
        m->abilities.lea);
    fprintf(f, "%d %d %d %d %d %d %ld\n",
        m->player.language,
        m->specials2.perception,
        m->specials.resistance,
        m->specials.vulnerability,
        m->specials.script_number,
        m->points.spirit,
        m->specials2.will_teach);
}

#define DESCRCHANGE(line, addr)                                       \
    do {                                                              \
        if (!IS_SET(SHAPE_PROTO(ch)->flags, SHAPE_SIMPLE_ACTIVE)) {   \
            sprintf(tmpstr, "You are about to change %s:\n\r", line); \
            send_to_char(tmpstr, ch);                                 \
            SHAPE_PROTO(ch)                                           \
                ->position                                            \
                = shape_standup(ch, POSITION_SHAPING);                \
            ch->specials.prompt_number = 1;                           \
            SET_BIT(SHAPE_PROTO(ch)->flags, SHAPE_SIMPLE_ACTIVE);     \
            str[0] = 0;                                               \
            SHAPE_PROTO(ch)                                           \
                ->tmpstr                                              \
                = str_dup(addr);                                      \
            string_add_init(ch->desc, &(SHAPE_PROTO(ch)->tmpstr));    \
            return;                                                   \
        } else {                                                      \
            if (SHAPE_PROTO(ch)->tmpstr) {                            \
                addr = SHAPE_PROTO(ch)->tmpstr;                       \
                clean_text(addr);                                     \
            }                                                         \
            SHAPE_PROTO(ch)                                           \
                ->tmpstr                                              \
                = 0;                                                  \
            SHAPE_PROTO(ch)                                           \
                ->editflag                                            \
                = 0;                                                  \
            shape_standup(ch, SHAPE_PROTO(ch)->position);             \
            ch->specials.prompt_number = 5;                           \
            REMOVE_BIT(SHAPE_PROTO(ch)->flags, SHAPE_SIMPLE_ACTIVE);  \
        }                                                             \
    } while (0);

#define LINECHANGE(line, addr)                                                              \
    do {                                                                                    \
        if (!IS_SET(SHAPE_PROTO(ch)->flags, SHAPE_DIGIT_ACTIVE)) {                          \
            sprintf(tmpstr, "Enter line %s:\n\r[%s]\n\r", line, (addr) ? (char*)addr : ""); \
            send_to_char(tmpstr, ch);                                                       \
            SHAPE_PROTO(ch)                                                                 \
                ->position                                                                  \
                = shape_standup(ch, POSITION_SHAPING);                                      \
            ch->specials.prompt_number = 2;                                                 \
            SET_BIT(SHAPE_PROTO(ch)->flags, SHAPE_DIGIT_ACTIVE);                            \
            return;                                                                         \
        } else {                                                                            \
            str[0] = 0;                                                                     \
            if (!sscanf(arg, "%s", str)) {                                                  \
                SHAPE_PROTO(ch)                                                             \
                    ->editflag                                                              \
                    = 0;                                                                    \
                shape_standup(ch, SHAPE_PROTO(ch)->position);                               \
                ch->specials.prompt_number = 5;                                             \
                REMOVE_BIT(SHAPE_PROTO(ch)->flags, SHAPE_DIGIT_ACTIVE);                     \
                break;                                                                      \
            }                                                                               \
        }                                                                                   \
        if (str[0] != 0) {                                                                  \
            if (!strcmp(str, "%q")) {                                                       \
                send_to_char("Empty line set.\n\r", ch);                                    \
                arg[0] = 0;                                                                 \
            }                                                                               \
            RELEASE(addr);                                                                  \
            /*addr=(char *)calloc(strlen(arg)+1,1);*/                                       \
            CREATE(addr, char, strlen(arg) + 1);                                            \
            strcpy(addr, arg);                                                              \
            tmp1 = strlen(addr);                                                            \
            for (tmp = 0; tmp < tmp1; tmp++) {                                              \
                if (addr[tmp] == '#')                                                       \
                    addr[tmp] = '+';                                                        \
                if (addr[tmp] == '~')                                                       \
                    addr[tmp] = '-';                                                        \
            }                                                                               \
        }                                                                                   \
        REMOVE_BIT(SHAPE_PROTO(ch)->flags, SHAPE_DIGIT_ACTIVE);                             \
        shape_standup(ch, SHAPE_PROTO(ch)->position);                                       \
        ch->specials.prompt_number = 5;                                                     \
        SHAPE_PROTO(ch)                                                                     \
            ->editflag                                                                      \
            = 0;                                                                            \
    } while (0);

void extra_coms_proto(struct char_data* ch, char* argument);

void shape_center_proto(struct char_data* ch, char* arg)
{

    char str[1000], tmpstr[1000];
    // int i,i1;
    int tmp, tmp1, tmp2, tmp3, tmp4, tmp5, choice;
    struct char_data* mob;
    char keymode;
    char* tmpptr = 0;
    choice = 0;

    mob = SHAPE_PROTO(ch)->proto;

    tmp = SHAPE_PROTO(ch)->procedure;
    if ((tmp != SHAPE_NONE) && (tmp != SHAPE_EDIT)) {
        extra_coms_proto(ch, arg);
        return;
    }
    if (tmp == SHAPE_NONE) {
        send_to_char("Enter any non-number for list of commands, 99 for list of editor commands:", ch);
        SHAPE_PROTO(ch)
            ->editflag
            = 0;
        REMOVE_BIT(SHAPE_PROTO(ch)->flags, SHAPE_SIMPLE_ACTIVE);
        return;
    }
    do { /* big loop for chains */
        if (SHAPE_PROTO(ch)->editflag == 0) {
            REMOVE_BIT(SHAPE_PROTO(ch)->flags, SHAPE_CHAIN);
            sscanf(arg, "%s", str);
            if ((str[0] >= '0') && (str[0] <= '9')) {
                SHAPE_PROTO(ch)
                    ->editflag
                    = atoi(str); /* now switching to integers.. */
                str[0] = 0;
                if (SHAPE_PROTO(ch)->editflag == 48)
                    SHAPE_PROTO(ch)
                        ->editflag
                        = 0; /* sorry, it seems necessary */
            } else {
                extra_coms_proto(ch, arg);
                return;
            }
        }
        if (!IS_SET(SHAPE_PROTO(ch)->flags, SHAPE_PROTO_LOADED)) {
            send_to_char("You have no mobile to edit.\n\r", ch);
            SHAPE_PROTO(ch)
                ->editflag
                = 0;
            return;
        }
        if (IS_SET(SHAPE_PROTO(ch)->flags, SHAPE_SIMPLEMODE) && !IS_SET(SHAPE_PROTO(ch)->flags, SHAPE_CHAIN))
            keymode = command_simple_convert(SHAPE_PROTO(ch)->editflag);
        else
            keymode = SHAPE_PROTO(ch)->editflag;
        switch (keymode) {
        case 1:
            /* An empty keyword list or name leaves the mob unusable. */
            if (IS_SET(SHAPE_PROTO(ch)->flags, SHAPE_DIGIT_ACTIVE) && sscanf(arg, "%s", str) == 1 && !strcmp(str, "%q")) {
                send_to_char("Aliases can't be empty.\n\r", ch);
                arg[0] = 0;
            }
            LINECHANGE("ALIASES, keywords separated by spaces,\n\r  e.g. man guard elrond (blank = keep)", SHAPE_PROTO(ch)->proto->player.name)
            if (IS_SET(SHAPE_PROTO(ch)->flags, SHAPE_CHAIN))
                SHAPE_PROTO(ch)
                    ->editflag
                    = proto_chain[1];
            break;
        case 2:
            /* An empty keyword list or name leaves the mob unusable. */
            if (IS_SET(SHAPE_PROTO(ch)->flags, SHAPE_DIGIT_ACTIVE) && sscanf(arg, "%s", str) == 1 && !strcmp(str, "%q")) {
                send_to_char("The reference description can't be empty.\n\r", ch);
                arg[0] = 0;
            }
            LINECHANGE("REFERENCE DESCRIPTION, e.g. a mouse, the Innkeeper, Elrond\n\r  (blank = keep)", SHAPE_PROTO(ch)->proto->player.short_descr)
            if (IS_SET(SHAPE_PROTO(ch)->flags, SHAPE_CHAIN))
                SHAPE_PROTO(ch)
                    ->editflag
                    = proto_chain[2];
            break;
        case 3:
            LINECHANGE("ROOM LINE, shown when the mob is in its default position,\n\r  e.g. A citizen is standing here. (blank = keep, %q = empty)", SHAPE_PROTO(ch)->proto->player.long_descr)
            if (IS_SET(SHAPE_PROTO(ch)->flags, SHAPE_CHAIN))
                SHAPE_PROTO(ch)
                    ->editflag
                    = proto_chain[3];
            break;
        case 4:
            DESCRCHANGE("DETAILED DESCRIPTION, shown by 'look <mob>'", SHAPE_PROTO(ch)->proto->player.description)
            if (IS_SET(SHAPE_PROTO(ch)->flags, SHAPE_CHAIN))
                SHAPE_PROTO(ch)
                    ->editflag
                    = proto_chain[4];
            break;
#undef DESCRCHANGE
#define DIGITCHANGE(line, addr)                                    \
    do {                                                           \
        if (!IS_SET(SHAPE_PROTO(ch)->flags, SHAPE_DIGIT_ACTIVE)) { \
            sprintf(tmpstr, "Enter %s [%d]:\n\r", line, addr);     \
            send_to_char(tmpstr, ch);                              \
            SHAPE_PROTO(ch)                                        \
                ->position                                         \
                = shape_standup(ch, POSITION_SHAPING);             \
            ch->specials.prompt_number = 3;                        \
            SET_BIT(SHAPE_PROTO(ch)->flags, SHAPE_DIGIT_ACTIVE);   \
            return;                                                \
        } else {                                                   \
            tmp = addr;                                            \
            string_to_new_value(arg, &tmp);                        \
            addr = tmp;                                            \
        }                                                          \
        shape_standup(ch, SHAPE_PROTO(ch)->position);              \
        ch->specials.prompt_number = 5;                            \
        REMOVE_BIT(SHAPE_PROTO(ch)->flags, SHAPE_DIGIT_ACTIVE);    \
        SHAPE_PROTO(ch)                                            \
            ->editflag                                             \
            = 0;                                                   \
    } while (0);
#define DIGITCHANGEL(line, addr)                                   \
    do {                                                           \
        if (!IS_SET(SHAPE_PROTO(ch)->flags, SHAPE_DIGIT_ACTIVE)) { \
            sprintf(tmpstr, "Enter %s [%ld]:\n\r", line, addr);    \
            send_to_char(tmpstr, ch);                              \
            SHAPE_PROTO(ch)                                        \
                ->position                                         \
                = shape_standup(ch, POSITION_SHAPING);             \
            ch->specials.prompt_number = 3;                        \
            SET_BIT(SHAPE_PROTO(ch)->flags, SHAPE_DIGIT_ACTIVE);   \
            return;                                                \
        } else {                                                   \
            tmp = addr;                                            \
            string_to_new_value(arg, &tmp);                        \
            addr = tmp;                                            \
        }                                                          \
        shape_standup(ch, SHAPE_PROTO(ch)->position);              \
        ch->specials.prompt_number = 5;                            \
        REMOVE_BIT(SHAPE_PROTO(ch)->flags, SHAPE_DIGIT_ACTIVE);    \
        SHAPE_PROTO(ch)                                            \
            ->editflag                                             \
            = 0;                                                   \
    } while (0);

        case 5:
            if (!IS_SET(SHAPE_PROTO(ch)->flags, SHAPE_DIGIT_ACTIVE))
                send_to_char("  0 SPEC, 1 SENTINEL, 2 SCAVENGER, 3 ISNPC, 4 NOBASH, 5 AGGR, 6 STAY-ZONE,\n\r"
                             "  7 WIMPY, 8 STAY-TYPE, 9 MOUNT, 10 CAN_SWIM, 11 MEMORY, 12 HELPER,\n\r"
                             "  13 AGGR_EVIL, 14 AGGR_NEUT, 15 AGGR_GOOD, 16 BODYGUARD, 17 WRAITH,\n\r"
                             "  18 SWITCH, 19 NORECALC, 20 FAST, 21 IS_PET, 22 HUNTER, 23 ORC_FRIEND,\n\r"
                             "  24 RACE_GUARD, 25 ASSISTANT, 26 GUARDIAN\n\r",
                    ch);
            DIGITCHANGEL("MOB FLAGS: pN sets bit N, mN clears it, one per answer\n\r  (bit 3 ISNPC is always on)", mob->specials2.act)
            mob->specials2.act |= MOB_ISNPC;
            if (IS_SET(SHAPE_PROTO(ch)->flags, SHAPE_CHAIN))
                SHAPE_PROTO(ch)
                    ->editflag
                    = proto_chain[5];
            break;
        case 6:
            if (!IS_SET(SHAPE_PROTO(ch)->flags, SHAPE_DIGIT_ACTIVE))
                send_to_char("  0 SENSE, 1 INFRA, 2 SNEAK, 3 HIDE, 4 DET-MAGIC, 5 CHARM, 6 CURSE, 7 SANCT,\n\r"
                             "  8 TWO-HANDED, 9 INVIS, 10 MOONVISION, 11 POISON, 12 SHIELD, 13 BREATHE,\n\r"
                             "  14 GROUP, 15 CONFUSE, 16 SLEEP, 17 BASH, 18 FLYING, 19 DET_INVIS, 20 FEAR,\n\r"
                             "  21 BLIND, 22 FOLLOW, 23 SWIM, 24 HUNT, 25 EVASION, 26 CASTING, 27 WAITWHEEL,\n\r"
                             "  28 (unused), 29 CONCENTR, 30 HAZE, 31 HALLU\n\r",
                    ch);
            DIGITCHANGEL("AFFECTS: pN sets bit N, mN clears it, one per answer", mob->specials.affected_by);
            if (IS_SET(SHAPE_PROTO(ch)->flags, SHAPE_CHAIN))
                SHAPE_PROTO(ch)
                    ->editflag
                    = proto_chain[6];
            break;
        case 7:
            DIGITCHANGE("ALIGNMENT -1000 to 1000 (good >= 100, evil <= -100)\n\r  (-N sets negative)", mob->specials2.alignment)
            string_to_negative_value(arg, &mob->specials2.alignment);
            if (IS_SET(SHAPE_PROTO(ch)->flags, SHAPE_CHAIN))
                SHAPE_PROTO(ch)
                    ->editflag
                    = proto_chain[7];
            break;
        /*-----------here go new mobs' features...----------*/
        case 8:
            DIGITCHANGE("LEVEL (does not recalculate stats; use recalculate)", mob->player.level);
            if (IS_SET(SHAPE_PROTO(ch)->flags, SHAPE_CHAIN))
                SHAPE_PROTO(ch)
                    ->editflag
                    = proto_chain[8];
            break;
        case 9:
            if (!IS_SET(SHAPE_PROTO(ch)->flags, SHAPE_DIGIT_ACTIVE)) {
                sprintf(tmpstr, "Enter OB PARRY DODGE (3 numbers, blank = keep)\n\rCurrent: %d %d %d\n\r",
                    mob->points.OB, mob->points.parry, mob->points.dodge);
                send_to_char(tmpstr, ch);
                SET_BIT(SHAPE_PROTO(ch)->flags, SHAPE_DIGIT_ACTIVE);
                SHAPE_PROTO(ch)
                    ->position
                    = shape_standup(ch, POSITION_SHAPING);
                ch->specials.prompt_number = 3;
                return;
            } else {
                /* Blank (or no number at all) keeps the current values. */
                tmp = mob->points.OB;
                tmp1 = mob->points.parry;
                tmp2 = mob->points.dodge;
                choice = sscanf(arg, "%d %d %d", &tmp, &tmp1, &tmp2);
                if (choice > 0 && choice != 3) {
                    send_to_char("three numbers required. dropped\n\r", ch);
                    REMOVE_BIT(SHAPE_PROTO(ch)->flags, SHAPE_DIGIT_ACTIVE);
                    shape_standup(ch, SHAPE_PROTO(ch)->position);
                    ch->specials.prompt_number = 5;
                    SHAPE_PROTO(ch)
                        ->editflag
                        = 0;
                    return;
                }
            }
            mob->points.OB = tmp;
            mob->points.parry = tmp1;
            mob->points.dodge = tmp2;
            REMOVE_BIT(SHAPE_PROTO(ch)->flags, SHAPE_DIGIT_ACTIVE);
            shape_standup(ch, SHAPE_PROTO(ch)->position);
            ch->specials.prompt_number = 5;
            SHAPE_PROTO(ch)
                ->editflag
                = 0;
            if (IS_SET(SHAPE_PROTO(ch)->flags, SHAPE_CHAIN))
                SHAPE_PROTO(ch)
                    ->editflag
                    = proto_chain[9];
            break;
        case 10:
            if (!IS_SET(SHAPE_PROTO(ch)->flags, SHAPE_DIGIT_ACTIVE)) {
                sprintf(tmpstr, "Enter MIN_HIT MAX_HIT (rolled per spawn, blank = keep)\n\rCurrent: %d %d\n\r",
                    mob->tmpabilities.hit, mob->abilities.hit);
                send_to_char(tmpstr, ch);
                SET_BIT(SHAPE_PROTO(ch)->flags, SHAPE_DIGIT_ACTIVE);
                SHAPE_PROTO(ch)
                    ->position
                    = shape_standup(ch, POSITION_SHAPING);
                ch->specials.prompt_number = 3;
                return;
            } else {
                tmp = mob->tmpabilities.hit;
                tmp1 = mob->abilities.hit;
                choice = sscanf(arg, "%d %d", &tmp, &tmp1);
                if (choice > 0 && choice != 2) {
                    send_to_char("two numbers required. dropped\n\r", ch);
                    REMOVE_BIT(SHAPE_PROTO(ch)->flags, SHAPE_DIGIT_ACTIVE);
                    shape_standup(ch, SHAPE_PROTO(ch)->position);
                    ch->specials.prompt_number = 5;
                    SHAPE_PROTO(ch)
                        ->editflag
                        = 0;
                    return;
                }
            }
            mob->tmpabilities.hit = tmp;
            mob->abilities.hit = tmp1;
            REMOVE_BIT(SHAPE_PROTO(ch)->flags, SHAPE_DIGIT_ACTIVE);
            shape_standup(ch, SHAPE_PROTO(ch)->position);
            ch->specials.prompt_number = 5;
            SHAPE_PROTO(ch)
                ->editflag
                = 0;
            SHAPE_PROTO(ch)
                ->editflag
                = 0;
            if (IS_SET(SHAPE_PROTO(ch)->flags, SHAPE_CHAIN))
                SHAPE_PROTO(ch)
                    ->editflag
                    = proto_chain[10];
            break;
        case 11:
            DIGITCHANGE("DAMAGE (added x10 to each hit)", mob->points.damage)
            if (IS_SET(SHAPE_PROTO(ch)->flags, SHAPE_CHAIN))
                SHAPE_PROTO(ch)
                    ->editflag
                    = proto_chain[11];
            break;
        case 12:
            DIGITCHANGE("ENERGY REGEN (higher = faster attacks)", mob->points.ENE_regen)
            if (IS_SET(SHAPE_PROTO(ch)->flags, SHAPE_CHAIN))
                SHAPE_PROTO(ch)
                    ->editflag
                    = proto_chain[12];
            break;
        case 13:
            DIGITCHANGE("GOLD (in copper coins)", mob->points.gold)
            if (IS_SET(SHAPE_PROTO(ch)->flags, SHAPE_CHAIN))
                SHAPE_PROTO(ch)
                    ->editflag
                    = proto_chain[13];
            break;
        case 14:
            DIGITCHANGE("XP", mob->points.exp)
            if (IS_SET(SHAPE_PROTO(ch)->flags, SHAPE_CHAIN))
                SHAPE_PROTO(ch)
                    ->editflag
                    = proto_chain[14];
            break;
        /* case 15: here will owner be? */
        case 16:
            if (!IS_SET(SHAPE_PROTO(ch)->flags, SHAPE_DIGIT_ACTIVE))
                send_to_char("  4 sleeping, 5 resting, 6 sitting, 8 standing\n\r", ch);
            tmp3 = mob->specials.position;
            DIGITCHANGE("POSITION", mob->specials.position)
            if (tmp != tmp3 && tmp != POSITION_SLEEPING && tmp != POSITION_RESTING
                && tmp != POSITION_SITTING && tmp != POSITION_STANDING) {
                send_to_char("Position must be 4, 5, 6 or 8. dropped.\n\r", ch);
                mob->specials.position = tmp3;
            }
            if (IS_SET(SHAPE_PROTO(ch)->flags, SHAPE_CHAIN))
                SHAPE_PROTO(ch)
                    ->editflag
                    = proto_chain[16];
            break;
        case 17:
            if (!IS_SET(SHAPE_PROTO(ch)->flags, SHAPE_DIGIT_ACTIVE))
                send_to_char("  4 sleeping, 5 resting, 6 sitting, 8 standing\n\r", ch);
            tmp3 = mob->specials.default_pos;
            DIGITCHANGE("DEFAULT POSITION", mob->specials.default_pos)
            if (tmp != tmp3 && tmp != POSITION_SLEEPING && tmp != POSITION_RESTING
                && tmp != POSITION_SITTING && tmp != POSITION_STANDING) {
                send_to_char("Position must be 4, 5, 6 or 8. dropped.\n\r", ch);
                mob->specials.default_pos = tmp3;
            }
            if (IS_SET(SHAPE_PROTO(ch)->flags, SHAPE_CHAIN))
                SHAPE_PROTO(ch)
                    ->editflag
                    = proto_chain[17];
            break;
        case 18:
            if (!IS_SET(SHAPE_PROTO(ch)->flags, SHAPE_DIGIT_ACTIVE)) {
                sprintf(tmpstr, "Enter SEX (n m f) [%c]:\n\r",
                    mob->player.sex <= SEX_FEMALE ? "nmf"[mob->player.sex] : '?');
                send_to_char(tmpstr, ch);
                SHAPE_PROTO(ch)->position = shape_standup(ch, POSITION_SHAPING);
                ch->specials.prompt_number = 2;
                SET_BIT(SHAPE_PROTO(ch)->flags, SHAPE_DIGIT_ACTIVE);
                return;
            }
            REMOVE_BIT(SHAPE_PROTO(ch)->flags, SHAPE_DIGIT_ACTIVE);
            shape_standup(ch, SHAPE_PROTO(ch)->position);
            ch->specials.prompt_number = 5;
            SHAPE_PROTO(ch)->editflag = 0;
            while (*arg && (*arg <= ' '))
                arg++;

            switch (*arg) {
            case 0: /* blank keeps */
                break;
            case 'm':
            case 'M':
                mob->player.sex = SEX_MALE;
                break;
            case 'f':
            case 'F':
                mob->player.sex = SEX_FEMALE;
                break;
            case 'n':
            case 'N':
                mob->player.sex = SEX_NEUTRAL;
                break;
            default:
                send_to_char("Unrecognized sex.\n\r", ch);
                break;
            }
            if (IS_SET(SHAPE_PROTO(ch)->flags, SHAPE_CHAIN))
                SHAPE_PROTO(ch)
                    ->editflag
                    = proto_chain[18];
            break;
        case 19:
            if (!IS_SET(SHAPE_PROTO(ch)->flags, SHAPE_DIGIT_ACTIVE))
                send_to_char("  0 god/animal, 1 human, 2 dwarf, 3 wood elf, 4 hobbit, 5 high elf,\n\r"
                             "  6 beorning, 11 uruk-hai, 12 harad, 13 orc, 14 easterling, 15 uruk-lhuth,\n\r"
                             "  16 undead, 17 olog-hai, 18 haradrim, 20 troll\n\r",
                    ch);
            tmp3 = mob->player.race;
            DIGITCHANGE("RACE", mob->player.race)
            /* pc_races[] and the other race tables stop at 20. */
            if (tmp != tmp3 && (tmp < 0 || tmp > 20)) {
                send_to_char("Race must be 0-20. dropped.\n\r", ch);
                mob->player.race = tmp3;
            }
            if (IS_SET(SHAPE_PROTO(ch)->flags, SHAPE_CHAIN))
                SHAPE_PROTO(ch)
                    ->editflag
                    = proto_chain[19];
            break;
        case 20:
            DIGITCHANGEL("RACE AGGRESSION: pN per race number (see /19), mN clears", mob->specials2.pref)
            if (IS_SET(SHAPE_PROTO(ch)->flags, SHAPE_CHAIN))
                SHAPE_PROTO(ch)
                    ->editflag
                    = proto_chain[20];
            break;
        case 21:
            DIGITCHANGE("WEIGHT (1/100 lb)", mob->player.weight)
            if (IS_SET(SHAPE_PROTO(ch)->flags, SHAPE_CHAIN))
                SHAPE_PROTO(ch)
                    ->editflag
                    = proto_chain[21];
            break;
        case 22:
            DIGITCHANGE("HEIGHT (cm)", mob->player.height)
            if (IS_SET(SHAPE_PROTO(ch)->flags, SHAPE_CHAIN))
                SHAPE_PROTO(ch)
                    ->editflag
                    = proto_chain[22];
            break;
        /* Here go 'M' fields */
        case 23:
            DIGITCHANGE("PROF", mob->player.prof)
            if (IS_SET(SHAPE_PROTO(ch)->flags, SHAPE_CHAIN))
                SHAPE_PROTO(ch)
                    ->editflag
                    = proto_chain[23];
            break;

        case 24:
            DIGITCHANGE("STAMINA (mana)", mob->abilities.mana)
            if (IS_SET(SHAPE_PROTO(ch)->flags, SHAPE_CHAIN))
                SHAPE_PROTO(ch)
                    ->editflag
                    = proto_chain[24];
            break;
        case 25:
            DIGITCHANGE("MOVE POINTS", mob->abilities.move)
            if (IS_SET(SHAPE_PROTO(ch)->flags, SHAPE_CHAIN))
                SHAPE_PROTO(ch)
                    ->editflag
                    = proto_chain[25];
            break;
        case 26:
            tmp3 = mob->player.bodytype;
            DIGITCHANGE("BODY TYPE: 0 none, 1 humanoid, 2 quadruped, 3 8-legged, 4 bird,\n\r"
                        "  15 bear (5-14 unused, same as 0)",
                mob->player.bodytype)
            /* bodyparts[] has 16 entries; fight.cpp indexes it with this. */
            if (tmp != tmp3 && (tmp < 0 || tmp > 15)) {
                send_to_char("Body type must be 0-15. dropped.\n\r", ch);
                mob->player.bodytype = tmp3;
            }
            if (IS_SET(SHAPE_PROTO(ch)->flags, SHAPE_CHAIN))
                SHAPE_PROTO(ch)
                    ->editflag
                    = proto_chain[26];
            break;
        case 27:
            DIGITCHANGE("SAVING THROW (more = less spell damage; -N sets negative)", mob->specials2.saving_throw)
            string_to_negative_value(arg, &mob->specials2.saving_throw);
            if (IS_SET(SHAPE_PROTO(ch)->flags, SHAPE_CHAIN))
                SHAPE_PROTO(ch)
                    ->editflag
                    = proto_chain[27];
            break;
        case 28:
            if (!IS_SET(SHAPE_PROTO(ch)->flags, SHAPE_DIGIT_ACTIVE)) {
                sprintf(tmpstr, "Enter STR INT WILL DEX CON LEA (6 numbers, <= 0 becomes 17, blank = keep)\n\r"
                                "Current: %d %d %d %d %d %d\n\r",
                    mob->abilities.str, mob->abilities.intel, mob->abilities.wil,
                    mob->abilities.dex, mob->abilities.con, mob->abilities.lea);
                send_to_char(tmpstr, ch);
                SET_BIT(SHAPE_PROTO(ch)->flags, SHAPE_DIGIT_ACTIVE);
                SHAPE_PROTO(ch)
                    ->position
                    = shape_standup(ch, POSITION_SHAPING);
                ch->specials.prompt_number = 3;
                return;
            } else {
                tmp = mob->abilities.str;
                tmp1 = mob->abilities.intel;
                tmp2 = mob->abilities.wil;
                tmp3 = mob->abilities.dex;
                tmp4 = mob->abilities.con;
                tmp5 = mob->abilities.lea;
                choice = sscanf(arg, "%d %d %d %d %d %d", &tmp, &tmp1, &tmp2, &tmp3, &tmp4, &tmp5);
                if (choice > 0 && choice != 6) {
                    send_to_char("six numbers required. dropped\n\r", ch);
                    REMOVE_BIT(SHAPE_PROTO(ch)->flags, SHAPE_DIGIT_ACTIVE);
                    shape_standup(ch, SHAPE_PROTO(ch)->position);
                    ch->specials.prompt_number = 5;
                    SHAPE_PROTO(ch)
                        ->editflag
                        = 0;
                    return;
                }
            }
            mob->abilities.str = tmp;
            mob->abilities.intel = tmp1;
            mob->abilities.wil = tmp2;
            mob->abilities.dex = tmp3;
            mob->abilities.con = tmp4;
            mob->abilities.lea = tmp5;
            REMOVE_BIT(SHAPE_PROTO(ch)->flags, SHAPE_DIGIT_ACTIVE);
            shape_standup(ch, SHAPE_PROTO(ch)->position);
            ch->specials.prompt_number = 5;
            SHAPE_PROTO(ch)
                ->editflag
                = 0;
            if (IS_SET(SHAPE_PROTO(ch)->flags, SHAPE_CHAIN))
                SHAPE_PROTO(ch)
                    ->editflag
                    = proto_chain[28];
            break;
        case 29:
            DIGITCHANGE("PROGRAM NUMBER: spec-proc # if flag 0 SPEC is set,\n\r  else mudlle vnum (0 = none)", mob->specials.store_prog_number)
            if (IS_SET(SHAPE_PROTO(ch)->flags, SHAPE_CHAIN))
                SHAPE_PROTO(ch)
                    ->editflag
                    = proto_chain[29];
            break;
        case 30:
            DIGITCHANGE("LANGUAGE: 0 common, 1 animal, 2 human, 3 orc", mob->player.language)
            if (mob->player.language > language_number)
                mob->player.language = language_number;

            if (IS_SET(SHAPE_PROTO(ch)->flags, SHAPE_CHAIN))
                SHAPE_PROTO(ch)
                    ->editflag
                    = proto_chain[30];
            break;
        case 31:
            DIGITCHANGE("BUTCHER ITEM object vnum (0 = none)", mob->specials.butcher_item)

            if (IS_SET(SHAPE_PROTO(ch)->flags, SHAPE_CHAIN))
                SHAPE_PROTO(ch)
                    ->editflag
                    = proto_chain[31];
            break;
        case 32:
            DIGITCHANGE("PERCEPTION 0-100 (-1 = race default)", mob->specials2.perception)
            string_to_negative_value(arg, &mob->specials2.perception);

            if (IS_SET(SHAPE_PROTO(ch)->flags, SHAPE_CHAIN))
                SHAPE_PROTO(ch)
                    ->editflag
                    = proto_chain[32];
            break;
        case 33:
            LINECHANGE("DEATH CRY to the room, $n = the mob\n\r  (blank = keep, %q = no message)", mob->player.death_cry)
            if (IS_SET(SHAPE_PROTO(ch)->flags, SHAPE_CHAIN))
                SHAPE_PROTO(ch)
                    ->editflag
                    = proto_chain[33];
            break;
        case 34:
            LINECHANGE("DEATH CRY to adjacent rooms (blank = keep, %q = no message)", mob->player.death_cry2)
            if (IS_SET(SHAPE_PROTO(ch)->flags, SHAPE_CHAIN))
                SHAPE_PROTO(ch)
                    ->editflag
                    = proto_chain[34];
            break;
        case 35:
            DIGITCHANGE("CORPSE object vnum (0 = generic corpse)", mob->player.corpse_num)
            if (IS_SET(SHAPE_PROTO(ch)->flags, SHAPE_CHAIN))
                SHAPE_PROTO(ch)
                    ->editflag
                    = proto_chain[35];
            break;
        case 36:
            if (!IS_SET(SHAPE_PROTO(ch)->flags, SHAPE_DIGIT_ACTIVE))
                send_to_char("  0 UNGROUPED, 1 FIRE, 2 COLD, 3 REGEN, 4 PROT, 5 ANIMALS, 6 STEALTH, 7 WILD,\n\r  8 TELEPORT, 9 ILLUSION, 10 LIGHTNING, 11 MIND\n\r", ch);
            DIGITCHANGE("RESISTANCES: pN sets bit N, mN clears it, one per answer", mob->specials.resistance)

            if (IS_SET(SHAPE_PROTO(ch)->flags, SHAPE_CHAIN))
                SHAPE_PROTO(ch)
                    ->editflag
                    = proto_chain[36];
            break;
        case 37:
            if (!IS_SET(SHAPE_PROTO(ch)->flags, SHAPE_DIGIT_ACTIVE))
                send_to_char("  0 UNGROUPED, 1 FIRE, 2 COLD, 3 REGEN, 4 PROT, 5 ANIMALS, 6 STEALTH, 7 WILD,\n\r  8 TELEPORT, 9 ILLUSION, 10 LIGHTNING, 11 MIND\n\r", ch);
            DIGITCHANGE("VULNERABILITIES: pN sets bit N, mN clears it, one per answer", mob->specials.vulnerability)

            if (IS_SET(SHAPE_PROTO(ch)->flags, SHAPE_CHAIN))
                SHAPE_PROTO(ch)
                    ->editflag
                    = proto_chain[37];
            break;
        case 38:
            DIGITCHANGE("SCRIPT vnum (0 = none)", mob->specials.script_number)

            if (IS_SET(SHAPE_PROTO(ch)->flags, SHAPE_CHAIN))
                SHAPE_PROTO(ch)
                    ->editflag
                    = proto_chain[38];
            break;

        case 39:
            DIGITCHANGE("ROLEPLAY FLAG", mob->specials2.rp_flag)

            if (IS_SET(SHAPE_PROTO(ch)->flags, SHAPE_CHAIN))
                SHAPE_PROTO(ch)
                    ->editflag
                    = proto_chain[39];
            break;
        case 40: {
            DIGITCHANGE("SPIRIT", mob->points.spirit);
            if (IS_SET(SHAPE_PROTO(ch)->flags, SHAPE_CHAIN)) {
                SHAPE_PROTO(ch)
                    ->editflag
                    = proto_chain[40];
            }
        } break;
        case 41: {
            DIGITCHANGE("WILL TEACH: pN per race number (see /19), mN clears", mob->specials2.will_teach);
            if (IS_SET(SHAPE_PROTO(ch)->flags, SHAPE_CHAIN)) {
                SHAPE_PROTO(ch)
                    ->editflag
                    = proto_chain[41];
            }
        } break;
#undef DIGITCHANGE
        case 48:
            if (!IS_SET(SHAPE_PROTO(ch)->flags, SHAPE_DIGIT_ACTIVE)) {
                recalculate_mob(ch);
                send_to_char("Stats were reset from level. Edit them now?\n\r", ch);
            }
            //      tmpptr=(char *)calloc(2,1);
            CREATE(tmpptr, char, 2);
            tmpptr[0] = 'N';
            tmpptr[1] = 0;
            LINECHANGE("choice [Y/N]", tmpptr);
            if ((tmpptr[0] != 'y') && (tmpptr[0] != 'Y')) {
                SHAPE_PROTO(ch)
                    ->editflag
                    = 0;
            } else {
                SHAPE_PROTO(ch)
                    ->editflag
                    = proto_chain[48];
                REMOVE_BIT(SHAPE_PROTO(ch)->flags, SHAPE_SIMPLE_ACTIVE);
                send_to_char("Continuing in extended mode.\n\r", ch);
            }
            RELEASE(tmpptr);
            tmpptr = 0;
            break;
        case 49:
            send_to_char("You invoked mobile creation sequence.\n\r", ch);
            SHAPE_PROTO(ch)
                ->editflag
                = proto_chain[49];
            SET_BIT(SHAPE_PROTO(ch)->flags, SHAPE_CHAIN);
            break;
        case 50:
            if (IS_SET(SHAPE_PROTO(ch)->flags, SHAPE_SIMPLEMODE))
                list_simple_proto(ch, mob);
            else
                list_proto(ch, mob);
            SHAPE_PROTO(ch)
                ->editflag
                = 0;
            if (IS_SET(SHAPE_PROTO(ch)->flags, SHAPE_CHAIN))
                SHAPE_PROTO(ch)
                    ->editflag
                    = proto_chain[50];
            break;
        default:
            if (IS_SET(SHAPE_PROTO(ch)->flags, SHAPE_SIMPLEMODE))
                list_simple_help(ch);
            else
                list_help(ch);
            SHAPE_PROTO(ch)
                ->editflag
                = 0;
            break;
        }
    } while (SHAPE_PROTO(ch)->editflag != 0);
    return;
}
#undef LINECHANGE
void list_simple_help(struct char_data* ch)
{
    send_to_char("Simple mode editing commands:\n\r", ch);
    send_to_char("possible fields are:\n\r1 - alias(es);\n\r", ch);
    send_to_char("2 - reference description;\n\r3 - full description;\n\r", ch);
    send_to_char("4 - detailed description;\n\r5 - flags;\n\r", ch);
    send_to_char("6 - affections;\n\r7 - level;\n\r", ch);
    send_to_char("8 - sex;\n\r9 - race;\n\r", ch);
    send_to_char("10 - body_type;\n\r", ch);
    send_to_char("11 - race aggressions flag;\n\r", ch);
    send_to_char("12 - the item to butcher (0 for none);\n\r", ch);
    send_to_char("13 - mob spirit;\n\r", ch);
    send_to_char("49 - mob creation sequence;\n\r", ch);
    send_to_char("50 - list;\n\r", ch);
    return;
}

void list_help(struct char_data* ch)
{
    send_to_char("Extended mode editing commands:\n\r", ch);
    send_to_char("possible fields are:\n\r1 - alias(es);\n\r", ch);
    send_to_char("2 - reference description;\n\r3 - full description;\n\r", ch);
    send_to_char("4 - detailed description;\n\r5 - flags;\n\r", ch);
    send_to_char("6 - affections;\n\r7 - alignment;\n\r8 - level;\n\r", ch);
    send_to_char("9 - OB, parry, dodge;\n\r", ch);
    send_to_char("10 - min_hit, max_hit;\n\r", ch);
    send_to_char("11 - damage;\n\r", ch);
    send_to_char("12 - energy regen;\n\r", ch);
    send_to_char("13 - gold;\n\r", ch);
    send_to_char("14 - exp;\n\r", ch);
    send_to_char("16 - position;\n\r", ch);
    send_to_char("17 - default position;\n\r", ch);
    send_to_char("18 - sex;\n\r19 - race;\n\r", ch);
    send_to_char("20 - race aggression;\n\r", ch);
    send_to_char("21 - weight;\n\r", ch);
    send_to_char("22 - height;\n\r", ch);
    send_to_char("23 - prof;\n\r", ch);
    send_to_char("24 - stamina;\n\r", ch);
    send_to_char("25 - movepoints;\n\r", ch);
    send_to_char("26 - body_type;\n\r", ch);
    send_to_char("27 - saving throw;\n\r", ch);
    send_to_char("28 - STR INT WILL DEX CON LEA;\n\r", ch);
    send_to_char("29 - program number;\n\r", ch);
    send_to_char("30 - language;\n\r", ch);
    send_to_char("31 - butcher item;\n\r", ch);
    send_to_char("32 - perception;\n\r", ch);
    send_to_char("33 - death cry;\n\r", ch);
    send_to_char("34 - death cry - to other rooms;\n\r", ch);
    send_to_char("35 - corpse number;\n\r", ch);
    send_to_char("36 - resistances;\n\r", ch);
    send_to_char("37 - vulnerabilities;\n\r", ch);
    send_to_char("38 - script number;\n\r", ch);
    send_to_char("39 - roleplay flag;\n\r", ch);
    send_to_char("40 - mob spirit;\n\r", ch);
    send_to_char("41 - will teach;\n\r", ch);
    send_to_char("49 - mob creation sequence;\n\r", ch);
    send_to_char("50 - list;\n\r", ch);
    return;
}
/*********--------------------------------*********/
void list_simple_proto(struct char_data* ch, struct char_data* mob)
{
    char str[255];

    //  recalculate_mob(ch);
    send_to_char("Simple editing mode.\n\r", ch);
    sprintf(str, "(1) alias(es)         :%s\n\r", mob->player.name);
    send_to_char(str, ch);
    sprintf(str, "(2) reference description  :%s\n\r", mob->player.short_descr);
    send_to_char(str, ch);
    sprintf(str, "(3) full  description   :\n\r");
    send_to_char(str, ch);
    send_to_char(mob->player.long_descr, ch);
    send_to_char("\n\r", ch);
    sprintf(str, "(4) detailed description  :\n\r");
    send_to_char(str, ch);
    send_to_char(mob->player.description, ch);
    send_to_char("\n\r", ch);
    sprintf(str, "(5) flag number  :%ld\n\r", mob->specials2.act);
    send_to_char(str, ch);
    sprintf(str, "(6) affections   :%ld\n\r", mob->specials.affected_by);
    send_to_char(str, ch);
    //  sprintf(str,"(7) alignment    :%d\n\r",mob->specials2.alignment);
    //  send_to_char(str,ch);
    sprintf(str, "(7) level        :%d\n\r", mob->player.level);
    send_to_char(str, ch);
    sprintf(str, "(8) sex          :%d\n\r", mob->player.sex);
    send_to_char(str, ch);
    sprintf(str, "(9) race         :%d\n\r", mob->player.race);
    send_to_char(str, ch);
    sprintf(str, "(10) bodytype    :%d\n\r", mob->player.bodytype);
    send_to_char(str, ch);
    sprintf(str, "(11) race aggr.  :%ld\n\r", mob->specials2.pref);
    send_to_char(str, ch);
    sprintf(str, "(12) butcher item:%d\n\r", mob->specials.butcher_item);
    send_to_char(str, ch);
    sprintf(str, "(13) spirit:%d\n\r", mob->points.spirit);
    send_to_char(str, ch);
}

void list_proto(struct char_data* ch, struct char_data* mob)
{
    static char str[MAX_STRING_LENGTH];
    send_to_char("Full editing mode.\n\r", ch);
    sprintf(str, "(1) alias(es)         :%s\n\r", mob->player.name);
    send_to_char(str, ch);
    sprintf(str, "(2) reference description  :%s\n\r", mob->player.short_descr);
    send_to_char(str, ch);
    sprintf(str, "(3) full  description   :\n\r");
    send_to_char(str, ch);
    send_to_char(mob->player.long_descr, ch);
    send_to_char("\n\r", ch);
    sprintf(str, "(4) detailed description  :\n\r");
    send_to_char(str, ch);
    send_to_char(mob->player.description, ch);
    send_to_char("\n\r", ch);
    sprintf(str, "(5) flag number  :%ld\n\r", mob->specials2.act);
    send_to_char(str, ch);
    sprintf(str, "(6) affections   :%ld\n\r", mob->specials.affected_by);
    send_to_char(str, ch);
    sprintf(str, "(7) alignment    :%d\n\r", mob->specials2.alignment);
    send_to_char(str, ch);
    sprintf(str, "(8) level        :%d\n\r", mob->player.level);
    send_to_char(str, ch);
    sprintf(str, "(9) OB:%d,   parry:%d, dodge:%d\n\r", mob->points.OB,
        mob->points.parry, mob->points.dodge);
    send_to_char(str, ch);
    sprintf(str, "(10) min_hit:%d, max_hit:%d\n\r",
        mob->tmpabilities.hit, mob->abilities.hit);
    send_to_char(str, ch);
    sprintf(str, "(11) damage       :%d\n\r", mob->points.damage);
    send_to_char(str, ch);
    sprintf(str, "(12) energy regen :%d\n\r", mob->points.ENE_regen);
    send_to_char(str, ch);
    sprintf(str, "(13) gold         :%d\n\r", mob->points.gold);
    send_to_char(str, ch);
    sprintf(str, "(14) exp          :%d\n\r", mob->points.exp);
    send_to_char(str, ch);
    sprintf(str, "(16) position     :%d\n\r", mob->specials.position);
    send_to_char(str, ch);
    sprintf(str, "(17) default pos. :%d\n\r", mob->specials.default_pos);
    send_to_char(str, ch);
    sprintf(str, "(18) sex          :%d\n\r", mob->player.sex);
    send_to_char(str, ch);
    sprintf(str, "(19) race         :%d\n\r", mob->player.race);
    send_to_char(str, ch);
    sprintf(str, "(20) race aggr.   :%ld\n\r", mob->specials2.pref);
    send_to_char(str, ch);
    sprintf(str, "(21) weight       :%d\n\r", mob->player.weight);
    send_to_char(str, ch);
    sprintf(str, "(22) height       :%d\n\r", mob->player.height);
    send_to_char(str, ch);
    sprintf(str, "(23) prof        :%d\n\r", mob->player.prof);
    send_to_char(str, ch);
    sprintf(str, "(24) stamina      :%d\n\r", mob->abilities.mana);
    send_to_char(str, ch);
    sprintf(str, "(25) movepoints   :%d\n\r", mob->abilities.move);
    send_to_char(str, ch);
    sprintf(str, "(26) bodytype     :%d\n\r", mob->player.bodytype);
    send_to_char(str, ch);
    sprintf(str, "(27) saving throw :%d\n\r", mob->specials2.saving_throw);
    send_to_char(str, ch);
    sprintf(str, "(28) STR=%d INT=%d WILL=%d DEX=%d CON=%d LEA=%d\n\r",
        mob->abilities.str, mob->abilities.intel, mob->abilities.wil,
        mob->abilities.dex, mob->abilities.con, mob->abilities.lea);
    send_to_char(str, ch);
    sprintf(str, "(29) program number:%d\n\r", mob->specials.store_prog_number);
    send_to_char(str, ch);
    sprintf(str, "(30) language (0-%d):%d\n\r", language_number,
        mob->player.language);
    send_to_char(str, ch);
    sprintf(str, "(31) butcher item   :%d\n\r", mob->specials.butcher_item);
    send_to_char(str, ch);
    sprintf(str, "(32) perception     :%d\n\r", mob->specials2.perception);
    send_to_char(str, ch);
    sprintf(str, "(33) death cry_1    :%s\n\r", (mob->player.death_cry) ? mob->player.death_cry : "(None)");
    send_to_char(str, ch);
    sprintf(str, "(34) death cry_2    :%s\n\r", (mob->player.death_cry2) ? mob->player.death_cry2 : "(None)");
    send_to_char(str, ch);
    sprintf(str, "(35) corpse number  :%d\n\r", mob->player.corpse_num);
    send_to_char(str, ch);
    sprintf(str, "(36) resistances    :%d\n\r", mob->specials.resistance);
    send_to_char(str, ch);
    sprintf(str, "(37) vulnerabilities:%d\n\r", mob->specials.vulnerability);
    send_to_char(str, ch);
    sprintf(str, "(38) script number  :%d\n\r", mob->specials.script_number);
    send_to_char(str, ch);
    sprintf(str, "(39) roleplay flag  :%d\n\r", mob->specials2.rp_flag);
    send_to_char(str, ch);
    sprintf(str, "(40) spirit:%d\n\r", mob->points.spirit);
    send_to_char(str, ch);
    sprintf(str, "(41) will teach     :%ld\n\r", mob->specials2.will_teach);
    send_to_char(str, ch);
}

/*********--------------------------------*********/
int find_mob(FILE* f, int n)
{
    int check = 0;
    int i = 0;
    do {
        char c = 0;
        do {
            check = fscanf(f, "%c", &c);
        } while ((c != '#') && (check != EOF));

        fscanf(f, "%d", &i);
    } while ((i < n) && (check != EOF));

    if (check == EOF) {
        return -1;
    }

    return i;
}

int get_text(FILE* f, char** line)
{
    *line = fread_string(f, "shaping");
    if (*line)
        return 0;
    else
        return -1;
}

/****************-------------------------------------****************/
int load_proto(struct char_data* ch, char* arg)
{
    char format;
    // int i;
    int tmp, number, room_number;
    char str[255], fname[80];
    FILE* file;

    if (2 != sscanf(arg, "%s %d", str, &number)) {
        send_to_char("Choose a mobile by 'shape mobile <number>'\n\r", ch);
        return -1;
    }
    sprintf(fname, "%d", number / 100);
    sprintf(str, SHAPE_MOB_DIR, fname);

    send_to_char(str, ch);
    file = fopen(str, "r+");
    if (file == 0) {
        send_to_char(" could not open that file.\n\r", ch);
        return -1;
    }

    strcpy(SHAPE_PROTO(ch)->f_from, str);
    SET_BIT(SHAPE_PROTO(ch)->flags, SHAPE_FILENAME);
    sprintf(SHAPE_PROTO(ch)->f_old, SHAPE_MOB_BACKDIR, fname);

    if (GET_IDNUM(ch) == mobile_master_idnum) {
        SHAPE_PROTO(ch)
            ->permission
            = 1;
    } else {
        SHAPE_PROTO(ch)
            ->permission
            = get_permission(number / 100, ch);
    }
    tmp = find_mob(file, number);
    if (tmp == -1) {
        send_to_char("No such mob or file corrupted.\n\r", ch);
        fclose(file);
        return -1;
    }
    if (tmp > number) {
        new_mob(ch);
        SHAPE_PROTO(ch)
            ->number
            = number;
        sprintf(str, " could not find mob #%d, created it.\n\r", number);
        send_to_char(str, ch);
    } else {
        sprintf(str, " loading mob #%d\n\r", tmp);
        send_to_char(str, ch);
        number = tmp;
        SHAPE_PROTO(ch)
            ->number
            = number;
        room_number = ch->in_room;
        CREATE1(SHAPE_PROTO(ch)->proto, char_data);
        SHAPE_PROTO(ch)
            ->proto->player.time.birth
            = time(0);
        SHAPE_PROTO(ch)
            ->proto->player.time.played
            = 0;
        SHAPE_PROTO(ch)
            ->proto->player.time.logon
            = time(0);
        SHAPE_PROTO(ch)
            ->proto->specials2.act
            = MOB_ISNPC;

        SHAPE_PROTO(ch)
            ->proto->nr
            = real_mobile(number); /*    ????????     to check    */
        get_text(file, &(SHAPE_PROTO(ch)->proto->player.name));
        get_text(file, &(SHAPE_PROTO(ch)->proto->player.short_descr));
        get_text(file, &(SHAPE_PROTO(ch)->proto->player.long_descr));
        get_text(file, &(SHAPE_PROTO(ch)->proto->player.description));
        fscanf(file, "%d ", &tmp);
        SHAPE_PROTO(ch)
            ->proto->specials2.act
            = tmp | MOB_ISNPC;
        fscanf(file, "%d ", &tmp);
        SHAPE_PROTO(ch)
            ->proto->specials.affected_by
            = tmp;
        fscanf(file, "%d ", &tmp);
        SHAPE_PROTO(ch)
            ->proto->specials2.alignment
            = tmp;

        fscanf(file, "%s ", str);
        format = str[0];

        if (format == 'N') {
            get_text(file, &(SHAPE_PROTO(ch)->proto->player.death_cry));
            get_text(file, &(SHAPE_PROTO(ch)->proto->player.death_cry2));
        } else {
            SHAPE_PROTO(ch)
                ->proto->player.death_cry
                = NULL;
            SHAPE_PROTO(ch)
                ->proto->player.death_cry2
                = NULL;
        }
        fscanf(file, "%d", &tmp);
        SHAPE_PROTO(ch)
            ->proto->player.level
            = tmp;
        fscanf(file, "%d", &tmp);
        SHAPE_PROTO(ch)
            ->proto->points.OB
            = tmp;
        fscanf(file, "%d", &tmp);
        SHAPE_PROTO(ch)
            ->proto->points.parry
            = tmp;
        fscanf(file, "%d", &tmp);
        SHAPE_PROTO(ch)
            ->proto->points.dodge
            = tmp;

        fscanf(file, "%d ", &tmp);
        SHAPE_PROTO(ch)
            ->proto->tmpabilities.hit
            = tmp;
        fscanf(file, " %d ", &tmp);
        SHAPE_PROTO(ch)
            ->proto->abilities.hit
            = tmp;

        fscanf(file, "%d ", &tmp);
        SHAPE_PROTO(ch)
            ->proto->points.damage
            = tmp;
        fscanf(file, "%d ", &tmp);
        SHAPE_PROTO(ch)
            ->proto->points.ENE_regen
            = tmp;
        fscanf(file, "%d ", &tmp);
        SHAPE_PROTO(ch)
            ->proto->points.gold
            = tmp;
        fscanf(file, "%d ", &tmp);
        SHAPE_PROTO(ch)
            ->proto->points.exp
            = tmp;
        fscanf(file, "%d ", &tmp); /* some mysterious owner...*/
        fscanf(file, "%d ", &tmp);
        SHAPE_PROTO(ch)
            ->proto->specials.position
            = tmp;
        fscanf(file, "%d ", &tmp);
        SHAPE_PROTO(ch)
            ->proto->specials.default_pos
            = tmp;
        fscanf(file, "%d ", &tmp);
        SHAPE_PROTO(ch)
            ->proto->player.sex
            = tmp;
        fscanf(file, "%d ", &tmp);
        SHAPE_PROTO(ch)
            ->proto->player.race
            = tmp;
        fscanf(file, "%d ", &tmp);
        SHAPE_PROTO(ch)
            ->proto->specials2.pref
            = tmp;

        fscanf(file, "%d ", &tmp);
        SHAPE_PROTO(ch)
            ->proto->player.weight
            = tmp;
        fscanf(file, "%d ", &tmp);
        SHAPE_PROTO(ch)
            ->proto->player.height
            = tmp;
        fscanf(file, "%d ", &tmp);
        SHAPE_PROTO(ch)
            ->proto->specials.store_prog_number
            = tmp;

        fscanf(file, "%d ", &tmp);
        SHAPE_PROTO(ch)
            ->proto->specials.butcher_item
            = tmp;
        fscanf(file, "%d ", &tmp);
        SHAPE_PROTO(ch)
            ->proto->player.corpse_num
            = tmp;
        fscanf(file, "%d ", &tmp);
        SHAPE_PROTO(ch)
            ->proto->specials2.rp_flag
            = tmp;
        if ((format == 'M') || (format == 'N')) {
            fscanf(file, "%d ", &tmp);
            SHAPE_PROTO(ch)
                ->proto->player.prof
                = tmp;
            fscanf(file, "%d ", &tmp);
            SHAPE_PROTO(ch)
                ->proto->abilities.mana
                = tmp;
            fscanf(file, "%d ", &tmp);
            SHAPE_PROTO(ch)
                ->proto->abilities.move
                = tmp;
            fscanf(file, "%d ", &tmp);
            SHAPE_PROTO(ch)
                ->proto->player.bodytype
                = tmp;
            fscanf(file, "%d ", &tmp);
            SHAPE_PROTO(ch)
                ->proto->specials2.saving_throw
                = tmp;

            fscanf(file, "%d ", &tmp);
            SHAPE_PROTO(ch)
                ->proto->tmpabilities.str
                = SHAPE_PROTO(ch)->proto->abilities.str = tmp;
            fscanf(file, "%d ", &tmp);
            SHAPE_PROTO(ch)
                ->proto->tmpabilities.intel
                = SHAPE_PROTO(ch)->proto->abilities.intel = tmp;
            fscanf(file, "%d ", &tmp);
            SHAPE_PROTO(ch)
                ->proto->tmpabilities.wil
                = SHAPE_PROTO(ch)->proto->abilities.wil = tmp;
            fscanf(file, "%d ", &tmp);
            SHAPE_PROTO(ch)
                ->proto->tmpabilities.dex
                = SHAPE_PROTO(ch)->proto->abilities.dex = tmp;
            fscanf(file, "%d ", &tmp);
            SHAPE_PROTO(ch)
                ->proto->tmpabilities.con
                = SHAPE_PROTO(ch)->proto->abilities.con = tmp;
            fscanf(file, "%d ", &tmp);
            SHAPE_PROTO(ch)
                ->proto->tmpabilities.lea
                = SHAPE_PROTO(ch)->proto->abilities.lea = tmp;

            fscanf(file, "%d ", &tmp);
            /* Out-of-range values (old /recalculate saved 121-123) boot as
             * 0 common, so load them as 0 to keep the mob as it plays. */
            if (tmp < 0 || tmp > language_number)
                tmp = 0;
            SHAPE_PROTO(ch)
                ->proto->player.language
                = tmp;
        }

        fscanf(file, "%d ", &tmp);
        SHAPE_PROTO(ch)
            ->proto->specials2.perception
            = tmp;

        fscanf(file, "%d ", &tmp);
        SHAPE_PROTO(ch)
            ->proto->specials.resistance
            = tmp;

        fscanf(file, "%d ", &tmp);
        SHAPE_PROTO(ch)
            ->proto->specials.vulnerability
            = tmp;

        fscanf(file, "%d ", &tmp);
        SHAPE_PROTO(ch)
            ->proto->specials.script_number
            = tmp;

        for (tmp = 0; tmp < 3; tmp++) {
            SHAPE_PROTO(ch)
                ->proto->specials2.conditions[tmp]
                = -1;
        }

        if (fscanf(file, "%d ", &tmp) != EOF) {
            SHAPE_PROTO(ch)
                ->proto->points.spirit
                = tmp;
        }

        if (fscanf(file, "%ld ", &tmp) != EOF) {
            SHAPE_PROTO(ch)
                ->proto->specials2.will_teach
                = tmp;
        }

        if ((format != 'M') && (format != 'N')) {
            send_to_char("Created new mobile or loaded wrong\n\rif you did the new mob and you sure it's correct do /save\n\r", ch);
        }
    }
    SET_BIT(SHAPE_PROTO(ch)->flags, SHAPE_PROTO_LOADED);
    SHAPE_PROTO(ch)
        ->procedure
        = SHAPE_EDIT;
    ch->specials.prompt_value = number;
    send_to_char("causing the eternal order to shiver in apprehension, you load a mobile\n\r", ch);
    fclose(file);
    return number;
}
/*****************----------------------------------******************/
int append_proto(struct char_data* ch, char* arg);

/* copy f1 to f2, replacing mob #num with new mob */
int replace_proto(struct char_data* ch, char* arg)
{
    char str[255];
    char *f_from, *f_old;
    char c;
    int i, check, num, oldnum;
    FILE* f1;
    FILE* f2;

    if (!IS_SET(SHAPE_PROTO(ch)->flags, SHAPE_FILENAME)) {
        send_to_char("ERROR: You have no file defined to write to.\n\r", ch);
        return -1;
    }

    f_from = SHAPE_PROTO(ch)->f_from;
    f_old = SHAPE_PROTO(ch)->f_old;

    if (!IS_SET(SHAPE_PROTO(ch)->flags, SHAPE_PROTO_LOADED)) {
        send_to_char("ERROR: You have no mobile to save.\n\r", ch);
        return -1;
    }
    if (!strcmp(f_from, f_old)) {
        send_to_char("ERROR: Source and target files are the same.\n\r", ch);
        return -1;
    }
    if (SHAPE_PROTO(ch)->permission == 0) {
        send_to_char("ERROR: You're not authorized to do this.\n\r", ch);
        return -1;
    }

    num = SHAPE_PROTO(ch)->number;
    if (SHAPE_PROTO(ch)->number == -1) {
        send_to_char("you created it afresh, remember? Adding it.\n\r", ch);
        append_proto(ch, arg);
        return -1;
    }

    /* Here goes file backuping... */
    f1 = fopen(f_from, "r+");
    if (!f1) {
        send_to_char("ERROR: Could not open source file\n\r", ch);
        return -1;
    }

    f2 = fopen(f_old, "w+");
    if (!f2) {
        send_to_char("ERROR: Could not open backup file\r\n", ch);
        fclose(f1);
        return -1;
    }

    /* Copy f1 into f2 */
    do {
        check = fscanf(f1, "%c", &c);
        fprintf(f2, "%c", c);
    } while (check != EOF);
    fclose(f1);
    fclose(f2);

    f2 = fopen(f_from, "w");
    if (!f2) {
        send_to_char("could not open source file-2\n\r", ch);
        return -1;
    }
    f1 = fopen(f_old, "r");
    if (!f1) {
        send_to_char("could not open backup file-2\n\r", ch);
        fclose(f2);
        return -1;
    }

    do {
        do {
            check = fscanf(f1, "%c", &c);
            if (c != '#')
                fprintf(f2, "%c", c);
        } while (c != '#' && check != EOF);

        if (check == EOF)
            break;

        fscanf(f1, "%d", &i);
        if (i < num)
            fprintf(f2, "#%-d", i);
        else
            oldnum = i;
    } while (i < num && check != EOF);

    if (check == EOF) {
        sprintf(str, "no mob #%d in this file\n\r", num);
        send_to_char(str, ch);
        fclose(f1);
        fclose(f2);
        return -1;
    }
    if (i == num) {
        do {
            i = fscanf(f1, "%c", &c);
        } while (c != '#' && i != EOF);
        if (c == '#')
            fscanf(f1, "%d", &oldnum);
    }

    if (!IS_SET(SHAPE_PROTO(ch)->flags, SHAPE_DELETE_ACTIVE)) {
        write_proto(f2, SHAPE_PROTO(ch)->proto, num);
        REMOVE_BIT(SHAPE_PROTO(ch)->flags, SHAPE_DELETE_ACTIVE);
    }

    fprintf(f2, "#%-d", oldnum);
    for (; i != EOF;) {
        i = fscanf(f1, "%c", &c);
        if (i != EOF)
            fprintf(f2, "%c", c);
    }
    fclose(f1);
    fclose(f2);

    return num;
}

int append_proto(struct char_data* ch, char* arg)
{
    /* copy f1 to f2, appending mob #next-in-file with new mob */
    char str[255], fname[80];
    char* f_from;
    char* f_old;
    char c;
    int i, i1, check;
    FILE* f1;
    FILE* f2;
    /*  if(3!=sscanf(arg,"%s %s %s",str,f_from,f_old)){
    send_to_char("format is <file_from> <file_to>\n\r",ch);
    return -1;
  }*/
    if (SHAPE_PROTO(ch)->permission == 0) {
        send_to_char("You're not authorized to do this.\n\r", ch);
        return -1;
    }
    if (SHAPE_PROTO(ch)->number != -1) {
        send_to_char("Mobile was already added to database. Saving.\n\r", ch);
        replace_proto(ch, arg);
        return -1;
    }
    if (!IS_SET(SHAPE_PROTO(ch)->flags, SHAPE_FILENAME)) {
        send_to_char("No file defined to write into.\n\r", ch);
        return -1;
    }
    if (!IS_SET(SHAPE_PROTO(ch)->flags, SHAPE_PROTO_LOADED)) {
        send_to_char("you have no mobile to save...\n\r", ch);
        return -1;
    }

    f_from = SHAPE_PROTO(ch)->f_from;
    f_old = SHAPE_PROTO(ch)->f_old;

    /* Here goes file backuping... */
    f1 = fopen(f_from, "r+");
    if (!f1) {
        send_to_char("could not open source file\n\r", ch);
        return -1;
    }
    f2 = fopen(f_old, "w+");
    if (!f2) {
        send_to_char("could not open backup file\n\r", ch);
        fclose(f1);
        return -1;
    }
    do {
        check = fscanf(f1, "%c", &c);
        fprintf(f2, "%c", c);
    } while (check != EOF);
    fclose(f1);
    fclose(f2);

    if (!strcmp(f_from, f_old)) {
        send_to_char("better make source and target files different\n\r", ch);
        return -1;
    }

    f1 = fopen(f_old, "r+");
    if (!f1) {
        send_to_char("could not open backup file\n\r", ch);
        return -1;
    }
    f2 = fopen(f_from, "w+");
    if (!f2) {
        send_to_char("could not open target file\n\r", ch);
        fclose(f1);
        return -1;
    }
    if (!IS_SET(SHAPE_PROTO(ch)->flags, SHAPE_FILENAME))
        i = atoi(fname) * 100 + 1;
    else {
        for (c = 0; (f_from[c] < '0' || f_from[c] > '9') && f_from[c]; c++)
            ;
        sscanf(f_from + c, "%d", &i);
        i = i * 100;
    }
    do {

        do {
            check = fscanf(f1, "%c", &c);
            fprintf(f2, "%c", c);
        } while ((c != '#') && (check != EOF));
        i1 = i;
        fscanf(f1, "%d", &i);
        if (i != 99999)
            fprintf(f2, "%-d\n\r", i);
    } while ((i != 99999) && (check != EOF));

    if (check == EOF) {
        send_to_char("no final record  in source file\n\r", ch);
    }

    fseek(f2, -1, SEEK_CUR);
    write_proto(f2, SHAPE_PROTO(ch)->proto, i1 + 1);
    sprintf(str, "Mobile added to database as #%d.\n\r", i1 + 1);
    send_to_char(str, ch);
    SHAPE_PROTO(ch)
        ->number
        = i1 + 1;
    ch->specials.prompt_value = i1 + 1;
    fprintf(f2, "#99999\n\r");
    fclose(f1);
    fclose(f2);
    return i1;
}

/*void animate_mob(struct char_data * ch){
  struct char_data * mob;
  int room_number=1;
  room_number=ch->in_room;
   CREATE(mob, struct char_data, 1);

   mob->tmpabilities.hit = mob->abilities.hit;
   mob->tmpabilities.mana = mob->abilities.mana;
   mob->tmpabilities.move = mob->abilities.move;

   mob->player.time.birth = time(0);
   mob->player.time.played = 0;
   mob->player.time.logon  = time(0);
   mob->next = character_list;
   character_list = mob;
   mob->in_room=room_number;
   mob->next_in_room=world[room_number].people;
   world[room_number].people=mob;
}
*/

void implement_proto(struct char_data* ch)
{
    int number;
    struct char_data* proto;
    char *t1, *t2, *t3, *t4;
    if (!IS_SET(SHAPE_PROTO(ch)->flags, SHAPE_PROTO_LOADED)) {
        send_to_char("You have no mob to implement.\n\r", ch);
        return;
    }
    if (SHAPE_PROTO(ch)->proto->nr == -1) {
        send_to_char("You can't implement fresh created mob. Wait for reboot.\n\r",
            ch);
        return;
    }
    number = SHAPE_PROTO(ch)->proto->nr;
    if (number == -1) {
        send_to_char("Weird mobile number... Aborted.\n\r", ch);
        return;
    }
    if (SHAPE_PROTO(ch)->permission == 0) {
        send_to_char("You're not authorized to do this.\n\r", ch);
        return;
    }

    proto = mob_proto + number;
    /* If the spec proc slot holds what the old /29 gave, implement put it
     * there: free it so the new flag and /29 can take its place.  A slot
     * filled at boot by vnum (guild, receptionist, shopkeeper...) is kept. */
    if (IS_SET(proto->specials2.act, MOB_SPEC) && mob_index[number].func
        && (void*)mob_index[number].func == virt_program_number(proto->specials.store_prog_number))
        mob_index[number].func = 0;
    memcpy(proto, SHAPE_PROTO(ch)->proto, sizeof(struct char_data));
    /*    if(proto->player.name) RELEASE(proto->player.name);
  if(proto->player.short_descr) RELEASE(proto->player.short_descr);
  if(proto->player.long_descr) RELEASE(proto->player.long_descr);
  if(proto->player.description) RELEASE(proto->player.description);
  */
    CREATE(proto->player.name, char,
        strlen(SHAPE_PROTO(ch)->proto->player.name) + 1);
    t1 = proto->player.name;
    CREATE(proto->player.short_descr, char,
        strlen(SHAPE_PROTO(ch)->proto->player.short_descr) + 1);
    t2 = proto->player.short_descr;
    CREATE(proto->player.long_descr, char,
        strlen(SHAPE_PROTO(ch)->proto->player.long_descr) + 1);
    t3 = proto->player.long_descr;
    CREATE(proto->player.description, char,
        strlen(SHAPE_PROTO(ch)->proto->player.description) + 1);
    t4 = proto->player.description;

    strcpy(proto->player.name, SHAPE_PROTO(ch)->proto->player.name);
    strcpy(proto->player.short_descr, SHAPE_PROTO(ch)->proto->player.short_descr);
    strcpy(proto->player.long_descr, SHAPE_PROTO(ch)->proto->player.long_descr);
    strcpy(proto->player.description, SHAPE_PROTO(ch)->proto->player.description);
    /*   printf("desc:%s.\b",proto->player.description); */

    if (SHAPE_PROTO(ch)->proto->player.language > 0
        && SHAPE_PROTO(ch)->proto->player.language <= language_number)
        proto->player.language = language_skills[SHAPE_PROTO(ch)->proto->player.language - 1];
    else
        proto->player.language = 0;
    if (!IS_SET(proto->specials2.act, MOB_SPEC))
        proto->specials.store_prog_number = real_program(proto->specials.store_prog_number);
    else
        virt_assignmob(mob_proto + number);
}
ACMD(do_shape)
{
    sh_int key, stlen, newflag;
    char str[50], tmp[50], tmp2[50];
    int room_number, i;
    room_number = ch->in_room;

    key = 0;
    for (stlen = 0; argument[stlen] <= ' '; stlen++)
        ;
    if (1 != sscanf(argument, "%s", str))
        key = 0;
    else {
        stlen += i = strlen(str);
        if (!strncmp("mobile", str, i))
            key = SHAPE_PROTOS;
        else if (!strncmp("object", str, i))
            key = SHAPE_OBJECTS;
        else if (!strncmp("room", str, i))
            key = SHAPE_ROOMS;
        else if (!strncmp("zone", str, i))
            key = SHAPE_ZONES;
        else if (!strncmp("program", str, i))
            key = SHAPE_MUDLLES;
        else if (!strncmp("script", str, i))
            key = SHAPE_SCRIPTS;
        else if (!strcmp("recalc_mobile", str)
            && (GET_LEVEL(ch) == LEVEL_IMPL))
            key = SHAPE_RECALC_ALL;
        else if (!strcmp("master_mobile", str)
            && (GET_LEVEL(ch) >= LEVEL_GRGOD))
            key = SHAPE_MASTER_MOBILE;
        else if (!strcmp("master_object", str)
            && (GET_LEVEL(ch) >= LEVEL_GRGOD))
            key = SHAPE_MASTER_OBJECT;

        if (key == 0) {
            send_to_char("Unrecognized parameter. Choose 'object', 'mob', 'room', 'zone' or script\n\r", ch);
            return;
        }
        if ((GET_LEVEL(ch) < LEVEL_GOD) && (key != SHAPE_ROOMS)) {
            send_to_char("You are permitted to shape rooms only.\n\r", ch);
            return;
        }
    }
    if (!key && !ch->temp) {
        send_to_char("You are not shaping anything. Check the manual.\n\r", ch);
        return;
    }
    if (ch->temp) {
        send_to_char("You are already shaping something. Free it first.\n\r", ch);
        return;
    }
    ch->specials.prompt_value = -1;
    if (key && !ch->temp) {
        tmp[0] = 0;
        sscanf(argument + stlen, "%s", tmp);
        if (strlen(tmp) && !strncmp("new", tmp, strlen(tmp))) {
            for (stlen = 0; argument[stlen] <= ' '; stlen++)
                ;
            for (; argument[stlen] > ' '; stlen++)
                ; /*room*/
            for (; argument[stlen] <= ' '; stlen++)
                ;
            for (; argument[stlen] > ' '; stlen++)
                ; /*new*/
            sprintf(str, "new %s\n\r", argument + stlen);
            newflag = 1;
        } else {
            sprintf(str, "load %s\n\r", argument + stlen);
            newflag = 0;
        }
        if ((key == SHAPE_PROTOS || key == SHAPE_OBJECTS || key == SHAPE_ROOMS || key == SHAPE_SCRIPTS) && newflag) {
            /* Picked last vnum + 1, which could run past the zone's range. */
            shape_disabled(ch, "shape ", argument);
            return;
        }
        switch (key) {
        case SHAPE_PROTOS:
            //      ch->temp=(struct shape_proto *)calloc(1,sizeof(struct shape_proto));
            CREATE1(ch->temp, shape_proto);
            SHAPE_PROTO(ch)
                ->act
                = SHAPE_PROTOS;
            SHAPE_PROTO(ch)
                ->flags
                = 0;
            SHAPE_PROTO(ch)
                ->editflag
                = 0;
            //      SHAPE_PROTO(ch)->tmpstr=(char *)calloc(1,1);
            CREATE(SHAPE_PROTO(ch)->tmpstr, char, 1);
            SHAPE_PROTO(ch)
                ->procedure
                = SHAPE_EDIT;
            SHAPE_PROTO(ch)
                ->position
                = ch->specials.position;
            SHAPE_PROTO(ch)
                ->number
                = -1;
            SET_BIT(SHAPE_PROTO(ch)->flags, SHAPE_SIMPLEMODE);
            SET_BIT(PRF_FLAGS(ch), PRF_DISPTEXT);
            ch->specials.prompt_number = 5;
            send_to_char("You start shaping a mobile.\n\r", ch);
            shape_center_proto(ch, str);
            break;
        case SHAPE_OBJECTS:
            //      ch->temp=(struct shape_object *)calloc(1,sizeof(struct shape_object));
            CREATE1(ch->temp, shape_object);
            SHAPE_OBJECT(ch)
                ->act
                = SHAPE_OBJECTS;
            SHAPE_OBJECT(ch)
                ->flags
                = 0;
            SHAPE_OBJECT(ch)
                ->editflag
                = 0;
            //      SHAPE_OBJECT(ch)->tmpstr=(char *)calloc(1,1);
            CREATE(SHAPE_OBJECT(ch)->tmpstr, char, 1);
            SHAPE_OBJECT(ch)
                ->procedure
                = SHAPE_EDIT;
            SHAPE_OBJECT(ch)
                ->position
                = ch->specials.position;
            SHAPE_OBJECT(ch)
                ->number
                = -1;
            SET_BIT(PRF_FLAGS(ch), PRF_DISPTEXT);
            ch->specials.prompt_number = 6;
            send_to_char("You start shaping an object.\n\r", ch);
            shape_center_obj(ch, str);
            break;
        case SHAPE_ROOMS:
            //      ch->temp=(struct shape_room *)calloc(1,sizeof(struct shape_room));
            CREATE1(ch->temp, shape_room);
            SHAPE_ROOM(ch)
                ->act
                = SHAPE_ROOMS;
            SHAPE_ROOM(ch)
                ->flags
                = 0;
            SHAPE_ROOM(ch)
                ->editflag
                = 0;
            //      SHAPE_ROOM(ch)->tmpstr=(char *)calloc(1,1);
            CREATE(SHAPE_ROOM(ch)->tmpstr, char, 1);
            SHAPE_ROOM(ch)
                ->procedure
                = SHAPE_EDIT;
            SET_BIT(PRF_FLAGS(ch), PRF_DISPTEXT);
            ch->specials.prompt_number = 4;
            SHAPE_ROOM(ch)
                ->position
                = ch->specials.position;
            send_to_char("You start shaping a room.\n\r", ch);
            if (2 == sscanf(argument, "%s %s", tmp, tmp2))
                if (!strncmp(tmp2, "current", strlen(tmp2)))
                    sprintf(str, "load %d", world[ch->in_room].number);
            shape_center_room(ch, str);
            break;
        case SHAPE_ZONES:
            //      ch->temp=(struct shape_zone *)calloc(1,sizeof(struct shape_zone));
            CREATE1(ch->temp, shape_zone);
            SHAPE_ZONE(ch)
                ->act
                = SHAPE_ZONES;
            SHAPE_ZONE(ch)
                ->flags
                = 0;
            //      SHAPE_ZONE(ch)->tmpstr=(char *)calloc(1,1);
            CREATE(SHAPE_ZONE(ch)->tmpstr, char, 1);
            SHAPE_ZONE(ch)
                ->mask.command
                = '*';
            SHAPE_ZONE(ch)
                ->mask.if_flag
                = -1;
            SHAPE_ZONE(ch)
                ->mask.arg1
                = -1;
            SHAPE_ZONE(ch)
                ->mask.arg2
                = -1;
            SHAPE_ZONE(ch)
                ->mask.arg3
                = -1;
            SHAPE_ZONE(ch)
                ->mask.arg4
                = -1;
            SHAPE_ZONE(ch)
                ->mask.arg5
                = -1;
            SHAPE_ZONE(ch)
                ->mask.arg6
                = -1;
            SHAPE_ZONE(ch)
                ->mask.arg7
                = -1;
            SHAPE_ZONE(ch)
                ->cur_room
                = 0;
            SHAPE_ZONE(ch)
                ->procedure
                = SHAPE_EDIT;
            SET_BIT(PRF_FLAGS(ch), PRF_DISPTEXT);
            ch->specials.prompt_number = 7;
            send_to_char("You start shaping a zone.\n\r", ch);
            SHAPE_ROOM(ch)
                ->position
                = ch->specials.position;
            if (2 == sscanf(argument, "%s %s", tmp, tmp2))
                if (!strncmp(tmp2, "current", strlen(tmp2)))
                    sprintf(str, "load %d", world[ch->in_room].number / 100);
            shape_center_zone(ch, str);
            break;
        case SHAPE_SCRIPTS:
            CREATE1(ch->temp, shape_script);
            SHAPE_SCRIPT(ch)
                ->act
                = SHAPE_SCRIPTS;
            SHAPE_SCRIPT(ch)
                ->flags
                = 0;
            SHAPE_SCRIPT(ch)
                ->procedure
                = SHAPE_EDIT;
            SET_BIT(PRF_FLAGS(ch), PRF_DISPTEXT);
            ch->specials.prompt_number = 9;
            send_to_char("You start shaping a script.\n\r", ch);
            SHAPE_SCRIPT(ch)
                ->position
                = ch->specials.position;
            SHAPE_SCRIPT(ch)
                ->editflag
                = 0;
            shape_center_script(ch, str);
            break;
        case SHAPE_MUDLLES:
            if (2 != sscanf(argument, "%s %s", tmp, tmp2)) {
                send_to_char("format is 'shape program <number>'.\n\r", ch);
                break;
            }
            CREATE1(ch->temp, shape_mudlle);
            SHAPE_MUDLLE(ch)
                ->act
                = SHAPE_MUDLLES;
            SHAPE_MUDLLE(ch)
                ->prog_num
                = -1;
            SET_BIT(PRF_FLAGS(ch), PRF_DISPTEXT);
            ch->specials.prompt_number = 8;
            sprintf(str, "load %s", tmp2);
            shape_center_mudlle(ch, str);

            break;
        case SHAPE_RECALC_ALL:
            /* Rewrote every mob file from level and shut down, no confirmation. */
            shape_disabled(ch, "shape ", argument);
            break;
            printf("going to recalc all\n");
            for (i = 0; i <= top_of_mobt; i++) {
                sprintf(str, "mobile %d", mob_index[i].virt);
                do_shape(ch, str, 0, 0, 0);
                if (!IS_SET(SHAPE_PROTO(ch)->proto->specials2.act, MOB_NORECALC)) {
                    recalculate_mob(ch);
                    replace_proto(ch, "");
                    sprintf(str, "Recalculated [%5d] %s;\n\r",
                        mob_index[i].virt, GET_NAME(mob_proto + i));
                }
                free_proto(ch);
                send_to_char(str, ch);
            }
            do_shutdown(ch, "", 0, 0, SCMD_SHUTDOWN);
            break;

        case SHAPE_MASTER_MOBILE:
            if (1 != sscanf(argument + stlen, "%d", &i)) {
                sprintf(str, "Mobile master is player #%d.\n\r", mobile_master_idnum);
                send_to_char(str, ch);
                send_to_char("Usage: shape master_mobile <idnum>\n\r", ch);
                break;
            }
            sprintf(str, "Mobile permission set to player #%d\n\r", i);
            mobile_master_idnum = i;
            send_to_char(str, ch);
            break;

        case SHAPE_MASTER_OBJECT:
            if (1 != sscanf(argument + stlen, "%d", &i)) {
                sprintf(str, "Object master is player #%d.\n\r", object_master_idnum);
                send_to_char(str, ch);
                send_to_char("Usage: shape master_object <idnum>\n\r", ch);
                break;
            }
            sprintf(str, "Object permission set to player #%d\n\r", i);
            object_master_idnum = i;
            send_to_char(str, ch);
            break;
        };
    }
}
void free_proto(struct char_data* ch)
{
    if (!SHAPE_PROTO(ch))
        return;
    if (IS_SET(SHAPE_PROTO(ch)->flags, SHAPE_PROTO_LOADED)) {
        RELEASE(SHAPE_PROTO(ch)->proto->player.name);
        RELEASE(SHAPE_PROTO(ch)->proto->player.short_descr);
        RELEASE(SHAPE_PROTO(ch)->proto->player.long_descr);
        RELEASE(SHAPE_PROTO(ch)->proto->player.description);
        RELEASE(SHAPE_PROTO(ch)->proto);
        SHAPE_PROTO(ch)
            ->proto
            = 0;
        REMOVE_BIT(SHAPE_PROTO(ch)->flags, SHAPE_PROTO_LOADED);
        ch->specials.prompt_value = 0;
    }
    ch->specials.position = SHAPE_PROTO(ch)->position;
    // RELEASE(SHAPE_PROTO(ch));
    REMOVE_BIT(PRF_FLAGS(ch), PRF_DISPTEXT);
    RELEASE(ch->temp);
    if (GET_POS(ch) <= POSITION_SHAPING)
        GET_POS(ch) = POSITION_STANDING;
    // printf("passed free_mob\n");
}

/****************************** main() ********************************/
void extra_coms_proto(struct char_data* ch, char* argument)
{

    //  extern struct room_data world;
    int comm_key, room_number, zonnum;
    char str[255], str2[50];

    room_number = ch->in_room;

    if (SHAPE_PROTO(ch)->procedure == SHAPE_EDIT) {

        send_to_char("you invoked some rhymes from shapeless indefinity...\n\r", ch);
        comm_key = SHAPE_NONE;
        str[0] = 0;
        str2[0] = 0;
        sscanf(argument, "%s %s", str, str2);
        if (str[0] == 0)
            return;
        do {
            if (!strlen(str))
                strcpy(str, "weird");
            if (!strncmp(str, "free", strlen(str))) {
                comm_key = SHAPE_FREE;
                break;
            }
            if (!strncmp(str, "new", strlen(str))) {
                comm_key = SHAPE_CREATE;
                break;
            }
            if (!strncmp(str, "load", strlen(str))) {
                comm_key = SHAPE_LOAD;
                break;
            }
            if (!strncmp(str, "save", strlen(str))) {
                comm_key = SHAPE_SAVE;
                break;
            }
            if (!strncmp(str, "add", strlen(str))) {
                comm_key = SHAPE_ADD;
                break;
            }
            if (!strncmp(str, "done", strlen(str))) {
                comm_key = SHAPE_DONE;
                break;
            }
            if (!strncmp(str, "delete", strlen(str))) {
                comm_key = SHAPE_DELETE;
                break;
            }
            if (!strncmp(str, "simple", strlen(str))) {
                comm_key = SHAPE_MODE;
                break;
            }
            if (!strncmp(str, "implement", strlen(str))) {
                comm_key = SHAPE_IMPLEMENT;
                break;
            }
            if (strlen(str) >= 6 && !strncmp(str, "recalculate", strlen(str))) {
                comm_key = SHAPE_RECALCULATE;
                break;
            }
            send_to_char("Possible commands are:\n\r", ch);
            send_to_char("save - to save changes to the disk database;\n\r", ch);
            send_to_char("implement - applies changes to the game, leaving disk intact;\n\r", ch);
            send_to_char("simple - to switch between simple and extended editing;\n\r", ch);
            send_to_char("recalc - to generate all mobile parameters from its level;\n\r", ch);
            send_to_char("done - to save your job, implement it and stop shaping.;\n\r", ch);
            send_to_char("free - to stop shaping.;\n\r", ch);

            return;
        } while (0);
    } else
        comm_key = SHAPE_PROTO(ch)->procedure;
    switch (comm_key) {
    case SHAPE_FREE:
        free_proto(ch);
        send_to_char("You released the mobile and stopped shaping.\n\r", ch);
        break;
    case SHAPE_CREATE:
        /* Picked last vnum + 1, which could run past the zone's range. */
        shape_disabled(ch, "/", argument);
        break;
        if (str2[0] == 0) {
            send_to_char("Choose zone of mob by 'new <zone_number>'.\n\r", ch);
            free_proto(ch);
            break;
        }
        zonnum = atoi(str2);
        if (zonnum <= 0 || zonnum >= MAX_ZONES) {
            send_to_char("Weird zone number. Aborted.\n\r", ch);
            free_proto(ch);
            break;
        }
        SHAPE_PROTO(ch)
            ->permission
            = get_permission(zonnum, ch);
        sprintf(SHAPE_PROTO(ch)->f_from, SHAPE_MOB_DIR, str2);
        sprintf(SHAPE_PROTO(ch)->f_old, SHAPE_MOB_BACKDIR, str2);
        SET_BIT(SHAPE_PROTO(ch)->flags, SHAPE_FILENAME);
        new_mob(ch);
        SET_BIT(SHAPE_PROTO(ch)->flags, SHAPE_PROTO_LOADED);
        SHAPE_PROTO(ch)
            ->procedure
            = SHAPE_EDIT;
        SHAPE_PROTO(ch)
            ->editflag
            = 49;
        send_to_char("OK. You created a new mobile. Do '/save' to assign a number to your mobile\n\r",
            ch);
        shape_center_proto(ch, "");
        break;

    case SHAPE_LOAD:
        if (!IS_SET(SHAPE_PROTO(ch)->flags, SHAPE_PROTO_LOADED)) {
            if (load_proto(ch, argument) < 0) {
                free_proto(ch);
            }
        } else
            send_to_char("you already have someone to care about\n\r", ch);
        break;
    case SHAPE_SAVE:
        replace_proto(ch, argument);
        break;
    case SHAPE_ADD:
        /* 'add <file>' could write the mob into any file, outside its zone. */
        if (str2[0]) {
            shape_disabled(ch, "/", argument);
            break;
        }
        append_proto(ch, argument);
        break;
    case SHAPE_RECALCULATE:
        recalculate_mob(ch);
        SHAPE_PROTO(ch)
            ->procedure
            = SHAPE_EDIT;
        send_to_char("You set parameters of your mobile to standard.\n\r", ch);
        break;
    case SHAPE_DELETE:
        /* Rewrote the zone file to drop the mob; a never-saved mob got
         * saved instead. */
        if (SHAPE_PROTO(ch)->procedure != SHAPE_DELETE) {
            shape_disabled(ch, "/", argument);
            break;
        }
        if (SHAPE_PROTO(ch)->procedure != SHAPE_DELETE) {
            send_to_char("You are about to remove this mobile from database.\n\r Are you sure? (type 'yes' to confirm):\n\r", ch);
            SHAPE_PROTO(ch)
                ->procedure
                = SHAPE_DELETE;
            SHAPE_PROTO(ch)
                ->position
                = ch->specials.position;
            ch->specials.position = POSITION_SHAPING;
            break;
        }
        while (*argument && (*argument <= ' '))
            argument++;
        if (!strcmp("yes", argument)) {
            SET_BIT(SHAPE_PROTO(ch)->flags, SHAPE_DELETE_ACTIVE);
            replace_proto(ch, argument);
            send_to_char("You still continue to shape it, /free to exit.\n\r", ch);
        } else
            send_to_char("Deletion cancelled.\n\r", ch);
        REMOVE_BIT(SHAPE_PROTO(ch)->flags, SHAPE_DELETE_ACTIVE);
        SHAPE_PROTO(ch)
            ->procedure
            = SHAPE_EDIT;
        ch->specials.position = SHAPE_PROTO(ch)->position;
        break;
    case SHAPE_MODE:
        if (IS_SET(SHAPE_PROTO(ch)->flags, SHAPE_SIMPLEMODE)) {
            REMOVE_BIT(SHAPE_PROTO(ch)->flags, SHAPE_SIMPLEMODE);
            send_to_char("You switched to extended editing mode.\n\r", ch);
        } else {
            SET_BIT(SHAPE_PROTO(ch)->flags, SHAPE_SIMPLEMODE);
            //      recalculate_mob(ch);
            send_to_char("You switched to simple editing mode.\n\r", ch);
        }
        SHAPE_PROTO(ch)
            ->procedure
            = SHAPE_EDIT;
        break;
    case SHAPE_IMPLEMENT:
        implement_proto(ch);
        SHAPE_PROTO(ch)
            ->procedure
            = SHAPE_EDIT;
        break;
    case SHAPE_DONE:
        /* A failed save must not throw the edits away. */
        if (replace_proto(ch, argument) < 0) {
            send_to_char("Not saved - still shaping. Fix the problem and /done again,\n\r"
                         "or /free to discard.\n\r",
                ch);
            break;
        }
        implement_proto(ch);
        extra_coms_proto(ch, "free");
        break;
    }
    //  printf("passed shape_proto_center\n");
    return;
}

/*************************** Dispatch here :) *******************************/
void shape_center(struct char_data* ch, char* argument)
{

    if (ch->temp)
        switch (*(sh_int*)ch->temp) {
        case SHAPE_PROTOS:
            shape_center_proto(ch, argument);
            break;
        case SHAPE_OBJECTS:
            shape_center_obj(ch, argument);
            break;
        case SHAPE_ROOMS:
            shape_center_room(ch, argument);
            break;
        case SHAPE_ZONES:
            shape_center_zone(ch, argument);
            break;
        case SHAPE_MUDLLES:
            shape_center_mudlle(ch, argument);
            break;
        case SHAPE_SCRIPTS:
            shape_center_script(ch, argument);
            break;
        }
    else {
        send_to_char("Use 'shape <mob|obj|room|script> <number>' to start shaping.\n\r", ch);
    }
}
