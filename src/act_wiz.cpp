/* ************************************************************************
 *   File: act.wizard.c                                  Part of CircleMUD *
 *  Usage: Player-level god commands and other goodies                     *
 *                                                                         *
 *  All rights reserved.  See license.doc for complete information.        *
 *                                                                         *
 *  Copyright (C) 1993 by the Trustees of the Johns Hopkins University     *
 *  CircleMUD is based on DikuMUD, Copyright (C) 1990, 1991.               *
 ************************************************************************ */

#include "platdef.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

#include "char_utils.h"
#include "color.h"
#include "comm.h"
#include "db.h"
#include "handler.h"
#include "interpre.h"
#include "limits.h"
#include "mudlle.h"
#include "pkill.h"
#include "profs.h"
#include "protos.h"
#include "spells.h"
#include "structs.h"
#include "utils.h"
#include "warrior_spec_handlers.h"
#include "zone.h"

const int IMM_SEE_INVIS_OBJ_MIN_LVL = 92;

/*   external vars  */
extern FILE* player_fl;
extern struct room_data world;
extern struct char_data* character_list;
extern struct obj_data* object_list;
extern struct descriptor_data* descriptor_list;
extern struct index_data* mob_index;
extern struct index_data* obj_index;
extern struct player_index_element* player_table;
int get_percent_absorb(char_data* ch);
void stop_riding(char_data* ch);
extern int cost_per_day(obj_data* obj);
extern struct script_head* script_table;
extern int top_of_script_table;
extern void msdp_room_update(char_data* ch);

extern int restrict;
extern int top_of_world;
extern int top_of_mobt;
extern int top_of_objt;
extern int top_of_p_table;
extern byte language_number;

extern int txt_block_counter;
extern int affected_type_counter;

extern unsigned long stat_ticks_passed;
extern unsigned long stat_mortals_counter;
extern unsigned long stat_immortals_counter;
extern unsigned long stat_whitie_counter;
extern unsigned long stat_darkie_counter;
extern unsigned long stat_whitie_legend_counter;
extern unsigned long stat_darkie_legend_counter;

/* for objects */
extern char* item_types[];
extern char* wear_bits[];
extern char* extra_bits[];
extern char* drinks[];

/* for rooms */
extern char* dirs[];
extern char* room_bits[];
extern char* exit_bits[];
extern char* sector_types[];

/* for chars */
extern struct skill_data skills[];
extern char* equipment_types[];
extern char* affected_bits[];
extern char* apply_types[];
extern char* pc_prof_types[];
extern char* npc_prof_types[];
extern char* action_bits[];
extern char* player_bits[];
extern char* preference_bits[];
extern char* position_types[];
extern char* connected_types[];
extern byte language_skills[];
extern int num_of_object_materials;
extern char* object_materials[];
extern char* mobile_program_base[];
extern char** mobile_program;
extern int* mobile_program_zone;
extern int num_of_programs;
extern char* resistance_name[];
extern char* vulnerability_name[];
/* external functs */

// char *crypt(const char *key, const char *salt);
int find_name(char* name);
void encrypt_line(unsigned char* line, int len);
void show_room_affection(char* str, affected_type* aff, int mode);
void print_exploits(char_data* sendto, char* name);

ACMD(do_look);
int Check_zone_authority(int zonenum, char_data* ch);

ACMD(do_emote)
{
    int i;

    //    if (IS_NPC(ch))
    //       return;

    if (wtl && (wtl->targ1.type == TARGET_TEXT))
        argument = wtl->targ1.ptr.text->text;

    for (i = 0; *(argument + i) == ' '; i++)
        ;

    if (!*(argument + i))
        send_to_char("Yes.. But what?\n\r", ch);
    else {
        sprintf(buf, "$n %s", argument + i);
        act(buf, FALSE, ch, 0, 0, TO_ROOM);
        if (!PRF_FLAGGED(ch, PRF_ECHO))
            send_to_char("Ok.\n\r", ch);
        else
            act(buf, FALSE, ch, 0, 0, TO_CHAR);
    }
}

ACMD(do_send)
{
    struct char_data* vict;

    half_chop(argument, arg, buf);

    if (!*arg) {
        send_to_char("Send what to who?\n\r", ch);
        return;
    }

    if (!(vict = get_char_vis(ch, arg))) {
        send_to_char("No such person around.\n\r", ch);
        return;
    }

    send_to_char(buf, vict);
    send_to_char("\n\r", vict);
    if (!PRF_FLAGGED(ch, PRF_ECHO))
        send_to_char("Sent.\n\r", ch);
    else {
        sprintf(buf2, "You send '%s' to %s.\n\r", buf, GET_NAME(vict));
        send_to_char(buf2, ch);
    }
}

ACMD(do_echo)
{
    int i;
    char_data* tmpch;

    if (/*IS_NPC(ch) ||*/ (ch->in_room == NOWHERE))
        return;

    for (i = 0; *(argument + i) == ' '; i++)
        ;

    if (!*(argument + i))
        send_to_char("That must be a mistake...\n\r", ch);
    else {
        sprintf(buf, "%s\n\r", argument + i);
        //      send_to_room_except(buf, ch->in_room, ch);
        for (tmpch = world[ch->in_room].people; tmpch;
             tmpch = tmpch->next_in_room)
            if (tmpch != ch)
                send_to_char(buf, tmpch);

        if (!PRF_FLAGGED(ch, PRF_ECHO))
            send_to_char("Ok.\n\r", ch);
        else
            send_to_char(buf, ch);
    }
}

/* take a string, and return an rnum.. used for goto, at, etc.  -je 4/6/93 */
int find_target_room(struct char_data* ch, char* rawroomstr)
{
    int tmp;
    int location;
    struct char_data* target_mob;
    struct obj_data* target_obj;
    char roomstr[MAX_INPUT_LENGTH];

    one_argument(rawroomstr, roomstr);

    if (!*roomstr) {
        send_to_char("You must supply a room number or name.\n\r", ch);
        return NOWHERE;
    }

    if (isdigit(*roomstr) && !strchr(roomstr, '.')) {
        tmp = atoi(roomstr);
        if ((location = real_room(tmp)) < 0) {
            send_to_char("No room exists with that number.\n\r", ch);
            return NOWHERE;
        }
    } else if ((target_mob = get_char_vis(ch, roomstr)))
        location = target_mob->in_room;
    else if ((target_obj = get_obj_vis(ch, roomstr))) {
        if (target_obj->in_room != NOWHERE)
            location = target_obj->in_room;
        else {
            send_to_char("That object is not available.\n\r", ch);
            return NOWHERE;
        }
    } else {
        send_to_char("No such creature or object around.\n\r", ch);
        return NOWHERE;
    }

    /* a location has been found. */
    if (IS_SET(world[location].room_flags, GODROOM) && GET_LEVEL(ch) < LEVEL_GOD) {
        send_to_char("You are not godly enough to use that room!\n\r", ch);
        return NOWHERE;
    }

    if (IS_SET(world[location].room_flags, SECURITYROOM) && GET_LEVEL(ch) < LEVEL_GRGOD) {
        send_to_char("That is a security room! Talk to Implementor or GRGOD, if you need access.\n\r", ch);
        return NOWHERE;
    }

    if (IS_SET(world[location].room_flags, PRIVATE) && GET_LEVEL(ch) < LEVEL_GRGOD)
        if (world[location].people && world[location].people->next_in_room) {
            send_to_char("There's a private conversation going on in that room.\n\r", ch);
            return NOWHERE;
        }

    return location;
}

ACMD(do_at)
{
    char command[MAX_INPUT_LENGTH];
    int location, original_loc;

    if (IS_NPC(ch))
        return;

    half_chop(argument, buf, command);
    if (!*buf) {
        send_to_char("You must supply a room number or a name.\n\r", ch);
        return;
    }

    if ((location = find_target_room(ch, buf)) < 0)
        return;

    /* a location has been found. */
    original_loc = ch->in_room;
    if (original_loc != location) {
        char_from_room(ch);
        char_to_room(ch, location);
    }
    command_interpreter(ch, command);

    /* check if the guy's still there */
    if (ch->in_room == location) {
        char_from_room(ch);
        char_to_room(ch, original_loc);
    }
}

ACMD(do_goto)
{
    int location;

    if (IS_NPC(ch))
        return;

    if ((location = find_target_room(ch, argument)) < 0)
        return;

    stop_riding(ch);

    if (ch->specials.poofOut)
        sprintf(buf, "%s", ch->specials.poofOut);
    else
        strcpy(buf, "$n disappears in a puff of smoke.");

    act(buf, TRUE, ch, 0, 0, TO_ROOM);
    char_from_room(ch);
    char_to_room(ch, location);

    if (ch->specials.poofIn)
        sprintf(buf, "%s", ch->specials.poofIn);
    else
        strcpy(buf, "A huge gate appears briefly and $n steps out.");

    act(buf, TRUE, ch, 0, 0, TO_ROOM);
    do_look(ch, "", wtl, 0, 0);
    msdp_room_update(ch);
}

ACMD(do_trans)
{
    struct descriptor_data* i;
    struct char_data* victim;

    if (IS_NPC(ch))
        return;

    one_argument(argument, buf);
    if (!*buf)
        send_to_char("Whom do you wish to transfer?\n\r", ch);
    else if (str_cmp("all", buf)) {
        if (!(victim = get_char_vis(ch, buf)))
            send_to_char("No-one by that name around.\n\r", ch);
        else if (victim == ch)
            send_to_char("That doesn't make much sense, does it?\n\r", ch);
        else {
            if ((GET_LEVEL(ch) < GET_LEVEL(victim)) && !IS_NPC(victim)) {
                send_to_char("Go transfer someone your own size.\n\r", ch);
                return;
            }
            act("$n disappears in a mushroom cloud.", FALSE, victim, 0, 0, TO_ROOM);
            if (IS_RIDING(victim))
                stop_riding(victim);
            char_from_room(victim);
            char_to_room(victim, ch->in_room);
            act("$n arrives from a puff of smoke.", FALSE, victim, 0, 0, TO_ROOM);
            act("$n has transferred you!", FALSE, ch, 0, victim, TO_VICT);
            do_look(victim, "", wtl, 0, 0);
        }
    } else { /* Trans All */
        for (i = descriptor_list; i; i = i->next)
            if (!i->connected && i->character && i->character != ch) {
                victim = i->character;
                act("$n disappears in a mushroom cloud.", FALSE, victim, 0, 0, TO_ROOM);
                if (IS_RIDING(victim))
                    stop_riding(victim);
                char_from_room(victim);
                char_to_room(victim, ch->in_room);
                act("$n arrives from a puff of smoke.", FALSE, victim, 0, 0, TO_ROOM);
                act("$n has transferred you!", FALSE, ch, 0, victim, TO_VICT);
                do_look(victim, "", wtl, 0, 0);
            }

        send_to_char("Ok.\n\r", ch);
    }
}

ACMD(do_teleport)
{
    struct char_data* victim;
    sh_int target;

    if (IS_NPC(ch))
        return;

    half_chop(argument, buf, buf2);

    if (!*buf)
        send_to_char("Who do you wish to teleport?\n\r", ch);
    else if (!(victim = get_char_vis(ch, buf)))
        send_to_char("No-one by that name around.\n\r", ch);
    else if (victim == ch)
        send_to_char("Use 'goto' to teleport yourself.\n\r", ch);
    else if (!*buf2)
        send_to_char("Where do you wish to send this person?\n\r", ch);
    else if ((target = find_target_room(ch, buf2)) >= 0) {
        act("$n disappears in a puff of smoke.", FALSE, victim, 0, 0, TO_ROOM);
        if (IS_RIDING(victim))
            stop_riding(victim);
        char_from_room(victim);
        char_to_room(victim, target);
        act("$n arrives from a puff of smoke.", FALSE, victim, 0, 0, TO_ROOM);
        act("$n has teleported you!", FALSE, ch, 0, (char*)victim, TO_VICT);
        do_look(victim, "", wtl, 0, 0);
    }
}

ACMD(do_vnum)
{
    if (IS_NPC(ch)) {
        send_to_char("What would a monster do with a vnum?\n\r", ch);
        return;
    }

    half_chop(argument, buf, buf2);

    if (!*buf || !*buf2 || (!is_abbrev(buf, "mob") && !is_abbrev(buf, "obj"))) {
        send_to_char("Usage: vnum { obj | mob } <name>\n\r", ch);
        return;
    }

    if (is_abbrev(buf, "mob"))
        if (!vnum_mobile(buf2, ch))
            send_to_char("No mobiles by that name.\n\r", ch);

    if (is_abbrev(buf, "obj"))
        if (!vnum_object(buf2, ch))
            send_to_char("No objects by that name.\n\r", ch);
}

void do_stat_room(struct char_data* ch)
{
    struct extra_descr_data* desc;
    struct room_data* rm = &world[ch->in_room];
    int i, found = 0;
    char* txt1 = 0;
    struct obj_data* j = 0;
    struct char_data* k = 0;
    struct affected_type* tmpaf;

    if (!Check_zone_authority(rm->number / 100, ch)) {
        send_to_char("You have no permissions in this zone.\n\r", ch);
        return;
    }

    sprintf(buf, "Room name: %s%s%s\n\r", CC_USE(ch, COLOR_ROOM), rm->name,
        CC_NORM(ch));
    send_to_char(buf, ch);

    sprinttype(rm->sector_type, sector_types, buf2);
    sprintf(buf, "Zone: [%3d], VNum: [%5d], RNum: [%5d], Type: %s\n\r",
        rm->zone, rm->number, ch->in_room, buf2);
    send_to_char(buf, ch);

    sprintbit((long)rm->room_flags, room_bits, buf2, 0);
    sprintf(buf, "SpecProc: %s, Flags: %s\n\r", (rm->funct) ? "Exists" : "No",
        buf2);
    send_to_char(buf, ch);

    sprintf(buf, "Level: %d, No. of lights: %d\n\r", rm->level, rm->light);
    send_to_char(buf, ch);

    send_to_char("Description:\n\r", ch);
    if (rm->description)
        send_to_char(rm->description, ch);
    else
        send_to_char("  None.\n\r", ch);

    if (rm->ex_description) {
        sprintf(buf, "Extra descs:%s", CC_USE(ch, COLOR_ROOM));
        for (desc = rm->ex_description; desc; desc = desc->next) {
            strcat(buf, " ");
            strcat(buf, desc->keyword);
        }
        strcat(buf, CC_NORM(ch));
        send_to_char(strcat(buf, "\n\r"), ch);
    }

    sprintf(buf, "Chars present:%s", CC_USE(ch, COLOR_CHAR));
    for (found = 0, k = rm->people; k; k = k->next_in_room) {
        if (!CAN_SEE(ch, k))
            continue;
        sprintf(buf2, "%s %s(%s)", found++ ? "," : "", GET_NAME(k),
            (!IS_NPC(k) ? "PC" : (!IS_MOB(k) ? "NPC" : "MOB")));
        strcat(buf, buf2);
        if(IS_NPC(k)) {
            sprintf(buf2, " [%d]", mob_index[k->nr].virt);
            strcat(buf, buf2);
        }
        if (strlen(buf) >= 62) {
            if (k->next_in_room)
                send_to_char(strcat(buf, ",\n\r"), ch);
            else
                send_to_char(strcat(buf, "\n\r"), ch);
            *buf = found = 0;
        }
    }

    if (*buf)
        send_to_char(strcat(buf, "\n\r"), ch);
    send_to_char(CC_NORM(ch), ch);

    if (rm->contents) {
        sprintf(buf, "Contents:%s", CC_USE(ch, COLOR_OBJ));
        for (found = 0, j = rm->contents; j; j = j->next_content) {
            if ( !CAN_SEE_OBJ(ch, j) && ch->player.level < IMM_SEE_INVIS_OBJ_MIN_LVL )
                continue;
            sprintf(buf2, "%s %s", found++ ? "," : "", j->short_description);
            if(found > 0 && ch->player.level > 91)
                sprintf(buf1, " [%d]", obj_index[j->item_number].virt);
                strcat(buf2, buf1);
            strcat(buf, buf2);
            if (strlen(buf) >= 62) {
                if (j->next_content)
                    send_to_char(strcat(buf, ",\n\r"), ch);
                else
                    send_to_char(strcat(buf, "\n\r"), ch);
                *buf = found = 0;
            }
        }

        if (*buf)
            send_to_char(strcat(buf, "\n\r"), ch);
        send_to_char(CC_NORM(ch), ch);
    }

    for (i = 0; i < NUM_OF_DIRS; i++) {
        if (rm->dir_option[i]) {
            if (rm->dir_option[i]->to_room == NOWHERE)
                sprintf(buf1, " NONE");
            else
                sprintf(buf1, "%5d",
                    world[rm->dir_option[i]->to_room].number);
            sprintbit(rm->dir_option[i]->exit_info, exit_bits, buf2, 0);
            sprintf(buf, "Exit %-5s:  To: [%s], Key: [%5d], Keywrd: %s, Type: %s\n\r ",
                dirs[i], buf1, rm->dir_option[i]->key,
                rm->dir_option[i]->keyword ? rm->dir_option[i]->keyword : "None",
                buf2);
            send_to_char(buf, ch);
            if (rm->dir_option[i]->general_description)
                strcpy(buf, rm->dir_option[i]->general_description);
            else
                strcpy(buf, "  No exit description.\n\r");
            send_to_char(buf, ch);
        }
    }
    if (!rm->affected) {
        send_to_char("Affections: None.\n\r", ch);
    } else {
        strcpy(buf, "Affections:\n\r");
        for (tmpaf = rm->affected; tmpaf; tmpaf = tmpaf->next) {

            show_room_affection(buf, tmpaf, 1);
        }
        send_to_char(buf, ch);
    }
}

void do_stat_object(struct char_data* ch, struct obj_data* j)
{
    char found;
    int i, player_num;
    struct obj_data* j2;
    struct extra_descr_data* desc;
    int virt;

    virt = (j->item_number >= 0) ? obj_index[j->item_number].virt : 0;

    sprintf(buf, "Name: '%s%s%s', Aliases: %s\n\r", CC_FIX(ch, CYEL),
        ((j->short_description) ? j->short_description : "<None>"),
        CC_NORM(ch), j->name);
    send_to_char(buf, ch);
    sprinttype(GET_ITEM_TYPE(j), item_types, buf1);
    if (j->item_number >= 0)
        strcpy(buf2, (obj_index[j->item_number].func ? "Exists" : "None"));
    else
        strcpy(buf2, "None");
    sprintf(buf, "VNum: [%s%5d%s], RNum: [%5d], Type: %s, SpecProc: %s\n\r",
        CC_FIX(ch, CGRN), virt, CC_NORM(ch), j->item_number, buf1, buf2);
    send_to_char(buf, ch);
    sprintf(buf, "L-Des: %s\n\r", ((j->description) ? j->description : "None"));
    send_to_char(buf, ch);

    if (j->ex_description) {
        sprintf(buf, "Extra descs:%s", CC_FIX(ch, CCYN));
        for (desc = j->ex_description; desc; desc = desc->next) {
            strcat(buf, " ");
            if (desc->keyword)
                strcat(buf, desc->keyword);
            else
                strcat(buf, "<None>");
        }
        strcat(buf, CC_NORM(ch));
        send_to_char(strcat(buf, "\n\r"), ch);
    }

    send_to_char("Can be worn on: ", ch);
    sprintbit(j->obj_flags.wear_flags, wear_bits, buf, 0);
    strcat(buf, "\n\r");
    send_to_char(buf, ch);

    send_to_char("Set char bits : ", ch);
    sprintbit(j->obj_flags.bitvector, affected_bits, buf, 0);
    strcat(buf, "\n\r");
    send_to_char(buf, ch);

    send_to_char("Extra flags   : ", ch);
    sprintbit(j->obj_flags.extra_flags, extra_bits, buf, 0);
    strcat(buf, "\n\r");
    send_to_char(buf, ch);

    sprintf(buf, "Material: %s\n\r",
        ((j->obj_flags.material >= 0) && (j->obj_flags.material < num_of_object_materials)) ? object_materials[j->obj_flags.material] : "Unknown");
    send_to_char(buf, ch);

    sprintf(buf, "Weight: %d, Value: %d, Cost/day: %d (set to %d), Level %d, Timer: %d\n\r",
        j->obj_flags.weight, j->obj_flags.cost, cost_per_day(j), j->obj_flags.cost_per_day, j->obj_flags.level, j->obj_flags.timer);
    send_to_char(buf, ch);

    sprintf(buf, "Script number: %d\n\r", j->obj_flags.script_number);
    send_to_char(buf, ch);

    strcpy(buf, "In room: ");
    if (j->in_room == NOWHERE)
        strcat(buf, "Nowhere");
    else {
        sprintf(buf2, "%d", world[j->in_room].number);
        strcat(buf, buf2);
    }
    strcat(buf, ", In object: ");
    strcat(buf, j->in_obj ? j->in_obj->short_description : "None");
    strcat(buf, ", Carried by: ");
    strcat(buf, j->carried_by ? GET_NAME(j->carried_by) : "Nobody");
    if ((GET_LEVEL(ch) >= LEVEL_AREAGOD) && (j->loaded_by)) {
        player_num = find_player_in_table("", j->loaded_by);
        if (player_num != -1) {
            strcat(buf, ", Loaded by: ");
            strcat(buf, (player_table + player_num)->name);
        }
    }
    strcat(buf, "\n\r");
    send_to_char(buf, ch);

    switch (j->obj_flags.type_flag) {
    case ITEM_LIGHT:
        sprintf(buf, "Color: [%d], Type: [%d], Hours: [%d]",
            j->obj_flags.value[0], j->obj_flags.value[1], j->obj_flags.value[2]);
        break;
    case ITEM_SCROLL:
    case ITEM_POTION:
        sprintf(buf, "Spells: %d, %d, %d, %d", j->obj_flags.value[0],
            j->obj_flags.value[1], j->obj_flags.value[2], j->obj_flags.value[3]);
        break;
    case ITEM_WAND:
    case ITEM_STAFF:
        sprintf(buf, "Spell: %d, Stamina: %d", j->obj_flags.value[0],
            j->obj_flags.value[1]);
        break;
    case ITEM_FIREWEAPON:
    case ITEM_WEAPON:
        sprintf(buf, "OB: %d, Parry: %d, Bulk: %d,  Type: %d, Damage: %d/10 (set to %d)", j->obj_flags.value[0], j->obj_flags.value[1], j->obj_flags.value[2], j->obj_flags.value[3], get_weapon_damage(j), j->obj_flags.value[4]);
        break;
    case ITEM_MISSILE:
        sprintf(buf, "To Hit: %d, To Dam: %d, Break Percentage: %d", j->obj_flags.value[0],
            j->obj_flags.value[1], j->obj_flags.value[3]);
        break;
    case ITEM_ARMOR:
        sprintf(buf, "Absorb: [%d]%%, Min.abs.: [%d], Encum: [%d], dodge[%d]", armor_absorb(j), j->obj_flags.value[1], j->obj_flags.value[2], j->obj_flags.value[3]);
        break;
    case ITEM_TRAP:
        sprintf(buf, "Spell: %d, - Hitpoints: %d",
            j->obj_flags.value[0], j->obj_flags.value[1]);
        break;
    case ITEM_CONTAINER:
        sprintf(buf, "Max-contains: %d, Locktype: %d, Corpse: %s",
            j->obj_flags.value[0], j->obj_flags.value[1],
            j->obj_flags.value[3] ? "Yes" : "No");
        break;
    case ITEM_DRINKCON:
    case ITEM_FOUNTAIN:
        sprinttype(j->obj_flags.value[2], drinks, buf2);
        sprintf(buf, "Max-contains: %d, Contains: %d, Poisoned: %s, Liquid: %s",
            j->obj_flags.value[0], j->obj_flags.value[1],
            j->obj_flags.value[3] ? "Yes" : "No", buf2);
        break;
    case ITEM_NOTE:
        sprintf(buf, "Tounge: %d", j->obj_flags.value[0]);
        break;
    case ITEM_KEY:
        sprintf(buf, "Keytype: %d", j->obj_flags.value[0]);
        break;
    case ITEM_FOOD:
        sprintf(buf, "Makes full: %d, Poisoned: %d",
            j->obj_flags.value[0], j->obj_flags.value[3]);
        break;
    case ITEM_SHIELD:
        sprintf(buf, "dodge: [%d], Parry: [%d], Encum: [%d], Block coef.: [%d]", j->obj_flags.value[0], j->obj_flags.value[1], j->obj_flags.value[2], j->obj_flags.value[3]);
        break;
    case ITEM_LEVER:
        sprintf(buf, "room: [%d], Direction: [(%d) %s]",
            j->obj_flags.value[0], j->obj_flags.value[1],
            ((j->obj_flags.value[1] >= 0) && (j->obj_flags.value[1] < NUM_OF_DIRS)) ? "undef" : dirs[j->obj_flags.value[1]]);
        break;

    default:
        sprintf(buf, "Values 0-4: [%d] [%d] [%d] [%d] [%d]",
            j->obj_flags.value[0], j->obj_flags.value[1],
            j->obj_flags.value[2], j->obj_flags.value[3], j->obj_flags.value[4]);
        break;
    }
    send_to_char(buf, ch);

    if (j->contains) {
        sprintf(buf, "Contents:%s", CC_USE(ch, COLOR_OBJ));
        for (found = 0, j2 = j->contains; j2; j2 = j2->next_content) {
            sprintf(buf2, "%s %s", found++ ? "," : "", j2->short_description);
            strcat(buf, buf2);
            if (strlen(buf) >= 62) {
                if (j2->next_content)
                    send_to_char(strcat(buf, ",\n\r"), ch);
                else
                    send_to_char(strcat(buf, "\n\r"), ch);
                *buf = found = 0;
            }
        }

        if (*buf)
            send_to_char(strcat(buf, "\n\r"), ch);
        send_to_char(CC_NORM(ch), ch);
    }

    found = 0;
    send_to_char("\n\rAffections:", ch);
    for (i = 0; i < MAX_OBJ_AFFECT; i++)
        if (j->affected[i].modifier) {
            sprinttype(j->affected[i].location, apply_types, buf2);
            sprintf(buf, "%s %+d to %s", found++ ? "," : "",
                j->affected[i].modifier, buf2);
            send_to_char(buf, ch);
        }
    if (!found)
        send_to_char(" None", ch);

    send_to_char("\n\r", ch);
}

void do_stat_character(struct char_data* ch, struct char_data* k)
{
    int i, i2, found = 0;
    struct obj_data* j;
    struct follow_type* fol;
    struct affected_type* aff;
    struct memory_rec* tmprec;

    extern const char* specialize_name[];

    if ((GET_LEVEL(ch) < LEVEL_AREAGOD) && (!IS_NPC(k))) {
        send_to_char("You can't do this.\n\r", ch);
        return;
    }

    switch (k->player.sex) {
    case SEX_NEUTRAL:
        strcpy(buf, "NEUTRAL-SEX");
        break;
    case SEX_MALE:
        strcpy(buf, "MALE");
        break;
    case SEX_FEMALE:
        strcpy(buf, "FEMALE");
        break;
    default:
        strcpy(buf, "ILLEGAL-SEX!!");
        break;
    }

    sprintf(buf2, " %s '%s'  IDNum: [%5ld], Player table index: [%d], In room [%5d]\n\r",
        (!IS_NPC(k) ? "PC" : (!IS_MOB(k) ? "NPC" : "MOB")),
        GET_NAME(k), GET_IDNUM(k), GET_INDEX(k), (k->in_room < 0) ? -1 : world[k->in_room].number);
    send_to_char(strcat(buf, buf2), ch);

    if (IS_MOB(k)) {
        sprintf(buf, "Alias: %s, VNum: [%5d], RNum: [%5d]\n\r",
            k->player.name, mob_index[k->nr].virt, k->nr);
        send_to_char(buf, ch);
    }
    sprintf(buf, "Title: %s\n\r", (k->player.title ? k->player.title : "<None>"));
    sprintf(buf2, "Race: %s, which is number %d\n\r", pc_race_types[GET_RACE(k)], GET_RACE(k));
    strcat(buf, buf2);
    send_to_char(buf, ch);

    send_to_char("L-Des: ", ch);
    if (k->player.long_descr)
        send_to_char(k->player.long_descr, ch);
    else
        send_to_char("<None>", ch);
    send_to_char("\n\r", ch);
    if (!IS_NPC(k)) {
        sprintf(buf, "Class coofs : Mag:%d Cle:%d Ran:%d War:%d\n\r", GET_PROF_COOF(PROF_MAGIC_USER, k), GET_PROF_COOF(PROF_CLERIC, k), GET_PROF_COOF(PROF_RANGER, k), GET_PROF_COOF(PROF_WARRIOR, k));
        send_to_char(buf, ch);

        sprintf(buf, "Class levels: Mag:%d Cle:%d Ran:%d War:%d,  mini-level: %d,mml:%d\n\r", GET_PROF_LEVEL(PROF_MAGIC_USER, k), GET_PROF_LEVEL(PROF_CLERIC, k), GET_PROF_LEVEL(PROF_RANGER, k), GET_PROF_LEVEL(PROF_WARRIOR, k), GET_MINI_LEVEL(k), GET_MAX_MINI_LEVEL(k));
        send_to_char(buf, ch);
    } else {
        sprintf(buf, "Class coofs are not defined for mobiles.\n\r");
        send_to_char(buf, ch);
    }

    if (IS_NPC(k) && k->profs) {
        strcpy(buf, "Monster Prof: ");
        sprinttype(k->player.prof, npc_prof_types, buf2);
    } else {
        sprintf(buf, "Spec:(%d) %s", GET_SPEC(k), specialize_name[GET_SPEC(k)]);
    }

    sprintf(buf2, ", Lev: [%s%2d%s], XP: [%s%7d%s], Align: [%4d]\n\r",
        CC_FIX(ch, CYEL), GET_LEVEL(k), CC_NORM(ch),
        CC_FIX(ch, CYEL), GET_EXP(k), CC_NORM(ch),
        GET_ALIGNMENT(k));
    strcat(buf, buf2);
    send_to_char(buf, ch);

    if (!IS_NPC(k)) {
        strcpy(buf1, (char*)asctime(localtime(&(k->player.time.birth))));
        strcpy(buf2, (char*)asctime(localtime(&(k->player.time.logon))));
        buf1[10] = buf2[10] = '\0';

        sprintf(buf, "Created: [%s], Last Logon: [%s], Played [%dh %dm], Age [%d]\n\r",
            buf1, buf2, k->player.time.played / 3600,
            ((k->player.time.played / 60) % 60), age(k).year);
        send_to_char(buf, ch);

        sprintf(buf, "Hometown: [%d], Speaks: [%d/%d/%d], (pracs left:[%d])\n\r",
            k->player.hometown, k->player.talks[0], k->player.talks[1],
            k->player.talks[2], SPELLS_TO_LEARN(k));
        send_to_char(buf, ch);
    } else {
        sprintf(buf, "Exists for %ld ticks, Difficulty %d.\n\r", MOB_AGE_TICKS(k, time(0)), GET_DIFFICULTY(k));
        send_to_char(buf, ch);
    }
    sprintf(buf, "Str:[%d/%d/%d] Int:[%d/%d/%d] Wil:[%d/%d/%d] Dex:[%d/%d/%d] Con: [%d/%d/%d] Lea:[%d/%d/%d]\n\r",
        GET_STR(k), GET_STR_BASE(k), (k)->constabilities.str,
        GET_INT(k), GET_INT_BASE(k), (k)->constabilities.intel,
        GET_WILL(k), GET_WILL_BASE(k), (k)->constabilities.wil,
        GET_DEX(k), GET_DEX_BASE(k), (k)->constabilities.dex,
        GET_CON(k), GET_CON_BASE(k), (k)->constabilities.con,
        GET_LEA(k), GET_LEA_BASE(k), (k)->constabilities.lea);
    send_to_char(buf, ch);

    sprintf(buf, "HP :[%d/%d+%0.f(%0.f)]  Stamina :[%d/%d+%0.f(%0.f)]  Move :[%d/%d+%0.f(%0.f)] Spirit:[%d/%d+%d]   Consts(hit/stamina/move): %d/%d/%d\n\r",
        GET_HIT(k), GET_MAX_HIT(k), hit_gain(k), get_bonus_hit_gain(k),
        GET_MANA(k), GET_MAX_MANA(k), mana_gain(k), get_bonus_mana_gain(k),
        GET_MOVE(k), GET_MAX_MOVE(k), move_gain(k), get_bonus_move_gain(k),
        utils::get_spirits(k), 0, 0, /*GET_MAX_SPIRIT(k), spirit_gain(k),*/
        (k)->constabilities.hit, (k)->constabilities.mana, (k)->constabilities.move);
    send_to_char(buf, ch);
    sprintf(buf, "Encumbrance %d, Leg_encu %d, Perception %d, Willpower %d,\n\r", utils::get_encumbrance(*k), utils::get_leg_encumbrance(*k), GET_PERCEPTION(k), GET_WILLPOWER(k));
    send_to_char(buf, ch);
    sprintf(buf, "Coins: [%9d]\n\r", GET_GOLD(k));
    send_to_char(buf, ch);
    sprintf(buf, " OB: %d(%d), parry: %d(%d), dodge: %d(%d), c_parry %d,  Saving throws %d,\n\r Mood %d, ambush awareness %d, rerolls done: %d, absorb %d\n\r",
        get_real_OB(k), GET_OB(k), get_real_parry(k), GET_PARRY(k), get_real_dodge(k), GET_DODGE(k), GET_CURRENT_PARRY(k), k->specials2.saving_throw, GET_TACTICS(k), GET_AMBUSHED(k), GET_REROLLS(k), get_percent_absorb(ch));
    send_to_char(buf, ch);

    sprintf(buf, "ENERGY: %d, ENE_regen: %d, damage: %d, null_speed: %d, str_speed %d\n\r",
        k->specials.ENERGY, utils::get_energy_regen(*k), k->points.damage, k->specials.null_speed, k->specials.str_speed);
    send_to_char(buf, ch);

    if (!utils::is_npc(*k)) {
        {
            float health_regen = hit_gain(k);
            float mana_regen = mana_gain(k);
            float move_regen = move_gain(k);
            player_spec::battle_mage_handler battle_mage_handler(k);
            sprintf(buf, "Spell_Pen: %d, Spell_Pow: %d, Hit_Gain: %.0f, Stam_Gain: %.0f, Mov_Gain: %.0f\n\r",
                battle_mage_handler.get_bonus_spell_pen(k->points.get_spell_pen()),
                battle_mage_handler.get_bonus_spell_power(k->points.get_spell_power()),
                health_regen, mana_regen, move_regen);
            send_to_char(buf, ch);
        }
    }

    sprinttype(GET_POS(k), position_types, buf2);
    sprintf(buf, "Pos: %s, Fighting: %s", buf2,
        ((k->specials.fighting) ? GET_NAME(k->specials.fighting) : "Nobody"));
    if (k->desc) {
        sprinttype(k->desc->connected, connected_types, buf2);
        strcat(buf, ", Connected: ");
        strcat(buf, buf2);
    }
    sprintf(buf, "%s  Hide_value:%d.\n\r", buf, k->specials.hide_value);
    send_to_char(buf, ch);

    strcpy(buf, "Default position: ");
    sprinttype((k->specials.default_pos), position_types, buf2);
    strcat(buf, buf2);

    sprintf(buf2, ", Idle Timer (in tics) [%d]\n\r", k->specials.timer);
    strcat(buf, buf2);
    send_to_char(buf, ch);

    if (IS_NPC(k)) {
        sprintbit(MOB_FLAGS(k), action_bits, buf2, 0);
        sprintf(buf, "NPC flags: (%ld) %s, agg flag: %ld, will_teach: %d\n\r", MOB_FLAGS(k),
            buf2, (k)->specials2.pref, (k)->specials2.will_teach);
        send_to_char(buf, ch);
    } else {
        sprintbit(PLR_FLAGS(k), player_bits, buf2, 0);
        sprintf(buf, "PLR: %s\n\r", buf2);
        send_to_char(buf, ch);
        sprintbit(PRF_FLAGS(k), preference_bits, buf2, 0);
        sprintf(buf, "PRF: %s\n\r", buf2);
        send_to_char(buf, ch);
        sprintf(buf, "rp_flag: %d\n\r", k->specials2.rp_flag);
        send_to_char(buf, ch);
    }

    if (IS_MOB(k)) {
        sprintf(buf, "Mob Spec-Proc: %s,  Asima: %d; Script: %d; ",
            (mob_index[k->nr].func ? "Exists" : "None"),
            ((!MOB_FLAGGED(k, MOB_SPEC) && (k->specials.union1.prog_number)) ? mobile_program_zone[PROG_NUMBER(k)] : -1), k->specials.script_number);

        send_to_char(buf, ch);
    }
    sprintf(buf, "Special prog_number: %d, Callmask: %d\n\rCarried: weight: %d, items: %d; ",
        k->specials.store_prog_number, (IS_NPC(k) ? CALL_MASK(k) : -1),
        IS_CARRYING_W(k), IS_CARRYING_N(k));

    for (i = 0, j = k->carrying; j; j = j->next_content, i++)
        ;
    sprintf(buf, "%sItems in: inventory: %d, ", buf, i);

    for (i = 0, i2 = 0; i < MAX_WEAR; i++)
        if (k->equipment[i])
            i2++;
    sprintf(buf2, "eq: %d\n\r", i2);
    strcat(buf, buf2);
    send_to_char(buf, ch);

    sprintf(buf, "Hunger: %d, Thirst: %d, Drunk: %d, Att.Level: %d\n\r",
        GET_COND(k, FULL), GET_COND(k, THIRST), GET_COND(k, DRUNK),
        k->specials.attacked_level);
    send_to_char(buf, ch);

    sprintf(buf, "Master is: %s, Followers are:",
        ((k->master) ? GET_NAME(k->master) : "<none>"));

    for (fol = k->followers; fol; fol = fol->next) {
        sprintf(buf2, "%s %s", found++ ? "," : "", GET_NAME(fol->follower));
        strcat(buf, buf2);
        if (strlen(buf) >= 62) {
            if (fol->next)
                send_to_char(strcat(buf, ",\n\r"), ch);
            else
                send_to_char(strcat(buf, "\n\r"), ch);
            *buf = found = 0;
        }
    }

    if (*buf)
        send_to_char(strcat(buf, "\n\r"), ch);

    sprintf(buf, "Delay command:%d delay_value:%d\n\r", k->delay.cmd, k->delay.wait_value);
    send_to_char(buf, ch);

    if (!IS_NPC(k) || !k->specials.memory)
        strcpy(buf, "No special memories.\n\r");
    else {
        strcpy(buf, "Memories: ");
        for (tmprec = k->specials.memory; tmprec; tmprec = tmprec->next_on_mob)
            sprintf(buf, "%s %ld", buf, tmprec->id);
        strcat(buf, "\n\r");
    }
    send_to_char(buf, ch);

    /* Showing the bitvector */
    sprintbit(k->specials.affected_by, affected_bits, buf2, 0);
    sprintf(buf, "AFF: %s%s%s\n\r", CC_FIX(ch, CYEL), buf2, CC_NORM(ch));
    send_to_char(buf, ch);

    sprintbit(GET_RESISTANCES(k), resistance_name, buf2, 0);
    sprintf(buf, "RES: %s%s%s ", CC_FIX(ch, CYEL), buf2, CC_NORM(ch));
    send_to_char(buf, ch);

    sprintbit(GET_VULNERABILITIES(k), vulnerability_name, buf2, 0);
    sprintf(buf, "VUL: %s%s%s\n\r", CC_FIX(ch, CYEL), buf2, CC_NORM(ch));
    send_to_char(buf, ch);

    /* Routine to show what spells a char is affected by */
    if (k->affected) {
        for (aff = k->affected; aff; aff = aff->next) {
            *buf2 = '\0';
            sprintf(buf, "SPL: (%3dhr) %s%-21s%s ", aff->duration + 1,
                CC_FIX(ch, CCYN), skills[aff->type].name, CC_NORM(ch));
            if (aff->modifier) {
                sprintf(buf2, "%+d to %s", aff->modifier, apply_types[(int)aff->location]);
                strcat(buf, buf2);
            }
            if (aff->bitvector) {
                if (*buf2)
                    strcat(buf, ", sets ");
                else
                    strcat(buf, "sets ");
                sprintbit(aff->bitvector, affected_bits, buf2, 0);
                strcat(buf, buf2);
            }
            send_to_char(strcat(buf, "\n\r"), ch);
        }
    }
}

int Check_zone_authority(int zonenum, char_data* ch)
{
    struct owner_list* tmpowner;
    int tmp;

    for (tmp = 0; (tmp <= top_of_zone_table) && (zone_table[tmp].number != zonenum); tmp++)
        ;
    if (tmp > top_of_zone_table)
        return 0;

    tmpowner = zone_table[tmp].owners;
    if ((tmpowner->owner == 0) || (zonenum == 11) || (GET_LEVEL(ch) >= LEVEL_AREAGOD))
        return 1;
    while (tmpowner->owner != 0) {
        if (tmpowner->owner == ch->specials2.idnum)
            return 1;
        tmpowner = tmpowner->next;
    }
    return 0;
}

ACMD(do_zone)
{
    int zonnum, tmp, numname;
    struct owner_list* tmpowner;
    char tmpstr[255];

    zonnum = 0;
    zonnum = atoi(argument);
    if (zonnum == 0)
        zonnum = (int)(world[ch->in_room].number) / 100;

    for (tmp = 0; (tmp <= top_of_zone_table) && (zone_table[tmp].number != zonnum); tmp++)
        ;

    if (tmp > top_of_zone_table) {
        send_to_char("No such zone in the world.\n\r", ch);
        return;
    }

    sprintf(buf, "Zone #%d: %s\n\r", zonnum, zone_table[tmp].name);
    send_to_char(buf, ch);
    sprintf(buf, "Owners: ");
    tmpowner = zone_table[tmp].owners;
    if (tmpowner->owner == 0)
        strcat(buf, "All");
    else
        while (tmpowner->owner != 0) {
            numname = find_player_in_table("", tmpowner->owner);
            if (numname != -1) {
                sprintf(tmpstr, "%s", (player_table + numname)->name);
            } else
                sprintf(tmpstr, "lost(%d)", tmpowner->owner);
            *tmpstr = toupper(*tmpstr);
            strcat(buf, " ");
            strcat(buf, tmpstr);
            tmpowner = tmpowner->next;
        }
    strcat(buf, ".\n\r");
    send_to_char(buf, ch);
    sprintf(buf, "Coordinates: (%d, %d), symbol '%c' Level %d\n\r",
        zone_table[tmp].x, zone_table[tmp].y, zone_table[tmp].symbol,
        zone_table[tmp].level);
    send_to_char(buf, ch);
    send_to_char(zone_table[tmp].description, ch);
    send_to_char("\n\r------------------------------------------\n\r", ch);
    send_to_char(zone_table[tmp].map, ch);
    return;
}

ACMD(do_wizstat)
{
    struct char_data* victim = 0;
    struct obj_data* object = 0;
    struct char_file_u tmp_store;

    if (IS_NPC(ch))
        return;

    half_chop(argument, buf1, buf2);

    if (!*buf1) {
        send_to_char("Stats on who or what?\n\r", ch);
        return;
    } else if (is_abbrev(buf1, "room")) {
        do_stat_room(ch);
    } else if (is_abbrev(buf1, "mob")) {
        if (!*buf2) {
            send_to_char("Stats on which mobile?\n\r", ch);
        } else {
            if ((victim = get_char_vis(ch, buf2))) {
                do_stat_character(ch, victim);
            } else
                send_to_char("No such mobile around.\n\r", ch);
        }
    } else if (is_abbrev(buf1, "player")) {
        if (!*buf2) {
            send_to_char("Stats on which player?\n\r", ch);
        } else {
            if ((victim = get_player_vis(ch, buf2)))
                do_stat_character(ch, victim);
            else
                send_to_char("No such player around.\n\r", ch);
        }
    } else if (is_abbrev(buf1, "file")) {
        if (!*buf2) {
            send_to_char("Stats on which player?\n\r", ch);
        } else {
            CREATE(victim, struct char_data, 1);
            clear_char(victim, MOB_VOID);
            if (load_char(buf2, &tmp_store) > -1) {
                store_to_char(&tmp_store, victim);
                if (GET_LEVEL(victim) > GET_LEVEL(ch))
                    send_to_char("Sorry, you can't do that.\n\r", ch);
                else
                    do_stat_character(ch, victim);
                free_char(victim);
            } else {
                send_to_char("There is no such player.\n\r", ch);
                RELEASE(victim);
            }
        }
    } else if (is_abbrev(buf1, "object")) {
        if (!*buf2) {
            send_to_char("Stats on which object?\n\r", ch);
        } else {
            if ((object = get_obj_vis(ch, buf2)))
                do_stat_object(ch, object);
            else
                send_to_char("No such object around.\n\r", ch);
        }
    } else {
        if ((victim = get_char_vis(ch, buf1)))
            do_stat_character(ch, victim);
        else if ((object = get_obj_vis(ch, buf1)))
            do_stat_object(ch, object);
        else
            send_to_char("Nothing around by that name.\n\r", ch);
    }
}

ACMD(do_shutdown)
{
    extern int circle_shutdown, circle_reboot;

    if (IS_NPC(ch))
        return;

    if (subcmd != SCMD_SHUTDOWN) {
        send_to_char("If you want to shut something down, say so!\n\r", ch);
        return;
    }

    one_argument(argument, arg);

    if (!*arg) {
        sprintf(buf, "(GC) Shutdown by %s. \n\r", GET_NAME(ch));
        send_to_all(buf);
        log(buf);
        circle_shutdown = 1;
    } else if (!str_cmp(arg, "reboot")) {
        sprintf(buf, "(GC) Reboot by %s.", GET_NAME(ch));
        log(buf);
        send_to_all("Rebooting... come back in a minute or two.");
        circle_shutdown = circle_reboot = 1;
    } else if (!str_cmp(arg, "die")) {
        sprintf(buf, "(GC) Shutdown by %s. \n\r", GET_NAME(ch));
        send_to_all(buf);
        log(buf);
        system("touch ../.killscript");
        circle_shutdown = 1;
    } else if (!str_cmp(arg, "pause")) {
        sprintf(buf, "(GC) Shutdown by %s. \n\r", GET_NAME(ch));
        send_to_all(buf);
        log(buf);
        system("touch ../pause");
        circle_shutdown = 1;
    } else
        send_to_char("Unknown shutdown option.\n\r", ch);
}

ACMD(do_snoop)
{
    struct char_data* victim;

    if (!ch->desc)
        return;

    one_argument(argument, arg);

    if (!*arg) {
        send_to_char("Snoop who?\n\r", ch);
        return;
    }

    if (!(victim = get_char_vis(ch, arg))) {
        send_to_char("No such person around.\n\r", ch);
        return;
    }

    if (!victim->desc) {
        send_to_char("There's no link.. nothing to snoop.\n\r", ch);
        return;
    }

    if (victim == ch) {
        send_to_char("Ok, you just snoop yourself.\n\r", ch);
        if (ch->desc->snoop.snooping) {
            ch->desc->snoop.snooping->desc->snoop.snoop_by = 0;
            ch->desc->snoop.snooping = 0;
        }
        return;
    }

    if (victim->desc->snoop.snoop_by) {
        send_to_char("Busy already. \n\r", ch);
        return;
    }

    if ((GET_LEVEL(victim) >= GET_LEVEL(ch)) && (GET_LEVEL(ch) != LEVEL_IMPL)) {
        send_to_char("You failed.\n\r", ch);
        return;
    }

    send_to_char("Ok. \n\r", ch);

    if (ch->desc->snoop.snooping)
        ch->desc->snoop.snooping->desc->snoop.snoop_by = 0;

    ch->desc->snoop.snooping = victim;
    victim->desc->snoop.snoop_by = ch;
    return;
}

ACMD(do_switch)
{
    struct char_data* victim;

    if (IS_NPC(ch))
        return;

    one_argument(argument, arg);

    if (!*arg) {
        send_to_char("Switch with who?\n\r", ch);
    } else {
        if (!(victim = get_char(arg)))
            send_to_char("They aren't here.\n\r", ch);
        else {
            if (ch == victim) {
                send_to_char("He he he... We are jolly funny today, eh?\n\r", ch);
                return;
            }

            if (!ch->desc || ch->desc->snoop.snoop_by || ch->desc->snoop.snooping) {
                send_to_char("Mixing snoop & switch is bad for your health.\n\r", ch);
                return;
            }

            if (victim->desc || (!IS_NPC(victim))) {
                send_to_char("You can't do that, the body is already in use!\n\r", ch);
            } else {
                send_to_char("Ok.\n\r", ch);

                ch->desc->character = victim;
                ch->desc->original = ch;

                victim->desc = ch->desc;
                ch->desc = 0;
            }
        }
    }
}

ACMD(do_return)
{
    if (!ch->desc)
        return;

    if (!ch->desc->original) {
        send_to_char("Yeah, right...\n\r", ch);
        return;
    } else {
        send_to_char("You return to your original body.\n\r", ch);

        ch->desc->character = ch->desc->original;
        ch->desc->original = 0;

        ch->desc->character->desc = ch->desc;
        ch->desc = 0;
    }
}

ACMD(do_load)
{
    struct char_data* mob;
    struct obj_data* obj;
    int number, r_num;

    if (IS_NPC(ch))
        return;

    argument_interpreter(argument, buf, buf2);

    if (!*buf || !*buf2 || !isdigit(*buf2)) {
        send_to_char("Usage: load { obj | mob } <number>\n\r", ch);
        return;
    }

    if ((number = atoi(buf2)) < 0) {
        send_to_char("A NEGATIVE number??\n\r", ch);
        return;
    }
    if (is_abbrev(buf, "mob")) {
        if ((r_num = real_mobile(number)) < 0) {
            send_to_char("There is no monster with that number.\n\r", ch);
            return;
        }
        mob = read_mobile(r_num, REAL);
        //      printf("mob created %p, %s\n",mob, GET_NAME(mob));
        char_to_room(mob, ch->in_room);
        act("$n makes a quaint, magical gesture with one hand.", TRUE, ch,
            0, 0, TO_ROOM);
        act("$n has created $N!", FALSE, ch, 0, mob, TO_ROOM);
        act("You create $N.", FALSE, ch, 0, mob, TO_CHAR);
    } else if (is_abbrev(buf, "obj")) {
        if ((r_num = real_object(number)) < 0) {
            send_to_char("There is no object with that number.\n\r", ch);
            return;
        }
        obj = read_object(r_num, REAL);
        if (obj) {
            obj_to_char(obj, ch);

            // Someone created this, so it's been touched.
            obj->touched = 1;
            obj->loaded_by = ch->specials2.idnum;
        }
        act("$n makes a strange magical gesture.", TRUE, ch, 0, 0, TO_ROOM);
        //      act("$n has created $p!", FALSE, ch, obj, 0, TO_ROOM);
        act("You create $p.", FALSE, ch, obj, 0, TO_CHAR);
    } else
        send_to_char("That'll have to be either 'obj' or 'mob'.\n\r", ch);
}

ACMD(do_vstat)
{
    struct char_data* mob;
    struct obj_data* obj;
    int number, r_num;

    if (IS_NPC(ch))
        return;

    argument_interpreter(argument, buf, buf2);

    if (!*buf || !*buf2 || !isdigit(*buf2)) {
        send_to_char("Usage: vstat { obj | mob } <number>\n\r", ch);
        return;
    }

    if ((number = atoi(buf2)) < 0) {
        send_to_char("A NEGATIVE number??\n\r", ch);
        return;
    }

    if (is_abbrev(buf, "mob")) {
        if ((r_num = real_mobile(number)) < 0) {
            send_to_char("There is no monster with that number.\n\r", ch);
            return;
        }
        mob = read_mobile(r_num, REAL);
        mob->in_room = ch->in_room;
        do_stat_character(ch, mob);
        mob->in_room = NOWHERE;
        extract_char(mob);
    } else if (is_abbrev(buf, "obj")) {
        if ((r_num = real_object(number)) < 0) {
            send_to_char("There is no object with that number.\n\r", ch);
            return;
        }
        obj = read_object(r_num, REAL);
        do_stat_object(ch, obj);
        extract_obj(obj);
    } else
        send_to_char("That'll have to be either 'obj' or 'mob'.\n\r", ch);
}

/* clean a room of all mobiles and objects */
ACMD(do_purge)
{
    struct char_data *vict, *next_v;
    struct obj_data *obj, *next_o;
    struct affected_type *tmpaf, *next_tmpaf;
    int my_zone, tmp;

    if (IS_NPC(ch) || (ch->in_room == NOWHERE))
        return;

    one_argument(argument, buf);

    if (!strcmp(buf, "zone")) {
        my_zone = world[ch->in_room].zone;

        for (vict = character_list; vict; vict = next_v) {
            next_v = vict->next;
            if (IS_NPC(vict) && (vict->in_room != NOWHERE) && (world[vict->in_room].zone == my_zone))
                extract_char(vict);
        }

        for (obj = object_list; obj; obj = next_o) {
            next_o = obj->next;
            if ((obj->in_room != NOWHERE) && (world[obj->in_room].zone == my_zone))
                extract_obj(obj);
        }
        sprintf(buf, "(GC) %s has purged zone %d.", GET_NAME(ch), zone_table[my_zone].number);
        mudlog(buf, BRF, LEVEL_GOD, TRUE);
        return;
    }

    if (*buf) /* argument supplied. destroy single object or char */ {
        if ((vict = get_char_room_vis(ch, buf))) {
            if (!IS_NPC(vict) && (GET_LEVEL(ch) <= GET_LEVEL(vict))) {
                send_to_char("Fuuuuuuuuu!\n\r", ch);
                return;
            }

            /* TEMP fix to save players when they're purged */
            if (!IS_NPC(vict))
                Crash_idlesave(ch);

            act("$n disintegrates $N.", FALSE, ch, 0, vict, TO_NOTVICT);

            for (tmp = 0; tmp < MAX_WEAR; tmp++)
                if (vict->equipment[tmp])
                    extract_obj(unequip_char(vict, tmp));

            for (tmp = 0; (tmp < 1000) && vict->carrying; tmp++) {
                obj = vict->carrying;
                obj_from_char(vict->carrying);
                extract_obj(obj);
            }

            if (IS_NPC(vict)) {
                extract_char(vict);
            } else {
                sprintf(buf, "(GC) %s has purged %s.", GET_NAME(ch), GET_NAME(vict));
                mudlog(buf, BRF, LEVEL_GOD, TRUE);
                if (vict->desc && vict->desc->descriptor) {
                    close_socket(vict->desc);
                    vict->desc = 0;
                }
                extract_char(vict);
            }
        } else if ((obj = get_obj_in_list_vis(ch, buf, world[ch->in_room].contents, 9999))) {
            act("$n destroys $p.", FALSE, ch, obj, 0, TO_ROOM);
            extract_obj(obj);
        } else {
            send_to_char("I don't know anyone or anything by that name.\n\r", ch);
            return;
        }

        send_to_char("Ok.\n\r", ch);
    } else { /* no argument. clean out the room */
        if (IS_NPC(ch)) {
            send_to_char("Don't... You would only kill yourself..\n\r", ch);
            return;
        }

        act("$n gestures... You are surrounded by scorching flames!",
            FALSE, ch, 0, 0, TO_ROOM);
        send_to_room("The world seems a little cleaner.\n\r", ch->in_room);

        for (vict = world[ch->in_room].people; vict; vict = next_v) {
            next_v = vict->next_in_room;
            if (IS_NPC(vict))
                extract_char(vict);
        }

        for (obj = world[ch->in_room].contents; obj; obj = next_o) {
            next_o = obj->next_content;
            extract_obj(obj);
        }

        for (tmpaf = world[ch->in_room].affected; tmpaf; tmpaf = next_tmpaf) {
            next_tmpaf = tmpaf->next;
            if (!IS_SET(tmpaf->bitvector, PERMAFFECT))
                affect_remove_room(&world[ch->in_room], tmpaf);
        }
    }
}

ACMD(do_advance)
{
    struct char_data* victim;
    char name[100], level[100];
    int adv, newlevel = 0;

    void gain_exp(struct char_data*, int);
    int advance_perm(struct char_data*, struct char_data*, int);

    if (IS_NPC(ch))
        return;

    half_chop(argument, name, buf);
    one_argument(buf, level);

    if (*name) {
        if (!(victim = get_char_vis(ch, name))) {
            send_to_char("That player is not here.\n\r", ch);
            return;
        }
    } else {
        send_to_char("Advance who?\n\r", ch);
        return;
    }

    if (ch == victim) {
        send_to_char("Maybe that's not such a great idea.\n\r", ch);
        return;
    }

    if (IS_NPC(victim)) {
        send_to_char("NO!  Not on NPC's.\n\r", ch);
        return;
    }

    if (GET_LEVEL(victim) == 0)
        adv = 1;
    else if (!*level) {
        send_to_char("You must supply a level number.\n\r", ch);
        return;
    } else {
        if (!isdigit(*level)) {
            send_to_char("Second argument must be a positive integer.\n\r", ch);
            return;
        }
        newlevel = atoi(level);
        adv = newlevel - GET_LEVEL(victim);
    }

    if (!advance_perm(ch, victim, newlevel))
        return;

    if (GET_LEVEL(victim) >= LEVEL_IMMORT) {
        victim->player.race = 0;
    }

    if ((adv + GET_LEVEL(victim)) > LEVEL_IMPL) {
        send_to_char("100 is the highest possible level.\n\r", ch);
        return;
    }
    act("$n makes some strange gestures.\n\rA strange feeling comes upon you,"
        "\n\rLike a giant hand, light comes down from\n\rabove, grabbing your"
        "body, that begins\n\rto pulse with colored lights from inside.\n\rYo"
        "ur head seems to be filled with demons\n\rfrom another plane as your"
        " body dissolves\n\rto the elements of time and space itself.\n\rSudde"
        "nly a silent explosion of light snaps\n\ryou back to reality.  You fee"
        "l slightly\n\rdifferent.",
        FALSE, ch, 0, victim, TO_VICT);

    send_to_char("Ok.\n\r", ch);

    if (GET_LEVEL(victim) == 0) {
        do_start(victim);
    } else {
        if (GET_LEVEL(victim) < LEVEL_IMPL) {
            sprintf(buf, "(GC) %s has advanced %s to level %d (from %d)",
                GET_NAME(ch), GET_NAME(victim), newlevel, GET_LEVEL(victim));
            log(buf);
            if (adv > 0)
                gain_exp_regardless(victim, (xp_to_level(GET_LEVEL(victim) + adv) - GET_EXP(victim)));

            if (adv < 0) {
                int n;

                GET_LEVEL(victim) = newlevel;
                GET_MINI_LEVEL(victim) = 100 * newlevel;
                GET_EXP(victim) = xp_to_level(GET_LEVEL(victim));
                SPELLS_TO_LEARN(victim) += adv * PRACS_PER_LEVEL + (adv * GET_LEA_BASE(victim)) / LEA_PRAC_FACTOR;
                for (n = 1; n <= MAX_PROFS; n++)
                    SET_PROF_LEVEL(n, victim, ((int)(GET_PROF_COOF(n, victim) * MIN(GET_MINI_LEVEL(victim), 3000)) / 100000));
            }

            save_char(victim, NOWHERE, 0);
        }
    }
}

ACMD(do_restore)
{
    struct char_data* victim;
    int i;

    void update_pos(struct char_data * victim);

    if (IS_NPC(ch))
        return;

    one_argument(argument, buf);
    if (!*buf)
        send_to_char("Whom do you wish to restore?\n\r", ch);
    else if (!(victim = get_char(buf)))
        send_to_char("No-one by that name in the world.\n\r", ch);
    else if (RETIRED(ch) && (victim != ch))
        send_to_char("Permission denied.\n\r", ch);
    else {

        if ((GET_LEVEL(ch) >= LEVEL_GRGOD) && (GET_LEVEL(victim) >= LEVEL_IMMORT)) {
            for (i = 0; i < MAX_SKILLS; i++)
                SET_KNOWLEDGE(victim, i, 100);
        }
        affect_total(victim);
        victim->tmpabilities = victim->abilities;
        update_pos(victim);
        send_to_char("Done.\n\r", ch);
        act("You have been fully healed by $N!", FALSE, victim, 0, ch, TO_CHAR);
    }
}

ACMD(do_invis)
{
    int level;

    if (IS_NPC(ch)) {
        send_to_char("Yeah.. like a mob knows how to bend light.\n\r", ch);
        return;
    }

    one_argument(argument, arg);
    if (!*arg) {
        if (GET_INVIS_LEV(ch) > 0) {
            GET_INVIS_LEV(ch) = 0;
            sprintf(buf, "You are now fully visible.\n\r");
        } else {
            GET_INVIS_LEV(ch) = GET_LEVEL(ch);
            sprintf(buf, "Your invisibility level is %d.\n\r", GET_LEVEL(ch));
        }
    } else {
        level = atoi(arg);
        if (level > GET_LEVEL(ch)) {
            send_to_char("You can't go invisible above your own level.\n\r", ch);
            return;
        } else if (level < 1) {
            GET_INVIS_LEV(ch) = 0;
            sprintf(buf, "You are now fully visible.\n\r");
        } else {
            GET_INVIS_LEV(ch) = level;
            sprintf(buf, "Your invisibility level is now %d.\n\r", level);
        }
    }
    send_to_char(buf, ch);
}

ACMD(do_gecho)
{
    int i;
    struct descriptor_data* pt;

    if (IS_NPC(ch))
        return;

    for (i = 0; *(argument + i) == ' '; i++)
        ;

    if (!*(argument + i))
        send_to_char("That must be a mistake...\n\r", ch);
    else {
        sprintf(buf, "%s\n\r", argument + i);
        for (pt = descriptor_list; pt; pt = pt->next)
            if (!pt->connected && pt->character && pt->character != ch) {
                //	    act(buf, FALSE, ch, 0, pt->character, TO_VICT);
                send_to_char(buf, pt->character);
            }
        if (!PRF_FLAGGED(ch, PRF_ECHO))
            send_to_char("Ok.\n\r", ch);
        else
            send_to_char(buf, ch);
    }
}

ACMD(do_poofset)
{
    char** msg;
    int i;

    switch (subcmd) {
    case SCMD_POOFIN:
        msg = &(ch->specials.poofIn);
        break;
    case SCMD_POOFOUT:
        msg = &(ch->specials.poofOut);
        break;
    default:
        return;
        break;
    }

    for (i = 0; *(argument + i) == ' '; i++)
        ;

    RELEASE(*msg);

    if (!*(argument + i))
        *msg = NULL;
    else
        *msg = str_dup(argument + i);

    send_to_char("Ok.\n\r", ch);
}

ACMD(do_dc)
{
    struct descriptor_data* d;
    int num_to_dc;

    if (IS_NPC(ch)) {
        send_to_char("Monsters can't cut connections... leave me alone.\n\r", ch);
        return;
    }

    if (!(num_to_dc = atoi(argument))) {
        send_to_char("Usage: DC <connection number> (type USERS for a list)\n\r", ch);
        return;
    }

    for (d = descriptor_list; d && d->desc_num != num_to_dc; d = d->next)
        ;

    if (!d) {
        send_to_char("No such connection.\n\r", ch);
        return;
    }

    if (d->character && GET_LEVEL(d->character) >= GET_LEVEL(ch)) {
        send_to_char("Umm.. maybe that's not such a good idea...\n\r", ch);
        return;
    }

    close_socket(d);
    sprintf(buf, "Connection #%d closed.\n\r", num_to_dc);
    send_to_char(buf, ch);
    sprintf(buf, "(GC) Connection closed by %s.", GET_NAME(ch));
    log(buf);
}

char* wizlock_msg = 0; /* wizlock message              */

ACMD(do_wizlock)
{
    int value;
    char* when;
    extern char* wizlock_default;

    half_chop(argument, buf, buf2);
    if (*buf) {
        value = atoi(buf);
        if (value < 0 || value > LEVEL_IMPL) {
            send_to_char("Invalid wizlock value.\n\r", ch);
            return;
        }
        restrict = value;
        when = "now";
    } else
        when = "currently";

    if (*buf2) {
        wizlock_msg = (char*)malloc(strlen(buf2) + 3);
        sprintf(wizlock_msg, "%s\n\r", buf2);
    } else {
        wizlock_msg = (char*)malloc(strlen(wizlock_default) + 1);
        sprintf(wizlock_msg, "%s", wizlock_default);
    }

    switch (restrict) {
    case 0:
        sprintf(buf, "The game is %s completely open.\n", when);
        break;
    case 1:
        sprintf(buf, "The game is %s closed to new players.\n", when);
        break;
    default:
        sprintf(buf, "Only level %d and above may enter the game %s.\n",
            restrict, when);
        break;
    }
    send_to_char(buf, ch);
    if (restrict != 0)
        sprintf(buf, "Message set to:  %s", wizlock_msg);
    send_to_char(buf, ch);
}

ACMD(do_date)
{
    long ct;
    char* tmstr;
    extern long boot_time;

    if (IS_NPC(ch))
        return;

    ct = time(0);
    tmstr = (char*)asctime(localtime(&ct));
    *(tmstr + strlen(tmstr) - 1) = '\0';
    sprintf(buf, "Current machine time: %s\n\r", tmstr);
    send_to_char(buf, ch);
    sprintf(buf, "Last reboot on: %s\n\r", asctime(localtime(&boot_time)));
    send_to_char(buf, ch);
}

ACMD(do_uptime)
{
    char* tmstr;
    long uptime;
    int d, h, m;

    extern long boot_time;
    extern char* lastdeath;

    if (IS_NPC(ch))
        return;

    tmstr = (char*)asctime(localtime(&boot_time));
    *(tmstr + strlen(tmstr) - 1) = '\0';

    uptime = time(0) - boot_time;
    d = uptime / 86400;
    h = (uptime / 3600) % 24;
    m = (uptime / 60) % 60;

    sprintf(buf, "Up since %s: %d day%s, %d:%02d\n\r", tmstr, d,
        ((d == 1) ? "" : "s"), h, m);

    send_to_char(buf, ch);
    send_to_char("The last minutes of the previous run:\n\r", ch);
    send_to_char(lastdeath, ch);
}

ACMD(do_last)
{
    struct char_file_u chdata;
    extern char* race_abbrevs[];

    if (IS_NPC(ch))
        return;

    if (!*argument) {
        send_to_char("For whom do you wish to search?\n\r", ch);
        return;
    }

    one_argument(argument, arg);
    if (load_char(arg, &chdata) < 0) {
        send_to_char("There is no such player.\n\r", ch);
        return;
    }
    if ((chdata.level == LEVEL_IMPL) && (GET_LEVEL(ch) < LEVEL_GRGOD)) {
        send_to_char("Last someone your own size!\n\r", ch);
        return;
    }

    if (chdata.race >= MAX_RACES)
        chdata.race = 17;

    sprintf(buf, "[%5ld] [%2d %s] %-12s : %-18s : %-20s\n\r",
        chdata.specials2.idnum, chdata.level, race_abbrevs[(int)chdata.race],
        chdata.name, chdata.host, ctime(&chdata.last_logon));
    send_to_char(buf, ch);
}

ACMD(do_force)
{
    struct descriptor_data* i;
    struct char_data* vict;
    char name[100], to_force[MAX_INPUT_LENGTH + 2];

    if (IS_NPC(ch)) {
        send_to_char("Umm.... no.\n\r", ch);
        return;
    }

    half_chop(argument, name, to_force);

    sprintf(buf1, "%s has forced you to %s.\n\r", GET_NAME(ch), to_force);
    sprintf(buf2, "Someone has forced you to %s.\n\r", to_force);

    if (!*name || !*to_force)
        send_to_char("Whom do you wish to force do what?\n\r", ch);
    else if (str_cmp("all", name) && str_cmp("room", name)) {
        if (!(vict = get_char_vis(ch, name)) || !CAN_SEE(ch, vict))
            send_to_char("No-one by that name here...\n\r", ch);
        else {
            if (GET_LEVEL(ch) > GET_LEVEL(vict)) {
                send_to_char("Ok.\n\r", ch);
                if (CAN_SEE(vict, ch) && GET_LEVEL(ch) < LEVEL_IMPL)
                    send_to_char(buf1, vict);
                else if (GET_LEVEL(ch) < LEVEL_IMPL) {
                    send_to_char(buf2, vict);
                }
                if (GET_LEVEL(ch) < LEVEL_IMPL) {
                    sprintf(buf, "(GC) %s forced %s to %s", GET_NAME(ch), name, to_force);
                    log(buf);
                }
                command_interpreter(vict, to_force);
            } else
                send_to_char("No, no, no!\n\r", ch);
        }
    } else if (str_cmp("room", name)) {
        send_to_char("Okay.\n\r", ch);
        if (GET_LEVEL(ch) < LEVEL_IMPL) {
            sprintf(buf, "(GC) %s forced %s to %s", GET_NAME(ch), name, to_force);
            log(buf);
        }
        for (i = descriptor_list; i; i = i->next)
            if (i->character != ch && !i->connected) {
                vict = i->character;
                if (GET_LEVEL(ch) > GET_LEVEL(vict)) {
                    if (CAN_SEE(vict, ch) && GET_LEVEL(ch) < LEVEL_IMPL)
                        send_to_char(buf1, vict);
                    else if (GET_LEVEL(ch) < LEVEL_IMPL)
                        send_to_char(buf2, vict);
                    command_interpreter(vict, to_force);
                }
            }
    } else {
        send_to_char("Okay.\n\r", ch);
        if (GET_LEVEL(ch) < LEVEL_IMPL) {
            sprintf(buf, "(GC) %s forced %s to %s", GET_NAME(ch), name, to_force);
            log(buf);
        }
        for (i = descriptor_list; i; i = i->next)
            if (i->character != ch && !i->connected && i->character->in_room == ch->in_room) {
                vict = i->character;
                if (GET_LEVEL(ch) > GET_LEVEL(vict)) {
                    if (CAN_SEE(vict, ch) && GET_LEVEL(ch) < LEVEL_IMPL)
                        send_to_char(buf1, vict);
                    else if (GET_LEVEL(ch) < LEVEL_IMPL)
                        send_to_char(buf2, vict);
                    command_interpreter(vict, to_force);
                }
            }
    }
}

ACMD(do_wiznet)
{
    struct descriptor_data* d;
    struct char_data* to_vict = 0;
    char emote = FALSE;
    int level = LEVEL_IMMORT;

    if (IS_NPC(ch)) {
        send_to_char("Yeah - like the Gods are interested in listening to mobs.\n\r", ch);
        return;
    }

    for (; *argument == ' '; argument++)
        ;

    if (!*argument)
        if (GET_LEVEL(ch) >= LEVEL_IMMORT) {
            send_to_char("Usage: wiznet <text> | #<level> <text> | *<emotetext> |\n\r "
                         "       wiznet @<person *<emotetext> | wiz @ | wiz - | wiz +\n\r",
                ch);
            return;
        } else {
            send_to_char("You whine pitifully to eternal powers.\n\r", ch);
            return;
        }
    if (GET_LEVEL(ch) >= LEVEL_IMMORT) /* petitioners can not use options */
        switch (*argument) {
        case '*':
            emote = TRUE;
            break;
        case '#':
            one_argument(argument + 1, buf1);
            if (is_number(buf1)) {
                half_chop(argument + 1, buf1, argument);
                level = MAX(atoi(buf1), LEVEL_IMMORT);
                if (level > GET_LEVEL(ch)) {
                    send_to_char("You can't wizline above your own level.\n\r", ch);
                    return;
                }
            } else if (emote)
                argument++;
            break;
        case '@':
            one_argument(argument + 1, buf1);

            if (!(to_vict = get_char_vis(ch, buf1))) {
                send_to_char("Petition to who?\n\r", ch);
                return;
            }

            half_chop(argument, buf1, argument);

            break;
        case '\\':
            ++argument;
            break;
        default:
            break;
        }
    if (!PRF_FLAGGED(ch, PRF_WIZ) && (GET_LEVEL(ch) >= LEVEL_IMMORT)) {
        send_to_char("You are offline!\n\r", ch);
        return;
    }

    for (; *argument && (*argument <= ' '); argument++)
        ;
    if (!*argument) {
        send_to_char("Don't bother the gods like that!\n\r", ch);
        return;
    }

    if (to_vict) {
        if (level > LEVEL_IMMORT) {
            sprintf(buf1, "%s %s <%d> (to %s) %s'%s'\n\r", GET_NAME(ch),
                (GET_LEVEL(ch) < LEVEL_IMMORT) ? "petitions" : "wiznets",
                level, GET_NAME(to_vict), emote ? "<--- " : "", argument);
            sprintf(buf2, "Someone %s <%d> (to %s) %s'%s'\n\r",
                (GET_LEVEL(ch) < LEVEL_IMMORT) ? "petitions" : "wiznets",
                level, GET_NAME(to_vict), emote ? "<--- " : "",
                argument);
        } else {
            sprintf(buf1, "%s %s (to %s) %s'%s'\n\r", GET_NAME(ch),
                (GET_LEVEL(ch) < LEVEL_IMMORT) ? "petitions" : "wiznets",
                GET_NAME(to_vict), emote ? "<--- " : "",
                argument);
            sprintf(buf2, "Someone %s (to %s) %s'%s'\n\r",
                (GET_LEVEL(ch) < LEVEL_IMMORT) ? "petitions" : "wiznets",
                GET_NAME(to_vict), emote ? "<--- " : "", argument);
        }
    } else {
        if (level > LEVEL_IMMORT) {
            sprintf(buf1, "%s %s <%d> %s'%s'\n\r", GET_NAME(ch),
                (GET_LEVEL(ch) < LEVEL_IMMORT) ? "petitions" : "wiznets",
                level, emote ? "<--- " : "", argument);
            sprintf(buf2, "Someone %s <%d> %s'%s'\n\r",
                (GET_LEVEL(ch) < LEVEL_IMMORT) ? "petitions" : "wiznets",
                level, emote ? "<--- " : "",
                argument);
        } else {
            sprintf(buf1, "%s %s %s'%s'\n\r", GET_NAME(ch),
                (GET_LEVEL(ch) < LEVEL_IMMORT) ? "petitions" : "wiznets",
                emote ? "<--- " : "",
                argument);
            sprintf(buf2, "Someone %s %s'%s'\n\r",
                (GET_LEVEL(ch) < LEVEL_IMMORT) ? "petitions" : "wiznets",
                emote ? "<--- " : "", argument);
        }
    }

    for (d = descriptor_list; d; d = d->next) {
        if ((!d->connected) && ((GET_LEVEL(d->character) >= level) || (to_vict && d->character == to_vict)) && (PRF_FLAGGED(d->character, PRF_WIZ)) && (!PLR_FLAGGED(d->character, PLR_WRITING | PLR_MAILING)) && (GET_POS(ch) != POSITION_SHAPING)
            && (d != ch->desc || (PRF_FLAGGED(d->character, PRF_ECHO)))) {
            send_to_char(CC_FIX(d->character, CCYN), d->character);
            if (CAN_SEE(d->character, ch))
                send_to_char(buf1, d->character);
            else
                send_to_char(buf2, d->character);
            send_to_char(CC_NORM(d->character), d->character);
        }
    }

    if (!PRF_FLAGGED(ch, PRF_ECHO))
        send_to_char("Ok.\n\r", ch);
}

ACMD(do_zreset)
{
    int i, j;

    if IS_NPC (ch) {
        send_to_char("Homie don't play that!\n\r", ch);
        return;
    }
    if (!*argument) {
        send_to_char("You must specify a zone.\n\r", ch);
        return;
    }
    one_argument(argument, arg);
    if (*arg == '*') {
        for (i = 0; i <= top_of_zone_table; i++)
            reset_zone(i);
        send_to_char("Reset world.\n\r", ch);
        return;
    } else if (*arg == '.')
        i = world[ch->in_room].zone;
    else {
        j = atoi(arg);
        for (i = 0; i <= top_of_zone_table; i++)
            if (zone_table[i].number == j)
                break;
    }
    if (i >= 0 && i <= top_of_zone_table) {
        reset_zone(i);
        sprintf(buf, "Reset zone %d: %s.\n\r", i, zone_table[i].name);
        send_to_char(buf, ch);
        sprintf(buf, "(GC) %s reset zone %d (%s)", GET_NAME(ch), zone_table[i].number, zone_table[i].name);
        mudlog(buf, NRM, (sh_int)MAX(LEVEL_GRGOD, GET_INVIS_LEV(ch)), TRUE);
    } else
        send_to_char("Invalid zone number.\n\r", ch);
}

/*
  General fn for wizcommands of the sort: cmd <player>
*/

struct
{
    char* field;
    int subcmd;
    int min_level;
} wizutil_options[] = {
    { "reroll", SCMD_REROLL, LEVEL_GRGOD },
    { "pardon", SCMD_PARDON, LEVEL_GOD + 1 }, // no longer used
    { "freeze", SCMD_FREEZE, LEVEL_FREEZE },
    { "notitle", SCMD_NOTITLE, LEVEL_GOD },
    { "thaw", SCMD_THAW, LEVEL_FREEZE },
    { "unaffect", SCMD_UNAFFECT, LEVEL_GOD + 1 },
    { "retire", SCMD_RETIRE, LEVEL_GOD + 1 },
    { "reactivate", SCMD_REACTV, LEVEL_GOD + 1 },
    { "rehash", SCMD_REHASH, LEVEL_GOD },
    { "noshout", SCMD_SQUELCH, LEVEL_AREAGOD },
    { "note", SCMD_NOTE, LEVEL_AREAGOD }
};

#define num_of_wizutils 11

ACMD(do_rehash);

ACMD(do_wizutil)
{
    struct char_data* vict;
    char name[40];
    long result, tmp;
    char* c;

    if (IS_NPC(ch)) {
        send_to_char("You're just an unfrozen caveman NPC.\n\r", ch);
        return;
    }
    if (subcmd == 0) { // the general wizutils command
        if (!wtl || (wtl->targ1.type != TARGET_TEXT)) {
            send_to_char("Wrong call to wizutils. Consult implementors, please.\n\r", ch);
            return;
        }
        for (tmp = 0; tmp < num_of_wizutils; tmp++)
            if (!str_cmp(wizutil_options[tmp].field, wtl->targ1.ptr.text->text))
                break;

        if ((tmp == num_of_wizutils) || (wtl->targ2.type != TARGET_CHAR)) {
            strcpy(buf, "The format is 'wizutil <field> <name>',\n\rPossible fields are:\n\r");
            for (tmp = 0; tmp < num_of_wizutils; tmp++)
                if (GET_LEVEL(ch) >= wizutil_options[tmp].min_level) {
                    strcat(buf, wizutil_options[tmp].field);
                    strcat(buf, ", ");
                }
            strcpy(buf + strlen(buf) - 2, ".\n\r");
            send_to_char(buf, ch);
            return;
        }
        if (GET_LEVEL(ch) < wizutil_options[tmp].min_level) {
            send_to_char("You are not godly enough for that.\n\r", ch);
            return;
        }
        subcmd = wizutil_options[tmp].subcmd;
        vict = wtl->targ2.ptr.ch;
    } else {
        one_argument(argument, name);
        if (!*name) {
            send_to_char("Yes, but for whom?!?\n\r", ch);
            return;
        }
        if (!(vict = get_char_vis(ch, name))) {
            send_to_char("There is no such player.\n\r", ch);
            return;
        }
    }
    if (IS_NPC(vict)) {
        send_to_char("You can't do that to a mob!\n\r", ch);
        return;
    }
    if (GET_LEVEL(vict) > GET_LEVEL(ch)) {
        send_to_char("Hmmm...you'd better not.\n\r", ch);
        return;
    }

    switch (subcmd) {
    case SCMD_PARDON:

        return;
        send_to_char("Pardoned.\n\r", ch);
        send_to_char("You have been pardoned by the Gods!\n\r", vict);
        sprintf(buf, "(GC) %s pardoned by %s", GET_NAME(vict), GET_NAME(ch));
        mudlog(buf, BRF, (sh_int)MAX(LEVEL_GOD, GET_INVIS_LEV(ch)), TRUE);
        break;
    case SCMD_NOTITLE:
        result = PLR_TOG_CHK(vict, PLR_NOTITLE);
        sprintf(buf, "(GC) Notitle %s for %s by %s.", ONOFF(result), GET_NAME(vict), GET_NAME(ch));
        mudlog(buf, NRM, (sh_int)MAX(LEVEL_GOD, GET_INVIS_LEV(ch)), TRUE);
        strcat(buf, "\n\r");
        send_to_char(buf, ch);
        break;
    case SCMD_SQUELCH:
        result = PLR_TOG_CHK(vict, PLR_NOSHOUT);
        sprintf(buf, "(GC) Squelch %s for %s by %s.", ONOFF(result), GET_NAME(vict), GET_NAME(ch));
        mudlog(buf, BRF, (sh_int)MAX(LEVEL_GOD, GET_INVIS_LEV(ch)), TRUE);
        strcat(buf, "\n\r");
        send_to_char(buf, ch);
        break;
    case SCMD_FREEZE:
        if (ch == vict) {
            send_to_char("Oh, yeah, THAT'S real smart...\n\r", ch);
            return;
        }
        if (PLR_FLAGGED(vict, PLR_FROZEN)) {
            send_to_char("Your victim is already pretty cold.\n\r", ch);
            return;
        }
        SET_BIT(PLR_FLAGS(vict), PLR_FROZEN);
        vict->specials2.freeze_level = GET_LEVEL(ch);
        send_to_char("A bitter wind suddenly rises and drains every erg of heat from your body!\n\rYou feel frozen!\n\r", vict);
        send_to_char("Frozen.\n\r", ch);
        act("A sudden cold wind conjured from nowhere freezes $n!", FALSE, vict, 0, 0, TO_ROOM);
        sprintf(buf, "(GC) %s frozen by %s.", GET_NAME(vict), GET_NAME(ch));
        mudlog(buf, BRF, (sh_int)MAX(LEVEL_GOD, GET_INVIS_LEV(ch)), TRUE);
        break;
    case SCMD_THAW:
        if (!PLR_FLAGGED(vict, PLR_FROZEN)) {
            send_to_char("Sorry, your victim is not morbidly encased in ice at the moment.\n\r", ch);
            return;
        }
        if (vict->specials2.freeze_level > GET_LEVEL(ch)) {
            sprintf(buf, "Sorry, a level %d God froze %s... you can't unfreeze %s.\n\r",
                vict->specials2.freeze_level, GET_NAME(vict), HMHR(vict));
            send_to_char(buf, ch);
            return;
        }
        sprintf(buf, "(GC) %s un-frozen by %s.", GET_NAME(vict), GET_NAME(ch));
        mudlog(buf, BRF, (sh_int)MAX(LEVEL_GOD, GET_INVIS_LEV(ch)), TRUE);
        REMOVE_BIT(PLR_FLAGS(vict), PLR_FROZEN);
        send_to_char("A fireball suddenly explodes in front of you, melting the ice!\n\rYou feel thawed.\n\r", vict);
        send_to_char("Thawed.\n\r", ch);
        act("A sudden fireball conjured from nowhere thaws $n!", FALSE, vict, 0, 0, TO_ROOM);
        break;
    case SCMD_UNAFFECT:
        if (vict->affected) {
            while (vict->affected)
                affect_remove(vict, vict->affected);
            send_to_char("All spells removed.\n\r", ch);
            send_to_char("There is a brief flash of light!\n\r"
                         "You feel slightly different.\n\r",
                vict);
        } else {
            send_to_char("Your victim does not have any affections!\n\r", ch);
            return;
        }
        break;
    case SCMD_REROLL:
        send_to_char("Rerolled...\n\r", ch);
        roll_abilities(vict, 80, 93);
        if (vict->desc)
            act("$n just rerolled you.", FALSE, ch, 0, vict, TO_VICT);
        sprintf(buf, "(GC) %s has rerolled %s.", GET_NAME(ch), GET_NAME(vict));
        log(buf);
        vict->update_available_practice_sessions();
        break;
    case SCMD_RETIRE:
        if (PLR_FLAGGED(vict, PLR_RETIRED)) {
            act("$N has retired already.", FALSE, ch, 0, vict, TO_CHAR);
            return;
        }
        retire(vict);
        act("You are retired by $n.", FALSE, ch, 0, vict, TO_VICT);
        act("You retire $N.", FALSE, ch, 0, vict, TO_CHAR);
        sprintf(buf, "(GC) %s has retired %s.", GET_NAME(ch), GET_NAME(vict));
        log(buf);
        break;
    case SCMD_REACTV:
        if (!PLR_FLAGGED(vict, PLR_RETIRED)) {
            act("$N is not retired.", FALSE, ch, 0, vict, TO_CHAR);
            return;
        }
        unretire(vict);
        act("You are reactivated by $n.", FALSE, ch, 0, vict, TO_VICT);
        act("You reactivate $N.", FALSE, ch, 0, vict, TO_CHAR);
        sprintf(buf, "(GC) %s has reactivated %s.", GET_NAME(ch), GET_NAME(vict));
        log(buf);
        break;
    case SCMD_REHASH:
        do_rehash(ch, "", 0, 0, 0);
        send_to_char("Ok, rehashed.\n\r", ch);
        break;
    case SCMD_NOTE:
        if (vict == ch) {
            send_to_char("Writing memoirs already?\r\n", ch);
            return;
        }
        // skip the first two arguments ('note' an player's name)
        c = argument;
        while (*c == ' ')
            c++; // skip leading spaces
        while (*c != ' ')
            c++; // skip 'note'
        while (*c == ' ')
            c++; // skip intermediate spaces
        while (*c != ' ')
            c++; // skip player name
        while (*c == ' ')
            c++; // skip trailing spaces
        if ((!*c) || (!isalpha(*c))) {
            send_to_char("usage: wizutil note <player> <message>\r\n", ch);
            return;
        }
        add_exploit_record(EXPLOIT_NOTE, vict, 0, c);
        sprintf(buf, "(GC) %s wrote a note about %s.", GET_NAME(ch), GET_NAME(vict));
        log(buf);
        send_to_char("OK.", ch);
        break;
    }
    save_char(vict, NOWHERE, 0);
}

/* single zone printing fn used by "show zone" so it's not repeated in the
   code 3 times ... -je, 4/6/93 */

void print_zone_to_buf(char* bufptr, int zone)
{
    sprintf(bufptr, "%s%3d %-30.30s Age: %3d; Reset: %3d (%d); Top: %5d\n\r",
        bufptr, zone_table[zone].number, zone_table[zone].name,
        zone_table[zone].age, zone_table[zone].lifespan,
        zone_table[zone].reset_mode, zone_table[zone].top);
}

ACMD(do_show)
{
    struct char_file_u vbuf;
    int i, j, k, l, con, count;
    ;
    char self = 0;
    struct char_data* vict;
    struct obj_data* obj;
    struct alias_list* list;
    char field[40], value[40], birth[80];
    universal_list* tmplist;

    extern char* prof_abbrevs[];
    extern char* genders[];
    extern int buf_switches, buf_largecount, buf_overflows;
    extern int memory_rec_counter;
    extern universal_list* affected_list;

    struct show_struct {
        char* cmd;
        char level;
    } fields[] = {
        { "nothing", 0 },
        { "zones", LEVEL_IMMORT },
        { "player", LEVEL_AREAGOD },
        { "rent", LEVEL_AREAGOD },
        { "stats", LEVEL_IMMORT },
        { "unused", LEVEL_IMPL },
        { "death", LEVEL_GOD },
        { "godrooms", LEVEL_GOD },
        { "affected", LEVEL_GOD },
        { "aliases", LEVEL_AREAGOD },
        { "exploits", LEVEL_AREAGOD },
        { "\n", 0 }
    };

    if IS_NPC (ch) {
        send_to_char("Oh no!  None of that stuff!\n\r", ch);
        return;
    }
    if (!*argument) {
        strcpy(buf, "Show options:\n\r");
        for (j = 0, i = 1; fields[i].level; i++)
            if (fields[i].level <= GET_LEVEL(ch))
                sprintf(buf, "%s%-15s%s", buf, fields[i].cmd, (!(++j % 5) ? "\n\r" : ""));
        strcat(buf, "\n\r");
        send_to_char(buf, ch);
        return;
    }
    half_chop(argument, field, arg);
    half_chop(arg, value, arg);
    for (l = 0; *(fields[l].cmd) != '\n'; l++)
        if (!strncmp(field, fields[l].cmd, strlen(field)))
            break;

    if (GET_LEVEL(ch) < fields[l].level) {
        send_to_char("You are not godly enough for that!\n\r", ch);
        return;
    }
    if (!strcmp(value, "."))
        self = 1;
    buf[0] = '\0';
    switch (l) {
    case 1: /* zone */
        /* tightened up by JE 4/6/93 */
        if (self)
            print_zone_to_buf(buf, world[ch->in_room].zone);
        else if (is_number(value)) {
            for (j = atoi(value), i = 0; zone_table[i].number != j && i <= top_of_zone_table; i++)
                ;
            if (i <= top_of_zone_table)
                print_zone_to_buf(buf, i);
            else {
                send_to_char("That is not a valid zone.\n\r", ch);
                return;
            }
        } else
            // 6-19-01 Errent: trying to write all the zones to the buffer and then displaying the
            // whole buffer was giving unwanted results. The ability to display all the zones at
            // the same time seems inefficient and unused, so I changed this to require a zone #.
            // for (i = 0; i <= top_of_zone_table; i++)
            // print_zone_to_buf(buf, i);

            sprintf(buf, "Show which zone?\n\r");

        send_to_char(buf, ch);
        break;
    case 2: /* player */
        if (load_char(value, &vbuf) < 0) {
            send_to_char("There is no such player.\n\r", ch);
            return;
        }
        sprintf(buf, "Player: %-12s (%s) [%2d %s]\n\r", vbuf.name,
            genders[(int)vbuf.sex], vbuf.level, prof_abbrevs[(int)vbuf.prof]);
        sprintf(buf,
            "%sAu: %-8d  Exp: %-8d  Align: %-5d  Lessons: %-3d\n\r",
            buf, vbuf.points.gold, vbuf.points.exp,
            vbuf.specials2.alignment, vbuf.specials2.spells_to_learn);
        strcpy(birth, ctime(&vbuf.birth));
        sprintf(buf,
            "%sStarted: %-20.16s  Last: %-20.16s  Played: %3dh %2dm\n\r",
            buf, birth, ctime(&vbuf.last_logon), (int)(vbuf.played / 3600),
            (int)(vbuf.played / 60 % 60));
        send_to_char(buf, ch);
        break;
    case 3:
        Crash_listrent(ch, value);
        break;
    case 4:
        i = 0;
        j = 0;
        k = 0;
        con = 0;
        for (vict = character_list; vict; vict = vict->next) {
            if (IS_NPC(vict))
                j++;
            else if (CAN_SEE(ch, vict)) {
                i++;
                if (vict->desc)
                    con++;
            }
        }
        for (obj = object_list; obj; obj = obj->next)
            k++;
        sprintf(buf, "Current stats:\n\r");
        sprintf(buf, "%s  %5d players in game  %5d connected\n\r", buf, i, con);
        sprintf(buf, "%s  %5d registered\n\r", buf, top_of_p_table + 1);
        sprintf(buf, "%s  %5d mobiles          %5d prototypes\n\r",
            buf, j, top_of_mobt + 1);
        sprintf(buf, "%s  %5d objects          %5d prototypes\n\r",
            buf, k, top_of_objt + 1);
        sprintf(buf, "%s  %5d rooms            %5d zones\n\r",
            buf, top_of_world + 1, top_of_zone_table + 1);
        sprintf(buf, "%s  %5d large bufs\n\r", buf, buf_largecount);
        sprintf(buf, "%s  %5d buf switches     %5d overflows\n\r", buf,
            buf_switches, buf_overflows);
        sprintf(buf, "%s  %5d txt_blocks       %5d affect_blocks\n\r", buf,
            txt_block_counter, affected_type_counter);
        sprintf(buf, "%s  %5d pkill records    %5d mobile memories \n\r", buf,
            pkill_get_total(), memory_rec_counter);

        if (!stat_ticks_passed)
            sprintf(buf, "%s  No player statistics yet\n\r", buf);
        else {
            sprintf(buf, "%s  %5.2f average players present\n\r  %5.2f mortals, %5.2f immortals\n\r", buf,
                float(stat_mortals_counter + stat_immortals_counter) / stat_ticks_passed,
                float(stat_mortals_counter) / stat_ticks_passed,
                float(stat_immortals_counter) / stat_ticks_passed);
            sprintf(buf, "%s  %5.2f good races,%5.2f legends \n\r", buf,
                float(stat_whitie_counter) / stat_ticks_passed,
                float(stat_whitie_legend_counter) / stat_ticks_passed);
            sprintf(buf, "%s  %5.2f evil races,%5.2f legends \n\r", buf,
                float(stat_darkie_counter) / stat_ticks_passed,
                float(stat_darkie_legend_counter) / stat_ticks_passed);
        }
        send_to_char(buf, ch);

        break;
    case 5:
        break;
    case 6:
        strcpy(buf, "Death Traps\n\r-----------\n\r");
        for (i = 0, j = 0; i < top_of_world; i++)
            if (IS_SET(world[i].room_flags, DEATH))
                sprintf(buf, "%s%2d: [%5d] %s\n\r", buf, ++j,
                    world[i].number, world[i].name);
        send_to_char(buf, ch);
        break;
    case 7:
#define GOD_ROOMS_ZONE 0
        strcpy(buf, "Godrooms\n\r--------------------------\n\r");
        for (i = 0, j = 0; i < top_of_world; i++)
            if (world[i].zone == GOD_ROOMS_ZONE)
                sprintf(buf, "%s%2d: [%5d] %s\n\r", buf, j++, world[i].number,
                    world[i].name);
        send_to_char(buf, ch);
        break;

    case 8:
        count = 0;
        for (tmplist = affected_list; tmplist; tmplist = tmplist->next) {
            if (count > 190) {
                strcat(buf, "*** More... ***");
                break;
            }
            if (tmplist->type == TARGET_CHAR) {
                if (char_exists(tmplist->number))
                    sprintf(buf, "%s%-38s| ", buf, GET_NAME(tmplist->ptr.ch));
                else
                    sprintf(buf, "%s%-38s| ", buf, "*Unknown char*");
            }
            if (tmplist->type == TARGET_ROOM) {
                sprintf(buf, "%sRoom affect(%d)                     | ",
                    buf, tmplist->ptr.room->number);
            }
            count++;
        }
        strcat(buf, "\n\r");
        send_to_char(buf, ch);

        break;

    case 9: // Aliases
        if (!(vict = get_char_vis(ch, value))) {
            send_to_char("There is no such player in the game.\n\r", ch);
            return;
        }
        /* alias list */
        list = vict->specials.alias;
        if (!list) {
            sprintf(buf, "%s has no aliases defined.\n\r", GET_NAME(vict));
            send_to_char(buf, ch);
            return;
        }
        sprintf(buf, "%s has the following aliases defined:\n\r", GET_NAME(vict));
        send_to_char(buf, ch);
        for (count = 0; list; list = list->next, count++) {
            sprintf(buf, "%-20s: %s\n\r", list->keyword, list->command);
            send_to_char(buf, ch);
        }
        break;
    case 10:
        send_to_char("Spying on exploits..", ch);
        print_exploits(ch, value);
        break;

    default:
        send_to_char("Sorry, I don't understand that.\n\r", ch);
        break;
    }
}

#define PC 1
#define NPC 2
#define BOTH 3

#define MISC 0
#define BINARY 1
#define NUMBER 2

#define SET_OR_REMOVE(flagset, flags)   \
    {                                   \
        if (on)                         \
            SET_BIT(flagset, flags);    \
        else if (off)                   \
            REMOVE_BIT(flagset, flags); \
    }

#define RANGE(low, high) (value = MAX((low), MIN((high), (value))))
/*
  ...Moved a bit lower...
  ACMD(do_wizset)
  {
  int	i, l;
  struct char_data *vict;
  struct char_data *cbuf;
  struct char_file_u tmp_store;
  char	field[MAX_INPUT_LENGTH], name[MAX_INPUT_LENGTH],
  val_arg[MAX_INPUT_LENGTH];
  int	on = 0, off = 0, value = 0;
  char	is_file = 0, is_mob = 0, is_player = 0;
  int	player_i;
*/
struct set_struct {
    char* cmd;
    char level;
    char pcnpc;
    char type;
} fields[] = {
    { "brief", LEVEL_GOD, PC, BINARY }, /* 0 */
    { "invstart", LEVEL_GOD, PC, BINARY },
    { "title", LEVEL_GOD, PC, MISC },
    { "nosummon", LEVEL_GRGOD, PC, BINARY },
    { "maxhit", LEVEL_GRGOD, BOTH, NUMBER },
    { "maxstamina", LEVEL_GRGOD, BOTH, NUMBER }, /* 5 */
    { "maxmove", LEVEL_GRGOD, BOTH, NUMBER },
    { "hit", LEVEL_GRGOD, BOTH, NUMBER },
    { "stamina", LEVEL_GRGOD, BOTH, NUMBER },
    { "move", LEVEL_GRGOD, BOTH, NUMBER },
    { "alignment", LEVEL_GOD, BOTH, NUMBER }, /* 10 */
    { "str", LEVEL_GRGOD, BOTH, NUMBER },
    { "lea", LEVEL_GRGOD, BOTH, NUMBER },
    { "int", LEVEL_GRGOD, BOTH, NUMBER },
    { "will", LEVEL_GRGOD, BOTH, NUMBER },
    { "dex", LEVEL_GRGOD, BOTH, NUMBER }, /* 15 */
    { "con", LEVEL_GRGOD, BOTH, NUMBER },
    { "sex", LEVEL_GRGOD, BOTH, MISC },
    { "dodge", LEVEL_GRGOD, BOTH, NUMBER },
    { "gold", LEVEL_GOD, BOTH, NUMBER },
    { "", LEVEL_GOD, PC, NUMBER }, /* 20 */
    { "exp", LEVEL_GRGOD, BOTH, NUMBER },
    { "OB", LEVEL_GRGOD, BOTH, NUMBER },
    { "damage", LEVEL_GRGOD, BOTH, NUMBER },
    { "invis", LEVEL_IMPL, PC, NUMBER },
    { "nohassle", LEVEL_GRGOD, PC, BINARY }, /* 25 */
    { "frozen", LEVEL_FREEZE, PC, BINARY },
    { "practices", LEVEL_GRGOD, PC, NUMBER },
    { "lessons", LEVEL_GRGOD, PC, NUMBER },
    { "drunk", LEVEL_GRGOD, BOTH, MISC },
    { "hunger", LEVEL_GRGOD, BOTH, MISC }, /* 30 */
    { "thirst", LEVEL_GRGOD, BOTH, MISC },
    { "", LEVEL_GOD, PC, BINARY },
    { "", LEVEL_GOD, PC, BINARY },
    { "level", LEVEL_GOD, BOTH, NUMBER },
    { "roomflag", LEVEL_GRGOD, PC, BINARY }, /* 35 */
    { "room", LEVEL_IMPL, BOTH, NUMBER },
    { "siteok", LEVEL_GRGOD, PC, BINARY },
    { "xxdeleted", LEVEL_IMPL, PC, BINARY }, // use delete command
    { "prof", LEVEL_GRGOD, BOTH, MISC },
    { "nowizlist", LEVEL_GOD, PC, BINARY }, /* 40 */
    { "trash", LEVEL_GOD, PC, BINARY },
    { "loadroom", LEVEL_GRGOD, PC, MISC },
    { "color", LEVEL_GOD, PC, BINARY },
    { "idnum", LEVEL_IMPL, PC, NUMBER },
    { "password", LEVEL_IMPL, PC, MISC }, /* 45 */
    { "nodelete", LEVEL_GOD, PC, BINARY },
    { "race", LEVEL_GRGOD, PC, NUMBER },
    { "parry", LEVEL_GRGOD, BOTH, NUMBER },
    { "ENE_regen", LEVEL_GRGOD, BOTH, NUMBER },
    { "bodytype", LEVEL_GRGOD, BOTH, NUMBER }, /* 50 */
    { "affected", LEVEL_GRGOD, BOTH, NUMBER },
    { "coof_mage", LEVEL_IMPL, PC, NUMBER },
    { "coof_cle", LEVEL_IMPL, PC, NUMBER },
    { "coof_ran", LEVEL_IMPL, PC, NUMBER },
    { "coof_war", LEVEL_IMPL, PC, NUMBER }, /* 55 */
    { "spirit", LEVEL_GRGOD, BOTH, NUMBER },
    { "height", LEVEL_GRGOD, BOTH, NUMBER },
    { "name", LEVEL_GRGOD, PC, MISC },
    { "language", LEVEL_GOD, BOTH, MISC },
    { "weight", LEVEL_GRGOD, BOTH, NUMBER }, /* 60 */
    { "saving_throw", LEVEL_GRGOD, BOTH, NUMBER },
    { "specialization", LEVEL_GRGOD, BOTH, NUMBER },
    { "rp_flag", LEVEL_AREAGOD, PC, NUMBER },
    { "\n", 0, BOTH, MISC }
};

ACMD(do_wizset)
{
    int i, l, tmp;
    struct char_data* vict;
    struct char_data* cbuf = 0;
    struct char_file_u tmp_store;
    struct descriptor_data descr;
    char field[MAX_INPUT_LENGTH], name[MAX_INPUT_LENGTH];
    char val_arg[MAX_INPUT_LENGTH];
    int on = 0, off = 0, value = 0;
    char is_file = 0, is_mob = 0, is_player = 0;
    int player_i;

    int advance_perm(struct char_data*, struct char_data*, int);

    half_chop(argument, name, buf);
    if (!strcmp(name, "file")) {
        is_file = 1;
        half_chop(buf, name, buf);
    } else if (!str_cmp(name, "player")) {
        is_player = 1;
        half_chop(buf, name, buf);
    } else if (!str_cmp(name, "mob")) {
        is_mob = 1;
        half_chop(buf, name, buf);
    }

    half_chop(buf, field, buf);
    strcpy(val_arg, buf);
    if (!*name || !*field) {
        send_to_char("Usage: wizset <victim> <field> <value>\n\r", ch);
        return;
    }
    if (IS_NPC(ch)) {
        send_to_char("None of that!\n\r", ch);
        return;
    }
    if (!is_file) {
        if (is_player) {
            if (!(vict = get_player_vis(ch, name))) {
                send_to_char("There is no such player.\n\r", ch);
                return;
            }
        } else {
            if (!(vict = get_char_vis(ch, name))) {
                send_to_char("There is no such creature.\n\r", ch);
                return;
            }
        }
        if (!IS_NPC(vict)) {
            player_i = find_name(name);
            if (player_i < 0) {
                send_to_char("No such player.\n\r", ch);
                return;
            }
        } else
            player_i = -1;

    } else if (is_file) {
        CREATE(cbuf, struct char_data, 1);
        clear_char(cbuf, MOB_VOID);
        if ((player_i = load_char(name, &tmp_store)) > -1) {
            store_to_char(&tmp_store, cbuf);
            if (GET_LEVEL(cbuf) >= GET_LEVEL(ch)) {
                free_char(cbuf);
                send_to_char("Sorry, you can't do that.\n\r", ch);
                return;
            }
            bzero(descr.pwd, MAX_PWD_LENGTH);
            strcpy(descr.pwd, tmp_store.pwd);
            bzero(descr.host, 50);
            strcpy(descr.host, tmp_store.host);
            cbuf->desc = &descr;
            vict = cbuf;
        } else {
            RELEASE(cbuf);
            send_to_char("There is no such player.\n\r", ch);
            return;
        }
    }
    if ((GET_LEVEL(ch) < LEVEL_GOD + 1) && (vict != ch)) {
        send_to_char("You may not do that.\n\r", ch);
        return;
    }

    if (GET_LEVEL(ch) != LEVEL_IMPL) {
        if (!IS_NPC(vict) && GET_LEVEL(ch) <= GET_LEVEL(vict) && vict != ch) {
            send_to_char("Maybe that's not such a great idea...\n\r", ch);
            return;
        }
    }

    for (l = 0; *(fields[l].cmd) != '\n'; l++)
        if (!strncmp(field, fields[l].cmd, strlen(field)))
            break;

    if ((GET_LEVEL(ch) < fields[l].level) && ((ch != vict) || fields[l].level > LEVEL_GRGOD)) {
        send_to_char("You are not godly enough for that!\n\r", ch);
        return;
    }
    if (IS_NPC(vict) && (!fields[l].pcnpc && NPC)) {
        send_to_char("You can't do that to a beast!\n\r", ch);
        return;
    } else if (!IS_NPC(vict) && (!fields[l].pcnpc && PC)) {
        send_to_char("That can only be done to a beast!\n\r", ch);
        return;
    }

    if (fields[l].type == BINARY) {
        if (!strcmp(val_arg, "on") || !strcmp(val_arg, "yes"))
            on = 1;
        else if (!strcmp(val_arg, "off") || !strcmp(val_arg, "no"))
            off = 1;
        if (!(on || off)) {
            send_to_char("Value must be on or off.\n\r", ch);
            return;
        }
    } else if (fields[l].type == NUMBER) {
        value = atoi(val_arg);
    }

    strcpy(buf, "Okay.");
    switch (l) {
    case 0:
        SET_OR_REMOVE(PRF_FLAGS(vict), PRF_BRIEF);
        break;
    case 1:
        SET_OR_REMOVE(PLR_FLAGS(vict), PLR_INVSTART);
        break;
    case 2:
        if (GET_TITLE(vict))
            RELEASE(GET_TITLE(vict));
        CREATE(GET_TITLE(vict), char, strlen(val_arg) + 1);
        strcpy(GET_TITLE(vict), val_arg);
        sprintf(buf, "%s's title is now: %s", GET_NAME(vict), GET_TITLE(vict));
        break;
    case 3:
        SET_OR_REMOVE(PRF_FLAGS(vict), PRF_SUMMONABLE);
        on = !on; /* so output will be correct */
        break;
    case 4:
        vict->constabilities.hit = RANGE(1, 5000);
        affect_total(vict);
        break;
    case 5:
        vict->constabilities.mana = RANGE(1, 5000);
        affect_total(vict);
        break;
    case 6:
        vict->constabilities.move = RANGE(1, 5000);
        affect_total(vict);
        break;
    case 7:
        vict->tmpabilities.hit = RANGE(-9, vict->abilities.hit);
        affect_total(vict);
        break;
    case 8:
        vict->tmpabilities.mana = RANGE(0, vict->abilities.mana);
        affect_total(vict);
        break;
    case 9:
        vict->tmpabilities.move = RANGE(0, vict->abilities.move);
        affect_total(vict);
        break;
    case 10:
        GET_ALIGNMENT(vict) = RANGE(-1000, 1000);
        affect_total(vict);
        break;
    case 11:
        vict->constabilities.str = RANGE(0, 40);
        affect_total(vict);
        break;

    case 12:
        vict->constabilities.lea = RANGE(0, 40);
        affect_total(vict);
        break;
    case 13:
        vict->constabilities.intel = RANGE(0, 40);
        affect_total(vict);
        break;
    case 14:
        vict->constabilities.wil = RANGE(0, 40);
        affect_total(vict);
        break;
    case 15:
        vict->constabilities.dex = RANGE(0, 40);
        affect_total(vict);
        break;
    case 16:
        vict->constabilities.con = RANGE(0, 40);
        affect_total(vict);
        break;
    case 17:
        if (!str_cmp(val_arg, "male"))
            vict->player.sex = SEX_MALE;
        else if (!str_cmp(val_arg, "female"))
            vict->player.sex = SEX_FEMALE;
        else if (!str_cmp(val_arg, "neutral"))
            vict->player.sex = SEX_NEUTRAL;
        else {
            send_to_char("Must be 'male', 'female', or 'neutral'.\n\r", ch);
            return;
        }
        break;
    case 18:
        SET_DODGE(vict) = RANGE(-100, 200);
        affect_total(vict);
        break;
    case 19:
        GET_GOLD(vict) = RANGE(0, 100000000);
        break;
    /* 20 was BANK */
    case 21:
        vict->points.exp = RANGE(0, 50000000);
        break;
    case 22:
        SET_OB(vict) = RANGE(-20, 200);
        affect_total(vict);
        break;
    case 23:
        vict->points.damage = RANGE(-20, 30);
        affect_total(vict);
        break;
    case 24:
        if (GET_LEVEL(ch) < LEVEL_IMPL && ch != vict) {
            send_to_char("You aren't godly enough for that!\n\r", ch);
            return;
        }
        GET_INVIS_LEV(vict) = RANGE(0, GET_LEVEL(vict));
        break;
    case 25:
        if (GET_LEVEL(ch) < LEVEL_IMPL && ch != vict) {
            send_to_char("You aren't godly enough for that!\n\r", ch);
            return;
        }
        SET_OR_REMOVE(PRF_FLAGS(vict), PRF_NOHASSLE);
        break;
    case 26:
        if (ch == vict) {
            send_to_char("Better not -- could be a long winter!\n\r", ch);
            return;
        }
        SET_OR_REMOVE(PLR_FLAGS(vict), PLR_FROZEN);
        break;
    case 27:
    case 28:
        SPELLS_TO_LEARN(vict) = RANGE(0, 1000);
        break;
    case 29:
    case 30:
    case 31:
        if (!str_cmp(val_arg, "off")) {
            GET_COND(vict, (l - 29)) = (signed char)-1;
            sprintf(buf, "%s's %s now off.", GET_NAME(vict),
                fields[l].cmd);
        } else if (is_number(val_arg)) {
            value = atoi(val_arg);
            RANGE(0, 24);
            GET_COND(vict, (l - 29)) = (signed char)value;
            sprintf(buf, "%s's %s set to %d.", GET_NAME(vict),
                fields[l].cmd, value);
        } else {
            send_to_char("Must be 'off' or a value from 0 to 24.\n\r", ch);
            return;
        }
        break;
    case 32:
        break;
    case 33:
        break;
    case 34:
        if (!advance_perm(ch, vict, value))
            return;

        vict->player.level = (byte)value;
        break;
    case 35:
        SET_OR_REMOVE(PRF_FLAGS(vict), PRF_ROOMFLAGS);
        break;
    case 36:
        if ((i = real_room(value)) < 0) {
            send_to_char("No room exists with that number.\n\r", ch);
            return;
        }
        if (IS_RIDING(vict))
            stop_riding(vict);
        char_from_room(vict);
        char_to_room(vict, i);
        break;
    case 37:
        SET_OR_REMOVE(PLR_FLAGS(vict), PLR_SITEOK);
        break;
    case 38:
        SET_OR_REMOVE(PLR_FLAGS(vict), PLR_DELETED);
        break;
    case 39:
        /* prof */
        break;
    case 40:
        SET_OR_REMOVE(PLR_FLAGS(vict), PLR_NOWIZLIST);
        break;
    case 42:
        if (!str_cmp(val_arg, "on"))
            SET_BIT(PLR_FLAGS(vict), PLR_LOADROOM);
        else if (!str_cmp(val_arg, "off"))
            REMOVE_BIT(PLR_FLAGS(vict), PLR_LOADROOM);
        else {
            if (real_room(i = atoi(val_arg)) > -1) {
                GET_LOADROOM(vict) = i;
                sprintf(buf, "%s will enter at %d.", GET_NAME(vict),
                    GET_LOADROOM(vict));
            } else
                sprintf(buf, "That room does not exist!");
        }
        break;
    case 43:
        SET_OR_REMOVE(PRF_FLAGS(vict), PRF_COLOR);
        break;
    case 44:
        if (GET_IDNUM(ch) != 1 || !IS_NPC(vict))
            return;
        vict->specials2.idnum = value;
        break;
    case 45:
        if (!is_file)
            return;
        if (GET_LEVEL(vict) >= LEVEL_GRGOD) {
            send_to_char("You cannot change that.\n\r", ch);
            return;
        }
        strncpy(descr.pwd, val_arg, MAX_PWD_LENGTH);
        //      encrypt_line((unsigned char *)(tmp_store.pwd), MAX_PWD_LENGTH);
        descr.pwd[MAX_PWD_LENGTH] = '\0';
        if (strlen(val_arg) < MAX_PWD_LENGTH)
            descr.pwd[strlen(val_arg)] = 0;
        sprintf(buf, "Password changed to '%s'.", val_arg);
        break;
    case 46:
        SET_OR_REMOVE(PLR_FLAGS(vict), PLR_NODELETE);
        break;
    case 47:
        vict->player.race = value;
        break;
    case 48:
        SET_PARRY(vict) = RANGE(-20, 200);
        affect_total(vict);
        break;
    case 49:
        vict->points.ENE_regen = RANGE(-20, 600);
        affect_total(vict);
        break;
    case 50:
        vict->player.bodytype = RANGE(0, MAX_BODYTYPES - 1);
        break;
    case 51:
        vict->specials.affected_by = value;
        break;
    case 52:
        GET_PROF_POINTS(PROF_MAGIC_USER, vict) = value;
        break;
    case 53:
        GET_PROF_POINTS(PROF_CLERIC, vict) = value;
        break;
    case 54:
        GET_PROF_POINTS(PROF_RANGER, vict) = value;
        break;
    case 55:
        GET_PROF_POINTS(PROF_WARRIOR, vict) = value;
        break;
    case 56:
        vict->points.spirit = value;
        affect_total(vict);
        break;
    case 57:
        vict->player.height = value;
        break;
    case 58:
        if (!*val_arg) {
            act("What new name would you like to give to $N?",
                FALSE, ch, 0, vict, TO_CHAR);
            return;
        }
        if (is_file) {
            send_to_char("You can only rename online characters.", ch);
            return;
        }
        extern int valid_name(char*);
        if (!valid_name(val_arg)) {
            send_to_char("Invalid name, please try another.", ch);
            return;
        }
        extern int rename_char(struct char_data*, char*);
        rename_char(vict, val_arg);
        send_to_char("You changed their name successfully.\n\r", ch);
        break;

    case 59:
        if (!*val_arg) {
            send_to_char("Which language?\n\r", ch);
            break;
        }
        if (!strncmp("common language", val_arg, strlen(val_arg))) {
            vict->player.language = LANG_BASIC;
            break;
        }
        for (tmp = 0; tmp < language_number; tmp++)
            if (!strncmp(skills[language_skills[tmp]].name, val_arg, strlen(val_arg)))
                break;
        if (tmp == language_number) {
            send_to_char("Unknown language.\n\r", ch);
            break;
        }
        vict->player.language = language_skills[tmp];
        break;

    case 60:
        vict->player.weight = value;
        break;
    case 61:
        GET_SAVE(vict) = RANGE(-100, 100);
        break;
    case 62:
        SET_SPEC(vict, RANGE(-100, 100));
        break;
        break;
    case 63:
        vict->specials2.rp_flag = value;
        break;

    default:
        sprintf(buf, "Can't set that!");
        break;
    }

    if (fields[l].type == BINARY) {
        sprintf(buf, "%s %s for %s.\n\r", fields[l].cmd, ONOFF(on),
            GET_NAME(vict));
        CAP(buf);
    } else if (fields[l].type == NUMBER) {
        sprintf(buf, "%s's %s set to %d.\n\r", GET_NAME(vict),
            fields[l].cmd, value);
    } else
        strcat(buf, "\n\r");
    send_to_char(buf, ch);

    if (!is_file && !IS_NPC(vict))
        save_char(vict, NOWHERE, 0);

    if (is_file) {
        char_to_store(vict, &tmp_store);
        save_char(vict, vict->in_room, 0);
        free_char(cbuf);
        send_to_char("Saved in file.\n\r", ch);
    }
}

ACMD(do_delete)
{
    struct char_data* vict;
    int char_index;

    half_chop(argument, arg, buf);
    if (strcmp(buf, "rots")) {
        send_to_char("Incorrect or missing password", ch);
        return;
    }

    char_index = find_player_in_table(arg, -1);
    if (char_index == -1) {
        send_to_char("No such player", ch);
        return;
    }

    pkill_unref_character_by_index(char_index);
    vict = get_char_vis(ch, arg);
    if (vict) {
        if (vict->desc && vict->desc->descriptor) {
            close_socket(vict->desc);
            vict->desc = 0;
        }
        extract_char(vict);
    }
    sprintf(buf, "(GC) %s has deleted %s.", GET_NAME(ch), arg);
    mudlog(buf, BRF, LEVEL_GOD, TRUE);
    Crash_delete_file(player_table[char_index].name);
    delete_exploits_file(player_table[char_index].name);
    move_char_deleted(char_index);
}

extern int top_of_world;
extern int top_of_mobt;
extern int top_of_objt;

extern struct char_data* mob_proto;
extern struct obj_data* obj_proto;
// extern room_data world;
ACMD(do_register)
{

    int zonnum, tmp, sw, vn, count, count1, race, i, oldest;
    char* tmpptr;

    char buf[255];
    char buf2[255];
    /*static*/ char arg1[MAX_INPUT_LENGTH];
    /*static*/ char arg2[MAX_INPUT_LENGTH];
    /*static*/ char arg3[MAX_INPUT_LENGTH];
    /*static*/ char arg4[MAX_INPUT_LENGTH];
    int lowest[25][2];

    for (tmpptr = argument; *tmpptr && isspace(*tmpptr); tmpptr++)
        ;
    for (tmp = 0; *tmpptr && *tmpptr > ' '; tmpptr++, tmp++)
        arg1[tmp] = *tmpptr;
    arg1[tmp] = 0;
    for (; *tmpptr && isspace(*tmpptr); tmpptr++)
        ;
    for (tmp = 0; *tmpptr && *tmpptr > ' '; tmpptr++, tmp++)
        arg2[tmp] = *tmpptr;
    arg2[tmp] = 0;
    for (; *tmpptr && isspace(*tmpptr); tmpptr++)
        ;
    for (tmp = 0; *tmpptr && *tmpptr > ' '; tmpptr++, tmp++)
        arg3[tmp] = *tmpptr;
    arg3[tmp] = 0;
    for (; *tmpptr && isspace(*tmpptr); tmpptr++)
        ;
    for (tmp = 0; *tmpptr && *tmpptr > ' '; tmpptr++, tmp++)
        arg4[tmp] = *tmpptr;
    arg4[tmp] = 0;

    zonnum = 0;
    count = 0;

    if (isdigit(*arg2))
        zonnum = atoi(arg2);
    if (!*arg2)
        zonnum = world[ch->in_room].number / 100;
    if (!*arg1 || !zonnum) {
        send_to_char("Usage: register <room|mobile|object> [zone number].\n\r", ch);
        return;
    }

    if (!strncmp("mobile", arg1, strlen(arg1))) {
        sw = 1;
        for (tmp = 0; tmp <= top_of_mobt; tmp++) {
            vn = mob_index[tmp].virt;
            //      if(vn/100 > zonnum) return;
            if (vn / 100 == zonnum) {
                sprintf(buf, "%5d: %-30s%s", vn,
                    mob_proto[tmp].player.short_descr,
                    (sw) ? "| " : "\n\r");
                send_to_char(buf, ch);
                sw = 1 - sw;
                count++;
            }
        }
        if (!sw)
            send_to_char("\n\r", ch);
        else if (!count)
            send_to_char("No mobiles there.\n\r", ch);
        return;
    }
    if (!strncmp("object", arg1, strlen(arg1))) {
        sw = 1;
        for (tmp = 0; tmp <= top_of_objt; tmp++) {
            vn = obj_index[tmp].virt;
            //      if(vn/100 > zonnum) return;
            if (vn / 100 == zonnum) {
                sprintf(buf, "%5d: %-30s%s", vn,
                    obj_proto[tmp].short_description,
                    (sw) ? "| " : "\n\r");
                send_to_char(buf, ch);
                sw = 1 - sw;
                count++;
            }
        }
        if (!sw)
            send_to_char("\n\r", ch);
        else if (!count)
            send_to_char("No objects there.\n\r", ch);
        return;
    }
    if (!strncmp("room", arg1, strlen(arg1))) {
        sw = 1;
        for (tmp = 0; tmp <= top_of_world; tmp++) {
            vn = zone_table[world[tmp].zone].number;
            //      if(vn/100 > zonnum) return;
            if (vn == zonnum) {
                sprintf(buf, "%5d: %-30s%s", world[tmp].number,
                    world[tmp].name,
                    (sw) ? "| " : "\n\r");
                send_to_char(buf, ch);
                sw = 1 - sw;
                count++;
            }
        }
        if (!sw)
            send_to_char("\n\r", ch);
        else if (!count)
            send_to_char("No rooms there.\n\r", ch);
        return;
    }
    if (!strncmp("player", arg1, strlen(arg1))) {
        send_to_char("Players of that level:\n\r", ch);
        for (tmp = 0; tmp <= top_of_p_table; tmp++)
            if ((player_table + tmp)->level == zonnum) {
                sprintf(buf, "%s\n\r", (player_table + tmp)->name);
                send_to_char(buf, ch);
            }
        return;
    }
    if (!strncmp("top", arg1, strlen(arg1))) {
        oldest = 0;
        if (zonnum > 25)
            zonnum = 25;
        if (zonnum < 1)
            zonnum = 1;
        race = 0;
        if (!*arg3) {
            sprintf(buf, "Top %2d Characters\n\r", zonnum);
        } else {
            if (!strncmp("dark", arg3, strlen(arg3)) || !strncmp("shadow", arg3, strlen(arg3))) {

            } else if (!strncmp("white", arg3, strlen(arg3)) || !strncmp("light", arg3, strlen(arg3))) {

            } else if (!strncmp("human", arg3, strlen(arg3))) {
                sprintf(buf2, "Human ");
                race = RACE_HUMAN;
            } else if (!strncmp("dwarf", arg3, strlen(arg3))) {
                sprintf(buf2, "Dwarf ");
                race = RACE_DWARF;
            } else if (!strncmp("elf", arg3, strlen(arg3)) || !strncmp("woodelf", arg3, strlen(arg3))) {
                sprintf(buf2, "Wood Elf ");
                race = RACE_WOOD;
            } else if (!strncmp("hobbit", arg3, strlen(arg3))) {
                sprintf(buf2, "Hobbit ");
                race = RACE_HOBBIT;
            } else if (!strncmp("bear", arg3, strlen(arg3)) || !strncmp("beorning", arg3, strlen(arg3))) {
                sprintf(buf2, "Beorning ");
                race = RACE_BEORNING;
            } else if (!strncmp("uruk-hai", arg3, strlen(arg3))) {
                sprintf(buf2, "Uruk-Hai ");
                race = RACE_URUK;
            } else if (!strncmp("orc", arg3, strlen(arg3))) {
                sprintf(buf2, "Common Orc ");
                race = RACE_ORC;
            } else if (!strncmp("uruk-lhuth", arg3, strlen(arg3)) || !strncmp("lhuth", arg3, strlen(arg3))) {
                sprintf(buf2, "Uruk-Lhuth ");
                race = RACE_MAGUS;
            } else if (!strncmp("olog-hai", arg3, strlen(arg3)) || !strncmp("olog", arg3, strlen(arg3))) {
                sprintf(buf2, "Olog-Hai ");
                race = RACE_OLOGHAI;
            } else if (!strncmp("harad", arg3, strlen(arg3)) || !strncmp("haradrim", arg3, strlen(arg3))) {
                sprintf(buf2, "Haradrim ");
                race = RACE_HARADRIM;
            } else if (!strncmp("oldest", arg3, strlen(arg3))) {
                sprintf(buf2, "Oldest Legend ");
                oldest = 1;
            }

            if (oldest == 1) {
                if (!strncmp("dark", arg4, strlen(arg4)) || !strncmp("shadow", arg4, strlen(arg4))) {

                } else if (!strncmp("white", arg4, strlen(arg4)) || !strncmp("light", arg4, strlen(arg4))) {

                } else if (!strncmp("human", arg4, strlen(arg4))) {
                    sprintf(buf2, "%sHuman ", buf2);
                    race = RACE_HUMAN;
                } else if (!strncmp("dwarf", arg4, strlen(arg4))) {
                    sprintf(buf2, "%sDwarf ", buf2);
                    race = RACE_DWARF;
                } else if (!strncmp("elf", arg4, strlen(arg4)) || !strncmp("woodelf", arg4, strlen(arg4))) {
                    sprintf(buf2, "%sWood Elf ", buf2);
                    race = RACE_WOOD;
                } else if (!strncmp("hobbit", arg4, strlen(arg4))) {
                    sprintf(buf2, "%sHobbit ", buf2);
                    race = RACE_HOBBIT;
                } else if (!strncmp("bear", arg4, strlen(arg4)) || !strncmp("beorning", arg4, strlen(arg4))) {
                    sprintf(buf2, "%sBeorning ", buf2);
                    race = RACE_BEORNING;
                } else if (!strncmp("uruk-hai", arg4, strlen(arg4))) {
                    sprintf(buf2, "%sUruk-Hai ", buf2);
                    race = RACE_URUK;
                } else if (!strncmp("orc", arg4, strlen(arg4))) {
                    sprintf(buf2, "%sCommon Orc ", buf2);
                    race = RACE_ORC;
                } else if (!strncmp("uruk-lhuth", arg4, strlen(arg4)) || !strncmp("lhuth", arg4, strlen(arg4))) {
                    sprintf(buf2, "%sUruk-Lhuth ", buf2);
                    race = RACE_MAGUS;
                } else if (!strncmp("olog-hai", arg4, strlen(arg4)) || !strncmp("olog", arg4, strlen(arg4))) {
                    sprintf(buf2, "%sOlog-Hai ", buf2);
                    race = RACE_OLOGHAI;
                } else if (!strncmp("harad", arg4, strlen(arg4)) || !strncmp("haradrim", arg4, strlen(arg4))) {
                    sprintf(buf2, "%sHaradrim ", buf2);
                    race = RACE_HARADRIM;
                }
            }

            sprintf(buf, "Top %2d %sCharacters\n\r", zonnum, buf2);
        }

        send_to_char(buf, ch);
        if (oldest == 1) {
            for (count = 0; count < 25; count++) {
                lowest[count][0] = -1;
                lowest[count][1] = -1;
            }
            for (i = 0; i <= top_of_p_table; i++) {
                if (race > 0) {
                    if (race != (player_table + i)->race) {
                        continue;
                    }
                }
                if ((player_table + i)->level >= 30 && (player_table + i)->level <= 90) {
                    for (count = 0; count < 25; count++) {
                        if (lowest[count][1] == -1 || lowest[count][1] > (player_table + i)->idnum) {
                            for (count1 = 25; count1 > count; count1--) {
                                lowest[count1][0] = lowest[count1 - 1][0];
                                lowest[count1][1] = lowest[count1 - 1][1];
                            }
                            lowest[count][0] = i;
                            lowest[count][1] = (player_table + i)->idnum;
                            break;
                        }
                    }
                }
            }
            for (i = 0; i < zonnum; i++) {
                if ((lowest[i][0] >= 0) && (lowest[i][0] <= top_of_p_table)) {
                    sprintf(buf, "%2d - %s\n\r", (player_table + lowest[i][0])->level, (player_table + lowest[i][0])->name);
                    send_to_char(buf, ch);
                }
            }
        } else {
            count = 1;
            for (vn = 90; vn > 30 && count <= zonnum; vn--) {
                for (tmp = 0; tmp <= top_of_p_table; tmp++) {
                    if ((player_table + tmp)->level == vn) {
                        if (race > 0) {
                            if (race != (player_table + tmp)->race) {
                                continue;
                            }
                        }
                        sprintf(buf, "%2d - %s\n\r", (player_table + tmp)->level, (player_table + tmp)->name);
                        send_to_char(buf, ch);
                        count++;
                    }
                }
            }
        }
        return;
    }
    if (!strncmp("script", arg1, strlen(arg1))) {
        sw = 1;
        for (tmp = 0; tmp <= top_of_script_table; tmp++) {
            vn = script_table[tmp].number;
            if (vn / 100 == zonnum) {
                sprintf(buf, "%5d: %-30s%s", vn, script_table[tmp].name, (sw) ? "| " : "\n\r");
                send_to_char(buf, ch);
                sw = 1 - sw;
                count++;
            }
        }
        if (!sw)
            send_to_char("\n\r", ch);
        else if (!count)
            send_to_char("No scripts there.\n\r", ch);
        return;
    }

    send_to_char("You can see register for mobile, object, player, script or room only.\n\r", ch);
}

ACMD(do_findzone)
{
    int x, y, i;
    char buf[1000];
    char* arg = argument;

    while (*arg && (*arg <= ' '))
        arg++;

    x = y = -1;
    if (isdigit(*arg)) {
        x = atoi(arg);
    }
    while (*arg && (*arg > ' '))
        arg++;
    while (*arg && (*arg <= ' '))
        arg++;

    if (isdigit(*arg)) {
        y = atoi(arg);
    }
    send_to_char("Zones loaded are:", ch);
    for (i = 0; i <= top_of_zone_table; i++)
        if (((x < 0) || (x == zone_table[i].x)) && ((y < 0) || (y == zone_table[i].y))) {
            sprintf(buf, "\n\rZone %3d (%2d, %2d, '%c')", zone_table[i].number,
                zone_table[i].x, zone_table[i].y, zone_table[i].symbol);
            strncat(buf, zone_table[i].name, 255);
            send_to_char(buf, ch);
        }
    send_to_char("\n\rEnd of list.\n\r", ch);
}

ACMD(do_setfree)
{
    int len;

    if (!*argument) {
        if (global_release_flag)
            send_to_char("RELEASE is allowed.\n\r", ch);
        else
            send_to_char("RELEASE is faked - be careful.\n\r", ch);
        return;
    }

    while (*argument && (*argument <= ' '))
        argument++;
    len = strlen(argument);

    if (!strncmp(argument, "on", len)) {
        global_release_flag = 1;
        send_to_char("You enabled pointer release.\n\r", ch);
    } else if (!strncmp(argument, "off", len)) {
        global_release_flag = 0;
        send_to_char("You disabled pointer release.\n\r", ch);
    } else {
        send_to_char("Use [on|off] to switch pointer RELEASE.\n\r", ch);
    }
    return;
}

extern universal_list* affected_list;
extern universal_list* affected_list_pool;

ACMD(do_rehash)
{
    char_data* tmpch;
    affected_type* tmpaf;
    universal_list* tmplist;
    int num, count1, count2, perms_only;

    count1 = 0;
    while (affected_list) {
        from_list_to_pool(&affected_list, &affected_list_pool, affected_list);
        count1++;
    }

    count2 = 0;

    for (tmpch = character_list; tmpch; tmpch = tmpch->next) {
        if (tmpch->affected) {
            tmplist = pool_to_list(&affected_list, &affected_list_pool);
            tmplist->ptr.ch = tmpch;
            tmplist->number = tmpch->abs_number;
            tmplist->type = TARGET_CHAR;

            count2++;
        }
    }

    for (num = 0; num <= top_of_world; num++) {
        perms_only = 1;
        for (tmpaf = world[num].affected; tmpaf; tmpaf = tmpaf->next) {
            if (!IS_SET(tmpaf->bitvector, PERMAFFECT))
                perms_only = 0;

            if ((tmpaf->type == ROOMAFF_SPELL) && (tmpaf->location >= 0) && (tmpaf->location != SPELL_NONE))
                perms_only = 0;
        }

        if (!perms_only) {
            tmplist = pool_to_list(&affected_list, &affected_list_pool);
            tmplist->ptr.room = &world[num];
            tmplist->number = world[num].number;
            tmplist->type = TARGET_ROOM;

            count2++;
        }
    }

    sprintf(buf, "(GC) %s rehashed affection, was %d, now %d.", GET_NAME(ch),
        count1, count2);

    mudlog(buf, NRM, LEVEL_GOD, TRUE);
}

/*
 * Decides whether or not `ch' can advance `vict' to `level'.
 * Returns non-zero if `ch' has permission; otherwise, a zero
 * is returned.  `ch' is denied permission if one of the
 * following conditions holds:
 *   - `ch' is trying to promote `vict' to a level higher than
 *     `ch's level minus three; that is, an arata (level 97)
 *     can promote to greater maia (level 94), but not a vala
 *     (level 95).
 *   - `ch' is trying to promote a `vict' whose level + 3 is
 *     greater than `ch's; that is, an arata (level 97) can
 *     change the level of a greater maia (level 94), but does
 *     not have permission to change the level of a vala (level
 *     95).
 * As a side note, if `ch' is an implementor, it will always be
 * granted permission to advance.
 */
int advance_perm(struct char_data* ch, struct char_data* vict, int level)
{
    if (GET_LEVEL(ch) == LEVEL_IMPL)
        return 1;

    if (level + 3 > GET_LEVEL(ch)) {
        sprintf(buf2, "You do not have permission to advance %s to level %d.\r\n",
            GET_NAME(vict), level);
        send_to_char(buf2, ch);
        return 0;
    }

    if (GET_LEVEL(vict) + 3 > GET_LEVEL(ch)) {
        sprintf(buf2, "You do not have permission to change %s's level.\r\n",
            GET_NAME(vict));
        send_to_char(buf2, ch);
        return 0;
    }

    return 1;
}

ACMD(do_top)
{

    int zonnum, tmp, vn, count, count1, race, i, oldest;
    char* tmpptr;

    char buf[255];
    char buf2[255];
    /*static*/ char arg2[MAX_INPUT_LENGTH];
    /*static*/ char arg3[MAX_INPUT_LENGTH];
    /*static*/ char arg4[MAX_INPUT_LENGTH];
    int lowest[25][2];

    for (tmpptr = argument; *tmpptr && isspace(*tmpptr); tmpptr++)
        ;
    for (tmp = 0; *tmpptr && *tmpptr > ' '; tmpptr++, tmp++)
        arg2[tmp] = *tmpptr;
    arg2[tmp] = 0;
    for (; *tmpptr && isspace(*tmpptr); tmpptr++)
        ;
    for (tmp = 0; *tmpptr && *tmpptr > ' '; tmpptr++, tmp++)
        arg3[tmp] = *tmpptr;
    arg3[tmp] = 0;
    for (; *tmpptr && isspace(*tmpptr); tmpptr++)
        ;
    for (tmp = 0; *tmpptr && *tmpptr > ' '; tmpptr++, tmp++)
        arg4[tmp] = *tmpptr;
    arg4[tmp] = 0;

    zonnum = 0;
    count = 0;

    if (isdigit(*arg2))
        zonnum = atoi(arg2);
    if (!*arg2) {
        send_to_char("Usage: top ## [[oldest] race].\n\r", ch);
        return;
    }

    oldest = 0;
    if (zonnum > 25)
        zonnum = 25;
    if (zonnum < 1)
        zonnum = 1;
    race = 0;
    if (!*arg3) {
        sprintf(buf, "Top %2d Characters\n\r", zonnum);
    } else {
        if (!strncmp("dark", arg3, strlen(arg3)) || !strncmp("shadow", arg3, strlen(arg3))) {

        } else if (!strncmp("white", arg3, strlen(arg3)) || !strncmp("light", arg3, strlen(arg3))) {

        } else if (!strncmp("human", arg3, strlen(arg3))) {
            sprintf(buf2, "Human ");
            race = 1;
        } else if (!strncmp("dwarf", arg3, strlen(arg3))) {
            sprintf(buf2, "Dwarf ");
            race = 2;
        } else if (!strncmp("elf", arg3, strlen(arg3)) || !strncmp("woodelf", arg3, strlen(arg3))) {
            sprintf(buf2, "Wood Elf ");
            race = 3;
        } else if (!strncmp("hobbit", arg3, strlen(arg3))) {
            sprintf(buf2, "Hobbit ");
            race = 4;
        } else if (!strncmp("bear", arg3, strlen(arg3)) || !strncmp("beorning", arg3, strlen(arg3))) {
            sprintf(buf2, "Beorning ");
            race = 6;
        } else if (!strncmp("uruk-hai", arg3, strlen(arg3))) {
            sprintf(buf2, "Uruk-Hai ");
            race = 11;
        } else if (!strncmp("orc", arg3, strlen(arg3))) {
            sprintf(buf2, "Common Orc ");
            race = 13;
        } else if (!strncmp("uruk-lhuth", arg3, strlen(arg3)) || !strncmp("lhuth", arg3, strlen(arg3))) {
            sprintf(buf2, "Uruk-Lhuth ");
            race = 15;
        } else if (!strncmp("olog", arg3, strlen(arg3)) || !strncmp("olog-hai", arg3, strlen(arg3))) {
            sprintf(buf2, "Olog-Hai ");
            race = RACE_OLOGHAI;
        } else if (!strncmp("harad", arg3, strlen(arg3)) || !strncmp("haradrim", arg3, strlen(arg3))) {
            sprintf(buf2, "Haradrim ");
            race = RACE_HARADRIM;
        } else if (!strncmp("oldest", arg3, strlen(arg3))) {
            sprintf(buf2, "Oldest Legend ");
            oldest = 1;
        }

        if (oldest == 1) {
            if (!strncmp("dark", arg4, strlen(arg4)) || !strncmp("shadow", arg4, strlen(arg4))) {

            } else if (!strncmp("white", arg4, strlen(arg4)) || !strncmp("light", arg4, strlen(arg4))) {

            } else if (!strncmp("human", arg4, strlen(arg4))) {
                sprintf(buf2, "%sHuman ", buf2);
                race = 1;
            } else if (!strncmp("dwarf", arg4, strlen(arg4))) {
                sprintf(buf2, "%sDwarf ", buf2);
                race = 2;
            } else if (!strncmp("elf", arg4, strlen(arg4)) || !strncmp("woodelf", arg4, strlen(arg4))) {
                sprintf(buf2, "%sWood Elf ", buf2);
                race = 3;
            } else if (!strncmp("hobbit", arg4, strlen(arg4))) {
                sprintf(buf2, "%sHobbit ", buf2);
                race = 4;
            } else if (!strncmp("uruk-hai", arg4, strlen(arg4))) {
                sprintf(buf2, "%sUruk-Hai ", buf2);
                race = 11;
            } else if (!strncmp("orc", arg4, strlen(arg4))) {
                sprintf(buf2, "%sCommon Orc ", buf2);
                race = 13;
            } else if (!strncmp("uruk-lhuth", arg4, strlen(arg4)) || !strncmp("lhuth", arg4, strlen(arg4))) {
                sprintf(buf2, "%sUruk-Lhuth ", buf2);
                race = 15;
            }
        }

        sprintf(buf, "Top %2d %sCharacters\n\r", zonnum, buf2);
    }

    send_to_char(buf, ch);
    if (oldest == 1) {
        for (count = 0; count < 25; count++) {
            lowest[count][0] = -1;
            lowest[count][1] = -1;
        }
        for (i = 0; i <= top_of_p_table; i++) {
            if (race > 0) {
                if (race != (player_table + i)->race) {
                    continue;
                }
            }
            if ((player_table + i)->level >= 30 && (player_table + i)->level <= 90) {
                for (count = 0; count < 25; count++) {
                    if (lowest[count][1] == -1 || lowest[count][1] > (player_table + i)->idnum) {
                        for (count1 = 25; count1 > count; count1--) {
                            lowest[count1][0] = lowest[count1 - 1][0];
                            lowest[count1][1] = lowest[count1 - 1][1];
                        }
                        lowest[count][0] = i;
                        lowest[count][1] = (player_table + i)->idnum;
                        break;
                    }
                }
            }
        }
        for (i = 0; i < zonnum; i++) {
            if ((lowest[i][0] >= 0) && (lowest[i][0] <= top_of_p_table)) {
                sprintf(buf, "%2d - %s\n\r", (player_table + lowest[i][0])->level, (player_table + lowest[i][0])->name);
                send_to_char(buf, ch);
            }
        }
    } else {
        count = 1;
        for (vn = 90; vn > 30 && count <= zonnum; vn--) {
            for (tmp = 0; tmp <= top_of_p_table; tmp++) {
                if ((player_table + tmp)->level == vn) {
                    if (race > 0) {
                        if (race != (player_table + tmp)->race) {
                            continue;
                        }
                    }
                    sprintf(buf, "%2d - %s\n\r", (player_table + tmp)->level, (player_table + tmp)->name);
                    send_to_char(buf, ch);
                    count++;
                }
            }
        }
    }
}

// --- savebench helpers (diagnostic command; see specs/2026-06-27-savebench-finalize-...) ---

// Byte-for-byte copy src -> dst. Returns true on success.
static bool sb_copy_file(const char *src, const char *dst) {
    FILE *in = fopen(src, "rb");
    if (!in) {
        return false;
    }
    FILE *out = fopen(dst, "wb");
    if (!out) {
        fclose(in);
        return false;
    }
    char chunk[4096];
    size_t n;
    bool ok = true;
    while ((n = fread(chunk, 1, sizeof(chunk), in)) > 0) {
        if (fwrite(chunk, 1, n, out) != n) {
            ok = false;
            break;
        }
    }
    fclose(in);
    fclose(out);
    return ok;
}

// True if both files exist and have identical contents.
static bool sb_files_identical(const char *a, const char *b) {
    FILE *fa = fopen(a, "rb");
    FILE *fb = fopen(b, "rb");
    if (!fa || !fb) {
        if (fa) {
            fclose(fa);
        }
        if (fb) {
            fclose(fb);
        }
        return false;
    }
    bool ok = true;
    int ca, cb;
    do {
        ca = fgetc(fa);
        cb = fgetc(fb);
        if (ca != cb) {
            ok = false;
            break;
        }
    } while (ca != EOF);
    fclose(fa);
    fclose(fb);
    return ok;
}

// Count non-dot entries in dir (-1 if it cannot be opened).
static int sb_count_files(const char *dir) {
    DIR *d = opendir(dir);
    if (!d) {
        return -1;
    }
    int count = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] != '.') {
            count++;
        }
    }
    closedir(d);
    return count;
}

// Remove every non-dot entry in dir, then rmdir it.
static void sb_remove_dir(const char *dir) {
    DIR *d = opendir(dir);
    if (d) {
        struct dirent *e;
        char path[512];
        while ((e = readdir(d)) != NULL) {
            if (e->d_name[0] == '.') {
                continue;
            }
            snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
            unlink(path);
        }
        closedir(d);
    }
    rmdir(dir);
}

// Microseconds elapsed from t0 to t1.
static long sb_usec_diff(const struct timeval *t0, const struct timeval *t1) {
    return (long)(t1->tv_sec - t0->tv_sec) * 1000000L + (t1->tv_usec - t0->tv_usec);
}

ACMD(do_savebench) {
    if (IS_NPC(ch) || !ch->desc) {
        send_to_char("savebench is not available.\n\r", ch);
        return;
    }

    int iterations = 100;
    while (*argument == ' ') {
        argument++;
    }
    if (*argument) {
        int n = atoi(argument);
        if (n > 0) {
            iterations = n;
        }
    }
    if (iterations < 1) {
        iterations = 1;
    }
    if (iterations > 10000) {
        iterations = 10000;
    }

    // All paths under players/ so rename() stays same-filesystem. base_name is synthetic
    // ("probe") and dot-anchored; it can never match a real player's files.
    const char *legacy_dir = "players/SAVEBENCH_LEGACY";
    const char *new_dir = "players/SAVEBENCH_NEW";
    const char *master = "players/savebench_master";
    const char *legacy_scratch = "players/savebench_legacy_scratch";
    const char *new_scratch = "players/savebench_new_scratch";
    const char *base_name = "probe";

    char legacy_base[128], legacy_versioned[200], new_versioned[200];
    char stale_legacy[160], stale_new[160];
    snprintf(legacy_base, sizeof(legacy_base), "%s/%s", legacy_dir, base_name);
    snprintf(legacy_versioned, sizeof(legacy_versioned), "%s.%d.%d.%ld.0.0", legacy_base,
             (int)GET_LEVEL(ch), (int)GET_RACE(ch), (long)GET_IDNUM(ch));
    snprintf(new_versioned, sizeof(new_versioned), "%s/%s.%d.%d.%ld.0.0", new_dir, base_name,
             (int)GET_LEVEL(ch), (int)GET_RACE(ch), (long)GET_IDNUM(ch));
    snprintf(stale_legacy, sizeof(stale_legacy), "%s/%s.stale", legacy_dir, base_name);
    snprintf(stale_new, sizeof(stale_new), "%s/%s.stale", new_dir, base_name);

    mkdir(legacy_dir, 0775);
    mkdir(new_dir, 0775);

    // Serialize THIS character once; copy those exact bytes to each finalizer so the
    // equivalence comparison is immune to any time-based field drift between serializations.
    if (!write_player_text(ch, 0, master)) {
        send_to_char("savebench: write_player_text failed.\n\r", ch);
        sb_remove_dir(legacy_dir);
        sb_remove_dir(new_dir);
        return;
    }

    // ---- Equivalence check ----
    sb_copy_file(master, stale_legacy); // stale, differently-suffixed seed the glob must remove
    sb_copy_file(master, stale_new);
    sb_copy_file(master, legacy_scratch);
    sb_copy_file(master, new_scratch);

    finalize_player_file_legacy(legacy_scratch, legacy_base, legacy_versioned);
    finalize_player_file_rename(new_scratch, new_dir, base_name, new_versioned);

    bool identical = sb_files_identical(legacy_versioned, new_versioned);
    int legacy_count = sb_count_files(legacy_dir);
    int new_count = sb_count_files(new_dir);

    // ---- Profiling: time ONLY the finalize call; regenerate scratch outside timing. ----
    long lmin = -1, lmax = 0, ltot = 0;
    long nmin = -1, nmax = 0, ntot = 0;
    struct timeval t0, t1;

    for (int i = 0; i < iterations; i++) {
        sb_copy_file(master, legacy_scratch);
        gettimeofday(&t0, NULL);
        finalize_player_file_legacy(legacy_scratch, legacy_base, legacy_versioned);
        gettimeofday(&t1, NULL);
        long us = sb_usec_diff(&t0, &t1);
        ltot += us;
        if (lmin < 0 || us < lmin) {
            lmin = us;
        }
        if (us > lmax) {
            lmax = us;
        }
    }

    for (int i = 0; i < iterations; i++) {
        sb_copy_file(master, new_scratch);
        gettimeofday(&t0, NULL);
        finalize_player_file_rename(new_scratch, new_dir, base_name, new_versioned);
        gettimeofday(&t1, NULL);
        long us = sb_usec_diff(&t0, &t1);
        ntot += us;
        if (nmin < 0 || us < nmin) {
            nmin = us;
        }
        if (us > nmax) {
            nmax = us;
        }
    }

    double lavg = (double)ltot / iterations;
    double navg = (double)ntot / iterations;

    // ---- Report ----
    sprintf(buf,
            "savebench (self, N=%d)\n\r"
            "  equivalence : %s\n\r"
            "  files left  : legacy=%d  new=%d  (expect 1 each)\n\r"
            "  legacy (system rm+cp) : min %ld / avg %.1f / max %ld usec\n\r"
            "  new    (unlink+rename): min %ld / avg %.1f / max %ld usec\n\r",
            iterations, identical ? "IDENTICAL" : "*** DIFFERENT ***", legacy_count, new_count,
            lmin, lavg, lmax, nmin, navg, nmax);
    if (navg > 0.0) {
        sprintf(buf + strlen(buf), "  speedup     : ~%.0fx\n\r", lavg / navg);
    }
    send_to_char(buf, ch);

    // ---- Cleanup (unconditional) ----
    unlink(master);
    unlink(legacy_scratch);
    unlink(new_scratch);
    sb_remove_dir(legacy_dir);
    sb_remove_dir(new_dir);
}
