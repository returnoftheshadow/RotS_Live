
/* ************************************************************************
 *   File: act.informative.c                             Part of CircleMUD *
 *  Usage: Player-level commands of an informative nature                  *
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
#include <time.h>

#include "boards.h"
#include "color.h"
#include "comm.h"
#include "db.h"
#include "handler.h"
#include "interpre.h"
#include "limits.h"
#include "pkill.h"
#include "script.h"
#include "skill_timer.h"
#include "spells.h"
#include "structs.h"
#include "utils.h"
#include "warrior_spec_handlers.h"
#include "zone.h" /* For zone_table */

#include "big_brother.h"
#include "char_utils.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

/* extern variables */
extern struct room_data world;
extern struct descriptor_data* descriptor_list;
extern struct char_data* character_list;
extern struct obj_data* object_list;
extern struct command_info cmd_info[];
extern char* room_spell_message[];
extern char* room_bits_message[];
extern struct player_index_element* player_table;
extern struct skill_data skills[];
extern char world_map[];
extern char small_map[][4 * SMALL_WORLD_RADIUS + 7];
extern struct char_data* waiting_list;
extern struct index_data* mob_index;
extern struct index_data* obj_index;
extern int social_list_top;
extern struct social_messg* soc_mess_list;
extern struct crime_record_type* crime_record;
extern int num_of_crimes;
extern long beginning_of_time;
extern char* credits;
extern char* news;
extern char* info;
extern char* wizlist;
extern char* immlist;
extern char* policies;
extern char* handbook;
extern char* dirs[];
extern char* refer_dirs[];
extern char* where[];
extern char* color_liquid[];
extern char* fullness[];
extern char* connected_types[];
extern char* command[];
extern char* prof_abbrevs[];
extern char* race_abbrevs[];
extern char* room_bits[];
extern int top_of_p_table;
extern char* sector_types[];
extern char* moon_phase[];
extern long judppwd;
extern int judpavailable;
extern char* beornwhere[];

extern char* extra_bits[];
extern int num_of_object_materials;
extern char* apply_types[];
extern char* drinks[];
extern char* pc_arda_fame_identifier[];
extern char* pc_evil_fame_identifier[];

void symbol_to_map(int, int, int);
void reset_small_map();

ACMD(do_orc_delay);
ACMD(do_affections);
ACMD(do_wizstat);
ACMD(do_trap);

void stop_hiding(struct char_data* ch, char);

/* intern functions & vars*/
int num_of_cmds;
void list_obj_to_char(struct obj_data*, struct char_data*, int, bool);
void calculate_small_map(int, int);
void report_mob_age(struct char_data*, struct char_data*);
void report_char_mentals(struct char_data*, char*, int);
void report_affection(struct affected_type*, char*);
void report_perception(struct char_data*, char*);
int show_tracks(struct char_data*, char*, int);
static char* get_level_abbr(sh_int level, sh_int);
int show_blood_trail(struct char_data*, char*, int);

namespace {
/* Simple structure for handling inventory formatting data. */
struct inventory_data {
    inventory_data()
        : description()
    {
        count = 0;
    }

    std::string description;
    int count;
};

struct inventory_data_sort_alphabetically {
    bool operator()(const inventory_data& left, const inventory_data& right)
    {
        return left.description < right.description;
    }
};

struct inventory_data_sort_by_length {
    bool operator()(const inventory_data& left, const inventory_data& right)
    {
        return left.description.size() < right.description.size();
    }
};

bool use_inventory_formatter(char_data* character)
{
    bool high_bit_set = utils::is_preference_flagged(*character, PRF_INV_SORT2);
    bool low_bit_set = utils::is_preference_flagged(*character, PRF_INV_SORT1);

    return high_bit_set || low_bit_set;
}

bool use_alpha_sorting(char_data* character)
{
    bool high_bit_set = utils::is_preference_flagged(*character, PRF_INV_SORT2);
    bool low_bit_set = utils::is_preference_flagged(*character, PRF_INV_SORT1);

    return high_bit_set && !low_bit_set;
}

bool use_length_sorting(char_data* character)
{
    bool high_bit_set = utils::is_preference_flagged(*character, PRF_INV_SORT2);
    bool low_bit_set = utils::is_preference_flagged(*character, PRF_INV_SORT1);

    return high_bit_set && low_bit_set;
}

/* Class that will return the contents of a container in a formatted string. */
class inventory_formatter {
public:
    inventory_formatter(obj_data* root_object, char_data* character)
    {
        m_root_object = root_object;
        m_character = character;
        m_working_data.reserve(64);
    }

    std::string format_inventory()
    {
        for (obj_data* item = m_root_object; item; item = item->next_content) {
            add_item_to_list(item);
        }

        if (m_seen_items.empty()) {
            return std::string(" Nothing.\n\r");
        }

        bool sort_alpha = use_alpha_sorting(m_character);
        bool sort_length = use_length_sorting(m_character);

        if (sort_alpha) {
            inventory_data_sort_alphabetically sorter;
            std::sort(m_seen_items.begin(), m_seen_items.end(), sorter);
        } else if (sort_length) {
            // stable sort is used for length because we want to maintain as much of
            // the original order as possible.
            inventory_data_sort_by_length sorter;
            std::stable_sort(m_seen_items.begin(), m_seen_items.end(), sorter);
        }

        std::ostringstream inventory_writer;
        for (size_t index = 0; index < m_seen_items.size(); ++index) {
            const inventory_data& item_data = m_seen_items[index];
            inventory_writer << item_data.description;
            if (item_data.count > 1) {
                inventory_writer << " (" << item_data.count << ')';
            }
            inventory_writer << std::endl;
        }

        inventory_writer << std::endl;
        return inventory_writer.str();
    }

private:
    void add_item_to_list(obj_data* object)
    {
        get_item_description(object, m_working_data);

        for (size_t index = 0; index < m_seen_items.size(); ++index) {
            inventory_data& item_data = m_seen_items[index];
            if (m_working_data == item_data.description) {
                ++item_data.count;
                return;
            }
        }

        inventory_data new_item;
        new_item.description.assign(m_working_data);
        new_item.count = 1;
        m_seen_items.push_back(new_item);
    }

    void get_item_description(obj_data* object, std::string& working_data)
    {
        // reset the state of working data - this shouldn't force a reallocation.
        working_data.clear();

        if (!CAN_SEE_OBJ(m_character, object)) {
            working_data.append("Something.");
            return;
        }

        working_data.append(object->short_description);

        if (IS_OBJ_STAT(object, ITEM_INVISIBLE)) {
            working_data.append("(invisible");
        }

        if (IS_OBJ_STAT(object, ITEM_EVIL) && IS_AFFECTED(m_character, AFF_DETECT_INVISIBLE)) {
            working_data.append("..It glows red!");
        }
        if (IS_OBJ_STAT(object, ITEM_MAGIC) && IS_AFFECTED(m_character, AFF_DETECT_MAGIC)) {
            working_data.append("..It glows blue!");
        }
        if (IS_OBJ_STAT(object, ITEM_WILLPOWER) && IS_SHADOW(m_character)) {
            working_data.append(" ..It has a powerful aura!");
        }
        if (IS_OBJ_STAT(object, ITEM_GLOW)) {
            working_data.append("..It has a soft glowing aura!");
        }
        if (IS_OBJ_STAT(object, ITEM_HUM)) {
            working_data.append("..It emits a faint humming sound!");
        }
        if (IS_OBJ_STAT(object, ITEM_BROKEN)) {
            working_data.append(" (broken)");
        }
        if ((GET_ITEM_TYPE(object) == ITEM_LIGHT) && (object->obj_flags.value[2] && object->obj_flags.value[3])) {
            working_data.append("..It glows brightly.");
        }
    }

    obj_data* m_root_object;
    char_data* m_character;

    // string that is used to store temporary work
    std::string m_working_data;
    std::vector<inventory_data> m_seen_items;
};
} // namespace

/* Procedures related to 'look' */
void argument_split_2(char* argument, char* first_arg, char* second_arg)
{
    int look_at, found, begin;
    found = begin = 0;

    /* Find first non blank */
    for (; *(argument + begin) == ' '; begin++)
        ;

    /* Find length of first word */
    for (look_at = 0; *(argument + begin + look_at) > ' '; look_at++)
        /* Make all letters lower case, AND copy them to first_arg */
        *(first_arg + look_at) = LOWER(*(argument + begin + look_at));
    *(first_arg + look_at) = '\0';
    begin += look_at;

    /* Find first non blank */
    for (; *(argument + begin) == ' '; begin++)
        ;

    /* Find length of second word */
    for (look_at = 0; *(argument + begin + look_at) > ' '; look_at++)
        /* Make all letters lower case, AND copy them to second_arg */
        *(second_arg + look_at) = LOWER(*(argument + begin + look_at));
    *(second_arg + look_at) = '\0';
    begin += look_at;
}

char* find_ex_description(char* word, struct extra_descr_data* list)
{
    struct extra_descr_data* i;

    for (i = list; i; i = i->next)
        if (isname(word, i->keyword))
            return (i->description);

    return 0;
}

void show_obj_to_char(struct obj_data* object, struct char_data* ch, int mode)
{
    char found;

    *buf = '\0';

    if ((mode == 0) && object->description)
        sprintf(buf, "%s", object->description);
    else if (object->short_description && ((mode == 1) || (mode == 2) || (mode == 3) || (mode == 4)))
        sprintf(buf, "%s", object->short_description);
    else if (mode == 5) { /* The trigger will deal with what is sent to ch */
        if (!call_trigger(ON_EXAMINE_OBJECT, object, ch, 0))
            return;

        if (object->obj_flags.type_flag == ITEM_NOTE) {
            if (object->action_description) {
                strcpy(buf, "There is something written upon it:\n\r\n\r");
                strcat(buf, object->action_description);
                page_string(ch->desc, buf, 1);
            } else
                act("It's blank.", FALSE, ch, 0, 0, TO_CHAR);
            return;
        } else if (object->action_description)
            strcpy(buf, object->action_description);
        else if ((object->obj_flags.type_flag != ITEM_DRINKCON))
            strcpy(buf, "You see nothing special..\n\r");
        else /* ITEM_TYPE == ITEM_DRINKCON||FOUNTAIN */
            strcpy(buf, "It looks like a drink container.\n\r");
    }

    if ((mode != 3) && (mode != 5)) {
        found = FALSE;
        if (IS_OBJ_STAT(object, ITEM_INVISIBLE)) {
            strcat(buf, "(invisible)");
            found = TRUE;
        }
        if (IS_OBJ_STAT(object, ITEM_EVIL) && IS_AFFECTED(ch, AFF_DETECT_INVISIBLE)) {
            strcat(buf, "..It glows red!");
            found = TRUE;
        }
        if (IS_OBJ_STAT(object, ITEM_MAGIC) && !IS_OBJ_STAT(object, ITEM_ANTI_GOOD) && IS_AFFECTED(ch, AFF_DETECT_MAGIC)) {
            strcat(buf, "..It glows blue!");
            found = TRUE;
        }
        if (IS_OBJ_STAT(object, ITEM_ANTI_GOOD) && IS_OBJ_STAT(object, ITEM_MAGIC) && IS_AFFECTED(ch, AFF_DETECT_MAGIC)) {
            strcat(buf, "..It glows red!");
            found = TRUE;
        }
        if (IS_OBJ_STAT(object, ITEM_WILLPOWER) && IS_SHADOW(ch)) {
            strcat(buf, " ..It has a powerful aura!");
            found = TRUE;
        }
        if (IS_OBJ_STAT(object, ITEM_GLOW)) {
            strcat(buf, "..It has a soft glowing aura!");
            found = TRUE;
        }
        if (IS_OBJ_STAT(object, ITEM_HUM)) {
            strcat(buf, "..It emits a faint humming sound!");
            found = TRUE;
        }
        if (IS_OBJ_STAT(object, ITEM_BROKEN)) {
            strcat(buf, " (broken)");
            found = TRUE;
        }
        if ((GET_ITEM_TYPE(object) == ITEM_LIGHT) && (object->obj_flags.value[2] && object->obj_flags.value[3]))
            strcat(buf, "..It glows brightly.");
    }
    if (mode != 5)
        strcat(buf, "\n\r");

    send_to_char(buf, ch);
}

/** The 'show' flag is true for containers and false for rooms. */
void list_obj_to_char(obj_data* list, char_data* ch, int mode, bool show)
{
    bool use_formatter = use_inventory_formatter(ch);
    if (show && use_formatter) {
        inventory_formatter formatter(list, ch);
        std::string inventory_message = formatter.format_inventory();
        send_to_char(inventory_message.c_str(), ch);
    } else {
        bool found = show;
        for (obj_data* root_object = list; root_object; root_object = root_object->next_content) {
            if (CAN_SEE_OBJ(ch, root_object)) {
                show_obj_to_char(root_object, ch, mode);
                found = true;
            } else if (show) {
                send_to_char("Something.\n\r", ch);
            }
        }

        // The character should get a report that the container is empty.
        if (show && !found) {
            send_to_char(" Nothing.\n\r", ch);
        }
    }
}

void show_equipment_to_char(struct char_data* from, struct char_data* to)
{
    int j;
    char found;

    found = FALSE;
    for (j = 0; j < MAX_WEAR; j++) {
        if (from->equipment[j]) {
            found = TRUE;

            if (j == WIELD && IS_TWOHANDED(from))
                send_to_char(where[WIELD_TWOHANDED], to);
            else {
                if (GET_RACE(from) == RACE_BEORNING) {
                    send_to_char(beornwhere[j], to);
                } else {
                    send_to_char(where[j], to);
                }
            }

            if (CAN_SEE_OBJ(to, from->equipment[j]))
                show_obj_to_char(from->equipment[j], to, 1);
            else
                send_to_char("Something.\n\r", to);
        }
    }

    if (!found)
        send_to_char(" Nothing.\n\r", to);
}

extern struct prompt_type health_diagnose[];

void report_char_health(struct char_data* ch, struct char_data* i, char* str)
{
    int tmp, percent;

    strcpy(str, PERS(i, ch, TRUE, FALSE));

    if (GET_MAX_HIT(i) > 0)
        percent = (1000 * GET_HIT(i)) / GET_MAX_HIT(i);
    else
        percent = -1;

    for (tmp = 0; health_diagnose[tmp].value < percent; tmp++)
        ;

    strcat(str, health_diagnose[tmp].message);
}

void diag_char_to_char(char_data* looked_at, char_data* viewer)
{
    char str[255], strname[255];
    struct affected_type* tmpaff;

    strcpy(strname, PERS(looked_at, viewer, TRUE, FALSE));

    *buf = 0;
    report_char_health(viewer, looked_at, buf);
    report_char_mentals(looked_at, str, 0);
    sprintf(buf, "%s%s is %s.\n\r", buf, strname, str);
    send_to_char(buf, viewer);
    if (IS_NPC(looked_at)) {
        report_mob_age(viewer, looked_at);
    }
    if (IS_AFFECTED(viewer, AFF_DETECT_MAGIC)) {
        bool is_exposed_to_elements = false;
        if (viewer->extra_specialization_data.is_mage_spec()) {
            elemental_spec_data* spec_data = static_cast<elemental_spec_data*>(
                viewer->extra_specialization_data.current_spec_info);
            is_exposed_to_elements = looked_at == spec_data->exposed_target;
        }

        game_rules::big_brother& bb_instance = game_rules::big_brother::instance();
        bool is_protected = !bb_instance.is_target_valid(viewer, looked_at);
        if (looked_at->is_affected() == false && is_protected == false && is_exposed_to_elements == false) {
            sprintf(buf, "%s is not affected by anything.\n\r", strname);
            send_to_char(buf, viewer);
        } else {
            sprintf(buf, "%s is affected by:\n\r", strname);
            if (is_protected) {
                sprintf(buf, "%-30s (special)\n\r", "holy protection");
            }
            if (is_exposed_to_elements) {
                sprintf(buf, "%-30s (special)\n\r", "expose elements");
            }
            for (tmpaff = looked_at->affected; tmpaff; tmpaff = tmpaff->next) {
                report_affection(tmpaff, str);
                strcat(buf, str);
            }
            send_to_char(buf, viewer);
        }
        report_perception(looked_at, str);
        send_to_char(str, viewer);
    }
}

/*
 * Puts a line into `str' describing how `ch' sees `i'; i.e.:
 * "i is sitting/standing/whatever here."
 *
 * NOTE: WILL CLOBBER COLOR.
 */
void get_char_position_line(struct char_data* ch, struct char_data* i, char* str)
{
    str[0] = 0;

    if (!(i->player.long_descr) || (GET_POS(i) != i->specials.default_pos) || (IS_NPC(i) && MOB_FLAGGED(i, MOB_ORC_FRIEND) && MOB_FLAGGED(i, MOB_PET))) {
        switch (GET_POS(i)) {
        case POSITION_SHAPING:
            strcat(str, " is sitting here in deep meditation,\r\n"
                        "softly humming the ancient song of creation.");
            break;
        case POSITION_STUNNED:
            strcat(str, " is lying here, stunned.");
            break;
        case POSITION_INCAP:
            strcat(str, " is lying here, mortally wounded.");
            break;
        case POSITION_DEAD:
            strcat(str, " is lying here, dead.");
            break;
        case POSITION_STANDING:
            strcat(str, " is standing here.");
            break;
        case POSITION_SITTING:
            strcat(str, " is sitting here.");
            break;
        case POSITION_RESTING:
            strcat(str, " is resting here.");
            break;
        case POSITION_SLEEPING:
            strcat(str, " is sleeping here.");
            break;
        case POSITION_FIGHTING:
            if (i->specials.fighting) {
                strcat(str, " is here, fighting ");
                if (i->specials.fighting == ch)
                    strcat(str, "YOU!");
                else {
                    if (i->in_room == i->specials.fighting->in_room)
                        strcat(buf, PERS(i->specials.fighting, ch, FALSE, FALSE));
                    else
                        strcat(buf, "SOMEONE WHO ALREADY LEFT! *BUG*!");
                    strcat(buf, ".");
                }
            } else /* NIL fighting pointer */
                strcat(str, " is here struggling with thin air.");
            break;
        default:
            strcat(str, " is floating here.");
            break;
        }
    }
}

void get_char_flag_line(char_data* viewer, char_data* viewed, char* character_message)
{
    if (IS_AFFECTED(viewer, AFF_DETECT_INVISIBLE)) {
        if (IS_EVIL(viewed)) {
            strcat(buf, " (red aura)");
        }
    }

    if (other_side(viewer, viewed) && (viewed->player.ranking > 0 && viewed->player.ranking <= 3)) {
        if (utils::is_race_evil(*viewed))
            strcat(character_message, pc_evil_fame_identifier[viewed->player.ranking]);
        else
            strcat(character_message, pc_arda_fame_identifier[viewed->player.ranking]);
    }

    if (IS_AFFECTED(viewed, AFF_HIDE)) {
        strcat(character_message, " (hiding)");
    }
    if (IS_AFFECTED(viewed, AFF_WAITING)) {
        strcat(character_message, " (busy)");
    }
    if (IS_AFFECTED(viewed, AFF_INVISIBLE)) {
        strcat(character_message, " (invisible)");
    }
    if (!IS_NPC(viewed) && !(viewed->desc && viewed->desc->descriptor)) {
        strcat(character_message, " (linkless)");
    }
    if (PLR_FLAGGED(viewed, PLR_WRITING)) {
        strcat(character_message, " (writing)");
    }
    if (PLR_FLAGGED(viewed, PLR_ISAFK)) {
        strcat(character_message, " (AFK)");
    }
    if (IS_AFFECTED(viewed, AFF_SANCTUARY)) {
        strcat(character_message, " (glowing)");
    }
    if (IS_SHADOW(viewed)) {
        strcat(character_message, " (shadow)");
    }

    if ((utils::is_affected_by_spell(*viewed, SKILL_MARK)) && (GET_RACE(viewer) == RACE_HARADRIM)) {
        strcat(character_message, " (marked)");
    }

    if ((MOB_FLAGGED(viewed, MOB_PET) || MOB_FLAGGED(viewed, MOB_ORC_FRIEND)) || !IS_NPC(viewed)) {
        game_rules::big_brother& bb_instance = game_rules::big_brother::instance();
        if (!bb_instance.is_target_valid(viewer, viewed)) {
            strcat(character_message, " (holy protection)");
        }
    }
}

/*
 * This function is ridiculously long, loopy, and hard to
 * understand; but it has finally reached a point where it will
 * display a mount with any number of riding characters without
 * implying anything untrue about any of the riders, all while
 * keeping itself decently within the bounds of english grammar.
 *
 * If you want to add a new positional message (a special message),
 * you need to not only add the are/is if block to the beginning of
 * your case, but you must also include it in the initial loop
 * through the rider list so that will_have_special_message is set
 * appropriately.
 *
 * On line1 and line2: nowhere in the code does line1 differ from
 * line2.  It is unclear what the purpose of splitting these two
 * arguments is.
 *
 * If 'color' is true, then we color this message.
 */
void show_mount_to_char(struct char_data* i, struct char_data* ch, char* line1, char* line2,
    int color)
{
    int vis_count, tmpnum, you_are_riding, riderno;
    int special_message;
    struct char_data *tmpch, *last_rider;

    you_are_riding = special_message = vis_count = 0;
    *buf = 0;

    if (color)
        strcat(buf, CC_USE(ch, COLOR_CHAR));

    /*
     * We NEED to know if there are multiple people riding one mount and
     * whether or not ANY of those riders will generate a special message
     */
    tmpch = i->mount_data.rider;
    tmpnum = i->mount_data.rider_number;
    for (riderno = 0; tmpch && char_exists(tmpnum);
         tmpch = tmpch->mount_data.next_rider, ++riderno) {
        if (CAN_SEE(ch, tmpch)) {
            tmpnum = tmpch->mount_data.next_rider_number;
            if (GET_POS(tmpch) == POSITION_FIGHTING || GET_POS(tmpch) == POSITION_RESTING)
                special_message = 1;
        } else
            --riderno;
    }

    tmpch = i->mount_data.rider;
    tmpnum = i->mount_data.rider_number;
    for (; tmpch && char_exists(tmpnum); tmpch = tmpch->mount_data.next_rider) {
        if (CAN_SEE(ch, tmpch)) {
            /* This block facilitates the junctions between multiple riders */
            if (vis_count == riderno - 1 && vis_count)
                strcat(buf, " and ");
            /* The special messages have commas */
            else if (vis_count) {
                if (!special_message)
                    strcat(buf, ", ");
                else /* But they don't have spaces */
                    strcat(buf, " ");
            }

            if (ch == tmpch) {
                sprintf(buf + strlen(buf), "%cou", !vis_count ? 'Y' : 'y');
                you_are_riding = 1;
            } else {
                /* Unfortunately, act can't take arbitrary numbers of riders */
                strcat(buf,
                    !vis_count ? PERS(tmpch, ch, TRUE, FALSE) : PERS(tmpch, ch, FALSE, FALSE));
                if (color)
                    strcat(buf, CC_USE(ch, COLOR_CHAR));
            }

            get_char_flag_line(ch, tmpch, buf + strlen(buf));

            switch (GET_POS(tmpch)) {
            case POSITION_RESTING:
                /*
                 * If special_message is 0, then the last person in the list
                 * isn't doing anything, and thus we are seperated from them
                 * by either " and " or ", ", so we need to use 'are', not
                 * 'is'.  This is the is/are if block mentioned in the comment
                 * heading this function.
                 */
                if (tmpch == ch || (!special_message && vis_count))
                    strcat(buf, " are");
                else /* tmpch is not the viewer */
                    strcat(buf, " is");
                strcat(buf, " resting here,");
                break;

            case POSITION_FIGHTING:
                /* Same is/are block as above */
                if (tmpch == ch || (!special_message && vis_count))
                    strcat(buf, " are");
                else /* tmpch is not the viewer */
                    strcat(buf, " is");
                if (tmpch->specials.fighting) {
                    strcat(buf, " here, fighting ");
                    if (tmpch->specials.fighting == ch)
                        strcat(buf, "YOU");
                    else {
                        if (tmpch->in_room == tmpch->specials.fighting->in_room) {
                            strcat(buf, PERS(tmpch->specials.fighting, ch, FALSE, FALSE));
                            if (color)
                                strcat(buf, CC_USE(ch, COLOR_CHAR));
                        } else
                            strcat(buf, "SOMEONE THAT ALREADY LEFT! *BUG*");
                    }
                } else /* NIL fighting pointer */
                    strcat(buf, " here struggling with thin air");
                strcat(buf, ",");
                break;

            default:
                /*
                 * If there's more than one rider, and at least one has a
                 * special message, we need to be sure that the message we
                 * generate does not imply that all of the other riders are
                 * affected by the same message, so they ALL get the special
                 * message "is here," if they didn't get one already.
                 */
                if (riderno > 1 && special_message) {
                    if (ch == tmpch)
                        strcat(buf, " are");
                    else
                        strcat(buf, " is");
                    strcat(buf, " here,");
                }
                break;
            }
            vis_count++;
        }
        last_rider = tmpch;
        tmpnum = tmpch->mount_data.next_rider_number;
    }

    /* It's just a mount, standing there, with no one on it */
    if (!vis_count) {
        tmpch = i->mount_data.rider;
        tmpnum = i->mount_data.rider_number;
        i->mount_data.rider = 0;
        i->mount_data.rider_number = 0;
        show_char_to_char(i, ch, 0);
        i->mount_data.rider = tmpch;
        i->mount_data.rider_number = tmpnum;

        return;
    } else { /* It's a ridden mount */
        /* None of the riders had a special message */
        if (!special_message) {
            if (last_rider == ch || you_are_riding || (!you_are_riding && riderno > 1))
                strcat(buf, " are");
            else if (vis_count == 1)
                strcat(buf, " is");
        }

        /*
         * Multiple riders only; if we didn't have this statement, it
         * would seem as though only the last person in our list is
         * actually riding on the mount
         */
        if (riderno > 1) {
            if (riderno == 2)
                strcat(buf, " both");
            else if (riderno > 2)
                strcat(buf, " all");
            if (special_message) {
                if (you_are_riding)
                    strcat(buf, " of you");
                else
                    strcat(buf, " of them");
            }
        }

        if ((vis_count == 1) && !you_are_riding)
            strcat(buf, line1);
        else
            strcat(buf, line2);
        strcat(buf, PERS(i, ch, FALSE, FALSE));
        get_char_flag_line(ch, i, buf + strlen(buf));
        strcat(buf, ".\n\r");
        strcat(buf, CC_NORM(ch));
        CAP(buf);
        send_to_char(buf, ch);
    }
}

extern char* spec_pro_message[];

/*
 * `i' is the character being shown; `ch' is the character who is
 * viewing `i'; mode tells us whether or not this call is part of
 * a room description (mode = 0) or a look routine (mode = 1).
 * Note that `i' should NEVER be a mount.  Use show_mount_to_char
 * for that.
 */
void show_char_to_char(struct char_data* i, struct char_data* ch, int mode, char* pos_line)
{
    int found;
    struct obj_data* tmp_obj;

    /* 'ch' looked at a room, and 'i' is in that room */
    if (mode == 0) {
        if (IS_AFFECTED(i, AFF_HIDE) && !CAN_SEE(ch, i)) {
            if (can_sense(ch, i))
                send_to_char("You sense a hidden life form in the room.\n\r", ch);
            return;
        }

        if (!CAN_SEE(ch, i))
            return;

        /*
         * A player char or a mobile without long descr, or not in the
         * default posistion, or a charmed orc-friend: basically anyone
         * who needs a special sort of message.
         */
        if ((!i->player.long_descr || GET_POS(i) != i->specials.default_pos || pos_line) || (IS_NPC(i) && MOB_FLAGGED(i, MOB_ORC_FRIEND) && MOB_FLAGGED(i, MOB_PET) && other_side(ch, i))) {
            if (!pos_line) {
                sprintf(buf, "%s%s%s", CC_USE(ch, COLOR_CHAR), PERS(i, ch, TRUE, FALSE),
                    CC_USE(ch, COLOR_CHAR));
                if (!IS_NPC(i) && !other_side(ch, i))
                    sprintf(buf + strlen(buf), " %s", GET_TITLE(i));

                get_char_flag_line(ch, i, buf + strlen(buf));
                get_char_position_line(ch, i, buf + strlen(buf));
            } else {
                sprintf(buf, "%s", PERS(i, ch, TRUE, FALSE));
                get_char_flag_line(ch, i, buf + strlen(buf));
                strcat(buf, pos_line);
            }
        } else { /* npc with long that's in the usual position */
            *buf = 0;
            strcat(buf, CC_USE(ch, COLOR_CHAR));
            strcat(buf, i->player.long_descr);
            get_char_flag_line(ch, i, buf + strlen(buf));
        }

        CAP(buf);
        strcat(buf, "\n\r");
        strcat(buf, CC_NORM(ch));
        send_to_char(buf, ch);

        /* Show spec_prog related messages: i.e., block exit, plant fragrance */
        if (i->specials.store_prog_number && (!IS_NPC(i) || (!mob_index[i->nr].func && MOB_FLAGGED(i, MOB_SPEC)))) {
            if (*spec_pro_message[i->specials.store_prog_number])
                act(spec_pro_message[i->specials.store_prog_number], FALSE, i, 0, ch, TO_VICT);
        }
    } else if (mode == 1) { /* `ch' performed `look at `i'' */
        if (i->player.description) {
            if (*i->player.description)
                send_to_char(i->player.description, ch);
            else
                act("You see nothing special about $m.", FALSE, i, 0, ch, TO_VICT);
        } else {
            log("show_char: No description.");
            if (GET_NAME(i))
                sprintf(buf, "show_char: No description on %s.\n", GET_NAME(i));
            act("You see nothing special about $m.", FALSE, i, 0, ch, TO_VICT);
        }

        *buf = 0;
        report_char_health(ch, i, buf);
        send_to_char(buf, ch);

        act("\n\r$n is using:", FALSE, i, 0, ch, TO_VICT);
        show_equipment_to_char(i, ch);

        /* Immortals and thieves get a chance to look at inventories */
        if ((GET_PROF(ch) == PROF_THIEF || GET_LEVEL(ch) >= LEVEL_IMMORT) && ch != i) {
            found = FALSE;
            send_to_char("\n\rYou attempt to peek at the inventory:\n\r", ch);
            for (tmp_obj = i->carrying; tmp_obj; tmp_obj = tmp_obj->next_content) {
                if (CAN_SEE_OBJ(ch, tmp_obj) && (number(0, 20) < GET_LEVEL(ch))) {
                    show_obj_to_char(tmp_obj, ch, 1);
                    found = TRUE;
                }
            }
        }
    } else if (mode == 2) { /* Lists inventory */
        act("$n is carrying:", FALSE, i, 0, ch, TO_VICT);
        list_obj_to_char(i->carrying, ch, 1, true);
    }
}

void list_char_to_char(struct char_data* list, struct char_data* ch, int mode)
{
    struct char_data* i;
    int should_show;

    for (i = list; i; i = i->next_in_room)
        if (ch != i) {
            should_show = 1;
            if (IS_RIDING(i))
                should_show = 0;
            if (mode == SCMD_LOOK_BRIEF)
                if (i == ch->mount_data.mount)
                    should_show = 0;
            if (should_show) {
                if (IS_RIDDEN(i))
                    show_mount_to_char(i, ch, " riding on ", " riding on ", TRUE);
                else
                    show_char_to_char(i, ch, 0);
            }
        }
}

/*
 * Put messages describing the room affection `aff' in `str'; a
 * `mode' of 0 means that we're showing the affection to a
 * mortal - a `mode' of 1 means we're displaying an affection to
 * an immortal who has used `stat room'.
 */
void show_room_affection(char* str, struct affected_type* aff, int mode)
{
    int tmp;

    if (mode == 0) {
        switch (aff->type) {
        case ROOMAFF_SPELL:
            if ((aff->location >= 0) && (aff->location < MAX_SKILLS) && *room_spell_message[aff->location] && !(aff->bitvector & PERMAFFECT)) {
                strcat(str, room_spell_message[aff->location]);
                strcat(str, "\n\r");
            }

            if (!(aff->bitvector & PERMAFFECT)) {
                for (tmp = 0; tmp < 32; tmp++) {
                    if ((aff->bitvector & (1 << tmp)) && *room_bits_message[tmp]) {
                        strcat(str, room_bits_message[tmp]);
                        strcat(str, "\n\r");
                    }
                }
            }
            break;

        default:
            strcat(str, "Unknown room affection here; "
                        "please report to an immortal as quickly as possible.\n\r");
            break;
        }
    }
    if (mode == 1) { /* stat room */
        switch (aff->type) {
        case ROOMAFF_SPELL:
            *buf2 = 0;
            sprintbit(aff->bitvector, room_bits, buf2, 0);
            sprintf(str, "%s Spell %s(%d) level %d, %dhrs, sets %s.\r\n", str,
                ((aff->location >= 0) && (aff->location < MAX_SKILLS))
                    ? skills[aff->location].name
                    : "none",
                aff->location, aff->modifier, aff->duration, buf2);
            break;

        case ROOMAFF_EXIT:
            strcat(str, "exit affects are not yet defined.\r\n");
            break;

        default:
            sprintf(str, "Unknown room affect (%d).\n\r", aff->type);
            break;
        }
    }
}

/*
 * Put a message describing the weather in `ch's room in `str'
 */
void show_room_weather(char* str, struct char_data* ch)
{
    /* Is it snowy? */
    if (weather_info.snow[world[ch->in_room].sector_type])
        sprintf(str, "%sSnow lies upon the ground.\n\r", str);
}

/*
 * Format strings passed to sprintf(3); used to print an exit
 * symbol when using the look <no argument> command; i.e.:
 * Exits are: E (W) *S* #U# %D%
 */
char* exit_mark[] = {
    "", /* A hidden exit */
    " %c", /* A plain, boring link to another room */
    " (%c)", /* A closed, non-broken door */
    " *%c*", /* A closed, hidden exit, as seen by an immortal */
    " {%c}", /* A NOWALK exit such as a window, as seen by an immortal */
    " #%c#", /* A sunlit room, as seen by an Orc, Uruk, or Lhuth */
    " %%%c%%" /* A shadowy room during the day, as seen by darkies */
};

/*
 * A list of valid arguments to look; i.e.: look north,
 * look at <thing>, look (no argument), etc.
 */
char* keywords[] = { "north", "east", "south", "west", "up",
    "down", "in", "at", "", /* Look at '' case */
    "\n" };

/* subcmd == 1 serves for "examine" calls */
ACMD(do_look)
{
    char arg1[MAX_INPUT_LENGTH], arg2[MAX_INPUT_LENGTH];
    char str[2000];
    int keyword_no;
    int j, bits = 0, temp, tmp;
    char found;
    struct obj_data *tmp_object, *found_object;
    struct char_data* tmp_char;
    struct affected_type* tmpaf;
    char* tmp_desc;
    sh_int exit_choice;
    char exit_line[20];

    if (!ch->desc)
        return;
    if (!ch->desc->descriptor)
        return;

    /* Position/condition checking */
    if (GET_POS(ch) < POSITION_SLEEPING) {
        send_to_char("You can't see anything but stars!\n\r", ch);
        return;
    } else if (GET_POS(ch) == POSITION_SLEEPING) {
        send_to_char("You can't see anything, you're sleeping!\n\r", ch);
        return;
    } else if (IS_AFFECTED(ch, AFF_BLIND)) {
        send_to_char("You can't see a damned thing, you're blinded!\n\r", ch);
        return;
    } else if (!CAN_SEE(ch)) {
        send_to_char("It is pitch black...\n\r", ch);
        list_char_to_char(world[ch->in_room].people, ch, 0);
        return;
    }

    argument_split_2(argument, arg1, arg2);
    keyword_no = search_block(arg1, keywords, FALSE); /* Partial match */

    if ((keyword_no == -1) && *arg1) {
        keyword_no = 7;
        strcpy(arg2, arg1); /* Let arg2 become the target object (arg1) */
    }

    found = FALSE;
    tmp_object = NULL;
    tmp_char = NULL;
    tmp_desc = NULL;

    switch (keyword_no) {
    case 0:
    case 1:
    case 2:
    case 3:
    case 4:
    case 5: /* look <direction> */
        if (EXIT(ch, keyword_no)) { /* If there's a room in that direction */
            if ((!*(EXIT(ch, keyword_no)->general_description) || (EXIT(ch, keyword_no)->to_room != NOWHERE)) && !IS_SET(EXIT(ch, keyword_no)->exit_info, EX_CLOSED) && !IS_SET(EXIT(ch, keyword_no)->exit_info, EX_NO_LOOK) && (EXIT(ch, keyword_no)->to_room != NOWHERE)) {

                /*
                 * exam <direction> causes you to look in the room connected
                 * to that direction
                 */
                if (subcmd == SCMD_LOOK_EXAM) {
                    sprintf(str, "To the %s you see:\n\r", keywords[keyword_no]);
                    send_to_char(str, ch);
                    tmp = ch->in_room;
                    ch->in_room = EXIT(ch, keyword_no)->to_room;

                    /* Darkies can't see room contents or description if it's sunny */
                    if (SUN_PENALTY(ch)) {
                        sprintf(str, "%s\n\r", world[ch->in_room].name);
                        send_to_char(str, ch);
                        send_to_char("The power of light makes it hard to see.\n\r", ch);
                        ch->in_room = tmp;
                        return;
                    }
                    if (ch->in_room != NOWHERE)
                        do_look(ch, "", wtl, 15, 0);
                    else
                        send_to_char("You see nothing special.\n\r", ch);
                    ch->in_room = tmp;
                } else {
                    /* They typed look <dir>; look renders the exit's description */
                    if (*(EXIT(ch, keyword_no)->general_description))
                        sprintf(str, "%s", (EXIT(ch, keyword_no)->general_description));
                    else {
                        tmp = EXIT(ch, keyword_no)->to_room;
                        if (tmp == NOWHERE)
                            sprintf(str, "Protogenal chaos.\n\r");
                        else if (IS_DARK(tmp) && !PRF_FLAGGED(ch, PRF_HOLYLIGHT))
                            sprintf(str, "It's too dark to the %s to see anything.\n\r",
                                keywords[keyword_no]);
                        else
                            sprintf(str, "To the %s you see %s.\n\r", keywords[keyword_no],
                                world[EXIT(ch, keyword_no)->to_room].name);
                    }
                    send_to_char(str, ch);
                }
            } else { /* There's no room there.. maybe a door? */
                if (EXIT(ch, keyword_no)->general_description && *(EXIT(ch, keyword_no)->general_description))
                    send_to_char(EXIT(ch, keyword_no)->general_description, ch);
                else if (IS_SET(EXIT(ch, keyword_no)->exit_info, EX_CLOSED) && (IS_SET(EXIT(ch, keyword_no)->exit_info, EX_ISHIDDEN)))
                    send_to_char("You see something strange.\n\r", ch);
                else
                    send_to_char("You see nothing special.\n\r", ch);
            }

            /* Handle different states of doors */
            if (EXIT(ch, keyword_no)->to_room != NOWHERE) {
                if (IS_SET(EXIT(ch, keyword_no)->exit_info, EX_ISBROKEN) && (EXIT(ch, keyword_no)->keyword)) { /* Broken door */
                    sprintf(buf, "The %s is broken.\n\r", fname(EXIT(ch, keyword_no)->keyword));
                    send_to_char(buf, ch);
                } else if (IS_SET(EXIT(ch, keyword_no)->exit_info, EX_CLOSED) && (EXIT(ch, keyword_no)->keyword)) { /* Closed door */
                    if (!IS_SET(EXIT(ch, keyword_no)->exit_info, EX_ISHIDDEN)) {
                        sprintf(buf, "The %s is closed.\n\r", fname(EXIT(ch, keyword_no)->keyword));
                        send_to_char(buf, ch);
                    }
                } else if (IS_SET(EXIT(ch, keyword_no)->exit_info, EX_ISDOOR) && EXIT(ch, keyword_no)->keyword) { /* Open door */
                    sprintf(buf, "The %s is open.\n\r", fname(EXIT(ch, keyword_no)->keyword));
                    send_to_char(buf, ch);
                }
            }
        } else
            send_to_char("You see nothing special.\n\r", ch);
        break;

    case 6: /* look 'in' */
        if (*arg2) { /* look in <item carried> */
            bits = generic_find(arg2, FIND_OBJ_INV | FIND_OBJ_ROOM | FIND_OBJ_EQUIP, ch, &tmp_char,
                &tmp_object);

            if (bits) { /* Found something */
                if ((GET_ITEM_TYPE(tmp_object) == ITEM_DRINKCON) || (GET_ITEM_TYPE(tmp_object) == ITEM_FOUNTAIN)) {
                    /* Found a drink container */
                    if (tmp_object->obj_flags.value[1] <= 0)
                        act("It is empty.", FALSE, ch, 0, 0, TO_CHAR);
                    else { /* It's not empty, how full is it? */
                        if (tmp_object->obj_flags.value[0]) {
                            temp = (tmp_object->obj_flags.value[1] * 3) / tmp_object->obj_flags.value[0];
                            sprintf(buf, "It's %sfull of a %s liquid.\n\r", fullness[temp],
                                color_liquid[tmp_object->obj_flags.value[2]]);
                        } else
                            sprintf(buf, "It's max_content is zero, beware!\n\r");
                        send_to_char(buf, ch);
                    }
                } else if (GET_ITEM_TYPE(tmp_object) == ITEM_CONTAINER) {
                    /* Found a normal container; i.e.: backpack, pouch, etc */
                    if (!IS_SET(tmp_object->obj_flags.value[1], CONT_CLOSED)) {
                        /* The item isn't closed, so they can see into it */
                        send_to_char(fname(tmp_object->name), ch);
                        switch (bits) {
                        case FIND_OBJ_INV:
                            send_to_char(" (carried) : \n\r", ch);
                            break;
                        case FIND_OBJ_ROOM:
                            send_to_char(" (here) : \n\r", ch);
                            break;
                        case FIND_OBJ_EQUIP:
                            send_to_char(" (used) : \n\r", ch);
                            break;
                        }
                        list_obj_to_char(tmp_object->contains, ch, 2, true);
                    } else /* It was closed, pretty simple case */
                        send_to_char("It is closed.\n\r", ch);
                } else /* They looked in something that isn't a container */
                    send_to_char("That is not a container.\n\r", ch);
            } else /* They looked at something that isn't here */
                send_to_char("You do not see that item here.\n\r", ch);
        } else /* They didn't look "in" anything! */
            send_to_char("Look in what?!\n\r", ch);
        break;

    case 7: /* look 'at' */
        if (*arg2) { /* If they had an argument.. */
            /* Check to see if they looked at a race */
            tmp = search_block(arg2, pc_race_keywords, TRUE);

            /* Nothing found, maybe they're looking at someone in the room? */
            if (tmp != -1) {
                tmp_char = world[ch->in_room].people;

                /* Search the room manually if search_block failed */
                while (tmp_char)
                    if ((tmp_char->player.race == tmp) && (tmp_char != ch) && (IS_NPC(tmp_char) || other_side(ch, tmp_char)) && CAN_SEE(ch, tmp_char))
                        break;
                    else
                        tmp_char = tmp_char->next_in_room;

                if (!tmp_char) /* No one was found */
                    tmp = -1;
                else { /* We got em */
                    bits = 0;
                    found = TRUE;
                }
            }

            if (tmp == -1) /* Still haven't found anyone */
                bits = generic_find(arg2,
                    FIND_OBJ_INV | FIND_OBJ_ROOM | FIND_OBJ_EQUIP | FIND_CHAR_ROOM,
                    ch, &tmp_char, &found_object);
            /*
             * The next line was the cause of look spirit crashes.
             *
             * if(tmp_char && CAN_SEE(ch, tmp_char)) {
             *
             * Why does this line fail? Because tmp_char is produced by
             * generic_find if and only if the ch can see tmp_char.
             * That is, the can_see has to succeed otherwise tmp_char
             * will not be assigned.  The look spirit crash happened if
             * generic_find succeeds its can_see and generates tmp_char,
             * but then this line can_see fails and mud assumes there is
             * no tmp_char or tmp_objs; the result was referencing an
             * undefined pointer.
             */

            if (tmp_char) {
                show_char_to_char(tmp_char, ch, 1);

                if (ch != tmp_char) {
                    if (CAN_SEE(tmp_char, ch))
                        act("$n looks at you.", TRUE, ch, 0, tmp_char, TO_VICT);
                    act("$n looks at $N.", TRUE, ch, 0, tmp_char, TO_NOTVICT);
                }
                return;
            }

            /* Still nothing; maybe an extra description in the room? */
            if (!found) {
                tmp_desc = find_ex_description(arg2, world[ch->in_room].ex_description);
                if (tmp_desc) {
                    page_string(ch->desc, tmp_desc, 0);
                    return; /* RETURN SINCE IT WAS A ROOM DESCRIPTION */
                    /* Old system was: found = TRUE; */
                }
            }

            /* Nothing still.. perhaps it's an extra description on an object? */
            if (!found) /* Check equipment used */
                for (j = 0; j < MAX_WEAR && !found; j++)
                    if (ch->equipment[j])
                        if (CAN_SEE_OBJ(ch, ch->equipment[j])) {
                            tmp_desc = find_ex_description(arg2, ch->equipment[j]->ex_description);
                            if (tmp_desc) {
                                page_string(ch->desc, tmp_desc, 1);
                                found = TRUE;
                            }
                        }

            /* Is it maybe something in your inventory? */
            if (!found)
                for (tmp_object = ch->carrying; tmp_object && !found;
                     tmp_object = tmp_object->next_content)
                    if (CAN_SEE_OBJ(ch, tmp_object)) {
                        tmp_desc = find_ex_description(arg2, tmp_object->ex_description);
                        if (tmp_desc) {
                            page_string(ch->desc, tmp_desc, 1);
                            found = TRUE;
                        }
                    }

            /* Ok.. how about an object lying around in the room? */
            if (!found)
                for (tmp_object = world[ch->in_room].contents; tmp_object && !found;
                     tmp_object = tmp_object->next_content)
                    if (CAN_SEE_OBJ(ch, tmp_object)) {
                        tmp_desc = find_ex_description(arg2, tmp_object->ex_description);
                        if (tmp_desc) {
                            page_string(ch->desc, tmp_desc, 1);
                            found = TRUE;
                        }
                    }

            if (bits) { /* If an object was found */
                if (!found)
                    show_obj_to_char(found_object, ch, 5); /* Show no-description */
                else
                    show_obj_to_char(found_object, ch, 6); /* Find hum, glow etc */
            } else if (!found) /* Well, nothing was ever found */
                send_to_char("You do not see that here.\n\r", ch);
        } else /* They didn't give us any argument */
            send_to_char("Look at what?\n\r", ch);
        break;

    case 8: /* look '' */
        strcpy(buf2, CC_USE(ch, COLOR_ROOM));
        strcat(buf2, world[ch->in_room].name);
        if (PRF_FLAGGED(ch, PRF_ROOMFLAGS)) {
            if (world[ch->in_room].room_flags == BFS_MARK)
                strcpy(buf, "NOFLAGS");
            else
                sprintbit((long)world[ch->in_room].room_flags, room_bits, buf, 0);
            sprintf(buf2, "%s (#%d) [ %s, %s]", buf2, world[ch->in_room].number,
                sector_types[world[ch->in_room].sector_type], buf);
        } else if (PRF_FLAGGED(ch, PRF_ADVANCED_VIEW)) {
            if (IS_SET(world[ch->in_room].room_flags, HIDE_VNUM)) {
                sprintf(buf2, "%s (???) [ %s ]", buf2,
                    sector_types[world[ch->in_room].sector_type]);
            } else {
                sprintf(buf2, "%s (#%d) [ %s ]", buf2, world[ch->in_room].number,
                    sector_types[world[ch->in_room].sector_type]);
            }
        }

        strcat(buf2, CC_NORM(ch));
        /* Send them the exits */
        int i;
        strcat(buf2, "    Exits are:");

        for (i = 0; i < NUM_OF_DIRS; i++) {
            /* exit_choice 1 means nothing special */
            exit_choice = 1;

            if (world[ch->in_room].dir_option[i])
                if (world[ch->in_room].dir_option[i]->to_room != NOWHERE) {
                    /* Are there any closed and non-broken doors? */
                    if (IS_SET(world[ch->in_room].dir_option[i]->exit_info, EX_CLOSED) && !IS_SET(world[ch->in_room].dir_option[i]->exit_info, EX_ISBROKEN)) {
                        exit_choice = 2; /* Denotes a normal, closed door */

                        if (IS_SET(world[ch->in_room].dir_option[i]->exit_info, EX_ISHIDDEN)) {
                            if (ch->player.level < LEVEL_GOD)
                                exit_choice = 0; /* An Immortal sees hidden doors */
                            else
                                exit_choice = 3; /* Denotes a hidden and closed door */
                        }
                    }
                    /*
                     * exit_choice 4 means that you cannot walk into this
                     * exit.  This is used for windows, mainly.
                     */
                    else if (IS_SET(world[ch->in_room].dir_option[i]->exit_info, EX_NOWALK)) {
                        if (ch->player.level >= LEVEL_GOD)
                            exit_choice = 4;
                        else
                            exit_choice = 0;
                    }

                    /*
                     * exit_choice 5 means a darkie is looking at an exit which
                     * leads to a sunlit room
                     */
                    if (((GET_RACE(ch) == RACE_URUK) || (GET_RACE(ch) == RACE_ORC) || (GET_RACE(ch) == RACE_MAGUS) || (GET_RACE(ch) == RACE_OLOGHAI)) && IS_SUNLIT_EXIT(ch->in_room, world[ch->in_room].dir_option[i]->to_room, i))
                        if (exit_choice != 4)
                            exit_choice = 5;

                    /*
                     * exit_choice 6 means a darkie is looking at an exit which
                     * leads to a shadowy room, AND the sun is shining in that
                     * room.
                     */
                    if (((GET_RACE(ch) == RACE_URUK) || (GET_RACE(ch) == RACE_ORC) || (GET_RACE(ch) == RACE_MAGUS) || (GET_RACE(ch) == RACE_OLOGHAI)) && IS_SHADOWY_EXIT(ch->in_room, world[ch->in_room].dir_option[i]->to_room, i) && weather_info.sunlight == SUN_LIGHT)
                        if (exit_choice != 4)
                            exit_choice = 6;

                    /*
                     * Generate the direction letter and any surrounding symbols
                     * based on the information we've gathered with exit_choice
                     */
                    switch (i) {
                    case 0:
                        sprintf(exit_line, exit_mark[exit_choice], 'N');
                        break;
                    case 1:
                        sprintf(exit_line, exit_mark[exit_choice], 'E');
                        break;
                    case 2:
                        sprintf(exit_line, exit_mark[exit_choice], 'S');
                        break;
                    case 3:
                        sprintf(exit_line, exit_mark[exit_choice], 'W');
                        break;
                    case 4:
                        sprintf(exit_line, exit_mark[exit_choice], 'U');
                        break;
                    case 5:
                        sprintf(exit_line, exit_mark[exit_choice], 'D');
                        break;
                    };

                    strcat(buf2, exit_line);
                }
        }
        strcat(buf2, "\n\r");

        /* Now generate the room description */
        if (PRF_FLAGGED(ch, PRF_SPAM)) {
            if (!PRF_FLAGGED(ch, PRF_BRIEF) || (cmd == CMD_LOOK)) {
                strcat(buf2, CC_USE(ch, COLOR_DESC));
                strcat(buf2, world[ch->in_room].description);
            }
        }
        strcat(buf2, CC_NORM(ch));

        /* Any affections in the room */
        for (tmpaf = world[ch->in_room].affected; tmpaf; tmpaf = tmpaf->next)
            show_room_affection(buf2, tmpaf, 0);

        show_room_weather(buf2, ch); /* A weather-related string */
        send_to_char(buf2, ch);

        /* Now list the objects in the room */
        send_to_char(CC_USE(ch, COLOR_OBJ), ch);
        list_obj_to_char(world[ch->in_room].contents, ch, 0, false);
        send_to_char(CC_NORM(ch), ch);

        /* Now list the people in the room */
        list_char_to_char(world[ch->in_room].people, ch, subcmd);

        show_blood_trail(ch, 0, 1);

        /* ORC DELAY - URUK DELAY - a nasty hack, but what else can i do.. */
        if (SUN_PENALTY(ch) && !IS_AFFECTED(ch, AFF_WAITING) && !(IS_AFFECTED((ch), AFF_BLIND)))
            do_orc_delay(ch, "", 0, 0, 0);

        /*
         * If you're hunting, have no sun penalty, and aren't confused,
         * we send you the tracks in this room.
         */
        else if (!IS_NPC(ch) && !SUN_PENALTY(ch) && IS_AFFECTED(ch, AFF_HUNT) && !IS_AFFECTED(ch, AFF_CONFUSE)) {
            show_tracks(ch, 0, 2);
            WAIT_STATE(ch, 4);
        }
        break;

    case -1: /* wrong arg */
        send_to_char("Sorry, I didn't understand that!\n\r", ch);
        break;
    }
}

/*
 * This is apparently supposed to be changed.  The reasoning
 * has been lost in time.
 */
ACMD(do_read)
{
    if (GET_POS(ch) < POSITION_RESTING) {
        send_to_char("In your dreams, or what?\n\r", ch);
        return;
    }

    sprintf(buf1, "at %s", argument);
    do_look(ch, buf1, wtl, 15, 0);
}

/*
 * This basically just correctly reroutes an examine to a
 * do_look.
 */
ACMD(do_examine)
{
    char name[100], buf[100];
    int bits;
    struct char_data* tmp_char;
    struct obj_data* tmp_object;

    one_argument(argument, name);
    if (*name) {
        do_look(ch, argument, wtl, 15, 1);
        one_argument(argument, name);
    } else {
        if (!PRF_FLAGGED(ch, PRF_SPAM)) {
            TOGGLE_BIT(PRF_FLAGS(ch), PRF_SPAM);
            do_look(ch, argument, wtl, CMD_LOOK, SCMD_LOOK_EXAM);
            TOGGLE_BIT(PRF_FLAGS(ch), PRF_SPAM);
        } else
            do_look(ch, argument, wtl, CMD_LOOK, SCMD_LOOK_EXAM);
        return;
    }

    bits = generic_find(name, FIND_OBJ_INV | FIND_OBJ_ROOM | FIND_OBJ_EQUIP, ch, &tmp_char,
        &tmp_object);

    if (tmp_object) {
        if ((GET_ITEM_TYPE(tmp_object) == ITEM_DRINKCON) || (GET_ITEM_TYPE(tmp_object) == ITEM_FOUNTAIN) || (GET_ITEM_TYPE(tmp_object) == ITEM_CONTAINER)) {
            send_to_char("When you look inside, you see:\n\r", ch);
            sprintf(buf, "in %s", argument);
            do_look(ch, buf, wtl, 15, 0);
        }
    }
}

ACMD(do_exits)
{
    int door, tmp;
    char* exits[] = { "North", "East ", "South", "West ", "Up   ", "Down " };
    char* sun_exits[] = { "#North#", "#East# ", "#South#", "#West# ", "#Up#   ", "#Down# " };

    *buf = '\0';

    for (door = 0; door <= 5; door++)
        if (EXIT(ch, door))
            if (EXIT(ch, door)->to_room != NOWHERE) {
                tmp = IS_SET(EXIT(ch, door)->exit_info, EX_NOWALK) && IS_SET(EXIT(ch, door)->exit_info, EX_ISHIDDEN);

                if (!tmp && (!IS_SET(EXIT(ch, door)->exit_info, EX_CLOSED) || IS_SET(EXIT(ch, door)->exit_info, EX_ISBROKEN))) {
                    if (GET_LEVEL(ch) >= LEVEL_IMMORT) {
                        sprintf(buf + strlen(buf), "%-7s - [%7d][w:%2d] %s\n\r", exits[door],
                            world[EXIT(ch, door)->to_room].number, EXIT(ch, door)->exit_width,
                            world[EXIT(ch, door)->to_room].name);
                    } else {
                        tmp = ch->in_room;
                        ch->in_room = EXIT(ch, door)->to_room;
                        if (!CAN_SEE(ch) && !PRF_FLAGGED(ch, PRF_HOLYLIGHT)) {
                            sprintf(buf + strlen(buf), "%-7s - Too dark to tell\n\r",
                                (((GET_RACE(ch) == RACE_URUK) || (GET_RACE(ch) == RACE_ORC)) && IS_SUNLIT_EXIT(tmp, ch->in_room, door))
                                    ? sun_exits[door]
                                    : exits[door]);
                        } else {
                            sprintf(buf + strlen(buf), "%-7s - %s\n\r",
                                (((GET_RACE(ch) == RACE_URUK) || (GET_RACE(ch) == RACE_ORC)) && IS_SUNLIT_EXIT(tmp, ch->in_room, door))
                                    ? sun_exits[door]
                                    : exits[door],
                                world[ch->in_room].name);
                        }
                        ch->in_room = tmp;
                    }
                } else {
                    if (!IS_SET(EXIT(ch, door)->exit_info, EX_ISHIDDEN)) {
                        sprintf(buf + strlen(buf), "%-7s - Closed %s\n\r",
                            (((GET_RACE(ch) == RACE_URUK) || (GET_RACE(ch) == RACE_ORC)) && IS_SUNLIT_EXIT(ch->in_room, ch->in_room, door))
                                ? sun_exits[door]
                                : exits[door],
                            EXIT(ch, door)->keyword);
                    } else if (ch->player.level >= LEVEL_GOD)
                        sprintf(buf + strlen(buf), "%-7s - *Hidden* %s\n\r",
                            (((GET_RACE(ch) == RACE_URUK) || (GET_RACE(ch) == RACE_ORC)) && IS_SUNLIT_EXIT(ch->in_room, ch->in_room, door))
                                ? sun_exits[door]
                                : exits[door],
                            EXIT(ch, door)->keyword);
                }
            }
    send_to_char("Obvious exits:\n\r", ch);

    if (*buf)
        send_to_char(buf, ch);
    else
        send_to_char(" None.\n\r", ch);
}

int get_percent_absorb(char_data* character)
{
    int absorb, hit_location, tmp, dam; /* up to max. of 10000 */

    absorb = 0;
    for (hit_location = 0; hit_location < MAX_BODYPARTS; hit_location++) {
        tmp = bodyparts[GET_BODYTYPE(character)].armor_location[hit_location];
        dam = 10; /* armour absorb % on 10 HP damage */
        if (tmp) {
            if (character->equipment[tmp]) {
                obj_data* armor = character->equipment[tmp];
                dam -= armor->obj_flags.value[1];
                dam -= (dam * armor_absorb(armor) + 50) / 100;
            }
        }
        absorb += (10 - dam) * bodyparts[GET_BODYTYPE(character)].percent[hit_location];
    }

    // Characters specialized in heavy fighting absorb 10% more damage.
    if (utils::get_specialization(*character) == game_types::PS_HeavyFighting) {
        absorb += absorb / 10;
    }

    return absorb / 10;
}

/*
 * Write a message into `bf' that describes the severity of
 * `real_move'.
 */
void add_move_report(int real_move, char* bf)
{
    int room_move_cost(struct char_data*, struct room_data*);

    if (real_move < 1)
        strcat(bf, "You barely notice the way as you move.\n\r");
    else if (real_move < 3)
        strcat(bf, "You move easily indeed.\n\r");
    else if (real_move < 5)
        strcat(bf, "You have no problems moving.\n\r");
    else if (real_move < 8)
        strcat(bf, "You move with some difficulty.\n\r");
    else if (real_move < 12)
        strcat(bf, "Moving is hard for you.\n\r");
    else if (real_move < 17)
        strcat(bf, "Moving is a real pain.\n\r");
    else
        strcat(bf, "It hurts even to think about moving.\n\r");
}

ACMD(do_info)
{
    int tmp;
    char* bufpt;
    struct time_info_data playing_time;
    int room_move_cost(struct char_data*, struct room_data*);
    struct time_info_data real_time_passed(time_t, time_t);
    extern const char* specialize_name[];

    bufpt = buf;

    /* `ch's name, title, alignment, sex and race */
    bufpt += sprintf(bufpt, "You are %s %s, %s (%d) %s %s.\n\r", GET_NAME(ch), GET_TITLE(ch),
        IS_GOOD(ch)       ? "a good"
            : IS_EVIL(ch) ? "an evil"
                          : "a neutral",
        GET_ALIGNMENT(ch), GET_SEX(ch) ? GET_SEX(ch) == 1 ? "male" : "female" : "neutral",
        pc_races[GET_RACE(ch)]);

    /* `ch's level */
    bufpt += sprintf(bufpt, "You have reached level %d.\n\r", GET_LEVEL(ch));

    /* `ch's class proficiencies */
    bufpt += sprintf(bufpt,
        "You are level %d Warrior, %d Ranger, "
        "%d Mystic, and %d Mage.\n\r",
        GET_PROF_LEVEL(PROF_WARRIOR, ch), GET_PROF_LEVEL(PROF_RANGER, ch),
        GET_PROF_LEVEL(PROF_CLERIC, ch), GET_PROF_LEVEL(PROF_MAGE, ch));

    /* `ch's specialization */
    game_types::player_specs spec = utils::get_specialization(*ch);
    if (spec == game_types::PS_None || spec == game_types::PS_Count) {
        bufpt += sprintf(bufpt, "You are not specialized in anything.\n\r");
    } else {
        bufpt += sprintf(bufpt, "You are specialized in %s.\n\r", specialize_name[spec]);
    }

    /* `ch's age */
    playing_time = real_time_passed((time(0) - ch->player.time.logon) + ch->player.time.played, 0);
    bufpt += sprintf(bufpt,
        "You are %d years old, and have played "
        "%d days and %d hours.\n\r",
        GET_AGE(ch), playing_time.day, playing_time.hours);

    /* Is it `ch's birthday today? */
    if (!age(ch).month && !age(ch).day)
        bufpt += sprintf(bufpt, "It's your birthday today!\r\n");

    /* `ch's weight and height */
    bufpt += sprintf(bufpt,
        "You are %d'%d\" high, weight %.1flb and "
        "carrying %.1flb.\r\n",
        GET_HEIGHT(ch) / 30, (GET_HEIGHT(ch) % 30) * 10 / 25, GET_WEIGHT(ch) / 100.,
        IS_CARRYING_W(ch) / 100.);

    /* `ch's hitpoints, stamina, moves and spirit */
    bufpt += sprintf(bufpt,
        "You have %d/%d hit points, %d/%d stamina, "
        "%d/%d moves and %d spirit.\n\r",
        GET_HIT(ch), GET_MAX_HIT(ch), GET_MANA(ch), GET_MAX_MANA(ch), GET_MOVE(ch),
        GET_MAX_MOVE(ch), utils::get_spirits(ch));

    /* ch's resource regeneration */
    {
        float health_regen = hit_gain(ch);
        float bonus_health_regen = get_bonus_hit_gain(ch);
        const char* health_symbol = bonus_health_regen > 0.0f ? "+" : "";

        float mana_regen = mana_gain(ch);
        float bonus_mana_regen = get_bonus_mana_gain(ch);
        const char* mana_symbol = bonus_mana_regen > 0.0f ? "+" : "";

        float move_regen = move_gain(ch);
        float bonus_move_regen = get_bonus_move_gain(ch);
        const char* move_symbol = bonus_move_regen > 0.0f ? "+" : "";

        bufpt += sprintf(bufpt,
            "You regain %.0f (%s%.0f) health, %.0f (%s%.0f) stamina, and %.0f "
            "(%s%.0f) moves per hour.\n\r",
            health_regen, health_symbol, bonus_health_regen, mana_regen, mana_symbol,
            bonus_mana_regen, move_regen, move_symbol, bonus_move_regen);
    }

    /* `ch's wealth */
    bufpt += sprintf(bufpt, "You have %s.\n\r", money_message(GET_GOLD(ch), 1));

    /* `ch's OB, DB, PB, and attack speed */
    bufpt += sprintf(bufpt,
        "Your OB is %d, dodge is %d, parry %d, "
        "and your attack speed is %d.\r\n"
        "Your armour absorbs about %d%% damage, and ",
        get_real_OB(ch), get_real_dodge(ch), get_real_parry(ch),
        utils::get_energy_regen(*ch) / 5, get_percent_absorb(ch));

    /* A small blurb on `ch's spellsave; should be its own function */
    if (ch->specials2.saving_throw < 0)
        bufpt += sprintf(bufpt, "leaves you helpless against magical "
                                "attacks.\r\n");
    else if (!ch->specials2.saving_throw)
        bufpt += sprintf(bufpt, "makes you extremely sensitive to magic.\r\n");
    else if (ch->specials2.saving_throw > 0 && ch->specials2.saving_throw < 5)
        bufpt += sprintf(bufpt, "gives you a meagre resilience to magic.\r\n");
    else if (ch->specials2.saving_throw > 4 && ch->specials2.saving_throw < 12)
        bufpt += sprintf(bufpt, "callouses you to the effects of magic.\r\n");
    else
        bufpt += sprintf(bufpt, "renders you numb to magical onslaught.\r\n");

    /* `ch's mystic abilities (perception and willpower) */
    bufpt += sprintf(bufpt,
        "Your spiritual perception is %d%%, "
        "willpower: %d,\r\n",
        GET_PERCEPTION(ch), GET_WILLPOWER(ch));

    player_spec::battle_mage_handler battle_mage_handler(ch);
    bufpt += sprintf(bufpt,
        "Your spell penetration is %d, "
        "and your spell power is %d,\n\r",
        battle_mage_handler.get_bonus_spell_pen(ch->points.get_spell_pen()),
        battle_mage_handler.get_bonus_spell_power(ch->points.get_spell_power()));

    /* `ch's skill encumbrance and leg encumbrance */
    bufpt += sprintf(bufpt,
        "Your skill encumbrance is %d, and your "
        "movement is encumbered by %d.\n\r",
        utils::get_encumbrance(*ch), utils::get_leg_encumbrance(*ch));

    /* How much effort `ch' has to put into moving in this room */
    tmp = room_move_cost(ch, &world[ch->in_room]);
    add_move_report(tmp, bufpt);
    bufpt += strlen(bufpt);

    /* `ch's total experience and TNL */
    if (GET_LEVEL(ch) < LEVEL_IMMORT - 1)
        bufpt += sprintf(bufpt,
            "You have scored %d experience points, and "
            "need %d more to advance.\n\r",
            GET_EXP(ch), xp_to_level(GET_LEVEL(ch) + 1) - GET_EXP(ch));
    else
        bufpt += sprintf(bufpt, "You have scored %d experience points.\r\n", GET_EXP(ch));

    /* `ch's stats */
    if (GET_LEVEL(ch) > 5)
        bufpt += sprintf(bufpt,
            "Strength: %d/%d, Intelligence: %d/%d, "
            "Will: %d/%d, Dexterity: %d/%d\r\n             "
            "Constitution: %d/%d, Learning Ability: %d/%d.\r\n",
            GET_STR(ch), GET_STR_BASE(ch), GET_INT(ch), GET_INT_BASE(ch), GET_WILL(ch),
            GET_WILL_BASE(ch), GET_DEX(ch), GET_DEX_BASE(ch), GET_CON(ch),
            GET_CON_BASE(ch), GET_LEA(ch), GET_LEA_BASE(ch));

    /* `ch's position */
    switch (GET_POS(ch)) {
    case POSITION_DEAD:
        bufpt += sprintf(bufpt, "You are DEAD!\r\n");
        break;

    case POSITION_INCAP:
        bufpt += sprintf(bufpt, "You are incapacitated, slowly fading away..\r\n");
        break;

    case POSITION_STUNNED:
        bufpt += sprintf(bufpt, "You are stunned!  You can't move!\r\n");
        break;

    case POSITION_SLEEPING:
        bufpt += sprintf(bufpt, "You are sleeping.\r\n");
        break;

    case POSITION_RESTING:
        bufpt += sprintf(bufpt, "You are resting.\n\r");
        break;

    case POSITION_SITTING:
        bufpt += sprintf(bufpt, "You are sitting.\r\n");
        break;

    case POSITION_FIGHTING:
        if (ch->specials.fighting)
            bufpt += sprintf(bufpt, "You are fighting %s.\r\n",
                PERS(ch->specials.fighting, ch, FALSE, FALSE));
        else
            bufpt += sprintf(bufpt, "You are fighting thin air.\r\n");
        break;

    case POSITION_STANDING:
        bufpt += sprintf(bufpt, "You are standing.\r\n");
        break;

    default:
        bufpt += sprintf(bufpt, "You are floating.\r\n");
        break;
    }

    /* Special conditions */
    if (GET_COND(ch, DRUNK) > 10)
        bufpt += sprintf(bufpt, "You are intoxicated.\r\n");
    if (!GET_COND(ch, FULL))
        bufpt += sprintf(bufpt, "You are hungry.\r\n");
    if (!GET_COND(ch, THIRST))
        bufpt += sprintf(bufpt, "You are thirsty.\r\n");

    send_to_char(buf, ch);
    do_affections(ch, "", 0, 0, 0);
}

/*
 * This function, when given i < 170*4, returns 200*sqrt(i).
 * CH is only needed to send overflow to.
 */
int do_squareroot(int i)
{
    if (i / 4 > 170) {
        i = 170 * 4;
    }

    return (4 - i % 4) * square_root[i / 4] + (i % 4) * square_root[i / 4 + 1];
}

int test_hp(int war_points, int ran_points, int cler_points, int level, int con_score)
{
    int coof_points = 3 * war_points + 2 * ran_points + 1 * cler_points;
    double class_factor = std::sqrt(coof_points) * 200;
    class_factor = class_factor * (con_score + 20) / 14.0;
    class_factor = class_factor * std::min(LEVEL_MAX * 100, level * 100) / 100000.0;

    double const_factor = (11 + (level * 1.94)) * con_score / 20.0;
    int health = int(10 + std::min(level, LEVEL_MAX) + const_factor + class_factor);
    return health;
}

ACMD(do_score)
{
    int tmp;
    char* bufpt;

    bufpt = buf;
    bufpt += sprintf(bufpt,
        "You have %d/%d hit, %d/%d stamina, %d/%d moves, "
        "%d spirit.\r\n",
        GET_HIT(ch), GET_MAX_HIT(ch), GET_MANA(ch), GET_MAX_MANA(ch), GET_MOVE(ch),
        GET_MAX_MOVE(ch), utils::get_spirits(ch));

    bufpt += sprintf(bufpt, "OB: %d, DB: %d, PB: %d, Speed: %d, Gold: %d", get_real_OB(ch),
        get_real_dodge(ch), get_real_parry(ch), utils::get_energy_regen(*ch) / 5,
        GET_GOLD(ch) / COPP_IN_GOLD);

    /* No XP blurb for immortals */
    if (GET_LEVEL(ch) < LEVEL_IMMORT - 1) {
        tmp = xp_to_level(GET_LEVEL(ch) + 1) - GET_EXP(ch);
        if (tmp < 1000 && tmp > -1000) {
            bufpt += sprintf(bufpt, ", XP Needed: %d.\n\r", tmp);
        } else {
            bufpt += sprintf(bufpt, ", XP Needed: %dK.\n\r", tmp / 1000);
        }
    } else {
        bufpt += sprintf(bufpt, ".\r\n");
    }

    if (utils::get_specialization(*ch) == game_types::PS_LightFighting) {
        obj_data* weapon = ch->equipment[WIELD];
        if (weapon && (weapon->get_bulk() <= 2 || (weapon->get_bulk() == 3 && weapon->get_weight() <= LIGHT_WEAPON_WEIGHT_CUTOFF))) {
            bufpt += sprintf(bufpt, "The lightness of your weapon lends precision to your strikes.\r\n");
        }
    } else if (utils::get_specialization(*ch) == game_types::PS_HeavyFighting) {
        obj_data* weapon = ch->equipment[WIELD];
        if (weapon && (weapon->get_bulk() >= 4 || (weapon->get_bulk() == 3 && weapon->get_weight() > LIGHT_WEAPON_WEIGHT_CUTOFF))) {
            bufpt += sprintf(bufpt, "The heft of your weapon lends power to your blows.\r\n");
        }
    } else if (utils::get_specialization(*ch) == game_types::PS_WildFighting && ch->specials.tactics == TACTICS_BERSERK) {
        float health_percentage = ch->tmpabilities.hit / (float)ch->abilities.hit;
        if (health_percentage <= 0.45f) {
            bufpt += sprintf(bufpt, "Your fury lends speed to your attacks!\r\n");
        }
    }

    player_spec::weapon_master_handler weapon_master(ch);
    bufpt += weapon_master.append_score_message(bufpt);

    if (GET_COND(ch, DRUNK) > 10) {
        bufpt += sprintf(bufpt, "You are intoxicated.\n\r");
    }

    if (!GET_COND(ch, FULL)) {
        bufpt += sprintf(bufpt, "You are hungry.\r\n");
    } else if (GET_COND(ch, FULL) < 4 && GET_COND(ch, FULL) > 0) {
        bufpt += sprintf(bufpt, "You are getting hungry.\r\n");
    }

    if (!GET_COND(ch, THIRST)) {
        bufpt += sprintf(bufpt, "You are thirsty.\r\n");
    } else if (GET_COND(ch, THIRST) < 4 && GET_COND(ch, THIRST) > 0) {
        bufpt += sprintf(bufpt, "You are getting thirsty.\r\n");
    }

    send_to_char(buf, ch);
}

ACMD(do_time)
{
    char* bufpt;
    char* year;
    int weekday, sunrise, sunset, hours;
    extern int sun_events[12][2];
    extern char* weekdays[];
    extern struct time_info_data time_info;
    int get_season();

    bufpt = buf;
    bufpt += sprintf(bufpt, "It is about %d:00 %s on ",
        time_info.hours % 12 == 0 ? 12 : time_info.hours % 12,
        time_info.hours >= 12 ? "PM" : "AM");

    /* 35 days in a month */
    weekday = ((30 * time_info.month) + time_info.day + 1) % 7;
    bufpt += sprintf(bufpt, "%s, ", weekdays[weekday]);

    /* Get the daytime */
    day_to_str(&time_info, bufpt);
    bufpt += strlen(bufpt);
    bufpt += sprintf(bufpt, ".\r\n");

    year = nth(time_info.year);
    bufpt += sprintf(bufpt,
        "By the Steward's Reckoning, it is "
        "the %s year of the fourth age of Arda.\r\n",
        year);
    free(year);

    /* A blurb on the phase of the moon */
    bufpt += sprintf(bufpt, "The moon is %s and %s.\n\r", moon_phase[weather_info.moonphase],
        weather_info.moonlight ? "shining" : "not shining");

    /* When the sun will rise and set */
    sunrise = sun_events[time_info.month][0];
    sunset = sun_events[time_info.month][1];
    if (time_info.hours >= sunrise && time_info.hours < sunset) {
        hours = sunset - time_info.hours;
        bufpt += sprintf(bufpt, "The sun will set in about %d hour%s.\r\n", hours,
            hours == 1 ? "" : "s");
    } else {
        hours = sunrise + (time_info.hours < 12 ? -time_info.hours : 24 - time_info.hours);
        bufpt += sprintf(bufpt, "The sun will rise in about %d hour%s.\n\r", hours,
            hours == 1 ? "" : "s");
    }
    send_to_char(buf, ch);
}

char* sky_look[6] = {
    "cloudless", "cloudy", "rainy", "lit by flashes of lightning", "snowy", "full of driving snow"
};

ACMD(do_weather)
{
    int get_season();
    void weather_to_char(char_data * ch);

    if (ch->in_room == NOWHERE)
        return;

    weather_to_char(ch);

    switch (get_season()) {
    case SEASON_SPRING:
        send_to_char("It is spring and the land is bursting with new life.\n\r", ch);
        break;

    case SEASON_SUMMER:
        send_to_char("It is summer and the land basks in the long drawn-out days."
                     "\n\r",
            ch);
        break;

    case SEASON_AUTUMN:
        send_to_char("It is autumn: the season for blustery storms.\n\r", ch);
        break;

    case SEASON_WINTER:
        send_to_char("It is winter: the land shivers awaiting the onset of spring."
                     "\n\r",
            ch);
        break;

    default:
        send_to_char("Error: unknown season.  Please alert an immortal.\n\r", ch);
    }
}

/*
 * If subcmd = 0, then we've been called with the literal
 * "help" command; if subcmd = 1, we've been called by "man",
 * and help is divided into chapters.
 */
ACMD(do_help)
{
    int chk, bot, top, mid, minlen, tmp, num, buf2tmp;
    char chapstr[255], *buf2pt;
    extern int help_summary_length;
    extern char* help;
    extern struct help_index_summary help_content[];

    if (!ch->desc)
        return;

    for (; isspace(*argument); argument++)
        ;

    buf2pt = buf2;
    /* Find the index for the chapter that the character requested */
    if (subcmd == 1) { /* man (manual) command */
        if (*argument) {
            for (tmp = 0; tmp < 80 && *argument && *argument > ' '; tmp++)
                chapstr[tmp] = *(argument++);
            chapstr[tmp] = 0;

            for (; isspace(*argument); argument++)
                ;

            for (tmp = 0; tmp < help_summary_length; tmp++)
                if (!strncmp(chapstr, help_content[tmp].keyword, strlen(chapstr)) && ((help_content[tmp].imm_only && GET_LEVEL(ch) >= LEVEL_IMMORT) || !help_content[tmp].imm_only))
                    break;
        } else /* no argument */
            tmp = help_summary_length;

        /* no argument, or no matching chapter */
        if (tmp == help_summary_length) {
            buf2pt += sprintf(buf2pt, "The manual chapters are:\r\n");
            for (tmp = 0; tmp < help_summary_length; tmp++)
                if ((help_content[tmp].imm_only && GET_LEVEL(ch) >= LEVEL_IMMORT) || !help_content[tmp].imm_only) {
                    buf2tmp = sprintf(buf2pt, "%-18s %-50s\r\n", help_content[tmp].keyword,
                        help_content[tmp].descr);
                    CAP(buf2pt);
                    buf2pt += buf2tmp;
                }
            send_to_char(buf2, ch);
            return;
        }
        num = tmp;
    } else /* The help command always uses help_content[0] (lib/text/help_tbl) */
        num = 0;

    /*
     * Now we're dealing with both the help command AND the manual
     * command.  `argument' either points to the first argument in
     * the help command, or the second and following arguments in
     * the manual: `help <argument>' or `man <chapter> <argument'.
     */
    if (*argument) {
        if (!help_content[num].keyword || !help_content[num].file) {
            send_to_char("No such help available.\n\r", ch);
            return;
        }

        /* Do a binary search through the chapters to find a match */
        bot = 0;
        top = help_content[num].top_of_helpt;
        minlen = strlen(argument);
        for (;;) {
            mid = (bot + top) / 2;

            if (!(chk = strn_cmp(argument, help_content[num].index[mid].keyword, minlen))) {
                fseek(help_content[num].file, help_content[num].index[mid].pos, SEEK_SET);
                *buf2 = '\0';
                for (;;) {
                    fgets(buf, 80, help_content[num].file);
                    if (*buf == '#')
                        break;
                    buf2pt += sprintf(buf2pt, "%s", buf);
                }
                page_string(ch->desc, buf2, 1);
                return;
            } else if (bot >= top) {
                sprintf(buf2, "There is no entry for '%s' in the %s chapter.\r\n", argument,
                    help_content[num].keyword);
                send_to_char(buf2, ch);
                return;
            } else if (chk > 0)
                bot = ++mid;
            else
                top = --mid;
        }
        return;
    } else if (subcmd == 1) { /* They used manual with a chapter but no argument */
        buf2pt += sprintf(buf2pt, "Topics in the '%s' chapter are:\r\n", help_content[num].keyword);
        for (tmp = 0; tmp < help_content[num].top_of_helpt; tmp++) {
            buf2pt += sprintf(buf2pt, "%-17s| ", help_content[num].index[tmp].keyword);
            if (!((tmp + 1) % 4))
                buf2pt += sprintf(buf2pt, "\r\n");
        }
        if (tmp % 4)
            strcat(buf2, "\n\r");
        send_to_char(buf2, ch);
    } else /* They used help with no first argument */
        send_to_char(help, ch);

    return;
}

#define WHO_FORMAT "format: who [minlev[-maxlev]] [-n name] [-s] [-q] [-r] [-z] [-w] [-d] [-m]\n\r"

ACMD(do_who)
{
    struct descriptor_data* d;
    struct char_data* tch;
    char name_search[80];
    char mode;
    int low = 0, high = LEVEL_IMPL, i, localwho = 0;
    int short_list = 0, num_can_see = 0;
    int who_room = 0, level_limit = 0, who_whitie = 0, who_darkie = 0;
    int who_magi = 0;
    char buf2[16384], *buf2pt;
    extern char* imm_abbrevs[];

    *buf2 = *name_search = '\0';
    buf2pt = buf2;

    for (i = 0; *(argument + i) == ' '; i++)
        ;

    strcpy(buf, (argument + i));

    while (*buf) {
        half_chop(buf, arg, buf1);
        if (isdigit(*arg)) {
            sscanf(arg, "%d-%d", &low, &high);
            level_limit = 1;
            buf2pt += sprintf(buf2pt, "Players between level %d and %d\r\n", low, high);
            strcpy(buf, buf1);
        } else if (*arg == '-') {
            mode = *(arg + 1); /* just in case; we destroy arg in the switch */
            switch (mode) {
            case 'z':
                localwho = 1;
                strcpy(buf, buf1);
                buf2pt += sprintf(buf2pt, "Players in your zone\r\n");
                break;

            case 's':
                short_list = 1;
                strcpy(buf, buf1);
                break;

            case 'l':
                half_chop(buf1, arg, buf);
                sscanf(arg, "%d-%d", &low, &high);
                level_limit = 1;
                buf2pt += sprintf(buf2pt, "Players between level %d and %d\r\n", low, high);
                break;

            case 'n':
                half_chop(buf1, name_search, buf);
                buf2pt += sprintf(buf2pt,
                    "Players with '%s' in their names or titles"
                    "\r\n",
                    name_search);
                break;

            case 'r':
                who_room = 1;
                strcpy(buf, buf1);
                buf2pt += sprintf(buf2pt, "Players in your room\r\n");
                break;

            case 'w':
                who_whitie = 1;
                strcpy(buf, buf1);
                buf2pt += sprintf(buf2pt, "Humans, Elves, Dwarves, Beornings and Hobbits\r\n");
                break;

            case 'm':
                who_magi = 1;
                strcpy(buf, buf1);
                buf2pt += sprintf(buf2pt, "Uruk-Lhuth and Haradrims\r\n");
                break;

            case 'd':
                who_darkie = 1;
                strcpy(buf, buf1);
                buf2pt += sprintf(buf2pt, "Uruk-Hais, Olog-Hais and Common Orcs\r\n");
                break;

            default:
                send_to_char(WHO_FORMAT, ch);
                return;
                break;
            } /* end of switch */
        } else {
            send_to_char(WHO_FORMAT, ch);
            return;
        }
    } /* end while (parser) */

    if (!*buf2)
        buf2pt += sprintf(buf2pt, "Players\r\n");
    /* Make a dashline the same size as the header */
    memset(buf2pt, '-', strlen(buf2) - 2);
    buf2pt += buf2pt - buf2 - 2;
    *buf2pt = '\0';
    buf2pt += sprintf(buf2pt, "\r\n");

    /* Cycle through all connected sockets */
    for (d = descriptor_list; d; d = d->next) {
        if (d->connected && !(d->connected == CON_LINKLS))
            continue;

        if (d->original)
            tch = d->original;
        else if (!(tch = d->character))
            continue;

        /* don't show players on the opposite side of the race war */
        if (other_side(ch, tch))
            continue;
        /* they're searching names and titles for `name_search' */
        if (*name_search && str_cmp(GET_NAME(tch), name_search) && !strstr(GET_TITLE(tch), name_search))
            continue;
        /* ch isn't big enough to see through tch's invisibility */
        if ((GET_LEVEL(ch) < GET_INVIS_LEV(tch)) || GET_LEVEL(tch) < low || GET_LEVEL(tch) > high)
            continue;
        /* we're doing a who -z, and tch isn't in the zone */
        if (localwho && world[ch->in_room].zone != world[tch->in_room].zone)
            continue;
        /* we're doing a who -r, and tch isn't in the room */
        if (who_room && (tch->in_room != ch->in_room))
            continue;
        /*   who -w, and tch isn't a whitie */
        if (who_whitie && !(GET_RACE(tch) == RACE_WOOD || GET_RACE(tch) == RACE_DWARF || GET_RACE(tch) == RACE_HOBBIT || GET_RACE(tch) == RACE_HUMAN || GET_RACE(tch) == RACE_BEORNING))
            continue;
        /* who -d, and tch isn't a darkie */
        if (who_darkie && !(GET_RACE(tch) == RACE_URUK || GET_RACE(tch) == RACE_ORC || GET_RACE(tch) == RACE_OLOGHAI))
            continue;
        /* who -m, and tch isn't a lhuth */
        if (who_magi && !(GET_RACE(tch) == RACE_MAGUS || GET_RACE(tch) == RACE_HARADRIM))
            continue;
        /* if level_limit is non-zero, we don't show incognito players */
        if (level_limit && PLR_FLAGGED(tch, PLR_INCOGNITO))
            continue;

        /* The short list doesn't show a title, and attempts 4 players per row */
        if (short_list) {
            if (PLR_FLAGGED(tch, PLR_INCOGNITO) && (GET_LEVEL(ch) < LEVEL_IMMORT))
                buf2pt += sprintf(buf2pt, "[--- %s] %-12.12s%s", RACE_ABBR(tch), GET_NAME(tch),
                    !(++num_can_see % 4) ? "\r\n" : "");
            else
                buf2pt += sprintf(buf2pt, "[%3d %s] %-12.12s%s", GET_LEVEL(tch), RACE_ABBR(tch),
                    GET_NAME(tch), !(++num_can_see % 4) ? "\r\n" : "");
        } else { /* A normal list */
            num_can_see++;
            if (PLR_FLAGGED(tch, PLR_INCOGNITO) && (GET_LEVEL(ch) < LEVEL_IMMORT))
                buf2pt += sprintf(buf2pt, "[--- %s] ", RACE_ABBR(tch));
            else {
                if (GET_LEVEL(tch) < LEVEL_IMMORT)
                    buf2pt += sprintf(buf2pt, "[%3d %s] ", GET_LEVEL(tch), RACE_ABBR(tch));
                else
                    buf2pt += sprintf(buf2pt, "[%s] ", imm_abbrevs[GET_LEVEL(tch) - LEVEL_MINIMM]);
            }
            buf2pt += sprintf(buf2pt, "%s %s", GET_NAME(tch), GET_TITLE(tch));

            if (GET_INVIS_LEV(tch))
                buf2pt += sprintf(buf2pt, " (i%d)", GET_INVIS_LEV(tch));
            else if (IS_AFFECTED(tch, AFF_INVISIBLE))
                buf2pt += sprintf(buf2pt, " (invis)");
            if (PLR_FLAGGED(tch, PLR_MAILING))
                buf2pt += sprintf(buf2pt, " (mailing)");
            else if (PLR_FLAGGED(tch, PLR_WRITING))
                buf2pt += sprintf(buf2pt, " (writing)");
            if (d->connected == CON_LINKLS)
                buf2pt += sprintf(buf2pt, " (linkless)");
            if (PLR_FLAGGED(tch, PLR_RETIRED))
                buf2pt += sprintf(buf2pt, " (retired)");
            if (!PRF_FLAGGED(tch, PRF_NARRATE))
                buf2pt += sprintf(buf2pt, " (deaf)");
            if (PRF_FLAGGED(tch, PRF_NOTELL))
                buf2pt += sprintf(buf2pt, " (notell)");
            if (PLR_FLAGGED(tch, PLR_ISAFK))
                buf2pt += sprintf(buf2pt, " (AFK)");
            if (GET_POS(tch) == POSITION_SLEEPING)
                buf2pt += sprintf(buf2pt, " (sleeping)");
            if (IS_SHADOW(tch))
                buf2pt += sprintf(buf2pt, " (shadow)");
            buf2pt += sprintf(buf2pt, "\n\r");
        }
    }

    if (short_list && (num_can_see % 4))
        buf2pt += sprintf(buf2pt, "\n\r");
    buf2pt += sprintf(buf2pt, "\n\r%d character%s displayed.\n\r", num_can_see,
        num_can_see == 1 ? "" : "s");

    page_string(ch->desc, buf2, 1);
}

#define USERS_FORMAT \
    "format: users [-l minlevel[-maxlevel]] [-n name] [-h host] [-c proflist] [-o] [-p]\n\r"

ACMD(do_users)
{
    char line[200], idletime[10], profname[20];
    char state[100], *timeptr;
    char name_search[80], host_search[80];
    char mode, *format;
    int low = 0, high = LEVEL_IMPL;
    unsigned int i;
    int showprof = 0, num_can_see = 0, playing = 0, deadweight = 0;
    struct char_data* tch;
    struct descriptor_data* d;
    extern char* connected_types[];

    name_search[0] = '\0';
    host_search[0] = '\0';

    strcpy(buf, argument);
    while (*buf) {
        half_chop(buf, arg, buf1);
        if (*arg == '-') {
            mode = *(arg + 1); /* just in case; we destroy arg in the switch */
            switch (mode) {
            case 'p':
                playing = 1;
                strcpy(buf, buf1);
                break;

            case 'd':
                deadweight = 1;
                strcpy(buf, buf1);
                break;

            case 'l':
                playing = 1;
                half_chop(buf1, arg, buf);
                sscanf(arg, "%d-%d", &low, &high);
                break;

            case 'n':
                playing = 1;
                half_chop(buf1, name_search, buf);
                break;

            case 'h':
                playing = 1;
                half_chop(buf1, host_search, buf);
                break;

            case 'c':
                playing = 1;
                half_chop(buf1, arg, buf);
                for (i = 0; i < strlen(arg); i++) {
                    switch (tolower(arg[i])) {
                    case 'm':
                        showprof = showprof | 1;
                        break;
                    case 'c':
                        showprof = showprof | 2;
                        break;
                    case 't':
                        showprof = showprof | 4;
                        break;
                    case 'w':
                        showprof = showprof | 8;
                        break;
                    }
                }
                break;
            default:
                send_to_char(USERS_FORMAT, ch);
                return;
                break;
            }
        } else {
            send_to_char(USERS_FORMAT, ch);
            return;
        }
    }

    /* Header */
    strcpy(line, "Num   Prof       Name         State       Idl  Login@   Site\n\r");
    strcat(line, "--- --------- ------------ -------------- --- -------- "
                 "------------------------\n\r");
    send_to_char(line, ch);

    one_argument(argument, arg);

    for (d = descriptor_list; d; d = d->next) {
        if (d->connected && playing)
            continue;
        if (!d->connected && deadweight)
            continue;
        if (!d->connected || (d->connected == CON_LINKLS)) {
            if (d->original)
                tch = d->original;
            else if (!(tch = d->character))
                continue;

            if (*host_search && !strstr(d->host, host_search))
                continue;
            if (*name_search && str_cmp(GET_NAME(tch), name_search) && !strstr(GET_TITLE(tch), name_search))
                continue;
            if (!CAN_SEE(ch, tch) || GET_LEVEL(tch) < low || GET_LEVEL(tch) > high)
                continue;
            if (showprof && !(showprof & (1 << (GET_PROF(tch) - 1))))
                continue;
            if (GET_INVIS_LEV(ch) > GET_LEVEL(ch))
                continue;

            if (d->original)
                sprintf(profname, "[%3d %s]", GET_LEVEL(d->original), RACE_ABBR(d->original));
            else
                sprintf(profname, "[%3d %s]", GET_LEVEL(d->character), RACE_ABBR(d->character));
        } else
            strcpy(profname, "[   -   ]");

        timeptr = asctime(localtime(&d->login_time));
        timeptr += 11;
        *(timeptr + 8) = '\0';

        if (!d->connected && d->original)
            strcpy(state, "Switched");
        else
            strcpy(state, connected_types[d->connected]);

        if (d->character && (!d->connected || (d->connected == CON_LINKLS)))
            sprintf(idletime, "%3d",
                d->character->specials.timer * SECS_PER_MUD_HOUR / SECS_PER_REAL_MIN);
        else
            strcpy(idletime, " - ");

        format = "%3d %-9s %-12s %-14s %-3s %-8s ";

        if (d->character && d->character->player.name) {
            if (d->original)
                sprintf(line, format, d->desc_num, profname, d->original->player.name, state,
                    idletime, timeptr);
            else
                sprintf(line, format, d->desc_num, profname, d->character->player.name, state,
                    idletime, timeptr);
        } else
            sprintf(line, format, d->desc_num, "   -   ", "UNDEFINED", state, idletime, timeptr);

        if (d->host && *d->host)
            sprintf(line + strlen(line), "[%s]\n\r", d->host);
        else
            strcat(line, "[Hostname unknown]\n\r");

        if (d->connected || (!d->connected && CAN_SEE(ch, d->character))) {
            send_to_char(line, ch);
            num_can_see++;
        }
    }

    sprintf(line, "\n\r%d visible sockets connected.\n\r", num_can_see);
    send_to_char(line, ch);
}

ACMD(do_inventory)
{
    send_to_char("You are carrying:\n\r", ch);
    list_obj_to_char(ch->carrying, ch, 1, true);
}

ACMD(do_equipment)
{
    send_to_char("You are using:\n\r", ch);
    show_equipment_to_char(ch, ch);
}

ACMD(do_gen_ps)
{
    extern char circlemud_version[];

    switch (subcmd) {
    case SCMD_CREDITS:
        page_string(ch->desc, credits, 0);
        break;
    case SCMD_NEWS:
        page_string(ch->desc, news, 0);
        break;
    case SCMD_INFO:
        page_string(ch->desc, info, 0);
        break;
    case SCMD_WIZLIST:
        page_string(ch->desc, wizlist, 0);
        break;
    case SCMD_IMMLIST:
        page_string(ch->desc, immlist, 0);
        break;
    case SCMD_HANDBOOK:
        page_string(ch->desc, handbook, 0);
        break;
    case SCMD_POLICIES:
        page_string(ch->desc, policies, 0);
        break;
    case SCMD_CLEAR:
        send_to_char("\033[H\033[J", ch);
        break;
    case SCMD_VERSION:
        send_to_char(circlemud_version, ch);
        break;
    case SCMD_WHOAMI:
        send_to_char(strcat(strcpy(buf, GET_NAME(ch)), "\n\r"), ch);
        break;
    }
}

void perform_mortal_where(struct char_data* ch, char* arg)
{
    int tmploc;
    register struct char_data* i;
    register struct descriptor_data* d;

    if (!*arg) {
        send_to_char("Players in your Zone\n\r--------------------\n\r", ch);
        for (d = descriptor_list; d; d = d->next)
            if (!d->connected) {
                i = (d->original ? d->original : d->character);
                if (i && CAN_SEE(ch, i) && (i->in_room != NOWHERE) && !other_side(ch, i) && (world[ch->in_room].zone == world[i->in_room].zone)) {
                    tmploc = ch->in_room;
                    ch->in_room = i->in_room;
                    sprintf(buf, "%-20s - %s\n\r", GET_NAME(i),
                        (CAN_SEE(ch)) ? world[i->in_room].name : "Somewhere");
                    ch->in_room = tmploc;
                    send_to_char(buf, ch);
                }
            }
    } else { /* print only FIRST char, not all. */
        for (i = character_list; i; i = i->next)
            if ((i->in_room != NOWHERE) && (!IS_NPC(i)) && (world[i->in_room].zone == world[ch->in_room].zone) && (world[i->in_room].level == world[ch->in_room].level) && CAN_SEE(ch, i) && (!other_side(ch, i)) && isname(arg, i->player.name)) {
                tmploc = ch->in_room;
                ch->in_room = i->in_room;
                sprintf(buf, "%-25s - %s\n\r", GET_NAME(i),
                    (CAN_SEE(ch)) ? world[i->in_room].name : "Somewhere");
                ch->in_room = tmploc;
                send_to_char(buf, ch);
                return;
            }
        send_to_char("No-one around by that name.\n\r", ch);
    }
}

void perform_immort_where(struct char_data* ch, char* arg)
{
    int num = 0, found = 0, tmp;
    register struct char_data* i;
    register struct obj_data* k;
    struct obj_data* tmpobj;
    struct descriptor_data* d;

    if (!*arg) {
        send_to_char("Players\n\r-------\n\r", ch);
        for (d = descriptor_list; d; d = d->next)
            if (!d->connected) {
                i = (d->original ? d->original : d->character);
                if (i && CAN_SEE(ch, i) && (i->in_room != NOWHERE)) {
                    if (d->original)
                        sprintf(buf, "%-20s - [%5d] %s (in %s)\n\r", GET_NAME(i),
                            world[d->character->in_room].number,
                            world[d->character->in_room].name, GET_NAME(d->character));
                    else
                        sprintf(buf, "%-20s - [%5d] %s\n\r", GET_NAME(i), world[i->in_room].number,
                            world[i->in_room].name);
                    send_to_char(buf, ch);
                }
            }
    } else {
        for (i = character_list; i; i = i->next)
            if (CAN_SEE(ch, i) && i->in_room != NOWHERE && (isname(arg, i->player.name) || mob_index[i->nr].virt == atoi(arg))) {
                found = 1;
                sprintf(buf, "%3d. %-25s - [%5d] %s\n\r", ++num, GET_NAME(i),
                    world[i->in_room].number, world[i->in_room].name);
                send_to_char(buf, ch);
            }

        for (num = 0, k = object_list; k; k = k->next)
            if (CAN_SEE_OBJ(ch, k) && isname(arg, k->name) || (atoi(arg) == obj_index[k->item_number].virt && atoi(arg))) {
                found = 1;
                tmp = NOWHERE;
                tmpobj = 0;
                i = 0;
                if (k->in_room != NOWHERE)
                    tmp = k->in_room;
                else if (k->carried_by)
                    i = k->carried_by;
                else if (k->in_obj)
                    for (tmpobj = k->in_obj; tmpobj->in_obj; tmpobj = tmpobj->in_obj)
                        ;
                else
                    tmpobj = k;

                if (tmpobj) {
                    if (tmpobj->carried_by)
                        i = tmpobj->carried_by;
                    else {
                        tmp = tmpobj->in_room;
                        sprintf(buf, "%3d. %-25s - [%5d] >> Stored in %s\n\r", ++num,
                            k->short_description, tmp < 0 ? tmp : world[tmp].number,
                            tmpobj ? tmpobj->short_description : "Something");
                        send_to_char(buf, ch);
                    }
                }
                if (i) {
                    if (!CAN_SEE(ch, i)) /* Save wizinvis */
                        continue;
                    tmp = i->in_room;
                    sprintf(buf, "%3d. %-25s - [%5d] >> Carried by %s\n\r", ++num,
                        k->short_description, tmp < 0 ? tmp : world[tmp].number,
                        i ? GET_NAME(i) : "Somebody");
                    send_to_char(buf, ch);
                }
                if (!tmpobj && !i) {
                    sprintf(buf, "%3d. %-25s - [%5d] %s\n\r", ++num, k->short_description,
                        tmp < 0 ? tmp : world[tmp].number,
                        tmp < 0 ? "Nowhere" : world[tmp].name);
                    send_to_char(buf, ch);
                }
            }
        if (!found)
            send_to_char("Couldn't find any such thing.\n\r", ch);
    }
}

ACMD(do_where)
{
    one_argument(argument, arg);

    if (GET_LEVEL(ch) >= LEVEL_GOD + 1 && !RETIRED(ch))
        perform_immort_where(ch, arg);
    else
        perform_mortal_where(ch, arg);
}

ACMD(do_levels)
{
    int i;

    if (IS_NPC(ch)) {
        send_to_char("You ain't nothin' but a hound-dog.\n\r", ch);
        return;
    }

    sprintf(buf,
        "You are %d%% Warrior, %d%% Ranger, %d%% Mystic, %d%% Mage\r\n"
        "Level:  Exp. to Level  : Warrior :  Ranger :  Mystic : Mage :\r\n",
        GET_PROF_COOF(PROF_WARRIOR, ch) / 10, GET_PROF_COOF(PROF_RANGER, ch) / 10,
        GET_PROF_COOF(PROF_CLERIC, ch) / 10, GET_PROF_COOF(PROF_MAGIC_USER, ch) / 10);

    for (i = 1; i < LEVEL_IMMORT && i < 31; i++) {
        sprintf(buf + strlen(buf), "[%2d] %8d-%-8d : %9d %9d %9d %9d\n\r", i, xp_to_level(i),
            xp_to_level(i + 1), i * GET_PROF_COOF(PROF_WARRIOR, ch) / 1000,
            i * GET_PROF_COOF(PROF_RANGER, ch) / 1000,
            i * GET_PROF_COOF(PROF_CLERIC, ch) / 1000,
            i * GET_PROF_COOF(PROF_MAGIC_USER, ch) / 1000);
    }
    page_string(ch->desc, buf, 1);
}

void report_mob_align(struct char_data* ch, struct char_data* victim)
{
    if ((GET_ALIGNMENT(ch) > 0) && (GET_ALIGNMENT(victim) > 0))
        act("It might be evil to kill $M.", FALSE, ch, 0, victim, TO_CHAR);

    else if ((GET_ALIGNMENT(ch) > 0) && (GET_ALIGNMENT(victim) > -GET_ALIGNMENT(ch)))
        act("It won't make you better to kill $M.", FALSE, ch, 0, victim, TO_CHAR);
    else if ((GET_ALIGNMENT(ch) > 0) && (GET_ALIGNMENT(victim) < -GET_ALIGNMENT(ch)))
        act("It might be good to kill $M.", FALSE, ch, 0, victim, TO_CHAR);

    else if ((GET_ALIGNMENT(ch) < 0) && (GET_ALIGNMENT(victim) > GET_ALIGNMENT(ch)))
        act("It might be evil to kill $M.", FALSE, ch, 0, victim, TO_CHAR);
    else if ((GET_ALIGNMENT(ch) < 0) && (GET_ALIGNMENT(victim) < GET_ALIGNMENT(ch)))
        act("It would be noble for you to kill $M.", FALSE, ch, 0, victim, TO_CHAR);
    else
        act("Killing $M won't change you.", FALSE, ch, 0, victim, TO_CHAR);
}

void report_mob_age(struct char_data* ch, struct char_data* victim)
{
    int age;
    char str[255];
    extern int average_mob_life;

    if (!IS_NPC(victim) || (MOB_FLAGGED(victim, MOB_ORC_FRIEND) && MOB_FLAGGED(victim, MOB_PET)))
        return;

    age = MOB_AGE_TICKS(victim, time(0));

    if (age <= 1)
        sprintf(str, "%s has just arrived to this place.\r\n", GET_NAME(victim));
    else if (age <= average_mob_life / 4)
        sprintf(str, "%s has arrived but recently.\r\n", GET_NAME(victim));
    else if (age <= average_mob_life * 3 / 4)
        sprintf(str, "%s has been here for a little while.\r\n", GET_NAME(victim));
    else if (age <= average_mob_life)
        sprintf(str, "%s has been here for quite a while.\r\n", GET_NAME(victim));
    else if (age <= average_mob_life * 3 / 2)
        sprintf(str, "%s has been here for a long time already.\r\n", GET_NAME(victim));
    else
        sprintf(str, "%s has been here for a very long time.\r\n", GET_NAME(victim));
    str[0] = toupper(str[0]);
    send_to_char(str, ch);
}

ACMD(do_consider)
{
    struct char_data* victim;
    int diff;

    one_argument(argument, buf);

    if (!(victim = get_char_room_vis(ch, buf))) {
        send_to_char("Consider killing who?\n\r", ch);
        return;
    }

    if (victim == ch) {
        send_to_char("The perfect match.\n\r", ch);
        return;
    }

    diff = (GET_LEVELB(victim) - GET_LEVELB(ch));

    if (diff <= -10)
        send_to_char("Now where did that chicken go?\n\r", ch);
    else if (diff <= -5)
        send_to_char("You could do it with a needle!\n\r", ch);
    else if (diff <= -3)
        send_to_char("Easy.\n\r", ch);
    else if (diff <= -1)
        send_to_char("Fairly easy.\n\r", ch);
    else if (diff == 0)
        send_to_char("The perfect match!\n\r", ch);
    else if (diff <= 1)
        send_to_char("You would need some luck!\n\r", ch);
    else if (diff <= 2)
        send_to_char("You would need a lot of luck!\n\r", ch);
    else if (diff <= 3)
        send_to_char("You would need a lot of luck and great equipment!\n\r", ch);
    else if (diff <= 5)
        send_to_char("Do you feel lucky, punk?\n\r", ch);
    else if (diff <= 10)
        send_to_char("Are you mad!?\n\r", ch);
    else
        send_to_char("You ARE mad!\n\r", ch);

    report_mob_age(ch, victim);
}

ACMD(do_toggle)
{
    char buf3[100];
    extern char* tactics[];
    extern char* shooting[];
    extern char* casting[];

    if (IS_NPC(ch))
        return;
    if (!WIMP_LEVEL(ch))
        strcpy(buf2, "OFF");
    else
        sprintf(buf2, "%-3d", WIMP_LEVEL(ch));

    switch (GET_TACTICS(ch)) {
    case TACTICS_DEFENSIVE:
        sprintf(buf3, "%s", tactics[0]);
        break;
    case TACTICS_CAREFUL:
        sprintf(buf3, "%s", tactics[1]);
        break;
    case TACTICS_NORMAL:
        sprintf(buf3, "%s", tactics[2]);
        break;
    case TACTICS_AGGRESSIVE:
        sprintf(buf3, "%s", tactics[3]);
        break;
    case TACTICS_BERSERK:
        sprintf(buf3, "%s", tactics[4]);
        break;
    default:
        sprintf(buf3, "tactical error, please notify IMPs.");
        break;
    }

    sprintf(buf,
        "         Prompt: %-3s    "
        "     Brief Mode: %-3s    "
        "         NoTell: %-3s\r\n"
        "   Compact Mode: %-3s    "
        "Narrate Channel: %-3s    "
        "      MSDP Mode: %-3s\r\n"
        "      Spam Mode: %-3s    "
        "   Chat Channel: %-3s    "
        " Incognito Mode: %-3s\r\n"
        "           Echo: %-3s    "
        "   Sing Channel: %-3s    "
        "    Auto Mental: %-3s\r\n"
        "      Wrap Mode: %-3s    "
        " Summon Protect: %-3s    "
        "     Wimp Level: %-3s\r\n"
        "           Swim: %-3s    "
        "        Latin-1: %-3s    "
        "        Spinner: %-3s\r\n"
        "  Advanced View: %-3s    "
        "Advanced Prompt: %-3s    ",
        ONOFF(PRF_FLAGGED(ch, PRF_PROMPT)), ONOFF(PRF_FLAGGED(ch, PRF_BRIEF)),
        ONOFF(PRF_FLAGGED(ch, PRF_NOTELL)), ONOFF(PRF_FLAGGED(ch, PRF_COMPACT)),
        ONOFF(PRF_FLAGGED(ch, PRF_NARRATE)), ONOFF(PRF_FLAGGED(ch, PRF_MSDP)),
        ONOFF(PRF_FLAGGED(ch, PRF_SPAM)), ONOFF(PRF_FLAGGED(ch, PRF_CHAT)),
        ONOFF(!PRF_FLAGGED(ch, PLR_INCOGNITO)), ONOFF(PRF_FLAGGED(ch, PRF_ECHO)),
        ONOFF(PRF_FLAGGED(ch, PRF_SING)), ONOFF(PRF_FLAGGED(ch, PRF_MENTAL)),
        ONOFF(PRF_FLAGGED(ch, PRF_WRAP)), ONOFF(PRF_FLAGGED(ch, PRF_SUMMONABLE)), buf2,
        ONOFF(PRF_FLAGGED(ch, PRF_SWIM)), ONOFF(PRF_FLAGGED(ch, PRF_LATIN1)),
        ONOFF(PRF_FLAGGED(ch, PRF_SPINNER)), ONOFF(PRF_FLAGGED(ch, PRF_ADVANCED_VIEW)),
        ONOFF(PRF_FLAGGED(ch, PRF_ADVANCED_PROMPT)));
    send_to_char(buf, ch);

    /* the special, immortal set list */
    if (GET_LEVEL(ch) >= LEVEL_IMMORT) {
        sprintf(buf,
            "      Roomflags: %-3s\r\n"
            "      Holylight: %-3s    "
            "       Nohassle: %-3s    "
            " Wiznet Channel: %-3s\r\n",
            ONOFF(PRF_FLAGGED(ch, PRF_ROOMFLAGS)), ONOFF(PRF_FLAGGED(ch, PRF_HOLYLIGHT)),
            ONOFF(PRF_FLAGGED(ch, PRF_NOHASSLE)), ONOFF(PRF_FLAGGED(ch, PRF_WIZ)));
        send_to_char(buf, ch);
    }

    sprintf(buf, "\r\nYou are employing %s tactics, and are speaking %s.\r\n", buf3,
        ch->player.language ? skills[ch->player.language].name : "common tongue");
    send_to_char(buf, ch);
    if (GET_SPEC(ch) == PLRSPEC_ARCH) {
        switch (GET_SHOOTING(ch)) {
        case SHOOTING_SLOW:
            sprintf(buf3, "%s", shooting[0]);
            break;
        case SHOOTING_NORMAL:
            sprintf(buf3, "%s", shooting[1]);
            break;
        case SHOOTING_FAST:
            sprintf(buf3, "%s", shooting[2]);
            break;
        default:
            sprintf(buf3, "shooting error, please notify IMMs!");
            break;
        }
        sprintf(buf, "You are using %s shooting speed.\r\n", buf3);
        send_to_char(buf, ch);
    }
    if (GET_SPEC(ch) == PLRSPEC_ARCANE) {
        switch (GET_CASTING(ch)) {
        case CASTING_SLOW:
            sprintf(buf3, "%s", casting[0]);
            break;
        case CASTING_NORMAL:
            sprintf(buf3, "%s", casting[1]);
            break;
        case CASTING_FAST:
            sprintf(buf3, "%s", casting[2]);
            break;
        default:
            sprintf(buf3, "casting error, please notify IMMs!");
            break;
        }
        sprintf(buf, "You are using %s casting speed.\r\n", buf3);
        send_to_char(buf, ch);
    }
}

void sort_commands(void)
{
    int a, b, tmp;
    ACMD(do_action);

    num_of_cmds = 1;

    while (num_of_cmds < MAX_CMD_LIST && cmd_info[num_of_cmds].command_pointer) {
        cmd_info[num_of_cmds].sort_pos = num_of_cmds - 1;
        cmd_info[num_of_cmds].is_social = (cmd_info[num_of_cmds].command_pointer == do_action);
        num_of_cmds++;
    }

    num_of_cmds--;
    cmd_info[33].is_social = 1; /* do_insult */

    for (a = 1; a <= num_of_cmds - 1; a++)
        for (b = a + 1; b <= num_of_cmds; b++)
            if (strcmp(command[cmd_info[a].sort_pos], command[cmd_info[b].sort_pos]) > 0) {
                tmp = cmd_info[a].sort_pos;
                cmd_info[a].sort_pos = cmd_info[b].sort_pos;
                cmd_info[b].sort_pos = tmp;
            }
}

ACMD(do_commands)
{
    int no, i, cmd_num;
    int wizhelp = 0, socials = 0;
    struct char_data* vict;

    one_argument(argument, buf);

    if (*buf) {
        if (!(vict = get_char_vis(ch, buf)) || IS_NPC(vict)) {
            send_to_char("Who is that?\n\r", ch);
            return;
        }
        if (GET_LEVEL(ch) < GET_LEVEL(vict)) {
            send_to_char("Can't determine commands of people above your level.\n\r", ch);
            return;
        }
    } else
        vict = ch;

    if (subcmd == SCMD_SOCIALS) {
        sprintf(buf, "The following socials are available to %s:\n\r",
            vict == ch ? "you" : GET_NAME(vict));

        for (no = 1; no < social_list_top; no++) {
            if ((GET_LEVEL(ch) >= LEVEL_GOD) && PRF_FLAGGED(ch, PRF_ROOMFLAGS))
                sprintf(buf + strlen(buf), "(%3d)%-11s", no, soc_mess_list[no].command);
            else
                sprintf(buf + strlen(buf), "%-16s", soc_mess_list[no].command);
            if (!(no % 5))
                strcat(buf, "\n\r");
        }
        strcat(buf, "\n\r");
        send_to_char(buf, ch);
        return;
    }

    if (subcmd == SCMD_WIZHELP)
        wizhelp = 1;

    sprintf(buf, "The following %s%s are available to %s:\n\r", wizhelp ? "privileged " : "",
        socials ? "socials" : "commands", vict == ch ? "you" : GET_NAME(vict));

    for (no = 1, cmd_num = 1; cmd_num <= num_of_cmds; cmd_num++) {
        i = cmd_info[cmd_num].sort_pos;
        if (cmd_info[i + 1].minimum_level >= 0 && (cmd_info[i + 1].minimum_level >= LEVEL_IMMORT) == wizhelp && GET_LEVEL(vict) >= cmd_info[i + 1].minimum_level && (wizhelp || socials == cmd_info[i + 1].is_social)) {
            if ((GET_LEVEL(ch) >= LEVEL_GOD) && PRF_FLAGGED(ch, PRF_ROOMFLAGS))
                sprintf(buf + strlen(buf), "(%3d)%-11s", i + 1, command[i]);
            else
                sprintf(buf + strlen(buf), "%-16s", command[i]);
            if (!(no % 5))
                strcat(buf, "\n\r");
            no++;
        }
    }

    strcat(buf, "\n\r");
    send_to_char(buf, ch);
}

ACMD(do_diagnose)
{
    struct char_data* vict;

    one_argument(argument, buf);

    if (*buf) {
        if (!(vict = get_char_room_vis(ch, buf))) {
            send_to_char("No-one by that name here.\n\r", ch);
            return;
        } else
            diag_char_to_char(vict, ch);
    } else {
        if (ch->specials.fighting)
            diag_char_to_char(ch->specials.fighting, ch);
        else
            send_to_char("Diagnose who?\n\r", ch);
    }
}

extern char* prompt_text[];
extern struct prompt_type prompt_hit[];
extern struct prompt_type prompt_mana[];
extern struct prompt_type prompt_move[];
extern struct prompt_type prompt_mount[];

void add_prompt(char* prompt, struct char_data* ch, long flag)
{
    int tmp;
    char str[250];
    if (flag & PRF_DISPTEXT) {
        if (ch->specials.prompt_value >= 0)
            sprintf(str, prompt_text[ch->specials.prompt_number], ch->specials.prompt_value);
        else
            sprintf(str, prompt_text[ch->specials.prompt_number], -1);
        sprintf(prompt, "%s%s%c", prompt, str, 0);
        return;
    }

    if (flag & PROMPT_ADVANCED) {
        sprintf(prompt, "%sHP: %d/%d S: %d/%d MV: %d/%d]%c", prompt, GET_HIT(ch), GET_MAX_HIT(ch),
            GET_MANA(ch), GET_MAX_MANA(ch), GET_MOVE(ch), GET_MAX_MOVE(ch), 0);
        return;
    }
    if (GET_MAX_HIT(ch))
        if (flag & PROMPT_HIT) {
            for (tmp = 0; (1000 * GET_HIT(ch)) / GET_MAX_HIT(ch) > prompt_hit[tmp].value; tmp++)
                ;
            if ((GET_HIT(ch) != GET_MAX_HIT(ch)) || (ch->specials.position == POSITION_FIGHTING))
                sprintf(prompt, "%s%s%c", prompt, prompt_hit[tmp].message, 0);
        }
    if (flag & PROMPT_STAT) {
        report_char_mentals(ch, str, 1);
        sprintf(prompt, "%s%s%c", prompt, str, 0);
        return;
    }
    if (flag & PROMPT_MAUL) {
        affected_type* maul_aff = affected_by_spell(ch, SKILL_MAUL);
        int mod = maul_aff->duration * 10 / 2;
        sprintf(prompt, "%s%d/1000%c", prompt, mod, 0);
    }

    if (flag & PROMPT_ARROWS) {
        int arrows = 0;
        struct obj_data* quiver = ch->equipment[WEAR_BACK];
        struct obj_data* arrow;
        for (arrow = quiver->contains; arrow; arrow = arrow->next_content) {
            arrows++;
        }
        sprintf(prompt, "%s%d)%c", prompt, arrows, 0);
    }

    if (GET_MAX_MANA(ch))
        if (flag & PROMPT_MANA) {
            for (tmp = 0; (1000 * GET_MANA(ch)) / GET_MAX_MANA(ch) > prompt_mana[tmp].value; tmp++)
                ;
            sprintf(prompt, "%s%s%c", prompt, prompt_mana[tmp].message, 0);
        }
    if (GET_MAX_MOVE(ch))
        if (flag & PROMPT_MOVE) {
            for (tmp = 0; (1000 * GET_MOVE(ch)) / GET_MAX_MOVE(ch) > prompt_move[tmp].value; tmp++)
                ;

            if (IS_NPC(ch) && MOB_FLAGGED(ch, MOB_MOUNT))
                sprintf(prompt, "%s%s%c", prompt, prompt_mount[tmp].message, 0);
            else
                sprintf(prompt, "%s%s%c", prompt, prompt_move[tmp].message, 0);
        }
}

ACMD(do_whois)
{
    int isplaying, numname, incognito;
    int t_race, t_level;
    time_t tt;
    int _player;
    char str[255];
    char name[MAX_INPUT_LENGTH];
    char *t_title, *t_retired;
    const char *retired = " [Retired]", *blank = "";
    struct char_data* target;
    struct char_file_u cfu;
    static const char* nsp = "No such player.\n\r";
#define P (player_table + _player)

    one_argument(argument, name);
    if (!*name) {
        send_to_char("Whois who?\n\r", ch);
        return;
    }

    /* Immortals can whois by id number (GET_IDNUM(ch)) */
    if (isdigit(*name)) {
        if (GET_LEVEL(ch) < LEVEL_IMMORT) {
            send_to_char(nsp, ch);
            return;
        }
        numname = atoi(name);
    } else
        numname = -1;

    _player = find_player_in_table(name, numname);
    if (_player < 0) {
        send_to_char(nsp, ch);
        return;
    }

    /* Get all the information we need */
    t_retired = (char*)blank;
    target = find_playing_char(P->idnum);
    if (target) {
        isplaying = 1;
        tt = target->desc->login_time;
        t_race = GET_RACE(target);
        t_level = GET_LEVEL(target);
        t_title = target->player.title;
        if (PLR_FLAGGED(target, PLR_RETIRED))
            t_retired = (char*)retired;
        incognito = PLR_FLAGGED(target, PLR_INCOGNITO);
    } else {
        isplaying = 0;
        tt = P->log_time;
        if (load_player(name, &cfu) == -1)
            cfu.title[0] = '\0';
        t_race = P->race;
        t_level = P->level;
        t_title = cfu.title;
        if (IS_SET(P->flags, PLR_RETIRED))
            t_retired = (char*)retired;
        incognito = IS_SET(P->flags, PLR_INCOGNITO);
    }

    if (other_side_num(GET_RACE(ch), t_race)) {
        /* P->title replaced with "" in the new player file format */
        if (incognito)
            sprintf(str, "%s %s%s.\n\r", P->name, t_title, t_retired);
        else
            sprintf(str, "%s %s is %s%s.\n\r", P->name, t_title, get_level_abbr(t_level, t_race),
                t_retired);
    } else { /* Now we assume they aren't on opposite sides of the war */
        if (incognito && GET_LEVEL(ch) < LEVEL_GRGOD)
            sprintf(str, "%s %s%s.\n\r", P->name, t_title, t_retired);
        else
            /* Playtime info given for: mortals, immortals whoising lower targets */
            if ((GET_LEVEL(ch) >= LEVEL_IMMORT && GET_LEVEL(ch) >= t_level) || t_level < LEVEL_IMMORT)
                sprintf(str, "%s %s is %s%s.\n\r%s %s\r", P->name, t_title,
                    get_level_abbr(t_level, t_race), t_retired,
                    isplaying ? "Playing since " : "Last seen ", asctime(localtime(&tt)));
            else
                sprintf(str, "%s %s is %s%s.\n\r", P->name, t_title,
                    get_level_abbr(t_level, t_race), t_retired);
    }

    *str = toupper(*str);
    send_to_char(str, ch);

    return;
#undef P
}

/*
 * Return an appropriate string describing a player's level.
 * Used for things like `whois'.  Note: this is not thread
 * safe.
 */
static char* get_level_abbr(sh_int level, sh_int race)
{
    static char buf[256];

    switch (level) {
    case LEVEL_IMPL:
        sprintf(buf, "an Implementor");
        break;

    case LEVEL_GRGOD + 2:
    case LEVEL_GRGOD + 1:
    case LEVEL_GRGOD:
        sprintf(buf, "one of the Aratar (level %d)", level);
        break;

    case LEVEL_AREAGOD + 1:
    case LEVEL_AREAGOD:
        sprintf(buf, "one of the Valar (level %d)", level);
        break;

    case LEVEL_GOD + 1:
        sprintf(buf, "one of the Greater Maiar");
        break;

    case LEVEL_GOD:
        sprintf(buf, "one of the Maiar");
        break;

    case LEVEL_IMMORT + 1:
    case LEVEL_IMMORT:
        sprintf(buf, "one of the Lesser Maiar (level %d)", level);
        break;

    default:
        sprintf(buf, "a level %d %s", level, pc_races[race]);
        break;
    }

    return buf;
}

/*
 * do_map is now associated with the 'world' command.
 * "map" relates to do_small_map to handle the immense
 * world.
 */
ACMD(do_map)
{
    char tmpch;
    int zone;

    zone = world[ch->in_room].zone;
    tmpch = zone_table[zone].symbol;

    /*
     * The zone_table is already generated, we just write an X
     * to the character's current location, send them that map,
     * then set the X back to what it was before.  This is very
     * thread-not-safe.
     */
    symbol_to_map(zone_table[zone].x, zone_table[zone].y, 'X');
    send_to_char(world_map, ch);
    symbol_to_map(zone_table[zone].x, zone_table[zone].y, tmpch);

    return;
}

/*
 * Edit SMALL_WORLD_RADIUS variable in structs.h to change
 * the map size.  Never make SMALL_WORLD_RADIUS larger than
 * WORLD_SIZE_Y / 2 or WORLD_SIZE_X / 4.  Always round
 * down in this calculation.
 */
void calculate_small_map(int x, int y)
{
    int tmpx, tmpy;

    reset_small_map();

    if (y < SMALL_WORLD_RADIUS) {
        if (x < SMALL_WORLD_RADIUS) /* top left */
            x = y = SMALL_WORLD_RADIUS;
        else if (x > ((WORLD_SIZE_X / 2) - SMALL_WORLD_RADIUS - 1)) { /* top right */
            x = (WORLD_SIZE_X / 2) - SMALL_WORLD_RADIUS - 1;
            y = SMALL_WORLD_RADIUS;
        } else /* top center */
            y = SMALL_WORLD_RADIUS;
    } else if (y > WORLD_SIZE_Y - SMALL_WORLD_RADIUS - 1) {
        if (x < SMALL_WORLD_RADIUS) { /* bottom left */
            x = SMALL_WORLD_RADIUS;
            y = WORLD_SIZE_Y - SMALL_WORLD_RADIUS - 1;
        } else if (x > ((WORLD_SIZE_X / 2) - SMALL_WORLD_RADIUS - 1)) { /* bottom right */
            x = (WORLD_SIZE_X / 2) - SMALL_WORLD_RADIUS - 1;
            y = WORLD_SIZE_Y - SMALL_WORLD_RADIUS - 1;
        } else /* bottom center */
            y = WORLD_SIZE_Y - SMALL_WORLD_RADIUS - 1;
    } else {
        if (x < SMALL_WORLD_RADIUS) /* center left */
            x = SMALL_WORLD_RADIUS;
        else if (x > (WORLD_SIZE_X / 2) - SMALL_WORLD_RADIUS - 1) /* center right */
            x = (WORLD_SIZE_X / 2) - SMALL_WORLD_RADIUS - 1;
        /* else: center center requires no changes */
    }

    /* Build the small_map with the updated central coordinates */
    for (tmpy = 0 - SMALL_WORLD_RADIUS; tmpy <= SMALL_WORLD_RADIUS; tmpy++)
        for (tmpx = 0 - SMALL_WORLD_RADIUS * 2; tmpx <= SMALL_WORLD_RADIUS * 2; tmpx += 2)
            small_map[SMALL_WORLD_RADIUS + 1 + tmpy][2 * SMALL_WORLD_RADIUS + 2 + tmpx] = world_map[(y + 1 + tmpy) * (WORLD_SIZE_X + 4) + (x + (tmpx / 2)) * 2 + 1];
}

/*
 * Show the player a small map, focused on where they are
 * in the world.  Corresponds to the 'map' command; the
 * full world requires 'world' to be typed instead.
 */
ACMD(do_small_map)
{
    char tmpch;
    int zone;

    zone = world[ch->in_room].zone;
    tmpch = zone_table[zone].symbol;

    symbol_to_map(zone_table[zone].x, zone_table[zone].y, 'X');
    calculate_small_map(zone_table[zone].x, zone_table[zone].y);
    send_to_char(small_map[0], ch);
    send_to_char("\n\r", ch);
    symbol_to_map(zone_table[zone].x, zone_table[zone].y, tmpch);

    return;
}

ACMD(do_search)
{
    int tmp = 0, len, skill, search_res, uncover_skill;
    struct room_direction_data* ex;
    struct char_data* tmpch;

    if (ch->in_room == NOWHERE)
        return;

    if (IS_NPC(ch) && IS_AFFECTED(ch, AFF_CHARM)) {
        send_to_char("You should allow your superior to search the area.\n\r", ch);
        return;
    }

    switch (subcmd) {
    case 0:
        while (*argument && (*argument <= ' '))
            argument++;

        if (!*argument) {
            send_to_char("You start searching the area.\n\r", ch);
            act("$n searches the area carefully.", TRUE, ch, 0, 0, TO_ROOM);
            WAIT_STATE_FULL(ch, 10, CMD_SEARCH, 2, 30, tmp, 0, 0, AFF_WAITING | AFF_WAITWHEEL,
                TARGET_OTHER);
            return;
        }

        len = strlen(argument);

        for (tmp = 0; tmp < NUM_OF_DIRS; tmp++)
            if (!strn_cmp(dirs[tmp], argument, len))
                break;

        if (tmp >= NUM_OF_DIRS) {
            send_to_char("You need to search in a direction.\n\r", ch);
            return;
        }

        WAIT_STATE_FULL(ch, 10, CMD_SEARCH, 1, 30, tmp, 0, 0, AFF_WAITING | AFF_WAITWHEEL,
            TARGET_OTHER);
        break;

    case 1:
        tmp = wtl->flg;
        ex = world[ch->in_room].dir_option[tmp];

        sprintf(buf, "$n searches for something to %s.", refer_dirs[tmp]);
        act(buf, TRUE, ch, 0, 0, TO_ROOM);

        if (ex && !IS_SET(ex->exit_info, EX_ISDOOR) && !IS_SET(ex->exit_info, EX_CLOSED)) {
            send_to_char("You could not find anything there.\n\r", ch);
            return;
        }

        skill = GET_SKILL(ch, SKILL_SEARCH);
        if (!CAN_SEE(ch))
            skill -= 50;

        if (skill > number(0, 99)) {
            if (!ex || (ex->to_room == NOWHERE))
                sprintf(buf, "There is no passage %s.\n\r", dirs[tmp]);
            else {
                if (ex->keyword && *ex->keyword)
                    sprintf(buf, "You found %s at %s.\n", ex->keyword, refer_dirs[tmp]);
                else
                    sprintf(buf, "The exit %s has no name, please notify immortals.\n\r",
                        dirs[tmp]);
            }
            send_to_char(buf, ch);
        } else
            send_to_char("You could not find anything there.\n\r", ch);
        break;

    case 2:
        if (!CAN_SEE(ch)) {
            send_to_char("It is too dark for you to find anything.\n\r", ch);
            return;
        }

        uncover_skill = MIN(200, (int)((float)(GET_SKILL(ch, SKILL_SEARCH) + see_hiding(ch)) * 1.50));
        uncover_skill = number(uncover_skill, uncover_skill * 7 / 6);
        search_res = 0;
        for (tmpch = world[ch->in_room].people; tmpch; tmpch = tmpch->next_in_room) {
            tmp = GET_HIDING(tmpch);
            GET_HIDING(tmpch) = 0;
            if (tmp && (tmpch != ch) && (uncover_skill > tmp) && CAN_SEE(ch, tmpch)) {
                act("You uncovered $N!", FALSE, ch, 0, tmpch, TO_CHAR);
                act("$n uncovered you!", FALSE, ch, 0, tmpch, TO_VICT);
                stop_hiding(tmpch, FALSE);
                search_res++;
            } else if (tmp && (tmpch != ch))
                GET_HIDING(tmpch) = tmp - uncover_skill / 3;
        }
        if (!search_res)
            send_to_char("You haven't found anything suspicious.\n\r", ch);
        break;
    }

    return;
}

ACMD(do_orc_delay)
{
    switch (subcmd) {
    case 0:
        if ((GET_RACE(ch) == RACE_URUK) || (GET_RACE(ch) == RACE_MAGUS)) {
            // WAIT_STATE_FULL(ch, 5, CMD_ORC_DELAY, 2, 40, 0, 0, 0, AFF_WAITING|AFF_ORC_DELAY,
            // TARGET_NONE);
            send_to_char("The power of light burns your eyes.\n\r", ch);
        } else if (GET_RACE(ch) == RACE_ORC) {
            // WAIT_STATE_FULL(ch, 7, CMD_ORC_DELAY, 2, 40, 0, 0, 0, AFF_WAITING|AFF_ORC_DELAY,
            // TARGET_NONE);
            send_to_char("The intensity of light here makes it hard to see.\n\r", ch);
        }
        break;
    case 1:
    default:
        // REMOVE_BIT(ch->specials.affected_by, AFF_ORC_DELAY);
        //    send_to_char("\n\r",ch);
        break;
    }
}

void report_perception(char_data* ch, char* str)
{

    if (GET_PERCEPTION(ch) == 0) {
        sprintf(str, "%s mind is totally numb.\n\r", HSHR(ch));
    } else if (GET_PERCEPTION(ch) < 20) {
        sprintf(str, "%s mind is as well as numb.\n\r", HSHR(ch));
    } else if (GET_PERCEPTION(ch) < 50) {
        sprintf(str, "%s is moderately sensitive to the spiritual.\n\r", HSSH(ch));
    } else if (GET_PERCEPTION(ch) < 80) {
        sprintf(str, "%s is well aware of the Wraith-world.\n\r", HSSH(ch));
    } else if (GET_PERCEPTION(ch) < 100) {
        sprintf(str, "%s is very perceptive to the Wraith-world.\n\r", HSSH(ch));
    } else {
        sprintf(str, "%s is one with the Wraith-world!\n\r", HSSH(ch));
    }
    str[0] = UPPER(str[0]);
}

void report_affection(affected_type* aff, char* str)
{
    static const char* durations[] = { "permanent", "short", "medium", "long", "fast-acting" };

    int dur_index = 0;

    if (aff->duration < 0)
        dur_index = 0;
    else if (aff->duration < 3)
        dur_index = 1;
    else if (aff->duration < 12)
        dur_index = 2;
    else
        dur_index = 3;

    const skill_data& skill = skills[aff->type];
    const char* skill_name = skill.name;
    const char* duration = durations[dur_index];

    char duration_text[32];
    if (skill.is_fast) {
        sprintf(duration_text, "%s, %s", duration, durations[4]);
    } else {
        sprintf(duration_text, "%s", duration);
    }

    sprintf(str, "%-30s (%s)\n\r", skill_name, duration_text);
}

void report_skill_timer(const char_data& ch, char* buf)
{
    game_timer::skill_timer& timer = game_timer::skill_timer::instance();
    buf += timer.report_skill_status(utils::get_idnum(ch), buf);
}

ACMD(do_affections)
{

    char str[255];
    affected_type* tmpaff;

    if (IS_AFFECTED(ch, AFF_SNEAK))
        send_to_char("You are trying to sneak.\n\r", ch);

    if (IS_AFFECTED(ch, AFF_HUNT))
        send_to_char("You are trying to hunt for tracks.\n\r", ch);

    if (ch->equipment[WIELD] && ch->equipment[WIELD]->obj_flags.prog_number == 1)
        send_to_char("Your weapon is focused to your will.\n\r", ch);

    if (IS_AFFECTED(ch, AFF_MOONVISION) && OUTSIDE(ch) && weather_info.moonlight)
        send_to_char("The moon lights your surroundings.\n\r", ch);

    if (!ch->affected) {
        strcpy(buf, "You are not affected by anything.\n\r");
    } else {
        sprintf(buf, "You are affected by:\n\r");

        for (tmpaff = ch->affected; tmpaff; tmpaff = tmpaff->next) {
            report_affection(tmpaff, str);
            sprintf(buf, "%s%s", buf, str);
        }
    }
    report_skill_timer(*ch, buf);

    game_rules::big_brother& bb_instance = game_rules::big_brother::instance();
    if (bb_instance.is_target_looting(ch)) {
        sprintf(buf, "%sYou are under the protection of the Gods.\n\r", buf);
    }

    /* checking for a prepared spell */
    if ((ch->delay.cmd == CMD_PREPARE) && (ch->delay.targ1.type == TARGET_IGNORE)) {
        sprintf(buf, "%sYou have prepared the '%s' spell.\n\r", buf,
            skills[ch->delay.targ1.ch_num].name);
    } else if (ch->delay.cmd == CMD_TRAP)
        sprintf(buf, "%sYou lay in wait to trap an unsuspecting victim.\r\n", buf);
    if (SUN_PENALTY(ch))
        strcat(buf, "You feel weak under the intensity of light.\n\r");

    send_to_char(buf, ch);
}

/*
 * Controls the format of the output of fame war.  Each column
 * of the fame war output is as follows:
 *  |  #. | name race            | fame  |
 *   < 4 > <-------- 30 --------> <- 4 ->
 * Each number is a two place number, right justified with a period
 * and space following it.  Character names are limited by
 * MAX_NAME_LENGTH, which is currently 15 and the longest race string
 * is "the Uruk-Lhuth", which is 14 characters long.  Hence we give
 * 30 characters for the name and race.  3 are given for the fame
 * value, and one space is forced.  Hence the field is 4 wide.
 * The total is a 38 character field.  This leaves room for two spaces
 * between the two columns.
 *
 * Note that the standard terminal width is 80 characters, so with a
 * single space separating the two lists side-by-side, we cannot go
 * any higher than 39 characters per leader.
 */

#define MAX_LEADER_STRING 40

void do_fame_leader_string(LEADER* ldr, char* buffer)
{
    int i, n;
    size_t bufpt;

    if (ldr->invalid) {
        sprintf(buffer, "%27s", " ");
        return;
    }

    bufpt = sprintf(buffer, "%2d. %s", ldr->rank + 1, ldr->name);
    bufpt += sprintf(buffer + bufpt, " the %s", pc_races[ldr->race]);

    /* Pad with spaces til the end of name/title section */
    n = 27 - strlen(ldr->name) - strlen(pc_races[ldr->race]) - 5;
    for (i = 0; i < n; ++i)
        bufpt += sprintf(buffer + bufpt, " ");

    bufpt += sprintf(buffer + bufpt, " %4d", ldr->fame);
}

ACMD(do_fame)
{
    int i, n;
    int ldr1valid, ldr2valid;
    int idx;
    int numname;
    int records;
    size_t bufpt;
    char leaderstr[MAX_LEADER_STRING];
    char* string;
    char name[MAX_INPUT_LENGTH];
    char* warheader = "Status of the War in Middle-earth";
    char* good_victory = "The free peoples of Middle-earth are victorious "
                         "over the forces of the Shadow.";
    char* evil_victory = "The power of the Shadow falls over Middle-earth.";
    char* no_victory = "The war in Middle-earth favors neither the "
                       "Shadow nor the free peoples.";
    PKILL* pkills;
    LEADER *ldr1, *ldr2;

    if (!ch->desc) {
        send_to_char("Mobiles cannot do this.\r\n", ch);
        return;
    }

    buf[0] = '\0';
    one_argument(argument, name);

    if (!strcmp(name, "war")) {
        bufpt = sprintf(buf, "%22s%s\r\n\r\n", " ", warheader);

        /* Report the top 10 fame leaders */
        bufpt += sprintf(buf + bufpt, "    The Free Peoples of Middle-earth     "
                                      "    The Forces of the Shadow\r\n");
        for (i = 0; i < 10; ++i) {
            /* Good rank i leader */
            ldr1 = pkill_get_leader_by_rank(i, RACE_WOOD);
            ldr1valid = !ldr1->invalid;
            do_fame_leader_string(ldr1, leaderstr);
            bufpt += sprintf(buf + bufpt, "%s%5s", leaderstr, " ");
            pkill_free_leader(ldr1);

            /* Evil rank i leader */
            ldr2 = pkill_get_leader_by_rank(i, RACE_URUK);
            ldr2valid = !ldr2->invalid;
            do_fame_leader_string(ldr2, leaderstr);
            bufpt += sprintf(buf + bufpt, "%s\r\n", leaderstr);
            pkill_free_leader(ldr2);

            /* If both ranks were invalid, stop looping */
            if (!ldr1valid && !ldr2valid)
                break;
        }
        if (i == 10)
            bufpt += sprintf(buf + bufpt, "\r\n");

        /* Report the exact states of the war */
        bufpt += sprintf(buf + bufpt, "Total fame for the free peoples of Middle-earth: %d\r\n",
            pkill_get_good_fame());
        bufpt += sprintf(buf + bufpt, "Total fame for the forces of the Shadow: %d\r\n",
            pkill_get_evil_fame());
        bufpt += sprintf(buf + bufpt, "\r\n");

        /* Report the general state of the war */
        if (pkill_get_good_fame() > pkill_get_evil_fame())
            sprintf(buf + bufpt, "%s\r\n", good_victory);
        else if (pkill_get_good_fame() < pkill_get_evil_fame())
            sprintf(buf + bufpt, "%s\r\n", evil_victory);
        else
            sprintf(buf + bufpt, "%s\r\n", no_victory);

        send_to_char(buf, ch);
        return;
    }

    /* The command was "fame all" */
    if (!strcmp(name, "all")) {
        pkills = pkill_get_new_kills(&n);
        if (pkills == NULL) {
            send_to_char("No notable battles have occurred of late.\r\n", ch);
            return;
        }

        bufpt = 0;
        for (i = 0; i < n; ++i) {
            string = pkill_get_string(&pkills[i], PKILL_STRING_KILLED);
            bufpt += sprintf(buf + bufpt, string);
            free(string);
        }

        page_string(ch->desc, buf, 1);
        return;
    }

    /* If we got here, it was neither fame all or fame war */
    /* XXX: This should all be replaced with better target code */
    if (*name == '\0')
        numname = ch->specials2.idnum;
    else if (isdigit(*name)) {
        /* Imms can fame by idnum */
        if (GET_LEVEL(ch) < LEVEL_IMMORT) {
            send_to_char("No such player.\r\n", ch);
            return;
        }
        numname = atoi(name);
    } else
        numname = -1;

    idx = find_player_in_table(name, numname);
    if (idx < 0) {
        send_to_char("No such player.\r\n", ch);
        return;
    }

    /* XXX: Quick and dirty.  We need a pkill API to get a list of
     * kills pertaining only to this character; but for now we'll just
     * use the entire list (that's what the legacy code does anyway).
     */
    pkills = pkill_get_all(&n);
    records = 0;
    bufpt = 0;

    /* Get the records where the character idx is the killer */
    for (i = 0; i < n; ++i) {
        if (pkills[i].killer == idx) {
            string = pkill_get_string(&pkills[i], PKILL_STRING_KILLED);
            bufpt += sprintf(buf + bufpt, string);
            free(string);
            ++records;
        }
    }

    /* Get the records where the character idx is the victim */
    for (i = 0; i < n; ++i) {
        if (pkills[i].victim == idx) {
            string = pkill_get_string(&pkills[i], PKILL_STRING_SLAIN);
            bufpt += sprintf(buf + bufpt, string);
            free(string);
            ++records;
        }
    }

    /* Display the total fame */
    asprintf(&string, "%s", player_table[idx].name);
    CAP(string);
    sprintf(buf + bufpt,
        "There %s %d record%s found about %s, "
        "total fame %d.\r\n",
        records == 1 ? "was" : "were", records, records == 1 ? "" : "s", string,
        player_table[idx].warpoints / 100);
    free(string);

    send_to_char(buf, ch);
}

ACMD(do_rank)
{
    int i, r;
    int ldrvalid;
    char* s;
    char leaderstr[MAX_LEADER_STRING];
    size_t bufpt;
    LEADER* ldr;

    r = pkill_get_rank_by_character(ch, false);

    /* Unranked characters don't get much output */
    if (r == PKILL_UNRANKED) {
        send_to_char("You have not accomplished anything worthy of rank.\r\n", ch);
        return;
    }

    s = nth(r + 1);
    bufpt = sprintf(buf, "You are ranked %s among %s:\r\n", s,
        RACE_GOOD(ch) ? "the free peoples of Middle-earth" : "the forces of the Shadow");
    free(s);

    /* Show 7 characters: 3 above ch, ch and 3 below ch */
    i = MAX(0, r - 3);

    /* We didn't start with the first ranked character */
    if (i > 0)
        bufpt += sprintf(buf + bufpt, "       ...");

    bufpt += sprintf(buf + bufpt, "\r\n");

    for (i = MAX(0, r - 3); i < r + 3; ++i) {
        ldr = pkill_get_leader_by_rank(i, GET_RACE(ch));
        ldrvalid = !ldr->invalid;
        do_fame_leader_string(ldr, leaderstr);
        bufpt += sprintf(buf + bufpt, " %s %s\r\n", i == r ? "*" : " ", leaderstr);
        pkill_free_leader(ldr);

        if (!ldrvalid)
            break;
    }

    /* We didn't hit the last ranked character */
    if (i == r + 3)
        bufpt += sprintf(buf + bufpt, "       ...\r\n");

    vsend_to_char(ch, buf);
}

ACMD(do_compare)
{
    obj_data *obj1, *obj2;
    char str1[MAX_INPUT_LENGTH], str2[MAX_INPUT_LENGTH];
    int lev;

    str1[0] = str2[0] = 0;

    argument = one_argument(argument, str1);
    one_argument(argument, str2);

    if (!str1[0] || !str2[0]) {
        send_to_char("You can compare only two objects in your inventory.\n\r", ch);
        return;
    }

    obj1 = get_obj_in_list_vis(ch, str1, ch->carrying, 9999);
    if (!obj1) {
        sprintf(buf, "You don't seem to have any %s.\n\r", str1);
        send_to_char(buf, ch);
        return;
    }

    obj2 = get_obj_in_list_vis(ch, str2, ch->carrying, 9999);
    if (!obj2) {
        sprintf(buf, "You don't seem to have any %s.\n\r", str2);
        send_to_char(buf, ch);
        return;
    }

    lev = obj2->obj_flags.level - obj1->obj_flags.level;

    if (lev < -10)
        sprintf(buf, "%s seems much better than %s.\n\r", obj1->short_description,
            obj2->short_description);
    else if (lev < -3)
        sprintf(buf, "%s seems better than %s.\n\r", obj1->short_description,
            obj2->short_description);
    else if (lev <= 3)
        sprintf(buf, "%s and %s seems about the same.\n\r", obj1->short_description,
            obj2->short_description);
    else if (lev < 10)
        sprintf(buf, "%s seems worse than %s.\n\r", obj1->short_description,
            obj2->short_description);
    else
        sprintf(buf, "%s seems much worse than %s.\n\r", obj1->short_description,
            obj2->short_description);

    send_to_char(buf, ch);
    return;
}

static char* stat_defects[] = {
    "weakened",
    "duped",
    "dispirited",
    "clumsy",
    "sickly",
    "bewildered",
    "\n",
};

static char* stat_attrs[] = { "horribly", "strongly", "strongly", "seriously", "seriously", "quite",
    "somewhat", "somewhat", "slightly", "barely", "not at all" };

void report_char_mentals(char_data* ch, char* str, int brief_mode)
{
    // puts the line about ch's condition into str.

    int low_stat1, low_stat2, stat_value1, stat_value2, stat_value;
    int tmp_base;
    low_stat1 = low_stat2 = -1;
    stat_value1 = stat_value2 = 99;

    tmp_base = GET_STR_BASE(ch);
    if (tmp_base <= 0)
        tmp_base = 1;
    stat_value = GET_STR(ch) * 100 / tmp_base;
    if (stat_value <= stat_value1) {
        low_stat2 = low_stat1;
        stat_value2 = stat_value1;
        low_stat1 = 0;
        stat_value1 = stat_value;
    }
    tmp_base = GET_INT_BASE(ch);
    if (tmp_base <= 0)
        tmp_base = 1;
    stat_value = GET_INT(ch) * 100 / tmp_base;
    if (stat_value <= stat_value1) {
        low_stat2 = low_stat1;
        stat_value2 = stat_value1;
        low_stat1 = 1;
        stat_value1 = stat_value;
    }
    tmp_base = GET_WILL_BASE(ch);
    if (tmp_base <= 0)
        tmp_base = 1;
    stat_value = GET_WILL(ch) * 100 / tmp_base;
    if (stat_value <= stat_value1) {
        low_stat2 = low_stat1;
        stat_value2 = stat_value1;
        low_stat1 = 2;
        stat_value1 = stat_value;
    }
    tmp_base = GET_DEX_BASE(ch);
    if (tmp_base <= 0)
        tmp_base = 1;
    stat_value = GET_DEX(ch) * 100 / tmp_base;
    if (stat_value <= stat_value1) {
        low_stat2 = low_stat1;
        stat_value2 = stat_value1;
        low_stat1 = 3;
        stat_value1 = stat_value;
    }
    tmp_base = GET_CON_BASE(ch);
    if (tmp_base <= 0)
        tmp_base = 1;
    stat_value = GET_CON(ch) * 100 / tmp_base;
    if (stat_value <= stat_value1) {
        low_stat2 = low_stat1;
        stat_value2 = stat_value1;
        low_stat1 = 4;
        stat_value1 = stat_value;
    }
    tmp_base = GET_LEA_BASE(ch);
    if (tmp_base <= 0)
        tmp_base = 1;
    stat_value = GET_LEA(ch) * 100 / tmp_base;
    if (stat_value <= stat_value1) {
        low_stat2 = low_stat1;
        stat_value2 = stat_value1;
        low_stat1 = 5;
        stat_value1 = stat_value;
    }

    if (stat_value1 < 0)
        stat_value1 = 0;
    if (stat_value2 < 0)
        stat_value2 = 0;

    if (low_stat1 == -1) {
        strcpy(str, "in top shape");
    } else if ((low_stat2 == -1) || brief_mode) {
        sprintf(str, "%s %s", stat_attrs[stat_value1 / 10], stat_defects[low_stat1]);
    } else {
        sprintf(str, "%s %s and %s %s", stat_attrs[stat_value1 / 10], stat_defects[low_stat1],
            stat_attrs[stat_value2 / 10], stat_defects[low_stat2]);
    }
    return;
}

ACMD(do_stat)
{

    if (GET_LEVEL(ch) < 6) {
        send_to_char("Sorry, you are too young to know your stats.\n\r", ch);
        return;
    }

    auto stat_sum = GET_STR_BASE(ch) + GET_INT_BASE(ch) + GET_WILL_BASE(ch) + GET_DEX_BASE(ch) + GET_CON_BASE(ch) + GET_LEA_BASE(ch);

    if (!wtl || ((wtl->targ1.type == TARGET_NONE) || RETIRED(ch)) || (GET_LEVEL(ch) < LEVEL_GOD)) {
        sprintf(buf,
            "Your fatigue is %d; Your willpower is %d; Your statistic sum is %d\n\rYour "
            "statistics are\n\rStr: %2d/%2d, Int: %2d/%2d, Wil: %2d/%2d, Dex: %2d/%2d, Con: "
            "%2d/%2d, Lea: %2d/%2d.\n\r",
            GET_MENTAL_DELAY(ch) / PULSE_MENTAL_FIGHT, GET_WILLPOWER(ch), stat_sum, GET_STR(ch),
            GET_STR_BASE(ch), GET_INT(ch), GET_INT_BASE(ch), GET_WILL(ch), GET_WILL_BASE(ch),
            GET_DEX(ch), GET_DEX_BASE(ch), GET_CON(ch), GET_CON_BASE(ch), GET_LEA(ch),
            GET_LEA_BASE(ch));

        send_to_char(buf, ch);
        return;
    }
    do_wizstat(ch, argument, wtl, cmd, subcmd);
}

void print_exploits(struct char_data* sendto, char* name)
{
    char str2[255];
    char str3[255];
    char str4[255];
    char str5[255];
    int i, iTotalPk, iDeaths = 0, iNotes = 0;
    exploit_record exploitrec;
    // 1000 lines max of output.
    // that means max of 2000 kills/deaths. certainly enough for a while.
    char buf[80000];
    int iMobDeaths;

    std::vector<exploit_record> records;
    std::string error_message;
    if (!load_exploit_records_for_character(".", name, &records, &error_message)) {
        snprintf(buf, sizeof(buf), "print_exploits: failed to load exploit history for %s: %s", name, error_message.c_str());
        mudlog(buf, NRM, LEVEL_IMMORT, TRUE);
        send_to_char("You have accomplished nothing worthy of note.\n\r", sendto);
        return;
    }

    if (records.empty()) {
        send_to_char("You have accomplished nothing worthy of note.\n\r", sendto);
        return;
    }

    buf[0] = '\0';
    sprintf(buf,
        "Exploits for %s\n\r"
        "Numbers in brackets indicate (your,their) level at time of a "
        "kill\n\r\n\r",
        name);

    // they have entries
    i = 0;
    iTotalPk = 0;

    iMobDeaths = 0;
    for (const exploit_record& loaded_record : records) {
        exploitrec = loaded_record;

        // this entry - date
        strcpy(str2, exploitrec.chtime + 4);
        str2[6] = '\0';

        // year - yeahyeah, it cuts off the first two digits. deal.
        strcpy(str3, exploitrec.chtime + 22);
        str3[2] = '\0';
        i++;

        sprintf(str5, "Unknown record type");

        switch (exploitrec.type) {
        case EXPLOIT_PK: // it's a pk type
            sprintf(str5, "%s, %s: Killed %s (%d,%d)", str2, str3, exploitrec.chVictimName,
                exploitrec.iKillerLevel, exploitrec.iVictimLevel);
            iTotalPk++;
            break;

        case EXPLOIT_DEATH: // it's a death type
            // chvictimname used to store killer name here
            if (exploitrec.iIntParam == 1) {
                sprintf(str5, "%s, %s: * Died to %s (%d,%d)", str2, str3, exploitrec.chVictimName,
                    exploitrec.iVictimLevel, exploitrec.iKillerLevel);
                iDeaths++;
            } else
                sprintf(str5, "%s, %s: Died to %s (%d,%d)", str2, str3, exploitrec.chVictimName,
                    exploitrec.iVictimLevel, exploitrec.iKillerLevel);
            break;

        case EXPLOIT_LEVEL:
            sprintf(str5, "%s, %s: Obtained level %d", str2, str3, exploitrec.iIntParam);
            break;

        case EXPLOIT_STAT:
            sprintf(str5, "%s, %s: L%d: Stat inc (%s)", str2, str3, exploitrec.iIntParam,
                exploitrec.chVictimName);
            break;

        case EXPLOIT_BIRTH:
            sprintf(str5, "%s, %s: Character Created", str2, str3);
            break;

        case EXPLOIT_MOBDEATH:
            sprintf(str5, "%s, %s: Mobdied: %s", str2, str3, exploitrec.chVictimName);
            iMobDeaths++;
            break;

        case EXPLOIT_RETIRED:
            sprintf(str5, "%s, %s: Retired", str2, str3);
            break;

        case EXPLOIT_ACHIEVEMENT:
            sprintf(str5, "%s, %s: %s", str2, str3, exploitrec.chVictimName);
            break;

        case EXPLOIT_NOTE:
            sprintf(str5, "%s, %s: !%s", str2, str3, exploitrec.chVictimName);
            iNotes++;
            break;

        case EXPLOIT_POISON:
            sprintf(str5, "%s, %s: Died to Poison", str2, str3);
            break;

        case EXPLOIT_REGEN_DEATH:
            sprintf(str5, "%s, %s: Died to Injuries", str2, str3);
            break;
        }
        // an output line - first column
        if (i == 1)
            sprintf(str4, "%-39s", str5);
        else {
            sprintf(str4, "%s%-39s\n\r", str4, str5);
            // add to output buffer
            sprintf(buf, "%s%s", buf, str4);
            i = 0;
        }
    }
    if (i == 1) {
        // add to output buffer
        sprintf(buf, "%s%s\n\r", buf, str4);
    }

    if (iTotalPk == 1)
        sprintf(buf, "%s\n\rTotal: 1 pkill, ", buf);
    else
        sprintf(buf, "%s\n\rTotal: %d pkills, ", buf, iTotalPk);

    if (iDeaths == 1) {
        sprintf(buf, "%s1 pdeath, ", buf);
    } else {
        sprintf(buf, "%s%d pdeaths, ", buf, iDeaths);
    }

    if (iMobDeaths == 1) {

        sprintf(buf, "%s1 mobdeath, ", buf);

    } else {

        sprintf(buf, "%s%d mobdeaths, ", buf, iMobDeaths);
    }

    if (iNotes == 1)
        sprintf(buf, "%s1 note.\n\r\n\r", buf);
    else
        sprintf(buf, "%s%d notes.\n\r\n\r", buf, iNotes);

    page_string(sendto->desc, buf, 1);
    return;
}

ACMD(do_exploits)
{
    print_exploits(ch, GET_NAME(ch));
    return;
}

/*
 * Arrarys used for identify, this is just a temporary
 * place of residence.
 */
char* light_messages[] = {

    "extremely weak, and will not last very long",
    "weak, and will not last very long",
    "quite weak, and will not last long",
    "fairly strong, and will last a few days",
    "quite strong, and will last just under a week",
    "very strong, and could last a number of weeks",
    "magical in nature, and could last a number of months",
    "magical in nature, and may never go out",

};

char* food_messages[] = {

    "is barely a morsel of food, and will\r\ndo little to aid against the pangs of hunger",
    "is not very filling, and will do little\r\nto keep hunger at bay",
    "is fairly filling, and will keep hunger\r\nat bay for a short few hours",
    "is quite filling, and would do as a \r\nsmall meal",
    "is rather filling, and would do as a \r\nlarge meal",
    "is very filling, and will keep you full\r\nfor most of the day",
    "is very filling, and will keep you full \r\nall day long",
    "is extremely filling, and will keep you \r\nfull all day and night",

};
char* wear_messages[] = {

    "taken",
    "worn on your finger",
    "worn around the neck",
    "worn on the body",
    "worn on the head",
    "worn on the legs",
    "worn on the feet",
    "worn on the hands",
    "worn on the arms",
    "used in the off hand",
    "worn about the body",
    "worn about the waiste",
    "worn around the wrist",
    "wielded",
    "held",
    "used as a light source",
    "worn on the back",
    "worn on a belt",
};

char* material_messages[] = {

    "of the usual stuff",
    "of cloth",
    "of leather",
    "of chain",
    "of metal",
    "of wood",
    "of stone",
    "of crystal",
    "of gold",
    "of silver",
    "from precious mithril",
    "of fur",
    "of glass",
    "from organic material",
    "Blu6rp",
    "Blurp3",
};

char* item_messages[] = {
    "Unidentified",
    "light source",
    "scroll",
    "wand",
    "staff",
    "weapon",
    "fire weapon",
    "missile",
    "piece of treasure",
    "piece of armour",
    "small potion",
    "worn",
    "item", /*formally known as other*/
    "item", /* trash */
    "trap",
    "container",
    "parchment", /* note */
    "liquid container",
    "key",
    "piece of food",
    "sum of money",
    "pen",
    "boat",
    "fountain",
    "shield",
    "lever",
};

char* extra_messages[] = {

    "It glows brigtly",
    "It hums softly",
    "Dark",
    "It appears to be breakable",
    "It is evil",
    "Error : Object is invisible how are you identifying this? Pleae Report",
    "It is magical in nature",
    "It does not want to be dropped",
    "It appears to be broken",
    "It cannot be used to aid the forces of light",
    "It cannot be used to aid the shadow",
    "It cannot be used by those who remain neutral in the struggle for Arda",
    "It cannot be stored for rent",
    "ERROR: Please report",
    "ERROR: Please report",
    "ERROR: Please report",

};

/*
 * This array is used as a generic value_flag display
 * for all items, with the exception of food/light.
 */
char* value_array[][5] = {

    {
        "",
        "",
        "",
        "",
        "",
    },
    {
        "",
        "",
        "",
        "",
        "",
    }, /* Light source handled by do_display_light */
    {
        "",
        "",
        "",
        "",
        "",
    }, /* Scroll not used */
    {
        "",
        "",
        "",
        "",
        "",
    }, /* Wand not used */
    {
        "",
        "",
        "",
        "",
        "",
    }, /* Staff not used*/
    {
        "Offensive Bonus",
        "Parry Bonus    ",
        "Bulk           ",
        "",
        "",
    }, /* Weapons Display */
    {
        "",
        "",
        "",
        "",
        "",
    }, /* Fire weapons ?, what the hell is this anyway */
    {
        "To Hit",
        "To Damage",
        "Character ID",
        "Break Percentage",
        "",
    }, /* Missile Weapon */
    {
        "",
        "",
        "",
        "",
        "",
    }, /* Treasure */
    {
        "",
        "Min Absorbtion",
        "Encumberance  ",
        "Dodge         ",
        "",
    }, /* Armour */
    {
        "",
        "",
        "",
        "",
        "",
    }, /* small potion */
    {
        "",
        "",
        "",
        "",
        "",
    }, /* worn ? */
    {
        "",
        "",
        "",
        "",
        "",
    }, /* other */
    {
        "",
        "",
        "",
        "",
        "",
    }, /* Trash */
    {
        "",
        "",
        "",
        "",
        "",
    }, /* Trap */
    {
        "Capacity",
        "Locktype",
        "",
        "",
        "",
    }, /* Container */
    {
        "",
        "",
        "",
        "",
        "",
    }, /* note */
    {
        "Capacity    ",
        "Ammount Left",
        "",
        "",
        "",
    }, /* Drink Container */
    {
        "",
        "",
        "",
        "",
        "",
    }, /* Key */
    {
        "",
        "",
        "",
        "",
        "",
    }, /* Food, handled by do_food_display */
    {
        "",
        "",
        "",
        "",
        "",
    }, /* Money */
    {
        "",
        "",
        "",
        "",
        "",
    }, /* Pen */
    {
        "",
        "",
        "",
        "",
        "",
    }, /* Money */
    {
        "",
        "",
        "",
        "",
        "",
    }, /* Fountain */
    {
        "Dodge Bonus ",
        "Parry Bonus ",
        "Encumberance",
        "",
        "",
    }, /* Shield */
    {
        "",
        "",
        "",
        "",
        "",
    }, /* Lever */
};

char* weapon_types[] = {

    "Error, Unsed weapon type, contact Imms",
    "Error, Unsed weapon type, contact Imms",
    "whipping",
    "slashing",
    "two-handed slashing",
    "flailing",
    "bludgeoning",
    "bludgeoning",
    "cleaving",
    "two-handed cleaving",
    "stabbing",
    "piercing",
    "smiting",
    "shooting",
    "shooting",
};

/*
 * A crude function used only twice in identify.
 * Used to get value ranges for food and light.
 */
int get_value_ranges(int range, int value1, int value2, int value3, int value4, int value5,
    int value6, int value7, int value8)
{

    int range_value;

    if (range <= value2 && range >= value1)
        range_value = 0;
    else if (range <= value3)
        range_value = 1;
    else if (range <= value4)
        range_value = 2;
    else if (range <= value5)
        range_value = 3;
    else if (range <= value6)
        range_value = 4;
    else if (range <= value7)
        range_value = 5;
    else if (range < value8)
        range_value = 6;
    else
        range_value = 7;
    return range_value;
}

/*
 * The two functions "do_food_display" and "do_light_display"
 * are used to display the value_flags of food and light items.
 * They are not handled by the generic value_array, as messages
 * giving the filling and lenght values (respectively) of these
 * items are handled by two seperate arrays of Pros' ( Light
 * messages and Food messages).
 */

void do_food_display(struct char_data* ch, struct obj_data* j)
{

    int make_full, message_num;

    make_full = j->obj_flags.value[0];
    if (j->obj_flags.value[3] != 0)
        sprintf(buf1, "However it seems to be of"
                      " a less than wholesome quality.");
    else
        sprintf(buf1, "It is also of a wholesome quality.");

    message_num = get_value_ranges(make_full, 0, 2, 4, 6, 10, 14, 18, 24);
    sprintf(buf, "%s %s. %s\r\n", j->short_description, food_messages[message_num], buf1);
    send_to_char(buf, ch);
}

void do_light_display(struct char_data* ch, struct obj_data* j)
{

    int duration_range, message_num;

    duration_range = j->obj_flags.value[2];
    message_num = get_value_ranges(duration_range, 0, 6, 12, 19, 50, 150, 500, 1000);
    sprintf(buf, "This source of light is %s.\r\n", light_messages[message_num]);
    send_to_char(buf, ch);
}

void do_flag_values_display(struct char_data* ch, struct obj_data* j)
{

    int i;

    if (GET_ITEM_TYPE(j) == ITEM_ARMOR) {
        sprintf(buf, "Absorbtion\t\t %d.\r\n", armor_absorb(j));
        send_to_char(buf, ch);
    }

    for (i = 0; i <= 4; i++) {
        sprintf(buf, "%s", value_array[GET_ITEM_TYPE(j)][i]);
        if (value_array[GET_ITEM_TYPE(j)][i] != "") {
            send_to_char(buf, ch);

            if (j->obj_flags.value[i] < 0) /* Checks for negative for display purposes */
                sprintf(buf, "\t %d.\r\n", j->obj_flags.value[i]);
            else
                sprintf(buf, "\t  %d.\r\n", j->obj_flags.value[i]);
            send_to_char(buf, ch);
        }
    }
}

void do_weapon_display(struct char_data* ch, struct obj_data* j)
{

    sprintf(buf1, weapon_types[j->obj_flags.value[3]]);
    sprintf(buf,
        "The weapon you hold is a %s weapon.\r\n"
        "\n\rDamage Rating \t   %d/10.\r\n",
        buf1, get_weapon_damage(j));
    send_to_char(buf, ch);
}

/*
 * Identify object is what the spell Identify uses to glean objects stats
 * It uses a new selection of arrays to better describe an items information
 * in a more user-friendly manner.
 * Information given is as follows
 * - Object description.
 * - Objects action description.
 * - Item type, Material, Weight, and where its to be worn/wielded/used.
 * It then uses the test_array to display all relative "extra values"
 * the item has, based on its type.
 *
 * Several new arrays were used for this spell, most of which are
 * replicas to existing arrays, however the elements contain different
 * descriptive text. The arrays are as follows :
 *   - Wear_messages.
 *   - Material_messages.
 *   - Light_messages.
 *   - Food_messages.
 *   - Extra_messages. (used for extra item flags.)
 *   - Item_messages.
 *   - value_array. (used to display value_flag messages.
 *   - weapon_type.
 */

void do_identify_object(struct char_data* ch, struct obj_data* j)
{

    char found;
    int i;

    sprintf(buf,
        "   You feel certain the object you have"
        " is %s. \r\n",
        j->short_description ? j->short_description
                             : "No object description found, please report. ");
    send_to_char(buf, ch);

    sprintf(buf, "%s \r\n",
        j->action_description ? j->action_description
                              : "No object description, please report. \r\n");
    send_to_char(buf, ch);

    sprintbit(j->obj_flags.wear_flags, wear_messages, buf2, 2);
    sprintf(buf,
        "This %s is made %s, and weighs %.1flbs.\r\n"
        "This %s can be%s\r\n",
        item_messages[GET_ITEM_TYPE(j)],
        j->obj_flags.material >= 0 && j->obj_flags.material < num_of_object_materials
            ? material_messages[j->obj_flags.material]
            : "an unknown substance",
        j->obj_flags.weight / 100., item_messages[GET_ITEM_TYPE(j)], buf2);
    send_to_char(buf, ch);

    /*
     * If an object type_flag is either Light or Food, its value_flags
     * are handled by two seperate fucntions, everything else
     * is handled via the value_array, which is the default
     * for the below switch.
     */

    switch (j->obj_flags.type_flag) {
    case ITEM_LIGHT:
        do_light_display(ch, j);
        break;
    case ITEM_FOOD:
        do_food_display(ch, j);
        break;
    case ITEM_WEAPON:
        do_weapon_display(ch, j);
        break;
    }
    do_flag_values_display(ch, j);

    sprintbit(j->obj_flags.extra_flags, extra_messages, buf1, 1);
    sprintf(buf, "\r\nThis item %s\r\n", buf1);
    send_to_char(buf, ch);

    found = 0;

    for (i = 0; i < MAX_OBJ_AFFECT; i++)
        if (j->affected[i].modifier) {
            sprinttype(j->affected[i].location, apply_types, buf2);
            sprintf(buf, "%s %+d to %s", found++ ? "" : "", j->affected[i].modifier, buf2);
            if (found == 1)
                send_to_char("\r\nThis item has the following affections.\r\n", ch);
            send_to_char(buf, ch);
            send_to_char("\r\n", ch);
        }
    if (!found)
        send_to_char("", ch);

    send_to_char("\r\n", ch);
}

void do_details(char_data* character, char* argument, waiting_type* wait_list, int command,
    int sub_command)
{
    const char* SPEC_FLAG = "spec";
    const char* GROUP_FLAG = "group";
    const char* DAMAGE_FLAG = "damage";
    const char* RESET_FLAG = "reset";

    if (wait_list && (wait_list->targ1.type == TARGET_TEXT)) {
        const char* details_argument = argument + 1;

        int len = strlen(details_argument);
        if (len == 0)
            return;

        if (strstr(details_argument, SPEC_FLAG)) {
            std::string spec_data = character->extra_specialization_data.to_string(*character);
            send_to_char(spec_data.c_str(), character);
        } else if (strstr(details_argument, DAMAGE_FLAG)) {
            if (strstr(details_argument, RESET_FLAG)) {
                character->damage_details.reset();

                for (follow_type* follower = character->followers; follower;
                     follower = follower->next) {
                    char_data* follow_char = follower->follower;
                    if (utils::is_npc(*follow_char) && utils::is_affected_by(*follow_char, AFF_CHARM)) {
                        follow_char->damage_details.reset();
                    }
                }

                send_to_char("Damage details have been reset.\r\n", character);
            } else {
                std::string damage_data = character->damage_details.get_damage_report(character);
                send_to_char(damage_data.c_str(), character);

                for (follow_type* follower = character->followers; follower;
                     follower = follower->next) {
                    const char_data* follow_char = follower->follower;
                    if (utils::is_npc(*follow_char) && utils::is_affected_by(*follow_char, AFF_CHARM)) {
                        std::string follower_damage_data = follow_char->damage_details.get_damage_report(follow_char);
                        send_to_char("\n\r", character);
                        send_to_char(follower_damage_data.c_str(), character);
                    }
                }
            }
        } else if (strstr(details_argument, GROUP_FLAG)) {
            group_data* group = character->group;
            if (group) {
                if (strstr(details_argument, RESET_FLAG)) {
                    if (group->is_leader(character)) {
                        group->reset_damage();
                    } else {
                        send_to_char("Only the group leader can reset damage details.\r\n",
                            character);
                    }
                } else {
                    std::string damage_data = group->get_damage_report();
                    send_to_char(damage_data.c_str(), character);
                }
            } else {
                send_to_char("You need to be in a group to get group damage details.\r\n",
                    character);
            }
        } else {
            send_to_char("Accepted arguments: spec, group, damage (optional: reset) \r\n",
                character);
        }
    } else {
        send_to_char("Accepted arguments: spec, group, damage (optional: reset) \r\n", character);
    }
}
