#include "platdef.h"
#include <ctype.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>
#include <vector>

#include "comm.h"
#include "db.h"
#include "interpre.h"
#include "object_skins.h"
#include "protos.h"
#include "structs.h"
#include "utils.h"

extern struct obj_data* obj_proto;
extern struct obj_data* object_list;
extern struct index_data* obj_index;
extern int object_master_idnum;
extern int object_master2_idnum;
extern char* object_materials[];
extern int num_of_object_materials;

int obj_chain[50] = {
    0, 2, 3, 4, 9, 6, 7, 0, 0, 11,
    0, 12, 13, 14, 15, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 1
};
int simple_edit(struct char_data* ch, char** str, char* arg); /* exist in objects.c */
int shape_standup(struct char_data* ch, int pos);
void shape_flag_value(const struct char_data* ch, char* arg, int* value);
int shape_help_asked(const char* arg);
void shape_prompt_help(struct char_data* ch, const char* chapter, const char* keyword, int position);
void shape_prompt_hint(const struct char_data* ch);

void new_obj(const struct char_data* ch)
{
    int i;
    //    SHAPE_OBJECT(ch)->object=(struct obj_data *)calloc(1,sizeof(struct obj_data));
    CREATE1(SHAPE_OBJECT(ch)->object, obj_data);
    //    bzero((char *)(SHAPE_OBJECT(ch)->object),sizeof(struct obj_data));
    SHAPE_OBJECT(ch)
        ->object->item_number
        = -1;
    SHAPE_OBJECT(ch)
        ->basenum
        = -1;
    //    SHAPE_OBJECT(ch)->object->name=(char *)calloc(6,1);
    //    strcpy(SHAPE_OBJECT(ch)->object->name,"gizmo");
    SHAPE_OBJECT(ch)
        ->object->name
        = str_dup("gizmo");
    //    SHAPE_OBJECT(ch)->object->short_description=(char *)calloc(6,1);
    //    strcpy(SHAPE_OBJECT(ch)->object->short_description,"gizmo");
    SHAPE_OBJECT(ch)
        ->object->short_description
        = str_dup("gizmo");
    //    SHAPE_OBJECT(ch)->object->description=(char *)calloc(8,1);
    //    strcpy(SHAPE_OBJECT(ch)->object->description,"gizmo");
    SHAPE_OBJECT(ch)
        ->object->description
        = str_dup("gizmo");
    //    SHAPE_OBJECT(ch)->object->action_description=(char *)calloc(6,1);
    //    strcpy(SHAPE_OBJECT(ch)->object->action_description,"gizmo");
    SHAPE_OBJECT(ch)
        ->object->action_description
        = str_dup("");
    SHAPE_OBJECT(ch)
        ->object->obj_flags.type_flag
        = ITEM_TRASH;
    SHAPE_OBJECT(ch)
        ->object->obj_flags.extra_flags
        = 0;
    SHAPE_OBJECT(ch)
        ->object->obj_flags.wear_flags
        = ITEM_TAKE;
    for (i = 0; i < 5; i++)
        SHAPE_OBJECT(ch)
            ->object->obj_flags.value[i]
            = 0;
    SHAPE_OBJECT(ch)
        ->object->obj_flags.weight
        = 0;
    SHAPE_OBJECT(ch)
        ->object->obj_flags.cost
        = 0;
    SHAPE_OBJECT(ch)
        ->object->obj_flags.cost_per_day
        = 0;
    SHAPE_OBJECT(ch)
        ->object->ex_description
        = 0;
    for (i = 0; i < MAX_OBJ_AFFECT; i++) {
        SHAPE_OBJECT(ch)
            ->object->affected[i]
            .location
            = APPLY_NONE;
        SHAPE_OBJECT(ch)
            ->object->affected[i]
            .modifier
            = 0;
    }

    SHAPE_OBJECT(ch)
        ->object->carried_by
        = 0;
    SHAPE_OBJECT(ch)
        ->object->in_obj
        = 0;
    SHAPE_OBJECT(ch)
        ->object->contains
        = 0;
    SHAPE_OBJECT(ch)
        ->object->in_room
        = ch->in_room;
}

void list_object(const struct char_data* ch, const struct obj_data* obj); /* forward declaration */

void list_help_obj(const struct char_data* ch);

void write_object(FILE* f, const struct obj_data* obj, int num)
{

    struct extra_descr_data* tmpdesc;

    int j;

    clean_record_text(obj->name);
    clean_record_text(obj->short_description);
    clean_record_text(obj->description);
    clean_record_text(obj->action_description);
    for (tmpdesc = obj->ex_description; tmpdesc; tmpdesc = tmpdesc->next) {
        clean_record_text(tmpdesc->keyword);
        clean_record_text(tmpdesc->description);
    }

    fprintf(f, "#%-d\n\r", num);

    fprintf(f, "%s~\n\r", obj->name);

    fprintf(f, "%s~\n\r", obj->short_description);

    fprintf(f, "%s~\n\r", obj->description);

    fprintf(f, "%s~\n\r", obj->action_description);

    fprintf(f, "%d %d %d\n\r", obj->obj_flags.type_flag, obj->obj_flags.extra_flags,

        obj->obj_flags.wear_flags);

    /*  fprintf(f,"%d %d %d %d %d\n\r",obj->obj_flags.value[0],

          obj->obj_flags.value[1],obj->obj_flags.value[2],

          obj->obj_flags.value[3],obj->obj_flags.value[4]);

*/
    fprintf(f, "%d %d %d %d %d\n\r", obj->obj_flags.value[0],

        obj->obj_flags.value[1], obj->obj_flags.value[2],

        obj->obj_flags.value[3], obj->obj_flags.value[4]);

    fprintf(f, "%d %d %d\n\r", obj->obj_flags.weight, obj->obj_flags.cost,

        obj->obj_flags.cost_per_day);

    fprintf(f, "%d %d %d %d %d", obj->obj_flags.level,
        obj->obj_flags.rarity, obj->obj_flags.material, obj->obj_flags.script_number, obj->obj_flags.version);
    if (obj->obj_flags.skin_base > 0)
        fprintf(f, " %d %d", obj->obj_flags.skin_base, obj->obj_flags.skin_own);
    fprintf(f, "\n\r");

    for (tmpdesc = obj->ex_description; tmpdesc; tmpdesc = tmpdesc->next) {

        fprintf(f, "E %s~\n %s~\n\r", tmpdesc->keyword, tmpdesc->description);
    }

    /* Skip empty slots rather than stop at one: an empty first slot used to
     * drop the second from the file. */
    for (j = 0; j < MAX_OBJ_AFFECT; j++) {
        if (obj->affected[j].location == APPLY_NONE)
            continue;
        fprintf(f, "A %d %d\n\r", obj->affected[j].location, obj->affected[j].modifier);
    }
}

// #define SUBST(x)   real->x=(char *)calloc(strlen(curr->x)+1,1);  strcpy(real->x,curr->x);

#define SUBST(x)                                \
    CREATE(real->x, char, strlen(curr->x) + 1); \
    strcpy(real->x, curr->x);

static void send_has_skins(const struct char_data* ch, int vnum, int rnum, const char* what);

/* Copies of a prototype share its text and extra-description list, and every copy is on
 * object_list: read_object links each new copy there, restyle_object points a copy already on it
 * at its new prototype's text, and extract_obj unlinks a copy before freeing it. Shape sessions
 * read their own text from the object files. Replaced text a copy holds is never freed, since
 * free_obj leaves a copy's text alone. Implement is a rare builder command, so each check below
 * scans the whole list. */

/* True when an object on object_list holds `text` as its name or any of its descriptions. */
static bool live_object_holds_text(const char* text)
{
    for (const obj_data* object = object_list; object; object = object->next) {
        if (object->name == text || object->short_description == text
            || object->description == text || object->action_description == text) {
            return true;
        }
    }
    return false;
}

/* Frees text a prototype has replaced, unless a live copy still holds it. */
static void release_replaced_text(char* text)
{
    if (text && !live_object_holds_text(text)) {
        RELEASE(text);
    }
}

/* Frees an extra-description list a prototype has replaced, unless a live copy's list reaches
 * it. A list that shares any node of it also reaches its last node, so only that one is sought. */
static void release_replaced_extra_descriptions(extra_descr_data* list)
{
    if (!list) {
        return;
    }
    const extra_descr_data* last_node = list;
    while (last_node->next) {
        last_node = last_node->next;
    }
    for (const obj_data* object = object_list; object; object = object->next) {
        for (const extra_descr_data* node = object->ex_description; node; node = node->next) {
            if (node == last_node) {
                return;
            }
        }
    }
    while (list) {
        extra_descr_data* next_node = list->next;
        RELEASE(list->keyword);
        RELEASE(list->description);
        RELEASE(list);
        list = next_node;
    }
}

void implement_object(const struct char_data* ch)
{
    struct obj_data *real, *curr;
    int rnum, i;
    // int tmp;
    struct extra_descr_data *tmpdescr, *tmp2descr;
    if (!SHAPE_OBJECT(ch)->object) {
        send_to_char("You have nothing to implement\n\r", ch);
        return;
    }
    if (!IS_SET(SHAPE_OBJECT(ch)->flags, SHAPE_OBJECT_LOADED)) {
        send_to_char("You have nothing to implement\n\r", ch);
        return;
    }
    if (SHAPE_OBJECT(ch)->permission == 0) {
        send_to_char("You're not authorized to do this.\n\r", ch);
        return;
    }
    rnum = real_object(SHAPE_OBJECT(ch)->basenum);
    if (rnum < 0) {
        send_to_char("This object does not exist (yet). Maybe reboot will help.\n\r",
            ch);
        return;
    }
    real = &(obj_proto[rnum]);
    curr = SHAPE_OBJECT(ch)->object;
    /* /9 refuses this while editing, but the type can also come from the file. */
    const int type = curr->obj_flags.type_flag;
    if ((type == ITEM_KEY || type == ITEM_NOTE) && !skin_rnums_of(rnum).empty()) {
        send_has_skins(ch, SHAPE_OBJECT(ch)->basenum, rnum, type == ITEM_KEY ? "a key" : "a note");
        send_to_char("Not implemented.\n\r", ch);
        return;
    }
    char* const old_name = real->name;
    char* const old_short_description = real->short_description;
    char* const old_description = real->description;
    char* const old_action_description = real->action_description;
    extra_descr_data* const old_extra_descriptions = real->ex_description;
    SUBST(name);
    SUBST(short_description);
    SUBST(description);
    SUBST(action_description);
    real->obj_flags.type_flag = curr->obj_flags.type_flag;
    real->obj_flags.extra_flags = curr->obj_flags.extra_flags;
    real->obj_flags.wear_flags = curr->obj_flags.wear_flags;
    for (i = 0; i < 5; i++)
        real->obj_flags.value[i] = curr->obj_flags.value[i];
    real->obj_flags.weight = curr->obj_flags.weight;
    real->obj_flags.cost = curr->obj_flags.cost;
    real->obj_flags.cost_per_day = curr->obj_flags.cost_per_day;
    real->obj_flags.level = curr->obj_flags.level;
    real->obj_flags.rarity = curr->obj_flags.rarity;
    real->obj_flags.material = curr->obj_flags.material;
    real->obj_flags.script_number = curr->obj_flags.script_number;
    real->obj_flags.version = curr->obj_flags.version;
    real->obj_flags.skin_base = curr->obj_flags.skin_base;
    real->obj_flags.skin_own = curr->obj_flags.skin_own;
    /* Start a new list instead of stacking onto the old one, so removed or
     * renamed extra descriptions go away for copies loaded from now on.
     * Prepending gives the same order boot builds. */
    real->ex_description = 0;
    for (tmpdescr = curr->ex_description; tmpdescr; tmpdescr = tmpdescr->next) {
        tmp2descr = real->ex_description;

        CREATE1(real->ex_description, extra_descr_data);

        real->ex_description->next = tmp2descr;

        if (tmpdescr->keyword) {
            CREATE(real->ex_description->keyword, char, strlen(tmpdescr->keyword) + 1);
            strcpy(real->ex_description->keyword, tmpdescr->keyword);
        } else {
            CREATE(real->ex_description->keyword, char, 1);
        }
        if (tmpdescr->description) {
            //      real->ex_description->description=(char *)calloc(strlen(tmpdescr->description)+1,1);
            CREATE(real->ex_description->description, char, strlen(tmpdescr->description) + 1);
            strcpy(real->ex_description->description, tmpdescr->description);
        } else {
            //      real->ex_description->description=(char *)calloc(1,1);
            CREATE(real->ex_description->description, char, 1);
        }
    }
    /* Copies already in the game keep the text they were made with. */
    release_replaced_text(old_name);
    release_replaced_text(old_short_description);
    release_replaced_text(old_description);
    release_replaced_text(old_action_description);
    release_replaced_extra_descriptions(old_extra_descriptions);
    for (i = 0; i < MAX_OBJ_AFFECT; i++) {
        real->affected[i].location = curr->affected[i].location;
        real->affected[i].modifier = curr->affected[i].modifier;
    }
    /* A skin's stats come from its base; a base passes its new stats on to its skins. A skin
     * whose base is no longer valid goes live as a plain object; the working copy keeps the
     * builder's base so it can be fixed. */
    if (obj_is_skin(*real)) {
        std::string problem = skin_base_problem(SHAPE_OBJECT(ch)->basenum, real->obj_flags.skin_base);
        /* /23 refuses this while editing, but a skin of this object can go live in the meantime. */
        if (problem.empty() && !skin_rnums_of(rnum).empty())
            problem = "obj " + std::to_string(SHAPE_OBJECT(ch)->basenum) + " has skins";
        if (problem.empty()) {
            copy_skin_stats_from_base(*real, obj_proto[real_object(real->obj_flags.skin_base)]);
        } else {
            char buf[100];
            real->obj_flags.skin_base = 0;
            real->obj_flags.skin_own = 0;
            snprintf(buf, sizeof(buf), "Skin not applied: %s.\n\r", problem.c_str());
            send_to_char(buf, ch);
        }
    }
    if (!obj_is_skin(*real))
        refresh_skins_of_base(rnum);
    send_to_char("Object implemented\n\r", ch);
}

#undef SUBST

/****************************************************************/

#define DESCRCHANGE(line, addr)                                       \
    do {                                                              \
        if (!IS_SET(SHAPE_OBJECT(ch)->flags, SHAPE_SIMPLE_ACTIVE)) {  \
            sprintf(tmpstr, "You are about to change %s:\n\r", line); \
            send_to_char(tmpstr, ch);                                 \
            SHAPE_OBJECT(ch)                                          \
                ->position                                            \
                = shape_standup(ch, POSITION_SHAPING);                \
            ch->specials.prompt_number = 1;                           \
            SET_BIT(SHAPE_OBJECT(ch)->flags, SHAPE_SIMPLE_ACTIVE);    \
            str[0] = 0;                                               \
            SHAPE_OBJECT(ch)->tmpstr = str_dup(addr);                 \
            string_add_init(ch->desc, &(SHAPE_OBJECT(ch)->tmpstr));   \
            return;                                                   \
        } else {                                                      \
            if (SHAPE_OBJECT(ch)->tmpstr) {                           \
                /* The session object owns the text replaced. */      \
                RELEASE(addr);                                        \
                addr = SHAPE_OBJECT(ch)->tmpstr;                      \
                clean_record_text(addr);                              \
            }                                                         \
            SHAPE_OBJECT(ch)                                          \
                ->tmpstr                                              \
                = 0;                                                  \
            REMOVE_BIT(SHAPE_OBJECT(ch)->flags, SHAPE_SIMPLE_ACTIVE); \
            shape_standup(ch, SHAPE_OBJECT(ch)->position);            \
            ch->specials.prompt_number = 6;                           \
            SHAPE_OBJECT(ch)                                          \
                ->editflag                                            \
                = 0;                                                  \
        }                                                             \
    } while (0);

#define LINECHANGE(line, addr)                                                   \
    if (!IS_SET(SHAPE_OBJECT(ch)->flags, SHAPE_DIGIT_ACTIVE)) {                  \
        sprintf(tmpstr, "Enter line %s:\n\r[%s]\n\r", line, (addr) ? addr : ""); \
        send_to_char(tmpstr, ch);                                                \
        SHAPE_OBJECT(ch)                                                         \
            ->position                                                           \
            = shape_standup(ch, POSITION_SHAPING);                               \
        ch->specials.prompt_number = 2;                                          \
        SET_BIT(SHAPE_OBJECT(ch)->flags, SHAPE_DIGIT_ACTIVE);                    \
        shape_prompt_hint(ch);                                                   \
        return;                                                                  \
    } else {                                                                     \
        str[0] = 0;                                                              \
        if (!sscanf(arg, "%s", str)) {                                           \
            SHAPE_OBJECT(ch)                                                     \
                ->editflag                                                       \
                = 0;                                                             \
            shape_standup(ch, SHAPE_OBJECT(ch)->position);                       \
            ch->specials.prompt_number = 6;                                      \
            REMOVE_BIT(SHAPE_OBJECT(ch)->flags, SHAPE_DIGIT_ACTIVE);             \
            break;                                                               \
        }                                                                        \
    }                                                                            \
    if (str[0] != 0) {                                                           \
        if (!strcmp(str, "%q")) {                                                \
            arg[0] = 0;                                                          \
            send_to_char("Empty line set.\n\r", ch);                             \
        }                                                                        \
        RELEASE(addr);                                                           \
        /*addr=(char *)calloc(strlen(arg)+1,1);*/                                \
        CREATE(addr, char, strlen(arg) + 1);                                     \
        strcpy(addr, arg);                                                       \
        tmp1 = strlen(addr);                                                     \
        for (tmp = 0; tmp < tmp1; tmp++) {                                       \
            if (addr[tmp] == '#')                                                \
                addr[tmp] = '+';                                                 \
            if (addr[tmp] == '~')                                                \
                addr[tmp] = '-';                                                 \
        }                                                                        \
    }                                                                            \
    REMOVE_BIT(SHAPE_OBJECT(ch)->flags, SHAPE_DIGIT_ACTIVE);                     \
    shape_standup(ch, SHAPE_OBJECT(ch)->position);                               \
    ch->specials.prompt_number = 6;                                              \
    SHAPE_OBJECT(ch)                                                             \
        ->editflag                                                               \
        = 0;

void extra_coms_obj(struct char_data* ch, char* arg);
void shape_disabled(const struct char_data* ch, const char* prefix, const char* typed);
extern char* item_types[];

/* What the five /12 values mean for each item type; '-' = not used. */
static const char* obj_value_names(int type)
{
    switch (type) {
    case ITEM_LIGHT:
        return "- - HOURS LIT -";
    case ITEM_WAND:
    case ITEM_STAFF:
        return "- - CHARGES SPELL -";
    case ITEM_WEAPON:
        return "OB PARRY BULK WEAPON_TYPE -";
    case ITEM_MISSILE:
        return "- TO_DAM - BREAK% -";
    case ITEM_ARMOR:
        return "ABSORB MIN_ABSORB ENCUMBRANCE DODGE -";
    case ITEM_CONTAINER:
        return "CAPACITY LOCK_FLAGS KEY_VNUM CORPSE -";
    case ITEM_DRINKCON:
    case ITEM_FOUNTAIN:
        return "MAX_UNITS UNITS LIQUID POISONED -";
    case ITEM_FOOD:
        return "FULL - - POISONED -";
    case ITEM_MONEY:
        return "COPPER - - - -";
    case ITEM_SHIELD:
        return "DODGE PARRY ENCUMBRANCE - -";
    case ITEM_LEVER:
        return "ROOM_VNUM DIRECTION - - -";
    case ITEM_KEYRING:
        return "CAPACITY - - - -";
    default:
        return "- - - - - (none used by this type)";
    }
}

/* The skins of base_rnum as "4513, 4514", cut short with ", ..." when the line it goes in, with
 * `used` other columns, would pass 78 columns. */
static std::string skin_vnum_list(int base_rnum, size_t used)
{
    std::vector<std::string> vnums;
    std::string all;
    for (int rnum : skin_rnums_of(base_rnum)) {
        vnums.push_back(std::to_string(obj_index[rnum].virt));
        all += (all.empty() ? "" : ", ") + vnums.back();
    }
    if (used + all.size() <= 78)
        return all;
    std::string cut;
    for (const std::string& vnum : vnums) {
        std::string next = cut + (cut.empty() ? "" : ", ") + vnum;
        if (used + next.size() + 5 > 78)
            break;
        cut = next;
    }
    return cut + ", ...";
}

/* "Obj 4512 has skins (4513, 4514); it can't become <what>." */
static void send_has_skins(const struct char_data* ch, int vnum, int rnum, const char* what)
{
    char head[40], tail[40];
    snprintf(head, sizeof(head), "Obj %d has skins (", vnum);
    snprintf(tail, sizeof(tail), "); it can't become %s.", what);
    std::string line = head + skin_vnum_list(rnum, strlen(head) + strlen(tail)) + tail + "\n\r";
    send_to_char(line.c_str(), ch);
}

/* The vnum of the object being edited. Not basenum: new_obj() resets that to -1 when the vnum is
 * not booted yet, while number holds the vnum on both load paths. */
static int edited_vnum(const struct char_data* ch) { return SHAPE_OBJECT(ch)->number; }

/* The rnum of the object being edited when it has skins, otherwise -1. */
static int edited_base_with_skins(const struct char_data* ch)
{
    int rnum = real_object(edited_vnum(ch));
    if (rnum < 0 || skin_rnums_of(rnum).empty())
        return -1;
    return rnum;
}

/* The stat fields: the ones a skin refuses (see skin_refuses_stat_edit). */
static bool skin_stat_field(int field)
{
    return (field >= 9 && field <= 13) || (field >= 15 && field <= 22 && field != 18);
}

/* Whether the working copy is a skin of a valid base, so its stats come from that base. One whose
 * base is no longer valid (gone, a key, a note, a skin) boots as a plain object and is edited as
 * one. */
static int skin_of_valid_base(const struct char_data* ch, const struct obj_data* obj)
{
    return obj_is_skin(*obj) && skin_base_problem(edited_vnum(ch), obj->obj_flags.skin_base).empty();
}

/* The creation chain's step after `field`; on a skin it skips the stat fields, so the chain
 * shows the refusal once and goes on to text, cost or material. */
static int next_chain_step(const struct char_data* ch, int field)
{
    int next = obj_chain[field];
    if (skin_of_valid_base(ch, SHAPE_OBJECT(ch)->object))
        while (next != 0 && skin_stat_field(next))
            next = obj_chain[next];
    return next;
}

/* A skin takes every stat from its base: refuses stat field `field` on one, and goes on with the
 * creation chain. */
static int skin_refuses_stat_edit(struct char_data* ch, int field)
{
    struct obj_data* obj = SHAPE_OBJECT(ch)->object;
    char buf[100];

    if (!skin_of_valid_base(ch, obj))
        return 0;
    snprintf(buf, sizeof(buf), "Obj %d is a skin of obj %d; edit stats on %d.\n\r",
        edited_vnum(ch), obj->obj_flags.skin_base, obj->obj_flags.skin_base);
    send_to_char(buf, ch);
    if (IS_SET(SHAPE_OBJECT(ch)->flags, SHAPE_DIGIT_ACTIVE)) {
        shape_standup(ch, SHAPE_OBJECT(ch)->position);
        ch->specials.prompt_number = 6;
        REMOVE_BIT(SHAPE_OBJECT(ch)->flags, SHAPE_DIGIT_ACTIVE);
    }
    SHAPE_OBJECT(ch)->editflag = IS_SET(SHAPE_OBJECT(ch)->flags, SHAPE_CHAIN) ? next_chain_step(ch, field) : 0;
    return 1;
}

/* True when a prompt answer is the word "base" and nothing else. */
static int answer_is_base(const char* arg)
{
    char word[8];
    char extra[2];
    return sscanf(arg, " %7s %1s", word, extra) == 1 && !str_cmp(word, "base");
}

/* True when a prompt answer starts with a number (a value, not blank or a word). */
static int answer_is_number(const char* arg)
{
    while (*arg && isspace((unsigned char)*arg))
        arg++;
    if (*arg == '+' || *arg == '-')
        arg++;
    return isdigit((unsigned char)*arg);
}

/* "base" at /14 or /18 on a skin: the skin's cost or material follows the base again. A base that
 * is no longer valid is refused and nothing is copied. Ends the prompt; the caller goes on with the
 * chain. */
static void skin_follow_base(struct char_data* ch, int own_bit)
{
    struct obj_data* obj = SHAPE_OBJECT(ch)->object;
    std::string problem = skin_base_problem(edited_vnum(ch), obj->obj_flags.skin_base);
    char buf[100];

    if (!problem.empty()) {
        snprintf(buf, sizeof(buf), "Can't follow the base: %s.\n\r", problem.c_str());
    } else {
        REMOVE_BIT(obj->obj_flags.skin_own, own_bit);
        copy_skin_stats_from_base(*obj, obj_proto[real_object(obj->obj_flags.skin_base)]);
        if (own_bit == SKIN_OWN_COST)
            snprintf(buf, sizeof(buf), "Cost now follows base (%d).\n\r", obj->obj_flags.cost);
        else
            snprintf(buf, sizeof(buf), "Material now follows base (%d).\n\r", obj->obj_flags.material);
    }
    send_to_char(buf, ch);
    shape_standup(ch, SHAPE_OBJECT(ch)->position);
    ch->specials.prompt_number = 6;
    REMOVE_BIT(SHAPE_OBJECT(ch)->flags, SHAPE_DIGIT_ACTIVE);
}

/* The answer to /23: makes the working copy a skin of base_vnum, or a plain object for 0. */
static void shape_set_skin_base(const struct char_data* ch, int base_vnum)
{
    struct obj_data* obj = SHAPE_OBJECT(ch)->object;
    const int this_vnum = edited_vnum(ch);
    char buf[MAX_STRING_LENGTH];

    if (base_vnum == 0) {
        obj->obj_flags.skin_base = 0;
        obj->obj_flags.skin_own = 0;
        send_to_char("This object is no longer a skin; its stats are now its own.\n\r", ch);
        return;
    }
    /* The rules live in check_skin_base; this only words each refusal for the builder. */
    switch (check_skin_base(this_vnum, base_vnum)) {
    case skin_base_rule::ok:
        break;
    case skin_base_rule::own_base:
        send_to_char("An object can't be its own base.\n\r", ch);
        return;
    case skin_base_rule::no_base:
        snprintf(buf, sizeof(buf), "There is no obj %d.\n\r", base_vnum);
        send_to_char(buf, ch);
        return;
    case skin_base_rule::base_is_key:
        send_to_char("Keys can't have skins.\n\r", ch);
        return;
    case skin_base_rule::base_is_note:
        send_to_char("Notes can't have skins.\n\r", ch);
        return;
    case skin_base_rule::base_is_skin: {
        const int its_base = obj_proto[real_object(base_vnum)].obj_flags.skin_base;
        snprintf(buf, sizeof(buf), "Obj %d is a skin of obj %d; use /23 %d.\n\r", base_vnum,
            its_base, its_base);
        send_to_char(buf, ch);
        return;
    }
    }
    const struct obj_data& base = obj_proto[real_object(base_vnum)];
    const int this_rnum = edited_base_with_skins(ch);
    if (this_rnum >= 0) {
        send_has_skins(ch, this_vnum, this_rnum, "a skin");
        return;
    }

    const int old_cost = obj->obj_flags.cost;
    const int old_material = obj->obj_flags.material;
    obj->obj_flags.skin_base = base_vnum;
    obj->obj_flags.skin_own = 0;
    copy_skin_stats_from_base(*obj, base);

    /* The base's name is cut, if need be, to keep the line within 78 columns. */
    const int name_room = 78 - snprintf(buf, sizeof(buf), "This object is now a skin of obj %d ().", base_vnum);
    snprintf(buf, sizeof(buf), "This object is now a skin of obj %d (%.*s).\n\r", base_vnum,
        name_room > 0 ? name_room : 0, base.short_description ? base.short_description : "");
    send_to_char(buf, ch);
    snprintf(buf, sizeof(buf), "Stats, values, affects and flags now come from %d.\n\r", base_vnum);
    send_to_char(buf, ch);
    if (old_cost != obj->obj_flags.cost) {
        snprintf(buf, sizeof(buf), "Cost was %d, now follows base (%d).\n\r", old_cost, obj->obj_flags.cost);
        send_to_char(buf, ch);
    }
    if (old_material != obj->obj_flags.material) {
        snprintf(buf, sizeof(buf), "Material was %d, now follows base (%d).\n\r", old_material,
            obj->obj_flags.material);
        send_to_char(buf, ch);
    }
}

void shape_center_obj(struct char_data* ch, char* arg)
{

    char str[1000], tmpstr[1000];

    //  int i,i1;
    int tmp, tmp1, tmp2, tmp3, tmp4, tmp5, choice;
    struct obj_data* obj;
    // char key;
    struct extra_descr_data* current_descr;
    choice = 0;
    obj = SHAPE_OBJECT(ch)->object;

    /*printf("Entering shape_center_obj, arg=%s.\n\r",arg);*/
    tmp = SHAPE_OBJECT(ch)->procedure;
    //  SHAPE_OBJECT(ch)->procedure=SHAPE_EDIT;
    if ((tmp != SHAPE_NONE) && (tmp != SHAPE_EDIT)) {
        // send_to_char("mixed orders. aborted - better restart shaping.\n\r",ch);
        extra_coms_obj(ch, arg);
        return;
    }
    if (tmp == SHAPE_NONE) {
        send_to_char("Enter any non-number for list of commands, 99 for list of editor commands:", ch);
        SHAPE_OBJECT(ch)
            ->editflag
            = 0;
        REMOVE_BIT(SHAPE_OBJECT(ch)->flags, SHAPE_SIMPLE_ACTIVE);
        return;
    }
    do { /* big chain loop */
        if (SHAPE_PROTO(ch)->editflag == 0) {
            REMOVE_BIT(SHAPE_OBJECT(ch)->flags, SHAPE_CHAIN);
            sscanf(arg, "%s", str);
            if ((str[0] >= '0') && (str[0] <= '9')) {
                SHAPE_OBJECT(ch)
                    ->editflag
                    = atoi(str); /* now switching to integers.. */
                str[0] = 0;
            } else {
                extra_coms_obj(ch, arg);
                return;
            }
        }
        if (!IS_SET(SHAPE_OBJECT(ch)->flags, SHAPE_OBJECT_LOADED)) {
            send_to_char("No object loaded for shaping. Strange.\n\r", ch);
            SHAPE_OBJECT(ch)
                ->editflag
                = 0;
            return;
        }

        current_descr = SHAPE_OBJECT(ch)->object->ex_description;

        if (current_descr)
            while (current_descr->next)
                current_descr = current_descr->next;

        /* "%h" at a prompt: the field's help, then the same prompt again. */
        if (IS_SET(SHAPE_OBJECT(ch)->flags, SHAPE_DIGIT_ACTIVE) && shape_help_asked(arg)) {
            char key[40];
            sprintf(key, "OBJ %d", SHAPE_OBJECT(ch)->editflag);
            shape_prompt_help(ch, "shape", key, SHAPE_OBJECT(ch)->position);
            REMOVE_BIT(SHAPE_OBJECT(ch)->flags, SHAPE_DIGIT_ACTIVE);
        }
        switch (SHAPE_OBJECT(ch)->editflag) {

        case 1:

            /* An empty keyword list or name leaves the object unusable. */
            if (IS_SET(SHAPE_OBJECT(ch)->flags, SHAPE_DIGIT_ACTIVE) && sscanf(arg, "%s", str) == 1 && !strcmp(str, "%q")) {
                send_to_char("Aliases can't be empty.\n\r", ch);
                arg[0] = 0;
            }
            LINECHANGE("ALIASES, keywords separated by spaces, e.g. sword longsword\n\r"
                       "  (drink containers: first word is the liquid) (blank = keep)",
                SHAPE_OBJECT(ch)->object->name)

            if (IS_SET(SHAPE_OBJECT(ch)->flags, SHAPE_CHAIN))
                SHAPE_OBJECT(ch)
                    ->editflag
                    = obj_chain[1];
            else
                SHAPE_OBJECT(ch)
                    ->editflag
                    = 0;
            break;

        case 2:
            /* An empty keyword list or name leaves the object unusable. */
            if (IS_SET(SHAPE_OBJECT(ch)->flags, SHAPE_DIGIT_ACTIVE) && sscanf(arg, "%s", str) == 1 && !strcmp(str, "%q")) {
                send_to_char("The reference description can't be empty.\n\r", ch);
                arg[0] = 0;
            }
            LINECHANGE("REFERENCE DESCRIPTION, no capital or period,\n\r"
                       "  e.g. a steel longsword (blank = keep)",
                SHAPE_OBJECT(ch)->object->short_description)

            if (IS_SET(SHAPE_OBJECT(ch)->flags, SHAPE_CHAIN))
                SHAPE_OBJECT(ch)
                    ->editflag
                    = obj_chain[2];
            else
                SHAPE_OBJECT(ch)
                    ->editflag
                    = 0;
            break;
        case 3:
            LINECHANGE("ROOM LINE, shown when it lies in a room,\n\r"
                       "  e.g. A steel longsword is lying here. (blank = keep, %q = empty)",
                SHAPE_OBJECT(ch)->object->description)
            if (IS_SET(SHAPE_OBJECT(ch)->flags, SHAPE_CHAIN))
                SHAPE_OBJECT(ch)
                    ->editflag
                    = obj_chain[3];
            else
                SHAPE_OBJECT(ch)
                    ->editflag
                    = 0;
            break;
        case 4:
            DESCRCHANGE("LOOK TEXT, shown by look/examine", SHAPE_OBJECT(ch)->object->action_description)
            if (IS_SET(SHAPE_OBJECT(ch)->flags, SHAPE_CHAIN))
                SHAPE_OBJECT(ch)
                    ->editflag
                    = obj_chain[4];
            else
                SHAPE_OBJECT(ch)
                    ->editflag
                    = 0;
            break;
        case 5:
            if (current_descr) {
                //	current_descr->next=(struct extra_descr_data *)calloc(1,sizeof(struct extra_descr_data));
                CREATE1(current_descr->next, extra_descr_data);
                // bzero((char *)(current_descr->next),sizeof(struct extra_descr_data));
                current_descr = current_descr->next;
            } else {
                CREATE1(SHAPE_OBJECT(ch)->object->ex_description, extra_descr_data);

                current_descr = SHAPE_OBJECT(ch)->object->ex_description;
            }
            current_descr->keyword = str_dup("");
            current_descr->description = str_dup("");
            send_to_char("Extra description added, fill it now.\n\r", ch);

            SHAPE_OBJECT(ch)
                ->editflag
                = 0;
            SET_BIT(SHAPE_OBJECT(ch)->flags, SHAPE_CHAIN);

            if (IS_SET(SHAPE_OBJECT(ch)->flags, SHAPE_CHAIN))
                SHAPE_OBJECT(ch)
                    ->editflag
                    = obj_chain[5];
            else
                SHAPE_OBJECT(ch)
                    ->editflag
                    = 0;
            break;

        case 6:
            if (current_descr) {
                LINECHANGE("EXTRA DESCRIPTION KEYWORDS (the last one), separated by spaces\n\r"
                           "  (blank = keep, %q = empty)",
                    current_descr->keyword)
            } else {
                send_to_char("No extra descriptions exist, use '/5' to add one\n\r", ch);
                SHAPE_OBJECT(ch)
                    ->editflag
                    = 0;
                break;
            }
            if (IS_SET(SHAPE_OBJECT(ch)->flags, SHAPE_CHAIN))
                SHAPE_OBJECT(ch)
                    ->editflag
                    = obj_chain[6];
            else
                SHAPE_OBJECT(ch)
                    ->editflag
                    = 0;
            break;

        case 7:
            if (current_descr) {
                DESCRCHANGE("EXTRA DESCRIPTION TEXT (the last one)", current_descr->description)
            } else {
                send_to_char("No extra descriptions exist, use '/5' to add one\n\r", ch);
                SHAPE_OBJECT(ch)
                    ->editflag
                    = 0;
                break;
            }

            if (IS_SET(SHAPE_OBJECT(ch)->flags, SHAPE_CHAIN))
                SHAPE_OBJECT(ch)
                    ->editflag
                    = obj_chain[7];
            else
                SHAPE_OBJECT(ch)
                    ->editflag
                    = 0;
            break;

        case 8:
            SHAPE_OBJECT(ch)
                ->editflag
                = 0;
            current_descr = SHAPE_OBJECT(ch)->object->ex_description;
            if (!current_descr) {
                send_to_char("No extra descriptions exist, nothing removed.\n\r", ch);
                break;
            }
            if (!current_descr->next) {
                RELEASE(SHAPE_OBJECT(ch)->object->ex_description->keyword);
                RELEASE(SHAPE_OBJECT(ch)->object->ex_description->description);
                RELEASE(SHAPE_OBJECT(ch)->object->ex_description);
                SHAPE_OBJECT(ch)
                    ->object->ex_description
                    = 0;
                current_descr = 0;
                send_to_char("Extra description removed, no extra descriptions now.\n\r", ch);
                if (IS_SET(SHAPE_OBJECT(ch)->flags, SHAPE_CHAIN))
                    SHAPE_OBJECT(ch)
                        ->editflag
                        = obj_chain[8];
                else
                    SHAPE_OBJECT(ch)
                        ->editflag
                        = 0;
                break;
            }
            while ((current_descr->next)->next)
                current_descr = current_descr->next;
            RELEASE(current_descr->next->keyword);
            RELEASE(current_descr->next->description);
            RELEASE(current_descr->next);
            current_descr->next = 0;
            send_to_char("Extra description removed, the previous one selected now.\n\r", ch);
            if (IS_SET(SHAPE_OBJECT(ch)->flags, SHAPE_CHAIN))
                SHAPE_OBJECT(ch)
                    ->editflag
                    = obj_chain[8];
            else
                SHAPE_OBJECT(ch)
                    ->editflag
                    = 0;
            break;

#undef DESCRCHANGE

#define DIGITCHANGE(line, addr)                                 \
    if (!IS_SET(SHAPE_OBJECT(ch)->flags, SHAPE_DIGIT_ACTIVE)) { \
        sprintf(tmpstr, "Enter %s [%d]:\n\r", line, addr);      \
        send_to_char(tmpstr, ch);                               \
        SHAPE_OBJECT(ch)                                        \
            ->position                                          \
            = shape_standup(ch, POSITION_SHAPING);              \
        ch->specials.prompt_number = 3;                         \
        SET_BIT(SHAPE_OBJECT(ch)->flags, SHAPE_DIGIT_ACTIVE);   \
        shape_prompt_hint(ch);                                  \
        return;                                                 \
    } else {                                                    \
        tmp = addr;                                             \
        shape_flag_value(ch, arg, &tmp);                        \
        addr = tmp;                                             \
    }                                                           \
    shape_standup(ch, SHAPE_OBJECT(ch)->position);              \
    ch->specials.prompt_number = 6;                             \
    REMOVE_BIT(SHAPE_OBJECT(ch)->flags, SHAPE_DIGIT_ACTIVE);    \
    SHAPE_OBJECT(ch)                                            \
        ->editflag                                              \
        = 0;

        case 9:
            if (skin_refuses_stat_edit(ch, 9))
                break;
            if (!IS_SET(SHAPE_OBJECT(ch)->flags, SHAPE_DIGIT_ACTIVE))
                send_to_char("   1 LIGHT          8 TREASURE   15 CONTAINER          22 BOAT\n\r"
                             "   2 SCROLL         9 ARMOR      16 NOTE               23 FOUNTAIN\n\r"
                             "   3 WAND          10 POTION     17 LIQUID CONTAINER   24 SHIELD\n\r"
                             "   4 STAFF         11 WORN       18 KEY                25 LEVER\n\r"
                             "   5 WEAPON        12 OTHER      19 FOOD               26 KEYRING\n\r"
                             "   6 FIRE WEAPON   13 TRASH      20 MONEY\n\r"
                             "   7 MISSILE       14 TRAP       21 PEN\n\r\n\r",
                    ch);
            tmp3 = obj->obj_flags.type_flag;
            DIGITCHANGE("TYPE (changes what /12 means)", obj->obj_flags.type_flag);
            if (tmp != tmp3 && (tmp < 1 || tmp > ITEM_KEYRING)) { /* the highest type */
                sprintf(tmpstr, "Item type must be 1-%d. dropped.\n\r", ITEM_KEYRING);
                send_to_char(tmpstr, ch);
                obj->obj_flags.type_flag = tmp3;
            } else if (tmp != tmp3 && (tmp == ITEM_KEY || tmp == ITEM_NOTE) && edited_base_with_skins(ch) >= 0) {
                send_has_skins(ch, edited_vnum(ch), edited_base_with_skins(ch), tmp == ITEM_KEY ? "a key" : "a note");
                obj->obj_flags.type_flag = tmp3;
            }

            if (IS_SET(SHAPE_OBJECT(ch)->flags, SHAPE_CHAIN))
                SHAPE_OBJECT(ch)
                    ->editflag
                    = obj_chain[9];
            else
                SHAPE_OBJECT(ch)
                    ->editflag
                    = 0;
            break;

        case 10:
            if (skin_refuses_stat_edit(ch, 10))
                break;
            if (!IS_SET(SHAPE_OBJECT(ch)->flags, SHAPE_DIGIT_ACTIVE))
                send_to_char("   0 GLOW         6 MAGIC          12 NORENT      18 DWARF      24 MOBORC\n\r"
                             "   1 HUM          7 NODROP         13 (unused)    19 WOODELF    25 MAGUS\n\r"
                             "   2 DARK         8 BROKEN         14 NOINVIS     20 HOBBIT     26 OLOGHAI\n\r"
                             "   3 BREAKABLE    9 ANTI_GOOD      15 WILLPOWER   21 BEORNING   27 HARADRIM\n\r"
                             "   4 EVIL        10 ANTI_EVIL      16 IMM         22 URUK       28 STAY_ZONE\n\r"
                             "   5 INVISIBLE   11 ANTI_NEUTRAL   17 HUMAN       23 ORC\n\r\n\r",
                    ch);
            DIGITCHANGE("EXTRA FLAGS: pN sets bit N, mN clears it, e.g. p1 p7 m2", obj->obj_flags.extra_flags);

            if (IS_SET(SHAPE_OBJECT(ch)->flags, SHAPE_CHAIN))
                SHAPE_OBJECT(ch)
                    ->editflag
                    = obj_chain[10];
            else
                SHAPE_OBJECT(ch)
                    ->editflag
                    = 0;
            break;

        case 11:
            if (skin_refuses_stat_edit(ch, 11))
                break;
            if (!IS_SET(SHAPE_OBJECT(ch)->flags, SHAPE_DIGIT_ACTIVE))
                send_to_char("   0 TAKE      3 BODY    6 FEET     9 SHIELD   12 WRIST   15 THROW\n\r"
                             "   1 FINGER    4 HEAD    7 HANDS   10 ABOUT    13 WIELD   16 BACK\n\r"
                             "   2 NECK      5 LEGS    8 ARMS    11 WAIST    14 HOLD    17 BELT\n\r\n\r",
                    ch);
            DIGITCHANGE("WEAR FLAGS: pN sets bit N, mN clears it, e.g. p1 p7 m2", obj->obj_flags.wear_flags)
            if (IS_SET(SHAPE_OBJECT(ch)->flags, SHAPE_CHAIN))
                SHAPE_OBJECT(ch)
                    ->editflag
                    = obj_chain[11];
            else
                SHAPE_OBJECT(ch)
                    ->editflag
                    = 0;
            break;

        case 12:
            if (skin_refuses_stat_edit(ch, 12))
                break;
            if (!IS_SET(SHAPE_OBJECT(ch)->flags, SHAPE_DIGIT_ACTIVE)) {
                sprintf(tmpstr, "Enter VALUES for %s: %s\n\r"
                                "  ('-' = not used; fewer numbers keep the rest, blank keeps all)\n\r"
                                "Current: %d %d %d %d %d\n\r",
                    obj->obj_flags.type_flag <= ITEM_KEYRING ? item_types[obj->obj_flags.type_flag] : "?",
                    obj_value_names(obj->obj_flags.type_flag),
                    obj->obj_flags.value[0], obj->obj_flags.value[1], obj->obj_flags.value[2],
                    obj->obj_flags.value[3], obj->obj_flags.value[4]);
                send_to_char(tmpstr, ch);
                SHAPE_OBJECT(ch)
                    ->position
                    = shape_standup(ch, POSITION_SHAPING);
                ch->specials.prompt_number = 3;
                SET_BIT(SHAPE_OBJECT(ch)->flags, SHAPE_DIGIT_ACTIVE);
                shape_prompt_hint(ch);
                return;
            } else {
                if (!(tmp5 = sscanf(arg, "%d %d %d %d %d", &tmp, &tmp1, &tmp2, &tmp3, &tmp4))) {
                    send_to_char("numbers required. dropped\n\r", ch);
                    shape_standup(ch, SHAPE_OBJECT(ch)->position);
                    ch->specials.prompt_number = 6;
                    REMOVE_BIT(SHAPE_OBJECT(ch)->flags, SHAPE_DIGIT_ACTIVE);
                    SHAPE_OBJECT(ch)
                        ->editflag
                        = 0;
                    return;
                }
            }
            if (tmp5 > 0)
                obj->obj_flags.value[0] = tmp;
            if (tmp5 > 1)
                obj->obj_flags.value[1] = tmp1;
            if (tmp5 > 2)
                obj->obj_flags.value[2] = tmp2;
            if (tmp5 > 3)
                obj->obj_flags.value[3] = tmp3;
            if (tmp5 > 4)
                obj->obj_flags.value[4] = tmp4;

            shape_standup(ch, SHAPE_OBJECT(ch)->position);
            ch->specials.prompt_number = 6;
            REMOVE_BIT(SHAPE_OBJECT(ch)->flags, SHAPE_DIGIT_ACTIVE);
            SHAPE_OBJECT(ch)
                ->editflag
                = 0;
            if (IS_SET(SHAPE_OBJECT(ch)->flags, SHAPE_CHAIN))
                SHAPE_OBJECT(ch)
                    ->editflag
                    = obj_chain[12];
            else
                SHAPE_OBJECT(ch)
                    ->editflag
                    = 0;
            break;

        case 13:
            if (skin_refuses_stat_edit(ch, 13))
                break;
            DIGITCHANGE("WEIGHT (1/100 lb)", obj->obj_flags.weight)

            if (IS_SET(SHAPE_OBJECT(ch)->flags, SHAPE_CHAIN))
                SHAPE_OBJECT(ch)
                    ->editflag
                    = obj_chain[13];
            else
                SHAPE_OBJECT(ch)
                    ->editflag
                    = 0;
            break;

        case 14:
            if (IS_SET(SHAPE_OBJECT(ch)->flags, SHAPE_DIGIT_ACTIVE) && obj_is_skin(*obj) && answer_is_base(arg)) {
                skin_follow_base(ch, SKIN_OWN_COST);
            } else {
                tmp3 = obj->obj_flags.cost;
                DIGITCHANGE("COST in copper (0 = shops won't trade it)", obj->obj_flags.cost)
                if (obj_is_skin(*obj) && (tmp3 != obj->obj_flags.cost || answer_is_number(arg)))
                    SET_BIT(obj->obj_flags.skin_own, SKIN_OWN_COST);
            }

            if (IS_SET(SHAPE_OBJECT(ch)->flags, SHAPE_CHAIN))
                SHAPE_OBJECT(ch)
                    ->editflag
                    = next_chain_step(ch, 14);
            else
                SHAPE_OBJECT(ch)
                    ->editflag
                    = 0;
            break;

        case 15:
            if (skin_refuses_stat_edit(ch, 15))
                break;
            DIGITCHANGE("RENT PER DAY (-1 = cost/100, below -1 = can't rent;\n\r  -N sets negative)", obj->obj_flags.cost_per_day)
            if (string_to_negative_value(arg, &tmp))
                obj->obj_flags.cost_per_day = tmp;

            if (IS_SET(SHAPE_OBJECT(ch)->flags, SHAPE_CHAIN))
                SHAPE_OBJECT(ch)
                    ->editflag
                    = obj_chain[15];
            else
                SHAPE_OBJECT(ch)
                    ->editflag
                    = 0;
            break;

        case 16:
            if (skin_refuses_stat_edit(ch, 16))
                break;
            tmp3 = obj->obj_flags.level;
            DIGITCHANGE("LEVEL 0-255 (item power, not a level needed to use it)", obj->obj_flags.level)
            if (tmp != tmp3 && (tmp < 0 || tmp > 255)) {
                send_to_char("Level must be 0-255. dropped.\n\r", ch);
                obj->obj_flags.level = tmp3;
            }

            if (IS_SET(SHAPE_OBJECT(ch)->flags, SHAPE_CHAIN))
                SHAPE_OBJECT(ch)
                    ->editflag
                    = obj_chain[16];
            else
                SHAPE_OBJECT(ch)
                    ->editflag
                    = 0;
            break;

        case 17:
            if (skin_refuses_stat_edit(ch, 17))
                break;
            DIGITCHANGE("RARITY (unused)", obj->obj_flags.rarity)

            if (IS_SET(SHAPE_OBJECT(ch)->flags, SHAPE_CHAIN))
                SHAPE_OBJECT(ch)
                    ->editflag
                    = obj_chain[17];
            else
                SHAPE_OBJECT(ch)
                    ->editflag
                    = 0;
            break;

        case 18:
            if (!IS_SET(SHAPE_OBJECT(ch)->flags, SHAPE_DIGIT_ACTIVE))
                send_to_char("   0 usual      3 chain    6 stone      9 silver    12 glass\n\r"
                             "   1 cloth      4 metal    7 crystal   10 mithril   13 plant\n\r"
                             "   2 leather    5 wood     8 gold      11 fur\n\r\n\r",
                    ch);
            if (IS_SET(SHAPE_OBJECT(ch)->flags, SHAPE_DIGIT_ACTIVE) && obj_is_skin(*obj) && answer_is_base(arg)) {
                skin_follow_base(ch, SKIN_OWN_MATERIAL);
            } else {
                tmp3 = obj->obj_flags.material;
                DIGITCHANGE("MATERIAL", obj->obj_flags.material)
                if (tmp != tmp3 && (tmp < 0 || tmp > 13)) {
                    send_to_char("Material must be 0-13. dropped.\n\r", ch);
                    obj->obj_flags.material = tmp3;
                } else if (obj_is_skin(*obj) && (tmp != tmp3 || answer_is_number(arg))) {
                    SET_BIT(obj->obj_flags.skin_own, SKIN_OWN_MATERIAL);
                }
            }

            if (IS_SET(SHAPE_OBJECT(ch)->flags, SHAPE_CHAIN))
                SHAPE_OBJECT(ch)
                    ->editflag
                    = obj_chain[18];
            else
                SHAPE_OBJECT(ch)
                    ->editflag
                    = 0;
            break;
        case 19: /* 'Affected' features... */
            if (skin_refuses_stat_edit(ch, 19))
                break;
            if (!IS_SET(SHAPE_OBJECT(ch)->flags, SHAPE_DIGIT_ACTIVE)) {
                sprintf(tmpstr, "   1 STR     9 AGE      17 DODGE       24 SPEED        31 VULN BIT\n\r"
                                "   2 DEX    10 WEIGHT   18 OB          25 PERCEPTION   33 BEND\n\r"
                                "   3 INT    11 HEIGHT   19 DAMROLL     27 SPELL        38 SPELL PEN\n\r"
                                "   4 WILL   12 MANA     20 SAVING      28 AFFECT BIT   39 SPELL POWER\n\r"
                                "   5 CON    13 HIT      21 WILLPOWER   29 MANA REGEN\n\r"
                                "   6 LEA    14 MOVE     23 VISION      30 RESIST BIT\n\r"
                                "\n\r"
                                "Enter AFFECTS: (location modifier) (location modifier), 2 at most\n\r"
                                "  (slots you don't type are kept, blank keeps both)\n\r"
                                "Current: (%d %d) (%d %d)\n\r",
                    obj->affected[0].location, obj->affected[0].modifier,
                    obj->affected[1].location, obj->affected[1].modifier);
                send_to_char(tmpstr, ch);
                SET_BIT(SHAPE_OBJECT(ch)->flags, SHAPE_DIGIT_ACTIVE);
                shape_prompt_hint(ch);
                ch->specials.prompt_number = 3;
                SHAPE_OBJECT(ch)
                    ->position
                    = shape_standup(ch, POSITION_SHAPING);
                return;
            } else {
                tmp = 0;
                tmp3 = 0;
                while ((2 == (tmp4 = sscanf(arg + tmp3, " (%d %d) ", &tmp1, &tmp2))) && (tmp < MAX_OBJ_AFFECT)) {
                    obj->affected[tmp].location = tmp1;
                    obj->affected[tmp].modifier = tmp2;
                    tmp++;
                    while ((arg[tmp3] != ')') && (arg[tmp3] != 0) && (tmp3 < 255))
                        tmp3++;
                    tmp3++;
                    /*	  printf("Arg string:%s, tmp=%d tmp4=%d tmp3=%d\n\r",arg+tmp3,tmp,tmp4,tmp3);*/
                }
                /*	  printf("Arg string:%s, tmp=%d tmp4=%d tmp3=%d\n\r",arg+tmp3,tmp,tmp4,tmp3);*/
                if (tmp == 0) {
                    /* Blank keeps both slots without a complaint. */
                    if (sscanf(arg, "%s", str) == 1)
                        send_to_char("No affections were set. Dropped.\n\r", ch);
                    shape_standup(ch, SHAPE_OBJECT(ch)->position);
                    ch->specials.prompt_number = 6;

                    REMOVE_BIT(SHAPE_OBJECT(ch)->flags, SHAPE_DIGIT_ACTIVE);
                    SHAPE_OBJECT(ch)
                        ->editflag
                        = 0;

                    if (IS_SET(SHAPE_OBJECT(ch)->flags, SHAPE_CHAIN))
                        SHAPE_OBJECT(ch)
                            ->editflag
                            = obj_chain[19];
                    else
                        SHAPE_OBJECT(ch)
                            ->editflag
                            = 0;
                    return;
                }
            }

            shape_standup(ch, SHAPE_OBJECT(ch)->position);
            ch->specials.prompt_number = 6;
            REMOVE_BIT(SHAPE_OBJECT(ch)->flags, SHAPE_DIGIT_ACTIVE);
            SHAPE_OBJECT(ch)
                ->editflag
                = 0;
            if (IS_SET(SHAPE_OBJECT(ch)->flags, SHAPE_CHAIN))
                SHAPE_OBJECT(ch)
                    ->editflag
                    = obj_chain[19];
            else
                SHAPE_OBJECT(ch)
                    ->editflag
                    = 0;
            break;

        case 20:
            if (skin_refuses_stat_edit(ch, 20))
                break;
            DIGITCHANGE("PROGRAM (not saved, no effect)", obj->obj_flags.prog_number)
            if (IS_SET(SHAPE_OBJECT(ch)->flags, SHAPE_CHAIN))
                SHAPE_OBJECT(ch)
                    ->editflag
                    = obj_chain[20];
            else
                SHAPE_OBJECT(ch)
                    ->editflag
                    = 0;
            break;

        case 21:
            if (skin_refuses_stat_edit(ch, 21))
                break;
            DIGITCHANGE("SCRIPT vnum (0 = none)", obj->obj_flags.script_number)

            if (IS_SET(SHAPE_OBJECT(ch)->flags, SHAPE_CHAIN))
                SHAPE_OBJECT(ch)
                    ->editflag
                    = obj_chain[21];
            else
                SHAPE_OBJECT(ch)
                    ->editflag
                    = 0;
            break;

        case 22: {
            if (skin_refuses_stat_edit(ch, 22))
                break;
            /* No typed numbers: a bump goes one past the number in the world file, so it cannot
             * land on a number this file has handed out. (A world file replaced by an older copy
             * takes the count back with it: see the OBJ 22 help.) Off keeps the number, negated. */
            const int current = obj->obj_flags.version;
            const int last = current < 0 ? -current : current;
            if (!IS_SET(SHAPE_OBJECT(ch)->flags, SHAPE_DIGIT_ACTIVE)) {
                sprintf(tmpstr, "   b   bump to %d (players' copies refresh at their next login)\n\r", last + 1);
                send_to_char(tmpstr, ch);
                if (current > 0) {
                    sprintf(tmpstr, "   o   turn off (keeps %d for the next bump)\n\r", last);
                    send_to_char(tmpstr, ch);
                }
                sprintf(tmpstr, "\n\rEnter VERSION: %s, blank keeps it [%s]:\n\r", current > 0 ? "b or o" : "b",
                    object_version_text(current).c_str());
                send_to_char(tmpstr, ch);
                SHAPE_OBJECT(ch)->position = shape_standup(ch, POSITION_SHAPING);
                ch->specials.prompt_number = 3;
                SET_BIT(SHAPE_OBJECT(ch)->flags, SHAPE_DIGIT_ACTIVE);
                shape_prompt_hint(ch);
                return;
            }
            /* Exactly "b" or "o": "on" must not read as o and turn it off. */
            char* answer = arg;
            while (*answer && isspace(*answer))
                answer++;
            char* answer_end = answer + strlen(answer);
            while (answer_end > answer && isspace(answer_end[-1]))
                answer_end--;
            const char choice = answer_end - answer == 1 ? LOWER(*answer) : '\0';
            if (choice == 'b') {
                obj->obj_flags.version = last + 1;
                sprintf(tmpstr, "Version is now %s.\n\r", object_version_text(obj->obj_flags.version).c_str());
                send_to_char(tmpstr, ch);
            } else if (choice == 'o' && current > 0) {
                obj->obj_flags.version = -current;
                sprintf(tmpstr, "Version is now %s.\n\r", object_version_text(obj->obj_flags.version).c_str());
                send_to_char(tmpstr, ch);
            } else if (*answer) {
                send_to_char("Version unchanged.\n\r", ch);
            }
            shape_standup(ch, SHAPE_OBJECT(ch)->position);
            ch->specials.prompt_number = 6;
            REMOVE_BIT(SHAPE_OBJECT(ch)->flags, SHAPE_DIGIT_ACTIVE);

            if (IS_SET(SHAPE_OBJECT(ch)->flags, SHAPE_CHAIN))
                SHAPE_OBJECT(ch)->editflag = obj_chain[22];
            else
                SHAPE_OBJECT(ch)->editflag = 0;
            break;
        }

        case 23: {
            const int old_base = obj->obj_flags.skin_base;
            int new_base = old_base;
            DIGITCHANGE("BASE (vnum of the object this is a skin of, 0 = none)", new_base)
            if (new_base != old_base)
                shape_set_skin_base(ch, new_base);

            if (IS_SET(SHAPE_OBJECT(ch)->flags, SHAPE_CHAIN))
                SHAPE_OBJECT(ch)->editflag = obj_chain[23];
            else
                SHAPE_OBJECT(ch)->editflag = 0;
            break;
        }

#undef DIGITCHANGE

        case 49:
            send_to_char("You invoked object creation sequence.\n\r", ch);
            SHAPE_OBJECT(ch)
                ->editflag
                = obj_chain[49];
            SET_BIT(SHAPE_OBJECT(ch)->flags, SHAPE_CHAIN);
            break;

        case 50:

            list_object(ch, (struct obj_data*)obj);

            SHAPE_OBJECT(ch)
                ->editflag
                = 0;

            break;

        default:

            list_help_obj(ch);

            SHAPE_OBJECT(ch)
                ->editflag
                = 0;

            break;
        }
    } while (SHAPE_OBJECT(ch)->editflag);
    return;
}

#undef LINECHANGE
void list_help_obj(const struct char_data* ch)
{

    send_to_char("possible fields are:\n\r1 - alias(es);\n\r", ch);

    send_to_char("2 - reference description;\n\r3 - full description;\n\r", ch);

    send_to_char("4 - action description;\n\r", ch);

    send_to_char("5 - add extra description;\n\r", ch);

    send_to_char("6 - edit keyword of the last extra description;\n\r", ch);

    send_to_char("7 - edit text of the last extra description;\n\r", ch);

    send_to_char("8 - remove last extra description;\n\r9 - type flag;\n\r", ch);

    send_to_char("10 - extra flags;\n\r11 - wear flags;\n\r12 - values;\n\r", ch);

    send_to_char("13 - weight;\n\r14 - cost;\n\r", ch);

    send_to_char("15 - cost per day;\n\r", ch);

    send_to_char("16 - level;\n\r", ch);

    send_to_char("17 - rarity;\n\r", ch);

    send_to_char("18 - material;\n\r", ch);

    send_to_char("19 - affections;\n\r", ch);

    send_to_char("20 - program (not saved);\n\r", ch);

    send_to_char("21 - script number (for special cases only);\n\r", ch);

    send_to_char("22 - version (refreshes players' copies at login);\n\r", ch);

    send_to_char("23 - base object (makes this a skin);\n\r", ch);

    send_to_char("49 - object creation sequence;\n\r", ch);

    send_to_char("50 - list;\n\r", ch);

    return;
}

/*********--------------------------------*********/

void list_object(const struct char_data* ch, const struct obj_data* obj)
{

    static char str[MAX_STRING_LENGTH]; // str1[100];

    struct extra_descr_data* tmpdesc;

    int i;

    /* On a skin, the stats come from its base; cost and material may be its own. */
    char from[40] = "";
    char cost_from[40] = "";
    char material_from[40] = "";
    if (skin_of_valid_base(ch, obj)) {
        snprintf(from, sizeof(from), " (from %d)", obj->obj_flags.skin_base);
        strcpy(cost_from, IS_SET(obj->obj_flags.skin_own, SKIN_OWN_COST) ? " [own]" : from);
        strcpy(material_from, IS_SET(obj->obj_flags.skin_own, SKIN_OWN_MATERIAL) ? " [own]" : from);
    }

    sprintf(str, "(1) alias(es)    :%s\n\r", obj->name);

    send_to_char(str, ch);

    sprintf(str, "(2) reference description :%s\n\r", obj->short_description);

    send_to_char(str, ch);

    sprintf(str, "(3) full  description     :%s\n\r", obj->description);

    send_to_char(str, ch);

    sprintf(str, "(4) action description  :\n\r%s\n\r", obj->action_description);

    send_to_char(str, ch);

    tmpdesc = obj->ex_description;

    if (tmpdesc) {

        send_to_char("Extra description(s):\n\r", ch);

        for (; tmpdesc; tmpdesc = tmpdesc->next) {

            sprintf(str, "(6) Keyword:%s\n\r(7) Text:%s\n\r",

                tmpdesc->keyword, tmpdesc->description);

            send_to_char(str, ch);
        }

    }

    else
        send_to_char("No extra descriptions for this object\n\r", ch);

    sprintf(str, "(9) type flag    :%d%s\n\r", obj->obj_flags.type_flag, from);

    send_to_char(str, ch);

    sprintf(str, "(10) extra flag   :%d%s\n\r", obj->obj_flags.extra_flags, from);

    send_to_char(str, ch);

    sprintf(str, "(11) wear flag    :%d%s\n\r", obj->obj_flags.wear_flags, from);

    send_to_char(str, ch);

    sprintf(str, "(12) values: %d %d %d %d %d%s\n\r", obj->obj_flags.value[0],

        obj->obj_flags.value[1], obj->obj_flags.value[2],

        obj->obj_flags.value[3], obj->obj_flags.value[4], from);

    send_to_char(str, ch);

    sprintf(str, "(13) weight       :%d%s\n\r", obj->obj_flags.weight, from);

    send_to_char(str, ch);

    sprintf(str, "(14) cost         :%d%s\n\r", obj->obj_flags.cost, cost_from);

    send_to_char(str, ch);

    sprintf(str, "(15) cost per day :%d%s\n\r", obj->obj_flags.cost_per_day, from);

    send_to_char(str, ch);

    sprintf(str, "(16) level        :%d%s\n\r", obj->obj_flags.level, from);

    send_to_char(str, ch);

    sprintf(str, "(17) rarity       :%d%s\n\r", obj->obj_flags.rarity, from);

    send_to_char(str, ch);

    sprintf(str, "(18) material     :%d (%s)%s\n\r", obj->obj_flags.material,
        ((obj->obj_flags.material >= 0) && (obj->obj_flags.material < num_of_object_materials)) ? object_materials[obj->obj_flags.material] : "Unknown",
        material_from);

    send_to_char(str, ch);

    sprintf(str, "(19) Affections%s:\n\r", from);

    for (i = 0; i < MAX_OBJ_AFFECT; i++) {

        sprintf(str + strlen(str), " (%d %d)", obj->affected[i].location,

            obj->affected[i].modifier);
    }

    send_to_char(str, ch);

    sprintf(str, "\n\r(20) program       :%d\n\r", obj->obj_flags.prog_number);

    send_to_char(str, ch);

    sprintf(str, "(21) script        :%d%s\n\r", obj->obj_flags.script_number, from);

    send_to_char(str, ch);

    sprintf(str, "(22) version       :%s%s\n\r", object_version_text(obj->obj_flags.version).c_str(), from);

    send_to_char(str, ch);

    int base_rnum;
    if (obj_is_skin(*obj)) {
        base_rnum = real_object(obj->obj_flags.skin_base);
        snprintf(str, sizeof(str), "(23) Skin of: obj %d (%.40s)\n\r", obj->obj_flags.skin_base,
            base_rnum >= 0 && obj_proto[base_rnum].short_description ? obj_proto[base_rnum].short_description : "missing");
    } else if ((base_rnum = edited_base_with_skins(ch)) >= 0) {
        snprintf(str, sizeof(str), "(23) Skins: %s\n\r", skin_vnum_list(base_rnum, strlen("(23) Skins: ")).c_str());
    } else {
        strcpy(str, "(23) Skin of: none\n\r");
    }
    send_to_char(str, ch);
}

/*********--------------------------------*********/

int find_obj(FILE* f, int n)
{

    int check, i;
    char c;

    do {
        do {
            check = fscanf(f, "%c", &c);
        } while ((c != '#') && (check != EOF));

        fscanf(f, "%d", &i);
    } while ((i < n) && (check != EOF));

    if (check == EOF)
        return -1;
    else
        return i;

    return 0;
}

int get_text(FILE* f, char** line); /* exist in protos.c */

/****************-------------------------------------****************/

extern struct room_data world;

int load_object(struct char_data* ch, const char* arg)
{
    // char c;
    // char format;
    int i, tmp, tmp2, tmp3, tmp4, tmp5, number, room_number;
    char str[255], fname[80];
    struct extra_descr_data *new_descr, *last_descr;
    FILE* f;
    // char s1[50],s2[50],s3[50],s4[50];
    char* st = 0;
    room_number = ch->in_room;

    if (2 != sscanf(arg, "%s %d\n\r", str, &number)) {
        send_to_char("Choose an object by 'shape object <number>'\n\r", ch);
        return -1;
    }
    sprintf(fname, "%d", number / 100);
    sprintf(str, SHAPE_OBJ_DIR, fname);

    send_to_char(str, ch);
    f = fopen(str, "r+");
    if (f == 0) {
        send_to_char("could not open that file.\n\r", ch);
        return -1;
    }
    strcpy(SHAPE_OBJECT(ch)->f_from, str);
    SET_BIT(SHAPE_OBJECT(ch)->flags, SHAPE_FILENAME);

    sprintf(SHAPE_OBJECT(ch)->f_old, SHAPE_OBJ_BACKDIR, fname);
    if (GET_IDNUM(ch) == object_master_idnum || GET_IDNUM(ch) == object_master2_idnum)
        SHAPE_OBJECT(ch)
            ->permission
            = 1;
    else
        SHAPE_OBJECT(ch)
            ->permission
            = get_permission(number / 100, ch);

    tmp = find_obj(f, number);
    if (tmp == -1) {
        send_to_char(" no object here.\n\r", ch);
        fclose(f);
        return -1;
    }

    /* fseek(f,tmp,SEEK_SET);
  fscanf(f,"%c",&c);

  fscanf(f,"%d",&tmp);
  */
    if (tmp > number) {
        SHAPE_OBJECT(ch)
            ->number
            = number;
        SHAPE_OBJECT(ch)
            ->basenum
            = number;
        new_obj(ch);
        sprintf(str, "Could not find obj #%d, created it.\n\r", number);
        send_to_char(str, ch);
    } else {
        sprintf(str, "loading object #%d\n\r", tmp);

        send_to_char(str, ch);

        number = tmp;

        room_number = ch->in_room;

        //  SHAPE_OBJECT(ch)->object=(struct obj_data *)calloc(1,sizeof(struct obj_data));
        CREATE1(SHAPE_OBJECT(ch)->object, obj_data);
        //  bzero((char *)(SHAPE_OBJECT(ch)->object),sizeof(struct obj_data));

        SHAPE_OBJECT(ch)
            ->object->name
            = 0;
        SHAPE_OBJECT(ch)
            ->object->short_description
            = 0;
        SHAPE_OBJECT(ch)
            ->object->description
            = 0;
        SHAPE_OBJECT(ch)
            ->object->action_description
            = 0;

        SHAPE_OBJECT(ch)
            ->basenum
            = number; /*    ????????     to check    */
        SHAPE_OBJECT(ch)
            ->number
            = number;
        get_text(f, &(st));
        SHAPE_OBJECT(ch)
            ->object->name
            = st;
        st = 0;
        get_text(f, &(st));
        SHAPE_OBJECT(ch)
            ->object->short_description
            = st;
        st = 0;
        get_text(f, &(st));
        SHAPE_OBJECT(ch)
            ->object->description
            = st;
        st = 0;
        get_text(f, &(st));
        SHAPE_OBJECT(ch)
            ->object->action_description
            = st;
        st = 0;

        if (1 != fscanf(f, "%d ", &tmp))
            printf("Error reading1\n\r");
        SHAPE_OBJECT(ch)
            ->object->obj_flags.type_flag
            = tmp;
        if (1 != fscanf(f, "%d ", &tmp))
            printf("Error reading2\n\r");
        SHAPE_OBJECT(ch)
            ->object->obj_flags.extra_flags
            = tmp;
        if (1 != fscanf(f, "%d ", &tmp))
            printf("Error reading3\n\r");
        SHAPE_OBJECT(ch)
            ->object->obj_flags.wear_flags
            = tmp;
        if (1 != fscanf(f, "%d ", &tmp))
            printf("Error reading4\n\r");
        SHAPE_OBJECT(ch)
            ->object->obj_flags.value[0]
            = tmp;
        if (1 != fscanf(f, "%d ", &tmp))
            printf("Error reading5\n\r");
        SHAPE_OBJECT(ch)
            ->object->obj_flags.value[1]
            = tmp;
        if (1 != fscanf(f, "%d ", &tmp))
            printf("Error reading6\n\r");
        SHAPE_OBJECT(ch)
            ->object->obj_flags.value[2]
            = tmp;
        if (1 != fscanf(f, "%d ", &tmp))
            printf("Error reading7\n\r");
        SHAPE_OBJECT(ch)
            ->object->obj_flags.value[3]
            = tmp;
        if (1 != fscanf(f, "%d ", &tmp))
            printf("Error reading8\n\r");
        SHAPE_OBJECT(ch)
            ->object->obj_flags.value[4]
            = tmp;

        if (1 != fscanf(f, "%d ", &tmp))
            printf("Error reading9\n\r");
        SHAPE_OBJECT(ch)
            ->object->obj_flags.weight
            = tmp;
        if (1 != fscanf(f, "%d ", &tmp))
            printf("Error reading10\n\r");
        SHAPE_OBJECT(ch)
            ->object->obj_flags.cost
            = tmp;
        if (1 != fscanf(f, "%d ", &tmp))
            printf("Error reading11\n\r");
        SHAPE_OBJECT(ch)
            ->object->obj_flags.cost_per_day
            = tmp;

        int level_nums[7];
        if (read_object_level_line(f, level_nums) < 5)
            printf("Error reading12\n\r"); /* an older line: no version, no skin */
        tmp = level_nums[0];
        tmp2 = level_nums[1];
        tmp3 = level_nums[2];
        tmp4 = level_nums[3];
        tmp5 = level_nums[4];
        SHAPE_OBJECT(ch)
            ->object->obj_flags.level
            = tmp;
        SHAPE_OBJECT(ch)
            ->object->obj_flags.rarity
            = tmp2;
        SHAPE_OBJECT(ch)
            ->object->obj_flags.material
            = tmp3;
        SHAPE_OBJECT(ch)
            ->object->obj_flags.script_number
            = tmp4;
        SHAPE_OBJECT(ch)
            ->object->obj_flags.version
            = tmp5;
        SHAPE_OBJECT(ch)->object->obj_flags.skin_base = level_nums[5];
        SHAPE_OBJECT(ch)->object->obj_flags.skin_own = level_nums[6];

        SHAPE_OBJECT(ch)
            ->object->ex_description
            = 0;
        /* Keep file order, so a save does not reverse the extra descriptions
         * (and /6 /7 /8 keep acting on the same "last" one). */
        last_descr = 0;
        while (fscanf(f, " %s \n\r", str), str[0] == 'E') {
            CREATE(new_descr, struct extra_descr_data, 1);
            get_text(f, &(new_descr->keyword));
            get_text(f, &(new_descr->description));
            new_descr->next = 0;
            if (last_descr)
                last_descr->next = new_descr;
            else
                (SHAPE_OBJECT(ch)->object)->ex_description = new_descr;
            last_descr = new_descr;
        }

        for (i = 0; (i < MAX_OBJ_AFFECT) && (str[0] == 'A'); i++) {
            fscanf(f, " %d %d ", &tmp, &tmp2);
            SHAPE_OBJECT(ch)
                ->object->affected[i]
                .location
                = tmp;
            SHAPE_OBJECT(ch)
                ->object->affected[i]
                .modifier
                = tmp2;
            fscanf(f, " %s \n\r", str);
        }

        for (; (i < MAX_OBJ_AFFECT); i++) {
            SHAPE_OBJECT(ch)
                ->object->affected[i]
                .location
                = APPLY_NONE;
            SHAPE_OBJECT(ch)
                ->object->affected[i]
                .modifier
                = 0;
        }

        /* A skin's stats in the file are only a fallback: show the base's current ones, as save
         * and implement use. */
        struct obj_data* loaded = SHAPE_OBJECT(ch)->object;
        if (obj_is_skin(*loaded) && skin_base_problem(number, loaded->obj_flags.skin_base).empty())
            copy_skin_stats_from_base(*loaded, obj_proto[real_object(loaded->obj_flags.skin_base)]);

        /*







   SHAPE_OBJECT(ch)->object->in_room=room_number;

  SHAPE_OBJECT(ch)->object->next_content=world[room_number].contents;

  world[room_number].contents=SHAPE_OBJECT(ch)->object;



  SET_BIT(SHAPE_OBJECT(ch)->flags,SHAPE_OBJECT_LOADED);

  ch->specials.prompt_value=number;

  SHAPE_OBJECT(ch)->procedure=SHAPE_EDIT;





   CREATE1(SHAPE_OBJECT(ch)->object,struct obj_data);



  SHAPE_OBJECT(ch)->object->item_number=-1;

     for(i=0;i<MAX_OBJ_AFFECT;i++){

      SHAPE_OBJECT(ch)->object->affected[i].location=APPLY_NONE;

      SHAPE_OBJECT(ch)->object->affected[i].modifier=0;

    }

*/

        SHAPE_OBJECT(ch)
            ->object->carried_by
            = 0;

        SHAPE_OBJECT(ch)
            ->object->in_obj
            = 0;

        SHAPE_OBJECT(ch)
            ->object->contains
            = 0;

        SHAPE_OBJECT(ch)
            ->object->item_number
            = -1;
    }

    SET_BIT(SHAPE_OBJECT(ch)->flags, SHAPE_OBJECT_LOADED);

    SHAPE_OBJECT(ch)
        ->procedure
        = SHAPE_EDIT;

    send_to_char("Causing the eternal order to snicker, you load an object.\n\r",

        ch);

    ch->specials.prompt_value = number;

    fclose(f);

    return number;
}

/*****************----------------------------------******************/

// #define min(a,b) (((a)<(b))? (a):(b))

int append_object(struct char_data* ch, char* arg);

int replace_object(struct char_data* ch, char* arg)
{

    /* this procedure is used for deleting objects, too */

    char str[255];

    char *f_from, *f_old;

    char c;

    int i, check, num, oldnum;

    FILE* f1;

    FILE* f2;

    /*  if(3!=sscanf(arg,"%s %s %s",str,f_from,f_old)){

    send_to_char("format is <file_from> <file_to>\n\r",ch);

    return -1;

  }

*/

    if (!IS_SET(SHAPE_OBJECT(ch)->flags, SHAPE_FILENAME)) {

        send_to_char("How strange... you have no file defined to write to.\n\r",

            ch);

        return -1;
    }

    f_from = SHAPE_OBJECT(ch)->f_from;

    f_old = SHAPE_OBJECT(ch)->f_old;

    if (!IS_SET(SHAPE_OBJECT(ch)->flags, SHAPE_OBJECT_LOADED)) {

        send_to_char("you have no object to save...\n\r", ch);

        return -1;
    }

    if (!strcmp(f_from, f_old)) {

        send_to_char("better make source and target files different\n\r", ch);

        return -1;
    }

    if (SHAPE_OBJECT(ch)->permission == 0) {
        send_to_char("You're not authorized to do this.\n\r", ch);
        return -1;
    }

    num = SHAPE_OBJECT(ch)->number;

    if (num == -1) {

        send_to_char("you created it afresh, remember? Adding.\n\r", ch);

        append_object(ch, arg);

        return -1;
    }

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

    f2 = fopen(f_from, "w+");

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

        } while ((c != '#') && (check != EOF));

        fscanf(f1, "%d", &i);

        if (i < num)
            fprintf(f2, "#%-d", i);

        else
            oldnum = i;

    } while ((i < num) && (check != EOF));

    if (check == EOF) {
        sprintf(str, "no object #%d in this file\n\r", num);

        send_to_char(str, ch);

        fclose(f1);
        fclose(f2);

        return -1;
    }

    if (i == num) {
        do {

            i = fscanf(f1, "%c", &c);

        } while ((c != '#') && (i != EOF));

        if (c == '#')
            fscanf(f1, "%d", &oldnum);
    }

    if (!IS_SET(SHAPE_OBJECT(ch)->flags, SHAPE_DELETE_ACTIVE)) {

        /* A skin's stats in the file are only a fallback: keep them current with its base. */
        struct obj_data* edited = SHAPE_OBJECT(ch)->object;
        if (obj_is_skin(*edited) && skin_base_problem(num, edited->obj_flags.skin_base).empty())
            copy_skin_stats_from_base(*edited, obj_proto[real_object(edited->obj_flags.skin_base)]);

        write_object(f2, SHAPE_OBJECT(ch)->object, num);

        REMOVE_BIT(SHAPE_OBJECT(ch)->flags, SHAPE_DELETE_ACTIVE);
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

int append_object(struct char_data* ch, char* arg)
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

    if (SHAPE_OBJECT(ch)->permission == 0) {
        send_to_char("You're not authorized to do this.\n\r", ch);
        return -1;
    }

    if (SHAPE_OBJECT(ch)->number != -1) {

        send_to_char("Object already added to database. Saving.\n\r", ch);

        /* Went on to append a second copy under a new vnum. */
        return replace_object(ch, arg);
    }

    if (2 != sscanf(arg, "%s %s", str, fname)) {

        if (!IS_SET(SHAPE_OBJECT(ch)->flags, SHAPE_FILENAME)) {

            send_to_char("No file defined to write into. Use 'add <filename>'\n\r",

                ch);

            return -1;
        }

    }

    else {

        sprintf(SHAPE_OBJECT(ch)->f_from, SHAPE_MOB_DIR, fname);

        sprintf(SHAPE_OBJECT(ch)->f_old, SHAPE_MOB_BACKDIR, fname);

        SET_BIT(SHAPE_OBJECT(ch)->flags, SHAPE_FILENAME);
    }

    if (!IS_SET(SHAPE_OBJECT(ch)->flags, SHAPE_OBJECT_LOADED)) {

        send_to_char("you have no object to save...\n\r", ch);

        return -1;
    }

    f_from = SHAPE_OBJECT(ch)->f_from;

    f_old = SHAPE_OBJECT(ch)->f_old;

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
        if (check != EOF)
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

    if (!IS_SET(SHAPE_OBJECT(ch)->flags, SHAPE_OBJECT_LOADED))
        i = atoi(fname) * 100;
    else {
        for (c = 0; (f_from[c] < '0' || f_from[c] > '9') && f_from[c]; c++)
            ;
        sscanf(f_from + c, "%d", &i);
        i = i * 100;
    }

    do {

        do {

            check = fscanf(f1, "%c", &c);
            if (check != EOF)
                fprintf(f2, "%c", c);

        } while ((c != '#') && (check != EOF));

        i1 = i;

        if (check != EOF)
            fscanf(f1, "%d", &i);

        if (i != 99999)
            fprintf(f2, "%-d", i);

    } while ((i != 99999) && (check != EOF));

    if (check == EOF) {
        send_to_char("no final record  in source file\n\r", ch);
    }

    fseek(f2, -1, SEEK_CUR);

    SHAPE_OBJECT(ch)
        ->number
        = i1 + 1;

    write_object(f2, SHAPE_OBJECT(ch)->object, i1 + 1);

    fprintf(f2, "#99999\n\r");

    fclose(f1);
    fclose(f2);

    send_to_char("You added a new object to database.\n\r", ch);

    return i1;
}

void free_object(struct char_data* ch)
{
    struct extra_descr_data *tmp, *tmp2;

    if (!SHAPE_OBJECT(ch))
        return;
    if (IS_SET(SHAPE_OBJECT(ch)->flags, SHAPE_OBJECT_LOADED)) {
        tmp = SHAPE_OBJECT(ch)->object->ex_description;
        while (tmp) {
            RELEASE(tmp->keyword);
            RELEASE(tmp->description);
            tmp2 = tmp->next;
            RELEASE(tmp);
            tmp = tmp2;
        }
        /* The session's object holds its own copy of every text field: load_object reads each
         * from the file, new_obj duplicates them, and an edit replaces one with a new copy. */
        obj_data* const session_object = SHAPE_OBJECT(ch)->object;
        RELEASE(session_object->name);
        RELEASE(session_object->short_description);
        RELEASE(session_object->description);
        RELEASE(session_object->action_description);
        RELEASE(SHAPE_OBJECT(ch)->object);
        SHAPE_OBJECT(ch)
            ->object
            = NULL;
    }
    //    shape_standup(ch,SHAPE_OBJECT(ch)->position);
    REMOVE_BIT(SHAPE_OBJECT(ch)->flags, SHAPE_OBJECT_LOADED);
    ch->specials.prompt_value = 0;
    //  RELEASE(SHAPE_OBJECT(ch));
    RELEASE(ch->temp);
    ch->temp = 0;
    REMOVE_BIT(PRF_FLAGS(ch), PRF_DISPTEXT);
    if (GET_POS(ch) <= POSITION_SHAPING)
        GET_POS(ch) = POSITION_STANDING;
}

/****************************** main() ********************************/

void extra_coms_obj(struct char_data* ch, char* argument)
{

    //  extern struct room_data world;
    int comm_key, room_number, i;
    char str[255], str2[50];
    room_number = ch->in_room;
    //  printf("Entering extra_coms_obj, proc=%d, arg=%s.\n\r",SHAPE_OBJECT(ch)->procedure,argument);
    if (SHAPE_OBJECT(ch)->procedure == SHAPE_EDIT) {

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
            if (!strncmp(str, "free", strlen(str))) {
                comm_key = SHAPE_FREE;
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
            if (!strncmp(str, "implement", strlen(str))) {
                comm_key = SHAPE_IMPLEMENT;
                break;
            }

            send_to_char("Possible commands are:\n\r", ch);
            //      send_to_char("load   <object #>;\n\r",ch);
            //      send_to_char("add    <zone #>;\n\r",ch);
            send_to_char("save  - to save changes to the disk database;\n\r", ch);
            send_to_char("implement - applies changes to the game, leaving disk intact;\n\r", ch);
            send_to_char("done - to save your job, implement it and stop shaping.;\n\r", ch);
            send_to_char("free - to stop shaping.;\n\r", ch);

            return;
        } while (0);
        /*    SHAPE_OBJECT(ch)->procedure=comm_key;*/
    } else
        comm_key = SHAPE_OBJECT(ch)->procedure;
    switch (comm_key) {
    case SHAPE_FREE:
        free_object(ch);
        send_to_char("You released an object and stopped shaping.\n\r", ch);
        break;
    case SHAPE_CREATE:
        /* Picked last vnum + 1, which could run past the zone's range. */
        shape_disabled(ch, "/", argument);
        break;
        if (str2[0] == 0) {
            send_to_char("Choose zone of object by 'new <zone_number>'.\n\r", ch);
            free_object(ch);
            break;
        }
        i = atoi(str2);
        if (i <= 0 || i >= MAX_ZONES) {
            send_to_char("Weird zone number. Aborted.\n\r", ch);
            free_object(ch);
            break;
        }
        SHAPE_OBJECT(ch)
            ->permission
            = get_permission(i, ch);

        sprintf(SHAPE_OBJECT(ch)->f_from, SHAPE_OBJ_DIR, str2);
        sprintf(SHAPE_OBJECT(ch)->f_old, SHAPE_OBJ_BACKDIR, str2);
        SET_BIT(SHAPE_OBJECT(ch)->flags, SHAPE_FILENAME);
        new_obj(ch);
        SET_BIT(SHAPE_OBJECT(ch)->flags, SHAPE_OBJECT_LOADED);
        SHAPE_OBJECT(ch)
            ->procedure
            = SHAPE_EDIT;
        send_to_char("OK. You have created a template. Do /save to assign a number to your object\n\r", ch);
        SHAPE_OBJECT(ch)
            ->editflag
            = 49;
        shape_center_obj(ch, "");
        break;

    case SHAPE_LOAD:
        if (!IS_SET(SHAPE_OBJECT(ch)->flags, SHAPE_OBJECT_LOADED)) {
            if (load_object(ch, argument) < 0) {
                free_object(ch);
                break;
            }
        } else
            send_to_char("You already have an object loaded.\n\r", ch);
        break;
    case SHAPE_SAVE:
        replace_object(ch, argument);
        break;
    case SHAPE_ADD:
        /* 'add <file>' wrote the object into the mob directory. */
        if (str2[0]) {
            shape_disabled(ch, "/", argument);
            break;
        }
        append_object(ch, argument);
        break;
    case SHAPE_DELETE:
        /* Rewrote the zone file to drop the object; a never-saved object got
         * saved instead, and the editor was left stuck in shaping. */
        if (SHAPE_OBJECT(ch)->procedure != SHAPE_DELETE) {
            shape_disabled(ch, "/", argument);
            break;
        }
        if (SHAPE_OBJECT(ch)->procedure != SHAPE_DELETE) {
            send_to_char("You are about to remove this object from database.\n\r Are you sure? (type 'yes' to confirm):\n\r", ch);
            SHAPE_OBJECT(ch)
                ->procedure
                = SHAPE_DELETE;
            SHAPE_OBJECT(ch)
                ->position
                = shape_standup(ch, POSITION_SHAPING);
            //      printf("shapeobj, set proc to %d\n",SHAPE_OBJECT(ch)->procedure);
            break;
        }
        if (!strcmp("yes", argument)) {
            SET_BIT(SHAPE_OBJECT(ch)->flags, SHAPE_DELETE_ACTIVE);
            replace_object(ch, argument);
            send_to_char("You still continue to shape it, though - take your chance.\n\r", ch);
        } else
            send_to_char("Deletion cancelled.\n\r", ch);
        REMOVE_BIT(SHAPE_OBJECT(ch)->flags, SHAPE_DELETE_ACTIVE);
        SHAPE_OBJECT(ch)
            ->basenum
            = -1;
        SHAPE_OBJECT(ch)
            ->procedure
            = SHAPE_EDIT;
        break;

    case SHAPE_IMPLEMENT:
        implement_object(ch);
        SHAPE_OBJECT(ch)
            ->procedure
            = SHAPE_EDIT;
        break;
    case SHAPE_DONE:
        /* A failed save must not throw the edits away. */
        if (replace_object(ch, argument) < 0) {
            send_to_char("Not saved - still shaping. Fix the problem and /done again,\n\r"
                         "or /free to discard.\n\r",
                ch);
            break;
        }
        implement_object(ch);
        extra_coms_obj(ch, "free");
        //    SHAPE_OBJECT(ch)->procedure=SHAPE_EDIT;
        break;
    }
    return;
}
