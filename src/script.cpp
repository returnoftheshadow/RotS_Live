/**********************************************************
 * File: script.cc                                         *
 *                                                         *
 *                 Return of the Shadow                    *
 *                   script functions                      *
 *                                                         *
 *                       script.cc                         *
 *                                                         *
 * NB                                                      *
 * scripts return 1 if program should continue as normal,  *
 * and 0 if not (eg on_die 0 == do not kill char)          *
 *                                                         *
 * Characters who are on the waiting list cannot run       *
 *   scripts                                               *
 **********************************************************/

// returns 1 if program should continue as normal, 0 if not (eg on_die 0 == do not kill char)

#include "platdef.h"
#include <ctype.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "comm.h"
#include "db.h"
#include "handler.h"
#include "interpre.h"
#include "limits.h"
#include "pkill.h"
#include "protos.h"
#include "script.h"
#include "structs.h"
#include "utils.h"
#include "zone.h"

// External declarations
ACMD(do_say);
ACMD(do_hit);
ACMD(do_flee);
ACMD(do_emote);
ACMD(do_gen_com);
ACMD(do_wear);
ACMD(do_action);
extern struct script_head* script_table;
extern struct room_data world;
extern int top_of_script_table;
int set_exit_state(struct room_data* room, int dir, int newstate);
extern int rev_dir[];
extern struct char_data* waiting_list;
extern void raw_kill(char_data* ch, char_data* killer, int attacktype);
void perform_give(struct char_data* ch, struct char_data* vict, struct obj_data* obj);
int perform_drop(struct char_data* ch, struct obj_data* obj, sh_int RDR);
void perform_remove(struct char_data* ch, int pos);
int find_eq_pos(struct char_data* ch, struct obj_data* obj, char* arg);
void perform_wear(struct char_data* ch, struct obj_data* obj, int where, bool wearall = false);
int find_action(char* arg);
extern struct index_data* obj_index;

// Internal declarations
int trigger_room_enter(room_data* room, char_data* ch);
int trigger_room_event(int trigger_type, room_data* room, char_data* ch);
int trigger_char_enter(char_data* ch, char_data* vict, room_data* room);
script_data* char_has_script(int* index, char_data* ch, int script_type);
int run_char_script(char_data* ch, void* sub1, void* sub2, script_data* position);
int script_char_do_say(char_data* ch, char* line);
int trigger_char_die(char_data* ch);
int trigger_char_receive(char_data* ch1, char_data* ch2, obj_data* ob1);
int trigger_before_char_enter(char_data* ch, char_data* vict, room_data* room);
int trigger_object_event(int trigger_type, obj_data* obj, char_data* ch);
void clear_script_info(info_script* inf);
int trigger_char_hear(char_data* ch, char_data* speaking, char* text);
int trigger_char_damage(char_data* vict, char_data* ch);
int trigger_object_damage(obj_data* obj, char_data* vict, char_data* ch);

// Returns the index position of a script in the script_table when supplied with a vnum
// -1 == script not found (0 is a valid position in the script_table)

int find_script_by_number(const int number)
{
    int i;

    for (i = 0; i <= top_of_script_table; i++)
        if ((script_table + i)->number == number)
            break;

    if (i > top_of_script_table)
        return -1;
    else
        return i;
}

void initialise_script_info_char(char_data* ch, int index)
{

    if (!(ch->specials.script_info))
        CREATE1(ch->specials.script_info, info_script);
    clear_script_info(ch->specials.script_info);

    /* -1 means ignore the index */
    if (index != -1)
        ch->specials.script_info->index = index;
}

void initialise_script_info_obj(obj_data* obj, int index)
{

    if (!(obj->obj_flags.script_info))
        CREATE1(obj->obj_flags.script_info, info_script);
    clear_script_info(obj->obj_flags.script_info);

    /* -1 means ignore the index */
    if (index != -1)
        obj->obj_flags.script_info->index = index;
}

void clear_script_info(info_script* inf)
{
    inf->ch[0] = 0;
    inf->ch[1] = 0;
    inf->ch[2] = 0;
    inf->rm[0] = 0;
    inf->rm[1] = 0;
    inf->rm[2] = 0;
    inf->ob[0] = 0;
    inf->ob[1] = 0;
    inf->ob[2] = 0;
    if (inf->str[0] != NULL && inf->str_dynamic[0] != 0)
        RELEASE(inf->str[0]);
    if (inf->str[1] != NULL && inf->str_dynamic[1] != 1)
        RELEASE(inf->str[1]);
    if (inf->str[2] != NULL && inf->str_dynamic[2] != 1)
        RELEASE(inf->str[2]);
}

char**
get_text_param_writable(int param, struct info_script* info)
{
    switch (param) {
    case SCRIPT_PARAM_STR1:
        return &info->str[0];
    case SCRIPT_PARAM_STR2:
        return &info->str[1];
    case SCRIPT_PARAM_STR3:
        return &info->str[2];
    case SCRIPT_PARAM_OB1_NAME:
        if (info->ob[0])
            return &info->ob[0]->short_description;
        else
            return NULL;
    case SCRIPT_PARAM_OB2_NAME:
        if (info->ob[1])
            return &info->ob[1]->short_description;
        else
            return NULL;
    case SCRIPT_PARAM_OB3_NAME:
        if (info->ob[2])
            return &info->ob[2]->short_description;
        else
            return NULL;
    default:
        vmudlog(BRF, "Script #%d attempted to write to string "
                     "variable %s.",
            script_table[info->index].number);
        return NULL;
    }
}

// Returns a pointer to a text field/parameter

char* get_text_param(int param, info_script* info)
{
    switch (param) {

    case SCRIPT_PARAM_STR1:
        return info->str[0];
    case SCRIPT_PARAM_STR2:
        return info->str[1];
    case SCRIPT_PARAM_STR3:
        return info->str[2];
    case SCRIPT_PARAM_CH1_NAME:
        if (info->ch[0])
            return GET_NAME(info->ch[0]);
        else
            return 0;
    case SCRIPT_PARAM_CH2_NAME:
        if (info->ch[1])
            return GET_NAME(info->ch[1]);
        else
            return 0;
    case SCRIPT_PARAM_CH3_NAME:
        if (info->ch[2])
            return GET_NAME(info->ch[2]);
        else
            return 0;
    case SCRIPT_PARAM_OB1_NAME:
        if (info->ob[0])
            return info->ob[0]->short_description;
        else
            return 0;
    case SCRIPT_PARAM_OB2_NAME:
        if (info->ob[1])
            return info->ob[1]->short_description;
        else
            return 0;
    case SCRIPT_PARAM_OB3_NAME:
        if (info->ob[2])
            return info->ob[2]->short_description;
        else
            return 0;
    case SCRIPT_PARAM_RM1_NAME:
        if (info->rm[0])
            return info->rm[0]->name;
        else
            return 0;
    case SCRIPT_PARAM_RM2_NAME:
        if (info->rm[1])
            return info->rm[1]->name;
        else
            return 0;
    case SCRIPT_PARAM_RM3_NAME:
        if (info->rm[2])
            return info->rm[2]->name;
        else
            return 0;

    default:
        return 0;
    }
}

/*
 * Return 0 if the script is not allowed to write to the
 * integer param.  For writable integer params, return 1.
 */
int is_int_param_writable(int param)
{
    /* Most chx, rmx and obx variables are non-writable */
    switch (param) {
    case SCRIPT_PARAM_CH1_LEVEL:
    case SCRIPT_PARAM_CH2_LEVEL:
    case SCRIPT_PARAM_CH3_LEVEL:
    case SCRIPT_PARAM_CH1_RANK:
    case SCRIPT_PARAM_CH2_RANK:
    case SCRIPT_PARAM_CH3_RANK:
    case SCRIPT_PARAM_CH1_RACE:
    case SCRIPT_PARAM_CH2_RACE:
    case SCRIPT_PARAM_CH3_RACE:
    case SCRIPT_PARAM_CH1_EXP:
    case SCRIPT_PARAM_CH2_EXP:
    case SCRIPT_PARAM_CH3_EXP:
    case SCRIPT_PARAM_OB1_VNUM:
    case SCRIPT_PARAM_OB2_VNUM:
    case SCRIPT_PARAM_OB3_VNUM:
        return 0;
    default:
        return 1;
    }
}

/*
 * Set dest to val if dest is a valid destination.  dest
 * should refer to one of the settable variables in a script,
 * such as chx.hit.
 */
void set_int_value(struct info_script* info, int param, int val)
{
    int* x;
    extern char* get_param_text(int); /* shapescript.cc */
    int* get_int_param(int, struct info_script*);

    x = get_int_param(param, info);
    if (x == NULL)
        return;

    if (is_int_param_writable(param))
        *x = val;
    else
        vmudlog(BRF, "Script #%d tried to write to unwritable "
                     "integer parameter %s",
            script_table[info->index].number,
            get_param_text(param));
}

void op_error(struct info_script* info, int command)
{
    vmudlog(BRF, "Unrecognized binary integer operation %d in script #%d",
        command, script_table[info->index].number);
    vmudlog(BRF, "Please notify an implementor.");
}

/*
 * Return the value of the binary operation denoted by
 * command on a and b.  I.e., if command is MULT, then
 * return a * b.
 */
int int_binary_op(struct info_script* info, int command, int* a, int* b)
{
    switch (command) {
    case SCRIPT_SET_INT_SUM:
        return *a + *b;
    case SCRIPT_SET_INT_SUB:
        return *a - *b;
    case SCRIPT_SET_INT_MULT:
        return *a * *b;
    case SCRIPT_SET_INT_DIV:
        if (*b != 0)
            return *a / *b;
        else
            vmudlog(BRF, "Script #%d tried to divide by 0.",
                script_table[info->index].number);
    case SCRIPT_SET_INT_RANDOM:
        return number(*a, *b);
    default:
        op_error(info, command);
    }

    return 0;
}

int int_unary_op(struct info_script* info, int command, int* a)
{
    switch (command) {
    case SCRIPT_SET_INT_VALUE:
        return *a;
    default:
        op_error(info, command);
    }

    return 0;
}

int int_0ary_op(struct info_script* info, int command)
{
    switch (command) {
    case SCRIPT_SET_INT_WAR_STATUS:
        if (pkill_get_good_fame() > pkill_get_evil_fame())
            return 1;
        else if (pkill_get_evil_fame() > pkill_get_good_fame())
            return -1;
        else
            return 0;
    default:
        op_error(info, command);
    }

    return 0;
}

// Returns a pointer to an integer field/parameter

int* get_int_param(int param, info_script* info)
{
    /* XXX: Dummy so that we can return something.  VERY thread unsafe */
    static int r;

    if (param == 0) {
        vmudlog(BRF, "Invalid integer parameter in script #%d",
            script_table[info->index].number);
        return NULL;
    }

    switch (param) {
    case SCRIPT_PARAM_CH1_HIT:
        if (info->ch[0])
            return &GET_HIT(info->ch[0]);
        else
            return 0;
    case SCRIPT_PARAM_CH2_HIT:
        if (info->ch[1])
            return &GET_HIT(info->ch[1]);
        else
            return 0;
    case SCRIPT_PARAM_CH3_HIT:
        if (info->ch[2])
            return &GET_HIT(info->ch[2]);
        else
            return 0;
    case SCRIPT_PARAM_CH1_LEVEL:
        if (info->ch[0])
            return &GET_LEVEL(info->ch[0]);
        else
            return 0;
    case SCRIPT_PARAM_CH2_LEVEL:
        if (info->ch[1])
            return &GET_LEVEL(info->ch[1]);
        else
            return 0;
    case SCRIPT_PARAM_CH3_LEVEL:
        if (info->ch[2])
            return &GET_LEVEL(info->ch[2]);
        else
            return 0;
    case SCRIPT_PARAM_CH1_RACE:
        if (info->ch[0])
            return &GET_RACE(info->ch[0]);
        else
            return 0;
    case SCRIPT_PARAM_CH2_RACE:
        if (info->ch[1])
            return &GET_RACE(info->ch[1]);
        else
            return 0;
    case SCRIPT_PARAM_CH3_RACE:
        if (info->ch[2])
            return &GET_RACE(info->ch[2]);
        else
            return 0;
    case SCRIPT_PARAM_CH1_EXP:
        if (info->ch[0])
            return &GET_EXP(info->ch[0]);
        else
            return 0;
    case SCRIPT_PARAM_CH2_EXP:
        if (info->ch[1])
            return &GET_EXP(info->ch[1]);
        else
            return 0;
    case SCRIPT_PARAM_CH3_EXP:
        if (info->ch[2])
            return &GET_EXP(info->ch[2]);
        else
            return 0;
    case SCRIPT_PARAM_CH1_RANK:
        if (info->ch[0])
            r = pkill_get_rank_by_character(info->ch[0], false);
        else
            r = PKILL_UNRANKED;

        if (r != PKILL_UNRANKED)
            ++r;
        return &r;
    case SCRIPT_PARAM_CH2_RANK:
        if (info->ch[1])
            r = pkill_get_rank_by_character(info->ch[1], false);
        else
            r = PKILL_UNRANKED;

        if (r != PKILL_UNRANKED)
            ++r;
        return &r;
    case SCRIPT_PARAM_CH3_RANK:
        if (info->ch[2])
            r = pkill_get_rank_by_character(info->ch[2], false);
        else
            r = PKILL_UNRANKED;

        if (r != PKILL_UNRANKED)
            ++r;
        return &r;

    case SCRIPT_PARAM_INT1:
        return &info->ints[0];
    case SCRIPT_PARAM_INT2:
        return &info->ints[1];
    case SCRIPT_PARAM_INT3:
        return &info->ints[2];

    case SCRIPT_PARAM_OB1_VNUM:
        if (info->ob[0])
            return ((info->ob[0]->item_number >= 0) ? &obj_index[info->ob[0]->item_number].virt : &obj_index[0].virt);
        else
            return 0;
    case SCRIPT_PARAM_OB2_VNUM:
        if (info->ob[1])
            return ((info->ob[1]->item_number >= 0) ? &obj_index[info->ob[1]->item_number].virt : &obj_index[0].virt);
        else
            return 0;
    case SCRIPT_PARAM_OB3_VNUM:
        if (info->ob[2])
            return ((info->ob[2]->item_number >= 0) ? &obj_index[info->ob[2]->item_number].virt : &obj_index[0].virt);
        else
            return 0;
    default:
        // We shouldn't get here.
        return NULL;
    }

    // We shouldn't get here.
    return NULL;
}

void assign_obj_param(int param, info_script* info, obj_data* obj)
{
    switch (param) {
    case SCRIPT_PARAM_OB1:
        info->ob[0] = obj;
        break;
    case SCRIPT_PARAM_OB2:
        info->ob[1] = obj;
        break;
    case SCRIPT_PARAM_OB3:
        info->ob[2] = obj;
    }
}

void assign_char_param(int param, info_script* info, char_data* ch)
{
    switch (param) {
    case SCRIPT_PARAM_CH1:
        info->ch[0] = ch;
        break;
    case SCRIPT_PARAM_CH2:
        info->ch[1] = ch;
        break;
    case SCRIPT_PARAM_CH3:
        info->ch[2] = ch;
        break;
    }
}

char_data* get_char_param(int param, info_script* info)
{
    switch (param) {
    case SCRIPT_PARAM_CH1:
        return info->ch[0];
    case SCRIPT_PARAM_CH2:
        return info->ch[1];
    case SCRIPT_PARAM_CH3:
        return info->ch[2];
    default:
        return 0;
    }
}

room_data* get_room_param(int param, info_script* info)
{
    switch (param) {
    case SCRIPT_PARAM_RM1:
        return info->rm[0];
    case SCRIPT_PARAM_RM2:
        return info->rm[1];
    case SCRIPT_PARAM_RM3:
        return info->rm[2];

    case SCRIPT_PARAM_CH1_ROOM:
        if (info->ch[0])
            return &world[info->ch[0]->in_room];
        else
            return 0;
    case SCRIPT_PARAM_CH2_ROOM:
        if (info->ch[1])
            return &world[info->ch[1]->in_room];
        else
            return 0;
    case SCRIPT_PARAM_CH3_ROOM:
        if (info->ch[2])
            return &world[info->ch[2]->in_room];
        else
            return 0;
    default:
        return 0;
    }
}

obj_data* get_obj_param(int param, info_script* info)
{
    switch (param) {
    case SCRIPT_PARAM_OB1:
        return info->ob[0];
    case SCRIPT_PARAM_OB2:
        return info->ob[1];
    case SCRIPT_PARAM_OB3:
        return info->ob[2];
    default:
        // We shouldn't reach here.
        return NULL;
    }

    // We shouldn't reach here.
    return NULL;
}

// Checks if a character/object/room has a script of script_type returns a pointer to the command if so, 0 if not

script_data* char_has_script(int* return_index, const int script_no, const int script_type)
{
    script_data* tmpscript = 0;
    int index;

    *return_index = 0;

    if (!(script_no))
        return tmpscript;
    index = find_script_by_number(script_no);
    if (index == -1)
        return tmpscript;
    for (tmpscript = script_table[index].script; tmpscript; tmpscript = tmpscript->next)
        if (tmpscript->command_type == script_type)
            break;

    *return_index = index;

    return tmpscript;
}

// Central trigger function - all triggers call here first
// returns 1 if program should continue as normal, 0 if not (eg on_die 0 == do not kill char)

int call_trigger(int trigger_type, void* subject, void* subject2, void* subject3)
{
    int return_value = 1;
    obj_data* tmpobj;

    switch (trigger_type) {

    case ON_BEFORE_ENTER:
        return_value = trigger_room_event(ON_BEFORE_ENTER, (room_data*)subject, (char_data*)subject2);
        break;

    case ON_DAMAGE:
        return_value = trigger_char_damage((char_data*)subject, (char_data*)subject2);
        if (return_value)
            if ((tmpobj = ((char_data*)subject2)->equipment[WIELD]))
                return_value = trigger_object_damage(tmpobj, (char_data*)subject, (char_data*)subject2);
        break;

    case ON_DIE:
        return_value = trigger_char_die((char_data*)subject); // we don't do anything with killer yet
        break;

    case ON_DRINK:
        return_value = trigger_object_event(ON_DRINK, (obj_data*)subject, (char_data*)subject2);
        break;

    case ON_EAT:
        return_value = trigger_object_event(ON_EAT, (obj_data*)subject, (char_data*)subject2);
        break;

    case ON_ENTER:
        return_value = trigger_room_event(ON_ENTER, (room_data*)subject, (char_data*)subject2); // This will trigger room, characters, objects
        break;

    case ON_EXAMINE_OBJECT:
        return_value = trigger_object_event(ON_EXAMINE_OBJECT, (obj_data*)subject, (char_data*)subject2);
        break;

    case ON_HEAR_SAY:
        return_value = trigger_char_hear((char_data*)subject, (char_data*)subject2, (char*)subject3);
        break;

    case ON_HEAR_YELL:
        return_value = trigger_char_hear((char_data*)subject, (char_data*)subject2, (char*)subject3);
        break;

    case ON_PULL:
        return_value = trigger_object_event(ON_PULL, (obj_data*)subject, (char_data*)subject2);
        break;

    case ON_RECEIVE:
        return_value = trigger_char_receive((char_data*)subject, (char_data*)subject2, (obj_data*)subject3);
        break;

    case ON_WEAR:
        return_value = trigger_object_event(ON_WEAR, (obj_data*)subject, (char_data*)subject2);
        break;

    default:
        log("Error in call_trigger: unknown trigger_type");
        return 1;
    } // switch (trigger_type)
    return return_value;
}

int trigger_room_event(int trigger_type, room_data* room, char_data* ch)
{
    int return_value = 1;
    char_data* tmpch;
    obj_data* tmpobj;

    switch (trigger_type) {

    case ON_BEFORE_ENTER:

        for (tmpch = room->people; tmpch && return_value; tmpch = tmpch->next_in_room)
            if (tmpch != ch)
                return_value = trigger_before_char_enter(tmpch, ch, room);

        break;

    case ON_ENTER:
        if (!(return_value = trigger_room_enter(room, ch)))
            return_value = 0;
        for (tmpch = room->people; tmpch && return_value; tmpch = tmpch->next_in_room)
            if (tmpch != ch)
                return_value = trigger_char_enter(tmpch, ch, room);
        for (tmpobj = room->contents; tmpobj && return_value; tmpobj = tmpobj->next_content)
            return_value = trigger_object_event(ON_ENTER, tmpobj, ch);
        break;

    default:
        log("Error in trigger_room_event: unknown trigger_type");
        return_value = 1;

    } // switch (trigger_type)
    return return_value;
}

script_data* get_next_command(script_data* curr)
{

    curr = curr->next;
    for (; (curr) && ((curr->command_type != SCRIPT_END) && (curr->command_type != SCRIPT_END_ELSE_BEGIN));
         curr = curr->next)
        if (curr->command_type == SCRIPT_BEGIN)
            curr = get_next_command(curr);

    if (curr)
        return curr->next;
    else
        return 0;
}

int run_script(struct info_script* info, struct script_data* position)
{
    char output[500];
    int return_value = 1;
    int exit = FALSE;
    int x;
    int* ptrint;
    int* ptrint2;
    int* ptrint3;
    script_data* curr;
    char** wtxt = NULL;
    char* txt1 = 0;
    char* txt2 = 0;
    char* txt3 = 0;
    char_data* tmpch = 0;
    room_data* tmprm = NULL;
    room_data* tmprm2;
    char_data* tmpch2;
    struct waiting_type tmpwtl;
    obj_data* tmpobj = 0;
    int tobjcnt;
    int tmpint, tmpint2;
    struct follow_type *k, *next_fol;

    curr = position;
    if (!position)
        exit = TRUE;
    while (!exit) {
        switch (curr->command_type) {

        case SCRIPT_ABORT:
            exit = TRUE;
            break;

        case SCRIPT_ASSIGN_EQ:
            if (curr->param[0] && curr->param[1]) {
                tmpch = get_char_param(curr->param[0], info);
                if (tmpch)
                    tmpobj = tmpch->equipment[curr->param[2]];
                ptrint = get_int_param(curr->param[3], info);

                // just in case somone puts something like obj1.vnum here...
                if (!(ptrint == &info->ints[0] || ptrint == &info->ints[1] || ptrint == &info->ints[2]))
                    ptrint = 0;
                if (tmpobj) {
                    assign_obj_param(curr->param[1], info, tmpobj);
                    if (ptrint)
                        *ptrint = 1;
                } else if (ptrint)
                    *ptrint = 0;
            }
            curr = curr->next;
            break;

        case SCRIPT_ASSIGN_INV:
            if (curr->param[0] && curr->param[1] && curr->param[2] && curr->param[3]) {
                tmpobj = 0;
                tmpch = get_char_param(curr->param[2], info);
                if (tmpch) {
                    tmpobj = get_obj_in_list_num_containers(real_object(curr->param[0]), tmpch->carrying);
                    tobjcnt = count_obj_in_list(real_object(curr->param[0]), tmpch->carrying);
                }
                ptrint = get_int_param(curr->param[3], info);

                // just in case somone puts something like obj1.vnum here...
                if (!(ptrint == &info->ints[0] || ptrint == &info->ints[1] || ptrint == &info->ints[2]))
                    ptrint = 0;
                if (tmpobj) {
                    assign_obj_param(curr->param[1], info, tmpobj);
                    if (ptrint)
                        *ptrint = tobjcnt;
                } else if (ptrint)
                    *ptrint = 0;
            }
            curr = curr->next;
            break;

        case SCRIPT_ASSIGN_ROOM:
            if (curr->param[0] && curr->param[1] && curr->param[2] && curr->param[3]) {
                tmpobj = 0;
                tmprm = get_room_param(curr->param[2], info);
                if (tmprm) {
                    tmpobj = get_obj_in_list_vnum(curr->param[0], tmprm->contents);
                    tobjcnt = count_obj_in_list(real_object(curr->param[0]), tmprm->contents);
                }
                ptrint = get_int_param(curr->param[3], info);

                // just in case somone puts something like obj1.vnum here...
                if (!(ptrint == &info->ints[0] || ptrint == &info->ints[1] || ptrint == &info->ints[2]))
                    ptrint = 0;
                if (tmpobj) {
                    assign_obj_param(curr->param[1], info, tmpobj);
                    if (ptrint)
                        *ptrint = tobjcnt;
                } else if (ptrint)
                    *ptrint = 0;
            }
            curr = curr->next;
            break;

        case SCRIPT_ASSIGN_STR:
            if (curr->param[0] && curr->text) {
                wtxt = get_text_param_writable(curr->param[0], info);
                CREATE(*wtxt, char, strlen(curr->text) + 1);
                sprintf(*wtxt, curr->text);
            }
            curr = curr->next;
            break;

        case SCRIPT_BEGIN:
            curr = curr->next;
            break;

        case SCRIPT_CHANGE_EXIT_TO:
            if (curr->param[0] && curr->param[2]) {
                tmprm = get_room_param(curr->param[0], info);
                tmpint = real_room(curr->param[2]);
                if (tmprm && (tmpint != NOWHERE) && (-1 < curr->param[1] < 6))
                    tmprm->dir_option[curr->param[1]]->to_room = tmpint;
            }
            curr = curr->next;
            break;

        case SCRIPT_DO_DROP:
            if (curr->param[0] && curr->param[1]) {
                tmpch = get_char_param(curr->param[0], info);
                tmpobj = get_obj_param(curr->param[1], info);
                if (tmpch && tmpobj && (tmpobj->carried_by == tmpch))
                    perform_drop(tmpch, tmpobj, 0);
            }
            curr = curr->next;
            break;

        case SCRIPT_DO_EMOTE:
            if (curr->param[0] && curr->text) {
                tmpch = get_char_param(curr->param[0], info);
                if (tmpch)
                    do_emote(tmpch, curr->text, 0, 36, 0);
            }
            curr = curr->next;
            break;

        case SCRIPT_DO_FLEE:
            if (curr->param[0]) {
                tmpch = get_char_param(curr->param[0], info);
                if (tmpch)
                    do_flee(tmpch, "", 0, 0, 0);
            }
            curr = curr->next;
            break;

        case SCRIPT_DO_FOLLOW:
            // tmpch is follower
            // tmpch2 is leader
            if (curr->param[0] && curr->param[1]) {
                tmpch = get_char_param(curr->param[0], info);
                tmpch2 = get_char_param(curr->param[1], info);
                if (tmpch && tmpch2) {
                    if (circle_follow(tmpch, tmpch2, FOLLOW_MOVE)) {
                        stop_follower(tmpch2, FOLLOW_MOVE);
                    }
                    add_follower(tmpch, tmpch2, FOLLOW_MOVE);
                }
            }
            curr = curr->next;
            break;

        case SCRIPT_DO_GIVE:
            if (curr->param[0] && curr->param[1] && curr->param[2]) {
                tmpch = get_char_param(curr->param[0], info);
                tmpch2 = get_char_param(curr->param[1], info);
                tmpobj = get_obj_param(curr->param[2], info);
                if ((tmpch && tmpch2 && tmpobj) && (tmpobj->carried_by == tmpch))
                    perform_give(tmpch, tmpch2, tmpobj);
                tmpobj = 0;
                assign_obj_param(curr->param[2], info, tmpobj);
            }
            curr = curr->next;
            break;

        case SCRIPT_DO_HIT:
            if (curr->param[0] && curr->param[1]) {
                tmpch = get_char_param(curr->param[0], info);
                tmpch2 = get_char_param(curr->param[1], info);
                if (tmpch && tmpch2) {
                    tmpwtl.targ1.type = TARGET_CHAR;
                    tmpwtl.targ1.ptr.ch = tmpch2;
                    tmpwtl.targ1.ch_num = tmpch2->abs_number;
                    tmpwtl.cmd = CMD_HIT;
                    tmpwtl.subcmd = 0;
                    do_hit(tmpch, 0, &tmpwtl, CMD_HIT, SCMD_MURDER);
                }
            }
            curr = curr->next;
            break;

        case SCRIPT_DO_REMOVE:
            if (curr->param[0] && curr->param[1]) {
                tmpch = get_char_param(curr->param[0], info);
                if (tmpch && (-1 < curr->param[1] < MAX_WEAR))
                    if (tmpch->equipment[curr->param[1]])
                        perform_remove(tmpch, curr->param[1]);
            }
            curr = curr->next;
            break;

        case SCRIPT_DO_SAY:
            if (curr->text && curr->param[0]) {
                txt1 = get_text_param(curr->param[1], info);
                sprintf(output, curr->text, txt1);
                tmpch = get_char_param(curr->param[0], info);
                if (tmpch)
                    do_say(tmpch, output, 0, 0, 0);
            }
            curr = curr->next;
            break;

        case SCRIPT_DO_SOCIAL:
            if (curr->text && curr->param[0])
                if ((tmpint = find_action(curr->text)) != -1) {
                    tmpch = get_char_param(curr->param[0], info);
                    tmpch2 = get_char_param(curr->param[1], info);
                    if ((tmpch2) && (tmpch2->in_room == tmpch->in_room)) {
                        tmpwtl.targ1.ptr.ch = tmpch2;
                        tmpwtl.targ1.type = TARGET_CHAR;
                        tmpwtl.targ1.ch_num = tmpwtl.targ1.ptr.ch->abs_number;
                    } else {
                        tmpwtl.targ1.type = TARGET_NONE;
                        tmpwtl.targ1.ptr.ch = 0;
                    }
                    tmpwtl.cmd = CMD_SOCIAL;
                    tmpwtl.subcmd = tmpint;
                    do_action(tmpch, curr->text, &tmpwtl, 0, 0);
                }
            curr = curr->next;
            break;

        case SCRIPT_DO_WAIT:
            if ((curr->param[0]) && (info->ch[0]) && !(IS_SET(info->ch[0]->specials.affected_by, AFF_WAITING))) {
                if (curr->next)
                    info->next_command = curr->next;
                WAIT_STATE_BRIEF(info->ch[0], curr->param[0], CMD_SCRIPT, 0, 0, AFF_WAITING);
            }
            exit = TRUE;
            break;

        case SCRIPT_DO_WEAR:
            if (curr->param[0] && curr->param[1]) {
                tmpch = get_char_param(curr->param[0], info);
                tmpobj = get_obj_param(curr->param[1], info);
                tmpint = find_eq_pos(tmpch, tmpobj, 0);
                if ((tmpint >= 0) && tmpch && tmpobj && (tmpobj->carried_by == tmpch) && (tmpch->equipment[tmpint] != tmpobj))
                    perform_wear(tmpch, tmpobj, tmpint);
            }
            curr = curr->next;
            break;

        case SCRIPT_DO_YELL:
            if (curr->text && curr->param[0]) {
                txt1 = get_text_param(curr->param[1], info);
                sprintf(output, curr->text, txt1);
                tmpch = get_char_param(curr->param[0], info);
                if (tmpch)
                    do_gen_com(tmpch, output, 0, 0, SCMD_YELL);
            }
            curr = curr->next;
            break;

        case SCRIPT_END:
            curr = curr->next;
            break;

        case SCRIPT_END_ELSE_BEGIN:
            curr = get_next_command(curr);
            break;

        case SCRIPT_EQUIP_CHAR:
            if (curr->param[0]) {
                tmpch = get_char_param(curr->param[0], info);
                if (tmpch) {
                    for (tmpint = 1; tmpint < 6; tmpint++) {
                        if ((tmpint2 = real_object(curr->param[tmpint])) > 0) {
                            tmpobj = read_object(tmpint2, REAL);
                            obj_to_char(tmpobj, tmpch);
                        }
                    }
                    do_wear(tmpch, "all", 0, 0, 0);
                }
            }
            curr = curr->next;
            break;

        case SCRIPT_EXTRACT_CHAR:
            if (curr->param[0]) {
                tmpch = get_char_param(curr->param[0], info);
                if (tmpch) {
                    if (IS_NPC(tmpch))
                        extract_char(tmpch);
                    tmpch = 0;
                    assign_char_param(curr->param[0], info, tmpch);
                }
            }
            curr = curr->next;
            break;

        case SCRIPT_EXTRACT_OBJ:
            if (curr->param[0]) {
                tmpobj = get_obj_param(curr->param[0], info);
                if (tmpobj)
                    extract_obj(tmpobj);
                tmpobj = 0;
                assign_obj_param(curr->param[0], info, tmpobj);
            }
            curr = curr->next;
            break;

        case SCRIPT_GAIN_EXP:
            if (curr->param[0] && curr->param[1]) {
                tmpch = get_char_param(curr->param[0], info);
                ptrint = get_int_param(curr->param[1], info);
                if (tmpch && ptrint)
                    if (!IS_NPC(tmpch)) {
                        int exp;

                        exp = *ptrint;
                        if (GET_LEVEL(tmpch) > 30) {
                            exp = exp * 25 / GET_LEVEL(tmpch);
                            exp = 6 * exp / (GET_LEVEL(tmpch) - 25);
                        }
                        gain_exp(tmpch, exp);
                    }
            }
            curr = curr->next;
            break;

        case SCRIPT_IF_INT_EQUAL:
            if (curr->param[0] && curr->param[1]) {
                ptrint = 0;
                ptrint2 = 0;
                ptrint = get_int_param(curr->param[0], info);
                ptrint2 = get_int_param(curr->param[1], info);
                if (ptrint && ptrint2) {
                    if (*ptrint == *ptrint2) {
                        curr = curr->next;
                    } else {
                        if (curr->next) {
                            if (curr->next->command_type == SCRIPT_BEGIN) {
                                curr = curr->next;
                                curr = get_next_command(curr);
                            } else
                                curr = curr->next->next;
                        } else
                            exit = TRUE;
                    }
                } else
                    exit = TRUE;
            } else
                exit = TRUE;
            break;

        case SCRIPT_IF_INT_LESS:
            if (curr->param[0] && curr->param[1]) {
                ptrint = 0;
                ptrint2 = 0;
                ptrint = get_int_param(curr->param[0], info);
                ptrint2 = get_int_param(curr->param[1], info);
                if (ptrint && ptrint2) {
                    if (*ptrint < *ptrint2) {
                        curr = curr->next;
                    } else {
                        if (curr->next) {
                            if (curr->next->command_type == SCRIPT_BEGIN) {
                                curr = curr->next;
                                curr = get_next_command(curr);
                            } else
                                curr = curr->next->next;
                        } else
                            exit = TRUE;
                    }
                } else
                    exit = TRUE;
            } else
                exit = TRUE;
            break;

        case SCRIPT_IF_INT_GREATER:
            if (curr->param[0] && curr->param[1]) {
                ptrint = 0;
                ptrint2 = 0;
                ptrint = get_int_param(curr->param[0], info);
                ptrint2 = get_int_param(curr->param[1], info);
                if (ptrint && ptrint2) {
                    if (*ptrint > *ptrint2) {
                        curr = curr->next;
                    } else {
                        if (curr->next) {
                            if (curr->next->command_type == SCRIPT_BEGIN) {
                                curr = curr->next;
                                curr = get_next_command(curr);
                            } else
                                curr = curr->next->next;
                        } else
                            exit = TRUE;
                    }
                } else
                    exit = TRUE;
            } else
                exit = TRUE;
            break;

        case SCRIPT_IF_INT_TRUE:
            if (curr->param[0]) {
                ptrint = 0;
                ptrint = get_int_param(curr->param[0], info);
                if (ptrint) {
                    if (*ptrint > 0) {
                        curr = curr->next;
                    } else {
                        if (curr->next) {
                            if (curr->next->command_type == SCRIPT_BEGIN) {
                                curr = curr->next;
                                curr = get_next_command(curr);
                            } else
                                curr = curr->next->next;
                        } else
                            exit = TRUE;
                    }
                } else
                    exit = TRUE;
            } else
                exit = TRUE;
            break;

        case SCRIPT_IF_INT_FALSE:
            if (!curr->param[0]) {
                exit = TRUE;
                break;
            }
            ptrint = 0;
            ptrint = get_int_param(curr->param[0], info);
            if (!ptrint) {
                exit = TRUE;
                break;
            }
            if (*ptrint < 1) {
                curr = curr->next;
            } else {
                if (curr->next) {
                    if (curr->next->command_type == SCRIPT_BEGIN) {
                        curr = curr->next;
                        curr = get_next_command(curr);
                    } else {
                        curr = curr->next->next;
                    }
                } else {
                    exit = TRUE;
                }
            }
            break;

        case SCRIPT_IF_IS_NPC:
            if (curr->param[0]) {
                tmpch = get_char_param(curr->param[0], info);
                if (tmpch) {
                    if (IS_NPC(tmpch)) {
                        curr = curr->next;
                    } else {
                        if (curr->next) {
                            if (curr->next->command_type == SCRIPT_BEGIN) {
                                curr = curr->next;
                                curr = get_next_command(curr);
                            } else
                                curr = curr->next->next;
                        } else
                            exit = TRUE;
                    }
                } else
                    exit = TRUE;
            } else
                exit = TRUE;
            break;

        case SCRIPT_IF_ROOM_SUNLIT:
            if (!curr->param[0]) {
                exit = TRUE;
                break;
            }
            tmprm = get_room_param(curr->param[0], info);
            if (!tmprm) {
                exit = TRUE;
                break;
            }
            if (IS_SUNLIT(tmprm->number)) {
                curr = curr->next;
            } else {
                if (!curr->next) {
                    exit = TRUE;
                    break;
                }
                if (curr->next->command_type == SCRIPT_BEGIN) {
                    curr = curr->next;
                    curr = get_next_command(curr);
                } else {
                    curr = curr->next->next;
                }
            }
            break;

        case SCRIPT_IF_STR_CONTAINS:
            if (curr->param[0]) {
                txt1 = get_text_param(curr->param[0], info);
                if (txt1) {
                    // CREATE(txt2, char, strlen(txt1));
                    // strcpy(txt2, txt1);
                    txt2 = str_dup(txt1);
                    for (txt3 = txt2; *txt3; txt3++)
                        *txt3 = toupper(*txt3);
                    if (strstr(txt2, curr->text)) {
                        RELEASE(txt2);
                        txt2 = 0;
                        curr = curr->next;
                    } else {
                        RELEASE(txt2);
                        txt2 = 0;
                        if (curr->next) {
                            if (curr->next->command_type == SCRIPT_BEGIN) {
                                curr = curr->next;
                                curr = get_next_command(curr);
                            } else
                                curr = curr->next->next;
                        } else
                            exit = TRUE;
                    }
                } else
                    exit = TRUE;
            } else
                exit = TRUE;
            break;

        case SCRIPT_IF_STR_EQUAL:
            if (curr->param[0]) {
                txt1 = get_text_param(curr->param[0], info);
                if (txt1) {
                    if (!strcasecmp(txt1, curr->text)) {
                        curr = curr->next;
                    } else {
                        if (curr->next) {
                            if (curr->next->command_type == SCRIPT_BEGIN) {
                                curr = curr->next;
                                curr = get_next_command(curr);
                            } else
                                curr = curr->next->next;
                        } else
                            exit = TRUE;
                    }
                } else
                    exit = TRUE;
            } else
                exit = TRUE;
            break;

        case SCRIPT_LOAD_MOB:
            if (curr->param[0] && curr->param[1]) {
                tmpch = read_mobile(real_mobile(curr->param[0]), REAL);
                if (tmpch)
                    assign_char_param(curr->param[1], info, tmpch);
            }
            curr = curr->next;
            break;

        case SCRIPT_LOAD_OBJ:
            if (curr->param[0] && curr->param[1]) {
                tmpobj = read_object(real_object(curr->param[0]), REAL);
                if (tmpobj)
                    assign_obj_param(curr->param[1], info, tmpobj);
            }
            curr = curr->next;
            break;

        case SCRIPT_LOAD_OBJ_X:
            if (curr->param[0] && curr->param[1]) {
                tmpobj = read_object(info->ob[0]->item_number, REAL);
                if (tmpobj) {
                    assign_obj_param(curr->param[1], info, tmpobj);
                }
            }
            curr = curr->next;
            break;

        case SCRIPT_OBJ_FROM_CHAR:
            if (curr->param[0] && curr->param[1]) {
                tmpobj = get_obj_param(curr->param[0], info);
                tmpch = get_char_param(curr->param[1], info);
                if (tmpobj && tmpch)
                    if (tmpobj->carried_by == tmpch)
                        obj_from_char(tmpobj);
            }
            curr = curr->next;
            break;

        case SCRIPT_OBJ_FROM_ROOM:
            if (curr->param[0] && curr->param[1]) {
                tmpobj = get_obj_param(curr->param[0], info);
                tmprm = get_room_param(curr->param[1], info);
                if (tmpobj && tmprm)
                    if ((tmpobj->in_room >= 0) ? world[tmpobj->in_room].number : 0 == tmprm->number)
                        obj_from_room(tmpobj);
            }
            curr = curr->next;
            break;

        case SCRIPT_OBJ_TO_CHAR:
            if (curr->param[0] && curr->param[1]) {
                tmpobj = get_obj_param(curr->param[0], info);
                tmpch = get_char_param(curr->param[1], info);
                if (tmpobj && tmpch)
                    obj_to_char(tmpobj, tmpch);
            }
            curr = curr->next;
            break;

        case SCRIPT_OBJ_TO_ROOM:
            if (curr->param[0] && curr->param[1]) {
                tmpobj = get_obj_param(curr->param[0], info);
                tmprm = get_room_param(curr->param[1], info);
                if (tmpobj && tmprm)
                    obj_to_room(tmpobj, real_room(tmprm->number));
            }
            curr = curr->next;
            break;

        case SCRIPT_PAGE_ZONE_MAP:
            if (curr->param[0] && curr->param[1]) {
                tmpch = get_char_param(curr->param[0], info);
                for (tmpint = 0; (tmpint <= top_of_zone_table) && (zone_table[tmpint].number != curr->param[1]); tmpint++)
                    ;
                if ((tmpch) && (tmpint <= top_of_zone_table))
                    send_to_char(zone_table[tmpint].map, tmpch);
            }
            curr = curr->next;
            break;

        // Could possibly add attacktype to this to create different corpses...?
        case SCRIPT_RAW_KILL:
            if (curr->param[0]) {
                tmpch = get_char_param(curr->param[0], info);
                if (tmpch)
                    raw_kill(tmpch, NULL, 0);
            }
            curr = curr->next;
            break;

        case SCRIPT_RETURN_FALSE:
            return_value = FALSE;
            exit = TRUE;
            break;

        case SCRIPT_SEND_TO_CHAR:
            if (curr->text && curr->param[0]) {
                tmpch = get_char_param(curr->param[0], info);
                txt1 = get_text_param(curr->param[1], info);
                sprintf(output, curr->text, txt1);
                if (tmpch) {
                    send_to_char(output, tmpch);
                    send_to_char("\n", tmpch);
                }
            }
            curr = curr->next;
            break;

        case SCRIPT_SEND_TO_ROOM:
            if (curr->text && curr->param[0]) {
                tmprm = get_room_param(curr->param[0], info);
                txt1 = get_text_param(curr->param[1], info);
                sprintf(output, curr->text, txt1);
                if (tmprm) {
                    send_to_room(output, real_room(tmprm->number));
                    send_to_room("\n", real_room(tmprm->number));
                }
            }
            curr = curr->next;
            break;

        case SCRIPT_SEND_TO_ROOM_X:
            if (curr->text && curr->param[0] && curr->param[1]) {
                tmprm = get_room_param(curr->param[0], info);
                tmpch = get_char_param(curr->param[1], info);
                txt1 = get_text_param(curr->param[2], info);
                sprintf(output, curr->text, txt1);
                if (tmprm && tmpch) {
                    send_to_room_except(output, real_room(tmprm->number), tmpch);
                    send_to_room_except("\n", real_room(tmprm->number), tmpch);
                }
            }
            curr = curr->next;
            break;

        case SCRIPT_SET_EXIT_STATE:
            if (curr->param[0]) {
                tmprm = get_room_param(curr->param[2], info);
                if (set_exit_state(tmprm, curr->param[0], curr->param[1])) {
                    tmpint = tmprm->dir_option[curr->param[0]]->to_room;
                    tmprm2 = &world[tmpint];
                    if ((tmprm2) && (tmprm2->dir_option[rev_dir[curr->param[0]]]) && (tmprm2->dir_option[rev_dir[curr->param[0]]]->to_room == real_room(tmprm->number)))
                        set_exit_state(tmprm2, rev_dir[curr->param[0]], curr->param[1]);
                }
            }
            curr = curr->next;
            break;

        /* All binary integer operations int1 = f(int2, int3) */
        case SCRIPT_SET_INT_SUM:
        case SCRIPT_SET_INT_SUB:
        case SCRIPT_SET_INT_MULT:
        case SCRIPT_SET_INT_DIV:
        case SCRIPT_SET_INT_RANDOM:
            ptrint2 = get_int_param(curr->param[1], info);
            ptrint3 = get_int_param(curr->param[2], info);
            if (ptrint2 != NULL && ptrint3 != NULL) {
                x = int_binary_op(info, curr->command_type, ptrint2, ptrint3);
                set_int_value(info, curr->param[0], x);
            } else
                vmudlog(BRF, "Two arguments required for binary integer "
                             "operation in script #%d, but only one given",
                    script_table[info->index].number);

            curr = curr->next;
            break;

        /*
         * This is technically a unary op--however, it sets the
         * variable pointed to by param[1] to the value of param[0],
         * so param[0] is NOT a variable, it is a valid integer
         * value.
         */
        case SCRIPT_SET_INT_VALUE:
            set_int_value(info, curr->param[1], curr->param[0]);
            curr = curr->next;
            break;

        /*
         * All 0-ary integer operations.  I.e., get some value
         * directly from the system.
         */
        case SCRIPT_SET_INT_WAR_STATUS:
            x = int_0ary_op(info, curr->command_type);
            set_int_value(info, curr->param[0], x);
            curr = curr->next;
            break;

        case SCRIPT_TELEPORT_CHAR:
            if (curr->param[0] && curr->param[1]) {
                tmpch = get_char_param(curr->param[1], info);
                tmpint = real_room(curr->param[0]);
                if ((tmpch) && (tmpint > -1)) {
                    if (IS_RIDING(tmpch))
                        stop_riding(tmpch);
                    for (k = tmpch->followers; k; k = next_fol) {
                        next_fol = k->next;
                        if (!IS_NPC(k->follower))
                            continue;
                        if (k->follower->in_room != tmpch->in_room)
                            continue;
                        char_from_room(k->follower);
                        char_to_room(k->follower, tmpint);
                    }
                    char_from_room(tmpch);
                    char_to_room(tmpch, tmpint);
                }
            }
            curr = curr->next;
            break;

        case SCRIPT_TELEPORT_CHAR_X:
            if (curr->param[0] && curr->param[1]) {
                tmpch = get_char_param(curr->param[1], info);
                tmpint = real_room(curr->param[0]);
                if ((tmpch) && (tmpint > -1)) {
                    if (IS_RIDING(tmpch))
                        stop_riding(tmpch);
                    char_from_room(tmpch);
                    char_to_room(tmpch, tmpint);
                }
            }
            curr = curr->next;
            break;

        case SCRIPT_TELEPORT_CHAR_XL:
            if (curr->param[0] && curr->param[1]) {
                tmpch = get_char_param(curr->param[1], info);
                tmprm = get_room_param(curr->param[0], info);
                if ((tmpch) && (tmpint > -1)) {
                    if (IS_RIDING(tmpch)) {
                        stop_riding(tmpch);
                    }
                    char_from_room(tmpch);
                    char_to_room(tmpch, real_room(tmprm->number));
                }
            }
            curr = curr->next;
            break;

        default:
            exit = TRUE;
            break;
        }
        if ((!curr) || (curr->command_type == SCRIPT_ABORT))
            exit = TRUE;
    }
    return return_value;
}

// *******************************************************************************
// ***************************** Triggers for rooms ******************************
// *******************************************************************************

// Empty functions for now - rooms do not have scripts yet

// A room's reaction to a character's entry

int trigger_room_enter(room_data* room, char_data* ch)
{
    return 1;
}

// *******************************************************************************
// *********************** Triggers and commands for characters ******************
// *******************************************************************************

void continue_char_script(char_data* ch)
{
    int return_value;

    initialise_script_info_char(ch, -1); // Invalidates all pointers, but its safter this way
    ch->specials.script_info->ch[0] = ch;
    return_value = run_script(ch->specials.script_info, ch->specials.script_info->next_command);
}

//  A character can prevent another entering the room by returning FALSE

int trigger_before_char_enter(char_data* ch, char_data* vict, room_data* room)
{
    int index;
    int return_value = 1;
    script_data* script_position;

    if (IS_AFFECTED(ch, AFF_WAITING))
        return 1;

    if ((script_position = char_has_script(&index, ch->specials.script_number, ON_BEFORE_ENTER)))
        if (script_position->next) {
            initialise_script_info_char(ch, index);
            ch->specials.script_info->ch[0] = ch;
            ch->specials.script_info->ch[1] = vict;
            ch->specials.script_info->rm[0] = room;
            return_value = run_script(ch->specials.script_info, script_position->next);
        }
    return return_value;
}

int trigger_char_damage(char_data* vict, char_data* ch)
{
    int index;
    int return_value = 1;
    script_data* script_position;

    if (IS_AFFECTED(ch, AFF_WAITING))
        return 1;
    if ((script_position = char_has_script(&index, vict->specials.script_number, ON_DAMAGE)))
        if (script_position->next) {
            initialise_script_info_char(vict, index);
            vict->specials.script_info->ch[0] = vict;
            vict->specials.script_info->ch[1] = ch;
            return_value = run_script(vict->specials.script_info, script_position->next);
        }
    return return_value;
}

int trigger_char_die(char_data* ch)
{
    int index;
    int return_value = 1;
    script_data* script_position;

    // This could potentially be a problem - someone dying bashed not executing their script
    if (IS_AFFECTED(ch, AFF_WAITING))
        return 1;
    if ((script_position = char_has_script(&index, ch->specials.script_number, ON_DIE)))
        if (script_position->next) {
            initialise_script_info_char(ch, index);
            ch->specials.script_info->ch[0] = ch;
            return_value = run_script(ch->specials.script_info, script_position->next);
        }
    return return_value;
}

// A character's reaction to another character's entry

int trigger_char_enter(char_data* ch, char_data* vict, room_data* room)
{
    int index;
    int return_value = 1;
    script_data* script_position;

    if (IS_AFFECTED(ch, AFF_WAITING))
        return 1;
    if ((script_position = char_has_script(&index, ch->specials.script_number, ON_ENTER)))
        if (script_position->next) {
            initialise_script_info_char(ch, index);
            ch->specials.script_info->ch[0] = ch;
            ch->specials.script_info->ch[1] = vict;
            ch->specials.script_info->rm[0] = room;
            return_value = run_script(ch->specials.script_info, script_position->next);
        }
    return return_value;
}

int trigger_char_hear(char_data* ch, char_data* speaking, char* text)
{
    int index;
    int return_value = 1;
    script_data* script_position;

    if (IS_AFFECTED(ch, AFF_WAITING))
        return 1;
    if ((script_position = char_has_script(&index, ch->specials.script_number, ON_HEAR_SAY)))
        if (script_position->next) {
            initialise_script_info_char(ch, index);
            ch->specials.script_info->ch[0] = ch;
            ch->specials.script_info->ch[1] = speaking;
            ch->specials.script_info->str[0] = text;
            return_value = run_script(ch->specials.script_info, script_position->next);
        }
    if ((script_position = char_has_script(&index, ch->specials.script_number, ON_HEAR_YELL))) {
        if (script_position->next) {
            initialise_script_info_char(ch, index);
            ch->specials.script_info->ch[0] = ch;
            ch->specials.script_info->ch[1] = speaking;
            ch->specials.script_info->str[0] = text;
            return_value = run_script(ch->specials.script_info, script_position->next);
        }
    }
    return return_value;
}

int trigger_char_receive(char_data* ch1, char_data* ch2, obj_data* ob1)
{
    int index;
    int return_value = 1;
    script_data* script_position;

    if (IS_AFFECTED(ch1, AFF_WAITING))
        return 1;
    if ((script_position = char_has_script(&index, ch1->specials.script_number, ON_RECEIVE)))
        if (script_position->next) {
            initialise_script_info_char(ch1, index);
            ch1->specials.script_info->ch[0] = ch1;
            ch1->specials.script_info->ch[1] = ch2;
            ch1->specials.script_info->ob[0] = ob1;
            return_value = run_script(ch1->specials.script_info, script_position->next);
        }

    return return_value;
}

// *******************************************************************************
// **************************** Triggers for objects *****************************
// *******************************************************************************

int trigger_object_damage(obj_data* obj, char_data* vict, char_data* ch)
{
    int index;
    script_data* script_position;
    int return_value = 1;

    if ((script_position = char_has_script(&index, obj->obj_flags.script_number, ON_DAMAGE)))
        if (script_position->next) {
            initialise_script_info_obj(obj, index);
            obj->obj_flags.script_info->ch[0] = vict;
            obj->obj_flags.script_info->ch[1] = ch;
            obj->obj_flags.script_info->ob[0] = obj;
            obj->obj_flags.script_info->rm[0] = 0; // ADD room of character
            return_value = run_script(obj->obj_flags.script_info, script_position->next);
        }

    return return_value;
}

int trigger_object_event(int trigger_type, obj_data* obj, char_data* ch)
{
    int index;
    script_data* script_position;
    int return_value = 1;

    switch (trigger_type) {

    case ON_EAT:
        if ((script_position = char_has_script(&index, obj->obj_flags.script_number, ON_EAT)))
            if (script_position->next) {
                initialise_script_info_obj(obj, index);
                obj->obj_flags.script_info->ch[0] = ch;
                obj->obj_flags.script_info->ob[0] = obj;
                obj->obj_flags.script_info->rm[0] = 0; // ADD room of character
                return_value = run_script(obj->obj_flags.script_info, script_position->next);
            }
        break;

    case ON_ENTER:
        if ((script_position = char_has_script(&index, obj->obj_flags.script_number, ON_ENTER)))
            if (script_position->next) {
                initialise_script_info_obj(obj, index);
                obj->obj_flags.script_info->ch[0] = ch;
                obj->obj_flags.script_info->ob[0] = obj;
                obj->obj_flags.script_info->rm[0] = 0; // ADD room of character
                return_value = run_script(obj->obj_flags.script_info, script_position->next);
            }
        break;

    case ON_EXAMINE_OBJECT:

        if ((script_position = char_has_script(&index, obj->obj_flags.script_number, ON_EXAMINE_OBJECT)))
            if (script_position->next) {
                initialise_script_info_obj(obj, index);
                obj->obj_flags.script_info->ch[0] = ch;
                obj->obj_flags.script_info->ob[0] = obj;
                return_value = run_script(obj->obj_flags.script_info, script_position->next);
            }
        break;

    case ON_DRINK:

        if ((script_position = char_has_script(&index, obj->obj_flags.script_number, ON_DRINK)))
            if (script_position->next) {
                initialise_script_info_obj(obj, index);
                obj->obj_flags.script_info->ch[0] = ch;
                obj->obj_flags.script_info->ob[0] = obj;
                return_value = run_script(obj->obj_flags.script_info, script_position->next);
            }
        break;

    case ON_PULL:

        if ((script_position = char_has_script(&index, obj->obj_flags.script_number, ON_PULL)))
            if (script_position->next) {
                initialise_script_info_obj(obj, index);
                obj->obj_flags.script_info->ch[0] = ch;
                obj->obj_flags.script_info->ob[0] = obj;
                return_value = run_script(obj->obj_flags.script_info, script_position->next);
            }
        break;

    case ON_WEAR:

        if ((script_position = char_has_script(&index, obj->obj_flags.script_number, ON_WEAR)))
            if (script_position->next) {
                initialise_script_info_obj(obj, index);
                obj->obj_flags.script_info->ch[0] = ch;
                obj->obj_flags.script_info->ob[0] = obj;
                return_value = run_script(obj->obj_flags.script_info, script_position->next);
            }
        break;
    }
    return return_value;
}
